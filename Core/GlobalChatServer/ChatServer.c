/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ChatServer.h"
#include "logging.h"
#include "chatdb.h"
#include "chatCommon.h"
#include "file.h"
#include "channels.h"
#include "ChatServer\chatShared.h"

char * chatServerGetDatabaseDir(void)
{
	static char spDBDir[MAX_PATH] = "";
	if (!spDBDir[0])
	{
		fileSpecialDir("chatdb", SAFESTR(spDBDir));
	}
	return spDBDir;
}

char *chatServerGetLogDir(void)
{
	static char spLogDir[MAX_PATH] = "";

	if (!spLogDir[0])
	{
		fileSpecialDir("chatdb\\logs", SAFESTR(spLogDir));
	}
	return spLogDir;
}

char *chatServerGetLogFile(void)
{
	static char spLogFile[MAX_PATH] = "";

	if (!spLogFile[0])
	{
		strcpy(spLogFile, "chat_log.txt");
	}
	return spLogFile;
}

void chatServerLogUserCommandf(enumLogCategory logCategory, const char *commandString, const char *converseCommandString, ChatUser *user, ChatUser *target, const char *fmt, ...)
{
	va_list ap;
	char *logString = NULL;

	va_start(ap, fmt);
	estrConcatfv(&logString, fmt, ap);
	va_end(ap);

	chatServerLogUserCommand(logCategory, commandString, converseCommandString, user, target, logString);

	estrDestroy(&logString);
}

void chatServerLogUserCommand(enumLogCategory logCategory, const char *commandString, const char *converseCommandString, ChatUser *user, ChatUser *target, const char *resultString)
{
	U32 userID = user ? user->id : 0;
	const char *userHandle = user ? user->handle : NULL;
	U32 targetID = target ? target->id : 0;
	const char *targetHandle = target ? target->handle : NULL;

	objLog(logCategory, GLOBALTYPE_CHATUSER, userID, 0, userHandle, NULL, user ? user->accountName : NULL, commandString, NULL, 
		"[To:%d][To:%s] %s", targetID, targetHandle, resultString);
	if (target && converseCommandString)
	{
		objLog(logCategory, GLOBALTYPE_CHATUSER, targetID, 0, targetHandle, NULL, target->accountName, converseCommandString, NULL, 
			"[From:%d][From:%s] %s", userID, userHandle, resultString);
	}
}

void chatServerLogUserCommandWithReturnCode(enumLogCategory logCategory, const char *commandString, const char *converseCommandString, ChatUser *user, ChatUser *target, ChatServerReturnCodes returnCode)
{
	switch (returnCode)
	{
	case CHATRETURN_NONE:
	case CHATRETURN_FWD_NONE:
	case CHATRETURN_FWD_SENDER:
	case CHATRETURN_FWD_ALLLOCAL:
	case CHATRETURN_FWD_ALLLOCAL_MINUSENDER:
		chatServerLogUserCommand(logCategory, commandString, converseCommandString, user, target, "[Success]");
	xcase CHATRETURN_UNSPECIFIED:
		chatServerLogUserCommand(logCategory, commandString, converseCommandString, user, target, "[Error:Unspecified error]");
	xcase CHATRETURN_INVALIDNAME:
		chatServerLogUserCommand(logCategory, commandString, converseCommandString, user, target, "[Error:Invalid Name]");
	xcase CHATRETURN_USER_OFFLINE: // Target user is offline
		chatServerLogUserCommand(logCategory, commandString, converseCommandString, user, target, "[Error:Target offline]");
	xcase CHATRETURN_USER_DNE: // Target user does not exist
		chatServerLogUserCommand(logCategory, commandString, converseCommandString, user, target, "[Error:Target does not exist]"); 
	xcase CHATRETURN_USER_PERMISSIONS: // User does not have sufficient permissions
		chatServerLogUserCommand(logCategory, commandString, converseCommandString, user, target, "[Error:Insufficient permissions]");
	xcase CHATRETURN_CHANNEL_ALREADYEXISTS:
		chatServerLogUserCommand(logCategory, commandString, converseCommandString, user, target, "[Error:Already exists]");
	xcase CHATRETURN_CHANNEL_RESERVEDPREFIX:
		chatServerLogUserCommand(logCategory, commandString, converseCommandString, user, target, "[Error:Reserved prefix]");
	xcase CHATRETURN_CHANNEL_WATCHINGMAX:
		chatServerLogUserCommand(logCategory, commandString, converseCommandString, user, target, "[Error:Watching list maxed]");
	xcase CHATRETURN_CHANNEL_FULL:
		chatServerLogUserCommand(logCategory, commandString, converseCommandString, user, target, "[Error:Channel full]");
	xcase CHATRETURN_CHANNEL_ALREADYMEMBER:
		chatServerLogUserCommand(logCategory, commandString, converseCommandString, user, target, "[Error:Already member of channel]");
	xcase CHATRETURN_CHANNEL_NOTMEMBER:
		chatServerLogUserCommand(logCategory, commandString, converseCommandString, user, target, "[Error:Not a member of channel]");
	xcase CHATRETURN_CHANNEL_DNE:
		chatServerLogUserCommand(logCategory, commandString, converseCommandString, user, target, "[Error:Channel does not exist]");
	xcase CHATRETURN_USER_IGNORING:
		chatServerLogUserCommand(logCategory, commandString, converseCommandString, user, target, "[Error:Target is ignoring sender]");
	xdefault:
		chatServerLogUserCommand(logCategory, commandString, converseCommandString, user, target, "[Error:Unspecified error]");
	}
}

