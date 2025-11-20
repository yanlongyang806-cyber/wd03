/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "itemGenCommon.h"

#include "entCritter.h"
#include "error.h"
#include "file.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "ResourceManager.h"
#include "sharedmemory.h"
#include "StringCache.h"
#include "TimedCallback.h"
#include "GraphicsLib.h"

#include "structDefines.h"
#include "textparser.h"

#ifdef GAMECLIENT
#include "GraphicsLib.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#endif

#include "AutoGen/itemGenCommon_h_ast.h"

DictionaryHandle g_hItemGenDict;
ItemGenRaritySettings g_ItemGenRaritySettings = {0};
ItemGenMasterRarityTableSettings g_ItemGenMasterRarityTableSettings = {0};
DefineContext *g_pItemGenRewardCategory = NULL;
DefineContext *g_pItemRarity = NULL;
int g_pItemRarityCount = 0;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static void ItemGenRewardCategoryLoadInternal(const char *pchPath, S32 iWhen)
{
	static ItemGenRewardCategoryNames s_RewardCategoryNames = {0};
	S32 i;

	StructReset(parse_ItemGenRewardCategoryNames, &s_RewardCategoryNames);

	if (g_pItemGenRewardCategory)
	{
		DefineDestroy(g_pItemGenRewardCategory);
	}
	g_pItemGenRewardCategory = DefineCreate();

	loadstart_printf("Loading ItemGen RewardCategories... ");

	ParserLoadFiles(NULL, 
		"defs/items/itemgen/ItemGenRewardCategories.def", 
		"ItemGenRewardCategories.bin", 
		PARSER_OPTIONALFLAG, 
		parse_ItemGenRewardCategoryNames, 
		&s_RewardCategoryNames);

	for (i = 0; i < eaSize(&s_RewardCategoryNames.ppchNames); i++)
	{
		DefineAddInt(g_pItemGenRewardCategory, s_RewardCategoryNames.ppchNames[i], i+1);
	}

	loadend_printf(" done (%d RewardCategories).", eaSize(&s_RewardCategoryNames.ppchNames));
}

static void ItemGenRarityLoadInternal(const char *pchPath, S32 iWhen)
{
	S32 i;
	StructReset(parse_ItemGenRaritySettings, &g_ItemGenRaritySettings);

	if (g_pItemRarity)
	{
		DefineDestroy(g_pItemRarity);
	}
	g_pItemRarity = DefineCreate();

	loadstart_printf("Loading ItemGen Rarities... ");

	ParserLoadFiles(NULL, 
		"defs/items/itemgen/ItemGen_rarity.def", 
		"ItemGen_rarity.bin", 
		PARSER_OPTIONALFLAG, 
		parse_ItemGenRaritySettings, 
		&g_ItemGenRaritySettings);

	// I use i+1 for the default "uncategorized" index, which is always present
	for (i = 0; i < eaSize(&g_ItemGenRaritySettings.eaData); i++)
	{
		ItemGenRarity eRarity = ItemGenRarity_CodeMax+i+1;
		g_ItemGenRaritySettings.eaData[i]->eRarityType = eRarity;
		DefineAddInt(g_pItemRarity, g_ItemGenRaritySettings.eaData[i]->pchName, eRarity);
	}

	loadend_printf(" done (%d ItemRaritys).", i);

	g_pItemRarityCount = ItemGenRarity_CodeMax+i+1;
}

static void ItemGenMasterRarityTablesLoadInternal(const char *pchPath, S32 iWhen)
{
	StructReset(parse_ItemGenMasterRarityTableSettings, &g_ItemGenMasterRarityTableSettings);

	loadstart_printf("Loading ItemGen Master Rarity Tables... ");

	ParserLoadFiles(NULL, 
		"defs/items/itemgen/ItemGenMasterRarityTables.def", 
		"ItemGenMasterRarityTables.bin", 
		PARSER_OPTIONALFLAG, 
		parse_ItemGenMasterRarityTableSettings,
		&g_ItemGenMasterRarityTableSettings);

	loadend_printf(" done (%d ItemGenMasterRarityTables).", eaSize(&g_ItemGenMasterRarityTableSettings.eaMasterTables));
}

