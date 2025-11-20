#include "AccountProxyCommon.h"
#include "gslAccountProxy.h"
#include "accountnet.h"
#include "objTransactions.h"
#include "gslSendToClient.h"
#include "StoredCredentialsCommon.h"
#include "Player.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

typedef struct gslTwitterCBData{
	void *entref;
	char token[1024];
	char secret[1024];
	char service[64];
	ContainerID userLockID;
	ContainerID tokenLockID;
} gslTwitterCBData;

// IMPORTANT NOTE VAS 111212
// String key-values on the AS are deprecated - it is no longer possible to store the user's credentials
// in the Account Server. If you need to reconsitute this flow, you'll need to do something entirely different.

AUTO_TRANSACTION
ATR_LOCKS(pSecret, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype")
ATR_LOCKS(pToken, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype")
ATR_LOCKS(pUser, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype");
enumTransactionOutcome StoredCredentials_tr_FinishLock(ATR_ARGS,
													   NOCONST(AccountProxyLockContainer) *pSecret,
													   NOCONST(AccountProxyLockContainer) *pToken,
													   NOCONST(AccountProxyLockContainer) *pUser,
													   U32 accountID, const char *service)
{
	char *key=NULL;
	bool user_success, token_success, secret_success;

	StoredCredentialsUserKey(&key, service);
	user_success = APFinalizeKeyValue(pUser, accountID, key, APRESULT_COMMIT, TransLogType_Other);

	StoredCredentialsTokenKey(&key, service);
	token_success = APFinalizeKeyValue(pToken, accountID, key, APRESULT_COMMIT, TransLogType_Other);
	
	StoredCredentialsSecretKey(&key, service);
	secret_success = APFinalizeKeyValue(pSecret, accountID, key, APRESULT_COMMIT, TransLogType_Other);

	estrDestroy(&key);

	return (user_success && token_success && secret_success) ? TRANSACTION_OUTCOME_SUCCESS : TRANSACTION_OUTCOME_FAILURE;
}

static void FinishKeyValueSet(SA_PARAM_NN_VALID TransactionReturnVal *pReturn, SA_PARAM_OP_VALID void *userData)
{
	switch (pReturn->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData), AKV_FAILURE_MESSAGE);
	//xcase TRANSACTION_OUTCOME_SUCCESS:
	//	gslSendPrintf(entFromEntityRef((EntityRef)(intptr_t)userData), AKV_SUCCESS_MESSAGE);
	}
}

static void setSecretCB(AccountKeyValueResult result, U32 accountID, SA_PARAM_NN_STR const char *key, ContainerID lockID, SA_PARAM_NN_VALID gslTwitterCBData *userData)
{
	switch (result)
	{
	case AKV_SUCCESS:
	{
		AutoTrans_StoredCredentials_tr_FinishLock(objCreateManagedReturnVal(FinishKeyValueSet, userData->entref), GLOBALTYPE_GAMESERVER,
			GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, lockID,
			GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, userData->tokenLockID,
			GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, userData->userLockID,
			accountID, userData->service);
	}
	xcase AKV_INVALID_KEY:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData->entref), AKV_INVALID_KEY_MESSAGE);
	xcase AKV_NONEXISTANT:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData->entref), AKV_NONEXISTANT_MESSAGE);
	xcase AKV_INVALID_RANGE:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData->entref), AKV_INVALID_RANGE_MESSAGE);
	xcase AKV_LOCKED:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData->entref), AKV_LOCKED_MESSAGE);
	xdefault:
	case AKV_FAILURE:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData->entref), AKV_FAILURE_MESSAGE);
		}
	}

static void setTokenCB(AccountKeyValueResult result, U32 accountID, SA_PARAM_NN_STR const char *key, ContainerID lockID, SA_PARAM_NN_VALID gslTwitterCBData *userData)
{
	switch (result)
	{
	case AKV_SUCCESS:
	{
		char *secret_key=NULL;
		userData->tokenLockID = lockID;
		StoredCredentialsSecretKey(&secret_key, userData->service);
//		DEPRECATED
//		APSetKeyValueStringUID(accountID, secret_key, userData->secret, setSecretCB, userData);
		estrDestroy(&secret_key);
		return;
	}
	xcase AKV_INVALID_KEY:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData->entref), AKV_INVALID_KEY_MESSAGE);
	xcase AKV_NONEXISTANT:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData->entref), AKV_NONEXISTANT_MESSAGE);
	xcase AKV_INVALID_RANGE:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData->entref), AKV_INVALID_RANGE_MESSAGE);
	xcase AKV_LOCKED:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData->entref), AKV_LOCKED_MESSAGE);
	xdefault:
	case AKV_FAILURE:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData->entref), AKV_FAILURE_MESSAGE);
	}
	free(userData);
}

static void setUserCB(AccountKeyValueResult result, U32 accountID, SA_PARAM_NN_STR const char *key, ContainerID lockID, SA_PARAM_NN_VALID gslTwitterCBData *userData)
{
	switch (result)
	{
	case AKV_SUCCESS:
		{
			char *token_key=NULL;
			userData->userLockID = lockID;
			StoredCredentialsTokenKey(&token_key, userData->service);
//			DEPRECATED
//			APSetKeyValueStringUID(accountID, token_key, userData->token, setTokenCB, userData);
			estrDestroy(&token_key);
			return;
		}
		xcase AKV_INVALID_KEY:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData->entref), AKV_INVALID_KEY_MESSAGE);
		xcase AKV_NONEXISTANT:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData->entref), AKV_NONEXISTANT_MESSAGE);
		xcase AKV_INVALID_RANGE:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData->entref), AKV_INVALID_RANGE_MESSAGE);
		xcase AKV_LOCKED:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData->entref), AKV_LOCKED_MESSAGE);
xdefault:
	case AKV_FAILURE:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData->entref), AKV_FAILURE_MESSAGE);
	}
	free(userData);
}

// Store a user's token and secret to the account server.
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gslStoreCredentials(Entity *pEntity, const char *service, const char *user, const char *token, const char *secret)
{
	char *key=NULL;
	gslTwitterCBData *cbdata = calloc(1, sizeof(gslTwitterCBData));
	assert(service);
	StoredCredentialsUserKey(&key, service);
	cbdata->entref = (void*)((intptr_t)entGetRef(pEntity));
	strcpy(cbdata->token, NULL_TO_EMPTY(token));
	strcpy(cbdata->secret, NULL_TO_EMPTY(secret));
	strcpy(cbdata->service, service);
//	DEPRECATED
//	APSetKeyValueStringUID(pEntity->pPlayer->accountID, key, NULL_TO_EMPTY(user), setUserCB, cbdata);
	estrDestroy(&key);
}
