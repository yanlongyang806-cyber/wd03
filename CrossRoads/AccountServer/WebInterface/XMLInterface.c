#include "XMLInterface.h"

#include "AccountIntegration.h"
#include "AccountLog.h"
#include "AccountManagement.h"
#include "AccountReporting.h"
#include "AccountSearch.h"
#include "AccountServer.h"
#include "AccountTransactionLog.h"
#include "Account/billingAccount.h"
#include "AutoBill/ActivateSubscription.h"
#include "AutoBill/CancelSubscription.h"
#include "AutoBill/GrantExtraDays.h"
#include "AutoBill/UpdateActiveSubscriptions.h"
#include "billing.h"
#include "cmdparse.h"
#include "crypt.h"
#include "ErrorStrings.h"
#include "EString.h"
#include "GlobalComm_h_ast.h"
#include "HttpLib.h"
#include "InternalSubs.h"
#include "KeyValues/KeyValues.h"
#include "KeyValues/VirtualCurrency.h"
#include "logging.h"
#include "MachineID.h"
#include "Money.h"
#include "netipfilter.h"
#include "objContainer.h"
#include "Product.h"
#include "ProductKey.h"
#include "ProxyInterface/AccountProxy.h"
#include "PurchaseProduct.h"
#include "Refund/billingRefund.h"
#include "sock.h"
#include "Steam.h"
#include "strings_opt.h"
#include "SubCreate.h"
#include "Subscription.h"
#include "SubscriptionHistory.h"
#include "StringUtil.h"
#include "UpdatePaymentMethod.h"
#include "timing.h"
#include "Transaction/billingTransaction.h"
#include "Transaction/transactionCancel.h"
#include "Transaction/transactionQuery.h"
#include "TransactionOutcomes.h"
#include "XMLRPC.h"
#include "redact.h"

#include "XMLInterface_c_ast.h"
#include "XMLInterface_h_ast.h"
#include "PurchaseProduct_h_ast.h"
#include "AutoGen/MachineID_h_ast.h"

AUTO_STRUCT;
typedef struct XMLAccountPermissionResponse
{
	char *product;
	char *permissions;
} XMLAccountPermissionResponse;

AUTO_STRUCT;
typedef struct XMLAccountAccessResponse
{
	char *product;			// INTERNAL product name, not the unique one
	char *shardCategory;
} XMLAccountAccessResponse;

AUTO_STRUCT;
typedef struct NameIDPair
{
	char *pName;	AST(ESTRING NAME(Name))
	U32 uID;		AST(NAME(ID))
} NameIDPair;

typedef struct QuestionsAnswers QuestionsAnswers;

AUTO_STRUCT;
typedef struct XMLRPCUpdateUserRequest
{
	const char *accountName;
	const char *displayName;
	const char *email;
	char *firstName;
	char *lastName;
	const char *locale;
	const char *currency;
	const char *country;
	QuestionsAnswers *questionsAnswers;
	const char *sha256Password;
	int bIsAdmin;
	char **ips;
} XMLRPCUpdateUserRequest;

// Meant to mirror CachedAccountSubscription (except status)
AUTO_STRUCT;
typedef struct XMLCachedSub
{
	U32 uSubscriptionID;				AST(NAME(SubscriptionID))
	char * name;						AST(ESTRING)
	char * internalName;				AST(ESTRING)
	U32 startTimeSS2000;
	U32 estimatedCreationTimeSS2000;
	char * PaymentMethodVID;			AST(ESTRING)
	char * creditCardLastDigits;		AST(ESTRING)
	U32 creditCardExpirationMonth;
	U32 creditCardExpirationYear;
	char * vindiciaID;					AST(ESTRING)
	SubscriptionStatus status;
	U32 entitlementEndTimeSS2000;
	U32 nextBillingDateSS2000;
	U32 gameCard;
	U32 pendingActionID;
	bool bBilled;
	bool entitled;
} XMLCachedSub;

AUTO_STRUCT;
typedef struct XMLCachedSubList
{
	EARRAY_OF(XMLCachedSub) ppList; AST(NAME("list"))
	U32 lastUpdatedSS2000;
} XMLCachedSubList;

AUTO_STRUCT;
typedef struct XMLAccountResponse
{
	U32  id;
	char userStatus[128];
	char * loginName;
	char * displayName;
	char * email;
	char * validateEmailToken;
	char * firstName;
	char * lastName;
	char * guid;
	char ip[MAX_IP_STR];
	U32 createdTimeSS2000;
	U32 lastLoginSS2000;

	bool bInvalidDisplayName;
	bool bInternalUseLogin;
	bool bLoginDisabled;
	bool bMachineLockEnabled;
	U32 uSaveNextMachineExpire;

	XMLAccountPermissionResponse ** productPermissions; // Broken shard portion
	XMLAccountPermissionResponse ** fullProductPermissions; // Full permissions
	const AccountAddress *shippingAddress;				AST(UNOWNED)
	CachedPaymentMethod **paymentMethods;				AST(UNOWNED)
	XMLCachedSubList *subscriptions;
	AccountProxyKeyValueInfoList *keyValues;
	XMLAccountAccessResponse **access;
	EARRAY_OF(NameIDPair) ppProducts;					AST(NAME(products))
	STRING_EARRAY secretQuestions;						AST(ESTRING)
	char * defaultLocale;
	char * defaultCurrency;
	EARRAY_OF(ProductKeyInfo) productKeys;
	EARRAY_OF(ProductKeyInfo) distributedProductKeys;
	EARRAY_OF(InternalSubscription) internalSubscriptions;
	U32 numActivityLogs;
	EARRAY_OF(const AccountLogEntry) activityLog;		AST(UNOWNED NO_INDEX)
	const SubscriptionHistory * const * subHistory;		AST(UNOWNED)
	EARRAY_OF(RecruiterContainer) eaRecruiters;			AST(UNOWNED)
	EARRAY_OF(RecruitContainer) eaRecruits;				AST(UNOWNED)
	const AccountGameMetadata * const * eaGameMetadata;	AST(UNOWNED NAME(PlayTime))
	const AccountProfile * const * eaProfiles;			AST(UNOWNED)
	const AccountSpendingCap * const * eaSpendingCaps;	AST(UNOWNED)
	const SavedMachine * const * eaSavedClients; AST(UNOWNED)
	const SavedMachine * const * eaSavedBrowsers; AST(UNOWNED)

	PaymentMethodType eForbiddenPaymentMethodTypes;

	int iNumRecruitSlotsAvailable;

	SubHistoryStats * subStats;

	int flags;

	bool billed;

	// Linked Perfect World account info
	char *pPWAccountName;
	char *pPWForumName;
	char *pPWEmail;
	bool bIsPWAutoCreated;
} XMLAccountResponse;

AUTO_FIXUPFUNC;
TextParserResult fixupXMLAccountResponse(XMLAccountResponse *p, enumTextParserFixupType eFixupType, void *pExtraData)
{
	if (eFixupType == FIXUPTYPE_DESTRUCTOR)
	{
		eaDestroy(&p->activityLog);
	}
	return PARSERESULT_SUCCESS;
}

typedef struct ProductLocalizedInfo ProductLocalizedInfo;
typedef struct ProductKeyValueChangeContainer ProductKeyValueChangeContainer;
typedef struct XMLRPCPrice XMLRPCPrice;

// Used to represent products to the web
AUTO_STRUCT;
typedef struct XMLRPCProduct
{
	U32 uID;								AST(NAME(ID))
	char *pName;							AST(ESTRING)
	char *pInternalName;					AST(ESTRING)
	char *pDescription;						AST(ESTRING)
	char *pSubscriptionCategory;			AST(ESTRING)
	STRING_EARRAY ppRequiredSubscriptions;	AST(ESTRING)
	STRING_EARRAY ppShards;					AST(ESTRING)
	EARRAY_OF(AccountPermissionValue) ppPermissions;
	U32 uAccessLevel;
	STRING_EARRAY ppCategories;				AST(ESTRING)
	EARRAY_OF(XMLRPCPrice) ppPrices;
	char *pItemID;							AST(ESTRING)
	STRING_EARRAY ppPrerequisites;			AST(ESTRING)
	U32 uDaysGranted;						AST(NAME(DaysGranted))
	char *pInternalSubGranted;				AST(ESTRING)
	EARRAY_OF(NameIDPair) ppOfferedSubs;
	EARRAY_OF(ProductLocalizedInfo) ppLocalizedInfo;
	EARRAY_OF(ProductKeyValueChangeContainer) ppKeyValueChanges;
	char *pDistributionPrefix;				AST(ESTRING)
} XMLRPCProduct;

AUTO_STRUCT;
typedef struct XMLListProductsResponse
{
	XMLRPCProduct **ppList; AST(NAME("list"))
} XMLListProductsResponse;

AUTO_STRUCT;
typedef struct XMLGiveTakeProductResponse
{
	const char *result; AST(UNOWNED)
} XMLGiveTakeProductResponse;

AUTO_STRUCT;
typedef struct XMLKeyValue
{
	char *key; AST(ESTRING)
	char *value; AST(ESTRING)
} XMLKeyValue;

AUTO_STRUCT;
typedef struct XMLGetKeyValueResponse
{
	char *result; AST(UNOWNED)
	XMLKeyValue **list;
} XMLGetKeyValueResponse;

AUTO_STRUCT;
typedef struct XMLResponseSubCreate
{
	char *transID; AST(ESTRING)
} XMLResponseSubCreate;

AUTO_STRUCT;
typedef struct XMLResponseSubChange
{
	char *transID; AST(ESTRING)
} XMLResponseSubChange;

AUTO_STRUCT;
typedef struct XMLResponseSubCancel
{
	char *transID; AST(ESTRING)
} XMLResponseSubCancel;

AUTO_STRUCT;
typedef struct XMLResponseSubGrantDays
{
	char *result;	AST(UNOWNED)
	char *transID;	AST(ESTRING)
} XMLResponseSubGrantDays;

// Status of a web transaction, returned from XMLRPCTransView()
AUTO_STRUCT;
typedef struct XMLResponseTransView
{
	char *status;							AST(ESTRING)		// Current status of the transaction: SUCCESS, FAILURE, PROCESS, or UNKNOWN
	char *resultString;						AST(ESTRING)		// Result string of a completed transaction
	char *transID;							AST(ESTRING)		// Unique transaction ID

	// Optional transaction-specific information.
	EARRAY_OF(AccountTransactionInfo) TransactionInfo;			// Deferred response from XMLRPCGetTransactions()
	char *refundAmount;											// Deferred response from XMLRPCRefund()
	char *refundCurrency;										// Deferred response from XMLRPCRefund()
	EARRAY_OF(ProductKeyInfo) distributedKeys;					// Distributed keys
	char *merchantTransactionID;								// Merchant transaction ID for captures

	char *steamCountry;							AST(ESTRING)	// Steam account country
	char *steamState;							AST(ESTRING)	// Steam account state (e.g. CA)
	char *steamCurrency;						AST(ESTRING)	// Steam account currency
	char *steamStatus;							AST(ESTRING)	// Steam account status
	char *redirectURL;							AST(ESTRING)	// URL to redirect the user for completion

	PayPalStatus *payPalStatus;				AST(UNOWNED)
	U32 uPendingActionID;
	U32 uPurchaseID;						AST(NAME(PurchaseID))
} XMLResponseTransView;

// Web version of SubscriptionContainer.
AUTO_STRUCT;
typedef struct XMLRPCSubscription
{
	U32 uID;
	const char *pName;											AST(ESTRING UNOWNED)
	const char *pInternalName;									AST(ESTRING UNOWNED)
	const char *pDescription;									AST(ESTRING UNOWNED)
	const char *pBillingStatementIdentifier;					AST(ESTRING UNOWNED)
	const char *pProductName;									AST(ESTRING UNOWNED)	// Identifies which ProductStruct is
	// associated with this when it becomes an AutoBill
	const SubscriptionLocalizedInfo *const *ppLocalizedInfo;	AST(UNOWNED)			// Stores localized information for the subscription
	SubscriptionPeriodType periodType;
	int iPeriodAmount;
	int iInitialFreeDays;
	U32 gameCard;																		// If true, no credit card will be associated
	// with the resultant autobill
	EARRAY_OF(XMLRPCPrice) ppPrices;
	const char *const *ppCategories;							AST(ESTRING UNOWNED)

	U32 uFlags;
} XMLRPCSubscription;

AUTO_STRUCT;
typedef struct XMLResponseSubscriptions
{
	XMLRPCSubscription **ppList; AST(NAME("list"))
} XMLResponseSubscriptions;

/************************************************************************/
/* Debugging variables                                                  */
/************************************************************************/

// Tick count of last time an XML-RPC request was started.
static U32 debugXmlrpcLastStart = 0;

/************************************************************************/
/* Private support functions                                            */
/************************************************************************/

// The IP address of the peer for the current request.
static U32 suCurrentXmlRpcIp;

// Gets the IP String for the currently active (or last) XML-RPC call
static char *XmlInterfaceGetIPStr(void)
{
	static char peerIp[4*4];
	return GetIpStr(suCurrentXmlRpcIp, SAFESTR(peerIp));
}

// XML-RPC logging level
static int giXMLRPCLogLevel = 1;
AUTO_CMD_INT(giXMLRPCLogLevel, XMLRPCLogLevel) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

int getXMLRPCLogLevel(void)
{
	return giXMLRPCLogLevel;
}

// Creates an XMLRPCProduct from a Product
SA_RET_NN_VALID static XMLRPCProduct * XMLRPCConvertProduct(SA_PARAM_NN_VALID const ProductContainer *pProduct)
{
	XMLRPCProduct *pXMLProduct = NULL;
	EARRAY_OF(const SubscriptionContainer) eaSubs = pProduct->pSubscriptionCategory ? 
		findSubscriptionsByCategory(pProduct->pSubscriptionCategory) : NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pXMLProduct = StructCreate(parse_XMLRPCProduct);

	pXMLProduct->uID = pProduct->uID;
	pXMLProduct->uAccessLevel = pProduct->uAccessLevel;
	pXMLProduct->uDaysGranted = pProduct->uDaysGranted;
	estrCopy2(&pXMLProduct->pName, pProduct->pName);
	estrCopy2(&pXMLProduct->pInternalName, pProduct->pInternalName);
	estrCopy2(&pXMLProduct->pDescription, pProduct->pDescription);
	estrCopy2(&pXMLProduct->pSubscriptionCategory, pProduct->pSubscriptionCategory);
	estrCopy2(&pXMLProduct->pItemID, pProduct->pItemID);
	estrCopy2(&pXMLProduct->pInternalSubGranted, pProduct->pInternalSubGranted);
	estrCopy2(&pXMLProduct->pDistributionPrefix, pProduct->pActivationKeyPrefix);
	eaCopyEStrings(&pProduct->ppRequiredSubscriptions, &pXMLProduct->ppRequiredSubscriptions);
	eaCopyEStrings(&pProduct->ppShards, &pXMLProduct->ppShards);
	eaCopyEStrings(&pProduct->ppCategories, &pXMLProduct->ppCategories);
	eaCopyEStrings(&pProduct->ppPrerequisites, &pXMLProduct->ppPrerequisites);
	eaCopyStructs(&pProduct->ppPermissions, &pXMLProduct->ppPermissions, parse_AccountPermissionValue);
	EARRAY_CONST_FOREACH_BEGIN(pProduct->ppMoneyPrices, i, n);
		XMLRPCPrice *price = StructCreate(parse_XMLRPCPrice);
		estrFromMoney(&price->pPrice, moneyContainerToMoneyConst(pProduct->ppMoneyPrices[i]));
		estrFromMoneyRaw(&price->pPriceRaw, moneyContainerToMoneyConst(pProduct->ppMoneyPrices[i]));
		estrCurrency(&price->pCurrency, moneyContainerToMoneyConst(pProduct->ppMoneyPrices[i]));
		eaPush(&pXMLProduct->ppPrices, price);
	EARRAY_FOREACH_END;
	eaCopyStructs(&pProduct->ppLocalizedInfo, &pXMLProduct->ppLocalizedInfo, parse_ProductLocalizedInfo);
	eaCopyStructs(&pProduct->ppKeyValueChanges, &pXMLProduct->ppKeyValueChanges, parse_ProductKeyValueChangeContainer);

	if (eaSubs)
	{
		EARRAY_CONST_FOREACH_BEGIN(eaSubs, i, s);
			NameIDPair *pair = StructCreate(parse_NameIDPair);
			estrCopy2(&pair->pName, eaSubs[i]->pName);
			pair->uID = eaSubs[i]->uID;
			eaPush(&pXMLProduct->ppOfferedSubs, pair);
		EARRAY_FOREACH_END;

		eaDestroy(&eaSubs); // do not free contents
	}

	PERFINFO_AUTO_STOP();
	return pXMLProduct;
}

// Append products owned to user output
static void XMLRPCAddProductsOwned(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_VALID XMLAccountResponse *pXML)
{
	PERFINFO_AUTO_START_FUNC();
	EARRAY_CONST_FOREACH_BEGIN(pAccount->ppProducts, i, s);
		const ProductContainer *pProduct = findProductByName(pAccount->ppProducts[i]->name);

		if (pProduct)
		{
			NameIDPair *pPair = StructCreate(parse_NameIDPair);

			estrCopy2(&pPair->pName, pProduct->pName);
			pPair->uID = pProduct->uID;
			eaPush(&pXML->ppProducts, pPair);
		}
	EARRAY_FOREACH_END;
	PERFINFO_AUTO_STOP();
}

// MOVED FROM AccountServerHttpRequests.c
static void XMLRPCAddPermissions(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_VALID XMLAccountResponse *xmlResponse)
{
	PERFINFO_AUTO_START_FUNC();

	// Make sure the permissions are calculated for the account
	accountConstructPermissions(pAccount);

	EARRAY_CONST_FOREACH_BEGIN(pAccount->ppPermissionCache, iCurPermission, iNumPermissions);
	{
		AccountPermission *pPermission = pAccount->ppPermissionCache[iCurPermission];
		char buffer[256];

		if (parseShardPermissionString(pPermission, buffer))
		{
			XMLAccountPermissionResponse *pShardPermissions = StructCreate(parse_XMLAccountPermissionResponse);
			XMLAccountPermissionResponse *pFullPermissions = StructCreate(parse_XMLAccountPermissionResponse);

			pShardPermissions->product = strdup(pPermission->pProductName);
			pShardPermissions->permissions = strdup(buffer);

			pFullPermissions->product = strdup(pPermission->pProductName);
			pFullPermissions->permissions = strdup(pPermission->pPermissionString);

			eaPush(&xmlResponse->productPermissions, pShardPermissions);
			eaPush(&xmlResponse->fullProductPermissions, pFullPermissions);
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();
}

// Add the password secret questions to the account information.
static void XMLRPCAddSecretQuestions(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_VALID XMLAccountResponse *xmlResponse)
{
	eaCopyEStrings(&account->personalInfo.secretQuestionsAnswers.questions, &xmlResponse->secretQuestions);
}

// Add an array of product keys to the response.
static void XMLRPCAddProductKeys(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_VALID XMLAccountResponse *xmlResponse)
{
	CONST_STRING_EARRAY ppKeys = account->ppProductKeys;

	PERFINFO_AUTO_START_FUNC();
	EARRAY_CONST_FOREACH_BEGIN(ppKeys, i, size);
	{
		ProductKey key = {0};
		ProductKeyInfo *pInfo;
		bool success;
		
		success = findProductKey(&key, ppKeys[i]);

		pInfo = success ? makeProductKeyInfo(&key) : NULL;

		if (!pInfo) continue;

		eaPush(&xmlResponse->productKeys, pInfo);
	}
	EARRAY_FOREACH_END;
	PERFINFO_AUTO_STOP();
}

// Add an array of distributed keys to the response.
static void XMLRPCAddDistributedKeys(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_VALID XMLAccountResponse *xmlResponse)
{
	if (!verify(account)) return;
	if (!verify(xmlResponse)) return;

	PERFINFO_AUTO_START_FUNC();
	EARRAY_CONST_FOREACH_BEGIN(account->ppDistributedKeys, iCurDistKey, iNumDistKey);
	{
		const DistributedKeyContainer * pDistributedKey = account->ppDistributedKeys[iCurDistKey];
		ProductKey key = {0};
		ProductKeyInfo *pInfo = NULL;
		bool bKeyBelongsToRecruit = false;
		bool success;

		if (!devassert(pDistributedKey)) continue;
		if (!devassert(pDistributedKey->pActivationKey && *pDistributedKey->pActivationKey)) continue;

		// Do not return any that belong to recruits
		EARRAY_CONST_FOREACH_BEGIN(account->eaRecruits, iCurRecruit, iNumRecruits);
		{
			const RecruitContainer *pRecruit = account->eaRecruits[iCurRecruit];

			if (!devassert(pRecruit)) continue;

			if (!stricmp_safe(pDistributedKey->pActivationKey, pRecruit->pProductKey))
			{
				bKeyBelongsToRecruit = true;
				break;
			}
		}
		EARRAY_FOREACH_END;

		if (bKeyBelongsToRecruit) continue;

		success = findProductKey(&key, pDistributedKey->pActivationKey);

		if (!devassert(success)) continue;

		pInfo = makeProductKeyInfo(&key);

		if (!devassert(pInfo)) continue;

		eaPush(&xmlResponse->distributedProductKeys, pInfo);
	}
	EARRAY_FOREACH_END;
	PERFINFO_AUTO_STOP();
}

// Add internal subscriptions
static void XMLRPCAddInternalSubs(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_VALID XMLAccountResponse *xmlResponse)
{
	EARRAY_OF(const InternalSubscription) eaSubs = findInternalSubsByAccountID(account->uID);

	if (eaSubs)
	{
		eaCopyStructs(&eaSubs, &xmlResponse->internalSubscriptions, parse_InternalSubscription);
		eaDestroy(&eaSubs);
	}
}

// Add activity log
static void XMLRPCAddActivityLog(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_VALID XMLAccountResponse *xmlResponse)
{
	accountGetLogEntries(account, &xmlResponse->activityLog, 0, 0);
}

AUTO_STRUCT;
typedef struct XMLGetActivityLogRequest
{
	U32 accountID;
	int offset;
	int limit;
} XMLGetActivityLogRequest;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("GetActivityLog");
XMLAccountResponse * XMLRPCGetActivityLog(XMLGetActivityLogRequest *request)
{
	XMLAccountResponse *xmlResponse = StructCreate(parse_XMLAccountResponse);
	AccountInfo *account = findAccountByID(request->accountID);
	if (!account)
	{
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_NOT_FOUND);
	}
	else
	{
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_EXISTS);
		xmlResponse->numActivityLogs = accountGetLogEntries(account, &xmlResponse->activityLog, request->offset, request->limit);
	}
	return xmlResponse;
}

// Add sub stats
static void XMLRPCAddSubStats(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_VALID XMLAccountResponse *xmlResponse)
{
	xmlResponse->subStats = getAllSubStats(account);
}

// Add recruits
static void XMLRPCAddRecruits(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_VALID XMLAccountResponse *xmlResponse)
{
	xmlResponse->eaRecruits = (EARRAY_OF(RecruitContainer))account->eaRecruits;
}

// Add recruiters
static void XMLRPCAddRecruiters(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_VALID XMLAccountResponse *xmlResponse)
{
	xmlResponse->eaRecruiters = (EARRAY_OF(RecruiterContainer))account->eaRecruiters;
}

// Maximum number of recruit slots
static int giMaxRecruitSlots = 5;
AUTO_CMD_INT(giMaxRecruitSlots, MaxRecruitSlots) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

// Recruit invitation timeout (in days)
static int giRecruitInviteTimeout = 14;
AUTO_CMD_INT(giRecruitInviteTimeout, RecruitInviteTimeout) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

// Add recruit slots
static void XMLRPCAddRecruitSlots(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_VALID XMLAccountResponse *xmlResponse)
{
	int iSlots = giMaxRecruitSlots;
	U32 uTimeCutoffSS2000 = timeSecondsSince2000() - giRecruitInviteTimeout * SECONDS_PER_DAY;

	EARRAY_CONST_FOREACH_BEGIN(account->eaRecruits, iCurRecruit, iNumRecruits);
	{
		const RecruitContainer *pRecruit = account->eaRecruits[iCurRecruit];

		if (!devassert(pRecruit)) continue;

		if (recruitOfferable(pRecruit) && pRecruit->uCreatedTimeSS2000 > uTimeCutoffSS2000)
			iSlots--;
	}
	EARRAY_FOREACH_END;

	xmlResponse->iNumRecruitSlotsAvailable = MAX(0, iSlots);
}

static void XMLRPCAddGameMetadata(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_VALID XMLAccountResponse *xmlResponse)
{
	xmlResponse->eaGameMetadata = account->ppGameMetadata;
}

static void XMLRPCAddProfiles(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_VALID XMLAccountResponse *xmlResponse)
{
	xmlResponse->eaProfiles = account->eaProfiles;
}

XMLCachedSubList *getXMLCachedSubList(SA_PARAM_OP_VALID CachedAccountSubscriptionList *pList)
{
	XMLCachedSubList *pNewList = NULL;

	if (!pList) return NULL;

	PERFINFO_AUTO_START_FUNC();
	pNewList = StructCreate(parse_XMLCachedSubList);

	if (!devassert(pNewList))
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	pNewList->lastUpdatedSS2000 = pList->lastUpdatedSS2000;

	EARRAY_CONST_FOREACH_BEGIN(pList->ppList, iCurCachedSub, iNumCachedSubs);
	{
		CachedAccountSubscription *pCachedSub = pList->ppList[iCurCachedSub];
		XMLCachedSub *pNewSub = NULL;

		if (!devassert(pCachedSub)) continue;

		pNewSub = StructCreate(parse_XMLCachedSub);

		if (!devassert(pNewSub)) continue;

		pNewSub->uSubscriptionID = pCachedSub->uSubscriptionID;
		estrCopy2(&pNewSub->name, pCachedSub->name);
		estrCopy2(&pNewSub->internalName, pCachedSub->internalName);
		pNewSub->startTimeSS2000 = pCachedSub->startTimeSS2000;
		pNewSub->estimatedCreationTimeSS2000 = pCachedSub->estimatedCreationTimeSS2000;
		estrCopy2(&pNewSub->PaymentMethodVID, pCachedSub->PaymentMethodVID);
		estrCopy2(&pNewSub->creditCardLastDigits, pCachedSub->creditCardLastDigits);
		pNewSub->creditCardExpirationMonth = pCachedSub->creditCardExpirationMonth;
		pNewSub->creditCardExpirationYear = pCachedSub->creditCardExpirationYear;
		estrCopy2(&pNewSub->vindiciaID, pCachedSub->vindiciaID);
		pNewSub->status = getCachedSubscriptionStatus(pCachedSub);
		pNewSub->entitlementEndTimeSS2000 = pCachedSub->entitlementEndTimeSS2000;
		pNewSub->nextBillingDateSS2000 = pCachedSub->nextBillingDateSS2000;
		pNewSub->gameCard = pCachedSub->gameCard;
		pNewSub->pendingActionID = pCachedSub->pendingActionID;
		pNewSub->bBilled = pCachedSub->bBilled;
		pNewSub->entitled = cachedSubEntitled(pCachedSub, timeSecondsSince2000());

		eaPush(&pNewList->ppList, pNewSub);
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
	return pNewList;
}

// MOVED FROM AccountServerHttpRequests.c
static void XMLRPCFillFromAccount(AccountInfo *account, XMLAccountResponse *xmlResponse)
{
	PERFINFO_AUTO_START_FUNC();
	xmlResponse->id = account->uID;
	xmlResponse->guid = strdup(account->globallyUniqueID);
	xmlResponse->loginName = strdup(account->accountName);
	xmlResponse->displayName = strdup(account->displayName);
	xmlResponse->email = account->personalInfo.email ? strdup(account->personalInfo.email) : strdup("");
	xmlResponse->validateEmailToken = account->validateEmailKey ? strdup(account->validateEmailKey) : NULL;
	xmlResponse->subscriptions = getXMLCachedSubList(account->pCachedSubscriptionList);
	xmlResponse->shippingAddress = &account->personalInfo.shippingAddress;
	xmlResponse->paymentMethods = (CachedPaymentMethod **)account->personalInfo.ppPaymentMethods;
	xmlResponse->firstName = account->personalInfo.firstName ? strdup(account->personalInfo.firstName) : strdup("");
	xmlResponse->lastName = account->personalInfo.lastName ? strdup(account->personalInfo.lastName) : strdup("");
	xmlResponse->defaultLocale = account->defaultLocale ? strdup(account->defaultLocale) : strdup("");
	xmlResponse->defaultCurrency = account->defaultCurrency ? strdup(account->defaultCurrency) : strdup("");
	xmlResponse->createdTimeSS2000 = account->uCreatedTime;
	xmlResponse->lastLoginSS2000 = account->loginData.uTime;
	GetIpStr(account->loginData.uIP, SAFESTR(xmlResponse->ip));

	xmlResponse->bInvalidDisplayName = !!(account->flags & ACCOUNT_FLAG_INVALID_DISPLAYNAME);
	xmlResponse->bInternalUseLogin = account->bInternalUseLogin;
	xmlResponse->bLoginDisabled = account->bLoginDisabled;

	xmlResponse->eForbiddenPaymentMethodTypes = account->forbiddenPaymentMethodTypes;

	xmlResponse->flags = account->flags;

	xmlResponse->billed = account->bBilled;

	// Append shard access information
	accountConstructPermissions(account);
	xmlResponse->access = NULL;
	EARRAY_CONST_FOREACH_BEGIN(account->ppPermissionCache, i, s);
		if (strcmpi(account->ppPermissionCache[i]->pShardCategory, "none"))
		{
			XMLAccountAccessResponse *response = StructCreate(parse_XMLAccountAccessResponse);
			response->product = strdup(account->ppPermissionCache[i]->pProductName);
			response->shardCategory = strdup(account->ppPermissionCache[i]->pShardCategory);
			eaPush(&xmlResponse->access, response);
		}
	EARRAY_FOREACH_END;
	PERFINFO_AUTO_STOP();
}

static void XMLRPCCopyPWAccount(SA_PARAM_NN_VALID XMLAccountResponse *xmlResponse, SA_PARAM_NN_VALID PWCommonAccount *pwAccount)
{
	xmlResponse->pPWAccountName = StructAllocString(pwAccount->pAccountName);
	xmlResponse->pPWForumName = StructAllocString(pwAccount->pForumName);
	xmlResponse->pPWEmail = StructAllocString(pwAccount->pEmail);
}

// MOVED FROM AccountServerHttpRequests.c
static XMLAccountResponse * XMLRPCGetUserInfo(AccountInfo *account, int iFlags)
{
	XMLAccountResponse *xmlResponse = NULL;

	PERFINFO_AUTO_START_FUNC();
	xmlResponse = StructCreate(parse_XMLAccountResponse);

	if (account)
	{
		XMLRPCFillFromAccount(account, xmlResponse);
		if (iFlags & USERINFO_PERMISSIONS)
			XMLRPCAddPermissions(account, xmlResponse);
		if (iFlags & USERINFO_KEYVALUES)
			xmlResponse->keyValues = GetProxyKeyValueList(account);
		if (iFlags & USERINFO_PRODUCTSOWNED)
			XMLRPCAddProductsOwned(account, xmlResponse);
		if (iFlags & USERINFO_QUESTIONS)
			XMLRPCAddSecretQuestions(account, xmlResponse);
		if (iFlags & USERINFO_PRODUCTKEYS)
			XMLRPCAddProductKeys(account, xmlResponse);
		if (iFlags & USERINFO_DISTRIBUTEDPRODUCTKEYS)
			XMLRPCAddDistributedKeys(account, xmlResponse);
		if (iFlags & USERINFO_INTERNALSUBS)
			XMLRPCAddInternalSubs(account, xmlResponse);
		if (iFlags & USERINFO_ACTIVITYLOG)
			XMLRPCAddActivityLog(account, xmlResponse);
		if (iFlags & USERINFO_SUBHISTORY)
			xmlResponse->subHistory = account->ppSubscriptionHistory;
		if (iFlags & USERINFO_SUBSTATS)
			XMLRPCAddSubStats(account, xmlResponse);
		if (iFlags & USERINFO_RECRUITS)
			XMLRPCAddRecruits(account, xmlResponse);
		if (iFlags & USERINFO_RECRUITERS)
			XMLRPCAddRecruiters(account, xmlResponse);
		if (iFlags & USERINFO_RECRUITSLOTS)
			XMLRPCAddRecruitSlots(account, xmlResponse);
		if (iFlags & USERINFO_PLAYTIME)
			XMLRPCAddGameMetadata(account, xmlResponse);
		if (iFlags & USERINFO_PROFILES)
			XMLRPCAddProfiles(account, xmlResponse);
		if (iFlags & USERINFO_SPENDINGCAP)
			xmlResponse->eaSpendingCaps = account->eaSpendingCaps;
		if (iFlags & USERINFO_PERFECTWORLDACCOUNT)
		{
			PWCommonAccount *pwAccount = findPWCommonAccountByLoginField(account->pPWAccountName);
			if (pwAccount)
			{
				XMLRPCCopyPWAccount(xmlResponse, pwAccount);
				xmlResponse->bIsPWAutoCreated = account->bPWAutoCreated;
			}
		}
		if (iFlags & USERINFO_SAVEDMACHINES)
		{
			xmlResponse->bMachineLockEnabled = accountMachineLockingIsEnabled(account);
			xmlResponse->uSaveNextMachineExpire = account->uSaveNextMachineExpire;
			xmlResponse->eaSavedClients = account->eaSavedClients;
			xmlResponse->eaSavedBrowsers = account->eaSavedBrowsers;
		}

		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_EXISTS);
	}
	else
	{
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_NOT_FOUND);
	}

	PERFINFO_AUTO_STOP();
	return xmlResponse;
}

