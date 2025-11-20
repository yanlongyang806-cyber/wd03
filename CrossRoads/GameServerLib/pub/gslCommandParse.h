/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GSLCOMMANDPARSE_H
#define GSLCOMMANDPARSE_H

#include "stdtypes.h"
#include "net/net.h"

typedef struct ClientLink ClientLink;
typedef struct Cmd Cmd;
typedef struct CmdList CmdList;
typedef struct CmdContext CmdContext;
typedef struct Entity Entity;
typedef struct Entity Entity;
typedef enum CmdContextFlag CmdContextFlag;
typedef enum enumCmdContextHowCalled enumCmdContextHowCalled;
typedef enum LogoffType LogoffType;
typedef struct CmdParseStructList CmdParseStructList;

typedef struct CmdServerContext
{
	Entity *clientEntity;
	char *sourceStr;
} CmdServerContext;

// Parses and executes a command using the public command list
int GameServerParsePublic(const char *str, CmdContextFlag iFlags, Entity *client, char **ppRetString, int iOverrideAccessLevel, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);

// Parses using private command list
int GameServerParsePrivate(const char *str, CmdContextFlag iFlags, Entity *client, int iOverrideAccessLevel, bool *pbUnknownCommand, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);

// Command handler for the server, which globcmdparse calls
int GameServerDefaultParse(const char *str, char **ppRetString, CmdContextFlag iCmdContextFlags, int iOverrideAccessLevel, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);

// Parses using the gateway command list
int GameServerParseGateway(const char *cmd_str, CmdContextFlag iFlags, Entity *clientEntity, int iOverrideAccessLevel, bool *pbUnknownCommand, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);

// Loads the server state from a file
void gslLoadCmdConfig(char *config_file);

//should be done with each client after it connects
void SendCommandNamesToClientForAutoCompletion(Entity *pEntity);

//log out a player
void CommandLogOutPlayer(Entity *clientEntity);
//Called after a player is 
void PostLogOutPlayer(Entity *pEnt, U32 iAuthTicket, LogoffType eType);

// Runs a CSR command
void RunCSRCommand(Entity *pCaller, const char *entName, ACMD_SENTENCE pCommandString);

// Set the time step
void timeStepScaleSetHandler(F32 newtimeDebug, F32 newtimeGame);

#endif
