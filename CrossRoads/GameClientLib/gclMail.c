#include "cmdparse.h"
#include "Expression.h"
#include "earray.h"

#include "UIGen.h"

#include "gclEntity.h"
#include "contact_common.h"

#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "inventoryCommon.h"
#include "NotifyCommon.h"
#include "StashTable.h"
#include "TextFilter.h"
#include "Player.h"
#include "gclLogin.h"
#include "LoginCommon.h"
#include "GameClientLib.h"
#include "StringUtil.h"
#include "EntitySavedData.h"
#include "SavedPetCommon.h"
#include "utilitiesLib.h"
#include "GamePermissionsCommon.h"
#include "FCInventoryUI.h"
#include "Login2Common.h"
#include "MailCommon.h"
#include "MailCommon_h_ast.h"
#include "chatCommonStructs.h"
#include "chatcommon.h"
#include "auctionlot.h"
#include "auctionlot_h_ast.h"
#include "uigen.h"
#include "AutoGen/itemCommon_h_ast.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

/*

	EmailV3 UI functions. These functions aggregate mail from 3 possible sources:
	1) New EmailV3 storage for player-to-player messages, located on the AccountSharedBank.
	2) Old NPCMail storage located on the player structure.
	3) Old chatserver mail.

*/

static EmailV3UIMessage** s_eaMessages = NULL;

static int EmailV3_SortByDateSent( const EmailV3UIMessage** ppMessage1, const EmailV3UIMessage** ppMessage2 )
{
	return (*ppMessage1)->sent-(*ppMessage2)->sent;
}

AUTO_STARTUP(PlayerEmail);
void EmailV3_Startup(void)
{
	ui_GenInitStaticDefineVars(EMailV3TypeEnum, "EmailMessageType");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenDeleteMessage);
void EmailV3_DeleteMessage(SA_PARAM_OP_VALID Entity* pEnt, U32 uMessageID, EMailV3Type eType)
{
	EmailV3UIMessage* pUIMessage = NULL;
	int i;
	for (i = 0; i < eaSize(&s_eaMessages); i++)
	{
		if (s_eaMessages[i]->uID == uMessageID && s_eaMessages[i]->eTypeOfEmail == eType)
		{
			pUIMessage = s_eaMessages[i];
			break;
		}
	}

	if (!pUIMessage)
		return;

	switch (eType)
	{
	case kEmailV3Type_Player:
		ServerCmd_EmailV3_DeleteMessage(pUIMessage->uID);
	default:
		ServerCmd_MailDeleteCompleteLog(pUIMessage->uID, pUIMessage->iNPCEMailID, pUIMessage->uLotID);
	}
}

// Set this gen's model to the list of mail the player has.
// Rebuilds the list every frame to prevent stale-pointer ugliness.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetMailList);
void EmailV3_ExprGenGetMailList(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID UIGen *pGen)
{
	Entity* pSharedBank = SAFE_GET_REF2(pEnt, pPlayer, hSharedBank);
	EmailV3_RebuildMailList(pEnt,pSharedBank,&s_eaMessages);
	ui_GenSetList(pGen, &s_eaMessages, parse_EmailV3UIMessage);
}

// Send a mail.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailSend);
void EmailV3_ExprGenSendMail(const char *pchTo, const char *pchSubject, const char *pchBody, SA_PARAM_OP_VALID UIGen* pAttachedItemHolder)
{
	EmailV3SenderItemsWrapper wrapper = {0};
	if (pAttachedItemHolder)
	{
		EmailV3SenderItem ***peaSlots = ui_GenGetManagedListSafe(pAttachedItemHolder, EmailV3SenderItem);
		wrapper.eaItemsFromPlayer = *peaSlots;
	}
	ServerCmd_EmailV3_SendPlayerEmail(pchSubject, pchBody, pchTo, &wrapper);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailCanViewItems);
