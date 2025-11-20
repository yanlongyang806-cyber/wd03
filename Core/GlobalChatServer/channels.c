#include "chatdb.h"
#include "ChatServer/chatShared.h"

#include "channels.h"
#include "chatGlobal.h"
#include "chatGlobalConfig.h"
#include "chatCommon.h"
#include "users.h"
#include "userPermissions.h"
#include "friendsIgnore.h"
#include "chatCommandStrings.h"
#include "xmpp/XMPP_Chat.h"

#include "objTransactions.h"
#include "chatUtilities.h"
#include "ChatServer.h"
#include "msgsend.h"
#include "objContainerIO.h"
#include "textparser.h"
#include "TextFilter.h"
#include "AutoTransDefs.h"

#include "AutoGen/channels_c_ast.h"
#include "AutoGen/chatdb_h_ast.h"
#include "Autogen/chatCommon_h_ast.h"
#include "AutoGen/GlobalChatServer_autotransactions_autogen_wrappers.h"

extern ParseTable parse_ChatChannelInfo[];
#define TYPE_parse_ChatChannelInfo ChatChannelInfo
extern bool gbGlobalChatResponse;
// Function Prototypes
void channelSetUserAccessHelper(ChatUser * user, ChatChannel * channel, ChatUser * target, Watching * watching, char * option);
static int channelSetUserOwner(ChatUser *originalOwner, ChatUser *target, ChatChannel *channel);
// -----------------------

void channelInitialize(NOCONST(ChatChannel) *channel, const char *channel_name, const char *description, U32 uReservedFlags)
{
	channel->name = StructAllocString(channel_name);
	channel->description = StructAllocString(description);
	channel->access = CHANFLAGS_COMMON;
	channel->reserved = uReservedFlags;

	channel->permissionLevels[CHANUSER_USER] = CHANPERM_DEFAULTUSER;
	channel->permissionLevels[CHANUSER_OPERATOR] = CHANPERM_DEFAULTOP;
	channel->permissionLevels[CHANUSER_ADMIN] = CHANPERM_DEFAULTADMIN;
	channel->permissionLevels[CHANUSER_OWNER] = CHANPERM_OWNER;

}

/*static opAllowed(ChatUser *user,Watching *watching,ChatUser *target)
{	
if (!watching || !user)
return 0;
if (user->access & CHANFLAGS_ADMIN)
return 1;
if (!(watching->access & CHANFLAGS_OPERATOR))
return 0;
if (!target || !(target->access & CHANFLAGS_ADMIN))
return 1;
return 0;
}*/

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
			STRFMT_STRING("Channel", channel_name), STRFMT_END);
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
		STRFMT_STRING("Channel", channel_name), 
		STRFMT_STRING("MemberList", pListString), STRFMT_END);
	channelGetMotd(user, channel, true);
	
	if (channel->description && channel->description[0])
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelDescription", 
			STRFMT_STRING("Channel", channel_name), 
			STRFMT_STRING("Description", channel->description), STRFMT_END);
	}
	estrDestroy(&pListString);
}

void channelGetList(ChatUser ***eaMembers, const char *channel_name)
{
	ChatChannel *channel;

	// Validate parameters.
	if (!eaMembers || !channel_name || !*channel_name)
	{
		devassert(0);
		return;
	}

	// Clear output array.
	eaClear(eaMembers);

	// Look up channel.
	channel = channelFindByName(channel_name);
	if (!channel)
		return;

	// Copy channel list.
	eaSetSize(eaMembers, ea32Size(&channel->members));
	EARRAY_CONST_FOREACH_BEGIN(*eaMembers, i, n);
	{
		ChatUser *user = userFindByContainerId(channel->members[i]);
		devassert(user);
		(*eaMembers)[i] = user;
	}
	EARRAY_FOREACH_END;
}

void channelGetListOnline(ChatUser ***eaMembers, const char *channel_name)
{
	ChatChannel *channel;

	// Validate parameters.
	if (!eaMembers || !channel_name || !*channel_name)
	{
		devassert(0);
		return;
	}

	// Clear output array.
	eaClear(eaMembers);

	// Look up channel.
	channel = channelFindByName(channel_name);
	if (!channel)
		return;

	// Copy channel list.
	eaSetSize(eaMembers, ea32Size(&channel->online));
	EARRAY_CONST_FOREACH_BEGIN(*eaMembers, i, n);
	{
		ChatUser *user = userFindByContainerId(channel->online[i]);
		devassert(user);
		(*eaMembers)[i] = user;
	}
	EARRAY_FOREACH_END;
}

int channelSize(ChatChannel * channel)
{
	return ( eaiSize(&channel->members) + eaiSize(&channel->invites));
}

AUTO_TRANSACTION 
	ATR_LOCKS(channel, ".uKey, .reserved, .uMemberCount, .name, .invites, .members")
	ATR_LOCKS(user, ".access_level, .watching[AO], .id, .invites");
enumTransactionOutcome trChannelJoin(ATR_ARGS, NOCONST(ChatUser) *user, NOCONST(ChatChannel) *channel, int bCreated)
{
	NOCONST(Watching) *watching;
	if (ISNULL(channel) || ISNULL(user))
		return TRANSACTION_OUTCOME_FAILURE;

	watching = StructAllocNoConst(parse_Watching);
	watching->name = StructAllocString(channel->name);
	watching->channelID = channel->uKey;
	//watching->access = (channel->access & CHANFLAGS_COPYMASK) | CHANFLAGS_JOIN;
	if (bCreated && !channel->reserved)
		watching->ePermissionLevel = CHANUSER_OWNER;
	else
		watching->ePermissionLevel = CHANUSER_USER;
	eaIndexedAdd(&user->watching, watching);

	eaiFindAndRemove(&channel->invites, user->id);
	eaiFindAndRemove(&user->invites, channel->uKey);
	eaiPush(&channel->members,user->id);
	if (user->access_level < ACCESS_GM) // Using UserIsAdmin doesn't work with ATR locking
		channel->uMemberCount++;
	return TRANSACTION_OUTCOME_SUCCESS;
}

static int channelJoinInternal(ChatUser *user, const char *channel_name, bool bJustCreated, int flags);

AUTO_STRUCT;
typedef struct ChannelCreateStruct
{
	ChatUser *user; AST(UNOWNED)
	U32 uChatServerID;
	int flags;
	char *channel_name;
} ChannelCreateStruct;

AUTO_STRUCT;
typedef struct PendingChannelJoin
{
	U32 uAccountID;
	U32 uChatServerID;
	int flags;
} PendingChannelJoin;
AUTO_STRUCT;
typedef struct PendingChannelCreate
{
	char *channel_name; AST(KEY)
	EARRAY_OF(PendingChannelJoin) ppJoins;
} PendingChannelCreate;
static EARRAY_OF(PendingChannelCreate) seaChannelCreates = NULL;

void trCreateChannel_CB(TransactionReturnVal *returnVal, ChannelCreateStruct *data)
{
	int idx = eaIndexedFindUsingString(&seaChannelCreates, data->channel_name);
	PendingChannelCreate *pending = NULL;

	if (idx >= 0)
		pending = eaRemove(&seaChannelCreates, idx);
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		U32 uID = atoi(returnVal->pBaseReturnVals->returnString);
		ChatChannel *channel = channelFindByID(uID);
		if (channel)
		{
			CmdContext context = {0};
			stashAddPointer(chat_db.channel_names, channel->name, channel, false);

			context.commandData = GetLocalChatLink(data->uChatServerID);
			forceSetCommandContext(&context);
			channelJoinInternal(data->user, channel->name, true, data->flags);

			if (pending)
			{
				EARRAY_FOREACH_BEGIN(pending->ppJoins, i);
				{
					ChatUser *user = userFindByContainerId(pending->ppJoins[i]->uAccountID);
					if (user)
					{
						context.commandData = GetLocalChatLink(pending->ppJoins[i]->uChatServerID);
						channelJoinInternal(user, channel->name, false, pending->ppJoins[i]->flags);
					}
				}
				EARRAY_FOREACH_END;
			}
			unsetCommandContext();
		}
		// TODO else error
	}
	//else
	if (pending)
		StructDestroy(parse_PendingChannelCreate, pending);
	StructDestroy(parse_ChannelCreateStruct, data);
}

