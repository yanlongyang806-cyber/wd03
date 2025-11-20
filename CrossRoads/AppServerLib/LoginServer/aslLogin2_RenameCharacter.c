/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLogin2_RenameCharacter.h"
#include "aslLogin2_IntershardCommands.h"
#include "aslLogin2_Error.h"
#include "aslLogin2_Util.h"

#include "TransactionOutcomes.h"
#include "objTransactions.h"
#include "GlobalTypeEnum.h"
#include "StringCache.h"
#include "timing.h"
#include "accountnet.h"
#include "AccountProxyCommon.h"
#include "microtransactions_common.h"
#include "LoggedTransactions.h"

#include "AutoGen/aslLogin2_IntershardCommands_h_ast.h"
#include "AutoGen/aslLogin2_RenameCharacter_c_ast.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"

#define RENAME_CHARACTER_COMMAND_NAME "Login2RenameCharacter"

AUTO_STRUCT;
typedef struct RenameCharacterState
{
    ContainerID playerID;
    STRING_POOLED shardName;                AST(POOL_STRING)

    U32 timeStarted;

    // Completion callback data.
    RenameCharacterCB cbFunc;               NO_AST
    U64 userData;                           NO_AST
} RenameCharacterState;

AUTO_STRUCT;
typedef struct RenameCharacterArgStruct
{
    ContainerID playerID;
    ContainerID accountID;
    GlobalType puppetType;
    ContainerID puppetID;
    const char *newName;
    bool badName;
} RenameCharacterArgStruct;

AUTO_STRUCT;
typedef struct RenameCharacterInShardState
{
    Login2IntershardCommandRemoteState *commandRemoteState; AST(UNOWNED)
    RenameCharacterArgStruct *argStruct;
} RenameCharacterInShardState;

// This callback runs on the remote shard.  It cleans up after the character rename and returns status to the originating shard.
static void 
RenameCharacterInShardCB(TransactionReturnVal *returnVal, RenameCharacterInShardState *renameCharacterInShardState)
{
    const char *errorString = NULL;
    bool success = true;
    Login2IntershardArgU32 returnStruct = {0};

    if ( returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS )
    {
        success = false;
        errorString = "Rename character command failed in remote shard.";
    }

    returnStruct.value = renameCharacterInShardState->argStruct->playerID;

    aslLogin2_IntershardCommandRemoteComplete(success, &returnStruct, errorString, renameCharacterInShardState->commandRemoteState);

    // Clean up state.
    StructDestroy(parse_RenameCharacterInShardState, renameCharacterInShardState);
}

static void
RenameCharacterWithKeyValue_CB(AccountKeyValueResult result, U32 accountID, SA_PARAM_NN_STR const char *key, ContainerID containerID, SA_PARAM_NN_VALID RenameCharacterInShardState *renameCharacterInShardState)
{
    if ( result == AKV_SUCCESS )
    {
        
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnVal("Login2RenameCharacterWithASCost", RenameCharacterInShardCB, renameCharacterInShardState);
        AutoTrans_asl_tr_SetNameWithCost(returnVal, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, renameCharacterInShardState->argStruct->playerID,
            renameCharacterInShardState->argStruct->puppetType, renameCharacterInShardState->argStruct->puppetID, 
            GLOBALTYPE_GAMEACCOUNTDATA, renameCharacterInShardState->argStruct->accountID, 
            GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID, key, renameCharacterInShardState->argStruct->newName);
    }
    else
    {
        Login2IntershardArgU32 returnStruct = {0};

        returnStruct.value = renameCharacterInShardState->argStruct->playerID;

        aslLogin2_IntershardCommandRemoteComplete(false, &returnStruct, "Rename character command failed in remote shard.", renameCharacterInShardState->commandRemoteState);

        // Clean up state.
        StructDestroy(parse_RenameCharacterInShardState, renameCharacterInShardState);
    }
}

