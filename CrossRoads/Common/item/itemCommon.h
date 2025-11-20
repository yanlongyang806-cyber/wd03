

#ifndef ITEMCOMMON_H
#define ITEMCOMMON_H
GCC_SYSTEM

#include "referencesystem.h"
#include "MultiVal.h" // For multival struct
#include "structDefines.h"	// For StaticDefineInt
#include "message.h"

#include "CharacterAttribsMinimal.h" // For enums
#include "CostumeCommon.h" // For kCostumeDisplayMode enum
#include "ItemEnums.h"
#include "PowerTree.h"
#include "MicroTransactions.h"
#include "allegiance.h"
#include "stdtypes.h"

typedef struct CritterDef CritterDef;
typedef struct AllegianceRef AllegianceRef;
typedef struct AlgoPet AlgoPet;
typedef struct AlgoPetDef AlgoPetDef;
typedef struct Power Power;
typedef struct PowerDef PowerDef;
typedef struct MissionDef MissionDef;
typedef struct Expression Expression;
typedef struct CostumeDisplayData CostumeDisplayData;
typedef struct DefineContext DefineContext;
typedef struct Entity Entity;
typedef struct Character Character;
typedef struct CharacterClass CharacterClass;
typedef struct Player Player;
typedef struct Guild Guild;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct InteriorDef InteriorDef;
typedef struct Item Item;
typedef struct ItemArt ItemArt;
typedef struct ItemDef ItemDef;
typedef struct ItemPowerDef ItemPowerDef;
typedef struct ItemPowerDefRef ItemPowerDefRef;
typedef struct StoreDef StoreDef;
typedef struct CritterFactionRelationship CritterFactionRelationship;
typedef struct PlayerCostume PlayerCostume;
typedef struct ObjectPathOperation ObjectPathOperation;
typedef struct PowerReplaceDef PowerReplaceDef;
typedef struct PowerReplace PowerReplace;
typedef struct PowerSubtarget PowerSubtarget;
typedef struct PowerSubtargetChoice PowerSubtargetChoice;
typedef struct PTNodeDefRef PTNodeDefRef;
typedef struct CritterPowerConfig CritterPowerConfig;
typedef struct PetDef PetDef;
typedef struct CharacterClassRef CharacterClassRef;
typedef struct CharacterPathRef CharacterPathRef;
typedef struct TempAttributes TempAttributes;
typedef struct ChatLinkInfo ChatLinkInfo;
typedef struct TransactionReturnVal TransactionReturnVal;
typedef struct RewardTable RewardTable;
typedef struct SpeciesDef SpeciesDef;
typedef struct StoreItemInfo StoreItemInfo;
typedef struct PlayerStatDef PlayerStatDef;
typedef struct ItemSortTypes ItemSortTypes;
typedef struct SuperCritterPetDef SuperCritterPetDef;
typedef struct ItemGemConfig ItemGemConfig;
typedef struct InvRewardRequest InvRewardRequest;
typedef struct InventorySlot InventorySlot;

typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(Power) NOCONST(Power);
typedef struct NOCONST(Item) NOCONST(Item);
typedef struct NOCONST(InfuseSlot) NOCONST(InfuseSlot);
typedef struct NOCONST(InventoryBag) NOCONST(InventoryBag);

extern StaticDefineInt AttribTypeEnum[];
extern StaticDefineInt AttribAspectEnum[];

extern DictionaryHandle g_hItemDict;
extern DictionaryHandle g_hItemPowerDict;
extern DictionaryHandle g_hInfuseSlotDict;
extern DictionaryHandle g_hPlayerCostumeClonesForItemsDict;
extern S32 g_bItemSets; // Whether or not any ItemSets are defined
extern bool g_bDisplayItemDebugInfo;
extern ItemSortTypes gSortTypes;
extern ItemGemConfig s_ItemGemConfig;

#define ITEM_BUY_MULTIPLIER -1.0
#define ITEM_SELL_MULTIPLIER 1.0
#define WITHDRAW_LIMIT_TIME 86400

#define ITEM_CRAFT_CLIENT_DELAY_SECS 0.2f
#define ITEM_CRAFT_DELAY_SECS 3.0f
#define ITEM_EXPERIMENT_DELAY_SECS 3.0f

#define ITEMSET_MAX_SIZE_BITS 4
#define ITEMSET_MAX_SIZE ((1 << ITEMSET_MAX_SIZE_BITS)-1)

#define ITEMS_BASE_DIR "defs/items"
#define ITEMS_EXTENSION "item"
#define ITEMS_BIN_FILE "ItemDef.bin"
#define ITEMPOWERS_BASE_DIR "defs/itempowers"
#define ITEMPOWERS_EXTENSION "itempower"
#define ITEMPOWERS_BIN_FILE "Itempowerdef.bin"
#define INFUSESLOT_BASE_DIR "defs/infuseslots"
#define INFUSESLOT_EXTENSION "infuseslot"
#define INFUSESLOT_BIN_FILE "infuseslotdef.bin"
#define ITEM_SORT_TYPE_FILE "defs/config/itemSortTypes.def"
#define ITEM_TAGS_FILE "defs/config/ItemTags.def"
#define ITEM_CATEGORIES_FILE "defs/config/ItemCategories.def"
#define ITEM_DECONSTRUCT_DEFAULTS_FILE "defs/config/ItemDeconstructDefaults.def"

// time before a player can buy/sell again, normally should never come into play as callbacks clear time
#define ITEM_BUY_BACK_TIMEOUT_SECONDS 30

// Hardcoded door key values
//		The map variable used as the DoorKey value when a door key item is created
#define ITEM_DOOR_KEY_MAP_VAR		"MAP_ENTRY_KEY"
//		The ItemDef to be used when creating hidden door key items for missions
#define ITEM_MISSION_DOOR_KEY_DEF	"DoorKeyHidden"

AUTO_ENUM;
typedef enum ItemDefFlag
{
	kItemDefFlag_Tradeable			= (1<<0),
	kItemDefFlag_BindOnPickup		= (1<<1),
	kItemDefFlag_BindOnEquip		= (1<<2),
	kItemDefFlag_EquipOnPickup		= (1<<3),
	kItemDefFlag_Enigma				= (1<<4),
	kItemDefFlag_Fused				= (1<<5),
	kItemDefFlag_CanUseUnequipped	= (1<<6),
	kItemDefFlag_CantSell			= (1<<7),
	kItemDefFlag_Silent				= (1<<8),
	kItemDefFlag_Unique				= (1<<9),
	kItemDefFlag_LevelFromQuality	= (1<<10),
	kItemDefFlag_LevelFromSource	= (1<<11),
	kItemDefFlag_ScaleWhenBought	= (1<<12),
	kItemDefFlag_RandomAlgoQuality	= (1<<13),
	kItemDefFlag_NoMinLevel			= (1<<14),
	kItemDefFlag_Unidentified_Unsafe		= (1<<15), ENAMES(Unidentified)
	kItemDefFlag_TransFailonLowLimit  = (1<<16),
	kItemDefFlag_TransFailonHighLimit = (1<<17),
	kItemDefFlag_ExpireOnAnyPower = (1<<18),
	kItemDefFlag_DoorKey			= (1<<19),
	kItemDefFlag_SetMissionOnCreate	= (1<<20),		// This is a mission item, but the missionDef is set on the item instance when the item is created
	kItemDefFlag_ScaleWithCritterScaling = (1<<21),	// Applies to numerics only
	kItemDefFlag_CantDiscard = (1<<22),
	kItemDefFlag_DoppelgangerPet = (1<<23),			//If this is a saved pet, it takes its costume and name from the player's current target when generated.
	kItemDefFlag_CostumeHideable = (1<<24),			//If this is set, allow the player to set the bHide flag on the item to hide/unhide the costume
	kItemDefFlag_UniqueEquipOnePerBag = (1<<25),	// Only one of these can be equipped per bag
	kItemDefFlag_LockToRestrictBags = (1<<26),		//If this is set, the item is not allowed to moved out of a restrict bag
	kItemDefFlag_CanSlotOnAssignment = (1<<27),		//This item can be slotted on an item assignment
	kItemDefFlag_BindToAccountOnPickup		= (1<<28),	
	kItemDefFlag_BindToAccountOnEquip		= (1<<29),
	kItemDefFlag_SCPBonusNumeric = (1<<30),
	kItemDefFlag_CantMove			= (1<<31),
}ItemDefFlag;

AUTO_ENUM;
typedef enum ItemPowerFlag
{
	kItemPowerFlag_Gadget			= (1<<0),
	kItemPowerFlag_CanUseUnequipped	= (1<<1),
	kItemPowerFlag_Rechargeable  	= (1<<2),
	kItemPowerFlag_LocalEnhancement	= (1<<3),
	kItemPowerFlag_DefaultStance	= (1<<4),
}ItemPowerFlag;

extern DefineContext *g_pDefineItemPowerCategories;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineItemPowerCategories);
typedef enum ItemPowerCategory		//Decribes a general category of item enhancement. Used for NNO to determine stacking rules on craftables.
{
	// this is kind of a different thing.  I will be using this to set a temporary context value, and no actual Power will be added to the item for this ItemPowerDef
	kItemPowerCategory_PowerFactor = 1 << 0,	ENAMES(PowerFactor)
	kItemPowerCategory_CODEMAX = kItemPowerCategory_PowerFactor,	EIGNORE
	kItemPowerCategory_FIRST_DATA_DEFINED,	EIGNORE
}ItemPowerCategory;

extern DefineContext *g_pDefineItemPowerArtCategories;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineItemPowerArtCategories);
typedef enum ItemPowerArtCategory		//Lets itempowers change the appearance of their base item.
{
	kItemPowerArtCategory_NONE = 0,	ENAMES(None)
	kItemPowerArtCategory_FIRST_DATA_DEFINED,	EIGNORE
}ItemPowerArtCategory;
// Do not change the order of this enum!
AUTO_ENUM;
typedef enum ItemIDType
{
	kItemIDType_Player,
	kItemIDType_SavedPet,
	kItemIDType_Puppet, // Unused
	kItemIDType_Critter,
	kItemIDType_SharedBank,
} ItemIDType;

AUTO_ENUM;
typedef enum ItemType
{
	kItemType_Upgrade,	//most non-weapon equipables are type upgrade
	kItemType_Component,
	kItemType_ItemRecipe, ENAMES(ItemRecipe Recipe)
	kItemType_ItemValue,  ENAMES(ItemValue ValueRecipe)
	kItemType_ItemPowerRecipe,
	kItemType_Mission,
	kItemType_MissionGrant,
	kItemType_Boost,
	kItemType_Device,
	kItemType_Numeric,
	kItemType_Weapon,
	kItemType_Bag,
	kItemType_Callout,
	kItemType_Lore,
	kItemType_Token,
	kItemType_Title,
	kItemType_SavedPet,
	kItemType_STOBridgeOfficer,
	kItemType_AlgoPet,
    kItemType_TradeGood,
	kItemType_ModifyAttribute,
	kItemType_VanityPet,
	kItemType_Container,
	kItemType_CostumeUnlock,
	kItemType_Injury,
	kItemType_InjuryCureGround,
	kItemType_InjuryCureSpace,
	kItemType_RewardPack,
	kItemType_GrantMicroSpecial,
	kItemType_ExperienceGift,
	kItemType_Coupon,
	kItemType_Gem,
	kItemType_DyeBottle,
	kItemType_DyePack,
	kItemType_UpgradeModifier,
	kItemType_SuperCritterPet,
	kItemType_IdentifyScroll,
	kItemType_LockboxKey,
	kItemType_PowerFactorLevelUp,		// Level up the power factor on an item. Very specific transactions on this. Make a items that match our category  the same power factor as this item.
	kItemType_UnidentifiedWrapper,
	kItemType_Junk,						// For categorization on (or exclusion from) the auction house.  These items literally do nothing.
	kItemType_None,
}ItemType;

AUTO_ENUM;
typedef enum SlotType
{
	kSlotType_Any,
	kSlotType_Primary,
	kSlotType_Secondary, 
}SlotType;

AUTO_ENUM;
typedef enum ItemFlag
{
	kItemFlag_Bound					= (1<<0), ENAMES(Bound)
	kItemFlag_Unidentified_Unsafe	= (1<<1), ENAMES(Unidentified)
	kItemFlag_Full					= (1<<2), ENAMES(Full)			// This item is now full. Used with kItemType_ExperienceGift
	kItemFlag_BoundToAccount		= (1<<3), ENAMES(BoundToAccount)	// This item can not be moved to entities that are on different accounts
	kItemFlag_TrainingFromItem		= (1<<4), ENAMES(TrainingFromItem)	// This item can not be moved to entities that are on different accounts
	kItemFlag_SlottedOnAssignment	= (1<<5), ENAMES(SlottedOnAssignment)	// This item can not be moved to entities that are on different accounts
	kItemFlag_Algo					= (1<<6), ENAMES(Algo)	// This item can not be moved to entities that are on different accounts
}ItemFlag;

