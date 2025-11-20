#pragma once

#include "AccountManagement_h_ast.h"
#include "AccountServer.h"
#include "ProductKey.h"
#include "JSONRPC.h"

typedef struct Container Container;
typedef struct AccountInfo AccountInfo;
typedef struct AccountTicketSigned AccountTicketSigned;
typedef struct AccountPermission AccountPermission;
typedef struct AccountPermissionValue AccountPermissionValue;
typedef struct AccountPermissionStruct AccountPermissionStruct;
typedef struct ProductContainer ProductContainer;
typedef struct SubscriptionContainer SubscriptionContainer;

AUTO_ENUM;
typedef enum AccountCreationResult
{
	ACCOUNTCREATE_Success = 0,
	ACCOUNTCREATE_AccountNameRestricted,
	ACCOUNTCREATE_AccountNameInvalid,
	ACCOUNTCREATE_DisplayNameInvalid,
	ACCOUNTCREATE_AccountNameConflict,
	ACCOUNTCREATE_DisplayNameConflict,
	ACCOUNTCREATE_AccountNameLength,
	ACCOUNTCREATE_DisplayNameLength,
	ACCOUNTCREATE_EmailConflict,
} AccountCreationResult;

void AccountManagement_DestroyContainers(void);
void createAccountFromStruct(AccountInfo *pNewAccount);

// Secret question and answer pairs.
// The number of questions must match the number of answers.
AUTO_STRUCT;
typedef struct QuestionsAnswers
{
	STRING_EARRAY questions;	AST(ESTRING)
	STRING_EARRAY answers;		AST(ESTRING)
} QuestionsAnswers;

typedef struct TransactionReturnVal TransactionReturnVal;
typedef void (*TransactionReturnCallback)(TransactionReturnVal *returnVal, void *userData);

void initAccountStashTables(void);
void initAccountStashTablesNew(void);
void objAccountAddToContainerStore_CB(Container *con, AccountInfo *account);
void objAccountRemoveFromContainerStore_CB(Container *con, AccountInfo *account);

U32 getAccountCount();
SA_RET_OP_VALID AccountInfo * findAccountByGUID(SA_PARAM_NN_STR const char *pGUID);
AccountInfo * findAccountByNameOrEmail(const char *pNameOrEmail);
AccountInfo * findAccountByID(U32 uID);
AccountInfo * findAccountByName(const char * pAccountName);
AccountInfo * findNextAccountByID(U32 uID);
AccountInfo * findAccountByDisplayName(const char * pDisplayName);
AccountInfo * findAccountByEmail(const char * pEmail);
Container * findAccountContainerByName(const char * pAccountOrDisplayName);
bool isUsernameUsed(const char *name);

bool confirmLoginPassword(AccountInfo *pAccount, const char *pPassword, const char *oldMd5Password, const char *pPasswordSaltedWithAccountName, U32 salt);

// Returns the corresponding XML-RPC result string for the AccountCreationResult
const char *getAccountCreationResultString(AccountCreationResult eResult);

AccountCreationResult createAutogenPWAccount(SA_PARAM_NN_STR const char *pAccountName, SA_PARAM_NN_STR const char *displayName);

bool createFullAccount(SA_PARAM_NN_STR const char *pAccountName,
					   SA_PARAM_NN_STR const char *pPasswordHash,
					   SA_PARAM_OP_STR const char *displayName,
					   SA_PARAM_NN_STR const char *email,
					   SA_PARAM_OP_STR const char *firstName,
					   SA_PARAM_OP_STR const char *lastName,
					   SA_PARAM_OP_STR const char *defaultCurrency,
					   SA_PARAM_OP_STR const char *defaultLocale,
					   SA_PARAM_OP_STR const char *defaultCountry,
					   SA_PRE_OP_OP_STR const char *const *ips,
					   bool bGenerateActivator,
					   bool bInternalLoginOnly,
					   int iAccessLevel);

void accountSetDOB(AccountInfo *account, U32 day, U32 month, U32 year);

int setInternalUse(U32 uID, bool bInternalUse);
int setLoginDisabled(U32 uID, bool bLoginDisabled);
void sendDisplayNameUpdates(const AccountInfo *pAccount);
bool changeAccountDisplayName(AccountInfo *account, const char *displayName, TransactionReturnCallback transCB, void *data);
bool changeAccountEmail(AccountInfo *account, const char *email, bool bGenerateActivator, TransactionReturnCallback transCB);
bool validateAccountEmail(AccountInfo *account, const char *validateEmailKey);
void requestPasswordChangeTransaction(AccountInfo *pAccount, const char *pHash);
void forceChangePasswordHash(AccountInfo* pAccount, const char *pHashedPassword);
int forceChangePasswordInternal(AccountInfo *account, const char *pNewPassword);
int accountChangePersonalInfo(const char *pAccountName, const char *pFirstName, const char *pLastName, const char *pEmail);
bool setShippingAddress(AccountInfo *account, 
								 const char * address1, const char * address2,
								 const char * city, const char * district,
								 const char * postalCode, const char * country,
								 const char * phone);
