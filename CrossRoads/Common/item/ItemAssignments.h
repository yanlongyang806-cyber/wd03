#ifndef ITEMASSIGNMENTS_H
#define ITEMASSIGNMENTS_H

#include "GlobalEnums.h"
#include "itemEnums.h"
#include "Message.h"
#include "MultiVal.h"
#include "referencesystem.h"
#include "WorldLibEnums.h"

typedef struct AllegianceDef AllegianceDef;
typedef struct Entity Entity;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct ExprContext ExprContext;
typedef struct Expression Expression;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct InventoryBag InventoryBag;
typedef struct NOCONST(InventoryBag) NOCONST(InventoryBag);
typedef struct InventorySlot InventorySlot;
typedef struct NOCONST(InventorySlot) NOCONST(InventorySlot);
typedef struct InvRewardRequest InvRewardRequest;
typedef struct NOCONST(ItemAssignment) NOCONST(ItemAssignment);
typedef struct ItemAssignmentDef ItemAssignmentDef;
typedef struct ItemAssignmentDefRef ItemAssignmentDefRef;
typedef struct NOCONST(ItemAssignmentSlottedItem) NOCONST(ItemAssignmentSlottedItem);
typedef struct NOCONST(ItemAssignmentCompleted) NOCONST(ItemAssignmentCompleted);
typedef struct NOCONST(ItemAssignmentCompletedDetails) NOCONST(ItemAssignmentCompletedDetails);
typedef struct Item Item;
typedef struct ItemNumericData ItemNumericData;
typedef struct ItemDef ItemDef;
typedef struct ItemDefRefCont ItemDefRefCont;
typedef struct MissionDef MissionDef;
typedef struct RewardTable RewardTable;

extern StaticDefineInt InvBagIDsEnum[];

#define ITEM_ASSIGNMENT_EXT "itemassign"
#define ITEM_ASSIGNMENT_BASE_DIR "defs/ItemAssignments"

#define ITEM_ASSIGNMENT_SEED_NUM_MAP_BITS 8
#define ITEM_ASSIGNMENT_SEED_NUM_VOLUME_BITS 8
#define ITEM_ASSIGNMENT_SEED_NUM_TIME_BITS 16

AUTO_ENUM;
typedef enum ItemAssignmentFailsRequiresReason
{
	kItemAssignmentFailsRequiresReason_None							= 0,
	kItemAssignmentFailsRequiresReason_Unspecified					= (1 << 0),
	kItemAssignmentFailsRequiresReason_Allegiance					= (1 << 1),
	kItemAssignmentFailsRequiresReason_AssignmentNonRepeatable		= (1 << 2),
	kItemAssignmentFailsRequiresReason_RequiredAssignment			= (1 << 3),
	kItemAssignmentFailsRequiresReason_RequiredMission				= (1 << 4),
	kItemAssignmentFailsRequiresReason_RequiresExpr					= (1 << 5),
	kItemAssignmentFailsRequiresReason_Level						= (1 << 6),
	kItemAssignmentFailsRequiresReason_RequiredNumeric				= (1 << 7),
	kItemAssignmentFailsRequiresReason_RequiredItemCost				= (1 << 8),
	kItemAssignmentFailsRequiresReason_InvalidSlots					= (1 << 9),
	kItemAssignmentFailsRequiresReason_NotEnoughAssignmentPoints	= (1 << 10),
	kItemAssignmentFailsRequiresReason_AssignmentInCooldown			= (1 << 11),
	kItemAssignmentFailsRequiresReason_CantFillSlots				= (1 << 12),
	kItemAssignmentFailsRequiresReason_CantFillUnslottableBag		= (1 << 13),
	kItemAssignmentFailsRequiresReason_CantFillNonremovableItem		= (1 << 14),
	kItemAssignmentFailsRequiresReason_NoOpenAssignmentSlot			= (1 << 15),
} ItemAssignmentFailsRequiresReason;

AUTO_ENUM;
typedef enum ItemAssignmentOperation
{
	kItemAssignmentOperation_None = 0,
	kItemAssignmentOperation_Add,
	kItemAssignmentOperation_Remove,

	kItemAssignmentOperation_Count, EIGNORE
} ItemAssignmentOperation;


AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pItemAssignmentCategories);
typedef enum ItemAssignmentCategory
{
	kItemAssignmentCategory_None, ENAMES(None)
	// Additional categories are data-defined
} ItemAssignmentCategory;


AUTO_STRUCT;
typedef struct ItemAssignmentFailsRequiresEntry
{
	ItemAssignmentFailsRequiresReason eReason;
	const char* pchReasonKey; AST(POOL_STRING)
	S32 iValue;
} ItemAssignmentFailsRequiresEntry;

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pItemAssignmentDurationScaleCategories);
typedef enum ItemAssignmentDurationScaleCategory
{
	kItemAssignmentDurationScaleCategory_None, ENAMES(None)
		// Additional categories are data-defined
} ItemAssignmentDurationScaleCategory;

// Used for scaling numerics over time
AUTO_STRUCT;
typedef struct ItemAssignmentDurationScale
{
	U32 uDurationMin; AST(NAME(DurationMin))
		// The minimum duration 

	U32 uDurationMax; AST(NAME(DurationMax))
		// The maximum duration
	
	F32 fScale; AST(NAME(Scale))
		// The scale to apply to this duration range
} ItemAssignmentDurationScale;

AUTO_STRUCT;
typedef struct ItemAssignmentDurationScaleCategoryData
{
	const char* pchName; AST(STRUCTPARAM)
		// The name of the category

	ItemAssignmentDurationScale** eaDurationScales; AST(NAME(DurationScale))
		// A list of duration-scale associations for this category

	ItemAssignmentDurationScaleCategory eCategory; NO_AST
		// The enum constant associated with this data
} ItemAssignmentDurationScaleCategoryData;

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pItemAssignmentWeights);
typedef enum ItemAssignmentWeightType
{
	kItemAssignmentWeightType_Default, ENAMES(Default)
		// Additional weight enum constants are data-defined
} ItemAssignmentWeightType;

AUTO_STRUCT;
typedef struct ItemAssignmentWeight
{
	const char* pchName; AST(POOL_STRING STRUCTPARAM)
		// Internal name
	
	F32 fWeight; AST(NAME(Value, Weight))
		// Weight value to apply for this 
	
	ItemAssignmentWeightType eWeightType; NO_AST
		// Enum constant associated with this data. Filled in at load time.

	DisplayMessage msgDisplayName; AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))  
		// Display name, if any

	ItemAssignmentCategory eCategory; AST(NAME(AssociatedCategory))
		// associated item assignment category- used for determining if we should generate assignment lists 
		// for the given weight

} ItemAssignmentWeight;

AUTO_STRUCT;
typedef struct ItemAssignmentWeights
{
	ItemAssignmentWeight** eaData; AST(NAME(Weight))
} ItemAssignmentWeights;


AUTO_STRUCT;
typedef struct ItemAssignmentVar
{
	char *pchName; AST(STRUCTPARAM KEY)
		// Name of the variable

	MultiVal mvValue; AST(NAME(Value))
		// MultiVal that defines the variable
} ItemAssignmentVar;

AUTO_STRUCT;
typedef struct ItemAssignmentVars
{
	ItemAssignmentVar** eaVars;	AST(NAME(Var))
		// Array of all ItemAssignmentVars
} ItemAssignmentVars;

AUTO_STRUCT;
typedef struct ItemAssignmentItemCost
{
	REF_TO(ItemDef) hItem; AST(NAME(Item))
		// Required item cost
	
	S32 iCount; AST(NAME(Count) DEFAULT(1))
		// The count of items used as a cost
} ItemAssignmentItemCost;

