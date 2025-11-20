#if !PLATFORM_CONSOLE
#include "utils.h"
#include "earray.h"
#include "EString.h"
#include "gclSteam.h"
#include "GfxConsole.h"
#include "GlobalTypes.h"
#include "SteamCommon.h"
#include "gclSendToServer.h"
#include "GlobalStateMachine.h"
#include "gclBaseStates.h"
#include "gclLogin.h"
#include "textparser.h"
#include "net.h"
#include "NotifyCommon.h"
#include "structNet.h"

#include "AutoGen/SteamCommon_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static char *s_pchSteamCurrency = NULL;

// This is the timestamp that the last steam purchase was started
static U32 s_iSteamPurchaseTimestamp = 0;
static bool s_bSteamPurchaseActive = false;

AUTO_RUN;
void gclSteamAutoRun(void)
{
	gclSteamInit();
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclSteamCmdSetAchievement(const char *pchName)
{
	gclSteamSetAchievement(pchName);
}

AUTO_COMMAND ACMD_NAME(steam_reset) ACMD_HIDE;
void gclSteamCmdReset(void)
{
	gclSteamReset();
}

AUTO_COMMAND ACMD_NAME(steam_id) ACMD_HIDE ACMD_ACCESSLEVEL(0);
void gclSteamCmdID(void)
{
	conPrintf("%"FORM_LL"u\n", gclSteamID());
}

bool gclSteam_CanAttemptPurchase()
{
	return !s_bSteamPurchaseActive && !s_iSteamPurchaseTimestamp;
}

void gclSteam_PurchaseActive(bool bActive)
{
	s_bSteamPurchaseActive = bActive;
	if(bActive)
		s_iSteamPurchaseTimestamp = timeSecondsSince2000();
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void gclSteam_PurchaseFailed()
{
	s_bSteamPurchaseActive = false;
	s_iSteamPurchaseTimestamp = 0;
}

void gclSteamOnMicroTxnAuthorizationResponse(bool bAuthorized, U64 uOrderID)
{
	// If s_iSteamPurchaseTimestamp is 0, a purchase failed before this auth response was received
	// Most likely this indicates a timeout
	if(!s_iSteamPurchaseTimestamp)
	{
		if(bAuthorized)
			notify_NotifySend(NULL, kNotifyType_MicroTrans_PointBuyFailed, TranslateMessageKey("MicroTrans_Purchase_Timeout"), NULL, NULL);
		return;
	}

	//I got a response, so set the tstamp back to 0
	s_iSteamPurchaseTimestamp = 0;

	//If I got a response but a purchase wasn't active.  This flag gets cleared whenever the client changes states
	// (Enter/Exit Gameplay, Exit Login and Enter AccountServer Login)
	if(!s_bSteamPurchaseActive)
	{
		if(bAuthorized)
			notify_NotifySend(NULL, kNotifyType_MicroTrans_PointBuyFailed, TranslateMessageKey("MicroTrans_Purchase_GenericFailure"), NULL, NULL);
		return;
	}

	if(GSM_IsStateActive(GCL_LOGIN))
	{
		Packet *pPak = pktCreate(gpLoginLink, TOLOGIN_STEAM_MICROTXN_AUTHZ_RESPONSE);
		SteamMicroTxnAuthorizationResponse *pResponse = StructCreate(parse_SteamMicroTxnAuthorizationResponse);
		pResponse->authorized = bAuthorized;
		pResponse->order_id = uOrderID;
		ParserSendStruct(parse_SteamMicroTxnAuthorizationResponse, pPak, pResponse);
		pktSend(&pPak);
		StructDestroy(parse_SteamMicroTxnAuthorizationResponse, pResponse);
	}
	else if(gclServerIsConnected())
	{
		ServerCmd_gslSteamOnMicroTxnAuthorizationResponse(bAuthorized, uOrderID);
	}
}

#endif