// Add the user to the channel's member list, channel to the user's watching list, and remove invites
int channelAddUser(ChatChannel *channel, ChatUser *user, int flags)
{
	if (!channel || !user)
		return 0;

	if (channel->reserved)
	{
		// does nothing
	}
	else
	{
		bool bCreated = eaiSize(&channel->members) == 0;
		AutoTrans_trChannelJoin(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, GLOBALTYPE_CHATCHANNEL, channel->uKey, bCreated);
	}
	return 1;
}

static int channelCreateInternal(ChatUser *user, const char *channel_name, int flags, bool bAdminAccess, bool bJoinIfExisting)
{
	ChannelCreateStruct *dataStruct;
	NOCONST(ChatChannel) *channel;
	PendingChannelCreate *pendingChan;
	int result;
	bool bAlreadyExists = false;

	if (channel = CONTAINER_NOCONST(ChatChannel, channelFindByName(channel_name)))
	{
		if (bJoinIfExisting)
			return channelJoinInternal(user, channel_name, false, flags);
		bAlreadyExists = true;
	}
	else if (pendingChan = eaIndexedGetUsingString(&seaChannelCreates, channel_name))
	{
		if (bJoinIfExisting)
		{
			PendingChannelJoin *pJoin = StructCreate(parse_PendingChannelJoin);
			pJoin->uAccountID = user->id;
			pJoin->flags = flags;
			pJoin->uChatServerID = chatServerGetCommandLinkID();
			eaPush(&pendingChan->ppJoins, pJoin);
			return CHATRETURN_FWD_NONE;
		}
		bAlreadyExists = true;
	}

	if (bAlreadyExists)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelAlreadyExists", 
			STRFMT_STRING("Channel", channel ? channel->name : channel_name), STRFMT_END);
		chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_CREATE, channel_name, user, "Already exists");
		return CHATRETURN_CHANNEL_ALREADYEXISTS;
	}

	if (result = channelIsNameValid(channel_name, bAdminAccess, flags))
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
		xcase CHATRETURN_INVALIDNAME:
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "NameFormat_InvalidCharacters", 
				STRFMT_END);
		}
		chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_JOIN, channel_name, user, errorString);
		estrDestroy(&errorString);
		return CHATRETURN_INVALIDNAME;
	}

	// Don't allow channel creation if the user is already at channel maximum.
	if( !flags && ChatServerGetMaxChannels() <= userWatchingCount(user))
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_WatchingMax", 
			STRFMT_INT("Limit", ChatServerGetMaxChannels()), STRFMT_END);
		return CHATRETURN_CHANNEL_WATCHINGMAX;
	}

	channel = StructCreateNoConst(parse_ChatChannel);
	channelInitialize(channel, channel_name, "", flags);

	if (!seaChannelCreates)
		eaIndexedEnable(&seaChannelCreates, parse_PendingChannelCreate);
	pendingChan = StructCreate(parse_PendingChannelCreate);
	pendingChan->channel_name = StructAllocString(channel_name);
	eaIndexedAdd(&seaChannelCreates, pendingChan);

	dataStruct = StructCreate(parse_ChannelCreateStruct);
	dataStruct->user = user;
	dataStruct->flags = flags;
	dataStruct->channel_name = StructAllocString(channel_name);
	dataStruct->uChatServerID = GetLocalChatLinkID(chatServerGetCommandLink());
	objRequestContainerCreateLocal(objCreateManagedReturnVal(trCreateChannel_CB, dataStruct), GLOBALTYPE_CHATCHANNEL, channel);
	return CHATRETURN_FWD_NONE;
}

static int channelJoinInternal(ChatUser *user, const char *chan_name, bool bJustCreated, int flags)
{
	static char *channel_name = NULL;
	ChatChannel *channel = NULL;
	Watching	*watching = NULL;
	Watching	***list;
	ContainerID	**channelList;

	if (chan_name) {
		estrCopy2(&channel_name, chan_name);
		removeLeadingAndFollowingSpaces(channel_name);
	} else {
		estrClear(&channel_name);
	}

	channel = channelFindByName(channel_name);
	if (!channel)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelDNE", 
			STRFMT_STRING("Channel", channel_name), STRFMT_END);
		return CHATRETURN_UNSPECIFIED;
	}

	list = &((Watching**) user->watching);
	channelList = (ContainerID**) &channel->members;
	watching = userFindWatching(user, channel);

	if (!watching)
	{
		int inviteIdx = -1;
		if (   ! (channel->access & CHANFLAGS_JOIN) 
			&& !UserIsAdmin(user) 
			&& eaiFind(&user->invites, channel->uKey) < 0)
		{
			// TODO new custom error message for Private Channel
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_PermissionDenied", STRFMT_END);
			return CHATRETURN_UNSPECIFIED;
		}
	}
	else
	{
		if (!channel->reserved)
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelAlreadyMember", 
				STRFMT_STRING("Channel", channel->name), STRFMT_END);
		return CHATRETURN_CHANNEL_ALREADYMEMBER;
	}

	// Check if the channel is full.
	if (   ! flags
		&&   channelSize(channel) >= ChatServerGetChannelSizeCap()
		&&   eaiFind(&user->invites, channel->uKey) < 0)			
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel->name, "ChatServer_ChannelFull", 
			STRFMT_STRING("Channel", channel->name), STRFMT_END);
		return CHATRETURN_CHANNEL_FULL;
	}

	// Check if the user is at the channel limit yet.
	if( !flags && ChatServerGetMaxChannels() <= userWatchingCount(user))
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, "ChatServer_WatchingMax", 
			STRFMT_INT("Limit", ChatServerGetMaxChannels()), STRFMT_END);
		return CHATRETURN_CHANNEL_WATCHINGMAX;
	}

	AutoTrans_trChannelJoin(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, GLOBALTYPE_CHATCHANNEL, channel->uKey, bJustCreated);

	channelOnline(channel, user);
	XMPPChat_NotifyChatOnline(channel, user);

	{
		char commandBuffer[256];
		sendChannelUpdate((ChatChannel*) channel);
		sendUserUpdate(user);
		sprintf(commandBuffer, "ChatServerJoinChannelFromGlobal %d \"%s\"", user->id, channel->name);
		sendCommandToUserLocal(user, commandBuffer, NULL);
		channelUserJoinUpdate(user, channel, false);
		// Leave the sendcommandtouserlocal for backwards compatibility
	}
	sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelJoin", 
		STRFMT_STRING("Channel", channel->name), STRFMT_END);
	return CHATRETURN_FWD_NONE;
}

int channelJoin(ChatUser *user,const char *channel_name, int flags)
{
	int result = channelJoinInternal(user, channel_name, false, flags);
	chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_JOIN, channel_name, user, result);
	return result;
}

int channelCreate(ChatUser *user,const char *chan_name, int flags, bool adminAccess)
{
	static char *channel_name = NULL;
	int result;

	estrCopy2(&channel_name, chan_name);
	estrTrimLeadingAndTrailingWhitespace(&channel_name);

	result = channelCreateInternal(user, channel_name, flags, adminAccess, false);
	chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_JOIN, channel_name, user, result);

	estrDestroy(&channel_name);
	return result;
}

int channelJoinOrCreate(ChatUser *user,const char *chan_name, int flags, bool adminAccess)
{
	static char *channel_name = NULL;
	int result;

	estrCopy2(&channel_name, chan_name);
	estrTrimLeadingAndTrailingWhitespace(&channel_name);

	result = channelCreateInternal(user, channel_name, flags, adminAccess, true);
	chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_JOIN, channel_name, user, result);

	estrDestroy(&channel_name);
	return result;
}

AUTO_TRANSACTION
	ATR_LOCKS(channel, ".uKey, .invites")
	ATR_LOCKS(user, ".id, .invites");
