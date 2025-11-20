#include "msgsend.h"
#include "chatGlobal.h"
#include "friendsIgnore.h"
#include "users.h"
#include "Entity.h"
#include "StringUtil.h"
#include "ChatServer/chatShared.h"
#include "logging.h"

#include "xmpp/XMPP_Chat.h"

extern ParseTable parse_ChatPlayerStruct[];
#define TYPE_parse_ChatPlayerStruct ChatPlayerStruct
 
static void sendMessageReceive(U32 uEntityID, const ChatMessage *msg)
{
	if (!uEntityID)
	{	//If the entID is 0, then we have a non-character client (i.e. XMPP)
		XMPPChat_RecvPrivateMessage(msg);
	}
}

////////////////////////////////////////////////////
bool sendChatSystemTranslatedMsgV(NetLink *link, ChatUser *user, int eType, const char *channel_name, Language eLanguage, const char *messageKey, va_list va)
{
	if (userOnline(user))
	{
		char *estrBuffer = NULL;
		langFormatMessageKeyV(eLanguage, &estrBuffer, messageKey, va);

		if (link && linkConnected(link))
			sendMessageToLink(link, user, eType, channel_name, estrBuffer);
		else
			sendMessageToUserStatic(user, eType, channel_name, estrBuffer);
		estrDestroy(&estrBuffer);
	}
	else
	{
		// Nothing! Do not send channel messages to offline players
	}
	return 1;
}

bool sendChatSystemStaticMsg(ChatUser *user, int eType, const char *channel_name, const char *msg)
{
	if (userOnline(user))
	{
			NetLink *link = chatServerGetCommandLink();
			if (link)
				sendMessageToLink(link, user, eType, channel_name, msg);
			else
				sendMessageToUserStatic(user, eType, channel_name, msg);
		return 1;
	}
	else
	{
		// Nothing! Do not send channel messages to offline players
	}
	return 0;
}

void forwardChatMessageToSpys(ChatUser *target, const ChatMessage *pMsg) {
	// Spying must be done from the LOCAL chat server
	// TODO make custom spying message that adds both player info?
	/*int i;
	ChatUser *spy;

	if (target)
	{
		ChatMessage *pMsgCopy = StructClone(parse_ChatMessage, pMsg);
		if (pMsgCopy) {
			pMsgCopy->eType = kChatLogEntryType_Spy;

			for (i=eaiSize(&target->spying)-1; i>=0; i--)
			{
				spy = userFindByContainerId(target->spying[i]);

				if (userCharacterOnline(spy))
				{
					sendMessageReceive(spy->pPlayerInfo->onlineCharacterID, pMsg);
				}
			}

			StructDestroy(parse_ChatMessage, pMsgCopy);
		}
	}*/
}

void ChatServerMessageSend(ContainerID toAccountID, const ChatMessage *pMsg)
{
	// Only sends the messages to XMPP from Global (or a similar global client)
	ChatUser *target = userFindByContainerId(toAccountID);
	PlayerInfoStruct *pXMPPInfo = NULL;

	if (!target || !pMsg)
		return;
	if (target->ppPlayerInfo)
	{
		pXMPPInfo = userFindPlayerInfoByLinkID(target, XMPP_CHAT_ID);
	}
	if (!pXMPPInfo)
		return;

	if (userOnline(target))
	{
		sendMessageReceive(pXMPPInfo->onlineCharacterID, pMsg);
	}
}

#define WriteChatPlayerString(estr, pStruct) if (pStruct) ParserWriteTextEscaped(estr, parse_ChatPlayerStruct, pStruct, 0, 0, 0); \
	else estrConcatf(estr, "<& __NULL__ &>");

