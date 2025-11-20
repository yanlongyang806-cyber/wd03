#include "MachineID.h"
#include "AutoGen/MachineID_h_ast.h"
#include "AutoGen/MachineID_c_ast.h"
#include "AutoGen/AccountServer_autotransactions_autogen_wrappers.h"

#include "AccountLog.h"
#include "AccountManagement.h"
#include "AccountServer.h"
#include "AccountServerConfig.h"
#include "crypt.h"
#include "error.h"
#include "httpAsync.h"
#include "JSONRPC.h"
#include "logging.h"
#include "netipfilter.h"
#include "objTransactions.h"
#include "ResourceInfo.h"
#include "StashTable.h"
#include "StringUtil.h"
#include "timing_profiler.h"
#include "url.h"
#include "websrv.h"

#include "AutoGen/websrv_h_ast.h"

extern bool gbMachineLockEnableDefault;
extern bool gbMachineLockDisable;

#define MACHINEID_MAX_LENGTH (100)
#define MACHINENAME_MAX_LENGTH (200) // Truncate down to this

bool machineIDIsValid(const char *pMachineID)
{
	const char *cur = pMachineID;
	int count = 0;
	if (!pMachineID)
		return true;
	while (*cur != 0)
	{
		if (!isalnum(*cur))
			return false;
		cur++;
		count++;
		if (count > MACHINEID_MAX_LENGTH)
			return false;
	}
	return true;
}

bool machineLockingIsIPWhitelisted(U32 uIp)
{
	return ipfIsIpInGroup(MACHINE_LOCKING_IPFILTER_GROUP, uIp);
}

///////////////////////////////////////////////////////////
// Emails

#define ONETIMECODE_EMAIL_EVENT "onetimecode"
#define SAVEDMACHINE_EMAIL_EVENT "machinesaved"
#define UNSAVEDMACHINE_EMAIL_EVENT "machinenotsaved"

static void WebSrvOneTimeCode_CB(struct JSONRPCState *state, void * userData)
{
	if (state->error)
	{
		ErrorOrCriticalAlert("ACCOUNTSERVER_WEBSRV", "JSONRPC to WebSrv failed: %s.", state->error);
	}
}

static void accountMachineEmailOneTimeCode(AccountInfo *account, const char *pOneTimeCode, const char *ip)
{
	const AccountServerConfig *config = GetAccountServerConfig();
	const char *email = accountGetEmail(account);
	if (nullStr(email))
		return;
	if (!nullStr(config->pWebSrvAddress) && config->iWebSrvPort)
	{
		WebSrvKeyValueList keyList = {0};

		websrvKVList_Add(&keyList, "account", account->displayName);
		websrvKVList_Add(&keyList, "onetimecode", pOneTimeCode);
		websrvKVList_Add(&keyList, "ip", ip);
		accountSendEventEmail(account, NULL, "AccountGuardEmail", &keyList, WebSrvOneTimeCode_CB, NULL);
		StructDeInit(parse_WebSrvKeyValueList, &keyList);
	}
	else
	{
		UrlArgumentList *args;
		if (nullStr(config->pOneTimeCodeEmailURL) || nullStr(email))
			return;
		args = urlToUrlArgumentList(config->pOneTimeCodeEmailURL);

		urlAddValue(args, "trigger", ONETIMECODE_EMAIL_EVENT, HTTPMETHOD_GET);
		urlAddValue(args, "email", email, HTTPMETHOD_GET);
		urlAddValue(args, "replace_vars[account]", account->displayName, HTTPMETHOD_GET);
		urlAddValue(args, "replace_vars[onetimecode]", pOneTimeCode, HTTPMETHOD_GET);
		urlAddValue(args, "replace_vars[ip]", ip, HTTPMETHOD_GET);
		urlAddValue(args, "lang", account->defaultLocale ? account->defaultLocale : "EN", HTTPMETHOD_GET);

		haRequest(NULL, &args, NULL, NULL, 0, NULL);
	}
}

