#include "AccountTransactionLog.h"

#include "AccountManagement.h"
#include "accountnet.h"
#include "AccountReporting.h"
#include "AccountServer.h"
#include "earray.h"
#include "EString.h"
#include "KeyValues/KeyValues.h"
#include "KeyValues/VirtualCurrency.h"
#include "Money.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "Product.h"
#include "stdtypes.h"
#include "StringCache.h"

#include "AccountTransactionLog_h_ast.h"

#include "autogen/AccountServer_autotransactions_autogen_wrappers.h"

static StashTable stTransactionLogByTransID = NULL;
static StashTable stTransactionLogByMTID = NULL;
static StashTable stTransactionLogByOrderID = NULL;

static void AccountTransactionStashLog(SA_PARAM_NN_VALID TransactionLogContainer *pLog)
{
#define ACCT_TRANS_ID_DUPLICATED_FMT " %s was duplicated, did not add (acct: %u, trans: %u) to stash table"

	if (devassert(pLog->pTransactionID) && !stashAddPointer(stTransactionLogByTransID, pLog->pTransactionID, pLog, false))
		ErrorOrAlert("TRANSID_DUPLICATED", "Trans ID" ACCT_TRANS_ID_DUPLICATED_FMT, pLog->pTransactionID, pLog->uAccountID, pLog->uID);

	if (pLog->pMerchantTransactionID && !stashAddPointer(stTransactionLogByMTID, pLog->pMerchantTransactionID, pLog, false))
		ErrorOrAlert("MTID_DUPLICATED", "MTID" ACCT_TRANS_ID_DUPLICATED_FMT, pLog->pMerchantTransactionID, pLog->uAccountID, pLog->uID);

	if (pLog->pMerchantOrderID && !stashAddPointer(stTransactionLogByOrderID, pLog->pMerchantOrderID, pLog, false))
		ErrorOrAlert("ORDERID_DUPLICATED", "Order ID" ACCT_TRANS_ID_DUPLICATED_FMT, pLog->pMerchantOrderID, pLog->uAccountID, pLog->uID);

#undef ACCT_TRANS_ID_DUPLICATED_FMT
}

static void AccountTransactionAddCB(Container *con, AccountTransactionLogContainer *pLogContainer)
{
	EARRAY_CONST_FOREACH_REVERSE_BEGIN(pLogContainer->eaTransactions, iLog, iNumLogs);
	{
		AccountTransactionStashLog(pLogContainer->eaTransactions[iLog]);
	}
	EARRAY_FOREACH_END;
}

static void AccountTransactionRemoveCB(Container *con, AccountTransactionLogContainer *pLogContainer)
{
	EARRAY_CONST_FOREACH_REVERSE_BEGIN(pLogContainer->eaTransactions, iLog, iNumLogs);
	{
		TransactionLogContainer * pLog = pLogContainer->eaTransactions[iLog];

		stashRemovePointer(stTransactionLogByTransID, pLog->pTransactionID, NULL);
		stashRemovePointer(stTransactionLogByMTID, pLog->pTransactionID, NULL);
		stashRemovePointer(stTransactionLogByOrderID, pLog->pTransactionID, NULL);
	}
	EARRAY_FOREACH_END;
}

void InitializeAccountTransactionLog(void)
{
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG, parse_AccountTransactionLogContainer, NULL, NULL, NULL, NULL, NULL);
	objRegisterContainerTypeAddCallback(GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG, AccountTransactionAddCB);
	objRegisterContainerTypeRemoveCallback(GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG, AccountTransactionRemoveCB);

	stTransactionLogByTransID = stashTableCreateWithStringKeys(131072, StashDefault);
	stTransactionLogByMTID = stashTableCreateWithStringKeys(131072, StashDefault);
	stTransactionLogByOrderID = stashTableCreateWithStringKeys(131072, StashDefault);
}

