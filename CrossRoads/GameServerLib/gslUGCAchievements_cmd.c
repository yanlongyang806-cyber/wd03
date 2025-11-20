#include "UGCAchievements.h"
#include "UGCCommon.h"
#include "UGCProjectCommon.h"
#include "UGCProjectUtils.h"

#include "LoggedTransactions.h"
#include "StringFormat.h"
#include "NotifyCommon.h"
#include "EntityLib.h"
#include "rewardCommon.h"
#include "Reward.h"

#include "file.h"
#include "ServerLib.h"
#include "utilitiesLib.h"

#include "Entity.h"
#include "Player.h"

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

static bool s_bUGCAchievementRewardsEnable = false;

AUTO_CMD_INT(s_bUGCAchievementRewardsEnable, UGCAchievementRewardsEnable) ACMD_ACCESSLEVEL(9);

AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_SERVERCMD; // access level 7 because its a debug only command
void gslUGC_SynchronizeAchievements(Entity* pEntity)
{
	ContainerID ugcAccountID = entGetAccountID(pEntity);
	if(ugcAccountID)
	{
		UGCSynchronizeAchievementsData ugcSynchronizeAchievementsData;
		StructInit(parse_UGCSynchronizeAchievementsData, &ugcSynchronizeAchievementsData);

		ugcSynchronizeAchievementsData.pcShard = GetShardNameFromShardInfoString();
		ugcSynchronizeAchievementsData.entContainerID = entGetContainerID(pEntity);
		ugcSynchronizeAchievementsData.ugcAccountID = ugcAccountID;

		RemoteCommand_Intershard_ugcAchievement_SynchronizeAchievements(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
			&ugcSynchronizeAchievementsData, __FUNCTION__);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(2) ACMD_SERVERCMD ACMD_HIDE ACMD_PRIVATE; // access level 2 because only foundry authors will call this and only while editing
void gslUGC_SendAchievementEvent(Entity* pEntity, UGCAchievementEvent *event)
{
	if(!isProductionEditMode()) return;

	// IMPORTANT!
	//
	// The client should never be sending achievement events that are supposed to come from servers! This check protects against spoofing!
	if(event->ugcAchievementServerEvent)
	{
		AssertOrAlertWithStruct("UGC_RECEIVED_A_SERVER_ONLY_UGC_ACHIEVEMENT_EVENT_FROM_A_CLIENT", parse_UGCAchievementEvent, event,
								"Entity ref: %d, Entity Container ID: %d, UGC Account ID: %d, for function: %s", pEntity->myRef, pEntity->myContainerID, event->uUGCAuthorID, __FUNCTION__);
		return;
	}

	RemoteCommand_ugcAchievementEvent_Send(GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gslUGC_ugcAchievementEvent_Send(UGCAchievementEvent *event, const char *reason)
{
	RemoteCommand_Intershard_ugcAchievementEvent_Send(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, event, reason);
}

AUTO_COMMAND ACMD_NAME(UGCAchievementsReset) ACMD_ACCESSLEVEL(7); // access level 7 because its a debug only command
void gslUGC_AchievementsReset(Entity* pEntity)
{
	UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);
	if(pUGCAccount)
	{
		VerifyServerTypeExistsInShard(GLOBALTYPE_UGCDATAMANAGER);
		RemoteCommand_ugcAchievement_Reset(GLOBALTYPE_UGCDATAMANAGER, 0, pUGCAccount->accountID, __FUNCTION__);
	}
}

AUTO_COMMAND ACMD_NAME(UGCAchievementsGrant) ACMD_ACCESSLEVEL(7); // access level 7 because its a debug only command
void gslUGC_AchievementsGrant(Entity* pEntity, U32 uUGCProjectID, U32 uUGCSeriesID, ACMD_NAMELIST("AllUGCAchievementsIndex", RESOURCEDICTIONARY) char *ugcAchievementName)
{
	UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);
	if(pUGCAccount)
		RemoteCommand_Intershard_ugcAchievement_Grant(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, pUGCAccount->accountID, uUGCProjectID, uUGCSeriesID, ugcAchievementName, __FUNCTION__);
}

AUTO_COMMAND ACMD_NAME(UGCAchievementEventSend_ProjectPublished) ACMD_ACCESSLEVEL(7); // access level 7 because its a debug only command
void gslUGC_AchievementEventSend_ProjectPublished(Entity* pEntity, U32 uUGCAuthorAccountID, U32 uUGCProjectID)
{
	UGCAchievementEvent *event = StructCreate(parse_UGCAchievementEvent);
	event->uUGCAuthorID = uUGCAuthorAccountID;
	event->uUGCProjectID = uUGCProjectID;
	event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
	event->ugcAchievementServerEvent->ugcProjectPublishedEvent = StructCreate(parse_UGCProjectPublishedEvent);
	RemoteCommand_Intershard_ugcAchievementEvent_Send(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
	StructDestroy(parse_UGCAchievementEvent, event);
}

AUTO_COMMAND ACMD_NAME(UGCAchievementEventSend_SeriesPublished) ACMD_ACCESSLEVEL(7); // access level 7 because its a debug only command
void gslUGC_AchievementEventSend_SeriesPublished(Entity* pEntity, U32 uUGCAuthorAccountID, U32 uUGCSeriesID)
{
	UGCAchievementEvent *event = StructCreate(parse_UGCAchievementEvent);
	event->uUGCAuthorID = uUGCAuthorAccountID;
	event->uUGCSeriesID = uUGCSeriesID;
	event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
	event->ugcAchievementServerEvent->ugcSeriesPublishedEvent = StructCreate(parse_UGCSeriesPublishedEvent);
	RemoteCommand_Intershard_ugcAchievementEvent_Send(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
	StructDestroy(parse_UGCAchievementEvent, event);
}

AUTO_COMMAND ACMD_NAME(UGCAchievementEventSend_ProjectPlayed) ACMD_ACCESSLEVEL(7); // access level 7 because its a debug only command
void gslUGC_AchievementEventSend_ProjectPlayed(Entity* pEntity, U32 uUGCAuthorAccountID, U32 uUGCProjectID)
{
	UGCAchievementEvent *event = StructCreate(parse_UGCAchievementEvent);
	event->uUGCAuthorID = uUGCAuthorAccountID;
	event->uUGCProjectID = uUGCProjectID;
	event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
	event->ugcAchievementServerEvent->ugcProjectPlayedEvent = StructCreate(parse_UGCProjectPlayedEvent);
	RemoteCommand_Intershard_ugcAchievementEvent_Send(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
	StructDestroy(parse_UGCAchievementEvent, event);

	event = StructCreate(parse_UGCAchievementEvent);
	event->uUGCAuthorID = entGetAccountID(pEntity);
	event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
	event->ugcAchievementServerEvent->ugcPlayedProjectEvent = StructCreate(parse_UGCPlayedProjectEvent);
	RemoteCommand_Intershard_ugcAchievementEvent_Send(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
	StructDestroy(parse_UGCAchievementEvent, event);
}

AUTO_COMMAND ACMD_NAME(UGCAchievementEventSend_ProjectReviewed) ACMD_ACCESSLEVEL(7); // access level 7 because its a debug only command
void gslUGC_AchievementEventSend_ProjectReviewed(Entity* pEntity, U32 uUGCAuthorAccountID, U32 uUGCProjectID, F32 fRating, F32 fHighestPreviousRating,
	U32 iTotalReviews, U32 iTotalStars, F32 fAverageRating, F32 fAdjustedRating, bool bBetaReviewing)
{
	UGCAchievementEvent *event = StructCreate(parse_UGCAchievementEvent);
	event->uUGCAuthorID = uUGCAuthorAccountID;
	event->uUGCProjectID = uUGCProjectID;
	event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
	event->ugcAchievementServerEvent->ugcProjectReviewedEvent = StructCreate(parse_UGCProjectReviewedEvent);
	event->ugcAchievementServerEvent->ugcProjectReviewedEvent->fRating = fRating;
	event->ugcAchievementServerEvent->ugcProjectReviewedEvent->fHighestRating = fHighestPreviousRating;
	event->ugcAchievementServerEvent->ugcProjectReviewedEvent->iTotalReviews = iTotalReviews;
	event->ugcAchievementServerEvent->ugcProjectReviewedEvent->iTotalStars = iTotalStars;
	event->ugcAchievementServerEvent->ugcProjectReviewedEvent->fAverageRating = fAverageRating;
	event->ugcAchievementServerEvent->ugcProjectReviewedEvent->fAdjustedRatingUsingConfidence = fAdjustedRating;
	event->ugcAchievementServerEvent->ugcProjectReviewedEvent->bBetaReviewing = bBetaReviewing;
	RemoteCommand_Intershard_ugcAchievementEvent_Send(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
	StructDestroy(parse_UGCAchievementEvent, event);

	event = StructCreate(parse_UGCAchievementEvent);
	event->uUGCAuthorID = entGetAccountID(pEntity);
	event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
	event->ugcAchievementServerEvent->ugcReviewedProjectEvent = StructCreate(parse_UGCReviewedProjectEvent);
	event->ugcAchievementServerEvent->ugcReviewedProjectEvent->fRating = fRating;
	event->ugcAchievementServerEvent->ugcReviewedProjectEvent->fHighestRating = fHighestPreviousRating;
	event->ugcAchievementServerEvent->ugcReviewedProjectEvent->iTotalReviews = iTotalReviews;
	event->ugcAchievementServerEvent->ugcReviewedProjectEvent->iTotalStars = iTotalStars;
	event->ugcAchievementServerEvent->ugcReviewedProjectEvent->fAverageRating = fAverageRating;
	event->ugcAchievementServerEvent->ugcReviewedProjectEvent->fAdjustedRatingUsingConfidence = fAdjustedRating;
	event->ugcAchievementServerEvent->ugcReviewedProjectEvent->bBetaReviewing = bBetaReviewing;
	RemoteCommand_Intershard_ugcAchievementEvent_Send(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
	StructDestroy(parse_UGCAchievementEvent, event);
}

AUTO_COMMAND ACMD_NAME(UGCAchievementEventSend_ProjectTipped) ACMD_ACCESSLEVEL(7); // access level 7 because its a debug only command
void gslUGC_AchievementEventSend_ProjectTipped(Entity* pEntity, U32 uUGCAuthorAccountID, U32 uUGCProjectID, U32 uTipAmount)
{
	UGCAchievementEvent *event = StructCreate(parse_UGCAchievementEvent);
	event->uUGCAuthorID = uUGCAuthorAccountID;
	event->uUGCProjectID = uUGCProjectID;
	event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
	event->ugcAchievementServerEvent->ugcProjectTippedEvent = StructCreate(parse_UGCProjectTippedEvent);
	event->ugcAchievementServerEvent->ugcProjectTippedEvent->uTipAmount = uTipAmount;
	RemoteCommand_Intershard_ugcAchievementEvent_Send(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
	StructDestroy(parse_UGCAchievementEvent, event);

	event = StructCreate(parse_UGCAchievementEvent);
	event->uUGCAuthorID = entGetAccountID(pEntity);
	event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
	event->ugcAchievementServerEvent->ugcTippedProjectEvent = StructCreate(parse_UGCTippedProjectEvent);
	event->ugcAchievementServerEvent->ugcTippedProjectEvent->uTipAmount = uTipAmount;
	RemoteCommand_Intershard_ugcAchievementEvent_Send(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
	StructDestroy(parse_UGCAchievementEvent, event);
}

AUTO_COMMAND ACMD_NAME(UGCAchievementEventSend_SeriesReviewed) ACMD_ACCESSLEVEL(7); // access level 7 because its a debug only command
void gslUGC_AchievementEventSend_SeriesReviewed(Entity* pEntity, U32 uUGCAuthorAccountID, U32 uUGCSeriesID, F32 fRating, F32 fHighestPreviousRating, U32 iTotalReviews,
	U32 iTotalStars, F32 fAverageRating, F32 fAdjustedRating)
{
	UGCAchievementEvent *event = StructCreate(parse_UGCAchievementEvent);
	event->uUGCAuthorID = uUGCAuthorAccountID;
	event->uUGCSeriesID = uUGCSeriesID;
	event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
	event->ugcAchievementServerEvent->ugcSeriesReviewedEvent = StructCreate(parse_UGCSeriesReviewedEvent);
	event->ugcAchievementServerEvent->ugcSeriesReviewedEvent->fRating = fRating;
	event->ugcAchievementServerEvent->ugcSeriesReviewedEvent->fHighestRating = fHighestPreviousRating;
	event->ugcAchievementServerEvent->ugcSeriesReviewedEvent->iTotalReviews = iTotalReviews;
	event->ugcAchievementServerEvent->ugcSeriesReviewedEvent->iTotalStars = iTotalStars;
	event->ugcAchievementServerEvent->ugcSeriesReviewedEvent->fAverageRating = fAverageRating;
	event->ugcAchievementServerEvent->ugcSeriesReviewedEvent->fAdjustedRatingUsingConfidence = fAdjustedRating;
	RemoteCommand_Intershard_ugcAchievementEvent_Send(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
	StructDestroy(parse_UGCAchievementEvent, event);

	event = StructCreate(parse_UGCAchievementEvent);
	event->uUGCAuthorID = entGetAccountID(pEntity);
	event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
	event->ugcAchievementServerEvent->ugcReviewedSeriesEvent = StructCreate(parse_UGCReviewedSeriesEvent);
	event->ugcAchievementServerEvent->ugcReviewedSeriesEvent->fRating = fRating;
	event->ugcAchievementServerEvent->ugcReviewedSeriesEvent->fHighestRating = fHighestPreviousRating;
	event->ugcAchievementServerEvent->ugcReviewedSeriesEvent->iTotalReviews = iTotalReviews;
	event->ugcAchievementServerEvent->ugcReviewedSeriesEvent->iTotalStars = iTotalStars;
	event->ugcAchievementServerEvent->ugcReviewedSeriesEvent->fAverageRating = fAverageRating;
	event->ugcAchievementServerEvent->ugcReviewedSeriesEvent->fAdjustedRatingUsingConfidence = fAdjustedRating;
	RemoteCommand_Intershard_ugcAchievementEvent_Send(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
	StructDestroy(parse_UGCAchievementEvent, event);
}

AUTO_COMMAND ACMD_NAME(UGCAchievementEventSend_ProjectFeatured) ACMD_ACCESSLEVEL(7); // access level 7 because its a debug only command
void gslUGC_AchievementEventSend_ProjectFeatured(Entity* pEntity, U32 uUGCAuthorAccountID, U32 uUGCProjectID)
{
	UGCAchievementEvent *event = StructCreate(parse_UGCAchievementEvent);
	event->uUGCAuthorID = uUGCAuthorAccountID;
	event->uUGCProjectID = uUGCProjectID;
	event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
	event->ugcAchievementServerEvent->ugcProjectFeaturedEvent = StructCreate(parse_UGCProjectFeaturedEvent);
	RemoteCommand_Intershard_ugcAchievementEvent_Send(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
	StructDestroy(parse_UGCAchievementEvent, event);
}

AUTO_COMMAND ACMD_NAME(UGCAchievementEventSend_PlayerReviewer) ACMD_ACCESSLEVEL(7); // access level 7 because its a debug only command
void gslUGC_AchievementEventSend_PlayerReviewer(Entity* pEntity)
{
	UGCAchievementEvent *event = StructCreate(parse_UGCAchievementEvent);
	event->uUGCAuthorID = entGetAccountID(pEntity);
	event->uUGCProjectID = 0;
	event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
	event->ugcAchievementServerEvent->ugcPlayerReviewerEvent = StructCreate(parse_UGCPlayerReviewerEvent);
	event->ugcAchievementServerEvent->ugcPlayerReviewerEvent->bPlayerIsReviewer = true;
	RemoteCommand_Intershard_ugcAchievementEvent_Send(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
	StructDestroy(parse_UGCAchievementEvent, event);
}

// TODO: THIS IS NOT HOW TO DO THIS. THIS IS JUST PLACEHOLDER
/*AUTO_COMMAND_REMOTE;
void gslUGC_GrantAchievementReward(ContainerID uUGCAccountID, const char *reward_table_name, const char *ugcAchievementName)
{
	if(uUGCAccountID)
	{
		Entity *pEntity = entFromAccountID(uUGCAccountID);
		if(pEntity)
		{
			RewardTable *reward = RefSystem_ReferentFromString(g_hRewardTableDict, reward_table_name);
			ItemChangeReason reason = {0};
			char *str = NULL;
			estrPrintf(&str, "UGCAchievement::Reward::%s", ugcAchievementName);
			inv_FillItemChangeReason(&reason, pEntity, str, NULL);
			reward_PowerExec(pEntity, reward, entity_GetCombatLevel(pEntity), 0, 0, &reason);
			estrDestroy(&str);
		}
	}
}*/
