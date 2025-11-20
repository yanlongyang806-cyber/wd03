#include "AccountManagement.h"

#include "accountCommon.h"
#include "AccountEncryption.h"
#include "AccountIntegration.h"
#include "AccountLog.h"
#include "AccountReporting.h"
#include "AccountServer.h"
#include "AccountServerConfig.h"
#include "autobill/UpdateActiveSubscriptions.h"
#include "AutoTransDefs.h"
#include "crypt.h"
#include "earray.h"
#include "ErrorStrings.h"
#include "estring.h"
#include "fastAtoi.h"
#include "file.h"
#include "GcsInterface.h"
#include "InternalSubs.h"
#include "JSONRPC.h"
#include "KeyValues/KeyValues.h"
#include "logging.h"
#include "net/accountnet.h"
#include "objContainerIO.h"
#include "objIndex.h"
#include "objTransactions.h"
#include "Product.h"
#include "ProductKey.h"
#include "ProxyInterface/AccountProxy.h"
#include "PurchaseProduct.h"
#include "StashTable.h"
#include "StringUtil.h"
#include "Subscription.h"
#include "SubscriptionHistory.h"
#include "sysutil.h"
#include "timing.h"
#include "utils.h"
#include "websrv.h"

#include "accountCommon_h_ast.h"
#include "AccountManagement_c_ast.h"
#include "AccountServer_h_ast.h"
#include "AutoGen/websrv_h_ast.h"

#include "AutoGen/AccountServer_autotransactions_autogen_wrappers.h"

// How long permission caches last
static U32 guPermissionCacheTimeToLive = SECONDS_PER_DAY;
AUTO_CMD_INT(guPermissionCacheTimeToLive, PermissionCacheTimeToLive) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

// The permission that indicates somebody is a "gold" subscriber
static char gsUpgradePermission[64] = "Upgraded";
AUTO_CMD_STRING(gsUpgradePermission, UpgradedPermission) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

//There's a special mode for the salting fixup kludge where, if we have a backup password info that is 
//the old style salting, we allow validation against it, violating the normal principle that backup passwords
//are there solely for backing up. This switch turns that kludge off, is intended to be used to verify that 
//all verifying clients have been updated
static bool sbDontAllowLoginWithBackupPassword = false;
AUTO_CMD_INT(sbDontAllowLoginWithBackupPassword, DontAllowLoginWithBackupPassword);


static StashTable sAccountNameStash; // also includes display names
static StashTable sAccountEmailStash;
static StashTable sGUIDStash;

// To help detect if we're still signing RSA tickets
int gSlowLoginCounter = 0;

void initAccountStashTables(void)
{
	sAccountNameStash = stashTableCreate(131072, StashDeepCopyKeys, StashKeyTypeStrings, 0);
	sAccountEmailStash = stashTableCreate(131072, StashDeepCopyKeys, StashKeyTypeStrings, 0);
	sGUIDStash = stashTableCreate(131072, StashDeepCopyKeys, StashKeyTypeStrings, 0);
}

static bool addAccountToStashTables(AccountInfo *account)
{
	if (stashAddInt(sAccountNameStash, account->accountName, account->uID, false))
	{
		if (account->globallyUniqueID && *account->globallyUniqueID)
			stashAddInt(sGUIDStash, account->globallyUniqueID, account->uID, false);

		if (account->displayName && stricmp(account->accountName, account->displayName) != 0)
		{
			if (stashAddInt(sAccountNameStash, account->displayName, account->uID, false))
			{
				if (!account->personalInfo.email)
					return true;
				if (stashAddInt(sAccountEmailStash, account->personalInfo.email, account->uID, false))
					return true;
				stashRemoveInt(sAccountNameStash, account->displayName, NULL);
			}
		}
		else
		{
			if (!account->personalInfo.email)
				return true;
			if (stashAddInt(sAccountEmailStash, account->personalInfo.email, account->uID, false))
				return true;
		}
		stashRemoveInt(sAccountNameStash, account->accountName, NULL);
	}
	return false;
}

static void removeAccountFromStashTables(AccountInfo *account)
{
	U32 uID;
	if (stashFindInt(sAccountNameStash, account->accountName, &uID))
	{
		if (uID == 0 || uID == account->uID)
		{
			stashRemoveInt(sAccountNameStash, account->accountName, NULL);
		}
	}
	if (account->displayName && stashFindInt(sAccountNameStash, account->displayName, &uID))
	{
		if (uID == 0 || uID == account->uID)
		{
			stashRemoveInt(sAccountNameStash, account->displayName, NULL);
		}
	}
	if (account->personalInfo.email && stashFindInt(sAccountEmailStash, account->personalInfo.email, &uID))
	{
		if (uID == 0 || uID == account->uID)
		{
			stashRemoveInt(sAccountEmailStash, account->personalInfo.email, NULL);
		}
	}
	if (account->globallyUniqueID && *account->globallyUniqueID && stashFindInt(sGUIDStash, account->globallyUniqueID, &uID))
	{
		if (uID == 0 || uID == account->uID)
		{
			stashRemoveInt(sGUIDStash, account->globallyUniqueID, NULL);
		}
	}
}

void objAccountAddToContainerStore_CB(Container *con, AccountInfo *account)
{
	addAccountToStashTables(account);
}

void objAccountRemoveFromContainerStore_CB(Container *con, AccountInfo *account)
{
	assert(account);
	removeAccountFromStashTables(account);
}

U32 getAccountCount()
{
	return objCountTotalContainersWithType(GLOBALTYPE_ACCOUNT);
}

Container * findAccountContainerByName(const char * pAccountOrDisplayName)
{
	int containerID = 0;

	if (!pAccountOrDisplayName || !pAccountOrDisplayName[0])
		return NULL;
	stashFindInt(sAccountNameStash, pAccountOrDisplayName, &containerID);

	if (containerID)
		return objGetContainer(GLOBALTYPE_ACCOUNT, containerID);
	return NULL;
}

SA_RET_OP_VALID static Container * findAccountContainerByGUID(SA_PARAM_NN_STR const char *pGUID)
{
	int containerID = 0;

	stashFindInt(sGUIDStash, pGUID, &containerID);

	if (containerID)
		return objGetContainer(GLOBALTYPE_ACCOUNT, containerID);

	return NULL;
}

bool isUsernameUsed(const char *name)
{
	if (findAccountContainerByName(name))
		return true;
	if (findPWCommonAccountbyName(name))
		return true;
	if (findPWCommonAccountbyForumName(name))
		return true;
	return false;
}

static Container * findAccountContainerByEmail(const char * pEmail)
{
	int containerID = 0;

	if (!pEmail || !pEmail[0])
		return NULL;
	stashFindInt(sAccountEmailStash, pEmail, &containerID);

	if (containerID)
		return objGetContainer(GLOBALTYPE_ACCOUNT, containerID);
	return NULL;
}

SA_RET_OP_VALID AccountInfo * findAccountByGUID(SA_PARAM_NN_STR const char *pGUID)
{
	Container *con = findAccountContainerByGUID(pGUID);
	if (con)
		return (AccountInfo *) con->containerData;
	return NULL;
}

AccountInfo * findAccountByNameOrEmail(const char *pNameOrEmail)
{
	AccountInfo *account = findAccountByName(pNameOrEmail);
	if (!account) account = findAccountByEmail(pNameOrEmail);
	return account;
}

AccountInfo * findAccountByEmail(const char * pEmail)
{
	Container *con = findAccountContainerByEmail(pEmail);
	if (con)
	{
		AccountInfo *account = (AccountInfo*) con->containerData;
		account->temporaryFlags |= ACCOUNT_ACCESSED_FLAG;
		return account;
	}
	return NULL;
}

AccountInfo * findAccountByName(const char * pAccountName)
{
	Container *con;
	PERFINFO_AUTO_START_FUNC();
	con = findAccountContainerByName(pAccountName);
	if (con)
	{
		AccountInfo *account = (AccountInfo*) con->containerData;		
		if (stricmp(account->accountName, pAccountName) == 0)
		{
			account->temporaryFlags |= ACCOUNT_ACCESSED_FLAG;
			PERFINFO_AUTO_STOP_FUNC();
			return account;
		}
	}
	PERFINFO_AUTO_STOP_FUNC();
	return NULL;
}

AccountInfo * findNextAccountByID(U32 uID)
{
	Container *con;
	ContainerIterator iter = {0};

	PERFINFO_AUTO_START_FUNC();
	if(uID > 0)
	{
		con = objGetContainer(GLOBALTYPE_ACCOUNT, uID);
		if (con)
		{
			objInitContainerIteratorFromContainer(con, &iter);
			objGetNextContainerFromIterator(&iter); // Advance past "this" one
		}
	}
	else
	{
		objInitContainerIteratorFromType(GLOBALTYPE_ACCOUNT, &iter);
	}

	con = objGetNextContainerFromIterator(&iter);
	objClearContainerIterator(&iter);
	if(con)
	{
		AccountInfo *account = (AccountInfo*) con->containerData;
		PERFINFO_AUTO_STOP_FUNC();
		return account;
	}

	PERFINFO_AUTO_STOP_FUNC();
	return NULL;
}

AccountInfo * findAccountByDisplayName(const char * pDisplayName)
{
	Container *con = findAccountContainerByName(pDisplayName);
	if (con)
	{
		AccountInfo *account = (AccountInfo*) con->containerData;
		if (stricmp(account->displayName, pDisplayName) == 0)
		{
			account->temporaryFlags |= ACCOUNT_ACCESSED_FLAG;
			return account;
		}
	}
	return NULL;
}

AccountInfo * findAccountByID(U32 uID)
{
	Container *con = objGetContainer(GLOBALTYPE_ACCOUNT, uID);
	if (con)
	{
		AccountInfo *account = (AccountInfo*) con->containerData;
		account->temporaryFlags |= ACCOUNT_ACCESSED_FLAG;
		return account;
	}
	return NULL;
}

typedef struct TRCreateAccountData
{
	AccountInfo *account;
	char **ips;
} TRCreateAccountData;

AUTO_TRANS_HELPER;
void generateAccountGUID(ATH_ARG NOCONST(AccountInfo) *pAccount)
{
	char *pUniqueID = createShortUniqueString(0, ALPHA_NUMERIC_CAPS);
	U32 uID;

	if (pAccount->globallyUniqueID && *pAccount->globallyUniqueID)
		stashRemoveInt(sGUIDStash, pAccount->globallyUniqueID, NULL);

	while (stashFindInt(sGUIDStash, pUniqueID, &uID)) // Might happen during migration, if they're generated quickly enough.
	{
		estrDestroy(&pUniqueID);
		pUniqueID = createShortUniqueString(0, ALPHA_NUMERIC_CAPS);
	}

	strcpy(pAccount->globallyUniqueID, pUniqueID); 

	stashAddInt(sGUIDStash, pAccount->globallyUniqueID, pAccount->uID, false);

	estrDestroy(&pUniqueID);
}

void trCreateAccount_CB(TransactionReturnVal *returnVal, TRCreateAccountData *trData)
{
	StructDestroy(parse_AccountInfo, trData->account);
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		U32 uID = atoi(returnVal->pBaseReturnVals->returnString);

		if (uID)
		{
			AccountInfo *account = findAccountByID(uID);
			char *ips = NULL;
			stashAddInt(sGUIDStash, account->globallyUniqueID, account->uID, true);

			// Log account creation.
			accountLog(account, "Account created.");
			estrStackCreate(&ips);
			EARRAY_CONST_FOREACH_BEGIN(trData->ips, i, n);
				if (i)
					estrAppend2(&ips, " ");
				estrConcatf(&ips, "Ip%d \"%s\"", i, trData->ips[i] ? trData->ips[i] : "");
			EARRAY_FOREACH_END;
			objLog(LOG_ACCOUNT_SERVER_GENERAL, GLOBALTYPE_ACCOUNT, uID, 0, NULL, NULL, NULL, "AccountCreated", NULL, "AccountName %s %s", account->accountName, ips);
			estrDestroy(&ips);

			AccountServer_SendCreateAccount(NULL, 0, uID);
		}
	}
	eaDestroyEx(&trData->ips, NULL);
	free (trData);
}

const char *getAccountCreationResultString(AccountCreationResult eResult)
{
	switch (eResult)
	{
	case ACCOUNTCREATE_Success:
		return ACCOUNT_HTTP_SUCCESS;

	case ACCOUNTCREATE_AccountNameRestricted:
		return ACCOUNT_HTTP_RESTRICTED_ACCOUNTNAME;
	case ACCOUNTCREATE_AccountNameInvalid:
		return ACCOUNT_HTTP_INVALID_ACCOUNTNAME;
	case ACCOUNTCREATE_DisplayNameInvalid:
		return ACCOUNT_HTTP_INVALID_DISPLAYNAME;

	case ACCOUNTCREATE_AccountNameLength:
		return ACCOUNT_HTTP_INVALIDLEN_ACCOUNTNAME;
	case ACCOUNTCREATE_DisplayNameLength:
		return ACCOUNT_HTTP_INVALIDLEN_DISPLAYNAME;

	case ACCOUNTCREATE_AccountNameConflict:
		return ACCOUNT_HTTP_USER_EXISTS;
	case ACCOUNTCREATE_DisplayNameConflict:
		return ACCOUNT_HTTP_DISPLAYNAME_EXISTS;
	case ACCOUNTCREATE_EmailConflict:
		return ACCOUNT_HTTP_EMAIL_EXISTS;
	default:
		return ACCOUNT_HTTP_SUCCESS;
	}
}

// Auto-create a minimal account with just the Account and Display names - no email or other personal info
AccountCreationResult createAutogenPWAccount(SA_PARAM_NN_STR const char *pAccountName, SA_PARAM_NN_STR const char *displayName)
{
	NOCONST(AccountInfo) *pNewAccount = NULL;
	TRCreateAccountData *data;
	int err;
	
	if (err = StringIsInvalidDisplayName(displayName, 0))
	{
		if (err == STRINGERR_MIN_LENGTH || err == STRINGERR_MAX_LENGTH)
			return ACCOUNTCREATE_DisplayNameLength;
		else
			return ACCOUNTCREATE_DisplayNameInvalid;
	}
	// Only check against Cryptic accounts here
	if (findAccountByName(pAccountName))
		return ACCOUNTCREATE_AccountNameConflict;
	if (findAccountByName(displayName))
		return ACCOUNTCREATE_DisplayNameConflict;
	// does not check email

	pNewAccount = StructCreateNoConst(parse_AccountInfo);
	strcpy(pNewAccount->accountName, pAccountName);
	if (displayName && displayName[0])
		estrCopy2(&pNewAccount->displayName, displayName);
	else
		estrCopy2(&pNewAccount->displayName, pAccountName);
	pNewAccount->bPWAutoCreated = true;
	pNewAccount->uCreatedTime = timeSecondsSince2000();
	pNewAccount->flags |= ACCOUNT_FLAG_LOGS_REBUCKETED;
	pNewAccount->uNextLogID = 1;

	data = callocStruct(TRCreateAccountData);
	data->account = (AccountInfo*) pNewAccount;

	generateAccountGUID(pNewAccount);
	objRequestContainerCreateLocal(objCreateManagedReturnVal(trCreateAccount_CB, data), GLOBALTYPE_ACCOUNT, pNewAccount);
	return ACCOUNTCREATE_Success;
}

// Creating non-internal, public accounts
bool createFullAccount(const char *pAccountName,
					   const char *pPasswordHash,
					   const char *displayName,
					   const char *email,
					   const char *firstName,
					   const char *lastName,
					   const char *defaultCurrency,
					   const char *defaultLocale,
					   const char *defaultCountry,
					   const char *const *ips,
					   bool bGenerateActivator,
					   bool bInternalLoginOnly,
					   int iAccessLevel)
{
	// Assume string validation happens earlier
	NOCONST(AccountInfo) *pNewAccount = NULL;
	TRCreateAccountData *data;

	if (StringIsInvalidAccountName(pAccountName, iAccessLevel)
		|| displayName && StringIsInvalidDisplayName(displayName, iAccessLevel))
		return false;
	if (isUsernameUsed(pAccountName) || isUsernameUsed(displayName))
		return false;
	if (email && findAccountContainerByEmail(email))
		return false;

	pNewAccount = StructCreateNoConst(parse_AccountInfo);
	strcpy(pNewAccount->accountName, pAccountName);
	strcpy(pNewAccount->password_obsolete, pPasswordHash);

	//make sure that this happens after AccountName is set
	if (!accountDoCreationTimePasswordFixup(pNewAccount))
	{
		StructDestroy(parse_AccountInfo, (AccountInfo*)pNewAccount);
		return false;
	}

	if (firstName)
		estrCopy2(&pNewAccount->personalInfo.firstName, firstName);
	
	if (lastName)
		estrCopy2(&pNewAccount->personalInfo.lastName, lastName);

	estrCopy2(&pNewAccount->personalInfo.email, email);

	if (defaultLocale)
		estrCopy2(&pNewAccount->defaultLocale, defaultLocale);

	if (defaultCurrency)
		estrCopy2(&pNewAccount->defaultCurrency, defaultCurrency);

	if (defaultCountry)
		estrCopy2(&pNewAccount->defaultCountry, defaultCountry);

	if (displayName && displayName[0])
		estrCopy2(&pNewAccount->displayName, displayName);
	else
		estrCopy2(&pNewAccount->displayName, pAccountName);
	pNewAccount->uCreatedTime = timeSecondsSince2000();
	pNewAccount->uPasswordChangeTime = timeSecondsSince2000();
	pNewAccount->flags |= ACCOUNT_FLAG_LOGS_REBUCKETED;
	pNewAccount->uNextLogID = 1;

	if (bGenerateActivator)
	{
		pNewAccount->flags |= ACCOUNT_FLAG_NOT_ACTIVATED;
		generateActivationKey(pNewAccount->validateEmailKey);
	}

	pNewAccount->bInternalUseLogin = bInternalLoginOnly;

	data = callocStruct(TRCreateAccountData);
	data->account = (AccountInfo*) pNewAccount;
	eaCopyEx((EArrayHandle*)&ips, &data->ips, strdupFunc, strFreeFunc);

	generateAccountGUID(pNewAccount);

	objRequestContainerCreateLocal(objCreateManagedReturnVal(trCreateAccount_CB, data), GLOBALTYPE_ACCOUNT, pNewAccount);
	return true;
}

void accountSetDOB(AccountInfo *account, U32 day, U32 month, U32 year)
{
    AutoTrans_trAccountChangeDOB(NULL, objServerType(), GLOBALTYPE_ACCOUNT, account->uID, day, month, year);
}

bool validateAccountEmail(AccountInfo *account, const char *validateEmailKey)
{
	if (validateEmailKey && stricmp(validateEmailKey, account->validateEmailKey) == 0)
	{
		AutoTrans_trAccountValidateEmail(NULL, objServerType(), GLOBALTYPE_ACCOUNT, account->uID);
		return true;
	}
	return false;
}

void sendDisplayNameUpdates(const AccountInfo *pAccount)
{
	AccountServer_SendDisplayNameUpdate(0, pAccount);
	SendDisplayNameToAllProxies(pAccount);
}

void trChangeName_CB(TransactionReturnVal *returnVal, char *name)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		//printf ("Name '%s' updated\n", name);
		AccountInfo *account = findAccountByDisplayName(name);
		if (account)
		{
			sendDisplayNameUpdates(account);
		}
	}
	else
	{
		//printf ("Name '%s' failed to update\n", name);
	}
	if (name)
		free(name);
}

bool changeAccountDisplayName(AccountInfo *account, const char *displayName, TransactionReturnCallback transCB, void *data)
{
	if (!account || !displayName || !displayName[0])
		return false;
	if (strcmp(displayName, account->displayName) == 0)
		return true;

	accountLog(account, "Attempted to change display name from %s to %s.", account->displayName, displayName);

	if (!transCB) 
	{
		transCB = trChangeName_CB;
		data = strdup(displayName);
	}
    AutoTrans_trAccountChangeDisplayName(objCreateManagedReturnVal(transCB, data), 
		objServerType(), GLOBALTYPE_ACCOUNT, account->uID, displayName);
	return true;
}

bool changeAccountName(AccountInfo *account, const char *accountName, TransactionReturnCallback transCB)
{
	if (!account || !accountName || !accountName[0] || StringIsInvalidAccountName(accountName, 9))
		return false;
	if (stricmp(accountName, account->accountName) == 0)
		return true;

	accountLog(account, "Attempted to change account name from %s to %s.", account->accountName, accountName);

	if (!transCB) transCB = trChangeName_CB;
    AutoTrans_trAccountChangeAccountName(objCreateManagedReturnVal(transCB, strdup(accountName)), 
		objServerType(), GLOBALTYPE_ACCOUNT, account->uID, accountName);
	return true;
}

bool changeAccountEmail(AccountInfo *account, const char *email, bool bGenerateActivator, TransactionReturnCallback transCB)
{
	if (!account)
		return false;
	if (account->personalInfo.email && stricmp(email, account->personalInfo.email) == 0)
		return true;

	accountLog(account, "Changed e-mail from %s to %s.", account->personalInfo.email, email);

	if (!transCB) transCB = trChangeName_CB;
    AutoTrans_trAccountChangeEmail(objCreateManagedReturnVal(transCB, strdup(email)), 
		objServerType(), GLOBALTYPE_ACCOUNT, account->uID, email, bGenerateActivator);
	return true;
}

bool setShippingAddress(AccountInfo *account, 
						 const char * address1, const char * address2,
						 const char * city, const char * district,
						 const char * postalCode, const char * country,
						 const char * phone)
{
	if (account)
	{
		AutoTrans_trAccountSetShippingAddress(NULL,  objServerType(), GLOBALTYPE_ACCOUNT, account->uID,
			address1, address2, city, district, postalCode, country, phone);
		return true;
	}
	return false;
}

void changeAccountLocale(U32 uAccountID, SA_PARAM_NN_STR const char *pLocale)
{
	AutoTrans_trAccountSetLocale(NULL, objServerType(), GLOBALTYPE_ACCOUNT, uAccountID, pLocale);
}

void accountSetForbiddenPaymentMethods(U32 uAccountID, PaymentMethodType eTypes)
{
	AutoTrans_trAccountSetForbiddenPaymentMethods(NULL, objServerType(), GLOBALTYPE_ACCOUNT, uAccountID, eTypes);
}