void ScanAccountTransactionLog(void)
{
	U32 uTotalTransactions = 0;

	loadstart_printf("Scanning transaction logs...");

	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG, pContainer);
	{
		AccountTransactionLogContainer *pLogContainer = pContainer->containerData;

		EARRAY_CONST_FOREACH_REVERSE_BEGIN(pLogContainer->eaTransactions, iLog, iNumLogs);
		{
			if ((pLogContainer->eaTransactions[iLog]->eTransactionType == TransLogType_MicroPurchase || pLogContainer->eaTransactions[iLog]->eTransactionType == TransLogType_CashPurchase)
				&& pLogContainer->eaTransactions[iLog]->eProvider != TPROVIDER_PerfectWorld && eaSize(&pLogContainer->eaTransactions[iLog]->eaPurchasedItems) == 0)
			{
				AutoTrans_trAccountTransactionDelete(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG, pLogContainer->uID, pLogContainer->eaTransactions[iLog]->uID);
				continue;
			}
		}
		EARRAY_FOREACH_END;
	}
	CONTAINER_FOREACH_END;

	loadend_printf("...done.");
}

// Returns an EString that YOU MUST FREE
AUTO_TRANS_HELPER_SIMPLE;
char *AccountTransactionCreateNewID(void)
{
	return createShortUniqueString(0, ALPHA_NUMERIC_CAPS_READABLE);
}

static AccountTransactionLogContainer *AccountTransactionGetLogContainer(U32 uAccountID, bool bCreate)
{
	Container *pContainer = objGetContainer(GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG, uAccountID);

	if (!pContainer && bCreate)
	{
		NOCONST(AccountTransactionLogContainer) newContainer = {0};
		newContainer.uID = uAccountID;
		newContainer.uNextLogID = GetPurchaseLogCount(uAccountID) + 1;
		newContainer.uNextMigrateID = 1;
		objRequestContainerCreateLocal(NULL, GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG, &newContainer);
		pContainer = objGetContainer(GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG, uAccountID);
		assert(pContainer);
	}

	return pContainer ? pContainer->containerData : NULL;
}

static TransactionLogContainer *AccountTransactionGetSingleLogContainer(U32 uAccountID, U32 uLogID, bool bCreate)
{
	AccountTransactionLogContainer *pLogContainer = AccountTransactionGetLogContainer(uAccountID, bCreate);
	TransactionLogContainer *pLog = NULL;

	if (!devassert(pLogContainer))
		return NULL;

	return eaIndexedGetUsingInt(&pLogContainer->eaTransactions, uLogID);
}

U32 AccountTransactionGetMigrateIDForAccount(U32 uAccountID)
{
	AccountTransactionLogContainer *pLogContainer = AccountTransactionGetLogContainer(uAccountID, true);
	return pLogContainer->uNextMigrateID;
}

void AccountTransactionMigrateComplete(U32 uAccountID, U32 uLogID)
{
	TransactionLogContainer *pLog = AccountTransactionGetSingleLogContainer(uAccountID, uLogID, false);
	if (!devassert(pLog))
		return;
	AccountTransactionStashLog(pLog);
}

AUTO_TRANSACTION
ATR_LOCKS(pLogContainer, ".uID, .uNextLogID, .eaPendingTransactions[]");
enumTransactionOutcome trAccountTransactionOpen(ATR_ARGS, NOCONST(AccountTransactionLogContainer) *pLogContainer, U32 uLogID,
	U32 eType, const char *pSource, U32 eProvider, const char *pMerchantTransactionID, const char *pOrderID)
{
	NOCONST(TransactionLogContainer) *pLog = StructCreateNoConst(parse_TransactionLogContainer);
	char *pUniqueID = AccountTransactionCreateNewID();

	pLog->uID = uLogID;
	pLog->uAccountID = pLogContainer->uID;
	pLog->pTransactionID = StructAllocString(pUniqueID);
	estrDestroy(&pUniqueID);

	pLog->uTimestampSS2000 = timeSecondsSince2000();
	pLog->eProvider = eProvider;
	pLog->pSource = pSource ? StructAllocString(pSource) : NULL;

	pLog->pMerchantTransactionID = pMerchantTransactionID ? StructAllocString(pMerchantTransactionID) : NULL;
	pLog->pMerchantOrderID = pOrderID ? StructAllocString(pOrderID) : NULL;

	pLog->eTransactionType = eType;

	++pLogContainer->uNextLogID;
	if (!eaIndexedPushUsingIntIfPossible(&pLogContainer->eaPendingTransactions, uLogID, pLog))
		return TRANSACTION_OUTCOME_FAILURE;
	return TRANSACTION_OUTCOME_SUCCESS;
}

