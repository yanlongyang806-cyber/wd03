#include "chatUtilities.h"
#include "channels.h"
#include "users.h"
#include "csr.h"
#include "msgsend.h"
#include "chatGlobal.h"
#include "ChatServer/chatShared.h"

#include "estring.h"
#include "cmdParse.h"

static bool csrUserIsGM(ChatUser *user)
{
	return userCharacterOnline(user) && UserIsAdmin(user);
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrSilence);
int csrSilenceCommand(CmdContext *context, ContainerID targetID, U32 duration)
{
	sendCommandToGlobalChatServer(context->commandString);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrSilenceByDisplayName);
int csrSilenceByDisplayName(CmdContext *context, const ACMD_SENTENCE displayName, U32 duration)
{
	sendCommandToGlobalChatServer(context->commandString);
	return CHATRETURN_FWD_NONE;
}

// Manually unsilence a user
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrUnsilenceByDisplayName);
int csrUnsilenceByDisplayName(CmdContext *context, ContainerID userID, const ACMD_SENTENCE displayName)
{
	sendCommandToGlobalChatServer(context->commandString);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrUnsilence);
int csrUnsilenceCommand(CmdContext *context, ContainerID targetID)
{
	sendCommandToGlobalChatServer(context->commandString);
	return CHATRETURN_FWD_NONE;
}

//////////////////////////////////
// Channel Membership Commands
//////////////////////////////////

void csrRemoveUserFromChannel(ChatChannel *channel, ChatUser *user)
{
	channelRemoveUser(channel, user);
}
AUTO_COMMAND ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrForceChannelKick);
void csrRemoveUserFromChannelInternal(char *channel_name, char *displayName)
{
	ChatUser *target = userFindByHandle(displayName);
	ChatChannel *channel = channelFindByName(channel_name);

	if (channel && target)
	{
		csrRemoveUserFromChannel(channel, target);
	}
}
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrForceChannelKick);
void csrRemoveUserFromChannelCommand(ContainerID userID, char *channel_name, char *displayName)
{
	ChatUser *user = userFindByContainerId(userID);

	if (csrUserIsGM(user))
	{
		csrRemoveUserFromChannelInternal(channel_name, displayName);
	}
}

void csrAddUserToChannel(ChatChannel *channel, ChatUser *user)
{
	channelAddUser(channel, user, 0);
	channelOnline(channel, user);
}
AUTO_COMMAND ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrForceChannelJoin);
void csrAddUserToChannelInternal(char *channel_name, char *displayName)
{
	ChatUser *target = userFindByHandle(displayName);
	ChatChannel *channel = channelFindByName(channel_name);

	if (channel && target)
	{
		csrAddUserToChannel(channel, target);
	}
}
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrForceChannelJoin);
void csrAddUserToChannelCommand(ContainerID userID, char *channel_name, char *displayName)
{
	ChatUser *user = userFindByContainerId(userID);

	if (csrUserIsGM(user))
	{
		csrAddUserToChannelInternal(channel_name, displayName);
	}
}

// Shard-wide message
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrAnnounce);
void csrAnnounce(ContainerID userID, char *msg)
{
	ChatUser *user = userFindByContainerId(userID);
	if (csrUserIsGM(user))
	{
		// TODO
		userBroadcastGlobal(msg);
	}
}

// Server-wide message
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrBroadcast);
void csrBroadcast(ContainerID userID, char *msg)
{
	ChatUser *user = userFindByContainerId(userID);
	if (csrUserIsGM(user))
	{
		// TODO
	}
}

//////////////////////////////////
// Chat Spying Commands
//////////////////////////////////

// Chat Spying : Channel Chat and Private Chat, both received and sent
void csrSpy(ChatUser *user, ChatUser *target)
{
	if (csrUserIsGM(user)) 
	{
		// Can't spy on other GMs/Admins with a equal or higher access level
		if (target && (!csrUserIsGM(target) || user->access_level > target->access_level))
		{
			userAddSpy(user, target);
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_Spying", 
				STRFMT_STRING("User", target->handle), STRFMT_END);
		}
		else if (target)
		{
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_NoSpyingOnAdmins", STRFMT_END);
		}
		else
		{
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_UnknownUser", STRFMT_END);
			//sendChatSystemStaticMsg(user, kChatLogEntryType_System, NULL, "Couldn't find user to spy on.");
		}
	}
}
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin);
void csrSpyByHandle(ContainerID userID, char *handle)
{
	ChatUser *user = userFindByContainerId(userID);
	ChatUser *target = userFindByHandle(handle);

	csrSpy(user, target);
}
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin);
void csrSpyByID(ContainerID userID, ContainerID targetID)
{
	ChatUser *user = userFindByContainerId(userID);
	ChatUser *target = userFindByContainerId(targetID);

	csrSpy(user, target);
}

