/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Player.h"
#include "EntCritter.h"
#include "EntitySavedData.h"
#include "gslInteraction.h"
#include "gslInteractable.h"
#include "gslInteractLoot.h"
#include "inventoryCommon.h"
#include "logging.h"
#include "reward.h"
#include "itemTransaction.h"
#include "GameStringFormat.h"
#include "NotifyCommon.h"
#include "ChatData.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "inventoryTransactions.h"

// ----------------------------------------------------------------------------------
// MASTER LOOTER
// ----------------------------------------------------------------------------------

static TeamLootEvent *interactloot_MasterLooterGetLootEvent(Entity *pLeaderEnt, U64 itemID, InventoryBag **ppLootBagOut)
{
	Team *pTeam = team_GetTeam(pLeaderEnt);
	InteractionLootTracker *pLootTracker = NULL;
	TeamLootEvent *pEvent = NULL;
	WorldInteractionNode *pNode = NULL;
	int i;
	int iPartitionIdx;

	if (!pLeaderEnt || !pLeaderEnt->pPlayer || !pTeam || !pLeaderEnt->pPlayer->InteractStatus.interactTarget.bLoot)
		return NULL;

	iPartitionIdx = entGetPartitionIdx(pLeaderEnt);

	if (pLeaderEnt->pPlayer->InteractStatus.interactTarget.entRef && stashIntFindPointer(s_LootEventFromLootEntRef, pLeaderEnt->pPlayer->InteractStatus.interactTarget.entRef, &pEvent))
	{
		Entity *pLootEnt = entFromEntityRef(iPartitionIdx, pLeaderEnt->pPlayer->InteractStatus.interactTarget.entRef);
		if (pLootEnt && interaction_IsLootEntity(pLootEnt))
		{
			BagIterator* pIter = NULL;
			FOR_EACH_IN_EARRAY(pLootEnt->pCritter->eaLootBags, InventoryBag, pBag)
			{
				if (pIter = inv_bag_trh_FindItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(InventoryBag, pBag), itemID, pIter))
				{
					*ppLootBagOut = pBag;
					bagiterator_Destroy(pIter);
					break;
				}
			}
			FOR_EACH_END
		}
		else if (SAFE_MEMBER2(pLootEnt, pCritter, encounterData.pLootTracker))
			pLootTracker = pLootEnt->pCritter->encounterData.pLootTracker;
	}
	else if (pNode = GET_REF(pLeaderEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode))
	{
		GameInteractable *pInteractable = interactable_GetByNode(pNode);
		TeamLootEvent ***peaEvents = NULL;

		pLootTracker = interactable_GetLootTracker(iPartitionIdx, pInteractable, false);
		if (pInteractable && stashFindPointer(s_LootEventFromLootInteractName, pInteractable->pcName, (void*) &peaEvents))
		{
			for (i = 0; i < eaSize(peaEvents); i++)
			{
				TeamLootEvent *pTempEvent = (*peaEvents)[i];
				if (pTeam == GET_REF(pTempEvent->hTeam))
				{
					pEvent = pTempEvent;
					break;
				}
			}
		}
	}

	if (pLootTracker)
		*ppLootBagOut = eaGet(&pLootTracker->eaLootBags, 0);

	return pEvent;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("MasterLooterPromptRoll") ACMD_SERVERCMD ACMD_SERVERONLY ACMD_PRIVATE;