U32 AccountTransactionOpen(U32 uAccountID, TransactionLogType eType, const char *pSource, TransactionProvider eProvider, const char *pMerchantTransactionID, const char *pOrderID)
{
	AccountTransactionLogContainer *pLogContainer = AccountTransactionGetLogContainer(uAccountID, true);
	U32 uIndex = pLogContainer->uNextLogID;

	AutoTrans_trAccountTransactionOpen(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG, uAccountID, uIndex, eType, pSource, eProvider, pMerchantTransactionID, pOrderID);
	devassert(eaIndexedFindUsingInt(&pLogContainer->eaPendingTransactions, uIndex) > -1);
	return uIndex;
}

AUTO_TRANSACTION
ATR_LOCKS(pLogContainer, ".eaPendingTransactions[]");
enumTransactionOutcome trAccountTransactionRecordPurchase(ATR_ARGS, NOCONST(AccountTransactionLogContainer) *pLogContainer, U32 uLogID, U32 uProductID, const Money *pPrice)
{
	NOCONST(TransactionLogContainer) *pLog = eaIndexedGetUsingInt(&pLogContainer->eaPendingTransactions, uLogID);
	NOCONST(TransactionLogItem) *pItem = NULL;

	if (!pLog)
		return TRANSACTION_OUTCOME_FAILURE;

	pItem = StructCreateNoConst(parse_TransactionLogItem);
	pItem->uProductID = uProductID;
	pItem->pPrice = StructCloneNoConst(parse_MoneyContainer, moneyToContainerConst(pPrice));

	if (!pLog->pPriceTotal)
		pLog->pPriceTotal = StructCloneNoConst(parse_MoneyContainer, moneyToContainerConst(pPrice));
	else
		moneyAdd(moneyContainerToMoney(pLog->pPriceTotal), pPrice);

	eaPush(&pLog->eaPurchasedItems, pItem);
	return TRANSACTION_OUTCOME_SUCCESS;
}

void AccountTransactionRecordPurchase(U32 uAccountID, U32 uLogID, U32 uProductID, const Money *pPrice)
{
	AutoTrans_trAccountTransactionRecordPurchase(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG, uAccountID, uLogID, uProductID, pPrice);
}

AUTO_TRANSACTION
ATR_LOCKS(pLogContainer, ".eaPendingTransactions[]");
enumTransactionOutcome trAccountTransactionRecordKeyValueChanges(ATR_ARGS, NOCONST(AccountTransactionLogContainer) *pLogContainer, U32 uLogID, TransactionLogKeyValueChangeSet *pChanges, const char *pReason)
{
	NOCONST(TransactionLogContainer) *pLog = eaIndexedGetUsingInt(&pLogContainer->eaPendingTransactions, uLogID);

	if (!pLog)
		return TRANSACTION_OUTCOME_FAILURE;

	eaCopyStructsDeConst(&pChanges->eaChanges, &pLog->eaKeyValueChanges, parse_TransactionLogKeyValueChange);
	pLog->pKeyValueChangeReason = pReason ? StructAllocString(pReason) : NULL;
	return TRANSACTION_OUTCOME_SUCCESS;
}

void AccountTransactionRecordKeyValueChanges(U32 uAccountID, U32 uLogID, CONST_EARRAY_OF(TransactionLogKeyValueChange) eaChanges, const char *pReason)
{
	TransactionLogKeyValueChangeSet changes = {0};
	changes.eaChanges = eaChanges;
	AutoTrans_trAccountTransactionRecordKeyValueChanges(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG, uAccountID, uLogID, &changes, pReason);
}

