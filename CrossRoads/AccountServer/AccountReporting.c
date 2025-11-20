#include "AccountReporting.h"

#include "AccountIntegration.h"
#include "AccountManagement.h"
#include "AccountServerConfig.h"
#include "AccountTransactionLog.h"
#include "Activity/billingActivity.h"
#include "billing.h"
#include "BitArray.h"
#include "EString.h"
#include "file.h"
#include "GlobalData.h"
#include "KeyValues/KeyValues.h"
#include "logging.h"
#include "Money.h"
#include "objContainer.h"
#include "objIndex.h"
#include "objTransactions.h"
#include "Product.h"
#include "ProductKey.h"
#include "StashTable.h"
#include "StringCache.h"
#include "textparser.h"
#include "timing.h"
#include "utilitiesLib.h"

#include "AccountReporting_c_ast.h"
#include "AccountTransactionLog_h_ast.h"

#include "autogen/AccountServer_autotransactions_autogen_wrappers.h"

// A single purchase in the purchase log
AUTO_STRUCT AST_CONTAINER;
typedef struct PurchaseLogContainer
{
	const U32 uID;													AST(PERSIST KEY)// ID
	const U32 uAccountID;											AST(PERSIST)	// Account ID
	const U32 uProductID;											AST(PERSIST)	// Product ID
	CONST_STRING_MODIFIABLE pSource;								AST(PERSIST)	// Source (proxy) of the transaction
	CONST_STRING_MODIFIABLE pPrice;									AST(PERSIST)	// String version of the price
	CONST_STRING_MODIFIABLE pCurrency;								AST(PERSIST)	// Currency
	const U32 uTimestampSS2000;										AST(PERSIST)	// Timestamp in seconds since 2000
	CONST_STRING_MODIFIABLE pMerchantTransactionId;					AST(PERSIST)	// Merchant transaction ID
	CONST_EARRAY_OF(ProductKeyValueChangeContainer) eaKeyValues;	AST(PERSIST)	// An array of key value changes
	CONST_STRING_MODIFIABLE pOrderID;								AST(PERSIST)
	const TransactionProvider eProvider;							AST(PERSIST)

	AST_COMMAND("Apply Transaction","ServerMonTransactionOnEntity AccountServerPurchaseLog $FIELD(UID) $STRING(Transaction String)")
} PurchaseLogContainer;

// True if reporting is enabled.
bool gbReportingDisabled = false;

// Purchase log indices.
static ObjectIndex *gidx_PurchaseLogByAccountID = NULL;

// Collection of all account keys that will be tracked as if it were a points currency.
static StashTable stPurchaseLogKeys = NULL;

// List of names of all batches.
static const char **gppBatchNames = NULL;
static int giLastMaxId = 0;

// Recent billing authorization transactions
static AuthTransaction **authTransactions = NULL;

static int giLoginHistoryCount = 10000;
AUTO_CMD_INT(giLoginHistoryCount, LoginHistoryCount) ACMD_CMDLINE;

/************************************************************************/
/* Unpersisted tracked statistics data                                  */
/************************************************************************/

// Last time reporting was enabled, if not recorded in the global container.
static U32 uLastEnabled = 0;

// Last time reporting was disabled, if not recorded in the global container.
static U32 uLastDisabled = 0;

// Login history record for performance monitoring
static TimingHistory *gLoginHistory = NULL;

// True for each account that has logged in during this reporting period.
static BitArray baUniqueLoginsHourlyCurrent = NULL;
U32 baUniqueLoginsHourlyPreviousCount = 0;

/************************************************************************/
/* Private support routines                                             */
/************************************************************************/

// Update last enabled or last disabled time in AccountDB, if necessary and possible.
static void UpdateStatus()
{
	// Make sure it's initialized.
	if (!astInitialized())
		return;

	// Update status in global container.
	if (uLastEnabled)
		asgSetGlobalStatsLastEnabled(uLastEnabled);
	if (uLastDisabled)
		asgSetGlobalStatsLastDisabled(uLastDisabled);
}

// Initialize StashTable for keys that we should be logging with purchases.
static void InitPurchaseLogKeys(void)
{
	CONST_STRING_EARRAY keys = GetCurrencyKeys();
	stPurchaseLogKeys = stashTableCreateWithStringKeys(23, StashDefault);
	EARRAY_CONST_FOREACH_BEGIN(keys, i, n);
	{
		bool success;
		const char *pPooledKey = allocAddString_dbg(keys[i], false, false, false, __FILE__, __LINE__);
		success = stashAddInt(stPurchaseLogKeys, pPooledKey, 1, false);
		devassert(success);
	}
	EARRAY_FOREACH_END;
}