static void accountMachineEmailOneTimeCodeUsed(AccountInfo *account, const char *pMachineName, const char *ip, MachineType eType)
{
	const AccountServerConfig *config = GetAccountServerConfig();
	const char *email = accountGetEmail(account);
	bool bSaved = !nullStr(pMachineName);
	if (nullStr(email))
		return;	
	if (!nullStr(config->pWebSrvAddress) && config->iWebSrvPort)
	{
		WebSrvKeyValueList keyList = {0};

		websrvKVList_Add(&keyList, "account", account->displayName);
		websrvKVList_Add(&keyList, "ip", ip);
		if (bSaved)
			websrvKVList_Add(&keyList, "machine", pMachineName);
		websrvKVList_Add(&keyList, "type", StaticDefineIntRevLookup(MachineTypeEnum, eType));

		accountSendEventEmail(account, NULL, bSaved ? "AccountGuardMachineSaved" : "AccountGuardMachineNotSaved", &keyList, WebSrvOneTimeCode_CB, NULL);
		StructDeInit(parse_WebSrvKeyValueList, &keyList);
	}
	else
	{
		UrlArgumentList *args;
		if (nullStr(config->pOneTimeCodeEmailURL) || nullStr(email))
			return;
		args = urlToUrlArgumentList(config->pOneTimeCodeEmailURL);

		urlAddValue(args, "trigger", bSaved ? SAVEDMACHINE_EMAIL_EVENT : UNSAVEDMACHINE_EMAIL_EVENT, HTTPMETHOD_GET);
		urlAddValue(args, "email", email, HTTPMETHOD_GET);
		urlAddValue(args, "replace_vars[account]", account->displayName, HTTPMETHOD_GET);
		if (bSaved)
			urlAddValue(args, "replace_vars[machine]", pMachineName, HTTPMETHOD_GET);
		urlAddValue(args, "replace_vars[ip]", ip, HTTPMETHOD_GET);
		urlAddValue(args, "replace_vars[type]", StaticDefineIntRevLookup(MachineTypeEnum, eType), HTTPMETHOD_GET);
		urlAddValue(args, "lang", account->defaultLocale ? account->defaultLocale : "EN", HTTPMETHOD_GET);

		haRequest(NULL, &args, NULL, NULL, 0, NULL);
	}
}

///////////////////////////////////////////////////////////
// Machine Locking and Saving

SavedMachine *accountMachineFindByID(AccountInfo *account, const char *pMachineID, MachineType eType)
{
	if (!account || nullStr(pMachineID))
		return NULL;
	if (eType == MachineType_CrypticClient)
		return eaIndexedGetUsingString(&account->eaSavedClients, pMachineID);
	else if (eType == MachineType_WebBrowser)
		return eaIndexedGetUsingString(&account->eaSavedBrowsers, pMachineID);
	return NULL;
}

bool accountMachineLockingIsEnabled(AccountInfo *account)
{
	if (gbMachineLockDisable)
		return false;
	if (account->eMachineLockState == AMLS_Unknown)
		return gbMachineLockEnableDefault;
	if (account->eMachineLockState == AMLS_Enabled)
		return true;
	return false;
}

static bool inMachineGracePeriod(AccountInfo *account, U32 uCurTime)
{
	U32 uGracePeriod = GetAccountServerConfig()->uMachineLockGracePeriod;
	if (uGracePeriod)
	{
		U32 uTime = account->uCreatedTime;
		if (uTime && uCurTime < uTime + uGracePeriod * SECONDS_PER_DAY)
			return true;
	}
	return false;
}

bool accountMachineSaveNextClient(AccountInfo *account, U32 time)
{
	if ((account->flags & ACCOUNT_FLAG_AUTOSAVE_MACHINEID_CLIENT) != 0 && time < account->uSaveNextMachineExpire)
		return true;
	if (eaSize(&account->eaSavedClients) == 0 && 
		(account->flags & ACCOUNT_FLAG_AUTOSAVE_CLIENT_LOGIN) == 0 && 
		inMachineGracePeriod(account, time))
		return true;
	return false;
}

bool accountMachineSaveNextBrowser(AccountInfo *account, U32 time)
{
	if ((account->flags & ACCOUNT_FLAG_AUTOSAVE_MACHINEID_BROWSER) != 0 && time < account->uSaveNextMachineExpire)
		return true;
	if (eaSize(&account->eaSavedBrowsers) == 0 && 
		(account->flags & ACCOUNT_FLAG_AUTOSAVE_BROWSER_LOGIN) == 0 && 
		inMachineGracePeriod(account, time))
		return true;
	return false;
}

bool accountMachineSaveNextMachineByType(AccountInfo *account, U32 time, MachineType eType)
{
	if (eType == MachineType_CrypticClient)
		return accountMachineSaveNextClient(account, time);
	if (eType == MachineType_WebBrowser)
		return accountMachineSaveNextBrowser(account, time);
	return false;
}

