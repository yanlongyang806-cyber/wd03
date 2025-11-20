#include "friendsIgnore.h"
#include "chatCommonStructs.h"
#include "chatCommonStructs_h_ast.h"
#include "chatdb.h"
#include "chatdb_h_ast.h"
#include "users.h"
#include "chatCommandStrings.h"
#include "chatGlobal.h"
#include "ChatServer/chatShared.h"

#include "msgsend.h"
#include "ChatServer.h"
#include "objContainer.h"
#include "chatGlobal.h"
#include "chatUtilities.h"
#include "chatCommon.h"

#include "objTransactions.h"
#include "AutoGen/ChatServer_autotransactions_autogen_wrappers.h"
#include "Autogen/GameServerLib_autogen_RemoteFuncs.h"

#include "ticketnet.h"
#include "file.h"

#include "NotifyCommon.h"

extern ParseTable parse_TicketData[];
#define TYPE_parse_TicketData TicketData

#define CHAT_HTTP_USER_FOUND "user_found"
#define CHAT_HTTP_USER_NOT_FOUND "user_not_found"
#define CHAT_HTTP_FRIEND_NOT_FOUND "friend_not_found"

#define CHAT_HTTP_REQUEST_SENT "friend_request_sent"
#define CHAT_HTTP_FRIENDED "friend_added"
#define CHAT_HTTP_REQUEST_REJECTED "friend_request_rejected"
#define CHAT_HTTP_UNFRIENDED "friend_removed"

#define CHAT_HTTP_USER_IGNORED "user_ignored"
#define CHAT_HTTP_USER_IGNORE_REMOVED "user_ignore_removed"
#define CHAT_HTTP_ERR_FRIENDS "user_already_friends"
#define CHAT_HTTP_ERR_IGNORED "user_already_ignored"
#define CHAT_HTTP_ERR_IGNORED_USER_NOT_FOUND "ignored_user_not_found"
#define CHAT_HTTP_ERR_IGNORED_USER_SELF "user_cannot_ignore_self"
#define CHAT_HTTP_ERR_NOTIGNORED "user_not_ignored"
#define CHAT_HTTP_ERR_REQUESTED "user_already_requested"
#define CHAT_HTTP_ERR_NOTFRIENDS "user_not_friends"

#define NAME_BEFORE(MSG) "%s%s%s "MSG"."
#define NAME_AFTER(MSG) MSG" %s%s%s."
#define LOG_ABOUT(PERSON, MSG) NAME_AFTER(MSG), "", "@", PERSON->handle
#define ABOUT_LOG(PERSON, MSG) NAME_BEFORE(MSG), "", "@", PERSON->handle

extern bool gbNoShardMode;
extern bool gbLocalShardOnlyMode;
extern ParseTable parse_ChatPlayerStruct[];
#define TYPE_parse_ChatPlayerStruct ChatPlayerStruct

static IgnoreChatCommand *ignoreAccountInfo(ChatUser *user);
static FriendChatCommand *friendAccountInfo(ChatUser *user);


__forceinline int friendInputTest (ChatUser *user, ChatUser *target, char *commandName, FriendResponseEnum eType)
{
	if (!user)
	{
		chatServerLogUserCommandWithReturnCode(LOG_CHATFRIENDS, commandName, NULL, user, target, CHATRETURN_UNSPECIFIED);
		return FRIENDS_RETURN_ERROR;
	}
	if (!target)
	{
		char *errorString = NULL;

		/*if(gbNoShardMode)
			trUserFriendDNE
		else
			trUserFriendDNE(NULL, NULL, user, ___);*/
		translateMessageForUser(&errorString, user, "ChatServer_UnknownUser", STRFMT_END);
		ChatServer_SendFriendUpdate(user, NULL, eType, errorString);
		estrDestroy(&errorString);

		chatServerLogUserCommandWithReturnCode(LOG_CHATFRIENDS, commandName, NULL, user, NULL, CHATRETURN_USER_DNE);
		return FRIENDS_RETURN_USER_DNE;
	}
	if (user->id == target->id)
	{
		char *errorString = NULL;
		translateMessageForUser(&errorString, user, "ChatServer_InvalidSelfAction", STRFMT_END);
		ChatServer_SendFriendUpdate(user, NULL, eType, errorString);
		estrDestroy(&errorString);

		chatServerLogUserCommand(LOG_CHATFRIENDS, commandName, NULL, user, target, "Cannot friend self");
		return FRIENDS_RETURN_USER_IS_SELF;
	}
	return -1;
}

__forceinline int ignoreInputTest (ChatUser *user, ChatUser *target, char *commandName, IgnoreResponseEnum eType)
{
	if (!user)
	{
		chatServerLogUserCommandWithReturnCode(LOG_CHATFRIENDS, commandName, NULL, user, target, CHATRETURN_UNSPECIFIED);
		return FRIENDS_RETURN_ERROR;
	}
	if (!target)
	{
		char *errorString = NULL;
		translateMessageForUser(&errorString, user, "ChatServer_UnknownUser", STRFMT_END);
		ChatServer_SendIgnoreUpdate(user, NULL, eType, errorString);
		estrDestroy(&errorString);

		chatServerLogUserCommandWithReturnCode(LOG_CHATFRIENDS, commandName, NULL, user, NULL, CHATRETURN_USER_DNE);
		return FRIENDS_RETURN_USER_DNE;
	}
	if (user->id == target->id)
	{
		char *errorString = NULL;
		translateMessageForUser(&errorString, user, "ChatServer_InvalidSelfAction", STRFMT_END);
		ChatServer_SendIgnoreUpdate(user, NULL, eType, errorString);
		estrDestroy(&errorString);

		chatServerLogUserCommand(LOG_CHATFRIENDS, commandName, NULL, user, target, "Cannot ignore self");
		return FRIENDS_RETURN_USER_IS_SELF;
	}
	return -1;
}