// Initialize purchase log stuff
void InitializePurchaseLog(void)
{
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNTSERVER_PURCHASE_LOG, parse_PurchaseLogContainer, NULL, NULL, NULL, NULL, NULL);
	gidx_PurchaseLogByAccountID = objAddContainerStoreIndexWithPaths(objFindOrCreateContainerStoreFromType(GLOBALTYPE_ACCOUNTSERVER_PURCHASE_LOG), ".uAccountID", ".uID", 0);
	InitPurchaseLogKeys();
}

bool KeyIsInteresting(const char *pKey)
{
	return stashFindInt(stPurchaseLogKeys, pKey, NULL);
}

// Copy purchases from the GlobalData container to an array in AccountStats.
static void CopyPurchases(EARRAY_OF(CurrencyPurchases) *pppDestination, CONST_EARRAY_OF(CurrencyPurchasesContainer) ppSource)
{
	EARRAY_CONST_FOREACH_BEGIN(ppSource, i, n);
		int j = eaPush(pppDestination, StructCreate(parse_CurrencyPurchases));
		devassert(i == j);
		(*pppDestination)[i]->Locale = estrDupIfNonempty(ppSource[i]->Locale);
		(*pppDestination)[i]->Product = estrDup(ppSource[i]->Product);
		(*pppDestination)[i]->PurchaseCount = ppSource[i]->PurchaseCount;
		estrFromMoneyRaw(&(*pppDestination)[i]->TotalPurchases, moneyContainerToMoneyConst(&ppSource[i]->TotalPurchases));
		estrCurrency(&(*pppDestination)[i]->Currency, moneyContainerToMoneyConst(&ppSource[i]->TotalPurchases));
	EARRAY_FOREACH_END;
}

AUTO_TRANSACTION
ATR_LOCKS(pLogContainer, ".uID, .uNextMigrateID, .eaTransactions[]");
enumTransactionOutcome trAccountTransactionMigratePurchaseLog(ATR_ARGS, NOCONST(AccountTransactionLogContainer) *pLogContainer, U32 uLogID, NON_CONTAINER PurchaseLogContainer *pPurchaseLog)
{
	NOCONST(MoneyContainer) *pPrice = StructCreateNoConst(parse_MoneyContainer);
	NOCONST(TransactionLogContainer) *pLog = StructCreateNoConst(parse_TransactionLogContainer);
	NOCONST(TransactionLogItem) *pItem = StructCreateNoConst(parse_TransactionLogItem);
	char *pUniqueID = AccountTransactionCreateNewID();

	pLog->uID = uLogID;
	pLog->uAccountID = pLogContainer->uID;
	pLog->pTransactionID = StructAllocString(pUniqueID);
	estrDestroy(&pUniqueID);

	pLog->uTimestampSS2000 = pPurchaseLog->uTimestampSS2000;
	pLog->eProvider = pPurchaseLog->eProvider;
	pLog->pSource = pPurchaseLog->pSource ? StructAllocString(pPurchaseLog->pSource) : NULL;

	pLog->pMerchantTransactionID = pPurchaseLog->pMerchantTransactionId ? StructAllocString(pPurchaseLog->pMerchantTransactionId) : NULL;
	pLog->pMerchantOrderID = pPurchaseLog->pOrderID ? StructAllocString(pPurchaseLog->pOrderID) : NULL;

	if (pLog->pMerchantTransactionID)
		pLog->eTransactionType = TransLogType_CashPurchase;
	else
		pLog->eTransactionType = TransLogType_MicroPurchase;

	moneyInitFromStr(moneyContainerToMoney(pPrice), pPurchaseLog->pPrice, pPurchaseLog->pCurrency);
	pLog->pPriceTotal = pPrice;
	eaCopyStructsDeConst(&pPurchaseLog->eaKeyValues, &pLog->eaLegacyKeyValueChanges, parse_ProductKeyValueChangeContainer);

	pItem->uProductID = pPurchaseLog->uProductID;
	pItem->pPrice = StructCloneNoConst(parse_MoneyContainer, pPrice);
	eaPush(&pLog->eaPurchasedItems, pItem);

	++pLogContainer->uNextMigrateID;
	if (!eaIndexedPushUsingIntIfPossible(&pLogContainer->eaTransactions, uLogID, pLog))
		return TRANSACTION_OUTCOME_FAILURE;
	return TRANSACTION_OUTCOME_SUCCESS;
}

