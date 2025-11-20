#include "earray.h"
#include "entity.h"
#include "Player.h"
#include "UIGen.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "inventoryCommon.h"
#include "itemEnums.h"
#include "itemEnums_h_ast.h"
#include "ItemUpgrade.h"
#include "GameAccountDataCommon.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "Autogen/FCItemUpgradeUI_c_ast.h"
#include "AutoGen/itemupgrade_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_STRUCT;
typedef struct ItemUpgradeUIState
{
	InventorySlotReference SourceSlot;
	InventorySlotReference ModSlot;
	Item *pSourceItem; AST(UNOWNED)
	Item *pModItem; AST(UNOWNED)

	InventorySlotReference SourceSlotPrev;
	InventorySlotReference ModSlotPrev;
	U64 uiSourceItemId;
	U64 uiModItemId;

	int iWillConsume;
	int iCount;
	ItemDef *pResultItemDef; AST(UNOWNED)
	Item *pResultItem; 
	F32 fChance;

} ItemUpgradeUIState;

static ItemUpgradeUIState *s_pItemUpgradeState = NULL;
static bool s_bItemUpgradeDeinit = false;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_Init);
void ItemUpgradeUI_Init()
{
	s_pItemUpgradeState = StructCreate(parse_ItemUpgradeUIState);
	//if (pGen)
	//{
	//	ui_GenSetManagedPointer(
	//		pGen,
	//		StructCreate(parse_ItemUpgradeUIState), 
	//		parse_ItemUpgradeUIState, 
	//		true);
	//}
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_Deinit);
void ItemUpgradeUI_Deinit()
{
	s_bItemUpgradeDeinit = true;
}

ItemUpgradeUIState* ItemUpgradeUI_GetItemUpgradeUIState()
{
	//ItemUpgradeUIState *pUpgradeState = NULL;
	//while (pGen && !pUpgradeState)
	//{
	//	pUpgradeState = ui_GenGetPointer(pGen, parse_ItemUpgradeUIState, NULL);
	//	pGen = pGen->pParent;
	//}
	//return pUpgradeState;
	return s_pItemUpgradeState;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_GetSourceInvSlotRef);
SA_RET_OP_VALID InventorySlotReference* ItemUpgradeUI_GetSourceInvSlotRef(SA_PARAM_OP_VALID UIGen* pGen)
{
	ItemUpgradeUIState *pUpgradeState = ItemUpgradeUI_GetItemUpgradeUIState();
	return pUpgradeState ? &pUpgradeState->SourceSlot : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_GetModInvSlotRef);
SA_RET_OP_VALID InventorySlotReference* ItemUpgradeUI_GetModInvSlotRef(SA_PARAM_OP_VALID UIGen* pGen)
{
	ItemUpgradeUIState *pUpgradeState = ItemUpgradeUI_GetItemUpgradeUIState();
	return pUpgradeState ? &pUpgradeState->ModSlot : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_GetSourceInventorySlot);
SA_RET_OP_VALID InventorySlot* ItemUpgradeUI_GetSourceInventorySlot(SA_PARAM_OP_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	ItemUpgradeUIState *pUpgradeState = ItemUpgradeUI_GetItemUpgradeUIState();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	return (pUpgradeState && pEnt && pExtract) ? InvSlotRef_GetInventoySlot(pEnt, &pUpgradeState->SourceSlot, pExtract) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_GetModInventorySlot);
SA_RET_OP_VALID InventorySlot* ItemUpgradeUI_GetModInventorySlot(SA_PARAM_OP_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	ItemUpgradeUIState *pUpgradeState = ItemUpgradeUI_GetItemUpgradeUIState();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	return (pUpgradeState && pEnt && pExtract) ? InvSlotRef_GetInventoySlot(pEnt, &pUpgradeState->ModSlot, pExtract) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_GetSourceItem);
SA_RET_OP_VALID Item* ItemUpgradeUI_GetSourceItem(SA_PARAM_OP_VALID UIGen* pGen)
{
	ItemUpgradeUIState *pUpgradeState = ItemUpgradeUI_GetItemUpgradeUIState();
	return SAFE_MEMBER(pUpgradeState, pSourceItem);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_GetSourceCount);
