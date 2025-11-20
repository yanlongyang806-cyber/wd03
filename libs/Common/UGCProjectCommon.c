//// UGC backend project handling
////
//// Commands and functions that relate to UGC project tracking,
//// rating, searching, etc. Shared among the various servers that have
//// to deal with UGC projects.

#include "UGCProjectCommon.h"
#include "UGCProjectCommon_h_ast.h"
#include "..\..\crossroads\common\UGC\UGCCommon.h"
#include "globalTypes.h"
#include "objSchema.h"
#include "UUID.h"
#include "GlobalEnums.h"
#include "stringCache.h"
#include "SubStringSearchTree.h"
#include "stringutil.h"
#include "ResourceInfo.h"
#include "MapDescription.h"
#include "UtilitiesLib.h"
#include "ugcProjectUtils.h"
#include "autoTransDefs.h"
#include "NameList.h"
#include "progression_common.h"
#include "timing.h"
#include "FolderCache.h"
#include "fileutil.h"

#ifndef GAMECLIENT
#include "localTransactionManager.h"
#include "objTransactions.h"
#include "remoteCommandGroup.h"
#include "Autogen/appserverlib_autogen_remotefuncs.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Indicates if UGC has been turned on via shard launcher config for this shard. It will be false from MCP unless set via cmdline.
bool gbUGCGenerallyEnabled = true; // defaulted back to on in order to not break existing shards.
AUTO_CMD_INT(gbUGCGenerallyEnabled, UGCGenerallyEnabled) ACMD_COMMANDLINE;

static bool sbAllowUGCProjResend = false;
AUTO_CMD_INT(sbAllowUGCProjResend, AllowUGCProjResend);

unsigned g_UGCImportantProjectNumPlays = 100;
AUTO_CMD_INT(g_UGCImportantProjectNumPlays, UGCImportantProjectNumPlays) ACMD_COMMANDLINE ACMD_AUTO_SETTING(Ugc, GAMESERVER, UGCDATAMANAGER);

float g_UGCImportantProjectAdjustedRating = 0.75;
AUTO_CMD_FLOAT(g_UGCImportantProjectAdjustedRating, UGCImportantProjectAdjustedRating) ACMD_COMMANDLINE ACMD_AUTO_SETTING(Ugc, GAMESERVER, UGCDATAMANAGER);

AUTO_RUN_LATE;
void ugcProjectStartup(void)
{
#ifndef GAMECLIENT
	objRegisterNativeSchema(GLOBALTYPE_UGCPROJECT, parse_UGCProject, NULL, NULL, NULL, NULL, NULL);
	objRegisterNativeSchema(GLOBALTYPE_UGCPROJECTSERIES, parse_UGCProjectSeries, NULL, NULL, NULL, NULL, NULL);
	objRegisterNativeSchema(GLOBALTYPE_UGCACCOUNT, parse_UGCAccount, NULL, NULL, NULL, NULL, NULL);
	
	{
		NameList *pUGCProjectVersionStateNameList = CreateNameList_StaticDefine((StaticDefine*)UGCProjectVersionStateEnum);
		NameList_AssignName(pUGCProjectVersionStateNameList, "UGCProjectVersionState");
	}
#endif
}

AUTO_TRANS_HELPER
ATR_LOCKS(pProject, ".Id");
NOCONST(UGCProjectVersion) *UGCProject_CreateEmptyVersion(ATR_ARGS, ATH_ARG NOCONST(UGCProject) *pProject, char *pUUIDString_In, char *pNameSpace_In)
{
	NOCONST(UGCProjectVersion) *pProjectVersion = StructCreateNoConst(parse_UGCProjectVersion);
	NOCONST(UGCProjectVersionFactionRestrictionProperties) *pFaction = NULL;
	UUID_t *pUUID = uuidGenerateV4();
	char uuidStr[40];

	uuidStringShort(pUUID, uuidStr, sizeof(uuidStr));

	ugcProjectSetVersionState(pProjectVersion, UGC_NEW, "Created");

	pProjectVersion->pUUID = strdup(pUUIDString_In ? pUUIDString_In : uuidStr);
	pProjectVersion->pName = strdup("happy test map");
	
	if (pNameSpace_In)
	{
		estrCopy2(&pProjectVersion->pNameSpace, pNameSpace_In);
	}
	else
	{
		UGCProject_MakeNamespace(&pProjectVersion->pNameSpace, pProject->id, pUUIDString_In ? pUUIDString_In : uuidStr);
	}

	pProjectVersion->pRestrictions = StructCreateNoConst(parse_UGCProjectVersionRestrictionProperties);
	if ( ugcDefaultsGetAllegianceRestriction() )
	{
		pFaction = StructCreateNoConst(parse_UGCProjectVersionFactionRestrictionProperties);
		pFaction->pcFaction = StructAllocString(ugcDefaultsGetAllegianceRestriction());
		eaPush(&pProjectVersion->pRestrictions->eaFactions, pFaction);
	}

	pProjectVersion->iModTime = timeSecondsSince2000();

	free(pUUID);

	return pProjectVersion;
}

#ifndef GAMECLIENT