// Log XML-RPC requests. 
static void LogXmlrpcRequestVerbose(U32 uIp, const char *pXml)
{
	char ip[17];
	PERFINFO_AUTO_START_FUNC();
	suCurrentXmlRpcIp = uIp;
	if (getXMLRPCLogLevel() >= 1)
		debugXmlrpcLastStart = timeGetTime();
	if (getXMLRPCLogLevel() >= 2)
		log_printf(LOG_ACCOUNT_SERVER_XMLRPC, "Request (%s): %s", GetIpStr(uIp, ip, sizeof(ip)), billingRedact(pXml));
	PERFINFO_AUTO_STOP_FUNC();
}

// Log XML-RPC responses.
static void LogXmlrpcResponseVerbose(U32 uIp, const char *pXml)
{
	char ip[17];
	PERFINFO_AUTO_START_FUNC();
	if (getXMLRPCLogLevel() >= 1)
		servLog(LOG_ACCOUNT_SERVER_XMLRPC, "XmlCall", "time %lu", timeGetTime() - debugXmlrpcLastStart);
	if (getXMLRPCLogLevel() >= 2)
		log_printf(LOG_ACCOUNT_SERVER_XMLRPC, "Response (%s): %s", GetIpStr(uIp, ip, sizeof(ip)), billingRedact(pXml));
	PERFINFO_AUTO_STOP_FUNC();
}

// Short XML-RPC logs.
#undef LogXmlrpcf
void LogXmlrpcf(FORMAT_STR const char *pFormat, ...)
{
	va_list ap;
	char *string = NULL;
	PERFINFO_AUTO_START_FUNC();

	// Only log these for log level 1.
	if (getXMLRPCLogLevel() < 1)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Format log.
	va_start(ap, pFormat);
	estrStackCreate(&string);
	estrConcatfv(&string, pFormat, ap);
	va_end(ap);

	// Redact and submit log.
	log_printf(LOG_ACCOUNT_SERVER_XMLRPC, "%s", billingRedact(string));

	// Clean up.
	estrDestroy(&string);
	PERFINFO_AUTO_STOP_FUNC();
}

// Log XML-RPC responses.
static void LogXmlrpcResponsef(U32 uIp, const char *pXml)
{
	PERFINFO_AUTO_START_FUNC();
	if (getXMLRPCLogLevel() == 1)
		log_printf(LOG_ACCOUNT_SERVER_XMLRPC, "Response: %s", billingRedact(pXml));
	PERFINFO_AUTO_STOP_FUNC();
}

static bool ShouldDoUpdatePaymentMethod(SA_PARAM_NN_VALID const PaymentMethod *pPaymentMethod)
{
	if (pPaymentMethod->VID && *pPaymentMethod->VID) return false;

	if (paymentMethodType(pPaymentMethod) == PMT_PayPal) return false;

	return true;
}

static char * XMLRPCConvertFailureCode(LoginFailureCode failureCode)
{
	switch (failureCode)
	{
	case LoginFailureCode_NotFound:
		return ACCOUNT_HTTP_USER_NOT_FOUND;
	case LoginFailureCode_RateLimit:
		return ACCOUNT_HTTP_USER_NOT_FOUND;
	case LoginFailureCode_UnlinkedPWCommonAccount:
		return ACCOUNT_HTTP_CRYPTICUSER_NOTLINKED;
	case LoginFailureCode_DisabledLinked:
		return ACCOUNT_HTTP_USER_LOGIN_DISABLED_LINKED;
	case LoginFailureCode_Disabled:
		return ACCOUNT_HTTP_USER_LOGIN_DISABLED;
	case LoginFailureCode_Banned:
		return ACCOUNT_HTTP_USER_BANNED;
	case LoginFailureCode_NewMachineID:
		return ACCOUNT_HTTP_NEWMACHINEID;
	case LoginFailureCode_CrypticDisabled:
		return ACCOUNT_HTTP_CRYPTIC_LOGIN_DISABLED;
	case LoginFailureCode_BadPassword:
	default:
		return ACCOUNT_HTTP_USER_BAD_PASSWORD;
	}
}


/************************************************************************/
/* Initialization                                                       */
/************************************************************************/

// Initialize XML interface.
void XmlInterfaceInit()
{

	// Initialize XML-RPC logging.
	HookReceivedXmlrpcRequests(LogXmlrpcRequestVerbose);
	HookSentXmlrpcResponses(LogXmlrpcResponseVerbose);
}

/************************************************************************/
/* List subscriptions                                                   */
/************************************************************************/

// MOVED FROM billingHttpRequests.c (listSubscriptions)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ListSubscriptions");
XMLResponseSubscriptions * XMLRPCListSubscriptions(void)
{
	XMLResponseSubscriptions *xmlResponse = StructCreate(parse_XMLResponseSubscriptions);
	EARRAY_OF(SubscriptionContainer) subs = getSubscriptionList();

	LogXmlrpcf("Request: XMLRPCListSubscriptions()");
	EARRAY_CONST_FOREACH_BEGIN(subs, i, n);
		XMLRPCSubscription *sub = StructCreate(parse_XMLRPCSubscription);
		sub->uID = subs[i]->uID;
		sub->pName = subs[i]->pName;
		sub->pInternalName = subs[i]->pInternalName;
		sub->pDescription = subs[i]->pDescription;
		sub->pBillingStatementIdentifier = subs[i]->pBillingStatementIdentifier;
		sub->pProductName = subs[i]->pProductName;
		sub->ppLocalizedInfo = subs[i]->ppLocalizedInfo;
		sub->periodType = subs[i]->periodType;
		sub->iPeriodAmount = subs[i]->iPeriodAmount;
		sub->iInitialFreeDays = subs[i]->iInitialFreeDays;
		sub->gameCard = subs[i]->gameCard;
		EARRAY_CONST_FOREACH_BEGIN(subs[i]->ppMoneyPrices, j, m);
			XMLRPCPrice *price = StructCreate(parse_XMLRPCPrice);
			estrFromMoney(&price->pPrice, moneyContainerToMoneyConst(subs[i]->ppMoneyPrices[j]));
			estrFromMoneyRaw(&price->pPriceRaw, moneyContainerToMoneyConst(subs[i]->ppMoneyPrices[j]));
			estrCurrency(&price->pCurrency, moneyContainerToMoneyConst(subs[i]->ppMoneyPrices[j]));
			eaPush(&sub->ppPrices, price);
		EARRAY_FOREACH_END;
		sub->ppCategories = subs[i]->ppCategories;
		sub->uFlags = subs[i]->uFlags;
		eaPush(&xmlResponse->ppList, sub);
	EARRAY_FOREACH_END;

	eaDestroy(&subs); // DO NOT FREE CONTENTS

	LogXmlrpcf("Response: XMLRPCListSubscriptions()");
	return xmlResponse;
}

/************************************************************************/
/* View transaction status                                              */
/************************************************************************/

// MOVED FROM billingHttpRequests.c (/transView)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("TransView");
XMLResponseTransView * XMLRPCTransView(const char *webUID)
{
	XMLResponseTransView *xmlResponse = StructCreate(parse_XMLResponseTransView);

	LogXmlrpcf("Request: TransView(%s)", webUID);

	estrCopy2(&xmlResponse->status, "UNKNOWN");
	estrCopy2(&xmlResponse->transID, webUID);

	if(webUID)
	{
		BillingTransaction *pTrans = btFindByUID(webUID);
		if(pTrans)
		{
			devassert(!stricmp_safe(webUID, pTrans->webUID));

			switch(pTrans->steamTransaction ? pTrans->steamTransactionResult : pTrans->result)
			{
				xcase BTR_NONE:    estrCopy2(&xmlResponse->status, "PROCESS");
				xcase BTR_SUCCESS: estrCopy2(&xmlResponse->status, "SUCCESS");
				xcase BTR_FAILURE: estrCopy2(&xmlResponse->status, "FAILURE");
			}

			if(pTrans->result != BTR_NONE)
				estrCopy2(&xmlResponse->resultString, pTrans->resultString);

			if (pTrans->webResponseTransactionInfo)
				eaCopyStructs(&pTrans->webResponseTransactionInfo, &xmlResponse->TransactionInfo, parse_AccountTransactionInfo);
			if (pTrans->webResponseRefundAmount)
				xmlResponse->refundAmount = strdup(pTrans->webResponseRefundAmount);
			if (pTrans->webResponseRefundCurrency)
				xmlResponse->refundCurrency = strdup(pTrans->webResponseRefundCurrency);

			if (pTrans->distributedKeys)
				eaCopyStructs(&pTrans->distributedKeys, &xmlResponse->distributedKeys, parse_ProductKeyInfo);

			if (pTrans->merchantTransactionID)
				xmlResponse->merchantTransactionID = strdup(pTrans->merchantTransactionID);

			estrCopy(&xmlResponse->steamCountry, &pTrans->steamCountry);
			estrCopy(&xmlResponse->steamState, &pTrans->steamState);
			estrCopy(&xmlResponse->steamCurrency, &pTrans->steamCurrency);
			estrCopy(&xmlResponse->steamStatus, &pTrans->steamStatus);
			estrCopy(&xmlResponse->redirectURL, &pTrans->redirectURL);

			if (pTrans->payPalStatus)
				xmlResponse->payPalStatus = pTrans->payPalStatus;

			xmlResponse->uPendingActionID = pTrans->uPendingActionID;
			xmlResponse->uPurchaseID = pTrans->uPurchaseID;
		}
	}

	LogXmlrpcf("Response: TransView(): %s", NULL_TO_EMPTY(xmlResponse->status));

	return xmlResponse;
}

/************************************************************************/
/* Update a payment method                                              */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCChangePaymentMethodRequest
{
	const char *pAccountName;				AST(NAME(AccountName))
	PaymentMethod *pPaymentMethod;			AST(NAME(PaymentMethod))
	const char *pIP;						AST(NAME(IP))
	const char *pBankName;					AST(NAME(BankName))
} XMLRPCChangePaymentMethodRequest;

AUTO_STRUCT;
typedef struct XMLRPCChangePaymentMethodResponse
{
	char *result; AST(ESTRING)
	char *transID; AST(ESTRING)
} XMLRPCChangePaymentMethodResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ChangePaymentMethod");
XMLRPCChangePaymentMethodResponse * XMLRPCChangePaymentMethod(const XMLRPCChangePaymentMethodRequest *pRequest)
{
	XMLRPCChangePaymentMethodResponse *pResponse = StructCreate(parse_XMLRPCChangePaymentMethodResponse);
	AccountInfo *pAccount = findAccountByName(pRequest->pAccountName);

	LogXmlrpcf("Request: ChangePaymentMethod(%s)", pRequest->pAccountName);

	if(pAccount)
	{
		BillingTransaction *pTrans = NULL;
		const char *pErrorDetails = NULL;
		const CachedPaymentMethod *pCachedPaymentMethod = NULL;
		PaymentMethodProblem eProblem = PMP_Invalid;

		if (pRequest->pPaymentMethod->VID && *pRequest->pPaymentMethod->VID)
		{
			pCachedPaymentMethod = getCachedPaymentMethod(pAccount, pRequest->pPaymentMethod->VID);

			if (pCachedPaymentMethod && !pRequest->pPaymentMethod->country)
			{
				pRequest->pPaymentMethod->country = estrDup(pCachedPaymentMethod->billingAddress.country);
			}
		}

		if (pRequest->pPaymentMethod->active)
		{
			eProblem = paymentMethodValid(0, pRequest->pPaymentMethod, pCachedPaymentMethod, pRequest->pBankName, &pErrorDetails);
		}
		else
		{
			eProblem = PMP_None; // Don't need a valid PM to deactivate
		}

		if (eProblem == PMP_None)
		{
			pTrans = UpdatePaymentMethod(pAccount,
				pRequest->pPaymentMethod, pRequest->pIP, pRequest->pBankName, NULL, NULL, NULL);

			if (pTrans)
			{
				estrCopy2(&pResponse->transID, pTrans->webUID);
			}
		}
		else
		{
			if (pErrorDetails)
			{
				estrPrintf(&pResponse->result, "%s: %s",
					StaticDefineIntRevLookupNonNull(PaymentMethodProblemEnum, eProblem), pErrorDetails);
			}
			else
			{
				estrPrintf(&pResponse->result, "%s",
					StaticDefineIntRevLookupNonNull(PaymentMethodProblemEnum, eProblem));
			}
		}
	}
	else
	{
		pResponse->result = estrDup(ACCOUNT_HTTP_USER_NOT_FOUND);
	}

	LogXmlrpcf("Response: ChangePaymentMethod(): %s", NULL_TO_EMPTY(pResponse->transID));
	return pResponse;
}

// Deprecated
AUTO_STRUCT;
typedef struct XMLResponseUpdatePaymentMethod
{
	char *transID; AST(ESTRING)
} XMLResponseUpdatePaymentMethod;

// Deprecated
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("UpdatePaymentMethod");
XMLResponseUpdatePaymentMethod * XMLRPCUpdatePaymentMethod(const char *user, PaymentMethod *pm, const char *ip)
{
	XMLResponseUpdatePaymentMethod *xmlResponse = StructCreate(parse_XMLResponseUpdatePaymentMethod);
	XMLRPCChangePaymentMethodRequest request = {0};
	XMLRPCChangePaymentMethodResponse *pResponse = NULL;

	StructInit(parse_XMLRPCChangePaymentMethodRequest, &request);
	request.pAccountName = user;
	request.pPaymentMethod = pm;
	request.pIP = ip;
	pResponse = XMLRPCChangePaymentMethod(&request);
	request.pAccountName = NULL;
	request.pPaymentMethod = NULL;
	request.pIP = NULL;
	StructDeInit(parse_XMLRPCChangePaymentMethodRequest, &request);
	estrCopy2(&xmlResponse->transID, pResponse->transID);
	StructDestroy(parse_XMLRPCChangePaymentMethodResponse, pResponse);

	return xmlResponse;
}

/************************************************************************/
/* Grant days on a subscription                                         */
/************************************************************************/

// MOVED FROM billingHttpRequests.c (/subGrantDays)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("SubGrantDays");
XMLResponseSubGrantDays * XMLRPCSubGrantDays(const char *user, const char *VID, int iDays)
{
	XMLResponseSubGrantDays *xmlResponse = StructCreate(parse_XMLResponseSubGrantDays);
	AccountInfo *account;
	BillingTransaction *pTrans;

	LogXmlrpcf("Request: SubGrantDays(%s, %d)", user, iDays);

	// Do basic parameter validation.
	if (!VID || !*VID)
	{
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
		return xmlResponse;
	}

	// Look up account.
	account = findAccountByName(user);
	if (!account)
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
		return xmlResponse;
	}

	// Start transaction to grant extra days.
	pTrans = btGrantDays(account, VID, iDays, NULL, NULL, NULL);

	estrCopy2(&xmlResponse->transID, pTrans->webUID);

	LogXmlrpcf("Response: SubGrantDays(): %s", NULL_TO_EMPTY(xmlResponse->transID));

	return xmlResponse;
}

/************************************************************************/
/* Cancel a subscription                                                */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLSubCancelParameters
{
	const char *user;
	const char *VID;
	bool instant;
	bool merchantInitiated;
} XMLSubCancelParameters;

// MOVED FROM billingHttpRequests.c (/subCancel)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("SubCancel");
XMLResponseSubCancel * XMLRPCSubCancel(XMLSubCancelParameters *pCancel)
{
	XMLResponseSubCancel *xmlResponse = StructCreate(parse_XMLResponseSubCancel);

	// TODO JDRAGO Validate input data, in case the web interface didn't

	AccountInfo *account   = findAccountByName(pCancel->user);
	LogXmlrpcf("Request: SubCancel(%s)", pCancel->user);
	if(account)
	{
		BillingTransaction *pTrans = btCancelSubscription(account->uID, pCancel->VID, pCancel->instant, pCancel->merchantInitiated);
		estrCopy2(&xmlResponse->transID, pTrans->webUID);
	}

	LogXmlrpcf("Response: SubCancel(): %s", NULL_TO_EMPTY(xmlResponse->transID));

	return xmlResponse;
}


/************************************************************************/
/* Set or change a key value                                            */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLSetKeyValueResponse
{
	char *result; AST(UNOWNED)
} XMLSetKeyValueResponse;

AUTO_STRUCT;
typedef struct XMLSetKeyValueExRequest
{
	const char *pAccountName;	AST(NAME(AccountName))
	const char *pKey;			AST(NAME(Key))
	const char *pValue;			AST(NAME(Value))
	bool bIncrement;			AST(NAME(Increment))
	const char *pReason;		AST(NAME(Reason))
	TransactionLogType eType;	AST(NAME(Type))
} XMLSetKeyValueExRequest;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("SetKeyValueEx");
SA_RET_NN_VALID XMLSetKeyValueResponse * XMLRPCSetKeyValueEx(SA_PARAM_NN_VALID const XMLSetKeyValueExRequest *pRequest)
{
	AccountInfo *pAccount = NULL;
	XMLSetKeyValueResponse *xmlResponse = StructCreate(parse_XMLSetKeyValueResponse);

	LogXmlrpcf("Request: SetKeyValueEx(%s, %s, %s, %d)", pRequest->pAccountName, pRequest->pKey, pRequest->pValue, pRequest->bIncrement);

	if (pRequest->pAccountName)
	{
		pAccount = findAccountByName(pRequest->pAccountName);
		if (!pAccount) pAccount = findAccountByDisplayName(pRequest->pAccountName);
		if (!pAccount) pAccount = findAccountByEmail(pRequest->pAccountName);
	}

	if (!pAccount)
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	}
	else
	{
		AccountKeyValueResult result = AKV_FAILURE;
		char *lock = NULL;
		TransactionLogKeyValueChange **eaChanges = NULL;

		if (pRequest->bIncrement)
		{
			result = AccountKeyValue_Change(pAccount, pRequest->pKey, pRequest->pValue ? atoi64(pRequest->pValue) : 0, &lock);
		}
		else
		{
			result = AccountKeyValue_Set(pAccount, pRequest->pKey, pRequest->pValue ? atoi64(pRequest->pValue) : 0, &lock);
		}

		if (result == AKV_SUCCESS)
		{
			char **eaKeys = NULL;

			CurrencyPopulateKeyList(pRequest->pKey, &eaKeys);
			AccountTransactionGetKeyValueChanges(pAccount, eaKeys, &eaChanges);
			eaDestroyEx(&eaKeys, NULL);

			if (pRequest->pReason && *pRequest->pReason)
			{
				accountLog(pAccount, "Key value change reason: %s", pRequest->pReason);
			}

			result = AccountKeyValue_Commit(pAccount, pRequest->pKey, lock);
		}

		estrDestroy(&lock);

		switch (result)
		{
		default:
		case AKV_FAILURE:
			xmlResponse->result = ACCOUNT_HTTP_KEY_FAILURE;
			break;
		case AKV_INVALID_KEY:
			xmlResponse->result = ACCOUNT_HTTP_INVALID_KEY;
			break;
		case AKV_INVALID_RANGE:
			xmlResponse->result = ACCOUNT_HTTP_INVALID_RANGE;
			break;
		case AKV_SUCCESS:
			// Record the change in the transaction log, if it's relevant
			if (eaSize(&eaChanges) > 0)
			{
				U32 uLogID = AccountTransactionOpen(pAccount->uID, pRequest->eType, NULL, TPROVIDER_AccountServer, NULL, NULL);
				AccountTransactionRecordKeyValueChanges(pAccount->uID, uLogID, eaChanges, pRequest->pReason);
				AccountTransactionFinish(pAccount->uID, uLogID);
			}

			xmlResponse->result = ACCOUNT_HTTP_KEY_SUCCESS;
			break;
		}

		AccountTransactionFreeKeyValueChanges(&eaChanges);
	}

	LogXmlrpcf("Response: SetKeyValueEx(): %s", NULL_TO_EMPTY(xmlResponse->result));

	return xmlResponse;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("SetOrChangeKeyValue");
SA_RET_NN_VALID XMLSetKeyValueResponse * XMLRPCSetOrChangeKeyValue(const char *accountName, const char *key, const char *value, int change)
{
	XMLSetKeyValueExRequest request = {0};
	request.bIncrement = !!change;
	request.pAccountName = accountName;
	request.pKey = key;
	request.pValue = value;
	return XMLRPCSetKeyValueEx(&request);
}


/************************************************************************/
/* Change a key value                                                   */
/************************************************************************/

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ChangeKeyValue");
XMLSetKeyValueResponse * XMLRPCChangeKeyValue(const char *accountName, const char *key, const char *value)
{
	LogXmlrpcf("Forward: ChangeKeyValue()");
	return XMLRPCSetOrChangeKeyValue(accountName, key, value, true);
}


/************************************************************************/
/* Set a key value                                                      */
/************************************************************************/

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("SetKeyValue");
XMLSetKeyValueResponse * XMLRPCSetKeyValue(const char *accountName, const char *key, const char *value)
{
	LogXmlrpcf("Forward: SetKeyValue()");
	return XMLRPCSetOrChangeKeyValue(accountName, key, value, false);
}


/************************************************************************/
/* Get key value list                                                   */
/************************************************************************/

// MOVED FROM AccountServerHttpRequests.c (/keyvalue_list)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("GetKeyValueList");
XMLGetKeyValueResponse * XMLRPCGetKeyValueList(const char *accountName)
{
	AccountInfo *account = NULL;
	XMLGetKeyValueResponse *xmlResponse = StructCreate(parse_XMLGetKeyValueResponse);

	LogXmlrpcf("Request: GetKeyValueList(%s)", accountName);

	account = accountName ? findAccountByName(accountName) : NULL;
	account = account ? account : findAccountByEmail(accountName);

	if (!account)
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	}
	else
	{
		STRING_EARRAY eaKeys = AccountKeyValue_GetAccountKeyList(account);

		xmlResponse->result = ACCOUNT_HTTP_USER_EXISTS;
		EARRAY_CONST_FOREACH_BEGIN(eaKeys, iCurKey, iNumKeys);
		{
			const char *key = eaKeys[iCurKey];
			S64 iValue = 0;

			if (AccountKeyValue_Get(account, key, &iValue) == AKV_SUCCESS)
			{
				XMLKeyValue *val = StructCreate(parse_XMLKeyValue);
				estrCopy2(&val->key, key);
				estrPrintf(&val->value, "%"FORM_LL"d", iValue);
				eaPush(&xmlResponse->list, val);
			}
		}
		EARRAY_FOREACH_END;

		AccountKeyValue_DestroyAccountKeyList(&eaKeys);
	}

	LogXmlrpcf("Response: GetKeyValueList()");

	return xmlResponse;
}

/************************************************************************/
/* Take product                                                         */
/************************************************************************/

// MOVED FROM AccountServerHttpRequests.c (/take_product)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("TakeProduct");
XMLGiveTakeProductResponse * XMLRPCTakeProduct(const char *accountName, const char *productName)
{
	XMLGiveTakeProductResponse *xmlResponse = StructCreate(parse_XMLGiveTakeProductResponse);
	AccountInfo *account = NULL;
	const ProductContainer *product = NULL;

	LogXmlrpcf("Request: TakeProduct(%s, %s)", accountName, productName);

	if(!accountName || !productName)
	{
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
	}

	if(!xmlResponse->result)
	{
		account = findAccountByName(accountName);
		if (!account)
		{
			account = findAccountByEmail(accountName);
		}

		if(!account)
		{
			xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
		}
	}

	if(!xmlResponse->result)
	{
		product = findProductByName(productName);

		if(!product)
		{
			xmlResponse->result = ACCOUNT_HTTP_PRODUCT_NOT_FOUND;
		}
	}

	if(!xmlResponse->result)
	{
		// Take the product
		int ret = accountRemoveProduct(account, product, "removed via XML-RPC TakeProduct");
		accountClearPermissionsCache(account);
		if(ret == 1)
		{
			xmlResponse->result = ACCOUNT_HTTP_PRODUCT_NOT_OWNED;
		}
		else
		{
			xmlResponse->result = ACCOUNT_HTTP_PRODUCT_TAKEN;
		}
	}

	LogXmlrpcf("Response: TakeProduct(): %s", NULL_TO_EMPTY(xmlResponse->result));

	return xmlResponse;
}

/************************************************************************/
/* Activate a product                                                   */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLActivateProductRequest
{
	char *pAccountName; AST(NAME(AccountName))
	char *pProductName; AST(NAME(ProductName))
	char *pProductKey;  AST(NAME(ProductKey))
	char *pRecruitEmailAddress; AST(NAME(RecruitEmailAddress))
	char *pReferrer; AST(NAME(Referrer))
} XMLActivateProductRequest;

