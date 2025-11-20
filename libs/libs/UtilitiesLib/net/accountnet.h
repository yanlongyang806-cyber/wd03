#ifndef ACCOUNTNET_H
#define ACCOUNTNET_H
#pragma once
#include "memcheck.h" // for GCC_SYSTEM
#include "stdtypes.h" // for bool, U32, etc.
GCC_SYSTEM

#include "GlobalComm.h"
#include "../../crossroads/common/accountCommon.h"
#include "utils.h"
#include "websrv.h"
#include "AppLocale.h"

#define ACCOUNT_PROXY_PROTOCOL_VERSION 2 // Change this whenever packet structures with the proxy change

typedef struct Packet Packet;
typedef struct Money Money;
typedef struct NetLink NetLink;
typedef struct NetComm NetComm;
typedef struct StashTableImp *StashTable;
typedef enum AccountLoginType AccountLoginType;

// These differ in pointerness due to net.h bizarrely using function type rather than function pointers.
typedef void (*FailedLoginCallback)(const char *reason, void *userData);
typedef void PacketCallback(Packet *pkt,int cmd,NetLink* link,void *user_data);
typedef void LinkCallback(NetLink* link,void *user_data);

#define ACCOUNT_FASTLOGIN_LABEL "{ticketID}"

LATELINK;
char * getAccountServer(void);


bool accountServerWasSet(void);
NetComm *accountCommDefault(void);

char * accountHashPassword_s (const char *password, char *buffer, size_t buffer_size);
#define accountHashPassword(pwd,buffer) accountHashPassword_s(pwd,buffer,ARRAY_SIZE_CHECKED(buffer))

char * accountAddSaltToHashedPassword_s (const char *passwordHash, U32 salt, char *buffer, size_t buffer_size);
#define accountAddSaltToHashedPassword(pwd,salt,buffer) accountAddSaltToHashedPassword_s(pwd,salt,buffer,ARRAY_SIZE_CHECKED(buffer))

int accountConvertHexToBase64_s (const char *hexString, char *buffer, size_t buffer_size);
#define accountConvertHexToBase64(hex,buffer) accountConvertHexToBase64_s(hex,buffer,ARRAY_SIZE_CHECKED(buffer))

#define accountPWHashPassword(account,pwd,buffer) accountPWHashPassword_s(account,pwd,buffer,ARRAY_SIZE_CHECKED(buffer))
char * accountPWHashPassword_s (const char *account_name, const char *password, char *buffer, size_t buffer_size);

#define accountPWHashPasswordFixedSalt(salt,pwd,buffer) accountPWHashPasswordFixedSalt_s(salt,pwd,buffer,ARRAY_SIZE_CHECKED(buffer))
char * accountPWHashPasswordFixedSalt_s (const char *salt, const char *password, char *buffer, size_t buffer_size);

char *accountAddAccountNameToHashedPassword_s(const char *passwordHash, const char *accountName, char *buffer, size_t buffer_size);
#define accountAddAccountNameToHashedPassword(pwd, actname, buffer) accountAddAccountNameToHashedPassword_s(pwd, actname, buffer,ARRAY_SIZE_CHECKED(buffer))


//as of the may 2012 security updates, we're adding salts (both the temporary ones and account names for permanent storage) in a slightly
//new, vaguely more correct, fashion. Make sure the salting style you use matches on both sides. "New Style" basically
//means calling cryptAddSaltToHash()
char *accountAddAccountNameThenNewStyleSaltToHashedPassword_s(const char *passwordHash, const char *accountName, U32 iSalt, char *buffer, size_t buffer_size);
#define accountAddAccountNameThenNewStyleSaltToHashedPassword(pwd, actname, iSalt, buffer) accountAddAccountNameThenNewStyleSaltToHashedPassword_s(pwd, actname, iSalt, buffer,ARRAY_SIZE_CHECKED(buffer))

char * accountAddNewStyleSaltToHashedPassword_s (const char *passwordHash, U32 salt, char *buffer, size_t buffer_size);
#define accountAddNewStyleSaltToHashedPassword(pwd,salt,buffer) accountAddNewStyleSaltToHashedPassword_s(pwd,salt,buffer,ARRAY_SIZE_CHECKED(buffer))

// Specific Hashes - SHA256 are base64-encoded, MD5 are Hexadecimal Strings
char *accountMD5HashPassword_s(const char *password, char *buffer, size_t buffer_size);
char *accountSHA256HashPassword_s(const char *password, char *buffer, size_t buffer_size);
#define accountMD5HashPassword(pwd,buffer) accountMD5HashPassword_s(pwd,buffer,ARRAY_SIZE_CHECKED(buffer))
#define accountSHA256HashPassword(pwd,buffer) accountSHA256HashPassword_s(pwd,buffer,ARRAY_SIZE_CHECKED(buffer))

void accountCancelLogin(void);

// Packet sending functions
void accountSendSaltRequest(NetLink * pLink, const char * pLoginField);
void accountSendLoginPacket(
	NetLink *link, const char * pLoginField, const char * pPassword, bool bPasswordHashed,
	U32 iSalt, const char * pFixedSalt, bool bMachineID, const char * pPreEncryptedPassword);
void accountSendLoginValidatePacket(NetLink * pLink, U32 uAccountID, U32 uTicketID);
void accountSendGenerateOneTimeCode(NetLink * pLink, U32 uAccountID, U32 uIP, const char * pMachineID);
void accountSendSaveNextMachine(NetLink * pLink, U32 uAccountID, const char * pMachineID, const char * pMachineName, U32 uIP);
void accountSendOneTimeCode(NetLink * pLink, U32 uAccountID, const char * pMachineID, const char * pOneTimeCode, const char * pMachineName, U32 uIP);

typedef struct AccountValidateData
{
	const char *pLoginField;
	const char *pPassword;
	bool bPasswordHashed;
	PacketCallback* login_cb;
	FailedLoginCallback failed_cb;
	LinkCallback *connect_cb;
	void *userData;
	AccountLoginType eLoginType;
} AccountValidateData;

