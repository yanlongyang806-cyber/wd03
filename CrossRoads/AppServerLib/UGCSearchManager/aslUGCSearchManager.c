/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslUGCSearchManager.h"
#include "AppServerLib.h"

#include "objSchema.h"
#include "error.h"
#include "serverlib.h"
#include "autostartupsupport.h"
#include "resourceManager.h"
#include "objTransactions.h"
#include "winInclude.h"
#include "globalTypes.h"
#include "autogen/controller_autogen_remotefuncs.h"
#include "autogen/appserverlib_autogen_remotefuncs.h"
#include "ugcProjectCommon.h"
#include "ugcprojectCommon_h_ast.h"
#include "logging.h"
#include "SubStringSearchTree.h"
#include "stringutil.h"
#include "aslTextSearch.h"
#include "file.h"
#include "aslUGCSearchManagerAccounts.h"
#include "ugcCommon.h"
#include "continuousBuilderSupport.h"
#include "Autogen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "pub/aslMapManagerPub.h"
#include "TimedCallback.h"
#include "autogen/aslMapManagerPub_h_ast.h"
#include "aslUGCSearchManagerSortByRatings.h"
#include "alerts.h"
#include "aslUGCSearchManageR_c_ast.h"
#include "UGCProjectUtils.h"
#include "UGCAchievements.h"
#include "httpXPathSupport.h"
#include "ShardVariableCommon.h"
#include "ChoiceTable_common.h"
#include "queue_common.h"
#include "UGCSearchCache.h"

static WhatsHotList sWhatsHot = {0};


static SubStringSearchTreeInt *spUGCProjectSSSTree = NULL;
static SubStringSearchTreeInt *spUGCProjectSeriesSSSTree = NULL;

static SubStringSearchTree *s_UGCContentOwnerSSSTree = NULL;
static StashTable s_UGCContentOwnerByID = NULL;

static AslTextSearchManager *spUGCProjectTextSearchManager = NULL;
static AslTextSearchManager *spUGCProjectSeriesTextSearchManager = NULL;

static StashTable sReviewerProjectsByID = NULL;

static StashTable sNewProjectsByID = NULL;
static StashTable sNewSeriesByID = NULL;

static bool gbShowUnpublishedProjectsForDebugging;
AUTO_CMD_INT(gbShowUnpublishedProjectsForDebugging, DebugShowUnpublishedProjects);

static int siMaximumDaysOldForNewContentList = 21;
AUTO_CMD_INT(siMaximumDaysOldForNewContentList, MaximumDaysOldForNewContentList);

static UGCContentInfo** g_eaFeaturedListSortedByTime = NULL;
static StashTable g_sAuthorAllowsFeaturedProjectsByID = NULL;

static U32 g_UGCFeaturedLastRefresh = 0;
static U32 g_UGCFeaturedSecondsBeteenRefresh = 60;
AUTO_CMD_INT(g_UGCFeaturedSecondsBeteenRefresh, UGCFeaturedSecondsBetweenRefresh);

DictionaryHandle hUGCProjectDictionary = NULL;
DictionaryHandle hUGCProjectSeriesDictionary = NULL;

//__CATEGORY UGC settings
// If non-zero, then all UGC searches for AL 0 characters will return zero results
static int giDisableUGCSearchForAL0 = 0;
AUTO_CMD_INT(giDisableUGCSearchForAL0, DisableUGCSearchForAL0) ACMD_AUTO_SETTING(Ugc, UGCSEARCHMANAGER);

//__CATEGORY UGC settings
// If non-zero, then the search cache is enabled for the UGCSearchManager
static int s_bUGCEnableSearchCache = 0;
AUTO_CMD_INT(s_bUGCEnableSearchCache, UGCEnableSearchCacheForSearchManager) ACMD_AUTO_SETTING(Ugc, UGCSEARCHMANAGER);

static UGCFeaturedData* UGCFeatured_ContentFeaturedData( const UGCContentInfo* pContent );
static void UGCFeatured_UpdateContent( const UGCContentInfo* pContent );
static void UGCFeatured_RemoveContent( const UGCContentInfo* pContent );
static void UGCFeatured_UpdateAuthorAllowsFeaturedContent( const UGCContentInfo* pContent );
static void UGCFeatured_RemoveAuthorAllowsFeaturedContent( const UGCContentInfo* pContent );

typedef struct UGCContentOwnerInfo
{
	char *pOwnerName;
	ContainerID *pProjectIDs;
	ContainerID *pProjectSeriesIDs;
} UGCContentOwnerInfo;

static __forceinline UGCProject *GetProject(U32 iID)
{
	char pchTemp[16];
	sprintf(pchTemp, "%u", iID);

	return RefSystem_ReferentFromString(hUGCProjectDictionary, pchTemp);
}

static __forceinline UGCProjectSeries *GetProjectSeries(U32 iID)
{
	char pchTemp[16];
	sprintf(pchTemp, "%u", iID);

	return RefSystem_ReferentFromString(hUGCProjectSeriesDictionary, pchTemp);
}

static __forceinline bool IsSearchable(UGCProject *pProject)
{
	return gbShowUnpublishedProjectsForDebugging || UGCProject_IsPublishedAndNotDeleted(pProject);
}

static const char* UGCGetAccountNameToUse( const char* id, const char* storedAccountName )
{
	const char *pOwnerAccountNameToUse = storedAccountName;

	if (!pOwnerAccountNameToUse || !pOwnerAccountNameToUse[0])
	{
		if (g_isContinuousBuilder)
		{
			pOwnerAccountNameToUse = "CB";
		}
		else
		{
			AssertOrAlert("UGC_NO_ACCOUNT_NAME", "UGC project %s has no owner account name. Using ANON for now", id);
			pOwnerAccountNameToUse = "ANON";
		}
	}

	return pOwnerAccountNameToUse;
}

static UGCContentOwnerInfo* UGCSearchManager_InternOwner( const char* pOwnerName, ContainerID iOwnerID )
{
	UGCContentOwnerInfo *pOwner = NULL;
	UGCContentOwnerInfo *pOwnerStash = NULL;
	UGCContentOwnerInfo **ppOwners = NULL;
	
	if (!s_UGCContentOwnerSSSTree)
	{
		s_UGCContentOwnerSSSTree = SSSTree_Create(2);
	}
	if( !s_UGCContentOwnerByID ) {
		s_UGCContentOwnerByID = stashTableCreateInt( 4 );
	}

	SSSTree_FindElementsByPreciseName(s_UGCContentOwnerSSSTree, &ppOwners, pOwnerName);
	if (eaSize(&ppOwners))
	{
		if (eaSize(&ppOwners) > 1)
		{
			AssertOrAlert("UGC_OWNER_NAME_OVERLAP", "Somehow got more than one UGC owner named %s", pOwnerName);
		}

		pOwner = ppOwners[0];
		eaDestroy(&ppOwners);
	}

	if (!pOwner)
	{
		pOwner = calloc(sizeof(UGCContentOwnerInfo), 1);
		pOwner->pOwnerName = strdup(pOwnerName);
		SSSTree_AddElement(s_UGCContentOwnerSSSTree, pOwner->pOwnerName, pOwner);
		stashIntAddPointer( s_UGCContentOwnerByID, iOwnerID, pOwner, false );
	}

	return pOwner;
}


void UGCSearchManager_AddProjectForOwner(const char *pOwnerName, ContainerID iOwnerID, UGCProject *pProject)
{
	if( iOwnerID ) {
		UGCContentOwnerInfo *pOwner = UGCSearchManager_InternOwner( pOwnerName, iOwnerID );
		ea32Push(&pOwner->pProjectIDs, pProject->id);
	}
}
			

void UGCSearchManager_RemoveProjectForOwner(const char *pOwnerName, ContainerID iOwnerID, UGCProject *pProject)
{
	if( iOwnerID ) {
		UGCContentOwnerInfo *pOwner = UGCSearchManager_InternOwner( pOwnerName, iOwnerID );
		ea32FindAndRemove(&pOwner->pProjectIDs, pProject->id);
	}
}

void UGCSearchManager_AddProjectSeriesForOwner(const char *pOwnerName, ContainerID iOwnerID, UGCProjectSeries *pProjectSeries)
{
	if( iOwnerID ) {
		UGCContentOwnerInfo* pOwner = UGCSearchManager_InternOwner( pOwnerName, iOwnerID );
		ea32Push(&pOwner->pProjectSeriesIDs, pProjectSeries->id);
	}
}
			

void UGCSearchManager_RemoveProjectSeriesForOwner(const char *pOwnerName, ContainerID iOwnerID, UGCProjectSeries *pProjectSeries)
{
	if( iOwnerID ) {
		UGCContentOwnerInfo* pOwner = UGCSearchManager_InternOwner( pOwnerName, iOwnerID );
		ea32FindAndRemove(&pOwner->pProjectSeriesIDs, pProjectSeries->id);
	}
}

bool ContentIsNew(const UGCProjectReviews *pReviews)
{
	if(!pReviews->iTimeBecameReviewed)
		return false;

	if(pReviews->iTimeBecameReviewed < timeSecondsSince2000() - siMaximumDaysOldForNewContentList * (24 * 60 * 60))
		return false;

	return true;
}

float UGCProject_Rating( const UGCProject* pProject )
{
	return pProject->ugcReviews.fAdjustedRatingUsingConfidence;
}

float UGCProjectSeries_Rating( const UGCProjectSeries* pProjectSeries )
{
	return pProjectSeries->ugcReviews.fAdjustedRatingUsingConfidence;
}

/// Certain states, like being featured, can force a UGCProject to
/// appear earlier or later.  This function contains all that logic.
///
/// By default, all projects have a rating 0.0 - 1.0.
float UGCProject_RatingForSorting( const UGCProject* pProject )
{
	if( UGCProject_IsFeaturedNow( ATR_EMPTY_ARGS, CONTAINER_NOCONST( UGCProject, pProject ) )) {
		return 2.0;
	}

	return pProject->ugcReviews.fAdjustedRatingUsingConfidence;
}

float UGCProjectSeries_RatingForSorting( const UGCProjectSeries* pProjectSeries )
{
	return pProjectSeries->ugcReviews.fAdjustedRatingUsingConfidence;
}

void AddProjectToAllLists(UGCProject *pProject)
{
	const UGCProjectVersion* pVersion = UGCProject_GetMostRecentPublishedVersion(pProject);
	UGCContentInfo contentInfo = { 0 };
	contentInfo.iUGCProjectID = pProject->id;
		
	PERFINFO_AUTO_START_FUNC();

	if (pProject->ugcReviews.iNumRatingsCached < ugc_NumReviewsBeforeNonReviewerCanPlay)
	{
		stashIntAddPointer(sReviewerProjectsByID, pProject->id, NULL, true);	
	}

	if(ContentIsNew(&pProject->ugcReviews))
		stashIntAddPointer(sNewProjectsByID, pProject->id, NULL, true);

	SortByRatings_AddOrUpdate(GLOBALTYPE_UGCPROJECT, pProject->id, pProject, UGCProject_RatingForSorting( pProject ));

	SSSTreeInt_AddElement(spUGCProjectSSSTree, UGCProject_GetVersionName(pProject, pVersion), pProject->id);		
	UGCSearchManager_AddProjectForOwner(UGCGetAccountNameToUse( pProject->pIDString, pProject->pOwnerAccountName ), pProject->iOwnerAccountID, pProject);
	
	if (pVersion && !nullStr(pVersion->pDescription))
	{
		aslTextSearch_AddString(spUGCProjectTextSearchManager, pProject->id, pVersion->pDescription, true);
	}

	if( pProject->pFeatured ) {
		UGCFeatured_UpdateContent( &contentInfo );
	}
	UGCFeatured_UpdateAuthorAllowsFeaturedContent( &contentInfo );

	PERFINFO_AUTO_STOP();
}

void RemoveProjectFromAllLists(UGCProject *pProject)
{
	const UGCProjectVersion* pVersion = UGCProject_GetMostRecentPublishedVersion(pProject);
	UGCContentInfo contentInfo = { 0 };
	contentInfo.iUGCProjectID = pProject->id;
	
	stashIntRemovePointer(sReviewerProjectsByID, pProject->id, NULL);
	stashIntRemovePointer(sNewProjectsByID, pProject->id, NULL);

	SSSTreeInt_RemoveElement(spUGCProjectSSSTree, pProject->pPublicName_ForSearching, pProject->id);
	UGCSearchManager_RemoveProjectForOwner(UGCGetAccountNameToUse( pProject->pIDString, pProject->pOwnerAccountName ), pProject->iOwnerAccountID, pProject);
	aslTextSearch_RemoveString(spUGCProjectTextSearchManager, pProject->id);

	if( pProject->pFeatured ) {
		UGCFeatured_RemoveContent( &contentInfo );
	}
	UGCFeatured_RemoveAuthorAllowsFeaturedContent( &contentInfo );

	SortByRatings_Remove(GLOBALTYPE_UGCPROJECT, pProject->id);
	SortByRatings_Remove(GLOBALTYPE_UGCPROJECT, pProject->id);
}

//when a UGC project is modified, we want to know if any of the fields we care about were modified, so we can do the minimal
//possible work to update it in all the various searchy indexes... so at premodified time we save off a bunch of
//info, and then compare it to what we get at post-modify time
AUTO_STRUCT;
typedef struct UGCProjectModificationCache
{
	bool bSearchable; //true if UGCProject_GetMostRecentPublishedVersionDescription() is true and iDeletionTime is not set
	U32 iContainerID;
	bool bReviewerOnly;
	bool bNew;
	char *pPublicName;
	char *pDescription;
	float fRating;
	bool bFeatured;
	U32 iOwnerAccountId;
	char *pOwnerAccountName;
} UGCProjectModificationCache;


UGCProjectModificationCache sProjectModificationCache = {0};

