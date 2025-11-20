#ifndef ACCOUNTSERVER_H_
#define ACCOUNTSERVER_H_

#include "accountnet.h"
#include "accountCommon.h"
#include "RateLimit.h"
#include "MachineID.h"

// Used for authentication
#define ACCOUNT_SERVER_INTERNAL_NAME "AccountServer"

#define ACCOUNT_SERVER_VERSION "(dev)" // Change to X.X once branched

// Name of the Account Server merger mutex
#define ACCOUNT_SERVER_MERGER_NAME "AccountServer"

#define ACCOUNT_FLAG_NOT_ACTIVATED			BIT(0)
#define ACCOUNT_FLAG_INVALID_DISPLAYNAME	BIT(1)
#define ACCOUNT_FLAG_AUTOSAVE_MACHINEID_CLIENT  BIT(2) // Automatically save and allow the next client machine ID that attempts to log in
#define ACCOUNT_FLAG_AUTOSAVE_MACHINEID_BROWSER BIT(3) // Automatically save and allow the next browser machine ID that attempts to log in
#define ACCOUNT_FLAG_AUTOSAVE_CLIENT_LOGIN  BIT(4) // User has successfully logged in with a cryptic client that was automatically allowed; for disabling grace period allowances
#define ACCOUNT_FLAG_AUTOSAVE_BROWSER_LOGIN BIT(5) // User has successfully logged in with a browser that was automatically allowed; for disabling grace period allowances
#define ACCOUNT_FLAG_LOGS_REBUCKETED		BIT(6) // User's logs have been rebucketed

// The following flag is deprecated, because we moved it to its own field
// You can replace it if you want, BUT you have to make sure that your thing will behave properly in dev mode ASs that have the flag set already
#define ACCOUNT_FLAG_DEPRECATED_LOGS_INDEXED	BIT(7)

// Account server access levels
AUTO_ENUM;
typedef enum AccountServerAccessLevel
{
	ASAL_SuperAdmin		= 9,
	ASAL_Admin			= 8,
	ASAL_Normal			= 7,
	ASAL_Limited		= 0,
	ASAL_Invalid		= -1,
} AccountServerAccessLevel;

#define ASAL_GRANT_ACCESS	BIT(0)	// Give/manipulate account server access
#define ASAL_KEY_ACCESS		BIT(1)	// Create/view activation keys

// Translate a numeric access level into an account server access level
AccountServerAccessLevel ASGetAccessLevel(int iLevel);

// Determine if an access level has the specified permissions
bool ASHasPermissions(AccountServerAccessLevel eAccessLevel, U32 uPermissions);

AUTO_ENUM;
typedef enum AccountServerMode
{
	ASM_Normal = 0,
	ASM_KeyGenerating,
	ASM_Merger,
	ASM_ExportUserList,
	ASM_UpdateSchemas,
} AccountServerMode;

AccountServerMode getAccountServerMode(void);
bool isAccountServerMode(AccountServerMode mode);

bool isAccountServerLikeLive(void);
bool isAccountServerLive(void);

// Machine Lock state; Unknown results in default behavior defined by AccountServer settings
AUTO_ENUM;
typedef enum AccountMachineLockState
{
	AMLS_Unknown = 0,
	AMLS_Enabled,
	AMLS_Disabled,
} AccountMachineLockState;

// Credit card specific payment method information
AUTO_STRUCT;
typedef struct CreditCard
{
	char *CVV2;							AST(ESTRING)
	char *account;						AST(ESTRING)
	char *expirationDate;				AST(ESTRING)
} CreditCard;

// PayPal specific payment method information
AUTO_STRUCT;
typedef struct PayPal
{
	char *emailAddress;					AST(ESTRING)
	char *returnUrl;					AST(ESTRING)
	char *cancelUrl;					AST(ESTRING)
	char *password;						AST(ESTRING)
} PayPal;

// Direct debit specific payment information
AUTO_STRUCT;
typedef struct DirectDebit
{
	char *account;						AST(ESTRING)
	char *bankSortCode;					AST(ESTRING)
	char *ribCode;						AST(ESTRING)
} DirectDebit;

// Payment method type
AUTO_ENUM;
typedef enum PaymentMethodType {
	PMT_Invalid		= 0,
	PMT_CreditCard	= BIT(0),
	PMT_PayPal		= BIT(1),
	PMT_DirectDebit = BIT(2),

	PMT_MAX, EIGNORE
} PaymentMethodType;
#define PaymentMethodType_NUMBITS 4
STATIC_ASSERT(PMT_MAX == ((1 << (PaymentMethodType_NUMBITS-2))+1));

