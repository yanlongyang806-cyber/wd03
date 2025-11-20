#include "AccountProxy.h"

#include "AccountLog.h"
#include "AccountManagement.h"
#include "AccountServer.h"
#include "AccountTransactionLog.h"
#include "Discount.h"
#include "KeyValues/KeyValues.h"
#include "KeyValues/VirtualCurrency.h"
#include "Product.h"
#include "Proxy.h"
#include "PurchaseProduct.h"
#include "Steam.h"
#include "SubscriptionHistory.h"

#include "AccountDataCache.h"
#include "AccountServerConfig.h"
#include "Alerts.h"
#include "CrypticPorts.h"
#include "earray.h"
#include "error.h"
#include "EString.h"
#include "fastAtoi.h"
#include "GlobalComm.h"
#include "GlobalComm_h_ast.h"
#include "JSONRPC.h"
#include "logging.h"
#include "Money.h"
#include "net/accountnet.h"
#include "net/net.h"
#include "objContainer.h"
#include "sock.h"
#include "SteamCommon.h"
#include "StringUtil.h"
#include "StructNet.h"
#include "timing.h"
#include "timing_profiler.h"

#include "AccountDataCache_h_ast.h"
#include "PurchaseProduct_h_ast.h"
#include "AutoGen/websrv_h_ast.h"

#define PACKET_PARAMS_PROTOTYPE SA_PARAM_OP_VALID Proxy *pProxy, SA_PARAM_NN_VALID Packet *pkt, int cmd, SA_PARAM_NN_VALID NetLink *link, SA_PARAM_OP_VALID void *userData
#define PACKET_PARAMS pProxy, pkt, cmd, link, userData


/************************************************************************/
/* Types                                                                */
/************************************************************************/

typedef struct SendKeyValueInfo
{
	const AccountInfo *pAccount;
	const char *pKey;
} SendKeyValueInfo;

typedef struct SendKeyRemoveInfo
{
	U32 uAccountID;
	const char *pKey;
} SendKeyRemoveInfo;


/************************************************************************/
/* Private functions                                                    */
/************************************************************************/

// Convert a single key-value pair into the account proxy equivelent
SA_RET_OP_VALID static AccountProxyKeyValueInfo *ConvertKeyValuePair(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey)
{
	S64 iValue = 0;

	PERFINFO_AUTO_START_FUNC();

	if (AccountKeyValue_Get(pAccount, pKey, &iValue) == AKV_SUCCESS)
	{
		AccountProxyKeyValueInfo *item = StructCreate(parse_AccountProxyKeyValueInfo);
		estrCopy2(&item->pKey, pKey);
		estrPrintf(&item->pValue, "%"FORM_LL"d", iValue);
		PERFINFO_AUTO_STOP();
		return item;
	}

	PERFINFO_AUTO_STOP();

	return NULL;
}

static AccountProxyDiscountList *GetProxyDiscountList(void)
{
	EARRAY_OF(DiscountContainer) eaDiscounts = NULL;
	AccountProxyDiscountList *apDiscountList = NULL;

	PERFINFO_AUTO_START_FUNC();
	eaDiscounts = getAllDiscounts();
	apDiscountList = StructCreate(parse_AccountProxyDiscountList);

	EARRAY_FOREACH_BEGIN(eaDiscounts, i);
	{
		DiscountContainer *pLocalDiscount = eaDiscounts[i];
		AccountProxyDiscount *apDiscount = NULL;

		if (!pLocalDiscount->bEnabled) continue;

		apDiscount = StructCreate(parse_AccountProxyDiscount);
		apDiscount->uID = pLocalDiscount->uID;
		apDiscount->bEnabled = pLocalDiscount->bEnabled;

		apDiscount->uStartSS2000 = pLocalDiscount->uStartSS2000;
		apDiscount->uEndSS2000 = pLocalDiscount->uEndSS2000;

		estrCopy2(&apDiscount->pName, pLocalDiscount->pName);
		estrCopy2(&apDiscount->pInternalName, pLocalDiscount->pProductInternalName);
		estrCopy2(&apDiscount->pCurrency, pLocalDiscount->pCurrency);

		apDiscount->uPercentageDiscount = pLocalDiscount->uPercentageDiscount;
		apDiscount->bBlacklistProducts = pLocalDiscount->bBlacklistProducts;
		apDiscount->bBlacklistCategories = pLocalDiscount->bBlacklistCategories;

		EARRAY_FOREACH_BEGIN(pLocalDiscount->eaPrereqs, j);
		{
			eaPush(&apDiscount->ppPrerequisites, estrDup(pLocalDiscount->eaPrereqs[j]));
		}
		EARRAY_FOREACH_END;

		EARRAY_FOREACH_BEGIN(pLocalDiscount->eaProducts, j);
		{
			eaPush(&apDiscount->ppProducts, estrDup(pLocalDiscount->eaProducts[j]));
		}
		EARRAY_FOREACH_END;

		EARRAY_FOREACH_BEGIN(pLocalDiscount->eaCategories, j);
		{
			eaPush(&apDiscount->ppCategories, estrDup(pLocalDiscount->eaCategories[j]));
		}
		EARRAY_FOREACH_END;

		eaPush(&apDiscountList->ppList, apDiscount);
	}
	EARRAY_FOREACH_END;

	freeDiscountsArray(&eaDiscounts);
	PERFINFO_AUTO_STOP();
	return apDiscountList;
}

static AccountProxyProductList *GetProxyProductList(void)
{
	EARRAY_OF(ProductContainer) eaProducts = NULL;
	AccountProxyProductList *apProductList = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	eaProducts = getProductList(PRODUCTS_ALL);
	apProductList = StructCreate(parse_AccountProxyProductList);

	EARRAY_FOREACH_BEGIN(eaProducts, i);
	{
		ProductContainer *pLocalProduct = eaProducts[i];
		AccountProxyProduct *apProduct = NULL;
		const Money *pFullPrice = NULL;
		Money *pDiscountedPrice = NULL;
		const char *pCurrency = NULL;
		U32 uDiscountedPrice = 0;
		char discountedPriceStr[11];

		if (eaSize(&pLocalProduct->ppCategories) == 0) continue;

		apProduct = StructCreate(parse_AccountProxyProduct);

		EARRAY_FOREACH_BEGIN(pLocalProduct->ppCategories, j);
		{
			eaPush(&apProduct->ppCategories, estrDup(pLocalProduct->ppCategories[j]));
		}
		EARRAY_FOREACH_END;

		EARRAY_FOREACH_BEGIN(pLocalProduct->ppMoneyPrices, j);
		{
			pFullPrice = moneyContainerToMoneyConst(pLocalProduct->ppMoneyPrices[j]);
			pCurrency = moneyCurrency(pFullPrice);
			eaPush(&apProduct->ppFullMoneyPrices, StructClone(parse_Money, pFullPrice));

			if(!isRealCurrency(pCurrency))
			{
				uDiscountedPrice = moneyCountPoints(pFullPrice);
				APPLY_DISCOUNT_EFFECT(uDiscountedPrice, getProductDiscountPercentage(NULL, pLocalProduct->uID, pCurrency, NULL, NULL, NULL, NULL));
				itoa(uDiscountedPrice, discountedPriceStr, 10);

				pDiscountedPrice = moneyCreate(discountedPriceStr, pCurrency);
				eaPush(&apProduct->ppMoneyPrices, pDiscountedPrice);
			}
			else
			{
				eaPush(&apProduct->ppMoneyPrices, StructClone(parse_Money, pFullPrice));
			}
		}
		EARRAY_FOREACH_END;

		EARRAY_FOREACH_BEGIN(pLocalProduct->ppPrerequisites, j);
		{
			eaPush(&apProduct->ppPrerequisites, estrDup(pLocalProduct->ppPrerequisites[j]));
		}
		EARRAY_FOREACH_END;

		EARRAY_FOREACH_BEGIN(pLocalProduct->ppKeyValues, j);
		{	
			eaPush(&apProduct->ppKeyValues, StructCloneVoid(parse_AccountProxyKeyValueInfo, pLocalProduct->ppKeyValues[j]));
		}
		EARRAY_FOREACH_END;

		EARRAY_FOREACH_BEGIN(pLocalProduct->ppLocalizedInfo, j);
		{
			ProductLocalizedInfo *pInfo = pLocalProduct->ppLocalizedInfo[j];
			AccountProxyProductLocalizedInfo *pLocalizedInfo = StructCreate(parse_AccountProxyProductLocalizedInfo);

			pLocalizedInfo->pDescription = estrDup(pInfo->pDescription);
			pLocalizedInfo->pName = estrDup(pInfo->pName);
			pLocalizedInfo->pLanguageTag = estrDup(pInfo->pLanguageTag);
			eaPush(&apProduct->ppLocalizedInfo, pLocalizedInfo);
		}
		EARRAY_FOREACH_END;

		apProduct->uID = pLocalProduct->uID;
		estrCopy2(&apProduct->pDescription, pLocalProduct->pDescription);
		estrCopy2(&apProduct->pInternalName, pLocalProduct->pInternalName);
		estrCopy2(&apProduct->pName, pLocalProduct->pName);
		estrCopy2(&apProduct->pItemID, pLocalProduct->pItemID);

		apProduct->qwOfferID = pLocalProduct->xbox.qwOfferID;
		memcpy(apProduct->contentId, pLocalProduct->xbox.contentId, XBOX_CONTENTID_SIZE);

		eaPush(&apProductList->ppList, apProduct);
	}
	EARRAY_FOREACH_END;

	eaDestroy(&eaProducts);
	PERFINFO_AUTO_STOP();
	return apProductList;
}

static void SendProductsToLink(SA_PARAM_NN_VALID NetLink *pLink, SA_PARAM_NN_VALID AccountProxyProductList *pProductList)
{
	Packet *packet = NULL;

	packet = pktCreate(pLink, FROM_ACCOUNTSERVER_PROXY_PRODUCTS);
	ParserSendStructSafe(parse_AccountProxyProductList, packet, pProductList);
	pktSend(&packet);
}