void changeAccountLocale(U32 uAccountID, SA_PARAM_NN_STR const char *pLocale);
void accountSetForbiddenPaymentMethods(U32 uAccountID, PaymentMethodType eTypes);
void changeAccountCurrency(U32 uAccountID, SA_PARAM_NN_STR const char *pCurrency);
void changeAccountCountry(U32 uAccountID, SA_PARAM_NN_STR const char *pCountry);
bool setSecretQuestions(SA_PARAM_OP_VALID AccountInfo *account, SA_PARAM_NN_VALID QuestionsAnswers *pQuestionsAnswers,
						SA_PARAM_OP_VALID INT_EARRAY *ppBad, bool bValidateOnly);
bool checkSecretAnswers(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_VALID STRING_EARRAY *ppAnswers, SA_PARAM_OP_VALID INT_EARRAY *piCorrect);

// Takes ownership of eaPaymentMethods and updates them on the account
void updatePaymentMethodCache(U32 uAccountID, SA_PARAM_OP_VALID EARRAY_OF(CachedPaymentMethod) eaPaymentMethods);

// Manipulate billing enabled flag on an account
void accountSetBillingEnabled(SA_PARAM_NN_VALID AccountInfo *pAccount);
void accountSetBillingDisabled(SA_PARAM_NN_VALID AccountInfo *pAccount);

void accountSetBilled(SA_PARAM_NN_VALID AccountInfo *pAccount);

bool accountLinkProfile(SA_PARAM_NN_VALID const AccountInfo *pAccount,
						SA_PARAM_NN_STR const char *pGameID,
						SA_PARAM_NN_STR const char *pProfileID,
						SA_PARAM_NN_STR const char *pPlatformID);

SA_RET_OP_VALID AccountInfo *findAccountByProfile(SA_PARAM_NN_STR const char *pGameID,
											   SA_PARAM_NN_STR const char *pProfileID,
											   SA_PARAM_NN_STR const char *pPlatformID);

SA_RET_NN_VALID AccountTicketSigned * createTicket(AccountLoginType eLoginType, SA_PARAM_NN_VALID AccountInfo *account, bool bMachineRestricted);

int parseShardPermissionString_s(AccountPermission *permission, char *buffer, size_t size);
#define parseShardPermissionString(permission, buffer) parseShardPermissionString_s(permission, buffer, ARRAY_SIZE_CHECKED(buffer))

// Product activation/removal
AUTO_STRUCT;
typedef struct ActivationInfo
{
	char *pDistributedKey; AST(ESTRING)
} ActivationInfo;

// Suppression flags for accountActivateProductCommit (to disable specific aspects of activation)
#define SUPPRESS_INTERNAL_SUB BIT(0)

AUTO_ENUM;
typedef enum ActivateProductResult
{
	APR_Success = 0,
	APR_InvalidParameter,
	APR_KeyValueLockFailure,
	APR_KeyValueCommitFailure,
	APR_KeyValueRollbackFailure,
	APR_ProductKeyDistributionLockFailure,
	APR_ProductKeyDistributionCommitFailure,
	APR_ProductKeyDistributionRollbackFailure,
	APR_InternalSubCreationFailure,
	APR_InternalSubAlreadyExistsOnAccount,
	APR_CouldNotAssociateProduct,
	APR_ProductAlreadyAssociated,
	APR_PrerequisiteFailure,
	APR_Failure,
	APR_InvalidReferrer,
} ActivateProductResult;

ActivateProductResult accountActivateProductLock(SA_PARAM_NN_VALID AccountInfo *pAccount,
												 SA_PARAM_NN_VALID const ProductContainer *pProduct,
												 SA_PARAM_OP_STR const char *pProxy,
												 SA_PARAM_OP_STR const char *pCluster,
												 SA_PARAM_OP_STR const char *pEnvironment,
											     SA_PARAM_OP_STR const char *pReferrer,
												 SA_PRE_NN_OP_VALID SA_POST_NN_NN_VALID AccountProxyProductActivation **ppOutActivation);

