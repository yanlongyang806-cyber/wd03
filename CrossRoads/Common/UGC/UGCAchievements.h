/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#include "ResourceManager.h"
#include "Message.h"
#include "Expression.h"
#include "WorldLibEnums.h"

typedef struct Entity Entity;
typedef struct UGCAccount UGCAccount;
typedef struct RewardTable RewardTable;

#define ALL_UGC_ACHIEVEMENTS_INDEX "AllUGCAchievementsIndex"

typedef struct UGCAchievementDef UGCAchievementDef;
typedef struct UGCAchievement UGCAchievement;

AUTO_STRUCT;
typedef struct UGCPlayerReviewerFilter
{
	bool bPlayerIsReviewer;	AST( NAME(PlayerIsReviewer) DEFAULT(1) )
} UGCPlayerReviewerFilter;
extern ParseTable parse_UGCPlayerReviewerFilter[];
#define TYPE_parse_UGCPlayerReviewerFilter UGCPlayerReviewerFilter

AUTO_STRUCT;
typedef struct UGCProjectPublishedFilter
{
	U32 uCustomMaps;	AST( NAME(CustomMaps) )
	U32 uDialogs;		AST( NAME(Dialogs) )
} UGCProjectPublishedFilter;
extern ParseTable parse_UGCProjectPublishedFilter[];
#define TYPE_parse_UGCProjectPublishedFilter UGCProjectPublishedFilter

AUTO_STRUCT;
typedef struct UGCSeriesPublishedFilter
{
	U32 uProjectCount;	AST( NAME(ProjectCount) )
} UGCSeriesPublishedFilter;
extern ParseTable parse_UGCSeriesPublishedFilter[];
#define TYPE_parse_UGCSeriesPublishedFilter UGCSeriesPublishedFilter

AUTO_STRUCT;
typedef struct UGCProjectPlayedFilter
{
	U32 uPlayDuration;	AST( NAME(PlayDuration) )
} UGCProjectPlayedFilter;
extern ParseTable parse_UGCProjectPlayedFilter[];
#define TYPE_parse_UGCProjectPlayedFilter UGCProjectPlayedFilter

AUTO_STRUCT;
typedef struct UGCPlayedProjectFilter
{
	U32 uPlayDuration;	AST( NAME(PlayDuration) )
} UGCPlayedProjectFilter;
extern ParseTable parse_UGCPlayedProjectFilter[];
#define TYPE_parse_UGCPlayedProjectFilter UGCPlayedProjectFilter

AUTO_STRUCT;
typedef struct UGCProjectReviewedFilter
{
	F32 fRating;						AST( NAME(Rating) )
	bool bCountRatingStars;				AST( NAME(CountRatingStars) )

	U32 iTotalReviews;					AST( NAME(TotalReviews) )
	U32 iTotalStars;					AST( NAME(TotalStars) )
	F32 fAverageRating;					AST( NAME(AverageRating) )
	F32 fAdjustedRatingUsingConfidence;	AST( NAME(AdjustedRating) )

	bool bBetaReviewing;				AST( NAME(BetaReviewing) )
} UGCProjectReviewedFilter;
extern ParseTable parse_UGCProjectReviewedFilter[];
#define TYPE_parse_UGCProjectReviewedFilter UGCProjectReviewedFilter

AUTO_STRUCT;
typedef struct UGCReviewedProjectFilter
{
	F32 fRating;						AST( NAME(Rating) )
	bool bCountRatingStars;				AST( NAME(CountRatingStars) )

	U32 iTotalReviews;					AST( NAME(TotalReviews) )
	U32 iTotalStars;					AST( NAME(TotalStars) )
	F32 fAverageRating;					AST( NAME(AverageRating) )
	F32 fAdjustedRatingUsingConfidence;	AST( NAME(AdjustedRating) )

	bool bBetaReviewing;				AST( NAME(BetaReviewing) )
} UGCReviewedProjectFilter;
extern ParseTable parse_UGCReviewedProjectFilter[];
#define TYPE_parse_UGCReviewedProjectFilter UGCReviewedProjectFilter

AUTO_STRUCT;
typedef struct UGCProjectTippedFilter
{
	U32 uTipAmount;				AST( NAME(TipAmount) )
	bool bCountTipAmount;		AST( NAME(CountTipAmount) )
} UGCProjectTippedFilter;
extern ParseTable parse_UGCProjectTippedFilter[];
#define TYPE_parse_UGCProjectTippedFilter UGCProjectTippedFilter

