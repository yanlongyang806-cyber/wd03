/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "Expression.h"
#include "GameAccountDataCommon.h"
#include "GameEvent.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "inventoryCommon.h"
#include "mission_common.h"
#include "Player.h"
#include "stringcache.h"
#include "gslEventTracker.h"
#include "PowerTreeHelpers.h"


// ----------------------------------------------------------------------------------
// Inventory Expression Functions
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC_STATIC_CHECK;
int player_FuncItemCountLoadVerify(ExprContext *pContext, const char *pcItemName)
{
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pMissionDef) {
		GameEvent *pItemGainedEvent, *pItemLostEvent;
		char *estrBuffer = NULL;
		
		pItemGainedEvent = StructCreate(parse_GameEvent);
		estrPrintf(&estrBuffer, "ItemGained_%s", pcItemName);
		pItemGainedEvent->pchEventName = allocAddString(estrBuffer);
		pItemGainedEvent->type = EventType_ItemGained;
		pItemGainedEvent->pchItemName = allocAddString(pcItemName);
		pItemGainedEvent->tMatchSource = TriState_Yes;
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pItemGainedEvent, pMissionDef->filename);

		pItemLostEvent = StructCreate(parse_GameEvent);
		estrPrintf(&estrBuffer, "ItemLost_%s", pcItemName);
		pItemLostEvent->pchEventName = allocAddString(estrBuffer);
		pItemLostEvent->type = EventType_ItemLost;
		pItemLostEvent->pchItemName = allocAddString(pcItemName);
		pItemLostEvent->tMatchSource = TriState_Yes;
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pItemLostEvent, pMissionDef->filename);

		estrDestroy(&estrBuffer);
	}
	return 0;
}


AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerItemCount) ACMD_EXPR_STATIC_CHECK(player_FuncItemCountLoadVerify);
int player_FuncItemCount(ExprContext *pContext, ACMD_EXPR_RES_DICT(ItemDef) const char *pcItemName)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	ItemDef *pDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict,pcItemName);

	if (!pPlayerEnt && pMission && pMission->infoOwner)
	{
		pPlayerEnt = pMission->infoOwner->parentEnt;
	}

	if (!pPlayerEnt)
	{
		pPlayerEnt = exprContextGetVarPointerUnsafe(pContext, "targetEnt");
	}

	if (!pPlayerEnt || !pDef)
	{
		return 0;
	}

	if (pDef->eType == kItemType_Numeric)
	{
		return inv_GetNumericItemValue(pPlayerEnt, pcItemName);
	}

	return item_CountOwned(pPlayerEnt, pDef);
}

AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncPlayerGetPointsSpentLoadVerify(ExprContext *pContext, const char *pcItemName)
{
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pMissionDef)
	{
		GameEvent *pPowerTreeStepsAddedEvent;

		pPowerTreeStepsAddedEvent = StructCreate(parse_GameEvent);
		pPowerTreeStepsAddedEvent->pchEventName = allocAddString("PowerTreePointsSpent");
		pPowerTreeStepsAddedEvent->type = EventType_PowerTreeStepAdded;
		pPowerTreeStepsAddedEvent->tMatchSource = TriState_Yes;
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pPowerTreeStepsAddedEvent, pMissionDef->filename);
	}
	return 0;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerGetPointsSpent) ACMD_EXPR_STATIC_CHECK(mission_FuncPlayerGetPointsSpentLoadVerify);
int mission_FuncPlayerGetPointsSpent(ExprContext *pContext, ACMD_EXPR_RES_DICT(ItemDef) const char *pcItemName)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	ItemDef *pDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict,pcItemName);

	if (!pPlayerEnt && pMission && pMission->infoOwner)
	{
		pPlayerEnt = pMission->infoOwner->parentEnt;
	}

	if (!pPlayerEnt)
	{
		pPlayerEnt = exprContextGetVarPointerUnsafe(pContext, "targetEnt");
	}

	if (!pPlayerEnt || !pDef)
	{
		return 0;
	}

	if (pDef->eType == kItemType_Numeric)
	{
		return entity_PointsSpentTryNumeric(NULL, CONTAINER_NOCONST(Entity, pPlayerEnt), pcItemName);
	}

	return 0;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