int ItemUpgradeUI_GetSourceCount(SA_PARAM_OP_VALID UIGen* pGen)
{
	ItemUpgradeUIState *pUpgradeState = ItemUpgradeUI_GetItemUpgradeUIState();
	return SAFE_MEMBER(pUpgradeState, iCount);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_GetModItem);
SA_RET_OP_VALID Item* ItemUpgradeUI_GetModItem(SA_PARAM_OP_VALID UIGen* pGen)
{
	ItemUpgradeUIState *pUpgradeState = ItemUpgradeUI_GetItemUpgradeUIState();
	return SAFE_MEMBER(pUpgradeState, pModItem);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_GetResultantItem);
SA_RET_OP_VALID Item* ItemUpgradeUI_GetResultantItem(SA_PARAM_OP_VALID UIGen* pGen)
{
	ItemUpgradeUIState *pUpgradeState = ItemUpgradeUI_GetItemUpgradeUIState();
	return SAFE_MEMBER(pUpgradeState, pResultItem);
}

void ItemUpgradeUI_Update(SA_PARAM_OP_VALID Entity *pEnt)
{
	// Stupid hack -- come up with better solution later
	ItemUpgradeUIState *pUpgradeState;
	if (s_bItemUpgradeDeinit)
	{
		StructDestroySafe(parse_ItemUpgradeUIState, &s_pItemUpgradeState);
		s_bItemUpgradeDeinit = false;
	}

	pUpgradeState = ItemUpgradeUI_GetItemUpgradeUIState();
	if (pUpgradeState)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventorySlot *pSourceSlot = pEnt ? InvSlotRef_GetInventoySlot(pEnt, &pUpgradeState->SourceSlot, pExtract) : NULL;
		InventorySlot *pModSlot = pEnt ? InvSlotRef_GetInventoySlot(pEnt, &pUpgradeState->ModSlot, pExtract) : NULL;
		ItemDef *pBaseDef = NULL;
		ItemDef *pModifier = NULL;
		ItemDef *pResultDef = NULL;

		pUpgradeState->pSourceItem = SAFE_MEMBER(pSourceSlot, pItem);
		pUpgradeState->pModItem = SAFE_MEMBER(pModSlot, pItem);

		// If the source or mod change slots, record the ID so they aren't null'd out. 
		if (pUpgradeState->SourceSlot.eBagID != pUpgradeState->SourceSlotPrev.eBagID
			|| pUpgradeState->SourceSlot.iIndex != pUpgradeState->SourceSlotPrev.iIndex)
		{
			pUpgradeState->uiSourceItemId = SAFE_MEMBER(pUpgradeState->pSourceItem, id);
		}
		if (pUpgradeState->ModSlot.eBagID != pUpgradeState->ModSlot.eBagID
			|| pUpgradeState->ModSlot.iIndex != pUpgradeState->ModSlot.iIndex)
		{
			pUpgradeState->uiModItemId = SAFE_MEMBER(pUpgradeState->pModItem, id);
		}

		// If the item in the current slot has changed, null out the item and forget the slot
		if (pUpgradeState->uiSourceItemId && pUpgradeState->uiSourceItemId != SAFE_MEMBER(pUpgradeState->pSourceItem, id))
		{
			pUpgradeState->pSourceItem = NULL;
			pUpgradeState->SourceSlot.eBagID = InvBagIDs_None;
			pUpgradeState->SourceSlot.iIndex= -1;
		}
		if (pUpgradeState->uiModItemId && pUpgradeState->uiModItemId != SAFE_MEMBER(pUpgradeState->pModItem, id))
		{
			pUpgradeState->pModItem = NULL;
			pUpgradeState->ModSlot.eBagID = InvBagIDs_None;
			pUpgradeState->ModSlot.iIndex= -1;
		}

		pBaseDef = SAFE_GET_REF(pUpgradeState->pSourceItem, hItem);
		pModifier = SAFE_GET_REF(pUpgradeState->pModItem, hItem);
		pUpgradeState->iCount = pUpgradeState->iWillConsume = SAFE_MEMBER2(pSourceSlot, pItem, count);
		pResultDef = itemUpgrade_GetUpgrade((NOCONST(Entity)*)pEnt,pBaseDef, &pUpgradeState->iWillConsume, pModifier);
		pUpgradeState->fChance = itemUpgrade_GetChanceForItem(pEnt,pBaseDef,pModifier);
		if (pUpgradeState->pResultItem && pResultDef != pUpgradeState->pResultItemDef)
			StructDestroySafe(parse_Item, &pUpgradeState->pResultItem);
		if (!pUpgradeState->pResultItem && pResultDef)
			pUpgradeState->pResultItem = (Item*)inv_ItemInstanceFromDefName(pResultDef->pchName, entity_GetSavedExpLevelLimited(pEnt), 0, NULL, pEnt ? GET_REF(pEnt->hAllegiance) : NULL, pEnt ? GET_REF(pEnt->hSubAllegiance) : NULL, false, NULL);
		pUpgradeState->pResultItemDef = pResultDef;

		pUpgradeState->uiSourceItemId = SAFE_MEMBER(pUpgradeState->pSourceItem, id);
		pUpgradeState->uiModItemId = SAFE_MEMBER(pUpgradeState->pModItem, id);
		pUpgradeState->SourceSlotPrev = pUpgradeState->SourceSlot;
		pUpgradeState->ModSlotPrev = pUpgradeState->ModSlot;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_SetSourceItem);