AUTO_STRUCT;
typedef struct UGCTippedProjectFilter
{
	U32 uTipAmount;				AST( NAME(TipAmount) )
	bool bCountTipAmount;		AST( NAME(CountTipAmount) )
} UGCTippedProjectFilter;
extern ParseTable parse_UGCTippedProjectFilter[];
#define TYPE_parse_UGCTippedProjectFilter UGCTippedProjectFilter

AUTO_STRUCT;
typedef struct UGCSeriesReviewedFilter
{
	F32 fRating;						AST( NAME(Rating) )
	bool bCountRatingStars;				AST( NAME(CountRatingStars) )

	U32 iTotalReviews;					AST( NAME(TotalReviews) )
	U32 iTotalStars;					AST( NAME(TotalStars) )
	F32 fAverageRating;					AST( NAME(AverageRating) )
	F32 fAdjustedRatingUsingConfidence;	AST( NAME(AdjustedRating) )
} UGCSeriesReviewedFilter;
extern ParseTable parse_UGCSeriesReviewedFilter[];
#define TYPE_parse_UGCSeriesReviewedFilter UGCSeriesReviewedFilter

AUTO_STRUCT;
typedef struct UGCReviewedSeriesFilter
{
	F32 fRating;						AST( NAME(Rating) )
	bool bCountRatingStars;				AST( NAME(CountRatingStars) )

	U32 iTotalReviews;					AST( NAME(TotalReviews) )
	U32 iTotalStars;					AST( NAME(TotalStars) )
	F32 fAverageRating;					AST( NAME(AverageRating) )
	F32 fAdjustedRatingUsingConfidence;	AST( NAME(AdjustedRating) )
} UGCReviewedSeriesFilter;
extern ParseTable parse_UGCReviewedSeriesFilter[];
#define TYPE_parse_UGCReviewedSeriesFilter UGCReviewedSeriesFilter

AUTO_STRUCT;
typedef struct UGCMapCreatedFilter
{
	UGCMapType type;	AST( NAME(Type) )
} UGCMapCreatedFilter;
extern ParseTable parse_UGCMapCreatedFilter[];
#define TYPE_parse_UGCMapCreatedFilter UGCMapCreatedFilter

AUTO_STRUCT;
typedef struct UGCProjectFeaturedFilter
{
	U32 uFeaturedProjectTotalCount;		AST( NAME(FeaturedProjectTotalCount) )
	U32 uFeaturedProjectCurrentCount;	AST( NAME(FeaturedProjectCurrentCount) )
} UGCProjectFeaturedFilter;
extern ParseTable parse_UGCProjectFeaturedFilter[];
#define TYPE_parse_UGCProjectFeaturedFilter UGCProjectFeaturedFilter

AUTO_ENUM;
typedef enum UGCAchievementGrantedFilterType {
	UGCAchievementGrantedFilterType_Exact,
	UGCAchievementGrantedFilterType_ImmediateDescendant,
	UGCAchievementGrantedFilterType_AnyDescendant
} UGCAchievementGrantedFilterType;
extern StaticDefineInt UGCAchievementGrantedFilterTypeEnum[];

AUTO_STRUCT;
typedef struct UGCAchievementGrantedFilter
{
	const char *pUGCAchievementName;			AST(NAME(UGCAchievement) POOL_STRING)
	UGCAchievementGrantedFilterType type;		AST(NAME(Type))
} UGCAchievementGrantedFilter;
extern ParseTable parse_UGCAchievementGrantedFilter[];
#define TYPE_parse_UGCAchievementGrantedFilter UGCAchievementGrantedFilter

AUTO_STRUCT;
typedef struct UGCAchievementClientFilter
{
	UGCMapCreatedFilter *ugcMapCreatedFilter;				AST( NAME(MapCreated) )
} UGCAchievementClientFilter;
extern ParseTable parse_UGCAchievementClientFilter[];
#define TYPE_parse_UGCAchievementClientFilter UGCAchievementClientFilter

