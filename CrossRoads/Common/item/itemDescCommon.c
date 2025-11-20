
#if GAMECLIENT
#include "gclEntity.h"
#endif

#include "stdtypes.h"
#include "Entity.h"
#include "ItemCommon.h"
#include "ItemEnums.h"
#include "GameAccountDataCommon.h"
#include "Character.h"
#include "PowersAutoDesc.h"
#include "CombatConfig.h"
#include "inventoryCommon.h"
#include "GameStringFormat.h"

#include "Autogen/PowersAutoDesc_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

void Item_InnatePowerAutoDesc(Entity *pEnt, Item *pItem, char **ppchDesc, S32 eActiveGemType)
{
	Character *pChar = pEnt ? pEnt->pChar : NULL;
	int iLevel, iPower, iNumPowers = item_GetNumItemPowerDefs(pItem, true);
	int iGemPowers = item_GetNumGemsPowerDefs(pItem);
	PowerDef **ppDefsInnate = NULL;
	F32 *pfScales = NULL;
	S32* eaSlotRequired = NULL;
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

	if (!pItem || !pChar)
		return;

	iLevel = item_GetLevel(pItem) ? item_GetLevel(pItem) : pChar ? pChar->iLevelCombat : 1;

	if(pItemDef->eType == kItemType_Gem)
		iLevel = 1;

	for(iPower=0; iPower<iNumPowers; iPower++)
	{
		PowerDef *pPowerDef = item_GetPowerDef(pItem, iPower);
		ItemPowerDef *pItemPower = item_GetItemPowerDef(pItem, iPower);

		if(pPowerDef && pPowerDef->eType==kPowerType_Innate)
		{
			if(iNumPowers - iGemPowers <= iPower)
			{
				if(pItemPower->pRestriction && pItemPower->pRestriction->eRequiredGemSlotType)
				{
					ItemGemSlotDef *pGemSlotDef = GetGemSlotDefFromPowerIdx(pItem, iPower);

					if(pGemSlotDef && !(pItemPower->pRestriction->eRequiredGemSlotType & pGemSlotDef->eType))
						continue;

					if(item_GetGemPowerLevel(pItem))
						iLevel = item_GetGemPowerLevel(pItem);
				}
			}
			eaPush(&ppDefsInnate,pPowerDef);
			eafPush(&pfScales, item_GetItemPowerScale(pItem, iPower));
			ea32Push(&eaSlotRequired, pItemPower->pRestriction ? pItemPower->pRestriction->eRequiredGemSlotType : 0);
		}
	}

	if(ppDefsInnate)
	{
		if(RefSystem_ReferentFromString(gMessageDict,"Item.InnateMod"))
			powerdefs_AutoDescInnateMods(entGetPartitionIdx(pEnt), pItem, ppDefsInnate, pfScales, ppchDesc,"Item.InnateMod",pChar, eaSlotRequired,iLevel,true, eActiveGemType,entGetPowerAutoDescDetail(pEnt,false));
		else
			powerdefs_AutoDescInnateMods(entGetPartitionIdx(pEnt), pItem, ppDefsInnate, pfScales, ppchDesc,NULL,pChar, eaSlotRequired, iLevel,true, eActiveGemType, entGetPowerAutoDescDetail(pEnt,false));

		eaDestroy(&ppDefsInnate);
		eafDestroy(&pfScales);
		ea32Destroy(&eaSlotRequired);
	}
}


void Item_GetInnatePowerDifferencesAutoDesc(Entity *pEnt, Item *pItem, Item *pOtherItem, char **ppchDesc)
{
	Character *pChar = pEnt ? pEnt->pChar : NULL;
	const char *pchMsgKey;
	if (!pItem || !pChar || !pOtherItem)
		return;

	// get the message key used for this message
	pchMsgKey = "Item.InnateModDiff";
	if(!RefSystem_ReferentFromString(gMessageDict,pchMsgKey))
		pchMsgKey = NULL;

	powerdefs_GetAutoDescInnateModsDiff(entGetPartitionIdx(pEnt), 
		pItem, 
		pOtherItem, 
		ppchDesc, 
		pchMsgKey, 
		pChar, 
		entGetPowerAutoDescDetail(pEnt,false));
}

