#ifndef BILLING_H
#define BILLING_H

#include "vindicia.h"
#include "Product.h"
#include "GlobalData.h"
#include "SOAPInterface/VindiciaStructsStub.h"
#include "AccountServer.h"

typedef struct Money Money;

#define BILLING_DEBUG(format, ...) BillingDebug_dbg(__FILE__, __FUNCTION__, __LINE__, false, FORMAT_STRING_CHECKED(format), __VA_ARGS__)
#define BILLING_DEBUG_CONTINUE(format, ...) BillingDebug_dbg(__FILE__, __FUNCTION__, __LINE__, true, FORMAT_STRING_CHECKED(format), __VA_ARGS__)
#define BILLING_DEBUG_RESPONSE(method, response) BILLING_DEBUG("%s responded: [%d] '%s'\n", method, response->_return_->returnCode, response->_return_->returnString)
#define BILLING_DEBUG_START { consolePushColor(); \
							consoleSetFGColor(COLOR_GREEN|COLOR_BLUE); \
							BILLING_DEBUG("\nFiring up %s()\n", __FUNCTION__); \
							consolePopColor(); }
#define BILLING_DEBUG_WARNING(format, ...) BillingDebugWarning_dbg(__FILE__, __FUNCTION__, __LINE__, FORMAT_STRING_CHECKED(format), __VA_ARGS__)



void BillingDebug_dbg(SA_PARAM_NN_STR const char *file, SA_PARAM_NN_STR const char *func, int line, bool skipExtra, FORMAT_STR const char *format, ...);
void BillingDebugWarning_dbg(SA_PARAM_NN_STR const char *pFile, SA_PARAM_NN_STR const char *pFunction, int iLine, FORMAT_STR const char *pFormat, ...);

AUTO_ENUM;
typedef enum BillingServerType
{
	BillingServerType_Unknown = 0,
	BillingServerType_Official,
	BillingServerType_Development
} BillingServerType;

AUTO_STRUCT;
typedef struct FraudSettings
{
	char *billingCountry; AST(ESTRING)
	U32 minChargebackProbability;
	char *avsAccept; AST(ESTRING)
	char *cvnAccept; AST(ESTRING)
	U32 acceptMissing;
} FraudSettings;

AUTO_ENUM;
typedef enum BillingNightlyMode
{
	BillingNightlyMode_IdleTransactions = 0,
	BillingNightlyMode_FetchDelta,
	BillingNightlyMode_None
} BillingNightlyMode;

AUTO_STRUCT;
typedef struct DirectDebitMandateVersion
{
	char *billingCountry; AST(ESTRING KEY)
	char *version;		  AST(ESTRING)
} DirectDebitMandateVersion;

AUTO_STRUCT;
typedef struct Division
{
	char *name; AST(STRUCTPARAM)
	STRING_EARRAY productInternalNames; AST(NAME("ProductInternalName"))
	STRING_EARRAY subscriptionInternalNames; AST(NAME("SubscriptionInternalName"))
} Division;

AUTO_STRUCT;
typedef struct SpendingCap
{
	char *currency;
	float amount;
} SpendingCap;

// One config, indicating the live URL and sandbox URL for Steam's API, as well as the API version we're using
AUTO_STRUCT;
typedef struct SteamConfig
{
	char *liveURL;
	char *sandboxURL;
	U32 port; AST(DEF(4431))
	char *apiVersion;
} SteamConfig;

// One config per internal product game, giving the Steam app ID and API key
AUTO_STRUCT;
typedef struct SteamGameConfig
{
	char *name; AST(STRUCTPARAM KEY)
	U32 appID;
	char *apiKey;
} SteamGameConfig;

// Config struct indexed by shard proxy name, can contain other shard-specific settings besides just Steam stuff
AUTO_STRUCT;
typedef struct ShardConfig
{
	char *name; AST(STRUCTPARAM KEY)
	char *product;
	bool allowSteam;
	bool useSteamLive;
} ShardConfig;

