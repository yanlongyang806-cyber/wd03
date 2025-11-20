#include "msgsend.h"
#include "chatGlobal.h"
#include "friendsIgnore.h"
#include "users.h"
#include "Autogen/GameServerLib_autogen_RemoteFuncs.h"
#include "Entity.h"
#include "StringUtil.h"
#include "chatLocal.h"
#include "chatLocal_h_ast.h"
#include "chatUtilities.h"
#include "channels.h"
#include "ChatServer/chatShared.h"
#include "GameStringFormat.h"
#include "Message.h"
#include "chatRelay/chatRelayManager.h"

#ifdef USE_CHATRELAY
#include "AutoGen/ChatRelay_autogen_RemoteFuncs.h"
#endif
#include "AutoGen/chatRelayManager_h_ast.h"

extern ParseTable parse_ChatPlayerStruct[];
#define TYPE_parse_ChatPlayerStruct ChatPlayerStruct
extern bool gbLocalShardOnlyMode;

////////////////////////////////////////////////////
// Game Server Remote Command Wrappers
static void sendChannelSystemMessage(ChatUser *user, int eType, const char *channel_name, const char *msg)
{
#ifdef USE_CHATRELAY
	if (user->uChatRelayID)
		RemoteCommand_crReceiveChannelSystemMessage(GLOBALTYPE_CHATRELAY, user->uChatRelayID, user->id, eType, channel_name, msg);
#else
	ChatLocalQueueMsg *data = StructCreate(parse_ChatLocalQueueMsg);
	data->uEntityID = user->pPlayerInfo->onlineCharacterID;
	data->uStartTime = timeSecondsSince2000();
	data->eType = CHATLOCAL_SYSMSG;
	data->data.channelName = StructAllocString(channel_name);
	data->data.message = StructAllocString(msg);
	data->data.iMessageType = eType;
	if (!ChatLocal_AddUserMessage(data))
		StructDestroy(parse_ChatLocalQueueMsg, data);
#endif
}

static void sendSystemAlert(ChatUser *user, const char *title, const char *msg)
{
#ifdef USE_CHATRELAY
	if (user->uChatRelayID)
		RemoteCommand_crReceiveSystemAlert(GLOBALTYPE_CHATRELAY, user->uChatRelayID, user->id, title, msg);
#else
	ChatLocalQueueMsg *data = StructCreate(parse_ChatLocalQueueMsg);
	data->uEntityID = user->pPlayerInfo->onlineCharacterID;
	data->uStartTime = timeSecondsSince2000();
	data->eType = CHATLOCAL_SYSALERT;
	data->data.channelName = StructAllocString(title);
	data->data.message = StructAllocString(msg);
	if (!ChatLocal_AddUserMessage(data))
		StructDestroy(parse_ChatLocalQueueMsg, data);
#endif
}

static void sendMessageReceive(SA_PARAM_NN_VALID ChatUser *user, const ChatMessage *msg)
{
#ifdef USE_CHATRELAY
	if (user->uChatRelayID)
		RemoteCommand_crMessageReceive(GLOBALTYPE_CHATRELAY, user->uChatRelayID, user->id, msg);
#else
	if (userCharacterOnline(user))
	{
		ChatLocalQueueMsg *data = StructCreate(parse_ChatLocalQueueMsg);
		data->uEntityID = user->pPlayerInfo->onlineCharacterID;
		data->eType = CHATLOCAL_PLAYERMSG;
		data->uStartTime = timeSecondsSince2000();
		data->data.chatMessage = StructClone(parse_ChatMessage, msg);
		if (!ChatLocal_AddUserMessage(data))
			StructDestroy(parse_ChatLocalQueueMsg, data);
	}
#endif
}
 
////////////////////////////////////////////////////
bool sendChatSystemTranslatedMsgV(NetLink *link, ChatUser *user, int eType, const char *channel_name, Language eLanguage, const char *messageKey, va_list va)
{
	if (userOnline(user))
	{
		char *estrBuffer = NULL;
		langFormatMessageKeyV(eLanguage, &estrBuffer, messageKey, va);

		if (userCharacterOnline(user))
		{
			sendChannelSystemMessage(user, eType, channel_name, estrBuffer);
		}
		estrDestroy(&estrBuffer);
	}
	else
	{
		// Nothing! Do not send channel messages to offline players
	}
	return 1;
}

