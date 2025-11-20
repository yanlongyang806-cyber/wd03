/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ItemAssignments.h"
#include "allegiance.h"
#include "contact_common.h"
#include "Entity.h"
#include "error.h"
#include "file.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GameAccountDataCommon.h"
#include "GlobalTypes.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "mission_common.h"
#include "Player.h"
#include "ResourceManager.h"
#include "RewardCommon.h"
#include "StringCache.h"
#include "wlVolumes.h"
#include "inventoryCommon.h"

#ifdef GAMESERVER
#include "gslItemAssignments.h"
#endif

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/ItemAssignments_h_ast.h"
#include "AutoGen/Player_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_hItemAssignmentDict = NULL;
DefineContext* g_pItemAssignmentCategories = NULL;
DefineContext* g_pItemAssignmentWeights = NULL;
DefineContext* g_pItemAssignmentDurationScaleCategories = NULL;
DefineContext* g_pItemAssignmentOutcomeModifierTypes = NULL;
DefineContext* g_pItemAssignmentRarityCountTypes = NULL;
ItemAssignmentSettings g_ItemAssignmentSettings = {0};
bool g_bRebuildItemAssignmentTree = false;
static ItemAssignmentCategorySettingsStruct s_ItemAssignmentCategorySettings = {0};
static ItemAssignmentWeights s_ItemAssignmentWeights = {0};
static ItemAssignmentOutcomeModifierTypes s_ItemAssignmentModifierTypes = {0};
static ItemAssignmentRarityCounts s_ItemAssignmentRarityCounts = {0};

static void ItemAssignmentModifierTypes_LoadInternal(const char *pchPath, S32 iWhen)
{
	S32 i;
	StructReset(parse_ItemAssignmentOutcomeModifierTypes, &s_ItemAssignmentModifierTypes);

	if (g_pItemAssignmentOutcomeModifierTypes)
	{
		DefineDestroy(g_pItemAssignmentOutcomeModifierTypes);
	}
	g_pItemAssignmentOutcomeModifierTypes = DefineCreate();

	loadstart_printf("Item Assignment Modifier Types... ");

	ParserLoadFiles(NULL, 
		"defs/config/ItemAssignmentModifierTypes.def", 
		"ItemAssignmentModifierTypes.bin", 
		PARSER_OPTIONALFLAG, 
		parse_ItemAssignmentOutcomeModifierTypes,
		&s_ItemAssignmentModifierTypes);

	for (i = 0; i < eaSize(&s_ItemAssignmentModifierTypes.eaData); i++)
	{
		ItemAssignmentOutcomeModifierTypeData* pData = s_ItemAssignmentModifierTypes.eaData[i];
		pData->eType = i+1;
		DefineAddInt(g_pItemAssignmentOutcomeModifierTypes, pData->pchName, pData->eType);
	}

	// Do validation
	for (i = 0; i < eaSize(&s_ItemAssignmentModifierTypes.eaData); i++)
	{
		ItemAssignmentOutcomeModifierTypeData* pData = s_ItemAssignmentModifierTypes.eaData[i];
		if (pData->pExprWeightModifier &&
			!exprGenerate(pData->pExprWeightModifier, ItemAssignments_GetContext(NULL)))
		{
			Errorf("Couldn't generate weight modifier expression for modifier type %s", pData->pchName);
		}
		if (pData->pchDependentOutcome && pData->pchDependentOutcome[0])
		{
			S32 iAffectedIdx;
			for (iAffectedIdx = eaSize(&pData->ppchAffectedOutcomes)-1; iAffectedIdx >= 0; iAffectedIdx--)
			{
				if (pData->pchDependentOutcome == pData->ppchAffectedOutcomes[iAffectedIdx])
				{
					Errorf("Dependent outcome matches an affected outcome %s", pData->pchDependentOutcome);
				}
			}
		}
	}

	loadend_printf(" done (%d Modifier Types).", i);
}

AUTO_STARTUP(ItemAssignmentModifierTypes);
void ItemAssignmentModifierTypes_Load(void)
{
	ItemAssignmentModifierTypes_LoadInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ItemAssignmentModifierTypes.def", ItemAssignmentModifierTypes_LoadInternal);
}

static void ItemAssignmentWeights_LoadInternal(const char *pchPath, S32 iWhen)
{
	S32 i;
	StructReset(parse_ItemAssignmentWeights, &s_ItemAssignmentWeights);

	if (g_pItemAssignmentWeights)
	{
		DefineDestroy(g_pItemAssignmentWeights);
	}
	g_pItemAssignmentWeights = DefineCreate();

	loadstart_printf("Item Assignment Weights... ");

	ParserLoadFiles(NULL, 
		"defs/config/ItemAssignmentWeights.def", 
		"ItemAssignmentWeights.bin", 
		PARSER_OPTIONALFLAG, 
		parse_ItemAssignmentWeights,
		&s_ItemAssignmentWeights);

	for (i = 0; i < eaSize(&s_ItemAssignmentWeights.eaData); i++)
	{
		ItemAssignmentWeight* pData = s_ItemAssignmentWeights.eaData[i];
		pData->eWeightType = i+1;
		DefineAddInt(g_pItemAssignmentWeights, pData->pchName, pData->eWeightType);
	}

	loadend_printf(" done (%d Weights).", i);
}

AUTO_STARTUP(ItemAssignmentWeights) ASTRT_DEPS(AS_Messages, ItemAssignmentCategories);
void ItemAssignmentWeights_Load(void)
{
	ItemAssignmentWeights_LoadInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ItemAssignmentWeights.def", ItemAssignmentWeights_LoadInternal);
}

static bool ItemAssignmentCategory_Validate(ItemAssignmentCategorySettings* pCategory)
{
	bool bSuccess = true;

	if (IsServer() && !GET_REF(pCategory->msgDisplayName.hMessage) && REF_STRING_FROM_HANDLE(pCategory->msgDisplayName.hMessage))
	{
		Errorf("Item Assignment Category '%s' refers to non-existent display message '%s'", 
			pCategory->pchName, REF_STRING_FROM_HANDLE(pCategory->msgDisplayName.hMessage));
		bSuccess = false;
	}

	if (pCategory->pExprIsCategoryHidden && 
		!exprGenerate(pCategory->pExprIsCategoryHidden, ItemAssignments_GetContext(NULL)))
	{
		ErrorFilenamef("ItemAssignmentCategories.def", "Couldn't generate category settings IsCategoryHidden expression.");
		bSuccess = false;
	}

	return bSuccess;
}

static void ItemAssignmentCategories_LoadInternal(const char *pchPath, S32 iWhen)
{
	S32 i;
	StructReset(parse_ItemAssignmentCategorySettingsStruct, &s_ItemAssignmentCategorySettings);

	if (g_pItemAssignmentCategories)
	{
		DefineDestroy(g_pItemAssignmentCategories);
	}
	g_pItemAssignmentCategories = DefineCreate();

	loadstart_printf("Item Assignment Categories... ");

	ParserLoadFiles(NULL, 
		"defs/config/ItemAssignmentCategories.def", 
		"ItemAssignmentCategories.bin", 
		PARSER_OPTIONALFLAG, 
		parse_ItemAssignmentCategorySettingsStruct,
		&s_ItemAssignmentCategorySettings);

	for (i = 0; i < eaSize(&s_ItemAssignmentCategorySettings.eaCategories); i++)
	{
		ItemAssignmentCategorySettings* pData = s_ItemAssignmentCategorySettings.eaCategories[i];
		pData->eCategory = i+1;
		DefineAddInt(g_pItemAssignmentCategories, pData->pchName, pData->eCategory);

		ItemAssignmentCategory_Validate(pData);
	}

	loadend_printf(" done (%d Categories).", i);
}

AUTO_STARTUP(ItemAssignmentCategories) ASTRT_DEPS(AS_Messages);
void ItemAssignmentCategories_Load(void)
{
	ItemAssignmentCategories_LoadInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ItemAssignmentCategories.def", ItemAssignmentCategories_LoadInternal);
}

static void ItemAssignmentRarityCounts_LoadInternal(const char *pchPath, S32 iWhen)
{
	S32 i;
	StructReset(parse_ItemAssignmentRarityCounts, &s_ItemAssignmentRarityCounts);

	if (g_pItemAssignmentRarityCountTypes)
	{
		DefineDestroy(g_pItemAssignmentRarityCountTypes);
	}
	g_pItemAssignmentRarityCountTypes = DefineCreate();

	loadstart_printf("Item Assignment Rarity Counts... ");

	ParserLoadFiles(NULL, 
		"defs/config/ItemAssignmentRarityCounts.def", 
		"ItemAssignmentRarityCounts.bin", 
		PARSER_OPTIONALFLAG, 
		parse_ItemAssignmentRarityCounts,
		&s_ItemAssignmentRarityCounts);

	for (i = 0; i < eaSize(&s_ItemAssignmentRarityCounts.eaRarityCounts); i++)
	{
		ItemAssignmentRarityCount* pData = s_ItemAssignmentRarityCounts.eaRarityCounts[i];
		pData->eType = i+1;
		DefineAddInt(g_pItemAssignmentRarityCountTypes, pData->pchName, pData->eType);
	}

	loadend_printf(" done (%d Rarity Counts).", i);
}

AUTO_STARTUP(ItemAssignmentRarityCounts) ASTRT_DEPS(ItemAssignmentWeights);
void ItemAssignmentRarityCounts_Load(void)
{
	if (IsServer() || isDevelopmentMode())
	{
		ItemAssignmentRarityCounts_LoadInternal(NULL, 0);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ItemAssignmentRarityCounts.def", ItemAssignmentRarityCounts_LoadInternal);
	}
}

static bool ItemAssignmentSettings_ValidateDurationScaleCategory(ItemAssignmentDurationScaleCategoryData* pData)
{
	bool bSuccess = true;
	S32 i;

	for (i = 0; i < eaSize(&pData->eaDurationScales)-1; i++)
	{
		if (pData->eaDurationScales[i]->uDurationMax >= pData->eaDurationScales[i+1]->uDurationMin)
		{
			Errorf("ItemAssignmentDurationScaleCategory '%s': Overlapping duration scale found at index %d", 
				StaticDefineIntRevLookup(ItemAssignmentDurationScaleCategoryEnum, pData->eCategory), i);
			bSuccess = false;
		}
	}
	return bSuccess;
}

// Given a weight value, will return a ItemAssignmentWeight def as defined on the g_ItemAssignmentSettings
// as a weight that is used for display purposes.
ItemAssignmentWeight* ItemAssignmentSettings_GetWeightDef(F32 fWeightValue)
{
	S32 i;
	for (i = eaiSize(&g_ItemAssignmentSettings.eaiDisplayWeightCategories) - 1; i >= 0; --i)
	{
		ItemAssignmentWeight* pWeight = ItemAssignmentWeightType_GetWeightDef(g_ItemAssignmentSettings.eaiDisplayWeightCategories[i]);
		if (pWeight->fWeight == fWeightValue)
			return pWeight;
	}

	return NULL;
}


static bool ItemAssignmentSettings_Validate(void)
{
	bool bSuccess = true;
	S32 i;
	S32 iMaxMaps = (1 << ITEM_ASSIGNMENT_SEED_NUM_MAP_BITS);
	S32 iMaxVolumes = (1 << ITEM_ASSIGNMENT_SEED_NUM_VOLUME_BITS);

	if (eaSize(&g_ItemAssignmentSettings.eaMapSettings) > iMaxMaps)
	{
		Errorf("ItemAssignmentSettings: Too many map names specified. Max is %d", iMaxMaps);
		bSuccess = false;
	}
	if (eaSize(&g_ItemAssignmentSettings.ppchValidVolumes) > iMaxVolumes)
	{
		Errorf("ItemAssignmentSettings: Too many volume names specified. Max is %d", iMaxVolumes);
		bSuccess = false;
	}
	if (!g_ItemAssignmentSettings.bRequirePlayerInValidVolume && eaSize(&g_ItemAssignmentSettings.ppchValidVolumes) > 0)
	{
		Errorf("ItemAssignmentSettings: 'RequirePlayerInValidVolume' is not set, but valid volumes specified");
		bSuccess = false;
	}
	else if (g_ItemAssignmentSettings.bRequirePlayerInValidVolume && !eaSize(&g_ItemAssignmentSettings.ppchValidVolumes))
	{
		Errorf("ItemAssignmentSettings: 'RequirePlayerInValidVolume' is set, but no valid volumes specified");
		bSuccess = false;
	}

	// Validate duration scale categories
	for (i = eaSize(&g_ItemAssignmentSettings.eaDurationScaleCategories)-1; i >= 0; i--)
	{
		ItemAssignmentDurationScaleCategoryData* pData = g_ItemAssignmentSettings.eaDurationScaleCategories[i];
		if (!ItemAssignmentSettings_ValidateDurationScaleCategory(pData))
		{
			bSuccess = false;
		}
	}

	for (i = eaSize(&g_ItemAssignmentSettings.ppchDurationScaleNumerics)-1; i >= 0; i--)
	{
		const char* pchNumeric = g_ItemAssignmentSettings.ppchDurationScaleNumerics[i];
		if (!item_DefFromName(pchNumeric))
		{
			Errorf("ItemAssignmentSettings references a non-existent duration scale numeric %s", pchNumeric);
			bSuccess = false;
		}
	}

	for (i = eaSize(&g_ItemAssignmentSettings.ppchQualityScaleNumerics)-1; i >= 0; i--)
	{
		const char* pchNumeric = g_ItemAssignmentSettings.ppchQualityScaleNumerics[i];
		if (!item_DefFromName(pchNumeric))
		{
			Errorf("ItemAssignmentSettings references a non-existent quality scale numeric %s", pchNumeric);
			bSuccess = false;
		}
	}

	if(g_ItemAssignmentSettings.pStrictAssignmentSlots)
	{
		for(i = eaSize(&g_ItemAssignmentSettings.pStrictAssignmentSlots->ppUnlockExpression)-1;i>=0;i--)
		{
			ItemAssignmentSlotUnlockExpression *pData = g_ItemAssignmentSettings.pStrictAssignmentSlots->ppUnlockExpression[i];

			if (pData->pUnlockExpr &&
				!exprGenerate(pData->pUnlockExpr, ItemAssignments_GetContext(NULL)))
			{

			}

			if (pData->pCompletedExpr &&
				!exprGenerate(pData->pCompletedExpr, ItemAssignments_GetContext(NULL)))
			{

			}
		}
	}

	return bSuccess;
}

