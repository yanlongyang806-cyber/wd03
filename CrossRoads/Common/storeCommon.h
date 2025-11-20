#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "itemCommon.h"
#include "referencesystem.h"

typedef struct ContactDef ContactDef;
typedef struct ExprContext ExprContext;
typedef struct ExprFuncTable ExprFuncTable;
typedef struct ItemDef ItemDef;
typedef struct Entity Entity;
typedef struct RewardTable RewardTable;
typedef struct MicroTransactionDef MicroTransactionDef;
typedef enum GroupProjectType GroupProjectType;
typedef struct GroupProjectDef GroupProjectDef;
typedef struct GroupProjectNumericDef GroupProjectNumericDef;

#define STORES_BASE_DIR "defs/stores"
#define STORES_BIN_FILE "Store.bin"
#define STORES_EXTENSION "store"

// Enumeration of different store types
AUTO_ENUM;
typedef enum StoreContents
{
	Store_All,
	Store_Recipes,
	Store_Costumes,
	Store_Injuries,
	Store_Sellable_Items,
} StoreContents;

AUTO_ENUM;
typedef enum StoreCanBuyError
{
	kStoreCanBuyError_None,
	kStoreCanBuyError_Unknown,
	kStoreCanBuyError_CannotEquipPet,
	kStoreCanBuyError_RequiredMission,
	kStoreCanBuyError_CanBuyRequirements,
	kStoreCanBuyError_RequiredNumeric,
	kStoreCanBuyError_CostRequirement,
	kStoreCanBuyError_InvalidRecipe,
	kStoreCanBuyError_RecipeAlreadyKnown,
	kStoreCanBuyError_RecipeRequirements,
	kStoreCanBuyError_InvalidCostume,
	kStoreCanBuyError_CostumeRequirements,
	kStoreCanBuyError_CostumeAlreadyUnlocked,
	kStoreCanBuyError_BagFull,
	kStoreCanBuyError_BagMissing,
	kStoreCanBuyError_MaxPuppets,
	kStoreCanBuyError_InvalidAllegiance,
	kStoreCanBuyError_ItemUnique,
	kStoreCanBuyError_PetAcquireLimit,
	kStoreCanBuyError_NotCurrentlyAvailable,		// Typically we have failed the ShowExpr or have an associated event that is not active
} StoreCanBuyError;

// Dynamic enum for store categories.
// Store categories are used to categorize items in stores so that the UI can sort or filter them.
extern DefineContext *g_pDefineStoreCategories;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineStoreCategories);
typedef enum StoreCategory
{
	kStoreCategory_None,				ENAMES(None)

	kStoreCategory_FIRST_DATA_DEFINED,	EIGNORE

} StoreCategory;

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineStoreHighlightCategories);
typedef enum StoreHighlightCategory
{
	kStoreHighlightCategory_FIRST_DATA_DEFINED,	EIGNORE
} StoreHighlightCategory;