AUTO_TRANS_HELPER
ATR_LOCKS(pProject, ".Id, .ppProjectVersions, .bAuthorAllowsFeatured, .pFeatured")
ATR_LOCKS(pProjectSeries, ".ugcSearchCache.eaPublishedProjectIDs");
enumTransactionOutcome UGCProject_trh_WithdrawProject(ATR_ARGS, ATH_ARG NOCONST(UGCProject) *pProject, ATH_ARG NOCONST(UGCProjectSeries) *pProjectSeries, bool bWithdraw, const char* pComment)
{
	int i;

	if(!ISNULL(pProject->pFeatured))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Featured project can not be withdrawn");
	}

	if(ugcDefaultsAuthorAllowsFeaturedBlocksEditing() && pProject->bAuthorAllowsFeatured)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Author allows featured project can not be withdrawn");
	}

	for (i=0; i < eaSize(&pProject->ppProjectVersions); i++)
	{
		UGCProjectVersionState versionState = ugcProjectGetVersionState(pProject->ppProjectVersions[i]);

		if (versionState == UGC_PUBLISHED)
		{
			if( bWithdraw && !pProject->pFeatured ) {
				ugcProjectSetVersionState(pProject->ppProjectVersions[i], UGC_WITHDRAWN, pComment);
			}
		}
		else if (versionState == UGC_PUBLISH_BEGUN || versionState == UGC_REPUBLISHING)
		{
			QueueRemoteCommand_CancelJobGroup(ATR_RESULT_SUCCESS, GLOBALTYPE_JOBMANAGER, 0, pProject->ppProjectVersions[i]->pPublishJobName, pComment);
			ugcProjectSetVersionState(pProject->ppProjectVersions[i], UGC_UNPLAYABLE, pComment);
		}
	}

	if(bWithdraw && NONNULL(pProjectSeries))
		ea32FindAndRemove( &pProjectSeries->ugcSearchCache.eaPublishedProjectIDs, pProject->id);

	return TRANSACTION_OUTCOME_SUCCESS;
}

#endif

bool UGCProject_IsFeaturedWindow(U32 iStartTimestamp, U32 iEndTimestamp, bool bIncludePreviouslyFeatured, bool bIncludeFutureFeatured )
{
	// Be restrictive.  Currently no editing whatsoever is allowed for
	// a featured project. (Move on to create new stuff, author!)  If
	// this ever changes to allow an author to republish a featured
	// project (for bug fixes), then this function should be updated.
	if(0 != iStartTimestamp)
	{
		U32 curTime = timeSecondsSince2000();

		bool previouslyFeatured = (curTime > iEndTimestamp && iEndTimestamp != 0);
		bool futureFeatured = (curTime < iStartTimestamp);
		bool currentlyFeatured = !previouslyFeatured && !futureFeatured;

		if( currentlyFeatured ) {
			return true;
		} else if( previouslyFeatured && bIncludePreviouslyFeatured ) {
			return true;
		} else if( futureFeatured && bIncludeFutureFeatured ) {
			return true;
		}
	}

	return false;
}

// Return if a project is featured.  This blocks certain operations.
AUTO_TRANS_HELPER;
bool UGCProject_IsFeaturedNow(ATH_ARG const NOCONST(UGCProject)* pProject )
{
	return UGCProject_IsFeatured(pProject, false, false);
}

AUTO_TRANS_HELPER;
bool UGCProject_IsFeatured(ATH_ARG const NOCONST(UGCProject)* pProject, bool bIncludePreviouslyFeatured, bool bIncludeFutureFeatured )
{
	// Be restrictive.  Currently no editing whatsoever is allowed for
	// a featured project. (Move on to create new stuff, author!)  If
	// this ever changes to allow an author to republish a featured
	// project (for bug fixes), then this function should be updated.
	if(!ISNULL(pProject->pFeatured))
		return UGCProject_IsFeaturedWindow(pProject->pFeatured->iStartTimestamp, pProject->pFeatured->iEndTimestamp, bIncludePreviouslyFeatured, bIncludeFutureFeatured);
	
	return false;
}

AUTO_TRANS_HELPER;
bool UGCProject_IsImportant(ATH_ARG const NOCONST(UGCProject)* pProject)
{
	if( UGCProject_IsFeatured( pProject, true, true )) {
		return true;
	} else if( UGCProject_trh_GetTotalPlayedCount( pProject ) > g_UGCImportantProjectNumPlays ) {
		return true;
	} else if( pProject->ugcReviews.fAdjustedRatingUsingConfidence > g_UGCImportantProjectAdjustedRating ) {
		return true;
	} else {
		return false;
	}
}

AUTO_EXPR_FUNC(util) ACMD_NAME(UGCProject_IsImportant);
bool UGCProject_IsImportantExpr( SA_PARAM_NN_VALID const UGCProject* project )
{
	return UGCProject_IsImportant( CONTAINER_NOCONST( UGCProject, project ));
}

char *UGCProject_GetUniqueMapDescription(char *pNameSpace)
{
	char temp[1024];
	sprintf(temp, "%s:%u", pNameSpace, timeSecondsSince2000());
	return (char*)allocAddString(temp);
}

AUTO_TRANS_HELPER_SIMPLE;
void UGCProject_MakeNamespace(char **ppOutString, ContainerID iProjectID, char *pUUIDStr)
{
	estrPrintf(ppOutString, "%s%u_%s", UGC_GetShardSpecificNSPrefix(NULL), iProjectID, pUUIDStr);
}

