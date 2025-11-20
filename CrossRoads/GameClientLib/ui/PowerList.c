/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Character.h"
#include "CharacterAttribs.h"
#include "CharacterClass.h"
#include "cmdClient.h"
#include "cmdparse.h"
#include "EntitySavedData.h"
#include "Expression.h"
#include "GameAccountDataCommon.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "Player.h"
#include "PowerGrid.h"
#include "Powers.h"
#include "Powers_h_ast.h"
#include "PowersAutoDesc.h"
#include "PowerSlots.h"
#include "PowersUI.h"
#include "PowerTree.h"
#include "RegionRules.h"
#include "SavedPetCommon.h"
#include "StringFormat.h"
#include "Tray.h"
#include "UIGen.h"
#include "UITray.h"
#include "WorldGrid.h"

#include "gclEntity.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/PowerList_c_ast.h"
#include "AutoGen/PowersEnums_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define POWERLIST_DEFAULT_ITEMTABLE_SIZE 20

AUTO_STRUCT;
typedef struct EntityPowerData
{
	GlobalType eOwnerType;		AST(NAME(OwnerType))
	ContainerID uOwnerID;		AST(NAME(OwnerID))
	char* pchOwnerName;			AST(NAME(OwnerName))
	REF_TO(PowerDef) hDef;		AST(NAME(PowerDef))
	Power* pPower;				AST(NAME(Power) UNOWNED)
} EntityPowerData;

AUTO_STRUCT;
typedef struct PetPowerData
{
	GlobalType eOwnerType;		AST(NAME(OwnerType))
	ContainerID uOwnerID;		AST(NAME(OwnerID))
	Entity* pEnt;				AST(NAME(Ent) UNOWNED)
	REF_TO(PowerDef) hDef;		AST(REFDICT(PowerDef))
	const char* pchOwnerName;	AST(NAME(OwnerName) UNOWNED)
} PetPowerData;

AUTO_STRUCT;
typedef struct PowerListWarpItemData
{
	U64 uItemID;				AST(KEY)
	Item* pItem;				AST(NAME(Item) UNOWNED)
	InvBagIDs eLastBagID;		NO_AST
	S32 iLastBagSlot;			NO_AST
	bool bDirty;				NO_AST
} PowerListWarpItemData;

AUTO_STRUCT;
typedef struct PowerListWarpPowerData
{
	Power* pPower;						AST(NAME(Power) UNOWNED)
	REF_TO(ItemDef) hItemDef;			AST(NAME(ItemDef) REFDICT(ItemDef))
	PowerListWarpItemData** eaItemData; AST(NAME(ItemData))
	S32 iChargesLeft;					AST(NAME(ChargesLeft))
	S32 iStackCount;					AST(NAME(StackCount))
	F32 fCooldown;						AST(NAME(Cooldown))
	bool bCanActivate;					AST(NAME(CanActivate))
	bool bDirty;						NO_AST
} PowerListWarpPowerData;

AUTO_STRUCT;
typedef struct PowerListWarpData
{
	const char* pchMapName;					AST(NAME(MapName) POOL_STRING)
	const char* pchMapDisplayName;			AST(NAME(MapDisplayName) UNOWNED)
	PowerListWarpPowerData** eaPowerData;	AST(NAME(Power) UNOWNED)
	F32 fShortestCooldown;					AST(NAME(ShortestCooldown))
	S32 iNumActivatable;					AST(NAME(NumActivatable))
	S32 iNumPowers;							AST(NAME(NumPowers))
} PowerListWarpData;

AUTO_RUN;
void PowerList_Init(void)
{
	ui_GenInitStaticDefineVars(PowerTypeEnum, "PowerType_");
}

static int SortPowerByDisplayName(const void *a, const void *b)
{
	PowerDef *adef = GET_REF((*(Power**)a)->hDef);
	PowerDef *bdef = GET_REF((*(Power**)b)->hDef);
	if(adef && bdef)
	{
		return stricmp(TranslateDisplayMessage(adef->msgDisplayName),TranslateDisplayMessage(bdef->msgDisplayName));
	}
	else
	{
		return 0;
	}
}

static int SortPowerListWarpData(const PowerListWarpData** ppA, const PowerListWarpData** ppB)
{
	const PowerListWarpData* pA = (*ppA);
	const PowerListWarpData* pB = (*ppB);

	if (pA->pchMapDisplayName && pB->pchMapDisplayName)
	{
		return stricmp(pA->pchMapDisplayName, pB->pchMapDisplayName);
	}
	else
	{
		return 0;
	}
}

static Power* gclEntFindBaseReplacePower(Entity* pEnt, PowerReplaceDef* pReplaceDef)
{
	int i, j;
	for(i=0;i<eaSize(&pEnt->pChar->ppPowerTrees);i++)
	{
		PowerTree *pTree = pEnt->pChar->ppPowerTrees[i];

		for(j=0;j<eaSize(&pTree->ppNodes);j++)
		{
			PTNodeDef *pNodeDef = GET_REF(pTree->ppNodes[j]->hDef);

			if (pTree->ppNodes[j]->bEscrow)
				continue;

			if(pNodeDef && GET_REF(pNodeDef->hGrantSlot) == pReplaceDef)
			{
				return powertreenode_GetActivatablePower(pTree->ppNodes[j]);
			}
		}
	}
	return NULL;
}

//returns NULL if the power shouldn't be added
static Power* gclGetPowerToAddToList(Entity* pEnt,
									 Power* pPower,
									 ItemPowerDef* pItemPowerDef,
									 S32* pePowerTypes,
									 S32* peIncludeCategories,
									 S32* peExcludeCategories,
									 S32* peAllowedPurposes,
									 bool bActivatable, 
									 bool bFindReplacementPower)
{
	S32 i;
	Power* pBaseReplace = NULL;
	PowerDef* pDef = GET_REF(pPower->hDef);
	PowerDef* pDefReplace = NULL;

	if (!pEnt || !pEnt->pChar)
	{
		return NULL;
	}
	if (!pDef || (bActivatable && !power_DefDoesActivate(pDef)))
	{
		return NULL;
	}
	if (ea32Size(&pePowerTypes) && ea32Find(&pePowerTypes, pDef->eType) < 0)
	{
		return NULL;
	}
	if (ea32Size(&peIncludeCategories))
	{
		for (i = ea32Size(&peIncludeCategories)-1; i >= 0; i--)
		{
			if (ea32Find(&pDef->piCategories, peIncludeCategories[i]) >= 0)
			{
				break;
			}
		}
		if (i < 0)
		{
			return NULL;
		}
	}
	if (ea32Size(&peAllowedPurposes))
	{
		if (ea32Find(&peAllowedPurposes, pDef->ePurpose) == -1)
		{
			return NULL;
		}
	}
	if (ea32Size(&peExcludeCategories))
	{
		for (i = ea32Size(&peExcludeCategories)-1; i >= 0; i--)
		{
			if (ea32Find(&pDef->piCategories, peExcludeCategories[i]) >= 0)
			{
				break;
			}
		}
		if (i >= 0)
		{
			return NULL;
		}
	}
	if (pItemPowerDef)
	{
		PowerReplaceDef* pReplace = GET_REF(pItemPowerDef->hPowerReplace);
		if (pReplace)
		{
			pBaseReplace = gclEntFindBaseReplacePower(pEnt, pReplace);
			pDefReplace = SAFE_GET_REF(pBaseReplace, hDef);
		}
	}
	if ((pBaseReplace && pBaseReplace->bHideInUI) || (!pBaseReplace && pPower->bHideInUI))
	{
		return NULL;
	}
	if ((pDefReplace && pDefReplace->bHideInUI) || (!pDefReplace && pDef->bHideInUI))
	{
		return NULL;
	}
	if (bFindReplacementPower && pPower->uiReplacementID)
	{
		Power *ppowReplace = character_FindPowerByID(pEnt->pChar,pPower->uiReplacementID);
		if(ppowReplace)
		{
			pPower = ppowReplace;
		}
	}
	return pPower;
}

