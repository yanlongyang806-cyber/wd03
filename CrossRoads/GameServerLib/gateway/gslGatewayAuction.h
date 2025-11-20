/***************************************************************************
 *     Copyright (c) Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#ifndef GSLGATEWAYAUCTION_H__
#define GSLGATEWAYAUCTION_H__

#pragma once

#define GATEWAYAUCTION_SEARCH "search"
#define GATEWAYAUCTION_BIDS "bidLots"
#define GATEWAYAUCTION_OWNED "ownedLots"


void session_SendIntegerUpdate(GatewaySession *psess, const char *pchUpdateMessage, U32 iInt);
void session_SendTradeBagResult(GatewaySession *psess, TradeBagLite *pBag);

typedef struct MappedAuctionSearch MappedAuctionSearch;
typedef struct ParamsAuctionSearch ParamsAuctionSearch;

void SubscribeAuctionSearch(GatewaySession *psess, ContainerTracker *ptracker, ParamsAuctionSearch *pParams);
bool IsModifiedAuctionSearch(GatewaySession *psess, ContainerTracker *ptracker);
bool CheckModifiedAuctionSearch(GatewaySession *psess, ContainerTracker *ptracker);
bool IsReadyAuctionSearch(GatewaySession *psess, ContainerTracker *ptracker);
MappedAuctionSearch *CreateMappedAuctionSearch(GatewaySession *psess, ContainerTracker *ptracker, MappedAuctionSearch *psearch);
void DestroyMappedAuctionSearch(GatewaySession *psess, ContainerTracker *ptracker, MappedAuctionSearch *psearch);

void session_updateAuctionLotList(GatewaySession *psess, char *pchLotID, AuctionLotList *pList);

#endif /* #ifndef GSLGATEWAYAUCTION_H__ */

/* End of File */
