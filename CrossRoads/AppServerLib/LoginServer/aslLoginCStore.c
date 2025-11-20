
#include "aslLoginServer.h"
#include "aslLoginCStore.h"
#include "aslLoginCharacterSelect.h"

#include "AccountProxyCommon.h"
#include "GameAccountData\GameAccountData.h"
#include "GlobalTypes.h"
#include "logging.h"
#include "LoginCommon.h"
#include "Microtransactions.h"
#include "Money.h"
#include "objTransactions.h"
#include "ResourceManager.h"
#include "SteamCommon.h"
#include "SteamCommonServer.h"
#include "NotifyCommon.h"
#include "net.h"
#include "aslLogin2_StateMachine.h"
#include "SteamCommon.h"
#include "SteamCommonServer.h"
#include "structNet.h"

#include "AccountDataCache_h_ast.h"
#include "accountnet_h_ast.h"
#include "GameAccountData_h_ast.h"
#include "Microtransactions_h_ast.h"
#include "SteamCommon_h_ast.h"

static void PurchaseCStoreProduct(U32 iAccountID, SA_PARAM_NN_STR const char *pCategory, U32 uProductID, SA_PARAM_OP_VALID const Money *pExpectedPrice, SA_PARAM_OP_STR const char *pPaymentMethodVID)
{
	MicroTransactionCategory *pMTCategory = microtrans_CategoryFromStr(pCategory);
	char pcIP[17];
    Login2State *loginState = aslLogin2_GetActiveLoginStateByAccountID(iAccountID);

    if( !loginState || !loginState->netLink || loginState->loginFailed )
	{
		log_printf(LOG_LOGIN, "Command %s returned for invalid account id %d", __FUNCTION__, iAccountID);
		return;
	}

	linkGetIpStr(loginState->netLink, pcIP, sizeof(pcIP));

	if(pMTCategory)
	{
		if(iAccountID && uProductID)
		{
			char pcBuffer[128];
			sprintf(pcBuffer, "%s.%s", microtrans_GetShardCategoryPrefix(), pMTCategory->pchName);
            APPurchaseProduct(NULL, iAccountID, loginState->clientLanguageID, pcBuffer, uProductID, pExpectedPrice, 0, pPaymentMethodVID, pcIP, 0);
		}
	}
	else if(pPaymentMethodVID && iAccountID)
	{
		APPurchaseProduct(NULL, iAccountID, loginState->clientLanguageID, pCategory, uProductID, pExpectedPrice, 0, pPaymentMethodVID, pcIP, 0);
	}
}

void APCacheRemoveKey(U32 uAccountID, const char *pchKey)
{
    Login2State *loginState = aslLogin2_GetActiveLoginStateByAccountID(uAccountID);
	if( loginState && loginState->netLink && !loginState->loginFailed )
	{
		CStoreUpdate *pUpdate = StructCreate(parse_CStoreUpdate);
		Packet *pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_CSTORE_UPDATE);

		pUpdate->eType = kCStoreUpdate_RemoveKey;
		pUpdate->pInfo = StructCreate(parse_AccountProxyKeyValueInfo);
		pUpdate->pInfo->pKey = estrCreateFromStr(pchKey);

		pktSendStruct(pkt, pUpdate, parse_CStoreUpdate);
		pktSend(&pkt);

		StructDestroy(parse_CStoreUpdate, pUpdate);
	}
}

AUTO_COMMAND_REMOTE;
void aslAPCmdClientCacheRemoveKeyValue(GlobalType eType, ContainerID uContainerID, U32 uAccountID, SA_PARAM_NN_STR const char *pKey)
{
	APUpdateGADCache(GLOBALTYPE_LOGINSERVER, uAccountID, pKey, NULL, NULL, NULL);
	APClientCacheRemoveKeyValue(eType, uContainerID, uAccountID, pKey);
}