static bool gclAddPowerToEntityPowerList(Entity* pEnt, 
										 Power* pPower,
										 int* piCount, 
										 bool bGetPuppetName,
										 EntityPowerData*** pppEntPowers)
{
	const char* pchName;
	EntityPowerData* pData;
	PowerDef* pDef = pPower ? GET_REF(pPower->hDef) : NULL;
	if (!pDef)
	{
		return false;
	}
	if (bGetPuppetName && pEnt->pSaved && pEnt->pSaved->pPuppetMaster)
	{
		pchName = pEnt->pSaved->pPuppetMaster->curPuppetName;
	}
	else
	{
		pchName = entGetLocalName(pEnt);
	}
	pData = eaGetStruct(pppEntPowers,parse_EntityPowerData,(*piCount)++);
	pData->eOwnerType = entGetType(pEnt);
	pData->uOwnerID = entGetContainerID(pEnt);
	if (pData->pchOwnerName)
		StructFreeString(pData->pchOwnerName);
	pData->pchOwnerName = StructAllocString(pchName);
	pData->pPower = pPower;
	REMOVE_HANDLE(pData->hDef);
	return true;
}

static void gclEntAddPowerTreePowers(Entity* pEnt, Entity* pSubject,
									 S32* pePowerTypes,
									 S32* peIncludeCategories,
									 S32* peExcludeCategories,
									 bool bActivatable, 
									 int* piCount,
									 EntityPowerData*** pppEntPowers,
									 Power*** pppPowers)
{
	int i, j, k;
	if (!pSubject->pChar)
		return;
	for (i = eaSize(&pSubject->pChar->ppPowerTrees)-1; i >= 0; i--)
	{
		PowerTree* pTree = pSubject->pChar->ppPowerTrees[i];
		for (j = eaSize(&pTree->ppNodes)-1; j >= 0; j--)
		{
			PTNode* pNode = pTree->ppNodes[j];

			if (pNode->bEscrow || eaSize(&pNode->ppPowers) == 0)
				continue;

			if(pNode->ppPowers[0])
			{
				PowerDef* pDef = GET_REF(pNode->ppPowers[0]->hDef);
				if(pDef && pDef->eType == kPowerType_Enhancement)
				{
					Power* pPower = pNode->ppPowers[eaSize(&pNode->ppPowers)-1];
					pPower = gclGetPowerToAddToList(pEnt,
													pPower,
													NULL,
													pePowerTypes,
													peIncludeCategories,
													peExcludeCategories,
													NULL,
													bActivatable,
													false);
					if (pppEntPowers)
					{
						gclAddPowerToEntityPowerList(pSubject,pPower,piCount,false,pppEntPowers);
					}
					else if (pppPowers && pPower)
					{
						if (piCount)
						{
							(*piCount)++;
						}
						eaPush(pppPowers, pPower);
					}
					continue;
				}
			}

			for (k = eaSize(&pNode->ppPowers)-1; k >= 0; k--)
			{
				Power* pPower = pNode->ppPowers[k];
				PowerDef* pDef = pPower ? GET_REF(pPower->hDef) : NULL;

				if (!pDef)
					continue;

				pPower = gclGetPowerToAddToList(pEnt,
												pPower,
												NULL,
												pePowerTypes,
												peIncludeCategories,
												peExcludeCategories,
												NULL,
												bActivatable,
												false);
				if (pppEntPowers)
				{
					gclAddPowerToEntityPowerList(pSubject,pPower,piCount,false,pppEntPowers);
				}
				else if (pppPowers && pPower)
				{
					if (piCount)
					{
						(*piCount)++;
					}
					eaPush(pppPowers, pPower);
				}

				if(!pDef || pDef->eType!=kPowerType_Enhancement)
				{
					break;
				}
			}
		}
	}
}

static void gclEntAddItemPowers(Entity* pEnt,
								Entity* pSubject, 
								S32* pePowerTypes,
								S32* peIncludeCategories,
								S32* peExcludeCategories,
								bool bActivatable, 
								int* piCount,
								EntityPowerData*** pppEntPowers,
								Power*** pppPowers)
{
	int i, k;
	if (!pSubject->pInventoryV2)
		return;
	for (i = eaSize(&pSubject->pInventoryV2->ppInventoryBags)-1; i >= 0; i--)
	{
		InventoryBag* pBag = pSubject->pInventoryV2->ppInventoryBags[i];
		bool bEquipBag = (invbag_flags(pBag) & (InvBagFlag_EquipBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag))!=0;
		BagIterator *iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pBag));
		for (; !bagiterator_Stopped(iter); bagiterator_Next(iter))
		{
			Item *pItem  = (Item*)bagiterator_GetItem(iter);
			ItemDef *pItemDef = bagiterator_GetDef(iter);
			int iNumPowers = item_GetNumItemPowerDefs(pItem, true);
			int iMinItemLevel = item_GetMinLevel(pItem);

			if (!(invbag_flags(pBag) & InvBagFlag_SpecialBag) && (!pItemDef || !(pItemDef->flags & kItemDefFlag_CanUseUnequipped) ||
				!itemdef_VerifyUsageRestrictions(PARTITION_CLIENT, pSubject, pItemDef, iMinItemLevel, NULL, -1)))
			{
				continue;
			}
			for (k = 0; k < iNumPowers; k++)
			{
				Power* pPower = item_GetPower(pItem, k);
				ItemPowerDef* pItemPowerDef = item_GetItemPowerDef(pItem, k);
				PowerDef* pPowerDef = pPower ? GET_REF(pPower->hDef) : NULL;
				if (!pPower || !pPowerDef)
				{
					continue;
				}
				if (pBag->BagID == InvBagIDs_ItemSet) // Special logic for item sets
				{
					U32 uSetCount = SAFE_MEMBER(pItem, uSetCount);
					ItemPowerDefRef* pItemPowerDefRef = pItemDef ? eaGet(&pItemDef->ppItemPowerDefRefs, k) : NULL;
					if (!pItemPowerDefRef || uSetCount < pItemPowerDefRef->uiSetMin)
					{
						continue;
					}
				}
				else if (!bEquipBag && !item_ItemPowerActive(pEnt, pBag, pItem, k))
				{
					// if the bag isn't an equip bag, and the power is not usable (item_ItemPowerActive
					// does not actually check to see if it's active it seems)
					continue;
				}
				pPower = gclGetPowerToAddToList(pEnt,
												pPower,
												pItemPowerDef,
												pePowerTypes,
												peIncludeCategories,
												peExcludeCategories,
												NULL,
												bActivatable,
												false);
				if (pppEntPowers)
				{
					gclAddPowerToEntityPowerList(pSubject,pPower,piCount,true,pppEntPowers);
				}
				else if (pppPowers && pPower)
				{
					if (piCount)
					{
						(*piCount)++;
					}
					eaPush(pppPowers, pPower);
				}
			}
		}
		bagiterator_Destroy(iter);
	}
}

static int gclPowerListFindPowerWithMode(EntityPowerData** ppEntPowers, S32 eModeRequire)
{
	int i, j;
	for (i = eaSize(&ppEntPowers)-1; i >= 0; i--)
	{
		Power* pCheckPower = ppEntPowers[i]->pPower;
		PowerDef* pCheckDef = GET_REF(pCheckPower->hDef);
		if (!pCheckDef)
		{
			continue;
		}
		for (j = eaSize(&pCheckDef->ppOrderedMods)-1; j >= 0; j--)
		{
			AttribModDef* pModDef = pCheckDef->ppOrderedMods[j];
			if (pModDef->offAttrib == kAttribType_PowerMode)
			{
				PowerModeParams* pParams = (PowerModeParams*)(pModDef->pParams);
				if (pParams->iPowerMode == eModeRequire)
				{
					return i;
				}
			}
		}
	}
	return -1;
}

