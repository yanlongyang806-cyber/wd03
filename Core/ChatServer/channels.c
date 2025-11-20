#include "channels.h"

#include "AutoTransDefs.h"
#include "chatCommandStrings.h"
#include "chatCommon.h"
#include "chatGlobal.h"
#include "chatRelay/chatRelayManager.h"
#include "ChatServer.h"
#include "ChatServer/chatShared.h"
#include "chatShardConfig.h"
#include "chatUtilities.h"
#include "chatVoice.h"
#include "friendsIgnore.h"
#include "msgsend.h"
#include "objContainerIO.h"
#include "objTransactions.h"
#include "TextFilter.h"
#include "textparser.h"
#include "users.h"

#include "AutoGen/chatdb_h_ast.h"
#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/ChatServer_autotransactions_autogen_wrappers.h"
#include "AutoGen/chatRelayManager_h_ast.h"
#include "Autogen/GameServerLib_autogen_RemoteFuncs.h"

// Function Prototypes
static int channelSetUserOwner(ChatUser *originalOwner, ChatUser *target, ChatChannel *channel);
// -----------------------

extern ShardChatServerConfig gShardChatServerConfig;

void channelInitialize(NOCONST(ChatChannel) *channel, const char *channel_name, const char *description, U32 uReservedFlags)
{
	channel->name = StructAllocString(channel_name);
	channel->description = StructAllocString(description);
	channel->access = CHANFLAGS_COMMON;
	channel->reserved = uReservedFlags;
	channel->voiceEnabled = (gShardChatServerConfig.bDisableVoiceInGuildChannels ? !!(uReservedFlags & (CHANNEL_SPECIAL_VOICE & ~CHANNEL_SPECIAL_GUILD)) : !!(uReservedFlags & CHANNEL_SPECIAL_VOICE));

	channel->permissionLevels[CHANUSER_USER] = CHANPERM_DEFAULTUSER;
	channel->permissionLevels[CHANUSER_OPERATOR] = CHANPERM_DEFAULTOP;
	channel->permissionLevels[CHANUSER_ADMIN] = CHANPERM_DEFAULTADMIN;
	channel->permissionLevels[CHANUSER_OWNER] = CHANPERM_OWNER;

	if(cvIsVoiceEnabled() && channel->voiceEnabled)
		chatVoiceCreateChannel((ChatChannel*)channel);
}

int channelIdx(ChatUser *user,ChatChannel *channel)
{
	int		i;
	if (!user) return -1;
	for(i=eaSize(&user->watching)-1;i>=0;i--)
	{
		if (user->watching[i]->channelID == channel->uKey)
			break;
	}
	return i;
}

static int channelReserveIdx(ChatUser *user,ChatChannel *channel)
{
	int		i;

	if (!user) return -1;
	for(i=eaSize(&user->reserved)-1;i>=0;i--)
	{
		if (stricmp(user->reserved[i]->name, channel->name) == 0)
			break;
	}
	return i;
}

void accessTranslateOptions(char **estr, char *options)
{
}

char *accessGetString(ChannelAccess access)
{
	static char buf[100]="";
	buf[0] = '\0';
	if (access & CHANFLAGS_OPERATOR)
		strcat(buf,"o");
	if (access & CHANFLAGS_SEND)
		strcat(buf,"s");
	if (access & CHANFLAGS_JOIN)
		strcat(buf,"j");
	return buf;
}

void channelSendAccess(ChatUser *user,ChatChannel *channel)
{
	char	buf[100]="";
	if (channel->access & CHANFLAGS_OPERATOR)
		strcat(buf,"o");
	if (!(channel->access & CHANFLAGS_SEND))
		strcat(buf,"s");
	if (channel->access & CHANFLAGS_JOIN)
		strcat(buf,"j");

	sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel->name, "ChatServer_ChannelPermission", 
		STRFMT_STRING("Channel", channel->name), STRFMT_STRING("PermissionString", accessGetString(channel->access)), STRFMT_END);
	//sendChatSystemMsg(user, kChatLogEntryType_System, channel->name, "access %s", buf);
}

void channelList(ChatUser *user, char ** estr)
{
	char *pListString = NULL;
	int i;
	
	for (i=eaSize(&user->reserved)-1; i>=0; i--)
	{
		ChatChannel *channel = channelFindByName(user->reserved[i]->name);
		if (channel)
		{
			estrConcatf(&pListString, "%s%s", pListString ? ", " : "", convertExactChannelName(channel->name, channel->reserved));
		}
	}
	for (i=eaSize(&user->watching)-1;i>=0; i--)
	{
		ChatChannel *channel = channelFindByID(user->watching[i]->channelID);
		if (channel)
		{
			estrConcatf(&pListString, "%s%s", pListString ? ", " : "", channel->name);
		}
	}

	if (estr)
	{
		estrCopy2(estr, pListString ? pListString : "");
	}
	else
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_WatchingList", 
			STRFMT_STRING("ChannelList", pListString ? pListString : "[No Channels]"), STRFMT_END);
	}
	estrDestroy(&pListString);
}

