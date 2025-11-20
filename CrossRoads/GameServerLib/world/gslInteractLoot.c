/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CombatConfig.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "Player.h"
#include "entCritter.h"
#include "gslInteractable.h"
#include "gslInteraction.h"
#include "gslInteractLoot.h"
#include "gslLogSettings.h"
#include "logging.h"
#include "rand.h"
#include "reward.h"
#include "timedeventqueue.h"
#include "inventoryCommon.h"
#include "itemTransaction.h"
#include "inventoryTransactions.h"

#include "GameStringFormat.h"
#include "NotifyCommon.h"
#include "ChatData.h"

#include "gslInteractLoot_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

StashTable s_LootEventFromLootEntRef;
StashTable s_LootEventFromLootInteractName;

// ----------------------------------------------------------------------------------
// Team Loot
// ----------------------------------------------------------------------------------

static void interactloot_CleanupTeamLootEvent(TeamLootEvent *pEvent)
{
	int i;

	if (!pEvent)
		return;

	// free loot event from stash and its corresponding event queue
	if (s_LootEventFromLootEntRef)
		stashIntRemovePointer(s_LootEventFromLootEntRef, pEvent->uiLootEnt, NULL);
	if (s_LootEventFromLootInteractName)
	{
		TeamLootEvent ***peaEvents;
		if (stashFindPointer(s_LootEventFromLootInteractName, pEvent->pchLootInteractName, (void*) &peaEvents))
		{
			eaFindAndRemove(peaEvents, pEvent);
			if (eaSize(peaEvents) == 0)
			{
				stashRemovePointer(s_LootEventFromLootInteractName, pEvent->pchLootInteractName, NULL);
				eaDestroy(peaEvents);
				free(peaEvents);
			}
		}
	}
	for (i = 0; i < eaSize(&pEvent->peaItems); i++)
		timedeventqueue_Remove("NeedOrGreedEventQueue", pEvent->peaItems[i]);

	// free the loot event
	StructDestroy(parse_TeamLootEvent, pEvent);
}


static NOCONST(InventoryBag) *interactloot_CreateItemBag(const Item *pItem)
{
	NOCONST(InventoryBag) *pBag = CONTAINER_NOCONST(InventoryBag, rewardbag_Create());
	Item *pItemCopy = StructClone(parse_Item, pItem);

	if (pBag && pItemCopy)
	{
		NOCONST(InventorySlot) *pSlot = inv_InventorySlotCreate(0);
		pSlot->pItem = CONTAINER_NOCONST(Item, pItemCopy);
		eaPush(&pBag->ppIndexedInventorySlots, pSlot);
	}

	return pBag;
}

static InventoryBag *interactloot_CopyLootBagForThreshold(const InventoryBag *pLootBag)
{
	InventoryBag *pLootBagCopy = StructClone(parse_InventoryBag, pLootBag);
	if (pLootBagCopy && pLootBag->pRewardBagInfo)
	{
		BagIterator *iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pLootBagCopy));

		for(;!bagiterator_Stopped(iter); bagiterator_Next(iter))
		{
			Item *pItem = (Item*)bagiterator_GetItem(iter);
			if (!pItem)
				continue;
			if (pItem->bUnlootable || pItem->owner || !reward_QualityShouldUseLootMode(item_GetQuality(pItem), pLootBagCopy->pRewardBagInfo->eLootModeThreshold))
			{
				rewardbag_RemoveItem(pLootBagCopy,iter->i_cur,NULL);
			}
		}
		bagiterator_Destroy(iter);
	}

	return pLootBagCopy;
}

bool interactloot_CleanupLootInteraction(Entity *pPlayerEnt, InteractionLootTracker **ppLootTracker, InventoryBag *pLootBag)
{
	bool bFoundLootableItem = false;
	BagIterator *iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pLootBag));

	for(;!bagiterator_Stopped(iter); bagiterator_Next(iter))
	{
		Item *pItem = (Item*)bagiterator_GetItem(iter);

		if (!pItem)
			continue;

		if (!pItem->bUnlootable)
		{
			bFoundLootableItem = true;
			break;
		}
	}
	bagiterator_Destroy(iter);

	if (!bFoundLootableItem)
	{
		if (ppLootTracker)
			LootTracker_Cleanup(ppLootTracker);
		if (pPlayerEnt && !(*ppLootTracker))
			interaction_EndInteractionAndDialog(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, true, false, true);
		return true;
	}
	return false;
}

const char* interactloot_GetLootFXName(S32 iHighestItemQuality, RewardPickupType ePickupType, bool bBagHasMissionItem)
{
	const char* pchFXName = NULL;
	if (bBagHasMissionItem && 
		g_CombatConfig.pInteractionConfig)
	{
		pchFXName = g_CombatConfig.pInteractionConfig->pchCorpseMissionItemLootFX;
	}
	else if (!bBagHasMissionItem && 
		iHighestItemQuality >= 0 && 
		iHighestItemQuality < eaSize(&g_ItemQualities.ppQualities))
	{
		if (ePickupType == kRewardPickupType_Rollover && g_ItemQualities.ppQualities[iHighestItemQuality]->pchRolloverLootFX)
			pchFXName = g_ItemQualities.ppQualities[iHighestItemQuality]->pchRolloverLootFX;
		else
			pchFXName = g_ItemQualities.ppQualities[iHighestItemQuality]->pchLootFX;
	}

	if (pchFXName == NULL && g_CombatConfig.pInteractionConfig)
	{
		// Fall-back FX
		pchFXName = g_CombatConfig.pInteractionConfig->pchCorpseLootFX;
	}
	return pchFXName;
}