void chatServerLogChannelCommand(const char *commandString, const char *channel, ChatUser *user, const char *resultString)
{
	U32 userID = user ? user->id : 0;
	const char *handle = user ? user->handle : NULL;

	objLog(LOG_CHATCHANNEL, GLOBALTYPE_CHATUSER, userID, 0, handle, NULL, NULL, commandString, channel, "%s", resultString);
}

void chatServerLogChannelCommandWithReturnCode(const char *commandString, const char *channel, ChatUser *user, ChatServerReturnCodes returnCode)
{
	switch (returnCode)
	{
	case CHATRETURN_NONE:
	case CHATRETURN_FWD_NONE:
	case CHATRETURN_FWD_SENDER:
	case CHATRETURN_FWD_ALLLOCAL:
	case CHATRETURN_FWD_ALLLOCAL_MINUSENDER:
		chatServerLogChannelCommand(commandString, channel, user, "[Success]");
	xcase CHATRETURN_UNSPECIFIED:
		chatServerLogChannelCommand(commandString, channel, user, "[Error:Unspecified error]");
	xcase CHATRETURN_INVALIDNAME:
		chatServerLogChannelCommand(commandString, channel, user, "[Error:Invalid Name]");
	xcase CHATRETURN_USER_OFFLINE: // Target user is offline
		chatServerLogChannelCommand(commandString, channel, user, "[Error:Target offline]");
	xcase CHATRETURN_USER_DNE: // Target user does not exist
		chatServerLogChannelCommand(commandString, channel, user, "[Error:Target does not exist]"); 
	xcase CHATRETURN_USER_PERMISSIONS: // User does not have sufficient permissions
		chatServerLogChannelCommand(commandString, channel, user, "[Error:Insufficient permissions]");
	xcase CHATRETURN_CHANNEL_ALREADYEXISTS:
		chatServerLogChannelCommand(commandString, channel, user, "[Error:Already exists]");
	xcase CHATRETURN_CHANNEL_RESERVEDPREFIX:
		chatServerLogChannelCommand(commandString, channel, user, "[Error:Reserved prefix]");
	xcase CHATRETURN_CHANNEL_WATCHINGMAX:
		chatServerLogChannelCommand(commandString, channel, user, "[Error:Watching list maxed]");
	xcase CHATRETURN_CHANNEL_FULL:
		chatServerLogChannelCommand(commandString, channel, user, "[Error:Channel full]");
	xcase CHATRETURN_CHANNEL_ALREADYMEMBER:
		chatServerLogChannelCommand(commandString, channel, user, "[Error:Already member of channel]");
	xcase CHATRETURN_CHANNEL_NOTMEMBER:
		chatServerLogChannelCommand(commandString, channel, user, "[Error:Not a member of channel]");
	xcase CHATRETURN_CHANNEL_DNE:
		chatServerLogChannelCommand(commandString, channel, user, "[Error:Channel does not exist]");
	xcase CHATRETURN_USER_IGNORING:
		chatServerLogChannelCommand(commandString, channel, user, "[Error:Target is ignoring sender]");
	xdefault:
		chatServerLogChannelCommand(commandString, channel, user, "[Error:Unspecified error]");
	}
}