void APCacheSetKey(U32 uAccountID, SA_PARAM_NN_VALID AccountProxyKeyValueInfo *pInfo)
{
    Login2State *loginState = aslLogin2_GetActiveLoginStateByAccountID(uAccountID);
    if( loginState && loginState->netLink && !loginState->loginFailed )
    {
		CStoreUpdate *pUpdate = StructCreate(parse_CStoreUpdate);
		Packet *pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_CSTORE_UPDATE);
		
		pUpdate->eType = kCStoreUpdate_SetKey;
		pUpdate->pInfo = StructClone(parse_AccountProxyKeyValueInfo, pInfo);

		pktSendStruct(pkt, pUpdate, parse_CStoreUpdate);
		pktSend(&pkt);

		StructDestroy(parse_CStoreUpdate, pUpdate);
	}
}

AUTO_COMMAND_REMOTE;
void aslAPCmdClientCacheSetKeyValue(GlobalType eType, ContainerID uContainerID, U32 uAccountID, AccountProxyKeyValueInfo *pInfo)
{
	APUpdateGADCache(GLOBALTYPE_LOGINSERVER, uAccountID, pInfo->pKey, pInfo->pValue, NULL, NULL);
	APClientCacheSetKeyValue(eType, uContainerID, uAccountID, pInfo);
}


void APCacheSetAllKeys(U32 uAccountID, SA_PARAM_NN_VALID AccountProxyKeyValueInfoList *pInfoList)
{
    Login2State *loginState = aslLogin2_GetActiveLoginStateByAccountID(uAccountID);
    if( loginState && loginState->netLink && !loginState->loginFailed )
	{
		CStoreUpdate *pUpdate = StructCreate(parse_CStoreUpdate);
		Packet *pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_CSTORE_UPDATE);
		
		pUpdate->eType = kCStoreUpdate_SetKeyList;
		pUpdate->pInfoList = StructClone(parse_AccountProxyKeyValueInfoList, pInfoList);

		pktSendStruct(pkt, pUpdate, parse_CStoreUpdate);
		pktSend(&pkt);
		
		StructDestroy(parse_CStoreUpdate, pUpdate);

	}
}

AUTO_COMMAND_REMOTE;
void aslAPCmdClientCacheSetAllKeyValues(GlobalType eType, ContainerID uContainerID, U32 uAccountID, AccountProxyKeyValueInfoList *pInfoList)
{
	APClientCacheSetAllKeyValues(eType, uContainerID, uAccountID, pInfoList);
}

AUTO_COMMAND_REMOTE;
void aslAPCmdSteamCaptureComplete(U32 uAccountID, PurchaseResult eResult)
{
	const char *pMsg = GetAuthCapture_ErrorMsgKey(eResult);
    Login2State *loginState = aslLogin2_GetActiveLoginStateByAccountID(uAccountID);

    if( loginState && loginState->netLink && !loginState->loginFailed )
	{
		if(eResult == PURCHASE_RESULT_COMMIT)
		{
			aslLogin2_Notify(loginState, langTranslateMessageKey(loginState->clientLanguageID, pMsg), kNotifyType_MicroTrans_PointBuySuccess, STEAM_NOTIFY_LABEL);
		}
		else
		{
			aslLogin2_Notify(loginState, langTranslateMessageKey(loginState->clientLanguageID, pMsg), kNotifyType_MicroTrans_PointBuyFailed, STEAM_NOTIFY_LABEL);
		}
	}
}

static void RequestProducts_CB(U32 uContainerID, U32 uAccountID, SA_PARAM_NN_VALID const MicroTransactionProductList *pList, void *pUserdata)
{
    Login2State *loginState = aslLogin2_GetActiveLoginStateByAccountID(uAccountID);

    if( loginState && loginState->netLink && !loginState->loginFailed )
	{
        GameAccountData *gameAccountData = GET_REF(loginState->hGameAccountData);

        if ( gameAccountData )
        {
            CStoreUpdate *pUpdate = StructCreate(parse_CStoreUpdate);
            Packet *pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_CSTORE_UPDATE);
            AccountProxyKeyValueInfoList keyValueList = {0};

            keyValueList.ppList = (AccountProxyKeyValueInfo **)gameAccountData->eaAccountKeyValues;

            APDiscountMTCatalog(&pList, &keyValueList);
		    pUpdate->pList = pList;
		    pUpdate->eType = kCStoreUpdate_ProductList;

		    pktSendStruct(pkt, pUpdate, parse_CStoreUpdate);
		    pktSend(&pkt);

		    pUpdate->pList = NULL;
		    StructDestroy(parse_CStoreUpdate, pUpdate);
        }
	}
}

