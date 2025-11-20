/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "itemGenCommon.h"
#include "itemCommon.h"
#include "entCritter.h"
#include "EString.h"
#include "Expression.h"
#include "Error.h"
#include "file.h"
#include "fileutil.h"
#include "gimmeDLLWrapper.h"
#include "HashFunctions.h"
#include "qsortG.h"
#include "Message.h"
#include "NameGen.h"
#include "PowerTree.h"
#include "rand.h"
#include "ResourceManager.h"
#include "SharedMemory.h"
#include "species_common.h"
#include "StringCache.h"
#include "structDefines.h"
#include "textparser.h"
#include "characterclass.h"

//AutoGen files
#include "AutoGen/itemGenCommon_h_ast.h"
#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/Expression_h_ast.h"
#include "AutoGen/Message_h_ast.h"
#include "AutoGen/rewardCommon_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/textparser_h_ast.h"

#include "gslMessageFixup.h"
#include "gslMessageFixup_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct ItemGenPowerCategory
{
	int iCategory;
	ItemGenPowerData **ppPowerData;
}ItemGenPowerCategory;

static RewardTable* ItemGen_GetRewardTableByCategoryAndRarity(ItemGenRewardCategory eCategory, ItemGenRarity eRarity)
{
	RewardTable* pTable = NULL;
	const char* pchCategoryName = StaticDefineIntRevLookup(ItemGenRewardCategoryEnum, eCategory);
	const char* pchRarityName = StaticDefineIntRevLookup(ItemGenRarityEnum, eRarity);

	if (pchRarityName)
	{
		char* estrTable = NULL;
		estrStackCreate(&estrTable);
		estrPrintf(&estrTable, "%s_%s", pchCategoryName, pchRarityName);
		
		pTable = RefSystem_ReferentFromString(g_hRewardTableDict, estrTable);
		estrDestroy(&estrTable);
	}
	return pTable;
}

static void itemGen_CopyTrainableNodes(ItemDef* pNewDef, PTNodeDefRef** ppTrainableNodes, S32 iRank)
{
	S32 i;
	for (i = 0; i < eaSize(&ppTrainableNodes); i++)
	{
		ItemTrainablePowerNode* pNode = StructCreate(parse_ItemTrainablePowerNode);
		COPY_HANDLE(pNode->hNodeDef, ppTrainableNodes[i]->hNodeDef);
		pNode->iNodeRank = iRank;
		eaPush(&pNewDef->ppTrainableNodes, pNode);
	}
}

static void itemGen_AddItemCategories(ItemDef* pNewDef, ItemCategory* peCategories)
{
	int i;
	for (i = 0; i < eaiSize(&peCategories); i++)
	{
		eaiPushUnique(&pNewDef->peCategories, peCategories[i]);
	}
}

static void ItemGen_GenerateIcon(ItemDef* pNewDef, ItemGenData* pGenData)
{
	if (pGenData->bGenerateSpeciesIcons && pGenData->iIconCount > 0)
	{
		ItemDef* pOldDef = RefSystem_ReferentFromString(g_hItemDict, pNewDef->pchName);

		if (pOldDef && pOldDef->pchIconName)
		{
			pNewDef->pchIconName = allocAddString(pOldDef->pchIconName);
		}
		else
		{
			char pchIconName[MAX_PATH];
			U32 uSeed = hashString(pNewDef->pchName, false);
			
			S32 iIconIndex = randomIntRangeSeeded(&uSeed, RandType_LCG, 1, pGenData->iIconCount);
			sprintf(pchIconName, "%s_%s_%02d", pGenData->pchIconPrefix, REF_STRING_FROM_HANDLE(pGenData->hSpecies), iIconIndex);

			pNewDef->pchIconName = allocAddString(pchIconName);
		}
	}
	else if (pGenData->bGenerateSpeciesIcons)
	{
		Errorf("Couldn't generate a species icon for Item '%s', ItemGen '%s', no icons were found",
			pNewDef->pchName, pGenData->pchDataName);
	}
}

