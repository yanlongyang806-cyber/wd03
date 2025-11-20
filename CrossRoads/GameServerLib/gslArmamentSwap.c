#include "ArmamentSwapCommon.h"

#include "CharacterClass.h"
#include "Entity.h"
#include "Player.h"

#include "ArmamentSwapCommon_h_ast.h"
#include "itemCommon.h"
#include "itemServer.h"
#include "gslPowerTransactions.h"
#include "WorldGrid.h"
#include "gslPvPGame.h"
#include "GameAccountDataCommon.h"
#include "inventoryCommon.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GlobalTypes_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static int s_bArmamentSwapAlways = false;
AUTO_CMD_INT(s_bArmamentSwapAlways, ArmamentSwapAlways);

// ------------------------------------------------------------------------------------------------------------
__forceinline static ArmamentSwapInfo* GetOrCreateArmamentSwapInfo(Entity *pEnt, bool bCreateIfNull)
{
	if (pEnt->pPlayer)
	{
		if (pEnt->pPlayer->pArmamentSwapInfo)
			return pEnt->pPlayer->pArmamentSwapInfo;

		if (bCreateIfNull)
		{
			pEnt->pPlayer->pArmamentSwapInfo = StructCreate(parse_ArmamentSwapInfo);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
			return pEnt->pPlayer->pArmamentSwapInfo;
		}
	}
	return NULL;
}

// ------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(ArmamentSwapClass) ACMD_HIDE ACMD_PRODUCTS(Bronze);
void gslArmamentSwapClass(Entity *pEnt, const char *pchClassName)
{
	if (pEnt)
	{
		CharacterClass *pClass = characterclasses_FindByName((char*)pchClassName);

		// check if we can be this class
		if (pClass)
		{
			ANALYSIS_ASSUME(pClass != NULL);
			if (entity_PlayerCanBecomeClass(pEnt, pClass))
			{
				ArmamentSwapInfo *pArmamentSwap = GetOrCreateArmamentSwapInfo(pEnt, true);

				if (pArmamentSwap)
				{
					SET_HANDLE_FROM_STRING(g_hCharacterClassDict, pchClassName, pArmamentSwap->hClassSwap);
					pArmamentSwap->bHasQueuedArmaments = true;
					entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
				}
			}
		}
	}
}


