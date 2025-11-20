/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "TestTransHarness.h"

#include "ServerLib.h"
#include "../TransactionRequestManager.h"

typedef struct
{
	bool bIncBoth;

	bool bTime;
	bool bPlus;
	int iAmount;
} DecodedSimpleObjectTransaction;






SimpleObject *GetSimpleObjectFromID(SimpleServer *pServer, int iID)
{
	if ((iID-1) < OBJECTS_PER_SERVER * (pServer->iServerID) || (iID-1) >= OBJECTS_PER_SERVER * (pServer->iServerID + 1))
	{
		return NULL;
	}

	return &pServer->objects[(iID - 1) % OBJECTS_PER_SERVER];
}


void SimpleObjectBackupData(SimpleObject *pObject)
{
	assert(pObject->pBackupData == NULL);
	pObject->pBackupData = (SimpleObjectPersistData*)malloc(sizeof(SimpleObjectPersistData));
	memcpy(pObject->pBackupData, &pObject->data, sizeof(SimpleObjectPersistData));
}

void SimpleObjectRestoreBackup(SimpleObject *pObject)
{
	assert(pObject->pBackupData);

	memcpy(&pObject->data, pObject->pBackupData, sizeof(SimpleObjectPersistData));

	free(pObject->pBackupData);
	pObject->pBackupData = NULL;
}

void SimpleObjectCancelBackup(SimpleObject *pObject)
{
	assert(pObject->pBackupData);

	free(pObject->pBackupData);
	pObject->pBackupData = NULL;
}

bool IsWhiteSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\n';
}

int GetToken(const char *pSourceString, char *pDestString, int iMaxCharsToRead)
{
	const char *pOrigSourceString = pSourceString;
	char *pOrigDestString = pDestString;

	while (IsWhiteSpace(*pSourceString))
	{
		pSourceString++;
	}

	while (*pSourceString && !IsWhiteSpace(*pSourceString))
	{
		if (pDestString - pOrigDestString < iMaxCharsToRead - 1)
		{
			*pDestString = *pSourceString;
			pDestString++;
		}
		pSourceString++;
	}

	*pDestString = 0;
	return pSourceString - pOrigSourceString;
}

bool CheckStringForIntAndDecode(char *pString, int *pOutVal)
{
	int iVal = 0;
	bool bFoundMinus = false;

	if (*pString == '-')
	{
		bFoundMinus = true;
		pString++;
	}

	if (!(*pString))
	{
		return false;
	}

	do 	
	{
		int iDigit = *pString - '0';

		if (iDigit < 0 || iDigit > 9)
		{
			return false;
		}

		iVal *= 10;
		iVal += iDigit;

		pString++;
	} while (*pString);

	*pOutVal = bFoundMinus ? -iVal : iVal;

	return true;
}





bool SimpleObjectDecodeTranasction(const char *pTransaction, bool *pIncBoth, bool *pTime, bool *pPlus, int *pAmount)
{
	char tempBuffer[256];

	int iCurOffset;

	iCurOffset = GetToken(pTransaction, tempBuffer, 256);

	if (strcmp(tempBuffer, "incboth") == 0)
	{
		*pIncBoth = true;
	}
	else
	{
		*pIncBoth = false;

		if (strcmp(tempBuffer, "time") == 0)
		{
			*pTime = true;
		}
		else if (strcmp(tempBuffer, "money") == 0)
		{
			*pTime = false;
		}
		else
		{
			return false;
		}

		iCurOffset += GetToken(pTransaction + iCurOffset, tempBuffer, 256);

		if (strcmp(tempBuffer, "inc") == 0)
		{
			*pPlus = true;
		}
		else if (strcmp(tempBuffer, "dec") == 0)
		{
			*pPlus = false;
		}
		else
		{
			return false;
		}
	}

	iCurOffset += GetToken(pTransaction + iCurOffset, tempBuffer, 256);

	if (!CheckStringForIntAndDecode(tempBuffer, pAmount))
	{
		return false;
	}

	return true;
}





bool SimpleObjectCanDoTransaction(SimpleObject *pObject, DecodedSimpleObjectTransaction *pTransaction, char *pResultString, size_t pResultString_size)
{
	if (pTransaction->bPlus || pTransaction->bIncBoth)
	{
		return true;
	}
	else
	{
		if (pTransaction->bTime)
		{
			if (pObject->data.iTime < pTransaction->iAmount)
			{
				if (pResultString)
				{
					sprintf_s(SAFESTR2(pResultString), "not enough time (have %d  need %d)", pObject->data.iTime, pTransaction->iAmount);
				}

				return false;
			}
			else
			{
				return true;
			}
		}
		else
		{
			if (pObject->data.iMoney < pTransaction->iAmount)
			{
				if (pResultString)
				{
					sprintf_s(SAFESTR2(pResultString), "not enough money (have %d  need %d)", pObject->data.iMoney, pTransaction->iAmount);
				}

				return false;
			}
			else
			{
				return true;
			}
		}
	}
}