void DoListStuffWithProjectModificationCache(UGCProject *pProject)
{
	const UGCProjectVersion* pVersion = UGCProject_GetMostRecentPublishedVersion(pProject);
	bool bNowReviewerOnly = pProject->ugcReviews.iNumRatingsCached < ugc_NumReviewsBeforeNonReviewerCanPlay;
	bool bNowNew = ContentIsNew(&pProject->ugcReviews);
	UGCContentInfo contentInfo = { 0 };
	contentInfo.iUGCProjectID = pProject->id;
	
	PERFINFO_AUTO_START_FUNC();


	//for tne new case, we can be lazy because the new table cleans itself up as it goes... so just make sure it's
	//added if it's now new
	if (bNowNew)
	{
		stashIntAddPointer(sNewProjectsByID, pProject->id, NULL, true);
	}


	if (sProjectModificationCache.bReviewerOnly)
	{
		if (bNowReviewerOnly)
		{
			//do nothing
		}
		else
		{
			//was reviewer only, no longer
			stashIntRemovePointer(sReviewerProjectsByID, pProject->id, NULL);
		}
	}
	else
	{
		if (bNowReviewerOnly)
		{
			//wasn't reviewer only, now is... might as well support a weird case
			stashIntAddPointer(sReviewerProjectsByID, pProject->id, NULL, true);
		}
	}

	if (strcmp(sProjectModificationCache.pPublicName, UGCProject_GetVersionName(pProject, pVersion)) != 0)
	{
		SSSTreeInt_RemoveElement(spUGCProjectSSSTree, sProjectModificationCache.pPublicName, pProject->id);
		SSSTreeInt_AddElement(spUGCProjectSSSTree, pVersion->pName, pProject->id);
	}

	if (sProjectModificationCache.fRating != UGCProject_RatingForSorting( pProject ))
	{
		SortByRatings_AddOrUpdate(GLOBALTYPE_UGCPROJECT, pProject->id, pProject, UGCProject_RatingForSorting( pProject ));
	}

	if (strcmp_safe(sProjectModificationCache.pDescription, SAFE_MEMBER(pVersion, pDescription)) != 0)
	{
		aslTextSearch_RemoveString(spUGCProjectTextSearchManager, pProject->id);
		if (SAFE_MEMBER(pVersion, pDescription))
		{
			aslTextSearch_AddString(spUGCProjectTextSearchManager, pProject->id, pVersion->pDescription, true);
		}
	}

	if( sProjectModificationCache.bFeatured || pProject->pFeatured ) {
		UGCFeatured_UpdateContent( &contentInfo );
	}
	UGCFeatured_UpdateAuthorAllowsFeaturedContent( &contentInfo );

	if(0 != strcmp(sProjectModificationCache.pOwnerAccountName, pProject->pOwnerAccountName) || sProjectModificationCache.iOwnerAccountId != pProject->iOwnerAccountID)
	{
		UGCSearchManager_RemoveProjectForOwner(sProjectModificationCache.pOwnerAccountName, sProjectModificationCache.iOwnerAccountId, pProject);
		UGCSearchManager_AddProjectForOwner(pProject->pOwnerAccountName, pProject->iOwnerAccountID, pProject);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void UGCProjectResCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, UGCProject *pProject, void *pUserData)
{
	CONTAINER_NOCONST(UGCProject, pProject)->ugcReviews.iNumRatingsCached = ugcReviews_GetRatingCount( CONTAINER_NOCONST( UGCProjectReviews, &pProject->ugcReviews ));

	switch (eType)
	{
	case RESEVENT_RESOURCE_ADDED:
		{
			aslUGCSearchManagerAddAccountIDForPermissionChecking(pProject->iOwnerAccountID);
			
			if (IsSearchable(pProject))
			{
				AddProjectToAllLists(pProject);
			}
		}
		break;

	case RESEVENT_RESOURCE_REMOVED:

		RemoveProjectFromAllLists(pProject);
		break;


	case RESEVENT_RESOURCE_PRE_MODIFIED:
		{
			const UGCProjectVersion* pVersion = UGCProject_GetMostRecentPublishedVersion(pProject);
			StructReset(parse_UGCProjectModificationCache, &sProjectModificationCache);
			sProjectModificationCache.iContainerID = pProject->id;

			if (!IsSearchable(pProject))
			{
				//didn't used to be searchable, so the rest of this is irrelevant
				return;
			}

			sProjectModificationCache.bSearchable = true;
			sProjectModificationCache.bNew = ContentIsNew(&pProject->ugcReviews);
			sProjectModificationCache.bReviewerOnly = pProject->ugcReviews.iNumRatingsCached < ugc_NumReviewsBeforeNonReviewerCanPlay;
			sProjectModificationCache.pPublicName = strdup(UGCProject_GetVersionName(pProject, pVersion));
			sProjectModificationCache.fRating = UGCProject_RatingForSorting( pProject );
			sProjectModificationCache.iOwnerAccountId = pProject->iOwnerAccountID;
			sProjectModificationCache.pOwnerAccountName = strdup(pProject->pOwnerAccountName);
			if (SAFE_MEMBER(pVersion, pDescription))
			{
				sProjectModificationCache.pDescription = strdup(pVersion->pDescription);
			}
			sProjectModificationCache.bFeatured = (pProject->pFeatured != NULL);
		}
		break;

	case RESEVENT_RESOURCE_MODIFIED:
		aslUGCSearchManagerAddAccountIDForPermissionChecking(pProject->iOwnerAccountID);

		if (pProject->id == sProjectModificationCache.iContainerID)
		{
			if (IsSearchable(pProject))
			{
				if (sProjectModificationCache.bSearchable)
				{	
					DoListStuffWithProjectModificationCache(pProject);
				}
				else
				{
					AddProjectToAllLists(pProject);
				}
			}
			else
			{
				if (sProjectModificationCache.bSearchable)
				{
					RemoveProjectFromAllLists(pProject);
				}
				else
				{
					//do nothing... wasn't searchable before, isn't searchable now
				}
			}
		}
		else
		{
			TriggerAutoGroupingAlert("UGC_NO_PREMOD_FOR_UGC", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 30, "The UGCSearchManager got a RESOURCE_MODIFIED without a corresponding RESOURCE_PREMODIFIED for project %u. This shouldn't break anything but is a performance hit", pProject->id);

			RemoveProjectFromAllLists(pProject);

			if (!IsSearchable(pProject))
			{
				return;
			}

			AddProjectToAllLists(pProject);
		}
	
		break;
	}
}

void AddProjectSeriesToAllLists(UGCProjectSeries *pProjectSeries)
{
	UGCProjectSeriesVersion* pVersion = eaTail( &pProjectSeries->eaVersions );
	if(!pVersion) // will be auto deleted
		return;

	PERFINFO_AUTO_START_FUNC();

	if(ContentIsNew(&pProjectSeries->ugcReviews))
		stashIntAddPointer(sNewSeriesByID, pProjectSeries->id, NULL, true);

	SortByRatings_AddOrUpdate(GLOBALTYPE_UGCPROJECTSERIES, pProjectSeries->id, pProjectSeries, UGCProjectSeries_RatingForSorting(pProjectSeries));

	if(pProjectSeries->ugcSearchCache.strPublishedName)
		SSSTreeInt_AddElement(spUGCProjectSeriesSSSTree, pProjectSeries->ugcSearchCache.strPublishedName, pProjectSeries->id);

	UGCSearchManager_AddProjectSeriesForOwner( UGCGetAccountNameToUse( pProjectSeries->strIDString, pProjectSeries->strOwnerAccountName ), pProjectSeries->iOwnerAccountID, pProjectSeries);
	if( pVersion->strDescription ) {
		aslTextSearch_AddString( spUGCProjectTextSearchManager, pProjectSeries->id, pVersion->strDescription, true );
	}
	
	PERFINFO_AUTO_STOP();
}

void RemoveProjectSeriesFromAllLists(UGCProjectSeries *pProjectSeries)
{
	PERFINFO_AUTO_START_FUNC();

	stashIntRemovePointer(sNewSeriesByID, pProjectSeries->id, NULL);

	if(pProjectSeries->ugcSearchCache.strPublishedName)
		SSSTreeInt_RemoveElement(spUGCProjectSeriesSSSTree, pProjectSeries->ugcSearchCache.strPublishedName, pProjectSeries->id);

	UGCSearchManager_RemoveProjectSeriesForOwner( UGCGetAccountNameToUse( pProjectSeries->strIDString, pProjectSeries->strOwnerAccountName ), pProjectSeries->iOwnerAccountID, pProjectSeries);
	aslTextSearch_RemoveString(spUGCProjectSeriesTextSearchManager, pProjectSeries->id);

	SortByRatings_Remove(GLOBALTYPE_UGCPROJECTSERIES, pProjectSeries->id);

	PERFINFO_AUTO_STOP();
}

AUTO_STRUCT;
typedef struct UGCProjectSeriesModificationCache
{
	U32 iContainerID;
	U32 iOwnerAccountId;
	char *pOwnerAccountName;
	char *strPublishedName;
	float fRating;
} UGCProjectSeriesModificationCache;

UGCProjectSeriesModificationCache sProjectSeriesModificationCache = {0};

void DoListStuffWithProjectSeriesModificationCache(UGCProjectSeries *pProjectSeries)
{
	UGCProjectSeriesVersion* pVersion = eaTail( &pProjectSeries->eaVersions );
	if(!pVersion) // will be auto deleted
		return;

	PERFINFO_AUTO_START_FUNC();

	aslTextSearch_RemoveString(spUGCProjectSeriesTextSearchManager, pProjectSeries->id);

	if(sProjectSeriesModificationCache.fRating != UGCProjectSeries_RatingForSorting( pProjectSeries ))
		SortByRatings_AddOrUpdate(GLOBALTYPE_UGCPROJECTSERIES, pProjectSeries->id, pProjectSeries, UGCProjectSeries_RatingForSorting(pProjectSeries));

	if(!sProjectSeriesModificationCache.strPublishedName || !pProjectSeries->ugcSearchCache.strPublishedName ||
		0 != strcmp(sProjectSeriesModificationCache.strPublishedName, pProjectSeries->ugcSearchCache.strPublishedName))
	{
		if(sProjectSeriesModificationCache.strPublishedName)
			SSSTreeInt_RemoveElement(spUGCProjectSeriesSSSTree, sProjectSeriesModificationCache.strPublishedName, pProjectSeries->id);
		if(pProjectSeries->ugcSearchCache.strPublishedName)
			SSSTreeInt_AddElement(spUGCProjectSeriesSSSTree, pProjectSeries->ugcSearchCache.strPublishedName, pProjectSeries->id);
	}

	if(pVersion->strDescription)
		aslTextSearch_AddString(spUGCProjectTextSearchManager, pProjectSeries->id, pVersion->strDescription, true);

	if(0 != strcmp(sProjectSeriesModificationCache.pOwnerAccountName, pProjectSeries->strOwnerAccountName)
			|| sProjectSeriesModificationCache.iOwnerAccountId != pProjectSeries->iOwnerAccountID)
	{
		UGCSearchManager_RemoveProjectSeriesForOwner(UGCGetAccountNameToUse(pProjectSeries->strIDString, sProjectSeriesModificationCache.pOwnerAccountName), sProjectSeriesModificationCache.iOwnerAccountId, pProjectSeries);
		UGCSearchManager_AddProjectSeriesForOwner(UGCGetAccountNameToUse(pProjectSeries->strIDString, pProjectSeries->strOwnerAccountName), pProjectSeries->iOwnerAccountID, pProjectSeries);
	}

	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP_FUNC();
}

static void UGCSeriesResCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, UGCProjectSeries *pProjectSeries, void *pUserData)
{
	CONTAINER_NOCONST(UGCProjectSeries, pProjectSeries)->ugcReviews.iNumRatingsCached = ugcReviews_GetRatingCount( CONTAINER_NOCONST( UGCProjectReviews, &pProjectSeries->ugcReviews ));

	switch (eType)
	{
		case RESEVENT_RESOURCE_ADDED:
			aslUGCSearchManagerAddAccountIDForPermissionChecking(pProjectSeries->iOwnerAccountID);

			AddProjectSeriesToAllLists(pProjectSeries);

		break;
		case RESEVENT_RESOURCE_REMOVED:
			RemoveProjectSeriesFromAllLists(pProjectSeries);

		break;
		case RESEVENT_RESOURCE_PRE_MODIFIED:
		{
			StructReset(parse_UGCProjectSeriesModificationCache, &sProjectSeriesModificationCache);
			sProjectSeriesModificationCache.iContainerID = pProjectSeries->id;

			sProjectSeriesModificationCache.iOwnerAccountId = pProjectSeries->iOwnerAccountID;
			sProjectSeriesModificationCache.pOwnerAccountName = strdup(pProjectSeries->strOwnerAccountName);
			sProjectSeriesModificationCache.strPublishedName = strdup(pProjectSeries->ugcSearchCache.strPublishedName);
			sProjectSeriesModificationCache.fRating = UGCProjectSeries_RatingForSorting( pProjectSeries );
		}
		break;
		case RESEVENT_RESOURCE_MODIFIED:
			aslUGCSearchManagerAddAccountIDForPermissionChecking(pProjectSeries->iOwnerAccountID);

			if(pProjectSeries->id == sProjectSeriesModificationCache.iContainerID)
			{
				DoListStuffWithProjectSeriesModificationCache(pProjectSeries);
			}
			else
			{
				TriggerAutoGroupingAlert("UGC_NO_PREMOD_FOR_UGC", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 30, "The UGCSearchManager got a RESOURCE_MODIFIED without a corresponding RESOURCE_PREMODIFIED for project series %u. This shouldn't break anything but is a performance hit", pProjectSeries->id);

				RemoveProjectSeriesFromAllLists(pProjectSeries);

				AddProjectSeriesToAllLists(pProjectSeries);
			}
		break;
	}
}

AUTO_STARTUP(UGCSearchManager) 
ASTRT_DEPS(	UGCReporting,
			UGCSearchCache,
			AS_GameProgression, 
			AS_Messages,
			AS_UGCSearchManagerQueueLoad,
			ShardVariables,);
void aslUGCSearchManagerStartup(void)
{
}

AUTO_STARTUP(AS_UGCSearchManagerQueueLoad) ASTRT_DEPS(QueueCategories, CharacterClasses);
void UGCSearchManagerQueueLoad(void)
{
	choice_Load();
	Queues_Load(NULL, Queues_ReloadQueues);	// Do not call the aslQueue version here. Bad container things will happen
	Queues_LoadConfig();
}

void RequestWhatsHotListCB(void *pUserData1, void *pUserData2);


void RequestWhatsHotListTCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	VerifyServerTypeExistsInShard(GLOBALTYPE_UGCDATAMANAGER);
	RemoteCommand_RequestWhatsHotList(GLOBALTYPE_UGCDATAMANAGER, 0, GetAppGlobalID(), RequestWhatsHotListCB, NULL, NULL);
}

