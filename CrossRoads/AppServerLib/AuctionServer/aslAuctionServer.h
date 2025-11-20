
#ifndef ASLAUCTIONSERVER_H
#define ASLAUCTIONSERVER_H

#include "AuctionLot.h"

typedef struct ObjectIndex ObjectIndex;
typedef struct ObjectIndexKey ObjectIndexKey;

#define MAX_RESULTS_PER_SEARCH 200 // Maximum number of results to return in a search

#define PRICE_HISTORY_CONTAINER_ID  1
#define SECONDS_PER_PRICE_HISTORY_TICK 5

//Modifying the following value will require a fixup for already-persisted data.
#define PRICE_HISTORIES_PER_CHUNK 100.0

typedef bool (*AuctionFilter_CB)(AuctionLot *pLot, AuctionSearchRequest *pRequest);

int AuctionServerLibInit(void);

int AuctionServerLibOncePerFrame(F32 fElapsed);

S64 AuctionServer_SearchLots(AuctionLotList *pLotList, AuctionSearchRequest *pRequest, AuctionFilter_CB pFilterCallback, char **estrSearchType);

S64 AuctionServer_SearchAllLots(AuctionLotList *ll, AuctionSearchRequest *request, AuctionFilter_CB filter);
S64 AuctionServer_SearchLotsMailID(const ContainerID iAccountID, U32 **ppLotIDs);
S64 AuctionServer_SearchLotsForOwner(AuctionLotList *ll, AuctionSearchRequest *request, AuctionFilter_CB filter);

S64 AuctionServer_SearchItemsLevelRange(AuctionLotList *ll, AuctionSearchRequest *request, AuctionFilter_CB filter);
S64 AuctionServer_SearchItemsQuality(AuctionLotList *ll, AuctionSearchRequest *request, AuctionFilter_CB filter);
S64 AuctionServer_SearchItemsSortType(AuctionLotList *ll, AuctionSearchRequest *request, AuctionFilter_CB filter);
S64 AuctionServer_SearchItemsSortTypeCategory(AuctionLotList *ll, AuctionSearchRequest *request, AuctionFilter_CB filter);
S64 AuctionServer_SearchItemsUsageRestriction(AuctionLotList *ll, AuctionSearchRequest *request, AuctionFilter_CB filter);

AuctionLot *AuctionServer_GetLot(ContainerID iLotID);

ObjectIndex *AuctionServer_GetAuctionItemOwnerIndex(void);

#endif //ASLAUCTIONSERVER_H