void channelListMembers(ChatUser *user,const char *channel_name)
{
	int		i;
	ChatChannel *channel = channelFindByName(channel_name);
	char *pListString = NULL;

	if (!channel)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelDNE", 
			STRFMT_STRING("Channel", channel_name), STRFMT_END);
		return;
	}
	if (eaiFind(&channel->members, user->id) < 0)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_NotInChannel", 
			STRFMT_STRING("Channel",  channel->name), STRFMT_END);
		return;
	}

	for(i=eaiSize(&channel->online)-1;i>=0;i--)
	{
		ChatUser *chanuser = userFindByContainerId(channel->online[i]);
		if (!(chanuser->access & CHANFLAGS_INVISIBLE))
		{
			estrConcatf(&pListString, "%s%s", pListString ? ", " : "", chanuser->handle);
		}
	}

	sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_MemberList", 
		STRFMT_STRING("Channel", convertExactChannelName(channel->name, channel->reserved)), 
		STRFMT_STRING("MemberList", pListString), STRFMT_END);
	channelGetMotd(user, channel);
	
	if (channel->description && channel->description[0])
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelDescription", 
			STRFMT_STRING("Channel", convertExactChannelName(channel->name, channel->reserved)), 
			STRFMT_STRING("Description", channel->description), STRFMT_END);
	}
	estrDestroy(&pListString);
}

int channelSize(ChatChannel * channel)
{
	return ( eaiSize(&channel->members) + eaiSize(&channel->invites));
}

static void channelJoinReserved(ChatUser *user, ChatChannel *channel)
{
	NOCONST(ChatChannel) *channelMod = CONTAINER_NOCONST(ChatChannel, channel);
	NOCONST(ChatUser) *userMod = CONTAINER_NOCONST(ChatUser, user);
	NOCONST(Watching) *watching;
	if (ISNULL(channel) || ISNULL(user) || !IsChannelFlagShardOnly(channel->reserved))
		return;

	watching = StructAllocNoConst(parse_Watching);
	watching->name = StructAllocString(channel->name);
	watching->channelID = channel->uKey;
	eaPush(&userMod->reserved, watching);
	watching->ePermissionLevel = CHANUSER_USER;

	if(cvIsVoiceEnabled() && channel->voiceEnabled)
		chatVoiceJoin(channel, user);

	eaiPush(&channelMod->members,user->id);
}

static int channelJoinSub(ChatUser *user,const char *channel_name,int create,int flags);
typedef struct ChannelCreateStruct
{
	ChatUser *user;
	U32 uChatServerID;
	int create;
	int flags;
} ChannelCreateStruct;

// Add the user to the channel's member list, channel to the user's watching list, and remove invites
int channelAddUser(ChatChannel *channel, ChatUser *user, int flags)
{
	if (!channel || !user)
		return 0;
	if (IsChannelFlagShardOnly(channel->reserved))
	{
		channelJoinReserved(user, channel);
	}
	return 1;
}

int channelCheckName(const char *channel_name, int flags)
{
	if (strStartsWith(channel_name, TEAM_CHANNEL_PREFIX) && !(flags & CHANNEL_SPECIAL_TEAM))
	{
		// Error: team channel name from non-team join
		return CHATRETURN_CHANNEL_RESERVEDPREFIX;
	}
	if (strStartsWith(channel_name, GUILD_CHANNEL_PREFIX) && !(flags & CHANNEL_SPECIAL_GUILD))
	{
		// Error: team channel name from non-team join
		return CHATRETURN_CHANNEL_RESERVEDPREFIX;
	}
	if (strStartsWith(channel_name, OFFICER_CHANNEL_PREFIX) && !(flags & CHANNEL_SPECIAL_OFFICER))
	{
		// Error: team channel name from non-team join
		return CHATRETURN_CHANNEL_RESERVEDPREFIX;
	}
	if (strStartsWith(channel_name, ZONE_CHANNEL_NAME) && !(flags & (CHANNEL_SPECIAL_ZONE | CHANNEL_SPECIAL_SHARDGLOBAL)))
	{
		return CHATRETURN_CHANNEL_RESERVEDPREFIX;
	}
	if (strStartsWith(channel_name, QUEUE_CHANNEL_PREFIX) && !(flags & CHANNEL_SPECIAL_PVP))
	{
		return CHATRETURN_CHANNEL_RESERVEDPREFIX;
	}
	if(strStartsWith(channel_name, TEAMUP_CHANNEL_PREFIX) && !(flags & CHANNEL_SPECIAL_TEAMUP))
	{
		return CHATRETURN_CHANNEL_RESERVEDPREFIX;
	}
	// TODO add invalid characters check for channel names
	return CHATRETURN_NONE;
}

