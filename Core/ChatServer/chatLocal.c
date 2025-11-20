#include "chatLocal.h"

#include "chatCommonStructs.h"
#include "chatGlobal.h"
#include "chatShardConfig.h"
#include "cmdparse.h"
#include "earray.h"
#include "friendsIgnore.h"
#include "LocalTransactionManager.h"
#include "msgsend.h"
#include "objContainer.h"
#include "users.h"

#include "chatLocal_h_ast.h"
#include "chatLocal_c_ast.h"
#include "Autogen/GameServerLib_autogen_RemoteFuncs.h"

/////////////////////////////////
// Queue for Messages

AUTO_STRUCT;
typedef struct ChatLocalUserMessages
{
	U32 uEntityID; AST(KEY)
	bool bResend; // This flag is toggled on after receiving a CHATENTITY_MAPTRANSFERRING response
	EARRAY_OF(ChatLocalQueueMsg) ppMessages;
} ChatLocalUserMessages;

static U32 suQueueID = 1;
static ChatLocalUserMessages **seaChatUserMessages = NULL;

AUTO_RUN;
void ChatLocal_InitUserMessageQueue(void)
{
	eaIndexedEnable(&seaChatUserMessages, parse_ChatLocalUserMessages);
}

static void ChatLocal_SendMessage(ChatLocalQueueMsg *data)
{
	// Does nothing with ChatRelays being used
#ifdef USE_CHATRELAY
	devassert(0);
#else
	switch (data->eType)
	{
	case CHATLOCAL_PLAYERMSG:
		RemoteCommand_cmdServerChat_MessageReceive(GLOBALTYPE_ENTITYPLAYER, data->uEntityID, data->uEntityID, 
			data->data.chatMessage, data->uQueueID);
	xcase CHATLOCAL_SYSMSG:
		RemoteCommand_cmdServerChat_SendChannelSystemMessage(GLOBALTYPE_ENTITYPLAYER, data->uEntityID, data->uEntityID, 
			data->data.iMessageType, data->data.channelName, data->data.message, data->uQueueID);
	xcase CHATLOCAL_SYSALERT:
		RemoteCommand_cmdServerChat_SendSystemAlert(GLOBALTYPE_ENTITYPLAYER, data->uEntityID, data->uEntityID, 
			data->data.channelName, data->data.message, data->uQueueID);
	}
#endif
}

AUTO_COMMAND_REMOTE;
void ChatLocal_MessageResponse (U32 uEntityID, U32 uQueueID, ChatEntityCommandReturnCode eReturnCode)
{
	switch (eReturnCode)
	{
		case CHATENTITY_SUCCESS:
			{
				int userMsgIdx = eaIndexedFindUsingInt(&seaChatUserMessages, uEntityID);
				if (userMsgIdx >= 0)
				{
					ChatLocalUserMessages *userMsgs = seaChatUserMessages[userMsgIdx];
					int index = eaIndexedFindUsingInt(&userMsgs->ppMessages, uQueueID);
					if (index >=0)
					{
						StructDestroy(parse_ChatLocalQueueMsg, userMsgs->ppMessages[index]);
						eaRemove(&userMsgs->ppMessages, index);
					}
					if (eaSize(&userMsgs->ppMessages) == 0)
					{
						StructDestroy(parse_ChatLocalUserMessages, userMsgs);
						eaRemove(&seaChatUserMessages, userMsgIdx);
					}
				}
			}
		xcase CHATENTITY_NOTFOUND:
		case CHATENTITY_MAPTRANSFERRING:
			{
				ChatLocalUserMessages *userMsgs = eaIndexedGetUsingInt(&seaChatUserMessages, uEntityID);
				if (userMsgs)
				{
					ChatLocalQueueMsg *data = eaIndexedGetUsingInt(&userMsgs->ppMessages, uQueueID);
					if (data) data->bResend = true;
				}
			}
		xcase CHATENTITY_OFFLINE:
			{   // User is offline - clear out all their pending messages
				int userMsgIdx = eaIndexedFindUsingInt(&seaChatUserMessages, uEntityID);
				if (userMsgIdx >= 0)
				{
					StructDestroy(parse_ChatLocalUserMessages, seaChatUserMessages[userMsgIdx]);
					eaRemove(&seaChatUserMessages, userMsgIdx);
				}
			}
	}
}

bool ChatLocal_AddUserMessage (ChatLocalQueueMsg *data)
{
	ChatLocalUserMessages *userMsgs;
	if (data->uEntityID == 0)
		return false;
	userMsgs = eaIndexedGetUsingInt(&seaChatUserMessages, data->uEntityID);
	data->uQueueID = suQueueID++;

	if (!userMsgs)
	{
		userMsgs = StructCreate(parse_ChatLocalUserMessages);
		userMsgs->uEntityID = data->uEntityID;
		eaIndexedAdd(&seaChatUserMessages, userMsgs);
	}
	
	eaPush(&userMsgs->ppMessages, data);
	ChatLocal_SendMessage(data);
	return true;
}

