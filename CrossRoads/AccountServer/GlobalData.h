#pragma once

#include "Money.h"

#include "GlobalData_h_ast.h"

typedef enum enumPasswordVersion enumPasswordVersion;

AST_PREFIX(PERSIST)

// AccountReporting.h: For each currency, locale, and product, track total purchases.
AUTO_STRUCT AST_CONTAINER;
typedef struct CurrencyPurchasesContainer
{
	CONST_STRING_MODIFIABLE Locale;						AST(ESTRING)	// Locale of users making transaction
	CONST_STRING_MODIFIABLE Product;					AST(ESTRING)	// Product
	const U64 PurchaseCount;											// Sum of purchases in this currency
	MoneyContainer TotalPurchases;										// Total number of purchases in this currency
} CurrencyPurchasesContainer;

// AccountReporting.h For each locale, product, and shard, record total number of logouts and playtime.
// Playtime is recorded at log out.
AUTO_STRUCT AST_CONTAINER;
typedef struct PlayTimeContainer
{
	CONST_STRING_MODIFIABLE Locale;						AST(ESTRING)	// Locale for this play time category.
	CONST_STRING_MODIFIABLE Product;					AST(ESTRING)	// Product for this play time category.
	CONST_STRING_MODIFIABLE Shard;						AST(ESTRING)	// Shard that being played on
	const U64 TotalPlayTime;											// Sum of play time for all users
	const U64 TotalLogouts;												// Number of log outs.
} PlayTimeContainer;

// AccountReporting.h: For each points type, number of accounts with this currency, and total balance of those accounts.
AUTO_STRUCT AST_CONTAINER;
typedef struct TotalPointsBalanceContainer
{
	MoneyContainer TotalBalances;										// Total number of purchases in this currency
	const U32 AccountsWithPoints;										// Sum of purchases in this currency
} TotalPointsBalanceContainer;

// AccountReporting.h: Count a particular combination of locale, currency, authorization code, AVS code, and CVN code.
AUTO_STRUCT AST_CONTAINER;
typedef struct TransactionCodesContainer {
	CONST_STRING_MODIFIABLE Locale;						AST(ESTRING)
	CONST_STRING_MODIFIABLE Currency;					AST(ESTRING)
	CONST_STRING_MODIFIABLE AuthCode;					AST(ESTRING)
	CONST_STRING_MODIFIABLE AvsCode;					AST(ESTRING)
	CONST_STRING_MODIFIABLE CvnCode;					AST(ESTRING)
	const U64 Count;													// Number of instances of this combination.
} TransactionCodesContainer;

// FetchDelta type
typedef enum FetchDeltaType {
	FDT_ENTITLEMENTS = 0,
	FDT_AUTOBILLS,
} FetchDeltaType;

// Used to store FetchDelta information
AUTO_STRUCT AST_CONTAINER;
typedef struct FetchDeltaStatsContainer
{
	const U32 Time;
	const U64 NumProcessed;
	const U64 NumTotal;
	const U32 StartedTime;
	const U32 FinishedTime;
	const U64 PageSize;
	CONST_STRING_EARRAY Errors;							AST(ESTRING)
} FetchDeltaStatsContainer;

#define parse_FetchDeltaStats parse_FetchDeltaStatsContainer
typedef NOCONST(FetchDeltaStatsContainer) FetchDeltaStats;