ActivateProductResult accountActivateProductCommit(SA_PARAM_NN_VALID AccountInfo *pAccount,
												   SA_PARAM_NN_VALID const ProductContainer *pProduct,
												   SA_PARAM_OP_STR const char *pKey,
												   U32 uSuppressFlags,
												   SA_PARAM_NN_VALID AccountProxyProductActivation *pActivation,
												   SA_PARAM_OP_VALID ActivationInfo *pActivationInfo,
												   bool bSuppressFailLog);

ActivateProductResult accountActivateProduct(SA_PARAM_NN_VALID AccountInfo *pAccount,
											 SA_PARAM_NN_VALID const ProductContainer *pProduct,
											 SA_PARAM_OP_STR const char *pProxy,
											 SA_PARAM_OP_STR const char *pCluster,
											 SA_PARAM_OP_STR const char *pEnvironment,
											 SA_PARAM_OP_STR const char *pKey,
											 SA_PARAM_OP_STR const char *pReferrer,
											 U32 uSuppressFlags,
											 SA_PARAM_OP_VALID ActivationInfo *pActivationInfo,
											 bool bSuppressFailLog);

ActivateProductResult accountActivateProductRollback(SA_PARAM_NN_VALID AccountInfo *pAccount,
													 SA_PARAM_NN_VALID const ProductContainer *pProduct,
													 SA_PARAM_NN_VALID AccountProxyProductActivation *pActivation,
													 bool bSuppressFailLog);

int accountRemoveProduct(AccountInfo *account, const ProductContainer *product, const char *reason);

bool shouldReplaceCachedSubscription(SA_PARAM_NN_VALID const CachedAccountSubscription *pSubOld,
									 SA_PARAM_NN_VALID const CachedAccountSubscription *pSubNew);

void accountUpdateCachedSubscriptions(SA_PARAM_NN_VALID AccountInfo *pAccount,
									  SA_PARAM_NN_VALID CachedAccountSubscriptionList *pNewList);

void accountMarkSubsRefunded(SA_PARAM_NN_VALID AccountInfo *pAccount);

// pSubscription will be updated with the values used if any are skipped (use existing values)
void accountUpdateCachedSubscription(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_VALID NOCONST(CachedAccountSubscription) *pSubscription);

void accountUpdateCachedCreatedTime(U32 uAccountID, SA_PARAM_NN_STR const char *pSubVID, U32 uNewCreatedTimeSS2000);
void accountStopCachedSub(U32 uAccountID, SA_PARAM_NN_STR const char *pSubVID);

int accountAddUniqueSubscription(AccountInfo *account, SubscriptionContainer *subscription, const char *key);
int accountRemoveSubscription(AccountInfo *account, SubscriptionContainer *subscription);

void accountAddPlayTime(AccountInfo *account, const char *productName, const char *shardCategory, U32 uPlayTime);
void accountResetPlayTime(U32 uAccountID);

SA_RET_OP_VALID const AccountGameMetadata *accountGetGameMetadata(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_STR const char *productName, SA_PARAM_NN_STR const char *shardCategory);
void accountSetHighestLevel(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_STR const char *productName, SA_PARAM_NN_STR const char *shardCategory, U32 uLevel);
void accountSetLastLogin(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_STR const char *productName, SA_PARAM_NN_STR const char *shardCategory);
void accountSetNumCharacters(AccountInfo *account, const char *productName, const char *shardCategory, U32 uNumCharacters, bool change);

bool isValidPaymentMethodVID(const AccountInfo *account, const char *pmVID);
SA_RET_OP_VALID const CachedPaymentMethod *getCachedPaymentMethod(SA_PARAM_NN_VALID const AccountInfo *account, SA_PARAM_NN_VALID const char *pmVID);

void accountsClearPermissionsCacheIfSubOwned(SA_PARAM_NN_STR const char *name);
void accountsClearPermissionsCacheIfProductOwned(SA_PARAM_NN_STR const char *name);
void accountClearPermissionsCache(AccountInfo *account);
void accountConstructPermissions(AccountInfo *account);

bool cachedSubEntitled(SA_PARAM_NN_VALID const CachedAccountSubscription *pCachedSub, U32 uAsOfSS2000);

void accountFlagInvalidDisplayName(AccountInfo *account, bool bFlag);

void accountRegenerateGUID(U32 uAccountID);

// Note that a subscription was refunded.
void accountRecordSubscriptionRefund(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_STR const char *pSubscriptionVid);

SA_RET_OP_VALID CachedAccountSubscription * findAccountSubscriptionByInternalName(SA_PARAM_NN_VALID const AccountInfo *account, SA_PARAM_NN_STR const char *pSubInternalName);