// Stores payment method information
AUTO_STRUCT;
typedef struct PaymentMethod
{
	char *VID;							AST(ESTRING)
	int active;
	char *accountHolderName;			AST(ESTRING)
	char *customerSpecifiedType;		AST(ESTRING)
	char *customerDescription;			AST(ESTRING)
	char *currency;						AST(ESTRING)
	char *addressName;					AST(ESTRING)
	char *addr1;						AST(ESTRING)
	char *addr2;						AST(ESTRING)
	char *city;							AST(ESTRING)
	char *county;						AST(ESTRING)
	char *district;						AST(ESTRING)
	char *postalCode;					AST(ESTRING)
	char *country;						AST(ESTRING)
	char *phone;						AST(ESTRING)
	PayPal *payPal;
	CreditCard *creditCard;
	DirectDebit *directDebit;
	bool precreated;
} PaymentMethod;

// Per-link information
typedef struct AccountLink
{
	char loginField[MAX_LOGIN_FIELD]; // What the user typed in (account name or e-mail)
	
	// Salts used for hashing the password on the client
	U32 temporarySalt; // Randomly-generated, one-time-only
	char fixedSalt[MAX_FIXED_SALT]; // Always the same for the given loginField

	AccountLoginType eLoginType; // Cryptic or PWE

	U32 ipRequest;
		// If non-zero, indicates the IP address of the client being validated.
		// This is used by the login proxies (such as the GatewayLogin server) which uses
		//   a single netlink (and hence IP) to log in multiple clients.
} AccountLink;

typedef struct AccountTicketSigned AccountTicketSigned;
AUTO_STRUCT;
typedef struct AccountTicketCache
{
	AccountTicketSigned *ticket;
	U32 uRandomID;
	U32 uExpireTime;
	U32 uIp;							// Internet address of client creating the ticket
	char *machineID;
	bool bMachineRestricted;
} AccountTicketCache;

AST_PREFIX( PERSIST )
AUTO_STRUCT AST_CONTAINER AST_IGNORE(uFlags) AST_IGNORE(uContext);
typedef struct AccountLogEntry
{
	const U32 uID; AST(KEY)								// ID 
	const U32 uSecondsSince2000;						// When it was created
	CONST_STRING_MODIFIABLE pMessage;	AST(ESTRING)	// Message
} AccountLogEntry;

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountLogBatch
{
	const U32 uBatchID;	AST(KEY)
	const U32 uAccountID;
	CONST_EARRAY_OF(AccountLogEntry) eaLogEntries; AST(NO_INDEX)
	CONST_EARRAY_OF(AccountLogEntry) eaIndexedLogEntries;
} AccountLogBatch;

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountPermissionValue
{
	CONST_STRING_MODIFIABLE pType; AST(ESTRING)
	CONST_STRING_MODIFIABLE pValue; AST(ESTRING)
} AccountPermissionValue;

AST_PREFIX();
AUTO_STRUCT;
typedef struct AccountPermission
{
	char * pProductName; AST(ESTRING)		// TODO: change to product ID to save stash lookups
	char * pShardCategory; AST(ESTRING)
	EARRAY_OF(AccountPermissionValue) ppPermissions;
	int iAccessLevel;
	char * pPermissionString; AST(ESTRING)
} AccountPermission;
AST_PREFIX(PERSIST);

// Secret question and answer pairs.
// The number of questions must match the number of answers.
AUTO_STRUCT AST_CONTAINER;
typedef struct AccountQuestionsAnswers
{
	STRING_EARRAY questions;	AST(ESTRING)
	STRING_EARRAY answers;		AST(ESTRING)
} AccountQuestionsAnswers;

AUTO_STRUCT AST_CONTAINER AST_IGNORE_STRUCT(billingAddress);
typedef struct AccountPersonalInfo
{
	CONST_STRING_MODIFIABLE firstName; AST(ESTRING)
	CONST_STRING_MODIFIABLE lastName; AST(ESTRING)
	CONST_STRING_MODIFIABLE email; AST(ESTRING)

	CONST_EARRAY_OF(CachedPaymentMethod) ppPaymentMethods;	// Gotten from Vindicia
	const AccountAddress shippingAddress;

	// Secret questions and answers.
	const AccountQuestionsAnswers secretQuestionsAnswers;

	const U32 dob[3]; // day,month,year
} AccountPersonalInfo;

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountLoginData
{
	const U32 uIP; // last login IP
	const U32 uTime; // last login time
} AccountLoginData;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(state);
typedef struct AccountProductSub
{
	CONST_STRING_MODIFIABLE name; AST(ESTRING) // TODO: change to product ID to save stash lookups
	CONST_STRING_MODIFIABLE key; AST(ESTRING)
	const U32 uAssociatedTimeSS2000;
	CONST_STRING_MODIFIABLE referrer;
} AccountProductSub;

AUTO_ENUM;
typedef enum SubscriptionStatus
{
	SUBSCRIPTIONSTATUS_INVALID = 0,
	SUBSCRIPTIONSTATUS_CANCELLED,
	SUBSCRIPTIONSTATUS_ACTIVE,
	SUBSCRIPTIONSTATUS_SUSPENDED,
	SUBSCRIPTIONSTATUS_PENDINGCUSTOMER,
	SUBSCRIPTIONSTATUS_REFUNDED,
} SubscriptionStatus;

