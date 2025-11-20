#pragma once
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "stdtypes.h"

typedef U32 ContainerID;
typedef struct Login2IntershardCommandInfo Login2IntershardCommandInfo;
typedef struct Login2InterShardDestination Login2InterShardDestination;
typedef struct Login2IntershardCommandRemoteState Login2IntershardCommandRemoteState;
typedef struct ParseTable ParseTable;

// This is the state in the remote shard.
AUTO_STRUCT;
typedef struct Login2IntershardCommandRemoteState
{
    Login2IntershardCommandInfo *commandInfo;   AST(UNOWNED)
    Login2InterShardDestination *returnDestination;
    void *argStruct;                            NO_AST
} Login2IntershardCommandRemoteState;

AUTO_STRUCT;
typedef struct Login2IntershardArgU32
{
    U32 value;
} Login2IntershardArgU32;

// Handler should return true to have argStruct freed automatically.  If the handler returns false, it is responsible for freeing the argStruct.
typedef bool (*RemoteShardCommandHandler)(const char *commandName, void *argStruct, Login2IntershardCommandRemoteState *commandRemoteState);
typedef void (*IntershardCommandCB)(const char *commandName, bool success, const char *errorString, void *returnStruct, void *userData);

bool aslLogin2_RegisterIntershardCommand(const char *commandName, ParseTable argParseTable[], ParseTable retParseTable[], RemoteShardCommandHandler commandHandler, IntershardCommandCB commandCB);
bool aslLogin2_CallIntershardCommand(const char *shardName, U32 instanceID, const char *commandName, void *argStruct, void *userData);

// This function is called (directly or indirectly) by the handler on the remote shard when the command is done.  
//  It returns the results to the calling shard.
//  The caller is responsible for freeing returnStruct.
void aslLogin2_IntershardCommandRemoteComplete(bool success, void *returnStruct, const char *errorString, Login2IntershardCommandRemoteState *commandRemoteState);

