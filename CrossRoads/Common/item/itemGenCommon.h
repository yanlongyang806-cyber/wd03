/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "referencesystem.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "rewardCommon.h"

#include "rewardCommon_h_ast.h"

typedef struct CritterPowerConfig CritterPowerConfig;
typedef struct Expression Expression;
typedef struct RewardTable RewardTable;
typedef enum InvBagIDs InvBagIDs;

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pItemGenRewardCategory);
typedef enum ItemGenRewardCategory
{
	kItemGenRewardCategory_None = 0,
} ItemGenRewardCategory;

AUTO_STRUCT;
typedef struct ItemGenRewardCategoryNames
{
	const char** ppchNames; AST(NAME(RewardCategory))
} ItemGenRewardCategoryNames;

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pItemRarity);
typedef enum ItemGenRarity
{
	ItemGenRarity_Base = 0, ENAMES(Base)
	ItemGenRarity_CodeMax, EIGNORE
}ItemGenRarity;
extern StaticDefineInt ItemGenRarityEnum[];

AUTO_STRUCT;
typedef struct ItemGenRarityData
{
	const char* pchName; AST(STRUCTPARAM)
	F32 fWeight; AST(DEF(1))
	ItemGenRarity eRarityType; NO_AST
} ItemGenRarityData;

AUTO_STRUCT;
typedef struct ItemGenRarityEntry
{
	ItemGenRarity* peRarities; AST(NAME(Rarity))
	S32 iNumChoices; AST(NAME(NumChoices))
} ItemGenRarityEntry;

AUTO_STRUCT;
typedef struct ItemGenMasterRarityTable
{
	const char* pchName; AST(STRUCTPARAM)
	ItemGenRewardCategory eRewardCategory; AST(NAME(RewardCategory))
	EARRAY_OF(ItemGenRarityEntry) eaEntries; AST(NAME(Entry))
} ItemGenMasterRarityTable;

AUTO_STRUCT;
typedef struct ItemGenRaritySettings
{
	EARRAY_OF(ItemGenRarityData) eaData; AST(NAME(Rarity, ItemRarityName))
} ItemGenRaritySettings;

AUTO_STRUCT;
typedef struct ItemGenMasterRarityTableSettings
{
	EARRAY_OF(ItemGenMasterRarityTable) eaMasterTables; AST(NAME(MasterTable))
} ItemGenMasterRarityTableSettings;

AUTO_ENUM;
typedef enum ItemGenSuffix {
	kItemGenSuffix_None = 0, //Don't add any suffix's
	kItemGenSuffix_All, //Add all suffix's
	kItemGenSuffix_BaseOnly, //Add only the suffix's from the base powers
	kItemGenSuffix_ExtendOnly, //Add only the suffix's from everything but the base powers
}ItemGenSuffix;

AUTO_STRUCT;
typedef struct CostumeRefEditor
{
	REF_TO(PlayerCostume) hCostumeRef;	AST(REFDICT(PlayerCostume) NAME(Costume))
} CostumeRefEditor;

AUTO_STRUCT;
typedef struct ItemGenPowerDef
{
	char *pchName;							AST( STRUCTPARAM KEY POOL_STRING )
	char *pchFileName;						AST( CURRENTFILE )
	char *pchScope;							AST( POOL_STRING )
	
	const char* pchDisplayName;				AST(NAME(DisplayName))
	const char* pchDisplayName2;			AST(NAME(DisplayName2))
	const char* pchDescription;				AST(NAME(Description))
	const char *pchIconName;				AST( NAME(Icon) POOL_STRING )
	
	char *pchNotes;	// Designer internal notes
	
	ItemPowerFlag flags;					AST(FLAGS)
	
	Expression *pExprEconomyPoints;			AST(LATEBIND)
	
	REF_TO(PowerDef) hPower;				AST(NAME(Power) REFDICT(PowerDef))
	REF_TO(PowerReplaceDef) hPowerReplace;	AST(NAME(PowerReplace,PowerSlot) REFDICT(PowerReplaceDef))
	
	UsageRestriction *pRestriction;
	
	REF_TO(ItemDef) hCraftRecipe;			AST(REFDICT(ItemDef) NAME(CraftRecipe))
	REF_TO(ItemDef) hValueRecipe;			AST(REFDICT(ItemDef) NAME(ValueRecipe))
	
	CritterPowerConfig *pPowerConfig;		AST (STRUCT(parse_CritterPowerConfig))

	// Obsolete message fields
	DisplayMessage displayNameMsg_Obsolete;	AST(NAME(displayNameMsg) STRUCT(parse_DisplayMessage))  
	DisplayMessage displayNameMsg2_Obsolete; AST(NAME(displayNameMsg2) STRUCT(parse_DisplayMessage))  
	DisplayMessage descriptionMsg_Obsolete;	AST(NAME(descriptionMsg) STRUCT(parse_DisplayMessage)) 

} ItemGenPowerDef;

