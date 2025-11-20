/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLogin2_IntershardCommands.h"
#include "aslLogin2_Error.h"
#include "Login2ServerCommon.h"
#include "textparser.h"
#include "stdtypes.h"
#include "earray.h"
#include "StashTable.h"
#include "StringCache.h"
#include "GlobalTypeEnum.h"

#include "AutoGen/aslLogin2_IntershardCommands_h_ast.h"
#include "AutoGen/aslLogin2_IntershardCommands_c_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"


// This is the state in the calling shard.
AUTO_STRUCT;
typedef struct Login2IntershardCommandState
{
    U64 returnToken;                            AST(KEY)
    void *userData;                             NO_AST
    Login2IntershardCommandInfo *commandInfo;   NO_AST
} Login2IntershardCommandState;

AUTO_STRUCT;
typedef struct Login2IntershardCommandInfo
{
    STRING_POOLED commandName;                  AST(KEY POOL_STRING)
    ParseTable *argParseTable;                  NO_AST
    ParseTable *retParseTable;                  NO_AST
    RemoteShardCommandHandler commandHandler;   NO_AST
    IntershardCommandCB commandCB;              NO_AST 
} Login2IntershardCommandInfo;

static EARRAY_OF(Login2IntershardCommandInfo) s_commandInfos = NULL;
static StashTable s_commandStates = NULL;

// This function is called (directly or indirectly) by the handler on the remote shard when the command is done.  
//  It returns the results to the calling shard.
//  The caller is responsible for freeing returnStruct.
void
aslLogin2_IntershardCommandRemoteComplete(bool success, void *returnStruct, const char *errorString, Login2IntershardCommandRemoteState *commandRemoteState)
{
    static char *s_returnString = NULL;

    estrClear(&s_returnString);

    if ( returnStruct )
    {
        if ( !ParserWriteText(&s_returnString, commandRemoteState->commandInfo->retParseTable, returnStruct, 0, 0, 0) )
        {
            aslLogin2_Log("IntershardCommandRemoteComplete: failed to write return struct.");
        }
    }

    RemoteCommand_Intershard_aslLogin2_IntershardCommandReturn(commandRemoteState->returnDestination->shardName, 
        commandRemoteState->returnDestination->serverType, commandRemoteState->returnDestination->serverID, success, errorString, s_returnString,
        commandRemoteState->returnDestination->requestToken);

    StructDestroy(parse_Login2IntershardCommandRemoteState, commandRemoteState);
}

// This is the function called on the remote shard that parses the arg string and dispatches the command to the correct handler.
AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(APPSERVER);
void
aslLogin2_IntershardCommandRemote(ACMD_OWNABLE(Login2InterShardDestination) ppReturnDestination, const char *commandName, const char *argString)
{
    Login2IntershardCommandInfo *commandInfo;
    Login2IntershardCommandRemoteState *commandRemoteState;
    void *argStruct = NULL;
    
    commandInfo = eaIndexedGetUsingString(&s_commandInfos, commandName);
    if ( commandInfo == NULL )
    {
        aslLogin2_Log("IntershardCommandRemote: unknown command %s", commandName);
        return;
    }

    commandRemoteState = StructCreate(parse_Login2IntershardCommandRemoteState);

    // Take ownership of the return destination struct.
    commandRemoteState->returnDestination = *ppReturnDestination;
    *ppReturnDestination = NULL;

    commandRemoteState->commandInfo = commandInfo;

    if ( argString )
    {
        argStruct = StructCreateVoid(commandInfo->argParseTable);
        if ( !ParserReadText(argString, commandInfo->argParseTable, argStruct, 0) )
        {
            aslLogin2_Log("IntershardCommandRemote: failed to parse arg string: %s", argString);
            aslLogin2_IntershardCommandRemoteComplete(false, NULL, "IntershardCommandRemote: failed to parse arg string", commandRemoteState);
            return;
        }
    }

    commandRemoteState->argStruct = argStruct;

    if ( (*commandInfo->commandHandler)(commandInfo->commandName, argStruct, commandRemoteState) )
    {
        // Only destroy the arg struct if it exists and the handler asked us to.
        if ( argStruct )
        {
            StructDestroyVoid(commandInfo->argParseTable, argStruct);
        }
    }
    // Don't do anything with any argStruct or commandRemoteState after the above call to the handler, 
    //  because they may be freed before the handler returns.
}

