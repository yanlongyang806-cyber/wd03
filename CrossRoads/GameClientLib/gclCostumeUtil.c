
#include "CostumeCommon.h"
#include "CostumeCommonEntity.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "gclCostumeView.h"
#include "gclCostumeUI.h"
#include "gclEntity.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "CharacterClass.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeUtil_GetFreeChangeTokens);
int CostumeUtilExpr_GetFreeChangeTokens(void)
{
	Entity *pEnt = entActivePlayerPtr();

	return costumeEntity_GetFreeChangeTokens(NULL, pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeUtil_GetFreeFlexChangeTokens);
int CostumeUtilExpr_GetFreeFlexChangeTokens(void)
{
	Entity *pEnt = entActivePlayerPtr();

	return costumeEntity_GetFreeFlexChangeTokens(pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeUtil_GetAccountChangeTokens);
int CostumeUtilExpr_GetAccountChangeTokens(void)
{
	Entity *pEnt = entActivePlayerPtr();
	const GameAccountData *pData = entity_GetGameAccount(pEnt);

	return costumeEntity_GetAccountChangeTokens(pEnt, pData);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeUtil_CanChangeForFree);
int CostumeUtilExpr_CanChangeForFree()
{
	Entity *pEnt = entActivePlayerPtr();
	return costumeEntity_CanChangeForFree(pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeUtil_CanPayCost);
int CostumeUtilExpr_CanPayCost(const char *pchPayMethod)
{
	int bReturnVal = 0;
	int ePayMethod = StaticDefineIntGetInt(PCPaymentMethodEnum, pchPayMethod);
	Entity *pEnt = entActivePlayerPtr();
	
	if(!pEnt || ePayMethod < 0)
		return bReturnVal;

	switch(ePayMethod)
	{
		case kPCPay_Resources:
		{
			bReturnVal = ( inv_GetNumericItemValue(pEnt, "Resources") >= CostumeCreator_GetCost() );
			break;
		}
		case kPCPay_FreeToken:
		{
			bReturnVal = (costumeEntity_GetFreeChangeTokens(NULL, pEnt) > 0);
			break;
		}
		case kPCPay_FreeFlexToken:
		{
			bReturnVal = (costumeEntity_GetFreeFlexChangeTokens(pEnt) > 0);
			break;
		}
		case kPCPay_GADToken:
		{
			const GameAccountData *pData = entity_GetGameAccount(pEnt);
			bReturnVal = ( costumeEntity_GetAccountChangeTokens(pEnt, pData) > 0);
			break;
		}
		case kPCPay_Default:
		{
			bReturnVal = ( costumeEntity_CanChangeForFree(pEnt) || (inv_GetNumericItemValue(pEnt, "Resources") >= CostumeCreator_GetCost()) );
			break;
		}
	}

	return bReturnVal;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(CostumeUtil_ShowItemsFromCharacterPath);
void CostumeUtilExpr_ShowItemsFromCharacterPath(SA_PARAM_OP_VALID CharacterPath *pPath)
{
	if (pPath)
	{
		ItemDefRef **eaItems = NULL;
		S32 i;

		for (i = eaSize(&pPath->eaPreviewItems) - 1; i >= 0; i--) {
			ItemDefRef *pItem = StructCreate(parse_ItemDefRef);
			COPY_HANDLE(pItem->hDef, pPath->eaPreviewItems[i]->hDef);
			eaPush(&eaItems, pItem);
		}

		CostumeUI_AddInventoryItems(eaItems);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(CostumeUtil_ShowGrantItemsFromCharacterPath);
void CostumeUtilExpr_ShowGrantItemsFromCharacterPath(SA_PARAM_OP_VALID CharacterPath *pPath)
{
	if (pPath)
	{
		ItemDefRef **eaItems = NULL;
		S32 i;

		for (i = eaSize(&pPath->eaDefaultItems) - 1; i >= 0; i--) {
			ItemDefRef *pItem = StructCreate(parse_ItemDefRef);
			COPY_HANDLE(pItem->hDef, pPath->eaDefaultItems[i]->hItem);
			eaPush(&eaItems, pItem);
		}

		CostumeUI_AddInventoryItems(eaItems);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(CostumeUtil_ClearItems);
void CostumeUtilExpr_ClearItems(void)
{
	CostumeUI_AddInventoryItems(NULL);
}
