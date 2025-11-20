#include "friendsIgnore.h"

#include "chatCommandStrings.h"
#include "chatCommonStructs.h"
#include "chatdb.h"
#include "ChatServer/chatShared.h"
#include "chatGlobal.h"
#include "chatGlobalConfig.h"
#include "ChatServer.h"
#include "chatShardCluster.h"
#include "msgsend.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "StringUtil.h"
#include "users.h"
#include "userPermissions.h"
#include "xmpp/XMPP_Chat.h"

#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/chatdb_h_ast.h"
#include "AutoGen/GlobalChatServer_autotransactions_autogen_wrappers.h"

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

extern ParseTable parse_ChatPlayerStruct[];
#define TYPE_parse_ChatPlayerStruct ChatPlayerStruct
extern GlobalChatServerConfig gGlobalChatServerConfig;

static IgnoreChatCommand *ignoreAccountInfo(ChatUser *user);
static FriendChatCommand *friendAccountInfo(ChatUser *user);
static FriendChatCommand * friendCopyMinimumInfo(ChatUser *user, U32 uTimeSent);

__forceinline int friendInputTest (ChatUser *user, ChatUser *target, char *commandName, FriendResponseEnum eType)
{
	if (!user)
	{
		chatServerLogUserCommandWithReturnCode(LOG_CHATFRIENDS, commandName, NULL, user, target, CHATRETURN_UNSPECIFIED);
		return FRIENDS_RETURN_ERROR;
	}
	if (!target)
	{
		ChatServer_SendFriendUpdateError(user, eType, "ChatServer_UnknownUser", NULL);
		chatServerLogUserCommandWithReturnCode(LOG_CHATFRIENDS, commandName, NULL, user, NULL, CHATRETURN_USER_DNE);
		return FRIENDS_RETURN_USER_DNE;
	}
	if (user->id == target->id)
	{
		ChatServer_SendFriendUpdateError(user, eType, "ChatServer_InvalidSelfAction", NULL);
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
		ChatServer_SendIgnoreUpdateError(user, eType, "ChatServer_UnknownUser", NULL);
		chatServerLogUserCommandWithReturnCode(LOG_CHATFRIENDS, commandName, NULL, user, NULL, CHATRETURN_USER_DNE);
		return FRIENDS_RETURN_USER_DNE;
	}
	if (user->id == target->id)
	{
		ChatServer_SendIgnoreUpdateError(user, eType, "ChatServer_InvalidSelfAction", NULL);
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

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Friend);
char *chatCommand_userIgnoreSpammer(const char * userSourceName, const char * userIgnoreName, bool bSpammer)
{
	ChatUser *user, *userToIgnore;
	user = userFindByAccountName(userSourceName);
	userToIgnore = userFindByHandle(userIgnoreName);

	if (user && userToIgnore && user != userToIgnore)
	{
		FriendsReturnCode result = userIgnore(user, userToIgnore, bSpammer, NULL);
		switch (result)
		{
		case FRIENDS_RETURN_ERROR_ALREADY_IGNORED:
			Errorf(CHAT_HTTP_ERR_IGNORED); 
			return NULL;
		xcase FRIENDS_RETURN_USER_IGNORED:
			return CHAT_HTTP_USER_IGNORED;
		xcase FRIENDS_RETURN_USER_DNE:
			Errorf(CHAT_HTTP_USER_NOT_FOUND);
			return NULL;
		}
	}
	else if (!user)
		Errorf(CHAT_HTTP_USER_NOT_FOUND);
	else if (!userToIgnore)
		Errorf(CHAT_HTTP_ERR_IGNORED_USER_NOT_FOUND);
	else // Ignoring yourself is stupid
		Errorf(CHAT_HTTP_ERR_IGNORED_USER_SELF);
	return NULL;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Friend);
char *chatCommand_userIgnore(const char * userSourceName, const char * userIgnoreName)
{
	return chatCommand_userIgnoreSpammer(userSourceName, userIgnoreName, false);
}

// Ignore target user.  If spammer is true, then user is reporting target as a spammer
int userIgnore (ChatUser *user, ChatUser *target, bool spammer, const char *pSpamMsg)
{
	int ret = 0;

	ret = ignoreInputTest(user, target, CHATCOMMAND_IGNORE_ADD, IGNORE_ADDED);
	if (ret != -1)
		return ret;

	if (userIsIgnoring(user, target))
	{
		ChatServer_SendIgnoreUpdateError(user, IGNORE_ADDED, "ChatServer_AlreadyIgnored", target->handle);
		chatServerLogUserCommand(LOG_CHATFRIENDS, CHATCOMMAND_IGNORE_ADD, NULL, user, target, "Already ignoring");
		ret = FRIENDS_RETURN_ERROR_ALREADY_IGNORED;
	}
	else if (!userCanIgnore(user, target))
	{
		// Users with higher access levels (or any devs) cannot be ignored
		chatServerLogUserCommand(LOG_CHATFRIENDS, CHATCOMMAND_IGNORE_ADD, NULL, user, target, "Cannot ignore higher access level");
		ret = FRIENDS_RETURN_USER_DNE;
	}
	else
	{
		AutoTrans_trUserAddIgnore(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, target->id);
		ChatServerUserIgnoreAdd(user, target);
		chatServerLogUserCommandWithReturnCode(LOG_CHATFRIENDS, CHATCOMMAND_IGNORE_ADD, CHATCOMMAND_IGNORE_TARGETADD, user, target, CHATRETURN_NONE);
		ret = FRIENDS_RETURN_USER_IGNORED;

		// If user hasn't unignored too many people, then increase the naughty value of the target
		// Always increase naughty if MaxUnignores == 0 (is disabled)
		if (gGlobalChatServerConfig.iMaxUnignores == 0 || user->unignoreCount < gGlobalChatServerConfig.iMaxUnignores)
		{
			bool bAlreadyReported = spammer && eaiFind(&target->eaiSpamIgnorers, user->id) >= 0;
			if (!bAlreadyReported)
			{
				char *ignoreString = NULL;
				int iIncrement = (spammer) ? gGlobalChatServerConfig.iIgnoreSpammerIncrement : 
					gGlobalChatServerConfig.iIgnoreIncrement;
				estrPrintf(&ignoreString, "Ignored by @%s (%d)", user->handle, user->id);
				if (spammer)
					eaiPush(&target->eaiSpamIgnorers, user->id);
				if (spammer && !nullStr(pSpamMsg))
					userIncrementNaughtyValueWithSpam(target, iIncrement, ignoreString, pSpamMsg);
				else
					userIncrementNaughtyValue(target, iIncrement, ignoreString);
				estrDestroy(&ignoreString);
			}
		}
		sendUserUpdate(user);
	}
	return ret;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".ignore");
enumTransactionOutcome trUserRemoveIgnore(ATR_ARGS, NOCONST(ChatUser) *user, ContainerID targetID)
{
	eaiFindAndRemove(&user->ignore, targetID);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".ignore");
enumTransactionOutcome trUserAddIgnore(ATR_ARGS, NOCONST(ChatUser) *user, ContainerID targetID)
{
	eaiPush(&user->ignore, targetID);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Friend);
char *chatCommand_userRemoveIgnore(const char * userSourceName, const char * userRemoveIgnoreName)
{
	ChatUser *user, *userToRemoveIgnore;
	user = userFindByAccountName(userSourceName);
	userToRemoveIgnore = userFindByHandle(userRemoveIgnoreName);

	if (user && userToRemoveIgnore && user != userToRemoveIgnore)
	{
		FriendsReturnCode result = userRemoveIgnore(user, userToRemoveIgnore);
		switch (result)
		{
		case FRIENDS_RETURN_ERROR_NOTIGNORED:
			Errorf(CHAT_HTTP_ERR_NOTIGNORED); 
			return NULL;
		xcase FRIENDS_RETURN_USER_IGNORE_REMOVED:
			return CHAT_HTTP_USER_IGNORE_REMOVED;
		}
	}
	else if (!user)
		Errorf(CHAT_HTTP_USER_NOT_FOUND);
	else if (!userToRemoveIgnore)
		Errorf(CHAT_HTTP_ERR_IGNORED_USER_NOT_FOUND);
	else // Ignoring yourself is stupid
		Errorf(CHAT_HTTP_ERR_IGNORED_USER_NOT_FOUND);
	return NULL;
}

int userRemoveIgnore (ChatUser *user, ChatUser *target)
{
	int ret = 0;

	ret = ignoreInputTest(user, target, CHATCOMMAND_IGNORE_REMOVE, IGNORE_REMOVED);
	if (ret != -1)
		return ret;

	if (eaiFind(&user->ignore, target->id) != -1)
	{
		AutoTrans_trUserRemoveIgnore(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, target->id);
		ChatServerUserIgnoreRemove(user, target);
		chatServerLogUserCommandWithReturnCode(LOG_CHATFRIENDS, CHATCOMMAND_IGNORE_REMOVE, CHATCOMMAND_IGNORE_TARGETREMOVE, user, target, CHATRETURN_NONE);
		//Increment the unignore count of the user.
		user->unignoreCount++;
		ret = FRIENDS_RETURN_USER_IGNORE_REMOVED;
	}
	else
	{
		ChatServer_SendIgnoreUpdateError(user, IGNORE_REMOVED, "ChatServer_NotIgnored", target->handle);
		chatServerLogUserCommand(LOG_CHATFRIENDS, CHATCOMMAND_IGNORE_REMOVE, NULL, user, target, "Not ignored");
		ret = FRIENDS_RETURN_ERROR_NOTIGNORED;
	}
	sendUserUpdate(user);
	return ret;
}

AUTO_TRANS_HELPER;
int trRemoveFriendRequests(ATH_ARG NOCONST(ChatUser) *user, U32 userID, ATH_ARG NOCONST(ChatUser) *target, U32 targetID)
{
	NOCONST(ChatFriendRequestStruct) *request;
	request = eaIndexedRemoveUsingInt(&user->friendReqs_out, targetID);
	if (request)
		StructDestroyNoConst(parse_ChatFriendRequestStruct, request);

	request = eaIndexedRemoveUsingInt(&target->friendReqs_in, userID);
	if (request)
		StructDestroyNoConst(parse_ChatFriendRequestStruct, request);
	return true;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".id, .friends, .friendReqs_in[]") ATR_LOCKS(target, ".id, .friends, .friendReqs_out[]");
enumTransactionOutcome trUserAcceptFriend(ATR_ARGS, NOCONST(ChatUser) *user, U32 userID, NOCONST(ChatUser) *target, U32 targetID)
{
	eaiPush(&user->friends, target->id);
	eaiPush(&target->friends, user->id);
	trRemoveFriendRequests(target, targetID, user, userID); // target initiated
	return TRANSACTION_OUTCOME_SUCCESS;
}

int userAcceptFriend(ChatUser *user, ChatUser *target)
{
	bool bTargetRequested;
	int result = friendInputTest(user, target, CHATCOMMAND_FRIEND_ACCEPT, FRIEND_REQUEST_SENT);
	U32 uChatServerID = GetLocalChatLinkID(chatServerGetCommandLink());
	if (result != -1)
		return result;

	bTargetRequested = userFindFriend(user, FRIEND_REQUEST_RECEIVED, target->id) != -1 && 
		userFindFriend(target, FRIEND_REQUEST_SENT, user->id) != -1 ;

	if (!bTargetRequested)
	{
		// No request to accept!
		ChatServer_SendFriendUpdateError(user, FRIEND_REQUEST_SENT, "ChatServer_NoFriendRequest", target->handle);
		chatServerLogUserCommand(LOG_CHATFRIENDS, CHATCOMMAND_FRIEND_ACCEPT, NULL, user, target, "No Friend request");
		return FRIENDS_RETURN_ERROR_NOTFRIENDS;
	}

	AutoTrans_trUserAcceptFriend(NULL, GLOBALTYPE_GLOBALCHATSERVER, 
		GLOBALTYPE_CHATUSER, user->id, user->id, GLOBALTYPE_CHATUSER, target->id, target->id);

	chatServerLogUserCommandWithReturnCode(LOG_CHATFRIENDS, CHATCOMMAND_FRIEND_ACCEPT, CHATCOMMAND_FRIEND_TARGETACCEPT, user, target, CHATRETURN_NONE);
	ChatServerUserFriendAccept(user, target, uChatServerID);

	sendUserUpdate(user);
	sendUserUpdate(target);
	return FRIENDS_RETURN_REQUEST_ACCEPTED;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".friendReqs_in[]") ATR_LOCKS(target, ".friendReqs_out[]");
enumTransactionOutcome trUserRejectFriend(ATR_ARGS, NOCONST(ChatUser) *user, U32 userID, NOCONST(ChatUser) *target, U32 targetID)
{
	trRemoveFriendRequests(target, targetID, user, userID); // target initiated
	return TRANSACTION_OUTCOME_SUCCESS;
}

int userRejectFriend(ChatUser *user, ChatUser *target)
{
	bool bTargetRequested;
	int result = friendInputTest(user, target, CHATCOMMAND_FRIEND_REJECT, FRIEND_REQUEST_REJECTED);
	if (result != -1)
		return result;

	bTargetRequested = userFindFriend(user, FRIEND_REQUEST_RECEIVED, target->id) != -1 && 
		userFindFriend(target, FRIEND_REQUEST_SENT, user->id) != -1;

	if (!bTargetRequested)
	{
		// No request to reject!
		ChatServer_SendFriendUpdateError(user, FRIEND_REQUEST_REJECTED, "ChatServer_NoFriendRequest", target->handle);
		chatServerLogUserCommand(LOG_CHATFRIENDS, CHATCOMMAND_FRIEND_REJECT, CHATCOMMAND_FRIEND_TARGETREJECT, user, target, "No Friend request");
		return FRIENDS_RETURN_ERROR_NOTFRIENDS;
	}

	AutoTrans_trUserRejectFriend(NULL, GLOBALTYPE_GLOBALCHATSERVER,
		GLOBALTYPE_CHATUSER, user->id, user->id, GLOBALTYPE_CHATUSER, target->id, target->id);

	chatServerLogUserCommandWithReturnCode(LOG_CHATFRIENDS, CHATCOMMAND_FRIEND_REJECT, CHATCOMMAND_FRIEND_TARGETREJECT, user, target, CHATRETURN_NONE);
	ChatServerUserFriendReject(user, target);

	sendUserUpdate(user);
	sendUserUpdate(target);
	return FRIENDS_RETURN_REQUEST_REJECTED;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".id, .friendReqs_out[AO]") ATR_LOCKS(target, ".id, .friendReqs_in[AO]");
enumTransactionOutcome trUserAddFriend(ATR_ARGS, NOCONST(ChatUser) *user, NOCONST(ChatUser) *target)
{
	U32 uTime = timeSecondsSince2000();
	NOCONST(ChatFriendRequestStruct) *request = StructCreateNoConst(parse_ChatFriendRequestStruct);
	request->userID = target->id;
	request->uTimeSent = uTime;
	eaIndexedAdd(&user->friendReqs_out, request);

	request = StructCreateNoConst(parse_ChatFriendRequestStruct); 
	request->userID = user->id;
	request->uTimeSent = uTime;
	request->eDirection = CHATFRIEND_INCOMING;
	eaIndexedAdd(&target->friendReqs_in, request);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Friend);
char *chatCommand_AddFriend(const char * userSourceName, const char * userFriendName, bool bForceFriend)
{
	ChatUser *user, *userFriend;
	user = userFindByAccountName(userSourceName);
	userFriend = userFindByHandle(userFriendName);

	if (user && userFriend && user != userFriend)
	{
		FriendsReturnCode result = userAddFriend(user, userFriend);
		switch (result)
		{
		case FRIENDS_RETURN_ERROR_IGNORED:
			Errorf(CHAT_HTTP_USER_IGNORED); 
			return NULL;
			xcase FRIENDS_RETURN_ERROR_ALREADY_FRIENDS:
			Errorf(CHAT_HTTP_ERR_FRIENDS); 
			return NULL;
			xcase FRIENDS_RETURN_ERROR_ALREADY_REQUESTED:
			Errorf(CHAT_HTTP_ERR_REQUESTED); 
			return NULL;
			xcase FRIENDS_RETURN_REQUEST_SENT:
			if (bForceFriend)
			{
				result = userAddFriend(userFriend, user);
				if (result != FRIENDS_RETURN_FRIEND_ADDED)
					return CHAT_HTTP_REQUEST_SENT;
				else
					return CHAT_HTTP_FRIENDED;
			}
			else
				return CHAT_HTTP_REQUEST_SENT;

			xcase FRIENDS_RETURN_FRIEND_ADDED:
			return CHAT_HTTP_FRIENDED;
		}
	}
	else if (!user)
		Errorf(CHAT_HTTP_USER_NOT_FOUND);
	else if (!userFriend)
		Errorf(CHAT_HTTP_FRIEND_NOT_FOUND);
	else // Friending yourself is stupid
		Errorf(CHAT_HTTP_ERR_FRIENDS);
	return NULL;
}

int userAddFriend(ChatUser *user, ChatUser *target)
{
	int result = friendInputTest(user, target, CHATCOMMAND_FRIEND_ADD, FRIEND_ADDED);
	U32 uChatServerID = GetLocalChatLinkID(chatServerGetCommandLink());
	if (result != -1)
		return result;

	if (userIsIgnoring(target, user))
	{
		ChatServer_SendFriendUpdateError(user, FRIEND_ADDED, "ChatServer_BeingIgnored", target->handle);
		chatServerLogUserCommandWithReturnCode(LOG_CHATFRIENDS, CHATCOMMAND_FRIEND_ADD, CHATCOMMAND_FRIEND_TARGETADD, user, target, CHATRETURN_USER_IGNORING);
		return FRIENDS_RETURN_ERROR_IGNORED; // target ignoring user
	}

	if (userIsFriend(user, target->id))
	{
		ChatServer_SendFriendUpdateError(user, FRIEND_ADDED, "ChatServer_AlreadyFriends", target->handle);
		chatServerLogUserCommand(LOG_CHATFRIENDS, CHATCOMMAND_FRIEND_ADD, NULL, user, target, "Already friends");
		return FRIENDS_RETURN_ERROR_ALREADY_FRIENDS; // already friends
	}

	if (userFindFriend(user, FRIEND_REQUEST_SENT, target->id) >= 0)
	{
		ChatServer_SendFriendUpdateError(user, FRIEND_ADDED, "ChatServer_AlreadyRequestedFriends", target->handle);
		chatServerLogUserCommand(LOG_CHATFRIENDS, CHATCOMMAND_FRIEND_ADD, NULL, user, target, "Request already pending");
		return FRIENDS_RETURN_ERROR_ALREADY_REQUESTED; // already requested
	}

	if (userFindFriend(user, FRIEND_REQUEST_RECEIVED, target->id) >= 0)
	{
		userAcceptFriend(user, target);
		chatServerLogUserCommand(LOG_CHATFRIENDS, CHATCOMMAND_FRIEND_ADD, CHATCOMMAND_FRIEND_TARGETADD, user, target, "Accepted request");
		return FRIENDS_RETURN_FRIEND_ADDED;
	}

	AutoTrans_trUserAddFriend(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, GLOBALTYPE_CHATUSER, target->id);

	ChatServerUserFriendRequest(user, target, uChatServerID);
	chatServerLogUserCommandWithReturnCode(LOG_CHATFRIENDS, CHATCOMMAND_FRIEND_ADD, CHATCOMMAND_FRIEND_TARGETADD, user, target, CHATRETURN_NONE);

	sendUserUpdate(user);
	sendUserUpdate(target);
	return FRIENDS_RETURN_REQUEST_SENT;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".friend_comments[]");
enumTransactionOutcome trUserSetFriendComment(ATR_ARGS, NOCONST(ChatUser) *user, U32 targetID, char *pcComment)
{
	NOCONST(ChatFriendComment) *pCFC;
	if (!pcComment || !*pcComment)
	{
		pCFC = eaIndexedRemoveUsingInt(&user->friend_comments, targetID);
		if (pCFC)
			StructDestroyNoConst(parse_ChatFriendComment, pCFC);
	}
	else
	{
		pCFC = eaIndexedGetUsingInt(&user->friend_comments, targetID);
		if (!pCFC)
		{
			pCFC = StructCreateNoConst(parse_ChatFriendComment);
			pCFC->id = targetID;
			eaIndexedPushUsingIntIfPossible(&user->friend_comments, targetID, pCFC);
		}
		estrCopy2(&pCFC->comment, pcComment);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}


int userSetFriendComment(ChatUser *user, ChatUser *target, char *pcComment)
{
	int result = friendInputTest(user, target, CHATCOMMAND_FRIEND_ADD, FRIEND_ADDED);
	U32 uChatServerID = GetLocalChatLinkID(chatServerGetCommandLink());
	char *pCleanedString = NULL;
	if (result != -1)
		return result;

	if (!userIsFriend(user, target->id))
	{
		ChatServer_SendFriendUpdateError(user, FRIEND_ADDED, "ChatServer_NotFriends", target->handle);
		chatServerLogUserCommand(LOG_CHATFRIENDS, CHATCOMMAND_FRIEND_COMMENT, NULL, user, target, "Not friends");
		return FRIENDS_RETURN_ERROR_NOTFRIENDS; // not friends
	}

	if (pcComment)
	{
		estrStackCreate(&pCleanedString);
		estrCopy2(&pCleanedString, pcComment);
		estrTrimLeadingAndTrailingWhitespace(&pCleanedString);
	}
	AutoTrans_trUserSetFriendComment(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, target->id, pCleanedString);

	sendUserUpdate(user); // This is needed for older shards
	userSendUpdateTargetNotifications(user, target, uChatServerID);
	chatServerLogUserCommandWithReturnCode(LOG_CHATFRIENDS, CHATCOMMAND_FRIEND_COMMENT, CHATCOMMAND_FRIEND_TARGETCOMMENT, user, target, CHATRETURN_NONE);
	estrDestroy(&pCleanedString);
	return FRIENDS_RETURN_REQUEST_SENT;
}

AUTO_TRANS_HELPER;
void trGCS_RemoveFriendComment (ATH_ARG NOCONST(ChatUser) *user, U32 uTargetID)
{
	// Remove comment for target from user
	NOCONST(ChatFriendComment) *pCFC = eaIndexedRemoveUsingInt(&user->friend_comments, uTargetID);
	if (pCFC)
		StructDestroyNoConst(parse_ChatFriendComment, pCFC);
}

AUTO_TRANSACTION ATR_LOCKS(user, ".id, .friends, .friend_comments[]") ATR_LOCKS(target, ".id, .friends, .friend_comments[]");
enumTransactionOutcome trUserRemoveFriend(ATR_ARGS, NOCONST(ChatUser) *user, U32 userID, NOCONST(ChatUser) *target, U32 targetID)
{
	// Remove comment for target from user
	trGCS_RemoveFriendComment(user, targetID);
	// Remove comment for user from target
	trGCS_RemoveFriendComment(target, userID);

	eaiFindAndRemove(&user->friends, target->id);
	eaiFindAndRemove(&target->friends, user->id);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(user, ".friends, .friend_comments[], .friendReqs_out[], .friendReqs_in[]");
enumTransactionOutcome trUserRemoveDeletedFriend(ATR_ARGS, NOCONST(ChatUser) *user, U32 targetID)
{
	NOCONST(ChatFriendRequestStruct) *request;
	trGCS_RemoveFriendComment(user, targetID);
	
	request = eaIndexedRemoveUsingInt(&user->friendReqs_out, targetID);
	if (request)
		StructDestroyNoConst(parse_ChatFriendRequestStruct, request);
	
	request = eaIndexedRemoveUsingInt(&user->friendReqs_in, targetID);
	if (request)
		StructDestroyNoConst(parse_ChatFriendRequestStruct, request);

	eaiFindAndRemove(&user->friends, targetID);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Friend);
char * chatCommand_RemoveFriend(const char *userSourceName, const char *userFriendName){
	ChatUser *user, *userFriend;
	user = userFindByAccountName(userSourceName);
	userFriend = userFindByHandle(userFriendName);

	if (user && userFriend && user != userFriend)
	{
		FriendsReturnCode result = userRemoveFriend(user, userFriend->id);
		switch (result)
		{
		case FRIENDS_RETURN_ERROR:
			Errorf(CHAT_HTTP_USER_NOT_FOUND); // Unknown error, really
			return NULL;
		xcase FRIENDS_RETURN_ERROR_NOTFRIENDS:
			Errorf(CHAT_HTTP_ERR_NOTFRIENDS);
			return NULL;
		xcase FRIENDS_RETURN_REQUEST_REJECTED:
			return CHAT_HTTP_REQUEST_REJECTED;
		xcase FRIENDS_RETURN_FRIEND_REMOVED:
			return CHAT_HTTP_UNFRIENDED;
		}
	}
	else if (!user)
		Errorf(CHAT_HTTP_USER_NOT_FOUND);
	else if (!userFriend)
		Errorf(CHAT_HTTP_FRIEND_NOT_FOUND);
	else // Unfriending yourself is stupid
		Errorf(CHAT_HTTP_ERR_NOTFRIENDS);
	return NULL;
}

int userRemoveFriend(ChatUser *user, U32 targetID)
{
	bool bWasFriend = false;
	ChatUser *target = userFindByContainerId(targetID);
	int result = friendInputTest(user, target, CHATCOMMAND_FRIEND_REMOVE, FRIEND_REMOVED);

	if (result == FRIENDS_RETURN_USER_DNE)
	{
		AutoTrans_trUserRemoveDeletedFriend(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, targetID);
		return FRIENDS_RETURN_USER_DNE;
	}
	if (result != -1)
		return result;

	if ( userIsFriend(user, target->id) )
	{
		AutoTrans_trUserRemoveFriend(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, user->id, GLOBALTYPE_CHATUSER, target->id, target->id);
		ChatServerUserFriendRemove(user, target);

		chatServerLogUserCommandWithReturnCode(LOG_CHATFRIENDS, CHATCOMMAND_FRIEND_REMOVE, CHATCOMMAND_FRIEND_TARGETREMOVE, user, target, CHATRETURN_NONE);
		bWasFriend = true;
	}
	else if (userFindFriend(user, FRIEND_REQUEST_SENT, target->id) >= 0)
	{
		AutoTrans_trUserRejectFriend(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, target->id, target->id, GLOBALTYPE_CHATUSER, user->id, user->id);

		ChatServerUserFriendRemove(user, target);
		chatServerLogUserCommand(LOG_CHATFRIENDS, CHATCOMMAND_FRIEND_REMOVE, CHATCOMMAND_FRIEND_TARGETREMOVE, user, target, "Cancelled request");
		bWasFriend = true;
	}
	else if (userFindFriend(user, FRIEND_REQUEST_RECEIVED, target->id) >= 0)
	{
		AutoTrans_trUserRejectFriend(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, user->id, GLOBALTYPE_CHATUSER, target->id, target->id);

		ChatServerUserFriendReject(user, target);
		chatServerLogUserCommand(LOG_CHATFRIENDS, CHATCOMMAND_FRIEND_REMOVE, CHATCOMMAND_FRIEND_TARGETREMOVE, user, target, "Rejected request");
	}
	else
	{
		ChatServer_SendFriendUpdateError(user, FRIEND_REMOVED, "ChatServer_NotFriends", target->handle);
		chatServerLogUserCommand(LOG_CHATFRIENDS, CHATCOMMAND_FRIEND_REMOVE, NULL, user, target, "Not friends");
		return FRIENDS_RETURN_ERROR_NOTFRIENDS;
	}

	XMPPChat_NotifyFriendRemove(user, target);
	XMPPChat_NotifyFriendRemove(target, user);
	sendUserUpdate(user);
	sendUserUpdate(target);
	if (bWasFriend)
		return FRIENDS_RETURN_FRIEND_REMOVED;
	else
		return FRIENDS_RETURN_REQUEST_REJECTED;
}

int userCreateFriendsList(ChatUser *user, ChatPlayerList *list, U32 uChatServerID)
{
	ChatPlayerStruct ** friends = NULL;
	int i, size;
	int count = 0;
	U32 *eaiToRemove = NULL;

	assert(list);
	size = eaiSize(&user->friends);
	for (i=0; i<size; i++)
	{
		ChatUser * friendUser = userFindByContainerId(user->friends[i]);
		if (!friendUser)
		{
			eaiPush(&eaiToRemove, user->friends[i]);
			continue;
		}
		eaPush(&list->chatAccounts, userCreateChatPlayerStruct(friendUser, user, FRIEND_FLAG_NONE, uChatServerID, true));
		count++;
	}
	size = eaSize(&user->friendReqs_out);
	for (i=0; i<size; i++)
	{
		ChatUser * friendUser = userFindByContainerId(user->friendReqs_out[i]->userID);
		if (!friendUser)
		{
			eaiPush(&eaiToRemove, user->friendReqs_out[i]->userID);
			continue;
		}
		eaPush(&list->chatAccounts, userCreateChatPlayerStruct(friendUser, user, FRIEND_FLAG_PENDINGREQUEST, 0, true));
		count++;
	}
	size = eaSize(&user->friendReqs_in);
	for (i=0; i<size; i++)
	{
		ChatUser * friendUser = userFindByContainerId(user->friendReqs_in[i]->userID);
		if (!friendUser)
		{
			eaiPush(&eaiToRemove, user->friendReqs_in[i]->userID);
			continue;
		}
		eaPush(&list->chatAccounts, userCreateChatPlayerStruct(friendUser, user, FRIEND_FLAG_RECEIVEDREQUEST, 0, true));
		count++;
	}
	
	size=eaiSize(&eaiToRemove);
	for (i=0; i<size; i++)
	{
		AutoTrans_trUserRemoveDeletedFriend(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id, eaiToRemove[i]);
	}
	eaiDestroy(&eaiToRemove);
	return count;
}

int userCreateIgnoreList(ChatUser *user, ChatPlayerList *list)
{
	ChatPlayerStruct ** ignores = NULL;
	int i;
	int count = 0;
	U32 *eaiToRemove = NULL;

	assert(list);
	for (i=eaiSize(&user->ignore)-1; i>=0; i--)
	{
		ChatUser * ignoreUser = userFindByContainerId(user->ignore[i]);
		if (!ignoreUser)
		{
			eaiPush(&eaiToRemove, user->ignore[i]);
			continue;
		}
		eaPush(&list->chatAccounts, userCreateChatPlayerStruct(ignoreUser, NULL, FRIEND_FLAG_IGNORE, 0, false));
		count++;
	}

	for (i=eaiSize(&eaiToRemove)-1; i>=0; i--)
	{
		// TODO remove dead ignores
	}
	eaiDestroy(&eaiToRemove);
	return count;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Ignore);
IgnoreListChatCommand * chatCommand_GetIgnoreList(const char *userSourceName)
{
	ChatUser *user = userFindByAccountName(userSourceName);
	if (user)
	{
		IgnoreListChatCommand *ret = StructCreate(parse_IgnoreListChatCommand);
		int size, i;
		size = eaiSize(&user->ignore);
		for (i=0; i<size; i++)
		{
			ChatUser *myIgnore = userFindByContainerId(user->ignore[i]);
			if (myIgnore)
			{
				IgnoreChatCommand *account = ignoreAccountInfo(myIgnore);
				eaPush(&ret->allIgnores, account);
			}
		}

		return ret;
	}
	else
	{
		Errorf(CHAT_HTTP_USER_NOT_FOUND);
		return NULL;
	}
}

static FriendChatCommand * friendCopyMinimumInfo(ChatUser *user, U32 uTimeSent)
{
	FriendChatCommand *account = StructCreate(parse_FriendChatCommand);
	account->uID = user->id;
	account->accountName = StructAllocString(user->accountName);
	account->displayName = StructAllocString(user->handle);
	account->friend_request_send_date = uTimeSent;
	return account;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Friend);
FriendsListChatCommand * chatCommand_GetFriendsList(const char *userSourceName)
{
	ChatUser *user = userFindByAccountName(userSourceName);
	if (user)
	{
		FriendsListChatCommand *ret = StructCreate(parse_FriendsListChatCommand);
		int size, i;
		// my friends
		size = eaiSize(&user->friends);
		for (i=0; i<size; i++)
		{
			ChatUser *myFriend = userFindByContainerId(user->friends[i]);
			if (myFriend)
			{
				FriendChatCommand *account = friendAccountInfo(myFriend);
				eaPush(&ret->allFriends, account);
			}
		}

		// pending friend requests
		size = eaSize(&user->friendReqs_out);
		for (i=0; i<size; i++)
		{
			ChatFriendRequestStruct *request = user->friendReqs_out[i];
			ChatUser *myFriend = userFindByContainerId(request->userID);
			if (myFriend)
			{
				FriendChatCommand *account = friendCopyMinimumInfo(myFriend, request->uTimeSent);
				eaPush(&ret->pendingFriends, account);
			}
		}

		// incoming friend requests
		size = eaSize(&user->friendReqs_in);
		for (i=0; i<size; i++)
		{
			ChatFriendRequestStruct *request = user->friendReqs_in[i];
			ChatUser *myFriend = userFindByContainerId(request->userID);
			if (myFriend)
			{
				FriendChatCommand *account = friendCopyMinimumInfo(myFriend, request->uTimeSent);
				eaPush(&ret->incomingFriends, account);
			}
		}
		return ret;
	}
	else
	{
		Errorf(CHAT_HTTP_USER_NOT_FOUND);
		return NULL;
	}
}


AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(XMLRPC, Friend);
UserListChatCommand *chatCommand_UsersStatus(char *users)
{
	UserListChatCommand *ret = StructCreate(parse_UserListChatCommand);

	if (users)
	{
		char *context = NULL;
		char *token = strtok_s(users, " ", &context);
		while (token)
		{
			ChatUser *user = userFindByHandle(token);
			if (user)
			{
				FriendChatCommand *account = friendAccountInfo(user);
				eaPush(&ret->allUsers, account);
			}
			token = strtok_s(NULL, " ", &context);

		}
	}
	return ret;
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
		account->accountName = StructAllocString(user->accountName);
		account->displayName = StructAllocString(user->handle);
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
			gameInfo->shardName = user->ppPlayerInfo[i]->shardName;
			//TODO: We probably want the map instance number and translated neighborhood name as well

			eaPush(&account->eaOnlineGames, gameInfo);
		}
	}

	return account;
}

void userSendUpdateNotifications(ChatUser *updatedUser, U32 uChatServerID)
{
	ChatPlayerStruct *pUpdatedPlayer = userCreateChatPlayerStruct(updatedUser, NULL, FRIEND_FLAG_NONE, uChatServerID, false);
	NetLink *originator = GetLocalChatLink(uChatServerID);
	bool bIsOnline = userOnline(updatedUser) && !(updatedUser->online_status & USERSTATUS_HIDDEN);

	// Send updates regarding all friends of this user
	pUpdatedPlayer->flags = FRIEND_FLAG_NONE;
	XMPPChat_RecvPresence(updatedUser, NULL);

	if (bIsOnline)
	{
		EARRAY_INT_CONST_FOREACH_BEGIN(updatedUser->friends, i, n);
		{
			ChatUser *pTargetFriend = userFindByContainerId(updatedUser->friends[i]);
			if(userOnline(pTargetFriend))
			{
				ChatFriendComment *pComment = eaIndexedGetUsingInt(&pTargetFriend->friend_comments, updatedUser->id);
				if (pComment)
					estrCopy2(&pUpdatedPlayer->comment, pComment->comment);
				else
					estrDestroy(&pUpdatedPlayer->comment);
				if (updatedUser->bOnlineChange)
					ChatServerUserFriendOnlineNotify(updatedUser, pTargetFriend, bIsOnline);
				else
					ChatServer_SendFriendUpdateInternal(pTargetFriend, pUpdatedPlayer, FRIEND_UPDATED, NULL, NULL, NULL);
			}
		}
		EARRAY_FOREACH_END;
	}
	updatedUser->bOnlineChange = false;

	// Do not send updates to friend requests
	//pUpdatedPlayer->flags = FRIEND_FLAG_PENDINGREQUEST;
	//pUpdatedPlayer->flags = FRIEND_FLAG_RECEIVEDREQUEST;

	// Send updates to all users ignoring this user
	// Or not. People don't care about the status of people they're ignoring.

	StructDestroy(parse_ChatPlayerStruct, pUpdatedPlayer);
}

// Sends an update for a change to the friend comment
void userSendUpdateTargetNotifications(ChatUser *user, ChatUser *updatedFriend, U32 uChatServerID)
{
	ChatPlayerStruct *pUpdatedPlayer = userCreateChatPlayerStruct(updatedFriend, user, FRIEND_FLAG_NONE, uChatServerID, false);
	NetLink *originator = GetLocalChatLink(uChatServerID);

	// Send updates regarding all friends of this user
	pUpdatedPlayer->flags = FRIEND_FLAG_NONE;
	ChatServer_SendFriendUpdateInternal(user, pUpdatedPlayer, FRIEND_COMMENT, NULL, NULL, NULL);
	// TODO send status update to XMPP here

	StructDestroy(parse_ChatPlayerStruct, pUpdatedPlayer);
}

ChatPlayerStruct *userCreateChatPlayerStruct(ChatUser *target, ChatUser *user, int flags, U32 uChatServerID, bool bInitialStatus)
{
	ChatShard_UpdateUserShards(target);
	return createChatPlayerStruct(target, user, flags, uChatServerID, bInitialStatus);
}

AUTO_COMMAND_REMOTE;
void userGetFriendShards(GlobalType eServerType, U32 uServerID, U32 uAccountID)
{
	NetLink *link = chatServerGetCommandLink();
	ChatUser *user = userFindByContainerId(uAccountID);
	ChatPlayerList friendList = {0};
	char *listString = NULL;

	if (!user)
	{
		sendCommandToLinkEx(link, "userForwardFriendShards", "%d %d %d \"\"", eServerType, uServerID, uAccountID);
		return;
	}

	EARRAY_INT_CONST_FOREACH_BEGIN(user->friends, i, n);
	{
		ChatUser *target = userFindByContainerId(user->friends[i]);
		ChatPlayerStruct *pFriend;
		if (!target)
			continue;
		ChatShard_UpdateUserShards(target);
		pFriend = StructCreate(parse_ChatPlayerStruct);
		pFriend->accountID = target->id;
		pFriend->chatHandle = StructAllocString(target->handle);
		eaCopy(&pFriend->eaCharacterShards, (char***) &target->eaCharacterShards);
		eaPush(&friendList.chatAccounts, pFriend);
	}
	EARRAY_FOREACH_END;

	estrStackCreate(&listString);
	ParserWriteText(&listString, parse_ChatPlayerList, &friendList, 0, 0, 0);
	sendCommandToLinkEx(link, "userForwardFriendShards", "%d %d %d <&%s&>", eServerType, uServerID, uAccountID, listString);
	estrDestroy(&listString);
	StructDeInit(parse_ChatPlayerList, &friendList);
}