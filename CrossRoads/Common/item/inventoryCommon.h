/***************************************************************************
*     Copyright (c) 2003-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef INVENTORYCOMMON_H
#define INVENTORYCOMMON_H
GCC_SYSTEM

#include "referencesystem.h"
#include "textparser.h"
#include "GlobalTypeEnum.h"
#include "itemEnums.h"
#include "message.h"
#include "TransactionOutcomes.h"

typedef struct Entity Entity;
typedef struct Character Character;
typedef struct EntityBuild EntityBuild;
typedef struct InventoryBag InventoryBag;
typedef struct InventorySlot InventorySlot;
typedef struct InventoryBagV1 InventoryBagV1;
typedef struct InventorySlotV1 InventorySlotV1;
typedef struct DefaultInventory DefaultInventory;
typedef struct Item Item;
typedef struct ItemV1 ItemV1;
typedef struct ItemDef ItemDef;
typedef struct ItemEquipLimitCategoryData ItemEquipLimitCategoryData;
typedef struct ItemNumericData ItemNumericData;
typedef struct S64EarrayWrapper S64EarrayWrapper;
typedef enum InventorySlotType InventorySlotType;

typedef struct RewardBagInfo RewardBagInfo;
typedef struct PlayerCostume PlayerCostume;
typedef struct TradeSlot TradeSlot;
typedef struct GuildBankTabInfo GuildBankTabInfo;
typedef struct aiModifierDef aiModifierDef;
typedef struct AIAnimList AIAnimList;
typedef struct AllegianceDef AllegianceDef;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct GiveRewardBagsData GiveRewardBagsData;

typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(EntityBuild) NOCONST(EntityBuild);
typedef struct NOCONST(InventoryBag) NOCONST(InventoryBag);
typedef struct NOCONST(InventorySlot) NOCONST(InventorySlot);
typedef struct NOCONST(InventoryBagV1) NOCONST(InventoryBagV1);
typedef struct NOCONST(InventorySlotV1) NOCONST(InventorySlotV1);
typedef struct NOCONST(InventoryBagLite) NOCONST(InventoryBagLite);
typedef struct NOCONST(InventorySlotLite) NOCONST(InventorySlotLite);
typedef struct NOCONST(Item) NOCONST(Item);
typedef struct NOCONST(ItemV1) NOCONST(ItemV1);
typedef struct NOCONST(InfuseSlot) NOCONST(InfuseSlot);
typedef struct NOCONST(Guild) NOCONST(Guild);
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(GuildBankTabInfo) NOCONST(GuildBankTabInfo);

#define INVENTORY_FULL_MSG "Inventory.InventoryFull"

//loot

bool ExperimentIsItemInListByBagIndex(SA_PARAM_NN_VALID Entity *pEnt, int BagIdx, int iSlot);

typedef enum ItemFindCriteria
{
	kFindItem_ByName = 0,
	kFindItem_ByType,
	kFindItem_ByID,
}ItemFindCriteria;

typedef enum InvGetFlag
{
	InvGetFlag_None = 0,
	InvGetFlag_NoBuyBackBag = (1<<0),	// Do not get items from the buyback bag
	InvGetFlag_NoBankBag =	  (2<<0),	// Do not get items from the bank bag
	
}InvGetFlag;

AUTO_ENUM;
typedef enum InvBagFlag
{
	InvBagFlag_StorageOnly				= (1<<0),	//bag is for storage only, nothing active in it
	InvBagFlag_EquipBag					= (1<<1),	//bag contains items that are equipped
	InvBagFlag_WeaponBag				= (1<<2),	//bag contains items that are weapons
	InvBagFlag_DeviceBag				= (1<<3),	//bag contains items that are devices
	InvBagFlag_PlayerBag				= (1<<4),	//bag is an optional player bag, equipped via inventory
	InvBagFlag_PlayerBagIndex			= (1<<5),	//bag is index of active player bags that player bags are slotted in
	InvBagFlag_Hidden					= (1<<6),	//not sure what this means yet
	InvBagFlag_NameIndexed				= (1<<7),	//bag slots are indexed by name, not by slot #
	InvBagFlag_SellEnabled				= (1<<8),	//bag contains items that will be listed on the vendor screen
	InvBagFlag_BankBag					= (1<<9),	//bag is only accessible when interacting with bank contact
	InvBagFlag_GuildBankBag				= (1<<10),	//bag is only accessible when interacting with guild bank contact
	InvBagFlag_NoCopy					= (1<<11),	//this bag will not be copied as part of the puppet swapping
	InvBagFlag_RecipeBag				= (1<<12),	//bag contains items that are recipes
	InvBagFlag_NoModifyInCombat			= (1<<14),	//bag cannot be modified in combat
	InvBagFlag_CostumeHideable			= (1<<15),	//bag can be set to have the item costumes in it hidden
	InvBagFlag_ActiveWeaponBag			= (1<<16),	//bag that has one active slot, and the rest are treated as storage
	InvBagFlag_DefaultReady				= (1<<17),	//bag is readied by characters that haven't readied any bags at all
	InvBagFlag_CostumeHideablePerSlot	= (1<<18),	//allows each slot in the bag to control whether the item's costume is active
	InvBagFlag_BoundPetStorage			= (1<<19),
	InvBagFlag_ShowInAllCostumeSets		= (1<<20),	//items in bag will be added to costume, regardless of the chosen costume set
	InvBagFlag_RestrictedOnly			= (1<<21),  //bag will only accept items that can only be placed in this bag
} InvBagFlag;

//DEPRECATED
AUTO_ENUM;
typedef enum InventorySlotType
{
	InventorySlotType_Empty,		//slot is empty
	InventorySlotType_Item,			//slot has an item in it

	InventorySlotType_ItemLite,		//slot refers to an item def but has no associated item instance
	//!!!! warning, currently ItemLite has a limited subset of full item functionality, be careful
	//!!!! these items can be added,removed,counted, but no t moved between bags or handled
	//!!!! main usage is as a token
}InventorySlotType;

AUTO_ENUM;
typedef enum InvBagType
{
	InvBagType_None			= 0,
	InvBagType_Item			= 1,	//bag contains items
	InvBagType_ItemLite		= 2,		//bag contains lite bags and slots, no items.
} InvBagType;

#define InvBagFlag_DefaultInventorySearch	(InvBagFlag_SellEnabled | InvBagFlag_EquipBag | InvBagFlag_WeaponBag | InvBagFlag_ActiveWeaponBag | InvBagFlag_DeviceBag | InvBagFlag_PlayerBag | InvBagFlag_BankBag)
#define InvBagFlag_BindBag (InvBagFlag_EquipBag|InvBagFlag_DeviceBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag|InvBagFlag_PlayerBagIndex)
#define InvBagFlag_SpecialBag (InvBagFlag_EquipBag|InvBagFlag_DeviceBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag|InvBagFlag_PlayerBagIndex|InvBagFlag_RecipeBag)
#define InvBagFlag_Reportable (InvBagFlag_EquipBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag|InvBagFlag_DeviceBag|InvBagFlag_PlayerBag|InvBagFlag_PlayerBagIndex|InvBagFlag_BankBag|InvBagFlag_SellEnabled)


//
// These structs let us data-define more bags than the extra-special ones
// that are defined in code.
//
AUTO_STRUCT;
typedef struct InvBagExtraID
{
	const char **ppNames; AST(STRUCTPARAM)
		// A list of synonyms for the bag. Only the first one in this list will
		//   get written out. (This is a list to provide backward compatibility
		//   with old data.)
	bool bCanAffectCostume;	AST(NAME(CanAffectCostume))
} InvBagExtraID;

AUTO_STRUCT;
typedef struct InvBagExtraIDs
{
	InvBagExtraID **ppIDs; AST(NAME(ID))
		// A list of bags namelists.

} InvBagExtraIDs;

extern InvBagExtraIDs g_InvBagExtraIDs;

AUTO_STRUCT;
typedef struct InventoryBagArray
{
	INT_EARRAY eaiBagArray;
} InventoryBagArray;

//these macros simulate enums for all of the dynamic enum values
//this is game specific, and these match the FightClub data
//there should be a less clunky way to do this, but this is it for now
//these macros should not be in this file, but should be in a fight club specific file
#define InvBagIDs_Head			(StaticDefineIntGetInt(InvBagIDsEnum, "Head"))
#define InvBagIDs_Body			(StaticDefineIntGetInt(InvBagIDsEnum, "Body"))
#define InvBagIDs_Offense		(StaticDefineIntGetInt(InvBagIDsEnum, "Offense"))
#define InvBagIDs_Max			(StaticDefineIntGetInt(InvBagIDsEnum, "Max"))

#define inv_IsGuildBag(eBagID) ((eBagID) >= InvBagIDs_Bank1 && (eBagID) <= InvBagIDs_Bank8)

AUTO_STRUCT;
typedef struct InventorySlotIDDef
{
	char *pchKey;			AST(STRUCTPARAM KEY) 
	char *pchIcon;			AST(NAME(Icon))
	InvBagIDs eMainBagID;	AST(SUBTABLE(InvBagIDsEnum) NAME(MainBagID))
} InventorySlotIDDef;

AUTO_STRUCT;
typedef struct InvSlotIDContainer
{
	REF_TO(InventorySlotIDDef) hSlot;	AST(STRUCTPARAM REFDICT(InventorySlotIDDef))
}InvSlotIDContainer;

AUTO_STRUCT AST_CONTAINER;
typedef struct InventorySlot
{
	CONST_STRING_POOLED pchName;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE KEY POOL_STRING REQUIRED)
	CONST_OPTIONAL_STRUCT(Item) pItem;		AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	CONST_REF_TO(InventorySlotIDDef) hSlotType; AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(InventorySlotIDDef))
	const U32 bHideCostumes : 1;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
}InventorySlot;

AUTO_STRUCT AST_CONTAINER;
typedef struct InventorySlotV1
{
	CONST_STRING_POOLED pchName;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE KEY POOL_STRING REQUIRED)
	const InventorySlotType Type;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	const int count;							AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	CONST_OPTIONAL_STRUCT(ItemV1) pItem;			AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	CONST_REF_TO(ItemDef) hItemDef;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(ItemDef)) // this field is for 'light' items only. i.e. don't have an Item pointer.
	CONST_REF_TO(InventorySlotIDDef) hSlotType; AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(InventorySlotIDDef))
	const U32 bHideCostumes : 1;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
}InventorySlotV1;

AUTO_STRUCT;
typedef struct InvBagSlotSwitchRequest
{
	InvBagIDs eBagID;
	S32 iIndex;
	S32 iNewActiveSlot;
	U32 uRequestID;
	U32 uTime;
	F32 fDelay;
	F32 fTimer;
	bool bHasChangedSlot;
	bool bHasHandledMoveEvents;
}InvBagSlotSwitchRequest;

AUTO_STRUCT AST_CONTAINER;
typedef struct InventorySlotLite
{
	CONST_STRING_POOLED pchName;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE KEY POOL_STRING REQUIRED)
	const int count;									AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE DEFAULT(1))
	//This sucks. Have to have the ref so the Client knows about it,
	// but can't index based on a ref so we need to have the string too. Oh well.
	CONST_REF_TO(ItemDef) hItemDef;				AST(REFDICT(ItemDef) PERSIST SUBSCRIBE)
}InventorySlotLite;

AUTO_STRUCT AST_CONTAINER;
typedef struct InventoryBagLite
{
	InvBagIDs const          BagID;							AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE KEY REQUIRED NAME(BagID))
	REF_TO(DefaultInventory) inv_def;						AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(DefaultInventory))
	CONST_EARRAY_OF(InventorySlotLite) ppIndexedLiteSlots;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	CONST_OPTIONAL_STRUCT(GuildBankTabInfo) pGuildBankInfo;	AST(PERSIST SUBSCRIBE FORCE_CONTAINER)
	// info needed if this bag corresponds to a guild bank tab

} InventoryBagLite;

AUTO_STRUCT AST_CONTAINER;
typedef struct OwnedUniqueItem
{
	CONST_STRING_POOLED pchName;	AST(POOL_STRING KEY PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
}OwnedUniqueItem;

AUTO_STRUCT AST_CONTAINER;
typedef struct NewItemID
{
	U64 id;					AST(PERSIST NO_TRANSACT KEY)
} NewItemID;

AUTO_STRUCT AST_CONTAINER;
typedef struct Inventory
{
	DirtyBit dirtyBit;										AST(NO_NETSEND)

	CONST_REF_TO(DefaultInventory)		inv_def;
	CONST_EARRAY_OF(InventoryBag)		ppInventoryBags;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	CONST_EARRAY_OF(InventoryBagLite)	ppLiteBags;			AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	CONST_EARRAY_OF(OwnedUniqueItem)	peaOwnedUniqueItems;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	InvBagSlotSwitchRequest** ppSlotSwitchRequest;			AST(NO_NETSEND)
	U32 uiCurrentSlotSwitchID;								AST(CLIENT_ONLY)
	EARRAY_OF(NewItemID) eaiNewItemIDs;						AST(PERSIST NO_TRANSACT)

}Inventory;

AUTO_STRUCT AST_CONTAINER;
typedef struct InventoryV1
{
	DirtyBit dirtyBit;										AST(NO_NETSEND)

	CONST_REF_TO(DefaultInventory)		inv_def;
	CONST_EARRAY_OF(InventoryBagV1)		ppInventoryBags;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	InvBagSlotSwitchRequest** ppSlotSwitchRequest;			AST(NO_NETSEND)
	U32 uiCurrentSlotSwitchID;								AST(CLIENT_ONLY)
}InventoryV1;

AUTO_STRUCT;
typedef struct InvBagItemMoveEvent
{
	const char** ppchFlashBits;				AST(NAME(FlashBits) POOL_STRING)
		//Flash bits to play when an item is added or removed from this bag
	
	const char* pchPowerCooldownCategory;	AST(NAME(PowerCooldownCategory) POOL_STRING) 
		//Set a cooldown on this category when an item is added or removed from this bag 

	F32	fCooldownTime;
		//The amount of cooldown time to set for the given category
} InvBagItemMoveEvent;

AUTO_STRUCT;
typedef struct InvBagDefItemArt
{
	const char *pchFX;		AST(POOL_STRING STRUCTPARAM)
		// Name of the FX

	const char *pchBone;	AST(POOL_STRING)
		// Default Bone param

	Vec3 vPosition;
		// Default Position param

	Vec3 vRotation;
		// Default Rotation param

} InvBagDefItemArt;

// This provides a way to associate a list of bags with a single display name (for UI display)
AUTO_STRUCT;
typedef struct InvBagCategory
{
	const char* pchName; AST(KEY STRUCTPARAM)
	DisplayMessage msgDisplayName; AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))  
	InvBagIDs* peBagIDs; AST(NAME(BagID))
} InvBagCategory;

AUTO_STRUCT;
typedef struct InvBagCategories
{
	InvBagCategory** eaCategories; AST(NAME(BagCategory))
} InvBagCategories;

AUTO_STRUCT;
typedef struct InvBagSlotTableEntry
{
	int iNumericValue; AST(STRUCTPARAM)
		// The numeric value

	int iMaxSlots;	AST(NAME(MaxSlots))
		// Max slots at this value
} InvBagSlotTableEntry;

AUTO_STRUCT;
typedef struct InvBagSlotTable
{
	const char* pchName; AST(STRUCTPARAM KEY)
		// Internal name of this table
	
	EARRAY_OF(InvBagSlotTableEntry) eaEntries; AST(NAME(Value, NumericValue))
		// List of value-slot pairs
} InvBagSlotTable;

AUTO_STRUCT;
typedef struct InvBagSlotTables
{
	EARRAY_OF(InvBagSlotTable) eaTables; AST(NAME(BagSlotTable))
} InvBagSlotTables;

AUTO_STRUCT;
typedef struct InvBagDef
{
    InvBagIDs BagID;						AST(KEY STRUCTPARAM SUBTABLE(InvBagIDsEnum))
    char *fname;							AST( CURRENTFILE )

	InvBagType eType;						AST(DEFAULT(1))
    int MaxSlots;							AST(DEFAULT(-1))
	InvSlotIDContainer **ppSlotIDs;			AST(NAME(SlotID))		
    InvBagFlag flags;						AST(FLAGS)
    const F32 power_yaw;
    int   not_in_default_inventory;

	const char* pchMaxSlotTable;			AST(NAME(MaxSlotTable))
		// Table to use to derive the max slots

	REF_TO(ItemDef) hMaxSlotTableNumericStandard;	AST(NAME(MaxSlotTableNumericStandard))
		// The numeric to use to look into the max slot table for standard(silver) characters.

    REF_TO(ItemDef) hMaxSlotTableNumericPremium;	AST(NAME(MaxSlotTableNumericPremium))
        // The numeric to use to look into the max slot table for premium(gold) characters.

	DisplayMessage msgBagFull;				AST(NAME(BagFullMessage) STRUCT(parse_DisplayMessage)) 
		// Special message to display when this bag is full. If not set, use the default message.

	S32 maxActiveSlots;						AST(DEFAULT(1))
		// the maximum number of active slots, currently only used for ActiveWeaponBags

	S32 *eaiDefaultActiveSlots;				AST(NAME(DefaultActiveSlot))
		// the default actives when the bag is first created, currently only used for ActiveWeaponBags.

	InvBagDefItemArt *pItemArtActive;				AST(ADDNAMES(FXItemArtActive))
		// ItemArt data for active (unholstered) Items in this bag

	InvBagDefItemArt *pItemArtInactive;				AST(ADDNAMES(FXItemArtInactive))
		// ItemArt data for inactive (holstered) Items in this bag

	InvBagDefItemArt *pItemArtActiveSecondary;		AST(ADDNAMES(SecondaryFXItemArtActive))
		// ItemArt data for active (unholstered) Items in this bag's secondary slot

	InvBagDefItemArt *pItemArtInactiveSecondary;	AST(ADDNAMES(SecondaryFXItemArtInactive))
		// ItemArt data for inactive (holstered) Items in this bag's secondary slot

	const char *pFXNodeName;
	//The actions that are taken when an item is moved into or out of this bag
	InvBagItemMoveEvent** ppItemMoveEvents;	AST(NAME(ItemMoveEvent))
	ItemCategory *pePrimaryOnlyCategories;	AST(NAME(PrimaryOnlyCategories) SUBTABLE(ItemCategoryEnum))
	F32 fChangeActiveSlotDelay;				AST(NAME(ChangeActiveSlotDelay))
	U32 bAutoAttack : 1; // Any ONE slot in this bag can autoattack
	U32 bUseItemsInInventoryFirst: 1; // Uses powers on matching items in inventory first
	U32 bFakePropSlots : 1; // This bag contains items that should be considered part of a player's prop slots
	U8 iCostumeSetIndex; // which costume set this bag belongs to. Mostly for Fashion Slots.
} InvBagDef;


AUTO_STRUCT AST_CONTAINER;
typedef struct InventoryBagV1
{
	InvBagIDs const          BagID;							AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE KEY REQUIRED NAME(BagID))
	REF_TO(DefaultInventory) inv_def;						AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(DefaultInventory))
	int const                n_additional_slots;			AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE NAME(MaxSlots)) // usually used for bag items 

	CONST_EARRAY_OF(InventorySlotV1) ppIndexedInventorySlots; AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	//earray of all the slots in this bag

	RewardBagInfo * pRewardBagInfo;
	//info needed for special handling of detached reward bags

	CONST_OPTIONAL_STRUCT(GuildBankTabInfo) pGuildBankInfo;	AST(PERSIST SUBSCRIBE FORCE_CONTAINER)
	// info needed if this bag corresponds to a guild bank tab

	int *eaiActiveSlots;									AST(NAME("iActiveSlot") PERSIST SUBSCRIBE LOGIN_SUBSCRIBE NO_TRANSACT)
	// The active slot for this bag (currently only used for ActiveWeaponBags)

	int *eaiPredictedActiveSlots;							AST(CLIENT_ONLY)
	// The slots that the client predicts will be active after swapping

	ContainerID uiTeamOwner;								AST(SERVER_ONLY)
	// stores the team to which this bag belongs; used for interactable loot bags

	const int bHideCostumes : 1;							AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
}InventoryBagV1;

AUTO_STRUCT AST_CONTAINER;
typedef struct InventoryBag
{
    InvBagIDs const          BagID;							AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE KEY REQUIRED NAME(BagID))
    REF_TO(DefaultInventory) inv_def;						AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(DefaultInventory))
    int const                n_additional_slots;			AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE NAME(MaxSlots)) // usually used for bag items 

	CONST_EARRAY_OF(InventorySlot) ppIndexedInventorySlots; AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE SELF_ONLY)
	//earray of all the slots in this bag

	RewardBagInfo * pRewardBagInfo;
	//info needed for special handling of detached reward bags
	
	CONST_OPTIONAL_STRUCT(GuildBankTabInfo) pGuildBankInfo;	AST(PERSIST SUBSCRIBE FORCE_CONTAINER)
	// info needed if this bag corresponds to a guild bank tab

	int *eaiActiveSlots;									AST(NAME("iActiveSlot") PERSIST SUBSCRIBE LOGIN_SUBSCRIBE NO_TRANSACT)
	// The active slot for this bag (currently only used for ActiveWeaponBags)

	int *eaiPredictedActiveSlots;							AST(CLIENT_ONLY)
	// The slots that the client predicts will be active after swapping

	ContainerID uiTeamOwner;								AST(SERVER_ONLY)
	// stores the team to which this bag belongs; used for interactable loot bags

	const int bHideCostumes : 1;							AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
}InventoryBag;

AUTO_STRUCT;
typedef struct DefaultItemDef
{
	int			   iKey;					AST(KEY)
	F32			   fOrder;

	REF_TO(ItemDef) hItem;					AST(REFDICT(ItemDef) NAME(Item))
	InvBagIDs eBagID;						AST(SUBTABLE(InvBagIDsEnum) DEFAULT(InvBagIDs_Inventory))
	//The bag to place this item into. Must match with the item or be the Inventory bag
	int iCount;								AST(NAME(Count) DEFAULT(1))
	//The number of items to grant of this particular itemDef
	aiModifierDef *pModifierInfo;			AST(NAME(ModifierInfo))

	S32 iMinLevel;							AST(DEF(-1))
	S32 iMaxLevel;							AST(DEF(-1))
	F32 fChance;							AST(DEF(1))

	S32 iGroup;
	S32 iSlot;								AST(DEF(-1))
	F32 fWeight;

	bool bDisabled;
}DefaultItemDef;

AUTO_STRUCT;
typedef struct DefaultInventory
{
	char *pchInventoryName;			AST(KEY STRUCTPARAM)
	InvBagDef **InventoryBags;
	//These items are granted upon initializing the inventory only
	DefaultItemDef **GrantedItems;	AST(NAME(GrantItem))
} DefaultInventory;

AUTO_STRUCT;
typedef struct InvRewardRequest
{
	EARRAY_OF(ItemNumericData) eaNumericRewards;
	EARRAY_OF(Item) eaItemRewards;
	EARRAY_OF(InventorySlot) eaRewards; AST(CLIENT_ONLY)
	S32 iRewardSlots; AST(CLIENT_ONLY)
} InvRewardRequest;

extern DictionaryHandle g_hDefaultInventoryDict;
extern DictionaryHandle g_hInvBagDefDict;

AUTO_STRUCT;
typedef struct ItemTransCBData{
	char* pchItemDefName;
	const char* pchSoldItem;		AST(POOL_STRING)
	const char** ppchNamesUntranslated;
	int iCount;
	S32 value;
	int type;
	InvBagIDs eBagID; //the bag we're adding or removing from
	int iBagSlot;
	U32 kQuality;
	U32 uiItemID;
	Vec3 vOrigin; 
	void* userFunc; NO_AST
	void* userData; NO_AST
	bool bTranslateName;
	bool bSilent;
	bool bFromRollover;
	bool bFromStore; AST(NAME(fromStore))
	bool bLite;
} ItemTransCBData;

AUTO_STRUCT;
typedef struct ItemChangeReason {
	// The nature of the change
	const char *pcReason;		AST(POOL_STRING)
	const char *pcDetail;		AST(POOL_STRING)

	// Player Position
	const char *pcMapName;		AST(POOL_STRING)
	Vec3 vPos;
	
	// Flags
	U32 bUGC : 1;
	U32 bKillCredit : 1;
	U32 bFromRollover : 1;
	U32 bFromStore : 1; AST(NAME(fromStore))
} ItemChangeReason;

AUTO_STRUCT;
typedef struct MoveItemGuildStruct
{
	int bSrcGuild;
	int iSrcSlotIdx;
	U64 uSrcItemID;
	int iSrcEPValue;
	int iSrcCount;
	int bDstGuild;
	int iDstSlotIdx;
	U64 uDstItemID;
	int iDstEPValue;
} MoveItemGuildStruct;

AUTO_STRUCT;
typedef struct ItemListEntry
{
	InvBagIDs id;
	int	iSlot;
	Item *pItem;		AST(UNOWNED)
} ItemListEntry;

AUTO_STRUCT;
typedef struct RewardPackItem
{
	const char *pchItemName;
	U32 iCount;
} RewardPackItem;

AUTO_STRUCT;
typedef struct RewardPackLog
{
	const char *pchEntityName;
	const char *pchRewardPackName;
	EARRAY_OF(RewardPackItem) ppRewardPackItems;
} RewardPackLog;

typedef struct BagIterator
{
	InventoryBag** eaBags;
	InventoryBagLite** eaLitebags;
	int i_cur;
	int i_bag;
} BagIterator;

//initialize global inventory data
void inv_Load(void);
void invIDs_Load(void);
/*G

Inventory routine naming convention

All routines start with inv_

followed by a bag_		specifies that the routine take bag pointer as input 
              ent_		specifies that the input is an Ent pointer and BagID


optionally followed by trh_  this specifies that the routine is a transaction helper

There are almost always bag, ent, non-trh and trh versions of all functions defined

*/