// This is for loot entities with loot bags (NOT for interactable entities with reward properties)
// THIS MAY REMOVE A LOOT BAG. DO NOT CALL INSIDE A FORWARD ITERATION.
//Returns true if it destroyed the bag.
bool interactloot_CleanupLootEntBag(Entity *pLootEnt, InventoryBag *pLootBag, Entity* pLootingPlayer)
{
	if (!pLootEnt || !pLootBag)
		return false;

	// if the loot bag was emptied, remove the bag and kill the ent
	if (0==rewardbag_Size(pLootBag))
	{
		bool bRemovedBag = false;

		if (eaFindAndRemove(&((InventoryBag**)pLootEnt->pCritter->eaLootBags), pLootBag) >= 0)
		{
			bRemovedBag = true;
			if (eaSize(&pLootEnt->pCritter->eaLootBags) <= 0)
			{

				reward_SendLootFXMessageToBagOwners(pLootingPlayer, pLootEnt, pLootBag);
				if (gbEnableRewardDataLogging) {
					entLogWithStruct(LOG_REWARDS, pLootEnt, "killing loot ent", pLootBag, parse_InventoryBag);
				}
				entDie(pLootEnt, gConf.lootent_postloot_linger_time, 0, 0, NULL);
			}
		}
		else if (pLootEnt->pCritter && pLootEnt->pCritter->encounterData.pLootTracker)
		{
			if (eaFindAndRemove(&pLootEnt->pCritter->encounterData.pLootTracker->eaLootBags, pLootBag) >= 0)
			{
				bRemovedBag = true;
				reward_SendLootFXMessageToBagOwners(pLootingPlayer, pLootEnt, pLootBag);
				if (gbEnableRewardDataLogging) {
					entLogWithStruct(LOG_REWARDS, pLootEnt, "killing loot ent", pLootBag, parse_InventoryBag);
				}
				entDie(pLootEnt, gConf.lootent_postloot_linger_time, 0, 0, NULL);
			}
		}

		if (bRemovedBag)
		{
			entity_SetDirtyBit(pLootEnt, parse_Critter, pLootEnt->pCritter, false);
			StructDestroy(parse_InventoryBag, pLootBag);
			return true;
		}
	}
	// otherwise, update the owners of the loot critter according to who still owns items on the bag
	else if (pLootEnt->pCritter && (!pLootBag->pRewardBagInfo || pLootBag->pRewardBagInfo->loot_mode != LootMode_FreeForAll))
	{
		ItemQuality eThreshold = pLootBag->pRewardBagInfo ? pLootBag->pRewardBagInfo->eLootModeThreshold : eaSize(&g_ItemQualities.ppQualities);
		int i;

		// optimization: if this is the case of a single round-robin owner, leave the owners untouched
		if ((pLootBag->pRewardBagInfo && pLootBag->pRewardBagInfo->loot_mode != LootMode_RoundRobin) || 
			eaSize(&pLootBag->pRewardBagInfo->peaLootOwners) != 1 ||
			eaiSize(&pLootBag->pRewardBagInfo->peaLootOwners[0]->peaiOwnerIDs) > 1)
		{
			CritterLootOwner **peaNewOwners = NULL;
			CritterLootOwner *pRoundRobinOwner = StructCreate(parse_CritterLootOwner);
			bool bHandledThreshold = false;
			BagIterator *iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pLootBag));
			int iPartitionIdx = entGetPartitionIdx(pLootEnt);

			pRoundRobinOwner->eOwnerType = kRewardOwnerType_Player;
			eaPush(&peaNewOwners, pRoundRobinOwner);

			for(;!bagiterator_Stopped(iter);bagiterator_Next(iter))
			{
				Item *pItem = (Item*)bagiterator_GetItem(iter);

				if (!pItem)
					continue;

				// if owned (should not be above threshold), add the owner ID
				if (!!pItem->owner)
					eaiPushUnique(&pRoundRobinOwner->peaiOwnerIDs, pItem->owner);
				// if unowned and above the loot threshold in a team looting mode, adjust accordingly
				else if (!bHandledThreshold && item_GetQuality(pItem) >= eThreshold && pLootBag->pRewardBagInfo)
				{
					// if something still needs to be looted in master looter, find the previous team leader
					// owner already on the critter and copy it
					if (SAFE_MEMBER(pLootBag->pRewardBagInfo, loot_mode) == LootMode_MasterLooter)
					{
						int j;
						for (j = 0; j < eaSize(&pLootBag->pRewardBagInfo->peaLootOwners); j++)
						{
							if (!pLootBag->pRewardBagInfo->peaLootOwners[j])
								continue;

							if (pLootBag->pRewardBagInfo->peaLootOwners[j]->eOwnerType == kRewardOwnerType_TeamLeader)
							{
								eaPush(&peaNewOwners, StructClone(parse_CritterLootOwner, pLootBag->pRewardBagInfo->peaLootOwners[j]));
								break;
							}
						}
					}
					// if something is still need or greed on the bag and an event doesn't exist (i.e. the timers haven't
					// been triggered), the original loot owners should all still be intact
					else if (SAFE_MEMBER(pLootBag->pRewardBagInfo, loot_mode) == LootMode_NeedOrGreed && !stashIntFindPointer(s_LootEventFromLootEntRef, pLootEnt->myRef, NULL))
					{
						eaDestroyStruct(&peaNewOwners, parse_CritterLootOwner);
						bagiterator_Destroy(iter);
						return false;
					}
					bHandledThreshold = true;
				}
			}
			bagiterator_Destroy(iter);

			//If some players are no longer owners, turn off the glow for them.
			for (i = 0; i < eaSize(&pLootBag->pRewardBagInfo->peaLootOwners); i++)
			{
				//find the player list
				if (pLootBag->pRewardBagInfo->peaLootOwners[i]->eOwnerType == kRewardOwnerType_Player)
				{
					int j;
					for (j = 0; j < eaiSize(&pLootBag->pRewardBagInfo->peaLootOwners[i]->peaiOwnerIDs); j++)
					{
						//get all player IDs that were in the old list but aren't in the new one.
						if (eaiFind(&pRoundRobinOwner->peaiOwnerIDs, pLootBag->pRewardBagInfo->peaLootOwners[i]->peaiOwnerIDs[j]) == -1)
						{
							ClientCmd_SetGlowOnCorpseForLoot(entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pLootBag->pRewardBagInfo->peaLootOwners[i]->peaiOwnerIDs[j]), pLootEnt->myRef, NULL);
						}
					}
				}
			}

			// set the new loot owners
			eaDestroyStruct(&pLootBag->pRewardBagInfo->peaLootOwners, parse_CritterLootOwner);
			eaCopyStructs(&peaNewOwners, &pLootBag->pRewardBagInfo->peaLootOwners, parse_CritterLootOwner);
			entity_SetDirtyBit(pLootEnt, parse_Critter, pLootEnt->pCritter, false);
			entSetActive(pLootEnt);
			eaDestroyStruct(&peaNewOwners, parse_CritterLootOwner);
		}
	}
	return false;
}

static Item* interactloot_RemoveItemFromLootEnt(Entity *pLootEnt, int id, int *iItemCount, Entity* pLootingEntity)
{
	Item *pItem = NULL;

	if (!pLootEnt || !pLootEnt->pCritter)
		return NULL;

	FOR_EACH_IN_EARRAY(pLootEnt->pCritter->eaLootBags, InventoryBag, pBag)
	{
		if(rewardbag_RemoveItemByID(pBag, id, &pItem, iItemCount))
		{
			interactloot_CleanupLootEntBag(pLootEnt, pBag, pLootingEntity);
			break;
		}
	}
	FOR_EACH_END


	return pItem;
}

static Item *interactloot_RemoveItemFromLootTracker(InteractionLootTracker **ppLootTracker, int itemID, int *iItemCount)
{
	InteractionLootTracker *pLootTracker = ppLootTracker ? *ppLootTracker : NULL;
	InventoryBag *pLootBag = NULL;
	Item *pItem = NULL;
	int i;

	if (pLootTracker)
	{
		// remove the item from all bags on the tracker (they should be kept in-sync with each other)
		for (i = 0; i < eaSize(&pLootTracker->eaLootBags); i++)
		{
			pLootBag = pLootTracker->eaLootBags[i];
			assert(pLootBag->pRewardBagInfo && pLootBag->pRewardBagInfo->PickupType == kRewardPickupType_Clickable);

			// count items and then remove the stack from the bag
			if (!pItem)
			{
				rewardbag_RemoveItemByID(pLootBag, itemID, &pItem, iItemCount);
			}
			else
				rewardbag_RemoveItemByID(pLootBag, itemID, NULL, NULL);
		}
	}

	interactloot_CleanupLootInteraction(NULL, ppLootTracker, pLootBag);

	return pItem;
}

static Item *interactloot_LootTrackerItemSetExemptFlag(InteractionLootTracker **ppLootTracker, int id)
{
	InteractionLootTracker *pLootTracker = ppLootTracker ? *ppLootTracker : NULL;
	InventoryBag *pLootBag = NULL;
	NOCONST(Item) *pItem = NULL;
	int i;

	if (pLootTracker)
	{
		for (i = 0; i < eaSize(&pLootTracker->eaLootBags); i++)
		{
			pLootBag = pLootTracker->eaLootBags[i];
			assert(pLootBag->pRewardBagInfo && pLootBag->pRewardBagInfo->PickupType == kRewardPickupType_Clickable);

			pItem = inv_bag_trh_GetItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(InventoryBag, (pLootBag)), id, NULL, NULL);
			if (!pItem)
				return NULL;
			
			pItem->bExemptFromLootMode = true;
			pItem->bUnlootable = false;
		}
	}

	return (Item*)pItem;
}
void interactloot_TeamLootGiveItemCallback(TransactionReturnVal *returnVal, TeamLootGiveItemCBData *pData)
{
	if (pData)
	{
		Entity *pLootEnt = pData->uiLootEntRef ? entFromEntityRef(pData->iPartitionIdx, pData->uiLootEntRef) : NULL;
		GameInteractable *pLootInteractable = EMPTY_TO_NULL(pData->pchLootInteractName) ? interactable_GetByName(pData->pchLootInteractName, NULL) : NULL;
		InteractionLootTracker **ppLootTracker = NULL;
		Entity *pRecipientEnt = entFromEntityRef(pData->iPartitionIdx, pData->uiRecipientRef);
		Item *pItem = NULL;
		int iPartitionIdx = pData->iPartitionIdx;

		// always remove the item from the bag
		if (pLootEnt)
		{
			if (interaction_IsLootEntity(pLootEnt))
				pItem = interactloot_RemoveItemFromLootEnt(pLootEnt, pData->itemID, NULL, pRecipientEnt);
			else if (SAFE_MEMBER(pLootEnt->pCritter, encounterData.pLootTracker))
				ppLootTracker = &pLootEnt->pCritter->encounterData.pLootTracker;
		}
		else if (pLootInteractable) 
			ppLootTracker = interactable_GetLootTrackerAddress(iPartitionIdx, pLootInteractable);

		if (!pItem && ppLootTracker)
			pItem = interactloot_RemoveItemFromLootTracker(ppLootTracker, pData->itemID, NULL);

		// deal with transaction failures
		if (returnVal->eOutcome == TRANSACTION_OUTCOME_FAILURE)
		{
			InventoryBag *pEntBag;
			Vec3 vPos;

			// clear pending flag
			if (pItem)
				pItem->bTransactionPending = false;

			if (pRecipientEnt)
			{
				ItemChangeReason reason = {0};

				// tell the recipient that his inventory is full
				ClientCmd_NotifySend(pRecipientEnt, kNotifyType_InventoryFull, entTranslateMessageKey(pRecipientEnt, INVENTORY_FULL_MSG), NULL, NULL);

				// put the item in a separate loot bag specifically for the recipient so they can
				// pick it up when their inventory has space
				pEntBag = (InventoryBag*) interactloot_CreateItemBag(pItem);
				entGetPos(pRecipientEnt, vPos);
				inv_FillItemChangeReason(&reason, pRecipientEnt, "Loot:TeamLootGiveItem", NULL);
				reward_GiveBag(pRecipientEnt, pEntBag, vPos, false, &reason);
			}
		}

		// destroy the structure
		StructDestroy(parse_TeamLootGiveItemCBData, pData);
		StructDestroy(parse_Item, pItem);
	}
}