// Not actually an AUTO_TRANS_HELPER
static void addSavedMachine(EARRAY_OF(NOCONST(SavedMachine)) *eaSavedMachines, const char *pMachineID, const char *pMachineName, const char *ip, U32 uMax)
{
	NOCONST(SavedMachine) *pMachine;
	while ((U32) eaSize(eaSavedMachines) >= uMax)
	{
		U32 uOldestIdx = 0;
		U32 uOldestTime = 0;
		EARRAY_FOREACH_BEGIN(*eaSavedMachines, i);
		{
			if (!uOldestTime || (*eaSavedMachines)[i]->uLastSeenTime < uOldestTime)
			{
				uOldestIdx = i;
				uOldestTime = (*eaSavedMachines)[i]->uLastSeenTime;
			}
		}
		EARRAY_FOREACH_END;
		eaRemove(eaSavedMachines, uOldestIdx);
	}
	pMachine = StructCreateNoConst(parse_SavedMachine);
	pMachine->pMachineID = StructAllocString(pMachineID);
	if (strlen(pMachineName) <= MACHINENAME_MAX_LENGTH)
		pMachine->pMachineName = StructAllocString(pMachineName);
	else
	{
		pMachine->pMachineName = malloc(MACHINENAME_MAX_LENGTH+1);
		strncpy_s(pMachine->pMachineName, MACHINENAME_MAX_LENGTH+1, pMachineName, MACHINENAME_MAX_LENGTH);
		pMachine->pMachineName[MACHINENAME_MAX_LENGTH] = 0;
	}
	estrCopy2(&pMachine->ip, ip);
	pMachine->uLastSeenTime = timeSecondsSince2000();
	if (!*eaSavedMachines)
		eaIndexedEnableNoConst(eaSavedMachines, parse_SavedMachine);
	eaIndexedAdd(eaSavedMachines, pMachine);
}