#define MAX_WAIT_TIME 180 // Give 3 minutes for transfers, since some maps may be really large

void ChatLocal_QueuedMessageTick(void)
{
	static int iLastTimeRun = 0;
	int i;
	U32 uTime = timeSecondsSince2000();

	if (uTime - iLastTimeRun <= 0)
		return;
	iLastTimeRun = uTime; // Runs at most every second

	for (i=eaSize(&seaChatUserMessages)-1; i>=0; i--)
	{
		ChatLocalUserMessages *userMsgs = seaChatUserMessages[i];
		int j, numMsgs = eaSize(&userMsgs->ppMessages);

		if (numMsgs == 0)
		{
			StructDestroy(parse_ChatLocalUserMessages, userMsgs);
			eaRemove(&seaChatUserMessages, i);
			continue;
		}
		for (j=0; j<numMsgs;)
		{
			ChatLocalQueueMsg *data = userMsgs->ppMessages[j];
			if (uTime - data->uStartTime > MAX_WAIT_TIME)
			{
				StructDestroy(parse_ChatLocalQueueMsg, data);
				eaRemove(&userMsgs->ppMessages, j);
				numMsgs--;
			}
			else
			{
				if (data->bResend)
				{
					ChatLocal_SendMessage(data);
					data->bResend = false;
				}
				j++;
			}
		}

	}
}

/////////////////////////////////
// Queue for Group Login Requests

static U32 suLoginQueueID = 1;
static U32 *seaiChatLoginQueue = NULL;

void ChatLocal_AddToLoginQueue(U32 **eaiAccountIDs)
{
	if (eaiAccountIDs)
		eaiPushEArray(&seaiChatLoginQueue, eaiAccountIDs);
}

#define MAX_LOGIN_REQUESTS_PER_FRAME (100)
void ChatLocal_LoginQueueTicket(void)
{
	int i, size, j;
	size = eaiSize(&seaiChatLoginQueue);
	if (size && seaiChatLoginQueue)
	{
		for (i=0; i<size && i<MAX_LOGIN_REQUESTS_PER_FRAME; i++)
		{
			ChatUser *user = userFindByContainerId(seaiChatLoginQueue[i]);

			if (userCharacterOnline(user))
			{
				sendCmdAndParamsToGlobalChat("ChatServerGetFriendsList", "%d", user->id);
				sendCmdAndParamsToGlobalChat("ChatServerGetIgnoreList", "%d", user->id);
				if (user->online_status & USERSTATUS_AUTOAFK)
					userSendUpdateNotifications(user);
			}
		}
		for (j=0; j<i; j++) // since there is no eaiRemoveRange...
			eaiRemove(&seaiChatLoginQueue, j);
	}
}

/////////////////////////////////
// Ad Message Spam

static void ChatLocal_SendSpam (ChatUser *user, U32 uCurTime, U32 uPurchaseExclusionDuration)
{
	if (!user || !user->pPlayerInfo)
		return;

	if (user->pPlayerInfo->bGoldSubscriber)
		return;
	if (user->pPlayerInfo->uLastPurchase + uPurchaseExclusionDuration >= uCurTime)
		return;
	sendChatSystemStaticMsg(user, kChatLogEntryType_ChatSystem, NULL, ChatConfig_GetRandomAd(user->pPlayerInfo->eLanguage));
}

static void ChatLocal_SendSpamMessages(void)
{
	ContainerIterator iter = {0};
	ChatUser *user;
	U32 uPurchaseExclusion = ChatConfig_GetPurchaseExclusionDuration();
	U32 uTime = timeSecondsSince2000();

	if (!uPurchaseExclusion || !ChatConfig_HasAds())
		return;

	objInitContainerIteratorFromType(GLOBALTYPE_CHATUSER, &iter);
	while (user = objGetNextObjectFromIterator(&iter))
	{
		ChatLocal_SendSpam(user, uTime, uPurchaseExclusion);
	}
	objClearContainerIterator(&iter);
}

void ChatLocal_AdSpamTick(void)
{
	static U32 uLastTime = 0;
	U32 uTime = timeSecondsSince2000();
	U32 uAdRepetition = ChatConfig_GetAdRepetitionRate();

	if (uAdRepetition && uLastTime + uAdRepetition < uTime)
	{
		ChatLocal_SendSpamMessages();
		uLastTime = uTime;
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void ChatAd_UpdatePurchaseTime(U32 uID, U32 uTime)
{
	ChatUser *user = userFindByContainerId(uID);
	if (user && user->pPlayerInfo && user->pPlayerInfo->uLastPurchase < uTime)
		user->pPlayerInfo->uLastPurchase = uTime;
}

#include "chatLocal_h_ast.c"
#include "chatLocal_c_ast.c"