AUTO_ENUM;
typedef enum TradeErrorType
{
	kTradeErrorType_None = 0,
	kTradeErrorType_InvalidNumeric = 1,
	kTradeErrorType_ItemBound = 2,
	kTradeErrorType_InvalidItem = 3,
} TradeErrorType;

AUTO_ENUM;
typedef enum ItemPowerGroup
{
	kItemPowerGroup_1			= (1<<0), ENAMES(Group1)
	kItemPowerGroup_2			= (1<<1), ENAMES(Group2)
	kItemPowerGroup_3			= (1<<2), ENAMES(Group3)
	kItemPowerGroup_4			= (1<<3), ENAMES(Group4)
	kItemPowerGroup_5			= (1<<4), ENAMES(Group5)
	kItemPowerGroup_6			= (1<<5), ENAMES(Group6)
	kItemPowerGroup_7			= (1<<6), ENAMES(Group7)
	kItemPowerGroup_8			= (1<<7), ENAMES(Group8)

}ItemPowerGroup;
#define ITEM_POWER_GROUP_COUNT 8

AUTO_ENUM;
typedef enum ComponentCountType
{
	kComponentCountType_Fixed,
	kComponentCountType_LevelAdjust,
	kComponentCountType_Common1,
	kComponentCountType_Common2,
	kComponentCountType_UnCommon1,
	kComponentCountType_UnCommon2,
	kComponentCountType_Rare1,

}ComponentCountType;

extern DefineContext *g_pDefineLoreJournalTypes;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineLoreJournalTypes);
typedef enum LoreJournalType
{

	kLoreJournalType_None,					ENAMES(None)

	kLoreJournalType_FIRST_DATA_DEFINED,	EIGNORE
}LoreJournalType;

AUTO_ENUM;
typedef enum ItemBuyBackStatus
{
	kItemBuyBackStatus_None,
	kItemBuyBackStatus_Waiting,				// waiting for server response
	kItemBuyBackStatus_OK,					// can be bought back
	kItemBuyBackStatus_BeingBought,			// being bought back
	kItemBuyBackStatus_Destroy,				// is going to be destroyed
}ItemBuyBackStatus;

AUTO_ENUM;
typedef enum SuperCritterPetFlags
{
	kSuperCritterPetFlag_None = 0,
	kSuperCritterPetFlag_Dead = 1 << 0,
	kSuperCritterPetFlag_MAX = 1 << 7,	//do not exceed; flags field is a U8.
}SuperCritterPetFlags;

AUTO_STRUCT AST_CONTAINER;
typedef struct SuperCritterPet
{
	REF_TO(SuperCritterPetDef) hPetDef;		AST(NAME(SuperCritterPetDef) REFDICT(SuperCritterPetDef) PERSIST SUBSCRIBE)
	REF_TO(CharacterClass) hClassDef;		AST(NAME(ClassDef) REFDICT(CharacterClass) PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pchName;		AST(PERSIST SUBSCRIBE)
	const U32 uXP;							AST(PERSIST SUBSCRIBE)
	const U32 uLevel;						AST(PERSIST SUBSCRIBE)
	const U8 iCurrentSkin;					AST(PERSIST SUBSCRIBE)
	const U8 bfFlags;						AST(PERSIST SUBSCRIBE)
} SuperCritterPet;

AUTO_STRUCT AST_CONTAINER;
typedef struct ItemTransmutationProps
{
	// The item that overrides the visuals of the main item
	REF_TO(ItemDef) hTransmutatedItemDef;		AST(NAME(TransmutatedItemDef) REFDICT(ItemDef) PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
} ItemTransmutationProps;

// Defines all the names of itempower categories
AUTO_STRUCT;
typedef struct ItemPowerCategoryNames
{
	char **ppchNames; AST(NAME(ItemPowerCategory))
	char **ppchArtNames; AST(NAME(ItemPowerArtCategory))
}ItemPowerCategoryNames;

AUTO_STRUCT;
typedef struct LorePage
{
	REF_TO(ItemDef) hItemDef; AST(STRUCTPARAM)
	S32 iUnlockValue; AST(STRUCTPARAM)
}LorePage;

// Lore Category information.
AUTO_STRUCT;
typedef struct LoreCategory
{
	const char *pchName;	AST(STRUCTPARAM, POOL_STRING)
		// The name of the category

	const char* pchParentCategoryName; AST(NAME(ParentCategory), POOL_STRING)
		// Name of this category's parent.

	REF_TO(PlayerStatDef) hPlayerStatDef; AST(NAME(PlayerStat))
		// PlayerStatDef that unlocks pages in this category.

	LorePage** ppPages; AST(NAME(Page))
} LoreCategory;

// Array of the LoreCategories, loaded and indexed directly
AUTO_STRUCT;
typedef struct LoreCategories
{
	LoreCategory **ppCategories;	AST(NAME(LoreCategory))
} LoreCategories;

// Globally accessibly LoreCategories structure
extern LoreCategories g_LoreCategories;

// StaticDefine that includes all categories, both code and data
extern StaticDefineInt LoreCategoryEnum[];


AUTO_ENUM;
typedef enum ItemQualityFlag
{
	kItemQualityFlag_HideFromUILists		= (1<<0),
	kItemQualityFlag_ReportToSocialNetworks	= (1<<1),
	kItemQualityFlag_IgnoreLootThreshold	= (1<<2),
}ItemQualityFlag;

// Item Quality information.
AUTO_STRUCT;
typedef struct ItemQualityInfo
{
	const char *pchName;			AST(STRUCTPARAM, POOL_STRING)
		// The name of the quality

	int iNeedBeforeGreedDelay;		AST(DEFAULT(60))
		//number of seconds that need before greed should wait before awarding this item

	ItemQualityFlag flags;			AST(FLAGS)

	F32 fWeaponDamageMultiplier;	AST(DEFAULT(1))
		// The weapon damage multiplier for this item quality

	// The name of the loot FX played for this item quality (right now only applies to corpse loot)
	const char *pchLootFX;			AST(POOL_STRING)
	const char *pchRolloverLootFX;			AST(POOL_STRING)

	const char *pchChatLinkColorName;

	Expression *pExprEPValue;			AST(LATEBIND NAME(ItemEPValue))

} ItemQualityInfo;

// Array of the ItemQualities, loaded and indexed directly
AUTO_STRUCT;
typedef struct ItemQualities
{
	ItemQualityInfo **ppQualities;	AST(NAME(ItemQuality))

	int iMaxStandardQuality; NO_AST
	//What the active project considers to be the highest tier of "normal" loot qualities. (Equivalent of "Purple" in most projects)
} ItemQualities;

// Globally accessible ItemQualities structure
extern ItemQualities g_ItemQualities;

//Projects specify their own default for this, 
//but just in case it's missing here's the equivalent of the old 
//hardcoded default.
#define FALLBACK_NEEDORGREED_THRESHOLD "Blue"

// A named MultiVal
AUTO_STRUCT;
typedef struct ItemVar
{
	char *pchName;			AST(STRUCTPARAM)
		// Name of the variable
	
	MultiVal mvValue;		AST(NAME(Value))
		// MultiVal that defines the variable
	
	S32 hVarHandle;
		// The handle used by the expression context
	
	const char *cpchFile;	AST(NAME(File), CURRENTFILE)
		// Current file (required for reloading)
} ItemVar;


// Wrapper for list of all item vars
AUTO_STRUCT;
typedef struct ItemVars
{
	ItemVar **ppItemVars;			AST(NAME(ItemVar))
	// EArray of all item vars
	
	StashTable stItemVars;			AST(USERFLAG(TOK_NO_WRITE))
	// Handy stash table of the item vars, built at load
} ItemVars;

AUTO_STRUCT;
typedef struct UsageRestrictionCategories
{
	const char **pchNames; AST(NAME(UICategoryName))
} UsageRestrictionCategories;

extern int g_iNumUsageRestrictionCategories;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pUsageRestrictionCategories);
typedef enum UsageRestrictionCategory
{
	UsageRestrictionCategory_None, ENAMES(None)
} UsageRestrictionCategory;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UsageRestriction
{
	int iMinLevel;		//if specified, character must be >= the specified level to avoid restriction
	int iMaxLevel;		//if specified, character must be <= the specified level to avoid restriction

	SkillType eSkillType;
	U32 iSkillLevel;
	
	//if specified, the expression must evaluate to true to avoid restriction
	Expression * pRequires;					AST(NAME(pRequiresBlock), REDUNDANT_STRUCT(pRequires, parse_Expression_StructParam), LATEBIND, USERFLAG(TOK_USEROPTIONBIT_1))

	//if specified, this indicates what UI category this item should fall under
	UsageRestrictionCategory eUICategory;	AST(NAME(UICategory) SUBTABLE(UsageRestrictionCategoryEnum) DEFAULT(UsageRestrictionCategory_None))

	// The required allegiances for this item
	AllegianceRef** eaRequiredAllegiances;	AST(NAME(RequiredAllegiance))

	// if specified, character must have a class matching one of the specified classes to avoid restriction
	CharacterClassRef **ppCharacterClassesAllowed; AST(NAME(ClassAllowed) STRUCT(parse_CharacterClassRef))
	S32 *peClassCategoriesAllowed; AST(NAME(ClassCategoryAllowed) SUBTABLE(CharClassCategoryEnum))
	
	CharacterPathRef **ppCharacterPathsAllowed; AST(NAME(PathAllowed) STRUCT(parse_CharacterPathRef))

	S32 eRequiredGemSlotType;		AST(NAME(RequiredGemSlotType) SUBTABLE(ItemGemTypeEnum)) // ItemGemType
}UsageRestriction;

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pItemEquipLimitCategories);
typedef enum ItemEquipLimitCategory
{
	kItemEquipLimitCategory_None, ENAMES(None)
} ItemEquipLimitCategory;

AUTO_STRUCT;
typedef struct ItemEquipLimitCategoryData
{
	const char *pchName; AST(STRUCTPARAM)
	DisplayMessage msgDisplayName; AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))  
	S32 iMaxItemCount; AST(NAME(MaxItemCount))
	ItemEquipLimitCategory eCategory; NO_AST
} ItemEquipLimitCategoryData;

AUTO_STRUCT;
typedef struct ItemEquipLimitCategories
{
	ItemEquipLimitCategoryData** eaData; AST(NAME(Category, CategoryName))
} ItemEquipLimitCategories;

// Decides how many of a given ItemDef a character can have equipped at once
AUTO_STRUCT;
typedef struct ItemEquipLimit
{
	S32 iMaxEquipCount;					AST(NAME(MaxEquipCount))
	ItemEquipLimitCategory eCategory;	AST(NAME(Category))
} ItemEquipLimit;

AUTO_STRUCT;
typedef struct ItemCraftingComponent
{
	// I would like to make this AST(NON_NULL_REF) as well, but since it loads inline with
	// the items it refers to, that is not possible.
	REF_TO(ItemDef) hItem;					AST(REFDICT(ItemDef) NAME(Item) STRUCTPARAM REQUIRED)
	F32 fCount;								AST(STRUCTPARAM DEF(1))
	F32 fWeight;							AST(DEF(1)) // Used in deconstruct
	U32 iMinLevel;							AST(DEF(1))
	U32 iMaxLevel;							AST(DEF(50))

	bool bDeconstructOnly;					//this component is not required for item construction
	ComponentCountType CountType;			//type of component for algo count lookup

}ItemCraftingComponent;  

AUTO_STRUCT;
typedef struct ItemCraftingTable
{
	U32 iResource;							AST(NAME(Resource))

	int iResultCount;						AST(DEF(1))
	// I would like to make this AST(NON_NULL_REF) as well, but since it loads inline with
	// the items it refers to, that is not possible.
	REF_TO(ItemDef) hItemResult;			AST(REFDICT(ItemDef) NAME(ItemResult) ADDNAMES(Result) )// this is the item we craft
	REF_TO(ItemPowerDef) hItemPowerResult;		AST(REFDICT(ItemPowerDef) NAME(ItemPowerResult) )// this is the item we craft

	ItemCraftingComponent ** ppPart;
	// List of parts, weights, values

}ItemCraftingTable;

AUTO_STRUCT;
typedef struct LoreJournalData
{
	LoreJournalType eType;						AST(NAME(Type))
	const char** ppchTextures;					AST(NAME(Textures) POOL_STRING)
	const char* pchCritterName;					AST(POOL_STRING NAME(CritterName))

}LoreJournalData;

AUTO_STRUCT AST_IGNORE(Group) 
AST_IGNORE(EconomyPoints);
typedef struct ItemPowerDef
{
	char *pchName;							AST( STRUCTPARAM KEY POOL_STRING )
	char *pchFileName;						AST( CURRENTFILE )
	char *pchScope;							AST( POOL_STRING )
	
	DisplayMessage displayNameMsg;			AST(STRUCT(parse_DisplayMessage))  
	DisplayMessage displayNameMsg2;			AST(STRUCT(parse_DisplayMessage))  
	DisplayMessage descriptionMsg;		    AST(STRUCT(parse_DisplayMessage)) 
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

	S32 iPointBuyCost;	

	int eItemPowerCategories;	AST(NAME(ItemPowerCategory), FLAGS, SUBTABLE(ItemPowerCategoryEnum))
	ItemPowerArtCategory iArtCategory;	//For itempowers that change the appearance of their base item when applied.
	// this is for special ItemPowers that set things like "iPowerFactor"

	S32 iFactorValue;

} ItemPowerDef;


