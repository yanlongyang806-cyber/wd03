/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslAccountProxy.h"
#include "AutoGen/gslAccountProxy_h_ast.h"
#include "gslSendToClient.h"

#include "AccountDataCache.h"
#include "AutoTransDefs.h"
#include "AccountProxyCommon.h"
#include "accountnet.h"
#include "cmdServerCharacter.h"
#include "Entity.h"
#include "EntityLib.h"
#include "fileutil.h"
#include "GameAccountDataCommon.h"
#include "GameServerLib.h"
#include "gslEntity.h"
#include "LoggedTransactions.h"
#include "gslMicroTransactions.h"
#include "gslTransactions.h"
#include "inventoryCommon.h"
#include "MicroTransactions.h"
#include "Money.h"
#include "Player.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "rand.h"
#include "ResourceInfo.h"
#include "SavedPetCommon.h"
#include "ServerLib.h"
#include "ShardCommon.h"
#include "StringCache.h"
#include "TimedCallback.h"
#include "WebRequestServer/wrContainerSubs.h"

#include "AutoGen/MicroTransactions_h_ast.h"

#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/ServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"

extern GameServerLibState gGSLState;

__forceinline U32 getAccountIDFromEntity(SA_PARAM_NN_VALID Entity *pEntity)
{
	Player *pPlayer = entGetPlayer(pEntity);

	if (!pPlayer)
	{
		ErrorOrAlert("ACCOUNTPROXY_GSL_NOT_PLAYER", "Attempting to get key values for an entity that is not a player.");
		return 0;
	}

	if (!pPlayer->accountID)
	{
		ErrorOrAlert("ACCOUNTPROXY_GSL_INVALID_ACCOUNT", "Attempting to get key values for a player that is missing their account ID.");
		return 0;
	}

	return pPlayer->accountID;
}

SA_RET_OP_VALID static Entity * SafeGetEntity(ContainerID uContainerID, U32 uAccountID)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uContainerID);
	// Sanity check to make sure I didn't screw something up
	// TODO: Remove this once we are sure it works. <NPK 2009-08-28>
	if (isDevelopmentMode() && pEntity)
	{
		Player *pPlayer;
		ANALYSIS_ASSUME(pEntity != NULL);
		pPlayer = entGetPlayer(pEntity);
		assert(pPlayer && pPlayer->accountID == uAccountID);
	}
	return pEntity;
}


static void FinishKeyValueSet(SA_PARAM_NN_VALID TransactionReturnVal *pReturn, SA_PARAM_OP_VALID void *userData)
{
	switch (pReturn->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData), AKV_FAILURE_MESSAGE);
	xcase TRANSACTION_OUTCOME_SUCCESS:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData), AKV_SUCCESS_MESSAGE);
	}
}

static void gslAPSetKeyValueCB(AccountKeyValueResult result, U32 accountID, SA_PARAM_NN_STR const char *key, ContainerID lockID, SA_PARAM_OP_VALID void *userData)
{
	switch (result)
	{
	case AKV_SUCCESS:
		AutoTrans_AccountProxy_tr_FinishLock(objCreateManagedReturnVal(FinishKeyValueSet, userData),
			GLOBALTYPE_GAMESERVER, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, lockID,
			accountID, key, TransLogType_Other);
	xcase AKV_INVALID_KEY:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData), AKV_INVALID_KEY_MESSAGE);
	xcase AKV_NONEXISTANT:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData), AKV_NONEXISTANT_MESSAGE);
	xcase AKV_INVALID_RANGE:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData), AKV_INVALID_RANGE_MESSAGE);
	xcase AKV_LOCKED:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData), AKV_LOCKED_MESSAGE);
	xdefault:
	case AKV_FAILURE:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData), AKV_FAILURE_MESSAGE);
	}
}

static void gslAPSimpleSetKeyValueCB(AccountKeyValueResult result, SA_PARAM_NN_STR const char *key, SA_PARAM_OP_VALID void *userData)
{
	switch (result)
	{
	xcase AKV_SUCCESS:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData), AKV_SUCCESS_MESSAGE);
	xcase AKV_INVALID_KEY:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData), AKV_INVALID_KEY_MESSAGE);
	xcase AKV_NONEXISTANT:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData), AKV_NONEXISTANT_MESSAGE);
	xcase AKV_INVALID_RANGE:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData), AKV_INVALID_RANGE_MESSAGE);
	xcase AKV_LOCKED:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData), AKV_LOCKED_MESSAGE);
	xdefault:
	case AKV_FAILURE:
		gslSendPrintf(entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData), AKV_FAILURE_MESSAGE);
	}
}

void gslAPSetKeyValueCmdNoCallback(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *key, S64 iVal)
{
	APSetKeyValueSimple(pEntity->pPlayer->accountID, key, iVal, false, NULL, NULL);
}

void gslAPSetKeyValueCmd(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *key, S64 iVal)
{
	APSetKeyValueSimple(pEntity->pPlayer->accountID, key, iVal, false, gslAPSimpleSetKeyValueCB, (void*)((intptr_t)entGetRef(pEntity)));
}

void gslAPChangeKeyValueCmdNoCallback(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *key, S64 iVal)
{
	APSetKeyValueSimple(pEntity->pPlayer->accountID, key, iVal, true, NULL, NULL);
}

void gslAPChangeKeyValueCmd(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *key, S64 iVal)
{
	APSetKeyValueSimple(pEntity->pPlayer->accountID, key, iVal, true, gslAPSimpleSetKeyValueCB, (void*)((intptr_t)entGetRef(pEntity)));
}


static void gslAPGetAccountIDFromDisplayNameCB(TransactionReturnVal *returnStruct, void *userData)
{
	Entity *pEntity = entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData);
	U32 uAccountID = 0;

	if (RemoteCommandCheck_aslAPCmdGetAccountIDFromDisplayName(returnStruct, &uAccountID) != TRANSACTION_OUTCOME_SUCCESS)
		return;

	gslSendPrintf(pEntity, "Account ID = %d", uAccountID);
}

void gslAPGetAccountIDFromDisplayNameCmd(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *pDisplayName)
{
	RemoteCommand_aslAPCmdGetAccountIDFromDisplayName(objCreateManagedReturnVal(gslAPGetAccountIDFromDisplayNameCB, (void*)((intptr_t)entGetRef(pEntity))), GLOBALTYPE_ACCOUNTPROXYSERVER, 0, pDisplayName);
}