void SimpleObjectDoTransaction(SimpleObject *pObject, DecodedSimpleObjectTransaction *pTransaction, char *pResultString, size_t pResultString_size)
{
	assert(!(pTransaction->bIncBoth));

	if (pTransaction->bPlus)
	{
		if (pTransaction->bTime)
		{
			if (pResultString)
			{
				sprintf_s(SAFESTR2(pResultString), "Added %d time", pTransaction->iAmount);
			}
			pObject->data.iTime += pTransaction->iAmount;
		}
		else
		{
			if (pResultString)
			{
				sprintf_s(SAFESTR2(pResultString), "Added %d money", pTransaction->iAmount);
			}
			pObject->data.iMoney += pTransaction->iAmount;
		}
	}
	else
	{
		if (pTransaction->bTime)
		{
			if (pResultString)
			{
				sprintf_s(SAFESTR2(pResultString), "Subtracted %d time", pTransaction->iAmount);
			}
			pObject->data.iTime -= pTransaction->iAmount;
		}
		else
		{
			if (pResultString)
			{
				sprintf_s(SAFESTR2(pResultString), "Subtracted %d money", pTransaction->iAmount);
			}
			pObject->data.iMoney -= pTransaction->iAmount;
		}
	}
}





bool DoesSimpleObjectExist_CB(GlobalType eObjType, U32 iObjID, LTMObjectHandle *pFoundObjectHandle, char **ppReturnValString, void *pUserData)
{
	SimpleServer *pServer;
	SimpleObject *pObject;

	if (eObjType != GLOBALTYPE_ENTITYPLAYER)
	{
		return 0;
	}

	pServer = (SimpleServer *)pUserData;

	pObject = GetSimpleObjectFromID(pServer, iObjID);

	if (pObject)
	{
		*pFoundObjectHandle = pObject;
		return true;
	}
	else
	{
		estrPrintf(ppReturnValString, "Object does not exist");
		return false;
	}
}

