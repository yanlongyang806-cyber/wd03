#ifndef ACCOUNTTRANSACTIONLOG_H
#define ACCOUNTTRANSACTIONLOG_H

typedef struct AccountInfo AccountInfo;
typedef struct Money Money;
typedef struct MoneyContainer MoneyContainer;
typedef struct ProductContainer ProductContainer;
typedef struct ProductKeyValueChangeContainer ProductKeyValueChangeContainer;
typedef struct PurchaseLog PurchaseLog;
typedef enum TransactionProvider TransactionProvider;
typedef enum TransactionLogType TransactionLogType;

AST_PREFIX(PERSIST)
AUTO_STRUCT AST_CONTAINER;
typedef struct TransactionLogKeyValueChange
{
	CONST_STRING_POOLED pKey;
	const S64 iOldValue;
	const S64 iNewValue;
} TransactionLogKeyValueChange;

AUTO_STRUCT AST_CONTAINER;
typedef struct TransactionLogItem
{
	const U32 uProductID;
	CONST_OPTIONAL_STRUCT(MoneyContainer) pPrice;
} TransactionLogItem;

// A single log of a transaction, either a purchase with currency or a grant/refund of currency
AUTO_STRUCT AST_CONTAINER;
typedef struct TransactionLogContainer
{
	// Primary / foreign keys
	const U32 uID;								AST(KEY)
	const U32 uAccountID;
	CONST_STRING_MODIFIABLE pTransactionID;

	// When and where it was purchased
	const U32 uTimestampSS2000;
	const TransactionProvider eProvider;
	CONST_STRING_MODIFIABLE pSource;

	// External foreign keys (i.e. outside the Account Server)
	CONST_STRING_MODIFIABLE pMerchantTransactionID;
	CONST_STRING_MODIFIABLE pMerchantOrderID;

	// What type of transaction, and any key-values that were changed
	const TransactionLogType eTransactionType;
	CONST_EARRAY_OF(TransactionLogKeyValueChange) eaKeyValueChanges;
	CONST_EARRAY_OF(ProductKeyValueChangeContainer) eaLegacyKeyValueChanges;

	// If it was a purchase - what items were purchased, for how much
	CONST_EARRAY_OF(TransactionLogItem) eaPurchasedItems;
	CONST_OPTIONAL_STRUCT(MoneyContainer) pPriceTotal;

	// If it was just KV changes - why the changes occurred
	CONST_STRING_MODIFIABLE pKeyValueChangeReason;
} TransactionLogContainer;

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountTransactionLogContainer
{
	const U32 uID;				AST(KEY)
	const U32 uNextLogID;
	const U32 uNextMigrateID;
	CONST_EARRAY_OF(TransactionLogContainer) eaTransactions;
	CONST_EARRAY_OF(TransactionLogContainer) eaPendingTransactions;

	AST_COMMAND("Apply Transaction","ServerMonTransactionOnEntity AccountServer_TransactionLog $FIELD(UID) $STRING(Transaction String)")
} AccountTransactionLogContainer;
AST_PREFIX()

AUTO_STRUCT;
typedef struct TransactionLogKeyValueChangeSet
{
	const TransactionLogKeyValueChange * const * eaChanges;
} TransactionLogKeyValueChangeSet;

// Startup initialization
void InitializeAccountTransactionLog(void);
void ScanAccountTransactionLog(void);

// Create a unique identifier for a transaction
char *AccountTransactionCreateNewID(void);

// Open a new transaction log with the following overarching details - should be populated with other functions below
U32 AccountTransactionOpen(U32 uAccountID, TransactionLogType eType, const char *pSource, TransactionProvider eProvider, const char *pMerchantTransactionID, const char *pOrderID);

// Commit the transaction log - after this, it can no longer be changed, and it's fit to be reported on
void AccountTransactionFinish(U32 uAccountID, U32 uLogID);

// Fixup a transaction log container - use only if a container was erroneously created with uNextLogID = 1 when there were purchase logs to migrate
void AccountTransactionFixupMigration(U32 uAccountID, U32 uPurchaseLogCount);

// Same as above, but does a managed return val for you
typedef struct TransactionReturnVal TransactionReturnVal;
typedef void (*TransactionReturnCallback)(TransactionReturnVal *returnVal, void *userData);
void AccountTransactionFixupMigrationReturn(U32 uAccountID, U32 uPurchaseLogCount, TransactionReturnCallback pCallback, void *pUserdata);

// Attach a product purchase to a particular transaction
void AccountTransactionRecordPurchase(U32 uAccountID, U32 uLogID, U32 uProductID, const Money *pPrice);

// Record a set of key-value changes, with an optional qualitative reason string
void AccountTransactionRecordKeyValueChanges(U32 uAccountID, U32 uLogID, CONST_EARRAY_OF(TransactionLogKeyValueChange) eaChanges, const char *pReason);

// Populate a list of key-value changes, which can later be reported using the above function; use the free function to destroy when done
void AccountTransactionGetKeyValueChanges(AccountInfo *pAccount, CONST_STRING_EARRAY eaKeys, TransactionLogKeyValueChange ***eaChanges);
void AccountTransactionFreeKeyValueChanges(TransactionLogKeyValueChange ***eaChanges);

// For migration - hooks for AccountReporting to call back into the new module to migrate logs correctly
U32 AccountTransactionGetMigrateIDForAccount(U32 uAccountID);
void AccountTransactionMigrateComplete(U32 uAccountID, U32 uLogID);

// LEGACY SUPPORT for GetPurchaseLogEx
EARRAY_OF(PurchaseLog) AccountTransactionGetPurchaseLog(U32 uSinceSS2000, U64 uMaxResponses, U32 uAccountID);

// Get the transaction logs for a given account
EARRAY_OF(TransactionLogContainer) AccountTransactionGetLog(U32 uSinceSS2000, U64 uMaxResponses, U32 uAccountID);

// Get the products associated with the transaction for a given MT ID
EARRAY_OF(const ProductContainer) AccountTransactionGetProductsByMTID(SA_PARAM_NN_STR const char *pMerchantTransactionId);

// Get the source of a transaction for a given order ID
SA_RET_OP_STR const char *AccountTransactionGetOrderIDSource(SA_PARAM_NN_STR const char *pOrderId);

#endif // ACCOUNTTRANSACTIONLOG_H