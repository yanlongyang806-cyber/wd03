/***************************************************************************
*     Copyright (c) 2013, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "dbGenericDatabaseThreads.h"
#include "dbLocalTransactionManager.h"
#include "LocalTransactionManager.h"
#include "ObjectDB.h"
#include "objTransactionCommands.h"
#include "ServerLib.h"
#include "StringCache.h"
#include "UtilitiesLib.h"
#include "wininclude.h"

#include "GlobalTypes_h_ast.h"

// This file is used by the db to change some behavior from LocalTransactionManager.c. 
// Because of this, we include the internals of LocalTransactionManager.
#include "..\LocalTransactionManager_Internal.h"

int HandshakeResultCB(Packet *pak, int cmd, NetLink *link, void *pUserData);
void HandleTransactionReturnVal(Packet *pPacket);
void HandleDBUpdateData(LocalTransactionManager *pManager, Packet *pak);
void HandleGotSendPacketSimple(LocalTransactionManager *pManager, Packet *pak);
void HandleSimplePacketError(LocalTransactionManager *pManager, Packet *pak);
void HandleContainerOwner(LocalTransactionManager *pManager, Packet *pPak);

extern bool sbSignalReplay;

typedef struct dbTransactionTimingData
{
	const char *pTransactionName;
	U64 iForegroundTicks;
	U64 iBackgroundTicks;
	U64 iQueueTicks;
	HandleNewTransOutcome eOutcome;
} dbTransactionTimingData;

void QueueAddHandleNewTransactionTimingOnMainThread(GWTCmdPacket *packet, dbTransactionTimingData *timingData)
{
	gwtQueueMsgStruct(packet, MSG_DB_TRANSACTION_TIMING, *timingData, dbTransactionTimingData);
}

void dbAddHandleNewTransactionTimingCB(dbTransactionTimingData *timingData)
{
	AddHandleNewTransactionThreadTiming(timingData->pTransactionName, timingData->iForegroundTicks, timingData->iBackgroundTicks, timingData->iQueueTicks, timingData->eOutcome);
}

void dbAddHandleNewTransactionTimingThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	dbTransactionTimingData *timingData = (dbTransactionTimingData*)data;
	dbAddHandleNewTransactionTimingCB(timingData);
}

void dbAddHandleNewTransactionTiming(GWTCmdPacket *packet, const char *pTransName, U64 iForegroundTicks, U64 iBackgroundTicks, U64 iQueueTicks, HandleNewTransOutcome eOutcome)
{
	if(packet)
	{
		dbTransactionTimingData timingData = {0};
		timingData.pTransactionName = pTransName;
		timingData.iForegroundTicks = iForegroundTicks;
		timingData.iBackgroundTicks = iBackgroundTicks;
		timingData.iQueueTicks = iQueueTicks;
		timingData.eOutcome = eOutcome;
		QueueAddHandleNewTransactionTimingOnMainThread(packet, &timingData);
	}
	else
	{
		AddHandleNewTransactionThreadTiming(pTransName, iForegroundTicks, iBackgroundTicks, iQueueTicks, eOutcome);
	}
}

static void dbAddHandleNewTransactionTimingBG(dbHandleNewTransaction_Data *data, GWTCmdPacket *packet, U64 iStartingTicks, HandleNewTransOutcome eOutcome)
{
	U64 iEndingTicks;
	GET_CPU_TICKS_64(iEndingTicks);
	if(packet)
	{
		data->iBackgroundTicks = iEndingTicks - iStartingTicks;
	}
	else
	{
		data->iForegroundTicks += iEndingTicks - iStartingTicks;
	}
	dbAddHandleNewTransactionTiming(packet, data->pTransactionName, data->iForegroundTicks, data->iBackgroundTicks, data->iQueueTicks, eOutcome);
}


void dbHandleNewTransactionCB(GWTCmdPacket *packet, dbHandleNewTransaction_Data *data)
{
	U64 iStartingTicks;
	char **ppReturnString;
	TransactionID iTransIDCausingBlock = 0;
	// This must always return data unless registering the TLS data fails
	LocalTransactionManager *pBackgroundManager = objLocalManager();

	PERFINFO_AUTO_START_FUNC();
	GET_CPU_TICKS_64(iStartingTicks);

	if(packet)
	{
		data->iQueueTicks = iStartingTicks - data->iQueueStartTicks;
	}

	pBackgroundManager->iCurrentlyActiveTransaction = data->iTransID;
	strcpy(pBackgroundManager->currentTransactionName, data->pTransactionName);
	pBackgroundManager->transVariableTable = data->transVariableTable;
	data->transVariableTable = NULL;

	ppReturnString = data->bWantsReturn ? &data->pReturnString : NULL;	

	if (!pBackgroundManager->pDoesObjectExistCB(data->eRecipientType, data->iRecipientID, &data->objHandle, ppReturnString, pBackgroundManager->pCBUserData))
	{
		SimpleMessageToServer(pBackgroundManager, TRANSCLIENT_TRANSACTIONFAILED, data->iTransID, data->iTransIndex, 0, data->pReturnString, NULL, NULL);

		ReleaseEverything(pBackgroundManager, data->eRecipientType, data->objFieldsHandle, data->processedTransactionHandle, &data->pReturnString, &data->pTransactionString);
		ReleaseHandleCache(&data->pHandleCache);
		dbAddHandleNewTransactionTimingBG(data, packet, iStartingTicks, OUTCOME_OBJ_DOESNT_EXIST);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (!pBackgroundManager->pAreFieldsOKToBeLockedCB(data->eRecipientType, data->objHandle, data->pTransactionString, data->objFieldsHandle, data->iTransID,  data->pTransactionName, pBackgroundManager->pCBUserData, &iTransIDCausingBlock))
	{
		SimpleMessageToServer(pBackgroundManager, TRANSCLIENT_TRANSACTIONBLOCKED, data->iTransID, data->iTransIndex, iTransIDCausingBlock, NULL, NULL, NULL);

		ReleaseEverything(pBackgroundManager, data->eRecipientType, data->objFieldsHandle, data->processedTransactionHandle, &data->pReturnString, &data->pTransactionString);
		ReleaseHandleCache(&data->pHandleCache);
		dbAddHandleNewTransactionTimingBG(data, packet, iStartingTicks, OUTCOME_FIELDS_LOCKED);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (data->bRequiresConfirm)
	{
		if (pBackgroundManager->pCanTransactionBeDoneCB(data->eRecipientType, data->objHandle, data->pTransactionString, data->processedTransactionHandle, data->objFieldsHandle,
			ppReturnString, data->iTransID,  data->pTransactionName, pBackgroundManager->pCBUserData))
		{
			TransDataBlock dbUpdateData = {0};
			char *pTransServerUpdateString = NULL;

			pBackgroundManager->pBeginLockCB(data->eRecipientType, data->objHandle, data->objFieldsHandle, data->iTransID, data->pTransactionName, pBackgroundManager->pCBUserData);

			if (pBackgroundManager->pApplyTransactionCB(data->eRecipientType, data->objHandle, data->pTransactionString, data->processedTransactionHandle, data->objFieldsHandle,
				ppReturnString, &dbUpdateData, &pTransServerUpdateString, data->iTransID, data->pTransactionName, pBackgroundManager->pCBUserData))
			{
				//for atomic transactions with only one step, we can do the succeed and confirm all at once
				if (data->bSucceedAndConfirmIsOK)
				{
					SimpleMessageToServer(pBackgroundManager, TRANSCLIENT_TRANSACTIONPOSSIBLEANDCONFIRMED, data->iTransID, data->iTransIndex,
						0, data->pReturnString, &dbUpdateData, pTransServerUpdateString);

					pBackgroundManager->pCommitAndReleaseLockCB(data->eRecipientType, data->objHandle, data->objFieldsHandle, data->iTransID, pBackgroundManager->pCBUserData);

					ReleaseEverything(pBackgroundManager, data->eRecipientType, data->objFieldsHandle, data->processedTransactionHandle, &data->pReturnString, &data->pTransactionString);
					ReleaseHandleCache(&data->pHandleCache);

					if (pBackgroundManager->pReleaseStringCB)
					{
						pBackgroundManager->pReleaseStringCB(data->eRecipientType, pTransServerUpdateString, pBackgroundManager->pCBUserData);
					}

					if (pBackgroundManager->pReleaseDataBlockCB)
					{
						pBackgroundManager->pReleaseDataBlockCB(data->eRecipientType, &dbUpdateData, pBackgroundManager->pCBUserData);
					}
	
				}
				else
				{
					// data->pHandleCache must already be filled in
					assertmsgf(data->pHandleCache, "Something went wrong. No Free handle caches. This case should already have been caught and TRANS_BLOCKED returned. Talk to Alex.");

					data->pHandleCache->objFieldsHandle = data->objFieldsHandle;
					data->pHandleCache->objHandle = data->objHandle;

					SimpleMessageToServer(pBackgroundManager, TRANSCLIENT_TRANSACTIONPOSSIBLE, data->iTransID, data->iTransIndex, data->pHandleCache->iID, data->pReturnString,
						&dbUpdateData, pTransServerUpdateString);


					//NOT a release everything, because the obj fields handle is kept to make releasing blocks fast
					if (pBackgroundManager->pReleaseStringCB)
					{
						pBackgroundManager->pReleaseStringCB(data->eRecipientType, data->pReturnString, pBackgroundManager->pCBUserData);
						pBackgroundManager->pReleaseStringCB(data->eRecipientType, pTransServerUpdateString, pBackgroundManager->pCBUserData);
					}

					if (pBackgroundManager->pReleaseDataBlockCB)
					{
						pBackgroundManager->pReleaseDataBlockCB(data->eRecipientType, &dbUpdateData, pBackgroundManager->pCBUserData);
					}

					data->pReturnString = NULL;

					if (pBackgroundManager->pReleaseProcessedTransactionHandleCB)
					{
						pBackgroundManager->pReleaseProcessedTransactionHandleCB(data->eRecipientType, data->processedTransactionHandle, pBackgroundManager->pCBUserData);
					}

					ReleaseAllTransVariables(pBackgroundManager);
					pBackgroundManager->iCurrentlyActiveTransaction = 0;
					estrDestroy(&data->pTransactionString);
				}
			}
			else
			{
				assertmsg(TransDataBlockIsEmpty(&dbUpdateData) && 
					(!pTransServerUpdateString || !pTransServerUpdateString[0]), 
					"Update strings can not be generated when a transaction fails");

				pBackgroundManager->pUndoLockCB(data->eRecipientType, data->objHandle, data->objFieldsHandle, data->iTransID, pBackgroundManager->pCBUserData);

				SimpleMessageToServer(pBackgroundManager, TRANSCLIENT_TRANSACTIONFAILED, data->iTransID, data->iTransIndex, 0, data->pReturnString, NULL, NULL);

				ReleaseEverything(pBackgroundManager, data->eRecipientType, data->objFieldsHandle, data->processedTransactionHandle, &data->pReturnString, &data->pTransactionString);
				ReleaseHandleCache(&data->pHandleCache);
			}

		}
		else
		{
			SimpleMessageToServer(pBackgroundManager, TRANSCLIENT_TRANSACTIONFAILED, data->iTransID, data->iTransIndex, 0, data->pReturnString, NULL, NULL);

			ReleaseEverything(pBackgroundManager, data->eRecipientType, data->objFieldsHandle, data->processedTransactionHandle, &data->pReturnString, &data->pTransactionString);
			ReleaseHandleCache(&data->pHandleCache);
		}
	}
	else
	{
		bool bCanDoTransaction;
		TransDataBlock dbUpdateData = {0};
		char *pTransServerUpdateString = NULL;

		if (pBackgroundManager->pApplyTransactionIfPossibleCB)
		{
			bCanDoTransaction = pBackgroundManager->pApplyTransactionIfPossibleCB(data->eRecipientType, data->objHandle, data->pTransactionString,
				data->processedTransactionHandle, data->objFieldsHandle, ppReturnString, &dbUpdateData, &pTransServerUpdateString,
				data->iTransID, data->pTransactionName, pBackgroundManager->pCBUserData);
		}
		else
		{
			bCanDoTransaction = pBackgroundManager->pCanTransactionBeDoneCB(data->eRecipientType, data->objHandle, data->pTransactionString,
				data->processedTransactionHandle, data->objFieldsHandle, ppReturnString, data->iTransID, data->pTransactionName, pBackgroundManager->pCBUserData);

			if (bCanDoTransaction)
			{
				bCanDoTransaction = pBackgroundManager->pApplyTransactionCB(data->eRecipientType, data->objHandle, data->pTransactionString, data->processedTransactionHandle, data->objFieldsHandle,
					ppReturnString,  &dbUpdateData, &pTransServerUpdateString, data->iTransID, data->pTransactionName, pBackgroundManager->pCBUserData);

				if (!bCanDoTransaction)
				{
					assertmsg(TransDataBlockIsEmpty(&dbUpdateData) && 
						(!pTransServerUpdateString || !pTransServerUpdateString[0]), 
						"Update strings can not be generated when a transaction fails");
				}

			}
		}

		if (bCanDoTransaction)
		{
			SimpleMessageToServer(pBackgroundManager, TRANSCLIENT_TRANSACTIONSUCCEEDED, data->iTransID, data->iTransIndex, 0, data->pReturnString,
				&dbUpdateData, pTransServerUpdateString);
		}
		else
		{
			SimpleMessageToServer(pBackgroundManager, TRANSCLIENT_TRANSACTIONFAILED, data->iTransID, data->iTransIndex, 0, data->pReturnString, NULL, NULL);
		}

		ReleaseEverything(pBackgroundManager, data->eRecipientType, data->objFieldsHandle, data->processedTransactionHandle, &data->pReturnString, &data->pTransactionString);

		if (pBackgroundManager->pReleaseStringCB)
		{
			pBackgroundManager->pReleaseStringCB(data->eRecipientType, pTransServerUpdateString, pBackgroundManager->pCBUserData);
		}

		if (pBackgroundManager->pReleaseDataBlockCB)
		{
			pBackgroundManager->pReleaseDataBlockCB(data->eRecipientType, &dbUpdateData, pBackgroundManager->pCBUserData);
		}
	}

	dbAddHandleNewTransactionTimingBG(data, packet, iStartingTicks, OUTCOME_COMPLETED);
	PERFINFO_AUTO_STOP();
}

static void dbAddHandleNewTransactionTimingFG(dbHandleNewTransaction_Data *data, U64 iStartingTicks, HandleNewTransOutcome eOutcome)
{
	U64 iEndingTicks;
	GET_CPU_TICKS_64(iEndingTicks);
	data->iForegroundTicks = iEndingTicks - iStartingTicks;
	dbAddHandleNewTransactionTiming(NULL, data->pTransactionName, data->iForegroundTicks, data->iBackgroundTicks, data->iQueueTicks, eOutcome);
}

// This should only ever be called from the main thread
void dbHandleNewTransaction(LocalTransactionManager *pManager, Packet *pPacket)
{
	TransactionCommand *command = NULL;
	char **ppReturnString = NULL;
	dbHandleNewTransaction_Data data = {0};
	U64 iStartingTicks;
	U64 iEndingTicks;
	PERFINFO_AUTO_START("HandleNewTransaction",1);
	assert(pManager->iThreadID == GetCurrentThreadId());
	GET_CPU_TICKS_64(iStartingTicks);
	{
	data.pTransactionString = NULL;
	data.pReturnString = NULL;

	data.bWantsReturn = pktGetBits(pPacket, 1);
	data.bRequiresConfirm = pktGetBits(pPacket, 1);

	//only applicable if bRequiresConfirm is set (ie, this is an atomic transaction). This means that
	//there is only one base transaction and no possibility of unrolling, so if you succeed you can confirm
	//immediately.
	data.bSucceedAndConfirmIsOK = pktGetBits(pPacket, 1);

	data.iTransID = pManager->iCurrentlyActiveTransaction = GetTransactionIDFromPacket(pPacket);
	data.pTransactionName = allocAddString(pktGetStringTemp(pPacket));

	data.iTransIndex = pktGetBitsPack(pPacket, 1);
	data.eRecipientType = GetContainerTypeFromPacket(pPacket);

	data.iRecipientID = GetContainerIDFromPacket(pPacket);

	if (data.bRequiresConfirm)
	{
		data.pHandleCache = AcquireNewHandleCache(data.eRecipientType, data.pTransactionName);
		if (!data.pHandleCache)
		{
			// Don't need to report HandleCacheOverflow here because it happens in AcquireNewHandleCache
			SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONBLOCKED, data.iTransID, data.iTransIndex, 0, NULL, NULL, NULL);
			dbAddHandleNewTransactionTimingFG(&data, iStartingTicks, OUTCOME_HANDLE_CACHE_FULL);
			
			PERFINFO_AUTO_STOP();
			return;
		}
	}


	pManager->bDoingRemoteTransaction = true;
	strcpy(pManager->currentTransactionName, data.pTransactionName);

	if (LTMLoggingEnabled())
	{
		LTM_LOG("Got HandleNewTransaction, trans ID %u (%s)\n", data.iTransID, data.pTransactionName);
	}

	estrAppendFromPacket(&data.pTransactionString,pPacket);

	if (pktGetBits(pPacket, 1))
	{
		data.transVariableTable = CreateNameTable(pPacket);
		pManager->transVariableTable = data.transVariableTable;
	}

	pManager->averageBaseTransactionSize = (pManager->averageBaseTransactionSize * pManager->totalBaseTransactions + pktGetSize(pPacket)) / (pManager->totalBaseTransactions + 1);
	pManager->totalBaseTransactions++;

	ppReturnString = data.bWantsReturn ? &data.pReturnString : NULL;

	if (pManager->pPreProcessTransactionStringCB)
	{
		enumTransactionValidity eValidity = pManager->pPreProcessTransactionStringCB(data.eRecipientType, data.pTransactionString, &data.processedTransactionHandle, &data.objFieldsHandle,
			ppReturnString, data.iTransID, data.pTransactionName, pManager->pCBUserData);

		switch (eValidity)
		{
		case TRANSACTION_INVALID:
			SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONFAILED, data.iTransID, data.iTransIndex, 0, data.pReturnString, NULL, NULL);

			ReleaseEverything(pManager, data.eRecipientType, data.objFieldsHandle, data.processedTransactionHandle, &data.pReturnString, &data.pTransactionString);
			ReleaseHandleCache(&data.pHandleCache);
			dbAddHandleNewTransactionTimingFG(&data, iStartingTicks, OUTCOME_TRANS_INVALID);
			PERFINFO_AUTO_STOP();
			return;

		case TRANSACTION_VALID_SLOW:
			data.bTransactionIsSlow = true;
			break;
		}
	}

	if (data.bTransactionIsSlow)
	{
		TransactionID iTransIDCausingBlock = 0;
		SlowTransactionInfo *pSlowTransInfo;
		//Do locks and finish this up here
		if (!pManager->pDoesObjectExistCB(data.eRecipientType, data.iRecipientID, &data.objHandle, ppReturnString, pManager->pCBUserData))
		{
			SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONFAILED, data.iTransID, data.iTransIndex, 0, data.pReturnString, NULL, NULL);

			ReleaseEverything(pManager, data.eRecipientType, data.objFieldsHandle, data.processedTransactionHandle, &data.pReturnString, &data.pTransactionString);
			ReleaseHandleCache(&data.pHandleCache);
			dbAddHandleNewTransactionTimingFG(&data, iStartingTicks, OUTCOME_OBJ_DOESNT_EXIST);
			PERFINFO_AUTO_STOP();
			return;
		}

		if (!pManager->pAreFieldsOKToBeLockedCB(data.eRecipientType, data.objHandle, data.pTransactionString, data.objFieldsHandle, data.iTransID,  data.pTransactionName, pManager->pCBUserData, &iTransIDCausingBlock))
		{
			SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONBLOCKED, data.iTransID, data.iTransIndex, iTransIDCausingBlock, NULL, NULL, NULL);

			ReleaseEverything(pManager, data.eRecipientType, data.objFieldsHandle, data.processedTransactionHandle, &data.pReturnString, &data.pTransactionString);
			ReleaseHandleCache(&data.pHandleCache);
			dbAddHandleNewTransactionTimingFG(&data, iStartingTicks, OUTCOME_FIELDS_LOCKED);
			PERFINFO_AUTO_STOP();
			return;
		}

		pSlowTransInfo = GetEmptySlowTransactionInfo(pManager);

		if (!pSlowTransInfo)
		{
			AssertOrAlert("TOO_MANY_SLOW_TRANS", "Too many slow transactions... blocking a %s transaction. This is going to clobber performance",
				data.pTransactionName);

			SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONBLOCKED, data.iTransID, data.iTransIndex, 0, NULL, NULL, NULL);

			ReleaseEverything(pManager, data.eRecipientType, data.objFieldsHandle, data.processedTransactionHandle, &data.pReturnString, &data.pTransactionString);
			ReleaseHandleCache(&data.pHandleCache);
			dbAddHandleNewTransactionTimingFG(&data, iStartingTicks, OUTCOME_TOO_MANY_SLOW);
			PERFINFO_AUTO_STOP();
			return;
		}


		if (data.bRequiresConfirm)
		{
			// data.pHandleCache should have been filled in at the beginnning of the function
			assertmsgf(data.pHandleCache, "Something went wrong. No Free handle caches. This case should already have been caught and TRANS_BLOCKED returned. Talk to Alex.");

			data.pHandleCache->objFieldsHandle = data.objFieldsHandle;
			data.pHandleCache->objHandle = data.objHandle;
		}

		pSlowTransInfo->bRequiresConfirm = data.bRequiresConfirm;
		pSlowTransInfo->bSucceedAndConfirmIsOK = data.bSucceedAndConfirmIsOK;
		pSlowTransInfo->iHandleCacheID = data.bRequiresConfirm ? data.pHandleCache->iID : -1;
		pSlowTransInfo->iTransID = data.iTransID;
		pSlowTransInfo->pTransactionName = data.pTransactionName;
		pSlowTransInfo->iTransIndex = data.iTransIndex;
		pSlowTransInfo->eObjType = data.eRecipientType;

		pSlowTransInfo->objFieldsHandle = data.objFieldsHandle;
		pSlowTransInfo->processedTransHandle = data.processedTransactionHandle;

		pSlowTransInfo->transVariableTable = data.transVariableTable; 

		strcpy_trunc(pSlowTransInfo->dbgTransString, data.pTransactionString);

		data.transVariableTable = NULL;
		pManager->transVariableTable = NULL;

		pManager->iCurrentlyActiveTransaction = 0;

		assert(pManager->pBeginSlowTransactionCB);

		pManager->pBeginSlowTransactionCB(data.eRecipientType, data.objHandle, data.bRequiresConfirm, data.pTransactionString,
			data.processedTransactionHandle, data.objFieldsHandle, data.iTransID, data.pTransactionName, pSlowTransInfo->iID, pManager->pCBUserData);


		if (pManager->pReleaseStringCB)
		{
			pManager->pReleaseStringCB(data.eRecipientType, data.pReturnString, pManager->pCBUserData);
		}
		data.pReturnString = NULL;

		estrDestroy(&data.pTransactionString);
		PERFINFO_AUTO_STOP();
		return;
	}

	command = (TransactionCommand*)data.objFieldsHandle;

	// At this point, we have pulled all data from the packet and need to send this to the background thread
	pManager->transVariableTable = NULL;
	
	GET_CPU_TICKS_64(iEndingTicks);
	data.iForegroundTicks = iEndingTicks - iStartingTicks;
	if(command->bRemoteCommand)
	{
		// Perform remote commands in the main thread
		dbHandleNewTransactionCB(NULL, &data);
	}
	else
	{
		data.iQueueStartTicks = iEndingTicks;
		if(data.eRecipientType == objServerType())
		{
			QueueDBHandleNewTransactionOnGenericDatabaseThreads(&data, command->objectType, command->objectID);
		}
		else
		{
			QueueDBHandleNewTransactionOnGenericDatabaseThreads(&data, data.eRecipientType, data.iRecipientID);
		}
	}

	pManager->bDoingRemoteTransaction = false;
	PERFINFO_AUTO_STOP();
	}
}

void dbHandleCancelTransactionCB(dbHandleCancelTransaction_Data *data)
{
	// This must always return data unless registering the TLS data fails
	LocalTransactionManager *pBackgroundManager = objLocalManager();

	PERFINFO_AUTO_START_FUNC();

	pBackgroundManager->bDoingRemoteTransaction = true;
	strcpy(pBackgroundManager->currentTransactionName, "CANCELLING");

	if (LTMLoggingEnabled())
	{
		LTM_LOG("Got HandleCancelTransaction, trans ID %u\n", data->iTransID);
	}

	SimpleMessageToServer(pBackgroundManager, TRANSCLIENT_TRANSACTIONCANCELCONFIRMED, data->iTransID, data->iTransIndex, 0, NULL, NULL, NULL);

	pBackgroundManager->pUndoLockCB(data->eRecipientType, data->pHandleCache->objHandle, data->pHandleCache->objFieldsHandle, data->iTransID, pBackgroundManager->pCBUserData);

	if (pBackgroundManager->pReleaseObjectFieldsHandleCB)
	{
		pBackgroundManager->pReleaseObjectFieldsHandleCB(data->eRecipientType, data->pHandleCache->objFieldsHandle, pBackgroundManager->pCBUserData);
	}
	
	pBackgroundManager->bDoingRemoteTransaction = false;

	ReleaseHandleCache(&data->pHandleCache);

	PERFINFO_AUTO_STOP();
}

void dbHandleCancelTransaction(LocalTransactionManager *pManager, Packet *pPacket)
{
	dbHandleCancelTransaction_Data data = {0};
	TransactionCommand *command;
	PERFINFO_AUTO_START("HandleCancelTransaction",1);
	assert(pManager->iThreadID == GetCurrentThreadId());
	{	
	int iHandleCacheID;
	data.iTransID = GetTransactionIDFromPacket(pPacket);
	data.iTransIndex = pktGetBitsPack(pPacket, 1);
	data.eRecipientType = GetContainerTypeFromPacket(pPacket);
	data.iRecipientID = GetContainerIDFromPacket(pPacket);

	iHandleCacheID = pktGetBits(pPacket, 32);
	data.pHandleCache = FindExistingHandleCache(iHandleCacheID);

	command = (TransactionCommand*)data.pHandleCache->objFieldsHandle;
	assert(command);

	QueueDBHandleCancelTransactionOnGenericDatabaseThreads(&data, command->objectType, command->objectID);
	}

	PERFINFO_AUTO_STOP();
}

void dbHandleConfirmTransactionCB(dbHandleConfirmTransaction_Data *data)
{
	LocalTransactionManager *pBackgroundManager = objLocalManager();

	PERFINFO_AUTO_START_FUNC();

	pBackgroundManager->bDoingRemoteTransaction = true;
	strcpy(pBackgroundManager->currentTransactionName, "CONFIRMING");

	if (LTMLoggingEnabled())
	{
		LTM_LOG("Got HandleConfirmTransaction, trans ID %u\n", data->iTransID);
	}

	SimpleMessageToServer(pBackgroundManager, TRANSCLIENT_TRANSACTIONSUCCEEDED, data->iTransID, data->iTransIndex, 0, NULL, NULL, NULL);

	pBackgroundManager->pCommitAndReleaseLockCB(data->eRecipientType, data->pHandleCache->objHandle, data->pHandleCache->objFieldsHandle, data->iTransID, pBackgroundManager->pCBUserData);

	if (pBackgroundManager->pReleaseObjectFieldsHandleCB)
	{
		pBackgroundManager->pReleaseObjectFieldsHandleCB(data->eRecipientType, data->pHandleCache->objFieldsHandle, pBackgroundManager->pCBUserData);
	}
	pBackgroundManager->bDoingRemoteTransaction = false;
	ReleaseHandleCache(&data->pHandleCache);
	PERFINFO_AUTO_STOP_FUNC();
}

void dbHandleConfirmTransaction(LocalTransactionManager *pManager, Packet *pPacket)
{
	TransactionCommand *command;
	dbHandleConfirmTransaction_Data data = {0};
	PERFINFO_AUTO_START("HandleConfirmTransaction",1);
	assert(pManager->iThreadID == GetCurrentThreadId());
	{	
	int iHandleCacheID;
	data.iTransID = GetTransactionIDFromPacket(pPacket);
	data.iTransIndex = pktGetBitsPack(pPacket, 1);
	data.eRecipientType = GetContainerTypeFromPacket(pPacket);
	data.iRecipientID = GetContainerIDFromPacket(pPacket);

	iHandleCacheID = pktGetBits(pPacket, 32);

	data.pHandleCache = FindExistingHandleCache(iHandleCacheID);
	command = (TransactionCommand*)data.pHandleCache->objFieldsHandle;

	QueueDBHandleConfirmTransactionOnGenericDatabaseThreads(&data, command->objectType, command->objectID);
	}
	PERFINFO_AUTO_STOP();
}

void ObjectDBThreadedLocalTransactionManagerLinkCallback(Packet *pak, int cmd, NetLink *link, void *pUserData)
{
	LocalTransactionManager *pManager = pUserData;
	PERFINFO_AUTO_START("ObjectDBThreadedLocalTransactionManagerLinkCallback",1);
	assert(pManager->iThreadID == GetCurrentThreadId());

	switch(cmd)
	{
		xcase TRANSSERVER_CONNECTION_RESULT:
			HandshakeResultCB(pak,cmd,link,pUserData); // Does not need threading
		xcase TRANSSERVER_REQUEST_SINGLE_TRANSACTION:
			dbHandleNewTransaction(pManager, pak); //Threaded
		xcase TRANSSERVER_CANCEL_TRANSACTION:
			dbHandleCancelTransaction(pManager, pak); //Threaded
		xcase TRANSSERVER_CONFIRM_TRANSACTION:
			dbHandleConfirmTransaction(pManager, pak); //Threaded
		xcase TRANSSERVER_TRANSACTION_COMPLETE:
			HandleTransactionReturnVal(pak); // Does not need threading
		xcase TRANSSERVER_TRANSACTION_DBUPDATE:
			HandleDBUpdateData(pManager, pak); //Internal callbacks are threaded
		xcase TRANSSERVER_SEND_PACKET_SIMPLE:
			HandleGotSendPacketSimple(pManager, pak); // Does not need threading
		xcase TRANSSERVER_SIMPLE_PACKET_ERROR:
			HandleSimplePacketError(pManager, pak); // Does not need threading
		xcase TRANSSERVER_HERE_IS_OWNER:
			HandleContainerOwner(pManager, pak); // Does not need threading


		xdefault:
			printf("Unknown command %d\n",cmd);
	}
	PERFINFO_AUTO_STOP();

	// Request the next packet.
	if (sbSignalReplay)
	{
		static HANDLE sGotPacket = NULL;
		if (!sGotPacket)
		{
			sGotPacket = OpenEvent(EVENT_ALL_ACCESS, false, LTM_DEBUG_REPLAY_SIGNAL_NAME);
			assert(sGotPacket);
		}
		SetEvent(sGotPacket);
	}
}