bool EmailV3_MailCanViewItems(EmailV3UIMessage *pUIMessage)
{
	Entity *ent = entActivePlayerPtr();
	char shardName[256];


	if (!ent)
		return false;
	if (pUIMessage->pMessage)
		return true;
	if (pUIMessage->pNPCMessage && eaSize(&pUIMessage->pNPCMessage->ppItemSlot) > 0)
		return true;
	if (!pUIMessage || !pUIMessage->uLotID)
		return false;


	shardName[0] = 0;

	if (stricmp(pUIMessage->shardName, GetVShardQualifiedName(shardName, ARRAY_SIZE_CHECKED(shardName), entGetVirtualShardID(ent)))!=0)
	{
		//don't show items in mail from another shard
		return false;
	}
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailTakeAttachedItems);
void EmailV3_GenExprMailTakeAttachedItems(U32 uID, EMailV3Type eType)
{
	EmailV3UIMessage *pUIMessage = NULL;
	int i;
	for (i = 0; i < eaSize(&s_eaMessages); i++)
	{
		if (s_eaMessages[i]->uID == uID && s_eaMessages[i]->eTypeOfEmail == eType)
		{
			pUIMessage = s_eaMessages[i];
			break;
		}
	}
	if (pUIMessage && pUIMessage->pChatServerMessage || pUIMessage->pNPCMessage)
	{
		ServerCmd_MailTakeItems(pUIMessage->uLotID, pUIMessage->iNPCEMailID);
	}
	else if (pUIMessage && pUIMessage->pMessage)
	{
		ServerCmd_EmailV3_TakeAllAttachedItems(pUIMessage->pMessage->uID);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailRead);
void EmailV3_ExprGenMarkMailRead(SA_PARAM_NN_VALID EmailV3UIMessage *pUIMessage)
{
	if(!pUIMessage->bRead && (pUIMessage->pChatServerMessage || pUIMessage->pNPCMessage))
	{
		pUIMessage->bRead = true;	// marking mail as read to reduce number of updates to client
		ServerCmd_ServerChat_MarkMailRead(pUIMessage->uID, pUIMessage->iNPCEMailID);
	}
	else if (!pUIMessage->bRead && pUIMessage->pMessage && !pUIMessage->pMessage->bRead)
	{
		ServerCmd_EmailV3_MarkMessageAsRead(pUIMessage->pMessage->uID);
	}
}

//Returns true if player has any unread messages.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenHasUnreadMail);
bool gclMail_GenMailHasUnreadMail(SA_PARAM_OP_VALID Entity *pEnt)
{
	Entity* pAccountSharedBank = SAFE_GET_REF2(pEnt, pPlayer, hSharedBank);
	EmailV3* pMailbox = EmailV3_GetSharedBankMail(pAccountSharedBank);
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->bUnreadMail)
	{
		return true;
	}
	else if (pMailbox && pMailbox->bUnreadMail)
	{
		return true;
	}
	return false;
}

// Get the AuctionItem(s) referenced by the mail into the cache.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailGetItems);
void EmailV3_ExprGenGetItems(EmailV3UIMessage *pUIMessage)
{
	if (pUIMessage && pUIMessage->pChatServerMessage && EmailV3_MailCanViewItems(pUIMessage))
	{
		ServerCmd_ServerChat_MailGetItems(pUIMessage->pChatServerMessage->uLotID);
	}
}

//To prevent stale pointer shenanigans in the UI.
static bool bClearSendingItemsThisFrame = false;

// Clear a TradeSlotLite's contents.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EmailClearAttachedItem);
void EmailV3_ExprGenClearAttachedItem(SA_PARAM_OP_VALID EmailV3SenderItem *pSlot)
{
	if (pSlot)
	{
		pSlot->iCount = 0;
		pSlot->uID = 0;
		pSlot->pchLiteItemName = NULL;
		if (pSlot->pItem)
		{
			StructDestroy(parse_Item, pSlot->pItem);
			pSlot->pItem = NULL;
		}
	}
}

// Get a list of the items in a mail, as EmailV3SenderItems.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenMailGetSendingItemList);
S32 EmailV3_ExprGenGetSendingItemList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	EmailV3SenderItem ***peaSlots = ui_GenGetManagedListSafe(pGen, EmailV3SenderItem);
	S32 i;
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	while (eaSize(peaSlots) < MAIL_ITEM_MAX)
		eaPush(peaSlots, StructCreate(parse_EmailV3SenderItem));

	if (bClearSendingItemsThisFrame)
	{
		for (i = 0; i < eaSize(peaSlots); i++)
		{
			EmailV3_ExprGenClearAttachedItem((*peaSlots)[i]);
		}
		bClearSendingItemsThisFrame = false;
	}
	else
	{
		//regenerate item clones if we need to
		for (i = 0; i < eaSize(peaSlots); i++)
		{
			EmailV3SenderItem *pSenderItem = (*peaSlots)[i];
			BagIterator* pIter = NULL;
			ItemDef* pItemDef = NULL;
			Item* pItem = NULL;
			NOCONST(Item)* pItemCloneForUI = NULL;
			Entity* pItemEnt = pEnt;
			
			if (pSenderItem->iPetID)
				pItemEnt = entity_GetSubEntity(PARTITION_CLIENT, pEnt, GLOBALTYPE_ENTITYSAVEDPET, pSenderItem->iPetID);
		
			if (pSenderItem->pItem && pSenderItem->bDestroyNextFrame)
			{
				StructDestroy(parse_Item, pSenderItem->pItem);
				pSenderItem->pItem = NULL;
			}

			if ((pSenderItem->pchLiteItemName || pSenderItem->uID) && !pSenderItem->pItem)
			{
				if (pSenderItem->pchLiteItemName)
					//only Numeric lite items supported right now.
					pIter = inv_bag_trh_FindItemByDefName(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pItemEnt), InvBagIDs_Numeric, pSenderItem->pchLiteItemName, NULL);
				else
					pIter = inv_trh_FindItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pItemEnt), pSenderItem->uID);

				if (pIter)
				{
					pItemDef = bagiterator_GetDef(pIter);
					pItem = CONTAINER_RECONST(Item, bagiterator_GetItem(pIter));

					if (!pItem && pItemDef)
					{
						pItemCloneForUI = inv_ItemInstanceFromDefName(pSenderItem->pchLiteItemName, 0, 0, NULL, NULL, NULL, false, NULL);
					}
					else if (pItem)
					{
						pItemCloneForUI = StructCloneDeConst(parse_Item, pItem);
					}
					if (pSenderItem->iCount > 0)
						pItemCloneForUI->count = pSenderItem->iCount;
					pSenderItem->pItem = CONTAINER_RECONST(Item, pItemCloneForUI);
				}
			}
		}
	}

	ui_GenSetManagedListSafe(pGen, peaSlots, EmailV3SenderItem, true);
	return eaSize(peaSlots);
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EmailClearAllAttachedItems);
void EmailV3_ExprGenClearAllAttachedItems(SA_PARAM_OP_VALID UIGen* pGen)
{
	bClearSendingItemsThisFrame = true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EmailAttachItem);