void gslAPRequestAllKeyValues(SA_PARAM_NN_VALID Entity *pEntity)
{
	U32 uAccountID;
	U32 uPlayerID;

	PERFINFO_AUTO_START_FUNC();
	
	uAccountID = getAccountIDFromEntity(pEntity);
	uPlayerID = entGetContainerID(pEntity);

	if (uAccountID)
		RemoteCommand_aslAPCmdRequestAllKeyValues(GLOBALTYPE_ACCOUNTPROXYSERVER, 0, uAccountID, uPlayerID);
	
	PERFINFO_AUTO_STOP();
}

bool gslAPCacheGetKeyValue(Entity *pEntity, SA_PARAM_NN_STR const char *pKey, char **ppValue)
{
	Player *pPlayer = entGetPlayer(pEntity);
	if(pPlayer)
	{
		AccountProxyKeyValueInfo *pInfo = eaIndexedGetUsingString(&pPlayer->ppKeyValueCache, pKey);
		if(pInfo)
		{
			if(ppValue)
				*ppValue = pInfo->pValue;
			return true;
		}
	}

	if(ppValue)
		*ppValue = NULL;
	return false;
}


static void gslAPGetKeyValuesCB(TransactionReturnVal *returnStruct, void *userData)
{
	Entity *pEntity = entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData);
	AccountProxyKeyValueInfoList *list = NULL;

	if (RemoteCommandCheck_aslAPCmdGetAllKeyValues(returnStruct, &list) != TRANSACTION_OUTCOME_SUCCESS || !list)
		return;

	if (!eaSize(&list->ppList)) gslSendPrintf(pEntity, "No key values.");

	EARRAY_CONST_FOREACH_BEGIN(list->ppList, i, s);
	gslSendPrintf(pEntity, "%s = %s", list->ppList[i]->pKey, list->ppList[i]->pValue);
	EARRAY_FOREACH_END;

	StructDestroy(parse_AccountProxyKeyValueInfoList, list);
}

void gslAPGetKeyValuesCmd(SA_PARAM_NN_VALID Entity *pEntity)
{
	Player *pPlayer = entGetPlayer(pEntity);

	if (pPlayer)
	{
		RemoteCommand_aslAPCmdGetAllKeyValues(	objCreateManagedReturnVal(gslAPGetKeyValuesCB, 
			(void*)((intptr_t)entGetRef(pEntity))), 
			GLOBALTYPE_ACCOUNTPROXYSERVER, 0, 
			pPlayer->accountID, entGetContainerID(pEntity));
	}
}


typedef struct GetSubbedTimeHolder
{
	AccountGetSubbedTimeCallback callback;
	void *userData;
	U32 accountID;
} GetSubbedTimeHolder;

static void gslAPGetSubbedTimeCB(TransactionReturnVal *returnStruct, GetSubbedTimeHolder *pHolder)
{
	U32 uEstimatedTotalSeconds = 0;

	if (RemoteCommandCheck_aslAPCmdGetSubbedTime(returnStruct, &uEstimatedTotalSeconds) != TRANSACTION_OUTCOME_SUCCESS)
	{
		(*pHolder->callback)(0, pHolder->userData);
	}
	else
	{
		(*pHolder->callback)(uEstimatedTotalSeconds, pHolder->userData);
	}

	free(pHolder);
}

void gslAPGetSubbedTime(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_OP_STR const char *pProductInternalName, SA_PARAM_NN_VALID AccountGetSubbedTimeCallback pCallback, SA_PARAM_OP_VALID void *pUserData)
{
	GetSubbedTimeHolder *holder = calloc(sizeof(GetSubbedTimeHolder), 1);

	if (!devassert(holder)) return;
	if (!verify(pEntity)) return;
	if (!verify(pCallback)) return;

	holder->callback = pCallback;
	holder->userData = pUserData;

	if (pEntity->pPlayer && pEntity->pPlayer->accountID)
		RemoteCommand_aslAPCmdGetSubbedTime(objCreateManagedReturnVal(gslAPGetSubbedTimeCB, holder), GLOBALTYPE_ACCOUNTPROXYSERVER, 0, pEntity->pPlayer->accountID, pProductInternalName);
	else
		free(holder);
}


void gslAPGetSubbedTimeCmdCB(U32 uEstimatedTotalSeconds, SA_PARAM_OP_VALID void *userData)
{
	Entity *pEntity = entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData);

	if (pEntity)
		gslSendPrintf(pEntity, "%s", GetPrettyDurationString(uEstimatedTotalSeconds));
}

void gslAPGetSubbedTimeCmd(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_OP_STR const char *productInternalName)
{
	gslAPGetSubbedTime(pEntity, productInternalName, gslAPGetSubbedTimeCmdCB, (void*)((intptr_t)entGetRef(pEntity)));
}

void gslAPGetSubbedDaysCB(U32 uEstimatedTotalSeconds, SA_PARAM_OP_VALID void *userData)
{
	Entity *pEntity = entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData);
	U32 days = 0;

	if(pEntity)
	{
		days = (SECONDS_PER_DAY + uEstimatedTotalSeconds - 1) / SECONDS_PER_DAY;
		gslEntity_SetGADDaysSubscribed(pEntity, days);
	}
}

// get the number of subscribed days (rounded up).
void gslAPGetSubbedDaysCmd(Entity *pEntity)
{
	if(pEntity)
	{
		gslAPGetSubbedTime(pEntity, GetProductName(), gslAPGetSubbedDaysCB, (void*)((intptr_t)entGetRef(pEntity)));
	}
}

static MicroTransactionInfo *sMicroTransactionInfo = NULL;