void ItemGenRewardsLoad(void)
{
	ItemGenRewardCategoryLoadInternal(NULL, 0);
	ItemGenRarityLoadInternal(NULL, 0);
	ItemGenMasterRarityTablesLoadInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/items/itemgen/ItemGenRewardCategories.def", ItemGenRewardCategoryLoadInternal);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/items/itemgen/ItemGen_rarity.def", ItemGenRarityLoadInternal);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/items/itemgen/ItemGenMasterRarityTables.def", ItemGenMasterRarityTablesLoadInternal);
}

static S32 ItemGenData_ValidateRarity(ItemGenData *pData, ItemGenRarityDef *pRarity)
{
	int i;
	S32 bSuccess = true;

	// Validate ItemSets
	for (i = 0; i < eaSize(&pRarity->ppItemSets); i++)
	{
		ItemDefRef* pRef = pRarity->ppItemSets[i];
		if (!GET_REF(pRef->hDef) && REF_STRING_FROM_HANDLE(pRef->hDef))
		{
			ErrorFilenamef(pData->pchFileName, "ItemGenRarity references non-existent ItemSet '%s'", REF_STRING_FROM_HANDLE(pRef->hDef));
			bSuccess = false;
		}
	}

	// Validate trainable nodes
	for (i = 0; i < eaSize(&pRarity->ppTrainableNodes); i++)
	{
		PTNodeDefRef* pRef = pRarity->ppTrainableNodes[i];
		if (!GET_REF(pRef->hNodeDef) && REF_STRING_FROM_HANDLE(pRef->hNodeDef))
		{
			ErrorFilenamef(pData->pchFileName, "ItemGenRarity references non-existent trainable node '%s'", REF_STRING_FROM_HANDLE(pRef->hNodeDef));
			bSuccess = false;
		}
	}

	// Validate ItemArt
	if (!GET_REF(pRarity->hArt) && REF_STRING_FROM_HANDLE(pRarity->hArt))
	{
		ErrorFilenamef(pData->pchFileName, "ItemGenRarity references non-existent ItemArt '%s'", REF_STRING_FROM_HANDLE(pRarity->hArt));
		bSuccess = false;
	}

	// Validate RewardCostume
	if (!GET_REF(pRarity->hRewardCostume) && REF_STRING_FROM_HANDLE(pRarity->hRewardCostume))
	{
		ErrorFilenamef(pData->pchFileName, "ItemGenRarity references non-existent RewardCostume '%s'", REF_STRING_FROM_HANDLE(pRarity->hRewardCostume));
		bSuccess = false;
	}

	// Validate Subtarget
	if (!GET_REF(pRarity->hSubtarget) && REF_STRING_FROM_HANDLE(pRarity->hSubtarget))
	{
		ErrorFilenamef(pData->pchFileName, "ItemGenRarity references non-existent SubTarget '%s'", REF_STRING_FROM_HANDLE(pRarity->hSubtarget));
		bSuccess = false;
	}

	// Validate ItemCostumes
	for (i = 0; i < eaSize(&pRarity->ppCostumes); i++)
	{
		ItemCostume* pItemCostume = pRarity->ppCostumes[i];
		if (!GET_REF(pItemCostume->hCostumeRef) && REF_STRING_FROM_HANDLE(pItemCostume->hCostumeRef))
		{
			ErrorFilenamef(pData->pchFileName, "ItemGenRarity references non-existent ItemCostume '%s'", REF_STRING_FROM_HANDLE(pItemCostume->hCostumeRef));
			bSuccess = false;
		}
	}

	return bSuccess;
}

