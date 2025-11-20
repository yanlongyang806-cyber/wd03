/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "WorldVariable.h"


#define SHARD_VAR_FILENAME "defs/config/shardvariables.def"

// The old/regular container is used to store possibly-often-changed vars that are only used by some maps. They will not be available on maps which do not request them.
// The new Broadcast container is used for vars that are broadcast to ALL maps. This is expensive, so it should only be used for infrequently-changed stuff.
//   (This is the old default for Champions)

#define SHARD_VAR_MAPREQUESTED_CONTAINER_ID  1
#define SHARD_VAR_BROADCAST_CONTAINER_ID  2

// This is the structure stored in the Object DB containers
AUTO_STRUCT AST_CONTAINER;
typedef struct ShardVariableContainer
{
	const ContainerID id;									AST( PERSIST SUBSCRIBE KEY )
	const U32 uClock;										AST( PERSIST SUBSCRIBE NAME("Clock") )
	CONST_EARRAY_OF(WorldVariableContainer) eaWorldVars;	AST( PERSIST SUBSCRIBE NAME("ShardVariable") FORCE_CONTAINER )
} ShardVariableContainer;

extern ParseTable parse_ShardVariableContainer[];
#define TYPE_parse_ShardVariableContainer ShardVariableContainer


// These are the structures used to read the "ShardVariables.def" file

// (Wrapper for WorldVariable so we can add extra data)
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct ShardWorldVarDef
{
	WorldVariable WorldVar;		AST( EMBEDDED_FLAT )
	
	U32 bBroadcast : 1;			// WARNING! This mimics the old champions behaviour and deprecates the gConf for it.
								//   It can be very expensive and should ONLY be used for variables that will change Very Infrequently(tm)
} ShardWorldVarDef;

AUTO_STRUCT;
typedef struct ShardVariableDefs
{
	ShardWorldVarDef **eaVariables;   AST( NAME("ShardVariableDef") )
} ShardVariableDefs;

extern ParseTable parse_ShardVariableDefs[];
#define TYPE_parse_ShardVariableDefs ShardVariableDefs

// This is a structure tracked on the Game Server
typedef struct ShardVariable
{
	// The shard variable's name
	const char *pcName;

	// The variable default value
	WorldVariable *pDefault;

	// The clock of the last container update
	U32 uClock;

	// The current value
	WorldVariable *pVariable;

	ContainerID  iSubscribeTypeContainerID; 
	// The container this shardVariable is stored in. Determines if we are MapRequested or Broadcast subscription
	
} ShardVariable;

// Each GameServer tracks this for use in subscribing to the containers
typedef struct ShardVariableContainerRef
{
	REF_TO(ShardVariableContainer) hMapRequestedContainer;
	REF_TO(ShardVariableContainer) hBroadcastContainer;

	// Since we need to deal with both MapRequested and Broadcast vars,
	//   it is insufficient to query g_ShardVariableDefs.eaVariables to know
	//   if we need to subscribe to either or both containers.
	//   As we AddVariables, set these booleans so each GameServer knows
	//   ahead of time which container(s) it wants.
	
	bool bWantsMapRequested;
	bool bWantsBroadcast;
} ShardVariableContainerRef;

extern ShardVariableContainerRef g_ShardVariableRef;

// Get the default value of a shard variable
const WorldVariable *shardvariable_GetDefaultValue(SA_PARAM_NN_STR const char *pcVarName);

// Error message if we can't find a ShardVar
void shardvariable_ErrorOnNotFound(const char *pcName, char **estrError);

// Get the earray of variable names.
const char ***shardvariable_GetShardVariableNames(void);

void shardvariable_Load(void);

#if (defined(GAMESERVER) || defined(APPSERVER))

// Gets the eaArray of ContainerIDs so we can call transactions with an earray
U32** shardVariable_GetContainerIDList();

// Gets a variable, if one exists
SA_RET_OP_VALID ShardVariable *shardvariable_GetByName(SA_PARAM_NN_STR const char *pcVarName);

// Clears the list of shard variables stored in the server
void shardvariable_ClearList(void);

// Returns a const list of shard variables stored in the server
const ShardVariable * const * const * const shardvariable_GetList(void);

void shardvariable_OncePerFrame(void);

// Adds all shard variables in the server shard variable list. Control whether we do MapRequested, Broadcast or both.
void shardVariable_AddAllVariables(bool bMapRequested, bool bBroadcast);


// These functions run transactions to alter shard variables
// They return true if the transaction starts, false if validation fails.
// If validation fails and "estrError" it not null, it contains the error string.
bool shardvariable_ResetVariable(const char *pcName, char **estrError);
bool shardvariable_ResetAllVariables(char **estrError);
bool shardvariable_SetVariable(WorldVariable *pWorldVar, char **estrError);
bool shardvariable_IncrementFloatVariable(const char *pcName, float fValue, char **estrError);
bool shardvariable_IncrementIntVariable(const char *pcName, int iValue, char **estrError);

#endif