static void SendDiscountsToLink(SA_PARAM_NN_VALID NetLink *pLink, SA_PARAM_NN_VALID AccountProxyDiscountList *pDiscountList)
{
	Packet *packet = NULL;

	packet = pktCreate(pLink, FROM_ACCOUNTSERVER_PROXY_DISCOUNTS);
	ParserSendStructSafe(parse_AccountProxyDiscountList, packet, pDiscountList);
	pktSend(&packet);
}

// Send the proxy protocol version
static void SendProtocolVersionToLink(SA_PARAM_NN_VALID NetLink *link)
{
	Packet *packet = pktCreate(link, FROM_ACCOUNTSERVER_PROXY_PROTOCOL_VERSION);
	pktSendU32(packet, ACCOUNT_PROXY_PROTOCOL_VERSION);
	pktSend(&packet);
}

// Send the current list of products to a proxy
static void SendProductsToProxy(SA_PARAM_NN_VALID Proxy *pProxy, SA_PARAM_NN_VALID AccountProxyProductList *pProductList)
{
	NetLink *pLink = getProxyNetLink(pProxy);
	if (pLink) SendProductsToLink(pLink, pProductList);
}

static void SendDiscountsToProxy(SA_PARAM_NN_VALID Proxy *pProxy, SA_PARAM_NN_VALID AccountProxyDiscountList *pDiscountList)
{
	NetLink *pLink = getProxyNetLink(pProxy);
	if (pLink) SendDiscountsToLink(pLink, pDiscountList);
}

// Send a key-value to a proxy
static void SendKeyValueToProxy(SA_PARAM_NN_VALID Proxy *pProxy, SA_PARAM_NN_VALID SendKeyValueInfo *pInfo)
{
	NetLink *pLink = getProxyNetLink(pProxy);
	Packet *pkt;
	AccountProxyKeyValueInfo *kvInfo;

	if (!pLink) return;
	
	pkt = pktCreate(pLink, FROM_ACCOUNTSERVER_PROXY_KEY_VALUE);
	kvInfo = ConvertKeyValuePair(pInfo->pAccount, pInfo->pKey);

	if (kvInfo)
	{
		pktSendU32(pkt, pInfo->pAccount->uID);
		ParserSendStructSafe(parse_AccountProxyKeyValueInfo, pkt, kvInfo);
		pktSend(&pkt);
		StructDestroy(parse_AccountProxyKeyValueInfo, kvInfo);
	}
}

// Send a request to remove a key-value to a proxy
static void SendKeyRemoveToProxy(SA_PARAM_NN_VALID Proxy *pProxy, SA_PARAM_NN_VALID SendKeyRemoveInfo *pInfo)
{
	NetLink *pLink = getProxyNetLink(pProxy);
	Packet *pkt;

	if (!pLink) return;

	pkt = pktCreate(pLink, FROM_ACCOUNTSERVER_PROXY_REMOVE_KEY);
	pktSendU32(pkt, pInfo->uAccountID);
	pktSendString(pkt, pInfo->pKey);
	pktSend(&pkt);
}

static void SendRecruitInfoToLink(SA_PARAM_NN_VALID NetLink *pLink, SA_PARAM_NN_VALID const AccountInfo *pAccount)
{
	Packet *pkt = NULL;
	RecruitInfo recruitInfo = {0};

	PERFINFO_AUTO_START_FUNC();
	pkt = pktCreate(pLink, FROM_ACCOUNTSERVER_PROXY_RECRUIT_INFO);

	recruitInfo.uAccountID = pAccount->uID;
	eaCopyStructsDeConst(&pAccount->eaRecruits, &recruitInfo.eaRecruits, parse_Recruit);
	eaCopyStructsDeConst(&pAccount->eaRecruiters, &recruitInfo.eaRecruiters, parse_Recruiter);

	// Prune out ones the shard doesn't care about
	EARRAY_FOREACH_REVERSE_BEGIN(recruitInfo.eaRecruits, iCurRecruit);
	{
		const Recruit *pRecruit = recruitInfo.eaRecruits[iCurRecruit];
		bool bPrune = false;

		if (!devassert(pRecruit)) continue;

		if (pRecruit->eRecruitState == RS_PendingOffer) bPrune = true;
		if (pRecruit->eRecruitState == RS_Offered) bPrune = true;
		if (pRecruit->eRecruitState == RS_OfferCancelled) bPrune = true;
		if (pRecruit->eRecruitState == RS_AlreadyRecruited) bPrune = true;
		if (!pRecruit->uAccountID) bPrune = true;

		if (bPrune)
		{
			StructDestroyNoConst(parse_Recruit, eaRemove(&recruitInfo.eaRecruits, iCurRecruit));
		}
	}
	EARRAY_FOREACH_END;

	ParserSendStructSafe(parse_RecruitInfo, pkt, &recruitInfo);

	StructDeInit(parse_RecruitInfo, &recruitInfo);

	pktSend(&pkt);
	PERFINFO_AUTO_STOP();
}

// Send recruit info to all proxies
static void SendRecruitInfoToProxy(SA_PARAM_NN_VALID Proxy *pProxy, SA_PARAM_NN_VALID const AccountInfo *pAccount)
{
	NetLink *pLink = getProxyNetLink(pProxy);

	if (!pLink) return;

	SendRecruitInfoToLink(pLink, pAccount);
}

static void SendDisplayNameToProxy(SA_PARAM_NN_VALID Proxy *pProxy, SA_PARAM_NN_VALID const AccountInfo *pAccount)
{
	NetLink *pLink = getProxyNetLink(pProxy);
	Packet *pkt = NULL;

	if (!pLink) return;

	pkt = pktCreate(pLink, FROM_ACCOUNTSERVER_PROXY_DISPLAY_NAME);
	pktSendU32(pkt, pAccount->uID);
	pktSendString(pkt, pAccount->displayName);
	pktSend(&pkt);
}


/************************************************************************/
/* Packet handlers                                                      */
/************************************************************************/

#define PROXY_SOURCE_LOG_PAIR(proxy) ("source", "%s", proxy ? getProxyName(proxy) : "proxy")

static void HandleGetRequest(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyGetRequest packetRequest = {0};
	AccountProxyGetResponse *packetResponse = NULL;
	AccountInfo *pAccount = NULL;
	AccountKeyValueResult result = AKV_FAILURE;
	Packet *response = NULL;
	const char *pSubbedKey = NULL;

	PERFINFO_AUTO_START_FUNC();

	packetResponse = StructCreate(parse_AccountProxyGetResponse);

	ParserRecvStructSafe(parse_AccountProxyGetRequest, pkt, &packetRequest);

	pAccount = findAccountByID(packetRequest.uAccountID);
	
	if (packetRequest.pKey)
		pSubbedKey = AccountProxySubstituteKeyTokens(packetRequest.pKey, getProxyName(pProxy), getProxyCluster(pProxy), getProxyEnvironment(pProxy));

	packetResponse->uAccountID = packetRequest.uAccountID;
	packetResponse->iCmdID = packetRequest.iCmdID;
	estrCopy2(&packetResponse->pKey, pSubbedKey);
	packetResponse->eResult = AKV_NONEXISTANT;

	if (pAccount && pSubbedKey)
	{
		S64 iValue = 0;
		ANALYSIS_ASSUME(pAccount);
		packetResponse->eResult = AccountKeyValue_Get(pAccount, pSubbedKey, &iValue);
		packetResponse->pValue = strdupf("%"FORM_LL"d", iValue);
		packetResponse->iValue = iValue;
	}

	response = pktCreate(link, FROM_ACCOUNTSERVER_PROXY_GET_RESULT);
	ParserSendStructSafe(parse_AccountProxyGetResponse, response, packetResponse);
	pktSend(&response);

	StructDestroy(parse_AccountProxyGetResponse, packetResponse);
	StructDeInit(parse_AccountProxyGetRequest, &packetRequest);

	PERFINFO_AUTO_STOP();
}