// Defines a single item in a store
AUTO_STRUCT AST_IGNORE(maxAvailable) AST_IGNORE(rechargeRate);
typedef struct StoreItemDef
{
	REF_TO(ItemDef) hItem;			AST(NAME(Item))
	U32 iCount; // The amount you buy in one batch (0 is the same as 1)

	bool bForceUseCurrency;
	
	REF_TO(MissionDef) hReqMission;	AST(NAME(ReqMission))
	Expression *pExprCanBuy;		AST(LATEBIND NAME(ExprCanBuy))
	DisplayMessage cantBuyMessage;	AST(STRUCT(parse_DisplayMessage))
	DisplayMessage longFlavorDesc;	AST(STRUCT(parse_DisplayMessage))
	DisplayMessage requirementsMessage;	AST(NAME(RequirementsMessage) STRUCT(parse_DisplayMessage))
	const char* pchDisplayTex;		AST(NAME(DisplayTex) POOL_STRING)
	REF_TO(MicroTransactionDef) hReqMicroTransaction; AST(NAME(ReqMicroTransaction))

	// This array can be used to override the ValueRecipes array in the ItemDef
	ItemDefRef **ppOverrideValueRecipes;	AST(NAME(OverrideValueRecipe))

	// Override any other prices with an Account Server key-value that is consumed one at a time to buy this item
	const char *pPriceAccountKeyValue;	AST(NAME(OverridePriceAccountKeyValue) POOL_STRING)

	// a flag that forces the purchased item to be immediately bound
	bool bForceBind;				AST(NAME(ForceBind))

	// Optional category for UI sorting
	StoreCategory eCategory;		AST(NAME(Category))

	// Optional categories for highlighting in the UI
	StoreHighlightCategory *peHighlightCategory;	AST(NAME(HighlightCategory) SUBTABLE(StoreHighlightCategoryEnum))

	// if present, defines which numeric gates the purchase of this item
	// In STO we use this for memory alpha crafting, which is implemented as stores.
	// Each time you buy(craft) an item, it will increment a tracking numeric to skill up your crafting.
	// Item purchase is also gated by the required value of the numeric (crafting skill)
	REF_TO(ItemDef) hRequiredNumeric;	AST(NAME(RequiredNumeric))

	// The value of the required numeric that the player must have in order to purchase this item
	// Use this rather than the "can buy" expression to make the value available for tooltips or other UI
	S32 requiredNumericValue;		AST(NAME(RequiredNumericValue))

	// The amount to increment the required numeric when purchasing this item.
	S32 requiredNumericIncr;		AST(NAME(RequiredNumericIncr)) 

	// The amount of time it takes to purchase the item (in seconds)
	U32 uResearchTime;				AST(NAME(ResearchTime, PurchaseTime))
	
	// This expression if present and false will prevent this item from even showing in the store list
	Expression *pExprShowItem;		AST(LATEBIND NAME(ExprShowItem))

	// This Activity must be active in order for the item to appear in the store
	const char*	pchActivityName; AST(NAME(ActivityName, RequiredActivity) POOL_STRING)

} StoreItemDef;

AUTO_STRUCT;
typedef struct StoreRestockDef
{
	const char* pchName;				AST(NAME(Name) STRUCTPARAM POOL_STRING)

	//The reward table used to periodically generate items
	REF_TO(RewardTable) hRewardTable;	AST(NAME(RewardTable))

	//The level to pass into the reward table
	S32 iItemLevel;						AST(NAME(ItemLevel))

	//Optional UI category to assign to each item created from the reward table
	StoreCategory eCategory;			AST(NAME(Category))
	
	//The min/max amount of time (in seconds) that it takes to replenish one of this type of item
	U32 uReplenishTimeMin;				AST(NAME(MinReplenishTime))
	U32 uReplenishTimeMax;				AST(NAME(MaxReplenishTime))
	
	//The min/max amount of time (in seconds) that it takes to remove one of these items
	U32 uExpireTimeMin;					AST(NAME(MinExpireTime))
	U32 uExpireTimeMax;					AST(NAME(MaxExpireTime))

	//If non-zero, the min/max number of items that can be filled for this category
	U32 uMinItemCount;					AST(NAME(MinItemCount))
	U32 uMaxItemCount;					AST(NAME(MaxItemCount))
} StoreRestockDef;

// Information about discounts that the client cares about
AUTO_STRUCT;
typedef struct StoreDiscountInfo
{
	const char* pchName;				AST(KEY STRUCTPARAM POOL_STRING)
	REF_TO(Message) hDisplayName;
	REF_TO(Message) hDescription;
	F32 fDiscountPercent;
	REF_TO(ItemDef) hDiscountCostItem;	AST(NAME(DiscountCostItem))
} StoreDiscountInfo;