// Convenience function to get a bag category by name
InvBagCategory* inv_GetBagCategoryByName(const char* pchBagCategoryName);
// Get a bag category from a list of BagIDs
InvBagCategory* inv_GetBagCategoryByBagIDs(InvBagIDs* peBagIDs);
// Create a list of sorted item IDs to pass to inv_bag_trh_ApplySort
S64EarrayWrapper* inv_bag_CreateSortData(Entity* pEnt, InventoryBag* pBag);

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
bool inv_bag_trh_BagEmpty(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag);
#define inv_bag_BagEmpty(pBag) inv_bag_trh_BagEmpty( ATR_EMPTY_ARGS,  CONTAINER_NOCONST(InventoryBag, (pBag)))
bool inv_ent_trh_BagEmpty(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int BagID, GameAccountDataExtract *pExtract);
#define inv_ent_BagEmpty(pEnt, BagID, pExtract) inv_ent_trh_BagEmpty( ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, pExtract)

bool inv_bag_trh_BagFull(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pBag, InventorySlotIDDef *pInvSlotIDDef);
#define inv_bag_BagFull(pEnt, pBag) inv_bag_trh_BagFull(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), CONTAINER_NOCONST(InventoryBag, (pBag)), NULL)
bool inv_ent_trh_BagFull(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int BagID, InventorySlotIDDef *pInvSlotID, GameAccountDataExtract *pExtract);
#define inv_ent_BagFull(pEnt, BagID) inv_ent_trh_BagFull( ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, NULL)
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines that add/remove bags