void RequestWhatsHotListCB(void *pUserData1, void *pUserData2)
{
	TimedCallback_Run(RequestWhatsHotListTCB, NULL, 1.0f);


}

S32 UGCSearchManagerOncePerFrame(F32 fElapsed)
{
	shardvariable_OncePerFrame();

	// Reindex all active featured projects for SortByRatings.	This
	// is because currently featured projects end up with a rating of
	// 2.0, but they will eventually become previously featured.
	if( g_UGCFeaturedLastRefresh + g_UGCFeaturedSecondsBeteenRefresh < timeSecondsSince2000() ) {
		U32 lastRefresh = g_UGCFeaturedLastRefresh;
		g_UGCFeaturedLastRefresh = timeSecondsSince2000();

		if( lastRefresh ) {
			FOR_EACH_IN_EARRAY_FORWARDS( g_eaFeaturedListSortedByTime, UGCContentInfo, pContent ) {
				UGCFeaturedData* pFeatured = UGCFeatured_ContentFeaturedData( pContent );

				if( pContent->iUGCProjectID ) {
					UGCProject* pProject = GetProject( pContent->iUGCProjectID );
					F32 rating = UGCProject_RatingForSorting( pProject );

					SortByRatings_AddOrUpdate(GLOBALTYPE_UGCPROJECT, pProject->id, pProject, UGCProject_RatingForSorting( pProject ));

					// We are spamming this transaction once per minute per currently featured project.
					// There's no way to avoid this because in the case of UGCSearchManager restarts, we may miss the tick where a project passes from
					// future featured to currently featured. If we need to avoid this spam in the future, we need a UGCProject Container field for whether
					// the UGC Achievement Event for being featured was sent or not.
					if(UGCProject_IsFeaturedNow( ATR_EMPTY_ARGS, CONTAINER_NOCONST( UGCProject, pProject )))
					{
						UGCAchievementEvent *event = StructCreate(parse_UGCAchievementEvent);
						event->uUGCAuthorID = pProject->iOwnerAccountID;
						event->uUGCProjectID = pProject->id;
						event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
						event->ugcAchievementServerEvent->ugcProjectFeaturedEvent = StructCreate(parse_UGCProjectFeaturedEvent);
						VerifyServerTypeExistsInShard(GLOBALTYPE_UGCDATAMANAGER);
						RemoteCommand_ugcAchievementEvent_Send(GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
						StructDestroy(parse_UGCAchievementEvent, event);
					}
				} else {
					AssertOrAlert( "UGC_FEATURED_UNSUPPORTED_TYPE", "Unsupported UGCContentInfo in g_eaFeaturedListSortedByTime." );
				}
			} FOR_EACH_END;
		}
	}

	return 1;
}

int UGCSearchManagerLibInit(void)
{

	AutoStartup_SetTaskIsOn("UGCSearchManager", 1);

	objLoadAllGenericSchemas();
		
	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	resFinishLoading();

	assertmsg(GetAppGlobalType() == GLOBALTYPE_UGCSEARCHMANAGER, "UGC Search Manager type not set");

	loadstart_printf("Connecting UGCSearchManager to TransactionServer (%s)... ", gServerLibState.transactionServerHost);

	while (!InitObjectTransactionManager(
		GetAppGlobalType(),
		gServerLibState.containerID,
		gServerLibState.transactionServerHost,
		gServerLibState.transactionServerPort,
		gServerLibState.bUseMultiplexerForTransactions, NULL))
	{
		Sleep(1000);
	}
	if (!objLocalManager())
	{
		loadend_printf("failed.");
		return 0;
	}

	loadend_printf("connected.");

	gAppServer->oncePerFrame = UGCSearchManagerOncePerFrame;

	hUGCProjectDictionary = RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECT), false, parse_UGCProject, false, false, NULL);
	resDictSetMaxUnreferencedResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECT), RES_DICT_KEEP_ALL); // We need to keep them around even if not references
	objSubscribeToOnlineContainers(GLOBALTYPE_UGCPROJECT);
	resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECT),UGCProjectResCB,NULL);
	
	hUGCProjectSeriesDictionary = RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECTSERIES), false, parse_UGCProjectSeries, false, false, NULL);
	resDictSetMaxUnreferencedResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECTSERIES), RES_DICT_KEEP_ALL);
	objSubscribeToOnlineContainers(GLOBALTYPE_UGCPROJECTSERIES);
	resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCPROJECTSERIES), UGCSeriesResCB, NULL);

	spUGCProjectTextSearchManager = aslTextSearch_Init("ugcDescriptions");
	assertmsgf(spUGCProjectTextSearchManager, "Couldn't create TextSearchManager");
	spUGCProjectSeriesTextSearchManager = aslTextSearch_Init("ugcSeriesDescriptions");
	assertmsgf(spUGCProjectSeriesTextSearchManager, "Couldn't create TextSearchManager");
	
	spUGCProjectSSSTree = SSSTreeInt_Create(UGCPROJ_MIN_NAME_SEARCH_STRING_LEN);
	spUGCProjectSeriesSSSTree = SSSTreeInt_Create(UGCPROJ_MIN_NAME_SEARCH_STRING_LEN);

	sReviewerProjectsByID = stashTableCreateInt(1024);
	sNewProjectsByID = stashTableCreateInt(1024);
	sNewSeriesByID = stashTableCreateInt(1024);
	SortyByRatings_Init();

	RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
	VerifyServerTypeExistsInShard(GLOBALTYPE_UGCDATAMANAGER);
	RemoteCommand_RequestWhatsHotList(GLOBALTYPE_UGCDATAMANAGER, 0, GetAppGlobalID(), RequestWhatsHotListCB, NULL, NULL);

	return 1;
}


AUTO_RUN;
int UGCSearchManagerRegister(void)
{
	aslRegisterApp(GLOBALTYPE_UGCSEARCHMANAGER,UGCSearchManagerLibInit, 0);

	return 1;
}