// AccountReporting.h: Global statistics
// PurchaseStatsHourlyCurrentStart, PurchaseStatsHourlyPreviousStart: These can be deleted when AccountServer 2.0 is no longer in use.
// ActivatedKeys, AvailableKeys, ActivatedKeysTotal, ActivatedKeysToday, ActivatedKeysYesterday: These can be deleted when 2.1 is no longer in use.
AUTO_STRUCT AST_CONTAINER AST_IGNORE(ActivatedKeysTodayStart) AST_IGNORE(ActivatedKeysYesterdayStart)
AST_IGNORE(ActivatedKeys) AST_IGNORE(AvailableKeys) AST_IGNORE(ActivatedKeysTotal) AST_IGNORE(ActivatedKeysToday) AST_IGNORE(ActivatedKeysYesterday);
typedef struct GlobalAccountStatsContainer {

	// Tracked statistics
	const U32 LastReset;												// SecsSince2000 of last statistics reset
	const U32 LastEnabled;												// Last time statistics reporting was enabled
	const U32 LastDisabled;												// Last time statistics reporting was disabled
	const U32 HourlyCurrentStart;										// Start of this hour in SecsSince2000
	const U32 HourlyPreviousStart;										// Start of previous hour in SecsSince2000
	const U32 DailyCurrentStart;										// Start of today in SecsSince2000
	const U32 DailyPreviousStart;										// Start of yesterday in SecsSince2000
	const U64 TotalPurchases;											// Count of total purchases transactions
	const U64 TotalLogins;												// Total number of logins since reset

	// FetchDelta statistics
	const FetchDeltaStatsContainer AutoBillFetchDelta;					// Used for the FetchDelta auto-bill stuff
	const FetchDeltaStatsContainer EntitlementFetchDelta;				// Used for the FetchDelta entitlement stuff
} GlobalAccountStatsContainer;

// Work-around for the apparent inability to persist an EArray of U64s.
AUTO_STRUCT AST_CONTAINER;
typedef struct U64Container
{
	const U64 uValue;
} U64Container;

// Product key lock
AUTO_STRUCT AST_CONTAINER;
typedef struct LockedKey
{
	CONST_STRING_MODIFIABLE keyName;									// Name of locked key
	const U32 uLockTime;												// Time that lock was set
} LockedKey;

//key-verification strings. At startup time, if the AS is encrypting hashed passwords, it loads up one or more (multiple only
//while transitioning between encryption keys) encryption keys, then uses each one to encrypt a constant string, and stores the
//encrypted output here. This verifies that the encryption key on disk hasn't been corrupted, which would of course be a total
//catastrophe. The existence and membership of this container array is also how the account server remembers whether
//it's using encryption, what key version it thinks is the newest, etc.
AUTO_STRUCT AST_CONTAINER;
typedef struct EncryptionKeyVerificationString
{
	const int iKeyIndex; AST(KEY)
	CONST_STRING_MODIFIABLE encodedStringForComparing; AST(ESTRING) //64-bit encode of encrypted constant string
} EncryptionKeyVerificationString;


AST_PREFIX()

// Initialize the schema
void asgRegisterSchema(void);

// Initialize the account server global data
void asgInitialize(void);

// Return true if the global data container has been initialized.
bool astInitialized(void);

// Set the last nightly walk time
void asgSetLastNightlyWalk(U32 uSecondsSince2000);

// Get the last nightly walk time
U32 asgGetLastNightlyWalk(void);

// Structures

// Example
//   AUTO_STRUCT AST_CONTAINER;
//   typedef struct SubstructureTypeName {
//   	CONST_STRING_MODIFIABLE str;					AST(ESTRING PERSIST)
//   } SubstructureTypeName;
//
//  void asgSetSubstructureNamestr(const char *str);
//  SubstructureTypeName *asgGetSubstructureName(void);
//  int asgGetSubstructureNamei(void);
//  const char *asgGetSubstructureNamestr(void);

/************************************************************************/
/* Statistics reporting                                                 */
/************************************************************************/

// Get the global statistics data.
const GlobalAccountStatsContainer *asgGetGlobalStats(void);

// Clear all statistics data.
void asgResetStats(void);

// Set the global statistics last reset time.
void asgSetGlobalStatsLastReset(U32 uSecondsSince2000);

// Get the global statistics last reset time.
U32 asgGetGlobalStatsLastReset(void);

// Set the global statistics last reset time.
void asgSetGlobalStatsLastEnabled(U32 uSecondsSince2000);

// Set the global statistics last reset time.
void asgSetGlobalStatsLastDisabled(U32 uSecondsSince2000);