AUTO_STRUCT;
typedef struct XMLActivateProductResponse
{
	const char *pResult;			AST(UNOWNED NAME(result))
	const char *pDistributedKey;	AST(ESTRING NAME(distributedKey))
} XMLActivateProductResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ActivateProduct");
XMLActivateProductResponse * XMLRPCActivateProduct(const XMLActivateProductRequest *pRequest)
{
	XMLActivateProductResponse *xmlResponse = StructCreate(parse_XMLActivateProductResponse);
	AccountInfo *pAccount = NULL;
	const ProductContainer *pProduct = NULL;
	ActivateProductResult eActivationResult = APR_Failure;
	AccountProxyProductActivation *pActivation = NULL;
	ActivationInfo activationInfo = {0};
	const char *pAccountName;
	const char *pProductName;
	const char *pProductKey;
	const char *pReferrer;

	if (!pRequest)
	{
		xmlResponse->pResult = ACCOUNT_HTTP_NO_ARGS;
		return xmlResponse;
	}

	PERFINFO_AUTO_START_FUNC();

	pAccountName = pRequest->pAccountName;
	pProductName = pRequest->pProductName;
	pProductKey = pRequest->pProductKey;
	pReferrer = pRequest->pReferrer;

	LogXmlrpcf("Request: ActivateProduct(%s, %s, %s)",
		NULL_TO_EMPTY(pAccountName), NULL_TO_EMPTY(pProductName), NULL_TO_EMPTY(pProductKey));

	if(!pAccountName || !pProductName)
	{
		xmlResponse->pResult = ACCOUNT_HTTP_NO_ARGS;
		goto out;
	}

	// Find the account
	pAccount = findAccountByName(pAccountName);
	if(!pAccount)
	{
		xmlResponse->pResult = ACCOUNT_HTTP_USER_NOT_FOUND;
		goto out;
	}

	// Find the product
	pProduct = findProductByName(pProductName);
	if(!pProduct)
	{
		xmlResponse->pResult = ACCOUNT_HTTP_PRODUCT_NOT_FOUND;
		goto out;
	}

	// Do not let the account server product to be given this way
	if(!stricmp_safe(pProduct->pInternalName, ACCOUNT_SERVER_INTERNAL_NAME))
	{
		xmlResponse->pResult = ACCOUNT_HTTP_NOT_AUTHORIZED;
		goto out;
	}

	// If we are recruiting...
	if (pRequest->pRecruitEmailAddress && *pRequest->pRecruitEmailAddress)
	{
		// Make sure that, if we have a recruit e-mail address, that we are also going to distribute a key
		if (!pProduct->pActivationKeyPrefix || !*pProduct->pActivationKeyPrefix)
		{
			xmlResponse->pResult = "Cannot recruit with a product that does not distribute a key.";
			goto out;
		}
	}

	// Lock the product
	eActivationResult = accountActivateProductLock((AccountInfo*) pAccount, pProduct, NULL, NULL, NULL, pReferrer, &pActivation);
	if (eActivationResult != APR_Success)
	{
		xmlResponse->pResult = StaticDefineIntRevLookupNonNull(ActivateProductResultEnum, eActivationResult);
		goto out;
	}

	// Commit the product
	eActivationResult = accountActivateProductCommit(pAccount, pProduct, pProductKey, 0, pActivation, &activationInfo, false);
	if(eActivationResult != APR_Success)
	{
		xmlResponse->pResult = StaticDefineIntRevLookupNonNull(ActivateProductResultEnum, eActivationResult);
		goto out;
	}

	// If we were provided a recruit e-mail address, link the accounts
	if (pRequest->pRecruitEmailAddress && *pRequest->pRecruitEmailAddress)
	{
		const ProductKeyBatch *pBatch = NULL;
		const ProductKeyGroup *pGroup = NULL;
		ProductKey key = {0};
		bool success;

		if (!devassert(activationInfo.pDistributedKey && *activationInfo.pDistributedKey))
		{
			xmlResponse->pResult = ACCOUNT_HTTP_INTERNAL_ERROR;
			goto out;
		}

		success = findProductKey(&key, activationInfo.pDistributedKey);

		if (!devassert(success))
		{
			xmlResponse->pResult = ACCOUNT_HTTP_INTERNAL_ERROR;
			goto out;
		}

		pBatch = getKeyBatchByID(key.uBatchId);

		if (!devassert(pBatch))
		{
			xmlResponse->pResult = ACCOUNT_HTTP_INTERNAL_ERROR;
			goto out;
		}

		pGroup = keyGroupFromBatch(pBatch);

		EARRAY_CONST_FOREACH_BEGIN(pGroup->ppProducts, iCurProduct, iNumProducts);
		{
			bool bRecruited = false;
			const char *pDistProductName = pGroup->ppProducts[iCurProduct];
			const ProductContainer *pDistProduct = NULL;

			if (!devassert(pDistProductName && *pDistProductName))
			{
				xmlResponse->pResult = ACCOUNT_HTTP_INTERNAL_ERROR;
				goto out;
			}

			pDistProduct = findProductByName(pDistProductName);

			if (!devassert(pDistProduct))
			{
				xmlResponse->pResult = ACCOUNT_HTTP_INTERNAL_ERROR;
				goto out;
			}

			bRecruited = accountNewRecruit(pAccount, pDistProduct->pInternalName, activationInfo.pDistributedKey, pRequest->pRecruitEmailAddress);

			if (!devassert(bRecruited))
			{
				xmlResponse->pResult = ACCOUNT_HTTP_INTERNAL_ERROR;
				goto out;
			}
		}
		EARRAY_FOREACH_END;
	}

	// If a key was distributed, put it in the response (and take ownership)
	xmlResponse->pDistributedKey = activationInfo.pDistributedKey;
	activationInfo.pDistributedKey = NULL;

	// If we made it this far, we win!
	xmlResponse->pResult = ACCOUNT_HTTP_PRODUCT_GIVEN;

out:

	// Clean up the activation info structure
	StructDeInit(parse_ActivationInfo, &activationInfo);

	// We should definitely have a result by now
	if (!devassert(xmlResponse->pResult && *xmlResponse->pResult))
	{
		xmlResponse->pResult = "Failed";
	}

	LogXmlrpcf("Response: ActivateProduct(): %s", NULL_TO_EMPTY(xmlResponse->pResult));

	PERFINFO_AUTO_STOP_FUNC();

	return xmlResponse;
}

// An alias for ActivateProduct
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("GiveProduct");
XMLActivateProductResponse * XMLRPCGiveProduct(const char *pAccountName, const char *pProductName, const char *pProductKey)
{
	XMLActivateProductRequest *pRequest = StructCreate(parse_XMLActivateProductRequest);
	XMLActivateProductResponse *pResponse = NULL;

	if (!devassert(pRequest)) return NULL;

	pRequest->pAccountName = (char *)pAccountName;
	pRequest->pProductName = (char *)pProductName;
	pRequest->pProductKey = (char *)pProductKey;

	pResponse = XMLRPCActivateProduct(pRequest);

	pRequest->pAccountName = NULL;
	pRequest->pProductName = NULL;
	pRequest->pProductKey = NULL;

	StructDestroy(parse_XMLActivateProductRequest, pRequest);

	if (!devassert(pResponse)) return NULL;

	return pResponse;
}

/************************************************************************/
/* Global account reporting statistics                                  */
/************************************************************************/

// Statistics response.
AUTO_STRUCT;
typedef struct XMLAccountServerStatsResponse
{
	char *result;						AST(UNOWNED)
	AccountStats *stats;									// Created by GetAccountStats()
} XMLAccountServerStatsResponse;

// Return general account global statistics
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("Stats");
XMLAccountServerStatsResponse * XMLRPCStats(void)
{
	XMLAccountServerStatsResponse *xmlResponse = StructCreate(parse_XMLAccountServerStatsResponse);
	LogXmlrpcf("Request: Stats()");
	xmlResponse->result = ACCOUNT_HTTP_SUCCESS;
	xmlResponse->stats = GetAccountStats();
	LogXmlrpcf("Response: Stats()");
	return xmlResponse;
}

/************************************************************************/
/* List products                                                        */
/************************************************************************/

// MOVED FROM AccountServerHttpRequests.c (/list_products)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ListProducts");
XMLListProductsResponse * XMLRPCListProducts(void)
{
	XMLListProductsResponse *xmlResponse = StructCreate(parse_XMLListProductsResponse);
	EARRAY_OF(ProductContainer) eaProducts = getProductList(PRODUCTS_ALL);

	LogXmlrpcf("Request: ListProducts()");

	EARRAY_CONST_FOREACH_BEGIN(eaProducts, i, s);
		eaPush(&xmlResponse->ppList, XMLRPCConvertProduct(eaProducts[i]));
	EARRAY_FOREACH_END;

	eaDestroy(&eaProducts); // Do not free contents
	LogXmlrpcf("Response: ListProducts()");
	return xmlResponse;
}

/************************************************************************/
/* Next user info                                                       */
/************************************************************************/

// MOVED FROM AccountServerHttpRequests.c (/next_user_info)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("NextUserInfo");
XMLAccountResponse * XMLRPCNextUserInfo(U32 uID, int iFlags)
{
	AccountInfo *account = findNextAccountByID(uID);
	LogXmlrpcf("Request: NextUserInfo(%lu)", uID);
	LogXmlrpcf("Response: NextUserInfo()");
	return XMLRPCGetUserInfo(account, iFlags);
}

/************************************************************************/
/* User info                                                            */
/************************************************************************/

// MOVED FROM AccountServerHttpRequests.c (/user_info)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("UserInfo");
XMLAccountResponse * XMLRPCUserInfo(const char *accountName, int iFlags)
{
	AccountInfo *account = NULL;

	LogXmlrpcf("Request: UserInfo(%s)", accountName);

	account = findAccountByName(accountName);
	if (!account)
		account = findAccountByDisplayName(accountName);

	LogXmlrpcf("Response: UserInfo()");

	return XMLRPCGetUserInfo(account, iFlags);
}

AUTO_STRUCT;
typedef struct XMLGetUserInfoRequest
{
	char *accountName;
	char *email;
	int iFlags;
	AccountLoginType eType;
} XMLGetUserInfoRequest;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("UserInfoEx");
XMLAccountResponse * XMLRPCUserInfoEx(XMLGetUserInfoRequest *request)
{
	LogXmlrpcf("Request: UserInfoEx(%s, %d)", request->accountName ? request->accountName : request->email, request->eType);

	if (!(request->accountName && request->accountName[0]) && !(request->email && request->email[0]))
	{
		XMLAccountResponse *response = StructCreate(parse_XMLAccountResponse);
		strcpy(response->userStatus, ACCOUNT_HTTP_NOT_FOUND);
		LogXmlrpcf("Response: UserInfoEx(): %s", response->userStatus);
		return response;
	}

	if (request->eType < ACCOUNTLOGINTYPE_Default || request->eType > ACCOUNTLOGINTYPE_PerfectWorld)
	{
		XMLAccountResponse *response = StructCreate(parse_XMLAccountResponse);
		strcpy(response->userStatus, ACCOUNT_HTTP_INVALID_ACCOUNT_TYPE);
		LogXmlrpcf("Response: UserInfoEx(): %s", response->userStatus);
		return response;
	}

	if (request->eType == ACCOUNTLOGINTYPE_PerfectWorld)
	{
		PWCommonAccount *pPWAccount = NULL;
		AccountInfo *account = NULL;

		if (request->accountName)
		{
			pPWAccount = findPWCommonAccountByLoginField(request->accountName);

			if (!pPWAccount)
				pPWAccount = findPWCommonAccountbyForumName(request->accountName);
		}
		else
			pPWAccount = findPWCommonAccountByEmail(request->email);

		if (pPWAccount)
		{
			if (pPWAccount->uLinkedID)
				account = findAccountByID(pPWAccount->uLinkedID);

			if (!account)
			{
				XMLAccountResponse *response = StructCreate(parse_XMLAccountResponse);
				strcpy(response->userStatus, ACCOUNT_HTTP_PWUSER_UNKNOWN);
				LogXmlrpcf("Response: UserInfoEx(): %s", response->userStatus);
				return response;
			}
		}

		LogXmlrpcf("Response: UserInfoEx()");
		return XMLRPCGetUserInfo(account, request->iFlags);
	}
	else // eType == ACCOUNTLOGINTYPE_Default || eType == ACCOUNTLOGINTYPE_Cryptic
	{
		if (request->accountName)
		{
			LogXmlrpcf("Response: UserInfoEx()");
			return XMLRPCUserInfo(request->accountName, request->iFlags);
		}
		else
		{
			AccountInfo *pAccount = findAccountByEmail(request->email);
			
			if (!pAccount)
			{
				XMLAccountResponse *response = StructCreate(parse_XMLAccountResponse);
				strcpy(response->userStatus, ACCOUNT_HTTP_USER_NOT_FOUND);
				LogXmlrpcf("Response: UserInfoEx(): %s", response->userStatus);
				return response;
			}

			LogXmlrpcf("Response: UserInfoEx()");
			return XMLRPCGetUserInfo(pAccount, request->iFlags);
		}
	}
}

/************************************************************************/
/* Validate ticket ID                                                   */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCValidateTicketIDRequest
{
	U32 uAccountID;
	char *pAccountName;
	U32 uTicketID;
	char *pMachineID;
	int iFlags;
} XMLRPCValidateTicketIDRequest;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ValidateTicketIDEx");
XMLAccountResponse * XMLRPCValidateTicketIDEx(XMLRPCValidateTicketIDRequest *pRequest)
{
	XMLAccountResponse *xmlResponse = StructCreate(parse_XMLAccountResponse);
	AccountInfo *account = NULL;
	
	LogXmlrpcf("Request: ValidateTicketIDEx(%lu, %lu, %s, %d)", pRequest->uTicketID, pRequest->uAccountID, 
		pRequest->pAccountName ? pRequest->pAccountName : "[NULL]", pRequest->iFlags);

	if (pRequest->uAccountID)
		account = findAccountByID(pRequest->uAccountID);
	else if (pRequest->pAccountName)
		account = findAccountByName(pRequest->pAccountName);

	if (account)
	{
		AccountTicketCache *ticket = AccountFindTicketByID(account, pRequest->uTicketID);
		U32 uCurTime = timeSecondsSince2000();
		if (!ticket)
		{
			strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_TICKET_NOT_FOUND);
		}
		else if (ticket->uExpireTime < uCurTime)
		{
			strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_TICKET_EXPIRED);
		}
		else
		{
			StructDestroy(parse_XMLAccountResponse, xmlResponse);
			xmlResponse = XMLRPCGetUserInfo(account, pRequest->iFlags);
			if (ticket->bMachineRestricted)
			{
				if (accountMachineSaveNextClient(account, uCurTime))
					strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_SAVENEXTCLIENT);
				else
					strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_NEWMACHINEID);
			}
			else
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_TICKET_OK);
		}
		if (ticket)
			StructDestroy(parse_AccountTicketCache, ticket); // destroy used ticket
	}
	else
	{
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_NOT_FOUND);
	}

	LogXmlrpcf("Response: ValidateTicketIDEx(%s)", NULL_TO_EMPTY(xmlResponse->userStatus));
	return xmlResponse;
}

// Deprecated, use ValidateTicketIDEx
// MOVED FROM AccountServerHttpRequests.c (/verify_ticketid)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ValidateTicketID");
XMLAccountResponse * XMLRPCValidateTicketID(U32 uTicketID, U32 uAccountID, int iFlags)
{
	XMLRPCValidateTicketIDRequest request = {0};
	request.uTicketID = uTicketID;
	request.uAccountID = uAccountID;
	request.iFlags = iFlags;
	return XMLRPCValidateTicketIDEx(&request);
}

/************************************************************************/
/* Validate ticket                                                      */
/************************************************************************/

// Deprecated
// MOVED FROM AccountServerHttpRequests.c (/verify_ticket)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ValidateTicket");
XMLAccountResponse * XMLRPCValidateTicket(const char *ticket)
{
	XMLAccountResponse *xmlResponse = StructCreate(parse_XMLAccountResponse);
	AccountTicketSigned *pTicketSigned = StructCreate(parse_AccountTicketSigned);
	LogXmlrpcf("Request: ValidateTicket(%s)", ticket);

	if (ticket && ParserReadText(ticket, parse_AccountTicketSigned, pTicketSigned, 0))
	{
		AccountTicket *pTicket = StructCreate(parse_AccountTicket);

		if (ParserReadTextSafe(pTicketSigned->ticketText, pTicketSigned->strTicketTPI, pTicketSigned->uTicketCRC, 
			parse_AccountTicket, pTicket, 0))
		{
			if (pTicket->uExpirationTime < timeSecondsSince2000())
			{
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_TICKET_EXPIRED);
			}
			else
			{
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_TICKET_OK);
			}
		}
		else
		{
			strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_TICKET_BADSIGNATURE);
		}
		StructDestroy(parse_AccountTicket, pTicket);
	}
	else
	{
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_TICKET_PARSEERR);
	}

	StructDestroy(parse_AccountTicketSigned, pTicketSigned);
	LogXmlrpcf("Response: ValidateTicket()");
	return xmlResponse;
}

/*************************************************************************/
/* Validate special conflict tickets                                     */
/*************************************************************************/

// Parameters for XMLRPCValidateConflictTicket()
AUTO_STRUCT;
typedef struct XMLRPCValidateConflictTicketRequest
{
	const char *pPWAccountName;									// Private account name
	U32 uTicket;						AST(NAME(Ticket))		// Ticket
} XMLRPCValidateConflictTicketRequest;

// Authenticate a login attempt.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ValidateConflictTicket");
XMLAccountResponse * XMLRPCValidateConflictTicket(SA_PARAM_NN_VALID XMLRPCValidateConflictTicketRequest *pRequest)
{
	XMLAccountResponse *xmlResponse = StructCreate(parse_XMLAccountResponse);
	PWCommonAccount *account;
	bool success = false;

	LogXmlrpcf("Request: ValidateConflictTicket(%s, %u)", pRequest->pPWAccountName, pRequest->uTicket);

	// Look up ticket.
	account = findPWCommonAccountByLoginField(pRequest->pPWAccountName);
	if (account)
		success = findConflictTicketByID(account, pRequest->uTicket);

	// Send appropriate response.
	if (success)
	{
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_CONFLICT_TICKET_OK);
		XMLRPCCopyPWAccount(xmlResponse, account);
	}
	else
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_CONFLICT_TICKET_NOT_FOUND);


	LogXmlrpcf("Response: ValidateConflictTicket(): %s", NULL_TO_EMPTY(xmlResponse->userStatus));

	return xmlResponse;
}

// Parameters for XMLRPCValidateCrypticConflictTicketRequest()
AUTO_STRUCT;
typedef struct XMLRPCValidateCrypticConflictTicketRequest
{
	const char *pAccountName;									// Private Cryptic account name
	U32 uTicket;						AST(NAME(Ticket))		// Ticket
	int iFlags;													// UserInfo flags
} XMLRPCValidateCrypticConflictTicketRequest;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ValidateCrypticConflictTicket");
XMLAccountResponse * XMLRPCValidateCrypticConflictTicket(SA_PARAM_NN_VALID XMLRPCValidateCrypticConflictTicketRequest *pRequest)
{
	XMLAccountResponse *xmlResponse;
	AccountInfo *account;
	bool success = false;
	LogXmlrpcf("Request: ValidateCrypticConflictTicket(%s, %u)", pRequest->pAccountName, pRequest->uTicket);

	// Look up ticket.
	account = findAccountByName(pRequest->pAccountName);
	if (account)
	{
		success = findCrypticConflictTicketByID(account, pRequest->uTicket);
		if (success)
		{
			xmlResponse = XMLRPCGetUserInfo(account, pRequest->iFlags);
			strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_CONFLICT_TICKET_OK);
		}
		else
		{
			xmlResponse = StructCreate(parse_XMLAccountResponse);
			strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_CONFLICT_TICKET_NOT_FOUND);
		}
	}
	else
	{
		xmlResponse = StructCreate(parse_XMLAccountResponse);
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_NOT_FOUND);
	}


	LogXmlrpcf("Response: ValidateCrypticConflictTicket(): %s", NULL_TO_EMPTY(xmlResponse->userStatus));

	return xmlResponse;
}


/************************************************************************/
/* Check if it is a valid product key                                   */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLTestKeyResponse
{
	char *userStatus; AST(ESTRING)
	char *accountNameUsedBy;
	XMLRPCProduct **products;
} XMLTestKeyResponse;

// MOVED FROM AccountServerHttpRequests.c (/test_key)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("IsValidProductKey");
XMLTestKeyResponse * XMLRPCIsValidProductKey(const char *productKey)
{
	ProductKey keyStruct;
	XMLTestKeyResponse *xmlResponse = StructCreate(parse_XMLTestKeyResponse);

	if (productKey && *productKey && productKeyIsValid(&keyStruct, productKey)) 
	{
		EARRAY_OF(ProductContainer) eaProducts = NULL;
		char *key = NULL;
		U32 uAccountID;

		estrStackCreate(&key);
		copyProductKeyName(&key, &keyStruct);
		findProductsFromKey(key, &eaProducts);
		estrDestroy(&key);
		EARRAY_CONST_FOREACH_BEGIN(eaProducts, i, s);
			XMLRPCProduct *pXMLProduct = NULL;
			
			if (eaProducts[i])
				pXMLProduct = XMLRPCConvertProduct(eaProducts[i]);

			if (pXMLProduct)
				eaPush(&xmlResponse->products, pXMLProduct);
		EARRAY_FOREACH_END;
		eaDestroyStruct(&eaProducts, parse_ProductContainer);

		if (uAccountID = getActivatedAccountId(&keyStruct))
		{
			const AccountInfo *pOwnerAccount = findAccountByID(uAccountID);

			if (devassert(pOwnerAccount))
			{
				xmlResponse->accountNameUsedBy = strdup(pOwnerAccount->accountName);
			}

			estrCopy2(&xmlResponse->userStatus, PRODUCT_KEY_IN_USE);
		}
		else if (xmlResponse->products)
		{
			estrCopy2(&xmlResponse->userStatus, PRODUCT_KEY_OK);
		}
		else
		{
			estrCopy2(&xmlResponse->userStatus, PRODUCT_KEY_INVALID);
		}
	}
	else
	{
		estrCopy2(&xmlResponse->userStatus, PRODUCT_KEY_INVALID);
	}

	LogXmlrpcf("Command: HttpIsValidKey; Key: %s; Result %s\n", productKey, xmlResponse->userStatus);
	return xmlResponse;
}

/************************************************************************/
/* Activate product key                                                 */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCActivateProductKeyRequest
{
	const char *pAccountName; AST(NAME(AccountName))
	const char *pProductKey; AST(NAME(ProductKey))
	const char *pReferrer; AST(NAME(Referrer))
} XMLRPCActivateProductKeyRequest;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ActivateProductKeyEx");
XMLAccountResponse * XMLRPCActivateProductKeyEx(XMLRPCActivateProductKeyRequest *pRequest)
{
	AccountInfo *account = NULL;
	XMLAccountResponse *xmlResponse = StructCreate(parse_XMLAccountResponse);
	LoginFailureCode failureCode;

	account = findAccountByName(pRequest->pAccountName);
	if (!account)
		account = findAccountByEmail(pRequest->pAccountName);

	failureCode = accountIsAllowedToLogin(account, false);
	if (failureCode == LoginFailureCode_Ok)
	{
		int iProductKeyErr = 0;
		xmlResponse->loginName = strdup(account->accountName);
		xmlResponse->displayName = strdup(account->displayName);
		xmlResponse->email = strdup(account->personalInfo.email);

		/*if (accountHasKey(account, productKey)) // check if a key for this product has been activated
		{
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_PRODUCT_ACTIVE);
		}
		else */
		if (iProductKeyErr = activateProductKey(account, pRequest->pProductKey, pRequest->pReferrer) != PK_Success)
		{
			// TODO
			if (iProductKeyErr == PK_KeyUsed)
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_PRODUCT_ACTIVE);
			else
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_BAD_PRODUCTKEY);
		}
		else
		{
			XMLRPCAddPermissions(account, xmlResponse);
			strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_FIELDS_MODIFIED);
		}
	}
	else
	{
		strcpy(xmlResponse->userStatus, XMLRPCConvertFailureCode(failureCode));
	}

	LogXmlrpcf("Command: HttpActivateKey; Account or Email: %s, Key: %s; Result %s\n", 
		pRequest->pAccountName, pRequest->pProductKey, xmlResponse->userStatus);

	return xmlResponse;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ActivateProductKey");
XMLAccountResponse * XMLRPCActivateProductKey(const char *pAccountName, const char *pKey)
{
	XMLRPCActivateProductKeyRequest request = {0};
	request.pAccountName = pAccountName;
	request.pProductKey = pKey;
	return XMLRPCActivateProductKeyEx(&request);
}

/************************************************************************/
/* Validate account e-mail                                              */
/************************************************************************/

// MOVED FROM AccountServerHttpRequests.c (/validate_email)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ValidateAccountEmail");
XMLAccountResponse * XMLRPCValidateAccountEmail(const char *accountName, const char *validateEmailKey, int bSendPermissions)
{
	AccountInfo *account = NULL;
	XMLAccountResponse *xmlResponse = StructCreate(parse_XMLAccountResponse);

	account = findAccountByName(accountName);
	if (!account)
		account = findAccountByEmail(accountName);

	if (account && !account->bInternalUseLogin && (account->flags & ACCOUNT_FLAG_NOT_ACTIVATED) && !account->bLoginDisabled)
	{
		ANALYSIS_ASSUME(account);
		xmlResponse->loginName = strdup(account->accountName);
		xmlResponse->displayName = strdup(account->displayName);
		xmlResponse->email = strdup(account->personalInfo.email);

		if (validateAccountEmail(account, validateEmailKey))
		{
			strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_EMAIL_VALIDATED);
			if (bSendPermissions)
				XMLRPCAddPermissions(account, xmlResponse);
		}
		else
			strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_BAD_VALIDATION);
	}
	else if (!account || account->bInternalUseLogin || account->bLoginDisabled)
	{
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_NOT_FOUND);
	}
	else if (!(account->flags & ACCOUNT_FLAG_NOT_ACTIVATED))
	{
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_EXISTS);
	}
	else
	{
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_NOT_FOUND);
	}

	LogXmlrpcf("Command: HttpValidateEmail; Account or Email: %s, Key: %s; Result %s\n", 
		accountName, validateEmailKey, xmlResponse->userStatus);

	return xmlResponse;
}

/************************************************************************/
/* Resend e-mail validation key                                         */
/************************************************************************/

// MOVED FROM AccountServerHttpRequests.c (/new_validate_email_token)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ResendEmailValidationKey");
XMLAccountResponse * XMLPRCResendEmailValidationKey(const char *accountName)
{
	AccountInfo *account = NULL;
	XMLAccountResponse *xmlResponse = StructCreate(parse_XMLAccountResponse);

	account = findAccountByName(accountName);
	if (!account)
		account = findAccountByEmail(accountName);

	if (account && !account->bInternalUseLogin && (account->flags & ACCOUNT_FLAG_NOT_ACTIVATED) && !account->bLoginDisabled)
	{
		// TODO create a new key? or just resend old one?
		xmlResponse->loginName = strdup(account->accountName);
		xmlResponse->displayName = strdup(account->displayName);
		xmlResponse->email = strdup(account->personalInfo.email);
		xmlResponse->validateEmailToken = strdup(account->validateEmailKey);
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_FIELDS_MODIFIED);
	}
	else if (!account || account->bInternalUseLogin || account->bLoginDisabled)
	{
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_NOT_FOUND);
	}
	else if (!(account->flags & ACCOUNT_FLAG_NOT_ACTIVATED))
	{
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_EXISTS);
	}
	else
	{
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_NOT_FOUND);
	}

	LogXmlrpcf("Command: HttpResendEmailKey; Account or Email: %s; Result %s\n", 
		accountName, xmlResponse->userStatus);
	
	return xmlResponse;
}

/************************************************************************/
/* Create new account                                                   */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCCreateAccountRequest
{
	char *accountName;
	char *passwordHash;
	char *displayName;
	char *email;
	char *firstName;
	char *lastName;
	char *defaultCurrency;
	char *defaultLocale;
	char *country;
	int iYear;
	int iMonth;
	int iDay;
	U32 uFlags;
	QuestionsAnswers *questionsAnswers;
	char **ips;								// Internet addresses of customer machine initiating request for new account.
	char *referrer;
} XMLRPCCreateAccountRequest;