static void HandleSetRequest(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxySetRequest packetRequest = {0};
	AccountProxySetResponse *packetResponse = NULL;
	AccountInfo *pAccount = NULL;
	AccountKeyValueResult result = AKV_FAILURE;
	Packet *response = NULL;
	const char *pSubbedKey =NULL;

	PERFINFO_AUTO_START_FUNC();

	packetResponse = StructCreate(parse_AccountProxySetResponse);

	ParserRecvStructSafe(parse_AccountProxySetRequest, pkt, &packetRequest);

	pAccount = findAccountByID(packetRequest.uAccountID);
	if (packetRequest.pKey)
		pSubbedKey = AccountProxySubstituteKeyTokens(packetRequest.pKey, getProxyName(pProxy), getProxyCluster(pProxy), getProxyEnvironment(pProxy));

	if (pAccount && pSubbedKey)
	{
		S64 iValue = 0;
		S64 iSetValue = 0;

		if (packetRequest.pValue)
		{
			iSetValue = atoi64(packetRequest.pValue);
		}
		else
		{
			iSetValue = packetRequest.iValue;
		}

		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "ProxySet",
			PROXY_SOURCE_LOG_PAIR(pProxy)
			("accountid", "%u", packetRequest.uAccountID)
			("key", "%s", pSubbedKey)
			("change", "%u", (packetRequest.operation == AKV_OP_INCREMENT) ? 1 : 0)
			("value", "%"FORM_LL"d", iSetValue));

		ANALYSIS_ASSUME(pSubbedKey);
		AccountKeyValue_Get(pAccount, pSubbedKey, &iValue);
		packetResponse->pValue = strdupf("%"FORM_LL"d", iValue);
		packetResponse->iValue = iValue;

		switch (packetRequest.operation)
		{
		xcase AKV_OP_SET:
			result = AccountKeyValue_Set(pAccount, pSubbedKey, iSetValue, &packetResponse->pLock);
		xcase AKV_OP_INCREMENT:
			result = AccountKeyValue_Change(pAccount, pSubbedKey, iSetValue, &packetResponse->pLock);
		xdefault:
			devassertmsg(false, "Unsupported account key operation.");
			result = AKV_FAILURE;
		}
	}

	packetResponse->containerID = packetRequest.containerID;
	packetResponse->uAccountID = packetRequest.uAccountID;
	estrCopy2(&packetResponse->pKey, pSubbedKey);
	packetResponse->result = result;
	packetResponse->requestID = packetRequest.requestID;

	response = pktCreate(link, FROM_ACCOUNTSERVER_PROXY_SET_RESULT);
	ParserSendStructSafe(parse_AccountProxySetResponse, response, packetResponse);
	pktSend(&response);

	StructDestroy(parse_AccountProxySetResponse, packetResponse);
	StructDeInit(parse_AccountProxySetRequest, &packetRequest);

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleCommitRollbackRequest(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyCommitRollbackRequest requestPacket = {0};
	Packet *response = NULL;
	AccountInfo *info = NULL;
	AccountProxyAcknowledge *responsePacket = NULL;
	bool bCommit = (cmd == TO_ACCOUNTSERVER_PROXY_COMMIT) ? true : false;
	S64 iPointsSpent = 0;
	const char *pSubbedKey = NULL;

	PERFINFO_AUTO_START_FUNC();
	ParserRecvStructSafe(parse_AccountProxyCommitRollbackRequest, pkt, &requestPacket);

	info = findAccountByID(requestPacket.uAccountID);

	if (requestPacket.pKey)
		pSubbedKey = AccountProxySubstituteKeyTokens(requestPacket.pKey, getProxyName(pProxy), getProxyCluster(pProxy), getProxyEnvironment(pProxy));

	if (requestPacket.pLock && pSubbedKey && info)
	{
		AccountKeyValueResult result = AKV_FAILURE;

		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "ProxyFinalize",
			PROXY_SOURCE_LOG_PAIR(pProxy)
			("accountid", "%u", requestPacket.uAccountID)
			("key", "%s", pSubbedKey)
			("commit", "%u", bCommit)
			("password", "%s", requestPacket.pLock));

		if (bCommit)
		{
			S64 iPreviousValue = 0;
			S64 iNewValue = 0;
			char **eaKeys = NULL;
			TransactionLogKeyValueChange **eaChanges = NULL;

			CurrencyPopulateKeyList(pSubbedKey, &eaKeys);
			AccountTransactionGetKeyValueChanges(info, eaKeys, &eaChanges);
			eaDestroyEx(&eaKeys, NULL);

			ANALYSIS_ASSUME(info);
			AccountKeyValue_Get(info, pSubbedKey, &iPreviousValue);

			// Attempt to commit key values.
			result = AccountKeyValue_Commit(info, pSubbedKey, requestPacket.pLock);

			if (eaSize(&eaChanges) > 0 && result == AKV_SUCCESS)
			{
				U32 uLogID = AccountTransactionOpen(info->uID, requestPacket.eTransactionType, pProxy ? getProxyName(pProxy) : NULL, TPROVIDER_AccountServer, NULL, NULL);
				AccountTransactionRecordKeyValueChanges(info->uID, uLogID, eaChanges, NULL);
				AccountTransactionFinish(info->uID, uLogID);
			}

			AccountTransactionFreeKeyValueChanges(&eaChanges);

			AccountKeyValue_Get(info, pSubbedKey, &iNewValue);

			iPointsSpent = iPreviousValue - iNewValue;
		}
		else
		{
			// Attempt to rollback key values.
			result = AccountKeyValue_Rollback(info, pSubbedKey, requestPacket.pLock);
		}
	}

	responsePacket = StructCreate(parse_AccountProxyAcknowledge);
	responsePacket->uAccountID = requestPacket.uAccountID;
	responsePacket->containerID = requestPacket.containerID;
	estrCopy2(&responsePacket->pKey, pSubbedKey);
	estrCopy2(&responsePacket->pLock, requestPacket.pLock);

	response = pktCreate(link, FROM_ACCOUNTSERVER_PROXY_ACK);
	ParserSendStructSafe(parse_AccountProxyAcknowledge, response, responsePacket);
	pktSend(&response);

	StructDestroy(parse_AccountProxyAcknowledge, responsePacket);
	StructDeInit(parse_AccountProxyCommitRollbackRequest, &requestPacket);
	PERFINFO_AUTO_STOP();
}

static void HandleRequestAllKeyValues(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyRequestAllKeyValues request = {0};
	U32 uAccountID, uPlayerID;
	AccountInfo *account;

	PERFINFO_AUTO_START_FUNC();
	ParserRecvStructSafe(parse_AccountProxyRequestAllKeyValues, pkt, &request);
	uAccountID = request.uAccountID;
	uPlayerID = request.uPlayerID;
	account = findAccountByID(uAccountID);

	if (account)
	{
		AccountProxyKeyValueInfoList *list = GetProxyKeyValueList(account);
		if (!uPlayerID)
		{
			Packet *packet = pktCreate(link, FROM_ACCOUNTSERVER_PROXY_ALL_KEY_VALUES);
			pktSendU32(packet, uAccountID);
			ParserSendStructSafe(parse_AccountProxyKeyValueInfoList, packet, list);
			pktSend(&packet);
			StructDestroy(parse_AccountProxyKeyValueInfoList, list);
		}
		else
		{
			AccountProxyKeyValueData* data = StructCreate(parse_AccountProxyKeyValueData);
			Packet *packet = pktCreate(link, FROM_ACCOUNTSERVER_PROXY_ALL_KEY_VALUES_PLAYERID);
			data->uAccountID = uAccountID;
			data->uPlayerID = uPlayerID;
			data->pList = list;
			ParserSendStructSafe(parse_AccountProxyKeyValueData, packet, data);
			pktSend(&packet);
			StructDestroy(parse_AccountProxyKeyValueData, data);
		}
	}

	StructDeInit(parse_AccountProxyRequestAllKeyValues, &request);
	PERFINFO_AUTO_STOP();
}

static void HandleRequestAccountIDByDisplayName(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyAccountIDFromDisplayNameRequest request = {0};
	AccountInfo *account;
	Packet *packet = pktCreate(link, FROM_ACCOUNTSERVER_PROXY_REQUEST_ACCOUNT_ID_BY_DISPLAY_NAME);
	AccountProxyAccountIDFromDisplayNameResponse *response = StructCreate(parse_AccountProxyAccountIDFromDisplayNameResponse);

	ParserRecvStructSafe(parse_AccountProxyAccountIDFromDisplayNameRequest, pkt, &request);
	account = findAccountByDisplayName(request.pDisplayName);

	estrCopy2(&response->pDisplayName, request.pDisplayName);
	response->uAccountID = account ? account->uID : 0;

	ParserSendStructSafe(parse_AccountProxyAccountIDFromDisplayNameResponse, packet, response);
	pktSend(&packet);

	StructDestroy(parse_AccountProxyAccountIDFromDisplayNameResponse, response);
	StructDeInit(parse_AccountProxyAccountIDFromDisplayNameRequest, &request);
}

static void HandleProtocolVersion(PACKET_PARAMS_PROTOTYPE)
{
	U32 uProtocolVersion = -1;

	if (pktCheckRemaining(pkt, 4))
		uProtocolVersion = pktGetU32(pkt);

	if (uProtocolVersion == ACCOUNT_PROXY_PROTOCOL_VERSION)
	{
		AccountProxyProductList *pProductList = NULL;
		AccountProxyDiscountList *pDiscountList = NULL;

		// Handle all the stuff that would normally go on the connect handler
		addProxy(link, uProtocolVersion);

		pProductList = GetProxyProductList();
		SendProductsToLink(link, pProductList);
		StructDestroy(parse_AccountProxyProductList, pProductList);

		pDiscountList = GetProxyDiscountList();
		SendDiscountsToLink(link, pDiscountList);
		StructDestroy(parse_AccountProxyDiscountList, pDiscountList);
	}
	else
	{
#define HandleProtocolVersionConnectedMessage "Account Proxy ignored because it is using protocol version %d and the account server is using %d.\n"
		log_printf(LOG_ACCOUNT_PROXY, HandleProtocolVersionConnectedMessage, uProtocolVersion, ACCOUNT_PROXY_PROTOCOL_VERSION);
		printf(HandleProtocolVersionConnectedMessage, uProtocolVersion, ACCOUNT_PROXY_PROTOCOL_VERSION);
#undef HandleProtocolVersionConnectedMessage
	}
}

static void HandleLogoutNotification(PACKET_PARAMS_PROTOTYPE)
{
	AccountLogoutNotification logout = {0};
	AccountInfo *pAccount = NULL;

	ParserRecvStructSafe(parse_AccountLogoutNotification, pkt, &logout);
	pAccount = findAccountByID(logout.uAccountID);

	if (pAccount)
	{
		AccountGameMetadata *pGameMetadata = NULL;
		accountAddPlayTime(pAccount, logout.pProductName, logout.pShardCategory, logout.uPlayTime);
		accountSetHighestLevel(pAccount, logout.pProductName, logout.pShardCategory, logout.uLevel);
	}

	StructDeInit(parse_AccountLogoutNotification, &logout);
}

