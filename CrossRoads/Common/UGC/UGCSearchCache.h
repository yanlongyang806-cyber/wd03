//// UGC Search Cache
////
//// This is shared code that the GameClient, GameServer, and AppServer can all use to cache the results
//// provided by UGCSearchManager when turning a UGCProjectSearchInfo into a UGCSearchResult.
#pragma once

typedef struct UGCProjectSearchInfo UGCProjectSearchInfo;

UGCProjectSearchInfo *ugcSearchCacheFind(UGCProjectSearchInfo *pUGCProjectSearchInfo);
void ugcSearchCacheStore(UGCProjectSearchInfo *pUGCProjectSearchInfo);