static int SortDurationScales(const ItemAssignmentDurationScale** ppA, const ItemAssignmentDurationScale** ppB)
{
	const ItemAssignmentDurationScale* pA = (*ppA);
	const ItemAssignmentDurationScale* pB = (*ppB);

	if (pA->uDurationMin != pB->uDurationMin)
	{
		return (int)pA->uDurationMin - (int)pB->uDurationMin;
	}
	return (int)pA->uDurationMax - (int)pB->uDurationMax;
}

static int SortExperience(const int* pA, const int* pB)
{
	return (int)(*pA) - (int)(*pB);
}

static int SortSlotUnlockSchedule(const ItemAssignmentSlotUnlockSchedule** ppA, const ItemAssignmentSlotUnlockSchedule** ppB)
{
	const ItemAssignmentSlotUnlockSchedule* pA = (*ppA);
	const ItemAssignmentSlotUnlockSchedule* pB = (*ppB);

	return (int)pA->iRank - (int)pB->iRank;
}

static void ItemAssignmentSettings_Fixup(void)
{
	S32 i;

	if (g_pItemAssignmentDurationScaleCategories)
	{
		DefineDestroy(g_pItemAssignmentDurationScaleCategories);
	}
	g_pItemAssignmentDurationScaleCategories = DefineCreate();

	for (i = 0; i < eaSize(&g_ItemAssignmentSettings.eaDurationScaleCategories); i++)
	{
		ItemAssignmentDurationScaleCategoryData* pData = g_ItemAssignmentSettings.eaDurationScaleCategories[i];
		pData->eCategory = i+1;
		DefineAddInt(g_pItemAssignmentDurationScaleCategories, pData->pchName, pData->eCategory);

		// Sort duration scales by minimum duration for faster validation
		eaQSort(pData->eaDurationScales, SortDurationScales);
	}

	if (g_ItemAssignmentSettings.pCategoryRankingSchedule)
	{
		eaiQSort(g_ItemAssignmentSettings.pCategoryRankingSchedule->eaiExperience, SortExperience);
	}

	if (g_ItemAssignmentSettings.pMetaRankingSchedule)
	{
		eaiQSort(g_ItemAssignmentSettings.pMetaRankingSchedule->eaiExperience, SortExperience);
	}

	if (g_ItemAssignmentSettings.pStrictAssignmentSlots)
	{
		 eaQSort(g_ItemAssignmentSettings.pStrictAssignmentSlots->eaSlotUnlockSchedule, SortSlotUnlockSchedule);
	}

	if (g_ItemAssignmentSettings.pExprDurationModifier && 
		!exprGenerate(g_ItemAssignmentSettings.pExprDurationModifier, ItemAssignments_GetContext(NULL)))
	{
		ErrorFilenamef("ItemAssignmentSettings.def", "Couldn't generate itemSettings DurationModifier expression");
	}

	if (g_ItemAssignmentSettings.pExprForceCompleteNumericCost && 
		!exprGenerate(g_ItemAssignmentSettings.pExprForceCompleteNumericCost, ItemAssignments_GetContext(NULL)))
	{
		ErrorFilenamef("ItemAssignmentSettings.def", "Couldn't generate itemSettings ForceCompleteNumericCost expression");
	}
	
    // check if we don't have personal settings buckets, create one from the pePersonalRarityCounts/uPersonalAssignmentRefreshTime
	if (eaSize(&g_ItemAssignmentSettings.eaPersonalAssignmentSettings) == 0)
	{
		ItemAssignmentPersonalAssignmentSettings *pSettings = StructCreate(parse_ItemAssignmentPersonalAssignmentSettings);
		eaPush(&g_ItemAssignmentSettings.eaPersonalAssignmentSettings, pSettings);
		
		eaiCopy(&pSettings->peRarityCounts, &g_ItemAssignmentSettings.pePersonalRarityCounts);
		pSettings->uAssignmentRefreshTime = g_ItemAssignmentSettings.uPersonalAssignmentRefreshTime;
	}
	else
	{
		if (eaiSize(&g_ItemAssignmentSettings.pePersonalRarityCounts) > 0)
		{
			ErrorFilenamef("ItemAssignmentSettings.def", "PersonalRarityCount defined but PersonalAssignmentSettings are defined. Both cannot exist.");
		}
	}


}

static void ItemAssignmentSettings_LoadInternal(const char *pchPath, S32 iWhen)
{
	StructReset(parse_ItemAssignmentSettings, &g_ItemAssignmentSettings);
	
	if (pchPath)
	{
		fileWaitForExclusiveAccess(pchPath);
		errorLogFileIsBeingReloaded(pchPath);
	}

	ParserLoadFiles(NULL, 
					"defs/config/ItemAssignmentSettings.def", 
					"ItemAssignmentSettings.bin", 
					PARSER_OPTIONALFLAG, 
					parse_ItemAssignmentSettings, 
					&g_ItemAssignmentSettings);

	// Fixup
	ItemAssignmentSettings_Fixup();

	if (IsGameServerBasedType())
	{
		// Validation
		ItemAssignmentSettings_Validate();
	}
}

AUTO_STARTUP(ItemAssignmentSettings) ASTRT_DEPS(ItemAssignmentCategories, ItemAssignmentRarityCounts, Items, ItemQualities);
void ItemAssignmentSettings_Load(void)
{
	ItemAssignmentSettings_LoadInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ItemAssignmentSettings.def", ItemAssignmentSettings_LoadInternal);
}

AUTO_STARTUP(ItemAssignmentSettingsMinimal) ASTRT_DEPS(ItemAssignmentCategories, ItemAssignmentRarityCounts, ItemQualities);
void ItemAssignmentSettings_LoadMinimal(void)
{
	ItemAssignmentSettings_Load();
}

static void ItemAssignment_UpdateDependenciesFlag(ItemAssignmentDef* pDef)
{
	ItemAssignmentDef* pDefDict;
	ResourceIterator ResIterator;

	eaClearStruct(&pDef->eaDependencies, parse_ItemAssignmentDefRef);

	resInitIterator(g_hItemAssignmentDict, &ResIterator);
	while (resIteratorGetNext(&ResIterator, NULL, &pDefDict))
	{
		if (pDefDict->pRequirements)
		{
			if (pDef == GET_REF(pDefDict->pRequirements->hRequiredAssignment))
			{
				ItemAssignmentDefRef* pRef = StructCreate(parse_ItemAssignmentDefRef);
				SET_HANDLE_FROM_REFERENT(g_hItemAssignmentDict, pDefDict, pRef->hDef);
				eaPush(&pDef->eaDependencies, pRef);
			}
		}
	}
	resFreeIterator(&ResIterator);
}

static int ItemAssignmentDef_SortCategories(const S32 **ppLeft, const S32 **ppRight)
{
	ItemCategoryInfo* pLeft = item_GetItemCategoryInfo ((ItemCategory)*ppLeft);
	ItemCategoryInfo* pRight = item_GetItemCategoryInfo ((ItemCategory)*ppRight);

	if (pLeft && pRight)
	{
		if (pLeft->iSortOrder != pRight->iSortOrder)
			return pLeft->iSortOrder - pRight->iSortOrder;
		else
		{
			if (pLeft->pchName && pRight->pchName)
				return stricmp(pLeft->pchName, pRight->pchName);
		}
	}

	return 0;
}

static bool ItemAssignment_ValidateAllButRefs(ItemAssignmentDef* pDef)
{
	bool bSuccess = true;
	S32 i;

	if (!resIsValidName(pDef->pchName))
	{
		ErrorFilenamef(pDef->pchFileName, "Item Assignment name is illegal: '%s'", pDef->pchName);
		bSuccess = false;
	}

	if (!resIsValidScope(pDef->pchScope))
	{
		ErrorFilenamef(pDef->pchFileName, "Item Assignment scope is illegal: '%s'", pDef->pchScope);
		bSuccess = false;
	}

	if (!pDef->bRepeatable && pDef->uCooldownAfterCompletion)
	{
		ErrorFilenamef(pDef->pchFileName, "Item Assignment is set as non-repeatable, but also has a 'CooldownAfterCompletion' time");
		bSuccess = false;
	}

	if (pDef->pRequirements)
	{
		if (pDef->pRequirements->pExprRequires &&
			!exprGenerate(pDef->pRequirements->pExprRequires, ItemAssignments_GetContext(NULL)))
		{
			bSuccess = false;
		}
		if (pDef->pRequirements->iMaximumLevel &&
			pDef->pRequirements->iMinimumLevel > pDef->pRequirements->iMaximumLevel)
		{
			ErrorFilenamef(pDef->pchFileName, "Item Assignment minimum level is higher than its maximum level");
			bSuccess = false;
		}
	}

	for (i = eaSize(&pDef->eaOutcomes)-1; i >= 0; i--)
	{
		if (pDef->eaOutcomes[i]->pExprScaleAllNumerics &&
			!exprGenerate(pDef->eaOutcomes[i]->pExprScaleAllNumerics, ItemAssignments_GetContext(NULL)))
		{
			bSuccess = false;
		}
		if (!pDef->eaOutcomes[i]->pchName || !pDef->eaOutcomes[i]->pchName[0])
		{
			ErrorFilenamef(pDef->pchFileName, "Item Assignment Outcome %i has no name", i);
			bSuccess = false;
		}
	}

	for (i = eaSize(&pDef->eaModifiers)-1; i >= 0; i--)
	{
		if (!pDef->eaModifiers[i]->pchName || !pDef->eaModifiers[i]->pchName[0])
		{
			ErrorFilenamef(pDef->pchFileName, "Item Assignment Outcome Modifier %d has no name", i);
			bSuccess = false;
		}
	}

	// 
	if (eaSize(&pDef->eaSlots))
	{
		S32 iNumOptionalSlots = 0;
		S32 iNumRequiredSlots = 0;
		S32 numSlots = eaSize(&pDef->eaSlots);
		
		for (i = numSlots -1; i >= 0; --i)
		{
			ItemAssignmentSlot* pSlot = pDef->eaSlots[i];

			if (pSlot->bIsOptional)
			{
				iNumOptionalSlots++;
			}
			else
				iNumRequiredSlots ++;
		}

		// if there were any optional slots defined, 
		// the def's iMinimumSlottedItems must either be 0 or the correct number of minimum slots
		// if 0, we set it ourselves

		//S32 minimumNumSlots = numSlots - iNumOptionalSlots;
		if (g_ItemAssignmentSettings.bUseOptionalSlots)
		{
			pDef->iMinimumSlottedItems = iNumRequiredSlots;
		}
		
		if (iNumOptionalSlots)
		{
			pDef->bHasOptionalSlots = true;
		}

		if (iNumRequiredSlots)
		{
			pDef->bHasRequiredSlots = true;
		}

	}
	

	if (g_ItemAssignmentSettings.bSlotsSortItemCategories)
	{
		FOR_EACH_IN_EARRAY(pDef->eaSlots, ItemAssignmentSlot, pSlots)
		{
			if (eaiSize(&pSlots->peRequiredItemCategories))
			{
				eaiQSort(pSlots->peRequiredItemCategories, ItemAssignmentDef_SortCategories );
			}
			if (eaiSize(&pSlots->peRestrictItemCategories))
			{
				eaiQSort(pSlots->peRestrictItemCategories, ItemAssignmentDef_SortCategories );
			}
		}
		FOR_EACH_END
	}
	
	

	if (bSuccess)
	{
		ItemAssignment_UpdateDependenciesFlag(pDef);
		g_bRebuildItemAssignmentTree = true;
#if GAMESERVER
		gslItemAssignmentDef_Fixup(pDef);
#endif 
	}
	return bSuccess;
}