static ItemDef *itemGen_MakeNewItemDef(ItemGenData *pGenData, ItemGenPowerData **ppPowers, int iDisplayTier, ItemGenRarityDef *pRarity, int iGemSet, int iCostume, U32 *pSeed)
{
	ItemDef *pNewDef = StructCreate(parse_ItemDef);
	ItemGenTier *pTier = NULL;
	char schNewName[255];
	char schPrefix[255] = {0};
	char schSuffix[255] = {0};
	char schBuffer[255];

	char *eStringName;

	const char *pchRarityName = StaticDefineIntRevLookup(ItemGenRarityEnum,pRarity->eRarityType);
	int i;
	int iTier;

	for(iTier=0;iTier<eaSize(&pGenData->ppItemTiers);iTier++)
	{
		if(pGenData->ppItemTiers[iTier]->iTier == iDisplayTier)
		{
			pTier = pGenData->ppItemTiers[iTier];
			break;
		}
	}

	devassertmsg(pTier,"Tier not found, this is a real problem, and you should get Michael McCarry right away!");

	estrCreate(&eStringName);

	estrCopy2(&eStringName,pGenData->pchDataName);
	estrAppend2(&eStringName,"_");
	estrAppend2(&eStringName,pchRarityName);
	estrAppend2(&eStringName,"_");

	if (iCostume < eaSize(&pRarity->eaGenerationLevels))
	{
		sprintf(schNewName,"L%d",pRarity->eaGenerationLevels[iCostume]->iLevel);
		estrAppend2(&eStringName,schNewName);
		estrAppend2(&eStringName,"_");
	}

	for(i=0;i<eaSize(&ppPowers);i++)
	{
		if(ppPowers[i]->pchInternalName)
		{
			estrAppend2(&eStringName,ppPowers[i]->pchInternalName);
			estrAppend2(&eStringName,"_");
		}
	}

	if (iGemSet < eaSize(&pRarity->eaGemSlotData))
	{
		sprintf(schNewName,"Gems%d",iGemSet);
		estrAppend2(&eStringName,schNewName);
		estrAppend2(&eStringName,"_");
	}

	sprintf(schNewName,"T%d",iDisplayTier);

	estrAppend2(&eStringName,schNewName);

	pNewDef->pchName = StructAllocString(eStringName);

	estrDestroy(&eStringName);

	sprintf(schNewName,"ItemGen/%s",pGenData->pchScope);
	pNewDef->pchScope = StructAllocString(schNewName);

	sprintf(schNewName,"Defs/Items/ItemGen/%s/%s.item",pGenData->pchScope,pGenData->pchDataName);
	pNewDef->pchFileName = allocAddFilename(schNewName);

	pNewDef->flags = pGenData->flags;

	pNewDef->flags |= pRarity->eFlagsToAdd;
	pNewDef->flags &= ~pRarity->eFlagsToRemove;

	pNewDef->eType = pGenData->eItemType;
	COPY_HANDLE(pNewDef->hSpecies, pGenData->hSpecies);

	// Copy Restrict Bags
	if (eaSize(&ppPowers) && eaiSize(&ppPowers[0]->peRestrictBags))
		eaiCopy(&pNewDef->peRestrictBagIDs, &ppPowers[0]->peRestrictBags);
	else
		eaiCopy(&pNewDef->peRestrictBagIDs, &pGenData->peRestrictBags);

	// Copy equip limit
	if (eaSize(&ppPowers) && ppPowers[0]->pEquipLimit && (ppPowers[0]->pEquipLimit->eCategory || ppPowers[0]->pEquipLimit->iMaxEquipCount))
		pNewDef->pEquipLimit = StructClone(parse_ItemEquipLimit, ppPowers[0]->pEquipLimit);
	else if (pGenData->pEquipLimit && (pGenData->pEquipLimit->eCategory || pGenData->pEquipLimit->iMaxEquipCount))
		pNewDef->pEquipLimit =  StructClone(parse_ItemEquipLimit, pGenData->pEquipLimit);

	if(IS_HANDLE_ACTIVE(pGenData->hSlotID))
	{
		COPY_HANDLE(pNewDef->hSlotID,pGenData->hSlotID);
	}
	
	//Pull Tier information
	pNewDef->iLevel = pTier->iTrueLevel;

	if(pTier->pRequires)
		pNewDef->pRestriction = StructClone(parse_UsageRestriction,pTier->pRequires);

	if(pRarity->pchIconName)
		pNewDef->pchIconName = allocAddString(pRarity->pchIconName);
	else if(pTier->pchIconName)
		pNewDef->pchIconName = allocAddString(pTier->pchIconName);
	else if(pGenData->pchIconName)
		pNewDef->pchIconName = allocAddString(pGenData->pchIconName);
	else if (pGenData->bGenerateSpeciesIcons)
		ItemGen_GenerateIcon(pNewDef, pGenData);

	if(pRarity->eItemTag)
	{
		pNewDef->eTag = pRarity->eItemTag;
	}
	else if(pTier->eItemTag)
	{
		pNewDef->eTag = pTier->eItemTag;
	}
	else if(pGenData->eItemTag)
	{
		pNewDef->eTag = pGenData->eItemTag;
	}

	// Item category arrays are additive
	for (i = 0; i < eaSize(&ppPowers); i++)
	{
		itemGen_AddItemCategories(pNewDef, ppPowers[i]->peCategories);
	}
	itemGen_AddItemCategories(pNewDef, pGenData->peCategories);
	itemGen_AddItemCategories(pNewDef, pTier->peCategories);
	itemGen_AddItemCategories(pNewDef, pRarity->peCategories);

	if(eaSize(&pRarity->ppItemSets))
	{
		eaCopyStructs(&pRarity->ppItemSets, &pNewDef->ppItemSets, parse_ItemDefRef);
	}
	else if(eaSize(&pTier->ppItemSets))
	{
		eaCopyStructs(&pTier->ppItemSets, &pNewDef->ppItemSets, parse_ItemDefRef);
	}
	else if(eaSize(&pGenData->ppItemSets))
	{
		eaCopyStructs(&pGenData->ppItemSets, &pNewDef->ppItemSets, parse_ItemDefRef);
	}

	if (eaSize(&pRarity->ppTrainableNodes))
	{
		itemGen_CopyTrainableNodes(pNewDef, pRarity->ppTrainableNodes, pRarity->iTrainableNodeRank);
	}
	else if (eaSize(&pTier->ppTrainableNodes))
	{
		itemGen_CopyTrainableNodes(pNewDef, pTier->ppTrainableNodes, pTier->iTrainableNodeRank);
	}
	else if (eaSize(&pGenData->ppTrainableNodes))
	{
		itemGen_CopyTrainableNodes(pNewDef, pGenData->ppTrainableNodes, pGenData->iTrainableNodeRank);
	}
	
	if(IS_HANDLE_ACTIVE(pRarity->hArt))
	{
		COPY_HANDLE(pNewDef->hArt,pRarity->hArt);
	}
	else if(IS_HANDLE_ACTIVE(pTier->hArt))
	{
		COPY_HANDLE(pNewDef->hArt,pTier->hArt);
	}
	else if(IS_HANDLE_ACTIVE(pGenData->hArt))
	{
		COPY_HANDLE(pNewDef->hArt,pGenData->hArt);
	}
	
	if (eaSize(&pRarity->eaGenerationLevels) && iCostume >= 0)
	{
		eaCopyStructs(&pRarity->eaGenerationLevels[iCostume]->ppCostumes,&pNewDef->ppCostumes,parse_ItemCostume);
		pNewDef->iLevel = pRarity->eaGenerationLevels[iCostume]->iLevel;
		if (pNewDef->pRestriction)
			pNewDef->pRestriction->iMinLevel = pRarity->eaGenerationLevels[iCostume]->iLevel;
		
		if (pRarity->eaGenerationLevels[iCostume]->pchIconName)
			pNewDef->pchIconName = allocAddString(pRarity->eaGenerationLevels[iCostume]->pchIconName);
	}
	else if(eaSize(&pRarity->ppCostumes))
		eaCopyStructs(&pRarity->ppCostumes,&pNewDef->ppCostumes,parse_ItemCostume);
	else if(eaSize(&pGenData->ppItemTiers[iTier]->ppCostumes))
		eaCopyStructs(&pGenData->ppItemTiers[iTier]->ppCostumes,&pNewDef->ppCostumes,parse_ItemCostume);
	else if(eaSize(&pGenData->ppCostumes))
		eaCopyStructs(&pGenData->ppCostumes,&pNewDef->ppCostumes,parse_ItemCostume);

	for(i=0;i<eaSize(&ppPowers);i++)
	{
		if(ppPowers[i]->ppCostumes)
		{
			eaCopyStructs(&ppPowers[i]->ppCostumes,&pNewDef->ppCostumes,parse_ItemCostume);
		}
	}

	pNewDef->eCostumeMode = pGenData->eCostumeMode;

	pNewDef->Quality = pRarity->eQuality; 
	pNewDef->iStackLimit = pGenData->iStackLimit;
	if(pGenData->pExprEconomyPoints)
		pNewDef->pExprEconomyPoints = StructClone(parse_Expression,pGenData->pExprEconomyPoints);
	if (pGenData->pchDisplayDesc && pGenData->pchDisplayDesc[0])
	{
		Message *pNewMessage = StructCreate(parse_Message);

		pNewMessage->pcDefaultString = StructAllocString(pGenData->pchDisplayDesc);
		pNewDef->descriptionMsg.pEditorCopy = pNewMessage;
	}
	if (pGenData->pchDisplayDescShort && pGenData->pchDisplayDescShort[0])
	{
		Message *pNewMessage = StructCreate(parse_Message);

		pNewMessage->pcDefaultString = StructAllocString(pGenData->pchDisplayDescShort);
		pNewDef->descShortMsg.pEditorCopy = pNewMessage;
	}
	if(IS_HANDLE_ACTIVE(pRarity->hSubtarget))
	{
		COPY_HANDLE(pNewDef->hSubtarget,pGenData->hSubtarget);
	}
	else if(IS_HANDLE_ACTIVE(pTier->hSubtarget))
	{
		COPY_HANDLE(pNewDef->hSubtarget,pGenData->hSubtarget);
	}
	else if(IS_HANDLE_ACTIVE(pGenData->hSubtarget))
	{
		COPY_HANDLE(pNewDef->hSubtarget,pGenData->hSubtarget);
	}
	
	for(i=0;i<eaSize(&ppPowers);i++)
	{
		ItemGenPowerData* pPowerData = ppPowers[i];
		const char *pchHandle;
		ItemPowerDefRef *pNewRef;
		NOCONST(ItemPowerDefRef)* pNewRefNoConst;

		// Create a new power only if there is a valid ItemPowerDef
		if (!IS_HANDLE_ACTIVE(pPowerData->hItemPowerDef))
		{
			continue;
		}
		pNewRef = StructCreate(parse_ItemPowerDefRef);
		pNewRefNoConst = CONTAINER_NOCONST(ItemPowerDefRef, pNewRef);

		pchHandle = REF_STRING_FROM_HANDLE(pPowerData->hItemPowerDef);

		SET_HANDLE_FROM_STRING("ItemPowerDef",pchHandle,pNewRefNoConst->hItemPowerDef);
		eaPush(&pNewDef->ppItemPowerDefRefs,pNewRef);

		if (pPowerData->pchDisplayPrefix && pPowerData->pchDisplayPrefix[0])
		{
			sprintf(schBuffer,"%s %s", pPowerData->pchDisplayPrefix, schPrefix);
			removeLeadingAndFollowingSpaces(schBuffer);
			strcpy(schPrefix,schBuffer);
		}

		if (pPowerData->pchDisplaySuffix && pPowerData->pchDisplaySuffix[0])
		{
			sprintf(schBuffer,"%s %s", pPowerData->pchDisplaySuffix, schSuffix);
			removeLeadingAndFollowingSpaces(schBuffer);
			strcpy(schSuffix,schBuffer);
		}

		pNewRefNoConst->ScaleFactor = ppPowers[i]->fScaleFactor;
		pNewRefNoConst->bGemSlotsAdjustScaleFactor = ppPowers[i]->bGemSlotsAdjustScaleFactor;
	}

	pNewDef->pItemDamageDef = StructClone(parse_ItemDamageDef, pGenData->pItemDamage);
	if (pNewDef->pItemDamageDef && pTier->fWeaponDamageVariance != -1)
		pNewDef->pItemDamageDef->fVariance = pTier->fWeaponDamageVariance;

	pNewDef->iPowerFactor = pTier->iPowerFactor;

	if (pRarity->iPowerFactor > -1)
		pNewDef->iPowerFactor = pRarity->iPowerFactor;

	pNewDef->eRestrictSlotType = pGenData->eRestrictSlotType;
	
	//create gem slots
	if (iGemSet < eaSize(&pRarity->eaGemSlotData))
	{
		for(i = 0; i < ea32Size(&pRarity->eaGemSlotData[iGemSet]->eaTypes); i++)
		{
			ItemGemSlotDef* pSlotDef = StructCreate(parse_ItemGemSlotDef);
			pSlotDef->eType = pRarity->eaGemSlotData[iGemSet]->eaTypes[i];
			eaPush(&pNewDef->ppItemGemSlots, pSlotDef);
		}
	}

	if (pRarity->pchDisplayPrefix && pRarity->pchDisplayPrefix[0])
	{
		sprintf(schBuffer,"%s %s", pRarity->pchDisplayPrefix, schPrefix);
		removeLeadingAndFollowingSpaces(schBuffer);
		strcpy(schPrefix,schBuffer);
	}

	if (pTier->pchDisplayPrefix && pTier->pchDisplayPrefix[0])
	{
		sprintf(schBuffer,"%s %s", pTier->pchDisplayPrefix, schPrefix);
		removeLeadingAndFollowingSpaces(schBuffer);
		strcpy(schPrefix,schBuffer);
	}

	if (pTier->pchDisplaySuffix && pTier->pchDisplaySuffix[0])
	{
		sprintf(schBuffer,"%s %s", pTier->pchDisplaySuffix, schSuffix);
		removeLeadingAndFollowingSpaces(schBuffer);
		strcpy(schSuffix,schBuffer);
	}

	if (pRarity->pchDisplaySuffix && pRarity->pchDisplaySuffix[0])
	{
		sprintf(schBuffer,"%s %s", pRarity->pchDisplaySuffix, schPrefix);
		removeLeadingAndFollowingSpaces(schBuffer);
		strcpy(schSuffix,schBuffer);
	}

	if(pGenData->eSuffixOption != kItemGenSuffix_None)
	{
		const char **ppchDisplayNames = NULL;
		int *piCounts = NULL;

		for(i=0;i<eaSize(&ppPowers);i++)
		{
			if(	pGenData->eSuffixOption == kItemGenSuffix_All
				|| (ppPowers[i]->eRarity == ItemGenRarity_Base && pGenData->eSuffixOption == kItemGenSuffix_BaseOnly)
				|| (ppPowers[i]->eRarity != ItemGenRarity_Base && pGenData->eSuffixOption == kItemGenSuffix_ExtendOnly))
			{
				ItemPowerDef *pItemPower = GET_REF(ppPowers[i]->hItemPowerDef);
				const char *pchDisplayName = NULL;
				
				if (pItemPower)
				{
					Message *pMessage = GET_REF(pItemPower->displayNameMsg.hMessage);
					if (pMessage)
					{
						pchDisplayName = pMessage->pcDefaultString;
					}
				}
				else
				{
					pchDisplayName = ppPowers[i]->itemPowerDefData.pchDisplayName;
				}
				
				if(pchDisplayName && pchDisplayName[0])
				{
					int m;

					for(m=eaSize(&ppchDisplayNames)-1;m>=0;m--)
					{
						if(stricmp(ppchDisplayNames[m],pchDisplayName) == 0)
						{
							piCounts[m]++;
							break;
						}
					}

					if(m==-1)
					{
						eaPush(&ppchDisplayNames,pchDisplayName);
						ea32Push(&piCounts,1);
					}
				}
			}
		}

		for(i=0;i<eaSize(&ppchDisplayNames);i++)
		{
			if(piCounts[i] > 1)
			{
				sprintf(schBuffer,"%s %sx%d",schSuffix, ppchDisplayNames[i], piCounts[i]);
				removeLeadingAndFollowingSpaces(schBuffer);
				strcpy(schSuffix,schBuffer);
			}
			else
			{
				sprintf(schBuffer,"%s %s", schSuffix, ppchDisplayNames[i]);
				removeLeadingAndFollowingSpaces(schBuffer);
				strcpy(schSuffix,schBuffer);
			}
		}
	}

	if (pNewDef->iPowerFactor != 0)
	{
		sprintf(schBuffer,"+%d %s", pNewDef->iPowerFactor, schPrefix);
		removeLeadingAndFollowingSpaces(schBuffer);
		strcpy(schPrefix,schBuffer);
	}

	if (pGenData->pchDisplayName && pGenData->pchDisplayName[0])
	{
		Message *pNewMessage = StructCreate(parse_Message);
        
		sprintf(schNewName,"%s %s %s",schPrefix,pGenData->pchDisplayName,schSuffix);

		removeLeadingAndFollowingSpaces(schNewName);
		pNewMessage->pcDefaultString = StructAllocString(schNewName);
		pNewDef->displayNameMsg.pEditorCopy = pNewMessage;
	}
	else if (GET_REF(pGenData->hSpecies))
	{
		Message *pNewMessage = StructCreate(parse_Message);
		SpeciesDef* pSpecies = GET_REF(pGenData->hSpecies);
		NameTemplateListRef** eaNameTemplateListRefs = (NameTemplateListRef**)pSpecies->eaNameTemplateLists;
		const char* pchSpeciesName = namegen_GenerateFullName(eaNameTemplateListRefs, pSeed);
			
		sprintf(schNewName, "%s", pchSpeciesName);
			
		removeLeadingAndFollowingSpaces(schNewName);
		pNewMessage->pcDefaultString = StructAllocString(schNewName);
		pNewDef->displayNameMsg.pEditorCopy = pNewMessage;
	}

	item_FixMessages(pNewDef);

	return pNewDef;
}

static void itemGen_GetUnidentifiedWrapperDefName(ItemGenData *pGenData, ItemGenRarityDef *pRarity, int iTier, char** ppchNameOut)
{
	estrPrintf(ppchNameOut, "%s_%s_T%d_UNID", pGenData->pchDataName,StaticDefineIntRevLookup(ItemGenRarityEnum,pRarity->eRarityType),iTier);
}

