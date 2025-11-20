#include "ArmamentSwapCommon.h"
#include "CharacterClass.h"
#include "Entity.h"
#include "Player.h"
#include "UIGen.h"
#include "CharacterClass_h_ast.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "FCInventoryUI.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// ------------------------------------------------------------------------------------------------------------
__forceinline static ArmamentSwapInfo* GetArmamentSwapInfo(Entity *pEnt)
{
	return (pEnt && pEnt->pPlayer) ? pEnt->pPlayer->pArmamentSwapInfo : NULL;
}

// ------------------------------------------------------------------------------------------------------------
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ArmamentSwapHasClass");
bool exprArmamentSwapHasClass(SA_PARAM_OP_VALID Entity *pEntity)
{
	ArmamentSwapInfo *pArmamentSwapInfo = GetArmamentSwapInfo(pEntity);
	if (pArmamentSwapInfo)
	{
		return IS_HANDLE_ACTIVE(pArmamentSwapInfo->hClassSwap);
	}
	return false;
}

// ------------------------------------------------------------------------------------------------------------
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ArmamentSwapHasActiveItemSlots");
bool exprArmamentSwapHasActiveItemSlots(SA_PARAM_OP_VALID Entity *pEntity)
{
	ArmamentSwapInfo *pArmamentSwapInfo = GetArmamentSwapInfo(pEntity);
	if (pArmamentSwapInfo)
	{
		return eaSize(&pArmamentSwapInfo->eaActiveItemSwap) > 0;
	}
	return false;
}


// get current queued swap class
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ArmamentSwapGetClass");
const char* exprArmamentSwapGetClass(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pPlayer)
	{
		ArmamentSwapInfo *pArmamentSwapInfo = GetArmamentSwapInfo(pEntity);
		if (pArmamentSwapInfo && IS_HANDLE_ACTIVE(pArmamentSwapInfo->hClassSwap))
		{
			return REF_HANDLE_GET_STRING(pArmamentSwapInfo->hClassSwap);
		}
	}
	
	return NULL;
}

// ------------------------------------------------------------------------------------------------------------
// sets the gen's list of the current queued swap class
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ArmamentSwapGetClassList");
void exprArmamentSwapGetClassList(	SA_PARAM_NN_VALID UIGen *pGen, 
									SA_PARAM_OP_VALID Entity *pEntity)
{
	static CharacterClass **s_ppClasses = NULL;
	
	eaClear(&s_ppClasses);

	if (pEntity && pEntity->pPlayer)
	{
		ArmamentSwapInfo *pArmamentSwapInfo = GetArmamentSwapInfo(pEntity);
		if (pArmamentSwapInfo)
		{
			CharacterClass *pClass = GET_REF(pArmamentSwapInfo->hClassSwap);
			eaPush(&s_ppClasses, pClass);
		}
	}
	ui_GenSetListSafe(pGen, &s_ppClasses, CharacterClass);
}

// ------------------------------------------------------------------------------------------------------------
// get current queued swap class
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ArmamentSwapGetActiveItemSlot");
int exprArmamentSwapGetActiveItemSlot(SA_PARAM_OP_VALID Entity *pEntity, S32 iBagID, S32 iIndex)
{
	if (pEntity && pEntity->pPlayer)
	{
		ArmamentSwapInfo *pArmamentSwapInfo = GetArmamentSwapInfo(pEntity);
		if (pArmamentSwapInfo)
		{
			ArmamentActiveItemSwap* pitem = findQueuedActiveItemSwap(pArmamentSwapInfo, iBagID, iIndex);

			if (pitem)
			{
				return pitem->iActiveSlot;
			}
		}
	}

	return -1;
}

// ------------------------------------------------------------------------------------------------------------
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ArmamentSwapGetActiveItemSlots");
bool exprArmamentSwapGetActiveItemSlots(SA_PARAM_NN_VALID UIGen *pGen, 
										SA_PARAM_OP_VALID Entity *pEntity, 
										S32 minSlots)
{
	if (pEntity)
	{
		static InventorySlot **s_eaSlots = NULL;
		ArmamentSwapInfo *pArmamentSwapInfo = GetArmamentSwapInfo(pEntity);
		S32 i = 0;
		S32 numSlots = eaSize(&pArmamentSwapInfo->eaActiveItemSwap);

		eaClearFast(&s_eaSlots);

		numSlots = MAX(numSlots, minSlots);

		FOR_EACH_IN_EARRAY(pArmamentSwapInfo->eaActiveItemSwap, ArmamentActiveItemSwap, pitem)
		{
			InventoryBag *pBag = gclInvExprGenInventoryGetBag(pEntity, pitem->iBagID);
			bool setSlot = false;
			
			if (pBag && invbag_flags(pBag) & InvBagFlag_ActiveWeaponBag)
			{
				if (pitem->iActiveSlot >= 0 && pitem->iActiveSlot < eaSize(&pBag->ppIndexedInventorySlots))
				{
					eaPush(&s_eaSlots, gclInventoryUpdateSlot(pEntity, pBag, pBag->ppIndexedInventorySlots[pitem->iActiveSlot]));
					setSlot = true;
				}
			}

			if (!setSlot)
			{
				InventorySlot *pEmptySlot = gclInventoryUpdateNullSlot(pEntity, pBag, i);
				if (pEmptySlot)
					eaPush(&s_eaSlots, pEmptySlot);
			}

			++i;
		}
		FOR_EACH_END

		ui_GenSetListSafe(pGen, &s_eaSlots, InventorySlot);
		return true;
	}
	else
	{
		ui_GenSetList(pGen, NULL, parse_InventorySlot);
		return false;
	}
}