void interactloot_NeedOrGreedResolveRolls(TeamLootEventItem *pEventItem, bool bForce)
{
	Entity **peaQualifyingEnts = NULL;
	Entity **peaMembers = NULL;
	NeedOrGreedChoice ePriorityChoice = NeedOrGreedChoice_None;
	Entity *pLootEnt = NULL;
	GameInteractable *pLootInteractable = NULL;
	InventoryBag** eaLootBags = NULL;
	InventoryBag* pLootBag = NULL;
	Item *pItem = NULL;
	int i, iItemCount = 0;
	Team* pTeam;
	Entity *cur_ent = NULL;
	int iPartitionIdx;
	bool bRemoveItem = true;
	TeamLootEvent* pEvent = pEventItem->pParentEvent;

	if (!pEventItem || !pEventItem->pParentEvent || pEventItem->bResolved)
		return;
	// mark event item as resolved when resolution is being forced so that
	// errors don't cause memory leaks
	else if (bForce)
		pEventItem->bResolved = true;

	iPartitionIdx = pEventItem->pParentEvent->iPartitionIdx;
	cur_ent = entFromEntityRef(iPartitionIdx, pEventItem->pParentEvent->uiInitiator);
	if(!cur_ent)
	{
		// need an ent for the partition
		return;
	}

	pTeam = GET_REF(pEventItem->pParentEvent->hTeam);
	// get the item and ensure transaction isn't already pending on the item
	if (pEventItem->pParentEvent->uiLootEnt)
	{
		pLootEnt = entFromEntityRef(iPartitionIdx, pEventItem->pParentEvent->uiLootEnt);
		if (interaction_IsLootEntity(pLootEnt))
			eaPushEArray(&eaLootBags, &pLootEnt->pCritter->eaLootBags);
		else if (pTeam && SAFE_MEMBER2(pLootEnt, pCritter, encounterData.pLootTracker) && eaSize(&pLootEnt->pCritter->encounterData.pLootTracker->eaLootBags) > 0)
			LootTracker_FindBagsForTeamID(pLootEnt->pCritter->encounterData.pLootTracker, pTeam->iContainerID, &eaLootBags);
	}
	else if (pTeam && pEventItem->pParentEvent->pchLootInteractName)
	{
		InteractionLootTracker *pLootTracker;

		pLootInteractable = interactable_GetByName(pEventItem->pParentEvent->pchLootInteractName, NULL);
		pLootTracker = interactable_GetLootTracker(iPartitionIdx, pLootInteractable, false);
		LootTracker_FindBagsForTeamID(pLootTracker, pTeam->iContainerID, &eaLootBags);
	}
	if (eaSize(&eaLootBags) <= 0)
		return;

	FOR_EACH_IN_EARRAY(eaLootBags, InventoryBag, pBag)
	{
		pItem = inv_bag_GetItemByID(pBag, pEventItem->itemID, NULL, &iItemCount);
		if (pItem)
		{
			pLootBag = pBag;
			break;
		}
	}
	FOR_EACH_END

	if (!pItem || pItem->bTransactionPending || iItemCount <= 0)
		return;

	// look at all of the rolls on the item so far
	for (i = 0; i < eaSize(&pEventItem->peaTeamRolls); i++)
	{
		TeamLootEventItemRoll *pItemRoll = pEventItem->peaTeamRolls[i];
		Entity *pEnt = pItemRoll ? entFromEntityRef(iPartitionIdx, pItemRoll->uiEntRef) : NULL;

		// skip entries for which the entity could not be found
		if (!pEnt)
			continue;
		if (pItemRoll->eRollChoice == NeedOrGreedChoice_None && !bForce)
		{
			eaDestroy(&peaQualifyingEnts);
			return;
		}

		// if a higher-priority roll is found, reset the qualifying recipients
		if (pItemRoll->eRollChoice > ePriorityChoice)
		{
			eaClear(&peaQualifyingEnts);
			ePriorityChoice = pItemRoll->eRollChoice;
			eaPush(&peaQualifyingEnts, pEnt);
		}
		// ...otherwise, just add to the list of recipients that rolled the current
		// high-priority roll
		else if (pItemRoll->eRollChoice == ePriorityChoice)
			eaPush(&peaQualifyingEnts, pEnt);
	}

	// gather all ents involved in the roll
	for (i = 0; i < eaSize(&pEventItem->peaTeamRolls); i++)
	{
		Entity *pMember = NULL;

		if (pEventItem->peaTeamRolls[i])
			pMember = entFromEntityRef(iPartitionIdx, pEventItem->peaTeamRolls[i]->uiEntRef);
		if (pMember)
			eaPush(&peaMembers, pMember);
	}

	// by this time, we are going to resolve this event item, so we ensure nothing can resolve this item again
	pEventItem->bResolved = true;

	// if someone won...
	if (ePriorityChoice > NeedOrGreedChoice_Pass && eaSize(&peaQualifyingEnts) > 0)
	{
		TeamLootGiveItemCBData *pData = StructCreate(parse_TeamLootGiveItemCBData);
		Entity *pWinner;
		char *estrWinnerName = NULL;
		int *peaiMemberRolls = NULL;
		int iWinningIdx = 0, iWinningRoll = 0;
		ItemChangeReason reason = {0};
		ItemDef *pItemDef;

		// determine a winner
		eaRandomize(&peaQualifyingEnts);
		for (i = 0; i < eaSize(&peaQualifyingEnts); i++)
		{
			int iRoll = randomIntRange(1, 100);

			// in case of collision, randomly determine which way to walk
			if (eaiPushUnique(&peaiMemberRolls, iRoll) < i)
			{
				// NOTE: THIS WILL NOT TERMINATE IF TEAMS SOMEHOW EXCEED 100 MEMBERS!
				bool bWalkUp = (iRoll == 1 || iRoll < 100 && !!randomIntRange(0, 1));
				while (eaiPushUnique(&peaiMemberRolls, bWalkUp ? ++iRoll : --iRoll) < i)
				{
					// reverse direction if roll reaches boundaries
					if (iRoll <= 1 || iRoll >= 100)
						bWalkUp = !bWalkUp;
				}
			}
		}
		assert(eaiSize(&peaiMemberRolls) == eaSize(&peaQualifyingEnts));
		for (i = 0; i < eaSize(&peaQualifyingEnts); i++)
		{
			if (peaiMemberRolls[i] > iWinningRoll)
			{
				iWinningIdx = i;
				iWinningRoll = peaiMemberRolls[i];
			}
		}
		pWinner = peaQualifyingEnts[iWinningIdx];

		//remove the item from the loot bag for god's sake
		rewardbag_RemoveItemByID(pLootBag, pEventItem->itemID, &pItem, NULL);

		// mark that the item is undergoing a transaction and grant the item
		pItem->bTransactionPending = true;


		// initiate the add item transaction
		pData->iPartitionIdx = iPartitionIdx;
		pData->uiRecipientRef = pWinner->myRef;
		pData->uiLootEntRef = pEventItem->pParentEvent->uiLootEnt;
		pData->pchLootInteractName = pEventItem->pParentEvent->pchLootInteractName;
		pData->itemID = pEventItem->itemID;

		pItemDef = GET_REF(pItem->hItem);
		inv_FillItemChangeReason(&reason, pWinner, "Loot:NeedOrGreed", pItemDef->pchName);

		invtransaction_AddItem(pWinner, InvBagIDs_None, -1, pItem, gConf.bKeepLootsOnCorpses ? 0 : ItemAdd_UseOverflow, &reason, interactloot_TeamLootGiveItemCallback, pData);

		// notify team of winner
		estrPrintf(&estrWinnerName, "%s@%s", entGetLocalName(pWinner),
			pWinner->pPlayer->publicAccountName && pWinner->pPlayer->publicAccountName[0] ? pWinner->pPlayer->publicAccountName : pWinner->pPlayer->privateAccountName);

		for (i = 0; i < eaSize(&peaMembers); i++)
		{
			Entity *pMemberEnt = peaMembers[i];
			ChatData *pChatData = NULL;
			ChatLinkInfo *pLinkInfo = NULL;
			const char *pchItemName = item_GetName(pItem,pMemberEnt);
			char *estrItemLinkText = NULL;
			char *estrFormattedItemName = NULL;
			char *estrNotification = NULL;
			char *estrWinnerID = NULL;
			int j;

 			estrPrintf(&estrItemLinkText, "[%s]", pchItemName);
			if (iItemCount > 1)
				estrPrintf(&estrFormattedItemName, "%i %s", iItemCount, estrItemLinkText);
			else
				estrPrintf(&estrFormattedItemName, "%s", estrItemLinkText);

			// create the initial string
			entFormatGameMessageKey(pMemberEnt, &estrNotification, "Team.NeedOrGreedWinItem",
				STRFMT_STRING("Name", estrWinnerName),
				STRFMT_STRING("Item", estrFormattedItemName),
				STRFMT_STRING("Method", entTranslateMessageKey(pMemberEnt, ePriorityChoice == NeedOrGreedChoice_Need ? "Team.Need" : "Team.Greed")), 
				STRFMT_INT("Roll", iWinningRoll),
				STRFMT_END);

			// setup the chat data and links
			pChatData = ChatData_CreatePlayerHandleDataFromMessage(estrNotification, estrWinnerName, false, false);
			pLinkInfo = item_CreateChatLinkInfoFromMessage(estrNotification, estrItemLinkText, pItem);
			if (pLinkInfo)
				eaPush(&pChatData->eaLinkInfos, pLinkInfo);

			// append other qualifying rolls
			if (eaSize(&peaQualifyingEnts) > 1)
			{
				bool bFirstName = true;

				estrConcatf(&estrNotification, " %s ", entTranslateMessageKey(pMemberEnt, "Team.NeedOrGreedOtherRolls"));
				for (j = 0; j < eaSize(&peaQualifyingEnts); j++)
				{
					if (peaQualifyingEnts[j] != pWinner)
					{
						char *estrMemberName = NULL;
						estrPrintf(&estrMemberName, "%s@%s", entGetLocalName(peaQualifyingEnts[j]),
							peaQualifyingEnts[j]->pPlayer->publicAccountName && peaQualifyingEnts[j]->pPlayer->publicAccountName[0] ? peaQualifyingEnts[j]->pPlayer->publicAccountName : peaQualifyingEnts[j]->pPlayer->privateAccountName);
						estrConcatf(&estrNotification, "%s%s(%i)", bFirstName ? "" : ", ", estrMemberName, peaiMemberRolls[j]);
						pLinkInfo = ChatData_CreatePlayerHandleLinkInfoFromMessage(estrNotification, estrMemberName, false, false);
						if (pLinkInfo)
							eaPush(&pChatData->eaLinkInfos, pLinkInfo);
						estrDestroy(&estrMemberName);
						bFirstName = false;
					}
				}
			}

			estrPrintf(&estrWinnerID, "%i", pWinner->myRef);
			// send the notification
			ClientCmd_NotifySendWithData(pMemberEnt, kNotifyType_TeamLoot, estrNotification, estrWinnerID, NULL, NULL, pChatData);
		
			//Also send fancier "You win!" notifications.
 			if (pMemberEnt == pWinner && RefSystem_IsReferentStringValid(gMessageDict, "Team.NeedOrGreedYouWonItem"))
			{
				NeedOrGreedChoice eMyMethod = NeedOrGreedChoice_None;
				//have to find this player's roll choice and result
				for (j = 0; j < eaSize(&pEventItem->peaTeamRolls); j++)
				{
					if (pEventItem->peaTeamRolls[j]->uiEntRef == pMemberEnt->myRef)
					{
						eMyMethod = pEventItem->peaTeamRolls[j]->eRollChoice;
						break;
					}
				}
				estrClear(&estrNotification);
				entFormatGameMessageKey(pMemberEnt, &estrNotification, "Team.NeedOrGreedYouWonItem",
					STRFMT_STRING("Item", estrFormattedItemName),
					STRFMT_STRING("Method", entTranslateMessageKey(pMemberEnt, ePriorityChoice == NeedOrGreedChoice_Need ? "Team.Need" : "Team.Greed")), 
					STRFMT_INT("Roll", iWinningRoll),
					STRFMT_END);
				
				ClientCmd_NotifySendWithItemID(pMemberEnt, kNotifyType_TeamLootResult, estrNotification, REF_STRING_FROM_HANDLE(pItem->hItem), entTranslateMessageKey(pMemberEnt, ePriorityChoice == NeedOrGreedChoice_Need ? "Team.NeedIcon" : "Team.GreedIcon") , 1, 1);
			}
			else if (RefSystem_IsReferentStringValid(gMessageDict, "Team.NeedOrGreedOtherWonItem"))
			{
				int iMyRoll = 0;
				NeedOrGreedChoice eMyMethod = NeedOrGreedChoice_None;
				//have to find this player's roll choice and result
				for (j = 0; j < eaSize(&peaQualifyingEnts); j++)
				{
					if (peaQualifyingEnts[j] == pMemberEnt)
					{
						iMyRoll = peaiMemberRolls[j];
						break;
					}
				}
				for (j = 0; j < eaSize(&pEventItem->peaTeamRolls); j++)
				{
					if (pEventItem->peaTeamRolls[j]->uiEntRef == pMemberEnt->myRef)
					{
						eMyMethod = pEventItem->peaTeamRolls[j]->eRollChoice;
						break;
					}
				}
				estrClear(&estrNotification);
				entFormatGameMessageKey(pMemberEnt, &estrNotification, "Team.NeedOrGreedOtherWonItem",
					STRFMT_STRING("Name", estrWinnerName),
					STRFMT_STRING("Item", estrFormattedItemName),
					STRFMT_STRING("Method", entTranslateMessageKey(pMemberEnt, ePriorityChoice == NeedOrGreedChoice_Need ? "Team.Need" : "Team.Greed")), 
					STRFMT_STRING("MyMethod", entTranslateMessageKey(pMemberEnt, eMyMethod == NeedOrGreedChoice_Need ? "Team.YouNeeded" : (eMyMethod == NeedOrGreedChoice_Greed ? "Team.YouGreeded" : "Team.YouPassed"))), 
					STRFMT_INT("Roll", iWinningRoll),
					STRFMT_INT("MyRoll", iMyRoll),
					STRFMT_END);

				ClientCmd_NotifySend(pMemberEnt, kNotifyType_TeamLootResult, estrNotification, REF_STRING_FROM_HANDLE(pItem->hItem), entTranslateMessageKey(pMemberEnt, ePriorityChoice == NeedOrGreedChoice_Need ? "Team.NeedIcon" : "Team.GreedIcon"));
			}
			// cleanup
			StructDestroy(parse_ChatData, pChatData);
			estrDestroy(&estrNotification);
			estrDestroy(&estrWinnerID);
			estrDestroy(&estrItemLinkText);
			estrDestroy(&estrFormattedItemName);
		}
		//Set interactable on cooldown here.
		if (gConf.bEnableLootModesForInteractables && pLootInteractable)
		{
			for (i = eaSize(&pEventItem->pParentEvent->peaItems) - 1; i >= 0; i--)
			{
				if (!pEventItem->pParentEvent->peaItems[i]->bResolved)
					break;
			}
			if (i < 0)
			{
				for (i = 0; i < eaSize(&peaMembers); i++)
				{
					Entity *pMemberEnt = peaMembers[i];
					interaction_SetCooldownForPlayer(pMemberEnt, pLootInteractable, pEvent->idxEntry, iPartitionIdx);
				}
				interaction_LootResolved(pLootInteractable, pEvent->idxEntry, iPartitionIdx);
			}
		}
		// log the winner
		if (pLootEnt && gbEnableRewardDataLogging) {
			entLog(LOG_REWARDS, pLootEnt, "NeedOrGreed", "naming %s as winner", SAFE_MEMBER2(pWinner, pSaved, savedName));
		}

		estrDestroy(&estrWinnerName);
		eaiDestroy(&peaiMemberRolls);
	}
	// ...otherwise, if everyone either passed or abstained (which is treated the same as passing)
	else
	{		
		InteractionLootTracker **ppLootTracker = NULL;
		CritterLootOwner **peaOwners = NULL;

		// get all team member container IDs
		eaPush(&peaOwners, StructCreate(parse_CritterLootOwner));
		peaOwners[0]->eOwnerType = kRewardOwnerType_Player;
		for (i = 0; i < eaSize(&pEventItem->peaTeamRolls); i++)
		{
			if (pEventItem->peaTeamRolls[i])
			{
				Entity *pTeamMemberEnt = entFromEntityRef(iPartitionIdx, pEventItem->peaTeamRolls[i]->uiEntRef);
				if (pTeamMemberEnt)
					eaiPush(&peaOwners[0]->peaiOwnerIDs, pTeamMemberEnt->myContainerID);
			}
		}

		// remove item from bag and cleanup if necessary
		pItem = NULL;
		if (pLootEnt)
		{
			if (gConf.bKeepLootsOnCorpses)
			{
				CritterLootOwner* pExistingPlayerList = NULL;
				S32 iHighestItemQuality = 0;
				bool bBagsHaveMissionItem = false;

				FOR_EACH_IN_EARRAY(pLootEnt->pCritter->eaLootBags, InventoryBag, pBag)
				{
					bool bMissionItem = false;
					pItem = (Item*)inv_bag_trh_GetItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(InventoryBag, pBag), pEventItem->itemID, NULL, NULL);
					iHighestItemQuality = max(iHighestItemQuality, invbag_GetHighestItemQuality(pBag, &bMissionItem));
					bBagsHaveMissionItem |= bMissionItem;
					if (!pItem)
						continue;

					for (i = 0; i < eaSize(&pBag->pRewardBagInfo->peaLootOwners); i++)
					{
						if (pBag->pRewardBagInfo->peaLootOwners[i]->eOwnerType == kRewardOwnerType_Player)
						{
							pExistingPlayerList = pBag->pRewardBagInfo->peaLootOwners[i];
						}
					}

					pItem->bExemptFromLootMode = true;
				}
				FOR_EACH_END
				
				//Repopulate loot owners.
				if (peaOwners[0]->eOwnerType == kRewardOwnerType_Player)
				{
					S32 itEntity;
					Entity *pRewardOwnerEnt;
					
					for (itEntity = 0; itEntity < eaiSize(&peaOwners[0]->peaiOwnerIDs); itEntity++)
					{
						pRewardOwnerEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, peaOwners[0]->peaiOwnerIDs[itEntity]);
						if (pRewardOwnerEnt)
						{
							const char* pchNewFX = interactloot_GetLootFXName(iHighestItemQuality, pLootBag->pRewardBagInfo->PickupType, bBagsHaveMissionItem);
							eaiPush(&pExistingPlayerList->peaiOwnerIDs, peaOwners[0]->peaiOwnerIDs[itEntity]);
							ClientCmd_SetGlowOnCorpseForLoot(pRewardOwnerEnt, pLootEnt->myRef, pchNewFX);
						}
					}			
				}
				entity_SetDirtyBit(pLootEnt, parse_Critter, pLootEnt->pCritter, false);
				bRemoveItem = false;
			}
			else
			{
				if (interaction_IsLootEntity(pLootEnt))
					pItem = interactloot_RemoveItemFromLootEnt(pLootEnt, pEventItem->itemID, &iItemCount, entFromEntityRef(iPartitionIdx, pEventItem->peaTeamRolls[0]->uiEntRef));
				else if (SAFE_MEMBER(pLootEnt->pCritter, encounterData.pLootTracker))
					ppLootTracker = &pLootEnt->pCritter->encounterData.pLootTracker;	
			}
		}
		else if (pLootInteractable)
		{
			ppLootTracker = interactable_GetLootTrackerAddress(iPartitionIdx, pLootInteractable);
			if (gConf.bLootBagsRemainOnInteractablesUntilEmpty)
			{
				pItem = interactloot_LootTrackerItemSetExemptFlag(ppLootTracker, pEventItem->itemID);
				bRemoveItem = false;
			}
			else
			{
				if (ppLootTracker)
					pItem = interactloot_RemoveItemFromLootTracker(ppLootTracker, pEventItem->itemID, &iItemCount);
			}
		}

 		if (pItem && iItemCount > 0)
		{
			// if everyone passed or declined to roll, give the item out as free-for-all
			if (pLootEnt && !gConf.bKeepLootsOnCorpses)
			{
				NOCONST(InventoryBag) *pBag = interactloot_CreateItemBag(pItem);
				pBag->pRewardBagInfo->loot_mode = LootMode_FreeForAll;
				pBag->pRewardBagInfo->LaunchType = kRRewardLaunchType_Scatter;
				// TODO (JDJ): determine whether we want some other behavior for interactables; currently, the item is
				// just lost forever if everyone passes
				if (pLootEnt)
				{
					Vec3 vDropPos;
					entGetPos(pLootEnt, vDropPos);
					reward_GiveInteractible(iPartitionIdx, peaOwners, (InventoryBag*) pBag, vDropPos);
				}
			}
			// notify team that everyone passed

			// notify team that everyone passed
			for (i = 0; i < eaSize(&peaMembers); i++)
			{
				Entity *pMemberEnt = peaMembers[i];
				ChatData *pChatData = NULL;
				ChatLinkInfo *pLinkInfo = NULL;
				const char *pchItemName = item_GetName(pItem,pMemberEnt);
				char *estrItemLinkText = NULL;
				char *estrFormattedItemName = NULL;
				char *estrNotification = NULL;

				estrPrintf(&estrItemLinkText, "[%s]", pchItemName);
				if (iItemCount > 1)
					estrPrintf(&estrFormattedItemName, "%i %s", iItemCount, estrItemLinkText);
				else
					estrPrintf(&estrFormattedItemName, "%s", estrItemLinkText);
			
				// create the initial string
				entFormatGameMessageKey(pMemberEnt, &estrNotification, "Team.NeedOrGreedPassAll", STRFMT_STRING("Item", estrFormattedItemName), STRFMT_END);

				// setup the chat data and link
				pChatData = StructCreate(parse_ChatData);
				pLinkInfo = item_CreateChatLinkInfoFromMessage(estrNotification, estrItemLinkText, pItem);
				if (pLinkInfo)
					eaPush(&pChatData->eaLinkInfos, pLinkInfo);

				// send the notification
				ClientCmd_NotifySendWithData(pMemberEnt, kNotifyType_TeamLoot, estrNotification, NULL, NULL, NULL, pChatData);
				
				if (RefSystem_IsReferentStringValid(gMessageDict, "Team.NeedOrGreedPassAllWithFormatting"))
				{
					estrClear(&estrNotification);
					entFormatGameMessageKey(pMemberEnt, &estrNotification, "Team.NeedOrGreedPassAllWithFormatting",
						STRFMT_STRING("Item", estrFormattedItemName),
						STRFMT_END);

					ClientCmd_NotifySend(pMemberEnt, kNotifyType_TeamLootResult, estrNotification, REF_STRING_FROM_HANDLE(pItem->hItem), entTranslateMessageKey(pMemberEnt, "Team.PassIcon"));
				}

				// cleanup
				StructDestroy(parse_ChatData, pChatData);
				estrDestroy(&estrNotification);
				estrDestroy(&estrItemLinkText);
				estrDestroy(&estrFormattedItemName);
			}
		}
		eaDestroyStruct(&peaOwners, parse_CritterLootOwner);
		if (bRemoveItem)
			StructDestroy(parse_Item, pItem);
	}

	// check all other event items in the parent event for resolution; if all have
	// been resolved, remove the event from the stash
	for (i = eaSize(&pEventItem->pParentEvent->peaItems) - 1; i >= 0; i--)
	{
		if (!pEventItem->pParentEvent->peaItems[i]->bResolved)
			break;
	}
	if (i < 0)
	{
		interactloot_CleanupTeamLootEvent(pEventItem->pParentEvent);
		if(interaction_IsLootEntity(pLootEnt) && pLootBag)
		{
			interactloot_CleanupLootEntBag(pLootEnt, pLootBag, NULL);
		}
	}

	eaDestroy(&peaMembers);
	eaDestroy(&peaQualifyingEnts);
}

