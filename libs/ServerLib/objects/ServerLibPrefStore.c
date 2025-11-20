#include "ServerLibPrefStore.h"
#include "serverLibPrefStore_h_ast.h"

#include "objSchema.h"
#include "transactionOutcomes.h"
#include "textparser.h"
#include "stringCache.h"
#include "ServerLibPrefStore_c_ast.h"
#include "autogen/ServerLib_autotransactions_autogen_wrappers.h"
#include "objTransactions.h"
#include "crypt.h"
#include "stringUtil.h"

//register native schema everywhere except object DB
AUTO_RUN_LATE;
void ServerLibPrefStoreInit(void)
{
	if (!IsThisObjectDB())
	{
		objRegisterNativeSchema(GLOBALTYPE_PREFSTORE, parse_PrefStore,
			NULL, NULL, NULL, NULL, NULL);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pStore, ".Ppprefs");
enumTransactionOutcome PrefStore_tr_SetStringVal(ATR_ARGS, NOCONST(PrefStore) *pStore, char *key, char *val)
{
	char *pEncodedString = NULL;
	int iLen = (int)strlen(val);
	int i = -1;

	estrPrintf(&pEncodedString, "%d ", iLen);
	estrBase64Encode(&pEncodedString, val, iLen);
	
	i = eaIndexedFindUsingString(&pStore->ppPrefs, key);

	if (i > -1)
	{
		if (pStore->ppPrefs[i]->stringVal) free (pStore->ppPrefs[i]->stringVal);
		pStore->ppPrefs[i]->stringVal = strdup(pEncodedString);
	}
	else
	{
		NOCONST(PrefKV) *kv = StructCreateNoConst(parse_PrefKV);
		kv->name = allocAddString(key);
		kv->stringVal = strdup(pEncodedString);
		eaIndexedAdd(&pStore->ppPrefs, kv);
	}

	estrDestroy(&pEncodedString);
	TRANSACTION_RETURN_SUCCESS("Pref[%s] = %s", key, val);
}


AUTO_STRUCT;
typedef struct PrefStoreLocalCache
{
	char *pKey;
	char *pString;
	U32 iVal;
	PrefStoreCallbackFunc pCB; NO_AST
	void *pUserData; NO_AST
} PrefStoreLocalCache;
	


static void FailPrefStoreCache(PrefStoreLocalCache *pCache)
{
	if (pCache->pCB)
	{
		pCache->pCB(false, NULL, pCache->pUserData);
	}

	StructDestroy(parse_PrefStoreLocalCache, pCache);
}

static void PrefStore_SetString_CB2(TransactionReturnVal *returnVal, PrefStoreLocalCache *pCache)
{
	if (pCache->pCB)
	{
		pCache->pCB(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, NULL, pCache->pUserData);
	}

	StructDestroy(parse_PrefStoreLocalCache, pCache);
}

static void PrefStore_SetString_CB1(TransactionReturnVal *returnVal, PrefStoreLocalCache *pCache)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		AutoTrans_PrefStore_tr_SetStringVal(objCreateManagedReturnVal(PrefStore_SetString_CB2, pCache),
			GetAppGlobalType(), GLOBALTYPE_PREFSTORE, 1, pCache->pKey, pCache->pString);
	}
	else
	{
		FailPrefStoreCache(pCache);
	}
}

void PrefStore_SetString(char *pKey, char *pString, PrefStoreCallbackFunc pCB, void *pUserData)
{
	if (!objLocalManager())
	{
		if (pCB)
		{
			pCB(false, NULL, pUserData);
		}
	}
	else
	{
		TransactionRequest *request = objCreateTransactionRequest();
		PrefStoreLocalCache *pCache = StructCreate(parse_PrefStoreLocalCache);
		pCache->pKey = strdup(pKey);
		pCache->pString = strdup(pString);
		pCache->pCB = pCB;
		pCache->pUserData = pUserData;

		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
			"VerifyContainer containerIDVar PrefStore 1");
		objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
			objCreateManagedReturnVal(PrefStore_SetString_CB1, pCache), "EnsurePrefStoreExists", request);
		objDestroyTransactionRequest(request);
	}
}






AUTO_TRANSACTION
ATR_LOCKS(pStore, ".Ppprefs");
enumTransactionOutcome PrefStore_tr_GetStringVal(ATR_ARGS, NOCONST(PrefStore) *pStore, char *key)
{
	int i = -1;


	i = eaIndexedFindUsingString(&pStore->ppPrefs, key);

	if (i != -1)
	{
		TRANSACTION_RETURN_SUCCESS("%s", pStore->ppPrefs[i]->stringVal);
	}

	TRANSACTION_RETURN_FAILURE("couldn't find it");
}



static void PrefStore_GetString_CB2(TransactionReturnVal *returnVal, PrefStoreLocalCache *pCache)
{
	char *pDecoded = NULL;
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		char *pReturnStringEncoded = objAutoTransactionGetResult(returnVal);
		char *pFirstSpace = strchr(pReturnStringEncoded, ' ');
		int iLen;

		if (!pFirstSpace)
		{
			FailPrefStoreCache(pCache);
			return;
		}

			
		iLen = atoi(pReturnStringEncoded);
			
		estrSetSize(&pDecoded, iLen);
		decodeBase64String(pFirstSpace + 1, strlen(pFirstSpace + 1), pDecoded, iLen);
		if (pCache->pCB)
		{
			pCache->pCB(true, pDecoded, pCache->pUserData);
		}		
		estrDestroy(&pDecoded);
		

	}
	else
	{
		FailPrefStoreCache(pCache);
		return;
	}

	StructDestroy(parse_PrefStoreLocalCache, pCache);
}