void ItemUpgradeUI_SetSourceItem(SA_PARAM_OP_VALID UIGen* pGen, const char *pchKey)
{
	ItemUpgradeUIState *pUpgradeState = ItemUpgradeUI_GetItemUpgradeUIState();
	if (pUpgradeState)
	{
		EntityRef hEntHolder;
		if (!pchKey || !pchKey[0])
		{
			pUpgradeState->SourceSlot.eBagID = InvBagIDs_None;
			pUpgradeState->SourceSlot.iIndex = -1;
		}
		else
			sscanf(pchKey, "%d,%d,%d", &hEntHolder, &pUpgradeState->SourceSlot.eBagID, &pUpgradeState->SourceSlot.iIndex);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_SetModItem);
void ItemUpgradeUI_SetModItem(SA_PARAM_OP_VALID UIGen* pGen, const char *pchKey)
{
	ItemUpgradeUIState *pUpgradeState = ItemUpgradeUI_GetItemUpgradeUIState();
	if (pUpgradeState)
	{
		EntityRef hEntHolder;
		if (!pchKey || !pchKey[0])
		{
			pUpgradeState->ModSlot.eBagID = InvBagIDs_None;
			pUpgradeState->ModSlot.iIndex = -1;
		}
		else
			sscanf(pchKey, "%d,%d,%d", &hEntHolder, &pUpgradeState->ModSlot.eBagID, &pUpgradeState->ModSlot.iIndex);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_GetSuccessChance);
F32 ItemUpgradeUI_GetSuccessChance(SA_PARAM_OP_VALID UIGen* pGen)
{
	ItemUpgradeUIState *pUpgradeState = ItemUpgradeUI_GetItemUpgradeUIState();
	return SAFE_MEMBER(pUpgradeState, fChance);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_WillConsume);
int ItemUpgradeUI_WillConsume(SA_PARAM_OP_VALID UIGen* pGen)
{
	ItemUpgradeUIState *pUpgradeState = ItemUpgradeUI_GetItemUpgradeUIState();
	return SAFE_MEMBER(pUpgradeState, iWillConsume);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_UpgradeItem);
void ItemUpgradeUI_UpgradeItem(SA_PARAM_OP_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	ItemUpgradeUIState *pUpgradeState = ItemUpgradeUI_GetItemUpgradeUIState();
	if (pUpgradeState && pUpgradeState->pSourceItem && pEnt)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventorySlot *pSourceSlot = pEnt ? InvSlotRef_GetInventoySlot(pEnt, &pUpgradeState->SourceSlot, pExtract) : NULL;
		ServerCmd_entCmd_UpgradeItem(
			pUpgradeState->SourceSlot.eBagID, 
			pUpgradeState->SourceSlot.iIndex, 
			pUpgradeState->pSourceItem->id, 
			pUpgradeState->pSourceItem->count,
			pUpgradeState->ModSlot.eBagID,
			pUpgradeState->ModSlot.iIndex, 
			SAFE_MEMBER(pUpgradeState->pModItem, id));
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_CancelUpgradeJob);
void ItemUpgradeUI_CancelUpgradeJob(SA_PARAM_OP_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	ItemUpgradeUIState *pUpgradeState = ItemUpgradeUI_GetItemUpgradeUIState();
	if (pUpgradeState && pEnt)
	{
		ServerCmd_entCmd_CancelUpgradeJob();
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_GetUpgradeTime);
F32 ItemUpgradeUI_GetUpgradeTime(SA_PARAM_OP_VALID Entity *pEnt)
{
	return SAFE_MEMBER2(pEnt, pPlayer, ItemUpgradeInfo.fUpgradeTime);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_GetFullUpgradeTime);
F32 ItemUpgradeUI_GetUpgradeMaxTime(SA_PARAM_OP_VALID Entity *pEnt)
{
	return SAFE_MEMBER2(pEnt, pPlayer, ItemUpgradeInfo.fFullUpgradeTime);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_GetStackRemaining);
S32 ItemUpgradeUI_GetStackRemaining(SA_PARAM_OP_VALID Entity *pEnt)
{
	ItemUpgradeStack *pStack = SAFE_MEMBER2(pEnt, pPlayer, ItemUpgradeInfo.pCurrentStack);
	return pStack ? pStack->iStackRemaining : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_CheckLastResult);
S32 ItemUpgradeUI_GetLastResult(SA_PARAM_OP_VALID Entity *pEnt, int eCheckResult)
{
	return SAFE_MEMBER2(pEnt, pPlayer, ItemUpgradeInfo.eLastResult) == eCheckResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_UpgradeInProgress);
bool ItemUpgradeUI_UpgradeInProgress(SA_PARAM_OP_VALID Entity *pEnt)
{
	return SAFE_MEMBER3(pEnt, pPlayer, ItemUpgradeInfo.pCurrentStack, iStackRemaining) > 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_GetUpgradeRank);
const char* ItemUpgradeUI_GetUpgradeRank(SA_PARAM_OP_VALID Item* pItem)
{
	ItemDef *pItemDef = SAFE_GET_REF(pItem, hItem);
	static char achRank[2];
	achRank[0] = '\0';
	if (pItemDef && pItemDef->pchName && pItemDef->pchName[0])
	{
		const char* c;
		for (c = pItemDef->pchName; *c; c++)
		{
			if (*c >= '0' && *c <= '9')
				break;
		}
		achRank[0] = *c;
		return achRank;
	}
	else
	{
		return "";
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemUpgradeUI_CanUpgrade);
bool ItemUpgradeUI_CanUpgrade(SA_PARAM_OP_VALID Item* pItem)
{
	ItemDef *pItemDef = SAFE_GET_REF(pItem, hItem);

	if(pItemDef)
	{
		ItemUpgradeLadder *pLadder = NULL;
		int iCurrentRank = itemUpgrade_FindCurrentRank(pItemDef,&pLadder);

		if(iCurrentRank == -1 || !pLadder)
			return false;

		if(iCurrentRank >= eaSize(&pLadder->ppItems)-1)
			return false;

		return true;
	}

	return false;
}

AUTO_RUN;
void ItemUpgradeUI_Initialize(void)
{
	ui_GenInitStaticDefineVars(ItemUpgradeResultEnum, "ItemUpgradeResult");
}

#include "Autogen/FCItemUpgradeUI_c_ast.c"