bool mission_FuncPlayerHasItemWithGemSlotLoadVerify(ExprContext *pContext)
{
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pMissionDef)
	{
		GameEvent *pItemGainedEvent, *pItemLostEvent;

		pItemGainedEvent = StructCreate(parse_GameEvent);
		pItemGainedEvent->pchEventName = allocAddString("GemSlotItemGained");
		pItemGainedEvent->type = EventType_ItemGained;
		pItemGainedEvent->tMatchSource = TriState_Yes;
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pItemGainedEvent, pMissionDef->filename);

		pItemLostEvent = StructCreate(parse_GameEvent);
		pItemLostEvent->pchEventName = allocAddString("GemSlotItemLost");
		pItemLostEvent->type = EventType_ItemLost;
		pItemLostEvent->tMatchSource = TriState_Yes;
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pItemLostEvent, pMissionDef->filename);
	}
	return false;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerHasItemWithGemSlot) ACMD_EXPR_STATIC_CHECK(mission_FuncPlayerHasItemWithGemSlotLoadVerify);
bool mission_FuncPlayerHasItemWithGemSlot(ExprContext *pContext)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	GameAccountDataExtract *pExtract;
	int iBagCount = 0;
	int i;

	if (!pPlayerEnt && pMission && pMission->infoOwner)
	{
		pPlayerEnt = pMission->infoOwner->parentEnt;
	}

	if (!pPlayerEnt)
	{
		pPlayerEnt = exprContextGetVarPointerUnsafe(pContext, "targetEnt");
	}

	if (!pPlayerEnt)
	{
		return false;
	}

	pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
	iBagCount = eaSize(&pPlayerEnt->pInventoryV2->ppInventoryBags);
	for (i = 0; i < iBagCount; i++)
	{
		InventoryBag *pBag = pPlayerEnt->pInventoryV2->ppInventoryBags[i];
		if (pBag)
		{
			BagIterator *pIter = invbag_IteratorFromEnt(pPlayerEnt, pBag->BagID, pExtract);
			for (; !bagiterator_Stopped(pIter); bagiterator_Next(pIter))
			{
				ItemDef *pItemDef = bagiterator_GetDef(pIter);
				if (pItemDef && eaSize(&pItemDef->ppItemGemSlots) > 0)
				{
					bagiterator_Destroy(pIter);
					return true;
				}
			}
			bagiterator_Destroy(pIter);
		}
	}

	return false;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
bool mission_FuncPlayerSlottedGemLoadVerify(ExprContext *pContext)
{
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pMissionDef)
	{
		GameEvent *pGemSlottedEvent;

		pGemSlottedEvent = StructCreate(parse_GameEvent);
		pGemSlottedEvent->pchEventName = allocAddString("SlottedGem");
		pGemSlottedEvent->type = EventType_GemSlotted;
		pGemSlottedEvent->tMatchSource = TriState_Yes;
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pGemSlottedEvent, pMissionDef->filename);
	}
	return false;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerSlottedGem) ACMD_EXPR_STATIC_CHECK(mission_FuncPlayerSlottedGemLoadVerify);
