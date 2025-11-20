#include "AccountDataCache.h"
#include "CostumeCommonLoad.h"
#include "Entity.h"
#include "GameAccountDataCommon.h"
#include "GameClientLib.h"
#include "GamePermissionsCommon.h"
#include "GameStringFormat.h"
#include "gclAccountProxy.h"
#include "gclBaseStates.h"
#include "gclEntity.h"
#include "gclLogin.h"
#include "gclMicroTransactions.h"
#include "gclMicroTransactions_h_ast.h"
#include "gclSendToServer.h"
#include "gclSteam.h"
#include "GlobalStateMachine.h"
#include "GlobalTypes.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "LoginCommon.h"
#include "LoginCommon_h_ast.h"
#include "Message.h"
#include "MicroTransactionUI.h"
#include "MicroTransactions.h"
#include "MicroTransactions_h_ast.h"
#include "Money.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "Powers.h"
#include "ResourceManager.h"
#include "species_common.h"
#include "SteamCommon.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "structNet.h"
#include "TimedCallback.h"
#include "UIGen.h"

#include "AccountDataCache_h_ast.h"
#include "AutoGen/accountnet_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/GamePermissionsCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

MicroTransactionProductList *g_pMTList = NULL;
MicroTransactionCostumeList *g_pMTCostumes = NULL;

AccountProxyProductList *g_pPointBuyList = NULL;
CachedPaymentMethod **g_eaPaymentMethods = NULL;

static InvRewardRequest* s_pMTRewards;

typedef struct MTCostumeListCallback {
	MicroTrans_CostumeListChanged pHandler;
	void *pUserData;
} MTCostumeListCallback;

static MTCostumeListCallback **s_eaCostumeListCallbacks = NULL;


typedef enum MTCostumeListReferenceType {
	kMTCostumeListReferenceType_MicroTransactionDef,
	kMTCostumeListReferenceType_PlayerCostume,
	kMTCostumeListReferenceType_ItemDef,
	kMTCostumeListReferenceType_CostumeSet,
	kMTCostumeListReferenceType_MAX,
} MTCostumeListReferenceType;

AUTO_STRUCT;
typedef struct MTCostumeListReferenceLoad {
	const char *pchDefName; AST(KEY POOL_STRING)
	U32 uType;
	REF_TO(MicroTransactionDef) hMTDef;
	REF_TO(PlayerCostume) hCostume;
	REF_TO(ItemDef) hItemDef;
	REF_TO(PCCostumeSet) hCostumeSet;
} MTCostumeListReferenceLoad;

extern ParseTable parse_MTCostumeListReferenceLoad[];
#define TYPE_parse_MTCostumeListReferenceLoad MTCostumeListReferenceLoad

static MTCostumeListReferenceLoad **s_eaLoadingRefs = NULL;
static bool gclMicroTrans_CostumeWaitForReferent(const char *pchDefName, MTCostumeListReferenceType uType);

static char *s_pchMicroTrans_MOTD_Body = NULL;
static char *s_pchMicroTrans_MOTD_Title = NULL;

static const char *countryShortToCurrency(const char *pchCountry)
{
	if( !stricmp(pchCountry, "US")
		|| !stricmp(pchCountry,"USA"))
	{
		return "USD";
	}
	else if( !stricmp(pchCountry, "GB")
			|| !stricmp(pchCountry, "UK")) //NOT iso-3166 but, meh
	{
		return "GBP";
	}
	else if( !stricmp(pchCountry, "CA") )
	{
		return "CAD";
	}
	else if( !stricmp(pchCountry, "DK") )
	{
		return "DKK";
	}
	else if(
		   !stricmp(pchCountry, "AT")		//Austria
		|| !stricmp(pchCountry, "BE")		//Belgium
		|| !stricmp(pchCountry, "CY")		//Cyprus
		|| !stricmp(pchCountry, "DE")		//Germany
		|| !stricmp(pchCountry, "EE")		//Estonia
		|| !stricmp(pchCountry, "ES")		//Spain
		|| !stricmp(pchCountry, "FI")		//Finland
		|| !stricmp(pchCountry, "FR")		//France
		|| !stricmp(pchCountry, "GR")		//Greece
		|| !stricmp(pchCountry, "IE")		//Ireland
		|| !stricmp(pchCountry, "IT")		//Italy
		|| !stricmp(pchCountry, "LU")		//Luxembourg
		|| !stricmp(pchCountry, "MT")		//Malta
		|| !stricmp(pchCountry, "NL")		//Netherlands
		|| !stricmp(pchCountry, "PT")		//Portugal
		|| !stricmp(pchCountry, "SE")		//Sweden
		|| !stricmp(pchCountry, "SK")		//Slovakia
		|| !stricmp(pchCountry, "SI"))		//Slovenia
		//|| !stricmp(pchCountry, "CH"))		//Switzerland
	{
		return "EUR";
	}

	return NULL;
}

