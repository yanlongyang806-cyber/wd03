#include "UGCEditorMain.h"

#include "ActivityLogCommon.h"
#include "ActivityLogEnums.h"
#include "AutoTransDefs.h"
#include "ContinuousBuilderSupport.h"
#include "ControllerScriptingSupport.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "Expression.h"
#include "GameAccountDataCommon.h"
#include "GameClientLib.h"
#include "GamePermissionsCommon.h"
#include "GameStringFormat.h"
#include "GfxConsole.h"
#include "GlobalComm.h"
#include "Login2Common.h"
#include "LoginCommon.h"
#include "Message.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "StringUtil.h"
#include "UGCCommon.h"
#include "UGCProjectChooser.h"
#include "UIGen.h"
#include "cmdparse.h"
#include "contact_common.h"
#include "file.h"
#include "gclAccountProxy.h"
#include "gclCommandParse.h"
#include "gclEntity.h"
#include "gclLogin.h"
#include "gclNotify.h"
#include "gclUGC.h"
#include "mission_common.h"
#include "resourceManager.h"

#include "GfxSpriteText.h"

#include "AutoGen/UGCProjectCommon_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "gclUGC_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

///////////////						   
// enum for the details requester
typedef enum UGCDetailRequestReasons {
	UGC_DETAILREQUEST_FOR_REVIEW,
	UGC_DETAILREQUEST_FOR_MISSIONDISPLAY,
} UGCDetailRequestReasons;

typedef enum UGCPlayMode {
	UGC_NORMAL,
	UGC_UNTARGETABLE,
	UGC_GOD_MODE,
} UGCPlayMode;
static UGCPlayMode sPlayMode;

AUTO_STRUCT;
typedef struct UGCPlayModeModelRow {
	int mode;
	const char* message;
} UGCPlayModeModelRow;
extern ParseTable parse_UGCPlayModeModelRow[];
#define TYPE_parse_UGCPlayModeModelRow UGCPlayModeModelRow
static UGCPlayModeModelRow** sPlayModeModel;

AUTO_STRUCT;
typedef struct UGCContentInfoWithMapLocation {
	UGCContentInfo contentInfo;			AST(NAME("ContentInfo"))
	UGCMapLocation mapLocation;			AST(NAME("MapLocation"))
} UGCContentInfoWithMapLocation;
extern ParseTable parse_UGCContentInfoWithMapLocation[];
#define TYPE_parse_UGCContentInfoWithMapLocation UGCContentInfoWithMapLocation


static bool sbSearchResultQueueClear = false;
static UGCSearchResult *spSearchResult = NULL;
static UGCProject **seaSearchProjectList = NULL;
static UGCContentInfo** seaSearchContentInfoList = NULL;
static UGCContentInfo** seaSearchContentInfoListFeatured = NULL;	//used for UGC Search agents to show a second list.
static bool sbWaitingForSearchResults = false; //so UI doesn't show "empty" while just loading.

static UGCContentInfo sChosenContentInfo;
static UGCContentInfoWithMapLocation** seaChosenMapLocations;

///////////////						   
// Globals used for the review pop up. They are set at various points as the information becomes available.
//  and then accessed globally.
static UGCProject* spReviewableProject = NULL; // Project that is being shown in the review dialog currently
static UGCProjectSeries* spReviewableProjectSeries = NULL;
static bool s_bMissionToReviewQueued = false;
static bool s_bMissionToReviewWasCompleted = false;

///////////////
// Global for find UGC window (lobby in NNO).
static bool showUGCFinder;

///////////////						   
// Globals used for the mission journal available and inProgress display.
static UGCProject* spDetailProject = NULL; // Project that is being shown in the list currently
static UGCProjectSeries* spDetailProjectSeries = NULL;
static NOCONST(UGCProjectReviews)* spDetailReviews = NULL;
static S32 s_iCurrentReviewsPage = 0;

///////////////						   
// General Globals 
static bool bGodModeEnabled = false;
static bool s_requestReviewsInProgress = false;
static bool sbReviewingIsEnabled = true;
static UGCProjectSearchInfo *s_search_info = NULL;	//Stores secondary search fields so advanced search can be split over two expressions.

UGCSearchResultCallback spSearchResultCallback;
void *spSearchResultUserdata;

//forward declarations
static bool gclUGC_PlayerHasUGCMission(ContainerID iProjectID);
static void gclUGC_SeriesNodeProjectsInSpoilerOrder( ContainerID** out_peaProjectsInOrder, CONST_EARRAY_OF(UGCProjectSeriesNode) eaNodes );
								 
AUTO_RUN;
void gclUGC_InitUI(void)
{
	UGCPlayModeModelRow* modelRow;
	ui_GenInitStaticDefineVars(UGCProjectSearchSpecialTypeEnum, "SPECIALSEARCH_");
	ui_GenInitStaticDefineVars(UGCProjectReportReasonEnum, "UGCReportReason_");
	ui_GenInitStaticDefineVars(LanguageEnum, "LANGUAGE_");

	modelRow = StructCreate( parse_UGCPlayModeModelRow );
	modelRow->mode = UGC_NORMAL;
	modelRow->message = "UGC.PlayMode_Normal";
	eaPush( &sPlayModeModel, modelRow );
	modelRow = StructCreate( parse_UGCPlayModeModelRow );
	modelRow->mode = UGC_UNTARGETABLE;
	modelRow->message = "UGC.PlayMode_Untargetable";
	eaPush( &sPlayModeModel, modelRow );
	modelRow = StructCreate( parse_UGCPlayModeModelRow );
	modelRow->mode = UGC_GOD_MODE;
	modelRow->message = "UGC.PlayMode_GodMode";
	eaPush( &sPlayModeModel, modelRow );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ToggleGodMode);
void gclUGC_ToggleGodMode(void)
{
	bGodModeEnabled = !bGodModeEnabled;
	
	ServerCmd_ugcGodMode(bGodModeEnabled);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GodModeEnabled);
int gclUGC_GodModeEnabled(void)
{
	return bGodModeEnabled;
}

void gclUGC_ReSyncGodMode(void)
{
	ServerCmd_GodMode(bGodModeEnabled);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetPlayMode);
SA_ORET_NN_VALID UGCPlayModeModelRow* gclUGC_GetPlayMode(void)
{
	int it;
	for( it = 0; it != eaSize( &sPlayModeModel ); ++it ) {
		if( sPlayModeModel[ it ]->mode == sPlayMode ) {
			return sPlayModeModel[ it ];
		}
	}
	assert( sPlayModeModel );
	return sPlayModeModel[ 0 ];
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_SetPlayMode);
void gclUGC_SetPlayMode(int mode)
{
	UGCPlayModeModelRow* row;	
	sPlayMode = mode;
	row = gclUGC_GetPlayMode();

	switch( row->mode ) {
		case UGC_NORMAL:
			ServerCmd_gslUGC_GodMode(0);
			ServerCmd_gslUGC_UntargetableMode(0);
		xcase UGC_UNTARGETABLE:
			ServerCmd_gslUGC_GodMode(1);
			ServerCmd_gslUGC_UntargetableMode(1);
		xcase UGC_GOD_MODE:
			ServerCmd_gslUGC_GodMode(1);
			ServerCmd_gslUGC_UntargetableMode(0);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetPlayModeModel);
void gclUGC_GetPlayModeModel( SA_PARAM_NN_VALID UIGen* pGen )
{
	ui_GenSetList( pGen, &sPlayModeModel, parse_UGCPlayModeModelRow );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FreeCamEnabled);
int gclUGC_FreeCamEnabled(void)
{
	return gGCLState.bUseFreeCamera;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_RespawnAtFullHealth);
void gclUGC_RespawnAtFullHealth( void )
{
	ServerCmd_gslUGC_RespawnAtFullHealth();
}

bool gclUGC_PlayerHasUGCMission(ContainerID iProjectID)
{
	// MJF TODO: Make this work without a GameServer
	Entity *pEnt = entActivePlayerPtr();
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);

	if (pInfo && mission_FindMissionFromUGCProjectID(pInfo, iProjectID))
	{
		return true;
	}
	return false;
}

// The server recorded this mission as being the last one completed or dropped.
bool gclUGC_PlayerJustCompletedOrDroppedUGCMission(ContainerID iProjectID)
{
	// MJF TODO: Make this work without a GameServer
	Entity *pEnt = entActivePlayerPtr();
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);

	if (!pInfo || pInfo->uLastMissionRatingRequestID != iProjectID)
	{
		return false;
	}
	return true;
}

// Returns true if the player currently has the mission for this project, 
// or has recently played the mission within uHours
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_HasRecentlyPlayedProject);
bool gclUGC_HasRecentlyPlayedProject(SA_PARAM_OP_VALID UGCProject *pProject, S32 uWithinHours)
{
	if (pProject)
	{
		// MJF TODO: Make this work without a GameServer
		if (gclUGC_PlayerHasUGCMission(pProject->id) ||
			entity_HasRecentlyCompletedUGCProject(entActivePlayerPtr(), pProject->id, uWithinHours*3600))
		{
			return true;
		}
	}
	return false;
}

static void gclUGC_DestroySearchInfo(UGCProjectSearchInfo *pSearchInfo)
{
	StructDestroy(parse_UGCProjectSearchInfo, pSearchInfo);
}

// returns true if we asked for search results more recently than we've recieved them.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_SearchIsWaiting);
bool gclUGC_SearchIsWaiting()
{
	return sbWaitingForSearchResults;
}

//Auto expressions take max 11 args, so this puts some of the parameters into a global 
//search info.  This gets copied then other fields are set.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_SearchSetOptionRanges);
void gclUGC_SearchSetOptionRanges(	F32 fRatingMin, F32 fRatingMax, 
								F32 fDurationMin, F32 fDurationMax, 
								S32 iPlayerLevelMin, S32 iPlayerLevelMax)
{
	UGCProjectSearchFilter *pFilter;
	devassert( !s_search_info );
	if(!s_search_info){
		s_search_info = StructCreate(parse_UGCProjectSearchInfo);
	}

	// Filter by rating
	if (fRatingMin > 0)
	{
		pFilter = StructCreate(parse_UGCProjectSearchFilter);
		pFilter->eType = UGCFILTER_RATING;
		pFilter->pField = StructAllocString("Rating");
		pFilter->eComparison = UGCCOMPARISON_GREATERTHAN;
		pFilter->fFloatValue = fRatingMin;
		eaPush(&s_search_info->ppFilters, pFilter);
	}
	if (fRatingMax > 0)
	{
		pFilter = StructCreate(parse_UGCProjectSearchFilter);
		pFilter->eType = UGCFILTER_RATING;
		pFilter->pField = StructAllocString("Rating");
		pFilter->eComparison = UGCCOMPARISON_LESSTHAN;
		pFilter->fFloatValue = fRatingMax;
		eaPush(&s_search_info->ppFilters, pFilter);
	}

	// Filter by average play time
	if (fDurationMin > 0)
	{
		pFilter = StructCreate(parse_UGCProjectSearchFilter);
		pFilter->eType = UGCFILTER_AVERAGEPLAYTIME;
		pFilter->pField = StructAllocString("AveragePlayTime");
		pFilter->eComparison = UGCCOMPARISON_GREATERTHAN;
		pFilter->fFloatValue = fDurationMin;
		eaPush(&s_search_info->ppFilters, pFilter);
	}
	if (fDurationMax > 0)
	{
		pFilter = StructCreate(parse_UGCProjectSearchFilter);
		pFilter->eType = UGCFILTER_AVERAGEPLAYTIME;
		pFilter->pField = StructAllocString("AveragePlayTime");
		pFilter->eComparison = UGCCOMPARISON_LESSTHAN;
		pFilter->fFloatValue = fDurationMax;
		eaPush(&s_search_info->ppFilters, pFilter);
	}

	// Filter by player level
	s_search_info->iPlayerLevelMin = iPlayerLevelMin;
	s_search_info->iPlayerLevelMax = iPlayerLevelMax;
}

static UGCProjectSearchInfo* gclUGC_CreateSearchInfo(const char *pSimple, 
													 int iSpecialType, 
													 const char *pAuthorName, 
													 S32 eLanguage,
													 const char *pchLocation,
													 int iLastNDays,
													 bool bUGCProjects,
													 bool bUGCSeries,
													 bool bFeaturedIncludeArchives)
{
	UGCProjectSearchInfo *pSearchInfo;
	UGCProjectSearchFilter *pFilter;

	if( s_search_info) {
		//copy the information that has already been set by ugc_SearchSetOptionRanges():
		pSearchInfo = s_search_info;
		s_search_info = NULL;
	} else {
		pSearchInfo = StructCreate(parse_UGCProjectSearchInfo);
	}

	//types to search:
	pSearchInfo->bUGCProjects = bUGCProjects;
	pSearchInfo->bUGCSeries = bUGCSeries;
	
	if (pSimple && pSimple[0])
	{
		char *pTemp = NULL;
		estrStackCreate(&pTemp);
		estrCopy2(&pTemp, pSimple);
		estrTrimLeadingAndTrailingWhitespace(&pTemp);
		pSearchInfo->pSimple_Raw = strdup(pTemp);
		estrDestroy(&pTemp);
	}
	else
	{
		pSearchInfo->pSimple_Raw = strdup("");
	}

	// Filter by author
	if (pAuthorName && pAuthorName[0])
	{
		pFilter = StructCreate(parse_UGCProjectSearchFilter);
		pFilter->eType = UGCFILTER_STRING;
		pFilter->pField = StructAllocString("Author");
		pFilter->eComparison = UGCCOMPARISON_CONTAINS;
		pFilter->pStrValue = estrCreateFromStr(pAuthorName);
		eaPush(&pSearchInfo->ppFilters, pFilter);
	}

	// Filter by language
	if(!gConf.bUGCSearchTreatsDefaultLanguageAsAll)
	{
		if( eLanguage >= 0 && eLanguage != LANGUAGE_DEFAULT ) {
			pSearchInfo->eLang = eLanguage;
		} else {
			pSearchInfo->eLang = entGetLanguage(entActivePlayerPtr());
		}
	}
	else
	{
		if(eLanguage < 0)
			pSearchInfo->eLang = 0;
		else
			pSearchInfo->eLang = eLanguage;
	}

	if( pchLocation && pchLocation[0] ){
		pSearchInfo->pchLocation = estrCreateFromStr(pchLocation);
	}
	if( iLastNDays ){
		pSearchInfo->iPublishedInLastNDays = iLastNDays;
	}

	pSearchInfo->eSpecialType = iSpecialType;
	pSearchInfo->bFeaturedIncludeArchives = bFeaturedIncludeArchives;

	return pSearchInfo;
}