static void gslAPCacheProductList(U32 uContainerID, U32 uAccountID, const MicroTransactionProductList *pMTList, void *pUserdata)
{
	const char *currency = microtrans_GetShardCurrencyExactName();

	if (!g_pMainMTCategory)
		return;

	PERFINFO_AUTO_START_FUNC();

	if (!sMicroTransactionInfo)
		sMicroTransactionInfo = StructCreate(parse_MicroTransactionInfo);

	StructReset(parse_MicroTransactionInfo, sMicroTransactionInfo);
	eaCopyStructs(&pMTList->ppProducts, &sMicroTransactionInfo->ppProducts, parse_MicroTransactionProduct);

	EARRAY_FOREACH_BEGIN(sMicroTransactionInfo->ppProducts, i);
	{
		MicroTransactionProduct *product = sMicroTransactionInfo->ppProducts[i];
		AccountProxyProduct *accountProductTrim = StructClone(parse_AccountProxyProduct, product->pProduct);
		const Money * price;

		// Trim the money
		eaDestroyStruct(&accountProductTrim->ppFullMoneyPrices, parse_Money);
		eaDestroyStruct(&accountProductTrim->ppMoneyPrices, parse_Money);
		price = findMoneyFromCurrency(product->pProduct->ppFullMoneyPrices, currency);
		if (price)
			eaPush(&accountProductTrim->ppFullMoneyPrices, StructClone(parse_Money, price));
		price = findMoneyFromCurrency(product->pProduct->ppMoneyPrices, currency);
		if (price)
			eaPush(&accountProductTrim->ppMoneyPrices, StructClone(parse_Money, price));
		StructDestroy(parse_AccountProxyProduct, product->pProduct);
		product->pDef = GET_REF(product->hDef);
		product->pProduct = accountProductTrim;
	}
	EARRAY_FOREACH_END;

	// Add the list of categories
	{
		RefDictIterator iter = {0};
		MicroTransactionCategory *curCategory = NULL;
		char *pchRefData;
		RefSystem_InitRefDictIterator(g_hMicroTransCategoryDict, &iter);
		while (pchRefData = RefSystem_GetNextReferenceDataFromIterator(&iter))
		{
			curCategory = RefSystem_ReferentFromString(g_hMicroTransCategoryDict, pchRefData);
			eaPush(&sMicroTransactionInfo->ppCategories, curCategory);
		}
	}

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_CATEGORY(GameServer_Debug);
void gslAPProductListUpdateCache(void)
{
	ADCStaleProducts();
	APGetMTCatalog(0, 0, gslAPCacheProductList, NULL);
}

AUTO_FIXUPFUNC;
TextParserResult fixupMicroTransactionInfo(MicroTransactionInfo*p, enumTextParserFixupType eFixupType, void *pExtraData)
{
	if (eFixupType == FIXUPTYPE_DESTRUCTOR)
	{
		eaDestroy(&p->ppCategories);		
	}
	return PARSERESULT_SUCCESS;
}

MicroTransactionInfo *gslAPGetProductList(void)
{
	return sMicroTransactionInfo;
}

///////////////////////////////////////////////////////
// Getting User-specific discounts and restrictions

static U32 suNextRequestID = 0;
static EARRAY_OF(WebDiscountRequest) seaDiscountRequests = NULL;

static WebDiscountRequest *gslAPFindDiscountRequest(U32 uAccountID)
{
	PERFINFO_AUTO_START_FUNC();
	EARRAY_FOREACH_BEGIN(seaDiscountRequests, i);
	{
		if (seaDiscountRequests[i]->uAccountID == uAccountID)
		{
			PERFINFO_AUTO_STOP();
			return seaDiscountRequests[i];
		}
	}
	EARRAY_FOREACH_END;
	PERFINFO_AUTO_STOP();

	return NULL;
}

SA_RET_OP_VALID static WebDiscountRequest *gslAPAddDiscountRequest(U32 uAccountID, U32 uCharacterID)
{
	WebDiscountRequest *request = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	request = StructCreate(parse_WebDiscountRequest);
	request->uAccountID = uAccountID;
	request->uCharacterID = uCharacterID;
	request->uTimeSent = timeSecondsSince2000();
	request->uRequestID = suNextRequestID++;
	if (!seaDiscountRequests)
		eaIndexedEnable(&seaDiscountRequests, parse_WebDiscountRequest);
	eaIndexedAdd(&seaDiscountRequests, request);
	PERFINFO_AUTO_STOP();

	return request;
}

WebDiscountRequest *gslAPGetDiscountRequest(U32 uRequestID)
{
	if (!seaDiscountRequests)
		return NULL;
	return eaIndexedGetUsingInt(&seaDiscountRequests, uRequestID);
}

static AccountProxyProductTrimmed *gslAPCopyClientDiscount(const AccountProxyProduct *product)
{
	AccountProxyProductTrimmed *productCopy;
	const Money *price = NULL;

	PERFINFO_AUTO_START_FUNC();
	if (!product)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}
	productCopy = StructCreate(parse_AccountProxyProductTrimmed);
	if (!devassert(productCopy))
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}
	productCopy->uID = product->uID;
	price = findMoneyFromCurrency(product->ppFullMoneyPrices, microtrans_GetShardCurrencyExactName());
	if (price)
		productCopy->pFullMoneyPrice = StructClone(parse_Money, price);
	price = findMoneyFromCurrency(product->ppMoneyPrices, microtrans_GetShardCurrencyExactName());
	if (price)
		productCopy->pMoneyPrice = StructClone(parse_Money, price);
	PERFINFO_AUTO_STOP();

	return productCopy;
}

void gslMTProducts_UserCheckProducts(Entity *pEnt, GameAccountData *pAccountData, WebDiscountRequest *data)
{
	char *error = NULL;

	PERFINFO_AUTO_START_FUNC();
	if ((pEnt && entGetAccountID(pEnt) != data->uAccountID) || (pAccountData && pAccountData->iAccountID != data->uAccountID))
	{
		data->eStatus = WDRSTATUS_FAILED;
		data->pError = strdup(WDR_ERROR_INVALID);
	}
	else if (pEnt && !wrCSub_VerifyVersion(pEnt))
	{
		data->eStatus = WDRSTATUS_FAILED;
		data->pError = strdup(WDR_ERROR_INVALID_VERSION);
	}
	else
	{
		MicroTransactionProductList *pMTList = NULL;
		APDiscountMTCatalog(&pMTList, data->pKVList);

		EARRAY_FOREACH_BEGIN(pMTList->ppProducts, i);
		{
			MicroTransactionProduct *pMTProduct = pMTList->ppProducts[i];
			MicroTransactionDef *pDef = GET_REF(pMTProduct->hDef);
			AccountProxyProduct *pProduct = pMTProduct->pProduct;
			MTUserProduct *userProduct = NULL;

			PERFINFO_AUTO_START("Per product", 1);
			userProduct = StructCreate(parse_MTUserProduct);
			userProduct->bPrereqsMet = AccountProxyKeysMeetRequirements(data->pKVList, pProduct->ppPrerequisites, AccountGetShardProxyName(), ShardCommon_GetClusterName(), microtrans_GetShardEnvironmentName(), NULL, NULL);
			userProduct->eErrorType = microtrans_GetCanPurchaseError(WEBREQUEST_PARTITION_INDEX, pEnt, pAccountData, pDef, locGetLanguage(getCurrentLocale()), &userProduct->pErrorMsg);
			userProduct->pProduct = gslAPCopyClientDiscount(pProduct);
			eaPush(&data->ppProducts, userProduct);
			PERFINFO_AUTO_STOP();
		} 
		EARRAY_FOREACH_END;

		data->eStatus = WDRSTATUS_SUCCESS;
	}
	PERFINFO_AUTO_STOP();
}