// ------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(ArmamentSwapClearClass) ACMD_HIDE ACMD_PRODUCTS(Bronze);
void gslArmamentSwapClearClass(Entity *pEnt)
{
	if (pEnt)
	{
		ArmamentSwapInfo *pArmamentSwap = GetOrCreateArmamentSwapInfo(pEnt, true);
		if (!pArmamentSwap)
			return;

		if (REF_HANDLE_IS_ACTIVE(pArmamentSwap->hClassSwap))
		{
			REMOVE_HANDLE(pArmamentSwap->hClassSwap);
			pArmamentSwap->bHasQueuedArmaments = eaSize(&pArmamentSwap->eaActiveItemSwap) > 0;
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------
static S32 gslArmamentSwap_CheckForActiveSwapConflicts(Entity *pEnt, ArmamentSwapInfo *pArmamentSwap, S32 iBagID)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	S32 maxActiveSlots;
	S32 iIndex;
	S32 numSwapsForBag = 0;
	S32 addedQueue = false;

	// get the number of max slots, and if we are swapping less slots than what we have
	// then queue the remaining unslotted 
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iBagID, pExtract);
	if (!pBag)
		return false;
	
	FOR_EACH_IN_EARRAY(pArmamentSwap->eaActiveItemSwap, ArmamentActiveItemSwap, pitem)
	{
		if (pitem->iBagID == iBagID)
			numSwapsForBag++;
	}
	FOR_EACH_END

	maxActiveSlots = invbag_maxActiveSlots(pBag);
	if (numSwapsForBag >= maxActiveSlots)
		return false;
	
	for (iIndex = 0; iIndex < maxActiveSlots; ++iIndex)
	{
		bool bSwappingIndex = false;
		// see if we are swapping this slot
		FOR_EACH_IN_EARRAY(pArmamentSwap->eaActiveItemSwap, ArmamentActiveItemSwap, pitem)
			if (pitem->iBagID == iBagID && pitem->iIndex == iIndex)
			{
				bSwappingIndex = true;
				break;
			}
		FOR_EACH_END

		// we are not swapping anything at this index, see if this swap will conflict with another index
		if (!bSwappingIndex)
		{	
			S32 activeSlot = invbag_GetActiveSlot(pBag, iIndex);
			bool bConflicting = false;
			FOR_EACH_IN_EARRAY(pArmamentSwap->eaActiveItemSwap, ArmamentActiveItemSwap, pitem)
				if (pitem->iBagID == iBagID && pitem->iActiveSlot == activeSlot)
				{	// we are swapping something that is already in another slot
					// find a replacement 
					bConflicting = true;
					break;
				}
			FOR_EACH_END

			if (bConflicting)
			{
				// there is another active weapon that is swapping using this index
				// we need to find a replacement from the ones we have active currently
				S32 j;
				S32 potentialActiveSlot = -1;
				for (j = 0; j < maxActiveSlots; ++j)
				{
					potentialActiveSlot = invbag_GetActiveSlot(pBag, j);

					FOR_EACH_IN_EARRAY(pArmamentSwap->eaActiveItemSwap, ArmamentActiveItemSwap, pitem)
						if (pitem->iBagID == iBagID && pitem->iActiveSlot == potentialActiveSlot)
						{
							potentialActiveSlot = -1;
							break;
						}
					FOR_EACH_END
					if (potentialActiveSlot != -1)
						break;
				}

				if (potentialActiveSlot != -1)
				{
					ArmamentActiveItemSwap *pItemSwap = StructCreate(parse_ArmamentActiveItemSwap);
					if (pItemSwap)
					{
						pItemSwap->iActiveSlot = potentialActiveSlot;
						pItemSwap->iBagID = iBagID;
						pItemSwap->iIndex = iIndex;
						eaPush(&pArmamentSwap->eaActiveItemSwap, pItemSwap);
						addedQueue = true;
					}
				}
			}
		}
	}

	return addedQueue;
}

int cmpArmamentSwapSortByIndex(const ArmamentActiveItemSwap** lhs, const ArmamentActiveItemSwap** rhs)
{
	return (S32)(*lhs)->iIndex - (S32)(*rhs)->iIndex;
}

// ------------------------------------------------------------------------------------------------------------
static void gslArmamentSwap(Entity *pEnt, ArmamentSwapInfo *pArmamentSwap)
{
	// if we have a class to swap, do it
	if (REF_IS_VALID(pArmamentSwap->hClassSwap))
	{
		const char *pchClassString = REF_STRING_FROM_HANDLE(pArmamentSwap->hClassSwap);
		if (pchClassString)
			character_SetClass(pEnt->pChar, pchClassString);
		// clear the handle
		REMOVE_HANDLE(pArmamentSwap->hClassSwap);
	}

	if (eaSize(&pArmamentSwap->eaActiveItemSwap) > 0)
	{
		static S32 *s_ibagsProcessed = NULL;
		IntEarrayWrapper Bags = {0};
		S32 i;
		S32 bAddedSwap = false;
		eaiClear(&s_ibagsProcessed);
		FOR_EACH_IN_EARRAY(pArmamentSwap->eaActiveItemSwap, ArmamentActiveItemSwap, pitem)
			if (eaiFind(&s_ibagsProcessed, pitem->iBagID) == -1)
			{
				bAddedSwap = gslArmamentSwap_CheckForActiveSwapConflicts(pEnt, pArmamentSwap, pitem->iBagID);
				eaiPush(&s_ibagsProcessed, pitem->iBagID);
			}
		FOR_EACH_END

		// Destroy all item swaps that were issued by the client
		if (pEnt->pInventoryV2)
		{
			for (i = eaSize(&pEnt->pInventoryV2->ppSlotSwitchRequest)-1; i >= 0; i--)
			{
				InvBagSlotSwitchRequest* pRequest = pEnt->pInventoryV2->ppSlotSwitchRequest[i];
				if (pRequest->uRequestID)
				{
					eaiPushUnique(&Bags.eaInts, pRequest->eBagID);
					eaRemove(&pEnt->pInventoryV2->ppSlotSwitchRequest, i);
					StructDestroy(parse_InvBagSlotSwitchRequest, pRequest);
				}
			}
		}
		// sort this so we apply the swaps in the index order, this helps so that powers get slotted in order, but seems kinda hacky
		eaQSort(pArmamentSwap->eaActiveItemSwap, cmpArmamentSwapSortByIndex);
	
		// first clear all the slots, then re-equip
		FOR_EACH_IN_EARRAY(pArmamentSwap->eaActiveItemSwap, ArmamentActiveItemSwap, pitem)
			gslBagChangeActiveSlotWhenReady(pEnt, pitem->iBagID, pitem->iIndex, -1, 0, 0, 0.0f);
		FOR_EACH_END

		// go through the active item slots and try equipping them
		FOR_EACH_IN_EARRAY_FORWARDS(pArmamentSwap->eaActiveItemSwap, ArmamentActiveItemSwap, pitem)
			gslBagChangeActiveSlotWhenReady(pEnt, pitem->iBagID, pitem->iIndex, pitem->iActiveSlot, 0, 0, 0.0f);
		FOR_EACH_END

		eaDestroyEx(&pArmamentSwap->eaActiveItemSwap, NULL);

		// Fail all slot switch requests started by the client
		ClientCmd_gclResetActiveSlotsForBags(pEnt, &Bags);
		StructDeInit(parse_IntEarrayWrapper, &Bags);
	}
	
	pArmamentSwap->bHasQueuedArmaments = false;
	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
}

// ------------------------------------------------------------------------------------------------------------
__forceinline static bool gslArmamentSwapCanSwap(Entity *pEnt)
{
	// should probably check if any of the powers are being used from the class given powers
	if (!entIsAlive(pEnt))
		return false;

	if (s_bArmamentSwapAlways)
		return true;

	if (zmapInfoGetMapType(NULL) == ZMTYPE_PVP)
	{
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		return gslPVPGame_CanArmamentSwap(iPartitionIdx);
	}
	
	return true;
}

// ------------------------------------------------------------------------------------------------------------
void gslArmamentSwapUpdate(Entity *pEnt)
{
	// check to see if we have queued
	ArmamentSwapInfo *pArmamentSwap = GetOrCreateArmamentSwapInfo(pEnt, false);
	if (pArmamentSwap && pArmamentSwap->bHasQueuedArmaments)
	{
		// check if we can swap yet
		if (gslArmamentSwapCanSwap(pEnt))
		{
			gslArmamentSwap(pEnt, pArmamentSwap);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------
void gslArmamentSwapOnDeath(Entity *pEnt)
{
	ArmamentSwapInfo *pArmamentSwap = GetOrCreateArmamentSwapInfo(pEnt, false);
	if (pArmamentSwap && pArmamentSwap->bHasQueuedArmaments)
	{
		gslArmamentSwap(pEnt, pArmamentSwap);
	}
}

// ------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(ArmamentSwapClearActiveItemSlots) ACMD_HIDE ACMD_PRODUCTS(Bronze);
void gslArmamentSwapClearActiveItemSlots(Entity *pEnt)
{
	if (pEnt)
	{
		ArmamentSwapInfo *pArmamentSwap = GetOrCreateArmamentSwapInfo(pEnt, true);
		if (!pArmamentSwap)
			return;

		if (eaSize(&pArmamentSwap->eaActiveItemSwap) > 0)
		{
			eaDestroyEx(&pArmamentSwap->eaActiveItemSwap, NULL);
			pArmamentSwap->bHasQueuedArmaments = REF_HANDLE_IS_ACTIVE(pArmamentSwap->hClassSwap);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(ArmamentSwapActiveWeapon) ACMD_HIDE ACMD_PRODUCTS(Bronze);
void gslArmamentSwapActiveItemSlots(Entity *pEnt, S32 iBagID, S32 iIndex, S32 iNewActiveSlot)
{
	if (pEnt)
	{
		ArmamentSwapInfo *pArmamentSwap = GetOrCreateArmamentSwapInfo(pEnt, true);
		ArmamentActiveItemSwap *pItemSwap;
		if (!pArmamentSwap)
			return;

		pItemSwap = findQueuedActiveItemSwap(pArmamentSwap, iBagID, iIndex);
		if (pItemSwap)
		{	// we already have something for this bad/index
			if (pItemSwap->iActiveSlot == iNewActiveSlot)
				return; // no change

			// want to change what active slot
			pItemSwap->iActiveSlot = iNewActiveSlot;
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
			return;
		}

		// check if this is a valid bad, index and activeslot

		pItemSwap = StructCreate(parse_ArmamentActiveItemSwap);
		if (pItemSwap)
		{
			pItemSwap->iActiveSlot = iNewActiveSlot;
			pItemSwap->iBagID = iBagID;
			pItemSwap->iIndex = iIndex;
			eaPush(&pArmamentSwap->eaActiveItemSwap, pItemSwap);
			pArmamentSwap->bHasQueuedArmaments = true;
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}	
	}
}