AUTO_STRUCT;
typedef struct ItemGenPowerData
{
	const char *pchInternalName;	AST(STRUCTPARAM)
	ItemGenPowerDef itemPowerDefData; AST(EMBEDDED_FLAT)
	int iTierMin;
	int iTierMax;

	ItemGenRarity eRarity;			AST(NAME(Rarity) SUBTABLE(ItemGenRarityEnum))
	U32 uiCategory;					AST(NAME(Category))
	ItemCategory* peCategories;		AST(NAME(ItemCategory) SUBTABLE(ItemCategoryEnum))
	InvBagIDs* peRestrictBags;		AST(NAME(RestrictBag) SUBTABLE(InvBagIDsEnum))
	ItemEquipLimit* pEquipLimit;	AST(NAME(EquipLimit))
	bool bNoStack;

	const char* pchDisplaySuffix;	AST(NAME(DisplaySuffix))
	const char* pchDisplayPrefix;	AST(NAME(DisplayPrefix))

	REF_TO(ItemPowerDef) hItemPowerDef; AST(NAME(ItemPower))
	bool bGenerated;				NO_AST

	F32 fScaleFactor;						AST(NAME(ScaleFactor) DEF(1.0))
	bool bGemSlotsAdjustScaleFactor;		AST(NAME(GemSlotsAdjustScaleFactor))

	// Obsolete message fields
	DisplayMessage Suffix_Obsolete;	AST(NAME(Suffix) STRUCT(parse_DisplayMessage))
	DisplayMessage Prefix_Obsolete;	AST(NAME(Prefix) STRUCT(parse_DisplayMessage))
	ItemCostume **ppCostumes;			AST(NAME(Costume))
}ItemGenPowerData;

AUTO_STRUCT;
typedef struct ItemGenGemSlotData
{
	F32 fWeight;			AST(NAME(Weight) DEF(1.0) STRUCTPARAM)
	INT_EARRAY eaTypes;	AST(NAME(SlotType) SUBTABLE(ItemGemTypeEnum))
}ItemGenGemSlotData;

AUTO_STRUCT;
typedef struct ItemGenRarityGenerateAtLevel
{
	int iLevel;							AST(STRUCTPARAM)
	ItemCostume **ppCostumes;			AST(NAME(Costume))
	const char* pchIconName;			AST(NAME(Icon))

}ItemGenRarityGenerateAtLevel;

