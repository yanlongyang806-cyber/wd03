/***************************************************************************
*     Copyright (c) 2013, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AuctionCommon.h"
#include "AuctionCommon_h_ast.h"
#include "objSchema.h"
#include "TransactionSystem.h"
#include "objTransactions.h"

AUTO_RUN_LATE;
int RegisterAuctionPriceHistoryContainer(void)
{
	objRegisterNativeSchema(GLOBALTYPE_AUCTIONPRICEHISTORYCONTAINER, parse_AuctionPriceHistories, NULL, NULL, NULL, NULL, NULL);

	return 1;
}
#include "AuctionCommon_h_ast.c"