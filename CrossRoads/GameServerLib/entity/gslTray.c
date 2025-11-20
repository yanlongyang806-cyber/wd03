/***************************************************************************
 *     Copyright (c) 2009, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/

#include "AutoTransDefs.h"
#include "Character.h"
#include "Character_h_ast.h"
#include "CharacterClass.h"
#include "CombatConfig.h"
#include "earray.h"
#include "Entity.h"
#include "EntityLib.h"
#include "Entity_h_ast.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "GameAccountDataCommon.h"
#include "gslEntity.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "LocalTransactionManager.h"
#include "objTransactions.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "Powers.h"
#include "PowerTree.h"
#include "RegionRules.h"
#include "SavedPetCommon.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "TransactionSystem.h"
#include "Tray.h"
#include "Tray_h_ast.h"
#include "qsortG.h"

#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Destroys the element in the tray at the slot
static bool entity_TrayElemDestroy(Entity* pEnt, int iTray, int iSlot)
{
	SavedTray* pTray = entity_GetActiveTray(pEnt);
	TrayElem* pElem = entity_TrayGetTrayElem(pEnt, iTray, iSlot);
	if(pElem && pTray)
	{
		eaFindAndRemoveFast(&pTray->ppTrayElems, pElem);
		StructDestroy(parse_TrayElem, pElem);
		entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
		return true;
	}
	return false;
}

// TrayElemDestroy <Tray> <Slot>: Destroys the element in the tray at the slot
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_SERVERCMD ACMD_HIDE;
void TrayElemDestroy(Entity *e, int iTray, int iSlot)
{
	entity_TrayElemDestroy(e, iTray, iSlot);
}

// TrayElemDestroy <Tray> <Slot>: Destroys the element in the tray at the slot
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_SERVERCMD ACMD_HIDE;
void TrayDestroy(Entity *e, int iTray)
{
	SavedTray* pTray = entity_GetActiveTray(e);
	int i;
	if (!pTray)
		return;
	for(i=eaSize(&pTray->ppTrayElems)-1; i>=0; i--)
	{
		if(pTray->ppTrayElems[i]->iTray==iTray)
		{
			TrayElem* pElem = pTray->ppTrayElems[i];
			if(pElem && pTray)
			{
				eaFindAndRemoveFast(&pTray->ppTrayElems, pElem);
				StructDestroy(parse_TrayElem, pElem);
				entity_SetDirtyBit(e, parse_SavedEntityData, e->pSaved, false);
			}
		}
	}
}


// Moves the element in the tray at the slot to the new location.
//  Performs a swap if the new location is not empty.
static bool entity_TrayElemMove(Entity* pEnt, int iTray, int iSlot, int iTrayNew, int iSlotNew)
{
	if((iTray!=iTrayNew || iSlot!=iSlotNew) && TRAYSLOT_VALID(iTray,iSlot) && TRAYSLOT_VALID(iTrayNew,iSlotNew))
	{
		bool bDirty = false;
		TrayElem *pelem = entity_TrayGetTrayElem(pEnt, iTray, iSlot);
		TrayElem *pelemTarget = entity_TrayGetTrayElem(pEnt, iTrayNew, iSlotNew);
		if(pelem)
		{
			pelem->iTray = iTrayNew;
			pelem->iTraySlot = iSlotNew;
			bDirty = true;
		}
		if(pelemTarget)
		{
			pelemTarget->iTray = iTray;
			pelemTarget->iTraySlot = iSlot;
			bDirty = true;
		}
		if (bDirty)
		{
			entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
		}
		return true;
	}
	return false;
}

// TrayElemMove <Tray> <Slot> <NewTray> <NewSlot>: Moves the element in the tray at the slot to the new location.
//  Performs a swap if the new location is not empty.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void TrayElemMove(Entity *e, int iTray, int iSlot, int iTrayNew, int iSlotNew)
{
	entity_TrayElemMove(e, iTray, iSlot, iTrayNew, iSlotNew);
}

static int cmpCooldowns(const Power **left, const Power **right)
{
	F32 cooldown_left = 0;
	F32 cooldown_right = 0;

	PowerDef* powerLeft = GET_REF((*left)->hDef);
	PowerDef* powerRight = GET_REF((*right)->hDef);

	if (!powerLeft) return 1;
	if (!powerRight) return -1;
	return powerLeft->fTimeRecharge < powerRight->fTimeRecharge ? -1 : 1;
}

static void entity_TrayElemSortCooldown(Entity* pEnt)
{
	SavedTray* pTray = entity_GetActiveTray(pEnt);
	if (pTray)
	{
		int i, j;
		Power** eaSortPowers = NULL;
		for (i=eaSize(&pTray->ppTrayElems)-1; i>=0; i--)
		{
			if (pTray->ppTrayElems[i]->iTray==0)
			{
				Power* pPower = entity_TrayGetPower(pEnt, pTray->ppTrayElems[i]);
				if (pPower)
				{
					eaPush(&eaSortPowers, pPower);
				}
			}
		}
		if (eaSortPowers)
		{
			eaQSort(eaSortPowers, cmpCooldowns);

			for (i=eaSize(&pTray->ppTrayElems)-1; i>=0; i--)
			{
				if (pTray->ppTrayElems[i]->iTray==0)
				{
					Power* pPower = entity_TrayGetPower(pEnt, pTray->ppTrayElems[i]);
					for (j=eaSize(&eaSortPowers)-1; j>=0; j--)
					{
						if (pPower == eaSortPowers[j])
						{
							entity_TrayElemMove(pEnt, 0, pTray->ppTrayElems[i]->iTraySlot, 0, j); 
							break;
						}
					}
				}
			}
			entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
			eaDestroy(&eaSortPowers);
		}
	}
}

//Sorts the contents of the entity's Tray 0 by their base cooldown timers.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Interface);
void SortTrayByCooldown(Entity *e)
{
	entity_TrayElemSortCooldown(e);

}

static bool gslDefaultTray_GetTraySlot(Entity* pEnt,
									   DefaultTray* pDefTray, 
									   DefaultTrayElemDef* pDefElem, 
									   S32 iSlotOffset, 
									   TraySlot* pDefaultSlot,
									   GameAccountDataExtract *pExtract)
{
	S32 iSlotRemainder;
	if (!pDefTray)
	{
		return false;
	}
	pDefaultSlot->bAutoAttack = pDefElem->bAutoAttack;
	if (!pDefElem->pchRelativeTo)
	{
		pDefaultSlot->iTray = pDefElem->iTray;
		pDefaultSlot->iSlot = pDefElem->iSlot;
	}
	else
	{
		S32 i;
		for (i = eaSize(&pDefTray->ppTrayElems)-1; i >= 0; i--)
		{
			DefaultTrayElemDef* pCheckElem = pDefTray->ppTrayElems[i];
			if (pDefElem->pchRelativeTo == pCheckElem->pchName)
			{
				pDefaultSlot->iTray = pCheckElem->iTray;
				if (pCheckElem->bSlotEntireBag)
				{
					S32 iBagSize = inv_ent_GetMaxSlots(pEnt, pCheckElem->eBagID, pExtract);
					if (iBagSize > 0)
					{
						pDefaultSlot->iSlot = pCheckElem->iSlot+iBagSize;
					}
					else
					{
						pDefaultSlot->iSlot = pCheckElem->iSlot+1;
					}
				}
				else
				{
					pDefaultSlot->iSlot = pCheckElem->iSlot+1;
				}
				break;
			}
		}
		if (i < 0)
		{
			DefaultTray* pParent = pDefTray->pParent;
			return gslDefaultTray_GetTraySlot(pEnt,pParent,pDefElem,iSlotOffset,pDefaultSlot,pExtract);
		}
	}
	pDefaultSlot->iSlot += iSlotOffset;
	while ((iSlotRemainder = TRAY_SIZE_MAX_CONFIG - pDefaultSlot->iSlot) <= 0)
	{
		pDefaultSlot->iTray++;
		pDefaultSlot->iSlot = -iSlotRemainder;
	}
	return true;
}

// Gets the default tray and slot for a particular element, returns true on success.
static bool entity_TrayGetDefaultTraySlot(Entity *e, TrayElem *pElem, TraySlot* pDefaultSlot, GameAccountDataExtract *pExtract)
{
	bool bAutoAttack = false;
	S32 i, iTray = -1, iSlot = -1, iRecurseCount = 100;
	CharacterClass* pClass = e->pChar ? character_GetClassCurrent(e->pChar) : NULL;
	DefaultTray* pDefaultTray = pClass ? GET_REF(pClass->hDefaultTray) : NULL;

	if (pDefaultTray==NULL || pElem==NULL)
		return false;

	switch (pElem->eType)
	{
		xcase kTrayElemType_PowerTreeNode:
		{
			PTNodeDef *pNodeDefElem = powertreenodedef_Find(pElem->pchIdentifier);
			if(!pNodeDefElem)
				return false;

			while (pDefaultTray)
			{
				for (i = eaSize(&pDefaultTray->ppTrayElems)-1; i >= 0; i--)
				{
					DefaultTrayElemDef* pDefaultElem = pDefaultTray->ppTrayElems[i];
					PTNodeDef* pNodeDef = GET_REF(pDefaultElem->hNodeDef);
					if(pNodeDef==pNodeDefElem)
					{
						return gslDefaultTray_GetTraySlot(e,pDefaultTray,pDefaultElem,0,pDefaultSlot,pExtract);
					}
				}
				pDefaultTray = GET_REF(pDefaultTray->hBorrowFrom);
				devassert(--iRecurseCount > 0);
			}
		}
		xcase kTrayElemType_InventorySlot:
		{
			S32 iTrayBagID = 0, iTrayBagSlot = 0, iTrayItemPowIdx = 0;
			const char *pchString = pElem->pchIdentifier;
			tray_InventorySlotStringToIDs(pchString, &iTrayBagID, &iTrayBagSlot, &iTrayItemPowIdx);

			while (pDefaultTray)
			{
				for (i = eaSize(&pDefaultTray->ppTrayElems)-1; i >= 0; i--)
				{
					DefaultTrayElemDef* pDefaultElem = pDefaultTray->ppTrayElems[i];
					
					if (pDefaultElem->bSlotEntireBag && iTrayBagID==pDefaultElem->eBagID)
					{
						return gslDefaultTray_GetTraySlot(e,pDefaultTray,pDefaultElem,iTrayBagSlot,pDefaultSlot,pExtract);
					}
					else if (	!pDefaultElem->bSlotEntireBag	
							&&	iTrayBagID==pDefaultElem->eBagID
							&&	iTrayBagSlot==pDefaultElem->iBagSlot
							&&	iTrayItemPowIdx==pDefaultElem->iItemPowerIndex)
					{
						return gslDefaultTray_GetTraySlot(e,pDefaultTray,pDefaultElem,0,pDefaultSlot,pExtract);
					}
				}
				pDefaultTray = GET_REF(pDefaultTray->hBorrowFrom);
				devassert(--iRecurseCount > 0);
			}
		}
	}
	return false;
}

// Adds the TrayElem, 
//fails if there is already a TrayElem in that location and !bReplace or the TrayElem's location isn't valid
static bool entity_TrayElemAdd(Entity* pEnt, TrayElem *pElem, S32 iPreferredTray, S32 bReplace)
{
	SavedTray* pTray = entity_GetActiveTray(pEnt);

	if (!pTray)
	{
		return false;
	}
	
	if (TRAYSLOT_VALID(pElem->iTray,pElem->iTraySlot))
	{
		// Valid tray/slot check for a pre-existing tray elem, if there wasn't one or bReplace is true, everything is fine
		TrayElem *pelemPrior = entity_TrayGetTrayElem(pEnt, pElem->iTray, pElem->iTraySlot);
		if (!pelemPrior || bReplace)
		{
			if (!pelemPrior)
			{
				eaPush(&pTray->ppTrayElems, pElem);
			}
			else
			{
				StructCopyAll(parse_TrayElem, pElem, pelemPrior);
				StructDestroy(parse_TrayElem, pElem);
			}
			entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
			return true;
		}
	}
	else
	{
		// Invalid tray/slot, find the default slot this should go into. If that fails, find the next valid one and use that
		int iTray=-1, iSlot=-1;
		bool bDefaultTrayValid = false;
		TraySlot DefaultSlot = {0};
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

		if (	entity_TrayGetDefaultTraySlot(pEnt, pElem, &DefaultSlot,pExtract) 
			&& !entity_TrayGetTrayElem(pEnt,DefaultSlot.iTray,DefaultSlot.iSlot))
		{
			iTray = DefaultSlot.iTray;
			iSlot = DefaultSlot.iSlot;
			bDefaultTrayValid = true;
		}

		if(bDefaultTrayValid || entity_TrayGetUnusedTrayElem(pEnt,&iTray,&iSlot,iPreferredTray))
		{
			pElem->iTray = iTray;
			pElem->iTraySlot = iSlot;

			if (DefaultSlot.bAutoAttack && tray_CanEnableAutoAttackForElem(pEnt, pElem, NULL))
			{
				eaPush(&pTray->ppAutoAttackElems, StructClone(parse_TrayElem, pElem));
			}
			eaPush(&pTray->ppTrayElems, pElem);
			entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
			return true;
		}
	}
	return false;
}

// Creates a TrayElem for the given Power ID.  If the Power is from a PowerTree, it builds a TrayElem to the
//  PowerTreeNode, otherwise it makes a regular Power TrayElem.  If it's a regular Power, but it's a replacement,
//  it uses the Power ID of the Power it's replacing.
// TODO(JW): Does the replacement stuff make sense?  Can't one replacement replace several Powers?
SA_RET_OP_VALID static TrayElem *CreateTrayElemForPower(SA_PARAM_NN_VALID Character *pchar, int iTray, int iSlot, U32 uiID)
{
	TrayElem *pelem = NULL;

	// First we find where this ID is from, and then build the proper type of TrayElem
	PowerTree *ptree = NULL;
	PTNode *pnode = NULL;
	Power *ppow = NULL;
	int i;
	//in the case of a replacement power, find the base power
	for (i = eaSize(&pchar->ppPowers)-1; i >= 0; i--)
	{
		if (pchar->ppPowers[i]->uiReplacementID == uiID)
		{
			uiID = pchar->ppPowers[i]->uiID;
			break;
		}
	}

	ppow = character_FindPowerByIDTree(pchar,uiID,&ptree,&pnode);

	if(ppow)
	{
		// Power from a tree, build a PowerTreeNode
		PTNodeDef *pdef = pnode ? GET_REF(pnode->hDef) : NULL;
		if(pdef)
		{
			pelem = tray_CreateTrayElemForPowerTreeNode(iTray,iSlot,pdef->pchNameFull);
		}
	}
	else 
	{
		InvBagIDs eBagID = 0;
		S32 iBagSlot = 0;
		Item* pItem = NULL;
		S32 iItemPowerIdx = 0;

		ppow = item_FindPowerByID(pchar->pEntParent, uiID, &eBagID, &iBagSlot, &pItem, &iItemPowerIdx);
		if ( ppow )
		{
			GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pchar->pEntParent);
			NOCONST(Entity)* pNCEnt = CONTAINER_NOCONST(Entity, pchar->pEntParent);
			InventoryBag* pBag = (InventoryBag*)inv_GetBag(pNCEnt, eBagID, pExtract);
			if (invbag_flags(pBag) & (InvBagFlag_EquipBag | InvBagFlag_WeaponBag))
				pelem = tray_CreateTrayElemForInventorySlot(iTray,iSlot,eBagID,iBagSlot,iItemPowerIdx,ppow->uiID);
			else
				pelem = tray_CreateTrayElemForPowerID(iTray,iSlot,uiID);
		}
		else
		{
			ppow = character_FindPowerByID(pchar,uiID);
			if(ppow && (ppow->eSource == kPowerSource_AttribMod || ppow->eSource == kPowerSource_Temporary))
			{
				PowerDef *pDef = GET_REF(ppow->hDef);
				if(pDef)
					pelem = tray_CreateTrayElemForTempPower(iTray,iSlot,uiID,pDef->pchName);
			}
			else if(ppow && ppow->eSource == kPowerSource_Propagation)
			{
				PowerDef* pDef = GET_REF(ppow->hDef);
				U32 uPropSlotID = 0;
				U32 uiPropPetID = POWERID_GET_ENT(uiID);
				U32 uiPropPowID = POWERID_GET_BASE(uiID);
				
				// Make a best guess at the prop slot ID
				if (iTray < 0)
				{
					S32 iPropSlotIdx = AlwaysPropSlot_FindByPetID(pchar->pEntParent, uiPropPetID, pchar->pEntParent->pSaved->pPuppetMaster->curID, kAlwaysPropSlotCategory_Default);
					AlwaysPropSlot* pPropSlot = eaGet(&pchar->pEntParent->pSaved->ppAlwaysPropSlots, iPropSlotIdx);
					if (pPropSlot)
					{
						uPropSlotID = pPropSlot->iSlotID;
					}
				}
				if (pDef)
				{
					SavedTray* pTray = entity_GetActiveTray(pchar->pEntParent);
					
					if (pTray && uiPropPetID && iTray < 0)
					{
						for (i = 0; i < eaSize(&pTray->ppTrayElems); i++)
						{
							TrayElem* pTrayElem = pTray->ppTrayElems[i];
							if (pTrayElem->eType == kTrayElemType_PowerPropSlot)
							{
								U32 uiTrayPowID = (U32)pTrayElem->lIdentifier;
								U32 uiTraySavedPetID = POWERID_GET_ENT(uiTrayPowID);
								U32 uiTrayPropPowID = POWERID_GET_BASE(uiTrayPowID);
								U32 uTrayPropSlotID;
								S32 ePurpose;

								tray_PowerPropSlotStringToSlotData(pTrayElem->pchIdentifier, &ePurpose, &uTrayPropSlotID);

								//HACK: For now, check purpose as a backup if the IDs don't match (each AlwaysPropSlot should only have one power of each purpose)
								if ((uiPropPetID == uiTraySavedPetID && (uiPropPowID == uiTrayPropPowID || pDef->ePurpose == ePurpose)) ||
									(uTrayPropSlotID && uPropSlotID == uTrayPropSlotID && pDef->ePurpose == ePurpose))
								{
									iTray = pTrayElem->iTray;
									iSlot = pTrayElem->iTraySlot;
									uPropSlotID = uTrayPropSlotID;
									break;
								}
							}
						}
					}
					pelem = tray_CreateTrayElemForPowerPropSlot(iTray,iSlot,uiID,pDef->ePurpose,uPropSlotID);
				}
			}
			else
			{
				// Power from somewhere besides the character's power tree or inventory, build a Power
				pelem = tray_CreateTrayElemForPowerID(iTray,iSlot,uiID);
			}
		}
	}

	return pelem;
}

// ServerCmd for creating a Power TrayElem.  If the tray slot is invalid, it picks the next unused tray slot.
//  Automatically handles Powers from PowerTrees.  Optionally can fail if an existing TrayElem executes the
//  same Power.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_CATEGORY(Interface);
void TrayElemCreatePower(Entity *e, int iTray, int iSlot, int iIDPower, S32 bCheckDuplicate)
{
	if(e && e->pChar && e->pSaved)
	{
		TrayElem *pelem = NULL;
		int iPreferredTray = 0;

		if(bCheckDuplicate && entity_TrayGetTrayElemByPowerID(e,iIDPower,kTrayElemOwner_Self))
		{
			return;
		}

		if(TRAYSLOT_VALID(iTray,iSlot))
		{
			pelem = CreateTrayElemForPower(e->pChar,iTray,iSlot,(U32)iIDPower);
		}
		else
		{
			Power *ppow = character_FindPowerByID(e->pChar,iIDPower);
			PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;
			iPreferredTray = pdef ? powerdef_GetPreferredTray(pdef) : 0;

			if(pdef && (ppow->eSource == kPowerSource_AttribMod || ppow->eSource == kPowerSource_Temporary))
			{
				TrayElem *pAttribTrayElem = entity_FindEmptyTempPowerTrayElem(e,pdef->pchName);
				
				if(pAttribTrayElem)
				{
					pelem = CreateTrayElemForPower(e->pChar,pAttribTrayElem->iTray,pAttribTrayElem->iTraySlot,(U32)iIDPower);
					iPreferredTray = pelem->iTray;
				}
			}

			if(!pelem && entity_TrayGetUnusedTrayElem(e,&iTray,&iSlot,iPreferredTray))
			{
				// There seems to be something available, make this with invalid tray/slot
				//  and call Add with the preferred tray, which will do the same thing but
				//  inside the transaction.
				pelem = CreateTrayElemForPower(e->pChar,-1,-1,(U32)iIDPower);
			}
		}

		if(pelem)
		{
			if (!entity_TrayElemAdd(e, pelem, iPreferredTray, true))
			{
				StructDestroy(parse_TrayElem, pelem);
			}
		}
	}
}

// ServerCmd for creating a Power TrayElem for your Pet.  If the tray slot is invalid, it picks the next unused tray slot.
// TODO(JW): This doesn't work anymore
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_CATEGORY(Interface) ACMD_SERVERONLY;
void TrayElemCreatePowerPet(Entity *e, int iTray, int iSlot, int iIDPower)
{
	if(e && e->pChar && e->pSaved)
	{
		TrayElem *pelem = NULL;
		int iPreferredTray = 0;

		if(TRAYSLOT_VALID(iTray,iSlot))
		{
			if(!entity_TrayGetTrayElem(e,iTray,iSlot))
			{
				pelem = tray_CreateTrayElemForPowerID(iTray,iSlot,(U32)iIDPower);
			}
		}
		else
		{
			if(entity_TrayGetUnusedTrayElem(e,&iTray,&iSlot,iPreferredTray))
			{
				// There seems to be something available, make this with invalid tray/slot
				//  and call Add with the preferred tray, which will do the same thing but
				//  inside the transaction.
				pelem = tray_CreateTrayElemForPowerID(-1,-1,(U32)iIDPower);
			}
		}

		if(pelem)
		{
			if (!entity_TrayElemAdd(e, pelem, iPreferredTray, false))
			{
				StructDestroy(parse_TrayElem, pelem);
			}
		}
	}
}

static bool TrayElemCanCreateSavedPetPower(Entity *pEnt, U32 uPetID, const char *pchPowerDef)
{
	Entity *pPetEnt = entFromContainerID(entGetPartitionIdx(pEnt), GLOBALTYPE_ENTITYSAVEDPET, uPetID);
	PowerDef *pPowDef = powerdef_Find(pchPowerDef);

	if (pPowDef && pPetEnt && pPetEnt->pChar && pPetEnt->erOwner == entGetRef(pEnt))
	{
		if (character_FindPowerByDef(pPetEnt->pChar, pPowDef))
		{
			return true;
		}
	}
	return false;
}

// ServerCmd for creating a SavedPetPower TrayElem for a SavedPet.  
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_CATEGORY(Interface);
void TrayElemCreateSavedPetPower(Entity *e, int iTray, int iSlot, U32 uPetID, const char* pchPowerDef)
{
	if(e && e->pChar && e->pSaved)
	{
		if(TRAYSLOT_VALID(iTray,iSlot))
		{
			if(TrayElemCanCreateSavedPetPower(e,uPetID,pchPowerDef))
			{
				TrayElem *pelem = tray_CreateTrayElemForSavedPetPower(iTray,iSlot,uPetID,pchPowerDef);
				if(pelem)
				{
					if (!entity_TrayElemAdd(e, pelem, 0, true))
					{
						StructDestroy(parse_TrayElem, pelem);
					}
				}
			}
		}
	}
}


static bool TrayElemCanCreateInventorySlot(Entity* pEnt, S32 iInvBag, S32 iInvBagSlot, S32 iItemPower, Power** ppPow)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	Power* ppow = entity_GetTrayPowerFromItem(pEnt, iInvBag, iInvBagSlot, iItemPower, pExtract);

	if(ppow && pEnt->pChar)
	{
		if (ppPow)
		{
			(*ppPow) = ppow;
		}
		return !!character_FindPowerByID(pEnt->pChar, ppow->uiID);
	}
	return false;
}

// ServerCmd for creating an InventorySlot TrayElem.  If the tray slot is invalid it fails.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_CATEGORY(Interface);
void TrayElemCreateInventorySlot(Entity *e, S32 iTray, S32 iSlot, S32 iInvBag, S32 iInvBagSlot, S32 iItemPowIdx)
{
	if(e && e->pSaved)
	{
		if(TRAYSLOT_VALID(iTray,iSlot))
		{
			Power* pPow = NULL;
			if(TrayElemCanCreateInventorySlot(e,iInvBag,iInvBagSlot,iItemPowIdx,&pPow))
			{
				TrayElem *pelem = tray_CreateTrayElemForInventorySlot(iTray,iSlot,iInvBag,iInvBagSlot,iItemPowIdx,pPow->uiID);
				if(pelem)
				{
					if (!entity_TrayElemAdd(e, pelem, 0, true))
					{
						StructDestroy(parse_TrayElem, pelem);
					}
				}
			}
		}
	}
}

// ServerCmd for creating an InventorySlot TrayElem.  If the tray slot is invalid it fails.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_CATEGORY(Interface);
void TrayElemCreatePowerFromInventorySlot(Entity *e, S32 iTray, S32 iSlot, S32 iInvBag, S32 iInvBagSlot, S32 iItemPowIdx)
{
	if(e && e->pSaved)
	{
		if(TRAYSLOT_VALID(iTray,iSlot))
		{
			Power* pPow = NULL;
			if(TrayElemCanCreateInventorySlot(e,iInvBag,iInvBagSlot,iItemPowIdx,&pPow))
			{
				PowerDef* pDef = pPow ? GET_REF(pPow->hDef) : NULL;
				TrayElem *pelem = tray_CreateTrayElemForTempPower(iTray,iSlot,pPow->uiID, pDef ? pDef->pchName : NULL);
				if(pelem)
				{
					if (!entity_TrayElemAdd(e, pelem, 0, true))
					{
						StructDestroy(parse_TrayElem, pelem);
					}
				}
			}
		}
	}
}

// ServerCmd for creating a PowerPropSlot TrayElem.  If the tray slot is invalid it fails.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_CATEGORY(Interface);
void TrayElemCreatePowerPropSlot(Entity *e, S32 iTray, S32 iSlot, S32 iKey, U32 uPropSlotID)
{
	if(e && e->pChar && e->pSaved)
	{
		U32 uiID = (U32)iKey;
		Power* pPow = character_FindPowerByID(e->pChar, uiID);
		PowerDef* pPowDef = pPow ? GET_REF(pPow->hDef) : NULL;

		if(pPowDef && TRAYSLOT_VALID(iTray,iSlot) && !entity_TrayGetTrayElem(e,iTray,iSlot))
		{
			TrayElem *pelem = tray_CreateTrayElemForPowerPropSlot(iTray,iSlot,uiID,pPowDef->ePurpose,uPropSlotID);
			if(pelem)
			{
				if (!entity_TrayElemAdd(e, pelem, 0, false))
				{
					StructDestroy(parse_TrayElem, pelem);
				}
			}
		}
	}
}


void gslTrayElemCreateMacroByID_Internal(Entity *e, int iTray, int iSlot, U32 uMacroID)
{
	if (uMacroID)
	{
		TrayElem *pelem = tray_CreateTrayElemForMacro(iTray,iSlot,uMacroID);
		if(pelem)
		{
			if (!entity_TrayElemAdd(e, pelem, 0, true))
			{
				StructDestroy(parse_TrayElem, pelem);
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_CATEGORY(Interface);
void TrayElemCreateMacroByID(Entity *e, int iTray, int iSlot, U32 uMacroID)
{
	if(e && e->pChar && e->pSaved && e->pPlayer && uMacroID)
	{
		if(TRAYSLOT_VALID(iTray,iSlot))
		{
			gslTrayElemCreateMacroByID_Internal(e, iTray, iSlot, uMacroID);
		}
	}
}

// ServerCmd for creating a Macro TrayElem from data
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_CATEGORY(Interface);
void TrayElemCreateMacro(Entity *e, int iTray, int iSlot, PlayerMacro *pMacro)
{
	if(e && e->pChar && e->pSaved && e->pPlayer && pMacro)
	{
		if(TRAYSLOT_VALID(iTray,iSlot) && entity_IsMacroValid(pMacro->pchMacro, pMacro->pchDescription, pMacro->pchIcon))
		{
			U32 uMacroID = 0;
			S32 iIdx = entity_FindMacro(e, pMacro->pchMacro, pMacro->pchDescription, pMacro->pchIcon);

			if (iIdx >= 0)
			{
				uMacroID = e->pPlayer->pUI->eaMacros[iIdx]->uMacroID;
			}
			else
			{
				uMacroID = gslEntity_CreateMacro(e, pMacro->pchMacro, pMacro->pchDescription, pMacro->pchIcon);
			}
			gslTrayElemCreateMacroByID_Internal(e, iTray, iSlot, uMacroID);
		}
	}
}

static void entity_TrayAutoCreate(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID Power* ppow, SA_PARAM_NN_VALID PowerDef* pdef)
{
	if(!entity_TrayGetTrayElemByPowerID(e,ppow->uiID,kTrayElemOwner_Self))
	{
		// We didn't have this Power before, and there doesn't seem to be a TrayElem that already
		//  points to it.
		S32 bSlot = true;

		// If the Power is from a Node that is auto-purchased, don't slot it automatically
		if(ppow->eSource==kPowerSource_PowerTree)
		{
			PTNode *pnode = NULL;
			if(character_FindPowerByIDTree(e->pChar,ppow->uiID,NULL,&pnode))
			{
				PTNodeDef *pnodedef = pnode ? GET_REF(pnode->hDef) : NULL;
				if(pnodedef && pnodedef->eFlag&kNodeFlag_AutoBuy && !(pnodedef->eFlag&kNodeFlag_AutoSlot) && !gConf.bAutoBuyPowersGoInTray)
				{
					bSlot = false;
				}
			}
		}
		
		if (pdef->bSlottingRequired || pdef->bDoNotAutoSlot)
		{
			// If bSlottingRequired, this will get put in the slotted tray
			bSlot = false;
		}

		if(bSlot)
		{
			// Go ahead and make one in the first available tray slot, but
			//  check if we've already made one.
			TrayElemCreatePower(e,-1,-1,ppow->uiID,true);
		}
	}
}

static bool entity_TrayShouldAutoSlot(SA_PARAM_NN_VALID Power* pPower)
{
	PowerDef *pDef = GET_REF(pPower->hDef);

	if(!pDef || (!POWERTYPE_ACTIVATABLE(pDef->eType) && !pDef->bSlottingRequired) || 
		pDef->bHideInUI || pPower->bHideInUI || pPower->bIsReplacing)
	{
		
		return false;
	}
	return true;
}

// Automatically fills the Entity's trays with Powers.  If an earray of preexisting
//  PowerIDs is included, it will not include those.
void entity_TrayAutoFill(Entity *e, U32 *puiIDs)
{
	SavedTray* pTray = entity_GetActiveTray(e);
	 
	// Fixup tray data
	if(pTray)
	{
		int i, n, c, s = eaSize(&e->pChar->ppPowers);

		for (i=eaSize(&pTray->ppTrayElems)-1; i >= 0; i--)
		{
			TrayElem* pElem = pTray->ppTrayElems[i];

			switch (pElem->eType)
			{
				xcase kTrayElemType_InventorySlot:
				{
					Power* ppowNew = entity_TrayGetPower(e, pElem);
					U32 uiNewPowID = ppowNew ? ppowNew->uiID : 0;
					U32 uiOldPowID = (U32)pElem->lIdentifier;
				
					// If the power cannot be found in the list of valid power IDs, then remove the tray element
					if (!uiNewPowID || !character_FindPowerByID(e->pChar, uiNewPowID))
					{
						StructDestroy(parse_TrayElem, eaRemoveFast(&pTray->ppTrayElems, i));
						entity_SetDirtyBit(e, parse_SavedEntityData, e->pSaved, false);
					}
					else if (uiOldPowID && uiNewPowID != uiOldPowID)
					{
						// Handle moving an item from one slot to another in the same bag
						if (!uiNewPowID)
						{					
							Power* ppowOld = character_FindPowerByID(e->pChar, uiOldPowID);
							PowerDef* pdefOld = ppowOld ? GET_REF(ppowOld->hDef) : NULL;
							if (pdefOld)
							{
								entity_TrayAutoCreate(e, ppowOld, pdefOld);
							}
						}
						pElem->lIdentifier = uiNewPowID;
					}
				}
				xcase kTrayElemType_PowerPropSlot:
				{
					Power* ppow = entity_TrayGetPower(e, pElem);
					PowerDef* ppowDef = SAFE_GET_REF(ppow, hDef);
					if (ppowDef && e->pSaved)
					{
						U32 uPropPetID = POWERID_GET_ENT(ppow->uiID);
						S32 iPropSlotIdx = AlwaysPropSlot_FindByPetID(e, uPropPetID, e->pSaved->pPuppetMaster->curID, kAlwaysPropSlotCategory_Default);
						AlwaysPropSlot* pPropSlot = eaGet(&e->pSaved->ppAlwaysPropSlots, iPropSlotIdx);
						if (pPropSlot)
						{
							U32 uPropSlotID;
							S32 ePurpose;
							tray_PowerPropSlotStringToSlotData(pElem->pchIdentifier, &ePurpose, &uPropSlotID);
							
							if (uPropSlotID && uPropSlotID != pPropSlot->iSlotID)
							{
								for (n = eaSize(&e->pChar->ppPowersPropagation)-1; n >= 0; n--)
								{
									Power* pPropPower = e->pChar->ppPowersPropagation[n];
									PowerDef* pPropPowDef = GET_REF(pPropPower->hDef);
									if (pPropPowDef)
									{
										U32 uCheckPropPetID = POWERID_GET_ENT(pPropPower->uiID);
										S32 iCheckPropSlotIdx = AlwaysPropSlot_FindByPetID(e, uCheckPropPetID, e->pSaved->pPuppetMaster->curID, kAlwaysPropSlotCategory_Default);
										AlwaysPropSlot* pCheckPropSlot = eaGet(&e->pSaved->ppAlwaysPropSlots, iCheckPropSlotIdx);
										if (pCheckPropSlot && pCheckPropSlot->iSlotID == uPropSlotID && pPropPowDef->ePurpose == ppowDef->ePurpose)
										{
											pElem->lIdentifier = pPropPower->uiID;
											break;
										}
									}
								}
								if (n < 0)
								{
									StructDestroy(parse_TrayElem, eaRemoveFast(&pTray->ppTrayElems, i));
								}
								entity_SetDirtyBit(e, parse_SavedEntityData, e->pSaved, false);
							}
						}
					}
				}
			}
		}

		// Slot tree powers into the tray, 
		// This used to be two separate loops, one for autobuys and one for manual powers, 
		// at one point they were slotting differently, but since this was disabled and they were doing the same thing
		// I condensed this to make sorting-by-priority easier to handle
		for(n=0; n<2; n++)
		{
			bool bHighPriorityPass = (n==0);

			// Check the new list for Powers we didn't have before
			for(i=0; i<s; i++)
			{
				Power *ppow = e->pChar->ppPowers[i];
				if(-1==eaiFind(&puiIDs,ppow->uiID))
				{
					// Found a new Power
					bool bHasPriority = false;
					PowerDef *pdef = GET_REF(ppow->hDef);

					if (!pdef || !entity_TrayShouldAutoSlot(ppow))
						continue;

					//check the priority
					for ( c=ea32Size(&pdef->piCategories)-1; c>=0; c-- )
					{
						if ( g_PowerCategories.ppCategories[ pdef->piCategories[c] ]->bHasAutoSlotPriority )
						{
							bHasPriority = true;
							break;
						}
					}

					if ( bHighPriorityPass && !bHasPriority )
						continue;

					if ( !bHighPriorityPass && bHasPriority )
						continue;

					entity_TrayAutoCreate(e, ppow, pdef);
				}
			}
		}
	}
}


// Automatically fills the Entity's trays with Powers.  If an earray of preexisting
//  PowerIDs is included, it will not include those.
// TODO(JW): This doesn't work anymore
void entity_TrayAutoFillPet(Entity *e, U32 *puiIDs)
{
	Entity *pPrimaryPet = entGetPrimaryPet(e);
	if(e->pChar && e->pSaved && pPrimaryPet && pPrimaryPet->pChar)
	{
		int i, n, c, s = eaSize(&pPrimaryPet->pChar->ppPowers);

		for(n=0; n<2; n++)
		{
			bool bHighPriorityPass = (n==0);

			// Check the new list for Powers we didn't have before
			for(i=0; i<s; i++)
			{
				Power *ppow = pPrimaryPet->pChar->ppPowers[i];
				if(-1==eaiFind(&puiIDs,ppow->uiID))
				{
					// Found a new Power
					PowerDef *pdef = GET_REF(ppow->hDef);

					if(!pdef || POWERTYPE_ACTIVATABLE(pdef->eType))
					{
						//check the priority
						bool bHasPriority = false;
					
						for ( c=0; c<ea32Size(&pdef->piCategories); c++ )
						{
							if ( g_PowerCategories.ppCategories[ pdef->piCategories[c] ]->bHasAutoSlotPriority )
							{
								bHasPriority = true;
								break;
							}
						}

						if ( bHighPriorityPass && !bHasPriority )
							continue;

						if ( !bHighPriorityPass && bHasPriority )
							continue;

						if(!entity_TrayGetTrayElemByPowerID(e,ppow->uiID,kTrayElemOwner_PrimaryPet))
						{
							// We didn't have this Power before, and there doesn't seem to be a TrayElem that already
							//  points to it.
							S32 bSlot = true;

							// If the Power is from a Node that is auto-purchased, don't slot it automatically
							if(ppow->eSource==kPowerSource_PowerTree)
							{
								PTNode *pnode = NULL;
								if(character_FindPowerByIDTree(pPrimaryPet->pChar,ppow->uiID,NULL,&pnode))
								{
									PTNodeDef *pnodedef = pnode ? GET_REF(pnode->hDef) : NULL;
									if(pnodedef && pnodedef->eFlag&kNodeFlag_AutoBuy && !(pnodedef->eFlag&kNodeFlag_AutoSlot))
									{
										bSlot = false;
									}
								}
							}
							if (pdef->bSlottingRequired || pdef->bDoNotAutoSlot)
							{
								// if bSlottingRequired, this will get put in the slotted tray
								bSlot = false;
							}

							if(bSlot)
							{
								// Go ahead and make one in the first available tray slot, but let the
								//  server check if we've already made one.
								TrayElemCreatePowerPet(e,-1,-1,ppow->uiID);
							}
						}
					}
				}
			}
		}
	}
}




// AutoAttack Elem commands and transactions

// Destroys the AutoAttack TrayElem in the given index
static bool entity_AutoAttackElemDestroy(Entity* pEnt, int index)
{
	SavedTray* pTray = entity_GetActiveTray(pEnt);
	if (pTray)
	{
		TrayElem *pelem = eaRemove(&pTray->ppAutoAttackElems, index);
		if(pelem)
		{
			StructDestroy(parse_TrayElem, pelem);
			entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
			return true;
		}
	}
	return false;
}

// Destroys the AutoAttack TrayElem earray
static bool entity_AutoAttackElemDestroyAll(Entity* pEnt)
{
	SavedTray* pTray = entity_GetActiveTray(pEnt);
	if (pTray)
	{
		eaDestroyStruct(&pTray->ppAutoAttackElems, parse_TrayElem);
		entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
		return true;
	}
	return false;
}

// ServerCmd for destroying a particular AutoAttack TrayElem, -1 destroys all of them
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_CATEGORY(Interface);
void AutoAttackElemDestroy(Entity *e, int index)
{
	if(index==-1)
	{
		entity_AutoAttackElemDestroyAll(e);
	}
	else
	{
		entity_AutoAttackElemDestroy(e, index);
	}
}

// Adds the AutoAttack TrayElem
static bool entity_AutoAttackElemAdd(Entity* pEnt, TrayElem *pelem)
{
	SavedTray* pTray = entity_GetActiveTray(pEnt);
	if (pTray)
	{
		eaPush(&pTray->ppAutoAttackElems, pelem);
		entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
		return true;
	}
	return false;
}


// ServerCmd for creating an InventorySlot AutoAttack TrayElem.  If the InventorySlot is invalid it fails.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_CATEGORY(Interface);
void AutoAttackElemCreateInventorySlot(Entity *e, S32 iInvBag, S32 iInvBagSlot, S32 iItemPowIdx)
{
	SavedTray* pTray = entity_GetActiveTray(e);
	if (pTray)
	{
		Power* pPow = NULL;
		if (TrayElemCanCreateInventorySlot(e,iInvBag,iInvBagSlot,iItemPowIdx,&pPow))
		{
			TrayElem *pelem = tray_CreateTrayElemForInventorySlot(0,0,iInvBag,iInvBagSlot,iItemPowIdx,pPow->uiID);
			if (pelem)
			{
				int index = tray_FindTrayElem(&pTray->ppAutoAttackElems, pelem);
				if(index >= 0 || !entity_AutoAttackElemAdd(e, pelem))
				{
					StructDestroy(parse_TrayElem, pelem);
				}
			}
		}
	}
}

// ServerCmd for creating a PowerTreeNode AutoAttack TrayElem.  If the PowerTreeNode is invalid it fails.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_CATEGORY(Interface);
void AutoAttackElemCreatePowerTreeNode(Entity *e, const char* pchNode)
{
	SavedTray* pTray = entity_GetActiveTray(e);
	if(pTray && e->pChar)
	{
		if(character_GetNode(e->pChar,pchNode))
		{
			TrayElem *pelem = tray_CreateTrayElemForPowerTreeNode(0,0,pchNode);
			if(pelem)
			{
				int index = tray_FindTrayElem(&pTray->ppAutoAttackElems, pelem);
				if(index >= 0 || !entity_AutoAttackElemAdd(e, pelem))
				{
					StructDestroy(parse_TrayElem, pelem);
				}
			}
		}
	}
}

// Sets values into the Entity's UITray array.  Returns true on success.
int entity_TraySetUITrayIndex(Entity *e, int iUITray, int iTray)
{
	SavedTray* pTray = entity_GetActiveTray(e);

	if(pTray && iUITray>=0 && iUITray<TRAY_UI_COUNT_MAX)
	{
		int i, s = eaiSize(&pTray->piUITrayIndex);
		if(iUITray>=s)
		{
			eaiSetSize(&pTray->piUITrayIndex,iUITray+1);
			for (i = iUITray-1; i >= 0; i--) //fill in (0,iUITray-1) with reasonable defaults
			{
				pTray->piUITrayIndex[i] = CLAMP(i,0,TRAY_COUNT_MAX-1);
			}
		}
		pTray->piUITrayIndex[iUITray] = CLAMP(iTray,0,TRAY_COUNT_MAX-1);
		entity_SetDirtyBit(e, parse_SavedEntityData, e->pSaved, false);
		return true;
	}
	return false;
}

// ServerCmd for saving an Entity's ui tray data
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE ACMD_SERVERONLY ACMD_CATEGORY(Interface);
void TraySetIndex(Entity *e, int iUITray, int iTray)
{
	if(e)
	{
		entity_TraySetUITrayIndex(e, iUITray, iTray);
	}
}

// ServerCmd for saving an Entity's ui tray data
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE ACMD_SERVERONLY ACMD_CATEGORY(Interface);
void TraySetIndexRange(Entity *e, int iUITrayStart, int iUITrayEnd, int iTrayStart, int iTrayEnd)
{
	if(e && e->pSaved)
	{
		int i, iSize = ABS(iUITrayEnd-iUITrayStart)+1;
		iUITrayStart = CLAMP( iUITrayStart, 0, TRAY_UI_COUNT_MAX-1 );
		iUITrayEnd = CLAMP( iUITrayEnd, 0, TRAY_UI_COUNT_MAX-1 );

		for ( i = 0; i < iSize; i++ )
		{
			int iUITray = iUITrayStart <= iUITrayEnd ? iUITrayStart+i : iUITrayStart-i;
			int iTray = iTrayStart <= iTrayEnd ? iTrayStart+i : iTrayStart-i;
			int iResult;

			iResult = entity_TraySetUITrayIndex(e, iUITray, iTray);

			devassert( iResult == true );
		}
	}
}

AUTO_TRANS_HELPER;
void entity_trh_CleanupOldTray(ATH_ARG NOCONST(Entity) *pEntity)
{
	if (NONNULL(pEntity->pSaved))
	{
		if (NONNULL(pEntity->pSaved->ppTrayElems_Obsolete))
		{
			eaDestroyStructNoConst(&pEntity->pSaved->ppTrayElems_Obsolete, parse_TrayElemOld);
		}
		if (NONNULL(pEntity->pSaved->ppAutoAttackElems_Obsolete))
		{
			eaDestroyStructNoConst(&pEntity->pSaved->ppAutoAttackElems_Obsolete, parse_TrayElemOld);
		}
	}
}

AUTO_TRANSACTION ATR_LOCKS(pEntity, ".Psaved.Pptrayelems_Obsolete, .Psaved.Ppautoattackelems_Obsolete");
enumTransactionOutcome trEntityCleanupOldTray(ATR_ARGS, NOCONST(Entity) *pEntity)
{
	entity_trh_CleanupOldTray(pEntity);
	return TRANSACTION_OUTCOME_SUCCESS;
}

static void entity_CopyOldTrayInternal(TrayElemOld*** peaOldTrayElems, TrayElem*** peaTrayElems)
{
	S32 i, iSize = eaSize(peaOldTrayElems);
	S32 iInvalidCount = 0;
	eaSetSize(peaTrayElems, iSize);
	for (i = 0; i < iSize; i++)
	{
		const TrayElemOld* pElemOld = (*peaOldTrayElems)[i];
		if (pElemOld->eType != kTrayElemType_Unset)
		{
			S32 iIndex = i-iInvalidCount;
			if (!(*peaTrayElems)[iIndex])
			{
				(*peaTrayElems)[iIndex] = tray_CreateTrayElemFromOldTrayElem(pElemOld);
			}
			else
			{
				tray_SetTrayElemFromOldTrayElem((*peaTrayElems)[iIndex], pElemOld);
			}
		}
		else
		{
			Alertf("Invalid tray element found during tray fixup");
			iInvalidCount++;
		}
	}
	if (iInvalidCount)
	{
		eaSetSize(peaTrayElems, iSize-iInvalidCount);
	}
}

void entity_CopyOldTrayData(Entity* pEntity, SavedTray* pTray)
{
	if (ea32Size(&pEntity->pSaved->piUITrayIndex_Obsolete))
	{
		ea32Copy(&pTray->piUITrayIndex,&pEntity->pSaved->piUITrayIndex_Obsolete);
	}
	if (eaSize(&pEntity->pSaved->ppTrayElems_Obsolete))
	{
		entity_CopyOldTrayInternal((TrayElemOld***)&pEntity->pSaved->ppTrayElems_Obsolete,&pTray->ppTrayElems);
	}
	if (eaSize(&pEntity->pSaved->ppAutoAttackElems_Obsolete))
	{
		entity_CopyOldTrayInternal((TrayElemOld***)&pEntity->pSaved->ppAutoAttackElems_Obsolete,&pTray->ppAutoAttackElems);
	}
}

void entity_TrayFixup(Entity* pEntity)
{
	PERFINFO_AUTO_START_FUNC();

	if(		pEntity->pSaved->ppTrayElems_Obsolete 
		||	pEntity->pSaved->ppAutoAttackElems_Obsolete
		||	pEntity->pSaved->piUITrayIndex_Obsolete)
	{
		if (!pEntity->pSaved->pPuppetMaster)
		{
			if (!pEntity->pSaved->pTray)
			{
				pEntity->pSaved->pTray = StructCreate(parse_SavedTray);
			}
			entity_CopyOldTrayData(pEntity, pEntity->pSaved->pTray);
		}
		else if (pEntity->pSaved->pTray)
		{
			StructDestroy(parse_SavedTray, pEntity->pSaved->pTray);
		}
		if (pEntity->pSaved->piUITrayIndex_Obsolete)
		{
			ea32Destroy(&pEntity->pSaved->piUITrayIndex_Obsolete);
		}
		if (pEntity->pSaved->ppTrayElems_Obsolete || pEntity->pSaved->ppAutoAttackElems_Obsolete)
		{
			AutoTrans_trEntityCleanupOldTray(NULL,GLOBALTYPE_GAMESERVER,entGetType(pEntity),entGetContainerID(pEntity));
		}
		entity_SetDirtyBit(pEntity, parse_SavedEntityData, pEntity->pSaved, false);
	}
	else if (pEntity->pSaved->pPuppetMaster)
	{
		GameAccountDataExtract *pExtract;
		bool bDirty = false;
		RegionRules* pRegionRules = getRegionRulesFromEnt(pEntity);
		int i, j;

		pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		character_ResetPowersArray(entGetPartitionIdx(pEntity), pEntity->pChar, pExtract);

		for (i = eaSize(&pEntity->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
		{
			PuppetEntity* pPupEnt = pEntity->pSaved->pPuppetMaster->ppPuppets[i];
			
			if (	pPupEnt->curType != pEntity->pSaved->pPuppetMaster->curType
				||	pPupEnt->curID != pEntity->pSaved->pPuppetMaster->curID)
			{
				continue;
			}
			
			for (j = eaSize(&pPupEnt->PuppetTray.ppTrayElems)-1; j >= 0; j--)
			{
				TrayElem* pTrayElem = pPupEnt->PuppetTray.ppTrayElems[j];
				if (pTrayElem->eType == kTrayElemType_Unset)
				{
					StructDestroy(parse_TrayElem, eaRemove(&pPupEnt->PuppetTray.ppTrayElems, j));
					bDirty = true;
				}
				else
				{
					Power* pPower = entity_TrayGetPower(pEntity, pTrayElem);
					PowerDef *pPowDef = pPower ? GET_REF(pPower->hDef) : NULL;
					if (pPowDef)
					{
						if (!POWERTYPE_ACTIVATABLE(pPowDef->eType) || pPower->bHideInUI)
						{
							StructDestroy(parse_TrayElem, eaRemove(&pPupEnt->PuppetTray.ppTrayElems, j));
							bDirty = true;
						}
						else if (pRegionRules)
						{
							S32 iCat;
							for (iCat=0;iCat<ea32Size(&pRegionRules->piCategoryDoNotAdd);iCat++)
							{
								if (ea32Find(&pPowDef->piCategories,pRegionRules->piCategoryDoNotAdd[iCat]) > -1)
								{
									StructDestroy(parse_TrayElem, eaRemove(&pPupEnt->PuppetTray.ppTrayElems, j));
									bDirty = true;
									break;
								}
							}
						}
					}
				}
			}
			break;
		}
		if (bDirty)
		{
			entity_SetDirtyBit(pEntity, parse_SavedEntityData, pEntity->pSaved, false);
		}
	}
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE ACMD_SERVERONLY ACMD_CATEGORY(Interface);
void TraySetLocked(Entity *e, bool bLocked)
{
	if(e && e->pPlayer)
	{
		e->pPlayer->pUI->pLooseUI->bLockTray = bLocked;
		entity_SetDirtyBit(e, parse_PlayerUI, e->pPlayer->pUI, true);
		entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
	}
}

void entity_BuildPowerIDListFromTray(Entity* pEnt, U32** ppuiIDs)
{
	SavedTray* pTray = entity_GetActiveTray(pEnt);
	if (pTray)
	{
		S32 i;
		// Add all power IDs from tray elements
		for (i = 0; i < eaSize(&pTray->ppTrayElems); i++)
		{
			Power* pPower = entity_TrayGetPower(pEnt, pTray->ppTrayElems[i]);
			if (pPower)
			{
				ea32Push(ppuiIDs, pPower->uiID);
			}
		}
		// Add power IDs that should not be slotted in the tray
		for (i = 0; i < ea32Size(&pTray->puiNoSlotPowerIDs); i++)
		{
			ea32Push(ppuiIDs, pTray->puiNoSlotPowerIDs[i]);
		}

		// If the character still has elements in the obsolete power ID array, add those as well
		if (pEnt->pChar && ea32Size(&pEnt->pChar->puiPowerIDsSaved_Obsolete))
		{
			for (i = 0; i < ea32Size(&pEnt->pChar->puiPowerIDsSaved_Obsolete); i++)
			{
				ea32PushUnique(ppuiIDs, pEnt->pChar->puiPowerIDsSaved_Obsolete[i]);
			}
			// Clean up the array
			ea32Destroy(&pEnt->pChar->puiPowerIDsSaved_Obsolete);
		}
	}
}

static void entity_SaveTrayNoSlotPowerIDs(Entity* pEnt, U32* puiIDs)
{
	SavedTray* pTray = entity_GetActiveTray(pEnt);
	if (pTray)
	{
		bool bDiff = false;
		ea32QSort(puiIDs, cmpU32);
		if (ea32Size(&puiIDs) != ea32Size(&pTray->puiNoSlotPowerIDs))
		{
			bDiff = true;
		}
		else
		{
			S32 i;
			for (i = ea32Size(&pTray->puiNoSlotPowerIDs)-1; i >= 0; i--)
			{
				U32 uiID = ea32Get(&puiIDs, i);
				if (pTray->puiNoSlotPowerIDs[i] != uiID)
				{
					bDiff = true;
					break;
				}
			}
		}
		// If the arrays are different, then save out puiIDs
		if (bDiff)
		{
			ea32Copy(&pTray->puiNoSlotPowerIDs, &puiIDs);
		}
	}
}

void entity_TrayPreSave(Entity* pEnt)
{
	if (pEnt && pEnt->pChar && pEnt->pChar->bLoaded)
	{
		U32* puiIDs = NULL;
		S32 i, s = eaSize(&pEnt->pChar->ppPowers);
		for (i = 0; i < s; i++)
		{
			Power* pPower = pEnt->pChar->ppPowers[i];
			U32 uiPowerID = pPower->uiID;

			if (!entity_TrayShouldAutoSlot(pPower))
			{
				continue;
			}
			if (!entity_TrayGetTrayElemByPowerID(pEnt, uiPowerID, kTrayElemOwner_Self))
			{
				ea32Push(&puiIDs, uiPowerID);
			}	
		}
		entity_SaveTrayNoSlotPowerIDs(pEnt, puiIDs);
		ea32Destroy(&puiIDs);
	}
}

/* End of File */