int inv_ent_trh_AddBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, NOCONST(InventoryBag)* pBag, GameAccountDataExtract *pExtract);
#define inv_ent_AddBag(pEnt, pBag, pExtract) inv_ent_trh_AddBag(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), CONTAINER_NOCONST(InventoryBag, (pBag)), pExtract)

int inv_ent_AddLootBag(Entity* pEnt, InventoryBag* pBag);

NOCONST(InventoryBag)* inv_ent_trh_AddBagFromDef(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, InvBagDef *pBagDef);

NOCONST(InventoryBag)* inv_ent_trh_RemoveBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID);
#define inv_ent_RemoveBag(pEnt, BagID) (InventoryBag*)inv_ent_trh_RemoveBag(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID)

void inv_trh_CollapseBag(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag);
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

void inv_trh_AddSlotToBagWithDef(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, InventorySlotIDDef *pDef);


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines that traverse a bag

//these routines will traverse a nested bag tree and execute a callback for each item or bag in that tree






//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines that clear bags

//these routines destroy all items within the specified bag and all nested bags
//currently, the bags are not destroyed and the bag tree stays as-is

void inv_bag_trh_ClearBag(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag);
void inv_lite_trh_ClearBag(ATR_ARGS, ATH_ARG NOCONST(InventoryBagLite)* pBag);

bool inv_ent_trh_ClearAllBags(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt);

bool inv_guildbank_trh_ClearAllBags(ATR_ARGS, ATH_ARG NOCONST(Entity)* pGuildBank);
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@



//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines used to initialize inventory