AUTO_STRUCT AST_CONTAINER AST_SINGLETHREADED_MEMPOOL;
typedef struct ItemPowerDefRef
{
	// Unique identifier for the ItemPowerDefRef
	const U32 uID;								AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)

	CONST_REF_TO(ItemPowerDef) hItemPowerDef;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE NAME(ItemPowerDef) REFDICT(ItemPowerDef) REQUIRED FORCE_CONTAINER)
	
	const F32 ScaleFactor;						AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE DEFAULT(1.0f))
		// Scale applied to the magnitude of the mods of the PowerDef (when applicable)
	
	const InvBagIDs eAppliesToSlot;				AST(NAME(AppliesToSlot))
	const ItemCategory *peRequiredCategories;	AST(NAME(RequiredCategories) SUBTABLE(ItemCategoryEnum))
	const F32 fChanceToApply;					AST(NAME(ChanceToApply))


	const U32 iPowerGroup : ITEM_POWER_GROUP_COUNT;		AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)

	const F32 fGemSlottingApplyChance;			AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE DEFAULT(1.0f) NAME(GemSlottingApplyChance))
		// The chance of applying this power when the gem is slotted into an item

	U32 uiSetMin : ITEMSET_MAX_SIZE_BITS;		AST(NAME(SetMin))
		// If this is on an ItemSet item, this is the minimum number of Items
		//  in the ItemSet that must be equipped for the Character to enable
		//  this ItemPowerDef.

	const U32 bGemSlotsAdjustScaleFactor : 1;

}ItemPowerDefRef;

// Describes the cost and result of a specific rank of an InfuseOption
AUTO_STRUCT;
typedef struct InfuseRank
{
	S32 iCost;
	// The cost (in the numeric from the InfuseOption) to purchase this rank

	const char *pchIcon;			AST(POOL_STRING)
		// The icon for this rank

		ItemPowerDefRef **ppDefRefs;	AST(NAME(ItemPowerDefRef))
		// The ItemPowerDefRefs provided as a result for being at this rank

} InfuseRank;

// Describes the costs and results of putting a specific numeric into an InfuseSlot.
AUTO_STRUCT;
typedef struct InfuseOption
{
	REF_TO(ItemDef) hItem;	AST(NAME(Item))
		// The numeric ItemDef that is spent for this option

		InfuseRank **ppRanks;	AST(NAME(Rank))
		// The ranks available for this option

} InfuseOption;

// Describes a space in which an Item can have numerics "infused" or spent on it in order to add
//  additional benefits.  It's a little like a "gem slot" in other games, except that it takes
//  numerics, rather than entire items, and the side-effect is defined by the slot-numeric pair, rather
//  than purely by the applied item.
// Conceptually it's like a simple PowerTree with a single set of mutually exclusive nodes.
AUTO_STRUCT;
typedef struct InfuseSlotDef
{
	char *pchName;						AST(STRUCTPARAM KEY POOL_STRING)
		// Internal name

		InfuseOption **ppOptions;			AST(NAME(Option))
		// What options there are for infusing the slot

		DisplayMessage msgDisplayName;		AST(STRUCT(parse_DisplayMessage)) 
		DisplayMessage msgDescription;		AST(STRUCT(parse_DisplayMessage))
		// Display messages

		const char *pchIcon;				AST(POOL_STRING)
		// The icon (may be overridden by the icon for the chosen option's current rank)

		char *pchFileName;					AST(CURRENTFILE)
} InfuseSlotDef;

// Wrapper for InfuseSlotDef for EArrays
AUTO_STRUCT;
typedef struct InfuseSlotDefRef
{
	REF_TO(InfuseSlotDef) hDef;		AST(STRUCTPARAM)
} InfuseSlotDefRef;

// The instance of an InfuseSlotDef, once it's actually got something in it.  The combination
//  of the InfuseSlotDef, Option ItemDef and rank tells you what should be happening.
AUTO_STRUCT AST_CONTAINER;
typedef struct InfuseSlot
{
	CONST_REF_TO(InfuseSlotDef) hDef;	AST(PERSIST)
		// The InfuseSlotDef that backs this particular InfuseSlot

		CONST_REF_TO(ItemDef) hItem;		AST(PERSIST)
		// The numeric ItemDef used to fill the InfuseSlot, and thus which InfuseOption is being used.

		const S32 iRefund;					AST(PERSIST)
		// Tracks how much of the numeric ItemDef has been spent at time of purchases.  Only saved to
		//  provide refunds in the case that the InfuseSlotDef is no longer valid
		//  and we need to remove this InfuseSlot from the Item.

		const S32 iIndex;					AST(PERSIST, KEY)
		// The index of the InfuseSlotDef on the ItemDef.  Keyed to keep the earray sorted.
		//  This is here to handle the case where an ItemDef has more than one identical
		//  InfuseSlotDef on it, and the player is attempting to fill the second one first
		//  or other such business.

		const S8 iRank;						AST(PERSIST)
		// The ZERO-BASED rank of the InfuseSlot.

} InfuseSlot;


AUTO_STRUCT;
typedef struct ItemCostume
{
	REF_TO(PlayerCostume) hCostumeRef;		AST(REFDICT(PlayerCostume) NAME(Costume) STRUCTPARAM)
	Expression *pExprRequires;				AST(NAME(ExprBlockRequires,ExprRequiresBlock), REDUNDANT_STRUCT(ExprRequires, parse_Expression_StructParam), LATEBIND)
} ItemCostume;

AUTO_STRUCT;
typedef struct ItemVanityPet
{
	REF_TO(PowerDef) hPowerDef;		AST(REFDICT(PowerDef) NAME(VanityPet))
} ItemVanityPet;

AUTO_STRUCT;
typedef struct ItemTagNames
{
	char **ppchName;					AST(NAME(ItemTag))
} ItemTagNames;

extern ItemTagNames g_ItemTagNames;

AUTO_ENUM;
typedef enum AdditionalCostumeBoneType
{
	kAdditionalCostumeBoneType_Clone = 0,
	kAdditionalCostumeBoneType_Move
}AdditionalCostumeBoneType;

AUTO_STRUCT;
typedef struct ItemCategoryAdditionalCostumeBone
{
	AdditionalCostumeBoneType eType;	AST(STRUCTPARAM)
	REF_TO(PCBoneDef) hOldBone;			AST(STRUCTPARAM NAME(Old) REFDICT(PCBoneDef))
	REF_TO(PCBoneDef) hNewBone;			AST(STRUCTPARAM NAME(New) REFDICT(PCBoneDef))
}ItemCategoryAdditionalCostumeBone;

AUTO_STRUCT;
typedef struct CostumeCloneForItemCat
{
	PlayerCostume* pCostume;
	ItemCategoryAdditionalCostumeBone** eaBones;
	const char* pchName;	AST(POOL_STRING KEY)
} CostumeCloneForItemCat;

AUTO_STRUCT;
typedef struct ItemCategoryInfo
{
	const char *pchName; AST(POOL_STRING STRUCTPARAM)

	// A UI hint string
	const char *pchHint; AST(POOL_STRING)

	// A second UI hint string
	const char *pchHint2; AST(POOL_STRING)

	// The icon to associate with this category
	const char *pchIcon; AST(POOL_STRING)

	// The an alternate icon to associate with this category
	const char *pchLargeIcon; AST(POOL_STRING)

	// Used by several functions to override the value of the item category in sorting
	S32 iSortOrder;

	//On an item with this category, a single gem slot is considered 
	//  to represent this portion of the item's overall power.
	F32 fGemSlotPowerPortion;

	const char* pchStanceWords;

	ItemCategoryAdditionalCostumeBone** eaPrimarySlotAdditionalBones;	AST(NAME(PrimaryAddedBone))
	ItemCategoryAdditionalCostumeBone** eaSecondarySlotAdditionalBones;	AST(NAME(SecondaryAddedBone))

	F32 fCostumeFXScaleValue;	AST(NAME(CostumeFXScale))

	const char* pchOverrideBaseCostumeFXAtBone;	AST(NAME(OverrideBaseCostumeFXAtBone) POOL_STRING)
	const char* pchOverrideCostumeFXBone;	AST(NAME(OverrideCostumeFXAtBone) POOL_STRING)

	ItemCategory eCategoryEnum; NO_AST
} ItemCategoryInfo;

AUTO_STRUCT;
typedef struct ItemCategoryNames
{
	ItemCategoryInfo **ppInfo;					AST(NAME(ItemCategory))
} ItemCategoryNames;

extern ItemCategoryNames g_ItemCategoryNames;

AUTO_STRUCT;
typedef struct ItemAttribModifyValues
{
	TempAttributes *pTempAttribs;
	U32 eSavedPetClassType;				AST(SUBTABLE(CharClassTypesEnum))
}ItemAttribModifyValues;

AUTO_STRUCT;
typedef struct ItemDefRef
{
	REF_TO(ItemDef) hDef;				AST(REFDICT(ItemDef) STRUCTPARAM)
} ItemDefRef;

AUTO_STRUCT AST_CONTAINER;
typedef struct ItemDefRefCont
{
	CONST_REF_TO(ItemDef) hDef;			AST(REFDICT(ItemDef) STRUCTPARAM PERSIST)
} ItemDefRefCont;

AUTO_STRUCT;
typedef struct ItemNumericData
{
	REF_TO(ItemDef) hDef;
	S32 iNumericValue;
} ItemNumericData;

AUTO_ENUM;
typedef enum ItemWarpType
{
	kItemWarp_None,
		//Invalid
	kItemWarp_SelfToMapSpawn,
		// Warp myself to a spawn point
	kItemWarp_SelfToTarget,
		// Warp myself to a target entity
	kItemWarp_TeamToSelf,
		// Warp everyone but myself to me
	kItemWarp_TeamToMapSpawn,
		// Evac type item warp
	kItemWarp_MAX,			EIGNORE
} ItemWarpType;

#define ItemWarp_NUMBITS 4
STATIC_ASSERT(kItemWarp_MAX <= (1 << (ItemWarp_NUMBITS - 1)));

AUTO_STRUCT;
typedef struct ItemDefWarp
{
	bool bLimitedUse;
		//If true, activating a warp will consume one of the stack of items.
	U32 iWarpChargesMax_DEPRECATED;		AST(NAME("WarpChargesMax"))
		//How many warp charges does this have
	const char *pchMap;					AST(POOL_STRING)
		//Which map to warp to
	const char *pchSpawn;				AST(POOL_STRING)
		// Which spawn to warp to
	U32 uiTimeToConfirm;
		// How long does the target have to confirm the warp
		//  Only really makes sense for TeamToSelf and TeamToMapSpawn
	U32 eWarpType : ItemWarp_NUMBITS;	AST(SUBTABLE(ItemWarpTypeEnum))
		// What kind of warp is this
	U32 bCanMapMove : 1;				AST(DEFAULT(1))
		// Can this warp work across map moves (Default true)
} ItemDefWarp;

AUTO_STRUCT;
typedef struct ItemWeaponDef
{
	S32	iDieSize;
	S32 iNumDice;						AST( DEF(1) )
	Expression* pAdditionalDamageExpr;	AST(LATEBIND)
} ItemWeaponDef;

AUTO_STRUCT;
typedef struct ItemDamageDef
{
	// Expression that defines the magnitude for the item damage
	Expression *pExprMagnitude;		AST(NAME(ExprMagnitude) LATEBIND)		

	// Defines how the item damage value randomly varies.  Valid values are [0..1].  0 means no
	//  variance at all.  1 means the value can vary +/- 100%.
	F32 fVariance;

	// If not null, this provides a default table lookup that is multiplied as a final step
	//  for calculating the item damage
	const char *pchTableName;	AST(POOL_STRING)
} ItemDamageDef;

//Contains a list of members of the ItemSet
AUTO_STRUCT;
typedef struct ItemSetMembers
{
	ItemDefRef **eaMembers;
} ItemSetMembers;

AUTO_STRUCT;
typedef struct ItemTrainablePowerNode
{
	REF_TO(PTNodeDef) hNodeDef;	AST(NAME(NodeDef) STRUCTPARAM)
		// The node to train
	S32 iNodeRank; AST(NAME(Rank) DEFAULT(1))
		// The rank to set to when the node is trained (1-based)
} ItemTrainablePowerNode;