// This is only used to Join/Create reserved channels on Shard
static int channelJoinSub(ChatUser *user, const char *chan_name, int create, int flags)
{
	static char *channel_name = NULL;
	NOCONST(ChatChannel) *channel = NULL;
	ChatChannel *channelConst = NULL;
	Watching	*watching = NULL;
	Watching	***list;
	const char *reason = NULL;
	int iNameCheck;

	if (!flags)
	{
		devassertmsg(0, "Shard Chat Server tried to create a non-reserved channel");
		return CHATRETURN_FWD_NONE; // early out - should also never happen
	}

	if (chan_name) {
		estrCopy2(&channel_name, chan_name);
		estrTrimLeadingAndTrailingWhitespace(&channel_name);
	} else {
		estrClear(&channel_name);
	}

	channel = CONTAINER_NOCONST(ChatChannel, channelFindByName(channel_name));
	if ((iNameCheck = channelCheckName(channel_name, flags)) != CHATRETURN_NONE)
		return iNameCheck;

	if (!channel)
	{
		if (!create)
		{
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelDNE", 
				STRFMT_STRING("Channel", channel_name), STRFMT_END);
			return CHATRETURN_UNSPECIFIED;
		}
		// TODO are these necessary for reserved channels?

		channel = StructCreateNoConst(parse_ChatChannel);
		channelInitialize(channel, channel_name, "", flags);
		stashAddPointer(chat_db.channel_names, channel->name, channel, false);
	}
	channelConst = (ChatChannel*) channel;

	list = &((Watching**) user->reserved);
	watching = userFindWatching(user, channelConst);

   	if (!watching)
	{
		int inviteIdx = -1;
		if (   ! (channel->access & CHANFLAGS_JOIN) 
			&& !UserIsAdmin(user) 
			&& eaiFind(&user->invites, channelConst->uKey) < 0)
		{
			// TODO new custom error message for Private Channel
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_PermissionDenied", STRFMT_END);
			return CHATRETURN_UNSPECIFIED;
		}
	}
	else
	{
		return CHATRETURN_CHANNEL_ALREADYMEMBER;
	}
	channelJoinReserved(user, channelConst);
	channelOnline(channelConst, user);
	if (userOnline(user))
	{
		ChatChannelInfo *info = ChatServerCreateChannelUpdate(user, channelConst, CHANNELUPDATE_UPDATE_NO_MEMBERS);
		if (info)
		{
			ChannelSendUpdateToUser(user, info, CHANNELUPDATE_UPDATE_NO_MEMBERS);
			StructDestroy(parse_ChatChannelInfo, info);
		}
	}
	sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelJoin", 
		STRFMT_STRING("Channel", convertExactChannelName(channelConst->name, channelConst->reserved)), STRFMT_END);
	return CHATRETURN_FWD_NONE;
}

int channelJoin(ChatUser *user,const char *channel_name, int flags)
{
	int result = channelJoinSub(user,channel_name,0,flags);
	chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_JOIN, channel_name, user, result);
	return result;
}

int channelCreate(ChatUser *user,const char *chan_name, int flags, bool adminAccess)
{
	static char *channel_name = NULL;
	ChatChannel *chan;
	int result;

	estrCopy2(&channel_name, chan_name);
	estrTrimLeadingAndTrailingWhitespace(&channel_name);

	if (chan = channelFindByName(channel_name))
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelAlreadyExists", 
			STRFMT_STRING("Channel", channel_name), STRFMT_END);
		return CHATRETURN_CHANNEL_ALREADYEXISTS;
	}
	if (result = channelIsNameValid(channel_name, adminAccess, flags))
	{
		char *errorString = NULL;
		estrPrintf(&errorString, "Invalid name: %s", StaticDefineIntRevLookup(ChatServerReturnCodesEnum, result));
		switch (result)
		{
		case CHATRETURN_CHANNEL_RESERVEDPREFIX:
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelReservedName", 
				STRFMT_STRING("Channel", channel_name), STRFMT_END);
		xcase CHATRETURN_NAME_LENGTH_LONG:
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelInvalidNameLenLong", 
				STRFMT_STRING("Channel", channel_name), STRFMT_END);
		xcase CHATRETURN_NAME_LENGTH_SHORT:
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelInvalidNameLenShort",
				STRFMT_STRING("Channel", channel_name), STRFMT_END);
		xcase CHATRETURN_PROFANITY_NOT_ALLOWED:
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelInvalidNameProfanity", 
				 STRFMT_END);
		}
		chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_JOIN, channel_name, user, errorString);
		estrDestroy(&errorString);
		return CHATRETURN_INVALIDNAME;
	}
	result = channelJoinSub(user,channel_name,1,flags);
	chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_JOIN, channel_name, user, result);
	return result;
}

bool channelJoinOrCreate_ShardOnly(ChatUser *user, ContainerID uCharacterID, const char *channel_name, int flags)
{
	int result;
	ChatChannel *channel;
	if (!user || !IsChannelFlagShardOnly(flags))
		return false;
	PERFINFO_AUTO_START_FUNC();
	channel = channelFindByName(channel_name);
	if (!channel)
		result = channelCreate(user, channel_name, flags, true);
	else
		result = channelJoin(user, channel_name, flags);
	if (CHATRETURN_SUCCESS(result) || result == CHATRETURN_CHANNEL_ALREADYMEMBER)
	{
		RemoteCommand_ServerChat_ShardChannelJoin(GLOBALTYPE_ENTITYPLAYER, uCharacterID, uCharacterID, flags);
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}
	PERFINFO_AUTO_STOP_FUNC();
	return false;
}