static void PrefStore_GetString_CB1(TransactionReturnVal *returnVal, PrefStoreLocalCache *pCache)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		AutoTrans_PrefStore_tr_GetStringVal(objCreateManagedReturnVal(PrefStore_GetString_CB2, pCache),
			GetAppGlobalType(), GLOBALTYPE_PREFSTORE, 1, pCache->pKey);
	}
	else
	{
		FailPrefStoreCache(pCache);
	}
}

void PrefStore_GetString(char *pKey, PrefStoreCallbackFunc pCB, void *pUserData)
{
	if (!objLocalManager())
	{
		if (pCB)
		{
			pCB(false, NULL, pUserData);
		}
	}
	else
	{
		TransactionRequest *request = objCreateTransactionRequest();
		PrefStoreLocalCache *pCache = StructCreate(parse_PrefStoreLocalCache);
		pCache->pKey = strdup(pKey);
		pCache->pCB = pCB;
		pCache->pUserData = pUserData;

		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
			"VerifyContainer containerIDVar PrefStore 1");
		objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
			objCreateManagedReturnVal(PrefStore_GetString_CB1, pCache), "EnsurePrefStoreExists", request);
		objDestroyTransactionRequest(request);
	}
}







AUTO_TRANSACTION
ATR_LOCKS(pStore, ".Ppprefs");
enumTransactionOutcome PrefStore_tr_AtomicAddAndGetVal(ATR_ARGS, NOCONST(PrefStore) *pStore, char *key, int iAmount)
{
	int i = -1;
	i = eaIndexedFindUsingString(&pStore->ppPrefs, key);

	if (i != -1)
	{
		U32 iCurVal;
		char *pCurString = pStore->ppPrefs[i]->stringVal;
		if (!pCurString)
		{
			TRANSACTION_RETURN_FAILURE("no string set");
		}

		if (!StringToUint_Paranoid(pCurString, &iCurVal))
		{
			TRANSACTION_RETURN_FAILURE("corrupt int");
		}

		SAFE_FREE(pStore->ppPrefs[i]->stringVal);
		iCurVal += iAmount;

		pStore->ppPrefs[i]->stringVal = strdupf("%u", iCurVal);
		TRANSACTION_RETURN_SUCCESS("%s", pStore->ppPrefs[i]->stringVal);
	}
	else
	{
		NOCONST(PrefKV) *kv = StructCreateNoConst(parse_PrefKV);
		kv->name = allocAddString(key);
		kv->stringVal = strdupf("%u", iAmount);
		eaIndexedAdd(&pStore->ppPrefs, kv);

		TRANSACTION_RETURN_SUCCESS("%s", kv->stringVal);
	}
}



static void PrefStore_AtomicAddAndGet_CB2(TransactionReturnVal *returnVal, PrefStoreLocalCache *pCache)
{
	char *pDecoded = NULL;
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if (pCache->pCB)
		{
			pCache->pCB(true, objAutoTransactionGetResult(returnVal), pCache->pUserData);
		}		
	}
	else
	{
		FailPrefStoreCache(pCache);
		return;
	}

	StructDestroy(parse_PrefStoreLocalCache, pCache);
}

static void PrefStore_AtomicAddAndGet_CB1(TransactionReturnVal *returnVal, PrefStoreLocalCache *pCache)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		AutoTrans_PrefStore_tr_AtomicAddAndGetVal(objCreateManagedReturnVal(PrefStore_AtomicAddAndGet_CB2, pCache),
			GetAppGlobalType(), GLOBALTYPE_PREFSTORE, 1, pCache->pKey, pCache->iVal);
	}
	else
	{
		FailPrefStoreCache(pCache);
	}
}

void PrefStore_AtomicAddAndGet(char *pKey, U32 iVal, PrefStoreCallbackFunc pCB, void *pUserData)
{
	if (!objLocalManager())
	{
		if (pCB)
		{
			pCB(false, NULL, pUserData);
		}
	}
	else
	{
		TransactionRequest *request = objCreateTransactionRequest();
		PrefStoreLocalCache *pCache = StructCreate(parse_PrefStoreLocalCache);
		pCache->pKey = strdup(pKey);
		pCache->iVal = iVal;
		pCache->pCB = pCB;
		pCache->pUserData = pUserData;

		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
			"VerifyContainer containerIDVar PrefStore 1");
		objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
			objCreateManagedReturnVal(PrefStore_AtomicAddAndGet_CB1, pCache), "EnsurePrefStoreExists", request);
		objDestroyTransactionRequest(request);
	}
}



void TestPrefStore_CB(bool bSucceeded, char *pString, void *pUserData)
{
	if (bSucceeded)
	{
		printf("SUCCEEDED: %s\n", pString);
	}
	else
	{
		printf("FAILED\n");
	}
}



AUTO_COMMAND;
void TestPrefStore_SetString(char *pKey, char *pString)
{
	PrefStore_SetString(pKey, pString, TestPrefStore_CB, NULL);
}

AUTO_COMMAND;
void TestPrefStore_GetString(char *pKey)
{
	PrefStore_GetString(pKey, TestPrefStore_CB, NULL);
}

AUTO_COMMAND;
void TestPrefStore_Add(char *pKey, U32 iVal)
{
	PrefStore_AtomicAddAndGet(pKey, iVal, TestPrefStore_CB, NULL);
}



#include "ServerLibPrefStore_h_ast.c"
#include "ServerLibPrefStore_c_ast.c"