NetLink * accountValidateInitializeLinkEx(SA_PARAM_NN_VALID AccountValidateData *pValidateData);
bool accountValidateWaitForLink(NetLink **pLink, F32 timeout);
int accountValidateStartLoginProcess(const char * pLoginField);
void accountValidateCloseAccountServerLink(void);

AUTO_STRUCT;
typedef struct AccountSaltRequest
{
	char pLoginField[MAX_LOGIN_FIELD]; // could be account name or e-mail address; whatever was typed in
} AccountSaltRequest;

#define ACCOUNTPERMISSION_BANNED 0x01

AUTO_STRUCT;
typedef struct AccountPermissionStruct
{
	char *pProductName; AST(ESTRING)
	char *pPermissionString; AST(ESTRING)
	int iAccessLevel;
	U32 uFlags; // special flags, eg. banned
} AccountPermissionStruct;

AUTO_STRUCT;
typedef struct AccountTicket
{
	U32 accountID;
	char accountName[MAX_ACCOUNTNAME];
	char displayName[MAX_ACCOUNTNAME];
	char *pwAccountName;
	U32 uExpirationTime;

	AccountPermissionStruct **ppPermissions;

	U32 uSalt;
	bool bInvalidDisplayName;
	bool bMachineRestricted; // True if the account is machine-locked and the machine ID that generated the ticket request was unknown
	bool bSavingNext; // True if bMachineRestricted and the account is set to automatically save the next client machine ID
	AccountLoginType eLoginType; // Default == Unspecified
} AccountTicket;

AUTO_STRUCT;
typedef struct AccountTicketSigned
{
	char *ticketText; AST(ESTRING) // text-parsered ticket
	U32 uExpirationTime;
	
	char * strTicketTPI; AST(ESTRING) // TextParser information; not hashed
	U32 uTicketCRC; // CRC for the ParseTable for the AccountTicket
	char *ticketSignature; AST(ESTRING) // RSA-SHA signature hash of ticketText (including NULL terminator)
} AccountTicketSigned;

#define ACCOUNTUSER_EXPORT_FILENAME "userlist.txt"
AUTO_STRUCT;
typedef struct AccountGenericList
{
	AccountTicket **ppAccounts; // just store them as tickets
} AccountGenericList;

AccountPermissionStruct* findGamePermissions(AccountTicket *pTicket, const char *pProductName);
AccountPermissionStruct* findGamePermissionsByShard(AccountTicket *pTicket, const char *pProductName, const char *pShardName);

// --------------------------------------------------------------------------------------------------------
// Account Validator: Helper state machine for processing a series of account ticket requests

AUTO_ENUM;
typedef enum AccountValidatorResult
{
	ACCOUNTVALIDATORRESULT_IDLE = 0,
	ACCOUNTVALIDATORRESULT_STILL_PROCESSING,
	ACCOUNTVALIDATORRESULT_FAILED_CONN_TIMEOUT,
	ACCOUNTVALIDATORRESULT_FAILED_GENERIC,
	ACCOUNTVALIDATORRESULT_SUCCESS_AUTH_SUCCESS,
	ACCOUNTVALIDATORRESULT_SUCCESS_AUTH_FAILED,
	ACCOUNTVALIDATORRESULT_OTC_GENERATED,
	ACCOUNTVALIDATORRESULT_OTC_SUCCESS,
	ACCOUNTVALIDATORRESULT_OTC_FAILED,
	ACCOUNTVALIDATORRESULT_SAVENEXTMACHINE_SUCCESS,
} AccountValidatorResult;

typedef struct AccountValidator AccountValidator;

AccountValidator * accountValidatorCreate();
void accountSetValidatorPersistLink (AccountValidator *pValidator, bool bPersistLink);
void accountValidatorDestroyLink(AccountValidator *pValidator);
void accountValidatorDestroy(AccountValidator *pValidator);
bool accountValidatorIsReady(AccountValidator *pValidator);
AccountValidatorResult accountValidatorGetResult(AccountValidator *pValidator);
const char *accountValidatorGetFailureReason(AccountValidator *pValidator);
const char *accountValidatorGetFailureReasonByCode(AccountValidator *pValidator, LoginFailureCode code);
LoginFailureCode accountValidatorGetFailureCode(AccountValidator *pValidator);
void accountValidatorTick(AccountValidator *pValidator);
void accountValidatorRequestTicket(AccountValidator *pValidator, const char *pLogin, const char *pPassword);
void accountValidatorTicketValidate(AccountValidator *pValidator, U32 uAccountID, U32 uTicketID);
void accountValidatorRestartValidation(AccountValidator *pValidator);
bool accountValidatorGetTicket(AccountValidator *pValidator, char **estr);

AccountValidator *** accountValidatorGetValidateRequests(AccountValidator *pBaseValidator);
void accountValidatorRemoveValidateRequest(AccountValidator *pBaseValidator, AccountValidator *pChildValidator);
AccountValidator *accountValidatorAddValidateRequest(AccountValidator *pBaseValidator, U32 uAccountID, U32 uTicketID);
AccountValidator *accountValidatorGenerateOneTimeCode(AccountValidator *pBaseValidator, U32 uAccountID, SA_PARAM_NN_STR const char *pMachineID, U32 uClientIP);
AccountValidator *accountValidatorSaveNextMachine(AccountValidator *pBaseValidator, U32 uAccountID, SA_PARAM_NN_STR const char *pMachineID, SA_PARAM_NN_STR const char *pMachineName, U32 uClientIP);
AccountValidator *accountValidatorAddOneTimeCodeValidation(AccountValidator *pBaseValidator, U32 uAccountID, SA_PARAM_NN_STR const char *pMachineID, SA_PARAM_NN_STR const char *pOneTimeCode, const char *pMachineName, U32 uClientIP);

NetLink *accountValidatorGetAccountServerLink (AccountValidator *pBaseValidator);
AccountValidator *accountValidatorGetPersistent(void);


/************************************************************************/
/* Unique request ID                                                    */
/************************************************************************/

typedef U32 ProxyRequestID;

