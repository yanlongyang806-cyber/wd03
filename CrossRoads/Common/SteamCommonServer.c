#include "SteamCommonServer.h"
#include "SteamCommon.h"
#include "utils.h"
#include "earray.h"
#include "EString.h"
#include "httpAsync.h"
#include "url.h"
#include "GlobalTypes.h"
#include "AccountProxyCommon.h"
#include "Money.h"
#include "rand.h"
#include "wininclude.h"
#include "objTransactions.h"
#include "accountnet.h"
#include "AppLocale.h"
#include "GameStringFormat.h"
#include "Alerts.h"

#include "AutoGen/SteamCommonServer_c_ast.h"
#include "AutoGen/accountnet_h_ast.h"
#include "AutoGen/Money_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define STEAMWALLET_CURRENCY_PLACEHOLDER "_SteamWallet"

AUTO_STRUCT;
typedef struct SteamPurchaseProductData
{
	U32 account_id;
	Language lang; AST(INT)
	char *ip;
	U64 steam_id;
	char *category;
	U32 product_id;
	char *currency;
	ccSteamPurchaseProductCallback cb; NO_AST
	void *userdata; NO_AST
	bool bAuthed;
} SteamPurchaseProductData;

static void purchaseCallCB(SteamPurchaseProductData *data, bool success, const char *msg, bool raw)
{
	if(data->cb)
		data->cb(success, raw?msg:langTranslateMessageKeyDefault(data->lang, msg, "[Unknown Steam Result Message]"), data->userdata);
}

static void purchaseAPCaptureOnlyCallback(TransactionReturnVal *pReturnVal, SteamPurchaseProductData *data)
{
	bool bSuccess = false;
	const char *pKey = NULL;

	RemoteCommandCheck_aslAPCmdCaptureOnly(pReturnVal, &bSuccess);

	if (bSuccess && !data->bAuthed)
	{
		// If the user canceled authorization, then any error message we display here would just be a duplicate
		// Wait for the AS response to display the "canceled" message
		StructDestroy(parse_SteamPurchaseProductData, data);
		return;
	}
	
	if(bSuccess && data->bAuthed)
	{
		pKey = "MicroTrans_Purchase_Pending";
	}
	else // (!bSuccess)
	{
		// The 99% likelihood here is that we failed to look up the order ID at the Account Proxy,
		// which means the lock expired
		pKey = "MicroTrans_Purchase_Timeout";
	}

	purchaseCallCB(data, bSuccess && data->bAuthed, pKey, false);
	StructDestroy(parse_SteamPurchaseProductData, data);
}

void ccSteamOnMicroTxnAuthorizationResponse(bool bAuthed, U32 uAccountID, U64 uOrderID, Language eLanguage, ccSteamPurchaseProductCallback pCallback, void *pUserData)
{
	char orderStr[32];
	SteamPurchaseProductData *data = NULL;

	data = StructCreate(parse_SteamPurchaseProductData);
	data->cb = pCallback;
	data->userdata = pUserData;
	data->lang = eLanguage;
	data->bAuthed = bAuthed;

	sprintf(orderStr, "%"FORM_LL"u", uOrderID);
	RemoteCommand_aslAPCmdCaptureOnly(objCreateManagedReturnVal(purchaseAPCaptureOnlyCallback, data), GLOBALTYPE_ACCOUNTPROXYSERVER, 0, uAccountID, 0, orderStr, bAuthed);
}

static void purchaseAPAuthCaptureCallback(SA_PARAM_NN_VALID TransactionReturnVal *return_value, SA_PRE_NN_VALID SA_POST_P_FREE SteamPurchaseProductData *data)
{
	AuthCaptureResultInfo * response = NULL;

	if (RemoteCommandCheck_aslAPCmdAuthCapture(return_value, &response) == TRANSACTION_OUTCOME_SUCCESS)
	{
		const char *pchMsgKey = GetAuthCapture_ErrorMsgKey(response->eResult);
		if(!pchMsgKey)
			pchMsgKey = "MicroTrans_Purchase_GenericFailure";

		// If the purchase failed, we call back immediately so a notification gets sent to the client
		// Otherwise, don't call it now - it'll get called when capture succeeds
		if(response->eResult != PURCHASE_RESULT_PENDING)
		{
			purchaseCallCB(data, false, pchMsgKey, false);
		}

		StructDestroy(parse_AuthCaptureResultInfo, response);
	}
	else
	{
		purchaseCallCB(data, false, "MicroTrans_Purchase_GenericFailure", false);
	}

	StructDestroySafe(parse_SteamPurchaseProductData, &data);
}

static void purchaseGetProductCallback(const AccountProxyProduct *product, SteamPurchaseProductData *data)
{
	AuthCaptureRequest *request = NULL;
	TransactionItem *item = NULL;

	if(!product)
	{
		// Error handling, how did this happen?
		purchaseCallCB(data, false, "MicroTrans_Purchase_GenericFailure", false);
		StructDestroy(parse_SteamPurchaseProductData, data);
		return;
	}
	
	request = StructCreate(parse_AuthCaptureRequest);
	request->bAuthOnly = true;
	request->uAccountID = data->account_id;
	request->uSteamID = data->steam_id;
	request->eProvider = TPROVIDER_Steam;

	request->pCategory = StructAllocString(data->category);
	request->pCurrency = StructAllocString(data->currency);
	request->pIP = StructAllocString(data->ip);
	request->pLocCode = StructAllocString(locGetCode(locGetIDByLanguage(data->lang)));
	
	item = StructCreate(parse_TransactionItem);
	item->uProductID = data->product_id;
	eaPush(&request->eaItems, item);

	RemoteCommand_aslAPCmdAuthCapture(objCreateManagedReturnVal(purchaseAPAuthCaptureCallback, data), GLOBALTYPE_ACCOUNTPROXYSERVER, 0, request);
	StructDestroy(parse_AuthCaptureRequest, request);
}

void ccSteamPurchaseProduct(U32 account_id, Language lang, U64 steam_id, const char *ip, const char *category, U32 product_id, const char *currency, ccSteamPurchaseProductCallback cb, void *userdata)
{
	SteamPurchaseProductData *data = StructAlloc(parse_SteamPurchaseProductData);
	data->account_id = account_id;
	data->lang = lang;
	data->steam_id = steam_id;
	if(ip)
		data->ip = StructAllocString(ip);
	data->category = StructAllocString(category);
	data->product_id = product_id;
	data->currency = StructAllocString(currency);
	data->cb = cb;
	data->userdata = userdata;
	APGetProductByID(category, product_id, purchaseGetProductCallback, data);
}

#include "AutoGen/SteamCommonServer_c_ast.c"
