#ifndef CHATLOCAL_H
#define CHATLOCAL_H
#pragma once

typedef struct TransactionReturnVal TransactionReturnVal;
typedef struct ChatMessage ChatMessage;

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
	U32 uQueueID;
	U32 uEntityID;
	U32 uStartTime; // when this message was first attempted to be sent
	ChatLocalMessageType eType;
	ChatLocalMsgData data; AST(EMBEDDED_FLAT)

	//bool bIsInserted;
} ChatLocalQueueMsg;

void ChatLocal_EntityCmdReturn(TransactionReturnVal *pReturnVal, ChatLocalQueueMsg *data);
void ChatLocal_SendQueuedMessages(void);

#endif