// Get a unique ID for a new proxy request (unqiue only within one proxy)
ProxyRequestID getNewProxyRequestID(void);

/************************************************************************/
/* Account Proxy Code                                                   */
/************************************************************************/

AUTO_ENUM;
typedef enum AccountProxyActivityType
{
	APACTIVITY_KEYVALUE = 0,
	APACTIVITY_PRODUCT_KEYVALUE,
	APACTIVITY_PRODUCT_REAL,
	APACTIVITY_AUTHCAPTURE,
	APACTIVITY_MOVE,
} AccountProxyActivityType;

AUTO_ENUM;
typedef enum TransactionLogType
{
	// Catch-all case - if you feel the need to use this, PLEASE find out if maybe we should add another type
	TransLogType_Other,

	// Known types so far
	TransLogType_CashPurchase,			// Standard product purchase WITH REAL MONEY
	TransLogType_MicroPurchase,			// Standard product purchase with points
	TransLogType_Stipend,				// Stipend grant, for any reason
	TransLogType_Exchange,				// Currency exchange transaction
	TransLogType_CustomerService,		// Customer service activity
	TransLogType_FoundryTips,			// Giving tips to a foundry author, withdrawing said tips
	TransLogType_Recognition,			// Currency recognition, independent of any purchase activity
} TransactionLogType;

// Obsolete: Use Money or MoneyContainer instead.
AUTO_STRUCT AST_CONTAINER;
typedef struct PriceContainer
{
	const F32 fPrice;				   AST(PERSIST NAME("Price"))
	CONST_STRING_MODIFIABLE pCurrency; AST(PERSIST ESTRING NAME("Currency")) // 'USD' (ISO 4217 Code)
} PriceContainer;
typedef struct NOCONST(PriceContainer) Price;
#define parse_Price parse_PriceContainer

// Stores the result
AUTO_ENUM;
typedef enum AccountKeyValueResult
{
	AKV_SUCCESS = 0,
	AKV_INVALID_KEY,
	AKV_NONEXISTANT,
	AKV_INVALID_RANGE,
	AKV_FAILURE,
	AKV_LOCKED,
	AKV_NOT_LOCKED,
	AKV_INVALID_LOCK,
	AKV_FORBIDDEN_CHANGE,
} AccountKeyValueResult;

#define AKV_SUCCESS_MESSAGE			"Successful."
#define AKV_INVALID_KEY_MESSAGE		"Invalid key."
#define AKV_NONEXISTANT_MESSAGE		"Non-existent key."
#define AKV_INVALID_RANGE_MESSAGE	"Value outside valid range."
#define AKV_FAILURE_MESSAGE			"Failure."
#define AKV_LOCKED_MESSAGE			"Key is currently locked."
#define AKV_NOT_LOCKED_MESSAGE		"Key is not currently locked."
#define AKV_INVALID_LOCK_MESSAGE	"Lock string is invalid or missing."

/************************************************************************/
/* Used to send info from the account server to account proxy           */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxyKeyValueInfo
{
	char *pKey;					AST(ESTRING KEY)
	char *pValue;				AST(ESTRING)
} AccountProxyKeyValueInfo;

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountProxyKeyValueInfoContainer
{
	CONST_STRING_MODIFIABLE pKey;		AST(ESTRING PERSIST SUBSCRIBE NAME("Key") KEY)
	CONST_STRING_MODIFIABLE pValue;		AST(ESTRING PERSIST SUBSCRIBE NAME("Value"))
} AccountProxyKeyValueInfoContainer;

//NOTE NOTE NOTE NOTE the persist versions must be kept identical to the non-persist versions
AUTO_STRUCT;
typedef struct AccountProxyKeyValueInfoList
{
	EARRAY_OF(AccountProxyKeyValueInfo) ppList;
} AccountProxyKeyValueInfoList;
//NOTE NOTE NOTE NOTE the persist versions must be kept identical to the non-persist versions
AUTO_STRUCT AST_CONTAINER;
typedef struct AccountProxyKeyValueInfoListContainer
{
	EARRAY_OF(AccountProxyKeyValueInfoContainer) ppList; AST(PERSIST NAME("List"))
} AccountProxyKeyValueInfoListContainer;
//NOTE NOTE NOTE NOTE the persist versions must be kept identical to the non-persist versions

//This is a wrapper structure for AccountProxyKeyValueInfoList that also contains the account ID and player ID
AUTO_STRUCT;
typedef struct AccountProxyKeyValueData
{
	U32 uAccountID;
	U32 uPlayerID;
	AccountProxyKeyValueInfoList *pList;
} AccountProxyKeyValueData;

AUTO_STRUCT;
typedef struct AccountProxyPermission
{
	STRING_EARRAY ppShards; AST(ESTRING)
	char *pType;			AST(ESTRING)
	char *pValue;			AST(ESTRING)
	char *pProduct;			AST(ESTRING)
} AccountProxyPermission;

AUTO_STRUCT;
typedef struct AccountProxyPermissionsList
{
	EARRAY_OF(AccountProxyPermission) ppPermissions;
} AccountProxyPermissionsList;

/************************************************************************/
/* Used in product activation                                           */
/************************************************************************/

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountProxyKeyLockPairContainer
{
	CONST_STRING_MODIFIABLE pKey;	AST(ESTRING PERSIST KEY)
	CONST_STRING_MODIFIABLE pLock;	AST(ESTRING PERSIST)
} AccountProxyKeyLockPairContainer;

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountProxyProductActivationContainer
{
	CONST_EARRAY_OF(AccountProxyKeyLockPairContainer) ppKeyLocks; AST(ESTRING PERSIST)
	const U32 uActivationKeyLock; AST(PERSIST)
	CONST_STRING_MODIFIABLE pActivationKey; AST(PERSIST ESTRING)
	CONST_STRING_MODIFIABLE pReferrer; AST(PERSIST)
} AccountProxyProductActivationContainer;

typedef struct NOCONST(AccountProxyProductActivationContainer) AccountProxyProductActivation;
#define parse_AccountProxyProductActivation parse_AccountProxyProductActivationContainer
#define TYPE_parse_AccountProxyProductActivation AccountProxyProductActivation

