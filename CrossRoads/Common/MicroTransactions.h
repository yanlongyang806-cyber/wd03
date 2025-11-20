/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#ifndef MICROTRANSACTIONS_H
#define MICROTRANSACTIONS_H
GCC_SYSTEM

#include "message.h"
#include "stdtypes.h"

typedef struct AccountProxyKeyValueInfo AccountProxyKeyValueInfo;
typedef struct AccountProxyKeyValueInfoList AccountProxyKeyValueInfoList;
typedef struct AllegianceDef AllegianceDef;
typedef struct AttribValuePair AttribValuePair;
typedef struct AccountProxyProduct AccountProxyProduct;
typedef struct AccountProxyProductList AccountProxyProductList;
typedef struct CachedPaymentMethod CachedPaymentMethod;
typedef struct DisplayMessage DisplayMessage;
typedef struct Entity Entity;
typedef struct Expression Expression;
typedef struct GameAccountData GameAccountData;
typedef struct InventoryBag InventoryBag;
typedef struct ItemDef ItemDef;
typedef struct MicroTransactionDef MicroTransactionDef;
typedef struct PlayerCostume PlayerCostume;
typedef struct PowerDef PowerDef;
typedef struct RewardTable RewardTable;
typedef struct SpeciesDef SpeciesDef;
typedef struct GameTokenText GameTokenText;
typedef struct ExprContext ExprContext;
typedef struct Money Money;
typedef struct Item Item;

#define MICROTRANSACTIONS_BASE_DIR "defs/microtransactions"
#define MICROTRANSACTIONS_EXTENSION "microtrans"

// Each percent on account server is the following
#define MT_DISCOUNT_PER_PERCENT 100

AUTO_ENUM;
typedef enum MicroPurchaseErrorType
{
	kMicroPurchaseErrorType_None,
	kMicroPurchaseErrorType_Unknown,
	kMicroPurchaseErrorType_InvalidTransaction,
	kMicroPurchaseErrorType_Unique,
	kMicroPurchaseErrorType_UsageRestrictions,
	kMicroPurchaseErrorType_Allegiance,
	kMicroPurchaseErrorType_InvalidPet,
	kMicroPurchaseErrorType_MaxPets,
	kMicroPurchaseErrorType_MaxPuppets,
	kMicroPurchaseErrorType_CannotPurchaseAgain,
	kMicroPurchaseErrorType_InventoryBagFull,
	kMicroPurchaseErrorType_RequiredPurchase,
	kMicroPurchaseErrorType_FailsExpressionRequirement,
	kMicroPurchaseErrorType_NotEnoughCurrency,
	kMicroPurchaseErrorType_AlreadyEntitled,
	kMicroPurchaseErrorType_PetAcquireLimit,
	kMicroPurchaseErrorType_NumericFull,
	kMicroPurchaseErrorType_AttribClamped,
	kMicroPurchaseErrorType_ItemDoesNotExist,
} MicroPurchaseErrorType;

AUTO_ENUM;
typedef enum MicroPartType
{
	kMicroPart_Attrib,
	kMicroPart_Item,
	kMicroPart_Costume,
	kMicroPart_CostumeRef,
	kMicroPart_VanityPet,
	kMicroPart_Species,
	kMicroPart_Special,
	kMicroPart_Permission,
	kMicroPart_RewardTable,
} MicroPartType;

AUTO_ENUM;
typedef enum SpecialPartType
{
	kSpecialPartType_None,
	kSpecialPartType_Respec,
	kSpecialPartType_Rename,
	kSpecialPartType_CharSlots,
	kSpecialPartType_CostumeSlots,
	kSpecialPartType_OfficerSlots,
	kSpecialPartType_BankSize,
	kSpecialPartType_SharedBankSize,
	kSpecialPartType_CostumeChange,
	kSpecialPartType_ShipCostumeChange,
	kSpecialPartType_InventorySize,
	kSpecialPartType_ExtraAuctionSlots,
	kSpecialPartType_Retrain,
	kSpecialPartType_ItemAssignmentCompleteNow,
	kSpecialPartType_ItemAssignmentUnslotItem,
    kSpecialPartType_ItemAssignmentReserveSlots,
	kSpecialPartType_SuperPremium,
} SpecialPartType;