AUTO_STRUCT;
typedef struct RewardPackInfo
{
	REF_TO(RewardTable) hRewardTable;	AST(STRUCTPARAM REFDICT(RewardTable) SERVER_ONLY)
		// The reward table to generate items from and give to the player

	DisplayMessage msgUnpackMessage;	AST(NAME(UnpackMessage) STRUCT(parse_DisplayMessage))
		// The message to display when this reward pack is opened

	DisplayMessage msgUnpackFailedMessage;	AST(NAME(UnpackFailedMessage) STRUCT(parse_DisplayMessage))
		// The message to display when this reward pack fails to open

	REF_TO(ItemDef) hRequiredItem;		AST(NAME(RequiredItem) REFDICT(ItemDef))
		// An item that is required in order to open this reward pack

	S32 iRequiredItemCount;				AST(NAME(RequiredItemCount) DEFAULT(1))
		// The amount of items required (see hRequiredItem)

	const char* pchRequiredItemProduct; AST(NAME(RequiredItemProduct) POOL_STRING)
		// The MT product associated with the required item

	U32 bConsumeRequiredItems : 1;		AST(NAME(ConsumeRequiredItems))
		// Whether or not the required items are destroyed when this pack is opened

} RewardPackInfo;

extern DefineContext *g_pDefineItemGemTypes;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineItemGemTypes);
typedef enum ItemGemType
{
	kItemGemType_None = 0,
	kItemGemType_Any = 0xFFFFFFFF,
}ItemGemType;

AUTO_STRUCT;
typedef struct ItemGemUnslotCost
{
	const char *pchCurrency;
	Expression *pExprCanUse;	AST(NAME(ExprBlockRequires,ExprRequiresBlock), REDUNDANT_STRUCT(ExprRequires, parse_Expression_StructParam), LATEBIND)
		Expression *pExprCost;		AST(NAME(ExprBlockCost,ExprCostBlock), REDUNDANT_STRUCT(ExprCost, parse_Expression_StructParam), LATEBIND)
}ItemGemUnslotCost;

AUTO_STRUCT;
typedef struct ItemGemConfig
{
	char **ppchGemType;		AST(NAME(Gemtype))
		ItemGemUnslotCost **ppUnslotCosts; AST(NAME(UnslotCost))
}ItemGemConfig;

AUTO_STRUCT;
typedef struct ItemGemSlotDef
{
	ItemGemType eType;					AST(NAME(TYPE) STRUCTPARAM FLAGS)
		REF_TO(ItemDef) hPreSlottedGem;		AST(NAME(PreSlottedGem))
}ItemGemSlotDef;

AUTO_STRUCT;
typedef struct ItemGemSlotFilledInfo
{
	ItemGemType eType;
	bool bIsFilled;
}ItemGemSlotFilledInfo;

AUTO_STRUCT;
typedef struct ItemDefBonus
{
	CONST_REF_TO(ItemDef) hItem;				AST(REFDICT(ItemDef) STRUCTPARAM)
		// the item (numeric) that that this item is given a bonus by

}ItemDefBonus;

// This is the definition of things that will not change on the item.
AUTO_STRUCT
AST_IGNORE(Slot)
AST_IGNORE(SubType)
AST_IGNORE(kSlot)
AST_IGNORE(Rating)
AST_IGNORE(EconomyPoints);
typedef struct ItemDef
{
	// Display info ---------------------------
	const char *pchName;				AST( STRUCTPARAM KEY POOL_STRING )
	const char *pchFileName;					AST( CURRENTFILE USERFLAG(TOK_USEROPTIONBIT_1))
	const char *pchScope;				AST( POOL_STRING )
	
	DisplayMessage displayNameMsg;		AST(STRUCT(parse_DisplayMessage) USERFLAG(TOK_USEROPTIONBIT_1))
	DisplayMessage descriptionMsg;		AST(STRUCT(parse_DisplayMessage) USERFLAG(TOK_USEROPTIONBIT_1)) 
	DisplayMessage descShortMsg;		AST(STRUCT(parse_DisplayMessage) USERFLAG(TOK_USEROPTIONBIT_1)) // A short description of the item.

	DisplayMessage displayNameMsgUnidentified;		AST(STRUCT(parse_DisplayMessage) USERFLAG(TOK_USEROPTIONBIT_1)) // The message to show before this item has been identified.
	DisplayMessage descriptionMsgUnidentified;		AST(STRUCT(parse_DisplayMessage) USERFLAG(TOK_USEROPTIONBIT_1)) // A short 6-10 word description of the item. (Un-IDed version)

	DisplayMessage msgAutoDesc;			AST(STRUCT(parse_DisplayMessage) USERFLAG(TOK_USEROPTIONBIT_1)) // A replacement for the autogenerated descriptions. 


	char *pchNotes; // Designer notes field
	
	const char *pchIconName;			AST( NAME(Icon) POOL_STRING )
	
	ItemDefFlag flags;					AST(FLAGS)
	
	ItemType eType;					AST( NAME(Type) )
	SkillType kSkillType;				AST(NAME(SkillType))
	ItemGemType eGemType;				AST(NAME(Gemtype) FLAGS)

	InvBagIDs* peRestrictBagIDs;		AST(NAME(RestrictBagID, RestrictBag2ID) SUBTABLE(InvBagIDsEnum))
	SlotType eRestrictSlotType;
	REF_TO(InventorySlotIDDef) hSlotID;	AST(NAME(SlotIDType) REFDICT(InventorySlotIDDef))

	int iStackLimit;					AST( DEF(1) ) // how many can we have
	
	Expression *pExprEconomyPoints;		AST(LATEBIND)	//the value of this item in economy points
	
	S32 MinNumericValue;				AST( DEF(S32_MIN+1) )
	S32 MaxNumericValue;				AST( DEF(S32_MAX) )

	REF_TO(ItemDef) hNumericOverflow;	AST(NAME(NumericOverflow))
	F32 fNumericOverflowMulti;					

    // If this field is set it indicates that spending from this numeric is done by incrementing the spending numeric rather
    //  than decrementing this numeric.  Note that the underlying numeric code does not enforce this.  Other systems that
    //  want to spend these numerics will have to handle it in their own code.  At the time this field was added this is 
    //  just the Group Project System.
    REF_TO(ItemDef) hSpendingNumeric;	AST(NAME(SpendingNumeric))

	//If iMayBuyInBulk is other than 0 means this item is something the user would typically want to buy many of (like 10+) in one store visit.
	//This tells the UI to add a slider and/or text entry field to let the user specify how much they want to buy.
	//If the UI will show a slider this value is the slider max value.
	S32 iMayBuyInBulk;

	S32 iLevel;							AST(DEF(1))		//!!!!  defaulting to 1 for now to avoid level 0 errors, will set to 0 when design fixed data
	ItemQuality Quality;
	S32 iPowerFactor;
	
	S32 iNumBagSlots;
	U32 iSortID;						AST(NO_TEXT_SAVE)

	F32 fScaleUI;						AST(NAME(ScaleUI) DEFAULT(1))
		// The amount to scale this numeric by for the UI
	
	REF_TO(MissionDef) hMission;		AST(REFDICT(Mission) NAME(Mission))

	U32 uNewItemPowerDefRefID;			AST(NAME(NewItemPowerDefRefID_DontTouchThis))
		// The ID to be used for a new ItemPowerDefRef instance
	
	ItemPowerDefRef **ppItemPowerDefRefs;
	ItemTrainablePowerNode **ppTrainableNodes; AST(NAME(TrainableNode))

	InfuseSlotDefRef **ppInfuseSlotDefRefs;
		// EArray of InfuseSlotDefs

	ItemDefRef **ppItemSets;			AST(NAME(ItemSet))
		// The ItemSet(s) that this ItemDef belongs to

	ItemDefRef **ppItemSetMembers;	AST(NO_TEXT_SAVE)
		// Members of the ItemSet (only on ItemDefs that other ItemDefs belong to)
		//  Derived at load time

	REF_TO(SuperCritterPetDef)	hSCPdef;	AST(NAME(SuperCritterPet) REFDICT(SuperCritterPetDef))
	ItemGemSlotDef **ppItemGemSlots;

	ItemVanityPet **ppItemVanityPetRefs;

	REF_TO(PowerSubtarget) hSubtarget;	AST(NAME(Subtarget) REFDICT(PowerSubtarget))

	REF_TO(ItemArt) hArt;				AST(NAME(Art) REFDICT(ItemArt))

	REF_TO(InteriorDef) hInterior;		AST(NAME(Interior) REFDICT(InteriorDef))

	RewardPackInfo* pRewardPackInfo;	AST(NAME(RewardPackInfo, RewardTable))
	
	ItemCraftingTable *pCraft;
	UsageRestriction *pRestriction;
	ItemEquipLimit *pEquipLimit;		AST(NAME(EquipLimit))
	ItemPowerGroup Group;				AST(FLAGS)
	
	Vec3 vDyeColor0;
	Vec3 vDyeColor1;
	Vec3 vDyeColor2;
	Vec3 vDyeColor3;
	REF_TO(PCMaterialAdd) hDyeMat;

	kCostumeDisplayMode eCostumeMode;
	U32 iCostumePriority;
	ItemCostume **ppCostumes;
	REF_TO(SpeciesDef) hSpecies;		AST(NAME(Species))
	
	REF_TO(ItemDef) hCraftRecipe;		AST(REFDICT(ItemDef) NAME(CraftRecipe) ADDNAMES(Recipe))
	ItemDefRef **ppValueRecipes;		AST(NAME(ValueRecipe))
	
	// Callout info
	DisplayMessage calloutMsg;			AST(STRUCT(parse_DisplayMessage))
	const char *calloutFSM;
	bool bImportantCallout; // Takes priority over other callouts

	// Lore
	S32 iLoreCategory;			AST(NAME(LoreCategory) SUBTABLE(LoreCategoryEnum))
	LoreJournalData* pJournalData;

	// tags
	ItemTag eTag;						AST(NAME(Tag))

	// EArray of ItemCategories
	ItemCategory *peCategories;			AST(NAME(Categories) SUBTABLE(ItemCategoryEnum))

	REF_TO(PetDef) hPetDef;				AST(REFDICT(PetDef) NAME(PetGrant))
	REF_TO(AlgoPetDef) hAlgoPet;		AST(REFDICT(AlgoPetDef) NAME(AlgoPet))
	
	ItemDefWarp *pWarp;

	U32 bMakeAsPuppet : 1;
	
	U32 bDeleteAfterUnlock: 1;
		// After unlocking the costumes on an item, remove the item from the inventory.
	
	U32 bAutoDescDisabled : 1;

	U32 bLogForEconomy : 1;
	U32 bLogForTracking : 1;

	U32 bItemSetMembersUnique : 1; AST(DEFAULT(1) ADDNAMES(UniqueItemSet))
		// If this is true and this item is an item set, 
		// then all items in the set are required to be unique or have an equip limit of 1

	U32 bCostumeIgnoreSkeletonMatching : 1;	AST(NAME(CostumeIgnoreSkeletonMatching))

	U32 bTrainingDestroysItem : 1; AST(NAME(TrainingDestroysItem) DEFAULT(1))
		// If this is set to true, then the item is destroyed when training from this item

	U32 bCouponUsesItemLevel : 1;
		// If set the coupon discount is equal to item's level

	U32 bExtraSafeRemove : 1;
		// For the UI, to be extra cautious before removing this item.

	U32 bMessageOnTrayActivateFailure : 1;
		// If set this item will display an error message if it fails to activate while in a tray (see TrayExec)

	ItemAttribModifyValues *pAttribModifyValues;

	ItemWeaponDef* pItemWeaponDef;		AST(NAME(Weapon))	

	ItemDamageDef *pItemDamageDef;		AST(NAME(Damage))

	SpecialPartType eSpecialPartType;	AST(NAME(SpecialPartType))				
		// The special part enum to be used with	kItemType_GrantMicroSpecial, this can be none

	const char* pcGemAddedCostumeFX;	AST(NAME(GemAddedCostumeFX) POOL_STRING)
		// An additional FX added to the holder item's costume when this gem is socketed.
	const char* pcGemAddedCostumeBone;	AST(NAME(GemAddedCostumeBone) POOL_STRING)
		// Bone to pass as a param to the above FX.

	U32 uSpecialPartCount;				AST(NAME(SpecialPartCount))				
		// The number of special parts to be granted

	const char *pchPermission;			AST(POOL_STRING NAME(GamePermission))
		// kItemType_GrantMicroSpecial permission granted

	U32 uExperienceGift;
		// How much experience this item can hold as an expereince gift when full

	U32 uCouponDiscount;
		// Amount of discount this coupon gives as a percent, can't be more than X which is defined in MTconfig.def

	const char **ppchMTCategories;			AST(NAME(MTCategories) POOL_STRING RESOURCEDICT(MicroTransactionCategory))
		// The categories of MicroTransaction that this coupon can apply to

	const char **ppchMTItems;			AST(NAME(MTItem) POOL_STRING RESOURCEDICT(MicroTransaction))
		// Micro trans defs that this can be applied to

	CONST_EARRAY_OF(ItemDefBonus) eaBonusNumerics;	AST(NAME(BonusNumerics))
		// bonus numeric types

	U32 uBonusPercent;								AST(NAME(BonusPercent))
		// bonus percent, must be > 0  for numerics that are used as a bonus, otherwise not needed

	F32 fPowerHue;
		// change the power hue of any powers on this item.

} ItemDef;