bool sendChatSystemTranslatedMsg(ChatUser *user, int eType, const char *channel_name, const char *messageKey, ...)
{
	if (userOnline(user))
	{
		char *estrBuffer = NULL;
		va_list		ap;
		Language eLanguage = LANGUAGE_DEFAULT;

		if (userCharacterOnline(user) && user->pPlayerInfo)
		{
			eLanguage = user->pPlayerInfo->eLanguage;
			va_start(ap, messageKey);
			langFormatMessageKeyV(eLanguage, &estrBuffer, messageKey, ap);
			va_end(ap);
			sendChannelSystemMessage(user, eType, channel_name, estrBuffer);
		}
		estrDestroy(&estrBuffer);
	}
	else
	{
		// Nothing! Do not send channel messages to offline players
	}
	return 1;
}

bool sendChatSystemMsg(ChatUser *user, int eType, const char *channel_name, const char *fmt, ...)
{
	if (userOnline(user))
	{
		char buffer[1024];
		va_list		ap;

		va_start(ap, fmt);
		vsprintf(buffer,fmt,ap);
		va_end(ap);

		if (userCharacterOnline(user))
		{
			sendChannelSystemMessage(user, eType, channel_name, buffer);
		}
	}
	else
	{
		// Nothing! Do not send channel messages to offline players
	}
	return 1;
}

bool sendChatSystemAlert(ChatUser *user, const char *title, const char *msgKey, ...)
{
	if (userCharacterOnline(user))
	{
		char *estrBuffer = NULL;
		va_list		ap;

		va_start(ap, msgKey);
		langFormatMessageKeyV(user->pPlayerInfo->eLanguage, &estrBuffer, msgKey, ap);
		va_end(ap);
		sendSystemAlert(user, title, estrBuffer);
		estrDestroy(&estrBuffer);
	}
	else
	{
		// Nothing! Do not send channel dialogs to offline players
	}
	return 1;
}

bool sendChatSystemStaticMsg(ChatUser *user, int eType, const char *channel_name, const char *msg)
{
	if (userOnline(user))
	{
		if (userCharacterOnline(user))
			sendChannelSystemMessage(user, eType, channel_name, msg);
		return 1;
	}
	else
	{
		// Nothing! Do not send channel messages to offline players
	}
	return 0;
}

void forwardChatMessageToSpys(ChatUser *target, const ChatMessage *pMsg) {
	int i;
	ChatUser *spy;

	// Spying must be done from the LOCAL chat server

	// TODO make custom spying message that adds both player info?
	if (target)
	{
		ChatMessage *pMsgCopy = StructClone(parse_ChatMessage, pMsg);
		if (pMsgCopy) {
			pMsgCopy->eType = kChatLogEntryType_Spy;

			for (i=eaiSize(&target->spying)-1; i>=0; i--)
			{
				spy = userFindByContainerId(target->spying[i]);
				if (spy)
					sendMessageReceive(spy, pMsg);
			}

			StructDestroy(parse_ChatMessage, pMsgCopy);
		}
	}
}

void ChatServerMessageSend(ContainerID toAccountID, const ChatMessage *pMsg)
{
	ChatUser *target = userFindByContainerId(toAccountID);
	if (!target || !pMsg)
		return;
	sendMessageReceive(target, pMsg);
}

void ChatServerMessageSendBatch(U32 uChatRelayID, U32 *eaiAccountIDs, const ChatMessage *pMsg)
{
	if (pMsg && eaiAccountIDs)
	{
		PlayerInfoList list = {0};
		list.piAccountIDs = eaiAccountIDs;
		RemoteCommand_crMessageReceiveBatch(GLOBALTYPE_CHATRELAY, uChatRelayID, &list, pMsg);
	}
}

