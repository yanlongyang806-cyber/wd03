#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GlobalTypeEnum.h"

AUTO_ENUM;
typedef enum AuctionLotState {
	ALS_New = 0,		//Items still being added
	ALS_Open,			//for sale
	ALS_Closed,			//withdrawn
	ALS_Sold,			//transfered ownership
	ALS_Mailed,         //do not list these - for use by internal systems (eg. mail)
	ALS_Cleanup,		//In the process of being cleaned up
	ALS_Cleanup_Mailed,		//In the process of being cleaned up
	ALS_Cleanup_BiddingClosed,		// An auction with valid bids has ended and being cleaned up
} AuctionLotState;

AUTO_ENUM;
typedef enum AuctionSearchResult
{
	ASR_SearchCap = -1,			//We hit the hard element search cap (culled list was too big)
	ASR_Finished = 0,			//We got all possible results
	ASR_RequstedCap = 1,		//We got numRequested
}AuctionSearchResult;

AUTO_ENUM;
typedef enum AuctionSortColumn
{
	AuctionSort_Price,					// Sort by price first, name second
	AuctionSort_Name,					// Sort by name first, price second
	AuctionSort_PricePerUnit,			// Sort by price per unit first, name second
	AuctionSort_PriceDesc,				// Sort by price first, name second
	AuctionSort_NameDesc,				// Sort by name first, price second
	AuctionSort_PricePerUnitDesc,		// Sort by price per unit first, name second
	AuctionSort_ExpireTimeValueAsc,		// Sort ascending by auction expiration time first, name second
	AuctionSort_ExpireTimeValueDesc,	// Sort descending by auction expiration time first, name second
	AuctionSort_ItemNumericValueAsc,	// Sort ascending by item numeric value, name second
	AuctionSort_ItemNumericValueDesc,	// Sort descending by item numeric value, name second
	AuctionSort_NoSort,					// Don't sort the results at all
	
	AuctionSort_Count, EIGNORE
} AuctionSortColumn;