typedef struct NOCONST(AccountProxyKeyLockPairContainer) AccountProxyKeyLockPair;
#define parse_AccountProxyKeyLockPair parse_AccountProxyKeyLockPairContainer
#define TYPE_parse_AccountProxyKeyLockPair AccountProxyKeyLockPair

/************************************************************************/
/* Used in packet communication for getting a key/value pair			*/
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxyGetRequest
{
	char *pProxy;
	U32 uAccountID;
	char *pKey;					AST(ESTRING)
	int iCmdID;
} AccountProxyGetRequest;

AUTO_STRUCT;
typedef struct AccountProxyGetResponse
{
	U32 uAccountID;
	char *pKey;					AST(ESTRING)
	AccountKeyValueResult eResult;
	char *pValue;
	S64 iValue;					AST(NAME(IntValue))
	int iCmdID;
} AccountProxyGetResponse;

/************************************************************************/
/* Used in packet communication for setting a key/value pair            */
/************************************************************************/

AUTO_ENUM;
typedef enum AccountProxySetOperation
{
	AKV_OP_SET = 0,
	AKV_OP_INCREMENT,
} AccountProxySetOperation;

AUTO_STRUCT;
typedef struct AccountProxySetRequest
{
	char *pProxy;				AST(ESTRING)	// Filled in by the account proxy
	U32 uAccountID;								// Account this is being used on
	char *pKey;					AST(ESTRING)	// The key
	char *pValue;				AST(ESTRING)	// The string value
	S64 iValue;					AST(NAME(IntValue))
	AccountProxySetOperation operation; AST(NAME(operation)) // The operation (set/inc/etc.)
	ProxyRequestID requestID; AST(INT)
	U32 containerID;
	char *pProductInternalName;
} AccountProxySetRequest;

AUTO_STRUCT;
typedef struct AccountProxySetResponse
{
	U32 uAccountID;
	char *pKey;									AST(ESTRING)
	AccountKeyValueResult result;
	char *pLock;								AST(ESTRING)
	ProxyRequestID requestID; AST(INT)
	U32 containerID;
	char *pValue; // The existing value BEFORE locking
	S64 iValue;									AST(NAME(IntValue))
} AccountProxySetResponse;

/************************************************************************/
/* Used in simple, non-atomic key-value changes                         */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxySimpleSetRequest
{
	char *pProxy;				AST(ESTRING)	// Filled in by the account proxy
	U32 uAccountID;								// Account this is being used on
	char *pKey;					AST(ESTRING)	// The key
	char *pValue;				AST(ESTRING)	// The string value
	S64 iValue;					AST(NAME(IntValue))
	AccountProxySetOperation operation; AST(NAME(operation)) // The operation (set/inc/etc.)
	ProxyRequestID requestID;	AST(INT)		// ID of the request
} AccountProxySimpleSetRequest;

AUTO_STRUCT;
typedef struct AccountProxySimpleSetResponse
{
	ProxyRequestID requestID;	AST(INT)		// ID of the request
	AccountKeyValueResult result;				// Result of the operation
} AccountProxySimpleSetResponse;

/************************************************************************/
/* Used to request subbed time (for vet rewards etc.)                   */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxySubbedTimeRequest
{
	char *pProductInternalName;
	U32 uAccountID;
} AccountProxySubbedTimeRequest;

AUTO_STRUCT;
typedef struct AccountProxySubbedTimeResponse
{
	char *pProductInternalName;
	U32 uAccountID;
	U32 uTotalSecondEstimate;
} AccountProxySubbedTimeResponse;

/************************************************************************/
/* Used to request account linking status                               */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxyLinkingStatusRequest
{
	U32 uAccountID;
} AccountProxyLinkingStatusRequest;

AUTO_STRUCT;
typedef struct AccountProxyLinkingStatusResponse
{
	U32 uAccountID;
	bool bLinked;
	bool bShadow;
} AccountProxyLinkingStatusResponse;

/************************************************************************/
/* Used to request account playtime info                                */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxyPlayedTimeRequest
{
	U32 uAccountID;
	char *pProduct;
	char *pCategory;
} AccountProxyPlayedTimeRequest;

AUTO_STRUCT;
typedef struct AccountProxyPlayedTimeResponse
{
	U32 uAccountID;
	char *pProduct;
	char *pCategory;
	U32 uFirstPlayed;
	U32 uTotalPlayed;
} AccountProxyPlayedTimeResponse;

/************************************************************************/
/* Used to request all game-related account data                        */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxyAccountDataRequest
{
	U32 uAccountID;
	char *pProduct;
	char *pCategory;
	char *pShard;
	char *pCluster;
} AccountProxyAccountDataRequest;

AUTO_STRUCT;
typedef struct AccountProxyAccountDataResponse
{
	U32 uAccountID;
	U32 uSubscribedSeconds;
	U32 uFirstPlayedSS2000;
	U32 uTotalPlayedSS2000;
	U32 uLastLoginSS2000;
	U32 uLastLogoutSS2000;
	U32 uHighestLevel;
	U32 uNumCharacters;
	bool bLinkedAccount;
	bool bShadowAccount;
	bool bBilled;
	AccountProxyKeyValueInfoList *pKeyValues;
} AccountProxyAccountDataResponse;

/************************************************************************/
/* Used to modify AS character counts                                   */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxyNumCharactersRequest
{
	U32 uAccountID;
	char *pProduct;
	char *pCategory;
	U32 uNumCharacters;
	bool bChange;
} AccountProxyNumCharactersRequest;

/************************************************************************/
/* Used in commit and rollback requests                                 */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxyCommitRollbackRequest
{
	U32 uAccountID;
	char *pKey;		AST(ESTRING)
	char *pLock;	AST(ESTRING)
	TransactionLogType eTransactionType;
	U32 containerID;
} AccountProxyCommitRollbackRequest;