bool inv_ent_trh_AddInventory(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt);
#define inv_ent_AddInventory(pEnt) inv_ent_trh_AddInventory(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)))

bool inv_ent_trh_InitAndFixupInventory(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, DefaultInventory *pModelInventory, bool bAddNoCopyBags, bool bFixupItemIDs, const ItemChangeReason *pReason);
bool inv_ent_trh_VerifyInventoryData(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bAddNoCopyBags, bool bFixupItemIDs, const ItemChangeReason *pReason);

void inv_ent_trh_AddInventoryItems(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

bool inv_guildbank_trh_InitializeInventory(ATR_ARGS, ATH_ARG NOCONST(Guild)* pGuild, ATH_ARG NOCONST(Entity)* pGuildBank, DefaultInventory *pModelInventory);

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@





//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines that make simple lists of items in bags

int inv_bag_trh_GetSimpleItemList(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, Item ***pppItemList, bool bCopyStructs, S32 iReUseArrayElementStartIndex);
#define inv_bag_GetSimpleItemList(pBag, pppItemList, bCopyStructs) inv_bag_trh_GetSimpleItemList(ATR_EMPTY_ARGS,  CONTAINER_NOCONST(InventoryBag, (pBag)), pppItemList, bCopyStructs, -1)
#define inv_bag_GetSimpleItemListAndReUseArrayElements(pBag, pppItemList, bCopyStructs, iReUseArrayElementStartIndex) inv_bag_trh_GetSimpleItemList(ATR_EMPTY_ARGS,  CONTAINER_NOCONST(InventoryBag, (pBag)), pppItemList, bCopyStructs, iReUseArrayElementStartIndex)

int inv_ent_trh_GetSimpleItemList(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, Item ***pppItemList, bool bCopyStructs, GameAccountDataExtract *pExtract);
#define inv_ent_GetSimpleItemList(pEnt, BagID, pppItemList, bCopyStructs, pExtract) inv_ent_trh_GetSimpleItemList(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, pppItemList, bCopyStructs, pExtract)
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@




//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines that count things in Bags

int inv_bag_trh_GetFirstEmptySlot(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pBag);
#define inv_bag_GetFirstEmptySlot(pEnt, pBag) inv_bag_trh_GetFirstEmptySlot(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), CONTAINER_NOCONST(InventoryBag, pBag))
int inv_ent_trh_GetFirstEmptySlot(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs eBag);
#define inv_ent_GetFirstEmptySlot(pEnt, eBagID) inv_ent_trh_GetFirstEmptySlot(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), eBagID)

int inv_bag_trh_CountItems(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, SA_PARAM_OP_STR const char *ItemDefName, ItemCategory eCategory);
#define inv_bag_CountItemsWithCategory(pBag, ItemDefName, eCategory) inv_bag_trh_CountItems(ATR_EMPTY_ARGS,  CONTAINER_NOCONST(InventoryBag, (pBag)), NULL, eCategory)
#define inv_bag_CountItems(pBag, ItemDefName) inv_bag_trh_CountItems(ATR_EMPTY_ARGS,  CONTAINER_NOCONST(InventoryBag, (pBag)), ItemDefName, -1)

int inv_ent_trh_CountItems(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, SA_PARAM_OP_STR const char *ItemDefName, ItemCategory eCategory, GameAccountDataExtract *pExtract);
#define inv_ent_CountItemsWithCategory(pEnt, BagID, eCategory, pExtract) inv_ent_trh_CountItems(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, NULL, eCategory, pExtract)
#define inv_ent_CountItems(pEnt, BagID, ItemDefName, pExtract) inv_ent_trh_CountItems(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, ItemDefName, -1, pExtract)

int inv_ent_trh_CountItemList(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, const char ***peaItemNameList, GameAccountDataExtract *pExtract);
#define inv_ent_CountItemList(pEnt, BagID, peaItemNameList, pExtract) inv_ent_trh_CountItemList(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, peaItemNameList, pExtract)

int inv_ent_trh_AllBagsCountItems(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, SA_PARAM_NN_STR const char *ItemDefName, InvBagIDs* peExcludeBags);
#define inv_ent_AllBagsCountItemsEx(pEnt, ItemDefName, peExcludeBags) inv_ent_trh_AllBagsCountItems(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), ItemDefName, peExcludeBags)
#define inv_ent_AllBagsCountItems(pEnt, ItemDefName) inv_ent_trh_AllBagsCountItems(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), ItemDefName, NULL)

bool inv_ent_trh_AllBagsCountItemsAtLeast(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, SA_PARAM_NN_STR const char *ItemDefName, InvBagIDs* peExcludeBags, unsigned int uiAtLeast);
#define inv_ent_AllBagsCountItemsAtLeastEx(pEnt, ItemDefName, peExcludeBags, uiAtLeast) inv_ent_trh_AllBagsCountItemsAtLeast(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), ItemDefName, peExcludeBags, uiAtLeast)
#define inv_ent_AllBagsCountItemsAtLeast(pEnt, ItemDefName, uiAtLeast) inv_ent_trh_AllBagsCountItemsAtLeast(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), ItemDefName, NULL, uiAtLeast)

bool inv_ent_trh_EquipLimitCheck(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pDstBag, ItemDef* pItemDef, S32 iIgnoreBagID, S32 iIgnoreSlot, ItemEquipLimitCategoryData** ppLimitCategory, S32* piEquipLimit);
#define inv_ent_EquipLimitCheckEx(pEnt, pBag, pItemDef, iIgnoreBagID, iIgnoreSlot, ppLimitCategory, piEquipLimit) inv_ent_trh_EquipLimitCheck(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), CONTAINER_NOCONST(InventoryBag, (pBag)), pItemDef, iIgnoreBagID, iIgnoreSlot, ppLimitCategory, piEquipLimit)
#define inv_ent_EquipLimitCheck(pEnt, pBag, pItemDef) inv_ent_trh_EquipLimitCheck(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), CONTAINER_NOCONST(InventoryBag, (pBag)), pItemDef, -1, -1, NULL, NULL)

bool inv_ent_trh_EquipLimitCheckSwap(ATR_ARGS, ATH_ARG NOCONST(Entity)* pSrcEnt, ATH_ARG NOCONST(Entity)* pDstEnt, ATH_ARG NOCONST(InventoryBag)* pSrcBag, ATH_ARG NOCONST(InventoryBag)* pDstBag, ItemDef* pSrcItemDef, ItemDef* pDstItemDef, ItemEquipLimitCategoryData** ppLimitCategory, S32* piEquipLimit);
#define inv_ent_EquipLimitCheckSwap(pSrcEnt, pDstEnt, pSrcBag, pDstBag, pSrcItemDef, pDstItemDef, ppLimitCategory, piEquipLimit) inv_ent_trh_EquipLimitCheckSwap(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pSrcEnt)), CONTAINER_NOCONST(Entity, (pDstEnt)), CONTAINER_NOCONST(InventoryBag, (pSrcBag)), CONTAINER_NOCONST(InventoryBag, (pDstBag)), pSrcItemDef, pDstItemDef, ppLimitCategory, piEquipLimit)

bool inv_ent_trh_HasUniqueItem( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, const char* pcItemName);
#define inv_ent_HasUniqueItem(pEnt, pcItemName) inv_ent_trh_HasUniqueItem(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), NULL, pcItemName)

int inv_bag_trh_CountItemSlots(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, SA_PARAM_OP_STR const char *ItemDefName);

int inv_bag_trh_AvailableSlots(ATR_ARGS, ATH_ARG NOCONST(Entity)*pEnt, ATH_ARG NOCONST(InventoryBag)* pBag);

int inv_ent_trh_AvailableSlots(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, GameAccountDataExtract *pExtract);
#define inv_ent_AvailableSlots(pEnt, BagID, pExtract) inv_ent_trh_AvailableSlots(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, pExtract)
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@



//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines to get count of items in a specified bag/slot
int inv_ent_trh_GetSlotItemCount(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, int SlotIdx, GameAccountDataExtract *pExtract);
#define inv_ent_GetSlotItemCount(pEnt, BagID, SlotIdx, pExtract) inv_ent_trh_GetSlotItemCount(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, SlotIdx, pExtract)

int inv_guildbank_trh_GetSlotItemCount(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs iBagID, int iSlotIdx);
#define inv_guildbank_GetSlotItemCount(pEnt, iBagID, iSlotIdx) inv_guildbank_trh_GetSlotItemCount(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), iBagID, iSlotIdx)

//routines to get count of items in a specified bag/slot
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

int inv_bag_trh_GetSlotItemCount(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, int SlotIdx);
#define inv_bag_GetSlotItemCount(pBag, SlotIdx) inv_bag_trh_GetSlotItemCount(ATR_EMPTY_ARGS, CONTAINER_NOCONST(InventoryBag, (pBag)), SlotIdx)



//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines to get count Bag info

const char* inv_trh_GetBagFullMessageKey(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs eBagID, GameAccountDataExtract *pExtract);

int invbag_trh_flags(ATH_ARG NOCONST(InventoryBag) *bag);
int invbaglite_trh_flags(ATH_ARG NOCONST(InventoryBagLite) *bag);
InvBagType invbag_trh_type(ATH_ARG NOCONST(InventoryBag) *bag);
int invbag_trh_bagid(ATH_ARG NOCONST(InventoryBag) *bag);
int invbag_trh_basemaxslots(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(InventoryBag) *pBag);
S32 invbag_BaseSlotsByName(Entity *pEntity, const char *pcBagName, GameAccountDataExtract *pExtract);
int invbag_trh_maxslots(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(InventoryBag) *pBag);
int invbag_maxActiveSlots(InventoryBag const *bag);
U8 invbag_trh_costumesetindex(ATH_ARG NOCONST(InventoryBag) *bag);

// Indicates if the bag contains reportable items
bool invbag_trh_isReportable(ATH_ARG NOCONST(InventoryBag) *pBag);

__forceinline static int invbag_flags(InventoryBag const *bag) { return invbag_trh_flags(CONTAINER_NOCONST(InventoryBag, bag)); }
__forceinline static int invbaglite_flags(InventoryBagLite const *bag) { return invbaglite_trh_flags(CONTAINER_NOCONST(InventoryBagLite, bag)); }
__forceinline static int invbag_bagid(InventoryBag const *bag) { return invbag_trh_bagid(CONTAINER_NOCONST(InventoryBag, bag)); }
__forceinline static int invbag_maxslots(Entity *ent, InventoryBag const *bag) { return invbag_trh_maxslots(CONTAINER_NOCONST(Entity, ent), CONTAINER_NOCONST(InventoryBag, bag)); }
__forceinline static int invbag_basemaxslots(Entity *ent, InventoryBag const *bag) { return invbag_trh_basemaxslots(CONTAINER_NOCONST(Entity, ent), CONTAINER_NOCONST(InventoryBag, bag)); }
__forceinline static int invbag_costumesetindex(InventoryBag const *bag) { return invbag_trh_costumesetindex(CONTAINER_NOCONST(InventoryBag, bag)); }

// Indicates if the bag contains reportable items
__forceinline static bool invbag_isReportable(InventoryBag const *bag) { return invbag_trh_isReportable(CONTAINER_NOCONST(InventoryBag, bag)); }

int inv_ent_trh_GetMaxSlots(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, GameAccountDataExtract *pExtract);
#define inv_ent_GetMaxSlots(pEnt, BagID, pExtract) inv_ent_trh_GetMaxSlots( ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, pExtract)

void inv_bag_trh_SetMaxSlots(ATR_ARGS, ATH_ARG NOCONST(Entity) * pEnt, ATH_ARG NOCONST(InventoryBag)* pBag, int MaxCount);
#define inv_bag_SetMaxSlots(pEnt, pBag, MaxCount) inv_bag_trh_SetMaxSlots( ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), CONTAINER_NOCONST(InventoryBag, (pBag)), MaxCount )

void inv_ent_trh_SetMaxSlots(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, int MaxCount, GameAccountDataExtract *pExtract);
#define inv_ent_SetMaxSlots(pEnt, BagID, MaxCount) inv_ent_trh_SetMaxSlots( ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, MaxCount )

int inv_bag_trh_GetNumSlots(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag);
#define inv_bag_GetNumSlots(pBag) inv_bag_trh_GetNumSlots( ATR_EMPTY_ARGS,  CONTAINER_NOCONST(InventoryBag, (pBag)) )

int inv_ent_trh_GetNumSlots(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, GameAccountDataExtract *pExtract);
#define inv_ent_GetNumSlots(pEnt, BagID, pExtract) inv_ent_trh_GetNumSlots( ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, pExtract)

//routines to get count Bag info
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@



//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//fixup routines

void inv_trh_FixupInventory(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, bool bFixupItemIDs, const ItemChangeReason *pReason);
void inv_trh_InitAndFixupItem(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Item) *pItem, bool bFixupItemIDs, const ItemChangeReason *pReason);
#define inv_FixupItemNoConst(pItem) inv_trh_InitAndFixupItem(ATR_EMPTY_ARGS, NULL, pItem, true, NULL)
#define inv_FixupItem(pItem) inv_FixupItemNoConst(CONTAINER_NOCONST(Item, pItem))
void inv_trh_ClearItemIDs(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt);
#define inv_ClearItemIDsNoConst(pEnt) inv_trh_ClearItemIDs(ATR_EMPTY_ARGS, (pEnt))
#define inv_ClearItemIDs(pEnt) inv_ClearItemIDsNoConst(CONTAINER_NOCONST(Entity, (pEnt)))
void inv_trh_FixupEquipBags(ATR_ARGS, ATH_ARG NOCONST(Entity) *pOwner, ATH_ARG NOCONST(Entity)* pEnt, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract, bool bAllowOwnerChange);
S32 inv_trh_ItemEquipValid(ATR_ARGS, ATH_ARG NOCONST(Entity) *pOwner, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Item) *pItem, ATH_ARG NOCONST(InventoryBag) *pBag, int iSlot);
void inv_trh_UpdateItemSets(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, const ItemChangeReason *pReason);
bool inv_NeedsBagFixup(Entity *pEnt);

