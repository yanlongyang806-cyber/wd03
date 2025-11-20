#include "earray.h"
#include "UIGen.h"
#include "entity.h"
#include "Player.h"
#include "itemCommon.h"
#include "itemEnums.h"
#include "FCInventoryUI.h"
#include "GameAccountDataCommon.h"
#include "SuperCritterPet.h"
#include "gclSuperCritterPet.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "Autogen/FCGemSlotUI_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_STRUCT;
typedef struct GemSlotUIGemSlot
{
	// A temporary item generated for this structure to hold
	Item *pCommittedItem;

	// If this and pCommittedItem don't match, that means the slot was just updated, and
	// pInventoryItem and Invshould be cleared
	void *pvCommittedItemPrev; NO_AST

	// An item from your inventory which is not owned by this structure
	Item *pInventoryItem; AST(UNOWNED) 
	ItemGemType eAllowedTypes;	AST( NAME(AllowedTypes) FLAGS)
	InventorySlotReference InvSlot;
	bool bLocked;
} GemSlotUIGemSlot;

AUTO_STRUCT;
typedef struct GemSlotUIState
{
	InventorySlotReference CurrentSlot;
	Item *pCurrentItem;	AST(UNOWNED)
	void *pvCurrentItemPrev; NO_AST
	GemSlotUIGemSlot **eaGemSlots;
} GemSlotUIState;

GemSlotUIState* GemSlotUI_GetGemSlotUIState(SA_PARAM_OP_VALID UIGen* pGen)
{
	GemSlotUIState *pGemState = NULL;
	while (pGen && !pGemState)
	{
		pGemState = ui_GenGetPointer(pGen, parse_GemSlotUIState, NULL);
		pGen = pGen->pParent;
	}
	return pGemState;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_GetItemInvSlotRef);