void ChatServerMessageSendToRelays(EARRAY_OF(ChatRelayUserList) eaRelays, const ChatMessage *pMsg)
{
	EARRAY_FOREACH_BEGIN(eaRelays, i);
	{
		ChatServerMessageSendBatch(eaRelays[i]->uChatRelayID, eaRelays[i]->eaiUsers, pMsg);
	}
	EARRAY_FOREACH_END;
}

__forceinline static void ChatServer_TranslateFriendMessage(char **estr, ChatUser *user, ChatPlayerStruct *friendStruct, FriendResponseEnum eResponse)
{
	char *pchMessageKey = NULL;
	if (eResponse == FRIEND_UPDATED ||
		eResponse == FRIEND_COMMENT || 
		eResponse == FRIEND_NONE)
		return;
	estrPrintf(&pchMessageKey, "FriendResponse.%s", FriendResponseEnumEnum[eResponse+1].key);
	langFormatMessageKey(userGetLastLanguage(user), estr, pchMessageKey, STRFMT_STRING("User", friendStruct->chatHandle), STRFMT_END);
	estrDestroy(&pchMessageKey);
}
void ChatServer_SendFriendUpdate(ChatUser *to, ChatPlayerStruct *friendStruct, FriendResponseEnum eResponse, const char *errorString)
{
	if (userOnline(to))
	{
		if (friendStruct)
		{
			ChatUser *user = userFindByContainerId(friendStruct->accountID);
			// AUTOAFK with no specified message
			if (user && eResponse != FRIEND_COMMENT && (user->online_status & USERSTATUS_AUTOAFK) != 0 && (!friendStruct->status || !*friendStruct->status))
			{
				friendStruct->online_status |= USERSTATUS_AUTOAFK;
			}
		}
#ifdef USE_CHATRELAY
		if (to->uChatRelayID)
		{
			bool bIsError = errorString && *errorString;
			char *pMessage = NULL;
			if (!bIsError)
				ChatServer_TranslateFriendMessage(&pMessage, to, friendStruct, eResponse);
			RemoteCommand_crReceiveFriendCB(GLOBALTYPE_CHATRELAY, to->uChatRelayID, to->id, friendStruct, eResponse, 
				bIsError ? errorString : pMessage, bIsError);
			estrDestroy(&pMessage);
		}
		if (errorString == NULL)
		{
			switch (eResponse)
			{
				case FRIEND_REQUEST_SENT:
				case FRIEND_REQUEST_ACCEPTED:
				case FRIEND_ADDED:
				case FRIEND_REQUEST_RECEIVED:
				case FRIEND_REQUEST_ACCEPT_RECEIVED:
				case FRIEND_UPDATED:
					{
						ChatUserMinimalNameIndex *minimal = NULL;
						if (!to->eaCachedFriendInfo)
							eaIndexedEnable(&to->eaCachedFriendInfo, parse_ChatUserMinimalNameIndex);
						else
							minimal = eaIndexedGetUsingString(&to->eaCachedFriendInfo, friendStruct->chatHandle);
						if (!minimal)
						{
							minimal = StructCreate(parse_ChatUserMinimalNameIndex);
							minimal->handle = StructAllocString(friendStruct->chatHandle);
							eaIndexedAdd(&to->eaCachedFriendInfo, minimal);
						}
						minimal->id = friendStruct->accountID;
						break;
					}
				case FRIEND_REQUEST_REJECTED:
				case FRIEND_REMOVED:
				case FRIEND_REQUEST_REJECT_RECEIVED:
				case FRIEND_REMOVE_RECEIVED:
					{
						ChatUserMinimalNameIndex *minimal = eaIndexedRemoveUsingString(&to->eaCachedFriendInfo, friendStruct->chatHandle);
						if (minimal)
							StructDestroy(parse_ChatUserMinimalNameIndex, minimal);
						break;
					}
				default:
					// does nothing
					break;
			}
		}
#endif
		if (userCharacterOnline(to) && to->pPlayerInfo->onlineCharacterID)
		{
			RemoteCommand_gslChat_ForwardFriendsCallbackCmd(GLOBALTYPE_ENTITYPLAYER, to->pPlayerInfo->onlineCharacterID, 
				to->pPlayerInfo->onlineCharacterID, friendStruct, eResponse, errorString);
		}
	}
}