void Item_PowerAutoDescCustom(Entity *pEnt, Item *pItem, char **ppchDesc, const char *pchPowerMessageKey, const char *pchAttribMessageKey, S32 eActiveGemSlotType)
{
	Character *pChar = pEnt ? pEnt->pChar : NULL;
	int iLevel, iPower, iNumPowers = item_GetNumItemPowerDefs(pItem, true);
	F32 *pfScales = NULL;
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

	if (!pItem || !pChar || !pItemDef)
		return;

	iLevel = item_GetLevel(pItem) ? item_GetLevel(pItem) : pChar ? pChar->iLevelCombat : 1;

	if(pItemDef->bAutoDescDisabled)
	{
		estrCopy2(ppchDesc, TranslateDisplayMessage(pItemDef->msgAutoDesc));
	}
	else
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		for(iPower=0; iPower< iNumPowers; iPower++)
		{
			Power *pPower = item_GetPower(pItem,iPower);
			PowerDef *pPowerDef = item_GetPowerDef(pItem, iPower);
			ItemPowerDef* pItemPowerDef = item_GetItemPowerDef(pItem, iPower);
			AutoDescPower *pAutoDesc = StructCreate(parse_AutoDescPower);

			if (item_ItemPowerActive(pEnt, NULL, pItem, iPower))
			{
				if(pPower)
				{
					// it would be nice if this pointer were already set.  Since this code will be run on the client, and the item is
					// being delivered by the server, I can't figure out where the right place to set this up is.  [RMARR - 9/27/10]
					pPower->eSource = kPowerSource_Item;
					pPower->pSourceItem = pItem;
					power_AutoDesc(iPartitionIdx,pPower,pChar,NULL,pAutoDesc,NULL,NULL,NULL,true,0,entGetPowerAutoDescDetail(pEnt,false),pExtract,NULL);
					if (pItemPowerDef && pItemPowerDef->pRestriction)
					{
						pAutoDesc->eRequiredGemType = pItemPowerDef->pRestriction->eRequiredGemSlotType;
						pAutoDesc->bActive = eActiveGemSlotType == 0 || (pItemPowerDef->pRestriction->eRequiredGemSlotType == eActiveGemSlotType);
					}
					else
						pAutoDesc->bActive = true;

					if (pPowerDef)
					{
						powerdef_AutoDescCustom(pEnt, ppchDesc, pPowerDef, pAutoDesc, pchPowerMessageKey, pchAttribMessageKey);
					}
				}
				else
				{
					if(pPowerDef && pPowerDef->eType!=kPowerType_Innate)
					{
						powerdef_AutoDescCustom(pEnt, ppchDesc, pPowerDef, pAutoDesc, pchPowerMessageKey, pchAttribMessageKey);
					}
				}
			}
			StructDestroy(parse_AutoDescPower, pAutoDesc);
		}
	}
}