static void
InterShardCommandComplete(Login2IntershardCommandInfo *commandInfo, bool success, const char *returnString, const char *errorString, void *userData)
{
    void *returnStruct = NULL;

    if ( returnString )
    {
        returnStruct = StructCreateVoid(commandInfo->retParseTable);
        ParserReadText(returnString, commandInfo->retParseTable, returnStruct, 0);
    }

    (* commandInfo->commandCB)(commandInfo->commandName, success, errorString, returnStruct, userData);

    // NOTE - the callback function owns the return struct and must free it.
}

// This is the function called on the remote shard that parses the arg string and dispatches the command to the correct handler.
AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(APPSERVER);
void
aslLogin2_IntershardCommandReturn(bool success, const char *errorString, const char *returnString, U64 returnToken)
{
    Login2IntershardCommandState *commandState = NULL;
    void *returnStruct = NULL;

    // Find the state for this command.  Remove it from the stash table.
    if ( !stashRemovePointer(s_commandStates, &returnToken, &commandState) )
    {
        aslLogin2_Log("IntershardCommandReturn didn't find command state.  returnToken: %llu", returnToken);
        return;
    }

    // Parse the return string.
    if ( returnString )
    {
        returnStruct = StructCreateVoid(commandState->commandInfo->retParseTable);
        if ( !ParserReadText(returnString, commandState->commandInfo->retParseTable, returnStruct, 0) )
        {
            aslLogin2_Log("IntershardCommandReturn error parsing return string.  returnString: %s", returnString);
        }
    }

    // Call the callback.
    InterShardCommandComplete(commandState->commandInfo, success, returnString, errorString, commandState->userData);

    // Free the state struct.
    StructDestroy(parse_Login2IntershardCommandState, commandState);
}

bool
aslLogin2_CallIntershardCommand(const char *shardName, U32 instanceID, const char *commandName, void *argStruct, void *userData)
{
    static char *s_argString = NULL;
    Login2IntershardCommandInfo *commandInfo;
    Login2IntershardCommandState *commandState;
    Login2InterShardDestination myDestination = {0};

    commandInfo = eaIndexedGetUsingString(&s_commandInfos, commandName);
    if ( commandInfo == NULL )
    {
        aslLogin2_Log("CallIntershardCommand: unknown command name %s", commandName);
        return false;
    }

    estrClear(&s_argString);

    if ( argStruct )
    {
        if ( !ParserWriteText(&s_argString, commandInfo->argParseTable, argStruct, 0, 0, 0) )
        {
            InterShardCommandComplete(commandInfo, false, NULL, "CallIntershardCommand: failed to write arg string", userData);
            return true;
        }
    }

    commandState = StructCreate(parse_Login2IntershardCommandState);
    commandState->returnToken = Login2_GenerateRequestToken();
    commandState->userData = userData;
    commandState->commandInfo = commandInfo;

    // Save command state.
    stashAddPointer(s_commandStates, &commandState->returnToken, commandState, false);

    // Fill in the destination struct, which will tell the remote shard where to send the response.
    Login2_FillDestinationStruct(&myDestination, commandState->returnToken);

    RemoteCommand_Intershard_aslLogin2_IntershardCommandRemote(shardName, GLOBALTYPE_LOGINSERVER, instanceID, &myDestination, commandName, s_argString);
    return true;
}

bool 
aslLogin2_RegisterIntershardCommand(const char *commandName, ParseTable argParseTable[], ParseTable retParseTable[], RemoteShardCommandHandler commandHandler, IntershardCommandCB commandCB)
{
    Login2IntershardCommandInfo * commandInfo;

    if ( commandName == NULL || argParseTable == NULL || retParseTable == NULL || commandHandler == NULL || commandCB == NULL )
    {
        return false;
    }

    if ( s_commandInfos == NULL )
    {
        // Initialize global tables.
        eaIndexedEnable(&s_commandInfos, parse_Login2IntershardCommandInfo);
        s_commandStates = stashTableCreateFixedSize(128, sizeof(U64));
    }

    if ( eaIndexedGetUsingString(&s_commandInfos, commandName) != NULL )
    {
        // Command already exists.
        return false;
    }

    commandInfo = StructCreate(parse_Login2IntershardCommandInfo);
    commandInfo->commandName = allocAddString(commandName);
    commandInfo->argParseTable = argParseTable;
    commandInfo->retParseTable = retParseTable;
    commandInfo->commandHandler = commandHandler;
    commandInfo->commandCB = commandCB;

    eaIndexedAdd(&s_commandInfos, commandInfo);
    return true;
}

#include "AutoGen/aslLogin2_IntershardCommands_h_ast.c"
#include "AutoGen/aslLogin2_IntershardCommands_c_ast.c"