AUTO_STRUCT;
typedef struct UGCAchievementServerFilter
{
	UGCPlayerReviewerFilter *ugcPlayerReviewerFilter;			AST( NAME(PlayerReviewer) )
	UGCProjectPublishedFilter *ugcProjectPublishedFilter;		AST( NAME(ProjectPublished) )
	UGCSeriesPublishedFilter *ugcSeriesPublishedFilter;			AST( NAME(SeriesPublished) )
	UGCProjectPlayedFilter *ugcProjectPlayedFilter;				AST( NAME(ProjectPlayed) )
	UGCPlayedProjectFilter *ugcPlayedProjectFilter;				AST( NAME(PlayedProject) )
	UGCProjectReviewedFilter *ugcProjectReviewedFilter;			AST( NAME(ProjectReviewed) )
	UGCReviewedProjectFilter *ugcReviewedProjectFilter;			AST( NAME(ReviewedProject) )
	UGCProjectTippedFilter *ugcProjectTippedFilter;				AST( NAME(ProjectTipped) )
	UGCTippedProjectFilter *ugcTippedProjectFilter;				AST( NAME(TippedProject) )
	UGCProjectFeaturedFilter *ugcProjectFeaturedFilter;			AST( NAME(ProjectFeatured) )
	UGCAchievementGrantedFilter *ugcAchievementGrantedFilter;	AST( NAME(AchievementGranted) )
	UGCSeriesReviewedFilter *ugcSeriesReviewedFilter;			AST( NAME(SeriesReviewed) )
	UGCReviewedSeriesFilter *ugcReviewedSeriesFilter;			AST( NAME(ReviewedSeries) )
} UGCAchievementServerFilter;
extern ParseTable parse_UGCAchievementServerFilter[];
#define TYPE_parse_UGCAchievementServerFilter UGCAchievementServerFilter

AUTO_STRUCT;
typedef struct UGCAchievementFilter
{
	UGCAchievementClientFilter ugcAchievementClientFilter;	AST( EMBEDDED_FLAT )
	UGCAchievementServerFilter ugcAchievementServerFilter;	AST( EMBEDDED_FLAT )
} UGCAchievementFilter;
extern ParseTable parse_UGCAchievementFilter[];
#define TYPE_parse_UGCAchievementFilter UGCAchievementFilter

AUTO_STRUCT;
typedef struct UGCPlayerReviewerEvent
{
	bool bPlayerIsReviewer;	AST( NAME(PlayerIsReviewer) DEFAULT(1) )
} UGCPlayerReviewerEvent;
extern ParseTable parse_UGCPlayerReviewerEvent[];
#define TYPE_parse_UGCPlayerReviewerEvent UGCPlayerReviewerEvent

AUTO_STRUCT;
typedef struct UGCProjectPublishedEvent
{
	U32 uCustomMaps;	AST( NAME(CustomMaps) )
	U32 uDialogs;		AST( NAME(Dialogs) )
} UGCProjectPublishedEvent;
extern ParseTable parse_UGCProjectPublishedEvent[];
#define TYPE_parse_UGCProjectPublishedEvent UGCProjectPublishedEvent

AUTO_STRUCT;
typedef struct UGCSeriesPublishedEvent
{
	U32 uProjectCount;	AST( NAME(ProjectCount) )
} UGCSeriesPublishedEvent;
extern ParseTable parse_UGCSeriesPublishedEvent[];
#define TYPE_parse_UGCSeriesPublishedEvent UGCSeriesPublishedEvent

AUTO_STRUCT;
typedef struct UGCProjectPlayedEvent
{
	U32 uPlayDuration;	AST( NAME(PlayDuration) )
} UGCProjectPlayedEvent;
extern ParseTable parse_UGCProjectPlayedEvent[];
#define TYPE_parse_UGCProjectPlayedEvent UGCProjectPlayedEvent

AUTO_STRUCT;
typedef struct UGCPlayedProjectEvent
{
	U32 uPlayDuration;	AST( NAME(PlayDuration) )
} UGCPlayedProjectEvent;
extern ParseTable parse_UGCPlayedProjectEvent[];
#define TYPE_parse_UGCPlayedProjectEvent UGCPlayedProjectEvent

AUTO_STRUCT;
typedef struct UGCProjectReviewedEvent
{
	F32 fRating;						AST( NAME(Rating) )
	F32 fHighestRating;					AST( NAME(HighestRating) )

	U32 iTotalReviews;					AST( NAME(TotalReviews) )
	U32 iTotalStars;					AST( NAME(TotalStars) )
	F32 fAverageRating;					AST( NAME(AverageRating) )
	F32 fAdjustedRatingUsingConfidence;	AST( NAME(AdjustedRating) )

	bool bBetaReviewing;				AST( NAME(BetaReviewing) )
} UGCProjectReviewedEvent;
extern ParseTable parse_UGCProjectReviewedEvent[];
#define TYPE_parse_UGCProjectReviewedEvent UGCProjectReviewedEvent