static bool ItemAssignment_CheckOutcomeExceedsMaxDepth(ItemAssignmentOutcome* pOutcome, S32 iCurrDepth, S32 iMaxDepth)
{
	if (iCurrDepth == iMaxDepth)
	{
		return true;
	}
	if (pOutcome->pResults)
	{
		ItemAssignmentDef* pNewDef = GET_REF(pOutcome->pResults->hNewAssignment);
		if (pNewDef)
		{
			S32 i;
			for (i = eaSize(&pNewDef->eaOutcomes)-1; i >= 0; i--)
			{
				ItemAssignmentOutcome* pNewOutcome = pNewDef->eaOutcomes[i];
				if (ItemAssignment_CheckOutcomeExceedsMaxDepth(pNewOutcome, iCurrDepth+1, iMaxDepth))
				{
					return true;
				}
			}
		}
	}
	return false;
}

static bool ItemAssignment_ValidateRefs(ItemAssignmentDef* pDef)
{
	bool bSuccess = true;
	S32 i;

	// Validate messages
	if (IsServer() && !GET_REF(pDef->msgDisplayName.hMessage) && REF_STRING_FROM_HANDLE(pDef->msgDisplayName.hMessage))
	{
		ErrorFilenamef(pDef->pchFileName, "Item Assignment refers to non-existent display message '%s'", REF_STRING_FROM_HANDLE(pDef->msgDisplayName.hMessage));
		bSuccess = false;
	}
	if (IsServer() && !GET_REF(pDef->msgDescription.hMessage) && REF_STRING_FROM_HANDLE(pDef->msgDescription.hMessage))
	{
		ErrorFilenamef(pDef->pchFileName, "Item Assignment refers to non-existent description message '%s'", REF_STRING_FROM_HANDLE(pDef->msgDescription.hMessage));
		bSuccess = false;
	}
	if (IsServer() && !GET_REF(pDef->msgAssignmentChainDisplayName.hMessage) && REF_STRING_FROM_HANDLE(pDef->msgAssignmentChainDisplayName.hMessage))
	{
		ErrorFilenamef(pDef->pchFileName, "Item Assignment refers to non-existent display message '%s'", REF_STRING_FROM_HANDLE(pDef->msgAssignmentChainDisplayName.hMessage));
		bSuccess = false;
	}
	if (IsServer() && !GET_REF(pDef->msgAssignmentChainDescription.hMessage) && REF_STRING_FROM_HANDLE(pDef->msgAssignmentChainDescription.hMessage))
	{
		ErrorFilenamef(pDef->pchFileName, "Item Assignment refers to non-existent description message '%s'", REF_STRING_FROM_HANDLE(pDef->msgAssignmentChainDescription.hMessage));
		bSuccess = false;
	}

	// Validate requirements
	if (pDef->pRequirements)
	{
		if (!GET_REF(pDef->pRequirements->hRequiredAllegiance) && REF_STRING_FROM_HANDLE(pDef->pRequirements->hRequiredAllegiance))
		{
			ErrorFilenamef(pDef->pchFileName, "Item Assignment Requirements references non-existent Allegiance '%s'", REF_STRING_FROM_HANDLE(pDef->pRequirements->hRequiredAllegiance));
			bSuccess = false;
		}
		if (!GET_REF(pDef->pRequirements->hRequiredAssignment) && REF_STRING_FROM_HANDLE(pDef->pRequirements->hRequiredAssignment))
		{
			ErrorFilenamef(pDef->pchFileName, "Item Assignment Requirements references non-existent Item Assignment '%s'", REF_STRING_FROM_HANDLE(pDef->pRequirements->hRequiredAssignment));
			bSuccess = false;
		}
		if (!GET_REF(pDef->pRequirements->hRequiredMission) && REF_STRING_FROM_HANDLE(pDef->pRequirements->hRequiredMission))
		{
			ErrorFilenamef(pDef->pchFileName, "Item Assignment Requirements references non-existent Mission '%s'", REF_STRING_FROM_HANDLE(pDef->pRequirements->hRequiredMission));
			bSuccess = false;
		}
		if (!GET_REF(pDef->pRequirements->hRequiredNumeric) && REF_STRING_FROM_HANDLE(pDef->pRequirements->hRequiredNumeric))
		{
			ErrorFilenamef(pDef->pchFileName, "Item Assignment Requirements references non-existent Numeric '%s'", REF_STRING_FROM_HANDLE(pDef->pRequirements->hRequiredNumeric));
			bSuccess = false;
		}
		else if (GET_REF(pDef->pRequirements->hRequiredNumeric))
		{
			if (GET_REF(pDef->pRequirements->hRequiredNumeric)->eType != kItemType_Numeric)
			{
				ErrorFilenamef(pDef->pchFileName, "Item Assignment Requirements references non-numeric item '%s'", REF_STRING_FROM_HANDLE(pDef->pRequirements->hRequiredNumeric));
				bSuccess = false;
			}
		}
		for (i = 0; i < eaSize(&pDef->pRequirements->eaItemCosts); i++)
		{
			ItemAssignmentItemCost* pItemCost = pDef->pRequirements->eaItemCosts[i];

			if (!GET_REF(pItemCost->hItem) && REF_STRING_FROM_HANDLE(pItemCost->hItem))
			{
				ErrorFilenamef(pDef->pchFileName, "Item Assignment Requirements references non-existent ItemDef '%s'", REF_STRING_FROM_HANDLE(pItemCost->hItem));
				bSuccess = false;
			}
		}
	}

	for (i = eaSize(&pDef->eaOutcomes)-1; i >= 0; i--)
	{
		ItemAssignmentOutcome* pOutcome = pDef->eaOutcomes[i];

		// Validate messages
		if (IsServer() && !GET_REF(pOutcome->msgDisplayName.hMessage) && REF_STRING_FROM_HANDLE(pOutcome->msgDisplayName.hMessage))
		{
			ErrorFilenamef(pDef->pchFileName, "Item Assignment refers to non-existent display message '%s'", REF_STRING_FROM_HANDLE(pOutcome->msgDisplayName.hMessage));
			bSuccess = false;
		}
		if (IsServer() && !GET_REF(pOutcome->msgDescription.hMessage) && REF_STRING_FROM_HANDLE(pOutcome->msgDescription.hMessage))
		{
			ErrorFilenamef(pDef->pchFileName, "Item Assignment refers to non-existent description message '%s'", REF_STRING_FROM_HANDLE(pOutcome->msgDescription.hMessage));
			bSuccess = false;
		}

		// Validate results
		if (!pOutcome->pResults)
		{
			ErrorFilenamef(pDef->pchFileName, "Item Assignment Outcome '%s' has no results", pOutcome->pchName);
			bSuccess = false;
		}
		else
		{
			if (!GET_REF(pOutcome->pResults->hNewAssignment) && REF_STRING_FROM_HANDLE(pOutcome->pResults->hNewAssignment))
			{
				ErrorFilenamef(pDef->pchFileName, "Item Assignment Outcome references non-existent Item Assignment '%s'", REF_STRING_FROM_HANDLE(pOutcome->pResults->hNewAssignment));
				bSuccess = false;
			}
			if (GET_REF(pOutcome->pResults->hNewAssignment))
			{
				if (pDef == GET_REF(pOutcome->pResults->hNewAssignment))
				{
					ErrorFilenamef(pDef->pchFileName, "Item Assignment cannot start itself as a new assignment");
					bSuccess = false;
				}
				else
				{
					const S32 iMaxDepth = 3;
					if (ItemAssignment_CheckOutcomeExceedsMaxDepth(pOutcome, 0, iMaxDepth))
					{
						ErrorFilenamef(pDef->pchFileName, "Item Assignment cannot have a new assignment depth >= %d", iMaxDepth);
						bSuccess = false;
					}
				}
			}
			if (!GET_REF(pOutcome->pResults->hRewardTable) && REF_STRING_FROM_HANDLE(pOutcome->pResults->hRewardTable))
			{
				ErrorFilenamef(pDef->pchFileName, "Item Assignment Outcome references non-existent Reward Table '%s'", REF_STRING_FROM_HANDLE(pOutcome->pResults->hRewardTable));
				bSuccess = false;
			}
		}
	}

	return bSuccess;
}

bool ItemAssignment_Validate(ItemAssignmentDef* pDef)
{
	bool bSuccess = true;

	if (!ItemAssignment_ValidateAllButRefs(pDef))
		bSuccess = false;

	if (!ItemAssignment_ValidateRefs(pDef))
		bSuccess = false;

	return bSuccess;
}

static int ItemAssignmentResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, ItemAssignmentDef *pDef, U32 userID)
{
	switch (eType)
	{	
		xcase RESVALIDATE_FIX_FILENAME: // Called for filename check
		{
			resFixPooledFilename(&pDef->pchFileName, ITEM_ASSIGNMENT_BASE_DIR, pDef->pchScope, pDef->pchName, ITEM_ASSIGNMENT_EXT);
		}
		return VALIDATE_HANDLED;
		xcase RESVALIDATE_POST_TEXT_READING: // Called after load/reload but before binning
		{
			ItemAssignment_ValidateAllButRefs(pDef);
#ifdef GAMESERVER
			gslItemAssignment_AddRemoteAssignment(pDef);
			gslItemAssignment_AddTrackedActivity(pDef);
#endif
		}
		return VALIDATE_HANDLED;
		xcase RESVALIDATE_CHECK_REFERENCES: // Called when all data has been loaded
		{
			if (IsServer() && !isProductionMode())
			{
				ItemAssignment_ValidateRefs(pDef);
				return VALIDATE_HANDLED;
			}
		}
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
int ItemAssignments_Startup(void)
{
	// Set up reference dictionaries
	g_hItemAssignmentDict = RefSystem_RegisterSelfDefiningDictionary("ItemAssignmentDef",false,parse_ItemAssignmentDef,true,true,NULL);
	
	if (IsGameServerBasedType())
	{
		resDictManageValidation(g_hItemAssignmentDict, ItemAssignmentResValidateCB);
	}
	if (IsServer())
	{
		resDictProvideMissingResources(g_hItemAssignmentDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hItemAssignmentDict, ".Name", ".Scope", NULL, NULL, NULL);
		}
	} 
	else
	{
		resDictRequestMissingResources(g_hItemAssignmentDict, 8, false, resClientRequestSendReferentCommand);
	}
	
	return 1;
}

AUTO_STARTUP(ItemAssignments) ASTRT_DEPS(ItemAssignmentSettings, ItemAssignmentWeights, ItemAssignmentVars, ItemAssignmentModifierTypes, RewardTables, Items, Allegiance);
void ItemAssignments_Load(void)
{
	if (!IsClient()) {
		resLoadResourcesFromDisk(g_hItemAssignmentDict,
								 ITEM_ASSIGNMENT_BASE_DIR,
								 "."ITEM_ASSIGNMENT_EXT,
								 NULL,
								 RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	}
}

AUTO_STARTUP(ItemAssignmentsMinimal) ASTRT_DEPS(ItemAssignmentSettingsMinimal, ItemAssignmentWeights, ItemAssignmentVars, ItemAssignmentModifierTypes);
void ItemAssignments_LoadMinimal(void)
{
	ItemAssignments_Load();
}

ItemAssignmentDef* ItemAssignment_DefFromName(const char* pchDefName)
{
	return (ItemAssignmentDef*)RefSystem_ReferentFromString(g_hItemAssignmentDict, pchDefName);
}

AUTO_TRANS_HELPER;
NOCONST(ItemAssignment)* ItemAssignment_trh_EntityGetActiveAssignmentByID(ATH_ARG NOCONST(Entity)* pEnt, U32 uID)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pItemAssignmentPersistedData) && uID)
	{
		NOCONST(ItemAssignmentPersistedData)* pPersistedData = pEnt->pPlayer->pItemAssignmentPersistedData;
		S32 i;
		for (i = eaSize(&pPersistedData->eaActiveAssignments)-1; i >= 0; i--)
		{
			NOCONST(ItemAssignment)* pAssignment = pPersistedData->eaActiveAssignments[i];
			if (pAssignment->uAssignmentID == uID)
			{
				return pAssignment;
			}
		}
	}
	return NULL;
}