AUTO_STRUCT AST_IGNORE(hRequiredItemCost) AST_IGNORE(iRequiredItemCostCount);
typedef struct ItemAssignmentRequirements
{
	S32 iMinimumLevel; AST(NAME(MinimumLevel) DEFAULT(1))
		// Required minimum level
	
	S32 iMaximumLevel; AST(NAME(MaximumLevel))
		// Required maximum level
	
	const char** ppchRequiredMaps; AST(NAME(RequiredMap) POOL_STRING)
		// The player must be on one of these maps to get access to this assignment

	ZoneMapType* peRequireMapTypes; AST(NAME(RequiredMapType) SUBTABLE(ZoneMapTypeEnum))
		// The player must be on one of these map types to get access to this assignment

	WorldRegionType* peRequiredRegionTypes; AST(NAME(RequiredRegionType) SUBTABLE(WorldRegionTypeEnum))
		// The player must be on one of these region types to get access to this assignment

	const char** ppchRequiredVolumes; AST(NAME(RequiredVolume) POOL_STRING)
		// The player must be in one of these volumes to get access to this assignment
	
	REF_TO(AllegianceDef) hRequiredAllegiance; AST(NAME(RequiredAllegiance))
		// Required allegiance
	
	REF_TO(ItemDef) hRequiredNumeric; AST(NAME(RequiredNumericItem))
		// Required numeric item
	
	S32 iRequiredNumericValue; AST(NAME(RequiredNumericValue))
		// Required numeric value

	ItemAssignmentItemCost** eaItemCosts; AST(NAME(ItemCost))
		// List of items that will be removed from the player's inventory

	REF_TO(MissionDef) hRequiredMission; AST(NAME(RequiredMission))
		// Required mission
	
	REF_TO(ItemAssignmentDef) hRequiredAssignment; AST(NAME(RequiredItemAssignment))
		// Requires that the player has completed an item assignment
	
	Expression *pExprRequires; AST(NAME(ExprBlockRequires,RequiresBlock), REDUNDANT_STRUCT(ExprRequires, parse_Expression_StructParam), LATEBIND)
		// Requires expression		
} ItemAssignmentRequirements;


AUTO_STRUCT;
typedef struct ItemAssignmentOutcomeResults
{
	REF_TO(RewardTable) hRewardTable; AST(NAME(GrantRewardTable))
		// Grant this reward table
	
	REF_TO(ItemAssignmentDef) hNewAssignment; AST(NAME(NewAssignment))
		// Immediately start this assignment

	F32 fNewAssignmentChance; AST(NAME(NewAssignmentChance) DEFAULT(1))
		// The chance [0..1] for each slotted item to be put on a new assignment
	
	ItemQuality* peDestroyItemsOfQuality; AST(NAME(DestroyItemsOfQuality) SUBTABLE(ItemQualityEnum))
		// Destroy all items of the specified qualities

	ItemCategory* peDestroyItemsOfCategory; AST(NAME(DestroyItemsOfCategory) SUBTABLE(ItemCategoryEnum))
		// Destroy all items of the specified categories, all categories must be present

	F32 fDestroyChance; AST(NAME(DestroyChance) DEFAULT(1))
		// The chance [0..1] for each slotted item to be destroyed

	DisplayMessage msgNewAssignmentDescription; AST(NAME(NewAssignmentDescription) STRUCT(parse_DisplayMessage))
		// The message explaining the new assignment

	DisplayMessage msgDestroyDescription; AST(NAME(DestroyDescription) STRUCT(parse_DisplayMessage))
		// The message explaining why the item was destroyed

	InvRewardRequest *pSampleRewards; AST(NO_TEXT_SAVE)
		// sample rewards for this outcome. This might not always be populated.

	
} ItemAssignmentOutcomeResults;

AUTO_STRUCT;
typedef struct ItemAssignmentOutcome
{
	const char* pchName; AST(STRUCTPARAM POOL_STRING)
		// Internal name
	
	DisplayMessage msgDisplayName; AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))  
		// Display name

	DisplayMessage msgDescription; AST(NAME(Description) STRUCT(parse_DisplayMessage))  
		// Description message
	
	ItemAssignmentWeightType eBaseWeight; AST(NAME(BaseWeight))
		// The base weight of this outcome
	
	ItemAssignmentOutcomeResults* pResults; AST(NAME(Results))
		// The results of this outcome

	Expression *pExprScaleAllNumerics; AST(NAME(ExprBlockScaleAllNumerics,ScaleAllNumericRewards), REDUNDANT_STRUCT(ExprScaleAllNumerics, parse_Expression_StructParam), LATEBIND)
		// Scale all numeric rewards by the result of this expression
} ItemAssignmentOutcome;

AUTO_STRUCT;
typedef struct ItemAssignmentSlot
{
	ItemCategory* peRequiredItemCategories; AST(NAME(RequiredItemCategory) SUBTABLE(ItemCategoryEnum))
		// Require items to have one of the following item categories to be slotted
	
	ItemCategory* peRestrictItemCategories; AST(NAME(RestrictItemCategory) SUBTABLE(ItemCategoryEnum))
		// Don't allow items to be slotted if they have any of the following categories

	const char** ppchOutcomeModifiers; AST(NAME(OutcomeModifier) POOL_STRING)
		// Modifiers applied to this slot

	const char* pchIcon; AST(NAME(Icon) POOL_STRING)
		// The icon to display for the slot

	U32 bIsOptional : 1;
		// if set this assignment slot is optional. The assignment can be started without this slot being occupied

} ItemAssignmentSlot;

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pItemAssignmentOutcomeModifierTypes);
typedef enum ItemAssignmentOutcomeModifierType
{
	kItemAssignmentOutcomeModifierType_None = 0,
} ItemAssignmentOutcomeModifierType;

// Modifier type data
AUTO_STRUCT;
typedef struct ItemAssignmentOutcomeModifierTypeData
{
	const char* pchName; AST(NAME(Name) STRUCTPARAM)
		// Name of the modifier type

	Expression* pExprWeightModifier;	AST(NAME(ExprBlockWeightModifier,WeightModifier), REDUNDANT_STRUCT(ExprWeightModifer, parse_Expression_StructParam), LATEBIND)
		// An expression that modifies the weight of the outcome

	const char** ppchAffectedOutcomes;	AST(NAME(AffectedOutcome) POOL_STRING)
		// The outcomes affected by this modifier

	const char* pchDependentOutcome; AST(NAME(DependentOutcome) POOL_STRING)
		// If this is set, then any modifications to affected outcomes will also modify the dependent outcome

	S32 eType; NO_AST
		// The type value. Set at load-time.
} ItemAssignmentOutcomeModifierTypeData;

AUTO_STRUCT;
typedef struct ItemAssignmentOutcomeModifierTypes
{
	ItemAssignmentOutcomeModifierTypeData** eaData; AST(NAME(ModifierType))
} ItemAssignmentOutcomeModifierTypes;

// Modifies the weight of an assignment outcome
AUTO_STRUCT AST_IGNORE(WeightAdjustment) AST_IGNORE(AffectedOutcome);
typedef struct ItemAssignmentOutcomeModifier
{
	const char* pchName; AST(STRUCTPARAM POOL_STRING)
		// The internal name of this modifier

	ItemAssignmentOutcomeModifierType eType; AST(NAME(ModifierType))
		// Outcome modifier type

	ItemCategory* peItemCategories; AST(NAME(ItemCategory) SUBTABLE(ItemCategoryEnum))
		// The item categories that receive this modifier. Empty means use all categories.
} ItemAssignmentOutcomeModifier;