static S32 ItemGenData_ValidateTier(ItemGenData *pData, ItemGenTier *pTier)
{
	int i;
	S32 bSuccess = true;

	// Validate ItemSets
	for (i = 0; i < eaSize(&pTier->ppItemSets); i++)
	{
		ItemDefRef* pRef = pTier->ppItemSets[i];
		if (!GET_REF(pRef->hDef) && REF_STRING_FROM_HANDLE(pRef->hDef))
		{
			ErrorFilenamef(pData->pchFileName, "ItemGenTier references non-existent ItemSet '%s'", REF_STRING_FROM_HANDLE(pRef->hDef));
			bSuccess = false;
		}
	}

	// Validate trainable nodes
	for (i = 0; i < eaSize(&pTier->ppTrainableNodes); i++)
	{
		PTNodeDefRef* pRef = pTier->ppTrainableNodes[i];
		if (!GET_REF(pRef->hNodeDef) && REF_STRING_FROM_HANDLE(pRef->hNodeDef))
		{
			ErrorFilenamef(pData->pchFileName, "ItemGenTier references non-existent trainable node '%s'", REF_STRING_FROM_HANDLE(pRef->hNodeDef));
			bSuccess = false;
		}
	}

	// Validate ItemArt
	if (!GET_REF(pTier->hArt) && REF_STRING_FROM_HANDLE(pTier->hArt))
	{
		ErrorFilenamef(pData->pchFileName, "ItemGenTier references non-existent ItemArt '%s'", REF_STRING_FROM_HANDLE(pTier->hArt));
		bSuccess = false;
	}

	// Validate Subtarget
	if (!GET_REF(pTier->hSubtarget) && REF_STRING_FROM_HANDLE(pTier->hSubtarget))
	{
		ErrorFilenamef(pData->pchFileName, "ItemGenTier references non-existent SubTarget '%s'", REF_STRING_FROM_HANDLE(pTier->hSubtarget));
		bSuccess = false;
	}

	// Validate ItemCostumes
	for (i = 0; i < eaSize(&pTier->ppCostumes); i++)
	{
		ItemCostume* pItemCostume = pTier->ppCostumes[i];
		if (!GET_REF(pItemCostume->hCostumeRef) && REF_STRING_FROM_HANDLE(pItemCostume->hCostumeRef))
		{
			ErrorFilenamef(pData->pchFileName, "ItemGenTier references non-existent ItemCostume '%s'", REF_STRING_FROM_HANDLE(pItemCostume->hCostumeRef));
			bSuccess = false;
		}
	}

	// Validate ItemGenRarities
	for (i = 0; i < eaSize(&pTier->ppRarities); i++)
	{
		if (!ItemGenData_ValidateRarity(pData, pTier->ppRarities[i]))
		{
			bSuccess = false;
		}
	}

	return bSuccess;
}

