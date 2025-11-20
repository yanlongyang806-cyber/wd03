#include "stdtypes.h"
#include "EArray.h"
#include "AutoTransDefs.h"
#include "ticketnet.h"
#include "StringFormat.h"
#include "file.h"
#include "logging.h"
#include "GameAccountData/GameAccountData.h"
#include "GameAccountData_h_ast.h"

#include "UGCProjectCommon.h"
#include "UGCProjectCommon_h_ast.h"
#include "UGCCommon.h"
#include "UGCCommon_h_ast.h"

#include "UGCAchievements.h"
#include "UGCAchievements_h_ast.h"

#include "Autogen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/AppServerLib_autogen_remotefuncs.h"

extern ParseTable parse_TicketData[];
#define TYPE_parse_TicketData TicketData

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BEGIN Transactions used by CSR to change UGC data
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".Bbanned, .Ugcreporting.Utemporarybanexpiretime, .Id, .pIDString, .Iowneraccountid, .Powneraccountname, .Ugcreporting.Inaughtyvalue, .Ugcreporting.Unextnaughtydecaytime");
enumTransactionOutcome trTemporaryBanUgcProject(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, const char* pchCSRAccount, U32 bEnableBan)
{
	// NOTE: This does not change the temporary ban count.
	if (NONNULL(pUGCProject) && !pUGCProject->bBanned &&
		((pUGCProject->ugcReporting.uTemporaryBanExpireTime > 0 && !bEnableBan) ||
		(!pUGCProject->ugcReporting.uTemporaryBanExpireTime && bEnableBan)))
	{
		char* estrLogText = NULL;
		estrStackCreate(&estrLogText);
		if (bEnableBan)
		{
			U32 uCurrentTime = timeSecondsSince2000();
			U32 uBanTimer = g_ReportingDef.uTemporaryBanTimer;
			pUGCProject->ugcReporting.uTemporaryBanExpireTime = uCurrentTime + uBanTimer;
		}
		else
		{
			pUGCProject->ugcReporting.uTemporaryBanExpireTime = 0;
		}

		// Temp banned or not, reset the naughty values
		//   WOLF[18Nov11] Per discussion with JeffW, we think manually changing the ban status should reset
		// all naughty values.
		pUGCProject->ugcReporting.iNaughtyValue = 0;
		pUGCProject->ugcReporting.uNextNaughtyDecayTime = 0;

		UGCProject_GetBanStatusString(pUGCProject->id, pUGCProject->iOwnerAccountID, pUGCProject->pOwnerAccountName,
			pchCSRAccount,
			pUGCProject->ugcReporting.iNaughtyValue, 
			pUGCProject->ugcReporting.uTemporaryBanExpireTime,
			bEnableBan, true, &estrLogText);
		TRANSACTION_APPEND_LOG_SUCCESS("%s", estrLogText);
		estrDestroy(&estrLogText);
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	else if (NONNULL(pUGCProject))
	{
		if (pUGCProject->bBanned)
		{
			TRANSACTION_RETURN_LOG_FAILURE("Project %s(%d) is permanently banned",
				pUGCProject->pIDString, pUGCProject->id);
		}
		else
		{
			TRANSACTION_RETURN_LOG_FAILURE("Project %s(%d) is already %s (temporary)",
				pUGCProject->pIDString, pUGCProject->id, bEnableBan ? "banned" : "unbanned");
		}
	}
	TRANSACTION_RETURN_LOG_FAILURE("Couldn't find project");
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".Bbanned, .Ugcreporting.Utemporarybanexpiretime, .Ugcreporting.Unextnaughtydecaytime, .Ugcreporting.Inaughtyvalue, .Ugcreporting.Itemporarybancount, .Id, .pIDString, .Iowneraccountid, .Powneraccountname");
enumTransactionOutcome trBanUgcProject(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, const char* pchCSRAccount, U32 bEnableBan)
{
	if (NONNULL(pUGCProject) && pUGCProject->bBanned != !!bEnableBan)
	{
		char* estrLogText = NULL;
		estrStackCreate(&estrLogText);

		pUGCProject->bBanned = !!bEnableBan;

		// Banned or not, reset the naughty values and the temporary ban count and ban expire timer.
		//   WOLF[18Nov11] Per discussion with JeffW, we think manually changing the ban status should reset
		// all naughty values and temporary ban status.
		pUGCProject->ugcReporting.iTemporaryBanCount=0;
		pUGCProject->ugcReporting.uTemporaryBanExpireTime = 0;
		pUGCProject->ugcReporting.iNaughtyValue = 0;
		pUGCProject->ugcReporting.uNextNaughtyDecayTime = 0;

		UGCProject_GetBanStatusString(pUGCProject->id, pUGCProject->iOwnerAccountID, pUGCProject->pOwnerAccountName,
			pchCSRAccount,
			pUGCProject->ugcReporting.iNaughtyValue, 0, bEnableBan, false, 
			&estrLogText);
		TRANSACTION_APPEND_LOG_SUCCESS("%s", estrLogText);
		estrDestroy(&estrLogText);

		if(pUGCProject->bBanned)
			QueueRemoteCommand_ugcSendMailForProjectChange(ATR_RESULT_SUCCESS, GLOBALTYPE_UGCDATAMANAGER, 0, pUGCProject->id, UGC_CHANGE_CSR_BAN);

		return TRANSACTION_OUTCOME_SUCCESS;
	}
	else if (NONNULL(pUGCProject))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Project %s(%d) is already %s (permanent)",
			pUGCProject->pIDString, pUGCProject->id, bEnableBan ? "banned" : "unbanned");
	}
	TRANSACTION_RETURN_LOG_FAILURE("Couldn't find project");
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".Ugcreporting.Inaughtyvalue, .Ugcreporting.Unextnaughtydecaytime, .pIDString, .Id");
enumTransactionOutcome trClearNaughtyValueForProject(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, const char* pchCSRAccount)
{
	if (ISNULL(pUGCProject))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Couldn't find project");
	}
	else if (!pUGCProject->ugcReporting.iNaughtyValue)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Project %s(%d) already has a naughty value of 0",
			pUGCProject->pIDString, pUGCProject->id);
	}

	pUGCProject->ugcReporting.iNaughtyValue = 0;
	pUGCProject->ugcReporting.uNextNaughtyDecayTime = 0;
	TRANSACTION_RETURN_LOG_SUCCESS("Project %s(%d) had its naughty value cleared by %s",
		pUGCProject->pIDString, pUGCProject->id, pchCSRAccount);
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".Ugcreporting.Bdisableautoban, .pIDString, .Id");
enumTransactionOutcome trDisableAutoBanForProject(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, const char* pchCSRAccount, U32 bDisableAutoBan)
{
	if (ISNULL(pUGCProject))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Couldn't find project");
	}

	pUGCProject->ugcReporting.bDisableAutoBan = bDisableAutoBan;
	TRANSACTION_RETURN_LOG_SUCCESS("Project %s(%d) had its disable auto ban flag set to %d by %s",
		pUGCProject->pIDString, pUGCProject->id, bDisableAutoBan, pchCSRAccount);
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".Ugcreviews.Ppreviews, .Pidstring, .Id");
enumTransactionOutcome trUgcProjectReviewSetHidden(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, const char *pReviewerName, const char* pchCSRAccount, U32 bHidden)
{
	bool bFoundOne=false;
	int i;

	for (i = eaSize(&pUGCProject->ugcReviews.ppReviews)-1; i >= 0; i--)
	{
		if (strcmp(pUGCProject->ugcReviews.ppReviews[i]->pReviewerAccountName, pReviewerName)==0)
		{
			bFoundOne=true;
			pUGCProject->ugcReviews.ppReviews[i]->bHidden = !!bHidden;
		}
	}

	if (bFoundOne)
	{
		TRANSACTION_RETURN_LOG_SUCCESS("Project %s(%d) had the review given by %s change its hidden state to %d by %s",
			pUGCProject->pIDString, pUGCProject->id, pReviewerName, bHidden, pchCSRAccount);
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE("No matching reviews found");
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".ppProjectVersions, .pFeatured, .bAuthorAllowsFeatured, .Pidstring, .Id");
enumTransactionOutcome trUGCFeaturedAddProject(ATR_ARGS, NOCONST(UGCProject)* pUGCProject, const char* pchCSRAccount, const char* pchDetails, U32 iStartTimestamp, U32 iEndTimestamp)
{
	int it;

	U32 curTime = timeSecondsSince2000();
	if( iStartTimestamp == 0 ) {
		TRANSACTION_RETURN_LOG_FAILURE("Project %s(%d) failed to update featured from %d to %d by account %s. Starting time must not be zero.",
			pUGCProject->pIDString, pUGCProject->id, 
			iStartTimestamp, iEndTimestamp,
			pchCSRAccount);
	}
	if( iStartTimestamp >= iEndTimestamp && iEndTimestamp != 0 ) {
		TRANSACTION_RETURN_LOG_FAILURE("Project %s(%d) failed to update featured from %d to %d by account %s. Starting time must be before ending time.",
			pUGCProject->pIDString, pUGCProject->id, 
			iStartTimestamp, iEndTimestamp,
			pchCSRAccount);
	}
	if( iEndTimestamp < curTime && iEndTimestamp != 0 ) {
		TRANSACTION_RETURN_LOG_FAILURE("Project %s(%d) failed to update featured from %d to %d by account %s. Featured end time has already past.",
			pUGCProject->pIDString, pUGCProject->id, 
			iStartTimestamp, iEndTimestamp,
			pchCSRAccount);
	}
	if( !pUGCProject->bAuthorAllowsFeatured ) {
		TRANSACTION_RETURN_LOG_FAILURE("Project %s(%d) failed to update featured from %d to %d by account %s. Author has not requested featuring for this project.",
			pUGCProject->pIDString, pUGCProject->id, 
			iStartTimestamp, iEndTimestamp,
			pchCSRAccount);
	}

	for( it = eaSize( &pUGCProject->ppProjectVersions ) - 1; it >= 0; --it ) {
		if( ugcProjectGetVersionState( pUGCProject->ppProjectVersions[ it ]) == UGC_PUBLISHED ) {
			if( ISNULL( pUGCProject->pFeatured )) {
				pUGCProject->pFeatured = StructCreateNoConst( parse_UGCFeaturedData );
			}

			pUGCProject->pFeatured->strDetails = StructAllocString(pchDetails);
			pUGCProject->pFeatured->iStartTimestamp = iStartTimestamp;
			pUGCProject->pFeatured->iEndTimestamp = iEndTimestamp;

			TRANSACTION_RETURN_LOG_SUCCESS("Project %s(%d) will be featured from %d to %d by account %s.",
				pUGCProject->pIDString, pUGCProject->id, 
				iStartTimestamp, iEndTimestamp,
				pchCSRAccount);
		}
	}

	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".pFeatured, .iDeletionTime, .uUGCFeaturedOrigProjectID, .Pidstring, .Id");
enumTransactionOutcome trUGCFeaturedRemoveProject(ATR_ARGS, NOCONST(UGCProject)* pUGCProject, const char* pchCSRAccount)
{
	if( pUGCProject->uUGCFeaturedOrigProjectID ) {
		pUGCProject->iDeletionTime = timeSecondsSince2000();
	}
	StructDestroyNoConstSafe( parse_UGCFeaturedData, &pUGCProject->pFeatured );

	TRANSACTION_RETURN_LOG_SUCCESS("Project %s(%d) will never be featured by account %s.",
		pUGCProject->pIDString, pUGCProject->id,
		pchCSRAccount);
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".pFeatured.iEndTimestamp, .Pidstring, .Id");
enumTransactionOutcome trUGCFeaturedArchiveProject(ATR_ARGS, NOCONST(UGCProject)* pUGCProject, const char* pchCSRAccount, U32 iEndTimestamp)
{
	U32 curTime = timeSecondsSince2000();
	if( !ISNULL( pUGCProject->pFeatured ))
	{
		if( pUGCProject->pFeatured->iEndTimestamp != 0 && pUGCProject->pFeatured->iEndTimestamp < curTime )
		{
			TRANSACTION_RETURN_LOG_FAILURE("Project %s(%d) failed to update featured end time to %d by account %s. Featured end time has already past.",
				pUGCProject->pIDString, pUGCProject->id,
				iEndTimestamp,
				pchCSRAccount);
		}

		pUGCProject->pFeatured->iEndTimestamp = iEndTimestamp;

		TRANSACTION_RETURN_LOG_SUCCESS("Project %s(%d) featured end time updated to %d by account %s.",
			pUGCProject->pIDString, pUGCProject->id,
			iEndTimestamp,
			pchCSRAccount);
	}

	TRANSACTION_RETURN_LOG_FAILURE("Project %s(%d) failed to update featured end time to %d by account %s. Project has never been featured.",
		pUGCProject->pIDString, pUGCProject->id,
		iEndTimestamp,
		pchCSRAccount);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// END Transactions used by CSR to change UGC data
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

AUTO_TRANS_HELPER;
NOCONST(UGCSingleReview)* aslUGCDataManager_trh_AddReview( ATR_ARGS, ATH_ARG NOCONST(UGCProjectReviews)* pReviews, U32 iReviewerAccountID, const char *pReviewerAccountName, float fRating, const char *pComment )
{
	NOCONST(UGCSingleReview)* pUGCSingleReview = eaIndexedGetUsingInt( &pReviews->ppReviews, iReviewerAccountID );
	S32 iNumRatings;

	if( !iReviewerAccountID ) {
		TRANSACTION_APPEND_LOG_FAILURE( "Invalid account ID" );
		return NULL;
	}
	if( fRating < 0 || fRating > 1.0f ) {
		TRANSACTION_APPEND_LOG_FAILURE( "Rating out of range" );
		return NULL;
	}

	iNumRatings = ugcReviews_GetRatingCount( pReviews );

	if( pUGCSingleReview ) {
		if( !iNumRatings ) {
			TRANSACTION_APPEND_LOG_FAILURE( "Num ratings corrupted" );
			return NULL;
		}

		{
			S32 iBucket = ugcReviews_FindBucketForRating( pUGCSingleReview->fRating );
			if( iBucket >= 0 && iBucket < eaiSize( &pReviews->piNumRatings )) {
				pReviews->piNumRatings[ iBucket ]--;
				MAX1( pReviews->piNumRatings[ iBucket ], 0 );
			}
		}
		{
			S32 iBucket = ugcReviews_FindBucketForRating( fRating );
			if( iBucket >= 0 && iBucket < eaiSize( &pReviews->piNumRatings )) {
				pReviews->piNumRatings[ iBucket ]++;
			}
		}
		pReviews->fRatingSum -= pUGCSingleReview->fRating;
		pUGCSingleReview->fRating = fRating;
		SAFE_FREE( pUGCSingleReview->pComment );
		pUGCSingleReview->pComment = StructAllocString( pComment );
		pUGCSingleReview->iTimestamp = timeSecondsSince2000();
		pReviews->fRatingSum += fRating;
		pReviews->fAverageRating = pReviews->fRatingSum / iNumRatings;
		pReviews->fAdjustedRatingUsingConfidence = ugcReviews_ComputeAdjustedRatingUsingConfidence(pReviews);
	} else {
		pUGCSingleReview = StructCreateNoConst( parse_UGCSingleReview );
		pUGCSingleReview->fRating = fRating;
		pUGCSingleReview->pComment = StructAllocString( pComment );
		pUGCSingleReview->iReviewerAccountID = iReviewerAccountID;
		pUGCSingleReview->pReviewerAccountName = StructAllocString( pReviewerAccountName );
		pUGCSingleReview->iTimestamp = timeSecondsSince2000();
		eaIndexedPushUsingIntIfPossible( &pReviews->ppReviews, iReviewerAccountID, pUGCSingleReview );
		iNumRatings++;

		{
			S32 iBucket = ugcReviews_FindBucketForRating( fRating );
			if( iBucket >= 0 ) {
				pReviews->piNumRatings[ iBucket ]++;
			}
		}
		if( iNumRatings >= ugc_NumReviewsBeforeNonReviewerCanPlay && !pReviews->iTimeBecameReviewed ) {
			pReviews->iTimeBecameReviewed = timeSecondsSince2000();
		}

		pReviews->fRatingSum += fRating;
		pReviews->fAverageRating = pReviews->fRatingSum / iNumRatings;
		pReviews->fAdjustedRatingUsingConfidence = ugcReviews_ComputeAdjustedRatingUsingConfidence(pReviews);
	}

	return pUGCSingleReview;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".id, .iOwnerAccountID, .ugcReviews.piNumRatings, .ugcReviews.fRatingSum, .ugcReviews.iTimeBecameReviewed, .ugcReviews.fAverageRating, .ugcReviews.fAdjustedRatingUsingConfidence, .ugcReviews.ppReviews[]");
enumTransactionOutcome trReviewUgcProject(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, U32 iReviewerAccountID, const char *pReviewerAccountName, float fRating, const char *pComment, int bBetaReviewing)
{
	NOCONST(UGCSingleReview)* pUGCSingleReview = NULL;

	if( iReviewerAccountID == pUGCProject->iOwnerAccountID )
		TRANSACTION_RETURN_LOG_FAILURE("You can't rate your own map");

	pUGCSingleReview = aslUGCDataManager_trh_AddReview( ATR_RECURSE, &pUGCProject->ugcReviews, iReviewerAccountID, pReviewerAccountName, fRating, pComment);
	if(pUGCSingleReview)
	{
		UGCAchievementEvent *event = StructCreate(parse_UGCAchievementEvent);
		event->uUGCAuthorID = pUGCProject->iOwnerAccountID;
		event->uUGCProjectID = pUGCProject->id;
		event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
		event->ugcAchievementServerEvent->ugcProjectReviewedEvent = StructCreate(parse_UGCProjectReviewedEvent);
		event->ugcAchievementServerEvent->ugcProjectReviewedEvent->fRating = fRating;
		event->ugcAchievementServerEvent->ugcProjectReviewedEvent->fHighestRating = pUGCSingleReview->fHighestRating;
		event->ugcAchievementServerEvent->ugcProjectReviewedEvent->iTotalReviews = ugcReviews_GetRatingCount(&pUGCProject->ugcReviews);
		event->ugcAchievementServerEvent->ugcProjectReviewedEvent->iTotalStars = 5 * pUGCProject->ugcReviews.fRatingSum;
		event->ugcAchievementServerEvent->ugcProjectReviewedEvent->fAverageRating = pUGCProject->ugcReviews.fAverageRating;
		event->ugcAchievementServerEvent->ugcProjectReviewedEvent->fAdjustedRatingUsingConfidence = pUGCProject->ugcReviews.fAdjustedRatingUsingConfidence;
		event->ugcAchievementServerEvent->ugcProjectReviewedEvent->bBetaReviewing = bBetaReviewing;
		QueueRemoteCommand_ugcAchievementEvent_Send(ATR_RESULT_SUCCESS, GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
		StructDestroy(parse_UGCAchievementEvent, event);

		event = StructCreate(parse_UGCAchievementEvent);
		event->uUGCAuthorID = iReviewerAccountID;
		event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
		event->ugcAchievementServerEvent->ugcReviewedProjectEvent = StructCreate(parse_UGCReviewedProjectEvent);
		event->ugcAchievementServerEvent->ugcReviewedProjectEvent->fRating = fRating;
		event->ugcAchievementServerEvent->ugcReviewedProjectEvent->fHighestRating = pUGCSingleReview->fHighestRating;
		event->ugcAchievementServerEvent->ugcReviewedProjectEvent->iTotalReviews = ugcReviews_GetRatingCount(&pUGCProject->ugcReviews);
		event->ugcAchievementServerEvent->ugcReviewedProjectEvent->iTotalStars = 5 * pUGCProject->ugcReviews.fRatingSum;
		event->ugcAchievementServerEvent->ugcReviewedProjectEvent->fAverageRating = pUGCProject->ugcReviews.fAverageRating;
		event->ugcAchievementServerEvent->ugcReviewedProjectEvent->fAdjustedRatingUsingConfidence = pUGCProject->ugcReviews.fAdjustedRatingUsingConfidence;
		event->ugcAchievementServerEvent->ugcReviewedProjectEvent->bBetaReviewing = bBetaReviewing;
		QueueRemoteCommand_ugcAchievementEvent_Send(ATR_RESULT_SUCCESS, GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
		StructDestroy(parse_UGCAchievementEvent, event);

		pUGCSingleReview->fHighestRating = MAX(fRating, pUGCSingleReview->fHighestRating);

		return TRANSACTION_OUTCOME_SUCCESS;
	}

	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProjectSeries, ".id, .iOwnerAccountID, .ugcReviews.piNumRatings, .ugcReviews.fRatingSum, .ugcReviews.iTimeBecameReviewed, .ugcReviews.fAverageRating, .ugcReviews.fAdjustedRatingUsingConfidence, .ugcReviews.ppReviews[]");
enumTransactionOutcome trReviewUgcProjectSeries(ATR_ARGS, NOCONST(UGCProjectSeries)* pUGCProjectSeries, U32 iReviewerAccountID, const char *pReviewerAccountName, float fRating, const char *pComment)
{
	NOCONST(UGCSingleReview)* pUGCSingleReview = NULL;

	if( iReviewerAccountID == pUGCProjectSeries->iOwnerAccountID )
		TRANSACTION_RETURN_LOG_FAILURE("You can't rate your own map");

	pUGCSingleReview = aslUGCDataManager_trh_AddReview( ATR_RECURSE, &pUGCProjectSeries->ugcReviews, iReviewerAccountID, pReviewerAccountName, fRating, pComment );
	if(pUGCSingleReview)
	{
		UGCAchievementEvent *event = StructCreate(parse_UGCAchievementEvent);
		event->uUGCAuthorID = pUGCProjectSeries->iOwnerAccountID;
		event->uUGCSeriesID = pUGCProjectSeries->id;
		event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
		event->ugcAchievementServerEvent->ugcSeriesReviewedEvent = StructCreate(parse_UGCSeriesReviewedEvent);
		event->ugcAchievementServerEvent->ugcSeriesReviewedEvent ->fRating = fRating;
		event->ugcAchievementServerEvent->ugcSeriesReviewedEvent ->fHighestRating = pUGCSingleReview->fHighestRating;
		event->ugcAchievementServerEvent->ugcSeriesReviewedEvent ->iTotalReviews = ugcReviews_GetRatingCount(&pUGCProjectSeries->ugcReviews);
		event->ugcAchievementServerEvent->ugcSeriesReviewedEvent ->iTotalStars = 5 * pUGCProjectSeries->ugcReviews.fRatingSum;
		event->ugcAchievementServerEvent->ugcSeriesReviewedEvent ->fAverageRating = pUGCProjectSeries->ugcReviews.fAverageRating;
		event->ugcAchievementServerEvent->ugcSeriesReviewedEvent ->fAdjustedRatingUsingConfidence = pUGCProjectSeries->ugcReviews.fAdjustedRatingUsingConfidence;
		QueueRemoteCommand_ugcAchievementEvent_Send(ATR_RESULT_SUCCESS, GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
		StructDestroy(parse_UGCAchievementEvent, event);

		event = StructCreate(parse_UGCAchievementEvent);
		event->uUGCAuthorID = iReviewerAccountID;
		event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
		event->ugcAchievementServerEvent->ugcReviewedSeriesEvent = StructCreate(parse_UGCReviewedSeriesEvent);
		event->ugcAchievementServerEvent->ugcReviewedSeriesEvent->fRating = fRating;
		event->ugcAchievementServerEvent->ugcReviewedSeriesEvent->fHighestRating = pUGCSingleReview->fHighestRating;
		event->ugcAchievementServerEvent->ugcReviewedSeriesEvent->iTotalReviews = ugcReviews_GetRatingCount(&pUGCProjectSeries->ugcReviews);
		event->ugcAchievementServerEvent->ugcReviewedSeriesEvent->iTotalStars = 5 * pUGCProjectSeries->ugcReviews.fRatingSum;
		event->ugcAchievementServerEvent->ugcReviewedSeriesEvent->fAverageRating = pUGCProjectSeries->ugcReviews.fAverageRating;
		event->ugcAchievementServerEvent->ugcReviewedSeriesEvent->fAdjustedRatingUsingConfidence = pUGCProjectSeries->ugcReviews.fAdjustedRatingUsingConfidence;
		QueueRemoteCommand_ugcAchievementEvent_Send(ATR_RESULT_SUCCESS, GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
		StructDestroy(parse_UGCAchievementEvent, event);

		pUGCSingleReview->fHighestRating = MAX(fRating, pUGCSingleReview->fHighestRating);

		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".Ugcreporting.Inaughtyvalue, .Ugcreporting.Unextnaughtydecaytime, .Ugcreporting.Utemporarybanexpiretime, .Ugcreporting.Eareports, .Ugcreporting.Itemporarybancount, .Ugcreporting.Bdisableautoban, .Bbanned, .Id, .pIDString, .Iowneraccountid, .Powneraccountname, .Iownerlangid");
enumTransactionOutcome trReportUgcProject(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, U32 iReporterAccountID, const char *pcReporterPublicAccountName, U32 eReason, const char *pchDetails)
{
	if (!UGCProject_trh_CanMakeReport(ATR_PASS_ARGS, pUGCProject, iReporterAccountID, eReason, pchDetails))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Account %s (ID %d) couldn't report project (ID %d)",
			pcReporterPublicAccountName, iReporterAccountID, pUGCProject->id);
	}
	else
	{
		U32 uCurrentTime = timeSecondsSince2000();
		NOCONST(UGCProjectReport)* pReport;
		if (eaSize(&pUGCProject->ugcReporting.eaReports) < g_ReportingDef.iMaxReportsPerProject)
		{
			pReport = StructCreateNoConst(parse_UGCProjectReport);
		}
		else
		{
			pReport = eaRemove(&pUGCProject->ugcReporting.eaReports, 0);
		}

		// Create the report
		pReport->uAccountID = iReporterAccountID;
		pReport->uReportTime = uCurrentTime;

		StructCopyString(&pReport->pchReason, StaticDefineIntRevLookup(UGCProjectReportReasonEnum, eReason));
		StructCopyString(&pReport->pchAccountName, pcReporterPublicAccountName);
		StructCopyString(&pReport->pchDetails, pchDetails);
		eaPush(&pUGCProject->ugcReporting.eaReports, pReport);

		// Update naughty value and decay time
		pUGCProject->ugcReporting.iNaughtyValue += g_ReportingDef.iNaughtyIncrement;
		pUGCProject->ugcReporting.uNextNaughtyDecayTime = uCurrentTime + g_ReportingDef.uNaughtyDecayInterval;

		// Check to see if a temporary ban or permanent ban needs to be made
		if (!pUGCProject->ugcReporting.uTemporaryBanExpireTime &&
			pUGCProject->ugcReporting.iNaughtyValue >= g_ReportingDef.iNaughtyThreshold &&
			!pUGCProject->ugcReporting.bDisableAutoBan)
		{
			UGCProjectReportList ReportList = {0};
			ReportList.eaReports = (UGCProjectReport**)pUGCProject->ugcReporting.eaReports;
			if (++pUGCProject->ugcReporting.iTemporaryBanCount >= g_ReportingDef.iTemporaryBanCountResultsInBan)
			{
				// Reset the temporary ban count so we start over if we happen to get unbanned
				pUGCProject->ugcReporting.iTemporaryBanCount=0;
				pUGCProject->bBanned = true;
			}
			else
			{
				pUGCProject->ugcReporting.uTemporaryBanExpireTime = uCurrentTime + g_ReportingDef.uTemporaryBanTimer;
			}

			// Create a CSR ticket and do logging for the ban
			QueueRemoteCommand_aslUGCDataManager_ProjectHandleAutomaticBan(ATR_RESULT_SUCCESS, GLOBALTYPE_UGCDATAMANAGER, 0,
				pUGCProject->id,
				pUGCProject->iOwnerAccountID,
				pUGCProject->pOwnerAccountName,
				pUGCProject->iOwnerLangID,
				pUGCProject->ugcReporting.iNaughtyValue,
				pUGCProject->ugcReporting.uTemporaryBanExpireTime,
				&ReportList);

			// Reset the Naughty values as we just ticked over into banned or temporary banned and we want to start over counting
			//  if we ever get the chance. 
			pUGCProject->ugcReporting.iNaughtyValue = 0;
			pUGCProject->ugcReporting.uNextNaughtyDecayTime = 0;
		}
	}
	TRANSACTION_RETURN_LOG_SUCCESS("Report was made for project %s(%d) by account %s(%d). Current Naughty Value %d. Reason: %s, Details: %s",
		pUGCProject->pIDString, pUGCProject->id, 
		pUGCProject->pOwnerAccountName, pUGCProject->iOwnerAccountID, 
		pUGCProject->ugcReporting.iNaughtyValue,
		StaticDefineIntRevLookup(UGCProjectReportReasonEnum, eReason),
		pchDetails);
}

static void aslUGCDataManager_CreateCSRTicketForBan(ContainerID uProjectID, 
	const char* pchOwnerAccountName,
	U32 eLanguage,
	S32 iNaughtyValue,
	U32 uTemporaryBanExpireTime,
	UGCProjectReportList* pReportList)
{
	char strIdString[UGC_IDSTRING_LENGTH_BUFFER_LENGTH];
	int i;
	char* estrBuffer = NULL;
	TicketData* pTicketData = StructCreate(parse_TicketData);

	UGCIDString_IntToString(uProjectID, /*isSeries=*/false, strIdString);

	pTicketData->pProductName = strdup(GetProductName());
	pTicketData->eVisibility = TICKETVISIBLE_HIDDEN;
	pTicketData->pPlatformName = strdup(PLATFORM_NAME);
	pTicketData->pMainCategory = strdup("CBug.CategoryMain.GM");
	pTicketData->pCategory = strdup("CBug.Category.GM.Behavior");
	pTicketData->pSummary = strdup("Auto-report for UGC project ban");

	estrStackCreate(&estrBuffer);

	//Create the description text
	if (uTemporaryBanExpireTime > 0)
	{
		langFormatMessageKey(eLanguage, &estrBuffer, "UGC.AutoTempBanTicket", 
			STRFMT_INT("ProjectID", uProjectID),
			STRFMT_STRING("ProjectName", strIdString),
			STRFMT_STRING("ExpirationTime", timeGetDateStringFromSecondsSince2000(uTemporaryBanExpireTime)),
			STRFMT_INT("NaughtyValue", iNaughtyValue), 
			STRFMT_END);
	}
	else
	{
		langFormatMessageKey(eLanguage, &estrBuffer, "UGC.AutoBanTicket", 
			STRFMT_INT("ProjectID", uProjectID),
			STRFMT_STRING("ProjectName", strIdString),
			STRFMT_INT("NaughtyValue", iNaughtyValue), 
			STRFMT_END);
	}
	if (pReportList)
	{
		for (i = eaSize(&pReportList->eaReports)-1; i >= 0; i--)
		{
			UGCProjectReport* pReport = pReportList->eaReports[i];
			langFormatMessageKey(eLanguage, &estrBuffer, "UGC.AutoBanPlayerReport",
				STRFMT_STRING("ReporterAccountName", pReport->pchAccountName),
				STRFMT_STRING("ReportTime", timeGetDateStringFromSecondsSince2000(pReport->uReportTime)),
				STRFMT_STRING("Reason", pReport->pchReason),
				STRFMT_STRING("Details", pReport->pchDetails),
				STRFMT_END);
		}
	}
	pTicketData->pUserDescription = strdup(estrBuffer);
	pTicketData->iProductionMode = isProductionMode();
	pTicketData->iMergeID = 0;
	pTicketData->eLanguage = eLanguage;
	pTicketData->uIsInternal = true;
	pTicketData->pAccountName = StructAllocString(pchOwnerAccountName);
	pTicketData->pDisplayName = NULL;
	pTicketData->pCharacterName = NULL;

	//Send CSR ticket
	ticketTrackerSendTicket(pTicketData);
	estrDestroy(&estrBuffer);
}

// Post-transaction processing of an automatic ban
AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void aslUGCDataManager_ProjectHandleAutomaticBan(ContainerID uProjectID,
	ContainerID uOwnerAccountID,
	const char* pchOwnerAccountName,
	U32 eLanguage,
	S32 iNaughtyValue,
	U32 uTemporaryBanExpireTime,
	UGCProjectReportList* pReportList,
	CmdContext* pContext)
{
	char* estrLogText = NULL;
	estrStackCreate(&estrLogText);
	// Create a CSR ticket
	aslUGCDataManager_CreateCSRTicketForBan(uProjectID, 
		pchOwnerAccountName,
		eLanguage,
		iNaughtyValue,
		uTemporaryBanExpireTime,
		pReportList);
	// Do logging
	UGCProject_GetBanStatusString(uProjectID,
		uOwnerAccountID,
		pchOwnerAccountName,
		NULL,
		iNaughtyValue,
		uTemporaryBanExpireTime,
		true, 
		uTemporaryBanExpireTime > 0,
		&estrLogText);
	log_printf(LOG_UGC, "%s", estrLogText);
	estrDestroy(&estrLogText);

	RemoteCommand_ugcSendMailForProjectChange(GLOBALTYPE_UGCDATAMANAGER, 0, uProjectID,
		uTemporaryBanExpireTime == 0 ? UGC_CHANGE_PERMANENT_AUTOBAN : UGC_CHANGE_TEMP_AUTOBAN);
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".id, .iOwnerAccountID")
ATR_LOCKS(pUGCAccountOfEntity, ".eaPlayers[]");
enumTransactionOutcome trUGCNotifyMissionTurnedIn(ATR_ARGS, NOCONST(UGCProject)* pUGCProject, NOCONST(UGCAccount)* pUGCAccountOfEntity, U32 iEntContainerID )
{
	if ( !iEntContainerID ) {
		return TRANSACTION_OUTCOME_FAILURE;
	}
	if( ISNULL( pUGCProject )) {
		return TRANSACTION_OUTCOME_FAILURE;
	}
	if( ISNULL( pUGCAccountOfEntity )) {
		return TRANSACTION_OUTCOME_FAILURE;
	}

	{
		UGCPlayer* pUGCPlayer = eaIndexedGetUsingInt( &pUGCAccountOfEntity->eaPlayers, iEntContainerID );
		if( pUGCPlayer && pUGCPlayer->pSubscription ) {
			NOCONST(UGCAuthorSubscription)* pAuthorSubscription = eaIndexedGetUsingInt( &pUGCPlayer->pSubscription->eaAuthors, pUGCProject->iOwnerAccountID );
			if( pAuthorSubscription ) {
				int projectIndex = eaIndexedFindUsingInt( &pAuthorSubscription->eaCompletedProjects, pUGCProject->id );
				NOCONST(UGCProjectSubscription)* subscribedProject;

				if( projectIndex < 0 ) {
					subscribedProject = StructCreateNoConst( parse_UGCProjectSubscription );
					subscribedProject->projectID = pUGCProject->id;
					eaIndexedPushUsingIntIfPossible( &pAuthorSubscription->eaCompletedProjects, pUGCProject->id, subscribedProject );
				} else {
					subscribedProject = pAuthorSubscription->eaCompletedProjects[ projectIndex ];
				}

				subscribedProject->completedTime = timeSecondsSince2000();
			}
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".id, .iOwnerAccountID");
enumTransactionOutcome trUGCNotifyMissionPlayed(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, ContainerID iPlayerAccountID)
{
	UGCAchievementEvent *event = NULL;

	if( ISNULL( pUGCProject ))
		return TRANSACTION_OUTCOME_FAILURE;

	event = StructCreate(parse_UGCAchievementEvent);
	event->uUGCAuthorID = pUGCProject->iOwnerAccountID;
	event->uUGCProjectID = pUGCProject->id;
	event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
	event->ugcAchievementServerEvent->ugcProjectPlayedEvent = StructCreate(parse_UGCProjectPlayedEvent);
	QueueRemoteCommand_ugcAchievementEvent_Send(ATR_RESULT_SUCCESS, GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
	StructDestroy(parse_UGCAchievementEvent, event);

	event = StructCreate(parse_UGCAchievementEvent);
	event->uUGCAuthorID = iPlayerAccountID;
	event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
	event->ugcAchievementServerEvent->ugcPlayedProjectEvent = StructCreate(parse_UGCPlayedProjectEvent);
	QueueRemoteCommand_ugcAchievementEvent_Send(ATR_RESULT_SUCCESS, GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
	StructDestroy(parse_UGCAchievementEvent, event);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".Imostrecentplayedtime");
enumTransactionOutcome trUpdateProjectPlayTime(ATR_ARGS, NOCONST(UGCProject) *pUGCProject)
{
	pUGCProject->iMostRecentPlayedTime = timeSecondsSince2000();
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pSubscriberAccount, ".accountID, .eaPlayers[]")
ATR_LOCKS(pAuthorAccount, ".accountID, author.subscribers.eaPlayers[]");
enumTransactionOutcome trSubscribeToAuthor(ATR_ARGS, NOCONST(UGCAccount)* pSubscriberAccount, ContainerID uSubscriberPlayerID, NOCONST(UGCAccount)* pAuthorAccount)
{
	NOCONST(UGCSubscriber) *pSubscriber = NULL;

	if(ISNULL(pAuthorAccount))
		TRANSACTION_RETURN_LOG_FAILURE("pAuthorAccount is NULL");

	if(TRANSACTION_OUTCOME_FAILURE == ugc_trh_SubscribeToAuthor(ATR_PASS_ARGS, pSubscriberAccount, uSubscriberPlayerID, pAuthorAccount->accountID))
		return TRANSACTION_OUTCOME_FAILURE;

	pSubscriber = StructCreateNoConst(parse_UGCSubscriber);
	pSubscriber->uPlayer = uSubscriberPlayerID;
	pSubscriber->uAccount = pSubscriberAccount->accountID;
	if(!eaIndexedPushUsingIntIfPossible(&pAuthorAccount->author.subscribers.eaPlayers, uSubscriberPlayerID, pSubscriber))
		StructDestroyNoConst(parse_UGCSubscriber, pSubscriber);

	TRANSACTION_RETURN_LOG_SUCCESS("UGC: Subscriber %u (Player %u) has subscribed to author %u", pSubscriberAccount->accountID, uSubscriberPlayerID, pAuthorAccount->accountID);
}

AUTO_TRANSACTION
ATR_LOCKS(pSubscriberAccount, ".accountID, .eaPlayers[]")
ATR_LOCKS(pAuthorAccount, ".accountID, author.subscribers.eaPlayers[]");
enumTransactionOutcome trUnsubscribeFromAuthor( ATR_ARGS, NOCONST(UGCAccount)* pSubscriberAccount, ContainerID uSubscriberPlayerID, NOCONST(UGCAccount)* pAuthorAccount )
{
	NOCONST(UGCSubscriber) *pSubscription = NULL;

	if(ISNULL(pAuthorAccount))
		TRANSACTION_RETURN_LOG_FAILURE("pAuthorAccount is NULL");

	if(TRANSACTION_OUTCOME_FAILURE == ugc_trh_UnsubscribeFromAuthor(ATR_PASS_ARGS, pSubscriberAccount, uSubscriberPlayerID, pAuthorAccount->accountID))
		return TRANSACTION_OUTCOME_FAILURE;

	pSubscription = eaIndexedRemoveUsingInt(&pAuthorAccount->author.subscribers.eaPlayers, uSubscriberPlayerID);
	if(pSubscription)
		StructDestroyNoConst(parse_UGCSubscriber, pSubscription);

	TRANSACTION_RETURN_LOG_SUCCESS("UGC: Subscriber %u (Player %u) has unsubscribed from author %u", pSubscriberAccount->accountID, uSubscriberPlayerID, pAuthorAccount->accountID);
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".bAuthorAllowsFeatured, .pFeatured");
enumTransactionOutcome trSetAuthorAllowsFeatured(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, int bAuthorAllowsFeatured, int bErrorIfAlreadyFeatured)
{
	if(bAuthorAllowsFeatured)
		pUGCProject->bAuthorAllowsFeatured = true;
	else {
		if(bErrorIfAlreadyFeatured && pUGCProject->pFeatured)
			TRANSACTION_RETURN_LOG_FAILURE("Project has already been featured");
		pUGCProject->bAuthorAllowsFeatured = false;
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".ugcStats.uTotalDropCount");
enumTransactionOutcome trIncrementDropCountStat(ATR_ARGS, NOCONST(UGCProject) *pUGCProject)
{
	pUGCProject->ugcStats.uTotalDropCount++;

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pBucket, ".*");
static void gslUGC_trh_UGCProjectDurationBucketDestructor(ATH_ARG NOCONST(UGCProjectDurationBucket)* pBucket)
{
	if(pBucket)
		StructDestroyNoConst(parse_UGCProjectDurationBucket, pBucket);
}

// Buckets which fall outside of 1.5*Innerquartile range are ignored
AUTO_TRANS_HELPER
ATR_LOCKS(pStats, ".Eadurationbuckets");
static F32 aslUGCDataManager_trh_GetAverageDurationInMinutes_IgnoreOutliers(ATR_ARGS, ATH_ARG NOCONST(UGCProjectDurationStats)* pStats)
{
	F32 fAverage = 0.f;

	F32 fTotalCount = 0;
	F32 fModifiedCount = 0;
	F32 fModifiedSum = 0;
	int iQ1Idx = 0;
	F32 fQ1Val = 0.f;
	int iQ3Idx = 0;
	F32 fQ3Val = 0.f;
	F32 fIQR = 0.f;
	int iTempCount = 0;
	int i;

	// Collect the total count
	for (i=0; i < eaSize(&pStats->eaDurationBuckets); i++) 
	{
		fTotalCount = (fTotalCount + pStats->eaDurationBuckets[i]->uCount);
	}

	// Determine IQR
	iQ1Idx = fTotalCount / 4;
	for (i=0; i < eaSize(&pStats->eaDurationBuckets) && iTempCount < iQ1Idx; i++) 
	{
		iTempCount = (iTempCount + pStats->eaDurationBuckets[i]->uCount);
	}
	if(i > 0)
	{
		i--;
		fQ1Val = pStats->eaDurationBuckets[i]->uAverage;
	}

	iQ3Idx = iQ1Idx * 3;
	iTempCount = fTotalCount;
	for (i=eaSize(&pStats->eaDurationBuckets)-1; i >= 0 && iTempCount > iQ3Idx; i--) 
	{
		iTempCount = (iTempCount - pStats->eaDurationBuckets[i]->uCount);
	}
	if(i < eaSize(&pStats->eaDurationBuckets)-1)
	{
		i++;
		fQ3Val = pStats->eaDurationBuckets[i]->uAverage;
	}

	fIQR = fQ3Val - fQ1Val;

	// Adjust Q1 and Q3 for convenience in finding outliers
	fQ1Val -= 1.5*fIQR;
	fQ3Val += 1.5*fIQR;

	// Find modified count and sum ignoring outlier buckets
	for (i=0; i < eaSize(&pStats->eaDurationBuckets); i++) 
	{
		if( pStats->eaDurationBuckets[i]->uAverage >= fQ1Val &&
			pStats->eaDurationBuckets[i]->uAverage <= fQ3Val)
		{
			fModifiedCount = (fModifiedCount + pStats->eaDurationBuckets[i]->uCount);
			fModifiedSum = (fModifiedSum + pStats->eaDurationBuckets[i]->uSum);
		}
	}

	if(fModifiedCount)
	{
		fAverage = fModifiedSum/fModifiedCount;
	}

	return fAverage;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pDurationStats, ".Eadurationbuckets");
static void aslUGCDataManager_trh_FixupUGCDurationStats(ATR_ARGS, ATH_ARG NOCONST(UGCProjectDurationStats)* pDurationStats)
{
	int iRangeToRemove = 0;
	if(ISNULL(pDurationStats->eaDurationBuckets))
	{
		eaCreate(&pDurationStats->eaDurationBuckets);
	}
	while(eaSize(&pDurationStats->eaDurationBuckets) < MAX_UGC_DURATION_BUCKETS)
	{
		eaPush(&pDurationStats->eaDurationBuckets, StructCreateNoConst(parse_UGCProjectDurationBucket));
	}
	iRangeToRemove = eaSize(&pDurationStats->eaDurationBuckets) - MAX(MAX_UGC_DURATION_BUCKETS, 0);
	if(iRangeToRemove > 0)
	{
		eaRemoveTailEx(&pDurationStats->eaDurationBuckets, iRangeToRemove, gslUGC_trh_UGCProjectDurationBucketDestructor);
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pStats, ".Completionstats.Ucurrentdaytimestamp, .Completionstats.Uremainingcompletedcount, .Completionstats.Eaicompletedcountbyday, .Durationstats.Eadurationbuckets");
static bool aslUGCDataManager_trh_FixupUGCProjectStats(ATR_ARGS, ATH_ARG NOCONST(UGCProjectStats)* pStats)
{
	U32 uCurrentTime = timeSecondsSince2000();
	U32 uSecondsInDay = 86400;
	int i;

	if(ISNULL(pStats))
		return false;

	// FIXUP COMPLETION STATS
	if(ISNULL(pStats->completionStats.eaiCompletedCountByDay))
	{
		eaiCreate(&pStats->completionStats.eaiCompletedCountByDay);
	}
	if(eaiSize(&pStats->completionStats.eaiCompletedCountByDay) != MAX_UGC_COMPLETION_BUCKETS)
	{
		eaiSetSize(&pStats->completionStats.eaiCompletedCountByDay, MAX_UGC_COMPLETION_BUCKETS);
	}

	if(pStats->completionStats.uCurrentDayTimestamp != 0)
	{
		U32 uCurrentDay = uCurrentTime/uSecondsInDay;
		U32 uLastRecordedDay = pStats->completionStats.uCurrentDayTimestamp/uSecondsInDay;

		// Update bucket contents due to a day change
		if(uCurrentDay > uLastRecordedDay)
		{
			U32 uDayDifference = uCurrentDay - uLastRecordedDay;
			uDayDifference = CLAMP(uDayDifference, 0, 7);

			for(i = MAX_UGC_COMPLETION_BUCKETS-1; i >= 0; i--)
			{
				U32 uNewIndex = i + uDayDifference;

				if(uNewIndex >= MAX_UGC_COMPLETION_BUCKETS)
				{
					// no more buckets, add to remainder bucket
					pStats->completionStats.uRemainingCompletedCount += pStats->completionStats.eaiCompletedCountByDay[i];
				}
				else
				{
					// move the contents of this bucket forward by uDayDifference buckets
					pStats->completionStats.eaiCompletedCountByDay[uNewIndex] = pStats->completionStats.eaiCompletedCountByDay[i];
				}

				// clear the current bucket
				pStats->completionStats.eaiCompletedCountByDay[i] = 0;
			}
			// Set the current timestamp
			pStats->completionStats.uCurrentDayTimestamp = uCurrentTime;
		}
	}
	else
	{
		// Set the current timestamp
		pStats->completionStats.uCurrentDayTimestamp = uCurrentTime;
	}

	aslUGCDataManager_trh_FixupUGCDurationStats(ATR_PASS_ARGS, &pStats->durationStats);

	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pDurationStats, ".Eadurationbuckets, .Iaveragedurationinminutes_Ignoreoutliers");
bool aslUGCDataManager_trh_RecordCompletionDuration(ATR_ARGS, ATH_ARG NOCONST(UGCProjectDurationStats)* pDurationStats, U32 uDurationInMinutes)
{
	int iBucketIndex = 0;
	if(ISNULL(pDurationStats) || ISNULL(pDurationStats->eaDurationBuckets))
		return false;

	iBucketIndex = (uDurationInMinutes / UGC_DURATION_BUCKET_SIZE_IN_MINUTES);
	iBucketIndex = CLAMP(iBucketIndex, 0, MAX_UGC_DURATION_BUCKETS-1);

	if(eaSize(&pDurationStats->eaDurationBuckets) <= iBucketIndex)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Unable to record UGC project completion duration, duration buckets not fixed up (actual buckets < max buckets)");
		return false;
	}

	pDurationStats->eaDurationBuckets[iBucketIndex]->uSum += uDurationInMinutes;
	pDurationStats->eaDurationBuckets[iBucketIndex]->uCount++;
	pDurationStats->eaDurationBuckets[iBucketIndex]->uAverage = pDurationStats->eaDurationBuckets[iBucketIndex]->uSum/pDurationStats->eaDurationBuckets[iBucketIndex]->uCount;

	pDurationStats->iAverageDurationInMinutes_IgnoreOutliers = aslUGCDataManager_trh_GetAverageDurationInMinutes_IgnoreOutliers(ATR_PASS_ARGS, pDurationStats);

	return true;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".Ppprojectversions, .Ugcstats.Completionstats.Eaicompletedcountbyday, .Pidstring, .Ugcstats.Completionstats.Ucurrentdaytimestamp, .Ugcstats.Completionstats.Uremainingcompletedcount, .Ugcstats.Durationstats.Eadurationbuckets, .Ugcstats.Durationstats.Iaveragedurationinminutes_Ignoreoutliers, .Ugcstats.Iaveragedurationinminutes_Usingmaps, .Ugcstats.Eadurationstatsbymap, .Bbanned, .Ugcreporting.Utemporarybanexpiretime");
enumTransactionOutcome trRecordCompletion(ATR_ARGS, NOCONST(UGCProject)* pUGCProject, const char *pcVersionNameSpace, U32 uDurationInMinutes, U32 bRecordCompletion)
{
	if(ISNULL(pUGCProject))
		return TRANSACTION_OUTCOME_FAILURE;

	if(!aslUGCDataManager_trh_FixupUGCProjectStats(ATR_PASS_ARGS, &pUGCProject->ugcStats))
		TRANSACTION_RETURN_LOG_FAILURE("Unable to record mission completion to UGC Project (ID String: %s), failed while fixing up stats.", pUGCProject->pIDString);

	if(bRecordCompletion)
		pUGCProject->ugcStats.completionStats.eaiCompletedCountByDay[0]++;

	if(!aslUGCDataManager_trh_RecordCompletionDuration(ATR_PASS_ARGS, &pUGCProject->ugcStats.durationStats, uDurationInMinutes))
		TRANSACTION_RETURN_LOG_FAILURE("Unable to record mission completion to UGC Project (ID String: %s), failed while recording completion duration.", pUGCProject->pIDString);

	// When a completion of an entire UGC mission is recorded, update the average duration using the maps for the most recent playable version
	{
		NOCONST(UGCProjectVersion) *pMostRecentPlayableVersion = NULL;
		FOR_EACH_IN_EARRAY(pUGCProject->ppProjectVersions, NOCONST(UGCProjectVersion), pVersion)
		{
			if(UGCProject_trh_VersionIsPlayable(ATR_PASS_ARGS, pUGCProject, pVersion))
			{
				pMostRecentPlayableVersion = pVersion;
				break;
			}
		}
		FOR_EACH_END;

		if(pMostRecentPlayableVersion)
		{
			pUGCProject->ugcStats.iAverageDurationInMinutes_UsingMaps = 0.0f;
			FOR_EACH_IN_EARRAY(pUGCProject->ugcStats.eaDurationStatsByMap, NOCONST(UGCProjectMapDurationStats), mapstats)
			{
				FOR_EACH_IN_EARRAY(pMostRecentPlayableVersion->ppMapNames, const char, name)
				{
					if(0 == stricmp(name, mapstats->pName))
					{
						pUGCProject->ugcStats.iAverageDurationInMinutes_UsingMaps += mapstats->durationStats.iAverageDurationInMinutes_IgnoreOutliers;
						break;
					}
				}
				FOR_EACH_END;
			}
			FOR_EACH_END;
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".Ugcstats.Eadurationstatsbymap, .id");
enumTransactionOutcome trRecordMapCompletion(ATR_ARGS, NOCONST(UGCProject)* pUGCProject, const char *pcBaseMapName, U32 uDurationInMinutes)
{
	NOCONST(UGCProjectMapDurationStats) *mapstats = NULL;

	if(ISNULL(pUGCProject))
		return TRANSACTION_OUTCOME_FAILURE;

	FOR_EACH_IN_EARRAY(pUGCProject->ugcStats.eaDurationStatsByMap, NOCONST(UGCProjectMapDurationStats), thismapstats)
	{
		if(stricmp(thismapstats->pName, pcBaseMapName) == 0)
		{
			mapstats = thismapstats;
			break;
		}
	}
	FOR_EACH_END;

	if(mapstats)
	{
		aslUGCDataManager_trh_FixupUGCDurationStats(ATR_PASS_ARGS, &mapstats->durationStats);

		if(!aslUGCDataManager_trh_RecordCompletionDuration(ATR_PASS_ARGS, &mapstats->durationStats, uDurationInMinutes))
			TRANSACTION_RETURN_LOG_FAILURE("Unable to record map completion to UGC Project version (id: %u, map: %s), failed while recording map completion duration.", pUGCProject->id, pcBaseMapName);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCAccount, ".author.uLastAchievementNotifyTime");
enumTransactionOutcome trUgcAchievementsNotify(ATR_ARGS, NOCONST(UGCAccount) *pUGCAccount, U32 uUGCAccountID, U32 uTime)
{
	if(uTime > pUGCAccount->author.uLastAchievementNotifyTime)
		pUGCAccount->author.uLastAchievementNotifyTime = uTime;
	else
		TRANSACTION_APPEND_LOG_SUCCESS(UGC_ACHIEVEMENT_TRANSACTION_NOTIFY_TIME_DID_NOT_INCREASE);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".ugcLifetimeTips");
enumTransactionOutcome trUgcProjectIncrementLifetimeTips(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, U32 uTipAmount)
{
	pUGCProject->ugcLifetimeTips += uTipAmount;

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pProject, ".Id, .Ppprojectversions, .bAuthorAllowsFeatured, .pFeatured")
ATR_LOCKS(pProjectSeries, ".ugcSearchCache.eaPublishedProjectIDs");
enumTransactionOutcome trWithdrawProject(ATR_ARGS, NOCONST(UGCProject) *pProject, NOCONST(UGCProjectSeries) *pProjectSeries, char *pComment)
{
	if (!UGCProject_CanBeWithdrawn(pProject))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Project was not in a state that could be withdrawn");
	}

	if( !UGCProject_WithdrawProject(ATR_RECURSE, pProject, pProjectSeries, /*bWithdraw=*/true, pComment)) {
		return TRANSACTION_OUTCOME_FAILURE;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}