typedef struct SubscriptionLocalizedInfo SubscriptionLocalizedInfo;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(entitled) AST_IGNORE(vindiciaEntitled);
typedef struct CachedAccountSubscription
{
	const U32 uSubscriptionID;	AST(NAME(SubscriptionID))

	// Directly maps to the SubscriptionStruct pName
	CONST_STRING_MODIFIABLE name; AST(ESTRING)
	CONST_STRING_MODIFIABLE internalName; AST(ESTRING)

	// Subscription info
	const U32 startTimeSS2000;
	const U32 estimatedCreationTimeSS2000;

	// VID of payment method associated with this subscription.
	CONST_STRING_MODIFIABLE PaymentMethodVID; AST(ESTRING)

	// Credit card
	CONST_STRING_MODIFIABLE creditCardLastDigits; AST(ESTRING)
	const U32 creditCardExpirationMonth;
	const U32 creditCardExpirationYear;

	// Used for updating/cancelling the correct subscription
	CONST_STRING_MODIFIABLE vindiciaID; AST(ESTRING)

	const SubscriptionStatus vindiciaStatus; AST(NAME(status))

	const U32 entitlementEndTimeSS2000;

	// Not only represents next billing date, but also used when re-subscribing (for free day credit calculation)
	const U32 nextBillingDateSS2000;

	const U32 gameCard; // True if this is a gamecard subscription

	const U32 pendingActionID;

	const bool bBilled; AST(DEF(false))

} CachedAccountSubscription;

SubscriptionStatus getCachedSubscriptionStatus(SA_PARAM_NN_VALID const CachedAccountSubscription *pCachedSub);

AUTO_STRUCT AST_CONTAINER;
typedef struct CachedAccountSubscriptionList
{
	CONST_EARRAY_OF(CachedAccountSubscription) ppList; AST(NAME("list"))
	const U32 lastUpdatedSS2000;
} CachedAccountSubscriptionList;


/************************************************************************/
/* Game metadata                                                        */
/************************************************************************/

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayTimeEntry
{
	const U32 uPlayTime;
	const U32 uYear;
	const U8 uMonth;
	const U8 uDay;
} PlayTimeEntry;

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountPlayTime
{
	const U32 uPlayTime;
	CONST_OPTIONAL_STRUCT(PlayTimeEntry) pCurrentMonth;
	CONST_OPTIONAL_STRUCT(PlayTimeEntry) pCurrentDay;
	CONST_EARRAY_OF(PlayTimeEntry) eaMonths;
	CONST_EARRAY_OF(PlayTimeEntry) eaDays;
} AccountPlayTime;

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountGameMetadata
{
	CONST_STRING_MODIFIABLE product; AST(ESTRING)
	CONST_STRING_MODIFIABLE shard; AST(ESTRING)
	const U32 uLastLogin;
	const U32 uLastLogout;
	const U32 uNumCharacters;
	const U32 uHighestLevel;
	const AccountPlayTime playtime; AST(EMBEDDED_FLAT)
} AccountGameMetadata;

/************************************************************************/
/* Keyvalues                                                            */
/************************************************************************/

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountKeyValuePairLock
{
	CONST_STRING_MODIFIABLE proxy;					AST(ESTRING)			  // Proxy that owns the lock, if locked
	const U32 time;															  // Time in SS2000 when last locked
	CONST_STRING_MODIFIABLE password;				AST(ESTRING)			  // Lock password
	CONST_STRING_MODIFIABLE sValue_Obsolete;		AST(ESTRING NAME(SValue)) // Obsolete string storage of locked value
	const S64 iValue;								AST(ESTRING NAME(Value))  // Value once lock is committed
	const U8 deleteOnRollback : 1;											  // If this key-value should be deleted when rolled back
} AccountKeyValuePairLock;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(lockDeleteMe) AST_IGNORE(lock) AST_IGNORE(lockSValue);
typedef struct AccountKeyValuePair
{
	CONST_STRING_MODIFIABLE key;					AST(ESTRING KEY)		  // Key
	CONST_STRING_MODIFIABLE sValue_Obsolete;		AST(ESTRING NAME(SValue)) // Obsolete value
	const S64 iValue;								AST(ESTRING NAME(Value))  // Value, now stored as an integer
	CONST_OPTIONAL_STRUCT(AccountKeyValuePairLock) lockData;
} AccountKeyValuePair;


/************************************************************************/
/* Pending actions                                                      */
/************************************************************************/

AUTO_ENUM;
typedef enum AccountPendingActionType
{
	APAT_REFRESH_SUB_CACHE = 0,
	APAT_FINISH_DELAYED_TRANS,
	APAT_NONE
} AccountPendingActionType;

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountPendingRefreshSubCache
{
	CONST_STRING_MODIFIABLE pSubscriptionVID;
} AccountPendingRefreshSubCache;

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountPendingFinishDelayedTrans
{
	const U32 uPurchaseID;
} AccountPendingFinishDelayedTrans;

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountPendingAction {
	const U32 uID; AST(KEY)
	const AccountPendingActionType eType; AST(DEF(APAT_NONE))
	const U32 uCreatedTime; // Seconds since 2000
	CONST_OPTIONAL_STRUCT(AccountPendingRefreshSubCache) pRefreshSubCache;			// Only populated if eType == REFRESH_SUB_CACHE
	CONST_OPTIONAL_STRUCT(AccountPendingFinishDelayedTrans) pFinishDelayedTrans;	// Only populated if eType == FINISH_DELAYED_TRANS
} AccountPendingAction;