AUTO_STRUCT;
typedef struct ItemGenRarityDef
{
	ItemGenRarity eRarityType;		AST(NAME(Type) SUBTABLE(ItemGenRarityEnum) STRUCTPARAM KEY)

	U32 *ePowerRarityChoices;		AST(NAME(PowerChoice) SUBTABLE(ItemGenRarityEnum))

	const char* pchDisplaySuffix;	AST(NAME(DisplaySuffix))
	const char* pchDisplayPrefix;	AST(NAME(DisplayPrefix))

	/* Item Def Overrides */
	ItemQuality eQuality;			AST(NAME(Quality) SUBTABLE(ItemQualityEnum))
	ItemDefFlag eFlagsToAdd;		AST(FLAGS SUBTABLE(ItemDefFlagEnum))
	ItemDefFlag eFlagsToRemove;		AST(FLAGS SUBTABLE(ItemDefFlagEnum))
	F32 fDurability;				AST(DEFAULT(-1))	// How much beating can this take, -1 means take the tier value
	const char *pchDurabilityTable;	AST(POOL_STRING) // Optional PowerTable to use to scale the durability using the Item instance's level
	const char *pchIconName;		AST(NAME(Icon))
	ItemTag eItemTag;				AST(NAME(Tag) SUBTABLE(ItemTagEnum))
	ItemCategory *peCategories;		AST(NAME(Categories) SUBTABLE(ItemCategoryEnum))
	ItemDefRef **ppItemSets;		AST(NAME(ItemSet))
	PTNodeDefRef **ppTrainableNodes; AST(NAME(TrainableNode))
	S32 iTrainableNodeRank;			AST(NAME(TrainableNodeRank) DEFAULT(1))
		// This rank applies every node in ppTrainableNodes, and must be done this way due to restrictions with nested lists in editors

	REF_TO(ItemArt) hArt;			AST(NAME(Art) REFDICT(ItemArt))

	REF_TO(PlayerCostume) hRewardCostume;	AST(NAME(RewardCostume) REFDICT(PlayerCostume) )
	
	REF_TO(PowerSubtarget) hSubtarget;	AST(NAME(Subtarget) REFDICT(PowerSubtarget))

	ItemCostume **ppCostumes;
	//Keep a separate array of costume refs for the editor
	CostumeRefEditor **ppCostumeRefs;	AST(NAME(CostumeRefs) NO_TEXT_SAVE)

	ItemGenRarityGenerateAtLevel** eaGenerationLevels;	AST(NAME(GenerateAtLevel))

	ItemGenGemSlotData** eaGemSlotData;	AST(NAME(GemSlotSet))

	// Obsolete message fields
	DisplayMessage Suffix_Obsolete;	AST(NAME(Suffix) STRUCT(parse_DisplayMessage))
	DisplayMessage Prefix_Obsolete;	AST(NAME(Prefix) STRUCT(parse_DisplayMessage))
	S32 iPowerFactor;				AST(NAME(PowerFactor) DEF(-1))

	bool bGenerateWithUnidentifiedWrappers; AST(NAME(GenerateWithUnidentifiedWrappers))
}ItemGenRarityDef;

AUTO_STRUCT;
typedef struct ItemGenTier
{
	int iTier;						AST(KEY STRUCTPARAM)
	int iLevelMin;
	int iLevelMax;
	int iDropWithinLevelDelta;		AST(NAME(DropWithinLevelDelta))
	int iTrueLevel;					AST(NAME(Level))

	ItemGenRarityDef **ppRarities;	AST(NAME(RarityDef))

	UsageRestriction *pRequires;	AST(NAME(Requires))

	const char* pchDisplaySuffix;	AST(NAME(DisplaySuffix))
	const char* pchDisplayPrefix;	AST(NAME(DisplayPrefix))

	/* Item Def Overrides */
	F32 fDurability;					AST(DEFAULT(-1)) // How much beating can this take, -1 means use the gen data value
	const char *pchDurabilityTable;		AST(POOL_STRING) // Optional PowerTable to use to scale the durability using the Item instance's level
	char *pchIconName;					AST(NAME(Icon))
	ItemTag eItemTag;					AST(NAME(Tag) SUBTABLE(ItemTagEnum))
	ItemCategory *peCategories;			AST(NAME(Categories) SUBTABLE(ItemCategoryEnum))

	ItemDefRef **ppItemSets;			AST(NAME(ItemSet))
	PTNodeDefRef **ppTrainableNodes;	AST(NAME(TrainableNode))
	S32 iTrainableNodeRank;				AST(NAME(TrainableNodeRank) DEFAULT(1))
		// This rank applies every node in ppTrainableNodes, and must be done this way due to restrictions with nested lists in editors
	
	REF_TO(ItemArt) hArt;			AST(NAME(Art) REFDICT(ItemArt))

	REF_TO(PowerSubtarget) hSubtarget;	AST(NAME(Subtarget) REFDICT(PowerSubtarget))
	
	ItemCostume **ppCostumes;
	//Keep a separate array of costume refs for the editor
	CostumeRefEditor **ppCostumeRefs;	AST(NAME(CostumeRefs) NO_TEXT_SAVE)	

	// Obsolete message fields
	DisplayMessage Suffix_Obsolete;	AST(NAME(Suffix) STRUCT(parse_DisplayMessage))
	DisplayMessage Prefix_Obsolete;	AST(NAME(Prefix) STRUCT(parse_DisplayMessage))

	S32 iPowerFactor;				AST(NAME(PowerFactor))
	F32 fWeaponDamageVariance;		AST(NAME(WeaponDamageVariance) DEF(-1))

}ItemGenTier;