AUTO_ENUM;
typedef enum AVChangeType
{
	kAVChangeType_None,
	kAVChangeType_Boolean,
		// Sets a 'boolean' value on the game account data keys.  Fails if the value is already set (or already NOT set)
	kAVChangeType_IntSet,
	kAVChangeType_IntIncrement,
	kAVChangeType_String,

		// Special change types that don't fail if the value is already set, but activate it otherwise
	kAVChangeType_BooleanNoFail,
	kAVChangeType_IntSetNoFail,
} AVChangeType;

AUTO_ENUM;
typedef enum MTCategoryType
{
	kMTCategory_Normal,
		//Just a category
	kMTCategory_Main,
		// The main category, can only have 1
	kMTCategory_Featured,
		// The featured category
	kMTCategory_New,
		// The new category.
	kMTCategory_Bonus,
		// The bonus category.  Can only have 1 top-level category.
	kMTCategory_Hidden,
		// A hidden category.  Has the same semantics as Normal, except not normally displayed in the UI.
} MTCategoryType;

AUTO_ENUM;
typedef enum ProductCategory
{
	kProductCategory_ActionFigure,			// AF = Action Figure
	kProductCategory_AdventurePack,			// AP = Adventure Pack
	kProductCategory_Archetype,				// AT = Archetype
	kProductCategory_BridgePack,			// BRG = Bridge Pack
	kProductCategory_CostumePack,			// CP = Costume Pack
	kProductCategory_EmotePack,				// EM = Emote Pack
	kProductCategory_EmblemPack,			// EP = Emblem Pack
	kProductCategory_FunctionalItem,		// FI = Functional Item
	kProductCategory_Hideout,				// HO = Champions Hideout
	kProductCategory_Item,					// IT = Item
	kProductCategory_Power,					// PO = Power
	kProductCategory_Promo,					// PR = promo
	kProductCategory_PlayableSpecies,		// PS = Playable Species
	kProductCategory_Pet,					// PT = Pet
	kProductCategory_Ship,					// S = Ship
	kProductCategory_ShipCostume,			// SC = Ship Costume
	kProductCategory_Service,				// SV = Service
	kProductCategory_Title,					// TI = Title
	kProductCategory_Token,					// TK = Token

	// New for Neverwinter
	kProductCategory_Healing,				// HEA
	kProductCategory_Buff,					// BUF
	kProductCategory_ResurrectionScroll,	// RES
	kProductCategory_Identification,		// IDN
	kProductCategory_Booster,				// BST
	kProductCategory_Skillcraft,			// SKC
	kProductCategory_Skin,					// SKN
	kProductCategory_Inscription,			// INS
	kProductCategory_CompanionBuff,			// CBF
	kProductCategory_Mount,					// MNT
	kProductCategory_CraftingTier1,			// CT1
	kProductCategory_CraftingTier2,			// CT2
	kProductCategory_CraftingTier3,			// CT3
	kProductCategory_Enchanting,			// ENC
	kProductCategory_Bag,					// BAG
	kProductCategory_Dye,					// DYE
} ProductCategory;

AUTO_ENUM;
typedef enum MicroTrans_ShardCategory
{
	// NEVER EVER CHANGE THE ORDER OF THESE VALUES
	// ALSO NEVER ADD VALUES TO THE MIDDLE OF THIS ENUM
	// THIS ENUM PROTECTS US FROM HAVING REAL MONEY SPENT ON SHARDS OTHER THAN LIVE
	kMTShardCategory_Off = 0,
	kMTShardCategory_Dev = 1,
	kMTShardCategory_Beta = 2,
	kMTShardCategory_PTS = 3,
	kMTShardCategory_Live = 4
} MicroTrans_ShardCategory;