/************************************************************************/
/* Purchase information                                                 */
/************************************************************************/

// Stores a minimum of information to continue a purchase
AUTO_STRUCT AST_CONTAINER;
typedef struct PurchaseInformationContainer
{
	const U32 uPurchaseID;													AST(PERSIST KEY)
	CONST_STRING_MODIFIABLE pProxy;											AST(PERSIST)
	CONST_EARRAY_OF(AccountProxyProductActivationContainer) eaActivations;	AST(PERSIST FORCE_CONTAINER)
	CONST_STRING_MODIFIABLE pPassword;										AST(PERSIST ESTRING)
	CONST_STRING_MODIFIABLE pCurrency;										AST(PERSIST)
	const U32 uFlags;														AST(PERSIST)
	CONST_STRING_MODIFIABLE pMerchantTransactionId;							AST(PERSIST)
	const U32 uActivationSuppressions;										AST(PERSIST)
	CONST_INT_EARRAY eaProductIDs;											AST(PERSIST)
	CONST_STRING_EARRAY eaProductPrices;									AST(PERSIST ESTRING)

	// Steam contents
	CONST_STRING_MODIFIABLE pOrderID;										AST(PERSIST)
	const TransactionProvider eProvider;									AST(PERSIST)
} PurchaseInformationContainer;

typedef struct NOCONST(PurchaseInformationContainer) PurchaseInformation;
#define parse_PurchaseInformation parse_PurchaseInformationContainer
#define TYPE_parse_PurchaseInformation PurchaseInformation


/************************************************************************/
/* Refunded subs                                                        */
/************************************************************************/

AUTO_STRUCT AST_CONTAINER;
typedef struct RefundedSubscription
{
	CONST_STRING_MODIFIABLE pSubscriptionVID;	AST(KEY)
	const U32 uRefundedSS2000;
} RefundedSubscription;


/************************************************************************/
/* Subscription history tracking                                        */
/************************************************************************/

AUTO_ENUM;
typedef enum SubscriptionHistoryEntryReason
{
	SHER_Invalid = 0,
	SHER_ManualEntry,
	SHER_Cancelled,
	SHER_Expired,
	SHER_Suspended,
	SHER_Refunded,
	SHER_Activation,
} SubscriptionHistoryEntryReason;

AUTO_ENUM;
typedef enum SubscriptionTimeSource
{
	STS_Invalid = 0,	// Invalid
	STS_External,		// From autobills in Vindicia
	STS_Internal,		// From internal subscriptions
	STS_Product,		// From days given by a product
	STS_Extended,		// From days given by grant days etc.
} SubscriptionTimeSource;

// Problems that can happen when calculating archived days
#define SHEP_OVERLAPS				BIT(0) // Time range overlaps with another entry
#define SHEP_NOT_EXACT				BIT(1) // Number of seconds is not exact (rounded etc.)

AUTO_STRUCT AST_CONTAINER;
typedef struct SubscriptionHistoryEntry
{
	const U32 uID; AST(KEY)

	const SubscriptionHistoryEntryReason eReason; AST(DEF(SHER_ManualEntry))
	const bool bEnabled; AST(DEF(true))

	// Calculation results
	const U32 uAdjustedStartSS2000;
	const U32 uAdjustedEndSS2000;

	// Times
	const U32 uStartTimeSS2000;
	const U32 uEndTimeSS2000;
	const U32 uLastCalculatedSS2000;
	const U32 uCreatedSS2000; // When this entry was created
	const U32 uProblemFlags; // From the list above this structure

	// Subscription identification
	CONST_STRING_MODIFIABLE pSubInternalName;
	CONST_STRING_MODIFIABLE pSubscriptionVID; // NULL for internal subscriptions
	const SubscriptionTimeSource eSubTimeSource; AST(DEF(STS_External))
} SubscriptionHistoryEntry;

AUTO_STRUCT AST_CONTAINER;
typedef struct SubscriptionHistory
{
	// Product for which we are storing history
	CONST_STRING_MODIFIABLE pProductInternalName; AST(KEY)
	
	// Entries used for calculations
	CONST_EARRAY_OF(SubscriptionHistoryEntry) eaArchivedEntries;
	const U32 uNextEntryID; AST(DEF(1))
} SubscriptionHistory;

/************************************************************************/
/* Distributed product keys                                             */
/************************************************************************/

AUTO_STRUCT AST_CONTAINER;
typedef struct DistributedKeyContainer
{
	CONST_STRING_MODIFIABLE pActivationKey;
	const U32 uDistributedTimeSS2000;
} DistributedKeyContainer;

/************************************************************************/
/* Expected subscriptions                                               */
/************************************************************************/