static int SortEntityPowers(const EntityPowerData** ppA, const EntityPowerData** ppB)
{
	const EntityPowerData* pA = (*ppA);
	const EntityPowerData* pB = (*ppB);
	PowerDef* pDefA;
	PowerDef* pDefB;
	int iOwnerNameCmp = stricmp(pA->pchOwnerName, pB->pchOwnerName);

	if (iOwnerNameCmp)
	{
		return iOwnerNameCmp;
	}
	pDefA = GET_REF(pA->hDef);
	if (!pDefA)
		return 1;
	pDefB = GET_REF(pB->hDef);
	if (!pDefB)
		return -1;

	return stricmp(TranslateDisplayMessage(pDefA->msgDisplayName),
				   TranslateDisplayMessage(pDefB->msgDisplayName));
}

static void gclEntFixupPowerData(Entity* pEnt,
								 const char* pchNodeDef, 
								 S32 iCount, 
								 bool bOnlyPowersAffectedByAttribs, 
								 bool bRemoveDuplicates,
								 EntityPowerData*** pppEntPowers)
{
	S32 i, j, k;
	U32* eaAttribs = NULL;
	PTNodeDef* pNodeDef = NULL;

	if (pchNodeDef && pchNodeDef[0])
	{
		pNodeDef = RefSystem_ReferentFromString("PTNodeDef", pchNodeDef);
	}
	for (i = iCount-1; i >= 0; i--)
	{
		EntityPowerData* pEntPowerData = (*pppEntPowers)[i];
		Power* pPower = pEntPowerData->pPower;
		PowerDef* pDef = GET_REF(pPower->hDef);
		if (!pDef)
		{
			continue;
		}
		if (eaSize(&pDef->ppCombos))
		{
			for (j = 0; j < eaSize(&pDef->ppCombos); j++)
			{
				PowerCombo* pCombo = pDef->ppCombos[j];
				PowerDef* pComboDef = GET_REF(pCombo->hPower);
				if (!pComboDef)
				{
					continue;
				}
				ea32Clear(&eaAttribs);
				if (bOnlyPowersAffectedByAttribs
					&& !GetAttributesAffectingPower(pComboDef, entGetLanguage(pEnt), &eaAttribs))
				{
					continue;
				}
				if (pNodeDef && !gclNodeMatchesAttributeAffectingPower(pNodeDef, pComboDef))
				{
					continue;
				}
				for (k = ea32Size(&pCombo->piModeRequire)-1; k >= 0; k--)
				{
					S32 eModeRequire = pCombo->piModeRequire[k];
					if (gclPowerListFindPowerWithMode(*pppEntPowers, eModeRequire) < 0)
					{
						break;
					}
				}
				if (k < 0)
				{
					EntityPowerData* pData;
					if (!GET_REF(pEntPowerData->hDef))
					{
						pData = pEntPowerData;
					}
					else
					{
						pData = eaGetStruct(pppEntPowers, parse_EntityPowerData, iCount++);
						pData->eOwnerType = pEntPowerData->eOwnerType;
						pData->uOwnerID = pEntPowerData->uOwnerID;
						if (pData->pchOwnerName)
							StructFreeString(pData->pchOwnerName);
						pData->pchOwnerName = StructAllocString(pEntPowerData->pchOwnerName);
						pData->pPower = pEntPowerData->pPower;
					}
					SET_HANDLE_FROM_REFERENT("PowerDef", pComboDef, pData->hDef);
				}
			}
		}
		else
		{
			ea32Clear(&eaAttribs);
			if (bOnlyPowersAffectedByAttribs
				&& !GetAttributesAffectingPower(pDef, entGetLanguage(pEnt), &eaAttribs))
			{
				continue;
			}
			if (!pNodeDef || gclNodeMatchesAttributeAffectingPower(pNodeDef, pDef))
			{
				SET_HANDLE_FROM_REFERENT("PowerDef", pDef, pEntPowerData->hDef);
			}
		}
	}
	eaSetSizeStruct(pppEntPowers, parse_EntityPowerData, iCount);
	for (i = iCount-1; i >= 0; i--)
	{
		if (!GET_REF((*pppEntPowers)[i]->hDef))
		{
			StructDestroy(parse_EntityPowerData,eaRemoveFast(pppEntPowers, i));
		}
	}
	if (eaSize(pppEntPowers))
	{
		eaQSort(*pppEntPowers,SortEntityPowers);
	}
	if (bRemoveDuplicates)
	{
		iCount = eaSize(pppEntPowers);
		for (i = iCount-1; i >= 1; i--)
		{
			PowerDef* pDefA = GET_REF((*pppEntPowers)[i]->hDef);
			PowerDef* pDefB = GET_REF((*pppEntPowers)[i-1]->hDef);

			if (pDefA == pDefB)
			{
				StructDestroy(parse_EntityPowerData, eaRemove(pppEntPowers, i));
			}
			else
			{
				const char* pchDisplayNameA = TranslateDisplayMessage(pDefA->msgDisplayName);
				const char* pchDisplayNameB = TranslateDisplayMessage(pDefB->msgDisplayName);
				if (stricmp(pchDisplayNameA, pchDisplayNameB) == 0)
				{
					StructDestroy(parse_EntityPowerData, eaRemove(pppEntPowers, i));
				}
			}
		}
	}
	ea32Destroy(&eaAttribs);
}

static void gclPowerListFilter(Entity* pEntity,
							   Power** ppPowersIn, 
							   Power*** pppPowersOut,
							   EntityPowerData*** pppEntPowersOut,
							   S32* piCount,
							   S32* pePowerTypes,
							   S32* peIncludeCategories,
							   S32* peExcludeCategories,
							   S32* peAllowedPurposes,
							   bool bActivatable,
							   bool bIncludeItemPowers,
							   bool bSort)
{
	S32 i;
	for (i = 0; i < eaSize(&ppPowersIn); i++)
	{
		Power* pPower = ppPowersIn[i];
		if (pPower->eSource == kPowerSource_Item && !bIncludeItemPowers)
		{
			continue;
		}
		
		pPower = gclGetPowerToAddToList(pEntity, 
									    pPower, 
									    NULL, 
									    pePowerTypes, 
									    peIncludeCategories, 
										peExcludeCategories, 
										peAllowedPurposes,
									    bActivatable,
									    true);
		if (pPower)
		{
			if (pppPowersOut)
			{
				eaPush(pppPowersOut, pPower);
			}
			else if (pppEntPowersOut)
			{
				gclAddPowerToEntityPowerList(pEntity,pPower,piCount,false,pppEntPowersOut);
			}
		}
	}
	if (bSort)
	{
		if (pppPowersOut && eaSize(pppPowersOut) > 1)
		{
			qsort(*pppPowersOut,eaSize(pppPowersOut),sizeof(Power*),SortPowerByDisplayName);
		}
		else if (pppEntPowersOut && eaSize(pppEntPowersOut) > 1)
		{
			eaSetSizeStruct(pppEntPowersOut, parse_EntityPowerData, *piCount);
			eaQSort(*pppEntPowersOut, SortEntityPowers);
		}
	}
}