AUTO_STRUCT;
typedef struct UGCReviewedProjectEvent
{
	F32 fRating;						AST( NAME(Rating) )
	F32 fHighestRating;					AST( NAME(HighestRating) )

	U32 iTotalReviews;					AST( NAME(TotalReviews) )
	U32 iTotalStars;					AST( NAME(TotalStars) )
	F32 fAverageRating;					AST( NAME(AverageRating) )
	F32 fAdjustedRatingUsingConfidence;	AST( NAME(AdjustedRating) )

	bool bBetaReviewing;				AST( NAME(BetaReviewing) )
} UGCReviewedProjectEvent;
extern ParseTable parse_UGCReviewedProjectEvent[];
#define TYPE_parse_UGCReviewedProjectEvent UGCReviewedProjectEvent

AUTO_STRUCT;
typedef struct UGCProjectTippedEvent
{
	U32 uTipAmount;	AST( NAME(TipAmount) )
} UGCProjectTippedEvent;
extern ParseTable parse_UGCProjectTippedEvent[];
#define TYPE_parse_UGCProjectTippedEvent UGCProjectTippedEvent

AUTO_STRUCT;
typedef struct UGCTippedProjectEvent
{
	U32 uTipAmount;	AST( NAME(TipAmount) )
} UGCTippedProjectEvent;
extern ParseTable parse_UGCTippedProjectEvent[];
#define TYPE_parse_UGCTippedProjectEvent UGCTippedProjectEvent

AUTO_STRUCT;
typedef struct UGCProjectFeaturedEvent
{
	U32 uFeaturedProjectTotalCount;		AST( NAME(FeaturedProjectTotalCount) )
	U32 uFeaturedProjectCurrentCount;	AST( NAME(FeaturedProjectCurrentCount) )
} UGCProjectFeaturedEvent;
extern ParseTable parse_UGCProjectFeaturedEvent[];
#define TYPE_parse_UGCProjectFeaturedEvent UGCProjectFeaturedEvent

AUTO_STRUCT;
typedef struct UGCAchievementGrantedEvent
{
	const char *pUGCAchievementName;	AST(NAME(UGCAchievement) POOL_STRING)
} UGCAchievementGrantedEvent;
extern ParseTable parse_UGCAchievementGrantedEvent[];
#define TYPE_parse_UGCAchievementGrantedEvent UGCAchievementGrantedEvent

AUTO_STRUCT;
typedef struct UGCSeriesReviewedEvent
{
	F32 fRating;						AST( NAME(Rating) )
	F32 fHighestRating;					AST( NAME(HighestRating) )

	U32 iTotalReviews;					AST( NAME(TotalReviews) )
	U32 iTotalStars;					AST( NAME(TotalStars) )
	F32 fAverageRating;					AST( NAME(AverageRating) )
	F32 fAdjustedRatingUsingConfidence;	AST( NAME(AdjustedRating) )
} UGCSeriesReviewedEvent;
extern ParseTable parse_UGCSeriesReviewedEvent[];
#define TYPE_parse_UGCSeriesReviewedEvent UGCSeriesReviewedEvent

AUTO_STRUCT;
typedef struct UGCReviewedSeriesEvent
{
	F32 fRating;						AST( NAME(Rating) )
	F32 fHighestRating;					AST( NAME(HighestRating) )

	U32 iTotalReviews;					AST( NAME(TotalReviews) )
	U32 iTotalStars;					AST( NAME(TotalStars) )
	F32 fAverageRating;					AST( NAME(AverageRating) )
	F32 fAdjustedRatingUsingConfidence;	AST( NAME(AdjustedRating) )
} UGCReviewedSeriesEvent;
extern ParseTable parse_UGCReviewedSeriesEvent[];
#define TYPE_parse_UGCReviewedSeriesEvent UGCReviewedSeriesEvent

AUTO_STRUCT;
typedef struct UGCMapCreatedEvent
{
	UGCMapType type;	AST( NAME(Type) )
} UGCMapCreatedEvent;
extern ParseTable parse_UGCMapCreatedEvent[];
#define TYPE_parse_UGCMapCreatedEvent UGCMapCreatedEvent

AUTO_STRUCT;
typedef struct UGCAchievementClientEvent
{
	UGCMapCreatedEvent *ugcMapCreatedEvent;				AST( NAME(MapCreated) )
} UGCAchievementClientEvent;
extern ParseTable parse_UGCAchievementClientEvent[];
#define TYPE_parse_UGCAchievementClientEvent UGCAchievementClientEvent