// ItemAssignment definition
AUTO_STRUCT;
typedef struct ItemAssignmentDef
{
	const char* pchName; AST(KEY STRUCTPARAM POOL_STRING)
		// The internal name of the item assignment
	
	const char* pchFileName; AST(CURRENTFILE)
		// Filename

	const char* pchScope; AST(POOL_STRING)
		// Scope

	const char* pchIconName; AST(NAME(Icon) POOL_STRING)
		// Optional override icon for the category icon

	const char* pchImage; AST(NAME(Image) POOL_STRING)
		// A larger image to display

	const char* pchFeaturedActivity; AST(NAME(FeaturedActivity) POOL_STRING)
		// If this activity is active, then enable special features for this assignment

	DisplayMessage msgDisplayName; AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))  
		// Display name message

	DisplayMessage msgDescription; AST(NAME(Description) STRUCT(parse_DisplayMessage))  
		// Description message

	DisplayMessage msgAssignmentChainDisplayName; AST(NAME(AssignmentChainDisplayName) STRUCT(parse_DisplayMessage))
		// The display name for this assignment chain

	DisplayMessage msgAssignmentChainDescription; AST(NAME(AssignmentChainDescription) STRUCT(parse_DisplayMessage))
		// The description for an assignment chain

	ItemAssignmentCategory eCategory; AST(NAME(Category))
		// The item assignment category that this belongs to
	
	U32 uDuration; AST(NAME(Duration))
		// The amount of time it takes for this assignment to finish
	
	S32 iMinimumSlottedItems; AST(NAME(MinimumSlottedItems) DEFAULT(1))
		// The minimum number of items that are required to be slotted in order to start this assignment
	
	S32 iAssignmentPointCost; AST(NAME(AssignmentPointCost) DEFAULT(1))
		// The number of assignment points the player must have available in order to make this an active assignment

	U32 uCooldownAfterCompletion; AST(NAME(CooldownAfterCompletion))
		// The amount of time that the player must wait after completion to start this assignment again

	ItemAssignmentWeightType eWeight; AST(NAME(Weight))
		// How often this assignment will be chosen to go in the player's list of assignments
	
	ItemAssignmentRequirements* pRequirements; AST(NAME(Requirements))
		// The various gating factors for getting access to this assignment
	
	ItemAssignmentOutcome** eaOutcomes; AST(NAME(Outcome))
		// Defines the possible outcomes of this assignment

	S32 iCompletionExperience; AST(NAME(CompletionExperience))
		// if greater than zero will award experience based on the category's pchNumericXP1 (currently not pchNumericXP2)
		
	ItemAssignmentSlot** eaSlots; AST(NAME(Slot))
		// Assignment slots

	ItemAssignmentOutcomeModifier** eaModifiers; AST(NAME(OutcomeModifier))
		// Rules for modifying outcome weights

	ItemAssignmentDurationScaleCategory eNumericDurationScaleCategory; AST(NAME(NumericDurationScaleCategory))
		// The duration scale to apply to numeric rewards
	
	S32 iUniqueAssignmentCount;
		// the maximum number of concurrent assignments of this type that are allowed running. It does not include those that are completed

	S32 iSortOrder;
		// usable by the UI to sort assignments

	U32 bIsAbortable : 1; AST(NAME(IsAbortable))
		// Whether or not this assignment can be cancelled

	U32 bDisabled : 1; AST(NAME(Disabled))
		// Internal flag to disable this assignment from being shown to players

	U32 bAllowItemUnslotting : 1; AST(NAME(AllowItemUnslotting))
		// Allow players to unslot items from this assignment

	U32 bRepeatable : 1; AST(NAME(Repeatable) DEFAULT(1))
		// Whether or not this assignment can be repeated

	U32 bCanStartRemotely : 1; AST(NAME(CanStartRemotely))
		// Whether or not this assignment can be started without having access to it in a map list or the player's personal list

	U32 bHasOptionalSlots : 1; AST(NO_TEXT_SAVE)
		// set if this itemAssignment has any slots tagged as optional

	U32 bHasRequiredSlots : 1; AST(NO_TEXT_SAVE)
		// set if this itemAssignment has any slots not tagged as optional
		
	ItemAssignmentDefRef** eaDependencies; AST(NO_TEXT_SAVE)
		// List of assignments that require this assignment in order to be started
		//  Derived at load time
} ItemAssignmentDef;

AUTO_STRUCT;
typedef struct ItemAssignmentDefRef
{
	REF_TO(ItemAssignmentDef) hDef; AST(STRUCTPARAM KEY)
	U32 bFeatured : 1;
	U32 bDirty : 1;
} ItemAssignmentDefRef;

AUTO_STRUCT AST_CONTAINER;
typedef struct ItemAssignmentDefRefCont
{
	CONST_REF_TO(ItemAssignmentDef) hDef; AST(KEY PERSIST SUBSCRIBE)
} ItemAssignmentDefRefCont;

AUTO_STRUCT;
typedef struct ItemAssignmentDefRefs
{
	ItemAssignmentDefRef** eaRefs;
} ItemAssignmentDefRefs;

AUTO_STRUCT;
typedef struct ItemAssignmentCategorySettings
{
	const char* pchName; AST(STRUCTPARAM)
		// Internal name of the category
	
	DisplayMessage msgDisplayName; AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))  
		// Display name

	const char* pchIconName; AST(NAME(Icon) POOL_STRING)
		// Icon

	const char* pchImage; AST(NAME(Image) POOL_STRING)
		// A larger image to display

	const char* pchNumericXP1; AST(NAME(NumericXP1) ADDNAMES(NumericXP) POOL_STRING)
		// The numeric used for XP in the UI

	const char* pchNumericXP2; AST(ADDNAMES(NumericXP2) POOL_STRING)
		// The second numeric used for XP in the UI

	const char* pchNumericRank1; AST(NAME(NumericRank1) ADDNAMES(NumericRank) POOL_STRING)
		// The numeric used for rank in the UI

	const char* pchNumericRank2; AST(ADDNAMES(NumericRank2) POOL_STRING)
		// The second numeric used for rank in the UI

	ItemCategory *peAssociatedItemCategories;	AST(NAME(AssociatedItemCategories) SUBTABLE(ItemCategoryEnum))
		//  an optional list of itemCategories that are associated with this ItemAssignmentCategory for search filter purposes

	S32 iSortOrder;
		// usable by the UI to sort assignments

	ItemAssignmentCategory eCategory; NO_AST
		// Filled in at load-time

	Expression* pExprIsCategoryHidden;	AST(NAME(ExprBlockIsCategoryHidden), REDUNDANT_STRUCT(IsCategoryHidden, parse_Expression_StructParam), LATEBIND)
		// if the expression returns true, then the category is hidden from the given user
		
} ItemAssignmentCategorySettings;

AUTO_STRUCT;
typedef struct ItemAssignmentCategorySettingsStruct
{
	ItemAssignmentCategorySettings** eaCategories; AST(NAME(Category))
} ItemAssignmentCategorySettingsStruct;

AUTO_STRUCT;
typedef struct ItemAssignmentRankingSchedule
{
	S32* eaiExperience;											AST(NAME(Experience))
		// the XP values that will cause a new rank

	const char* pchNumericXP;									AST(NAME(NumericXP) POOL_STRING)
		// numeric used for tracking XP. Only used for the meta leveling schedule

	const char* pchNumericRank;									AST(NAME(NumericRank) POOL_STRING)
		// numeric used for tracking rank. Only used for the meta leveling schedule

} ItemAssignmentRankingSchedule;

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pItemAssignmentRarityCountTypes);
typedef enum ItemAssignmentRarityCountType
{
	kItemAssignmentRarityCountType_None, ENAMES(None)
		// Additional categories are data-defined
} ItemAssignmentRarityCountType;

