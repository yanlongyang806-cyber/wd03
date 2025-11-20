#include "AccountIntegration.h"

#include "AccountEncryption.h"
#include "AccountLog.h"
#include "AccountManagement.h"
#include "accountnet.h"
#include "AccountServer.h"
#include "AccountTransactionLog.h"
#include "billing.h"
#include "crypt.h"
#include "csv.h"
#include "error.h"
#include "ErrorStrings.h"
#include "file.h"
#include "FolderCache.h"
#include "GlobalTypeEnum.h"
#include "KeyValues/KeyValues.h"
#include "logging.h"
#include "objContainer.h"
#include "objIndex.h"
#include "objSchema.h"
#include "objTransactions.h"
#include "StringUtil.h"
#include "StashTable.h"
#include "XMLInterface.h"

#include "AutoGen/AccountIntegration_h_ast.h"
#include "AutoGen/AccountIntegration_c_ast.h"

#include "AutoGen/AccountServer_autotransactions_autogen_wrappers.h"

#define MAX_PWACCOUNTNAME (MAX_ACCOUNTNAME - 40)
#define PWACCOUNT_DEFAULT_MAX_BATCH (100)

#ifdef _M_X64
#define PWACCOUNT_DEFAULT_STASH_SIZE (1000000) 
#else
#define PWACCOUNT_DEFAULT_STASH_SIZE (1000) 
#endif

// Stash Table by both Account and Forum name
static StashTable stPWAccountByAccountName = NULL;
static StashTable stPWAccountByForumName = NULL;
static StashTable stPWAccountByEmail = NULL;

// Conflict tickets: PWCommonAccount * -> expiry time
static StashTable stConflictTickets = NULL;

#define ACCOUNT_INTEGRATION_CONFIG_FILE "server/AccountServer/perfectWorld_Config.txt"

AUTO_STRUCT;
typedef struct PerfectWorldConfigSettings
{
	char *pAutoCreateAccountPrefix;
	char *pAutoCreateAccountSuffix;

	int iMaxAccountsPerBatch; AST(DEFAULT(PWACCOUNT_DEFAULT_MAX_BATCH))
} PerfectWorldConfigSettings;

static PerfectWorldConfigSettings sPerfectWorldConfig = {0};

static void AccountIntegration_ConfigFileUpdate(FolderCache * pFolderCache,
	FolderNode * pFolderNode, int iVirtualLocation,
	const char * szRelPath, int iWhen, void * pUserData)
{
	PERFINFO_AUTO_START_FUNC();
	StructInit(parse_PerfectWorldConfigSettings, &sPerfectWorldConfig);
	if (fileExists(szRelPath))
	{
		ParserReadTextFile(szRelPath, parse_PerfectWorldConfigSettings, &sPerfectWorldConfig, 0);
		printf("Perfect World config loaded: %s\n", szRelPath);
	}
	if (sPerfectWorldConfig.iMaxAccountsPerBatch <= 0)
		sPerfectWorldConfig.iMaxAccountsPerBatch = PWACCOUNT_DEFAULT_MAX_BATCH; // do not allow non-positive values
	PERFINFO_AUTO_STOP();
}

static void AccountIntegration_AutoLoadConfigFromFile(SA_PARAM_NN_STR const char * szFileName)
{
	PERFINFO_AUTO_START_FUNC();
	FolderCacheSetCallbackEx(FOLDER_CACHE_CALLBACK_UPDATE, szFileName, AccountIntegration_ConfigFileUpdate, NULL);
	AccountIntegration_ConfigFileUpdate(NULL, NULL, 0, szFileName, 1, NULL);
	PERFINFO_AUTO_STOP();
}

const char *getAccountIntegrationResultString(AccountIntegrationResult eResult)
{
	switch (eResult)
	{
	xcase ACCOUNT_INTEGRATION_Success:
		return ACCOUNT_HTTP_SUCCESS;

	xcase ACCOUNT_INTEGRATION_EmailConflict:
		return ACCOUNT_HTTP_EMAIL_EXISTS;
	xcase ACCOUNT_INTEGRATION_DisplayNameConflict:
		return ACCOUNT_HTTP_DISPLAYNAME_EXISTS;
	xcase ACCOUNT_INTEGRATION_DisplayNameInvalid:
		return ACCOUNT_HTTP_INVALID_DISPLAYNAME;

	xcase ACCOUNT_INTEGRATION_PWAlreadyLinked:
		return ACCOUNT_HTTP_PWUSER_LINKED;
	xcase ACCOUNT_INTEGRATION_CrypticAlreadyLinked:
		return ACCOUNT_HTTP_CRYPTICUSER_LINKED;
	xcase ACCOUNT_INTEGRATION_NotLinked:
		return ACCOUNT_HTTP_CRYPTICUSER_NOTLINKED;
	xcase ACCOUNT_INTEGRATION_PWBanned:
		return ACCOUNT_HTTP_USER_BANNED;
	xdefault:
		return ACCOUNT_HTTP_INTERNAL_ERROR;
	}
	return "";
}

static PerfectWorldAccountBatch *findPWAccountBatch(U32 uBatchID)
{
	if (!uBatchID)
		return NULL;
	return (PerfectWorldAccountBatch*) objGetContainerData(GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT, uBatchID);
}

PWCommonAccount * findPWCommonAccountbyName(const char *pAccountName)
{
	PWCommonAccount *pAccount = NULL;
	
	if (!pAccountName)
		return NULL;

	PERFINFO_AUTO_START_FUNC();
	if (!devassert(stPWAccountByAccountName))
		stPWAccountByAccountName = stashTableCreateWithStringKeys(PWACCOUNT_DEFAULT_STASH_SIZE, StashDeepCopyKeys);
	stashFindPointer(stPWAccountByAccountName, pAccountName, &pAccount);
	PERFINFO_AUTO_STOP();

	return pAccount;
}

PWCommonAccount * findPWCommonAccountbyForumName(const char *pForumName)
{
	PWCommonAccount *pAccount = NULL;

	if (!pForumName)
		return NULL;

	PERFINFO_AUTO_START_FUNC();
	if (!devassert(stPWAccountByForumName))
		stPWAccountByForumName = stashTableCreateWithStringKeys(PWACCOUNT_DEFAULT_STASH_SIZE, StashDeepCopyKeys);
	stashFindPointer(stPWAccountByForumName, pForumName, &pAccount);
	PERFINFO_AUTO_STOP();

	return pAccount;
}

static bool manglePWEmail(const char * szEmail, char * szOutEmail, size_t uOutSize)
{
	const char * szAtSymbol = strrchr(szEmail, '@');
	const char * szCurInChar = szEmail;
	char * szCurOutChar = szOutEmail;
	bool bMangled = false;

	if (!szAtSymbol)
	{
		return false;
	}

	if (stricmp(szAtSymbol, "@gmail.com"))
	{
		return false;
	}

	while (szCurInChar != szAtSymbol)
	{
		if (*szCurInChar != '.')
		{
			*szCurOutChar = *szCurInChar;
			szCurOutChar++;
		}
		else
		{
			bMangled = true;
		}
		szCurInChar++;
	}

	if (bMangled)
	{
		strcpy_s(szCurOutChar, uOutSize - (szCurOutChar - szOutEmail), szAtSymbol);
	}
	return bMangled;
}

PWCommonAccount * findPWCommonAccountByEmail(const char *pEmail)
{
	PWCommonAccount *pAccount = NULL;

	if (!pEmail)
		return NULL;

	PERFINFO_AUTO_START_FUNC();
	if (!devassert(stPWAccountByEmail))
		stPWAccountByEmail = stashTableCreateWithStringKeys(PWACCOUNT_DEFAULT_STASH_SIZE, StashDeepCopyKeys);
	stashFindPointer(stPWAccountByEmail, pEmail, &pAccount);

	if (!pAccount)
	{
		char szMangledName[MAX_LOGIN_FIELD] = {0};
		if (manglePWEmail(pEmail, SAFESTR(szMangledName)))
		{
			stashFindPointer(stPWAccountByEmail, szMangledName, &pAccount);
		}
	}

	PERFINFO_AUTO_STOP();

	return pAccount;
}