AUTO_STRUCT;
typedef struct ItemGenRarityDefEditor {
	ItemGenRarityDef *pRarityDef;	AST(NAME(Rarity))
	ItemGenTier *pTierData;			AST(NAME(Tier))
}ItemGenRarityDefEditor;

AUTO_STRUCT;
typedef struct ItemGenData
{
	char *pchDataName;				AST(STRUCTPARAM KEY POOL_STRING)
	char *pchScope;					AST(POOL_STRING DEFAULT(""))
	char *pchInternalScope;         AST(DEFAULT("") POOL_STRING)
	char *pchFileName;				AST(CURRENTFILE SERVER_ONLY)
	ItemGenSuffix eSuffixOption;	AST(NAME(SuffixOption) DEFAULT(kItemGenSuffix_ExtendOnly))
	ItemType eItemType;				AST(NAME(ItemType) SUBTABLE(ItemTypeEnum))
	ItemTag eItemTag;				AST(NAME(Tag) SUBTABLE(ItemTagEnum))
	ItemCategory *peCategories;		AST(NAME(Categories) SUBTABLE(ItemCategoryEnum))
	ItemGenTier **ppItemTiers;		AST(NAME(Tier))
	ItemDefRef **ppItemSets;		AST(NAME(ItemSet))
	PTNodeDefRef **ppTrainableNodes; AST(NAME(TrainableNode))
	S32 iTrainableNodeRank;			AST(NAME(TrainableNodeRank) DEFAULT(1))
	ItemGenPowerData **ppPowerData;	AST(NO_INDEX)
	REF_TO(ItemArt) hArt;			AST(NAME(Art) REFDICT(ItemArt))
	ItemGenRewardCategory *peRewardCategories; AST(NAME(RewardCategory) SUBTABLE(ItemGenRewardCategoryEnum))
	RewardLaunchType eLaunchType;	AST(NAME(LaunchType) SUBTABLE(RewardLaunchTypeEnum))
	RewardPickupType eRewardPickupType; AST(NAME(PickupType) SUBTABLE(RewardPickupTypeEnum))
	REF_TO(PlayerCostume) hRewardCostume;	AST(NAME(RewardCostume) REFDICT(PlayerCostume) )
	REF_TO(PlayerCostume) hNotYoursCostumeRef;	AST(REFDICT(PlayerCostume) NAME(NotYoursCostume))
	REF_TO(PlayerCostume) hYoursCostumeRef;		AST(REFDICT(PlayerCostume) NAME(YoursCostume))
	REF_TO(SpeciesDef) hSpecies;	AST(NAME(Species) REFDICT(Species))

	ItemDamageDef* pItemDamage;		AST(NAME(Damage))

	RewardFlag eRewardFlags;		AST(NAME(RewardFlags) SUBTABLE(RewardFlagEnum))

	const char* pchDisplayName;		AST(NAME(DisplayNameStr))
	const char* pchDisplayDesc;		AST(NAME(DisplayDescStr))
	const char* pchDisplayDescShort; AST(NAME(DisplayDescShortStr))

	ItemDefFlag flags;				AST(FLAGS SUBTABLE(ItemDefFlagEnum))
	InvBagIDs* peRestrictBags;		AST(NAME(bag, bag2) SUBTABLE(InvBagIDsEnum))
	SlotType eRestrictSlotType;		AST(NAME(RestrictSlotType))
	ItemEquipLimit* pEquipLimit;	AST(NAME(EquipLimit))
	REF_TO(InventorySlotIDDef) hSlotID; AST(NAME(SlotID) REFDICT(InventorySlotIDDef))
	int iStackLimit;				AST(DEFAULT(1))
	char *pchIconName;				AST(NAME(Icon, IconName))
	RewardTable **ppRewardTables;	NO_AST
	RewardTable **ppCategoryTables;	NO_AST

	kCostumeDisplayMode eCostumeMode;
	U32 iCostumePriority;

	U32 uSeed; // The assigned seed for this ItemGen

	Expression *pExprEconomyPoints;		AST(LATEBIND)

	F32 fDurability;					// How much beating can this take, 0 means it does not decay
	const char *pchDurabilityTable;		AST(POOL_STRING) // Optional PowerTable to use to scale the durability using the Item instance's level

	REF_TO(PowerSubtarget) hSubtarget;	AST(NAME(Subtarget) REFDICT(PowerSubtarget))

	ItemCostume **ppCostumes;

	bool bGenerateSpeciesIcons; AST(NAME(GenerateSpeciesIcons))
	char *pchIconPrefix; AST(NAME(IconPrefix))
	S32 iIconCount; NO_AST

	//Keep a separate array of costume refs for the editor
	CostumeRefEditor **ppCostumeRefs;			AST(NAME(CostumeRefs) NO_TEXT_SAVE)	
	UsageRestriction *pRequires;				AST(NAME(Requires) NO_TEXT_SAVE)
	ItemGenRarityDefEditor **ppEditorRarity;	AST(NO_TEXT_SAVE)

	// Obsolete fields
	const char** ppchRewardCategories_Obsolete; AST(NAME(RewardCategoryObsolete) POOL_STRING)
	DisplayMessage DisplayName_Obsolete;		AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))
	DisplayMessage DisplayDesc_Obsolete;		AST(NAME(DisplayDesc) STRUCT(parse_DisplayMessage))
	DisplayMessage DisplayDescShort_Obsolete;	AST(NAME(DisplayDescShort) STRUCT(parse_DisplayMessage))
}ItemGenData;