static void interactloot_NeedOrGreedTimeout(TeamLootEventItem *pEventItem)
{
	// force the item to be granted
	interactloot_NeedOrGreedResolveRolls(pEventItem, true);
}

static bool interactloot_NeedOrGreedBeginInteract(Entity *pPlayerEnt, Team *pTeam, InventoryBag *pLootBag, Entity *pTargetEnt, GameInteractable *pTargetInteractable, TeamLootEvent *pEvent)
{
	InventoryBag *pLootBagCopy = NULL;
	InteractionLootTracker *pLootTracker = NULL;
	Critter *pLootCritter = SAFE_MEMBER(pTargetEnt, pCritter);
	Entity **peaTeamMembers = NULL;
	BagIterator *iter;
	int i, j;
	int iMaxDelay = 0;
	int iPartitionIdx;

	if (!pPlayerEnt || !pTeam || !pLootBag || !pEvent)
		return false;

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	// generate list of "team members" by just using all owners on the loot critter
	// to ensure everyone that got kill credit on the original team gets to roll (regardless of whether
	// they leave the team afterward or not)
	assert(!pLootCritter || !interaction_IsLootEntity(pTargetEnt) ||
		(eaSize(&pLootBag->pRewardBagInfo->peaLootOwners) == 1 && pLootBag->pRewardBagInfo->peaLootOwners[0]->eOwnerType == kRewardOwnerType_Player));
	if (pLootCritter && interaction_IsLootEntity(pTargetEnt))
	{
		for (i = 0; i < eaiSize(&pLootBag->pRewardBagInfo->peaLootOwners[0]->peaiOwnerIDs); i++)
		{
			Entity *pMember = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pLootBag->pRewardBagInfo->peaLootOwners[0]->peaiOwnerIDs[i]);
			if (pMember)
				eaPushUnique(&peaTeamMembers, pMember);
		}
	}
	else
		team_GetOnMapEntsUnique(iPartitionIdx, &peaTeamMembers, pTeam, false);
	if (!peaTeamMembers)
		return false;

	// get loot tracker
	if (SAFE_MEMBER(pLootCritter, encounterData.pLootTracker))
		pLootTracker = pLootCritter->encounterData.pLootTracker;
	else if (pTargetInteractable)
		pLootTracker = interactable_GetLootTracker(iPartitionIdx, pTargetInteractable, false);

	// add TeamLootEventItem for each item in the bag to hold team member roll data
	pLootBagCopy = interactloot_CopyLootBagForThreshold(pLootBag);
	iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pLootBagCopy));
	for(;!bagiterator_Stopped(iter); bagiterator_Next(iter))
	{
		Item *pItem = (Item*)bagiterator_GetItem(iter);
		TeamLootEventItem *pEventItem = NULL;
		ItemQuality eQuality;
		U32 uiDecisionTime;

		if (!pItem || pItem->bExemptFromLootMode)
			continue;

		pEventItem = StructCreate(parse_TeamLootEventItem);
		pEventItem->pParentEvent = pEvent;
		pEventItem->itemID = pItem->id;
		eaPush(&pEvent->peaItems, pEventItem);

		// add roll data for each team member
		for (j = 0; j < eaSize(&peaTeamMembers); j++)
		{
			TeamLootEventItemRoll *pMemberRoll = NULL;

			if (!peaTeamMembers[j])
				continue;

			pMemberRoll = StructCreate(parse_TeamLootEventItemRoll);
			pMemberRoll->uiEntRef = peaTeamMembers[j]->myRef;
			pMemberRoll->eRollChoice = NeedOrGreedChoice_None;
			eaPush(&pEventItem->peaTeamRolls, pMemberRoll);
		}

		eQuality = item_GetQuality(pItem);
		if (eQuality <= kItemQuality_None || eQuality >= eaSize(&g_ItemQualities.ppQualities))
			eQuality = 0;

		// add one second to the decision time to give some leeway for net lag
		iMaxDelay = MAX(iMaxDelay, g_ItemQualities.ppQualities[eQuality]->iNeedBeforeGreedDelay + 1);
		uiDecisionTime = timeSecondsSince2000() + g_ItemQualities.ppQualities[eQuality]->iNeedBeforeGreedDelay + 1;

		// create a timed event for each item
		if (!timedeventqueue_Exists("NeedOrGreedEventQueue"))
			timedeventqueue_Create("NeedOrGreedEventQueue", interactloot_NeedOrGreedTimeout, NULL);
		timedeventqueue_Set("NeedOrGreedEventQueue", pEventItem, uiDecisionTime);   

		// mark the item as unlootable on all bags on the loot tracker
		if (pLootTracker)
		{
			for (j = 0; j < eaSize(&pLootTracker->eaLootBags); j++)
			{
				InventoryBag *pTempBag = pLootTracker->eaLootBags[j];
				Item *pTempItem = inv_bag_GetItemByID(pTempBag, pItem->id, NULL, NULL); 
				if (pTempItem)
					pTempItem->bUnlootable = true;
			}
		}
	}
	bagiterator_Destroy(iter);

	if (!pEvent->peaItems)
	{
		StructDestroy(parse_InventoryBag, pLootBagCopy);
		eaDestroy(&peaTeamMembers);
		return false;
	}

	// open need or greed UI on all team members
	for (i = 0; i < eaSize(&peaTeamMembers); i++)
	{
		if (peaTeamMembers[i])
			ClientCmd_NeedOrGreedInteract(peaTeamMembers[i], pTargetEnt ? pTargetEnt->myRef : 0, SAFE_MEMBER(pTargetInteractable, pcName), pLootBagCopy);
	}
	StructDestroy(parse_InventoryBag, pLootBagCopy);

	// ensure critter lingers around a little longer than the longest need or greed item delay
	if (pLootCritter)
		pLootCritter->timeToLinger = pLootCritter->StartingTimeToLinger = (MAX(pLootCritter->StartingTimeToLinger, iMaxDelay) + 0.01);

	// cleanup
	eaDestroy(&peaTeamMembers);

	return true;
}