PWCommonAccount * findPWCommonAccountByLoginField(const char *pLoginField)
{
	PWCommonAccount * pAccount = findPWCommonAccountbyName(pLoginField);
	if (!pAccount)
	{
		pAccount = findPWCommonAccountByEmail(pLoginField);
	}
	return pAccount;
}

static bool confirmPassword(const char * pStoredPassword, const char * pPassword, U32 salt)
{
	char saltedPassword[MAX_PASSWORD] = {0};
	char passwordCopy[MAX_PASSWORD] = {0};

	if (nullStr(pStoredPassword)) return false;
	if (nullStr(pPassword)) return false;

	strcpy(passwordCopy, pPassword);

	// It's only safe to strupr the password if the salt is 0
	// because it's in hex in that case and base64 otherwise
	if (!salt) strupr(passwordCopy);

	accountAddSaltToHashedPassword(pStoredPassword, salt, saltedPassword);
	if (strcmp(saltedPassword, passwordCopy) == 0)
	{
		return true;
	}

	return false;
}

bool confirmPerfectWorldLoginPassword(PWCommonAccount *pAccount, const char *pPassword, 
	const char * pPasswordFixedSalt, const char * pFixedSalt, U32 salt, bool bEmailLogin)
{
	if (!pAccount) return false;

	PERFINFO_AUTO_START_FUNC();

	if (AccountIntegration_PWCommonAccountNeedsEncryptionFixup(pAccount))
	{
		if (!AccountIntegration_PWCommonAccountDoEncryptionFixup(pAccount))
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	AccountIntegration_PWCommonAccountDecryptPasswordIfNecessary(pAccount);

	if (pAccount && pPassword)
	{
		if (confirmPassword(pAccount->pPasswordHash_RAM, pPassword, salt) ||
			confirmPassword(pAccount->pPasswordHashFixedSalt_RAM, pPasswordFixedSalt, salt))
		{
			if (nullStr(pAccount->pFixedSalt) &&
				!nullStr(pFixedSalt) && !nullStr(pPasswordFixedSalt) &&
				bEmailLogin && !salt)
			{
				// We just got a hash using a fixed salt, so store it for next time
				AutoTrans_trUpdatePWCommonCrypticFixedHash(NULL, objServerType(),
					GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT, pAccount->uBatchID,
					pAccount->pAccountName, pPasswordFixedSalt, pFixedSalt);

				SAFE_FREE(pAccount->pPasswordHash_RAM);
				SAFE_FREE(pAccount->pPasswordHashFixedSalt_RAM);
			}

			PERFINFO_AUTO_STOP();
			return true;
		}
	}

	PERFINFO_AUTO_STOP();
	return false;
}

AUTO_TRANSACTION ATR_LOCKS(account, ".uid, .pPWAccountName") ATR_LOCKS(pwBatch, ".eaAccounts[]");
enumTransactionOutcome trAccountLinkWithPW(ATR_ARGS, NOCONST(AccountInfo) *account, NOCONST(PerfectWorldAccountBatch) *pwBatch, 
	const char *pAccountName)
{
	NOCONST(PWCommonAccount) *pwAccount = eaIndexedGetUsingString(&pwBatch->eaAccounts, pAccountName);
	if (!pwAccount)
		return TRANSACTION_OUTCOME_FAILURE;
	// Should be clean - still runs everything if not clean
	devassert(nullStr(account->pPWAccountName)  && pwAccount->uLinkedID == 0);
	SAFE_FREE(account->pPWAccountName);
	account->pPWAccountName = StructAllocString(pwAccount->pAccountName);
	pwAccount->uLinkedID = account->uID;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(account, ".pPWAccountName") ATR_LOCKS(pwBatch, ".eaAccounts[]");
enumTransactionOutcome trAccountUnlinkWithPW(ATR_ARGS, NOCONST(AccountInfo) *account, NOCONST(PerfectWorldAccountBatch) *pwBatch, 
	const char *pAccountName)
{
	NOCONST(PWCommonAccount) *pwAccount = eaIndexedGetUsingString(&pwBatch->eaAccounts, pAccountName);
	SAFE_FREE(account->pPWAccountName);
	if (pwAccount)
		pwAccount->uLinkedID = 0;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(account, ".pPWAccountName");
enumTransactionOutcome trAccountLinkClear(ATR_ARGS, NOCONST(AccountInfo) *account)
{
	SAFE_FREE(account->pPWAccountName);
	return TRANSACTION_OUTCOME_SUCCESS;
}

void AccountIntegration_ClearPWAutoCreatedFlag(AccountInfo *account)
{
	objRequestTransactionSimplef(NULL, GLOBALTYPE_ACCOUNT, account->uID, "ClearPWAutoCreate", 
		"set bPWAutoCreated = 0");
}

// Finds the account (if any) that is already using the specified PW name as an Account or Display Name
// Note: this should ONLY be used to check existing PW names as it doesn't check against PW accounts
static AccountInfo *findConflictingAccount(const char *pName)
{
	AccountInfo *account;
	if (!(account = findAccountByDisplayName(pName)))
		account = findAccountByName(pName);
	return account;
}

// Generate a unique account name based on the provided base name
// Replaces all invalid characters with underscores and adds a prefix and/or suffix to guarantee uniqueness
// Note that generated account name can still be invalid due to restricted strings and/or length restrictions, 
// but auto-creation will ignore invalidness because it will not be viewable.
static void autoGenerateAccountName(char **estr, SA_PARAM_NN_VALID PWCommonAccount *pAccount)
{
	// Username usage checks here are done against both PW and Cryptic accounts
	int suffix = 1;
	char name[ASCII_ACCOUNTNAME_MAX_LENGTH];
	char *pBaseFixed = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	estrCopy2(&pBaseFixed, pAccount->pAccountName);

	// Truncate it down so everything will fit
	if (estrLength(&pBaseFixed) > MAX_PWACCOUNTNAME)
		estrRemove(&pBaseFixed, MAX_PWACCOUNTNAME, estrLength(&pBaseFixed) - MAX_PWACCOUNTNAME);

	// Replace all invalid chars with underscores
	estrReplaceMultipleChars(&pBaseFixed, ACCOUNTNAME_INVALID_CHARS, '_');

	// If account name wasn't modified, only check against existing CRYPTIC accounts
	if ((stricmp(pBaseFixed, pAccount->pAccountName) == 0 && !findAccountContainerByName(pBaseFixed)) ||
		!isUsernameUsed(pBaseFixed))
	{
		estrCopy2(estr, pBaseFixed);
		estrDestroy(&pBaseFixed);
		PERFINFO_AUTO_STOP();
		return;
	}

	// Add prefix/suffix
	sprintf(name, "%s%s%s", 
		nullStr(sPerfectWorldConfig.pAutoCreateAccountPrefix) ? "" : sPerfectWorldConfig.pAutoCreateAccountPrefix, 
		pBaseFixed, 
		nullStr(sPerfectWorldConfig.pAutoCreateAccountSuffix) ? "" : sPerfectWorldConfig.pAutoCreateAccountSuffix);
	estrCopy2(&pBaseFixed, name); // Update this base string
	
	// Add additional numbers to the end as needed (starting with 1)
	// Check against all accounts here, because it's started modifying the name
	while (isUsernameUsed(name)) 
	{
		sprintf(name, "%s%d", pBaseFixed, suffix);
		suffix++;
	} 
	estrCopy2(estr, name);
	estrDestroy(&pBaseFixed);
	PERFINFO_AUTO_STOP();
}

AccountIntegrationResult AccountIntegration_LinkAccountToPWCommon(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_VALID PWCommonAccount *pCommon)
{
	// Check if Cryptic Account is linked to a different account
	if (!nullStr(pAccount->pPWAccountName) && stricmp_safe(pAccount->pPWAccountName, pCommon->pAccountName) != 0)
		return ACCOUNT_INTEGRATION_CrypticAlreadyLinked;
	// Check if PW Account is linked to a different account
	if (pCommon->uLinkedID)
		return ACCOUNT_INTEGRATION_PWAlreadyLinked;
	// Check if the PW Account is banned
	if (pCommon->bBanned)
		return ACCOUNT_INTEGRATION_PWBanned;

	AutoTrans_trAccountLinkWithPW(NULL, objServerType(), 
		GLOBALTYPE_ACCOUNT, pAccount->uID, 
		GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT, pCommon->uBatchID, pCommon->pAccountName);
	return ACCOUNT_INTEGRATION_Success;
}

AccountIntegrationResult AccountIntegration_UnlinkAccountFromPWCommon(SA_PARAM_NN_VALID AccountInfo *pAccount)
{
	PerfectWorldAccountBatch *pBatch = NULL;
	PWCommonAccount *pCommon;
	if (nullStr(pAccount->pPWAccountName))
		return ACCOUNT_INTEGRATION_NotLinked;

	pCommon = findPWCommonAccountbyName(pAccount->pPWAccountName);
	if (pCommon)
		pBatch = findPWAccountBatch(pCommon->uBatchID);
	if (!pBatch)
	{
		AutoTrans_trAccountLinkClear(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID);
	}
	else
	{
		if (pCommon && pCommon->uLinkedID != pAccount->uID)
		{
			AssertOrAlert("PWINTEGRATION_ACCOUNTLINK_MISMATCH", "PWCommonAccount link mismatch: [Cryptic %d, PW %d:%s, Cryptic %d]", 
				pAccount->uID, pCommon->uBatchID, pCommon->pAccountName, pCommon->uLinkedID);
		}
		// And clear it always, even if it doesn't match
		AutoTrans_trAccountUnlinkWithPW(NULL, objServerType(), 
			GLOBALTYPE_ACCOUNT, pAccount->uID, 
			GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT, pBatch->uBatchID, pAccount->pPWAccountName);
	}
	return ACCOUNT_INTEGRATION_Success;
}

AccountIntegrationResult AccountIntegration_CreateAccountFromPWCommon(PWCommonAccount *pCommon, const char *pOverrideDisplayName, AccountInfo **ppAccount, AccountCreationResult *peCreateResult)
{
	AccountInfo *account = NULL;
	char *pAccountName = NULL;
	const char *pDisplayName = NULL; 

	PERFINFO_AUTO_START_FUNC();

	if (ppAccount)
		*ppAccount = NULL;
	if (!(pCommon->pAccountName && pCommon->pEmail))
	{
		PERFINFO_AUTO_STOP();
		return ACCOUNT_INTEGRATION_Error;
	}
	if (pCommon->uLinkedID)
	{
		PERFINFO_AUTO_STOP();
		return ACCOUNT_INTEGRATION_PWAlreadyLinked;
	}
	if (pCommon->bBanned)
	{
		PERFINFO_AUTO_STOP();
		return ACCOUNT_INTEGRATION_PWBanned;
	}
	
	if (nullStr(pOverrideDisplayName))
	{
		pDisplayName = pCommon->pForumName;
		// Check for Display Name conflict with Cryptic accounts only
		if (account = findConflictingAccount(pDisplayName))
		{
			PERFINFO_AUTO_STOP();
			return ACCOUNT_INTEGRATION_DisplayNameConflict;
		}
	}
	else
	{
		pDisplayName = pOverrideDisplayName;
		// Check for name conflicts against both PW and Cryptic accounts
		if (isUsernameUsed(pDisplayName))
		{
			PERFINFO_AUTO_STOP();
			return ACCOUNT_INTEGRATION_DisplayNameConflict;
		}
	}

	// Finally, check that the chosen display name is valid for Cryptic restrictions
	if (StringIsInvalidDisplayName(pDisplayName, 0))
	{
		PERFINFO_AUTO_STOP();
		return ACCOUNT_INTEGRATION_DisplayNameInvalid;
	}
	
	account = findConflictingAccount(pCommon->pAccountName);
	if (account || StringIsInvalidAccountName(pCommon->pAccountName, 0))
		autoGenerateAccountName(&pAccountName, pCommon);
	else
		estrCopy2(&pAccountName, pCommon->pAccountName);

	// Auto-generated account has minimal information
	*peCreateResult = createAutogenPWAccount(pAccountName, pDisplayName);
	if (*peCreateResult != ACCOUNTCREATE_Success)
	{
		estrDestroy(&pAccountName);
		PERFINFO_AUTO_STOP();
		return ACCOUNT_INTEGRATION_Error;
	}

	account = findAccountByName(pAccountName);
	devassert(AccountIntegration_LinkAccountToPWCommon(account, pCommon) == ACCOUNT_INTEGRATION_Success);
	if (ppAccount)
		*ppAccount = account;
	estrDestroy(&pAccountName);
	PERFINFO_AUTO_STOP();
	return ACCOUNT_INTEGRATION_Success;
}

typedef struct PWUpdateData
{
	U32 uBatchID;
	char *pAccountName;
	char *pPrevForumName;
} PWUpdateData;

static void trUpdatePWCommonAccountCB(TransactionReturnVal *returnVal, PWUpdateData *trData)
{
	if (!devassert(trData)) return;

	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		PerfectWorldAccountBatch *pPWBatch = findPWAccountBatch(trData->uBatchID);

		if (pPWBatch && trData->pAccountName)
		{
			PWCommonAccount *pPWcom = eaIndexedGetUsingString(&pPWBatch->eaAccounts, trData->pAccountName);

			if (pPWcom)
			{
				if (trData->pPrevForumName && stricmp_safe(trData->pPrevForumName, pPWcom->pForumName))
				{
					stashRemovePointer(stPWAccountByForumName, trData->pPrevForumName, NULL);
				}

				stashAddPointer(stPWAccountByAccountName, pPWcom->pAccountName, pPWcom, true);
				stashAddPointer(stPWAccountByEmail, pPWcom->pEmail, pPWcom, true);

				if (pPWcom->pForumName)
				{
					stashAddPointer(stPWAccountByForumName, pPWcom->pForumName, pPWcom, true);
				}

				// Log account creation.
				objLog(LOG_ACCOUNT_SERVER_GENERAL, GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT, pPWBatch->uBatchID, 0, 
					NULL, NULL, NULL, "PWCommonAccountCreated", NULL, "AccountName %s", pPWcom->pAccountName);
			}
		}
	}

	SAFE_FREE(trData->pAccountName);
	SAFE_FREE(trData->pPrevForumName);
	free(trData);
}

AUTO_TRANS_HELPER;
bool trhEncryptPassword_RAMToDisk(ATH_ARG NOCONST(PWCommonAccount) * pwAccount)
{
	pwAccount->iEncryptionVersion = AccountEncryption_GetCurEncryptionKeyIndex();
	if (pwAccount->pPasswordHash_RAM)
	{
		char *pNewPasswordHash_Disk = AccountEncryption_EncryptEncodeAndAllocString(pwAccount->pPasswordHash_RAM);
		if (!pNewPasswordHash_Disk) return false;
		SAFE_FREE(pwAccount->pPasswordHash_Disk);
		pwAccount->pPasswordHash_Disk = pNewPasswordHash_Disk;
	}
	if (pwAccount->pPasswordHashFixedSalt_RAM)
	{
		char *pNewPasswordHashFixedSalt_Disk = AccountEncryption_EncryptEncodeAndAllocString(pwAccount->pPasswordHashFixedSalt_RAM);
		if (!pNewPasswordHashFixedSalt_Disk) return false;
		SAFE_FREE(pwAccount->pPasswordHashFixedSalt_Disk);
		pwAccount->pPasswordHashFixedSalt_Disk = pNewPasswordHashFixedSalt_Disk;
	}

	return true;
}

AUTO_TRANSACTION ATR_LOCKS(pwBatch, ".eaAccounts[]");
enumTransactionOutcome trUpdatePWCommonAccount(ATR_ARGS, NOCONST(PerfectWorldAccountBatch) *pwBatch, const char *pAccountName, 
	const char *pForumName, const char *pEmail, const char *pPasswordHash,
	const char *pPasswordHashFixedSalt, const char *pFixedSalt,
	int iBanned)
{
	NOCONST(PWCommonAccount) * pwAccount = eaIndexedGetUsingString(&pwBatch->eaAccounts, pAccountName);
	if (!pwAccount)
		return TRANSACTION_OUTCOME_FAILURE;
	if (strcmp_safe(pwAccount->pForumName, pForumName))
	{
		SAFE_FREE(pwAccount->pForumName);
		pwAccount->pForumName = StructAllocString(pForumName);
	}
	if (strcmp_safe(pwAccount->pEmail, pEmail))
	{
		SAFE_FREE(pwAccount->pEmail);
		pwAccount->pEmail = StructAllocString(pEmail);
	}
	if (stricmp_safe(pFixedSalt, pwAccount->pFixedSalt))
	{
		SAFE_FREE(pwAccount->pFixedSalt);
		pwAccount->pFixedSalt = StructAllocString(pFixedSalt);
	}
	if ((!nullStr(pPasswordHash) && stricmp_safe(pPasswordHash, pwAccount->pPasswordHash_RAM)) ||
		(!nullStr(pPasswordHashFixedSalt) && stricmp_safe(pPasswordHashFixedSalt, pwAccount->pPasswordHashFixedSalt_RAM)))
	{
		SAFE_FREE(pwAccount->pPasswordHash_Obsolete);
		SAFE_FREE(pwAccount->pPasswordHash_RAM);
		SAFE_FREE(pwAccount->pPasswordHash_Disk);
		SAFE_FREE(pwAccount->pPasswordHashFixedSalt_RAM);
		SAFE_FREE(pwAccount->pPasswordHashFixedSalt_Disk);

		if (pPasswordHash)
		{
			pwAccount->pPasswordHash_RAM = strdup(pPasswordHash);
		}

		if (pPasswordHashFixedSalt)
		{
			pwAccount->pPasswordHashFixedSalt_RAM = strdup(pPasswordHashFixedSalt);
		}

		pwAccount->bPasswordHashIsFromCryptic = false;

		if (!trhEncryptPassword_RAMToDisk(pwAccount))
		{
			TRANSACTION_RETURN_FAILURE("Unable to encrypt PW");
		}
	}
	pwAccount->bBanned = iBanned;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pwBatch, ".eaAccounts[]");
enumTransactionOutcome trUpdatePWCommonCrypticFixedHash(ATR_ARGS, NOCONST(PerfectWorldAccountBatch) *pwBatch, const char *pAccountName, 
	const char *pPasswordHashFixedSalt, const char *pFixedSalt)
{
	NOCONST(PWCommonAccount) * pwAccount = eaIndexedGetUsingString(&pwBatch->eaAccounts, pAccountName);
	if (!pwAccount)
		return TRANSACTION_OUTCOME_FAILURE;
	pwAccount->bPasswordHashIsFromCryptic = true;
	SAFE_FREE(pwAccount->pPasswordHashFixedSalt_RAM);
	SAFE_FREE(pwAccount->pPasswordHashFixedSalt_Disk);
	SAFE_FREE(pwAccount->pFixedSalt);
	pwAccount->pPasswordHashFixedSalt_RAM = strdup(pPasswordHashFixedSalt);
	pwAccount->pFixedSalt = strdup(pFixedSalt);
	if (!trhEncryptPassword_RAMToDisk(pwAccount))
	{
		TRANSACTION_RETURN_FAILURE("Unable to encrypt PW");
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pwBatch, ".uBatchID, .eaAccounts[], .eaAccounts[AO]");
enumTransactionOutcome trAddPWCommonAccount(ATR_ARGS, NOCONST(PerfectWorldAccountBatch) *pwBatch, const char *pAccountName, 
	const char *pForumName, const char *pEmail, const char *pPasswordHash, const char *pPasswordHashFixedSalt, const char *pFixedSalt, int iBanned)
{
	NOCONST(PWCommonAccount) * pwAccount = eaIndexedGetUsingString(&pwBatch->eaAccounts, pAccountName);
	if (pwAccount)
		return TRANSACTION_OUTCOME_FAILURE;
	pwAccount = StructCreateNoConst(parse_PWCommonAccount);
	if (!pwAccount)
		return TRANSACTION_OUTCOME_FAILURE;
	pwAccount->pAccountName = StructAllocString(pAccountName);
	pwAccount->pForumName = StructAllocString(pForumName);
	pwAccount->pEmail = StructAllocString(pEmail);
	pwAccount->pPasswordHash_RAM = StructAllocString(pPasswordHash);
	pwAccount->pPasswordHashFixedSalt_RAM = StructAllocString(pPasswordHashFixedSalt);
	pwAccount->pFixedSalt = StructAllocString(pFixedSalt);

	if (!trhEncryptPassword_RAMToDisk(pwAccount))
	{
		StructDestroyNoConst(parse_PWCommonAccount, pwAccount);
		TRANSACTION_RETURN_FAILURE("Unable to encrypt PW");
	}

	pwAccount->uBatchID = pwBatch->uBatchID;
	pwAccount->bBanned = iBanned;
	eaIndexedAdd(&pwBatch->eaAccounts, pwAccount);
	return TRANSACTION_OUTCOME_SUCCESS;
}

static bool AccountIntegration_PWCommonHasChanges(PWCommonAccount *pPWAccount, const char *pAccountName, const char *pForumName,
	const char *pEmail, const char *pPasswordHash, const char *pPasswordHashFixedSalt, const char *pFixedSalt, bool bBanned)
{
	if (!pPWAccount)
		return true;
	// All checks are case-sensitive including the password hash (it should already be upper-cased at this point)
	if (strcmp_safe(pPWAccount->pAccountName, pAccountName) != 0)
		return true;
	if (strcmp_safe(pPWAccount->pForumName, pForumName) != 0)
		return true;
	if (strcmp_safe(pPWAccount->pEmail, pEmail) != 0)
		return true;

	AccountIntegration_PWCommonAccountDecryptPasswordIfNecessary(pPWAccount);

	if (strcmp_safe(pPWAccount->pPasswordHash_RAM, pPasswordHash) != 0)
		return true;
	if (strcmp_safe(pPWAccount->pPasswordHashFixedSalt_RAM, pPasswordHashFixedSalt) != 0)
		return true;
	if (strcmp_safe(pPWAccount->pFixedSalt, pFixedSalt) != 0)
		return true;
	if (pPWAccount->bBanned != bBanned)
		return true;
	return false;
}

PerfectWorldUpdateResult AccountIntegration_CreateOrUpdatePWCommon(
	const char *pAccountName, const char *pForumName,
	const char *pEmail, const char *pPasswordHash, const char *pPasswordHashFixedSalt,
	const char *pFixedSalt, bool bBanned)
{
	PWCommonAccount *pExistingCom;
	
	if (pPasswordHash)
		strupr((char*) pPasswordHash); // Force the password hash to uppercase
	if (pPasswordHashFixedSalt)
		strupr((char*) pPasswordHashFixedSalt); // Force the password hash to uppercase

	if (pExistingCom = findPWCommonAccountbyName(pAccountName))
	{
		if (AccountIntegration_PWCommonHasChanges(pExistingCom, pAccountName, pForumName, pEmail, pPasswordHash, pPasswordHashFixedSalt, pFixedSalt, bBanned))
		{
			PerfectWorldAccountBatch *pBatchAccount = findPWAccountBatch(pExistingCom->uBatchID);
			PWUpdateData *pData;
			devassert(pBatchAccount);

			pData = callocStruct(PWUpdateData);
			pData->pAccountName = StructAllocString(pAccountName);
			pData->uBatchID = pExistingCom->uBatchID;
			pData->pPrevForumName = StructAllocString(pExistingCom->pForumName);
			AutoTrans_trUpdatePWCommonAccount(objCreateManagedReturnVal(trUpdatePWCommonAccountCB, pData), objServerType(),
				GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT, pExistingCom->uBatchID,
				pAccountName, pForumName, pEmail, pPasswordHash, pPasswordHashFixedSalt, pFixedSalt, bBanned);
			
			//any time we change a PW common account, we might need to re-decrypt the password
			SAFE_FREE(pExistingCom->pPasswordHash_RAM);
			SAFE_FREE(pExistingCom->pPasswordHashFixedSalt_RAM);

			return PWUPDATE_Updated;
		}
		else
		{
			return PWUPDATE_Useless;
		}
	}
	else if (!nullStr(pPasswordHash) || (!nullStr(pPasswordHashFixedSalt) && !nullStr(pFixedSalt)))
	{
		U32 uMaxBatchID = objContainerGetMaxID(GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT);
		PerfectWorldAccountBatch *pwBatch = uMaxBatchID ? findPWAccountBatch(uMaxBatchID) : NULL;
		PWUpdateData *pData;

		if (!pwBatch || eaSize(&pwBatch->eaAccounts) >= sPerfectWorldConfig.iMaxAccountsPerBatch)
		{
			NOCONST(PerfectWorldAccountBatch) *pwBatchNew = StructCreateNoConst(parse_PerfectWorldAccountBatch);
			objRequestContainerCreateLocal(NULL, GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT, pwBatchNew);
			pwBatch = findPWAccountBatch(uMaxBatchID + 1);
			if (!devassert(pwBatch))
			{
				return PWUPDATE_Error;
			}
		}

		pData = callocStruct(PWUpdateData);
		pData->pAccountName = StructAllocString(pAccountName);
		pData->uBatchID = pwBatch->uBatchID;
		AutoTrans_trAddPWCommonAccount(objCreateManagedReturnVal(trUpdatePWCommonAccountCB, pData), objServerType(),
			GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT, pwBatch->uBatchID,
			pAccountName, pForumName, pEmail, pPasswordHash, pPasswordHashFixedSalt, pFixedSalt, bBanned);
		return PWUPDATE_Created;
	}
	else
	{
		return PWUPDATE_Error;
	}
}

AUTO_TRANSACTION ATR_LOCKS(pwBatch, ".uBatchID, .eaAccounts[AO]");
enumTransactionOutcome trAddPWCommonAccountStruct(ATR_ARGS, NOCONST(PerfectWorldAccountBatch) *pwBatch, NON_CONTAINER PWCommonAccount *pwAccount)
{
	NOCONST(PWCommonAccount)* pwAccountCopy = StructCloneNoConst(parse_PWCommonAccount, CONTAINER_NOCONST(PWCommonAccount, pwAccount));
	pwAccountCopy->uBatchID = pwBatch->uBatchID;
	if (eaIndexedAdd(&pwBatch->eaAccounts, pwAccountCopy))
		return TRANSACTION_OUTCOME_SUCCESS;
	return TRANSACTION_OUTCOME_FAILURE;
}

void AccountIntegration_CreatePWFromStruct(PWCommonAccount *pAccount)
{
	PWUpdateData *pData;
	U32 uMaxBatchID = objContainerGetMaxID(GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT);
	PerfectWorldAccountBatch *pwBatch = uMaxBatchID ? findPWAccountBatch(uMaxBatchID) : NULL;

	if (!pwBatch || eaSize(&pwBatch->eaAccounts) >= sPerfectWorldConfig.iMaxAccountsPerBatch)
	{
		NOCONST(PerfectWorldAccountBatch) *pwBatchNew = StructCreateNoConst(parse_PerfectWorldAccountBatch);
		objRequestContainerCreateLocal(NULL, GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT, pwBatchNew);
		pwBatch = findPWAccountBatch(uMaxBatchID + 1);
		if (!devassert(pwBatch))
			return;
	}
	pData = callocStruct(PWUpdateData);
	pData->pAccountName = StructAllocString(pAccount->pAccountName);
	pData->uBatchID = pwBatch->uBatchID;
	AutoTrans_trAddPWCommonAccountStruct(objCreateManagedReturnVal(trUpdatePWCommonAccountCB, pData), objServerType(), 
		GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT, pwBatch->uBatchID, pAccount);
}

static void pwBatchAddCallback(Container *con, PerfectWorldAccountBatch *pBatch)
{
	EARRAY_FOREACH_BEGIN(pBatch->eaAccounts, i);
	{
		PWCommonAccount *pAccount = pBatch->eaAccounts[i];
		if (pAccount->pAccountName)
		{
			stashAddPointer(stPWAccountByAccountName, pAccount->pAccountName, pAccount, true);
			if (pAccount->pForumName)
				stashAddPointer(stPWAccountByForumName, pAccount->pForumName, pAccount, true);
			if (pAccount->pEmail)
				stashAddPointer(stPWAccountByEmail, pAccount->pEmail, pAccount, true);
		}
	}
	EARRAY_FOREACH_END;
}

void initializeAccountIntegration(void)
{
	AccountIntegration_AutoLoadConfigFromFile(ACCOUNT_INTEGRATION_CONFIG_FILE);

	stPWAccountByAccountName = stashTableCreateWithStringKeys(131072, StashDeepCopyKeys);
	stPWAccountByForumName = stashTableCreateWithStringKeys(131072, StashDeepCopyKeys);
	stPWAccountByEmail = stashTableCreateWithStringKeys(131072, StashDeepCopyKeys);

	// Register schema for PerfectWorld Common Account stub
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT, parse_PerfectWorldAccountBatch, NULL, NULL, NULL, NULL, NULL);
	objRegisterContainerTypeAddCallback(GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT, pwBatchAddCallback);
}

void AccountIntegration_DestroyContainers(void)
{
	objRemoveAllContainersWithType(GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT);
	stashTableClear(stPWAccountByAccountName);
	stashTableClear(stPWAccountByForumName);
	stashTableClear(stPWAccountByEmail);
}

// Perform periodic Account Integration processing.
// -Expire old conflict tickets
void AccountIntegration_Tick()
{
	static U32 last = 0;
	U32 now;
	S64 start;
	U32 iterations = 0;

	PERFINFO_AUTO_START_FUNC();

	// Check the time.
	now = timeSecondsSince2000();

	// Don't do processing if it hasn't been at least a certain amount of time.
	if (now < last + 60)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	last = now;

	// For a limited time, scan each conflict ticket to remove those that are expired.
	start = timerCpuTicks64();
	FOR_EACH_IN_STASHTABLE2(stConflictTickets, elem)
	{
		U32 expiry = stashElementGetInt(elem);
		if (now > expiry)
		{
			bool success = stashRemoveInt(stConflictTickets, stashElementGetKey(elem), NULL);
			devassert(success);
		}
		if (!(iterations % 1024) && timerCpuTicks64() - start > timerCpuSpeed64()/10)
			break;
	}
	FOR_EACH_END;

	PERFINFO_AUTO_STOP_FUNC();
}

/******************************************************/
/* Conflict Tickets - PWE and Cryptic                 */
/******************************************************/

// Generate a conflict ticket ID from an account and an expiry time.
static U32 generateConflictTicketId(void *pAccount, U32 expiry)
{
	static U32 salt = 0;
	U32 hash[8];
	struct {
		void *account;
		U32 expiry;
		U32 salt;
	} info;
	U32 ticket = 0;
	int i;

	// Create a run-unique salt, if necessary.
	while (!salt)
		salt = cryptSecureRand();

	// Hash the account ID and expiry.
	memset(&info, 0, sizeof(info));
	info.account = pAccount;
	info.expiry = expiry;
	info.salt = salt;
	cryptSHA256((void*)&info, sizeof(info), hash);

	// Fold the hash value into a U32.
	for (i = 0; i != sizeof(hash)/sizeof(*hash); ++i)
		ticket ^= hash[i];

	// Don't allow the hash value to be zero.
	if (!ticket)
		ticket = salt;

	return ticket;
}

static U32 createConflictTicket_Internal(void *ptr)
{
	U32 expiry;
	U32 ticket;
	// Compute a hash of account ID and expiry time.
	expiry = timeSecondsSince2000() + CONFLICT_TICKET_LIFESPAN;
	ticket = generateConflictTicketId(ptr, expiry);

	// Create StashTable, if needed.
	if (!stConflictTickets)
		stConflictTickets = stashTableCreateAddress(0);
	// Add to StashTable.
	stashAddInt(stConflictTickets, ptr, expiry, true);
	return ticket;
}

// Find a conflict ticket
static bool findConflictTicketByID_Internal(void *ptr, U32 uTicketID)
{
	int result;
	U32 expiry;
	bool success;
	
	// If there are no tickets, fail.
	if (!stConflictTickets)
		return false;

	// Look up the ticket expiry for this account.
	success = stashFindInt(stConflictTickets, ptr, &result);
	if (!success)
		return false;
	expiry = result;

	// If the ticket is expired, fail.
	if (timeSecondsSince2000() > expiry)
	{
		success = stashRemoveInt(stConflictTickets, ptr, NULL);
		devassert(success);
		return false;
	}

	// Check the ticket ID.
	if (uTicketID != generateConflictTicketId(ptr, expiry))
		return false;

	// Clear ticket.
	success = stashRemoveInt(stConflictTickets, ptr, NULL);
	devassert(success);
	return true;
}

// Create a conflict ticket for an account.
//
// Conflict tickets are somewhat like account login tickets, except they are used to verify that a LoginFailureCode_UnlinkedPWCommonAccount event
// has occurred.  The verifying can use it to establish that the PW account name and password were in fact valid, and that, at the time of issuing the
// ticket, there was no Cryptic account associated with it.
//
// Currently, the list of active tickets is kept in a StashTable separate from the container storage, but this has been implemented so it could
// be rewritten to store the tickets directly on the PWCommonAccount structs.  Doing the former avoids increasing the memory usage of the
// PWCommonAccount struct, as we have a very large number of these.
//
// There is only ever one conflict ticket per PW stub, and they expire after CONFLICT_TICKET_LIFESPAN.  If a new ticket is created, or all old tickets
// are invalidated.  Tickets are checked for expiry when being checked, and old tickets are occasionally pruned to avoid leaking memory.  In a worst-
// case situation, where an adversary is attempting to maximize our resource usage, they can at most create one stash table entry per stub, so the
// total memory usage of this feature is probably bounded to under 20% of the total stub memory usage.  The most effective attack would be to exhaust
// CPU usage in SHA-256 computation, but this would be much less effective than other accountnet-based CPU attacks, so there's nothing to see here.
// In practice, even under extreme load situations, SHA-256 calculation (the most expensive part of conflict tickets) is unlikely to cause any
// serious performance degradation.  In the situation where conflict resolution tickets are not generated, no significant RAM or CPU should be used.
U32 createConflictTicket(SA_PARAM_NN_VALID PWCommonAccount *pAccount)
{
	U32 ticket;
	PERFINFO_AUTO_START_FUNC();
	ticket = createConflictTicket_Internal(pAccount);
	PERFINFO_AUTO_STOP();
	return ticket;
}

bool findConflictTicketByID(SA_PARAM_NN_VALID PWCommonAccount *pAccount, U32 uTicketID)
{
	bool bSuccess;
	PERFINFO_AUTO_START_FUNC();
	bSuccess = findConflictTicketByID_Internal(pAccount, uTicketID);
	PERFINFO_AUTO_STOP();
	return bSuccess;
}

// Cryptic conflict ticket - used when Cryptic accounts are fully disabled with DisallowCrypticAndPwLoginType
U32 createCrypticConflictTicket(SA_PARAM_NN_VALID AccountInfo *pAccount)
{
	U32 ticket;
	PERFINFO_AUTO_START_FUNC();
	ticket = createConflictTicket_Internal(pAccount);
	PERFINFO_AUTO_STOP();
	return ticket;
}

bool findCrypticConflictTicketByID(SA_PARAM_NN_VALID AccountInfo *pAccount, U32 uTicketID)
{
	bool bSuccess;
	PERFINFO_AUTO_START_FUNC();
	bSuccess = findConflictTicketByID_Internal(pAccount, uTicketID);
	PERFINFO_AUTO_STOP();
	return bSuccess;
}

/***************************************************************************/
/* XML-RPC Methods for Creating and Linking accounts with PWCommonAccounts */
/***************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCPWUpdateAccountRequest
{
	const char *pAccountName; AST(NAME(AccountName))
	const char *pForumName; AST(NAME(ForumName))
	const char *pEmail; AST(NAME(Email))
	const char *pPasswordHash; AST(NAME(PasswordHash))
	const char *pPasswordHashFixedSalt; AST(NAME(PasswordHashFixedSalt))
	const char *pFixedSalt; AST(NAME(FixedSalt))
	bool bBanned; AST(NAME(Banned))
} XMLRPCPWUpdateAccountRequest;

AUTO_STRUCT;
typedef struct XMLRPCPWUpdateAccountResponse
{
	const char *pResult; AST(NAME(Result) UNOWNED)
} XMLRPCPWUpdateAccountResponse;

static U32 sHourlyPWCommonUpdateCounts[PWUPDATE_Max] = {0};
int AccountIntegration_GetHourlyUsefulUpdateCount(void)
{
	return sHourlyPWCommonUpdateCounts[PWUPDATE_Updated] + sHourlyPWCommonUpdateCounts[PWUPDATE_Created];
}
int AccountIntegration_GetHourlyUselessUpdateCount(void)
{
	return sHourlyPWCommonUpdateCounts[PWUPDATE_Useless];
}
int AccountIntegration_GetHourlyFailedUpdateCount(void)
{
	return sHourlyPWCommonUpdateCounts[PWUPDATE_Error];
}
void AccountIntegration_ResetHourlyUpdateCounts(void)
{
	int i;
	for (i=0; i< PWUPDATE_Max; i++)
		sHourlyPWCommonUpdateCounts[i] = 0;
}

static bool XMLRPCPWUpdateAccountParamsGood(XMLRPCPWUpdateAccountRequest *pRequest)
{
	// Must have account name
	if (nullStr(pRequest->pAccountName)) return false;

	// Must have e-mail address
	if (nullStr(pRequest->pEmail)) return false;

	// Must have a fixed salt if we have a fixed salt hash
	if (!nullStr(pRequest->pPasswordHashFixedSalt) && nullStr(pRequest->pFixedSalt)) return false;

	// Must have one of the two password types
	if (nullStr(pRequest->pPasswordHash) && nullStr(pRequest->pPasswordHashFixedSalt)) return false;

	return true;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("PW::UpdateAccount");
XMLRPCPWUpdateAccountResponse * XMLRPCPWUpdateAccount(XMLRPCPWUpdateAccountRequest *pRequest)
{
	XMLRPCPWUpdateAccountResponse *pResponse = StructCreate(parse_XMLRPCPWUpdateAccountResponse);
	
	LogXmlrpcf("Request: PWUpdateAccount(%s, %s, %s, %d)",
		pRequest->pAccountName ? pRequest->pAccountName : "(null)",
		pRequest->pForumName ? pRequest->pForumName : "(null)",
		pRequest->pEmail ? pRequest->pEmail : "(null)",
		pRequest->bBanned);
	
	if (XMLRPCPWUpdateAccountParamsGood(pRequest))
	{
		PerfectWorldUpdateResult result;

		ANALYSIS_ASSUME(pRequest->pAccountName); devassert(pRequest->pAccountName);
		ANALYSIS_ASSUME(pRequest->pEmail); devassert(pRequest->pEmail);

		result = AccountIntegration_CreateOrUpdatePWCommon(pRequest->pAccountName,
			pRequest->pForumName, pRequest->pEmail, pRequest->pPasswordHash,
			pRequest->pPasswordHashFixedSalt, pRequest->pFixedSalt, pRequest->bBanned);

		if (result >= 0 && result < PWUPDATE_Max)
		{
			sHourlyPWCommonUpdateCounts[result]++;
		}

		switch (result)
		{
		case PWUPDATE_Updated:
		case PWUPDATE_Created: // Success cases
		case PWUPDATE_Useless:
			pResponse->pResult = ACCOUNT_HTTP_SUCCESS;
			break;
		case PWUPDATE_Error: // Failure cases (and default)
		default:
			pResponse->pResult = ACCOUNT_HTTP_NO_ARGS;
		}
	}
	else
	{
		pResponse->pResult = ACCOUNT_HTTP_NO_ARGS;
	}

	SERVLOG_PAIRS(LOG_ACCOUNT_SERVER_XMLRPC, "PWEUpdateAccount",
		("account", "%s", pRequest->pAccountName ? pRequest->pAccountName : "(null)")
		("forum", "%s", pRequest->pForumName ? pRequest->pForumName : "(null)")
		("email", "%s", pRequest->pEmail ? pRequest->pEmail : "(null)")
		("haspasswordhash", "%d", pRequest->pPasswordHash ? 1 : 0)
		("hasfixedsalt", "%d", pRequest->pFixedSalt ? 1 : 0)
		("hasfixedsalthash", "%d", pRequest->pPasswordHashFixedSalt ? 1 : 0)
		("banned", "%d", pRequest->bBanned)
		("result", "%s", pResponse->pResult));

	LogXmlrpcf("Response: PWUpdateAccount(): %s", pResponse->pResult);

	return pResponse;
}

AUTO_STRUCT;
typedef struct XMLRPCPWRequestAccountRequest
{
	// Looks up by AccountName if non-empty string, otherwise looks up by ForumName
	const char *pAccountName; AST(NAME(AccountName))
	const char *pForumName; AST(NAME(ForumName))
} XMLRPCPWRequestAccountRequest;

AUTO_STRUCT;
typedef struct XMLRPCPWRequestAccountResponse
{
	char *pAccountName; AST(NAME(AccountName))
	char *pForumName; AST(NAME(ForumName))
	char *pEmail; AST(NAME(Email))
	char *pResult; AST(NAME(Result) UNOWNED)
} XMLRPCPWRequestAccountResponse;

// Does not return the password hash
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("PW::RequestAccount");
XMLRPCPWRequestAccountResponse * XMLRPCPWRequestAccount(XMLRPCPWRequestAccountRequest *pRequest)
{
	XMLRPCPWRequestAccountResponse *pResponse = StructCreate(parse_XMLRPCPWRequestAccountResponse);
	PWCommonAccount *pAccount = NULL;
	
	LogXmlrpcf("Request: PWRequestAccount(%s)", pRequest->pAccountName);
	if (!nullStr(pRequest->pAccountName))
		pAccount = findPWCommonAccountbyName(pRequest->pAccountName);
	else if (!nullStr(pRequest->pForumName))
		pAccount = findPWCommonAccountbyForumName(pRequest->pForumName);

	if (pAccount)
	{
		pResponse->pAccountName = StructAllocString(pAccount->pAccountName);
		pResponse->pForumName = StructAllocString(pAccount->pForumName);
		pResponse->pEmail = StructAllocString(pAccount->pEmail);
		pResponse->pResult = ACCOUNT_HTTP_SUCCESS;
	}
	else
		pResponse->pResult = ACCOUNT_HTTP_PWUSER_UNKNOWN;
	LogXmlrpcf("Response: PWRequestAccount(): %s", NULL_TO_EMPTY(pResponse->pResult));
	return pResponse;
}


/***************************************************************************/
/* XML-RPC Methods for transferring virtual currency                       */
/***************************************************************************/

AUTO_STRUCT;
typedef struct XMLRPCPWTransferCurrencyRequest
{
	const char *pAccountName; AST(NAME(AccountName))
	const char *pCurrency; AST(NAME(Currency))
	U32 uAmount; AST(NAME(Amount))
} XMLRPCPWTransferCurrencyRequest;

AUTO_STRUCT;
typedef struct XMLRPCPWTransferCurrencyResponse
{
	const char *pResult; AST(NAME(Result) UNOWNED)
} XMLRPCPWTransferCurrencyResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9) ACMD_NAME("PW::TransferCurrency");
XMLRPCPWTransferCurrencyResponse * XMLRPCPWTransferCurrency(XMLRPCPWTransferCurrencyRequest *pRequest)
{
	XMLRPCPWTransferCurrencyResponse *pResponse = StructCreate(parse_XMLRPCPWTransferCurrencyResponse);
	PWCommonAccount * pPWAccount = NULL;
	AccountInfo * pAccount = NULL;
	AccountKeyValueResult eResult = AKV_FAILURE;
	STRING_EARRAY eaLocks = NULL;
	TransactionLogKeyValueChange **eaChanges = NULL;
	STRING_EARRAY eaCurrencies = NULL;
	U32 uLogID = 0;

	LogXmlrpcf("Request: PWTransferCurrency(%s, %s, %u)", pRequest->pAccountName, pRequest->pCurrency, pRequest->uAmount);

	if (!pRequest->pCurrency || !billingMapPWCurrency(pRequest->pCurrency, &eaCurrencies))
	{
		pResponse->pResult = ACCOUNT_HTTP_INVALID_CURRENCY;
		goto done;
	}

	pPWAccount = findPWCommonAccountbyName(pRequest->pAccountName);
	if (!pPWAccount)
	{
		pResponse->pResult = ACCOUNT_HTTP_PWUSER_UNKNOWN;
		goto done;
	}

	if (!pPWAccount->uLinkedID)
	{
		pResponse->pResult = ACCOUNT_HTTP_CRYPTICUSER_NOTLINKED;
		goto done;
	}

	pAccount = findAccountByID(pPWAccount->uLinkedID);
	devassertmsgf(pAccount, "PW user %s has an invalid account link", pRequest->pAccountName);
	if (!pAccount)
	{
		pResponse->pResult = ACCOUNT_HTTP_USER_UNKNOWN_ERROR;
		goto done;
	}

	accountLog(pAccount, "Key value change reason: ZEN transfer");

	// Lock the key-values
	EARRAY_CONST_FOREACH_BEGIN(eaCurrencies, iCurCurrency, iNumCurrencies);
	{
		char * pLock = NULL;

		eResult = AccountKeyValue_Change(pAccount, eaCurrencies[iCurCurrency], pRequest->uAmount, &pLock);
		if (eResult != AKV_SUCCESS)
		{
			switch (eResult)
			{
			default:
			case AKV_FAILURE:
				pResponse->pResult = ACCOUNT_HTTP_KEY_FAILURE;
				break;
			case AKV_INVALID_KEY:
				pResponse->pResult = ACCOUNT_HTTP_INVALID_CURRENCY;
				break;
			case AKV_INVALID_RANGE:
				pResponse->pResult = ACCOUNT_HTTP_INVALID_RANGE;
				break;
			}
			goto done;
		}

		eaPush(&eaLocks, pLock);
	}
	EARRAY_FOREACH_END;

	assert(eaSize(&eaCurrencies) == eaSize(&eaLocks));

	// Commit the key-values
	EARRAY_FOREACH_REVERSE_BEGIN(eaCurrencies, iCurCurrency);
	{
		eResult = AccountKeyValue_Commit(pAccount, eaCurrencies[iCurCurrency], eaLocks[iCurCurrency]);
		devassert(eResult == AKV_SUCCESS);
		estrDestroy(&eaLocks[iCurCurrency]);
		eaRemoveFast(&eaLocks, iCurCurrency);
	}
	EARRAY_FOREACH_END;

	assert(!eaSize(&eaLocks));

	AccountTransactionGetKeyValueChanges(pAccount, eaCurrencies, &eaChanges);

	uLogID = AccountTransactionOpen(pAccount->uID, TransLogType_CashPurchase, NULL, TPROVIDER_PerfectWorld, NULL, NULL);
	AccountTransactionRecordKeyValueChanges(pAccount->uID, uLogID, eaChanges, "Zen transfer");
	AccountTransactionFinish(pAccount->uID, uLogID);

	accountSetBilled(pAccount);
	pResponse->pResult = ACCOUNT_HTTP_SUCCESS;

done:
	if (eaLocks)
	{
		// Rollback outstanding key-values
		assert(eaSize(&eaLocks) <= eaSize(&eaCurrencies));

		EARRAY_FOREACH_REVERSE_BEGIN(eaLocks, iCurLock);
		{
			eResult = AccountKeyValue_Rollback(pAccount, eaCurrencies[iCurLock], eaLocks[iCurLock]);
			devassert(eResult == AKV_SUCCESS);
			estrDestroy(&eaLocks[iCurLock]);
			eaRemoveFast(&eaLocks, iCurLock);
		}
		EARRAY_FOREACH_END;

		eaDestroy(&eaLocks);
	}

	if (eaCurrencies)
	{
		eaDestroy(&eaCurrencies); // we don't own the member pointers
	}

	SERVLOG_PAIRS(LOG_ACCOUNT_SERVER_XMLRPC, "PWETransferCurrency",
		("account", "%s", pRequest->pAccountName)
		("currency", "%s", pRequest->pCurrency)
		("amount", "%d", pRequest->uAmount)
		("result", "%s", pResponse->pResult));
	AccountTransactionFreeKeyValueChanges(&eaChanges);
	devassert(pResponse->pResult);
	LogXmlrpcf("Response: PWTransferCurrency(): %s", pResponse->pResult);
	return pResponse;
}

// File must be a CSV file with fields "<Account Name>, <Forum Name>, <Email>, <Password Hash>"
AUTO_COMMAND ACMD_CATEGORY(Account_Server) ACMD_NAME("ImportPerfectWorldAccounts");
int AccountIntegration_ImportPerfectWorldAccounts(const char *filename)
{
	char **eaFields = NULL;
	FileWrapper *file = fopen(filename, "r");
	int iCount = 0;
	int size;

	if (!file)
		return 0;
	while (csvReadLine(file, &eaFields, 1024))
	{
		size = eaSize(&eaFields);
		if (size >= 4)
		{
			int i;
			for (i=0; i<4; i++)
			{
				estrTrimLeadingAndTrailingWhitespace(&eaFields[i]);
			}
			if (size > 4 && eaFields[4])
			{
				bool bBanned = atoi(eaFields[4]);
				AccountIntegration_CreateOrUpdatePWCommon(eaFields[0], eaFields[1], eaFields[2], eaFields[3], NULL, NULL, bBanned);
			}
			else
				AccountIntegration_CreateOrUpdatePWCommon(eaFields[0], eaFields[1], eaFields[2], eaFields[3], NULL, NULL, false);
		}
		iCount++;
		eaClearEString(&eaFields);
	}
	fclose(file);
	eaDestroyEString(&eaFields);
	return iCount; // number of lines read
}

void AccountIntegration_PWCommonAccountDecryptPassword(SA_PARAM_NN_VALID PWCommonAccount *pAccount)
{
	SAFE_FREE(pAccount->pPasswordHash_RAM);
	SAFE_FREE(pAccount->pPasswordHashFixedSalt_RAM);

	if (pAccount->pPasswordHash_Obsolete)
	{
		pAccount->pPasswordHash_RAM = strdup(pAccount->pPasswordHash_Obsolete);
	}
	else
	{
		pAccount->pPasswordHash_RAM = AccountEncryption_DecodeDecryptAndAllocString(pAccount->pPasswordHash_Disk);
		pAccount->pPasswordHashFixedSalt_RAM = AccountEncryption_DecodeDecryptAndAllocString(pAccount->pPasswordHashFixedSalt_Disk);
	}
}



bool AccountIntegration_PWCommonAccountNeedsEncryptionFixup(SA_PARAM_NN_VALID PWCommonAccount *pAccount)
{
	if (pAccount->iEncryptionVersion != AccountEncryption_GetCurEncryptionKeyIndex())
	{
		return true;
	}

	if (pAccount->pPasswordHash_Obsolete)
	{
		return true;
	}

	return false;
}

AUTO_TRANSACTION
ATR_LOCKS(pwBatch, ".Eaaccounts");
enumTransactionOutcome trFixupPWAccountBatch(ATR_ARGS, NOCONST(PerfectWorldAccountBatch) *pwBatch)
{
	FOR_EACH_IN_EARRAY(pwBatch->eaAccounts, NOCONST(PWCommonAccount), pAccount)
	{
		if (pAccount->pPasswordHash_Obsolete)
		{
			SAFE_FREE(pAccount->pPasswordHash_RAM);
			pAccount->pPasswordHash_RAM = pAccount->pPasswordHash_Obsolete;
			pAccount->pPasswordHash_Obsolete = NULL;
		}

		if (!pAccount->pPasswordHash_RAM)
		{
			if (!pAccount->pPasswordHash_Disk)
			{
				TRANSACTION_RETURN_FAILURE("No password at all");
			}

			if (!AccountEncryption_FindKeyFromIndex(pAccount->iEncryptionVersion))
			{
				TRANSACTION_RETURN_FAILURE("Encryption key %d could not be found", pAccount->iEncryptionVersion);
			}

			pAccount->pPasswordHash_RAM = AccountEncryption_DecodeDecryptAndAllocStringWithKeyVersion(pAccount->pPasswordHash_Disk, pAccount->iEncryptionVersion);
			if (!pAccount->pPasswordHash_RAM)
			{
				TRANSACTION_RETURN_FAILURE("Unable to decode disk PW");
			}
		}

		SAFE_FREE(pAccount->pPasswordHash_Disk);
		pAccount->iEncryptionVersion = AccountEncryption_GetCurEncryptionKeyIndex();

		pAccount->pPasswordHash_Disk = AccountEncryption_EncryptEncodeAndAllocString(pAccount->pPasswordHash_RAM);
		if (!pAccount->pPasswordHash_Disk)
		{
			TRANSACTION_RETURN_FAILURE("Unable to encode RAM pw");
		}
	}
	FOR_EACH_END;

	return TRANSACTION_OUTCOME_SUCCESS;
}

bool AccountIntegration_PWCommonAccountDoEncryptionFixup(SA_PARAM_NN_VALID PWCommonAccount *pAccount)
{
	TransactionReturnVal result = {0};
	
	bool bRetVal;

	if (!AccountIntegration_PWCommonAccountNeedsEncryptionFixup(pAccount))
	{
		return true;
	}

	AutoTrans_trFixupPWAccountBatch(&result, objServerType(), GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT, pAccount->uBatchID);

	bRetVal = (result.eOutcome == TRANSACTION_OUTCOME_SUCCESS);

	if (!bRetVal)
	{
		CRITICAL_NETOPS_ALERT("PW_ACCT_FIXUP_FAIL", "While doing encryption fixup on a PerfectWorld account, failed because: %s",
			GetTransactionFailureString(&result));
	}

	ReleaseReturnValData(objLocalManager(), &result);

	return bRetVal;
}

PerfectWorldAccountBatch *PerfectWorldAccountBatch_FindByID(U32 iID)
{
	Container *con = objGetContainer(GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT, iID);
	if (con)
	{
		return  (PerfectWorldAccountBatch*)(con->containerData);
	}

	return NULL;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Account_Server);
int TranslatePWAccountNameToBatchID(const char *pwAccountName)
{
	if (!nullStr(pwAccountName))
	{
		PWCommonAccount *pwAccount = findPWCommonAccountbyName(pwAccountName);
		if (pwAccount)
			return pwAccount->uBatchID;
	}
	return 0;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Account_Server);
int TranslatePWForumNameToBatchID(const char *pwForumName)
{
	if (!nullStr(pwForumName))
	{
		PWCommonAccount *pwAccount = findPWCommonAccountbyForumName(pwForumName);
		if (pwAccount)
			return pwAccount->uBatchID;
	}
	return 0;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Account_Debug);
void CreateTestPWAccount(const char *pwAccountName, const char *forumName)
{
	char *email = NULL;
	char pwBuffer[MAX_PASSWORD];
	PerfectWorldUpdateResult ignore;
	estrPrintf(&email, "%s@donotsent.crypticstudios.com", pwAccountName);
	accountPWHashPassword(pwAccountName, pwAccountName, pwBuffer);
	ignore = AccountIntegration_CreateOrUpdatePWCommon(pwAccountName, forumName, email, pwBuffer, NULL, NULL, false);
	estrDestroy(&email);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Account_Debug);
void CreateLinkedCrypticAccount(const char *pwAccountName)
{
	AccountCreationResult ignore = ACCOUNTCREATE_Success;
	PWCommonAccount *pAccount = findPWCommonAccountbyName(pwAccountName);
	if (pAccount)
		AccountIntegration_CreateAccountFromPWCommon(pAccount, NULL, NULL, &ignore);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Account_Debug);
void EmailChangePWAccount(const char *pwAccountName, const char *email)
{
	PWCommonAccount *pAccount = findPWCommonAccountbyName(pwAccountName);
	AccountCreationResult ignore;
	if (!pAccount)
		return;
	ignore = AccountIntegration_CreateOrUpdatePWCommon(pwAccountName, pAccount->pForumName, email, NULL, NULL, NULL, false);
}

#include "AutoGen/AccountIntegration_h_ast.c"
#include "AutoGen/AccountIntegration_c_ast.c"