//@param pSimple: search string to match
//@param iSpecialType: search type
//@param pchLocation: used by NW to put UGC missions in regions so that search agents can give nearby results
//@param iLastNDays: return projects that passed review in the last N days
static void gclUGC_SearchInternal(const char *pSimple, 
								  UGCProjectSearchSpecialType iSpecialType, 
								  const char *pAuthorName, 
								  Language eLanguage,
								  const char *pchLocation,
								  int iLastNDays,
								  bool bUGCProjects,
								  bool bUGCSeries,
								  bool bFeaturedIncludeArchives
								  )
{
	UGCProjectSearchInfo* pSearchInfo = gclUGC_CreateSearchInfo(pSimple, 
																iSpecialType, 
																pAuthorName,
																eLanguage,
																pchLocation,
																iLastNDays,
																bUGCProjects,
																bUGCSeries,
																bFeaturedIncludeArchives);
	const char* astrErrorMessageKey = NULL;
	if(spSearchResult) {
		REMOVE_HANDLE(spSearchResult->hErrorMessage);
	}
	
	if (UGCProject_ValidateAndFixupSearchInfo(pSearchInfo, &astrErrorMessageKey)) 
	{
		devassert( astrErrorMessageKey == NULL );
		if( linkConnected( gServerLink )) {
			ServerCmd_gslUGC_Search(pSearchInfo);
			sbWaitingForSearchResults = true;
        }

		// Clear the existing project list -- but can't do it in a UI callback
		sbSearchResultQueueClear = true;
	}
	else
	{
		UGCSearchResult searchResult = { 0 };
		SET_HANDLE_FROM_STRING("Message", astrErrorMessageKey, searchResult.hErrorMessage);
		
		gclUGC_ReceiveSearchResult( &searchResult );
		StructReset( parse_UGCSearchResult, &searchResult );
	}
	gclUGC_DestroySearchInfo(pSearchInfo);
}

//This is the preferred search function to be called from UIGens.  
//Optionally call ugc_SearchSetOptionRanges first to set more options.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_SearchAdvanced);
void gclUGC_SearchAdvanced(
							const char *pSimple, 
							int iSpecialType, 
							const char *pAuthorName, 
							int eLanguage,
							const char *pchLocation,
							int iLastNDays)
{
	gclUGC_SearchInternal(pSimple, iSpecialType, pAuthorName, eLanguage, pchLocation, 
						  iLastNDays, true, true, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_LanguageDisplayMessage);
const char *gclUGC_LanguageDisplayMessage(int lang)
{
	return locGetDisplayName(locGetIDByLanguage(lang));
}

//This is depreciated.  It should be removed after STO Gens stop using it.
//Use  gclUGC_SearchAdvanced instead (and optionally ugc_SearchSetOptionRanges).
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_SearchEx);
void gclUGC_SearchEx(
					const char *pSimple, 
					int iSpecialType, 
					const char *pAuthorName, 
					int eLanguage,
					F32 fRatingMin,
					F32 fRatingMax,
					F32 fDurationMin,
					F32 fDurationMax,
					int iPlayerLevelMin,
					int iPlayerLevelMax)
{
	gclUGC_SearchSetOptionRanges(fRatingMin, fRatingMax, fDurationMin, fDurationMax, iPlayerLevelMin, iPlayerLevelMax);
	gclUGC_SearchInternal(pSimple, iSpecialType, pAuthorName, eLanguage, "", 0, true, true, false);
}



AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_SearchAgent);
void gclUGC_SearchContactUGCSearchAgent(
	const char *pSimple, 
	const char *pAuthorName, 
	int eLanguage
	)
{
	const char* location = entActivePlayerPtr()->pPlayer->pInteractInfo->pContactDialog->pUGCSearchAgentData->location;
	int duration = entActivePlayerPtr()->pPlayer->pInteractInfo->pContactDialog->pUGCSearchAgentData->maxDuration;
	int iLastNDays = entActivePlayerPtr()->pPlayer->pInteractInfo->pContactDialog->pUGCSearchAgentData->lastNDays;
	gclUGC_SearchInternal(pSimple, SPECIALSEARCH_FEATURED_AND_MATCHING, pAuthorName, eLanguage, location, iLastNDays, true, true, false);
	
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_Search);
void gclUGC_Search(const char *pSimple)
{
	gclUGC_SearchInternal(pSimple, SPECIALSEARCH_NONE, NULL, -1, NULL, 0, true, true, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetBrowseContent);
void gclUGC_GetBrowseContent(S32 eLanguage)
{
	gclUGC_SearchInternal(NULL, SPECIALSEARCH_BROWSE, NULL, eLanguage, NULL, 0, true, true, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetNewContent);
void gclUGC_SearchNewContent(S32 eLanguage)
{
	gclUGC_SearchInternal(NULL, SPECIALSEARCH_NEW, NULL, eLanguage, NULL, 0, true, true, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetFeaturedContent);
void gclUGC_GetFeaturedContent(S32 eLanguage, bool bIncludeArchives)
{
	gclUGC_SearchInternal(NULL, SPECIALSEARCH_FEATURED, NULL, eLanguage, NULL, 0, true, true, bIncludeArchives);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetSubscribedContent);
void gclUGC_SearchSubcribedContent(void)
{
	gclUGC_SearchInternal(NULL, SPECIALSEARCH_SUBCRIBED, NULL, -1, NULL, 0, true, true, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetReviewContent);
void gclUGC_SearchReviewContent(void)
{
	gclUGC_SearchInternal(NULL, SPECIALSEARCH_REVIEWER, NULL, -1, NULL, 0, true, true, false);
}

/*----------these don't seem to be called anywhere, including NW and STO gens. They look useful but out of date.-------------
// Returns the error string for the passed search data. If the string is empty, then the search is valid.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ValidateSearchEx);
const char* ugc_ValidateSearchEx(const char *pSimple, 
								int iSpecialType, 
								const char *pAuthorName, 
								S32 eLanguage,
								char *pchLocation,
								int iLastNDays
	)
{
	const char* astrErrorMessageKey = NULL;
	UGCProjectSearchInfo* pSearchInfo = gclUGC_CreateSearchInfo(pSimple, 
																iSpecialType, 
																pAuthorName,
																eLanguage,
																pchLocation,
																iLastNDays
																eLanguage,
																pchLocation,
																iLastNDays,
																bShowRecentlyPlayed);
	UGCProject_ValidateAndFixupSearchInfo(pSearchInfo, &astrErrorMessageKey );
	gclUGC_DestroySearchInfo(pSearchInfo);

	if(astrErrorMessageKey) {
		return TranslateMessageKey( astrErrorMessageKey );
	} else {
		return NULL;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ValidateSearch);
const char* ugc_ValidateSearch(const char *pSimple, 
								int iSpecialType, 
								const char *pAuthorName, 
								S32 eLanguage,
								char *pchLocation,
								int iLastNDays)
{
	return ugc_ValidateSearchEx(pSimple, iSpecialType, pAuthorName, eLanguage, 
		pchLocation, iLastNDays);
}
*/

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclUGC_ReceiveSearchResult(UGCSearchResult *pSearchResult) 
{
	StructDestroySafe(parse_UGCSearchResult, &spSearchResult);
	spSearchResult = StructClone(parse_UGCSearchResult, pSearchResult);
	sbSearchResultQueueClear = false;
	sbWaitingForSearchResults = false;

	if (spSearchResultCallback)
	{
		spSearchResultCallback(spSearchResult, spSearchResultUserdata);
	}
}

void gclUGC_SetSearchResultCallback(UGCSearchResultCallback callback, void *userdata)
{
	StructDestroySafe(parse_UGCSearchResult, &spSearchResult);
	spSearchResultCallback = callback;
	spSearchResultUserdata = userdata;
}

//requests to show the UGC finder.  Called by the game server after interacting with 
//a job board.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_HIDE ACMD_ACCESSLEVEL(0);
void gclUGC_ShowUGCFinder(){
	showUGCFinder = true;
}

//returns whether the user has requested to search for UGC since this was last called.
//polled each frame by 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetShowUGCFinder);
bool gclUGC_GetShowUGCFinder(){
	bool tmp = showUGCFinder;
	showUGCFinder = false;
	return tmp;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetSearchList);
void gclUGC_GetSearchListProjectsOnly(SA_PARAM_NN_VALID UIGen *pGen)
{
	if(spSearchResult) {
		if( sbSearchResultQueueClear ) {
			StructReset( parse_UGCSearchResult, spSearchResult );
		}
	}
	sbSearchResultQueueClear = false;

	// rebuild the list -- MJF TODO: Possibly inefficient to do once a frame?
	eaClearStruct( &seaSearchProjectList, parse_UGCProject );
	if( spSearchResult ) {
		int it;
		for( it = 0; it != eaSize( &spSearchResult->eaResults ); ++it ) {
			if( spSearchResult->eaResults[ it ]->iUGCProjectID ) {
				UGCProject* pProject = gclUGC_CacheGetProject( spSearchResult->eaResults[ it ]->iUGCProjectID );
				if( pProject ) {
					eaPush( &seaSearchProjectList, StructClone( parse_UGCProject, pProject ));
				}
			}
		}
	}
	
	ui_GenSetManagedListSafe(pGen, &seaSearchProjectList, UGCProject, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetSearchListContentInfo);
void gclUGC_GetSearchListContentInfo(SA_PARAM_NN_VALID UIGen* pGen, bool bAllowUGCProject, bool bAllowUGCSeries, bool bAllowNonUGC)
{
	if(spSearchResult) {
		if( sbSearchResultQueueClear ) {
			StructReset( parse_UGCSearchResult, spSearchResult );
		}
	}
	sbSearchResultQueueClear = false;

	// rebuild the list -- MJF TODO: Possibly inefficient to do once a frame?
	eaClearStruct( &seaSearchContentInfoList, parse_UGCContentInfo );
	if( spSearchResult ) {
		int it;
		for( it = 0; it != eaSize( &spSearchResult->eaResults ); ++it ) {
			UGCContentInfo* info = spSearchResult->eaResults[ it ];
			if( info->iUGCProjectID ) {
				if( bAllowUGCProject && gclUGC_CacheGetProject( info->iUGCProjectID )) {
					eaPush( &seaSearchContentInfoList, StructClone( parse_UGCContentInfo, info ));
				}
			} else if( info->iUGCProjectSeriesID ) {
				if( bAllowUGCSeries && gclUGC_CacheGetProjectSeries( info->iUGCProjectSeriesID )) {
					eaPush( &seaSearchContentInfoList, StructClone( parse_UGCContentInfo, info ));
				}
			} else if( bAllowNonUGC ) {
				eaPush( &seaSearchContentInfoList, StructClone( parse_UGCContentInfo, info ));
			}
		}
	}
	
	ui_GenSetManagedListSafe(pGen, &seaSearchContentInfoList, UGCContentInfo, false);
}

// returns either the featured or unfeatured missions (not campaigns) from search result.  
// This is for UGC search agent design.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetListForSearchAgent);
void gclUGC_GetListForSearchAgent(SA_PARAM_NN_VALID UIGen* pGen, bool bFeatured, int iMaxResults, int hours)
{
	UGCContentInfo*** peaList = bFeatured ? &seaSearchContentInfoListFeatured : &seaSearchContentInfoList;
	if(spSearchResult) {
		if( sbSearchResultQueueClear ) {
			StructReset( parse_UGCSearchResult, spSearchResult );
		}
	}
	sbSearchResultQueueClear = false;
	// rebuild the list -- MJF TODO: Possibly inefficient to do once a frame?
	eaClearStruct( peaList, parse_UGCContentInfo );
	if( spSearchResult ) {
		int i;
		iMaxResults = iMaxResults ? iMaxResults : eaSize(&spSearchResult->eaResults);
		for( i = 0; eaSize(peaList) < iMaxResults && i < eaSize(&spSearchResult->eaResults); ++i ) {
			UGCContentInfo* info = spSearchResult->eaResults[i];
			if( info->iUGCProjectID ) {
				UGCProject* pProject = gclUGC_CacheGetProject( info->iUGCProjectID );
				if(pProject														//only quests in the cache (not campaigns)
				&& (bFeatured || !entity_HasRecentlyCompletedUGCProject(entActivePlayerPtr(), pProject->id, hours * 3600))	//that I have not completed recently or are featured
				&& !!bFeatured == !!gclUGC_GetContentInfoFeaturedData(info)){	//and match the requested featured-ness
					eaPush( peaList, StructClone( parse_UGCContentInfo, info ));
				}
			}
		}
	}

	ui_GenSetManagedListSafe(pGen, peaList, UGCContentInfo, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetSearchListSize);
int gclUGC_GetSearchListSize(bool bAllowUGCProject, bool bAllowUGCSeries, bool bAllowNonUGC)
{
	int accum = 0;
	if( spSearchResult ) {
		int it;
		for( it = 0; it != eaSize( &spSearchResult->eaResults ); ++it ) {
			UGCContentInfo* info = spSearchResult->eaResults[ it ];
			if( info->iUGCProjectID ) {
				if( bAllowUGCProject ) {
					++accum;
				}
			} else if( info->iUGCProjectSeriesID ) {
				if( bAllowUGCSeries ) {
					++accum;
				}
			} else if( bAllowNonUGC ) {
				++accum;
			}
		}
	}

	return accum;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetSearchError);
const char *gclUGC_GetSearchError(void)
{
	return gclUGC_GetSearchErrorForResults(spSearchResult);
}

const char* gclUGC_GetSearchErrorForResults(UGCSearchResult* pResults)
{
	if(pResults)
	{
		if(IS_HANDLE_ACTIVE(pResults->hErrorMessage))
			return TranslateMessageRef(pResults->hErrorMessage);
		else if(!eaSize(&pResults->eaResults))
			return TranslateMessageKey("UGCSearchError_NoResults");
#if 0
		else
			return TranslateMessageKey("UGCSearchError_Generic");
#endif
	}
	return "";
}

/////////////////////////////////////////////////

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_SetChosen);
void gclUGC_SetChoosen(ContainerID iProjectID, ContainerID iProjectSeriesID)
{
	sChosenContentInfo.iUGCProjectID = iProjectID;
	sChosenContentInfo.iUGCProjectSeriesID = iProjectSeriesID;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetChosenProject);
SA_ORET_OP_VALID UGCProject* gclUGC_GetChosenProject()
{
	return gclUGC_CacheGetProject( sChosenContentInfo.iUGCProjectID );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetChosenProjectSeries);
SA_ORET_OP_VALID UGCProjectSeries* gclUGC_GetChosenProjectSeries()
{
	return gclUGC_CacheGetProjectSeries( sChosenContentInfo.iUGCProjectSeriesID );
}

static bool gclUGC_IsProjectInSeries(ContainerID uProjectID, ContainerID uSeriesID)
{
	const UGCProjectSeries *pSeries = gclUGC_CacheGetProjectSeries(uSeriesID);
	if(pSeries)
	{
		const UGCProjectSeriesVersion *pVersion = UGCProjectSeries_GetMostRecentPublishedVersion(pSeries);
		if(pVersion)
		{
			ContainerID *uProjectIDs = NULL;
			int i;
			gclUGC_SeriesNodeProjectsInSpoilerOrder(&uProjectIDs, pVersion->eaChildNodes);
			for(i = 0; i < eaiSize(&uProjectIDs); i++)
			{
				if(uProjectID == uProjectIDs[i])
				{
					eaiDestroy(&uProjectIDs);
					return true;
				}
			}
			eaiDestroy(&uProjectIDs);
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetChosenContent);
SA_ORET_OP_VALID UGCContentInfo* gclUGC_GetChosenContentInfo()
{
	// only return the content if it is currently in our search list
	if(spSearchResult)
	{
		if(sChosenContentInfo.iUGCProjectID || sChosenContentInfo.iUGCProjectSeriesID)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(spSearchResult->eaResults, UGCContentInfo, pUGCContentInfo)
			{
				if(sChosenContentInfo.iUGCProjectID && sChosenContentInfo.iUGCProjectID == pUGCContentInfo->iUGCProjectID)
					return &sChosenContentInfo;
				else if(sChosenContentInfo.iUGCProjectSeriesID && sChosenContentInfo.iUGCProjectSeriesID == pUGCContentInfo->iUGCProjectSeriesID)
					return &sChosenContentInfo;
				else if(sChosenContentInfo.iUGCProjectID && pUGCContentInfo->iUGCProjectSeriesID)
					if(gclUGC_IsProjectInSeries(sChosenContentInfo.iUGCProjectID, pUGCContentInfo->iUGCProjectSeriesID))
						return &sChosenContentInfo;
			}
			FOR_EACH_END;
		}

		if(eaSize(&spSearchResult->eaResults))
		{
			gclUGC_SetChoosen(spSearchResult->eaResults[0]->iUGCProjectID, spSearchResult->eaResults[0]->iUGCProjectSeriesID);
			return &sChosenContentInfo;
		}
	}

	return NULL;
}

static void gclUGC_PlayInternal(bool bPlayingAsBetaReviewer)
{
	const UGCProjectVersion *pUGCProjectVersion = UGCProject_GetMostRecentPublishedVersion(gclUGC_GetChosenProject());
	if(!pUGCProjectVersion)
	{
		const UGCProjectSeries *pUGCProjectSeries = gclUGC_GetChosenProjectSeries();
		const UGCProjectSeriesNode *pUGCProjectSeriesNode = NULL;
		const UGCProject *pUGCProject = NULL;

		const UGCProjectSeriesVersion *pUGCProjectSeriesVersion = UGCProjectSeries_GetMostRecentPublishedVersion(pUGCProjectSeries);
		if(!pUGCProjectSeriesVersion || 0 == eaSize(&pUGCProjectSeriesVersion->eaChildNodes))
			return;

		pUGCProjectSeriesNode = pUGCProjectSeriesVersion->eaChildNodes[0];
		pUGCProject = gclUGC_CacheGetProject(pUGCProjectSeriesNode->iProjectID);
		if(!pUGCProject)
			return;

		pUGCProjectVersion = UGCProject_GetMostRecentPublishedVersion(pUGCProject);
		if(!pUGCProjectVersion)
			return;
	}

	printf("Playing UGC misson %s\n", pUGCProjectVersion->pNameSpace);
	ServerCmd_gslUGC_PlayProjectNonEditor(pUGCProjectVersion->pNameSpace,
		pUGCProjectVersion->pCostumeOverride,
		pUGCProjectVersion->pPetOverride,
		pUGCProjectVersion->pBodyText,
		bPlayingAsBetaReviewer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_Play);
void gclUGC_Play(void)
{
	gclUGC_PlayInternal(/*bPlayingAsBetaReviewer=*/false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_PlayAsBetaReviewer);
void gclUGC_PlayAsBetaReviewer(void)
{
	gclUGC_PlayInternal(/*bPlayingAsBetaReviewer=*/true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_PlayFeatured);
void gclUGC_PlayFeatured(void)
{
	const UGCProjectVersion *pVer = UGCProject_GetMostRecentPublishedVersion(gclUGC_GetChosenProject());
	if (!pVer)
		return;

	printf("Playing Featured UGC mission %s\n", pVer->pNameSpace);
	ServerCmd_gslUGC_PlayProjectNonEditor(pVer->pNameSpace,
										  pVer->pCostumeOverride,
										  pVer->pPetOverride,
										  pVer->pBodyText,
										  /*bPlayingAsBetaReviewer=*/false);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Reviewing is enabled. Gets set whenever we request mission details, or reviewerOverview

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_reviewingIsEnabled);
bool gclUGC_reviewingIsEnabled(void)
{
	return(sbReviewingIsEnabled);
}


//////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Details Requests

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_RequestDetailsByID);
void gclUGC_RequestDetailsByID(int iProjectID, int iProjectSeriesID)
{
	if(iProjectID || iProjectSeriesID)
	{
		StructDestroySafe(parse_UGCProject, &spDetailProject);
		StructDestroySafe(parse_UGCProjectSeries, &spDetailProjectSeries);
		StructDestroyNoConstSafe(parse_UGCProjectReviews, &spDetailReviews);
		ServerCmd_gslUGC_RequestDetails(iProjectID, iProjectSeriesID, UGC_DETAILREQUEST_FOR_MISSIONDISPLAY);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_RequestDetails);
void gclUGC_RequestDetails(SA_PARAM_OP_VALID UGCProject *pProject)
{
	if(pProject)
	{
		gclUGC_RequestDetailsByID(pProject->id, 0);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_RequestDetailsByResourceName);
void gclUGC_RequestDetailsByResourceName(const char* pchResourceName)
{
	if(pchResourceName)
	{
		gclUGC_RequestDetailsByID(UGCProject_GetContainerIDFromUGCNamespace(pchResourceName), 0);
	}
}

///////////////////////////////
// Receivers

// WOLF[6Dec11] This function gets called when the server sends details for a
//   particular UGC project. Either when we are displaying in the journal (both Available and In Progress),
//   or when a reviewable mission completes and we are determining
//   if we should pop up the review box. The requesterID will specify
//   which of these cases we are in

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclUGC_ReceiveDetails(UGCDetails* pDetails, S32 iProjectReviewPageCount, int iSeriesReviewPageCount,
						   bool bReviewingEnabled, S32 iRequesterID)
{
	sbReviewingIsEnabled=bReviewingEnabled;	// This is an AUTO_SETTING on the GameServer side.

	if (pDetails)
	{
		if (iRequesterID==UGC_DETAILREQUEST_FOR_REVIEW)
		{
			// s_bMissionToReviewQueue could have been cleared if
			// someone chose to edit a reviewable mission from the
			// journal selection while we were waiting for our details
			// to arrive
			if( s_bMissionToReviewQueued )
			{
				StructDestroySafe(parse_UGCProject, &spReviewableProject);
				spReviewableProject = StructClone(parse_UGCProject, pDetails->pProject);
				if( spReviewableProject ) {
					eaQSort((UGCSingleReview**)spReviewableProject->ugcReviews.ppReviews, ugcReviews_SortByTimestamp);
				}

				StructDestroySafe( parse_UGCProjectSeries, &spReviewableProjectSeries );
				spReviewableProjectSeries = StructClone( parse_UGCProjectSeries, pDetails->pSeries );
				if( spReviewableProjectSeries ) {
					eaQSort( (UGCSingleReview**)spReviewableProjectSeries->ugcReviews.ppReviews, ugcReviews_SortByTimestamp );
				}
			
				if( spReviewableProject && (entGetAccountID(entActivePlayerPtr()) == spReviewableProject->iOwnerAccountID)
					// If the project has a series, then it needs to
					// show the review gen to allow the Series to be
					// continued.  Don't worry, the Review gen should
					// disallow reviews.
					&& !spReviewableProject->seriesID) 
				{
					// We can't review our own project. So just do nothing.
					const UGCProjectVersion* pVersion = UGCProject_GetMostRecentPublishedVersion(spReviewableProject);
				
					printf( "Ignoring review request for project %s because you own it.\n",
							UGCProject_GetVersionName(spReviewableProject, pVersion ));

				}
				else if (bReviewingEnabled)
				{
					globCmdParse( "ugcShowReviewGen" );
				}
				else
				{
					// Tell the player they can't review now
	
					char* estrMessage = NULL;
					const char* pchMessageKey;
					Entity *pEntity = entActivePlayerPtr();
					NotifyType eNotifyType;
					estrStackCreate(&estrMessage);
					pchMessageKey = "UGC.ReviewsDisabledForPopup";
					eNotifyType = kNotifyType_UGCError;
					
					entFormatGameMessageKey(pEntity, &estrMessage, pchMessageKey, STRFMT_END);
				
					if (estrMessage && estrMessage[0])
					{
						notify_NotifySend(pEntity, eNotifyType, estrMessage, NULL, NULL);
					}
					estrDestroy(&estrMessage);
				}
				s_bMissionToReviewQueued = false;
			}
		}
		else
		{
			//UGC_DETAILREQUEST_FOR_MISSIONDISPLAY,
			StructDestroySafe(parse_UGCProject, &spDetailProject);
			StructDestroySafe(parse_UGCProjectSeries, &spDetailProjectSeries);
			StructDestroyNoConstSafe(parse_UGCProjectReviews, &spDetailReviews);

			if( pDetails->pProject ) {
				spDetailProject = StructClone(parse_UGCProject, pDetails->pProject);
				eaQSort((UGCSingleReview**)spDetailProject->ugcReviews.ppReviews, ugcReviews_SortByTimestamp);
				spDetailReviews = StructCloneDeConst(parse_UGCProjectReviews, &spDetailProject->ugcReviews);
			} else if( pDetails->pSeries ) {
				spDetailProjectSeries = StructClone( parse_UGCProjectSeries, pDetails->pSeries );
				eaQSort((UGCSingleReview**)spDetailProjectSeries->ugcReviews.ppReviews, ugcReviews_SortByTimestamp);
				spDetailReviews = StructCloneDeConst(parse_UGCProjectReviews, &spDetailProjectSeries->ugcReviews);
			}
			s_iCurrentReviewsPage = 0;
		}
	}
	else
	{
		// MJF TODO: !!!: Do something better here. <NPK 2010-04-28>
		Errorf("Error getting review copy\n");
	}
}

////////////////////////////////////////////////////////////
//  Detail/review stuff

// This gets called when reviews are added, modified, deleted or hidden.
//We need to rerequest so we are displaying the right thing
AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclUGC_ReviewsChanged(U32 uProjectID, U32 uSeriesID)
{
	// If project ID matches the details one, re-get reviews
	// Can happen if a review is added, modified, deleted or hidden
	if ((spDetailProject && spDetailProject->id == uProjectID)
		|| (spDetailProjectSeries && spDetailProjectSeries->id == uSeriesID))
	{
		// Rerequest the details
		gclUGC_RequestDetailsByID(uProjectID, uSeriesID);
	}
	// Don't worry if it was not the details project or series.
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_detail_GetCurrentReviewPage);
S32 gclUGC_detail_GetCurrentReviewPage(void)
{
	return s_iCurrentReviewsPage;
}

#define UGC_REVIEWS_REQUEST_TIMEOUT 5
static bool gclUGC_ReviewsCanRequestNextPage_Internal(bool bUpdateRequest)
{
	if ((spDetailProject || spDetailProjectSeries) && spDetailReviews)
	{
		UGCExtraDetailData *pExtraDetailData = spDetailProject ? spDetailProject->pExtraDetailData : spDetailProjectSeries->pExtraDetailData;

		S32 iNextPage = s_iCurrentReviewsPage + 1;
		if (iNextPage < pExtraDetailData->iNumReviewPages)
		{
			static U32 s_uNextRequestTime = 0;
			static S32 s_iCurrentRequestPage = -1;
			U32 uCurrentTime = timeSecondsSince2000();
			
			if (s_iCurrentRequestPage >= 0 && 
				s_iCurrentReviewsPage != s_iCurrentRequestPage && 
				uCurrentTime < s_uNextRequestTime)
			{
				return false;
			}
			if (bUpdateRequest)
			{
				s_uNextRequestTime = uCurrentTime + UGC_REVIEWS_REQUEST_TIMEOUT;
				s_iCurrentRequestPage = iNextPage;
			}
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_detail_ReviewsCanRequestNextPage);
bool gclUGC_detail_ReviewsCanRequestNextPage(void)
{
	return gclUGC_ReviewsCanRequestNextPage_Internal(false);
}

static void gclUGC_ReceiveReviewsNextPage(U32 uProjectID, U32 uSeriesID, int iPageNumber, UGCProjectReviews *pReviews)
{
	if (pReviews && spDetailReviews)
	{
		if((spDetailProject && spDetailProject->id == uProjectID) || (spDetailProjectSeries && spDetailProjectSeries->id == uSeriesID))
			ugcEditorUpdateReviews(spDetailReviews, &s_iCurrentReviewsPage, pReviews, iPageNumber);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_detail_ReviewsRequestNextPage);
bool gclUGC_detail_ReviewsRequestNextPage(void)
{
	if (gclUGC_ReviewsCanRequestNextPage_Internal(true))
	{
		S32 iNextPage = s_iCurrentReviewsPage+1;
		gclUGC_RequestReviewsForPage(spDetailProject ? spDetailProject->id : 0, spDetailProjectSeries ? spDetailProjectSeries->id : 0, iNextPage);
		return true;
	}
	return false;
}

static ContainerID gclUGC_NextProjectID1( bool* found, CONST_EARRAY_OF(UGCProjectSeriesNode) eaNodes, ContainerID proj )
{
	int it;
	for( it = 0; it != eaSize( &eaNodes ); ++it ) {
		const UGCProjectSeriesNode* node = eaNodes[ it ];

		if( node->iProjectID ) {
			if( *found ) {
				return node->iProjectID;
			} else if( node->iProjectID == proj ) {
				*found = true;
			}
		} else {
			ContainerID id = gclUGC_NextProjectID1( found, node->eaChildNodes, proj );
			if( id ) {
				return id;
			}
		}
	}

	return 0;
}

static ContainerID gclUGC_NextProjectID( UGCProject* proj )
{
	if( proj && proj->seriesID ) {
		UGCProjectSeries* projSeries = gclUGC_CacheGetProjectSeries( proj->seriesID );
		if( projSeries && eaSize( &projSeries->eaVersions ) == 1 ) {
			bool found = false;
			return gclUGC_NextProjectID1( &found, projSeries->eaVersions[ 0 ]->eaChildNodes, proj->id );
		}
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_HasNextProject);
bool gclUGC_HasNextProject( SA_PARAM_OP_VALID UGCProject* proj )
{
	return gclUGC_NextProjectID( proj ) != 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_NextProject);
SA_ORET_OP_VALID UGCProject* gclUGC_NextProject( SA_PARAM_OP_VALID UGCProject* proj )
{
	return gclUGC_CacheGetProject( gclUGC_NextProjectID( proj ));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_PlayNextProject);
void gclUGC_detail_PlayNextProject( SA_PARAM_OP_VALID UGCProject* proj )
{
	const UGCProjectVersion *pVer;

	if( !proj ) {
		return;
	}
	
	pVer = UGCProject_GetMostRecentPublishedVersion(gclUGC_NextProject( proj ));
	if(!pVer) {
		return;
	}

	printf("Playing UGC misson %s\n", pVer->pNameSpace);
	ServerCmd_gslUGC_PlayProjectNonEditor(pVer->pNameSpace,
										  pVer->pCostumeOverride,
										  pVer->pPetOverride,
										  pVer->pBodyText,
										  /*bPlayingAsBetaReviewer=*/false);
}

static ContainerID gclUGC_SeriesFirstProjectID1( CONST_EARRAY_OF(UGCProjectSeriesNode) eaNodes )
{
	const UGCProjectSeriesNode* node = eaGet( &eaNodes, 0 );
	if( node ) {
		if( node->iProjectID ) {
			return node->iProjectID;
		} else {
			return gclUGC_SeriesFirstProjectID1( node->eaChildNodes );
		}
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_SeriesFirstProjectID);
SA_ORET_OP_VALID ContainerID gclUGC_SeriesFirstProjectID( SA_PARAM_OP_VALID UGCProjectSeries* series )
{
	if( series ) {
		UGCProjectSeriesVersion* version = eaGet( &series->eaVersions, 0 );
		if( version ) {
			return gclUGC_SeriesFirstProjectID1( version->eaChildNodes );
		}
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetSeriesByProject);
SA_ORET_OP_VALID UGCProjectSeries* gclUGC_GetSeriesByProject( SA_PARAM_OP_VALID UGCProject* proj )
{
	if( proj ) {
		return gclUGC_CacheGetProjectSeries( proj->seriesID );
	} else {
		return NULL;
	}
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_detail_GetReviewList);
void gclUGC_detail_GetReviewList(SA_PARAM_NN_VALID UIGen *pGen)
{
	UGCSingleReview **ppReviews = NULL;
	if(spDetailReviews)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(spDetailReviews->ppReviews, NOCONST(UGCSingleReview), pReview)
			if(pReview->pComment && pReview->pComment[0])
				eaPush(&ppReviews, StructCloneReConst(parse_UGCSingleReview, pReview));
		FOR_EACH_END
	}
	ui_GenSetManagedListSafe(pGen, &ppReviews, UGCSingleReview, true);
	eaDestroy(&ppReviews);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_detail_GetReviewListEx);
void gclUGC_detail_GetReviewListEx(SA_PARAM_NN_VALID UIGen *pGen, bool bHideSelfReview)
{
	UGCSingleReview **ppReviews = NULL;
	if(spDetailReviews)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(spDetailReviews->ppReviews, NOCONST(UGCSingleReview), pReview) {
			if(pReview->pComment && pReview->pComment[0]) {
				if( !bHideSelfReview || pReview->iReviewerAccountID != entGetAccountID(entActivePlayerPtr())) {
					eaPush(&ppReviews, StructCloneReConst(parse_UGCSingleReview, pReview));
				}
			}
		} FOR_EACH_END;
	}
	ui_GenSetManagedListSafe(pGen, &ppReviews, UGCSingleReview, true);
	eaDestroy(&ppReviews);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_detail_GetReviewCount);
S32 gclUGC_detail_GetReviewCount(bool bRequireComments)
{
	S32 iCount = 0;
	if(spDetailReviews)
	{
		if (bRequireComments)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(spDetailReviews->ppReviews, NOCONST(UGCSingleReview), pReview)
				if(pReview->pComment && pReview->pComment[0])
					iCount++;
			FOR_EACH_END
		}
		else
		{
			iCount = eaSize(&spDetailReviews->ppReviews);
		}
	}
	return iCount;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_detail_PlayerHasReview);
bool gclUGC_detail_PlayerHasReview(void)
{
	if( spDetailProject ) {
		return SAFE_MEMBER( spDetailProject->pExtraDetailData, pReviewForCurAccount ) != NULL;
	} else if( spDetailProjectSeries ) {
		return SAFE_MEMBER( spDetailProjectSeries->pExtraDetailData, pReviewForCurAccount ) != NULL;
	} else {
		return false;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_detail_GetPlayerReview);
void gclUGC_detail_GetPlayerReview(SA_PARAM_NN_VALID UIGen* pGen)
{
	UGCSingleReview* pReview = NULL;
	if( spDetailProject ) {
		pReview = SAFE_MEMBER( spDetailProject->pExtraDetailData, pReviewForCurAccount );
	} else if( spDetailProjectSeries ) {
		pReview = SAFE_MEMBER( spDetailProjectSeries->pExtraDetailData, pReviewForCurAccount );
	}

	ui_GenSetPointer( pGen, pReview, parse_UGCSingleReview );
}


//////////////////////////////////////////////////////////////////////
// Queries

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetDetailInfo);
SA_ORET_OP_VALID UGCProject* gclUGC_GetDetailInfo(void)
{
	return spDetailProject;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetDetailSeriesInfo);
SA_ORET_OP_VALID UGCProjectSeries* gclUGC_GetDetailSeriesInfo(void)
{
	return spDetailProjectSeries;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetReviewableInfo);
SA_ORET_OP_VALID UGCProject* gclUGC_GetReviewableInfo(void)
{
	return spReviewableProject;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetReviewableSeriesInfo);
SA_ORET_OP_VALID UGCProjectSeries* gclUGC_GetReviewableSeriesInfo(void)
{
	return spReviewableProjectSeries;
}

/////////////////

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_Name);
const char *gclUGC_Name(SA_PARAM_OP_VALID UGCProject *pProject)
{
	if (pProject)
	{
		const UGCProjectVersion* pVersion = UGCProject_GetMostRecentPublishedVersion(pProject);
		return UGCProject_GetVersionName(pProject, pVersion);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_NameSeries);
const char *gclUGC_NameSeries(SA_PARAM_OP_VALID UGCProjectSeries *pSeries)
{
	if (pSeries) {
		const UGCProjectSeriesVersion* pVersion = UGCProjectSeries_GetMostRecentPublishedVersion(pSeries);
		if( pVersion ) {
			return pVersion->strName;
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ContentName);
const char* gclUGC_ContentInfoName(SA_PARAM_OP_VALID UGCContentInfo* pInfo )
{
	if( pInfo ) {
		if( pInfo->iUGCProjectID ) {
			return gclUGC_Name( gclUGC_CacheGetProject( pInfo->iUGCProjectID ));
		} else if( pInfo->iUGCProjectSeriesID ) {
			return gclUGC_NameSeries( gclUGC_CacheGetProjectSeries( pInfo->iUGCProjectSeriesID ));
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_Author);
const char *gclUGC_Author(SA_PARAM_OP_VALID UGCProject *pProject)
{
	if(pProject)
		return pProject->pOwnerAccountName;
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_AuthorSeries);
const char *gclUGC_AuthorSeries(SA_PARAM_OP_VALID UGCProjectSeries *pSeries)
{
	if(pSeries)
		return pSeries->strOwnerAccountName;
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ContentAuthor);
const char *gclUGC_ContentInfoAuthor(SA_PARAM_OP_VALID UGCContentInfo* pInfo )
{
	if( pInfo ) {
		if( pInfo->iUGCProjectID ) {
			return gclUGC_Author( gclUGC_CacheGetProject( pInfo->iUGCProjectID ));
		} else if( pInfo->iUGCProjectSeriesID ) {
			return gclUGC_AuthorSeries( gclUGC_CacheGetProjectSeries( pInfo->iUGCProjectSeriesID ));
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ContentAuthorID);
int gclUGC_ContentInfoAuthorID(SA_PARAM_OP_VALID UGCContentInfo* pInfo )
{
	if( pInfo ) {
		if( pInfo->iUGCProjectID ) {
			UGCProject* pProject = gclUGC_CacheGetProject( pInfo->iUGCProjectID );
			return SAFE_MEMBER( pProject, iOwnerAccountID );
		} else if( pInfo->iUGCProjectSeriesID ) {
			UGCProjectSeries* pProjectSeries = gclUGC_CacheGetProjectSeries( pInfo->iUGCProjectSeriesID );
			return SAFE_MEMBER( pProjectSeries, iOwnerAccountID );
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_IsAuthorMoonstar);
bool gclUGC_ContentInfoAuthorIsMoonstar(SA_PARAM_OP_VALID UGCContentInfo* pInfo )
{
	// TODO: Hook this up to something on the Author info
	// Moonstars have the following product: PRD-NW-Beta-UGC
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_IsAuthorSilverstar);
bool gclUGC_ContentInfoAuthorSilverstar(SA_PARAM_OP_VALID UGCContentInfo* pInfo )
{
	// TODO: Hook this up to something on the Author info
	// Silverstars have the following product: PRD-NW-Beta-UGC-Phase2
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetProjectDescription);
const char* gclUGC_GetProjectDescription(ExprContext *pContext, SA_PARAM_OP_VALID UGCProject *pProject)
{
	char* estrResult = NULL;
	char *result = NULL;

	if(!pProject)
		return "";

	estrCreate(&estrResult);

	{
		const UGCProjectVersion* version = UGCProject_GetMostRecentPublishedVersion( pProject );
		if( version ) {
			// MJF TODO: Make this work without being connected to a GameServer.
			Entity* pEnt = entActivePlayerPtr();
			char* smfDescription = ugcAllocSMFString( version->pDescription, true );
			FormatGameString(&estrResult, smfDescription, STRFMT_ENTITY_KEY("Entity", pEnt), STRFMT_END);
			StructFreeStringSafe( &smfDescription );
		}
	}

	if (estrResult)
	{
		result = exprContextAllocScratchMemory(pContext, strlen(estrResult) + 1);
		memcpy(result, estrResult, strlen(estrResult) + 1);
		estrDestroy(&estrResult);
	}
	return NULL_TO_EMPTY(result);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetSeriesDescription);
const char* gclUGC_GetSeriesDescription(ExprContext *pContext, SA_PARAM_OP_VALID UGCProjectSeries* pSeries)
{
	char* estrResult = NULL;
	char *result = NULL;

	if(!pSeries)
		return "";

	estrCreate(&estrResult);

	{
		const UGCProjectSeriesVersion* version = UGCProjectSeries_GetMostRecentPublishedVersion( pSeries );
		if( version ) {
			// MJF TODO: Make this work without being connected to a GameServer.
			Entity* pEnt = entActivePlayerPtr();
			char* smfDescription = ugcAllocSMFString( version->strDescription, true );
			FormatGameString(&estrResult, smfDescription, STRFMT_ENTITY_KEY("Entity", pEnt), STRFMT_END);
			StructFreeStringSafe( &smfDescription );
		}
	}

	if (estrResult)
	{
		result = exprContextAllocScratchMemory(pContext, strlen(estrResult) + 1);
		memcpy(result, estrResult, strlen(estrResult) + 1);
		estrDestroy(&estrResult);
	}
	return NULL_TO_EMPTY(result);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ContentDescription);
const char* gclUGC_ContentInfoDescription(ExprContext* pContext, SA_PARAM_OP_VALID UGCContentInfo* pInfo )
{
	if( pInfo ) {
		if( pInfo->iUGCProjectID ) {
			return gclUGC_GetProjectDescription( pContext, gclUGC_CacheGetProject( pInfo->iUGCProjectID ));
		} else if( pInfo->iUGCProjectSeriesID ) {
			return gclUGC_GetSeriesDescription( pContext, gclUGC_CacheGetProjectSeries( pInfo->iUGCProjectSeriesID ));
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetContentInfoFeaturedData);
SA_ORET_OP_VALID UGCFeaturedData* gclUGC_GetContentInfoFeaturedData(SA_PARAM_OP_VALID UGCContentInfo* pInfo)
{
	UGCFeaturedData* pFeatured = NULL;

	if( pInfo ) {
		if( pInfo->iUGCProjectID ) {
			UGCProject* proj = gclUGC_CacheGetProject( pInfo->iUGCProjectID );
			const UGCProjectVersion* version = UGCProject_GetMostRecentPublishedVersion( proj );
			if( version ) {
				pFeatured = SAFE_MEMBER( proj, pFeatured );
			}
		}
	}

	// Only return the featured data if it was ever active
	if( pFeatured && timeSecondsSince2000() > pFeatured->iStartTimestamp ) {
		return pFeatured;
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GenSetContentInfoFeaturedData);
void gclUGC_GenSetContentInfoFeaturedData(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID UGCContentInfo* pInfo)
{
	UGCFeaturedData* pFeatured = gclUGC_GetContentInfoFeaturedData( pInfo );
	ui_GenSetPointer( pGen, pFeatured, parse_UGCFeaturedData );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetProjectRatingAvg, ugc_RatingAverage);
float gclUGC_GetProjectRatingAvg(SA_PARAM_OP_VALID UGCProject *pProject)
{
	if(pProject)
		return pProject->ugcReviews.fAverageRating;

	return 0.f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetProjectRatingAdj, ugc_RatingAdjusted);
float gclUGC_GetProjectRatingAdj(SA_PARAM_OP_VALID UGCProject *pProject)
{
	if(pProject)
		return pProject->ugcReviews.fAdjustedRatingUsingConfidence;

	return 0.f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetSeriesRatingAvg);
float gclUGC_GetSeriesRatingAvg(SA_PARAM_OP_VALID UGCProjectSeries* pSeries)
{
	return SAFE_MEMBER( pSeries, ugcReviews.fAverageRating );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetSeriesRatingAdj);
float gclUGC_GetSeriesRatingAdj(SA_PARAM_OP_VALID UGCProjectSeries* pSeries)
{
	return SAFE_MEMBER( pSeries, ugcReviews.fAdjustedRatingUsingConfidence );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ContentRatingAvg);
float gclUGC_ContentInfoRatingAvg(SA_PARAM_OP_VALID UGCContentInfo* pInfo)
{
	if( pInfo ) {
		if( pInfo->iUGCProjectID ) {
			return gclUGC_GetProjectRatingAvg( gclUGC_CacheGetProject( pInfo->iUGCProjectID ));
		} else if( pInfo->iUGCProjectSeriesID ) {
			return gclUGC_GetSeriesRatingAvg( gclUGC_CacheGetProjectSeries( pInfo->iUGCProjectSeriesID ));
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ContentRatingAdj);
float gclUGC_ContentInfoRatingAdj(SA_PARAM_OP_VALID UGCContentInfo* pInfo)
{
	if( pInfo ) {
		if( pInfo->iUGCProjectID ) {
			return gclUGC_GetProjectRatingAdj( gclUGC_CacheGetProject( pInfo->iUGCProjectID ));
		} else if( pInfo->iUGCProjectSeriesID ) {
			return gclUGC_GetSeriesRatingAdj( gclUGC_CacheGetProjectSeries( pInfo->iUGCProjectSeriesID ));
		}
	}
	return 0;
}

static int gclUGC_SeriesNodesTimeToComplete( CONST_EARRAY_OF(UGCProjectSeriesNode) eaNodes )
{
	int accum = 0;
	int it;
	for( it = 0; it != eaSize( &eaNodes ); ++it ) {
		const UGCProjectSeriesNode* node = eaNodes[ it ];
		if( node->iProjectID ) {
			UGCProject* project = gclUGC_CacheGetProject( node->iProjectID );
			if( project ) {
				accum += UGCProject_AverageDurationInMinutes(project);
			}
		}
		accum += gclUGC_SeriesNodesTimeToComplete( node->eaChildNodes );
	}

	return accum;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ContentDurationAvgInMinutes);
int gclUGC_ContentInfoDurationAvgInMinutes(SA_PARAM_OP_VALID UGCContentInfo* pInfo)
{
	if( pInfo ) {
		if( pInfo->iUGCProjectID ) {
			const UGCProject* pProject = gclUGC_CacheGetProject( pInfo->iUGCProjectID );
			if( pProject ) {
				return UGCProject_AverageDurationInMinutes(pProject);
			}
		} else if( pInfo->iUGCProjectSeriesID ) {
			const UGCProjectSeriesVersion* pVersion = UGCProjectSeries_GetMostRecentPublishedVersion( gclUGC_CacheGetProjectSeries( pInfo->iUGCProjectSeriesID ));
			if( pVersion ) {
				return gclUGC_SeriesNodesTimeToComplete( pVersion->eaChildNodes );
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ProjectNumPlays);
int gclUGC_ProjectNumPlays(SA_PARAM_OP_VALID UGCProject* pProject)
{
	if( pProject ) {
		return UGCProject_GetTotalPlayedCount(pProject);
	}
	return 0;
}

static void gclUGC_SeriesNodesAccumMapLocations( UGCContentInfoWithMapLocation*** peaAccum, CONST_EARRAY_OF(UGCProjectSeriesNode) eaNodes )
{
	int it;
	for( it = 0; it != eaSize( &eaNodes ); ++it ) {
		const UGCProjectSeriesNode* node = eaNodes[ it ];
		if( node->iProjectID ) {
			const UGCProjectVersion* pVersion = UGCProject_GetMostRecentPublishedVersion( gclUGC_CacheGetProject( node->iProjectID ));
			if( pVersion ) {
				UGCContentInfoWithMapLocation* data = StructCreate( parse_UGCContentInfoWithMapLocation );
				UGCMapLocation* mapLoc = ugcCreateMapLocation( pVersion->pMapLocation );
				data->contentInfo.iUGCProjectID = node->iProjectID;
				StructCopy( parse_UGCMapLocation, mapLoc, &data->mapLocation, 0, 0, 0 );
				eaPush( peaAccum, data );

				StructDestroySafe( parse_UGCMapLocation, &mapLoc );
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GenSetContentMapLocations);
void gclUGC_GenSetContentInfoMapLocations(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID UGCContentInfo* pInfo)
{
	eaClearStruct( &seaChosenMapLocations, parse_UGCContentInfoWithMapLocation );
	if( pInfo ) {
		if( pInfo->iUGCProjectID ) {
			const UGCProjectVersion* pVersion = UGCProject_GetMostRecentPublishedVersion( gclUGC_CacheGetProject( pInfo->iUGCProjectID ));
			if( pVersion && pVersion->pMapLocation) {
				UGCContentInfoWithMapLocation* data = StructCreate( parse_UGCContentInfoWithMapLocation );
				UGCMapLocation* mapLoc = ugcCreateMapLocation( pVersion->pMapLocation );
				data->contentInfo.iUGCProjectID = pInfo->iUGCProjectID;
				StructCopy( parse_UGCMapLocation, mapLoc, &data->mapLocation, 0, 0, 0 );
				eaPush( &seaChosenMapLocations, data );

				StructDestroySafe( parse_UGCMapLocation, &mapLoc );
			}
		} else if( pInfo->iUGCProjectSeriesID ) {
			const UGCProjectSeriesVersion* pVersion = UGCProjectSeries_GetMostRecentPublishedVersion( gclUGC_CacheGetProjectSeries( pInfo->iUGCProjectSeriesID ));
			if( pVersion ) {
				gclUGC_SeriesNodesAccumMapLocations( &seaChosenMapLocations, pVersion->eaChildNodes );
			}
		}
	}	
	ui_GenSetManagedListSafe(pGen, &seaChosenMapLocations, UGCContentInfoWithMapLocation, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetProjectIDString);
const char* gclUGC_GetProjectIDString(SA_PARAM_OP_VALID UGCProject *pProject)
{
	if(pProject)
	{
		return pProject->pIDString;
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GenSetSeriesChildren);
void gclUGC_GenSetSeriesChildren(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID UGCProjectSeries* pSeries)
{
	UGCContentInfo** eaAccum = NULL;
	const UGCProjectSeriesVersion* pVersion = UGCProjectSeries_GetMostRecentPublishedVersion( pSeries );
	if( pVersion ) {
		ContainerID* eaiProjects = NULL;
		int it;
		
		gclUGC_SeriesNodeProjectsInSpoilerOrder( &eaiProjects, pVersion->eaChildNodes );
		for( it = 0; it != eaiSize( &eaiProjects ); ++it ) {
			UGCContentInfo* info = StructCreate( parse_UGCContentInfo );
			info->iUGCProjectID = eaiProjects[ it ];
			eaPush( &eaAccum, info );
		}
		eaiDestroy( &eaiProjects );
	}

	ui_GenSetManagedListSafe( pGen, &eaAccum, UGCContentInfo, true );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetSeriesIDString);
const char* gclUGC_GetSeriesIDString(SA_PARAM_OP_VALID UGCProjectSeries* pSeries)
{
	return SAFE_MEMBER(pSeries, strIDString);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ContentIDString);
const char* gclUGC_GetContentIDString(SA_PARAM_OP_VALID UGCContentInfo *pInfo)
{
	if( pInfo ) {
		if( pInfo->iUGCProjectID ) {
			return gclUGC_GetProjectIDString( gclUGC_CacheGetProject( pInfo->iUGCProjectID ));
		} else if( pInfo->iUGCProjectSeriesID ) {
			return gclUGC_GetSeriesIDString( gclUGC_CacheGetProjectSeries( pInfo->iUGCProjectSeriesID ));
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_RatingCount);
int gclUGC_RatingCount(SA_PARAM_OP_VALID UGCProject *pProject)
{
	if(pProject)
		return pProject->ugcReviews.iNumRatingsCached;
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_RatingCountSeries);
int gclUGC_RatingCountSeries(SA_PARAM_OP_VALID UGCProjectSeries* pSeries)
{
	return SAFE_MEMBER(pSeries, ugcReviews.iNumRatingsCached);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ContentRatingCount);
int gclUGC_ContentInfoRatingCount(SA_PARAM_OP_VALID UGCContentInfo* pInfo )
{
	if( pInfo ) {
		if( pInfo->iUGCProjectID ) {
			return gclUGC_RatingCount( gclUGC_CacheGetProject( pInfo->iUGCProjectID ));
		} else if( pInfo->iUGCProjectSeriesID ) {
			return gclUGC_RatingCountSeries( gclUGC_CacheGetProjectSeries( pInfo->iUGCProjectSeriesID ));
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ContentImage);
const char* gclUGC_ContentInfoImage(SA_PARAM_OP_VALID UGCContentInfo* pInfo )
{
	if( pInfo ) {
		if( pInfo->iUGCProjectID ) {
			const UGCProjectVersion* pVersion = UGCProject_GetMostRecentPublishedVersion( gclUGC_CacheGetProject( pInfo->iUGCProjectID ));
			return SAFE_MEMBER( pVersion, pImage );
		} else if( pInfo->iUGCProjectSeriesID ) {
			const UGCProjectSeriesVersion* pVersion = UGCProjectSeries_GetMostRecentPublishedVersion( gclUGC_CacheGetProjectSeries( pInfo->iUGCProjectSeriesID ));
			return SAFE_MEMBER( pVersion, strImage );
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ContentCompletionTime);
int gclUGC_ContentInfoCompletionTime(SA_PARAM_OP_VALID UGCContentInfo* pInfo )
{
	if( pInfo ) {
		if( pInfo->iUGCProjectID ) {
			return gclUGC_ProjectCompletionTime( pInfo->iUGCProjectID );
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ContentCreatedTime);
int gclUGC_ContentInfoCreatedTime(SA_PARAM_OP_VALID UGCContentInfo* pInfo )
{
	if( pInfo ) {
		if( pInfo->iUGCProjectID ) {
			const UGCProject* pProject = gclUGC_CacheGetProject( pInfo->iUGCProjectID );
			return SAFE_MEMBER( pProject, iCreationTime );
		} else if( pInfo->iUGCProjectSeriesID ) {
			const UGCProjectSeries* pSeries = gclUGC_CacheGetProjectSeries( pInfo->iUGCProjectSeriesID );
			return SAFE_MEMBER( pSeries, iCreationTime );
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ContentLastModifiedTime);
int gclUGC_ContentInfoLastModifiedTime(SA_PARAM_OP_VALID UGCContentInfo* pInfo )
{
	if( pInfo ) {
		if( pInfo->iUGCProjectID ) {
			const UGCProjectVersion* pVersion = UGCProject_GetMostRecentPublishedVersion( gclUGC_CacheGetProject( pInfo->iUGCProjectID ));
			return SAFE_MEMBER( pVersion, iModTime );
		} else if( pInfo->iUGCProjectSeriesID ) {
			const UGCProjectSeriesVersion* pVersion = UGCProjectSeries_GetMostRecentPublishedVersion( gclUGC_CacheGetProjectSeries( pInfo->iUGCProjectSeriesID ));
			return SAFE_MEMBER( pVersion, sPublishTimeStamp.iTimestamp );
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ContentVisible);
bool gclUGC_ContentInfoVisible(SA_PARAM_OP_VALID UGCContentInfo* pInfo )
{
	if( pInfo ) {
		if( pInfo->iUGCProjectID ) {
			UGCProject* pProject = gclUGC_CacheGetProject( pInfo->iUGCProjectID );
			if( pProject && pProject->seriesID ) {
				return gclUGC_SeriesProjectIsVisible( pProject->seriesID, pProject->id );
			} else {
				return true;
			}
		} else if( pInfo->iUGCProjectSeriesID ) {
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_PlayerIsOwner);
bool gclUGC_PlayerIsOwner(SA_PARAM_OP_VALID UGCProject *pProject)
{
	U32 iAccountID = 0;
	
	if( linkConnected( gServerLink )) {
		iAccountID = entGetAccountID( entActivePlayerPtr() );
	} else if( linkConnected( gpLoginLink )) {
		iAccountID = LoginGetAccountID();
	}

	if( pProject && pProject->iOwnerAccountID == iAccountID ) {
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_PlayerIsOwnerSeries);
bool gclUGC_PlayerIsOwnerSeries(SA_PARAM_OP_VALID UGCProjectSeries* pSeries)
{
	U32 iAccountID = 0;
	
	if( linkConnected( gServerLink )) {
		iAccountID = entGetAccountID( entActivePlayerPtr() );
	} else if( linkConnected( gpLoginLink )) {
		iAccountID = LoginGetAccountID();
	}

	if( pSeries && pSeries->iOwnerAccountID == iAccountID ) {
		return true;
	}
	return false;
}

static bool gclUGC_CanReviewProjectID( ContainerID projectID )
{
	if(   gclUGC_PlayerHasUGCMission( projectID )
		  || gclUGC_PlayerJustCompletedOrDroppedUGCMission( projectID )
		  || entity_HasCompletedUGCProject( entActivePlayerPtr(), projectID )) {
		return true;
	} else {
		return false;
	}
}

static int gclUGC_CanReviewSeriesNodeCount( CONST_EARRAY_OF(UGCProjectSeriesNode) eaNodes )
{
	int accum = 0;
	
	int it;
	for( it = 0; it != eaSize( &eaNodes ); ++it ) {
		const UGCProjectSeriesNode* node = eaNodes[ it ];
		if( node->iProjectID && gclUGC_CanReviewProjectID( node->iProjectID )) {
			++accum;
		} else {
			accum += gclUGC_CanReviewSeriesNodeCount( node->eaChildNodes );
		} 
	}

	return accum;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_CanReview);
bool gclUGC_CanReview(SA_PARAM_OP_VALID UGCProject *pProject)
{
	// MJF TODO: make this work without being connected to a GameServer
	Entity* pEnt = entActivePlayerPtr();

	if (!pEnt || !pProject)
	{
		return false;
	}
	if (gclUGC_PlayerIsOwner(pProject))
	{
		return(false);
	}

	return gclUGC_CanReviewProjectID( pProject->id );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_CanReviewSeries);
bool gclUGC_CanReviewSeries(SA_PARAM_OP_VALID UGCProjectSeries* pSeries)
{
	// MJF TODO: make this work without being connected to a GameServer
	Entity* pEnt = entActivePlayerPtr();

	if (!pEnt || !pSeries)
	{
		return false;
	}
	if (gclUGC_PlayerIsOwnerSeries(pSeries))
	{
		return(false);
	}

	{
		const UGCProjectSeriesVersion* version = UGCProjectSeries_GetMostRecentPublishedVersion( pSeries );
		if( version && gclUGC_CanReviewSeriesNodeCount( version->eaChildNodes ) >= 1 ) {
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_NumRatingsAtValue);
int gclUGC_NumRatingsAtValue(SA_PARAM_OP_VALID UGCProject* pProject, float fValue)
{
	int iCount = 0;
	if(pProject && ea32Size(&pProject->ugcReviews.piNumRatings) == UGCPROJ_NUM_RATING_BUCKETS)
	{
		S32 iBucket = ugcReviews_FindBucketForRating( fValue );
		iCount = ea32Get( &pProject->ugcReviews.piNumRatings, iBucket );
	}
	return iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_NumRatingsAtValueSeries);
int gclUGC_NumRatingsAtValueSeries(SA_PARAM_OP_VALID UGCProjectSeries* pSeries, float fValue)
{
	int iCount = 0;
	if(pSeries && ea32Size(&pSeries->ugcReviews.piNumRatings) == UGCPROJ_NUM_RATING_BUCKETS)
	{
		S32 iBucket = ugcReviews_FindBucketForRating( fValue );
		iCount = ea32Get( &pSeries->ugcReviews.piNumRatings, iBucket );
	}
	return iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ProjectInProgress);
bool gclUGC_ProjectInProgress(SA_PARAM_OP_VALID UGCProject *pProject)
{
	return gclUGC_PlayerHasUGCMission( SAFE_MEMBER( pProject, id ));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_PlayerHasReviewedProject);
bool gclUGC_PlayerHasReviewedProject(SA_PARAM_OP_VALID UGCProject *pProject)
{
	const UGCSingleReview* pReview = SAFE_MEMBER2( pProject, pExtraDetailData, pReviewForCurAccount );
	return (pReview != NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_PlayerHasReviewedProjectSeries);
bool gclUGC_PlayerHasReviewedProjectSeries( SA_PARAM_OP_VALID UGCProjectSeries* pSeries)
{
	const UGCSingleReview* pReview = SAFE_MEMBER2( pSeries, pExtraDetailData, pReviewForCurAccount );
	return (pReview != NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_PlayerComment);
const char* gclUGC_PlayerComment(SA_PARAM_OP_VALID UGCProject *pProject)
{
	const UGCSingleReview *pReview = SAFE_MEMBER2( pProject, pExtraDetailData, pReviewForCurAccount );
	if(pReview && pReview->pComment) {
		return pReview->pComment;
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_PlayerCommentSeries);
const char* gclUGC_PlayerCommentSeries(SA_PARAM_OP_VALID UGCProjectSeries* pSeries)
{
	const UGCSingleReview *pReview = SAFE_MEMBER2( pSeries, pExtraDetailData, pReviewForCurAccount );
	if(pReview && pReview->pComment) {
		return pReview->pComment;
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_PlayerRating);
float gclUGC_PlayerRating(SA_PARAM_OP_VALID UGCProject *pProject)
{
	const UGCSingleReview *pReview = SAFE_MEMBER2( pProject, pExtraDetailData, pReviewForCurAccount );
	if(pReview) {
		return pReview->fRating;
	}

	return 0.f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_PlayerRatingSeries);
float gclUGC_PlayerRatingSeries(SA_PARAM_OP_VALID UGCProjectSeries* pSeries)
{
	const UGCSingleReview *pReview = SAFE_MEMBER2( pSeries, pExtraDetailData, pReviewForCurAccount );
	if(pReview) {
		return pReview->fRating;
	}

	return 0.f;
}

/////////////////////////////////////////////////////////////
// Add a Review

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_AddReview);
void gclUGC_AddReview(SA_PARAM_OP_VALID UGCProject *pProject, float fRating, char *pComment)
{
	// Note: If a different mission was completed or dropped and we haven't yet been informed through the
	//  "maybe" function, this call may fail since the pInfo->uLastMissionRatingRequestID may already have 
	//  been set to the new project on the server side.

	if (pProject && gclUGC_CanReview(pProject))
	{
		const UGCProjectVersion* pVersion = UGCProject_GetMostRecentPublishedVersion(pProject);
		ServerCmd_gslUGC_AddReview(pProject->id,
			UGCProject_GetVersionName(pProject, pVersion),
			fRating, 
			pComment);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_AddReviewSeries);
void gclUGC_AddReviewSeries(SA_PARAM_OP_VALID UGCProjectSeries* pSeries, float fRating, char* pComment)
{
	if( pSeries && gclUGC_CanReviewSeries( pSeries )) {
		const UGCProjectSeriesVersion* pVersion = UGCProjectSeries_GetMostRecentPublishedVersion(pSeries);
		ServerCmd_gslUGC_AddReviewSeries( pSeries->id, pVersion->strName, fRating, pComment);
	}
}


/////////////////////////////////////////////////////////////
// UGC Subscriptions

static UGCAuthorSubscription* gclUGC_GetAuthorSubscription( ContainerID uAccountID )
{
	Entity* entity = entActivePlayerPtr();
	UGCAccount* ugcAccount;
	UGCPlayer* ugcPlayer;

	if( !entity || !entity->pPlayer) {
		return NULL;
	}
	
	ugcAccount = entGetUGCAccount(entity);
	if( !ugcAccount ) {
		return NULL;
	}

	ugcPlayer = eaIndexedGetUsingInt( &ugcAccount->eaPlayers, entGetContainerID( entity ));
	if( !ugcPlayer || !ugcPlayer->pSubscription ) {
		return NULL;
	}

	return eaIndexedGetUsingInt( &ugcPlayer->pSubscription->eaAuthors, uAccountID );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_SubscribeToAuthor);
void gclUGC_SubscribeToAuthor( ContainerID uAccountID, const char* strAccountName )
{
	ServerCmd_gslUGC_SubscribeToAuthor( uAccountID, strAccountName );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_UnsubscribeFromAuthor);
void gclUGC_UnsubscribeFromAuthor( ContainerID uAccountID, const char* strAccountName )
{
	ServerCmd_gslUGC_UnsubscribeFromAuthor( uAccountID, strAccountName );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_IsSubscribedToAuthor);
bool gclUGC_IsSubscribedToAuthor( ContainerID uAccountID )
{
	return gclUGC_GetAuthorSubscription( uAccountID ) != NULL;
}

/// Put into OUT_PEAPROJECTSINORDER a traversal of the projects in
/// SERIES.
///
/// Just a simple tree flatening.
void gclUGC_SeriesNodeProjectsInSpoilerOrder( ContainerID** out_peaProjectsInOrder, CONST_EARRAY_OF(UGCProjectSeriesNode) eaNodes )
{
	int it;
	for( it = 0; it != eaSize( &eaNodes ); ++it ) {
		const UGCProjectSeriesNode* node = eaNodes[ it ];
		if( node->iProjectID ) {
			eaiPush( out_peaProjectsInOrder, node->iProjectID );
		} else {
			gclUGC_SeriesNodeProjectsInSpoilerOrder( out_peaProjectsInOrder, node->eaChildNodes );
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_SeriesProjectIsVisible);
bool gclUGC_SeriesProjectIsVisible(U32 iSeries, U32 iProject)
{
	Entity* ent = entActivePlayerPtr();
	UGCProjectSeries* series = gclUGC_CacheGetProjectSeries( iSeries );
	if( series ) {
		UGCAuthorSubscription* pSubscription = gclUGC_GetAuthorSubscription( series->iOwnerAccountID );

		if( pSubscription ) {
			ContainerID* eaProjectsInOrder = NULL;
			int it;
			gclUGC_SeriesNodeProjectsInSpoilerOrder( &eaProjectsInOrder, series->eaVersions[ 0 ]->eaChildNodes );
			for( it = 0; it != eaiSize( &eaProjectsInOrder ); ++it ) {
				if( eaProjectsInOrder[ it ] == iProject ) {
					eaiDestroy( &eaProjectsInOrder );
					return true;
				}
				if( eaIndexedFindUsingInt( &pSubscription->eaCompletedProjects, eaProjectsInOrder[ it ]) < 0 ) {
					eaiDestroy( &eaProjectsInOrder );
					return false;
				}
			}
			
			eaiDestroy( &eaProjectsInOrder );
		}
	}

	return false;
}

U32 gclUGC_ProjectCompletionTime(U32 iProject)
{
	Entity* ent = entActivePlayerPtr();
	UGCProject* project = gclUGC_CacheGetProject( iProject );

	if( project ) {
		UGCAuthorSubscription* pSubscription = gclUGC_GetAuthorSubscription( project->iOwnerAccountID );
		if( pSubscription ) {
			UGCProjectSubscription* proj = eaIndexedGetUsingInt( &pSubscription->eaCompletedProjects, iProject );
			if( proj ) {
				return proj->completedTime;
			}
		}
	}

	return 0;
}


////////////////////////////////////////////////////////////////

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_IsResourceUGC);
bool gclUGC_IsResourceUGC(const char* pchResourceName)
{
	if(pchResourceName)
		return resNamespaceIsUGC(pchResourceName);

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_PlayerIsReviewer);
bool gclUGC_PlayerIsReviewer(void)
{
	GameAccountData* pData = NULL;
	
	if( linkConnected( gServerLink )) {
		pData = entity_GetGameAccount(entActivePlayerPtr());
	}

	if (gConf.bDontAllowGADModification)
		return gad_GetAccountValueInt(pData, GetAccountUgcReviewerKey());
	else
		return gad_GetAttribInt(pData, GetAccountUgcReviewerKey());
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_SetPlayerIsReviewer);
void gclUGC_SetPlayerIsReviewer(bool bIsReviewer)
{
	if( linkConnected( gServerLink )) { 
		ServerCmd_gslUGC_SetPlayerIsReviewer(bIsReviewer);
	} else {
		// Unsupported call path
		return;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_AreUGCGensEnabled);
bool gclUGC_AreUGCGensEnabled()
{
	return gConf.bEnableUGCUIGens && gConf.bUserContent;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_IsUGCSearchEULAAccepted);
bool gclUGC_IsUGCSearchEULAAccepted()
{
	GameAccountData* pData = NULL;
		
	if( linkConnected( gServerLink )) {
		pData = entity_GetGameAccount(entActivePlayerPtr());
	}
	
	if (gConf.bDontAllowGADModification)
		return gad_GetAccountValueInt(pData, GetAccountUgcProjectSearchEULAKey());
	else
		return gad_GetAttribInt(pData, GetAccountUgcProjectSearchEULAKey());
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_SetUGCSearchEULAAccepted);
void gclUGC_SetUGCSearchEULAAccepted(bool bAccepted)
{
	if( linkConnected( gServerLink )) {
		ServerCmd_gslUGC_SetUGCProjectSearchEULAAccepted(bAccepted);
	} else {
		// Unsupported call path
		return;
	}
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_PlayerHasCompletedProject);
bool gclUGC_PlayerHasCompletedProject(SA_PARAM_OP_VALID UGCProject* pProject)
{
	// MJF TODO: make this work without a GameServer
	Entity* pEnt = entActivePlayerPtr();
	ContainerID iProjectID = pProject ? pProject->id : 0;

	return entity_HasCompletedUGCProject(pEnt, iProjectID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_PlayerHasCompletedProjectSince);
bool gclUGC_PlayerHasCompletedProjectSince(SA_PARAM_OP_VALID UGCProject* pProject, U32 uTimeSince)
{
	// MJF TODO: make this work without a GameServer
	Entity* pEnt = entActivePlayerPtr();
	ContainerID iProjectID = pProject ? pProject->id : 0;

	return entity_HasCompletedUGCProjectSince(pEnt, iProjectID, uTimeSince);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_PlayerHasCompletedProjectSinceFeatured);
bool gclUGC_PlayerHasCompletedProjectSinceFeatured(SA_PARAM_OP_VALID UGCProject* pProject)
{
	// MJF TODO: make this work without a GameServer
	Entity* pEnt = entActivePlayerPtr();
	ContainerID iProjectID = pProject ? pProject->id : 0;
	U32 uFeatureStart = pProject && pProject->pFeatured ? pProject->pFeatured->iStartTimestamp : 0;

	return pProject && pProject->pFeatured && entity_HasCompletedUGCProjectSince(pEnt, iProjectID, uFeatureStart);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ProjectQualifiesForRewards);
bool gclUGC_QualifiesForRewards(SA_PARAM_OP_VALID UGCProject* pProject)
{
	return UGCProject_QualifiesForRewards(pProject);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_ProjectIsFeatured);
bool gclUGC_ProjectIsFeatured(SA_PARAM_OP_VALID UGCProject* pProject)
{
	return UGCProject_IsFeatured(ATR_EMPTY_ARGS, CONTAINER_NOCONST(UGCProject, pProject), gConf.bUGCPreviouslyFeaturedMissionsQualifyForRewards, false);
}

////////////////////////////////////////////////////////////////////////////
//  Reporting

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_CanReport);
bool gclUGC_CanReport(SA_PARAM_OP_VALID UGCProject *pProject)
{
	if( linkConnected( gServerLink )) {
		Entity* pEnt = entActivePlayerPtr();
		const U32 uAccountID = entGetAccountID(pEnt);

		if (!pEnt || !pProject)
		{
			return false;
		}
		if (!(gclUGC_PlayerHasUGCMission(pProject->id) ||
			  gclUGC_PlayerJustCompletedOrDroppedUGCMission(pProject->id) ||
			  entity_HasCompletedUGCProject(pEnt, pProject->id)))
		{
			return false;
		}
		if (UGCProject_FindReportByAccountID(pProject, uAccountID) >= 0)
		{
			return false;
		}
		return true;
	} else {
		return false;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_Report);
bool gclUGC_Report(SA_PARAM_OP_VALID UGCProject *pProject, U32 eReason, const char* pchDetails)
{
	// Reporting can only be done on a GameServer, so this is fine.
	Entity* pEnt = entActivePlayerPtr();
	const U32 uAccountID = entGetAccountID(pEnt);
	const UGCProjectVersion* pVersion = UGCProject_GetMostRecentPublishedVersion(pProject);
	
	if (pEnt && pProject &&
			(gclUGC_PlayerJustCompletedOrDroppedUGCMission(pProject->id) ||
			 gclUGC_PlayerHasUGCMission(pProject->id) ||
			 entity_HasCompletedUGCProject(pEnt, pProject->id)) && 
		UGCProject_CanMakeReport(pProject, uAccountID, eReason, pchDetails))
	{
		ServerCmd_gslUGC_Report(pProject->id, UGCProject_GetVersionName(pProject, pVersion), eReason, pchDetails);
		return true;
	}
	return false;
}


void gclUGC_RequestReviewsForPage(U32 uProjectID, U32 uSeriesID, int iPageNumber)
{
	s_requestReviewsInProgress = true;
		
	if( linkConnected( gServerLink )) {
		ServerCmd_gslUGC_RequestReviewsForPage(uProjectID, uSeriesID, iPageNumber);
	} else {
		Packet* pak = pktCreate(gpLoginLink, TOLOGIN_UGC_REQUEST_MORE_REVIEWS);
		pktSendU32(pak, uProjectID);
		pktSendU32(pak, 0);
		pktSendU32(pak, iPageNumber);
		pktSend(&pak);
	}
}

bool gclUGC_RequestReviewsInProgress(void)
{
	return s_requestReviewsInProgress;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclUGC_ReceiveReviewsForPage(U32 uProjectID, U32 uSeriesID, int iPageNumber, UGCProjectReviews* pReviews)
{
	s_requestReviewsInProgress = false;
	
	ugcProjectChooserReceiveMoreReviews(uProjectID, uSeriesID, iPageNumber, pReviews);
	ugcEditorReceiveMoreReviews(uProjectID, uSeriesID, iPageNumber, pReviews);
	gclUGC_ReceiveReviewsNextPage(uProjectID, uSeriesID, iPageNumber, pReviews);
}

AUTO_STRUCT;
typedef struct UGCReportReasonData 
{
    UGCProjectReportReason eReason; AST(NAME(Reason))
	const char* pchDisplayName;		AST(NAME(DisplayName))
} UGCReportReasonData;

static UGCReportReasonData** s_eaReasons = NULL;

static void gclUGC_GenerateReportReasonData(void)
{
	// Reporting can only be done while on a GameServer, so this is fine
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt && !s_eaReasons)
	{
		S32 i;
		S32* eaValues = NULL;
		DefineFillAllKeysAndValues(UGCProjectReportReasonEnum, NULL, &eaValues);
		for (i = 0; i < ea32Size(&eaValues); i++)
		{
			UGCReportReasonData* pData = StructCreate(parse_UGCReportReasonData);
			UGCProjectReportReason eReason = eaValues[i];
			Message* pMessage = StaticDefineGetMessage(UGCProjectReportReasonEnum, eReason);
			pData->pchDisplayName = StructAllocString(entTranslateMessage(pEnt, pMessage));
			pData->eReason = eReason;
			eaPush(&s_eaReasons, pData);
		}
		eaiDestroy(&eaValues);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetReportReasons);
S32 gclUGC_GetReportReasons(SA_PARAM_NN_VALID UIGen* pGen)
{
	gclUGC_GenerateReportReasonData();
	ui_GenSetList(pGen, &s_eaReasons, parse_UGCReportReasonData);
	return eaSize(&s_eaReasons);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetReportReasonDisplayName);
const char* gclUGC_GetReportReasonDisplayName(S32 eReason)
{
	UGCReportReasonData* pReasonData;
	gclUGC_GenerateReportReasonData();
	if (pReasonData = eaGet(&s_eaReasons, eReason))
	{
		return pReasonData->pchDisplayName;
	}
	return "";
}

static ContainerID scriptingProjID = 0;

static void gclUGC_ScriptingCacheUpdated( void )
{
	if( scriptingProjID ) {
		UGCProject* pProject = gclUGC_CacheGetProject( scriptingProjID );
		const UGCProjectVersion* pVer = UGCProject_GetMostRecentPublishedVersion( pProject );

		if( pVer ) {
			ServerCmd_gslUGC_PlayProjectNonEditor(pVer->pNameSpace,
												  pVer->pCostumeOverride, 
												  pVer->pPetOverride, 
												  pVer->pBodyText,
												  /*bPlayingAsBetaReviewer=*/false);
			ControllerScript_Succeeded();
		}
	}
}

void SearchAndChooseCB(UGCSearchResult *pList, void *pUserData)
{
	if (eaSize(&pList->eaResults) != 1 || !pList->eaResults[0]->iUGCProjectID)
	{
		ControllerScript_Failed(STACK_SPRINTF("Expected 1 UGC project returned from search. Received %d results", eaSize(&pList->eaResults)));
		return;
	}

	scriptingProjID = pList->eaResults[ 0 ]->iUGCProjectID;

	// Force it into the cache
	if( scriptingProjID ) {
		gclUGC_CacheGetProject( scriptingProjID );
	}
}


AUTO_COMMAND;
void UGCSearchAndImmediatelyChooseForScripting(char *pSimpleName)
{
	gclUGC_SetSearchResultCallback(SearchAndChooseCB, NULL);
	gclUGC_Search(pSimpleName);
	scriptingProjID = 0;
}

// Used by the UGC publish tester to select a contact dialog option
AUTO_COMMAND;
void UGCChooseFirstContactDialogEntryIfPossible(void)
{
	Entity* ent = entActivePlayerPtr();
	ContactDialog* entContactDialog = SAFE_MEMBER3( ent, pPlayer, pInteractInfo, pContactDialog );
	ContactRewardChoices rewardChoices = {0};

	if( !entContactDialog || eaSize( &entContactDialog->eaOptions ) == 0 ) {
		if( g_isContinuousBuilder ) {
			assertmsg( 0, "UGCChooseFirstContactDialogEntryIfPossible: There is no player or no contact dialog active on the player." );
		}
		return;
	}

	ServerCmd_ContactResponse( entContactDialog->eaOptions[0]->pchKey, &rewardChoices, 0 );
}

void gclClearAllUGCEditModeStuff(void)
{
	resClearAllDictionaryEditModes();
}

///////////////////////////////////////////////////////////////////////////////////////////////

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_PlayerCanReport);
bool gclUGC_PlayerCanReport( Entity* ent )
{
	return GamePermission_EntHasToken( ent, GAME_PERMISSION_UGC_CAN_REPORT_PROJECT );
}

// Set the reviewable mission to the one passed in.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_SetReviewableProject, ugc_SetReviewableMission);
void gclUGC_SetReviewableProject(SA_PARAM_OP_VALID UGCProject *pProject)
{
	if (pProject!=NULL)
	{
		if (pProject==spReviewableProject)
		{
			Errorf("Attempt to set reviewable mission to the current reviewable mission. This is not good.");
			return;
		}
		
		StructDestroySafe(parse_UGCProject, &spReviewableProject);
		StructDestroySafe(parse_UGCProjectSeries, &spReviewableProjectSeries);
		s_bMissionToReviewWasCompleted = false;
		s_bMissionToReviewQueued = false;
		spReviewableProject = StructClone(parse_UGCProject, pProject);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_SetReviewableSeries);
void gclUGC_SetReviewableSeries(SA_PARAM_OP_VALID UGCProjectSeries* pSeries)
{
	if( pSeries ) {
		if( pSeries == spReviewableProjectSeries ) {
			Errorf( "Attempt to set reviewable mission to the current reviewable mission. This is not good." );
			return;
		}
		
		StructDestroySafe(parse_UGCProject, &spReviewableProject);
		StructDestroySafe(parse_UGCProjectSeries, &spReviewableProjectSeries);
		s_bMissionToReviewWasCompleted = false;
		s_bMissionToReviewQueued = false;
		spReviewableProjectSeries = StructClone(parse_UGCProjectSeries, pSeries);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_reviewable_MissionWasDropped);
bool gclUGC_reviewable_MissionWasDropped(void)
{
	return !s_bMissionToReviewWasCompleted;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_reviewable_HasAuthor);
bool gclUGC_reviewable_HasAuthor(Entity* ent)
{
	if( ent && spReviewableProject ) {
		return entGetAccountID( ent ) == spReviewableProject->iOwnerAccountID;
	} else {
		return false;
	}
}

AUTO_COMMAND ACMD_NAME(ugc_MaybeShowReviewGen) ACMD_ACCESSLEVEL(0);
void gclUGC_MaybeShowReviewGen(const char* missionName, int iMissionWasCompleted)
{
	/// WOLF[11Jan12] This is called when a review notify comes through on Star Trek.
	// Do initial checks to see if we want to actially bring up the review box.
	// We can't check everything we need to here as we don't have details of the
	// mission. Fire off a details request, and do more checks in the receive.
	char namespaceName[ RESOURCE_NAME_MAX_SIZE ];
	char baseName[ RESOURCE_NAME_MAX_SIZE ];
	
	// Ignore all mission rating requests when in production edit mode
	if(isProductionEditMode()) {
		return;
	}

	// Only the real mission should give you a chance to review.  The
	// "leave map" mission can't.
	if(   resExtractNameSpace( missionName, namespaceName, baseName )
		  && namespaceIsUGC( namespaceName ) && stricmp( baseName, "Mission" ) == 0 ) {
		// Close the gen if it is already up. We are about to invalidate its data.
		globCmdParse( "ugcHideReviewGen" );
		
		StructDestroySafe(parse_UGCProject, &spReviewableProject);
		StructDestroySafe(parse_UGCProjectSeries, &spReviewableProjectSeries);
		
		s_bMissionToReviewWasCompleted = (iMissionWasCompleted!=0);
		s_bMissionToReviewQueued = true;
		ServerCmd_gslUGC_RequestDetails(UGCProject_GetContainerIDFromUGCNamespace(missionName), UGC_SERIES_ID_FROM_PROJECT, UGC_DETAILREQUEST_FOR_REVIEW);
	}
}



////////////////////////////////////////////////////////////////////////////////
// UGC Project cache -- so we can get data about UGCProjects on the
// client regardless of where we are

// After how many seconds should we keep header data after we have no longer requested to view the header data
U32 ugcCacheRetainTimeInSeconds = 60;
AUTO_CMD_INT( ugcCacheRetainTimeInSeconds, ugcCacheRetainTimeInSeconds );

static StashTable s_stProjectCache = NULL;
static UGCIDList s_RequestedIDs;
typedef struct CachedUGCProject
{
	U32 lastRequestedTime;
	UGCProject* ugcProj;
} CachedUGCProject;

static StashTable s_stProjectSeriesCache = NULL;
typedef struct CachedUGCProjectSeries
{
	U32 lastRequestedTime;
	UGCProjectSeries* ugcProjSeries;
} CachedUGCProjectSeries;

void gclUGC_CacheOncePerFrame( void )
{
	if(!s_stProjectCache) s_stProjectCache = stashTableCreateInt(256);
	if(!s_stProjectSeriesCache) s_stProjectSeriesCache = stashTableCreateInt(256);

	if(ea32Size(&s_RequestedIDs.eaProjectIDs) || ea32Size(&s_RequestedIDs.eaProjectSeriesIDs))
		if(linkConnected(gServerLink))
			ServerCmd_gslUGC_CacheSearchByID(&s_RequestedIDs);

	StructReset( parse_UGCIDList, &s_RequestedIDs );

	// free any projects that haven't been requested for a while
	{
		StashTableIterator it = { 0 };
		StashElement elem;
		stashGetIterator(s_stProjectCache, &it);
		while(stashGetNextElement(&it, &elem))
		{
			ContainerID id = stashElementGetU32Key(elem);
			CachedUGCProject *cachedProj = stashElementGetPointer(elem);

			if(gGCLState.totalElapsedTimeMs - cachedProj->lastRequestedTime > ugcCacheRetainTimeInSeconds * 1000)
			{
				stashIntRemovePointer(s_stProjectCache, id, NULL);
				StructDestroySafe(parse_UGCProject, &cachedProj->ugcProj);
				free(cachedProj);
			}
		}
	}

	// free any series that haven't been requested for a while
	{
		StashTableIterator it = { 0 };
		StashElement elem;
		stashGetIterator(s_stProjectSeriesCache, &it);
		while(stashGetNextElement(&it, &elem))
		{
			ContainerID id = stashElementGetU32Key(elem);
			CachedUGCProjectSeries* cachedSeries = stashElementGetPointer(elem);

			if(gGCLState.totalElapsedTimeMs - cachedSeries->lastRequestedTime > ugcCacheRetainTimeInSeconds * 1000)
			{
				stashIntRemovePointer(s_stProjectSeriesCache, id, NULL);
				StructDestroySafe(parse_UGCProjectSeries, &cachedSeries->ugcProjSeries);
				free(cachedSeries);
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetProject);
SA_ORET_OP_VALID UGCProject *gclUGC_CacheGetProject(ContainerID projID)
{
	CachedUGCProject *proj = NULL;
	if(!projID)
		return NULL;

	if(!s_stProjectCache) s_stProjectCache = stashTableCreateInt(256);

	if(!stashIntFindPointer(s_stProjectCache, projID, &proj))
	{
		// prevent constantly requesting this project before it comes back
		proj = calloc(1, sizeof(CachedUGCProject));
		proj->ugcProj = NULL;
		stashIntAddPointer(s_stProjectCache, projID, proj, /*bOverwriteIfFound=*/true);

		ea32PushUnique(&s_RequestedIDs.eaProjectIDs, projID);
	}

	devassert(proj);

	proj->lastRequestedTime = gGCLState.totalElapsedTimeMs;

	return proj->ugcProj;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ugc_GetSeries);
SA_ORET_OP_VALID UGCProjectSeries *gclUGC_CacheGetProjectSeries(ContainerID seriesID)
{
	CachedUGCProjectSeries *series = NULL;
	if(!seriesID)
		return NULL;

	if(!s_stProjectSeriesCache) s_stProjectSeriesCache = stashTableCreateInt(256);

	if(!stashIntFindPointer(s_stProjectSeriesCache, seriesID, &series))
	{
		// prevent constantly requesting this series before it comes back
		series = calloc(1, sizeof(CachedUGCProjectSeries));
		series->ugcProjSeries = NULL;
		stashIntAddPointer(s_stProjectSeriesCache, seriesID, series, /*bOverwriteIfFound=*/true);

		ea32PushUnique(&s_RequestedIDs.eaProjectSeriesIDs, seriesID);
	}

	// return NULL if not all projects have been returned
	if(series->ugcProjSeries)
	{
		ContainerID *ugcSeriesProjects = NULL;
		bool allProjectsFound = true;

		devassert(eaSize(&series->ugcProjSeries->eaVersions) == 1);

		if(eaSize(&series->ugcProjSeries->eaVersions))
		{
			int it;
			ugcProjectSeriesGetProjectIDs(&ugcSeriesProjects, series->ugcProjSeries->eaVersions[0]->eaChildNodes);
			for(it = 0; it != ea32Size(&ugcSeriesProjects); ++it)
				if(!gclUGC_CacheGetProject(ugcSeriesProjects[it]))
					allProjectsFound = false;
			ea32Destroy(&ugcSeriesProjects);

			if(!allProjectsFound)
				return NULL; // after 1 minute, this series will be ejected from the cache and it will be requested again, maybe all projects will arrive that time?
		}
	}

	devassert(series);

	series->lastRequestedTime = gGCLState.totalElapsedTimeMs;

	return series->ugcProjSeries;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclUGC_CacheReceiveSearchResult(UGCProjectList *pResult)
{
	if(!s_stProjectCache) s_stProjectCache = stashTableCreateInt(256);
	if(!s_stProjectSeriesCache) s_stProjectSeriesCache = stashTableCreateInt(256);

	if(pResult)
	{
		int it;
		for(it = 0; it != eaSize(&pResult->eaProjects); ++it)
		{
			UGCProject *proj = pResult->eaProjects[it];
			CachedUGCProject *existingProj = NULL;

			if(stashIntFindPointer(s_stProjectCache, proj->id, &existingProj))
			{
				StructDestroySafe(parse_UGCProject, &existingProj->ugcProj);

				existingProj->ugcProj = proj;
				pResult->eaProjects[it] = NULL;
			}
		}
		for(it = 0; it != eaSize(&pResult->eaProjectSeries); ++it)
		{
			UGCProjectSeries *series = pResult->eaProjectSeries[it];
			CachedUGCProjectSeries *existingSeries = NULL;

			if(stashIntFindPointer(s_stProjectSeriesCache, series->id, &existingSeries))
			{
				StructDestroySafe(parse_UGCProjectSeries, &existingSeries->ugcProjSeries);

				existingSeries->ugcProjSeries = series;
				pResult->eaProjectSeries[it] = NULL;
			}
		}
	}

	// Current list of parts of the code that care about the project cache
	gclUGC_ScriptingCacheUpdated();
}

const char* gclUGC_PlayerAllegiance(void)
{
	if( linkConnected( gServerLink )) {
		Entity* ent = entActivePlayerPtr();
		if( ent && IS_HANDLE_ACTIVE( ent->hAllegiance )) {
			return REF_STRING_FROM_HANDLE( ent->hAllegiance );
		}
	} else if( linkConnected( gpLoginLink )) {
        const char *allegianceName = gclLoginGetChosenCharacterAllegiance();
		if( !nullStr( allegianceName )) {
		    return allegianceName;
        }
	}

	return "";
}

//////////////////////////////////////////////////////////////////////
// UGC Featured Content
AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(4);
void gclUGC_FeaturedSaveState( UGCFeaturedContentInfoList* pFeaturedContent )
{
	if( ParserWriteTextFile( "C:/FeaturedContent.def", parse_UGCFeaturedContentInfoList, pFeaturedContent, 0, 0 )) {
		gclNotifyReceive( kNotifyType_Default, "Saved to C:/FeaturedContent.def", NULL, NULL );
	} else {
		gclNotifyReceive( kNotifyType_Failed, "Command Failed", NULL, NULL );
	}
}

//////////////////////////////////////////////////////////////////////
/// Load the state of Featured/Featured Archives from FILENAME (on the
/// client).
AUTO_COMMAND ACMD_NAME(ugcFeatured_LoadState) ACMD_ACCESSLEVEL(4);
void gclUGC_FeaturedLoadState( const char* filename )
{
	UGCFeaturedContentInfoList list = { 0 };
	ParserReadTextFile( filename, parse_UGCFeaturedContentInfoList, &list, 0 );
	ServerCmd_gslUGC_FeaturedLoadState( &list );
	StructReset( parse_UGCFeaturedContentInfoList, &list );
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(4);
void gclUGC_FeaturedSaveAuthorAllowsFeaturedList( UGCProjectList* pList )
{
	if( ParserWriteTextFile( "C:/AuthorAllowsFeatured.def", parse_UGCProjectList, pList, 0, 0 )) {
		gclNotifyReceive( kNotifyType_Default, "Saved to C:/AuthorAllowsFeatured.def", NULL, NULL );
	} else {
		gclNotifyReceive( kNotifyType_Failed, "Command Failed", NULL, NULL );
	}
}

static bool bUseFreeCamera = false;
AUTO_CMD_INT(bUseFreeCamera, ugc_freecam) ACMD_CALLBACK(ugcCommandFreeCameraCB) ACMD_ACCESSLEVEL(2);
void ugcCommandFreeCameraCB(void)
{
	if(!isProductionEditMode()) return;

	if (bUseFreeCamera)
		gclSetFreeCameraActive();
	else
		gclSetGameCameraActive();
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(2);
void ugcGodModeClient(int iSet)
{
	if(!isProductionEditMode()) return;

	GodModeClient(iSet);
}

// this resets all camera movement flags
AUTO_COMMAND ACMD_NAME("ugcCamera.halt") ACMD_ACCESSLEVEL(2);
void ugcCommandCameraHalt(void)
{
	if(!isProductionEditMode()) return;

	CommandCameraHalt();
}

AUTO_COMMAND ACMD_NAME("ugcFreeCamera.spawn_player") ACMD_ACCESSLEVEL(2);
void ugcSpawnPlayerAtCameraAndLeaveFreecam(void)
{
	if(!isProductionEditMode()) return;

	gfxSpawnPlayerAtCameraAndLeaveFreecam();
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void ugcFlagProjectAsCryptic(ContainerID uUGCProjectID, bool bFlaggedAsCryptic)
{
	if(uUGCProjectID)
		ServerCmd_gslUGC_FlagProjectAsCryptic(uUGCProjectID, bFlaggedAsCryptic);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void ugcFlagProjectSeriesAsCryptic(ContainerID uUGCProjectSeriesID, bool bFlaggedAsCryptic)
{
	if(uUGCProjectSeriesID)
		ServerCmd_gslUGC_FlagProjectSeriesAsCryptic(uUGCProjectSeriesID, bFlaggedAsCryptic);
}

#include "gclUGC_c_ast.c"