static ItemDef *itemGen_MakeNewUnidentifiedWrapperDef(ItemGenData *pGenData, ItemGenTier* pTier, ItemGenRarityDef *pRarity)
{
	ItemDef *pNewDef = StructCreate(parse_ItemDef);
	char *eStringName;
	char schNewName[255];
	int iTier = pTier->iTier;

	devassertmsg(pTier,"Tier not found, this is a real problem, and you should get Michael McCarry right away!");

	estrCreate(&eStringName);

	itemGen_GetUnidentifiedWrapperDefName(pGenData, pRarity, pTier->iTier, &eStringName);

	pNewDef->pchName = StructAllocString(eStringName);

	estrDestroy(&eStringName);

	sprintf(schNewName,"ItemGen/%s",pGenData->pchScope);
	pNewDef->pchScope = StructAllocString(schNewName);

	sprintf(schNewName,"Defs/Items/ItemGen/%s/%s.item",pGenData->pchScope,pGenData->pchDataName);
	pNewDef->pchFileName = allocAddFilename(schNewName);

	pNewDef->flags = pGenData->flags;

	pNewDef->flags |= pRarity->eFlagsToAdd;
	pNewDef->flags &= ~pRarity->eFlagsToRemove;

	pNewDef->eType = kItemType_UnidentifiedWrapper;
	COPY_HANDLE(pNewDef->hSpecies, pGenData->hSpecies);

	// Copy Restrict Bags
	eaiCopy(&pNewDef->peRestrictBagIDs, &pGenData->peRestrictBags);

	// Copy equip limit
	if (pGenData->pEquipLimit && (pGenData->pEquipLimit->eCategory || pGenData->pEquipLimit->iMaxEquipCount))
		pNewDef->pEquipLimit =  StructClone(parse_ItemEquipLimit, pGenData->pEquipLimit);

	if(IS_HANDLE_ACTIVE(pGenData->hSlotID))
	{
		COPY_HANDLE(pNewDef->hSlotID,pGenData->hSlotID);
	}

	//Pull Tier information
	pNewDef->iLevel = pTier->iTrueLevel;

	if(pTier->pRequires)
		pNewDef->pRestriction = StructClone(parse_UsageRestriction,pTier->pRequires);

	if(pRarity->pchIconName)
		pNewDef->pchIconName = allocAddString(pRarity->pchIconName);
	else if(pTier->pchIconName)
		pNewDef->pchIconName = allocAddString(pTier->pchIconName);
	else if(pGenData->pchIconName)
		pNewDef->pchIconName = allocAddString(pGenData->pchIconName);
	else if (pGenData->bGenerateSpeciesIcons)
		ItemGen_GenerateIcon(pNewDef, pGenData);

	if(pRarity->eItemTag)
	{
		pNewDef->eTag = pRarity->eItemTag;
	}
	else if(pTier->eItemTag)
	{
		pNewDef->eTag = pTier->eItemTag;
	}
	else if(pGenData->eItemTag)
	{
		pNewDef->eTag = pGenData->eItemTag;
	}

	// Item category arrays are additive
	itemGen_AddItemCategories(pNewDef, pGenData->peCategories);
	itemGen_AddItemCategories(pNewDef, pTier->peCategories);
	itemGen_AddItemCategories(pNewDef, pRarity->peCategories);

	if (!pNewDef->pchIconName && eaSize(&pRarity->eaGenerationLevels))
	{
		//Grab the first listed icon to use for this unidentified stub.
		if (pRarity->eaGenerationLevels[0] && pRarity->eaGenerationLevels[0]->pchIconName)
			pNewDef->pchIconName = allocAddString(pRarity->eaGenerationLevels[0]->pchIconName);
	}
	else if(eaSize(&pRarity->ppCostumes))
		eaCopyStructs(&pRarity->ppCostumes,&pNewDef->ppCostumes,parse_ItemCostume);
	else if(eaSize(&pGenData->ppItemTiers[iTier]->ppCostumes))
		eaCopyStructs(&pGenData->ppItemTiers[iTier]->ppCostumes,&pNewDef->ppCostumes,parse_ItemCostume);
	else if(eaSize(&pGenData->ppCostumes))
		eaCopyStructs(&pGenData->ppCostumes,&pNewDef->ppCostumes,parse_ItemCostume);

	pNewDef->Quality = pRarity->eQuality; 
	pNewDef->eRestrictSlotType = pGenData->eRestrictSlotType;

	if (pGenData->pchDisplayName && pGenData->pchDisplayName[0])
	{
		Message *pNewMessage = StructCreate(parse_Message);

		sprintf(schNewName,"Unidentified %s",pGenData->pchDisplayName);

		removeLeadingAndFollowingSpaces(schNewName);
		pNewMessage->pcDefaultString = StructAllocString(schNewName);
		pNewDef->displayNameMsg.pEditorCopy = pNewMessage;
	}

	item_FixMessages(pNewDef);

	return pNewDef;
}


static bool itemGen_FindPowerDataRecurse(ItemGenData *pGenData, ItemGenPowerData **ppInputData, ItemGenRarityDef *pRarity, int iStart, int iRarityIndex, int iTier, ItemGenPowerData ***pppPowers, ItemDef ***pppItems, RewardTable **ppRewardTableOut, U32 *pSeed)
{
	int i, iGem, iCostume;
	bool bFoundPower = false;

	if(!ppInputData)
		return false;
	//If all the powers have been found
	if(iRarityIndex >= ea32Size(&pRarity->ePowerRarityChoices))
	{
		//Make items, always want to make at least 1 despite a lack of gem sets or costume combinations
		int iNumCostumeCombinations = max(eaSize(&pRarity->eaGenerationLevels), 1);
		int iNumGemCombinations = max(eaSize(&pRarity->eaGemSlotData), 1);
		for (iCostume = 0; iCostume < iNumCostumeCombinations; iCostume++)
		{
			for (iGem = 0; iGem < iNumGemCombinations; iGem++)
			{
				ItemDef *pNewDef = itemGen_MakeNewItemDef(pGenData,*pppPowers,iTier,pRarity,iGem, iCostume, pSeed);
				F32 fWeight = pRarity->eaGemSlotData && pRarity->eaGemSlotData[iGem] ? pRarity->eaGemSlotData[iGem]->fWeight : 1.0;
			
				eaPush(pppItems,pNewDef);

				if(ppRewardTableOut)
				{
					RewardEntry *pNewEntry;
					if(!(*ppRewardTableOut))
					{
						char *eStringName = NULL;

						(*ppRewardTableOut) = StructCreate(parse_RewardTable);

						estrCreate(&eStringName);

						estrPrintf(&eStringName,"%s_%s_T%d",pGenData->pchDataName,StaticDefineIntRevLookup(ItemGenRarityEnum,pRarity->eRarityType),iTier);
						(*ppRewardTableOut)->pchName = StructAllocString(eStringName);

						estrClear(&eStringName);
						estrPrintf(&eStringName,"ItemGen/%s/%s",pGenData->pchScope,StaticDefineIntRevLookup(ItemGenRarityEnum,pRarity->eRarityType));
						(*ppRewardTableOut)->pchScope = StructAllocString(eStringName);

						estrClear(&eStringName);
						estrPrintf(&eStringName,"Defs/rewards/%s/%s.rewards",(*ppRewardTableOut)->pchScope,(*ppRewardTableOut)->pchName);
						(*ppRewardTableOut)->pchFileName = StructAllocString(eStringName);
						estrDestroy(&eStringName);

						(*ppRewardTableOut)->NumChoices = 1;
						(*ppRewardTableOut)->NumPicks = 1;
						(*ppRewardTableOut)->LaunchType = pGenData->eLaunchType;
						(*ppRewardTableOut)->flags = pGenData->eRewardFlags;
						(*ppRewardTableOut)->PickupType = pGenData->eRewardPickupType;

						if(IS_HANDLE_ACTIVE(pGenData->hYoursCostumeRef))
						{
							COPY_HANDLE((*ppRewardTableOut)->hYoursCostumeRef,pGenData->hYoursCostumeRef);
						}

						if(IS_HANDLE_ACTIVE(pGenData->hNotYoursCostumeRef))
						{
							COPY_HANDLE((*ppRewardTableOut)->hNotYoursCostumeRef,pGenData->hNotYoursCostumeRef);
						}
					}

					pNewEntry = StructCreate(parse_RewardEntry);

					pNewEntry->ChoiceType = kRewardChoiceType_Choice;
					pNewEntry->Type = kRewardType_Item;
					pNewEntry->Count = 1;
					pNewEntry->fWeight = fWeight;

					if (eaSize(&pRarity->eaGenerationLevels))
					{
						pNewEntry->MinLevel = max(1, pRarity->eaGenerationLevels[iCostume]->iLevel - pGenData->ppItemTiers[iTier]->iDropWithinLevelDelta);
						pNewEntry->MaxLevel = min(MAX_LEVELS, pRarity->eaGenerationLevels[iCostume]->iLevel + pGenData->ppItemTiers[iTier]->iDropWithinLevelDelta);
					}

					if (pRarity->bGenerateWithUnidentifiedWrappers)
					{
						char* eStringName = NULL;
						pNewEntry->Type = kRewardType_UnidentifiedItemWrapper;
						itemGen_GetUnidentifiedWrapperDefName(pGenData, pRarity, iTier, &eStringName);
						pNewEntry->iUnidentifiedResultLevel = pNewDef->iLevel;
						SET_HANDLE_FROM_STRING(g_hItemDict,eStringName,pNewEntry->hItemDef);
						SET_HANDLE_FROM_STRING(g_hItemDict,pNewDef->pchName,pNewEntry->hUnidentifiedResultDef);
						estrDestroy(&eStringName);
					}
					else
					{
						SET_HANDLE_FROM_STRING(g_hItemDict,pNewDef->pchName,pNewEntry->hItemDef);
					}

					if(IS_HANDLE_ACTIVE(pRarity->hRewardCostume))
					{
						COPY_HANDLE(pNewEntry->hCostumeDef,pRarity->hRewardCostume);
					}
					else if(IS_HANDLE_ACTIVE(pGenData->hRewardCostume))
					{
						COPY_HANDLE(pNewEntry->hCostumeDef,pGenData->hRewardCostume);
					}

					eaPush(&(*ppRewardTableOut)->ppRewardEntry,pNewEntry);
				}
			}
		}
		//Make new entry into reward table
		return true;
	}

	if(!pRarity->ePowerRarityChoices)
		return false;

	//More powers need to be found
	for(i=iStart;i<eaSize(&ppInputData);i++)
	{
		if(ppInputData[i]->eRarity == (ItemGenRarity)pRarity->ePowerRarityChoices[iRarityIndex])
		{
			if(ppInputData[i]->uiCategory != 0)
			{
				//See if a power from this category has already been chosen
				int j;

				for(j=eaSize(pppPowers)-1;j>=0;j--)
				{
					if((*pppPowers)[j]->uiCategory == ppInputData[i]->uiCategory)
						break;
				}

				if(j!=-1)
					continue;
			}

			if(ppInputData[i]->bNoStack == true)
			{
				int j;

				for(j=eaSize(pppPowers)-1;j>=0;j--)
				{
					if((*pppPowers)[j] == ppInputData[i])
						break;
				}

				if(j!=-1)
					continue;
			}
			bFoundPower = true;
			eaPush(pppPowers,ppInputData[i]);
			
			if(!itemGen_FindPowerDataRecurse(pGenData,ppInputData,pRarity,i,iRarityIndex+1,iTier,pppPowers,pppItems,ppRewardTableOut,pSeed))
			{
				eaFindAndRemove(pppPowers,ppInputData[i]);
				return true;
			}

			eaFindAndRemove(pppPowers,ppInputData[i]);
		}
	}

	return bFoundPower;
}

static bool itemGen_FindPowerDataRecurse_BaseCategories(ItemGenData *pGenData, ItemGenPowerData **ppInputData, ItemGenRarityDef *pRarity, int iStart, U32 *puiCategories, int iCategoryIndex, int iTier, ItemGenPowerData ***pppPowers, ItemDef ***pppItems,RewardTable **ppRewardTableOut, U32 *pSeed)
{
	int i;
	bool bFoundPower = false;

	if(!ppInputData)
		return false;

	if(iCategoryIndex >= ea32Size(&puiCategories))
	{
		for(i=0;i<eaSize(&ppInputData);i++)
		{
			if(ppInputData[i]->eRarity != ItemGenRarity_Base)
				break;
		}

		itemGen_FindPowerDataRecurse(pGenData,ppInputData,pRarity,i,0,iTier,pppPowers,pppItems,ppRewardTableOut,pSeed);
		return true;
	}

	if(!puiCategories)
		return false;

	for(i=iStart;i<eaSize(&ppInputData);i++)
	{
		if(ppInputData[i]->eRarity == ItemGenRarity_Base
			&& ppInputData[i]->uiCategory == puiCategories[iCategoryIndex])
		{
			bFoundPower = true;
			eaPush(pppPowers,ppInputData[i]);
			
			if(!itemGen_FindPowerDataRecurse_BaseCategories(pGenData,ppInputData,pRarity,i+1,puiCategories,iCategoryIndex+1,iTier,pppPowers,pppItems,ppRewardTableOut,pSeed))
				return false;
			eaFindAndRemove(pppPowers,ppInputData[i]);
		}
	}

	return bFoundPower;
}