static bool interactloot_MasterLooterBeginInteract(Entity *pPlayerEnt, Team *pTeam, InventoryBag *pLootBag, Entity *pTargetEnt, GameInteractable *pTargetInteractable)
{
	InventoryBag *pLootBagCopy = NULL;
	Entity *pLeader = team_GetTeamLeader(entGetPartitionIdx(pPlayerEnt), pTeam);
	Entity **peaTeamMembers = NULL;
	char *estrLeaderName = NULL;
	int i;
	BagIterator *iter;

	// if the team leader could not be found, log an error and convert the bag mode to RoundRobin
	if (!pLeader || !pLeader->pPlayer)
	{
		if (pTargetEnt)
			entLog(LOG_GSL, pTargetEnt, "Interact", "couldn't get leader for this team to give reward to (team %i)", pTeam ? pTeam->iContainerID : 0);
		// TODO (JDJ): is this likely?  we have to update owners in this case, too
		//if (SAFE_MEMBER(pLootBag, pRewardBagInfo))
		//	pLootBag->pRewardBagInfo->loot_mode = LootMode_RoundRobin;
		return false;
	}
	// fail if interaction initiator was not the leader
	else if (pLeader->myContainerID != pPlayerEnt->myContainerID)
		return false;
	
	// fail if nothing exists in the bag that's over the threshold
	pLootBagCopy = interactloot_CopyLootBagForThreshold(pLootBag);
	iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pLootBagCopy));
	if (bagiterator_Stopped(iter))
	{
		StructDestroy(parse_InventoryBag, pLootBagCopy);
		bagiterator_Destroy(iter);
		return false;
	}
	bagiterator_Destroy(iter);
	iter = NULL;

	estrPrintf(&estrLeaderName, "%s@%s", entGetLocalName(pLeader),
		pLeader->pPlayer->publicAccountName && pLeader->pPlayer->publicAccountName[0] ? pLeader->pPlayer->publicAccountName : pLeader->pPlayer->privateAccountName);		

	// send notification to all team members of all items in bag
	team_GetOnMapEntsUnique(entGetPartitionIdx(pPlayerEnt), &peaTeamMembers, pTeam, false);
	for (i = 0; i < eaSize(&peaTeamMembers); i++)
	{
		Entity *pMemberEnt = peaTeamMembers[i];
		ChatData *pChatData = NULL;
		char *estrNotification = NULL;
		char *delim = "";

		if (!pMemberEnt)
			continue;

		entFormatGameMessageKey(pMemberEnt, &estrNotification, "Team.MasterLooterItemList", STRFMT_STRING("Name", estrLeaderName), STRFMT_END);
		pChatData = ChatData_CreatePlayerHandleDataFromMessage(estrNotification, estrLeaderName, false, false);
		iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pLootBagCopy));
		for(;!bagiterator_Stopped(iter);bagiterator_Next(iter))
		{
			Item *pItem = (Item*)bagiterator_GetItem(iter);
			if (pItem)
			{
				ChatLink *pItemLink;
				ChatLinkInfo *pItemLinkInfo = StructCreate(parse_ChatLinkInfo);
				const char *pchItemName = item_GetName(pItem, pMemberEnt);
				char *estrEncodedItem = NULL;
				int iItemCount = bagiterator_GetItemCount(iter);
				char pchCount[16] = "";

				if (iItemCount > 1)
					sprintf(pchCount,"%i%c", iItemCount, 'x');

				// add item count descriptors and comma delimiters
				estrConcatf(&estrNotification, "%s%s", delim, pchCount[0] ? pchCount : "");
				delim = ",";

				// setup the chat link info
				ParserWriteText(&estrEncodedItem, parse_Item, pItem, WRITETEXTFLAG_USEHTMLACCESSLEVEL |WRITETEXTFLAG_DONTWRITEENDTOKENIFNOTHINGELSEWRITTEN, 0, 0);
				pItemLink = ChatData_CreateLink(kChatLinkType_Item, estrEncodedItem, pchItemName);
				pItemLinkInfo->pLink = pItemLink;
				pItemLinkInfo->iStart = estrLength(&estrNotification);
				eaPush(&pChatData->eaLinkInfos, pItemLinkInfo);

				// add the item string to the notification
				estrConcatf(&estrNotification, "[%s]", pchItemName);
				pItemLinkInfo->iLength = estrLength(&estrNotification) - pItemLinkInfo->iStart;

				// cleanup
				estrDestroy(&estrEncodedItem);
			}
		}
		bagiterator_Destroy(iter);

		ClientCmd_NotifySendWithData(pMemberEnt, kNotifyType_TeamLoot, estrNotification, NULL, NULL, NULL, pChatData);

		StructDestroy(parse_ChatData, pChatData);
		estrDestroy(&estrNotification);
	}

	estrDestroy(&estrLeaderName);
	eaDestroy(&peaTeamMembers);

	ClientCmd_MasterLooterInteract(pLeader, pTargetEnt ? pTargetEnt->myRef : 0, SAFE_MEMBER(pTargetInteractable, pcName), pLootBagCopy);
	StructDestroy(parse_InventoryBag, pLootBagCopy);

	return true;
}

