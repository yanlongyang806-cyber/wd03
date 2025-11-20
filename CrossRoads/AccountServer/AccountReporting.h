#ifndef ACCOUNTREPORTING_H
#define ACCOUNTREPORTING_H

#include "AccountReporting_h_ast.h"

/* Testing reporting.
 *		1. Populate all parts of http://localhost:8090/stats by triggering logging hooks.
 *		2. Verify XML-RPC Stats().
 *		3. Verify that ResetAccountStatistics works.
 *		4. Verify that all of the above still work with setDisableReporting 1.
 */

/************************************************************************/
/* Logging hooks                                                        */
/************************************************************************/

// Forward declarations for structures related to report logging.
typedef struct AccountInfo AccountInfo;
typedef struct BillingConfiguration BillingConfiguration;
typedef struct CurrencyPurchasesContainer CurrencyPurchasesContainer;
typedef struct GlobalAccountStatsContainer GlobalAccountStatsContainer;
typedef struct PlayTimeContainer PlayTimeContainer;
typedef struct ProductContainer ProductContainer;
typedef struct ProductKeyValueChangeContainer ProductKeyValueChangeContainer;
typedef struct TotalPointsBalance TotalPointsBalance;
typedef struct TransactionCodesContainer TransactionCodesContainer;
typedef struct U64Container U64Container;
typedef struct Money Money;
typedef enum TransactionProvider TransactionProvider;

// Report the results of a transaction authorization attempt.
void accountReportAuthAttempt(bool bSuccess, U32 uTransId, char *pWebUid, char *pResultString, U32 uAccountId);

// Record key-value changes
// Pass bActualActivity as true only if this is an actual change due to an activity; pass false if its part of a general updating pass.
void accountReportKeyValue(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *key, S64 oldValue,
						   S64 newValue, bool bActualActivity);

// Report login.
void accountReportLogin(SA_PARAM_NN_VALID const AccountInfo *pAccount, U32 uIp);

// Record play time on an account.
void accountReportLogoutPlayTime(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pProductName,
								 SA_PARAM_NN_STR const char *pShardCategory, U32 uPlayTime);

// Report a number of keys as being activated.
void accountReportProductKeyActivate(int iBatchId, U64 change);

// Record a change in activated product keys.
// Note: To report activations, the above function should be used.
void accountReportProductKeyActivatedChange(int iBatchId, S64 change);

// Record each purchase
void accountReportPurchase(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_VALID const ProductContainer *pProduct, SA_PARAM_NN_VALID const Money *pPrice);

// Record transaction code responses.
void accountReportTransactionCodes(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pCurrency, SA_PARAM_OP_STR const char *pAuthCode,
								   SA_PARAM_OP_STR const char *pAvsCode, SA_PARAM_OP_STR const char *pCvnCode);

/************************************************************************/
/* Statistics reporting                                                 */
/************************************************************************/

/* Checklist for adding a new statistic
 *		1. Add as a member or substructure member of ASGlobalData in GlobalData.c.
 *		2. Add accessor prototypes in GlobalData.h.
 *		3. Add accessor macro or function in GlobalData.c.
 *		4. Tracked: Add or update a logging hook in AccountReporting.h and AccountReporting.c that populates the statistic.
 *		5. Tracked: Make sure the statistic is being updated properly on startup, if necessary.
 *		6. Add to AccountStats in AccountReporting.h.
 *		7. Make sure its being copied or calculated correctly in GetAccountStats().
 *		8. Make sure its being reset by ResetAccountStats().
 *		9. Add to web interface table in wiStats() in WebInterface.c.
 */

// For each currency, locale, and product, track total purchases
AUTO_STRUCT;
typedef struct CurrencyPurchases
{
	char *Locale;													AST(ESTRING)	// Locale of users making transaction
	char *Product;													AST(ESTRING)	// Product
	U64 PurchaseCount;																// Sum of purchases in this currency
	char *TotalPurchases;											AST(ESTRING)	// Total number of purchases in this currency
	char *Currency;													AST(ESTRING)	// Currency
} CurrencyPurchases;