static void itemGen_AddRarityRewardTable(ItemGenData *pGenData, ItemGenRarityDef *pGenRarity, ItemGenTier *pGenTier,RewardTable *pRewardTable)
{
	RewardTable *pRarityRewardTable = NULL;
	RewardEntry *pRewardEntry = NULL;
	int i;

	if(!pGenData->ppRewardTables 
		||eaSize(&pGenData->ppRewardTables) <= pGenRarity->eRarityType 
		|| pGenData->ppRewardTables[pGenRarity->eRarityType] == NULL)
	{
		char *eStringName = NULL;

		pRarityRewardTable = StructCreate(parse_RewardTable);

		estrCreate(&eStringName);
		estrPrintf(&eStringName,"%s_%s",pGenData->pchDataName,StaticDefineIntRevLookup(ItemGenRarityEnum,pGenRarity->eRarityType));
		pRarityRewardTable->pchName = StructAllocString(eStringName);

		estrClear(&eStringName);
		estrPrintf(&eStringName,"ItemGen/%s/%s",pGenData->pchScope,StaticDefineIntRevLookup(ItemGenRarityEnum,pGenRarity->eRarityType));
		pRarityRewardTable->pchScope = StructAllocString(eStringName);

		estrClear(&eStringName);
		estrPrintf(&eStringName,"Defs/Rewards/%s/%s.rewards",pRarityRewardTable->pchScope,pRarityRewardTable->pchName);
		pRarityRewardTable->pchFileName = StructAllocString(eStringName);

		estrDestroy(&eStringName);

		pRarityRewardTable->NumChoices = 1;
		pRarityRewardTable->NumPicks = 1;

		if(eaSize(&pGenData->ppRewardTables) <= pGenRarity->eRarityType)
			eaSetSize(&pGenData->ppRewardTables,pGenRarity->eRarityType);
		eaInsert(&pGenData->ppRewardTables,pRarityRewardTable,pGenRarity->eRarityType);
	}else{
		pRarityRewardTable = pGenData->ppRewardTables[pGenRarity->eRarityType];
	}

	pRewardEntry = StructCreate(parse_RewardEntry);

	pRewardEntry->ChoiceType = kRewardChoiceType_LevelRange;
	pRewardEntry->MinLevel = pGenTier->iLevelMin;
	pRewardEntry->MaxLevel = pGenTier->iLevelMax;
	SET_HANDLE_FROM_STRING(g_hRewardTableDict,pRewardTable->pchName,pRewardEntry->hRewardTable);

	eaPush(&pRarityRewardTable->ppRewardEntry,pRewardEntry);

	for(i=0;i<eaiSize(&pGenData->peRewardCategories);i++)
	{
		char *pchRewardCat;
		const char *pchRewardCatName = StaticDefineIntRevLookup(ItemGenRewardCategoryEnum, pGenData->peRewardCategories[i]);
		const char *pchRarityName = StaticDefineIntRevLookup(ItemGenRarityEnum,pGenRarity->eRarityType);
		RewardTable *pCatTable = NULL;
		RewardEntry *pCatEntry = NULL;
		int j;
		
		estrStackCreate(&pchRewardCat);
		estrPrintf(&pchRewardCat, "%s_%s", pchRewardCatName, pchRarityName);
		
		for(j=eaSize(&pGenData->ppCategoryTables)-1;j>=0;j--)
		{
			if(stricmp(pGenData->ppCategoryTables[j]->pchName,pchRewardCat)==0)
			{
				pCatTable = pGenData->ppCategoryTables[j];
				break;
			}
		}
		if(!pCatTable)
		{
			pCatTable = RefSystem_ReferentFromString(g_hRewardTableDict,pchRewardCat);

			if(pCatTable)
			{
				RewardTable *pCpyTable = StructCreate(parse_RewardTable);
				StructCopyAll(parse_RewardTable,pCatTable,pCpyTable);
				pCatTable = pCpyTable;
			}
		}

		if(!pCatTable)
		{
			char *eStringName;
			pCatTable = StructCreate(parse_RewardTable);

			pCatTable->pchName = StructAllocString(pchRewardCat);
			pCatTable->pchScope = "ItemGen";

			estrCreate(&eStringName);
			estrPrintf(&eStringName,"Defs/Rewards/ItemGen/%s.rewards",pchRewardCat);
			pCatTable->pchFileName = StructAllocString(eStringName);

			estrDestroy(&eStringName);
			pCatTable->NumChoices = 1;
			pCatTable->NumPicks = 1;
		}

		eaPushUnique(&pGenData->ppCategoryTables,pCatTable);

		for(j=eaSize(&pCatTable->ppRewardEntry)-1;j>=0;j--)
		{
			if(stricmp(REF_STRING_FROM_HANDLE(pCatTable->ppRewardEntry[j]->hRewardTable),pRarityRewardTable->pchName)==0)
				break;
		}

		if(j==-1)
		{
			pCatEntry = StructCreate(parse_RewardEntry);
			pCatEntry->ChoiceType = kRewardChoiceType_LevelRange;
			pCatEntry->MinLevel = pGenTier->iLevelMin;
			pCatEntry->MaxLevel = pGenTier->iLevelMax;
			SET_HANDLE_FROM_STRING(g_hRewardTableDict,pRarityRewardTable->pchName,pCatEntry->hRewardTable);
			eaPush(&pCatTable->ppRewardEntry,pCatEntry);
		}
		else
		{
			pCatEntry = pCatTable->ppRewardEntry[j];
			pCatEntry->MinLevel = min(pCatEntry->MinLevel,pGenTier->iLevelMin);
			pCatEntry->MaxLevel = max(pCatEntry->MaxLevel,pGenTier->iLevelMax);
		}

		estrDestroy(&pchRewardCat);
	}

}

static RewardTable* itemGen_CreateSingleRarityRewardTable(ItemGenData *pGenData, ItemGenRarityDef *pGenRarity, ItemGenTier *pGenTier)
{
	RewardTable *pRarityRewardTable = NULL;
	char *eStringName = NULL;
	pRarityRewardTable = StructCreate(parse_RewardTable);

	estrCreate(&eStringName);
	estrPrintf(&eStringName,"%s_%s",pGenData->pchDataName,StaticDefineIntRevLookup(ItemGenRarityEnum,pGenRarity->eRarityType));
	pRarityRewardTable->pchName = StructAllocString(eStringName);

	estrClear(&eStringName);
	estrPrintf(&eStringName,"ItemGen/%s/%s",pGenData->pchScope,StaticDefineIntRevLookup(ItemGenRarityEnum,pGenRarity->eRarityType));
	pRarityRewardTable->pchScope = StructAllocString(eStringName);

	estrClear(&eStringName);
	estrPrintf(&eStringName,"Defs/Rewards/%s/%s.rewards",pRarityRewardTable->pchScope,pRarityRewardTable->pchName);
	pRarityRewardTable->pchFileName = StructAllocString(eStringName);

	estrDestroy(&eStringName);

	pRarityRewardTable->NumChoices = 1;
	pRarityRewardTable->NumPicks = 1;
	pRarityRewardTable->LaunchType = pGenData->eLaunchType;
	pRarityRewardTable->flags = pGenData->eRewardFlags;
	pRarityRewardTable->PickupType = pGenData->eRewardPickupType;

	if(IS_HANDLE_ACTIVE(pGenData->hYoursCostumeRef))
	{
		COPY_HANDLE(pRarityRewardTable->hYoursCostumeRef,pGenData->hYoursCostumeRef);
	}

	if(IS_HANDLE_ACTIVE(pGenData->hNotYoursCostumeRef))
	{
		COPY_HANDLE(pRarityRewardTable->hNotYoursCostumeRef,pGenData->hNotYoursCostumeRef);
	}
	return pRarityRewardTable;
}

static U32 itemGen_GenerateRarity(ItemGenData *pGenData, ItemGenTier *pGenTier, ItemGenRarityDef *pGenRarity, ItemDef ***pppItemDefsOut, RewardTable ***pppRewardTablesOut, U32 *pSeed)
{
	int i;
	U32 iItemCount =0;
	U32 *puiCategories=NULL;
	ItemGenPowerData **ppPowerData = NULL;
	ItemGenPowerData **ppSelectedPowers = NULL;
	//Find all the usable powers

	iItemCount = eaSize(pppItemDefsOut) * -1;

	for(i=0;i<eaSize(&pGenData->ppPowerData);i++)
	{
		ItemGenPowerData *pPowerData = pGenData->ppPowerData[i];
		if(pPowerData->iTierMin <= pGenTier->iTier 
			&& pPowerData->iTierMax >= pGenTier->iTier
			&& (ea32Find(&pGenRarity->ePowerRarityChoices,pPowerData->eRarity) != -1
			|| pPowerData->eRarity == ItemGenRarity_Base))
		{
			eaPush(&ppPowerData, pPowerData);
		}
	}

	//Find all categories under Rarity "base"
	for(i=0;i<eaSize(&ppPowerData);i++)
	{
		if(ppPowerData[i]->eRarity == ItemGenRarity_Base
			&& ppPowerData[i]->uiCategory != 0
			&& ea32Find(&puiCategories,ppPowerData[i]->uiCategory) == -1)
		{
			ea32Push(&puiCategories,ppPowerData[i]->uiCategory);
		}
	}
	


	for(i=0;i<eaSize(&ppPowerData);i++)
	{
		RewardTable *pRewardTable = NULL;

		if (eaSize(&pGenRarity->eaGenerationLevels))
		{
			int j;
			bool bDupFound = false;
			pRewardTable = itemGen_CreateSingleRarityRewardTable(pGenData, pGenRarity, pGenTier);
			//look for already-created table
			for (j = 0; j < eaSize(pppRewardTablesOut); j++)
			{
				//pooled string comparison
				if (strcmp(pRewardTable->pchName, (*pppRewardTablesOut)[j]->pchName) == 0)
				{
					StructDestroy(parse_RewardTable, pRewardTable);
					pRewardTable = (*pppRewardTablesOut)[j];
					bDupFound = true;
					break;
				}
			}
			if (!bDupFound)
				eaPush(pppRewardTablesOut,pRewardTable);
		}
		if(ppPowerData[i]->eRarity == ItemGenRarity_Base
			&& ppPowerData[i]->uiCategory == 0)
		{
			eaPush(&ppSelectedPowers,ppPowerData[i]);
			if(i < eaSize(&ppPowerData)-1)
				continue;
		}

		itemGen_FindPowerDataRecurse_BaseCategories(pGenData,ppPowerData,pGenRarity,i,puiCategories,0,pGenTier->iTier,&ppSelectedPowers,pppItemDefsOut,&pRewardTable,pSeed);

		if(pRewardTable && eaSize(&pGenRarity->eaGenerationLevels) == 0)
		{
			eaPush(pppRewardTablesOut,pRewardTable);
			itemGen_AddRarityRewardTable(pGenData,pGenRarity,pGenTier,pRewardTable);
		}
		break;
	}

	if (pGenRarity->bGenerateWithUnidentifiedWrappers)
	{
		ItemDef* pWrapperDef = itemGen_MakeNewUnidentifiedWrapperDef(pGenData, pGenTier, pGenRarity);
		eaPush(pppItemDefsOut, pWrapperDef);
	}


    eaDestroy(&ppPowerData);
    eaDestroy(&ppSelectedPowers);

	iItemCount += eaSize(pppItemDefsOut);

	return iItemCount;
}

static U32 itemGen_GenerateTier(ItemGenData *pGenData, ItemGenTier *pGenTier, ItemDef ***pppItemDefsOut, RewardTable ***pppRewardTablesOut, U32 *pSeed)
{
	U32 iItemCount =0;
	int i;
	
	for(i=0;i<eaSize(&pGenTier->ppRarities);i++)
	{
		int iTempCount = itemGen_GenerateRarity(pGenData,pGenTier,pGenTier->ppRarities[i],pppItemDefsOut,pppRewardTablesOut,pSeed);
		printf("\t\t%s Teir %d Rarity %s Generated %d ItemDefs\n",pGenData->pchDataName,pGenTier->iTier,StaticDefineIntRevLookup(ItemGenRarityEnum,pGenTier->ppRarities[i]->eRarityType), iTempCount);
		iItemCount += iTempCount;
	}

	return iItemCount;
}