AUTO_STRUCT;
typedef struct AccountProxyAcknowledge
{
	U32 uAccountID;
	char *pKey;		AST(ESTRING)
	char *pLock;	AST(ESTRING)
	U32 containerID;
} AccountProxyAcknowledge;

/************************************************************************/
/* Used for getting account ID from display name                        */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxyAccountIDFromDisplayNameRequest
{
	char *pDisplayName; AST(ESTRING)
} AccountProxyAccountIDFromDisplayNameRequest;

AUTO_STRUCT;
typedef struct AccountProxyAccountIDFromDisplayNameResponse
{
	U32 uAccountID;
	char *pDisplayName; AST(ESTRING)
} AccountProxyAccountIDFromDisplayNameResponse;

/************************************************************************/
/* Used to notify the account server to keep a lock on a key            */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxyKeepLock
{
	char *pProxy;	AST(ESTRING)
	U32 uAccountID;
	char *pKey;		AST(ESTRING)
	U32 uProductID;
	AccountProxyActivityType activityType;
} AccountProxyKeepLock;

/************************************************************************/
/* Begin or end proxy lock walk                                         */
/************************************************************************/

// This packet was originally sent repeatedly but has been repurposed to be part
// of the initial handshake done between the proxy and Account Server and is only
// sent once.
AUTO_STRUCT;
typedef struct AccountProxyBeginEndWalk
{
	char *pProxy; AST(ESTRING)
	char *pCluster; AST(ESTRING)
	char *pEnvironment; AST(ESTRING)
} AccountProxyBeginEndWalk;

/************************************************************************/
/* Request all key values                                               */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxyRequestAllKeyValues
{
	U32 uAccountID;
	U32 uPlayerID;
} AccountProxyRequestAllKeyValues;

/************************************************************************/
/* Logout notification                                                  */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountLogoutNotification
{
	U32 uPlayTime;
	U32 uAccountID;
	char *pProductName; AST(ESTRING)
	char *pShardCategory; AST(ESTRING)
	U32 uLevel;
} AccountLogoutNotification;

/************************************************************************/
/* Containers stored in the DB                                          */
/************************************************************************/

AUTO_ENUM;
typedef enum AccountProxyResult
{
	APRESULT_PENDING_ACCOUNT_SERVER_AUTHORIZE = 0,
	APRESULT_COMMIT,
	APRESULT_ROLLBACK,
	APRESULT_WAITING_FOR_COMPLETION,
	APRESULT_TIMED_OUT,
} AccountProxyResult;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(id) AST_IGNORE(uProductID) AST_IGNORE_STRUCT(pActivation);
typedef struct AccountProxyLock
{
	//CONST_STRING_MODIFIABLE id;				AST(KEY PERSIST ESTRING)
	const U32 uAccountID;					AST(PERSIST)
	CONST_STRING_MODIFIABLE pKey;			AST(PERSIST ESTRING)

	// Optional, if the activity in question is a move
	const U32 uDestAccountID;				AST(PERSIST)
	CONST_STRING_MODIFIABLE pDestKey;		AST(PERSIST ESTRING)

	const F32 fDestroyTime;					AST(PERSIST)
	const AccountProxyResult result;		AST(PERSIST)
	const AccountProxyActivityType activityType;	AST(PERSIST)
	const TransactionLogType eTransactionType;	AST(PERSIST)
	const ProxyRequestID requestID;		 AST(INT PERSIST) // Request ID used to set it

	// One of the following must be set.  If the lock is set, it's either an old-style purchase
	// or a keyvalue set.  If uPurchaseID is non-zero, it's a new style purchase.  With the
	// new purchase system, the purchase ID serves the purpose the lock did.
	CONST_STRING_MODIFIABLE pLock;			AST(PERSIST ESTRING)
	const U32 uPurchaseID;					AST(PERSIST)

	// Special for Steam or possibly other third-party purchases - marks the external order ID
	// associated with the lock. Indexed via objIndex so that we can find a lock from a
	// particular order ID after user confirmation occurs.
	CONST_STRING_MODIFIABLE pOrderID;		AST(PERSIST)

	const int iCmdID;						// Not persisted since a crash of the Account Proxy
											// would likely invalidate this.
} AccountProxyLock;

AUTO_STRUCT AST_CONTAINER AST_IGNORE_STRUCT(ppLocks) AST_IGNORE(dirty) AST_IGNORE(sentToAccountServer);
typedef struct AccountProxyLockContainer
{
	const U32 id;									AST(KEY PERSIST)
	CONST_OPTIONAL_STRUCT(AccountProxyLock) pLock;	AST(PERSIST)
} AccountProxyLockContainer;

/************************************************************************/
/* Caching functions                                                    */
/************************************************************************/

bool stashReplaceStruct(SA_PARAM_NN_VALID StashTable pTable, SA_PARAM_NN_STR const char *pKey, SA_PARAM_OP_VALID void *pValue, SA_PARAM_NN_VALID ParseTable pt[]);
bool stashRemoveStruct(SA_PARAM_NN_VALID StashTable pTable, SA_PARAM_NN_STR const char *pKey, SA_PARAM_NN_VALID ParseTable pt[]);
void stashClearStruct(SA_PARAM_NN_VALID StashTable pTable, SA_PARAM_NN_VALID ParseTable pt[]);

SA_RET_NN_STR const char *AccountGetShardProxyName(void);
void AccountProxyClearKeyValueList(void);
bool AccountProxyReplaceKeyValue(SA_PARAM_NN_VALID AccountProxyKeyValueInfo *pItem);
bool AccountProxySetKeyValueList(SA_PARAM_OP_VALID AccountProxyKeyValueInfoList *pList);
bool AccountProxyGetKeyValue(SA_PARAM_NN_STR const char *pKey, SA_PRE_NN_FREE SA_POST_NN_VALID AccountProxyKeyValueInfo **pItem);
bool AccountProxyRemoveKeyValue(SA_PARAM_NN_STR const char *pKey);
char *AccountProxyFindValueFromKeyInList(SA_PARAM_OP_VALID const AccountProxyKeyValueInfoList *pList, SA_PARAM_NN_STR const char *pKey);
char *AccountProxyFindValueFromKey(SA_PARAM_OP_VALID CONST_EARRAY_OF(AccountProxyKeyValueInfo) keyValues, SA_PARAM_NN_STR const char *pKey);
char *AccountProxyFindValueFromKeyContainer(SA_PARAM_OP_VALID CONST_EARRAY_OF(AccountProxyKeyValueInfoContainer) keyValues, SA_PARAM_NN_STR const char *pKey);