AUTO_TRANSACTION
ATR_LOCKS(pLogContainer, ".eaTransactions[], .eaPendingTransactions[]");
enumTransactionOutcome trAccountTransactionFinish(ATR_ARGS, NOCONST(AccountTransactionLogContainer) *pLogContainer, U32 uLogID)
{
	NOCONST(TransactionLogContainer) *pLog = eaIndexedRemoveUsingInt(&pLogContainer->eaPendingTransactions, uLogID);

	if (!pLog)
		return TRANSACTION_OUTCOME_FAILURE;

	if (!eaIndexedPushUsingIntIfPossible(&pLogContainer->eaTransactions, uLogID, pLog))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}

void AccountTransactionFinish(U32 uAccountID, U32 uLogID)
{
	TransactionLogContainer *pLog = NULL;
	AutoTrans_trAccountTransactionFinish(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG, uAccountID, uLogID);
	pLog = AccountTransactionGetSingleLogContainer(uAccountID, uLogID, false);
	if (!devassert(pLog))
		return;
	AccountTransactionStashLog(pLog);
}

AUTO_TRANSACTION
ATR_LOCKS(pLogContainer, ".eaTransactions[]");
enumTransactionOutcome trAccountTransactionDelete(ATR_ARGS, NOCONST(AccountTransactionLogContainer) *pLogContainer, U32 uLogID)
{
	NOCONST(TransactionLogContainer) *pLog = eaIndexedRemoveUsingInt(&pLogContainer->eaTransactions, uLogID);

	if (!pLog)
		return TRANSACTION_OUTCOME_FAILURE;

	StructDestroyNoConst(parse_TransactionLogContainer, pLog);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pLogContainer, ".uNextLogID, .uNextMigrateID, .eaTransactions, .eaPendingTransactions");
enumTransactionOutcome trAccountTransactionFixupMigration(ATR_ARGS, NOCONST(AccountTransactionLogContainer) *pLogContainer, int iPurchaseLogCount_N)
{
	// The way this works is:
	// N = iPurchaseLogCount_N, total number of purchase logs there originally were for this user
	// K = pLogContainer->uNextMigrateID - 1, number of purchase logs actually migrated
	// M = pLogContainer->uNextLogID - 1, number of new transactions completed by the user
	//
	// There are four desired chunks of logs:
	//		1) (1, K) - successfully migrated purchase logs
	//		2) (K+1, N) - non-migrated purchase logs, offset K
	//		3) (N+1, N+K) - pending transactions, offset N
	//		4) if M > K: (N+K+1, N+M) - completed transactions, offset N
	int iNumMigrated_K = pLogContainer->uNextMigrateID - 1; // K
	int iNumPending_MinKM = eaSize(&pLogContainer->eaPendingTransactions); // MIN(K, M)
	int iNumNew_M = pLogContainer->uNextLogID - 1; // M
	int iLogIndex = 0;

	if (iNumMigrated_K >= iPurchaseLogCount_N && iNumPending_MinKM == 0 && pLogContainer->uNextLogID >= pLogContainer->uNextMigrateID)
		TRANSACTION_RETURN_FAILURE("Container doesn't meet requirements for fixup");

	for (iLogIndex = iNumNew_M; iLogIndex > iNumMigrated_K; --iLogIndex)
	{
		NOCONST(TransactionLogContainer) *pLog = eaIndexedRemoveUsingInt(&pLogContainer->eaTransactions, iLogIndex);

		if (!pLog) continue;
		pLog->uID += iPurchaseLogCount_N;

		if (!eaIndexedPushUsingIntIfPossible(&pLogContainer->eaTransactions, pLog->uID, pLog))
			TRANSACTION_RETURN_FAILURE("Couldn't move log ID %d to %d", iLogIndex, pLog->uID);
	}

	for (iLogIndex = iNumPending_MinKM; iLogIndex > 0; --iLogIndex)
	{
		NOCONST(TransactionLogContainer) *pLog = eaIndexedRemoveUsingInt(&pLogContainer->eaPendingTransactions, iLogIndex);

		if (!pLog) continue;
		pLog->uID += iPurchaseLogCount_N;

		if (!eaIndexedPushUsingIntIfPossible(&pLogContainer->eaTransactions, pLog->uID, pLog))
			TRANSACTION_RETURN_FAILURE("Couldn't move pending log ID %d to %d", iLogIndex, pLog->uID);
	}

	pLogContainer->uNextLogID += iPurchaseLogCount_N;
	return TRANSACTION_OUTCOME_SUCCESS;
}