// Stop Chat Spying
void csrStopSpying(ChatUser *user, ChatUser *target)
{
	if (csrUserIsGM(user))
	{
		if (target)
		{
			userRemoveSpy(user, target);
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_NotSpying", 
				STRFMT_STRING("User", target->handle), STRFMT_END);
		}
		else
		{
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_UnknownUser", STRFMT_END);
			//sendChatSystemMsg(user, kChatLogEntryType_System, NULL, "User does not exist");
		}
	}
}
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin);
void csrStopSpyingByHandle(ContainerID userID, char *handle)
{
	ChatUser *user = userFindByContainerId(userID);
	ChatUser *target = userFindByHandle(handle);

	csrStopSpying(user, target);
}
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin);
void csrStopSpyingByID(ContainerID userID, ContainerID targetID)
{
	ChatUser *user = userFindByContainerId(userID);
	ChatUser *target = userFindByContainerId(targetID);

	csrStopSpying(user, target);
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin);
void csrSpyList(ContainerID userID)
{
	ChatUser *user = userFindByContainerId(userID);
	char *spyList = NULL;
	if (csrUserIsGM(user))
	{
		int i;
		for (i=eaiSize(&user->spyingOn)-1; i>=0; i--)
		{
			ChatUser *spyTarget = userFindByContainerId(user->spyingOn[i]);

			if (spyTarget)
			{
				estrConcatf(&spyList, "%s,", spyTarget->handle);
			}
		}
	}
	sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_SpyingList", 
		STRFMT_STRING("UserList", spyList), STRFMT_END);
	estrDestroy(&spyList);
}


//////////////////////////////////
// User/Channel Access Commands
//////////////////////////////////

void csrSetUserAccessLevel(ChatChannel *channel, ChatUser *user, char *options)
{
	Watching *watching = userFindWatching(user, channel);
	// TODO does nothing?
}
AUTO_COMMAND ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrUserAccess);
void csrSetUserAccessInternal (char *channel_name, char *display_name, ACMD_SENTENCE options)
{
	ChatUser *target = userFindByHandle(display_name);
	ChatChannel *channel = channelFindByName(channel_name);

	if (channel && target)
	{
		csrSetUserAccessLevel(channel, target, options);
	}
}
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrUserAccess);
void csrSetUserAccessCommand(ContainerID userID, char *channel_name, char *display_name, ACMD_SENTENCE options)
{
	ChatUser *user = userFindByContainerId(userID);
	if (csrUserIsGM(user))
	{
		csrSetUserAccessInternal(channel_name, display_name, options);
	}
}

//////////////////////////////////
// Deafen Commands
//////////////////////////////////

void csrDeafenAll(ChatUser *user)
{
	if (user->access_level & CHANFLAGS_CSR && userCharacterOnline(user))
	{
		user->online_status |= USERSTATUS_DEAF;
		user->deafen_exceptionID = 0; // clear this
	}
}
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrdeaf);
void csrDeafenCommand(ContainerID userID)
{
	ChatUser *user = userFindByContainerId(userID);
	if (csrUserIsGM(user))
		csrDeafenAll(user);
}


void csrUndeafenAll(ChatUser *user)
{
	if (user->access_level & CHANFLAGS_CSR && userCharacterOnline(user))
		user->online_status &= ~(USERSTATUS_DEAF);
}
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrundeaf);
void csrUndeafenCommand(ContainerID userID)
{
	ChatUser *user = userFindByContainerId(userID);
	if (csrUserIsGM(user))
		csrUndeafenAll(user);
}


void csrSetDeafenException(ChatUser *user, ChatUser *target)
{
	if (user->access_level & CHANFLAGS_CSR && userCharacterOnline(user))
	{
		user->deafen_exceptionID = target->id;
	}
}
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrdeaf_except);
void csrSetDeafenExceptionCommand(ContainerID userID, ContainerID targetID)
{
	ChatUser *user = userFindByContainerId(userID);
	ChatUser *target = userFindByContainerId(targetID);
	if (target && csrUserIsGM(user))
		csrSetDeafenException(user, target);

}

void csrClearDeafenException(ChatUser *user)
{
	if (user->access_level & CHANFLAGS_CSR && userCharacterOnline(user))
	{
		user->deafen_exceptionID = 0;
	}
}
AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrdeaf_reset);
void csrClearDeafenCommand(ContainerID userID)
{
	ChatUser *user = userFindByContainerId(userID);
	if (csrUserIsGM(user))
		csrClearDeafenException(user);

}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(ChatAdmin) ACMD_NAME(csrsendmsg);
void csrSendMessage(ContainerID userID, ContainerID targetID, ACMD_SENTENCE msg)
{
	// TODO send an Admin/CSR message to target
}