AUTO_TRANS_HELPER;
NOCONST(ItemAssignment)* ItemAssignment_trh_EntityGetActiveAssignmentByDef(ATH_ARG NOCONST(Entity)* pEnt, ItemAssignmentDef* pDef)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pItemAssignmentPersistedData) && pDef)
	{
		NOCONST(ItemAssignmentPersistedData)* pPersistedData = pEnt->pPlayer->pItemAssignmentPersistedData;
		S32 i;
		for (i = eaSize(&pPersistedData->eaActiveAssignments)-1; i >= 0; i--)
		{
			NOCONST(ItemAssignment)* pAssignment = pPersistedData->eaActiveAssignments[i];
			if (GET_REF(pAssignment->hDef) == pDef)
			{
				return pAssignment;
			}
		}
	}
	return NULL;
}

AUTO_TRANS_HELPER;
NOCONST(ItemAssignmentCompletedDetails)* ItemAssignment_trh_EntityGetRecentlyCompletedAssignmentByID(ATH_ARG NOCONST(Entity)* pEnt, U32 uID)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pItemAssignmentPersistedData) && uID)
	{
		NOCONST(ItemAssignmentPersistedData)* pPersistedData = pEnt->pPlayer->pItemAssignmentPersistedData;
		S32 i;
		for (i = eaSize(&pPersistedData->eaRecentlyCompletedAssignments)-1; i >= 0; i--)
		{
			NOCONST(ItemAssignmentCompletedDetails)* pCompletedAssignment = pPersistedData->eaRecentlyCompletedAssignments[i];
			if (pCompletedAssignment->uAssignmentID == uID)
			{
				return pCompletedAssignment;
			}
		}
	}
	return NULL;
}

ItemAssignmentCategorySettings* ItemAssignmentCategory_GetSettings(ItemAssignmentCategory eCategory)
{
	return eaGet(&s_ItemAssignmentCategorySettings.eaCategories, eCategory-1);
}

const ItemAssignmentCategorySettings** ItemAssignmentCategory_GetCategoryList()
{
	return s_ItemAssignmentCategorySettings.eaCategories;
}


ItemAssignmentRarityCount* ItemAssignment_GetRarityCountByType(ItemAssignmentRarityCountType eType)
{
	if (eType != kItemAssignmentRarityCountType_None)
	{
		return eaGet(&s_ItemAssignmentRarityCounts.eaRarityCounts, eType-1);
	}
	return NULL;
}


ItemAssignmentWeight* ItemAssignmentWeightType_GetWeightDef(ItemAssignmentWeightType eWeightType)
{
	if (eWeightType != kItemAssignmentWeightType_Default)
	{
		ItemAssignmentWeight* pData = eaGet(&s_ItemAssignmentWeights.eaData, eWeightType-1);
		if (pData)
		{
			return pData;
		}
	}
	return NULL;
}

F32 ItemAssignmentWeightType_GetWeightValue(ItemAssignmentWeightType eWeightType)
{
	if (eWeightType != kItemAssignmentWeightType_Default)
	{
		ItemAssignmentWeight* pData = eaGet(&s_ItemAssignmentWeights.eaData, eWeightType-1);
		if (pData)
		{
			return pData->fWeight;
		}
	}
	return 0.0f;
}

S32 ItemAssignments_EvaluateCategoryIsHidden(Entity *pEnt, const ItemAssignmentCategorySettings *pCategory)
{
	if (pCategory->pExprIsCategoryHidden)
	{
		MultiVal mVal;
		ExprContext* pContext = ItemAssignments_GetContext(pEnt);
		exprEvaluate(pCategory->pExprIsCategoryHidden, pContext, &mVal);
		return MultiValGetInt(&mVal,NULL);
	}

	return false;
}


F32 ItemAssignment_GetDurationScale(ItemAssignment* pAssignment, ItemAssignmentDef* pDef)
{
	if (pDef)
	{
		U32 uDuration = ItemAssignment_GetDuration(pAssignment, pDef);
		ItemAssignmentDurationScaleCategory eCategory = pDef->eNumericDurationScaleCategory;
		ItemAssignmentDurationScaleCategoryData* pData;
		if (pData = eaGet(&g_ItemAssignmentSettings.eaDurationScaleCategories, eCategory-1))
		{
			S32 i;
			for (i = eaSize(&pData->eaDurationScales)-1; i >= 0; i--)
			{
				if (uDuration >= pData->eaDurationScales[i]->uDurationMin &&
					uDuration <= pData->eaDurationScales[i]->uDurationMax)
				{
					return (uDuration * pData->eaDurationScales[i]->fScale);
				}
			}
		}
	}
	return 1.0f;
}

F32 ItemAssignment_GetNumericQualityScaleForItemDef(ItemDef* pItemDef)
{
	if (pItemDef)
	{
		ItemAssignmentQualityNumericScale* pData;
		if (pData = eaIndexedGetUsingInt(&g_ItemAssignmentSettings.eaQualityScales, pItemDef->Quality))
		{
			return pData->fScale;
		}
	}
	return 0.0f;
}

F32 ItemAssignment_GetNumericQualityScale(Entity* pEnt, const ItemAssignment* pAssignment, ItemAssignmentCompletedDetails* pCompletedDetails, const ItemAssignmentDef* pDef)
{
	F32 fScale = 1.0f;

	if (pEnt && pDef)
	{
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		int i;

		if (pAssignment)
		{
			for (i = eaSize(&pAssignment->eaSlottedItems)-1; i >= 0; i--)
			{
				ItemAssignmentSlottedItem* pSlottedItem = pAssignment->eaSlottedItems[i];
				Item* pItem = ItemAssignments_GetItemFromSlottedItem(pEnt, pSlottedItem, pExtract);
				ItemDef* pItemDef = SAFE_GET_REF(pItem, hItem);
				fScale += ItemAssignment_GetNumericQualityScaleForItemDef(pItemDef);
			}
		}
		else if (pCompletedDetails)
		{
			for (i = eaSize(&pCompletedDetails->eaSlottedItemRefs)-1; i >= 0; i--)
			{
				ItemAssignmentSlottedItemResults* pSlottedItem = pCompletedDetails->eaSlottedItemRefs[i];
				ItemDef* pItemDef = GET_REF(pSlottedItem->hDef);
				fScale += ItemAssignment_GetNumericQualityScaleForItemDef(pItemDef);
			}
		}	
	}
	return fScale;
}

ItemAssignmentOutcome* ItemAssignment_GetOutcomeByName(ItemAssignmentDef* pDef, const char* pchOutcomeName)
{
	const char* pchOutcomePooled = allocFindString(pchOutcomeName);
	if (pDef && pchOutcomePooled && pchOutcomePooled[0])
	{
		S32 i;
		for (i = eaSize(&pDef->eaOutcomes)-1; i >= 0; i--)
		{
			ItemAssignmentOutcome* pOutcome = pDef->eaOutcomes[i];
			if (pOutcome->pchName == pchOutcomePooled)
			{
				return pOutcome;
			}
		}
	}
	return NULL;
}

ItemAssignmentOutcomeModifierTypeData* ItemAssignment_GetModifierTypeData(ItemAssignmentOutcomeModifierType eType)
{
	S32 i;
	for (i = eaSize(&s_ItemAssignmentModifierTypes.eaData)-1; i >= 0; i--)
	{
		if (eType == s_ItemAssignmentModifierTypes.eaData[i]->eType)
		{
			return s_ItemAssignmentModifierTypes.eaData[i];
		}
	}
	return NULL;
}

ItemAssignmentOutcomeModifier* ItemAssignment_GetModifierByName(ItemAssignmentDef* pDef, const char* pchModifierName)
{
	const char* pchModifierPooled = allocFindString(pchModifierName);
	if (pDef && pchModifierPooled && pchModifierPooled[0])
	{
		S32 i, iNumModifiers = eaSize(&pDef->eaModifiers);
		if (iNumModifiers)
		{
			for (i = iNumModifiers-1; i >= 0; i--)
			{
				ItemAssignmentOutcomeModifier* pModifier = pDef->eaModifiers[i];
				if (pModifier->pchName == pchModifierPooled)
				{
					return pModifier;
				}
			}
		}
		else 
		{	// if the ItemAssignmentDef does not have any modifiers, attempt to use the defaults on the ItemAssignmentSettings if they exist
			FOR_EACH_IN_EARRAY(g_ItemAssignmentSettings.eaDefaultWeightModifierSettings, ItemAssignmentOutcomeModifier, pModifier)
			{
				if (pModifier->pchName == pchModifierPooled)
					return pModifier;
			}
			FOR_EACH_END
		}
	}
	
	return NULL;
}

F32 ItemAssignments_GetQualityWeight(ItemQuality eQuality, const char* pchOutcomeName)
{
	ItemAssignmentQualityWeight* pQualityWeight;
	if (pQualityWeight = eaIndexedGetUsingInt(&g_ItemAssignmentSettings.eaQualityWeights, eQuality))
	{
		if (pchOutcomeName)
		{
			ItemAssignmentOutcomeWeight* pOutcomeWeight;
			if (pOutcomeWeight = eaIndexedGetUsingString(&pQualityWeight->eaOutcomes, pchOutcomeName))
			{
				return ItemAssignmentWeightType_GetWeightValue(pOutcomeWeight->eWeight);
			}
		}
		return ItemAssignmentWeightType_GetWeightValue(pQualityWeight->eWeight);
	}
	return 0.0f;
}

F32 ItemAssignments_GetQualityDurationScale(ItemQuality eQuality)
{
	ItemAssignmentQualityDurationScale* pQualityWeight;
	if (pQualityWeight = eaIndexedGetUsingInt(&g_ItemAssignmentSettings.eaDurationScales, eQuality))
	{
		return pQualityWeight->fScale;
	}
	return 0.0f;
}

F32 ItemAssignments_GetOutcomeWeightModifier(Entity* pEnt, 
											 ItemAssignmentOutcome* pOutcome, 
											 ItemAssignmentOutcomeModifier* pMod, 
											 ItemAssignmentOutcomeModifierTypeData* pData,
											 ItemAssignmentSlot *pSlotDef, 
											 Item* pSlottedItem, 
											 ItemCategory eItemCategoryUI) 
{
	if (pEnt && pMod && pData && (pSlottedItem || eItemCategoryUI != kItemCategory_None))
	{
		if (pData->pExprWeightModifier)
		{
			MultiVal mVal;
			ExprContext* pContext = ItemAssignments_GetContextEx(pEnt, NULL, pOutcome, pMod, pSlottedItem, pSlotDef, eItemCategoryUI, 0);
			exprEvaluate(pData->pExprWeightModifier, pContext, &mVal);
			return (F32)MultiValGetFloat(&mVal,NULL);
		}
	}
	return 0.0f;
}

F32 ItemAssignments_GetDurationModifier(Entity* pEnt, 
										Item* pSlottedItem, 
										ItemAssignmentOutcomeModifier *pMod, 
										ItemAssignmentSlot *pSlotDef, 
										ItemCategory eItemCategoryUI)
{
	if (pEnt && (pSlottedItem || eItemCategoryUI != kItemCategory_None))
	{
		if (g_ItemAssignmentSettings.pExprDurationModifier)
		{
			MultiVal mVal;
			ExprContext* pContext = ItemAssignments_GetContextEx(pEnt, NULL, NULL, pMod, pSlottedItem, pSlotDef, eItemCategoryUI, 0);
			exprEvaluate(g_ItemAssignmentSettings.pExprDurationModifier, pContext, &mVal);
			return (F32)MultiValGetFloat(&mVal,NULL);
		}
	}
	return 0.0f;
}

const char** ItemAssignments_GetOutcomeModifiersForSlot(ItemAssignmentSlot* pSlot)
{	// use the slot's ppchOutcomeModifiers if there's something in the list, otherwise the default
	return eaSize(&pSlot->ppchOutcomeModifiers) ? pSlot->ppchOutcomeModifiers :
													g_ItemAssignmentSettings.ppchDefaultOutcomeModifiers;
}