void MigratePurchaseLog(const PurchaseLogContainer *pPurchaseLog)
{
	U32 uLogID = AccountTransactionGetMigrateIDForAccount(pPurchaseLog->uAccountID);
	AutoTrans_trAccountTransactionMigratePurchaseLog(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG, pPurchaseLog->uAccountID, uLogID, pPurchaseLog);
	AccountTransactionMigrateComplete(pPurchaseLog->uAccountID, uLogID);
}

void PurchaseLogMigrationTick(void)
{
	static U32 uMigrateID = 0;
	U32 uLogID = 0;
	Container *pContainer = NULL;
	PurchaseLogContainer *pPurchaseLog = NULL;

	if (!objCountTotalContainersWithType(GLOBALTYPE_ACCOUNTSERVER_PURCHASE_LOG)) return;

	PERFINFO_AUTO_START_FUNC();

	while (!pContainer)
	{
		// Just in case we are somehow missing one or two - get the next existing one
		++uMigrateID;
		pContainer = objGetContainer(GLOBALTYPE_ACCOUNTSERVER_PURCHASE_LOG, uMigrateID);
	}

	pPurchaseLog = pContainer->containerData;
	MigratePurchaseLog(pPurchaseLog);
	objRequestContainerDestroyLocal(NULL, GLOBALTYPE_ACCOUNTSERVER_PURCHASE_LOG, uMigrateID);
	PERFINFO_AUTO_STOP();
}

// Reconstruct a purchase log that should have been migrated but wasn't
void FixupMigratedPurchaseLog(U32 uAccountID,
	U32 uProductID,
	const char *pSource,
	const char *strPrice,
	const char *pCurrency,
	U32 uTimestampSS2000,
	SA_PARAM_OP_STR const char *pOrderID,
	SA_PARAM_OP_STR const char *merchantTransactionId,
	TransactionProvider eProvider)
{
	NOCONST(PurchaseLogContainer) log = {0};

	log.uAccountID = uAccountID;
	log.uProductID = uProductID;
	log.pSource = (char *)pSource;
	log.pPrice = (char *)strPrice;
	log.pCurrency = (char *)pCurrency;
	log.uTimestampSS2000 = uTimestampSS2000;
	log.pMerchantTransactionId = (char *)merchantTransactionId;
	log.pOrderID = (char *)pOrderID;
	log.eProvider = eProvider;

	MigratePurchaseLog(CONTAINER_RECONST(PurchaseLogContainer, &log));
}

// Round a SecsSince2000() time down to the nearest hour.
static U32 asgFloorToHour(U32 uTime)
{
	return uTime / (60*60) * (60*60);
}

// Round a SecsSince2000() time down to the nearest day
static U32 asgFloorToDay(U32 uTime)
{
	return uTime / (60*60*24) * (60*60*24);
}

// Add each key batch to the cached list.
static void AddKeyBatchToList(SA_PARAM_NN_VALID const ProductKeyBatch *pBatch, SA_PARAM_OP_VALID void *pUserData)
{
	int i;
	devassert(!pUserData);
	i = pBatch->uID;
	if (!gppBatchNames || i >= eaSize(&gppBatchNames))
		eaSetSize(&gppBatchNames, i + 1);
	gppBatchNames[i] = pBatch->pBatchName;
}