AST_PREFIX();
AUTO_STRUCT;
typedef struct ExpectedSubscription
{
	char * pInternalName; AST(KEY)
	U32 uExpectedSinceSS2000;
} ExpectedSubscription;
AST_PREFIX(PERSIST);

/************************************************************************/
/* Profiles                                                             */
/************************************************************************/

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountProfile
{
	CONST_STRING_MODIFIABLE pKey; AST(KEY ESTRING)
	CONST_STRING_MODIFIABLE pGameID;
	CONST_STRING_MODIFIABLE pProfileID;
	CONST_STRING_MODIFIABLE pPlatformID;
} AccountProfile;

/************************************************************************/
/* Recent purchase amounts                                              */
/************************************************************************/

AUTO_STRUCT AST_CONTAINER;
typedef struct RecentPurchaseAmount
{
	CONST_STRING_MODIFIABLE pCurrency;
	const U32 uTime;
	const float fAmount; // Doesn't have to be exact
} RecentPurchaseAmount;

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountSpendingCap
{
	CONST_STRING_MODIFIABLE pCurrency;
	const float fAmount; // Doesn't have to be exact
} AccountSpendingCap;

/************************************************************************/
/* The most important structure:                                        */
/************************************************************************/

// Temporary Flags
#define ACCOUNT_ACCESSED_FLAG BIT(0) // Set if Account has ever been accessed since last restart

//what version of password storage is the password primarily stored with
AUTO_ENUM;
typedef enum enumPasswordVersion
{
	PASSWORDVERSION_HASHED_UNSALTED = 0, ENAMES(HashedUnsalted)
	PASSWORDVERSION_HASHED_SALTED_WITH_ACTNAME, ENAMES(HashedSaltedWithAccountName)


	PASSWORDVERSION_COUNT, AEN_IGNORE
} enumPasswordVersion;

extern enumPasswordVersion eCurPasswordVersion;

const char *GetPasswordVersionName(enumPasswordVersion eVersion);

//all the information about a container's password
AUTO_STRUCT AST_CONTAINER;
typedef struct AccountPasswordInfo
{
	const int iEncryptionKeyIndex; //if the password is encrypted, which encryption key was used to store it
	const enumPasswordVersion ePasswordVersion;

	//unencrypted password, still hashed. Not populated until required
	char password_ForRAM[MAX_PASSWORD]; NO_AST

	//if encryption is being used, encrypted password, otherwise just hashed password
	const char password_ForDisk[MAX_PASSWORD];
} AccountPasswordInfo;