// assign a new operator if no online channel operators exist
void chooseNewChannelOwner(ChatUser *currentOwner, ChatChannel * channel)
{
	int size = eaiSize(&channel->members);
	ChatUser *maxUser = NULL;
	ChannelUserLevel eMaxLevel = CHANUSER_USER;

	if (channel->reserved)
		return;

	if(size)
	{
		ChatUser * target = NULL;
		int i;
		for(i=0;i<size;i++)
		{
			Watching *watch;
			target = userFindByContainerId(channel->members[i]);

			if (!target)
				continue;
			if(watch = userFindWatching(target, channel))
			{
				if (maxUser == NULL || watch->ePermissionLevel > eMaxLevel)
				{
					maxUser = target;
					eMaxLevel = watch->ePermissionLevel;
				}
				if (watch->ePermissionLevel == CHANUSER_ADMIN)
					break;
			}
		}
		channelSetUserOwner(currentOwner, target, channel);
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_OWNERAUTO, CHATCOMMAND_CHANNEL_TARGETOWNERAUTO, 
			channel->name, currentOwner, target, CHATRETURN_NONE);
	}
}

void channelDestroy(ChatChannel * channel)
{
	if (IsChannelFlagShardOnly(channel->reserved)) // Shard Chat only handles reserved channels
	{
		if(channel->voiceEnabled)
			chatVoiceChannelDelete(channel);

		stashRemovePointer(chat_db.channel_names, channel->name, NULL);
		StructDestroy(parse_ChatChannel, channel);
	}
}

bool channelDeleteIfEmpty(ChatDb *db,ChatChannel *channel)
{
	if (!channel)
		return false;
	if (channel->reserved & CHANNEL_SPECIAL_SHARDGLOBAL)
		return false; // Never destroy the global channel
	if (channelSize(channel) <= 0)
	{
		chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_DESTROY, channel->name, NULL, "Channel empty");
		channelDestroy(channel);
		return true;
	}
	return false;
}

int channelKick(ChatUser *user, ChatUser *target, const char *channel_name)
{
	// TODO
	return 0;
}

// Shard only handles reserved channel leaves / kicks
int channelLeave(ChatUser *user, const char *channel_name, int isKicking)
{
	ChatChannel *channel = channelFindByName(channel_name);
	char *pCaseExactName = NULL;
	Watching *watch = NULL;
	bool bDeleted = false;
	U32 uDeletedID = 0;
	U32 uReservedFlags = 0;

	if (!channel_name || !*channel_name)
		return CHATRETURN_CHANNEL_DNE;
	if (!channel) // No error message for Shard stuff
		return CHATRETURN_CHANNEL_NOTMEMBER;

	uReservedFlags = channel->reserved;
	pCaseExactName = StructAllocString(channel->name);
	watch = userFindWatching(user, channel);

	// Must be done before leaving channel
	if (watch && watch->ePermissionLevel == CHANUSER_OWNER)
		chooseNewChannelOwner(user, channel);

	if (!channelRemoveUser(channel, user)) // No error message for Shard stuff
	{
		if (pCaseExactName) free(pCaseExactName);
		return CHATRETURN_CHANNEL_NOTMEMBER;
	}

	// Log before any other things happen, including channelDestroy
	chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_LEAVE, channel_name, user, CHATRETURN_NONE);

	if (userOnline(user))
	{
		ChatChannelInfo *info = ChatServerCreateChannelUpdate(user, channel, CHANNELUPDATE_REMOVE);
		if (info)
		{
			ChannelSendUpdateToUser(user, info, CHANNELUPDATE_REMOVE);
			StructDestroy(parse_ChatChannelInfo, info);
		}
		if (isKicking)
		{
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, pCaseExactName, "ChatServer_ChannelKicked", 
				STRFMT_STRING("Channel", convertExactChannelName(channel_name, uReservedFlags)), STRFMT_END);
		} else {
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, pCaseExactName, "ChatServer_ChannelLeave", 
				STRFMT_STRING("Channel", convertExactChannelName(channel_name, uReservedFlags)), STRFMT_END);
		}
	}
	if (!(channel->reserved & CHANNEL_SPECIAL_SHARDGLOBAL))
	{
		uDeletedID = channel->uKey;
		bDeleted = channelDeleteIfEmpty(&chat_db,channel);
	}
	if (pCaseExactName) free(pCaseExactName);
	return CHATRETURN_FWD_NONE;
}

static void channelSendToRelays(SA_PARAM_NN_VALID ChatChannel *pChannel, SA_PARAM_NN_VALID const ChatMessage *pMsg, ChatUser *pFromUser)
{
	U32 iChanSize=eaiSize(&pChannel->online);

	EARRAY_OF(ChatRelayUserList) eaRelays = NULL;
	eaStackCreate(&eaRelays, ChatConfig_GetNumberOfChatRelays());
	eaIndexedEnable(&eaRelays, parse_ChatRelayUserList);

	EARRAY_INT_CONST_FOREACH_BEGIN(pChannel->online, i, n);
	{
		ChatUser *user = userFindByContainerId(pChannel->online[i]);
		if (user && user->uChatRelayID && 
			!userIsIgnoringByID(user, pMsg->pFrom->accountID, pMsg->pFrom->bIsGM) &&
			(!pFromUser || isWhitelisted(pFromUser, user, pChannel->name, 0)))
		{
			ANALYSIS_ASSUME(user);
			ChatServerAddToRelayUserList(&eaRelays, user);
		}
	}
	EARRAY_FOREACH_END;

	ChatServerMessageSendToRelays(eaRelays, pMsg);
	eaDestroyStruct(&eaRelays, parse_ChatRelayUserList);
}