// Available discounts offered by stores
AUTO_STRUCT;
typedef struct StoreDiscountDef
{
	// Internal name of this discount
	const char* pchName;				AST(STRUCTPARAM POOL_STRING)

	// The display name of this discount
	DisplayMessage msgDisplayName;		AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))

	// The description of this discount
	DisplayMessage msgDescription;		AST(NAME(Description) STRUCT(parse_DisplayMessage))

	// Apply the discount if this expression evaluates to true
	Expression *pExprRequires;			AST(NAME(ExprBlockRequires,ExprRequiresBlock), REDUNDANT_STRUCT(ExprRequires, parse_Expression_StructParam), LATEBIND)

	// Amount to discount items
	F32 fDiscountPercent;				AST(NAME(DiscountPercent))

	// The items to apply the discount to. Applies to all items in the store if unset.
	S32 eApplyToItemCategory;			AST(NAME(ApplyToItemCategory) SUBTABLE(ItemCategoryEnum))

	// The cost item to discount
	REF_TO(ItemDef) hDiscountCostItem;	AST(NAME(DiscountCostItem))

} StoreDiscountDef;

// Defines a store's inventory
AUTO_STRUCT AST_IGNORE(AutoLearn) AST_IGNORE(TempData);
typedef struct StoreDef
{
	// Store name that uniquely identifies a store.  Required.
	const char* name;						AST(STRUCTPARAM KEY POOL_STRING)
	
	// Filename
	const char* filename;					AST(CURRENTFILE)
	
	// Scope helps determine filename
	const char* scope;						AST(POOL_STRING SERVER_ONLY)
	
	// Comments for designers (not used at runtime)
	char* notes;							AST(SERVER_ONLY)
	
	// List of items this store can have
	StoreItemDef** inventory;

	// Periodic item drop data (for persisted stores)
	StoreRestockDef** eaRestockDefs;		AST(NAME(RestockDef))

	/// List of discounts
	StoreDiscountDef** eaDiscountDefs;		AST(NAME(DiscountDef))
	
	// The default numeric to use as a currency for items without a value recipe
	REF_TO(ItemDef) hCurrency;

	// Value multipliers for the vendor
	F32 fBuyMultiplier;				AST(DEF(1.0))
	F32 fSellMultiplier;			AST(DEF(1.0))
	
	// This expression converts the item's EP value to it's purchase value at this store
	// If left empty, it will default to the expression supplied in GlobalExpressions.def
	Expression *pExprEPConversion;	AST(LATEBIND)

	// This store will not be available if this expression is not met - If this is blank the store is always available
	Expression *pExprRequires;				AST(NAME(ExprBlockRequires,ExprRequiresBlock), REDUNDANT_STRUCT(ExprRequires, parse_Expression_StructParam), LATEBIND)

	StoreContents eContents;

	// Region this store is affiliated with (used to drive UI).
	WorldRegionType eRegion;		AST(DEF(WRT_None))

	// Only used in persisted stores to generate the reward tables
	S32 iItemLevel;					AST(NAME(ItemLevel))

    // What type of group project provides provisioning for this store.  Only used if bProvisionFromGroupProject is set.
    GroupProjectType eGroupProjectType;

    // Which group project to use for provisioning this store.
    REF_TO(GroupProjectDef) provisioningProjectDef;

    // Which group project numeric to use for provisioning this store.
    REF_TO(GroupProjectNumericDef) provisioningNumericDef;

	U32 bSellEnabled : 1;
	U32 bIsPersisted : 1;
	U32 bIsResearchStore : 1;
	U32 bDisplayStoreCPoints : 1;

    // Turning on this option enables provisioning of this store from a group project numeric.
    // This means that every time someone buys an item from the store, the numeric is decremented, and when it reaches zero, nothing can be
    //  purchased from the store until it is refilled, which is generally done by running provisioning tasks in the group project system.
    // The group project container determined by the player entity and eGroupProjectType is the one use for provisioning.
    U32 bProvisionFromGroupProject : 1;

    // If this flag is set players will only be able to purchase from the store if it is on a guild owned map, and they are a member of the owning guild.
    U32 bGuildMapOwnerMembersOnly : 1;

	// TODO
	// Faction - Faction that this store is affiliated with
	// Rank Requirement - Rank that a character must achieve to purchase from this store
} StoreDef;