AUTO_STRUCT AST_CONTAINER;
typedef struct ItemContainerInfo
{
	const GlobalType eContainerType;					AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	
	// Reference to the pet represented in this item 
	REF_TO(Entity) hSavedPet;							AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE COPYDICT(EntitySavedPet))

}ItemContainerInfo;

AUTO_STRUCT AST_CONTAINER;
typedef struct ItemDoorKey
{
	CONST_STRING_POOLED pchDoorKey;				AST(PERSIST SUBSCRIBE POOL_STRING)	// Tag used to identify the door
	CONST_STRING_POOLED pchMap;					AST(PERSIST SUBSCRIBE POOL_STRING)	// The map to transfer to when using this key
	CONST_STRING_MODIFIABLE pchMapVars;			AST(PERSIST SUBSCRIBE)				// The map vars to use when transferring
	const Vec3 vPos;							AST(PERSIST SUBSCRIBE VEC3)			// The player's location when this item was created
	CONST_REF_TO(MissionDef) hMission;			AST(PERSIST SUBSCRIBE REFDICT(Mission))
} ItemDoorKey;

AUTO_STRUCT AST_CONTAINER;
typedef struct DyeData
{
	//four channels, RGB
	const U8 DyeColors[12];						AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	CONST_REF_TO(PCMaterialDef) hMat;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
} DyeData;

AUTO_STRUCT AST_CONTAINER;
typedef struct AlgoItemProps //Properties that are commonly algo-ized on items.
{
	const S8 Quality;							AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE SUBTABLE(ItemQualityEnum))
	const S8 MinLevel_UseAccessor;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE NAME(MinLevel))
	const S8 Level_UseAccessor;					AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE NAME(Level))
	const S8 iPowerFactor;						AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	CONST_EARRAY_OF(ItemPowerDefRef) ppItemPowerDefRefs;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	// These are created when the item is created, NOT INDEXED
	
	CONST_OPTIONAL_STRUCT(DyeData) pDyeData;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)

} AlgoItemProps;

AUTO_STRUCT AST_CONTAINER;
typedef struct ItemGemSlotRollResult
{
	// The ID of the item power def ref
	const U32 uItemPowerDefRefID;	AST(KEY PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)

	// The roll result
	const bool bSuccess;			AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
} ItemGemSlotRollResult;

AUTO_STRUCT AST_CONTAINER;
typedef struct ItemGemSlotRollData
{
	// The roll results for each item power def ref
	CONST_EARRAY_OF(ItemGemSlotRollResult) ppRollResults;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
} ItemGemSlotRollData;

AUTO_STRUCT AST_CONTAINER AST_IGNORE_STRUCT(pSlottedItem);
typedef struct ItemGemSlot
{
	//CONST_OPTIONAL_STRUCT(Item) pSlottedItem;	AST(PERSIST SUBSCRIBE STRUCT_NORECURSE)
	CONST_REF_TO(ItemDef) hSlottedItem;			AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	CONST_EARRAY_OF(Power) ppPowers;			AST(PERSIST SUBSCRIBE NO_INDEX LOGIN_SUBSCRIBE)	
	// NOT INDEXED!!!!
	CONST_OPTIONAL_STRUCT(ItemGemSlotRollData) pRollData;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
}ItemGemSlot;

//*******************************************
//if you add anything to this struct, make sure you 
//update item_trh_FreeSpecialPropsIfPossible().
//*****************************************
AUTO_STRUCT AST_CONTAINER;
typedef struct SpecialItemProps //Properties that are used on a much smaller subset of items.
{
	/*
		Durability is commented out because it's not used by any projects.
	*/

//	F32 fDurability;							AST(PERSIST SOMETIMES_TRANSACT SUBSCRIBE LOGIN_SUBSCRIBE)
	// what is current condition
//	const F32 fDurabilityMax;					AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	// what is max condition
	 
	CONST_REF_TO(PlayerCostume) hCostumeRef;					AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(PlayerCostume) NAME(Costume))

	CONST_OPTIONAL_STRUCT(AlgoPet) pAlgoPet;					AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE FORCE_CONTAINER)

	CONST_OPTIONAL_STRUCT(ItemContainerInfo) pContainerInfo;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)

	CONST_OPTIONAL_STRUCT(ItemDoorKey) pDoorKey;				AST(PERSIST SUBSCRIBE)

	CONST_EARRAY_OF(ItemGemSlot) ppItemGemSlots;			AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)

	CONST_OPTIONAL_STRUCT(SuperCritterPet) pSuperCritterPet;	AST(PERSIST SUBSCRIBE FORCE_CONTAINER)
//	CONST_EARRAY_OF(InfuseSlot) ppInfuseSlots;	AST(PERSIST)
	// Filled InfuseSlots, keyed off the index

	CONST_OPTIONAL_STRUCT(ItemTransmutationProps) pTransmutationProps;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE FORCE_CONTAINER)

	CONST_REF_TO(ItemDef) hIdentifiedItemDef;					AST(PERSIST SUBSCRIBE SERVER_ONLY)

//	U8 iWarpChargesUsed;						AST(PERSIST SOMETIMES_TRANSACT)
	//How many of the warp charges have been used
} SpecialItemProps;


AUTO_STRUCT;
typedef struct ItemRewardData
{
	REF_TO(Message) hBroadcastChatMessage;					AST(REFDICT(Message))
		// BroadcastChatMessage: Send a shard chat message when this item is added

	bool bHideInUI;
		// Whether or not this item should be hidden in reward lists on the client
} ItemRewardData;

// This is the instance of the item saved to DB
AUTO_STRUCT AST_CONTAINER AST_IGNORE(bHide);
typedef struct ItemV1
{
		const U64 id;								AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
		//shard wide unique ID per item instance

		CONST_REF_TO(ItemDef) hItem;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(ItemDef))

		F32 fDurability;							AST(PERSIST SOMETIMES_TRANSACT SUBSCRIBE LOGIN_SUBSCRIBE)
		// what is current condition
		const F32 fDurabilityMax;					AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
		// what is max condition

		CONST_EARRAY_OF(Power) ppPowers;			AST(PERSIST SUBSCRIBE NO_INDEX)
		// These are created when the item is created, NOT INDEXED

		CONST_EARRAY_OF(InfuseSlot) ppInfuseSlots;	AST(PERSIST)
		// Filled InfuseSlots, keyed off the index

		const S32 iNumericValue;					AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE NAME(NumericValue))

		// Algorithmic Generation Data
		char *pchDisplayName;						AST(ESTRING NO_NETSEND)

		const S8 Quality;							AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE SUBTABLE(ItemQualityEnum))
		const S8 MinLevel_UseAccessor;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE NAME(MinLevel))
		const S8 Level_UseAccessor;					AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE NAME(Level))
		const S8 iPowerFactor;						AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)

		const U8 flags;								AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE FLAGS SUBTABLE(ItemFlagEnum))
		const U8 numeric_op; 						AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE FLAGS SUBTABLE(NumericOpEnum))

		U8 uSetCount;								AST(SELF_ONLY)
		// Non-persisted cache of the number of equipped Items in this Item's ItemSet.
		//  Generated by a queued remote command any time the UpdateItemSet helper is called.

		CONST_EARRAY_OF(ItemPowerDefRef) ppItemPowerDefRefs;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)

		CONST_REF_TO(PlayerCostume) hCostumeRef;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(PlayerCostume) NAME(Costume))

		CONST_OPTIONAL_STRUCT(AlgoPet) pAlgoPet;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE FORCE_CONTAINER)

		CONST_OPTIONAL_STRUCT(ItemContainerInfo) pContainerInfo; AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
		 
		CONST_OPTIONAL_STRUCT(ItemDoorKey) pDoorKey;			AST(PERSIST SUBSCRIBE)

		CONST_REF_TO(MissionDef) hMission;						AST(PERSIST SUBSCRIBE REFDICT(Mission))

		CONST_EARRAY_OF(ItemGemSlot) ppItemGemSlots;			AST(PERSIST SUBSCRIBE)

		U32 uiTimestamp;										AST(SELF_ONLY)
			// Used to tell if this is a new item

			U32 owner;
		// not persisted; used to manage ownership during team looting; holds owning player's container ID

		U8 iWarpChargesUsed;						AST(PERSIST SOMETIMES_TRANSACT)
			//How many of the warp charges have been used

			U32 bWarpActive : 1;
		// If a warp is active, this flag is true.  Set to false when the warp is canceled or succeeds

		const U32 bTrainingFromItem : 1;			AST(PERSIST)
			// If set, this item is providing training and cannot be removed, traded, auctioned or mailed

			const U32 bSlottedOnItemAssignment : 1;		AST(PERSIST)
			// This item has been slotted on an item assignment. It cannot be removed, traded, auctioned, mailed, or equipped.
			const U32 bAlgo : 1;						AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
			// this is an algorithmic item

			U32 bUnlootable : 1;
		// not persisted; used by Need-or-Greed looting to lock down items that are being rolled on
		U32 bExemptFromLootMode : 1;
		// not persisted; used to identify items that are exempt from the active loot mode.
		U32 bTransactionPending : 1;
		// not persisted; used to signal that this item is in the middle of being given to a player
		U32 bForceBind : 1;
		// not persisted; used by stores to override the bind type of the item
		S8	iPendingTransactionCount;

}ItemV1;

// The item buy back struct
AUTO_STRUCT;
typedef struct ItemBuyBack
{
	// id on this game server, as these fields aren't persisted each game server will start over
	U32 uBuyBackId;					AST(SELF_ONLY KEY)

	Item *pItem;					AST(SELF_ONLY)

	ItemBuyBackStatus eStatus;		AST(SELF_ONLY)
	S32 uBuyBackPrice;				AST(SELF_ONLY)
	const char *pcCurrency;			AST(POOL_STRING SELF_ONLY)

}ItemBuyBack;

// This is the instance of the item saved to DB
AUTO_STRUCT AST_CONTAINER AST_IGNORE(bHide);
typedef struct Item
{
	const U64 id;								AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	//shard wide unique ID per item instance

	CONST_REF_TO(ItemDef) hItem;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(ItemDef))

	// Algorithmic Generation Data
	char *pchDisplayName;						AST(ESTRING NO_NETSEND)

	CONST_OPTIONAL_STRUCT(AlgoItemProps) pAlgoProps;		AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	CONST_OPTIONAL_STRUCT(SpecialItemProps) pSpecialProps;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)

	CONST_EARRAY_OF(Power) ppPowers;			AST(PERSIST SUBSCRIBE NO_INDEX)

	const U8 flags;								AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE FLAGS SUBTABLE(ItemFlagEnum))

	const int count;							AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE DEFAULT(1))

	S8 numeric_op;

	ItemRewardData* pRewardData;							AST(SERVER_ONLY)
		// Meta-data about reward items.
		// This is reward-specific data and should probably be refactored at some point (see RewardEntry->msgBroadcastChatMessage)

	U8 uSetCount;								AST(SELF_ONLY)
	// Non-persisted cache of the number of equipped Items in this Item's ItemSet.
	//  Generated by a queued remote command any time the UpdateItemSet helper is called.

	REF_TO(Message) hBroadcastChatMessage;					AST(REFDICT(Message) SERVER_ONLY)
	// BroadcastChatMessage: Send a shard chat message when this item is added
	// This is reward-specific data and should probably be refactored at some point (see RewardEntry->msgBroadcastChatMessage)

	U32 owner;
	// not persisted; used to manage ownership during team looting; holds owning player's container ID

	U32 bWarpActive : 1;
	// If a warp is active, this flag is true.  Set to false when the warp is canceled or succeeds

	U32 bUnlootable : 1;
	// not persisted; used by Need-or-Greed looting to lock down items that are being rolled on
	U32 bExemptFromLootMode : 1;
	// not persisted; used to identify items that are exempt from the active loot mode.
	U32 bTransactionPending : 1;
	// not persisted; used to signal that this item is in the middle of being given to a player
	U32 bForceBind : 1;
	// not persisted; used by stores to override the bind type of the item
	S8	iPendingTransactionCount;
	// not persisted. This is the count of pending transactions. This number is used for splitting numerics between team members.

}Item;



AUTO_STRUCT;
typedef struct GiveRewardBagsData
{
	InventoryBag **ppRewardBags;	AST(NO_INDEX)
	char **ppChoices;
} GiveRewardBagsData;

// InventorySlotReference is just a bag ID and slot index, 
// not a reference like in the REF system. 
AUTO_STRUCT;
typedef struct InventorySlotReference
{
	InvBagIDs eBagID;		AST(DEFAULT(InvBagIDs_None))
		S32 iIndex;				AST(DEFAULT(-1))
} InventorySlotReference;


AUTO_STRUCT;
typedef struct ExperimentEntry
{
	int SrcBagId;
	int SrcSlot;
	int count;

	int iEPValue;
		// populated on server immediately before invoking transaction
} ExperimentEntry;

AUTO_STRUCT;
typedef struct ExperimentData
{
	ExperimentEntry **ppEntry;
} ExperimentData;

AUTO_STRUCT;
typedef struct ExperimentRanges
{
	int *eaiRanges;
} ExperimentRanges;

