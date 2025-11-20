#include "KeyValues.h"

#include "accountnet.h"
#include "AccountManagement.h"
#include "AccountLog.h"
#include "AccountReporting.h"
#include "AccountServer.h"
#include "AutoTransDefs.h"
#include "fastAtoi.h"
#include "logging.h"
#include "objContainer.h"
#include "ProxyInterface/AccountProxy.h"
#include "rand.h"
#include "stdtypes.h"
#include "StringUtil.h"
#include "TransactionOutcomes.h"
#include "VirtualCurrency.h"

#include "autogen/AccountServer_autotransactions_autogen_wrappers.h"

static bool sbLazyUnlockOnGet = false;
AUTO_CMD_INT(sbLazyUnlockOnGet, LazyUnlockOnGet) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

static bool sbLazyUnlockOnTest = false;
AUTO_CMD_INT(sbLazyUnlockOnTest, LazyUnlockOnTest) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

static bool sbLazyUnlockOnLock = true;
AUTO_CMD_INT(sbLazyUnlockOnLock, LazyUnlockOnLock) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

static U32 suLazyUnlockThreshold = MINUTES(5);
AUTO_CMD_INT(suLazyUnlockThreshold, LazyUnlockThreshold) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

static S64 siKeyValueLimit = U32_MAX;
AUTO_CMD_INT(siKeyValueLimit, KeyValueLimit) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

#define ACCOUNTKEYVALUE_LOCKMARKER 'a'
#define ACCOUNTKEYVALUE_LOCKSEPARATOR '|'
#define ACCOUNTKEYVALUE_LOCKSEPARATOR_STRING "|"

#define ACCOUNTKEYVALUE_LOCKPART_MARKERLEN 1
#define ACCOUNTKEYVALUE_LOCKPART_ACCTIDLEN 8
#define ACCOUNTKEYVALUE_LOCKPART_RANDOMLEN 2

SA_RET_OP_VALID static const AccountKeyValuePair *akvGetPair(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey)
{
	return eaIndexedGetUsingString(&pAccount->ppKeyValuePairs, pKey);
}

static void akvUnlock(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, bool bLazy)
{
	SERVLOG_PAIRS(LOG_ACCOUNT_KEY_VALUE, "AKVUnlock",
		("accountid", "%u", pAccount->uID)
		("key", "%s", pKey)
		("lazy", "%u", bLazy));

	AutoTrans_trAccountKeyValue_Unlock(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, pKey);
}

static bool akvLazyUnlock(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_OP_VALID const AccountKeyValuePair *pPair)
{
	if (!pPair)
	{
		pPair = akvGetPair(pAccount, pKey);
	}

	if (pPair && pPair->lockData && pPair->lockData->time <= timeSecondsSince2000() - suLazyUnlockThreshold)
	{
		akvUnlock(pAccount, pKey, true);
		return true;
	}

	return false;
}

static bool akvPairIsLocked(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_OP_VALID const AccountKeyValuePair *pPair)
{
	if (!pPair)
	{
		pPair = akvGetPair(pAccount, pKey);
	}

	if (sbLazyUnlockOnTest && akvLazyUnlock(pAccount, pKey, pPair))
	{
		return false;
	}

	return pPair && pPair->lockData;
}