static ItemPowerDef *itemGen_NewPowerDef(ItemGenData *pGenData, ItemGenPowerData *pPowerData)
{
	if (IS_HANDLE_ACTIVE(pPowerData->itemPowerDefData.hPower))
	{
		ItemPowerDef *pNewDef = StructCreate(parse_ItemPowerDef);
		Message *pNewMessage;
		char schNewName[255];

		// Copy Display Name
		pNewMessage = StructCreate(parse_Message);
		pNewMessage->pcDefaultString = StructAllocString(pPowerData->itemPowerDefData.pchDisplayName);
		pNewDef->descriptionMsg.pEditorCopy = pNewMessage;
		// Copy Display Name 2
		pNewMessage = StructCreate(parse_Message);
		pNewMessage->pcDefaultString = StructAllocString(pPowerData->itemPowerDefData.pchDisplayName2);
		pNewDef->displayNameMsg2.pEditorCopy = pNewMessage;
		// Copy Description
		pNewMessage = StructCreate(parse_Message);
		pNewMessage->pcDefaultString = StructAllocString(pPowerData->itemPowerDefData.pchDescription);
		pNewDef->descriptionMsg.pEditorCopy = pNewMessage;

		// Copy generic fields
		pNewDef->flags = pPowerData->itemPowerDefData.flags;
		pNewDef->pchIconName = allocAddString(pPowerData->itemPowerDefData.pchIconName);
		COPY_HANDLE(pNewDef->hPower, pPowerData->itemPowerDefData.hPower);
		COPY_HANDLE(pNewDef->hCraftRecipe, pPowerData->itemPowerDefData.hCraftRecipe);
		COPY_HANDLE(pNewDef->hPowerReplace, pPowerData->itemPowerDefData.hPowerReplace);
		COPY_HANDLE(pNewDef->hValueRecipe, pPowerData->itemPowerDefData.hValueRecipe);
		pNewDef->pRestriction = StructClone(parse_UsageRestriction, pPowerData->itemPowerDefData.pRestriction);
		pNewDef->pExprEconomyPoints = exprClone(pPowerData->itemPowerDefData.pExprEconomyPoints);
		pNewDef->pPowerConfig = StructClone(parse_CritterPowerConfig, pPowerData->itemPowerDefData.pPowerConfig);
		pNewDef->pchNotes = StructAllocString(pPowerData->itemPowerDefData.pchNotes);

		// Make a unique name
		sprintf(schNewName,"%s_%s",pGenData->pchDataName,pPowerData->pchInternalName);
		pNewDef->pchName = StructAllocString(schNewName);
		sprintf(schNewName,"ItemGen/%s",pGenData->pchScope);
		pNewDef->pchScope = StructAllocString(schNewName);
		sprintf(schNewName,"Defs/ItemPowers/ItemGen/%s/%s.itempower",pGenData->pchScope,pNewDef->pchName);
		pNewDef->pchFileName = StructAllocString(schNewName);

		itempower_FixMessages(pNewDef);

		return pNewDef;
	}
	return NULL;
}

// Generate Function, this function generates all the new items from the def files, and
// returns the number of items created
static U32 itemGen_GenerateItemDefs(ItemGenData *pGenData,
									ItemDef ***pppItemDefsOut, 
									ItemPowerDef ***pppItemPowerDefsOut, 
									RewardTable ***pppRewardTablesOut)
{
	U32 iItemCount = 0;
	int i;
	U32 uSeed = SAFE_MEMBER(pGenData, uSeed);

	if(!pGenData)
		return 0;

	if (!uSeed)
		uSeed = hashString(pGenData->pchDataName, false);

	if(eaSize(pppRewardTablesOut)>0)
	{
		char *pchRewardCat;
		int r;

		estrCreate(&pchRewardCat);

		for(r=0;r<g_pItemRarityCount;r++)
		{
			for(i=0;i<eaiSize(&pGenData->peRewardCategories);i++)
			{
				const char* pchRewardCatName = StaticDefineIntRevLookup(ItemGenRewardCategoryEnum, pGenData->peRewardCategories[i]);
				const char* pchRarityName = StaticDefineIntRevLookup(ItemGenRarityEnum,r);
				int j;
				

				estrPrintf(&pchRewardCat, "%s_%s", pchRewardCatName, pchRarityName);

				for(j=eaSize(pppRewardTablesOut)-1;j>=0;j--)
				{
					if(stricmp((*pppRewardTablesOut)[j]->pchName,pchRewardCat)==0)
					{
						eaPush(&pGenData->ppCategoryTables,(*pppRewardTablesOut)[j]);
						break;
					}
				}
				estrClear(&pchRewardCat);
			}
		}
		
		estrDestroy(&pchRewardCat);
	}
	

	for(i=0;i<eaSize(&pGenData->ppPowerData);i++)
	{
		if(!IS_HANDLE_ACTIVE(pGenData->ppPowerData[i]->hItemPowerDef) || pGenData->ppPowerData[i]->bGenerated == true)
		{
			ItemPowerDef *pNewDef = itemGen_NewPowerDef(pGenData,pGenData->ppPowerData[i]);

			if (pNewDef)
			{
				SET_HANDLE_FROM_STRING("ItemPowerDef",pNewDef->pchName,pGenData->ppPowerData[i]->hItemPowerDef);
			
				eaPush(pppItemPowerDefsOut,pNewDef);
				pGenData->ppPowerData[i]->bGenerated = true;
			}
		}
	}

	for(i=0;i<eaSize(&pGenData->ppItemTiers);i++)
	{
		int iTempCount = itemGen_GenerateTier(pGenData,pGenData->ppItemTiers[i],pppItemDefsOut,pppRewardTablesOut,&uSeed);
		iItemCount += iTempCount;
		printf("\t%s Tier %d Generated %d ItemDefs\n",pGenData->pchDataName,pGenData->ppItemTiers[i]->iTier, iTempCount); 
	}

	for(i=0;i<eaSize(&pGenData->ppRewardTables);i++)
	{
		if(pGenData->ppRewardTables[i])
			eaPush(pppRewardTablesOut,pGenData->ppRewardTables[i]);
	}

	for(i=0;i<eaSize(&pGenData->ppCategoryTables);i++)
	{
		eaPushUnique(pppRewardTablesOut,pGenData->ppCategoryTables[i]);
	}

	eaClear(&pGenData->ppRewardTables);
	eaClear(&pGenData->ppCategoryTables);

	return iItemCount;
}

static void ItemGen_DeleteItemGensWithScope(const char *pchScope)
{
	ItemDef **ppItems = NULL;
	ItemPowerDef **ppItemPowers = NULL;
	RewardTable **ppRewardTables = NULL;
	RewardTable **ppCatRewardTables = NULL;
	char schScopeName[255];
	int i;
	DictionaryEArrayStruct *eArrayStruct = resDictGetEArrayStruct(g_hItemDict);

	resSetDictionaryEditMode(g_hItemDict, true);
	resSetDictionaryEditMode(g_hItemPowerDict, true);
	resSetDictionaryEditMode(g_hRewardTableDict, true);

	resSetDictionaryEditMode(gMessageDict, true);

	sprintf(schScopeName,"ItemGen/%s",pchScope);

	for(i=eaSize(&eArrayStruct->ppReferents)-1;i>=0;i--)
	{
		ItemDef *pItem = eArrayStruct->ppReferents[i];

		if(pItem->pchScope && stricmp(pItem->pchScope,schScopeName) == 0)
			eaPush(&ppItems,pItem);
	}

	eArrayStruct = resDictGetEArrayStruct(g_hItemPowerDict);

	for(i=eaSize(&eArrayStruct->ppReferents)-1;i>=0;i--)
	{
		ItemPowerDef *pItemPower = eArrayStruct->ppReferents[i];

		if(pItemPower->pchScope && stricmp(pItemPower->pchScope,schScopeName) == 0)
			eaPush(&ppItemPowers,pItemPower);
	}

	eArrayStruct = resDictGetEArrayStruct(g_hRewardTableDict);

	for(i=eaSize(&eArrayStruct->ppReferents)-1;i>=0;i--)
	{
		RewardTable *pRewardTable = eArrayStruct->ppReferents[i];

		if(pRewardTable->pchScope && strStartsWith(pRewardTable->pchScope,schScopeName))
			eaPush(&ppRewardTables,pRewardTable);

		if(pRewardTable->pchScope && stricmp(pRewardTable->pchScope,"ItemGen") == 0)
		{
			int j;

			for(j=eaSize(&pRewardTable->ppRewardEntry)-1;j>=0;j--)
			{
				RewardTable *pTableCheck = GET_REF(pRewardTable->ppRewardEntry[j]->hRewardTable);

				if(pTableCheck && strStartsWith(pTableCheck->pchScope,schScopeName))
				{
					RewardTable *pRewardTableCpy = NULL;
					int n;

					for(n=eaSize(&ppCatRewardTables)-1;n>=0;n--)
					{
						if(stricmp(pRewardTable->pchName,ppCatRewardTables[n]->pchName) == 0)
						{
							pRewardTableCpy = ppCatRewardTables[n];
							break;
						}
					}
					
					if(n==-1)
					{
						pRewardTableCpy = StructCreate(parse_RewardTable);
						StructCopyAll(parse_RewardTable,pRewardTable,pRewardTableCpy);
						eaPush(&ppCatRewardTables,pRewardTableCpy);
					}

					eaRemove(&pRewardTableCpy->ppRewardEntry,j);
				}
			}
		}
	}

	for(i=0;i<eaSize(&ppCatRewardTables);i++)
	{
		
		//Reward table is empty, remove it
		char buf[260];
		int result;
		fileLocatePhysical(ppCatRewardTables[i]->pchFileName,buf);
		result = !remove(buf); // Remove returns 0 on success and 1 on failure

		if(eaSize(&ppCatRewardTables[i]->ppRewardEntry)!=0)
		{
			//Reward table still has entries, save it out
			if(!resGetLockOwner(g_hRewardTableDict, ppCatRewardTables[i]->pchName))
			{
				resRequestLockResource(g_hRewardTableDict,ppCatRewardTables[i]->pchName,ppCatRewardTables[i]);
			}

			ParserWriteTextFileFromSingleDictionaryStruct(ppCatRewardTables[i]->pchFileName,g_hRewardTableDict,ppCatRewardTables[i],0,0);

		}
	}

	for(i=0;i<eaSize(&ppRewardTables);i++)
	{
		char buf[260];
		int result;
		fileLocatePhysical(ppRewardTables[i]->pchFileName,buf);
		result = !remove(buf); // Remove returns 0 on success and 1 on failure
	}

	for(i=0;i<eaSize(&ppItems);i++)
	{
		char buf[260];
		int result;
		DisplayMessage pMessage = ppItems[i]->displayNameMsg;
		fileLocatePhysical(ppItems[i]->pchFileName, buf);
		result = !remove(buf); // Remove returns 0 on success and 1 on failure

		if(result)
		{
			Message *pMessageDef = GET_REF(pMessage.hMessage);
			if(pMessageDef)
			{
				fileLocatePhysical(pMessageDef->pcFilename,buf);
				result = !remove(buf); // Remove returns 0 on success and 1 on failure
			}
			
		}
	}

	for(i=0;i<eaSize(&ppItemPowers);i++)
	{
		char buf[260];
		int result;
		fileLocatePhysical(ppItemPowers[i]->pchFileName, buf);
		result = !remove(buf); // Remove returns 0 on success and 1 on failure
	}
}