static int userFindFriend (ChatUser *user, FriendResponseEnum eList, U32 uID)
{
	switch(eList)
	{
	case FRIEND_NONE:
		{
			return eaiFind(&user->friends, uID);
		}
	xcase FRIEND_REQUEST_SENT:
		{
			return eaIndexedFindUsingInt(&user->friendReqs_out, uID);
		}
	xcase FRIEND_REQUEST_RECEIVED:
		{
			return eaIndexedFindUsingInt(&user->friendReqs_in, uID);
		}
	}
	return -1;
}

static IgnoreChatCommand *ignoreAccountInfo(ChatUser *user)
{
	IgnoreChatCommand *account = StructCreate(parse_IgnoreChatCommand);

	if (user)
	{
		account->uID = user->id;
		account->displayName = strdup(user->handle);
	}
	return account;
}

static FriendChatCommand *friendAccountInfo(ChatUser *user)
{
	int i, numGames;
	FriendChatCommand *account = StructCreate(parse_FriendChatCommand);

	if (user)
	{
		//TODO: Is this the correct language? This assumes the caller is the 'user'.
		//      If not, we'll need to pass enough context to get the caller's language.
		Language eLanguage = user->pPlayerInfo ? user->pPlayerInfo->eLanguage : LANGUAGE_DEFAULT;

		account->uID = user->id;
		account->displayName = strdup(user->handle);
		account->last_online_time = user->last_online;
		account->online_status = user->online_status;
		if (user->status)
			account->status_message = strdup(user->status);

		numGames = eaSize(&user->ppPlayerInfo);
		for (i=0; i<numGames; i++)
		{
			OnlineGamesChatCommand *gameInfo = StructCreate(parse_OnlineGamesChatCommand);
			gameInfo->characterName = strdup(user->ppPlayerInfo[i]->onlinePlayerName);
			gameInfo->characterContainerID = user->ppPlayerInfo[i]->onlineCharacterID;
			gameInfo->productName = strdup(user->ppPlayerInfo[i]->gamePublicNameKey);
			gameInfo->mapName = strdup(langTranslateMessageKeyDefault(eLanguage, user->ppPlayerInfo[i]->playerMap.pchMapNameMsgKey, user->ppPlayerInfo[i]->playerMap.pchMapName));
			//TODO: We probably want the map instance number and translated neighborhood name as well

			eaPush(&account->eaOnlineGames, gameInfo);
		}
	}

	return account;
}

void userSendUpdateNotifications(ChatUser *updatedUser) 
{
	int i;
	ChatPlayerStruct *pUpdatedPlayer;
	
	PERFINFO_AUTO_START_FUNC();
	pUpdatedPlayer = createChatPlayerStruct(updatedUser, NULL, FRIEND_FLAG_NONE, 0, false);
	// Set the shard hash for this
	pUpdatedPlayer->pPlayerInfo.uShardHash = Game_GetShardHash();
	// Send updates regarding all friends of this user
	pUpdatedPlayer->flags = FRIEND_FLAG_NONE;
	for (i=eaiSize(&updatedUser->friends)-1; i>=0; i--)
	{
		ChatUser *pTargetFriend = userFindByContainerId(updatedUser->friends[i]);
		if(userOnline(pTargetFriend))
		{
			ChatFriendComment *pComment = eaIndexedGetUsingInt(&pTargetFriend->friend_comments, updatedUser->id);
			if (pComment)
				estrCopy2(&pUpdatedPlayer->comment, pComment->comment);
			else
				estrClear(&pUpdatedPlayer->comment);
			ChatServer_SendFriendUpdate(pTargetFriend, pUpdatedPlayer, FRIEND_UPDATED, NULL);
		}
	}
	StructDestroy(parse_ChatPlayerStruct, pUpdatedPlayer);
	PERFINFO_AUTO_STOP_FUNC();
}

// Forwards ban request to Global Chat Server - nothing is handled on shard
void banSpammer(ChatUser* user) 
{
	if (user)
		sendCmdAndParamsToGlobalChat("ChatServerBanSpammer", "%d", user->id);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void userGetFriendShards(CmdContext *context, GlobalType eServerType, U32 uServerID, U32 uAccountID)
{
	sendCommandToGlobalChatServer(context->commandString);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void userForwardFriendShards(GlobalType eServerType, U32 uServerID, U32 uAccountID, ChatPlayerList *friendList)
{
	switch (eServerType)
	{
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATRELAY);
void ChatServerAddIgnoreByStruct(CmdContext *context, ChatIgnoreData *ignoreData)
{
	sendCommandToGlobalChatServer(context->commandString);
}