// Update the cached list of batch names if the number of batches has changed.
static void UpdateBatchNamesIfNecessary()
{
	PERFINFO_AUTO_START_FUNC();

	// Recreate cached batch list if necessary.
	if (giLastMaxId != getMaxBatchId())
	{
		eaClear(&gppBatchNames);
		iterateKeyBatches(AddKeyBatchToList, NULL);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

/************************************************************************/
/* Logging hooks                                                        */
/************************************************************************/

// Report the results of a transaction authorization attempt.
void accountReportAuthAttempt(bool bSuccess, U32 uTransId, char *pWebUid, char *pResultString, U32 uAccountId)
{
	AuthTransaction *newTrans = NULL;
	U32 now = 0;
	float countSuccess = 0;
	float rate = 0;
	static U32 cooldown = 0;
	U32 lifetime = 0;
	U32 minimumCount = 0;

	PERFINFO_AUTO_START_FUNC();

	lifetime = billingGetAuthFailPeriod();
	minimumCount = billingGetAuthFailMinimumCount();

	if (billingGetVindiciaLogLevel() >= 1 && !bSuccess)
		servLog(LOG_ACCOUNT_SERVER_VINDICIA, "VindiciaTransactionFailed", "transaction %lu", uTransId);

	// Don't record data if reporting is disabled.
	if (!ReportingEnabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Remove old transactions from the buffer.
	now = timeSecondsSince2000();
	EARRAY_FOREACH_REVERSE_BEGIN(authTransactions, i);
		AuthTransaction *trans = authTransactions[i];
		if (trans->uRecorded + lifetime < now)
		{
			StructDestroy(parse_AuthTransaction, trans);
			eaRemoveFast(&authTransactions, i);
		}
	EARRAY_FOREACH_END;

	// If there are too many transactions, remove one.
	if (eaSize(&authTransactions) > 2000)
	{
		int i = rand() % eaSize(&authTransactions);
		eaRemoveFast(&authTransactions, i);
	}

	// Insert this transaction.
	newTrans = StructCreate(parse_AuthTransaction);
	newTrans->bSuccess = bSuccess;
	newTrans->uTransId = uTransId;
	newTrans->pWebUid = strdup(pWebUid);
	newTrans->pResultString = strdup(pResultString);
	newTrans->uAccountId = uAccountId;
	newTrans->uRecorded = now;
	eaPush(&authTransactions, newTrans);

	// Alert if the authorization success ratio is too low.
	EARRAY_CONST_FOREACH_BEGIN(authTransactions, i, n);
		if (authTransactions[i]->bSuccess)
			++countSuccess;
	EARRAY_FOREACH_END;
	rate = countSuccess/eaSize(&authTransactions);
	if (countSuccess/eaSize(&authTransactions) < billingGetAuthFailThreshold() && eaUSize(&authTransactions) > minimumCount && now > cooldown)
	{
		char *report = NULL;

		// Alert.
		AssertOrAlert("ACCOUNTSERVER_VINDICIA_AUTH_FAIL_RATIO",
			"Over the past %lu transaction, %3.2f%% of them have failed.",
			eaSize(&authTransactions), 100.0f-rate*100.0f);

		// Log report.
		estrStackCreate(&report);
		estrPrintf(&report, "ACCOUNTSERVER_VINDICIA_AUTH_FAIL_RATIO Report\nSuccess\tTransId\tWebUid\tResultString\tAccountId\tRecorded\n");
		EARRAY_CONST_FOREACH_BEGIN(authTransactions, i, n);
		{
			AuthTransaction *trans = authTransactions[i];
			estrConcatf(&report, "%d\t%lu\t%s\t%s\t%lu\t%lu\n", (int)trans->bSuccess, trans->uTransId, NULL_TO_EMPTY(trans->pWebUid),
				NULL_TO_EMPTY(trans->pResultString), trans->uAccountId, trans->uRecorded);
		}
		EARRAY_FOREACH_END;
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "%s", report);
		estrDestroy(&report);

		// Don't alert for five more minutes.
		cooldown = now + 60*5;
	}

	PERFINFO_AUTO_STOP_FUNC();
}


// Record key-value changes
void accountReportKeyValue(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *key, S64 oldValue,
						   S64 newValue, bool bActualActivity)
{
	Money oldMoney, newMoney, zero;
	char *currency = NULL;
	U64 change;

	PERFINFO_AUTO_START_FUNC();

	// Can happen if a lock expired
	if (!oldValue && !newValue)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Don't record data if reporting is disabled.
	if (!ReportingEnabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Don't record data if this is not an interesting key.
	if (!stashFindInt(stPurchaseLogKeys, key, NULL))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Convert to currency.
	estrStackCreate(&currency);
	estrPrintf(&currency, "_%s", key);
	moneyInitFromStr(&zero, "0", currency);
	moneyInit(&oldMoney);
	moneyInit(&newMoney);
	if (oldValue)
		moneyInitFromInt(&oldMoney, oldValue, currency);
	else
		moneyAssign(&oldMoney, &zero);
	if (newValue)
		moneyInitFromInt(&newMoney, newValue, currency);
	else
		moneyAssign(&newMoney, &zero);
	estrDestroy(&currency);

	// Record this if this is actually a change in the amount of points.
	if (!moneyEqual(&oldMoney, &newMoney))
	{
		char *note = NULL;

		// Determine if this is a change to the number of accounts with points balances.
		if (!oldValue || moneyEqual(&oldMoney, &zero))
			change = 1;
		else if (!newValue || moneyEqual(&newMoney, &zero))
			change = -1;
		else
			change = 0;

		// Record the points change.
		moneySubtract(&newMoney, &oldMoney);
		asgAddTotalAccountsPointsBalanceItem(&newMoney, change);

		// Report points changes to Vindicia.
		if (bActualActivity)
		{
			estrStackCreate(&note);
			estrPrintf(&note, "Points type %s changed from %"FORM_LL"d to %"FORM_LL"d", moneyCurrency(&oldMoney),
				moneyCountPoints(&oldMoney), moneyCountPoints(&newMoney));
			btActivityRecordNote(pAccount, note);
			estrDestroy(&note);
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Report login.
void accountReportLogin(SA_PARAM_NN_VALID const AccountInfo *pAccount, U32 uIp)
{
	PERFINFO_AUTO_START_FUNC();

	// Always report logins to Vindicia.
	btActivityRecordLogin(pAccount, uIp);

	// Don't record data if reporting is disabled.
	if (!ReportingEnabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Record this account as having logged in within this hourly period.
	baSizeToFit(&baUniqueLoginsHourlyCurrent, pAccount->uID + 1);
	baSetBit(baUniqueLoginsHourlyCurrent, pAccount->uID);

	// Add this login to the login timing history.
	timingHistoryPush(gLoginHistory);

	// Record login in the total login count.
	asgAddGlobalStatsTotalLogins(1);

	PERFINFO_AUTO_STOP_FUNC();
}

// Record play time on an account.
void accountReportLogoutPlayTime(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pProductName,
								 SA_PARAM_NN_STR const char *pShardCategory, U32 uPlayTime)
{
	PERFINFO_AUTO_START_FUNC();

	// Always record logout play time.
	btActivityRecordLogout(pAccount, 0, uPlayTime);

	// Don't record data if reporting is disabled.
	if (!ReportingEnabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Don't do anything if there's no play time to add.
	if (!uPlayTime)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Add this to the play time total.
	asgAddPlayTime(pAccount->defaultLocale, pProductName, pShardCategory, uPlayTime);

	PERFINFO_AUTO_STOP_FUNC();
}

// Report a number of keys as being activated.
void accountReportProductKeyActivate(int iBatchId, U64 change)
{
	PERFINFO_AUTO_START_FUNC();

	// Don't record data if reporting is disabled.
	if (!ReportingEnabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Report the keys as being activated.
	asgAddKeyActivations(iBatchId, change);

	PERFINFO_AUTO_STOP_FUNC();
}

// Log each purchase to Vindicia and to global stats if appropriate
void accountReportPurchase(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_VALID const ProductContainer *pProduct, SA_PARAM_NN_VALID const Money *pPrice)
{
	char * pPriceValue = NULL;

	PERFINFO_AUTO_START_FUNC();
	estrFromMoneyRaw(&pPriceValue, pPrice);

	if (isRealCurrency(moneyCurrency(pPrice)))
	{
		accountSetBilled(pAccount);
	}
	else if (moneyCountPoints(pPrice) > 0)
	{
		// Report points purchases to Vindicia.
		char *pNote = NULL;
		estrStackCreate(&pNote);
		estrPrintf(&pNote, "Purchased product \"%s\" using %s %s", pProduct->pName, pPriceValue, moneyCurrency(pPrice));
		btActivityRecordNote(pAccount, pNote);
		estrDestroy(&pNote);
	}
	estrDestroy(&pPriceValue);

	// Don't record data if reporting is disabled.
	if (!ReportingEnabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Record this purchase.
	asgAddPurchaseStatsItem(pAccount->defaultLocale, pProduct->pName, pPrice);
	asgAddGlobalStatsTotalPurchases(1);

	PERFINFO_AUTO_STOP_FUNC();
}

// Record transaction code responses.
void accountReportTransactionCodes(const AccountInfo *pAccount, const char *pCurrency, const char *pAuthCode,
								   const char *pAvsCode, const char *pCvnCode)
{
	PERFINFO_AUTO_START_FUNC();

	// Don't record data if reporting is disabled.
	if (!ReportingEnabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Record codes for this transaction.
	asgAddTransactionCodes(pAccount->defaultLocale, pCurrency, pAuthCode, pAvsCode, pCvnCode);

	PERFINFO_AUTO_STOP_FUNC();
}

/************************************************************************/
/* Statistics reporting                                                 */
/************************************************************************/

// Add each key batch to the cached list.
static void GetKeyCount(SA_PARAM_NN_VALID const ProductKeyBatch *pBatch, SA_PARAM_NN_VALID void *pUserData)
{
	EARRAY_OF(BatchKeyCounts) eaKeyCounts = pUserData;
	eaKeyCounts[pBatch->uID]->AvailableKeys = productKeysBatchSizeNew(pBatch->uID);
	eaKeyCounts[pBatch->uID]->ActivatedKeys = productKeysBatchSizeUsed(pBatch->uID);
}


// Get key counts.
static void GetKeyCounts(EARRAY_OF(BatchKeyCounts) *eaKeyCounts)
{
	PERFINFO_AUTO_START_FUNC();

	eaSetSizeStruct(eaKeyCounts, parse_BatchKeyCounts, getMaxBatchId() + 1);
	iterateKeyBatches(GetKeyCount, *eaKeyCounts);

	PERFINFO_AUTO_STOP_FUNC();
}

// Get all global statistics.
// The returned object must be destroyed with StructDestroy()
SA_RET_NN_VALID AccountStats *GetAccountStats(void)
{
	AccountStats *result;

	PERFINFO_AUTO_START_FUNC();

	result = StructCreate(parse_AccountStats);
	result->enabled = ReportingEnabled();
	if (ReportingEnabled())
	{
		CONST_EARRAY_OF(TotalPointsBalanceContainer) balances = asgGetTotalAccountsPointsBalance();
		bool bFullDebug = false;
		bool bWin64 = false;

#ifdef _FULLDEBUG
		bFullDebug = true;
#endif
#ifdef _WIN64
		bWin64 = true;
#endif

		// General statistics
		result->numAccounts = getAccountCount();
		result->SlowLogins = gSlowLoginCounter;
		GetKeyCounts(&result->KeyCounts);

		// Build information
		result->AppType = GlobalTypeToName(GetAppGlobalType());
		result->ProductionVersion = ProdVersion();
		result->UsefulVersion = GetUsefulVersionString();
		result->BuildVersion = gBuildVersion;
		result->BuildBranch = gBuildBranch;
		result->BuildFullDebug = bFullDebug;
		result->BuildWin64Bit = bWin64;
		result->ProxyProtocolVersion = ACCOUNT_PROXY_PROTOCOL_VERSION;

		// Configuration information
		result->ProductionMode = isProductionMode();
		result->BillingConfig = StructClone(parse_BillingConfiguration, billingGetConfig());
		devassert(result->BillingConfig);
		result->BillingConfig->vindiciaPassword = NULL;

		// Performance statistics
		result->LoginsLast60Secs = timingHistoryInLastInterval(gLoginHistory, 60.0);
		result->Last10000Logins = timingHistoryForLastCount(gLoginHistory, 10000);
		result->MostLoginsInSec = timingHistoryMostInInterval(gLoginHistory, 1.0);
		result->ShortestTime100Logins = timingHistoryShortestForCount(gLoginHistory, 100);
		result->UniqueLoginsHourlyCurrent = baUniqueLoginsHourlyCurrent ? baCountSetBits(baUniqueLoginsHourlyCurrent) : 0;
		result->UniqueLoginsHourlyPrevious = baUniqueLoginsHourlyPreviousCount;
		result->QueuedTransactionCount = btGetQueuedTransactionCount();
		result->IdleTransactionCount = btGetIdleTransactionCount();
		result->ActiveTransactionCount = btGetActiveTransactionCount();
		result->CompletedTransactionCount = btGetCompletedTransactionCount();
		result->LastIdleStart = btLastIdleTransactionQueueFill();
		result->LastIdleComplete = btLastIdleTransactionQueueCompletion();
		result->LastIdleLength = btLastIdleTransactionQueueCompletionLength();

		// Tracked statistics
		result->global = asgGetGlobalStats();
		result->KeyActivations = asgGetKeyActivations();
		result->KeyActivationsDailyCurrent = asgGetKeyActivationsDailyCurrent();
		result->KeyActivationsDailyPrevious = asgGetKeyActivationsDailyPrevious();
		result->PlayTime = asgGetPlayTime();
		CopyPurchases(&result->Purchases, asgGetPurchaseStats());
		CopyPurchases(&result->PurchasesHourlyCurrent, asgGetPurchaseStatsHourlyCurrent());
		CopyPurchases(&result->PurchasesHourlyPrevious, asgGetPurchaseStatsHourlyPrevious());
		EARRAY_CONST_FOREACH_BEGIN(balances, i, n);
			int j = eaPush(&result->TotalAccountsPointsBalance, StructCreate(parse_TotalPointsBalance));
			devassert(i == j);
			estrCurrency(&result->TotalAccountsPointsBalance[i]->Currency, moneyContainerToMoneyConst(&balances[i]->TotalBalances));
			estrFromMoneyRaw(&result->TotalAccountsPointsBalance[i]->TotalBalances, moneyContainerToMoneyConst(&balances[i]->TotalBalances));
			result->TotalAccountsPointsBalance[i]->AccountsWithPoints = balances[i]->AccountsWithPoints;
		EARRAY_FOREACH_END;
		result->TransactionCodes = asgGetTransactionCodes();
		eaCopyStructs(&authTransactions, &result->AuthTransactions, parse_AuthTransaction);

		// Miscellaneous information
		UpdateBatchNamesIfNecessary();
		result->BatchNames = gppBatchNames;

		// Perfect World stats
		result->PerfectWorldUsefulUpdates = AccountIntegration_GetHourlyUsefulUpdateCount();
		result->PerfectWorldUselessUpdates = AccountIntegration_GetHourlyUselessUpdateCount();
		result->PerfectWorldFailedUpdates = AccountIntegration_GetHourlyFailedUpdateCount();
	}

	PERFINFO_AUTO_STOP_FUNC();
	return result;
}

// Reset tracked statistics.
void ResetAccountStats(void)
{
	PERFINFO_AUTO_START_FUNC();

	// Reset unique logins.
	if (baUniqueLoginsHourlyCurrent)
		baClearAllBits(baUniqueLoginsHourlyCurrent);
	baUniqueLoginsHourlyPreviousCount = 0;

	// Reset PerfectWorld Update counts
	AccountIntegration_ResetHourlyUpdateCounts();

	asgResetStats();
	ScanAndUpdateStats();
	asgSetGlobalStatsLastReset(timeSecondsSince2000());

	// Reset billing authorization transaction buffer.
	eaDestroyStruct(&authTransactions, parse_AuthTransaction);

	PERFINFO_AUTO_STOP_FUNC();
}

// Return true if reporting is currently enabled.
bool ReportingEnabled(void)
{
	return !gbReportingDisabled;
}

// Enable or disable reporting.
void SetReportingStatus(bool bEnabled)
{
	U32 now;

	// Do nothing if this isn't a change of state.
	if (gbReportingDisabled == !bEnabled)
		return;
	
	PERFINFO_AUTO_START_FUNC();

	// Record reporting status change.
	now = timeSecondsSince2000();
	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Statistics reporting %s.", bEnabled ? "enabled" : "disabled");
	if (bEnabled)
		uLastEnabled = now;
	else
		uLastDisabled = now;
	UpdateStatus();

	// Enable or disable.
	gbReportingDisabled = !bEnabled;

	// If statistics are being re-enabled, update tracked statistics.
	if (ReportingEnabled())
		ScanAndUpdateStats();

	PERFINFO_AUTO_STOP_FUNC();
}

static void ScanAndUpdateStatsQuick(void);

// Perform periodic tasks.
// This function should run fairly rapidly in all cases, and only execute constant-time algorithms.
void ReportingTick()
{
	static bool bFirstRun = true;
	U32 now, nowHour, nowDay;

	PERFINFO_AUTO_START_FUNC();
	coarseTimerAddInstance(NULL, __FUNCTION__);

	// Send any pending activities to Vindicia, even if reporting is disabled.
	(void) btSendActivities();

	// Don't perform scan if reporting is disabled.
	if (!ReportingEnabled())
	{
		coarseTimerStopInstance(NULL, __FUNCTION__);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// If this is the first run, perform some initialization and perform an AccountDB scan.
	if (bFirstRun)
	{
		gLoginHistory = timingHistoryCreate(giLoginHistoryCount);
		ScanAndUpdateStatsQuick();
		bFirstRun = false;
	}

	// Update status if necessary.
	UpdateStatus();

	// Update hourly statistics.
	now = timeSecondsSince2000();
	nowHour = asgFloorToHour(now);
	if (nowHour > asgGetGlobalStatsHourlyCurrentStart())
	{
		// Rotate hourly unique logins.
		if (baUniqueLoginsHourlyCurrent)
		{
			baUniqueLoginsHourlyPreviousCount = baCountSetBits(baUniqueLoginsHourlyCurrent);
			baClearAllBits(baUniqueLoginsHourlyCurrent);
		}
		// Reset PerfectWorld Update counts
		AccountIntegration_ResetHourlyUpdateCounts();

		// Rotate persisted hourly statistics.
		asgRotateStatsHourly(nowHour);
	}

	// Update daily statistics.
	nowDay = asgFloorToDay(now);
	if (nowDay > asgGetGlobalStatsDailyCurrentStart())
		asgRotateStatsDaily(nowDay);

	coarseTimerStopInstance(NULL, __FUNCTION__);
	PERFINFO_AUTO_STOP_FUNC();
}

// Scan all account data to update the global account statistics.  Do not rescan persistent statistics that should still be current.
static void ScanAndUpdateStatsQuick()
{
	PERFINFO_AUTO_START_FUNC();

	// Don't perform scan if reporting is disabled.
	if (!ReportingEnabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Scan all account data to update the global account statistics.
void ScanAndUpdateStats()
{
	Container *con;
	ContainerIterator iter = {0};

	PERFINFO_AUTO_START_FUNC();

	// Don't perform scan if reporting is disabled.
	if (!ReportingEnabled())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	loadstart_printf("Scanning statistics...");

	// Do quick scan.
	ScanAndUpdateStatsQuick();

	// Start from scratch.
	asgResetTotalAccountsPointsBalance();

	// Accumulate the points balances.
	objInitContainerIteratorFromType(GLOBALTYPE_ACCOUNT, &iter);
	con = objGetNextContainerFromIterator(&iter);
	while (con)
	{
		AccountInfo *account = (AccountInfo *) con->containerData;
		STRING_EARRAY eaKeys = AccountKeyValue_GetAccountKeyList(account);
		
		EARRAY_CONST_FOREACH_BEGIN(eaKeys, iCurKey, iNumKeys);
		{
			const char *pKey = eaKeys[iCurKey];
			bool locked = AccountKeyValue_IsLocked(account, pKey);

			if (!locked)
			{
				S64 iValue = 0;
				if (AccountKeyValue_Get(account, pKey, &iValue) == AKV_SUCCESS)
				{
					accountReportKeyValue(account, pKey, 0, iValue, false);
				}
			}
		}
		EARRAY_FOREACH_END;

		AccountKeyValue_DestroyAccountKeyList(&eaKeys);

		con = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
	loadend_printf("done.");

	PERFINFO_AUTO_STOP_FUNC();
}

int GetPurchaseLogCount(U32 uAccountID)
{
	ObjectIndexKey key = {0};
	ObjectIndexIterator iter = {0};
	ContainerStore *pStore = objFindContainerStoreFromType(GLOBALTYPE_ACCOUNTSERVER_PURCHASE_LOG);
	PurchaseLogContainer *pPurchaseLog = NULL;
	int count = 0;

	objIndexInitKey_Int(gidx_PurchaseLogByAccountID, &key, uAccountID);
	objIndexObtainReadLock(gidx_PurchaseLogByAccountID);
	objIndexGetIteratorFrom(gidx_PurchaseLogByAccountID, &iter, ITERATE_FORWARD, &key, 0);

	while ((pPurchaseLog = objIndexGetNextContainerData(&iter, pStore)) && pPurchaseLog->uAccountID == uAccountID)
		++count;

	objIndexReleaseReadLock(gidx_PurchaseLogByAccountID);
	objIndexDeinitKey_Int(gidx_PurchaseLogByAccountID, &key);
	return count;
}

// Destroy all the purchase logs for a given account ID
void DestroyPurchaseLogs(U32 uAccountID)
{
	ObjectIndexIterator iter;
	PurchaseLogContainer *purchase;
	bool success = false;
	ContainerStore *store = objFindContainerStoreFromType(GLOBALTYPE_ACCOUNTSERVER_PURCHASE_LOG);
	ObjectIndexKey searchKey = {0};
	U32 *eaiToDestroy = NULL;

	if (!uAccountID)
		return;
	// Get all matching purchases.
	objIndexInitKey_Int(gidx_PurchaseLogByAccountID, &searchKey, uAccountID);
	objIndexObtainReadLock(gidx_PurchaseLogByAccountID);
	success = objIndexGetIteratorFrom(gidx_PurchaseLogByAccountID, &iter, ITERATE_REVERSE, &searchKey, 0);

	while (success && (purchase = objIndexGetNextContainerData(&iter, store)) && (uAccountID == purchase->uAccountID))
	{
		eaiPush(&eaiToDestroy, purchase->uID);
	}

	objIndexReleaseReadLock(gidx_PurchaseLogByAccountID);
	objIndexDeinitKey_Int(gidx_PurchaseLogByAccountID, &searchKey);

	EARRAY_INT_CONST_FOREACH_BEGIN(eaiToDestroy, i, n);
	{
		objRequestContainerDestroyLocal(NULL, GLOBALTYPE_ACCOUNTSERVER_PURCHASE_LOG, eaiToDestroy[i]);
	}
	EARRAY_FOREACH_END;
	eaiDestroy(&eaiToDestroy);
}

AUTO_COMMAND ACMD_CATEGORY(Account_Debug);
void DumpLoginStats(const char * pFilename)
{
	timingHistoryDumpToFile(gLoginHistory, 1, pFilename);
}

#define parse_GlobalAccountStats parse_GlobalAccountStatsContainer
#include "AccountReporting_h_ast.c"
#include "AccountReporting_c_ast.c"