// MOVED FROM AccountServerHttpRequests.c (/create_user)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("CreateNewAccount");
XMLAccountResponse * XMLRPCCreateNewAccount(SA_PARAM_NN_VALID const XMLRPCCreateAccountRequest *pRequest)
{
	AccountInfo *account = NULL;
	char sha256Base64[256] = "";
	bool success;

	XMLAccountResponse *xmlResponse = StructCreate(parse_XMLAccountResponse);

	if (accountConvertHexToBase64(pRequest->passwordHash, sha256Base64) == -1)
		sha256Base64[0] = 0;

	if (pRequest->accountName && pRequest->passwordHash &&  pRequest->email && pRequest->accountName[0] && pRequest->passwordHash[0] && pRequest->email[0]) 
	{
		bool bFailed = false;
		int err;
		if (err = StringIsInvalidAccountName(pRequest->accountName, 0))
		{
			bFailed = true;
			if (err == STRINGERR_RESTRICTED || err == STRINGERR_PROFANITY)
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_RESTRICTED_ACCOUNTNAME);
			else if (err == STRINGERR_MIN_LENGTH || err == STRINGERR_MAX_LENGTH)
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_INVALIDLEN_ACCOUNTNAME);
			else
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_INVALID_ACCOUNTNAME);
		}
		else if (pRequest->displayName && (err = StringIsInvalidDisplayName(pRequest->displayName, 0))
				|| !pRequest->displayName && (err = StringIsInvalidDisplayName(pRequest->accountName, 0)))
		{
			bFailed = true;
			if (err == STRINGERR_MIN_LENGTH || err == STRINGERR_MAX_LENGTH)
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_INVALIDLEN_DISPLAYNAME);
			else
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_INVALID_DISPLAYNAME);
		}
		else if (findAccountByEmail(pRequest->email))
		{
			bFailed = true;
			strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_EMAIL_EXISTS);
		}
		else
		{
			if (isUsernameUsed(pRequest->accountName))
				bFailed = true;

			if (isUsernameUsed(pRequest->displayName))
			{
				if (bFailed)
					strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_ALLNAMES_EXISTS);
				else 
				{
					strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_DISPLAYNAME_EXISTS);
					bFailed = true;
				}
			}
			else if (bFailed)
			{
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_EXISTS);
			}
		}

		// Validate secret questions.
		if (!bFailed && pRequest->questionsAnswers)
		{
			if (eaSize(&pRequest->questionsAnswers->questions)
				&& eaSize(&pRequest->questionsAnswers->questions) == eaSize(&pRequest->questionsAnswers->answers))
			{
				INT_EARRAY bad = NULL;
				success = setSecretQuestions(account, pRequest->questionsAnswers, &bad, true);
				if (!success)
					bFailed = true;
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_SECRET_NOT_OK);
				EARRAY_INT_CONST_FOREACH_BEGIN(bad, i, n);
					if (bad[i])
					{
						char buf[12];
						snprintf(buf, sizeof(buf), " %ld", i);
						strncat(xmlResponse->userStatus, buf, sizeof(xmlResponse->userStatus));
					}
				EARRAY_FOREACH_END;
			}
			else
			{
				bFailed = true;
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_NO_ARGS);
			}
		}

		if (!bFailed)
		{
			// Check IP rate limit
			if (!IPRateLimit(pRequest->ips, IPRLA_AccountCreation))
			{
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_RATE_LIMIT);
				bFailed = true;
			}
		}

		if (!bFailed)
		{
			if (createFullAccount(pRequest->accountName, sha256Base64, pRequest->displayName, pRequest->email, pRequest->firstName,
				pRequest->lastName, pRequest->defaultCurrency, pRequest->defaultLocale, pRequest->country, pRequest->ips,
				true, pRequest->uFlags & ACCOUNTCREATE_INTERNAL_LOGIN_ONLY, 0))
			{
				account = findAccountByName(pRequest->accountName);
			}

			if (account)
			{
				int iProductKeyErr = 0;

				accountSetDOB(account, pRequest->iDay, pRequest->iMonth, pRequest->iYear);

				xmlResponse->loginName = strdup(account->accountName);
				xmlResponse->displayName = strdup(account->displayName);
				xmlResponse->email = strdup(account->personalInfo.email);
				xmlResponse->id = account->uID;
				xmlResponse->validateEmailToken = strdup(account->validateEmailKey);
				xmlResponse->guid = strdup(account->globallyUniqueID);
				if (pRequest->questionsAnswers)
				{
					success = setSecretQuestions(account, pRequest->questionsAnswers, NULL, false);
					devassert(success);
				}
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_FIELDS_MODIFIED);

				EARRAY_CONST_FOREACH_BEGIN(pRequest->ips, iCurIP, iNumIPs);
				{
					const char *pIP = pRequest->ips[iCurIP];
					accountLog(account, "Account creation IP: %s", pIP);
				}
				EARRAY_FOREACH_END;
			}
			else
			{
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_EXISTS);
			}
		}
	}
	else
	{
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_UNKNOWN_ERROR);
	}

	LogXmlrpcf("Command: HttpCreateUser; Account: %s, DisplayName: %s, Email: %s; Result %s\n", 
		pRequest->accountName, pRequest->displayName, pRequest->email, xmlResponse->userStatus);
	
	return xmlResponse;
}

/************************************************************************/
/* Set shipping address                                                 */
/************************************************************************/

// MOVED FROM AccountServerHttpRequests.c (/set_shipping_address)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("SetShippingAddress");
XMLAccountResponse * XMLRPCSetShippingAddress(const char *accountName,
											  int bIsAdmin,
											  const char *address1,
											  const char *address2,
											  const char *city,
											  const char *district,
											  const char *postalCode,
											  const char *country,
											  const char *phone)
{
	AccountInfo *account = NULL;
	XMLAccountResponse *xmlResponse = StructCreate(parse_XMLAccountResponse);
	LoginFailureCode failureCode;

	account = findAccountByName(accountName);

	failureCode = accountIsAllowedToLogin(account, false);
	if (failureCode == LoginFailureCode_Ok || (bIsAdmin && account && !account->bInternalUseLogin && !account->bLoginDisabled))
	{
		setShippingAddress(account, address1, address2, city, district, postalCode, country, phone);
		XMLRPCFillFromAccount(account, xmlResponse);
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_FIELDS_MODIFIED);
	}
	else
	{
		strcpy(xmlResponse->userStatus, XMLRPCConvertFailureCode(failureCode));
	}

	LogXmlrpcf("Command: HttpSetShippingAddress; Account: %s, Address1: %s, Address2: %s, City: %s, District: %s, PostalCode: %s, Country: %s, Phone: %s, Result %s\n", 
		accountName, address1, address2, city, district, postalCode, country, phone, xmlResponse->userStatus);
	
	return xmlResponse;
}

/************************************************************************/
/* Update user                                                          */
/************************************************************************/

// MOVED FROM AccountServerHttpRequests.c (/update_user)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("UpdateUser");
XMLAccountResponse * XMLRPCUpdateUser(XMLRPCUpdateUserRequest *pRequest)
{
	char sha256Base64[256] = "";
	AccountInfo *account = NULL;
	LoginFailureCode failureCode;

	XMLAccountResponse *xmlResponse = StructCreate(parse_XMLAccountResponse);

	devassert(pRequest);

	if (accountConvertHexToBase64(pRequest->sha256Password, sha256Base64) == -1)
		sha256Base64[0] = 0;

	account = findAccountByName(pRequest->accountName);

	failureCode = accountIsAllowedToLogin(account, false);
	if (failureCode == LoginFailureCode_Ok || (pRequest->bIsAdmin && account && !account->bInternalUseLogin && !account->bLoginDisabled))
	{
		int err;
		xmlResponse->loginName = strdup(account->accountName);

		// Check secret questions and answers.
		if (pRequest->questionsAnswers)
		{
			if (eaSize(&pRequest->questionsAnswers->questions)
				&& eaSize(&pRequest->questionsAnswers->questions) == eaSize(&pRequest->questionsAnswers->answers))
			{
				INT_EARRAY bad = NULL;
				bool success = setSecretQuestions(account, pRequest->questionsAnswers, &bad, true);
				if (!success)
				{
					strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_SECRET_NOT_OK);
					EARRAY_INT_CONST_FOREACH_BEGIN(bad, i, n);
						if (bad[i])
						{
							char buf[12];
							snprintf(buf, sizeof(buf), " %ld", i);
							strncat(xmlResponse->userStatus, buf, sizeof(xmlResponse->userStatus));
						}
					EARRAY_FOREACH_END;
					return xmlResponse;
				}
			}
			else
			{
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_NO_ARGS);
				return xmlResponse;
			}
		}

		if (pRequest->email && (!account->personalInfo.email || stricmp(account->personalInfo.email, pRequest->email) != 0) &&
			findAccountByEmail(pRequest->email))
		{
			strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_EMAIL_EXISTS);
			return xmlResponse;
		}
		if (pRequest->displayName && (err = StringIsInvalidDisplayName(pRequest->displayName, 0)))
		{
			if (err == STRINGERR_MIN_LENGTH || err == STRINGERR_MAX_LENGTH)
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_INVALIDLEN_DISPLAYNAME);
			else
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_INVALID_DISPLAYNAME);
			return xmlResponse;
		}
		if (pRequest->displayName && stricmp(pRequest->displayName, account->accountName) != 0 && 
			stricmp(pRequest->displayName, account->displayName) != 0 &&
			isUsernameUsed(pRequest->displayName))
		{
			strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_DISPLAYNAME_EXISTS);
			return xmlResponse;
		}
		if (pRequest->email)
		{
			changeAccountEmail(account, pRequest->email, true, NULL);
			xmlResponse->email= strdup(account->personalInfo.email);
			xmlResponse->validateEmailToken = strdup(account->validateEmailKey);
		}
		if (pRequest->displayName)
		{
			changeAccountDisplayName(account, pRequest->displayName, NULL, NULL);
			xmlResponse->displayName = strdup(account->displayName);
		}
		if (pRequest->firstName || pRequest->lastName)
		{
			accountChangePersonalInfo(account->accountName, pRequest->firstName, pRequest->lastName, NULL);
			xmlResponse->firstName = pRequest->firstName ? strdup(pRequest->firstName) : NULL;
			xmlResponse->lastName = pRequest->lastName ? strdup(pRequest->lastName) : NULL;
		}
		if (sha256Base64[0])
		{
			forceChangePasswordHash(account, sha256Base64);
		}
		if (pRequest->locale)
		{
			changeAccountLocale(account->uID, pRequest->locale);
		}
		if (pRequest->currency)
		{
			changeAccountCurrency(account->uID, pRequest->currency);
		}
		if (pRequest->country)
		{
			changeAccountCountry(account->uID, pRequest->country);
		}
		if (pRequest->questionsAnswers)
		{
			bool success = setSecretQuestions(account, pRequest->questionsAnswers, NULL, false);
			devassert(success);
		}

		EARRAY_CONST_FOREACH_BEGIN(pRequest->ips, iCurIP, iNumIPs);
		{
			const char *pIP = pRequest->ips[iCurIP];
			accountLog(account, "Account update IP: %s", pIP);
		}
		EARRAY_FOREACH_END;

		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_FIELDS_MODIFIED);

	}
	else
	{
		strcpy(xmlResponse->userStatus, XMLRPCConvertFailureCode(failureCode));
	}

	LogXmlrpcf("Command: HttpUpdateUser; Account: %s, DisplayName: %s, Email: %s; Result %s\n", 
		pRequest->accountName, pRequest->displayName, pRequest->email, xmlResponse->userStatus);
	
	return xmlResponse;
}

/************************************************************************/
/* Forgot password                                                      */
/************************************************************************/

// MOVED FROM AccountServerHttpRequests.c (/forgot_password)
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ForgotPassword");
XMLAccountResponse * XMLRPCForgotPassword(const char *accountName, const char *sha256Password)
{
	char sha256Base64[256] = "";
	AccountInfo *account = NULL;
	XMLAccountResponse *xmlResponse = StructCreate(parse_XMLAccountResponse);
	LoginFailureCode failureCode;

	if (accountConvertHexToBase64(sha256Password, sha256Base64) == -1)
		sha256Base64[0] = 0;

	account = findAccountByName(accountName);	
	if (!account)
		account = findAccountByEmail(accountName);

	failureCode = accountIsAllowedToLogin(account, false);
	if (failureCode == LoginFailureCode_Ok)
	{
		xmlResponse->loginName = strdup(account->accountName);
		xmlResponse->displayName = strdup(account->displayName);
		xmlResponse->email = strdup(account->personalInfo.email ? account->personalInfo.email : "");
		forceChangePasswordHash(account, sha256Base64);
		strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_FIELDS_MODIFIED);
	}
	else
	{
		strcpy(xmlResponse->userStatus, XMLRPCConvertFailureCode(failureCode));
	}

	LogXmlrpcf("Command: HttpForgotPassword; Account or Email: %s, Password(Hex): %s; Result %s\n", 
		accountName, sha256Password, xmlResponse->userStatus);

	return xmlResponse;
}

/************************************************************************/
/* Validate login                                                       */
/************************************************************************/

// Parameters for XMLRPCValidateLoginEx()
AUTO_STRUCT;
typedef struct XMLRPCValidateLoginExRequest
{
	const char *accountName;								// Private account name
	const char *sha256Password;	AST(NAME(sha256Password))	// SHA-256 hash of password, without salt, hexadecimal encoded
	const char *pCrypticPasswordWithAccountNameAndNewStyleSalt; //new style password already hashed with account name and temp salt		
	const char *md5Password;								// Legacy MD5 hash of password, without salt, hexadecimal encoded
															// Used for Perfect World logins
	const char *encryptedPassword;
	AccountServerEncryptionKeyVersion eKeyVersion; AST(NAME(KeyVersion))
	const char *machineID;
	int iFlags;					AST(NAME(Flags))			// USERINFO_ flags for account response
	bool bInternal;				NO_AST						// If true, called from another XML-RPC function
	U32 uSalt;					AST(NAME(Salt) DEFAULT(0))

	// Data for accountLogLoginAttempt().  See that function for more information on these parameters.
	char **ips;
	char *location;
	char *referrer;
	char *clientVersion;
	char *note;
	AccountLoginType eType; AST(DEFAULT(ACCOUNTLOGINTYPE_Default))
} XMLRPCValidateLoginExRequest;

static XMLAccountResponse *XMLRPCCompleteAccountLogin(SA_PARAM_NN_VALID AccountInfo *account, int iFlags)
{
	XMLAccountResponse *xmlResponse = XMLRPCGetUserInfo(account, iFlags);
	strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_USER_LOGIN_OK);
	return xmlResponse;
}

static void XMLRPCValidateLoginExCallback(AccountInfo * pAccount, LoginFailureCode eLoginFailure, U32 uConflictTicket, void * userData)
{
	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo = userData;
	XMLAccountResponse *xmlResponse = NULL;
	XMLRPCValidateLoginExRequest *pRequest = pSlowReturnInfo->pUserData;
	char *pReturnString = NULL;

	// Send appropriate response.
	if (pAccount)
	{
		bool bMachineRestricted = !accountIsMachineIDAllowed(pAccount, pRequest->machineID, MachineType_WebBrowser, 
			eaSize(&pRequest->ips) ? pRequest->ips[0] : NULL, &eLoginFailure);

		xmlResponse = XMLRPCCompleteAccountLogin(pAccount, pRequest->iFlags);
		if (bMachineRestricted)
		{
			if (accountMachineSaveNextBrowser(pAccount, timeSecondsSince2000()))
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_SAVENEXTBROWSER);
			else
				strcpy(xmlResponse->userStatus, ACCOUNT_HTTP_NEWMACHINEID);
		}
	}
	else
	{
		xmlResponse = StructCreate(parse_XMLAccountResponse);
		if (eLoginFailure == LoginFailureCode_UnlinkedPWCommonAccount)
		{
			PWCommonAccount *pwAccount = findPWCommonAccountByLoginField(pRequest->accountName);
			if (pwAccount && pRequest->iFlags & USERINFO_PERFECTWORLDACCOUNT)
			{
				ANALYSIS_ASSUME(pwAccount);
				XMLRPCCopyPWAccount(xmlResponse, pwAccount);
			}
		}
		strcpy(xmlResponse->userStatus, XMLRPCConvertFailureCode(eLoginFailure));
	}

	if (pRequest->bInternal)
	{
		// Note: Formerly, the following erroneously reported "XMLRPCListSubscriptions()"
		LogXmlrpcf("Response: ValidateLogin(): %s", NULL_TO_EMPTY(xmlResponse->userStatus));
	}
	else
	{
		LogXmlrpcf("Response: ValidateLoginEx(): %s", NULL_TO_EMPTY(xmlResponse->userStatus));
	}

	XMLRPC_WriteSimpleStructResponse(&pReturnString, xmlResponse, parse_XMLAccountResponse);
	DoSlowCmdReturn(1, pReturnString, pSlowReturnInfo);

	estrDestroy(&pReturnString);
	StructDestroy(parse_XMLRPCValidateLoginExRequest, pRequest);
	StructDestroy(parse_XMLAccountResponse, xmlResponse);
	free(pSlowReturnInfo);
}

// Authenticate a login attempt.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ValidateLoginEx");
void XMLRPCValidateLoginEx(
	CmdContext *pContext,
	SA_PARAM_NN_VALID XMLRPCValidateLoginExRequest *pRequest)
{
	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo = malloc(sizeof(*pSlowReturnInfo));
	AccountLoginAttemptData loginData = {0};
	char sha256Base64[256] = {0};

	pContext->slowReturnInfo.bDoingSlowReturn = true;

	*pSlowReturnInfo = pContext->slowReturnInfo;
	pSlowReturnInfo->pUserData = StructClone(parse_XMLRPCValidateLoginExRequest, pRequest);

	if (!pRequest->bInternal)
		LogXmlrpcf("Request: ValidateLoginEx(%s, %d, %s)", pRequest->accountName, pRequest->iFlags, StaticDefineIntRevLookup(AccountLoginTypeEnum, pRequest->eType));

	// Populate loginData
	loginData.pPrivateAccountName = pRequest->accountName;
	loginData.pMD5Password = pRequest->md5Password;
	loginData.pCrypticPasswordWithAccountNameAndNewStyleSalt = pRequest->pCrypticPasswordWithAccountNameAndNewStyleSalt;
	loginData.pPasswordEncrypted = pRequest->encryptedPassword;
	loginData.eKeyVersion = pRequest->eKeyVersion;
	loginData.salt = pRequest->uSalt;
	loginData.eLoginType = pRequest->eType;
	loginData.pMachineID = pRequest->machineID;
	loginData.bIpNeedsPassword = true;
	loginData.bIpAllowedAutocreate = false;
	loginData.ips = pRequest->ips;
	loginData.protocol = LoginProtocol_Xmlrpc;
	loginData.location = pRequest->location;
	loginData.referrer = pRequest->referrer;
	loginData.clientVersion = pRequest->clientVersion;
	loginData.note = pRequest->note;
	loginData.peerIp = XmlInterfaceGetIPStr();

	// Try to login.
	// Convert hexadecimal encoding to base64.
	if (accountConvertHexToBase64(pRequest->sha256Password, sha256Base64) == -1)
		sha256Base64[0] = 0;
	loginData.pSHA256Password = sha256Base64;
	confirmLogin(&loginData, false, XMLRPCValidateLoginExCallback, pSlowReturnInfo);		
}

// MOVED FROM AccountServerHttpRequests.c (/login): DEPRECATED by ValidateLoginEx()
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ValidateLogin");
void XMLRPCValidateLogin(
	CmdContext *pContext,
	const char *accountName, const char *sha256Password, const char *md5Password, int iFlags)
{
	XMLRPCValidateLoginExRequest request = {0};

	LogXmlrpcf("Request: ValidateLogin(%s, %d)", accountName, iFlags);

	// Create request struct.
	StructInit(parse_XMLRPCValidateLoginExRequest, &request);
	request.accountName = accountName;
	request.sha256Password = sha256Password;
	request.md5Password = md5Password;
	request.iFlags = iFlags;
	request.bInternal = true;

	// Forward to ValidateLoginEx().
	XMLRPCValidateLoginEx(pContext, &request);

	// Destroy request struct.
	request.accountName = NULL;
	request.sha256Password = NULL;
	request.md5Password = NULL;
	StructDeInit(parse_XMLRPCValidateLoginExRequest, &request);
}

AUTO_STRUCT;
typedef struct XMLRPCSaveNextMachineRequest
{
	U32 accountID;
	const char *machineID;
	const char *machineName;
	MachineType machineType;
	char *ip;
} XMLRPCSaveNextMachineRequest;

AUTO_STRUCT;
typedef struct XMLRPCSaveNextMachineResponse
{
	char *result; AST(UNOWNED)
} XMLRPCSaveNextMachineResponse;

// Input the machine name and tell the account to save the machine ID, or just to clear the "Save Next Machine" flag if no machine name is entered
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("SaveNextMachine");
XMLRPCSaveNextMachineResponse * XMLRPCSaveNextMachine(XMLRPCSaveNextMachineRequest *pRequest)
{
	XMLRPCSaveNextMachineResponse *xmlResponse = StructCreate(parse_XMLRPCSaveNextMachineResponse);
	AccountInfo *account;
	LogXmlrpcf("Request: SaveNextMachine(%d, %s)", pRequest->accountID, pRequest->machineID);

	account = findAccountByID(pRequest->accountID);
	if (!account)
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	}
	else if (nullStr(pRequest->machineID))
	{
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
	}
	else if (!accountMachineSaveNextMachineByType(account, timeSecondsSince2000(), pRequest->machineType))
	{
		xmlResponse->result = ACCOUNT_HTTP_NOT_AUTHORIZED;
	}
	else
	{
		accountMachineProcessAutosave(account, pRequest->machineID, pRequest->machineName, pRequest->machineType, pRequest->ip);
		xmlResponse->result = ACCOUNT_HTTP_SUCCESS;
	}
	LogXmlrpcf("Response: SaveNextMachine(): %s", NULL_TO_EMPTY(xmlResponse->result));
	return xmlResponse;
}

/************************************************************************/
/* One-Time Codes                                                       */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCGenerateOTCRequest
{
	U32 accountID;
	const char *machineID;
	char **ips;
} XMLRPCGenerateOTCRequest;

AUTO_STRUCT;
typedef struct XMLRPCGenerateOTCResponse
{
	char *result; AST(UNOWNED)
} XMLRPCGenerateOTCResponse;

// Generate a One-Time Code
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("GenerateOTC");
XMLRPCGenerateOTCResponse * XMLRPCGenerateOTC(XMLRPCGenerateOTCRequest *pRequest)
{
	XMLRPCGenerateOTCResponse *xmlResponse = StructCreate(parse_XMLRPCGenerateOTCResponse);
	AccountInfo *account;
	LogXmlrpcf("Request: GenerateOTC(%d, %s)", pRequest->accountID, pRequest->machineID);

	account = findAccountByID(pRequest->accountID);
	if (!account)
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	}
	else if (nullStr(pRequest->machineID))
	{
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
	}
	else
	{
		generateOneTimeCode(account, pRequest->machineID, pRequest->ips, LoginProtocol_Xmlrpc);
		xmlResponse->result = ACCOUNT_HTTP_SUCCESS;
	}
	LogXmlrpcf("Response: GenerateOTC(): %s", NULL_TO_EMPTY(xmlResponse->result));
	return xmlResponse;
}

AUTO_STRUCT;
typedef struct XMLRPCRemoveOTCRequest
{
	U32 accountID;
	const char *machineID;
} XMLRPCRemoveOTCRequest;

AUTO_STRUCT;
typedef struct XMLRPCRemoveOTCResponse
{
	char *result; AST(UNOWNED)
} XMLRPCRemoveOTCResponse;

// Remvoe the One-Time Code for a machine ID
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("RemoveOTC");
XMLRPCRemoveOTCResponse * XMLRPCRemoveOTC(XMLRPCRemoveOTCRequest *pRequest)
{
	XMLRPCRemoveOTCResponse *xmlResponse = StructCreate(parse_XMLRPCRemoveOTCResponse);
	AccountInfo *account;
	LogXmlrpcf("Request: RemoveOTC(%d, %s)", pRequest->accountID, pRequest->machineID);

	account = findAccountByID(pRequest->accountID);
	if (!account)
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	}
	else if (nullStr(pRequest->machineID))
	{
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
	}
	else
	{
		accountMachineRemoveOneTimeCode(account, pRequest->machineID);
		xmlResponse->result = ACCOUNT_HTTP_SUCCESS;
	}
	LogXmlrpcf("Response: RemoveOTC(): %s", NULL_TO_EMPTY(xmlResponse->result));
	return xmlResponse;
}

AUTO_STRUCT;
typedef struct XMLRPCValidateLoginOTCRequest
{
	U32 accountID;
	const char *machineID;
	const char *oneTimeCode;
	const char *machineName;

	// Data for accountLogOneTimeCodeValidation()
	char **ips;
	char *location;
	char *referrer;
	char *clientVersion;
	char *note;
} XMLRPCValidateLoginOTCRequest;

AUTO_STRUCT;
typedef struct XMLValidateLoginOTCResponse
{
	char *result; AST(UNOWNED)
} XMLValidateLoginOTCResponse;

// Validate a Login One-Time Code
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ValidateLoginOTC");
XMLValidateLoginOTCResponse * XMLRPCValidateLoginOTC(XMLRPCValidateLoginOTCRequest *pRequest)
{

	AccountInfo *account = NULL;
	XMLValidateLoginOTCResponse *xmlResponse = StructCreate(parse_XMLValidateLoginOTCResponse);
	enum LoginFailureCode failureCode;
	AccountLoginAttemptData loginData = {0};
	
	LogXmlrpcf("Request: ValidateLoginOTC(%d, %s, %s)", pRequest->accountID, pRequest->machineID, pRequest->oneTimeCode);

	// Populate loginData
	loginData.ips = pRequest->ips;
	loginData.protocol = LoginProtocol_Xmlrpc;
	loginData.location = pRequest->location;
	loginData.referrer = pRequest->referrer;
	loginData.clientVersion = pRequest->clientVersion;
	loginData.note = pRequest->note;
	loginData.peerIp = XmlInterfaceGetIPStr();
	loginData.pMachineID = pRequest->machineID;

	account = findAccountByID(pRequest->accountID);
	processOneTimeCode(account, &loginData, pRequest->oneTimeCode, pRequest->machineName, &failureCode);
	if (failureCode == LoginFailureCode_Ok)
	{
		//xmlResponse = XMLRPCCompleteAccountLogin(account, pRequest->iFlags);
		xmlResponse->result = ACCOUNT_HTTP_SUCCESS;
	}
	else
	{
		xmlResponse->result = XMLRPCConvertFailureCode(failureCode);
	}
	LogXmlrpcf("Response: ValidateLoginOTC(): %s", NULL_TO_EMPTY(xmlResponse->result));
	return xmlResponse;
}

AUTO_STRUCT;
typedef struct XMLRPCMachineRenameSavedRequest
{
	const char *accountName;	// Private account name
	const char *machineID;
	const char *machineName;
	MachineType type;
} XMLRPCMachineRenameSavedRequest;

AUTO_STRUCT;
typedef struct XMLRPCMachineRenameSavedResponse
{
	char *result; AST(UNOWNED)
} XMLRPCMachineRenameSavedResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("MachineRenameSaved");
XMLRPCMachineRenameSavedResponse *XMLRPCMachineRenameSaved(XMLRPCMachineRenameSavedRequest *pRequest)
{
	XMLRPCMachineRenameSavedResponse *xmlResponse = StructCreate(parse_XMLRPCMachineRenameSavedResponse);
	AccountInfo *pAccount = findAccountByName(pRequest->accountName);

	LogXmlrpcf("Request: MachineRenameSaved(%s, %s, %s)", pRequest->accountName, NULL_TO_EMPTY(pRequest->machineID), pRequest->machineName);
	if (!pAccount)
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	}
	else if (!nullStr(pRequest->machineID) && !nullStr(pRequest->machineName))
	{
		accountRenameSavedMachine(pAccount, pRequest->machineID, pRequest->type, pRequest->machineName);
		xmlResponse->result = ACCOUNT_HTTP_SUCCESS;
	}
	else
	{
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
	}
	LogXmlrpcf("Response: MachineRenameSaved(): %s", NULL_TO_EMPTY(xmlResponse->result));
	return xmlResponse;
}

AUTO_STRUCT;
typedef struct XMLRPCMachineRemoveSavedRequest
{
	const char *accountName;	// Private account name
	const char *machineID;      // If you only want to clear one machine ID (clearAll must be false)
	bool clearAll;				// Whether or not to clear all machines
	MachineType type;
} XMLRPCMachineRemoveSavedRequest;

AUTO_STRUCT;
typedef struct XMLRPCMachineRemoveSavedResponse
{
	char *result; AST(UNOWNED)
} XMLRPCMachineRemoveSavedResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("MachineRemoveSaved");
XMLRPCMachineRemoveSavedResponse *XMLRPCMachineRemoveSaved(XMLRPCMachineRemoveSavedRequest *pRequest)
{
	XMLRPCMachineRemoveSavedResponse *xmlResponse = StructCreate(parse_XMLRPCMachineRemoveSavedResponse);
	AccountInfo *pAccount = findAccountByName(pRequest->accountName);

	LogXmlrpcf("Request: MachineRemoveSaved(%s, %s, %d)", pRequest->accountName, NULL_TO_EMPTY(pRequest->machineID), pRequest->clearAll);
	if (!pAccount)
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	}
	else if (pRequest->clearAll)
	{
		accountClearSavedMachines(pAccount, pRequest->type);
		xmlResponse->result = ACCOUNT_HTTP_SUCCESS;
	}
	else if (!nullStr(pRequest->machineID))
	{
		accountRemoveSavedMachine(pAccount, pRequest->machineID, pRequest->type);
		xmlResponse->result = ACCOUNT_HTTP_SUCCESS;
	}
	else
	{
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
	}
	LogXmlrpcf("Response: MachineRemoveSaved(): %s", NULL_TO_EMPTY(xmlResponse->result));
	return xmlResponse;
}

AUTO_STRUCT;
typedef struct XMLRPCMachineLockingEnableRequest
{
	const char *accountName;	// Private account name
	bool enable;
} XMLRPCMachineLockingEnableRequest;

AUTO_STRUCT;
typedef struct XMLRPCMachineLockingEnableResponse
{
	char *result; AST(UNOWNED)
} XMLRPCMachineLockingEnableResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("MachineLockingEnable");
XMLRPCMachineLockingEnableResponse *XMLRPCMachineLockingEnable(XMLRPCMachineLockingEnableRequest *pRequest)
{
	XMLRPCMachineLockingEnableResponse *xmlResponse = StructCreate(parse_XMLRPCMachineLockingEnableResponse);
	AccountInfo *pAccount = findAccountByName(pRequest->accountName);
	LogXmlrpcf("Request: MachineLockingEnable(%s, %d)", pRequest->accountName, pRequest->enable);
	if (!pAccount)
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	}
	else
	{
		accountMachineLockingEnable(pAccount, pRequest->enable);
		xmlResponse->result = ACCOUNT_HTTP_SUCCESS;
	}
	LogXmlrpcf("Response: MachineLockingEnable(): %s", NULL_TO_EMPTY(xmlResponse->result));
	return xmlResponse;
}