void chatServerLogChannelTargetCommand(const char *commandString, const char *converseCommandString, 
								 const char *channel_name, ChatUser *user, ChatUser *target, const char *resultString)
{
	U32 userID = user ? user->id : 0;
	const char *handle = user ? user->handle : NULL;
	U32 targetID = target ? target->id : 0;
	const char *targetHandle = target ? target->handle : NULL;
	ChatChannel * channel = channelFindByName(channel_name);

	objLog(LOG_CHATCHANNEL, GLOBALTYPE_CHATUSER, userID, 0, handle, NULL, user ? user->accountName : NULL, commandString, NULL, 
		"[Channel:%s][Tar:%d-%s] %s", channel_name, targetID, targetHandle, resultString);
	if (channel)
	{
		objLog(LOG_CHATCHANNEL, GLOBALTYPE_CHATCHANNEL, channel->uKey, channel->uKey, channel->name, NULL, channel->name, commandString, NULL, 
			"[User:%d-%s][Tar:%d-%s] %s", userID, handle, targetID, targetHandle, resultString);
	}
	
	if (target && converseCommandString)
	{
		objLog(LOG_CHATCHANNEL, GLOBALTYPE_CHATUSER, targetID, 0, handle, NULL, target->accountName, converseCommandString, NULL, 
			"[Channel:%s][User:%d-%s] %s", channel_name, userID, handle, resultString);
	}
}

void chatServerLogChannelTargetCommandWithReturnCode(const char *commandString, const char *converseCommandString, 
												const char *channel_name, ChatUser *user, ChatUser *target, int returnCode)
{
	switch (returnCode)
	{
	case CHATRETURN_NONE:
	case CHATRETURN_FWD_NONE:
	case CHATRETURN_FWD_SENDER:
	case CHATRETURN_FWD_ALLLOCAL:
	case CHATRETURN_FWD_ALLLOCAL_MINUSENDER:
		chatServerLogChannelTargetCommand(commandString, converseCommandString, channel_name, user, target, "[Success]");
	xcase CHATRETURN_UNSPECIFIED:
		chatServerLogChannelTargetCommand(commandString, converseCommandString, channel_name, user, target, "[Error:Unspecified error]");
	xcase CHATRETURN_INVALIDNAME:
		chatServerLogChannelTargetCommand(commandString, converseCommandString, channel_name, user, target, "[Error:Invalid Name]");
	xcase CHATRETURN_USER_OFFLINE: // Target user is offline
		chatServerLogChannelTargetCommand(commandString, converseCommandString, channel_name, user, target, "[Error:Target offline]");
	xcase CHATRETURN_USER_DNE: // Target user does not exist
		chatServerLogChannelTargetCommand(commandString, converseCommandString, channel_name, user, target, "[Error:Target does not exist]"); 
		// TODO get the name they entered somehow
	xcase CHATRETURN_USER_PERMISSIONS: // User does not have sufficient permissions
		chatServerLogChannelTargetCommand(commandString, converseCommandString, channel_name, user, target, "[Error:Insufficient permissions]");
	xcase CHATRETURN_CHANNEL_ALREADYEXISTS:
		chatServerLogChannelTargetCommand(commandString, converseCommandString, channel_name, user, target, "[Error:Already exists]");
	xcase CHATRETURN_CHANNEL_RESERVEDPREFIX:
		chatServerLogChannelTargetCommand(commandString, converseCommandString, channel_name, user, target, "[Error:Reserved prefix]");
	xcase CHATRETURN_CHANNEL_WATCHINGMAX:
		chatServerLogChannelTargetCommand(commandString, converseCommandString, channel_name, user, target, "[Error:Watching list maxed]");
	xcase CHATRETURN_CHANNEL_FULL:
		chatServerLogChannelTargetCommand(commandString, converseCommandString, channel_name, user, target, "[Error:channel_name full]");
	xcase CHATRETURN_CHANNEL_ALREADYMEMBER:
		chatServerLogChannelTargetCommand(commandString, converseCommandString, channel_name, user, target, "[Error:Already member of channel]");
	xcase CHATRETURN_CHANNEL_NOTMEMBER:
		chatServerLogChannelTargetCommand(commandString, converseCommandString, channel_name, user, target, "[Error:Not a member of channel]");
	xcase CHATRETURN_CHANNEL_DNE:
		chatServerLogChannelTargetCommand(commandString, converseCommandString, channel_name, user, target, "[Error:channel_name does not exist]");
	xcase CHATRETURN_USER_IGNORING:
		chatServerLogChannelTargetCommand(commandString, converseCommandString, channel_name, user, target, "[Error:Target is ignoring sender]");
	xdefault:
		chatServerLogChannelTargetCommand(commandString, converseCommandString, channel_name, user, target, "[Error:Unspecified error]");
	}
}