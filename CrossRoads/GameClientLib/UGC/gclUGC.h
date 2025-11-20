#pragma once

typedef struct UGCContentInfo UGCContentInfo;
typedef struct UGCFeaturedData UGCFeaturedData;
typedef struct UGCProject UGCProject;
typedef struct UGCProjectList UGCProjectList;
typedef struct UGCProjectReviews UGCProjectReviews;
typedef struct UGCProjectSeries UGCProjectSeries;
typedef struct UGCSearchResult UGCSearchResult;

typedef void (*UGCSearchResultCallback)(UGCSearchResult *, void *userdata);

//resets anything that was set while going into UGC mode back to normal state. For instance, 
//turns all resource dictionaries back out of edit mode. It MUST be safe to run this function
//even if we never actually went into UGC mode, in which case it should do nothing safely.
void gclClearAllUGCEditModeStuff(void);

void gclUGC_ReSyncGodMode(void);
bool gclUGC_IsResourceUGC(const char* pchResourceName);

bool gclUGC_PlayerIsOwner(SA_PARAM_OP_VALID UGCProject *pProject);
bool gclUGC_PlayerJustCompletedOrDroppedUGCMission(ContainerID iProjectID);

SA_ORET_OP_VALID UGCFeaturedData* gclUGC_GetContentInfoFeaturedData(SA_PARAM_OP_VALID UGCContentInfo* pInfo);


// These all are exposed to allow login-server searching
void gclUGC_ReceiveSearchResult(UGCSearchResult *pSearchResult);
void gclUGC_SetSearchResultCallback(UGCSearchResultCallback callback, void *userdata);

void gclUGC_CacheOncePerFrame( void );
SA_ORET_OP_VALID UGCProject* gclUGC_CacheGetProject( ContainerID projID );
SA_ORET_OP_VALID UGCProjectSeries* gclUGC_CacheGetProjectSeries( ContainerID seriesID );
void gclUGC_CacheReceiveSearchResult(UGCProjectList *pSearchResult);

void gclUGC_RequestReviewsForPage(U32 uProjectID, U32 uSeriesID, int iPageNumber);
bool gclUGC_RequestReviewsInProgress(void);
void gclUGC_ReceiveReviewsForPage(U32 uProjectID, U32 uSeriesID, int iPageNumber, UGCProjectReviews* pReviews);

SA_ORET_OP_STR const char* gclUGC_PlayerAllegiance(void);
void gclUGC_GetBrowseContent(S32 eLanguage);
void gclUGC_GetFeaturedContent(S32 eLanguage, bool bIncludeArchives);
bool gclUGC_SeriesProjectIsVisible(U32 iSeries, U32 iProject);
U32 gclUGC_ProjectCompletionTime(U32 iProject);
SA_ORET_OP_STR const char* gclUGC_GetSearchErrorForResults(UGCSearchResult* pResults);