void channelForward(SA_PARAM_OP_VALID const ChatMessage *pMsg)
{
	if (pMsg)
	{
		ChatChannel	*channel = channelFindByName(pMsg->pchChannel);
		if (channel)
			channelSendToRelays(channel, pMsg, NULL);
	}
}

void channelSend(SA_PARAM_OP_VALID ChatUser *pFromUser, SA_PARAM_OP_VALID ChatMessage *pMsg)
{
	Watching	*watching;
	ChatChannel	*channel;
	bool bSendErrors = false;

	if (!pFromUser | !pMsg)
		return;
	if (!ChatServerIsValidMessage(pFromUser, pMsg->pchText))
		return;

	channel = channelFindByName(pMsg->pchChannel);
	if (!channel)
	{
		sendTranslatedMessageToUser(pFromUser, kChatLogEntryType_System, NULL, "ChatServer_ChannelDNE", STRFMT_STRING("Channel", pMsg->pchChannel), STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, CHATRETURN_CHANNEL_DNE);
		return;
	}
	bSendErrors = channel->reserved;

	if (userIsSilenced(pFromUser))
	{
		U32 hour, min, sec;
		userGetSilenceTimeLeft(pFromUser, &hour, &min, &sec);
		sendTranslatedMessageToUser(pFromUser, kChatLogEntryType_System, NULL, "ChatServer_Silenced", 
			STRFMT_INT("H", hour), STRFMT_INT("M", min), STRFMT_INT("S", sec), STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, CHATRETURN_USER_PERMISSIONS);
		return;
	}
	if (!userCanTalk(pFromUser))
	{
		if (bSendErrors) sendTranslatedMessageToUser(pFromUser, kChatLogEntryType_System, NULL, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, CHATRETURN_USER_PERMISSIONS);
		return;
	}

	watching = IsChannelFlagShardOnly(channel->reserved) 
		? channelFindWatchingReserved(pFromUser, pMsg->pchChannel) 
		: channelFindWatching(pFromUser,pMsg->pchChannel);
	if (!watching)
	{
		if (bSendErrors) sendTranslatedMessageToUser(pFromUser, kChatLogEntryType_System, NULL, "ChatServer_NotInChannel", 
			STRFMT_STRING("Channel", pMsg->pchChannel), STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, CHATRETURN_USER_PERMISSIONS);
		return;
	}

	// Non-User channel members ignore the mute - TODO - GM Override?
	if (watching->ePermissionLevel == CHANUSER_USER && !(channel->access & CHANFLAGS_SEND) ) 
	{
		if (bSendErrors) sendTranslatedMessageToUser(pFromUser, kChatLogEntryType_System, NULL, "ChatServer_ChannelMuted", STRFMT_STRING("Channel", channel->name), STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, CHATRETURN_USER_PERMISSIONS);
		return;
	}

	if (!channelUserHasPermission(channel, pFromUser, watching, CHANPERM_SEND))
	{
		if (bSendErrors) sendTranslatedMessageToUser(pFromUser, kChatLogEntryType_System, NULL, "ChatServer_UserMuted", STRFMT_STRING("Channel", channel->name), STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, CHATRETURN_USER_PERMISSIONS);
		return;
	}

	channelSendToRelays(channel, pMsg, pFromUser);

	if (IsChannelFlagShardOnly(channel->reserved)) {
		chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, pMsg->pchText);
	}
}

//////////////////////////////////////
// Channel Permission / Access Functions

// Bit flags for access changes
#define CHANACCESS_JOIN_ADD		0x01
#define CHANACCESS_JOIN_REMOVE  0x02
#define CHANACCESS_SEND_ADD		0x04
#define CHANACCESS_SEND_REMOVE	0x08
#define CHANACCESS_OPDEFAULT_ADD 0x10
#define CHANACCESS_OPDEFAULT_REMOVE 0x20