// Returns the item categories based on NNO rules
static void Item_GetItemCategoriesStringNNO(SA_PARAM_NN_VALID ItemDef * pItemDef, SA_PARAM_NN_VALID char ** estrItemCategories)
{
	devassert(pItemDef);
	devassert(estrItemCategories);

	if (pItemDef && estrItemCategories)
	{
		static char *estrMessageKey = NULL;
		S32 i, iAddedCategoryCount = 0;
		const char *pchCategoryName;
		const char *pchTranslatedName;
		bool bSkipSecondPass = false;

		// First pass for Light, Medium, Heavy and Ranged
		for (i = 0; i < eaiSize(&pItemDef->peCategories); i++)
		{
			// Get the category name
			pchCategoryName = StaticDefineIntRevLookup(ItemCategoryEnum, pItemDef->peCategories[i]);

			if (pchCategoryName && *pchCategoryName &&
				(stricmp(pchCategoryName, "Light") == 0 || stricmp(pchCategoryName, "Medium") == 0 || stricmp(pchCategoryName, "Heavy") == 0 || stricmp(pchCategoryName, "Ranged") == 0))
			{
				// See if there is a message for this category
				estrPrintf(&estrMessageKey, "StaticDefine_ItemCategory_%s", pchCategoryName);
				pchTranslatedName = TranslateMessageKey(estrMessageKey);
				if (pchTranslatedName)
				{
					if (iAddedCategoryCount > 0)
						estrAppend2(estrItemCategories, " ");
					estrAppend2(estrItemCategories, pchTranslatedName);

					iAddedCategoryCount++;

					if (stricmp(pchCategoryName, "Ranged") == 0)
						bSkipSecondPass = true;
				}
			}
		}

		if (bSkipSecondPass)
			return;

		// Second pass for the rest
		for (i = 0; i < eaiSize(&pItemDef->peCategories); i++)
		{
			// Get the category name
			pchCategoryName = StaticDefineIntRevLookup(ItemCategoryEnum, pItemDef->peCategories[i]);

			if (pchCategoryName && *pchCategoryName &&
				(stricmp(pchCategoryName, "Blade") == 0 || stricmp(pchCategoryName, "Blunt") == 0 || stricmp(pchCategoryName, "Pierce") == 0))
			{
				// See if there is a message for this category
				estrPrintf(&estrMessageKey, "StaticDefine_ItemCategory_%s", pchCategoryName);
				pchTranslatedName = TranslateMessageKey(estrMessageKey);
				if (pchTranslatedName)
				{
					if (iAddedCategoryCount > 0)
						estrAppend2(estrItemCategories, " ");
					estrAppend2(estrItemCategories, pchTranslatedName);

					iAddedCategoryCount++;
				}
			}
		}
	}
}

// Returns the list of item categories for the item
void Item_GetItemCategoriesString(SA_PARAM_OP_VALID ItemDef * pItemDef, 
	SA_PARAM_OP_VALID const char* pchFilterPrefix,
	SA_PARAM_OP_VALID char ** estrItemCategories)
{
	if (pItemDef && estrItemCategories)
	{
		if (gConf.bUseNNOItemCategoryNamingRules)
		{
			Item_GetItemCategoriesStringNNO(pItemDef, estrItemCategories);
		}
		else
		{
			// Default behavior is to build a comma separated list of item categories
			S32 i, iTranslatedCategoryCount = 0;
			const char *pchCategoryName;
			const char *pchTranslatedName;
			static char *estrMessageKey = NULL;

			// Iterate thru all categories
			for (i = 0; i < eaiSize(&pItemDef->peCategories); i++)
			{
				// Get the category name
				pchCategoryName = StaticDefineIntRevLookup(ItemCategoryEnum, pItemDef->peCategories[i]);

				if (pchCategoryName && *pchCategoryName && 
					(!pchFilterPrefix || !pchFilterPrefix[0] || strStartsWith(pchCategoryName, pchFilterPrefix)))
				{
					// See if there is a message for this category
					estrPrintf(&estrMessageKey, "StaticDefine_ItemCategory_%s", pchCategoryName);
					pchTranslatedName = TranslateMessageKey(estrMessageKey);
					if (pchTranslatedName)
					{
						if (iTranslatedCategoryCount > 0)
							estrAppend2(estrItemCategories, ", ");
						estrAppend2(estrItemCategories, pchTranslatedName);

						iTranslatedCategoryCount++;
					}
				}
			}
		}
	}
}

const char* item_GetItemPowerUsagePrompt(Language lang, Item* pItem)
{
	int NumPowers = item_GetNumItemPowerDefs(pItem, true);
	int iPower;
	if(NumPowers >0)
	{
		for(iPower=NumPowers-1; iPower>=0; iPower--)
		{
			ItemPowerDef* pPowerDef = item_GetItemPowerDef(pItem, iPower);
			if (pPowerDef)
				return langTranslateDisplayMessage(lang, pPowerDef->descriptionMsg);
		}
	}
	return NULL;
}