SA_RET_OP_VALID const CachedAccountSubscription * findAccountSubscriptionByVID(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pVID);

U32 accountAddPendingAction(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_VALID const AccountPendingAction *pPendingAction);
U32 accountAddPendingRefreshSubCache(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pSubscriptionVID);
U32 accountAddPendingFinishDelayedTrans(SA_PARAM_NN_VALID const AccountInfo *pAccount, U32 uPurchaseID);

typedef enum AccountPendingActionResult {
	APAR_SUCCESS = 0,
	APAR_NOT_FOUND,
	APAR_FAILED
} AccountPendingActionResult;

typedef struct BillingTransaction BillingTransaction;

AccountPendingActionResult accountDoPendingAction(SA_PARAM_NN_OP_VALID BillingTransaction **pTrans,
												  SA_PARAM_NN_VALID const AccountInfo *pAccount,
												  U32 uPendingActionID);

// Determine a payment method type
PaymentMethodType paymentMethodType(SA_PARAM_NN_VALID const PaymentMethod *pPaymentMethod);

AUTO_ENUM;
typedef enum PaymentMethodProblem {
	PMP_Invalid = 0,
	PMP_None,
	PMP_MissingField,
	PMP_InvalidField,
	PMP_AmbiguousType,
	PMP_Forbidden,
} PaymentMethodProblem;

PaymentMethodProblem paymentMethodValid(PaymentMethodType eForbiddenTypes,
										SA_PARAM_NN_VALID const PaymentMethod *pPaymentMethod,
										SA_PARAM_OP_VALID const CachedPaymentMethod *pCachedPaymentMethod,
										SA_PARAM_OP_STR const char *pBankName,
										SA_PARAM_OP_OP_STR const char **ppProblemDetails);

// Count of slow logins.
extern int gSlowLoginCounter;

bool accountNewRecruit(SA_PARAM_NN_VALID AccountInfo *pAccount,
					   SA_PARAM_NN_STR const char *pProductInternalName,
					   SA_PARAM_NN_STR const char *pProductKey,
					   SA_PARAM_NN_STR const char *pEmailAddress);

bool accountRecruitmentOffered(SA_PARAM_NN_VALID AccountInfo *pAccount,
							   SA_PARAM_NN_STR const char *pProductKey);

bool recruitOfferable(SA_PARAM_NN_VALID const RecruitContainer *pRecruit);

RecruitState accountRecruited(SA_PARAM_NN_VALID AccountInfo *pRecruiterAccount,
							  SA_PARAM_NN_VALID AccountInfo *pAccount,
							  SA_PARAM_NN_STR const char *pProductKey);

bool accountSetRecruitStateByAccountID(SA_PARAM_NN_VALID AccountInfo *pAccount,
									   U32 uRecruitAccountID,
									   SA_PARAM_NN_STR const char *pProductInternalName,
									   RecruitState eState);

// Mark a recruit on an account as being upgraded
bool accountUpgradeRecruit(SA_PARAM_NN_VALID AccountInfo *pAccount,
						   U32 uRecruitAccountID,
						   SA_PARAM_NN_STR const char *pProductInternalName,
						   RecruitState eState);

// -1 to remove
void accountSetSpendingCap(SA_PARAM_NN_VALID AccountInfo *pAccount,
						   SA_PARAM_NN_STR const char *pCurrency,
						   float fCap);

// Delete a user account
bool accountDelete(SA_PARAM_NN_VALID AccountInfo *pAccount);

// Set or clear flags - trans helpers
void trhAccountSetFlags(NOCONST(AccountInfo) *pAccount, U32 uFlags);
void trhAccountClearFlags(NOCONST(AccountInfo) *pAccount, U32 uFlags);
typedef enum SubState { SUBSTATE_EXTERNALACTIVEONLY, SUBSTATE_ANY } SubState;
SA_ORET_NN_NN_VALID STRING_EARRAY accountGetInternalProductList(SA_PARAM_NN_VALID const AccountInfo * pAccount, SubState eState,
	SA_PARAM_OP_VALID STRING_EARRAY * peaOutInternalSubList);

void accountSendEventEmail(SA_PARAM_NN_VALID const AccountInfo * pAccount, SA_PARAM_OP_STR const char * pLocale,
	SA_PARAM_NN_STR const char * pEmailType, SA_PARAM_NN_VALID const WebSrvKeyValueList * pKeyList,
	SA_PARAM_OP_VALID jsonrpcFinishedCB pCallback, SA_PARAM_OP_VALID void * pUserData);