__forceinline  static void ChatServer_TranslateIgnoreMessage(char **estr, ChatUser *user, ChatPlayerStruct *ignoreStruct, IgnoreResponseEnum eResponse)
{
	char *pchMessageKey = NULL;
	if (eResponse == IGNORE_NONE || eResponse == IGNORE_UPDATED)
		return;
	estrPrintf(&pchMessageKey, "IgnoreResponse.%s", IgnoreResponseEnumEnum[eResponse+1].key);
	langFormatMessageKey(userGetLastLanguage(user), estr, pchMessageKey, STRFMT_STRING("User", ignoreStruct->chatHandle), STRFMT_END);
	estrDestroy(&pchMessageKey);
}
void ChatServer_SendIgnoreUpdate(ChatUser *to, ChatPlayerStruct *ignoreStruct, IgnoreResponseEnum eResponse, const char *errorString)
{
#ifdef USE_CHATRELAY
	bool bIsError = errorString && *errorString;
	char *pMessage = NULL;
	if (userOnline(to) && to->uChatRelayID)
	{
		if (!bIsError)
			ChatServer_TranslateIgnoreMessage(&pMessage, to, ignoreStruct, eResponse);
		RemoteCommand_crReceiveIgnoreCB(GLOBALTYPE_CHATRELAY, to->uChatRelayID, to->id, ignoreStruct, eResponse, 
			bIsError ? errorString : pMessage, bIsError);
		estrDestroy(&pMessage);
	}
#endif
	if (userCharacterOnline(to) && to->pPlayerInfo->onlineCharacterID)
	{
		RemoteCommand_gslChat_ForwardIgnoreCallbackCmd(GLOBALTYPE_ENTITYPLAYER, to->pPlayerInfo->onlineCharacterID, 
			to->pPlayerInfo->onlineCharacterID, ignoreStruct, eResponse, errorString);
	}
}

void ChatServer_SendWhiteListUpdate(ChatUser *to)
{
	if (userOnline(to))
	{
		bool bChatEnable = to->chatWhitelistEnabled;
		bool bTellsEnable = to->tellWhitelistEnabled;
		bool bEmailEnable = to->emailWhitelistEnabled;
#ifdef USE_CHATRELAY
		if (to->uChatRelayID)
			RemoteCommand_crForwardWhiteListInfo(GLOBALTYPE_CHATRELAY, to->uChatRelayID, to->id, bChatEnable, bTellsEnable, bEmailEnable);
#else
		if (to->pPlayerInfo && to->pPlayerInfo->onlineCharacterID)
			RemoteCommand_ServerChat_ForwardWhiteListInfo(GLOBALTYPE_ENTITYPLAYER, to->pPlayerInfo->onlineCharacterID,
				to->pPlayerInfo->onlineCharacterID, bChatEnable, bTellsEnable, bEmailEnable);
#endif
	}
}

bool ChatServerIsValidMessage(const ChatUser *from, const char *msg) 
{
	// If the message is null or empty, it's invalid
	if (!msg || !*msg) {
		return false;
	}

	// If the user is known, then check their access level.
	// Otherwise assume they can send any message.
	if (from && !UserIsAdmin(from)) 
	{
		// The player is mundane, so prevent them from spamming large messages
		if (UTF8GetLength(msg) > MAX_CHAT_LENGTH) {
			return false;
		}
	}

	return true;
}