AUTO_STRUCT;
typedef struct UGCAchievementServerEvent
{
	UGCPlayerReviewerEvent *ugcPlayerReviewerEvent;			AST( NAME(PlayerReviewerd) )
	UGCProjectPublishedEvent *ugcProjectPublishedEvent;		AST( NAME(ProjectPublished) )
	UGCSeriesPublishedEvent *ugcSeriesPublishedEvent;		AST( NAME(SeriesPublished) )
	UGCProjectPlayedEvent *ugcProjectPlayedEvent;			AST( NAME(ProjectPlayed) )
	UGCPlayedProjectEvent *ugcPlayedProjectEvent;			AST( NAME(PlayedProject) )
	UGCProjectReviewedEvent *ugcProjectReviewedEvent;		AST( NAME(ProjectReviewed) )
	UGCReviewedProjectEvent *ugcReviewedProjectEvent;		AST( NAME(ReviewedProject) )
	UGCProjectTippedEvent *ugcProjectTippedEvent;			AST( NAME(ProjectTipped) )
	UGCTippedProjectEvent *ugcTippedProjectEvent;			AST( NAME(TippedProject) )
	UGCProjectFeaturedEvent *ugcProjectFeaturedEvent;		AST( NAME(ProjectFeatured) )
	UGCAchievementGrantedEvent *ugcAchievementGrantedEvent;	AST( NAME(AchievementGranted) )
	UGCSeriesReviewedEvent *ugcSeriesReviewedEvent;			AST( NAME(SeriesReviewed) )
	UGCReviewedSeriesEvent *ugcReviewedSeriesEvent;			AST( NAME(ReviewedSeries) )
} UGCAchievementServerEvent;
extern ParseTable parse_UGCAchievementServerEvent[];
#define TYPE_parse_UGCAchievementServerEvent UGCAchievementServerEvent

AUTO_STRUCT;
typedef struct UGCAchievementEvent
{
	U32 uUGCAuthorID;										AST( NAME(UGCAuthorID) )
	U32 uUGCProjectID;										AST( NAME(UGCProjectID) )
	U32 uUGCSeriesID;										AST( NAME(UGCSeriesID) )

	UGCAchievementClientEvent *ugcAchievementClientEvent;	AST( NAME(Client) )
	UGCAchievementServerEvent *ugcAchievementServerEvent;	AST( NAME(Server) )
} UGCAchievementEvent;
extern ParseTable parse_UGCAchievementEvent[];
#define TYPE_parse_UGCAchievementEvent UGCAchievementEvent