//fixup routines
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@



//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines to get info about bags or items

NOCONST(InventoryBag)* inv_guildbank_trh_GetBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pGuildBank, InvBagIDs BagID);
#define inv_guildbank_GetBag(pGuildBank, BagID) (InventoryBag*) inv_guildbank_trh_GetBag(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pGuildBank)), BagID)

NOCONST(InventoryBagLite)* inv_guildbank_trh_GetLiteBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pGuildBank, InvBagIDs bag_id);
#define inv_guildbank_GetLiteBag(pGuildBank, BagID) (InventoryBagLite*) inv_guildbank_trh_GetLiteBag(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pGuildBank)), BagID)

NOCONST(Item)* inv_trh_GetItemByID(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, U64 id, InvBagIDs *pRetBagID, int *pRetSlot, InvGetFlag getFlag);
#define inv_GetItemAndSlotsByID(pEnt, id, bagIDRet, slotIdxRet) (Item*) inv_trh_GetItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), id, bagIDRet, slotIdxRet, InvGetFlag_None)
#define inv_GetItemByID(pEnt, id) (Item*) inv_trh_GetItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), id, NULL, NULL, InvGetFlag_None)

NOCONST(Item)* inv_trh_GetItemFromBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, int SlotIdx, GameAccountDataExtract *pExtract);
Item* inv_GetItemFromBag(Entity* pEnt, InvBagIDs BagID, int SlotIdx, GameAccountDataExtract *pExtract);

NOCONST(Item)* inv_guildbank_trh_GetItemFromBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, int SlotIdx);
#define inv_guildbank_GetItemFromBag(pEnt, BagID, SlotIdx) (Item*) inv_guildbank_trh_GetItemFromBag(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, SlotIdx)

NOCONST(Item)* inv_trh_GetItemFromBagIDByName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, const char* ItemDefName, GameAccountDataExtract *pExtract);
#define inv_GetItemFromBagIDByName(pEnt, BagID, ItemDefName, pExtract) (Item*) inv_trh_GetItemFromBagIDByName(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, ItemDefName, pExtract)

NOCONST(Item)* inv_bag_trh_GetItem(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, int SlotIdx);
__forceinline static Item *inv_bag_GetItem(InventoryBag *pBag, S32 iSlot) {
	return (Item*)inv_bag_trh_GetItem(ATR_EMPTY_ARGS, CONTAINER_NOCONST(InventoryBag, (pBag)), iSlot);
}
NOCONST(Item)* inv_bag_trh_GetItemByID(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, U64 id, int* pSlotOut, int* pCountOut);
#define inv_bag_GetItemByID(pBag, id, pSlot, pCount) (Item*) inv_bag_trh_GetItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(InventoryBag, (pBag)), id, pSlot, pCount)



// Gets or creates an inventory slot in the bag
int inv_trh_GetSlot(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pDestBag, int SlotIdx, SA_PARAM_OP_STR const char *name, InventorySlotIDDef *pIDDef);

NOCONST(InventorySlot)* inv_trh_GetSlotFromBag(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, int SlotIdx);
#define inv_GetSlotFromBag(pBag, SlotIdx) (InventorySlot*)inv_trh_GetSlotFromBag(ATR_EMPTY_ARGS, CONTAINER_NOCONST(InventoryBag, (pBag)), SlotIdx)

NOCONST(Item)* inv_bag_trh_GetItemByName(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, SA_PARAM_NN_STR const char* ItemDefName);
#define inv_bag_GetItemByName(pBag, ItemDefName) (Item*) inv_bag_trh_GetItemByName(ATR_EMPTY_ARGS,  CONTAINER_NOCONST(InventoryBag, (pBag)), ItemDefName)

NOCONST(Item)* inv_bag_trh_GetIndexedItemByName(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, SA_PARAM_NN_STR const char* ItemDefName);
#define inv_bag_GetIndexedItemByName(pBag, ItemDefName) (Item*) inv_bag_trh_GetItemByName(ATR_EMPTY_ARGS,  CONTAINER_NOCONST(InventoryBag, (pBag)), ItemDefName)

int inv_lite_trh_CountItemByName(ATR_ARGS, ATH_ARG NOCONST(InventoryBagLite)* pBag, const char* ItemDefName);

bool inv_trh_MatchingItemInSlot(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, int SlotIdx, SA_PARAM_NN_STR const char* ItemDefName);
#define inv_MatchingItemInSlot(pBag, SlotIdx, ItemDefName) inv_trh_MatchingItemInSlot(ATR_EMPTY_ARGS, CONTAINER_NOCONST(InventoryBag, (pBag)), SlotIdx, ItemDefName)

void inv_trh_GetSlotByItemName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, const char* ItemDefName, InvBagIDs *pRetBagID, int *pRetSlot, GameAccountDataExtract *pExtract);
#define inv_GetSlotByItemName(pEnt, BagID, ItemDefName, pRetBagID, pRetSlot, pExtract) inv_trh_GetSlotByItemName(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, ItemDefName, pRetBagID, pRetSlot, pExtract)

NOCONST(InventorySlot)* inv_trh_GetSlotPtr(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, int SlotIdx);
#define inv_GetSlotPtrNoConst(pBag, SlotIdx) CONTAINER_RECONST(InventorySlot, inv_trh_GetSlotPtr(ATR_EMPTY_ARGS,  (pBag), SlotIdx))
#define inv_GetSlotPtr(pBag, SlotIdx) inv_GetSlotPtrNoConst(CONTAINER_NOCONST(InventoryBag, (pBag)), SlotIdx)

