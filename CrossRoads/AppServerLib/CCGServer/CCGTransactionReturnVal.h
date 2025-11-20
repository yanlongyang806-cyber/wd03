/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

AUTO_STRUCT;
typedef struct CCGTransactionReturnVal
{
	bool success;
	STRING_MODIFIABLE message;		AST(ESTRING)
	STRING_EARRAY details;			AST(ESTRING)
} CCGTransactionReturnVal;

typedef struct TransactionReturnVal TransactionReturnVal;

CCGTransactionReturnVal *CCG_TRVFromTransactionRet(TransactionReturnVal *pReturn, char *message);

char *CCG_TRToXMLResponseString(TransactionReturnVal *pReturn, char *message);

void CCG_TRVAddDetailEString(CCGTransactionReturnVal *trv, char *msg);

void CCG_TRVAddDetailString(CCGTransactionReturnVal *trv, char *msg);

void CCG_TRVSetMessage(CCGTransactionReturnVal *trv, char *msg);

char *CCG_TRVToXMLResponseString(CCGTransactionReturnVal *trv, bool freetrv);

void CCG_FillXMLResponse(char **estr, CCGTransactionReturnVal *trv, bool freetrv);

CCGTransactionReturnVal *CCG_CreateTRV(bool success, char *message);