AUTO_STRUCT;
typedef struct MicroTransactionCategory
{
	STRING_POOLED		pchName;			AST(KEY POOL_STRING STRUCTPARAM)
		// The name of the category

	STRING_POOLED pchParentCategory;		AST(POOL_STRING)
		// This is a sub category of this category

	const char *pchFile;					AST(CURRENTFILE)
		// The current file.  Used while reloading

	MTCategoryType		eType;
		//Normal, Main, New, Featured etc

	REF_TO(AllegianceDef)	hAllegiance;	AST(NAME(Allegiance) REFDICT(Allegiance))
		//For STO, is this category restricted to a specific allegiance

	REF_TO(AllegianceDef)	hDisplayAllegiance;	AST(NAME(DisplayAllegiance) REFDICT(Allegiance))
		//For STO, items in this category will be displayed with this allegiance (but not be restricted to)

	DisplayMessage		displayNameMesg;	AST(STRUCT(parse_DisplayMessage))
		// The display name of the category

	S32 iSortIndex;							AST( ADDNAMES(SortValue) )
		//The sort index determines where this category gets sorted in the UI.  Featured ignores the sort index

	U32					bHideUnusable : 1;
		// Hide MTs with requirements the user does not meet

} MicroTransactionCategory;

AUTO_STRUCT;
typedef struct MicroTransactionCategories
{
	MicroTransactionCategory **ppCategories;	AST(NAME(Category))
} MicroTransactionCategories;

AUTO_STRUCT;
typedef struct AttribValuePairChange
{
	STRING_MODIFIABLE pchAttribute;			AST(KEY)
		// the name/key of the attribute

	AVChangeType eType;
		//IntSet, IntIncrement, String (set only)

	int iMinVal;
		// The minimum clamp
	int iMaxVal;
		// The maximum clamp
	int iVal;
		//The integer value

	U32 bClampValues : 1;
		// If true, iMinVal and iMaxVal would be in effect

	STRING_MODIFIABLE pchStringVal;
		// The string value to set
} AttribValuePairChange;

// Bags generated from a specific MicroTransactionPart of type RewardTable
AUTO_STRUCT;
typedef struct MicroTransactionPartRewards
{
	InventoryBag** eaBags; AST(NO_INDEX)
	S32 iPartIndex;
} MicroTransactionPartRewards;

// Rewards generated from MicroTransactionParts of type RewardTable
AUTO_STRUCT;
typedef struct MicroTransactionRewards
{
	MicroTransactionPartRewards** eaRewards;
} MicroTransactionRewards;

AUTO_STRUCT;
typedef struct MicroTransactionPart
{
	MicroPartType ePartType;
		//What type of Part this is

	SpecialPartType eSpecialPartType;
		// Code defined special types

	REF_TO(ItemDef) hItemDef;				AST(NAME(ItemDef) REFDICT(ItemDef))
		// A reference to an item def.  Valid in Part_Item and Part_Costume.

	REF_TO(PlayerCostume) hCostumeDef;		AST(NAME(Costume) REFDICT(PlayerCostume))
		// A reference to a player costume.  Valid in Part_CostumeRef

	REF_TO(PowerDef) hPowerDef;				AST(NAME(PowerDef) REFDICT(PowerDef))
		// A PowerDef reference.  Valid in Part_VanityPet

	REF_TO(SpeciesDef) hSpeciesDef;			AST(NAME(Species) REFDICT(SpeciesDef))
		// A SpeciesDef reference.  Valid in Part_Species

	REF_TO(RewardTable) hRewardTable;		AST(NAME(RewardTable) REFDICT(RewardTable))
		// A RewardTable reference. Valid in Part_RewardTable

	const char *pchPermission;				AST(POOL_STRING)				
		// A reference to a GamePermissionDef

	AttribValuePairChange	*pPairChange;	AST(NAME(Attrib))
		// an AttribValuePair Change struct.  Valid in Part_Attrib.

	int iCount;								AST(DEFAULT(1))
		// How many to purchase.  Defaults to 1

	char *pchIconPart;						AST( NAME(Icon) POOL_STRING )
		// The large icon for this specific part of the microtransaction
	
	S32 bAddToBestBag : 1;					AST(SERVER_ONLY)
		//Used for item transactions

	S32 bAllowOverflowBag : 1;				AST(SERVER_ONLY)
		// If the inventory is full, add to the overflow bag

	S32 bIgnoreUsageRestrictions : 1;		AST(SERVER_ONLY)
		// Allow the player to purchase this item, even if the requirements aren't met
} MicroTransactionPart;