static void HandleSimpleSet(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxySimpleSetRequest packetRequest = {0};
	AccountProxySimpleSetResponse *packetResponse;
	AccountInfo *info;
	AccountKeyValueResult result = AKV_FAILURE;
	Packet *response;
	S64 iSetValue = 0;
	const char *pSubbedKey = NULL;

	PERFINFO_AUTO_START_FUNC();
	packetResponse = StructCreate(parse_AccountProxySimpleSetResponse);
	ParserRecvStructSafe(parse_AccountProxySimpleSetRequest, pkt, &packetRequest);

	if (packetRequest.pValue)
	{
		iSetValue = atoi64(packetRequest.pValue);
	}
	else
	{
		iSetValue = packetRequest.iValue;
	}

	info = findAccountByID(packetRequest.uAccountID);
	
	if (packetRequest.pKey)
		pSubbedKey = AccountProxySubstituteKeyTokens(packetRequest.pKey, getProxyName(pProxy), getProxyCluster(pProxy), getProxyEnvironment(pProxy));

	if (info && pSubbedKey)
	{
		char *lock = NULL;

		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "ProxySimpleSet",
			PROXY_SOURCE_LOG_PAIR(pProxy)
			("accountid", "%u", packetRequest.uAccountID)
			("key", "%s", pSubbedKey)
			("change", "%u", (packetRequest.operation == AKV_OP_INCREMENT) ? 1 : 0)
			("value", "%"FORM_LL"d", iSetValue));

		ANALYSIS_ASSUME(info);

		switch (packetRequest.operation)
		{
		case AKV_OP_SET:
			result = AccountKeyValue_Set(info, pSubbedKey, iSetValue, &lock);
		xcase AKV_OP_INCREMENT:
			result = AccountKeyValue_Change(info, pSubbedKey, iSetValue, &lock);
		xdefault:
			devassertmsg(false, "Unsupported account key operation.");
			result = AKV_FAILURE;
		}

		if (result == AKV_SUCCESS)
		{
			result = AccountKeyValue_Commit(info, pSubbedKey, lock);
		}
	}

	packetResponse->requestID = packetRequest.requestID;
	packetResponse->result = result;

	response = pktCreate(link, FROM_ACCOUNTSERVER_PROXY_SIMPLE_SET);
	ParserSendStructSafe(parse_AccountProxySimpleSetResponse, response, packetResponse);
	pktSend(&response);

	StructDestroy(parse_AccountProxySimpleSetResponse, packetResponse);
	StructDeInit(parse_AccountProxySimpleSetRequest, &packetRequest);
	PERFINFO_AUTO_STOP();
}

static void GetAccountPlayedTime(SA_PARAM_NN_VALID const AccountPlayTime *pPlayTime, SA_PARAM_NN_VALID U32 *uFirstPlayedSS2000, SA_PARAM_NN_VALID U32 *uTotalPlayedSS2000)
{
	U32 uTrackedTotal = 0;

	if (pPlayTime->pCurrentMonth)
	{
		uTrackedTotal = pPlayTime->pCurrentMonth->uPlayTime;
	}

	EARRAY_CONST_FOREACH_BEGIN(pPlayTime->eaMonths, iMonth, iNumMonths);
	{
		uTrackedTotal += pPlayTime->eaMonths[iMonth]->uPlayTime;
	}
	EARRAY_FOREACH_END;

	if (uTrackedTotal < pPlayTime->uPlayTime)
	{
		// If the month totals add up to less than the overall total, they must have played before 01/28/2010
		// So just assume that's their first played time
		static struct tm earliestFirstPlayedTime = {0};

		if (!earliestFirstPlayedTime.tm_year)
		{
			earliestFirstPlayedTime.tm_year = 2010 - 1900;
			earliestFirstPlayedTime.tm_mon = 1 - 1;
			earliestFirstPlayedTime.tm_mday = 28;
		}

		*uFirstPlayedSS2000 = timeGetSecondsSince2000FromLocalTimeStruct(&earliestFirstPlayedTime);
	}
	else
	{
		// Otherwise, their first played day is the earliest day on which they played (duh!)
		PlayTimeEntry *pFirstPlayTime = NULL;

		if (eaSize(&pPlayTime->eaDays) > 0)
		{
			pFirstPlayTime = pPlayTime->eaDays[0];
		}
		else
		{
			pFirstPlayTime = pPlayTime->pCurrentDay;
		}

		if (pFirstPlayTime)
		{
			struct tm firstPlayedTime = {0};
			firstPlayedTime.tm_year = pFirstPlayTime->uYear - 1900;
			firstPlayedTime.tm_mon = pFirstPlayTime->uMonth - 1;
			firstPlayedTime.tm_mday = pFirstPlayTime->uDay;
			*uFirstPlayedSS2000 = timeGetSecondsSince2000FromLocalTimeStruct(&firstPlayedTime);
		}
		else
		{
			// But if there's no first day, then we have to assume that this is their first time playing, literally right now
			*uFirstPlayedSS2000 = timeSecondsSince2000();
		}
	}

	*uTotalPlayedSS2000 = pPlayTime->uPlayTime;
}

static void HandleGetPlayedTime(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyPlayedTimeRequest packetRequest = {0};
	AccountProxyPlayedTimeResponse packetResponse = {0};
	AccountInfo *pAccount = NULL;
	Packet *pResponsePacket = NULL;

	PERFINFO_AUTO_START_FUNC();
	ParserRecvStructSafe(parse_AccountProxyPlayedTimeRequest, pkt, &packetRequest);

	if (!packetRequest.uAccountID || nullStr(packetRequest.pProduct) || nullStr(packetRequest.pCategory))
		goto out;

	packetResponse.uAccountID = packetRequest.uAccountID;
	packetResponse.pProduct = strdup(packetRequest.pProduct);
	packetResponse.pCategory = strdup(packetRequest.pCategory);

	pAccount = findAccountByID(packetRequest.uAccountID);

	if (!pAccount)
		goto out;

	accountSetLastLogin(pAccount, packetRequest.pProduct, packetRequest.pCategory);

	{
		const AccountGameMetadata *pGameMetadata = accountGetGameMetadata(pAccount, packetRequest.pProduct, packetRequest.pCategory);

		if (pGameMetadata)
			GetAccountPlayedTime(&pGameMetadata->playtime, &packetResponse.uFirstPlayed, &packetResponse.uTotalPlayed);
	}

out:
	pResponsePacket = pktCreate(link, FROM_ACCOUNTSERVER_PROXY_PLAYED_TIME);
	ParserSendStructSafe(parse_AccountProxyPlayedTimeResponse, pResponsePacket, &packetResponse);
	pktSend(&pResponsePacket);

	StructDeInit(parse_AccountProxyPlayedTimeRequest, &packetRequest);
	StructDeInit(parse_AccountProxyPlayedTimeResponse, &packetResponse);
	PERFINFO_AUTO_STOP();
}

static void HandleGetLinkingStatus(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyLinkingStatusRequest packetRequest = {0};
	AccountProxyLinkingStatusResponse packetResponse = {0};
	AccountInfo *pAccount = NULL;
	Packet *response = NULL;

	PERFINFO_AUTO_START_FUNC();
	ParserRecvStructSafe(parse_AccountProxyLinkingStatusRequest, pkt, &packetRequest);

	if (!packetRequest.uAccountID)
		goto out;

	packetResponse.uAccountID = packetRequest.uAccountID;

	pAccount = findAccountByID(packetRequest.uAccountID);

	if (!pAccount)
		goto out;

	if (pAccount->pPWAccountName && *pAccount->pPWAccountName)
		packetResponse.bLinked = true;

	if (pAccount->bPWAutoCreated)
		packetResponse.bShadow = true;

out:
	response = pktCreate(link, FROM_ACCOUNTSERVER_PROXY_LINKING_STATUS);
	ParserSendStructSafe(parse_AccountProxyLinkingStatusResponse, response, &packetResponse);
	pktSend(&response);

	StructDeInit(parse_AccountProxyLinkingStatusRequest, &packetRequest);
	StructDeInit(parse_AccountProxyLinkingStatusResponse, &packetResponse);
	PERFINFO_AUTO_STOP();
}

static void HandleGetSubbedTime(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxySubbedTimeRequest packetRequest = {0};
	AccountProxySubbedTimeResponse packetResponse = {0};
	AccountInfo *pAccount = NULL;
	Packet *response = NULL;

	PERFINFO_AUTO_START_FUNC();
	ParserRecvStructSafe(parse_AccountProxySubbedTimeRequest, pkt, &packetRequest);

	// Make sure we were given a valid account ID
	if (!packetRequest.uAccountID)
		goto out; // Send the response packet and cleanup

	// Set the account ID to what we were given
	packetResponse.uAccountID = packetRequest.uAccountID;

	// Make sure we were given a valid product internal name
	if (!packetRequest.pProductInternalName || !*packetRequest.pProductInternalName)
		goto out; // Send the response packet and cleanup

	// Set the product internal name to what we were given
	packetResponse.pProductInternalName = strdup(packetRequest.pProductInternalName);

	// Find the account given
	pAccount = findAccountByID(packetRequest.uAccountID);

	// Make sure the account exists
	if (!pAccount)
		goto out; // Send the response packet and cleanup

	// Set the estimated total seconds
	packetResponse.uTotalSecondEstimate = productTotalSeconds(pAccount, packetRequest.pProductInternalName);

out:

	// Send the response (we should always do this)
	response = pktCreate(link, FROM_ACCOUNTSERVER_PROXY_SUBBED_TIME);
	ParserSendStructSafe(parse_AccountProxySubbedTimeResponse, response, &packetResponse);
	pktSend(&response);

	// Cleanup
	StructDeInit(parse_AccountProxySubbedTimeResponse, &packetResponse);
	StructDeInit(parse_AccountProxySubbedTimeRequest, &packetRequest);
	PERFINFO_AUTO_STOP();
}

void HandleRequestRecruitInfo(PACKET_PARAMS_PROTOTYPE)
{
	RequestRecruitInfo packetRequest = {0};
	const AccountInfo *pAccount = NULL;

	PERFINFO_AUTO_START_FUNC();
	ParserRecvStructSafe(parse_RequestRecruitInfo, pkt, &packetRequest);

	pAccount = findAccountByID(packetRequest.uAccountID);

	if (!pAccount)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	SendRecruitInfoToLink(link, pAccount);

	StructDeInit(parse_RequestRecruitInfo, &packetRequest);
	PERFINFO_AUTO_STOP();
}

