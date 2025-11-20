/***************************************************************************
*     Copyright (c) 2013, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef AUCTIONCOMMON_H
#define AUCTIONCOMMON_H

#include "GlobalTypeEnum.h"
#include "StashTable.h"

#define PERSISTED_PRICES_PER_ITEM 30
#define SECONDS_PER_PRICE_HISTORY_TRANSACTION 600

typedef struct AuctionPriceHistoryChunk AuctionPriceHistoryChunk;
AUTO_STRUCT AST_CONTAINER;
typedef struct AuctionPriceHistory
{
	CONST_STRING_POOLED pchItemDefName;	AST(PERSIST SUBSCRIBE POOL_STRING )
	CONST_INT_EARRAY eaiLastSalePrices; AST(PERSIST SUBSCRIBE)
	const U32 uNextWriteIndex;	AST(PERSIST SUBSCRIBE)
	S32 iCachedSum;
	AuctionPriceHistoryChunk* pParentChunk; NO_AST

} AuctionPriceHistory;

AUTO_STRUCT AST_CONTAINER;
typedef struct AuctionPriceHistoryChunk
{
	const int iKey;	AST(PERSIST SUBSCRIBE KEY)
	CONST_EARRAY_OF(AuctionPriceHistory) eaPrices; AST(PERSIST SUBSCRIBE)
	bool bDirty;	NO_AST
} AuctionPriceHistoryChunk;

AUTO_STRUCT AST_CONTAINER AST_IGNORE_STRUCT(eaPrices);
typedef struct AuctionPriceHistories
{
	const U32 uContainerID;	AST(PERSIST SUBSCRIBE KEY)
	//there will only ever be a singleton of this container, with ID 1

	CONST_EARRAY_OF(AuctionPriceHistoryChunk) eaPriceChunks; AST(PERSIST SUBSCRIBE)
} AuctionPriceHistories;

#endif //AUCTIONCOMMON_H