void channelAccessGetString (char **estr, int iFlags, ChatUser *user)
{
	Language eLanguage = userGetLastLanguage(user);
	bool bWritten = false;
	if (iFlags & CHANACCESS_JOIN_REMOVE)
	{
		langFormatMessageKey(eLanguage, estr, "ChatServer_ChannelPrivateChange", STRFMT_END);
		bWritten = true;
	}
	if (iFlags & CHANACCESS_SEND_REMOVE)
	{
		if (bWritten) estrConcatf(estr, ", ");
		langFormatMessageKey(eLanguage, estr, "ChatServer_ChannelMuteChange", STRFMT_END);
		bWritten = true;
	}
	if (iFlags & CHANACCESS_OPDEFAULT_REMOVE)
	{
		if (bWritten) estrConcatf(estr, ", ");
		langFormatMessageKey(eLanguage, estr, "ChatServer_ChannelOpOffChange", STRFMT_END);
		bWritten = true;
	}

	if (iFlags & CHANACCESS_JOIN_ADD)
	{
		if (bWritten) estrConcatf(estr, ", ");
		langFormatMessageKey(eLanguage, estr, "ChatServer_ChannelPublicChange", STRFMT_END);
		bWritten = true;
	}
	if (iFlags & CHANACCESS_SEND_ADD)
	{
		if (bWritten) estrConcatf(estr, ", ");
		langFormatMessageKey(eLanguage, estr, "ChatServer_ChannelUnmuteChange", STRFMT_END);
		bWritten = true;
	}
	if (iFlags & CHANACCESS_OPDEFAULT_ADD)
	{
		if (bWritten) estrConcatf(estr, ", ");
		langFormatMessageKey(eLanguage, estr, "ChatServer_ChannelOpOnChange", STRFMT_END);
		bWritten = true;
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void ChatServer_SendChannelAccessChange(U32 uID, const char *pChannelName, U32 uFlags)
{
	ChatUser *user = userFindByContainerId(uID);
	if (user)
	{
		char *accessDescription = NULL;
		estrStackCreate(&accessDescription);
		channelAccessGetString(&accessDescription, uFlags, user);
		sendTranslatedMessageToUser(user,  kChatLogEntryType_System, pChannelName, "ChatServer_SetChannelPermission", 
			STRFMT_STRING("Channel", pChannelName), STRFMT_STRING("PermissionString", accessDescription ? accessDescription : ""), STRFMT_END);
		estrDestroy(&accessDescription);
	}
}

AUTO_TRANSACTION ATR_LOCKS(owner, ".watching[]") ATR_LOCKS(target, ".watching[]");
enumTransactionOutcome trPassChannelOwnership(ATR_ARGS, NOCONST(ChatUser) *owner, NOCONST(ChatUser) *target, const char *channel_name)
{
	NOCONST(Watching) *oldWatch = NULL, *newWatch = NULL;
	oldWatch = eaIndexedGetUsingString(&owner->watching, channel_name);
	newWatch = eaIndexedGetUsingString(&target->watching, channel_name);
	if (oldWatch && newWatch && oldWatch->ePermissionLevel == CHANUSER_OWNER)
	{
		oldWatch->ePermissionLevel = CHANUSER_USER;
		newWatch->ePermissionLevel = CHANUSER_OWNER;
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

// Owner reverts to normal user status
static int channelSetUserOwner(ChatUser *originalOwner, ChatUser *target, ChatChannel *channel)
{
	Watching *watch = NULL;
	if (!originalOwner || !channel || !target)
		return CHATRETURN_UNSPECIFIED;
	watch = userFindWatching(originalOwner, channel);
	if (!watch || watch->ePermissionLevel != CHANUSER_OWNER)
		return CHATRETURN_USER_PERMISSIONS;
	watch = userFindWatching(target, channel);
	if (!watch)
		return CHATRETURN_UNSPECIFIED;

	if (!IsChannelFlagShardOnly(channel->reserved))
	{
		trPassChannelOwnership(NULL, NULL, CONTAINER_NOCONST(ChatUser, originalOwner), CONTAINER_NOCONST(ChatUser, target), channel->name);
	}
	// TODO reserved channels? send user updates

	return CHATRETURN_FWD_NONE;
}

int channelDonateOwnership (ChatUser *user, ChatUser *target, const char *channel_name)
{
	ChatChannel * channel = channelFindByName(channel_name);
	Watching *watch;
	
	if (!user || !channel || !target)
		return CHATRETURN_UNSPECIFIED;
	watch = userFindWatching(user, channel);
	if (!watch)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_NotInChannel", 
			STRFMT_STRING("Channel",  channel->name), STRFMT_END);
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_OWNER, CHATCOMMAND_CHANNEL_TARGETOWNER, 
			channel_name, user, NULL, CHATRETURN_CHANNEL_NOTMEMBER);
		return CHATRETURN_CHANNEL_NOTMEMBER;
	}
	if (watch->ePermissionLevel != CHANUSER_OWNER)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_OWNER, CHATCOMMAND_CHANNEL_TARGETOWNER,
			channel_name, user, NULL, CHATRETURN_USER_PERMISSIONS);
		return CHATRETURN_USER_PERMISSIONS;
	}
	watch = userFindWatching(target, channel);
	if (!watch)
	{		
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_TargetNotInChannel", 
			STRFMT_STRING("Target", target->handle), STRFMT_STRING("Channel",  channel->name), STRFMT_END);
		chatServerLogChannelTargetCommand(CHATCOMMAND_CHANNEL_OWNER, CHATCOMMAND_CHANNEL_TARGETOWNER,
			channel_name, user, target, "[Error: Target not in channel]");
		return CHATRETURN_UNSPECIFIED;
	}	
	channelSetUserOwner(user, target, channel);
	chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_OWNER, CHATCOMMAND_CHANNEL_TARGETOWNER,
		channel_name, user, target, CHATRETURN_NONE);
	return CHATRETURN_FWD_NONE;
}

void channelGetMotd (ChatUser *user,ChatChannel *channel)
{
	int			i,num_emails;
	Email		*email;
	ChatUser		*from;
	char		datestr[100];

	if (!user || !channel)
		return;
	num_emails = eaSize(&channel->messageOfTheDay);
	for(i=0;i<num_emails;i++)
	{
		email = channel->messageOfTheDay[i];
		if (email->sent < user->last_online && i+1 != num_emails)
			continue;
		from = userFindByContainerId(email->from);
		if(from)
		{
			timeMakeLocalDateStringFromSecondsSince2000(datestr, email->sent);
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel->name, "ChatServer_ChannelMotD", 
				STRFMT_STRING("Message", email->body), STRFMT_STRING("User", from->handle), STRFMT_STRING("Date", datestr), STRFMT_END);
		}
	}
	CONTAINER_NOCONST(ChatUser, user)->last_online = timeSecondsSince2000();
}

