#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef struct AuctionDurationOption AuctionDurationOption;
typedef struct NOCONST(AuctionLot) NOCONST(AuctionLot);
typedef struct AuctionBrokerLevelInfo AuctionBrokerLevelInfo;

#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "inventoryCommon.h"

#include "AuctionLotEnums.h"
#include "CharacterAttribsMinimal.h"
#include "CharacterAttribsMinimal_h_ast.h"

#define SECONDS_PER_AUCTION_REMOVE_TICK 5 // Seconds between ticks that remove old auctions
#define SECONDS_PER_AUCTION_EXPIRE_TICK 6 // Seconds between ticks that check for expired auctions
// setting CHECK_AUCTIONLOTS_PER_TICK to zero for now to disable expiration.  It will be turned on by hand
//  the first time we deploy, so that we can make sure it doesn't blow up.  I will set it back to a reasonable
//  value once we are happy that everything is working correctly.
#define CHECK_AUCTIONLOTS_PER_TICK 60 // Max number of auctions to check for expiration in one tick
#define AUCTIONLOT_EXPIRE_DAYS 7	// How many days to expire a lot (default)
#define MAX_AUCTION_RETURN_RESULTS 400 // Max number of auctions to return to the client
#define DEFAULT_MAXIMUM_AUCTIONS_POSTS 50	// The default number of auctions a player can have posted. This is used if uDefaultAuctionPostsMaximum is zero

// The current version number of the auction lot
#define AUCTION_LOT_VERSION 1

AUTO_STRUCT;
typedef struct AuctionDurationOption
{
	// The duration in seconds
	U32 iDuration;				AST(KEY)

	// The display name
	DisplayMessage msgDisplayName;	AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))

	// The posting fee (% of asking price) associated with this duration option.
	// If set to 0, fAuctionDefaultPostingFee will be used
	F32 fAuctionPostingFee;

	// The sold fee (% of sale cost) associated with this duration option.
	// If set to 0, fAuctionDefaultSoldFee will be used
	F32 fAuctionSoldFee;

	// The default duration option in the UI.
	bool bUIDefaultDuration;
} AuctionDurationOption;

AUTO_STRUCT;
typedef struct AuctionConfig
{
	// The default time for an auction lot to expire in
	U32 uDefaultExpireTimeDays;				AST(NAME(DefaultExpireTimeDays))

	// If your auctions allow custom durations you need to define this also
	AuctionDurationOption **eaDurationOptions;	AST(NAME(DurationOption))
	
	// default posting fee
	F32 fAuctionDefaultPostingFee;		

	// minimum posting fee
	F32 fAuctionMinimumPostingFee;		

	// default sold fee (% of sale cost)
	F32 fAuctionDefaultSoldFee;		
	
	// the default number of auction posts one character can make
	U32 uDefaultAuctionPostsMaximum;		AST(NAME(DefaultAuctionPostsMaximum))

	// If bidding is enabled, this value defines the amount of seconds added to the expiration time for each bid
	U32 iBidExpirationExtensionInSeconds;

	// This defines the bid incremental value for an auction. Default is 1% of the current bid.
	F32 fBiddingIncrementalMultiplier;	AST(DEFAULT(0.01f))

	// do auctions require a posting fee?
	bool bAuctionsUsePostingFee;

	// Indicates whether players can define custom auction durations
	bool bAllowCustomAuctionDurations;

	// do auctions require a selling fee (% of sale cost)?
	bool bAuctionsUseSoldFee;

	// Indicates whether bidding is enabled
	bool bBiddingEnabled;

	// Indicates whether the auction is mailed to the player in a buyout/purchase scenario.
	// The default behavior is to add the items to the inventory automatically.
	bool bPurchasedItemsAreMailed;

	// Indicates whether the owner's character name and handle should be persisted with the auction for easy access
	bool bPersistOwnerNameAndHandleWithAuction;

	// Indicates whether the logical item name is persisted within the auction data
	bool bPersistItemNameWithAuction;

	// Indicates whether the search should hide auctions posted by the player doing the search
	bool bHideOwnAuctionsInSearch;

	// Indicates whether the proceeds from an auction are deposited directly into the character's inventory or included with the "Auction Sold" email.
	bool bIncludeCurrencyInSoldEmail;

	// Whether the "maximum number of auctions posted" includes expired item mail siting in your inbox 
	bool bExpiredAuctionsCountTowardMaximum;
	
	// Whether or not a sort category MUST be specified for a search to be performed.
	bool bRequiredCategoryForSearch;

	// Whether we should keep a record of the last successful sale prices for each item, so we can inform the UI.
	bool bEnablePriceHistoryTracking;

	// Additional multiplier on all applicable fees if the player is not standing at an auction house when posting.
	F32 fPlayerRoamingFeeMultiplier;	AST(DEF(1.0))

	// maximum price that a lot can cost (numerics), note that the atr that does the final auction lot placement will prevent any value > 2^31
	U32 uMaximumAuctionPrice;

	// Alternatively you can define a currency numeric other than "Resources" here.
	const char *pchCurrencyNumeric;		AST(POOL_STRING)

	INT_EARRAY eaiSearchableAttribs;		AST(NAME(SearchableAttribs), SUBTABLE(AttribTypeEnum))
	
}AuctionConfig;

