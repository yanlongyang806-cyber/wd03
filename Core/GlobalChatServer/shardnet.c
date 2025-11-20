#include "aslChatServer.h"
#include "chatdb.h"
#include "ChatServer/chatShared.h"
#include "AutoGen\chatdb_h_ast.h"
#include "ChatServer.h"
#include "chatCommandStrings.h"
#include "users.h"
#include "userStatus.h"
#include "userPermissions.h"
#include "channels.h"
#include "friendsIgnore.h"
#include "chatUtilities.h"
#include "msgsend.h"
#include "NotifyCommon.h"

#include "shardnet.h"
#include "chatCommonStructs_h_ast.h"
#include "chatGlobal.h"
#include "chatGlobalCommands.h"
#include "objContainer.h"
#include "cmdparse.h"
#include "xmpp/XMPP_Chat.h"

extern CmdList gRemoteCmdList;
extern bool gbGlobalChatResponse;

void updateCrossShardStats(ChatUser * from, ChatUser * to)
{
}

AUTO_COMMAND_REMOTE;
bool ChatServerChangeDisplayName (CmdContext *context, ContainerID accountID, char *handleName)
{
	// TODO convert this to match int error codes
	return userChangeChatHandle(accountID, handleName);
}

AUTO_COMMAND_REMOTE;
int ChatServerPlayerUpdateEx(CmdContext *context, ContainerID accountID, PlayerInfoStruct *pPlayerInfo, ChatUserUpdateEnum eForwardToGlobalFriends)
{
	ChatUser *user = userFindByContainerId(accountID);

	if (userOnline(user) && pPlayerInfo)
	{
		NetLink *localChatLink = chatServerGetCommandLink();
		U32 uID = localChatLink ? GetLocalChatLinkID(localChatLink) : 0;

		userLoginPlayerUpdate(user, pPlayerInfo, uID);
		// TODO(Theo) Fix this to not double-send to existing shard
		if (eForwardToGlobalFriends == CHATUSER_UPDATE_GLOBAL)
			userSendUpdateNotifications(user, uID);
	}

	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerPlayerUpdate(CmdContext *context, ContainerID accountID, PlayerInfoStruct *pPlayerInfo)
{
	return ChatServerPlayerUpdateEx(context, accountID, pPlayerInfo, CHATUSER_UPDATE_GLOBAL);
}

AUTO_COMMAND_REMOTE;
int ChatServerLogin(CmdContext *context, ContainerID accountID, ContainerID characterID, const char *accountName, const char *displayName, U32 access_level)
{
	NetLink *localChatLink = chatServerGetCommandLink();
	U32 uID = localChatLink ? GetLocalChatLinkID(localChatLink) : 0;
	if (accountID == 0)
		return CHATRETURN_USER_DNE;

	return userAddAndLogin(accountID, characterID, accountName, displayName, access_level, uID, NULL);
}

AUTO_COMMAND_REMOTE;
int ChatServerAddOrUpdateUser(CmdContext *context, ChatLoginData *pLoginData)
{
	userAddOrUpdate(pLoginData);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerLoginOnly(CmdContext *context, ChatLoginData *pLoginData)
{
	ChatUser *user = userFindByContainerId(pLoginData->uAccountID);
	if (user)
	{
		U32 uChatLinkID = GetLocalChatLinkID(chatServerGetCommandLink());
		userLoginOnly(user, pLoginData, uChatLinkID, false);
	}
	return CHATRETURN_NONE;
}

// Deprecated for ChatRelay
AUTO_COMMAND_REMOTE;
int ChatServerGroupLogin(CmdContext *context, const char *zoneChannelName, PlayerInfoList *accounts)
{
	int i, size, iAccountNameSize;
	NetLink *localChatLink = chatServerGetCommandLink();
	U32 uID = localChatLink ? GetLocalChatLinkID(localChatLink) : 0;

	if (!accounts || !accounts->piAccountIDs || !accounts->ppPlayerInfos)
		return CHATRETURN_UNSPECIFIED;
	size = min (eaiSize(&accounts->piAccountIDs), eaSize(&accounts->ppPlayerInfos));
	iAccountNameSize = eaSize(&accounts->ppPlayerNames);

	for (i=0; i<size; i++)
	{
		ChatUser *user = userFindByContainerId(accounts->piAccountIDs[i]);
		PlayerInfoStruct *player = accounts->ppPlayerInfos[i];
		if (player && !(player->onlinePlayerName && player->onlinePlayerName[0]))
			player = NULL;
		if (user)
		{
			userLogin(user, player->onlineCharacterID, uID, false);
			user->online_status = USERSTATUS_ONLINE; // TODO figure out how to get existing status?
			if (player)
				userLoginPlayerUpdate(user, accounts->ppPlayerInfos[i], uID);
			if (accounts->ppPlayerNames[i]->pStatus)
				userChangeStatus(user, 0, 0, accounts->ppPlayerNames[i]->pStatus, true);
			userSendUpdateNotifications(user, uID);
		}
		else if (i < iAccountNameSize)
		{
			userAddAndLogin(accounts->piAccountIDs[i], accounts->ppPlayerInfos[i]->onlineCharacterID, 
				accounts->ppPlayerNames[i]->pAccountName, accounts->ppPlayerNames[i]->pAccountName, 
				accounts->ppPlayerNames[i]->uAccessLevel, uID, player);
		}
	}
	sendAccountList(localChatLink, uID, accounts->piAccountIDs);
	return CHATRETURN_NONE;
}

AUTO_COMMAND_REMOTE;
void ChatServerLogout(ContainerID accountID)
{
	ChatUser *user = userFindByContainerId(accountID);
	if (user)
	{
		NetLink *localChatLink = chatServerGetCommandLink();
		U32 uID = localChatLink ? GetLocalChatLinkID(localChatLink) : 0;
		userLogout(user, uID);
	}
}

// ------------------------------------------------------
// Channel Information

AUTO_COMMAND_REMOTE;
int ChatServerChannelListMembers(CmdContext *context, ContainerID accountID, char *channel_name)
{
	ChatUser *user = userFindByContainerId(accountID);
	if (user)
		channelListMembers(user, channel_name);
	return CHATRETURN_FWD_NONE;
}

// ------------------------------------------------------
// Channel Joining / Leaving

AUTO_COMMAND_REMOTE;
int ChatServerJoinChannel(CmdContext *context, ContainerID id, char *channel_name, int flags, bool bCreate)
{
	ChatUser *user = userFindByContainerId(id);
	if (!user)
		return CHATRETURN_USER_DNE;

	if (flags && gbChatVerbose)
	{
		printf("\nLocal: User '%s' Joining special channel type '%d'\n", user->handle, flags);
	}

	if (bCreate)
	{
		return channelJoinOrCreate(user, channel_name, flags, UserIsAdmin(user));
	} 
	else 
	{
		return channelJoin(user, channel_name, flags);
	}
}

AUTO_COMMAND_REMOTE;
int ChatServerCreateChannel(CmdContext *context, ContainerID id, char *channel_name, int flags, bool adminAccess)
{
	ChatUser *user = userFindByContainerId(id);
	if (!user)
	{
		return CHATRETURN_USER_DNE;
	}
	return channelCreate(user, channel_name, flags, adminAccess);
}

AUTO_COMMAND_REMOTE;
int ChatServerDestroyChannel(CmdContext *context, ContainerID id, char *channel_name)
{
	ChatUser *user = userFindByContainerId(id);
	if (!user)
	{
		return CHATRETURN_USER_DNE;
	}
	return channelKill(user, channel_name);
}

AUTO_COMMAND_REMOTE;
int ChatServerLeaveChannel(CmdContext *context, ContainerID id, ACMD_SENTENCE channel_name)
{
	ChatChannel *channel = channelFindByName(channel_name);
	ChatUser *user = userFindByContainerId(id);
	if (!user)
	{
		return CHATRETURN_USER_DNE;
	}
	return channelLeave(user, channel_name, 0);
}

// Only used for special channels
AUTO_COMMAND_REMOTE;
int ChatServerJoinOrCreateChannel(CmdContext *context, ContainerID id, char *channel_name, int flags, bool adminAccess)
{
	ChatUser *user = userFindByContainerId(id);
	NOCONST(ChatChannel) *channel = CONTAINER_NOCONST(ChatChannel, channelFindByName(channel_name));
	if (!user)
	{
		return CHATRETURN_USER_DNE;
	}
	if (!channel)
	{
		return channelCreate(user, channel_name, flags, adminAccess);
	}
	return channelJoin(user, channel_name, flags);
}

// ------------------------------------------------------

AUTO_COMMAND_REMOTE;
int ChatServerReservedChannelLog(CmdContext *context, ChatMessage *pMsg)
{
	ChatUser *pFromUser = userFindByContainerId(pMsg->pFrom->accountID);
	if (!pFromUser)
		return CHATRETURN_FWD_NONE;
	if (chatRateLimiter(pFromUser))
	{
		//append "SILENCED_" to message in log
		char* message = NULL;
		estrCreate(&message);
		estrAppend2(&message, "SILENCED_");
		estrAppend2(&message, pMsg->pchText);
		chatServerLogUserCommandWithReturnCode(LOG_CHATSERVER, CHATCOMMAND_CHANNEL_SEND, CHATCOMMAND_CHANNEL_RECEIVE, pFromUser, NULL, CHATRETURN_USER_PERMISSIONS);
		estrDestroy(&message);
		return CHATRETURN_FWD_NONE;
	}
	XMPPChat_RecvSpecialMessage(pMsg);
	chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_SEND, 
		STACK_SPRINTF("%s:%s", GetLocalChatLinkShardName(chatServerGetCommandLink()), pMsg->pchChannel), 
		pFromUser, pMsg->pchText);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
void ChatServerSendLocalChat(ContainerID targetID, ChatMessage *pMsg)
{
	ChatUser *pTargetUser = userFindByContainerId(targetID);
	ChatUser *pFromUser;

	if (!pMsg || !pMsg->pFrom) {
		return;
	}

	pFromUser = userFindByContainerId(pMsg->pFrom->accountID);

	if (!ChatServerIsValidMessage(pFromUser, pMsg->pchText)) {
		return;
	}

	if (!userCharacterOnline(pFromUser) || !userCharacterOnline(pTargetUser))
		return;

	//Make sure from isn't spamming
	if(pFromUser == pTargetUser && chatRateLimiter(pFromUser)) {
		//append "SILENCED_" to message in log
		char* message = NULL;
		estrCreate(&message);
		estrAppend2(&message, "SILENCED_");
		estrAppend2(&message, pMsg->pchText);
		chatServerLogUserCommandWithReturnCode(LOG_CHATLOCAL, CHATCOMMAND_CHANNEL_SEND, CHATCOMMAND_CHANNEL_RECEIVE, pFromUser, pTargetUser, CHATRETURN_USER_PERMISSIONS);
		estrDestroy(&message);
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
		sendTranslatedMessageToUser(pFromUser, 0, NULL, "ChatServer_PermissionDenied", STRFMT_END);
		chatServerLogChannelCommandWithReturnCode(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, CHATRETURN_USER_PERMISSIONS);
		return;
	}

	// Fill in missing FROM user info as best as possible. 
	// DO NOT FILL in pMsg->pTo (even if we know at this point) because it's supposed to be NULL
	ChatFillUserInfo(&pMsg->pFrom, pFromUser);

	// TODO use batch for this to reduce spam
	forwardChatMessageToSpys(pFromUser, pMsg);

	if (!userIsIgnoring(pTargetUser, pFromUser) && isWhitelisted(pFromUser, pTargetUser, LOCAL_CHANNEL_NAME, chatServerGetCommandLinkID()))
	{
		ChatServerMessageSend(pTargetUser->id, pMsg);
	}
	chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_SEND, pMsg->pchChannel, pFromUser, pMsg->pchText);
}

// ------------------------------------------------------
// Channel Management (OP commands)

AUTO_COMMAND_REMOTE;
int ChatServerSetMotd(CmdContext *context, ContainerID id, char *channel_name, ACMD_SENTENCE motd)
{
	ChatUser *user = userFindByContainerId(id);
	return channelSetMotd(user, channel_name, motd);
}

AUTO_COMMAND_REMOTE;
int ChatServerSetChannelDescription(CmdContext *context, ContainerID id, char *channel_name, ACMD_SENTENCE description)
{
	ChatUser *user = userFindByContainerId(id);
	return channelSetDescription(user, channel_name, description);
}

AUTO_COMMAND_REMOTE;
int ChatServerChannelUninviteByID(ContainerID userID, ContainerID targetID, ACMD_SENTENCE channel_name)
{
	ChatUser *user = userFindByContainerId(userID);
	ChatUser *target = userFindByContainerId(targetID);
	channelUninvite(user, target, channel_name, false);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerChannelUninviteByHandle(ContainerID userID, char *targetHandle, ACMD_SENTENCE channel_name)
{
	ChatUser *user = userFindByContainerId(userID);
	ChatUser *target = userFindByHandle(targetHandle);

	if (user && !target)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_UserDNE", 
			STRFMT_STRING("User", targetHandle), STRFMT_END);
	}
	else
		channelUninvite(user, target, channel_name, false);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerDeclineChannelInvite(ContainerID userID, ACMD_SENTENCE channel_name)
{
	ChatUser *user = userFindByContainerId(userID);
	channelDeclineInvite(user, channel_name);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerInviteByID(CmdContext *context, ContainerID idInviter, char *playerNameInviter, char *channel_name, ContainerID idInvitee)
{
	ChatUser *inviter = userFindByContainerId(idInviter);
	ChatUser *invitee = userFindByContainerId(idInvitee);

	if (!inviter || !invitee)
		return CHATRETURN_USER_DNE;
	return channelInvite(inviter, channel_name, invitee);
}

AUTO_COMMAND_REMOTE;
int ChatServerInviteByHandle(CmdContext *context, ContainerID idInviter, char *playerNameInviter, char *channel_name, char *handleInvitee)
{
	ChatUser *inviter, *invitee;

	if (!gbGlobalChatResponse)
	{
		if (globalChatAddCommandWaitQueueByLink(chatServerGetCommandLink(), handleInvitee, context->commandString))
			return CHATRETURN_FWD_NONE; // delayed response
	}
	inviter = userFindByContainerId(idInviter);
	invitee = userFindByHandle(handleInvitee);
	if (!inviter)
		return CHATRETURN_USER_DNE;
	if  (!invitee)
	{
		sendTranslatedMessageToUser(inviter, kChatLogEntryType_System, channel_name, "ChatServer_UserDNE", 
			STRFMT_STRING("User", handleInvitee), STRFMT_END);
		return CHATRETURN_USER_DNE;
	}
	return channelInvite(inviter, channel_name, invitee);
}

AUTO_COMMAND_REMOTE;
int ChatServerSetChannelAccess(CmdContext *context, ContainerID userID, char *channel_name, char *pAccessString)
{
	ChatUser *user = userFindByContainerId(userID);
	if (user && pAccessString)
		return channelSetChannelAccess (user, channel_name, pAccessString);
	return CHATRETURN_UNSPECIFIED;
}

AUTO_COMMAND_REMOTE;
int ChatServerSetUserAccessByHandleNew(CmdContext *context, ContainerID userID, char *channel_name, char *targetHandle, 
									   U32 uAddFlags, U32 uRemoveFlags)
{
	ChatUser *user, *target;

	if (!gbGlobalChatResponse)
	{
		if (globalChatAddCommandWaitQueueByLink(chatServerGetCommandLink(), targetHandle, context->commandString))
			return CHATRETURN_FWD_NONE; // delayed response
	}

	user = userFindByContainerId(userID);
	target = userFindByHandle(targetHandle);
	if (user && target)
		return channelSetUserAccess (user, channel_name, target, uAddFlags, uRemoveFlags);
	return CHATRETURN_UNSPECIFIED;
}

AUTO_COMMAND_REMOTE;
int ChatServerSetUserAccessByHandle(CmdContext *context, ContainerID userID, char *channel_name, char *targetHandle, char *pAccessString)
{
	ChatUser *user, *target;

	if (!gbGlobalChatResponse)
	{
		if (globalChatAddCommandWaitQueueByLink(chatServerGetCommandLink(), targetHandle, context->commandString))
			return CHATRETURN_FWD_NONE; // delayed response
	}

	user = userFindByContainerId(userID);
	target = userFindByHandle(targetHandle);

	if (user && target && pAccessString)
	{
		U32 uAddFlags = 0, uRemoveFlags = 0;
		if (!parseUserAccessStringChanges(&uAddFlags, &uRemoveFlags, pAccessString))
		{
			sendTranslatedMessageToUser(user, kChatLogEntryType_System, 0, "ChatServer_InvalidPermissionString", STRFMT_END);
			// TODO
			//chatServerLogChannelCommand("ChanUserAccess", channel_name, user, target, STACK_SPRINTF("Invalid permissions %s", pAccessString));
			return CHATRETURN_UNSPECIFIED;
		}
		return channelSetUserAccess (user, channel_name, target, uAddFlags, uRemoveFlags);
	}
	return CHATRETURN_UNSPECIFIED;
}

AUTO_COMMAND_REMOTE;
int ChatServerSetUserAccessByID(CmdContext *context, ContainerID userID, char *channel_name, ContainerID targetID, U32 uAddFlags, U32 uRemoveFlags)
{
	ChatUser *user = userFindByContainerId(userID);
	ChatUser *target = userFindByContainerId(targetID);

	if (user && target)
		return channelSetUserAccess (user, channel_name, target, uAddFlags, uRemoveFlags);
	return CHATRETURN_UNSPECIFIED;
}

/*AUTO_COMMAND_REMOTE;
void ChatServerDonateOperator(CmdContext *context, ContainerID userID, char *channel_name, ContainerID targetID)
{
ChatUser *user = userFindByContainerId(userID);
ChatUser *target = userFindByContainerId(targetID);

if (user && target)
channelDonateOperatorAccess (user, channel_name, target);
}*/

// ------------------------------------------------------
// Friends and Ignores

AUTO_COMMAND_REMOTE;
char * ChatServerGetChatHandle(ContainerID userID)
{
	ChatUser *user = userFindByContainerId(userID);
	if (user)
		return (char*) user->handle;
	return NULL;
}

AUTO_COMMAND_REMOTE;
int ChatServerGetFriendsList(CmdContext *context, ContainerID userID)
{
	ChatUser *user = userFindByContainerId(userID);
	ChatPlayerList list = {0};
	if (user)
	{
		char *listString = NULL;
		U32 uChatServerID = GetLocalChatLinkID(chatServerGetCommandLink());
		userCreateFriendsList(user, &list, uChatServerID);
		ParserWriteTextEscaped(&listString, parse_ChatPlayerList, &list, 0, 0, 0);
		{
			char *command = NULL;
			estrPrintf(&command, "ChatServerForwardFriendsFromGlobal %d %s", user->id, listString);
			sendCommandToLink(chatServerGetCommandLink(), command);
			estrDestroy(&command);
		}
		StructDeInit(parse_ChatPlayerList, &list);
		estrDestroy(&listString);
	}
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerGetIgnoreList(CmdContext *context, ContainerID userID)
{
	ChatUser *user = userFindByContainerId(userID);
	ChatPlayerList list = {0};
	if (user)
	{
		char *listString = NULL;
		userCreateIgnoreList(user, &list);
		ParserWriteTextEscaped(&listString, parse_ChatPlayerList, &list, 0, 0, 0);
		{
			char *command = NULL;
			estrPrintf(&command, "ChatServerForwardIgnoresFromGlobal %d %s", user->id, listString);
			sendCommandToLink(chatServerGetCommandLink(), command);
			estrDestroy(&command);
		}
		StructDeInit(parse_ChatPlayerList, &list);
		estrDestroy(&listString);
	}
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
ChatPlayerList * ChatServerGetOnlinePlayers(PlayerFindFilterStruct *pFilters)
{
	// TODO move this out of the chat system
	ChatPlayerList *list = StructAlloc(parse_ChatPlayerList);
	U32 uChatServerID = GetLocalChatLinkID(chatServerGetCommandLink());
	list->chatAccounts = NULL;

	getOnlineList(list, pFilters, uChatServerID);
	//devassert(eaSize(&list->chatAccounts) <= MAX_FIND_PLAYER_LIST);
	return list;
}

AUTO_COMMAND_REMOTE;
int ChatServerGetChannelInfo(CmdContext *context, U32 uAccountID, U32 uCharacterID, const char *channel_name)
{
	ChatChannel *channel = channelFindByName(channel_name);
	if (channel)
	{
		ChatUser *user = userFindByContainerId(uAccountID);
		Watching *pWatching = channelFindWatching(user, channel_name);

		if (pWatching)
		{
			char *command = NULL;
			char *chatString = NULL;
			ChatChannelInfo *chatinfo = copyChannelInfo(user, channel, pWatching, true, user && UserIsAdmin(user));
			if (chatinfo)
				ParserWriteTextEscaped(&chatString, parse_ChatChannelInfo, chatinfo, 0, 0, 0);
			else
				estrConcatf(&chatString, "<& __NULL__ &>");

			estrPrintf(&command, "ChatServerForwardChannelInfo %d %s", user->id, chatString);
			sendCommandToLink(chatServerGetCommandLink(), command);
			if (chatinfo)
			{   // New method of updating
				ChatChannelUpdateData data = {0};
				char *structString = NULL;
				data.uTargetUserID = uAccountID;
				data.eUpdateType = CHANNELUPDATE_UPDATE; // send all data, including members
				data.channelInfo = chatinfo;

				ParserWriteTextEscaped(&structString, parse_ChatChannelUpdateData, &data, 0, 0, TOK_NO_NETSEND);
				estrPrintf(&command, "ChannelUpdateFromGlobal %s", structString);
				estrDestroy(&structString);
				sendCommandToLink(chatServerGetCommandLink(), command);
				data.channelInfo = NULL; // ChatChannelInfo destroyed later
				StructDeInit(parse_ChatChannelUpdateData, &data);
			}
			estrDestroy(&command);
			estrDestroy(&chatString);
			StructDestroy(parse_ChatChannelInfo, chatinfo);
		}
	}
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerSetChannelLevel (CmdContext *context, ContainerID accountID, char *channel_name, ChannelUserLevel eLevel, U32 uPermissions)
{
	ChatUser *user = userFindByContainerId(accountID);
	ChatChannel *channel = channelFindByName(channel_name);

	if (!channel)
	{
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel_name, "ChatServer_ChannelDNE", 
			STRFMT_STRING("Channel", channel_name), STRFMT_END);
		return CHATRETURN_CHANNEL_DNE;
	}
	return channelSetLevelPermissions (user, channel, eLevel, uPermissions);
}

// TODO(Theo) FIX ALL THE WHITELIST FUNCTIONS
AUTO_TRANSACTION ATR_LOCKS(user, ".chatWhitelistEnabled");
enumTransactionOutcome trEnableUserWhitelist(ATR_ARGS, NOCONST(ChatUser) *user, int iEnable)
{
	user->chatWhitelistEnabled = iEnable;
	return TRANSACTION_OUTCOME_SUCCESS;
}
AUTO_TRANSACTION ATR_LOCKS(user, ".tellWhitelistEnabled");
enumTransactionOutcome trEnableUserWhitelistTells(ATR_ARGS, NOCONST(ChatUser) *user, int iEnable)
{
	user->tellWhitelistEnabled = iEnable;
	return TRANSACTION_OUTCOME_SUCCESS;
}
AUTO_TRANSACTION ATR_LOCKS(user, ".emailWhitelistEnabled");
enumTransactionOutcome trEnableUserWhitelistEmails(ATR_ARGS, NOCONST(ChatUser) *user, int iEnable)
{
	user->emailWhitelistEnabled = iEnable;
	return TRANSACTION_OUTCOME_SUCCESS;
}

//Enables the user's whitelist for all chat
AUTO_COMMAND_REMOTE;
int ChatServerToggleWhitelist(CmdContext *context, ContainerID userID, bool enabled)
{
	const char* pchKey = enabled ? "ChatServer_WhitelistChatEnabled":"ChatServer_WhitelistChatDisabled";
	ChatUser *user;
	NOCONST(ChatUser)* userMod;
	user = userFindByContainerId(userID);
	userMod = CONTAINER_NOCONST(ChatUser, user);

	if (!gbGlobalChatResponse)
	{
		if (globalChatAddCommandWaitQueueByLink(chatServerGetCommandLink(), user->handle, context->commandString))
			return CHATRETURN_FWD_NONE; // delayed response
	}

	userMod->chatWhitelistEnabled = enabled;
	objContainerMarkModified(objGetContainer(GLOBALTYPE_CHATUSER, user->id));
	sendUserUpdate(user);
	ChatServerForwardWhiteListInfo(user);
	sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, pchKey, STRFMT_END);
	// TODO fix this to use transactions
	return CHATRETURN_FWD_NONE;
}

//Enables the user's whitelist for tells only
AUTO_COMMAND_REMOTE;
int ChatServerToggleWhitelistTells(CmdContext *context, ContainerID userID, bool enabled)
{
	const char* pchKey = enabled ? "ChatServer_WhitelistTellEnabled":"ChatServer_WhitelistTellDisabled";
	ChatUser *user;
	NOCONST(ChatUser)* userMod;
	user = userFindByContainerId(userID);
	userMod = CONTAINER_NOCONST(ChatUser, user);

	if (!gbGlobalChatResponse)
	{
		if (globalChatAddCommandWaitQueueByLink(chatServerGetCommandLink(), user->handle, context->commandString))
			return CHATRETURN_FWD_NONE; // delayed response
	}

	userMod->tellWhitelistEnabled = enabled;
	objContainerMarkModified(objGetContainer(GLOBALTYPE_CHATUSER, user->id));
	ChatServerForwardWhiteListInfo(user);
	sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, pchKey, STRFMT_END);
	return CHATRETURN_FWD_NONE;
}

//Enables the user's whitelist for emails
AUTO_COMMAND_REMOTE;
int ChatServerToggleWhitelistEmails(CmdContext *context, ContainerID userID, bool enabled)
{
	const char* pchKey = enabled ? "ChatServer_WhitelistEmailEnabled":"ChatServer_WhitelistEmailDisabled";
	ChatUser *user;
	NOCONST(ChatUser)* userMod;
	user = userFindByContainerId(userID);
	userMod = CONTAINER_NOCONST(ChatUser, user);

	if (!gbGlobalChatResponse)
	{
		if (globalChatAddCommandWaitQueueByLink(chatServerGetCommandLink(), user->handle, context->commandString))
			return CHATRETURN_FWD_NONE; // delayed response
	}

	userMod->emailWhitelistEnabled = enabled;
	objContainerMarkModified(objGetContainer(GLOBALTYPE_CHATUSER, user->id));
	sendUserUpdate(user);
	ChatServerForwardWhiteListInfo(user);
	sendTranslatedMessageToUser(user, kChatLogEntryType_System, NULL, pchKey, STRFMT_END);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerBanSpammer(ContainerID userID)
{
	ChatUser *user = userFindByContainerId(userID);
	if (user)
		banSpammer(user, "Banned from Shard");
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void ChatPlayerInfo_UpdateActivityString(CmdContext *context, ContainerID uID, const char *pActivityString)
{
	ChatUser *user = userFindByContainerId(uID);
	if (user)
	{
		PlayerInfoStruct *pStruct = findPlayerInfoByLocalChatServerID(user, GetLocalChatLinkID(chatServerGetCommandLink()));
		if (pStruct)
		{
			SAFE_FREE(pStruct->playerActivity);
			if (pActivityString && *pActivityString)
				pStruct->playerActivity = strdup(pActivityString);
			userSendActivityStatus(user, chatServerGetCommandLink());
		}
	}
}