enumTransactionOutcome trChannelInvite(ATR_ARGS, NOCONST(ChatChannel) *channel, NOCONST(ChatUser) *user)
{
	eaiPushUnique(&channel->invites, user->id);
	eaiPushUnique(&user->invites, channel->uKey);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(channel, ".uKey, .invites")
	ATR_LOCKS(user, ".id, .invites");
enumTransactionOutcome trChannelRemoveInvite(ATR_ARGS, NOCONST(ChatChannel) *channel, NOCONST(ChatUser) *user)
{
	eaiFindAndRemove(&channel->invites, user->id);
	eaiFindAndRemove(&user->invites, channel->uKey);
	return TRANSACTION_OUTCOME_SUCCESS;
}

int channelUninvite(ChatUser * user, ChatUser *target, const char *channel_name, bool bOverride)
{
	ChatChannel *channel = channelFindByName(channel_name);
	if (!user && !bOverride)
		return CHATRETURN_UNSPECIFIED;
	if (!target)
		return CHATRETURN_USER_DNE;
	if (!channel)
	{
		if (user)
		{
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelDNE", 
				STRFMT_STRING("Channel", channel_name), STRFMT_END);
			chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_INVITE, CHATCOMMAND_CHANNEL_UNINVITE, 
				channel_name, user, target, CHATRETURN_CHANNEL_DNE);
		}
		return CHATRETURN_CHANNEL_DNE;
	}	
	if (!bOverride && !channelUserHasPermission(channel, user, NULL, CHANPERM_INVITE))
	{ // user will always be non-NULL here; no check necessary
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_INVITE, CHATCOMMAND_CHANNEL_UNINVITE, 
			channel_name, user, target, CHATRETURN_USER_PERMISSIONS);
		return CHATRETURN_USER_PERMISSIONS;
	}
	AutoTrans_trChannelRemoveInvite(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATCHANNEL, channel->uKey, 
		GLOBALTYPE_CHATUSER, target->id);

	if (userOnline(target))
	{ // does not return a message to target
		char commandBuffer[256];
		sendUserUpdate(target);
		sprintf(commandBuffer, "ChatServerLeaveChannelFromGlobal %d \"%s\"", target->id, channel->name);
		sendCommandToUserLocal(target, commandBuffer, NULL);
	}
	if (user)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_Uninvite", 
			STRFMT_STRING("User", target->handle), STRFMT_STRING("Channel", channel->name), STRFMT_END);
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_INVITE, CHATCOMMAND_CHANNEL_UNINVITE, 
			channel_name, user, target, CHATRETURN_NONE);
	}
	return CHATRETURN_FWD_NONE;
}

int channelDeclineInvite(ChatUser * user, const char *channel_name)
{
	ChatChannel *channel = channelFindByName(channel_name);
	if (!user)
		return CHATRETURN_UNSPECIFIED;
	if (!channel)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelDNE", 
			STRFMT_STRING("Channel", channel_name), STRFMT_END);
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_INVITE, CHATCOMMAND_CHANNEL_DECLINEINVITE, 
			channel_name, user, NULL, CHATRETURN_CHANNEL_DNE);
		return CHATRETURN_CHANNEL_DNE;
	}	
	AutoTrans_trChannelRemoveInvite(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATCHANNEL, channel->uKey, 
		GLOBALTYPE_CHATUSER, user->id);

	{
		char commandBuffer[256];
		sendUserUpdate(user);
		sprintf(commandBuffer, "ChatServerLeaveChannelFromGlobal %d \"%s\"", user->id, channel->name);
		sendCommandToUserLocal(user, commandBuffer, NULL);
	}
	sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel->name, "ChatServer_DeclineInvite", 
		STRFMT_STRING("Channel", channel->name), STRFMT_END);
	chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_INVITE, CHATCOMMAND_CHANNEL_DECLINEINVITE, 
		channel_name, user, NULL, CHATRETURN_NONE);
	return CHATRETURN_FWD_NONE;
}

int channelInvite(ChatUser * user, const char *channel_name, ChatUser* invitee)
{
	ChatChannel *channelConst = channelFindByName(channel_name);
	NOCONST(ChatChannel) *channel = CONTAINER_NOCONST(ChatChannel, channelConst);

	if (!channel)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelDNE", 
			STRFMT_STRING("Channel", channel_name), STRFMT_END);
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_INVITE, CHATCOMMAND_CHANNEL_TARGETINVITE, 
			channel_name, user, NULL, CHATRETURN_CHANNEL_DNE);
		return CHATRETURN_CHANNEL_DNE;
	}
	if (channel->reserved)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelDNE", 
			STRFMT_STRING("Channel", channel_name), STRFMT_END);
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_INVITE, CHATCOMMAND_CHANNEL_TARGETINVITE, 
			channel_name, user, NULL, CHATRETURN_CHANNEL_RESERVEDPREFIX);
		return CHATRETURN_CHANNEL_RESERVEDPREFIX;
	}

	if (!channelUserHasPermission(channelConst, user, NULL, CHANPERM_INVITE))
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_INVITE, CHATCOMMAND_CHANNEL_TARGETINVITE, 
			channel_name, user, NULL, CHATRETURN_USER_PERMISSIONS);
		return CHATRETURN_USER_PERMISSIONS;
	}
	if(channelSize(channelConst) >= ChatServerGetChannelSizeCap())
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelFull", 
			STRFMT_STRING("Channel",  channel->name), STRFMT_END);
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_INVITE, CHATCOMMAND_CHANNEL_TARGETINVITE, 
			channel_name, user, NULL, CHATRETURN_CHANNEL_FULL);
		return CHATRETURN_CHANNEL_FULL;
	}

	if (!invitee)
	{
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_INVITE, CHATCOMMAND_CHANNEL_TARGETINVITE, 
			channel_name, user, NULL, CHATRETURN_USER_DNE);
		return CHATRETURN_USER_DNE;
	}

	if (userIsIgnoring(invitee, user))
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_BeingIgnored", 
			STRFMT_STRING("User", invitee->handle), STRFMT_END);
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_INVITE, CHATCOMMAND_CHANNEL_TARGETINVITE, 
			channel_name, user, invitee, CHATRETURN_USER_OFFLINE);
		return CHATRETURN_USER_IGNORING;
	}
	if (channelFindWatching(invitee, channel_name)) 
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelInviteeAlreadyMember", 
			STRFMT_STRING("User", invitee->handle), STRFMT_STRING("Channel",  channel->name), STRFMT_END);
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_INVITE, CHATCOMMAND_CHANNEL_TARGETINVITE, 
			channel_name, user, invitee, CHATRETURN_CHANNEL_ALREADYMEMBER);
		return CHATRETURN_CHANNEL_ALREADYMEMBER;
	}
	if (eaiFind(&channel->invites, invitee->id) >= 0) 
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelInviteeAlreadyInvited", 
			STRFMT_STRING("User", invitee->handle), STRFMT_STRING("Channel",  channel->name), STRFMT_END);
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_INVITE, CHATCOMMAND_CHANNEL_TARGETINVITE, 
			channel_name, user, invitee, CHATRETURN_CHANNEL_ALREADYINVITED);
		return CHATRETURN_CHANNEL_ALREADYINVITED;
	}
	chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_INVITE, CHATCOMMAND_CHANNEL_TARGETINVITE, 
		channel_name, user, invitee, CHATRETURN_NONE);
	
	AutoTrans_trChannelInvite(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATCHANNEL, channel->uKey, 
		GLOBALTYPE_CHATUSER, invitee->id);

	broadcastTranslatedMessageToUser(invitee, kChatLogEntryType_System, channel_name, "ChatServer_ChannelInviteIn", 
		STRFMT_STRING("User", user->handle), STRFMT_STRING("Channel",  channel->name), STRFMT_END);
	sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelInviteOut", 
		STRFMT_STRING("User", invitee->handle), STRFMT_STRING("Channel",  channel->name), STRFMT_END);
	sendChannelUpdate(channelConst);
	sendUserUpdate(invitee);
	if (userCharacterOnline(invitee))
	{
		char commandBuffer[256];
		sprintf(commandBuffer, "ChatServerJoinChannelFromGlobal %d \"%s\"", invitee->id, channel_name);
		sendCommandToUserLocal(invitee, commandBuffer, NULL);
		// Leave the sendcommandtouserlocal for backwards compatibility
	}
	channelUserJoinUpdate(invitee, (ChatChannel*)channel, true);
	return CHATRETURN_FWD_NONE;
}

