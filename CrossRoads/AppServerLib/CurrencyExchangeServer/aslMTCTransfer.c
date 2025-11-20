/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslMTCTransfer.h"
#include "aslCurrencyExchange.h"
#include "AppServerLib.h"
#include "textparser.h"
#include "earray.h"
#include "objTransactions.h"
#include "accountnet.h"
#include "AccountProxyCommon.h"
#include "logging.h"

#include "AutoGen/aslMTCTransfer_h_ast.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"

static EARRAY_OF(CurrencyExchangeMTCTransferData) sMTCPendingTransfers = NULL;
static INT_EARRAY sMTCAccountsActive = NULL;

static U32 sNextTransferID = 1;

extern bool gUseKeyValueMove;

// Fill in info for server monitor overview struct.
void
MTCTransfer_GetOverviewInfo(CurrencyExchangeOverview *overview)
{
    overview->numMTCTransfersPending = eaSize(&sMTCPendingTransfers);
    overview->numMTCTransferLockedAccounts = ea32Size(&sMTCAccountsActive);
}

static void
MTCTransferLog(const char *tag, CurrencyExchangeMTCTransferData *transferData)
{
    aslCurrencyExchange_Log("MTCTransfer:%s:transferID=%u, quantity=%u, srcAccount=%u, srcKey=%s, srcLock=%u, destAcct=%u, destKey=%s, destLock=%u, retryCount=%u, retryTime=%u", 
        tag, transferData->transferID, transferData->quantity, transferData->sourceAccountID, transferData->sourceKey, transferData->sourceLock, 
        transferData->destinationAccountID, transferData->destinationKey, transferData->destinationLock, transferData->retryCount, transferData->retryTime);
}

static bool
MTCTransferIsAccountActive(ContainerID accountID)
{
    int index;
    return ea32SortedFindIntOrPlace(&sMTCAccountsActive, accountID, &index);
}

static void
MTCTransferMarkAccountActive(ContainerID accountID)
{
    int index;
    bool found = ea32SortedFindIntOrPlace(&sMTCAccountsActive, accountID, &index);

    devassert(!found);
    ea32Insert(&sMTCAccountsActive, accountID, index);
}

static bool
MTCTransferAccountsActive(CurrencyExchangeMTCTransferData *transferData)
{
    return ( MTCTransferIsAccountActive(transferData->sourceAccountID) || MTCTransferIsAccountActive(transferData->destinationAccountID) );
}

static void
MTCTransferMarkAccountsActive(CurrencyExchangeMTCTransferData *transferData)
{
    devassert(!MTCTransferAccountsActive(transferData));

    // Mark the source account as active.
    MTCTransferMarkAccountActive(transferData->sourceAccountID);

    // If the source and destination are separate accounts, then also mark the destination.
    if ( transferData->sourceAccountID != transferData->destinationAccountID )
    {
        MTCTransferMarkAccountActive(transferData->destinationAccountID);
    }
}

static void
MTCTransferMarkAccountNotActive(ContainerID accountID)
{
    int index;
    bool found = ea32SortedFindIntOrPlace(&sMTCAccountsActive, accountID, &index);

    devassert(found);
    ea32Remove(&sMTCAccountsActive, index);
}

static void
MTCTransferMarkAccountsNotActive(CurrencyExchangeMTCTransferData *transferData)
{
    devassert(MTCTransferIsAccountActive(transferData->sourceAccountID) && MTCTransferIsAccountActive(transferData->destinationAccountID));

    // Mark the source account as not active.
    MTCTransferMarkAccountNotActive(transferData->sourceAccountID);

    // If the source and destination are separate accounts, then also mark the destination.
    if ( transferData->sourceAccountID != transferData->destinationAccountID )
    {
        MTCTransferMarkAccountNotActive(transferData->destinationAccountID);
    }
}

//
// This is called when the transfer is completely done.  It marks the accounts as not active and frees the transfer data.
//
static void
MTCTransferDone(CurrencyExchangeMTCTransferData *transferData)
{
    MTCTransferLog("Done", transferData);

    // Mark the accounts as not active.
    MTCTransferMarkAccountsNotActive(transferData);

    // Remove from pending transfers.
    eaFindAndRemove(&sMTCPendingTransfers, transferData);

    // Free the transfer data.
    StructDestroy(parse_CurrencyExchangeMTCTransferData, transferData);
}

static void
MTCTransferLocksFail(CurrencyExchangeMTCTransferData *transferData, CurrencyExchangeResultType resultType, char *errorDetailString)
{
    MTCTransferLog("LocksFail", transferData);

    if ( transferData->callback != NULL )
    {
        transferData->callback(resultType, 0, 0, errorDetailString, transferData->userData);
    }

    MTCTransferDone(transferData);
}