AUTO_STRUCT;
typedef struct ItemAssignmentRarityCount
{
	const char* pchName; AST(STRUCTPARAM)
		// The name of this rarity count

	ItemAssignmentWeightType* peWeights; AST(NAME(Weight))
		// A list of weights
	
	S32 iAssignmentCount; AST(NAME(AssignmentCount))
		// The number of assignments to show for the specified weights

	S32 iFeaturedAssignmentCount; AST(NAME(FeaturedAssignmentCount))
		// The number of featured assignments to show
	
	S32 eType; NO_AST
		// The enum constant associated with this data. Filled in at load-time.

} ItemAssignmentRarityCount;

AUTO_STRUCT;
typedef struct ItemAssignmentRarityCounts
{
	ItemAssignmentRarityCount** eaRarityCounts;	AST(NAME(RarityCount))
		// Array of all ItemAssignmentVars
} ItemAssignmentRarityCounts;

AUTO_STRUCT;
typedef struct ItemAssignmentActivityRefs
{
	const char* pchActivity; AST(NAME(Activity) KEY POOL_STRING)
		// The activity name

	ItemAssignmentDefRef** eaRefs;
		// A list of refs that match the specified weight
} ItemAssignmentActivityRefs;

AUTO_STRUCT;
typedef struct ItemAssignmentRefsInRarity
{
	ItemAssignmentWeightType eWeight; AST(STRUCTPARAM)
		// The weight

	ItemAssignmentActivityRefs** eaActivityRefs;
		// A list of refs for each activity
	
	ItemAssignmentDefRef** eaRefs;
		// A list of refs that match the specified weight

	ItemAssignmentDefRef** eaFeaturedRefs;
		// List of currently featured assignments. Used temporarily for assignment list generation

	ItemAssignmentDefRef** eaStandardRefs;
		// List of standard assignments. Used temporarily for assignment list generation
} ItemAssignmentRefsInRarity;

AUTO_STRUCT;
typedef struct ItemAssignmentMapSettings
{
	const char* pchMapName; AST(POOL_STRING STRUCTPARAM)
		// The name of the map
	
	ItemAssignmentRarityCountType* peRarityCounts; AST(NAME(RarityCount) POOL_STRING)
		// How many assignments to show for various rarities, overrides global settings
} ItemAssignmentMapSettings;

AUTO_STRUCT;
typedef struct ItemAssignmentOutcomeWeight
{
	const char* pchOutcome; AST(STRUCTPARAM POOL_STRING KEY)
		// The outcome name

	ItemAssignmentWeightType eWeight; AST(NAME(Value, Weight))
		// The weight for this outcome
} ItemAssignmentOutcomeWeight;

AUTO_STRUCT;
typedef struct ItemAssignmentQualityWeight
{
	ItemQuality eQuality; AST(STRUCTPARAM SUBTABLE(ItemQualityEnum) KEY)
		// The quality

	ItemAssignmentWeightType eWeight; AST(NAME(Value, Weight))
		// The default weight to associate with the specified quality

	ItemAssignmentOutcomeWeight** eaOutcomes; AST(NAME(Outcome))
		// Outcome weights to associate with the specified quality
} ItemAssignmentQualityWeight;

AUTO_STRUCT;
typedef struct ItemAssignmentQualityDurationScale
{
	ItemQuality eQuality; AST(STRUCTPARAM SUBTABLE(ItemQualityEnum) KEY)
	
	F32 fScale;	AST(NAME(Scale))	

} ItemAssignmentQualityDurationScale;

AUTO_STRUCT;
typedef struct ItemAssignmentQualityNumericScale
{
	ItemQuality eQuality; AST(STRUCTPARAM SUBTABLE(ItemQualityEnum) KEY)
		// The quality

	F32 fScale;	AST(NAME(Scale))	
		// The scale to apply to this quality
} ItemAssignmentQualityNumericScale;

AUTO_STRUCT;
typedef struct ItemAssignmentSlotUnlockSchedule
{
	S32 iRank;
		// the rank this applies to. refers to the pchRankNumeric on the ItemAssignmentSettingsSlots

	S32 iNumUnlockedSlots;
		// the number of slots that are unlocked when this rank is reached

} ItemAssignmentSlotUnlockSchedule;

AUTO_STRUCT;
typedef struct ItemAssignmentSlotUnlockExpression
{
	int key;								AST(KEY STRUCTPARAM)
	DisplayMessage displayReason;			AST(NAME(Reason) STRUCT(parse_DisplayMessage))
	Expression *pUnlockExpr;				AST(NAME("UnlockExpressionBlock"), REDUNDANT_STRUCT("UnlockExpression", parse_Expression_StructParam), LATEBIND)
	Expression *pCompletedExpr;				AST(NAME("CompletedExpressionBlock"), REDUNDANT_STRUCT("CompletedExpression", parse_Expression_StructParam), LATEBIND)
}ItemAssignmentSlotUnlockExpression;

AUTO_STRUCT;
typedef struct ItemAssignmentSettingsSlots
{
	S32 iMaxActiveAssignmentSlots;
		// the maximum number of active assignments that can be potentially started

	S32 iInitialUnlockedSlots;
		// the number of slots that are unlocked to start

	const char* pchRankNumeric;
		// the numeric we look at to determine what slots are unlocked
	
	ItemAssignmentSlotUnlockSchedule	**eaSlotUnlockSchedule;

	const char* pchAdditionalSlotsUnlockedNumeric;
		// another way of determining how many slots are unlocked.

	const char** ppchPerkUnlockSlots;				AST(NAME(PerkUnlockSlots), POOL_STRING)
		// a list of perks that will unlock the slots. Used for UI displaying what will unlock an individual slot. 

	ItemAssignmentSlotUnlockExpression** ppUnlockExpression;	AST(NAME(UnlockExpression))
		// a list of expressions that will unlock the slots.
	
} ItemAssignmentSettingsSlots;


AUTO_STRUCT;
typedef struct ItemAssignmentPersonalAssignmentSettings
{
	ItemAssignmentRarityCountType* peRarityCounts; AST(NAME(RarityCounts))
		// list of rarities to include in this bucket

	U32 uAssignmentRefreshTime; AST(NAME(AssignmentRefreshTime) DEFAULT(14400))
		// How often (in seconds) assignments are refreshed on participating GameServers

} ItemAssignmentPersonalAssignmentSettings;

AUTO_STRUCT;
typedef struct ItemAssignmentOutcomeWeightWindowConfig
{
	F32 fStartingWeight;
		// What the weight starts at. 

	F32 fWeightPerOutcome;
		// if set, each weight is weighed evenly. 

	F32 fWeightChanceWindow;
		// the window's range into the linear weighted outcomes
	
	ItemCategory* peWeightedItemCategories; AST(NAME(WeightedItemCategories) SUBTABLE(ItemCategoryEnum))
		// categories that count as effecting weight

} ItemAssignmentOutcomeWeightWindowConfig;