//Syntax: <channel name> <type> <name0> ... <nameN>
// Where N >= 0
// 'type' specifies the format of the names (kChatId_AuthId, kChatId_DbId, kChatId_Handle)
void channelCsrInvite(ChatUser *user,char **args, int count)
{
	// TODO 
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
		ChatUser * target;
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

void channelOffline(ChatChannel *channel,ChatUser *user)
{
	//int		i,resend=0;

	if (eaiFind(&channel->online,user->id) < 0)
		return;

	//for(i=eaSize(&channel->online)-1;i>=0;i--)
	//{
	//	if (resend)
	//		resendMsg(channel->online[i]);
	//	else
	//		resend = sendUserMsg(channel->online[i],"Leave %s %d 0",channel->name,user->id);

	//	updateCrossShardStats(user, channel->online[i]);
	//}
	if (eaiFindAndRemove(&CONTAINER_NOCONST(ChatChannel, channel)->online,user->id) >= 0 && !UserIsAdmin(user))
	{
		if (channel->uOnlineCount > 0)
			channel->uOnlineCount--;
	}
	// TODO update channels on local shards?
	chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_OFFLINE, channel->name, user, CHATRETURN_NONE);
}

void channelDestroy(ChatChannel * channel)
{
	stashRemovePointer(chat_db.channel_names, channel->name, NULL);

	if (!IsChannelFlagShardOnly(channel->reserved))
	{
		int iInviteCount = eaiSize(&channel->invites);
		U32 uKey = channel->uKey;

		if (iInviteCount > 0)
		{
			int i;
			U32 *inviteCopy = NULL;
			eaiCopy(&inviteCopy, &channel->invites);

			for (i=iInviteCount-1; i>=0; i--)
			{
				ChatUser *user = userFindByContainerId(inviteCopy[i]);
				channelUninvite(NULL, user, channel->name, true);
			}
			eaiDestroy(&inviteCopy);
		}
		objRequestContainerDestroyLocal(NULL, GLOBALTYPE_CHATCHANNEL, channel->uKey);
		sendChannelDeletion(uKey);
	}
	else
	{
		StructDestroy(parse_ChatChannel, channel);
	}
}

bool channelDeleteIfEmpty(ChatDb *db,ChatChannel *channel)
{
	// Only considers members - invited users aren't counted and are removed by channelDestroy
	if (channel && eaiSize(&channel->members) <= 0)
	{
		chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_DESTROY, channel->name, NULL, "Channel empty");
		channelDestroy(channel);
		return true;
	}
	return false;
}

int channelLeave(ChatUser *user, const char *channel_name, bool bIsKicking)
{
	ChatChannel *channel = channelFindByName(channel_name);
	Watching *watch = NULL;
	bool bDeleted = false;
	U32 uDeletedID = 0;
	bool bUserWasInvited = false;

	if (!channel)
	{
		if (!bIsKicking)
		{
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelDNE", STRFMT_STRING("Channel", channel_name), STRFMT_END);
			chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_LEAVE, channel_name, user, CHATRETURN_CHANNEL_NOTMEMBER);
		}
		return CHATRETURN_CHANNEL_NOTMEMBER;
	}

	watch = userFindWatching(user, channel);
	// Must be done before leaving channel
	if (watch && watch->ePermissionLevel == CHANUSER_OWNER)
		chooseNewChannelOwner(user, channel);

	// The following is done in two lines to avoid short circuiting
	// because we want to make sure we find & remove from 
	// both lists, but count having been in either list as
	// having been invited.
	bUserWasInvited = eaiFind(&channel->invites, user->id) >= 0;
	bUserWasInvited = bUserWasInvited || eaiFind(&user->invites, channel->uKey) >= 0;

	if (!channelRemoveUser(channel, user) && !bUserWasInvited)
	{
		if (!bIsKicking)
		{
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_NotInChannel", 
				STRFMT_STRING("Channel", channel->name), STRFMT_END);
			chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_LEAVE, channel_name, user, CHATRETURN_CHANNEL_NOTMEMBER);
		}
		return CHATRETURN_CHANNEL_NOTMEMBER;
	}

	XMPPChat_NotifyChatPart(channel, user, bIsKicking);

	// Log before any other things happen, including channelDestroy
	chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_LEAVE, channel_name, user, CHATRETURN_NONE);
	if (userCharacterOnline(user))
	{
		if (bIsKicking)
		{
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelKicked", 
				STRFMT_STRING("Channel", channel->name), STRFMT_END);
		} else if (bUserWasInvited) {
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelUninvited", 
				STRFMT_STRING("Channel", channel->name), STRFMT_END);
		} else {
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelLeave", 
				STRFMT_STRING("Channel", channel->name), STRFMT_END);
		}
	}
	if (!(channel->reserved & CHANNEL_SPECIAL_GLOBAL)) // GCS only cares about SPECIAL_GLOBAL
	{
		uDeletedID = channel->uKey;
		bDeleted = channelDeleteIfEmpty(&chat_db,channel);
	}
	{
		char commandBuffer[256];
		if (!bDeleted)
		{
			sendChannelUpdate((ChatChannel*) channel);
			channelUserRemoveUpdate(user, channel);
		}
		sendUserUpdate(user);
		sprintf(commandBuffer, "ChatServerLeaveChannelFromGlobal %d \"%s\"", user->id, channel_name);
		sendCommandToUserLocal(user, commandBuffer, NULL);
		// Leave the sendcommandtouserlocal for backwards compatibility
	}
	return CHATRETURN_FWD_NONE;
}

void channelForward(SA_PARAM_OP_VALID const ChatMessage *pMsg) {
	int i;
	ChatChannel	*channel;

	if (pMsg) {
		channel = channelFindByName(pMsg->pchChannel);
		if (channel)
		{
			for (i=eaiSize(&channel->online)-1; i>=0; i--)
			{
				ChatUser *chanuser = userFindByContainerId(channel->online[i]);
				if (chanuser && (!pMsg->pFrom || !userIsIgnoringByID(chanuser, pMsg->pFrom->accountID, pMsg->pFrom->bIsGM)))
				{
					ChatServerMessageSend(chanuser->id, pMsg);
				}
			}
		}
	}
}

void channelSendSpecial(SA_PARAM_OP_VALID ChatUser *pFromUser, SA_PARAM_OP_VALID const ChatMessage *pMsg, U32 shardId)
{
	char *commandBuffer = NULL;
	char *encodedMsg = NULL;
	NetLink *toLink = GetLocalChatLink(shardId);

	if (!toLink)
		return;

	if (!pFromUser | !pMsg) {
		return;
	}

	if (!ChatServerIsValidMessage(pFromUser, pMsg->pchText)) {
		return;
	}

	estrStackCreate(&commandBuffer);
	estrStackCreate(&encodedMsg);
	ChatCommon_EncodeMessage(&encodedMsg, pMsg);
	estrPrintf(&commandBuffer, "ChatServerChannelForwardEncoded %s", encodedMsg);

	sendCommandToLink(toLink, commandBuffer);

	estrDestroy(&encodedMsg);
	estrDestroy(&commandBuffer);

	XMPPChat_RecvSpecialMessage(pMsg);

	chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, pMsg->pchText);
}

