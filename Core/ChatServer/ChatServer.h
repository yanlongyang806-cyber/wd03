/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "logging.h"

int ChatServerInit(void);

char * chatServerGetDatabaseDir(void);

char *chatServerGetLogDir(void);

char *chatServerGetLogFile(void);

typedef struct ChatUser ChatUser;
typedef struct ChatChannel ChatChannel;

void chatServerLogUserCommand(enumLogCategory logCategory, const char *commandString, const char *converseCommandString, ChatUser *user, ChatUser *target, const char *resultString);
void chatServerLogUserCommandWithReturnCode(enumLogCategory logCategory, const char *commandString, const char *converseCommandString, 
												ChatUser *user, ChatUser *target, int returnCode);

void chatServerLogChannelCommand(const char *commandString, const char *channel, ChatUser *user, const char *resultString);
void chatServerLogChannelCommandWithReturnCode(const char *commandString, const char *channel_name, ChatUser *user, int returnCode);

void chatServerLogChannelTargetCommand(const char *commandString, const char *converseCommandString, 
								 const char *channel_name, ChatUser *user, ChatUser *target, const char *resultString);
void chatServerLogChannelTargetCommandWithReturnCode(const char *commandString, const char *converseCommandString, 
												const char *channel_name, ChatUser *user, ChatUser *target, int returnCode);