/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct NOCONST(AuctionLot) NOCONST(AuctionLot);
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct ItemChangeReason ItemChangeReason;
typedef U32 ContainerID;

AUTO_STRUCT;
typedef struct AuctionExpiredInfo
{
	char *esAuctionExpiredFromName;		AST(ESTRING)
	char *esAuctionExpiredBody;			AST(ESTRING)
	char *esAuctionExpiredSubject;		AST(ESTRING)

}AuctionExpiredInfo;

AUTO_STRUCT;
typedef struct PlayerAuctionBidListCleanupData
{
	ContainerID * eaiAuctionLotContainerIDsToRemove;
} PlayerAuctionBidListCleanupData;

// Returns the auction expiration time
U32 auction_trh_GetExpirationTime(ATR_ARGS, ATH_ARG NOCONST(AuctionLot) *pAuctionLot);
#define Auction_GetExpirationTime(pAuctionLot) auction_trh_GetExpirationTime(ATR_EMPTY_ARGS, CONTAINER_NOCONST(AuctionLot, (pAuctionLot)))

// Indicates whether the bids are still accepted for the given auction
bool auction_trh_AcceptsBids(ATR_ARGS, ATH_ARG NOCONST(AuctionLot) *pAuctionLot);
#define Auction_AcceptsBids(pAuctionLot) auction_trh_AcceptsBids(ATR_EMPTY_ARGS, CONTAINER_NOCONST(AuctionLot, (pAuctionLot)))

// Returns the minimum numeric value for the next bid
U32 auction_trh_GetMinimumNextBidValue(ATR_ARGS, ATH_ARG NOCONST(AuctionLot) *pAuctionLot);
#define Auction_GetMinimumNextBidValue(pAuctionLot) auction_trh_GetMinimumNextBidValue(ATR_EMPTY_ARGS, CONTAINER_NOCONST(AuctionLot, (pAuctionLot)))

// Indicates whether the auction can be owned by paying the buyout price
bool auction_trh_AcceptsBuyout(ATR_ARGS, ATH_ARG NOCONST(AuctionLot) *pAuctionLot);
#define Auction_AcceptsBuyout(pAuctionLot) auction_trh_AcceptsBuyout(ATR_EMPTY_ARGS, CONTAINER_NOCONST(AuctionLot, (pAuctionLot)))

// Indicates whether the auction can be canceled
bool auction_trh_CanBeCanceled(ATR_ARGS, ATH_ARG NOCONST(AuctionLot) *pAuctionLot);
#define Auction_CanBeCanceled(pAuctionLot) auction_trh_CanBeCanceled(ATR_EMPTY_ARGS, CONTAINER_NOCONST(AuctionLot, (pAuctionLot)))

// Calculates the final sales fee. This function already takes the posting fee paid into account.
U32 auction_trh_GetFinalSalesFee(ATR_ARGS, ATH_ARG NOCONST(Entity) *pAuctionOwnerEnt, ATH_ARG NOCONST(AuctionLot) *pAuctionLot);

#ifndef GAMECLIENT

// Called to close an auction (works for bids and buyouts)
bool auction_trh_CloseAuction(ATR_ARGS, 
	ATH_ARG NOCONST(Entity) *pOwner, 
	ATH_ARG NOCONST(Entity) *pBuyer, 
	ATH_ARG NOCONST(AuctionLot) *pLot,
	bool bBuyout,
	const ItemChangeReason *pSellerReason,
	const ItemChangeReason *pBuyerReason);

#endif