SA_RET_NN_STR const char *AccountProxySubstituteKeyTokens(SA_PARAM_NN_STR const char *pKey,
														  SA_PARAM_OP_STR const char *pProxy,
														  SA_PARAM_OP_STR const char *pCluster,
														  SA_PARAM_OP_STR const char *pEnvironment);
bool AccountProxyKeysMeetRequirements(SA_PARAM_OP_VALID const AccountProxyKeyValueInfoList *pList,
									  const char * const * pRequirementStack,
									  SA_PARAM_OP_STR const char *pProxy,
									  SA_PARAM_OP_STR const char *pCluster,
									  SA_PARAM_OP_STR const char *pEnvironment,
									  SA_PARAM_OP_VALID int *pError,
									  char *** pppKeysUsed);

/************************************************************************/
/* Account identifier                                                   */
/************************************************************************/

// Used to identify an account
AUTO_STRUCT;
typedef struct AccountIdentity
{
	U32 uAccountID;
	char * pDisplayName; AST(ESTRING)
} AccountIdentity;

//global keys
char *GetAccountBannedKey(void);
char *GetAccountSuspendedKey(void);
char *GetAccountGMKey(void);
char *GetAccountTutorialDoneKey(void);
char *GetAccountUgcEditBanKey(void);//DEPRECATED
char *GetAccountUgcPublishBanKey(void);
char *GetAccountUgcProjectExtraSlotsKey(void);
char *GetAccountUgcProjectSeriesExtraSlotsKey(void);
char *GetAccountUgcReviewerKey(void);
char *GetAccountUgcCreateProjectEULAKey(void);
char *GetAccountUgcProjectSearchEULAKey(void);

//specifies that a given account IS allowed to play on a particular virtual shard
char *GetAccountVShardAllowedKey(U32 iVShardContainerID);

//specifies that a given account is NOT allowed to play on a particular virtual shard (overrides previous)
char *GetAccountVShardNotAllowedKey(U32 iVShardContainerID);

/************************************************************************/
/* Recruit stuff                                                        */
/************************************************************************/

AUTO_STRUCT AST_CONTAINER;
typedef struct RecruiterContainer
{
	CONST_STRING_MODIFIABLE pProductInternalName;	AST(KEY PERSIST SUBSCRIBE)	// FightClub etc.
	const U32 uAccountID;							AST(PERSIST SUBSCRIBE)		// The person who recruited them for the above product
	const U32 uAcceptedTimeSS2000;					AST(PERSIST SUBSCRIBE)		// When the offer to become a recruit was accepted
} RecruiterContainer;

typedef struct NOCONST(RecruiterContainer) Recruiter;
#define parse_Recruiter parse_RecruiterContainer

AUTO_ENUM;
typedef enum RecruitState
{
	RS_Invalid = 0,			// Impossible
	RS_PendingOffer,
	RS_Offered,
	RS_Accepted,
	RS_Upgraded,
	RS_Billed,
	RS_OfferCancelled,		// Error state
	RS_AlreadyRecruited,	// Error state
} RecruitState;

AUTO_STRUCT AST_CONTAINER;
typedef struct RecruitContainer
{
	const RecruitState eRecruitState;				AST(PERSIST SUBSCRIBE)		// Current state of the recruit

	// Always valid
	CONST_STRING_MODIFIABLE pProductKey;			AST(PERSIST)				// Product key being offered
	CONST_STRING_MODIFIABLE	pEmailAddress;			AST(PERSIST)				// E-mail address the offer was sent to
	CONST_STRING_MODIFIABLE pProductInternalName;	AST(PERSIST SUBSCRIBE)		// Product internal name offered
	const U32 uCreatedTimeSS2000;					AST(PERSIST)				// When this entry was created

	// Valid after offered
	const U32 uOfferedTimeSS2000;					AST(PERSIST)				// When the offer to become a recruit was last sent

	// Valid after acceptance	
	const U32 uAccountID;							AST(PERSIST SUBSCRIBE)		// The person who was recruited (will be 0 until accepted)
	const U32 uAcceptedTimeSS2000;					AST(PERSIST SUBSCRIBE)		// When the offer to become a recruit was accepted
} RecruitContainer;

typedef struct NOCONST(RecruitContainer) Recruit;
#define parse_Recruit parse_RecruitContainer
#define TYPE_parse_Recruit Recruit

AUTO_STRUCT;
typedef struct RecruitInfo
{
	U32 uAccountID;
	EARRAY_OF(Recruit) eaRecruits;
	EARRAY_OF(Recruiter) eaRecruiters;
} RecruitInfo;

AUTO_STRUCT;
typedef struct RequestRecruitInfo
{
	U32 uAccountID;
} RequestRecruitInfo;


/************************************************************************/
/* Discounts                                                            */
/************************************************************************/

#define APPLY_DISCOUNT_EFFECT(price, perc) (price = price * (10000 - (perc)) / 10000)

/************************************************************************/
/* Payment methods                                                      */
/************************************************************************/

AUTO_ENUM;
typedef enum TransactionProvider
{
	TPROVIDER_AccountServer = 0,	// Collected by the Account Server
	TPROVIDER_Steam,				// Collected by Steam before reaching the Account Server
	TPROVIDER_PerfectWorld,			// Collected by Perfect World (i.e. Zen transfer)
} TransactionProvider;