// Static Definition of an UGCAchievement
AUTO_STRUCT;
typedef struct UGCAchievementDef
{
	// Achievement name that uniquely identifies the Achievement. Required.
	const char* name;							AST( STRUCTPARAM KEY POOL_STRING )

	// Scope of the AchievementDef. Root AchievementDef only.
	const char* scope;							AST( POOL_STRING )

	// Filename that this Achievement came from.
	const char* filename;						AST( CURRENTFILE )

	// User-facing name of Achievement
	DisplayMessage nameMsg;						AST( NAME("NameDisplayMsg") STRUCT(parse_DisplayMessage) )

	// User-facing description of Achievement
	DisplayMessage descriptionMsg;				AST( NAME("DescriptionDisplayMsg") STRUCT(parse_DisplayMessage) )

	// User-facing notification of Achievement granted
	DisplayMessage grantedNotificationMsg;		AST( NAME("GrantedNotificationDisplayMsg") STRUCT(parse_DisplayMessage) )

	// List of achievements that are grouped within this achievement. If no children exist, this achievement is considered leafy and will count events towards the target.
	UGCAchievementDef **subAchievements;		AST( NAME("SubAchievement") NO_INDEX )

	UGCAchievementFilter ugcAchievementFilter;	AST( EMBEDDED_FLAT )

	bool bHidden;								AST( NAME("Hidden") DEFAULT(0) )

	// The target number of Achievement Events that must match this Achievement before considered Achieved
	U32 uTarget;								AST( NAME("Target") DEFAULT(1) )

	// Only start counting events any of the child achievements once their previous sibling has been granted
	bool bOrderedCounting;						AST( NAME("OrderedCounting") DEFAULT(0) )

	// When ConsecutiveHours is non-zero, the player must repeat the achievement activity once per every number of hours indicated.
	// The Count will only be increased for the first time the player completes the activity in the block time period. If
	// the player takes longer than the next block time period, the Count is reset to zero before incrementing.
	// Example 24 with a Target of 5 would result in the player needing to complete the activity once every day for 5 consecutive days.
	U32 uConsecutiveHours;						AST( NAME("ConsecutiveHours") )

	// If ConsecutiveHours is non-zero and this is non-zero, the Achievement Count is reset to the largest multiple of this value that is still
	// less than the Count. This provides a way of counting the number of times a particular consecutive count has been reached.
	// For example, "Review a Quest once per day for 14 consecutive days; and do that 8 times."
	U32 uConsecutiveMissCountResetMultiple;		AST( NAME("ConsecutiveMissCountResetMultiple"))

	// Whether the achievement is repeatable after the reward is claimed
	bool bRepeatable;							AST( NAME("Repeatable") )

	// Cooldown time for how often the UGC author can acquire this Achievement. This checks when you last claimed the Achievement reward.
	U32 uRepeatCooldownHours;					AST( NAME("RepeatCooldownHours"))

	// If true then Achievement claim times are set to the nearest block based on cooldown time.
	// Example 24 in fRepeatCooldownHours would result in Achievement cooldown times being started at 12:00am and ending 24 hours later.
	// Example 8 in fRepeatCooldownHours would result in Achievement cooldown times being started at 12:00am, 08:00am, and 4:00pm and ending 8 hours later.
	// When it is past the last block then its in a new cooldown block (count starts over again).
	bool bRepeatCooldownBlockTime;				AST( NAME("RepeatCooldownBlockTime"))

	// Post-processed on text read, fully-scoped reference name for the Achievement
	const char* pchRefString;					AST( NO_TEXT_SAVE POOL_STRING )

	// Post-processed after binning and bin load, used to traverse from child to root
	UGCAchievementDef* pParentDef;				NO_AST
} UGCAchievementDef;
extern ParseTable parse_UGCAchievementDef[];
#define TYPE_parse_UGCAchievementDef UGCAchievementDef

// A running Achievement that can be attached to anything. In our primary use-case, this can be Account, UGCAccount, or UGCProject.
AUTO_STRUCT AST_CONTAINER AST_IGNORE(uClaimTime);
typedef struct UGCAchievement
{
	// Fully-scoped name of Achievement
	CONST_STRING_POOLED ugcAchievementName;		AST( PERSIST SUBSCRIBE KEY NAME("UGCAchievementName") POOL_STRING )

	// Achievement count towards the AchievementDef target
	const U32 uCount;							AST( PERSIST SUBSCRIBE )

	// Longest consecutive count achieved for AchievementsDefs with a non-zero ConsecutiveHours setting.
	const U32 uMaximumConsecutiveCount;			AST( PERSIST SUBSCRIBE )

	// Time the achievement had its count last incremented. Zero if never.
	const U32 uLastCountTime;					AST( PERSIST SUBSCRIBE FORMATSTRING(HTML_SECS_AGO_SHORT = 1) )

	// Time the achievement was granted. Zero if never. This is the time the author completes the achievement, not starts it.
	const U32 uGrantTime;						AST( PERSIST SUBSCRIBE FORMATSTRING(HTML_SECS_AGO_SHORT = 1) )
} UGCAchievement;
extern ParseTable parse_UGCAchievement[];
#define TYPE_parse_UGCAchievement UGCAchievement

// Given a ref string in the form of A::B::C, returns the UGCAchievementDef representing C.
UGCAchievementDef* ugcAchievement_DefFromRefString(const char *pcRefString);

// Used by external systems to match AchievementEvents with an UGCAchievementFilter. A return value of zero indicates no match. Otherwise, the return value indicates the amount to increment the count
// towards the target.
U32 ugcAchievement_EventFilter(UGCAchievementFilter *pUGCAchievementFilter, UGCAchievementEvent *pUGCAchievementEvent);

U32 ugcAchievement_GetCooldownBlockGrantTime(U32 grantTime, UGCAchievementDef *pUGCAchievementDef);

U32 ugcAchievement_GetConsecutiveBlockLastCountTime(U32 lastCountTime, UGCAchievementDef *pUGCAchievementDef);

const char *ugcAchievement_Scope(UGCAchievementDef *pUGCAchievementDef);

bool ugcAchievement_IsHidden(UGCAchievementDef *pUGCAchievementDef);

UGCAchievementDef *ugcAchievement_GetPreviousSibling(UGCAchievementDef *pUGCAchievementDef);