static void gslAP_UserCheckProducts(WebDiscountRequest *data)
{
	PERFINFO_AUTO_START_FUNC();
	wrCSub_AddDiscountRequest(data);
	PERFINFO_AUTO_STOP();
}

static void gslAP_UserKeyValuesResponse(TransactionReturnVal *returnVal, void *id)
{
	U32 uID = (U32) (intptr_t) id;
	AccountProxyKeyValueInfoList *pList = NULL;
	WebDiscountRequest *data = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	data = gslAPGetDiscountRequest(uID);
	if (data)
	{
		switch (RemoteCommandCheck_aslAPCmdGetAllKeyValues(returnVal, &pList))
		{
		case TRANSACTION_OUTCOME_FAILURE:
			data->eStatus = WDRSTATUS_FAILED;
			data->pError = strdup(WDR_ERROR_UNKNOWN);
		xcase TRANSACTION_OUTCOME_SUCCESS:
			data->pKVList = pList;
			gslAP_UserCheckProducts(data);
		}
	}
	PERFINFO_AUTO_STOP();
}

static void gslAP_UserGetKeyValues(WebDiscountRequest *data)
{
	RemoteCommand_aslAPCmdGetAllKeyValues(objCreateManagedReturnVal(gslAP_UserKeyValuesResponse, (void*)(intptr_t) data->uRequestID),
		GLOBALTYPE_ACCOUNTPROXYSERVER, 0, data->uAccountID, data->uCharacterID);
}

static void gslAPRequestMTCatalog_CB(U32 uContainerID, U32 uAccountID, const MicroTransactionProductList *pList, void *pUserdata)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uContainerID);
	AccountProxyKeyValueInfoList kvList = {0};

	if (!pEntity) return;

	kvList.ppList = SAFE_MEMBER2(pEntity, pPlayer, ppKeyValueCache);
	APDiscountMTCatalog(&pList, &kvList);
	ClientCmd_gclMicroTrans_RecvProductList(pEntity, pList);
}

void gslAPRequestMTCatalog(SA_PARAM_NN_VALID Entity *pEntity)
{
	PERFINFO_AUTO_START_FUNC();
	APGetMTCatalog(entGetContainerID(pEntity), entGetAccountID(pEntity), gslAPRequestMTCatalog_CB, NULL);
	PERFINFO_AUTO_STOP();
}

static void gslAPRequestPointBuyCatalog_CB(U32 uContainerID, U32 uAccountID, const AccountProxyProductList *pList, void *pUserdata)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uContainerID);

	if (!pEntity) return;

	ClientCmd_gclMicroTrans_RecvPointBuyProducts(pEntity, pList);
}

void gslAPRequestPointBuyCatalog(SA_PARAM_NN_VALID Entity *pEntity)
{
	APGetPointBuyCatalog(entGetContainerID(pEntity), entGetAccountID(pEntity), gslAPRequestPointBuyCatalog_CB, NULL);
}

void gslAPWebCatalogCB(U32 uContainerID, U32 uAccountID, const MicroTransactionProductList *pList, WebDiscountRequest *pRequest)
{
	PERFINFO_AUTO_START_FUNC();
	pRequest->uTimeReceived = timeSecondsSince2000();
	gslAP_UserGetKeyValues(pRequest);
	PERFINFO_AUTO_STOP();
}

WebDiscountRequest *gslAPRequestDiscounts(U32 uContainerID, U32 uAccountID)
{
	WebDiscountRequest *request;

	PERFINFO_AUTO_START_FUNC();
	if (request = gslAPFindDiscountRequest(uAccountID))
	{
		U32 uCurTime = timeSecondsSince2000();
		switch (request->eStatus)
		{
		case WDRSTATUS_SUCCESS:
		case WDRSTATUS_FAILED: // Intentional fall through
			eaFindAndRemove(&seaDiscountRequests, request);
			if (request->uTimeReceived > uCurTime - DISCOUNT_RESPONSE_EXPIRE)
			{
				PERFINFO_AUTO_STOP();
				return request;
			}
			// Otherwise response is expired, destroy it completely and continue with a new one
			StructDestroy(parse_WebDiscountRequest, request);
		xdefault:
			if (request->uTimeSent <= uCurTime - DISCOUNT_REQUEST_TIMEOUT)
			{
				// Request has timed out, destroy request
				eaFindAndRemove(&seaDiscountRequests, request);
				StructDestroy(parse_WebDiscountRequest, request);
			}
			PERFINFO_AUTO_STOP();
			return NULL;
		}
	}

	request = gslAPAddDiscountRequest(uAccountID, uContainerID);
	APGetMTCatalog(uContainerID, uAccountID, gslAPWebCatalogCB, request);
	PERFINFO_AUTO_STOP();

	return NULL;
}

//////////////////////////////////////////////
// Web C-Store Product Purchase call

static U32 suNextPurchaseID = 0;
static EARRAY_OF(PurchaseRequestData) seaPurchaseRequests = NULL;

PurchaseRequestData *gslMTPurchase_GetRequest(U32 uRequestID)
{
	if (!seaPurchaseRequests)
		return NULL;
	return eaIndexedGetUsingInt(&seaPurchaseRequests, uRequestID);
}

static void gslMTPurchase_AddRequest(PurchaseRequestData *data)
{
	data->uRequestID = suNextPurchaseID++;
	if (!seaPurchaseRequests)
		eaIndexedEnable(&seaPurchaseRequests, parse_PurchaseRequestData);
	eaIndexedAdd(&seaPurchaseRequests, data);
}

static void gslMTPurchase_SetStatus(PurchaseRequestData *data, PurchaseRequestStatus eStatus)
{
	data->eStatus = eStatus;
	if (eStatus == PURCHASE_FAILED || eStatus == PURCHASE_SUCCESS)
		data->uTimeComplete = timeSecondsSince2000();
}