AUTO_COMMAND;
void ItemGen_RemoveDefsFromGen(const char *pchItemGenDef)
{
	ItemGenData *pItemGen = RefSystem_ReferentFromString(g_hItemGenDict,pchItemGenDef);

	if(pItemGen)
		ItemGen_DeleteItemGensWithScope(pItemGen->pchScope);
}

static void ItemGen_GenerateSingleDef(ItemGenData *pItemGen, ItemDef ***pppItemDefs, ItemPowerDef ***pppItemPowerDefs, RewardTable ***pppRewardTables)
{
	if(!pItemGen)
		return;

	resSetDictionaryEditMode(g_hItemDict, true);
	resSetDictionaryEditMode(g_hItemPowerDict, true);
	resSetDictionaryEditMode(g_hRewardTableDict,true);

	resSetDictionaryEditMode(gMessageDict, true);

	itemGen_GenerateItemDefs(pItemGen,pppItemDefs,pppItemPowerDefs,pppRewardTables);
}

MsgKeyFixup *ItemGenFixup_CreateMsgKeyFixup(Message *pOldMessage, Message *pNewMessage)
{
	if(pOldMessage && pNewMessage)
	{
		MsgKeyFixup *pFixup = StructCreate(parse_MsgKeyFixup);

		pFixup->pcNewKey = pNewMessage->pcMessageKey;
		pFixup->pcOldKey = pOldMessage->pcMessageKey;
		pFixup->pcNewDescripton = StructAllocString(pNewMessage->pcDescription);
		pFixup->pFilename = StructAllocString(pNewMessage->pcFilename);

		return pFixup;
	}

	return NULL;
}

void ItemGen_RunCompareMethod(ItemGenData **ppItemGens, ItemGenData **ppIconData)
{
	int i = 2356;
	ItemDef **ppItemDefs = NULL;
	ItemDef **ppOldItemDefs = NULL;
	ItemPowerDef **ppItemPowerDefs = NULL;
	RewardTable **ppRewardTables = NULL;
	int iTotalConflicts = 0;
	int iTotalMissing = 0;
	int iTotalSafe = 0;
	const char **ppchMissingDefs = NULL;
	int iSize = eaSize(&ppItemGens);

	// If you're running this command, you may want to manually split up the loop
	// into smaller chunks or it will take a long time
	for(i=0;i<1000 && i<eaSize(&ppItemGens);i++)
	{
		int iConflicts = 0;
		int iMissing = 0;
		int iSafe = 0;
		int n;
		MsgKeyFixup **ppFixups = NULL;

		ItemGen_GenerateSingleDef(ppItemGens[i],&ppItemDefs,&ppItemPowerDefs,&ppRewardTables);
		
		printf("%s Generated %d ItemDefs, checking for conflicts\n",ppItemGens[i]->pchDataName,eaSize(&ppItemDefs));

		for(n=0;n<eaSize(&ppItemDefs);n++)
		{
			ItemDef *pGenDef = ppItemDefs[n];
			ItemDef *pCurrentDef = item_DefFromName(ppItemDefs[n]->pchName);
			StructDiff *diff;
			int iCostume;
			MsgKeyFixup *pFixup = NULL;

			if(!pCurrentDef)
			{
				iMissing++;
				printf("ItemDef %s Generated but not found\n",ppItemDefs[n]->pchName);
				eaPush(&ppchMissingDefs,ppItemDefs[n]->pchName);
				continue;
			}

			eaPush(&ppOldItemDefs,pCurrentDef);

			if(ppItemGens[i]->bGenerateSpeciesIcons)
			{
				pGenDef->pchIconName = pCurrentDef->pchIconName;
			}

			//Weird things happen with these fields, so just copy them
			pGenDef->iSortID = pCurrentDef->iSortID;
			if(pCurrentDef->pExprEconomyPoints)
			{
				StructDestroySafe(parse_Expression,&pGenDef->pExprEconomyPoints);
				pGenDef->pExprEconomyPoints = StructClone(parse_Expression,pCurrentDef->pExprEconomyPoints);
			}

			if(pCurrentDef->pRestriction && pCurrentDef->pRestriction->pRequires)
			{
				StructDestroySafe(parse_Expression,&pGenDef->pRestriction->pRequires);
				pGenDef->pRestriction->pRequires = StructClone(parse_Expression,pCurrentDef->pRestriction->pRequires);
			}

			for(iCostume=0;iCostume<eaSize(&pCurrentDef->ppCostumes);iCostume++)
			{
				ItemCostume *pCurrentCostume = pCurrentDef->ppCostumes[iCostume];
				ItemCostume *pGenCostume = pGenDef->ppCostumes[iCostume];

				StructDestroySafe(parse_Expression,&pGenCostume->pExprRequires);
				pGenCostume->pExprRequires = StructClone(parse_Expression,pCurrentCostume->pExprRequires);
			}

			//Do a compare on the restrict bags, because sometimes they can be backwards for some reason
			if(ea32Size(&pCurrentDef->peRestrictBagIDs)>0)
			{
				int x;

				for(x=eaiSize(&pCurrentDef->peRestrictBagIDs)-1;x>=0;x--)
				{
					if(eaiFind(&pGenDef->peRestrictBagIDs,pCurrentDef->peRestrictBagIDs[x])==-1)
						break;
				}

				if(x==-1)
				{
					eaiDestroy(&pGenDef->peRestrictBagIDs);
					eaiCopy(&pGenDef->peRestrictBagIDs,&pCurrentDef->peRestrictBagIDs);
				}
			}

			diff = StructMakeDiff(parse_ItemDef,pGenDef,pCurrentDef,0,TOK_USEROPTIONBIT_1,0,0);
			
			if(eaSize(&diff->ppOps) > 1)
			{
				if(StructCompare(parse_ItemDef,pGenDef,pCurrentDef,0,0,TOK_USEROPTIONBIT_1))
					printf("True Conflict\n");
				iConflicts++;
				printf("ItemDef %s Has a Conflict\n",ppItemDefs[n]->pchName);
				continue;
			}

			pFixup = ItemGenFixup_CreateMsgKeyFixup(GET_REF(pCurrentDef->displayNameMsg.hMessage),pGenDef->displayNameMsg.pEditorCopy);
			if(pFixup)
				eaPush(&ppFixups,pFixup);

			pFixup = ItemGenFixup_CreateMsgKeyFixup(GET_REF(pCurrentDef->descriptionMsg.hMessage),pGenDef->descriptionMsg.pEditorCopy);
			if(pFixup)
				eaPush(&ppFixups,pFixup);

			pFixup = ItemGenFixup_CreateMsgKeyFixup(GET_REF(pCurrentDef->displayNameMsgUnidentified.hMessage),pGenDef->displayNameMsgUnidentified.pEditorCopy);
			if(pFixup)
				eaPush(&ppFixups,pFixup);

			pFixup = ItemGenFixup_CreateMsgKeyFixup(GET_REF(pCurrentDef->descriptionMsgUnidentified.hMessage),pGenDef->descriptionMsgUnidentified.pEditorCopy);
			if(pFixup)
				eaPush(&ppFixups,pFixup);

			pFixup = ItemGenFixup_CreateMsgKeyFixup(GET_REF(pCurrentDef->calloutMsg.hMessage),pGenDef->calloutMsg.pEditorCopy);
			if(pFixup)
				eaPush(&ppFixups,pFixup);

			pFixup = ItemGenFixup_CreateMsgKeyFixup(GET_REF(pCurrentDef->descShortMsg.hMessage),pGenDef->descShortMsg.pEditorCopy);
			if(pFixup)
				eaPush(&ppFixups,pFixup);

			pFixup = ItemGenFixup_CreateMsgKeyFixup(GET_REF(pCurrentDef->msgAutoDesc.hMessage),pGenDef->msgAutoDesc.pEditorCopy);
			if(pFixup)
				eaPush(&ppFixups,pFixup);

			StructDestroy(parse_StructDiff,diff);

			iTotalSafe++;
		}

		for(n=0;n<eaSize(&ppItemPowerDefs);n++)
		{
			ItemPowerDef *pGenDef = ppItemPowerDefs[n];
			ItemPowerDef *pCurrentDef = (ItemPowerDef*)RefSystem_ReferentFromString(g_hItemPowerDict, ppItemPowerDefs[n]->pchName);

			if(!pCurrentDef)
			{
				iMissing++;
				printf("ItemPower %s Generated but not found\n", ppItemPowerDefs[n]->pchName);
				continue;
			}

			if(StructCompare(parse_ItemPowerDef,pGenDef,pCurrentDef,0,0,TOK_FILENAME_X) != 0)
			{
				iConflicts++;
				printf("ItemPower %s Has a Conflict\n", ppItemPowerDefs[n]->pchName);
				continue;
			}
		}

		for(n=0;n<eaSize(&ppRewardTables);n++)
		{
			RewardTable *pGenDef = ppRewardTables[n];
			RewardTable *pCurrentDef = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,ppRewardTables[n]->pchName);

			if(!pCurrentDef)
			{
				iMissing++;
				printf("Reward Table %s Generated but not found\n",ppRewardTables[n]->pchName);
				continue;
			}

			if(StructCompare(parse_RewardTable,pGenDef,pCurrentDef,0,0,TOK_FILENAME_X) != 0)
			{
				iConflicts++;
				printf("Reward Table %s Has a Conflict\n",ppRewardTables[n]->pchName);
				continue;
			}
		}

		if(iConflicts==0 && iMissing==0 && eaSize(&ppItemDefs) > 0)
		{
			int x;
			ResourceActionList tempList = { 0 };
			ItemGenItemDefsToSave sItemDefs = { 0 };
			char *pcheStr = NULL;

			estrCreate(&pcheStr);
			
			printf("Saving Item Defs...\n");

			printf("1");
			for(x=0;x<eaSize(&ppItemDefs);x++)
			{
				//resAddRequestLockResource( &tempList, g_hItemDict, ppItemDefs[x]->pchName, ppItemDefs[x] );
				//resAddRequestSaveResource( &tempList, g_hItemDict, ppItemDefs[x]->pchName, ppItemDefs[x] );
				langApplyEditorCopy(parse_ItemDef, ppItemDefs[x], NULL, true, false); 
				eaPush(&sItemDefs.ppItemDefs,ppItemDefs[x]);
				
			}

			printf("2");
			for(x=0;x<eaSize(&ppOldItemDefs);x++)
			{
				estrPrintf(&pcheStr,"%s%s",ppOldItemDefs[x]->pchFileName,".ms");
				fileForceRemove(ppOldItemDefs[x]->pchFileName);
				fileForceRemove(pcheStr);
				estrClear(&pcheStr);
			}

			printf("3");
			estrDestroy(&pcheStr);

			printf("4");
			printf(" - %d",i);
			printf("%s %d",ppItemDefs[0]->pchFileName,eaSize(&sItemDefs.ppItemDefs));
			ParserWriteTextFileEx(ppItemDefs[0]->pchFileName,parse_ItemGenItemDefsToSave,&sItemDefs,1,0,0);

			printf("5\n");
			gslMsgFixupApplyTranslationFixups(&ppFixups,false); 
		}

		printf("1");
		eaDestroyStruct(&ppItemDefs,parse_ItemDef);
		printf("2");
		eaDestroyStruct(&ppRewardTables,parse_RewardTable);
		printf("3");
		eaDestroyStruct(&ppItemPowerDefs,parse_ItemPowerDef);
		printf("4");
		eaDestroyStruct(&ppFixups,parse_MsgKeyFixup);
		printf("5");
		eaClear(&ppOldItemDefs);

		ppItemPowerDefs = NULL;
		ppRewardTables = NULL;
		ppItemPowerDefs = NULL;

		printf("%s has %d conflicts\n",ppItemGens[i]->pchDataName,iConflicts);
		iTotalConflicts += iConflicts;
		iTotalMissing += iMissing;
	}

	printf("Total Conflicts: %d\n",iTotalConflicts);
	printf("Total Missing: %d\n",iTotalMissing);
	printf("Total Safe: %d\n",iTotalSafe);

	for(i=0;i<eaSize(&ppchMissingDefs);i++)
	{
		printf("%s\n",ppchMissingDefs[i]);
	}

	printf("Done");
}