UGCProject *UGCProject_CreateHeaderCopy(UGCProject *pProject, bool bAlsoIncludeMostRecentPublish)
{
	NOCONST(UGCProject) *pProjectNoConst = CONTAINER_NOCONST(UGCProject, pProject);
	NOCONST(UGCProject) *pRetVal = StructCreateNoConst(parse_UGCProject);
	
	StructCopyFieldsDeConst(parse_UGCProject, pProject, pRetVal, 0, TOK_EARRAY | TOK_FIXED_ARRAY);
	eaiCopy(&pRetVal->ugcReviews.piNumRatings, &pProject->ugcReviews.piNumRatings);
	eaiCopy(&pRetVal->ugcStats.completionStats.eaiCompletedCountByDay, &pProject->ugcStats.completionStats.eaiCompletedCountByDay);
	eaCopyStructsNoConst(&pProjectNoConst->ugcReviews.eaTagData, &pRetVal->ugcReviews.eaTagData, parse_UGCTagData);

	// Header copies are sent down to the client, which uses the prev
	// version fields to calculate data client side.
	//
	// NOTE: The duration stats are not fixed up because the data to
	// fixup is in EArrays, which are not copied and sent down to the
	// client.
	if( !g_bUGCDurationAndCompletionStatsResetAfterPublish ) {
		pRetVal->ugcStats.completionStats.uRemainingCompletedCount += pRetVal->ugcStats.completionStats.uPrevVersionsCompletedCount;
	}
	pRetVal->ugcStats.completionStats.uPrevVersionsCompletedCount = 0;

	if(bAlsoIncludeMostRecentPublish)
	{
		const UGCProjectVersion *pVersion = UGCProject_GetMostRecentPublishedVersion(pProject);

		if(pVersion)
		{
			NOCONST(UGCProjectVersion) *pUGCProjectVersionRetVal = StructCreateNoConst(parse_UGCProjectVersion);
			StructCopyFieldsDeConst(parse_UGCProjectVersion, pVersion, pUGCProjectVersionRetVal, 0, TOK_EARRAY | TOK_FIXED_ARRAY);
			eaPush(	&pRetVal->ppProjectVersions, pUGCProjectVersionRetVal);
		}
	}
	return CONTAINER_RECONST(UGCProject, pRetVal);
}

UGCProject *UGCProject_CreateDetailCopy(UGCProject *pProject, S32 iReviewsPage, bool bAlsoIncludeMostRecentPublish, U32 uReviewerAccountID)
{
	NOCONST(UGCProject) *pRetVal = NULL;

	PERFINFO_AUTO_START_FUNC();

	pRetVal = CONTAINER_NOCONST(UGCProject, UGCProject_CreateHeaderCopy(pProject, bAlsoIncludeMostRecentPublish));
	ugcReviews_GetForPage( &pProject->ugcReviews, iReviewsPage, &pRetVal->ugcReviews );

	// Set up our extra detail data.
	pRetVal->pExtraDetailData =	StructCreate(parse_UGCExtraDetailData);
	pRetVal->pExtraDetailData->iNumReviewPages = ugcReviews_GetPageCount( &pProject->ugcReviews );

	pRetVal->pExtraDetailData->pReviewForCurAccount = StructClone(parse_UGCSingleReview, eaIndexedGetUsingInt(&pProject->ugcReviews.ppReviews, uReviewerAccountID));

	PERFINFO_AUTO_STOP();

	return (UGCProject*)pRetVal;
}

UGCProjectSeries* UGCProjectSeries_CreateEditingCopy(const UGCProjectSeries* pSeries)
{
	const UGCProjectSeriesVersion* pVersion = eaTail( &pSeries->eaVersions );
	
	NOCONST(UGCProjectSeries) *pRetVal = StructCreateNoConst( parse_UGCProjectSeries );
	StructCopyFieldsDeConst( parse_UGCProjectSeries, pSeries, pRetVal, 0, TOK_EARRAY | TOK_FIXED_ARRAY );

	if( pVersion ) {
		eaPush( &pRetVal->eaVersions, StructCloneDeConst( parse_UGCProjectSeriesVersion, pVersion ));
	}

	return CONTAINER_RECONST( UGCProjectSeries, pRetVal );
}

UGCProjectSeries* UGCProjectSeries_CreateHeaderCopy(const UGCProjectSeries* pSeries)
{
	NOCONST(UGCProjectSeries) *pSeriesNoConst = CONTAINER_NOCONST(UGCProjectSeries, pSeries);

	const UGCProjectSeriesVersion *pVersion = UGCProjectSeries_GetMostRecentPublishedVersion(pSeries);
	
	NOCONST(UGCProjectSeries) *pRetVal = StructCreateNoConst(parse_UGCProjectSeries);
	StructCopyFieldsDeConst(parse_UGCProjectSeries, pSeries, pRetVal, 0, TOK_EARRAY | TOK_FIXED_ARRAY);
	eaiCopy(&pRetVal->ugcReviews.piNumRatings, &pSeries->ugcReviews.piNumRatings);
	eaCopyStructsNoConst(&pSeriesNoConst->ugcReviews.eaTagData, &pRetVal->ugcReviews.eaTagData, parse_UGCTagData);

	if(pVersion)
		eaPush(&pRetVal->eaVersions, StructCloneDeConst(parse_UGCProjectSeriesVersion, pVersion));

	return CONTAINER_RECONST(UGCProjectSeries, pRetVal);
}

UGCProjectSeries* UGCProjectSeries_CreateDetailCopy(const UGCProjectSeries* pSeries, S32 iReviewsPage, U32 uReviewerAccountID)
{
	NOCONST(UGCProjectSeries)* pRetVal = NULL;

	PERFINFO_AUTO_START_FUNC();

	pRetVal = CONTAINER_NOCONST( UGCProjectSeries, UGCProjectSeries_CreateHeaderCopy( pSeries ));
	ugcReviews_GetForPage( &pSeries->ugcReviews, iReviewsPage, &pRetVal->ugcReviews );	

	// Set up our extra detail data. Currently just the single review associated with the uReviewerAccountID.
	pRetVal->pExtraDetailData =	StructCreate( parse_UGCExtraDetailData );
	pRetVal->pExtraDetailData->iNumReviewPages = ugcReviews_GetPageCount( &pSeries->ugcReviews );

	pRetVal->pExtraDetailData->pReviewForCurAccount = StructClone(parse_UGCSingleReview, eaIndexedGetUsingInt(&pSeries->ugcReviews.ppReviews, uReviewerAccountID));

	PERFINFO_AUTO_STOP();

	return CONTAINER_RECONST( UGCProjectSeries, pRetVal );
}


bool UGCProject_ValidateNewProjectRequest(PossibleUGCProject *pRequest, char **ppErrorString)
{
	if (!pRequest->pProjectInfo)
	{
		estrPrintf(ppErrorString, "No project info");
		return false;
	}

	return true;
}