AUTO_STRUCT;
typedef struct MicroTransactionAccountServerConfig
{
	char *pchName;							AST(SERVER_ONLY)
		// The name of the product on the Account Server

	U32 uiOverridePrice;					AST(SERVER_ONLY)
		// The price in Cryptic Points of the item

	char *pchKeyValueChanges;				AST(SERVER_ONLY)
		// The Account Server key-value changes granted by the item

	char *pchPrerequisites;					AST(SERVER_ONLY)
		// The Account Server prerequisites required for the item
} MicroTransactionAccountServerConfig;

AUTO_STRUCT;
typedef struct MicroTransactionDef
{
	char *pchName;							AST(KEY POOL_STRING)
		// The name of the micro transaction

	char *pchProductName;					AST(SERVER_ONLY)
		// PARTIALLY DEPRECATED: use the product name in pProductConfig where possible - VAS 091911
		// The name of the product on the Account Server

	ProductCategory eCategory;				AST(SERVER_ONLY)
		// The account server category this product is in

	char *pchProductIdentifier;				AST(SERVER_ONLY)
		// This is the short identifier for the Account Server product.  It'll likely be very similar to pchName.

	char *pchScope;							AST(SERVER_ONLY, POOL_STRING)
		// Internal category/scope of the micro transaction

	const char *pchFile;					AST(CURRENTFILE, SERVER_ONLY)
		// The current file.  Used while reloading

	REF_TO(MicroTransactionDef) hRequiredPurchase;	AST(NAME(RequiredPurchase))
		//The required purchase before this microtransaction can be purchased

	const char **ppchCategories;			AST(NAME(Categories) POOL_STRING RESOURCEDICT(MicroTransactionCategory))
		// The categories of this MicroTransaction is in.  These are in the MicroTransactionCategory dictionary

	MicroTrans_ShardCategory *eaShards;		AST(NAME(Shards) SERVER_ONLY)
		// The shards for which this MicroTransaction will be active.

	MicroTransactionAccountServerConfig *pProductConfig;		AST(SERVER_ONLY)
		// Account Server product configuration for the normal/buy version of this product

	MicroTransactionAccountServerConfig *pReclaimProductConfig;	AST(SERVER_ONLY)
		// Account Server product configuration for the reclaim version of this product

	DisplayMessage displayNameMesg;			AST(STRUCT(parse_DisplayMessage))
		// What to display as the name
	DisplayMessage descriptionShortMesg;	AST(STRUCT(parse_DisplayMessage))
		// The short description, used in the list UI to display as a tooltip
	DisplayMessage descriptionLongMesg;		AST(STRUCT(parse_DisplayMessage))
		// The long description, shown in the details for the item.  If not given, will display the short desc

	char *pchIconSmall;						AST( NAME(SmallIcon) POOL_STRING )
		// The small icon for the product
	char *pchIconLarge;						AST( NAME(LargeIcon) POOL_STRING )
		// The large icon for the product
	char *pchIconLargeSecond;				AST( NAME(LargeIconSecond) POOL_STRING )
		// The second large icon
	char *pchIconLargeThird;				AST( NAME(LargeIconThird) POOL_STRING )
		// The third large icon
		//TODO(BH): An earray of icons?

	char *pchLegacyItemID;					AST( NAME(LegacyName) )
		// If this MT was a legacy product, this is the item ID it used to have

	U32 uiPrice;
		//The price in cryptic points of the object
		
	Expression *pExprCanBuy;		AST(NAME(ExprCanBuyBlock), REDUNDANT_STRUCT(ExprCanBuy, parse_Expression_StructParam), LATEBIND)
		// This expression if present must be true in order to be able to purchase the item	

	U32 bBuyExprRequiresEntity : 1;			AST(NO_TEXT_SAVE)
		// This flag gets set when an entity is required to resolve the buy expression

	U32 bUseBuyExprForVisibility : 1;
		// This flag causes the buy expression to determine whether the item is even visible at all

	U32 bOnePerCharacter : 1;
		//This microtransaction can be purchased once per character

	U32 bOnePerAccount : 1;
		//This microtransaction can be purchased once per account

	U32 bAllegianceRestriction : 1;			AST(NO_TEXT_SAVE)
		//If this has an allegiance restriction on one of the categories.  Generated after text save

	U32 bDeprecated : 1;
		//This microtransaction is deprecated and can no longer be exported or purchased

	U32 bBuyProduct : 1;					AST(SERVER_ONLY)
		// DEPRECATED: you should use bGenerateReclaimProduct from now on - VAS 091911
		// This MT specially marked to be an STO Buy product.  If you don't know what that is, don't set this flag to true

	U32 bIsF2PDuplicate : 1;				AST(SERVER_ONLY)
		// DO NOT USE: only intended for one-time use when duplicating the products for STO F2P - VAS 092111

	U32 bGenerateReclaimProduct : 1;		AST(SERVER_ONLY)
		// This MT should generate a "reclaim" version - implicitly flags the product itself a "buy" product.

	U32 bBuyForAllShards : 1;				AST(SERVER_ONLY)
		// ONLY USE THIS IF YOU KNOW WHAT YOU'RE DOING - VAS 091911
		// This MT should generate a buy product that can be reclaimed on any shard

	U32 bPromoProduct : 1;					AST(SERVER_ONLY)
		// This is a promo product instead of a "normal" microtransaction

	U32 bOldProductName : 1;				AST(SERVER_ONLY)
		// This MT is an old product name, don't fix it up anymore
	
	MicroTransactionPart **eaParts;
		//What this micro transaction delivers

} MicroTransactionDef;