// Client Transactions
AUTO_TRANSACTION ATR_LOCKS(account, ".eaSavedClients");
enumTransactionOutcome trAddSavedClient(ATR_ARGS, NOCONST(AccountInfo) *account, const char *pMachineID, const char *pMachineName, const char *ip)
{
	if (eaIndexedGetUsingString(&account->eaSavedClients, pMachineID) != NULL)
		return TRANSACTION_OUTCOME_FAILURE;
	addSavedMachine(&account->eaSavedClients, pMachineID, pMachineName, ip, GetAccountServerConfig()->uSavedClientMachinesMax);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(account, ".eaSavedClients[]");
enumTransactionOutcome trRenameSavedClient(ATR_ARGS, NOCONST(AccountInfo) *account, const char *pMachineID, const char *pMachineName)
{
	NOCONST(SavedMachine) *pMachine = eaIndexedGetUsingString(&account->eaSavedClients, pMachineID);
	if (!pMachine)
		return TRANSACTION_OUTCOME_FAILURE;
	SAFE_FREE(pMachine->pMachineName);
	if (strlen(pMachineName) <= MACHINENAME_MAX_LENGTH)
		pMachine->pMachineName = StructAllocString(pMachineName);
	else
	{
		pMachine->pMachineName = malloc(MACHINENAME_MAX_LENGTH+1);
		strncpy_s(pMachine->pMachineName, MACHINENAME_MAX_LENGTH+1, pMachineName, MACHINENAME_MAX_LENGTH);
		pMachine->pMachineName[MACHINENAME_MAX_LENGTH] = 0;
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(account, ".eaSavedClients[]");
enumTransactionOutcome trUpdateSavedClient(ATR_ARGS, NOCONST(AccountInfo) *account, const char *pMachineID, const char *ip)
{
	NOCONST(SavedMachine) *pMachine = eaIndexedGetUsingString(&account->eaSavedClients, pMachineID);
	if (!pMachine)
		return TRANSACTION_OUTCOME_FAILURE;
	pMachine->uLastSeenTime = timeSecondsSince2000();
	if (ip && *ip)
		estrCopy2(&pMachine->ip, ip);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(account, ".eaSavedClients");
enumTransactionOutcome trRemoveSavedClient(ATR_ARGS, NOCONST(AccountInfo) *account, const char *pMachineID)
{
	int idx = eaIndexedFindUsingString(&account->eaSavedClients, pMachineID);
	NOCONST(SavedMachine) *pMachine;
	if (idx == -1)
		return TRANSACTION_OUTCOME_FAILURE;
	pMachine = eaRemove(&account->eaSavedClients, idx);
	StructDestroyNoConst(parse_SavedMachine, pMachine);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(account, ".eaSavedClients");
enumTransactionOutcome trClearSavedClients(ATR_ARGS, NOCONST(AccountInfo) *account)
{
	eaDestroyStructNoConst(&account->eaSavedClients, parse_SavedMachine);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Browser Transactions
AUTO_TRANSACTION ATR_LOCKS(account, ".eaSavedBrowsers");
enumTransactionOutcome trAddSavedBrowser(ATR_ARGS, NOCONST(AccountInfo) *account, const char *pMachineID, const char *pMachineName, const char *ip)
{
	if (eaIndexedGetUsingString(&account->eaSavedBrowsers, pMachineID) != NULL)
		return TRANSACTION_OUTCOME_FAILURE;
	addSavedMachine(&account->eaSavedBrowsers, pMachineID, pMachineName, ip, GetAccountServerConfig()->uSavedBrowserMachinesMax);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(account, ".eaSavedBrowsers[]");
enumTransactionOutcome trRenameSavedBrowser(ATR_ARGS, NOCONST(AccountInfo) *account, const char *pMachineID, const char *pMachineName)
{
	NOCONST(SavedMachine) *pMachine = eaIndexedGetUsingString(&account->eaSavedBrowsers, pMachineID);
	if (!pMachine)
		return TRANSACTION_OUTCOME_FAILURE;
	SAFE_FREE(pMachine->pMachineName);
	pMachine->pMachineName = StructAllocString(pMachineName);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(account, ".eaSavedBrowsers[]");
enumTransactionOutcome trUpdateSavedBrowser(ATR_ARGS, NOCONST(AccountInfo) *account, const char *pMachineID, const char *ip)
{
	NOCONST(SavedMachine) *pMachine = eaIndexedGetUsingString(&account->eaSavedBrowsers, pMachineID);
	if (!pMachine)
		return TRANSACTION_OUTCOME_FAILURE;
	pMachine->uLastSeenTime = timeSecondsSince2000();
	if (ip && *ip)
		estrCopy2(&pMachine->ip, ip);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(account, ".eaSavedBrowsers");
enumTransactionOutcome trRemoveSavedBrowser(ATR_ARGS, NOCONST(AccountInfo) *account, const char *pMachineID)
{
	int idx = eaIndexedFindUsingString(&account->eaSavedBrowsers, pMachineID);
	NOCONST(SavedMachine) *pMachine;
	if (idx == -1)
		return TRANSACTION_OUTCOME_FAILURE;
	pMachine = eaRemove(&account->eaSavedBrowsers, idx);
	StructDestroyNoConst(parse_SavedMachine, pMachine);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(account, ".eaSavedBrowsers");
enumTransactionOutcome trClearSavedBrowsers(ATR_ARGS, NOCONST(AccountInfo) *account)
{
	eaDestroyStructNoConst(&account->eaSavedBrowsers, parse_SavedMachine);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Other Transactions
AUTO_TRANSACTION ATR_LOCKS(account, ".eaSavedClients, .eaSavedBrowsers");
enumTransactionOutcome trClearAllSavedMachines(ATR_ARGS, NOCONST(AccountInfo) *account)
{
	eaDestroyStructNoConst(&account->eaSavedClients, parse_SavedMachine);
	eaDestroyStructNoConst(&account->eaSavedBrowsers, parse_SavedMachine);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(account, ".eMachineLockState, .eaSavedClients, .eaSavedBrowsers");
enumTransactionOutcome trMachineLockingEnable(ATR_ARGS, NOCONST(AccountInfo) *account, int iEnable)
{
	account->eMachineLockState = iEnable ? AMLS_Enabled : AMLS_Disabled;
	if (account->eMachineLockState == AMLS_Disabled)
	{
		eaDestroyStructNoConst(&account->eaSavedClients, parse_SavedMachine);
		eaDestroyStructNoConst(&account->eaSavedBrowsers, parse_SavedMachine);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(account, ".flags, .uSaveNextMachineExpire");
enumTransactionOutcome trMachineLockingSaveNext(ATR_ARGS, NOCONST(AccountInfo) *account, int iEnable, int eType, int iProcessingAutosave)
{
	int flags = 0;
	if (eType == MachineType_CrypticClient)
		flags = ACCOUNT_FLAG_AUTOSAVE_MACHINEID_CLIENT;
	else if (eType == MachineType_WebBrowser)
		flags = ACCOUNT_FLAG_AUTOSAVE_MACHINEID_BROWSER;
	else
		flags = (ACCOUNT_FLAG_AUTOSAVE_MACHINEID_CLIENT | ACCOUNT_FLAG_AUTOSAVE_MACHINEID_BROWSER);
	if (iEnable)
	{
		trhAccountSetFlags(account, flags);
		account->uSaveNextMachineExpire = timeSecondsSince2000() + GetAccountServerConfig()->uSaveNextMachineDuration * SECONDS_PER_DAY;
	}
	else
	{
		trhAccountClearFlags(account, flags);
		if ((account->flags & (ACCOUNT_FLAG_AUTOSAVE_MACHINEID_CLIENT | ACCOUNT_FLAG_AUTOSAVE_MACHINEID_BROWSER)) == 0)
			account->uSaveNextMachineExpire = 0;
		if (iProcessingAutosave)
		{
			if (eType == MachineType_CrypticClient)
				trhAccountSetFlags(account, ACCOUNT_FLAG_AUTOSAVE_CLIENT_LOGIN);
			else
				trhAccountSetFlags(account, ACCOUNT_FLAG_AUTOSAVE_BROWSER_LOGIN);
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Transaction Entry points
bool accountIsMachineIDSaved(AccountInfo *account, const char *pMachineID, MachineType eType, const char *ip)
{
	SavedMachine *pMachine = accountMachineFindByID(account, pMachineID, eType);
	if (!pMachine || (eType != MachineType_CrypticClient && eType != MachineType_WebBrowser))
		return false;
	if (ip)
	{
		if (eType == MachineType_CrypticClient)
			AutoTrans_trUpdateSavedClient(NULL, GetAppGlobalType(), GLOBALTYPE_ACCOUNT, account->uID, pMachineID, ip);
		else if (eType == MachineType_WebBrowser)
			AutoTrans_trUpdateSavedBrowser(NULL, GetAppGlobalType(), GLOBALTYPE_ACCOUNT, account->uID, pMachineID, ip);
	}
	return true;
}

void accountAddSavedMachine(AccountInfo *account, const char *pMachineID, MachineType eType, const char *pMachineName, const char *ip)
{
	if (!account || nullStr(pMachineID))
		return;
	if (!accountIsMachineIDSaved(account, pMachineID, eType, ip))
	{
		if (eType == MachineType_CrypticClient)
		{
			AutoTrans_trAddSavedClient(NULL, GetAppGlobalType(), GLOBALTYPE_ACCOUNT, account->uID, pMachineID, pMachineName, ip);
			accountLog(account, "Added Saved Cryptic Client: [machine:%s].", pMachineName);
		}
		else if (eType == MachineType_WebBrowser)
		{
			AutoTrans_trAddSavedBrowser(NULL, GetAppGlobalType(), GLOBALTYPE_ACCOUNT, account->uID, pMachineID, pMachineName, ip);
			accountLog(account, "Added Saved Web Browser: [machine:%s].", pMachineName);
		}
		// TODO(Theo) logging of stuff auto-removed
		accountMachineEmailOneTimeCodeUsed(account, pMachineName, ip, eType);
	}
	if (eType == MachineType_CrypticClient && account->flags & ACCOUNT_FLAG_AUTOSAVE_MACHINEID_CLIENT)
		accountMachineLockingSaveNext(account, false, MachineType_CrypticClient, true);
	else if (eType == MachineType_WebBrowser && account->flags & ACCOUNT_FLAG_AUTOSAVE_MACHINEID_BROWSER)
		accountMachineLockingSaveNext(account, false, MachineType_WebBrowser, true);
	accountMachineRemoveOneTimeCode(account, pMachineID);
}

void accountRenameSavedMachine(AccountInfo *account, const char *pMachineID, MachineType eType, const char *pMachineName)
{
	SavedMachine *pMachine = accountMachineFindByID(account, pMachineID, eType);
	if (!pMachine)
		return;
	if (eType == MachineType_CrypticClient)
	{
		accountLog(account, "Renaming Saved Cryptic Client [machine:%s] to [machine:%s].", pMachine->pMachineName, pMachineName);
		AutoTrans_trRenameSavedClient(NULL, GetAppGlobalType(), GLOBALTYPE_ACCOUNT, account->uID, pMachineID, pMachineName);
	}
	else if (eType == MachineType_WebBrowser)
	{
		accountLog(account, "Renaming Saved Web Browser [machine:%s] to [machine:%s].", pMachine->pMachineName, pMachineName);
		AutoTrans_trRenameSavedBrowser(NULL, GetAppGlobalType(), GLOBALTYPE_ACCOUNT, account->uID, pMachineID, pMachineName);
	}
	else
		devassert(0);
}

void accountRemoveSavedMachine(AccountInfo *account, const char *pMachineID, MachineType eType)
{
	SavedMachine *pMachine;
	if (!account)
		return;
	pMachine = accountMachineFindByID(account, pMachineID, eType);
	if (!pMachine)
		return;
	if (eType == MachineType_CrypticClient)
	{
		accountLog(account, "Removing Saved Cryptic Client: [machine:%s].", pMachine->pMachineName);
		AutoTrans_trRemoveSavedClient(NULL, GetAppGlobalType(), GLOBALTYPE_ACCOUNT, account->uID, pMachineID);
	}
	else if (eType == MachineType_WebBrowser)
	{
		accountLog(account, "Removing Saved Web Browser: [machine:%s].", pMachine->pMachineName);
		AutoTrans_trRemoveSavedBrowser(NULL, GetAppGlobalType(), GLOBALTYPE_ACCOUNT, account->uID, pMachineID);
	}
	else
		devassert(0);
}

void accountClearSavedMachines(AccountInfo *account, MachineType eType)
{
	if (account)
	{
		if (eType == MachineType_CrypticClient)
		{
			accountLog(account, "Clearing all saved Cryptic Clients.");
			AutoTrans_trClearSavedClients(NULL, GetAppGlobalType(), GLOBALTYPE_ACCOUNT, account->uID);
		}
		else if (eType == MachineType_WebBrowser)
		{
			accountLog(account, "Clearing all saved Web Browsers.");
			AutoTrans_trClearSavedBrowsers(NULL, GetAppGlobalType(), GLOBALTYPE_ACCOUNT, account->uID);
		}
		else
		{
			accountLog(account, "Clearing all saved Cryptic Clients and Web Browsers.");
			AutoTrans_trClearAllSavedMachines(NULL, GetAppGlobalType(), GLOBALTYPE_ACCOUNT, account->uID);
		}
	}
}

// TODO(Theo) add optional machine ID saving in with this to automatically save the machine that the enable was trigger from?
void accountMachineLockingEnable(AccountInfo *account, bool bEnable)
{
	if (!account)
		return;
	if (account->eMachineLockState != AMLS_Unknown && accountMachineLockingIsEnabled(account) == bEnable)
		return;
	if (bEnable)
		accountLog(account, "Enabling machine locking.");
	else
		accountLog(account, "Disabling machine locking.");
	AutoTrans_trMachineLockingEnable(NULL, GetAppGlobalType(), GLOBALTYPE_ACCOUNT, account->uID, bEnable);
}

void accountMachineLockingSaveNext(AccountInfo *account, bool bEnable, MachineType eType, bool bProcessingAutosave)
{
	bool bChanged = bEnable; // always run the transaction to update the time if bEnable is true
	if (!account)
		return;
	if (eType == MachineType_All || eType == MachineType_CrypticClient)
	{
		if (!!(account->flags & ACCOUNT_FLAG_AUTOSAVE_MACHINEID_CLIENT) != bEnable)
			bChanged = true;
	}
	if (eType == MachineType_All || eType == MachineType_WebBrowser)
	{
		if (!!(account->flags & ACCOUNT_FLAG_AUTOSAVE_MACHINEID_BROWSER) != bEnable)
			bChanged = true;
	}
	if (bChanged || bProcessingAutosave)
		AutoTrans_trMachineLockingSaveNext(NULL, GetAppGlobalType(), GLOBALTYPE_ACCOUNT, account->uID, bEnable, eType, bProcessingAutosave);
}

void accountMachineProcessAutosave(AccountInfo *account, const char *pMachineID, const char *pMachineName, MachineType eType, const char *ip)
{
	accountMachineLockingSaveNext(account, false, eType, true);
	if (!nullStr(pMachineName))
		accountAddSavedMachine(account, pMachineID, eType, pMachineName, ip);
	else		
		accountMachineEmailOneTimeCodeUsed(account, "", ip, eType);
}

///////////////////////////////////////////////////////////
// One-Time Codes

#define ONETIMECODE_STASH_DEFAULT_SIZE (1000)
#define ONETIMECODE_LENGTH (5)
#define ONETIMECODE_CLEANUP_PERIOD (60) // in seconds
#define OTC_CHARACTER_TABLE_SIZE 10 // 10 digits
// Does not use '0','1','I','O'; All other characters should be easily distinguishable with the correct font
static char sOTCTableBaseCharacters[OTC_CHARACTER_TABLE_SIZE] = {
	'0', '1', '2','3','4','5','6','7','8','9',
};

AUTO_STRUCT;
typedef struct OneTimeCodeStruct
{
	char *pMachineID; AST(KEY)
	char *pOneTimeCode; AST(ESTRING)
	U32 uAccountID;
	U32 uExpireTime;
	U32 uNumAttempts;
} OneTimeCodeStruct;

AUTO_STRUCT;
typedef struct AccountOneTimeCodes
{
	U32 uAccountID;
	EARRAY_OF(OneTimeCodeStruct) eaOneTimeCodes;
} AccountOneTimeCodes;


static StashTable stOneTimeCodeStash;
static EARRAY_OF(OneTimeCodeStruct) seaOneTimeCodes = NULL; // stored in time generated=expired order

// Creates a random alphanumeric string of 5 characters (capital letters and digits)
static void GenerateOneTimeCode(SA_PARAM_NN_VALID char **estr)
{
	int i;
	char buffer[ONETIMECODE_LENGTH+1];
	buffer[ONETIMECODE_LENGTH] = 0;
	for (i=0; i<ONETIMECODE_LENGTH; i++)
	{
		int iRand = cryptSecureRand() % OTC_CHARACTER_TABLE_SIZE;
		buffer[i] = sOTCTableBaseCharacters[iRand];
	}
	estrCopy2(estr, buffer);
}

static bool oneTimeCodeUsed(AccountOneTimeCodes *pAccountOTC, OneTimeCodeStruct *pOTC)
{
	EARRAY_FOREACH_BEGIN(pAccountOTC->eaOneTimeCodes, i);
	{
		if (pAccountOTC->eaOneTimeCodes[i] == pOTC)
			continue;
		if (strcmp(pAccountOTC->eaOneTimeCodes[i]->pOneTimeCode, pOTC->pOneTimeCode) == 0)
			return true;
	}
	EARRAY_FOREACH_END;
	return false;
}

static int findOneTimeCodeByMachineID(AccountOneTimeCodes *pAccountOTC, const char *pMachineID)
{
	EARRAY_FOREACH_BEGIN(pAccountOTC->eaOneTimeCodes, i);
	{
		if (strcmp(pAccountOTC->eaOneTimeCodes[i]->pMachineID, pMachineID) == 0)
			return i;
	}
	EARRAY_FOREACH_END;
	return -1;
}

// Removes all expired codes
static void cleanUpAllExpiredCodes(void)
{
	static U32 suLastTimeRan = 0;
	int iFirstUnexpired;
	U32 uCurTime = timeSecondsSince2000();
	CONTAINERID_EARRAY eaiAccounts = NULL;
	int iRemoved = 0;

	PERFINFO_AUTO_START_FUNC();
	if (!suLastTimeRan)
		suLastTimeRan = uCurTime;
	if (uCurTime - suLastTimeRan < ONETIMECODE_CLEANUP_PERIOD)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	suLastTimeRan = uCurTime;

	iFirstUnexpired = eaSize(&seaOneTimeCodes);
	// Remove from the expire time-sorted EArray and queue accounts that need to be cleaned up
	EARRAY_FOREACH_BEGIN(seaOneTimeCodes, i);
	{
		if (seaOneTimeCodes[i]->uExpireTime > uCurTime)
		{
			iFirstUnexpired = i;
			break;
		}
		eaiPushUnique(&eaiAccounts, seaOneTimeCodes[i]->uAccountID);
	}
	EARRAY_FOREACH_END;
	if (iFirstUnexpired)
		eaRemoveRange(&seaOneTimeCodes, 0, iFirstUnexpired);

	// Go through each account, destroy all the expired OneTimeCodeStructs, and clean up the earray
	EARRAY_INT_CONST_FOREACH_BEGIN(eaiAccounts, i, s);
	{
		AccountOneTimeCodes *pAccountOTC;
		if (stashIntFindPointer(stOneTimeCodeStash, eaiAccounts[i], &pAccountOTC))
		{
			EARRAY_FOREACH_REVERSE_BEGIN(pAccountOTC->eaOneTimeCodes, j);
			{
				if (pAccountOTC->eaOneTimeCodes[j]->uExpireTime > uCurTime)
				{
					continue;
				}

				StructDestroy(parse_OneTimeCodeStruct, eaRemove(&pAccountOTC->eaOneTimeCodes, j));
				++iRemoved;
			}
			EARRAY_FOREACH_END;
			
			if (eaSize(&pAccountOTC->eaOneTimeCodes) == 0)
			{
				stashIntRemovePointer(stOneTimeCodeStash, pAccountOTC->uAccountID, NULL);
				StructDestroy(parse_AccountOneTimeCodes, pAccountOTC);
			}
		}
	}
	EARRAY_FOREACH_END;
	eaiDestroy(&eaiAccounts);
	devassert(iFirstUnexpired == iRemoved);
	PERFINFO_AUTO_STOP_FUNC();
}

static void removeOneTimeCode(SA_PARAM_NN_VALID AccountOneTimeCodes *pAccountOTC, int idx, bool bDestroyStruct)
{
	OneTimeCodeStruct *pOTC;

	PERFINFO_AUTO_START_FUNC();
	pOTC = eaRemove(&pAccountOTC->eaOneTimeCodes, idx);
	if (!devassert(pOTC))
		return;
	devassert(eaFindAndRemove(&seaOneTimeCodes, pOTC) != -1);

	if (bDestroyStruct)
	{
		StructDestroy(parse_OneTimeCodeStruct, pOTC);
		if (eaSize(&pAccountOTC->eaOneTimeCodes) == 0)
		{
			stashIntRemovePointer(stOneTimeCodeStash, pAccountOTC->uAccountID, NULL);
			StructDestroy(parse_AccountOneTimeCodes, pAccountOTC);
		}
	}
	PERFINFO_AUTO_STOP_FUNC();
}

// Returns true if it generated a new One-Time Code, false if it's still using an old one or didn't create one
bool accountMachineGenerateOneTimeCode(AccountInfo *account, const char *pMachineID, const char *ip)
{
	AccountOneTimeCodes *pAccountOTC;
	OneTimeCodeStruct *pOTC;
	U32 uCurTime;
	char *logline = NULL;
	int iExistingIdx;
	bool bCreateNewCode = true;
	
	if (!account || nullStr(pMachineID))
		return false;

	PERFINFO_AUTO_START_FUNC();
	if (!stOneTimeCodeStash)
		stOneTimeCodeStash = stashTableCreateInt(ONETIMECODE_STASH_DEFAULT_SIZE);

	if (!stashIntFindPointer(stOneTimeCodeStash, account->uID, &pAccountOTC))
	{
		pAccountOTC = StructCreate(parse_AccountOneTimeCodes);
		pAccountOTC->uAccountID = account->uID;
		stashIntAddPointer(stOneTimeCodeStash, pAccountOTC->uAccountID, pAccountOTC, false);
	}

	iExistingIdx = findOneTimeCodeByMachineID(pAccountOTC, pMachineID);	
	uCurTime = timeSecondsSince2000();
	if (iExistingIdx == -1)
	{
		pOTC = StructCreate(parse_OneTimeCodeStruct);
		pOTC->uAccountID = account->uID;
		pOTC->pMachineID = StructAllocString(pMachineID);
	}
	else
	{
		pOTC = pAccountOTC->eaOneTimeCodes[iExistingIdx];
		if (pOTC->uExpireTime <= uCurTime)
			removeOneTimeCode(pAccountOTC, iExistingIdx, false);
		else
			bCreateNewCode = false;
	}

	if (bCreateNewCode)
	{
		do 
		{
			GenerateOneTimeCode(&pOTC->pOneTimeCode);
		} while (oneTimeCodeUsed(pAccountOTC, pOTC));
		pOTC->uExpireTime = uCurTime + GetAccountServerConfig()->uOneTimeCodeDuration;
		pOTC->uNumAttempts = 0;
		eaPush(&pAccountOTC->eaOneTimeCodes, pOTC);
		eaPush(&seaOneTimeCodes, pOTC);
	}
	accountMachineEmailOneTimeCode(account, pOTC->pOneTimeCode, ip);

	// Logging
	estrStackCreate(&logline);
	logAppendPairs(&logline,
		logPair("accountid", "%d", account->uID), 
		logPair("machineid", "%s", pMachineID),
		logPair("onetimecode", "%s", pOTC->pOneTimeCode),
		NULL);
	objLog(LOG_LOGIN, GLOBALTYPE_ACCOUNT, account->uID, 0, account->displayName, NULL, NULL, "OneTimeCodeGenerate", NULL, "%s", logline);
	estrDestroy(&logline);
	PERFINFO_AUTO_STOP_FUNC();
	return bCreateNewCode;
}

void accountMachineRemoveOneTimeCode(AccountInfo *account, const char *pMachineID)
{
	AccountOneTimeCodes *pAccountOTC;
	int idx;
	if (!stashIntFindPointer(stOneTimeCodeStash, account->uID, &pAccountOTC))
		return;
	idx = findOneTimeCodeByMachineID(pAccountOTC, pMachineID);
	if (idx != -1)
		removeOneTimeCode(pAccountOTC, idx, true);
}

bool accountMachineValidateOneTimeCode(AccountInfo *account, const char *pMachineID, MachineType eType, 
	const char *pOneTimeCode, const char *pMachineName, const char *ip)
{
	AccountOneTimeCodes *pAccountOTC;
	OneTimeCodeStruct *pOTC;
	int idx;

	if (!stOneTimeCodeStash)
		return false;
	if (!account || nullStr(pMachineID) || nullStr(pOneTimeCode))
		return false;

	if (!stashIntFindPointer(stOneTimeCodeStash, account->uID, &pAccountOTC))
		return false;
	idx = findOneTimeCodeByMachineID(pAccountOTC, pMachineID);
	if (idx == -1)
		return false;
	pOTC = pAccountOTC->eaOneTimeCodes[idx];
	pOTC->uNumAttempts++;
	if (stricmp(pOTC->pOneTimeCode, pOneTimeCode) != 0)
	{
		if (pOTC->uNumAttempts >= GetAccountServerConfig()->uOneTimeCodeAttempts)
			removeOneTimeCode(pAccountOTC, idx, true);
		return false;
	}
	if (pOTC->uExpireTime <= timeSecondsSince2000())
	{
		removeOneTimeCode(pAccountOTC, idx, true);
		return false;
	}
	removeOneTimeCode(pAccountOTC, idx, true);
	if (!nullStr(pMachineName))
		accountAddSavedMachine(account, pMachineID, eType, pMachineName, ip);
	else
		accountMachineEmailOneTimeCodeUsed(account, "", ip, eType);
	return true;
}

void oneTimeCodeTick(void)
{
	// Lazy cleanup is done instead - expired codes for an account are deleted upon generate or validate OTC requests
	cleanUpAllExpiredCodes();
}

AUTO_RUN;
void oneTimeCodeInitialize(void)
{
	stOneTimeCodeStash = stashTableCreateInt(ONETIMECODE_STASH_DEFAULT_SIZE);
	resRegisterDictionaryForStashTable("One Time Codes", RESCATEGORY_OTHER, 0, stOneTimeCodeStash, parse_AccountOneTimeCodes);
}

#include "AutoGen/MachineID_h_ast.c"
#include "AutoGen/MachineID_c_ast.c"