AUTO_STRUCT;
typedef struct XMLRPCMachineLockingSaveNextRequest
{
	const char *accountName;	// Private account name
	bool enable;
} XMLRPCMachineLockingSaveNextRequest;

AUTO_STRUCT;
typedef struct XMLRPCMachineLockingSaveNextResponse
{
	char *result; AST(UNOWNED)
} XMLRPCMachineLockingSaveNextResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("MachineLockingSaveNext");
XMLRPCMachineLockingSaveNextResponse *XMLRPCMachineLockingSaveNext(XMLRPCMachineLockingEnableRequest *pRequest)
{
	XMLRPCMachineLockingSaveNextResponse *xmlResponse = StructCreate(parse_XMLRPCMachineLockingSaveNextResponse);
	AccountInfo *pAccount = findAccountByName(pRequest->accountName);
	LogXmlrpcf("Request: MachineLockingSaveNext(%s, %d)", pRequest->accountName, pRequest->enable);
	if (!pAccount)
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	}
	else
	{
		if (accountMachineLockingIsEnabled(pAccount))
		{
			accountMachineLockingSaveNext(pAccount, pRequest->enable, MachineType_All, false);
			xmlResponse->result = ACCOUNT_HTTP_SUCCESS;
		}
		else
			xmlResponse->result = ACCOUNT_HTTP_INVALID_ACCOUNT_TYPE;
	}
	LogXmlrpcf("Response: MachineLockingSaveNext(): %s", NULL_TO_EMPTY(xmlResponse->result));
	return xmlResponse;
}


/************************************************************************/
/* Reporting a login failure, from an external source                   */
/************************************************************************/

// The purpose of this call is so that an external agent that decides to deny a login attempt
// can report to us that it has done so, and we can include this fact in our logs.  Generally,
// this will happen before the external agent actually asks us to authenticate the credentials.

// Logging request parameters.
AUTO_STRUCT;
typedef struct XMLRPCReportLoginFailureRequest
{
	const char *accountName;			// Private account name
	char *rejectReason;					// Reason that the external agent rejected the login

	// Data for accountLogLoginAttempt().  See that function for more information on these parameters.
	char **ips;
	char *location;											
	char *referrer;
	char *clientVersion;
	char *note;
} XMLRPCReportLoginFailureRequest;

// Response from the function
AUTO_STRUCT;
typedef struct XMLRPCReportLoginFailureResponse
{
	char *result; AST(UNOWNED)			// This should always be ACCOUNT_HTTP_SUCCESS
} XMLRPCReportLoginFailureResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("ReportLoginFailure");
XMLRPCReportLoginFailureResponse * XMLRPCReportLoginFailure(SA_PARAM_NN_VALID XMLRPCReportLoginFailureRequest *pRequest)
{
	XMLRPCReportLoginFailureResponse *xmlResponse;
	AccountLoginAttemptData loginData = {0};

	LogXmlrpcf("Request: ReportLoginFailure(%s, %s)", NULL_TO_EMPTY(pRequest->accountName), NULL_TO_EMPTY(pRequest->rejectReason));

	// Populate loginData
	loginData.pPrivateAccountName = pRequest->accountName;
	loginData.bRejectedAPriori = true;
	loginData.aPrioriReason = pRequest->rejectReason;
	loginData.ips = pRequest->ips;
	loginData.protocol = LoginProtocol_Xmlrpc;
	loginData.location = pRequest->location;
	loginData.referrer = pRequest->referrer;
	loginData.clientVersion = pRequest->clientVersion;
	loginData.note = pRequest->note;
	loginData.peerIp = XmlInterfaceGetIPStr();

	// Log information provided by website.
	accountLogLoginAttempt(&loginData);

	xmlResponse = StructCreate(parse_XMLRPCReportLoginFailureResponse);
	xmlResponse->result = ACCOUNT_HTTP_SUCCESS;

	LogXmlrpcf("Response: ReportLoginFailure(): %s", NULL_TO_EMPTY(xmlResponse->result));

	return xmlResponse;
}

/************************************************************************/
/* Purchase                                                             */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCPurchaseRequestItem
{
	U32 uProductID;				AST(NAME(ProductID))
	char *pPrice;
} XMLRPCPurchaseRequestItem;

AUTO_STRUCT;
typedef struct XMLRPCPurchaseRequestEx
{
	char *user;					AST(ESTRING)
	char *currency;				AST(ESTRING) 
	PaymentMethod *pPaymentMethod;
	EARRAY_OF(XMLRPCPurchaseRequestItem) eaItems;
	char *ip;					AST(ESTRING)
	char *bankName;				AST(ESTRING)
	bool bAuthOnly;
	bool bVerifyPrice;

	char *steamid;	// If specified, the purchase is implicitly a Steam purchase
	char *source;
	char *locCode;				AST(ESTRING)
} XMLRPCPurchaseRequestEx;

AUTO_STRUCT;
typedef struct XMLRPCPurchaseRequest
{
	char *user;					AST(ESTRING)
	char *currency;				AST(ESTRING) 
	PaymentMethod *pPaymentMethod;
	U32 *eaProductID;
	char *ip;					AST(ESTRING)
	char *bankName;				AST(ESTRING)
} XMLRPCPurchaseRequest;

AUTO_STRUCT;
typedef struct XMLRPCPurchaseResponse
{
	char *resultString;			AST(ESTRING)
	char *transID;				AST(ESTRING)
} XMLRPCPurchaseResponse;

static void XMLRPCPurchase_CB(PurchaseResult eResult,
							  SA_PARAM_OP_VALID BillingTransaction *pTrans,
							  PurchaseSession *pPurchaseSession,
							  PurchaseResult *pResult)
{
	PERFINFO_AUTO_START_FUNC();
	if (eResult != PURCHASE_RESULT_PENDING_PAYPAL)
	{
		// Only safe if the purchase is for points
		if (PurchaseIsPoints(pPurchaseSession))
		{
			*pResult = eResult;
		}

		btCompleteSteamTransaction(pTrans, eResult == PURCHASE_RESULT_COMMIT);
	}
	PERFINFO_AUTO_STOP();
}

static void XMLRPCAuth_CB(PurchaseResult eResult,
							  SA_PARAM_OP_VALID BillingTransaction *pTrans,
							  PurchaseSession *pPurchaseSession,
							  PurchaseResult *pResult)
{
	PERFINFO_AUTO_START_FUNC();
	if (pTrans)
	{
		U32 uPurchaseID = PurchaseDelaySession(pPurchaseSession);

		if (uPurchaseID)
		{
			pTrans->uPurchaseID = uPurchaseID;
		}
		else
		{
			btFail(pTrans, "Could not delay transaction.");

			// I'm pretty sure this line below is completely pointless
			eResult = PURCHASE_RESULT_DELAY_FAILURE;
		}

		btCompleteSteamTransaction(pTrans, eResult == PURCHASE_RESULT_PENDING);
	}
	PERFINFO_AUTO_STOP();
}

static PurchaseResult XMLRPCPurchase_DoPurchase(SA_PARAM_OP_VALID BillingTransaction *pTrans,
												SA_PARAM_NN_VALID AccountInfo *pAccount,
												SA_PARAM_NN_VALID const XMLRPCPurchaseRequestEx *pRequest,
												SA_PARAM_OP_VALID PurchaseResult *pResultPoints)
{
	PurchaseResult eResult = PURCHASE_RESULT_PENDING;
	EARRAY_OF(TransactionItem) eaItems = NULL;
	PurchaseDetails purchaseDetails = {0};

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(pRequest->eaItems, iCurItem, iNumItems);
	{
		XMLRPCPurchaseRequestItem *pXMLItem = pRequest->eaItems[iCurItem];
		TransactionItem *pItem = StructCreate(parse_TransactionItem);

		pItem->uProductID = pXMLItem->uProductID;
		if (pXMLItem->pPrice)
		{
			pItem->pPrice = StructCreate(parse_Money);
			moneyInitFromStr(pItem->pPrice, pXMLItem->pPrice, pRequest->currency);
		}
		eaPush(&eaItems, pItem);
	}
	EARRAY_FOREACH_END;

	if (pRequest->steamid)
	{
		U64 uSteamID = atoui64(pRequest->steamid);
		devassert(uSteamID);
		PopulatePurchaseDetailsSteam(&purchaseDetails, pRequest->source, NULL, NULL, pRequest->ip, NULL, NULL, uSteamID, pRequest->locCode, true);
		btMarkSteamTransaction(pTrans);
		purchaseDetails.pTrans = pTrans;
	}
	else
	{
		PopulatePurchaseDetails(&purchaseDetails, pRequest->source, NULL, NULL, pRequest->ip, pRequest->pPaymentMethod, pTrans, pRequest->bankName);
	}

	if (pRequest->bAuthOnly)
	{
		eResult = PurchaseProductLock(pAccount, eaItems, pRequest->currency, 0, pRequest->bVerifyPrice, &purchaseDetails, XMLRPCAuth_CB, pResultPoints);
	}
	else
	{
		eResult = PurchaseProduct(pAccount, eaItems, pRequest->currency, 0, pRequest->bVerifyPrice, &purchaseDetails, XMLRPCPurchase_CB, pResultPoints);
	}

	StructDeInit(parse_PurchaseDetails, &purchaseDetails);
	eaDestroyStruct(&eaItems, parse_TransactionItem);

	PERFINFO_AUTO_STOP();
	return eResult;
}

static void XMLRPCPurchase_Callback(UpdatePMResult eResult,
									SA_PARAM_OP_VALID BillingTransaction *pTrans,
									SA_PARAM_OP_VALID const CachedPaymentMethod *pCachedPaymentMethod,
									SA_PARAM_NN_VALID XMLRPCPurchaseRequestEx *pRequest)
{
	PERFINFO_AUTO_START_FUNC();
	if (eResult == UPMR_Success)
	{
		if (!devassertmsg(pTrans, "After a payment method update, if it is success, there must be a billing transaction.")) return;

		if (pCachedPaymentMethod)
		{
			AccountInfo *pAccount = findAccountByNameOrEmail(pRequest->user);

			if (!pAccount)
			{
				PERFINFO_AUTO_STOP();
				return;
			}

			pRequest->pPaymentMethod->VID = estrDup(pCachedPaymentMethod->VID);
			XMLRPCPurchase_DoPurchase(pTrans, pAccount, pRequest, NULL);
			StructDestroy(parse_XMLRPCPurchaseRequestEx, pRequest);
		}
	}
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("PurchaseEx");
SA_RET_NN_VALID XMLRPCPurchaseResponse * XMLRPCPurchaseEx(SA_PARAM_NN_VALID XMLRPCPurchaseRequestEx *pRequest)
{
	XMLRPCPurchaseResponse *pResponse = StructCreate(parse_XMLRPCPurchaseResponse);
	AccountInfo *pAccount = findAccountByNameOrEmail(pRequest->user);
	PurchaseResult eResult = PURCHASE_RESULT_PENDING;

	LogXmlrpcf("Request: Purchase(%s)", NULL_TO_EMPTY(pRequest->user));

	if (!pAccount)
	{
		pResponse->resultString = estrDup(ACCOUNT_HTTP_USER_NOT_FOUND);
		return pResponse;
	}

	if (!pRequest->currency)
	{
		pResponse->resultString = estrDup(ACCOUNT_HTTP_INVALID_CURRENCY);
		return pResponse;
	}

	// If the purchase is real money...
	if (isRealCurrency(pRequest->currency))
	{
		BillingTransaction *pTrans = NULL;
		PaymentMethodProblem eProblem = PMP_None;
		const char *pErrorDetails = NULL;

		// If a payment method was provided, make sure it is (somewhat) valid
		if (pRequest->pPaymentMethod)
		{	
			const CachedPaymentMethod *pCachedPaymentMethod = NULL;

			if (pRequest->pPaymentMethod->VID && *pRequest->pPaymentMethod->VID)
			{
				pCachedPaymentMethod = getCachedPaymentMethod(pAccount, pRequest->pPaymentMethod->VID);

				if (pCachedPaymentMethod && !pRequest->pPaymentMethod->country)
				{
					pRequest->pPaymentMethod->country = estrDup(pCachedPaymentMethod->billingAddress.country);
				}
			}

			eProblem = paymentMethodValid(pAccount->forbiddenPaymentMethodTypes, pRequest->pPaymentMethod, pCachedPaymentMethod, pRequest->bankName, &pErrorDetails);
		}

		// If the payment method is valid, initiate the purchase
		if (eProblem == PMP_None)
		{
			pTrans = btCreateBlank(true);

			if (pRequest->pPaymentMethod && ShouldDoUpdatePaymentMethod(pRequest->pPaymentMethod))
			{
				// Do an update payment method
				UpdatePaymentMethod(pAccount, pRequest->pPaymentMethod, pRequest->ip, pRequest->bankName, pTrans, XMLRPCPurchase_Callback, StructClone(parse_XMLRPCPurchaseRequestEx, pRequest));
			}
			else
			{
				// Just do the purchase
				eResult = XMLRPCPurchase_DoPurchase(pTrans, pAccount, pRequest, NULL);
			}
		}
		else
		{
			// Return an error about what went wrong with the payment method
			if (pErrorDetails)
			{
				estrPrintf(&pResponse->resultString, "%s: %s",
					StaticDefineIntRevLookupNonNull(PaymentMethodProblemEnum, eProblem), pErrorDetails);
			}
			else
			{
				estrPrintf(&pResponse->resultString, "%s",
					StaticDefineIntRevLookupNonNull(PaymentMethodProblemEnum, eProblem));
			}
		}

		if (pTrans)
		{
			estrCopy2(&pResponse->transID, pTrans->webUID);
		}
		else if (eResult == PURCHASE_RESULT_PENDING && !pResponse->resultString)
		{
			pResponse->resultString = estrDup(ACCOUNT_HTTP_INVALID_PAYMENT_METHOD);
		}
	}
	else
	{
		PurchaseResult eResultPoints = PURCHASE_RESULT_PENDING;
		BillingTransaction *pTrans = btCreateBlank(true);

		estrCopy2(&pResponse->transID, pTrans->webUID);

		// If the purchase is using points, the callback will be called before this function returns,
		// so the stack user data variable will still have a valid address.
		eResult = XMLRPCPurchase_DoPurchase(pTrans, pAccount, pRequest, &eResultPoints);

		if (eResultPoints != PURCHASE_RESULT_PENDING) eResult = eResultPoints;
	}

	if (eResult != PURCHASE_RESULT_PENDING)
	{
		pResponse->resultString = estrDup(purchaseResultMessage(eResult));
	}
	
	LogXmlrpcf("Response: Purchase(): %s", NULL_TO_EMPTY(pResponse->resultString));
	return pResponse;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("Purchase");
SA_RET_NN_VALID XMLRPCPurchaseResponse * XMLRPCPurchase(SA_PARAM_NN_VALID XMLRPCPurchaseRequest *pRequest)
{
	XMLRPCPurchaseRequestEx *pRequestEx = StructCreate(parse_XMLRPCPurchaseRequestEx);
	XMLRPCPurchaseResponse *pResponse = NULL;
	int iCurProductID = 0;

	for (iCurProductID = 0; iCurProductID < ea32Size(&pRequest->eaProductID); iCurProductID++)
	{
		XMLRPCPurchaseRequestItem *pItem = StructCreate(parse_XMLRPCPurchaseRequestItem);
		pItem->uProductID = pRequest->eaProductID[iCurProductID];
		eaPush(&pRequestEx->eaItems, pItem);
	}
	pRequestEx->user = pRequest->user;
	pRequestEx->currency = pRequest->currency;
	pRequestEx->pPaymentMethod = pRequest->pPaymentMethod;
	pRequestEx->ip = pRequest->ip;
	pRequestEx->bankName = pRequest->bankName;

	pResponse = XMLRPCPurchaseEx(pRequestEx);

	pRequestEx->user = NULL;
	pRequestEx->currency = NULL;
	pRequestEx->pPaymentMethod = NULL;
	pRequestEx->ip = NULL;
	pRequestEx->bankName = NULL;
	StructDestroy(parse_XMLRPCPurchaseRequestEx, pRequestEx);

	return pResponse;
}


/************************************************************************/
/* Complete a started purchase                                          */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCCompletePurchaseRequest
{
	char *user;
	U32 uPurchaseID;			AST(NAME(PurchaseID))
} XMLRPCCompletePurchaseRequest;

AUTO_STRUCT;
typedef struct XMLRPCCompletePurchaseResponse
{
	char *resultString;			AST(UNOWNED)
	char *transID;				AST(UNOWNED)
} XMLRPCCompletePurchaseResponse;

static void XMLRPCCompletePurchase_Callback(PurchaseResult eResult,
											SA_PARAM_OP_VALID BillingTransaction *pTrans,
											PurchaseSession *pPurchaseSession,
											SA_PARAM_OP_VALID void *pUserData)
{
	PERFINFO_AUTO_START_FUNC();
	if (eResult == PURCHASE_RESULT_RETRIEVED)
	{
		PurchaseProductFinalize(pPurchaseSession, true, NULL, NULL);
	}
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("CompletePurchase");
SA_RET_NN_VALID XMLRPCCompletePurchaseResponse * XMLRPCCompletePurchase(SA_PARAM_NN_VALID XMLRPCCompletePurchaseRequest *pRequest)
{
	XMLRPCCompletePurchaseResponse *pResponse = StructCreate(parse_XMLRPCCompletePurchaseResponse);
	const AccountInfo *pAccount = findAccountByNameOrEmail(pRequest->user);
	BillingTransaction *pTrans = NULL;

	LogXmlrpcf("Request: CompletePurchase(%s)", NULL_TO_EMPTY(pRequest->user));

	if (!pAccount)
	{
		pResponse->resultString = ACCOUNT_HTTP_USER_NOT_FOUND;
		return pResponse;
	}

	pTrans = PurchaseRetrieveDelayedSession(pAccount, pRequest->uPurchaseID, NULL, XMLRPCCompletePurchase_Callback, NULL);
	if (pTrans)
	{
		pResponse->resultString = ACCOUNT_HTTP_SUCCESS;
		pResponse->transID = pTrans->webUID;
	}
	else
	{
		pResponse->resultString = ACCOUNT_HTTP_INVALID_PURCHASE_ID;
	}

	LogXmlrpcf("Response: CompletePurchase(): %s", NULL_TO_EMPTY(pResponse->resultString));
	return pResponse;
}


/************************************************************************/
/* Check the answers to the secret questions                            */
/************************************************************************/

// Array of strings for questions or answers.
AUTO_STRUCT;
typedef struct XMLAnswers
{
	STRING_EARRAY answers;		AST(ESTRING)
} XMLAnswers;

// Result of checking answers.
AUTO_STRUCT;
typedef struct XMLCheckSecretAnswersResponse
{
	char *result;					AST(UNOWNED)
	INT_EARRAY piCorrect;							// piCorrect[i] is true for each answer that was correct, and false otherwise.
} XMLCheckSecretAnswersResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("CheckSecretAnswers");
XMLCheckSecretAnswersResponse * XMLRPCCheckSecretAnswers(const char *accountName, XMLAnswers *checkAnswers)
{
	XMLCheckSecretAnswersResponse *xmlResponse = StructCreate(parse_XMLCheckSecretAnswersResponse);
	AccountInfo *account;
	bool success;

	// Log request.
	LogXmlrpcf("Request: CheckSecretAnswers(%s)", accountName);

	// Look up account by name or email.
	account = findAccountByName(accountName);
	account = account ? account : findAccountByEmail(accountName);
	xmlResponse->piCorrect = 0;
	if (!account)
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
		return xmlResponse;
	}

	// Check answers.
	success = checkSecretAnswers(account, &checkAnswers->answers, &xmlResponse->piCorrect);

	// Return result of checking.
	xmlResponse->result = success ? ACCOUNT_HTTP_SECRET_CORRECT_ANSWERS : ACCOUNT_HTTP_SECRET_INCORRECT_ANSWERS;
	LogXmlrpcf("Response: CheckSecretAnswers(): %s", xmlResponse->result);
	return xmlResponse;
}

/************************************************************************/
/* Get a list of real money transactions made by an account.            */
/************************************************************************/

// Copy account transaction information into web transaction.
static void XMLGetTransactionsComplete(SA_PARAM_NN_VALID BillingTransaction *pTrans, bool bKnownAccount, SA_PARAM_NN_VALID AccountTransactionInfo **ppInfo,
									   SA_PARAM_OP_VALID void *userData)
{
	devassert(!userData);

	PERFINFO_AUTO_START_FUNC();
	pTrans->webResponseTransactionInfo = ppInfo;
	if (eaSize(&ppInfo))
		pTrans->resultString = estrDup(ACCOUNT_HTTP_SUCCESS);
	else
	{
		btFail(pTrans, "Account not found");
		pTrans->resultString = estrDup(bKnownAccount ? ACCOUNT_HTTP_NOT_FOUND : ACCOUNT_HTTP_VINDICIA_UNKNOWN);
	}
	PERFINFO_AUTO_STOP();
}

// If successful, an array of VIDs, one for each real money transaction associated with the account.
AUTO_STRUCT;
typedef struct XMLGetTransactionsResponse
{
	char *result;				AST(UNOWNED)
	char *transID;				AST(UNOWNED)
} XMLGetTransactionsResponse;

// Return a list of real money transactions for which we are able to issue refunds.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("GetTransactions");
SA_RET_NN_VALID XMLGetTransactionsResponse * XMLRPCGetTransactions(const char *accountName)
{
	XMLGetTransactionsResponse *xmlResponse = StructCreate(parse_XMLGetTransactionsResponse);
	AccountInfo *account;
	BillingTransaction *pTrans;

	// Log request.
	LogXmlrpcf("Request: GetTransactions(%s)", accountName);

	// Look up account by name or email.
	account = findAccountByName(accountName);
	account = account ? account : findAccountByEmail(accountName);
	if (!account)
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
		return xmlResponse;
	}

	// Start account fetch transaction.
	pTrans = btFetchAccountTransactions(account->uID, NULL, XMLGetTransactionsComplete, NULL);
	xmlResponse->transID = pTrans->webUID;
	LogXmlrpcf("Response: GetTransactions(): %s", NULL_TO_EMPTY(xmlResponse->transID));
	return xmlResponse;
}


/************************************************************************/
/* Request transactions changed in a date range                         */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCTransactionFetchDeltaRequest
{
	U32 startSS2000;
	U32 endSS2000;
	TransactionFilterFlags filters;
} XMLRPCTransactionFetchDeltaRequest;

AUTO_STRUCT;
typedef struct XMLRPCTransactionFetchDeltaResponse
{
	char *result;	AST(UNOWNED)
	char *transID;	AST(UNOWNED)
} XMLRPCTransactionFetchDeltaResponse;

static void XMLRPCTransactionFetchDeltaComplete(SA_PARAM_NN_VALID BillingTransaction *pTrans, bool bSuccess, SA_PARAM_NN_VALID AccountTransactionInfo **ppInfo,
												SA_PARAM_OP_VALID void *userData)
{
	devassert(!userData);

	pTrans->webResponseTransactionInfo = ppInfo;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("TransactionFetchDelta");
SA_RET_NN_VALID XMLRPCTransactionFetchDeltaResponse * XMLRPCTransactionFetchDelta(SA_PARAM_NN_VALID XMLRPCTransactionFetchDeltaRequest *pRequest)
{
	XMLRPCTransactionFetchDeltaResponse *pResponse = StructCreate(parse_XMLRPCTransactionFetchDeltaResponse);
	BillingTransaction *pTrans = NULL;

	if (!pRequest->endSS2000 || !pRequest->startSS2000)
	{
		pResponse->result = "Invalid time given.";
		return pResponse;
	}

	LogXmlrpcf("Request: TransactionFetchDelta(%u, %u)", pRequest->startSS2000, pRequest->endSS2000);

	pTrans = btFetchChangedTransactionsSince(pRequest->startSS2000, pRequest->endSS2000, pRequest->filters, NULL, XMLRPCTransactionFetchDeltaComplete, NULL);
	pResponse->transID = pTrans->webUID;

	LogXmlrpcf("Response: TransactionFetchDelta(): %s", NULL_TO_EMPTY(pResponse->transID));

	return pResponse;
}


/************************************************************************/
/* Refund a real money transaction.                                     */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRefundRequest
{
	// Required parameters.
	const char *accountName;		// Account name to issue refund to
	const char *transaction;		// Transaction VID or MTID to refund
	bool refundWithVindicia;		// If not true, do not refund; only record that a refund has been given.
	const char *amount;				// Amount to refund
	bool merchantInitiated;			// True if this refund request was initiated by the merchant, false if by the customer.

	// Optional parameters.
	const char *optionalSubVid;		// If present, mark the refund as associated with this subscription VID.
	bool optionalSubInstant;		// If is present, subscription is disentitled immediately, rather than at the end of the period.
} XMLRefundRequest;

// Status of the refund request
AUTO_STRUCT;
typedef struct XMLRefundResponse
{
	char *result;				AST(UNOWNED)
	char *transID;				AST(UNOWNED)
} XMLRefundResponse;

// Return result of refund request.
static void XMLRPCRefundComplete(SA_PARAM_NN_VALID BillingTransaction *pTrans, bool bSuccess, SA_PARAM_OP_STR const char *pReason,
								 SA_PARAM_OP_STR const char *pAmount, SA_PARAM_OP_STR const char *pCurrency, SA_PARAM_OP_VALID void *userData)
{
	devassert(!userData);
	PERFINFO_AUTO_START_FUNC();
	if (bSuccess)
	{
		pTrans->result = BTR_SUCCESS;
		pTrans->resultString = estrDup("Refund successful");
		pTrans->webResponseRefundAmount = pAmount ? btStrdup(pTrans, pAmount) : NULL;
		pTrans->webResponseRefundCurrency = pCurrency ? btStrdup(pTrans, pCurrency) : NULL;
	}
	else
		btFail(pTrans, pReason);
	PERFINFO_AUTO_STOP();
}

// Refund a transaction for a particular account.
// If amount is specified, a partial refund of this amount will be issued.
// If bRefundWithVindicia is false, the transaction will not actually be refunded, but only recorded as refunded with Vindicia.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("Refund");
SA_RET_NN_VALID XMLRefundResponse * XMLRPCRefund(XMLRefundRequest *pRequest)
{
	XMLRefundResponse *xmlResponse = StructCreate(parse_XMLRefundResponse);
	AccountInfo *account;
	BillingTransaction *pTrans;

	// Log request.
	LogXmlrpcf("Request: Refund(%s, %s, %d, %s, %s)", NULL_TO_EMPTY(pRequest->accountName), NULL_TO_EMPTY(pRequest->transaction),
		(int)pRequest->refundWithVindicia, NULL_TO_EMPTY(pRequest->amount), NULL_TO_EMPTY(pRequest->optionalSubVid));

	// Look up account by name or email.
	account = findAccountByName(pRequest->accountName);
	account = account ? account : findAccountByEmail(pRequest->accountName);
	if (!account)
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
		return xmlResponse;
	}

	// Verify transaction argument.
	if (!pRequest->transaction || !*pRequest->transaction || !pRequest->amount || !*pRequest->amount)
	{
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
		return xmlResponse;
	}

	// Refuse to refund accounts which are not billing-enabled.
	if (!account->bBillingEnabled)
	{
		xmlResponse->result = ACCOUNT_HTTP_VINDICIA_UNKNOWN;
		return xmlResponse;
	}

	// Start refund transaction.
	pTrans = btRefund(account, pRequest->transaction, pRequest->amount, pRequest->refundWithVindicia, pRequest->merchantInitiated,
	pRequest->optionalSubVid,
		pRequest->optionalSubInstant, NULL, XMLRPCRefundComplete, NULL);
	devassert(pTrans);
	xmlResponse->transID = pTrans->webUID;
	LogXmlrpcf("Response: Refund(): %s, %s", NULL_TO_EMPTY(xmlResponse->result), NULL_TO_EMPTY(xmlResponse->transID));
	return xmlResponse;
}

/************************************************************************/
/* Refund a Steam Wallet transaction.                                   */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLSteamRefundRequest
{
	// Required parameters.
	const char *accountName;		// Account name to issue refund to
	const char *orderid;			// Steam order ID to refund
	const char *source;				// Source of the request
} XMLSteamRefundRequest;

// Status of the refund request
AUTO_STRUCT;
typedef struct XMLSteamRefundResponse
{
	char *result;				AST(UNOWNED)
	char *transID;				AST(UNOWNED)
} XMLSteamRefundResponse;

// Return result of refund request.
static void XMLRPCSteamRefundComplete(bool bSuccess, SA_PARAM_NN_VALID BillingTransaction *pTrans, U64 uOrderID, U64 uTransactionID)
{
	AccountInfo *account = findAccountByID(pTrans->uDebugAccountId);

	PERFINFO_AUTO_START_FUNC();
	btCompleteSteamTransaction(pTrans, bSuccess);

	if (devassert(account))
	{
		accountLog(account, "Refund of Steam Wallet transaction %s (order: %"FORM_LL"u)", bSuccess? "succeeded" : "failed", uOrderID);
	}
	PERFINFO_AUTO_STOP();
}

// Refund a transaction for a particular account.
// If amount is specified, a partial refund of this amount will be issued.
// If bRefundWithVindicia is false, the transaction will not actually be refunded, but only recorded as refunded with Vindicia.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("SteamRefund");
SA_RET_NN_VALID XMLSteamRefundResponse * XMLRPCSteamRefund(XMLSteamRefundRequest *pRequest)
{
	XMLSteamRefundResponse *xmlResponse = StructCreate(parse_XMLSteamRefundResponse);
	AccountInfo *account;
	BillingTransaction *pTrans;
	const char *pSource;
	U64 uOrderID = 0;

	// Log request.
	LogXmlrpcf("Request: SteamRefund(%s, %s, %s)", NULL_TO_EMPTY(pRequest->accountName), NULL_TO_EMPTY(pRequest->orderid),
		NULL_TO_EMPTY(pRequest->source));

	// Look up account by name or email.
	account = findAccountByName(pRequest->accountName);
	account = account ? account : findAccountByEmail(pRequest->accountName);
	if (!account)
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
		return xmlResponse;
	}

	// Verify transaction argument.
	if (!pRequest->orderid || !*pRequest->orderid)
	{
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
		return xmlResponse;
	}

	pSource = AccountTransactionGetOrderIDSource(pRequest->orderid);

	if (!pSource)
	{
		pSource = pRequest->source;
	}

	if (!pSource || !*pSource)
	{
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
		return xmlResponse;
	}

	uOrderID = atoui64(pRequest->orderid);

	if (!uOrderID)
	{
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
		return xmlResponse;
	}

	// Start refund transaction.
	pTrans = btCreateBlank(true);
	devassert(pTrans);
	btMarkSteamTransaction(pTrans);
	pTrans->uDebugAccountId = account->uID;
	RefundSteamTransaction(account->uID, uOrderID, pSource, XMLRPCSteamRefundComplete, pTrans);

	xmlResponse->result = ACCOUNT_HTTP_SUCCESS;
	xmlResponse->transID = pTrans->webUID;
	LogXmlrpcf("Response: Refund(): %s, %s", NULL_TO_EMPTY(xmlResponse->result), NULL_TO_EMPTY(xmlResponse->transID));
	return xmlResponse;
}

