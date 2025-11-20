#ifndef CHATLOCAL_H
#define CHATLOCAL_H
#pragma once

#include "stdtypes.h"
#include "referencesystem.h"

typedef struct TransactionReturnVal TransactionReturnVal;
typedef struct ChatMessage ChatMessage;
typedef struct GameAccountData GameAccountData;

// Seconds between local chat messages required for social-restricted users
#define LOCAL_CHAT_THROTTLE (2)
#define LOCAL_CHAT_MESSAGE_BUFFER (4)

AUTO_ENUM;
typedef enum ChatLocalMessageType
{
	CHATLOCAL_PLAYERMSG = 0,
	CHATLOCAL_SYSMSG,
	CHATLOCAL_SYSALERT,
} ChatLocalMessageType;

AUTO_STRUCT;
typedef struct ChatLocalMsgData
{
	int iMessageType;
	char *channelName;
	char *message;
	ChatMessage *chatMessage;
} ChatLocalMsgData;

AUTO_STRUCT;
typedef struct ChatLocalQueueMsg
{
	U32 uQueueID; AST(KEY)
	U32 uEntityID;
	U32 uStartTime; // when this message was first attempted to be sent
	ChatLocalMessageType eType;
	ChatLocalMsgData data; AST(EMBEDDED_FLAT)

	bool bResend; // This is toggled on after receiving a CHATENTITY_MAPTRANSFERRING response
	//bool bIsInserted;
} ChatLocalQueueMsg;

AUTO_STRUCT;
typedef struct GADRef {
	REF_TO(GameAccountData) hGAD;
} GADRef;

extern ParseTable parse_GADRef[];
#define TYPE_parse_GADRef GADRef

void ChatLocal_EntityCmdReturn(TransactionReturnVal *pReturnVal, ChatLocalQueueMsg *data);
//void ChatLocal_SendQueuedMessages(void);

void ChatLocal_AddToLoginQueue(U32 **eaiAccountIDs);
void ChatLocal_LoginQueueTicket(void);

bool ChatLocal_AddUserMessage (ChatLocalQueueMsg *data);
void ChatLocal_QueuedMessageTick(void);
void ChatLocal_AdSpamTick(void);

#endif