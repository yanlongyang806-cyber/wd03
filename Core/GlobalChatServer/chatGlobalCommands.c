#include "chatdb.h"
#include "AutoGen\chatdb_h_ast.h"
#include "ChatServer.h"
#include "chatCommandStrings.h"
#include "ChatServer/chatShared.h"
#include "users.h"
#include "userPermissions.h"
#include "channels.h"
#include "friendsIgnore.h"
#include "chatUtilities.h"
#include "msgsend.h"
#include "NotifyCommon.h"

#include "shardnet.h"
#include "chatCommonStructs_h_ast.h"
#include "chatGlobal.h"
#include "objContainer.h"
#include "cmdparse.h"
#include "ChatServer/xmppTypes.h"

extern CmdList gRemoteCmdList;
extern bool gbGlobalChatResponse;

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerPrivateMesssage(CmdContext *context, ChatMessage *pMsg)
{
	if (!pMsg || !pMsg->pFrom || !pMsg->pTo) {
		return CHATRETURN_NONE;
	}
	{
		ChatUser *from = userFindByContainerId(pMsg->pFrom->accountID);
		ChatUser *to = (pMsg->pTo->accountID) ? userFindByContainerId(pMsg->pTo->accountID) : userFindByHandle(pMsg->pTo->pchHandle);
		NetLink *link = chatServerGetCommandLink();

		//Make sure from isn't spamming
		if(from && to && !UserIsAdmin(to) && chatRateLimiter(from)) {
			//log from's message, but append "SILENCED_" to it
			char* message = NULL;
			estrCreate(&message);
			estrAppend2(&message, "SILENCED_");
			estrAppend2(&message, pMsg->pchText);
			chatServerLogUserCommandWithReturnCode(LOG_CHATPRIVATE, CHATCOMMAND_PRIVATE_SEND, 
				CHATCOMMAND_PRIVATE_RECEIVE, from, to, CHATRETURN_USER_PERMISSIONS);
			estrDestroy(&message);
			return CHATRETURN_USER_PERMISSIONS;
		}

		if (from && to && userIsSilenced(from) && (!UserIsAdmin(to) || from == to))
		{
			U32 hour, min, sec;
			userGetSilenceTimeLeft(from, &hour, &min, &sec);
			sendTranslatedMessageToUser(from, kChatLogEntryType_System, NULL, "ChatServer_Silenced", 
				STRFMT_INT("H", hour), STRFMT_INT("M", min), STRFMT_INT("S", sec), STRFMT_END);
			chatServerLogUserCommandWithReturnCode(LOG_CHATPRIVATE, 
				CHATCOMMAND_PRIVATE_SEND, CHATCOMMAND_PRIVATE_RECEIVE, from, to, CHATRETURN_USER_PERMISSIONS);
			return CHATRETURN_USER_PERMISSIONS;
		}

		if (!to) { // does not query account server in localshardonlymode
			if (!gbGlobalChatResponse && context)
			{
				if (globalChatAddCommandWaitQueueByLink(chatServerGetCommandLink(), pMsg->pTo->pchHandle, context->commandString))
					return CHATRETURN_FWD_NONE; // delayed response
			}
			to = userFindByHandle(pMsg->pTo->pchHandle);
		}
		pMsg->xmppType = XMPP_MessageType_Chat; // private tells from game are XMPP-type "chat", NOT a normal "message"
		return userSendPrivateMessage(from, to, pMsg, link);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerBatchSendLocalChatGlobal (CmdContext *context, ChatContainerIDList *pTargetAccountIDList, ChatMessage *pMsg)
{
	ChatUser *pFromUser;
	if (!pMsg || !pMsg->pFrom)
		return CHATRETURN_FWD_NONE;
	pFromUser = userFindByContainerId(pMsg->pFrom->accountID);
	if (!pFromUser)
		return CHATRETURN_FWD_NONE;
	
	//Make sure from isn't spamming
	if(chatRateLimiter(pFromUser)) {
		char* message = NULL; //append "SILENCED_" to message in log
		estrCreate(&message);
		estrAppend2(&message, "SILENCED_");
		estrAppend2(&message, pMsg->pchText);
		chatServerLogUserCommandWithReturnCode(LOG_CHATLOCAL, CHATCOMMAND_CHANNEL_SEND, NULL, pFromUser, NULL, CHATRETURN_USER_PERMISSIONS);
		estrDestroy(&message);
	}

	{ // Global Chat Logging
		char localNameBuffer[512];
		NetLink *localLink = chatServerGetCommandLink();
		U32 uLocalID = GetLocalChatLinkID(localLink);
		PlayerInfoStruct *pInfo = userFindPlayerInfoByLinkID(pFromUser, uLocalID);
		if (pInfo)
			sprintf (localNameBuffer, "%s:%s-%d:%s", GetLocalChatLinkShardName(localLink), 
				pInfo->playerMap.pchMapName, pInfo->playerMap.iMapInstance, LOCAL_CHANNEL_NAME);
		chatServerLogChannelCommand(CHATCOMMAND_CHANNEL_SEND, localNameBuffer, pFromUser, pMsg->pchText);
	}
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerMessageReceive(CmdContext *context, ChatMessage *pMsg)
{
	ChatUser *pFromUser= NULL;

	if (pMsg && pMsg->pFrom) {
		pFromUser = userFindByContainerId(pMsg->pFrom->accountID);
	}

	if (!pFromUser) {
		return CHATRETURN_USER_DNE;
	}

	//Make sure from isn't spamming
	if(chatRateLimiter(pFromUser)) {
		//append "SILENCED_" to message in log
		char* message = NULL;
		estrCreate(&message);
		estrAppend2(&message, "SILENCED_");
		estrAppend2(&message, pMsg->pchText);
		chatServerLogUserCommandWithReturnCode(LOG_CHATSERVER, CHATCOMMAND_CHANNEL_SEND, CHATCOMMAND_CHANNEL_RECEIVE, pFromUser, NULL, CHATRETURN_USER_PERMISSIONS);
		estrDestroy(&message);
		return CHATRETURN_USER_PERMISSIONS;
	}

	// Fill in missing FROM user info as best as possible. 
	// DO NOT FILL in pMsg->pTo (even if we know at this point) because it's supposed to be NULL
	ChatFillUserInfo(&pMsg->pFrom, pFromUser);

	channelSend(pFromUser, pMsg);
	return CHATRETURN_FWD_NONE;
}

////////////////////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerAddFriendByHandle(CmdContext *context, ContainerID userID, ACMD_SENTENCE targetHandle)
{
	ChatUser *user, *target;
	if (!gbGlobalChatResponse)
	{
		if (globalChatAddCommandWaitQueueByLink(chatServerGetCommandLink(), targetHandle, context->commandString))
			return CHATRETURN_FWD_NONE; // delayed response
	}
	user = userFindByContainerId(userID);
	target = userFindByHandle(targetHandle);
	userAddFriend(user, target);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerAcceptFriendByHandle(CmdContext *context, ContainerID userID, ACMD_SENTENCE targetHandle)
{
	ChatUser *user, *target;
	if (!gbGlobalChatResponse)
	{
		if (globalChatAddCommandWaitQueueByLink(chatServerGetCommandLink(), targetHandle, context->commandString))
			return CHATRETURN_FWD_NONE; // delayed response
	}
	user = userFindByContainerId(userID);
	target = userFindByHandle(targetHandle);
	userAcceptFriend(user, target);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerRejectFriendByHandle(CmdContext *context, ContainerID userID, ACMD_SENTENCE targetHandle)
{
	ChatUser *user, *target;
	if (!gbGlobalChatResponse)
	{
		if (globalChatAddCommandWaitQueueByLink(chatServerGetCommandLink(), targetHandle, context->commandString))
			return CHATRETURN_FWD_NONE; // delayed response
	}
	user = userFindByContainerId(userID);
	target = userFindByHandle(targetHandle);
	userRejectFriend(user, target);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerAddFriendByID(CmdContext *context, ContainerID userID, ContainerID targetID)
{
	ChatUser *user = userFindByContainerId(userID);
	ChatUser *target = userFindByContainerId(targetID);
	userAddFriend(user, target);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerAddFriendComment(CmdContext *context, ContainerID userID, ContainerID targetID, ACMD_SENTENCE pcComment)
{
	ChatUser *user = userFindByContainerId(userID);
	ChatUser *target = userFindByContainerId(targetID);
	userSetFriendComment(user, target, pcComment);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerAddFriendCommentByHandle(CmdContext *context, ContainerID userID, const char *handle, ACMD_SENTENCE pcComment)
{
	ChatUser *user = userFindByContainerId(userID);
	ChatUser *target = userFindByHandle(handle);
	userSetFriendComment(user, target, pcComment);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerAcceptFriendByID(CmdContext *context, ContainerID userID, ContainerID targetID)
{
	ChatUser *user = userFindByContainerId(userID);
	ChatUser *target = userFindByContainerId(targetID);
	userAcceptFriend(user, target);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerRejectFriendByID(CmdContext *context, ContainerID userID, ContainerID targetID)
{
	ChatUser *user = userFindByContainerId(userID);
	ChatUser *target = userFindByContainerId(targetID);
	userRejectFriend(user, target);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerRemoveFriendByHandle(CmdContext *context, ContainerID userID, ACMD_SENTENCE targetHandle)
{
	ChatUser *user, *target;
	if (!gbGlobalChatResponse)
	{
		if (globalChatAddCommandWaitQueueByLink(chatServerGetCommandLink(), targetHandle, context->commandString))
			return CHATRETURN_FWD_NONE; // delayed response
	}
	user = userFindByContainerId(userID);
	target = userFindByHandle(targetHandle);
	userRemoveFriend(user, target ? target->id : 0);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerRemoveFriendByID(CmdContext *context, ContainerID userID, ContainerID targetID)
{
	ChatUser *user = userFindByContainerId(userID);
	userRemoveFriend(user, targetID);
	return CHATRETURN_FWD_NONE;
}


AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerAddIgnoreByHandle(CmdContext *context, ContainerID userID, ACMD_SENTENCE targetHandle, bool spammer)
{
	ChatUser *user, *target;
	if (!gbGlobalChatResponse)
	{
		if (globalChatAddCommandWaitQueueByLink(chatServerGetCommandLink(), targetHandle, context->commandString))
			return CHATRETURN_FWD_NONE; // delayed response
	}
	user = userFindByContainerId(userID);
	target = userFindByHandle(targetHandle);
	userIgnore(user, target, spammer, NULL);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerAddIgnoreByID(CmdContext *context, ContainerID userID, ContainerID targetID, bool spammer)
{
	ChatUser *user, *target;
	user = userFindByContainerId(userID);
	target = userFindByContainerId(targetID);
	userIgnore(user, target, spammer, NULL);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerIgnoreByID(CmdContext *context, ContainerID userID, ContainerID targetID)
{
	ChatUser *user = userFindByContainerId(userID);
	ChatUser *target = userFindByContainerId(targetID);
	userIgnore(user, target, false, NULL);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerAddIgnoreByStruct(CmdContext *context, ChatIgnoreData *ignoreData)
{
	ChatUser *user, *target;
	user = userFindByContainerId(ignoreData->uUserAccountID);
	target = userFindByContainerId(ignoreData->uTargetAccountID);
	if (!target && ignoreData->pTargetHandle)
		target = userFindByHandle(ignoreData->pTargetHandle);
	if (user && target)
		userIgnore(user, target, ignoreData->bSpammer, ignoreData->pSpamMessage);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerRemoveIgnoreByHandle(CmdContext *context, ContainerID userID, ACMD_SENTENCE targetHandle)
{
	ChatUser *user, *target;
	if (!gbGlobalChatResponse)
	{
		if (globalChatAddCommandWaitQueueByLink(chatServerGetCommandLink(), targetHandle, context->commandString))
			return CHATRETURN_FWD_NONE; // delayed response
	}
	user = userFindByContainerId(userID);
	target = userFindByHandle(targetHandle);
	userRemoveIgnore(user, target);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerRemoveIgnoreByID(CmdContext *context, ContainerID userID, ContainerID targetID)
{
	ChatUser *user, *target;
	user = userFindByContainerId(userID);
	target = userFindByContainerId(targetID);
	userRemoveIgnore(user, target);
	return CHATRETURN_FWD_NONE;
}