static void ItemAssignments_UpdateOutcomeWeightsForSlot(Entity* pEnt,
														ItemAssignmentDef* pDef,
														ItemAssignmentSlot* pSlot,
														Item* pItem,
														F32* pfBaseWeights,
														F32** ppfOutcomeWeightsOut)
{
	ItemDef* pItemDef = SAFE_GET_REF(pItem, hItem);
	F32 fSlotWeight = 0.0f;
	S32 i, j, k;
	S32* piOutcomeIndexes = NULL;

	if (pItemDef)
	{
		for (i = eaSize(&pDef->eaOutcomes)-1; i >= 0; i--)
		{
			ItemAssignmentOutcome* pOutcome = pDef->eaOutcomes[i];
			const char** ppchOutcomeModifiers = ItemAssignments_GetOutcomeModifiersForSlot(pSlot);

			for (j = eaSize(&ppchOutcomeModifiers)-1; j >= 0; j--)
			{
				const char* pchModifier = ppchOutcomeModifiers[j];
				ItemAssignmentOutcomeModifier* pMod = ItemAssignment_GetModifierByName(pDef, pchModifier);
				ItemAssignmentOutcomeModifierTypeData* pData = pMod ? ItemAssignment_GetModifierTypeData(pMod->eType) : NULL;
	
				// Check to see if the modifier should affect the specified outcome
				if (pData && eaFind(&pData->ppchAffectedOutcomes, pOutcome->pchName) >= 0)
				{
					F32 fResult = ItemAssignments_GetOutcomeWeightModifier(pEnt, pOutcome, pMod, pData, pSlot, pItem, kItemCategory_None);
				
					// Apply the weight adjustment
					(*ppfOutcomeWeightsOut)[i] += fResult;
				
					// If this is actually modifying the weight, then also do dependent outcome calculations
					if (pData->pchDependentOutcome && !nearSameF32(fResult, 0.0f))
					{
						F32 fTotalWeight = 0.0f;
						eaiClear(&piOutcomeIndexes);
						for (k = eaSize(&pDef->eaOutcomes)-1; k >= 0; k--)
						{
							ItemAssignmentOutcome* pFindOutcome = pDef->eaOutcomes[k];
							if (pFindOutcome != pOutcome &&
								pFindOutcome->pchName != pData->pchDependentOutcome)
							{
								eaiPush(&piOutcomeIndexes, k);
								fTotalWeight += pfBaseWeights[k];
							}
						}
						if (fTotalWeight > 0.0f)
						{
							F32 fMult = fResult / fTotalWeight;
							for (k = eaiSize(&piOutcomeIndexes)-1; k >= 0; k--)
							{
								S32 iOutcomeIndex = piOutcomeIndexes[k];
								(*ppfOutcomeWeightsOut)[iOutcomeIndex] -= pfBaseWeights[iOutcomeIndex] * fMult;
							}
						}
					}
				}
			}
		}
	}
	eaiDestroy(&piOutcomeIndexes);
}

// --------------------------------------------------------------------------------------------------------------------
// calculates weights based on the ItemAssignmentOutcomeModifierType data
static void ItemAssignments_CalculateOutcomeWeightsByModifierType(	Entity* pEnt, 
																	ItemAssignmentDef* pDef, 
																	ItemAssignmentSlottedItem** eaSlottedItems, 
																	F32** ppfOutcomeWeightsOut)
{
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	S32 iSlotCount = eaSize(&pDef->eaSlots);
	S32 i, j;
	F32* pfBaseWeights = NULL;

	ea32SetSize((U32**)ppfOutcomeWeightsOut, eaSize(&pDef->eaOutcomes));
	ea32SetSize((U32**)&pfBaseWeights, eaSize(&pDef->eaOutcomes));

	// Cache outcome base weights
	for (i = eaSize(&pDef->eaOutcomes)-1; i >= 0; i--)
	{
		pfBaseWeights[i] = ItemAssignmentWeightType_GetWeightValue(pDef->eaOutcomes[i]->eBaseWeight);
	}

	// Calculate the slot weight contribution
	for (i = 0; i < iSlotCount; i++)
	{
		ItemAssignmentSlot* pSlot = pDef->eaSlots[i];
		Item* pItem = NULL;

		for (j = eaSize(&eaSlottedItems)-1; j >= 0; j--)
		{
			ItemAssignmentSlottedItem* pSlottedItem = eaSlottedItems[j];
			if (pSlottedItem->iAssignmentSlot == i)
			{
				pItem = ItemAssignments_GetItemFromSlottedItem(pEnt, pSlottedItem, pExtract);
				break;
			}
		}
		ItemAssignments_UpdateOutcomeWeightsForSlot(pEnt, pDef, pSlot, pItem, pfBaseWeights, ppfOutcomeWeightsOut);
	}

	// Finalize outcome weights
	for (i = eaSize(&pDef->eaOutcomes)-1; i >= 0; i--)
	{
		ItemAssignmentOutcome* pOutcome = pDef->eaOutcomes[i];

		// Get the average weight contribution of all slots
		if (!g_ItemAssignmentSettings.bDoNotAverageOutcomeWeights && iSlotCount)
		{
			(*ppfOutcomeWeightsOut)[i] /= iSlotCount;
		}

		// Add the base weight
		(*ppfOutcomeWeightsOut)[i] += pfBaseWeights[i];
		MAX1F((*ppfOutcomeWeightsOut)[i], 0.0f);
	}

	ea32Destroy((U32**)&pfBaseWeights);
}


static void ItemAssignments_CalculateOutcomeWeightsByWindow(Entity* pEnt, 
															ItemAssignmentDef* pDef, 
															ItemAssignmentSlottedItem** eaSlottedItems, 
															F32** ppfOutcomeWeightsOut)
{
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	ItemAssignmentOutcomeWeightWindowConfig *pConfig = g_ItemAssignmentSettings.pOutcomeWeightWindowConfig;
	// first calculate our window's base position based on what is slotted and how it effects duration
	F32 fBaseNumber = 0.f;
	S32 iNumOutcomes = eaSize(&pDef->eaOutcomes);

	devassert(pConfig);
	
	if (!iNumOutcomes)
		return;

	fBaseNumber = pConfig->fStartingWeight;

	FOR_EACH_IN_EARRAY(eaSlottedItems, ItemAssignmentSlottedItem, pSlot)
	{
		Item* pItem = NULL;
		ItemDef* pItemDef = NULL;
		pItem = ItemAssignments_GetItemFromSlottedItem(pEnt, pSlot, pExtract);
		pItemDef = SAFE_GET_REF(pItem, hItem);
		if (pItemDef)
		{
			bool bHasCategory = false;
			FOR_EACH_IN_EARRAY_INT(pItemDef->peCategories, ItemCategory, eCat)
			{
				if (eaiFind(&pConfig->peWeightedItemCategories, eCat) >= 0)
				{
					bHasCategory = true;
					break;
				}
			}
			FOR_EACH_END
			
			if (bHasCategory)
			{
				fBaseNumber += ItemAssignments_GetQualityWeight(pItemDef->Quality, NULL);
			}
		}
	}
	FOR_EACH_END
	
	// calculate the weights based on the WeightChanceWindow
	{
		F32 fCurWindowMin = 0.f;
		S32 i;

		eaiSetSize(ppfOutcomeWeightsOut, eaSize(&pDef->eaOutcomes));

		for (i = 0; i < iNumOutcomes; ++i)
		{
			F32 fCurWindowMax = fCurWindowMin + pConfig->fWeightPerOutcome;

			if (fBaseNumber > fCurWindowMax)
			{	// window went past the end
				(*ppfOutcomeWeightsOut)[i] = 0.f;
			}
			else if (fBaseNumber + pConfig->fWeightChanceWindow <= fCurWindowMin)
			{	// window doesn't overlap
				(*ppfOutcomeWeightsOut)[i] = 0.f;
			}
			else if (fBaseNumber < fCurWindowMin)
			{
				(*ppfOutcomeWeightsOut)[i] = (fBaseNumber + pConfig->fWeightChanceWindow) - fCurWindowMin;
			}
			else
			{
				(*ppfOutcomeWeightsOut)[i] = fCurWindowMax - fBaseNumber;
			}

			if ((*ppfOutcomeWeightsOut)[i] > pConfig->fWeightPerOutcome)
				(*ppfOutcomeWeightsOut)[i] = pConfig->fWeightPerOutcome;

			fCurWindowMin += pConfig->fWeightPerOutcome;
		}

		// if the window is past the last outcome, this will give the last outcome 100% chance
		if (fBaseNumber > fCurWindowMin)
		{
			(*ppfOutcomeWeightsOut)[iNumOutcomes-1] = 1.f;
		}
	}
}

// for UI displaying purposes, calculate a modify value that might make sense to the user
// only supports g_ItemAssignmentSettings.pOutcomeWeightWindowConfig
F32 ItemAssignments_CalculateOutcomeDisplayValueForSlot(ItemAssignmentDef* pDef, Item* pItem)
{
	if (pItem && g_ItemAssignmentSettings.pOutcomeWeightWindowConfig)
	{
		ItemAssignmentOutcomeWeightWindowConfig *pConfig = g_ItemAssignmentSettings.pOutcomeWeightWindowConfig;
		ItemDef *pItemDef = GET_REF(pItem->hItem);
		S32 iNumOutcomes = eaSize(&pDef->eaOutcomes);
		F32 fWeight = 0.f;
		if (iNumOutcomes && pItemDef)
		{
			bool bHasCategory = false;
			F32 fDenom = pConfig->fStartingWeight + pConfig->fWeightPerOutcome;

			FOR_EACH_IN_EARRAY_INT(pItemDef->peCategories, ItemCategory, eCat)
			{
				if (eaiFind(&pConfig->peWeightedItemCategories, eCat) >= 0)
				{
					bHasCategory = true;
					break;
				}
			}
			FOR_EACH_END

			if (bHasCategory)
			{
				fWeight = ItemAssignments_GetQualityWeight(pItemDef->Quality, NULL);
			}
			
			if (fDenom > 0.f)
				return fWeight / fDenom;
		}
	}


	return 0.f;
}

// --------------------------------------------------------------------------------------------------------------------
// Given the slotted items, returns the weights for each outcome. 
void ItemAssignments_CalculateOutcomeWeights(	Entity* pEnt, 
												ItemAssignmentDef* pDef, 
												ItemAssignmentSlottedItem** eaSlottedItems, 
												F32** ppfOutcomeWeightsOut)
{
	if (g_ItemAssignmentSettings.pOutcomeWeightWindowConfig)
	{
		ItemAssignments_CalculateOutcomeWeightsByWindow(pEnt, pDef, eaSlottedItems, ppfOutcomeWeightsOut);
	}
	else
	{
		ItemAssignments_CalculateOutcomeWeightsByModifierType(pEnt, pDef, eaSlottedItems, ppfOutcomeWeightsOut);
	}
	
}

// given the itemAssignments and the slotted items, calculate the duration
U32 ItemAssignments_CalculateDuration(Entity *pEnt, ItemAssignmentDef *pDef, ItemAssignmentSlottedItem **eaSlottedItems)
{
	F32 fModifier = 0.f;
	F32 fDuration;
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	if (eaSize(&eaSlottedItems) && g_ItemAssignmentSettings.pExprDurationModifier)
	{
		FOR_EACH_IN_EARRAY(pDef->eaSlots, ItemAssignmentSlot, pSlotDef)
		{
			Item* pItem = NULL;

			FOR_EACH_IN_EARRAY(eaSlottedItems, ItemAssignmentSlottedItem, pSlottedItem)
			{
				if (pSlottedItem->iAssignmentSlot == FOR_EACH_IDX(-, pSlotDef))
				{
					pItem = ItemAssignments_GetItemFromSlottedItem(pEnt, pSlottedItem, pExtract);
					break;
				}
			}
			FOR_EACH_END

				if (pItem)
				{
					fModifier += ItemAssignments_GetDurationModifier(pEnt, pItem, 
										g_ItemAssignmentSettings.pDefaultDurationScaleModifierSettings, 
										pSlotDef, kItemCategory_None);
				}
		}
		FOR_EACH_END
	}
		
	if (fModifier <= -1.f)
		fModifier = -0.99f;

	fDuration = pDef->uDuration/(1.f + fModifier);
	if (fDuration <= 0.5f)
		fDuration = 1.f; 
		
	return (U32)floor(fDuration + 0.5f);
	
}

AUTO_TRANS_HELPER;
NOCONST(InventorySlot)* ItemAssignments_trh_GetInvSlotFromSlottedItem(ATR_ARGS,
																	  ATH_ARG NOCONST(Entity)* pEnt, 
																	  ATH_ARG NOCONST(ItemAssignmentSlottedItem)* pSlottedItem,
																	  GameAccountDataExtract* pExtract)
{
	NOCONST(InventorySlot)* pSlot = NULL;

	if (pSlottedItem->eBagID && pSlottedItem->iBagSlot)
	{
		NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, pSlottedItem->eBagID, pExtract);
		if (NONNULL(pBag))
		{
			pSlot = eaGet(&pBag->ppIndexedInventorySlots, pSlottedItem->iBagSlot);
		}
	}
	if (ISNULL(pSlot) || ISNULL(pSlot->pItem) || pSlot->pItem->id != pSlottedItem->uItemID)
	{
		BagIterator* pIter = inv_trh_FindItemByIDEx(ATR_PASS_ARGS, pEnt, NULL, pSlottedItem->uItemID, false, true);
		NOCONST(InventoryBag)* pBag = bagiterator_GetCurrentBag(pIter);
		if (NONNULL(pIter) && NONNULL(pBag))
		{
			pSlot = pBag->ppIndexedInventorySlots[pIter->i_cur];
		}
		else
		{
			pSlot = NULL;
		}
		pSlottedItem->eBagID = SAFE_MEMBER(pBag, BagID);
		pSlottedItem->iBagSlot = SAFE_MEMBER(pIter, i_cur);
		bagiterator_Destroy(pIter);
	}
	return pSlot;
}