//The conglomeration of micro transaction products and their definitions
AUTO_STRUCT;
typedef struct MicroTransactionProduct
{
	U32 uID;							AST(KEY)
		//The ID of the product

	REF_TO(MicroTransactionDef) hDef;
		//The microtransaction defintion
	
	MicroTransactionDef *pDef; AST(UNOWNED)
		// Optional pointer to definition rather than a reference (used by WebRequestServer) - DOES NOT GET FREED

	AccountProxyProduct *pProduct;
		// The account server's product

	const char **ppchCategories;		AST(POOL_STRING)
		// The categories this MicroTransaction is in.  These are the categories that come directly from the account server.
		//  However, the account server is supposed to be fed the categories from the MicroTransactionDef so...

	U32 uAPPrerequisitesMetUpdateTime;	AST(CLIENT_ONLY)
		// Used on the client to invalidate the cached results of bAPPrerequisitesMet.

	bool bAPPrerequisitesMet;			AST(CLIENT_ONLY)
		// Used on the client to cache the results of gclAPPrerequisitesMet

	bool bFreeForPremiumMembers : 1;
		//This flag will be true if this microtransaction is free for premium members

} MicroTransactionProduct;

AUTO_STRUCT;
typedef struct MicroTransactionProductList
{
	MicroTransactionProduct **ppProducts;
} MicroTransactionProductList;

AUTO_STRUCT;
typedef struct MicroTransactionRef
{
	REF_TO(MicroTransactionDef) hMTDef;
} MicroTransactionRef;