// This function is called at the beginning of interaction with the loot ent.  It
// calls the appropriate loot interaction code depending on the loot mode and whether
// the looter is on a team or not
void interactloot_PerformInteract(Entity *pPlayerEnt, Entity *pTargetEnt, GameInteractable *pTargetInteractable, InventoryBag** eaLootBags)
{
	// ensure the loot interaction hasn't already been processed
	// TODO (abrady): this check needs to handle pRewardBag->OwnerType
	if (pPlayerEnt && (pTargetEnt || pTargetInteractable) && eaSize(&eaLootBags) > 0)
	{
		InteractTarget *pTarget = pPlayerEnt->pPlayer ? &pPlayerEnt->pPlayer->InteractStatus.interactTarget : NULL;
        Team *pTeam = pPlayerEnt->pTeam ? GET_REF(pPlayerEnt->pTeam->hTeam) : NULL;
		TeamLootEvent *pExistingEvent = NULL;
		bool bInteractSuccess = false;

		if (!pTarget || pTarget->bLoot)
			return;

		// mark the interaction target as loot
		pTarget->bLoot = true;

		// find an existing team loot event
		if (s_LootEventFromLootEntRef && pTargetEnt)
			stashIntFindPointer(s_LootEventFromLootEntRef, pTargetEnt->myRef, &pExistingEvent);
		else if (s_LootEventFromLootInteractName && pTargetInteractable && pTeam)
		{
			TeamLootEvent ***peaEvents = NULL;
			if (stashFindPointer(s_LootEventFromLootInteractName, pTargetInteractable->pcName, (void*) &peaEvents))
			{
				int i;
				for (i = 0; i < eaSize(peaEvents); i++)
				{
					if (GET_REF((*peaEvents)[i]->hTeam) == pTeam)
					{
						pExistingEvent = (*peaEvents)[i];
						break;
					}
				}
			}
		}

		// create a new team loot event
		if (!pExistingEvent)
		{
			FOR_EACH_IN_EARRAY(eaLootBags, InventoryBag, pBag)
			{
				bool bItemsLooted = false;
				if (pTeam && (pBag->pRewardBagInfo->loot_mode > LootMode_FreeForAll))
				{
					TeamLootEvent *pEvent = StructCreate(parse_TeamLootEvent);

					pEvent->iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
					pEvent->uiInitiator = pPlayerEnt->myRef;
					pEvent->uiLootEnt = pTargetEnt ? pTargetEnt->myRef : 0;
					pEvent->pchLootInteractName = SAFE_MEMBER(pTargetInteractable, pcName);
					pEvent->eLootMode = pBag->pRewardBagInfo->loot_mode;
					pEvent->idxEntry = pPlayerEnt->pPlayer ? pPlayerEnt->pPlayer->InteractStatus.interactTarget.iInteractionIndex : -1;
					COPY_HANDLE(pEvent->hTeam, pPlayerEnt->pTeam->hTeam);

					pExistingEvent = pEvent;
					// loot-mode specific behaviors
					switch (pBag->pRewardBagInfo->loot_mode)
					{
						xcase LootMode_NeedOrGreed:
							bItemsLooted = interactloot_NeedOrGreedBeginInteract(pPlayerEnt, pTeam, pBag, pTargetEnt, pTargetInteractable, pExistingEvent);
						xcase LootMode_MasterLooter:
							bItemsLooted = interactloot_MasterLooterBeginInteract(pPlayerEnt, pTeam, pBag, pTargetEnt, pTargetInteractable);
						xdefault:
							Errorf("unknown loot mode %i", pBag->pRewardBagInfo->loot_mode);
					}
					if (bItemsLooted)
					{
						// register the event with our global team loot event stashes
						if (pTargetEnt)
						{
							if (!s_LootEventFromLootEntRef)
								s_LootEventFromLootEntRef = stashTableCreateInt(50);
							stashIntAddPointer(s_LootEventFromLootEntRef, pTargetEnt->myRef, pEvent, false);

							// update ownership on the loot ent
							if (interaction_IsLootEntity(pTargetEnt))
								interactloot_CleanupLootEntBag(pTargetEnt, pBag, pPlayerEnt);
						}
						else if (pTargetInteractable)
						{
							TeamLootEvent ***peaEvents = NULL;
							if (!s_LootEventFromLootInteractName)
								s_LootEventFromLootInteractName = stashTableCreateWithStringKeys(50, StashDefault);
							if (!stashFindPointer(s_LootEventFromLootInteractName, pTargetInteractable->pcName, (void*) &peaEvents))
							{
								peaEvents = calloc(1, sizeof(*peaEvents));
								stashAddPointer(s_LootEventFromLootInteractName, pTargetInteractable->pcName, peaEvents, false);
							}
							eaPush(peaEvents, pEvent);
						}
						bInteractSuccess = true;
					}
				}
			}
			FOR_EACH_END
			if (!bInteractSuccess)
				StructDestroy(parse_TeamLootEvent, pExistingEvent);
		}
		// always attempt single-player looting to loot below-threshold items
		bInteractSuccess = loot_InteractBeginMultiBags(pPlayerEnt, &eaLootBags, false, false, (pTargetEnt && pTargetEnt->pCritter && pTargetEnt->pCritter->bAutoLootMe)) || bInteractSuccess;

		// end interaction if nothing turns out to be lootable (this case should not actually be hit
		// unless in need or greed mode with no available round-robin items because of loot ownership
		// updating)
		if (!bInteractSuccess)
			interaction_DoneInteracting(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, false, false);
		else if (pTargetEnt && pTargetEnt->pCritter)
		{
			pTargetEnt->pCritter->encounterData.iPlayerOwnerID = pPlayerEnt->myContainerID;

			// Add the player to the list of interacting players
			ea32PushUnique(&pTargetEnt->pCritter->encounterData.perInteractingPlayers, pPlayerEnt->myContainerID);
		}

		//Auto-loot check
		if(entity_ShouldAutoLootTarget(pPlayerEnt, pTargetEnt) && bInteractSuccess)
		{
			loot_InteractTakeAll(pPlayerEnt,InvBagIDs_None, true);
		}
	}
}