AUTO_STRUCT;
typedef struct CurrencyDuplicate
{
	char *currency; AST(STRUCTPARAM KEY)
	STRING_EARRAY extraCurrencies; AST(STRUCTPARAM)
} CurrencyDuplicate;

// Account Server configuration
// These settings are stored in data/server/AccountServer/billingSettings.txt, and will be dynamically reloaded when this file changes.
// If any new settings are added to this file, please update wiStats() in WebInterface.c so they are printed on the Stats page.
AUTO_STRUCT;
typedef struct BillingConfiguration
{
	BillingServerType serverType;
	char *prefix; AST(ESTRING)

	//------------------------------------------------------------------------------
	U32 connectTimeoutSeconds;						AST(DEF(5))
	U32 connectAlertSeconds;						AST(DEF(3))
	U32 replyTimeoutSeconds;						AST(DEF(120))
	U32 replyAlertSeconds;							AST(DEF(60))
	U32 completedMaxAgeSeconds;						AST(DEF(6 * 60 * 60))
	U32 maxActivities;								AST(DEF(100))	// Maximum number of activities to queue
	U32 activityPeriod;								AST(DEF(10))	// Minimum time between each send to Vindicia (seconds)

	float authFailThreshold;						AST(DEF(0.5))
	U32 authFailPeriod;							AST(DEF(HOURS(2)))
	U32 authFailMinimumCount;						AST(DEF(10))
	float steamFailThreshold;						AST(DEF(0.5))
	U32 steamFailPeriod;							AST(DEF(HOURS(1)))
	U32 steamFailMinimumCount;						AST(DEF(5))

	U32 idleTransactionsQueuedOnStartup;			AST(DEF(0))
	U32 idleTransactionCountLimit;					AST(DEF(100))
	U32 idleTransactionsSkipNonBillingAccounts;
	U32 debug;

	char *validationProduct;						AST(ESTRING)	// Stores the name of the product used for payment method validation
	U32 deactivatePaymentMethodTries;				AST(DEF(3))

	U32 nightlyProcessingHour;						AST(DEF(1))

	char *stunnelHost;								AST(ESTRING DEF("localhost") ADDNAMES("vindiciaHost"))

	U32 vindiciaPort;								AST(DEF(4430))
	char *vindiciaLogin;							AST(ESTRING)
	char *vindiciaPassword;							AST(ESTRING)

	SteamConfig *steamConfig;
	EARRAY_OF(SteamGameConfig) steamGameConfigs;	AST(NAME("SteamGame"))
	EARRAY_OF(ShardConfig) shardConfigs;			AST(NAME("ShardConfig"))

	// Logging
	int vindiciaLogLevel;							AST(DEF(2))

	S32 entitlementBillingDateDelta;

	BillingNightlyMode nightlyMode;

	U32 fetchDeltaEntitlementPageSize;				AST(DEF(10))
	U32 fetchDeltaAutobillPageSize;					AST(DEF(10))

	EARRAY_OF(FraudSettings) fraudSettings;

	EARRAY_OF(DirectDebitMandateVersion) directDebitMandates;

	EARRAY_OF(Division) divisions; AST(NAME("Division"))

	U32 spendingCapPeriod;							AST(DEF(WEEKS(1)))
	EARRAY_OF(SpendingCap) spendingCap;

	STRING_EARRAY PurchaseLogKeys;					AST(ESTRING)	// Stores a list of keys to log purchase information about

	STRING_EARRAY ValidPWCurrencies;
	EARRAY_OF(CurrencyDuplicate) PWCurrencyDupe;
	//------------------------------------------------------------------------------

} BillingConfiguration;

// Get a copy of the active billing configuration.
SA_RET_NN_VALID const struct BillingConfiguration *billingGetConfig(void);

bool billingSetConfiguration(SA_PARAM_NN_VALID BillingConfiguration * pConfig);