Item* ItemAssignments_GetItemFromSlottedItem(Entity* pEnt, ItemAssignmentSlottedItem* pSlottedItem, GameAccountDataExtract* pExtract)
{
	NOCONST(InventorySlot)* pSlot;
	pSlot = ItemAssignments_trh_GetInvSlotFromSlottedItem(ATR_EMPTY_ARGS, 
														  CONTAINER_NOCONST(Entity, pEnt), 
														  CONTAINER_NOCONST(ItemAssignmentSlottedItem, pSlottedItem),
														  pExtract);
	if (pSlot)
	{
		return (Item*)pSlot->pItem;
	}
	return NULL;
}

AUTO_TRANS_HELPER;
bool ItemAssignments_trh_CanSlotItem(ATR_ARGS,
									 ATH_ARG NOCONST(Entity)* pEnt, 
									 ItemAssignmentSlot* pSlot, 
									 InvBagIDs eBagID, 
									 S32 iBagSlot, 
									 GameAccountDataExtract* pExtract)
{
	NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, eBagID, pExtract);
	const InvBagDef* pBagDef = invbag_trh_def(pBag);
	NOCONST(Item)* pItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pBag, iBagSlot);
	ItemDef* pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

	if (!pSlot)
		return false;

	// The item cannot currently be equipped
	if (!pBagDef || (pBagDef->flags & InvBagFlag_EquipBag))
	{
		return false;
	}
	// Check that the bag is valid for a slotted item
	if (!ItemAssignment_trh_CanSlottedItemResideInBag(pBag))
	{
		return false;
	}
	// The item cannot already be on an assignment or in training
	if (!item_trh_CanRemoveItem(pItem))
	{
		return false;
	}
	// The item must be flagged as 'CanSlotOnAssignment'
	if (!pItemDef || !(pItemDef->flags & kItemDefFlag_CanSlotOnAssignment))
	{
		return false;
	}
	
	// The item must have one of the required item categories
	if (eaiSize(&pSlot->peRequiredItemCategories))
	{
		if (!g_ItemAssignmentSettings.bUseStrictCategoryChecking)
		{
			if (!itemdef_HasItemCategory(pItemDef, pSlot->peRequiredItemCategories))
				return false;
		}
		else
		{
			if (!itemdef_HasAllItemCategories(pItemDef, pSlot->peRequiredItemCategories))
				return false;
		}
	}
			

	// The item cannot have a restrict item category
	if (itemdef_HasItemCategory(pItemDef, pSlot->peRestrictItemCategories))
	{
		return false;
	}
	// The player must be able to use the item. Doesn't check the requires expression on the ItemDef.
	if (!itemdef_trh_VerifyUsageRestrictions(ATR_PASS_ARGS, pEnt, pEnt, pItemDef, 0, NULL))
	{
		return false;
	}
	return true;
}

AUTO_TRANS_HELPER;
bool ItemAssignments_trh_ValidateSlots(ATR_ARGS,
									   ATH_ARG NOCONST(Entity)* pEnt, 
									   ItemAssignmentDef* pDef, 
									   ItemAssignmentSlots* pSlots, 
									   GameAccountDataExtract* pExtract)
{
	if (pSlots)
	{
		S32 i, j, iSlotCount = eaSize(&pSlots->eaSlots);
		
		if (iSlotCount < pDef->iMinimumSlottedItems)
		{
			return false;
		}
		
		if (pDef->bHasOptionalSlots && iSlotCount < eaSize(&pDef->eaSlots))
		{	
			// this def has slots marked as optional, so we need to make sure all the ones that aren't tagged optional are present.
			FOR_EACH_IN_EARRAY(pDef->eaSlots, ItemAssignmentSlot, pSlotDef)
			{
				if (!pSlotDef->bIsOptional)
				{	// make sure we have something slotted here
					bool bFound = false;
					for (i = iSlotCount-1; i >= 0; i--)
					{
						NOCONST(ItemAssignmentSlottedItem)* pSlottedItem = CONTAINER_NOCONST(ItemAssignmentSlottedItem, pSlots->eaSlots[i]);
						
						if (pSlottedItem->iAssignmentSlot == FOR_EACH_IDX(0,pSlotDef))
						{
							bFound = true;
							break;
						}
					}
					if (!bFound)
						return false;
				}
			}
			FOR_EACH_END
		}

		for (i = iSlotCount-1; i >= 0; i--)
		{
			NOCONST(ItemAssignmentSlottedItem)* pSlottedItem = CONTAINER_NOCONST(ItemAssignmentSlottedItem, pSlots->eaSlots[i]);
			ItemAssignmentSlot* pSlot = eaGet(&pDef->eaSlots, pSlottedItem->iAssignmentSlot);
			NOCONST(InventorySlot)* pInvSlot = ItemAssignments_trh_GetInvSlotFromSlottedItem(ATR_PASS_ARGS, pEnt, pSlottedItem, pExtract);
			
			if (!pSlot) 
			{
				// there is no defined slot for the slotted item! This shouldn't happen. 
				// The UI might not be clearing the slotted items for the current assignment def
				return false;
			}

			if (ISNULL(pInvSlot))
			{	
				if (pSlot->bIsOptional)
					continue; // this slot is optional, it's okay if there's no item here

				return false;
			}

			// Don't allow an item to be slotted more than once
			for (j = i-1; j >= 0; j--)
			{
				if (pSlottedItem->uItemID == pSlots->eaSlots[j]->uItemID)
				{
					return false;
				}
			}

			if (!ItemAssignments_trh_CanSlotItem(ATR_PASS_ARGS, pEnt, pSlot, pSlottedItem->eBagID, pSlottedItem->iBagSlot, pExtract))
			{
				return false;
			}
		}
	}
	return true;
}

AUTO_TRANS_HELPER;
bool ItemAssignment_trh_CanSlottedItemResideInBag(ATH_ARG NOCONST(InventoryBag)* pBag)
{
	if (ISNULL(pBag))
	{
		return false;
	}
	if (invbag_trh_flags(pBag) & (InvBagFlag_EquipBag|InvBagFlag_DeviceBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag))
	{
		return false;
	}
	return true;
}

AUTO_TRANS_HELPER;
S32 ItemAssignments_trh_GetMaxAssignmentPoints(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt)
{
	S32 iPoints = g_ItemAssignmentSettings.iActiveAssignmentPointsPerPlayer;

	if (NONNULL(pEnt))
	{
		iPoints += inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, "Additemassignmentpoints");
	}

	return iPoints;
}

AUTO_TRANS_HELPER;
S32 ItemAssignments_trh_GetRemainingAssignmentPoints(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt)
{
	S32 i, iRemaining = ItemAssignments_trh_GetMaxAssignmentPoints(ATR_PASS_ARGS, pEnt);

	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pItemAssignmentPersistedData))
	{
		NOCONST(ItemAssignmentPersistedData)* pPersistedData = pEnt->pPlayer->pItemAssignmentPersistedData;
		for (i = eaSize(&pPersistedData->eaActiveAssignments)-1; i >= 0; i--)
		{
			ItemAssignmentDef* pDef = GET_REF(pPersistedData->eaActiveAssignments[i]->hDef);
			if (pDef)
			{
				iRemaining -= pDef->iAssignmentPointCost;
			}
		}
	}
	return iRemaining;
}

static void ItemAssignments_AddFailRequiresEntry(ItemAssignmentFailsRequiresEntry*** peaEntries,
												 ItemAssignmentFailsRequiresReason eReason,
												 const char* pchReasonKey,
												 S32 iValue)
{
	ItemAssignmentFailsRequiresEntry* pEntry = StructCreate(parse_ItemAssignmentFailsRequiresEntry);
	pEntry->eReason = eReason;
	pEntry->pchReasonKey = allocAddString(pchReasonKey);
	pEntry->iValue = iValue;
	eaPush(peaEntries, pEntry);
}

// Check fields on the player that are expected to change
AUTO_TRANS_HELPER;
ItemAssignmentFailsRequiresReason ItemAssignments_trh_GetFailsRequiresReason(ATR_ARGS, 
																			 ATH_ARG NOCONST(Entity)* pEnt, 
																			 ItemAssignmentDef* pDef,
																			 ItemAssignmentSlots* pSlots,
																			 ItemAssignmentFailsRequiresEntry*** peaErrors,
																			 GameAccountDataExtract* pExtract)
{
	S32 iRemainingPoints = ItemAssignments_trh_GetRemainingAssignmentPoints(ATR_PASS_ARGS, pEnt);
	if (iRemainingPoints < pDef->iAssignmentPointCost)
	{
		if (peaErrors)
		{
			ItemAssignments_AddFailRequiresEntry(peaErrors, kItemAssignmentFailsRequiresReason_NotEnoughAssignmentPoints, NULL, 0); 
		}
		else
		{
			return kItemAssignmentFailsRequiresReason_NotEnoughAssignmentPoints;
		}
	}
	if (pDef->pRequirements)
	{
		S32 iEntLevel = entity_trh_GetSavedExpLevelLimited(pEnt);
		// Check the level restrictions
		if (iEntLevel < pDef->pRequirements->iMinimumLevel ||
			(pDef->pRequirements->iMaximumLevel && iEntLevel > pDef->pRequirements->iMaximumLevel))
		{
			if (peaErrors)
			{
				S32 iReqLevel;
				const char* pchReasonKey;
				if (iEntLevel < pDef->pRequirements->iMinimumLevel)
				{
					pchReasonKey = "ItemAssignment_MinimumLevel";
					iReqLevel = pDef->pRequirements->iMinimumLevel;
				}
				else
				{
					pchReasonKey = "ItemAssignment_MaximumLevel";
					iReqLevel = pDef->pRequirements->iMaximumLevel;
				}
				ItemAssignments_AddFailRequiresEntry(peaErrors, kItemAssignmentFailsRequiresReason_Level, pchReasonKey, iReqLevel);
			}
			else
			{
				return kItemAssignmentFailsRequiresReason_Level;
			}
		}
		// Check the required numeric
		if (GET_REF(pDef->pRequirements->hRequiredNumeric))
		{
			const char* pchNumeric = REF_STRING_FROM_HANDLE(pDef->pRequirements->hRequiredNumeric);
			S32 iNumericValue = inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, pchNumeric);
			if (iNumericValue < pDef->pRequirements->iRequiredNumericValue)
			{
				if (peaErrors)
				{
					ItemDef *pNumericDef = GET_REF(pDef->pRequirements->hRequiredNumeric);
					const char* pchReasonKey = REF_STRING_FROM_HANDLE(pNumericDef->displayNameMsg.hMessage);
					ItemAssignments_AddFailRequiresEntry(peaErrors, kItemAssignmentFailsRequiresReason_RequiredNumeric, pchReasonKey, pDef->pRequirements->iRequiredNumericValue); 
				}
				else
				{
					return kItemAssignmentFailsRequiresReason_RequiredNumeric;
				}
			}
		}
		// Check to see if the assignment isn't repeatable or is in cooldown
		if (!pDef->bRepeatable || pDef->uCooldownAfterCompletion)
		{
			NOCONST(ItemAssignmentCompleted)* pCompleted = ItemAssignments_trh_PlayerGetCompletedAssignment(pEnt, pDef);
			ItemAssignmentFailsRequiresReason eReason = kItemAssignmentFailsRequiresReason_None;
			U32 uTimeleft = 0;

			if (NONNULL(pCompleted) && !pDef->bRepeatable)
			{
				eReason = kItemAssignmentFailsRequiresReason_AssignmentNonRepeatable;
			}
			else if (NONNULL(pCompleted) && pDef->uCooldownAfterCompletion)
			{
				U32 uCurrentTime = timeServerSecondsSince2000();
				U32 uFinishCooldown = pCompleted->uCompleteTime + pDef->uCooldownAfterCompletion;
				if (uCurrentTime < uFinishCooldown)
				{
					eReason = kItemAssignmentFailsRequiresReason_AssignmentInCooldown;
					uTimeleft = uFinishCooldown - uCurrentTime;
				}
			}
			if (eReason != kItemAssignmentFailsRequiresReason_None)
			{
				if (peaErrors)
				{
					const char* pchReasonKey = REF_STRING_FROM_HANDLE(pDef->msgDisplayName.hMessage);
					ItemAssignments_AddFailRequiresEntry(peaErrors, eReason, pchReasonKey, uTimeleft);
				}
				else
				{
					return eReason;
				}
			}
		}
		// Validate that slot restrictions
		if (!ItemAssignments_trh_ValidateSlots(ATR_PASS_ARGS, pEnt, pDef, pSlots, pExtract))
		{
			if (peaErrors)
			{
				ItemAssignments_AddFailRequiresEntry(peaErrors, kItemAssignmentFailsRequiresReason_InvalidSlots, NULL, 0);
			}
			else
			{
				return kItemAssignmentFailsRequiresReason_InvalidSlots;
			}
		}
	}
	if (!peaErrors || !eaSize(peaErrors))
	{
		return kItemAssignmentFailsRequiresReason_None;
	}
	return kItemAssignmentFailsRequiresReason_Unspecified;
}