void changeAccountCurrency(U32 uAccountID, SA_PARAM_NN_STR const char *pCurrency)
{
	AutoTrans_trAccountSetCurrency(NULL, objServerType(), GLOBALTYPE_ACCOUNT, uAccountID, pCurrency);
}

void changeAccountCountry(U32 uAccountID, SA_PARAM_NN_STR const char *pCountry)
{
	AutoTrans_trAccountSetCountry(NULL, objServerType(), GLOBALTYPE_ACCOUNT, uAccountID, pCountry);
}

// Normalize an answer string for a secret question by performing the following transformations.
//   -Remove non-alphanumeric characters, such as spaces and punctuation
//   -Fold case
//   -Merge equivalent Unicode characters
//   -Expand ligatures
//	 -Converts to normal form that eliminates diacritics, case, and special character attributes.
static void normalizeAnswer(char **esAnswer)
{
	LPWSTR oldstring, newstring;
	int result;
	int size = estrLength(esAnswer) + 4;  // Null, 3 key delimiters.
	int bytesize = size * sizeof(WCHAR);
	WCHAR *oldptr, *newptr;
	char *sortkey;

	// Make sure this is a valid string.
	if (!estrLength(esAnswer) || !UTF8StringIsValid(*esAnswer, NULL))
	{
		estrClear(esAnswer);
		return;
	}

	// Convert string to UTF-16.
	oldstring = callocStructs(WCHAR, size);
	newstring = callocStructs(WCHAR, size);
	result = MultiByteToWideChar(CP_UTF8, 0, *esAnswer, size, oldstring, size);
	if (!result)
	{
		free(oldstring);
		free(newstring);
		estrClear(esAnswer);
		return;
	}

	// Remove non-alphanumeric characters.
	newptr = newstring;
	for (oldptr = oldstring; *oldptr; ++oldptr)
		if (iswalnum(*oldptr))
			*newptr++ = *oldptr;
	*newptr = 0;
	SWAPP(oldstring, newstring);

	// Perform Windows character folding.
	result = FoldStringW(MAP_FOLDCZONE | MAP_FOLDDIGITS | MAP_EXPAND_LIGATURES, oldstring, -1, newstring, size);
	if (!result)
	{
		free(oldstring);
		free(newstring);
		estrClear(esAnswer);
		return;
	}
	SWAPP(oldstring, newstring);

	// Convert to simplified characters.
	result = LCMapStringW(MAKELCID(MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), SORT_DEFAULT),
		LCMAP_SIMPLIFIED_CHINESE, oldstring, -1, newstring, size);
	if (!result)
	{
		free(oldstring);
		free(newstring);
		estrClear(esAnswer);
		return;
	}

	// Make sure the string is still long enough.
	if (wcslen(oldstring) < 4)
	{
		free(oldstring);
		free(newstring);
		estrClear(esAnswer);
		return;
	}

	// Create Unicode sort key.
	result = LCMapStringW(MAKELCID(MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), SORT_DEFAULT),
		LCMAP_SORTKEY | NORM_IGNORECASE | NORM_IGNOREKANATYPE | NORM_IGNORENONSPACE | NORM_IGNORESYMBOLS | NORM_IGNOREWIDTH,
		oldstring, -1, newstring, bytesize);
	if (!result)
	{
		free(oldstring);
		free(newstring);
		estrClear(esAnswer);
		return;
	}
	SWAPP(oldstring, newstring);

	// Extract base character part of sort key.
	for (sortkey = (char *)oldstring; *sortkey != 1; ++sortkey)
		if (*sortkey == 0)
			*sortkey = 1;
	*sortkey = 0;

	// Return result.
	estrCopy2(esAnswer, (char *)oldstring);
	free(oldstring);
	free(newstring);
}

// Update the password secret questions and answers for an account
// Return true if set successfully.
// If the questions or answers are not acceptable for some reason, *ppBad will be an array (which the caller must destroy) that corresponds
// to the questions, with a true value for each question that was not OK.
// If bValidateOnly is true, do not actually set the secret questions.
// If a secret question is NULL or the empty string, it will not be updated.
bool setSecretQuestions(SA_PARAM_OP_VALID AccountInfo *account, SA_PARAM_NN_VALID QuestionsAnswers *pQuestionsAnswers,
						SA_PARAM_OP_VALID INT_EARRAY *ppBad, bool bValidateOnly)
{
	bool result = true;
	char *normalized = NULL;

	devassert(pQuestionsAnswers);
	devassert(account || bValidateOnly);

	// Make sure that we're passed two non-empty arrays of equal length.
	if (!eaSize(&pQuestionsAnswers->questions) || eaSize(&pQuestionsAnswers->questions) != eaSize(&pQuestionsAnswers->questions))
		return false;

	// Existing questions beyond those updated are left as-is.
	if (!bValidateOnly && eaSize(&pQuestionsAnswers->questions) < eaSize(&account->personalInfo.secretQuestionsAnswers.questions))
	{
		eaSetSize(&pQuestionsAnswers->questions, eaSize(&account->personalInfo.secretQuestionsAnswers.questions));
		eaSetSize(&pQuestionsAnswers->answers, eaSize(&account->personalInfo.secretQuestionsAnswers.questions));
	}

	// Validate and normalize each question-answer pair.
	estrStackCreate(&normalized);
	EARRAY_FOREACH_BEGIN(pQuestionsAnswers->questions, i);
		char hashedAnswer[128];
		bool ok = true;

		// Set a secret question.
		if (estrLength(&pQuestionsAnswers->questions[i]))
		{
			// Normalize answer and hash it.
			estrCopy(&normalized, &pQuestionsAnswers->answers[i]);
			normalizeAnswer(&normalized);
			if (estrLength(&normalized) < 4)
				ok = false;
			else if (!bValidateOnly)
				accountHashPassword(normalized, hashedAnswer);

			// Save the hash, or record this entry as bad.
			if (ok)
			{
				if (!bValidateOnly)
					estrCopy2(&pQuestionsAnswers->answers[i], hashedAnswer);
			}
			else
			{
				if (ppBad)
				{
					eaiClear(ppBad);
					eaiSetSize(ppBad, eaSize(&pQuestionsAnswers->questions));
					(*ppBad)[i] = true;
				}
				result = false;
			}
		}

		// Leave a secret question alone.
		else
		{
			if (!bValidateOnly && i < eaSize(&account->personalInfo.secretQuestionsAnswers.questions))
			{
				estrCopy(&pQuestionsAnswers->questions[i], &account->personalInfo.secretQuestionsAnswers.questions[i]);
				estrCopy(&pQuestionsAnswers->answers[i], &account->personalInfo.secretQuestionsAnswers.answers[i]);
			}
		}

	EARRAY_FOREACH_END;

	// Commit new secret questions and answers.
	if (!bValidateOnly && result)
	{
		accountLog(account, "Changed secret questions.");
		AutoTrans_trAccountSetSecretQuestions(NULL, objServerType(), GLOBALTYPE_ACCOUNT, account->uID, pQuestionsAnswers);
	}

	estrDestroy(&normalized);
	return result;
}

// Check password secret answers against the ones in the account database.
// Return true if they all match, and false otherwise.
// If ppiCorrect is set, corresponding array indices will be set to true for each correct answer, and false if the answer is incorrect.
bool checkSecretAnswers(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_VALID STRING_EARRAY *ppAnswers, SA_PARAM_OP_VALID INT_EARRAY *ppiCorrect)
{
	const AccountPersonalInfo *personalInfo = &account->personalInfo;
	bool success = true;

	devassert(account && ppAnswers);

	// If the number of questions and answers don't match, fail.
	if (eaSize(&personalInfo->secretQuestionsAnswers.answers) != eaSize(ppAnswers))
		return false;

	if (ppiCorrect)
		eaiClear(ppiCorrect);

	// Check each answer.
	EARRAY_FOREACH_BEGIN(personalInfo->secretQuestionsAnswers.answers, i);
		char hashedAnswer[128];
		bool matched;

		// Normalize the provided answer and hash it.
		normalizeAnswer(&(*ppAnswers)[i]);
		accountHashPassword((*ppAnswers)[i], hashedAnswer);
		estrCopy2(&(*ppAnswers)[i], hashedAnswer);

		// Check the answer.
		matched = !strcmp((*ppAnswers)[i], personalInfo->secretQuestionsAnswers.answers[i]);
		if (ppiCorrect)
			eaiPush(ppiCorrect, matched);
		success = success && matched;

	EARRAY_FOREACH_END;
	return success;
}

AUTO_STRUCT;
typedef struct PaymentMethodCacheUpdate
{
	EARRAY_OF(CachedPaymentMethod) eaPaymentMethods;
} PaymentMethodCacheUpdate;

// Takes ownership of eaPaymentMethods and updates them on the account
void updatePaymentMethodCache(U32 uAccountID, SA_PARAM_OP_VALID EARRAY_OF(CachedPaymentMethod) eaPaymentMethods)
{
	PaymentMethodCacheUpdate update = {0};

	update.eaPaymentMethods = eaPaymentMethods;
	AutoTrans_trAccountUpdatePaymentMethodCache(NULL, objServerType(), GLOBALTYPE_ACCOUNT, uAccountID, &update);
}

static void copyPermissionsToFake(AccountInfo *account, CONST_EARRAY_OF(AccountPermission) src, AccountPermissionStruct ***dest)
{
	int i;

	PERFINFO_AUTO_START_FUNC();
	for (i=0; i<eaSize(&src); i++)
	{
		AccountPermissionStruct *pPermission = StructCreate(parse_AccountPermissionStruct);
		estrCopy2(&pPermission->pProductName, src[i]->pProductName);
		estrCopy2(&pPermission->pPermissionString, src[i]->pPermissionString);
		pPermission->iAccessLevel = src[i]->iAccessLevel;
		eaPush(dest, pPermission);		
	}
	PERFINFO_AUTO_STOP();
}

void accountSetBillingEnabled(SA_PARAM_NN_VALID AccountInfo *pAccount)
{
	if (!pAccount || pAccount->bBillingEnabled) return;

	accountLog(pAccount, "Account set as being billing enabled.");

	AutoTrans_trAccountSetBillingEnabled(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID, true);
}

void accountSetBillingDisabled(SA_PARAM_NN_VALID AccountInfo *pAccount)
{
	if (!pAccount || !pAccount->bBillingEnabled) return;

	accountLog(pAccount, "Account set as being billing disabled.");

	AutoTrans_trAccountSetBillingEnabled(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID, false);
}

void accountSetBilled(SA_PARAM_NN_VALID AccountInfo *pAccount)
{
	if (!verify(pAccount)) return;

	if (pAccount->bBilled) return; // already marked; no work to do

	accountLog(pAccount, "Account marked as having been billed.");

	AutoTrans_trAccountSetBilled(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID, false);
}