static S64 akvGetValue(SA_PARAM_OP_VALID const AccountKeyValuePair *pPair, bool bAllowLock)
{
	if (!pPair)
	{
		return 0;
	}

	if (pPair->lockData && bAllowLock)
	{
		return pPair->lockData->iValue;
	}
	else
	{
		return pPair->iValue;
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppkeyvaluepairs[]");
enumTransactionOutcome trAccountKeyValue_Lock(ATR_ARGS, NOCONST(AccountInfo) *pAccount, const char *pKey, const char *pPassword, S64 iValue)
{
	NOCONST(AccountKeyValuePair) *pPair = eaIndexedGetUsingString(&pAccount->ppKeyValuePairs, pKey);

	if (NONNULL(pPair) && NONNULL(pPair->lockData) && stricmp(pPair->lockData->password, pPassword))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (ISNULL(pPair))
	{
		pPair = StructCreateNoConst(parse_AccountKeyValuePair);
		estrCopy2(&pPair->key, pKey);

		if (!eaIndexedPushUsingStringIfPossible(&pAccount->ppKeyValuePairs, pKey, pPair))
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
	}

	if (ISNULL(pPair->lockData))
	{
		pPair->lockData = StructCreateNoConst(parse_AccountKeyValuePairLock);
	}

	pPair->lockData->iValue = iValue;
	estrCopy2(&pPair->lockData->password, pPassword);
	pPair->lockData->time = timeSecondsSince2000();

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Stupidly hacky hack of hacky propotions. Basically, we know that all our
// key-value chains in the live environment have six keys in them. Thus, we can
// save on the transaction overhead of six individual lock calls by instead
// doing just one transaction to lock all six.
AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppkeyvaluepairs[]");
enumTransactionOutcome trAccountKeyValue_LockSix(ATR_ARGS,
	NOCONST(AccountInfo) *pAccount,
	const char *pPassword,
	const char *pKey1,
	S64 iValue1,
	const char *pKey2,
	S64 iValue2,
	const char *pKey3,
	S64 iValue3,
	const char *pKey4,
	S64 iValue4,
	const char *pKey5,
	S64 iValue5,
	const char *pKey6,
	S64 iValue6)
{
	if (nullStr(pKey1))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (trAccountKeyValue_Lock(ATR_PASS_ARGS, pAccount, pKey1, pPassword, iValue1) != TRANSACTION_OUTCOME_SUCCESS)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (nullStr(pKey2))
	{
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	if (trAccountKeyValue_Lock(ATR_PASS_ARGS, pAccount, pKey2, pPassword, iValue2) != TRANSACTION_OUTCOME_SUCCESS)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (nullStr(pKey3))
	{
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	if (trAccountKeyValue_Lock(ATR_PASS_ARGS, pAccount, pKey3, pPassword, iValue3) != TRANSACTION_OUTCOME_SUCCESS)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (nullStr(pKey4))
	{
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	if (trAccountKeyValue_Lock(ATR_PASS_ARGS, pAccount, pKey4, pPassword, iValue4) != TRANSACTION_OUTCOME_SUCCESS)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (nullStr(pKey5))
	{
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	if (trAccountKeyValue_Lock(ATR_PASS_ARGS, pAccount, pKey5, pPassword, iValue5) != TRANSACTION_OUTCOME_SUCCESS)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (nullStr(pKey6))
	{
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	if (trAccountKeyValue_Lock(ATR_PASS_ARGS, pAccount, pKey6, pPassword, iValue6) != TRANSACTION_OUTCOME_SUCCESS)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void akvLogLock(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_OP_VALID const AccountKeyValuePair *pPair, SA_PARAM_NN_STR const char *pPassword, S64 iValue)
{
	S64 iOldValue = akvGetValue(pPair, true);

	SERVLOG_PAIRS(LOG_ACCOUNT_KEY_VALUE, "AKVLock",
		("accountid", "%u", pAccount->uID)
		("key", "%s", pKey)
		("oldvalue", "%"FORM_LL"d", iOldValue)
		("value", "%"FORM_LL"d", iValue)
		("password", "%s", pPassword));
}

static void akvLock(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_OP_VALID const AccountKeyValuePair *pPair, SA_PARAM_NN_STR const char *pPassword, S64 iValue)
{
	akvLogLock(pAccount, pKey, pPair, pPassword, iValue);
	AutoTrans_trAccountKeyValue_Lock(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, pKey, pPassword, iValue);
}

static void akvLockMultiple(
	SA_PARAM_NN_VALID const AccountInfo *pAccount,
	SA_PARAM_NN_NN_STR const char **ppKeys,
	SA_PARAM_NN_OP_VALID const AccountKeyValuePair **ppPairs,
	SA_PARAM_NN_STR const char *pPassword,
	SA_PARAM_NN_VALID const S64 *piValues)
{
	const char *pKey1 = NULL, *pKey2 = NULL, *pKey3 = NULL, *pKey4 = NULL, *pKey5 = NULL, *pKey6 = NULL;
	S64 iValue1 = 0, iValue2 = 0, iValue3 = 0, iValue4 = 0, iValue5 = 0, iValue6 = 0;

	if (eaSize(&ppKeys) > 6)
	{
		EARRAY_FOREACH_BEGIN(ppKeys, iKey);
		{
			akvLock(pAccount, ppKeys[iKey], ppPairs[iKey], pPassword, piValues[iKey]);
		}
		EARRAY_FOREACH_END;

		return;
	}

	pKey1 = ppKeys[0];
	iValue1 = piValues[0];
	akvLogLock(pAccount, pKey1, ppPairs[0], pPassword, iValue1);

	if (eaSize(&ppKeys) > 1)
	{
		pKey2 = ppKeys[1];
		iValue2 = piValues[1];
		akvLogLock(pAccount, pKey2, ppPairs[1], pPassword, iValue2);
	}

	if (eaSize(&ppKeys) > 2)
	{
		pKey3 = ppKeys[2];
		iValue3 = piValues[2];
		akvLogLock(pAccount, pKey3, ppPairs[2], pPassword, iValue3);
	}

	if (eaSize(&ppKeys) > 3)
	{
		pKey4 = ppKeys[3];
		iValue4 = piValues[3];
		akvLogLock(pAccount, pKey4, ppPairs[3], pPassword, iValue4);
	}

	if (eaSize(&ppKeys) > 4)
	{
		pKey5 = ppKeys[4];
		iValue5 = piValues[4];
		akvLogLock(pAccount, pKey5, ppPairs[4], pPassword, iValue5);
	}

	if (eaSize(&ppKeys) > 5)
	{
		pKey6 = ppKeys[5];
		iValue6 = piValues[5];
		akvLogLock(pAccount, pKey6, ppPairs[5], pPassword, iValue6);
	}

	AutoTrans_trAccountKeyValue_LockSix(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, pPassword, pKey1, iValue1, pKey2, iValue2, pKey3, iValue3, pKey4, iValue4, pKey5, iValue5, pKey6, iValue6);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pAccount, ".Ppkeyvaluepairs[]");
enumTransactionOutcome trhAccountKeyValue_RemovePair(ATR_ARGS, ATH_ARG NOCONST(AccountInfo) *pAccount, const char *pKey)
{
	NOCONST(AccountKeyValuePair) *pPair = eaIndexedRemoveUsingString(&pAccount->ppKeyValuePairs, pKey);

	if (ISNULL(pPair))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	StructDestroyNoConstSafe(parse_AccountKeyValuePair, &pPair);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pAccount, ".Ppkeyvaluepairs[]");
enumTransactionOutcome trhAccountKeyValue_RemoveLock(ATR_ARGS, ATH_ARG NOCONST(AccountInfo) *pAccount, const char *pKey, bool bCommit)
{
	NOCONST(AccountKeyValuePair) *pPair = eaIndexedGetUsingString(&pAccount->ppKeyValuePairs, pKey);

	if (ISNULL(pPair) || ISNULL(pPair->lockData))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (bCommit)
	{
		pPair->iValue = pPair->lockData->iValue;
	}

	if (!pPair->iValue)
	{
		return trhAccountKeyValue_RemovePair(ATR_PASS_ARGS, pAccount, pKey);
	}
	else
	{
		StructDestroyNoConstSafe(parse_AccountKeyValuePairLock, &pPair->lockData);
		return TRANSACTION_OUTCOME_SUCCESS;
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppkeyvaluepairs[]");
enumTransactionOutcome trAccountKeyValue_Unlock(ATR_ARGS, NOCONST(AccountInfo) *pAccount, const char *pKey)
{
	return trhAccountKeyValue_RemoveLock(ATR_PASS_ARGS, pAccount, pKey, false);
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppkeyvaluepairs[]");
enumTransactionOutcome trAccountKeyValue_Commit(ATR_ARGS, NOCONST(AccountInfo) *pAccount, const char *pKey)
{
	return trhAccountKeyValue_RemoveLock(ATR_PASS_ARGS, pAccount, pKey, true);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pAccount, ".Ppkeyvaluepairs[]");
enumTransactionOutcome trhAccountKeyValue_RemoveSix(ATR_ARGS,
	ATH_ARG NOCONST(AccountInfo) *pAccount,
	bool bCommit,
	const char *pKey1,
	const char *pKey2,
	const char *pKey3,
	const char *pKey4,
	const char *pKey5,
	const char *pKey6)
{
	if (nullStr(pKey1))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (trhAccountKeyValue_RemoveLock(ATR_PASS_ARGS, pAccount, pKey1, bCommit) != TRANSACTION_OUTCOME_SUCCESS)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (nullStr(pKey2))
	{
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	if (trhAccountKeyValue_RemoveLock(ATR_PASS_ARGS, pAccount, pKey2, bCommit) != TRANSACTION_OUTCOME_SUCCESS)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (nullStr(pKey3))
	{
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	if (trhAccountKeyValue_RemoveLock(ATR_PASS_ARGS, pAccount, pKey3, bCommit) != TRANSACTION_OUTCOME_SUCCESS)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (nullStr(pKey4))
	{
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	if (trhAccountKeyValue_RemoveLock(ATR_PASS_ARGS, pAccount, pKey4, bCommit) != TRANSACTION_OUTCOME_SUCCESS)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (nullStr(pKey5))
	{
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	if (trhAccountKeyValue_RemoveLock(ATR_PASS_ARGS, pAccount, pKey5, bCommit) != TRANSACTION_OUTCOME_SUCCESS)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (nullStr(pKey6))
	{
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	if (trhAccountKeyValue_RemoveLock(ATR_PASS_ARGS, pAccount, pKey6, bCommit) != TRANSACTION_OUTCOME_SUCCESS)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppkeyvaluepairs[]");
enumTransactionOutcome trAccountKeyValue_UnlockSix(ATR_ARGS, NOCONST(AccountInfo) *pAccount, const char *pKey1, const char *pKey2, const char *pKey3, const char *pKey4, const char *pKey5, const char *pKey6)
{
	return trhAccountKeyValue_RemoveSix(ATR_PASS_ARGS, pAccount, false, pKey1, pKey2, pKey3, pKey4, pKey5, pKey6);
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppkeyvaluepairs[]");
enumTransactionOutcome trAccountKeyValue_CommitSix(ATR_ARGS, NOCONST(AccountInfo) *pAccount, const char *pKey1, const char *pKey2, const char *pKey3, const char *pKey4, const char *pKey5, const char *pKey6)
{
	return trhAccountKeyValue_RemoveSix(ATR_PASS_ARGS, pAccount, false, pKey1, pKey2, pKey3, pKey4, pKey5, pKey6);
}

static void akvNotifyKeyChange(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey)
{
	const VirtualCurrency **eaCurrencies = NULL;

	if (akvGetPair(pAccount, pKey))
	{
		SendKeyValueToAllProxies(pAccount, pKey);
	}
	else
	{
		SendKeyRemoveToAllProxies(pAccount->uID, pKey);
	}

	eaCurrencies = VirtualCurrency_GetAll();

	EARRAY_CONST_FOREACH_BEGIN(eaCurrencies, iCurrency, iNumCurrencies);
	{
		const VirtualCurrency *pCurrency = eaCurrencies[iCurrency];

		if (!devassert(pCurrency) || !pCurrency->bIsChain)
		{
			continue;
		}

		if (VirtualCurrency_IsKeyInChain(pCurrency, pKey))
		{
			SendKeyValueToAllProxies(pAccount, pCurrency->pName);
		}
	}
	EARRAY_FOREACH_END;

	eaDestroy(&eaCurrencies);
}

static void akvLogCommit(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_NN_VALID const AccountKeyValuePair *pPair)
{
	SERVLOG_PAIRS(LOG_ACCOUNT_KEY_VALUE, "AKVCommit",
		("accountid", "%u", pAccount->uID)
		("key", "%s", pKey)
		("oldvalue", "%"FORM_LL"d", pPair->iValue)
		("value", "%"FORM_LL"d", pPair->lockData->iValue));

	if (pPair->iValue != pPair->lockData->iValue)
	{
		accountLog(pAccount, "Key-value committed: %s (changed from: '%"FORM_LL"d' to: '%"FORM_LL"d')", pKey, pPair->iValue, pPair->lockData->iValue);
	}
}

static void akvCommit(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_NN_VALID const AccountKeyValuePair *pPair)
{
	akvLogCommit(pAccount, pKey, pPair);
	accountReportKeyValue(pAccount, pKey, pPair->iValue, pPair->lockData->iValue, true);
	AutoTrans_trAccountKeyValue_Commit(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, pKey);
	akvNotifyKeyChange(pAccount, pKey);
}

static void akvCommitMultiple(
	SA_PARAM_NN_VALID const AccountInfo *pAccount,
	SA_PARAM_NN_NN_STR const char **ppKeys,
	SA_PARAM_NN_NN_VALID const AccountKeyValuePair **ppPairs)
{
	const char *pKey1 = NULL, *pKey2 = NULL, *pKey3 = NULL, *pKey4 = NULL, *pKey5 = NULL, *pKey6 = NULL;

	if (eaSize(&ppKeys) > 6)
	{
		EARRAY_FOREACH_BEGIN(ppKeys, iKey);
		{
			akvCommit(pAccount, ppKeys[iKey], ppPairs[iKey]);
		}
		EARRAY_FOREACH_END;

		return;
	}

	pKey1 = ppKeys[0];
	akvLogCommit(pAccount, pKey1, ppPairs[0]);

	if (eaSize(&ppKeys) > 1)
	{
		pKey2 = ppKeys[1];
		akvLogCommit(pAccount, pKey2, ppPairs[1]);
	}

	if (eaSize(&ppKeys) > 2)
	{
		pKey3 = ppKeys[2];
		akvLogCommit(pAccount, pKey3, ppPairs[2]);
	}

	if (eaSize(&ppKeys) > 3)
	{
		pKey4 = ppKeys[3];
		akvLogCommit(pAccount, pKey4, ppPairs[3]);
	}

	if (eaSize(&ppKeys) > 4)
	{
		pKey5 = ppKeys[4];
		akvLogCommit(pAccount, pKey5, ppPairs[4]);
	}

	if (eaSize(&ppKeys) > 5)
	{
		pKey6 = ppKeys[5];
		akvLogCommit(pAccount, pKey6, ppPairs[5]);
	}

	AutoTrans_trAccountKeyValue_CommitSix(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, pKey1, pKey2, pKey3, pKey4, pKey5, pKey6);
}

static void akvLogRollback(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey)
{
	SERVLOG_PAIRS(LOG_ACCOUNT_KEY_VALUE, "AKVRollback",
		("accountid", "%u", pAccount->uID)
		("key", "%s", pKey));
}

static void akvRollback(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey)
{
	akvLogRollback(pAccount, pKey);
	AutoTrans_trAccountKeyValue_Unlock(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, pKey);
}

static void akvRollbackMultiple(
	SA_PARAM_NN_VALID const AccountInfo *pAccount,
	SA_PARAM_NN_NN_STR const char **ppKeys,
	SA_PARAM_NN_NN_VALID const AccountKeyValuePair **ppPairs)
{
	const char *pKey1 = NULL, *pKey2 = NULL, *pKey3 = NULL, *pKey4 = NULL, *pKey5 = NULL, *pKey6 = NULL;

	if (eaSize(&ppKeys) > 6)
	{
		EARRAY_FOREACH_BEGIN(ppKeys, iKey);
		{
			akvRollback(pAccount, ppKeys[iKey]);
		}
		EARRAY_FOREACH_END;

		return;
	}

	pKey1 = ppKeys[0];
	akvLogRollback(pAccount, pKey1);

	if (eaSize(&ppKeys) > 1)
	{
		pKey2 = ppKeys[1];
		akvLogRollback(pAccount, pKey2);
	}

	if (eaSize(&ppKeys) > 2)
	{
		pKey3 = ppKeys[2];
		akvLogRollback(pAccount, pKey3);
	}

	if (eaSize(&ppKeys) > 3)
	{
		pKey4 = ppKeys[3];
		akvLogRollback(pAccount, pKey4);
	}

	if (eaSize(&ppKeys) > 4)
	{
		pKey5 = ppKeys[4];
		akvLogRollback(pAccount, pKey5);
	}

	if (eaSize(&ppKeys) > 5)
	{
		pKey6 = ppKeys[5];
		akvLogRollback(pAccount, pKey6);
	}

	AutoTrans_trAccountKeyValue_UnlockSix(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, pKey1, pKey2, pKey3, pKey4, pKey5, pKey6);
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppkeyvaluepairs[]");
enumTransactionOutcome trAccountKeyValue_Remove(ATR_ARGS, NOCONST(AccountInfo) *pAccount, const char *pKey)
{
	return trhAccountKeyValue_RemovePair(ATR_PASS_ARGS, pAccount, pKey);
}

static void akvRemove(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_NN_VALID const AccountKeyValuePair *pPair)
{
	SERVLOG_PAIRS(LOG_ACCOUNT_KEY_VALUE, "AKVRemove",
		("accountid", "%u", pAccount->uID)
		("key", "%s", pKey)
		("oldvalue", "%"FORM_LL"d", pPair->iValue));

	accountLog(pAccount, "Key-value removed: %s (value: '%"FORM_LL"d')", pKey, pPair->iValue);
	AutoTrans_trAccountKeyValue_Remove(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, pKey);
	akvNotifyKeyChange(pAccount, pKey);
}

static void akvConcatLockPassword(SA_PARAM_OP_OP_STR char **ppOutPassword, SA_PARAM_NN_STR const char *pInPassword)
{
	if (!ppOutPassword)
	{
		return;
	}

	if (estrLength(ppOutPassword) > 0)
	{
		estrConcatChar(ppOutPassword, ACCOUNTKEYVALUE_LOCKSEPARATOR);
	}

	estrAppend2(ppOutPassword, pInPassword);
}

static void akvGenerateLockPassword(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PRE_NN_OP_STR SA_POST_NN_NN_STR char **ppOutPassword)
{
	estrPrintf(ppOutPassword, "%c%08x%02x%s", ACCOUNTKEYVALUE_LOCKMARKER, pAccount->uID, randomIntRange(0, 255), pKey);
}

SA_RET_OP_OP_STR static char **akvDivideLockPassword(SA_PARAM_NN_STR const char *pInPassword)
{
	char **ppPasswordParts = NULL;
	int i = 0;

	if (!pInPassword || pInPassword[0] != ACCOUNTKEYVALUE_LOCKMARKER)
	{
		return NULL;
	}

	DivideString(pInPassword, ACCOUNTKEYVALUE_LOCKSEPARATOR_STRING, &ppPasswordParts, DIVIDESTRING_POSTPROCESS_ESTRINGS);
	return ppPasswordParts;
}

static AccountKeyValueResult akvDecodeLockPassword(SA_PARAM_NN_STR const char *pInPasswordPart, SA_PRE_NN_NULL SA_POST_NN_OP_VALID const AccountInfo **ppOutAccount, SA_PRE_NN_NULL SA_POST_NN_OP_STR const char **ppOutKey)
{
	const AccountInfo *pAccount = NULL;
	const char *pKey = NULL;

	char cEncodedAccount[ACCOUNTKEYVALUE_LOCKPART_ACCTIDLEN + 1] = "";
	char *pConsumed = NULL;
	U32 uAccountID = 0;

	if (!pInPasswordPart || pInPasswordPart[0] != ACCOUNTKEYVALUE_LOCKMARKER)
	{
		return AKV_INVALID_LOCK;
	}

	// Lock part must be at least long enough to hold the first three parts and a key of non-zero length
	if (estrLength(&pInPasswordPart) <= ACCOUNTKEYVALUE_LOCKPART_MARKERLEN + ACCOUNTKEYVALUE_LOCKPART_ACCTIDLEN + ACCOUNTKEYVALUE_LOCKPART_RANDOMLEN)
	{
		return AKV_INVALID_LOCK;
	}

	// Read out pCurLock[1..8] and try to decode them from hex into an account ID
	strncpy(cEncodedAccount, pInPasswordPart + ACCOUNTKEYVALUE_LOCKPART_MARKERLEN, ACCOUNTKEYVALUE_LOCKPART_ACCTIDLEN);
	cEncodedAccount[ACCOUNTKEYVALUE_LOCKPART_ACCTIDLEN] = 0;
	uAccountID = strtoul(cEncodedAccount, &pConsumed, 16);

	// If the whole thing didn't decode as valid hex, the lock was invalid
	if (!pConsumed || pConsumed - cEncodedAccount < ACCOUNTKEYVALUE_LOCKPART_ACCTIDLEN)
	{
		return AKV_INVALID_LOCK;
	}

	pAccount = findAccountByID(uAccountID);

	if (!pAccount)
	{
		return AKV_NONEXISTANT;
	}

	// Skip forward to where the key [presumably] begins
	pKey = pInPasswordPart + ACCOUNTKEYVALUE_LOCKPART_MARKERLEN + ACCOUNTKEYVALUE_LOCKPART_ACCTIDLEN + ACCOUNTKEYVALUE_LOCKPART_RANDOMLEN;

	if (ppOutAccount)
	{
		*ppOutAccount = pAccount;
	}

	if (ppOutKey)
	{
		*ppOutKey = pKey;
	}

	return AKV_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppkeyvaluepairs");
enumTransactionOutcome trAccountMigrateKeyValues(ATR_ARGS, NOCONST(AccountInfo) *pAccount)
{
	EARRAY_CONST_FOREACH_REVERSE_BEGIN(pAccount->ppKeyValuePairs, iPair, iNumPairs);
	{
		NOCONST(AccountKeyValuePair) *pPair = pAccount->ppKeyValuePairs[iPair];

		if (pPair->lockData && pPair->lockData->sValue_Obsolete)
		{
			char *pConsumed = NULL;
			S64 iValue = strtoi64_fast(pPair->lockData->sValue_Obsolete, &pConsumed, 10);

			if (pConsumed && !*pConsumed)
				pPair->lockData->iValue = iValue;

			estrDestroy(&pPair->lockData->sValue_Obsolete);
		}

		if (pPair->sValue_Obsolete)
		{
			char *pConsumed = NULL;
			S64 iValue = strtoi64_fast(pPair->sValue_Obsolete, &pConsumed, 10);

			if (pConsumed && !*pConsumed)
				pPair->iValue = iValue;

			estrDestroy(&pPair->sValue_Obsolete);
		}

		// If the key has a 0 value it should go away, unless it's locked
		if (!pPair->iValue && !pPair->lockData)
		{
			eaRemove(&pAccount->ppKeyValuePairs, iPair);
			StructDestroyNoConst(parse_AccountKeyValuePair, pPair);
		}
		else if (!pPair->iValue)
		{
			pPair->lockData->deleteOnRollback = true;
		}
	}
	EARRAY_FOREACH_END;

	return TRANSACTION_OUTCOME_SUCCESS;
}

void AccountKeyValue_MigrateLegacyStorage(const AccountInfo *account)
{
	AutoTrans_trAccountMigrateKeyValues(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, account->uID);
}

static AccountKeyValueResult akvGetChain(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, bool bAllowLock, SA_PARAM_NN_VALID S64 *piValue)
{
	const AccountKeyValuePair *pPair = NULL;
	const VirtualCurrency *pCurrency = VirtualCurrency_GetByName(pKey);
	char **ppUniqueParts = NULL;
	S64 iTotal = 0;

	if (!pCurrency)
	{
		return AKV_INVALID_KEY;
	}

	VirtualCurrency_GetUniqueChainParts(pCurrency->pName, &ppUniqueParts);

	EARRAY_FOREACH_BEGIN(ppUniqueParts, iPart);
	{
		const char *pPart = ppUniqueParts[iPart];
		S64 iPartValue = 0;

		if (!devassert(pPart))
		{
			return AKV_NONEXISTANT;
		}

		AccountKeyValue_GetEx(pAccount, pPart, bAllowLock, &iPartValue);
		iTotal += iPartValue;
	}
	EARRAY_FOREACH_END;

	eaDestroyEx(&ppUniqueParts, NULL);

	if (piValue)
	{
		*piValue = iTotal;
	}

	return AKV_SUCCESS;
}

AccountKeyValueResult AccountKeyValue_GetEx(const AccountInfo *pAccount, const char *pKey, bool bAllowLock, S64 *piValue)
{
	const AccountKeyValuePair *pPair = NULL;

	if (VirtualCurrency_IsChain(pKey))
	{
		return akvGetChain(pAccount, pKey, bAllowLock, piValue);
	}

	pPair = akvGetPair(pAccount, pKey);

	if (!pPair)
	{
		return AKV_NONEXISTANT;
	}

	if (sbLazyUnlockOnGet && akvLazyUnlock(pAccount, pKey, pPair))
	{
		pPair = akvGetPair(pAccount, pKey);

		if (!pPair)
		{
			return AKV_NONEXISTANT;
		}
	}

	if (piValue)
	{
		*piValue = akvGetValue(pPair, bAllowLock);
	}

	return AKV_SUCCESS;
}

bool AccountKeyValue_IsLockedEx(const AccountInfo *pAccount, const char *pKey)
{
	return akvPairIsLocked(pAccount, pKey, NULL);
}

static AccountKeyValueResult akvPairCanBeLocked(SA_PARAM_NN_VALID const AccountInfo *pAccount,
	SA_PARAM_NN_STR const char *pKey,
	SA_PARAM_OP_VALID const AccountKeyValuePair *pPair,
	SA_PARAM_NN_VALID S64 *piValue,
	bool bModify,
	SA_PARAM_OP_STR const char *pPassword,
	bool bAllowRelock)
{
	// If the pair is present and locked, it might still be lockable
	if (pPair && akvPairIsLocked(pAccount, pKey, pPair))
	{
		// If we can't relock, or they didn't provide a password, or the password doesn't match, try lazy unlock
		if (!bAllowRelock || !pPassword || stricmp(pPair->lockData->password, pPassword))
		{
			if (sbLazyUnlockOnLock && akvLazyUnlock(pAccount, pKey, pPair))
			{
				pPair = akvGetPair(pAccount, pKey);
			}
		}

		if (akvPairIsLocked(pAccount, pKey, pPair))
		{
			// If the pair is still locked, and we don't allow relock or they didn't provide a password, AKV_LOCKED
			if (!bAllowRelock || !pPassword)
			{
				return AKV_LOCKED;
			}

			// If the pair is still locked and the password doesn't match, AKV_INVALID_LOCK
			if (stricmp(pPair->lockData->password, pPassword))
			{
				return AKV_INVALID_LOCK;
			}

			// Otherwise, the pair is locked, we allow relock, a password was specified and it matches, so it should succeed
		}
	}

	// Now we know that we could lock the key - so compute and verify the value
	if (bModify)
	{
		*piValue += akvGetValue(pPair, true);
	}

	if (*piValue < 0 || *piValue > siKeyValueLimit)
	{
		return AKV_INVALID_RANGE;
	}

	return AKV_SUCCESS;
}

AccountKeyValueResult AccountKeyValue_LockEx(const AccountInfo *pAccount, const char *pKey, S64 iValue, bool bModify, char **ppInOutPassword, bool bAllowRelock)
{
	const AccountKeyValuePair *pPair = akvGetPair(pAccount, pKey);
	AccountKeyValueResult eResult = AKV_FAILURE;

	if (!ppInOutPassword)
	{
		return AKV_FAILURE;
	}

	eResult = akvPairCanBeLocked(pAccount, pKey, pPair, &iValue, bModify, *ppInOutPassword, bAllowRelock);

	if (eResult != AKV_SUCCESS)
	{
		return eResult;
	}

	if (!*ppInOutPassword)
	{
		akvGenerateLockPassword(pAccount, pKey, ppInOutPassword);
	}

	akvLock(pAccount, pKey, pPair, *ppInOutPassword, iValue);
	return AKV_SUCCESS;
}

static AccountKeyValueResult akvSetChain(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, S64 iValue, bool bModify, SA_PRE_NN_OP_STR SA_POST_NN_NN_STR char **ppInOutPassword, bool bAllowRelock)
{
	VirtualCurrency *pCurrency = NULL;
	AccountKeyValueResult eResult = AKV_SUCCESS;
	char *pPassword = NULL;
	char **ppUniqueParts = NULL;
	S64 iExistingValue = 0;
	S64 iDiff = iValue;
	S64 *piValues = NULL;
	const AccountKeyValuePair **ppPairs = NULL;

	if (!ppInOutPassword)
	{
		return AKV_FAILURE;
	}

	pCurrency = VirtualCurrency_GetByName(pKey);

	if (!pCurrency)
	{
		return AKV_INVALID_KEY;
	}

	pPassword = *ppInOutPassword;

	if (!bModify)
	{
		akvGetChain(pAccount, pKey, bAllowRelock, &iExistingValue);
		iDiff -= iExistingValue;
	}

	VirtualCurrency_GetUniqueChainParts(pCurrency->pName, &ppUniqueParts);

	if (iDiff >= 0)
	{
		int iPart = 0;
		const AccountKeyValuePair *pPair = akvGetPair(pAccount, ppUniqueParts[0]);

		ea64Push(&piValues, iDiff);
		eaPush(&ppPairs, pPair);
		eResult = akvPairCanBeLocked(pAccount, ppUniqueParts[0], pPair, &piValues[0], true, pPassword, bAllowRelock);

		for (iPart = 1; eResult == AKV_SUCCESS && iPart < eaSize(&ppUniqueParts); ++iPart)
		{
			const char *pPart = ppUniqueParts[iPart];
			pPair = akvGetPair(pAccount, pPart);
			ea64Push(&piValues, 0);
			eaPush(&ppPairs, pPair);

			eResult = akvPairCanBeLocked(pAccount, pPart, pPair, &piValues[iPart], true, pPassword, bAllowRelock);
		}
	}
	else
	{
		iDiff = -iDiff;

		EARRAY_FOREACH_BEGIN(ppUniqueParts, iPart);
		{
			const char *pPart = ppUniqueParts[iPart];
			S64 iPartValue = 0;
			S64 iSpend = 0;
			const AccountKeyValuePair *pPair = akvGetPair(pAccount, pPart);

			eResult = akvPairCanBeLocked(pAccount, pPart, pPair, &iPartValue, true, pPassword, true);

			if (eResult != AKV_SUCCESS)
			{
				break;
			}

			iSpend = min(iDiff, iPartValue);
			iDiff -= iSpend;

			ea64Push(&piValues, iPartValue - iSpend);
			eaPush(&ppPairs, pPair);
		}
		EARRAY_FOREACH_END;

		if (eResult == AKV_SUCCESS && iDiff > 0)
		{
			eResult = AKV_INVALID_RANGE;
		}
	}

	if (eResult == AKV_SUCCESS)
	{
		if (!pPassword)
		{
			akvGenerateLockPassword(pAccount, pKey, &pPassword);
			akvConcatLockPassword(ppInOutPassword, pPassword);
			estrDestroy(&pPassword);
		}

		ANALYSIS_ASSUME(ppUniqueParts);
		akvLockMultiple(pAccount, ppUniqueParts, ppPairs, *ppInOutPassword, piValues);
	}

	eaDestroyEx(&ppUniqueParts, NULL);
	eaDestroy(&ppPairs);
	ea64Destroy(&piValues);
	return eResult;
}

AccountKeyValueResult AccountKeyValue_SetEx(const AccountInfo *pAccount, const char *pKey, S64 iValue, bool bModify, char **ppInOutPassword, bool bAllowRelock)
{
	if (VirtualCurrency_IsChain(pKey))
	{
		return akvSetChain(pAccount, pKey, iValue, bModify, ppInOutPassword, bAllowRelock);
	}

	return AccountKeyValue_LockEx(pAccount, pKey, iValue, bModify, ppInOutPassword, bAllowRelock);
}

static AccountKeyValueResult akvMoveChain(
	SA_PARAM_NN_VALID const AccountInfo *pSrcAccount,
	SA_PARAM_NN_STR const char *pSrcKey,
	SA_PARAM_NN_VALID const AccountInfo *pDestAccount,
	SA_PARAM_NN_STR const char *pDestKey,
	S64 iValue,
	SA_PRE_NN_OP_STR SA_POST_NN_NN_STR char **ppInOutSrcPassword,
	SA_PRE_NN_OP_STR SA_POST_NN_NN_STR char **ppInOutDestPassword,
	bool bAllowRelock);

static AccountKeyValueResult akvMove(
	SA_PARAM_NN_VALID const AccountInfo *pSrcAccount,
	SA_PARAM_NN_STR const char *pSrcKey,
	SA_PARAM_NN_VALID const AccountInfo *pDestAccount,
	SA_PARAM_NN_STR const char *pDestKey,
	S64 iValue,
	SA_PRE_NN_OP_STR SA_POST_NN_NN_STR char **ppInOutSrcPassword,
	SA_PRE_NN_OP_STR SA_POST_NN_NN_STR char **ppInOutDestPassword,
	bool bAllowRelock)
{
	AccountKeyValueResult eResult = AKV_FAILURE;
	char *pSrcPassword = NULL;
	char *pDestPassword = NULL;

	if (!ppInOutSrcPassword || !ppInOutDestPassword)
	{
		return AKV_FAILURE;
	}

	if (iValue < 0 || iValue > siKeyValueLimit)
	{
		return AKV_INVALID_RANGE;
	}

	if (VirtualCurrency_IsChain(pSrcKey) && VirtualCurrency_IsChain(pDestKey))
	{
		// Special case chain move, matching part for part
		return akvMoveChain(pSrcAccount, pSrcKey, pDestAccount, pDestKey, iValue, ppInOutSrcPassword, ppInOutDestPassword, bAllowRelock);
	}

	pSrcPassword = *ppInOutSrcPassword;
	pDestPassword = *ppInOutDestPassword;

	eResult = AccountKeyValue_SetEx(pSrcAccount, pSrcKey, -iValue, true, &pSrcPassword, bAllowRelock);

	if (eResult != AKV_SUCCESS)
	{
		return eResult;
	}

	eResult = AccountKeyValue_SetEx(pDestAccount, pDestKey, iValue, true, &pDestPassword, bAllowRelock);

	if (eResult != AKV_SUCCESS)
	{
		akvRollback(pSrcAccount, pSrcKey);
		estrDestroy(&pSrcPassword);
	}
	else
	{
		if (!*ppInOutSrcPassword)
		{
			akvConcatLockPassword(ppInOutSrcPassword, pSrcPassword);
			estrDestroy(&pSrcPassword);
		}

		if (!*ppInOutDestPassword)
		{
			akvConcatLockPassword(ppInOutDestPassword, pDestPassword);
			estrDestroy(&pDestPassword);
		}
	}

	return eResult;
}

static AccountKeyValueResult akvMoveChain(
	const AccountInfo *pSrcAccount,
	const char *pSrcKey,
	const AccountInfo *pDestAccount,
	const char *pDestKey,
	S64 iValue,
	char **ppInOutSrcPassword,
	char **ppInOutDestPassword,
	bool bAllowRelock)
{
	char *pSrcPassword = NULL;
	char *pDestPassword = NULL;
	AccountKeyValueResult eResult = AKV_SUCCESS;

	char **ppSrcParts = NULL;
	char **ppDestParts = NULL;
	S64 *piSrcValues = NULL;
	S64 *piDestValues = NULL;
	const AccountKeyValuePair **ppSrcPairs = NULL;
	const AccountKeyValuePair **ppDestPairs = NULL;

	if (!ppInOutSrcPassword || !ppInOutDestPassword)
	{
		return AKV_FAILURE;
	}

	if (!VirtualCurrency_IsChain(pSrcKey) || !VirtualCurrency_IsChain(pDestKey))
	{
		return AKV_INVALID_KEY;
	}

	VirtualCurrency_GetChainParts(pSrcKey, &ppSrcParts, false);
	VirtualCurrency_GetChainParts(pDestKey, &ppDestParts, false);

	if (eaSize(&ppSrcParts) != eaSize(&ppDestParts))
	{
		return AKV_FORBIDDEN_CHANGE;
	}

	pSrcPassword = *ppInOutSrcPassword;
	pDestPassword = *ppInOutDestPassword;

	if (!pSrcPassword)
	{
		akvGenerateLockPassword(pSrcAccount, pSrcKey, &pSrcPassword);
	}

	if (!pDestPassword)
	{
		akvGenerateLockPassword(pDestAccount, pDestKey, &pDestPassword);
	}

	EARRAY_FOREACH_BEGIN(ppSrcParts, iPart);
	{
		const char *pSrcPart = ppSrcParts[iPart];
		const char *pDestPart = ppDestParts[iPart];
		S64 iSrcValue = 0;
		S64 iSpend = 0;
		const AccountKeyValuePair *pPair = akvGetPair(pSrcAccount, pSrcPart);
		int iDupIndex = -1;

		// If the pair exists, it was already consumed as much as possible
		// If it wasn't fully consumed, then iValue must now be 0 and there's nothing left to do, so skip this
		// If it was fully consumed, then iValue is still over 0 and akvPairCanBeLocked will say it wasn't consumed, so skip this
		// If the pair doesn't exist, it has no value and there's no danger in locking it twice for "set to 0"
		if (pPair)
		{
			iDupIndex = eaFind(&ppSrcPairs, pPair);
		}

		if (iDupIndex == -1)
		{
			eResult = akvPairCanBeLocked(pSrcAccount, pSrcPart, pPair, &iSrcValue, true, pSrcPassword, true);

			if (eResult != AKV_SUCCESS)
			{
				break;
			}

			iSpend = min(iValue, iSrcValue);
			iValue -= iSpend;

			ea64Push(&piSrcValues, iSrcValue - iSpend);
			eaPush(&ppSrcPairs, pPair);
		}
		else
		{
			iSrcValue = piSrcValues[iDupIndex];
			ea64Push(&piSrcValues, iSrcValue);
			eaPush(&ppSrcPairs, pPair);
		}

		// Here we have to specifically look for a previous occurrence of the key in our dest part list
		// This is because the act of moving stuff to the destination chain may create some pairs that don't exist now
		// So if there are any of those, we may need to modify them twice (I'm sad about this, but it has to work)
		iDupIndex = eaFindString(&ppDestParts, pDestPart);
		pPair = akvGetPair(pDestAccount, pDestPart);

		if (iDupIndex == -1 || iDupIndex >= iPart)
		{
			eResult = akvPairCanBeLocked(pDestAccount, pDestPart, pPair, &iSpend, true, pDestPassword, true);

			if (eResult != AKV_SUCCESS)
			{
				break;
			}

			ea64Push(&piDestValues, iSpend);
			eaPush(&ppDestPairs, pPair);
		}
		else
		{
			iSrcValue = piDestValues[iDupIndex];
			ea64Push(&piDestValues, iSrcValue + iSpend);
			eaPush(&ppDestPairs, pPair);
		}
	}
	EARRAY_FOREACH_END;

	if (eResult == AKV_SUCCESS && iValue > 0)
	{
		eResult = AKV_INVALID_RANGE;
	}

	if (eResult == AKV_SUCCESS)
	{
		if (!*ppInOutSrcPassword)
		{
			akvConcatLockPassword(ppInOutSrcPassword, pSrcPassword);
			estrDestroy(&pSrcPassword);
		}

		if (!*ppInOutDestPassword)
		{
			akvConcatLockPassword(ppInOutDestPassword, pDestPassword);
			estrDestroy(&pDestPassword);
		}

		akvLockMultiple(pSrcAccount, ppSrcParts, ppSrcPairs, *ppInOutSrcPassword, piSrcValues);
		akvLockMultiple(pDestAccount, ppDestParts, ppDestPairs, *ppInOutDestPassword, piDestValues);
	}

	eaDestroyEx(&ppSrcParts, NULL);
	eaDestroyEx(&ppDestParts, NULL);
	ea64Destroy(&piSrcValues);
	ea64Destroy(&piDestValues);
	eaDestroy(&ppSrcPairs);
	eaDestroy(&ppDestPairs);
	return eResult;
}

AccountKeyValueResult AccountKeyValue_MoveEx(
	const AccountInfo *pSrcAccount,
	const char *pSrcKey,
	const AccountInfo *pDestAccount,
	const char *pDestKey,
	S64 iValue,
	char **ppInOutPassword,
	bool bAllowRelock)
{
	AccountKeyValueResult eResult = AKV_FAILURE;
	char **ppPasswordParts = NULL;
	char *pSrcPassword = NULL;
	char *pDestPassword = NULL;

	if (!ppInOutPassword)
	{
		return AKV_FAILURE;
	}
	
	if (*ppInOutPassword)
	{
		ppPasswordParts = akvDivideLockPassword(*ppInOutPassword);
	
		if (eaSize(&ppPasswordParts) != 2)
		{
			eaDestroyEString(&ppPasswordParts);
			return AKV_INVALID_LOCK;
		}
	
		pSrcPassword = ppPasswordParts[0];
		pDestPassword = ppPasswordParts[1];
	}
	
	eResult = akvMove(pSrcAccount, pSrcKey, pDestAccount, pDestKey, iValue, &pSrcPassword, &pDestPassword, bAllowRelock);
	eaDestroyEString(&ppPasswordParts);

	if (!*ppInOutPassword)
	{
		akvConcatLockPassword(ppInOutPassword, pSrcPassword);
		estrDestroy(&pSrcPassword);

		akvConcatLockPassword(ppInOutPassword, pDestPassword);
		estrDestroy(&pDestPassword);
	}

	return eResult;
}

static AccountKeyValueResult akvPairCanBeFinalized(
	SA_PARAM_NN_VALID const AccountInfo *pAccount,
	SA_PARAM_NN_STR const char *pKey,
	SA_PARAM_OP_STR const char *pPassword,
	SA_PARAM_OP_VALID const AccountKeyValuePair *pPair)
{
	if (!pPair)
	{
		return AKV_NONEXISTANT;
	}

	if (!akvPairIsLocked(pAccount, pKey, pPair))
	{
		return AKV_NOT_LOCKED;
	}

	if (!pPassword || stricmp(pPair->lockData->password, pPassword))
	{
		return AKV_INVALID_LOCK;
	}

	return AKV_SUCCESS;
}

static AccountKeyValueResult akvFinalizeChain(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_NN_STR const char *pPassword, bool bCommit)
{
	const VirtualCurrency *pCurrency = VirtualCurrency_GetByName(pKey);
	char **ppUniqueParts = NULL;
	AccountKeyValueResult eResult = AKV_SUCCESS;
	int iSuccesses = 0;
	const AccountKeyValuePair **ppPairs = NULL;

	if (!pCurrency)
	{
		return AKV_INVALID_KEY;
	}

	VirtualCurrency_GetUniqueChainParts(pCurrency->pName, &ppUniqueParts);

	EARRAY_FOREACH_BEGIN(ppUniqueParts, iPart);
	{
		const char *pPart = ppUniqueParts[iPart];
		const AccountKeyValuePair *pPair = akvGetPair(pAccount, pPart);
		
		eResult = akvPairCanBeFinalized(pAccount, pKey, pPassword, pPair);

		if (eResult != AKV_SUCCESS)
		{
			break;
		}

		++iSuccesses;
		eaPush(&ppPairs, pPair);
	}
	EARRAY_FOREACH_END;

	if (eResult == AKV_SUCCESS)
	{
		if (bCommit)
		{
			ANALYSIS_ASSUME(ppUniqueParts);
			ANALYSIS_ASSUME(ppPairs);
			akvCommitMultiple(pAccount, ppUniqueParts, ppPairs);
		}
		else
		{
			ANALYSIS_ASSUME(ppUniqueParts);
			ANALYSIS_ASSUME(ppPairs);
			akvRollbackMultiple(pAccount, ppUniqueParts, ppPairs);
		}
	}

	if (bCommit && eResult != AKV_SUCCESS)
	{
		char *pAlertStr = NULL;

		EARRAY_FOREACH_BEGIN(ppUniqueParts, iPart);
		{
			const char *pPart = ppUniqueParts[iPart];
			const char *pResult = NULL;

			if (iSuccesses > iPart)
			{
				pResult = "SUCCESS";
			}
			else if (iSuccesses == iPart)
			{
				pResult = StaticDefineInt_FastIntToString(AccountKeyValueResultEnum, eResult);
			}
			else
			{
				pResult = "N/A";
			}

			estrConcatf(&pAlertStr, " (account: %s, key: %s, result: %s)", pAccount->accountName, pPart, pResult);
		}
		EARRAY_FOREACH_END;

		ErrorOrAlert("ACCOUNTSERVER_COMMIT_CHAIN_ERROR", "Key chain commit failed at least one step.%s", pAlertStr);

		if (iSuccesses > 0 && iSuccesses < eaSize(&ppUniqueParts))
		{
			CRITICAL_NETOPS_ALERT("ACCOUNTSERVER_COMMIT_CHAIN_PARTIAL", "Key chain commit partially failed!%s", pAlertStr);
		}
	}

	SendKeyValueToAllProxies(pAccount, pKey);
	eaDestroyEx(&ppUniqueParts, NULL);
	eaDestroy(&ppPairs);

	return eResult;
}

AccountKeyValueResult AccountKeyValue_FinalizeEx(const AccountInfo *pAccount, const char *pKey, const char *pPassword, bool bCommit)
{
	const AccountKeyValuePair *pPair = akvGetPair(pAccount, pKey);
	AccountKeyValueResult eResult = AKV_FAILURE;

	if (VirtualCurrency_IsChain(pKey))
	{
		return akvFinalizeChain(pAccount, pKey, pPassword, bCommit);
	}

	eResult = akvPairCanBeFinalized(pAccount, pKey, pPassword, pPair);
	
	if (eResult != AKV_SUCCESS)
	{
		return eResult;
	}

	if (bCommit)
	{
		akvCommit(pAccount, pKey, pPair);
	}
	else
	{
		akvRollback(pAccount, pKey);
	}

	return AKV_SUCCESS;
}

AccountKeyValueResult AccountKeyValue_Finalize(const char *pPassword, bool bCommit)
{
	AccountKeyValueResult eResult = AKV_FAILURE;
	const AccountInfo **ppAccounts = NULL;
	const char **ppKeys = NULL;
	char **ppPasswordParts = NULL;
	int iSuccesses = 0;

	ppPasswordParts = akvDivideLockPassword(pPassword);

	if (!ppPasswordParts || !eaSize(&ppPasswordParts))
	{
		return AKV_INVALID_LOCK;
	}

	EARRAY_FOREACH_BEGIN(ppPasswordParts, iPart);
	{
		const AccountInfo *pAccount = NULL;
		const char *pKey = NULL;
		const char *pPart = ppPasswordParts[iPart];
		
		eResult = akvDecodeLockPassword(pPart, &pAccount, &pKey);

		if (eResult != AKV_SUCCESS)
		{
			break;
		}

		eaPush(&ppAccounts, pAccount);
		eaPush(&ppKeys, pKey);
	}
	EARRAY_FOREACH_END;

	if (eResult == AKV_SUCCESS)
	{
		EARRAY_FOREACH_BEGIN(ppPasswordParts, iPart);
		{
			const AccountInfo *pAccount = ppAccounts[iPart];
			const char *pKey = ppKeys[iPart];
			const char *pPart = ppPasswordParts[iPart];

			eResult = AccountKeyValue_FinalizeEx(pAccount, pKey, pPart, bCommit);

			if (eResult != AKV_SUCCESS)
			{
				break;
			}

			++iSuccesses;
		}
		EARRAY_FOREACH_END;
	}

	if (bCommit && eResult != AKV_SUCCESS)
	{
		char *pAlertStr = NULL;
		
		EARRAY_FOREACH_BEGIN(ppPasswordParts, iPart);
		{
			const AccountInfo *pAccount = NULL;
			const char *pKey = NULL;
			const char *pPart = ppPasswordParts[iPart];

			if (iPart < eaSize(&ppAccounts))
			{
				pAccount = ppAccounts[iPart];
				pKey = ppKeys[iPart];
			}

			if (pAccount)
			{
				const char *pResult = NULL;

				if (iSuccesses > iPart)
				{
					pResult = "SUCCESS";
				}
				else if (iSuccesses == iPart)
				{
					pResult = StaticDefineInt_FastIntToString(AccountKeyValueResultEnum, eResult);
				}
				else
				{
					pResult = "N/A";
				}

				estrConcatf(&pAlertStr, " (account: %s, key: %s, result: %s)", pAccount->accountName, pKey, pResult);
			}
			else if (iPart == eaSize(&ppAccounts))
			{
				estrConcatf(&pAlertStr, " (invalid part: %s)", pPart);
			}
			else
			{
				estrConcatf(&pAlertStr, " (undecoded part: %s)", pPart);
			}
		}
		EARRAY_FOREACH_END;

		ErrorOrAlert("ACCOUNTSERVER_COMMIT_ERROR", "Key commit failed at least one step.%s", pAlertStr);

		if (iSuccesses > 0 && iSuccesses < eaSize(&ppPasswordParts))
		{
			CRITICAL_NETOPS_ALERT("ACCOUNTSERVER_COMMIT_PARTIAL", "Key commit partially failed!%s", pAlertStr);
		}

		estrDestroy(&pAlertStr);
	}

	eaDestroyEString(&ppPasswordParts);
	return eResult;
}

AccountKeyValueResult AccountKeyValue_RemoveEx(const AccountInfo *pAccount, const char *pKey, bool bForce)
{
	const AccountKeyValuePair *pPair = NULL;
	
	if (VirtualCurrency_IsChain(pKey))
	{
		// Chains can't be removed directly
		return AKV_INVALID_KEY;
	}

	pPair = akvGetPair(pAccount, pKey);

	if (!pPair)
	{
		return AKV_NONEXISTANT;
	}

	if (akvPairIsLocked(pAccount, pKey, pPair) && !bForce)
	{
		return AKV_LOCKED;
	}

	akvRemove(pAccount, pKey, pPair);
	return AKV_SUCCESS;
}

AccountKeyValueResult AccountKeyValue_UnlockEx(const AccountInfo *pAccount, const char *pKey)
{
	const AccountKeyValuePair *pPair = NULL;

	if (VirtualCurrency_IsChain(pKey))
	{
		// Chains can't be unlocked directly
		return AKV_INVALID_KEY;
	}

	pPair = akvGetPair(pAccount, pKey);

	if (!pPair)
	{
		return AKV_NONEXISTANT;
	}

	if (!pPair->lockData)
	{
		return AKV_NOT_LOCKED;
	}

	akvUnlock(pAccount, pKey, false);
	return AKV_SUCCESS;
}

STRING_EARRAY AccountKeyValue_GetAccountKeyListEx(const AccountInfo *pAccount)
{
	STRING_EARRAY eaKeys = NULL;

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(pAccount->ppKeyValuePairs, iCurPair, iNumPairs);
	{
		eaPush(&eaKeys, strdup(pAccount->ppKeyValuePairs[iCurPair]->key));
	}
	EARRAY_FOREACH_END;

	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_VIRTUALCURRENCY, pContainer);
	{
		const VirtualCurrency *pCurrency = pContainer->containerData;

		if (!devassert(pCurrency) || !pCurrency->bIsChain) continue;

		eaPush(&eaKeys, strdup(pCurrency->pName));
	}
	CONTAINER_FOREACH_END;

	PERFINFO_AUTO_STOP();

	return eaKeys;
}