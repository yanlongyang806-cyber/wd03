#pragma once
GCC_SYSTEM

//if this include fails, it's probably because you have a slow remote function in a project that doesn't really
//support them, like a library
#include "localtransactionmanager.h"
#include "objtransactions.h"
#include "earray.h"


typedef struct TransactionReturnVal TransactionReturnVal;

//if you've called a remote command but no longer care about the return struct, you must call this:
void CancelRemoteAutoCommandReturn(TransactionReturnVal **hHandle);

__forceinline TransactionReturnVal *GetEmptyReturnValStructForRemoteCommand(void) {return calloc(sizeof(TransactionReturnVal),1);}
__forceinline void FreeReturnValStructForRemoteCommand(TransactionReturnVal *pRetVal) { free(pRetVal); }
__forceinline void CancelRemoteAutoCommandReturn(TransactionReturnVal **hHandle)
{
	TransactionReturnVal *pTransReturnStruct =  *hHandle;
	CancelAndRelaseReturnValData(objLocalManager(), pTransReturnStruct);
	FreeReturnValStructForRemoteCommand(pTransReturnStruct);
	*hHandle = 0;
}