AUTO_STRUCT AST_CONTAINER;
typedef struct AuctionLotSearchableStat{
	const AttribType eType; AST(PERSIST KEY)
		const F32 fMag;			AST(PERSIST)
} AuctionLotSearchableStat;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(ppTranslatedTokens);
typedef struct AuctionSlot
{
	InventorySlot slot;									AST(EMBEDDED_FLAT)
	const U32 iItemSortType;							AST(PERSIST) // Corresponds to an ItemSortType, used by auction
	CONST_STRING_POOLED pchItemSortTypeCategoryName;	AST(PERSIST POOL_STRING)
	const UsageRestrictionCategory eUICategory;			AST(PERSIST SUBTABLE(UsageRestrictionCategoryEnum) DEFAULT(UsageRestrictionCategory_None))
	CONST_STRING_EARRAY ppTranslatedNames;				AST(PERSIST POOL_STRING) // translated name of item, in all languages, used by auction

	// The item name is persisted so that the auction broker can search items by their logical names
	CONST_STRING_POOLED pchItemName;					AST(PERSIST POOL_STRING)

	//Fields to allow auctionlot indexing with more complex properties of items.
	const U32 uiNumGemSlots;							AST(PERSIST)
	const U32 uiCachedMinLevel;							AST(PERSIST)

	CONST_EARRAY_OF(AuctionLotSearchableStat) ppSearchableAttribMagnitudes;	AST(PERSIST)
}AuctionSlot;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(ppTranslatedTokens);
typedef struct AuctionSlotV1
{
	InventorySlotV1 slot;					AST(EMBEDDED_FLAT)
	const U32 iItemSortType;			AST(PERSIST) // Corresponds to an ItemSortType, used by auction
	CONST_STRING_POOLED pchItemSortTypeCategoryName;	AST(PERSIST POOL_STRING)
	const UsageRestrictionCategory eUICategory;	AST(PERSIST SUBTABLE(UsageRestrictionCategoryEnum) DEFAULT(UsageRestrictionCategory_None))
	CONST_STRING_EARRAY ppTranslatedNames; AST(PERSIST POOL_STRING) // translated name of item, in all languages, used by auction
}AuctionSlotV1;