AUTO_ENUM;
typedef enum CStoreActionType
{
	kCStoreAction_RequestProducts,
	kCStoreAction_PurchaseProduct,
	kCStoreAction_RequestMOTD,
	kCStoreAction_RequestPaymentMethods,
	kCStoreAction_RequestPointBuyProducts,
} CStoreActionType;

AUTO_ENUM;
typedef enum CStoreUpdateType
{
	kCStoreUpdate_ProductList,
	kCStoreUpdate_MOTD,
	kCStoreUpdate_SetKey,
	kCStoreUpdate_RemoveKey,
	kCStoreUpdate_SetKeyList,
	kCStoreUpdate_PaymentMethods,
	kCStoreUpdate_PointBuyProducts,
	kCStoreUpdate_SteamUpdate,
} CStoreUpdateType;

AUTO_STRUCT;
typedef struct CStoreAction
{
	CStoreActionType eType;
	U32 iProductID;
	char *pchCategory;
	char *pchPaymentID;
	Money *pExpectedPrice;  AST(LATEBIND)
	U64 iSteamID; // Used for Steam Wallet purchases
} CStoreAction;

AUTO_STRUCT;
typedef struct CStoreUpdate
{
	CStoreUpdateType eType;
		//The type of update
	const MicroTransactionProductList *pList;
		//The list of products
	AccountProxyProduct *pProduct;
		//Currently used for a single product update (MOTD, etc)
	
	const AccountProxyProductList *pProductList;
		//A list of products, currently used for point-buy products
	
	AccountProxyKeyValueInfo *pInfo;
		//The key/value in question
	AccountProxyKeyValueInfoList *pInfoList;
		// A list of key values

	CachedPaymentMethod **ppMethods;
		//earray of payment methods.  Used for buying point-buy products

	char *pchSteamCurrency;
		//The steam currency in the CStoreUpdate for Steam
} CStoreUpdate;


AUTO_STRUCT;
typedef struct SpecialKey
{
	char *pchKey;				AST(KEY STRUCTPARAM)
		//The special key

	DisplayMessage msgDisplay;	AST(STRUCT(parse_DisplayMessage))
		// Message to display in the UI
} SpecialKey;

AUTO_STRUCT;
typedef struct MicroTrans_MOTDConfig
{
	char *pchCategory;
	//The category of the message of the day on the account server.  Can be "<GameTitle>.CategoryName" or just "CategoryName" which
	// will be prefixed with "<GameTitle>." before requesting it from the account server.

	char *pchName;
	//The name of the MOTD product on the account server.

} MicroTrans_MOTDConfig;

AUTO_STRUCT;
typedef struct MicroTrans_ShardConfig
{
	MicroTrans_ShardCategory eShardCategory;		AST(KEY STRUCTPARAM REQUIRED)
		// The shard category

	char *pchCurrency;								AST(REQUIRED)
		// The currency used on this shard category

    char *pchExchangeWithdrawCurrency;
        // Which account server currency to send MTC to that is withdrawn from the currency exchange.

    char *pchCurrencyExchangeForSaleBucket;
        // The account server key/value used to store the For Sale escrow bucket for the Currency Exchange.

    char *pchCurrencyExchangeReadyToClaimBucket;
        // The account server key/value used to store the Ready to Claim escrow bucket for the Currency Exchange.

	char *pchFoundryTipBucket;
		// The account server key/value used to store Foundry tips to authors.

	char *pchPromoGameCurrencyBucket;
		// The account server key/value used to store promotional game currency (NOT virtual currency) that can be withdrawn.

	char *pchCategoryPrefix;						AST(REQUIRED NAME(Prefix, CategoryPrefix))
		// The product category prefix

	MicroTrans_MOTDConfig *pMOTD;
		// The message of the day config for this shard

	bool bPointBuyEnabled;
		//Is it enabled?  This allows us to have it configured but disabled (for qa/debugging)

	bool bSteamWalletEnabled : 1;
		// Is steam wallet enabled
} MicroTrans_ShardConfig;