void ChatServerForwardFriendsList(ChatUser *user, ChatPlayerList *friendList)
{
	if (userOnline(user))
	{
		// Cache friends' handle on ChatServer
		if (!user->eaCachedFriendInfo)
			eaIndexedEnable(&user->eaCachedFriendInfo, parse_ChatUserMinimalNameIndex);
		else
			eaClearStruct(&user->eaCachedFriendInfo, parse_ChatUserMinimalNameIndex);
		EARRAY_CONST_FOREACH_BEGIN(friendList->chatAccounts, i, s);
		{
			ChatUserMinimalNameIndex *minimal = StructCreate(parse_ChatUserMinimalNameIndex);
			minimal->id = friendList->chatAccounts[i]->accountID;
			minimal->handle = StructAllocString(friendList->chatAccounts[i]->chatHandle);
			eaIndexedAdd(&user->eaCachedFriendInfo, minimal);
		}
		EARRAY_FOREACH_END;
		if (user->uChatRelayID)
			RemoteCommand_crReceiveFriends(GLOBALTYPE_CHATRELAY, user->uChatRelayID, user->id, friendList);
		if (userCharacterOnline(user) && user->pPlayerInfo->onlineCharacterID)
			RemoteCommand_gslChat_RefreshFriendListReturnCmd(GLOBALTYPE_ENTITYPLAYER, user->pPlayerInfo->onlineCharacterID, 
				user->pPlayerInfo->onlineCharacterID, friendList);
	}
}

void ChatServerForwardIgnoreList(ChatUser *user, ChatPlayerList *ignoreList)
{
	if (userOnline(user))
	{
#ifdef USE_CHATRELAY
		if (user->uChatRelayID)
			RemoteCommand_crReceiveIgnores(GLOBALTYPE_CHATRELAY, user->uChatRelayID, user->id, ignoreList);
#endif
		if (userCharacterOnline(user) && user->pPlayerInfo->onlineCharacterID)
			RemoteCommand_gslChat_RefreshClientIgnoreListReturnCmd(GLOBALTYPE_ENTITYPLAYER, user->pPlayerInfo->onlineCharacterID, 
				user->pPlayerInfo->onlineCharacterID, ignoreList);
	}
}

void ChatServerForwardMail(ChatUser *user, ChatMailList *mailList)
{
	if (userCharacterOnline(user) && user->pPlayerInfo->onlineCharacterID)
	{
		// change npcids to -1 if they are not from this character, this prevents them from permanently being delete from other characters 
		if(mailList && user->pPlayerInfo)
		{
			S32 i, size;
			size = eaSize(&mailList->mail);
			for(i = 0; i < size; ++i)		
			{
				if(mailList->mail[i]->eTypeOfEmail == EMAIL_TYPE_NPC_FROM_PLAYER && mailList->mail[i]->toContainerID != user->pPlayerInfo->onlineCharacterID)
				{
					mailList->mail[i]->iNPCEMailID = -1;
				}
			}
			RemoteCommand_ServerChat_RefreshClientMailListReturn(GLOBALTYPE_ENTITYPLAYER, user->pPlayerInfo->onlineCharacterID, 
				user->pPlayerInfo->onlineCharacterID, mailList);
		}
	}
}

void ChatServerForwardUnreadMailBit(ChatUser *user, bool hasUnreadMail)
{
	if (userCharacterOnline(user) && user->pPlayerInfo->onlineCharacterID)
	{
		// change npcids to -1 if they are not from this character, this prevents them from permanently being delete from other characters 
		if(user->pPlayerInfo)
		{
			RemoteCommand_ServerChat_RefreshClientUnreadMailBitReturn(GLOBALTYPE_ENTITYPLAYER, user->pPlayerInfo->onlineCharacterID, 
				user->pPlayerInfo->onlineCharacterID, hasUnreadMail);
		}
	}
}


void ChatServerForwardMailConfirmation(ChatUser *user, U32 auctionLot, const char *errorString)
{
	if (userCharacterOnline(user) && user->pPlayerInfo->onlineCharacterID)
	{
		RemoteCommand_ServerChat_SendMailConfirm(GLOBALTYPE_ENTITYPLAYER, user->pPlayerInfo->onlineCharacterID, 
			user->pPlayerInfo->onlineCharacterID, errorString == NULL || !(*errorString), auctionLot, errorString);
	}
}