static void RollbackPurchase_CB(TransactionReturnVal *pReturn, void *id)
{
	U32 uID = (U32) (intptr_t) id;
	PurchaseRequestData *pCBStruct = gslMTPurchase_GetRequest(uID);
	Entity *pEnt = pCBStruct->pEnt;
	const char *pchAction = NULL;

	PERFINFO_AUTO_START_FUNC();
	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		pchAction = "KeyRollbackSuccess";
	else
		pchAction = "KeyRollbackFailure";

	APLog(pEnt, pCBStruct->uAccountID, pchAction, "AccountID %d, ProductID %d, Currency %s",
		pCBStruct->uAccountID,
		pCBStruct->pProduct->uID,
		moneyCurrency(pCBStruct->pExpectedPrice));

	gslMTPurchase_SetStatus(pCBStruct, PURCHASE_FAILED);

	// Remove the reference handles
	wrCSub_MarkPurchaseCompleted(pCBStruct);
	PERFINFO_AUTO_STOP();
}

static void FinishPurchase_CB(TransactionReturnVal *pReturn, void *id)
{
	U32 uID = (U32) (intptr_t) id;
	PurchaseRequestData *pCBStruct = gslMTPurchase_GetRequest(uID);
	Entity *pEnt = pCBStruct->pEnt;
	const char *pchNotifyMesg = NULL;

	PERFINFO_AUTO_START_FUNC();
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		TransactionReturnVal *returnVal = NULL;

		PERFINFO_AUTO_START("Failure", 1);
		if(pEnt)
			returnVal = LoggedTransactions_CreateManagedReturnValEnt("MicroPurchaseRollback", pEnt, RollbackPurchase_CB, id);
		else
			returnVal = LoggedTransactions_CreateManagedReturnVal("MicroPurchaseRollback", RollbackPurchase_CB, id);

		devassert(returnVal != NULL);

		APLog(pEnt, pCBStruct->uAccountID, "MicroTransactionFailed", "AccountID %d, ProductID %d, Currency %s",
			pCBStruct->uAccountID,
			pCBStruct->pProduct->uID,
			moneyCurrency(pCBStruct->pExpectedPrice));

		pCBStruct->pError = strdup(WEBPURCHASE_ERROR_PURCHASEFAIL);
		gslMTPurchase_SetStatus(pCBStruct, PURCHASE_ROLLBACK);

		AutoTrans_AccountProxy_tr_RollbackPurchase(returnVal, objServerType(), 
			GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, pCBStruct->containerLockID, 
			pCBStruct->uAccountID,
			pCBStruct->pProduct,
			moneyCurrency(pCBStruct->pExpectedPrice),
			NULL);
		PERFINFO_AUTO_STOP();
	}
	else
	{
		PERFINFO_AUTO_START("Success", 1);
		APLog(pEnt, pCBStruct->uAccountID, "MicroTransactionSuccess", "AccountID %d, ProductID %d, Currency %s",
			pCBStruct->uAccountID,
			pCBStruct->pProduct->uID,
			moneyCurrency(pCBStruct->pExpectedPrice));

		gslMTPurchase_SetStatus(pCBStruct, PURCHASE_SUCCESS);

		// Remove the reference handles
		wrCSub_MarkPurchaseCompleted(pCBStruct);
		PERFINFO_AUTO_STOP();
	}
	PERFINFO_AUTO_STOP();
}

