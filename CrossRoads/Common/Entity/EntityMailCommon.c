/***************************************************************************
*     Copyright (c) 2005-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMailCommon.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "itemCommon.h"
#include "rewardCommon.h"
#include "ChatCommonstructs.h"
#include "utilitiesLib.h"
#include "EntityLib.h"
#include "GameStringFormat.h"
#include "Player.h"
#include "entCritter.h"
#include "Player_h_ast.h"
#include "logging.h"
#include "AuctionLot.h"
#include "EntitySavedData.h"

#include "autogen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/ObjectDB_autogen_remotefuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/EntityMailCommon_h_ast.h"
#include "autogen/AuctionLot_h_ast.h"
#include "autogen/EntitySavedData_h_ast.h"

AUTO_TRANS_HELPER_SIMPLE;
MailCharacterItems *CharacterMailAddItem(MailCharacterItems *pItems, Item *pItem, U32 uQuantity)
{
	if(!pItems)
	{
		pItems = StructCreate(parse_MailCharacterItems);
	}

	if(pItems)
	{
		eaPush(&pItems->eaMailItems, pItem);
	}

	return pItems;
}

AUTO_TRANS_HELPER;
MailCharacterItems *CharacterMailAddItemsFromAuctionLot(MailCharacterItems *pItems, ATH_ARG NOCONST(AuctionLot) *pLot)
{
	if(!pItems)
	{
		pItems = StructCreate(parse_MailCharacterItems);
	}

	if(pItems)
	{
		int i;
		for (i = 0; i < eaSize(&pLot->ppItemsV2); i++)
		{
			eaPush(&pItems->eaMailItems, StructCloneReConst(parse_Item, pLot->ppItemsV2[i]->slot.pItem));
		}
	}

	return pItems;
}

AUTO_TRANS_HELPER;
void EntityMail_AddItemCleanupFromError(ATR_ARGS, ContainerID iContainerID, ATH_ARG MailCharacterItems *pItems)
{
	if(pItems)
	{
		// fail case, destroy all structs that have not been pushed on to the character
		if(eaSize(&pItems->eaMailItems) > 0)
		{
			S32 i;
			for(i = 0; i < eaSize(&pItems->eaMailItems); ++i)
			{
				if(pItems->eaMailItems[i])
				{
					ItemDef *pItemDef = GET_REF(pItems->eaMailItems[i]->hItem);
					if(NONNULL(pItemDef))
					{
						TRANSACTION_APPEND_LOG_FAILURE("EntityMail_AddItemCleanupFromError ent %d failed to add item %s.", iContainerID, pItemDef->pchName);
					}
				}
			}
		}

		// destroy all data in pItems
		StructDestroy(parse_MailCharacterItems, pItems);
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.pEmailV2.Mail, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp");
int EntityMail_trh_AddItemFromDef(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, S32 iNPCEmailID, const char* ItemDefName)
{
	S32 iSlotIndex = -1, i, bRet = -1;
	NOCONST(Item)* pItem = NULL;
	NOCONST(InventorySlot) *pInvSlot = NULL;

	for(i = 0; i < eaSize(&pEnt->pPlayer->pEmailV2->mail); ++i)
	{
		if(pEnt->pPlayer->pEmailV2->mail[i]->iNPCEMailID == iNPCEmailID)
		{
			iSlotIndex = i;
			break;
		}
	}

	if(iSlotIndex >= 0)
	{
		pItem = CONTAINER_NOCONST(Item, item_FromEnt( pEnt,ItemDefName,false,NULL,0));

		if(NONNULL(pItem))
		{
			pInvSlot = StructCreateNoConst(parse_InventorySlot);
			if(NONNULL(pInvSlot))
			{
				pInvSlot->pItem = pItem;

				eaPush(&pEnt->pPlayer->pEmailV2->mail[iSlotIndex]->ppItemSlot, pInvSlot);
				bRet = eaSize(&pEnt->pPlayer->pEmailV2->mail[iSlotIndex]->ppItemSlot) - 1;
			}
		}
	}

	if(bRet < 0)	
	{
		if(NONNULL(pInvSlot))
		{
			StructDestroyNoConst(parse_InventorySlot, pInvSlot);
		}
		else if(NONNULL(pItem))
		{
			StructDestroyNoConst(parse_Item, pItem);
		}
		TRANSACTION_APPEND_LOG_FAILURE("NPC email failed to create item from def %s", ItemDefName );
	}

	return bRet;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.pEmailV2.Mail");
int EntityMail_trh_AddItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, MailCharacterItems *pItems, S32 iNPCEmailID)
{
	S32 iSlotIndex = -1, i, bRet = -1, j;
	NOCONST(InventorySlot) *pInvSlot = NULL;

	if(NONNULL(pItems))
	{
		for(i = 0; i < eaSize(&pEnt->pPlayer->pEmailV2->mail); ++i)
		{
			if(pEnt->pPlayer->pEmailV2->mail[i]->iNPCEMailID == iNPCEmailID)
			{
				iSlotIndex = i;
				break;
			}
		}

		if(iSlotIndex >= 0)
		{
			for(j = 0; j < eaSize(&pItems->eaMailItems); ++j)
			{
				pInvSlot = StructCreateNoConst(parse_InventorySlot);
				if(NONNULL(pInvSlot))
				{
					pInvSlot->pItem = CONTAINER_NOCONST(Item, pItems->eaMailItems[j]);

					eaPush(&pEnt->pPlayer->pEmailV2->mail[iSlotIndex]->ppItemSlot, pInvSlot);
					bRet = eaSize(&pEnt->pPlayer->pEmailV2->mail[iSlotIndex]->ppItemSlot) - 1;

					pItems->eaMailItems[j] = NULL;	// no need to free this item in case of error
					pInvSlot = NULL;					// slot also does not need to be freed

				}
				else
				{
					bRet = -1;
					break;
				}
			}
		}

		if(bRet < 0)	
		{
			if(eaSize(&pItems->eaMailItems) < 1)
			{
				TRANSACTION_APPEND_LOG_FAILURE("EntityMail_trh_AddItem failed to add items due zero items in pItems.");
			}
			else if(iSlotIndex < 0)
			{
				// no slot index ...
				TRANSACTION_APPEND_LOG_FAILURE("EntityMail_trh_AddItem failed to add items due to iSlotIndex < 0.");
			}

			// fail case, destroy all structs that have not been pushed on to the character
			EntityMail_AddItemCleanupFromError(ATR_PASS_ARGS, pEnt->myContainerID, pItems);

			// free inventory slots that have not been added to character			
			if(NONNULL(pInvSlot))
			{
				StructDestroyNoConst(parse_InventorySlot, pInvSlot);
			}
		}
		else
		{
			// cleanup pItems
			StructDestroy(parse_MailCharacterItems, pItems);
		}
	}
	else
	{
		TRANSACTION_APPEND_LOG_FAILURE("EntityMail_trh_AddItem failed to add items due NULL pItems.");
	}

	return bRet;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Pplayer.pEmailV2.Ilastusedid, .Pplayer.pEmailV2.Mail");
bool EntityMail_trh_NPCAddMail(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char *fromName, const char *subject, const char *body,
							   MailCharacterItems *pCharacterItems, U32 uFutureTimeSeconds, NPCEmailType eType)
{
	NOCONST(NPCEMailData) *pNewMail;
	bool bRet = true;

	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
	{
		TRANSACTION_APPEND_LOG_FAILURE("EntityMail_trh_NPCAddMail failed due to pEnt or pEnt->Player being NULL.");
		return false;
	}

	pNewMail = StructCreateNoConst(parse_NPCEMailData);

	if(ISNULL(pNewMail))
	{
		TRANSACTION_APPEND_LOG_FAILURE("EntityMail_trh_NPCAddMail failed due to pNewMail being NULL.");
		if(!pCharacterItems)
		{
			EntityMail_AddItemCleanupFromError(ATR_PASS_ARGS, pEnt->myContainerID, pCharacterItems);
		}
		return false;
	}

	if(++pEnt->pPlayer->pEmailV2->iLastUsedID <= 0)
	{
		pEnt->pPlayer->pEmailV2->iLastUsedID = 1;	// start over, of course this seems extremely unlikely to happen
	}
	pNewMail->sentTime = timeSecondsSince2000() + uFutureTimeSeconds;
	pNewMail->iNPCEMailID = pEnt->pPlayer->pEmailV2->iLastUsedID;
	pNewMail->fromName = estrCreateFromStr(fromName);
	pNewMail->subject = estrCreateFromStr(subject);
	pNewMail->body = estrCreateFromStr(body);
	pNewMail->eType = eType;

	eaPush(&pEnt->pPlayer->pEmailV2->mail, pNewMail);

	// If there is pItem Add it to the mail	
	if(NONNULL(pCharacterItems))
	{
		// this destroys pCharacterItems->eaItems and any items in that struct upon error
		if(EntityMail_trh_AddItem(ATR_PASS_ARGS, pEnt, pCharacterItems, pNewMail->iNPCEMailID) < 0)
		{
			bRet = false;
		}
	}

	return bRet;
}

#include "AutoGen/EntityMailCommon_h_ast.c"
