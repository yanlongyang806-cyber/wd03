/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "logging.h"
#include "gslTrade.h"
#include "gslEntity.h"
#include "EntitySavedData.h"
#include "gslChat.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "rewardCommon.h"
#include "inventoryCommon.h"
#include "tradeCommon.h"
#include "chatCommon.h"
#include "inventoryTransactions.h"
#include "GameStringFormat.h"
#include "gslSendToClient.h"
#include "StringCache.h"
#include "NotifyCommon.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "tradeCommon_h_ast.h"
#include "EntityLib.h"
#include "Player.h"
#include "Character.h"
#include "entCritter.h"
#include "PowersMovement.h"
#include "referencesystem.h"
#include "OfficerCommon.h"
#include "SavedPetCommon.h"
#include "GameAccountDataCommon.h"
#include "contact_common.h"
#include "mission_common.h"
#include "itemTransaction.h"
#include "GamePermissionsCommon.h"
#include "gslLogSettings.h"

#include "AutoTransDefs.h"
#include "LoggedTransactions.h"

#include "Entity_h_ast.h"
#include "EntitySavedData_h_ast.h"

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void trade_AddSavedPet(SA_PARAM_NN_VALID Entity *pEnt, ContainerID srcPetID, S32 iTargetSlot)
{
	Item *pItem = NULL;
	ItemDef *pItemDef;
	PetDef *pPetDef;
	TradeSlot *pSlot;
	TradeBag *pCachedTradeBag;
	char *estrError;
	Entity *pSavedPet;
	int i, iNumSlots;
	GameAccountDataExtract *pExtract;
	char idBuf[128];

	pSavedPet = RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET),ContainerIDToString(srcPetID, idBuf));

	//Verify that the entity adding the saved pet, is the owner of the saved pet
	if(!pEnt->pPlayer || !pEnt->pPlayer->pTradeBag || !pSavedPet || !pSavedPet->pSaved || pSavedPet->pSaved->conOwner.containerID != pEnt->myContainerID || pSavedPet->pSaved->conOwner.containerType != pEnt->myEntityType)
		return;

	estrCreate(&estrError);
	if(!Entity_CanInitiatePetTransfer(pEnt, pSavedPet, &estrError))
	{
		ClientCmd_NotifySend(pEnt, kNotifyType_TradeFailed, estrError, NULL, NULL);
		estrDestroy(&estrError);
		return;
	}
	estrDestroy(&estrError);


	pPetDef = GET_REF(pSavedPet->pCritter->petDef);
	pItemDef = pPetDef ? GET_REF(pPetDef->hTradableItem) : NULL;

	if(!pItemDef)
		return;

	// determine if this pet has already been added
	iNumSlots = eaSize(&pEnt->pPlayer->pTradeBag->ppTradeSlots);

	if(pItemDef)
	{
		for(i=0;i<iNumSlots;i++)
		{
			pSlot = eaGet(&pEnt->pPlayer->pTradeBag->ppTradeSlots,i);

			if(pSlot && pSlot->pItem && pSlot->pItem->pSpecialProps
				&& GET_REF(pSlot->pItem->hItem) == pItemDef
				&& StringToContainerID(REF_STRING_FROM_HANDLE(pSlot->pItem->pSpecialProps->pContainerInfo->hSavedPet)) == srcPetID)
			{
				return;
			}
		}
	}

	pCachedTradeBag = StructClone(parse_TradeBag, pEnt->pPlayer->pTradeBag);

	//Create a new slot to hold a fake version of the item
	pSlot = (TradeSlot*)StructCreate(parse_TradeSlot);
	pSlot->pItem = item_FromPetInfo(pItemDef->pchName,srcPetID, GET_REF(pEnt->hAllegiance), GET_REF(pEnt->hSubAllegiance), inv_ent_getEntityItemQuality(pSavedPet));
	pSlot->count = 1;
	pSlot->SrcBagId = -1;
	pSlot->SrcSlot = -1;

	if (iTargetSlot < eaSize(&pEnt->pPlayer->pTradeBag->ppTradeSlots)) {
		eaInsert(&pEnt->pPlayer->pTradeBag->ppTradeSlots, pSlot, iTargetSlot);
	} else {
		eaPush(&pEnt->pPlayer->pTradeBag->ppTradeSlots, pSlot);
	}

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	estrError = trade_OfferIsValid(pEnt, pExtract);
	if (estrError) {
		ClientCmd_NotifySend(pEnt, kNotifyType_TradeFailed, estrError, NULL, NULL);
		estrDestroy(&estrError);
		StructDestroy(parse_TradeBag, pEnt->pPlayer->pTradeBag);
		pEnt->pPlayer->pTradeBag = pCachedTradeBag;
	} else {
		StructDestroy(parse_TradeBag, pCachedTradeBag);
		trade_ClearAcceptance(pEnt);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}

}
// Add the item in the given entity's bag and slot to that entity's trade item list.  A negative count means
// all instances of the item in that slot.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void trade_AddTradeItem(SA_PARAM_NN_VALID Entity* pEnt, S32 SrcBagId, S32 iSrcSlot, S32 iTargetSlot, int iCount)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	Item *pItem = inv_GetItemFromBag(pEnt, SrcBagId, iSrcSlot, pExtract);
	ItemDef *pItemDef;
	TradeSlot *pSlot;
	TradeBag *pCachedTradeBag;
	char *estrError;
	int i, iMaxCount, iNumSlots;
	
	if (!pItem || !pEnt->pPlayer || !pEnt->pPlayer->pTradeBag || iCount == 0) {
		return;
	}
	pItemDef = GET_REF(pItem->hItem);
	if (!pItemDef) {
		return;
	}
	
	// if the item given is a numeric, handle it as one
	if (pItemDef->eType == kItemType_Numeric) {
		trade_AddTradeNumericItem(pEnt, pItemDef->pchName, iCount);
		return;
	}
	
	pCachedTradeBag = StructClone(parse_TradeBag, pEnt->pPlayer->pTradeBag);
	iMaxCount = inv_ent_GetSlotItemCount(pEnt, SrcBagId, iSrcSlot, pExtract);
	
	// determine if any of this item is already being offered
	iNumSlots = eaSize(&pEnt->pPlayer->pTradeBag->ppTradeSlots);
	for (i = 0; i < iNumSlots; i++) {
		pSlot = eaGet(&pEnt->pPlayer->pTradeBag->ppTradeSlots, i);
		if (pSlot && pSlot->SrcBagId == SrcBagId && pSlot->SrcSlot == iSrcSlot) {
			pSlot->count += iCount;
			if (pSlot->count < 0 || pSlot->count > iMaxCount) {
				pSlot->count = iMaxCount;
			}
			if (pSlot->count == 0) {
				eaRemove(&pEnt->pPlayer->pTradeBag->ppTradeSlots, i);
				StructDestroy(parse_TradeSlot, pSlot);
			}
			break;
		}
	}
	if (i >= iNumSlots) {
		// Create a new slot structure to hold a copy of the item
		pSlot = (TradeSlot*)StructCreate(parse_TradeSlot);
		pSlot->pItem = (Item*)StructClone(parse_Item, pItem);
		pSlot->count = iCount;
		pSlot->SrcBagId = SrcBagId;
		pSlot->SrcSlot = iSrcSlot;
		if (pSlot->count < 0 || pSlot->count > iMaxCount) {
			pSlot->count = iMaxCount;
		}
		
		if (iTargetSlot < eaSize(&pEnt->pPlayer->pTradeBag->ppTradeSlots)) {
			eaInsert(&pEnt->pPlayer->pTradeBag->ppTradeSlots, pSlot, iTargetSlot);
		} else {
			eaPush(&pEnt->pPlayer->pTradeBag->ppTradeSlots, pSlot);
		}
	}
	
	estrError = trade_OfferIsValid(pEnt, pExtract);
	if (estrError) {
		ClientCmd_NotifySend(pEnt, kNotifyType_TradeFailed, estrError, NULL, NULL);
		estrDestroy(&estrError);
		StructDestroy(parse_TradeBag, pEnt->pPlayer->pTradeBag);
		pEnt->pPlayer->pTradeBag = pCachedTradeBag;
	} else {
		StructDestroy(parse_TradeBag, pCachedTradeBag);
		trade_ClearAcceptance(pEnt);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

// Add the item in the given entity's bag and slot to that entity's trade item list.  A negative count means
// all instances of the item in that slot.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void trade_AddTradeItemFromEnt(SA_PARAM_NN_VALID Entity* pEnt, S32 iSrcType, U32 iSrcID, S32 SrcBagId, S32 iSrcSlot, S32 iTargetSlot, int iCount)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	Entity *pSrcEnt = (void*)entity_GetSubEntity(iPartitionIdx, pEnt, iSrcType, iSrcID);
	Item *pItem;
	ItemDef *pItemDef;
	TradeSlot *pSlot;
	TradeBag *pCachedTradeBag;
	char *estrError;
	int i, iMaxCount, iNumSlots, iPetID;
	GameAccountDataExtract *pExtract;

	if (!pSrcEnt) {
		return;
	}

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	pItem = inv_GetItemFromBag(pSrcEnt, SrcBagId, iSrcSlot, pExtract);
	if (!pItem || !pEnt->pPlayer || !pEnt->pPlayer->pTradeBag || iCount == 0) {
		return;
	}
	pItemDef = GET_REF(pItem->hItem);
	if (!pItemDef) {
		return;
	}

	// if the item given is a numeric, handle it as one
	if (pItemDef->eType == kItemType_Numeric) {
		trade_AddTradeNumericItemFromEnt(pEnt, iSrcType, iSrcID, pItemDef->pchName, iCount);
		return;
	}

	pCachedTradeBag = StructClone(parse_TradeBag, pEnt->pPlayer->pTradeBag);
	iMaxCount = inv_ent_GetSlotItemCount(pSrcEnt, SrcBagId, iSrcSlot, pExtract);

	// determine if any of this item is already being offered
	iNumSlots = eaSize(&pEnt->pPlayer->pTradeBag->ppTradeSlots);
	iPetID = iSrcType == GLOBALTYPE_ENTITYSAVEDPET ? iSrcID : 0;
	for (i = 0; i < iNumSlots; i++) {
		pSlot = eaGet(&pEnt->pPlayer->pTradeBag->ppTradeSlots, i);
		if (pSlot && pSlot->SrcBagId == SrcBagId && pSlot->SrcSlot == iSrcSlot && pSlot->tradeSlotPetID == iPetID) {
			pSlot->count += iCount;
			if (pSlot->count < 0 || pSlot->count > iMaxCount) {
				pSlot->count = iMaxCount;
			}
			if (pSlot->count == 0) {
				eaRemove(&pEnt->pPlayer->pTradeBag->ppTradeSlots, i);
				StructDestroy(parse_TradeSlot, pSlot);
			}
			break;
		}
	}
	if (i >= iNumSlots) {
		// Create a new slot structure to hold a copy of the item
		pSlot = (TradeSlot*)StructCreate(parse_TradeSlot);
		pSlot->pItem = (Item*)StructClone(parse_Item, pItem);
		pSlot->count = iCount;
		pSlot->SrcBagId = SrcBagId;
		pSlot->SrcSlot = iSrcSlot;
		pSlot->tradeSlotPetID = iPetID;
		if (pSlot->count < 0 || pSlot->count > iMaxCount) {
			pSlot->count = iMaxCount;
		}

		if (iTargetSlot < eaSize(&pEnt->pPlayer->pTradeBag->ppTradeSlots)) {
			eaInsert(&pEnt->pPlayer->pTradeBag->ppTradeSlots, pSlot, iTargetSlot);
		} else {
			eaPush(&pEnt->pPlayer->pTradeBag->ppTradeSlots, pSlot);
		}
	}

	estrError = trade_OfferIsValid(pEnt, pExtract);
	if (estrError) {
		ClientCmd_NotifySend(pEnt, kNotifyType_TradeFailed, estrError, NULL, NULL);
		estrDestroy(&estrError);
		StructDestroy(parse_TradeBag, pEnt->pPlayer->pTradeBag);
		pEnt->pPlayer->pTradeBag = pCachedTradeBag;
	} else {
		StructDestroy(parse_TradeBag, pCachedTradeBag);
		trade_ClearAcceptance(pEnt);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

// Add the given quantity of numeric item to the entity's trade item list.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void trade_AddTradeNumericItem(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_STR const char* pItemDefName, int iCount)
{
	NOCONST(Item) *pItem;
	TradeSlot *pSlot;
	TradeBag *pCachedTradeBag;
	char *estrError;
	int i, iNumSlots;
	GameAccountDataExtract *pExtract;
	
	// only proceed if the given itemdef and entity trade bag are valid
	if (!pEnt->pPlayer || !pEnt->pPlayer->pTradeBag || iCount == 0) {
		return;
	}
	
	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	pCachedTradeBag = StructClone(parse_TradeBag, pEnt->pPlayer->pTradeBag);
	pItemDefName = allocAddString(pItemDefName);
	
	// check to see if an instance of the given numeric is already being offered
	iNumSlots = eaSize(&pEnt->pPlayer->pTradeBag->ppTradeSlots);
	for (i = 0; i < iNumSlots; i++) {
		pSlot = eaGet(&pEnt->pPlayer->pTradeBag->ppTradeSlots, i);
		if (pSlot && pSlot->pItem && REF_STRING_FROM_HANDLE(pSlot->pItem->hItem) == pItemDefName) {
			pItem = CONTAINER_NOCONST(Item, pSlot->pItem);
			pItem->count += iCount;
			if (pItem->count <= 0) {
				eaRemove(&pEnt->pPlayer->pTradeBag->ppTradeSlots, i);
				StructDestroy(parse_TradeSlot, pSlot);
			}
			break;
		}
	}
	if (i >= iNumSlots) {
		pItem = CONTAINER_NOCONST(Item, item_FromEnt( CONTAINER_NOCONST(Entity, pEnt),pItemDefName,0,NULL,0));
		pItem->count = iCount;
		
		pSlot = (TradeSlot*)StructCreate(parse_TradeSlot);
		pSlot->pItem = (Item*) pItem;
		pSlot->SrcBagId = -1;
		pSlot->SrcSlot = -1;
		
		eaPush(&pEnt->pPlayer->pTradeBag->ppTradeSlots, pSlot);
	}
	
	estrError = trade_OfferIsValid(pEnt, pExtract);
	if (estrError) {
		ClientCmd_NotifySend(pEnt, kNotifyType_TradeFailed, estrError, NULL, NULL);
		estrDestroy(&estrError);
		StructDestroy(parse_TradeBag, pEnt->pPlayer->pTradeBag);
		pEnt->pPlayer->pTradeBag = pCachedTradeBag;
	} else {
		StructDestroy(parse_TradeBag, pCachedTradeBag);
		trade_ClearAcceptance(pEnt);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

// Add the given quantity of numeric item to the entity's trade item list.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void trade_AddTradeNumericItemFromEnt(SA_PARAM_NN_VALID Entity* pEnt, S32 iSrcType, U32 iSrcID, SA_PARAM_NN_STR const char* pItemDefName, int iCount)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	Entity *pSrcEnt = entity_GetSubEntity(iPartitionIdx, pEnt, iSrcType, iSrcID);
	NOCONST(Item) *pItem;
	TradeSlot *pSlot;
	TradeBag *pCachedTradeBag;
	char *estrError;
	int i, iNumSlots;
	GameAccountDataExtract *pExtract;

	// only proceed if the given itemdef and entity trade bag are valid
	if (!pSrcEnt || !pEnt->pPlayer || !pEnt->pPlayer->pTradeBag || iCount == 0) {
		return;
	}

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	pCachedTradeBag = StructClone(parse_TradeBag, pEnt->pPlayer->pTradeBag);
	pItemDefName = allocAddString(pItemDefName);

	// check to see if an instance of the given numeric is already being offered
	iNumSlots = eaSize(&pEnt->pPlayer->pTradeBag->ppTradeSlots);
	for (i = 0; i < iNumSlots; i++) {
		pSlot = eaGet(&pEnt->pPlayer->pTradeBag->ppTradeSlots, i);
		if (pSlot && pSlot->pItem && REF_STRING_FROM_HANDLE(pSlot->pItem->hItem) == pItemDefName) {
			pItem = CONTAINER_NOCONST(Item, pSlot->pItem);
			pItem->count += iCount;
			if (pItem->count <= 0) {
				eaRemove(&pEnt->pPlayer->pTradeBag->ppTradeSlots, i);
				StructDestroy(parse_TradeSlot, pSlot);
			}
			break;
		}
	}
	if (i >= iNumSlots) {
		pItem = CONTAINER_NOCONST(Item, item_FromEnt( CONTAINER_NOCONST(Entity, pSrcEnt),pItemDefName,0,NULL,0));
		pItem->count = iCount;

		pSlot = (TradeSlot*)StructCreate(parse_TradeSlot);
		pSlot->pItem = (Item*) pItem;
		pSlot->SrcBagId = -1;
		pSlot->SrcSlot = -1;
		if (iSrcType == GLOBALTYPE_ENTITYSAVEDPET) pSlot->tradeSlotPetID = iSrcID;

		eaPush(&pEnt->pPlayer->pTradeBag->ppTradeSlots, pSlot);
	}

	estrError = trade_OfferIsValid(pEnt, pExtract);
	if (estrError) {
		ClientCmd_NotifySend(pEnt, kNotifyType_TradeFailed, estrError, NULL, NULL);
		estrDestroy(&estrError);
		StructDestroy(parse_TradeBag, pEnt->pPlayer->pTradeBag);
		pEnt->pPlayer->pTradeBag = pCachedTradeBag;
	} else {
		StructDestroy(parse_TradeBag, pCachedTradeBag);
		trade_ClearAcceptance(pEnt);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

// Remove the indexed item from the given entity's trade item list.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void trade_RemoveTradeItem(SA_PARAM_NN_VALID Entity* pEnt, S32 iSlot)
{
	TradeSlot* pSlot;
	
	if (!pEnt->pPlayer || !pEnt->pPlayer->pTradeBag) {
		return;
	}
	pSlot = (TradeSlot*)eaGet(&pEnt->pPlayer->pTradeBag->ppTradeSlots, iSlot);
	if (!pSlot) {
		return;
	}
	
	trade_ClearAcceptance(pEnt);
	eaRemove(&pEnt->pPlayer->pTradeBag->ppTradeSlots, iSlot);
	StructDestroy(parse_TradeSlot, pSlot);
	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
}

// The player is requesting a trade session with the referenced entity.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_SERVERONLY ACMD_PRIVATE;
void trade_RequestTrade(SA_PARAM_NN_VALID Entity* pPlayerEnt, EntityRef erTargetEntRef)
{
	EntityRef	erPlayerEntRef = entGetRef(pPlayerEnt);
	Entity*		pTargetEnt = entFromEntityRefAnyPartition(erTargetEntRef);
	
	// if either party is invalid, abort immediately
	if ( !pPlayerEnt->pPlayer || !pTargetEnt || !pTargetEnt->pPlayer || !pTargetEnt->pChar )
	{
		return;
	}
	
	// If the player is on a trial account, they can't trade
	if(gamePermission_Enabled() && !GamePermission_EntHasToken(pPlayerEnt, GAME_PERMISSION_CAN_USE_MARKET))
	{
		notify_NotifySend(pPlayerEnt, kNotifyType_TradeFailed, entTranslateMessageKey(pPlayerEnt, "Trade_PlayerOnTrial"), NULL, NULL);
		return;
	}
	
	// if the player is busy, deny the request
	if (pPlayerEnt->pPlayer->InteractStatus.bInteracting || pPlayerEnt->pPlayer->erTradePartner != 0) {
		notify_NotifySend(pPlayerEnt, kNotifyType_TradeFailed, entTranslateMessageKey(pPlayerEnt, "Trade_PlayerBusy"), NULL, NULL);
		return;
	}
	
	// If the target is on a trial account, they can't trade
	if( gamePermission_Enabled() && !GamePermission_EntHasToken(pPlayerEnt, GAME_PERMISSION_CAN_USE_MARKET))
	{
		notify_NotifySend(pPlayerEnt, kNotifyType_TradeFailed, entTranslateMessageKey(pPlayerEnt, "Trade_TargetOnTrial"), NULL, NULL);
		return;
	}
	
	// Check if the target player is within interact range
	if (entGetDistance(pPlayerEnt, NULL, pTargetEnt, NULL, NULL) > gslEntity_GetInteractRange(pPlayerEnt, pTargetEnt, NULL)) {
		notify_NotifySend(pPlayerEnt, kNotifyType_TradeFailed, entTranslateMessageKey(pPlayerEnt, "Trade_TargetOutOfRange"), NULL, NULL);
		return;
	}
	
	// if the target entity is busy, deny the request
	if (pTargetEnt->pPlayer->InteractStatus.bInteracting || pTargetEnt->pPlayer->erTradePartner != 0 || pmTimestamp(0) < pTargetEnt->pChar->uiTimeCombatExit) {
		char *estrTemp = NULL;
		char *estrName = NULL;
		estrConcatf(&estrName, "%s@%s", entGetLangName(pPlayerEnt, entGetLanguage(pTargetEnt)), entGetAccountOrLangName(pPlayerEnt, entGetLanguage(pTargetEnt)));
		notify_NotifySend(pPlayerEnt, kNotifyType_TradeFailed, entTranslateMessageKey(pPlayerEnt, "Trade_TargetBusy"), NULL, NULL);
		entFormatGameMessageKey(pTargetEnt, &estrTemp, "Trade_AttemptedTrade", STRFMT_STRING("name", estrName), STRFMT_END);
		notify_NotifySend(pTargetEnt, kNotifyType_TradeFailed, estrTemp, NULL, NULL);
		estrDestroy(&estrTemp);
		estrDestroy(&estrName);
		return;
	}
	 
	//Check to see if pTargetEnt is accepting trades from pPlayerEnt
	if (!entIsWhitelisted(pTargetEnt, pPlayerEnt, kPlayerWhitelistFlags_Trades) || ChatCommon_FindIgnoreByAccount(pTargetEnt, entGetAccountID(pPlayerEnt))) {
		notify_NotifySend(pPlayerEnt, kNotifyType_TradeFailed, entTranslateMessageKey(pPlayerEnt, "Trade_TargetIgnoring"), NULL, NULL);
		return;
	}

	// if the player is in a tailor
	if (pTargetEnt->pPlayer->pInteractInfo && pTargetEnt->pPlayer->pInteractInfo->pContactDialog) {
		ContactScreenType screenType = pTargetEnt->pPlayer->pInteractInfo->pContactDialog->screenType;
		if (screenType == ContactScreenType_Tailor || screenType == ContactScreenType_StarshipTailor) {
			notify_NotifySend(pPlayerEnt, kNotifyType_TradeFailed, entTranslateMessageKey(pPlayerEnt, "Trade_TargetBusy"), NULL, NULL);
			return;
		}
	}
	
	// make sure each player's acceptance status is cleared
	trade_ClearAcceptance(pPlayerEnt);
	
	// set the trade partner references for each player
	pPlayerEnt->pPlayer->erTradePartner = erTargetEntRef;
	pTargetEnt->pPlayer->erTradePartner = erPlayerEntRef;
	entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
	entity_SetDirtyBit(pTargetEnt, parse_Player, pTargetEnt->pPlayer, false);
	
	// Make sure both players have a trade bag
	if (!pPlayerEnt->pPlayer->pTradeBag) {
		pPlayerEnt->pPlayer->pTradeBag = StructCreate(parse_TradeBag);
	}
	if (!pTargetEnt->pPlayer->pTradeBag) {
		pTargetEnt->pPlayer->pTradeBag = StructCreate(parse_TradeBag);
	}
	
	// send a client command to each entity to open their trade window
	ClientCmd_trade_ReceiveTradeRequest(pPlayerEnt, true);
	ClientCmd_trade_ReceiveTradeRequest(pTargetEnt, false);
}

// Enable Trade Whitelist
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(Whitelist_Trades);
void trade_SetTradeWhitelist_cmd(Entity* pEnt, bool enabled) 
{
	if (!pEnt || !pEnt->pPlayer)
	{
		return;
	}
	if(enabled) {
		pEnt->pPlayer->eWhitelistFlags |= kPlayerWhitelistFlags_Trades;
	} else {
		pEnt->pPlayer->eWhitelistFlags &= ~kPlayerWhitelistFlags_Trades;
	}
	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
}

// Cancel the current trading session.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_SERVERONLY ACMD_HIDE;
void trade_Cancel(SA_PARAM_NN_VALID Entity* pPlayerEnt)
{
	EntityRef	erPlayerEntRef = entGetRef(pPlayerEnt);
	EntityRef	erTargetEntRef = pPlayerEnt->pPlayer->erTradePartner;
	Entity*		pTargetEnt = entFromEntityRefAnyPartition(erTargetEntRef);
	
	if (!pPlayerEnt->pPlayer->pTradeBag) {
		return;
	}
	
	// if the player has already accepted the trade, then just cancel the acceptance
	//	unless we are using the cursor mode two step process, in which case we really do want to cancel everything
	if (pPlayerEnt->pPlayer->pTradeBag->finished && !gConf.bEnableTwoStepTradeRequest) {
		trade_ClearAcceptance(pPlayerEnt);
		return;
	}
	
	// clear the canceling player's trade items and partner
	pPlayerEnt->pPlayer->erTradePartner = 0;
	trade_ClearTradeItems(pPlayerEnt);
	entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
	ClientCmd_trade_ReceiveTradeCancel(pPlayerEnt);

	// check to make sure the target player exists and is trading with this player
	if (pTargetEnt && pTargetEnt->pPlayer && pTargetEnt->pPlayer->pTradeBag && pTargetEnt->pPlayer->erTradePartner == erPlayerEntRef) {
		// clear the other player's trade items, partner, and acceptance flag
		pTargetEnt->pPlayer->pTradeBag->finished = false;
		pTargetEnt->pPlayer->erTradePartner = 0;
		trade_ClearTradeItems(pTargetEnt);
		entity_SetDirtyBit(pTargetEnt, parse_Player, pTargetEnt->pPlayer, false);
		ClientCmd_trade_ReceiveTradeCancel(pTargetEnt);
	}
}

// Accept the current trade offer. A trade completes when both players accept.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE;
void trade_Accept(SA_PARAM_NN_VALID Entity* pPlayerEnt)
{
	EntityRef	erPlayerEntRef = entGetRef(pPlayerEnt);
	EntityRef	erTargetEntRef = pPlayerEnt->pPlayer->erTradePartner;
	Entity*		pTargetEnt = entFromEntityRefAnyPartition(erTargetEntRef);
	
	if (!pPlayerEnt->pPlayer || !pPlayerEnt->pPlayer->pTradeBag || !pTargetEnt || !pTargetEnt->pPlayer || !pTargetEnt->pPlayer->pTradeBag) {
		return;
	}
	
	// make sure that these two entities know they're trading with each other
	if (pTargetEnt->pPlayer->erTradePartner != erPlayerEntRef) {
		return;
	}
	
	// mark the player as accepting the trade
	pPlayerEnt->pPlayer->pTradeBag->finished = true;
	entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
	
	// if the other player has already accepted, then execute the trade
	if (pTargetEnt->pPlayer->pTradeBag->finished) {
		char *estrError;
		GameAccountDataExtract *pExtractPlayer, *pExtractTarget;

		pExtractPlayer = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
		estrError = trade_OfferIsValid(pPlayerEnt, pExtractPlayer);
		if (estrError) {
			ClientCmd_NotifySend(pPlayerEnt, kNotifyType_TradeFailed, estrError, NULL, NULL);
			estrDestroy(&estrError);
			trade_ClearAcceptance(pPlayerEnt);
			return;
		}

		pExtractTarget = entity_GetCachedGameAccountDataExtract(pTargetEnt);
		estrError = trade_OfferIsValid(pTargetEnt, pExtractTarget);
		if (estrError) {
			ClientCmd_NotifySend(pTargetEnt, kNotifyType_TradeFailed, estrError, NULL, NULL);
			estrDestroy(&estrError);
			trade_ClearAcceptance(pPlayerEnt);
			return;
		}
		invtransaction_trade(pPlayerEnt, pTargetEnt, pPlayerEnt->pPlayer->pTradeBag, pTargetEnt->pPlayer->pTradeBag, trade_AcceptTrade_Callback, (void *)(intptr_t)entGetRef(pPlayerEnt));
	}
}

// Remove all items from the given entity's trade item list.
void trade_ClearTradeItems(SA_PARAM_NN_VALID Entity* pEnt)
{
	eaDestroyStruct(&pEnt->pPlayer->pTradeBag->ppTradeSlots, parse_TradeSlot);
}

// Clear the trade acceptance flag for this player and his trade partner, if any
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE;
void trade_ClearAcceptance(SA_PARAM_NN_VALID Entity* pPlayerEnt)
{
	Entity*	pTargetEnt;
	
	if (!pPlayerEnt->pPlayer || !pPlayerEnt->pPlayer->pTradeBag) {
		return;
	}
	pPlayerEnt->pPlayer->pTradeBag->finished = false;
	
	pTargetEnt = entFromEntityRefAnyPartition(pPlayerEnt->pPlayer->erTradePartner);
	if (!pTargetEnt || !pTargetEnt->pPlayer || !pTargetEnt->pPlayer->pTradeBag) {
		return;
	}
	pTargetEnt->pPlayer->pTradeBag->finished = false;
}

// Handle the return of the trade transaction
static void trade_AcceptTrade_Callback(TransactionReturnVal* returnVal, void *hEntity)
{
	Entity* pPlayerEnt = entFromEntityRefAnyPartition((intptr_t)hEntity);

	if (gbEnableGamePlayDataLogging && pPlayerEnt && returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS) {
		entLog(LOG_PLAYER, pPlayerEnt, "Trade", "Trade failed; outcome = %d\n", returnVal->eOutcome);
	}

	if (SAFE_MEMBER2(pPlayerEnt, pPlayer, erTradePartner) && returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		GameAccountDataExtract *pExtract;
		Entity *pTargetEnt = entFromEntityRefAnyPartition(pPlayerEnt->pPlayer->erTradePartner);
		U32 uiTime = timeSecondsSince2000();
		TradeBag *pTradeBag;
		int i;

		// Add timestamp to player
		pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
		pTradeBag = SAFE_MEMBER2(pTargetEnt, pPlayer, pTradeBag);
		for	(i = 0; pTradeBag && i < eaSize(&pTradeBag->ppTradeSlots); i++)
		{
			Item *pItem = SAFE_MEMBER2(pTradeBag, ppTradeSlots[i], pItem);
			ItemDef *pDef = GET_REF(pItem->hItem);
		}

		// Add timestamp to target
		pExtract = entity_GetCachedGameAccountDataExtract(pTargetEnt);
		pTradeBag = SAFE_MEMBER2(pPlayerEnt, pPlayer, pTradeBag);
		for	(i = 0; pTradeBag && i < eaSize(&pTradeBag->ppTradeSlots); i++)
		{
			Item *pItem = SAFE_MEMBER2(pTradeBag, ppTradeSlots[i], pItem);
			ItemDef *pDef = GET_REF(pItem->hItem);
		}
	}

	// end the trade session when done
	if (pPlayerEnt)
	{
		trade_ClearAcceptance(pPlayerEnt);
		trade_Cancel(pPlayerEnt);
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt1, "pInventoryV2.ppLiteBags, .Psaved.Ppuppetmaster.Pppuppets, .pInventoryV2.Ppinventorybags, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]")
ATR_LOCKS(pEnt2, "pInventoryV2.ppLiteBags[], .Hallegiance, .Hsuballegiance, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets, .pInventoryV2.Ppinventorybags, .Pplayer.Playertype, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
bool trade_trh_OfferIsValid(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt1, ATH_ARG NOCONST(Entity) *pEnt2, TradeBag *pTrade, GameAccountDataExtract *pExtract1, GameAccountDataExtract *pExtract2)
{
	S32 i, iNumItems, iNumPets, iSrcCount;
	TradeSlot *pSlot;
	ItemDef *pItemDef;

	iNumItems = 0;
	iNumPets = 0;
	for (i = 0; i < eaSize(&pTrade->ppTradeSlots); i++) {
		pSlot = pTrade->ppTradeSlots[i];
		pItemDef = pSlot && pSlot->pItem ? GET_REF(pSlot->pItem->hItem) : NULL;

		if (ISNULL(pItemDef)) {
			return false;
		}

		if (pItemDef->eType == kItemType_Numeric) {
			if (!(pItemDef->flags & kItemDefFlag_Tradeable)) {
				return false;
			}

			if (pSlot->count < 0 || pSlot->count < pItemDef->MinNumericValue) {
				return false;
			}

			iSrcCount = inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt1, pItemDef->pchName);
			if (pSlot->count > iSrcCount) {
				return false;
			}
			if (pSlot->count > pItemDef->MaxNumericValue || (iSrcCount - pSlot->count) < pItemDef->MinNumericValue) {
				return false;
			}

			iSrcCount = NONNULL(pEnt2) ? inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt2, pItemDef->pchName) : 0;
			if ((iSrcCount + pSlot->count) > pItemDef->MaxNumericValue) {
				return false;
			}
		} else {
			Item *pBagItem = invbag_GetItem(ATR_PASS_ARGS, pEnt1, pSlot->SrcBagId, pSlot->SrcSlot, pExtract1);
			iSrcCount = inv_ent_trh_CountItems(ATR_PASS_ARGS, pEnt1, pSlot->SrcBagId, pItemDef->pchName, -1, pExtract1);

			if (pBagItem) {
				if (pSlot->pItem->id != pBagItem->id &&
					(pItemDef->iStackLimit == 1 || pSlot->count >= iSrcCount || pItemDef->pchName == REF_STRING_FROM_HANDLE(pSlot->pItem->hItem))) {
						return false;
				}

				if (pSlot->count > iSrcCount) {
					return false;
				}

				iNumItems++;
			} else if(pSlot->SrcBagId == -1){
				//If there is no bag, it must be a temp item
				if(pItemDef->eType == kItemType_Container && pSlot->pItem->pSpecialProps)
				{
					if(NONNULL(pEnt1->pSaved->pPuppetMaster))
					{
						int j;
						ContainerID iPetID = StringToContainerID(REF_STRING_FROM_HANDLE(pSlot->pItem->pSpecialProps->pContainerInfo->hSavedPet));

						for(j=0;j<eaSize(&pEnt1->pSaved->pPuppetMaster->ppPuppets);j++)
						{
							//Cannot trade currently active puppets
							if(pEnt1->pSaved->pPuppetMaster->ppPuppets[j]->curID == iPetID
								&& pEnt1->pSaved->pPuppetMaster->ppPuppets[j]->eState == PUPPETSTATE_ACTIVE)
							{
								return false;
							}
						}

						trhOfficer_CanAddOfficer(pEnt2,NULL,pExtract2);
						iNumPets++;
					}
				}else{
					return false;
				}

			} else {
				return false;
			}
		}

		if (NONNULL(pEnt2)) {
			if (iNumItems > inv_ent_trh_AvailableSlots(ATR_PASS_ARGS, pEnt2, InvBagIDs_Inventory, pExtract2)) {
				return false;
			}
		}

		if (NONNULL(pEnt2) && iNumPets)
		{
			S32 iMaxOfficers = trhOfficer_GetMaxAllowedPets(pEnt2,allegiance_GetOfficerPreference(GET_REF(pEnt2->hAllegiance),GET_REF(pEnt2->hSubAllegiance)),pExtract2);
			S32 iOfficers = trhEntity_CountPets(pEnt2,true,false,false);

			if(iMaxOfficers < iOfficers + iNumPets)
				return false;
		}
	}

	return true;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void trade_SendGift(SA_PARAM_NN_VALID Entity* pEnt, int iEntID, int SrcBagIdx, int SrcSlot)
{
	TransactionReturnVal* returnVal;
	GameAccountDataExtract *pExtract;
	Item* pItem;

	if (!iEntID) {
		return;
	}
	returnVal = LoggedTransactions_CreateManagedReturnValEnt("trade_SendGift", pEnt, NULL, NULL);

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	pItem = inv_GetItemFromBag(pEnt, SrcBagIdx, SrcSlot, pExtract);

	if (pItem)
	{
		ItemChangeReason reason = {0};

		inv_FillItemChangeReason(&reason, pEnt, "Trade:SendGift", NULL);
		itemtransaction_MoveItemAcrossEnts(returnVal, entGetType(pEnt), entGetContainerID(pEnt), SrcBagIdx, SrcSlot, pItem->id, 
											entGetType(pEnt), iEntID, InvBagIDs_Inventory, -1, 0, 1, &reason, &reason);
		
	}
}




