static void WebCStorePurchase_CB(AccountKeyValueResult result, U32 uAccountID, const AccountProxyProduct *pProduct, const Money *pExpectedPrice, const char *pmVID, ContainerID containerID, void *id)
{
	U32 uID = (U32) (intptr_t) id;
	// pmVID (payment method) is ignored since this is exclusively used with virtual currencies (key-values)
	PurchaseRequestData *pCBStruct = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pCBStruct = gslMTPurchase_GetRequest(uID);
	if (pCBStruct)
	{
		Entity *pEnt = pCBStruct->pEnt;
		const GameAccountData *pData = pCBStruct->pGAD;
		MicroTransactionDef *pDef = GET_REF(pCBStruct->hDef);
		bool bRollback = false;

		pCBStruct->containerLockID = containerID;

		//If there isn't an entity, player, GameAccountData, or product, then quit
		if(pEnt && pEnt->pPlayer && pDef && pData && pCBStruct && microtrans_CanPurchaseProduct(WEBREQUEST_PARTITION_INDEX, pEnt, pData, pDef))
		{
			if (result == AKV_SUCCESS)
			{
				// Extra check that should probably always pass - not a devassert if it doesn't though
				if(microtrans_CanPurchaseProduct(WEBREQUEST_PARTITION_INDEX, pEnt, pData, pDef))
				{
					TransactionReturnVal *returnVal = NULL;
					U32* eaPets = NULL;
					MicroTransactionRewards Rewards = {0};
					ItemChangeReason reason = {0};

					PERFINFO_AUTO_START("Success", 1);
					returnVal = LoggedTransactions_CreateManagedReturnValEnt("MicroPurchase", pEnt, FinishPurchase_CB, id);
					devassert(returnVal != NULL);

					MicroTrans_GenerateRewardBags(WEBREQUEST_PARTITION_INDEX, pEnt, pDef, &Rewards);

					if (microtrans_GrantsUniqueItem(pDef, &Rewards))
					{
						ea32Create(&eaPets);
						Entity_GetPetIDList(pEnt, &eaPets);
					}

					APLog(pEnt, pCBStruct->uAccountID, "KeyChangedsuccess", "AccountID %d, ProductID %d, Currency %s",
						pCBStruct->uAccountID,
						pCBStruct->pProduct->uID,
						moneyCurrency(pExpectedPrice));

					inv_FillItemChangeReason(&reason, pEnt, "MicroTrans:Purchase", pProduct->pInternalName);

					AutoTrans_AccountProxy_tr_FinishPurchase_MicroTransactionDef(returnVal,
						objServerType(),
						GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID, 
						GLOBALTYPE_ENTITYPLAYER, pEnt ? pEnt->myContainerID : 0, 
						GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
						GLOBALTYPE_GAMEACCOUNTDATA, pCBStruct->uAccountID,
						&Rewards, uAccountID, pProduct, pDef, pDef->pchName, moneyCurrency(pExpectedPrice), pmVID, &reason);

					ea32Destroy(&eaPets);
					StructDeInit(parse_MicroTransactionRewards, &Rewards);
					PERFINFO_AUTO_STOP();
				}
				else
				{
					TransactionReturnVal *returnVal = objCreateManagedReturnVal(RollbackPurchase_CB, id);

					PERFINFO_AUTO_START("Failure 1", 1);
					gslMTPurchase_SetStatus(pCBStruct, PURCHASE_ROLLBACK);
					pCBStruct->pError = strdup(WEBPURCHASE_ERROR_RESTRICTED);

					APLog(NULL, pCBStruct->uAccountID, "RollingBackItemRestricted", "AccountID %d, ProductID %d, Currency %s",
						pCBStruct->uAccountID,
						pCBStruct->pProduct->uID,
						moneyCurrency(pExpectedPrice));

					AutoTrans_AccountProxy_tr_RollbackPurchase(returnVal, objServerType(), 
						GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID, 
						pCBStruct->uAccountID,
						pCBStruct->pProduct,
						moneyCurrency(pExpectedPrice),
						NULL);
					PERFINFO_AUTO_STOP();
				}
			}
			else // some sort of failure, make error message based on result
			{
				PERFINFO_AUTO_START("Failure 2", 1);
				switch (result)
				{
				xcase AKV_INVALID_KEY:
					pCBStruct->pError = strdup(WEBPURCHASE_ERROR_BADCURRENCY);
				xcase AKV_NONEXISTANT:
					pCBStruct->pError = strdup(WEBPURCHASE_ERROR_CURRENCY_INSUFFICIENT);
				xcase AKV_INVALID_RANGE:
					pCBStruct->pError = strdup(WEBPURCHASE_ERROR_CURRENCY_INSUFFICIENT);
				xcase AKV_LOCKED:
					pCBStruct->pError = strdup(WEBPURCHASE_ERROR_PURCHASEFAIL);
				xcase AKV_FORBIDDEN_CHANGE:
					pCBStruct->pError = strdup(WEBPURCHASE_ERROR_PRICE_CHANGED);
				xdefault:
				case AKV_FAILURE:
					pCBStruct->pError = strdup(WEBPURCHASE_ERROR_PURCHASEFAIL);
				}

				gslMTPurchase_SetStatus(pCBStruct, PURCHASE_FAILED);

				// Remove the reference handles
				wrCSub_MarkPurchaseCompleted(pCBStruct);
				PERFINFO_AUTO_STOP();
			}
		}
		else if (result == AKV_SUCCESS) // Roll backs aren't needed for AKV failures
		{
			TransactionReturnVal *returnVal = objCreateManagedReturnVal(RollbackPurchase_CB, id);

			PERFINFO_AUTO_START("Failure 3", 1);
			APLog(pEnt, pCBStruct->uAccountID, "RollingBack - NoGAD/Def", "AccountID %d, ProductID %d, Currency %s",
				pCBStruct->uAccountID,
				pCBStruct->pProduct->uID,
				moneyCurrency(pExpectedPrice));

			pCBStruct->pError = strdup(WEBPURCHASE_ERROR_UNKNOWN);
			gslMTPurchase_SetStatus(pCBStruct, PURCHASE_ROLLBACK);

			AutoTrans_AccountProxy_tr_RollbackPurchase(returnVal, objServerType(), 
				GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID, 
				pCBStruct->uAccountID,
				pCBStruct->pProduct,
				moneyCurrency(pExpectedPrice),
				NULL);
			PERFINFO_AUTO_STOP();
		}
	}
	PERFINFO_AUTO_STOP();
}

void gslAP_WebCStorePurchase (Entity *pEnt, GameAccountData *pAccountData, PurchaseRequestData *pCBStruct)
{
	MicroTransactionDef *pDef = GET_REF(pCBStruct->hDef);
	pCBStruct->pEnt = pEnt;
	pCBStruct->pGAD = pAccountData;

	PERFINFO_AUTO_START_FUNC();
	if (!pEnt || !pAccountData || pAccountData->iAccountID != pCBStruct->uAccountID || entGetAccountID(pEnt) != pCBStruct->uAccountID)
	{
		pCBStruct->eStatus = WDRSTATUS_FAILED;
		pCBStruct->pError = strdup(WDR_ERROR_INVALID);
	}
	else if (!wrCSub_VerifyVersion(pEnt))
	{
		pCBStruct->eStatus = WDRSTATUS_FAILED;
		pCBStruct->pError = strdup(WDR_ERROR_INVALID_VERSION);
	}
	else if (!pDef || !pCBStruct->pProduct)
	{
		gslMTPurchase_SetStatus(pCBStruct, PURCHASE_FAILED);
		pCBStruct->pError = strdup(WEBPURCHASE_ERROR_UNKNOWN);
	}
	else if (!pCBStruct->pExpectedPrice || isRealCurrency(moneyCurrency(pCBStruct->pExpectedPrice)) || 
		findMoneyFromCurrency(pCBStruct->pProduct->ppFullMoneyPrices, moneyCurrency(pCBStruct->pExpectedPrice)) == NULL)
	{
		// Make sure currency is virtual and valid
		gslMTPurchase_SetStatus(pCBStruct, PURCHASE_FAILED);
		pCBStruct->pError = strdup(WEBPURCHASE_ERROR_BADCURRENCY);
	}
	else if(pDef->bDeprecated)
	{
		gslMTPurchase_SetStatus(pCBStruct, PURCHASE_FAILED);
		pCBStruct->pError = strdup(WEBPURCHASE_ERROR_DEPRECATED);
	}
	else if(IS_HANDLE_ACTIVE(pDef->hRequiredPurchase) && !microtrans_HasPurchased(pAccountData, GET_REF(pDef->hRequiredPurchase)))
	{
		MicroTransactionDef *pRequiredDef = GET_REF(pDef->hRequiredPurchase);
		gslMTPurchase_SetStatus(pCBStruct, PURCHASE_FAILED);
		pCBStruct->pError = strdup(WEBPURCHASE_ERROR_RESTRICTED);

		if(pRequiredDef)
			estrCopy2(&pCBStruct->prereqName, pRequiredDef->pchProductName);
	}
	else if (!microtrans_CanPurchaseProduct(WEBREQUEST_PARTITION_INDEX, pEnt, pAccountData, pDef))
	{
		gslMTPurchase_SetStatus(pCBStruct, PURCHASE_FAILED);
		pCBStruct->pError = strdup(WEBPURCHASE_ERROR_RESTRICTED);
		// Product name is unknown here
	}
	else
	{
		APLog(pEnt, pCBStruct->uAccountID, "FoundProduct", "AccountID %d, ProductID %d, Currency %s",
			pCBStruct->uAccountID,
			pCBStruct->pProduct->uID,
			moneyCurrency(pCBStruct->pExpectedPrice));
		APBeginPurchase(pCBStruct->uAccountID, pCBStruct->pProduct, pCBStruct->pExpectedPrice, 0, NULL, WebCStorePurchase_CB, (void*)(intptr_t) pCBStruct->uRequestID);
	}
	if (pCBStruct->eStatus == PURCHASE_FAILED)
	{
		// Remove the reference handles
		wrCSub_MarkPurchaseCompleted(pCBStruct);
	}
	PERFINFO_AUTO_STOP();
}

