/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CCGTransactionReturnVal.h"
#include "EString.h"
#include "earray.h"
#include "CCGServer.h"
#include "TransactionOutcomes.h"
#include "LocalTransactionManager.h"
#include "objTransactions.h"

#include "AutoGen/CCGTransactionReturnVal_h_ast.h"

void
CCG_TRVSetMessage(CCGTransactionReturnVal *trv, char *msg)
{
	if ( trv->message != NULL )
	{
		estrDestroy(&trv->message);
	}

	trv->message = estrCreateFromStr(msg);
}

void
CCG_TRVAddDetailString(CCGTransactionReturnVal *trv, char *msg)
{
	eaPush(&trv->details, estrCreateFromStr(msg));
}

CCGTransactionReturnVal *
CCG_TRVFromTransactionRet(TransactionReturnVal *pReturn, char *message)
{
	CCGTransactionReturnVal *trv = StructCreate(parse_CCGTransactionReturnVal);

	trv->success = ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS );

	if ( message == NULL )
	{
		message = objAutoTransactionGetResult(pReturn);
	}

	if ( message != NULL )
	{
		CCG_TRVSetMessage(trv, message);
	}

	return trv;
}

char *
CCG_TRVToXMLResponseString(CCGTransactionReturnVal *trv, bool freetrv)
{
	char *retStr = NULL;

	CCG_BuildXMLResponseString(&retStr, parse_CCGTransactionReturnVal, trv);

	if ( freetrv )
	{
		StructDestroy(parse_CCGTransactionReturnVal, trv);
	}

	return retStr;
}

char *
CCG_TRToXMLResponseString(TransactionReturnVal *pReturn, char *message)
{
	return CCG_TRVToXMLResponseString(CCG_TRVFromTransactionRet(pReturn, message), true);
}

CCGTransactionReturnVal *
CCG_CreateTRV(bool success, char *message)
{
	CCGTransactionReturnVal *trv = StructCreate(parse_CCGTransactionReturnVal);

	trv->success = success;
	trv->message = estrCreateFromStr(message);

	return trv;
}

void
CCG_FillXMLResponse(char **estr, CCGTransactionReturnVal *trv, bool freetrv)
{
	CCG_BuildXMLResponseString(estr, parse_CCGTransactionReturnVal, trv);

	if ( freetrv )
	{
		StructDestroy(parse_CCGTransactionReturnVal, trv);
	}
}

#include "CCGTransactionReturnVal_h_ast.c"