// Add to the global statistics total purchase count.
void asgAddGlobalStatsTotalPurchases(U64 addend);

// Get the starting time of the current key activation day.
U32 asgGetGlobalStatsDailyCurrentStart(void);

// Add to daily activated key total.
void asgAddGlobalStatsActivatedKeysToday(U64 addend);

// Rotate today's stats into yesterday's stats.
void asgRotateStatsDaily(U32 uStarted);

// Get total number of logins.
U64 asgGetGlobalStatsTotalLogins(void);

// Add to total number of logins.
void asgAddGlobalStatsTotalLogins(U64 addend);

// Get purchase statistics.
CONST_EARRAY_OF(CurrencyPurchasesContainer) asgGetPurchaseStats(void);

// Add information for a purchase to the PurchaseStats array.
void asgAddPurchaseStatsItem(const char *pLocale, const char *pName, const Money *pAmount);

// Get purchase statistics for this hour.
CONST_EARRAY_OF(CurrencyPurchasesContainer) asgGetPurchaseStatsHourlyCurrent(void);

// Get the start of the current statistics hour.
U32 asgGetGlobalStatsHourlyCurrentStart(void);

// Get purchase statistics for the previous hour.
CONST_EARRAY_OF(CurrencyPurchasesContainer) asgGetPurchaseStatsHourlyPrevious(void);

// Get the start of the previous statistics hour.
U32 asgGetPurchaseStatsHourlyPreviousStart(void);

// Rotate HourlyCurrent to HourlyPrevious.
void asgRotateStatsHourly(U32 uStarted);

// Get total play time.
CONST_EARRAY_OF(PlayTimeContainer) asgGetPlayTime(void);

// Record some play time.
void asgAddPlayTime(SA_PARAM_OP_STR const char *pLocale, SA_PARAM_NN_STR const char *pProduct, SA_PARAM_NN_STR const char *pShard,
					U32 uPlayTime);

// Get points balance statistics.
CONST_EARRAY_OF(TotalPointsBalanceContainer) asgGetTotalAccountsPointsBalance(void);

// Reset points balance statistics.
void asgResetTotalAccountsPointsBalance(void);

// Update the total points balance.
void asgAddTotalAccountsPointsBalanceItem(SA_PARAM_NN_VALID const Money *pAmountChange, U64 uAccountChange);

// Get distribution of transaction response codes.
CONST_EARRAY_OF(TransactionCodesContainer) asgGetTransactionCodes(void);

// Update transaction response codes.
void asgAddTransactionCodes(SA_PARAM_OP_STR const char *pLocale, SA_PARAM_NN_STR const char *pCurrency, SA_PARAM_OP_STR const char *pAuthCode,
							SA_PARAM_OP_STR const char *pAvsCode, SA_PARAM_OP_STR const char *pCvnCode);

// Set the fetch delta stats
void asgSetFetchDeltaStats(FetchDeltaType eType, SA_PARAM_NN_VALID const FetchDeltaStats *pStats);

// Get count of activated and available keys.
CONST_EARRAY_OF(U64Container) asgGetKeyActivations(void);

// Get count of activated and available keys, for today.
CONST_EARRAY_OF(U64Container) asgGetKeyActivationsDailyCurrent(void);

// Get count of activated and available keys, for yesterday.
CONST_EARRAY_OF(U64Container) asgGetKeyActivationsDailyPrevious(void);

// Add to activated key total.
void asgAddKeyActivations(int iBatchId, S64 addend);

// Get locked key list.
CONST_EARRAY_OF(LockedKey) asgGetLockedKeys(void);

CONST_EARRAY_OF(EncryptionKeyVerificationString) asgGetEncryptionKeyVerificationStrings(void);

// Lock a key.
void asgAddKeyLock(const char *pKeyName, U32 uLockTime);

// Unlock a key.
bool asgRemoveKeyLock(const char *pKeyName);

enumPasswordVersion asgGetPasswordVersion(void);
U32 asgGetPasswordBackupCreationTime(void);