static void ItemGen_GetAllDefsFromFilename(const char *pchFileName, ItemDef ***pppItemDefsOut)
{
	DictionaryEArrayStruct *deas;
	int i;

	deas = resDictGetEArrayStruct("ItemDef");

	for(i = eaSize(&deas->ppReferents)-1; i >= 0; --i)
	{
		ItemDef *pItemDef = (ItemDef*)deas->ppReferents[i];

		if(pItemDef->pchFileName == pchFileName)
		{
			eaPush(pppItemDefsOut,pItemDef);
		}
	}
}

static bool ItemGen_RemoveMessageReferants(DisplayMessage *pDisplayMessage, ItemDef *pItemDef, const char *pchUnused, void *pUnused)
{
	if (GET_REF(pDisplayMessage->hMessage))
		RefSystem_RemoveReferent(GET_REF(pDisplayMessage->hMessage), true);
	return true;
}

static void ItemGen_FillIconData(ItemGenData** eaData, ItemGenTextureNames *pTexNames)
{
	S32 i;
	for (i = 0; i < eaSize(&eaData) && eaSize(&pTexNames->eapchTextureNames); i++)
	{
		ItemGenData* pItemGen = eaData[i];
		if (pItemGen->bGenerateSpeciesIcons)
		{
			S32 iIndex = 0;
			char pchIconName[MAX_PATH];
			const char* pchPrefix = pItemGen->pchIconPrefix;
			const char* pchSpeciesName = REF_STRING_FROM_HANDLE(pItemGen->hSpecies);

			while(true) 
			{
				sprintf(pchIconName, "%s_%s_%02d.wtex", pchPrefix, pchSpeciesName, iIndex+1);
				if (eaFindString(&pTexNames->eapchTextureNames, pchIconName) >= 0)
					++iIndex;
				else
					break;
			}

			pItemGen->iIconCount = iIndex;
		}
	}
}

static void ItemGen_GenerateDefs(ItemGenData **ppItemGens,
								 ItemGenTextureNames *pTexNames,
								 bool bSaveItems, 
								 bool bSavedItemPowers, 
								 bool bSaveRewards)
{
	ItemDef **ppItemDefs = NULL;
	ItemPowerDef **ppItemPowerDefs = NULL;
	RewardTable **ppRewardTables = NULL;
	const char **ppchFileNames = NULL;
	const char **ppchItemFileNames = NULL;
	int i;
	int iItemCount = 0;
	int iItemPowerCount = 0;

	if (!eaSize(&ppItemGens))
		return;

	resSetDictionaryEditMode(g_hItemDict, true);
	resSetDictionaryEditMode(g_hItemGenDict, true);
	resSetDictionaryEditMode(g_hItemPowerDict, true);
	resSetDictionaryEditMode(g_hRewardTableDict, true);
	resSetDictionaryEditMode(gMessageDict, true);

	ItemGen_FillIconData(ppItemGens, pTexNames);

	for(i=0;i<eaSize(&ppItemGens);i++)
	{
		ItemGen_GenerateSingleDef(ppItemGens[i],&ppItemDefs,&ppItemPowerDefs,&ppRewardTables);
		printf("%s Generated %d ItemDefs\n", ppItemGens[i]->pchDataName, eaSize(&ppItemDefs)-iItemCount);
		printf("%s Generated %d ItemPowerDefs\n", ppItemGens[i]->pchDataName, eaSize(&ppItemPowerDefs)-iItemPowerCount);
	}

	iItemCount = eaSize(&ppItemDefs);
	iItemPowerCount = eaSize(&ppItemPowerDefs);

	if(eaSize(&ppItemGens)>1)
	{
		printf("-------------------------------------\n");
		printf("Item Gen Totals: (%d Total Item Gens)\n",eaSize(&ppItemGens));
		printf("ItemDefs Created: %d\n",eaSize(&ppItemDefs));
		printf("ItemPowerDefs Created: %d\n",eaSize(&ppItemPowerDefs));
	}
	printf("RewardTables Created or Modified: %d\n",eaSize(&ppRewardTables));

	// Need this for things like fixing up rewards that point to this item.
	sharedMemoryEnableEditorMode();

	// Create the list of files to checkout
	if(bSavedItemPowers)
	{
		for(i=0;i<eaSize(&ppItemPowerDefs);i++)
		{
			eaPushUnique(&ppchFileNames, ppItemPowerDefs[i]->pchFileName);
		}
	}
	if(bSaveItems)
	{
		for(i=0;i<eaSize(&ppItemDefs);i++)
		{
			eaPushUnique(&ppchFileNames, ppItemDefs[i]->pchFileName);
			eaPushUnique(&ppchItemFileNames, ppItemDefs[i]->pchFileName);
		}
	}
	if(bSaveRewards)
	{
		for(i=0;i<eaSize(&ppRewardTables);i++)
		{
			eaPushUnique(&ppchFileNames, ppRewardTables[i]->pchFileName);
		}
	}

	// Checkout all files
	gimmeDLLDoOperations(ppchFileNames, GIMME_CHECKOUT, GIMME_QUIET|GIMME_QUIET_LARGE_CHECKOUT);
	eaDestroy(&ppchFileNames);

	// Save files
	if(bSavedItemPowers)
	{
		printf("Saving Item Power Defs...\n");
		for(i=0;i<eaSize(&ppItemPowerDefs);i++)
		{
			ParserWriteTextFileFromSingleDictionaryStruct(ppItemPowerDefs[i]->pchFileName,g_hItemPowerDict,ppItemPowerDefs[i],0,0);
			printf("Saved Item Power Progress %d of %d\n",i,eaSize(&ppItemPowerDefs));
		}
	}
	else
	{
		printf("Skipping Saving Item Power Defs\n");
	}
	if(bSaveItems)
	{
		ResourceActionList tempList = { 0 };
		ItemGenItemDefsToSave sItemDefs = { 0 };
		ItemDef **ppFileNameDefs = NULL;
		
		printf("Saving Item Defs...\n");	


		for(i=0;i<eaSize(&ppItemDefs);i++)
		{
			ItemDef *pCurrentDef = item_DefFromName(ppItemDefs[i]->pchName);
			int x;

			if (pCurrentDef)
			{
				ParserScanForSubstruct(parse_ItemDef, pCurrentDef, parse_DisplayMessage, 0, 0, ItemGen_RemoveMessageReferants, NULL);

				if(pCurrentDef->pchFileName != ppItemDefs[i]->pchFileName)
				{
					char *pcheStr = NULL;

					gimmeDLLDoOperation(pCurrentDef->pchFileName, GIMME_CHECKOUT, GIMME_QUIET);

					estrCreate(&pcheStr);
					//Delete this file
					estrPrintf(&pcheStr,"%s%s",pCurrentDef->pchFileName,".ms");
					fileForceRemove(pCurrentDef->pchFileName);
					fileForceRemove(pcheStr);
					estrDestroy(&pcheStr);
				}
			}
			ItemGen_GetAllDefsFromFilename(ppItemDefs[i]->pchFileName,&ppFileNameDefs);

			for(x=eaSize(&ppFileNameDefs)-1;x>=0;x--)
			{
				if(stricmp(ppFileNameDefs[x]->pchName,ppItemDefs[i]->pchName) == 0)
					eaRemove(&ppFileNameDefs,x);
			}

			eaPush(&sItemDefs.ppItemDefs,ppItemDefs[i]);
		}

		if (ppFileNameDefs)
			eaDestroy(&ppFileNameDefs);

		for(i=0;i<eaSize(&ppchItemFileNames);i++)
		{
			ItemGenItemDefsToSave toSave;
			ItemDef **ppDefsToSave = NULL;
			int x;

			for(x=0;x<eaSize(&ppItemDefs);x++)
			{
				if(ppItemDefs[x]->pchFileName == ppchItemFileNames[i])
				{
					eaPush(&ppDefsToSave,ppItemDefs[x]);
				}
			}
			toSave.ppItemDefs = ppItemDefs;
			toSave.pchFileName = ppItemDefs[0]->pchFileName;
			langApplyEditorCopy(parse_ItemGenItemDefsToSave, &toSave, NULL, true, false);
			ParserWriteTextFileEx(ppchItemFileNames[i],parse_ItemGenItemDefsToSave,&ppDefsToSave,1,0,0);
			eaDestroy(&ppDefsToSave);

			printf("Saved Item Progress %d of %d\n",i,eaSize(&ppchItemFileNames));
		}
		
	}
	else
	{
		printf("Skipping Saving Item Defs\n");
	}
	if(bSaveRewards)
	{
		printf("Saving Reward Tables...\n");
		for(i=0;i<eaSize(&ppRewardTables);i++)
		{
			ParserWriteTextFileFromSingleDictionaryStruct(ppRewardTables[i]->pchFileName,g_hRewardTableDict,ppRewardTables[i],0,0);
			printf("Saved Reward Table Progress %d of %d\n",i,eaSize(&ppRewardTables));
		}
	}
	else
	{
		printf("Skipping Saving Reward Tables\n");
	}
	eaDestroyStruct(&ppItemPowerDefs,parse_ItemPowerDef);
	eaDestroyStruct(&ppItemDefs,parse_ItemDef);
	eaDestroyStruct(&ppRewardTables,parse_RewardTable);
	printf("Saving done!");
}