// Global item assignment settings
AUTO_STRUCT;
typedef struct ItemAssignmentSettings
{
	U32 uAssignmentRefreshTime;	AST(NAME(AssignmentRefreshTime) DEFAULT(14400))
		// How often (in seconds) assignments are refreshed on participating GameServers

	U32 uPersonalAssignmentRefreshTime; AST(NAME(PersonalAssignmentRefreshTime) DEFAULT(14400))
		// How often (in seconds) personal assignments are refreshed on participating GameServers
	
	const char** ppchValidVolumes; AST(NAME(Volume) POOL_STRING)
		// List of volumes that the player must enter to show item assignments

	ItemAssignmentRarityCountType* peGlobalMapRarityCounts; AST(NAME(GlobalMapRarityCount))
		// Global rarity settings for all maps in eaMapSettings

	ItemAssignmentRarityCountType* pePersonalRarityCounts; AST(NAME(PersonalRarityCount))
		// Personal assignment rarity counts, not valid if eaPersonalAssignmentSettings is set

	ItemAssignmentWeightType* peAutograntAssignmentWeights; AST(NAME(AutograntAssignmentWeights) SUBTABLE(ItemAssignmentWeightTypeEnum))
		// any assignments matching the given weights will be autogranted to the player

	ItemAssignmentPersonalAssignmentSettings **eaPersonalAssignmentSettings; AST(NAME(PersonalAssignmentSettings))
		// settings for a variable list of personal assignments, each having different rarity counts and refresh times
		// if this is not defined, uPersonalAssignmentRefreshTime and pePersonalRarityCounts will be used to populate this
		
	ItemAssignmentMapSettings** eaMapSettings; AST(NAME(Map))
		// List of valid maps with associated category data

	ItemAssignmentQualityWeight** eaQualityWeights; AST(NAME(QualityWeight))
		// A list of quality-weight associations

	ItemAssignmentQualityDurationScale** eaDurationScales;	AST(NAME(DurationScale))
		// a list of quality to duration scales

	ItemAssignmentDurationScaleCategoryData** eaDurationScaleCategories; AST(NAME(DurationScaleCategory))
		// A list of duration scale categories to apply to reward numerics

	ItemAssignmentQualityNumericScale** eaQualityScales; AST(NAME(NumericQualityScale, QualityScale))
		// A list of item quality scales to apply to reward numerics

	Expression* pExprDurationModifier;	AST(NAME(ExprBlockDurationModifier,DurationModifier), REDUNDANT_STRUCT(ExprDurationModifer, parse_Expression_StructParam), LATEBIND)
		// An expression that modifies the duration of the outcome

	ItemAssignmentOutcomeModifier	 **eaDefaultWeightModifierSettings;
		// if set, will be used if the ItemAssignmentDef does not have any ItemAssignmentOutcomeModifier

	const char** ppchDefaultOutcomeModifiers; AST(NAME(DefaultOutcomeModifier) POOL_STRING)
		// if set, the default modifiers applied to a slot that has none defined

	ItemAssignmentOutcomeModifier	 *pDefaultDurationScaleModifierSettings;
		// if set, will be used if the ItemAssignmentDef does not have any ItemAssignmentOutcomeModifier
		// will be used regardless of the eType

	const char** ppchDurationScaleNumerics; AST(NAME(DurationScaleNumeric) POOL_STRING)
		// A list of numerics that receive a duration scale. If left empty, then all numerics get a duration scale.

	const char** ppchQualityScaleNumerics; AST(NAME(QualityScaleNumeric) POOL_STRING)
		// A list of numerics that receive a quality scale. If left empty, then all numerics get a quality scale.

	S32 iActiveAssignmentPointsPerPlayer; AST(NAME(ActiveAssignmentPointsPerPlayer) DEFAULT(5))
		// The number of assignment "points" each player can use at any given time

	S32 iMaxAssignmentHistoryCount; AST(NAME(MaxAssignmentHistoryCount) DEFAULT(10))
		// The maximum number of completed assignments to save in the player's history

	ItemAssignmentSettingsSlots *pStrictAssignmentSlots;
		// optional. If set item assignments are assigned "slots", with a maximum number of assignments that can be started.
	
	const char* pchXPFilterBaseName;
	// used for filtering XP numerics out of outcome UI list generation

	ItemAssignmentRankingSchedule *pCategoryRankingSchedule;
		// if present, will automatically rank up the category's NumericRank1 when NumericXP1 hits given thresholds

	ItemAssignmentRankingSchedule *pMetaRankingSchedule;
		// if preset, will automatically rank up the NumericRank when NumericXP defined in the ItemAssignmentRankingSchedule

	InvBagIDs *eaiDefaultInventoryBagCacheIDs; AST(NAME(DefaultInventoryBagCacheIDs) SUBTABLE(InvBagIDsEnum))
		// default bags to cache when caching inventory for checking things for availability

	ItemAssignmentWeightType *eaiDisplayWeightCategories; AST(NAME(DisplayWeightCategories) SUBTABLE(ItemAssignmentWeightTypeEnum))

	Expression	*pExprForceCompleteNumericCost;		AST(NAME(ExprForceCompleteNumericCost,ForceCompleteNumericCost), REDUNDANT_STRUCT(ExprDurationModifer, parse_Expression_StructParam), LATEBIND)
		// expression evaluates the cost to forcing a completion of an assignment. 
		// Defining this makes it so forcing completion can only be done via the numeric and not the micro-trans token

	REF_TO(ItemDef) hForceCompleteNumeric;		AST(REFDICT(ItemDef))
		// the numeric required for force completion, see pExprForceCompleteNumericCost

	ItemAssignmentOutcomeWeightWindowConfig *pOutcomeWeightWindowConfig;
		// if set present, will override the way choosing outcomes is weighed.  
		// having this This overrides the default ItemAssignmentOutcomeModifierType system

	S32 eExcludeBagFlagsForItemCosts;	AST(FLAGS SUBTABLE(InvBagFlagEnum))
		// if set, then bags with these flags will not be considered when doing item costs for assignments

	U32 bInteriorsUseLastStaticMap : 1; AST(NAME(InteriorsUseLastStaticMap))
		// Whether or not 'Interior' maps use the player's last static map for fetching available assignments

	U32 bRequirePlayerInValidVolume : 1; AST(NAME(RequirePlayerInValidVolume))
		// Whether or not to check to see if the player is in one of the volumes in ppchValidVolumes

	U32 bDisableRewardModifiers : 1; AST(NAME(DisableRewardModifiers))
		// If this is set, then do not apply reward modifiers to item assignment rewards

	U32 bDebugOnly : 1;
		// Item assignments are only enabled for AL7 and above

	U32 bDoNotRequireMapRaritiesForAssignmentGeneration : 1;
		// if set, when generating assignments map assignments will not be generated

	U32 bKeepEmptyRewardOutcomeAssignments : 1;
		// if set, assignments that complete that have no rewards will not be removed from the eaActiveAssignments

	U32 bAllowDuplicateActiveAssignments : 1;
		// if set, will allow more than one assignment to be active at once 

	U32 bDoNotAverageOutcomeWeights : 1;
		// if set, will not average the outcome weights by the number of slots in ItemAssignments_GetOutcomeWeights

	U32 bGetItemAssignmentUIChecksSlottedNotInventory : 1;
		// a modification to how the expression GetItemAssignmentUI() checks for requirements. 

	U32 bUseStrictCategoryChecking : 1;
		// a modification to how gclItemAssignmentSlot_CheckInventory works in seeing if you have the correct items to slot
		// an item must contain all the RequiredItemCategories on the slot

	U32 bGenerateSampleRewardTable : 1;
		// if true, will generate the InvRewardRequest *pSampleRewards field on the ItemAssignmentDef's ItemAssignmentOutcomeResults
		// but only for the first outcome in the list

	U32 bSlotsSortItemCategories : 1;
		// if set, the slots will sort the categories on them by the ItemCategoryInfo's SortOrder

	U32 bUseOptionalSlots : 1;
		// Enables the use of optional slots, and redefines what iMinimumSlottedItems does for item assignment defs
} ItemAssignmentSettings;

AUTO_STRUCT;
typedef struct ItemAssignmentOutcomeRewardRequest
{
	const char* pchOutcome; AST(POOL_STRING)
	InvRewardRequest* pData;
} ItemAssignmentOutcomeRewardRequest;