AUTO_ENUM;
typedef enum BillingTransactionResult
{
	BTR_NONE = 0,
	BTR_SUCCESS,
	BTR_FAILURE
} BillingTransactionResult;

AUTO_ENUM;
typedef enum BillingTransactionState
{
	BTS_IDLE = 0,
	BTS_CONNECTING,
	BTS_WAITING_FOR_REPLY,

	BTS_DEBUG_DELAY,       // Used when faking long transactions
	BTS_DISABLED_AUTOFAIL, // Used when -billingenabled 0
	BTS_ALREADY_FAILED,    // Used when a transaction fails before it even talks to Vindicia
} BillingTransactionState;

typedef struct HttpClient HttpClient;
typedef struct BillingTransaction BillingTransaction;
typedef void (*BillingTransactionCompleteCB)(BillingTransaction *pTrans); // Can optionally change/set pTrans->result and resultString

typedef struct BillingTransactionStructCleanupData
{
	ParseTable *pti;
	void *ptr;
} BillingTransactionStructCleanupData;

typedef struct AccountTransactionInfo AccountTransactionInfo;
typedef struct ProductKeyInfo ProductKeyInfo;

AUTO_STRUCT;
typedef struct PayPalStatus
{
	char *token;
	char *authCode;
	char *redirectUrl;
} PayPalStatus;

typedef struct BillingTransaction
{
	// Tracking information.
	U32 ID;																// Ever-incrementing ID
	char *action;														// Used for logging
	U32 uDebugAccountId;												// If non-null, an account name associated with this transaction

	// Transaction result information
	BillingTransactionResult result;									// For a finished transaction, success or failure.
	char *resultString;													// Descriptive result of entire transaction reported to web site
	char *webUID;														// A unique identifier that can look up this transaction

	// Web response information.
	EARRAY_OF(AccountTransactionInfo) webResponseTransactionInfo;		// Response from btFetchAccountTransactions()
	char *webResponseRefundAmount;										// Response from btRefund()
	char *webResponseRefundCurrency;									// Response from btRefund()
	EARRAY_OF(ProductKeyInfo) distributedKeys;							// Stores distributed keys
	char *merchantTransactionID;										// Stores the merchant transaction ID

	// Steam response information.
	bool steamTransaction;												// If this is a Steam transaction
	BillingTransactionResult steamTransactionResult;					// Result of the Steam transaction, separate because this module is retarded
	char *steamCurrency;												// Currency to use for Steam purchases
	char *steamState;													// State (e.g. CA) of the Steam user
	char *steamCountry;													// Country of the Steam user
	char *steamStatus;													// Status of the Steam user (should be "Active")
	char *redirectURL;													// An external URL to redirect the user to for completion

	// Transaction status information
	BillingTransactionState state;
	U32 creationTime;
	U32 stateTime;														// Time when the state last changed
	HttpClient *client;
	char *request;
	char *response;
	bool track;															// True if we will track this connection for high failure rate alerts

	int idle;

	U32 uPurchaseID;

	// Information for individual steps of transaction processing
	BillingTransactionCompleteCB callback;
	void *userData;

	void **ppAllocatedMemoryBlocks;
	VindiciaXMLtoObjResult **ppResultsToCleanup;
	BillingTransactionStructCleanupData **ppStructsToCleanup;

	PayPalStatus *payPalStatus;
	U32 uPendingActionID;
} BillingTransaction;

SA_RET_OP_STR const char *billingGetLastVindiciaResponse(U32 uBillingTransID);