static Item* GetItemSetItem(SA_PARAM_OP_VALID Entity *pEntity, ItemDef *pItemDefSet, GameAccountDataExtract *pExtract)
{
	InventoryBag *pBag; 
	if (pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEntity), InvBagIDs_ItemSet,pExtract))
	{
		int i;
		for(i=eaSize(&pBag->ppIndexedInventorySlots)-1; i>=0; i--)
		{
			InventorySlot *pSlot = pBag->ppIndexedInventorySlots[i];
			if(pSlot && pSlot->pItem && GET_REF(pSlot->pItem->hItem)==pItemDefSet)
			{
				return pSlot->pItem;
			}
		}
	}
	return NULL;
}

// This describes any ItemSet-based ItemPowers on the Item.  It's split out from the normaly ItemSet
//  description so that when someone makes an Item with an additional unique bonus based on the ItemSet
//  it belongs to (rather than just the normal ItemSet bonuses), support for describing it can be added fairly easily.
static void GetItemDescriptionItemSetBonus(char **pestrItemSetBonusDesc, SA_PARAM_OP_VALID Item *pItem, SA_PARAM_NN_VALID ItemDef *pItemDef, SA_PARAM_OP_VALID Entity *pEntity, U8 uiCount, const char* pchPowerMessageKey, const char* pchAttribModsMessageKey, GameAccountDataExtract *pExtract)
{
	int i,j,s;
	Character *pChar = pEntity ? pEntity->pChar : NULL;
	int iLevel = 1;
	PowerDef **ppDefsInnate = NULL;
	F32 *pfScales = NULL;
	PowerDef **ppDefsPowers = NULL;
	S32 *piIndex = NULL;
	AutoDescDetail eDetail = entGetPowerAutoDescDetail(pEntity,false);

	if(pItem && item_GetLevel(pItem))
		iLevel = item_GetLevel(pItem);
	else if(pItemDef->iLevel)
		iLevel = pItemDef->iLevel;
	else if(pChar)
		iLevel = pChar->iLevelCombat;

	if(iLevel <= 0)
	{
		ErrorDetailsf("ItemDef %s, Level %d, !!Item %d, !!Character %d",pItemDef->pchName,iLevel,!!pItem,!!pChar);
		devassertmsg(0,"Level for ItemSet description <= 0");
		iLevel = 1;
	}

	s = eaSize(&pItemDef->ppItemPowerDefRefs);
	for(i=0; i<s; i++)
	{
		char *estrCount = NULL;
		char *estrInnates = NULL;
		char *estrPowers = NULL;
		ItemPowerDefRef *pItemPowerDefRef = pItemDef->ppItemPowerDefRefs[i];
		ItemPowerDef *pItemPowerDef = GET_REF(pItemDef->ppItemPowerDefRefs[i]->hItemPowerDef);
		PowerDef *pPowerDef = pItemPowerDef ? GET_REF(pItemPowerDef->hPower) : NULL;
		S32 bActive;

		if(!pItemPowerDefRef->uiSetMin || !pPowerDef)
			continue;

		if(pPowerDef->eType==kPowerType_Innate)
		{
			eaPush(&ppDefsInnate,pPowerDef);
			eafPush(&pfScales, pItemPowerDefRef->ScaleFactor);
		}
		else
		{
			eaPush(&ppDefsPowers,pPowerDef);
			eaiPush(&piIndex,i);
		}

		// Keep looping to include any that match this min (assumes they're sorted to make this work)
		if(i+1<s && pItemDef->ppItemPowerDefRefs[i+1]->uiSetMin==pItemPowerDefRef->uiSetMin)
			continue;

		bActive = (uiCount >= pItemPowerDefRef->uiSetMin);

		// Generate the string representing how many of the set is required for this bonus
		estrStackCreate(&estrCount);
		FormatGameMessageKey(&estrCount, "Inventory_ItemInfo_ItemSet_Count",STRFMT_INT("Count",pItemPowerDefRef->uiSetMin),STRFMT_END);

		// Describe the innates
		if(ppDefsInnate)
		{
			estrStackCreate(&estrInnates);
			if(RefSystem_ReferentFromString(gMessageDict,"Item.InnateMod"))
				powerdefs_AutoDescInnateMods(PARTITION_CLIENT, pItem, ppDefsInnate, pfScales, &estrInnates,"Item.InnateMod",pChar,NULL, iLevel,0, true,eDetail);
			else
				powerdefs_AutoDescInnateMods(PARTITION_CLIENT, pItem, ppDefsInnate, pfScales, &estrInnates,NULL,pChar,NULL, iLevel,true,0,eDetail);

			eaDestroy(&ppDefsInnate);
			eafDestroy(&pfScales);
		}

		// Describe the non-innates
		// TODO: This probably needs different messages, or no header, or something, it doesn't look real good right now
		if(ppDefsPowers)
		{
			estrStackCreate(&estrPowers);

			//loop for all powers on the item
			for(j=0; j<eaSize(&ppDefsPowers); j++)
			{
				AutoDescPower *pAutoDescPower = StructCreate(parse_AutoDescPower);
				Power *pPower = item_GetPower(pItem,piIndex[j]);
				pPowerDef = ppDefsPowers[j];

				if (j > 0)
					estrAppend2(&estrPowers, "<br>");

				if(pPower)
				{
					power_AutoDesc(PARTITION_CLIENT,pPower,pChar,NULL,pAutoDescPower,NULL,NULL,NULL,true,0,eDetail,pExtract,NULL);
				}
				else
				{
					powerdef_AutoDesc(PARTITION_CLIENT,pPowerDef,NULL,pAutoDescPower,NULL,NULL,NULL,pChar,NULL,NULL,iLevel,true,eDetail,pExtract, NULL);
				}
				powerdef_AutoDescCustom(pEntity, &estrPowers, pPowerDef, pAutoDescPower, pchPowerMessageKey, pchAttribModsMessageKey);
				StructDestroy(parse_AutoDescPower,pAutoDescPower);
			}

			eaDestroy(&ppDefsPowers);
			eaiClearFast(&piIndex);
		}

		FormatGameMessageKey(pestrItemSetBonusDesc, bActive ? "Inventory_ItemInfo_ItemSet_BonusActive" : "Inventory_ItemInfo_ItemSet_BonusInactive",
			STRFMT_STRING("Count",estrCount),
			STRFMT_STRING("Innates",estrInnates),
			STRFMT_STRING("Powers",estrPowers),
			STRFMT_END);

		estrDestroy(&estrCount);
		estrDestroy(&estrInnates);
		estrDestroy(&estrPowers);
	}
	eaiDestroy(&piIndex);
}