void channelSend(SA_PARAM_OP_VALID ChatUser *pFromUser, SA_PARAM_OP_VALID const ChatMessage *pMsg)
{
	Watching	*watching;
	ChatChannel	*channel;
	int			i;

	if (!pFromUser | !pMsg) {
		return;
	}

	if (!ChatServerIsValidMessage(pFromUser, pMsg->pchText)) {
		return;
	}

	channel = channelFindByName(pMsg->pchChannel);
	if (!channel)
	{
		sendTranslatedMessageToUser(pFromUser, kChatLogEntryType_System, NULL, "ChatServer_ChannelDNE", STRFMT_STRING("Channel", pMsg->pchChannel), STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, CHATRETURN_CHANNEL_DNE);
		return;
	}

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
		sendTranslatedMessageToUser(pFromUser, kChatLogEntryType_System, NULL, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, CHATRETURN_USER_PERMISSIONS);
		return;
	}

	watching = channelFindWatching(pFromUser,pMsg->pchChannel);
	if (!watching)
	{
		sendTranslatedMessageToUser(pFromUser, kChatLogEntryType_System, NULL, "ChatServer_NotInChannel", 
			STRFMT_STRING("Channel", channel->name), STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, CHATRETURN_USER_PERMISSIONS);
		return;
	}

	// Non-User channel members ignore the mute - TODO - GM Override?
	if (watching->ePermissionLevel == CHANUSER_USER && !(channel->access & CHANFLAGS_SEND) ) 
	{
		sendTranslatedMessageToUser(pFromUser, kChatLogEntryType_System, NULL, "ChatServer_ChannelMuted", STRFMT_STRING("Channel", channel->name), STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, CHATRETURN_USER_PERMISSIONS);
		return;
	}

	if (!channelUserHasPermission(channel, pFromUser, watching, CHANPERM_SEND))
	{
		sendTranslatedMessageToUser(pFromUser, kChatLogEntryType_System, NULL, "ChatServer_UserMuted", STRFMT_STRING("Channel", channel->name), STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, CHATRETURN_USER_PERMISSIONS);
		return;
	}

	{
		char *commandBuffer = NULL;
		char *encodedMsg = NULL;
		U32 *eaiChatServerIDs = NULL;
		int commandLinkID = GetLocalChatLinkID(chatServerGetCommandLink());

		for (i=eaiSize(&channel->online)-1; i>=0; i--)
		{
			ChatUser *chanuser = userFindByContainerId(channel->online[i]);
			if (chanuser && !userIsIgnoring(chanuser, pFromUser) && isWhitelisted(pFromUser, chanuser, pMsg->pchChannel, chatServerGetCommandLinkID()))
			{
				int j;
				for (j=eaSize(&chanuser->ppPlayerInfo)-1; j>=0; j--)
				{
					eaiPushUnique(&eaiChatServerIDs, chanuser->ppPlayerInfo[j]->uChatServerID);
				}
			}
		}

		estrStackCreate(&commandBuffer);
		estrStackCreate(&encodedMsg);
		ChatCommon_EncodeMessage(&encodedMsg, pMsg);
		estrPrintf(&commandBuffer, "ChatServerChannelForwardEncoded %s", encodedMsg);

		for (i=eaiSize(&eaiChatServerIDs)-1; i>=0; i--)
		{
			// Skip forwarding to the originating shard, unless its XMPP.
			if (eaiChatServerIDs[i] != commandLinkID || eaiChatServerIDs[i] == XMPP_CHAT_ID)
			{
				if (eaiChatServerIDs[i] == XMPP_CHAT_ID)
					XMPPChat_NotifyChatMessage(channel, pMsg);
				else
				{
					NetLink *toLink = GetLocalChatLink(eaiChatServerIDs[i]);
					sendCommandToLink(toLink, commandBuffer);
				}
			}
		}
		eaiDestroy(&eaiChatServerIDs);
		estrDestroy(&encodedMsg);
		estrDestroy(&commandBuffer);
	}
	chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, pMsg->pchText);
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

int parseChannelAccessTranslateOptions(char *options)
{
	int flag = 0;
	char * optionCopy = strdup(options);
	char *tokContext = NULL;
	char *curTok;
	bool bFailedParse = false;

	curTok = strtok_s(optionCopy, " ", &tokContext);

	while (curTok)
	{
		unsigned char *s = curTok;
		if(!*s || (*s != '-' && *s != '+'))
		{
			bFailedParse = true;
			break;
		}
		s++;
		flag = 0;
		if (stricmp(s,"Join")==0)
		{
			if (s[-1] == '-') flag |= CHANACCESS_JOIN_REMOVE;
			else flag |= CHANACCESS_JOIN_ADD;
		}
		else if (stricmp(s,"Send")==0)
		{
			if (s[-1] == '-') flag |= CHANACCESS_SEND_REMOVE;
			else flag |= CHANACCESS_SEND_ADD;
		}
		else if (stricmp(s,"Operator")==0)
		{
			if (s[-1] == '-') flag |= CHANACCESS_OPDEFAULT_REMOVE;
			else flag |= CHANACCESS_OPDEFAULT_ADD;
		}
		else
		{
			bFailedParse = true;
			break;
		}
		curTok = strtok_s(NULL, " ", &tokContext);
	}
	free(optionCopy);
	if (bFailedParse)
		return -1;
	return flag;
}

void channelAccessApplyFlags(ChannelAccess *pAccess, int iFlags)
{
	if (iFlags & CHANACCESS_JOIN_REMOVE)
	{
		*pAccess &= ~CHANFLAGS_JOIN;
	}
	if (iFlags & CHANACCESS_SEND_REMOVE)
	{
		*pAccess &= ~CHANFLAGS_SEND;
	}
	if (iFlags & CHANACCESS_OPDEFAULT_REMOVE)
	{
		*pAccess &= ~CHANFLAGS_OPERATOR;
	}

	if (iFlags & CHANACCESS_JOIN_ADD)
	{
		*pAccess |= CHANFLAGS_JOIN;
	}
	if (iFlags & CHANACCESS_SEND_ADD)
	{
		*pAccess |= CHANFLAGS_SEND;
	}
	if (iFlags & CHANACCESS_OPDEFAULT_ADD)
	{
		*pAccess |= CHANFLAGS_OPERATOR;
	}
}

AUTO_TRANSACTION ATR_LOCKS(channel, ".access");
enumTransactionOutcome trChangeChannelAccess(ATR_ARGS, NOCONST(ChatChannel) *channel, int access)
{
	channel->access = (ChannelAccess) access;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".id, .watching") ATR_LOCKS(channel, ".name, .mutedUsers");
