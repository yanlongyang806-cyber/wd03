/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "allegiance.h"
#include "CharacterClass.h"
#include "ItemGenEditor.h"
#include "itemGenCommon.h"
#include "ResourceSearch.h"
#include "StringCache.h"
#include "EString.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "inventoryCommon.h"
#include "Powervars.h"
#include "rand.h"

#include "MultiEditTable.h"
#include "MultiEditWindow.h"

#include "AutoGen/allegiance_h_ast.h"
#include "AutoGen/CharacterClass_h_ast.h"
#include "AutoGen/itemGenCommon_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"

static MEWindow *igeWindow = NULL;

#define IG_GROUP_MAIN "Main"
#define IGEP_GROUP_MAIN        "PowerChoice_IP_Main"
#define IGEP_GROUP_POWER       "PowerChoice_IP_Power"
#define IGEP_SUBGROUP_RESTRICTION "PowerChoice_IP_Restriction"
#define IGEP_GROUP_AI		  "PowerChoice_IP_AI"

#define IGR_GROUP_MAIN			"Rarity_Main"
#define IGR_GROUP_TIER			"Rariry_Tier_Info"
#define IGR_GROUP_TIER_OR		"Rarity_Tier_Overrides"
#define IGR_GROUP_RARITY_MAIN	"Rarity_Info"
#define IGR_GROUP_RARITY_OR		"Rarity_Overrides"
#define IGR_GROUP_POWER			"PowerData"

static int ige_PowerChoiceid = 0;
static int ige_Rarityid = 0;

extern ExprContext *g_pItemContext;

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static char** ie_getPowerTableNames(METable *pTable, void *pUnused)
{
	char **eaPowerTableNames = NULL;

	powertables_FillAllocdNameEArray(&eaPowerTableNames);

	return eaPowerTableNames;
}

static void *ige_createObject(METable *pTable, ItemGenData *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	ItemGenData *pNewDef = NULL;
	const char *pcBaseName;
	char *tmpS = NULL;

	estrStackCreate(&tmpS);

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructCreate(parse_ItemGenData);
		
		StructCopyAll(parse_ItemGenData,pObjectToClone,pNewDef);
		pcBaseName = pObjectToClone->pchDataName;
	} else {
		pNewDef = StructCreate(parse_ItemGenData);

		pcBaseName = "_New_AlgoPet";
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create Item Gen");

	// Assign a new name
	pNewDef->pchDataName = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	estrPrintf(&tmpS, "defs/items/algopets/%s.algopet",pNewDef->pchDataName);
	pNewDef->pchFileName = (char*)allocAddString(tmpS);
	
	// Clear the seed
	pNewDef->uSeed = 0;

	estrDestroy(&tmpS);

	return pNewDef;
}

static void *ige_createPowerData(METable *pTable, ItemGenData *pAlgoDef, ItemGenPowerData *pToClone, ItemGenPowerData *pBefore, ItemGenPowerData *pAfter)
{
	ItemGenPowerData *pNewPowerData = NULL;

	if(pToClone) {
		pNewPowerData = StructClone(parse_ItemGenPowerData, pToClone);
	} else {
		pNewPowerData = StructCreate(parse_ItemGenPowerData);
	}

	if(!pNewPowerData)
		return NULL;

	return pNewPowerData;
}

static void *ige_createRarityData(METable *pTable, ItemGenRarityDefEditor *pDef, ItemGenRarityDefEditor *pToClone, ItemGenRarityDefEditor *pBefore, ItemGenRarityDefEditor *pAfter)
{
	ItemGenRarityDefEditor *pNew = NULL;

	if(pToClone)
		pNew = StructClone(parse_ItemGenRarityDefEditor, pToClone);
	else
		pNew = StructCreate(parse_ItemGenRarityDefEditor);

	if(!pNew)
		return NULL;

	return pNew;
}