bool mission_FuncPlayerSlottedGem(ExprContext *pContext)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	GameAccountDataExtract *pExtract;
	int iBagCount = 0;
	int i;

	if (!pPlayerEnt && pMission && pMission->infoOwner)
	{
		pPlayerEnt = pMission->infoOwner->parentEnt;
	}

	if (!pPlayerEnt)
	{
		pPlayerEnt = exprContextGetVarPointerUnsafe(pContext, "targetEnt");
	}

	if (!pPlayerEnt)
	{
		return false;
	}

	pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
	iBagCount = eaSize(&pPlayerEnt->pInventoryV2->ppInventoryBags);
	for (i = 0; i < iBagCount; i++)
	{
		InventoryBag *pBag = pPlayerEnt->pInventoryV2->ppInventoryBags[i];
		if (pBag)
		{
			BagIterator *pIter = invbag_IteratorFromEnt(pPlayerEnt, pBag->BagID, pExtract);
			for (; !bagiterator_Stopped(pIter); bagiterator_Next(pIter))
			{
				Item *pItem = (Item *)bagiterator_GetItem(pIter);
				if (pItem && pItem->pSpecialProps)
				{
					int j;
					for (j = 0; j < eaSize(&pItem->pSpecialProps->ppItemGemSlots); j++)
					{
						if (IS_HANDLE_ACTIVE(pItem->pSpecialProps->ppItemGemSlots[j]->hSlottedItem))
						{
							bagiterator_Destroy(pIter);
							return true;
						}
					}
				} 
			}
			bagiterator_Destroy(pIter);
		}
	}

	return false;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncPlayerHasNemesisGrantItem_SC(ExprContext *pContext)
{
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pMissionDef) {
		GameEvent *pItemGainedEvent, *pItemLostEvent;
		
		pItemGainedEvent = StructCreate(parse_GameEvent);
		pItemGainedEvent->pchEventName = allocAddString("NemesisItemGained");
		pItemGainedEvent->type = EventType_ItemGained;
		pItemGainedEvent->tMatchSource = TriState_Yes;
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pItemGainedEvent, pMissionDef->filename);

		pItemLostEvent = StructCreate(parse_GameEvent);
		pItemLostEvent->pchEventName = allocAddString("NemesisItemLost");
		pItemLostEvent->type = EventType_ItemLost;
		pItemLostEvent->tMatchSource = TriState_Yes;
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pItemLostEvent, pMissionDef->filename);
	}
	return 0;
}


AUTO_EXPR_FUNC(player, mission)  ACMD_NAME(PlayerHasNemesisGrantItem) ACMD_EXPR_STATIC_CHECK(mission_FuncPlayerHasNemesisGrantItem_SC);
int mission_FuncPlayerHasNemesisGrantItem(ExprContext *pContext)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	ItemDef *pDef = NULL;
	bool bFoundNemesisGrant = false;

	if (!pPlayerEnt && pMission && pMission->infoOwner) {
		pPlayerEnt = pMission->infoOwner->parentEnt;
	}

	if (!pPlayerEnt) {
		pPlayerEnt = exprContextGetVarPointerUnsafe(pContext, "targetEnt");
	}

	if (!pPlayerEnt) {
		return 0;
	} else {
		int iBag;
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
		for (iBag = eaSize(&pPlayerEnt->pInventoryV2->ppInventoryBags)-1; iBag >=0; iBag--) {
			InventoryBag *bag = pPlayerEnt->pInventoryV2->ppInventoryBags[iBag];
			BagIterator *iter;
			bool found = false;
			
			if (!bag) {
				continue;
			}
			
			iter = invbag_IteratorFromEnt(pPlayerEnt,bag->BagID,pExtract);
			for(; !bagiterator_Stopped(iter); bagiterator_Next(iter)) {
				ItemDef *itemdef = bagiterator_GetDef(iter);
				MissionDef *mish_def = itemdef?GET_REF(itemdef->hMission):NULL;
				if (!mish_def) {
					break;
				}
				
				if (itemdef && mish_def && itemdef->eType == kItemType_MissionGrant && mish_def->missionType == MissionType_Nemesis) {
					found = true;
					break;
				}
			}
			bagiterator_Destroy(iter);
		}
	}
	return bFoundNemesisGrant;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int player_CountBagItemsLoadVerify(ExprContext *pContext, ACMD_EXPR_ENUM(InvBagIDs) const char *pcBagType)
{
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pMissionDef) {
		GameEvent *pBagGetsItemEvent = StructCreate(parse_GameEvent);
		char *estrBuffer = NULL;

		estrPrintf(&estrBuffer, "BagGetsItem_%s", pcBagType);
		pBagGetsItemEvent->pchEventName = allocAddString(estrBuffer);
		pBagGetsItemEvent->type = EventType_BagGetsItem;
		pBagGetsItemEvent->bagType = StaticDefineIntGetInt(InvBagIDsEnum, pcBagType);
		pBagGetsItemEvent->tMatchSource = TriState_Yes;
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pBagGetsItemEvent, pMissionDef->filename);

		estrDestroy(&estrBuffer);
	}
	return 0;
}


