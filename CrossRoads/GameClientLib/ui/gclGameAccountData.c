
#include "ChatCommonStructs.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "gclEntity.h"
#include "gclFriendsIgnore.h"
#include "Player.h"
#include "earray.h"
#include "UIGen.h"

#include "AutoGen/microtransactions_common_h_ast.h"
#include "AutoGen/GameAccountDataCommon_h_ast.h"
#include "AutoGen/GameAccountData_h_ast.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/gclGameAccountData_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_STRUCT;
typedef struct GameAccountNumericPurchaseUI
{
	const char* pchName; AST(UNOWNED)
	const char* pchDisplayName; AST(UNOWNED)
	const char* pchDescription; AST(UNOWNED)
	REF_TO(ItemDef) hRequiredNumericItem; AST(REFDICT(ItemDef))
	int iNumericCost;
} GameAccountNumericPurchaseUI;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GAD_IsRecruitAccount");
bool gclGAD_IsRecruitAccount(U32 accountId)
{
	Entity *pEnt = entActivePlayerPtr();
	const GameAccountData *pData = entity_GetGameAccount(pEnt);
	if(pData)
	{
		return !!GAD_AccountIsRecruit(pData, accountId);
	}
	return false;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GAD_IsRecruit");
bool gclGAD_IsRecruit(const char *pchHandle)
{
	ChatPlayerStruct *pPlayer = FindFriendByHandle(pchHandle);
	if(pPlayer)
		return !!gclGAD_IsRecruitAccount(pPlayer->accountID);
	else
		return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GAD_IsRecruiterAccount");
bool gclGAD_IsRecruiterAccount(U32 accountId)
{
	Entity *pEnt = entActivePlayerPtr();
	const GameAccountData *pData = entity_GetGameAccount(pEnt);
	if(pData)
	{
		return !!GAD_AccountIsRecruiter(pData, accountId);
	}
	return false;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GAD_IsRecruiter");
bool gclGAD_IsRecruiter(const char *pchHandle)
{
	ChatPlayerStruct *pPlayer = FindFriendByHandle(pchHandle);
	if(pPlayer)
		return !!gclGAD_IsRecruiterAccount(pPlayer->accountID);
	else
		return false;
}

AUTO_COMMAND ACMD_NAME("WarpToRecruitHandle") ACMD_ACCESSLEVEL(0) ACMD_PRODUCTS(StarTrek, FightClub);
void gclGAD_WarpToRecruitHandle(const char *pchHandle)
{
	ChatPlayerStruct *pPlayer = FindFriendByHandle(pchHandle);
	if(pPlayer && (gclGAD_IsRecruitAccount(pPlayer->accountID) || gclGAD_IsRecruiterAccount(pPlayer->accountID)))
	{
		ServerCmd_WarpToRecruit(pPlayer->accountID);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GAD_GetWarpCooldownInHours");
int gclGAD_GetWarpCooldownInHours(void)
{
	return (RECRUIT_WARP_TIME / 3600);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GAD_GetRemainingWarpCooldown");
int gclGAD_GetRemainingWarpCooldown(void)
{
	Entity *pEnt = entActivePlayerPtr();
	U32 iCurrentTime = timeServerSecondsSince2000();
	if(pEnt && pEnt->pPlayer)
		return (pEnt->pPlayer->uiLastRecruitWarpTime + RECRUIT_WARP_TIME > iCurrentTime ?
				pEnt->pPlayer->uiLastRecruitWarpTime + RECRUIT_WARP_TIME - iCurrentTime : 0);
	else
		return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GAD_GetMinimumCostForNumericPurchase");
int gclGAD_GetMinimumCostForNumericPurchase(const char* pchNumeric, S32 eCategory)
{
	Entity *pEnt = entActivePlayerPtr();
	GameAccountData *pData = entity_GetGameAccount(pEnt);
	return GAD_NumericPurchaseGetMinimumCostForAccount(pData, pchNumeric, eCategory, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GAD_GetNumericPurchaseList");
int gclGAD_GetNumericPurchaseList(SA_PARAM_NN_VALID UIGen* pGen, S32 eCategory)
{
	GameAccountNumericPurchaseUI*** peaData = ui_GenGetManagedListSafe(pGen, GameAccountNumericPurchaseUI);
	int i, iCount = 0;
	Entity *pEnt = entActivePlayerPtr();
	const GameAccountData *pData = entity_GetGameAccount(pEnt);

	if (pData)
	{
		for (i = 0; i < eaSize(&g_GameAccountDataNumericPurchaseDefs.eaDefs); i++)
		{
			GameAccountDataNumericPurchaseDef* pDef = g_GameAccountDataNumericPurchaseDefs.eaDefs[i];
		
			if (eCategory > kGameAccountDataNumericPurchaseCategory_None && pDef->eCategory != eCategory)
			{
				continue;
			}
			if (GAD_CanMakeNumericPurchaseCheckKeyValues(pData, pDef))
			{
				GameAccountNumericPurchaseUI* pPurchaseData = eaGetStruct(peaData, parse_GameAccountNumericPurchaseUI, iCount++);
				pPurchaseData->pchName = pDef->pchName;
				pPurchaseData->pchDisplayName = TranslateDisplayMessage(pDef->msgDisplayName);
				pPurchaseData->pchDescription = TranslateDisplayMessage(pDef->msgDescription);
				SET_HANDLE_FROM_STRING("ItemDef", pDef->pchNumericItemDef, pPurchaseData->hRequiredNumericItem);
				pPurchaseData->iNumericCost = pDef->iNumericCost;
			}
		}
	}
	eaSetSizeStruct(peaData, parse_GameAccountNumericPurchaseUI, iCount);
	ui_GenSetManagedListSafe(pGen, peaData, GameAccountNumericPurchaseUI, true);
	return iCount;
}

#include "AutoGen/gclGameAccountData_c_ast.c"