NOCONST(InventorySlot)* inv_ent_trh_GetSlotPtr(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, int SlotIdx, GameAccountDataExtract *pExtract);
#define inv_ent_GetSlotPtr( pEnt, BagID, SlotIdx, pExtract) (InventorySlot*) inv_ent_trh_GetSlotPtr(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, SlotIdx, pExtract)

NOCONST(InventorySlot)* inv_guildbank_trh_GetSlotPtr(ATR_ARGS, ATH_ARG NOCONST(Entity)* pGuildBank, InvBagIDs iBagID, int iSlotIdx);
#define inv_guildbank_GetSlotPtr(pGuildBank, iBagID, iSlotIdx) (InventorySlot*) inv_guildbank_trh_GetSlotPtr(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pGuildBank)), iBagID, iSlotIdx)
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

S32 inv_trh_GetNumericValue(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char* ItemDefName);
S32 inv_GetNumericItemValueScaled(Entity* pEnt, const char* ItemDefName);
#define inv_GetNumericItemValue(pEnt, ItemDefName) inv_trh_GetNumericValue(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), ItemDefName)

S32 inv_guildbank_trh_GetNumericItemValue(ATR_ARGS, ATH_ARG NOCONST(Entity)* pGuildBank, S32 iBagID, const char* pcItemDefName);
#define inv_guildbank_GetNumericItemValue(pGuildBank, iBagID, pcItemDefName) inv_guildbank_trh_GetNumericItemValue(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pGuildBank)), iBagID, pcItemDefName)

const char *inv_GetNumericItemDisplayName(Entity* pEnt, const char* ItemDefName);

// Require a bag
bool inv_lite_trh_SetNumeric(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(InventoryBagLite)* pBag, const char* pcItemDefName, S32 iValue, const ItemChangeReason *pReason);
bool inv_lite_trh_AddNumeric(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(InventoryBagLite)* pBag, const char* pcItemDefName, S32 iValue, const ItemChangeReason *pReason);
bool inv_lite_trh_ApplyNumeric(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(InventoryBagLite)* pBag, const char* pcItemDefName, S32 iValue, NumericOp op, const ItemChangeReason *pReason);
int inv_bag_trh_AddNumericItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(InventoryBag)* pDestBag, int SlotIdx, const char* ItemDefName, int count, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract );

// Wrappers that get the bag for you
bool inv_ent_trh_SetNumeric(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, const char* pcItemDefName, S32 iValue, const ItemChangeReason *pReason);
bool inv_ent_trh_AddNumeric(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, const char* pcItemDefName, S32 iValue, const ItemChangeReason *pReason);
bool inv_ent_trh_ApplyNumeric(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, const char* pcItemDefName, S32 iValue, NumericOp op, const ItemChangeReason *pReason);

bool inv_trh_CopyNumerics(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEntSrc, ATH_ARG NOCONST(Entity)* pEntDst, const ItemChangeReason *pReason );

S32 inv_trh_HasToken(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char* ItemDefName);
#define inv_HasToken(pEnt, pchItemName) inv_trh_HasToken(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), pchItemName)
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines to deal with EntityBuild inventory tracking

void inv_ent_trh_BuildFill(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(EntityBuild)* pBuild);

void inv_ent_trh_BuildCurrentFill(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent);

void inv_ent_trh_BuildCurrentSetItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, InvBagIDs BagID, int iSlot, U64 itemID);

//routines to deal with EntityBuild inventory tracking
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
InvBagIDs inv_trh_GetBestBagForItemDef(ATH_ARG NOCONST(Entity)* pEnt, ItemDef* pItemDef, int iCount, bool bGive, GameAccountDataExtract *pExtract);
#define GetBestBagForItemDef( pEnt, pItemDef, iCount, bGive, pExtract) inv_trh_GetBestBagForItemDef(CONTAINER_NOCONST(Entity, (pEnt)), pItemDef, iCount, bGive, pExtract)

bool inv_trh_CanEquipUnique(ATR_ARGS, ItemDef* pItemDef, ATH_ARG NOCONST(InventoryBag)* pBag);
#define inv_CanEquipUniqueNoConst(pItemDef, pBag) inv_trh_CanEquipUnique(ATR_EMPTY_ARGS, pItemDef, (pBag))
#define inv_CanEquipUnique(pItemDef, pBag) inv_CanEquipUniqueNoConst(pItemDef, CONTAINER_NOCONST(InventoryBag, (pBag)))
bool inv_trh_CanEquipUniqueCheckSwap(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ItemDef* pSrcItemDef, ItemDef* pDstItemDef, NOCONST(InventoryBag)* pSrcBag, NOCONST(InventoryBag)* pDstBag);
#define inv_CanEquipUniqueCheckSwap(pEnt, pSrcItemDef, pDstItemDef, pSrcBag, pDstBag) inv_trh_CanEquipUniqueCheckSwap(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), pSrcItemDef, pDstItemDef, CONTAINER_NOCONST(InventoryBag, (pSrcBag)), CONTAINER_NOCONST(InventoryBag, (pDstBag)))
bool inv_bag_HasAnyUniqueItems(InventoryBag* pBag);
bool inv_CheckUniqueItemsInBags(InventoryBag** eaBags);

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#define inv_HasLoot( pEnt ) (pEnt && pEnt->pCritter && eaSize(&pEnt->pCritter->eaLootBags) > 0)
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
bool inv_trh_UnlockInterior( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(Item)* pItem );
bool inv_trh_UnlockCostumeOnItem( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(Item)* pItem, GameAccountDataExtract *pExtract );
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
// Routines that log guild bank transactions and track withdraw limits

bool inv_guildbank_trh_CanWithdrawFromBankTab(ATR_ARGS, ContainerID iContainerID, ATH_ARG NOCONST(Guild) *pGuild, ATH_ARG NOCONST(Entity) *pGuildBank, InvBagIDs iBagID, U32 iCount, U32 iValue);
#define inv_guildbank_CanWithdrawFromBankTab(iContainerID, pGuild, pGuildBank, iBagID, iCount, iValue) inv_guildbank_trh_CanWithdrawFromBankTab(ATR_EMPTY_ARGS, iContainerID, CONTAINER_NOCONST(Guild, (pGuild)), CONTAINER_NOCONST(Entity, (pGuildBank)), (iBagID), (iCount), (iValue))

bool inv_guildbank_trh_ManageBankWithdrawLimitAndLog(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Guild) *pGuild, ATH_ARG NOCONST(Entity) *pGuildBank,
	S32 iRequestedCount, NOCONST(Item) *pSrcItem,
	bool bSrcGuild, NOCONST(InventoryBag) *pSrcBag, int iSrcBagID, S32 iSrcEPValue, S32 iSrcSlot,
	bool bDstGuild, NOCONST(InventoryBag) *pDstBag, int iDstBagID, S32 iDstEPValue, S32 iDstSlot);
bool inv_guildbank_trh_UpdateBankTabWithdrawLimit(ATR_ARGS, ContainerID iContainerID, ATH_ARG NOCONST(Guild) *pGuild, ATH_ARG NOCONST(Entity) *pGuildBank, InvBagIDs iBagID, U32 iCount, U32 iValue);
void inv_guild_trh_AddLog(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(Guild) *pGuild, ATH_ARG NOCONST(Item) *pItem, const char* pchItemDefName, InvBagIDs bagID, S32 iCount);
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


NOCONST(InventoryBag)* inv_trh_PlayerBagFromSlotIdx(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int SlotIdx );
#define inv_PlayerBagFromSlotIdx( pEnt, iSlot ) (InventoryBag*)inv_trh_PlayerBagFromSlotIdx( ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), iSlot )
void inv_trh_UpdatePlayerBags(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, GameAccountDataExtract *pExtract);

bool inv_trh_PlayerBagFail(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pBag, int SlotIdx );
#define inv_PlayerBagFail(pEnt, pBag, SlotIdx) inv_trh_PlayerBagFail(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), CONTAINER_NOCONST(InventoryBag, (pBag)), SlotIdx )

void inv_trh_ItemAddedCallbacks(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(Item)* pItem, ATH_ARG NOCONST(InventoryBag)* pDestBag, int DestSlotIdx, int count, bool bForceBind, const ItemChangeReason *pReason);
void inv_trh_ItemLiteAddedCallbacks(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(InventorySlot)* pSlot, ATH_ARG NOCONST(InventoryBag)* pDestBag, int iDestSlotIdx, int count );
void inv_trh_ItemRemovedCallbacks(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(Item)* pItem, ATH_ARG NOCONST(InventoryBag)* pSrcBag, int SrcSlotIdx, int count, const ItemChangeReason *pReason );
void inv_trh_ItemRemovedCallbacksBySlot(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, InvBagIDs BagID, int iSlot, int count, const ItemChangeReason *pReason , GameAccountDataExtract *pExtract);

int inv_bag_trh_GetMaxDropCount(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pDestBag, ATH_ARG NOCONST(Item)* pItem);
#define inv_GetMaxDropCount(pEnt, pBag, pItem) inv_bag_trh_GetMaxDropCount(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), CONTAINER_NOCONST(InventoryBag, (pBag)), CONTAINER_NOCONST(Item, (pItem)))

bool inv_bag_trh_CanItemFitInBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pDestBag, ATH_ARG NOCONST(Item)* pItem, int count);
#define inv_CanItemFitInBag(pBag, pItem, iCount) inv_bag_trh_CanItemFitInBag(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), CONTAINER_NOCONST(InventoryBag, (pBag)), CONTAINER_NOCONST(Item, (pItem)), iCount)
bool inv_bag_trh_CanItemDefFitInBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pDestBag, ItemDef* pItemDef, int count);
#define inv_CanItemDefFitInBag(pEnt, pBag, pItemDef, iCount) inv_bag_trh_CanItemDefFitInBag(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), CONTAINER_NOCONST(InventoryBag, (pBag)), pItemDef, iCount)