BillingTransaction * btCreateBlank(bool bCreateID);
BillingTransaction * btCreate(const char *action, const char *xml, BillingTransactionCompleteCB callback, void *userData, bool bCreateID);
void btOncePerFrame(BillingTransaction *pTrans);
void btFail(BillingTransaction *pTrans, const char *format, ...); // Marks a transaction as a failure, finishes it up
void btMarkIdle(BillingTransaction *pTrans); // Considers this an "idle" transaction
void btMarkSteamTransaction(BillingTransaction *pTrans); // Marks the transaction for Steam
void btCompleteSteamTransaction(BillingTransaction *pTrans, bool bSuccess); // Marks a Steam transaction complete
void btContinue(BillingTransaction *pTrans, const char *action, const char *xml, BillingTransactionCompleteCB callback, void *userData);
void btTrackOutcome(BillingTransaction *pTrans); // Track this transaction for purposes of high failure rate alerting.
void btFreeObjResult(BillingTransaction *pTrans, VindiciaXMLtoObjResult *pRes); // Defers freeing until the transaction is destroyed

void * btAlloc_dbg(BillingTransaction *pTrans, size_t count, size_t size, MEM_DBG_PARMS_VOID); // all automatically freed by btDestroy()
#define btAlloc(pTrans, ptr, type) ((void)(1/(S32)(sizeof(*ptr)==sizeof(type))),(ptr) = (type*)btAlloc_dbg(pTrans, 1, sizeof(*(ptr)), MEM_DBG_PARMS_INIT_VOID))
#define btAllocCount(pTrans, ptr, type, count) ((void)(1/(S32)(sizeof(*ptr)==sizeof(type))),(ptr) = (type*)btAlloc_dbg(pTrans, (count), sizeof(*(ptr)), MEM_DBG_PARMS_INIT_VOID))

void *btSoapAlloc(struct soap *soap, size_t size); // for gSOAP
void btSoapFreeAllocs(struct soap *soap); // also for gSOAP

void * btStructCreateVoid_dbg(BillingTransaction *pTrans, ParseTable pti[], MEM_DBG_PARMS_VOID); // all automatically freed by btDestroy()
#define btStructCreateVoid(pTrans, pti) btStructCreateVoid_dbg(pTrans, pti, MEM_DBG_PARMS_INIT_VOID)
#define btStructCreate(pTrans, pti) (TYPEOF_PARSETABLE(pti) *)btStructCreateVoid(pTrans, pti)
void   btStructAutoCleanup(BillingTransaction *pTrans, ParseTable pti[], void *ptr); // all automatically freed by btDestroy()

char * btStrdup_dbg(BillingTransaction *pTrans, const char *pStr, MEM_DBG_PARMS_VOID); // all automatically freed by btDestroy()
#define btStrdup(pTrans, pStr) btStrdup_dbg(pTrans, pStr, MEM_DBG_PARMS_INIT_VOID)
#define btStrdupNonNULL(pTrans, pStr) (pStr ? btStrdup((pTrans), (pStr)) : NULL)

char * btSPrintf_dbg(BillingTransaction *pTrans, MEM_DBG_PARMS_VOID, FORMAT_STR const char* format, ...); // all automatically freed by btDestroy()
#define btSPrintf(pTrans, format, ...) btSPrintf_dbg(pTrans, MEM_DBG_PARMS_INIT_VOID, FORMAT_STRING_CHECKED(format), __VA_ARGS__)

SA_RET_NN_STR char * btMoneyRaw_dbg(BillingTransaction *pTrans, SA_PARAM_NN_VALID const Money *money, MEM_DBG_PARMS_VOID);
#define btMoneyRaw(pTrans, money) btMoneyRaw_dbg(pTrans, money, MEM_DBG_PARMS_INIT_VOID)

void * btAllocUserData_dbg(BillingTransaction *pTrans, int size, MEM_DBG_PARMS_VOID); // calls btAlloc(), sets userData ptr
#define btAllocUserData(pTrans, size) btAllocUserData_dbg(pTrans, size, MEM_DBG_PARMS_INIT_VOID)

SA_RET_OP_VALID BillingTransaction * btFindByUID(SA_PARAM_NN_STR const char *uid);

const char * billingSkipPrefix(const char *pStr);
char * btStrdupWithPrefix_dbg(BillingTransaction *pTrans, const char *pStr, MEM_DBG_PARMS_VOID); // prepends billing prefix
#define btStrdupWithPrefix(pTrans, pStr) btStrdupWithPrefix_dbg(pTrans, pStr, MEM_DBG_PARMS_INIT_VOID)