static void gclEntGetCompletePowersListForSpecificEntity(SA_PARAM_OP_VALID Entity* pEnt,
														 SA_PARAM_OP_VALID Entity* pSpecificEnt,
														 const char* pchIgnoreCategory,
														 const char* pchNodeDef,
														 bool bActivatable,
														 bool bOnlyPowersAffectedByAttribs,
														 bool bRemoveDuplicates,
														 bool bIsPuppet,
														 EntityPowerData*** pppEntPowers)
{
	S32 iCount = 0;

	if (pEnt && pEnt->pChar && pEnt->pSaved && pSpecificEnt && pSpecificEnt->pChar)
	{
		RegionRules* pRegionRules = NULL;
		S32* eaExcludeCategories = NULL;
		S32 eIgnoreCategory = StaticDefineIntGetInt(PowerCategoriesEnum, pchIgnoreCategory);
		if (eIgnoreCategory >= 0)
		{
			ea32Push(&eaExcludeCategories, eIgnoreCategory);
		}

		gclEntAddPowerTreePowers(pEnt, pSpecificEnt,
								 NULL, NULL, eaExcludeCategories,
								 bActivatable,
								 &iCount,
								 pppEntPowers,
								 NULL);
		gclEntAddItemPowers(pEnt, pSpecificEnt,
							NULL, NULL, eaExcludeCategories,
							bActivatable,
							&iCount,
							pppEntPowers, 
							NULL);
		
		if (bIsPuppet)
		{
			
			CharacterClass* pClass = GET_REF(pSpecificEnt->pChar->hClass);
			if (pClass)
			{
				pRegionRules = getValidRegionRulesForCharacterClassType(pClass->eType);
			}
		}
		if (pRegionRules)
		{
			ea32PushArray(&eaExcludeCategories, &pRegionRules->piCategoryDoNotAdd);
			gclEntAddPowerTreePowers(pEnt,pEnt,NULL,NULL,eaExcludeCategories,bActivatable,&iCount,pppEntPowers,NULL);
			gclEntAddItemPowers(pEnt,pEnt,NULL,NULL,eaExcludeCategories,bActivatable,&iCount,pppEntPowers,NULL);
			gclPowerListFilter(pEnt,(Power**)pEnt->pChar->ppPowersPersonal,NULL,pppEntPowers,&iCount,NULL,NULL,eaExcludeCategories,NULL,bActivatable,true,false);
			gclPowerListFilter(pEnt,(Power**)pEnt->pChar->ppPowersClass,NULL,pppEntPowers,&iCount,NULL,NULL,eaExcludeCategories,NULL,bActivatable,true,false);
			gclPowerListFilter(pEnt,(Power**)pEnt->pChar->ppPowersSpecies,NULL,pppEntPowers,&iCount,NULL,NULL,eaExcludeCategories,NULL,bActivatable,true,false);
			gclPowerListFilter(pEnt,(Power**)pEnt->pChar->ppPowersTemporary,NULL,pppEntPowers,&iCount,NULL,NULL,eaExcludeCategories,NULL,bActivatable,true,false);
		}
		ea32Destroy(&eaExcludeCategories);
	}

	gclEntFixupPowerData(pEnt, pchNodeDef, iCount, bOnlyPowersAffectedByAttribs, bRemoveDuplicates, pppEntPowers);
}

static void gclEntGetCompletePowersList(SA_PARAM_OP_VALID Entity* pEnt,
										const char* pchIgnoreCategory,
										const char* pchNodeDef,
										bool bActivatable, 
										bool bActivePuppetsOnly,
										bool bOnlyPowersAffectedByAttribs,
										bool bRemoveDuplicates,
										EntityPowerData*** pppEntPowers)
{
	S32 i, iCount = 0;

	if (pEnt && pEnt->pSaved)
	{
		S32* eaExcludeCategories = NULL;
		S32 eIgnoreCategory = StaticDefineIntGetInt(PowerCategoriesEnum, pchIgnoreCategory);
		if (eIgnoreCategory >= 0)
		{
			ea32Push(&eaExcludeCategories, eIgnoreCategory);
		}

		gclEntAddPowerTreePowers(pEnt,pEnt,
								 NULL, NULL, eaExcludeCategories,
								 bActivatable,
								 &iCount,
								 pppEntPowers,
								 NULL);
		gclEntAddItemPowers(pEnt,pEnt,
							NULL, NULL, eaExcludeCategories,
							bActivatable,
							&iCount,
							pppEntPowers,
							NULL);

		//Add powers from pet items and power trees
		for (i = eaSize(&pEnt->pSaved->ppOwnedContainers)-1; i >= 0; i--)
		{
			PetRelationship* pPet = pEnt->pSaved->ppOwnedContainers[i];
			PuppetEntity* pPuppet = SavedPet_GetPuppetFromPet(pEnt, pPet);
			Entity* pPetEnt;
			if (pPuppet)
			{
				if ((bActivePuppetsOnly && pPuppet->eState != PUPPETSTATE_ACTIVE)
					|| pPuppet->curID == pEnt->pSaved->pPuppetMaster->curID)
				{
					continue;
				}
				pPetEnt = SavedPuppet_GetEntity(PARTITION_CLIENT, pPuppet);
			}
			else
			{
				pPetEnt = SavedPet_GetEntity(PARTITION_CLIENT, pPet);
			}
			if (pPetEnt)
			{
				gclEntAddPowerTreePowers(pEnt, pPetEnt,
											NULL, NULL, eaExcludeCategories,
											bActivatable,
											&iCount,
											pppEntPowers,
											NULL);
				gclEntAddItemPowers(pEnt, pPetEnt,
									NULL, NULL, eaExcludeCategories,
									bActivatable,
									&iCount,
									pppEntPowers, 
									NULL);
			}
		}
		ea32Destroy(&eaExcludeCategories);
	}

	gclEntFixupPowerData(pEnt, pchNodeDef, iCount, bOnlyPowersAffectedByAttribs, bRemoveDuplicates, pppEntPowers);
}

static void gclEntGetPowersList(SA_PARAM_OP_VALID Entity *pEnt, 
								bool bActivatable, 
								bool bIncludeItemPowers,
								Power*** pppPowers)
{
	eaClear(pppPowers);
	if (pEnt && pEnt->pChar)
	{
		gclPowerListFilter(pEnt, 
			pEnt->pChar->ppPowers, 
			pppPowers, 
			NULL, NULL, NULL, NULL, NULL, NULL,
			bActivatable, 
			bIncludeItemPowers,
			true);
	}
}

static bool gclGetPowerPurposesFromString(ExprContext *pContext, 
	const char* pchPowerPurposes, 
	S32** peaPurposes)
{
	char* pchContext;
	char* pchStart;
	char* pchPurposesCopy;
	if (!pchPowerPurposes || !pchPowerPurposes[0])
	{
		return false;
	}
	strdup_alloca(pchPurposesCopy, pchPowerPurposes);
	pchStart = strtok_r(pchPurposesCopy, " ,\t\r\n", &pchContext);
	do
	{
		if (pchStart)
		{
			PowerPurpose ePurpose = StaticDefineIntGetInt(PowerPurposeEnum,pchStart);
			if (ePurpose != -1)
			{
				ea32Push(peaPurposes, ePurpose);
			}
			else
			{
				const char* pchBlameFile = exprContextGetBlameFile(pContext);
				ErrorFilenamef(pchBlameFile, "Power Purpose %s not recognized", pchStart);
			}
		}
	} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	return true;
}