static void ige_initColumns(METable *pTable)
{
	METableAddSimpleColumn(pTable, "Name", "dataname", 150, NULL, kMEFieldType_TextEntry);

	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);

	METableAddSimpleColumn(pTable,   "Display Name", ".DisplayNameStr", 160, IG_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable,   "Description", ".DisplayDescStr", 160, IG_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable,   "Short Description", ".DisplayDescShortStr", 160, IG_GROUP_MAIN, kMEFieldType_TextEntry);

	METableAddEnumColumn(pTable, "Powers Suffix Option", "SuffixOption", 100, IG_GROUP_MAIN, kMEFieldType_Combo, ItemGenSuffixEnum);

	METableAddScopeColumn(pTable,    "Data Scope",        "InternalScope",       160, IG_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose

	METableAddFileNameColumn(pTable, "File Name",    "fileName",    210, IG_GROUP_MAIN, NULL, "defs/items/itemgen", "defs/items/itemgen", ".itemgen", UIBrowseNewOrExisting);

	METableAddScopeColumn(pTable,    "Generated Scope",        "Scope",       160, IG_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose

	METableAddScopeColumn(pTable,    "Seed",        "uSeed", 160, IG_GROUP_MAIN, kMEFieldType_TextEntry);

	METableAddEnumColumn(pTable, "Reward Categories", "RewardCategory", 160, IG_GROUP_MAIN, kMEFieldType_FlagCombo, ItemGenRewardCategoryEnum);
	METableAddGlobalDictColumn(pTable, "Reward Costume", "RewardCostume",  100, IG_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "PlayerCostume", "resourceName");
	METableAddGlobalDictColumn(pTable, "Yours Costume", "YoursCostume",  100, IG_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "PlayerCostume", "resourceName");
	METableAddGlobalDictColumn(pTable, "Not Yours Costume", "NotYoursCostume",  100, IG_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "PlayerCostume", "resourceName");
	METableAddGlobalDictColumn(pTable, "Species", "Species",  100, IG_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "Species", "resourceName");

	METableAddEnumColumn(pTable,	"Pickup Type", "PickupType", 150, IG_GROUP_MAIN, kMEFieldType_Combo, RewardPickupTypeEnum);
	METableAddEnumColumn(pTable,	"Reward Launch Type","LaunchType",	120, IG_GROUP_MAIN, kMEFieldType_Combo, RewardLaunchTypeEnum);
	METableAddEnumColumn(pTable,	"Reward Flags",		"RewardFlags",		120, IG_GROUP_MAIN, kMEFieldType_Combo, RewardFlagEnum);
	
	METableAddEnumColumn(pTable,		"Type",			"ItemType",		120, IG_GROUP_MAIN, kMEFieldType_Combo, ItemTypeEnum);
	METableAddEnumColumn(pTable,		"Tag",			"Tag",			120, IG_GROUP_MAIN, kMEFieldType_Combo, ItemTagEnum);
	METableAddEnumColumn(pTable,		"Categories",	"Categories",	140, IG_GROUP_MAIN, kMEFieldType_FlagCombo, ItemCategoryEnum);
	METableAddGlobalDictColumn(pTable,	"ItemSets",		"ItemSet",		160, IG_GROUP_MAIN, kMEFieldType_FlagCombo, "ItemDef", "resourceName");
	METableAddDictColumn(pTable,		"Trainable Nodes","TrainableNode",160, IG_GROUP_MAIN, kMEFieldType_FlagCombo, "PowerTreeNodeDef", parse_PTNodeDef, "NameFull");
	METableAddSimpleColumn(pTable,		"Trainable Node Rank", "TrainableNodeRank", 100, IG_GROUP_MAIN, kMEFieldType_TextEntry);

	METableAddEnumColumn(pTable,     "Restrict Bag",	"Bag",  100, IG_GROUP_MAIN, kMEFieldType_FlagCombo, InvBagIDsEnum);
	METableAddDictColumn(pTable, "Restrict Slot ID", "SlotID", 100, IG_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "InventorySlotIDDef", parse_InventorySlotIDDef, "Key");
	METableAddGlobalDictColumn(pTable, "Subtarget Type", "Subtarget",  100, IG_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "PowerSubtarget", "resourceName");
	METableAddEnumColumn(pTable,     "Flags",        "flags",       140, IG_GROUP_MAIN, kMEFieldType_FlagCombo, ItemDefFlagEnum);

	METableAddSimpleColumn(pTable, "Equip Limit Max Count", ".EquipLimit.MaxEquipCount", 100, IGR_GROUP_POWER, kMEFieldType_TextEntry);
	METableAddEnumColumn(pTable, "Equip Limit Category", ".EquipLimit.Category", 100, IGR_GROUP_POWER, kMEFieldType_Combo, ItemEquipLimitCategoryEnum);

	METableAddSimpleColumn(pTable,   "StackLimit",   "StackLimit",  100, IG_GROUP_MAIN, kMEFieldType_TextEntry);

	METableAddSimpleColumn(pTable, "Icon", "icon", 150, IG_GROUP_MAIN, kMEFieldType_Texture);

	METableAddGlobalDictColumn(pTable, "Item Art", "Art", 120, IG_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "ItemArt", "resourceName");

	METableAddSimpleColumn(pTable, "Durability",       "Durability",      100, IG_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddColumn(pTable,       "Durability Table", "DurabilityTable", 130, IG_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, ie_getPowerTableNames);
	METableAddExprColumn(pTable,   "Economy Points", "ExprEconomyPoints", 140, IG_GROUP_MAIN, g_pItemContext);

	METableAddGlobalDictColumn(pTable, "Allowed Classes", ".Requires.ClassAllowed", 160, IG_GROUP_MAIN, kMEFieldType_FlagCombo, "CharacterClass", "resourceName");
	METableAddEnumColumn(pTable, "Allowed Class Categories", ".Requires.ClassCategoryAllowed", 160, IG_GROUP_MAIN, kMEFieldType_FlagCombo, CharClassCategoryEnum);
	METableAddGlobalDictColumn(pTable, "Required Allegiances", ".Requires.RequiredAllegiance", 160, IG_GROUP_MAIN, kMEFieldType_FlagCombo, "Allegiance", "resourceName");
	METableAddGlobalDictColumn(pTable, "Costumes", "CostumeRefs", 160, IG_GROUP_MAIN, kMEFieldType_FlagCombo, "PlayerCostume", "resourceName");
	METableAddEnumColumn(pTable,     "Costume Mode", "CostumeMode", 100, IG_GROUP_MAIN, kMEFieldType_Combo, kCostumeDisplayModeEnum);

	METableAddSimpleColumn(pTable, "Generate Species Icons", "GenerateSpeciesIcons", 100, IG_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Icon Prefix", "IconPrefix", 60, IG_GROUP_MAIN, kMEFieldType_TextEntry);
}

static void ige_initPowerData(METable *pTable)
{
	int id = 0;

	ige_PowerChoiceid = id = METableCreateSubTable(pTable, "Power", "PowerData", parse_ItemGenPowerData, 
		NULL, NULL, NULL, ige_createPowerData); 


	METableAddSimpleSubColumn(pTable, id, "Power", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Power", ME_STATE_LABEL);

	METableSetNumLockedSubColumns(pTable, id, 3);

	METableAddSimpleSubColumn(pTable, id, "Name", "InternalName", 60, IGR_GROUP_POWER, kMEFieldType_TextEntry);
	METableAddEnumSubColumn(pTable, id, "Rarity", "Rarity", 100, IGR_GROUP_POWER, kMEFieldType_Combo, ItemGenRarityEnum);
	METableAddSimpleSubColumn(pTable, id, "Category", "Category", 100, IGR_GROUP_POWER, kMEFieldType_TextEntry);
	METableAddEnumSubColumn(pTable, id, "Item Categories", "ItemCategory", 100, IGR_GROUP_POWER, kMEFieldType_FlagCombo, ItemCategoryEnum);
	METableAddEnumSubColumn(pTable, id, "Restrict Bags", "RestrictBag", 100, IGR_GROUP_POWER, kMEFieldType_FlagCombo, InvBagIDsEnum);

	METableAddSimpleSubColumn(pTable, id, "Equip Limit Max Count", ".EquipLimit.MaxEquipCount", 100, IGR_GROUP_POWER, kMEFieldType_TextEntry);
	METableAddEnumSubColumn(pTable, id, "Equip Limit Category", ".EquipLimit.Category", 100, IGR_GROUP_POWER, kMEFieldType_Combo, ItemEquipLimitCategoryEnum);

	METableAddSimpleSubColumn(pTable, id, "Tier Min", "TierMin", 100, "PowerData_Rules", kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Tier Max", "TierMax", 100, "PowerData_Rules", kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Cannot Stack", "NoStack", 100, "PowerData_Rules", kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Prefix", ".DisplayPrefix", 140, "PowerData_Display", kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Suffix", ".DisplaySuffix", 140, "PowerData_Display", kMEFieldType_TextEntry);

	METableAddGlobalDictSubColumn(pTable, id, "Item Power", "ItemPower", 160, "PowerData_ItemPower", kMEFieldType_ValidatedTextEntry, "ItemPowerDef", "resourceName");

	// Item power fields
	METableAddSimpleSubColumn(pTable, id,   "Display Name (noun)", ".DisplayName",     160, IGEP_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id,   "Display Name (adj)",  ".DisplayName2",     160, IGEP_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id,  "Description",  ".Description",     160, IGEP_GROUP_MAIN, kMEFieldType_TextEntry);

	METableAddSimpleSubColumn(pTable, id,  "Notes",           "notes",                       150, IGEP_GROUP_MAIN, kMEFieldType_MultiText);

	METableAddSimpleSubColumn(pTable, id,   "Icon",         "icon",						   180, IGEP_GROUP_MAIN, kMEFieldType_Texture);

	METableAddEnumSubColumn(pTable, id,      "Flags",           "flags",                     140, IGEP_GROUP_POWER, kMEFieldType_FlagCombo, ItemPowerFlagEnum);
	METableAddGlobalDictSubColumn(pTable, id, "Power",			  "power",                     140, IGEP_GROUP_POWER, kMEFieldType_ValidatedTextEntry, "PowerDef", "resourceName");
	METableAddGlobalDictSubColumn(pTable, id, "PowerReplace",    "PowerReplace",              140, IGEP_GROUP_POWER, kMEFieldType_ValidatedTextEntry, "PowerReplaceDef", "resourceName");

	METableAddExprSubColumn(pTable, id,      "Economy Points",  "ExprEconomyPoints",         140, IGEP_GROUP_MAIN, g_pItemContext);
	METableAddGlobalDictSubColumn(pTable, id, "Craft Recipe",    "CraftRecipe",               140, IGEP_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddGlobalDictSubColumn(pTable, id, "Value Recipe",    "ValueRecipe",               140, IGEP_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");

	METableAddSimpleSubColumn(pTable, id,  "Min Level",       ".Restriction.MinLevel",       100, IGEP_SUBGROUP_RESTRICTION, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id,  "Max Level",       ".Restriction.MaxLevel",       100, IGEP_SUBGROUP_RESTRICTION, kMEFieldType_TextEntry);
	METableAddExprSubColumn(pTable, id,    "Requires Expr",   ".Restriction.pRequiresBlock", 120, IGEP_SUBGROUP_RESTRICTION, NULL);
	METableAddEnumSubColumn(pTable, id,    "Req UI Category", ".Restriction.UICategory",	 120, IGEP_SUBGROUP_RESTRICTION, kMEFieldType_Combo, UsageRestrictionCategoryEnum);

	METableAddExprSubColumn(pTable, id,    "AI Requires",       ".PowerConfig.AIRequires",          120, IGEP_GROUP_AI, NULL);
	METableAddExprSubColumn(pTable, id,    "AI End Condition",  ".PowerConfig.AIEndCondition",      120, IGEP_GROUP_AI, NULL);
	METableAddSimpleSubColumn(pTable, id, "AI Min Range",      ".PowerConfig.AIPreferredMinRange", 100, IGEP_GROUP_AI, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id,  "AI Max Range",      ".PowerConfig.AIPreferredMaxRange", 100, IGEP_GROUP_AI, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "AI Weight",         ".PowerConfig.AIWeight",            100, IGEP_GROUP_AI, kMEFieldType_TextEntry);
	METableAddExprSubColumn(pTable, id,   "AI Weight Modifier",".PowerConfig.AIWeightModifier",    100, IGEP_GROUP_AI, NULL);
	METableAddSimpleSubColumn(pTable, id, "AI Chain Target",   ".PowerConfig.AIChainTarget",       100, IGEP_GROUP_AI, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "AI Chain Time",     ".PowerConfig.AIChainTime",         100, IGEP_GROUP_AI, kMEFieldType_TextEntry);
	METableAddExprSubColumn(pTable, id,   "AI Chain Requires", ".PowerConfig.AIChainRequires",     120, IGEP_GROUP_AI, NULL);
	METableAddExprSubColumn(pTable, id,   "AI TargetOverride", ".PowerConfig.AITargetOverride",    120, IGEP_GROUP_AI, NULL);
}

static void ige_initRarityData(METable *pTable)
{
	int id = 0;

	ige_Rarityid = id = METableCreateSubTable(pTable, "Rarity", "EditorRarity", parse_ItemGenRarityDefEditor, 
		NULL, NULL, NULL, ige_createRarityData); 

	METableAddSimpleSubColumn(pTable, id, "Rarity", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Rarity", ME_STATE_LABEL);

	METableSetNumLockedSubColumns(pTable, id, 3);

	METableAddSimpleSubColumn(pTable, id, "Tier", ".Tier.Tier", 40, IGR_GROUP_MAIN, kMEFieldType_TextEntry);

	METableAddSimpleSubColumn(pTable, id, "True Level", ".Tier.Level", 40, IGR_GROUP_TIER, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Min Level", ".Tier.LevelMin", 40, IGR_GROUP_TIER, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Max Level", ".Tier.LevelMax", 40, IGR_GROUP_TIER, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Tier Prefix", ".Tier.DisplayPrefix", 140, IGR_GROUP_TIER, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Tier Suffix", ".Tier.DisplaySuffix", 140, IGR_GROUP_TIER, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Tier Icon", ".Tier.Icon", 140, IGR_GROUP_TIER_OR, kMEFieldType_Texture);
	METableAddEnumSubColumn(pTable, id, "Tier Tag", ".Tier.Tag", 120, IG_GROUP_MAIN, kMEFieldType_Combo, ItemTagEnum);
	METableAddEnumSubColumn(pTable, id, "Tier Categories", ".Tier.Categories", 140, IG_GROUP_MAIN, kMEFieldType_FlagCombo, ItemCategoryEnum);
	METableAddGlobalDictSubColumn(pTable, id, "Tier ItemSets", ".Tier.ItemSet", 160, IG_GROUP_MAIN, kMEFieldType_FlagCombo, "ItemDef", "resourceName");
	METableAddDictSubColumn(pTable, id, "Tier Trainable Nodes", ".Tier.TrainableNode", 160, IG_GROUP_MAIN, kMEFieldType_FlagCombo, "PowerTreeNodeDef", parse_PTNodeDef, "NameFull");
	METableAddSimpleSubColumn(pTable, id, "Tier Trainable Node Rank", ".Tier.TrainableNodeRank", 100, IG_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddGlobalDictSubColumn(pTable, id,  "Tier Item Art", ".Tier.Art", 120, IGEP_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "ItemArt", "resourceName");
	METableAddGlobalDictSubColumn(pTable, id, "Tier Costumes", ".Tier.CostumeRefs", 160, IGR_GROUP_TIER_OR, kMEFieldType_FlagCombo, "PlayerCostume", "resourceName");
	METableAddGlobalDictSubColumn(pTable, id, "Tier Subtarget Type", ".Tier.Subtarget",  100, IG_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "PowerSubtarget", "resourceName");
	METableAddSimpleSubColumn(pTable, id, "Tier Durability",       ".Tier.Durability",      100, IG_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSubColumn(pTable, id,      "Tier Durability Table", ".Tier.DurabilityTable", NULL, 130, IG_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, ie_getPowerTableNames);
	METableAddGlobalDictSubColumn(pTable, id, "Allowed Classes", ".Tier.Requires.ClassAllowed", 160, IGR_GROUP_RARITY_OR, kMEFieldType_FlagCombo, "CharacterClass", "resourceName");
	METableAddEnumSubColumn(pTable, id, "Allowed Class Categories", ".Tier.Requires.ClassCategoryAllowed", 160, IGR_GROUP_RARITY_OR, kMEFieldType_FlagCombo, CharClassCategoryEnum);
	METableAddExprSubColumn(pTable, id, "Tier Requires Expr", ".Tier.Requires.pRequiresBlock", 120, IGR_GROUP_TIER, NULL);
	METableAddEnumSubColumn(pTable, id, "Tier Req UI Category", ".Tier.Requires.UICategory", 120, IGR_GROUP_TIER, kMEFieldType_Combo, UsageRestrictionCategoryEnum);
	METableAddGlobalDictSubColumn(pTable, id, "Required Allegiances", ".Tier.Requires.RequiredAllegiance", 160, IGR_GROUP_TIER, kMEFieldType_FlagCombo, "Allegiance", "resourceName");

	METableAddEnumSubColumn(pTable, id, "Type", ".Rarity.Type", 100, IGR_GROUP_RARITY_MAIN, kMEFieldType_Combo, ItemGenRarityEnum);
	METableAddEnumSubColumn(pTable, id, "Choices", ".Rarity.PowerChoice", 140, IGR_GROUP_RARITY_MAIN, kMEFieldType_TextEntry, ItemGenRarityEnum);
	METableAddGlobalDictSubColumn(pTable, id,  "Rarity Reward Costume", ".Rarity.RewardCostume", 120, IGEP_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "PlayerCostume", "resourceName");
	METableAddSimpleSubColumn(pTable, id, "Rarity Icon", ".Rarity.Icon", 140, IGR_GROUP_RARITY_OR,kMEFieldType_Texture);
	METableAddEnumSubColumn(pTable, id, "Rarity Tag", ".Rarity.Tag", 120, IG_GROUP_MAIN, kMEFieldType_Combo, ItemTagEnum);
	METableAddEnumSubColumn(pTable, id, "Rarity Categories", ".Rarity.Categories", 140, IG_GROUP_MAIN, kMEFieldType_FlagCombo, ItemCategoryEnum);
	METableAddGlobalDictSubColumn(pTable, id, "Rarity ItemSets", ".Rarity.ItemSet", 160, IG_GROUP_MAIN, kMEFieldType_FlagCombo, "ItemDef", "resourceName");
	METableAddDictSubColumn(pTable, id, "Rarity Trainable Nodes", ".Rarity.TrainableNode", 160, IG_GROUP_MAIN, kMEFieldType_FlagCombo, "PowerTreeNodeDef", parse_PTNodeDef, "NameFull");
	METableAddSimpleSubColumn(pTable, id, "Rarity Trainable Node Rank", ".Rarity.TrainableNodeRank", 100, IG_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddGlobalDictSubColumn(pTable, id,  "Rarity Item Art", ".Rarity.Art", 120, IGEP_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "ItemArt", "resourceName");
	METableAddEnumSubColumn(pTable, id, "Rarity Quality", ".Rarity.Quality", 100, IGEP_GROUP_MAIN, kMEFieldType_Combo, ItemQualityEnum);
	METableAddEnumSubColumn(pTable, id, "Rarity FlagsToAdd", ".Rarity.FlagsToAdd", 100, IGEP_GROUP_MAIN, kMEFieldType_FlagCombo, ItemDefFlagEnum);
	METableAddEnumSubColumn(pTable, id, "Rarity FlagsToRemove", ".Rarity.FlagsToRemove", 100, IGEP_GROUP_MAIN, kMEFieldType_FlagCombo, ItemDefFlagEnum);
	METableAddGlobalDictSubColumn(pTable, id, "Rarity Costumes", ".Rarity.CostumeRefs", 160, IGR_GROUP_RARITY_OR, kMEFieldType_FlagCombo, "PlayerCostume", "resourceName");
	METableAddGlobalDictSubColumn(pTable, id, "Rarity Subtarget Type", ".Rarity.Subtarget",  100, IG_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "PowerSubtarget", "resourceName");
	METableAddSimpleSubColumn(pTable, id, "Rarity Durability",       ".Rarity.Durability",      100, IG_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSubColumn(pTable, id,      "Rarity Durability Table", ".Rarity.DurabilityTable", NULL, 130, IG_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, ie_getPowerTableNames);
	METableAddSimpleSubColumn(pTable, id, "Rarity Prefix", ".Rarity.DisplayPrefix", 140, IGR_GROUP_TIER, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Rarity Suffix", ".Rarity.DisplaySuffix", 140, IGR_GROUP_TIER, kMEFieldType_TextEntry);

}

static void *ige_windowCreateCallback(MEWindow *pWindow, ItemGenData *pObjectToClone)
{
	return ige_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}

static int ige_validateCallback(METable *pTable, ItemGenData *pItemGen, void *pUserData)
{
	char buf[1024];
	int i;

	if (pItemGen->pchDataName[0] == '_') {
		sprintf(buf, "The Algo Pet '%s' cannot have a name starting with an underscore.", pItemGen->pchDataName);
		ui_DialogPopup("Validation Error", buf);
		return 0;
	}

	if (pItemGen->pEquipLimit &&
		!pItemGen->pEquipLimit->iMaxEquipCount &&
		!pItemGen->pEquipLimit->eCategory)
	{
		StructDestroySafe(parse_ItemEquipLimit, &pItemGen->pEquipLimit);
	}

	for (i = eaSize(&pItemGen->ppPowerData)-1; i >= 0; i--)
	{
		ItemGenPowerData* pPowerData = pItemGen->ppPowerData[i];
		if (pPowerData->pEquipLimit &&
			!pPowerData->pEquipLimit->iMaxEquipCount &&
			!pPowerData->pEquipLimit->eCategory)
		{
			StructDestroySafe(parse_ItemEquipLimit, &pPowerData->pEquipLimit);
		}
	}

	return ItemGenData_Validate(pItemGen);
}

static void ige_FixupCostumesForLoad(ItemCostume** ppCostumes, CostumeRefEditor*** pppCostumeRefs)
{
	int i;
	eaDestroyStruct(pppCostumeRefs, parse_CostumeRefEditor);
	for (i = 0; i < eaSize(&ppCostumes); i++)
	{
		CostumeRefEditor* pCostumeRef = StructCreate(parse_CostumeRefEditor);
		COPY_HANDLE(pCostumeRef->hCostumeRef, ppCostumes[i]->hCostumeRef);
		eaPush(pppCostumeRefs, pCostumeRef);
	}
}

static void ige_postOpenCallback(METable *pTable, ItemGenData *pItemGen, ItemGenData *pOrigItemGen)
{
	int i, j;

	if (pOrigItemGen) {

		if(pOrigItemGen->ppEditorRarity)
			return;

		ige_FixupCostumesForLoad(pItemGen->ppCostumes, &pItemGen->ppCostumeRefs);

		for(i=0;i<eaSize(&pOrigItemGen->ppItemTiers);i++)
		{
			ItemGenTier* pTier = pOrigItemGen->ppItemTiers[i];
			
			ige_FixupCostumesForLoad(pTier->ppCostumes, &pTier->ppCostumeRefs);
			
			for(j=0;j<eaSize(&pTier->ppRarities);j++)
			{
				ItemGenRarityDef* pRarity = pTier->ppRarities[j];
				ItemGenRarityDefEditor *pOrigEditorRarity = StructCreate(parse_ItemGenRarityDefEditor);
				ItemGenRarityDefEditor *pNewEditorRarity = NULL;

				ige_FixupCostumesForLoad(pRarity->ppCostumes, &pRarity->ppCostumeRefs);

				pOrigEditorRarity->pRarityDef = StructClone(parse_ItemGenRarityDef, pRarity);
				pOrigEditorRarity->pTierData = StructClone(parse_ItemGenTier, pTier);

				eaDestroyStruct(&pOrigEditorRarity->pTierData->ppRarities,parse_ItemGenRarityDef);

				eaPush(&pOrigItemGen->ppEditorRarity, pOrigEditorRarity);

				pNewEditorRarity = StructClone(parse_ItemGenRarityDefEditor, pOrigEditorRarity);

				eaPush(&pItemGen->ppEditorRarity, pNewEditorRarity);
			}
		}

		if(pOrigItemGen->ppEditorRarity)
			METableRegenerateRow(pTable,pItemGen);
	}else{
		
	}

	METableRefreshRow(pTable,pItemGen);
}

static void ige_FixupCostumesForSave(CostumeRefEditor** ppCostumeRefs, ItemCostume*** pppCostumes)
{
	int i, j;
	for (i = eaSize(pppCostumes)-1; i >= 0; i--)
	{
		for (j = eaSize(&ppCostumeRefs)-1; j >= 0; j--)
		{
			if (GET_REF((*pppCostumes)[i]->hCostumeRef) == GET_REF(ppCostumeRefs[j]->hCostumeRef))
			{
				break;
			}
		}
		if (j < 0)
		{
			StructDestroy(parse_ItemCostume, eaRemove(pppCostumes, i));
		}
	}
	for (i = eaSize(&ppCostumeRefs)-1; i >= 0; i--)
	{
		for (j = eaSize(pppCostumes)-1; j >= 0; j--)
		{
			if (GET_REF((*pppCostumes)[j]->hCostumeRef) == GET_REF(ppCostumeRefs[i]->hCostumeRef))
			{
				break;
			}
		}
		if (j < 0)
		{
			ItemCostume* pItemCostume = StructCreate(parse_ItemCostume);
			COPY_HANDLE(pItemCostume->hCostumeRef, ppCostumeRefs[i]->hCostumeRef);
			eaPush(pppCostumes, pItemCostume);
		}
	}
}

static void ige_FixupRequiresBlockForSave(ItemGenTier* pTier, ItemGenData* pItemGen)
{
	if (!pTier->pRequires && pItemGen->pRequires)
	{
		pTier->pRequires = StructClone(parse_UsageRestriction, pItemGen->pRequires);
	}
	else if (pItemGen->pRequires)
	{
		if (!pTier->pRequires)
		{
			pTier->pRequires = StructCreate(parse_UsageRestriction);
		}
		if (!eaSize(&pTier->pRequires->ppCharacterClassesAllowed) && 
			 eaSize(&pItemGen->pRequires->ppCharacterClassesAllowed))
		{
			eaCopyStructs(&pItemGen->pRequires->ppCharacterClassesAllowed,
						  &pTier->pRequires->ppCharacterClassesAllowed,
						  parse_CharacterClassRef);
		}
		if (!eaiSize(&pTier->pRequires->peClassCategoriesAllowed) &&
			 eaiSize(&pItemGen->pRequires->peClassCategoriesAllowed))
		{
			eaiCopy(&pTier->pRequires->peClassCategoriesAllowed,
					&pItemGen->pRequires->peClassCategoriesAllowed);
		}
		if (!eaSize(&pTier->pRequires->eaRequiredAllegiances) &&
			 eaSize(&pItemGen->pRequires->eaRequiredAllegiances))
		{
			eaCopyStructs(&pItemGen->pRequires->eaRequiredAllegiances,
						  &pTier->pRequires->eaRequiredAllegiances,
						  parse_AllegianceRef);
		}
	}
}

static void ige_preSaveCallback(METable *pTable, ItemGenData *pItemGen)
{
	int i, j;

	eaDestroyStruct(&pItemGen->ppItemTiers,parse_ItemGenTier);
	ige_FixupCostumesForSave(pItemGen->ppCostumeRefs, &pItemGen->ppCostumes);

	if (!pItemGen->uSeed)
		pItemGen->uSeed = randomU32Seeded(NULL, RandType_Mersenne);

	for(i=0;i<eaSize(&pItemGen->ppEditorRarity);i++)
	{
		ItemGenTier* pTier = NULL;
		ItemGenRarityDefEditor* pRarity = pItemGen->ppEditorRarity[i];
		ItemGenRarityDef* pRarityDef = StructClone(parse_ItemGenRarityDef, pRarity->pRarityDef);

		ige_FixupCostumesForSave(pRarityDef->ppCostumeRefs, &pRarityDef->ppCostumes);
		ige_FixupRequiresBlockForSave(pRarity->pTierData, pItemGen);

		for(j=0;j<eaSize(&pItemGen->ppItemTiers);j++)
		{
			if(pItemGen->ppItemTiers[j]->iTier == pRarity->pTierData->iTier)
			{
				pTier = pItemGen->ppItemTiers[j];
				break;
			}
		}
		if(!pTier)
		{
			pTier = StructClone(parse_ItemGenTier, pRarity->pTierData);

			eaPush(&pItemGen->ppItemTiers, pTier);
		}
		eaPush(&pTier->ppRarities,pRarityDef);
	}

	for (i = 0; i < eaSize(&pItemGen->ppItemTiers); i++)
	{
		ItemGenTier* pTier = pItemGen->ppItemTiers[i];
		ige_FixupCostumesForSave(pTier->ppCostumeRefs, &pTier->ppCostumes);
	}
	StructDestroySafe(parse_UsageRestriction, &pItemGen->pRequires);
}

static void *ige_tableCreateCallback(METable *pTable, ItemGenData *pObjectToClone, bool bCloneKeepsKeys)
{
	return ige_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}

static void ige_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}

static void ige_tierChangeCallback(METable *pTable, ItemGenData *pItemGen, ItemGenRarityDefEditor *pRarity, void *pUserData, bool bInitNotify)
{
	//Tier change, change info
	int iNewTier = pRarity->pTierData->iTier;
	int i;

	if(bInitNotify)
		return;
	

	for(i=eaSize(&pItemGen->ppEditorRarity)-1;i>=0;i--)
	{
		if(pItemGen->ppEditorRarity[i]->pTierData->iTier == iNewTier
			&& pItemGen->ppEditorRarity[i] != pRarity)
		{
			//Copy all existing information
			StructCopyAll(parse_ItemGenTier,pItemGen->ppEditorRarity[i]->pTierData,pRarity->pTierData);
			break;
		}
	}
}

static void ige_tierInfoChangeCallback(METable *pTable, ItemGenData *pItemGen, ItemGenRarityDefEditor *pRarity, void *pUserData, bool bInitNotify)
{
	int iTier = pRarity->pTierData->iTier;
	int i;

	if(bInitNotify)
		return;

	for(i=0;i<eaSize(&pItemGen->ppEditorRarity);i++)
	{
		if(pItemGen->ppEditorRarity[i]->pTierData->iTier == iTier
			&& pItemGen->ppEditorRarity[i] != pRarity)
		{
			StructCopyAll(parse_ItemGenTier,pRarity->pTierData,pItemGen->ppEditorRarity[i]->pTierData);
		}
	}
}

//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void ige_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, ige_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, ige_validateCallback, pTable);
	METableSetPostOpenCallback(pTable, ige_postOpenCallback);
	METableSetPreSaveCallback(pTable, ige_preSaveCallback);
	METableSetCreateCallback(pTable, ige_tableCreateCallback);

	METableSetSubColumnChangeCallback(pTable, ige_Rarityid, "Tier", ige_tierChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, ige_Rarityid, "True Level", ige_tierInfoChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, ige_Rarityid, "Min Level", ige_tierInfoChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, ige_Rarityid, "Max Level", ige_tierInfoChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, ige_Rarityid, "Prefix", ige_tierInfoChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, ige_Rarityid, "Suffix", ige_tierInfoChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, ige_Rarityid, "Tier Icon", ige_tierInfoChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, ige_Rarityid, "Tier Costumes", ige_tierInfoChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, ige_Rarityid, "Tier Subtarget Type", ige_tierInfoChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, ige_Rarityid, "Tier Durability", ige_tierInfoChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, ige_Rarityid, "Tier Durability Table", ige_tierInfoChangeCallback, NULL);


	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_hItemGenDict, ige_dictChangeCallback, pTable);
}
 
static void ige_init(MultiEditEMDoc *pEditorDoc)
{

	if (!igeWindow) {
		// Create the editor window
		igeWindow = MEWindowCreate("Item Gen Editor", "Item Gen", "Item Gens", SEARCH_TYPE_ITEMGEN, g_hItemGenDict, parse_ItemGenData, "DataName", "FileName", "InternalScope", pEditorDoc);

		// Add Itemgen specific columns
		ige_initColumns(igeWindow->pTable);

		// Add Itemgen specific sub-columns
		ige_initPowerData(igeWindow->pTable);
		ige_initRarityData(igeWindow->pTable);

		METableFinishColumns(igeWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(igeWindow);

		// Set the callbacks
		ige_initCallbacks(igeWindow, igeWindow->pTable);
	}

	// Show the window
	ui_WindowPresent(igeWindow->pUIWindow);
}

//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *itemGenEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	ige_init(pEditorDoc);	
	return igeWindow;
}

void itemGenEditor_createItemGen(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = ige_createObject(igeWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(igeWindow->pTable, pObject, 1, 1);
}

#endif