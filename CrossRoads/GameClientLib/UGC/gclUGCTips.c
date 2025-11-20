/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "UGCTipsCommon.h"
#include "Entity.h"
#include "Player.h"
#include "gclEntity.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "uiGen.h"
#include "gameAccountDataCommon.h"
#include "ugcProjectCommon.h"

#include "gclUGC.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

#include "Autogen/gclUGCTips_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

/////////////////////////////////////////////////////////////////////////
// Withdrawal

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FoundryTips_Withdraw");
void gclUGCTipsWithdraw(int iWithdrawAmount)
{
	if (UGCTipsEnabled())
	{
	    ServerCmd_gslUGCTipsWithdraw(iWithdrawAmount);
	}
}

AUTO_COMMAND ACMD_NAME("FoundryTips_Withdraw") ACMD_ACCESSLEVEL(0);
void gclUGCTipsWithdrawCmd(int iWithdrawAmount)
{
	if (UGCTipsEnabled())
	{
		ServerCmd_gslUGCTipsWithdraw(iWithdrawAmount);
	}
}

/////////////////////////////////////////////////////////////////////////
// Amounts List

AUTO_STRUCT;
typedef struct TipAmount
{
	int iTipAmount;
} TipAmount;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FoundryTipsGetTipAmountList");
bool gclUGCTipsGetTipAmountList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
    Entity* pEnt = entActivePlayerPtr();
	TipAmount*** peaTipAmounts = (TipAmount***)ui_GenGetManagedList(pGen, parse_TipAmount);
	int i, iCount = 0;
	int iCurrentPlayerTippableBalance=0;
	TipAmount* pTipAmount = NULL;
    ItemDef *tipsItemDef;

	if (!UGCTipsEnabled())
	{
		return(false);
	}

    tipsItemDef = GET_REF(gUGCTipsConfig.hTipsNumeric);
	
	if (pEnt==NULL || tipsItemDef==NULL)
	{
		return false;
	}

	// Get the player balance
	iCurrentPlayerTippableBalance=inv_GetNumericItemValue(pEnt, tipsItemDef->pchName);

	// Create an empty slot
	pTipAmount = eaGetStruct(peaTipAmounts, parse_TipAmount, iCount++);
	pTipAmount->iTipAmount = 0;


	// Get the list of tipAmounts
	for (i=0;i<ea32Size(&(gUGCTipsConfig.pTipAmounts));i++)
	{
		int iTipAmount = gUGCTipsConfig.pTipAmounts[i];

		if (iTipAmount <= iCurrentPlayerTippableBalance)
		{
			pTipAmount = eaGetStruct(peaTipAmounts, parse_TipAmount, iCount++);
			pTipAmount->iTipAmount = iTipAmount;
		}
	}
	eaSetSizeStruct(peaTipAmounts, parse_TipAmount, iCount);
	ui_GenSetManagedList(pGen, peaTipAmounts, parse_TipAmount, true);
	return true;
}

/////////////////////////////////////////////////////////////////////////
// Error checkers and queries

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FoundryTipsEnabled");
bool gclUGCTipsEnabled(void)
{
	return(UGCTipsEnabled());
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FoundryTipsAuthorAlreadyTipped");
bool gclUGCTipsAuthorAlreadyTipped(SA_PARAM_OP_VALID UGCProject *pProject)
{
	Entity* pEnt = entActivePlayerPtr();
	GameAccountData* pGameAccountData;
	
	if (pEnt!=NULL && pProject!=NULL && UGCTipsEnabled())
	{
		pGameAccountData = entity_GetGameAccount(pEnt);
		if (pGameAccountData!=NULL)
		{
			return(UGCTipsAlreadyTippedAuthor(pGameAccountData, pProject->iOwnerAccountID));
		}
	}
	return(false);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FoundryTipsAccountAtTipLimit");
bool gclUGCTipsAccountAtTipLimit()
{
	Entity* pEnt = entActivePlayerPtr();
	GameAccountData* pGameAccountData;
	
	if (pEnt!=NULL && UGCTipsEnabled())
	{
		pGameAccountData = entity_GetGameAccount(pEnt);
		if (pGameAccountData!=NULL)
		{
			return(UGCTipsAlreadyTippedMaxTimes(pGameAccountData));
		}
	}
	return(false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FoundryTipsAccountTipsRemaining");
bool gclUGCTipsAccountTipsRemaining()
{
	Entity* pEnt = entActivePlayerPtr();
	GameAccountData* pGameAccountData;
	
	if (pEnt!=NULL && UGCTipsEnabled())
	{
		pGameAccountData = entity_GetGameAccount(pEnt);
		if (pGameAccountData!=NULL)
		{
			return(UGCTipsAllowedTipsRemaining(pGameAccountData));
		}
	}
	return(0);
}



AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FoundryTips_CanTip);
bool gclUGCTips_CanTip(SA_PARAM_OP_VALID UGCProject *pProject)
{
	Entity* pEnt = entActivePlayerPtr();

	if (!UGCTipsEnabled())
	{
		return false;
	}

	if (!pEnt || !pProject)
	{
		return false;
	}
	if (gclUGC_PlayerIsOwner(pProject))
	{
		return(false);
	}

	if (gclUGCTipsAuthorAlreadyTipped(pProject) || gclUGCTipsAccountAtTipLimit())
	{
		return(false);
	}
		
	if (gclUGC_PlayerJustCompletedOrDroppedUGCMission(pProject->id))
	{
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FoundryTipsGetNumericValue);
int gclUGCTips_GetNumericValue(void)
{
	Entity* pEnt = entActivePlayerPtr();
	ItemDef* tipsItemDef = GET_REF(gUGCTipsConfig.hTipsNumeric);

	if( !UGCTipsEnabled() || !pEnt || !tipsItemDef ) {
		return 0;
	}

	return inv_GetNumericItemValue(pEnt, tipsItemDef->pchName);
}


/////////////////////////////////////////////////////////////
// Give a Tip

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FoundryTips_GiveTip);
void gclUGCTips_GiveTip(SA_PARAM_OP_VALID UGCProject *pProject, int iTipAmount, int iTipIndex)
{
	if (pProject && gclUGCTips_CanTip(pProject) && iTipAmount > 0 && iTipIndex > 0 )
	{
		ServerCmd_gslUGCTips_GiveTip(pProject, iTipAmount, iTipIndex);
	}
}

/////////////////////////////////////////////////////////////
// Query tip balance that works in both multi- and single-shard environments
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GAD_GetFoundryTipBalance");
int gclGAD_GetFoundryTipBalance()
{
	Entity *pEnt = entActivePlayerPtr();
	if(pEnt)
	{
		GameAccountData* pData = entity_GetGameAccount(pEnt);
		if(pData)
		{
			if(gConf.bDontAllowGADModification)
				return gad_GetAccountValueInt(pData, microtrans_GetShardFoundryTipBucketKey());
			else
				return pData->iFoundryTipBalance;
		}
	}

	return 0;
}

#include "Autogen/gclUGCTips_c_ast.c"