AUTO_STRUCT;
typedef struct CraftData
{
	ItemQuality eQuality;
	char *pcBaseItemRecipeName;
	char **eaItemPowerRecipes;
} CraftData;

AUTO_STRUCT;
typedef struct CraftSkillupRanges
{
	int *eaiRanges;
} CraftSkillupRanges;

// These structures are used to load the default deconstruction items
AUTO_STRUCT;
typedef struct ItemDeconstructSkillDefault
{
	SkillType eSkillType;							AST(STRUCTPARAM)
	REF_TO(ItemDef) hPrimaryRecipe;					AST(NAME(PrimaryRecipe))
	REF_TO(ItemDef) hSecondaryRecipe;				AST(NAME(SecondaryRecipe))
	REF_TO(ItemDef) hDeviceRecipe;					AST(NAME(DeviceRecipe))
} ItemDeconstructSkillDefault;

AUTO_STRUCT;
typedef struct ItemDeconstructDefaults
{
	ItemDeconstructSkillDefault **peaSkillDefaults;	AST(NAME(SkillDefault))
} ItemDeconstructDefaults;

// Structure used to define a mapping of type to display name and fast index id
AUTO_STRUCT;
typedef struct ItemSortType
{
	REF_TO(Message) hNameMsg; AST(NAME(NameMsg))
	U32 iSortID; AST(NAME(SortID) KEY)

	bool bSearchable; // shows up in marketplace search
	bool bLevelIgnored; // if true ignore level during search

	ItemType eType;

	// A second item type that can match this entry.  A hacky solution to having two
	//  bridge officer item types in STO, which we can fix when revamping the auction
	//  system after launch.
	ItemType eType2;

	InvBagIDs eRestrictBagID; 
	SlotType eRestrictSlotType;

	// The item category used to match the sort type with the item
	ItemCategory eItemCategory;
} ItemSortType;

AUTO_STRUCT;
typedef struct ItemSortTypeCategory
{
	const char *pchName;			AST(POOL_STRING KEY)

	// The display name
	REF_TO(Message) hNameMsg;		AST(NAME(NameMsg))

	// The item sort types
	U32 *eaiItemSortTypes;			AST(NAME(ItemSortTypes))

} ItemSortTypeCategory;

AUTO_STRUCT;
typedef struct ItemSortTypes
{
	ItemSortTypeCategory **ppItemSortTypeCategories;	AST(NAME(ItemSortTypeCategory) NO_INDEX)
	ItemSortType **ppItemSortType;						AST(NO_INDEX)
	ItemSortType **ppIndexedItemSortTypes;				AST(UNOWNED NO_TEXT_SAVE)	
	const char *pchFileName;							AST(CURRENTFILE)
} ItemSortTypes;

AUTO_STRUCT;
typedef struct ItemAliasDisplay
{
	DisplayMessage DisplayNameMessage;	AST(STRUCT(parse_DisplayMessage))
	//Expression *pExprRequires;			AST(LATEBIND NAME(ExpressionBlock) REDUNDANT_STRUCT(ExprRequires, parse_Expression_StructParam))
	REF_TO(AllegianceDef) hRequiredAllegiance; AST(NAME(RequiredAllegiance))
}ItemAliasDisplay;

AUTO_STRUCT;
typedef struct ItemAlias
{
	char *pchAlias;					AST(KEY STRUCTPARAM NAME(Alias))
	REF_TO(ItemDef) hItem;			AST(NAME(Item))
	ItemAliasDisplay **ppChoices;	AST(NAME(Choice))
}ItemAlias;

AUTO_STRUCT;
typedef struct ItemAliasLookup
{
	ItemAlias **ppAlias;
	const char *pchFileName; AST(CURRENTFILE)
}ItemAliasLookup;

//
// This struct provides a mechanism to override the default behavior of sticking
//  acquired items into the player's main inventory.
// It will instead put the item in the bag indicated by the ItemDef's eRestrictBagID.
//
AUTO_STRUCT;
typedef struct ItemAcquireOverride
{
	ItemType itemType;				AST(KEY)
	bool fromMail;					AST(BOOLFLAG)
	bool fromTrade;					AST(BOOLFLAG)
	bool fromStore;					AST(BOOLFLAG)
	bool fromAuction;				AST(BOOLFLAG)
	bool fromGameAction;			AST(BOOLFLAG)
	bool fromMissionReward;			AST(BOOLFLAG)

	InvBagIDs ePreferredRestrictBag; AST(NAME(PreferredRestrictBag) SUBTABLE(InvBagIDsEnum))

	REF_TO(Message) bagFullError;					AST(NAME("BagFullError"))
} ItemAcquireOverride;

AUTO_STRUCT;
typedef struct ItemHandlingConfig
{
	// override default behavior of putting items received from various sources into the inventory bag
	EARRAY_OF(ItemAcquireOverride) overrideList;	AST(NAME("AcquireOverride"))

	// the contents of these bags show up in the context menu when selecting items to trade, add to auction house, etc.
	INT_EARRAY tradeableBags;						AST(NAME("TradeableBag") SUBTABLE(InvBagIDsEnum))
}ItemHandlingConfig;

AUTO_STRUCT;
typedef struct ItemTimestampData 
{
	Entity *pEnt;				AST(UNOWNED)
	ItemDef *pItemDef;			AST(UNOWNED)
	U32 uiTime;			
} ItemTimestampData;

AUTO_STRUCT;
typedef struct ItemRewardPackRequestData
{
	REF_TO(ItemDef) hRewardPackItem;
	ItemQuality ePackResultQuality;
	InvRewardRequest* pRewards;
} ItemRewardPackRequestData;

AUTO_STRUCT;
typedef struct ItemRestrictBagToFxMap
{
	InvBagIDs eID;	AST(STRUCTPARAM)
	SlotType eSlotType;	AST(STRUCTPARAM)
	const char* pchFXName;	AST(STRUCTPARAM)
}ItemRestrictBagToFxMap;

AUTO_STRUCT;
typedef struct ItemHeadshotStyleConfig
{
	const char* pchHeadshotStyleDef;	AST(STRUCTPARAM)
	InvBagIDs eRestrictBag;	AST(NAME(RestrictBag))
	ItemCategory* eaCategories;	AST(NAME(Category))
} ItemHeadshotStyleConfig;

AUTO_STRUCT;
typedef struct ItemConfig
{
	// When this is enable, the items have uniquely definable item power def refs on them
	U32 bUseUniqueIDsForItemPowerDefRefs : 1;	AST(NAME(UseUniqueIDsForItemPowerDefRefs))
	EARRAY_OF(ItemRestrictBagToFxMap) eaBagToFxMaps; AST(NAME(ItemTypeEquippedFX))

	Expression *pItemTransmuteCost;		AST(LATEBIND NAME(ItemTransmuteCost))
	// Defines an expression to determine the cost of transmuting two items.
	const char* pchTransmuteCurrencyName;	AST(NAME(ItemTransmuteCurrency) POOL_STRING)

	EARRAY_OF(ItemHeadshotStyleConfig) eaHeadshotStyleConfigs;		AST(NAME(HeadshotStyleConfig))
} ItemConfig;

// Global settings for the item system
extern ItemConfig g_ItemConfig;

void item_Load(void);
void itemtags_Load(void);

bool item_ValidateAllButRefs(ItemDef *pDef);
bool item_ValidateRefs(ItemDef *pDef);
bool item_Validate(ItemDef *pDef);

bool itempower_Validate(ItemPowerDef *pDef);
// if the power category is out of this range, then it doesn't correspond to a power, but rather it's something
// special that we are just going to handle, like the power factor
bool itempower_IsRealPower(ItemPowerDef * pItemPowerDef);

SA_RET_OP_VALID ItemDef* item_DefFromName(SA_PARAM_NN_STR const char *pchName);
bool item_MatchAnyRestrictBagIDs(SA_PARAM_NN_VALID ItemDef* pItemDef, InvBagIDs* peRestrictBagIDs);
void item_trh_FillInPreSlottedGems(ATH_ARG NOCONST(Item) *pItem, int iCharacterLevel, const char *pcRank, AllegianceDef *pAllegiance, U32 *pSeed);
bool item_trh_FixupPowers( ATH_ARG NOCONST(Item) *pItem );
void item_trh_RemovePowers( ATH_ARG NOCONST(Item) *pItem );
void item_trh_ResetPowerLifetimes(ATH_ARG NOCONST(Item)* pItem);
void item_trh_UpdateDurability( ATH_ARG NOCONST(Item) *pItem, int iLevel);
int item_trh_FixupItemGemSlots(ATH_ARG NOCONST(Item) *pItem);
void item_trh_FixupAlgoProps(ATH_ARG NOCONST(Item)* pItem);
	void item_trh_FixupSpecialProps(ATH_ARG NOCONST(Item)* pItem);
int itemGem_GetUnslotCostFromGemIdx(const char *pchCurrency,  Entity *pEnt, Item *pHolderItem, int iGemIdx);
int itemGem_GetUnslotCostFromGemDef(const char *pchCurrency, Entity *pEnt, Item *pHolderItem, SA_PARAM_OP_VALID ItemDef *pGemDef);
ItemDef *item_GetSlottedGemItemDef(SA_PARAM_NN_VALID Item *pHolderItem, S32 iSlot);

bool item_trh_SimpleClampLow(ATH_ARG NOCONST(Item)* pItem, ItemDef *pDef);
bool item_trh_SimpleClampHigh(ATH_ARG NOCONST(Item)* pItem, ItemDef *pDef, int *piOverflow);

ItemEquipLimitCategoryData* item_GetEquipLimitCategory(ItemEquipLimitCategory eCategory);

// Returns a Power owned by the Player's inventory, otherwise returns NULL
SA_ORET_OP_VALID NOCONST(Power)* item_trh_FindPowerByID(SA_PARAM_NN_VALID NOCONST(Entity) *pEnt, U32 uiID, InvBagIDs *pBagID, int *pSlotIdx, Item **ppItem, S32 *piItemPowerIdx );
#define item_FindPowerByIDNoConst(pEnt, uiID, pBagID, pSlotIdx, ppItem, piItemPowerIdx) CONTAINER_RECONST(Power, item_trh_FindPowerByID((pEnt), uiID, pBagID, pSlotIdx, ppItem, piItemPowerIdx))
#define item_FindPowerByID(pEnt, uiID, pBagID, pSlotIdx, ppItem, piItemPowerIdx) item_FindPowerByIDNoConst(CONTAINER_NOCONST(Entity, (pEnt)), uiID, pBagID, pSlotIdx, ppItem, piItemPowerIdx)

void item_trh_ClearPowerIDs(ATH_ARG NOCONST(Item)*pItem);
#define item_ClearPowerIDs(pItem) item_trh_ClearPowerIDs(CONTAINER_NOCONST(Item, (pItem)))
void item_trh_FixupPowerIDs(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Item)*pItem);
void item_FixMessages(ItemDef *pItemDef);
void itempower_FixMessages(ItemPowerDef* pItemPowerDef);

// Walks the Players's inventory and adds all the available Powers to the Character's general Powers list
//  Returns false if it had trouble because of missing references.
int item_AddPowersFromItems(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pEnt, GameAccountDataExtract *pExtract);

// Get the EP value of an item
// DO NOT CALL THESE IN TRANSACTIONS!!!
// They do expression evaluation, which isn't transaction safe
S32 item_GetEPValue(int iPartitionIdx, Entity *pEnt, Item *pItem);
S32 item_GetStoreEPValue(int iPartitionIdx, Entity *pEnt, Item *pItem, StoreDef *pStore);
S32 item_GetDefEPValue(int iPartitionIdx, Entity *pEnt, ItemDef *pDef, S32 iLevel, ItemQuality eQuality);
S32 itempower_GetEPValue(int iPartitionIdx, Entity *pEnt, ItemPowerDef *pDef, ItemDef *pItemDef, S32 iLevel, ItemQuality eQuality);
int item_GetResourceValue(int iPartitionIdx, Entity* pEnt, Item *pItem, const char *pchResources);

// Appends local enhancements on the item to the earray
void item_GetEnhancementsLocal(SA_PARAM_NN_VALID Item *pItem, SA_PARAM_NN_VALID Power ***pppPowersAttached);

// Attaches enhancements on the same item as the power, if they are "local" enhancements.  Global enhancements
//  would already be attached.
void power_GetEnhancementsLocalItem(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID Power *ppow, SA_PARAM_NN_VALID Power ***pppPowersAttached);

// Walks the inventory looking for Powers that have the limited use flag
void item_UpdateLimitedUsePowers(SA_PARAM_NN_VALID Entity *pEnt, GameAccountDataExtract *pExtract);

LATELINK;
void GameSpecific_HandleInventoryChangeNumeric(Entity* pEnt, const char* pchNumeric, bool bPreCommit);

void item_HandleInventoryChangeNumeric(Entity* pEnt, const char* pchNumeric, bool bPreCommit);
void item_HandleInventoryChangeLevelNumeric(Entity* pEnt, bool bPreCommit);