static void gslMTPurchase_Start(PurchaseRequestData *data)
{
	wrCSub_AddPurchaseRequest(data);
}

// Returns the request ID
int gslAP_InitiatePurchase(U32 uCharacterID, U32 uAccountID, U32 uProductID, int iExpectedPrice)
{
	PurchaseRequestData *pCBStruct = StructCreate(parse_PurchaseRequestData);
	MicroTransactionInfo *productList;

	PERFINFO_AUTO_START_FUNC();
	productList = gslAPGetProductList();

	if (devassert(productList))
	{
		MicroTransactionProduct *pProduct = eaIndexedGetUsingInt(&productList->ppProducts, uProductID);
		if (pProduct)
		{
			MicroTransactionDef *pDef = GET_REF(pProduct->hDef);
			if (pDef)
			{
				pCBStruct->pProduct = StructClone(parse_AccountProxyProduct, pProduct->pProduct);
				SET_HANDLE_FROM_REFERENT(g_hMicroTransDefDict, pDef, pCBStruct->hDef);
			}
			else
			{
				gslMTPurchase_SetStatus(pCBStruct, PURCHASE_FAILED);
				pCBStruct->pError = strdup(WEBPURCHASE_ERROR_UNKNOWN);
			}
		}
		else
		{
			gslMTPurchase_SetStatus(pCBStruct, PURCHASE_FAILED);
			pCBStruct->pError = strdup(WEBPURCHASE_ERROR_UNKNOWN);
		}
	}
	else
	{
		gslMTPurchase_SetStatus(pCBStruct, PURCHASE_FAILED);
		pCBStruct->pError = strdup(WEBPURCHASE_ERROR_UNKNOWN);
	}

	if(iExpectedPrice < 0)
		pCBStruct->pExpectedPrice = moneyCreateInvalid(microtrans_GetShardCurrencyExactName());
	else
		pCBStruct->pExpectedPrice = moneyCreateFromInt(iExpectedPrice, microtrans_GetShardCurrencyExactName());
	pCBStruct->uAccountID = uAccountID;
	pCBStruct->uCharacterID = uCharacterID;

	gslMTPurchase_AddRequest(pCBStruct);
	if (pCBStruct->eStatus != PURCHASE_FAILED)
		gslMTPurchase_Start(pCBStruct);
	PERFINFO_AUTO_STOP();
	return pCBStruct->uRequestID;
}

PurchaseRequestStatus gslAP_GetPurchaseStatus(U32 uAccountID, U32 uRequestID, char **pOutError)
{
	PurchaseRequestData *request = gslMTPurchase_GetRequest(uRequestID);
	PurchaseRequestStatus eStatus = PURCHASE_FAILED;

	PERFINFO_AUTO_START_FUNC();
	
	if (!request)
	{
		*pOutError = strdup(WEBPURCHASE_ERROR_PURCHASEUNKNOWN);
		PERFINFO_AUTO_STOP();
		return eStatus;
	}

	if (request->uAccountID != uAccountID)
	{
		*pOutError = strdup(WEBPURCHASE_ERROR_MISMATCHED_ACCOUNTID);
		PERFINFO_AUTO_STOP();
		return eStatus;
	}

	eStatus = request->eStatus;
	if (request->eStatus == PURCHASE_FAILED)
	{
		*pOutError = strdup(request->pError);
	}

	if (PurchaseIsCompleted(request->eStatus))
	{
		eaFindAndRemove(&seaPurchaseRequests, request);
		StructDestroy(parse_PurchaseRequestData, request);
	}

	PERFINFO_AUTO_STOP();
	return eStatus;
}

/////////////////////////////////////////////
// Periodic Cleanup of Discount and PurchaseRequests

void gslWebRequestTick(void)
{
	U32 uCurTime = timeSecondsSince2000();
	int i;

	PERFINFO_AUTO_START_FUNC();
	PERFINFO_AUTO_START("Discounts", 1);
	for (i=eaSize(&seaDiscountRequests)-1; i>=0; i--)
	{
		if ((seaDiscountRequests[i]->uTimeReceived && 
			seaDiscountRequests[i]->uTimeReceived + DISCOUNT_RESPONSE_EXPIRE < uCurTime) ||
			(!seaDiscountRequests[i]->uTimeReceived && 
			seaDiscountRequests[i]->uTimeSent + DISCOUNT_REQUEST_TIMEOUT < uCurTime))
		{
			WebDiscountRequest *request = eaRemove(&seaDiscountRequests, i);
			if (request->uSubRequestID)
				wrCSub_RemoveDiscountRequest(request);
			StructDestroy(parse_WebDiscountRequest, request);
		}
	}
	PERFINFO_AUTO_STOP_START("Products", 1);
	for (i=eaSize(&seaPurchaseRequests)-1; i>=0; i--)
	{
		if (seaPurchaseRequests[i]->uTimeComplete &&
			seaPurchaseRequests[i]->uTimeComplete + PURCHASE_RESPONSE_EXPIRE < uCurTime &&
			PurchaseIsCompleted(seaPurchaseRequests[i]->eStatus))
		{
			PurchaseRequestData *request = eaRemove(&seaPurchaseRequests, i);
			if (request->uSubRequestID)
				wrCSub_RemovePurchaseRequest(request);
			StructDestroy(parse_PurchaseRequestData, request);
		}
	}
	PERFINFO_AUTO_STOP();
	wrCSub_Tick();
	PERFINFO_AUTO_STOP();
}

#if 0

AccountProxyProductList *gpMicrotransHammerList = NULL;
F32 gfMicrotransHammerSuccessRate = 0.0f;
F32 gfMicrotransHammerDelayForCompletion = 5.0f;
char gpcMicrotransHammerCategory[128];
char gpcMicrotransHammerCurrency[128] = "_ChampsPoints";
int giMicrotransHammerProductIter = -2;