AUTO_FIXUPFUNC;
TextParserResult InfoForUGCProjectSaveOrPublish_Fixup(InfoForUGCProjectSaveOrPublish *pInfo, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		if (pInfo->pWhatToDoIfPublishJobDoesntStart)
		{
#ifndef GAMECLIENT
			StructDestroy(parse_RemoteCommandGroup, pInfo->pWhatToDoIfPublishJobDoesntStart);
#endif
		}
		break;
	}

	return true;
}

AUTO_TRANS_HELPER;
bool UGCProject_IsReadyForSendToOtherShard(ATH_ARG UGCProject *pProject, char **ppWhyNot)
{
	const UGCProjectVersion *pVersion = UGCProject_GetMostRecentPublishedVersion(pProject);

	if (!pVersion)
	{
		if (ppWhyNot)
		{
			estrPrintf(ppWhyNot, "This project has never been published");
		}
		return false;
	}

	if (sbAllowUGCProjResend)
	{
		return true;
	}

	if (pVersion->bSendToOtherShardSucceeded )
	{
		if (ppWhyNot)
		{
			estrPrintf(ppWhyNot, "This project has already been successfuly sent");
		}
		return false;
	}

	if (pVersion->iLastTimeSentToOtherShard)
	{
		if (ppWhyNot)
		{
			estrPrintf(ppWhyNot, "This project is already in the midst of being sent");
		}
		return false;
	}

	return true;
}

UGCProjectStatusQueryInfo* UGCProject_GetStatusFromProject(UGCProject *pProject, const char **ppchJobName, bool *pbCurrentlyPublishing)
{
	int i;
	UGCProjectStatusQueryInfo *pInfo;

	if (!pProject)
		return NULL;

	pInfo = StructCreate( parse_UGCProjectStatusQueryInfo );

	i = eaSize(&pProject->ppProjectVersions)-2;
	if (i >= 0 &&
		ugcProjectGetVersionStateConst(pProject->ppProjectVersions[i]) == UGC_PUBLISH_BEGUN)
	{
		if(pbCurrentlyPublishing) 
			(*pbCurrentlyPublishing) = true;
		if(ppchJobName)
			(*ppchJobName) = pProject->ppProjectVersions[i]->pPublishJobName;
	}

	for (i = 0; i < eaSize(&pProject->ppProjectVersions); i++)
	{
		UGCProjectVersionState eState = ugcProjectGetVersionStateConst(pProject->ppProjectVersions[i]);
		if (eState == UGC_PUBLISHED || eState == UGC_PUBLISH_FAILED || eState == UGC_REPUBLISH_FAILED)
		{
			if (pProject->ppProjectVersions[i]->iModTime > pInfo->iLastPublishTime)
			{
				StructCopyString( &pInfo->strPublishedName, pProject->ppProjectVersions[i]->pName );
				pInfo->pPublishedMapLocation = ugcCreateMapLocation( pProject->ppProjectVersions[i]->pMapLocation );
				pInfo->iLastPublishTime = pProject->ppProjectVersions[i]->iModTime;
				pInfo->bLastPublishSucceeded = (eState == UGC_PUBLISHED);
			}
			pInfo->iLastSaveTime = MAX(pInfo->iLastSaveTime, pProject->ppProjectVersions[i]->iModTime);
		}
		else if (eState == UGC_SAVED || eState == UGC_PUBLISH_BEGUN)
		{
			pInfo->iLastSaveTime = MAX(pInfo->iLastSaveTime, pProject->ppProjectVersions[i]->iModTime);
		}

		//a WITHDRAWN cancels out a previous PUBLISH_FAILED
		if (eState == UGC_WITHDRAWN)
		{
			pInfo->iLastPublishTime = 0;
		}

		if( pProject->pFeatured ) {
			if( !pInfo->pFeatured ) {
				pInfo->pFeatured = StructCreate( parse_UGCFeaturedData );
			}
			StructCopy( parse_UGCFeaturedData, pProject->pFeatured, pInfo->pFeatured, 0, 0, 0 );
		}
	}

	pInfo->bCanBeWithdrawn = UGCProject_CanBeWithdrawn(CONTAINER_NOCONST(UGCProject, pProject));
	pInfo->bAuthorAllowsFeatured = pProject->bAuthorAllowsFeatured;

	return pInfo;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pProject, ".ppProjectVersions, .bAuthorAllowsFeatured, .pFeatured");
bool UGCProject_CanBeWithdrawn(ATH_ARG NOCONST(UGCProject) *pProject)
{
	int i;

	if( ugcDefaultsAuthorAllowsFeaturedBlocksEditing() && pProject->bAuthorAllowsFeatured ) {
		return false;
	}
	if( pProject->pFeatured ) {
		return false;
	}

	for (i=0; i < eaSize(&pProject->ppProjectVersions); i++)
	{
		switch (ugcProjectGetVersionState(pProject->ppProjectVersions[i]))
		{
		case UGC_PUBLISH_BEGUN:
		case UGC_PUBLISHED:
		case UGC_REPUBLISHING:
			return true;
		}
	}

	return false;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pProject, ".bBanned, .ugcReporting.uTemporaryBanExpireTime")
ATR_LOCKS(pVersion, ".*");
bool UGCProject_trh_VersionIsPlayable(ATR_ARGS, ATH_ARG NOCONST(UGCProject) *pProject, ATH_ARG NOCONST(UGCProjectVersion) *pVersion)
{
	if (ISNULL(pVersion))
	{
		return false;
	}

	if (pProject->bBanned && pProject->ugcReporting.uTemporaryBanExpireTime == 0)
	{
		return false;
	}

	if (ugcProjectGetVersionState(pVersion) != UGC_PUBLISHED && ugcProjectGetVersionState(pVersion) != UGC_WITHDRAWN)
	{
		return false;
	}
	
	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pProject, ".Ugcstats.Completionstats.Eaicompletedcountbyday, .Ugcstats.Completionstats.Uremainingcompletedcount, ugcStats.completionStats.uPrevVersionsCompletedCount");