AUTO_STRUCT AST_CONTAINER
AST_IGNORE(uMasterAccountID) AST_IGNORE(uLogBatchID) AST_IGNORE(xuid) AST_IGNORE(uNextLogContext) AST_IGNORE(uLogBufferWriteTime) AST_IGNORE(uLastLogID)
AST_IGNORE_STRUCT(ppPermissions) AST_IGNORE_STRUCT(ppProductFlags);
typedef struct AccountInfo
{
	const U32 uID; AST(KEY)
	const char accountName[MAX_ACCOUNTNAME]; AST(CASE_SENSITIVE)

	const char password_obsolete[MAX_PASSWORD]; AST(NAME(password))
	CONST_OPTIONAL_STRUCT(AccountPasswordInfo) passwordInfo;
	CONST_OPTIONAL_STRUCT(AccountPasswordInfo) pBackupPasswordInfo;

	const char globallyUniqueID[MAX_GUID]; // Should be unique across all account servers

	const U32 uCreatedTime;
	const U32 uPasswordChangeTime; // Last time the password was changed

	CONST_EARRAY_OF(AccountProductSub) ppProducts;
	CONST_EARRAY_OF(AccountGameMetadata) ppGameMetadata; AST(ADDNAMES(Ppplaytimes))
	CONST_EARRAY_OF(AccountKeyValuePair) ppKeyValuePairs;

	CONST_EARRAY_OF(AccountLogEntry) ppLogEntries;			AST(NO_INDEX)
	CONST_EARRAY_OF(AccountLogEntry) ppLogEntriesBuffer;	AST(NO_INDEX)
	CONST_EARRAY_OF(AccountLogEntry) ppIndexedLogBuffer;
	CONST_CONTAINERID_EARRAY eauLogBatchIDs;
	CONST_CONTAINERID_EARRAY eauIndexedLogBatchIDs;
	CONST_CONTAINERID_EARRAY eauRebucketLogBatchIDs; // Used temporarily to store batch IDs of an in-progress rebucketing
	const U32 uNextLogID;

	// Terrible kludge to store all activity logs while the account is being rebucketed.
	// Also, if the account hasn't been indexed yet, then this stores all activity logs generated until rebucketing occurs,
	//	even if the account is still waiting in the rebucketing queue.
	CONST_EARRAY_OF(AccountLogEntry) ppTemporaryLogBufferDuringRebucketing;

	CONST_EARRAY_OF(AccountPendingAction) ppPendingActions;
	CONST_EARRAY_OF(PurchaseInformationContainer) ppPurchaseInformation;
	CONST_EARRAY_OF(RefundedSubscription) ppRefundedSubscriptions;
	CONST_EARRAY_OF(SubscriptionHistory) ppSubscriptionHistory;
	CONST_EARRAY_OF(DistributedKeyContainer) ppDistributedKeys;
	const U32 uNextPendingActionID; AST(DEF(1))
	const U32 uNextPurchaseID; AST(DEF(1))

	CONST_EARRAY_OF(RecruiterContainer) eaRecruiters;	AST(FORCE_CONTAINER)
	CONST_EARRAY_OF(RecruitContainer) eaRecruits;		AST(FORCE_CONTAINER)

	CONST_OPTIONAL_STRUCT(CachedAccountSubscriptionList) pCachedSubscriptionList;
	EARRAY_OF(ExpectedSubscription) eaExpectedSubs; NO_AST

	const bool bInternalUseLogin;										// Account can be used by trusted IPs only
	const bool bLoginDisabled;											// Login on this account is disabled
	CONST_STRING_EARRAY ppProductKeys;
	CONST_STRING_EARRAY ppDeprecatedDistributedProductKeys; AST(NAME(ppDistributedProductKeys))

	const AccountPersonalInfo personalInfo;
	const AccountLoginData loginData; AST(ADDNAMES(lastLogin))
	CONST_STRING_MODIFIABLE displayName; AST(ESTRING CASE_SENSITIVE)

	const PaymentMethodType forbiddenPaymentMethodTypes;

	const char validateEmailKey[11];
	const U32 flags; // ACCOUNT_FLAG_*

	// Permission cache and ban flags
	AccountPermission **ppPermissionCache; NO_AST
	U32 uPermissionCachedTimeSS2000; NO_AST
	AccountTicketCache **ppTickets; NO_AST

	const bool bBillingEnabled;
	const bool bBilled;

	CONST_STRING_MODIFIABLE defaultLocale; AST(ESTRING) // Provided by the web site, possibly empty
	CONST_STRING_MODIFIABLE defaultCurrency; AST(ESTRING) // Provided by the web site, possibly empty
	CONST_STRING_MODIFIABLE defaultCountry; AST(ESTRING) // Provided by the web site, possibly empty

	// PWCommon Account Data associated with Cryptic AccountInfo
	CONST_STRING_MODIFIABLE pPWAccountName;
	const bool bPWAutoCreated;

	CONST_EARRAY_OF(AccountProfile) eaProfiles;

	CONST_EARRAY_OF(RecentPurchaseAmount) eaRecentPurchaseAmounts;
	CONST_EARRAY_OF(AccountSpendingCap) eaSpendingCaps;

	CONST_EARRAY_OF(SavedMachine) eaSavedClients;
	CONST_EARRAY_OF(SavedMachine) eaSavedBrowsers;
	const AccountMachineLockState eMachineLockState;
	const U32 uSaveNextMachineExpire;

	// Non-persisted flags
	U8 temporaryFlags; AST_NOT(PERSIST)

	AST_COMMAND("Set Internal Use Flag", "setInternalUse $FIELD(uID) $INT(Value)")
	AST_COMMAND("Set Login Disabled Flag", "setLoginDisabled $FIELD(uID) $INT(Value)")
	AST_COMMAND("Set Password", "changePassword $FIELD(uID) $STRING(New Password)")
	AST_COMMAND("Manually Assign Key", "assignProductKey, $FIELD(uID) $STRING(Product Key)")
	AST_COMMAND("Add/Modify Permissions", "addPermission $FIELD(uID) $STRING(Product Name) $STRING(Permission String) $INT(Access Level)")
	AST_COMMAND("Change Personal Info", "setPersonalInfo $FIELD(uID) $STRING(First Name) $STRING(Last Name) $STRING(Email)")
	AST_COMMAND("Rename Account", "renameAccount $FIELD(uID) $STRING(New Account Name) $STRING(New Display Name)")
	AST_COMMAND("Remove Flags", "removeProductFlags $FIELD(uID) $INT(Index)")
	AST_COMMAND("Toggle Invalid", "flagInvalidDisplayName $FIELD(uID)")
	AST_COMMAND("Apply Transaction","ServerMonTransactionOnEntity Account $FIELD(UID) $STRING(Transaction String)")
} AccountInfo;
AST_PREFIX();

typedef struct ContainerIOConfig ContainerIOConfig;