bool IsSimpleObjectOKToBeLocked_CB(GlobalType eObjType, LTMObjectHandle objHandle, const char *pTransactionString, 
	LTMObjectFieldsHandle objFieldsHandle, U32 iTransactionID, const char *pTransactionName, void *pUserData, U32 *blockingID)
{
	SimpleServer *pServer;
	SimpleObject *pObject;

	assert(eObjType == GLOBALTYPE_ENTITYPLAYER);
	pServer = (SimpleServer *)pUserData;
	pObject = (SimpleObject *)objHandle;

	if (pObject->iSlowTransID)
	{
		return false;
	}

	if (pObject->iLockingID == 0 || pObject->iLockingID == iTransactionID)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool SimpleObjectCanTransactionBeDone_CB(GlobalType eObjType, LTMObjectHandle objHandle, 
	const char *pTransactionString, LTMProcessedTransactionHandle processedTransactionHandle, 
	LTMObjectFieldsHandle objFieldsHandle, 
	char **ppReturnValString, TransactionID iTransID, const char *pTransactionName, void *pUserData)
{
	char resultString[256] = {0};

	SimpleServer *pServer;
	SimpleObject *pObject;

	bool bResult;

	assert(eObjType == GLOBALTYPE_ENTITYPLAYER);
	pServer = (SimpleServer *)pUserData;
	pObject = (SimpleObject *)objHandle;

	resultString[0] = 0;

	bResult = SimpleObjectCanDoTransaction(pObject, (DecodedSimpleObjectTransaction*)processedTransactionHandle, SAFESTR(resultString));

	estrCopy2(ppReturnValString, resultString);

	return bResult;
}

void SimpleObjectBeginLock_CB(GlobalType eObjType, LTMObjectHandle objHandle, LTMObjectFieldsHandle objFieldsHandle, 
	U32 iTransactionID, const char *pTransactionName, void *pUserData)
{
	SimpleServer *pServer;
	SimpleObject *pObject;

	assert(eObjType == GLOBALTYPE_ENTITYPLAYER);
	pServer = (SimpleServer *)pUserData;
	pObject = (SimpleObject *)objHandle;

	assert(pObject->iLockingID == 0 || pObject->iLockingID == iTransactionID);

	if (pObject->iLockingID == 0)
	{
		SimpleObjectBackupData(pObject);
		pObject->iLockingID = iTransactionID;
		pObject->iLockExtraCount = 0;
	}
	else
	{
		pObject->iLockExtraCount++;
	}
}

bool SimpleObjectApplyTransaction_CB(GlobalType eObjType, LTMObjectHandle objHandle, const char *pTransactionString, 
	LTMProcessedTransactionHandle processedTransactionHandle, LTMObjectFieldsHandle objFieldsHandle, char **ppReturnValString, 
	TransDataBlock *pDatabaseUpdateData, char **ppTransServerUpdateString, TransactionID iTransID, const char *pTransactionName, void *pUserData)
{
	char resultString[256] = {0};

	SimpleServer *pServer;
	SimpleObject *pObject;

	assert(eObjType == GLOBALTYPE_ENTITYPLAYER);
	pServer = (SimpleServer *)pUserData;
	pObject = (SimpleObject *)objHandle;

	SimpleObjectDoTransaction(pObject, (DecodedSimpleObjectTransaction*)processedTransactionHandle, resultString,256);

	estrCopy2(ppReturnValString, resultString);

	return 1;
}

void SimpleObjectUndoLock_CB(GlobalType eObjType, LTMObjectHandle objHandle, LTMObjectFieldsHandle objFieldsHandle, U32 iTransactionID, 
	void *pUserData)
{
	SimpleServer *pServer;
	SimpleObject *pObject;

	assert(eObjType == GLOBALTYPE_ENTITYPLAYER);
	pServer = (SimpleServer *)pUserData;
	pObject = (SimpleObject *)objHandle;

	if (pObject->iLockExtraCount)
	{
		pObject->iLockExtraCount--;
	}
	else
	{
		SimpleObjectRestoreBackup(pObject);
		pObject->iLockingID = 0;
	}
}

void SimpleObjectCommitAndReleaseLock_CB(GlobalType eObjType, LTMObjectHandle objHandle, LTMObjectFieldsHandle objFieldsHandle, U32 iTransactionID, 
	void *pUserData)
{
	SimpleServer *pServer;
	SimpleObject *pObject;

	assert(eObjType == GLOBALTYPE_ENTITYPLAYER);
	pServer = (SimpleServer *)pUserData;
	pObject = (SimpleObject *)objHandle;

	if (pObject->iLockExtraCount)
	{
		pObject->iLockExtraCount--;
	}
	else
	{
		SimpleObjectCancelBackup(pObject);
		pObject->iLockingID = 0;
	}
}

void SimpleObjectReleaseString_CB(GlobalType eObjType, char *pReturnValString, void *pUserData)
{
	SimpleServer *pServer;
	
	assert(eObjType == GLOBALTYPE_ENTITYPLAYER);
	pServer = (SimpleServer *)pUserData;
	
	if (pReturnValString) 
	{
		estrDestroy(&pReturnValString);
	}
}

void SimpleObjectReleaseDataBlock_CB(GlobalType eObjType, TransDataBlock *pDataBlock, void *pUserData)
{
	assert(eObjType == GLOBALTYPE_ENTITYPLAYER);
	
	if (pDataBlock) 
	{
		TransDataBlockClear(pDataBlock);
	}
}


 enumTransactionValidity PreProcessSimpleObjectString_CB(GlobalType eObjType, const char *pTransactionString, 
	LTMProcessedTransactionHandle *pProcessedTransactionHandle, 
	LTMObjectFieldsHandle *pObjectFieldsHandle, char **ppReturnValString, 
	TransactionID iTransactionID, const char *pTransactionName, void *pUserData)
 {
	SimpleServer *pServer;
	DecodedSimpleObjectTransaction *pDecodedTransaction;

	assert(eObjType == GLOBALTYPE_ENTITYPLAYER);
	pServer = (SimpleServer *)pUserData;

	pDecodedTransaction = (DecodedSimpleObjectTransaction*)malloc(sizeof(DecodedSimpleObjectTransaction));

	assert(pDecodedTransaction);

	if (!SimpleObjectDecodeTranasction(pTransactionString, &pDecodedTransaction->bIncBoth, &pDecodedTransaction->bTime, &pDecodedTransaction->bPlus, &pDecodedTransaction->iAmount))
	{	
		free(pDecodedTransaction);
		estrPrintf(ppReturnValString, "Invalid transaction string");
		return TRANSACTION_INVALID;
	}
	else
	{
	

		if (pDecodedTransaction->bIncBoth)
		{
			if (pDecodedTransaction->iAmount <= 0)
			{
				free(pDecodedTransaction);

				estrPrintf(ppReturnValString, "can't incboth a nonpostive amount");
				return TRANSACTION_INVALID;
			}
	
			*pProcessedTransactionHandle = pDecodedTransaction;
			return TRANSACTION_VALID_SLOW;
		}
		*pProcessedTransactionHandle = pDecodedTransaction;
		return TRANSACTION_VALID_NORMAL;
	}
 }

void SimpleObjectReleaseProcessedTransaction_CB(GlobalType eObjType, LTMProcessedTransactionHandle transactionHandle, 
	void *pUserData)
{
	SimpleServer *pServer;
	DecodedSimpleObjectTransaction *pDecodedTransaction;

	assert(eObjType == GLOBALTYPE_ENTITYPLAYER);
	pServer = (SimpleServer *)pUserData;

	pDecodedTransaction = (DecodedSimpleObjectTransaction *)transactionHandle;

	if (pDecodedTransaction)
	{
		free(pDecodedTransaction);
	}
}

void SimpleObjectBeginSlowTransaction_CB(GlobalType eObjType, LTMObjectHandle objHandle, 
	bool bTransactionRequiresLockAndConfirm, char *pTransactionString,
	LTMProcessedTransactionHandle processedTransactionHandle, LTMObjectFieldsHandle objFieldsHandle,
	U32 iTransactionID, const char *pTransactionName, LTMSlowTransactionID slowTransactionID, void *pUserData)
{
	char resultString[256] = {0};

	SimpleServer *pServer;
	SimpleObject *pObject;

	DecodedSimpleObjectTransaction *pTransaction;

	assert(eObjType == GLOBALTYPE_ENTITYPLAYER);
	pServer = (SimpleServer *)pUserData;
	pObject = (SimpleObject *)objHandle;

	pTransaction = (DecodedSimpleObjectTransaction*)processedTransactionHandle;

	assert(pTransaction->bIncBoth);
	assert(pObject->iSlowTransID == 0);
	assert(pObject->iLockingID == 0 || pObject->iLockingID == iTransactionID);

	pObject->iSlowTransactionCount = pTransaction->iAmount;
	pObject->iSlowTransID = slowTransactionID;

	if (bTransactionRequiresLockAndConfirm)
	{
		if (pObject->iLockingID == 0)
		{
			SimpleObjectBackupData(pObject);
			pObject->iLockingID = iTransactionID;
			pObject->iLockExtraCount = 0;
		}
		else
		{
			pObject->iLockExtraCount++;
		}
	}
}

void UpdateSimpleServer(SimpleServer *pServer)
{
	int i;

	for (i=0; i < OBJECTS_PER_SERVER; i++)
	{
		SimpleObject *pObject = &pServer->objects[i];

		if (pObject->iSlowTransactionCount)
		{
			pObject->data.iMoney++;
			pObject->data.iTime++;
			pObject->iSlowTransactionCount--;

			if (pObject->iSlowTransactionCount == 0)
			{
				char resultString[256];

				char *pResultString = NULL;

				sprintf(resultString, "After incrementing both, money is %d and time is %d", pObject->data.iMoney, pObject->data.iTime);

				estrCopy2(&pResultString, resultString);

				SlowTransactionCompleted(pServer->pLocalTransactionManager, pObject->iSlowTransID, SLOWTRANSACTION_SUCCEEDED,
					pResultString, NULL, NULL);

				pObject->iSlowTransID = 0;
			}
		}
	}

	UpdateLocalTransactionManager(pServer->pLocalTransactionManager);
}



void InitSimpleServer(SimpleServer *pServer)
{
	memset(&pServer->objects, 0, sizeof(pServer->objects));	

	pServer->pLocalTransactionManager = CreateAndRegisterLocalTransactionManager(pServer, 
		&DoesSimpleObjectExist_CB, 
		&PreProcessSimpleObjectString_CB, 
		&IsSimpleObjectOKToBeLocked_CB, 
		&SimpleObjectCanTransactionBeDone_CB, 
		&SimpleObjectBeginLock_CB, 
		&SimpleObjectApplyTransaction_CB, 
		NULL, 
		&SimpleObjectUndoLock_CB, 
		&SimpleObjectCommitAndReleaseLock_CB, 
		NULL, 
		&SimpleObjectReleaseProcessedTransaction_CB, 
		&SimpleObjectReleaseString_CB, 
		&SimpleObjectBeginSlowTransaction_CB,
		&SimpleObjectReleaseDataBlock_CB,
		NULL,
		GetAppGlobalType(),gServerLibState.containerID,
		gServerLibState.transactionServerHost,
		gServerLibState.transactionServerPort,
		gServerLibState.bUseMultiplexerForTransactions, false, NULL);

	InitTransactionRequestManager();
	InitializeHandleCache();
}





		





	






