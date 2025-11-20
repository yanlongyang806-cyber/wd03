/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "earray.h"
#include "Expression.h"
#include "UIGen.h"

#include "CharacterClass.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonGenerate.h"
#include "CostumeCommonTailor.h"
#include "Entity.h"
#include "EntityBuild.h"
#include "EntitySavedData.h"
#include "estring.h"
#include "GameAccountDataCommon.h"
#include "gclEntity.h"
#include "GraphicsLib.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "nemesis_common.h"
#include "qsortG.h"
#include "Character.h"

#include "AutoGen/EntityBuild_h_ast.h"
#include "AutoGen/entity_h_ast.h"
#include "AutoGen/BuildSelectionUI_c_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_STRUCT;
typedef struct UIBuildItem
{
	//The actual data about the item
	EntityBuildItem *pBuildItem;		AST(UNOWNED)

	// The build this item is a part of
	S32 uiIndex;

	//The index into the builds items that this one is
	S32 uiItemIdx;

} UIBuildItem;

// Get a list of currently available loot.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetBuildList);
void buildui_GetBuildList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	ui_GenSetListSafe(pGen, SAFE_MEMBER(pEnt, pSaved) ? &(EntityBuild**)pEnt->pSaved->ppBuilds : NULL, EntityBuild);
}

// Gets the Entity's current EntityBuild index. If no build is found returns -1
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetCurrentBuildIndex);
S32 buildui_GetCurrentBuildIndex(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (pEnt && pEnt->pSaved)
	{
		return pEnt->pSaved->uiIndexBuild;
	}
	return -1;
}

// Gets the Entity's EntityBuild at the given index.  May return NULL if then Entity doesn't have an EntityBuild at that index.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetBuildByIndex);
SA_RET_OP_VALID EntityBuild *buildui_GetBuildByIndex(SA_PARAM_OP_VALID Entity *pEnt, U32 uiIndex)
{
	EntityBuild *pBuild = NULL;
	if(pEnt)
	{
		pBuild = entity_BuildGet(pEnt, uiIndex);
	}

	return pBuild;
}

// Returns true if the Entity is currently allowed to create a new EntityBuild
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenBuildCanCreate);
bool buildui_BuildCanCreate(SA_PARAM_OP_VALID Entity *pEnt)
{
	bool bCanCreate = false;

	if(pEnt)
	{
		bCanCreate = !!entity_BuildCanCreate((ATH_ARG NOCONST(Entity)*)pEnt);
	}

	return bCanCreate;
}

// Returns true if the Entity is currently allowed to set its EntityBuild
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenBuildCanSet);
bool buildui_BuildCanSet(SA_PARAM_OP_VALID Entity *pEnt, U32 iBuild)
{
	bool bCanSet = false;
	if(pEnt)
		bCanSet = entity_BuildCanSet(pEnt, iBuild);

	return(bCanSet);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenBuildTimeToWait);
S32 buildui_BuildTimeToWait(SA_PARAM_OP_VALID Entity *pEnt, U32 iBuild)
{
	if(pEnt)
	{
		return entity_BuildTimeToWait(pEnt, iBuild);
	}
	return 0;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntBuildsCapacity);
S32 buildui_BuildsCapacity(SA_PARAM_OP_VALID Entity *pEnt)
{
	S32 iMaxSlots = 0;

	if(pEnt)
	{
		iMaxSlots = entity_BuildMaxSlots((ATH_ARG NOCONST(Entity)*)pEnt);
	}

	return iMaxSlots;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntBuildsSize);
S32 buildui_BuildsSize(SA_PARAM_OP_VALID Entity *pEnt)
{
	if(pEnt && pEnt->pSaved)
	{
		return eaSize(&pEnt->pSaved->ppBuilds);
	}
	else
		return(0);
}

// Returns  the name of the build
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenBuildGetDisplayName);
const char* buildui_BuildGetDisplayName(SA_PARAM_OP_VALID Entity *pEnt, U32 uiIndex)
{
	if(pEnt)
	{
		EntityBuild *pBuild = entity_BuildGet(pEnt, uiIndex);
		if (pBuild && pBuild->achName && pBuild->achName[0])
		{
			return pBuild->achName;
		}
	}
	return "";
}