AUTO_COMMAND;
void ItemGen_RunCompareMethodTest(Entity *pEnt)
{
	ItemGenData **ppItemGens = NULL;
	FOR_EACH_IN_REFDICT(g_hItemGenDict,ItemGenData,p);
	{
		eaPush(&ppItemGens,p);
	}
	FOR_EACH_END;


	ItemGen_RunCompareMethod(ppItemGens,NULL);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_PRIVATE;
void ItemGen_GenerateDef(Entity* pEnt, ACMD_NAMELIST("ItemGenData", REFDICTIONARY) const char *pchItemGenDef, ItemGenTextureNames *pTexNames)
{
	ItemGenData *pItemGen = RefSystem_ReferentFromString(g_hItemGenDict,pchItemGenDef);
	ItemGenData **ppItemGens = NULL;

	if(!pItemGen)
	{
		Errorf("Invalid Item Gen specified (%s)",pchItemGenDef);
		return;
	}

	eaPush(&ppItemGens,pItemGen);

	ItemGen_GenerateDefs(ppItemGens,pTexNames,true,true,true);

	eaDestroy(&ppItemGens);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_PRIVATE;
void ItemGen_GenerateAllDefs(Entity* pEnt, ItemGenTextureNames *pTexNames)
{
	ItemGenData **ppItemGens = NULL;
    FOR_EACH_IN_REFDICT(g_hItemGenDict,ItemGenData,p);
    {
		eaPush(&ppItemGens,p);
    }
    FOR_EACH_END;

	ItemGen_GenerateDefs(ppItemGens,pTexNames,true,true,true);

	eaDestroy(&ppItemGens);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_PRIVATE;
void ItemGen_GenerateByScope(Entity *pEnt, const char *pchScope, ItemGenTextureNames *pTexNames)
{
	ItemGenData **ppItemGens = NULL;

	FOR_EACH_IN_REFDICT(g_hItemGenDict,ItemGenData,p);
	{
		if(strStartsWith(p->pchScope,pchScope))
		{
			eaPush(&ppItemGens,p);
		}
	}
	FOR_EACH_END;

	ItemGen_GenerateDefs(ppItemGens,pTexNames,true,true,true);

	eaDestroy(&ppItemGens);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_PRIVATE;
void ItemGen_GenerateAllDefsWithPrefix(Entity* pEnt, const char* pchPrefix, ItemGenTextureNames *pTexNames)
{
	ItemGenData **ppItemGens = NULL;
    FOR_EACH_IN_REFDICT(g_hItemGenDict,ItemGenData,p);
    {
		if (strStartsWith(p->pchDataName, pchPrefix))
		{
			eaPush(&ppItemGens,p);
		}
    }
    FOR_EACH_END;

	ItemGen_GenerateDefs(ppItemGens,pTexNames,true,true,true);

	eaDestroy(&ppItemGens);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_PRIVATE;
void ItemGen_GenerateDefsMatchingWildcardString(Entity* pEnt, const char* pchWildcardString, ItemGenTextureNames *pTexNames)
{
	ItemGenData **ppItemGens = NULL;
    FOR_EACH_IN_REFDICT(g_hItemGenDict,ItemGenData,p);
    {
		if (isWildcardMatch(pchWildcardString, p->pchDataName, false, true))
		{
			eaPush(&ppItemGens,p);
		}
    }
    FOR_EACH_END;

	ItemGen_GenerateDefs(ppItemGens,pTexNames,true,true,true);

	eaDestroy(&ppItemGens);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_PRIVATE;
void ItemGen_GenerateDefsFromFile(Entity* pEnt, const char* pchFilePath, ItemGenTextureNames *pTexNames)
{
	ItemGenRefs ItemGenRefData = {0};
	ItemGenData **ppItemGens = NULL;
	S32 i;

	ParserLoadFiles(NULL, pchFilePath, NULL, 0, parse_ItemGenRefs, &ItemGenRefData);

	for (i = 0; i < eaSize(&ItemGenRefData.eaRefs); i++)
	{
		ItemGenRef* pItemGenRef = ItemGenRefData.eaRefs[i];
		ItemGenData* pItemGen = GET_REF(pItemGenRef->hItemGenData);
		if (pItemGen)
		{
			eaPush(&ppItemGens, pItemGen);
		}
		else
		{
			Errorf("ItemGen_GenerateDefsFromFile: Couldn't find ItemGenData '%s'", 
				REF_STRING_FROM_HANDLE(pItemGenRef->hItemGenData));
		}
	}

	ItemGen_GenerateDefs(ppItemGens,pTexNames,true,true,true);

	eaDestroy(&ppItemGens);
	StructDeInit(parse_ItemGenRefs, &ItemGenRefData);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_PRIVATE;
void ItemGen_GenerateRewardTables(Entity* pEnt, ACMD_NAMELIST("ItemGenData", REFDICTIONARY) const char *pchItemGenDef, ItemGenTextureNames *pTexNames)
{
	ItemGenData *pItemGen = RefSystem_ReferentFromString(g_hItemGenDict,pchItemGenDef);
	ItemGenData **ppItemGens = NULL;

	if(!pItemGen)
	{
		Errorf("Invalid Item Gen specified (%s)",pchItemGenDef);
		return;
	}

	eaPush(&ppItemGens,pItemGen);

	ItemGen_GenerateDefs(ppItemGens,pTexNames,false,false,true);

	eaDestroy(&ppItemGens);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_PRIVATE;
void ItemGen_GenerateAllRewardTables(Entity* pEnt, ItemGenTextureNames *pTexNames)
{
	ItemGenData **ppItemGens = NULL;
	FOR_EACH_IN_REFDICT(g_hItemGenDict,ItemGenData,p);
	{
		eaPush(&ppItemGens,p);
	}
	FOR_EACH_END;

	ItemGen_GenerateDefs(ppItemGens,pTexNames,false,false,true);

	eaDestroy(&ppItemGens);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_PRIVATE;
void ItemGen_GenerateDefNoSave(Entity* pEnt, ACMD_NAMELIST("ItemGenData", REFDICTIONARY) const char *pchItemGen, ItemGenTextureNames *pTexNames)
{
	ItemGenData *pItemGen = RefSystem_ReferentFromString(g_hItemGenDict,pchItemGen);
	ItemGenData **ppItemGens = NULL;

	if(pItemGen)
	{
		eaPush(&ppItemGens,pItemGen);
		ItemGen_GenerateDefs(ppItemGens,pTexNames,false,false,false);
		eaDestroy(&ppItemGens);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_PRIVATE;
void ItemGen_GenerateAllNoSave(Entity* pEnt, ItemGenTextureNames *pTexNames)
{
	ItemGenData **ppItemGens = NULL;
	FOR_EACH_IN_REFDICT(g_hItemGenDict,ItemGenData,p);
	{
		eaPush(&ppItemGens,p);
	}
	FOR_EACH_END;

	ItemGen_GenerateDefs(ppItemGens,pTexNames,false,false,false);

	eaDestroy(&ppItemGens);
}

static void ItemGen_SaveMasterRarityTables(RewardTable** ppRewardTables)
{
	S32 i;
	printf("Saving Master Rarity Reward Tables...\n");

	for (i = 0; i < eaSize(&ppRewardTables); i++)
	{
		if(!resGetLockOwner(g_hRewardTableDict, ppRewardTables[i]->pchName))
			resRequestLockResource(g_hRewardTableDict,ppRewardTables[i]->pchName,ppRewardTables[i]);

		ParserWriteTextFileFromSingleDictionaryStruct(ppRewardTables[i]->pchFileName,g_hRewardTableDict,ppRewardTables[i],0,0);
	}
}

static void ItemGen_GenerateMasterRarityTable(ItemGenMasterRarityTable* pMasterTableData, RewardTable*** peaTables)
{
	S32 i, j;
	char* estrFileName = NULL;
	RewardTable* pMasterTable = StructCreate(parse_RewardTable);

	pMasterTable->pchName = StructAllocString(pMasterTableData->pchName);
	pMasterTable->pchScope = StructAllocString("ItemGen/MasterRarityTables");

	estrStackCreate(&estrFileName);
	estrPrintf(&estrFileName, "Defs/Rewards/Itemgen/MasterRarityTables/%s.rewards", pMasterTable->pchName);
	pMasterTable->pchFileName = StructAllocString(estrFileName);
	estrDestroy(&estrFileName);

	pMasterTable->Algorithm = kRewardAlgorithm_GiveAll;
	eaPush(peaTables, pMasterTable);

	for (i = eaSize(&pMasterTableData->eaEntries)-1; i >= 0; i--)
	{
		ItemGenRarityEntry* pEntryData = pMasterTableData->eaEntries[i];
		RewardTable* pEntryTable = NULL;

		if (!eaiSize(&pEntryData->peRarities))
			continue;

		if (eaiSize(&pEntryData->peRarities) > 1)
		{
			ItemGenRewardCategory eRewardCategory = pMasterTableData->eRewardCategory;
			const char* pchCategoryName = StaticDefineIntRevLookup(ItemGenRewardCategoryEnum,eRewardCategory);
			char* estrTable = NULL;
			ItemGenRarity* peRarities = NULL;
			eaiCopy(&peRarities, &pEntryData->peRarities);
			eaiQSort(peRarities, intCmp);

			estrPrintf(&estrTable, "%s", pchCategoryName);

			for (j = 0; j < eaiSize(&peRarities); j++)
			{
				const char* pchRarityName = StaticDefineIntRevLookup(ItemGenRarityEnum, peRarities[j]);
				if (pchRarityName)
				{
					estrConcatf(&estrTable, "_%s", pchRarityName);
				}
			}

			pEntryTable = StructCreate(parse_RewardTable);
			pEntryTable->pchName = StructAllocString(estrTable);
			pEntryTable->pchScope = StructAllocString("ItemGen/MasterRarityTables");
				
			estrStackCreate(&estrFileName);
			estrPrintf(&estrFileName, "Defs/Rewards/Itemgen/MasterRarityTables/%s.rewards", estrTable);
			pEntryTable->pchFileName = StructAllocString(estrFileName);
			estrDestroy(&estrFileName);

			pEntryTable->NumChoices = 1;
			pEntryTable->NumPicks = 1;

			for (j = 0; j < eaiSize(&peRarities); j++)
			{
				ItemGenRarity eRarity = peRarities[j];
				RewardTable* pRarityTable;
				pRarityTable = ItemGen_GetRewardTableByCategoryAndRarity(pMasterTableData->eRewardCategory, eRarity);
				if (pRarityTable)
				{
					RewardEntry* pEntry = StructCreate(parse_RewardEntry);
					pEntry->ChoiceType = kRewardChoiceType_Choice;
					pEntry->Type = kRewardType_RewardTable;
					pEntry->fWeight = ItemGen_GetWeightFromRarityType(eRarity); 
					SET_HANDLE_FROM_REFERENT(g_hRewardTableDict, pRarityTable, pEntry->hRewardTable);
					eaPush(&pEntryTable->ppRewardEntry,pEntry);
				}
			}
			eaPush(peaTables, pEntryTable);

			eaiDestroy(&peRarities);
			estrDestroy(&estrTable);
		}
		else
		{
			ItemGenRarity eRarity = eaiGet(&pEntryData->peRarities, 0);
			pEntryTable = ItemGen_GetRewardTableByCategoryAndRarity(pMasterTableData->eRewardCategory, eRarity);
		}

		if (pEntryTable)
		{
			RewardEntry* pEntry = StructCreate(parse_RewardEntry);
			pEntry->ChoiceType = kRewardChoiceType_Choice;
			pEntry->Type = kRewardType_RewardTable;
			pEntry->Count = pEntryData->iNumChoices;
			SET_HANDLE_FROM_STRING(g_hRewardTableDict, pEntryTable->pchName, pEntry->hRewardTable);
			eaPush(&pMasterTable->ppRewardEntry,pEntry);
		}
	}
}

AUTO_COMMAND;
void ItemGen_GenerateMasterRarityTables(void)
{
	RewardTable** ppRewardTables = NULL;
	S32 i;
	for (i = 0; i < eaSize(&g_ItemGenMasterRarityTableSettings.eaMasterTables); i++)
	{
		ItemGen_GenerateMasterRarityTable(g_ItemGenMasterRarityTableSettings.eaMasterTables[i], &ppRewardTables);
	}
	ItemGen_SaveMasterRarityTables(ppRewardTables);
	eaDestroy(&ppRewardTables);
}

static ItemGenMasterRarityTable* ItemGen_FindMasterRarityTableByName(const char* pchName)
{
	S32 i;
	for (i = eaSize(&g_ItemGenMasterRarityTableSettings.eaMasterTables)-1; i >= 0; i--)
	{
		ItemGenMasterRarityTable* pMasterTableData = g_ItemGenMasterRarityTableSettings.eaMasterTables[i];
		if (stricmp(pMasterTableData->pchName, pchName)==0)
		{
			return pMasterTableData;
		}
	}
	return NULL;
}

AUTO_COMMAND;
void ItemGen_GenerateMasterRarityTableByName(const char* pchName)
{
	ItemGenMasterRarityTable* pMasterTableData = ItemGen_FindMasterRarityTableByName(pchName);
	if (pMasterTableData)
	{
		RewardTable** ppRewardTables = NULL;
		ItemGen_GenerateMasterRarityTable(pMasterTableData, &ppRewardTables);
		ItemGen_SaveMasterRarityTables(ppRewardTables);
		eaDestroy(&ppRewardTables);
	}
	else
	{
		Errorf("Couldn't find MasterRarityTable data by name '%s'", pchName);
	}
}

AUTO_COMMAND;
void ItemGen_DumpRewardCategoryListToFile(const char* pchFile)
{
	FILE* pFile = fopen(pchFile, "w");
	const char** ppchCategories = NULL;

	if (!pFile) {
		Alertf("Could not open '%s' for writing.", pchFile);
		return;
	}

    FOR_EACH_IN_REFDICT(g_hItemGenDict,ItemGenData,p);
    {
		if (p->ppchRewardCategories_Obsolete)
		{
			S32 i;
			for (i = eaSize(&p->ppchRewardCategories_Obsolete)-1; i >= 0; i--)
			{
				const char* pchCategory = p->ppchRewardCategories_Obsolete[i];
				if (eaFind(&ppchCategories, pchCategory) < 0)
				{
					fprintf(pFile, "RewardCategory %s\n", pchCategory);
					eaPush(&ppchCategories, pchCategory);
				}
			}
		}
    }
	FOR_EACH_END;

	eaDestroy(&ppchCategories);
	fclose(pFile);
}

#endif