AUTO_STRUCT;
typedef struct ItemAssignmentRewardRequestData
{
	REF_TO(ItemAssignmentDef) hDef;
	ItemAssignmentOutcomeRewardRequest** eaOutcomes;
} ItemAssignmentRewardRequestData;

// Persisted information about slotted items
AUTO_STRUCT AST_CONTAINER; 
typedef struct ItemAssignmentSlottedItem
{
	const U64 uItemID; AST(NAME(ItemID) SELF_ONLY PERSIST SUBSCRIBE)
		// The ID of the item slotted

	const S32 iAssignmentSlot; AST(NAME(Slot) SELF_ONLY PERSIST SUBSCRIBE)
		// The assignment slot that this item is slotted to
	
	InvBagIDs eBagID; NO_AST
		// Cached bag ID of the item
	
	S32 iBagSlot; NO_AST
		// Cached bag slot index of the item
} ItemAssignmentSlottedItem;

AUTO_STRUCT;
typedef struct ItemAssignmentSlots
{
	EARRAY_OF(ItemAssignmentSlottedItem) eaSlots;
} ItemAssignmentSlots;

// Persisted assignment data
AUTO_STRUCT AST_CONTAINER;
typedef struct ItemAssignment
{
	CONST_REF_TO(ItemAssignmentDef) hDef; AST(SELF_ONLY PERSIST SUBSCRIBE)
		// The ItemAssignmentDef that started
	
	const U32 uAssignmentID; AST(NAME(AssignmentID) SELF_ONLY PERSIST SUBSCRIBE)
		// The unique ID given to this assignment

	const U32 uTimeStarted; AST(NAME(TimeStarted) SELF_ONLY PERSIST SUBSCRIBE)
		// The time the assignment started

	const U32 uDuration; AST(NAME(Duration) SELF_ONLY PERSIST SUBSCRIBE)
		// the actual duration of the item assignment- post modification time

	STRING_POOLED pchRewardOutcome; AST(NAME(RewardOutcome) POOL_STRING SELF_ONLY PERSIST SUBSCRIBE)
		// The outcome to used for reward collection

	STRING_POOLED pchMapMsgKey; AST(NAME(MapMessageKey) POOL_STRING SELF_ONLY PERSIST SUBSCRIBE)
		// The message key of the map that this assignment started on

	CONST_EARRAY_OF(ItemAssignmentSlottedItem) eaSlottedItems; AST(NAME(SlottedItems) SELF_ONLY PERSIST SUBSCRIBE)
		// The items that were slotted for this assignment

	const S8 uItemAssignmentSlot;	AST(SELF_ONLY PERSIST SUBSCRIBE)
		// what item assignment slot this belongs to, if any. -1 is undefined.
	
	U32 bCompletionPending : 1; NO_AST
		// The completion transaction is running, so don't try to complete it again

} ItemAssignment;

// Information about completed assignments for validation purposes
AUTO_STRUCT AST_CONTAINER;
typedef struct ItemAssignmentCompleted
{
	REF_TO(ItemAssignmentDef) hDef; AST(SELF_ONLY KEY PERSIST SUBSCRIBE)
		// The ItemAssignmentDef that was completed
	
	const U32 uCompleteTime; AST(SELF_ONLY PERSIST SUBSCRIBE)
		// The time that the assignment completed
} ItemAssignmentCompleted;

AUTO_STRUCT AST_CONTAINER;
typedef struct ItemAssignmentSlottedItemResults
{
	CONST_REF_TO(ItemDef) hDef; AST(STRUCTPARAM PERSIST)
		// The item that was slotted

	const U64 uItemID; AST(NAME(ItemID) PERSIST)
		// The item ID for this slotted item

	const U32 bNewAssignment : 1; AST(NAME(NewAssignment) PERSIST)
		// Whether or not the item was put on a new assignment

	const U32 bDestroyed : 1; AST(NAME(Destroyed) PERSIST)
		// Whether or not the item was destroyed
} ItemAssignmentSlottedItemResults;

// Details about a completed assignment
AUTO_STRUCT AST_CONTAINER;
typedef struct ItemAssignmentCompletedDetails
{
	CONST_REF_TO(ItemAssignmentDef) hDef; AST(SELF_ONLY PERSIST SUBSCRIBE)
		// The ItemAssignmentDef that was completed

	const U32 uAssignmentID; AST(NAME(AssignmentID) SELF_ONLY PERSIST SUBSCRIBE)
		// The unique ID of the assignment that completed

	const U32 uNewAssignmentID; AST(NAME(NewAssignmentID) SELF_ONLY PERSIST SUBSCRIBE)
		// The ID of the new assignment that started immediately after the completion of the first

	STRING_POOLED pchOutcome; AST(SELF_ONLY PERSIST POOL_STRING SUBSCRIBE)
		// The chosen outcome

	STRING_POOLED pchMapMsgKey; AST(SELF_ONLY PERSIST POOL_STRING SUBSCRIBE)
		// The message key of the map that this assignment started on

	const U32 uTimeStarted; AST(SELF_ONLY PERSIST SUBSCRIBE)
		// The time the assignment started

	const U32 uDuration; AST(SELF_ONLY PERSIST SUBSCRIBE)
		// the duration it was calculated to complete

	U32 bMarkedAsRead : 1; AST(SELF_ONLY PERSIST NO_TRANSACT SUBSCRIBE)
		// This has been flagged as seen by the user and no longer needs to be prominently displayed in the UI

	CONST_EARRAY_OF(ItemAssignmentSlottedItemResults) eaSlottedItemRefs; AST(SELF_ONLY PERSIST SUBSCRIBE)
		// The items that were slotted
} ItemAssignmentCompletedDetails;


AUTO_STRUCT AST_CONTAINER;
typedef struct ItemAssignmentPersonalPersistedBucket
{
	CONST_EARRAY_OF(ItemAssignmentDefRefCont)	eaAvailableAssignments; AST(SERVER_ONLY PERSIST SUBSCRIBE)

	const U32 uLastPersonalUpdateTime; AST(SELF_ONLY PERSIST SUBSCRIBE)
		// The last time personal assignments were updated on the player (also used as a seed to generate the personal list)

} ItemAssignmentPersonalPersistedBucket;

// Persisted item assignment player data
AUTO_STRUCT AST_CONTAINER AST_IGNORE(uLastPersonalUpdateTime) AST_IGNORE_STRUCT(eaPersonalAssignments);
typedef struct ItemAssignmentPersistedData
{
	CONST_EARRAY_OF(ItemAssignment) eaActiveAssignments; AST(SELF_ONLY PERSIST FORCE_CONTAINER SUBSCRIBE)
		// List of current active item assignments
	
	CONST_EARRAY_OF(ItemAssignmentCompleted) eaCompletedAssignments; AST(SELF_ONLY PERSIST FORCE_CONTAINER SUBSCRIBE)
		// List of all relevant completed item assignments for requirement validation

	CONST_EARRAY_OF(ItemAssignmentCompletedDetails) eaRecentlyCompletedAssignments; AST(SELF_ONLY PERSIST SUBSCRIBE)
		// List of completed assignments with associated data for UI display

	CONST_EARRAY_OF(ItemAssignmentPersonalPersistedBucket) eaPersistedPersonalAssignmentBuckets; AST(SELF_ONLY PERSIST SUBSCRIBE)
		// List of personal assignment buckets

	const U32 uMaxAssignmentID; AST(SERVER_ONLY PERSIST SUBSCRIBE)
		// The max item assignment ID for this player

	CONST_INT_EARRAY eaItemAssignmentSlotsUnlocked; AST(SELF_ONLY PERSIST SUBSCRIBE)
		// Item assignment slots that have been unlocked with expressions

	U32 uNextUpdateTime; NO_AST
		// The time when assignment data needs to be processed next
} ItemAssignmentPersistedData;


