/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct CmdSlowReturnForServerMonitorInfo CmdSlowReturnForServerMonitorInfo;
typedef struct CmdContext CmdContext;
typedef struct TransactionReturnVal TransactionReturnVal;
typedef struct CCGPrintRun CCGPrintRun;
typedef struct StashTableImp *StashTable;
typedef struct CCGCardDefList CCGCardDefList;

extern CCGCardDefList g_CardDefs;

#include "GlobalTypeEnum.h"

AUTO_STRUCT;
typedef struct CCGPooledStringList
{
	STRING_EARRAY list;				AST(POOL_STRING)	
} CCGPooledStringList;

typedef struct CCGTransactionReturnVal CCGTransactionReturnVal;

typedef void (*CCGCallbackFunc)(CCGTransactionReturnVal *trv, void *userData);

typedef struct CCGCallback
{
	CCGCallbackFunc userFunc;
	void *userData;
	void *internalData;
} CCGCallback;

CCGCallback *CCG_CreateCallback(CCGCallbackFunc userFunc, void *userData);
void CCG_CallCallback(CCGCallback *cb, CCGTransactionReturnVal *trv);
void CCG_FreeCallback(CCGCallback *cb);

void CCG_GenericTransactionCallback(TransactionReturnVal *pReturnVal, CCGCallback *cb);
void CCG_GenericCommandCallback(CCGTransactionReturnVal *trv, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo);

// create a simple success/fail string to return from xmlrpc commands
char * CCG_CreateSimpleReturnString(char *action, TransactionReturnVal *pReturnVal);

CmdSlowReturnForServerMonitorInfo *CCG_SetupSlowReturn(CmdContext *pContext);
void CCG_CancelSlowReturn(CmdContext *pContext);

CCGPrintRun *CCG_GetPrintRun(const char *printRunName);

void CCG_BuildXMLResponseString(char **responseString, ParseTable *tpi, void *struct_mem);
void CCG_BuildXMLResponseStringWithType(char **responseString, char *type, char *val);

const char *CCG_GetCardType(U32 cardID);

#include "GlobalTypeEnum.h"