// Wrapper for remote command
void ChannelSendUpdateToUser (ChatUser *user, ChatChannelInfo *info, ChannelUpdateEnum eUpdateType)
{
#ifdef USE_CHATRELAY
	if (user && user->uChatRelayID)
		RemoteCommand_crChannelUpdate(GLOBALTYPE_CHATRELAY, user->uChatRelayID, user->id, info, eUpdateType);
#else
	if (user && user->pPlayerInfo && user->pPlayerInfo->onlineCharacterID)
		RemoteCommand_ServerChat_ChannelUpdate(GLOBALTYPE_ENTITYPLAYER, user->pPlayerInfo->onlineCharacterID, 
			user->pPlayerInfo->onlineCharacterID, info, eUpdateType);
#endif
}

AUTO_COMMAND_REMOTE;
void ChannelUpdateFromGlobal (ChatChannelUpdateData *update)
{
	if (!devassert(update->channelInfo))
		return;

	if (update->bSendToAll || update->bSendToOperators)
	{
		ChatChannelInfo *info = update->channelInfo;
		ChatChannel *channel = channelFindByName(info->pName);
		if (channel)
		{
			int i, size, uMemberCount, uOnlineCount;
			size = eaiSize(&channel->members);
			uMemberCount = info->uMemberCount;
			uOnlineCount = info->uOnlineMemberCount;
			for (i=0; i<size; i++)
			{
				ChatUser *user = userFindByContainerId(channel->members[i]);
				if (user && user->pPlayerInfo && user->pPlayerInfo->onlineCharacterID)
				{
					if (user->id != update->uTargetUserID)
					{
						Watching *watch = IsChannelFlagShardOnly(channel->reserved) ? 
							channelFindWatchingReserved(user, channel->name) : channelFindWatching(user, channel->name);
						if (watch && UserIsAdmin(user))
						{   // Update the numbers to include GMs
							info->uMemberCount = eaiSize(&channel->members);
							info->uOnlineMemberCount = eaiSize(&channel->online);
							ChannelSendUpdateToUser(user, info, update->eUpdateType);
						}
						else if (watch && (update->bSendToAll || 
								update->bSendToOperators && watch->ePermissionLevel >= CHANUSER_OPERATOR))
						{   // Make sure the numbers are set to the non-GM number
							info->uMemberCount = uMemberCount;
							info->uOnlineMemberCount = uOnlineCount;
							ChannelSendUpdateToUser(user, info, update->eUpdateType);
						}
					}
				}
			}

			if (update->bSendToAll)
			{   // Sending to invitees
				info->uMemberCount = uMemberCount;
				info->uOnlineMemberCount = uOnlineCount;
				size = eaiSize(&channel->invites);for (i=0; i<size; i++)
				{
					ChatUser *user = userFindByContainerId(channel->invites[i]);
					if (user && user->pPlayerInfo && user->pPlayerInfo->onlineCharacterID && user->id != update->uTargetUserID)
					{
						ChannelSendUpdateToUser(user, info, update->eUpdateType);
					}
				}
			}
		}
	}
	else if (update->uTargetUserID && update->channelInfo)
	{
		ChatUser *target = userFindByContainerId(update->uTargetUserID);
		if (target && target->pPlayerInfo && target->pPlayerInfo->onlineCharacterID)
		{
			ChannelSendUpdateToUser(target, update->channelInfo, update->eUpdateType);
		}
	}
}

static const char *translateChannelPermissionLevel (ChannelUserLevel ePermissionLevel)
{
	switch (ePermissionLevel)
	{
	case CHANUSER_USER:
		return TranslateMessageKeyDefault("ChatConfig_Channel_Status_User_User", "Unknown");
	xcase CHANUSER_OPERATOR:
		return TranslateMessageKeyDefault("ChatConfig_Channel_Status_User_Operator", "Unknown");
	xcase CHANUSER_ADMIN:
		return TranslateMessageKeyDefault("ChatConfig_Channel_Status_User_Admin", "Unknown");
	xcase CHANUSER_OWNER:		
		return TranslateMessageKeyDefault("ChatConfig_Channel_Status_User_Owner", "Unknown");
	xcase CHANUSER_GM:
		return TranslateMessageKeyDefault("ChatConfig_Channel_Status_User_GM", "Unknown"); // TODO add this message
	xdefault:
		return TranslateMessageKeyDefault("ChatConfig_Channel_Status_User_Unknown", "Unknown");
	}
}