enumTransactionOutcome trChangeUserAccess(ATR_ARGS, NOCONST(ChatUser) *user, NOCONST(ChatChannel) *channel, int uAddFlags, int uRemoveFlags)
{
	NOCONST(Watching) *watch = eaIndexedGetUsingString(&user->watching, channel->name);
	if (watch)
	{
		if (uAddFlags & CHANPERM_MUTE)
		{
			watch->bSilenced = false; // deprecated
			eaiBFindAndRemove(&channel->mutedUsers, user->id);
		}
		if (uAddFlags & CHANPERM_PROMOTE)
		{
			// Promoting to admin level removes user silence
			watch->ePermissionLevel = min(watch->ePermissionLevel+1, CHANUSER_COUNT-1);
			if (watch->ePermissionLevel >= CHANUSER_ADMIN)
			{
				watch->bSilenced = false; // deprecated
				eaiBFindAndRemove(&channel->mutedUsers, user->id);
			}
		}

		if (uRemoveFlags & CHANPERM_MUTE)
		{
			watch->bSilenced = true; // deprecated
			eaiBInsertUnique(&channel->mutedUsers, user->id);
		}
		if (uRemoveFlags & CHANPERM_DEMOTE)
			watch->ePermissionLevel = max(watch->ePermissionLevel-1, 0);
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION ATR_LOCKS(owner, ".watching") ATR_LOCKS(target, ".id, .watching") ATR_LOCKS(channel, ".name, .mutedUsers");
enumTransactionOutcome trPassChannelOwnership(ATR_ARGS, NOCONST(ChatUser) *owner, NOCONST(ChatUser) *target, NOCONST(ChatChannel) *channel)
{
	NOCONST(Watching) *oldWatch = NULL, *newWatch = NULL;
	oldWatch = eaIndexedGetUsingString(&owner->watching, channel->name);
	newWatch = eaIndexedGetUsingString(&target->watching, channel->name);
	if (oldWatch && newWatch && oldWatch->ePermissionLevel == CHANUSER_OWNER)
	{
		oldWatch->ePermissionLevel = CHANUSER_USER;
		newWatch->ePermissionLevel = CHANUSER_OWNER;
		eaiBFindAndRemove(&channel->mutedUsers, target->id);
		newWatch->bSilenced = false; // deprecated
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

// This should ONLY be called if there is no channel owner for some reason!
AUTO_TRANSACTION ATR_LOCKS(target, ".id, .watching") ATR_LOCKS(channel, ".name, .mutedUsers");
enumTransactionOutcome trAddChannelOwnership(ATR_ARGS, NOCONST(ChatUser) *target, NOCONST(ChatChannel) *channel)
{
	NOCONST(Watching) *watch = eaIndexedGetUsingString(&target->watching, channel->name);
	if (watch)
	{
		watch->ePermissionLevel = CHANUSER_OWNER;
		eaiBFindAndRemove(&channel->mutedUsers, target->id);
		watch->bSilenced = false; // deprecated
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

	if (!channel->reserved)
	{
		AutoTrans_trPassChannelOwnership(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, originalOwner->id, 
			GLOBALTYPE_CHATUSER, target->id, GLOBALTYPE_CHATCHANNEL, channel->uKey);
		sendUserUpdate(target);
		sendChannelUpdate(channel);
	}

	return CHATRETURN_FWD_NONE;
}

void channelSendUserAccessChangeMessage(ChatUser *user, ChatUser *target, ChatChannel *channel, 
	U32 uAddFlags, U32 uRemoveFlags, ChannelUserLevel ePermissionLevel)
{
	ChatAccessChangeMessage message = {0};
	char *pMessageString = NULL;
	char *pCommandString = NULL;
	NetLink *link = chatServerGetCommandLink();

	message.uAccountID = user->id;
	message.uChannelID = channel->uKey;
	message.targetHandle = StructAllocString(target->handle);
	message.uAddFlags = uAddFlags;
	message.uRemoveFlags = uRemoveFlags;
	message.ePermissionLevel = ePermissionLevel;

	estrStackCreate(&pMessageString);
	ParserWriteTextEscaped(&pMessageString, parse_ChatAccessChangeMessage, &message, 0, 0, 0);
	estrPrintf(&pCommandString, "ChatServer_ReceiveChannelAccessChangeMessage %s", pMessageString);
	if (link)
		sendCommandToLink(link, pCommandString);
	else
		sendCommandToUserLocal(user, pCommandString, NULL);
	if (user != target)
	{
		estrPrintf(&pCommandString, "ChatServer_ReceiveChannelAccessChange %s", pMessageString);
		sendCommandToUserLocal(target, pCommandString, NULL);
	}
	estrDestroy(&pMessageString);
	estrDestroy(&pCommandString);
	StructDeInit(parse_ChatAccessChangeMessage, &message);
}

int channelSetUserAccess(ChatUser *user, const char *channel_name, ChatUser *target, U32 uAddFlags, U32 uRemoveFlags)
{
	ChatChannel * channel = channelFindByName(channel_name);
	Watching *watching = userFindWatching(user,channel);
	Watching *targetWatch = userFindWatching(target, channel);
	U32 uActions = 0;

	if (!user)
		return CHATRETURN_USER_PERMISSIONS;
	if (!channel)
		return CHATRETURN_CHANNEL_DNE;

	uActions = uAddFlags | uRemoveFlags;

	if (!channelUserHasTargetPermission(channel, user, target, watching, uActions))
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_MODIFYUSER, CHATCOMMAND_CHANNEL_MODIFYTARGET, 
			channel_name, user, target, CHATRETURN_USER_PERMISSIONS);
		return CHATRETURN_USER_PERMISSIONS;
	}

	if (!targetWatch)
	{
		if (uRemoveFlags & CHANPERM_KICK)
		{
			int result = channelLeave(target, channel_name, true);
			if (result == CHATRETURN_FWD_NONE)
			{
				sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_Uninvite", 
					STRFMT_STRING("User", target->handle), STRFMT_STRING("Channel", channel->name), STRFMT_END);
				chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_INVITE, CHATCOMMAND_CHANNEL_UNINVITE, 
					channel->name, user, target, CHATRETURN_NONE);
				sendUserUpdate(target);
			}
			return result;
		}
		else
		{
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_UserNotInChannel", 
				STRFMT_STRING("User", target->handle), STRFMT_STRING("Channel", channel_name), STRFMT_END);
			chatServerLogChannelTargetCommand(CHATCOMMAND_CHANNEL_MODIFYUSER, CHATCOMMAND_CHANNEL_MODIFYTARGET, 
				channel_name, user, target, "[Error: Target not in channel]");
			return CHATRETURN_CHANNEL_NOTMEMBER;
		}
	}

	if (!channel->reserved)
		AutoTrans_trChangeUserAccess(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, target->id, GLOBALTYPE_CHATCHANNEL, channel->uKey, uAddFlags, uRemoveFlags);
	if (uRemoveFlags & CHANPERM_KICK)
		channelLeave(target,channel->name,true);

	{ // Logging stuff
		channelSendUserAccessChangeMessage(user, target, channel, uAddFlags, uRemoveFlags, targetWatch->ePermissionLevel);
		chatServerLogChannelCommand("ChanUserAccess", watching->name, user, STACK_SPRINTF("Permissions changed for %d: +%d - %d", 
			target->id, uAddFlags, uRemoveFlags));
	}
	sendUserUpdate(target);
	sendChannelUpdate(channel);
	if ((uRemoveFlags & CHANPERM_KICK) == 0) // Kicking = channelLeave, update handled there
		channelMemberInfoUpdate(user, channel);
	return CHATRETURN_FWD_NONE;
}

AUTO_TRANSACTION ATR_LOCKS(channel, ".bannedUsers");
enumTransactionOutcome trChannelBanUser(ATR_ARGS, NOCONST(ChatChannel) *channel, U32 uUserID)
{
	eaiPushUnique(&channel->bannedUsers, uUserID);
	return TRANSACTION_OUTCOME_SUCCESS;
}
AUTO_TRANSACTION ATR_LOCKS(channel, ".bannedUsers");
enumTransactionOutcome trChannelUnbanUser(ATR_ARGS, NOCONST(ChatChannel) *channel, U32 uUserID)
{
	eaiFindAndRemove(&channel->bannedUsers, uUserID);
	return TRANSACTION_OUTCOME_SUCCESS;
}

int channelBanUser(ChatUser *user, ChatUser *target, ChatChannel *channel)
{
	Watching *watching = userFindWatching(user,channel);
	Watching *targetWatch = userFindWatching(target, channel);
	U32 uActions = 0;

	if (!channel)
		return CHATRETURN_CHANNEL_DNE;
	if (!user)
		return CHATRETURN_USER_PERMISSIONS;

	if (!channelUserHasTargetPermission(channel, user, target, watching, CHANPERM_KICK))
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel->name, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_BAN, CHATCOMMAND_CHANNEL_BAN, 
			channel->name, user, target, CHATRETURN_USER_PERMISSIONS);
		return CHATRETURN_USER_PERMISSIONS;
	}
	
	channelLeave(target,channel->name,true);
	if (targetWatch)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel->name, "ChatServer_Uninvite", 
			STRFMT_STRING("User", target->handle), STRFMT_STRING("Channel", channel->name), STRFMT_END);
	}
	AutoTrans_trChannelBanUser(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATCHANNEL, channel->uKey, target->id);
	chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_BAN, CHATCOMMAND_CHANNEL_BAN, 
		channel->name, user, target, CHATRETURN_FWD_NONE);
	sendUserUpdate(target);
	sendChannelUpdate(channel);
	return CHATRETURN_FWD_NONE;
}

int channelUnbanUser(ChatUser *user, ChatUser *target, ChatChannel *channel)
{
	Watching *watching = userFindWatching(user,channel);
	Watching *targetWatch = userFindWatching(target, channel);
	U32 uActions = 0;

	if (!channel)
		return CHATRETURN_CHANNEL_DNE;
	if (!user)
		return CHATRETURN_USER_PERMISSIONS;

	if (!channelUserHasTargetPermission(channel, user, target, watching, CHANPERM_KICK))
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel->name, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_UNBAN, CHATCOMMAND_CHANNEL_UNBAN, 
			channel->name, user, target, CHATRETURN_USER_PERMISSIONS);
		return CHATRETURN_USER_PERMISSIONS;
	}

	AutoTrans_trChannelUnbanUser(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATCHANNEL, channel->uKey, target->id);
	chatServerLogChannelTargetCommandWithReturnCode(CHATCOMMAND_CHANNEL_UNBAN, CHATCOMMAND_CHANNEL_UNBAN, 
		channel->name, user, target, CHATRETURN_FWD_NONE);
	sendUserUpdate(target);
	return CHATRETURN_FWD_NONE;
}