bool
RenameCharacterCommandHandler(const char *commandName, RenameCharacterArgStruct *argStruct, Login2IntershardCommandRemoteState *commandRemoteState)
{
    TransactionReturnVal *returnVal;
    RenameCharacterInShardState *renameCharacterInShardState = StructCreate(parse_RenameCharacterInShardState);

    renameCharacterInShardState->commandRemoteState = commandRemoteState;
    renameCharacterInShardState->argStruct = StructClone(parse_RenameCharacterArgStruct, argStruct);

    // Run transaction to rename the character.
    if ( argStruct->badName )
    {
        returnVal = objCreateManagedReturnVal(RenameCharacterInShardCB, renameCharacterInShardState);
        AutoTrans_asl_tr_ResetBadName(returnVal, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, argStruct->playerID, 
            argStruct->puppetType, argStruct->puppetID, argStruct->newName);
    }
    else
    {
        if ( gConf.bDontAllowGADModification )
        {
            APChangeKeyValue(argStruct->accountID, MicroTrans_GetRenameTokensASKey(), -1, RenameCharacterWithKeyValue_CB, renameCharacterInShardState);
        }
        else
        {
            returnVal = LoggedTransactions_CreateManagedReturnVal("Login2RenameCharacterWithGADCost", RenameCharacterInShardCB, renameCharacterInShardState);
            AutoTrans_asl_tr_SetNameWithCost(returnVal, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, argStruct->playerID,
                argStruct->puppetType, argStruct->puppetID, GLOBALTYPE_GAMEACCOUNTDATA, argStruct->accountID, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, 0, NULL, argStruct->newName);
        }
    }

    // Free argStruct.
    return true;
}

// Handle the completion of the character rename operation.  Log any errors and call the callback to return results to the caller.
static void
RenameCharacterComplete(bool success, const char *errorString, RenameCharacterState *renameCharacterState)
{
    // Log success or failure
    if ( !success )
    {
        aslLogin2_Log("aslLogin2_RenameCharacter failed for playerID %u.\n%s", renameCharacterState->playerID, errorString);
    }
    else
    {
        aslLogin2_Log("aslLogin2_RenameCharacter succeeded for playerID %u.", renameCharacterState->playerID);
    }

    // Notify the caller that we are done.
    if ( renameCharacterState->cbFunc )
    {
        (* renameCharacterState->cbFunc)(success, renameCharacterState->userData);
    }

    // Clean up state.
    StructDestroy(parse_RenameCharacterState, renameCharacterState);
}

static void
RenameCharacterCommandCB(const char *commandName, bool success, const char *errorString, Login2IntershardArgU32 *returnStruct, RenameCharacterState *renameCharacterState)
{
    devassert(returnStruct->value == renameCharacterState->playerID);
    RenameCharacterComplete(success, errorString, renameCharacterState);
}

void 
aslLogin2_RenameCharacter(ContainerID playerID, ContainerID accountID, GlobalType puppetType, ContainerID puppetID, const char *newName, bool badName, const char *shardName, RenameCharacterCB cbFunc, U64 userData)
{
    RenameCharacterState *renameCharacterState = StructCreate(parse_RenameCharacterState);
    RenameCharacterArgStruct argStruct = {0};

    renameCharacterState->playerID = playerID;
    renameCharacterState->shardName = allocAddString(shardName);
    renameCharacterState->timeStarted = timeSecondsSince2000();
    renameCharacterState->cbFunc = cbFunc;
    renameCharacterState->userData = userData;

    argStruct.playerID = playerID;
    argStruct.accountID = accountID;
    argStruct.puppetType = puppetType;
    argStruct.puppetID = puppetID;
    argStruct.badName = badName;
    argStruct.newName = newName;

    if ( !aslLogin2_CallIntershardCommand(shardName, aslLogin2_GetRandomServerOfTypeInShard(shardName, GLOBALTYPE_LOGINSERVER), RENAME_CHARACTER_COMMAND_NAME, &argStruct, renameCharacterState) )
    {
        RenameCharacterComplete(true, "CallIntershardCommand failed", renameCharacterState);
    }

    return;
}

AUTO_RUN;
void
aslLogin2_RenameCharacterAutoRun(void)
{
    aslLogin2_RegisterIntershardCommand(RENAME_CHARACTER_COMMAND_NAME, parse_RenameCharacterArgStruct, parse_Login2IntershardArgU32, RenameCharacterCommandHandler, RenameCharacterCommandCB);
}

#include "AutoGen/aslLogin2_RenameCharacter_c_ast.c"