void RequestProducts(Login2State *loginState)
{
	static char pcBuffer[128] = {0};

	if (!(loginState && loginState->netLink && !loginState->loginFailed))
		return;

	APGetMTCatalog(0, loginState->accountID, RequestProducts_CB, NULL);
}

static void RequestMOTD_CB(const AccountProxyProduct *pProduct, void *userPtr)
{
	int iAccountID = (intptr_t)userPtr;
    Login2State *loginState = aslLogin2_GetActiveLoginStateByAccountID(iAccountID);

    if( loginState && loginState->netLink && !loginState->loginFailed )
	{
		CStoreUpdate *pUpdate = StructCreate(parse_CStoreUpdate);
		Packet *pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_CSTORE_UPDATE);
		pUpdate->eType = kCStoreUpdate_MOTD;
		pUpdate->pProduct = StructClone(parse_AccountProxyProduct, pProduct);

		pktSendStruct(pkt, pUpdate, parse_CStoreUpdate);
		pktSend(&pkt);

		StructDestroy(parse_CStoreUpdate, pUpdate);
	}
}

void RequestMOTD(ContainerID accountID)
{
	static char pcBuffer[128];
	MicroTrans_ShardConfig *pConfig = eaIndexedGetUsingInt(&g_MicroTransConfig.ppShardConfigs, g_eMicroTrans_ShardCategory);
	if( pConfig 
		&& pConfig->pMOTD 
		&& pConfig->pMOTD->pchCategory 
		&& pConfig->pMOTD->pchName )
	{
		if(!pcBuffer[0])
		{
			sprintf(pcBuffer, "%s.", microtrans_GetShardCategoryPrefix());
			if(strnicmp(pcBuffer, pConfig->pMOTD->pchCategory, strlen(pcBuffer)) == 0)
			{
				strcpy(pcBuffer, pConfig->pMOTD->pchCategory);
			}
			else
			{
				sprintf(pcBuffer, "%s.%s", microtrans_GetShardCategoryPrefix(), pConfig->pMOTD->pchCategory);
			}
		}
		APGetProductByName(pcBuffer, pConfig->pMOTD->pchName, RequestMOTD_CB, (void*)(intptr_t)accountID);
	}
}

static void RequestPaymentMethods_CB(U32 entityRef, U32 iAccountID, PaymentMethodsResponse *pResponse)
{
    Login2State *loginState = aslLogin2_GetActiveLoginStateByAccountID(iAccountID);

    if( loginState && loginState->netLink && !loginState->loginFailed )
	{
		CStoreUpdate *pUpdate = StructCreate(parse_CStoreUpdate);
		Packet *pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_CSTORE_UPDATE);
		
		pUpdate->eType = kCStoreUpdate_PaymentMethods;

		if(pResponse)
		{
			pUpdate->ppMethods = pResponse->eaPaymentMethods;
		}

		pktSendStruct(pkt, pUpdate, parse_CStoreUpdate);
		pktSend(&pkt);

		pUpdate->ppMethods = NULL;
		StructDestroy(parse_CStoreUpdate, pUpdate);
	}
}

void RequestPaymentMethods(ContainerID uAccountID, U64 uSteamID)
{
    APRequestPaymentMethods(NULL, uAccountID, uSteamID, RequestPaymentMethods_CB);
}

static void RequestPointBuyProducts_CB(U32 uContainerID, U32 uAccountID, const AccountProxyProductList *pList, void *pUserdata)
{
    Login2State *loginState = aslLogin2_GetActiveLoginStateByAccountID(uAccountID);

    if( loginState && loginState->netLink && !loginState->loginFailed )
	{
		CStoreUpdate *pUpdate = StructCreate(parse_CStoreUpdate);
		Packet *pkt = pktCreate(loginState->netLink, LOGINSERVER_TO_CLIENT_CSTORE_UPDATE);

		pUpdate->eType = kCStoreUpdate_PointBuyProducts;
		pUpdate->pProductList = pList;

		pktSendStruct(pkt, pUpdate, parse_CStoreUpdate);
		pktSend(&pkt);

		pUpdate->pProductList = NULL;
		StructDestroy(parse_CStoreUpdate, pUpdate);
	}
}