void GetItemDescriptionFromItemSetDef(char **pestrItemSetDesc, 
	SA_PARAM_OP_VALID ItemDef *pItemDefSet, 
	SA_PARAM_OP_VALID Entity *pEntity, 
	const char* pchPowerMessageKey, 
	const char* pchAttribModsMessageKey,
	const char* pchItemSetFormatKey,
	GameAccountDataExtract *pExtract)
{
	if(pItemDefSet)
	{
		int i,j,k,s;
		char *estrTemp = NULL;
		char *estrItems = NULL;
		char *estrBonuses = NULL;
		Item *pItemSet = NULL;
		int iCount = 0;

		// If this is a pet, request an offline copy of the entity
		if (pEntity && entGetType(pEntity) == GLOBALTYPE_ENTITYSAVEDPET)
		{
#if GAMECLIENT
			Entity* pEntCopy = savedpet_GetOfflineOrNotCopy(entGetContainerID(pEntity));
			if (pEntCopy)
			{
				pEntity = pEntCopy;
			}
			else
#endif
			{
				// If the copy entity hasn't been received yet, then just return
				return;
			}
		}

		if (pItemDefSet->bItemSetMembersUnique)
		{
			estrStackCreate(&estrTemp);
			estrStackCreate(&estrItems);
			s = eaSize(&pItemDefSet->ppItemSetMembers);
			for(i=0; i<s; i++)
			{
				ItemDef *pItemDefMember = GET_REF(pItemDefSet->ppItemSetMembers[i]->hDef);
				if(pItemDefMember)
				{
					S32 bActive = false;
					if(pEntity && pEntity->pInventoryV2)
					{
						for(j=eaSize(&pEntity->pInventoryV2->ppInventoryBags)-1; j>=0; j--)
						{
							InventoryBag *pBag = pEntity->pInventoryV2->ppInventoryBags[j];
							if(invbag_flags(pBag) & (InvBagFlag_EquipBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag|InvBagFlag_DeviceBag))
							{
								for(k=eaSize(&pBag->ppIndexedInventorySlots)-1; k>=0; k--)
								{
									InventorySlot *pSlot = pBag->ppIndexedInventorySlots[k];
									if(pSlot && pSlot->pItem && GET_REF(pSlot->pItem->hItem)==pItemDefMember)
										break;
								}
								if(k>=0)
								{
									bActive = true;
									break;
								}
							}
						}
					}

					FormatGameMessageKey(&estrItems,bActive ? "Inventory_ItemInfo_ItemSet_ItemActive" : "Inventory_ItemInfo_ItemSet_ItemInactive",
						STRFMT_STRING("Item",NULL_TO_EMPTY(TranslateDisplayMessage(pItemDefMember->displayNameMsg))),
						STRFMT_END);
				}
			}
		}

		// Find the actual ItemSet Item so we know how many this Entity actually has
		if (pItemSet = GetItemSetItem(pEntity, pItemDefSet, pExtract))
		{
			iCount = pItemSet->uSetCount;
		}
		GetItemDescriptionItemSetBonus(&estrBonuses,pItemSet,pItemDefSet,pEntity,iCount,pchPowerMessageKey,pchAttribModsMessageKey,pExtract);

		FormatGameMessageKey(pestrItemSetDesc, pchItemSetFormatKey,
			STRFMT_STRING("Name",NULL_TO_EMPTY(TranslateDisplayMessage(pItemDefSet->displayNameMsg))),
			STRFMT_STRING("Description",NULL_TO_EMPTY(TranslateDisplayMessage(pItemDefSet->descriptionMsg))),
			STRFMT_STRING("Description",NULL_TO_EMPTY(TranslateDisplayMessage(pItemDefSet->descShortMsg))),
			STRFMT_INT("Current",iCount),
			STRFMT_INT("Max",eaSize(&pItemDefSet->ppItemSetMembers)),
			STRFMT_STRING("Items",NULL_TO_EMPTY(estrItems)),
			STRFMT_STRING("Bonuses",NULL_TO_EMPTY(estrBonuses)),
			STRFMT_END);

		estrDestroy(&estrBonuses);
		estrDestroy(&estrItems);
		estrDestroy(&estrTemp);
	}
	else
	{
		// Display something for currently unknown ItemSets?
	}
}

// Describes the ItemSet data for an Item
void GetItemDescriptionItemSet(char **pestrItemSetDesc, 
	SA_PARAM_NN_VALID ItemDef *pItemDef, 
	SA_PARAM_OP_VALID Entity *pEntity, 
	const char* pchPowerKey, 
	const char* pchModKey,
	const char* pchItemSetFormatKey,
	GameAccountDataExtract *pExtract)
{
	S32 i, iItemSetCount = eaSize(&pItemDef->ppItemSets);
	S32 iCount = 0;

	for (i = 0; i < iItemSetCount; i++)
	{
		ItemDef* pItemSetDef = GET_REF(pItemDef->ppItemSets[i]->hDef);
		if (pItemSetDef && eaSize(&pItemSetDef->ppItemSetMembers) > 0)
		{
			if (++iCount > 1)
				estrAppend2(pestrItemSetDesc,"\n");
			GetItemDescriptionFromItemSetDef(pestrItemSetDesc,pItemSetDef,pEntity,pchPowerKey,pchModKey,pchItemSetFormatKey,pExtract);
		}
	}
}
