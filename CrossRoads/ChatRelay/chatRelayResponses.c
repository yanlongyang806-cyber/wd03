#include "chatRelay.h"
#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "ChatData.h"
#include "NotifyEnum.h"
#include "textparser.h"

#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/ChatData_h_ast.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_GenericClientCmdWrappers.h"

/////////////////////////////////
// Responses from Chat Server
/////////////////////////////////

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crPlayerEntityUpdate(U32 uAccountID, U32 uEntityID)
{
	ChatRelayUser *relayUser = chatRelayGetUser(uAccountID);
	if (!relayUser)
		return;
	relayUser->uEntityID = uEntityID;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crLoginSucceeded(U32 uAccountID)
{
	ChatRelayUser *relayUser = chatRelayGetUser(uAccountID);
	if (!relayUser)
		return;
	RemoteCommand_ChatServerGetIgnoreList(GLOBALTYPE_CHATSERVER, 0, uAccountID);
	RemoteCommand_ChatServerGetFriendsList(GLOBALTYPE_CHATSERVER, 0, uAccountID);
	GClientCmd_gclChat_LoginSuccess(relayUser->uAccountID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crReceiveFriends(U32 uAccountID, ChatPlayerList *pFriends)
{
	ChatRelayUser *relayUser = chatRelayGetUser(uAccountID);
	if (!relayUser)
		return;
	GClientCmd_gclChatCmd_ReceiveFriends(uAccountID, uAccountID, pFriends);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crReceiveIgnores(U32 uAccountID, ChatPlayerList *pIgnores)
{
	ChatRelayUser *relayUser = chatRelayGetUser(uAccountID);
	if (relayUser)
		GClientCmd_gclChatCmd_ReceiveIgnores(uAccountID, uAccountID, pIgnores);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crReceiveBatchActivityUpdate(PlayerStatusChange *pStatusChange)
{
	INT_EARRAY eaiAccountIDs;
	eaiAccountIDs = pStatusChange->eaiAccountIDs;
	pStatusChange->eaiAccountIDs = NULL;
	EARRAY_INT_CONST_FOREACH_BEGIN(eaiAccountIDs, i, n);
	{
		ChatRelayUser *relayUser = chatRelayGetUser(eaiAccountIDs[i]);
		if (relayUser && relayUser->bAuthed)
		{
			GClientCmd_gclChatCmd_ReceiveFriendActivityUpdate(relayUser->uAccountID, pStatusChange);
		}
	}
	EARRAY_FOREACH_END;
	pStatusChange->eaiAccountIDs = eaiAccountIDs; // Set this back so it gets properly freed by the cmd handler
}

extern bool gbDebugMode;
__forceinline static void crAddMessageCount(ChatLogEntryType eType)
{
	switch (eType)
	{
	case kChatLogEntryType_Zone:
		ADD_MISC_COUNT(1, "ZoneMsgReceive");
	xcase kChatLogEntryType_Guild:
		ADD_MISC_COUNT(1, "GuildMsgReceive");
	xcase kChatLogEntryType_Officer:
		ADD_MISC_COUNT(1, "OfficerMsgReceive");
	xcase kChatLogEntryType_Local:
		ADD_MISC_COUNT(1, "LocalMsgReceive");
	xcase kChatLogEntryType_Team:
		ADD_MISC_COUNT(1, "TeamMsgReceive");
	xcase kChatLogEntryType_TeamUp:
		ADD_MISC_COUNT(1, "TeamUpMsgReceive");
	xcase kChatLogEntryType_Channel:
		ADD_MISC_COUNT(1, "ChanMsgReceive");
	xcase kChatLogEntryType_Private:
		ADD_MISC_COUNT(1, "PrivateMsgReceive");
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crMessageReceive(U32 uAccountID, SA_PARAM_NN_VALID ChatMessage *pMsg)
{
	ChatRelayUser *relayUser = chatRelayGetUser(uAccountID);
	if (relayUser && relayUser->bAuthed)
	{
		PERFINFO_AUTO_START_FUNC();
		if (gbDebugMode)
			crAddMessageCount(pMsg->eType);
		GClientCmd_ChatLog_AddMessage(relayUser->uAccountID, pMsg, true);
		PERFINFO_AUTO_STOP_FUNC();
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crMessageReceiveBatch(PlayerInfoList *pAccountList, SA_PARAM_NN_VALID ChatMessage *pMsg)
{
	PERFINFO_AUTO_START_FUNC();
	if (gbDebugMode)
		crAddMessageCount(pMsg->eType);
	EARRAY_INT_CONST_FOREACH_BEGIN(pAccountList->piAccountIDs, i, n);
	{
		ChatRelayUser *relayUser = chatRelayGetUser(pAccountList->piAccountIDs[i]);
		if (relayUser && relayUser->bAuthed)
		{
			GClientCmd_ChatLog_AddMessage(relayUser->uAccountID, pMsg, true);
		}
	}
	EARRAY_FOREACH_END;
	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crReceiveSystemAlert(U32 uAccountID, const char *title, const char *text)
{
	ChatRelayUser *relayUser = chatRelayGetUser(uAccountID);
	crNotifySend(relayUser, kNotifyType_ServerBroadcast, text);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crReceiveNotify(U32 uAccountID, NotifyType eType, const char *pchDisplayMsg)
{
	ChatRelayUser *relayUser = chatRelayGetUser(uAccountID);
	crNotifySend(relayUser, eType, pchDisplayMsg);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crReceiveChannelSystemMessage(U32 uAccountID, int eType, const char *channel_name, const char *msg)
{
	ChatRelayUser *relayUser = chatRelayGetUser(uAccountID);
	if (relayUser)
	{
		ChatMessage *pMsg;
		PERFINFO_AUTO_START("CreateMsg", 1);
		pMsg = ChatCommon_CreateMsg(NULL, NULL, eType, channel_name, msg, NULL);
		GClientCmd_ChatLog_AddMessage(relayUser->uAccountID, pMsg, false);
		StructDestroy(parse_ChatMessage, pMsg);
		PERFINFO_AUTO_STOP();
	}
}

static void chatRelay_SendCallbackMsg(ChatRelayUser *relayUser, NotifyType eNotifyType, const char *pchMessage, const char *pchRawHandle)
{
	char *pchHandle = NULL;
	ChatData *pData = NULL;

	estrPrintf(&pchHandle, "@%s", pchRawHandle);
	pData = ChatData_CreatePlayerHandleDataFromMessage(pchMessage, pchHandle, false, false);
	crNotifySendData(relayUser, eNotifyType, pchMessage, pData);

	StructDestroy(parse_ChatData, pData); // Includes pLinkInfo & pLink
	estrDestroy(&pchHandle);
}

// Friends and Ignores //

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crReceiveFriendCB(U32 uAccountID, ChatPlayerStruct *pChatPlayer, FriendResponseEnum eType, const char *pchNotifyMsg, bool bIsError)
{
	ChatRelayUser *relayUser = chatRelayGetUser(uAccountID);
	if (!relayUser)
		return;
	if (bIsError)
	{
		crNotifySend(relayUser, kNotifyType_ChatFriendError, pchNotifyMsg);
	}
	else if (pChatPlayer)
	{
		bool bSendDownToClient = false;
		if (pchNotifyMsg && *pchNotifyMsg)
			chatRelay_SendCallbackMsg(relayUser, kNotifyType_ChatFriendNotify, pchNotifyMsg, pChatPlayer->chatHandle);
		
		// Synchronize the friends list
		switch (eType) {
		case FRIEND_REQUEST_SENT:
		case FRIEND_REQUEST_ACCEPTED:
		case FRIEND_ADDED:
		case FRIEND_REQUEST_RECEIVED:
		case FRIEND_REQUEST_ACCEPT_RECEIVED:
		case FRIEND_UPDATED:
			bSendDownToClient= true;
			break;

		case FRIEND_OFFLINE:
		case FRIEND_ONLINE:
			bSendDownToClient= true;
			break;

		case FRIEND_COMMENT:
			bSendDownToClient= true;
			break;

		case FRIEND_REQUEST_REJECTED:
		case FRIEND_REMOVED:
		case FRIEND_REQUEST_REJECT_RECEIVED:
		case FRIEND_REMOVE_RECEIVED:
			bSendDownToClient= true;
			break;

		case FRIEND_NONE:
			break; // do nothing

		default:
			devassertmsgf(0, "Unexpected FriendResponseEnum: %d", eType);
			break;
		}
		if (bSendDownToClient)
			GClientCmd_gclChatCmd_ReceiveFriendCB(relayUser->uAccountID, uAccountID, pChatPlayer, eType);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crReceiveIgnoreCB(U32 uAccountID, ChatPlayerStruct *pChatPlayer, IgnoreResponseEnum eType, const char *pchNotifyMsg, bool bIsError)
{
	ChatRelayUser *relayUser = chatRelayGetUser(uAccountID);
	if (!relayUser)
		return;
	if (bIsError)
		crNotifySend(relayUser, kNotifyType_ChatIgnoreError, pchNotifyMsg);
	else if (pChatPlayer)
	{
		if (pchNotifyMsg && *pchNotifyMsg)
			chatRelay_SendCallbackMsg(relayUser, kNotifyType_ChatIgnoreNotify, pchNotifyMsg, pChatPlayer->chatHandle);
		GClientCmd_gclChatCmd_ReceiveIgnoreCB(relayUser->uAccountID, uAccountID, pChatPlayer, eType);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crForwardWhiteListInfo(U32 uAccountID, bool bChatEnable, bool bTellsEnable, bool bEmailEnable)
{
	ChatRelayUser *relayUser = chatRelayGetUser(uAccountID);
	if (relayUser)
	{
		GClientCmd_cmdClientChat_ReceiveWhitelistInfo(relayUser->uAccountID, bChatEnable, bTellsEnable, bEmailEnable);
	}
}

/////////////////////////////////
// Channels

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crReceiveChannelList(U32 uAccountID, ChatChannelInfoList *pChannelList)
{
	ChatRelayUser *relayUser = chatRelayGetUser(uAccountID);
	if (relayUser)
	{
		eaDestroyEString(&relayUser->eaSubscribedChannels);
		EARRAY_FOREACH_BEGIN(pChannelList->ppChannels, i);
		{
			if (pChannelList->ppChannels[i]->pName)
				eaPush(&relayUser->eaSubscribedChannels, estrDup(pChannelList->ppChannels[i]->pName));
		}
		EARRAY_FOREACH_END;
		GClientCmd_ClientChat_ReceiveUserChannelList(relayUser->uAccountID, pChannelList);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crChannelUpdate(U32 uAccountID, ChatChannelInfo *channel_info, ChannelUpdateEnum eChangeType)
{
	ChatRelayUser *relayUser = chatRelayGetUser(uAccountID);
	if (relayUser)
	{
		/*if (channel_info->uReservedFlags & CHANNEL_SPECIAL_SHARDGLOBAL)
		{
			ServerChat_AddShardGlobalChannel(pEnt, ShardChannel_StripVShardPrefix(channel_info->pName));
		}*/
		GClientCmd_ClientChat_ChannelUpdate(relayUser->uAccountID, channel_info, eChangeType);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crReceiveJoinChannelInfo(U32 uAccountID, ChatChannelInfo *pInfo)
{
	ChatRelayUser *relayUser = chatRelayGetUser(uAccountID);
	if (relayUser)
	{
		GClientCmd_cmdClientChat_ReceiveJoinChannelInfo(relayUser->uAccountID, pInfo);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crActiveMapsUpdate(U32 uAccountID, ChatMapList *pMapList)
{
	ChatRelayUser *relayUser = chatRelayGetUser(uAccountID);
	if (relayUser)
	{
		GClientCmd_gclChat_UpdateMapsCmd(relayUser->uAccountID, pMapList);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void crReceiveChatAccessLevel(U32 uAccountID, U32 uChatAccessLevel)
{
	ChatRelayUser *relayUser = chatRelayGetUser(uAccountID);

	if (relayUser)
	{
		ChatMessage msg = {0};
		msg.eType = kChatLogEntryType_System;
		estrPrintf(&msg.pchText, "%d", uChatAccessLevel);
		GClientCmd_ChatLog_AddMessage(uAccountID, &msg, false);
		StructDeInit(parse_ChatMessage, &msg);
	}
}