int channelSetChannelAccess(ChatUser *user,const char *channel_name,char *options)
{
	ChatChannel	*channel = channelFindByName(channel_name);
	Container *con = channel ? objGetContainer(GLOBALTYPE_CHATCHANNEL, channel->uKey) : NULL;
	Watching *watching = userFindWatching(user, channel);
	ChannelAccess access = channel ? channel->access : 0;
	int iOptionFlags;

	if (!channel)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelDNE", STRFMT_STRING("Channel", channel_name), STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_MODIFY, channel_name, user, CHATRETURN_CHANNEL_DNE);
		return CHATRETURN_CHANNEL_DNE;
	}
	if (!channelUserHasPermission(channel, user, watching, CHANPERM_MODIFYCHANNEL))
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_MODIFY, channel_name, user, CHATRETURN_USER_PERMISSIONS);
		return CHATRETURN_USER_PERMISSIONS;
	}
	if (!options)
		return CHATRETURN_UNSPECIFIED;
	iOptionFlags = parseChannelAccessTranslateOptions(options);

	//if (!parseAccessString(&access, options))
	if (iOptionFlags == -1)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, 0, "ChatServer_InvalidPermissionString", STRFMT_END);
		chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_MODIFY, channel_name, user, STACK_SPRINTF("Invalid mode %s", options));
		return CHATRETURN_UNSPECIFIED;
	}
	channelAccessApplyFlags(&access, iOptionFlags);

	AutoTrans_trChangeChannelAccess(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATCHANNEL, channel->uKey, access);

	{
		sendCommandToLinkEx(chatServerGetCommandLink(), "ChatServer_SendChannelAccessChange", "%d \"%s\" %d", 
			user->id, channel->name, iOptionFlags);
		chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_MODIFY, channel_name, user, STACK_SPRINTF("Permissions changed %s", options));
	}
	sendChannelUpdate(channel);
	channelGeneralInfoUpdate(channel, CHANNELUPDATE_CHANNEL_PERMISSIONS);
	return CHATRETURN_FWD_NONE;
}

static ChatUser *channelFindOwner (ChatChannel *channel)
{
	if (channel)
	{
		int i,size = eaiSize(&channel->members);
		for (i=0; i<size; i++)
		{
			ChatUser *user = userFindByContainerId(channel->members[i]);
			if (user)
			{
				Watching *watch = userFindWatching(user, channel);
				if (watch && watch->ePermissionLevel == CHANUSER_OWNER)
					return user;
			}
		}
	}
	return NULL;
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
			STRFMT_STRING("Channel", channel_name), STRFMT_END);
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

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(2);
int chatCommand_ForceTransferOwnership(const char *channelName, const char *displayName)
{
	ChatChannel *channel = channelFindByName(channelName);
	ChatUser *user = userFindByHandle(displayName), *owner;

	if (!channel)
	{
		Errorf("channel_dne");
		return 0;
	}
	if (!user)
	{
		Errorf("user_dne");
		return 0;
	}
	if (userFindWatching(user, channel) == NULL)
	{
		Errorf("user_not_in_channel");
		return 0;
	}
	owner = channelFindOwner(channel);
	if (owner)
	{
		AutoTrans_trPassChannelOwnership(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, owner->id, 
			GLOBALTYPE_CHATUSER, user->id, GLOBALTYPE_CHATCHANNEL, channel->uKey);
	}
	else
	{
		AutoTrans_trAddChannelOwnership(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, GLOBALTYPE_CHATCHANNEL, channel->uKey);
	}
	sendUserUpdate(user);
	sendChannelUpdate(channel);
	return 1;
}

AUTO_TRANSACTION ATR_LOCKS(channel, ".messageOfTheDay");
enumTransactionOutcome trChannelMotdCleanup(ATR_ARGS, NOCONST(ChatChannel) *channel)
{
	int numMotd = eaSize(&channel->messageOfTheDay);
	if  (numMotd > 1)
	{
		eaRemoveRange(&channel->messageOfTheDay, 0, numMotd-1);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(channel, ".messageOfTheDay");
enumTransactionOutcome trChannelSetMotd(ATR_ARGS, NOCONST(ChatChannel) *channel, ContainerID userID, char *motd)
{
	int numMotd = eaSize(&channel->messageOfTheDay);
	if  (numMotd > 0)
	{
		int lastidx = numMotd-1;
		if (channel->messageOfTheDay[lastidx]->body)
			free(channel->messageOfTheDay[lastidx]->body);
		channel->messageOfTheDay[lastidx]->sent = timeSecondsSince2000();
		channel->messageOfTheDay[lastidx]->from = userID;
		channel->messageOfTheDay[lastidx]->body = StructAllocString(motd);
	}
	else
	{
		NOCONST(Email)* email = CONTAINER_NOCONST(Email, StructAlloc(parse_Email));
		email->sent = timeSecondsSince2000();
		email->body = StructAllocString(motd);
		email->from = userID;
		eaPush(&channel->messageOfTheDay, email);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

int channelSetMotd(ChatUser *user,const char *channel_name,const char *motd)
{
	ChatChannel		*channel = channelFindByName(channel_name);
	Watching	*watching = userFindWatching(user,channel);
	int i;

	if (!channel || channel->reserved)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_MOTD, channel_name, user, CHATRETURN_CHANNEL_RESERVEDPREFIX);
		return CHATRETURN_CHANNEL_RESERVEDPREFIX;
	}
	if (!channelUserHasPermission(channel, user, watching, CHANPERM_MOTD))
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_MOTD, channel_name, user, CHATRETURN_USER_PERMISSIONS);
		return CHATRETURN_USER_PERMISSIONS;
	}

	if (strlen(motd) > MAX_CHANNEL_MOTD_LENGTH)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelMotDLong", STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_MOTD, channel_name, user, CHATRETURN_CHANNEL_MOTD_LENGTH_LONG);
		return CHATRETURN_CHANNEL_MOTD_LENGTH_LONG;
	}

	if (IsAnyProfane(motd)) {
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ProfanityNotAllowed", STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_MOTD, channel_name, user, CHATRETURN_PROFANITY_NOT_ALLOWED);
		return CHATRETURN_PROFANITY_NOT_ALLOWED;
	}

	AutoTrans_trChannelSetMotd(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATCHANNEL, channel->uKey, user->id, motd);

	sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel->name, "ChatServer_SetChannelMotD", 
		STRFMT_STRING("Channel", channel_name), STRFMT_END);
	for (i=eaiSize(&channel->online)-1; i>=0; i--)
	{
		ChatUser *member = userFindByContainerId(channel->online[i]);
		if (userCharacterOnline(member))
			channelGetMotd(member, channel, true);
	}

	chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_MOTD, channel_name, user, motd);
	{
		ChatChannelInfo *chatinfo = copyChannelInfo(user, channel, watching, true, UserIsAdmin(user));
		if (chatinfo)
		{
			char *command = NULL;
			char *chatString = NULL;
			ParserWriteTextEscaped(&chatString, parse_ChatChannelInfo, chatinfo, 0, 0, 0);
			estrPrintf(&command, "ChatServerForwardChannelInfo %d %s", user->id, chatString);
			sendCommandToLink(chatServerGetCommandLink(), command);
			estrDestroy(&command);
			estrDestroy(&chatString);
			StructDestroy(parse_ChatChannelInfo, chatinfo);
		}
	}
	sendChannelUpdate(channel);
	channelGeneralInfoUpdate(channel, CHANNELUPDATE_MOTD);
	return CHATRETURN_FWD_NONE; // TODO fix these to send channel updates via a slightly different method?
}

void channelGetMotd (ChatUser *user, ChatChannel *channel, bool bForceSend)
{
	int	num_emails;
	Email *email;
	ChatUser *from;
	char datestr[100];

	if (!user || !channel)
		return;

	num_emails = eaSize(&channel->messageOfTheDay);
	if (num_emails == 0)
		return;
	if (num_emails > 1)
	{
		AutoTrans_trChannelMotdCleanup(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATCHANNEL, channel->uKey);
	}

	email = channel->messageOfTheDay[0];
	if (email->sent < user->last_online && !bForceSend)
		return;
	from = userFindByContainerId(email->from);
	if(from)
	{
		timeMakeLocalDateStringFromSecondsSince2000(datestr, email->sent);
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel->name, "ChatServer_ChannelMotD", 
			STRFMT_STRING("Message", email->body), STRFMT_STRING("User", from->handle), STRFMT_STRING("Date", datestr), STRFMT_END);
	}
	CONTAINER_NOCONST(ChatUser, user)->last_online = timeSecondsSince2000();
}