static bool gclGetPowerSourcesFromString(ExprContext *pContext, 
	const char* pchPowerSources, 
	S32** peaSources)
{
	char* pchContext;
	char* pchStart;
	char* pchSourcesCopy;
	if (!pchPowerSources || !pchPowerSources[0])
	{
		return false;
	}
	strdup_alloca(pchSourcesCopy, pchPowerSources);
	pchStart = strtok_r(pchSourcesCopy, " ,\t\r\n", &pchContext);
	do
	{
		if (pchStart)
		{
			PowerSource eSource = StaticDefineIntGetInt(PowerSourceEnum,pchStart);
			if (eSource != -1)
			{
				ea32Push(peaSources, eSource);
			}
			else
			{
				const char* pchBlameFile = exprContextGetBlameFile(pContext);
				ErrorFilenamef(pchBlameFile, "Power Source %s not recognized", pchStart);
			}
		}
	} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	return true;
}

static bool gclGetPowerCategoriesFromString(ExprContext *pContext, 
											const char* pchCategories, 
											S32** peaCategories)
{
	char* pchContext;
	char* pchStart;
	char* pchCategoriesCopy;
	if (!pchCategories || !pchCategories[0])
	{
		return false;
	}
	strdup_alloca(pchCategoriesCopy, pchCategories);
	pchStart = strtok_r(pchCategoriesCopy, " ,\t\r\n", &pchContext);
	do
	{
		if (pchStart)
		{
			S32 eCategory = StaticDefineIntGetInt(PowerCategoriesEnum,pchStart);
			if (eCategory != -1)
			{
				ea32Push(peaCategories, eCategory);
			}
			else
			{
				const char* pchBlameFile = exprContextGetBlameFile(pContext);
				ErrorFilenamef(pchBlameFile, "Power Category %s not recognized", pchStart);
			}
		}
	} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	return true;
}

