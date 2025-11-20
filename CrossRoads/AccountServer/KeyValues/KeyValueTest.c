#include "AccountManagement.h"
#include "KeyValues.h"
#include "stdtypes.h"

AUTO_COMMAND ACMD_NAME(KVGet);
S64 KeyValueTest_Get(const char *pAccountName, const char *pKey)
{
	AccountInfo *pAccount = findAccountByName(pAccountName);
	S64 iValue = -1;
	AccountKeyValueResult eResult = AKV_FAILURE;

	if (!pAccount) return iValue;

	eResult = AccountKeyValue_GetLocked(pAccount, pKey, &iValue);
	return iValue;
}

AUTO_COMMAND ACMD_NAME(KVLock);
char *KeyValueTest_Lock(const char *pAccountName, const char *pKey)
{
	AccountInfo *pAccount = findAccountByName(pAccountName);
	AccountKeyValueResult eResult = AKV_FAILURE;
	char *pLock = NULL;

	if (!pAccount) return pLock;

	eResult = AccountKeyValue_Lock(pAccount, pKey, &pLock);
	return pLock;
}

AUTO_COMMAND ACMD_NAME(KVLockAgain);
bool KeyValueTest_LockAgain(const char *pAccountName, const char *pKey, char *pLock)
{
	AccountInfo *pAccount = findAccountByName(pAccountName);
	AccountKeyValueResult eResult = AKV_FAILURE;

	if (!pAccount) return false;

	eResult = AccountKeyValue_LockAgain(pAccount, pKey, &pLock);
	return eResult == AKV_SUCCESS;
}

AUTO_COMMAND ACMD_NAME(KVSetOnce);
char *KeyValueTest_SetOnce(const char *pAccountName, const char *pKey, S64 iValue)
{
	AccountInfo *pAccount = findAccountByName(pAccountName);
	AccountKeyValueResult eResult = AKV_FAILURE;
	char *pLock = NULL;

	if (!pAccount) return pLock;

	eResult = AccountKeyValue_SetOnce(pAccount, pKey, iValue, &pLock);
	return pLock;
}

AUTO_COMMAND ACMD_NAME(KVSet);
bool KeyValueTest_Set(const char *pAccountName, const char *pKey, S64 iValue, char *pLock)
{
	AccountInfo *pAccount = findAccountByName(pAccountName);
	AccountKeyValueResult eResult = AKV_FAILURE;

	if (!pAccount) return false;

	eResult = AccountKeyValue_Set(pAccount, pKey, iValue, &pLock);
	return eResult == AKV_SUCCESS;
}

AUTO_COMMAND ACMD_NAME(KVChangeOnce);
char *KeyValueTest_ChangeOnce(const char *pAccountName, const char *pKey, S64 iValue)
{
	AccountInfo *pAccount = findAccountByName(pAccountName);
	AccountKeyValueResult eResult = AKV_FAILURE;
	char *pLock = NULL;

	if (!pAccount) return pLock;

	eResult = AccountKeyValue_ChangeOnce(pAccount, pKey, iValue, &pLock);
	return pLock;
}

AUTO_COMMAND ACMD_NAME(KVChange);
bool KeyValueTest_Change(const char *pAccountName, const char *pKey, S64 iValue, char *pLock)
{
	AccountInfo *pAccount = findAccountByName(pAccountName);
	AccountKeyValueResult eResult = AKV_FAILURE;

	if (!pAccount) return false;

	eResult = AccountKeyValue_Change(pAccount, pKey, iValue, &pLock);
	return eResult == AKV_SUCCESS;
}

AUTO_COMMAND ACMD_NAME(KVMoveOnce);
char *KeyValueTest_MoveOnce(const char *pAccountNameFrom, const char *pKeyFrom, const char *pAccountNameTo, const char *pKeyTo, S64 iValue)
{
	AccountInfo *pAccountFrom = findAccountByName(pAccountNameFrom);
	AccountInfo *pAccountTo = findAccountByName(pAccountNameTo);
	char *pLock = NULL;
	AccountKeyValueResult eResult = AKV_FAILURE;

	if (!pAccountFrom || !pAccountTo) return pLock;

	eResult = AccountKeyValue_MoveOnce(pAccountFrom, pKeyFrom, pAccountTo, pKeyTo, iValue, &pLock);
	return pLock;
}

AUTO_COMMAND ACMD_NAME(KVMove);
bool KeyValueTest_Move(const char *pAccountNameFrom, const char *pKeyFrom, const char *pAccountNameTo, const char *pKeyTo, S64 iValue, char *pLock)
{
	AccountInfo *pAccountFrom = findAccountByName(pAccountNameFrom);
	AccountInfo *pAccountTo = findAccountByName(pAccountNameTo);
	AccountKeyValueResult eResult = AKV_FAILURE;

	if (!pAccountFrom || !pAccountTo) return false;

	eResult = AccountKeyValue_Move(pAccountFrom, pKeyFrom, pAccountTo, pKeyTo, iValue, &pLock);
	return eResult == AKV_SUCCESS;
}

AUTO_COMMAND ACMD_NAME(KVCommit);
bool KeyValueTest_Commit(const char *pAccountName, const char *pKey, const char *pPassword)
{
	AccountInfo *pAccount = findAccountByName(pAccountName);
	AccountKeyValueResult eResult = AKV_FAILURE;

	if (!pAccount) return false;

	eResult = AccountKeyValue_Commit(pAccount, pKey, pPassword);
	return eResult == AKV_SUCCESS;
}

AUTO_COMMAND ACMD_NAME(KVRollback);
bool KeyValueTest_Rollback(const char *pAccountName, const char *pKey, const char *pPassword)
{
	AccountInfo *pAccount = findAccountByName(pAccountName);
	AccountKeyValueResult eResult = AKV_FAILURE;

	if (!pAccount) return false;

	eResult = AccountKeyValue_Rollback(pAccount, pKey, pPassword);
	return eResult == AKV_SUCCESS;
}