void AccountTransactionFixupMigration(U32 uAccountID, U32 uPurchaseLogCount)
{
	AutoTrans_trAccountTransactionFixupMigration(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG, uAccountID, uPurchaseLogCount);
}

void AccountTransactionFixupMigrationReturn(U32 uAccountID, U32 uPurchaseLogCount, TransactionReturnCallback pCallback, void *pUserdata)
{
	if (!pCallback) return;
	AutoTrans_trAccountTransactionFixupMigration(objCreateManagedReturnVal(pCallback, pUserdata), GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG, uAccountID, uPurchaseLogCount);
}

void AccountTransactionGetKeyValueChanges(AccountInfo *pAccount, CONST_STRING_EARRAY eaKeys, TransactionLogKeyValueChange ***eaChanges)
{
	PERFINFO_AUTO_START_FUNC();
	EARRAY_CONST_FOREACH_BEGIN(eaKeys, iKey, iNumKeys);
	{
		const char *pKey = eaKeys[iKey];
		NOCONST(TransactionLogKeyValueChange) *pChange = NULL;
		AccountKeyValueResult eResult;

		if (!KeyIsCurrency(pKey))
			continue;

		pChange = StructCreateNoConst(parse_TransactionLogKeyValueChange);
		pChange->pKey = allocAddString(pKey);

		eResult = AccountKeyValue_Get(pAccount, pKey, &pChange->iOldValue);
		if (eResult == AKV_SUCCESS)
			eResult = AccountKeyValue_GetLocked(pAccount, pKey, &pChange->iNewValue);

		if (eResult == AKV_SUCCESS && pChange->iOldValue != pChange->iNewValue)
			eaPush(eaChanges, CONTAINER_RECONST(TransactionLogKeyValueChange, pChange));
		else
			StructDestroyNoConst(parse_TransactionLogKeyValueChange, pChange);
	}
	EARRAY_FOREACH_END;
	PERFINFO_AUTO_STOP();
}

void AccountTransactionFreeKeyValueChanges(TransactionLogKeyValueChange ***eaChanges)
{
	eaDestroyStruct(eaChanges, parse_TransactionLogKeyValueChange);
}