void inv_InventorySlotSetNameFromIndex(ATH_ARG NOCONST(InventorySlot)* pSlot, S32 iIndex);
NOCONST(InventorySlot) *inv_InventorySlotCreate(S32 iIndex);

bool inv_EntCouldCraftRecipe(Entity* pEnt, ItemDef* pRecipe);

// Pushes all equipped Items that have the ItemCategory into the earray
void inv_ent_FindEquippedItemsByCategory(SA_PARAM_OP_VALID Entity *pEnt, ItemCategory eCategory, SA_PARAM_NN_VALID Item ***pppItems, GameAccountDataExtract *pExtract);
void inv_ent_FindEquippedItemsWithCategories(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID int * piCategories, SA_PARAM_NN_VALID Item ***pppItems, GameAccountDataExtract *pExtract);
void inv_ent_FindItemsInBagByCategory(Entity *pEnt, ItemCategory eCategory, const char* pchBagName, Item ***pppItems, GameAccountDataExtract *pExtract);

// Returns true if there is any equipped item found with any of the given categories
bool inv_ent_HasAnyEquippedItemsWithCategory(Entity *pEnt, const int *piCategories, GameAccountDataExtract *pExtract);

ItemDef *inv_ent_trh_FindItemDefFromAlias(ATH_ARG NOCONST(Entity) *pEnt, const char *AliasName);
#define inv_ent_FindItemDefFromAlias(pEnt,AliasName) inv_ent_trh_FindItemDefFromAlias(CONTAINER_NOCONST(Entity, pEnt),AliasName)

Item* invbag_GetActiveSlotItem(InventoryBag *pBag, S32 activeIndex);
S32 invbag_GetActiveSlot(InventoryBag *pBag, S32 activeIndex);
bool invbag_IsActiveSlot(InventoryBag *pBag, S32 iSlot);

bool invbag_IsValidActiveSlotChange(Entity* pEnt, InventoryBag* pBag, int* piActiveSlots, S32 iActiveSlotIndex, S32 iNewActiveSlot);
bool invbag_CanChangeActiveSlot(Entity* pEnt, InventoryBag* pBag, S32 iActiveSlotIndex, S32 iNewActiveSlot);
void invbag_HandleMoveEvents(Entity* pEnt, InventoryBag* pBag, S32 iNewActiveSlot, U32 uiTime, U32 uiRequestID, bool bCheckActiveSlot);
bool inv_AddActiveSlotChangeRequest(Entity* pEnt, InventoryBag* pBag, S32 iIndex, S32 iNewActiveSlot, U32 uiRequestID, F32 fDelayOverride);

bool ItemEntHasUnlockedCostumes(const Entity *pEnt, const Item *pItem);

LATELINK;
void inv_LevelNumericSet_GetNotifyString(Entity* pEnt, const char* pchItemDisplayName, S32 iLevel, char** pestr);

// new inventory functionality
// AB NOTE: eventually remove the above functionality 12/10/10
// ===============================================================================

AUTO_ENUM;
typedef enum ItemAddFlags
{
	ItemAdd_Silent					= (1 << 0),
	ItemAdd_OverrideStackRules		= (1 << 1),
	ItemAdd_IgnoreUnique			= (1 << 2),
	ItemAdd_ForceBind				= (1 << 3),
	ItemAdd_UseOverflow				= (1 << 4),
	ItemAdd_FromBuybackOkay			= (1 << 5),
	ItemAdd_ClearID					= (1 << 6),
} ItemAddFlags;

// ===============================================================================
// Creation functions

NOCONST(Item)* inv_ItemInstanceFromDefName(const char* ItemDefName, int iCharacterLevel, int overrideLevel, const char *pcRank, AllegianceDef *pAllegiance, AllegianceDef *pSubAllegiance, bool bUseOverrideLevel, U32 *pSeed);
Item* item_FromMission(const char *def_name, const char *mission);
Item* item_FromEnt(ATH_ARG NOCONST(Entity)* pEnt, const char* ItemDefName, int overrideLevel, const char *pcRank, ItemAddFlags eFlags);
Item* item_FromSavedPet(Entity *ent, S32 pet_id);
Item* item_FromPetInfo(char const *def_name, ContainerID id, AllegianceDef *allegiance, AllegianceDef *suballegiance, ItemQuality quality);
#define item_FromDefName(def_name) CONTAINER_RECONST(Item, inv_ItemInstanceFromDefName(def_name,0,0,NULL,NULL,NULL,false,NULL))



// ===============================================================================
// Inventory manipulation

bool inv_bag_trh_AddItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, ATH_ARG NOCONST(InventoryBag)* pDestBag, int iDestSlotIdx, ATH_ARG NOCONST(Item)* pAddItem, ItemAddFlags eFlags, U64** peaItemIDs, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

enumTransactionOutcome inv_AddItem(ATR_ARGS, NOCONST(Entity)* pEnt, CONST_EARRAY_OF(NOCONST(Entity)) eaPets, int BagID, int SlotIdx, NON_CONTAINER Item* pItem, const char* pchItemdefName, int /*ItemAddFlags*/ eFlags, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

bool inv_ent_trh_AddItemFromDef(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, int BagID, int SlotIdx, const char* ItemDefName, int Count, int overrideLevel, const char *pcRank, ItemAddFlags eFlags, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
bool inv_ent_AddItemFromDef(Entity *pEnt, int iBagID, int iSlotIndex, const char *pchItemDefName, int iCount, ItemAddFlags eFlags, GameAccountDataExtract *pExtract);

Item* invbag_RemoveItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, int BagID, int SlotIdx, int count, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
enumTransactionOutcome inventory_RemoveItemByDefName(ATR_ARGS, NOCONST(Entity)* pEnt, const char *def_name, int count, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
enumTransactionOutcome inventory_RemoveAllItemByDefName(ATR_ARGS, NOCONST(Entity)* pEnt, const char *def_name, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
Item* inv_RemoveItemByID(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, U64 item_id, int count, ItemAddFlags eFlags, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
S32 invbag_RemoveItemByDefName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int BagID, const char* def_name, int count, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
NOCONST(Item)* inv_bag_trh_RemoveItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(InventoryBag)* pBag, int SlotIdx, int count, const ItemChangeReason *pReason);

bool inv_MoveItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pDstEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaDstPets, NOCONST(InventoryBag) *pDestBag, int iDstSlot, ATH_ARG NOCONST(Entity)* pSrcEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaSrcPets, NOCONST(InventoryBag) *pSrcBag, int iSrcSlot, int iCount, int eFlags, const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason, GameAccountDataExtract *pExtract);
bool inv_MoveItemSelf(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int SrcBagID, int DestBagID, int SrcSlotIdx, int DestSlotIdx, int Count, int eFlags, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

// ===============================================================================
// Inventory queries

Item* invbag_GetItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, int SlotIdx, GameAccountDataExtract *pExtract);

NOCONST(InventoryBag)* inv_trh_GetBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, GameAccountDataExtract *pExtract);
#define inv_GetBag(pEnt, BagID, pExtract) inv_trh_GetBag(ATR_EMPTY_ARGS, pEnt, BagID, pExtract)
NOCONST(InventoryBagLite)* inv_trh_GetLiteBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs bag_id, GameAccountDataExtract *pExtract);
#define inv_GetLiteBag(pEnt, BagID, pExtract) inv_trh_GetLiteBag(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), BagID, pExtract)

void inv_FillItemChangeReason(ItemChangeReason *pReason, Entity *pEnt, const char *pcReason, const char *pcDetail);
void inv_FillItemChangeReasonKill(ItemChangeReason *pReason, Entity *pEnt, Entity *pKilledEntity);
void inv_FillItemChangeReasonStore(ItemChangeReason *pReason, Entity *pEnt, const char *pcReason, const char *pcDetail);
ItemChangeReason *inv_CreateItemChangeReason(Entity *pEnt, const char *pcReason, const char *pcDetail);


BagIterator *invbag_IteratorFromLiteBag(ATH_ARG NOCONST(InventoryBagLite) *bag);
BagIterator *invbag_IteratorFromBag(ATH_ARG NOCONST(InventoryBag) *bag);
BagIterator *invbag_trh_IteratorFromEnt(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int BagID, GameAccountDataExtract *pExtract);
#define invbag_IteratorFromEnt(pEnt, BagID, pExtract) invbag_trh_IteratorFromEnt(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), BagID, pExtract)
BagIterator *invbag_trh_LiteIteratorFromEnt(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int BagID, GameAccountDataExtract *pExtract);
#define invbag_LiteIteratorFromEnt(pEnt, BagID, pExtract) invbag_trh_LiteIteratorFromEnt(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), BagID, pExtract)
BagIterator *invbag_IteratorFromBagEarray(ATH_ARG NOCONST(InventoryBag)*** peaBags);
bool bagiterator_Next(BagIterator *iter);
bool bagiterator_Stopped(BagIterator *iter);
ItemDef* bagiterator_GetDef(BagIterator *iter);
bool bagiterator_ItemDefStillLoading(BagIterator *iter);
NOCONST(Item)* bagiterator_GetItem(BagIterator *iter);
Item *bagiterator_RemoveItem(ATR_ARGS, BagIterator *iter, ATH_ARG NOCONST(Entity)* ent,  int count, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
int bagiterator_GetSlotID(BagIterator *iter);
InventorySlot *bagiterator_GetSlot(BagIterator *iter);
int bagiterator_GetItemCount(BagIterator *iter);
void bagiterator_Destroy(BagIterator *iter);
char *bagiterator_GetSlotName(BagIterator *iter);
NOCONST(InventoryBag)* bagiterator_GetCurrentBag(BagIterator *iter);
InvBagIDs bagiterator_GetCurrentBagID(BagIterator *iter);
NOCONST(InventoryBagLite)* bagiterator_GetCurrentLiteBag(BagIterator *iter);

BagIterator* inv_bag_FindItem(Entity* pEnt, int BagID, BagIterator* res, ItemFindCriteria eCriteria, const void* pSearchParam, bool bIncludePlayerBags, bool bIncludeOverflow);

BagIterator* inv_bag_trh_MultiBagFindItem(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)*** peaBags, BagIterator* pIter, ItemFindCriteria eCriteria, const void* pSearchParam);
BagIterator* inv_bag_trh_FindItem(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, BagIterator* pIter, ItemFindCriteria eCriteria, const void* pData);
BagIterator* inv_bag_trh_FindItemByDefName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int BagID, const char* def_name, BagIterator* res);
BagIterator* inv_bag_trh_FindItemByType(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int BagID, int eType, BagIterator* res);
BagIterator* inv_bag_trh_FindItemByID(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, U64 item_id, BagIterator* res);

