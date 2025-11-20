/***************************************************************************
*     Copyright (c) 2013, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#pragma once

typedef struct Packet Packet;
typedef struct NetLink NetLink;

#include "objTransactions.h"

void ObjectDBThreadedLocalTransactionManagerLinkCallback(Packet *pak, int cmd, NetLink *link, void *pUserData);


typedef unsigned int TransactionID;
typedef struct StashTableImp *StashTable;
typedef StashTable NameTable;
typedef struct TransactionHandleCache TransactionHandleCache;

typedef struct dbHandleNewTransaction_Data
{
	bool bWantsReturn;
	bool bRequiresConfirm;
	bool bSucceedAndConfirmIsOK;
	bool bTransactionIsSlow;
	TransactionID iTransID;
	const char *pTransactionName;
	int iTransIndex;
	GlobalType eRecipientType;
	ContainerID iRecipientID;
	NameTable transVariableTable; 
	char *pReturnString;
	char *pTransactionString;
	U64 iForegroundTicks;
	U64 iBackgroundTicks;
	U64 iQueueTicks;
	U64 iQueueStartTicks;
	LTMObjectHandle objHandle;
	LTMObjectFieldsHandle objFieldsHandle;
	LTMProcessedTransactionHandle processedTransactionHandle;
	TransactionHandleCache *pHandleCache;
} dbHandleNewTransaction_Data;

typedef struct dbHandleCancelTransaction_Data
{
	TransactionID iTransID;
	int iTransIndex;
	GlobalType eRecipientType;
	ContainerID iRecipientID;
	TransactionHandleCache *pHandleCache;
} dbHandleCancelTransaction_Data;

typedef struct dbHandleConfirmTransaction_Data
{
	TransactionID iTransID;
	int iTransIndex;
	GlobalType eRecipientType;
	ContainerID iRecipientID;
	TransactionHandleCache *pHandleCache;
} dbHandleConfirmTransaction_Data;

void dbHandleNewTransactionCB(GWTCmdPacket *packet, dbHandleNewTransaction_Data *data);
void dbHandleCancelTransactionCB(dbHandleCancelTransaction_Data *data);
void dbHandleConfirmTransactionCB(dbHandleConfirmTransaction_Data *data);
void dbAddHandleNewTransactionTimingThreaded(void *user_data, void *data, GWTCmdPacket *packet);