int channelSetDescription(ChatUser *user, const char *channel_name, char *description)
{
	ChatChannel		*channel = channelFindByName(channel_name);
	Watching	*watching = userFindWatching(user,channel);

	if (!channel || channel->reserved)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_DESCRIPTION, channel_name, user, CHATRETURN_CHANNEL_RESERVEDPREFIX);
		return CHATRETURN_CHANNEL_RESERVEDPREFIX;
	}

	if (!channelUserHasPermission(channel, user, watching, CHANPERM_DESCRIPTION))
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_DESCRIPTION, channel_name, user, CHATRETURN_USER_PERMISSIONS);
		return CHATRETURN_USER_PERMISSIONS;
	}

	if (strlen(description) > MAX_CHANNEL_DESCRIPTION_LENGTH)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelDescriptionTooLong", STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_DESCRIPTION, channel_name, user, CHATRETURN_CHANNEL_DESCRIPTION_LENGTH_LONG);
		return CHATRETURN_CHANNEL_DESCRIPTION_LENGTH_LONG;
	}

	if (IsAnyProfane(description)) {
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ProfanityNotAllowed", STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_DESCRIPTION, channel_name, user, CHATRETURN_PROFANITY_NOT_ALLOWED);
		return CHATRETURN_PROFANITY_NOT_ALLOWED;
	}

	objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATCHANNEL, channel->uKey, "channelSetDescription", "set description = \"%s\"", description);
	sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_SetChannelDescription", STRFMT_END);
	chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_DESCRIPTION, channel_name, user, description);
	sendChannelUpdate(channel);
	channelGeneralInfoUpdate(channel, CHANNELUPDATE_DESCRIPTION);
	return CHATRETURN_FWD_NONE;
}

void channelOnline(ChatChannel *channel, ChatUser *user)
{
	if (eaiFind(&channel->online,user->id) >= 0)
		return;
	eaiPushUnique(&CONTAINER_NOCONST(ChatChannel, channel)->online,user->id);
	if (!UserIsAdmin(user))
		channel->uOnlineCount++;
	channelGetMotd(user, channel, false);
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

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(ChatDebug);
void channelCmdDestroy(U32 uID)
{
	ChatChannel *channel = channelFindByID(uID);
	if (channel)
		channelForceKill(channel);
	channel = channelFindByID(uID);
	if (channel) // all users leaving didn't completely destroy it
		channelDestroy(channel);
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
			channelLeave(invitee,channel_name,false);
		}
	}
	for(i=eaiSize(&channel->members)-1; i>=0; i--)
	{
		ChatUser *member = userFindByContainerId(channel->members[i]);
		if (member) 
		{
			channelLeave(member,channel_name,false);
		}
	}
	chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_DESTROY, channel_name, user, "Channel destroyed");
	// channelDestroy is called indirectly via channelLeave->channelDeleteIfEmpty()
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
	if (UserIsAdmin(user))
		return true;
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
	if (UserIsAdmin(user))
		return true;
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

	objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATCHANNEL, channel->uKey, "SetLevelPermissions", "set permissionLevels[%d] = \"%d\"", eLevel, uPermissions);
	chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_MODIFYLEVEL, channel->name, user, STACK_SPRINTF("Set level %d to %d", eLevel, uPermissions));
	sendChannelUpdate(channel);
	channelGeneralInfoUpdate(channel, CHANNELUPDATE_CHANNEL_PERMISSIONS);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND ACMD_CATEGORY(ChatDebug) ACMD_ACCESSLEVEL(9);
void forceDestroyChannel(ContainerID uID)
{
	ChatChannel *channel = channelFindByID(uID);
	if (channel)
		channelForceKill(channel);
}

void channelUpdateSendToShards(ChatUser *user, ChatChannelUpdateData *data)
{
	char *commandString = NULL, *structString = NULL;
	if (!devassert(data))
		return;
	estrStackCreate(&commandString);
	ParserWriteTextEscaped(&structString, parse_ChatChannelUpdateData, data, 0, 0, TOK_NO_NETSEND);
	devassert(structString);
	estrPrintf(&commandString, "ChannelUpdateFromGlobal %s", structString);
	if (data->bSendToAll || data->bSendToOperators)
	{
		sendCommandToAllLocal(commandString, false);
	}
	else if (user)
	{
		sendCommandToUserLocal(user, commandString, NULL);
	}
	estrDestroy(&structString);
	estrDestroy(&commandString);
}

// Updates for general channel info (MotD, Description, Channel Permission Settings)
// are always sent to everyone
void channelGeneralInfoUpdate (ChatChannel *channel, ChannelUpdateEnum eUpdateType)
{
	ChatChannelUpdateData data = {0}; 
	if (!devassert(channel))
		return;
	data.eUpdateType = eUpdateType; // send all data
	data.channelInfo = ChatServerCreateChannelUpdate(NULL, channel, data.eUpdateType);
	data.bSendToAll = true;
	channelUpdateSendToShards(NULL, &data);
	StructDeInit(parse_ChatChannelUpdateData, &data);
}

// Send member permission changes to all shards - user and channel admins/ops get it
void channelMemberInfoUpdate(ChatUser *user, ChatChannel *channel)
{
	ChatChannelUpdateData data = {0}; 
	if (!devassert(user && channel))
		return;

	data.uTargetUserID = user->id;
	data.eUpdateType = CHANNELUPDATE_USER_PERMISSIONS; // send all data, minus users
	data.channelInfo = ChatServerCreateChannelUpdate(user, channel, data.eUpdateType);
	
	if (data.channelInfo) // Only do a send if there is a valid channelInfo
	{
		channelUpdateSendToShards(user, &data);
	}
	if (eaiSize(&channel->members) > 1) // assume that there may be more than 1 user interested in this
	{
		StructReset(parse_ChatChannelUpdateData, &data);
		data.uTargetUserID = user->id;
		data.eUpdateType = CHANNELUPDATE_MEMBER_UPDATE;
		data.channelInfo = ChatServerCreateChannelUpdate(user, channel, data.eUpdateType);
		data.bSendToOperators = true;
		channelUpdateSendToShards(NULL, &data);
	}
	StructDeInit(parse_ChatChannelUpdateData, &data);
}

// Send join information to all shards - user and channel admins/ops get it
void channelUserJoinUpdate(ChatUser *user, ChatChannel *channel, bool bInvite)
{
	ChatChannelUpdateData data = {0}; 
	if (!devassert(user && channel))
		return;

	data.uTargetUserID = user->id;
	data.eUpdateType = CHANNELUPDATE_UPDATE_NO_MEMBERS; // send all data, minus users
	data.channelInfo = ChatServerCreateChannelUpdate(user, channel, data.eUpdateType);
	channelUpdateSendToShards(user, &data);

	if (bInvite || eaiSize(&channel->members) > 1) // assume that there may be more than 1 user interested in this
	{
		StructReset(parse_ChatChannelUpdateData, &data);
		data.uTargetUserID = user->id;
		data.eUpdateType = CHANNELUPDATE_MEMBER_UPDATE;
		data.channelInfo = ChatServerCreateChannelUpdate(user, channel, data.eUpdateType);
		data.bSendToOperators = true;
		channelUpdateSendToShards(NULL, &data);
	}
	StructDeInit(parse_ChatChannelUpdateData, &data);
}

// Send leave information to all shards - user and channel admins/ops get it
// Does not happen for channel destroys, since there will be no admins left
// Channel destroy updates are handled on the shard side
void channelUserRemoveUpdate(ChatUser *user, ChatChannel *channel)
{
	ChatChannelUpdateData updateData = {0};

	updateData.uTargetUserID = user->id;
	updateData.eUpdateType = CHANNELUPDATE_REMOVE;
	updateData.channelInfo = ChatServerCreateChannelUpdate(user, channel, updateData.eUpdateType);
	channelUpdateSendToShards(user, &updateData);

	StructReset(parse_ChatChannelUpdateData, &updateData);
	updateData.uTargetUserID = user->id;
	updateData.eUpdateType = CHANNELUPDATE_MEMBER_REMOVE;
	updateData.bSendToOperators = true;
	updateData.channelInfo = ChatServerCreateChannelUpdate(user, channel, updateData.eUpdateType);
	channelUpdateSendToShards(NULL, &updateData);

	StructDeInit(parse_ChatChannelUpdateData, &updateData);
}

#include "AutoGen/channels_c_ast.c"