void EmailV3_ExprGenAttachItem(SA_PARAM_NN_VALID UIGen *pGen, U32 uSlot, U64 uID, int iCount, int iPetID)
{
	EmailV3SenderItem ***peaSlots = ui_GenGetManagedListSafe(pGen, EmailV3SenderItem);
	EmailV3SenderItem* pSlot = eaGet(peaSlots, uSlot);
	if (pSlot)
	{
		int i;
		if (!uID && iPetID)
		{
			Entity* pPlayer = entActivePlayerPtr();
			Item* pItem = item_FromSavedPet(pPlayer, iPetID);
			uID = pItem->id;
		}
		for (i = 0; i < eaSize(peaSlots); i++)
		{
			//Check if this item has already been attached.
			if ((*peaSlots)[i] && (*peaSlots)[i]->uID == uID && (*peaSlots)[i]->iPetID == iPetID)
				return;
		}
		pSlot->iCount = iCount;
		pSlot->uID = uID;
		pSlot->iPetID = iPetID;
		pSlot->pchLiteItemName = NULL;

		if (pSlot->pItem)
		{
			pSlot->bDestroyNextFrame = true;
		}
	}
}

// Clear a TradeSlotLite's contents.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EmailAttachNumeric);
void EmailV3_ExprGenAttachNumeric(SA_PARAM_OP_VALID EmailV3SenderItem* pSlot, const char* pchDefName, int iCount, int iPetID)
{
	if (pSlot)
	{
		pSlot->iCount = iCount;
		pSlot->uID = 0;
		pSlot->iPetID = iPetID;
		pSlot->pchLiteItemName = pchDefName;

		if (pSlot->pItem)
		{
			StructDestroy(parse_Item, pSlot->pItem);
			pSlot->pItem = NULL;
		}
	}
}

static StashTable s_stMailItemMapping = NULL;

// Get a list of the items in a mail, as InventorySlots including the NPC email items
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenMailGetFullItemList);
void gclMail_GenMailGetFullItemList(SA_PARAM_NN_VALID UIGen *pGen, U32 uID, EMailV3Type eType)
{
	EmailV3UIMessage* pUIMessage = NULL;
	static InventorySlot **s_eaSlots = NULL;
	AuctionLot *pLot = NULL;
	U32 uiLotID = 0, toContainerID = 0;
	S32 iNPCEmailID = 0;
	S32 i;
	for (i = 0; i < eaSize(&s_eaMessages); i++)
	{
		if (s_eaMessages[i]->uID == uID && s_eaMessages[i]->eTypeOfEmail == eType)
		{
			pUIMessage = s_eaMessages[i];
			break;
		}
	}
	if (pUIMessage)
	{
		uiLotID = pUIMessage->uLotID;
		iNPCEmailID = pUIMessage->iNPCEMailID;
		toContainerID = pUIMessage->toContainerID;
	}
	else
	{
		ui_GenSetListSafe(pGen, NULL, InventorySlot);
		return;
	}
	eaDestroyStruct(&s_eaSlots, parse_InventorySlot);

	if(uiLotID > 0)
	{
		if (s_stMailItemMapping)
		{
			stashIntFindPointer(s_stMailItemMapping, uiLotID, &pLot);
			if(pLot)
			{
				eaCopyStructs((InventorySlot ***)&pLot->ppItemsV2, &s_eaSlots, parse_InventorySlot);
			}
		}
	}
	else if(pUIMessage->pNPCMessage)
	{
		eaCopyStructs(&pUIMessage->pNPCMessage->ppItemSlot, &s_eaSlots, parse_InventorySlot);
	}
	else if (pUIMessage->pMessage)
	{
		for (i = 0; i < eaSize(&pUIMessage->pMessage->ppItems); i++)
		{
			NOCONST(InventorySlot)* pSlot = inv_InventorySlotCreate(i);
			pSlot->pItem = StructCloneDeConst(parse_Item, pUIMessage->pMessage->ppItems[i]);
			eaPush(&s_eaSlots, CONTAINER_RECONST(InventorySlot, pSlot));
		}
	}

	ui_GenSetListSafe(pGen, &s_eaSlots, InventorySlot);
}