AUTO_STRUCT;
typedef struct DatabaseConfig
{
	char	*pDatabaseDir;						//"" Defaults to C:/<Product Name>/objectdb
	char	*pCloneDir;							//Defaults to pDatabaseDir/../cloneobjectdb
	bool	bNoHogs;							//force discrete container file storage.

	bool	bShowSnapshots;						//Whether to show/hide the snapshot console window.
	bool	bBackupSnapshot;	AST(DEF(true))	//Whether to create backup snapshots

	U32		iIncrementalInterval;				//The number of minutes between incremental hog rotation (defaults to 5)
	U32		iSnapshotInterval;					//The number of minutes between snapshot creation		 (defaults to 60)
	U32		iIncrementalHoursToKeep;			//The number of hours to keep incremental files around.	 (default to 4)

	U32		iDaysBetweenDefrags;				AST(DEFAULT(1))//The number of days between automated defrags. 0 means no automated defrags
	U32		iTargetDefragWindowStart;			AST(DEFAULT(3))//Automated defrags will happen after start and before start + duration.
	U32		iTargetDefragWindowDuration;		AST(DEFAULT(2))//

	ContainerIOConfig *IOConfig;
} DatabaseConfig;

SA_RET_NN_STR const char * dbAccountDataDir(void);
int AccountServerInit(void);
void AccountServerShutdown(void);
int AccountServerOncePerFrame(F32 fTotalElapsed, F32 fElapsed);

AccountTicketCache *AccountFindTicketByID(AccountInfo *account, U32 uTicketID);

// Permission Utility Functions
void accountPermissionValueStringParse(const char *permissionString, char *** eaShards, AccountPermissionValue ***eaValues);
void accountPermissionStringParse(const char *pPermissionString, char ***eaPermissionParts);

// Permission Comparison Functions
int accountPermissionValueCmp(const AccountPermissionValue **pptr1, const AccountPermissionValue **pptr2);

LoginFailureCode accountIsAllowedToLogin(AccountInfo *account, bool bLocalIp);
// Gets the PWE email if account is linked, Cryptic Email otherwise;
const char *accountGetEmail(const AccountInfo *account);

AUTO_ENUM;
typedef enum LoginProtocol
{
	LoginProtocol_Unknown = 0,
	LoginProtocol_AccountNet,
	LoginProtocol_Xmlrpc,
} LoginProtocol;

// Login Attempt Data used in logging
// Member variables are in approximate order of consideration in authentication, with unconsidered information at the end.
AUTO_STRUCT;
typedef struct AccountLoginAttemptData
{
	// Information used for login - only pPrivateAccountName, eLoginType, and pMachineID are used for logging
	const char *pPrivateAccountName;			// Account name attempted

	//"old style" passwords, pre-May 2012
	const char *pSHA256Password;				// SHA-256 hashed password (Cryptic)
	const char *pMD5Password;					// MD5 hashed password (Perfect World)

	//"new style", salted differently, account name salted in
	const char *pCrypticPasswordWithAccountNameAndNewStyleSalt;	

	// Used for e-mail login; the password is salted with the fixed salt below
	const char *pPasswordFixedSalt;
	const char *pFixedSalt;

	// Encrypted password
	const char *pPasswordEncrypted;
	AccountServerEncryptionKeyVersion eKeyVersion;

	U32 salt;									// Extra salt used for SHA-256 hashes for Cryptic credentials (0 = no salt)
	bool bIpNeedsPassword;						// If the IP is allowed to log in skipping password checks
	bool bIpAllowedAutocreate;					// If the IP is allowed to auto-create an account if it does not exist
	AccountLoginType eLoginType;			    // Login Account/Authentication Type
	const char *pMachineID;						// Machine ID

	// Other basic information
	bool bLoginSuccessful;						// true if the login was ultimately successful
	bool bRejectedAPriori;						// true if we decided to reject this login attempt before looking up the account
	const char *aPrioriReason;					// reason that bRejectedAPriori is true

	// Authentication information and login criteria.
	const char *matchingDisplayName;			// Public account name for the account, if account exists
	U32 uMatchingAccountId;						// Account ID, if account exists - Batch ID for PW logins
	bool bGoodAccount;							// true if this is a real account name
	bool bGoodPassword;							// true if the password matched
	bool bLoginAllowed;							// true if login is allowed (not disabled and not internal from external IP, etc; not related to permission checking)
	bool bInternal;								// true if this is an internal login, on the basis of its IP
	bool bIpBlocked;							// true if an IP address from this request is blocked
	bool bLinkBlocked;							// true if we're preventing them from logging in because they're linked
	bool bMachineLocked;                        // true if the account has machine locking enabled
	bool bPasswordDecrypted;					// true if the Account Server tried to decrypt the password
	bool bAccountNameIsEmail;					// true if the account name used is their e-mail address

	// Request origin information
	LoginProtocol protocol;						// Protocol used to submit login request
	const char *const *ips;						// External IP list for request.  This is the same as for account creation: the IP list starts with the actual IP
												// our internal system got the request from, followed by any reported Vias, followed by the origin IP that the proxy
												// chain claims
	const char *location;						// Location of login attempt (eg. URL)
	const char *referrer;						// Location referrer information (eg. HTTP Referer)
	const char *clientVersion;					// Client version, if available (eg. HTTP User-Agent)
	const char *note;							// Any special free-form notes from the login point of origin
	const char *peerIp;							// This is the IP of the internal system we got the request from (for instance, the website), if any.
} AccountLoginAttemptData;

// Report this login attempt.
void accountLogLoginAttempt(AccountLoginAttemptData *pLoginAttemptData);