// This function is called at the beginning of interaction with the loot ent.  It
// calls the appropriate loot interaction code depending on the loot mode and whether
// the looter is on a team or not
void interactloot_PerformLootEntInteract(Entity *pPlayerEnt, Entity *pTargetEnt)
{
	bool bEndInteraction = true;
	// ensure the loot interaction hasn't already been processed
	// TODO (abrady): this check needs to handle pRewardBag->OwnerType
	if (pPlayerEnt && pTargetEnt)
	{
		InteractTarget *pTarget = pPlayerEnt->pPlayer ? &pPlayerEnt->pPlayer->InteractStatus.interactTarget : NULL;
		Team *pTeam = pPlayerEnt->pTeam ? GET_REF(pPlayerEnt->pTeam->hTeam) : NULL;
		TeamLootEvent *pExistingEvent = NULL;
		bool bInteractSuccess = false;
		InventoryBag** eaBags = NULL;

		if (!pTarget || pTarget->bLoot)
			return;

		// mark the interaction target as loot
		pTarget->bLoot = true;

		// find an existing team loot event
		if (s_LootEventFromLootEntRef && pTargetEnt)
			stashIntFindPointer(s_LootEventFromLootEntRef, pTargetEnt->myRef, &pExistingEvent);

		eaPushEArray(&eaBags, &pTargetEnt->pCritter->eaLootBags);
		// create a new team loot event
		if (!pExistingEvent)
		{
			FOR_EACH_IN_EARRAY(eaBags, InventoryBag, pBag)
			{
				bool bItemsLooted = false;
				if (pTeam && (pBag->pRewardBagInfo->loot_mode > LootMode_FreeForAll))
				{
					TeamLootEvent *pEvent = StructCreate(parse_TeamLootEvent);

					pEvent->iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
					pEvent->uiInitiator = pPlayerEnt->myRef;
					pEvent->uiLootEnt = pTargetEnt ? pTargetEnt->myRef : 0;
					pEvent->eLootMode = pBag->pRewardBagInfo->loot_mode;
					pEvent->idxEntry = pPlayerEnt->pPlayer ? pPlayerEnt->pPlayer->InteractStatus.interactTarget.iInteractionIndex : -1;
					COPY_HANDLE(pEvent->hTeam, pPlayerEnt->pTeam->hTeam);
					pExistingEvent = pEvent;
					// loot-mode specific behaviors
					switch (pBag->pRewardBagInfo->loot_mode)
					{
						xcase LootMode_NeedOrGreed:
							bItemsLooted = interactloot_NeedOrGreedBeginInteract(pPlayerEnt, pTeam, pBag, pTargetEnt, NULL, pExistingEvent) || bInteractSuccess;
						xcase LootMode_MasterLooter:
							bItemsLooted = interactloot_MasterLooterBeginInteract(pPlayerEnt, pTeam, pBag, pTargetEnt, NULL) || bInteractSuccess;
							bEndInteraction = false;
						xdefault:
							Errorf("unknown loot mode %i", pBag->pRewardBagInfo->loot_mode);
					}
					if (bItemsLooted)
					{
						// register the event with our global team loot event stashes
						if (pTargetEnt)
						{
							if (!s_LootEventFromLootEntRef)
								s_LootEventFromLootEntRef = stashTableCreateInt(50);
							stashIntAddPointer(s_LootEventFromLootEntRef, pTargetEnt->myRef, pExistingEvent, false);

							// update ownership on the loot ent
							if (interaction_IsLootEntity(pTargetEnt))
								interactloot_CleanupLootEntBag(pTargetEnt, pBag, pPlayerEnt);
						}
						bInteractSuccess = true;
					}
				}
			}
			FOR_EACH_END
			if (!bInteractSuccess)
				StructDestroy(parse_TeamLootEvent, pExistingEvent);
		}

		// always attempt single-player looting to loot below-threshold items
		bInteractSuccess = loot_InteractBeginMultiBags(pPlayerEnt, &eaBags, false, false, (pTargetEnt && pTargetEnt->pCritter && pTargetEnt->pCritter->bAutoLootMe)) || bInteractSuccess;

		// end interaction if nothing turns out to be lootable (this case should not actually be hit
		// unless in need or greed mode with no available round-robin items because of loot ownership
		// updating)
		if (!bInteractSuccess)
			interaction_DoneInteracting(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, false, false);
		else if (pTargetEnt && pTargetEnt->pCritter)
		{
			pTargetEnt->pCritter->encounterData.iPlayerOwnerID = pPlayerEnt->myContainerID;

			// Add the player to the list of interacting players
			ea32PushUnique(&pTargetEnt->pCritter->encounterData.perInteractingPlayers, pPlayerEnt->myContainerID);
		}

		//Auto-loot check
		if(entity_ShouldAutoLootTarget(pPlayerEnt, pTargetEnt) && bInteractSuccess)
		{
			loot_InteractTakeAll(pPlayerEnt,InvBagIDs_None, bEndInteraction);
		}
		eaDestroy(&eaBags);
	}
}