void channelOnline(ChatChannel *channel, ChatUser *user)
{
	if (eaiFind(&channel->online,user->id) >= 0)
	{
		return;
	}
	eaiPushUnique(&CONTAINER_NOCONST(ChatChannel, channel)->online,user->id);
	channelGetMotd(user,channel);
	chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_ONLINE, channel->name, user, CHATRETURN_NONE);
}

void channelForceKill(ChatChannel *channel)
{
	int i;
	for (i=eaiSize(&channel->invites)-1; i>=0; i--)
	{
		ChatUser *invitee = userFindByContainerId(channel->invites[i]);
		if (invitee) 
		{
			channelLeave(invitee,channel->name,false);
		}
	}
	for(i=eaiSize(&channel->members)-1; i>=0; i--)
	{
		ChatUser *member = userFindByContainerId(channel->members[i]);
		if (member) 
		{
			channelLeave(member,channel->name,false);
		}
	}
	chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_DESTROY, channel->name, NULL, "Channel force killed");
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void channelCmdDestroy(U32 uID)
{
	ChatChannel *channel = channelFindByID(uID);
	if (channel)
		channelForceKill(channel);
}

int channelKill(ChatUser *user, const char *channel_name)
{
	ChatChannel	*channel = channelFindByName(channel_name);
	int i;

	if (!channel)
	{
		if (user)
		{
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelDNE", 
				STRFMT_STRING("Channel", channel_name), STRFMT_END);
		}
		chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_DESTROY, channel_name, user, "Channel did not exist");
		return CHATRETURN_CHANNEL_DNE;
	}

	// Make sure the channel isn't reserved
	if (channel->reserved) {
		if (user)
		{
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelReserved", 
				STRFMT_STRING("Channel", channel_name), STRFMT_END);
		}
		chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_DESTROY, channel_name, user, "Cannot destroy reserved channels");
		return CHATRETURN_CHANNEL_RESERVEDPREFIX;

	}

	// Make sure calling user has the CHANPERM_DESTROY privilege
	if (!channelUserHasPermission(channel, user, NULL, CHANPERM_DESTROY))
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_DESCRIPTION, channel_name, user, CHATRETURN_USER_PERMISSIONS);
		return CHATRETURN_USER_PERMISSIONS;
	}
	
	for (i=eaiSize(&channel->invites)-1; i>=0; i--)
	{
		ChatUser *invitee = userFindByContainerId(channel->invites[i]);
		if (invitee) 
		{
			channelLeave(invitee,channel_name,0);
		}
	}
	for(i=eaiSize(&channel->members)-1; i>=0; i--)
	{
		ChatUser *member = userFindByContainerId(channel->members[i]);
		if (member) 
		{
			channelLeave(member,channel_name,0);
		}
	}
	chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_DESTROY, channel_name, user, "Channel destroyed");
	// channelDestroy is called indirectly via channelLeave->channelDeleteIfEmpty() -Trev
	return CHATRETURN_FWD_NONE;
}

static int chanPopCompare(const ChatChannel **chan1, const ChatChannel **chan2)
{
	return eaiSize(&(*chan2)->online) - eaiSize(&(*chan1)->online);
}

void channelFind(ChatUser *user,char *mode,char *substring)
{
	int		i,found=0;
	ChatChannel	*chan;

	if (strcmp(mode,"list") == 0)
	{
		char **str = &substring;
		char *token;
		while (token = strsep(str,","))
		{
			int members = 0,online = 0;
			const char *description = "";
			chan = channelFindByName(token);
			if (chan)
			{
				members = eaiSize(&chan->members);
				online = eaiSize(&chan->online);
				description = chan->description;
			}
			//sendUserMsg(user,"Chaninfo %s %d %d %s",token,members,online,description);
			found++;
		}
	}
	else if (strcmp(mode,"memberof") == 0)
	{
		for(i=eaSize(&user->watching)-1;i>=0;i--)
		{
			chan = channelFindByID(user->watching[i]->channelID);
			//if (chan)
			//	sendUserMsg(user,"Chaninfo %s %d %d %s",chan->name,eaSize(&chan->members),eaSize(&chan->online),chan->description);
			found++;
		}
	}
	else //mode = "substring"
	{	
		ContainerIterator iterator = {0};
		int size;
		ChatChannel **chanlist = NULL;
		eaCreate(&chanlist);
		/*for(i=eaSize(&chat_db.channels)-1;i>=0;i--)
		{
			chan = chat_db.channels[i];
	
			if ((chan->access & CHANFLAGS_JOIN) && strstri(chan->name,substring))
			{
				eaPush(&chanlist,chan);
			}
		}*/
		objInitContainerIteratorFromType(GLOBALTYPE_CHATCHANNEL, &iterator);
		chan = objGetNextObjectFromIterator(&iterator);
		while (chan)
		{
			if ((chan->access & CHANFLAGS_JOIN) && strstri(chan->name,substring))
			{
				eaPush(&chanlist,chan);
			}
			chan = objGetNextObjectFromIterator(&iterator);
		}
		objClearContainerIterator(&iterator);

		eaQSort(chanlist, chanPopCompare);

		size = eaSize(&chanlist);
		for(i = 0; i < size; i++)
		{	
			chan = chanlist[i];
			//sendUserMsg(user,"Chaninfo %s %d %d %s",chan->name,eaSize(&chan->members),eaSize(&chan->online),chan->description);
			found++;
			if (found >= 20)
				break;
		}
	}
	/*if (found == 0)
	{
		sendUserMsg(user,"Chaninfo %s %d %d %s","",0,0,"");
	}*/
}