bool bag_trh_IsEquipBag(ATH_ARG NOCONST(Entity) * pEnt, InvBagIDs BagID, GameAccountDataExtract *pExtract);
bool bag_IsEquipBag(const Entity *pEnt, InvBagIDs BagID, GameAccountDataExtract *pExtract);
bool bag_IsWeaponBag(const Entity * pEnt, InvBagIDs BagID, GameAccountDataExtract *pExtract);
bool bag_IsNoModifyInCombatBag( const Entity* pEnt, InvBagIDs BagID, GameAccountDataExtract *pExtract);

SA_RET_NN_STR const char *item_GetIconName(SA_PARAM_OP_VALID Item* pItem, SA_PARAM_OP_VALID ItemDef *pDef);

int item_CountOwned(Entity *e, ItemDef *pDef);

void item_GetNameFromUntranslatedStrings(Language eLangID, bool bTranslate, const char** ppchNames, char** pestrOut);
// This extended version of the function does the gender translation for you, and also translates any variables
// that are valid to have in item text
void item_GetFormattedNameFromUntranslatedStrings(Entity * pEnt,ItemDef * pItemDef,Item * pItem,Language eLangID, bool bTranslate, const char** ppchNames, char** pestrOut);

bool item_ShouldGetGenderSpecificName(ItemDef* pItemDef);
void item_trh_GetNameUntranslated(ATH_ARG NOCONST(Item)* pItem, 
								  ATH_ARG NOCONST(Entity)* pEnt, 
								  const char*** peaNames,
								  bool* pbIsMessageKey);
const char* item_GetNameLangInternal(Item* pItem, Language eLangID, Entity* pEnt); 
#define item_GetNameLang(pItem, langID, pEnt) item_GetNameLangInternal(pItem, langID, pEnt)
#define item_GetName(pItem, pEnt) item_GetNameLangInternal(pItem, locGetLanguage(getCurrentLocale()), pEnt)
const char* itemdef_GetNameLangInternal(char** pestrDisplayName, ItemDef* pItemDef, Language eLangID, Entity* pEnt);
#define itemdef_GetNameLang(pestrDisplayName, pItemDef, langID, pEnt) itemdef_GetNameLangInternal(pestrDisplayName, pItemDef, langID, pEnt)
#define itemdef_GetName(pestrDisplayName, pItemDef, pEnt) itemdef_GetNameLangInternal(pestrDisplayName, pItemDef, locGetLanguage(getCurrentLocale()), pEnt)
bool item_GetDisplayNameFromPetCostume(Entity* pPlayerEnt, Item* pItem, char** pestrName, PlayerCostume** ppCostume);

const char* item_trh_GetDefLocalNameKeyFromEnt(SA_PARAM_NN_VALID const ItemDef* pDef, ATH_ARG NOCONST(Entity)* pEntity);
const char* item_GetDefLocalNameFromEnt(SA_PARAM_NN_VALID const ItemDef* pDef, Entity* pEntity, Language langID);

const char* item_GetDefLocalName(const ItemDef* pDef, Language langID);

bool item_IsUpgrade(SA_PARAM_NN_VALID ItemDef *pItemDef);
bool item_IsPrimaryUpgrade(SA_PARAM_NN_VALID ItemDef *pItemDef);
bool item_IsSecondaryUpgrade(SA_PARAM_NN_VALID ItemDef *pItemDef);
bool item_IsPrimaryEquip(ItemDef *pItemDef);
bool item_IsSecondaryEquip(ItemDef *pItemDef);
bool item_IsRecipe(SA_PARAM_NN_VALID ItemDef *pItemDef);
bool item_IsMission(SA_PARAM_NN_VALID ItemDef *pItemDef);
bool item_IsMissionGrant(SA_PARAM_NN_VALID ItemDef *pItemDef);
bool item_IsSavedPet(SA_PARAM_NN_VALID ItemDef *pItemDef);
bool item_isAlgoPet(SA_PARAM_NN_VALID ItemDef *pItemDef);
bool item_IsAttributeModify(SA_PARAM_NN_VALID ItemDef *pItemDef);
bool item_IsVanityPet(ItemDef *pItemDef);
bool item_IsCostumeUnlock(ItemDef *pItemDef);
bool item_IsInjuryCureGround(ItemDef *pItemDef);
bool item_IsInjuryCureSpace(ItemDef *pItemDef);
bool item_IsRewardPack(ItemDef *pItemDef);
bool item_IsMicroSpecial(ItemDef *pItemDef);
bool item_IsExperienceGift(ItemDef *pItemDef);

// Returns the def durability at the given level.  If Level is 0, uses the Level specified in the def.
F32 item_GetDefDurability(ItemDef *pItemDef, int Level);

bool itemdef_CheckEquipLimit(Entity *pEnt, ItemDef *pItemDef);
bool itemdef_CheckRequiresExpression(int iPartitionIdx, Entity *pOwner, Entity *pEnt, ItemDef *pItemDef, const char **ppchDisplayTextMsg, int iDestinationSlot);
bool itemdef_trh_VerifyUsageRestrictionsLevel(ATR_ARGS, UsageRestriction *pRestrict,  ATH_ARG NOCONST(Entity) *pOwner, ItemDef *pItemDef, int iOverrideLevel, const char **ppchDisplayTextMsg);
bool itemdef_trh_VerifyUsageRestrictionsSkillLevel(ATR_ARGS, UsageRestriction *pRestrict, ATH_ARG NOCONST(Entity) *pOwner, ItemDef *pItemDef, const char **ppchDisplayTextMsg);
bool itemdef_trh_VerifyUsageRestrictionsClass(ATR_ARGS, UsageRestriction *pRestrict, ATH_ARG NOCONST(Entity) *pEnt, const char **ppchDisplayTextMsg);
bool itemdef_trh_VerifyUsageRestrictionsCharacterPath(ATR_ARGS, UsageRestriction *pRestrict, ATH_ARG NOCONST(Entity) *pEnt, const char **ppchDisplayTextMsg);
bool itemdef_trh_VerifyUsageRestrictionsAllegiance(ATR_ARGS, UsageRestriction *pRestrict, ATH_ARG NOCONST(Entity) *pOwner, ATH_ARG NOCONST(Entity) *pEnt, const char **ppchDisplayTextMsg);
bool itemdef_trh_VerifyUsageRestrictions(ATR_ARGS, ATH_ARG NOCONST(Entity) *pOwner, ATH_ARG NOCONST(Entity) *pEnt, ItemDef *pItemDef, int iOverrideLevel, const char **ppchDisplayTextMsg);
bool itemdef_VerifyUsageRestrictions(int iPartitionIdx, Entity *pEnt, ItemDef *pItemDef, int iOverrideLevel, const char **ppchDisplayTextMsg, int iDestinationSlot);

SkillType entity_GetSkill(Entity *pEnt);
S32 entity_GetSkillLevel(const Entity *pEnt);
bool entity_HasSkill(SA_PARAM_OP_VALID Entity *pEnt, SkillType eType);



// Returns the total number of ItemPowerDefs on the Item instance
int item_trh_GetNumItemPowerDefs(ATH_ARG NOCONST(Item) *pItem, bool bIncludeGems);
ItemPowerDefRef* item_trh_GetItemPowerDefRef(ATH_ARG NOCONST(Item)* pItem, int PowerIdx);
#define item_GetItemPowerDefRefNoConst(pItem, PowerIdx) item_trh_GetItemPowerDefRef((pItem), PowerIdx)
#define item_GetItemPowerDefRef(pItem, PowerIdx) item_GetItemPowerDefRefNoConst(CONTAINER_NOCONST(Item, (pItem)), PowerIdx)

#define item_GetNumItemPowerDefsNoConst(pItem, bIncludeGems) item_trh_GetNumItemPowerDefs(pItem, bIncludeGems)
#define item_GetNumItemPowerDefs(pItem, bIncludeGems) item_GetNumItemPowerDefsNoConst(CONTAINER_NOCONST(Item, (pItem)), bIncludeGems)

int item_trh_GetNumGemsPowerDefs(ATH_ARG NOCONST(Item) *pItem);
#define item_GetNumGemsPowerDefs(pItem) item_trh_GetNumGemsPowerDefs(CONTAINER_NOCONST(Item, (pItem)))

// Returns the specific ItemPowerDef on the Item instance
ItemPowerDef* item_trh_GetItemPowerDef(ATH_ARG NOCONST(Item)* pItem, int PowerIdx);
#define item_GetItemPowerDefNoConst(pItem, PowerIdx) item_trh_GetItemPowerDef((pItem), PowerIdx)
#define item_GetItemPowerDef(pItem, PowerIdx) item_GetItemPowerDefNoConst(CONTAINER_NOCONST(Item, (pItem)), PowerIdx)

// Returns the scale factor of the specific ItemPowerDefRef on the Item instance
F32 item_GetItemPowerScale(Item *pItem, int PowerIdx);

// Returns the specific PowerDef on the Item instance
SA_RET_OP_VALID PowerDef* item_GetPowerDef(SA_PARAM_NN_VALID Item *pItem, int PowerIdx);

// Returns the Power* on an Item instance associated with a particular PowerIdx
NOCONST(Power)* item_trh_GetPower(ATH_ARG NOCONST(Item) *pItem, int PowerIdx);
#define item_GetPowerNoConst(pItem, PowerIdx) (CONTAINER_RECONST(Power, item_trh_GetPower((pItem), PowerIdx)))
#define item_GetPower(pItem, PowerIdx) item_GetPowerNoConst(CONTAINER_NOCONST(Item, (pItem)), PowerIdx)



// Returns the ItemDef's (NOT Item instance) ItemPowerDef
ItemPowerDef* itemdef_GetItemPowerDef(ItemDef *pItemDef, int PowerIdx);

// Returns the ItemGemSlotDef where the power index is slotted
ItemGemSlotDef* GetGemSlotDefFromPowerIdx(Item *pItem, int PowerIdx);

// Finds the *first* ItemPowerDef on the Item that provides the given PowerDef
SA_RET_OP_VALID ItemPowerDef* item_GetItemPowerDefByPowerDef(SA_PARAM_NN_VALID Item *pItem, SA_PARAM_NN_VALID PowerDef *pPowerDef);

// Returns true if the passed ItemDef has any of the categories in peItemCategories
bool itemdef_HasItemCategory(ItemDef* pItemDef, ItemCategory* peItemCategories);
// Returns true if the passed ItemDef has all of the categories in peItemCategories
bool itemdef_HasAllItemCategories(ItemDef* pItemDef, ItemCategory* peItemCategories);

// Returns true if the passed ItemDef has all of the restrict bags in peRestrictBagIDs
bool itemdef_HasAllRestrictBagIDs(ItemDef* pItemDef, const InvBagIDs * const peRestrictBagIDs);

bool item_ItemPowerActive(Entity* pEnt, InventoryBag* pBag, Item* pItem, int PowerIdx);
bool item_AreAnyPowersInUse(Entity* pEnt, Item* pItem);


// InfuseSlot utility functions

// Gets the InfuseSlotDef on the ItemDef at the given index
SA_RET_OP_VALID InfuseSlotDef* item_GetInfuseSlotDef(SA_PARAM_NN_VALID ItemDef *pItemDef, S32 iInfuseSlot);

// Gets the InfuseOption for the InfuseSlotDef given the option Item
SA_RET_OP_VALID InfuseOption* infuse_GetDefOption(SA_PARAM_NN_VALID InfuseSlotDef *pInfuseDef, SA_PARAM_NN_VALID ItemDef *pItemOption);

// Returns the current InfuseRank of the InfuseSlot
SA_RET_OP_VALID InfuseRank* infuse_trh_GetRank(SA_PARAM_NN_VALID NOCONST(InfuseSlot) *pInfuseSlot);

// Returns the icon for the InfuseSlotDef, optionally overriding it with a specific one for the option
//  and rank of an actual InfuseSlot.
SA_RET_OP_STR const char* infuse_GetIcon(SA_PARAM_NN_VALID InfuseSlotDef *pInfuseSlotDef, SA_PARAM_OP_VALID InfuseSlot *pInfuseSlot);

TradeErrorType item_GetTradeError(Item *pItem);
bool item_CanTrade(SA_PARAM_OP_VALID Item *pItem);
bool item_trh_CanRemoveItem(ATH_ARG NOCONST(Item)* pItem);
#define item_CanRemoveItem(pItem) item_trh_CanRemoveItem(CONTAINER_NOCONST(Item, (pItem)))

CostumeDisplayData * item_GetCostumeDisplayData( int iPartitionIdx, Entity *pEnt, Item *pItem, ItemDef *pItemDef, SlotType eEquippedSlot, Item* pFakeDye, int iChannel );
// Returns true if the entity and the inventory exists and there are no items we are interested that's not completely downloaded from the server
bool item_GetItemCostumeDataToShow(int iPartitionIdx, const Entity *pEnt, CostumeDisplayData ***peaData, GameAccountDataExtract *pExtract);

int item_GetLevelingNumeric(const Entity *pEnt);

int item_trh_GetLevelingNumeric(ATR_ARGS,ATH_ARG NOCONST(Entity) *pEnt);
bool item_idCheck(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Item) *pItem, GameAccountDataExtract *pExtract);
void item_trh_SetItemID(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Item) *pItem);
ItemIDType item_GetIDTypeFromID(U64 uItemID);