AUTO_STRUCT;
typedef struct ItemAssignmentPersonalAssignmentBucket
{
	EARRAY_OF(ItemAssignmentDefRef)	eaAvailableAssignments;

	U32 uNextPersonalRequestTime; NO_AST
		// The time that personal assignments should be requested again

	U32 bUpdatedList : 1; NO_AST
		// Whether or not the server updated the player's personal assignment list

} ItemAssignmentPersonalAssignmentBucket;

// Non-persisted item assignment player data
AUTO_STRUCT;
typedef struct ItemAssignmentPlayerData
{
	EARRAY_OF(ItemAssignmentDefRef) eaVolumeAvailableAssignments; AST(SELF_ONLY)
		// Array of currently available assignments from the current volume the player is in

	EARRAY_OF(ItemAssignmentDefRef) eaGrantedPersonalAssignments; AST(SELF_ONLY)
		// ItemAssignments granted by game actions

	EARRAY_OF(ItemAssignmentDefRef) eaAutograntedPersonalAssignments; AST(SELF_ONLY)
		// ItemAssignments that the player is always given 

	ItemAssignmentPersonalAssignmentBucket **eaPersonalAssignmentBuckets; AST(SELF_ONLY)
		// Array of currently personal assignment lists

	U32 uNextAutograntUpdateTime;

	U32 uLastUpdateTime; NO_AST
	
	U32 bDebugForceRequestPersonalAssignments : 1; NO_AST
		// If this is set, then force request a new batch of personal assignments
		// this should only be used for debugging

	U32 bUnfilterExpression : 1;
		// Bypass the requirements expression filter

	U32 bUnfilterAllegiance : 1;
		// Bypass the allegiance requirements filter

	U32 bUnfilterMaximumLevel : 1;
		// Bypass the maximum level requirement filter

	U32 bUnfilterActive : 1;
		// Bypass the active assignment restriction filter

	U32 bUnfilterNotRepeatable : 1;
		// Bypass the not repeatable filter

	U32 bUnfilterCooldown : 1;
		// Bypass the cooldown filter

	U32 bUnfilterRequiredMission : 1;
		// Bypass the mission requirement filter

	U32 bUnfilterRequiredAssignment : 1;
		// Bypass the assignment requirement filter

	U32 bUnfilterLocation : 1;
		// Bypass the location filter
} ItemAssignmentPlayerData;

typedef struct ItemAssignmentPersonalIterator
{
	S32 iSubList;
	S32 iSubListIdx;
} ItemAssignmentPersonalIterator;

AUTO_STRUCT;
typedef struct ItemAssignmentUnlockedSlots
{
	INT_EARRAY eaSlots;
} ItemAssignmentUnlockedSlots;



extern ItemAssignmentSettings g_ItemAssignmentSettings;
extern DictionaryHandle g_hItemAssignmentDict;
extern bool g_bRebuildItemAssignmentTree;

ExprContext* ItemAssignments_GetContextEx(	Entity* pEnt, 
											ItemAssignmentCompletedDetails *pCompletedDetails,
											ItemAssignmentOutcome* pOutcome, 
											ItemAssignmentOutcomeModifier* pMod, 
											Item* pSlottedItem, 
											ItemAssignmentSlot *pSlotDef, 
											ItemCategory eItemCategoryUI, 
											S32 iSecondsRemaining);
#define ItemAssignments_GetContext(pEnt) ItemAssignments_GetContextEx(pEnt, NULL, NULL, NULL, NULL, NULL, 0, 0)

bool ItemAssignment_Validate(ItemAssignmentDef* pDef);

// General accessors
ItemAssignmentDef* ItemAssignment_DefFromName(const char* pchDefName);
NOCONST(ItemAssignment)* ItemAssignment_trh_EntityGetActiveAssignmentByID(ATH_ARG NOCONST(Entity)* pEnt, U32 uID);
#define ItemAssignment_EntityGetActiveAssignmentByID(pEnt, uID) CONTAINER_RECONST(ItemAssignment, ItemAssignment_trh_EntityGetActiveAssignmentByID(CONTAINER_NOCONST(Entity, (pEnt)), uID))
NOCONST(ItemAssignment)* ItemAssignment_trh_EntityGetActiveAssignmentByDef(ATH_ARG NOCONST(Entity)* pEnt, ItemAssignmentDef* pDef);
#define ItemAssignment_EntityGetActiveAssignmentByDef(pEnt, uID) CONTAINER_RECONST(ItemAssignment, ItemAssignment_trh_EntityGetActiveAssignmentByDef(CONTAINER_NOCONST(Entity, (pEnt)), pDef))
NOCONST(ItemAssignmentCompletedDetails)* ItemAssignment_trh_EntityGetRecentlyCompletedAssignmentByID(ATH_ARG NOCONST(Entity)* pEnt, U32 uID);
#define ItemAssignment_EntityGetRecentlyCompletedAssignmentByID(pEnt, uID) CONTAINER_RECONST(ItemAssignmentCompletedDetails, ItemAssignment_trh_EntityGetRecentlyCompletedAssignmentByID(CONTAINER_NOCONST(Entity, (pEnt)), uID))
ItemAssignmentCategorySettings* ItemAssignmentCategory_GetSettings(ItemAssignmentCategory eCategory);
const ItemAssignmentCategorySettings** ItemAssignmentCategory_GetCategoryList();

ItemAssignmentWeight* ItemAssignmentSettings_GetWeightDef(F32 fWeightValue);

#define ItemAssignment_GetDuration(a,d) (((a) && (a)->uDuration) ? (a)->uDuration : SAFE_MEMBER((d),uDuration))

// Outcome accessors
ItemAssignmentWeight* ItemAssignmentWeightType_GetWeightDef(ItemAssignmentWeightType eWeightType);
F32 ItemAssignmentWeightType_GetWeightValue(ItemAssignmentWeightType eWeightType);
S32 ItemAssignments_EvaluateCategoryIsHidden(Entity *pEnt, const ItemAssignmentCategorySettings *pCategory);

F32 ItemAssignment_GetDurationScale(ItemAssignment* pAssignment, ItemAssignmentDef* pDef);
F32 ItemAssignment_GetNumericQualityScaleForItemDef(ItemDef* pItemDef);
F32 ItemAssignment_GetNumericQualityScale(Entity* pEnt, const ItemAssignment* pAssignment, ItemAssignmentCompletedDetails* pCompletedDetails, const ItemAssignmentDef* pDef);
ItemAssignmentOutcome* ItemAssignment_GetOutcomeByName(ItemAssignmentDef* pDef, const char* pchOutcomeName);
ItemAssignmentOutcomeModifierTypeData* ItemAssignment_GetModifierTypeData(ItemAssignmentOutcomeModifierType eType);
ItemAssignmentOutcomeModifier* ItemAssignment_GetModifierByName(ItemAssignmentDef* pDef, const char* pchModifierName);
F32 ItemAssignments_GetOutcomeWeightModifier(Entity* pEnt, ItemAssignmentOutcome* pOutcome, ItemAssignmentOutcomeModifier* pMod, 
												ItemAssignmentOutcomeModifierTypeData* pData, ItemAssignmentSlot *pSlotDef, 
												Item* pSlottedItem, ItemCategory eItemCategoryUI);
F32 ItemAssignments_GetQualityWeight(ItemQuality eQuality, const char* pchOutcomeName);
F32 ItemAssignments_GetQualityDurationScale(ItemQuality eQuality);
U32 ItemAssignments_CalculateDuration(Entity *pEnt, ItemAssignmentDef *pDef, ItemAssignmentSlottedItem **eaSlottedItems);
F32 ItemAssignments_CalculateOutcomeDisplayValueForSlot(ItemAssignmentDef* pDef, Item* pItem);