S32 ItemGenData_Validate(ItemGenData *pData)
{
	int i;
	S32 bSuccess = true;

	// Check for a valid display name, which can either come from the species or the DisplayMessage
	if (REF_STRING_FROM_HANDLE(pData->hSpecies))
	{
		if (!GET_REF(pData->hSpecies))
		{
			ErrorFilenamef(pData->pchFileName, "ItemGen references non-existent species '%s'", REF_STRING_FROM_HANDLE(pData->hSpecies));
			bSuccess = false;
		}
	}

	// Validate ItemSets
	for (i = 0; i < eaSize(&pData->ppItemSets); i++)
	{
		ItemDefRef* pRef = pData->ppItemSets[i];
		if (!GET_REF(pRef->hDef) && REF_STRING_FROM_HANDLE(pRef->hDef))
		{
			ErrorFilenamef(pData->pchFileName, "ItemGen references non-existent ItemSet '%s'", REF_STRING_FROM_HANDLE(pRef->hDef));
			bSuccess = false;
		}
	}

	// Validate trainable nodes
	for (i = 0; i < eaSize(&pData->ppTrainableNodes); i++)
	{
		PTNodeDefRef* pRef = pData->ppTrainableNodes[i];
		if (!GET_REF(pRef->hNodeDef) && REF_STRING_FROM_HANDLE(pRef->hNodeDef))
		{
			ErrorFilenamef(pData->pchFileName, "ItemGen references non-existent trainable node '%s'", REF_STRING_FROM_HANDLE(pRef->hNodeDef));
			bSuccess = false;
		}
	}

	// Validate ItemArt
	if (!GET_REF(pData->hArt) && REF_STRING_FROM_HANDLE(pData->hArt))
	{
		ErrorFilenamef(pData->pchFileName, "ItemGen references non-existent ItemArt '%s'", REF_STRING_FROM_HANDLE(pData->hArt));
		bSuccess = false;
	}

	// Validate RewardCostumes
	if (!GET_REF(pData->hRewardCostume) && REF_STRING_FROM_HANDLE(pData->hRewardCostume))
	{
		ErrorFilenamef(pData->pchFileName, "ItemGen references non-existent RewardCostume '%s'", REF_STRING_FROM_HANDLE(pData->hRewardCostume));
		bSuccess = false;
	}
	if (!GET_REF(pData->hNotYoursCostumeRef) && REF_STRING_FROM_HANDLE(pData->hNotYoursCostumeRef))
	{
		ErrorFilenamef(pData->pchFileName, "ItemGen references non-existent NotYoursCostumeRef '%s'", REF_STRING_FROM_HANDLE(pData->hNotYoursCostumeRef));
		bSuccess = false;
	}
	if (!GET_REF(pData->hYoursCostumeRef) && REF_STRING_FROM_HANDLE(pData->hYoursCostumeRef))
	{
		ErrorFilenamef(pData->pchFileName, "ItemGen references non-existent YoursCostumeRef '%s'", REF_STRING_FROM_HANDLE(pData->hYoursCostumeRef));
		bSuccess = false;
	}

	// Validate SlotID
	if (!GET_REF(pData->hSlotID) && REF_STRING_FROM_HANDLE(pData->hSlotID))
	{
		ErrorFilenamef(pData->pchFileName, "ItemGen references non-existent InventorySlotIDDef '%s'", REF_STRING_FROM_HANDLE(pData->hSlotID));
		bSuccess = false;
	}

	// Validate Subtarget
	if (!GET_REF(pData->hSubtarget) && REF_STRING_FROM_HANDLE(pData->hSubtarget))
	{
		ErrorFilenamef(pData->pchFileName, "ItemGen references non-existent SubTarget '%s'", REF_STRING_FROM_HANDLE(pData->hSubtarget));
		bSuccess = false;
	}

	// Validate ItemCostumes
	for (i = 0; i < eaSize(&pData->ppCostumes); i++)
	{
		ItemCostume* pItemCostume = pData->ppCostumes[i];
		if (!GET_REF(pItemCostume->hCostumeRef) && REF_STRING_FROM_HANDLE(pItemCostume->hCostumeRef))
		{
			ErrorFilenamef(pData->pchFileName, "ItemGen references non-existent ItemCostume '%s'", REF_STRING_FROM_HANDLE(pItemCostume->hCostumeRef));
			bSuccess = false;
		}
	}

	// Validate ItemGenTiers
	for (i = 0; i < eaSize(&pData->ppItemTiers); i++)
	{
		if (!ItemGenData_ValidateTier(pData, pData->ppItemTiers[i]))
		{
			bSuccess = false;
		}
	}

	return bSuccess;
}