AUTO_STRUCT;
typedef struct MicroTransConfig
{
	char *pchAutoImportFile;
		// TODO(BH): The path to the file to automatically import in dev-mode

	SpecialKey **eaSpecialKeys;				AST(NAME(SpecialKey))
		//The special keys the game is interested in.  If it finds one of these keys on the key-value pairs received from
		// the account server, it attempts to notify the user with the message specified.

	char *pchGlobalPointBuyCategory;
		// The category containing ALL point-buy products, regardless of type

	char *pchPointBuyCategory;
		// The category (sans prefix) of "regular" point-buy products (using standard payment methods) for this game

	char *pchPointBuySteamCategory;
		// The category (sans prefix) of Steam point-buy products for this game (different because of e.g. VAT-inclusive pricing)

	char *pchPromoGameCurrencyWithdrawNumeric;
		// The numeric into which withdrawn "promo game currency" should be deposited

	MicroTrans_ShardConfig **ppShardConfigs;		AST(NAME(ShardConfig))
		// The shard configurations (Live, PTS, etc)

	U32 uMaximumCouponDiscount;						AST(NAME(MaximumCouponDiscount))
		// The maximum discount an item can grant a Microtransaction

} MicroTransConfig;

// The following is used by the UI to create a list
AUTO_STRUCT;
typedef struct MicroUiCoupon
{
	// The Item ID of the coupon
	U64 uCouponItemID;

	// the index set for other functions to access item
	S32 iIndex;

	// translated name ... should only be accessed through expressions
	char *eaName;				AST(ESTRING)

} MicroUiCoupon;

extern DictionaryHandle g_hMicroTransDefDict;
extern MicroTransactionCategory *g_pMainMTCategory;
extern DictionaryHandle g_hMicroTransCategoryDict;

extern MicroTransConfig g_MicroTransConfig;
extern MicroTrans_ShardCategory g_eMicroTrans_ShardCategory;

extern int g_bPointBuyDebugging;

// This function is ONLY FOR THE GAME CLIENT
// The shard category is expected to be set on shard startup, and this function is only used by the client when informed of the category by the shard
void MicroTrans_SetShardCategory(MicroTrans_ShardCategory eCategory);
void MicroTrans_ConfigLoad(void);

MicroTransactionCategory *microtrans_CategoryFromStr(SA_PARAM_OP_STR const char *pchCategory);

void microtrans_GetGameTokenTextFromPermissionExpr(Expression *pExpr, GameTokenText ***peaTokensOut);
MicroTransactionDef *microtrans_findDefFromAPProd(SA_PARAM_OP_VALID const AccountProxyProduct *pProduct);
MicroTransactionDef *microtrans_FindDefFromPermissionExpr(SA_PARAM_NN_VALID Expression *pExpression);
void microtrans_FindAllMTDefsForPermissionExpr(Expression *pExpr, MicroTransactionDef ***peaMTDefs);
bool microtrans_PremiumSatisfiesPermissionExpr(SA_PARAM_NN_VALID Expression *pExpression);
bool microtrans_MTDefGrantsPermission(MicroTransactionDef *pMTDef, GameTokenText *pToken);

MicroPurchaseErrorType microtrans_GetCanPurchaseError(int iPartitionIdx, Entity *pEnt, const GameAccountData *pData, MicroTransactionDef *pDef, int iLangID, char** pestrError);
void microtrans_GetCanPurchaseErrorString(MicroPurchaseErrorType eError, MicroTransactionDef *pDef, int iLangID, char** pestrError);