AUTO_STRUCT AST_CONTAINER;
typedef struct AuctionBiddingInfo
{
	// The account ID of the auction owner
	// We persisted this so we can check if the person bidding is actually the same account owner as the auction owner.
	// We don't have access to the owner in bidding transaction in order to not lock a 3rd entity.
	const ContainerID iOwnerAccountID;				AST(PERSIST)

	// The minimum bid amount
	const U32 iMinimumBid;							AST(PERSIST)

	// The number of bids (Also used to calculate the amount of time needs to be added to the expiration time to prevent sniping)
	const U32 iNumBids;								AST(PERSIST)

	// The current bid amount
	const U32 iCurrentBid;							AST(PERSIST)	

	// The player ID for the current bid
	const ContainerID iBiddingPlayerContainerID;	AST(PERSIST)

	// The player Account ID for the current bid
	const ContainerID iBiddingPlayerAccountID;		AST(PERSIST)

	// The current bidder's languageID
	const S32 iBiddingPlayerLangID;					AST(PERSIST)

	// The number of times the auction server failed to close this auction
	U32 iNumFailedCloseOperations;					AST(SERVER_ONLY)
} AuctionBiddingInfo;

AUTO_STRUCT AST_CONTAINER;
typedef struct AuctionLotOptionalData
{
	// Owner's character name
	const char pchOwnerName[MAX_NAME_LEN];		AST(PERSIST)

	// Owner's account handle
	const char pchOwnerHandle[MAX_NAME_LEN];	AST(PERSIST)

} AuctionLotOptionalData;

AUTO_STRUCT AST_CONTAINER;
typedef struct AuctionLot {
	const ContainerID iContainerID;			AST(PERSIST, KEY)

	// Owner ID; the person who posted the lot. This is an entity ID.
	const ContainerID ownerID;				AST(PERSIST)
	const ContainerID OwnerAccountID;				AST(PERSIST)

	const S32 iLangID;						AST(PERSIST)
	
	// If a mail auction, the account this was intended for. This is an account ID.
	const ContainerID recipientID;			AST(PERSIST)
	
	CONST_STRING_MODIFIABLE title;			AST(PERSIST)
	CONST_STRING_MODIFIABLE description;	AST(PERSIST)
	
	const U32 price;						AST(PERSIST) // If bidding is enabled this is the buyout price

	CONST_OPTIONAL_STRUCT(AuctionLotOptionalData) pOptionalData;	AST(PERSIST)

	CONST_OPTIONAL_STRUCT(AuctionBiddingInfo) pBiddingInfo;			AST(PERSIST)
	
	const AuctionLotState state;			AST(PERSIST)
	
	const U32 creationTime;					AST(PERSIST FORMATSTRING(JSON_SECS_TO_RFC822=1))
	const U32 modifiedTime;					AST(PERSIST FORMATSTRING(JSON_SECS_TO_RFC822=1))

	const U32 uExpireTime;					AST(PERSIST FORMATSTRING(JSON_SECS_TO_RFC822=1))

	const U32 uPostingFee;					AST(PERSIST)
	const U32 uSoldFee;						AST(PERSIST)

	CONST_EARRAY_OF(AuctionSlot) ppItemsV2;	AST(PERSIST NO_INDEX)
	CONST_EARRAY_OF(AuctionSlotV1) ppItemsV1_Deprecated;	AST(PERSIST NO_INDEX ADDNAMES(ppItems))
	INT_EARRAY auctionPetContainerIDs;		AST(PERSIST)

	const U32 uVersion;						AST(PERSIST)
	
	AST_COMMAND("Apply Transaction","ServerMonTransactionOnEntity AuctionLot $FIELD(iContainerID) $STRING(Transaction String)$CONFIRM(Really apply this transaction?)")
} AuctionLot;

AUTO_STRUCT;
typedef struct AuctionLotList
{
	EARRAY_OF(AuctionLot) ppAuctionLots; AST(NO_INDEX)
	UINT_EARRAY piAuctionLotsCounts;
	AuctionSearchResult result;
	int hitcount;				AST(NAME(asrhc))	// The number of items looked at. This value is somewhat meaningless.
	int realHitcount;								// The number of items that actually match the search
	int maxResults;
	F32 timeTaken;
	int cooldownEnd;
	ContainerID * eaiNonExistingAuctionLotContainerIDs; // This is used to indicate the non existing auction lot IDs which were specified in the search request
} AuctionLotList;