AUTO_EXPR_FUNC(player, mission) ACMD_NAME(CountBagItems) ACMD_EXPR_STATIC_CHECK(player_CountBagItemsLoadVerify);
int player_CountBagItems(ExprContext *pContext, ACMD_EXPR_ENUM(InvBagIDs) const char *pcBagType)
{
	Mission *pcMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	int iResult = 0;

	if (!pPlayerEnt && pcMission && pcMission->infoOwner) {
		pPlayerEnt = pcMission->infoOwner->parentEnt;
	}

	if (!pPlayerEnt) {
		pPlayerEnt = exprContextGetVarPointerUnsafe(pContext, "targetEnt");
	}

	if (pPlayerEnt && pcBagType){
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
		iResult = inv_ent_CountItems(pPlayerEnt, StaticDefineIntGetInt(InvBagIDsEnum, pcBagType), NULL, pExtract);
	}
	return iResult;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int player_CountBagItemsWithCategoryLoadVerify(ExprContext *pContext, 
											   ACMD_EXPR_ENUM(InvBagIDs) const char *pcBag,
											   ACMD_EXPR_ENUM(ItemCategory) const char *pcCategory)
{
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pMissionDef) {
		GameEvent *pBagGetsItemEvent;
		ItemCategory eCategory = StaticDefineIntGetInt(ItemCategoryEnum, pcCategory);
		char *estrBuffer = NULL;

		pBagGetsItemEvent = StructCreate(parse_GameEvent);
		estrPrintf(&estrBuffer, "ItemBagInCategory_%s_%s", pcBag, pcCategory);
		pBagGetsItemEvent->pchEventName = allocAddString(estrBuffer);
		pBagGetsItemEvent->type = EventType_BagGetsItem;
		pBagGetsItemEvent->bagType = StaticDefineIntGetInt(InvBagIDsEnum, pcBag);
		if (eCategory > kItemCategory_None) {
			ea32Push((U32**)&pBagGetsItemEvent->eaItemCategories, eCategory);
		}
		pBagGetsItemEvent->tMatchSource = TriState_Yes;
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pBagGetsItemEvent, pMissionDef->filename);

		estrDestroy(&estrBuffer);
	}
	return 0;
}


AUTO_EXPR_FUNC(player, mission) ACMD_NAME(CountBagItemsWithCategory);
int player_CountBagItemsWithCategory(ExprContext *pContext, 
									 ACMD_EXPR_ENUM(InvBagIDs) const char *pcBag,
									 ACMD_EXPR_ENUM(ItemCategory) const char *pcCategory)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	int iResult = 0;

	if (!pPlayerEnt && pMission && pMission->infoOwner) {
		pPlayerEnt = pMission->infoOwner->parentEnt;
	}

	if (!pPlayerEnt) {
		pPlayerEnt = exprContextGetVarPointerUnsafe(pContext, "targetEnt");
	}

	if (pPlayerEnt && pcBag && pcCategory) {
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
		InvBagIDs eBagID = StaticDefineIntGetInt(InvBagIDsEnum, pcBag);
		ItemCategory eCategory = StaticDefineIntGetInt(ItemCategoryEnum, pcCategory);
		iResult = inv_ent_CountItemsWithCategory(pPlayerEnt, eBagID, eCategory, pExtract);
	}
	return iResult;
}