static void userAccessGetString (char **estr, int iAddFlags, int iRemoveFlags, ChannelUserLevel ePermissionLevel, ChatUser *user)
{
	Language eLanguage = userGetLastLanguage(user);
	bool bWritten = false;

	if (iAddFlags & CHANPERM_KICK)
	{
		langFormatMessageKey(eLanguage, estr, "ChatServer_UserChannelKick", STRFMT_END);
		bWritten = true;
	}
	if (iAddFlags & CHANPERM_MUTE)
	{
		if (bWritten) estrConcatf(estr, ", ");
		langFormatMessageKey(eLanguage, estr, "ChatServer_UserChannelUnmute", STRFMT_END);
		bWritten = true;
	}
	if (iAddFlags & CHANPERM_PROMOTE)
	{
		if (bWritten) estrConcatf(estr, ", ");
		langFormatMessageKey(eLanguage, estr, "ChatServer_UserChannelPromote", 
			STRFMT_STRING("Level", translateChannelPermissionLevel(ePermissionLevel)), STRFMT_END);
		bWritten = true;
	}

	if (iRemoveFlags & CHANPERM_MUTE)
	{
		if (bWritten) estrConcatf(estr, ", ");
		langFormatMessageKey(eLanguage, estr, "ChatServer_UserChannelMute", STRFMT_END);
		bWritten = true;
	}
	if (iRemoveFlags & CHANPERM_DEMOTE)
	{
		if (bWritten) estrConcatf(estr, ", ");
		langFormatMessageKey(eLanguage, estr, "ChatServer_UserChannelDemote", 
			STRFMT_STRING("Level", translateChannelPermissionLevel(ePermissionLevel)), STRFMT_END);
		bWritten = true;
	}
	if (iRemoveFlags & CHANPERM_KICK)
	{
		if (bWritten) estrConcatf(estr, ", ");
		langFormatMessageKey(eLanguage, estr, "ChatServer_UserChannelKick", STRFMT_END);
		bWritten = true;
	}
}

// Message to the person who changed the permission
AUTO_COMMAND_REMOTE;
void ChatServer_ReceiveChannelAccessChangeMessage(ChatAccessChangeMessage *pMessage)
{
	char *accessDescription = NULL;
	ChatUser *user = userFindByContainerId(pMessage->uAccountID);
	ChatChannel *channel = channelFindByID(pMessage->uChannelID);

	if (!user || !channel)
		return;
	userAccessGetString(&accessDescription, pMessage->uAddFlags, pMessage->uRemoveFlags, pMessage->ePermissionLevel, user);
	sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel->name, "ChatServer_SetUserChannelPermission", 
		STRFMT_STRING("User", pMessage->targetHandle), STRFMT_STRING("Channel", channel->name), 
		STRFMT_STRING("PermissionString", accessDescription ? accessDescription : ""), STRFMT_END);
	estrDestroy(&accessDescription);
}

// Message to the person whose permission was changed
AUTO_COMMAND_REMOTE;
void ChatServer_ReceiveChannelAccessChange(ChatAccessChangeMessage *pMessage)
{
	ChatUser *user = userFindByHandle(pMessage->targetHandle);
	if (user)
	{
		char *accessDescription = NULL;
		ChatChannel *channel = channelFindByID(pMessage->uChannelID);
		userAccessGetString(&accessDescription, pMessage->uAddFlags, pMessage->uRemoveFlags, pMessage->ePermissionLevel, user);
		sendTranslatedMessageToUser(user, kChatLogEntryType_System, channel->name, "ChatServer_SetUserChannelPermission", 
			STRFMT_STRING("User", pMessage->targetHandle), STRFMT_STRING("Channel", channel->name), 
			STRFMT_STRING("PermissionString", accessDescription ? accessDescription : ""), STRFMT_END);
		estrDestroy(&accessDescription);
	}
}