void gclMicroTrans_ClearLists()
{
	StructDestroySafe(parse_MicroTransactionProductList, &g_pMTList);
	gclMicroTrans_UpdateCostumeList();

	StructDestroySafe(parse_AccountProxyProductList, &g_pPointBuyList);
	eaDestroyStruct(&g_eaPaymentMethods, parse_CachedPaymentMethod);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclMicroTrans_RecvProductList(const MicroTransactionProductList *pList)
{
	if(g_pMTList)
		StructDestroySafe(parse_MicroTransactionProductList, &g_pMTList);

	if(pList)
		g_pMTList = StructClone(parse_MicroTransactionProductList, pList);

	gclMicroTrans_UpdateCostumeList();
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclMicroTrans_RecvPointBuyProducts(const AccountProxyProductList *pList)
{
	if(g_pPointBuyList)
		StructDestroySafe(parse_AccountProxyProductList, &g_pPointBuyList);

	if(pList)
		g_pPointBuyList = StructClone(parse_AccountProxyProductList, pList);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclMicroTrans_RecvPaymentMethods(PaymentMethodsResponse *pResponse)
{
	if(pResponse && eaSize(&pResponse->eaPaymentMethods))
	{
		int i;
		
		eaCopyStructs(&pResponse->eaPaymentMethods, &g_eaPaymentMethods, parse_CachedPaymentMethod);
		
		for(i=eaSize(&g_eaPaymentMethods)-1; i>=0; i--)
		{
			NOCONST(CachedPaymentMethod) *pMethod = CONTAINER_NOCONST(CachedPaymentMethod, g_eaPaymentMethods[i]);

			if(!pMethod)
			{
				eaRemove(&g_eaPaymentMethods, i);
				continue;
			}

			if(!(pMethod->currency && *pMethod->currency))
			{
				if(pResponse->pDefaultCurrency && *pResponse->pDefaultCurrency)
				{
					pMethod->currency = estrCreateFromStr(pResponse->pDefaultCurrency);
				}
				else if(pMethod->billingAddress.country && *pMethod->billingAddress.country)
				{
					//Attempt to turn the country into a currency for the payment method
					const char *pchCountryCurrency = countryShortToCurrency(pMethod->billingAddress.country);
					if(pchCountryCurrency)
					{
						pMethod->currency = estrCreateFromStr(pchCountryCurrency);
					}
				}
			}

			//If still no currency, destroy the method
			if(!(pMethod->currency && *pMethod->currency))
			{
				eaRemove(&g_eaPaymentMethods, i);
				StructDestroyNoConst(parse_CachedPaymentMethod, pMethod);
			}
		}
	}
	else
	{
		eaClearStruct(&g_eaPaymentMethods, parse_CachedPaymentMethod);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RequestMicroTransactions);
void gclMicroTrans_expr_RequestProductList()
{
	if(g_pMainMTCategory)
	{
		if(GSM_IsStateActive(GCL_LOGIN) && gpLoginLink)
		{
			Packet *pPak;
			CStoreAction *pAction = StructCreate(parse_CStoreAction);
			pAction->eType = kCStoreAction_RequestProducts;

			pPak = pktCreate(gpLoginLink, TOLOGIN_CSTORE_ACTION);
			ParserSendStruct(parse_CStoreAction, pPak, pAction);
			pktSend(&pPak);	
			StructDestroy(parse_CStoreAction, pAction);
		}
		else if(gclServerIsConnected())
		{
			ServerCmd_gslAPCmdRequestMTCatalog();
		}
	}
}

AUTO_COMMAND ACMD_NAME(RefreshCStore) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclMicroTrans_RefreshCStore()
{
	gclMicroTrans_expr_RequestProductList();
}

bool gclMicroTrans_PrerequisitesMet(MicroTransactionProduct *pMTProduct)
{
	// Update the cached flag if out of date
	if (pMTProduct && pMTProduct->uAPPrerequisitesMetUpdateTime < gclAPGetKeyValueLastUpdateTime())
	{
		pMTProduct->bAPPrerequisitesMet = gclAPPrerequisitesMet(pMTProduct->pProduct);
		pMTProduct->uAPPrerequisitesMetUpdateTime = gclAPGetKeyValueLastUpdateTime();
	}

	return pMTProduct && pMTProduct->bAPPrerequisitesMet;
}

SA_RET_NN_VALID static Money *gclMicroTrans_GetExpectedPrice(U32 uID, SA_PARAM_OP_STR const char *pchCurrency, bool bUseFullPrice)
{
	MicroTransactionProduct *pProduct = eaIndexedGetUsingInt(&g_pMTList->ppProducts, uID);
	const Money *pProductPrice = NULL;
	Money *pPrice = NULL;

	if(!devassert(pProduct))
	{
		pProductPrice = NULL;
	}
	else if(!pchCurrency || !pchCurrency[0])
	{
		if(bUseFullPrice)
		{
			pProductPrice = findMoneyFromCurrency(pProduct->pProduct->ppFullMoneyPrices, microtrans_GetShardCurrencyExactName());
		}
		else
		{
			pProductPrice = findMoneyFromCurrency(pProduct->pProduct->ppMoneyPrices, microtrans_GetShardCurrencyExactName());
		}
	}
	else
	{
		if(bUseFullPrice)
		{
			pProductPrice = findMoneyFromCurrency(pProduct->pProduct->ppFullMoneyPrices, pchCurrency);
		}
		else
		{
			pProductPrice = findMoneyFromCurrency(pProduct->pProduct->ppMoneyPrices, pchCurrency);
		}
	}

	if(pProductPrice)
	{
		pPrice = StructClone(parse_Money, pProductPrice);
	}
	else
	{
		pPrice = moneyCreateInvalid(pchCurrency);
	}

	return pPrice;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PurchaseMicroTransaction);
void gclMicroTrans_expr_PurchaseMicroTransaction(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_STR const char *pchCategory, U32 uID, SA_PARAM_OP_STR const char *pchCurrency)
{
	if(pchCategory &&
		uID &&
		g_pMTList != NULL)
	{
		bool bUseFullPrice = (ui_GenExprGetCurrentCouponItemID(uID) > 0);
		Money *pPrice = gclMicroTrans_GetExpectedPrice(uID, pchCurrency, bUseFullPrice);

		if(gpLoginLink && GSM_IsStateActive(GCL_LOGIN))
		{
			Packet *pPak;
			CStoreAction *pAction = StructCreate(parse_CStoreAction);
			pAction->eType = kCStoreAction_PurchaseProduct;
			pAction->iProductID = uID;
			pAction->pchCategory = StructAllocString(pchCategory);
			pAction->pExpectedPrice = pPrice;

			pPak = pktCreate(gpLoginLink, TOLOGIN_CSTORE_ACTION);
			ParserSendStruct(parse_CStoreAction, pPak, pAction);
			pktSend(&pPak);	

			StructDestroy(parse_CStoreAction, pAction);
			return;
		}
		else if(gclServerIsConnected())
		{
			if(pEntity)
			{
				ServerCmd_PurchaseProductEx(pchCategory, uID, pPrice, NULL, ui_GenExprGetCurrentCouponItemID(uID));
			}
		}

		StructDestroy(parse_Money, pPrice);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsPWAccount);
bool gclMicroTrans_expr_IsPWAccount(void)
{
	return gGCLState.pwAccountName[0] ? true : false;
}

//Purchase a point-buy product (AtariTokens, cryptic points etc)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PurchasePoints);
void gclMicroTrans_expr_PurchasePoints(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_STR const char *pchCategory, U32 uID, SA_PARAM_OP_STR const char *pchCurrency, SA_PARAM_OP_STR const char *pchPaymentID)
{
	if(pchCategory && *pchCategory &&
		pchPaymentID && *pchPaymentID && 
		g_pPointBuyList &&
		uID >= 0)
	{
		Money *pPrice = NULL;

		AccountProxyProduct *pProduct = NULL;
		CachedPaymentMethod *pMethod = NULL;
		bool bIsSteamPurchase = stricmp(pchPaymentID, STEAM_PMID) == 0;
		int i;

		//Try to find the payment method
		for(i=eaSize(&g_eaPaymentMethods)-1; i>=0; i--)
		{
			if(!strcmp_safe(g_eaPaymentMethods[i]->VID, pchPaymentID))
			{
				pMethod = g_eaPaymentMethods[i];
				break;
			}
		}

		//If the payment method specified isn't valid, fail it here
		if(!pMethod)
		{
			char *estrMsg = NULL;
			FormatGameMessageKey(&estrMsg, "MicroTrans_Purchase_BadPaymentMethod", STRFMT_END);

			notify_NotifySend(NULL, kNotifyType_Failed, estrMsg, NULL, NULL);

			estrDestroy(&estrMsg);
			return;
		}

		for(i=eaSize(&g_pPointBuyList->ppList)-1; i>=0; i--)
		{
			if(g_pPointBuyList->ppList[i]->uID == uID)
			{
				pProduct = g_pPointBuyList->ppList[i];
				break;
			}
		}

		if(!gclMicroTrans_PointBuy_CanAttemptPurchase(false))
		{
			char *estrMsg = NULL;
			FormatGameMessageKey(&estrMsg, "MicroTrans_Purchase_Cooldown", STRFMT_END);
			
			notify_NotifySend(NULL, kNotifyType_Failed, estrMsg, NULL, NULL);

			estrDestroy(&estrMsg);
			return;
		}

		//Try to find a valid currency to use
		if(!pchCurrency || !*pchCurrency)
		{
			//Find the currency I should use
			pchCurrency = pMethod->currency;
			
			//If after these checks there still isn't a currency available, then quit
			if(!pchCurrency || !*pchCurrency)
			{
				char *estrMsg = NULL;
				FormatGameMessageKey(&estrMsg, "MicroTrans_Purchase_BadRequest", STRFMT_END);

				notify_NotifySend(NULL, kNotifyType_Failed, estrMsg, NULL, NULL);

				estrDestroy(&estrMsg);
				return;
			}
		}

		if(bIsSteamPurchase && !gclSteam_CanAttemptPurchase())
		{
			char *estrMsg = NULL;
			FormatGameMessageKey(&estrMsg, "MicroTrans_Purchase_WaitingOnSteam", STRFMT_END);

			notify_NotifySend(NULL, kNotifyType_Failed, estrMsg, NULL, NULL);

			estrDestroy(&estrMsg);
			return;
		}

		if(bIsSteamPurchase && !microtrans_SteamWalletEnabled())
		{
			char *estrMsg = NULL;
			FormatGameMessageKey(&estrMsg, "MicroTrans_Purchase_SteamDisabled", STRFMT_END);

			notify_NotifySend(NULL, kNotifyType_Failed, estrMsg, NULL, NULL);

			estrDestroy(&estrMsg);
			return;
		}

		//Get the expected price from the product
		{
			const Money *pProductPrice = findMoneyFromCurrency(pProduct->ppMoneyPrices, pchCurrency);
	
			if(pProductPrice)
			{
				pPrice = StructClone(parse_Money, pProductPrice);
			}
			//If this product is not in that currency, then fail the purchase
			else
			{
				char *estrMsg = NULL;
				FormatGameMessageKey(&estrMsg, "MicroTrans_Purchase_BadRequest", STRFMT_END);

				notify_NotifySend(NULL, kNotifyType_Failed, estrMsg, NULL, NULL);

				estrDestroy(&estrMsg);
				return;
			}
		}

		if(gpLoginLink && GSM_IsStateActive(GCL_LOGIN))
		{
			Packet *pPak;
			CStoreAction *pAction = StructCreate(parse_CStoreAction);
			pAction->eType = kCStoreAction_PurchaseProduct;
			pAction->iProductID = uID;
			pAction->pchCategory = StructAllocString(pchCategory);
			pAction->pchPaymentID = StructAllocString(pchPaymentID);
			pAction->iSteamID = gclSteamID();
			pAction->pExpectedPrice = pPrice;

			pPak = pktCreate(gpLoginLink, TOLOGIN_CSTORE_ACTION);
			ParserSendStruct(parse_CStoreAction, pPak, pAction);
			pktSend(&pPak);

			if(bIsSteamPurchase)
			{
				gclSteam_PurchaseActive(true);
			}

			StructDestroy(parse_CStoreAction, pAction);
			return;
		}
		else if(gclServerIsConnected())
		{
			if(pEntity)
			{
				if(bIsSteamPurchase)
				{
					ServerCmd_gslSteamPurchaseProduct(gclSteamID(), pchCategory, uID, pchCurrency);
					gclSteam_PurchaseActive(true);
				}
				else
				{
					ServerCmd_gslAPCmdPurchaseProduct(pchCategory, uID, pPrice, pchPaymentID);
				}
			}
		}

		StructDestroy(parse_Money, pPrice);
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclMicroTrans_SetMOTD(SA_PARAM_OP_VALID AccountProxyProduct *pProduct)
{
	char *estrName = NULL;
	char *estrDescription = NULL;
	if(pProduct)
	{
		microtrans_DEPR_GetLocalizedInfo(pProduct, &estrName, &estrDescription);
	}

	s_pchMicroTrans_MOTD_Body = strdup_ifdiff(estrDescription, s_pchMicroTrans_MOTD_Body);
	s_pchMicroTrans_MOTD_Title = strdup_ifdiff(estrName, s_pchMicroTrans_MOTD_Title);

	estrDestroy(&estrName);
	estrDestroy(&estrDescription);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTrans_GetMOTD);
const char* gclMicroTrans_expr_GetMOTD()
{
	return s_pchMicroTrans_MOTD_Body;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTrans_GetMOTDTitle);
const char* gclMicroTrans_expr_GetMOTDTitle()
{
	return s_pchMicroTrans_MOTD_Title;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RequestMicroTrans_MOTD);
void gclMicroTrans_expr_RequestMicroTrans_MOTD()
{
	if(gpLoginLink && GSM_IsStateActive(GCL_LOGIN))
	{
		Packet *pPak;
		CStoreAction *pAction = StructCreate(parse_CStoreAction);
		pAction->eType = kCStoreAction_RequestMOTD;

		pPak = pktCreate(gpLoginLink, TOLOGIN_CSTORE_ACTION);
		ParserSendStruct(parse_CStoreAction, pPak, pAction);
		pktSend(&pPak);	
	}
	else if(gclServerIsConnected())
	{
		ServerCmd_gslAPCmdGetMOTD();
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RequestPaymentMethods);
void gclMicroTrans_expr_RequestPaymentMethods()
{
	U64 uSteamID = microtrans_SteamWalletEnabled() ? gclSteamID() : 0;

	if(GSM_IsStateActive(GCL_LOGIN) && gpLoginLink)
	{
		Packet *pPak;
		CStoreAction *pAction = StructCreate(parse_CStoreAction);
		pAction->eType = kCStoreAction_RequestPaymentMethods;
		pAction->iSteamID = uSteamID;

		pPak = pktCreate(gpLoginLink, TOLOGIN_CSTORE_ACTION);
		ParserSendStruct(parse_CStoreAction, pPak, pAction);
		pktSend(&pPak);
	}
	else if(gclServerIsConnected())
	{
		ServerCmd_gslAPCmdGetPaymentMethods(uSteamID);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RequestPointBuyProducts);
void gclMicroTrans_expr_RequestPointBuyProducts()
{
	if(GSM_IsStateActive(GCL_LOGIN) && gpLoginLink)
	{
		Packet *pPak;
		CStoreAction *pAction = StructCreate(parse_CStoreAction);
		pAction->eType = kCStoreAction_RequestPointBuyProducts;

		pPak = pktCreate(gpLoginLink, TOLOGIN_CSTORE_ACTION);
		ParserSendStruct(parse_CStoreAction, pPak, pAction);
		pktSend(&pPak);
	}
	else if(gclServerIsConnected())
	{
		ServerCmd_gslAPCmdRequestPointBuyCatalog();
	}
}

// Language tag to use if one does not exist for the current locale
#define BACKUP_LANGUAGE_TAG "en"

// Get the index of the localized info an account proxy product for this client
static int microtrans_DEPR_GetIETFLanguageTagIndex(SA_PARAM_NN_VALID const AccountProxyProduct *pProduct)
{
	const char *tag = locGetCode(getCurrentLocale());
	int backupIndex = -1;

	EARRAY_CONST_FOREACH_BEGIN(pProduct->ppLocalizedInfo, i, size);
	AccountProxyProductLocalizedInfo *pLocalizedInfo = pProduct->ppLocalizedInfo[i];

	if (!stricmp(pLocalizedInfo->pLanguageTag, tag))
	{
		return i;
	}

	if (!stricmp(pLocalizedInfo->pLanguageTag, BACKUP_LANGUAGE_TAG))
	{
		backupIndex = i;
	}
	EARRAY_FOREACH_END;

	return backupIndex;
}

// Get the localized name/description for a product
void microtrans_DEPR_GetLocalizedInfo(const AccountProxyProduct *pProduct, char **pNameOut, char **pDescriptionOut)
{
	int localizationIndex = microtrans_DEPR_GetIETFLanguageTagIndex(pProduct);

	if (localizationIndex < 0)
	{
		estrCopy2(pNameOut, pProduct->pName);
		estrCopy2(pDescriptionOut, pProduct->pDescription);
		return;
	}

	estrCopy2(pNameOut, pProduct->ppLocalizedInfo[localizationIndex]->pName);
	estrCopy2(pDescriptionOut, pProduct->ppLocalizedInfo[localizationIndex]->pDescription);
}

void gclMicroTrans_PurchaseMicroTransactionList(Entity *pEntity, MicroTransactionProduct **eaProducts, const char *pchCurrency)
{
	if (pEntity && g_pMainMTCategory && g_pMTList)
	{
		S32 iIdx, iSize = eaSize(&eaProducts);
		for (iIdx = 0; iIdx < iSize; iIdx++)
		{
			Money *pPrice = gclMicroTrans_GetExpectedPrice(eaProducts[iIdx]->uID, pchCurrency, false);
			ServerCmd_gslAPCmdPurchaseProduct(g_pMainMTCategory->pchName, eaProducts[iIdx]->uID, pPrice, NULL);
			StructDestroy(parse_Money, pPrice);
		}
	}
}

bool gclMicroTrans_IsProductInMain(MicroTransactionProduct *pMTProduct)
{
	int iCatIdx;

	if (!g_pMainMTCategory)
		return false;

	for (iCatIdx = 0; iCatIdx < eaSize(&pMTProduct->ppchCategories); iCatIdx++)
	{
		if (pMTProduct->ppchCategories[iCatIdx] == g_pMainMTCategory->pchName)
			return true;
		else if (!stricmp(pMTProduct->ppchCategories[iCatIdx], g_pMainMTCategory->pchName))
			return true;
	}

	return false;
}

bool gclMicroTrans_IsProductHidden(MicroTransactionProduct *pMTProduct)
{
	bool bRequirementsMet = gclMicroTrans_PrerequisitesMet(pMTProduct);
	int iCatIdx;

	if (bRequirementsMet)
		return false;

	for (iCatIdx = 0; iCatIdx < eaSize(&pMTProduct->ppchCategories); iCatIdx++)
	{
		MicroTransactionCategory *pMTCategory = microtrans_CategoryFromStr(pMTProduct->ppchCategories[iCatIdx]);

		if (pMTCategory && pMTCategory->bHideUnusable)
			return true;
	}

	return false;
}

MicroTransactionProduct *gclMicroTrans_FindProductForPermission(const char *pchPermission)
{
	MicroTransactionProduct *pProduct = NULL;

	if (g_pMTList)
	{
		GameTokenText *pText = gamePermission_TokenStructFromString(pchPermission);
		if (pText)
		{
			S32 iIdx, iNum = eaSize(&g_pMTList->ppProducts);
			for (iIdx = 0; iIdx < iNum; iIdx++)
			{
				MicroTransactionProduct *pProd = g_pMTList->ppProducts[iIdx];
				MicroTransactionDef *pDef = GET_REF(pProd->hDef);
				if (pDef)
				{
					if (microtrans_MTDefGrantsPermission(pDef, pText))
					{
						pProduct = pProd;
						break;
					}
				}
			}
			StructDestroy(parse_GameTokenText, pText);
		}
	}

	return pProduct;
}

S32 gclMicroTrans_FindProductsForPermission(MicroTransactionProduct ***pppProducts, const char *pchPermission)
{
	S32 iCount = 0;

	eaClear(pppProducts);

	if (g_pMTList)
	{
		GameTokenText *pText = gamePermission_TokenStructFromString(pchPermission);
		if (pText)
		{
			S32 iIdx, iNum = eaSize(&g_pMTList->ppProducts);
			for (iIdx = 0; iIdx < iNum; iIdx++)
			{
				MicroTransactionProduct *pProd = g_pMTList->ppProducts[iIdx];
				MicroTransactionDef *pDef = GET_REF(pProd->hDef);
				if (pDef)
				{
					if (microtrans_MTDefGrantsPermission(pDef, pText))
					{
						eaPush(pppProducts, pProd);
						iCount++;
					}
				}
			}
			StructDestroy(parse_GameTokenText, pText);
		}
	}

	return iCount;
}

S32 gclMicroTrans_FindProductsForPermissionExpr(MicroTransactionProduct ***pppProducts, Expression *pExpr)
{
	S32 iCount = 0;
	static GameTokenText **s_eaTokens = NULL;
	if(!pExpr)
	{
		return iCount;
	}
	microtrans_GetGameTokenTextFromPermissionExpr(pExpr, &s_eaTokens);
	eaClear(pppProducts);

	if (g_pMTList && eaSize(&s_eaTokens))
	{
		S32 iIdx, iNum = eaSize(&g_pMTList->ppProducts);
		for (iIdx = 0; iIdx < iNum; iIdx++)
		{
			MicroTransactionProduct *pProd = g_pMTList->ppProducts[iIdx];
			MicroTransactionDef *pDef = GET_REF(pProd->hDef);
			if (pDef)
			{
				FOR_EACH_IN_EARRAY(s_eaTokens, GameTokenText, pText)
				{
					if (microtrans_MTDefGrantsPermission(pDef, pText))
					{
						eaPush(pppProducts, pProd);
						iCount++;
						break;
					}
				} FOR_EACH_END;
			}
		}
	}

	return iCount;
}

static bool gclMicroTrans_MTDefChangesKey(MicroTransactionDef *pDef, const char *pchAccountKey)
{
	if (pDef && pchAccountKey)
	{
		S32 iPartIdx;
		for(iPartIdx = eaSize(&pDef->eaParts)-1; iPartIdx>=0; iPartIdx--)
		{
			AttribValuePairChange *pPairChange;
			if(pDef->eaParts[iPartIdx]->ePartType != kMicroPart_Attrib)
				continue;

			pPairChange = pDef->eaParts[iPartIdx]->pPairChange;
			if (pPairChange && pPairChange->pchAttribute && !stricmp(pPairChange->pchAttribute, pchAccountKey))
			{
				return true;
			}
		}
	}

	return false;
}

MicroTransactionProduct *gclMicroTrans_FindProductForKey(const char *pchAccountKey)
{
	MicroTransactionProduct *pProduct = NULL;

	if (g_pMTList)
	{
		if (pchAccountKey && *pchAccountKey)
		{
			S32 iIdx, iNum = eaSize(&g_pMTList->ppProducts);
			for (iIdx = 0; iIdx < iNum; iIdx++)
			{
				MicroTransactionProduct *pProd = g_pMTList->ppProducts[iIdx];
				MicroTransactionDef *pDef = GET_REF(pProd->hDef);
				if (pDef)
				{
					if (gclMicroTrans_MTDefChangesKey(pDef, pchAccountKey))
					{
						pProduct = pProd;
						break;
					}
				}
			}
		}
	}

	return pProduct;
}

S32 gclMicroTrans_FindProductsForKey(MicroTransactionProduct ***pppProducts, const char *pchAccountKey)
{
	S32 iCount = 0;

	eaClear(pppProducts);

	if (g_pMTList)
	{
		if (pchAccountKey && *pchAccountKey)
		{
			S32 iIdx, iNum = eaSize(&g_pMTList->ppProducts);
			for (iIdx = 0; iIdx < iNum; iIdx++)
			{
				MicroTransactionProduct *pProd = g_pMTList->ppProducts[iIdx];
				MicroTransactionDef *pDef = GET_REF(pProd->hDef);
				if (pDef)
				{
					if (gclMicroTrans_MTDefChangesKey(pDef, pchAccountKey))
					{
						eaPush(pppProducts, pProd);
						iCount++;
					}
				}
			}
		}
	}

	return iCount;
}

static MicroTransactionCostume *gclMicroTrans_getMicroTransactionCostume(const char *pcName)
{
	MicroTransactionCostume *pMTCostume = NULL;

	if (g_pMTCostumes)
	{
		pMTCostume = eaIndexedGetUsingString(&g_pMTCostumes->ppCostumes, pcName);
	}

	if (!pMTCostume)
	{
		pMTCostume = StructCreate(parse_MicroTransactionCostume);
		SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, pcName, pMTCostume->hCostume);

		// These flags get and'd with some boolean so they should start true.
		pMTCostume->bHidden = true;
		pMTCostume->bNew = true;

		if (!g_pMTCostumes)
			g_pMTCostumes = StructCreate(parse_MicroTransactionCostumeList);
		eaPush(&g_pMTCostumes->ppCostumes, pMTCostume);
	}

	return pMTCostume;
}

__forceinline void gclMicroTrans_UpdateFlags(MicroTransactionCostume *pMTCostume, MicroTransactionCostumeSource *pMTCostumeSource)
{
	pMTCostume->bHidden = pMTCostume->bHidden && pMTCostumeSource->bHidden;
	pMTCostume->bOwned = pMTCostume->bOwned || pMTCostumeSource->bOwned;
	pMTCostume->bNew = pMTCostume->bNew && pMTCostumeSource->bNew;
}

static void gclMicroTrans_addCostumeToCostumeList(MicroTransactionProduct *pMTProduct, bool bHidden, bool bOwned, bool bNew, S64 iPrice, SA_PARAM_NN_VALID PlayerCostume *pCostume)
{
	MicroTransactionCostume *pMTCostume = gclMicroTrans_getMicroTransactionCostume(pCostume->pcName);
	MicroTransactionCostumeSource *pMTCostumeSource;

	if (eaIndexedFindUsingInt(&pMTCostume->eaSources, pMTProduct->uID) >= 0)
	{
		return;
	}
	
	// Add source
	pMTCostumeSource = StructCreate(parse_MicroTransactionCostumeSource);
	pMTCostumeSource->uID = pMTProduct->uID;
	pMTCostumeSource->pProduct = pMTProduct;
	pMTCostumeSource->bHidden = bHidden;
	pMTCostumeSource->bNew = bNew;
	pMTCostumeSource->bOwned = bOwned;
	eaPush(&pMTCostume->eaSources, pMTCostumeSource);
	gclMicroTrans_UpdateFlags(pMTCostume, pMTCostumeSource);
}

static void gclMicroTrans_addItemToCostumeList(MicroTransactionProduct *pMTProduct, bool bHidden, bool bOwned, bool bNew, S64 iPrice, SA_PARAM_NN_VALID ItemDef *pItemDef)
{
	MicroTransactionCostume *pMTCostume = NULL;
	MicroTransactionCostumeSource *pMTCostumeSource;
	int iIdx;

	for (iIdx = 0; iIdx < eaSize(&pItemDef->ppCostumes); iIdx++)
	{
		pMTCostume = gclMicroTrans_getMicroTransactionCostume(REF_STRING_FROM_HANDLE(pItemDef->ppCostumes[iIdx]->hCostumeRef));

		if (eaIndexedFindUsingInt(&pMTCostume->eaSources, pMTProduct->uID) >= 0)
		{
			return;
		}

		// Add source
		pMTCostumeSource = StructCreate(parse_MicroTransactionCostumeSource);
		pMTCostumeSource->uID = pMTProduct->uID;
		pMTCostumeSource->pProduct = pMTProduct;
		pMTCostumeSource->bHidden = bHidden;
		pMTCostumeSource->bNew = bNew;
		pMTCostumeSource->bOwned = bOwned;
		SET_HANDLE_FROM_REFERENT(g_hItemDict, pItemDef, pMTCostumeSource->hItem);
		eaPush(&pMTCostume->eaSources, pMTCostumeSource);
		gclMicroTrans_UpdateFlags(pMTCostume, pMTCostumeSource);
	}
}

static void gclMicroTrans_addCostumeSetToCostumeList(MicroTransactionProduct *pMTProduct, bool bHidden, bool bOwned, bool bNew, S64 iPrice, SA_PARAM_NN_VALID PCCostumeSet *pCostumeSet)
{
	MicroTransactionCostume *pMTCostume = NULL;
	MicroTransactionCostumeSource *pMTCostumeSource;
	int iIdx;
	for (iIdx = 0; iIdx < eaSize(&pCostumeSet->eaPlayerCostumes); iIdx++)
	{
		pMTCostume = gclMicroTrans_getMicroTransactionCostume(REF_STRING_FROM_HANDLE(pCostumeSet->eaPlayerCostumes[iIdx]->hPlayerCostume));

		if (eaIndexedFindUsingInt(&pMTCostume->eaSources, pMTProduct->uID) >= 0)
		{
			return;
		}

		// Add source
		pMTCostumeSource = StructCreate(parse_MicroTransactionCostumeSource);
		pMTCostumeSource->uID = pMTProduct->uID;
		pMTCostumeSource->pProduct = pMTProduct;
		pMTCostumeSource->bHidden = bHidden;
		pMTCostumeSource->bNew = bNew;
		pMTCostumeSource->bOwned = bOwned;
		SET_HANDLE_FROM_REFERENT(g_hCostumeSetsDict, pCostumeSet, pMTCostumeSource->hCostumeSet);
		eaPush(&pMTCostume->eaSources, pMTCostumeSource);
		gclMicroTrans_UpdateFlags(pMTCostume, pMTCostumeSource);
	}
}

// Add a CostumeSet based off of a permission to the list, note this function
// makes a lot of assumptions about how the data is setup.
static void gclMicroTrans_addCostumeSetPermissionToCostumeList(MicroTransactionProduct *pMTProduct, bool bHidden, bool bOwned, bool bNew, S64 iPrice, SA_PARAM_NN_VALID GameTokenText *pToken)
{
	PCCostumeSet *pLoadCostumeSet;
	MTCostumeListReferenceLoad *pPartLoad;
	GameTokenText **eaExprTokens = NULL;

	pPartLoad = eaIndexedGetUsingString(&s_eaLoadingRefs, pToken->pchValue);

	// Scan through the CostumeSet dictionary to see if a costume set requires
	// the permission token.
	//
	// CAUTION: This only works correctly if there is a single permission in the
	// expression and it's not NOTed or the like.
	FOR_EACH_IN_REFDICT(g_hCostumeSetsDict, PCCostumeSet, pCostumeSet);
	{
		if (pCostumeSet->pExprUnlock)
		{
			S32 iTokenIdx;

			microtrans_GetGameTokenTextFromPermissionExpr(pCostumeSet->pExprUnlock, &eaExprTokens);

			for (iTokenIdx = 0; iTokenIdx < eaSize(&eaExprTokens); iTokenIdx++)
			{
				if (!StructCompare(parse_GameTokenText, eaExprTokens[iTokenIdx], pToken, 0, 0, 0))
				{
					gclMicroTrans_addCostumeSetToCostumeList(pMTProduct, bHidden, bOwned, bNew, iPrice, pCostumeSet);
					break;
				}
			}

			// If the costume set was added...
			if (iTokenIdx < eaSize(&eaExprTokens))
			{
				S32 i;
				for (i = 0; i < eaSize(&s_eaLoadingRefs); i++)
				{
					if (GET_REF(s_eaLoadingRefs[i]->hCostumeSet) == pCostumeSet)
					{
						StructDestroy(parse_MTCostumeListReferenceLoad, eaRemove(&s_eaLoadingRefs, i));
						break;
					}
				}

				return;
			}
		}
	}
	FOR_EACH_END;

	if (eaExprTokens)
	{
		eaDestroyStruct(&eaExprTokens, parse_GameTokenText);
	}

	// No costume set was found, which probably means it's on the server,
	// try the following hack which assumes that token value is the name
	// of the costume set granted.
	pLoadCostumeSet = RefSystem_ReferentFromString(g_hCostumeSetsDict, pToken->pchValue);
	if (pLoadCostumeSet)
	{
		// Unable to find the CostumeSet that is granted by the MicroTransaction
	}
	else if (!pPartLoad)
	{
		gclMicroTrans_CostumeWaitForReferent(pToken->pchValue, kMTCostumeListReferenceType_CostumeSet);
	}
}

void gclMicroTrans_UpdateCostumeList(void)
{
	Entity *pEnt = entActivePlayerPtr();
	S32 iIdx;
	S64 iPrice;
	if(GSM_IsStateActiveOrPending(GCL_LOGIN_USER_CHOOSING_EXISTING))
	{
		pEnt = (Entity*)gclLoginGetChosenEntity();
	}
	

	if (g_pMTCostumes)
		StructDestroySafe(parse_MicroTransactionCostumeList, &g_pMTCostumes);

	if (g_pMTList && g_pMainMTCategory)
	{
		for (iIdx = 0; iIdx < eaSize(&g_pMTList->ppProducts); iIdx++)
		{
			MicroTransactionProduct *pMTProduct = g_pMTList->ppProducts[iIdx];
			MicroTransactionDef *pDef = GET_REF(pMTProduct->hDef);
			bool bHidden = true;
			bool bOwned = false;
			bool bNew = false;
			MTCostumeListReferenceLoad *pLoad = NULL;

			if(!pMTProduct->pProduct)
				continue;

			//Get the price
			iPrice = microtrans_GetPrice(pMTProduct->pProduct);

			//Not a good price
			if(iPrice < 0)
				continue;

			// Not accessible through the Main category
			if (!gclMicroTrans_IsProductInMain(pMTProduct))
				continue;

			bHidden = gclMicroTrans_IsProductHidden(pMTProduct);
			bOwned = microtrans_AlreadyPurchased(pEnt, entity_GetGameAccount(pEnt), pDef);
			bNew = microtrans_HasNewCategory(pMTProduct);

			// Check for costumes
			if (pDef || IS_HANDLE_ACTIVE(pMTProduct->hDef))
			{
				S32 iPartIdx;

				pLoad = eaIndexedGetUsingString(&s_eaLoadingRefs, REF_STRING_FROM_HANDLE(pMTProduct->hDef));

				if(pDef)
				{
					MTCostumeListReferenceLoad *pPartLoad = NULL;
					bool bIsCostume = false;

					for (iPartIdx = 0; (iPartIdx < eaSize(&pDef->eaParts) && !bIsCostume); iPartIdx++)
					{
						MicroTransactionPart *pMTPart = pDef->eaParts[iPartIdx];
						switch (pMTPart->ePartType)
						{
							case kMicroPart_Costume:
							case kMicroPart_CostumeRef:
							case kMicroPart_Permission:
								{
									bIsCostume = true;
									break;
								}
						}
					}

					if(!bIsCostume)
					{
						// not a costume, don't run checks as some expressions will error that are used on non-costumes before there is an ent available
						continue;
					}

					for (iPartIdx = 0; iPartIdx < eaSize(&pDef->eaParts); iPartIdx++)
					{
						MicroTransactionPart *pMTPart = pDef->eaParts[iPartIdx];
						switch (pMTPart->ePartType)
						{
						case kMicroPart_Costume:
							if (IS_HANDLE_ACTIVE(pMTPart->hItemDef))
							{
								pPartLoad = eaIndexedGetUsingString(&s_eaLoadingRefs, REF_STRING_FROM_HANDLE(pMTPart->hItemDef));

								if (GET_REF(pMTPart->hItemDef))
								{
									gclMicroTrans_addItemToCostumeList(pMTProduct, bHidden, bOwned, bNew, iPrice, GET_REF(pMTPart->hItemDef));

									if (pPartLoad)
									{
										eaFindAndRemove(&s_eaLoadingRefs, pPartLoad);
										StructDestroySafe(parse_MTCostumeListReferenceLoad, &pPartLoad);
									}
								}
								else if (!pPartLoad)
								{
									gclMicroTrans_CostumeWaitForReferent(REF_STRING_FROM_HANDLE(pMTPart->hItemDef), kMTCostumeListReferenceType_ItemDef);
								}
							}

							// kMicroPart_Costume can use the hCostumeDef,
							// so check that handle too.
						case kMicroPart_CostumeRef:
							if (IS_HANDLE_ACTIVE(pMTPart->hCostumeDef))
							{
								pPartLoad = eaIndexedGetUsingString(&s_eaLoadingRefs, REF_STRING_FROM_HANDLE(pMTPart->hCostumeDef));

								if (GET_REF(pMTPart->hCostumeDef))
								{
									gclMicroTrans_addCostumeToCostumeList(pMTProduct, bHidden, bOwned, bNew, iPrice, GET_REF(pMTPart->hCostumeDef));

									if (pPartLoad)
									{
										eaFindAndRemove(&s_eaLoadingRefs, pPartLoad);
										StructDestroySafe(parse_MTCostumeListReferenceLoad, &pPartLoad);
									}
								}
								else if (!pPartLoad)
								{
									gclMicroTrans_CostumeWaitForReferent(REF_STRING_FROM_HANDLE(pMTPart->hCostumeDef), kMTCostumeListReferenceType_PlayerCostume);
								}
							}
							break;

							// kMicroPart_Permission can unlock a CostumeSet so may have to
							// wait for that to get to the client.
						case kMicroPart_Permission:
							{
								GamePermissionDef *pPermissionDef = eaIndexedGetUsingString(&g_GamePermissions.eaPermissions, pMTPart->pchPermission);
								if (pPermissionDef && pPermissionDef->eType == kGamePermission_Normal)
								{
									S32 iTokenIdx;
									for (iTokenIdx = 0; iTokenIdx < eaSize(&pPermissionDef->eaTextTokens); iTokenIdx++)
									{
										GameTokenText *pToken = pPermissionDef->eaTextTokens[iTokenIdx];
										switch (pToken->eType)
										{
											// A costume set is being unlocked, need to
											// examine the costume set for costumes
										case kGameToken_CostumeSet:
											gclMicroTrans_addCostumeSetPermissionToCostumeList(pMTProduct, bHidden, bOwned, bNew, iPrice, pToken);
											break;

											// TODO(jm): Need to figure out if I need to wait for this.
										case kGameToken_Costume:
											break;
										}
									}
								}
							}
						}
					}

					// Remove def load request
					if (pLoad)
					{
						eaFindAndRemove(&s_eaLoadingRefs, pLoad);
						StructDestroySafe(parse_MTCostumeListReferenceLoad, &pLoad);
					}
				}
				else if (!pLoad)
				{
					gclMicroTrans_CostumeWaitForReferent(REF_STRING_FROM_HANDLE(pMTProduct->hDef), kMTCostumeListReferenceType_MicroTransactionDef);
				}
			}
		}
	}

	// Notify the callbacks
	for (iIdx = 0; iIdx < eaSize(&s_eaCostumeListCallbacks); iIdx++)
	{
		(s_eaCostumeListCallbacks[iIdx]->pHandler)(s_eaCostumeListCallbacks[iIdx]->pUserData);
	}
}

void gclMicroTrans_AddCostumeListChangedHandler(MicroTrans_CostumeListChanged pHandler, void *pUserData)
{
	MTCostumeListCallback *pCallback = NULL;
	int iIdx;

	if (!pHandler)
	{
		return;
	}

	for (iIdx = 0; iIdx < eaSize(&s_eaCostumeListCallbacks); iIdx++)
	{
		if (s_eaCostumeListCallbacks[iIdx]->pHandler == pHandler)
		{
			s_eaCostumeListCallbacks[iIdx]->pUserData = pUserData;
			return;
		}
	}

	pCallback = malloc(sizeof(MTCostumeListCallback));
	pCallback->pHandler = pHandler;
	pCallback->pUserData = pUserData;
	eaPush(&s_eaCostumeListCallbacks, pCallback);
}

void gclMicroTrans_RemoveCostumeListChangedHandler(MicroTrans_CostumeListChanged pHandler)
{
	int iIdx;
	for (iIdx = 0; iIdx < eaSize(&s_eaCostumeListCallbacks); iIdx++)
	{
		if (s_eaCostumeListCallbacks[iIdx]->pHandler == pHandler)
		{
			free(eaRemove(&s_eaCostumeListCallbacks, iIdx));
			return;
		}
	}
}

static bool s_bWaitingForTimer = false;

static void gclMicroTrans_UpdateCostumeList_TimedCB(TimedCallback *callback, F32 timeSinceLastCallback, void *pData)
{
	s_bWaitingForTimer = false;
	gclMicroTrans_UpdateCostumeList();
}

//////////////////////////////////////////////////////////////////////////
// Callbacks for when references need to be loaded

static void gclMicroTrans_ResourceCallback(enumResourceEventType eType, const char *pDictName, const char *pResourceName, void *pResource, void *pUserData)
{
	if (eType == RESEVENT_RESOURCE_ADDED)
	{
		MTCostumeListReferenceLoad *pRef = eaIndexedGetUsingString(&s_eaLoadingRefs, pResourceName);
		if (pRef)
		{
			if(!s_bWaitingForTimer)
			{
				s_bWaitingForTimer = true;
				TimedCallback_Run(gclMicroTrans_UpdateCostumeList_TimedCB, NULL, 0.1);
			}
		}
	}
}

static bool gclMicroTrans_CostumeWaitForReferent(const char *pchDefName, MTCostumeListReferenceType uType)
{
	MTCostumeListReferenceLoad *pRef = NULL;

	// Check to see if the reference is already loaded
	switch (uType)
	{
	case kMTCostumeListReferenceType_ItemDef:
		if (RefSystem_ReferentFromString(g_hItemDict, pchDefName))
			return false;
		break;
	case kMTCostumeListReferenceType_PlayerCostume:
		if (RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchDefName))
			return false;
		break;
	case kMTCostumeListReferenceType_MicroTransactionDef:
		if (RefSystem_ReferentFromString(g_hMicroTransDefDict, pchDefName))
			return false;
		break;
	case kMTCostumeListReferenceType_CostumeSet:
		if (RefSystem_ReferentFromString(g_hCostumeSetsDict, pchDefName))
			return false;
		break;
	}

	if (eaIndexedGetUsingString(&s_eaLoadingRefs, pchDefName))
	{
		// Already requested
		return true;
	}

	// Create the reference
	pRef = StructCreate(parse_MTCostumeListReferenceLoad);
	pRef->pchDefName = allocAddString(pchDefName);

	switch (uType)
	{
	case kMTCostumeListReferenceType_ItemDef:
		SET_HANDLE_FROM_STRING(g_hItemDict, pRef->pchDefName, pRef->hItemDef);
		break;
	case kMTCostumeListReferenceType_PlayerCostume:
		SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, pRef->pchDefName, pRef->hCostume);
		break;
	case kMTCostumeListReferenceType_MicroTransactionDef:
		SET_HANDLE_FROM_STRING(g_hMicroTransDefDict, pRef->pchDefName, pRef->hMTDef);
		break;
	case kMTCostumeListReferenceType_CostumeSet:
		SET_HANDLE_FROM_STRING(g_hCostumeSetsDict, pRef->pchDefName, pRef->hCostumeSet);
		break;
	}

	if (!s_eaLoadingRefs)
	{
		eaCreate(&s_eaLoadingRefs);
		eaIndexedEnable(&s_eaLoadingRefs, parse_MTCostumeListReferenceLoad);

		resDictRegisterEventCallback(g_hItemDict, gclMicroTrans_ResourceCallback, NULL);
		resDictRegisterEventCallback(g_hPlayerCostumeDict, gclMicroTrans_ResourceCallback, NULL);
		resDictRegisterEventCallback(g_hMicroTransDefDict, gclMicroTrans_ResourceCallback, NULL);
		resDictRegisterEventCallback(g_hCostumeSetsDict, gclMicroTrans_ResourceCallback, NULL);
	}

	eaPush(&s_eaLoadingRefs, pRef);
	return true;
}

static U32 s_uiLastTimestamp = 0;

// Implements a client-side cooldown on purchasing points
//  Params - 
//	 @bTest: if bTest is true it's just testing to see if it could attempt to purchase.  It doesn't set the timestamp when testing
bool gclMicroTrans_PointBuy_CanAttemptPurchase(bool bTest)
{
	U32 uiNow = timeSecondsSince2000();

	if(s_uiLastTimestamp + 10 < uiNow)
	{
		if(!bTest)
			s_uiLastTimestamp = uiNow;
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CanAttemptPointPurchase);
bool gclMicroTrans_expr_CanAttemptPointPurchase()
{
	return gclMicroTrans_PointBuy_CanAttemptPurchase(true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CanAttemptSteamPointPurchase);
bool gclMicroTrans_expr_CanAttemptSteamPointPurchase()
{
	return gclSteam_CanAttemptPurchase();
}

void gclMicroTrans_PointBuySuccess(const char *pchName)
{
	s_uiLastTimestamp = 0;
	if(pchName && !stricmp(pchName, STEAM_NOTIFY_LABEL))
		gclSteam_PurchaseActive(false);
}

void gclMicroTrans_PointBuyFailed(const char *pchName)
{
	s_uiLastTimestamp = 0;
	if(pchName && !stricmp(pchName, STEAM_NOTIFY_LABEL))
		gclSteam_PurchaseFailed();
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclMicroTrans_RecvRewards(InvRewardRequest* pRequest)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt && pRequest)
	{
		if (!s_pMTRewards)
			s_pMTRewards = StructCreate(parse_InvRewardRequest);
		else
			StructReset(parse_InvRewardRequest, s_pMTRewards);

		inv_FillRewardRequestClient(pEnt, pRequest, s_pMTRewards, false);
	}
	else
	{
		StructDestroySafe(parse_InvRewardRequest, &s_pMTRewards);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetMicroTransactionRewardList);
void gclMicroTrans_expr_GenGetMicroTransactionRewardList(SA_PARAM_NN_VALID UIGen* pGen)
{
	if (s_pMTRewards)
	{
		ui_GenSetList(pGen, &s_pMTRewards->eaRewards, parse_InventorySlot);
	}
	else
	{
		ui_GenSetList(pGen, NULL, NULL);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetMicroTransactionRewardCount);
S32 gclMicroTrans_expr_GetMicroTransactionRewardCount(void)
{
	if (s_pMTRewards)
	{
		return eaSize(&s_pMTRewards->eaRewards);
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ClearMicroTransactionRewards);
void gclMicroTrans_expr_ClearMicroTransactionRewards(void)
{
	StructDestroySafe(parse_InvRewardRequest, &s_pMTRewards);
}

// Not quite a true "enabled", but if there is a product
// with a valid price, then for all intents and purposes
// the microtransactions can be considered enabled for
// the user.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTransactionsEnabled);
int gclMicroTrans_expr_MicroTransactionsEnabled(void)
{
	if (g_pMTList)
	{
		S32 i;
		for (i = eaSize(&g_pMTList->ppProducts) - 1; i >= 0; i--)
		{
			if (microtrans_GetPrice(g_pMTList->ppProducts[i]->pProduct) >= 0)
				return 1;
		}

		return 0;
	}

	return -1;
}

//////////////////////////////////////////////////////////////////////////

#include "gclMicroTransactions_h_ast.c"
#include "gclMicroTransactions_c_ast.c"