AUTO_STRUCT;
typedef struct StoreItemCostInfo {
	REF_TO(ItemDef) hItemDef;
	const char *pPriceAccountKeyValue; AST(POOL_STRING)
	int iCount;
	int iOriginalCount; // For discounted items
	int iAvailableCount; // The amount the player has available
	const char** ppchDiscounts; AST(POOL_STRING) // Discounts applied
	bool bTooExpensive;
} StoreItemCostInfo;

AUTO_STRUCT;
typedef struct StoreItemCostInfoList {
	EARRAY_OF(StoreItemCostInfo) eaCostInfo;
	int iStoreItemCount;
} StoreItemCostInfoList;

AUTO_STRUCT;
typedef struct BuyBackItemInfo
{
	U64 iItemID;
	const char* pchItemName;		AST( POOL_STRING )
	S32 iCount;
	S32 iCost;
	const char* pchResourceName;	AST( POOL_STRING )

	U32 uBuyBackItemId;
} BuyBackItemInfo;

// The information the client needs to know about sellable items
AUTO_STRUCT;
typedef struct StoreSellableItemInfo
{
	U64 uItemID;
	S32 iBagID;
	S32 iSlot;
	S32 iCost;
	S32 iCount;
} StoreSellableItemInfo;

// This is used to display a Store Item in the Store UI
AUTO_STRUCT;
typedef struct StoreItemInfo
{
	// I need two pointers for this because sometimes it's Owned and sometimes it's not
	Item *pItem;					AST( UNOWNED CLIENT_ONLY)
	Item *pOwnedItem;
	int iCount;

	StoreCanBuyError eCanBuyError;	AST( NAME(CanBuyError,FailsRequirements) )
	char *pchRequirementsText;		AST( ESTRING )
	char *pchRequirementsMessage;	AST( ESTRING )
	REF_TO(MicroTransactionDef) hRequiredMicroTransaction;	AST( NAME(RequiredMicroTransaction) )
	U32 uRequiredMicroTransactionID;		AST( NAME(RequiredMicroTransactionID) CLIENT_ONLY )

	// When selling items, we use a bag and a slot
	int iBagID;
	int iSlot;

	// When buying items, we use a Store Name and an Index
	const char *pchStoreName;		AST( POOL_STRING )
	int index;

	// How much the item costs (or how much it will sell for)
	StoreItemCostInfo** eaCostInfo;

	int storeItemPetID; //Owner pet containerID if a pet holds it
	
	// If the price is based on value recipe(s), the price doesn't need to be recalculated
	bool bIsValueRecipe;			AST( SERVER_ONLY )
	bool bIsFromPersistedStore;
	bool bUsesAccountKey;
	int iAccountKeyCount;

	S32 iMayBuyInBulk;

	// If this is a header entry then the only other field that is valid is eStoreCategory
	bool bIsHeader;

	// What is the store category of this item or header
	StoreCategory eStoreCategory;

	// The UI highlight categories of this item
	StoreHighlightCategory *peHighlightCategory; AST(NAME(HighlightCategory) SUBTABLE(StoreHighlightCategoryEnum))

	// The index of the item in the store array.  Used when sorting for the UI.
	int iStoreItemIndex;

	// Name of the required numeric.  See comments on StoreItemDef for more info.
	const char *pchRequiredNumericName;	AST( POOL_STRING )

	// Value of the required numeric that the player must have to buy the item.  See comments on StoreItemDef for more info.
	S32 iRequiredNumericValue;

	// Value that the required numeric will be incremented by when this item is purchased.  See comments on StoreItemDef for more info.
	S32 iRequiredNumericIncr;

	// The amount of time it takes to purchase the item (in seconds)
	U32 uResearchTime; AST(NAME(ResearchTime, PurchaseTime))

	// If this is a persisted store item, this is the server time that the item will be removed from the store
	// If it is not a persisted store item and we are associated with an activity, this is the server time that the item will be removed from the store
	U32 uExpireTime;	AST( NAME(ExpireTime) )

	// If the item is going to be forced to bind on purchase, this info needs to be available for the UI
	bool bForceBind;

	// The micro transaction that may be used to purchase the item
	REF_TO(MicroTransactionDef) hMicroTransaction;	AST( NAME(MicroTransaction) )

	// The product ID that may be used to purchase this item as a micro transaction
	U32 uMicroTransactionID;			AST( NAME(MicroTransactionID) CLIENT_ONLY )

	// Links this store item info to the correct player owned item and contains information on the price of the item when it was
	// sold. Filled only if this is an item available to buyback.
	BuyBackItemInfo* pBuyBackInfo;

	// Fancy display info
	const char* pchTranslatedLongDescription; AST(NAME(LongDesc))
	const char* pchDisplayTex;		AST(NAME(DisplayTex) POOL_STRING)

} StoreItemInfo;