/************************************************************************/
/* Get the purchase log                                                 */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLGetPurchaseLogRequest
{
	U32 uSinceSS2000;
	U64 uMaxResponses;
	U32 uAccountID;
} XMLGetPurchaseLogRequest;

// Result of checking answers.
AUTO_STRUCT;
typedef struct XMLGetPurchaseLogResponse
{
	char *result;							AST(UNOWNED)
	U32 uSinceSS2000;
	U64 uMaxResponses;
	EARRAY_OF(PurchaseLog) log;
} XMLGetPurchaseLogResponse;

#define ACCOUNT_XMLRPC_GETLOG_MAX 100

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("GetPurchaseLogEx");
XMLGetPurchaseLogResponse * XMLRPCGetPurchaseLogEx(SA_PARAM_NN_VALID const XMLGetPurchaseLogRequest * pRequest)
{
	XMLGetPurchaseLogResponse *pResponse = StructCreate(parse_XMLGetPurchaseLogResponse);

	// Log request.
	LogXmlrpcf("Request: GetPurchaseLogEx()");

	if (!pRequest->uAccountID)
	{
		pResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	}
	else
	{
		// Limit the number of responses unless the caller has explicitly asked for a certain maximum.
		if (pRequest->uMaxResponses)
		{
			pResponse->uMaxResponses = pRequest->uMaxResponses;
		}
		else
		{
			pResponse->uMaxResponses = ACCOUNT_XMLRPC_GETLOG_MAX;
		}

		pResponse->result = ACCOUNT_HTTP_SUCCESS;
		pResponse->uSinceSS2000 = pRequest->uSinceSS2000;
		pResponse->log = AccountTransactionGetPurchaseLog(pRequest->uSinceSS2000, pResponse->uMaxResponses, pRequest->uAccountID);
	}

	// Return result.
	LogXmlrpcf("Response: GetPurchaseLogEx()");
	return pResponse;
}


AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("GetPurchaseLog");
XMLGetPurchaseLogResponse * XMLRPCGetPurchaseLog(U32 uSinceSS2000, U64 uMaxResponses)
{
	XMLGetPurchaseLogRequest request = {0};
	XMLGetPurchaseLogResponse * pResponse = NULL;

	pResponse = StructCreate(parse_XMLGetPurchaseLogResponse);
	pResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
/*	THIS CALL IS DEPRECATED -- TODO: REMOVE
	StructInit(parse_XMLGetPurchaseLogRequest, &request);
	request.uAccountID = 0;
	request.uMaxResponses = uMaxResponses;
	request.uSinceSS2000 = uSinceSS2000;
	pResponse = XMLRPCGetPurchaseLogEx(&request);
	StructDeInit(parse_XMLGetPurchaseLogRequest, &request);
*/
	return pResponse;
}

/************************************************************************/
/* Get the transaction log                                              */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLGetTransactionLogRequest
{
	U32 uSinceSS2000;
	U64 uMaxResponses;
	U32 uAccountID;
} XMLGetTransactionLogRequest;

// Result of checking answers.
AUTO_STRUCT;
typedef struct XMLGetTransactionLogResponse
{
	char *result;							AST(UNOWNED)
	U32 uSinceSS2000;
	U64 uMaxResponses;
	EARRAY_OF(TransactionLogContainer) log;	AST(UNOWNED)
} XMLGetTransactionLogResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("GetTransactionLogEx");
XMLGetTransactionLogResponse * XMLRPCGetTransactionLogEx(SA_PARAM_NN_VALID const XMLGetTransactionLogRequest * pRequest)
{
	XMLGetTransactionLogResponse *pResponse = StructCreate(parse_XMLGetTransactionLogResponse);

	// Log request.
	LogXmlrpcf("Request: GetTransactionLogEx()");

	if (!pRequest->uAccountID)
	{
		pResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	}
	else
	{
		// Limit the number of responses unless the caller has explicitly asked for a certain maximum.
		if (pRequest->uMaxResponses)
		{
			pResponse->uMaxResponses = pRequest->uMaxResponses;
		}
		else
		{
			pResponse->uMaxResponses = ACCOUNT_XMLRPC_GETLOG_MAX;
		}

		pResponse->result = ACCOUNT_HTTP_SUCCESS;
		pResponse->uSinceSS2000 = pRequest->uSinceSS2000;
		pResponse->log = AccountTransactionGetLog(pRequest->uSinceSS2000, pResponse->uMaxResponses, pRequest->uAccountID);
	}

	// Return result.
	LogXmlrpcf("Response: GetTransactionLogEx()");
	return pResponse;
}

/************************************************************************/
/* Remove an internal sub                                               */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRemoveInternalSubRequest
{
	char *user;
	char *internalSubName;
} XMLRemoveInternalSubRequest;

AUTO_STRUCT;
typedef struct XMLRemoveInternalSubResponse
{
	char *result; AST(UNOWNED)
} XMLRemoveInternalSubResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("RemoveInternalSub");
XMLRemoveInternalSubResponse * XMLRPCRemoveInternalSub(SA_PARAM_NN_VALID const XMLRemoveInternalSubRequest *pRequest)
{
	XMLRemoveInternalSubResponse *pResponse = StructCreate(parse_XMLRemoveInternalSubResponse);
	const AccountInfo *pAccountInfo = findAccountByNameOrEmail(pRequest->user);

	LogXmlrpcf("Request: RemoveInternalSub(%s, %s)", pRequest->user, pRequest->internalSubName);

	if (!pAccountInfo)
		pResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	else if (!pRequest->internalSubName || !*pRequest->internalSubName)
		pResponse->result = ACCOUNT_HTTP_INVALID_SUBSCRIPTION;
	else if (!internalSubRemove(pAccountInfo->uID, pRequest->internalSubName))
		pResponse->result = ACCOUNT_HTTP_INVALID_SUBSCRIPTION;
	else
		pResponse->result = ACCOUNT_HTTP_SUCCESS;

	LogXmlrpcf("Response: RemoveInternalSub(): %s", pResponse->result);
	return pResponse;
}


/************************************************************************/
/* Super-duper-awesome SubCreate                                        */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLSuperSubCreateRequest
{
	char *user;
	char *subscription;
	STRING_EARRAY activationKeys; AST(ESTRING)
	PaymentMethod *paymentMethod;
	char *currency;
	char *ip;
	char *referrer;
	char *bankName;
} XMLSuperSubCreateRequest;

AUTO_STRUCT;
typedef struct XMLSuperSubCreateResponse
{
	char *result;		AST(ESTRING)
	const char *transID;	AST(UNOWNED)
} XMLSuperSubCreateResponse;

AUTO_STRUCT;
typedef struct SuperSubCreateInformation
{
	XMLSuperSubCreateRequest *pRequest;
	AccountInfo *pAccount;						NO_AST
	const SubscriptionContainer *pSubscription; NO_AST
	U32 uExtraDays;
	EARRAY_OF(ActivateProductLock) eaLocks;		NO_AST
} SuperSubCreateInformation;

static void XMLRPCSuperSubCreate_Cleanup(SA_PARAM_NN_VALID SuperSubCreateInformation *pSubInfo, bool commit)
{
	PERFINFO_AUTO_START_FUNC();
	if (pSubInfo->eaLocks)
	{
		EARRAY_CONST_FOREACH_BEGIN(pSubInfo->eaLocks, i, size);
			ProductKeyResult ret;

			if (commit)
			{
				ret = activateProductKeyCommit(pSubInfo->pAccount, pSubInfo->pRequest->activationKeys[i], pSubInfo->eaLocks[i]);
			}
			else
			{
				ret = activateProductKeyRollback(pSubInfo->pAccount, pSubInfo->pRequest->activationKeys[i], pSubInfo->eaLocks[i]);
			}

			if (ret != PK_Success)
			{
				AssertOrAlert("ACCOUNTSERVER_ACTIVATE_AFTER_SUB", "Subscription created but product key could not be activated! Please investigate the key %s and the account %s to find out why.",
					pSubInfo->pRequest->activationKeys[i], pSubInfo->pAccount->accountName);
			}

		EARRAY_FOREACH_END;

		eaDestroy(&pSubInfo->eaLocks);
	}
	StructDestroy(parse_SuperSubCreateInformation, pSubInfo);
	PERFINFO_AUTO_STOP();
}


static void XMLRPCSuperSubCreate_Callback(SubCreateResult eResult,
										  SA_PARAM_OP_VALID BillingTransaction *pTrans,
										  SA_PARAM_NN_VALID SuperSubCreateInformation *pSubInfo)
{
	PERFINFO_AUTO_START_FUNC();
	if (eResult != SUBCREATE_RESULT_SUCCESS)
	{
		XMLRPCSuperSubCreate_Cleanup(pSubInfo, false);
		PERFINFO_AUTO_STOP();
		return;
	}

	XMLRPCSuperSubCreate_Cleanup(pSubInfo, true);
	PERFINFO_AUTO_STOP();
}

static void XMLRPCSuperSubCreate_UpdatePMCallback(UpdatePMResult eResult,
												  SA_PARAM_OP_VALID BillingTransaction *pTrans,
												  SA_PARAM_OP_VALID const CachedPaymentMethod *pCachedPaymentMethod,
												  SA_PARAM_NN_VALID SuperSubCreateInformation *pSubInfo)
{
	PERFINFO_AUTO_START_FUNC();
	if (eResult == UPMR_Success && pTrans && pCachedPaymentMethod)
	{
		pSubInfo->pRequest->paymentMethod->VID = estrDup(pCachedPaymentMethod->VID);

		pTrans = SubCreate(pSubInfo->pAccount,
			pSubInfo->pSubscription, pSubInfo->uExtraDays, pTrans, pSubInfo->pRequest->paymentMethod,
			pSubInfo->pRequest->currency, pSubInfo->pRequest->ip, pSubInfo->pRequest->bankName,
			XMLRPCSuperSubCreate_Callback, pSubInfo);
		PERFINFO_AUTO_STOP();
		return;
	}
	
	XMLRPCSuperSubCreate_Cleanup(pSubInfo, false);
	PERFINFO_AUTO_STOP();
}

static void XMLRPCSuperSubCreate_Error(
	SA_PARAM_OP_VALID SuperSubCreateInformation * pSubInfo,
	SA_PARAM_NN_VALID XMLSuperSubCreateResponse * pResponse,
	SA_PARAM_NN_STR const char * pReturnCode)
{
	PERFINFO_AUTO_START_FUNC();
	pResponse->result = estrDup(pReturnCode);
	if (pSubInfo) XMLRPCSuperSubCreate_Cleanup(pSubInfo, false);
	LogXmlrpcf("Response: SuperSubCreate(): %s", pResponse->result);
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("SuperSubCreate");
XMLSuperSubCreateResponse * XMLRPCSuperSubCreate(SA_PARAM_NN_VALID XMLSuperSubCreateRequest *pRequest)
{
	XMLSuperSubCreateResponse *pResponse = StructCreate(parse_XMLSuperSubCreateResponse);
	AccountInfo *pAccount = findAccountByNameOrEmail(pRequest->user);
	const SubscriptionContainer *pSubscription = findSubscriptionByName(pRequest->subscription);
	BillingTransaction *pTrans = NULL;
	unsigned int uExtraDays = 0;
	unsigned int uNumKeysBefore = eaSize(&pRequest->activationKeys);
	SuperSubCreateInformation *pSubInfo = NULL;

	LogXmlrpcf("Request: SuperSubCreate(%s, %s)", pRequest->user, pRequest->subscription);

	if (!pAccount)
	{
		XMLRPCSuperSubCreate_Error(pSubInfo, pResponse, ACCOUNT_HTTP_USER_NOT_FOUND);
		return pResponse;
	}

	if (pRequest->paymentMethod)
	{
		const char *pErrorDetails = NULL;
		const CachedPaymentMethod *pCachedPaymentMethod = NULL;
		PaymentMethodProblem eProblem = PMP_Invalid;

		if (pRequest->paymentMethod->VID && *pRequest->paymentMethod->VID)
		{
			pCachedPaymentMethod = getCachedPaymentMethod(pAccount, pRequest->paymentMethod->VID);

			if (pCachedPaymentMethod && !pRequest->paymentMethod->country)
			{
				pRequest->paymentMethod->country = estrDup(pCachedPaymentMethod->billingAddress.country);
			}
		}

		eProblem = paymentMethodValid(pAccount->forbiddenPaymentMethodTypes, pRequest->paymentMethod, pCachedPaymentMethod, pRequest->bankName, &pErrorDetails);

		if (eProblem != PMP_None)
		{
			if (pErrorDetails)
			{
				estrPrintf(&pResponse->result, "%s: %s",
					StaticDefineIntRevLookupNonNull(PaymentMethodProblemEnum, eProblem), pErrorDetails);
			}
			else
			{
				estrPrintf(&pResponse->result, "%s",
					StaticDefineIntRevLookupNonNull(PaymentMethodProblemEnum, eProblem));
			}

			LogXmlrpcf("Response: SuperSubCreate(): %s", pResponse->result);
			return pResponse;
		}
	}

	accountLog(pAccount, "Attempting SuperSubCreate");

	if (!pSubscription)
	{
		accountLog(pAccount, "Failed in SuperSubCreate because the subscription plan does not exist");
		XMLRPCSuperSubCreate_Error(pSubInfo, pResponse, ACCOUNT_HTTP_INVALID_SUBSCRIPTION);
		return pResponse;
	}

	if (accountExpectsSub(pAccount, pSubscription->pInternalName) > 0)
	{
		btUpdateActiveSubscriptions(pAccount->uID, NULL, NULL, NULL);

		accountLog(pAccount, "Failed in SuperSubCreate because the account expected to already have a subscription with the same internal name but doesn't");
		XMLRPCSuperSubCreate_Error(pSubInfo, pResponse, ACCOUNT_HTTP_ALREADY_EXPECTING);
		return pResponse;
	}

	// Make sure there are no duplicate keys
	eaRemoveDuplicateEStrings(&pRequest->activationKeys);
	if (uNumKeysBefore != eaUSize(&pRequest->activationKeys))
	{
		accountLog(pAccount, "Failed in SuperSubCreate because duplicate activation keys were sent");
		AssertOrAlert("ACCOUNTSERVER_DUPLICATE_KEYS", "The web site sent the same activation key more than once as part of a single request to SuperSubCreate.  The web site should be fixed to prevent this and inform the user of their mistake.");
		XMLRPCSuperSubCreate_Error(pSubInfo, pResponse, ACCOUNT_HTTP_INVALID_ACTIVATION_KEY);
		return pResponse;
	}

	pSubInfo = StructCreate(parse_SuperSubCreateInformation);
	pSubInfo->pAccount = pAccount;
	pSubInfo->pSubscription = pSubscription;
	pSubInfo->pRequest = StructClone(parse_XMLSuperSubCreateRequest, pRequest);

	// Make sure the product keys are valid
	EARRAY_CONST_FOREACH_BEGIN(pRequest->activationKeys, iCurKey, size);
	{
		ProductKey key = {0};
		ActivateProductLock *pLock = NULL;
		int iDays;
		ProductKeyResult eResult = PK_Invalid;
		U32 accountId = 0;
		const char * pProductKey = pRequest->activationKeys[iCurKey];

		if (pProductKey && *pProductKey && productKeyIsValid(&key, pProductKey))
		{
			accountId = getActivatedAccountId(&key);
		}
		else
		{
			accountLog(pAccount, "Failed in SuperSubCreate because the key %s is invalid", pProductKey);
			XMLRPCSuperSubCreate_Error(pSubInfo, pResponse, ACCOUNT_HTTP_INVALID_ACTIVATION_KEY);
			return pResponse;
		}

		if (accountId || productKeyIsLocked(&key))
		{
			accountLog(pAccount, "Failed in SuperSubCreate because the key %s was used or locked", pProductKey);
			XMLRPCSuperSubCreate_Error(pSubInfo, pResponse, ACCOUNT_HTTP_INVALID_ACTIVATION_KEY);
			return pResponse;
		}

		iDays = productKeyDaysGranted(pProductKey);
		if (iDays < 0)
		{
			accountLog(pAccount, "Failed in SuperSubCreate because the key %s gives an invalid number of days", pProductKey);
			XMLRPCSuperSubCreate_Error(pSubInfo, pResponse, ACCOUNT_HTTP_INVALID_ACTIVATION_KEY);
			return pResponse;
		}

		eResult = activateProductKeyLock(pAccount, pProductKey, pRequest->referrer, &pLock);
		if (eResult != PK_Pending)
		{
			accountLog(pAccount, "Failed in SuperSubCreate because the key %s could not be locked", pProductKey);
			XMLRPCSuperSubCreate_Error(pSubInfo, pResponse, StaticDefineIntRevLookupNonNull(ProductKeyResultEnum, eResult));
			return pResponse;
		}

		eaPush(&pSubInfo->eaLocks, pLock);

		uExtraDays += iDays;
	}
	EARRAY_FOREACH_END;

	accountLog(pAccount, "SuperSubCreate calculated %d free days", uExtraDays);

	pSubInfo->uExtraDays = uExtraDays;

	pTrans = btCreateBlank(true);
	if (pRequest->paymentMethod && !ShouldDoUpdatePaymentMethod(pRequest->paymentMethod))
	{
		PaymentMethodType ePMType = paymentMethodType(pRequest->paymentMethod);

		if (ePMType == PMT_PayPal)
			accountLog(pAccount, "SuperSubCreate was given a PayPal payment method");
		else if (pRequest->paymentMethod->VID && *pRequest->paymentMethod->VID)
			accountLog(pAccount, "SuperSubCreate was given an existing payment method");
		else
			accountLog(pAccount, "SuperSubCreate is skipping the update payment method flow");

		pTrans = SubCreate(pAccount,
			pSubscription, uExtraDays, pTrans, pRequest->paymentMethod, pRequest->currency, pRequest->ip, pRequest->bankName,
			XMLRPCSuperSubCreate_Callback, pSubInfo);
	}
	else if (!pRequest->paymentMethod)
	{
		accountLog(pAccount, "SuperSubCreate was not given a payment method");
		pTrans = SubCreate(pAccount,
			pSubscription, uExtraDays, pTrans, NULL, pRequest->currency, pRequest->ip, pRequest->bankName,
			XMLRPCSuperSubCreate_Callback, pSubInfo);
	}
	else
	{
		accountLog(pAccount, "SuperSubCreate was given a new payment method");
		UpdatePaymentMethod(pAccount, pRequest->paymentMethod, pRequest->ip, pRequest->bankName, pTrans, XMLRPCSuperSubCreate_UpdatePMCallback, pSubInfo);
	}

	if (pTrans)
		pResponse->transID = pTrans->webUID;

	LogXmlrpcf("Response: SuperSubCreate(): %s", NULL_TO_EMPTY(pResponse->result));
	return pResponse;
}


/************************************************************************/
/* Cancel an uncaptured transaction                                     */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLCancelTransactionParameters {
	const char *pAccount;
	const char *pTransaction;
} XMLCancelTransactionParameters;

AUTO_STRUCT;
typedef struct XMLCancelTransactionResponse
{
	char *result;				AST(UNOWNED)
	char *transID;				AST(UNOWNED)
} XMLCancelTransactionResponse;

// Return result of cancel request.
static void XMLRPCCancelComplete(SA_PARAM_NN_VALID BillingTransaction *pTrans, bool bSuccess, SA_PARAM_OP_STR const char *pReason, SA_PARAM_OP_VALID void *userData)
{
	devassert(!userData);
	PERFINFO_AUTO_START_FUNC();
	if (bSuccess)
		pTrans->result = BTR_SUCCESS;
	else
		btFail(pTrans, pReason);
	PERFINFO_AUTO_STOP();
}

// Cancel a transaction for a particular account.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("CancelTransaction");
SA_RET_NN_VALID XMLCancelTransactionResponse *XMLRPCCancelTransaction(XMLCancelTransactionParameters *pParameters)
{
	XMLCancelTransactionResponse *xmlResponse = StructCreate(parse_XMLCancelTransactionResponse);
	AccountInfo *account;
	BillingTransaction *pTrans;

	// Log request.
	LogXmlrpcf("Request: CancelTransaction(%s, %s)", pParameters->pAccount, pParameters->pTransaction);

	// Verify transaction argument.
	if (!pParameters || !pParameters->pTransaction || !*pParameters->pTransaction)
	{
		devassert(pParameters);
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
		return xmlResponse;
	}

	// Look up account by name or email.
	account = findAccountByName(pParameters->pAccount);
	account = account ? account : findAccountByEmail(pParameters->pAccount);
	if (!account)
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
		return xmlResponse;
	}

	// Refuse to cancel accounts which are not billing-enabled.
	if (!account->bBillingEnabled)
	{
		xmlResponse->result = ACCOUNT_HTTP_VINDICIA_UNKNOWN;
		return xmlResponse;
	}

	// Start refund transaction.
	pTrans = btCancelTransaction(account, pParameters->pTransaction, NULL, XMLRPCCancelComplete, NULL);
	devassert(pTrans);
	xmlResponse->result;
	xmlResponse->transID = pTrans->webUID;
	LogXmlrpcf("Response: CancelTransaction(): %s", xmlResponse->transID);
	return xmlResponse;
}


/************************************************************************/
/* Do a pending action on an account                                    */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLDoPendingActionRequest {
	const char *pAccount;
	U32 uActionID;
} XMLDoPendingActionRequest;

AUTO_STRUCT;
typedef struct XMLDoPendingActionResponse
{
	char *result;				AST(UNOWNED)
	char *transID;				AST(UNOWNED)
} XMLDoPendingActionResponse;

// Do a pending action
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("DoPendingAction");
SA_RET_NN_VALID XMLDoPendingActionResponse *XMLRPCDoPendingAction(XMLDoPendingActionRequest *pRequest)
{
	XMLDoPendingActionResponse *xmlResponse = StructCreate(parse_XMLDoPendingActionResponse);
	const AccountInfo *account;
	BillingTransaction *pTrans = NULL;

	if (!pRequest) 
	{
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
		return xmlResponse;
	}

	// Log request.
	LogXmlrpcf("Request: DoPendingAction(%s, %d)", pRequest->pAccount, pRequest->uActionID);

	account = findAccountByName(pRequest->pAccount);

	if (account)
	{
		AccountPendingActionResult eResult = accountDoPendingAction(&pTrans, account, pRequest->uActionID);

		switch (eResult)
		{
		xcase APAR_FAILED:
		default:
			xmlResponse->result = "Failed to do pending action.";
		xcase APAR_NOT_FOUND:
			xmlResponse->result = "Pending action ID not found.";
		xcase APAR_SUCCESS:
			xmlResponse->result = ACCOUNT_HTTP_SUCCESS;
		}

		if (pTrans)
			xmlResponse->transID = pTrans->webUID;
	}
	else
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	LogXmlrpcf("Response: DoPendingAction(): %s", NULL_TO_EMPTY(xmlResponse->result));
	return xmlResponse;
}


/************************************************************************/
/* Mark a subscription as refunded                                      */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLMarkSubRefundedRequest {
	const char *pAccount;
	const char *pSubVID;
} XMLMarkSubRefundedRequest;

AUTO_STRUCT;
typedef struct XMLMarkSubRefundedResponse
{
	char *result;				AST(UNOWNED)
} XMLMarkSubRefundedResponse;

// MarkSubRefunded
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("MarkSubRefunded");
SA_RET_NN_VALID XMLMarkSubRefundedResponse *XMLRPCMarkSubRefunded(XMLMarkSubRefundedRequest *pRequest)
{
	XMLMarkSubRefundedResponse *xmlResponse = StructCreate(parse_XMLMarkSubRefundedResponse);
	AccountInfo *account;

	if (!pRequest || !pRequest->pSubVID || !*pRequest->pSubVID)
	{
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
		return xmlResponse;
	}

	// Log request.
	LogXmlrpcf("Request: MarkSubRefunded(%s, %s)", pRequest->pAccount, pRequest->pSubVID);

	account = findAccountByName(pRequest->pAccount);

	if (account)
	{
		accountRecordSubscriptionRefund(account, pRequest->pSubVID);
		xmlResponse->result = ACCOUNT_HTTP_SUCCESS;
	}
	else
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	LogXmlrpcf("Response: MarkSubRefunded(): %s", NULL_TO_EMPTY(xmlResponse->result));
	return xmlResponse;
}


/************************************************************************/
/* Archive a subscription history                                       */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLArchiveSubHistoryRequest
{
	char *pAccountName;				AST(NAME(AccountName))
	char *pProductInternalName;		AST(NAME(ProductInternalName))
	char *pSubInternalName;			AST(NAME(SubInternalName))
	char *pSubVID;					AST(NAME(SubVID))
	U32 uStartTime;					AST(NAME(StartTime))
	U32 uEndTime;					AST(NAME(EndTime))
	SubscriptionTimeSource eSubTimeSource; AST(NAME(SubTimeSource))
	U32 uProblemFlags;				AST(NAME(ProblemFlags))
} XMLArchiveSubHistoryRequest;

AUTO_STRUCT;
typedef struct XMLArchiveSubHistoryResponse
{
	char *result; AST(UNOWNED)
} XMLArchiveSubHistoryResponse;

// ArchiveSubHistory
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("ArchiveSubHistory");
SA_RET_NN_VALID XMLArchiveSubHistoryResponse *XMLRPCArchiveSubHistory(XMLArchiveSubHistoryRequest *pRequest)
{
	XMLArchiveSubHistoryResponse *xmlResponse = StructCreate(parse_XMLArchiveSubHistoryResponse);
	AccountInfo *pAccount;

	if (!pRequest ||
		!pRequest->pAccountName || !*pRequest->pAccountName ||
		!pRequest->pProductInternalName || !*pRequest->pProductInternalName ||
		!pRequest->pSubInternalName || !*pRequest->pSubInternalName ||
		!pRequest->uStartTime ||
		!pRequest->uEndTime)
	{
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
		return xmlResponse;
	}

	// Log request.
	LogXmlrpcf("Request: ArchiveSubHistory(%s, %s, %s, %s, %d, %d, %s, %d)",
		pRequest->pAccountName,
		pRequest->pProductInternalName,
		pRequest->pSubInternalName,
		NULL_TO_EMPTY(pRequest->pSubVID),
		pRequest->uStartTime,
		pRequest->uEndTime,
		StaticDefineIntRevLookupNonNull(SubscriptionTimeSourceEnum, pRequest->eSubTimeSource),
		pRequest->uProblemFlags);

	pAccount = findAccountByName(pRequest->pAccountName);

	if (pAccount)
	{
		if (pRequest->uStartTime > pRequest->uEndTime)
		{
			U32 uTemp = pRequest->uStartTime;
			pRequest->uStartTime = pRequest->uEndTime;
			pRequest->uEndTime = uTemp;
		}

		accountArchiveSubscription(pAccount->uID,
			pRequest->pProductInternalName,
			pRequest->pSubInternalName,
			pRequest->pSubVID,
			pRequest->uStartTime,
			pRequest->uEndTime,
			pRequest->eSubTimeSource,
			SHER_ManualEntry,
			pRequest->uProblemFlags);

		xmlResponse->result = ACCOUNT_HTTP_SUCCESS;
	}
	else
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	LogXmlrpcf("Response: ArchiveSubHistory(): %s", NULL_TO_EMPTY(xmlResponse->result));
	return xmlResponse;
}


/************************************************************************/
/* Recalculate archived subscription history                            */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRecalculateArchivedSubHistoryRequest
{
	char *pAccountName;				AST(NAME(AccountName))
	char *pProductInternalName;		AST(NAME(ProductInternalName))
} XMLRecalculateArchivedSubHistoryRequest;