// Returns the logical name of the role of the build
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenBuildGetRoleLogicalName);
const char* buildui_BuildGetRoleLogicalName(SA_PARAM_OP_VALID Entity *pEnt, U32 uiIndex)
{
	if(pEnt)
	{
		EntityBuild *pBuild = entity_BuildGet(pEnt, uiIndex);
		if (pBuild)
		{
			CharacterClass *pClass = GET_REF(pBuild->hClass);
			if (pClass)
			{
				return pClass->pchName;
			}
		}
	}
	return "";
}

// Returns the role of the build
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenBuildGetRole);
const char* buildui_BuildGetRole(SA_PARAM_OP_VALID Entity *pEnt, U32 uiIndex)
{
	if(pEnt)
	{
		EntityBuild *pBuild = entity_BuildGet(pEnt, uiIndex);
		if (pBuild)
		{
			CharacterClass *pClass = GET_REF(pBuild->hClass);
			if (pClass)
			{
				return TranslateDisplayMessage(pClass->msgDisplayName);
			}
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenBuildGetCostumeType);
U8 buildui_GenBuildGetCostumeType(SA_PARAM_OP_VALID Entity *pEnt, U32 uiIndex)
{
	EntityBuild *pBuild = NULL;

	if (pEnt)
		pBuild = entity_BuildGet(pEnt, uiIndex);

	if (!pEnt || !pBuild)
		return -1;

	return pBuild->chCostumeType;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenBuildGetCostumeSlot);
U8 buildui_GenBuildGetCostumeSlot(SA_PARAM_OP_VALID Entity *pEnt, U32 uiIndex)
{
	EntityBuild *pBuild = NULL;

	if (pEnt)
		pBuild = entity_BuildGet(pEnt, uiIndex);

	if (!pEnt || !pBuild)
		return -1;

	switch (pBuild->chCostumeType)
	{
	case kPCCostumeStorageType_Primary:
	case kPCCostumeStorageType_Secondary:
		return pBuild->chCostume;
	}
	return -1;
}

// DEPRECATED; Use UIGenPaperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenBuildGetHeadshot);
SA_RET_OP_VALID BasicTexture *buildui_GenBuildGetHeadshot(SA_PARAM_OP_VALID Entity *pEnt, U32 uiIndex, SA_PARAM_OP_VALID BasicTexture *pTexture, F32 fWidth, F32 fHeight)
{
	return NULL;
}

static int SortBuildItems(const UIBuildItem **a, const UIBuildItem **b)
{
	if((*a)->pBuildItem->eBagID < (*b)->pBuildItem->eBagID)
		return -1;
	if((*a)->pBuildItem->eBagID > (*b)->pBuildItem->eBagID)
		return 1;
	else if((*a)->pBuildItem->iSlot < (*b)->pBuildItem->iSlot)
		return -1;
	else if((*a)->pBuildItem->iSlot > (*b)->pBuildItem->iSlot)
		return 1;

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenBuildGetDevices);
void expr_GetBuildDevices(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, S32 iIndex)
{
	UIBuildItem ***peaItems = ui_GenGetManagedListSafe(pGen, UIBuildItem);
	S32 idx;
	S32 iUsed = 0;

	if(pEnt && pEnt->pSaved && pEnt->pSaved->ppBuilds && eaSize(&pEnt->pSaved->ppBuilds) > iIndex)
	{
		int *eaBagIds = NULL;
		EntityBuild *pBuild = eaGet(&pEnt->pSaved->ppBuilds, iIndex);

		int iDevicesBag = StaticDefineIntGetInt(InvBagIDsEnum,"Devices");

		if(pBuild)
		{
			for(idx=eaSize(&pBuild->ppItems)-1; idx>=0; idx--)
			{
				EntityBuildItem *pBuildItem = pBuild->ppItems[idx];

				if(iDevicesBag == pBuildItem->eBagID && pBuildItem->iSlot < 5)
				{
					UIBuildItem *pItem = eaGetStruct(peaItems, parse_UIBuildItem, iUsed++);
					pItem->pBuildItem = pBuildItem;
					pItem->uiItemIdx = idx;
					pItem->uiIndex = iIndex;
				}
			}
		}
	}

	while (eaSize(peaItems) > iUsed)
		StructDestroy(parse_UIBuildItem, eaPop(peaItems));

	eaQSortG(*peaItems,SortBuildItems);

	ui_GenSetManagedListSafe(pGen, peaItems, UIBuildItem, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenBuildClearEquipment);
void expr_GetBuildClearEquipment(SA_PARAM_NN_VALID UIGen *pGen)
{
	UIBuildItem ***peaItems = ui_GenGetManagedListSafe(pGen, UIBuildItem);
	eaClearStruct(peaItems, parse_UIBuildItem);
	ui_GenSetManagedListSafe(pGen, peaItems, UIBuildItem, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenBuildGetEquipmentFrom);
void expr_GetBuildEquipmentFrom(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, S32 iIndex, S32 bSecondary, SA_PARAM_OP_STR const char *pchBag)
{
	UIBuildItem ***peaItems = ui_GenGetManagedListSafe(pGen, UIBuildItem);
	int iBag = StaticDefineIntGetInt(InvBagIDsEnum,pchBag);
	S32 iUsed = 0;

	if(pEnt && pEnt->pSaved && pEnt->pSaved->ppBuilds && eaSize(&pEnt->pSaved->ppBuilds) > iIndex && iBag >= 0)
	{
		EntityBuild *pBuild = eaGet(&pEnt->pSaved->ppBuilds, iIndex);

		if(pBuild)
		{
			S32 idx;

			for(idx=eaSize(&pBuild->ppItems)-1; idx>=0; idx--)
			{
				EntityBuildItem *pBuildItem = pBuild->ppItems[idx];

				if(pBuildItem &&
					iBag == pBuildItem->eBagID &&
					((!bSecondary && pBuildItem->iSlot == 0) ||
						(bSecondary && pBuildItem->iSlot != 0)) )
				{
					UIBuildItem *pItem = eaGetStruct(peaItems, parse_UIBuildItem, iUsed++);
					pItem->pBuildItem = pBuildItem;
					pItem->uiItemIdx = idx;
					pItem->uiIndex = iIndex;
				}
			}
		}
	}

	while (eaSize(peaItems) > iUsed)
		StructDestroy(parse_UIBuildItem, eaPop(peaItems));

	eaQSortG(*peaItems,SortBuildItems);

	//Set the gen's array
	ui_GenSetManagedListSafe(pGen, peaItems, UIBuildItem, true);
}

bool buildui_MoveValid(SA_PARAM_OP_VALID Entity *pEnt, U32 uiIndex, SA_PARAM_OP_VALID Item *pItem, S32 iSrcBag, S32 iSrcSlot, S32 iDestBag, S32 iDestSlot, GameAccountDataExtract *pExtract)
{
	if(pEnt && pItem && !(inv_IsGuildBag(iSrcBag) || inv_IsGuildBag(iDestBag)))
	{
		ItemDef *pDef = GET_REF(pItem->hItem);
		if(pDef)
		{
			if(pEnt->pSaved->uiIndexBuild == uiIndex)
			{
				return item_ItemMoveValid(pEnt, pDef, false, iSrcBag, iSrcSlot, false, iDestBag, iDestSlot, pExtract);
			}
			else
			{
				return item_ItemMoveDestValid( pEnt, pDef, pItem, false, iDestBag, iDestSlot, true, pExtract );
			}
		}
	}

	return false;
}


bool buildui_SlotsMoveValid(SA_PARAM_OP_VALID Entity *pEnt, U32 uiIndex, S32 iSrcBag, S32 iSrcSlot, S32 iDestBag, S32 iDestSlot, GameAccountDataExtract *pExtract)
{
	Item *pItem = inv_GetItemFromBag(pEnt, iSrcBag, iSrcSlot, pExtract);
	return buildui_MoveValid(pEnt, uiIndex, pItem, iSrcBag, iSrcSlot, iDestBag, iDestSlot, pExtract);
}

// Check if this inventory move is valid.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(BuildUIItemMoveValid);
bool gclExprBuildItemMoveValid( SA_PARAM_OP_VALID Entity *pEnt, U32 uiIndex, const char *pchSrcBag, S32 iSrcSlot, S32 iDestBag, S32 iDestSlot)
{
	bool bResult = false;

	if(pEnt && pchSrcBag && pchSrcBag[0])
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		S32 iInvBag = StaticDefineIntGetInt(InvBagIDsEnum,pchSrcBag);
		bResult = buildui_SlotsMoveValid(pEnt, uiIndex, iInvBag, iSrcSlot, iDestBag, iDestSlot, pExtract);
	}
	return bResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(BuildUIInvSlotMoveValid);
bool gclExprBuildInvSlotMoveValid(const char *pchKeyA, U32 uiIndex, S32 iDestBag, S32 iDestSlot)
{
	EntityRef hEntA;
	InvBagIDs eBagA;
	S32 iSlotA;
	Entity *pEntA;
	bool bResult = false;

	if (pchKeyA
		&& sscanf(pchKeyA, "%d,%d,%d", &hEntA, &eBagA, &iSlotA) == 3
		&& (pEntA = entFromEntityRefAnyPartition(hEntA)) &&
		pEntA == entActivePlayerPtr())
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntA);
		bResult = buildui_SlotsMoveValid(pEntA, uiIndex, eBagA, iSlotA, iDestBag, iDestSlot, pExtract);
	}
	return bResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntBuildItemCatchItem");
void gclBuildExp_BuildItemCatchItem(U32 iBuildIdx,
									SA_PARAM_OP_VALID UIBuildItem *pUIBuildItem,
									SA_PARAM_OP_STR const char *pchBagName,
									S32 iSlot)
{
	Entity *pEnt = entActivePlayerPtr();
	if(pUIBuildItem && pchBagName && pchBagName[0])
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		S32 iInvBag = StaticDefineIntGetInt(InvBagIDsEnum,pchBagName);
		Item *pItem = inv_GetItemFromBag(pEnt, iInvBag, iSlot, pExtract);
		if(pItem && buildui_MoveValid(pEnt, iBuildIdx, pItem, iInvBag, iSlot, pUIBuildItem->pBuildItem->eBagID, pUIBuildItem->pBuildItem->iSlot, pExtract))
		{
			ServerCmd_buildSetItem(iBuildIdx, pUIBuildItem->pBuildItem->eBagID, pUIBuildItem->pBuildItem->iSlot, pItem->id, iInvBag, iSlot);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntBuildItemCatchInventorySlot");
void gclBuildExp_BuildItemCatchInventorySlot(	U32 iBuildIdx,
												SA_PARAM_OP_VALID UIBuildItem *pUIBuildItem,
												const char *pchKeyA)
{
	EntityRef hEntA;
	InvBagIDs eBagA;
	S32 iSlotA;
	Entity *pEntA;
	if (pchKeyA
		&& sscanf(pchKeyA, "%d,%d,%d", &hEntA, &eBagA, &iSlotA) == 3
		&& (pEntA = entFromEntityRefAnyPartition(hEntA)) &&
		pEntA == entActivePlayerPtr())
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntA);
		Item *pItem = inv_GetItemFromBag(pEntA, eBagA, iSlotA, pExtract);
		if(pUIBuildItem && pItem &&
			buildui_MoveValid(	pEntA, iBuildIdx, pItem,
									eBagA, iSlotA, pUIBuildItem->pBuildItem->eBagID,
									pUIBuildItem->pBuildItem->iSlot, pExtract))
		{
			ServerCmd_buildSetItem(iBuildIdx, pUIBuildItem->pBuildItem->eBagID, pUIBuildItem->pBuildItem->iSlot, pItem->id, eBagA, iSlotA);
		}
	}
}

#include "AutoGen/BuildSelectionUI_c_ast.c"