// Item information saved for Persisted Stores
AUTO_STRUCT AST_CONTAINER AST_IGNORE(hStoreItemDef) AST_IGNORE(pchStoreItemName);
typedef struct PersistedStoreItem
{
	CONST_STRING_MODIFIABLE pchRestockDef;		AST(PERSIST SUBSCRIBE POOL_STRING)
	const U32 uID;								AST(PERSIST SUBSCRIBE)
	const U32 uSeed;							AST(PERSIST SUBSCRIBE)
	const U32 uExpireTime;						AST(PERSIST SUBSCRIBE)
	const S32 iRewardIndex;						AST(PERSIST SUBSCRIBE)
} PersistedStoreItem;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(eCategory);
typedef struct PersistedStoreRestockData
{
	CONST_STRING_MODIFIABLE pchRestockDef;		AST(PERSIST SUBSCRIBE POOL_STRING)
	const U32 uNextReplenishTime;				AST(PERSIST SUBSCRIBE)
	StoreRestockDef* pRestockDef;				NO_AST
} PersistedStoreRestockData;

// Information saved for Persisted Stores
AUTO_STRUCT AST_CONTAINER AST_IGNORE(eaCategories);
typedef struct PersistedStore
{
	const ContainerID uContainerID;								AST(PERSIST SUBSCRIBE KEY)
	CONST_REF_TO(StoreDef) hStoreDef;							AST(PERSIST SUBSCRIBE)
	const U32 uNextUpdateTime;									AST(PERSIST SUBSCRIBE)
	const U32 uVersion;											AST(PERSIST SUBSCRIBE)
	const U32 uMaxID;											AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(PersistedStoreItem) eaInventory;			AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(PersistedStoreRestockData) eaRestockData;	AST(PERSIST SUBSCRIBE)
	bool bDirty;												NO_AST
} PersistedStore;

AUTO_STRUCT;
typedef struct StoreBuyExtraArgs
{
    int bProvisioned;
	REF_TO(StoreDef) hStoreDef;
	U32 uStoreItemIndex;
    STRING_POOLED provisioningProjectName; AST(POOL_STRING)
    STRING_POOLED provisioningNumericName; AST(POOL_STRING)
} StoreBuyExtraArgs;

extern DictionaryHandle g_StoreDictionary;

StoreDef* store_DefFromName(const char* storeName);
bool store_GetSellableItemList(Entity* pEnt, StoreDef* pStore, Item*** peaItems);
bool store_Validate(StoreDef *pDef);
void store_Load(void);
void StoreCategories_Load(void);
void StoreHighlightCategories_Load(void);
ExprFuncTable* store_CreateExprFuncTable(void);

void store_InitContext(void);
void store_BuyContextSetup(Entity *pPlayerEnt, ItemDef *pItemDef);
void store_BuyContextGenerateSetup(void);
void store_ShowItemContextSetup(Entity *pEntity);
void store_ShowItemContextGenerateSetup(void);
ExprContext *store_GetBuyContext(void);
ExprContext *store_GetShowItemContext(void);
bool store_ValidateResearchStore(StoreDef *pStore);
bool store_ValidatePuppetStore(StoreDef *pStore);
const char *store_GetItemNameContextPtr(void);

StoreRestockDef* PersistedStore_FindRestockDefByName(StoreDef* pDef, const char* pchName);
S32 PersistedStore_FindItemByID(PersistedStore* pPersistStore, U32 uID);

StoreDiscountDef* store_FindDiscountDefByName(StoreDef* pDef, const char* pchName);