// For each points type, number of accounts with this currency, and total balance of those accounts
AUTO_STRUCT;
struct TotalPointsBalance
{
	char *Currency;													AST(ESTRING)	// Points type
	char *TotalBalances;											AST(ESTRING)	// Purchases in this points type
	U32 AccountsWithPoints;															// Number of accounts with a balance in this type
};

// For each batch, information about the number of activated and available keys.
AUTO_STRUCT;
typedef struct BatchKeyCounts {
	U64 ActivatedKeys;										// Current number of activated keys
	U64 AvailableKeys;										// Current number of available keys
} BatchKeyCounts;

// Record of a billing transaction requesting authorization.
AUTO_STRUCT;
typedef struct AuthTransaction
{
	bool bSuccess;																	// True if the transaction succeeded
	U32 uTransId;																	// The logging transaction ID associated with this transaction.
	char *pWebUid;																	// The website ID associated with this transaction, if any.
	char *pResultString;															// The result string from this transaction, if any.
	U32 uAccountId;																	// The account ID associated with transaction, if any.
	U32 uRecorded;																	// SS2000 of transaction recording
} AuthTransaction;

// General account statistics
AUTO_STRUCT;
typedef struct AccountStats
{
	// Enabled status: true if reporting is currently enabled
	bool enabled;									

	// Instantaneously-calculated statistics
	//		General statistics
	U32 numAccounts;																// Number of accounts
	int SlowLogins;																	// Number of slow logins
	EARRAY_OF(BatchKeyCounts) KeyCounts;											// New and used keys, by batch

	//		Build information
	const char *AppType;											AST(UNOWNED)	// Global app type
	const char *ProductionVersion;									AST(UNOWNED)	// For production builds, patch version
	const char *UsefulVersion;										AST(UNOWNED)	// Generic version
	int BuildVersion;																// AccountServer build version
	const char *BuildBranch;										AST(UNOWNED)	// AccountServer build branch
	bool BuildWin64Bit;																// True if built for Win64.
	bool BuildFullDebug;															// True if compiled in full debug
	int ProxyProtocolVersion;														// Proxy protocol version

	//		Configuration information
	bool ProductionMode;															// False in development mode, true in production mode
	BillingConfiguration *BillingConfig;											// Active billing configuration

	//		Performance statistics
	U32 LoginsLast60Secs;															// Number of logins in last 60 seconds
	float Last10000Logins;															// Total time that last 10000 logins took
	U32 MostLoginsInSec;															// All time highest logins in one second
	float ShortestTime100Logins;													// All time shortest time for any 100 logins
	U32 UniqueLoginsHourlyCurrent;													// Unique logins during this hour
	U32 UniqueLoginsHourlyPrevious;													// Unique logins during the previous hour
	int QueuedTransactionCount;														// Number of queued transactions
	int IdleTransactionCount;														// Number of idle transactions
	int ActiveTransactionCount;														// Number of active transactions
	int CompletedTransactionCount;													// Number of completed transaction
	U32 LastIdleStart;																// When did we last fill spAccountIDUpdateQueue?
	U32 LastIdleComplete;															// When did spAccountIDUpdateQueue completely empty last?
	U32 LastIdleLength;																// Last idle completion time

	// Tracked statistics
	const GlobalAccountStatsContainer *global;						AST(UNOWNED)	// General tracked global statistics
	U64Container *const *KeyActivations;							AST(UNOWNED)	// Number of keys activated, by batch
	U64Container *const *KeyActivationsDailyCurrent;				AST(UNOWNED)	// Number of keys activated, by batch, today
	U64Container *const *KeyActivationsDailyPrevious;				AST(UNOWNED)	// Number of keys activated, by batch, yesterday
	PlayTimeContainer *const *PlayTime;								AST(UNOWNED)	// Play time
	EARRAY_OF(CurrencyPurchases) Purchases;											// Total purchases
	EARRAY_OF(CurrencyPurchases) PurchasesHourlyCurrent;							// Purchases during this hourly period
	EARRAY_OF(CurrencyPurchases) PurchasesHourlyPrevious;							// Purchases during the last hourly period
	EARRAY_OF(TotalPointsBalance) TotalAccountsPointsBalance;						// Points balances of all accounts
	TransactionCodesContainer *const *TransactionCodes;				AST(UNOWNED)	// Distribution of transaction response codes
	EARRAY_OF(AuthTransaction) AuthTransactions;									// Recent billing authorization transactions

	// Miscellaneous information
	const char **BatchNames;										AST(UNOWNED)	// List of batch names

	// Perfect World stats
	U32 PerfectWorldUsefulUpdates;
	U32 PerfectWorldUselessUpdates;
	U32 PerfectWorldFailedUpdates;
} AccountStats;