BagIterator* inv_trh_FindItemByIDEx(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, BagIterator* pIter, U64 item_id, bool bIncludeOverflowBag, bool bSearchAllBags);
BagIterator* inv_trh_FindItemByID(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, U64 item_id);

S32 inv_bag_trh_FindItemCountByDefName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, S32 eBagID, const char* pchDefName, int iCount, U32 bRemoveFound, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract);
S32 inv_bag_trh_RemoveAllItemByDefName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, S32 eBagID, const char* pchDefName, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract);

#define inv_bag_FindItemCountByDefName(pEnt, eBagID, pchDefName, iCount) inv_bag_trh_FindItemCountByDefName(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), eBagID, pchDefName, iCount, false, NULL)
S32 inv_trh_FindItemCountByDefName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char* pchDefName, int iCount, U32 bRemoveFound, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract);
S32 inv_trh_FindItemCountByDefNameEx(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagFlag searchBagFlags, const char* pchDefName, int iCount, U32 bRemoveFound, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract);
#define inv_FindItemCountByDefName(pEnt, pchDefName, iCount) inv_trh_FindItemCountByDefNameEx(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), InvBagFlag_DefaultInventorySearch, pchDefName, iCount, false, NULL, NULL)
#define inv_FindItemCountByDefNameEx(pEnt, bagFlags, pchDefName, iCount) inv_trh_FindItemCountByDefNameEx(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), (bagFlags), pchDefName, iCount, false, NULL, NULL)

S32 inv_trh_RemoveAllItemByDefName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char* pchDefName, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract);

bool invbag_AddItem(NOCONST(InventoryBag)* pDestBag, int iDestSlotIdx, Item* pItem, int iCount, ItemAddFlags eFlags, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

#if !AILIB
#include "AutoGen/inventoryCommon_h_ast.h"
#endif

const InvBagDef *invbag_trh_def(ATH_ARG NOCONST(InventoryBag) *bag);
const InvBagDef *invbaglite_trh_def(ATH_ARG NOCONST(InventoryBagLite) *bag);
__forceinline static InvBagDef const *invbag_def(InventoryBag const *bag) { return invbag_trh_def(CONTAINER_NOCONST(InventoryBag, bag)); }
__forceinline static InvBagDef const *invbaglite_def(InventoryBagLite const *bag) { return invbaglite_trh_def(CONTAINER_NOCONST(InventoryBagLite, bag)); }

// Returns the highest item quality in the inventory bag
S32 invbag_GetHighestItemQuality(SA_PARAM_NN_VALID InventoryBag *pRewardBag, SA_PARAM_OP_VALID bool * pbHasMissionItem);

S32 inv_bag_trh_ItemMoveFindBestEquipSlot(ATR_ARGS, ATH_ARG NOCONST(Entity)* pDstEnt, ATH_ARG NOCONST(InventoryBag)* pDstBag, ItemDef* pSrcItemDef, S32 bTrySwap, S32* pbCycleSecondary);
#define inv_bag_ItemMoveFindBestEquipSlot(pDstEnt, pDstBag, pSrcItemDef, bTrySwap, pbCycleSecondary) inv_bag_trh_ItemMoveFindBestEquipSlot(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pDstEnt), CONTAINER_NOCONST(InventoryBag, (pDstBag)), pSrcItemDef, bTrySwap, pbCycleSecondary)
bool inv_trh_GemItem(ATH_ARG NOCONST(Item) *pHolder,ATH_ARG NOCONST(Item) *pGem, int iDestGemSlot);
bool inv_bag_trh_MoveItem(ATR_ARGS, bool bSilent, ATH_ARG NOCONST(Entity)* pDstEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaDstPets, ATH_ARG NOCONST(InventoryBag)* pDstBag, int iDstSlot, ATH_ARG NOCONST(Entity)* pSrcEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaSrcPets, ATH_ARG NOCONST(InventoryBag)* pSrcBag, int iSrcSlot, int iCount, int bSrcBuyBackBagAllowed, bool bUseOverflow, const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason, GameAccountDataExtract *pExtract);
bool inv_trh_MoveGemItem(ATR_ARGS, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaDstPets, NOCONST(InventoryBag) *pDestBag, int iDstSlot, int iDstGemSlot, U64 uDstItemID, ATH_ARG NOCONST(Entity) *pSrcEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaSrcPets, NOCONST(InventoryBag) *pSrcBag, int iSrcSlot, U64 uSrcItemID, int iCount, int /*ItemAddFlags*/ eFlags, const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason, GameAccountDataExtract *pExtract, const char *pDestEntDebugName );
bool inv_trh_UnGemItem(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(InventoryBag) *pSrcBag, int SrcSlotIdx, U64 uSrcItemID, int iGemSlotIdx, ATH_ARG NOCONST(InventoryBag) *pDestBag, int DestSlotIdx, const char *pchCostNumeric, int iCost, GameAccountDataExtract *pExtract, const ItemChangeReason *pReason);
bool inv_ent_HasAllItemCategoriesEquipped(Entity *pEnt, int * piCategories, GameAccountDataExtract *pExtract, int *piMissingCategory);
bool inv_trh_CanGemSlot(ATH_ARG NOCONST(Item) *pHolder, ATH_ARG NOCONST(Item) *pGem, int iDestGemSlot);
#define inv_CanGemSlot(pHolder, pGem, iDestGemSlot) inv_trh_CanGemSlot(CONTAINER_NOCONST(Item, pHolder), CONTAINER_NOCONST(Item, pGem), iDestGemSlot)

ItemQuality inv_FillRewardRequest(InventoryBag** eaRewardBags, InvRewardRequest* pRequest);
bool inv_FillNumericRewardRequestClient(Entity* pEnt, ItemNumericData* pNumericData, InvRewardRequest* pOutRequest);

// Takes a InvRewardRequest and processes the eaNumericRewards and eaItemRewards into the client_only list eaRewards
// bPlaceNumericsAfterItems determines whether the numerics are placed at the beginning or end of the list
void inv_FillRewardRequestClient(Entity* pEnt, InvRewardRequest* pInRequest, InvRewardRequest* pOutRequest, bool bPlaceNumericsAfterItems);

// for bags with a MaxSlotTable, will return the max slots the table can have
S32 inv_GetBagMaxSlotTableMaxSlots(InventoryBag *pBag);


bool inv_trh_GiveRewardBags(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, GiveRewardBagsData *pRewardBagsData, U32 eOverflow, S32* peFailBag, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract, RewardPackLog *pRewardPackLog);
enumTransactionOutcome inv_tr_GiveNumericBag(ATR_ARGS, NOCONST(Entity)* pEnt, NON_CONTAINER InventoryBag *pRewardBag, const ItemChangeReason *pReason);
void inv_ent_trh_RegisterUniqueItem( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char* pcItemName, bool bOwned);
#endif

bool inv_trh_ent_MigrateBagV1ToV2(ATR_ARGS, ATH_ARG NOCONST(InventoryBagV1)* pOldBag, ATH_ARG NOCONST(InventoryBag)* pNewBag);
bool inv_trh_ent_MigrateBagV1ToV2Lite(ATR_ARGS, ATH_ARG NOCONST(InventoryBagV1)* pOldBag, ATH_ARG NOCONST(InventoryBagLite)* pNewBagLite);
bool inv_trh_ent_MigrateInventoryV1ToV2(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt);
bool inv_trh_ent_MigrateSlotV1ToV2(ATR_ARGS, ATH_ARG NOCONST(InventorySlotV1)* pOldSlot, ATH_ARG NOCONST(InventorySlot)* pNewSlot);

NOCONST(GuildBankTabInfo) * inv_guildbank_trh_GetBankTabInfo(ATH_ARG NOCONST(Entity) *pGuildBank, InvBagIDs iBagID);
#define inv_GuildbankGetBankTabInfo(pGuildBank, iBagID) inv_guildbank_trh_GetBankTabInfo(CONTAINER_NOCONST(Entity, pGuildBank), iBagID)

int inv_ent_trh_MoveAllItemsFromBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pSrcBag, ATH_ARG NOCONST(InventoryBag)* pDestBag, bool bUseOverflow, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

bool invBagIDs_BagIDCanAffectCostume(InvBagIDs id);

bool inv_ent_trh_RemoveItemByID(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, U64 uiItemId, int count);

bool inv_ent_trh_MoveItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, int SrcBagID, int DestBagID, int SrcSlotIdx, int DestSlotIdx, int Count, int bSrcBuyBackBagAllowed, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

LATELINK;
U32 inv_ent_getEntityItemQuality(Entity *pEnt);

NOCONST(Item)* inv_UnidentifiedWrapperFromDefName(const char* pchWrapperDefName, const char* pchResultDefName);