AST_PREFIX(PERSIST)
AUTO_STRUCT AST_CONTAINER;
typedef struct AccountAddress
{
	CONST_STRING_MODIFIABLE address1;   AST(ESTRING)
	CONST_STRING_MODIFIABLE address2;   AST(ESTRING)
	CONST_STRING_MODIFIABLE city;       AST(ESTRING)
	CONST_STRING_MODIFIABLE district;   AST(ESTRING) // US: State
	CONST_STRING_MODIFIABLE postalCode; AST(ESTRING) // US: ZIP
	CONST_STRING_MODIFIABLE country;    AST(ESTRING) // "US"
	CONST_STRING_MODIFIABLE phone;      AST(ESTRING) // "408-555-1212"
} AccountAddress;

AUTO_STRUCT AST_CONTAINER;
typedef struct CachedCreditCard
{
	CONST_STRING_MODIFIABLE bin;		AST(ESTRING)	// First 6 digits of the card number
	CONST_STRING_MODIFIABLE lastDigits; AST(ESTRING)	// Last 4 or 5 digits of the card number.
	const int numDigits;								// Number of digits in card number.
	CONST_STRING_MODIFIABLE expireDate; AST(ESTRING)	// Credit card expiration date
} CachedCreditCard;

AUTO_STRUCT AST_CONTAINER;
typedef struct CachedPayPal
{
	CONST_STRING_MODIFIABLE emailAddress; AST(ESTRING)	// E-mail used for their PayPal account
} CachedPayPal;

AUTO_STRUCT AST_CONTAINER;
typedef struct CachedDirectDebit
{
	CONST_STRING_MODIFIABLE account;		AST(ESTRING)
	CONST_STRING_MODIFIABLE bankSortCode;	AST(ESTRING)
	CONST_STRING_MODIFIABLE ribCode;		AST(ESTRING)
} CachedDirectDebit;

AUTO_STRUCT AST_CONTAINER;
typedef struct CachedPaymentMethod
{
	CONST_STRING_MODIFIABLE VID;						AST(ESTRING)	// Generated by Vindicia
	CONST_STRING_MODIFIABLE description;				AST(ESTRING)	// Customer-supplied description
	CONST_STRING_MODIFIABLE type;						AST(ESTRING)	// Customer-supplied type (Visa etc.)
	CONST_STRING_MODIFIABLE accountName;				AST(ESTRING)	// Customer's name
	const AccountAddress billingAddress;								// Billing address
	CONST_STRING_MODIFIABLE currency;					AST(ESTRING)	// Currency used
	CONST_OPTIONAL_STRUCT(CachedCreditCard) creditCard;
	CONST_OPTIONAL_STRUCT(CachedPayPal) payPal;
	CONST_OPTIONAL_STRUCT(CachedDirectDebit) directDebit;
	const TransactionProvider methodProvider;			AST_NOT(PERSIST)
} CachedPaymentMethod;
AST_PREFIX()

AUTO_STRUCT;
typedef struct PaymentMethodsRequest
{
	U32 uAccountID;
	U64 uSteamID;
	char *pProxy;
	SlowRemoteCommandID iCmdID;
} PaymentMethodsRequest;

AUTO_STRUCT;
typedef struct PaymentMethodsResponse
{
	U32 uAccountID;
	SlowRemoteCommandID iCmdID;
	bool bSuccess;
	EARRAY_OF(CachedPaymentMethod) eaPaymentMethods;
	char * pDefaultCurrency;
} PaymentMethodsResponse;

/************************************************************************/
/* Purchases                                                            */
/************************************************************************/

// Possible purchase results
AUTO_ENUM;
typedef enum {
	PURCHASE_RESULT_COMMIT = 0,
	PURCHASE_RESULT_MISSING_ACCOUNT,
	PURCHASE_RESULT_INVALID_PAYMENT_METHOD,
	PURCHASE_RESULT_INVALID_PRODUCT,
	PURCHASE_RESULT_CANNOT_ACTIVATE,
	PURCHASE_RESULT_PENDING,
	PURCHASE_RESULT_INVALID_PRICE,
	PURCHASE_RESULT_INSUFFICIENT_POINTS,
	PURCHASE_RESULT_COMMIT_FAIL,
	PURCHASE_RESULT_INVALID_CURRENCY,
	PURCHASE_RESULT_AUTH_FAIL,
	PURCHASE_RESULT_MISSING_PRODUCTS,
	PURCHASE_RESULT_ROLLBACK,
	PURCHASE_RESULT_ROLLBACK_FAIL,
	PURCHASE_RESULT_INVALID_IP,
	PURCHASE_RESULT_RETRIEVED,
	PURCHASE_RESULT_RETRIEVE_FAILED,
	PURCHASE_RESULT_PENDING_PAYPAL,
	PURCHASE_RESULT_DELAY_FAILURE,
	PURCHASE_RESULT_SPENDING_CAP_REACHED,
	PURCHASE_RESULT_INVALID_PURCHASE_ID,
	PURCHASE_RESULT_PRICE_CHANGED,
	PURCHASE_RESULT_CURRENCY_LOCKED,
	PURCHASE_RESULT_STEAM_DISABLED,
	PURCHASE_RESULT_STEAM_UNAVAILABLE,
	PURCHASE_RESULT_NOT_READY,
	PURCHASE_RESULT_UNKNOWN_ERROR,
} PurchaseResult;

AUTO_STRUCT;
typedef struct TransactionItem
{
	U32 uProductID;
	Money *pPrice;
	U32 uOverridePercentageDiscount;
} TransactionItem;

AUTO_STRUCT;
typedef struct AuthCaptureRequest
{
	U32 uAccountID;
	SlowRemoteCommandID iCmdID;				// Set by Account Proxy
	char * pPaymentMethodID;
	char * pCurrency;
	char * pProxy;
	char * pIP;
	char * pBankName;
	EARRAY_OF(TransactionItem) eaItems;
	ProxyRequestID requestID; AST(INT)		// Set by Account Proxy
	bool bAuthOnly;
	bool bVerifyPrice;
	U32 uLockContainerID;					// Set by Account Proxy

	U64 uSteamID;							// If provided, request for a particular Steam ID
	char * pLocCode;
	char * pCategory;
	char * pOrderID;
	char * pTransactionID;
	TransactionProvider eProvider; AST(DEFAULT(TPROVIDER_AccountServer))
} AuthCaptureRequest;