AUTO_STRUCT;
typedef struct AuctionSearchRequest
{
	int minLevel; // Must be greater or equal to this level, if >= 0
	int maxLevel; // Must be less than or equal to this level, if >= 0
	int itemQuality; // If >= 0, quality must match
	UsageRestrictionCategory minUsageRestrictionCategory; // usage restriction must be >= this value to match
	UsageRestrictionCategory maxUsageRestrictionCategory; // usage restriction must be <= this value to match, if 0 skip usage restriction check
	ContainerID ownerID; // ID of player who owns the auction lots, if > 0
	bool bExcludeOwner;
	int sortType; // The user-visible type of an item
	const char *pchItemSortTypeCategoryName;	AST(POOL_STRING) // Sort type category that the sort type belongs to.
	ContainerID * eaiAuctionLotContainerIDs;	// The container IDs of the auction lots we're interested in
	AuctionSortColumn eSortColumn; // How to sort the search results
	
	char *stringSearch; // String defining specific name to look for
	int stringLanguage; // Locale of entity requesting

	const char* pchRequiredClass; AST(POOL_STRING)
	U32 uiPetMinLevel;
	U32 uiPetMaxLevel;
	U32 uiNumGemSlots;
	ItemGemType eRequiredGemType; 

	INT_EARRAY eaiAttribs;

	int numResults; // Maximum number of results to return	
	const AuctionBrokerLevelInfo *pAuctionBrokerSearchData;
} AuctionSearchRequest;

AUTO_STRUCT;
typedef struct AuctionBlockedItem
{
	const char *pcItemName;				AST(KEY POOL_STRING)

}AuctionBlockedItem;

AUTO_STRUCT;
typedef struct AuctionBlockedItems
{
	EARRAY_OF(AuctionBlockedItem) eaBlockedItems;

}AuctionBlockedItems;

AUTO_STRUCT;
typedef struct AuctionBrokerLot {
	REF_TO(ItemDef) hItemDef;		AST(REFDICT(ItemDef))
	Item* pUnownedItem; AST(NAME(Item) UNOWNED)
	Item* pOwnedItem;
	AuctionLot *pCheapestLot;		AST(NAME(CheapestLot))
	U32 uiCheapestPrice;			AST(NAME(CheapestPrice))
	U32 uiLotCountAtAnyPrice;		AST(NAME(LotCountAtAnyPrice))
	const char* pWhereFrom;			AST(NAME(WhereFrom))
}AuctionBrokerLot;

AUTO_STRUCT;
typedef struct AuctionBrokerLotList {
	AuctionBrokerLot **ppAuctionBrokerLots; AST(NO_INDEX)
}AuctionBrokerLotList;

void gslAuction_FindLostLots(SA_PARAM_NN_VALID Entity* pEnt);
void AuctionConfig_Load(void);
U32 Auction_GetExpireTime(Entity *pEntity);
U32 Auction_GetPostingFee(Entity *pEntity, U32 iBuyoutPrice, U32 iStartingBid, U32 iAuctionDuration, bool bPlayerIsAtAuctionHouse);
U32 Auction_GetSalesFee(Entity *pEnt, U32 iBuyoutPrice, U32 iStartingBid, U32 iAuctionDuration, bool bPlayerIsAtAuctionHouse);
U32 Auction_GetSoldFee(Entity *pEntity, U32 uAskingPrice, U32 uPostingFee, bool bPlayerIsAtAuctionHouse);
U32 Auction_GetMaximumPostedAuctions(Entity *pEntity);
U32 Auction_GetMaximumDuration(Entity *pEntity);
AuctionDurationOption * Auction_GetDurationOption(U32 iAuctionDuration);
void AuctionLotInit(NOCONST(AuctionLot) *pAuctionLot);
U32 Auction_MaximumSellPrice(void);

// Returns the numeric used for the auction house currency
const char * Auction_GetCurrencyNumeric(void);

extern AuctionConfig gAuctionConfig;


