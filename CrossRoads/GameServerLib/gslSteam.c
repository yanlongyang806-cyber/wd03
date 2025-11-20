#include "utils.h"
#include "earray.h"
#include "EString.h"
#include "SteamCommon.h"
#include "SteamCommonServer.h"
#include "gslSendToClient.h"
#include "Entity.h"
#include "EntityLib.h"
#include "Player.h"
#include "GameServerLib.h"
#include "NotifyCommon.h"
#include "AccountProxyCommon.h"
#include "mission_common.h"
#include "Expression.h"
#include "stdtypes.h"

#include "autogen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static void authCallback(bool success, const char *msg, void *userdata)
{
	Entity *ent = entFromEntityRefAnyPartition((EntityRef)(intptr_t)userdata);
	if(!ent)
		return;
	notify_NotifySend(ent, (success?kNotifyType_MicroTrans_PointBuySuccess:kNotifyType_MicroTrans_PointBuyFailed), msg, STEAM_NOTIFY_LABEL, NULL);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslSteamPurchaseProduct(SA_PARAM_NN_VALID Entity *ent, U64 steam_id, const char *category, U32 product_id, const char *currency)
{
	char ip[32];
	ClientLink *link = entGetClientLink(ent);
	if(link)
		linkGetIpStr(link->netLink, SAFESTR(ip));
	else
		ip[0] = '\0';
	ccSteamPurchaseProduct(entGetAccountID(ent), entGetLanguage(ent), steam_id, ip, category, product_id, currency, authCallback, (void*)(intptr_t)entGetRef(ent));
}

static void purchaseCallback(bool success, const char *msg, void *userdata)
{
	Entity *ent = entFromEntityRefAnyPartition((EntityRef)(intptr_t)userdata);
	if(!ent)
		return;
	notify_NotifySend(ent, (success?kNotifyType_MicroTrans_PointBuyPending:kNotifyType_MicroTrans_PointBuyFailed), msg, STEAM_NOTIFY_LABEL, NULL);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslSteamOnMicroTxnAuthorizationResponse(SA_PARAM_NN_VALID Entity *ent, bool authorized, U64 order_id)
{
	ccSteamOnMicroTxnAuthorizationResponse(authorized, entGetAccountID(ent), order_id, entGetLanguage(ent), purchaseCallback, (void*)(intptr_t)entGetRef(ent));
}

AUTO_COMMAND_REMOTE;
void gslAPCmdSteamCaptureComplete(GlobalType eType, ContainerID uID, U32 uAccountID, PurchaseResult eResult)
{
	Entity *pEnt = entFromContainerIDAnyPartition(eType, uID);
	const char *pMsg = GetAuthCapture_ErrorMsgKey(eResult);

	if(pEnt)
	{
		if(eResult == PURCHASE_RESULT_COMMIT)
		{
			notify_NotifySend(pEnt, kNotifyType_MicroTrans_PointBuySuccess, langTranslateMessageKey(entGetLanguage(pEnt), pMsg), STEAM_NOTIFY_LABEL, NULL);
		}
		else
		{
			notify_NotifySend(pEnt, kNotifyType_MicroTrans_PointBuyFailed, langTranslateMessageKey(entGetLanguage(pEnt), pMsg), STEAM_NOTIFY_LABEL, NULL);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslSteamEnabled(SA_PARAM_NN_VALID Entity *ent)
{
	if(ent && ent->pPlayer)
		ent->pPlayer->pPlayerAccountData->bSteamLogin = 1;
}

AUTO_EXPR_FUNC(player, Mission) ACMD_NAME(EntityHasSteamLogin);
S32 exprEntHasSteamLogin(ExprContext *pContext)
{
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);

	if(SAFE_MEMBER2(pEnt,pPlayer,pPlayerAccountData))
		return pEnt->pPlayer->pPlayerAccountData->bSteamLogin;

	return 0;
}