bool microtrans_CanPurchaseProduct(int iPartitionIdx, Entity *pEnt, const GameAccountData *pData, MicroTransactionDef *pDef); 
bool microtrans_CannotPurchaseAgain(Entity *pEnt, const MicroTransactionProduct *pMTProduct);
bool mictrotrans_HasPurchasedEx(const GameAccountData *pData, const char *pchMictroTransName);
bool microtrans_HasPurchased(const GameAccountData *pData, MicroTransactionDef *pDef);
bool microtrans_PermissionCheck(Entity *pEnt, const GameAccountData *pData, MicroTransactionDef *pDef);
bool microtrans_IsPremiumEntitlement(MicroTransactionDef *pDef);
bool microtrans_GrantsUniqueItem(SA_PARAM_NN_VALID const MicroTransactionDef *pDef, SA_PARAM_OP_VALID const MicroTransactionRewards *pRewards);
bool microtrans_AlreadyPurchased(Entity *pEnt, const GameAccountData *pData, MicroTransactionDef *pDef);

void microtrans_GenerateProductName(MicroTransactionDef *pDef);
void microtrans_GenerateProductConfigs(MicroTransactionDef *pDef);
int microtransdef_Validate(MicroTransactionDef *pDef);

void microtranscategory_FillNameEArray(const char ***pppchNames);

// returns the main cateogry string for the shard
const char *microtrans_GetShardMainCategory(void);
// returns the category prefix used on this shard
const char *microtrans_GetShardCategoryPrefix(void);
// returns the MT shard category used on this shard
const char *microtrans_GetShardMTCategory(void);
// returns the environment scope name for this shard
const char *microtrans_GetShardEnvironmentName(void);
// returns the currency used on this shard
const char *microtrans_GetShardCurrency(void);
// returns the currency when withdrawing MTC from the currency exchange
const char *microtrans_GetExchangeWithdrawCurrency(void);
// returns the name of the For Sale key value pair used by the currency exchange
const char *microtrans_GetShardForSaleBucketKey(void);
// returns the name of the Ready to Claim value pair used by the currency exchange
const char *microtrans_GetShardReadyToClaimBucketKey(void);
// returns the name of the Promo Game Currency value pair, which can be claimed into a numeric
const char *microtrans_GetPromoGameCurrencyKey(void);
// returns the name of the Numeric into which Promo Game Currency can be claimed
const char *microtrans_GetPromoGameCurrencyWithdrawNumericName(void);
// returns the name of the Foundry Tip value pair used by the currency exchange
const char *microtrans_GetShardFoundryTipBucketKey(void);
// returns the currency prefixed with the "_" used by Account Server
const char *microtrans_GetShardCurrencyExactName(void);

// return categories related to shard point-buy
// global refers to ALL point-buy products, regardless of type or game
// shard refers to point-buy products for this shard using standard payment methods
// Steam refers to point-buy products for Steam (differentiated due to VAT-inclusive pricing)
SA_RET_OP_STR const char *microtrans_GetGlobalPointBuyCategory(void);
SA_RET_OP_STR const char *microtrans_GetShardPointBuyCategory(void);
SA_RET_OP_STR const char *microtrans_GetShardPointBuySteamCategory(void);

//Returns true if point buying is enabled on this shard.
bool microtrans_PointBuyingEnabled();
//Returns true if steam wallet buying is enabled on this shard.
bool microtrans_SteamWalletEnabled();

// get the context for CanBuy expression
ExprContext *microtrans_GetBuyContext(bool bNoEnt);

// setup the buy context
void microtrans_BuyContextSetup(int iPartitionIdx, Entity *pPlayerEnt, const GameAccountData *pData, bool bNoEnt);

// Get the discount that this coupon item will provide
U32 MicroTrans_GetItemDiscount(Entity *pEntity, Item *pItem);

// Is this coupon item valid for this product?
bool MicroTrans_ValidDiscountItem(Entity *pEntity, U32 uProductID, Item *pItem, MicroTransactionProduct ***ppProducts);

// Get the best coupon item id for this product
U64 MicroTrans_GetBestCouponItemID(Entity *pEntity, U32 uProductID, MicroTransactionProduct ***ppProducts, MicroUiCoupon ***ppCoupons);

S64 microtrans_GetPrice(AccountProxyProduct *pProduct);

S64 microtrans_GetFullPrice(AccountProxyProduct *pProduct);

#endif // MICROTRANSACTIONS_H