// Billing-related statistics
int btGetQueuedTransactionCount(void);
int btGetActiveTransactionCount(void);
int btGetCompletedTransactionCount(void);
int btGetIdleTransactionCount(void);
U32 btLastIdleTransactionQueueFill(void);
U32 btLastIdleTransactionQueueCompletion(void);
U32 btLastIdleTransactionQueueCompletionLength(void);

void btWaitForAllTransactions(void);   // This will block until all transactions complete. Be careful!



bool billingIsEnabled();
U32 billingGetDebugLevel();
bool billingSkipNonBillingAccounts();

const FraudSettings * getFraudSettings(SA_PARAM_OP_STR const char *pCountry);

CONST_STRING_EARRAY getPurchaseLogKeys(void);

bool billingMapPWCurrency(
	SA_PARAM_NN_STR const char * pCurrency,
	SA_PARAM_NN_OP_VALID const char * * * eaOutCurrencies);

U32 billingGetSpendingCapPeriod(void);
float billingGetSpendingCap(SA_PARAM_NN_VALID const char *pCurrency);

const char * billingGetPrefix(void);
float billingGetAuthFailThreshold(void);
U32 billingGetAuthFailPeriod(void);
U32 billingGetAuthFailMinimumCount(void);
float billingGetSteamFailThreshold(void);
U32 billingGetSteamFailPeriod(void);
U32 billingGetSteamFailMinimumCount(void);

bool billingInit();
void billingShutdown();
void billingOncePerFrame();

typedef struct AccountInfo AccountInfo;

SA_RET_NN_STR char * getMerchantAccountID(SA_PARAM_NN_VALID const AccountInfo *pAccount);
SA_RET_NN_STR char * btGetMerchantAccountID(SA_PARAM_NN_VALID BillingTransaction *pTrans, SA_PARAM_NN_VALID const AccountInfo *pAccount);

// Flags used by parseFraudSettings
#define INVALID_AVS	BIT(0)
#define INVALID_CVN	BIT(1)

#define VALID_MIN_CHARGEBACK(x) ((x) >= VINDICIA_CHARGEBACK_PROBABILITY_MIN && (x) <= VINDICIA_CHARGEBACK_PROBABILITY_MAX)

bool parseFraudSettings(SA_PARAM_NN_STR const char *pBillingCountry,
						SA_PARAM_NN_STR const char *pCVN,
						SA_PARAM_NN_STR const char *pAVS,
						SA_PARAM_OP_VALID int *pMinChargeback, // out
						SA_PARAM_OP_VALID U32 *pFlags // out
						);

// Billing settings accessors
SA_RET_OP_VALID const char *billingGetValidationProduct(void);
U32 billingGetDeactivatePaymentMethodTries(void);
U32 billingGetMaxActivities(void);
U32 billingGetActivityPeriod(void);
int billingGetVindiciaLogLevel(void);
SA_RET_OP_VALID const char *billingGetStunnelHost(void);
SA_RET_OP_STR const char *billingGetSteamURL(bool bLive);
U32 billingGetSteamPort(void);
SA_RET_OP_STR const char *billingGetSteamVersion(void);
SA_RET_OP_VALID const SteamGameConfig *billingGetSteamGameConfig(SA_PARAM_NN_VALID const char *pProduct);
SA_RET_OP_VALID const ShardConfig *billingGetShardConfig(SA_PARAM_NN_VALID const char *pProxy);
U32 billingGetFetchDeltaAutobillPageSize(void);
BillingServerType billingGetServerType(void);

typedef enum DivisionType
{
	DT_Subscription,
	DT_Product,
} DivisionType;

SA_RET_OP_STR const char * billingGetDivision(DivisionType eType, SA_PARAM_OP_STR const char * pInternalName);

SA_RET_OP_VALID AccountInfo * findAccountByMerchantAccountID(SA_PARAM_NN_STR const char *pMerchantID);