AUTO_STRUCT;
typedef struct XMLRecalculateArchivedSubHistoryResponse
{
	char *result; AST(UNOWNED)
} XMLRecalculateArchivedSubHistoryResponse;

// RecalculateArchivedSubHistory
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("RecalculateArchivedSubHistory");
SA_RET_NN_VALID XMLRecalculateArchivedSubHistoryResponse *XMLRPCRecalculateArchivedSubHistory(XMLRecalculateArchivedSubHistoryRequest *pRequest)
{
	XMLRecalculateArchivedSubHistoryResponse *xmlResponse = StructCreate(parse_XMLRecalculateArchivedSubHistoryResponse);
	AccountInfo *pAccount;

	if (!pRequest ||
		!pRequest->pAccountName || !*pRequest->pAccountName)
	{
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
		return xmlResponse;
	}

	// Log request.
	LogXmlrpcf("Request: RecalculateArchivedSubHistory(%s, %s)",
		pRequest->pAccountName,
		NULL_TO_EMPTY(pRequest->pProductInternalName));

	pAccount = findAccountByName(pRequest->pAccountName);

	if (pAccount)
	{
		if (pRequest->pProductInternalName && *pRequest->pProductInternalName)
		{
			accountRecalculateArchivedSubHistory(pAccount->uID,
				pRequest->pProductInternalName);
		}
		else
		{
			accountRecalculateAllArchivedSubHistory(pAccount->uID);
		}

		xmlResponse->result = ACCOUNT_HTTP_SUCCESS;
	}
	else
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	LogXmlrpcf("Response: RecalculateArchivedSubHistory(): %s", NULL_TO_EMPTY(xmlResponse->result));
	return xmlResponse;
}


/************************************************************************/
/* Enable/disable archived sub history                                  */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLEnableArchivedSubHistoryRequest
{
	char *pAccountName;				AST(NAME(AccountName))
	char *pProductInternalName;		AST(NAME(ProductInternalName))
	U32 uID;						AST(NAME(ID))
	bool bEnable;					AST(NAME(Enable))
} XMLEnableArchivedSubHistoryRequest;

AUTO_STRUCT;
typedef struct XMLEnableArchivedSubHistoryResponse
{
	char *result; AST(UNOWNED)
} XMLEnableArchivedSubHistoryResponse;