typedef void (* ConfirmLoginCallback)(AccountInfo *, LoginFailureCode, U32, void *);

// Confirm login credentials and log in an account.
// See accountLogLoginAttempt() for an explanation of logging parameters.
void confirmLogin(SA_PARAM_NN_VALID AccountLoginAttemptData *pLoginAttemptData,
	bool bCreateConflictTicket,
	SA_PARAM_NN_VALID ConfirmLoginCallback callback,
	void * userData);

typedef enum MachineType MachineType;
// Returns if the given machine ID is allowed to access the given account; changes the failure code if false
bool accountIsMachineIDAllowed(AccountInfo *pAccount, const char *pMachineID, MachineType eType, const char *ip, LoginFailureCode *failureCode);

// Request a One-Time code to be generated for the account and machine ID
void generateOneTimeCode(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_STR const char *pMachineID, SA_PARAM_NN_NN_STR char **ips, LoginProtocol eLoginProtocol);
// Confirm and Activate One-Time Code for account
bool processOneTimeCode (AccountInfo *account, AccountLoginAttemptData *pLoginData, const char *pOneTimeCode, const char *pMachineName, LoginFailureCode *failureCode);

// Return false if the account name or password is too long.
bool confirmLoginInfoLength(SA_PARAM_OP_STR const char *pLoginField, SA_PARAM_OP_STR const char *pPassword, SA_PARAM_OP_VALID STRING_EARRAY ips, char **estrFailureReason);

void accountAddExpectedSub(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pSubInternalName);
U32 accountExpectsSub(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pSubInternalName);
void accountRemoveExpectedSub(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pSubInternalName);


// Used by createShortUniqueString
#define ALPHA_NUMERIC_CAPS "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define ALPHA_NUMERIC_CAPS_READABLE "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"

// Creates a unique string no more than uMaxLen characters long, consisting of characters found in pTable
SA_RET_NN_STR char * createShortUniqueString(unsigned int uMaxLen, SA_PARAM_NN_STR const char *pTable);

// Gets the account server ID
int getAccountServerID(void);

void StartMerger(bool hideMerger, bool defrag);
const DatabaseConfig *GetDatabaseConfig(void);

typedef struct TransactionReturnVal TransactionReturnVal;

// Create generic managed return value that alerts on transaction failures.
TransactionReturnVal * astrRequireSuccess(SA_PARAM_OP_VALID const char *pExtraInformation);

// Redirect stdout to a file for a code fragment
#define REDIRECT_STDOUT_START(filename) \
	{ void *rdPtr = NULL; void *rdFilePtr = &rdPtr; \
		freopen_s(rdFilePtr, STACK_SPRINTF("%s%s", dbAccountDataDir(), filename), "w+", stdout);

#define REDIRECT_STDOUT_END \
		freopen_s(rdFilePtr, "CONOUT$", "w+", stdout); \
	}

AUTO_ENUM;
typedef enum IPRateLimitActivity
{
	IPRLA_Test = 0, // Used for testing; should have no cost
	IPRLA_AccountCreation,
	IPRLA_Authentication,
	IPRLA_AuthenticationSuccess,
	IPRLA_AuthenticationFailure,
	IPRLA_AuthenticationMachineUnknown,
	IPRLA_OneTimeCodeGenerate,
	IPRLA_OneTimeCodeSuccess,
	IPRLA_OneTimeCodeFailure,
} IPRateLimitActivity;

bool IPRateLimit(SA_PARAM_NN_NN_STR const char * const * easzIP, IPRateLimitActivity eActivity);

// Returns SS2000 for when an IP will be unblocked (or 0 if it is not blocked)
U32 IPBlockedUntil(SA_PARAM_NN_STR const char * szIP);

SA_RET_OP_VALID RateLimitBlockedIter * IPBlockedIterCreate(void);
#define IPBlockedIterNext(iter) RLBlockedIterNext(iter)
#define IPBlockedIterDestroy(iter) RLBlockedIterDestroy(iter)

typedef struct ProductContainer ProductContainer;
typedef struct PWCommonAccount PWCommonAccount;
typedef struct SubscriptionCreateData SubscriptionCreateData;
typedef struct ProductKeyGroup ProductKeyGroup;
typedef struct ProductKeyBatch ProductKeyBatch;
void AccountServer_ReplaceDatabase(EARRAY_OF(AccountInfo) eaAccounts, EARRAY_OF(ProductContainer) eaProducts, 
	EARRAY_OF(PWCommonAccount) eaPWAccounts, EARRAY_OF(SubscriptionCreateData) eaSubscriptions, 
	EARRAY_OF(ProductKeyGroup) eaPKGroups, EARRAY_OF(ProductKeyBatch) eaPKBatches);

int AccountCreateTicket(AccountLoginType eLoginType, SA_PARAM_NN_VALID AccountInfo *account, const char *pMachineID, U32 uIp, bool bIgnoreMachineID);

void StartupFail(FORMAT_STR const char* format, ...);

#include "AccountServer_h_ast.h"

#endif