U32 UGCProject_trh_GetTotalPlayedCount(ATH_ARG const NOCONST(UGCProject)* pProject)
{
	U32 uTotal = 0;
	if(NONNULL(pProject))
	{
		if(pProject->ugcStats.completionStats.eaiCompletedCountByDay)
		{
			int i;
			for (i=0; i < eaiSize(&pProject->ugcStats.completionStats.eaiCompletedCountByDay); i++) 
			{
				uTotal += pProject->ugcStats.completionStats.eaiCompletedCountByDay[i];
			}
		}

		uTotal += pProject->ugcStats.completionStats.uRemainingCompletedCount;

		// Since this flag is an auto setting, the client can not
		// detect the value of the setting.
		//
		// This is handled for client data in header copies
		if( !g_bUGCDurationAndCompletionStatsResetAfterPublish && !IsClient() ) {
			uTotal += pProject->ugcStats.completionStats.uPrevVersionsCompletedCount;
		}
	}

	return uTotal;
}

static F32 UGCProject_GetRewardQualifiyTime(void)
{
	if(gConf.bUgcRewardOverrideEnable)
	{
		return gConf.fUgcRewardOverrideTimeMinutes;
	}

	return 20.0f;
}


// Determines if a UGC project qualifies for a reward.
// Requirements: Total played count 20+, Average duration 20+
AUTO_TRANS_HELPER
ATR_LOCKS(pProject, ".Ugcstats.Iaveragedurationinminutes_Usingmaps, .ugcStats.CompletionStats.uPrevVersionsCompletedCount, .Ugcstats.Durationstats.Iaveragedurationinminutes_Ignoreoutliers, .Ugcstats.Completionstats.Eaicompletedcountbyday, .Ugcstats.Completionstats.Uremainingcompletedcount, .pFeatured.fAverageDurationInMinutes_Override, .Ugcstats.Bmapsfilledin");
bool UGCProject_trh_QualifiesForRewards(ATR_ARGS, ATH_ARG NOCONST(UGCProject)* pProject)
{
	if(NONNULL(pProject))
		if(UGCProject_trh_GetTotalPlayedCount(pProject) >= 20 && UGCProject_trh_AverageDurationInMinutes(ATR_PASS_ARGS, pProject) >= UGCProject_GetRewardQualifiyTime())
			return true;
	return false;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pProject, ".Ppprojectversions");
bool UGCProject_trh_BeingPublished(ATR_ARGS, ATH_ARG NOCONST(UGCProject) *pProject)
{
	int i;

	for (i=0; i < eaSize(&pProject->ppProjectVersions); i++)
	{
		switch (ugcProjectGetVersionState(pProject->ppProjectVersions[i]))
		{
		case UGC_PUBLISH_BEGUN:
			return true;
		}
	}

	return false;
}

AUTO_TRANS_HELPER ATR_LOCKS(pProject, ".ppProjectVersions");
bool UGCProject_trh_AnyVersionNeedsAttention(ATR_ARGS, ATH_ARG NOCONST(UGCProject)* pProject)
{
	int i;

	for (i=0; i < eaSize(&pProject->ppProjectVersions); i++)
	{
		switch (ugcProjectGetVersionState(pProject->ppProjectVersions[i]))
		{
		case UGC_NEEDS_REPUBLISHING:
		case UGC_NEEDS_UNPLAYABLE:
		case UGC_NEEDS_FIRST_PUBLISH:
			return true;
		}
	}

	return false;
}

void ugcProjectSetVersionState(NOCONST(UGCProjectVersion) *pVersion, 
	UGCProjectVersionState eNewState, const char *pComment)
{
	NOCONST(UGCProjectVersionStateChangeHistory) *pHistory = StructCreateVoid(parse_UGCProjectVersionStateChangeHistory);
	pVersion->eState_USEACCESSOR = eNewState;
	pVersion->iModTime = timeSecondsSince2000();

	pHistory->eNewState = eNewState;
	pHistory->iTime = timeSecondsSince2000();
	estrCopy2(&pHistory->pComment, pComment);

	eaPush(&pVersion->ppRecentHistory, pHistory);
	if (eaSize(&pVersion->ppRecentHistory) > 4)
	{
		StructDestroyVoid(parse_UGCProjectVersionStateChangeHistory, pVersion->ppRecentHistory[0]);
		eaRemove(&pVersion->ppRecentHistory, 0);
	}
}

UGCProjectVersionState ugcProjectGetVersionState(NOCONST(UGCProjectVersion) *pVersion)
{
	return pVersion->eState_USEACCESSOR;
}

void UGCProject_ApplySaveOrPublishInfoToVersion(NOCONST(UGCProjectVersion) *pMostRecent, InfoForUGCProjectSaveOrPublish *pInfo)
{
	UGCProject_ApplyProjectInfoToVersion(pMostRecent, &pInfo->sProjectInfo);
	
	// To find all the places you need to update to add a per
	// UGCProjectVersion field, search for this: {{UGCPROJECTVERSION}}
	pMostRecent->iModTime = timeSecondsSince2000();
	pMostRecent->pCostumeOverride = pInfo->pCostumeOverride ? strdup(pInfo->pCostumeOverride) : NULL;
	pMostRecent->pPetOverride = pInfo->pPetOverride ? strdup(pInfo->pPetOverride) : NULL;
	pMostRecent->strInitialMapName = pInfo->strInitialMapName ? strdup(pInfo->strInitialMapName) : NULL;
	pMostRecent->strInitialSpawnPoint = pInfo->strInitialSpawnPoint ? strdup(pInfo->strInitialSpawnPoint) : NULL;	
	pMostRecent->pBodyText = pInfo->pBodyText ? strdup(pInfo->pBodyText) : NULL;

	FOR_EACH_IN_EARRAY(pMostRecent->ppMapNames, char, name)
	{
		free(name);
	}
	FOR_EACH_END;
	eaClear(&pMostRecent->ppMapNames);

	FOR_EACH_IN_EARRAY(pInfo->ppMapNames, char, name)
	{
		eaPush(&pMostRecent->ppMapNames, strdup(name));
	}
	FOR_EACH_END;
}