static void
MTCTransferScheduleRetry(CurrencyExchangeMTCTransferData *transferData)
{
    U32 curTime = timeSecondsSince2000();

    // Update retry time and count.
    transferData->retryTime = curTime + ( 1 << transferData->retryCount );
    transferData->retryCount++;

    // If we have already retried too many time, then generate an error.
    if ( transferData->retryCount > gCurrencyExchangeConfig.maxMTCLockRetryCount )
    {
        MTCTransferLocksFail(transferData, CurrencyResultType_InternalError, "Too many lock retries.");
        return;
    }

    MTCTransferLog("Retry", transferData);

    // Mark the accounts as not active.
    MTCTransferMarkAccountsNotActive(transferData);
}

static void
MTCTransferLocksFinish(CurrencyExchangeMTCTransferData *transferData)
{
    MTCTransferLog("LocksFinish", transferData);

    if ( transferData->callback != NULL )
    {
        transferData->callback(CurrencyResultType_Success, transferData->sourceLock, transferData->destinationLock, NULL, transferData->userData);
    }
}

static void
MTCTransferPhaseOneRollbackCB(TransactionReturnVal *returnVal, CurrencyExchangeMTCTransferData *transferData)
{
    if ( returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
    {
        MTCTransferScheduleRetry(transferData);
    }
    else
    {
        MTCTransferLocksFail(transferData, CurrencyResultType_InternalError, "Failed MTC transfer lock phase two.  Release of phase one lock failed.");
    }
}

static void
MTCTransferMoveCB(AccountKeyValueResult result, ContainerID containerID, SA_PARAM_OP_VALID CurrencyExchangeMTCTransferData *transferData)
{
	if ( result == AKV_SUCCESS )
	{
		transferData->sourceLock = containerID;
		MTCTransferLocksFinish(transferData);
	}
	else
	{
		if ( result == AKV_INVALID_RANGE )
		{
			MTCTransferLocksFail(transferData, CurrencyResultType_NotEnoughMTC, "Not enough MTC to transfer.");
		}
		else
		{
			if ( result != AKV_LOCKED )
			{
				// Start a check on the account server for failure other than INVALID_RANGE or LOCKED.
				CurrencyExchange_CheckAccountServerAvailable();
			}
			MTCTransferScheduleRetry(transferData);
		}
	}
}

static void
MTCTransferPhaseTwoCB(AccountKeyValueResult result, U32 accountID, SA_PARAM_NN_STR const char *key, ContainerID containerID, SA_PARAM_OP_VALID CurrencyExchangeMTCTransferData *transferData)
{
    devassert(accountID == transferData->destinationAccountID);

    if ( result == AKV_SUCCESS )
    {
        transferData->destinationLock = containerID;
        MTCTransferLocksFinish(transferData);
    }
    else
    {
        // phase two lock failed, so we need to roll back the phase one lock.
        TransactionReturnVal *returnVal = objCreateManagedReturnVal(MTCTransferPhaseOneRollbackCB, transferData);

        if ( result != AKV_LOCKED )
        {
            // Start a check on the account server for failure other than LOCKED.
            CurrencyExchange_CheckAccountServerAvailable();
        }

        AutoTrans_AccountProxy_tr_RollbackLock(returnVal, GLOBALTYPE_ACCOUNTPROXYSERVER, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, 
            transferData->sourceLock, transferData->sourceAccountID, transferData->sourceKey);
    }
}

static void
MTCTransferPhaseOneCB(AccountKeyValueResult result, U32 accountID, SA_PARAM_NN_STR const char *key, ContainerID containerID, SA_PARAM_OP_VALID CurrencyExchangeMTCTransferData *transferData)
{
    devassert(accountID == transferData->sourceAccountID);

    if ( result == AKV_SUCCESS )
    {
        transferData->sourceLock = containerID;

		APChangeKeyValue(transferData->destinationAccountID, transferData->destinationKey, transferData->quantity, MTCTransferPhaseTwoCB, transferData);
    }
    else
    {
        if ( result == AKV_INVALID_RANGE )
        {
            MTCTransferLocksFail(transferData, CurrencyResultType_NotEnoughMTC, "Not enough MTC to transfer.");
        }
        else
        {
            if ( result != AKV_LOCKED )
            {
                // Start a check on the account server for failure other than INVALID_RANGE or LOCKED.
                CurrencyExchange_CheckAccountServerAvailable();
            }
            MTCTransferScheduleRetry(transferData);
        }
    }
}

static void
MTCTransferCommitFailRollbackDestCB(TransactionReturnVal *returnVal, CurrencyExchangeMTCTransferData *transferData)
{
    MTCTransferDone(transferData);
}

static void
MTCTransferCommitFailRollbackSourceCB(TransactionReturnVal *returnVal, CurrencyExchangeMTCTransferData *transferData)
{
    // We don't really care at this point if the source lock rollback failed.  Just rollback the destination lock now.
    TransactionReturnVal *newReturnVal = objCreateManagedReturnVal(MTCTransferCommitFailRollbackDestCB, transferData);
    AutoTrans_AccountProxy_tr_RollbackLock(newReturnVal, GLOBALTYPE_ACCOUNTPROXYSERVER, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, 
        transferData->destinationLock, transferData->destinationAccountID, transferData->destinationKey);   
}

void
MTCTransferCommitFailed(U32 transferID)
{
    CurrencyExchangeMTCTransferData *transferData = eaIndexedGetUsingInt(&sMTCPendingTransfers, transferID);

    devassert(transferData != NULL);

    if ( transferData != NULL )
    {
        // Roll back the source lock.
        TransactionReturnVal *returnVal = objCreateManagedReturnVal(MTCTransferCommitFailRollbackSourceCB, transferData);

        MTCTransferLog("CommitFail", transferData);

        AutoTrans_AccountProxy_tr_RollbackLock(returnVal, GLOBALTYPE_ACCOUNTPROXYSERVER, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, 
            transferData->sourceLock, transferData->sourceAccountID, transferData->sourceKey);
    }
}

void
MTCTransferCommitSuccessful(U32 transferID)
{
    CurrencyExchangeMTCTransferData *transferData = eaIndexedGetUsingInt(&sMTCPendingTransfers, transferID);

    devassert(transferData != NULL);

    if ( transferData != NULL )
    {
        MTCTransferLog("CommitSuccess", transferData);
        MTCTransferDone(transferData); 
    }
}

static void
MTCTransferBegin(CurrencyExchangeMTCTransferData *transferData)
{
    MTCTransferLog("Begin", transferData);

    MTCTransferMarkAccountsActive(transferData);

    transferData->startTime = timeSecondsSince2000();

	if (gUseKeyValueMove)
	{
		APMoveKeyValue(transferData->sourceAccountID, transferData->sourceKey, transferData->destinationAccountID, transferData->destinationKey, transferData->quantity, MTCTransferMoveCB, transferData);
	}
	else
	{
		APChangeKeyValue(transferData->sourceAccountID, transferData->sourceKey, -(S32)transferData->quantity, MTCTransferPhaseOneCB, transferData);
	}
}

// Add a transfer to the list of pending transfers, and begin the transfer if neither account is currently active.
static void
MTCTransferAddPending(CurrencyExchangeMTCTransferData *transferData)
{
    eaPush(&sMTCPendingTransfers, transferData);

    if ( !MTCTransferAccountsActive(transferData) )
    {
        MTCTransferBegin(transferData);
    }
}

U32
MTCTransferRequest(ContainerID sourceAccountID, ContainerID destinationAccountID, const char *srcKey, const char *destKey, U32 quantity, void *userData, MTCTransferCallback func)
{
    CurrencyExchangeMTCTransferData *transferData = StructCreate(parse_CurrencyExchangeMTCTransferData);

    // if userData is set, there must be a callback to free it
    devassert( (userData == NULL ) || ( func != NULL ) );

    transferData->transferID = sNextTransferID++;
    transferData->requestTime = timeSecondsSince2000();
    transferData->startTime = 0;
    transferData->retryTime = 0;
    transferData->retryCount = 0;
    transferData->sourceAccountID = sourceAccountID;
    transferData->destinationAccountID = destinationAccountID;
    transferData->sourceKey = srcKey;
    transferData->destinationKey = destKey;
    transferData->quantity = quantity;
    transferData->userData = userData;
    transferData->callback = func;

    MTCTransferLog("Request", transferData);

    MTCTransferAddPending(transferData);

    return transferData->transferID;
}

void
MTCTransfer_BeginFrame(void)
{
    int i;
    U32 curTime = timeSecondsSince2000();

    // Scan the list of pending transfers and find any that can run now because their accounts are not active.
    for ( i = eaSize(&sMTCPendingTransfers) - 1; i >= 0; i-- )
    {
        CurrencyExchangeMTCTransferData *transferData = sMTCPendingTransfers[i];

        if ( ( ( transferData->startTime == 0 ) || ( ( transferData->retryTime > 0 ) && ( transferData->retryTime <= curTime ) ) ) && ( !MTCTransferAccountsActive(transferData) ) )
        {
            MTCTransferBegin(transferData);
        }
    }
}

void
MTCTransferInit(void)
{
    eaIndexedEnable(&sMTCPendingTransfers, parse_CurrencyExchangeMTCTransferData);
}

#include "AutoGen/aslMTCTransfer_h_ast.c"