int ItemGenPowerData_sort(const ItemGenPowerData **pdataA, const ItemGenPowerData **pdataB)
{
	if((*pdataA)->eRarity > (*pdataB)->eRarity)
		return 1;
	else if((*pdataB)->eRarity > (*pdataA)->eRarity)
		return -1;
	else if ((*pdataA)->uiCategory > (*pdataB)->uiCategory)
		return 1;
	else if ((*pdataB)->uiCategory > (*pdataA)->uiCategory)
		return -1;

	if(!(*pdataB)->pchInternalName)
		return 1;
	if(!(*pdataA)->pchInternalName)
		return -1;

	return strcmp((*pdataA)->pchInternalName,(*pdataB)->pchInternalName);
}

static S32 CompareU32(const U32 *i, const U32 *j)
{
	return (*i < *j) ? -1 : (*i > *j);
}

static void ItemGenData_SortArrays(ItemGenData *pData)
{
	int i;
	eaQSort(pData->ppPowerData,ItemGenPowerData_sort);

	for(i=0;i<eaSize(&pData->ppItemTiers);i++)
	{
		int j;

		for(j=0;j<eaSize(&pData->ppItemTiers[i]->ppRarities);j++)
		{
			ea32QSort(pData->ppItemTiers[i]->ppRarities[j]->ePowerRarityChoices,CompareU32);
		}
	}
}

static int ItemGenDataValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, void *pResource, U32 userID)
{
	ItemGenData *pData = pResource;

	switch(eType)
	{	
	xcase RESVALIDATE_FIX_FILENAME: // Called for filename check
		resFixPooledFilename(&pData->pchFileName, "defs/items/itemgen", pData->pchInternalScope, pData->pchDataName, "itemgen");
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_POST_TEXT_READING:
		ItemGenData_SortArrays(pData);
		return VALIDATE_HANDLED;
	
	xcase RESVALIDATE_CHECK_REFERENCES:
		ItemGenData_Validate(pData);
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void RegisterItemGenDict(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return;
	// Set up reference dictionaries
	g_hItemGenDict = RefSystem_RegisterSelfDefiningDictionary("ItemGenData", false, parse_ItemGenData, true, true, NULL);

	resDictManageValidation(g_hItemGenDict, ItemGenDataValidateCB);

	if(IsServer())
	{
		resDictProvideMissingResources(g_hItemGenDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hItemGenDict, NULL, ".Scope", NULL, NULL, NULL);
		}
	}
	else
	{
		resDictRequestMissingResources(g_hItemGenDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand );
	}
	resDictProvideMissingRequiresEditMode(g_hItemGenDict);

#endif
}

static void ItemGenReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading AlgoPets...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserReloadFileToDictionaryWithFlags(pchRelPath,g_hItemGenDict,PARSER_OPTIONALFLAG);

	loadend_printf(" done (%d AlgoPets)", RefSystem_GetDictionaryNumberOfReferents(g_hItemGenDict));
}


AUTO_STARTUP(ItemGen) ASTRT_DEPS(Powers, Items, ItemPowers, ItemTags, AS_Messages);
void ItemGenDataLoad(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return;

	if(IsClient())
	{
		//Do nothing
	}
	else
	{
		loadstart_printf("Loading ItemGen Data files...");

		ParserLoadFilesToDictionary("defs/items/itemgen",".ItemGen","ItemGens.bin",PARSER_OPTIONALFLAG,g_hItemGenDict);

		// Reload callbacks
		if(isDevelopmentMode())
		{
			FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "defs/items/itemgen*.ItemGen", ItemGenReload);
		}

		loadend_printf(" done (%d ItemGens).", RefSystem_GetDictionaryNumberOfReferents(g_hItemGenDict));
	}


#endif
}

F32 ItemGen_GetWeightFromRarityType(ItemGenRarity eRarityType)
{
	S32 i;
	for (i = eaSize(&g_ItemGenRaritySettings.eaData)-1; i >= 0; i--)
	{
		ItemGenRarityData* pData = g_ItemGenRaritySettings.eaData[i];
		if (pData->eRarityType == eRarityType)
		{
			return pData->fWeight;
		}
	}
	return 0.0f;
}

#include "AutoGen/itemGenCommon_h_ast.c"