AUTO_TRANS_HELPER;
bool ItemAssignments_trh_CheckRequirements(ATR_ARGS, 
										   ATH_ARG NOCONST(Entity)* pEnt, 
										   ItemAssignmentDef* pDef,
										   ItemAssignmentSlots* pSlots,
										   GameAccountDataExtract* pExtract)
{
	return !ItemAssignments_trh_GetFailsRequiresReason(ATR_PASS_ARGS, pEnt, pDef, pSlots, NULL, pExtract);
}

ItemAssignmentSlotUnlockExpression *ItemAssignment_GetUnlockFromKey(int key)
{
	int i;

	if(!g_ItemAssignmentSettings.pStrictAssignmentSlots)
		return NULL;

	for(i=0;i<eaSize(&g_ItemAssignmentSettings.pStrictAssignmentSlots->ppUnlockExpression);i++)
	{
		if(g_ItemAssignmentSettings.pStrictAssignmentSlots->ppUnlockExpression[i]->key == key)
			return g_ItemAssignmentSettings.pStrictAssignmentSlots->ppUnlockExpression[i];
	}

	return NULL;
}

bool ItemAssignments_CheckItemSlotExpression(ItemAssignmentSlotUnlockExpression *pUnlock, Entity *pEnt, ItemAssignmentCompletedDetails *pCompletedAssignment)
{
	bool bReturn = true;
	ItemAssignmentPersistedData *pData = SAFE_MEMBER2(pEnt,pPlayer,pItemAssignmentPersistedData);

	if(pUnlock && pUnlock->pUnlockExpr)
	{
		MultiVal mVal;
		ExprContext* pContext = ItemAssignments_GetContext(pEnt);
		exprEvaluate(pUnlock->pUnlockExpr, pContext, &mVal);
		if (!MultiValGetInt(&mVal,NULL))
		{
			bReturn = false;
		}
	}

	if(pData && pUnlock && pUnlock->pCompletedExpr)
	{
		if(pCompletedAssignment)
		{
			MultiVal mVal;
			ExprContext* pContext = ItemAssignments_GetContextEx(pEnt,pCompletedAssignment,NULL,NULL,NULL,NULL,0,0);
			exprEvaluate(pUnlock->pCompletedExpr,pContext,&mVal);
			if(!MultiValGetInt(&mVal,NULL) && ea32Find(&pData->eaItemAssignmentSlotsUnlocked,pUnlock->key) == -1)
			{
				bReturn = false;
			}
		}
		else if(ea32Find(&pData->eaItemAssignmentSlotsUnlocked,pUnlock->key) == -1)
		{
			bReturn = false;
		}
	}

	return bReturn;
}

bool ItemAssignments_CheckRequirementsExpression(ItemAssignmentDef* pDef, Entity* pEnt) 
{
	if (pEnt && pDef && pDef->pRequirements && pDef->pRequirements->pExprRequires)
	{
		MultiVal mVal;
		ExprContext* pContext = ItemAssignments_GetContext(pEnt);
		exprEvaluate(pDef->pRequirements->pExprRequires, pContext, &mVal);
		if (!MultiValGetInt(&mVal,NULL))
		{
			return false;
		}
	}
	return true;
}

// returns the bag types that are valid to search through
InvBagFlag ItemAssignments_GetSearchInvBagFlags()
{
	InvBagFlag eSearchBags = InvBagFlag_DefaultInventorySearch;
	
	if (g_ItemAssignmentSettings.eExcludeBagFlagsForItemCosts)
	{
		eSearchBags = eSearchBags & ~g_ItemAssignmentSettings.eExcludeBagFlagsForItemCosts;
	}
	return eSearchBags;
}

// Check data that shouldn't change or can't be validated inside of a transaction
// Note: Also calls ItemAssignments_trh_CheckRequirements
ItemAssignmentFailsRequiresReason ItemAssignments_GetFailsRequiresReason(Entity* pEnt, 
																		 ItemAssignmentDef* pDef, 
																		 ItemAssignmentSlots* pSlots,
																		 ItemAssignmentFailsRequiresEntry*** peaErrors)
{
	ItemAssignmentFailsRequiresReason eReason = kItemAssignmentFailsRequiresReason_Unspecified;
	if (pEnt && pDef)
	{
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		
		if (g_ItemAssignmentSettings.pStrictAssignmentSlots)
		{
			if (!ItemAssignments_HasAnyOpenSlots(pEnt, g_ItemAssignmentSettings.pStrictAssignmentSlots))
			{
				if (peaErrors)
				{
					ItemAssignments_AddFailRequiresEntry(peaErrors, kItemAssignmentFailsRequiresReason_NoOpenAssignmentSlot, NULL, 0);
				}
				else
				{
					return kItemAssignmentFailsRequiresReason_NoOpenAssignmentSlot;
				}
			}
		}

		if (pDef->pRequirements)
		{
			GameAccountDataExtract* pExtract;
			// Check the required allegiance
			if (GET_REF(pDef->pRequirements->hRequiredAllegiance))
			{
				if (GET_REF(pEnt->hAllegiance) != GET_REF(pDef->pRequirements->hRequiredAllegiance) && GET_REF(pEnt->hSubAllegiance) != GET_REF(pDef->pRequirements->hRequiredAllegiance))
				{
					if (peaErrors)
					{
						AllegianceDef* pAllegianceDef = GET_REF(pDef->pRequirements->hRequiredAllegiance);
						const char* pchReasonKey = REF_STRING_FROM_HANDLE(pAllegianceDef->displayNameMsg.hMessage);
						ItemAssignments_AddFailRequiresEntry(peaErrors, kItemAssignmentFailsRequiresReason_Allegiance, pchReasonKey, 0);
					}
					else
					{
						return kItemAssignmentFailsRequiresReason_Allegiance;
					}
				}
			}
			// Check the required assignment
			if (GET_REF(pDef->pRequirements->hRequiredAssignment))
			{
				ItemAssignmentDef* pRequiredDef = GET_REF(pDef->pRequirements->hRequiredAssignment);

				if (!ItemAssignments_PlayerGetCompletedAssignment(pEnt, pRequiredDef))
				{
					if (peaErrors)
					{
						const char* pchReasonKey = REF_STRING_FROM_HANDLE(pRequiredDef->msgDisplayName.hMessage);
						ItemAssignments_AddFailRequiresEntry(peaErrors, kItemAssignmentFailsRequiresReason_RequiredAssignment, pchReasonKey, 0);
					}
					else
					{
						return kItemAssignmentFailsRequiresReason_RequiredAssignment;
					}
				}
			}
			// Check the required mission
			if (GET_REF(pDef->pRequirements->hRequiredMission))
			{
				MissionDef* pRequiredMissionDef = GET_REF(pDef->pRequirements->hRequiredMission);
				if (!mission_GetCompletedMissionByDef(mission_GetInfoFromPlayer(pEnt), pRequiredMissionDef))
				{
					if (peaErrors)
					{
						const char* pchReasonKey = NULL;
						if (pRequiredMissionDef)
						{
							pchReasonKey = REF_STRING_FROM_HANDLE(pRequiredMissionDef->displayNameMsg.hMessage);
						}
						ItemAssignments_AddFailRequiresEntry(peaErrors, kItemAssignmentFailsRequiresReason_RequiredMission, pchReasonKey, 0);
					}
					else
					{
						return kItemAssignmentFailsRequiresReason_RequiredMission;
					}
				}
			}
			// Check the item cost
			if (eaSize(&pDef->pRequirements->eaItemCosts))
			{
				InvBagFlag eSearchBags = ItemAssignments_GetSearchInvBagFlags();
				S32 i;

				for (i = 0; i < eaSize(&pDef->pRequirements->eaItemCosts); i++)
				{
					ItemAssignmentItemCost* pItemCost = pDef->pRequirements->eaItemCosts[i];
					ItemDef* pRequiredDef = GET_REF(pItemCost->hItem);
					S32 iCount = pItemCost->iCount;
					if (!pRequiredDef || !iCount)
						continue;

					if ((pRequiredDef->eType == kItemType_Numeric &&
						inv_GetNumericItemValue(pEnt, pRequiredDef->pchName) < iCount) ||
						(pRequiredDef->eType != kItemType_Numeric && 
						 inv_FindItemCountByDefNameEx(pEnt, eSearchBags, pRequiredDef->pchName, iCount) < iCount))
					{
						if (peaErrors)
						{
							const char* pchReasonKey = REF_STRING_FROM_HANDLE(pRequiredDef->displayNameMsg.hMessage);
							ItemAssignments_AddFailRequiresEntry(peaErrors, kItemAssignmentFailsRequiresReason_RequiredItemCost, pchReasonKey, iCount);
						}
						else
						{
							return kItemAssignmentFailsRequiresReason_RequiredItemCost;
						}
					}
				}
			}

			// Check the requires expression. Don't evaluate on the client.
			if (IsServer() && !ItemAssignments_CheckRequirementsExpression(pDef, pEnt))
			{
				if (peaErrors)
				{
					ItemAssignments_AddFailRequiresEntry(peaErrors, kItemAssignmentFailsRequiresReason_RequiresExpr, NULL, 0);
				}
				else
				{
					return kItemAssignmentFailsRequiresReason_RequiresExpr;
				}
			}

			pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			eReason = ItemAssignments_trh_GetFailsRequiresReason(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity, pEnt),pDef,pSlots,peaErrors,pExtract);
		}

		if (!peaErrors || !eaSize(peaErrors))
		{
			return kItemAssignmentFailsRequiresReason_None;
		}
	}
	return eReason;
}

AUTO_TRANS_HELPER;
NOCONST(ItemAssignmentCompleted)* ItemAssignments_trh_PlayerGetCompletedAssignment(ATH_ARG NOCONST(Entity)* pEnt, 
																				    ItemAssignmentDef* pDef)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pItemAssignmentPersistedData))
	{
		return eaIndexedGetUsingString(&pEnt->pPlayer->pItemAssignmentPersistedData->eaCompletedAssignments, pDef->pchName);
	}
	return NULL;
}

ItemAssignmentCompletedDetails* ItemAssignments_PlayerGetRecentlyCompletedAssignment(Entity* pEnt, 
																					 ItemAssignmentDef* pDef, 
																					 const char* pchOutcome)
{
	const char* pchOutcomePooled = allocFindString(pchOutcome);
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pItemAssignmentPersistedData)
	{
		S32 i;
		for (i = eaSize(&pEnt->pPlayer->pItemAssignmentPersistedData->eaRecentlyCompletedAssignments)-1; i >= 0; i--)
		{
			ItemAssignmentCompletedDetails* pDetails;
			pDetails = pEnt->pPlayer->pItemAssignmentPersistedData->eaRecentlyCompletedAssignments[i];
			if (pDef == GET_REF(pDetails->hDef))
			{
				if (!pchOutcomePooled || !pchOutcomePooled[0] || pchOutcomePooled == pDetails->pchOutcome)
				{
					return pDetails;
				}
			}
		}
	}
	return NULL;
}

S32 ItemAssignments_PlayerFindGrantedAssignment(Entity* pEnt, ItemAssignmentDef* pDef)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pItemAssignmentData && pDef)
	{
		return eaIndexedFindUsingString(&pEnt->pPlayer->pItemAssignmentData->eaGrantedPersonalAssignments, pDef->pchName);
	}
	return -1;
}

bool ItemAssignments_PlayerCanAccessAssignments(Entity* pEnt) 
{
	if (!pEnt || !pEnt->pPlayer)
		return false;

	if (g_ItemAssignmentSettings.bDebugOnly && entGetAccessLevel(pEnt) < ACCESS_GM_FULL)
		return false;

	return true;
}