void ChatServer_SendFriendUpdateSingleShard(ChatUser *to, ChatPlayerStruct *friendStruct, FriendResponseEnum eResponse, U32 uChatServerID)
{
	char *command = NULL;
	char *structString = NULL;
	NetLink *link = GetLocalChatLink(uChatServerID);

	if (!linkConnected(link))
		return;

	WriteChatPlayerString(&structString, friendStruct);

	estrPrintf(&command, "ChatServerForwardFriendResponse %d %s %d \"\"", to->id, structString, eResponse);
	sendCommandToLink(link, command);
	estrDestroy(&structString);
	estrDestroy(&command);
}
void ChatServer_SendFriendUpdateInternal(ChatUser *to, ChatPlayerStruct *friendStruct, FriendResponseEnum eResponse, 
								 const char *errorKey, const char *errorHandle, NetLink *originator)
{
	if (userCharacterOnline(to))
	{
		char *command = NULL;
		char *structString = NULL;

		// Warn if we're sending empty player information for an online player.
		if (eResponse == FRIEND_UPDATED && !friendStruct)
		{
			AssertOrAlert("CHATSERVER.EMPTYPLAYERINFO", "Sending player with empty structure");
			log_printf(LOG_CHATSERVER, "[Error: Empty Player Info Send]: \"%s\"",
				friendStruct && friendStruct->pPlayerInfo.onlinePlayerName ? friendStruct->pPlayerInfo.onlinePlayerName : "");
		}

		WriteChatPlayerString(&structString, friendStruct);

		if (errorKey)
		{
			estrPrintf(&command, "ChatServerForwardFriendResponseError %d %d \"%s\" \"%s\"", to->id, eResponse, 
				errorKey, errorHandle ? errorHandle : "");
		}
		else
			estrPrintf(&command, "ChatServerForwardFriendResponse %d %s %d \"%s\"", to->id, structString, eResponse, "");
		sendCommandToUserLocal(to, command, originator);
		estrDestroy(&structString);
		estrDestroy(&command);
	}
}
void ChatServer_SendIgnoreUpdateInternal(ChatUser *to, ChatPlayerStruct *ignoreStruct, IgnoreResponseEnum eResponse, 
								 const char *errorKey, const char *errorHandle, NetLink *originator)
{
	if (userCharacterOnline(to))
	{
		char *command = NULL;
		char *structString = NULL;
		if (ignoreStruct)
			ParserWriteTextEscaped(&structString, parse_ChatPlayerStruct, ignoreStruct, 0, 0, 0);
		else
			estrConcatf(&structString, "<& __NULL__ &>");
		
		if (errorKey)
		{
			estrPrintf(&command, "ChatServerForwardIgnoreResponseError %d %d \"%s\" \"%s\"", to->id, eResponse, 
				errorKey, errorHandle ? errorHandle : "");
		}
		else
			estrPrintf(&command, "ChatServerForwardIgnoreResponse %d %s %d \"%s\"", to->id, structString, eResponse, "");
		sendCommandToUserLocal(to, command, originator);
		estrDestroy(&structString);
		estrDestroy(&command);
	}
}

void ChatServerUserFriendRequest(ChatUser *from, ChatUser *to, U32 uChatServerID)
{
	ChatPlayerStruct *chatPlayer;
	if (!from || !to)
		return;
	// Send to XMPP client, if connected.
	XMPPChat_NotifyFriendRequest(to, from);

	// Friend requests shouldn't show detailed info about the other party
	if (userCharacterOnline(from))
	{
		chatPlayer = userCreateChatPlayerStruct(to, from, FRIEND_FLAG_PENDINGREQUEST, 0, false);
		ChatServer_SendFriendUpdate(from, chatPlayer, FRIEND_REQUEST_SENT);
		StructDestroy(parse_ChatPlayerStruct, chatPlayer);
	}
	if (userCharacterOnline(to))
	{
		chatPlayer = userCreateChatPlayerStruct(from, to, FRIEND_FLAG_RECEIVEDREQUEST, uChatServerID, true);
		ChatServer_SendFriendUpdate(to, chatPlayer, FRIEND_REQUEST_RECEIVED);
		StructDestroy(parse_ChatPlayerStruct, chatPlayer);
	}
}

void ChatServerUserFriendOnlineNotify(ChatUser *user, ChatUser *target, bool bOnline)
{
	ChatPlayerStruct *chatPlayer;
	if (userCharacterOnline(target))
	{
		chatPlayer = userCreateChatPlayerStruct(user, target, 0, 0, false);
		ChatServer_SendFriendUpdate(target, chatPlayer, bOnline ? FRIEND_ONLINE : FRIEND_OFFLINE);
		StructDestroy(parse_ChatPlayerStruct, chatPlayer);
	}
}