static bool RunMultiWordStringFilter(UGCProjectSearchFilter *pFilter, const char *pString)
{
	static char **ppWords = NULL;
	int iCount = 0;
	int i;

	switch (pFilter->eComparison)
	{
	case UGCCOMPARISON_CONTAINS:
		eaDestroyEx(&ppWords, NULL);
		DivideString(pFilter->pStrValue, " ", &ppWords, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

		for (i=0; i < eaSize(&ppWords); i++)
		{
			if(!strstri(pString, ppWords[i]))
				return false;
		}
		break;

	case UGCCOMPARISON_NOTCONTAINS:
		eaDestroyEx(&ppWords, NULL);
		DivideString(pFilter->pStrValue, " ", &ppWords, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

		for (i=0; i < eaSize(&ppWords); i++)
		{
			if(strstri(pString, ppWords[i]))
				iCount++;
		}

		if (iCount == eaSize(&ppWords))
		{
			return false;
		}
		return true;
	
	case UGCCOMPARISON_BEGINSWITH:
		if(!strStartsWith(pString, pFilter->pStrValue))
			return false;
		break;
	case UGCCOMPARISON_ENDSWITH:
		if(!strEndsWith(pString, pFilter->pStrValue))
			return false;
	}

	return true;
}

bool RunStringFilter(UGCProjectSearchFilter *pFilter, const char *pString)
{
	if(!pFilter->pStrValue || !pFilter->pStrValue[0])
		return false; // "" doesn't match anything

	//if we have a space, then this must be a search of description, which is left as whole words rather than being
	//glommed together for a SSSTree search. In this case, we do things word by word
	if (strchr(pFilter->pStrValue, ' '))
	{
		return RunMultiWordStringFilter(pFilter, pString);
	}

	switch(pFilter->eComparison)
	{
	case UGCCOMPARISON_CONTAINS:
		if(!strstri(pString, pFilter->pStrValue))
			return false;
		break;
	case UGCCOMPARISON_NOTCONTAINS:
		if(strstri(pString, pFilter->pStrValue))
			return false;
		break;
	case UGCCOMPARISON_BEGINSWITH:
		if(!strStartsWith(pString, pFilter->pStrValue))
			return false;
		break;
	case UGCCOMPARISON_ENDSWITH:
		if(!strEndsWith(pString, pFilter->pStrValue))
			return false;
		break;
	}
	return true;
}

bool RunNumberFilter(UGCProjectSearchFilter *pFilter, float fValue)
{
	switch(pFilter->eComparison)
	{
	case UGCCOMPARISON_LESSTHAN:
		if(fValue >= pFilter->fFloatValue)
			return false;
		break;
	case UGCCOMPARISON_GREATERTHAN:
		if(fValue < pFilter->fFloatValue)
			return false;
		break;
	}
	return true;
}

static bool RunIntNumberFilter(UGCProjectSearchFilter *pFilter, U32 uValue)
{
	switch(pFilter->eComparison)
	{
	case UGCCOMPARISON_LESSTHAN:
		if(uValue >= pFilter->uIntValue)
			return false;
		break;
	case UGCCOMPARISON_GREATERTHAN:
		if(uValue <= pFilter->uIntValue)
			return false;
		break;
	}
	return true;
}

bool RunTagsFilter(UGCProjectSearchFilter *pFilter, UGCProject *pProject)
{
	U32 iMatchCount = 0;
	U32 iNonMatchCount = 0;
	int i;

	if (!eaSize(&pFilter->ppTags))
	{
		if (!estrLength(&pFilter->pStrValue))
		{
			return false;
		}
		DivideString(pFilter->pStrValue, ",", &pFilter->ppTags, DIVIDESTRING_POSTPROCESS_ALLOCADD 
			| DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);
	}

	if (!eaSize(&pFilter->ppTags))
	{
		return false;
	}

	for (i=0; i < eaSize(&pFilter->ppTags); i++)
	{
		if (UGCProject_HasTag(pProject, pFilter->ppTags[i]))
		{
			iMatchCount++;
		}
		else
		{
			iNonMatchCount++;
		}
	}

	switch (pFilter->eComparison)
	{
	case UGCCOMPARISON_N_TAGS_ON:
		return (iMatchCount >= pFilter->uIntValue);
	case UGCCOMPARISON_N_TAGS_OFF:
		return (iNonMatchCount >= pFilter->uIntValue);
	}

	//something is wrong, but should have alreayd been caught by verify functions
	return false;

}

static bool ProjectMeetsBasicCriteria(UGCProjectSearchInfo *pSearchInfo, UGCProject *pProject)
{
	const UGCProjectVersion *pPublishedVersion;

	if (gbShowUnpublishedProjectsForDebugging)
	{
		return true;
	}
	if (UGCProject_CanAutoDelete(pProject))
	{
		return false;
	}

	pPublishedVersion = UGCProject_GetMostRecentPublishedVersion(pProject);

	if (!UGCProject_VersionIsPlayable(pProject, pPublishedVersion))
	{
		return false;
	}

	if(stricmp(GetProductName(), "StarTrek") && pSearchInfo->iAccessLevel > ACCESS_UGC)
	{
		return true;
	}

	// If this is a featured copy, and it has not yet been featured, we don't want to show it to the masses
	if ( pProject->uUGCFeaturedOrigProjectID && !UGCProject_IsFeatured( ATR_EMPTY_ARGS, CONTAINER_NOCONST( UGCProject, pProject ),
			/*bIncludePreviouslyFeatured=*/true, /*bIncludeFutureFeatured=*/false )) {
		return false;
	}

	if (ugcDefaultsSearchFiltersByPlayerLevel())
	{
		if ( pPublishedVersion->pRestrictions && pPublishedVersion->pRestrictions->iMinLevel && ( pSearchInfo->iPlayerLevel < pPublishedVersion->pRestrictions->iMinLevel ) )
		{
			return false;
		}
		if ( pPublishedVersion->pRestrictions && pPublishedVersion->pRestrictions->iMaxLevel && ( pSearchInfo->iPlayerLevel > pPublishedVersion->pRestrictions->iMaxLevel ) )
		{
			return false;
		}
	}

	if (pSearchInfo->pchPlayerAllegiance && pSearchInfo->pchPlayerAllegiance[0] && pPublishedVersion->pRestrictions && eaSize(&pPublishedVersion->pRestrictions->eaFactions))
	{
		bool bMatched = false;
		int i;

		for (i = 0;i < eaSize(&pPublishedVersion->pRestrictions->eaFactions); i++)
		{
			if (stricmp_safe(pSearchInfo->pchPlayerAllegiance, pPublishedVersion->pRestrictions->eaFactions[i]->pcFaction) == 0)
			{
				bMatched = true;
				break;
			}
		}

		if (!bMatched)
		{
			return false;
		}
	}

    if (pProject->ugcReporting.uTemporaryBanExpireTime > 0)
	{
		return false;
	}

	if(!pSearchInfo->bIsIDString)
	{
		bool bStillInReview = (pProject->ugcReviews.iNumRatingsCached < ugc_NumReviewsBeforeNonReviewerCanPlay);
		bool bLookingForInReview = (pSearchInfo->eSpecialType == SPECIALSEARCH_REVIEWER);

		if(bStillInReview != bLookingForInReview)
			return false;
	}

	return true;
}

static bool RunFilters(UGCProjectSearchInfo *pSearchInfo, UGCProject *pProject)
{
	const char *pString;
	float fValue;
	U32 uValue;
	const UGCProjectVersion *pPublishedVersion;

	PERFINFO_AUTO_START_FUNC();

	pPublishedVersion = UGCProject_GetMostRecentPublishedVersion(pProject);

	if(!gbShowUnpublishedProjectsForDebugging && !pPublishedVersion)
	{
		PERFINFO_AUTO_STOP();	
		return false; // No published version, never show it
	}

	if (!ProjectMeetsBasicCriteria(pSearchInfo, pProject))
	{
		PERFINFO_AUTO_STOP();	
		return false;
	}

	if (pSearchInfo->eLang != LANGUAGE_DEFAULT && pPublishedVersion->eLanguage != pSearchInfo->eLang) {
		PERFINFO_AUTO_STOP();
		return false;
	}

	//check if there is any overlap between search range and mission range:
	if (pPublishedVersion && pPublishedVersion->pRestrictions && (pSearchInfo->iPlayerLevelMin || pSearchInfo->iPlayerLevelMax))
	{
		bool bMinInRange = pPublishedVersion->pRestrictions->iMinLevel == 0;
		bool bMaxInRange = pPublishedVersion->pRestrictions->iMaxLevel == 0;

		if (pPublishedVersion->pRestrictions->iMinLevel && 
			(pSearchInfo->iPlayerLevelMin == 0 || pPublishedVersion->pRestrictions->iMinLevel >= pSearchInfo->iPlayerLevelMin) &&
			(pSearchInfo->iPlayerLevelMax == 0 || pPublishedVersion->pRestrictions->iMinLevel <= pSearchInfo->iPlayerLevelMax))
		{
			bMinInRange = true;
		}

		if (pPublishedVersion->pRestrictions->iMaxLevel && 
			(pSearchInfo->iPlayerLevelMin == 0 || pPublishedVersion->pRestrictions->iMaxLevel >= pSearchInfo->iPlayerLevelMin) &&
			(pSearchInfo->iPlayerLevelMax == 0 || pPublishedVersion->pRestrictions->iMaxLevel <= pSearchInfo->iPlayerLevelMax))
		{
			bMaxInRange = true;
		}

		if (!bMinInRange && !bMaxInRange)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	//check publish date (really the date it passed review):
	if(pSearchInfo->iPublishedInLastNDays 
		&& pProject->ugcReviews.iTimeBecameReviewed //0 if not reviewed yet; should show if reviewer.
		&& pProject->ugcReviews.iTimeBecameReviewed 
		< timeSecondsSince2000() - pSearchInfo->iPublishedInLastNDays * 86400)
	{
			return false;
	}

	//check location:
	if(pSearchInfo->pchLocation && pSearchInfo->pchLocation[0] &&
		stricmp(pSearchInfo->pchLocation, pPublishedVersion->pLocation )){
			return false;
	}


	if(pSearchInfo->ppFilters)
	{
		FOR_EACH_IN_EARRAY(pSearchInfo->ppFilters, UGCProjectSearchFilter, pFilter)
		switch(pFilter->eType)
		{
		case UGCFILTER_SIMPLESTRING:
			if (!strstri_safe(pProject->pPublicName_ForSearching, pFilter->pStrValue)
				&& !strstri_safe(pProject->pOwnerAccountName_ForSearching, pFilter->pStrValue)
				&& !strstri_safe(pPublishedVersion ? pPublishedVersion->pDescription : NULL, pFilter->pStrValue))
			{
				PERFINFO_AUTO_STOP();	
				return false;
			}
			break;

				

		case UGCFILTER_STRING:
			if(stricmp(pFilter->pField, "Name")==0)
				pString = pProject->pPublicName_ForSearching;
			else if(stricmp(pFilter->pField, "Author")==0)
				pString = pProject->pOwnerAccountName_ForSearching;
			else if (stricmp(pFilter->pField, "description") == 0)
				pString = pPublishedVersion ? pPublishedVersion->pDescription : NULL;
			else
			{
				PERFINFO_AUTO_STOP();	
				return false;
			}
			if(!RunStringFilter(pFilter, pString))
			{
				PERFINFO_AUTO_STOP();	
				return false;
			}
			break;

		case UGCFILTER_RATING:
			if(stricmp(pFilter->pField, "Rating")==0)
				fValue = UGCProject_Rating(pProject);
			else
			{
				PERFINFO_AUTO_STOP();	
				return false;
			}
			if(!RunNumberFilter(pFilter, fValue))
			{
				PERFINFO_AUTO_STOP();	
				return false;
			}
			break;

		case UGCFILTER_AVERAGEPLAYTIME:
			if(stricmp(pFilter->pField, "AveragePlayTime")==0)
				fValue = UGCProject_AverageDurationInMinutes(pProject);
			else
			{
				PERFINFO_AUTO_STOP();	
				return false;
			}
			if(!RunNumberFilter(pFilter, fValue))
			{
				PERFINFO_AUTO_STOP();	
				return false;
			}
			break;

		case UGCFILTER_PERMISSIONS:
			if(pSearchInfo->iAccessLevel>=4)		// Permissions are default true for accesslevel people
				continue;

			if (stricmp(pFilter->pField, "ReadPermission")==0)
				uValue = UGCProject_CheckUserPermission(pProject, pSearchInfo->iOwnerAccountID) ? 1 : 0;
			else
			{
				PERFINFO_AUTO_STOP();	
				return false;
			}
			if (!RunIntNumberFilter(pFilter, uValue))
			{
				PERFINFO_AUTO_STOP();	
				return false;
			}
			break;
		case UGCFILTER_TAGS:
			if (stricmp(pFilter->pField, "tags") == 0)
			{
				if (!RunTagsFilter(pFilter, pProject))
				{
					PERFINFO_AUTO_STOP();	
					return false;
				}
			}
			else
			{
				PERFINFO_AUTO_STOP();	
				return false;
			}
		}
		FOR_EACH_END
	}

	// Nothing failed, must be good
	PERFINFO_AUTO_STOP();	
	return true;
}

static bool RunSeriesFilters(UGCProjectSearchInfo *pSearchInfo, UGCProjectSeries *pProjectSeries)
{
	const UGCProjectSeriesVersion* pPublishedVersion = UGCProjectSeries_GetMostRecentPublishedVersion(pProjectSeries);
	const UGCProjectVersion* pFirstProject = NULL;

	if(!pPublishedVersion)
		return false;

	{
		//find the first project in this series:
		const UGCProjectSeriesNode* node = eaGet(&pPublishedVersion->eaChildNodes, 0);
		while(node && !node->iProjectID){
			node = eaGet(&node->eaChildNodes, 0);
		}
		if(node){
			pFirstProject = UGCProject_GetMostRecentPublishedVersion(GetProject(node->iProjectID));
		}
		if(!pFirstProject){
			return false;
		}
	}

	if (pSearchInfo->eLang != LANGUAGE_DEFAULT && pFirstProject->eLanguage != pSearchInfo->eLang)
		return false;

	//check location:
	if(pSearchInfo->pchLocation && pSearchInfo->pchLocation[0]){
		if (stricmp(pSearchInfo->pchLocation, pFirstProject->pLocation)){
			return false;
		}
	}

	if(pSearchInfo->ppFilters) {
		FOR_EACH_IN_EARRAY(pSearchInfo->ppFilters, UGCProjectSearchFilter, pFilter) {
			const char *pString;
			float fValue;
			switch(pFilter->eType)
			{
				case UGCFILTER_SIMPLESTRING:
					if (!strstri_safe( pPublishedVersion->strName, pFilter->pStrValue)
						&& !strstri_safe( pProjectSeries->strOwnerAccountName, pFilter->pStrValue)
						&& !strstri_safe( pPublishedVersion->strDescription, pFilter->pStrValue))
					{
						PERFINFO_AUTO_STOP();	
						return false;
					}
					break;

				case UGCFILTER_STRING:
					if(stricmp(pFilter->pField, "Name")==0) {
						pString = pPublishedVersion->strName;
					} else if(stricmp(pFilter->pField, "Author")==0) {
						pString = pProjectSeries->strOwnerAccountName;
					} else if (stricmp(pFilter->pField, "description") == 0) {
						pString = pPublishedVersion->strDescription;
					} else {
						PERFINFO_AUTO_STOP();	
						return false;
					}
					
					if(!RunStringFilter(pFilter, pString))
					{
						PERFINFO_AUTO_STOP();	
						return false;
					}
					break;

				case UGCFILTER_RATING:
					if(stricmp(pFilter->pField, "Rating")==0) {
						fValue = UGCProjectSeries_Rating(pProjectSeries);
					} else {
						PERFINFO_AUTO_STOP();	
						return false;
					}
					if(!RunNumberFilter(pFilter, fValue)) {
						PERFINFO_AUTO_STOP();	
						return false;
					}
					break;

				case UGCFILTER_AVERAGEPLAYTIME:
					if(stricmp(pFilter->pField, "AveragePlayTime")==0)
					{
						const UGCProjectSeriesVersion *pVersion = UGCProjectSeries_GetMostRecentPublishedVersion(pProjectSeries);
						fValue = 0.0f;
						if(pVersion)
						{
							int it;
							for(it = 0; it != eaSize(&pVersion->eaChildNodes); ++it)
							{
								const UGCProjectSeriesNode *node = pVersion->eaChildNodes[ it ];
								if(node->iProjectID)
								{
									UGCProject* project = GetProject(node->iProjectID);
									if(project)
										fValue += UGCProject_AverageDurationInMinutes(project);
								}
							}
						}
					}
					else
					{
						PERFINFO_AUTO_STOP();	
						return false;
					}
					if(!RunNumberFilter(pFilter, fValue))
					{
						PERFINFO_AUTO_STOP();	
						return false;
					}
					break;

				default:
					PERFINFO_AUTO_STOP();
					return false;
			}
		} FOR_EACH_END;
	}

	return true;
}

static bool RunContentFilters(UGCProjectSearchInfo* pSearchInfo, const UGCContentInfo* pInfo)
{
	if( pInfo->iUGCProjectID ) {
		return RunFilters( pSearchInfo, GetProject( pInfo->iUGCProjectID ));
	} else if( pInfo->iUGCProjectSeriesID ) {
		return RunSeriesFilters( pSearchInfo, GetProjectSeries( pInfo->iUGCProjectSeriesID ));
	} else {
		return true;
	}
}

UGCProjectSearchFilter *UGCSearch_FindPrimaryFilter(UGCProjectSearchInfo *pSearchInfo)
{
	int iCurBestNameLength = 0;
	int iCurBestNameIndex = -1;
	int iCurBestAuthorLength = 0;
	int iCurBestAuthorIndex = -1;
	int i;

	//if we have a description-string-contains then we always use it as primary filter
	for (i=0; i < eaSize(&pSearchInfo->ppFilters); i++)
	{
		UGCProjectSearchFilter *pFilter = pSearchInfo->ppFilters[i];
		if (stricmp_safe(pFilter->pField, "description") == 0 && pFilter->eComparison == UGCCOMPARISON_CONTAINS)
		{
			return (UGCProjectSearchFilter*)eaRemoveFast(&pSearchInfo->ppFilters, i);
		}
	}

	//then try to find the filter searching for the longest thing by name
	for (i=0; i < eaSize(&pSearchInfo->ppFilters); i++)
	{
		UGCProjectSearchFilter *pFilter = pSearchInfo->ppFilters[i];

		if (stricmp_safe(pFilter->pField, "name") == 0 && pFilter->eComparison != UGCCOMPARISON_NOTCONTAINS)
		{
			int iLen = (int)strlen(pFilter->pStrValue);
			if (iLen > iCurBestNameLength)
			{
				iCurBestNameLength = iLen;
				iCurBestNameIndex = i;
			}
		}
		else if (stricmp_safe(pFilter->pField, "Author") == 0 && pFilter->eComparison != UGCCOMPARISON_NOTCONTAINS)
		{
			int iLen = (int)strlen(pFilter->pStrValue);
			if (iLen > iCurBestAuthorLength)
			{
				iCurBestAuthorLength = iLen;
				iCurBestAuthorIndex = i;
			}
		}
	}

	if (iCurBestNameIndex != -1)
	{
		return (UGCProjectSearchFilter*)eaRemoveFast(&pSearchInfo->ppFilters, iCurBestNameIndex);
	}

	if (iCurBestAuthorIndex != -1)
	{
		return (UGCProjectSearchFilter*)eaRemoveFast(&pSearchInfo->ppFilters, iCurBestAuthorIndex);
	}

	return NULL;
}

bool UGCSearchCompareCB(UGCProject *pProject, UGCProjectSearchInfo *pSearchInfo)
{
	return RunFilters(pSearchInfo, pProject);
}

bool UGCSearchCompareCB_Int(U32 iID, UGCProjectSearchInfo *pSearchInfo)
{
	UGCProject *pProject = GetProject(iID);
	if (pProject)
	{
		return RunFilters(pSearchInfo, pProject);
	}
	return false;
}

bool aslSearchSimpleProjPreFilter(UGCProject *pProject, UGCProjectSearchInfo *pSearchInfo)
{
	if (!ProjectMeetsBasicCriteria(pSearchInfo, pProject))
	{
		return false;
	}

	if(!RunFilters(pSearchInfo, pProject))
	{
		return false;
	}

	if(gbShowUnpublishedProjectsForDebugging || UGCProject_GetMostRecentPublishedVersion(pProject))
	{
		return true;
	}

	return false;
}

bool aslSearchSimpleProjPreFilter_Int(ContainerID iID, UGCProjectSearchInfo *pSearchInfo)
{
	UGCProject *pProject = GetProject(iID);
	if (pProject)
	{
		return aslSearchSimpleProjPreFilter(pProject, pSearchInfo);
	}

	return false;
}

bool aslSearchSimpleSeriesPreFilter(UGCProjectSeries *pProjectSeries, UGCProjectSearchInfo *pSearchInfo)
{
	if(!RunSeriesFilters(pSearchInfo, pProjectSeries))
	{
		return false;
	}

	return true;
}

bool aslSearchSimpleSeriesPreFilter_Int(ContainerID iID, UGCProjectSearchInfo *pSearchInfo)
{
	UGCProjectSeries *pProjectSeries = GetProjectSeries(iID);
	if (pProjectSeries)
	{
		return aslSearchSimpleSeriesPreFilter(pProjectSeries, pSearchInfo);
	}

	return false;
}

SSSTreeSearchType SSSSearchTypeFromUGCComparison(UGCProjectSearchFilterComparison eComparison)
{
	switch (eComparison)
	{
	case UGCCOMPARISON_CONTAINS:
		return SSSTREE_SEARCH_SUBSTRINGS;
	case UGCCOMPARISON_BEGINSWITH:
		return SSSTREE_SEARCH_PREFIXES;
	case UGCCOMPARISON_ENDSWITH:
		return SSSTREE_SEARCH_SUFFIXES;
	}

	assert(0);
	return 0;
}

bool textSearchCB(U64 iFoundID, TextSearchContext *pContext)
{
	UGCProject *pProject = GetProject(iFoundID);

	if (!pProject)
	{
		//FIXME do something here... text search manager knows about a project that doesn't exist
		return true;
	}

	if (!RunFilters(pContext->pSearchInfo, pProject))
	{
		//doesn't match, keep searching
		return true;
	}

	// matches

	if (eaSize(pContext->pppFoundProjects) + eaSize(pContext->pppFoundSeries) > pContext->iMaxToReturn)
	{
		return false;
	}

	eaPush(pContext->pppFoundProjects, pProject);
	return true;
}

bool textSearchSeriesCB(U64 iFoundID, TextSearchContext *pContext)
{
	UGCProjectSeries *pProjectSeries = GetProjectSeries(iFoundID);

	if (!pProjectSeries)
	{
		//FIXME do something here... text search manager knows about a project that doesn't exist
		return true;
	}

	if (!RunSeriesFilters(pContext->pSearchInfo, pProjectSeries))
	{
		//doesn't match, keep searching
		return true;
	}

	// matches

	if (eaSize(pContext->pppFoundProjects) + eaSize(pContext->pppFoundSeries) > pContext->iMaxToReturn)
	{
		return false;
	}

	eaPush(pContext->pppFoundSeries, pProjectSeries);
	return true;
}

static int SortProjectsByRating(const UGCProject** ppProj1, const UGCProject** ppProj2)
{
	float f1 = UGCProject_RatingForSorting( *ppProj1 );
	float f2 = UGCProject_RatingForSorting( *ppProj2 );
	
	if (f1 > f2)
	{
		return -1;
	}

	if (f2 > f1)
	{
		return 1;
	}

	return 0;
}

static int SortProjectsByAdjustedRating(const UGCProject** ppProj1, const UGCProject** ppProj2)
{
	float f1 = UGCProject_RatingForSorting( *ppProj1 );
	float f2 = UGCProject_RatingForSorting( *ppProj2 );

	if (f1 > f2)
	{
		return -1;
	}

	if (f2 > f1)
	{
		return 1;
	}

	return 0;
}

static int SortContentByAdjustedRating(const UGCContentInfo **ppContent1, const UGCContentInfo **ppContent2)
{
	float f1 = (*ppContent1)->iUGCProjectID ? UGCProject_RatingForSorting(GetProject((*ppContent1)->iUGCProjectID)) : UGCProjectSeries_RatingForSorting(GetProjectSeries((*ppContent1)->iUGCProjectSeriesID));
	float f2 = (*ppContent2)->iUGCProjectID ? UGCProject_RatingForSorting(GetProject((*ppContent2)->iUGCProjectID)) : UGCProjectSeries_RatingForSorting(GetProjectSeries((*ppContent2)->iUGCProjectSeriesID));
	if(f1 > f2)
		return -1;
	else if(f2 > f1)
		return 1;
	return 0;
}

static void RemoveSequentialDuplicates(UGCSearchResult *pSearchResult)
{
	int it;
	for(it = eaSize(&pSearchResult->eaResults) - 2; it >= 0; --it)
	{
		if(pSearchResult->eaResults[it]->iUGCProjectID)
		{
			if(pSearchResult->eaResults[it]->iUGCProjectID == pSearchResult->eaResults[it+1]->iUGCProjectID)
				eaRemove(&pSearchResult->eaResults, it + 1);
		}
		else
		{
			if(pSearchResult->eaResults[it]->iUGCProjectSeriesID == pSearchResult->eaResults[it+1]->iUGCProjectSeriesID)
				eaRemove(&pSearchResult->eaResults, it + 1);
		}
	}
}

void FindUGCMapsWithPrimaryFilter(UGCSearchResult *pSearchResult, UGCProjectSearchFilter *pPrimaryFilter, UGCProjectSearchInfo *pSearchInfo)
{
	UGCProject **ppFoundProjects = NULL;
	UGCProjectSeries **ppFoundSeries = NULL;
	int i;

	PERFINFO_AUTO_START_FUNC();	

	if (stricmp(pPrimaryFilter->pField, "description") == 0)
	{
		TextSearchContext context = {0};
		context.iMaxToReturn = UGCPROJ_MAX_SEARCH_RETURNS_INTERNAL;
		context.pSearchInfo = pSearchInfo;
		context.pppFoundProjects = &ppFoundProjects;
		
		if (!aslTextSearch_Search(spUGCProjectTextSearchManager, pPrimaryFilter->pStrValue, textSearchCB, &context))
		{
			Errorf("Unable to search with string: %s", pPrimaryFilter->pStrValue);
			eaDestroy(&ppFoundProjects);					
		}

		if (pSearchResult->bCapped)
		{
			WARNING_NETOPS_ALERT("UGC_TOO_MANY_SEARCH_RESULTS", "A DESCRIPTION search for %s resulted in > %d results internally. Some desired results may have been missed, and lots of CPU was wasted",
				pPrimaryFilter->pStrValue, UGCPROJ_MAX_SEARCH_RETURNS_INTERNAL);
		}
	}
	else if (stricmp(pPrimaryFilter->pField, "name") == 0)
	{
		U32 *pFoundIDs = NULL;

		pSearchResult->bCapped = SSSTreeInt_FindElementsWithRestrictions(spUGCProjectSSSTree, SSSSearchTypeFromUGCComparison(pPrimaryFilter->eComparison),
			pPrimaryFilter->pStrValue, &pFoundIDs, UGCPROJ_MAX_SEARCH_RETURNS_INTERNAL - eaSize(&ppFoundProjects), UGCSearchCompareCB_Int, pSearchInfo);

		if (pSearchResult->bCapped)
		{
			WARNING_NETOPS_ALERT("UGC_TOO_MANY_SEARCH_RESULTS", "A NAME search for %s resulted in > %d results internally. Some desired results may have been missed, and lots of CPU was wasted",
				pPrimaryFilter->pStrValue, UGCPROJ_MAX_SEARCH_RETURNS_INTERNAL);
		}
		
		for (i=0; i < ea32Size(&pFoundIDs); i++)
		{
			UGCProject *pProject = GetProject(pFoundIDs[i]);
			if (pProject)
			{
				eaPush(&ppFoundProjects, pProject);
			}
		}

		ea32Destroy(&pFoundIDs);
	}
	else if (stricmp(pPrimaryFilter->pField, "author") == 0)
	{
		UGCContentOwnerInfo **ppOwnerInfos = NULL;
		int iAuthorNum;
		int iContNum;

		if(!s_UGCContentOwnerSSSTree)
		{
			PERFINFO_AUTO_STOP();	
			return;
		}

		SSSTree_FindElementsWithRestrictions(s_UGCContentOwnerSSSTree, SSSSearchTypeFromUGCComparison(pPrimaryFilter->eComparison),
			pPrimaryFilter->pStrValue, &ppOwnerInfos, 0, NULL, NULL);

		for (iAuthorNum = 0; iAuthorNum < eaSize(&ppOwnerInfos); iAuthorNum++)
		{
			for (iContNum = 0; iContNum < ea32Size(&ppOwnerInfos[iAuthorNum]->pProjectIDs); iContNum++)
			{
				UGCProject *pProject = GetProject(ppOwnerInfos[iAuthorNum]->pProjectIDs[iContNum]);
				if(pProject)
				{
					if(RunFilters(pSearchInfo, pProject))
					{
						if(eaSize(&ppFoundProjects) + eaSize(&ppFoundSeries) == UGCPROJ_MAX_SEARCH_RETURNS_INTERNAL)
						{
							pSearchResult->bCapped = true;
							break;
						}

						eaPush(&ppFoundProjects, pProject);
					}
				}
			}

			for (iContNum = 0; iContNum < ea32Size(&ppOwnerInfos[iAuthorNum]->pProjectSeriesIDs); iContNum++)
			{
				UGCProjectSeries *pSeries = GetProjectSeries(ppOwnerInfos[iAuthorNum]->pProjectSeriesIDs[iContNum]);
				if(pSeries)
				{
					if(RunSeriesFilters(pSearchInfo, pSeries))
					{
						if(eaSize(&ppFoundProjects) + eaSize(&ppFoundSeries) == UGCPROJ_MAX_SEARCH_RETURNS_INTERNAL)
						{
							pSearchResult->bCapped = true;
							break;
						}

						eaPush(&ppFoundSeries, pSeries);
					}
				}
			}

			if(pSearchResult->bCapped)
				break;
		}
	}

	for(i=0; i < eaSize(&ppFoundProjects); i++)
	{
		UGCContentInfo* pNode = StructCreate(parse_UGCContentInfo);
		pNode->iUGCProjectID = ppFoundProjects[i]->id;
		eaPush(&pSearchResult->eaResults, pNode);
	}
	for(i=0; i < eaSize(&ppFoundSeries); i++)
	{
		UGCContentInfo* pNode = StructCreate(parse_UGCContentInfo);
		pNode->iUGCProjectSeriesID = ppFoundSeries[i]->id;
		eaPush(&pSearchResult->eaResults, pNode);
	}

	eaDestroy(&ppFoundProjects);
	eaDestroy(&ppFoundSeries);

	eaQSort(pSearchResult->eaResults, SortContentByAdjustedRating);
	RemoveSequentialDuplicates(pSearchResult);

	pSearchResult->bCapped = (eaSize(&ppFoundProjects) + eaSize(&ppFoundSeries) > UGCPROJ_MAX_SEARCH_RETURNS);

	PERFINFO_AUTO_STOP();
}

typedef struct SortedByRating_SearchContext
{
	UGCSearchResult *pSearchResult;
	UGCProjectSearchInfo *pSearchInfo;
} SortedByRating_SearchContext;

static int SortByRatings_Iterate_ProjectCB(GlobalType nodeType, UGCProject *pProject, U32 iKey, SortedByRating_SearchContext *pContext)
{
	PERFINFO_AUTO_START_FUNC();	

	devassert(GLOBALTYPE_UGCPROJECT == nodeType);

	if(!pContext->pSearchInfo->bUGCProjects)
		return 0;

	if(RunFilters(pContext->pSearchInfo, pProject))
	{
		if(eaSize(&pContext->pSearchResult->eaResults) >= UGCPROJ_MAX_SEARCH_RETURNS)
		{
			pContext->pSearchResult->bCapped = true;
			PERFINFO_AUTO_STOP();	
			return 1;
		}

		{
			UGCContentInfo* pNode = StructCreate(parse_UGCContentInfo);
			pNode->iUGCProjectID = pProject->id;
			eaPush(&pContext->pSearchResult->eaResults, pNode);
		}
	}
	
	PERFINFO_AUTO_STOP();

	return 0;
}

static int SortByRatings_Iterate_SeriesCB(GlobalType nodeType, UGCProjectSeries *pSeries, U32 iKey, SortedByRating_SearchContext *pContext)
{
	PERFINFO_AUTO_START_FUNC();	

	devassert(GLOBALTYPE_UGCPROJECTSERIES == nodeType);

	if(!pContext->pSearchInfo->bUGCSeries)
		return 0;

	if(RunSeriesFilters(pContext->pSearchInfo, pSeries))
	{
		if(eaSize(&pContext->pSearchResult->eaResults) >= UGCPROJ_MAX_SEARCH_RETURNS)
		{
			pContext->pSearchResult->bCapped = true;
			PERFINFO_AUTO_STOP();	
			return 1;
		}

		{
			UGCContentInfo* pNode = StructCreate(parse_UGCContentInfo);
			pNode->iUGCProjectSeriesID = pSeries->id;
			eaPush(&pContext->pSearchResult->eaResults, pNode);
		}
	}
	
	PERFINFO_AUTO_STOP();

	return 0;
}

static int SortByRatings_Iterate_CB(GlobalType nodeType, void *pVal, U32 iKey, SortedByRating_SearchContext *pContext)
{
	switch(nodeType)
	{
		case GLOBALTYPE_UGCPROJECT:
			return SortByRatings_Iterate_ProjectCB(GLOBALTYPE_UGCPROJECT, (UGCProject *)pVal, iKey, pContext);
		case GLOBALTYPE_UGCPROJECTSERIES:
			return SortByRatings_Iterate_SeriesCB(GLOBALTYPE_UGCPROJECTSERIES, (UGCProjectSeries *)pVal, iKey, pContext);
		default:
			FatalErrorf("Unexpected type");
	}

	return 0;
}

void FindUGCMapsSortedByRating(UGCSearchResult *pSearchResult, UGCProjectSearchInfo *pSearchInfo)
{
	SortedByRating_SearchContext localContext = { pSearchResult, pSearchInfo };

	PERFINFO_AUTO_START_FUNC();	

	SortByRatings_Iterate(2.0f, 0.0f, SortByRatings_Iterate_CB, &localContext);

	PERFINFO_AUTO_STOP();
}

S32 UGCSearch_GetTotalSearchResultCount(const UGCContentInfo *const *const eaResults,
										const UGCProject *const *const eaUGCProjects,
										const UGCProjectSeries *const *const eaUGCSeries,
										const GameProgressionNodeRef *const *const eaProgressionNodes,
										const QueueDefRef *const *const eaQueues)
{
	return (eaSize(&eaResults) + eaSize(&eaUGCProjects) + eaSize(&eaUGCSeries)
			+ eaSize(&eaProgressionNodes) + eaSize(&eaQueues));
}

bool UGCSearch_ShouldEarlyOut(const UGCContentInfo *const *const eaResults,
							  const UGCProject *const *const eaUGCProjects,
							  const UGCProjectSeries *const *const eaUGCSeries,
							  const GameProgressionNodeRef *const *const eaProgressionNodes,
							  const QueueDefRef *const *const eaQueues)
{
	return (UGCSearch_GetTotalSearchResultCount(eaResults, eaUGCProjects, eaUGCSeries, eaProgressionNodes, eaQueues)
			> UGCPROJ_MAX_SEARCH_RETURNS_INTERNAL);
}

void FindUGCMapsSimple(UGCSearchResult *pSearchResult, const char *pQuery_SSSTree, const char *pQuery_Raw, UGCProjectSearchInfo *pSearchInfo)
{
	int i, j;
	UGCProject **ppFoundProjects = NULL;
	UGCProjectSeries **ppFoundSeries = NULL;
	U32 iIDFromIDString;
	bool bIsSeriesFromIDString;

	PERFINFO_AUTO_START_FUNC();	

	//first, see if this is an ID string (looks like STO-H12345678 or whatever)
	if (UGCIDString_StringToInt(pQuery_SSSTree, &iIDFromIDString, &bIsSeriesFromIDString) && !UGCSearch_ShouldEarlyOut(pSearchResult->eaResults, ppFoundProjects, ppFoundSeries, NULL, NULL))
	{
		if( !bIsSeriesFromIDString ) {
			UGCProject *pProject = GetProject(iIDFromIDString);
		
			if (pProject)
			{
				pSearchInfo->bIsIDString = true;
				if (RunFilters(pSearchInfo, pProject))
				{
					eaPush(&ppFoundProjects, pProject);
				}
				pSearchInfo->bIsIDString = false;
			}
		} else {
			UGCProjectSeries *pProjectSeries = GetProjectSeries(iIDFromIDString);

			if (pProjectSeries)
			{
				pSearchInfo->bIsIDString = true;
				if (RunSeriesFilters(pSearchInfo, pProjectSeries))
				{
					eaPush(&ppFoundSeries, pProjectSeries);
				}
				pSearchInfo->bIsIDString = false;
			}
		}
	}
	else
	{
		//first try to find a project with this name (substring)
		if (strlen(pQuery_SSSTree) >= UGCPROJ_MIN_NAME_SEARCH_STRING_LEN && !UGCSearch_ShouldEarlyOut(pSearchResult->eaResults, ppFoundProjects, ppFoundSeries, NULL, NULL))
		{
			U32 *pFoundIDs = NULL;
			if(pSearchInfo->bUGCProjects){
				SSSTreeInt_FindElementsWithRestrictions(spUGCProjectSSSTree, SSSTREE_SEARCH_SUBSTRINGS, pQuery_SSSTree, &pFoundIDs, UGCPROJ_MAX_SEARCH_RETURNS_INTERNAL, aslSearchSimpleProjPreFilter_Int, pSearchInfo);	
				for (i=0; i < ea32Size(&pFoundIDs); i++)
				{
					UGCProject *pProject = GetProject(pFoundIDs[i]);
					if (pProject)
					{
						eaPush(&ppFoundProjects, pProject);
					}
				}
				ea32Destroy(&pFoundIDs);
			}
			if(pSearchInfo->bUGCSeries){
				SSSTreeInt_FindElementsWithRestrictions(spUGCProjectSeriesSSSTree, SSSTREE_SEARCH_SUBSTRINGS, pQuery_SSSTree, &pFoundIDs, UGCPROJ_MAX_SEARCH_RETURNS_INTERNAL, aslSearchSimpleSeriesPreFilter_Int, pSearchInfo);
				for (i=0; i < ea32Size(&pFoundIDs); i++)
				{
					UGCProjectSeries *pProjectSeries = GetProjectSeries(pFoundIDs[i]);
					if (pProjectSeries)
					{
						eaPush(&ppFoundSeries, pProjectSeries);
					}
				}
				ea32Destroy(&pFoundIDs);
			}
		}

		//also try to find an author with precisely this name
		if (s_UGCContentOwnerSSSTree && !UGCSearch_ShouldEarlyOut(pSearchResult->eaResults, ppFoundProjects, ppFoundSeries, NULL, NULL))
		{
			UGCContentOwnerInfo **ppOwnerInfos = NULL;

			SSSTree_FindElementsWithRestrictions(s_UGCContentOwnerSSSTree, SSSTREE_SEARCH_PRECISE_STRING, pQuery_SSSTree, &ppOwnerInfos, UGCPROJ_MAX_SEARCH_RETURNS_INTERNAL, NULL, NULL);
			if(pSearchInfo->bUGCProjects){
				for (i=0; i < eaSize(&ppOwnerInfos) && !UGCSearch_ShouldEarlyOut(pSearchResult->eaResults, ppFoundProjects, ppFoundSeries, NULL, NULL); i++)
				{
					UGCContentOwnerInfo *pOwnerInfo = ppOwnerInfos[i];
					for (j = 0; j < ea32Size(&pOwnerInfo->pProjectSeriesIDs) && !UGCSearch_ShouldEarlyOut(pSearchResult->eaResults, ppFoundProjects, ppFoundSeries, NULL, NULL); j++)
					{
						UGCProjectSeries *pProjectSeries = GetProjectSeries(pOwnerInfo->pProjectSeriesIDs[j]);
						if (pProjectSeries)
						{
							if (aslSearchSimpleSeriesPreFilter(pProjectSeries, pSearchInfo))
							{
								eaPush(&ppFoundSeries, pProjectSeries);
							}
						}
					}
				}
			}
			if(pSearchInfo->bUGCSeries){
				for (i=0; i < eaSize(&ppOwnerInfos) && !UGCSearch_ShouldEarlyOut(pSearchResult->eaResults, ppFoundProjects, ppFoundSeries, NULL, NULL); i++)
				{
					UGCContentOwnerInfo *pOwnerInfo = ppOwnerInfos[i];
					for (j = 0; j < ea32Size(&pOwnerInfo->pProjectIDs) && !UGCSearch_ShouldEarlyOut(pSearchResult->eaResults, ppFoundProjects, ppFoundSeries, NULL, NULL); j++)
					{
						UGCProject *pProject = GetProject(pOwnerInfo->pProjectIDs[j]);
						if (pProject)
						{
							if (aslSearchSimpleProjPreFilter(pProject, pSearchInfo))
							{
								eaPush(&ppFoundProjects, pProject);
							}
						}
					}
				}
			}
		}

		//Text search
		if (strlen(pQuery_Raw) >= UGCPROJ_MIN_DESCRIPTION_SEARCH_STRING_LEN && !UGCSearch_ShouldEarlyOut(pSearchResult->eaResults, ppFoundProjects, ppFoundSeries, NULL, NULL))
		{
			TextSearchContext context = {0};
			context.iMaxToReturn = UGCPROJ_MAX_SEARCH_RETURNS_INTERNAL;
			context.pSearchInfo = pSearchInfo;
			context.pppFoundProjects = &ppFoundProjects;
			context.pppFoundSeries = &ppFoundSeries;

			if (pSearchInfo->bUGCProjects && !aslTextSearch_Search(spUGCProjectTextSearchManager, pQuery_Raw, textSearchCB, &context))
			{
				Errorf("Unable to search with string: %s", pQuery_Raw);
				eaDestroy(&ppFoundProjects);
				eaDestroy(&ppFoundSeries);
			}
			if (pSearchInfo->bUGCSeries && !aslTextSearch_Search(spUGCProjectSeriesTextSearchManager, pQuery_Raw, textSearchSeriesCB, &context))
			{
				Errorf("Unable to search with string: %s", pQuery_Raw);
				eaDestroy(&ppFoundProjects);
				eaDestroy(&ppFoundSeries);
			}
		}

		if (UGCSearch_GetTotalSearchResultCount(pSearchResult->eaResults, ppFoundProjects, ppFoundSeries, NULL, NULL) > UGCPROJ_MAX_SEARCH_RETURNS_INTERNAL)
		{
			WARNING_NETOPS_ALERT("UGC_TOO_MANY_SEARCH_RESULTS", "A SIMPLE search for %s resulted in > %d results internally. Some desired results may have been missed, and lots of CPU was wasted",
								 pQuery_Raw, UGCPROJ_MAX_SEARCH_RETURNS_INTERNAL);
		}
	}

	FOR_EACH_IN_EARRAY_FORWARDS(ppFoundSeries, UGCProjectSeries, series) // forwards
	{
		UGCContentInfo* pNode = StructCreate( parse_UGCContentInfo );
		pNode->iUGCProjectSeriesID = series->id;
		eaPush(&pSearchResult->eaResults, pNode);
		if(eaSize(&pSearchResult->eaResults) >= UGCPROJ_MAX_SEARCH_RETURNS)
			break;
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS(ppFoundProjects, UGCProject, project) // also forwards
	{
		UGCContentInfo* pNode = StructCreate( parse_UGCContentInfo );
		pNode->iUGCProjectID = project->id;
		eaPush(&pSearchResult->eaResults, pNode);
		if(eaSize(&pSearchResult->eaResults) >= UGCPROJ_MAX_SEARCH_RETURNS)
			break;
	}
	FOR_EACH_END;

	eaDestroy(&ppFoundProjects);
	eaDestroy(&ppFoundSeries);

	eaQSort(pSearchResult->eaResults, SortContentByAdjustedRating);
	RemoveSequentialDuplicates(pSearchResult);

	pSearchResult->bCapped = UGCSearch_GetTotalSearchResultCount(pSearchResult->eaResults, ppFoundProjects, ppFoundSeries, NULL, NULL) > UGCPROJ_MAX_SEARCH_RETURNS;

	PERFINFO_AUTO_STOP();	
}

void DoWhatsHotSearch(UGCProjectSearchInfo *pSearchInfo, UGCSearchResult *pSearchResult)
{
	int i;

	PERFINFO_AUTO_START_FUNC();	

	for (i=0; i < ea32Size(&sWhatsHot.pProjectIDs); i++)
	{
		UGCProject *pProject = GetProject( sWhatsHot.pProjectIDs[i]);

		if (pProject && RunFilters(pSearchInfo, pProject))
		{
			UGCContentInfo* pNode = StructCreate( parse_UGCContentInfo );
			pNode->iUGCProjectID = pProject->id;
			eaPush(&pSearchResult->eaResults, pNode);
		}
	}

	PERFINFO_AUTO_STOP();	

}

void DoReviewerSearch(UGCProjectSearchInfo *pSearchInfo, UGCSearchResult *pSearchResult)
{
	StashTableIterator stashIterator;
	StashElement element;

	PERFINFO_AUTO_START_FUNC();	


	stashGetIterator(sReviewerProjectsByID, &stashIterator);

	while (stashGetNextElement(&stashIterator, &element))
	{
		U32 iContainerID = stashElementGetIntKey(element);
		UGCProject *pProject = GetProject(iContainerID);
		if (pProject)
		{
			if (RunFilters(pSearchInfo, pProject))
			{
				UGCContentInfo* pNode = StructCreate( parse_UGCContentInfo );
				pNode->iUGCProjectID = pProject->id;
				eaPush(&pSearchResult->eaResults, pNode);
			}
		}
	}

	PERFINFO_AUTO_STOP();	

}


void DoNewProjectSearch(UGCProjectSearchInfo *pSearchInfo, UGCSearchResult *pSearchResult)
{
	StashTableIterator stashIterator;
	StashElement element;
	UGCProject **eaProjects = NULL;
	UGCProjectSeries **eaSeries = NULL;

	PERFINFO_AUTO_START_FUNC();	

	stashGetIterator(sNewProjectsByID, &stashIterator);
	while(stashGetNextElement(&stashIterator, &element))
	{
		U32 iContainerID = stashElementGetIntKey(element);
		UGCProject *pProject = GetProject(iContainerID);
		if(pProject && ContentIsNew(&pProject->ugcReviews))
		{
			if(RunFilters(pSearchInfo, pProject))
				eaPush(&eaProjects, pProject);
		}
		else
			stashIntRemovePointer(sNewProjectsByID, iContainerID, NULL);
	}

	stashGetIterator(sNewSeriesByID, &stashIterator);
	while(stashGetNextElement(&stashIterator, &element))
	{
		U32 iContainerID = stashElementGetIntKey(element);
		UGCProjectSeries *pSeries = GetProjectSeries(iContainerID);
		if(pSeries && ContentIsNew(&pSeries->ugcReviews))
		{
			if(RunSeriesFilters(pSearchInfo, pSeries))
				eaPush(&eaSeries, pSeries);
		}
		else
			stashIntRemovePointer(sNewSeriesByID, iContainerID, NULL);
	}

	{
		int it;
		for(it = 0; it != eaSize(&eaProjects); it++)
		{
			UGCContentInfo* pNode = StructCreate(parse_UGCContentInfo);
			pNode->iUGCProjectID = eaProjects[it]->id;
			eaPush(&pSearchResult->eaResults, pNode);
		}
	}
	{
		int it;
		for(it = 0; it != eaSize(&eaSeries); it++)
		{
			UGCContentInfo* pNode = StructCreate(parse_UGCContentInfo);
			pNode->iUGCProjectSeriesID = eaSeries[it]->id;
			eaPush(&pSearchResult->eaResults, pNode);
		}
	}
	eaDestroy(&eaProjects);
	eaDestroy(&eaSeries);

	eaQSort(pSearchResult->eaResults, SortContentByAdjustedRating);

	PERFINFO_AUTO_STOP();	
}

static void DoFeaturedSearch(UGCProjectSearchInfo *pSearchInfo, UGCSearchResult *pSearchResult)
{
	U32 curTime = timeSecondsSince2000();
	
	FOR_EACH_IN_CONST_EARRAY_FORWARDS( g_eaFeaturedListSortedByTime, UGCContentInfo, pFeaturedContent ) {
		if( RunContentFilters( pSearchInfo, pFeaturedContent )) {
			UGCFeaturedData* pFeatured = UGCFeatured_ContentFeaturedData( pFeaturedContent );
			if(   pFeatured && pFeatured->iStartTimestamp <= curTime
				  && (pSearchInfo->bFeaturedIncludeArchives || pFeatured->iEndTimestamp == 0 || curTime <= pFeatured->iEndTimestamp) ) {
				eaPush( &pSearchResult->eaResults, StructClone( parse_UGCContentInfo, pFeaturedContent ));
			}
		}
	} FOR_EACH_END;
}

// This is for Featured and Matching searches. 
static void DoFeaturedSearchNotInFilters(UGCProjectSearchInfo *pSearchInfo, UGCSearchResult *pSearchResult)
{
	U32 curTime = timeSecondsSince2000();

	FOR_EACH_IN_CONST_EARRAY_FORWARDS( g_eaFeaturedListSortedByTime, UGCContentInfo, pFeaturedContent ) {
		// NOTE THE !.  This is to prevent duplication with the matching search:
		if( ! RunContentFilters( pSearchInfo, pFeaturedContent )) {
			UGCFeaturedData* pFeatured = UGCFeatured_ContentFeaturedData( pFeaturedContent );
			if(   pFeatured && pFeatured->iStartTimestamp <= curTime
				&& (pSearchInfo->bFeaturedIncludeArchives || pFeatured->iEndTimestamp == 0 || curTime <= pFeatured->iEndTimestamp) ) {
					eaPush( &pSearchResult->eaResults, StructClone( parse_UGCContentInfo, pFeaturedContent ));
			}
		}
	} FOR_EACH_END;
}

static int SortProjectsByDate( const UGCProject** ppProject1, const UGCProject** ppProject2 )
{
	const UGCProjectVersion* pVersion1 = UGCProject_GetMostRecentPublishedVersion( *ppProject1 );
	const UGCProjectVersion* pVersion2 = UGCProject_GetMostRecentPublishedVersion( *ppProject2 );
	U32 timestamp1 = SAFE_MEMBER( pVersion1, sLastPublishTimeStamp.iTimestamp );
	U32 timestamp2 = SAFE_MEMBER( pVersion2, sLastPublishTimeStamp.iTimestamp );
	
	return timestamp1 - timestamp2;
}
static int SortProjectSeriesByDate( const UGCProjectSeries** ppSeries1, const UGCProjectSeries** ppSeries2 )
{
	return (*ppSeries1)->iLastUpdatedTime - (*ppSeries2)->iLastUpdatedTime;
}

static void DoSubscribedSearch(UGCProjectSearchInfo* pSearchInfo, UGCSearchResult* pSearchResult)
{
	UGCProject** eaProjects = NULL;
	UGCProjectSeries** eaProjectSeries = NULL;

	if( pSearchInfo->pSubscription ) {
		int it;
		for( it = 0; it != eaiSize( &pSearchInfo->pSubscription->eaiAuthors ); ++it ) {
			UGCContentOwnerInfo* pOwner = NULL;
			stashIntFindPointer( s_UGCContentOwnerByID, pSearchInfo->pSubscription->eaiAuthors[ it ], &pOwner );
			
			if( pOwner ) {
				int projIt;
				for( projIt = 0; projIt != eaiSize( &pOwner->pProjectIDs ); ++projIt ) {
					UGCProject* project = GetProject( pOwner->pProjectIDs[ projIt ]);
					// The project must be in a series that has the
					// same owner, so it will be found by the next for
					// loop.
					if( project && !project->seriesID ) {
						eaPush( &eaProjects, project );
					}
				}
				for( projIt = 0; projIt != eaiSize( &pOwner->pProjectSeriesIDs ); ++projIt ) {
					UGCProjectSeries* series = GetProjectSeries( pOwner->pProjectSeriesIDs[ projIt ]);
					if( series ) {
						eaPush( &eaProjectSeries, series );
					}
				}
			}
		}
	}
	eaQSort( eaProjects, SortProjectsByDate );
	eaQSort( eaProjectSeries, SortProjectSeriesByDate );
	
	{
		int it;
		for( it = 0; it != eaSize( &eaProjectSeries ); ++it ) {
			UGCContentInfo* pNode = StructCreate( parse_UGCContentInfo );
			pNode->iUGCProjectSeriesID = eaProjectSeries[it]->id;
			eaPush( &pSearchResult->eaResults, pNode );
		}
		for( it = 0; it != eaSize( &eaProjects ); ++it ) {
			UGCContentInfo* pNode = StructCreate( parse_UGCContentInfo );
			pNode->iUGCProjectID = eaProjects[it]->id;
			eaPush( &pSearchResult->eaResults, pNode );
		}
	}
	eaDestroy( &eaProjects );
	eaDestroy( &eaProjectSeries );	
}

void MaybeConvertSimpleSearchIntoFilters(UGCProjectSearchInfo *pSearchInfo)
{
	char **ppWords = NULL;
	int i;
	char *pSSSConverted = NULL;

	if (!(pSearchInfo->pSimple_Raw && pSearchInfo->pSimple_Raw[0]))
	{
		return;
	}

	if (pSearchInfo->eSpecialType == SPECIALSEARCH_NONE)
	{
		return;
	}

	DivideString(pSearchInfo->pSimple_Raw, " ,;", &ppWords, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE|DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS|
				DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);

	estrCreate(&pSSSConverted);
	for (i = 0; i < eaSize(&ppWords); i++)
	{
		estrClear(&pSSSConverted);
		SSSTree_InternalizeString(&pSSSConverted, ppWords[i]);
		if (estrLength(&pSSSConverted) >= UGCPROJ_MIN_SIMPLE_SEARCH_STRING_LEN && estrLength(&pSSSConverted) <= UGCPROJ_MAX_SIMPLE_SEARCH_STRING_LEN)
		{
			UGCProjectSearchFilter *pNewFilter = StructCreate(parse_UGCProjectSearchFilter);
			pNewFilter->eComparison = UGCCOMPARISON_CONTAINS;
			pNewFilter->eType = UGCFILTER_SIMPLESTRING;
			pNewFilter->pStrValue = pSSSConverted;
			pSSSConverted = NULL;

			eaPush(&pSearchInfo->ppFilters, pNewFilter);
		}
	}


	estrDestroy(&pSSSConverted);
	SAFE_FREE(pSearchInfo->pSimple_Raw);
	SAFE_FREE(pSearchInfo->pSimple_SSSTree);
}

AUTO_ENUM;
typedef enum eSearchType
{
	SEARCH_UNKNOWN,
	SEARCH_SIMPLE,
	SEARCH_W_PRIMARY_FILTER,
	SEARCH_SORTED_BY_RATINGS,
	SEARCH_WHATSHOT,
	SEARCH_REVIEWER,
	SEARCH_NEW,
	SEARCH_BROWSE,
	SEARCH_FEATURED,
	SEARCH_SUBSCRIBED,
	SEARCH_FEATURED_AND_FILTER,

	SEARCH_LAST,
} eSearchType;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Name, Count, TotalMsecs, AverageMsecs, AverageNumReturned");
typedef struct SearchTypeReport
{
	char *pName;
	int iCount;
	U64 iTotalMicroSecs;
	U64 iTotalMsecs; 
	int iAverageMsecs;
	U64 iTotalReturns;
	int iAverageNumReturned;
} SearchTypeReport;

static SearchTypeReport sSearchTypeReports[SEARCH_LAST] = {0};
static StashTable hSearchTypeReports = NULL;

static int siTicksToMicrosecs = 0;

AUTO_RUN;
void InitSearchTypeReports(void)
{
	int i;

	hSearchTypeReports = stashTableCreateWithStringKeys(SEARCH_LAST, StashDefault);

	for (i=0; i < SEARCH_LAST; i++)
	{
		sSearchTypeReports[i].pName = strdup(StaticDefineIntRevLookup(eSearchTypeEnum, i));
		stashAddPointer(hSearchTypeReports, sSearchTypeReports[i].pName, &sSearchTypeReports[i], false);
	}

	resRegisterDictionaryForStashTable("SearchReports", RESCATEGORY_OTHER, 0, hSearchTypeReports, parse_SearchTypeReport);

	siTicksToMicrosecs = timerCpuSpeed64() / 1000000;
}

static eSearchType ChooseNormalSearch(UGCProjectSearchInfo* pSearchInfo, UGCSearchResult* pSearchResult)
{
	UGCProjectSearchFilter *pPrimaryFilter;

	if(pSearchInfo->pSimple_Raw && pSearchInfo->pSimple_Raw[0])
	{
		pSearchResult->eType = SPECIALSEARCH_NONE;
		if(pSearchInfo->bUGCProjects || pSearchInfo->bUGCSeries) // Find the UGC maps
			FindUGCMapsSimple(pSearchResult, pSearchInfo->pSimple_SSSTree, pSearchInfo->pSimple_Raw, pSearchInfo);
		return SEARCH_SIMPLE;
	}
	else if (eaSize(&pSearchInfo->ppFilters) <= 0 && pSearchInfo->iPlayerLevelMin == 0 && pSearchInfo->iPlayerLevelMax == 0)
	{
		pSearchResult->eType = SPECIALSEARCH_NONE;
		if(pSearchInfo->bUGCProjects || pSearchInfo->bUGCSeries) // Find the UGC maps
			FindUGCMapsSortedByRating(pSearchResult, pSearchInfo);
		return SEARCH_BROWSE;
	}
	else
	{
		pSearchResult->eType = SPECIALSEARCH_NONE;
		if(pSearchInfo->bUGCProjects || pSearchInfo->bUGCSeries) // Find the UGC maps
		{
			pPrimaryFilter = UGCSearch_FindPrimaryFilter(pSearchInfo);
			if(pPrimaryFilter)
			{
				FindUGCMapsWithPrimaryFilter(pSearchResult, pPrimaryFilter, pSearchInfo);
				//this doesn't seem to look for series?
				StructDestroy(parse_UGCProjectSearchFilter, pPrimaryFilter);
				return SEARCH_W_PRIMARY_FILTER;
			}
			else
			{
				FindUGCMapsSortedByRating(pSearchResult, pSearchInfo);
				return SEARCH_SORTED_BY_RATINGS;
			}
		}
	}
	return SEARCH_UNKNOWN;
}

UGCSearchResult *FindUGCMaps_Internal(UGCProjectSearchInfo *pSearchInfo)
{
	UGCSearchResult *pSearchResult = StructCreate(parse_UGCSearchResult);
	const char* astrErrorMessageKey = NULL;

	eSearchType eType = SEARCH_UNKNOWN;
	U64 iStartTime;
	int i,j;

	PERFINFO_AUTO_START_FUNC();	

	if (giDisableUGCSearchForAL0 && pSearchInfo->iAccessLevel == 0)
	{
		PERFINFO_AUTO_STOP();	
		return pSearchResult;
	}

	iStartTime = timerCpuTicks64();

	MaybeConvertSimpleSearchIntoFilters(pSearchInfo);

	if (!UGCProject_ValidateAndFixupSearchInfo(pSearchInfo, &astrErrorMessageKey))
	{
		SET_HANDLE_FROM_STRING("Message", astrErrorMessageKey, pSearchResult->hErrorMessage);
		PERFINFO_AUTO_STOP();	
		return pSearchResult;
	}
	assert( !astrErrorMessageKey );

	switch (pSearchInfo->eSpecialType)
	{
		case SPECIALSEARCH_NONE:
		{
			eType = ChooseNormalSearch(pSearchInfo, pSearchResult);
		}
		break;

		case SPECIALSEARCH_BROWSE:
		{
			eType = SEARCH_BROWSE;
			pSearchResult->eType = SPECIALSEARCH_BROWSE;

			if(pSearchInfo->bUGCProjects || pSearchInfo->bUGCSeries) // Find the UGC maps
				FindUGCMapsSortedByRating(pSearchResult, pSearchInfo);
		}
		break;

		case SPECIALSEARCH_WHATSHOT:
		{
			eType = SEARCH_WHATSHOT;
			DoWhatsHotSearch(pSearchInfo, pSearchResult);
		}
		break;

		case SPECIALSEARCH_REVIEWER:
		{
			eType = SEARCH_REVIEWER;
			if(pSearchInfo->bIsReviewer)
				DoReviewerSearch(pSearchInfo, pSearchResult);
		}
		break;

		case SPECIALSEARCH_NEW:
		{
			eType = SEARCH_NEW;
			DoNewProjectSearch(pSearchInfo, pSearchResult);
		}
		break;

		case SPECIALSEARCH_FEATURED:
		{
			eType = SEARCH_FEATURED;
			pSearchResult->eType = SPECIALSEARCH_FEATURED;
			DoFeaturedSearch(pSearchInfo, pSearchResult);
		}
		break;

		case SPECIALSEARCH_FEATURED_AND_MATCHING:
		{
			eType = SEARCH_FEATURED_AND_FILTER;
			DoFeaturedSearchNotInFilters(pSearchInfo, pSearchResult);
			ChooseNormalSearch(pSearchInfo, pSearchResult);
		}
		break;

		case SPECIALSEARCH_SUBCRIBED:
		{
			eType = SEARCH_SUBSCRIBED;
			pSearchResult->eType = SPECIALSEARCH_SUBCRIBED;
			DoSubscribedSearch( pSearchInfo, pSearchResult );
		}
		break;
	}

	// remove any included projects that are in included series:
	// it would be nice to do this earlier, but the different searches work in different ways
	// so it's cleaner to deal with all cases here.
	for (i=eaSize(&pSearchResult->eaResults)-1; i >= 0; i--)
	{
		UGCProject* project = NULL;
		if (!pSearchResult->eaResults[i]->iUGCProjectID){
			continue;	//iterate over only the projects in the outside loop.
		}
		project = GetProject(pSearchResult->eaResults[i]->iUGCProjectID);
		if (project && project->seriesID){
			for (j=eaSize(&pSearchResult->eaResults)-1; j >= 0; j--){
				if (!pSearchResult->eaResults[j]->iUGCProjectSeriesID){
					continue;	//iterate over only the series in the inside loop.
				}
				if (pSearchResult->eaResults[j]->iUGCProjectSeriesID == project->seriesID){
					eaRemove(&pSearchResult->eaResults, i);
					break;
				}
			}
		}
	}

	// Add series' projects to a separate, included list
	{
		int it;
		int projIt;
		for( it = 0; it != eaSize( &pSearchResult->eaResults ); ++it ) {
			if( pSearchResult->eaResults[ it ]->iUGCProjectSeriesID ) {
				UGCProjectSeries* series = GetProjectSeries( pSearchResult->eaResults[ it ]->iUGCProjectSeriesID );
				if( series ) {
					for( projIt = 0; projIt != ea32Size( &series->ugcSearchCache.eaPublishedProjectIDs ); ++projIt ) {
						ea32PushUnique( &pSearchResult->eaProjectSeriesProjectIDs,
							series->ugcSearchCache.eaPublishedProjectIDs[ projIt ]);
					}
				}
			}
		}
	}

	{
		SearchTypeReport *pReport = &sSearchTypeReports[eType];
		pReport->iCount++;
		pReport->iTotalMicroSecs += (timerCpuTicks64() - iStartTime) / siTicksToMicrosecs;
		pReport->iTotalMsecs = pReport->iTotalMicroSecs / 1000;
		pReport->iAverageMsecs = pReport->iTotalMsecs / pReport->iCount;
		pReport->iTotalReturns += eaSize(&pSearchResult->eaResults);
		pReport->iAverageNumReturned = pReport->iTotalReturns / pReport->iCount;
	}

	PERFINFO_AUTO_STOP();	
	return pSearchResult;
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void FindUGCMapsForPlaying(UGCProjectSearchInfo *pSearchInfo)
{
	if(s_bUGCEnableSearchCache && pSearchInfo->entContainerID)
	{
		UGCProjectSearchInfo *pUGCSearchCacheResult = ugcSearchCacheFind(pSearchInfo);
		if(pUGCSearchCacheResult)
		{
			pUGCSearchCacheResult->entContainerID = pSearchInfo->entContainerID;
			RemoteCommand_Intershard_gslUGCSearch_Return(pSearchInfo->pcShardName, GLOBALTYPE_ENTITYPLAYER, pSearchInfo->entContainerID, pUGCSearchCacheResult);
			return;
		}
	}

	{
		UGCProjectSearchInfo *pInputSearchInfo = StructClone(parse_UGCProjectSearchInfo, pSearchInfo);
		pInputSearchInfo->pUGCSearchResult = FindUGCMaps_Internal(pSearchInfo); // this function modifies pSearchInfo! so we clone it first so our caching on the GameServer works.

		if(s_bUGCEnableSearchCache && !IS_HANDLE_ACTIVE(pInputSearchInfo->pUGCSearchResult->hErrorMessage))
			ugcSearchCacheStore(pInputSearchInfo);

		if(pInputSearchInfo->loginCookie)
			RemoteCommand_Intershard_aslLoginUGCSearch_Return(pInputSearchInfo->pcShardName, GLOBALTYPE_LOGINSERVER, 0, pInputSearchInfo);
		else if(pInputSearchInfo->entContainerID)
			RemoteCommand_Intershard_gslUGCSearch_Return(pInputSearchInfo->pcShardName, GLOBALTYPE_ENTITYPLAYER, pInputSearchInfo->entContainerID, pInputSearchInfo);

		StructDestroy(parse_UGCProjectSearchInfo, pInputSearchInfo);
	}
}

AUTO_COMMAND_REMOTE;
UGCSearchResult *FindUGCMaps(UGCProjectSearchInfo *pSearchInfo)
{
	return FindUGCMaps_Internal(pSearchInfo);
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK ACMD_IFDEF(APPSERVER);
void SendWhatsHotListToSearchManager(WhatsHotList *pList)
{
	StructCopy(parse_WhatsHotList, pList, &sWhatsHot, 0, 0, 0);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCSearchManager_SearchBySeriesID_ForReviewing(UGCShardReturnProjectSeriesReviewed *pUGCShardReturnProjectSeriesReviewed)
{
	UGCProjectSeries* series = GetProjectSeries(pUGCShardReturnProjectSeriesReviewed->iSeriesID);
	
	RemoteCommand_Intershard_gslUGC_SearchBySeriesID_ForReviewing_Return(pUGCShardReturnProjectSeriesReviewed->pcShardName, GLOBALTYPE_ENTITYPLAYER, pUGCShardReturnProjectSeriesReviewed->entContainerID,
		series, pUGCShardReturnProjectSeriesReviewed);
}

static UGCProjectList *aslUGCSearchManager_SearchByID_Internal(UGCIDList* pList)
{
	UGCProjectList *accum = StructCreate( parse_UGCProjectList );
	int it;
	for( it = 0; it != ea32Size( &pList->eaProjectIDs ); ++it ) {
		UGCProject* proj = GetProject( pList->eaProjectIDs[ it ]);
		if( proj && UGCProject_GetMostRecentPublishedVersion( proj )) {
			eaPush( &accum->eaProjects, UGCProject_CreateHeaderCopy( proj, true ));
		}
	}
	for( it = 0; it != ea32Size( &pList->eaProjectSeriesIDs ); ++it ) {
		UGCProjectSeries* series = GetProjectSeries( pList->eaProjectSeriesIDs[ it ]);
		if( series ) {
			eaPush( &accum->eaProjectSeries, UGCProjectSeries_CreateHeaderCopy( series ));
		}
	}

	return accum;
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCSearchManager_SearchByID(UGCIDList* pList, UGCIntershardData *pUGCIntershardData)
{
	UGCProjectList *accum = aslUGCSearchManager_SearchByID_Internal(pList);

	if(pUGCIntershardData->entContainerID)
		RemoteCommand_Intershard_gslUGC_CacheSearchByID_Return(pUGCIntershardData->pcShardName, GLOBALTYPE_ENTITYPLAYER, pUGCIntershardData->entContainerID, accum, pUGCIntershardData->entContainerID);
	else if(pUGCIntershardData->loginCookie)
		RemoteCommand_Intershard_aslLogin_UGCSearchByID_Return(pUGCIntershardData->pcShardName, GLOBALTYPE_LOGINSERVER, 0, accum, pUGCIntershardData->loginCookie);

	StructDestroy(parse_UGCProjectList, accum);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void SearchByProjectID_ForPlaying(ContainerID iUGCProjectID, const char *pcShardName, ContainerID entContainerID)
{
	UGCProject *pUGCProject = GetProject(iUGCProjectID);
	if(pUGCProject && UGCProject_GetMostRecentPublishedVersion(pUGCProject))
	{
		UGCProject *pUGCProjectHeaderCopy = UGCProject_CreateHeaderCopy(pUGCProject, true);

		RemoteCommand_Intershard_gslUGC_SearchByProjectID_ForPlaying_Return(pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, pUGCProjectHeaderCopy, entContainerID);
	}
}

//////////////////////////////////////////////////////////////////////
// Featured search

typedef enum UGCFeaturedType {
	UGCFEATURED_PROJECT,
	UGCFEATURED_SERIES,
} UGCFeaturedType;

static UGCFeaturedData* UGCFeatured_ContentFeaturedData( const UGCContentInfo* pContent )
{
	if( pContent->iUGCProjectID ) {
		UGCProject* project = GetProject( pContent->iUGCProjectID );
		const UGCProjectVersion* version = UGCProject_GetMostRecentPublishedVersion( project );

		if( version && project->pFeatured ) {
			if( project->pFeatured->iStartTimestamp == 0 ) {
				AssertOrAlert( "UGC_FEATURED_BAD_TIMESTAMP", "Featured UGCProject %d has start timestamp 0", pContent->iUGCProjectID );
			}
			if( project->pFeatured->iEndTimestamp != 0 && project->pFeatured->iEndTimestamp < project->pFeatured->iStartTimestamp ) {
				AssertOrAlert( "UGC_FEATURED_BAD_TIMESTAMP", "Featured UGCProject %d has end timestamp %d before start timestamp %d",
							   pContent->iUGCProjectID, project->pFeatured->iEndTimestamp, project->pFeatured->iStartTimestamp );
			}
			return project->pFeatured;
		} else {
			return NULL;
		}
	}

	AssertOrAlert( "UGC_FEATURED_UNSUPPORTED_TYPE", "Got a request for a timestamp from an unsupported UGCContentInfo." );
	return NULL;
}

static U32 UGCFeatured_ContentStartTimestamp( const UGCContentInfo* pContent )
{
	UGCFeaturedData* pFeatured = UGCFeatured_ContentFeaturedData( pContent );
	return SAFE_MEMBER( pFeatured, iStartTimestamp );
}

static U32 UGCFeatured_ContentEndTimestamp( const UGCContentInfo* pContent )
{
	UGCFeaturedData* pFeatured = UGCFeatured_ContentFeaturedData( pContent );
	return SAFE_MEMBER( pFeatured, iEndTimestamp );
}

static UGCFeaturedType UGCFeatured_ContentType( const UGCContentInfo* pContent )
{
	if( pContent->iUGCProjectID ) {
		return UGCFEATURED_PROJECT;
	}

	AssertOrAlert( "UGC_FEATURED_UNSUPPORTED_TYPE", "Got a request for a timestamp from an unsupported UGCContentInfo." );
	return 0;
}

static U32 UGCFeatured_ContentID( const UGCContentInfo* pContent )
{
	if( pContent->iUGCProjectID ) {
		return pContent->iUGCProjectID;
	}

	AssertOrAlert( "UGC_FEATURED_UNSUPPORTED_TYPE", "Got a request for a timestamp from an unsupported UGCContentInfo." );
	return 0;
}

typedef struct UGCFeaturedListSearchData {
	const UGCContentInfo* pContent;
} UGCFeaturedListSearchData;

static int UGCFeatured_CompareFn( const UGCFeaturedListSearchData* pData, const UGCContentInfo** ppContent )
{
	int iStartTimestamp1 = UGCFeatured_ContentStartTimestamp( pData->pContent );
	int iStartTimestamp2 = UGCFeatured_ContentStartTimestamp( *ppContent );
	UGCFeaturedType type1 = UGCFeatured_ContentType( pData->pContent );
	UGCFeaturedType type2 = UGCFeatured_ContentType( *ppContent );
	U32 id1 = UGCFeatured_ContentID( pData->pContent );
	U32 id2 = UGCFeatured_ContentID( *ppContent );

	// Because we just store the IDs, sorted by time, iTimestamp2 WILL
	// be wrong if the UGCContentInfo is the same.
	if( type1 == type2 && id1 == id2 ) {
		return 0;
	}

	if( iStartTimestamp1 != iStartTimestamp2 ) {
		return iStartTimestamp2 - iStartTimestamp1;
	} else if( type1 != type2 ) {
		return type1 - type2;
	} else {
		return id1 - id2;
	}
}

void UGCFeatured_UpdateContent( const UGCContentInfo* pContent )
{
	UGCFeatured_RemoveContent( pContent );

	// Add to list, keeping list sorted by start time
	if( UGCFeatured_ContentStartTimestamp( pContent )) {
		UGCFeaturedListSearchData curSearchData = { pContent };
		int curIndex = (int)eaBFind( g_eaFeaturedListSortedByTime, UGCFeatured_CompareFn, curSearchData );
		eaInsert( &g_eaFeaturedListSortedByTime, StructClone( parse_UGCContentInfo, pContent ), curIndex );
	}
}

/// You must call this function while the content is still in the
/// dictionary, so it can find the old featured timestamp
void UGCFeatured_RemoveContent( const UGCContentInfo* pContent )
{
	FOR_EACH_IN_EARRAY( g_eaFeaturedListSortedByTime, UGCContentInfo, pOtherContent ) {
		if( StructCompare( parse_UGCContentInfo, pContent, pOtherContent, 0, 0, 0 ) == 0 ) {
			StructDestroy( parse_UGCContentInfo, pOtherContent );
			eaRemove( &g_eaFeaturedListSortedByTime, FOR_EACH_IDX( g_eaFeaturedListSortedByTime, pOtherContent ));
		}
	} FOR_EACH_END;
}

static UGCFeaturedContentInfoList *aslUGCSearchManager_Featured_GetState_Internal(void)
{
	UGCFeaturedContentInfoList* accum = StructCreate( parse_UGCFeaturedContentInfoList );

	int it;
	for( it = 0; it != eaSize( &g_eaFeaturedListSortedByTime ); ++it ) {
		UGCContentInfo* pContentInfo = g_eaFeaturedListSortedByTime[ it ];
		UGCFeaturedData* pFeatured = UGCFeatured_ContentFeaturedData( pContentInfo );

		if( pContentInfo && pFeatured ) {
			UGCFeaturedContentInfo* pFeaturedContentInfo = StructCreate( parse_UGCFeaturedContentInfo );
			StructCopy( parse_UGCContentInfo, pContentInfo, &pFeaturedContentInfo->sContentInfo, 0, 0, 0 );
			StructCopy( parse_UGCFeaturedData, pFeatured, &pFeaturedContentInfo->sFeaturedData, 0, 0, 0 );

			eaPush( &accum->eaFeaturedContent, pFeaturedContentInfo );
		}
	}

	return accum;
}

/// Debugging command for remotely saving the featured state.
AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCSearchManager_Featured_LoadState(const char *pcShardName, ContainerID entContainerID, UGCFeaturedContentInfoList* pFeaturedContent)
{
	UGCFeaturedContentInfoList *accum = aslUGCSearchManager_Featured_GetState_Internal();

	RemoteCommand_Intershard_gslUGC_Featured_LoadState_Return(pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, pFeaturedContent, accum);

	StructDestroy(parse_UGCFeaturedContentInfoList, accum);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCSearchManager_Featured_SaveState(const char *pcShardName, ContainerID entContainerID)
{
	UGCFeaturedContentInfoList *accum = aslUGCSearchManager_Featured_GetState_Internal();

	RemoteCommand_Intershard_gslUGC_Featured_SaveState_Return(pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, entContainerID, accum);

	StructDestroy(parse_UGCFeaturedContentInfoList, accum);
}

void UGCFeatured_UpdateAuthorAllowsFeaturedContent( const UGCContentInfo* pContent )
{
	UGCFeatured_RemoveAuthorAllowsFeaturedContent( pContent );

	if( pContent->iUGCProjectID ) {
		UGCProject* pUGCProject = GetProject( pContent->iUGCProjectID );
		if( pUGCProject && pUGCProject->bAuthorAllowsFeatured ) {
			stashIntAddInt( g_sAuthorAllowsFeaturedProjectsByID, pContent->iUGCProjectID, pContent->iUGCProjectID, false );
		}
	} else {
		// Other types are not yet supported for featuring.
	}
}

void UGCFeatured_RemoveAuthorAllowsFeaturedContent( const UGCContentInfo* pContent )
{
	if( !g_sAuthorAllowsFeaturedProjectsByID ) {
		g_sAuthorAllowsFeaturedProjectsByID = stashTableCreateInt( 4 );
	}

	if( pContent->iUGCProjectID ) {
		stashIntRemoveInt( g_sAuthorAllowsFeaturedProjectsByID, pContent->iUGCProjectID, NULL );
	} else {
		// Other types are not yet supported for featuring.
	}
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCSearchManager_Featured_GetAuthorAllowsFeaturedList(const char *pcShardName, ContainerID entContainerID, bool bShow, bool bIncludeAlreadyFeatured)
{
	UGCIDList idList = { 0 };
	UGCProjectList* accum = NULL;

	// Get projects
	FOR_EACH_IN_STASHTABLE2( g_sAuthorAllowsFeaturedProjectsByID, it ) {
		UGCContentInfo info = { 0 };
		info.iUGCProjectID = stashElementGetU32Key( it );
		if(bIncludeAlreadyFeatured || !UGCFeatured_ContentFeaturedData( &info ))
			ea32Push( &idList.eaProjectIDs, info.iUGCProjectID );
	} FOR_EACH_END;

	accum = aslUGCSearchManager_SearchByID_Internal(&idList);
	StructReset( parse_UGCIDList, &idList );
	
	RemoteCommand_Intershard_gslUGC_Featured_GetAuthorAllowsFeaturedList_Return(pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, entContainerID, accum, bShow);

	StructDestroy(parse_UGCProjectList, accum);
}

#include "aslUGCSearchManager_c_ast.c"