// Syncs all account's subscriptions with Vindicia -- pass 0 to use the last known time
void billingUpdateAccountSubscriptions(U32 uSecondsSince2000);

// Convert a tax classification into it's Vindicia form
enum vin__TaxClassification btConvertTaxClassification(TaxClassification eTaxClassification);

// Log fetch delta stats
void billingLogFetchDeltaStats(FetchDeltaType eType, SA_PARAM_NN_VALID const FetchDeltaStats *pStats);

typedef struct PaymentMethod PaymentMethod;
typedef struct vin__PaymentMethod vin__PaymentMethod;

SA_RET_NN_VALID struct vin__PaymentMethod *
btCreateVindiciaPaymentMethod(SA_PARAM_OP_VALID const AccountInfo *pAccount,
							  SA_PARAM_NN_VALID BillingTransaction *pTrans,
							  SA_PARAM_NN_VALID const PaymentMethod *pPaymentMethod,
							  bool bForceAll,
							  SA_PARAM_OP_STR const char * pBankName);

void btSetVindiciaTransStatus(SA_PARAM_NN_VALID BillingTransaction *pTrans,
							  SA_PARAM_OP_VALID struct vin__TransactionStatus *pStatus);

// Convert a Vindicia subscription status to ours
SubscriptionStatus convertSubStatusFromVindicia(enum vin__AutoBillStatus eStatus);

// Populate a cached account subscription payment method information from an autobill
void populateSubPaymentMethodFromVindicia(SA_PARAM_NN_VALID NOCONST(CachedAccountSubscription) *pCachedSub,
										  SA_PARAM_NN_VALID const struct vin__PaymentMethod *pVinPaymentMethod);

// Create a cached account subscription from a Vindicia autobill
SA_RET_OP_VALID NOCONST(CachedAccountSubscription) *
createCachedAccountSubscriptionFromAutoBill(
	SA_PARAM_NN_VALID const AccountInfo *pAccount,
	SA_PARAM_NN_VALID struct vin__AutoBill *pAutoBill);


// Update a cached subscription in the event of there being no rebills in Vindicia
void updateCachedSubscriptionNoRebills(SA_PARAM_NN_VALID NOCONST(CachedAccountSubscription) *pCachedSub);

void btInitializeNameValues(SA_PARAM_NN_VALID BillingTransaction *pTrans,
							SA_PARAM_NN_VALID struct ArrayOfNameValuePairs **ppPairs,
							unsigned int uSize);

void btSetNameValuePair(SA_PARAM_NN_VALID BillingTransaction *pTrans,
						SA_PARAM_NN_VALID struct ArrayOfNameValuePairs *pPairs,
						unsigned int uIndex,
						SA_PARAM_OP_STR const char *pName,
						SA_PARAM_OP_STR const char *pValue);

#define VINDICIA_MANDATE_NUM_NAME_VALUES 3

// Requires three name-value slots
void btSetMandateNameValues(SA_PARAM_NN_VALID BillingTransaction *pTrans,
							SA_PARAM_NN_VALID struct ArrayOfNameValuePairs *pPairs,
							unsigned int uIndexStart,
							SA_PARAM_OP_STR const char *pMandateVersion,
							SA_PARAM_OP_STR const char *pBankName);

void btSetInterestingNameValues(SA_PARAM_NN_VALID BillingTransaction *pTrans,
								SA_PARAM_NN_VALID struct ArrayOfNameValuePairs **ppPairs,
								SA_PARAM_OP_STR const char *pDivision,
								SA_PARAM_OP_STR const char *pBillingCountry,
								SA_PARAM_OP_STR const char *pBankName);

SA_RET_OP_STR const char *getMandateVersion(SA_PARAM_NN_STR const char *pCountry);

#define mandateExistsForCountry(pCountry) ((pCountry) ? getMandateVersion(pCountry) != NULL : false)

#include "Billing_h_ast.h"

#endif