static void HandleBeginRequest(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyBeginEndWalk walk = {0};
	
	ParserRecvStructSafe(parse_AccountProxyBeginEndWalk, pkt, &walk);

	if (pProxy)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "ProxyRegister",
			("source", "%s", walk.pProxy)
			("cluster", "%s", walk.pCluster ? walk.pCluster : "")
			("environment", "%s", walk.pEnvironment ? walk.pEnvironment : ""));

		setProxyName(pProxy, walk.pProxy);
		setProxyCluster(pProxy, walk.pCluster);
		setProxyEnvironment(pProxy, walk.pEnvironment);
	}

	StructDeInit(parse_AccountProxyBeginEndWalk, &walk);
}

static void HandleRequestPaymentMethods_Complete(bool bSuccess,
												 SA_PARAM_OP_STR const char *pDefaultCurrency,
												 SA_PARAM_NN_VALID PaymentMethodsRequest *pRequest,
												 SA_PARAM_OP_VALID EARRAY_OF(CachedPaymentMethod) eaPaymentMethods)
{
	Proxy *pProxy = findProxyByName(pRequest->pProxy);
	PaymentMethodsResponse response = {0};
	Packet *responsePkt = NULL;
	NetLink *pLink = NULL;

	if (!pProxy)
	{
		CRITICAL_NETOPS_ALERT("PAYMENT_METHODS_INVALID_PROXY", "Failed to find proxy %s responding to payment methods request. Couldn't return results!", pRequest->pProxy);
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	pLink = getProxyNetLink(pProxy);
	if (!pLink)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	response.uAccountID = pRequest->uAccountID;
	response.iCmdID = pRequest->iCmdID;

	response.bSuccess = bSuccess;
	response.pDefaultCurrency = StructAllocString(pDefaultCurrency);
	response.eaPaymentMethods = eaPaymentMethods;

	responsePkt = pktCreate(pLink, FROM_ACCOUNTSERVER_PROXY_PAYMENT_METHODS);
	ParserSendStructSafe(parse_PaymentMethodsResponse, responsePkt, &response);
	pktSend(&responsePkt);

	response.eaPaymentMethods = NULL;

	StructDeInit(parse_PaymentMethodsResponse, &response);

	PERFINFO_AUTO_STOP();
}

static void HandleRequestPaymentMethods_SteamInfo(bool bSuccess,
												  SA_PARAM_OP_STR const char *pCountry,
												  SA_PARAM_OP_STR const char *pState,
												  SA_PARAM_OP_STR const char *pCurrency,
												  SA_PARAM_OP_STR const char *pStatus,
												  PaymentMethodsRequest *pRequest)
{
	EARRAY_OF(CachedPaymentMethod) eaPaymentMethods = NULL;
	AccountInfo *pAccount = NULL;

	PERFINFO_AUTO_START_FUNC();

	pAccount = findAccountByID(pRequest->uAccountID);

	if (!pAccount)
	{
		HandleRequestPaymentMethods_Complete(false, NULL, pRequest, NULL);
	}
	else
	{
		eaCopy(&eaPaymentMethods, &pAccount->personalInfo.ppPaymentMethods);

		if (bSuccess)
		{
			NOCONST(CachedPaymentMethod) *pSteamMethod = StructCreateNoConst(parse_CachedPaymentMethod);

			estrCopy2(&pSteamMethod->VID, STEAM_PMID);
			estrCopy2(&pSteamMethod->currency, pCurrency);
			pSteamMethod->methodProvider = TPROVIDER_Steam;
			eaPush(&eaPaymentMethods, CONTAINER_RECONST(CachedPaymentMethod, pSteamMethod));
		}

		HandleRequestPaymentMethods_Complete(true, pAccount->defaultCurrency, pRequest, eaPaymentMethods);

		if (bSuccess)
		{
			NOCONST(CachedPaymentMethod) *pSteamMethod = eaPop(&eaPaymentMethods);
			StructDestroyNoConst(parse_CachedPaymentMethod, pSteamMethod);
		}

		eaDestroy(&eaPaymentMethods);
	}

	StructDestroy(parse_PaymentMethodsRequest, pRequest);

	PERFINFO_AUTO_STOP();
}

static void HandleRequestPaymentMethods(PACKET_PARAMS_PROTOTYPE)
{
	PaymentMethodsRequest *request = NULL;
	PaymentMethodsResponse response = {0};
	Packet *responsePkt = NULL;
	AccountInfo *pAccount = NULL;
	
	PERFINFO_AUTO_START_FUNC();

	request = StructCreate(parse_PaymentMethodsRequest);
	ParserRecvStructSafe(parse_PaymentMethodsRequest, pkt, request);

	pAccount = findAccountByID(request->uAccountID);
	
	if (pProxy && !request->pProxy)
	{
		request->pProxy = StructAllocString(getProxyName(pProxy));
	}

	// If this is a Steam user, then call GetUserInfo, and only return the payment methods once that's done
	if (pAccount && request->uSteamID && devassert(request->pProxy))
	{
		GetSteamUserInfo(request->uAccountID, request->uSteamID, request->pProxy, NULL, HandleRequestPaymentMethods_SteamInfo, request);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (pAccount)
	{
		EARRAY_OF(CachedPaymentMethod) eaPaymentMethods = NULL;

		eaCopy(&eaPaymentMethods, &pAccount->personalInfo.ppPaymentMethods);
		HandleRequestPaymentMethods_Complete(true, pAccount->defaultCurrency, request, eaPaymentMethods);
		eaDestroy(&eaPaymentMethods);
	}
	else
	{
		HandleRequestPaymentMethods_Complete(false, NULL, request, NULL);
	}

	StructDestroy(parse_PaymentMethodsRequest, request);

	PERFINFO_AUTO_STOP();
}

static void SendAuthCaptureResponse(SA_PARAM_NN_VALID NetLink *pLink,
									SA_PARAM_NN_VALID AuthCaptureResponse *pResponse,
									int ePacketType)
{
	Packet *responsePkt = pktCreate(pLink, ePacketType);
	ParserSendStructSafe(parse_AuthCaptureResponse, responsePkt, pResponse);
	pktSend(&responsePkt);
}

static void populateAuthCaptureResponseFromRequest(SA_PARAM_NN_VALID AuthCaptureResponse *pResponse,
												   SA_PARAM_NN_VALID const AuthCaptureRequest *pRequest)
{
	pResponse->uAccountID = pRequest->uAccountID;
	pResponse->iCmdID = pRequest->iCmdID;
	pResponse->requestID = pRequest->requestID;
	pResponse->uLockContainerID = pRequest->uLockContainerID;
	pResponse->pCurrency = strdup(pRequest->pCurrency);
}

static void HandleRequestAuthCapture_PurchaseComplete(PurchaseResult eResult,
													  SA_PARAM_OP_VALID BillingTransaction *pTrans,
													  PurchaseSession *pPurchaseSession,
													  AuthCaptureRequest *pRequest)
{
	AuthCaptureResponse response = {0};
	Proxy *pProxy = NULL;
	NetLink *pLink = NULL;

	PERFINFO_AUTO_START_FUNC();

	populateAuthCaptureResponseFromRequest(&response, pRequest);
	response.eResult = eResult;
	response.pOrderID = pPurchaseSession ? StructAllocString(PurchaseOrderID(pPurchaseSession)) : NULL;

	pProxy = findProxyByName(pRequest->pProxy);
	if (pProxy)
	{
		pLink = getProxyNetLink(pProxy);
	}

	if (pLink)
	{
		if (eResult == PURCHASE_RESULT_PENDING && pRequest->bAuthOnly)
		{
			response.uPurchaseID = PurchaseDelaySession(pPurchaseSession);

			if (!response.uPurchaseID)
			{
				response.eResult = PURCHASE_RESULT_DELAY_FAILURE;
				PurchaseProductFinalize(pPurchaseSession, false, NULL, NULL);
			}
			else
			{
				response.pLock = strdup(PurchaseLockString(pPurchaseSession));
			}
		}

		SendAuthCaptureResponse(pLink, &response, FROM_ACCOUNTSERVER_PROXY_AUTHCAPTURE);
	}
	else
	{
		AccountInfo *pAccount = findAccountByID(pRequest->uAccountID);

		if (devassert(pAccount) && devassert(pRequest->pProxy))
		{
			accountLog(pAccount, "Result of purchase from proxy %s could not be communicated to the proxy.", pRequest->pProxy);

			AssertOrAlert("ACCOUNTSERVER_PROXY_PURCHASE_RESULT_UNCOMMUNICATED", "The account %s was attempting to purchase something from the Account Proxy %s, "
				"but the proxy is no longer connected.  The purchase result was not communicated to the game server, however it was %s "
				"on the Account Server.  This account should be looked at by CS to ensure everything is correct.",
				pAccount->accountName, pRequest->pProxy, pRequest->bAuthOnly ? "authorized" : "completed");
		}
	}

	StructDestroy(parse_AuthCaptureRequest, pRequest);
	StructDeInit(parse_AuthCaptureResponse, &response);

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleRequestAuthCapture(PACKET_PARAMS_PROTOTYPE)
{
	AuthCaptureRequest *pRequest = NULL;
	AuthCaptureResponse response = {0};
	Packet *responsePkt = NULL;
	AccountInfo *pAccount = NULL;
	bool bProxyIsValid = false;

	PERFINFO_AUTO_START_FUNC();

	pRequest = StructCreate(parse_AuthCaptureRequest);
	ParserRecvStructSafe(parse_AuthCaptureRequest, pkt, pRequest);

	populateAuthCaptureResponseFromRequest(&response, pRequest);

	if (pProxy)
	{
		// Normally, we could get the name from this packet and initialize
		// the proxy with it, but we would rather fail than take people's money
		// multiple times.
		/*if (pRequest->pProxy)
		{
			setProxyName(pProxy, pRequest->pProxy);
		}*/

		bProxyIsValid = proxyHandshakeCompleted(pProxy);
	}

	if (bProxyIsValid)
	{
		pAccount = findAccountByID(pRequest->uAccountID);
		if (pAccount)
		{
			PaymentMethod paymentMethod = {0};
			PurchaseDetails purchaseDetails = {0};

			paymentMethod.VID = pRequest->pPaymentMethodID;

			if (pRequest->eProvider == TPROVIDER_Steam)
			{
				if (pRequest->bAuthOnly && !devassert(pRequest->uSteamID))
				{
					response.eResult = PURCHASE_RESULT_MISSING_ACCOUNT;
					SendAuthCaptureResponse(link, &response, FROM_ACCOUNTSERVER_PROXY_AUTHCAPTURE);
					StructDestroy(parse_AuthCaptureRequest, pRequest);
					StructDeInit(parse_AuthCaptureResponse, &response);
					return;
				}

				PopulatePurchaseDetailsSteam(&purchaseDetails,
					getProxyName(pProxy),
					getProxyCluster(pProxy),
					getProxyEnvironment(pProxy),
					pRequest->pIP,
					pRequest->pOrderID,
					pRequest->pTransactionID,
					pRequest->uSteamID,
					pRequest->pLocCode,
					false);
			}
			else
			{
				PopulatePurchaseDetails(&purchaseDetails,
					getProxyName(pProxy),
					getProxyCluster(pProxy),
					getProxyEnvironment(pProxy),
					pRequest->pIP,
					&paymentMethod,
					NULL,
					pRequest->pBankName);
			}

			EARRAY_FOREACH_BEGIN(pRequest->eaItems, iItem);
			{
				TransactionItem *pItem = pRequest->eaItems[iItem];
				char *pPrice = NULL;

				if (pItem->pPrice)
				{
					estrStackCreate(&pPrice);
					estrFromMoneyRaw(&pPrice, pItem->pPrice);
				}

				SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "ProxyAuthCapture",
					PROXY_SOURCE_LOG_PAIR(pProxy)
					("requestid", "%u", pRequest->requestID)
					("accountid", "%u", pRequest->uAccountID)
					("productid", "%u", pItem->uProductID)
					("currency", "%s", pRequest->pCurrency)
					("price", "%s", pPrice ? pPrice : "0")
					("overridediscount", "%u", pItem->uOverridePercentageDiscount)
					("provider", "%s", StaticDefineInt_FastIntToString(TransactionProviderEnum, pRequest->eProvider))
					("authonly", "%u", pRequest->bAuthOnly));

				estrDestroy(&pPrice);
			}
			EARRAY_FOREACH_END;

			if (pRequest->bAuthOnly)
			{
				response.eResult = PurchaseProductLock(pAccount,
					pRequest->eaItems,
					pRequest->pCurrency,
					0,
					pRequest->bVerifyPrice,
					&purchaseDetails,
					HandleRequestAuthCapture_PurchaseComplete,
					pRequest);
			}
			else // Auth and capture at once
			{
				response.eResult = PurchaseProduct(pAccount,
					pRequest->eaItems,
					pRequest->pCurrency,
					0,
					pRequest->bVerifyPrice,
					&purchaseDetails,
					HandleRequestAuthCapture_PurchaseComplete,
					pRequest);
			}

			StructDeInit(parse_PurchaseDetails, &purchaseDetails);
			//pRequest will be destroyed by HandleRequestAuthCapture_PurchaseComplete
		}
		else // Account is invalid
		{
			response.eResult = PURCHASE_RESULT_MISSING_ACCOUNT;
			SendAuthCaptureResponse(link, &response, FROM_ACCOUNTSERVER_PROXY_AUTHCAPTURE);
			StructDestroy(parse_AuthCaptureRequest, pRequest);
		}
	}
	else // Proxy is invalid
	{
		response.eResult = PURCHASE_RESULT_NOT_READY;
		SendAuthCaptureResponse(link, &response, FROM_ACCOUNTSERVER_PROXY_AUTHCAPTURE);
		StructDestroy(parse_AuthCaptureRequest, pRequest);
	}
	
	StructDeInit(parse_AuthCaptureResponse, &response);

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleRequestCapture_CaptureComplete(PurchaseResult eResult,
												 SA_PARAM_OP_VALID BillingTransaction *pTrans,
												 PurchaseSession *pPurchaseSession,
												 SA_PARAM_NN_VALID CaptureRequest *pRequest)
{
	AuthCaptureResponse response = {0};
	Proxy *pProxy = findProxyByName(pRequest->pProxy);
	NetLink *pLink = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (pProxy)
	{
		pLink = getProxyNetLink(pProxy);
	}

	if (pLink)
	{
		response.uAccountID = pRequest->uAccountID;
		response.requestID = pRequest->requestID;
		response.uLockContainerID = pRequest->uLockContainerID;
		response.pOrderID = StructAllocString(pRequest->pOrderID);
		response.eResult = eResult;

		SendAuthCaptureResponse(pLink, &response, FROM_ACCOUNTSERVER_PROXY_CAPTURE);
		StructDeInit(parse_AuthCaptureResponse, &response);
		StructDestroy(parse_CaptureRequest, pRequest);
	}
	PERFINFO_AUTO_STOP();
}

static void HandleRequestCapture_RetrievedSession(PurchaseResult eResult,
												  SA_PARAM_OP_VALID BillingTransaction *pTrans,
												  PurchaseSession *pPurchaseSession,
												  SA_PARAM_NN_VALID CaptureRequest *pRequest)
{
	PERFINFO_AUTO_START_FUNC();
	if (devassert(eResult == PURCHASE_RESULT_RETRIEVED))
	{
		PurchaseProductFinalize(pPurchaseSession, pRequest->bCapture, HandleRequestCapture_CaptureComplete, pRequest);
	}
	else
	{
		HandleRequestCapture_CaptureComplete(eResult, pTrans, pPurchaseSession, pRequest);
	}
	PERFINFO_AUTO_STOP();
}

static void HandleRequestCapture(PACKET_PARAMS_PROTOTYPE)
{
	AuthCaptureResponse response = {0};
	CaptureRequest *request = NULL;
	AccountInfo *pAccount = NULL;
	BillingTransaction *pTrans = NULL;
	
	PERFINFO_AUTO_START_FUNC();

	request = StructCreate(parse_CaptureRequest);
	ParserRecvStructSafe(parse_CaptureRequest, pkt, request);

	pAccount = findAccountByID(request->uAccountID);
	if (pAccount)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "ProxyCapture",
			PROXY_SOURCE_LOG_PAIR(pProxy)
			("requestid", "%u", request->requestID)
			("accountid", "%u", request->uAccountID)
			("purchaseid", "%u", request->uPurchaseID)
			("capture", "%u", request->bCapture));

		pTrans = PurchaseRetrieveDelayedSession(pAccount, request->uPurchaseID, NULL, HandleRequestCapture_RetrievedSession, request);

		if (pTrans)
		{
			PERFINFO_AUTO_STOP();
			return;
		}

		response.eResult = PURCHASE_RESULT_INVALID_PURCHASE_ID;
	}
	else
	{
		response.eResult = PURCHASE_RESULT_MISSING_ACCOUNT;
	}

	response.uAccountID = request->uAccountID;
	response.requestID = request->requestID;
	response.uLockContainerID = request->uLockContainerID;
	response.pOrderID = StructAllocString(request->pOrderID);

	SendAuthCaptureResponse(link, &response, FROM_ACCOUNTSERVER_PROXY_CAPTURE);
	StructDeInit(parse_AuthCaptureResponse, &response);
	StructDestroy(parse_CaptureRequest, request);

	PERFINFO_AUTO_STOP();
}

// This generates tickets that IGNORE machine locking under the assumption that the user is already online
static void HandleRequestCreateTicketForOnlineAccount(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyRequestAuthTicketForOnlinePlayer request = { 0 };
	AccountInfo *pAccount;
	Packet *pResponsePacket;

	PERFINFO_AUTO_START_FUNC();

	ParserRecvStructSafe(parse_AccountProxyRequestAuthTicketForOnlinePlayer, pkt, &request);

	pAccount = findAccountByID(request.uAccountID);
	if (pAccount)
	{
		request.uTicketID = AccountCreateTicket(ACCOUNTLOGINTYPE_Default, pAccount, NULL, request.uIp, true);
	}
	else
	{
		request.uTicketID = 0;
	}

	pResponsePacket = pktCreate(link, FROM_ACCOUNTSERVER_PROXY_TICKET_FOR_ONLINE_ACCOUNT);
	ParserSendStructSafe(parse_AccountProxyRequestAuthTicketForOnlinePlayer, pResponsePacket, &request);
	pktSend(&pResponsePacket);

	StructDeInit(parse_AccountProxyRequestAuthTicketForOnlinePlayer, &request);

	PERFINFO_AUTO_STOP_FUNC();
}

static void WebSrvGameEvent_CB(struct JSONRPCState *state, void * userData)
{
	if (state->error)
	{
		ErrorOrAlert("ACCOUNTSERVER_WEBSRV", "JSONRPC to WebSrv failed: %s.", state->error);
	}
}

static void HandleRequestWebSrvGameEvent(PACKET_PARAMS_PROTOTYPE)
{
	char fullEventName[128];
	AccountProxyWebSrvGameEvent request = { 0 };
	AccountInfo *pAccount;
	const AccountServerConfig *config = GetAccountServerConfig();

	PERFINFO_AUTO_START_FUNC();

	if (nullStr(config->pWebSrvAddress) || !config->iWebSrvPort)
	{
		ErrorOrAlert("ACCOUNTSERVER_WEBSRV", "Received a WebSrv Game Event with no WebSrv address configured.");
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	ParserRecvStructSafe(parse_AccountProxyWebSrvGameEvent, pkt, &request);
	pAccount = findAccountByID(request.uAccountID);
	if (!pAccount || nullStr(request.pProductShortName) || nullStr(request.pEventName))
	{
		StructDeInit(parse_AccountProxyWebSrvGameEvent, &request);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	sprintf(fullEventName, "%s_%s", request.pProductShortName, request.pEventName);

	accountSendEventEmail(pAccount, request.pLang, fullEventName, request.keyValueList, WebSrvGameEvent_CB, NULL);
	StructDeInit(parse_AccountProxyWebSrvGameEvent, &request);

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleCreateCurrency(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyCreateCurrencyRequest request = {0};

	// Don't allow this on a live-like AS
	if (isAccountServerLikeLive())
		return;

	ParserRecvStructSafe(parse_AccountProxyCreateCurrencyRequest, pkt, &request);

	if (!request.pName || VirtualCurrency_GetByName(request.pName))
		return;

	VirtualCurrency_UpdateCurrency(request.pName, request.pGame, request.pEnvironment, 0, 0, VCRT_Promotional, false, NULL);
}

static void HandleMoveRequest(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyMoveRequest request = {0};
	AccountProxyMoveResponse response = {0};
	AccountInfo *pSrcAccount = NULL;
	AccountInfo *pDestAccount = NULL;
	Packet *pResponsePacket = NULL;

	PERFINFO_AUTO_START_FUNC();

	ParserRecvStructSafe(parse_AccountProxyMoveRequest, pkt, &request);

	response.eResult = AKV_FAILURE;
	response.uRequestID = request.uRequestID;
	response.uLockContainerID = request.uLockContainerID;

	pSrcAccount = findAccountByID(request.uSrcAccountID);
	pDestAccount = findAccountByID(request.uDestAccountID);

	if (pSrcAccount && pDestAccount && request.pSrcKey && request.pDestKey)
	{
		SERVLOG_PAIRS(LOG_ACCOUNT_KEY_VALUE, "Move",
			("source", "%s", pProxy ? getProxyName(pProxy) : "proxy")
			("srcaccountid", "%u", request.uSrcAccountID)
			("srckey", "%s", request.pSrcKey)
			("destaccountid", "%u", request.uDestAccountID)
			("destkey", "%s", request.pDestKey)
			("value", "%"FORM_LL"d", request.iValue));

		response.eResult = AccountKeyValue_Move(pSrcAccount, request.pSrcKey, pDestAccount, request.pDestKey, request.iValue, &response.pLock);
	}

	pResponsePacket = pktCreate(link, FROM_ACCOUNTSERVER_PROXY_MOVE_RESULT);
	ParserSendStructSafe(parse_AccountProxyMoveResponse, pResponsePacket, &response);
	pktSend(&pResponsePacket);

	StructDeInit(parse_AccountProxyMoveRequest, &request);
	StructDeInit(parse_AccountProxyMoveResponse, &response);

	PERFINFO_AUTO_STOP();
}

static void HandleCommitRollbackMoveRequest(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyCommitRollbackMoveRequest request = {0};
	AccountProxyAcknowledge response = {0};
	AccountInfo *pSrcAccount = NULL;
	AccountInfo *pDestAccount = NULL;
	TransactionLogKeyValueChange **eaSrcChanges = NULL;
	TransactionLogKeyValueChange **eaDestChanges = NULL;
	Packet *pResponsePacket = NULL;
	bool bCommit = (cmd == TO_ACCOUNTSERVER_PROXY_COMMIT_MOVE) ? true : false;

	PERFINFO_AUTO_START_FUNC();
	ParserRecvStructSafe(parse_AccountProxyCommitRollbackMoveRequest, pkt, &request);

	pSrcAccount = findAccountByID(request.uSrcAccountID);
	pDestAccount = findAccountByID(request.uDestAccountID);

	if (!pSrcAccount || !pDestAccount || nullStr(request.pSrcKey) || nullStr(request.pDestKey))
	{
		goto end;
	}

	SERVLOG_PAIRS(LOG_ACCOUNT_KEY_VALUE, "FinalizeMove",
		("source", "%s", pProxy ? getProxyName(pProxy) : "proxy")
		("commit", "%u", bCommit)
		("srcaccountid", "%u", request.uSrcAccountID)
		("srckey", "%s", request.pSrcKey)
		("destaccountid", "%u", request.uDestAccountID)
		("destkey", "%s", request.pDestKey)
		("lock", "%s", request.pLock));

	if (request.uSrcAccountID == request.uDestAccountID)
	{
		char **eaKeys = NULL;
		CurrencyPopulateKeyList(request.pSrcKey, &eaKeys);
		CurrencyPopulateKeyList(request.pDestKey, &eaKeys);
		AccountTransactionGetKeyValueChanges(pSrcAccount, eaKeys, &eaSrcChanges);
		eaDestroyEx(&eaKeys, NULL);
	}
	else if (request.uSrcAccountID != request.uDestAccountID)
	{
		char **eaKeys = NULL;

		CurrencyPopulateKeyList(request.pSrcKey, &eaKeys);
		AccountTransactionGetKeyValueChanges(pSrcAccount, eaKeys, &eaSrcChanges);
		eaDestroyEx(&eaKeys, NULL);

		CurrencyPopulateKeyList(request.pDestKey, &eaKeys);
		AccountTransactionGetKeyValueChanges(pDestAccount, eaKeys, &eaDestChanges);
		eaDestroyEx(&eaKeys, NULL);
	}

	if (bCommit && AccountKeyValue_Commit(pSrcAccount, request.pSrcKey, request.pLock) == AKV_SUCCESS)
	{
		char *pReason = NULL;
		U32 uSrcLogID = 0;
		U32 uDestLogID = 0;

		if (eaSrcChanges)
		{
			uSrcLogID = AccountTransactionOpen(pSrcAccount->uID, request.eTransactionType, pProxy ? getProxyName(pProxy) : NULL, TPROVIDER_AccountServer, NULL, NULL);
		}

		if (request.uSrcAccountID != request.uDestAccountID && eaDestChanges)
		{
			uDestLogID = AccountTransactionOpen(pDestAccount->uID, request.eTransactionType, pProxy ? getProxyName(pProxy) : NULL, TPROVIDER_AccountServer, NULL, NULL);
		}

		if (uSrcLogID)
		{
			if (uDestLogID)
			{
				if (request.uSrcAccountID == request.uDestAccountID)
				{
					estrPrintf(&pReason, "MoveTo:%u:%u", pSrcAccount->uID, uSrcLogID);
				}
				else
				{
					estrPrintf(&pReason, "MoveTo:%u:%u", pDestAccount->uID, uDestLogID);
				}
			}

			AccountTransactionRecordKeyValueChanges(pSrcAccount->uID, uSrcLogID, eaSrcChanges, pReason);
			AccountTransactionFinish(pSrcAccount->uID, uSrcLogID);
			estrDestroy(&pReason);
		}

		if (uDestLogID)
		{
			if (uSrcLogID)
			{
				estrPrintf(&pReason, "MoveFrom:%u:%u", pSrcAccount->uID, uSrcLogID);
			}
			
			AccountTransactionRecordKeyValueChanges(pDestAccount->uID, uDestLogID, eaDestChanges, pReason);
			AccountTransactionFinish(pDestAccount->uID, uDestLogID);
			estrDestroy(&pReason);
		}
	}
	else
	{
		AccountKeyValue_Rollback(pSrcAccount, request.pSrcKey, request.pLock);
	}

end:
	AccountTransactionFreeKeyValueChanges(&eaSrcChanges);
	AccountTransactionFreeKeyValueChanges(&eaDestChanges);

	estrCopy(&response.pLock, &request.pLock);
	response.containerID = request.uLockContainerID;

	pResponsePacket = pktCreate(link, FROM_ACCOUNTSERVER_PROXY_ACK);
	ParserSendStructSafe(parse_AccountProxyAcknowledge, pResponsePacket, &response);
	pktSend(&pResponsePacket);
	
	StructDeInit(parse_AccountProxyCommitRollbackMoveRequest, &request);
	StructDeInit(parse_AccountProxyAcknowledge, &response);
	PERFINFO_AUTO_STOP();
}

static void HandleGetAccountData(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyAccountDataRequest request = {0};
	AccountProxyAccountDataResponse response = {0};
	AccountInfo *pAccount = NULL;
	Packet *pResponsePacket = NULL;

	PERFINFO_AUTO_START_FUNC();
	ParserRecvStructSafe(parse_AccountProxyAccountDataRequest, pkt, &request);

	pAccount = findAccountByID(request.uAccountID);
	response.uAccountID = request.uAccountID;

	if (pAccount && !nullStr(request.pProduct) && !nullStr(request.pCategory) && !nullStr(request.pShard))
	{
		const AccountGameMetadata *pGameMetadata = NULL;
		ANALYSIS_ASSUME(pAccount);

		accountSetLastLogin(pAccount, request.pProduct, request.pCategory);

		pGameMetadata = accountGetGameMetadata(pAccount, request.pProduct, request.pCategory);

		if (pGameMetadata)
		{
			GetAccountPlayedTime(&pGameMetadata->playtime, &response.uFirstPlayedSS2000, &response.uTotalPlayedSS2000);
			response.uLastLoginSS2000 = pGameMetadata->uLastLogin;
			response.uLastLogoutSS2000 = pGameMetadata->uLastLogout;
			response.uHighestLevel = pGameMetadata->uHighestLevel;
			response.uNumCharacters = pGameMetadata->uNumCharacters;
		}

		response.uSubscribedSeconds = productTotalSeconds(pAccount, request.pProduct);
		response.bLinkedAccount = !nullStr(pAccount->pPWAccountName);
		response.bShadowAccount = pAccount->bPWAutoCreated;
		response.bBilled = pAccount->bBilled;
		response.pKeyValues = GetProxyKeyValueList(pAccount);
	}

	pResponsePacket = pktCreate(link, FROM_ACCOUNTSERVER_PROXY_ACCOUNT_DATA);
	ParserSendStructSafe(parse_AccountProxyAccountDataResponse, pResponsePacket, &response);
	pktSend(&pResponsePacket);

	StructDeInit(parse_AccountProxyAccountDataRequest, &request);
	StructDeInit(parse_AccountProxyAccountDataResponse, &response);
	PERFINFO_AUTO_STOP();
}

static void HandleNumCharacters(PACKET_PARAMS_PROTOTYPE)
{
	AccountProxyNumCharactersRequest request = {0};
	AccountInfo *pAccount = NULL;

	PERFINFO_AUTO_START_FUNC();
	ParserRecvStructSafe(parse_AccountProxyNumCharactersRequest, pkt, &request);

	pAccount = findAccountByID(request.uAccountID);

	if (pAccount && !nullStr(request.pProduct) && !nullStr(request.pCategory))
	{
		accountSetNumCharacters(pAccount, request.pProduct, request.pCategory, request.uNumCharacters, request.bChange);
	}

	StructDeInit(parse_AccountProxyNumCharactersRequest, &request);
	PERFINFO_AUTO_STOP();
}

// Entry point of all packets
static void AccountProxyMsgHandler(SA_PARAM_NN_VALID Packet *pkt, int cmd, SA_PARAM_NN_VALID NetLink *link, SA_PARAM_OP_VALID void *userData)
{
	static StaticCmdPerf cmdPerf[TO_ACCOUNTSERVER_PROXY_MAX];
	Proxy *pProxy = NULL;

	// Record packet processing performance.
	PERFINFO_AUTO_START_FUNC();
	if (cmd >= 0 && cmd < ARRAY_SIZE(cmdPerf))
	{
		if(!cmdPerf[cmd].name)
		{
			char buffer[100];
			sprintf(buffer, "Cmd:%s (%d)", StaticDefineIntRevLookupNonNull(AccountServerProxyCmdEnum, cmd), cmd);
			cmdPerf[cmd].name = strdup(buffer);
		}
		PERFINFO_AUTO_START_STATIC(cmdPerf[cmd].name, &cmdPerf[cmd].pi, 1);
	}
	else
		PERFINFO_AUTO_START("Cmd:Unknown", 1);

	pProxy = findProxyByLink(link);

	SERVLOG_PAIRS(LOG_ACCOUNT_PROXY, "ProxyCmd",
		PROXY_SOURCE_LOG_PAIR(pProxy)
		("cmd", "%s", StaticDefineInt_FastIntToString(AccountServerProxyCmdEnum, cmd)));

	switch (cmd)
	{
	xcase TO_ACCOUNTSERVER_PROXY_SET:
		HandleSetRequest(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_COMMIT:
	case TO_ACCOUNTSERVER_PROXY_ROLLBACK:
		HandleCommitRollbackRequest(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_BEGIN:
		HandleBeginRequest(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_REQUEST_ALL_KEY_VALUES:
		HandleRequestAllKeyValues(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_REQUEST_ACCOUNT_ID_BY_DISPLAY_NAME:
		HandleRequestAccountIDByDisplayName(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_PROTOCOL_VERSION:
		HandleProtocolVersion(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_LOGOUT_NOTIFICATION:
		HandleLogoutNotification(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_SIMPLE_SET:
		HandleSimpleSet(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_GET_SUBBED_TIME:
		HandleGetSubbedTime(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_REQUEST_RECRUIT_INFO:
		HandleRequestRecruitInfo(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_PAYMENT_METHODS:
		HandleRequestPaymentMethods(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_AUTHCAPTURE:
		HandleRequestAuthCapture(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_CAPTURE:
		HandleRequestCapture(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_CREATE_TICKET_FOR_ONLINE_ACCOUNT:
		HandleRequestCreateTicketForOnlineAccount(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_GET:
		HandleGetRequest(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_GET_LINKING_STATUS:
		HandleGetLinkingStatus(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_EVENT_REQUEST:
		HandleRequestWebSrvGameEvent(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_CREATE_CURRENCY:
		HandleCreateCurrency(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_MOVE:
		HandleMoveRequest(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_COMMIT_MOVE:
	case TO_ACCOUNTSERVER_PROXY_ROLLBACK_MOVE:
		HandleCommitRollbackMoveRequest(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_PLAYED_TIME:
		HandleGetPlayedTime(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_ACCOUNT_DATA:
		HandleGetAccountData(PACKET_PARAMS);
	xcase TO_ACCOUNTSERVER_PROXY_NUM_CHARACTERS:
		HandleNumCharacters(PACKET_PARAMS);
	}

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}


/************************************************************************/
/* Connection handlers                                                  */
/************************************************************************/

// Executed when an account proxy connects
static int AccountProxyConnectHandler(SA_PARAM_NN_VALID NetLink* link, SA_PARAM_OP_VALID void *userData)
{
#define AccountProxyConnectHandlerConnectedMessage "Account Proxy connected.\n"
	log_printf(LOG_ACCOUNT_PROXY, AccountProxyConnectHandlerConnectedMessage);
	printf(AccountProxyConnectHandlerConnectedMessage);
#undef AccountProxyConnectHandlerConnectedMessage
	linkSetTimeout(link, 60);
	SendProtocolVersionToLink(link);
	return 0;
}

// Executed when an account proxy disconnects
static int AccountProxyDisconnectHandler(SA_PARAM_NN_VALID NetLink* link, SA_PARAM_OP_VALID void *userData)
{
	Proxy *pProxy = findProxyByLink(link);

#define AccountProxyDisconnectHandlerDisconnectedMessage "Account Proxy disconnected.\n"
	log_printf(LOG_ACCOUNT_PROXY, AccountProxyDisconnectHandlerDisconnectedMessage);
	printf(AccountProxyDisconnectHandlerDisconnectedMessage);
#undef AccountProxyDisconnectHandlerDisconnectedMessage

	if (pProxy)
	{
		disconnectProxy(pProxy);
	}
	
	return 0;
}


/************************************************************************/
/* Public functions                                                     */
/************************************************************************/

// Initialize the account proxy server interface
void AccountProxyServerInit(void)
{
	commListen(commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH, DEFAULT_ACCOUNTPROXYSERVER_PORT, AccountProxyMsgHandler, 
		AccountProxyConnectHandler, AccountProxyDisconnectHandler, 0);
}

// Clear the product cache on all account proxies
void ClearAccountProxyProductCache(void)
{
	AccountProxyProductList *pProductList = NULL;

	PERFINFO_AUTO_START_FUNC();

	pProductList = GetProxyProductList();
	forEachProxy(SendProductsToProxy, pProductList);
	StructDestroy(parse_AccountProxyProductList, pProductList);

	PERFINFO_AUTO_STOP();
}

// Clear the discount cache on all account proxies
void ClearAccountProxyDiscountCache(void)
{
	AccountProxyDiscountList *pDiscountList = NULL;

	PERFINFO_AUTO_START_FUNC();

	pDiscountList = GetProxyDiscountList();
	forEachProxy(SendDiscountsToProxy, pDiscountList);
	StructDestroy(parse_AccountProxyDiscountList, pDiscountList);

	PERFINFO_AUTO_STOP();
}

// Send a single key-value to all proxies
void SendKeyValueToAllProxies(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey)
{
	SendKeyValueInfo info = {0};

	PERFINFO_AUTO_START_FUNC();

	info.pKey = pKey;
	info.pAccount = pAccount;
	forEachProxy(SendKeyValueToProxy, &info);

	PERFINFO_AUTO_STOP_FUNC();
}

// Sent a request for the removal of a single key-value to all proxies
void SendKeyRemoveToAllProxies(U32 uAccountID, SA_PARAM_NN_STR const char *pKey)
{
	SendKeyRemoveInfo info = {0};

	PERFINFO_AUTO_START_FUNC();

	info.pKey = pKey;
	info.uAccountID = uAccountID;
	forEachProxy(SendKeyRemoveToProxy, &info);

	PERFINFO_AUTO_STOP_FUNC();
}

// Send all recruit info to all proxies for an account
void SendRecruitInfoToAllProxies(SA_PARAM_NN_VALID const AccountInfo *pAccount)
{
	PERFINFO_AUTO_START_FUNC();

	forEachProxy(SendRecruitInfoToProxy, (void *)pAccount);

	PERFINFO_AUTO_STOP_FUNC();
}

// Send a display name update to all proxies
void SendDisplayNameToAllProxies(const AccountInfo *pAccount)
{
	PERFINFO_AUTO_START_FUNC();
	forEachProxy(SendDisplayNameToProxy, (void *)pAccount);
	PERFINFO_AUTO_STOP();
}

// Convert key-value pairs on an account to the account proxy version
SA_RET_NN_VALID AccountProxyKeyValueInfoList *GetProxyKeyValueList(SA_PARAM_NN_VALID AccountInfo *pAccount)
{
	AccountProxyKeyValueInfoList *list = NULL;
	STRING_EARRAY eaKeys = NULL;

	PERFINFO_AUTO_START_FUNC();

	list = StructCreate(parse_AccountProxyKeyValueInfoList);

	eaKeys = AccountKeyValue_GetAccountKeyList(pAccount);

	EARRAY_CONST_FOREACH_BEGIN(eaKeys, iCurKey, iNumKeys);
	{
		AccountProxyKeyValueInfo *item = ConvertKeyValuePair(pAccount, eaKeys[iCurKey]);

		if (item)
		{
			eaPush(&list->ppList, item);
		}
	}
	EARRAY_FOREACH_END;

	AccountKeyValue_DestroyAccountKeyList(&eaKeys);

	PERFINFO_AUTO_STOP_FUNC();

	return list;
}

U32 AccountProxyLastSeen(SA_PARAM_NN_STR const char *pProxyName)
{
	Proxy *pProxy = NULL;
	U32 uTime = 0;
	
	PERFINFO_AUTO_START_FUNC();

	pProxy = findProxyByName(pProxyName);

	if (pProxy)
	{
		uTime = getProxyLastSeenTime(pProxy);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return uTime;
}