/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "Tray.h"
#include "Tray_h_ast.h"

#include "AutoTransDefs.h"
#include "Character.h"
#include "CombatConfig.h"
#include "ControlScheme.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "EntitySavedData.h"
#include "EString.h"
#include "GameAccountDataCommon.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "itemEnums.h"
#include "itemEnums_h_ast.h"
#include "MemoryPool.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "Powers.h"
#include "PowerSlots.h"
#include "PowerTree.h"
#include "CombatPowerStateSwitching.h"
#include "ResourceManager.h"
#include "StringUtil.h"

#include "EntitySavedData_h_ast.h"

#ifdef GAMECLIENT
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

MP_DEFINE(TrayElem);

DictionaryHandle g_hDefaultTrayDict;

AUTO_RUN;
void TrayElemInitMemPool(void)
{
	MP_CREATE(TrayElem, 16);
}

static void DefaultTray_FixupPointers(DefaultTray *pDefaultTray)
{
	DefaultTray* pParent = GET_REF(pDefaultTray->hBorrowFrom);
	if (pParent)
	{
		if (pParent == pDefaultTray)
		{
			ErrorFilenamef(pDefaultTray->pchFilename, 
				"Default Tray %s borrows from itself. This isn't allowed.", 
				pDefaultTray->pchDefaultTrayName);
		}
		else
		{
			pDefaultTray->pParent = pParent;
		}
	}
}

static void DefaultTray_Validate(DefaultTray *pDefaultTray)
{
	S32 i;
	for (i = eaSize(&pDefaultTray->ppTrayElems)-1; i >= 0; i--)
	{
		DefaultTrayElemDef* pTrayElemDef = pDefaultTray->ppTrayElems[i];
		PTNodeDef* pNodeDef = GET_REF(pTrayElemDef->hNodeDef);
		InvBagIDs eBagID = pTrayElemDef->eBagID;
		const char* pchBagName = StaticDefineIntRevLookup(InvBagIDsEnum, eBagID);
		
		if (!TRAYSLOT_VALID(pTrayElemDef->iTray, pTrayElemDef->iSlot))
		{
			ErrorFilenamef(pDefaultTray->pchFilename,
				"%s %d: Invalid tray element specified (Tray: %d/Slot: %d)",
				pDefaultTray->pchDefaultTrayName,i,
				pTrayElemDef->iTray, pTrayElemDef->iSlot);
		}
		if (pNodeDef && pTrayElemDef->eBagID != InvBagIDs_None)
		{
			ErrorFilenamef(pDefaultTray->pchFilename,
				"%s %d: Specified both a power from a node (%s) and a bag (%s). Specify only one.", 
				pDefaultTray->pchDefaultTrayName,i,
				pNodeDef->pchName, pchBagName);
		}
		else if (!pNodeDef && eBagID == InvBagIDs_None)
		{
			ErrorFilenamef(pDefaultTray->pchFilename,
				"%s %d: No valid node (%s) nor bag (%s) was specified.",
				pDefaultTray->pchDefaultTrayName,i,
				REF_STRING_FROM_HANDLE(pTrayElemDef->hNodeDef),pchBagName);
		}
	}
}