// Entry in the purchase log.
// This is a simplified version of PurchaseLogContainer designed to be used for XML-RPC and general reporting.
AUTO_STRUCT;
typedef struct PurchaseLog
{
	U32 uID;
	const char *pAccount;											AST(UNOWNED)
	const char *pProduct;											AST(UNOWNED)
	char *pPrice;													AST(ESTRING)
	const char *pCurrency;											AST(UNOWNED)
	U32 uTimestampSS2000;
	const char *pMerchantTransactionId;								AST(UNOWNED)
	const ProductKeyValueChangeContainer * const * eaKeyValues;		AST(UNOWNED)
	const char *pOrderID;											AST(UNOWNED)
	TransactionProvider eProvider;
} PurchaseLog;

// Get all global statistics.
// The returned object must be destroyed with StructDestroy()
SA_RET_NN_VALID AccountStats *GetAccountStats(void);

// Reset tracked statistics.
void ResetAccountStats(void);

// Return true if reporting is currently enabled.
bool ReportingEnabled(void);

// Enable or disable reporting.
void SetReportingStatus(bool bEnabled);

// Perform periodic tasks.
// This function should run fairly rapidly in all cases, and only execute constant-time algorithms.
void ReportingTick(void);

// Scan all account data to update the global account statistics.
void ScanAndUpdateStats(void);

// Find out if a key-value is considered "interesting" for recording purposes
bool KeyIsInteresting(const char *pKey);

/************************************************************************/
/* Purchase logging                                                     */
/************************************************************************/

// Get the purchase log.
EARRAY_OF(PurchaseLog) GetPurchaseLog(U32 uSinceSS2000, U64 uMaxResponses, U32 uAccountID);

// Get the count of purchase logs.
int GetPurchaseLogCount(U32 uAccountID);

// Initialize purchase log stuff
void InitializePurchaseLog(void);

// Get the products associated with a merchant transaction ID.
EARRAY_OF(const ProductContainer) GetProductsFromMTID(SA_PARAM_NN_STR const char *pMerchantTransactionId);

// Get the source of a specified third-party order ID.
SA_RET_OP_STR const char *GetPurchaseOrderIDSource(SA_PARAM_NN_STR const char *pOrderId);

// Destroy all the purchase logs for a given account ID
void DestroyPurchaseLogs(U32 uAccountID);

// Migrate purchase logs to new transaction log
void PurchaseLogMigrationTick(void);

// Reconstruct a purchase log that should have been migrated but wasn't
void FixupMigratedPurchaseLog(U32 uAccountID,
	U32 uProductID,
	const char *pSource,
	const char *strPrice,
	const char *pCurrency,
	U32 uTimestampSS2000,
	SA_PARAM_OP_STR const char *pOrderID,
	SA_PARAM_OP_STR const char *merchantTransactionId,
	TransactionProvider eProvider);

#endif  // ACCOUNTREPORTING_H
