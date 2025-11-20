#pragma once

#define USE_NEW_KVCODE 1

typedef struct AccountInfo AccountInfo;

typedef enum AccountKeyValueResult AccountKeyValueResult;

AccountKeyValueResult AccountKeyValue_GetEx(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, bool bAllowLock, SA_PARAM_OP_VALID S64 *piValue);
bool AccountKeyValue_IsLockedEx(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey);
AccountKeyValueResult AccountKeyValue_LockEx(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, S64 iValue, bool bModify, SA_PRE_NN_OP_STR SA_POST_NN_NN_STR char **ppInOutPassword, bool bAllowRelock);
AccountKeyValueResult AccountKeyValue_SetEx(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, S64 iValue, bool bModify, SA_PRE_NN_OP_STR SA_POST_NN_NN_STR char **ppInOutPassword, bool bAllowRelock);

AccountKeyValueResult AccountKeyValue_MoveEx(
	SA_PARAM_NN_VALID const AccountInfo *pSrcAccount,
	SA_PARAM_NN_STR const char *pSrcKey,
	SA_PARAM_NN_VALID const AccountInfo *pDestAccount,
	SA_PARAM_NN_STR const char *pDestKey,
	S64 iValue,
	SA_PRE_NN_OP_STR SA_POST_NN_NN_STR char **ppInOutPassword,
	bool bAllowRelock);

AccountKeyValueResult AccountKeyValue_FinalizeEx(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_NN_STR const char *pPassword, bool bCommit);
AccountKeyValueResult AccountKeyValue_Finalize(SA_PARAM_NN_STR const char *pPassword, bool bCommit);

AccountKeyValueResult AccountKeyValue_RemoveEx(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, bool bForce);
AccountKeyValueResult AccountKeyValue_UnlockEx(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey);

SA_RET_OP_OP_STR STRING_EARRAY AccountKeyValue_GetAccountKeyListEx(SA_PARAM_NN_VALID const AccountInfo *pAccount);

void AccountKeyValue_MigrateLegacyStorage(SA_PARAM_NN_VALID const AccountInfo *account);

#define AccountKeyValue_Get(pAccount, pKey, piValue) AccountKeyValue_GetEx(pAccount, pKey, false, piValue)
#define AccountKeyValue_GetLocked(pAccount, pKey, piValue) AccountKeyValue_GetEx(pAccount, pKey, true, piValue)
#define AccountKeyValue_IsLocked(pAccount, pKey) AccountKeyValue_IsLockedEx(pAccount, pKey)

#define AccountKeyValue_LockOnce(pAccount, pKey, ppInOutPassword) AccountKeyValue_LockEx(pAccount, pKey, 0, true, ppInOutPassword, false)
#define AccountKeyValue_Lock(pAccount, pKey, ppInOutPassword) AccountKeyValue_LockEx(pAccount, pKey, 0, true, ppInOutPassword, true)
#define AccountKeyValue_LockAgain(pAccount, pKey, ppInOutPassword) AccountKeyValue_LockEx(pAccount, pKey, 0, true, ppInOutPassword, true)

#define AccountKeyValue_SetOnce(pAccount, pKey, iValue, ppInOutPassword) AccountKeyValue_SetEx(pAccount, pKey, iValue, false, ppInOutPassword, false)
#define AccountKeyValue_Set(pAccount, pKey, iValue, ppInOutPassword) AccountKeyValue_SetEx(pAccount, pKey, iValue, false, ppInOutPassword, true)

#define AccountKeyValue_ChangeOnce(pAccount, pKey, iValue, ppInOutPassword) AccountKeyValue_SetEx(pAccount, pKey, iValue, true, ppInOutPassword, false)
#define AccountKeyValue_Change(pAccount, pKey, iValue, ppInOutPassword) AccountKeyValue_SetEx(pAccount, pKey, iValue, true, ppInOutPassword, true)

#define AccountKeyValue_MoveOnce(pSrcAccount, pSrcKey, pDestAccount, pDestKey, iValue, ppInOutPassword) \
	AccountKeyValue_MoveEx(pSrcAccount, pSrcKey, pDestAccount, pDestKey, iValue, ppInOutPassword, false)
#define AccountKeyValue_Move(pSrcAccount, pSrcKey, pDestAccount, pDestKey, iValue, ppInOutPassword) \
	AccountKeyValue_MoveEx(pSrcAccount, pSrcKey, pDestAccount, pDestKey, iValue, ppInOutPassword, true)

#define AccountKeyValue_Commit(pAccount, pKey, pPassword) AccountKeyValue_Finalize(pPassword, true)
#define AccountKeyValue_Rollback(pAccount, pKey, pPassword) AccountKeyValue_Finalize(pPassword, false)

#define AccountKeyValue_Remove(pAccount, pKey) AccountKeyValue_RemoveEx(pAccount, pKey, false)
#define AccountKeyValue_RemoveForce(pAccount, pKey) AccountKeyValue_RemoveEx(pAccount, pKey, true)

#define AccountKeyValue_Unlock(pAccount, pKey) AccountKeyValue_UnlockEx(pAccount, pKey)

#define AccountKeyValue_GetAccountKeyList(pAccount) AccountKeyValue_GetAccountKeyListEx(pAccount)
#define AccountKeyValue_DestroyAccountKeyList(eaKeys) eaDestroyEx(eaKeys, NULL)