//////////////////////////////////////
// User Permission / Access Functions

bool channelUserHasTargetPermission (ChatChannel *channel, ChatUser *user, ChatUser *target, Watching *userWatching, U32 uAction)
{
	Watching *targetWatching;
	if (!channel || !user || !uAction)
		return false;
	// TODO GM / CSR override
	if (!userWatching)
	{
		userWatching = userFindWatching(user, channel);
		if (!userWatching)
			return false;
	}
	if (!channelUserHasPermission(channel, user, userWatching, uAction))
		return false;
	targetWatching = userFindWatching(target, channel);
	if (targetWatching && targetWatching->ePermissionLevel == CHANUSER_OWNER)
		return false; // Shortcut out, can never do anything to owner, even if you are the owner
	if (user == target)
		return true;
	if (targetWatching)
	{
		if (uAction & CHANPERM_PROMOTE)
		{
			// Can only promote to level below yours (must be two levels above current target level)
			if (userWatching->ePermissionLevel <= targetWatching->ePermissionLevel+1)
				return false;
		}
		else if (userWatching->ePermissionLevel <= targetWatching->ePermissionLevel)
			return false;
	}
	return true;
}

bool channelUserHasPermission (ChatChannel *channel, ChatUser *user, Watching *watching, U32 uAction)
{
	if (!channel || !user || !uAction)
		return false;
	// TODO GM / CSR override
	if (uAction == CHANPERM_SEND && userIsSilenced(user))
	{
		return false;
	}
	if (!watching)
	{
		watching = userFindWatching(user, channel);
		if (!watching)
			return false;
	}
	if (uAction == CHANPERM_SEND && channelIsUserSilenced(user->id, channel))
		return false;
	return ((channel->permissionLevels[watching->ePermissionLevel] & uAction) == uAction);
}

int channelSetLevelPermissions (ChatUser *user, ChatChannel *channel, ChannelUserLevel eLevel, U32 uPermissions)
{
	Watching *watching = userFindWatching(user, channel);
	if (!channelUserHasPermission(channel, user, watching, CHANPERM_MODIFYCHANNEL))
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel->name, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_MODIFYLEVEL, channel->name, user, CHATRETURN_USER_PERMISSIONS);
		return CHATRETURN_USER_PERMISSIONS;
	}
	// Must be of a strictly greater than permission level to set permissions
	if (watching->ePermissionLevel <= eLevel || !(eLevel < CHANUSER_COUNT))
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel->name, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_MODIFYLEVEL, channel->name, user, CHATRETURN_USER_PERMISSIONS);
		return CHATRETURN_USER_PERMISSIONS;
	}

	CONTAINER_NOCONST(ChatChannel,channel)->permissionLevels[eLevel] = uPermissions;
	objContainerMarkModified(objGetContainer(GLOBALTYPE_CHATCHANNEL, channel->uKey));
	chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_MODIFYLEVEL, channel->name, user, STACK_SPRINTF("Set level %d to %d", eLevel, uPermissions));
	return CHATRETURN_FWD_NONE;
}

void channelSendOnline(ChatChannel *channel, ChatUser *user)
{
	ChatChannelUpdateData updateData = {0};

	if (!devassert(user && channel))
		return;
	updateData.uTargetUserID = user->id;
	updateData.eUpdateType = CHANNELUPDATE_USER_ONLINE;
	updateData.channelInfo = ChatServerCreateChannelUpdate(user, channel, updateData.eUpdateType);
	updateData.bSendToOperators = true;
	ChannelUpdateFromGlobal(&updateData);
	StructDeInit(parse_ChatChannelUpdateData, &updateData);
}

void channelSendOffline(ChatChannel *channel, ChatUser *user)
{
	ChatChannelUpdateData updateData = {0};

	if (!devassert(user && channel))
		return;
	// Make sure this updates the online count, since this occurs before the channel update
	if (eaiFindAndRemove(&(CONTAINER_NOCONST(ChatChannel, channel))->online,user->id) >= 0 && !UserIsAdmin(user))
		channel->uOnlineCount--;

	updateData.uTargetUserID = user->id;
	updateData.eUpdateType = CHANNELUPDATE_USER_OFFLINE;
	updateData.channelInfo = ChatServerCreateChannelUpdate(user, channel, updateData.eUpdateType);
	updateData.bSendToOperators = true;
	ChannelUpdateFromGlobal(&updateData);
	StructDeInit(parse_ChatChannelUpdateData, &updateData);
}