void RequestPointBuyProducts(ContainerID uAccountID)
{
	APGetPointBuyCatalog(0, uAccountID, RequestPointBuyProducts_CB, NULL);
}

static void ccSteamPurchase_CB(bool bSuccess, const char *pchMessage, void *userdata)
{
    U32 iAccountID = (U32)(intptr_t)userdata;
    Login2State *loginState = aslLogin2_GetActiveLoginStateByAccountID(iAccountID);
    if(loginState && loginState->netLink && !loginState->loginFailed)
    {
        // A "success" notification here actually means the purchase is pending
        aslLogin2_Notify(loginState, pchMessage, (bSuccess?kNotifyType_MicroTrans_PointBuyPending:kNotifyType_MicroTrans_PointBuyFailed), STEAM_NOTIFY_LABEL);
    }
}

void aslLoginHandleSteamPurchase(Login2State *loginState, Packet *pak)
{
    SteamMicroTxnAuthorizationResponse *pResponse = StructCreate(parse_SteamMicroTxnAuthorizationResponse);
    ParserRecv(parse_SteamMicroTxnAuthorizationResponse, pak, pResponse, aslLoginServerClientsAreUntrustworthy() ? RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE : 0 );
    ccSteamOnMicroTxnAuthorizationResponse(pResponse->authorized, loginState->accountID, pResponse->order_id, loginState->clientLanguageID, ccSteamPurchase_CB, (void*)(intptr_t)loginState->accountID);
    StructDestroy(parse_SteamMicroTxnAuthorizationResponse, pResponse);
}

static void ccSteamAuth_CB(bool bSuccess, const char *pchMessage, void *userdata)
{
	U32 iAccountID = (U32)(intptr_t)userdata;
    Login2State *loginState = aslLogin2_GetActiveLoginStateByAccountID(iAccountID);

    if( loginState && loginState->netLink && !loginState->loginFailed )
	{
		aslLogin2_Notify(loginState, pchMessage, (bSuccess?kNotifyType_MicroTrans_PointBuySuccess:kNotifyType_MicroTrans_PointBuyFailed), STEAM_NOTIFY_LABEL);
	}
}

void HandleCStoreAction(Login2State *loginState, Packet *pak)
{
    CStoreAction *pAction = StructCreate(parse_CStoreAction);
    ParserRecv(parse_CStoreAction, pak, pAction , aslLoginServerClientsAreUntrustworthy() ? RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE : 0 );

    if (loginState && loginState->netLink && !loginState->loginFailed)
    {
	    switch(pAction->eType)
	    {
	    case kCStoreAction_RequestProducts:
		    {
			    RequestProducts(loginState);
			    break;
		    }
	    case kCStoreAction_PurchaseProduct:
		    {
			    if(pAction->pchPaymentID && stricmp(pAction->pchPaymentID, STEAM_PMID)==0)
			    {	
				    char ip[MAX_IP_STR];
				    linkGetIpStr(loginState->netLink, SAFESTR(ip));
				    ccSteamPurchaseProduct(loginState->accountID, loginState->clientLanguageID, pAction->iSteamID, ip, pAction->pchCategory, pAction->iProductID, moneyCurrency(pAction->pExpectedPrice), ccSteamAuth_CB, (void*)(intptr_t)loginState->accountID);
			    }
			    else
			    {
				    PurchaseCStoreProduct(loginState->accountID, pAction->pchCategory, pAction->iProductID, pAction->pExpectedPrice, pAction->pchPaymentID);
				    aslLogin2_Notify(loginState, langTranslateMessageKey(loginState->clientLanguageID, "MicroTrans_Purchase_Pending"), kNotifyType_MicroTrans_PointBuyPending, NULL);
			    }
			    break;
		    }
	    case kCStoreAction_RequestMOTD:
		    {
			    RequestMOTD(loginState->accountID);
			    break;
		    }
	    case kCStoreAction_RequestPaymentMethods:
		    {
			    RequestPaymentMethods(loginState->accountID, pAction->iSteamID);
			    break;
		    }
	    case kCStoreAction_RequestPointBuyProducts:
		    {
                RequestPointBuyProducts(loginState->accountID);
			    break;
		    }
	    default:
		    break;
	    }
    }

    StructDestroy(parse_CStoreAction,pAction);
}