ItemAssignment* ItemAssignments_FindActiveAssignmentWithSlottedItem(const ItemAssignmentPersistedData* pPlayerData, U64 uItemID)
{
	// look for any active assignment that is using this inventory slot
	FOR_EACH_IN_EARRAY(pPlayerData->eaActiveAssignments, ItemAssignment, pAssignment)
	{
		if (pAssignment->pchRewardOutcome)
		{	// this assignment is completed, we need to check the eaRecentlyCompletedAssignments
			FOR_EACH_IN_EARRAY(pPlayerData->eaRecentlyCompletedAssignments, ItemAssignmentCompletedDetails, pCompleted)
			{
				if (pCompleted->uAssignmentID == pAssignment->uAssignmentID)
				{
					FOR_EACH_IN_EARRAY(pCompleted->eaSlottedItemRefs, ItemAssignmentSlottedItemResults, pSlot)
					{
						if (uItemID == pSlot->uItemID)
						{
							return pAssignment;
						}
					}
					FOR_EACH_END
					break;
				}
			}
			FOR_EACH_END
		}
		else
		{
			FOR_EACH_IN_EARRAY(pAssignment->eaSlottedItems, ItemAssignmentSlottedItem, pSlotted)
			{
				if (uItemID == pSlotted->uItemID)
				{
					return pAssignment;
				}
			}
			FOR_EACH_END
		}
		
	}
	FOR_EACH_END

	// we also have to look through the eaRecentlyCompletedAssignments

	return NULL;
}

bool ItemAssignments_CheckDestroyRequirements(ItemDef* pItemDef, ItemAssignmentOutcome* pOutcome)
{
	if (eaiSize(&pOutcome->pResults->peDestroyItemsOfQuality) == 0 && 
		eaiSize(&pOutcome->pResults->peDestroyItemsOfCategory) == 0)
		return false; // no destroy conditions

	if (eaiSize(&pOutcome->pResults->peDestroyItemsOfCategory) > 0)
	{
		S32 i;
		// check if we have the required categories
		for (i = eaiSize(&pOutcome->pResults->peDestroyItemsOfCategory) - 1; i >= 0; --i)
		{
			S32 iCategory = pOutcome->pResults->peDestroyItemsOfCategory[i];
			if (eaiFind(&pItemDef->peCategories, iCategory) < 0)
				return false; // failed to find a required category
		}
	}

	if (eaiSize(&pOutcome->pResults->peDestroyItemsOfQuality) > 0 &&
		eaiFind(&pOutcome->pResults->peDestroyItemsOfQuality, pItemDef->Quality) < 0)
	{	// failed to find an item category
		return false;
	}

	return true;
}

S32 ItemAssignments_GetRankRequiredToUnlockSlot(SA_PARAM_NN_VALID ItemAssignmentSettingsSlots *pStrictSlotSettings, S32 iSlot)
{
	S32 iNumSlots = pStrictSlotSettings->iInitialUnlockedSlots;
	S32 iMaxSlots = pStrictSlotSettings->iMaxActiveAssignmentSlots;

	if (iSlot >= iMaxSlots || iSlot < iNumSlots)
		return 0;

	// assuming pStrictSlotSettings->eaSlotUnlockSchedule is in rank order 
	FOR_EACH_IN_EARRAY_FORWARDS(pStrictSlotSettings->eaSlotUnlockSchedule, ItemAssignmentSlotUnlockSchedule, pSchedule)
		iNumSlots += pSchedule->iNumUnlockedSlots;
		if (iNumSlots > iSlot)
		{
			return pSchedule->iRank;
		}
	FOR_EACH_END

	return 0;
}

AUTO_TRANS_HELPER;
S32 ItemAssignments_trh_GetNumberUnlockedAssignmentSlots(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, 
													 SA_PARAM_NN_VALID ItemAssignmentSettingsSlots *pStrictSlotSettings)
{
	S32 iNumSlots = pStrictSlotSettings->iInitialUnlockedSlots;
	S32 iMaxSlots = pStrictSlotSettings->iMaxActiveAssignmentSlots;
	NOCONST(ItemAssignmentPersistedData) *pData = SAFE_MEMBER2(pEnt,pPlayer,pItemAssignmentPersistedData);

	if (pStrictSlotSettings->pchRankNumeric)
	{
		S32 iRank = inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, pStrictSlotSettings->pchRankNumeric);

		FOR_EACH_IN_EARRAY(pStrictSlotSettings->eaSlotUnlockSchedule, ItemAssignmentSlotUnlockSchedule, pSchedule)
			if (iRank >= pSchedule->iRank)
			{
				iNumSlots += pSchedule->iNumUnlockedSlots;
			}
		FOR_EACH_END
	}

	if (pStrictSlotSettings->pchAdditionalSlotsUnlockedNumeric)
	{
		iNumSlots += inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, pStrictSlotSettings->pchAdditionalSlotsUnlockedNumeric);
	}

	if (pData)
	{
		iNumSlots += ea32Size(&pData->eaItemAssignmentSlotsUnlocked);
	}

	return CLAMP(iNumSlots, 0, iMaxSlots);
}

// returns true if there are any open assignment slots
bool ItemAssignments_HasAnyOpenSlots(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID ItemAssignmentSettingsSlots *pStrictSlotSettings)
{
	S32 iNumSlots;
	S32 iNumAssignments = 0;
	iNumSlots = ItemAssignments_trh_GetNumberUnlockedAssignmentSlots(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), pStrictSlotSettings);

	// see if we have any assignments using this slot
	if (pEnt->pPlayer->pItemAssignmentPersistedData)
	{
		iNumAssignments = eaSize(&pEnt->pPlayer->pItemAssignmentPersistedData->eaActiveAssignments);
	}

	return  iNumSlots > iNumAssignments;
}

// helper function to find out if the iAssignmentSlot given is valid
AUTO_TRANS_HELPER;
bool ItemAssignments_trh_IsValidNewItemAssignmentSlot(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ItemAssignmentSettingsSlots *pStrictSlotSettings, S32 iAssignmentSlot)
{
	S32 iNumSlots;
	if (iAssignmentSlot < 0 || iAssignmentSlot >= pStrictSlotSettings->iMaxActiveAssignmentSlots)
		return false;
		
	iNumSlots = ItemAssignments_trh_GetNumberUnlockedAssignmentSlots(ATR_PASS_ARGS, pEnt, pStrictSlotSettings);

	if (iAssignmentSlot >= iNumSlots)
		return false;	

	// see if we have any assignments using this slot
	if (pEnt->pPlayer->pItemAssignmentPersistedData)
	{
		FOR_EACH_IN_EARRAY(pEnt->pPlayer->pItemAssignmentPersistedData->eaActiveAssignments, NOCONST(ItemAssignment), pAssignment)
		{
			if (iAssignmentSlot == (S32)pAssignment->uItemAssignmentSlot)
				return false;
		}
		FOR_EACH_END
	}

	return true;
}

S32 ItemAssignments_GetExperienceThresholdForRank(SA_PARAM_NN_VALID ItemAssignmentRankingSchedule *pSchedule, S32 iRank)
{
	S32 iNumRanks = eaiSize(&pSchedule->eaiExperience);

	iRank = CLAMP(iRank, 0, iNumRanks-1);

	return eaiGet(&pSchedule->eaiExperience, iRank);
}

S32 ItemAssignments_GetNumRanks(SA_PARAM_NN_VALID ItemAssignmentRankingSchedule *pSchedule)
{
	return eaiSize(&pSchedule->eaiExperience);
}

// searches through the ItemAssignmentPlayerData's various lists of itemAssignments and returns true if found
bool ItemAssignments_HasAssignment(Entity* pEnt, ItemAssignmentDef* pDef, ItemAssignmentPlayerData *pItemAssignmentData)
{
	ContactDialog* pContactDialog = SAFE_MEMBER(pEnt->pPlayer->pInteractInfo, pContactDialog);
	ItemAssignmentDef* pAvailableDef = NULL;
	// make sure we can find that we have this assignment, otherwise we can't start it
	if (pItemAssignmentData)
	{
		S32 i = eaIndexedFindUsingString(&pItemAssignmentData->eaVolumeAvailableAssignments, pDef->pchName);
		if (i >= 0)
			return true;

		FOR_EACH_IN_EARRAY(pItemAssignmentData->eaPersonalAssignmentBuckets, ItemAssignmentPersonalAssignmentBucket, pBucket)
		{
			i = eaIndexedFindUsingString(&pBucket->eaAvailableAssignments, pDef->pchName);
			if (i >= 0)
				return true;
		}
		FOR_EACH_END

			i = eaIndexedFindUsingString(&pItemAssignmentData->eaGrantedPersonalAssignments, pDef->pchName);
		if (i >= 0)
			return true;

		i = eaIndexedFindUsingString(&pItemAssignmentData->eaAutograntedPersonalAssignments, pDef->pchName);
		if (i >= 0)
			return true;
	}

	if (pContactDialog)
	{
		return eaIndexedFindUsingString(&pContactDialog->eaItemAssignments, pDef->pchName) >= 0;
	}

	return false;
}



void ItemAssignments_InitializeIteratorPersonalAssignments(ItemAssignmentPersonalIterator *it)
{
	it->iSubList = 0;
	it->iSubListIdx = 0;
}

ItemAssignmentDefRef* ItemAssignments_IterateOverPersonalAssignments(ItemAssignmentPlayerData* pPlayerData, 
																		ItemAssignmentPersonalIterator *it)
{
	S32 iNumBuckets = eaSize(&pPlayerData->eaPersonalAssignmentBuckets);
	ItemAssignmentDefRef *pDefRef = NULL;
	ItemAssignmentDefRef **eaAvailableAssignmentsList = NULL;

	if (it->iSubList < iNumBuckets)
	{
		ItemAssignmentPersonalAssignmentBucket *pBucket = pPlayerData->eaPersonalAssignmentBuckets[it->iSubList];
		eaAvailableAssignmentsList = pBucket->eaAvailableAssignments;
	}
	else if (it->iSubList == iNumBuckets)
	{
		eaAvailableAssignmentsList = pPlayerData->eaGrantedPersonalAssignments;
	}
	else if (it->iSubList == iNumBuckets + 1)
	{
		eaAvailableAssignmentsList = pPlayerData->eaAutograntedPersonalAssignments;
	}
	else
		return NULL;

	if (!eaAvailableAssignmentsList)
	{
		it->iSubListIdx = 0;
		it->iSubList++;
		return ItemAssignments_IterateOverPersonalAssignments(pPlayerData, it);
	}
	else
	{
		S32 iNumAssignments = eaSize(&eaAvailableAssignmentsList);
		if (it->iSubListIdx >= iNumAssignments)
		{
			it->iSubListIdx = 0;
			it->iSubList++;
			return ItemAssignments_IterateOverPersonalAssignments(pPlayerData, it);
		}

		pDefRef = eaGet(&eaAvailableAssignmentsList, it->iSubListIdx);
		it->iSubListIdx++;
		return pDefRef;
	}
	
	return NULL;
}



bool ItemAssignments_GetForceCompleteNumericCost(Entity *pEnt, ItemAssignment* pAssignment, S32 *piCostToComplete)
{
	if (g_ItemAssignmentSettings.pExprForceCompleteNumericCost)
	{
		U32 uCurrentTime = timeSecondsSince2000();
		S32 iTimeToComplete = pAssignment->uTimeStarted + pAssignment->uDuration - uCurrentTime;
		MultiVal mVal;
		ExprContext* pContext;

		if (iTimeToComplete < 0)
		{
			return false;
		}

		pContext = ItemAssignments_GetContextEx(pEnt, NULL, NULL, NULL, NULL, NULL, 0, iTimeToComplete);
		exprEvaluate(g_ItemAssignmentSettings.pExprForceCompleteNumericCost, pContext, &mVal);
		if (MultiValIsNumber(&mVal))
		{
			*piCostToComplete = MultiValGetInt(&mVal,NULL);
			return true;
		}
	}
	return false;
}


// Searches your active list for uAssignmentID. 
ItemAssignment* ItemAssignment_FindActiveAssignmentByID(ItemAssignmentPersistedData* pPlayerData, U32 uAssignmentID)
{
	S32 i;
	for (i = eaSize(&pPlayerData->eaActiveAssignments) - 1; i >= 0; i--)
	{
		if (pPlayerData->eaActiveAssignments[i]->uAssignmentID == uAssignmentID)
		{
			return pPlayerData->eaActiveAssignments[i];
		}
	}

	return NULL;
}


// Searches your completed list for uAssignmentID. 
ItemAssignmentCompletedDetails* ItemAssignment_FindCompletedAssignmentByID(ItemAssignmentPersistedData* pPlayerData, U32 uAssignmentID)
{
	S32 i;
	for (i = eaSize(&pPlayerData->eaRecentlyCompletedAssignments) - 1; i >= 0; i--)
	{
		if (pPlayerData->eaRecentlyCompletedAssignments[i]->uAssignmentID == uAssignmentID)
		{
			return pPlayerData->eaRecentlyCompletedAssignments[i];
		}
	}

	return NULL;
}

#include "AutoGen/itemAssignments_h_ast.c"