#include "chatLocal.h"
#include "earray.h"
#include "chatGlobal.h"
#include "LocalTransactionManager.h"
#include "cmdparse.h"
#include "chatCommonStructs.h"

#include "chatLocal_h_ast.h"
#include "Autogen/GameServerLib_autogen_RemoteFuncs.h"

//static int siQueueCount = 0;
static U32 suQueueID = 1;
static ChatLocalQueueMsg **seaChatMessageQueue = NULL;

static ChatLocalQueueMsg *ChatLocal_FindQueueID(U32 uQueueID)
{
	int i;
	int size = eaSize(&seaChatMessageQueue);
	for (i=0; i<size; i++)
	{
		if (seaChatMessageQueue[i]->uQueueID == uQueueID)
			return seaChatMessageQueue[i];
	}
	return NULL;
}
static __forceinline void ChatLocal_RemoveQueue (U32 uQueueID)
{
	ChatLocalQueueMsg *queue = ChatLocal_FindQueueID(uQueueID);
	if (queue && eaFindAndRemove(&seaChatMessageQueue, queue) >= 0)
	{
		StructDestroy(parse_ChatLocalQueueMsg, queue);
	}
}

void ChatLocal_EntityCmdReturn(TransactionReturnVal *pReturnVal, ChatLocalQueueMsg *data)
{
	if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		int iReturnCode = atoi(pReturnVal->pBaseReturnVals->returnString);
		switch (iReturnCode)
		{
		case CHATENTITY_SUCCESS:
			// does nothing
		xcase CHATENTITY_NOTFOUND:
		case CHATENTITY_MAPTRANSFERRING:
			if (!data->uQueueID)
			{
				data->uQueueID = suQueueID++;
				eaPush(&seaChatMessageQueue, data);
				return;
			}
		}
	}
	else if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		if (!data->uQueueID)
		{
			data->uQueueID = suQueueID++;
			eaPush(&seaChatMessageQueue, data);
			return;
		}
	}
	if (data->uQueueID)
	{
		ChatLocal_RemoveQueue(data->uQueueID);
	}
	StructDestroy(parse_ChatLocalQueueMsg, data);
}

static void ChatLocal_SendMessage(ChatLocalQueueMsg *data)
{
	ChatLocalQueueMsg *dataCopy = StructClone(parse_ChatLocalQueueMsg, data);
	switch (data->eType)
	{
	case CHATLOCAL_PLAYERMSG:
		RemoteCommand_cmdServerChat_MessageReceive(objCreateManagedReturnVal(ChatLocal_EntityCmdReturn, dataCopy), 
			GLOBALTYPE_ENTITYPLAYER, data->uEntityID, data->uEntityID, data->data.chatMessage);
	xcase CHATLOCAL_SYSMSG:
		RemoteCommand_cmdServerChat_SendChannelSystemMessage(objCreateManagedReturnVal(ChatLocal_EntityCmdReturn, dataCopy), 
			GLOBALTYPE_ENTITYPLAYER, data->uEntityID, data->uEntityID, data->data.iMessageType, data->data.channelName, data->data.message);
	xcase CHATLOCAL_SYSALERT:
		RemoteCommand_cmdServerChat_SendSystemAlert(objCreateManagedReturnVal(ChatLocal_EntityCmdReturn, dataCopy), 
			GLOBALTYPE_ENTITYPLAYER, data->uEntityID, data->uEntityID, data->data.channelName, data->data.message);
	xdefault:
		StructDestroy(parse_ChatLocalQueueMsg, dataCopy);
	}
}

#define MAX_WAIT_TIME 180 // Give 3 minutes for transfers, since some maps may be really large
void ChatLocal_SendQueuedMessages(void)
{
	static int iLastTimeRun = 0;
	int i, size;
	U32 uTime = timeSecondsSince2000();

	if (uTime - iLastTimeRun <= 0)
		return;
	iLastTimeRun = uTime; // Runs at most every second

	size = eaSize(&seaChatMessageQueue);
	for (i=0; i<size; )
	{
		ChatLocalQueueMsg *data = seaChatMessageQueue[i];
		CmdContext context = {0};
		
		if (uTime - data->uStartTime > MAX_WAIT_TIME)
		{
			// TODO send failure message?
			if (eaRemove(&seaChatMessageQueue, i))
				size--;
			StructDestroy(parse_ChatLocalQueueMsg, data);
		}
		else
		{
			ChatLocal_SendMessage(data);
			i++;
		}
	}
}

#include "chatLocal_h_ast.c"