S32 item_GetComponentCount(ItemCraftingComponent *pComponent);
void item_GuessAtMoveCounts(int iCount, int iSrcStackCount, int iDstStackCount, ItemDef *pSrcItemDef, ItemDef *pDstItemDef, int *piSrcMoveCount, int *piDstMoveCount);
bool item_ItemMoveValid(Entity *pEnt, ItemDef *pItemDef, bool bSrcGuild, S32 iSrcBagID, S32 iSrcSlot, bool bDstGuild, S32 iDstBagID, S32 iDstSlot, GameAccountDataExtract *pExtract);
bool item_ItemMoveValidWithCount(Entity *pEnt, ItemDef *pItemDef, int iCount, bool bSrcGuild, S32 iSrcBagID, S32 iSrcSlot, bool bDstGuild, S32 iDstBagID, S32 iDstSlot, GameAccountDataExtract *pExtract);
bool item_ItemMoveDestValid(Entity *pEnt, ItemDef *pItemDef, const Item *pItem, bool bGuild, S32 iDstBagID, S32 iDestSlot, bool bSilent, GameAccountDataExtract *pExtract);
bool item_ItemMoveValidAcrossEnts(Entity* pPlayerEnt, Entity* pEntSrc, ItemDef* pItemDef, bool bSrcGuild, S32 SrcBagID, S32 iSrcSlot, Entity* pEntDst, bool bDstGuild, S32 DestBagID, S32 iDestSlot, GameAccountDataExtract *pExtract);
bool item_ItemMoveValidAcrossEntsWithCount(Entity* pPlayerEnt, Entity* pEntSrc, ItemDef* pItemDef, int iCount, bool bSrcGuild, S32 SrcBagID, S32 iSrcSlot, Entity* pEntDst, bool bDstGuild, S32 DestBagID, S32 iDestSlot, GameAccountDataExtract *pExtract);
bool item_ItemMoveValidTemporaryPuppetCheck(Entity* pEnt, InventoryBag* pBag);

bool item_GetAlgoIngredients(ItemDef *pRecipe, ItemCraftingComponent ***peaComponents, int iPowerGroup, U32 iLevel, ItemQuality eQuality);
void item_GetDeconstructionComponents(SA_PARAM_OP_VALID Item *pItem, ItemCraftingComponent ***peaComponents);
void itemeval_Eval(int iPartitionIdx, Expression *pExpr, ItemDef *pDef, ItemPowerDef *pItemPowerDef, Item *pItem, SA_PARAM_NN_VALID Entity *pEnt, S32 iLevel, ItemQuality eQuality, S32 iEP, const char *pcFilename, int iDestinationSlot, MultiVal * pResult);
void itemeval_EvalGem(int iPartitionIdx, Expression *pExpr, ItemDef *pDef, ItemPowerDef *pItemPowerDef, Item *pItem, ItemDef *pGemDef, ItemDef *pAppearanceDef, Entity *pEnt, S32 iLevel, ItemQuality eQuality, S32 iEP, const char *pcFilename, S32 iDestinationSlot, MultiVal * pResult);
int itemeval_GetIntResult(MultiVal * pResult, const char *pcFilename, Expression *pExpr);
float itemeval_GetFloatResult(MultiVal * pResult, const char *pcFilename, Expression *pExpr);
S32 itemeval_GetTransmutationCost(int iPartitionIdx, Entity *pEnt, Item* pStats, Item* pAppearance);
F32 itemeval_GetItemMaxOfRechargeAndCooldown(SA_PARAM_OP_VALID Item *pItem);
S32 itemeval_GetItemChargesAndCount(SA_PARAM_OP_VALID Item *pItem);
S32 itemeval_GetItemChargesMax(SA_PARAM_OP_VALID Item *pItem);


ItemSortType *item_GetSortTypeForTypes(ItemType eType, const InvBagIDs * const peaiRestrictBagIDs, SlotType eRestrictSlotType, const ItemCategory * const peaiItemCategories);
ItemSortType *item_GetSortTypeForID(U32 itemSortID);
void item_GetSearchableSortTypes(ItemSortType ***pppTypes); // Adds the actual things, not copies

bool Item_ItemTagRequiresSpecialization(ItemTag eItemTag);

//Determines if the player may use the specified device
bool item_IsDeviceUsableByPlayer(Entity *pEnt, Item *pItem, InventoryBag *pBag);
//Determines if the player may use the item's specified power
bool item_isPowerUsableByPlayer(Entity *pEnt, Item *pItem, InventoryBag *pBag, S32 iPower);


F32 item_GetLongestPowerCooldown(Item* pItem);

// Returns the string that should be used to describe an item in all logs
SA_RET_NN_STR const char *item_GetLogString(SA_PARAM_OP_VALID const Item *pItem);

// Creates a chat link given a translated message string that contains the item link text and an Item
SA_RET_OP_VALID ChatLinkInfo *item_CreateChatLinkInfoFromMessage(SA_PARAM_NN_STR const char *pchMessage, SA_PARAM_NN_STR const char *pchItemText, SA_PARAM_NN_VALID Item *pItem);

DisplayMessage entity_GetItemDisplayNameMessage(NOCONST(Entity) *pEntity,const ItemDef *pItemDef);

const char *itemHandling_GetErrorMessage(ItemDef *pItemDef);

// check to see if a particular item should be put in a bag other than inventory when acquired from various sources
InvBagIDs itemAcquireOverride_FromStore(ItemDef *itemDef);
InvBagIDs itemAcquireOverride_FromTrade(ItemDef *itemDef);
InvBagIDs itemAcquireOverride_FromAuction(ItemDef *itemDef);
InvBagIDs itemAcquireOverride_FromMail(ItemDef *itemDef);
InvBagIDs itemAcquireOverride_FromGameAction(ItemDef *itemDef);
InvBagIDs itemAcquireOverride_FromMissionReward(ItemDef *itemDef);
InvBagIDs itemAcquireOverride_GetPreferredRestrictBag(ItemDef *itemDef);

// check and see if the bag's contents should show up in the trade context menu
bool itemHandling_IsBagTradeable(InvBagIDs bagID);

// check to determine if the src/dst item is or may be unique
bool item_MoveRequiresUniqueCheck(Entity *pSrcEnt, bool bSrcGuild, S32 iSrcBagID, S32 iSrcSlot, Entity *pDstEnt, bool bDstGuild, S32 iDstBagID, S32 iDstSlot, GameAccountDataExtract *pExtract);

// Get an item's level
int item_trh_GetLevel(ATH_ARG NOCONST(Item)* pItem);
#define item_GetLevel(pItem) item_trh_GetLevel(CONTAINER_NOCONST(Item, pItem))
int item_trh_GetGemPowerLevel(ATH_ARG NOCONST(Item) *pHolder);
#define item_GetGemPowerLevel(pHolder) item_trh_GetGemPowerLevel(CONTAINER_NOCONST(Item,pHolder))

// Get an item's minimum level
int item_trh_GetMinLevel(ATH_ARG NOCONST(Item)* pItem);
#define item_GetMinLevel(pItem) item_trh_GetMinLevel(CONTAINER_NOCONST(Item, pItem))

// Set an item's level
void item_trh_SetAlgoPropsLevel(ATH_ARG NOCONST(Item)* pItem, int iLevel);

// Set an item's minimum level
void item_trh_SetAlgoPropsMinLevel(ATH_ARG NOCONST(Item)* pItem, int iLevel);

// Set an item's count
void item_trh_SetCount(ATH_ARG NOCONST(Item)* pItem, int iCount);

ItemQuality item_trh_GetQuality(ATH_ARG NOCONST(Item)* pItem);
#define item_GetQuality(pItem) item_trh_GetQuality(CONTAINER_NOCONST(Item, pItem))

// Returns the weapon damage multiplier given the quality
F32 item_GetWeaponDamageMultiplierByQuality(ItemQuality eQuality);

// Returns the chat link color name given the quality
const char* item_GetChatLinkColorNameByQuality(ItemQuality eQuality);

void item_trh_SetAlgoPropsQuality(ATH_ARG NOCONST(Item)* pItem, ItemQuality iQuality);

int item_trh_GetPowerFactor(ATH_ARG NOCONST(Item)* pItem);
#define item_GetPowerFactor(pItem) item_trh_GetPowerFactor(CONTAINER_NOCONST(Item, pItem))

void item_trh_SetAlgoPropsPowerFactor(ATH_ARG NOCONST(Item)* pItem, int iPowFactor);

typedef struct AlgoItemProps_AutoGen_NoConst AlgoItemProps_AutoGen_NoConst;
typedef struct SpecialItemProps_AutoGen_NoConst SpecialItemProps_AutoGen_NoConst;

NOCONST(AlgoItemProps)* item_trh_GetOrCreateAlgoProperties(ATH_ARG NOCONST(Item)* pItem);
NOCONST(SpecialItemProps)* item_trh_GetOrCreateSpecialProperties(ATH_ARG NOCONST(Item)* pItem);

Item* InvSlotRef_GetItem(Entity *pEnt, InventorySlotReference *pInvSlotRef, GameAccountDataExtract *pExtract);
InventorySlot *InvSlotRef_GetInventoySlot(Entity *pEnt, InventorySlotReference *pInvSlotRef, GameAccountDataExtract *pExtract);

// Indicates whether the main item can transmutate into the other item
bool item_CanTransMutateTo(SA_PARAM_NN_VALID ItemDef *pMainItemDef, SA_PARAM_NN_VALID ItemDef *pTransmutateToItemDef);

// Indicates whether the item can transmutate into another item
bool item_CanTransMutate(SA_PARAM_OP_VALID ItemDef *pItemDef);

// Indicates whether the item can be dyed.  Temporary until we reenable dying weapons.
bool item_CanDye(SA_PARAM_OP_VALID ItemDef *pItemDef);

// Function returns the transmutated item def if there is a transmutation on the item
SA_RET_OP_VALID ItemDef * item_GetTransmutation(SA_PARAM_NN_VALID Item * pItem);

void item_trh_FreeSpecialPropsIfPossible(ATH_ARG NOCONST(Item)* pItem);

//-----------------------------------
// Common Item description functions
//-----------------------------------

// Fills the estr with a description of the Classes allowed by the ItemDef (or nothing if all Classes are alowed)
void itemdef_DescClassesAllowed(SA_PARAM_NN_VALID char **pestrResult, SA_PARAM_OP_VALID ItemDef *pItemDef, SA_PARAM_OP_VALID CharacterClass *pClassMatch);

// Attaches the base tooltip dev info for itemDebug mode
void item_PrintDebugText(char ** pestrDevText,Entity * pEnt,Item * pItem,ItemDef * pDef);

//This function creates a quick custom description.  Does not create auto description for non-innate powers.
// NOTE(JW): This is hardcoded to be NORMALIZED. If you don't know what that means,
//  or you need non-normalized descriptions, talk to Jered.
void item_GetNormalizedDescCustom(char** pestrResult, Entity* pEnt, Item *pItem, ItemDef* pItemDef,
								  const char* pchDescriptionKey, const char* pchPowers, const char* pchItemSetBonus,
								  const char* pchInnateDescKey, const char* pchResourceType, const char* pchValueKey, 
								  S32 iExchangeRate);

void item_GetFormattedResourceString(Language lang, char** pestrResult, int iResources, const char* pchValueFormatKey, S32 iExchangeRate, bool bShowZeros);

Message* UsageRestriction_GetCategoryMessage(Entity* pEnt, UsageRestrictionCategory eRestrictCategory);
bool UsageRestriction_IsRestricted(Entity* pEnt, UsageRestrictionCategory eRestrictCategory);

int getItempowerCategoriesForItem(Item* pItem);

// Returns the first sort type category that contains the given sort type ID
SA_RET_OP_VALID const ItemSortTypeCategory * item_GetSortTypeCategoryBySortTypeID(U32 iSortTypeID);

void item_RegisterItemTypeAsAlwaysSafeForGranting(ItemType theType);
bool item_IsUnsafeGrant(ItemDef * pDef);

bool item_CanOpenRewardPack(Entity* pEnt, ItemDef* pItemDef, ItemDef** ppRequiredItemDef, char** pestrError);

const char* item_GetTranslatedDescription(Item* pDef, Language langID);

ItemCategoryInfo* item_GetItemCategoryInfo(ItemCategory eCat);

const char* item_GetHeadshotStyleDef(Item* pItem);

bool item_trh_ItemDefMeetsBagRestriction(ItemDef* pDef, NOCONST(InventoryBag)* pBag);
#define item_ItemDefMeetsBagRestriction(def, bag) item_trh_ItemDefMeetsBagRestriction(def, CONTAINER_NOCONST(InventoryBag, bag))

bool itemdef_IsUnidentified(ItemDef* pItemDef);

bool item_trh_IsUnidentified(NOCONST(Item)* pItem);

#define item_IsUnidentified(pItem) item_trh_IsUnidentified(CONTAINER_NOCONST(Item, pItem))

#endif