SA_RET_OP_VALID InventorySlotReference* GemSlotUI_GetItemInvSlotRef(SA_PARAM_OP_VALID UIGen* pGen)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	return pGemState ? &pGemState->CurrentSlot : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_GetItemInventorySlot);
SA_RET_OP_VALID InventorySlot* GemSlotUI_GetItemInventorySlot(SA_PARAM_OP_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	return (pGemState && pEnt && pExtract) ? InvSlotRef_GetInventoySlot(pEnt, &pGemState->CurrentSlot, pExtract) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_GetItem);
SA_RET_OP_VALID Item* GemSlotUI_GetItem(SA_PARAM_OP_VALID UIGen* pGen)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	return SAFE_MEMBER(pGemState, pCurrentItem);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_Init);
void GemSlotUI_Init(SA_PARAM_OP_VALID UIGen* pGen)
{
	if (pGen)
	{
		ui_GenSetManagedPointer(
			pGen,
			StructCreate(parse_GemSlotUIState), 
			parse_GemSlotUIState, 
			true);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_Update);
void GemSlotUI_Update(SA_PARAM_OP_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	if (pGemState)
	{
		if (pEnt)
		{
			int i;
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			Item *pCurrentItem = NULL;

			if (pGemState->CurrentSlot.iIndex == -1 && pGemState->CurrentSlot.eBagID == InvBagIDs_SuperCritterPets)
			{
				//this means we should trust the current item for pet inspect.
				pCurrentItem = pGemState->pCurrentItem;
			}
			else
			{
				InventorySlot *pHolderInventorySlot = pEnt ? InvSlotRef_GetInventoySlot(pEnt, &pGemState->CurrentSlot, pExtract) : NULL;
				pCurrentItem = pGemState->pCurrentItem = SAFE_MEMBER(pHolderInventorySlot, pItem);
			}

			
			if (pCurrentItem)
			{
				ItemDef *pCurrentItemDef = GET_REF(pCurrentItem->hItem);
				int iSize;
				if (pGemState->pvCurrentItemPrev != pGemState->pCurrentItem)
					eaClearStruct(&pGemState->eaGemSlots, parse_GemSlotUIGemSlot);

				iSize = pCurrentItemDef ? eaSize(&pCurrentItemDef->ppItemGemSlots) : 0;
				eaSetSizeStruct(&pGemState->eaGemSlots, parse_GemSlotUIGemSlot, iSize);
				for (i = 0; i < iSize; i++)
				{
					GemSlotUIGemSlot *pGemSlot = pGemState->eaGemSlots[i];
					ItemDef *pCommittedItemDef = SAFE_GET_REF(pGemSlot->pCommittedItem, hItem);
					ItemDef *pSlottedDef = item_GetSlottedGemItemDef(pCurrentItem, i);

					// The the item has changed or been removed, delete the temp item
					if (pGemSlot->pCommittedItem && pSlottedDef != pCommittedItemDef)
					{
						StructDestroySafe(parse_Item, &pGemSlot->pCommittedItem);
					}
					// If there is a gem itemdef in the slot but no item, generate one
					if (!pGemSlot->pCommittedItem && pSlottedDef)
					{
						pGemSlot->pCommittedItem = (Item*)inv_ItemInstanceFromDefName(pSlottedDef->pchName, entity_GetSavedExpLevelLimited(pEnt), 0, NULL, pEnt ? GET_REF(pEnt->hAllegiance) : NULL, pEnt ? GET_REF(pEnt->hSubAllegiance) : NULL,false, NULL);
					}

					if (pCurrentItem->pSpecialProps && pCurrentItem->pSpecialProps->pSuperCritterPet)
					{
						pGemSlot->bLocked = scp_IsGemSlotLockedOnPet(pCurrentItem->pSpecialProps->pSuperCritterPet, i);
					}
					else
						pGemSlot->bLocked = false;

					// If there is no item, but there is a invslotref, point to that item
					if (pGemSlot->InvSlot.eBagID == InvBagIDs_None)
					{
						pGemSlot->pInventoryItem = NULL;
					}
					else
					{
						InventorySlot *pGemInventorySlot = InvSlotRef_GetInventoySlot(pEnt, &pGemSlot->InvSlot, pExtract);
						pGemSlot->pInventoryItem = SAFE_MEMBER(pGemInventorySlot, pItem);
						if (!pGemSlot->pInventoryItem)
						{
							pGemSlot->pInventoryItem = NULL;
							pGemSlot->InvSlot.eBagID = InvBagIDs_None;
						}
					}
					pGemSlot->eAllowedTypes = pCurrentItemDef->ppItemGemSlots[i]->eType;
				}
			}
			else
			{
				pGemState->CurrentSlot.eBagID = InvBagIDs_None;
				pGemState->CurrentSlot.iIndex = -1;
				eaClearStruct(&pGemState->eaGemSlots, parse_GemSlotUIGemSlot);
			}
			pGemState->pvCurrentItemPrev = pGemState->pCurrentItem;
		}
		else
		{
			ui_GenClearPointer(pGen);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_GetGemList);
void GemSlotUI_GetGemList(SA_PARAM_OP_VALID UIGen* pGen)
{
	if (pGen)
	{
		GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
		Item ***peaItems = ui_GenGetManagedListSafe(pGen, Item);
		int i, iSize = pGemState ? eaSize(&pGemState->eaGemSlots) : 0;
		eaSetSize(peaItems, iSize);
		for (i = 0; i < iSize; i++)
		{
			GemSlotUIGemSlot *pGemSlot = pGemState->eaGemSlots[i];
			(*peaItems)[i] = pGemSlot->pInventoryItem ? pGemSlot->pInventoryItem : pGemSlot->pCommittedItem;
		}
		ui_GenSetManagedListSafe(pGen, peaItems, Item, false);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_GetGemSlotList);
void GemSlotUI_GetGemSlotList(SA_PARAM_OP_VALID UIGen* pGen)
{
	if (pGen)
	{
		GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
		if (pGemState)
		{
			GemSlotUIGemSlot ***peaSlots = ui_GenGetManagedListSafe(pGen, GemSlotUIGemSlot);
			eaCopy(peaSlots, &pGemState->eaGemSlots);
			ui_GenSetManagedListSafe(pGen, peaSlots, GemSlotUIGemSlot, false);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_AttachGemToItem);
void GemSlotUI_AttachGemToItem(SA_PARAM_OP_VALID UIGen* pGen, const char *pchGemKey, int iGemSlotIndex)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	EntityRef hEntGem;
	InvBagIDs eBagGem;
	S32 iSlotGem;
	if (pGemState && pchGemKey && sscanf(pchGemKey, "%d,%d,%d", &hEntGem, &eBagGem, &iSlotGem) == 3)
	{
		GemSlotUIGemSlot *pGemSlot = eaGet(&pGemState->eaGemSlots, iGemSlotIndex);
		if (pGemSlot)
		{
			pGemSlot->InvSlot.eBagID = eBagGem;
			pGemSlot->InvSlot.iIndex = iSlotGem;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_AttachGemToItemAnywhere);
void GemSlotUI_AttachGemToItemAnywhere(SA_PARAM_OP_VALID UIGen* pGen, const char *pchGemKey)
{
	UIInventoryKey Key = {0};
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	if (pGen)
	{
		int i, n = eaSize(&pGemState->eaGemSlots);
		for (i = 0; i < n; i++)
		{
			if (gclInventoryParseKey(pchGemKey, &Key) && Key.pSlot && inv_CanGemSlot(pGemState->pCurrentItem, Key.pSlot->pItem, i))
			{
				GemSlotUI_AttachGemToItem(pGen, pchGemKey, i);
				return;
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_RevertChanges);
void GemSlotUI_RevertChanges(SA_PARAM_OP_VALID UIGen* pGen)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	if (pGen)
	{
		int i;
		for (i = 0; i < eaSize(&pGemState->eaGemSlots); i++)
		{
			GemSlotUIGemSlot *pGemSlot = pGemState->eaGemSlots[i];
			pGemSlot->InvSlot.eBagID = InvBagIDs_None;
			pGemSlot->InvSlot.iIndex = -1;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_CommitChanges);
void GemSlotUI_CommitChanges(SA_PARAM_OP_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEnt)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	if(pGen && pEnt && pGemState)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		int i;
		for (i = 0; i < eaSize(&pGemState->eaGemSlots); i++)
		{
			GemSlotUIGemSlot *pGemSlot = pGemState->eaGemSlots[i];
			if (pGemSlot->InvSlot.eBagID != InvBagIDs_None)
			{
				Item* pGem = inv_GetItemFromBag(pEnt, pGemSlot->InvSlot.eBagID, pGemSlot->InvSlot.iIndex, pExtract);
				if (pGemState->pCurrentItem && pGem)
				{
					ServerCmd_entCmd_GemItem(
						pGemSlot->InvSlot.eBagID, pGemSlot->InvSlot.iIndex, pGem->id, 
						pGemState->CurrentSlot.eBagID, pGemState->CurrentSlot.iIndex, pGemState->pCurrentItem->id, i);
				}
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_CommitSlotChanges);
void GemSlotUI_CommitSlotChanges(SA_PARAM_OP_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEnt, int iSlot)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	if(pGen && pEnt && pGemState && iSlot >= 0 && iSlot < eaSize(&pGemState->eaGemSlots))
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		GemSlotUIGemSlot *pGemSlot = pGemState->eaGemSlots[iSlot];
		if (pGemSlot->InvSlot.eBagID != InvBagIDs_None)
		{
			Item* pGem = inv_GetItemFromBag(pEnt, pGemSlot->InvSlot.eBagID, pGemSlot->InvSlot.iIndex, pExtract);
			if (pGemState->pCurrentItem && pGem)
			{
				ServerCmd_entCmd_GemItem(
					pGemSlot->InvSlot.eBagID, pGemSlot->InvSlot.iIndex, pGem->id, 
					pGemState->CurrentSlot.eBagID, pGemState->CurrentSlot.iIndex, pGemState->pCurrentItem->id, iSlot);
				// kinda ugly here - pGemSlot->InvSlot.eBagID should be clear IF the transaction succeeds.  Transaction callback -> notify -> gen response is too ugly
				// so (on NW at least) instead the Gens watch for changes.
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_RemoveGemFromItem);
void GemSlotUI_RemoveGemFromItem(SA_PARAM_OP_VALID UIGen* pGen,  const char *pchTargetKey, int iGemSlotIndex, const char *pchCurrency)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	EntityRef hEntTarget;
	InvBagIDs eBagTarget;
	S32 iSlotTarget;
	if (sscanf(pchTargetKey, "%d,%d,%d", &hEntTarget, &eBagTarget, &iSlotTarget) != 3)
	{
		eBagTarget = InvBagIDs_Inventory;
		iSlotTarget = -1;
	}
	if (pGemState)
	{
		GemSlotUIGemSlot *pGemSlot = eaGet(&pGemState->eaGemSlots, iGemSlotIndex);
		if (pGemSlot->InvSlot.eBagID != InvBagIDs_None)
		{
			//we are still remembering the inventory slot that owned the gem, so clear that.
			pGemSlot->InvSlot.eBagID = InvBagIDs_None;
			pGemSlot->InvSlot.iIndex = -1;
		}
		if(pGemSlot->pCommittedItem)
		{
			ServerCmd_ItemUngemItem(pGemState->CurrentSlot.eBagID, pGemState->CurrentSlot.iIndex, iGemSlotIndex, eBagTarget, iSlotTarget, pchCurrency);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_GemRemovalCost);
S32 GemSlotUI_GemRemovalCost(SA_PARAM_OP_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEnt, int iGemSlotIndex, const char *pchCurrency)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	return pGemState ? itemGem_GetUnslotCostFromGemIdx(pchCurrency, pEnt, pGemState->pCurrentItem, iGemSlotIndex) : -1;
}

// split from GemSlotUI_GemRemovalCost so UI can predict cost before committing the gem.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_GemRemovalCostFromDef);
S32 GemSlotUI_GemRemovalCostFromDef(SA_PARAM_OP_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_OP_VALID ItemDef* pItemDef, const char *pchCurrency)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	S32 ret = pGemState ? itemGem_GetUnslotCostFromGemDef(pchCurrency, pEnt, pGemState->pCurrentItem, pItemDef) : -1;
	return ret;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_SetItem);
void GemSlotUI_SetItem(SA_PARAM_OP_VALID UIGen* pGen, const char *pchKey)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	if (pGemState)
	{
		EntityRef hEntHolder;

		if (!pchKey || !pchKey[0])
		{
			pGemState->CurrentSlot.eBagID = InvBagIDs_None;
			pGemState->CurrentSlot.iIndex = -1;
			return;
		}
		sscanf(pchKey, "%d,%d,%d", &hEntHolder, &pGemState->CurrentSlot.eBagID, &pGemState->CurrentSlot.iIndex);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_GetNumberOfGemSlots);
int GemSlotUI_GetNumberOfGemSlots(SA_PARAM_OP_VALID Item* pHolder)
{
	ItemDef *pItemDef = pHolder ? GET_REF(pHolder->hItem) : NULL;
	return pItemDef ? eaSize(&pItemDef->ppItemGemSlots) : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_CanPlaceGemAnywhere);
bool GemSlotUI_CanPlaceGemAnywhere(SA_PARAM_OP_VALID Item* pHolder, SA_PARAM_OP_VALID Item* pGem)
{
	if (pHolder && pGem)
	{
		ItemDef *pHolderDef = GET_REF(pHolder->hItem);
		int i, n = pHolderDef ? eaSize(&pHolderDef->ppItemGemSlots) : 0;
		for (i = 0; i < n; i++)
		{
			if (inv_CanGemSlot(pHolder, pGem, i))
			{
				return true;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_CanPlaceGemInSlot);
bool GemSlotUI_CanPlaceGemInSlot(SA_PARAM_OP_VALID Item* pHolder, SA_PARAM_OP_VALID Item* pGem, int iDestGemSlot)
{
	return inv_CanGemSlot(pHolder, pGem, iDestGemSlot);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_GetGemSlotType);
int GemSlotUI_GetGemSlotType(SA_PARAM_OP_VALID Item* pHolder, int iGemSlot)
{
	if (pHolder)
	{
		ItemDef *pItemDef = SAFE_GET_REF(pHolder, hItem);
		ItemGemSlotDef *pGemSlotDef = pItemDef ? eaGet(&pItemDef->ppItemGemSlots, iGemSlot) : NULL;
		return SAFE_MEMBER(pGemSlotDef, eType);
	}
	return kItemGemType_None;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_IsItemAttached);
int GemSlotUI_IsItemAttached(SA_PARAM_OP_VALID UIGen* pGen, int iGemSlot)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	if (pGemState)
	{
		GemSlotUIGemSlot *pGemSlot = eaGet(&pGemState->eaGemSlots, iGemSlot);
		return !!pGemSlot->pCommittedItem;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_CheckForChanges);
int GemSlotUI_CheckForChanges(SA_PARAM_OP_VALID UIGen* pGen)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	if (pGemState)
	{
		int i;
		for (i = 0; i < eaSize(&pGemState->eaGemSlots); i++)
		{
			GemSlotUIGemSlot *pGemSlot = eaGet(&pGemState->eaGemSlots, i);
			if (pGemSlot->pInventoryItem && (!pGemSlot->pCommittedItem || !REF_COMPARE_HANDLES(pGemSlot->pInventoryItem->hItem, pGemSlot->pCommittedItem->hItem)))
				return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_CheckSlotForChanges);
int GemSlotUI_CheckSlotForChanges(SA_PARAM_OP_VALID UIGen* pGen, int iSlotIndex)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	if (pGemState)
	{
		if( iSlotIndex >= 0 && iSlotIndex < eaSize(&pGemState->eaGemSlots) )
		{
			GemSlotUIGemSlot *pGemSlot = eaGet(&pGemState->eaGemSlots, iSlotIndex);
 			if (pGemSlot->pInventoryItem && (!pGemSlot->pCommittedItem || !REF_COMPARE_HANDLES(pGemSlot->pInventoryItem->hItem, pGemSlot->pCommittedItem->hItem)))
				return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GemSlotUI_ItemInUse);
int GemSlotUI_ItemInUse(SA_PARAM_OP_VALID UIGen* pGen, SA_PARAM_OP_VALID Item* pItem)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	if (pGemState && pItem)
	{
		int i;
		if (pGemState->pCurrentItem == pItem)
			return true;

		for (i = 0; i < eaSize(&pGemState->eaGemSlots); i++)
		{
			GemSlotUIGemSlot *pGemSlot = eaGet(&pGemState->eaGemSlots, i);
			if (pGemSlot->pInventoryItem == pItem)
				return true;
		}
	}
	return false;
}

AUTO_STARTUP(ItemGems) ASTRT_DEPS(Items);
void GemSlotUI_Initialize(void)
{
	ui_GenInitStaticDefineVars(ItemGemTypeEnum, "GemType");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_SetGemUIToPet);
void scp_ExprSetGemUIToPet(SA_PARAM_NN_VALID UIGen *pGen, int idx)
{
	GemSlotUIState *pGemState = GemSlotUI_GetGemSlotUIState(pGen);
	if (pGemState)
	{
		if (idx == -1)
		{
			//pet inspect fake pet - use this item.
			pGemState->CurrentSlot.eBagID = InvBagIDs_SuperCritterPets;
			pGemState->CurrentSlot.iIndex = -1;
			pGemState->pCurrentItem = scp_GetInspectItem();
		}
		else
		{
			//player status pet - use the pet in this bag/slot.
			pGemState->CurrentSlot.eBagID = InvBagIDs_SuperCritterPets;
			pGemState->CurrentSlot.iIndex = idx;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GemCommit);
void scp_ExprGemCommit(SA_PARAM_OP_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEnt, int iSlot)
{
	GemSlotUI_CommitSlotChanges(pGen, pEnt, iSlot);
	ServerCmd_scp_resetSummonedPetInventory();
}

#include "Autogen/FCGemSlotUI_c_ast.c"