AUTO_STRUCT;
typedef struct AuthCaptureResponse
{
	U32 uAccountID;
	SlowRemoteCommandID iCmdID;
	PurchaseResult eResult;
	ProxyRequestID requestID; AST(INT)
	U32 uPurchaseID;
	U32 uLockContainerID;
	char * pCurrency;

	char * pOrderID; // If a Steam transaction, this is the order ID
	char * pLock; // If there's a KV lock, this is the lock string
} AuthCaptureResponse;

AUTO_STRUCT;
typedef struct CaptureRequest
{
	U32 uAccountID;
	U32 uPurchaseID;
	char * pProxy;
	ProxyRequestID requestID; AST(INT)
	U32 uLockContainerID;
	bool bCapture;

	char * pOrderID; // For echoing back upon capture completion
} CaptureRequest;

AUTO_STRUCT;
typedef struct AuthCaptureResultInfo
{
	PurchaseResult eResult;
	U32 uLockContainerID;
} AuthCaptureResultInfo;

/************************************************************************/
/* Structure for getting an authentication ticket for the given player */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxyRequestAuthTicketForOnlinePlayer
{
	U32 uAccountID;
	U32 uIp;
	U32 uTicketID;
	SlowRemoteCommandID iCmdID;
} AccountProxyRequestAuthTicketForOnlinePlayer;


/************************************************************************/
/* Structure for WebSrv Game Events                                     */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxyWebSrvGameEvent
{
	char *pProductShortName;
	char *pEventName;
	U32 uAccountID;
	
	char *pLang;
	WebSrvKeyValueList *keyValueList;
} AccountProxyWebSrvGameEvent;

/************************************************************************/
/* Requests from the shard to create currencies                         */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxyCreateCurrencyRequest
{
	char *pName;
	char *pGame;
	char *pEnvironment;
} AccountProxyCreateCurrencyRequest;

/************************************************************************/
/* Requests from the shard to move currency from one key to another     */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxyMoveRequest
{
	char *pProxy;				AST(ESTRING)
	U32 uSrcAccountID;
	char *pSrcKey;				AST(ESTRING)
	U32 uDestAccountID;
	char *pDestKey;				AST(ESTRING)
	S64 iValue;

	// Data relevant only to the proxy
	ProxyRequestID uRequestID;	AST(INT)
	U32 uLockContainerID;
} AccountProxyMoveRequest;

AUTO_STRUCT;
typedef struct AccountProxyMoveResponse
{
	AccountKeyValueResult eResult;
	char *pLock;					AST(ESTRING)

	// Data relevant only to the proxy
	ProxyRequestID uRequestID;	AST(INT)
	U32 uLockContainerID;
} AccountProxyMoveResponse;

AUTO_STRUCT;
typedef struct AccountProxyCommitRollbackMoveRequest
{
	U32 uSrcAccountID;
	char *pSrcKey;			AST(ESTRING)
	U32 uDestAccountID;
	char *pDestKey;			AST(ESTRING)
	char *pLock;			AST(ESTRING)
	TransactionLogType eTransactionType;

	U32 uLockContainerID;
} AccountProxyCommitRollbackMoveRequest;

//structs and enums used for the various accountnet packets that use ParserSendStructAsCheckedNameValuePairs, each associated
//with a particular packet type. Note that fields in these structs can ONLY be one of two things: ints or fixed-size strings. But within
//that limitation, fields can be added or removed without ever breaking forward/backward compatibility

//stuff relating to FROM_ACCOUNTSERVER_NVPSTRUCT_LOGIN_SALT
typedef enum eLoginSaltType
{
	//should never be used
	LOGINSALTTYPE_UNSPEC = 0,

	//standard login salt type after May 2012 update... cryptic PWs hashed, then salted hashed with AN, then salted hashed with short term salt,
	//PW PWs salted hashed, then salted hashed with short term salt
	LOGINSALTTYPE_SALT_WITH_ACNTNAME_THEN_SHORT_TERM_SALT = 1,

	// Standard login salt type as of e-mail login support; the Account Server will send back a
	// per-account fixed salt that should be used rather than using the account name.
	LOGINSALTTYPE_SALT_WITH_FIXED_THEN_SHORT_TERM_SALT = 2,
} eLoginSaltType;

AUTO_STRUCT;
typedef struct AccountNetStruct_FromAccountServerLoginSalt
{
	int eSaltType; //eLoginSaltType
	int iSalt;
	char pFixedSalt[MAX_FIXED_SALT];
} AccountNetStruct_FromAccountServerLoginSalt;

AUTO_ENUM;
typedef enum AccountServerEncryptionKeyVersion
{
	ASKEY_none = 0, // no password at all
	ASKEY_identity, // no encryption (plaintext)

	// These MUST be of the form "dev_*" or "prod_*" to be loaded properly
	// "prod_" ones will never be used outside of the LIVE Account Server

	ASKEY_dev_1,
	ASKEY_prod_1,

	ASKEY_MAX,
} AccountServerEncryptionKeyVersion;

//stuff relating to TO_ACCOUNTSERVER_NVPSTRUCT_LOGIN
AUTO_STRUCT;
typedef struct AccountNetStruct_ToAccountServerLogin
{
	char hashedCrypticPassword[MAX_PASSWORD];
	char hashedPWPassword[MAX_PASSWORD];
	char accountName[MAX_ACCOUNTNAME];
	int eLoginType; //AccountLoginType
	char machineID[MACHINE_ID_MAX_LEN];
	char hashedPWPasswordFixedSalt[MAX_PASSWORD];
	int eKeyVersion; // AccountServerEncryptionKeyVersion
	char encryptedPassword[MAX_PASSWORD_ENCRYPTED_BASE64];
	LocaleID localeID; AST(INT DEFAULT(LOCALE_ID_INVALID))
} AccountNetStruct_ToAccountServerLogin;

const char * accountNetGetPubKey(AccountServerEncryptionKeyVersion eKeyVersion);

#endif