void gslAPMicrotransHammerIterate(void);

void gslAPMicrotransHammerReturn(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData)
{
	gslAPMicrotransHammerIterate();
}

void gslAPMicrotransHammerReturnContainer(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData)
{
	Entity *pEnt = entFromEntityRefAnyPartition((EntityRef)((intptr_t)userData));

	if(!pEnt)
	{
		return;
	}

	gslLogOutEntity(pEnt, 0, 0);
}

void gslAPMicrotransHammerBuyProduct(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData)
{
	Entity *pEnt = entFromEntityRefAnyPartition((EntityRef)((intptr_t)userData));
	AccountProxyProduct *pProd = NULL;

	if(!pEnt || !entGetAccountID(pEnt))
	{
		return;
	}

	APPurchaseProduct(pEnt, entGetAccountID(pEnt), entGetLanguage(pEnt), gpcMicrotransHammerCategory, gpMicrotransHammerList->ppList[giMicrotransHammerProductIter]->uID, NULL, 0, NULL, NULL);
	TimedCallback_Run(gslAPMicrotransHammerReturnContainer, userData, gfMicrotransHammerDelayForCompletion);
}

void gslAPMicrotransHammerReadyToBuy(AccountKeyValueResult result, SA_PARAM_NN_STR const char *key, SA_PARAM_OP_VALID void *userData)
{
	Entity *pEnt = entFromEntityRefAnyPartition((EntityRef)((intptr_t)userData));

	if(!pEnt)
	{
		return;
	}

	if(result != AKV_SUCCESS)
	{
		gslLogOutEntity(pEnt, 0, 0);
		return;
	}

	TimedCallback_Run(gslAPMicrotransHammerBuyProduct, userData, 0.0f);
}

void gslAPMicrotransHammerSetKeyValues(Entity *pEnt)
{
	int success = 0;
	int cost = 0;

	if(!pEnt)
	{
		return;
	}

	FOR_EACH_IN_EARRAY(gpMicrotransHammerList->ppList[giMicrotransHammerProductIter]->ppMoneyPrices, Money, pMoney)
	{
		if(!stricmp(moneyCurrency(pMoney), STACK_SPRINTF("_%s", gpcMicrotransHammerCurrency)))
		{
			cost = moneyCountPoints(pMoney);
			break;
		}
	}
	FOR_EACH_END

	success = cost * gfMicrotransHammerSuccessRate;
	AutoTrans_gslGAD_tr_SetReference(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt));
	APSetKeyValueSimple(pEnt->pPlayer->accountID, gpcMicrotransHammerCurrency, randomIntRange(success, cost+success-1), false, gslAPMicrotransHammerReadyToBuy, (void *)((intptr_t)entGetRef(pEnt)));
}

void gslAPMicrotransHammerVerifyGADReturn(TransactionReturnVal *returnVal, void *userData)
{
	if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		TimedCallback_Run(gslAPMicrotransHammerReturnContainer, userData, 0.0f);
	}
}

void gslAPMicrotransHammerContainerReturn(TimedCallback *pCallback, F32 timeSinceLastCallback, BatonHolder *holder)
{
	Container *con = objGetContainer(holder->type, holder->id);
	Entity *pEnt = con->containerData;

	if(giMicrotransHammerProductIter >= 0)
	{
		gslAPMicrotransHammerSetKeyValues(pEnt);
	}
	else
	{
		TransactionRequest *request = objCreateTransactionRequest();

		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, "VerifyContainer containerIDVar %s %d", GlobalTypeToName(GLOBALTYPE_GAMEACCOUNTDATA), pEnt->pPlayer->accountID);
		objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC, objCreateManagedReturnVal(gslAPMicrotransHammerVerifyGADReturn, (void *)((intptr_t)entGetRef(pEnt))), "EnsureGameContainerExists_MicrotransHammer", request);
		objDestroyTransactionRequest(request);
	}
}

void gslAPMicrotransHammerIterate(void)
{
	char *perc_id;

	devassert(gpMicrotransHammerList->ppList);

	if(++giMicrotransHammerProductIter >= eaSize(&gpMicrotransHammerList->ppList))
	{
		printf("MicrotransHammer: COMPLETE\n");
		return;
	}

	if(giMicrotransHammerProductIter < 0)
	{
		printf("MicrotransHammer: Verifying all entities' GAD containers\n");
	}
	else
	{
		printf("MicrotransHammer: Beginning percolation for product \"%s\"\n", gpMicrotransHammerList->ppList[giMicrotransHammerProductIter]->pName);
	}

	perc_id = strdup(allocAddString("MicrotransHammer"));
	gsl_GenericPercoRaider(GLOBALTYPE_ENTITYPLAYER, perc_id, 500, 0, 80, 0.003f, gslAPMicrotransHammerReturn, gslAPMicrotransHammerContainerReturn, NULL);
	free(perc_id);
}

void gslAPDirectProductList(SA_PARAM_NN_STR const char *pchCategory, SA_PARAM_NN_VALID AccountProxyProductList *pList)
{
	if(eaSize(&pList->ppList) == 0)
	{
		printf("MicrotransHammer: FAILED - no products available in category \"%s\"\n", pchCategory);
		return;
	}

	gpMicrotransHammerList = StructClone(parse_AccountProxyProductList, pList);
	giMicrotransHammerProductIter = -2;
	gslAPMicrotransHammerIterate();
}

void gslAPMicrotransHammer(const char *pchCategory, F32 fSuccessRate)
{
	sprintf(gpcMicrotransHammerCategory, "%s.%s", microtrans_GetShardCategoryPrefix(), pchCategory);
	printf("MicrotransHammer: Beginning on category \"%s\" with success rate %0.1f\n", gpcMicrotransHammerCategory, fSuccessRate);
	gfMicrotransHammerSuccessRate = fSuccessRate;
	RemoteCommand_aslAPCmdRequestProductListDirectToServer(GLOBALTYPE_ACCOUNTPROXYSERVER, 0, GetAppGlobalType(), GetAppGlobalID(), gpcMicrotransHammerCategory, true);
}

void gslAPMicrotransHammerDelay(F32 fDelay)
{
	gfMicrotransHammerDelayForCompletion = fDelay;
}

void gslAPMicrotransHammerCurrency(const char *pchCurrency)
{
	sprintf(gpcMicrotransHammerCurrency, "%s", pchCurrency);
}

#endif // 0

#include "AutoGen/gslAccountProxy_h_ast.c"