// EnableArchivedSubHistory
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("EnableArchivedSubHistory");
SA_RET_NN_VALID XMLEnableArchivedSubHistoryResponse *XMLRPCEnableArchivedSubHistory(XMLEnableArchivedSubHistoryRequest *pRequest)
{
	XMLEnableArchivedSubHistoryResponse *xmlResponse = StructCreate(parse_XMLEnableArchivedSubHistoryResponse);
	AccountInfo *pAccount;

	if (!pRequest ||
		!pRequest->pAccountName || !*pRequest->pAccountName ||
		!pRequest->pProductInternalName || !*pRequest->pProductInternalName)
	{
		xmlResponse->result = ACCOUNT_HTTP_NO_ARGS;
		return xmlResponse;
	}

	// Log request.
	LogXmlrpcf("Request: EnableArchivedSubHistory(%s, %s, %d, %d)",
		pRequest->pAccountName,
		pRequest->pProductInternalName,
		pRequest->uID,
		pRequest->bEnable);

	pAccount = findAccountByName(pRequest->pAccountName);

	if (pAccount)
	{
		int index = eaIndexedFindUsingString(&pAccount->ppSubscriptionHistory, pRequest->pProductInternalName);

		if (index >= 0 && pAccount->ppSubscriptionHistory[index])
		{
			index = eaIndexedFindUsingInt(&pAccount->ppSubscriptionHistory[index]->eaArchivedEntries, pRequest->uID);

			if (index >= 0)
			{
				accountEnableArchivedSubHistory(pAccount->uID,
					pRequest->pProductInternalName, pRequest->uID, pRequest->bEnable);

				xmlResponse->result = ACCOUNT_HTTP_SUCCESS;
			}
			else
			{
				xmlResponse->result = "Subscription history archive entry not found for the specified ID.";
			}
		}
		else
		{
			xmlResponse->result = "Subscription history archive not found for the specified product.";
		}
	}
	else
	{
		xmlResponse->result = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	LogXmlrpcf("Response: EnableArchivedSubHistory(): %s", NULL_TO_EMPTY(xmlResponse->result));
	return xmlResponse;
}


/************************************************************************/
/* Changed cached sub created time                                      */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLChangeSubCreatedTimeRequest
{
	char *pAccountName;		AST(NAME(AccountName))
	char *pSubVID;			AST(NAME(SubVID))
	U32 uNewCreatedTime;	AST(NAME(NewCreatedTime))
} XMLChangeSubCreatedTimeRequest;

AUTO_STRUCT;
typedef struct XMLChangeSubCreatedTimeResponse
{
	char *pResult;			AST(NAME(Result) UNOWNED)
	U32 uOldCreatedTime;	AST(NAME(OldCreatedTime))
} XMLChangeSubCreatedTimeResponse;

// ChangeSubCreatedTime
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("ChangeSubCreatedTime");
SA_RET_NN_VALID XMLChangeSubCreatedTimeResponse *XMLRPCChangeSubCreatedTime(XMLChangeSubCreatedTimeRequest *pRequest)
{
	XMLChangeSubCreatedTimeResponse *pResponse = StructCreate(parse_XMLChangeSubCreatedTimeResponse);
	AccountInfo *pAccount;

	if (!pRequest ||
		!pRequest->pAccountName ||
		!*pRequest->pAccountName ||
		!pRequest->pSubVID ||
		!*pRequest->pSubVID ||
		!pRequest->uNewCreatedTime)
	{
		pResponse->pResult = ACCOUNT_HTTP_NO_ARGS;
		return pResponse;
	}

	// Log request.
	LogXmlrpcf("Request: ChangeSubCreatedTime(%s, %s, %d)",
		pRequest->pAccountName,
		pRequest->pSubVID,
		pRequest->uNewCreatedTime);

	pAccount = findAccountByName(pRequest->pAccountName);

	if (pAccount)
	{
		const CachedAccountSubscription *pCachedSub = findAccountSubscriptionByVID(pAccount, pRequest->pSubVID);

		if (pCachedSub)
		{
			pResponse->uOldCreatedTime = pCachedSub->estimatedCreationTimeSS2000;

			accountUpdateCachedCreatedTime(pAccount->uID, pRequest->pSubVID, pRequest->uNewCreatedTime);

			pResponse->pResult = ACCOUNT_HTTP_SUCCESS;
		}
		else
		{
			pResponse->pResult = ACCOUNT_HTTP_INVALID_SUBSCRIPTION;
		}
	}
	else
	{
		pResponse->pResult = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	LogXmlrpcf("Response: ChangeSubCreatedTime(): %s", NULL_TO_EMPTY(pResponse->pResult));
	return pResponse;
}


/************************************************************************/
/* Mark a recruit as having been offered                                */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRecruitmentOfferedRequest
{
	char * pAccountName; AST(NAME(AccountName))
	char * pProductKey; AST(NAME(ProductKey))
} XMLRecruitmentOfferedRequest;

AUTO_STRUCT;
typedef struct XMLRecruitmentOfferedResponse
{
	char * pResult;		AST(NAME(Result) UNOWNED)
} XMLRecruitmentOfferedResponse;

// RecruitmentOffered
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("RecruitmentOffered");
SA_RET_NN_VALID XMLRecruitmentOfferedResponse *XMLRPCRecruitmentOffered(XMLRecruitmentOfferedRequest *pRequest)
{
	XMLRecruitmentOfferedResponse *pResponse = StructCreate(parse_XMLRecruitmentOfferedResponse);
	AccountInfo *pAccount = NULL;
	bool bSuccess = false;

	if (!devassert(pResponse)) return NULL;

	if (!pRequest ||
		!pRequest->pAccountName || !*pRequest->pAccountName ||
		!pRequest->pProductKey || !*pRequest->pProductKey)
	{
		pResponse->pResult = ACCOUNT_HTTP_NO_ARGS;
		return pResponse;
	}

	pAccount = findAccountByName(pRequest->pAccountName);

	if (!pAccount)
	{
		pResponse->pResult = ACCOUNT_HTTP_INVALID_ACCOUNTNAME;
		return pResponse;
	}

	EARRAY_CONST_FOREACH_BEGIN(pAccount->eaRecruits, iCurRecruit, iNumRecruits);
	{
		const RecruitContainer *pRecruit = pAccount->eaRecruits[iCurRecruit];

		if (!devassert(pRecruit)) continue;

		if (!stricmp_safe(pRecruit->pProductKey, pRequest->pProductKey) && recruitOfferable(pRecruit))
		{
			bSuccess = true;

		}
	}
	EARRAY_FOREACH_END;

	if (!bSuccess)
	{
		pResponse->pResult = ACCOUNT_HTTP_INVALID_ACTIVATION_KEY;
		return pResponse;
	}

	bSuccess = accountRecruitmentOffered(pAccount, pRequest->pProductKey);
	devassert(bSuccess);

	pResponse->pResult = ACCOUNT_HTTP_SUCCESS;

	return pResponse;
}


/************************************************************************/
/* Set forbidden payment method types                                   */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCForbidPaymentMethodRequest
{
	const char *pAccountName; AST(NAME(AccountName))
	const PaymentMethodType eForbiddenTypes; AST(NAME(ForbiddenTypes))
} XMLRPCForbidPaymentMethodRequest;

AUTO_STRUCT;
typedef struct XMLRPCForbidPaymentMethodResponse
{
	const char *pResult; AST(NAME(Result) UNOWNED)
} XMLRPCForbidPaymentMethodResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("ForbidPaymentMethod");
SA_RET_NN_VALID XMLRPCForbidPaymentMethodResponse *XMLRPCForbidPaymentMethod(XMLRPCForbidPaymentMethodRequest *pRequest)
{
	XMLRPCForbidPaymentMethodResponse *pResponse = StructCreate(parse_XMLRPCForbidPaymentMethodResponse);
	const AccountInfo *pAccount = findAccountByName(pRequest->pAccountName);

	LogXmlrpcf("Request: ForbidPaymentMethod(%s, %d)",
		pRequest->pAccountName, pRequest->eForbiddenTypes);

	if (!pAccount)
	{
		pResponse->pResult = ACCOUNT_HTTP_INVALID_ACCOUNTNAME;
		goto done;
	}

	accountSetForbiddenPaymentMethods(pAccount->uID, pRequest->eForbiddenTypes);

	pResponse->pResult = ACCOUNT_HTTP_SUCCESS;

done:
	devassert(pResponse->pResult);

	LogXmlrpcf("Response: ForbidPaymentMethod(): %s", NULL_TO_EMPTY(pResponse->pResult));

	return pResponse;
}

/************************************************************************/
/* Mark a recruit as having been offered                                */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCProductKeySetInvalidRequest
{
	const char *pKey;			// Key name
	bool bInvalid;				// True if the key should be marked as invalid, false if it should be marked as valid.
} XMLRPCProductKeySetInvalidRequest;

AUTO_STRUCT;
typedef struct XMLRPCProductKeySetInvalidResponse
{
	const char *pResult; AST(NAME(Result) UNOWNED)
} XMLRPCProductKeySetInvalidResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("ProductKeySetInvalid");
SA_RET_NN_VALID XMLRPCProductKeySetInvalidResponse *XMLRPCProductKeySetInvalid(XMLRPCProductKeySetInvalidRequest *pRequest)
{
	XMLRPCProductKeySetInvalidResponse *pResponse = StructCreate(parse_XMLRPCProductKeySetInvalidResponse);
	ProductKey key;
	bool success;
	U32 account;

	LogXmlrpcf("Request: ProductKeySetInvalid(%s, %d)",
		pRequest->pKey, pRequest->bInvalid);

	// Make sure this key exists.
	success = findProductKey(&key, pRequest->pKey);
	if (!success)
	{
		pResponse->pResult = ACCOUNT_HTTP_INVALID_KEY;
		goto done;
	}

	// Check if this key is activated.
	account = getActivatedAccountId(&key);
	if (account)
	{
		pResponse->pResult = ACCOUNT_HTTP_KEY_USED;
		goto done;
	}

	// Set validity.
	success = productKeySetInvalid(pRequest->pKey, pRequest->bInvalid);
	if (success)
		pResponse->pResult = ACCOUNT_HTTP_SUCCESS;
	else
		pResponse->pResult = ACCOUNT_HTTP_KEY_FAILURE;

done:
	devassert(pResponse->pResult);

	LogXmlrpcf("Response: ProductKeySetInvalid(): %s", NULL_TO_EMPTY(pResponse->pResult));

	return pResponse;
}

/************************************************************************/
/* Find the account that matches a transaction                          */
/************************************************************************/

// Result the result of the transaction.
static void GetAccountByTransaction_Complete(SA_PARAM_NN_VALID BillingTransaction *pTrans, SA_PARAM_NN_VALID const char *pMerchantAccountId)
{
	AccountInfo *account = findAccountByGUID(billingSkipPrefix(pMerchantAccountId));
	PERFINFO_AUTO_START_FUNC();
	if (account)
		pTrans->resultString = estrDup(account->accountName);
	else
		btFail(pTrans, ACCOUNT_HTTP_USER_NOT_FOUND);
	PERFINFO_AUTO_STOP();
}

// Check result of lookup by VID.
static void GetAccountByTransaction_VidCallback(SA_PARAM_NN_VALID BillingTransaction *pTrans, bool bFoundTransaction,
												 SA_PARAM_OP_VALID AccountTransactionInfo *pInfo, SA_PARAM_NN_VALID void *pUserData)
{
	PERFINFO_AUTO_START_FUNC();
	free(pUserData);
	if (bFoundTransaction)
	{
		devassert(pInfo);
		GetAccountByTransaction_Complete(pTrans, pInfo->merchantAccountId);
	}
	else
		btFail(pTrans, ACCOUNT_HTTP_VINDICIA_UNKNOWN);
	PERFINFO_AUTO_STOP();
}

// Check result of lookup by MTID.
static void GetAccountByTransaction_MtidCallback(SA_PARAM_NN_VALID BillingTransaction *pTrans, bool bFoundTransaction,
												 SA_PARAM_OP_VALID AccountTransactionInfo *pInfo, SA_PARAM_NN_VALID void *pUserData)
{
	PERFINFO_AUTO_START_FUNC();
	if (bFoundTransaction)
	{
		free(pUserData);
		devassert(pInfo);
		GetAccountByTransaction_Complete(pTrans, pInfo->merchantAccountId);
	}
	else
		btFetchTransactionByVID(pUserData, pTrans, GetAccountByTransaction_VidCallback, pUserData);
	PERFINFO_AUTO_STOP();
}

AUTO_STRUCT;
typedef struct XMLRPCGetAccountByTransactionRequest
{
	const char *pTransaction;			// Transaction name, as a MTID or VID.
} XMLRPCGetAccountByTransactionRequest;

AUTO_STRUCT;
typedef struct XMLRPCGetAccountByTransactionResponse
{
	char *transID;						// Web UID of pending lookup transaction
	char *result;		AST(UNOWNED)	// If the call failed, this is the reason why.
} XMLRPCGetAccountByTransactionResponse;

// Initiate request for the account name associated with a transaction, either by MTID or by VID.  Return a Web UID for use with TransView().
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("GetAccountByTransaction");
SA_RET_NN_VALID XMLRPCGetAccountByTransactionResponse *XMLRPCGetAccountByTransaction(XMLRPCGetAccountByTransactionRequest *pRequest)
{
	XMLRPCGetAccountByTransactionResponse *pResponse = StructCreate(parse_XMLRPCGetAccountByTransactionResponse);
	BillingTransaction *pTrans;
	LogXmlrpcf("Request: GetAccountByTransaction(%s)",
		pRequest->pTransaction);
	if (pRequest->pTransaction && *pRequest->pTransaction)
	{
		pTrans = btCreateBlank(true);
		btFetchTransactionByMTID(pRequest->pTransaction, pTrans, GetAccountByTransaction_MtidCallback, strdup(pRequest->pTransaction));
		pResponse->transID = strdup(pTrans->webUID);
	}
	else
		pResponse->result = ACCOUNT_HTTP_NO_ARGS;
	LogXmlrpcf("Response: GetAccountByTransaction(): %s, %s", NULL_TO_EMPTY(pResponse->result), NULL_TO_EMPTY(pResponse->transID));
	return pResponse;
}


/************************************************************************/
/* Set whether an account has login enabled                             */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCSetLoginEnabledRequest
{
	const char *pAccountName;
	bool bEnabled;
} XMLRPCSetLoginEnabledRequest;

AUTO_STRUCT;
typedef struct XMLRPCSetLoginEnabledResponse
{
	const char *pResult; AST(UNOWNED)
} XMLRPCSetLoginEnabledResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("SetLoginEnabled");
SA_RET_NN_VALID XMLRPCSetLoginEnabledResponse *XMLRPCSetLoginEnabled(SA_PARAM_NN_VALID const XMLRPCSetLoginEnabledRequest *pRequest)
{
	XMLRPCSetLoginEnabledResponse *pResponse = StructCreate(parse_XMLRPCSetLoginEnabledResponse);
	const AccountInfo *pAccount = findAccountByName(pRequest->pAccountName);

	LogXmlrpcf("Request: SetLoginEnabled(%s, %d)", pRequest->pAccountName, pRequest->bEnabled);

	if (pAccount)
	{
		setLoginDisabled(pAccount->uID, !pRequest->bEnabled);
		pResponse->pResult = ACCOUNT_HTTP_SUCCESS;
	}
	else
	{
		pResponse->pResult = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	LogXmlrpcf("Response: SetLoginEnabled(): %s", NULL_TO_EMPTY(pResponse->pResult));
	return pResponse;
}


/************************************************************************/
/* Update a cached sub start time                                       */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCUpdateSubStartRequest
{
	const char *pAccountName;
	const char *pVID;
	U32 uNewStartTimeSS2000;
} XMLRPCUpdateSubStartRequest;

AUTO_STRUCT;
typedef struct XMLRPCUpdateSubStartResponse
{
	const char *pResult; AST(UNOWNED)	
} XMLRPCUpdateSubStartResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("UpdateSubStart");
SA_RET_NN_VALID XMLRPCUpdateSubStartResponse *XMLRPCUpdateSubStart(SA_PARAM_NN_VALID const XMLRPCUpdateSubStartRequest *pRequest)
{
	XMLRPCUpdateSubStartResponse *pResponse = StructCreate(parse_XMLRPCUpdateSubStartResponse);
	const AccountInfo *pAccount = findAccountByName(pRequest->pAccountName);

	LogXmlrpcf("Request: UpdateSubStart(%s, %s, %u)", pRequest->pAccountName, pRequest->pVID, pRequest->uNewStartTimeSS2000);

	if (pAccount)
	{
		const CachedAccountSubscription *pCachedSub = findAccountSubscriptionByVID(pAccount, pRequest->pVID);

		if (pCachedSub)
		{
			accountUpdateCachedCreatedTime(pAccount->uID, pCachedSub->vindiciaID, pRequest->uNewStartTimeSS2000);
			pResponse->pResult = ACCOUNT_HTTP_SUCCESS;
		}
		else
		{
			pResponse->pResult = ACCOUNT_HTTP_INVALID_SUBSCRIPTION;
		}
	}
	else
	{
		pResponse->pResult = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	LogXmlrpcf("Response: UpdateSubStart(): %s", NULL_TO_EMPTY(pResponse->pResult));
	return pResponse;
}


/************************************************************************/
/* Mark recruit as having been billed for recruit purposes              */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCMarkRecruitBilledRequest
{
	const char *pRecruiterAccountName;
	const char *pRecruitAccountName;
	const char *pProductInternalName;
} XMLRPCMarkRecruitBilledRequest;

AUTO_STRUCT;
typedef struct XMLRPCMarkRecruitBilledResponse
{
	const char *pResult; AST(UNOWNED)
} XMLRPCMarkRecruitBilledResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("MarkRecruitBilled");
SA_RET_NN_VALID XMLRPCMarkRecruitBilledResponse *XMLRPCMarkRecruitBilled(SA_PARAM_NN_VALID const XMLRPCMarkRecruitBilledRequest *pRequest)
{
	XMLRPCMarkRecruitBilledResponse *pResponse = StructCreate(parse_XMLRPCMarkRecruitBilledResponse);
	AccountInfo *pRecruiterAccount = findAccountByName(pRequest->pRecruiterAccountName);
	AccountInfo *pRecruitAccount = findAccountByName(pRequest->pRecruitAccountName);

	LogXmlrpcf("Request: MarkRecruitBilled(%s, %s, %s)", pRequest->pRecruiterAccountName, pRequest->pRecruitAccountName, pRequest->pProductInternalName);

	if (pRecruitAccount && pRecruiterAccount)
	{
		if (pRequest->pProductInternalName && *pRequest->pProductInternalName)
		{
			bool bUpgraded = accountUpgradeRecruit(pRecruiterAccount, pRecruitAccount->uID, pRequest->pProductInternalName, RS_Billed);

			if (bUpgraded)
			{
				accountLog(pRecruiterAccount, "Recruit manually marked as billed: [account:%s] for %s", pRecruitAccount->accountName, pRequest->pProductInternalName);
				accountLog(pRecruitAccount, "Manually marked as billed for %s.", pRequest->pProductInternalName);

				pResponse->pResult = ACCOUNT_HTTP_SUCCESS;
			}
			else
			{
				pResponse->pResult = ACCOUNT_HTTP_UPGRADE_FAILED;
			}
		}
		else
		{
			pResponse->pResult = ACCOUNT_HTTP_PRODUCT_NOT_FOUND;
		}
	}
	else
	{
		pResponse->pResult = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	devassert(pResponse->pResult && *pResponse->pResult);
	LogXmlrpcf("Response: MarkRecruitBilled(): %s", NULL_TO_EMPTY(pResponse->pResult));
	return pResponse;
}


/************************************************************************/
/* Testing Fixtures XML-RPC function for replacing DB                   */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCReplaceAccountDatabaseRequest
{
	EARRAY_OF(AccountInfo) eaAccounts;
	EARRAY_OF(ProductContainer) eaProducts;
	EARRAY_OF(PWCommonAccount) eaPWAccounts;
	EARRAY_OF(SubscriptionCreateData) eaSubscriptions;
	EARRAY_OF(ProductKeyGroup) eaPKGroups;
	EARRAY_OF(ProductKeyBatch) eaPKBatches;
} XMLRPCReplaceAccountDatabaseRequest;

AUTO_STRUCT;
typedef struct XMLRPCReplaceAccountDatabaseResponse
{
	char *pResult; AST(UNOWNED)
} XMLRPCReplaceAccountDatabaseResponse;

static int giAllowSuperDangerousReplaceAccountDB = false;
AUTO_CMD_INT(giAllowSuperDangerousReplaceAccountDB, AllowSuperDangerousReplaceAccountDB) ACMD_CMDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("ReplaceAccountDatabase");
SA_RET_NN_VALID XMLRPCReplaceAccountDatabaseResponse *XMLRPCReplaceAccountDatabase(XMLRPCReplaceAccountDatabaseRequest *db)
{
	XMLRPCReplaceAccountDatabaseResponse *pResponse = StructCreate(parse_XMLRPCReplaceAccountDatabaseResponse);

	if (btGetActiveTransactionCount() > 0)
	{
		// DB can't be purged when there are active transactions
		pResponse->pResult = ACCOUNT_HTTP_DB_LOCKED;
	}
	else if (giAllowSuperDangerousReplaceAccountDB)
	{
		LogXmlrpcf("Request: ReplaceAccountDatabase()");
		AccountServer_ReplaceDatabase(db->eaAccounts, db->eaProducts, db->eaPWAccounts, db->eaSubscriptions, 
			db->eaPKGroups, db->eaPKBatches);
		pResponse->pResult = ACCOUNT_HTTP_SUCCESS;
		LogXmlrpcf("Response: ReplaceAccountDatabase(): %s", NULL_TO_EMPTY(pResponse->pResult));
	}
	else
	{
		pResponse->pResult = ACCOUNT_HTTP_NOT_AUTHORIZED;
	}

	return pResponse;
}

AUTO_STRUCT;
typedef struct XMLRPCReplaceBillingConfigResponse
{
	char *pResult; AST(UNOWNED)
} XMLRPCReplaceBillingConfigResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("ReplaceBillingConfig");
SA_RET_NN_VALID XMLRPCReplaceBillingConfigResponse *XMLRPCReplaceBillingConfig(BillingConfiguration * pConfig)
{
	XMLRPCReplaceBillingConfigResponse * pResponse = StructCreate(parse_XMLRPCReplaceBillingConfigResponse);

	LogXmlrpcf("Request: ReplaceBillingConfig()");

	if (btGetActiveTransactionCount() > 0)
	{
		// Can't change billing config while it's being used
		pResponse->pResult = ACCOUNT_HTTP_DB_LOCKED;
	}
	else if (giAllowSuperDangerousReplaceAccountDB)
	{
		if (billingSetConfiguration(pConfig))
		{
			pResponse->pResult = ACCOUNT_HTTP_SUCCESS;
		}
		else
		{
			pResponse->pResult = ACCOUNT_HTTP_FAILURE;
		}
	}
	else
	{
		pResponse->pResult = ACCOUNT_HTTP_NOT_AUTHORIZED;
	}

	LogXmlrpcf("Response: ReplaceBillingConfig(): %s", NULL_TO_EMPTY(pResponse->pResult));

	return pResponse;
}


/************************************************************************/
/* Set spending cap for an account                                      */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCSetSpendingCapRequest
{
	char *pAccountName;
	char *pCurrency;
	float fAmount; // -1 to set to default
} XMLRPCSetSpendingCapRequest;

AUTO_STRUCT;
typedef struct XMLRPCSetSpendingCapResponse
{
	char *pResult; AST(UNOWNED)
} XMLRPCSetSpendingCapResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("SetSpendingCap");
SA_RET_NN_VALID XMLRPCSetSpendingCapResponse *XMLRPCSetSpendingCap(XMLRPCSetSpendingCapRequest *pRequest)
{
	XMLRPCSetSpendingCapResponse *pResponse = StructCreate(parse_XMLRPCSetSpendingCapResponse);
	AccountInfo *pAccount = findAccountByName(pRequest->pAccountName);

	LogXmlrpcf("Request: SetSpendingCap(%s, %s, %f)", pRequest->pAccountName, pRequest->pCurrency, pRequest->fAmount);

	if (pAccount)
	{
		accountSetSpendingCap(pAccount, pRequest->pCurrency, pRequest->fAmount);
		pResponse->pResult = ACCOUNT_HTTP_SUCCESS;
	}
	else
	{
		pResponse->pResult = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	LogXmlrpcf("Response: SetSpendingCap(): %s", NULL_TO_EMPTY(pResponse->pResult));
	
	return pResponse;
}


/************************************************************************/
/* Mark an account as billed                                            */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCMarkAccountBilledRequest
{
	char *pAccountName;
} XMLRPCMarkAccountBilledRequest;

AUTO_STRUCT;
typedef struct XMLRPCMarkAccountBilledResponse
{
	char *pResult; AST(UNOWNED)
} XMLRPCMarkAccountBilledResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("MarkAccountBilled");
SA_RET_NN_VALID XMLRPCMarkAccountBilledResponse *XMLRPCMarkAccountBilled(XMLRPCMarkAccountBilledRequest *pRequest)
{
	XMLRPCMarkAccountBilledResponse *pResponse = StructCreate(parse_XMLRPCMarkAccountBilledResponse);
	AccountInfo *pAccount = findAccountByName(pRequest->pAccountName);

	LogXmlrpcf("Request: MarkAccountBilled(%s)", pRequest->pAccountName);

	if (pAccount)
	{
		accountSetBilled(pAccount);
		pResponse->pResult = ACCOUNT_HTTP_SUCCESS;
	}
	else
	{
		pResponse->pResult = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	LogXmlrpcf("Response: MarkAccountBilled(): %s", NULL_TO_EMPTY(pResponse->pResult));

	return pResponse;
}

/************************************************************************/
/* Move one key-value to another on the same account                    */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCMoveKeyValueRequest
{
	char *pAccountName;
	char *pSource;
	char *pDestination;
	char *pReason;
} XMLRPCMoveKeyValueRequest;

AUTO_STRUCT;
typedef struct XMLRPCMoveKeyValueResponse
{
	char *pResult; AST(UNOWNED)
} XMLRPCMoveKeyValueResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("MoveKeyValue");
SA_RET_NN_VALID XMLRPCMoveKeyValueResponse *XMLRPCMoveKeyValue(XMLRPCMoveKeyValueRequest* pRequest)
{
	XMLRPCMoveKeyValueResponse *pResponse = StructCreate(parse_XMLRPCMoveKeyValueResponse);
	AccountInfo *pAccount = findAccountByName(pRequest->pAccountName);

	LogXmlrpcf("Request: MoveKeyValue(%s, %s, %s)", pRequest->pAccountName, pRequest->pSource, pRequest->pDestination);
	
	// TODO: Change this to actually use move
	if (pAccount)
	{
		AccountKeyValueResult eResult;
		S64 amount = 0;
		char *pSourceLock = NULL, *pDestLock = NULL;

		eResult = AccountKeyValue_Get(pAccount, pRequest->pSource, &amount);

		if (eResult == AKV_SUCCESS && amount)
		{
			accountLog(pAccount, "Key value change reason: move (from: '%s' to: '%s')%s",
				pRequest->pSource, pRequest->pDestination, pRequest->pReason ? STACK_SPRINTF(" - %s", pRequest->pReason) : "");
		}

		if (eResult == AKV_SUCCESS)
		{
			eResult = AccountKeyValue_Change(pAccount, pRequest->pSource, -amount, &pSourceLock);
		}

		if (eResult == AKV_SUCCESS)
		{
			eResult = AccountKeyValue_Change(pAccount, pRequest->pDestination, amount, &pDestLock);
		}

		if (eResult == AKV_SUCCESS)
		{
			if (pSourceLock) eResult = AccountKeyValue_Commit(pAccount, pRequest->pSource, pSourceLock);
			pSourceLock = NULL;
		}
		
		if (eResult == AKV_SUCCESS)
		{
			if (pDestLock) devassert((eResult = AccountKeyValue_Commit(pAccount, pRequest->pDestination, pDestLock)) == AKV_SUCCESS);
			pDestLock = NULL;
		}

		if (eResult != AKV_SUCCESS)
		{
			if (pSourceLock) AccountKeyValue_Rollback(pAccount, pRequest->pSource, pSourceLock);
			if (pDestLock) AccountKeyValue_Rollback(pAccount, pRequest->pDestination, pDestLock);
		}

		switch (eResult)
		{
		default:
		case AKV_FAILURE:
			pResponse->pResult = ACCOUNT_HTTP_KEY_FAILURE;
			break;
		case AKV_INVALID_KEY:
			pResponse->pResult = ACCOUNT_HTTP_INVALID_KEY;
			break;
		case AKV_INVALID_RANGE:
			pResponse->pResult = ACCOUNT_HTTP_INVALID_RANGE;
			break;
		case AKV_SUCCESS:
			pResponse->pResult = ACCOUNT_HTTP_KEY_SUCCESS;
			break;
		}
	}
	else
	{
		pResponse->pResult = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	LogXmlrpcf("Response: MoveKeyValue(): %s", NULL_TO_EMPTY(pResponse->pResult));

	return pResponse;
}

/************************************************************************/
/* Duplicate one key-value to another on the same account               */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCDuplicateKeyValueRequest
{
	char *pAccountName;
	char *pSource;
	char *pDestination;
	char *pReason;
} XMLRPCDuplicateKeyValueRequest;

AUTO_STRUCT;
typedef struct XMLRPCDuplicateKeyValueResponse
{
	char *pResult; AST(UNOWNED)
} XMLRPCDuplicateKeyValueResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("DuplicateKeyValue");
SA_RET_NN_VALID XMLRPCDuplicateKeyValueResponse *XMLRPCDuplicateKeyValue(XMLRPCDuplicateKeyValueRequest* pRequest)
{
	XMLRPCDuplicateKeyValueResponse *pResponse = StructCreate(parse_XMLRPCDuplicateKeyValueResponse);
	AccountInfo *pAccount = findAccountByName(pRequest->pAccountName);

	LogXmlrpcf("Request: DuplicateKeyValue(%s, %s, %s)", pRequest->pAccountName, pRequest->pSource, pRequest->pDestination);

	if (pAccount)
	{
		AccountKeyValueResult eResult;
		S64 amount = 0;
		char *pDestLock = NULL;

		eResult = AccountKeyValue_Get(pAccount, pRequest->pSource, &amount);

		if (eResult == AKV_SUCCESS && amount)
		{
			accountLog(pAccount, "Key value change reason: duplicate (from: '%s' to: '%s')%s",
				pRequest->pSource, pRequest->pDestination, pRequest->pReason ? STACK_SPRINTF(" - %s", pRequest->pReason) : "");
		}

		if (eResult == AKV_SUCCESS)
		{
			eResult = AccountKeyValue_Change(pAccount, pRequest->pDestination, amount, &pDestLock);
		}

		if (eResult == AKV_SUCCESS)
		{
			if (pDestLock) eResult = AccountKeyValue_Commit(pAccount, pRequest->pDestination, pDestLock);
			pDestLock = NULL;
		}

		if (eResult != AKV_SUCCESS)
		{
			if (pDestLock) AccountKeyValue_Rollback(pAccount, pRequest->pDestination, pDestLock);
		}

		switch (eResult)
		{
		default:
		case AKV_FAILURE:
			pResponse->pResult = ACCOUNT_HTTP_KEY_FAILURE;
			break;
		case AKV_INVALID_KEY:
			pResponse->pResult = ACCOUNT_HTTP_INVALID_KEY;
			break;
		case AKV_INVALID_RANGE:
			pResponse->pResult = ACCOUNT_HTTP_INVALID_RANGE;
			break;
		case AKV_SUCCESS:
			pResponse->pResult = ACCOUNT_HTTP_KEY_SUCCESS;
			break;
		}
	}
	else
	{
		pResponse->pResult = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	LogXmlrpcf("Response: DuplicateKeyValue(): %s", NULL_TO_EMPTY(pResponse->pResult));

	return pResponse;
}

/************************************************************************/
/* Get the Steam user info for a particular Steam ID                    */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCSteamGetUserInfoRequest
{
	char *steamid;
	char *ip;
	char *source;
} XMLRPCSteamGetUserInfoRequest;

AUTO_STRUCT;
typedef struct XMLRPCSteamGetUserInfoResponse
{
	char *steamid;	AST(ESTRING)
	char *result;	AST(UNOWNED)
	char *transID;						// Web UID of pending lookup transaction
} XMLRPCSteamGetUserInfoResponse;

static void XMLRPCSteamGetUserInfo_CB(bool bSuccess, const char *pCountry, const char *pState, const char *pCurrency, const char *pStatus, BillingTransaction *pTrans)
{
	PERFINFO_AUTO_START_FUNC();
	btCompleteSteamTransaction(pTrans, bSuccess);

	if (bSuccess)
	{
		estrCopy2(&pTrans->steamCountry, pCountry);
		estrCopy2(&pTrans->steamState, pState);
		estrCopy2(&pTrans->steamCurrency, pCurrency);
		estrCopy2(&pTrans->steamStatus, pStatus);
	}
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("SteamGetUserInfo");
SA_RET_NN_VALID XMLRPCSteamGetUserInfoResponse *XMLRPCSteamGetUserInfo(XMLRPCSteamGetUserInfoRequest *pRequest)
{
	XMLRPCSteamGetUserInfoResponse *pResponse = StructCreate(parse_XMLRPCSteamGetUserInfoResponse);
	BillingTransaction *pTrans = NULL;
	U64 uSteamID;

	PERFINFO_AUTO_START_FUNC();

	LogXmlrpcf("Request: SteamGetUserInfo(%s)", pRequest->steamid);

	if (!pRequest->steamid || !*pRequest->steamid)
	{
		pResponse->result = ACCOUNT_HTTP_INVALID_STEAMID;
		return pResponse;
	}

	uSteamID = atoui64(pRequest->steamid);

	if (!uSteamID)
	{
		pResponse->result = ACCOUNT_HTTP_INVALID_STEAMID;
		return pResponse;
	}

	if (!pRequest->source || !*pRequest->source)
	{
		pResponse->result = ACCOUNT_HTTP_NO_ARGS;
		return pResponse;
	}

	pResponse->steamid = estrDup(pRequest->steamid);

	pTrans = btCreateBlank(true);
	btMarkSteamTransaction(pTrans);
	GetSteamUserInfo(0, uSteamID, pRequest->source, pRequest->ip, XMLRPCSteamGetUserInfo_CB, pTrans);
	pResponse->result = ACCOUNT_HTTP_SUCCESS;
	pResponse->transID = strdup(pTrans->webUID);

	LogXmlrpcf("Response: SteamGetUserInfo(): %s", NULL_TO_EMPTY(pResponse->result));

	return pResponse;
}

/************************************************************************/
/* Delete an Account and all related containers (logs, subscriptions)   */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCAccountDeleteResponse
{
	const char *pResult; AST(NAME(Result) UNOWNED)
} XMLRPCAccountDeleteResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("UserDelete");
XMLRPCAccountDeleteResponse * XMLRPCUserDelete(U32 uID)
{
	AccountInfo *pAccount = (AccountInfo*) objGetContainerData(GLOBALTYPE_ACCOUNT, uID);
	XMLRPCAccountDeleteResponse *pResponse = StructCreate(parse_XMLRPCAccountDeleteResponse);

	if (pAccount)
	{
		if (accountDelete(pAccount))
			pResponse->pResult = ACCOUNT_HTTP_SUCCESS;
		else
			pResponse->pResult = ACCOUNT_HTTP_NOT_AUTHORIZED;
	}
	else
	{
		pResponse->pResult = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	return pResponse;
}


/************************************************************************/
/* Generate an error!                                                   */
/************************************************************************/

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("Error");
void XMLRPCError(void)
{
	LogXmlrpcf("Request: Error()");

	Errorf("This is a test error.  Please ignore.");

	LogXmlrpcf("Response: Error()");
}


/************************************************************************/
/* Get a list of blocked IPs                                            */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCBlockedIPsEntry
{
	U32 uBlockedUntilSS2000;
	const char * pIPAddress; AST(UNOWNED)
} XMLRPCBlockedIPsEntry;

AUTO_STRUCT;
typedef struct XMLRPCBlockedIPsResponse
{
	EARRAY_OF(XMLRPCBlockedIPsEntry) eaIPRateLimit;
} XMLRPCBlockedIPsResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("BlockedIPs");
SA_RET_NN_VALID XMLRPCBlockedIPsResponse *XMLRPCBlockedIPs(void)
{
	XMLRPCBlockedIPsResponse *pResponse = StructCreate(parse_XMLRPCBlockedIPsResponse);
	RateLimitBlockedIter * pIter = NULL;

	LogXmlrpcf("Request: BlockedIPs()");

	pIter = IPBlockedIterCreate();
	if (pIter)
	{
		const char * pIPAddress = NULL;
		while ((pIPAddress = IPBlockedIterNext(pIter)))
		{
			U32 uBlockedUntil = IPBlockedUntil(pIPAddress);
			if (uBlockedUntil)
			{
				XMLRPCBlockedIPsEntry * pEntry = StructCreate(parse_XMLRPCBlockedIPsEntry);
				pEntry->pIPAddress = pIPAddress;
				pEntry->uBlockedUntilSS2000 = uBlockedUntil;
				eaPush(&pResponse->eaIPRateLimit, pEntry);
			}
		}
		IPBlockedIterDestroy(pIter);
	}

	LogXmlrpcf("Response: BlockedIPs()");

	return pResponse;
}

/************************************************************************/
/* Fixup a transaction log container whose migration derped             */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCFixupTransactionLogMigrationRequest
{
	U32 uAccountID;
	U32 uPurchaseLogCount;
} XMLRPCFixupTransactionLogMigrationRequest;

AUTO_STRUCT;
typedef struct XMLRPCFixupTransactionLogMigrationResponse
{
	char *status; AST(UNOWNED)
	char *result; AST(ESTRING)
} XMLRPCFixupTransactionLogMigrationResponse;

void XMLRPCFixupTransactionLogMigration_CB(TransactionReturnVal *returnVal, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	XMLRPCFixupTransactionLogMigrationResponse *pResponse = pSlowReturnInfo->pUserData;
	char *pReturnString = NULL;

	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		pResponse->status = ACCOUNT_HTTP_SUCCESS;
	}
	else
	{
		int i = 0;
		pResponse->status = ACCOUNT_HTTP_INTERNAL_ERROR;
		for (i = 0; i < returnVal->iNumBaseTransactions; ++i)
		{
			if (returnVal->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE)
			{
				estrCopy2(&pResponse->result, returnVal->pBaseReturnVals[i].returnString);
				break;
			}
		}
	}

	XMLRPC_WriteSimpleStructResponse(&pReturnString, pResponse, parse_XMLRPCFixupTransactionLogMigrationResponse);
	DoSlowCmdReturn(1, pReturnString, pSlowReturnInfo);

	estrDestroy(&pReturnString);
	StructDestroy(parse_XMLRPCFixupTransactionLogMigrationResponse, pResponse);
	free(pSlowReturnInfo);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("FixupTransactionLogMigration");
void XMLRPCFixupTransactionLogMigration(CmdContext *pContext, XMLRPCFixupTransactionLogMigrationRequest *pRequest)
{
	XMLRPCFixupTransactionLogMigrationResponse *pResponse = StructCreate(parse_XMLRPCFixupTransactionLogMigrationResponse);
	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo = malloc(sizeof(CmdSlowReturnForServerMonitorInfo));

	LogXmlrpcf("Request: XMLRPCFixupTransactionLogMigration(%d, %d)", pRequest->uAccountID, pRequest->uPurchaseLogCount);
	pContext->slowReturnInfo.bDoingSlowReturn = true;
	memcpy(pSlowReturnInfo, &pContext->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));
	pSlowReturnInfo->pUserData = pResponse;

	AccountTransactionFixupMigrationReturn(pRequest->uAccountID, pRequest->uPurchaseLogCount, XMLRPCFixupTransactionLogMigration_CB, pSlowReturnInfo);
}

/************************************************************************/
/* Get a list of accounts matching specified criteria                   */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCAccountSearchRequest
{
	char *pAccountName;
	char *pDisplayName;
	char *pEmail;
	char *pFirstName;
	char *pLastName;
	char *pProduct;
	char *pKey;
	char *pCardName;
	char *pCardFirstSix;
	char *pCardLastFour;
	char *pStreetAddress;
	char *pCity;
	char *pState;
	char *pZip;
	int iMaxToReturn;
} XMLRPCAccountSearchRequest;

AUTO_STRUCT;
typedef struct XMLRPCAccountSearchResponse
{
	char *pStatus;		AST(UNOWNED)
	int iMaxToReturn;
	U32 *piResults;		AST(UNOWNED)
} XMLRPCAccountSearchResponse;

void XMLRPCAccountSearch_CB(U32 **ppOutContainerIDs, NonBlockingContainerIterationSummary *pSummary, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	XMLRPCAccountSearchResponse *pResponse = pSlowReturnInfo->pUserData;
	char *pReturnString = NULL;
	
	if (!devassert(pSlowReturnInfo->eHowCalled == CMD_CONTEXT_HOWCALLED_XMLRPC))
	{
		DoSlowCmdReturn(1, "Error: AccountSearch cannot be called other than XMLRPC", pSlowReturnInfo);
		return;
	}

	if (!devassert(pResponse))
	{
		pResponse = StructCreate(parse_XMLRPCAccountSearchResponse);
		pResponse->iMaxToReturn = ea32Size(ppOutContainerIDs);
	}

	pResponse->piResults = *ppOutContainerIDs;

	if (ea32Size(ppOutContainerIDs))
		pResponse->pStatus = ACCOUNT_HTTP_SUCCESS;
	else
		pResponse->pStatus = ACCOUNT_HTTP_NO_RESULTS;

	XMLRPC_WriteSimpleStructResponse(&pReturnString, pResponse, parse_XMLRPCAccountSearchResponse);
	DoSlowCmdReturn(1, pReturnString, pSlowReturnInfo);

	estrDestroy(&pReturnString);
	StructDestroy(parse_XMLRPCAccountSearchResponse, pResponse);
	free(pSlowReturnInfo);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("AccountSearch");
void XMLRPCAccountSearch(CmdContext *pContext, XMLRPCAccountSearchRequest *pRequest)
{
	XMLRPCAccountSearchResponse *pResponse = StructCreate(parse_XMLRPCAccountSearchResponse);
	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo = malloc(sizeof(*pSlowReturnInfo));

	LogXmlrpcf("Request: XMLRPCAccountSearch()");
	pContext->slowReturnInfo.bDoingSlowReturn = true;
	*pSlowReturnInfo = pContext->slowReturnInfo;
	pSlowReturnInfo->pUserData = pResponse;

	accountSearch_Begin(pRequest->pAccountName,
		pRequest->pDisplayName,
		pRequest->pEmail,
		pRequest->pFirstName,
		pRequest->pLastName,
		pRequest->pProduct,
		pRequest->pKey,
		pRequest->pCardName,
		pRequest->pCardFirstSix,
		pRequest->pCardLastFour,
		pRequest->pStreetAddress,
		pRequest->pCity,
		pRequest->pState,
		pRequest->pZip,
		pRequest->iMaxToReturn,
		XMLRPCAccountSearch_CB,
		pSlowReturnInfo);
}

/************************************************************************/
/* Reconstruct a purchase log that was lost in migration                */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCFixupMigratedPurchaseLogRequest
{
	U32 uAccountID;
	U32 uProductID;
	char *pSource;
	char *pPrice;
	char *pCurrency;
	U32 uTimestampSS2000;
	char *pOrderID;
	char *pMerchantTransactionID;
	TransactionProvider eProvider;
} XMLRPCFixupMigratedPurchaseLogRequest;

AUTO_STRUCT;
typedef struct XMLRPCFixupMigratedPurchaseLogResponse
{
	char *status; AST(UNOWNED)
} XMLRPCFixupMigratedPurchaseLogResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("FixupMigratedPurchaseLog");
XMLRPCFixupMigratedPurchaseLogResponse *XMLRPCFixupMigratedPurchaseLog(XMLRPCFixupMigratedPurchaseLogRequest *pRequest)
{
	XMLRPCFixupMigratedPurchaseLogResponse *pResponse = StructCreate(parse_XMLRPCFixupMigratedPurchaseLogResponse);

	if (!pRequest->uAccountID || !pRequest->uProductID || !pRequest->pPrice || !pRequest->pCurrency || !pRequest->uTimestampSS2000)
		pResponse->status = ACCOUNT_HTTP_NO_ARGS;
	else
	{
		FixupMigratedPurchaseLog(pRequest->uAccountID, pRequest->uProductID, pRequest->pSource, pRequest->pPrice, pRequest->pCurrency,
			pRequest->uTimestampSS2000, pRequest->pOrderID, pRequest->pMerchantTransactionID, pRequest->eProvider);
		pResponse->status = ACCOUNT_HTTP_SUCCESS;
	}

	return pResponse;
}


/************************************************************************/
/* Update account info from Vindicia                                    */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCAccountLogRequest
{
	char * pAccountName;
	char * pLogLine;
} XMLRPCAccountLogRequest;

AUTO_STRUCT;
typedef struct XMLRPCAccountLogResponse
{
	char * status; AST(UNOWNED)
} XMLRPCAccountLogResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("AccountLog");
XMLRPCAccountLogResponse *XMLRPCAccountLog(XMLRPCAccountLogRequest * pRequest)
{
	XMLRPCAccountLogResponse * pResponse = NULL;
	AccountInfo * pAccount = NULL;

	PERFINFO_AUTO_START_FUNC();

	pResponse = StructCreate(parse_XMLRPCAccountLogResponse);

	LogXmlrpcf("Request: " __FUNCTION__ "(%s, %s)", pRequest->pAccountName, pRequest->pLogLine);

	if (pRequest->pAccountName)
	{
		pAccount = findAccountByName(pRequest->pAccountName);
	}

	if (pAccount)
	{
		accountLog(pAccount, "%s", pRequest->pLogLine);
		pResponse->status = ACCOUNT_HTTP_SUCCESS;
	}
	else
	{
		pResponse->status = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	LogXmlrpcf("Response: " __FUNCTION__ "(): %s", pResponse->status);

	PERFINFO_AUTO_STOP_FUNC();

	return pResponse;
}


/************************************************************************/
/* Update account info from Vindicia                                    */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCRefreshVindiciaAccountRequest
{
	char * pAccountName;
} XMLRPCRefreshVindiciaAccountRequest;

AUTO_STRUCT;
typedef struct XMLRPCRefreshVindiciaAccountResponse
{
	char * status; AST(UNOWNED)
	char * transID;
} XMLRPCRefreshVindiciaAccountResponse;

static void XMLRPCRefreshVindiciaAccount_FetchComplete(BillingTransaction * pTrans)
{
	VindiciaXMLtoObjResult * pVindiciaResult = NULL;
	AccountInfo * pAccount = pTrans->userData;

	PERFINFO_AUTO_START_FUNC();

	pVindiciaResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(acc, fetchByMerchantAccountIdResponse));
	if (pVindiciaResult)
	{
		struct acc__fetchByMerchantAccountIdResponse * pResponse = pVindiciaResult->pObj;
		BILLING_DEBUG_RESPONSE("acc__fetchByMerchantAccountId", pResponse);
		btFreeObjResult(pTrans, pVindiciaResult);
		pVindiciaResult = NULL;

		if (pResponse->_return_->returnCode == VINDICIA_SUCCESS_CODE)
		{
			if (!btPopulateVinAccountResponseFromAccount(pTrans, pResponse, pAccount->uID))
			{			
				btFail(pTrans, "Could not populate the account cache from the Vindicia response.");
			}
		}
		else
		{
			btFail(pTrans, "Failed [%d]: %s", pResponse->_return_->returnCode, pResponse->_return_->returnString);
		}
	}
	else
	{
		btFail(pTrans, "Failed to receive result from Vindicia.");
	}

	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("RefreshVindiciaAccount");
XMLRPCRefreshVindiciaAccountResponse *XMLRPCRefreshVindiciaAccount(XMLRPCRefreshVindiciaAccountRequest * pRequest)
{
	XMLRPCRefreshVindiciaAccountResponse * pResponse = NULL;
	AccountInfo * pAccount = NULL;

	PERFINFO_AUTO_START_FUNC();

	pResponse = StructCreate(parse_XMLRPCRefreshVindiciaAccountResponse);

	LogXmlrpcf("Request: " __FUNCTION__ "(%s)", pRequest->pAccountName);

	if (pRequest->pAccountName)
	{
		pAccount = findAccountByName(pRequest->pAccountName);
	}

	if (pAccount)
	{
		BillingTransaction * pTrans = btCreateBlank(true);
		if (btFetchAccountStep(pAccount->uID, pTrans, XMLRPCRefreshVindiciaAccount_FetchComplete, pAccount))
		{
			pResponse->transID = strdup(pTrans->webUID);
			pResponse->status = ACCOUNT_HTTP_SUCCESS;
		}
		else
		{
			pResponse->status = ACCOUNT_HTTP_INTERNAL_ERROR;
		}
	}
	else
	{
		pResponse->status = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	LogXmlrpcf("Response: " __FUNCTION__ "(): %s", pResponse->status);

	PERFINFO_AUTO_STOP_FUNC();

	return pResponse;
}


/************************************************************************/
/* Perfect World-related calls                                          */
/************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCCollisionQueryResponse
{
	const char *pResult; AST(UNOWNED)
} XMLRPCCollisionQueryResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("NameCollisionQuery");
XMLRPCCollisionQueryResponse *XMLRPCNameCollisionQuery(const char *pDesiredName)
{
	XMLRPCCollisionQueryResponse *pResponse = StructCreate(parse_XMLRPCCollisionQueryResponse);

	LogXmlrpcf("Request: XMLRPCNameCollisionQuery(%s)", pDesiredName);

	if (isUsernameUsed(pDesiredName))
	{
		pResponse->pResult = ACCOUNT_HTTP_USER_EXISTS;
	}
	else
	{
		pResponse->pResult = ACCOUNT_HTTP_USER_NOT_FOUND;
	}

	LogXmlrpcf("Response: XMLRPCNameCollisionQuery(): %s", pResponse->pResult);

	return pResponse;
}

AUTO_STRUCT;
typedef struct XMLRPCLinkPWRequest
{
	const char *pPWAccountName; AST(NAME(PWAccountName))
	const char *pCrypticAccountName; AST(NAME(CrypticAccountName))
	int iFlags;					AST(NAME(Flags))			// USERINFO_ flags for account response
} XMLRPCLinkPWRequest;

AUTO_STRUCT;
typedef struct XMLRPCLinkPWResponse
{
	const char *pResult; AST(NAME(Result) UNOWNED)
} XMLRPCLinkPWResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("LinkToPWAccount");
XMLAccountResponse * XMLRPCLinkPWToCryptic(XMLRPCLinkPWRequest *pRequest)
{
	XMLAccountResponse *xmlResponse = NULL;
	AccountInfo *pAccount = findAccountByName(pRequest->pCrypticAccountName);
	PWCommonAccount *pPWAccount = findPWCommonAccountByLoginField(pRequest->pPWAccountName);
	const char *pResult = "";

	LogXmlrpcf("Request: XMLRPCLinkPWToCryptic(%s, %s)", pRequest->pPWAccountName, pRequest->pCrypticAccountName);
	if (!pAccount)
	{
		pResult = ACCOUNT_HTTP_USER_UNKNOWN_ERROR;
	}
	else if (!pPWAccount)
	{
		pResult = ACCOUNT_HTTP_PWUSER_UNKNOWN;
	}
	else
	{
		AccountIntegrationResult eResult = AccountIntegration_LinkAccountToPWCommon(pAccount, pPWAccount);
		pResult = getAccountIntegrationResultString(eResult);
		if (eResult == ACCOUNT_INTEGRATION_Success)
			xmlResponse = XMLRPCGetUserInfo(pAccount, pRequest->iFlags);
	}
	if (!xmlResponse)
		xmlResponse = StructCreate(parse_XMLAccountResponse);
	strcpy(xmlResponse->userStatus, pResult);
	LogXmlrpcf("Response: XMLRPCLinkPWToCryptic(%s)", pResult);
	return xmlResponse;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("UnlinkPWAccount");
XMLRPCLinkPWResponse * XMLRPCUnlinkPWFromCryptic(XMLRPCLinkPWRequest *pRequest)
{
	XMLRPCLinkPWResponse *pResponse = StructCreate(parse_XMLRPCLinkPWResponse);
	AccountInfo *pAccount = findAccountByName(pRequest->pCrypticAccountName);

	LogXmlrpcf("Request: XMLRPCUnlinkPWFromCryptic(%s)", pRequest->pCrypticAccountName);
	if (!pAccount)
	{
		pResponse->pResult = ACCOUNT_HTTP_USER_UNKNOWN_ERROR;
	}
	else
	{
		AccountIntegrationResult eResult = AccountIntegration_UnlinkAccountFromPWCommon(pAccount);
		pResponse->pResult = getAccountIntegrationResultString(eResult);
	}
	LogXmlrpcf("Response: XMLRPCUnlinkPWFromCryptic(%s)", pResponse->pResult);
	return pResponse;
}

AUTO_STRUCT;
typedef struct XMLRPCAutoCreatePWRequest
{
	const char *pPWAccountName; AST(NAME(PWAccountName))
	const char *pOverrideDisplayName; AST(NAME(OverrideDisplayName))
	int iFlags;					AST(NAME(Flags))			// USERINFO_ flags for account response
} XMLRPCAutoCreatePWRequest;

AUTO_STRUCT;
typedef struct XMLRPCAutoCreatePWResponse
{
	U32  id;
	char * loginName;
	char * displayName;
	char * email;
	char * guid;

	const char *pResult; AST(NAME(Result) UNOWNED)
} XMLRPCAutoCreatePWResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("AutoCreateAccountFromPW");
XMLAccountResponse * XMLRPCAutoCreateAccountFromPW(XMLRPCAutoCreatePWRequest *pRequest)
{
	XMLAccountResponse *xmlResponse = NULL;
	PWCommonAccount *pPWAccount = findPWCommonAccountByLoginField(pRequest->pPWAccountName);
	AccountInfo *pAccount = NULL;
	const char *pResultString = "";

	LogXmlrpcf("Request: XMLRPCAutoCreateAccountFromPW(%s, %s)",
		pRequest->pPWAccountName ? pRequest->pPWAccountName : "(null)",
		pRequest->pOverrideDisplayName ? pRequest->pOverrideDisplayName : "(null)");

	if (!pPWAccount)
	{
		xmlResponse = StructCreate(parse_XMLAccountResponse);
		pResultString = ACCOUNT_HTTP_PWUSER_UNKNOWN;
	}
	else
	{
		AccountCreationResult eCreateResult = ACCOUNTCREATE_Success;
		AccountIntegrationResult eResult = AccountIntegration_CreateAccountFromPWCommon(pPWAccount, pRequest->pOverrideDisplayName, &pAccount, &eCreateResult);
		
		// Special handling for certain results
		switch (eResult)
		{
		case ACCOUNT_INTEGRATION_Error:
			if (eCreateResult)
				pResultString = getAccountCreationResultString(eCreateResult);
			else
				pResultString = ACCOUNT_HTTP_NO_ARGS;
		xcase ACCOUNT_INTEGRATION_Success:
			if (pAccount)
			{
				xmlResponse = XMLRPCGetUserInfo(pAccount, pRequest->iFlags);
				pResultString = getAccountIntegrationResultString(eResult);
			}
			else
				pResultString = ACCOUNT_HTTP_INTERNAL_ERROR;
		xcase ACCOUNT_INTEGRATION_EmailConflict:
			if (pAccount)
			{
				// TODO(Theo) should this send full account info on conflicting account?
				xmlResponse = XMLRPCGetUserInfo(pAccount, pRequest->iFlags);
				pResultString = getAccountIntegrationResultString(eResult);
			}
		xdefault:
			pResultString = getAccountIntegrationResultString(eResult);
		}
		if (!xmlResponse)
			xmlResponse = StructCreate(parse_XMLAccountResponse);
	}
	strcpy(xmlResponse->userStatus, pResultString);

	LogXmlrpcf("Response: XMLRPCAutoCreateAccountFromPW(%s, %s)", pResultString, xmlResponse->loginName ? xmlResponse->loginName : "");
	return xmlResponse;
}

#include "XMLInterface_c_ast.c"
#include "XMLInterface_h_ast.c"