bool confirmLoginPassword(AccountInfo *pAccount, const char *pPassword, const char *oldMd5Password, const char *pPasswordSaltedWithAccountName, U32 salt)
{
	if (!pAccount) return false;

	PERFINFO_AUTO_START_FUNC();

	if (accountNeedsPasswordOrEncryptionFixup(pAccount))
	{
		//will trigger an alert internally if it fails
		if (!accountDoPasswordOrEncryptionFixup(pAccount))
		{
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}
	}

	accountDecryptPasswordIfNecessary(pAccount);

	if (pPasswordSaltedWithAccountName)
	{
		char saltedPassword[MAX_PASSWORD];

		if (!pAccount->passwordInfo || !pAccount->passwordInfo->password_ForRAM[0])
		{
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}

		switch(eCurPasswordVersion)
		{
			//the password we have been passed in is hashed with account name and temp salt, the password
			//we have stored is just old-style hashed
		xcase PASSWORDVERSION_HASHED_UNSALTED:
			accountAddAccountNameThenNewStyleSaltToHashedPassword(pAccount->passwordInfo->password_ForRAM, pAccount->accountName, salt, saltedPassword);
			if (strcmp(saltedPassword, pPasswordSaltedWithAccountName) == 0)
			{
				PERFINFO_AUTO_STOP_FUNC();
				return true;
			}
			//the password we have been passed in is hashed with account name and temp salt, the password
			//we have stored is hased with just account name
		xcase PASSWORDVERSION_HASHED_SALTED_WITH_ACTNAME:
			accountAddNewStyleSaltToHashedPassword(pAccount->passwordInfo->password_ForRAM, salt, saltedPassword);
			if (strcmp(saltedPassword, pPasswordSaltedWithAccountName) == 0)
			{
				PERFINFO_AUTO_STOP_FUNC();
				return true;
			}


		xdefault:
			assertmsgf(0, "Don't know how to do confirmLoginPassword for password type %s", GetPasswordVersionName(eCurPasswordVersion));
		}

	}

	// Attempt to authenticate using the regular SHA-256 password.
	if (pPassword)
	{
		char saltedPassword[MAX_PASSWORD];

		if (!pAccount->passwordInfo || !pAccount->passwordInfo->password_ForRAM[0])
		{
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}

		switch(eCurPasswordVersion)
		{
			//the password we have been passed in is unsalted hashed, then re-hashed with the temporary salt,
			//the password we have stored is unsalted hashed
		xcase PASSWORDVERSION_HASHED_UNSALTED:
			accountAddSaltToHashedPassword(pAccount->passwordInfo->password_ForRAM, salt, saltedPassword);
			if (strcmp(saltedPassword, pPassword) == 0)
			{
				devassertmsgf(!pPasswordSaltedWithAccountName, "Account %s fell back on old password hash, even though it sent a new hash - unsalted password", pAccount->accountName);
				PERFINFO_AUTO_STOP_FUNC();
				return true;
			}
		xcase PASSWORDVERSION_HASHED_SALTED_WITH_ACTNAME:
			//the password we have been passed in is unsalted hashed, then might be re-hashed with the temporary salt,
			//the password we have stored is hashed with account name, this case is illegal because we can't
			//get backwards from hashed-with-account-name to unsalted hashed
			//but if the salt is 0, everything is okay
			if (salt)
			{
				if (pAccount->pBackupPasswordInfo && pAccount->pBackupPasswordInfo->ePasswordVersion == PASSWORDVERSION_HASHED_UNSALTED)
				{
					if (!sbDontAllowLoginWithBackupPassword)
					{
						accountDecryptBackupPasswordIfNecessary(pAccount);
						accountAddSaltToHashedPassword(pAccount->pBackupPasswordInfo->password_ForRAM, salt, saltedPassword);
						if (strcmp(saltedPassword, pPassword) == 0)
						{
							devassertmsgf(!pPasswordSaltedWithAccountName, "Account %s fell back on old password hash, even though it sent a new hash - unsalted backup password", pAccount->accountName);
							PERFINFO_AUTO_STOP_FUNC();
							return true;
						}
						else
						{
							PERFINFO_AUTO_STOP_FUNC();
							return false;
						}
					}
				}

				if (!pPasswordSaltedWithAccountName)
				{
					CRITICAL_NETOPS_ALERT("SALT_INCOMPATIBILITY", "Someone is trying to validate account %s with an old-style unsalted hashed password with temporary salt;"
						" we cannot reconcile this with the new-style passwords salted with account name",
						pAccount->accountName);
				}
			}
			else
			{
				accountAddAccountNameThenNewStyleSaltToHashedPassword(pPassword, pAccount->accountName, salt, saltedPassword);
				if (strcmp(pAccount->passwordInfo->password_ForRAM, saltedPassword) == 0)
				{
					devassertmsgf(!pPasswordSaltedWithAccountName, "Account %s fell back on old password hash, even though it sent a new hash - salted password", pAccount->accountName);
					PERFINFO_AUTO_STOP_FUNC();
					return true;
				}
			}

		xdefault:
			assertmsgf(0, "Don't know how to do confirmLoginPassword for password type %s", GetPasswordVersionName(eCurPasswordVersion));
		}
	}

	// If there's an MD5 password, try to authenticate with that.
	if (oldMd5Password)
	{
		// Hexadecimal strings are case-insensitive, and MD5 passwords are never salted
		if (pAccount->passwordInfo && stricmp(pAccount->passwordInfo->password_ForRAM, oldMd5Password) == 0)
		{
			// Migrate to new SHA-256 password.
			if (pPassword)
			{
				objRequestTransactionSimplef(NULL, GLOBALTYPE_ACCOUNT, pAccount->uID, "SetPassword", 
					"set password = \"%s\"", pPassword);
			}
			PERFINFO_AUTO_STOP_FUNC();
			return true;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return false;
}

AccountTicketSigned * createTicket(AccountLoginType eLoginType, AccountInfo *account, bool bMachineRestricted)
{
	AccountTicket ticket = {0};
	AccountTicketSigned *ticketSigned = NULL;
	U32 uTime;
	static char *pTicketTPI = NULL;
	static U32 uCRC = 0;

	PERFINFO_AUTO_START_FUNC();

	uTime = timeSecondsSince2000();

	StructInit(parse_AccountTicket, &ticket);
	
	strcpy(ticket.accountName, account->accountName);

	if (account->displayName)
		strcpy(ticket.displayName, account->displayName);

	if (findPWCommonAccountbyName(account->pPWAccountName))
		ticket.pwAccountName = (char *)account->pPWAccountName;

	ticket.bMachineRestricted = bMachineRestricted;
	ticket.bSavingNext = bMachineRestricted && accountMachineSaveNextClient(account, uTime);
	ticket.eLoginType = eLoginType;

	accountConstructPermissions(account);

	copyPermissionsToFake(account, account->ppPermissionCache, &ticket.ppPermissions);
	ticket.accountID = account->uID;
	ticket.bInvalidDisplayName = account->flags & ACCOUNT_FLAG_INVALID_DISPLAYNAME;
	ticket.uExpirationTime = uTime + ACCOUNT_TICKET_LIFESPAN;

	if (!pTicketTPI)
	{
		const char *pTableName = ParserGetTableName(parse_AccountTicket);
		int ret = ParseTableWriteText(&pTicketTPI, parse_AccountTicket, pTableName ? pTableName : "unknown", PARSETABLESENDFLAG_MAINTAIN_BITFIELDS);
		devassert(ret);
		uCRC = ParseTableCRC(parse_AccountTicket, NULL, 0);
	}

	ticketSigned = StructCreate(parse_AccountTicketSigned);
	ticketSigned->uExpirationTime = ticket.uExpirationTime;
	ParserWriteText(&ticketSigned->ticketText, parse_AccountTicket, &ticket, 0, 0, 0);
	ticketSigned->uTicketCRC = uCRC;
	estrCopy2(&ticketSigned->strTicketTPI, pTicketTPI);

	ticket.pwAccountName = NULL;
	StructDeInit(parse_AccountTicket, &ticket);

	PERFINFO_AUTO_STOP_FUNC();

	return ticketSigned;
}

// --------------------------------------
// Commands

AUTO_COMMAND ACMD_NAME(flagInvalidDisplayName) ACMD_CATEGORY(Accounts);
int accountCmd_FlagInvalidDisplayName(U32 uID)
{
	AccountInfo *account = findAccountByID(uID);
	if (account)
		accountFlagInvalidDisplayName(account, !(account->flags & ACCOUNT_FLAG_INVALID_DISPLAYNAME));
	return 0;
}

AUTO_COMMAND ACMD_NAME(renameAccount) ACMD_CATEGORY(Accounts);
int accountRename(U32 uID, const char *newAccountName, const char *newDisplayName)
{
	AccountInfo *account = findAccountByID(uID);

	if (!account)
		return 1;
	if (newAccountName && newAccountName[0])
		if (!changeAccountName(account, newAccountName, NULL))
			return 2;
	if (newDisplayName && newDisplayName[0])
		if (!changeAccountDisplayName(account, newDisplayName, NULL, NULL))
			return 3;

	return 0;
}

// ONLY intended for usage in very specific fixup cases where you have the exact hash needed, and don't want any normal AccountEncryption stuff to happen
AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Passwordinfo");
enumTransactionOutcome trForceSetPasswordHash(ATR_ARGS, NOCONST(AccountInfo) *pAccount, const char *pHash)
{
	if (!pAccount->passwordInfo)
	{
		pAccount->passwordInfo = StructCreateNoConst(parse_AccountPasswordInfo);
	}

	pAccount->passwordInfo->ePasswordVersion = eCurPasswordVersion;
	pAccount->passwordInfo->iEncryptionKeyIndex = 0;
	strcpy(pAccount->passwordInfo->password_ForDisk, pHash);
	return TRANSACTION_OUTCOME_SUCCESS;
}

void requestPasswordChangeTransaction(AccountInfo *pAccount, const char *pHash)
{
	AutoTrans_trAccountSetPasswordHashed(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID, pHash);
	
	//after anythign which might change password_ForRAM, have to set it back to empty in the real container so it
	//will be re-decrypted next time it's needed
	if (pAccount->passwordInfo)
	{
		pAccount->passwordInfo->password_ForRAM[0] = 0;
	}
	if (pAccount->pBackupPasswordInfo)
		pAccount->pBackupPasswordInfo->password_ForRAM[0] = 0;
}

void forceChangePasswordHash(AccountInfo* pAccount, const char *pHashedPassword)
{
	requestPasswordChangeTransaction(pAccount, pHashedPassword);
}

// Set the user's password
int forceChangePasswordInternal(AccountInfo *account, const char *pNewPassword)
{
	if (account && strlen(pNewPassword) < MAX_PASSWORD_PLAINTEXT)
	{
		char hashedPassword[MAX_PASSWORD] = "";
		accountHashPassword(pNewPassword, hashedPassword);
		if (hashedPassword[0] == 0)
		{
			devassertmsg(0, "Login password failure");
			return 0;
		}
		if (account->bPWAutoCreated && nullStr(account->pPWAccountName))
			AccountIntegration_ClearPWAutoCreatedFlag(account);
		requestPasswordChangeTransaction(account, hashedPassword);
		return 1;
	}
	return 0;
}

// Set the user's password
AUTO_COMMAND ACMD_NAME(changePassword) ACMD_CATEGORY(Accounts);
int forceChangePassword(U32 uID, const char *pNewPassword)
{
	AccountInfo *account = findAccountByID(uID);

	if (account)
		return forceChangePasswordInternal(account, pNewPassword);

	return 0;
}

// Flag or unflag the user account for internal Cryptic use only
AUTO_COMMAND ACMD_CATEGORY(Accounts);
int setInternalUse(U32 uID, bool bInternalUse)
{
	objRequestTransactionSimplef(astrRequireSuccess(NULL), GLOBALTYPE_ACCOUNT, uID, "SetInternalUse", "set bInternalUseLogin = \"%d\"", bInternalUse);
	return 1; 
}

// Enable or disable login access to this account.
AUTO_COMMAND;
int setLoginDisabled(U32 uID, bool bLoginDisabled)
{
	AccountInfo *account = findAccountByID(uID);

	// Validate parameters.
	if (!account)
	{
		verify(0);
		return 0;
	}
	bLoginDisabled = !!bLoginDisabled;

	// Update account.
	objRequestTransactionSimplef(astrRequireSuccess(NULL), GLOBALTYPE_ACCOUNT, uID, "SetLoginDisabled", "set bLoginDisabled = \"%d\"", bLoginDisabled);
	accountClearPermissionsCache(account);

	return 1; 
}

AUTO_COMMAND ACMD_NAME(setPersonalInfo) ACMD_CATEGORY(Accounts);
int accountSetPersonalInfo(U32 uID, const char *pFirstName, const char *pLastName, const char *pEmail)
{
	Container *con = objGetContainer(GLOBALTYPE_ACCOUNT, uID);
	AccountInfo *pAccount = (AccountInfo*) con->containerData;

	if (pAccount)
	{
		if (pFirstName && pFirstName[0])
		{
			accountLog(pAccount, "Changed first name from %s to %s.",
				pAccount->personalInfo.firstName, pFirstName);

			objRequestTransactionSimplef(NULL, GLOBALTYPE_ACCOUNT, pAccount->uID, "SetFirstName", 
				"set personalInfo.firstName = \"%s\"", pFirstName);
		}
		if (pLastName && pLastName[0])
		{
			accountLog(pAccount, "Changed last name from %s to %s.",
				pAccount->personalInfo.lastName, pLastName);

			objRequestTransactionSimplef(NULL, GLOBALTYPE_ACCOUNT, pAccount->uID, "SetLastName", 
				"set personalInfo.lastName = \"%s\"", pLastName);
		}
		if (pEmail && pEmail[0])
		{
			accountLog(pAccount, "Changed e-mail from %s to %s.",
				pAccount->personalInfo.email, pEmail);

			objRequestTransactionSimplef(NULL, GLOBALTYPE_ACCOUNT, pAccount->uID, "SetEmail", 
				"set personalInfo.email = \"%s\"", pEmail);
		}
	}
	return 0;
}

int accountChangePersonalInfo(const char *pAccountName, const char *pFirstName, const char *pLastName, const char *pEmail)
{
	AccountInfo *pAccount = findAccountByName(pAccountName);
	if (pAccount)
		return accountSetPersonalInfo(pAccount->uID, pFirstName, pLastName, pEmail);
	return 0;
}

int parseShardPermissionString_s(AccountPermission *permission, char *buffer, size_t size)
{
	char *shardNames = strstri(permission->pPermissionString, ACCOUNT_PERMISSION_SHARD_PREFIX);
	int copied = 0;
	if (shardNames)
	{
		char * endShardNames = strstri(shardNames, ";");
		int len = endShardNames ? 
			endShardNames - shardNames - ACCOUNT_PERMISSION_SHARD_PREFIX_LEN : 
		(int) strlen(shardNames) - ACCOUNT_PERMISSION_SHARD_PREFIX_LEN;
		shardNames += ACCOUNT_PERMISSION_SHARD_PREFIX_LEN; // advance past the "shard:"
		
		if (strncpy_s(buffer, size, shardNames, len))
			return 0;
		return len;
	}
	else // assume entire string is shard category filter
	{
		if (strcpy_s(buffer, size, permission->pPermissionString))
			return 0;
		return (int) strlen(permission->pPermissionString);
	}
}

// Associate a product with an account (returns true on success)
static bool accountAssociateProduct(SA_PARAM_NN_VALID AccountInfo *pAccount,
									SA_PARAM_NN_VALID const ProductContainer *pProduct,
									SA_PARAM_OP_STR const char *pKey,
								    SA_PARAM_OP_STR const char *pReferrer)
{
	if (!verify(pAccount)) return false;
	if (!verify(pProduct)) return false;

	PERFINFO_AUTO_START_FUNC();

	AutoTrans_trAccountAddProduct(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID, pProduct->pName, pKey, pReferrer);
	
	accountClearPermissionsCache(pAccount);

	PERFINFO_AUTO_STOP_FUNC();

	return true;
}

static int accountProductUnique(const AccountInfo *account, const ProductContainer *product)
{
	if (!account || !product) return 2;
	if (product->uFlags & PRODUCT_DONT_ASSOCIATE) return 0; // Doesn't matter if it's unique, return success

	PERFINFO_AUTO_START_FUNC();
	EARRAY_CONST_FOREACH_BEGIN(account->ppProducts, i, s);
		if (stricmp(account->ppProducts[i]->name, product->pName) == 0) return 1;
	EARRAY_FOREACH_END;
	PERFINFO_AUTO_STOP();

	return 0; // success
}

ActivateProductResult accountActivateProductLock(AccountInfo *pAccount,
												 const ProductContainer *pProduct,
												 const char *pProxy,
												 const char *pCluster,
												 const char *pEnvironment,
											     const char *pReferrer,
												 AccountProxyProductActivation **ppOutActivation)
{
	ActivateProductResult eResult = APR_Success;
	AccountProxyKeyValueInfoList *KVList = NULL;
	char **keysUsedInRequirements = NULL;

	if (!verify(pAccount)) return APR_InvalidParameter;
	if (!verify(pProduct)) return APR_InvalidParameter;
	if (!verify(ppOutActivation)) return APR_InvalidParameter;

	PERFINFO_AUTO_START_FUNC();
	PERFINFO_AUTO_START("Sanity Checks", 1);

	// Make sure they don't already have the internal sub this product might give
	if (pProduct->pInternalSubGranted && findInternalSub(pAccount->uID, pProduct->pInternalSubGranted))
	{
		eResult = APR_InternalSubAlreadyExistsOnAccount;
		goto out;
	}

	// Make sure that the person does not have this product associated with their account
	if (accountProductUnique(pAccount, pProduct))
	{
		eResult = APR_ProductAlreadyAssociated;
		goto out;
	}

	// Get a list of key-values used
	KVList = GetProxyKeyValueList(pAccount);
	if (!AccountProxyKeysMeetRequirements(KVList, pProduct->ppPrerequisites, pProxy, pCluster, pEnvironment, NULL, &keysUsedInRequirements))
	{
		StructDestroy(parse_AccountProxyKeyValueInfoList, KVList);
		eResult = APR_PrerequisiteFailure;
		goto out;
	}
	StructDestroy(parse_AccountProxyKeyValueInfoList, KVList);

	// Create the activation structure
	*ppOutActivation = StructCreateNoConst(parse_AccountProxyProductActivation);
	if (!devassert(*ppOutActivation))
	{
		eResult = APR_Failure;
		goto out;
	}

	if (pReferrer && *pReferrer)
	{
		const AccountInfo *pReferrerAccount = findAccountByDisplayName(pReferrer);
		if (pReferrerAccount && stricmp_safe(pReferrerAccount->accountName, pAccount->accountName))
		{
			(*ppOutActivation)->pReferrer = strdup(pReferrer);
		}
		else
		{
			eResult = APR_InvalidReferrer;
			goto out;
		}
	}

	eaIndexedEnableNoConst(&(*ppOutActivation)->ppKeyLocks, parse_AccountProxyKeyLockPair);

	// Loop through all key/value changes and lock the keys
	PERFINFO_AUTO_START("Lock Changed KVs", 1);
	EARRAY_CONST_FOREACH_BEGIN(pProduct->ppKeyValueChanges, iCurChange, iNumChanges);
	{
		ProductKeyValueChangeContainer *change = pProduct->ppKeyValueChanges[iCurChange];
		const char *pKey = NULL;
		char *lock = NULL;
		AccountProxyKeyLockPair *pair = NULL;

		if (!devassert(change)) continue;

		pKey = AccountProxySubstituteKeyTokens(change->pKey, pProxy, pCluster, pEnvironment);

		if (change->change) // Should the key value change cause an "int change" or "int set"?
		{
			if (AccountKeyValue_Change(pAccount, pKey, change->pValue ? atoi64(change->pValue) : 0, &lock) != AKV_SUCCESS)
			{
				eResult = APR_KeyValueLockFailure;
				goto out;
			}
		}
		else if (AccountKeyValue_Set(pAccount, pKey, change->pValue ? atoi(change->pValue) : 0, &lock) != AKV_SUCCESS)
		{
			eResult = APR_KeyValueLockFailure;
			goto out;
		}

		pair = StructCreateNoConst(parse_AccountProxyKeyLockPair);
		estrCopy2(&pair->pKey, pKey);
		pair->pLock = lock;
		eaPush(&(*ppOutActivation)->ppKeyLocks, pair);
	}
	EARRAY_FOREACH_END;

	// Lock all keys used in requirements
	if (keysUsedInRequirements)
	{
		PERFINFO_AUTO_STOP_START("Lock Prereq KVs", 1);
		EARRAY_CONST_FOREACH_BEGIN(keysUsedInRequirements, iCurKey, iNumKeys);
		{
			bool found = false;
			const char *keyUsed = keysUsedInRequirements[iCurKey];
			AccountProxyKeyLockPair *pPair = eaIndexedGetUsingString(&(*ppOutActivation)->ppKeyLocks, keyUsed);

			if (!pPair)
			{
				char *lock = NULL;
				AccountProxyKeyLockPair *pair = NULL;

				if (AccountKeyValue_Lock(pAccount, keyUsed, &lock) != AKV_SUCCESS)
				{
					eResult = APR_KeyValueLockFailure;
					goto out;
				}

				pPair = StructCreateNoConst(parse_AccountProxyKeyLockPair);
				estrCopy2(&pPair->pKey, keyUsed);
				pPair->pLock = lock;
				eaPush(&(*ppOutActivation)->ppKeyLocks, pPair);
			}
		}
		EARRAY_FOREACH_END;
	}

	// Lock the product key if needed
	if (pProduct->pActivationKeyPrefix && *pProduct->pActivationKeyPrefix)
	{
		ProductKey productKey;
		bool success;

		PERFINFO_AUTO_STOP_START("Lock Product Key", 1);
		success = findUndistributedProductKey(&productKey, pProduct->pActivationKeyPrefix);

		if (!success)
		{
			AssertOrAlert("ACCOUNT_SERVER_NO_DISTRIBUTION_KEYS", "Unable to find keys to distribute in group %s for product %s.",
				pProduct->pActivationKeyPrefix, pProduct->pName);
			eResult = APR_ProductKeyDistributionLockFailure;
			goto out;
		}
		else
		{
			char *keyName = NULL;
			ProductKeyResult result;

			// Create lock.
			estrStackCreate(&keyName);
			copyProductKeyName(&keyName, &productKey);
			result = distributeProductKeyLock(pAccount, keyName, &(*ppOutActivation)->uActivationKeyLock);

			switch (result)
			{
			xcase PK_Success:
				estrCopy2(&(*ppOutActivation)->pActivationKey, keyName);
			xdefault:
				eResult = APR_ProductKeyDistributionLockFailure;
			}

			estrDestroy(&keyName);
		}
	}

out:

	// If the keys could not all be locked, unlock all the ones that were.
	if (eResult != APR_Success)
	{
		PERFINFO_AUTO_STOP_START("Rollback Keys", 1);
		if (*ppOutActivation)
		{
			EARRAY_CONST_FOREACH_BEGIN(pProduct->ppKeyValueChanges, iCurChange, iNumChanges);
			{
				ProductKeyValueChangeContainer *pChange = pProduct->ppKeyValueChanges[iCurChange];
				AccountProxyKeyLockPair *pLockPair = NULL;

				if (!devassert(pChange)) continue;

				pLockPair = eaIndexedGetUsingString(&(*ppOutActivation)->ppKeyLocks, pChange->pKey);

				if (!pLockPair) continue;

				AccountKeyValue_Rollback(pAccount, pLockPair->pKey, pLockPair->pLock);
			}
			EARRAY_FOREACH_END;

			StructDestroyNoConstSafe(parse_AccountProxyProductActivation, ppOutActivation);
		}

		PERFINFO_AUTO_STOP();
		accountLog(pAccount, "Product failed to lock: [product:%s] (%s)", pProduct->pName,
			StaticDefineIntRevLookupNonNull(ActivateProductResultEnum, eResult));
	}
	else
	{
		PERFINFO_AUTO_STOP();
		accountLog(pAccount, "Product locked: [product:%s]", pProduct->pName);
	}

	if (keysUsedInRequirements)
	{
		eaDestroyEString(&keysUsedInRequirements);
	}

	PERFINFO_AUTO_STOP_FUNC();
	return eResult;
}

// Returns which state a product can upgrade a recruit to, or RS_Invalid if none.
static RecruitState productUpgradesRecruit(SA_PARAM_NN_VALID const ProductContainer *pProduct)
{
	if (!verify(pProduct)) return RS_Invalid;

	if (productGrantsPermission(pProduct, gsUpgradePermission))
	{
		return RS_Upgraded; // Retail box
	}
	else if (pProduct->pInternalSubGranted && *pProduct->pInternalSubGranted)
	{
		return pProduct->uDaysGranted ? RS_Invalid : RS_Billed; // Lifetime subscriptions, if no days
	}
	else if (pProduct->uDaysGranted)
	{
		return RS_Billed; // Game card
	}

	return RS_Invalid;
}

static U32 determineSubHistoryStartTimeForProduct(SA_PARAM_NN_VALID AccountInfo *pAccount,
												  SA_PARAM_NN_VALID const ProductContainer *pProduct)
{
	U32 uTime = timeSecondsSince2000();

	if (!verify(pAccount)) return uTime;
	if (!verify(pProduct)) return uTime;

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(pAccount->ppSubscriptionHistory, iCurHistory, iNumHistory);
	{
		const SubscriptionHistory *pHistory = pAccount->ppSubscriptionHistory[iCurHistory];

		if (!devassert(pHistory)) continue;

		if (!stricmp_safe(pHistory->pProductInternalName, pProduct->pInternalName))
		{
			EARRAY_CONST_FOREACH_BEGIN(pHistory->eaArchivedEntries, iCurEntry, iNumEntries);
			{
				const SubscriptionHistoryEntry *pEntry = pHistory->eaArchivedEntries[iCurEntry];

				if (!devassert(pEntry)) continue;

				if (pEntry->eSubTimeSource == STS_Product && pEntry->uAdjustedEndSS2000 > uTime)
				{
					uTime = pEntry->uAdjustedEndSS2000;
				}
			}
			EARRAY_FOREACH_END;

			break;
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();

	return uTime;
}

ActivateProductResult accountActivateProductCommit(SA_PARAM_NN_VALID AccountInfo *pAccount,
												   SA_PARAM_NN_VALID const ProductContainer *pProduct,
												   SA_PARAM_OP_STR const char *pKey,
												   U32 uSuppressFlags,
												   SA_PARAM_NN_VALID AccountProxyProductActivation *pActivation,
												   SA_PARAM_OP_VALID ActivationInfo *pActivationInfo,
												   bool bSuppressFailLog)
{
	ActivateProductResult eResult = APR_Success;
	bool bAssociated = false;
	RecruitState eUpgradeTo = RS_Invalid;

	if (!verify(pAccount)) return APR_InvalidParameter;
	if (!verify(pProduct)) return APR_InvalidParameter;
	if (!verify(pActivation)) return APR_InvalidParameter;

	PERFINFO_AUTO_START_FUNC();

	// Commit the key-values
	PERFINFO_AUTO_START("Commit KVs", 1);
	EARRAY_CONST_FOREACH_BEGIN(pActivation->ppKeyLocks, iCurLockPair, iNumLocks);
	{
		AccountProxyKeyLockPair *pKeyValueLockPair = pActivation->ppKeyLocks[iCurLockPair];

		if (!devassert(pKeyValueLockPair))
		{
			eResult = APR_KeyValueCommitFailure;
			continue;
		}

		if (AccountKeyValue_Commit(pAccount, pKeyValueLockPair->pKey, pKeyValueLockPair->pLock) != AKV_SUCCESS)
		{
			eResult = APR_KeyValueCommitFailure;
		}
	}
	EARRAY_FOREACH_END;

	// Commit the key distribution if needed
	if (pActivation->uActivationKeyLock)
	{
		PERFINFO_AUTO_STOP_START("Commit Product Key", 1);
		if (!pActivation->pActivationKey ||
			!*pActivation->pActivationKey || 
			distributeProductKeyCommit(pAccount, pActivation->pActivationKey, pActivation->uActivationKeyLock) != PK_Success)
		{
			eResult = APR_ProductKeyDistributionCommitFailure;
		}
		else if (pActivationInfo)
		{
			estrCopy2(&pActivationInfo->pDistributedKey, pActivation->pActivationKey);
		}
	}

	// Give the internal sub if we have one to give and aren't supposed to ignore it
	if (!(uSuppressFlags & SUPPRESS_INTERNAL_SUB) && pProduct->pInternalSubGranted && *pProduct->pInternalSubGranted)
	{
		U32 uExpiration = pProduct->uDaysGranted ? 
			timeSecondsSince2000() + pProduct->uDaysGranted * SECONDS_PER_DAY : 0;
		PERFINFO_AUTO_STOP_START("Grant Internal Sub", 1);

		if (!internalSubCreate(pAccount, pProduct->pInternalSubGranted, uExpiration, pProduct->uID))
		{
			eResult = APR_InternalSubCreationFailure;
		}
	}

	// Associate the product
	if (!(pProduct->uFlags & PRODUCT_DONT_ASSOCIATE))
	{
		PERFINFO_AUTO_STOP_START("Associate Product", 1);
		if (!accountAssociateProduct(pAccount, pProduct, pKey, pActivation->pReferrer))
		{
			eResult = APR_CouldNotAssociateProduct;
		}
		else
		{
			bAssociated = true;
		}
	}

	if (pProduct->uFlags & PRODUCT_MARK_BILLED)
	{
		PERFINFO_AUTO_STOP_START("Mark Billed", 1);
		accountSetBilled(pAccount);
	}

	if (pActivation->pReferrer && *pActivation->pReferrer && pProduct->pReferredProduct && *pProduct->pReferredProduct)
	{
		const ProductContainer *pBonusProduct = findProductByName(pProduct->pReferredProduct);
		AccountInfo *pReferrer = findAccountByDisplayName(pActivation->pReferrer);

		PERFINFO_AUTO_STOP_START("Referrer Bonus", 1);
		if (devassert(pBonusProduct) && devassert(pReferrer))
		{
			ActivateProductResult eBonusResult = accountActivateProduct(pReferrer, pBonusProduct, NULL, NULL, NULL, NULL, NULL, 0, NULL, bSuppressFailLog);
			accountLog(pReferrer, "Attempted to activate a product for referring [account:%s]: %s",
				pAccount->accountName, StaticDefineIntRevLookupNonNull(ActivateProductResultEnum, eBonusResult));
		}
	}

	// Destroy the activation structure
	PERFINFO_AUTO_STOP();
	StructDestroyNoConst(parse_AccountProxyProductActivation, pActivation);
	pActivation = NULL;

	// Record the subscription history entry preemptively
	if (pProduct->uDaysGranted && !(pProduct->pInternalSubGranted && *pProduct->pInternalSubGranted))
	{
		U32 uStartTime;
		U32 uEndTime;

		PERFINFO_AUTO_START("Record Sub History", 1);
		uStartTime = determineSubHistoryStartTimeForProduct(pAccount, pProduct);
		uEndTime = uStartTime + pProduct->uDaysGranted * SECONDS_PER_DAY;

		accountArchiveSubscription(pAccount->uID,
			pProduct->pInternalName,
			NULL,
			NULL,
			uStartTime,
			uEndTime,
			STS_Product,
			SHER_Activation,
			SHEP_NOT_EXACT);
		PERFINFO_AUTO_STOP();
	}

	// Mark the recruit as upgraded if appropriate
	eUpgradeTo = productUpgradesRecruit(pProduct);
	if (eUpgradeTo != RS_Invalid)
	{
		PERFINFO_AUTO_START("Upgrade Recruit", 1);
		EARRAY_CONST_FOREACH_BEGIN(pAccount->eaRecruiters, iCurRecruiter, iNumRecruiters);
		{
			const RecruiterContainer * pRecruiter = pAccount->eaRecruiters[iCurRecruiter];

			if (!devassert(pRecruiter)) continue;

			if (!stricmp_safe(pRecruiter->pProductInternalName, pProduct->pInternalName))
			{
				AccountInfo *pRecruiterAccount = findAccountByID(pRecruiter->uAccountID);

				if (devassert(pRecruiterAccount))
				{
					accountUpgradeRecruit(pRecruiterAccount, pAccount->uID, pProduct->pInternalName, eUpgradeTo);
				}
			}
		}
		EARRAY_FOREACH_END;
		PERFINFO_AUTO_STOP();
	}

	// Log the result
	if (eResult == APR_Success)
	{
		accountLog(pAccount, "Product activated (%sassociated): [product:%s]", bAssociated ? "" : "not ", pProduct->pName);
	}
	else if (!bSuppressFailLog)
	{
		accountLog(pAccount, "Product activation failed (%sassociated): [product:%s] (%s)", bAssociated ? "" : "not ", pProduct->pName,
			StaticDefineIntRevLookupNonNull(ActivateProductResultEnum, eResult));
	}

	PERFINFO_AUTO_STOP_FUNC();

	return eResult;
}

ActivateProductResult accountActivateProduct(AccountInfo *pAccount,
											 const ProductContainer *pProduct,
											 const char *pProxy,
											 const char *pCluster,
											 const char *pEnvironment,
											 const char *pKey,
											 const char *pReferrer,
											 U32 uSuppressFlags,
											 ActivationInfo *pActivationInfo,
											 bool bSuppressFailLog)
{
	AccountProxyProductActivation *pActivation = NULL;
	ActivateProductResult eResult;
	
	PERFINFO_AUTO_START_FUNC();
	eResult = accountActivateProductLock(pAccount, pProduct, pProxy, pCluster, pEnvironment, pReferrer, &pActivation);

	if (eResult == APR_Success)
	{
		eResult = accountActivateProductCommit(pAccount, pProduct, pKey, uSuppressFlags, pActivation, pActivationInfo, bSuppressFailLog);
	}
	PERFINFO_AUTO_STOP();

	return eResult;
}

ActivateProductResult accountActivateProductRollback(AccountInfo *pAccount,
													 const ProductContainer *pProduct,
													 AccountProxyProductActivation *pActivation,
													 bool bSuppressFailLog)
{
	ActivateProductResult eResult = APR_Success;

	if (!verify(pAccount)) return APR_InvalidParameter;
	if (!verify(pProduct)) return APR_InvalidParameter;
	if (!verify(pActivation)) return APR_InvalidParameter;

	PERFINFO_AUTO_START_FUNC();

	// Rollback key-values
	PERFINFO_AUTO_START("Rollback KVs", 1);
	EARRAY_CONST_FOREACH_BEGIN(pActivation->ppKeyLocks, iCurLockPair, s);
	{
		AccountProxyKeyLockPair *pKeyValueLockPair = pActivation->ppKeyLocks[iCurLockPair];

		if (!devassert(pKeyValueLockPair))
		{
			eResult = APR_KeyValueRollbackFailure;
			continue;
		}
		
		if (AccountKeyValue_Rollback(pAccount, pKeyValueLockPair->pKey, pKeyValueLockPair->pLock) != AKV_SUCCESS)
		{
			eResult = APR_KeyValueRollbackFailure;
		}
	}
	EARRAY_FOREACH_END;

	// Rollback the key distribution if needed
	if (pActivation->uActivationKeyLock)
	{
		PERFINFO_AUTO_STOP_START("Rollback Product Key", 1);
		if (distributeProductKeyRollback(pAccount, pActivation->pActivationKey, pActivation->uActivationKeyLock) != PK_Success)
		{
			eResult = APR_ProductKeyDistributionRollbackFailure;
		}
	}

	// Cleanup the activation structure
	PERFINFO_AUTO_STOP();
	StructDestroyNoConst(parse_AccountProxyProductActivation, pActivation);
	pActivation = NULL;

	// Log the result
	if (eResult == APR_Success)
	{
		accountLog(pAccount, "Product activation rolled back: [product:%s]", pProduct->pName);
	}
	else if (!bSuppressFailLog)
	{
		accountLog(pAccount, "Product activation rollback failed: [product:%s] (%s)", pProduct->pName,
			StaticDefineIntRevLookupNonNull(ActivateProductResultEnum, eResult));
	}

	PERFINFO_AUTO_STOP_FUNC();

	return eResult;
}

int accountRemoveProduct(AccountInfo *account, const ProductContainer *product, const char *reason)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	int i,size;
	if (!product)
		return 2;

	size = eaSize(&account->ppProducts);
	for (i=0; i<size; i++)
	{
		AccountProductSub *psub = account->ppProducts[i];
		if (stricmp(psub->name, product->pName) == 0)
		{
			accountLog(account, "Product removed from account: [product:%s] %s", product->pName, NULL_TO_EMPTY(reason));

			AutoTrans_trAccountModifyProduct(NULL, objServerType(), GLOBALTYPE_ACCOUNT, account->uID, 
				product->pName, NULL, NULL, 1);
			return 0;
		}
	}
	return 1; // couldn't find
}

void accountsClearPermissionsCacheIfProductOwned(SA_PARAM_NN_STR const char *name)
{
	ContainerIterator iter = {0};
	Container *currCon = NULL;

	objInitContainerIteratorFromType(GLOBALTYPE_ACCOUNT, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		AccountInfo *pAccount = (AccountInfo*) currCon->containerData;
		int i;

		for (i=eaSize(&pAccount->ppProducts)-1; i>=0; i--)
		{
			AccountProductSub *psub = pAccount->ppProducts[i];
			if (stricmp(pAccount->ppProducts[i]->name, name) == 0)
			{
				accountClearPermissionsCache(pAccount);
				break;
			}
		}
		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
}

///////////////////////////////////////
// Subscriptions
///////////////////////////////////////

AUTO_TRANS_HELPER;
void trAccountMarkRefundedSubs(ATH_ARG NOCONST(AccountInfo) *pAccount)
{
	if (!verify(NONNULL(pAccount))) return;

	if (ISNULL(pAccount->pCachedSubscriptionList)) return;

	if (ISNULL(pAccount->pCachedSubscriptionList->ppList)) return;

	if (ISNULL(pAccount->ppRefundedSubscriptions)) return;

	EARRAY_CONST_FOREACH_BEGIN(pAccount->pCachedSubscriptionList->ppList, i, size);
	{
		NOCONST(CachedAccountSubscription) *pCachedSub = pAccount->pCachedSubscriptionList->ppList[i];
		int refundIndex;
		
		if (!devassert(pCachedSub)) continue;

		refundIndex = eaIndexedFindUsingString(&pAccount->ppRefundedSubscriptions, pCachedSub->vindiciaID);

		if (refundIndex < 0) continue;

		pCachedSub->vindiciaStatus = SUBSCRIPTIONSTATUS_REFUNDED;
	}
	EARRAY_FOREACH_END;
}

AUTO_TRANSACTION
ATR_LOCKS(account, ".Pcachedsubscriptionlist.Pplist, .Pprefundedsubscriptions");
enumTransactionOutcome trAccountMarkSubsRefunded(ATR_ARGS, NOCONST(AccountInfo) *account)
{
	trAccountMarkRefundedSubs(account);

	return TRANSACTION_OUTCOME_SUCCESS;
}

void accountMarkSubsRefunded(SA_PARAM_NN_VALID AccountInfo *pAccount)
{
	AutoTrans_trAccountMarkSubsRefunded(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID);
	accountClearPermissionsCache(pAccount);
}

// Determine if two cached subs are the same
static bool CachedSubsAreSame(SA_PARAM_NN_VALID const CachedAccountSubscription *pSubA,
							  SA_PARAM_NN_VALID const CachedAccountSubscription *pSubB)
{
	if (!verify(pSubA && pSubB)) return false;
	
	return !stricmp_safe(pSubA->vindiciaID, pSubB->vindiciaID);
}

static void accountNotifyLapse_CB(struct JSONRPCState * pState, void * pUserData)
{
	if (pState->error)
	{
		ErrorOrCriticalAlert("ACCOUNTSERVER_WEBSRV_SUB_LAPSE", "Could not send subscription lapse e-mail to WebSrv: %s", pState->error);
	}
}

static bool gbSendLapseEmails = true;
AUTO_CMD_INT(gbSendLapseEmails, SendLapseEmails) ACMD_CMDLINE;

static void accountHandleLapse(SA_PARAM_NN_VALID const AccountInfo * pAccount, SA_PARAM_NN_STR const char * pInternalName,
	SA_PARAM_NN_STR const char * pSubInternalName)
{
	WebSrvKeyValueList keyList = {0};
	CachedAccountSubscription * pCachedSub = NULL;

	// While it's possible (though extremely unlikely) for this function to be executed for interal subs,
	// it's unlikely that it will be timely because they are lazily expired.
	if (!gbSendLapseEmails) return;
	if (!verify(pAccount)) return;
	if (!verify(pInternalName)) return;

	PERFINFO_AUTO_START_FUNC();

	pCachedSub = findAccountSubscriptionByInternalName(pAccount, pSubInternalName);
	if (!pCachedSub)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	websrvKVList_Add(&keyList, "account", pAccount->displayName);
	websrvKVList_Add(&keyList, "product", pInternalName);
	websrvKVList_Add(&keyList, "sub_internal_name", pSubInternalName);
	websrvKVList_Addf(&keyList, "billed", "%i", pCachedSub->bBilled);
	websrvKVList_Add(&keyList, "sub_name", pCachedSub->name);
	websrvKVList_Addf(&keyList, "game_card", "%i", pCachedSub->gameCard);
	websrvKVList_Addf(&keyList, "created_time", "%u", pCachedSub->estimatedCreationTimeSS2000);
	websrvKVList_Addf(&keyList, "start_time", "%u", pCachedSub->startTimeSS2000);
	websrvKVList_Addf(&keyList, "end_time", "%u", pCachedSub->entitlementEndTimeSS2000);
	websrvKVList_Addf(&keyList, "next_billing_time", "%u", pCachedSub->nextBillingDateSS2000);
	websrvKVList_Add(&keyList, "status",
		StaticDefineIntRevLookupNonNull(SubscriptionStatusEnum, getCachedSubscriptionStatus(pCachedSub)));

	accountSendEventEmail(pAccount, NULL, "LapsedSubEmail", &keyList, accountNotifyLapse_CB, NULL);
	StructDeInit(parse_WebSrvKeyValueList, &keyList);

	PERFINFO_AUTO_STOP_FUNC();
}

static void accountHandleAllLapsedSubs(SA_PARAM_NN_VALID const AccountInfo * pAccount,
	SA_PRE_NN_NN_VALID const char * const * eaFormerProductList, SA_PRE_NN_NN_VALID const char * const * eaFormerSubInternalNameList)
{
	STRING_EARRAY eaCurrentProductList = NULL;

	if (!verify(pAccount)) return;
	
	PERFINFO_AUTO_START_FUNC();

	if (!devassert(eaSize(&eaFormerProductList) == eaSize(&eaFormerSubInternalNameList)))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	eaCurrentProductList = accountGetInternalProductList(pAccount, SUBSTATE_EXTERNALACTIVEONLY, NULL);
	EARRAY_CONST_FOREACH_BEGIN(eaFormerProductList, iCurProduct, iNumProducts);
	{
		const char * pInternalProductName = eaFormerProductList[iCurProduct];
		const char * pSubInternalName = eaFormerSubInternalNameList[iCurProduct];

		if (!devassert(pInternalProductName)) continue;
		if (!devassert(pSubInternalName)) continue;

		if (eaFindString(&eaCurrentProductList, pInternalProductName) == -1)
		{
			accountHandleLapse(pAccount, pInternalProductName, pSubInternalName);
		}
	}
	EARRAY_FOREACH_END;

	eaDestroyEx(&eaCurrentProductList, NULL);

	PERFINFO_AUTO_STOP_FUNC();
}

// Record any changed to a sub
static void RecordCachedSubChange(SA_PARAM_NN_VALID AccountInfo *pAccount,
								  SA_PARAM_NN_VALID const CachedAccountSubscription *pSubOld,
								  SA_PARAM_NN_VALID const CachedAccountSubscription *pSubNew)
{
	if (!verify(pAccount)) return;
	if (!verify(pSubOld && pSubNew)) return;
	if (!verify(CachedSubsAreSame(pSubOld, pSubNew))) return;

	PERFINFO_AUTO_START_FUNC();

	if (getCachedSubscriptionStatus(pSubOld) != getCachedSubscriptionStatus(pSubNew))
	{
		accountLog(pAccount, "Cached subscription [cachedsub:%s] changed from %s to %s.",
			pSubOld->vindiciaID,
			StaticDefineIntRevLookupNonNull(SubscriptionStatusEnum, getCachedSubscriptionStatus(pSubOld)),
			StaticDefineIntRevLookupNonNull(SubscriptionStatusEnum, getCachedSubscriptionStatus(pSubNew)));
	}

	// Compare the Vindicia status without using getCachedSubscriptionStatus so that it sees the old one as having been active,
	// even if it's past the entitlement end date.
	if (pSubOld->vindiciaStatus == SUBSCRIPTIONSTATUS_ACTIVE && pSubNew->vindiciaStatus != SUBSCRIPTIONSTATUS_ACTIVE)
	{
		// Active flipped off
		const char *pSubInternalName = pSubOld->internalName;
		SubscriptionHistoryEntryReason eReason = SHER_Invalid;
		ProductDefaultPermission * const *ppDefaults = NULL;
		STRING_EARRAY eaProductInternalNames = NULL;
		U32 uCreated = pSubOld->estimatedCreationTimeSS2000;
		U32 uEntitlementEnded = pSubOld->entitlementEndTimeSS2000;
		U32 uNextBilling = pSubOld->nextBillingDateSS2000;
		U32 uStart = uEntitlementEnded ? MIN(uCreated, uEntitlementEnded) : uCreated;
		U32 uEnd = uEntitlementEnded ? MAX(uCreated, uEntitlementEnded) : MAX(uCreated, uNextBilling);

		if (!devassert(pSubInternalName && *pSubInternalName))
		{
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}

		if (!(uStart && uEnd))
		{
			accountLog(pAccount, "Could not record sub history for the sub VID %s because the %s date is missing.",
				pSubOld->vindiciaID, uStart ? "end" : "start");
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}

		// Determine the reason for the subscription history entry
		if (getCachedSubscriptionStatus(pSubNew) == SUBSCRIPTIONSTATUS_SUSPENDED)
			eReason = SHER_Suspended;
		else if (getCachedSubscriptionStatus(pSubNew) == SUBSCRIPTIONSTATUS_CANCELLED)
			eReason = SHER_Cancelled;
		else if (getCachedSubscriptionStatus(pSubNew) == SUBSCRIPTIONSTATUS_REFUNDED)
			eReason = SHER_Refunded;
		if (!devassert(eReason != SHER_Invalid))
		{
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}

		// Get a list of internal product names that require this subscription
		EARRAY_CONST_FOREACH_BEGIN(pAccount->ppProducts, iCurProduct, iNumProducts);
		{
			const AccountProductSub *pProductStub = pAccount->ppProducts[iCurProduct];
			const ProductContainer *pProduct;

			if (!devassert(pProductStub)) continue;

			pProduct = findProductByName(pProductStub->name);

			if (!devassert(pProduct)) continue;

			if (productHasSubscriptionListed(pProduct, pSubInternalName))
			{
				eaPush(&eaProductInternalNames, estrDup(pProduct->pInternalName));
			}
		}
		EARRAY_FOREACH_END;

		ppDefaults = productGetDefaultPermission();
		EARRAY_CONST_FOREACH_BEGIN(ppDefaults, iCurDefault, iNumDefaults);
		{
			ProductDefaultPermission *pDefaultPermission = ppDefaults[iCurDefault];

			EARRAY_CONST_FOREACH_BEGIN(pDefaultPermission->eaRequiredSubscriptions, iCurSubscription, iNumSubscriptions);
			{
				const char *pRequiredSub = pDefaultPermission->eaRequiredSubscriptions[iCurSubscription];
				if (!stricmp(pRequiredSub, pSubInternalName))
					eaPush(&eaProductInternalNames, estrDup(pDefaultPermission->pProductName));
			}
			EARRAY_FOREACH_END;
		}
		EARRAY_FOREACH_END;

		// Remove duplicates because we only want to record one entry per internal product name
		eaRemoveDuplicateEStrings(&eaProductInternalNames);

		// Make sure there are some products that require the subscription
		if (!eaSize(&eaProductInternalNames))
		{
			eaDestroyEString(&eaProductInternalNames);
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}

		EARRAY_CONST_FOREACH_BEGIN(eaProductInternalNames, iCurProductInternalName, iNumProductInternalNames);
		{
			accountArchiveSubscription(pAccount->uID,
				eaProductInternalNames[iCurProductInternalName],
				pSubInternalName,
				pSubOld->vindiciaID,
				uStart,
				uEnd,
				STS_External,
				eReason,
				SHEP_NOT_EXACT);
		}
		EARRAY_FOREACH_END;

		eaDestroyEString(&eaProductInternalNames);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_TRANSACTION
ATR_LOCKS(account, ".Pcachedsubscriptionlist, .Pprefundedsubscriptions");
enumTransactionOutcome trAccountUpdateCachedSubscriptions(ATR_ARGS, NOCONST(AccountInfo) *account, NON_CONTAINER CachedAccountSubscriptionList *pNewList)
{	
	if (!verify(NONNULL(account))) return TRANSACTION_OUTCOME_FAILURE;

	StructDestroyNoConst(parse_CachedAccountSubscriptionList, account->pCachedSubscriptionList);
	account->pCachedSubscriptionList = StructCloneDeConst(parse_CachedAccountSubscriptionList, pNewList);

	trAccountMarkRefundedSubs(account);

	return TRANSACTION_OUTCOME_SUCCESS;
}

typedef struct UpdateCachedSubListUserData
{
	AccountInfo *pAccount;
	CachedAccountSubscriptionList *pList;
	STRING_EARRAY eaFormerProductList;
	STRING_EARRAY eaSubInternalNameList;
} UpdateCachedSubListUserData;

static void accountUpdateCachedSubscriptionsCB(SA_PARAM_NN_VALID TransactionReturnVal *returnVal,
											   SA_PARAM_NN_VALID UpdateCachedSubListUserData *pData)
{
	if (!verify(pData)) return;
	
	PERFINFO_AUTO_START_FUNC();

	if (!devassert(pData->pList)) goto cleanup;
	if (!devassert(pData->pAccount)) goto cleanup;
	if (!devassert(returnVal)) goto cleanup;

	EARRAY_CONST_FOREACH_BEGIN(pData->pList->ppList, iCurCachedSubNew, iNumCachedSubsNew);
	{
		const CachedAccountSubscription *pSubNew = pData->pList->ppList[iCurCachedSubNew];

		accountRemoveExpectedSub(pData->pAccount, pSubNew->internalName); // Silently fails if it wasn't expected

		if (pSubNew->gameCard || pSubNew->bBilled)
		{
			// If they have a game card, they are automatically marked as billed,
			// even if we didn't get the money directly
			accountSetBilled(pData->pAccount);
		}
	}
	EARRAY_FOREACH_END;

	accountHandleAllLapsedSubs(pData->pAccount, pData->eaFormerProductList, pData->eaSubInternalNameList);

cleanup:
	if (pData)
	{
		if (pData->pList)
		{
			StructDestroy(parse_CachedAccountSubscriptionList, pData->pList);
		}

		eaDestroyEx(&pData->eaFormerProductList, NULL);
		eaDestroyEx(&pData->eaSubInternalNameList, NULL);
		
		free(pData);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// This function expects to claim ownership over pNewList
void accountUpdateCachedSubscriptions(SA_PARAM_NN_VALID AccountInfo *pAccount,
									  SA_PARAM_NN_VALID CachedAccountSubscriptionList *pNewList)
{
	UpdateCachedSubListUserData *pData = NULL;
	STRING_EARRAY eaFormerProductList = NULL;
	STRING_EARRAY eaSubInternalNameList = NULL;

	if (!verify(pNewList)) return;

	if (!verify(pAccount))
	{
		StructDestroy(parse_CachedAccountSubscriptionList, pNewList);
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	eaCreate(&eaSubInternalNameList);
	eaFormerProductList = accountGetInternalProductList(pAccount, SUBSTATE_EXTERNALACTIVEONLY, &eaSubInternalNameList);

	// Just in case the subscription information changes
	accountClearPermissionsCache(pAccount);

	if (pAccount->pCachedSubscriptionList)
	{
		EARRAY_CONST_FOREACH_BEGIN(pAccount->pCachedSubscriptionList->ppList, iCurCachedSubOld, iNumCachedSubsOld);
		{
			const CachedAccountSubscription *pSubOld = pAccount->pCachedSubscriptionList->ppList[iCurCachedSubOld];
			bool bFound = false;

			EARRAY_CONST_FOREACH_BEGIN(pNewList->ppList, iCurCachedSubNew, iNumCachedSubsNew);
			{
				const CachedAccountSubscription *pSubNew = pNewList->ppList[iCurCachedSubNew];

				if (CachedSubsAreSame(pSubOld, pSubNew))
				{
					NOCONST(CachedAccountSubscription) *pSub = CONTAINER_NOCONST(CachedAccountSubscription, pSubNew);

					// Never turn off billed once it is on
					if (!devassert(pSubOld->bBilled ? pSub->bBilled : true))
						pSub->bBilled = pSub->bBilled || pSubOld->bBilled;

					RecordCachedSubChange(pAccount, pSubOld, pSubNew);
				}

				if (!stricmp_safe(pSubNew->internalName, pSubOld->internalName))
				{
					bFound = true;
				}
			}
			EARRAY_FOREACH_END;

			if (!bFound)
			{
				NOCONST(CachedAccountSubscriptionList) *pList = CONTAINER_NOCONST(CachedAccountSubscriptionList, pNewList);

				eaPush(&pList->ppList, StructCloneDeConst(parse_CachedAccountSubscription, pSubOld));

				log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Account %s forgot it had a sub with the internal name %s.",
							  pAccount->accountName, pSubOld->internalName);
			}
		}
		EARRAY_FOREACH_END;
	}

	pData = callocStruct(UpdateCachedSubListUserData);
	pData->pAccount = pAccount;
	pData->pList = pNewList;
	pData->eaFormerProductList = eaFormerProductList;
	pData->eaSubInternalNameList = eaSubInternalNameList;

	AutoTrans_trAccountUpdateCachedSubscriptions(
		objCreateManagedReturnVal(accountUpdateCachedSubscriptionsCB, pData), 
		objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID, pNewList);

	PERFINFO_AUTO_STOP_FUNC();
}

bool shouldReplaceCachedSubscription(SA_PARAM_NN_VALID const CachedAccountSubscription *pSubOld,
									 SA_PARAM_NN_VALID const CachedAccountSubscription *pSubNew)
{
	SubscriptionStatus eOldStatus, eNewStatus;

	if (stricmp_safe(pSubNew->internalName, pSubOld->internalName)) return false; // Different internal names

	eOldStatus = getCachedSubscriptionStatus(pSubOld);
	eNewStatus = getCachedSubscriptionStatus(pSubNew);

	if (eOldStatus == SUBSCRIPTIONSTATUS_ACTIVE &&
		eNewStatus != SUBSCRIPTIONSTATUS_ACTIVE)
	{
		return false; // Do not replace an existing active one with an inactive one
	}
	else if (eOldStatus != SUBSCRIPTIONSTATUS_ACTIVE &&
			 eNewStatus == SUBSCRIPTIONSTATUS_ACTIVE)
	{
		return true; // Always replace an existing inactive one with an active one
	}

	if(pSubOld->startTimeSS2000 == pSubNew->startTimeSS2000)
	{
		if (eOldStatus == SUBSCRIPTIONSTATUS_ACTIVE && eNewStatus == SUBSCRIPTIONSTATUS_ACTIVE)
		{
			// if they're both active, prefer the one with the greatest end date
			return pSubOld->nextBillingDateSS2000 < pSubNew->nextBillingDateSS2000;
		}
		else if(eOldStatus == SUBSCRIPTIONSTATUS_ACTIVE ||
				eOldStatus == SUBSCRIPTIONSTATUS_PENDINGCUSTOMER)
		{
			// if they're the same time, keep the active one
			return false; // Do not replace
		}
		else
		{
			return true; // Replace
		}
	}
	else if(pSubOld->startTimeSS2000 < pSubNew->startTimeSS2000)
	{
		return true; // Replace
	}

	return false; // Do not replace
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Pcachedsubscriptionlist.Pplist, .Pcachedsubscriptionlist.Lastupdatedss2000, .Pprefundedsubscriptions");
enumTransactionOutcome trAccountUpdateCachedSubscription(ATR_ARGS, NOCONST(AccountInfo) *pAccount, NON_CONTAINER CachedAccountSubscription *pSub)
{
	NOCONST(CachedAccountSubscription) *pSubscription = CONTAINER_NOCONST(CachedAccountSubscription, pSub);
	U32 estimatedCreationTimeSS2000 = pSubscription->estimatedCreationTimeSS2000;
	bool bKeepOld = false;
	bool bBilled = pSubscription->bBilled;

	if (ISNULL(pAccount->pCachedSubscriptionList)) return TRANSACTION_OUTCOME_SUCCESS;

	// Remove it if it already exists on the account
	EARRAY_FOREACH_REVERSE_BEGIN(pAccount->pCachedSubscriptionList->ppList, i);
		NOCONST(CachedAccountSubscription) *pCachedSub = pAccount->pCachedSubscriptionList->ppList[i];

		if (CachedSubsAreSame((const CachedAccountSubscription *)pCachedSub, (const CachedAccountSubscription *)pSubscription))
		{
			// It's the same subscription
			estimatedCreationTimeSS2000 = pCachedSub->estimatedCreationTimeSS2000;

			// Should never turn of billed once on
			if (!devassert(pCachedSub->bBilled ? bBilled : true))
				bBilled = bBilled || pCachedSub->bBilled;

			StructDestroyNoConst(parse_CachedAccountSubscription, eaRemoveFast(&pAccount->pCachedSubscriptionList->ppList, i));
		}
		else if (!stricmp_safe(pCachedSub->internalName, pSub->internalName))
		{
			// We found one with the same internal name; see if we should replace (delete) the old one or keep it
			if (shouldReplaceCachedSubscription((CachedAccountSubscription *)pCachedSub, pSub))
			{
				StructDestroyNoConst(parse_CachedAccountSubscription, eaRemoveFast(&pAccount->pCachedSubscriptionList->ppList, i));
			}
			else
			{
				bKeepOld = true;
			}
		}
	EARRAY_FOREACH_END;

	if (bKeepOld)
		return TRANSACTION_OUTCOME_SUCCESS;

	// Replace as appropriate
	pSubscription->estimatedCreationTimeSS2000 = estimatedCreationTimeSS2000 ? estimatedCreationTimeSS2000 : timeSecondsSince2000();
	pSubscription->bBilled = bBilled;

	// Update the timestamp
	pAccount->pCachedSubscriptionList->lastUpdatedSS2000 = timeSecondsSince2000();

	// Add it to the list
	eaPush(&pAccount->pCachedSubscriptionList->ppList, StructCloneNoConst(parse_CachedAccountSubscription, pSubscription));
	
	trAccountMarkRefundedSubs(pAccount);

	return TRANSACTION_OUTCOME_SUCCESS;
}

typedef struct UpdateCachedSubUserData
{
	STRING_EARRAY eaFormerProducts;
	STRING_EARRAY eaSubInternalNameList;
	AccountInfo * pAccount;
} UpdateCachedSubUserData;

static void accountUpdateCachedSubscriptionCB(SA_PARAM_NN_VALID TransactionReturnVal * pReturnVal, void * pUserData)
{
	UpdateCachedSubUserData * pData = pUserData;

	if (!verify(pData)) return;

	accountHandleAllLapsedSubs(pData->pAccount, pData->eaFormerProducts, pData->eaSubInternalNameList);
	eaDestroyEx(&pData->eaFormerProducts, NULL);
	eaDestroyEx(&pData->eaSubInternalNameList, NULL);
	free(pData);
}

void accountUpdateCachedSubscription(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_VALID NOCONST(CachedAccountSubscription) *pSubscription)
{
	const CachedAccountSubscription *pCachedSub = findAccountSubscriptionByVID(pAccount, pSubscription->vindiciaID);
	STRING_EARRAY eaSubInternalNameList = NULL;
	STRING_EARRAY eaFormerProducts = NULL;
	UpdateCachedSubUserData * pData = callocStruct(UpdateCachedSubUserData);

	eaCreate(&eaSubInternalNameList);
	eaFormerProducts = accountGetInternalProductList(pAccount, SUBSTATE_EXTERNALACTIVEONLY, &eaSubInternalNameList);

	if (pCachedSub && CachedSubsAreSame(pCachedSub, (const CachedAccountSubscription *)pSubscription))
		RecordCachedSubChange(pAccount, pCachedSub, (const CachedAccountSubscription *)pSubscription);

	pData->pAccount = pAccount;
	pData->eaFormerProducts = eaFormerProducts;
	pData->eaSubInternalNameList = eaSubInternalNameList;
	AutoTrans_trAccountUpdateCachedSubscription(objCreateManagedReturnVal(accountUpdateCachedSubscriptionCB, pData),
		objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID, (CachedAccountSubscription*)pSubscription);
	accountClearPermissionsCache(pAccount);

	accountRemoveExpectedSub(pAccount, pSubscription->internalName); // Silently fails if it wasn't expected
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Pcachedsubscriptionlist.Pplist, .Pcachedsubscriptionlist.Lastupdatedss2000");
enumTransactionOutcome trAccountUpdateCachedCreatedTime(ATR_ARGS, NOCONST(AccountInfo) *pAccount, SA_PARAM_NN_STR const char *pSubVID, U32 uNewCreatedTimeSS2000)
{
	if (!verify(NONNULL(pAccount))) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(NONNULL(pAccount->pCachedSubscriptionList))) return TRANSACTION_OUTCOME_FAILURE;

	EARRAY_CONST_FOREACH_BEGIN(pAccount->pCachedSubscriptionList->ppList, iCurCachedSub, iNumCachedSubs);
	{
		NOCONST(CachedAccountSubscription) *pCachedSub = pAccount->pCachedSubscriptionList->ppList[iCurCachedSub];

		if (!verify(pCachedSub)) continue;

		if (!stricmp_safe(pCachedSub->vindiciaID, pSubVID))
		{
			pCachedSub->estimatedCreationTimeSS2000 = uNewCreatedTimeSS2000;
			break;
		}
	}
	EARRAY_FOREACH_END;

	// Update the timestamp
	pAccount->pCachedSubscriptionList->lastUpdatedSS2000 = timeSecondsSince2000();

	return TRANSACTION_OUTCOME_SUCCESS;
}


void accountUpdateCachedCreatedTime(U32 uAccountID, SA_PARAM_NN_STR const char *pSubVID, U32 uNewCreatedTimeSS2000)
{
	AutoTrans_trAccountUpdateCachedCreatedTime(NULL, objServerType(), GLOBALTYPE_ACCOUNT, uAccountID, pSubVID, uNewCreatedTimeSS2000);
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Pcachedsubscriptionlist.Pplist, .Pcachedsubscriptionlist.Lastupdatedss2000");
enumTransactionOutcome trAccountStopCachedSub(ATR_ARGS, NOCONST(AccountInfo) *pAccount, SA_PARAM_NN_STR const char *pSubVID)
{
	if (!verify(NONNULL(pAccount))) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(NONNULL(pAccount->pCachedSubscriptionList))) return TRANSACTION_OUTCOME_FAILURE;

	EARRAY_CONST_FOREACH_BEGIN(pAccount->pCachedSubscriptionList->ppList, iCurCachedSub, iNumCachedSubs);
	{
		NOCONST(CachedAccountSubscription) *pCachedSub = pAccount->pCachedSubscriptionList->ppList[iCurCachedSub];

		if (!verify(pCachedSub)) continue;

		if (!stricmp_safe(pCachedSub->vindiciaID, pSubVID))
		{
			// Add one to make it so that this sub is still active as of the last updated time (below),
			// which is needed for the accountHandleLapsed function to be executed properly (depending on
			// whether or not Vindicia's clocks are before or after the local  one)
			// These will soon be overwritted by Vindicia
			pCachedSub->entitlementEndTimeSS2000 = timeSecondsSince2000() + 1;
			pCachedSub->nextBillingDateSS2000 = timeSecondsSince2000() + 1;
			break;
		}
	}
	EARRAY_FOREACH_END;

	// Update the timestamp
	pAccount->pCachedSubscriptionList->lastUpdatedSS2000 = timeSecondsSince2000();

	return TRANSACTION_OUTCOME_SUCCESS;
}

void accountStopCachedSub(U32 uAccountID, SA_PARAM_NN_STR const char *pSubVID)
{
	AutoTrans_trAccountStopCachedSub(NULL, objServerType(), GLOBALTYPE_ACCOUNT, uAccountID, pSubVID);
}

void accountsClearPermissionsCacheIfSubOwned(SA_PARAM_NN_STR const char *name)
{
	ContainerIterator iter = {0};
	Container *currCon = NULL;

	objInitContainerIteratorFromType(GLOBALTYPE_ACCOUNT, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		AccountInfo *pAccount = currCon->containerData;
		int i;

		if(pAccount->pCachedSubscriptionList)
		{
			for (i=eaSize(&pAccount->pCachedSubscriptionList->ppList)-1; i>=0; i--)
			{
				CachedAccountSubscription *psub = pAccount->pCachedSubscriptionList->ppList[i];
				if (stricmp(psub->name, name) == 0)
				{
					accountClearPermissionsCache(pAccount);
					break;
				}
			}
		}
		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
}

void accountFlagInvalidDisplayName(AccountInfo *account, bool bFlag)
{
	if (bFlag)
		AutoTrans_trAccountSetFlags(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, account->uID, ACCOUNT_FLAG_INVALID_DISPLAYNAME);
	else
		AutoTrans_trAccountClearFlags(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, account->uID, ACCOUNT_FLAG_INVALID_DISPLAYNAME);
}

////////////////////////////////////////////////
// Transactions
////////////////////////////////////////////////
// TODO set error / sucess messages

AUTO_TRANS_HELPER
ATR_LOCKS(pAccount, ".flags");
void trhAccountSetFlags(ATH_ARG NOCONST(AccountInfo) *pAccount, U32 uFlags)
{
	pAccount->flags |= uFlags;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pAccount, ".flags");
void trhAccountClearFlags(ATH_ARG NOCONST(AccountInfo) *pAccount, U32 uFlags)
{
	pAccount->flags &= ~uFlags;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".flags");
enumTransactionOutcome trAccountSetFlags(ATR_ARGS, NOCONST(AccountInfo) *pAccount, U32 uFlags)
{
	trhAccountSetFlags(pAccount, uFlags);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".flags");
enumTransactionOutcome trAccountClearFlags(ATR_ARGS, NOCONST(AccountInfo) *pAccount, U32 uFlags)
{
	trhAccountClearFlags(pAccount, uFlags);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Personalinfo.Pppaymentmethods");
enumTransactionOutcome trAccountUpdatePaymentMethodCache(ATR_ARGS, NOCONST(AccountInfo) *pAccount,
														 PaymentMethodCacheUpdate *pUpdate)
{
	eaClearStructNoConst(&pAccount->personalInfo.ppPaymentMethods, parse_CachedPaymentMethod);
	pAccount->personalInfo.ppPaymentMethods = (EARRAY_OF(NOCONST(CachedPaymentMethod))) pUpdate->eaPaymentMethods;
	pUpdate->eaPaymentMethods = NULL;

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(account, ".Binternaluselogin, .Displayname, .Flags, .Accountname, .Uid");
enumTransactionOutcome trAccountChangeDisplayName (ATR_ARGS, NOCONST(AccountInfo) *account, const char *displayName)
{
	// This is not transactionally safe and only works because Account Server is fully local
	int error;
	if (!displayName || !displayName[0])
		return TRANSACTION_OUTCOME_FAILURE;
	
	if (error = StringIsInvalidDisplayName(displayName, 0))
	{
		if (error != STRINGERR_RESTRICTED || !account->bInternalUseLogin)
		{
			estrPrintf(ATR_RESULT_FAIL, "%d", error);
			return TRANSACTION_OUTCOME_FAILURE;
		}
	}

	if (stricmp(displayName, account->accountName) != 0 && stashFindInt(sAccountNameStash, displayName, NULL))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (account->displayName && stricmp(account->displayName, account->accountName) != 0)
	{
		// Only remove it if the old display name wasn't the account name
		stashRemoveInt(sAccountNameStash, account->displayName, NULL);
	}
	estrCopy2(&account->displayName, displayName);
	if (stricmp(account->displayName, account->accountName) != 0)
	{
		stashAddInt(sAccountNameStash, account->displayName, account->uID, false);
	}
	trhAccountClearFlags(account, ACCOUNT_FLAG_INVALID_DISPLAYNAME);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(account, ".Displayname, .Accountname, .Uid");
enumTransactionOutcome trAccountChangeAccountName (ATR_ARGS, NOCONST(AccountInfo) *account, const char *accountName)
{
	// This is not transactionally safe and only works because Account Server is fully local
	if (!accountName || !accountName[0] || StringIsInvalidAccountName(accountName, 9))
		return TRANSACTION_OUTCOME_FAILURE;
	if (stricmp(accountName, account->accountName) == 0)
		return TRANSACTION_OUTCOME_SUCCESS;

	if (stricmp(accountName, account->displayName) != 0 && stashFindInt(sAccountNameStash, accountName, NULL))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (!account->displayName || stricmp(account->displayName, account->accountName) != 0)
	{
		// Only remove it if the old account name wasn't the display name as well
		stashRemoveInt(sAccountNameStash, account->accountName, NULL);
	}
	strcpy(account->accountName, accountName);
	if (stricmp(account->displayName, account->accountName) != 0)
	{
		stashAddInt(sAccountNameStash, account->accountName, account->uID, false);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(account, ".Personalinfo.Email, .Flags, .Uid, .Validateemailkey");
enumTransactionOutcome trAccountChangeEmail(ATR_ARGS, NOCONST(AccountInfo) *account,  const char *email, int iGenerateActivator)
{
	// This is not transactionally safe and only works because Account Server is fully local
	if (!email)
		return TRANSACTION_OUTCOME_FAILURE;
	if (account->personalInfo.email && stricmp(email, account->personalInfo.email) == 0)
		return TRANSACTION_OUTCOME_SUCCESS;

	// See if new email key is already in use
	if (stashFindInt(sAccountEmailStash, email, NULL))
		return TRANSACTION_OUTCOME_FAILURE;

	// Remove old email key
	if (account->personalInfo.email)
		stashRemoveInt(sAccountEmailStash, account->personalInfo.email, NULL);

	estrCopy2(&account->personalInfo.email, email);
	stashAddInt(sAccountEmailStash, account->personalInfo.email, account->uID, true);
	if (iGenerateActivator)
	{
		trhAccountSetFlags(account, ACCOUNT_FLAG_NOT_ACTIVATED);
		generateActivationKey(account->validateEmailKey);
	}
	else
		trhAccountClearFlags(account, ACCOUNT_FLAG_NOT_ACTIVATED);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(account, ".Personalinfo.Dob");
enumTransactionOutcome trAccountChangeDOB(ATR_ARGS, NOCONST(AccountInfo) *account,  int dob_day, int dob_month, int dob_year)
{
	account->personalInfo.dob[0] = dob_day;
	account->personalInfo.dob[1] = dob_month;
	account->personalInfo.dob[2] = dob_year;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(account, ".Validateemailkey, .Flags");
enumTransactionOutcome trAccountValidateEmail(ATR_ARGS, NOCONST(AccountInfo) *account)
{
	account->validateEmailKey[0] = 0;
	trhAccountClearFlags(account, ACCOUNT_FLAG_NOT_ACTIVATED);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Unhashed Password
AUTO_TRANSACTION
ATR_LOCKS(account, ".Password_obsolete, .passwordInfo, .uPasswordChangeTime, .accountName, .pBackupPasswordInfo");
enumTransactionOutcome trAccountSetPassword(ATR_ARGS, NOCONST(AccountInfo) *account, const char *plaintext)
{	
	char temp[MAX_PASSWORD];
	accountHashPassword(plaintext, temp);
	accountSetAndEncryptPassword(account, temp);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Hashed Password (and encoded in base64)
AUTO_TRANSACTION
ATR_LOCKS(account, ".Password_obsolete, .passwordInfo, .uPasswordChangeTime, .accountName, .pBackupPasswordInfo");
enumTransactionOutcome trAccountSetPasswordHashed(ATR_ARGS, NOCONST(AccountInfo) *account, const char *hashstring)
{	
	accountSetAndEncryptPassword(account, hashstring);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(account, ".Personalinfo.Shippingaddress");
enumTransactionOutcome trAccountSetShippingAddress(ATR_ARGS, NOCONST(AccountInfo) *account, 
												   const char * address1, const char * address2,
												   const char * city, const char * district,
												   const char * postalCode, const char * country,
												   const char * phone)
{
	NOCONST(AccountAddress) *pAddress = &account->personalInfo.shippingAddress;
	estrCopy2(&pAddress->address1, address1);
	estrCopy2(&pAddress->address2, address2);
	estrCopy2(&pAddress->city, city);
	estrCopy2(&pAddress->district, district);
	estrCopy2(&pAddress->postalCode, postalCode);
	estrCopy2(&pAddress->country, country);
	estrCopy2(&pAddress->phone, phone);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Defaultlocale");
enumTransactionOutcome trAccountSetLocale(ATR_ARGS, NOCONST(AccountInfo) *pAccount, SA_PARAM_NN_STR const char *pLocale)
{
	estrCopy2(&pAccount->defaultLocale, pLocale);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Forbiddenpaymentmethodtypes");
enumTransactionOutcome trAccountSetForbiddenPaymentMethods(ATR_ARGS, NOCONST(AccountInfo) *pAccount, int eForbiddenTypes)
{
	pAccount->forbiddenPaymentMethodTypes = eForbiddenTypes;
	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Defaultcurrency");
enumTransactionOutcome trAccountSetCurrency(ATR_ARGS, NOCONST(AccountInfo) *pAccount, SA_PARAM_NN_STR const char *pCurrency)
{
	estrCopy2(&pAccount->defaultCurrency, pCurrency);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Defaultcountry");
enumTransactionOutcome trAccountSetCountry(ATR_ARGS, NOCONST(AccountInfo) *pAccount, SA_PARAM_NN_STR const char *pCountry)
{
	estrCopy2(&pAccount->defaultCountry, pCountry);
	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_TRANSACTION
ATR_LOCKS(account, ".Ppproducts");
enumTransactionOutcome trAccountAddProduct(ATR_ARGS, NOCONST(AccountInfo) *account, const char *name, const char *key, const char *referrer)
{	
	NOCONST(AccountProductSub) *product = StructCreateNoConst(parse_AccountProductSub);
	estrCopy2(&product->name, name);
	estrCopy2(&product->key, key);
	product->uAssociatedTimeSS2000 = timeSecondsSince2000();
	if (referrer && *referrer)
		product->referrer = strdup(referrer);
	eaPush(&account->ppProducts, product);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(account, ".Bbillingenabled");
enumTransactionOutcome trAccountSetBillingEnabled(ATR_ARGS, NOCONST(AccountInfo) *account, int enabled)
{
	account->bBillingEnabled = enabled;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(account, ".Bbilled");
enumTransactionOutcome trAccountSetBilled(ATR_ARGS, NOCONST(AccountInfo) *account, int enabled)
{
	account->bBilled = true;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(account, ".Ppproducts");
enumTransactionOutcome trAccountModifyProduct(ATR_ARGS, NOCONST(AccountInfo) *account, SA_PARAM_NN_STR const char *oldProductName, 
											  const char *newProductName, const char *key, int iToDelete)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	int i;
	for (i=eaSize(&account->ppProducts)-1; i>=0; i--)
	{
		NOCONST(AccountProductSub) *product = account->ppProducts[i];
		if (stricmp(product->name, oldProductName) == 0)
		{
			if (iToDelete)
			{
				eaRemove(&account->ppProducts, i);
			}
			else
			{
				if (newProductName)
					estrCopy2(&product->name, newProductName);
				if (key)
					estrCopy2(&product->key, key);
			}
			// Cache should be cleared earlier
			return TRANSACTION_OUTCOME_SUCCESS;
		}
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

void accountAddPlayTime(AccountInfo *account, const char *productName, const char *shardCategory, U32 uPlayTime)
{
	OBJLOG_PAIRS(LOG_LOGIN, GLOBALTYPE_ACCOUNT, account->uID, 0, account->displayName, NULL, NULL, "ProxyLogout", NULL,
		("product", "%s", NULL_TO_EMPTY(productName))
		("shardCategory", "%s", NULL_TO_EMPTY(shardCategory))
		("playtime", "%u", uPlayTime)
		);
	if (account && productName && shardCategory && *productName && *shardCategory)
	{
		AutoTrans_trAccountAddPlayTime(NULL, objServerType(), GLOBALTYPE_ACCOUNT, account->uID, 
			productName, shardCategory, uPlayTime);
		accountReportLogoutPlayTime(account, productName, shardCategory, uPlayTime);
	}
}

const AccountGameMetadata *accountGetGameMetadata(AccountInfo *account, const char *productName, const char *shardCategory)
{
	EARRAY_CONST_FOREACH_BEGIN(account->ppGameMetadata, iMetadata, iNumMetadata);
	{
		const AccountGameMetadata *pGameMetadata = account->ppGameMetadata[iMetadata];

		if (!stricmp(productName, pGameMetadata->product) && !stricmp(shardCategory, pGameMetadata->shard))
			return pGameMetadata;
	}
	EARRAY_FOREACH_END;

	return NULL;
}

AUTO_TRANS_HELPER
ATR_LOCKS(account, ".Ppgamemetadata");
NOCONST(AccountGameMetadata) *trhAccountGetGameMetadata(ATH_ARG NOCONST(AccountInfo) *account, const char *productName, const char *shardCategory, bool create)
{
	EARRAY_CONST_FOREACH_BEGIN(account->ppGameMetadata, iMetadata, iNumMetadata);
	{
		NOCONST(AccountGameMetadata) *gameMetadata = account->ppGameMetadata[iMetadata];

		if (!stricmp(gameMetadata->product, productName) && !stricmp(gameMetadata->shard, shardCategory))
			return gameMetadata;
	}
	EARRAY_FOREACH_END;

	if (create)
	{
		NOCONST(AccountGameMetadata) *gameMetadata = StructCreateNoConst(parse_AccountGameMetadata);
		estrCopy2(&gameMetadata->product, productName);
		estrCopy2(&gameMetadata->shard, shardCategory);
		eaPush(&account->ppGameMetadata, gameMetadata);
		return gameMetadata;
	}

	return NULL;
}

AUTO_TRANSACTION
ATR_LOCKS(account, ".Ppgamemetadata");
enumTransactionOutcome trAccountSetHighestLevel(ATR_ARGS, NOCONST(AccountInfo) *account, const char *productName, const char *shardCategory, U32 uLevel)
{
	NOCONST(AccountGameMetadata) *gameMetadata = trhAccountGetGameMetadata(account, productName, shardCategory, true);
	if (!gameMetadata) return TRANSACTION_OUTCOME_FAILURE;
	gameMetadata->uHighestLevel = uLevel;
	return TRANSACTION_OUTCOME_SUCCESS;
}

void accountSetHighestLevel(AccountInfo *account, const char *productName, const char *shardCategory, U32 uLevel)
{
	const AccountGameMetadata *pGameMetadata = accountGetGameMetadata(account, productName, shardCategory);

	if (pGameMetadata && uLevel > pGameMetadata->uHighestLevel)
	{
		AutoTrans_trAccountSetHighestLevel(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, account->uID, productName, shardCategory, uLevel);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(account, ".Ppgamemetadata");
enumTransactionOutcome trAccountSetLastLogin(ATR_ARGS, NOCONST(AccountInfo) *account, const char *productName, const char *shardCategory)
{
	NOCONST(AccountGameMetadata) *gameMetadata = trhAccountGetGameMetadata(account, productName, shardCategory, true);
	if (!gameMetadata) return TRANSACTION_OUTCOME_FAILURE;
	gameMetadata->uLastLogin = timeSecondsSince2000();
	return TRANSACTION_OUTCOME_SUCCESS;
}

void accountSetLastLogin(AccountInfo *account, const char *productName, const char *shardCategory)
{
	AutoTrans_trAccountSetLastLogin(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, account->uID, productName, shardCategory);
}

AUTO_TRANSACTION
ATR_LOCKS(account, ".Ppgamemetadata");
enumTransactionOutcome trAccountSetNumCharacters(ATR_ARGS, NOCONST(AccountInfo) *account, const char *productName, const char *shardCategory, U32 uNumCharacters, U32 change)
{
	NOCONST(AccountGameMetadata) *gameMetadata = trhAccountGetGameMetadata(account, productName, shardCategory, true);
	if (!gameMetadata) return TRANSACTION_OUTCOME_FAILURE;
	if (change)
		gameMetadata->uNumCharacters += uNumCharacters;
	else
		gameMetadata->uNumCharacters = uNumCharacters;
	return TRANSACTION_OUTCOME_SUCCESS;
}

void accountSetNumCharacters(AccountInfo *account, const char *productName, const char *shardCategory, U32 uNumCharacters, bool change)
{
	AutoTrans_trAccountSetNumCharacters(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, account->uID, productName, shardCategory, uNumCharacters, change);
}

// Number of days to keep track of for play time on the granularity of days
static int giNumPlayTimeDays = 0; // Default to "keep all days"
AUTO_CMD_INT(giNumPlayTimeDays, NumPlayTimeDays) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

AUTO_TRANSACTION
ATR_LOCKS(account, ".Ppgamemetadata");
enumTransactionOutcome trAccountAddPlayTime(ATR_ARGS, NOCONST(AccountInfo) *account, SA_PARAM_NN_STR const char *productName, 
											  const char *shardCategory, U32 uPlayTime)
{
	NOCONST(AccountGameMetadata) *pGameMetadata = NULL;
	__time32_t now = _time32(NULL);
	struct tm today;

	if (!devassert(!_localtime32_s(&today, &now))) return TRANSACTION_OUTCOME_FAILURE;

	pGameMetadata = trhAccountGetGameMetadata(account, productName, shardCategory, true);

	if (!devassert(pGameMetadata)) return TRANSACTION_OUTCOME_FAILURE;

	pGameMetadata->playtime.uPlayTime += uPlayTime;
	pGameMetadata->uLastLogout = timeSecondsSince2000();

	if (pGameMetadata->playtime.pCurrentDay &&
		(pGameMetadata->playtime.pCurrentDay->uDay != today.tm_mday || 
		 pGameMetadata->playtime.pCurrentDay->uMonth != today.tm_mon + 1 ||
		 pGameMetadata->playtime.pCurrentDay->uYear != (U32)today.tm_year + 1900))
	{
		int iCurDay;

		eaPush(&pGameMetadata->playtime.eaDays, pGameMetadata->playtime.pCurrentDay);
		pGameMetadata->playtime.pCurrentDay = NULL;

		if (giNumPlayTimeDays > 0)
		{
			int iNumToPrune = eaSize(&pGameMetadata->playtime.eaDays) - giNumPlayTimeDays;

			for (iCurDay = 0; iCurDay < iNumToPrune; iCurDay++)
			{
				NOCONST(PlayTimeEntry) *pRemovedPlayTimeDay = pGameMetadata->playtime.eaDays[iCurDay];

				if (devassert(pRemovedPlayTimeDay))
				{
					StructDestroyNoConst(parse_PlayTimeEntry, pRemovedPlayTimeDay);
				}
			}

			if (iNumToPrune > 0)
			{
				eaRemoveRange(&pGameMetadata->playtime.eaDays, 0, iNumToPrune);
			}
		}
	}

	if (!pGameMetadata->playtime.pCurrentDay)
	{
		pGameMetadata->playtime.pCurrentDay = StructCreateNoConst(parse_PlayTimeEntry);
		pGameMetadata->playtime.pCurrentDay->uDay = today.tm_mday;
		pGameMetadata->playtime.pCurrentDay->uMonth = today.tm_mon + 1; // add one because it is difference from January
		pGameMetadata->playtime.pCurrentDay->uYear = today.tm_year + 1900;
	}

	if (!devassert(pGameMetadata->playtime.pCurrentDay)) return TRANSACTION_OUTCOME_FAILURE;

	pGameMetadata->playtime.pCurrentDay->uPlayTime += uPlayTime;

	if (pGameMetadata->playtime.pCurrentMonth &&
		(pGameMetadata->playtime.pCurrentMonth->uMonth != today.tm_mon + 1 ||
		 pGameMetadata->playtime.pCurrentMonth->uYear != (U32)today.tm_year + 1900))
	{
		eaPush(&pGameMetadata->playtime.eaMonths, pGameMetadata->playtime.pCurrentMonth);
		pGameMetadata->playtime.pCurrentMonth = NULL;
	}

	if (!pGameMetadata->playtime.pCurrentMonth)
	{
		pGameMetadata->playtime.pCurrentMonth = StructCreateNoConst(parse_PlayTimeEntry);
		pGameMetadata->playtime.pCurrentMonth->uMonth = today.tm_mon + 1; // add one because it is difference from January
		pGameMetadata->playtime.pCurrentMonth->uYear = today.tm_year + 1900;
	}

	if (!devassert(pGameMetadata->playtime.pCurrentMonth)) return TRANSACTION_OUTCOME_FAILURE;

	pGameMetadata->playtime.pCurrentMonth->uPlayTime += uPlayTime;
	
	return TRANSACTION_OUTCOME_SUCCESS;
}

void accountResetPlayTime(U32 uAccountID)
{
	AutoTrans_trAccountResetPlayTime(NULL, objServerType(), GLOBALTYPE_ACCOUNT, uAccountID);
}

AUTO_TRANSACTION
ATR_LOCKS(account, ".Ppgamemetadata");
enumTransactionOutcome trAccountResetPlayTime(ATR_ARGS, NOCONST(AccountInfo) *account)
{
	eaDestroyStructNoConst(&account->ppGameMetadata, parse_AccountGameMetadata);
	return TRANSACTION_OUTCOME_SUCCESS;
}

bool isValidPaymentMethodVID(const AccountInfo *account, const char *pmVID)
{
	if (!account || !pmVID) return false;

	EARRAY_CONST_FOREACH_BEGIN(account->personalInfo.ppPaymentMethods, i, s);
		if (!strcmp(account->personalInfo.ppPaymentMethods[i]->VID, pmVID)) return true;
	EARRAY_FOREACH_END;

	return false;
}

SA_RET_OP_VALID const CachedPaymentMethod *getCachedPaymentMethod(SA_PARAM_NN_VALID const AccountInfo *account, SA_PARAM_NN_VALID const char *pmVID)
{
	if (!devassertmsg(account && pmVID, "Must provide account.")) return NULL;

	EARRAY_CONST_FOREACH_BEGIN(account->personalInfo.ppPaymentMethods, i, s);
		if (!strcmp(account->personalInfo.ppPaymentMethods[i]->VID, pmVID))
			return account->personalInfo.ppPaymentMethods[i];
	EARRAY_FOREACH_END;

	return NULL;
}

// Clear the permission string cache
void accountClearPermissionsCache (AccountInfo *account)
{
	PERFINFO_AUTO_START_FUNC();
	eaDestroyStruct(&account->ppPermissionCache, parse_AccountPermission);
	PERFINFO_AUTO_STOP();
}

static void writePermissionString(AccountPermission *permission)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	if (!permission)
		return;
	estrClear(&permission->pPermissionString);

	estrConcatf(&permission->pPermissionString, "shard: %s", permission->pShardCategory);

	concatPermissionString(permission->ppPermissions, &permission->pPermissionString);
}

bool cachedSubEntitled(SA_PARAM_NN_VALID const CachedAccountSubscription *pCachedSub, U32 uAsOfSS2000)
{
	if (!verify(pCachedSub)) return false;

	return uAsOfSS2000 < MAX(pCachedSub->entitlementEndTimeSS2000, pCachedSub->startTimeSS2000);
}

typedef enum RequiredSubState { REQSUBSTATE_NONE = 0, REQSUBSTATE_EXTERNAL, REQSUBSTATE_INTERNAL, REQSUBSTATE_NOTNEEDED } RequiredSubState;
typedef enum RequiredSubExternalTimeCompare { REQSUBETC_NOW = 0, REQSUBETC_LAST_UPDATED } RequiredSubExternalTimeCompare;
static RequiredSubState hasRequiredSubscription(SA_PARAM_NN_VALID const AccountInfo *pAccount,
									SA_PARAM_NN_VALID const char * const * const * eaRequiredSubscriptions,
									RequiredSubExternalTimeCompare eTimeCompare,
									SA_PARAM_OP_OP_VALID const char * * ppOutSubInternalName)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	int iNumRequired = eaSize(eaRequiredSubscriptions);
	int iCurRequiredSub = 0;

	if (!iNumRequired) return REQSUBSTATE_NOTNEEDED;

	for (iCurRequiredSub = 0; iCurRequiredSub < iNumRequired; iCurRequiredSub++)
	{
		const char * pCachedSubName = (*eaRequiredSubscriptions)[iCurRequiredSub];
		const CachedAccountSubscription * pCachedSub = NULL;
		const InternalSubscription * pInternalSub = NULL;

		if (!pCachedSubName || !*pCachedSubName) continue;

		// Check internal subs
		if ((pInternalSub = findInternalSub(pAccount->uID, pCachedSubName)))
		{
			if (ppOutSubInternalName) *ppOutSubInternalName = pInternalSub->pSubInternalName;

			return REQSUBSTATE_INTERNAL;
		}

		// Check external subs
		pCachedSub = findAccountSubscriptionByInternalName(pAccount, pCachedSubName);
		if (pCachedSub)
		{
			U32 uTime = 0;

			if (eTimeCompare == REQSUBETC_NOW) uTime = timeSecondsSince2000();
			else if (eTimeCompare == REQSUBETC_LAST_UPDATED) uTime = pAccount->pCachedSubscriptionList->lastUpdatedSS2000;

			if (ppOutSubInternalName) *ppOutSubInternalName = pCachedSub->internalName;

			return cachedSubEntitled(pCachedSub, uTime) ? REQSUBSTATE_EXTERNAL : REQSUBSTATE_NONE;
		}
	}

	return REQSUBSTATE_NONE;
}

static RequiredSubState hasOneRequiredSubscription(SA_PARAM_NN_VALID const AccountInfo *account,
									   SA_PARAM_NN_VALID const ProductContainer *pProduct,
									   RequiredSubExternalTimeCompare eTimeCompare,
									   SA_PARAM_OP_OP_VALID const char * * ppOutSubInternalName)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	if (!verify(account && pProduct)) return REQSUBSTATE_NONE;

	if (pProduct->bRequiresNoSubs) return REQSUBSTATE_NOTNEEDED;

	return hasRequiredSubscription(account, &pProduct->ppRequiredSubscriptions, eTimeCompare, ppOutSubInternalName);
}

static void populateAccountPermissions_Helper(SA_PARAM_NN_VALID AccountInfo *account,
											 CONST_STRING_MODIFIABLE pInternalName,
											 CONST_EARRAY_OF(AccountPermissionValue) ppPermissions,
											 CONST_STRING_EARRAY ppShards,
											 const int iAccessLevel)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	bool bHasShardData = false;
	int j, k;
	for (k=eaSize(&ppShards)-1; k>=0; k--)
	{
		bool bFound = false;
		bHasShardData = true;
		for (j=eaSize(&account->ppPermissionCache)-1; j>=0; j--)
		{
			if (stricmp(pInternalName, account->ppPermissionCache[j]->pProductName) == 0)
			{
				if (stricmp(ppShards[k], account->ppPermissionCache[j]->pShardCategory) == 0)
				{ 
					eaPushEArray((NOCONST(AccountPermissionValue) ***) &account->ppPermissionCache[j]->ppPermissions, 
						(NOCONST(AccountPermissionValue) ***) &ppPermissions);
					if (iAccessLevel > account->ppPermissionCache[j]->iAccessLevel)
						CONTAINER_NOCONST(AccountInfo, account)->ppPermissionCache[j]->iAccessLevel = iAccessLevel;
					bFound = true;
				}
				else if (stricmp(ppShards[k], "all") == 0)
				{
					eaPushEArray((NOCONST(AccountPermissionValue) ***) &account->ppPermissionCache[j]->ppPermissions, 
						(NOCONST(AccountPermissionValue) ***) &ppPermissions);
					if (iAccessLevel > account->ppPermissionCache[j]->iAccessLevel)
						CONTAINER_NOCONST(AccountInfo, account)->ppPermissionCache[j]->iAccessLevel = iAccessLevel;
				}
			}
		}
		if (!bFound)
		{
			AccountPermission *permission = StructCreate(parse_AccountPermission);
			estrCopy2(&permission->pProductName, pInternalName);
			estrCopy2(&permission->pShardCategory, ppShards[k]);
			eaPushEArray((NOCONST(AccountPermissionValue) ***) &permission->ppPermissions, 
				(NOCONST(AccountPermissionValue) ***) &ppPermissions);
			permission->iAccessLevel = iAccessLevel;
			eaPush(&account->ppPermissionCache, permission);
		}
	}

	// Now add in any products that don't have shard data associated with them
	if(!bHasShardData)
	{
		AccountPermission *permission = StructCreate(parse_AccountPermission);
		estrCopy2(&permission->pProductName, pInternalName);
		estrCopy2(&permission->pShardCategory, "none");
		eaPushEArray((NOCONST(AccountPermissionValue) ***) &permission->ppPermissions, 
			(NOCONST(AccountPermissionValue) ***) &ppPermissions);
		permission->iAccessLevel = iAccessLevel;
		eaPush(&account->ppPermissionCache, permission);
	}
}

static void populateDefaultPermissions(SA_PARAM_NN_VALID AccountInfo *pAccount)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	ProductDefaultPermission * const * ppDefaults = NULL;
	U32 uTime = 0;

	PERFINFO_AUTO_START_FUNC();

	ppDefaults = productGetDefaultPermission();
	uTime = timeSecondsSince2000();

	EARRAY_CONST_FOREACH_BEGIN(ppDefaults, iCurDefault, iNumDefaults);
	{
		ProductDefaultPermission *pDefaultPermission = ppDefaults[iCurDefault];

		if ((pDefaultPermission->uStartTime == 0 || pDefaultPermission->uStartTime <= uTime) &&
			(pDefaultPermission->uEndTime == 0 || uTime < pDefaultPermission->uEndTime))
		{
			RequiredSubState eState = hasRequiredSubscription(pAccount,
				&pDefaultPermission->eaRequiredSubscriptions, REQSUBETC_NOW, NULL);

			if (eState && (!pDefaultPermission->bSubsMustBeInternal || eState == REQSUBSTATE_INTERNAL))
			{
				populateAccountPermissions_Helper(pAccount, pDefaultPermission->pProductName, pDefaultPermission->ppPermissions,
					pDefaultPermission->ppShards, pDefaultPermission->uAccessLevel);
			}
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();
}

static void populateAccountPermsFromProduct(AccountInfo *account, const ProductContainer *product)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	if(product && hasOneRequiredSubscription(account, product, REQSUBETC_NOW, NULL))
	{
		populateAccountPermissions_Helper(account, product->pInternalName, product->ppPermissions, product->ppShards, (int)product->uAccessLevel);
	}
}

// Determine if a product has expired after the specified associated time
__forceinline static bool productExpired(U32 uAssociatedTime, SA_PARAM_NN_VALID const ProductContainer *pProduct)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	U32 uDays = pProduct->uExpireDays;
	U32 uSeconds = uDays * SECONDS_PER_DAY;
	U32 uExpireTime = uAssociatedTime + uSeconds;

	if (!uDays) return false;

	if (timeSecondsSince2000() > uExpireTime) return true;
	return false;
}

void accountConstructPermissions (AccountInfo *account)
{
	if (!account)
		return;

	PERFINFO_AUTO_START_FUNC();

	// Empty permission cache for disabled accounts.
	if (accountIsAllowedToLogin(account, true) != LoginFailureCode_Ok)
	{
		if (account->ppPermissionCache)
			accountClearPermissionsCache(account);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (guPermissionCacheTimeToLive)
	{
		if (timeSecondsSince2000() > account->uPermissionCachedTimeSS2000 + guPermissionCacheTimeToLive)
			accountClearPermissionsCache(account);
	}

	// If there's already a permission cache, do nothing.
	if (account->ppPermissionCache)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	account->uPermissionCachedTimeSS2000 = timeSecondsSince2000();

	// !!! After this line (and until the end of the function)
	// DO NOT CLEAR CACHED PERMISSIONS (by calling any functions that do)
	// otherwise, the shallow copies of product permissions will be destroyed
	// which will break the product.

	// Populate our starting lists with products
	PERFINFO_AUTO_START("Products", 1);
	EARRAY_FOREACH_REVERSE_BEGIN(account->ppProducts, i);
		const ProductContainer *product = findProductByName(account->ppProducts[i]->name);
		if(product)
		{
			if (productExpired(account->ppProducts[i]->uAssociatedTimeSS2000, product))
			{
				accountRemoveProduct(account, product, "expired");
			}
			else
			{
				// Populates permissions with shallow copies of product's
				populateAccountPermsFromProduct(account, product);
			}
		}
	EARRAY_FOREACH_END;

	// Populate default permissions
	PERFINFO_AUTO_STOP_START("Default Permissions", 1);
	populateDefaultPermissions(account);

	// Clean up shallow copies while writing permission strings
	PERFINFO_AUTO_STOP_START("Write Permission Strings", 1);
	EARRAY_FOREACH_REVERSE_BEGIN(account->ppPermissionCache, i);
		AccountPermission *permission = account->ppPermissionCache[i];
		int j;
		eaQSort(permission->ppPermissions, accountPermissionValueCmp);

		// Remove duplicates
		for (j = eaSize(&permission->ppPermissions) - 1; j > 0; j--)
		{
			AccountPermissionValue *pFirstValue = permission->ppPermissions[j];
			AccountPermissionValue *pSecondValue = permission->ppPermissions[j - 1];

			devassert(pFirstValue && pSecondValue);

			if (stricmp_safe(pFirstValue->pType, pSecondValue->pType) == 0 &&
				stricmp_safe(pFirstValue->pValue, pSecondValue->pValue) == 0)
			{
				eaRemove(&permission->ppPermissions, j);
			}
		}

		writePermissionString(permission);
		eaDestroy(&permission->ppPermissions); // Destroys shallow copies
	EARRAY_FOREACH_END;
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP_FUNC();
}

void accountRegenerateGUID(U32 uAccountID)
{
	AutoTrans_trAccountRegenerateGUID(NULL, objServerType(), GLOBALTYPE_ACCOUNT, uAccountID);
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Globallyuniqueid, .Uid");
enumTransactionOutcome trAccountRegenerateGUID(ATR_ARGS, NOCONST(AccountInfo) *pAccount)
{
	generateAccountGUID(pAccount);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Pprefundedsubscriptions");
enumTransactionOutcome trAccountRecordSubscriptionRefund(ATR_ARGS, NOCONST(AccountInfo) *pAccount, SA_PARAM_NN_STR const char *pSubscriptionVid)
{
	NOCONST(RefundedSubscription) *pRefund;

	if (!pAccount->ppRefundedSubscriptions)
	{
		eaIndexedEnableNoConst(&pAccount->ppRefundedSubscriptions, parse_RefundedSubscription);
	}

	// See if it is already recorded as having been refunded
	if (eaIndexedFindUsingString(&pAccount->ppRefundedSubscriptions, pSubscriptionVid) >= 0)
		return TRANSACTION_OUTCOME_SUCCESS;

	pRefund = StructCreateNoConst(parse_RefundedSubscription);

	pRefund->uRefundedSS2000 = timeSecondsSince2000();
	pRefund->pSubscriptionVID = strdup(pSubscriptionVid);

	eaIndexedAdd(&pAccount->ppRefundedSubscriptions, (RefundedSubscription *)pRefund);

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Note that a subscription was refunded.
void accountRecordSubscriptionRefund(SA_PARAM_NN_VALID AccountInfo *account, SA_PARAM_NN_STR const char *pSubscriptionVid)
{
	if (!pSubscriptionVid || !*pSubscriptionVid) return;

	AutoTrans_trAccountRecordSubscriptionRefund(NULL, objServerType(), GLOBALTYPE_ACCOUNT, account->uID, pSubscriptionVid); 

	accountMarkSubsRefunded(account);
}

// Transaction to set the password secret questions and answers for an account.  See setSecretQuestions().
AUTO_TRANSACTION
ATR_LOCKS(account, ".Personalinfo.Secretquestionsanswers");
enumTransactionOutcome trAccountSetSecretQuestions(ATR_ARGS, NOCONST(AccountInfo) *account, QuestionsAnswers *pQuestionsAnswers)
{
	NOCONST(AccountQuestionsAnswers) *pOldQuestionsAnswers = &account->personalInfo.secretQuestionsAnswers;
	eaCopyEStrings(&pQuestionsAnswers->questions, &pOldQuestionsAnswers->questions);
	eaCopyEStrings(&pQuestionsAnswers->answers, &pOldQuestionsAnswers->answers);
	EARRAY_FOREACH_BEGIN(pOldQuestionsAnswers->questions, i);
		if (!estrLength(&pOldQuestionsAnswers->questions[i]))
		{
			estrDestroy(&pOldQuestionsAnswers->questions[i]);
			estrDestroy(&pOldQuestionsAnswers->answers[i]);
			eaRemove(&pOldQuestionsAnswers->questions, i);
			eaRemove(&pOldQuestionsAnswers->answers, i);
			--i;
		}
	EARRAY_FOREACH_END;
	return TRANSACTION_OUTCOME_SUCCESS;
}

CachedAccountSubscription * findAccountSubscriptionByInternalName(const AccountInfo *account, const char *pSubInternalName)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	if(!account)
		return NULL;

	if(!account->pCachedSubscriptionList)
		return NULL;

	if(eaSize(&account->pCachedSubscriptionList->ppList) == 0)
		return NULL;

	EARRAY_CONST_FOREACH_BEGIN(account->pCachedSubscriptionList->ppList, i, s);
		if(!strcmp(account->pCachedSubscriptionList->ppList[i]->internalName, pSubInternalName))
			return account->pCachedSubscriptionList->ppList[i];
	EARRAY_FOREACH_END;

	return NULL;
}

const CachedAccountSubscription * findAccountSubscriptionByVID(const AccountInfo *pAccount, const char *pVID)
{
	if (!pAccount->pCachedSubscriptionList)
		return NULL;

	if (eaSize(&pAccount->pCachedSubscriptionList->ppList) == 0)
		return NULL;

	EARRAY_CONST_FOREACH_BEGIN(pAccount->pCachedSubscriptionList->ppList, i, s);
		if (!strcmp(pAccount->pCachedSubscriptionList->ppList[i]->vindiciaID, pVID))
			return pAccount->pCachedSubscriptionList->ppList[i];
	EARRAY_FOREACH_END;

	return NULL;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Unextpendingactionid, .Pppendingactions");
enumTransactionOutcome trAccountAddPendingAction(ATR_ARGS, NOCONST(AccountInfo) *pAccount, NON_CONTAINER const AccountPendingAction *pPendingActionOriginal)
{
	AccountPendingAction *pPendingAction = StructClone(parse_AccountPendingAction, pPendingActionOriginal);

	assert(pPendingAction);

	// 0 is an invalid ID
	if (!pAccount->uNextPendingActionID)
		pAccount->uNextPendingActionID++;

	while (eaIndexedFindUsingInt(&pAccount->ppPendingActions, pAccount->uNextPendingActionID) >= 0)
	{
		pAccount->uNextPendingActionID++;

		if (!pAccount->uNextPendingActionID)
			pAccount->uNextPendingActionID++;
	}

	CONTAINER_NOCONST(AccountPendingAction, pPendingAction)->uID = pAccount->uNextPendingActionID;
	CONTAINER_NOCONST(AccountPendingAction, pPendingAction)->uCreatedTime = timeSecondsSince2000();

	eaIndexedAdd(&pAccount->ppPendingActions, pPendingAction);

	pAccount->uNextPendingActionID++;

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Pppendingactions");
enumTransactionOutcome trAccountRemovePendingAction(ATR_ARGS, NOCONST(AccountInfo) *pAccount, U32 uPendingActionID)
{
	int index = eaIndexedFindUsingInt(&pAccount->ppPendingActions, uPendingActionID);

	if (index < 0) return TRANSACTION_OUTCOME_FAILURE;

	StructDestroyNoConst(parse_AccountPendingAction, eaRemove(&pAccount->ppPendingActions, index));

	return TRANSACTION_OUTCOME_SUCCESS;
}

U32 accountAddPendingAction(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_VALID const AccountPendingAction *pPendingAction)
{
	AutoTrans_trAccountAddPendingAction(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID, pPendingAction); 
	return pAccount->uNextPendingActionID - 1;
}

U32 accountAddPendingRefreshSubCache(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pSubscriptionVID)
{
	NOCONST(AccountPendingAction) *pAction = StructCreateNoConst(parse_AccountPendingAction);
	NOCONST(AccountPendingRefreshSubCache) *pRefreshSubCache = StructCreateNoConst(parse_AccountPendingRefreshSubCache);
	U32 uRet;

	pRefreshSubCache->pSubscriptionVID = strdup(pSubscriptionVID);

	pAction->eType = APAT_REFRESH_SUB_CACHE;
	pAction->pRefreshSubCache = pRefreshSubCache;

	uRet = accountAddPendingAction(pAccount, (AccountPendingAction *)pAction);
	StructDestroyNoConst(parse_AccountPendingAction, pAction);
	return uRet;
}

U32 accountAddPendingFinishDelayedTrans(SA_PARAM_NN_VALID const AccountInfo *pAccount, U32 uPurchaseID)
{
	NOCONST(AccountPendingAction) *pAction = StructCreateNoConst(parse_AccountPendingAction);
	NOCONST(AccountPendingFinishDelayedTrans) *pFinishDelayedTrans = StructCreateNoConst(parse_AccountPendingFinishDelayedTrans);
	U32 uRet;

	pFinishDelayedTrans->uPurchaseID = uPurchaseID;

	pAction->eType = APAT_FINISH_DELAYED_TRANS;
	pAction->pFinishDelayedTrans = pFinishDelayedTrans;

	uRet = accountAddPendingAction(pAccount, (AccountPendingAction *)pAction);
	StructDestroyNoConst(parse_AccountPendingAction, pAction);
	return uRet;
}

static void accountDoPendingFinishDelayedTrans_Callback(PurchaseResult eResult,
														SA_PARAM_OP_VALID BillingTransaction *pTrans,
														PurchaseSession *pPurchaseSession,
														SA_PARAM_OP_VALID void *pUserData)
{
	if (eResult == PURCHASE_RESULT_RETRIEVED)
	{
		PurchaseProductFinalize(pPurchaseSession, true, NULL, NULL);
	}
}

static bool accountDoPendingFinishDelayedTrans(SA_PARAM_NN_OP_VALID BillingTransaction **pTrans,
											   SA_PARAM_NN_VALID const AccountInfo *pAccount,
											   SA_PARAM_NN_VALID AccountPendingAction *pPendingAction)
{
	BillingTransaction *pNewTrans;

	pNewTrans = PurchaseRetrieveDelayedSession(pAccount, pPendingAction->pFinishDelayedTrans->uPurchaseID, pTrans ? *pTrans : NULL, accountDoPendingFinishDelayedTrans_Callback, NULL);
	
	if (pTrans)
		*pTrans = pNewTrans;

	return true;
}

static bool accountDoPendingRefreshSubCache(SA_PARAM_NN_OP_VALID BillingTransaction **pTrans,
											SA_PARAM_NN_VALID const AccountInfo *pAccount,
											SA_PARAM_NN_VALID AccountPendingAction *pPendingAction)
{
	BillingTransaction *pNewTrans;

	pNewTrans = btUpdateActiveSubscriptions(pAccount->uID, NULL, NULL, NULL);

	if (pTrans)
		*pTrans = pNewTrans;

	return true;
}

AccountPendingActionResult accountDoPendingAction(BillingTransaction **pTrans,
												  const AccountInfo *pAccount,
												  U32 uPendingActionID)
{
	int index = eaIndexedFindUsingInt(&pAccount->ppPendingActions, uPendingActionID);
	AccountPendingAction *pAction;

	if (index < 0) return APAR_NOT_FOUND;

	pAction = pAccount->ppPendingActions[index];

	switch (pAction->eType)
	{
	xcase APAT_FINISH_DELAYED_TRANS:
		if (!accountDoPendingFinishDelayedTrans(pTrans, pAccount, pAction)) return APAR_FAILED;
	xcase APAT_REFRESH_SUB_CACHE:
		if (!accountDoPendingRefreshSubCache(pTrans, pAccount, pAction)) return APAR_FAILED;
	xdefault:
		return APAR_FAILED;
	}

	// Shouldn't fail since we already verified the ID exists
	AutoTrans_trAccountRemovePendingAction(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID, uPendingActionID); 

	return APAR_SUCCESS;
}

PaymentMethodType paymentMethodType(SA_PARAM_NN_VALID const PaymentMethod *pPaymentMethod)
{
	if (!verify(pPaymentMethod)) return PMT_Invalid;

	if (pPaymentMethod->directDebit &&
		pPaymentMethod->directDebit->account && *pPaymentMethod->directDebit->account)
		return PMT_DirectDebit;

	if (pPaymentMethod->payPal &&
		pPaymentMethod->payPal->returnUrl && *pPaymentMethod->payPal->returnUrl)
		return PMT_PayPal;

	if (pPaymentMethod->creditCard &&
		pPaymentMethod->creditCard->account && *pPaymentMethod->creditCard->account)
		return PMT_CreditCard;

	return PMT_Invalid;
}

static bool stringIsNumeric(const char *pString)
{
	if (!pString) return true;
	while (*pString)
	{
		if (*pString < '0' || *pString > '9') return false;
		pString++;
	}
	return true;
}

// Whether or not mandates are required for direct debit
static bool gbMandateRequired = true;
AUTO_CMD_INT(gbMandateRequired, MandateRequired) ACMD_CMDLINE ACMD_CATEGORY(Account_Server_Billing);

PaymentMethodProblem paymentMethodValid(PaymentMethodType eForbiddenTypes,
										const PaymentMethod *pPaymentMethod,
										const CachedPaymentMethod *pCachedPaymentMethod,
										const char *pBankName,
										const char **ppProblemDetails)
{
	bool bCheckMandateFields = false;
	PaymentMethodType eType = PMT_Invalid;

	if (!verify(pPaymentMethod)) return PMP_Invalid;

#define PMV_FAIL(type, details) { if (ppProblemDetails) *ppProblemDetails = (details); return (type); }
#define PMV_REQUIRED_STRING(field) { if (!pPaymentMethod->##field || !*pPaymentMethod->##field) { PMV_FAIL(PMP_MissingField, #field " is required"); } }
#define PMV_NUMERIC_STRING(field) { if (!stringIsNumeric(pPaymentMethod->##field)) { PMV_FAIL(PMP_InvalidField, #field " is not numeric") } }

	switch (paymentMethodType(pPaymentMethod))
	{
	xcase PMT_DirectDebit:
		if (!devassert(pPaymentMethod->directDebit)) return PMP_Invalid;

		PMV_REQUIRED_STRING(directDebit->account);
		PMV_REQUIRED_STRING(directDebit->bankSortCode);

		PMV_NUMERIC_STRING(directDebit->account);
		PMV_NUMERIC_STRING(directDebit->bankSortCode);

		if (strlen(pPaymentMethod->directDebit->account) > 11)
			PMV_FAIL(PMP_InvalidField, "directDebit->account too long (max 11 digits)");

		bCheckMandateFields = true;

		eType = PMT_DirectDebit;

	xcase PMT_PayPal:
		if (!devassert(pPaymentMethod->payPal)) return PMP_Invalid;

		PMV_REQUIRED_STRING(payPal->emailAddress);

		eType = PMT_PayPal;

	xcase PMT_CreditCard:
		if (!devassert(pPaymentMethod->creditCard)) return PMP_Invalid;

		PMV_REQUIRED_STRING(creditCard->account);
		PMV_REQUIRED_STRING(creditCard->CVV2);
		PMV_REQUIRED_STRING(creditCard->expirationDate);

		eType = PMT_CreditCard;

	xdefault:
		if (!pPaymentMethod->VID || !*pPaymentMethod->VID)
			PMV_FAIL(PMP_AmbiguousType, "type ambiguous");

		if (pPaymentMethod->precreated)
		{
			eType = PMT_CreditCard;
			break;
		}

		if (!pCachedPaymentMethod)
			PMV_FAIL(PMP_InvalidField, "VID not matched with cached payment method");

		if (pCachedPaymentMethod->directDebit)
		{
			bCheckMandateFields = true;

			eType = PMT_DirectDebit;
		}
		else if (pCachedPaymentMethod->creditCard)
		{
			eType = PMT_CreditCard;
		}
		else if (pCachedPaymentMethod->payPal)
		{
			eType = PMT_PayPal;
		}
	}

	if (eType != PMT_PayPal && pPaymentMethod->active)
	{
		// Must be present on non-PayPal updates/creates so that we can do a $1 auth
		PMV_REQUIRED_STRING(currency);
	}

	if (bCheckMandateFields && gbMandateRequired)
	{
		if (!pBankName || !*pBankName) // Required for mandates
			PMV_FAIL(PMP_MissingField, "bankName");

		PMV_REQUIRED_STRING(country); // Required for mandates

		if (!mandateExistsForCountry(pPaymentMethod->country))
			PMV_FAIL(PMP_InvalidField, "country has no mandate configured");
	}

	if (eType & eForbiddenTypes)
	{
		PMV_FAIL(PMP_Forbidden, "Payment method type forbidden for account");
	}

#undef PMV_NUMERIC_STRING
#undef PMV_REQUIRED_STRING
#undef PMV_FAIL

	return PMP_None;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Earecruits");
enumTransactionOutcome trAccountNewRecruit(ATR_ARGS, NOCONST(AccountInfo) *pAccount,
										   SA_PARAM_NN_STR const char *pProductInternalName,
										   SA_PARAM_NN_STR const char *pProductKey,
										   SA_PARAM_NN_STR const char *pEmailAddress)
{
	NOCONST(RecruitContainer) *pRecruit = NULL;

	if (!verify(NONNULL(pAccount))) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(pProductInternalName && *pProductInternalName)) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(pProductKey && *pProductKey)) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(pEmailAddress && *pEmailAddress)) return TRANSACTION_OUTCOME_FAILURE;

	pRecruit = StructCreateNoConst(parse_RecruitContainer);

	if (!devassert(pRecruit)) return TRANSACTION_OUTCOME_FAILURE;

	pRecruit->eRecruitState = RS_PendingOffer;
	pRecruit->uCreatedTimeSS2000 = timeSecondsSince2000();
	pRecruit->pEmailAddress = strdup(pEmailAddress);
	pRecruit->pProductKey = strdup(pProductKey);
	pRecruit->pProductInternalName = strdup(pProductInternalName);

	eaPush(&pAccount->eaRecruits, pRecruit);

	return TRANSACTION_OUTCOME_SUCCESS;
}

bool accountNewRecruit(SA_PARAM_NN_VALID AccountInfo *pAccount,
					   SA_PARAM_NN_STR const char *pProductInternalName,
					   SA_PARAM_NN_STR const char *pProductKey,
					   SA_PARAM_NN_STR const char *pEmailAddress)
{
	if (!verify(pAccount)) return false;
	if (!verify(pProductInternalName && *pProductInternalName)) return false;
	if (!verify(pProductKey && *pProductKey)) return false;
	if (!verify(pEmailAddress && *pEmailAddress)) return false;

	AutoTrans_trAccountNewRecruit(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID, pProductInternalName, pProductKey, pEmailAddress);

	accountLog(pAccount, "Recruit added: %s", pEmailAddress);

	return true;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Earecruits");
enumTransactionOutcome trAccountRecruitmentOffered(ATR_ARGS, NOCONST(AccountInfo) *pAccount,
												   SA_PARAM_NN_STR const char *pProductKey)
{
	if (!verify(NONNULL(pAccount))) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(pProductKey && *pProductKey)) return TRANSACTION_OUTCOME_FAILURE;

	EARRAY_CONST_FOREACH_BEGIN(pAccount->eaRecruits, iCurRecruit, iNumRecruits);
	{
		NOCONST(RecruitContainer) *pRecruit = pAccount->eaRecruits[iCurRecruit];

		if (!devassert(pRecruit)) continue;

		if (!stricmp_safe(pRecruit->pProductKey, pProductKey) && recruitOfferable((const RecruitContainer *)pRecruit))
		{
			pRecruit->eRecruitState = RS_Offered;
			pRecruit->uOfferedTimeSS2000 = timeSecondsSince2000();
		}
	}
	EARRAY_FOREACH_END;

	return TRANSACTION_OUTCOME_SUCCESS;
}

bool accountRecruitmentOffered(SA_PARAM_NN_VALID AccountInfo *pAccount,
							   SA_PARAM_NN_STR const char *pProductKey)
{

	if (!verify(pAccount)) return false;
	if (!verify(pProductKey && *pProductKey)) return false;
	
	AutoTrans_trAccountRecruitmentOffered(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID, pProductKey);

	accountLog(pAccount, "Recruitment offer sent to recruit with key: %s", pProductKey);

	return true;
}

bool recruitOfferable(SA_PARAM_NN_VALID const RecruitContainer *pRecruit)
{
	if (!verify(pRecruit)) return false;
	return pRecruit->eRecruitState == RS_Offered || pRecruit->eRecruitState == RS_PendingOffer;
}

AUTO_TRANSACTION
ATR_LOCKS(pRecruiterAccount, ".Earecruits, .Uid")
ATR_LOCKS(pAccount, ".Uid, .Earecruiters");
enumTransactionOutcome trAccountRecruited(ATR_ARGS,
										  NOCONST(AccountInfo) *pRecruiterAccount,
										  NOCONST(AccountInfo) *pAccount,
										  SA_PARAM_NN_STR const char *pProductKey)
{
	if (!verify(NONNULL(pRecruiterAccount))) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(NONNULL(pAccount))) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(pProductKey && *pProductKey)) return TRANSACTION_OUTCOME_FAILURE;

	EARRAY_CONST_FOREACH_BEGIN(pRecruiterAccount->eaRecruits, iCurRecruit, iNumRecruits);
	{
		NOCONST(RecruitContainer) *pRecruit = pRecruiterAccount->eaRecruits[iCurRecruit];

		if (!devassert(pRecruit)) continue;

		if (!stricmp_safe(pRecruit->pProductKey, pProductKey) && pRecruit->eRecruitState == RS_Offered)
		{
			pRecruit->uAccountID = pAccount->uID;

			if (eaIndexedFindUsingString(&pAccount->eaRecruiters, pRecruit->pProductInternalName) < 0)
			{
				NOCONST(RecruiterContainer) *pRecruiter = StructCreateNoConst(parse_RecruiterContainer);

				if (!devassert(pRecruiter)) return TRANSACTION_OUTCOME_FAILURE;

				pRecruit->uAcceptedTimeSS2000 = timeSecondsSince2000();
				pRecruit->eRecruitState = RS_Accepted;

				pRecruiter->uAccountID = pRecruiterAccount->uID;
				pRecruiter->pProductInternalName = strdup(pRecruit->pProductInternalName);
				pRecruiter->uAcceptedTimeSS2000 = pRecruit->uAcceptedTimeSS2000;

				if (!pAccount->eaRecruiters)
				{
					eaIndexedEnableNoConst(&pAccount->eaRecruiters, parse_RecruiterContainer);
				}

				eaIndexedAdd(&pAccount->eaRecruiters, pRecruiter);
			}
			else
			{
				pRecruit->eRecruitState = RS_AlreadyRecruited;
			}

			break;
		}
	}
	EARRAY_FOREACH_END;

	return TRANSACTION_OUTCOME_SUCCESS;
}

RecruitState accountRecruited(SA_PARAM_NN_VALID AccountInfo *pRecruiterAccount,
							  SA_PARAM_NN_VALID AccountInfo *pAccount,
							  SA_PARAM_NN_STR const char *pProductKey)
{
	const RecruitContainer *pRecruit = NULL;

	if (!verify(pRecruiterAccount)) return RS_Invalid;
	if (!verify(pAccount)) return RS_Invalid;
	if (!verify(pProductKey && *pProductKey)) return RS_Invalid;

	PERFINFO_AUTO_START_FUNC();

	// Find the recruit on the recruiter by key.
	EARRAY_CONST_FOREACH_BEGIN(pRecruiterAccount->eaRecruits, iCurRecruit, iNumRecruits);
	{
		const RecruitContainer *pRecruitCandidate = pRecruiterAccount->eaRecruits[iCurRecruit];

		if (!devassert(pRecruitCandidate)) continue;

		if (!stricmp_safe(pRecruitCandidate->pProductKey, pProductKey) && pRecruitCandidate->eRecruitState == RS_Offered)
		{
			pRecruit = pRecruitCandidate;
			break;
		}
	}
	EARRAY_FOREACH_END;

	// Make sure the recruit was found on the recruiter.
	if (!pRecruit)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return RS_Invalid;
	}

	AutoTrans_trAccountRecruited(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pRecruiterAccount->uID, GLOBALTYPE_ACCOUNT, pAccount->uID, pProductKey);

	devassert(pRecruit->eRecruitState == RS_Accepted || pRecruit->eRecruitState == RS_AlreadyRecruited);

	if (pRecruit->eRecruitState == RS_Accepted)
	{
		SendRecruitInfoToAllProxies(pAccount);
		SendRecruitInfoToAllProxies(pRecruiterAccount);

		accountLog(pAccount, "Recruited by: [account:%s]",
			pRecruiterAccount->accountName);

		accountLog(pRecruiterAccount, "Recruited: [account:%s]",
			pAccount->accountName);
	}
	else
	{
		accountLog(pAccount, "Attempted to be recruited by: [account:%s] (but already a recruit)",
			pRecruiterAccount->accountName);

		accountLog(pRecruiterAccount, "Attempted to recruit: [account:%s] (but already recruited)",
			pAccount->accountName);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return pRecruit->eRecruitState;
}

static bool validRecruitStateTransition(RecruitState eOldState, RecruitState eNewState)
{
	switch (eNewState)
	{
		xcase RS_Invalid:
			return false;
		xcase RS_PendingOffer:
			return false;
		xcase RS_Offered:
			if (eOldState == RS_PendingOffer) return true;
			return false;
		xcase RS_Accepted:
			if (eOldState == RS_PendingOffer ||
				eOldState == RS_Offered) return true;
			return false;
		xcase RS_Upgraded:
			if (eOldState == RS_Accepted) return true;
			return false;
		xcase RS_Billed:
			if (eOldState == RS_Upgraded) return true;
			return false;
		xcase RS_OfferCancelled:
			if (eOldState == RS_Offered) return true;
			return false;
		xcase RS_AlreadyRecruited:
			if (eOldState == RS_Offered) return true;
			return false;
	}
	return false;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Earecruits");
enumTransactionOutcome trAccountSetRecruitState(ATR_ARGS,
												NOCONST(AccountInfo) *pAccount,
												int iRecruitIndex,
												int iRecruitState)
{
	RecruitState eState = iRecruitState;
	NOCONST(RecruitContainer) *pRecruit = NULL;

	if (!verify(NONNULL(pAccount))) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(eState != RS_Invalid)) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(iRecruitIndex >= 0 && iRecruitIndex < eaSize(&pAccount->eaRecruits))) return TRANSACTION_OUTCOME_FAILURE;

	pRecruit = pAccount->eaRecruits[iRecruitIndex];

	if (!devassert(pRecruit)) return TRANSACTION_OUTCOME_FAILURE;

	if (!validRecruitStateTransition(pRecruit->eRecruitState, eState))
		return TRANSACTION_OUTCOME_FAILURE;

	pRecruit->eRecruitState = eState;

	return TRANSACTION_OUTCOME_SUCCESS;
}

static int getRecruitIndexFromAccountID(SA_PARAM_NN_VALID const AccountInfo *pAccount,
										U32 uRecruitAccountID,
										SA_PARAM_NN_STR const char *pProductInternalName)
{
	int iIndex = -1;

	if (!verify(pAccount)) return -1;
	if (!verify(uRecruitAccountID)) return -1;
	if (!verify(pProductInternalName && *pProductInternalName)) return -1;

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(pAccount->eaRecruits, iCurRecruit, iNumRecruits);
	{
		const RecruitContainer *pRecruit = pAccount->eaRecruits[iCurRecruit];

		if (!devassert(pRecruit)) continue;

		if (pRecruit->eRecruitState == RS_AlreadyRecruited) continue; // Ignore these
		if (pRecruit->eRecruitState == RS_OfferCancelled) continue; // Ignore these

		if (pRecruit->uAccountID == uRecruitAccountID &&
			!stricmp_safe(pRecruit->pProductInternalName, pProductInternalName))
		{
			devassertmsgf(iIndex == -1,
				"The combination of product internal name and recruit account ID should be unique.  First status: %s; second status: %s; account: %s",
				StaticDefineIntRevLookupNonNull(RecruitStateEnum, pAccount->eaRecruits[iIndex]->eRecruitState),
				StaticDefineIntRevLookupNonNull(RecruitStateEnum, pRecruit->eRecruitState),
				pAccount->accountName);
			iIndex = iCurRecruit;
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();

	return iIndex;
}

bool accountSetRecruitStateByAccountID(SA_PARAM_NN_VALID AccountInfo *pAccount,
									   U32 uRecruitAccountID,
									   SA_PARAM_NN_STR const char *pProductInternalName,
									   RecruitState eState)
{
	int iRecruitIndex = -1;

	if (!verify(pAccount)) return false;
	if (!verify(uRecruitAccountID)) return false;
	if (!verify(pProductInternalName && *pProductInternalName)) return false;
	if (!verify(eState != RS_Invalid)) return false;

	PERFINFO_AUTO_START_FUNC();

	iRecruitIndex = getRecruitIndexFromAccountID(pAccount, uRecruitAccountID, pProductInternalName);

	if (iRecruitIndex >= 0)
	{
		const RecruitContainer *pRecruit = pAccount->eaRecruits[iRecruitIndex];

		if (devassert(pRecruit) && validRecruitStateTransition(pRecruit->eRecruitState, eState))
		{
			AutoTrans_trAccountSetRecruitState(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID, iRecruitIndex, eState);

			accountLog(pAccount, "Recruit state changed for recruit with key %s: %s",
				pRecruit->pProductKey, StaticDefineIntRevLookupNonNull(RecruitStateEnum, eState));
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return iRecruitIndex >= 0;
}

// Give a reward product to a recruiter
static void accountGiveRecruitReward(SA_PARAM_NN_VALID AccountInfo *pAccount,
									 SA_PARAM_NN_VALID const ProductContainer *pProduct,
									 RecruitState eState,
									 SA_PARAM_NN_STR const char *pKey)
{
	bool bProductGiven = false;
	const char * const * eaProducts = NULL;

	if (!verify(pAccount)) return;
	if (!verify(pProduct)) return;
	if (!verify(eState == RS_Upgraded || eState == RS_Billed)) return;
	if (!verify(pKey && *pKey)) return;

	PERFINFO_AUTO_START_FUNC();

	eaProducts = eState == RS_Upgraded ? pProduct->recruit.eaUpgradedProducts : pProduct->recruit.eaBilledProducts;

	EARRAY_CONST_FOREACH_BEGIN(eaProducts, iCurProduct, iNumProducts);
	{
		const char *pProductToGive = eaProducts[iCurProduct];
		const ProductContainer *pProductReward = NULL;

		if (!devassert(pProductToGive && *pProductToGive)) continue;

		// Make sure it is configured to give such a product
		pProductReward = findProductByName(pProductToGive);

		if (devassert(pProductReward))
		{
			// Activate the reward product
			ActivateProductResult eResult = accountActivateProduct(pAccount, pProductReward, NULL, NULL, NULL, pKey, NULL, 0, NULL, false);

			if (eResult == APR_Success)
			{
				bProductGiven = true;
			}
			else
			{
				accountLog(pAccount, "Recruit %s but product ([product:%s]) could not be given: %s",
					eState == RS_Upgraded ? "upgraded" : "billed",
					pProductReward->pName,
					StaticDefineIntRevLookupNonNull(ActivateProductResultEnum, eResult));
			}
		}
	}
	EARRAY_FOREACH_END;

	if (!bProductGiven)
	{
		accountLog(pAccount, "Recruit %s but no products were given as a reward",
			eState == RS_Upgraded ? "upgraded" : "billed");
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Mark a recruit on an account as being upgraded
bool accountUpgradeRecruit(SA_PARAM_NN_VALID AccountInfo *pAccount,
						   U32 uRecruitAccountID,
						   SA_PARAM_NN_STR const char *pProductInternalName,
						   RecruitState eState)
{
	int iRecruitIndex = -1;
	bool bUpgraded = false;

	if (!verify(pAccount)) return false;
	if (!verify(uRecruitAccountID)) return false;
	if (!verify(pProductInternalName && *pProductInternalName)) return false;
	if (!verify(eState == RS_Upgraded || eState == RS_Billed)) return false;

	PERFINFO_AUTO_START_FUNC();

	iRecruitIndex = getRecruitIndexFromAccountID(pAccount, uRecruitAccountID, pProductInternalName);

	if (iRecruitIndex >= 0)
	{
		const RecruitContainer *pRecruit = pAccount->eaRecruits[iRecruitIndex];

		if (devassert(pRecruit) && validRecruitStateTransition(pRecruit->eRecruitState, eState))
		{
			bUpgraded = accountSetRecruitStateByAccountID(pAccount, uRecruitAccountID, pProductInternalName, eState);

			if (bUpgraded)
			{
				const AccountInfo *pRecruitedAccount = findAccountByID(uRecruitAccountID);
				EARRAY_OF(ProductContainer) eaProducts = NULL;
				findProductsFromKey(pRecruit->pProductKey, &eaProducts);

				EARRAY_CONST_FOREACH_BEGIN(eaProducts, iCurProduct, iNumProducts);
				{
					const ProductContainer *pProduct = eaProducts[iCurProduct];

					if (!devassert(pProduct)) continue;

					accountGiveRecruitReward(pAccount, pProduct, eState, pRecruit->pProductKey);
				}
				EARRAY_FOREACH_END;

				eaDestroyStruct(&eaProducts, parse_ProductContainer);

				if (devassert(pRecruitedAccount))
				{
					accountLog(pAccount, "Recruit %s: [account:%s]", eState == RS_Upgraded ? "upgraded" : "billed", pRecruitedAccount->accountName);
				}
			}
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return bUpgraded;
}

// Functions for basic Fixture testing

static void trCreateAccount_SimpleCB(TransactionReturnVal *returnVal, void *data)
{
	if (devassert(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS))
	{
		U32 uID = atoi(returnVal->pBaseReturnVals->returnString);
		if (devassert(uID))
		{
			AccountInfo *account = findAccountByID(uID);
			devassert(account);
		}
	}
}

void createAccountFromStruct(AccountInfo *pNewAccount)
{
	char hashedPassword[MAX_PASSWORD] = "";
	accountHashPassword(pNewAccount->password_obsolete, hashedPassword);
	strcpy( CONTAINER_NOCONST(AccountInfo, pNewAccount)->password_obsolete, hashedPassword);
	if (!pNewAccount->globallyUniqueID || !*pNewAccount->globallyUniqueID) {
		generateAccountGUID(CONTAINER_NOCONST(AccountInfo, pNewAccount));
	}
	objRequestContainerCreateLocal(objCreateManagedReturnVal(trCreateAccount_SimpleCB, NULL), GLOBALTYPE_ACCOUNT, pNewAccount);
}

void AccountManagement_DestroyContainers(void)
{
	objRemoveAllContainersWithType(GLOBALTYPE_ACCOUNT);
	stashTableClear(sAccountNameStash);
	stashTableClear(sAccountEmailStash);
	stashTableClear(sGUIDStash);
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Easpendingcaps");
enumTransactionOutcome trAccountSetSpendingCap(ATR_ARGS,
											   NOCONST(AccountInfo) *pAccount,
											   SA_PARAM_NN_STR const char *pCurrency,
											   float fCap)
{
	bool bFound = false;

	EARRAY_FOREACH_REVERSE_BEGIN(pAccount->eaSpendingCaps, iCurCap);
	{
		NOCONST(AccountSpendingCap) *pCap = pAccount->eaSpendingCaps[iCurCap];

		if (devassert(pCap) && !stricmp_safe(pCap->pCurrency, pCurrency))
		{
			if (fCap == -1)
			{
				StructDestroyNoConst(parse_AccountSpendingCap, eaRemoveFast(&pAccount->eaSpendingCaps, iCurCap));
			}
			else
			{
				pCap->fAmount = fCap;
			}
			bFound = true;
			break;
		}
	}
	EARRAY_FOREACH_END;

	if (fCap != -1 && !bFound)
	{
		NOCONST(AccountSpendingCap) *pCap = CONTAINER_NOCONST(AccountSpendingCap, StructCreate(parse_AccountSpendingCap));
		pCap->fAmount = fCap;
		pCap->pCurrency = strdup(pCurrency);
		eaPush(&pAccount->eaSpendingCaps, pCap);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

// -1 to remove
void accountSetSpendingCap(SA_PARAM_NN_VALID AccountInfo *pAccount,
						   SA_PARAM_NN_STR const char *pCurrency,
						   float fCap)
{
	if (!verify(pAccount)) return;
	if (!verify(pCurrency && *pCurrency)) return;
	if (!verify(fCap >= 0 || fCap == -1)) return;

	PERFINFO_AUTO_START_FUNC();

	AutoTrans_trAccountSetSpendingCap(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID, pCurrency, fCap);

	if (fCap != -1)
	{
		accountLog(pAccount, "Spending cap set: %f %s", fCap, pCurrency);
	}
	else
	{
		accountLog(pAccount, "Spending cap reset to default for %s", pCurrency);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

bool accountDelete(SA_PARAM_NN_VALID AccountInfo *pAccount)
{	
	if (pAccount->temporaryFlags & ACCOUNT_ACCESSED_FLAG)
		return false;
	PERFINFO_AUTO_START_FUNC();
	// AccountServerLogBatch == AccountLogBatch
	EARRAY_INT_CONST_FOREACH_BEGIN(pAccount->eauLogBatchIDs, i, n);
	{
		destroyAccountLogBatch(pAccount->eauLogBatchIDs[i]);
	}
	EARRAY_FOREACH_END;

	EARRAY_INT_CONST_FOREACH_BEGIN(pAccount->eauIndexedLogBatchIDs, i, n);
	{
		destroyAccountLogBatch(pAccount->eauIndexedLogBatchIDs[i]);
	}
	EARRAY_FOREACH_END;

	EARRAY_INT_CONST_FOREACH_BEGIN(pAccount->eauRebucketLogBatchIDs, i, n);
	{
		destroyAccountLogBatch(pAccount->eauRebucketLogBatchIDs[i]);
	}
	EARRAY_FOREACH_END;

	// AccountServerPurchaseLog == PurchaseLogContainer
	DestroyPurchaseLogs(pAccount->uID);

	// AccountServerInternalSubscription == InternalSubscription
	destroyInternalSubsByAccountID(pAccount->uID);

	removeAccountFromStashTables(pAccount);
	objRequestContainerDestroyLocal(NULL, GLOBALTYPE_ACCOUNT, pAccount->uID);
	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

STRING_EARRAY accountGetInternalProductList(const AccountInfo * pAccount, SubState eState, STRING_EARRAY * peaOutInternalSubList)
{
	STRING_EARRAY eaProductInternalNames = NULL;
	const ProductDefaultPermission * const * ppDefaults = NULL;

	if (!verify(pAccount)) return NULL;

	PERFINFO_AUTO_START_FUNC();

	// Add all the products associated with the account
	EARRAY_CONST_FOREACH_BEGIN(pAccount->ppProducts, iCurProduct, iNumProducts);
	{
		const AccountProductSub * pAccountProduct = pAccount->ppProducts[iCurProduct];
		const ProductContainer * pProduct = NULL;
		const char * pSubInternalName = NULL;

		if (!devassert(pAccountProduct)) continue;

		pProduct = findProductByName(pAccountProduct->name);

		if (!devassert(pProduct)) continue;

		if (eState != SUBSTATE_EXTERNALACTIVEONLY ||
			hasOneRequiredSubscription(pAccount, pProduct, REQSUBETC_LAST_UPDATED, &pSubInternalName) == REQSUBSTATE_EXTERNAL)
		{
			eaPush(&eaProductInternalNames, strdup(pProduct->pInternalName));
			if (peaOutInternalSubList) eaPush(peaOutInternalSubList, strdup(pSubInternalName));
		}
	}
	EARRAY_FOREACH_END;

	if (eState != SUBSTATE_EXTERNALACTIVEONLY)
	{
		// Add all the sub history entries
		EARRAY_CONST_FOREACH_BEGIN(pAccount->ppSubscriptionHistory, iCurHistory, iNumHistories);
		{
			const SubscriptionHistory * pHistory = pAccount->ppSubscriptionHistory[iCurHistory];

			if (!devassert(pHistory)) continue;

			eaPush(&eaProductInternalNames, strdup(pHistory->pProductInternalName));

			// There could be multiple, but return the last (most recently added)
			if (peaOutInternalSubList) eaPush(peaOutInternalSubList,
				strdup(pHistory->eaArchivedEntries[eaSize(&pHistory->eaArchivedEntries) - 1]->pSubInternalName));
		}
		EARRAY_FOREACH_END;
	}

	// Add all the global default permission entries
	ppDefaults = productGetDefaultPermission();
	EARRAY_CONST_FOREACH_BEGIN(ppDefaults, iCurDefault, iNumDefaults);
	{
		const ProductDefaultPermission * pDefaultPermission = ppDefaults[iCurDefault];
		const char * pSubInternalName = NULL;

		if (eState != SUBSTATE_EXTERNALACTIVEONLY ||
			hasRequiredSubscription(pAccount, &pDefaultPermission->eaRequiredSubscriptions,
				REQSUBETC_LAST_UPDATED, &pSubInternalName) == REQSUBSTATE_EXTERNAL)
		{
			eaPush(&eaProductInternalNames, strdup(pDefaultPermission->pProductName));
			if (peaOutInternalSubList) eaPush(peaOutInternalSubList, strdup(pSubInternalName));
		}
	}
	EARRAY_FOREACH_END;

	// Remove duplicates (only if we're not trying maintain a 1:1 mapping with subs)
	if (!peaOutInternalSubList)
	{
		eaRemoveDuplicateStrings(&eaProductInternalNames);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return eaProductInternalNames;
}

typedef struct AccountSendEventEmailData
{
	unsigned int uID;
	const AccountInfo * pAccount;
	jsonrpcFinishedCB pCallback;
	void * pUserData;
} AccountSendEventEmailData;

void accountSendEventEmail_CB(JSONRPCState * pState, void * pUserData)
{
	AccountSendEventEmailData * pData = pUserData;
	char * pLogLine = NULL;

	if (!devassert(pData)) return;

	PERFINFO_AUTO_START_FUNC();

	logAppendPairs(&pLogLine,
		logPair("ID", "%u", pData->uID),
		logPair("Code", "%i", pState->responseCode),
		logPair("Error", "%s", pState->error ? pState->error : "none"),
		NULL);
	objLog(LOG_ACCOUNT_SERVER_EVENTS, GLOBALTYPE_ACCOUNT, pData->pAccount->uID,
		0, pData->pAccount->displayName, NULL, NULL, "EventEmailResponse", NULL, "%s", pLogLine);
	estrDestroy(&pLogLine);

	if (pData->pCallback) pData->pCallback(pState, pData->pUserData);

	free(pData);
	pData = NULL;

	PERFINFO_AUTO_STOP_FUNC();
}

static unsigned int guSendEventID = 0; // Ever-incrementing number used for logging

void accountSendEventEmail(SA_PARAM_NN_VALID const AccountInfo * pAccount, SA_PARAM_OP_STR const char * pLocale,
	SA_PARAM_NN_STR const char * pEmailType, SA_PARAM_NN_VALID const WebSrvKeyValueList * pKeyList,
	SA_PARAM_OP_VALID jsonrpcFinishedCB pCallback, SA_PARAM_OP_VALID void * pUserData)
{
	const AccountServerConfig * pConfig = NULL;

	if (!verify(pAccount)) return;
	if (!verify(pEmailType && *pEmailType)) return;
	if (!verify(pKeyList)) return;

	PERFINFO_AUTO_START_FUNC();

	pConfig = GetAccountServerConfig();
	if (!nullStr(pConfig->pWebSrvAddress) && pConfig->iWebSrvPort)
	{
		const char * pEmail = accountGetEmail(pAccount);
		char * pLogLine = NULL;
		AccountSendEventEmailData * pData = NULL;

		if (!pEmail || nullStr(pEmail)) {
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}

		if (!pLocale) {
			pLocale = pAccount->defaultLocale ? pAccount->defaultLocale : locGetCode(DEFAULT_LOCALE_ID);
		}

		pData = callocStruct(AccountSendEventEmailData);
		pData->pAccount = pAccount;
		pData->pCallback = pCallback;
		pData->pUserData = pUserData;
		pData->uID = guSendEventID;

		jsonrpcCreate(commDefault(), pConfig->pWebSrvAddress, pConfig->iWebSrvPort, "/rpc/", accountSendEventEmail_CB, pData, NULL, 
			"Email.trigger_email_event", 4, 
			JT_STRING, pEmail,
			JT_STRING, pEmailType,
			JT_STRING, pLocale,
			JT_OBJECT, parse_WebSrvKeyValueList, pKeyList);

		logAppendPairs(&pLogLine,
			logPair("ID", "%u", guSendEventID),
			logPair("EmailType", "%s", pEmailType),
			logPair("Locale", "%s", pLocale),
			logPair("EmailAddress", "%s", pEmail),
			NULL);
		EARRAY_CONST_FOREACH_BEGIN(pKeyList->kvList, iCurItem, iNumItems);
		{
			const WebSrvKeyValue * pKeyValue = pKeyList->kvList[iCurItem];

			if (!devassert(pKeyValue)) continue;

			logAppendPairs(&pLogLine, logPair(pKeyValue->key, "%s", pKeyValue->value), NULL);
		}
		EARRAY_FOREACH_END;

		objLog(LOG_ACCOUNT_SERVER_EVENTS, GLOBALTYPE_ACCOUNT, pAccount->uID,
			0, pAccount->displayName, NULL, NULL, "EventEmailRequest", NULL, "%s", pLogLine);
		estrDestroy(&pLogLine);

		guSendEventID++;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

#include "rand.h"

static void makeRandomAlphabetString(char *pOutBuf, int iLen)
{
	int i;

	for (i = 0; i < iLen; i++)
	{
		pOutBuf[i] = randomIntRange('a', 'z');
	}

	pOutBuf[iLen] = 0;
}


AUTO_COMMAND;
void TestCreateManyAccounts(int iHowMany)
{
	int i;
	for (i = 0; i < iHowMany; i++)
	{
		char accountName[128];
		char passwordHashed[128];
		char firstName[128];
		char lastName[128];
		char email[256];

		if (i % 1000 == 0)
		{
			printf("Created %d accounts", i);
		}

		makeRandomAlphabetString(accountName, 32);
		makeRandomAlphabetString(firstName, 12);
		makeRandomAlphabetString(lastName, 12);
		quick_snprintf(accountName + 32, 96, 96, "%d", i);

		accountHashPassword(accountName, passwordHashed);

		sprintf(email, "%s_%s@fake.com", accountName, firstName);


		 createFullAccount(accountName,
					   passwordHashed,
					   accountName,
					   email,
					   firstName,
					   lastName,
					   NULL,
					   NULL,
					   NULL,
					   NULL,
					   false,
					   false,
					   0);
	}
}

#include "AccountManagement_h_ast.c"
#include "AccountManagement_c_ast.c"