static void UGCProject_ApplyMapLocation( NOCONST(UGCProjectVersion)* pVersion )
{
	// TODO: fill this in
}

void UGCProject_ApplyProjectInfoToVersion(NOCONST(UGCProjectVersion) *pMostRecent, UGCProjectInfo *pInfo)
{
	// To find all the places you need to update to add a per
	// UGCProjectVersion field, search for this: {{UGCPROJECTVERSION}}
	pMostRecent->iModTime = timeSecondsSince2000();
	StructCopyString(&pMostRecent->pName, pInfo->pcPublicName);
	StructCopyString(&pMostRecent->pDescription, pInfo->strDescription);
	StructCopyString(&pMostRecent->pNotes, pInfo->strNotes);

	if( pInfo->pMapLocation ) {
		if( !pMostRecent->pMapLocation ) {
			pMostRecent->pMapLocation = StructCreateNoConst( parse_UGCProjectVersionMapLocation );
		}
		pMostRecent->pMapLocation->positionX = pInfo->pMapLocation->positionX;
		pMostRecent->pMapLocation->positionY = pInfo->pMapLocation->positionY;
		pMostRecent->pMapLocation->astrIcon = allocAddString( pInfo->pMapLocation->astrIcon );
		StructCopyString( &pMostRecent->pImage, pInfo->pMapLocation->astrIcon );
	}
	UGCProject_ApplyMapLocation( pMostRecent );
	if (pInfo->strSearchLocation){
		pMostRecent->pLocation = StructAllocString( pInfo->strSearchLocation );
	}
	pMostRecent->eLanguage = pInfo->eLanguage;
	if( !pMostRecent->pRestrictions ) {
		pMostRecent->pRestrictions = StructCreateNoConst( parse_UGCProjectVersionRestrictionProperties );
	}
	ugcRestrictionsContainerFromWL(pMostRecent->pRestrictions, pInfo->pRestrictionProperties);

	if( pInfo->pRestrictionProperties ) {
		if( !pMostRecent->pRestrictions ) {
			pMostRecent->pRestrictions = StructCreateNoConst( parse_UGCProjectVersionRestrictionProperties );
		}
		ugcRestrictionsContainerFromWL( pMostRecent->pRestrictions, pInfo->pRestrictionProperties );
	} else {
		StructDestroyNoConstSafe( parse_UGCProjectVersionRestrictionProperties, &pMostRecent->pRestrictions );
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pSubscriberAccount, "accountId, .eaPlayers[]");
enumTransactionOutcome ugc_trh_SubscribeToAuthor(ATR_ARGS, ATH_ARG NOCONST(UGCAccount) *pSubscriberAccount, ContainerID iSubscriberPlayerID, ContainerID iAuthorID)
{
	NOCONST(UGCPlayer) *pPlayer = NULL;
	NOCONST(UGCAuthorSubscription) *subscription = NULL;

	if(ISNULL(pSubscriberAccount))
		TRANSACTION_RETURN_LOG_FAILURE("pSubscriberAccount is NULL");

	pPlayer = eaIndexedGetUsingInt(&pSubscriberAccount->eaPlayers, iSubscriberPlayerID);
	if(ISNULL(pPlayer))
	{
		pPlayer = StructCreateNoConst(parse_UGCPlayer);
		pPlayer->playerID = iSubscriberPlayerID;
		eaIndexedPushUsingIntIfPossible(&pSubscriberAccount->eaPlayers, iSubscriberPlayerID, pPlayer);
	}

	if(eaSize(&pPlayer->pSubscription->eaAuthors) > UGC_SUBSCRIPTION_MAX)
		TRANSACTION_RETURN_LOG_FAILURE("Subscriber %d (Player %d) already has %d subscriptions", pSubscriberAccount->accountID, iSubscriberPlayerID, UGC_SUBSCRIPTION_MAX);

	subscription = StructCreateNoConst(parse_UGCAuthorSubscription);
	subscription->authorID = iAuthorID;
	if(!eaIndexedPushUsingIntIfPossible(&pPlayer->pSubscription->eaAuthors, iAuthorID, subscription))
		StructDestroyNoConst(parse_UGCAuthorSubscription, subscription);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pAuthorAccount, "author.subscribers.eaPlayers[]");
void ugc_trh_AuthorSubscribedTo(ATR_ARGS, ATH_ARG NOCONST(UGCAccount) *pAuthorAccount, ContainerID iSubscriberPlayerID, ContainerID iSubscriberAccountID)
{
	NOCONST(UGCSubscriber) *pSubscriber = StructCreateNoConst(parse_UGCSubscriber);
	pSubscriber->uPlayer = iSubscriberPlayerID;
	pSubscriber->uAccount = iSubscriberAccountID;
	if(!eaIndexedPushUsingIntIfPossible(&pAuthorAccount->author.subscribers.eaPlayers, iSubscriberPlayerID, pSubscriber))
		StructDestroyNoConst(parse_UGCSubscriber, pSubscriber);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pSubscriberAccount, ".eaPlayers[]");
enumTransactionOutcome ugc_trh_UnsubscribeFromAuthor(ATR_ARGS, ATH_ARG NOCONST(UGCAccount) *pSubscriberAccount, ContainerID iSubscriberPlayerID, ContainerID iAuthorID)
{
	NOCONST(UGCPlayer) *pPlayer = NULL;

	if(ISNULL(pSubscriberAccount))
		TRANSACTION_RETURN_LOG_FAILURE("pSubscriberAccount is NULL");

	pPlayer = eaIndexedGetUsingInt(&pSubscriberAccount->eaPlayers, iSubscriberPlayerID);
	if(!ISNULL(pPlayer))
	{
		NOCONST(UGCAuthorSubscription) *subscribed = eaIndexedRemoveUsingInt(&pPlayer->pSubscription->eaAuthors, iAuthorID);
		if(subscribed)
			StructDestroyNoConst(parse_UGCAuthorSubscription, subscribed);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pAuthorAccount, "author.subscribers.eaPlayers[]");
void ugc_trh_AuthorUnsubscribedFrom(ATR_ARGS, ATH_ARG NOCONST(UGCAccount) *pAuthorAccount, ContainerID iSubscriberPlayerID)
{
	NOCONST(UGCSubscriber) *pSubscription = eaIndexedRemoveUsingInt(&pAuthorAccount->author.subscribers.eaPlayers, iSubscriberPlayerID);
	if(pSubscription)
		StructDestroyNoConst(parse_UGCSubscriber, pSubscription);
}

NOCONST(UGCAccount) *ugcAccountClonePersistedAndSubscribedDataOnly(NOCONST(UGCAccount) *pUGCAccount)
{
	NOCONST(UGCAccount) *result = NULL;
	if(pUGCAccount)
	{
		result = StructCreateNoConst(parse_UGCAccount);
		StructCopyNoConst(parse_UGCAccount, pUGCAccount, result, 0, TOK_SUBSCRIBE, 0);
	}

	return result;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".pIDString, .id, .ppProjectVersions");
enumTransactionOutcome trUGCProjectFillInNewVersionForImport(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, int bClearExistingVersions)
{
	char IDString[UGC_IDSTRING_LENGTH_BUFFER_LENGTH];

	NOCONST(UGCProjectVersion)* newVersion = UGCProject_CreateEmptyVersion(ATR_RECURSE, pUGCProject, NULL, NULL);

	if(bClearExistingVersions)
		eaDestroy(&pUGCProject->ppProjectVersions);

	eaPush(&pUGCProject->ppProjectVersions, newVersion);

	UGCIDString_IntToString(pUGCProject->id, false, IDString);
	pUGCProject->pIDString = strdup(IDString);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProjectSeries, ".strIDString, .id");
enumTransactionOutcome trUGCProjectSeriesFillInForImport(ATR_ARGS, NOCONST(UGCProjectSeries) *pUGCProjectSeries)
{
	char IDString[UGC_IDSTRING_LENGTH_BUFFER_LENGTH];

	UGCIDString_IntToString(pUGCProjectSeries->id, /*isSeries=*/true, IDString);
	pUGCProjectSeries->strIDString = strdup(IDString);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".seriesID");
enumTransactionOutcome trUGCProjectFixupSeriesForImport(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, U32 seriesID)
{
	pUGCProject->seriesID = seriesID;

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
void ugc_trh_UGCProjectSeries_UpdateCache( ATR_ARGS, ATH_ARG NOCONST(UGCSeriesSearchCache)* ugcSeriesSearchCache, NON_CONTAINER UGCProjectSeriesVersion* newPublishedVersion )
{
	StructCopyString( &ugcSeriesSearchCache->strPublishedName, newPublishedVersion->strName );
	ea32Destroy( &ugcSeriesSearchCache->eaPublishedProjectIDs );
	ugcProjectSeriesGetProjectIDs( &ugcSeriesSearchCache->eaPublishedProjectIDs, newPublishedVersion->eaChildNodes );
}

AUTO_TRANS_HELPER
ATR_LOCKS(pUGCProject, ".bFlaggedAsCryptic, .pOwnerAccountName, .pOwnerAccountName_ForSearching");
void UGCProject_trh_SetOwnerAccountName(ATR_ARGS, ATH_ARG NOCONST(UGCProject) *pUGCProject, const char *ownerAccountName)
{
	if(!nullStr(ownerAccountName))
	{
		char buffer[RESOURCE_NAME_MAX_SIZE];

		if(pUGCProject->bFlaggedAsCryptic)
			sprintf(buffer, "%s@Cryptic", ownerAccountName);
		else
			strcpy(buffer, ownerAccountName);

		StructCopyString(&pUGCProject->pOwnerAccountName, buffer);

		estrDestroy(&pUGCProject->pOwnerAccountName_ForSearching);
		SSSTree_InternalizeString(&pUGCProject->pOwnerAccountName_ForSearching, buffer);
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pUGCProjectSeries, ".bFlaggedAsCryptic, .strOwnerAccountName");
void UGCProjectSeries_trh_SetOwnerAccountName(ATR_ARGS, ATH_ARG NOCONST(UGCProjectSeries) *pUGCProjectSeries, const char *ownerAccountName)
{
	if(!nullStr(ownerAccountName))
	{
		char buffer[RESOURCE_NAME_MAX_SIZE];

		if(pUGCProjectSeries->bFlaggedAsCryptic)
			sprintf(buffer, "%s@Cryptic", ownerAccountName);
		else
			strcpy(buffer, ownerAccountName);

		StructCopyString(&pUGCProjectSeries->strOwnerAccountName, buffer);
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pUGCProject, ".ugcStats.iAverageDurationInMinutes_UsingMaps, .ugcStats.durationStats.iAverageDurationInMinutes_IgnoreOutliers, .pFeatured.fAverageDurationInMinutes_Override, .Ugcstats.Bmapsfilledin");
F32 UGCProject_trh_AverageDurationInMinutes(ATR_ARGS, ATH_ARG NOCONST(UGCProject) *pUGCProject)
{
	if(NONNULL(pUGCProject))
	{
		if( NONNULL( pUGCProject->pFeatured ) && pUGCProject->pFeatured->fAverageDurationInMinutes_Override > 0 ) {
			return pUGCProject->pFeatured->fAverageDurationInMinutes_Override;
		}
		
		if(gConf.bUGCAveragePlayingTimeUsesCustomMapPlayingTime)
		{
			if(gConf.bUGCMigrateToAveragePlayingTimeUsingCustomMaps && !pUGCProject->ugcStats.bMapsFilledIn)
				return pUGCProject->ugcStats.durationStats.iAverageDurationInMinutes_IgnoreOutliers;
			else
				return pUGCProject->ugcStats.iAverageDurationInMinutes_UsingMaps;
		}
		else
			return pUGCProject->ugcStats.durationStats.iAverageDurationInMinutes_IgnoreOutliers;
	}
	return 0.0f;
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
	if( UGCProject_IsFeaturedNow( CONTAINER_NOCONST( UGCProject, pProject ) )) {
		return 2.0;
	}

	return pProject->ugcReviews.fAdjustedRatingUsingConfidence;
}

float UGCProjectSeries_RatingForSorting( const UGCProjectSeries* pProjectSeries )
{
	return pProjectSeries->ugcReviews.fAdjustedRatingUsingConfidence;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pUGCProjectReviews, ".ppReviews, .eaTagData");
enumTransactionOutcome ugc_trh_ComputeAggregateTagData(ATR_ARGS, ATH_ARG NOCONST(UGCProjectReviews) *pUGCProjectReviews)
{
	eaClearStructNoConst(&pUGCProjectReviews->eaTagData, parse_UGCTagData);

	FOR_EACH_IN_EARRAY(pUGCProjectReviews->ppReviews, NOCONST(UGCSingleReview), pUGCSingleReview)
	{
		FOR_EACH_IN_EARRAY_INT(pUGCSingleReview->eaiUGCTags, UGCTag, eUGCTag)
		{
			NOCONST(UGCTagData) *pUGCTagData = eaIndexedGetUsingInt(&pUGCProjectReviews->eaTagData, eUGCTag);
			if(!pUGCTagData)
			{
				pUGCTagData = StructCreateNoConst(parse_UGCTagData);
				pUGCTagData->eUGCTag = eUGCTag;
				eaIndexedPushUsingIntIfPossible(&pUGCProjectReviews->eaTagData, eUGCTag, pUGCTagData);
			}
			pUGCTagData->iCount++;
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	return TRANSACTION_OUTCOME_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// UGC data defined tag loading
///////////////////////////////////////////////////////////////////////////////////////////////////

DefineContext *g_pUGCTags = NULL;

AUTO_STARTUP(UGCTags);
void ugcLoadTags(void)
{
	// Sorry, no reload semantics. It would be possible if DefineLoadFromFile supported replacing the contents of a DefineContext, but it only supports adding to it.
	S32 iSize;

	g_pUGCTags = DefineCreate();
	iSize = DefineLoadFromFile(g_pUGCTags, "UGCTag", "UGCTags", NULL, "defs/config/UGCTags.def", "UGCTags.bin", kUGCTag_FIRST_DATA_DEFINED);

	StaticDefineInt_ExtendWithDefineContext(UGCTagEnum, &g_pUGCTags);

	if(IsClient())
	{
		const char *pchMessageFail = StaticDefineVerifyMessages(UGCTagEnum);
		if(pchMessageFail)
			Errorf("Not all UGCTag messages were found: %s", pchMessageFail);
	}
}

static UGCSearchConfig *s_pUGCSearchConfig = NULL;
static UGCSearchConfigCallback s_pUGCSearchConfigSetupFunction = NULL;
static UGCSearchConfigCallback s_pUGCSearchConfigTeardownFunction = NULL;

UGCSearchConfig *ugcGetSearchConfig()
{
	return s_pUGCSearchConfig;
}

void ugcSetSearchConfigSetupFunction(UGCSearchConfigCallback setupFunction)
{
	s_pUGCSearchConfigSetupFunction = setupFunction;
}

void ugcSetSearchConfigTeardownFunction(UGCSearchConfigCallback teardownFunction)
{
	s_pUGCSearchConfigTeardownFunction = teardownFunction;
}

static void ugcSearchConfigLoad()
{
	loadstart_printf("Loading UGC Search Config...");

	if(s_pUGCSearchConfig)
	{
		if(s_pUGCSearchConfigTeardownFunction)
			s_pUGCSearchConfigTeardownFunction(s_pUGCSearchConfig);

		StructReset(parse_UGCSearchConfig, s_pUGCSearchConfig);
	}

	s_pUGCSearchConfig = StructCreate(parse_UGCSearchConfig);

	if(!ParserLoadFiles(NULL, "genesis/ugc_search_sql.txt", "UGCSearchConfig.bin", PARSER_BINS_ARE_SHARED | PARSER_OPTIONALFLAG, parse_UGCSearchConfig, s_pUGCSearchConfig))
	{
		Errorf("Error loading UGC Search Config.");
		StructReset(parse_UGCSearchConfig, s_pUGCSearchConfig);
	}
	else if(s_pUGCSearchConfigSetupFunction)
		s_pUGCSearchConfigSetupFunction(s_pUGCSearchConfig);

	loadend_printf(" done");
}

static void ugcSearchConfigReload(const char *pchRelPath, int UNUSED_when)
{
	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ugcSearchConfigLoad();
}

AUTO_STARTUP(UGCSearchConfigStartup);
void ugcSearchConfigStartup(void)
{
	ugcSearchConfigLoad();
	if(isDevelopmentMode())
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "genesis/ugc_search_sql.txt", ugcSearchConfigReload);
}