static int DefaultTray_Validate_CB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, DefaultTray *pDefaultTray, U32 userID)
{
    switch (eType)
    {	
        xcase RESVALIDATE_POST_BINNING: 
			DefaultTray_Validate(pDefaultTray);
			return VALIDATE_HANDLED;
		xcase RESVALIDATE_FINAL_LOCATION:
			DefaultTray_FixupPointers(pDefaultTray);
			return VALIDATE_HANDLED;
	};
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void Tray_AutoRun(void)
{
	g_hDefaultTrayDict = RefSystem_RegisterSelfDefiningDictionary("DefaultTray", false, parse_DefaultTray, true, true, NULL);
	
#ifdef GAMESERVER
	resDictManageValidation(g_hDefaultTrayDict, DefaultTray_Validate_CB);
#endif
}

AUTO_STARTUP(Tray) ASTRT_DEPS(InventoryBagIDs, PowerTrees);
void Tray_Load(void)
{
#ifdef GAMESERVER
	resLoadResourcesFromDisk(g_hDefaultTrayDict, NULL, "defs/config/DefaultTray.def", "DefaultTray.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);	
#endif
}

SavedTray* entity_GetActiveTray(Entity *e)
{
	if (e && e->pSaved)
	{
		if (e->pSaved->pPuppetMaster)
		{
			S32 i;
			if(e->pSaved->pPuppetMaster->curTempID)
			{
				ContainerID curID = e->pSaved->pPuppetMaster->curTempID;
				for (i=eaSize(&e->pSaved->pPuppetMaster->ppTempPuppets)-1; i>= 0; i--)
				{
					if(curID == (ContainerID)e->pSaved->pPuppetMaster->ppTempPuppets[i]->uiID)
					{
						return &e->pSaved->pPuppetMaster->ppTempPuppets[i]->PuppetTray;
					}
				}
			}
			else
			{
				ContainerID curID = e->pSaved->pPuppetMaster->curID;
				GlobalType curType = e->pSaved->pPuppetMaster->curType;

				for (i = eaSize(&e->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
				{
					PuppetEntity* pPuppet = e->pSaved->pPuppetMaster->ppPuppets[i];
					if (pPuppet->curID == curID && pPuppet->curType == curType)
					{
						return &pPuppet->PuppetTray;
					}
				}
			}
		}
		else
		{
			return e->pSaved->pTray;
		}
	}
	return NULL;
}

// Gets the next unused TrayElem's Tray and Slot.  Returns true when successful.
int entity_TrayGetUnusedTrayElem(Entity *e, int *piTrayOut, int *piSlotOut, int iPreferredTray)
{
	// This would be easier if the tray elements were sorted
	int a[TRAY_COUNT_MAX][TRAY_SIZE_MAX] = {0};
	int i,j;
	SavedTray* pTray = entity_GetActiveTray(e);

	if(!iPreferredTray)
	{
		if(!pTray || !eaSize(&pTray->ppTrayElems))
		{
			*piTrayOut = 0;
			*piSlotOut = 0;
			return true;
		}
	}

	// Fill the array
	if(pTray)
	{
		for(i=eaSize(&pTray->ppTrayElems)-1; i>=0; i--)
		{
			a[pTray->ppTrayElems[i]->iTray][pTray->ppTrayElems[i]->iTraySlot] = 1;
		}
	}

	// Run through the preferred tray first
	if(iPreferredTray >= 0 && iPreferredTray < TRAY_COUNT_MAX)
	{
		i=iPreferredTray;
		for(j=0; j<TRAY_SIZE_MAX_CONFIG; j++)
		{
			if(!a[i][j])
			{
				*piTrayOut = i;
				*piSlotOut = j;
				return true;
			}
		}
	}

	for(i=0; i<TRAY_COUNT_MAX; i++)
	{
		for(j=0; j<TRAY_SIZE_MAX_CONFIG; j++)
		{
			if(!a[i][j])
			{
				*piTrayOut = i;
				*piSlotOut = j;
				return true;
			}
		}
	}

	*piTrayOut = TRAY_COUNT_MAX;
	*piSlotOut = TRAY_SIZE_MAX_CONFIG;
	return false;
}

TrayElem *entity_TrayGetTrayElem(Entity *e, int iTray, int iSlot)
{
	SavedTray* pTray = entity_GetActiveTray(e);
	if(pTray && TRAYSLOT_VALID(iTray,iSlot))
	{
		int i;
		for(i=eaSize(&pTray->ppTrayElems)-1; i>=0; i--)
		{
			if(pTray->ppTrayElems[i]->iTray==iTray && pTray->ppTrayElems[i]->iTraySlot==iSlot)
			{
				return pTray->ppTrayElems[i];
			}
		}
	}
	return NULL;
}

// Finds an TempPower type tray elem that matches the power def, and no longer has a matching power
TrayElem *entity_FindEmptyTempPowerTrayElem(Entity *e, const char *pchPowerDef)
{
	SavedTray* pTray = entity_GetActiveTray(e);
	int i;

	if (pTray)
	{
		for(i=0;i<eaSize(&pTray->ppTrayElems);i++)
		{
			if(pTray->ppTrayElems[i]->eType == kTrayElemType_TempPower
				&& character_FindPowerByID(e->pChar,(U32)pTray->ppTrayElems[i]->lIdentifier) == NULL
				&& strcmp(pchPowerDef,pTray->ppTrayElems[i]->pchIdentifier) == 0)
			{
				return pTray->ppTrayElems[i];
			}
		}
	}
	return NULL;
}

// Gets the TrayElem given the PowerID.  May return NULL.
TrayElem *entity_TrayGetTrayElemByPowerID(Entity *e, U32 uiID, TrayElemOwner eOwner)
{
	SavedTray* pTray = entity_GetActiveTray(e);
	Entity *ePowerOwner = e;
	int i;
	bool bGetItemPower = true;
	bool bFoundItemPower = false;
	if (pTray)
	{	
		if (eOwner == kTrayElemOwner_PrimaryPet)
		{
			ePowerOwner = entGetPrimaryPet(e);
			if (!ePowerOwner || !ePowerOwner->pChar)
			{
				return NULL;
			}
		}
		for(i=eaSize(&pTray->ppTrayElems)-1; i>=0; i--)
		{
			TrayElem* pTrayElem = pTray->ppTrayElems[i];

			if (pTrayElem->eOwner != eOwner)
				continue;
			
			switch (pTrayElem->eType)
			{
				xcase kTrayElemType_Power:
				case kTrayElemType_TempPower:
				{
					if((U32)pTrayElem->lIdentifier==uiID)
					{
						return pTrayElem;
					}
				}
				xcase kTrayElemType_PowerTreeNode:
				{
					const char *pchNode = pTrayElem->pchIdentifier;
					if(pchNode && ePowerOwner->pChar)
					{
						PTNode *pnode = character_GetNode(ePowerOwner->pChar,pchNode);
						if(pnode)
						{
							Power *ppow = powertreenode_GetActivatablePower(pnode);
							if(ppow && (ppow->uiID==uiID || ppow->uiReplacementID == uiID))
							{
								return pTrayElem;
							}
						}
					}
				}
				xcase kTrayElemType_PowerPropSlot:
				{
					U32 uiTrayPowID = (U32)pTrayElem->lIdentifier;

					if (uiTrayPowID == uiID)
					{
						return pTrayElem;
					}
				}
				xcase kTrayElemType_InventorySlot:
				{
					const char *pchString = pTrayElem->pchIdentifier;
					if(pchString)
					{
						static InvBagIDs s_eBagID;
						static S32 s_iBagSlot, s_iItemPowIdx;
						S32 iTrayBagID = -1, iTrayBagSlot = -1, iTrayItemPowIdx = -1;
						
						if (bGetItemPower)
						{
							bGetItemPower = false;
							if (item_FindPowerByID(ePowerOwner,uiID,&s_eBagID,&s_iBagSlot,NULL,&s_iItemPowIdx))
							{
								bFoundItemPower = true;
							}
						}
						if (!bFoundItemPower)
						{
							continue;
						}

						tray_InventorySlotStringToIDs(pchString, &iTrayBagID, &iTrayBagSlot, &iTrayItemPowIdx);

						if (iTrayBagID == s_eBagID && iTrayBagSlot == s_iBagSlot && iTrayItemPowIdx == s_iItemPowIdx)
						{
							return pTrayElem;
						}
					}
				}
			}
		}
	}
	return NULL;
}

// Gets the Power given the TrayElem.  May return NULL.
Power *entity_TrayGetPower(Entity *e, TrayElem *pelem)
{
	Power *ppow = NULL;
	
	if(e->pChar && pelem)
	{
		switch (pelem->eType)
		{
			xcase kTrayElemType_Power:
			case kTrayElemType_TempPower:
			{
				ppow = character_FindPowerByID(e->pChar,(U32)pelem->lIdentifier);
			}
			xcase kTrayElemType_PowerTreeNode:
			{
				const char *pchNode = pelem->pchIdentifier;
				if(pchNode)
				{
					PTNode *pnode = character_GetNode(e->pChar,pchNode);
					if(pnode)
					{
						ppow = powertreenode_GetActivatablePower(pnode);
					}
				}
			}
			xcase kTrayElemType_InventorySlot:
			{
				const char *pchInventorySlot = pelem->pchIdentifier;
				if(pchInventorySlot)
				{
					S32 iBag = -1, iSlot = -1, iItemPowIdx = -1;
					tray_InventorySlotStringToIDs(pchInventorySlot,&iBag,&iSlot,&iItemPowIdx);
					ppow = entity_GetTrayPowerFromItem(e,iBag,iSlot,iItemPowIdx,NULL);
				}
			}
			xcase kTrayElemType_PowerSlot:
			{
				U32 uiID = character_PowerSlotGetFromTray(e->pChar, pelem->iTray, pelem->lIdentifier);
				if(uiID)
				{
					ppow = character_FindPowerByID(e->pChar,uiID);
				}
			}
			xcase kTrayElemType_PowerPropSlot:
			{
				S32 i;
				U32 uiTrayPowID = (U32)pelem->lIdentifier;

				for ( i = 0; i < eaSize(&e->pChar->ppPowersPropagation); i++ )
				{
					if ( uiTrayPowID == e->pChar->ppPowersPropagation[i]->uiID )
					{
						ppow = e->pChar->ppPowersPropagation[i];
						break;
					}
				}
			}
		}

		if (ppow)
		{
			Power *pSwitchedPower = NULL;
			pSwitchedPower = CombatPowerStateSwitching_GetSwitchedStatePower(e->pChar, ppow);
			if (pSwitchedPower)
				ppow = pSwitchedPower;
		}
		
	}

	return ppow;
}

// Gets the PowerDef given the TrayElem.  May return NULL.
PowerDef *entity_TrayGetPowerDef(Entity *e, TrayElem *pelem)
{
	Power *pPower;
	if (pelem->eType == kTrayElemType_SavedPetPower)
	{
		return powerdef_Find(pelem->pchIdentifier);
	}
	pPower = entity_TrayGetPower(e, pelem);
	return pPower ? GET_REF(pPower->hDef) : NULL;
}


// Returns the Power from the Entity's Item, based on the Bag, Slot and optional ItemPower
//  If the ItemPower is -1, it just returns the first executable Power on the Item
Power* entity_GetTrayPowerFromItem(Entity* pEnt, S32 iInvBag, S32 iInvBagSlot, S32 iItemPower, GameAccountDataExtract *pExtract)
{
	InventoryBag *pBag = CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iInvBag, pExtract));
	Item *pItem = inv_bag_GetItem(pBag,iInvBagSlot);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	Power *pPowItem = NULL;
	PowerDef *pDefItem = NULL;
	if(pItemDef)
	{
		int iMinItemLevel = item_GetMinLevel(pItem);
		
		if ((invbag_flags(pBag) & InvBagFlag_SpecialBag) || ((pItemDef->flags & kItemDefFlag_CanUseUnequipped) &&
			itemdef_VerifyUsageRestrictions(entGetPartitionIdx(pEnt), pEnt, pItemDef, iMinItemLevel, NULL, -1)))
		{
			if(iItemPower < 0)
			{
				// TODO(JW): UI: We need to scan through the item to find a Device, for now
				//  just return the first one activatable Power on the item.
				S32 i, iPowers = item_GetNumItemPowerDefs(pItem, true);
				for(i=0; i<iPowers; i++)
				{
					pPowItem = item_GetPower(pItem,i);
					pDefItem = pPowItem ? GET_REF(pPowItem->hDef) : NULL;
					iItemPower = i;
					break;
				}
			}
			else
			{
				pPowItem = item_GetPower(pItem,iItemPower);
				pDefItem = pPowItem ? GET_REF(pPowItem->hDef) : NULL;
			}
		}
	}
	if (pDefItem && 
		!pDefItem->bSlottingRequired && 
		POWERTYPE_ACTIVATABLE(pDefItem->eType) &&
		item_ItemPowerActive(pEnt, pBag, pItem, iItemPower))
	{
		return pPowItem;
	}
	return NULL;
}

// Checks to see if the bag or power tree node allow auto-attack
bool tray_CanEnableAutoAttackForElem(Entity *e, TrayElem* pElem, S32* piTrayIndex)
{
	int iTrayIndex;
	SavedTray* pTray = entity_GetActiveTray(e);
	if (!pTray || schemes_DisableTrayAutoAttack(e))
		return false;
	iTrayIndex = tray_FindTrayElem(&pTray->ppAutoAttackElems, pElem);
	if (piTrayIndex)
		(*piTrayIndex) = iTrayIndex;
	if (iTrayIndex < 0)
	{
		switch (pElem->eType)
		{
			xcase kTrayElemType_InventorySlot:
			{
				InventoryBag* pBag;
				GameAccountDataExtract *pExtract;
				int iBag = 0, iSlot = 0, iPower = 0;

				tray_InventorySlotStringToIDs(pElem->pchIdentifier, &iBag, &iSlot, &iPower);

				// Make sure this bag supports AutoAttack
				pExtract = entity_GetCachedGameAccountDataExtract(e);
				pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, e), iBag, pExtract);
				if(pBag)
				{
					InvBagDef const *pBagDef = invbag_def(pBag);
					if (pBagDef && pBagDef->bAutoAttack)
					{
						return true;
					}
				}
			}
			xcase kTrayElemType_PowerTreeNode:
			{
				const char *pchNode = pElem->pchIdentifier;
				if(pchNode)
				{
					PTNodeDef* pNodeDef = powertreenodedef_Find(pchNode);
					if (pNodeDef && (pNodeDef->eFlag & kNodeFlag_AutoAttack))
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

// Converts an inventory bag ID and slot into an InventorySlot string identifier (assumes a pre-created estring)
void tray_InventorySlotIDsToString(char **ppchInventorySlot, S32 iInvBag, S32 iInvBagSlot, S32 iItemPowerIndex)
{
	const char *pchBag = StaticDefineIntRevLookup(InvBagIDsEnum,iInvBag);
	if(pchBag)
	{
		estrPrintf(ppchInventorySlot,"%s:%d:%d",pchBag,iInvBagSlot,iItemPowerIndex);
	}
}

// Converts an InventorySlot string identifier into the bag and slot id
void tray_InventorySlotStringToIDs(const char *pchInventorySlot, S32 *piInvBagOut, S32 *piInvBagSlotOut, S32 *piItemPowerIndex)
{
	char *pchBag = NULL;
	char *pchMark = strchr(pchInventorySlot,':');
	char *pchMark2 = strchr(pchMark+1,':');
	*piInvBagSlotOut = pchMark ? atoi(pchMark+1) : -1;
	*piItemPowerIndex = pchMark2 ? atoi(pchMark2+1) : -1;
	estrStackCreate(&pchBag);
	estrConcat(&pchBag,pchInventorySlot,pchMark-pchInventorySlot);
	*piInvBagOut = StaticDefineIntGetInt(InvBagIDsEnum,pchBag);
	estrDestroy(&pchBag);
}

// Converts a PowerPropSlot string identifier to a purpose and prop slot ID
void tray_PowerPropSlotStringToSlotData(const char *pchPowerPropSlot, S32* ePurpose, U32* piPropSlotID)
{
	char *estrPurpose = NULL;
	const char *pchMark = pchPowerPropSlot ? strchr(pchPowerPropSlot,':') : NULL;
	*piPropSlotID = pchMark ? atoi(pchMark+1) : 0;
	estrStackCreate(&estrPurpose);
	if (pchMark)
	{
		estrConcat(&estrPurpose,pchPowerPropSlot,pchMark-pchPowerPropSlot);
	}
	else
	{
		estrAppend2(&estrPurpose,pchPowerPropSlot);
	}
	*ePurpose = atoi(estrPurpose);
	estrDestroy(&estrPurpose);
}

// Converts PlayerMacro data into a Macro drag string
void tray_MacroDataToDragString(SA_PARAM_NN_VALID char **pestrMacroDragString, PlayerMacro *pMacroData)
{
	if (pestrMacroDragString && pMacroData)
	{
		ParserWriteText(pestrMacroDragString, parse_PlayerMacro, pMacroData, 0, 0, 0);
	}
}

// Converts a Macro drag string into PlayerMacro data
void tray_MacroDragStringToMacroData(const char *pchMacroDragString, PlayerMacro *pMacroData)
{
	if (pchMacroDragString && pMacroData)
	{
		ParserReadText(pchMacroDragString, parse_PlayerMacro, pMacroData, 0);
	}
}

// Searches an earray of TrayElems to find a match to the given TrayElem based on type and identifier. returns
//  the index, or -1 if not found.
int tray_FindTrayElem(TrayElem*** pppElems, TrayElem *pElem)
{
	int i, s = eaSize(pppElems);

	TrayElemType eType = pElem->eType;
	S64 lIdentifier = pElem->lIdentifier;
	const char *pchIdentifier = pElem->pchIdentifier;

	for(i=0; i<s; i++)
	{
		TrayElem* pElemMatch = (*pppElems)[i];
		if(pElemMatch->eType==eType
			&& (pElemMatch->eType==kTrayElemType_InventorySlot 
			|| pElemMatch->lIdentifier==lIdentifier)
			&& !stricmp_safe(pElemMatch->pchIdentifier, pchIdentifier))
		{
			return i;
		}
	}
	return -1;
}



// Creates a TrayElem mapped to the PowerID
TrayElem *tray_CreateTrayElemForPowerID(S32 iTray, S32 iSlot, U32 uiID)
{
	TrayElem *pelem = StructCreate(parse_TrayElem);
	tray_SetTrayElemForPowerID(pelem,iTray,iSlot,uiID);
	return pelem;
}

// Creates a TrayElem mapped to the PowerTree Node
TrayElem *tray_CreateTrayElemForPowerTreeNode(S32 iTray, S32 iSlot, const char *pchName)
{
	TrayElem *pelem = StructCreate(parse_TrayElem);
	tray_SetTrayElemForPowerTreeNode(pelem,iTray,iSlot,pchName);
	return pelem;
}

// Creates a TrayElem mapped to the inventory data
TrayElem *tray_CreateTrayElemForInventorySlot(S32 iTray, S32 iSlot, S32 iInvBag, S32 iInvBagSlot, S32 iItemPowIdx, U32 uiPowID)
{
	TrayElem *pelem = StructCreate(parse_TrayElem);
	tray_SetTrayElemForInventorySlot(pelem,iTray,iSlot,iInvBag,iInvBagSlot,iItemPowIdx,uiPowID);
	return pelem;
}

TrayElem *tray_CreateTrayElemForTempPower(S32 iTray,S32 iSlot,S32 uiID, const char *pchPowerDef)
{
	TrayElem *pelem = StructCreate(parse_TrayElem);
	tray_SetTrayElemForTempPower(pelem,iTray,iSlot,uiID,pchPowerDef);
	return pelem;
}

// Creates a TrayElem mapped to the PowerSlot index
TrayElem *tray_CreateTrayElemForPowerSlot(S32 iTray, S32 iSlot, U32 uiIndex)
{
	TrayElem *pelem = StructCreate(parse_TrayElem);
	tray_SetTrayElemForPowerSlot(pelem,iTray,iSlot,uiIndex);
	return pelem;
}

// Creates a TrayElem mapped to the AlwaysPropSlot PetID and PowID
TrayElem *tray_CreateTrayElemForPowerPropSlot(S32 iTray, S32 iSlot, U32 uiPowID, S32 ePurpose, U32 uPropSlotID)
{
	TrayElem *pelem = StructCreate(parse_TrayElem);
	tray_SetTrayElemForPowerPropSlot(pelem,iTray,iSlot,uiPowID,ePurpose,uPropSlotID);
	return pelem;
}

// Creates a TrayElem mapped to a PetID and PowerDef name
TrayElem *tray_CreateTrayElemForSavedPetPower(S32 iTray, S32 iSlot, U32 uiPetID, const char *pchPowerDef)
{
	TrayElem *pelem = StructCreate(parse_TrayElem);
	tray_SetTrayElemForSavedPetPower(pelem,iTray,iSlot,uiPetID,pchPowerDef);
	return pelem;
}

// Creates a TrayElem mapped to the macro ID
TrayElem *tray_CreateTrayElemForMacro(S32 iTray, S32 iSlot, U32 uMacroID)
{
	TrayElem *pelem = StructCreate(parse_TrayElem);
	tray_SetTrayElemForMacro(pelem,iTray,iSlot,uMacroID);
	return pelem;
}

TrayElem *tray_CreateTrayElemFromOldTrayElem(const TrayElemOld* pElemOld)
{
	TrayElem *pElem = StructCreate(parse_TrayElem);
	tray_SetTrayElemFromOldTrayElem(pElem, pElemOld);
	return pElem;
}

void tray_SetTrayElemFromOldTrayElem(TrayElem* pElem, const TrayElemOld* pElemOld)
{
	pElem->eType = pElemOld->eType;
	pElem->lIdentifier = pElemOld->lIdentifier;
	pElem->pchIdentifier = strdup_ifdiff(pElemOld->pchIdentifier, pElem->pchIdentifier);
	pElem->iTray = pElemOld->iTray;
	pElem->iTraySlot = pElemOld->iTraySlot;
}

// Sets a TrayElem mapped to the PowerID
void tray_SetTrayElemForPowerID(TrayElem *pelem, S32 iTray, S32 iSlot, U32 uiID)
{
	pelem->eType = kTrayElemType_Power;
	pelem->lIdentifier = uiID;
	StructFreeStringSafe(&pelem->pchIdentifier);
	pelem->iTray = iTray;
	pelem->iTraySlot = iSlot;
}

// Sets a TrayElem mapped to the PowerTree Node
void tray_SetTrayElemForPowerTreeNode(TrayElem *pelem, S32 iTray, S32 iSlot, const char *pchName)
{
	pelem->eType = kTrayElemType_PowerTreeNode;
	pelem->lIdentifier = 0;
	pelem->pchIdentifier = strdup_ifdiff(pchName, pelem->pchIdentifier);
	pelem->iTray = iTray;
	pelem->iTraySlot = iSlot;
}

// Sets a TrayElem mapped to the inventory data
void tray_SetTrayElemForInventorySlot(TrayElem *pelem, S32 iTray, S32 iSlot, S32 iInvBag, S32 iInvBagSlot, S32 iItemPowerIdx, U32 uiPowID)
{
	pelem->eType = kTrayElemType_InventorySlot;
	pelem->lIdentifier = uiPowID;
	pelem->iTray = iTray;
	pelem->iTraySlot = iSlot;
	{
		char *pchTemp = NULL;
		estrStackCreate(&pchTemp);
		tray_InventorySlotIDsToString(&pchTemp,iInvBag,iInvBagSlot,iItemPowerIdx);
		pelem->pchIdentifier = strdup_ifdiff(pchTemp, pelem->pchIdentifier);
		estrDestroy(&pchTemp);
	}
}

void tray_SetTrayElemForTempPower(TrayElem *pelem, S32 iTray, S32 iSlot, S32 uiID, const char *pchPowerDef)
{
	char *pchTemp = NULL;
	pelem->eType = kTrayElemType_TempPower;
	pelem->lIdentifier = uiID;
	pelem->pchIdentifier = strdup_ifdiff(pchPowerDef, pelem->pchIdentifier);
	pelem->iTray = iTray;
	pelem->iTraySlot = iSlot;
}

// Sets a TrayElem mapped to the PowerSlot index
void tray_SetTrayElemForPowerSlot(TrayElem *pelem, S32 iTray, S32 iSlot, U32 uiIndex)
{
	pelem->eType = kTrayElemType_PowerSlot;
	pelem->lIdentifier = uiIndex;
	StructFreeStringSafe(&pelem->pchIdentifier);
	pelem->iTray = iTray;
	pelem->iTraySlot = iSlot;
}

// Sets a TrayElem mapped to the AlwaysPropSlot PetID and PowID
void tray_SetTrayElemForPowerPropSlot(TrayElem *pelem, S32 iTray, S32 iSlot, U32 uiPowID, S32 ePurpose, U32 uPropSlotID)
{
	char* pchSlot = NULL;
	pelem->eType = kTrayElemType_PowerPropSlot;
	pelem->lIdentifier = uiPowID;
	estrStackCreate(&pchSlot);
	estrPrintf(&pchSlot, "%d:%u", ePurpose, uPropSlotID);
	pelem->pchIdentifier = strdup_ifdiff(pchSlot, pelem->pchIdentifier);
	pelem->iTray = iTray;
	pelem->iTraySlot = iSlot;
	estrDestroy(&pchSlot);
}

// Sets a TrayElem mapped to a PetID and PowerDef name
void tray_SetTrayElemForSavedPetPower(TrayElem *pelem, S32 iTray, S32 iSlot, U32 uiPetID, const char *pchPowerDef)
{
	pelem->eType = kTrayElemType_SavedPetPower;
	pelem->lIdentifier = uiPetID;
	pelem->pchIdentifier = strdup_ifdiff(pchPowerDef, pelem->pchIdentifier);
	pelem->iTray = iTray;
	pelem->iTraySlot = iSlot;
}

// Sets a TrayElem mapped to the macro ID
void tray_SetTrayElemForMacro(TrayElem *pelem, S32 iTray, S32 iSlot, U32 uMacroID)
{
	pelem->eType = kTrayElemType_Macro;
	pelem->iTray = iTray;
	pelem->iTraySlot = iSlot;
	pelem->lIdentifier = (S64)uMacroID;
}

#include "AutoGen/Tray_h_ast.c"