AUTO_STRUCT;
typedef struct ItemGenRef
{
	REF_TO(ItemGenData) hItemGenData; AST(STRUCTPARAM)
} ItemGenRef;

AUTO_STRUCT;
typedef struct ItemGenRefs
{
	EARRAY_OF(ItemGenRef) eaRefs; AST(NAME(ItemGenRef))
} ItemGenRefs;

AUTO_STRUCT;
typedef struct ItemGenItemDefsToSave
{
	ItemDef **ppItemDefs; AST(NAME(ItemDef))
	const char *pchFileName; AST( CURRENTFILE )
}ItemGenItemDefsToSave;

AUTO_STRUCT;
typedef struct ItemGenTextureNames
{
	const char **eapchTextureNames; AST(FORMATSTRING(MAX_ARRAY_SIZE=500000))
} ItemGenTextureNames;

void ItemGenRewardsLoad(void);
S32 ItemGenData_Validate(ItemGenData *pData);
F32 ItemGen_GetWeightFromRarityType(ItemGenRarity eRarityType);

extern ItemGenMasterRarityTableSettings g_ItemGenMasterRarityTableSettings;
extern ItemGenRaritySettings g_ItemGenRaritySettings;
extern DictionaryHandle g_hItemGenDict;
extern DefineContext *g_pItemGenRewardCategory;
extern DefineContext *g_pItemRarity;
extern int g_pItemRarityCount;