// Transform the transaction log into the old-school purchase log format, for XMLRPC purposes
EARRAY_OF(PurchaseLog) AccountTransactionGetPurchaseLog(U32 uSinceSS2000, U64 uMaxResponses, U32 uAccountID)
{
	AccountTransactionLogContainer *pLogContainer;
	PurchaseLog **eaLogs = NULL;

	if (!devassert(uAccountID))
		return NULL;

	// Get all matching purchases.
	pLogContainer = AccountTransactionGetLogContainer(uAccountID, false);

	if (!pLogContainer)
		return NULL;

	EARRAY_CONST_FOREACH_REVERSE_BEGIN(pLogContainer->eaTransactions, iLog, iNumLogs);
	{
		TransactionLogContainer *pLog = pLogContainer->eaTransactions[iLog];
		PurchaseLog *pPurchaseLog = NULL;
		AccountInfo *pAccount = NULL;
		const ProductContainer *pProduct = NULL;

		if (!uMaxResponses)
			break;
		if (pLog->uTimestampSS2000 < uSinceSS2000)
			break;
		if (eaSize(&pLog->eaPurchasedItems) == 0)
			continue;

		--uMaxResponses;
		pPurchaseLog = StructCreate(parse_PurchaseLog);
		pAccount = findAccountByID(pLog->uAccountID);
		pProduct = findProductByID(pLog->eaPurchasedItems[0]->uProductID);

		pPurchaseLog->uID = pLog->uID;
		pPurchaseLog->pAccount = pAccount ? pAccount->accountName : NULL;
		pPurchaseLog->pProduct = pProduct ? pProduct->pName : NULL;
		estrFromMoneyRaw(&pPurchaseLog->pPrice, moneyContainerToMoneyConst(pLog->pPriceTotal));
		pPurchaseLog->pCurrency = moneyCurrency(moneyContainerToMoneyConst(pLog->pPriceTotal));
		pPurchaseLog->uTimestampSS2000 = pLog->uTimestampSS2000;
		pPurchaseLog->pMerchantTransactionId = pLog->pMerchantTransactionID;
		pPurchaseLog->pOrderID = pLog->pMerchantOrderID;
		pPurchaseLog->eProvider = pLog->eProvider;
		pPurchaseLog->eaKeyValues = pLog->eaLegacyKeyValueChanges;
		eaPush(&eaLogs, pPurchaseLog);
	}
	EARRAY_FOREACH_END;

	return eaLogs;
}

// FOR THE LOVE OF GOD DO NOT FREE ANYTHING RETURNED BY THIS FUNCTION - YOU DO NOT OWN IT
EARRAY_OF(TransactionLogContainer) AccountTransactionGetLog(U32 uSinceSS2000, U64 uMaxResponses, U32 uAccountID)
{
	AccountTransactionLogContainer *pLogContainer = NULL;
	TransactionLogContainer **eaLogs = NULL;

	if (!devassert(uAccountID))
		return NULL;

	pLogContainer = AccountTransactionGetLogContainer(uAccountID, false);

	if (!pLogContainer)
		return NULL;

	EARRAY_CONST_FOREACH_REVERSE_BEGIN(pLogContainer->eaTransactions, iLog, iNumLogs);
	{
		TransactionLogContainer *pLog = pLogContainer->eaTransactions[iLog];

		if (!uMaxResponses)
			break;
		if (pLog->uTimestampSS2000 < uSinceSS2000)
			break;

		--uMaxResponses;
		eaPush(&eaLogs, pLog);
	}
	EARRAY_FOREACH_END;

	return eaLogs;
}

int AccountTransactionGetLogCount(U32 uAccountID)
{
	AccountTransactionLogContainer *pLogContainer = AccountTransactionGetLogContainer(uAccountID, false);
	return pLogContainer ? eaSize(&pLogContainer->eaTransactions) : 0;
}

// Get the products associated with a merchant transaction ID.
EARRAY_OF(const ProductContainer) AccountTransactionGetProductsByMTID(SA_PARAM_NN_STR const char *pMerchantTransactionId)
{
	TransactionLogContainer *pLog = NULL;
	const ProductContainer **eaProducts = NULL;

	// Find all products matching this MTID.
	if (devassert(pMerchantTransactionId) && stashFindPointer(stTransactionLogByMTID, pMerchantTransactionId, &pLog))
	{
		EARRAY_CONST_FOREACH_BEGIN(pLog->eaPurchasedItems, iItem, iNumItems);
		{
			const ProductContainer *pProduct = findProductByID(pLog->eaPurchasedItems[iItem]->uProductID);
			eaPush(&eaProducts, pProduct);
		}
		EARRAY_FOREACH_END;
	}

	// Return results.
	return eaProducts;
}

SA_RET_OP_STR const char *AccountTransactionGetOrderIDSource(SA_PARAM_NN_STR const char *pOrderId)
{
	TransactionLogContainer *pLog = NULL;
	const char *pSource = NULL;
	
	if (devassert(pOrderId) && stashFindPointer(stTransactionLogByOrderID, pOrderId, &pLog))
	{
		pSource = pLog->pSource;
	}

	return pSource;
}

#include "AccountTransactionLog_h_ast.c"