static void gclMail_AddLot(U32 uLotID, NOCONST(AuctionLot)* pLot)
{
	NOCONST(AuctionLot) *pClientLot = NULL;

	if (!uLotID)
		return;

	if (!s_stMailItemMapping)
	{
		s_stMailItemMapping = stashTableCreateInt(10);
	}
	if (!stashIntFindPointer(s_stMailItemMapping, uLotID, &pClientLot))
	{
		pClientLot = StructCreateNoConst(parse_AuctionLot);
		stashIntAddPointer(s_stMailItemMapping, uLotID, pClientLot, false);
	}

	eaDestroyStructNoConst(&pClientLot->ppItemsV2, parse_AuctionSlot);

	if (pLot)
	{
		eaCopy(&pClientLot->ppItemsV2, &pLot->ppItemsV2);
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclMail_MailGetItemReturn(U32 uLotID, AuctionLot *pItemList)
{
	NOCONST(AuctionLot) *pServerLot = CONTAINER_NOCONST(AuctionLot, pItemList);

	if (!uLotID)
		return;

	if (pServerLot)
	{
		gclMail_AddLot(uLotID, pServerLot);
		eaClear(&pServerLot->ppItemsV2);
	}
	else
	{
		NOCONST(AuctionLot) *pClientLot = NULL;
		if (stashIntRemovePointer(s_stMailItemMapping, uLotID, &pClientLot))
		{
			StructDestroyNoConst(parse_AuctionLot, pClientLot);
		}
	}
}

static void AuctionListDestroy(AuctionLot *pLot)
{
	StructDestroy(parse_AuctionLot, pLot);
}

// Clear all auction lots stored in the mail cache.
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME(MailClearItemCache);
void gclMail_MailClearItemCache(void)
{
	stashTableDestroyEx(s_stMailItemMapping, NULL, AuctionListDestroy);
	s_stMailItemMapping = NULL;
}

// Get a list of the items in a mail, as InventorySlots.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenMailGetItemList);
void gclMail_GenMailGetItemList(SA_PARAM_NN_VALID UIGen *pGen, U32 uiLotID)
{
	AuctionLot *pLot = NULL;

	if (s_stMailItemMapping)
		stashIntFindPointer(s_stMailItemMapping, uiLotID, &pLot);

	// FIXME(jm): This is dangerous as it assumes that an AuctionSlot can map to an InventorySlot.

	if (pLot)
		ui_GenSetListSafe(pGen, (InventorySlot ***)&pLot->ppItemsV2, InventorySlot); // Actually AuctionSlots, but first members are identical
	else
		ui_GenSetListSafe(pGen, NULL, InventorySlot);
}

// Checks if there are any mail items to be gotten, returns true if there are.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenMailGetShowItemList);
bool gclMail_GenMailGetShowItemList(SA_PARAM_NN_VALID UIGen *pGen, U32 uiLotID, S32 iNPCEmailID, U32 toContainerID)
{
	AuctionLot *pLot = NULL;
	bool bAnswer = false;

	if(uiLotID > 0)
	{
		if (s_stMailItemMapping)
		{
			stashIntFindPointer(s_stMailItemMapping, uiLotID, &pLot);
			if(pLot)
				return eaSize(&pLot->ppItemsV2);
		}
	}
	else if(iNPCEmailID > 0)
	{
		S32 i;
		Entity *pEnt = entActivePlayerPtr();
		if(pEnt && pEnt->pPlayer && pEnt->myContainerID == toContainerID)
		{
			for(i = 0; i < eaSize(&pEnt->pPlayer->pEmailV2->mail); ++i)
			{
				if(pEnt->pPlayer->pEmailV2->mail[i]->iNPCEMailID == iNPCEmailID)
					return eaSize(&pEnt->pPlayer->pEmailV2->mail[i]->ppItemSlot);
			}
		}
	}
	return false;
}