F32 ItemAssignments_GetDurationModifier(Entity* pEnt, Item* pSlottedItem, ItemAssignmentOutcomeModifier *pMod, 
										ItemAssignmentSlot *pSlotDef, ItemCategory eItemCategoryUI);

void ItemAssignments_CalculateOutcomeWeights(Entity* pEnt, ItemAssignmentDef* pDef, ItemAssignmentSlottedItem** eaSlottedItems, F32** ppfOutcomeWeights);
ItemAssignmentRarityCount* ItemAssignment_GetRarityCountByType(ItemAssignmentRarityCountType eType);
bool ItemAssignments_CheckDestroyRequirements(ItemDef* pItemDef, ItemAssignmentOutcome* pOutcome);

// Slotting accessors
NOCONST(InventorySlot)* ItemAssignments_trh_GetInvSlotFromSlottedItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(ItemAssignmentSlottedItem)* pSlottedItem, GameAccountDataExtract* pExtract);
Item* ItemAssignments_GetItemFromSlottedItem(Entity* pEnt, ItemAssignmentSlottedItem* pSlottedItem, GameAccountDataExtract* pExtract);
bool ItemAssignments_trh_CanSlotItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ItemAssignmentSlot* pSlot, InvBagIDs eBagID, S32 iBagSlot, GameAccountDataExtract* pExtract);
#define ItemAssignments_CanSlotItem(pEnt, pSlot, eBagID, iBagSlot, pExtract) ItemAssignments_trh_CanSlotItem(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), pSlot, eBagID, iBagSlot, pExtract)
bool ItemAssignments_trh_ValidateSlots(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ItemAssignmentDef* pDef, ItemAssignmentSlots* pSlots, GameAccountDataExtract* pExtract);
bool ItemAssignment_trh_CanSlottedItemResideInBag(ATH_ARG NOCONST(InventoryBag)* pBag);
#define ItemAssignment_CanSlottedItemResideInBag(pBag) ItemAssignment_trh_CanSlottedItemResideInBag(CONTAINER_NOCONST(InventoryBag, (pBag)))
const char** ItemAssignments_GetOutcomeModifiersForSlot(ItemAssignmentSlot* pSlot);

// Requirement evaluation
S32 ItemAssignments_trh_GetMaxAssignmentPoints(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt);
#define ItemAssignments_GetMaxAssignmentPoints(pEnt) ItemAssignments_trh_GetMaxAssignmentPoints(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)))
S32 ItemAssignments_trh_GetRemainingAssignmentPoints(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt);
#define ItemAssignments_GetRemainingAssignmentPoints(pEnt) ItemAssignments_trh_GetRemainingAssignmentPoints(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)))
bool ItemAssignments_trh_CheckRequirements(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ItemAssignmentDef* pDef, ItemAssignmentSlots* pSlots, GameAccountDataExtract* pExtract);
bool ItemAssignments_CheckRequirementsExpression(ItemAssignmentDef* pDef, Entity* pEnt);
ItemAssignmentFailsRequiresReason ItemAssignments_GetFailsRequiresReason(Entity* pEnt, ItemAssignmentDef* pDef, ItemAssignmentSlots* pSlots, ItemAssignmentFailsRequiresEntry*** peaErrors);
#define ItemAssignments_CheckRequirements(pEnt, pDef, pSlots) !ItemAssignments_GetFailsRequiresReason(pEnt, pDef, pSlots, NULL)

// Persisted assignment accessors
NOCONST(ItemAssignmentCompleted)* ItemAssignments_trh_PlayerGetCompletedAssignment(ATH_ARG NOCONST(Entity)* pEnt, ItemAssignmentDef* pDef);
#define ItemAssignments_PlayerGetCompletedAssignment(pEnt, pDef) CONTAINER_RECONST(ItemAssignmentCompleted, ItemAssignments_trh_PlayerGetCompletedAssignment(CONTAINER_NOCONST(Entity, (pEnt)), pDef))
ItemAssignmentCompletedDetails* ItemAssignments_PlayerGetRecentlyCompletedAssignment(Entity* pEnt, ItemAssignmentDef* pDef, const char* pchOutcome);
S32 ItemAssignments_PlayerFindGrantedAssignment(Entity* pEnt, ItemAssignmentDef* pDef);

bool ItemAssignments_PlayerCanAccessAssignments(Entity* pEnt);

ItemAssignment* ItemAssignments_FindActiveAssignmentWithSlottedItem(const ItemAssignmentPersistedData* pPlayerData, U64 uItemID);


S32 ItemAssignments_GetRankRequiredToUnlockSlot(SA_PARAM_NN_VALID ItemAssignmentSettingsSlots *pStrictSlotSettings, S32 iSlot);

S32 ItemAssignments_trh_GetNumberUnlockedAssignmentSlots(ATR_ARGS, SA_PARAM_NN_VALID NOCONST(Entity)* pEnt, 
													 SA_PARAM_NN_VALID ItemAssignmentSettingsSlots *pStrictSlotSettings);
#define ItemAssignments_GetNumberUnlockedAssignmentSlots(pEnt,pSettings) ItemAssignments_trh_GetNumberUnlockedAssignmentSlots(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity,pEnt),pSettings)

// returns true if there are any open assignment slots
bool ItemAssignments_HasAnyOpenSlots(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID ItemAssignmentSettingsSlots *pStrictSlotSettings);

bool ItemAssignments_trh_IsValidNewItemAssignmentSlot(ATR_ARGS, SA_PARAM_NN_VALID NOCONST(Entity)* pEnt, SA_PARAM_NN_VALID ItemAssignmentSettingsSlots *pStrictSlotSettings, S32 iAssignmentSlot);
#define ItemAssignments_IsValidNewItemAssignmentSlot(pEnt,pSettings,iAssignmentSlot) ItemAssignments_trh_IsValidNewItemAssignmentSlot(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), pSettings, iAssignmentSlot)

S32 ItemAssignments_GetExperienceThresholdForRank(SA_PARAM_NN_VALID ItemAssignmentRankingSchedule *pSchedule, S32 iRank);
S32 ItemAssignments_GetNumRanks(SA_PARAM_NN_VALID ItemAssignmentRankingSchedule *pSchedule);

void ItemAssignments_InitializeIteratorPersonalAssignments(ItemAssignmentPersonalIterator *it);
ItemAssignmentDefRef* ItemAssignments_IterateOverPersonalAssignments(ItemAssignmentPlayerData* pPlayerData, ItemAssignmentPersonalIterator *it);

bool ItemAssignments_HasAssignment(Entity* pEnt, ItemAssignmentDef* pDef, ItemAssignmentPlayerData *pItemAssignmentData);
bool ItemAssignments_GetForceCompleteNumericCost(Entity *pEnt, ItemAssignment* pAssignment, S32 *piCostToComplete);

ItemAssignment* ItemAssignment_FindActiveAssignmentByID(ItemAssignmentPersistedData* pPlayerData, U32 uAssignmentID);
ItemAssignmentCompletedDetails* ItemAssignment_FindCompletedAssignmentByID(ItemAssignmentPersistedData* pPlayerData, U32 uAssignmentID);

S32 ItemAssignments_GetSearchInvBagFlags();

ItemAssignmentSlotUnlockExpression *ItemAssignment_GetUnlockFromKey(int key);
bool ItemAssignments_CheckItemSlotExpression(ItemAssignmentSlotUnlockExpression *pUnlock, Entity *pEnt, ItemAssignmentCompletedDetails *pCompletedAssignment);

#endif //ITEMASSIGNMENTS_H