static bool gclGetPowerTypesFromString(ExprContext *pContext, 
									   const char* pchTypes, 
									   S32** peaTypes)
{
	char* pchContext;
	char* pchStart;
	char* pchTypesCopy;
	if (!pchTypes || !pchTypes[0])
	{
		return false;
	}
	strdup_alloca(pchTypesCopy, pchTypes);
	pchStart = strtok_r(pchTypesCopy, " ,\t\r\n", &pchContext);
	do
	{
		if (pchStart)
		{
			S32 eType = StaticDefineIntGetInt(PowerTypeEnum,pchStart);
			if (eType != -1)
			{
				ea32Push(peaTypes, eType);
			}
			else
			{
				const char* pchBlameFile = exprContextGetBlameFile(pContext);
				ErrorFilenamef(pchBlameFile, "Power Type %s not recognized", pchStart);
			}
		}
	} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	return true;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowersAffectedByNode");
void exprEntGetPowersAffectedByNode(SA_PARAM_NN_VALID UIGen *pGen, 
									SA_PARAM_OP_VALID Entity *pEntity, 
									const char* pchNodeDef,
									const char* pchIgnoreCategory,
									bool bActivatable,
									bool bActivePuppetsOnly,
									bool bRegenList)
{
	EntityPowerData*** pppEntPowers = ui_GenGetManagedListSafe(pGen, EntityPowerData);
	if (bRegenList)
	{
		gclEntGetCompletePowersList(pEntity,
									pchIgnoreCategory,
									pchNodeDef,
									bActivatable, 
									bActivePuppetsOnly,
									true,
									false,
									pppEntPowers);
	}
	ui_GenSetManagedListSafe(pGen, pppEntPowers, EntityPowerData, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetCompletePowerList");
void exprEntGetCompletePowerList(SA_PARAM_NN_VALID UIGen *pGen, 
								 SA_PARAM_OP_VALID Entity *pEntity, 
								 const char* pchIgnoreCategory,
								 bool bActivatable, 
								 bool bOnlyPowersAffectedByAttribs,
								 bool bActivePuppetsOnly,
								 bool bRegenList)
{
	EntityPowerData*** pppEntPowers = ui_GenGetManagedListSafe(pGen, EntityPowerData);
	if (bRegenList)
	{
		gclEntGetCompletePowersList(pEntity,
									pchIgnoreCategory,
									NULL,
									bActivatable, 
									bActivePuppetsOnly,
									bOnlyPowersAffectedByAttribs,
									false,
									pppEntPowers);
	}
	ui_GenSetManagedListSafe(pGen, pppEntPowers, EntityPowerData, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetCompletePowerListEx");
void exprEntGetCompletePowerListEx(SA_PARAM_NN_VALID UIGen *pGen, 
								   SA_PARAM_OP_VALID Entity *pEntity,
								   U32 uSpecificEntID,
								   const char* pchIgnoreCategory,
								   bool bActivatable, 
								   bool bOnlyPowersAffectedByAttribs,
								   bool bActivePuppetsOnly,
								   bool bRegenList)
{
	EntityPowerData*** pppEntPowers = (EntityPowerData***)ui_GenGetManagedList(pGen, parse_EntityPowerData);
	if (bRegenList)
	{
		PetRelationship* pPet = SavedPet_GetPetFromContainerID(pEntity, uSpecificEntID, false);
		Entity* pSpecificEnt = pPet ? GET_REF(pPet->hPetRef) : NULL;
		
		if (pSpecificEnt)
		{
			bool bIsPuppet = SavedPet_IsPetAPuppet(pEntity, pPet);
			gclEntGetCompletePowersListForSpecificEntity(pEntity, 
														 pSpecificEnt,
														 pchIgnoreCategory,
														 NULL,
														 bActivatable, 
														 bOnlyPowersAffectedByAttribs,
														 true,
														 bIsPuppet,
														 pppEntPowers);
		}
		else
		{
			eaClearStruct(pppEntPowers, parse_EntityPowerData);
		}
	}
	ui_GenSetManagedListSafe(pGen, pppEntPowers, EntityPowerData, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowers");
void exprEntGetPowers(SA_PARAM_NN_VALID UIGen *pGen,
	SA_PARAM_OP_VALID Entity *pEntity, bool bActivatable)
{
	Power*** pppPowers = ui_GenGetManagedListSafe(pGen, Power);
	gclEntGetPowersList(pEntity, bActivatable, true, pppPowers);
	ui_GenSetManagedListSafe(pGen, pppPowers, Power, false);
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowersWithPurpose");
void exprEntGetPowersWithPurpose(ExprContext* pContext, SA_PARAM_NN_VALID UIGen *pGen,
	SA_PARAM_OP_VALID Entity *pEntity, const char* pchPurposes)
{
	Power*** pppPowers = ui_GenGetManagedListSafe(pGen, Power);
	static S32* s_pePurposes = NULL;

	ea32ClearFast(&s_pePurposes);
	gclGetPowerPurposesFromString(pContext, pchPurposes, &s_pePurposes);

	eaClear(pppPowers);
	if (pEntity && pEntity->pChar)
	{
		gclPowerListFilter(pEntity, 
			pEntity->pChar->ppPowers, 
			pppPowers, 
			NULL, NULL, NULL, NULL, NULL, s_pePurposes,
			false, 
			true,
			true);
	}
	ui_GenSetManagedListSafe(pGen, pppPowers, Power, false);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowersNoItems");
void exprEntGetPowersNoItems(SA_PARAM_NN_VALID UIGen *pGen,
					  SA_PARAM_OP_VALID Entity *pEntity, bool bActivatable)
{
	Power*** pppPowers = ui_GenGetManagedListSafe(pGen, Power);
	gclEntGetPowersList(pEntity, bActivatable, false, pppPowers);
	ui_GenSetManagedListSafe(pGen, pppPowers, Power, false);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowersByTypeCategoryAndSource");
void exprEntGetPowersByTypeCategoryAndSource(ExprContext* pContext,
											 SA_PARAM_NN_VALID UIGen* pGen, 
											 SA_PARAM_OP_VALID Entity* pEntity, 
											 const char* pchPowerTypes,
											 const char* pchIncludeCategories,
											 const char* pchExcludeCategories,
											 const char* pchPowerSources)
{
	static S32* s_peTypes = NULL;
	static S32* s_peIncludeCategories = NULL;
	static S32* s_peExcludeCategories = NULL;
	static S32* s_pePowerSources = NULL;
	Power*** pppPowers = ui_GenGetManagedListSafe(pGen, Power);
	ea32ClearFast(&s_peTypes);
	ea32ClearFast(&s_peIncludeCategories);
	ea32ClearFast(&s_peExcludeCategories);
	ea32ClearFast(&s_pePowerSources);
	eaClearFast(pppPowers);

	if (pEntity && pEntity->pChar)
	{
		S32 i;
		gclGetPowerTypesFromString(pContext, pchPowerTypes, &s_peTypes);
		gclGetPowerCategoriesFromString(pContext, pchIncludeCategories, &s_peIncludeCategories);
		gclGetPowerCategoriesFromString(pContext, pchExcludeCategories, &s_peExcludeCategories);
		gclGetPowerSourcesFromString(pContext, pchPowerSources, &s_pePowerSources);

		if (pEntity->pSaved)
		{
			for (i = eaSize(&pEntity->pSaved->ppOwnedContainers)-1; i >= 0; i--)
			{
				PetRelationship* pPet = pEntity->pSaved->ppOwnedContainers[i];
				PuppetEntity* pPuppet = SavedPet_GetPuppetFromPet(pEntity, pPet);
				Entity* pPetEnt = NULL;
				if (pPuppet)
				{
					if (pPuppet->eState != PUPPETSTATE_ACTIVE)
					{
						continue;
					}
					if (pPuppet->curID != pEntity->pSaved->pPuppetMaster->curID)
					{
						pPetEnt = SavedPuppet_GetEntity(PARTITION_CLIENT, pPuppet);
					}
				}
				else
				{
					pPetEnt = SavedPet_GetEntity(PARTITION_CLIENT, pPet);
				}
				if (!pPetEnt)
				{
					continue;
				}
				if (!ea32Size(&s_pePowerSources) || ea32Find(&s_pePowerSources, kPowerSource_PowerTree) >= 0)
				{
					gclEntAddPowerTreePowers(pEntity, pPetEnt, 
						s_peTypes, s_peIncludeCategories, s_peExcludeCategories,
						false, NULL, NULL, pppPowers);
				}
				if (!ea32Size(&s_pePowerSources) || ea32Find(&s_pePowerSources, kPowerSource_Item) >= 0)
				{
					gclEntAddItemPowers(pEntity, pPetEnt, 
						s_peTypes, s_peIncludeCategories, s_peExcludeCategories,
						false, NULL, NULL, pppPowers);
				}
			}
		}
		if (!ea32Size(&s_pePowerSources) || ea32Find(&s_pePowerSources, kPowerSource_PowerTree) >= 0)
		{
			gclEntAddPowerTreePowers(pEntity, pEntity, 
				s_peTypes, s_peIncludeCategories, s_peExcludeCategories,
				false, NULL, NULL, pppPowers);
		}
		if (!ea32Size(&s_pePowerSources) || ea32Find(&s_pePowerSources, kPowerSource_Item) >= 0)
		{
			gclEntAddItemPowers(pEntity, pEntity, 
				s_peTypes, s_peIncludeCategories, s_peExcludeCategories,
				false, NULL, NULL, pppPowers);
		}
		if (!ea32Size(&s_pePowerSources) || ea32Find(&s_pePowerSources, kPowerSource_Personal) >= 0)
		{
			gclPowerListFilter(pEntity, 
							   (Power**)pEntity->pChar->ppPowersPersonal, 
							   pppPowers,
							   NULL, NULL,
							   s_peTypes, 
							   s_peIncludeCategories, 
							   s_peExcludeCategories,
							   NULL,
							   false, true, false);
		}
		if (!ea32Size(&s_pePowerSources) || ea32Find(&s_pePowerSources, kPowerSource_Class) >= 0)
		{
			gclPowerListFilter(pEntity, 
							   (Power**)pEntity->pChar->ppPowersClass, 
							   pppPowers,
							   NULL, NULL,
							   s_peTypes, 
							   s_peIncludeCategories, 
							   s_peExcludeCategories,
							   NULL,
							   false, true, false);
		}
		if (!ea32Size(&s_pePowerSources) || ea32Find(&s_pePowerSources, kPowerSource_Species) >= 0)
		{
			gclPowerListFilter(pEntity, 
							   (Power**)pEntity->pChar->ppPowersSpecies, 
							   pppPowers,
							   NULL, NULL,
							   s_peTypes, 
							   s_peIncludeCategories, 
							   s_peExcludeCategories,
							   NULL,
							   false, true, false);
		}
		if (!ea32Size(&s_pePowerSources) || ea32Find(&s_pePowerSources, kPowerSource_Temporary) >= 0)
		{
			gclPowerListFilter(pEntity, 
							   (Power**)pEntity->pChar->ppPowersTemporary, 
							   pppPowers,
							   NULL, NULL,
							   s_peTypes, 
							   s_peIncludeCategories, 
							   s_peExcludeCategories,
							   NULL,
							   false, true, true);
		}
	}
	ui_GenSetManagedListSafe(pGen, pppPowers, Power, false);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowersByTypeAndCategory");
void exprEntGetPowersByTypeAndCategory(ExprContext* pContext,
									   SA_PARAM_NN_VALID UIGen* pGen, 
									   SA_PARAM_OP_VALID Entity* pEntity, 
									   const char* pchPowerTypes,
									   const char* pchIncludeCategories,
									   const char* pchExcludeCategories)
{
	exprEntGetPowersByTypeCategoryAndSource(pContext, 
											pGen, 
											pEntity, 
											pchPowerTypes, 
											pchIncludeCategories, 
											pchExcludeCategories,
											NULL);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntBindPowerIDToTray");
void exprEntBindPowerIDToTray(SA_PARAM_OP_VALID Entity *pEntity,
							  int iTray,
							  int iSlot,
							  int iID)
{
	TrayElem *pelem = pEntity ? entity_TrayGetTrayElemByPowerID(pEntity,(U32)iID,kTrayElemOwner_Self) : NULL;
	if(pelem)
	{
		globCmdParsef("TrayElemDestroy %d %d",pelem->iTray,pelem->iTraySlot);
	}
	globCmdParsef("TrayElemDestroy %d %d",iTray,iSlot);
	ServerCmd_TrayElemCreatePower(iTray,iSlot,iID,false);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntBindPowerIDToPowerSlot");
void exprEntBindPowerIDToPowerSlot(SA_PARAM_OP_VALID Entity *pEntity,
								   int iSlot,
								   int iID)
{
	if (iID)
	{
		if(pEntity && pEntity->pChar)
		{
			S32 iSlotOld = character_PowerIDSlot(pEntity->pChar,(U32)iID);
			if(iSlotOld >= 0)
			{
				ServerCmd_Power_SlotSwap(iSlot,iSlotOld);
			}
			else
			{
				ServerCmd_Power_Slot(iSlot,(U32)iID);
			}
		}
		else
		{
			ServerCmd_Power_Slot(iSlot,(U32)iID);
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetXBoxButtonForPowerID");
const char* exprEntGetXBoxButtonForPowerID(SA_PARAM_OP_VALID Entity *pEntity, int iID)
{
	if(pEntity && pEntity->pChar)
	{
		S32 iSlot = character_PowerIDSlot(pEntity->pChar,(U32)iID);
		switch(iSlot)
		{
		case 0:
			return UI_XBOX_XB;
		case 1:
			return UI_XBOX_YB;
		case 2:
			return UI_XBOX_BB;
		case 3:
			return UI_XBOX_XB;
		case 4:
			return UI_XBOX_YB;
		case 5:
			return UI_XBOX_BB;
		case 6:
			return UI_XBOX_AB;
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetXBoxShiftButtonForPowerID");
const char* exprEntGetXBoxShiftButtonForPowerID(SA_PARAM_OP_VALID Entity *pEntity, int iID)
{
	if(pEntity && pEntity->pChar)
	{
		S32 iSlot = character_PowerIDSlot(pEntity->pChar,(U32)iID);
		if(iSlot >= 3 && iSlot <= 6)
		{
			return UI_XBOX_LT;
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntFindSlotForPower);
S32 exprEntFindSlotForPower(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID Power *pPower)
{
	return (pEntity && pPower) ? character_PowerIDSlot(pEntity->pChar, pPower->uiID) : -1;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowerListDescriptionForPower");
char *exprEntGetPowerListDescriptionForPower(	SA_PARAM_OP_VALID Entity *pEntity,
												SA_PARAM_OP_VALID Power *pPower,
												SA_PARAM_NN_VALID const char *pchMessageKey)
{
	static char *s_pchDescription = NULL;
	estrClear(&s_pchDescription);
	if(pEntity && pEntity->pChar && pPower)
	{
		PowerDef *pDef = GET_REF(pPower->hDef);

		if(pDef)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
			char *pchTemp = NULL;
			power_AutoDesc(entGetPartitionIdx(pEntity),pPower,pEntity->pChar,&pchTemp,NULL,"<br>","<bsp><bsp>","- ",false,0,entGetPowerAutoDescDetail(pEntity,false), pExtract,NULL);
			FormatMessageKey(&s_pchDescription, pchMessageKey, STRFMT_STRUCT("Power", pDef, parse_PowerDef), STRFMT_STRING("PowerAutoDesc",pchTemp), STRFMT_END);
			estrDestroy(&pchTemp);
			return s_pchDescription;
		}
	}
	return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowerListDescription");
char *exprEntGetPowerListDescription(SA_PARAM_OP_VALID Entity *pEntity,
									 int iPowerID,
									 SA_PARAM_NN_VALID const char *pchMessageKey)
{
	if ( pEntity && pEntity->pChar )
	{
		return exprEntGetPowerListDescriptionForPower(pEntity,character_FindPowerByID(pEntity->pChar,iPowerID),pchMessageKey);
	}
	return "";
}

// This intentionally ignores checking the HideInUI flag on powers
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetWarpDestinationList");
void exprEntGetWarpDestinationList(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEnt)
{
	PowerListWarpData*** peaData = ui_GenGetManagedListSafe(pGen, PowerListWarpData);
	int i, j, k, iCount = 0;
	static StashTable s_stMaps = NULL;

	if (s_stMaps)
	{
		stashTableClear(s_stMaps);
	}
	if (pEnt && pEnt->pChar)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		int iPowers = eaSize(&pEnt->pChar->ppPowers);
		for (i = 0; i < iPowers; i++)
		{
			Power* pPower = pEnt->pChar->ppPowers[i];
			PowerDef* pPowerDef = GET_REF(pPower->hDef);
			if (pPowerDef && pPowerDef->bHasWarpAttrib)
			{
				F32 fCooldown;
				PowerListWarpData* pData = NULL;
				PowerListWarpPowerData* pPowerData;
				PowerListWarpItemData* pItemData;
				AttribModDef* pWarpModDef = powerdef_GetWarpAttribMod(pPowerDef, true, NULL);
				
				// todo: if we ever need to handle multiple WarpAttribs here this will need to be updated
				if (pWarpModDef)
				{
					const char* pchMapName = NULL;
					if (pWarpModDef->offAttrib == kAttribType_WarpTo)
					{
						WarpToParams *pParams = (WarpToParams*)pWarpModDef->pParams;
						pchMapName = pParams->cpchMap;
					}
					if (!s_stMaps) {
						s_stMaps = stashTableCreateWithStringKeys(POWERLIST_DEFAULT_ITEMTABLE_SIZE, StashDefault);
					}
					stashFindPointer(s_stMaps, pchMapName, &pData);

					if (!pData)
					{
						ZoneMapInfo* pZoneInfo = worldGetZoneMapByPublicName(pchMapName);
						DisplayMessage* pDisplayMessage = zmapInfoGetDisplayNameMessage(pZoneInfo);
						pData = eaGetStruct(peaData, parse_PowerListWarpData, iCount++);
						pData->pchMapName = pchMapName;
						pData->pchMapDisplayName = TranslateDisplayMessage(*pDisplayMessage);
						pData->fShortestCooldown = -1.0f;
						pData->iNumActivatable = 0;
						pData->iNumPowers = 0;
						stashAddPointer(s_stMaps, pchMapName, pData, false);
						for (j = eaSize(&pData->eaPowerData)-1; j >= 0; j--)
						{
							pPowerData = pData->eaPowerData[j];
							pPowerData->pPower = NULL;
							pPowerData->iChargesLeft = 0;
							pPowerData->iStackCount = 0;
							pPowerData->bCanActivate = false;
							pPowerData->bDirty = false;
							for (k = eaSize(&pPowerData->eaItemData)-1; k >= 0; k--)
							{
								pItemData = pPowerData->eaItemData[k];
								pItemData->pItem = NULL;
								pItemData->bDirty = false;
							}
						}
					}
				}
				else
				{
					continue;
				}
				
				pPowerData = NULL;
				if (pPower->pSourceItem)
				{
					for (j = eaSize(&pData->eaPowerData)-1; j >= 0; j--)
					{
						if (REF_COMPARE_HANDLES(pPower->pSourceItem->hItem, pData->eaPowerData[j]->hItemDef))
						{
							pPowerData = pData->eaPowerData[j];
							break;
						}
					}
				}
				if (!pPowerData)
				{
					pPowerData = StructCreate(parse_PowerListWarpPowerData);
					eaPush(&pData->eaPowerData, pPowerData);
				}
				pPowerData->iChargesLeft += pPowerDef->iCharges - power_GetChargesUsed(pPower);

				fCooldown = character_GetCooldownFromPowerDef(pEnt->pChar, pPowerDef);
				if (EntIsPowerTrayActivatable(pEnt->pChar, pPower, NULL, pExtract) && fCooldown < 0.0001f)
				{
					pPowerData->bCanActivate = true;
					pData->iNumActivatable++;
				}
				pData->iNumPowers++;
				pPowerData->bDirty = true;

				if (pPower->pSourceItem)
				{
					InventorySlot* pSlot;
					pItemData = eaIndexedGetUsingInt(&pPowerData->eaItemData, pPower->pSourceItem->id);

					if (!pItemData)
					{
						pItemData = StructCreate(parse_PowerListWarpItemData);
						pItemData->uItemID = pPower->pSourceItem->id;
						eaIndexedEnable(&pPowerData->eaItemData, parse_PowerListWarpItemData);
						eaPush(&pPowerData->eaItemData, pItemData);
					}
					pItemData->pItem = pPower->pSourceItem;
					if (!pItemData->bDirty)
					{
						pPowerData->pPower = pPower;
						pPowerData->fCooldown = fCooldown;
					}
					pItemData->bDirty = true;

					pSlot = inv_ent_GetSlotPtr(pEnt, pItemData->eLastBagID, pItemData->iLastBagSlot, pExtract);
					if (!pSlot || !pSlot->pItem || pSlot->pItem->id != pPower->pSourceItem->id)
					{
						BagIterator* pIter = inv_trh_FindItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), pPower->pSourceItem->id);
						if (pIter)
						{
							pItemData->eLastBagID = bagiterator_GetCurrentBagID(pIter);
							pItemData->iLastBagSlot = pIter->i_cur;
							pSlot = bagiterator_GetSlot(pIter);
						}
						else
						{
							pSlot = NULL;
						}
						bagiterator_Destroy(pIter);
					}
					if (pSlot && pSlot->pItem)
					{
						pPowerData->iStackCount += pSlot->pItem->count;
						pPowerData->iChargesLeft += (pSlot->pItem->count-1) * pPowerDef->iCharges;
					}
					COPY_HANDLE(pPowerData->hItemDef, pPower->pSourceItem->hItem);
				}
				else
				{
					pPowerData->pPower = pPower;
					pPowerData->fCooldown = fCooldown;
					REMOVE_HANDLE(pPowerData->hItemDef);
				}
			}
		}
	}
	for (i = 0; i < iCount; i++)
	{
		PowerListWarpData* pData = (*peaData)[i];
		for (j = eaSize(&pData->eaPowerData)-1; j >= 0; j--)
		{
			PowerListWarpPowerData* pPowerData = pData->eaPowerData[j];
			if (!pPowerData->bDirty)
			{
				StructDestroy(parse_PowerListWarpPowerData, eaRemove(&pData->eaPowerData, j));
			}
			else
			{
				if (pData->fShortestCooldown < 0 || pPowerData->fCooldown < pData->fShortestCooldown)
				{
					pData->fShortestCooldown = pPowerData->fCooldown;
				}
				for (k = eaSize(&pPowerData->eaItemData)-1; k >= 0; k--)
				{
					PowerListWarpItemData* pItemData = pPowerData->eaItemData[k];
					if (!pItemData->bDirty)
					{
						StructDestroy(parse_PowerListWarpItemData, eaRemove(&pPowerData->eaItemData, k));
					}
				}
			}
		}
	}
	eaSetSizeStruct(peaData, parse_PowerListWarpData, iCount);
	eaQSort(*peaData, SortPowerListWarpData);
	ui_GenSetManagedListSafe(pGen, peaData, PowerListWarpData, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetWarpPowerListForDestination");
void exprGetWarpPowerListForDestination(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID PowerListWarpData* pData)
{
	if (pData)
	{
		ui_GenSetList(pGen, &pData->eaPowerData, parse_PowerListWarpPowerData);
	}
	else
	{
		ui_GenSetList(pGen, NULL, NULL);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("WarpPowerExec");
void exprGetWarpPowerExec(SA_PARAM_OP_VALID PowerListWarpData* pData, S32 iPowerIdx)
{
	if (pData)
	{
		PowerListWarpPowerData* pPowerData = eaGet(&pData->eaPowerData, iPowerIdx);
		if (pPowerData && pPowerData->bCanActivate && pPowerData->pPower)
		{
			entUsePowerID(1, pPowerData->pPower->uiID);
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetWarpPowerCount");
int exprEntGetWarpPowerCount(SA_PARAM_OP_VALID Entity* pEnt)
{
	int i, iCount = 0;
	if (pEnt && pEnt->pChar)
	{
		for (i = eaSize(&pEnt->pChar->ppPowers)-1; i >= 0; i--)
		{
			Power* pPower = pEnt->pChar->ppPowers[i];
			PowerDef* pPowerDef = GET_REF(pPower->hDef);
			if (pPowerDef && pPowerDef->bHasWarpAttrib)
			{
				iCount++;
			}
		}
	}
	return iCount;
}

// Categories is a delimited list of power categories
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPetPowerList");
void exprEntGetPetPowerList(ExprContext* pContext, SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEnt, const char* pchCategories)
{
	PetPowerData*** peaData = ui_GenGetManagedListSafe(pGen, PetPowerData);
	int i, j, iCount = 0;

	if (pEnt && pEnt->pPlayer)
	{
		S32* piCategories = NULL;
		gclGetPowerCategoriesFromString(pContext, pchCategories, &piCategories);

		for (i = 0; i < eaSize(&pEnt->pPlayer->petInfo); i++)
		{
			PlayerPetInfo* pPetInfo = pEnt->pPlayer->petInfo[i];
			Entity* pPetEnt = entFromEntityRefAnyPartition(pPetInfo->iPetRef);
			if (pPetEnt)
			{
				for (j = 0; j < eaSize(&pPetInfo->ppPowerStates); j++)
				{
					PetPowerState* pPowerState = pPetInfo->ppPowerStates[j];
					PowerDef* pPowDef = GET_REF(pPowerState->hdef);

					if (pPowDef && POWERTYPE_ACTIVATABLE(pPowDef->eType) && !pPowDef->bHideInUI)
					{
						int c;
						for (c = eaiSize(&piCategories)-1; c >= 0; c--)
						{
							 if (eaiFind(&pPowDef->piCategories,piCategories[c]) >= 0)
								 break;
						}
						if (c >= 0)
						{
							PetPowerData* pData = eaGetStruct(peaData, parse_PetPowerData, iCount++);
							COPY_HANDLE(pData->hDef, pPowerState->hdef);
							pData->eOwnerType = GLOBALTYPE_ENTITYSAVEDPET;
							pData->uOwnerID = entGetContainerID(pPetEnt);
							pData->pchOwnerName = entGetLocalName(pPetEnt);
							pData->pEnt = pPetEnt;
						}
					}
				}
			}
		}
		eaiDestroy(&piCategories);
	}

	eaSetSizeStruct(peaData, parse_PetPowerData, iCount);
	ui_GenSetManagedListSafe(pGen, peaData, PetPowerData, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPetPowerCount");
int exprEntGetPetPowerCount(ExprContext* pContext, SA_PARAM_OP_VALID Entity* pEnt, const char* pchCategories)
{
	int i, j, iCount = 0;
	if (pEnt && pEnt->pPlayer)
	{
		S32* piCategories = NULL;
		gclGetPowerCategoriesFromString(pContext, pchCategories, &piCategories);

		for (i = eaSize(&pEnt->pPlayer->petInfo)-1; i >= 0; i--)
		{
			PlayerPetInfo* pPetInfo = pEnt->pPlayer->petInfo[i];
			Entity* pPetEnt = entFromEntityRefAnyPartition(pPetInfo->iPetRef);
			if (pPetEnt)
			{
				for (j = 0; j < eaSize(&pPetInfo->ppPowerStates); j++)
				{
					PetPowerState* pPowerState = pPetInfo->ppPowerStates[j];
					PowerDef* pPowDef = GET_REF(pPowerState->hdef);

					if (pPowDef && POWERTYPE_ACTIVATABLE(pPowDef->eType) && !pPowDef->bHideInUI)
					{
						int c;
						for (c = eaiSize(&piCategories)-1; c >= 0; c--)
						{
							 if (eaiFind(&pPowDef->piCategories,piCategories[c]) >= 0)
								 break;
						}
						if (c >= 0)
						{
							iCount++;
						}
					}
				}
			}
		}
		eaiDestroy(&piCategories);
	}
	return iCount;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntPowerNameToID");
S32 exprEntPowerNameToID(SA_PARAM_OP_VALID Entity *pEnt, const char *pchPowerName)
{
	if (pEnt && pEnt->pChar && (pchPowerName = allocFindString(pchPowerName)))
	{
		S32 i;

		for (i = eaSize(&pEnt->pChar->ppPowers) - 1; i >= 0; i--)
		{
			PowerDef *pDef = GET_REF(pEnt->pChar->ppPowers[i]->hDef);
			const char *pchName = pDef ? pDef->pchName : REF_STRING_FROM_HANDLE(pEnt->pChar->ppPowers[i]->hDef);
			if (pchName == pchPowerName)
				return pEnt->pChar->ppPowers[i]->uiID;
		}
	}
	return 0;
}

#include "AutoGen/PowerList_c_ast.c"