void interactloot_MasterLooterPromptRoll(Entity *pLeaderEnt, int itemID)
{
	Team *pTeam = SAFE_GET_REF2(pLeaderEnt, pTeam, hTeam);
	TeamLootEvent *pEvent;
	InventoryBag *pServerBag = NULL;
	Item *pServerItem = NULL;
	int i, iServerItemCount = 0;

	// ensure the leader is still interacting with a loot target for which we have an event registered
	if (!pLeaderEnt || !pLeaderEnt->pPlayer || !pLeaderEnt->pPlayer->InteractStatus.interactTarget.bLoot)
		return;
	pEvent = interactloot_MasterLooterGetLootEvent(pLeaderEnt, itemID, &pServerBag);
	if (!pEvent)
		return;

	// ensure the team is the same as the one registered to the event
	if (!pTeam || pTeam != GET_REF(pEvent->hTeam))
		return;

	// ensure this command was invoked by the team leader
	if (!pTeam->pLeader || pTeam->pLeader->iEntID != pLeaderEnt->myContainerID)
		return;

	// ensure the loot was dropped in Master Looter mode
	if (pEvent->eLootMode != LootMode_MasterLooter)
		return;

	// get the item from the loot bag
	if (!pServerBag)
		return;
	pServerItem = inv_bag_GetItemByID(pServerBag, itemID, NULL, &iServerItemCount);

	// send the notification for the item to all team members
	if (pServerItem && !pServerItem->bTransactionPending && iServerItemCount > 0)
	{
		char *estrLeaderName = NULL;
		Entity **peaOnMapMembers = NULL;

		// get the leader name
		estrPrintf(&estrLeaderName, "%s@%s", entGetLocalName(pLeaderEnt),
			pLeaderEnt->pPlayer->publicAccountName && pLeaderEnt->pPlayer->publicAccountName[0] ? pLeaderEnt->pPlayer->publicAccountName : pLeaderEnt->pPlayer->privateAccountName);		

		team_GetOnMapEntsUnique(entGetPartitionIdx(pLeaderEnt), &peaOnMapMembers, pTeam, false);
		for (i = 0; i < eaSize(&peaOnMapMembers); i++)
		{
			Entity *pMemberEnt = peaOnMapMembers[i];
			ChatData *pChatData = NULL;
			ChatLinkInfo *pItemLinkInfo = NULL;
			const char *pchItemName = item_GetName(pServerItem, pMemberEnt);
			char *estrItemLinkText = NULL;
			char *estrFormattedItemName = NULL;
			char *estrNotification = NULL;

			estrPrintf(&estrItemLinkText, "[%s]", pchItemName);

			if (iServerItemCount > 1)
				estrPrintf(&estrFormattedItemName, "%i %s", iServerItemCount, estrItemLinkText);
			else
				estrPrintf(&estrFormattedItemName, "%s", estrItemLinkText);

			// create the initial string
			entFormatGameMessageKey(pMemberEnt, &estrNotification, "Team.MasterLooterPromptRoll", STRFMT_STRING("Name", estrLeaderName), STRFMT_STRING("Item", estrFormattedItemName), STRFMT_END);

			// setup the chat data and links
			pChatData = ChatData_CreatePlayerHandleDataFromMessage(estrNotification, estrLeaderName, false, false);

			pItemLinkInfo = item_CreateChatLinkInfoFromMessage(estrNotification, estrItemLinkText, pServerItem);
			if (pItemLinkInfo)
				eaPush(&pChatData->eaLinkInfos, pItemLinkInfo);

			// send the notification
			ClientCmd_NotifySendWithData(pMemberEnt, kNotifyType_TeamLoot, estrNotification, NULL, NULL, NULL, pChatData);

			// cleanup
			StructDestroy(parse_ChatData, pChatData);
			estrDestroy(&estrNotification);
			estrDestroy(&estrItemLinkText);
			estrDestroy(&estrFormattedItemName);
		}

		eaDestroy(&peaOnMapMembers);
		estrDestroy(&estrLeaderName);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("MasterLooterGiveLoot") ACMD_SERVERCMD ACMD_SERVERONLY ACMD_PRIVATE;
void interactloot_MasterLooterGiveLoot(Entity *pLeaderEnt, int itemID, EntityRef uiRecipient)
{
	Team *pTeam = SAFE_GET_REF2(pLeaderEnt, pTeam, hTeam);
	EntityRef *eaiOnMapMembers = NULL;
	Entity *pRecipientEnt = NULL;
	TeamLootEvent *pEvent;
	InventoryBag *pServerBag = NULL;
	Item *pServerItem;
	int iServerItemCount;
	int iPartitionIdx;

	// ensure the leader is still interacting with a loot ent for which we have an event registered
	if (!pLeaderEnt || !pLeaderEnt->pPlayer || !pLeaderEnt->pPlayer->InteractStatus.interactTarget.bLoot)
		return;
	pEvent = interactloot_MasterLooterGetLootEvent(pLeaderEnt, itemID, &pServerBag);
	if (!pEvent)
		return;

	// ensure the team is the same as the one registered to the event
	if (!pTeam || pTeam != GET_REF(pEvent->hTeam))
		return;

	// ensure this command was invoked by the team leader
	if (!pTeam->pLeader || pTeam->pLeader->iEntID != pLeaderEnt->myContainerID)
		return;

	// ensure the loot was dropped in Master Looter mode
	if (pEvent->eLootMode != LootMode_MasterLooter)
		return;

	// ensure assignee is a member of the team that is on the same map
	iPartitionIdx = entGetPartitionIdx(pLeaderEnt);
	team_GetOnMapEntRefs(iPartitionIdx, &eaiOnMapMembers, pTeam);
	if (eaiFind(&eaiOnMapMembers, uiRecipient) > -1)
		pRecipientEnt = entFromEntityRef(iPartitionIdx, uiRecipient);
	eaiDestroy(&eaiOnMapMembers);
	if (!pRecipientEnt)
		return;

	// get the item from the loot bag
	if (!pServerBag)
		return;
	pServerItem = inv_bag_GetItemByID(pServerBag, itemID, NULL, &iServerItemCount);

	// make sure the item doesn't already have a pending transaction on it
	if (pServerItem && !pServerItem->bTransactionPending && iServerItemCount > 0)
	{
		TeamLootGiveItemCBData *pData = StructCreate(parse_TeamLootGiveItemCBData);
		Entity **peaOnMapMembers = NULL;
		char *estrLeaderName = NULL;
		char *estrRecipientName = NULL;
		int i;
		ItemChangeReason reason = {0};
		ItemDef *pItemDef;

		// mark that the item is undergoing a transaction and grant the item
		pServerItem->bTransactionPending = true;

		// initiate the add item transaction
		pData->uiRecipientRef = pRecipientEnt->myRef;
		pData->uiLootEntRef = pEvent->uiLootEnt;
		pData->pchLootInteractName = pEvent->pchLootInteractName;
		pData->itemID = pServerItem->id;
		pData->iPartitionIdx = iPartitionIdx;

		pItemDef = GET_REF(pServerItem->hItem);
		inv_FillItemChangeReason(&reason, pRecipientEnt, "Loot:MasterLooterGive", pItemDef ? pItemDef->pchName : NULL);

		invtransaction_AddItem(pRecipientEnt, InvBagIDs_Inventory, -1, pServerItem, 0, &reason, interactloot_TeamLootGiveItemCallback, pData);

		// send the notification for the grant to all team members
		estrPrintf(&estrLeaderName, "%s@%s", entGetLocalName(pLeaderEnt),
			pLeaderEnt->pPlayer->publicAccountName && pLeaderEnt->pPlayer->publicAccountName[0] ? pLeaderEnt->pPlayer->publicAccountName : pLeaderEnt->pPlayer->privateAccountName);
		estrPrintf(&estrRecipientName, "%s@%s", entGetLocalName(pRecipientEnt),
			pRecipientEnt->pPlayer->publicAccountName && pRecipientEnt->pPlayer->publicAccountName[0] ? pRecipientEnt->pPlayer->publicAccountName : pRecipientEnt->pPlayer->privateAccountName);

		team_GetOnMapEntsUnique(entGetPartitionIdx(pLeaderEnt), &peaOnMapMembers, pTeam, false);
		for (i = 0; i < eaSize(&peaOnMapMembers); i++)
		{
			Entity *pMemberEnt = peaOnMapMembers[i];
			ChatData *pChatData = NULL;
			ChatLinkInfo *pLinkInfo = NULL;
			const char *pchItemName = item_GetName(pServerItem, pMemberEnt);
			char *estrItemLinkText = NULL;
			char *estrFormattedItemName = NULL;
			char *estrNotification = NULL;

			estrPrintf(&estrItemLinkText, "[%s]", pchItemName);

			if (iServerItemCount > 1)
				estrPrintf(&estrFormattedItemName, "%i %s", iServerItemCount, estrItemLinkText);
			else
				estrPrintf(&estrFormattedItemName, "%s", estrItemLinkText);

			// create the initial string
			entFormatGameMessageKey(pMemberEnt, &estrNotification, "Team.MasterLooterAssignItem", STRFMT_STRING("Name", estrLeaderName), STRFMT_STRING("Recipient", estrRecipientName), STRFMT_STRING("Item", estrFormattedItemName), STRFMT_END);

			// setup the chat data and links
			pChatData = ChatData_CreatePlayerHandleDataFromMessage(estrNotification, estrLeaderName, false, false);
			pLinkInfo = ChatData_CreatePlayerHandleLinkInfoFromMessage(estrNotification, estrRecipientName, false, false);
			if (pLinkInfo)
				eaPush(&pChatData->eaLinkInfos, pLinkInfo);

			pLinkInfo = item_CreateChatLinkInfoFromMessage(estrNotification, estrItemLinkText, pServerItem);
			if (pLinkInfo)
				eaPush(&pChatData->eaLinkInfos, pLinkInfo);

			// send the notification
			ClientCmd_NotifySendWithData(pMemberEnt, kNotifyType_TeamLoot, estrNotification, NULL, NULL, NULL, pChatData);

			// cleanup
			StructDestroy(parse_ChatData, pChatData);
			estrDestroy(&estrNotification);
			estrDestroy(&estrItemLinkText);
			estrDestroy(&estrFormattedItemName);
		}

		eaDestroy(&peaOnMapMembers);
		estrDestroy(&estrLeaderName);
		estrDestroy(&estrRecipientName);
	}
}

// ----------------------------------------------------------------------------------
// NEED OR GREED
// ----------------------------------------------------------------------------------

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("NeedOrGreedChoose") ACMD_SERVERCMD ACMD_SERVERONLY ACMD_PRIVATE;
void interactloot_NeedOrGreedChoose(Entity *pPlayerEnt, EntityRef uiLootEnt, const char *pchLootInteractable, U64 id, NeedOrGreedChoice eChoice)
{
	Team *pTeam = SAFE_GET_REF2(pPlayerEnt, pTeam, hTeam);
	TeamLootEvent *pEvent = NULL;
	TeamLootEventItem *pEventItem = NULL;
	int i;
	int iPartitionIdx;
	
	// ensure we know about the team loot event
	if (uiLootEnt && !stashIntFindPointer(s_LootEventFromLootEntRef, uiLootEnt, &pEvent))
		return;

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	if (pchLootInteractable && pchLootInteractable[0])
	{
		TeamLootEvent ***peaEvents = NULL;
		if (!stashFindPointer(s_LootEventFromLootInteractName, pchLootInteractable, (void*) &peaEvents))
			return;
		else
		{
			for (i = 0; i < eaSize(peaEvents); i++)
			{
				if (GET_REF((*peaEvents)[i]->hTeam) == pTeam)
				{
					pEvent = (*peaEvents)[i];
					break;
				}
			}
			if (!pEvent)
				return;
		}
	}

	// ensure the team is the same as the one registered to the event
	if (!pTeam || pTeam != GET_REF(pEvent->hTeam))
		return;

	// ensure the loot event was in Need or Greed mode
	if (pEvent->eLootMode != LootMode_NeedOrGreed)
		return;

	if (eChoice <= NeedOrGreedChoice_None || eChoice >= NeedOrGreedChoice_Count)
		return;

	// find the corresponding item in the event
	for (i = 0; i < eaSize(&pEvent->peaItems); i++)
	{
		if (pEvent->peaItems[i] && pEvent->peaItems[i]->itemID == id)
		{
			pEventItem = pEvent->peaItems[i];
			break;
		}
	}

	// return if the item wasn't found or it was resolved already
	if (!pEventItem || pEventItem->bResolved)
		return;

	// set the player's choice for the item (will not do anything if the player
	// was not part of this event - eg. player joined later)
	for (i = 0; i < eaSize(&pEventItem->peaTeamRolls); i++)
	{
		if (pEventItem->peaTeamRolls[i] && pEventItem->peaTeamRolls[i]->uiEntRef == pPlayerEnt->myRef)
		{
			if (pEventItem->peaTeamRolls[i]->eRollChoice == NeedOrGreedChoice_None)
			{
				Entity *pLootEnt = entFromEntityRef(iPartitionIdx, pEventItem->pParentEvent->uiLootEnt);
				GameInteractable *pLootInteractable = interactable_GetByName(pEventItem->pParentEvent->pchLootInteractName, NULL);
				Entity **peaMembers = NULL;
				InventoryBag** eaLootBags = NULL;
				Item *pItem = NULL;
				int iItemCount = 0;
				char *estrPlayerName = NULL;

				if (pLootEnt)
				{
					if (interaction_IsLootEntity(pLootEnt))
						eaPushEArray(&eaLootBags, &pLootEnt->pCritter->eaLootBags);
					else if (SAFE_MEMBER(pLootEnt->pCritter, encounterData.pLootTracker) && eaSize(&pLootEnt->pCritter->encounterData.pLootTracker->eaLootBags) > 0)
						eaPushEArray(&eaLootBags, &pLootEnt->pCritter->encounterData.pLootTracker->eaLootBags);
				}
				else if (pLootInteractable)
				{
					InteractionLootTracker *pTracker = interactable_GetLootTracker(iPartitionIdx, pLootInteractable, false);
					if (pTracker && eaSize(&pTracker->eaLootBags) > 0)
						eaPushEArray(&eaLootBags, &pTracker->eaLootBags);
				}

				if (eaSize(&eaLootBags) <= 0)
					return;

				FOR_EACH_IN_EARRAY(eaLootBags, InventoryBag, pBag)
				{
					pItem = inv_bag_GetItemByID(pBag, pEventItem->itemID, NULL, &iItemCount);
					if (pItem)
						break;
				}
				FOR_EACH_END

				if (!pItem || iItemCount <= 0)
					return;

				// set the choice on the event item roll
				pEventItem->peaTeamRolls[i]->eRollChoice = eChoice;

				// notify team of the choice
				estrPrintf(&estrPlayerName, "%s@%s", entGetLocalName(pPlayerEnt),
					pPlayerEnt->pPlayer->publicAccountName && pPlayerEnt->pPlayer->publicAccountName[0] ? pPlayerEnt->pPlayer->publicAccountName : pPlayerEnt->pPlayer->privateAccountName);

				for (i = 0; i < eaSize(&pEventItem->peaTeamRolls); i++)
				{
					Entity *pMember = NULL;

					if (pEventItem->peaTeamRolls[i])
						pMember = entFromEntityRef(iPartitionIdx, pEventItem->peaTeamRolls[i]->uiEntRef);
					if (pMember)
						eaPush(&peaMembers, pMember);
				}
				for (i = 0; i < eaSize(&peaMembers); i++)
				{
					Entity *pMemberEnt = peaMembers[i];
					ChatData *pChatData = NULL;
					ChatLinkInfo *pLinkInfo = NULL;
					const char *pchItemName = item_GetName(pItem, pMemberEnt);
					char *estrItemLinkText = NULL;
					char *estrFormattedItemName = NULL;
					char *estrNotification = NULL;

					estrPrintf(&estrItemLinkText, "[%s]", pchItemName);
					if (iItemCount > 1)
						estrPrintf(&estrFormattedItemName, "%i %s", iItemCount, estrItemLinkText);
					else
						estrPrintf(&estrFormattedItemName, "%s", estrItemLinkText);

					// create the initial string
					if (eChoice == NeedOrGreedChoice_Pass)
					{
						entFormatGameMessageKey(pMemberEnt, &estrNotification, "Team.NeedOrGreedPass",
							STRFMT_STRING("Name", estrPlayerName),
							STRFMT_STRING("Item", estrFormattedItemName),
							STRFMT_END);
					}
					else
					{
						entFormatGameMessageKey(pMemberEnt, &estrNotification, "Team.NeedOrGreedChoose",
							STRFMT_STRING("Name", estrPlayerName),
							STRFMT_STRING("Method", entTranslateMessageKey(pMemberEnt, eChoice == NeedOrGreedChoice_Need ? "Team.Need" : "Team.Greed")),
							STRFMT_STRING("Item", estrFormattedItemName),
							STRFMT_END);
					}

					// setup the chat data and links
					pChatData = ChatData_CreatePlayerHandleDataFromMessage(estrNotification, estrPlayerName, false, false);
					pLinkInfo = item_CreateChatLinkInfoFromMessage(estrNotification, estrItemLinkText, pItem);
					if (pLinkInfo)
						eaPush(&pChatData->eaLinkInfos, pLinkInfo);

					// send the notification
					ClientCmd_NotifySendWithData(pMemberEnt, kNotifyType_TeamLoot, estrNotification, NULL, NULL, NULL, pChatData);

					// cleanup
					StructDestroy(parse_ChatData, pChatData);
					estrDestroy(&estrNotification);
					estrDestroy(&estrItemLinkText);
					estrDestroy(&estrFormattedItemName);
				}

				// if all rolls have been made, grant the reward
				interactloot_NeedOrGreedResolveRolls(pEventItem, false);

				// cleanup
				estrDestroy(&estrPlayerName);
				eaDestroy(&peaMembers);
			}
			break;
		}
	}
}