// This function is called whenever a player's interaction with a loot target ends; this includes
// a player disconnecting/switching gameservers or the loot entity dying (which actually should not
// happen if a player is interacting with it)
void interactloot_EndInteract(Entity *pPlayerEnt, EntityRef uiLootEntRef, const char *pchLootInteractableName)
{
	if (s_LootEventFromLootEntRef && uiLootEntRef)
	{
		TeamLootEvent *pEvent = NULL;
		stashIntFindPointer(s_LootEventFromLootEntRef, uiLootEntRef, &pEvent);
		if (pEvent && pEvent->eLootMode == LootMode_MasterLooter && pEvent->uiInitiator == pPlayerEnt->myRef)
			interactloot_CleanupTeamLootEvent(pEvent);
	}
	else if (s_LootEventFromLootInteractName && pchLootInteractableName)
	{
		TeamLootEvent ***peaEvents = NULL;
		TeamLootEvent *pEvent = NULL;
		if (stashFindPointer(s_LootEventFromLootInteractName, pchLootInteractableName, (void*) &peaEvents))
		{
			int i;
			for (i = eaSize(peaEvents) - 1; i >= 0; i--)
			{
				pEvent = (*peaEvents)[i];
				if (pEvent && pEvent->eLootMode == LootMode_MasterLooter && pEvent->uiInitiator == pPlayerEnt->myRef)
					interactloot_CleanupTeamLootEvent((*peaEvents)[i]);
			}
		}
	}
}

// Returns TRUE if the loot tracker can be looted by the specified player
bool LootTracker_CanEntityLoot(InteractionLootTracker *pLootTracker, Entity *pPlayerEnt)
{
	int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	ContainerID uiTeamID = team_GetTeamID(pPlayerEnt);
	Team *pTeam = team_GetTeam(pPlayerEnt);
	Entity *pLeaderEnt = team_GetTeamLeader(iPartitionIdx, pTeam);
	bool bLootableItem = false;
	bool bHaveTeamOwnedBag = false;

	if (pLootTracker)
	{
		int i;
		for (i = 0; i < eaSize(&pLootTracker->eaLootBags); i++)
		{
			InventoryBag *pLootBag = pLootTracker->eaLootBags[i];
			RewardBagInfo *pRewardBagInfo = pLootBag->pRewardBagInfo;
			BagIterator *iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pLootBag));

			// skip empty bags
			if (!pLootBag || bagiterator_Stopped(iter))
			{
				bagiterator_Destroy(iter);
				continue;
			}

			// check if your team owns a bag; if so, you have to find an unowned item or one that
			// was assigned to you
			if (pLootBag->uiTeamOwner == uiTeamID)
				bHaveTeamOwnedBag = true;

			for(;!bagiterator_Stopped(iter); bagiterator_Next(iter))
			{
				Item *pItem = (Item*)bagiterator_GetItem(iter);

				if (!pItem)
					continue;
				if (!pItem->bUnlootable)
				{
					bLootableItem = true;

					// if this bag is your team's bag...
					if (pLootBag->uiTeamOwner == uiTeamID || pLootBag->pRewardBagInfo->OwnerType == kRewardOwnerType_Player)
					{
						// master looter bags
						if (pRewardBagInfo && pRewardBagInfo->loot_mode == LootMode_MasterLooter)
						{
							if (reward_QualityShouldUseLootMode(item_GetQuality(pItem), pRewardBagInfo->eLootModeThreshold))
							{
								if (pPlayerEnt == pLeaderEnt)
								{
									bagiterator_Destroy(iter);
									return true;
								}
							}
							else if (!pItem->owner || pItem->owner == pPlayerEnt->myContainerID)
							{
								bagiterator_Destroy(iter);
								return true;
							}
						}
						// all other bags
						else if (!pItem->owner || pItem->owner == pPlayerEnt->myContainerID)
						{
							bagiterator_Destroy(iter);
							return true;
						}
					}
				}
			}
			bagiterator_Destroy(iter);
		}

		// if you have no team-owned bag (or if you're not on a team), you can
		// loot the bag as long as there exists one item that is not unlootable
		// (since it will be copied into a new bag for you)
		return (!uiTeamID || !bHaveTeamOwnedBag) && bLootableItem;
	}

	return false;
}

// Returns the loot bag on the loot tracker that can be looted by the specified player
bool LootTracker_FindOwnedLootBags(InteractionLootTracker *pLootTracker, Entity *pPlayerEnt, InventoryBag*** peaBagsOut)
{
	Team *pTeam = team_GetTeam(pPlayerEnt);
	int i;
	bool retVal = false;

	eaClear(peaBagsOut);

	if (pLootTracker)
	{
		for (i = 0; i < eaSize(&pLootTracker->eaLootBags); i++)
		{
			InventoryBag *pLootBag = pLootTracker->eaLootBags[i];

			assert(pLootBag->pRewardBagInfo);

			// ignore non-clickable loot bags
			if (pLootBag->pRewardBagInfo->PickupType != kRewardPickupType_Clickable)
				continue;

			if (pLootBag->pRewardBagInfo->OwnerType != kRewardOwnerType_Team)
			{
				retVal = true;
				eaPush(peaBagsOut, pLootBag);
			}
			else if (!pLootBag->uiTeamOwner && (!pTeam || pTeam->loot_mode == LootMode_FreeForAll))
			{
				retVal = true;
				eaPush(peaBagsOut, pLootBag);
			}
			else if (pTeam && pLootBag->uiTeamOwner == pTeam->iContainerID)
			{
				retVal = true;
				eaPush(peaBagsOut, pLootBag);
			}
		}
	}
	return retVal;
}

bool LootTracker_FindBagsForTeamID(InteractionLootTracker *pLootTracker, U32 uiTeamID, InventoryBag*** peaBagsOut)
{
	int i;
	bool retVal = false;
	for (i = 0; i < eaSize(&pLootTracker->eaLootBags); i++)
	{
		InventoryBag *pBag = pLootTracker->eaLootBags[i];
		if (pBag->uiTeamOwner == uiTeamID)
		{
			retVal = true;
			eaPush(peaBagsOut, pBag);
		}
	}
	return retVal;
}

void LootTracker_RemoveItemFromAllBags(InteractionLootTracker *pLootTracker, U64 iID)
{
	int i;
	for (i = 0; i < eaSize(&pLootTracker->eaLootBags); i++)
	{
		InventoryBag *pBag = pLootTracker->eaLootBags[i];
		assert(pBag->pRewardBagInfo && pBag->pRewardBagInfo->PickupType == kRewardPickupType_Clickable);
		rewardbag_RemoveItemByID(pBag, iID, NULL, NULL);
	}
}

// This function cleans out a loot tracker's empty bags.
void LootTracker_Cleanup(InteractionLootTracker **ppTracker)
{
	InteractionLootTracker *pTracker;
	int i;

	// remove all bags from the tracker that have no unlootable items
	if (!ppTracker || !*ppTracker)
		return;
	pTracker = *ppTracker;

	for (i = eaSize(&pTracker->eaLootBags) - 1; i >= 0; i--)
	{
		InventoryBag *pLootBag = pTracker->eaLootBags[i];

		if (!pLootBag)
			continue;

		// if bag has no items, remove it
		if (inv_bag_BagEmpty(pLootBag))
		{
			eaRemove(&pTracker->eaLootBags, i);
			StructDestroy(parse_InventoryBag, pLootBag);
		}
	}

	if (eaSize(&pTracker->eaLootBags) == 0)
		StructDestroySafe(parse_InteractionLootTracker, ppTracker);
}


#include "gslInteractLoot_h_ast.c"