void ChatServerUserFriendAccept(ChatUser *user, ChatUser *target, U32 uChatServerID)
{
	ChatPlayerStruct *chatPlayer;
	int i;

	// Send to XMPP client, if connected.
	XMPPChat_NotifyNewFriend(user, target);
	XMPPChat_NotifyNewFriend(target, user);

	// Sends a unique update to every shard so that every shard gets the correct, shard-specific online info
	for (i=eaSize(&user->ppPlayerInfo)-1; i>=0; i--)
	{
		if (user->ppPlayerInfo[i]->uChatServerID >= LOCAL_CHAT_ID_START)
		{
			chatPlayer = userCreateChatPlayerStruct(target, user, FRIEND_FLAG_NONE, user->ppPlayerInfo[i]->uChatServerID, true);
			ChatServer_SendFriendUpdateSingleShard(user, chatPlayer, FRIEND_REQUEST_ACCEPTED, 
				user->ppPlayerInfo[i]->uChatServerID);
			StructDestroy(parse_ChatPlayerStruct, chatPlayer);
		}
	}
	for (i=eaSize(&target->ppPlayerInfo)-1; i>=0; i--)
	{
		if (target->ppPlayerInfo[i]->uChatServerID >= LOCAL_CHAT_ID_START)
		{
			chatPlayer = userCreateChatPlayerStruct(user, target, FRIEND_FLAG_NONE, target->ppPlayerInfo[i]->uChatServerID, true);
			ChatServer_SendFriendUpdateSingleShard(target, chatPlayer, FRIEND_REQUEST_ACCEPT_RECEIVED, 
				target->ppPlayerInfo[i]->uChatServerID);
			StructDestroy(parse_ChatPlayerStruct, chatPlayer);
		}
	}
}

void ChatServerUserFriendReject(ChatUser *user, ChatUser *target)
{
	ChatPlayerStruct *chatPlayer;
	if (userCharacterOnline(user))
	{
		chatPlayer = userCreateChatPlayerStruct(target, user, FRIEND_FLAG_NONE, 0, false);
		ChatServer_SendFriendUpdate(user, chatPlayer, FRIEND_REQUEST_REJECTED);
		StructDestroy(parse_ChatPlayerStruct, chatPlayer);
	}
	if (userCharacterOnline(target))
	{
		chatPlayer = userCreateChatPlayerStruct(user, target, FRIEND_FLAG_NONE, 0, false);
		ChatServer_SendFriendUpdate(target, chatPlayer, FRIEND_REQUEST_REJECT_RECEIVED);
		StructDestroy(parse_ChatPlayerStruct, chatPlayer);
	}
}

void ChatServerUserFriendRemove(ChatUser *user, ChatUser *target)
{
	ChatPlayerStruct *chatPlayer;
	if (userCharacterOnline(user))
	{
		chatPlayer = userCreateChatPlayerStruct(target, user, FRIEND_FLAG_NONE, 0, false);
		ChatServer_SendFriendUpdate(user, chatPlayer, FRIEND_REMOVED);
		StructDestroy(parse_ChatPlayerStruct, chatPlayer);
	}
	if (userCharacterOnline(target))
	{
		chatPlayer = userCreateChatPlayerStruct(user, target, FRIEND_FLAG_NONE, 0, false);
		ChatServer_SendFriendUpdate(target, chatPlayer, FRIEND_REMOVE_RECEIVED);
		StructDestroy(parse_ChatPlayerStruct, chatPlayer);
	}
}

void ChatServerUserIgnoreAdd(ChatUser *user, ChatUser *target)
{
	if (userCharacterOnline(user))
	{
		ChatPlayerStruct *chatPlayer = userCreateChatPlayerStruct(target, user, IGNORE_NONE, 0, false);
		ChatServer_SendIgnoreUpdate(user, chatPlayer, IGNORE_ADDED);
		StructDestroy(parse_ChatPlayerStruct, chatPlayer);
	}
}
void ChatServerUserIgnoreRemove(ChatUser *user, ChatUser *target)
{
	if (userCharacterOnline(user))
	{
		ChatPlayerStruct *chatPlayer = userCreateChatPlayerStruct(target, user, IGNORE_NONE, 0, false);
		ChatServer_SendIgnoreUpdate(user, chatPlayer, IGNORE_REMOVED);
		StructDestroy(parse_ChatPlayerStruct, chatPlayer);
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

void ChatServerForwardWhiteListInfo(ChatUser *user)
{
	if (userCharacterOnline(user))
	{
		char *command = NULL;
		bool bChatEnable = user->chatWhitelistEnabled;
		bool bTellsEnable = user->tellWhitelistEnabled;
		bool bEmailsEnable = user->emailWhitelistEnabled;
		estrPrintf(&command, "ChatServerForwardWhiteListInfo %d %d %d %d", user->id, bChatEnable, bTellsEnable, bEmailsEnable);
		sendCommandToUserLocal(user, command, NULL); 
	}
}