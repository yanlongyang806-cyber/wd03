//// UGC backend project handling
////
//// Containers and other structures that relate to UGC project tracking,
//// rating, searching, etc. Shared among the various servers that have
//// to deal with UGC projects.

#pragma once

#include "stdtypes.h"
#include "globaltypes.h"
#include "ReferenceSystem.h"
#include "UGCProjectUtils.h"
#include "MapDescription.h"
#include "TransactionOutcomes.h"

typedef enum ZoneMapType ZoneMapType;
typedef struct DynamicPatchInfo DynamicPatchInfo;
typedef struct Entity Entity;
typedef struct GameProgressionNodeDef GameProgressionNodeDef;
typedef struct GameProgressionNodeRef GameProgressionNodeRef;
typedef struct NOCONST(UGCProjectReviews) NOCONST(UGCProjectReviews);
typedef struct NOCONST(UGCProject) NOCONST(UGCProject);
typedef struct NOCONST(UGCProjectDurationStats) NOCONST(UGCProjectDurationStats);
typedef struct NOCONST(UGCProjectSeries) NOCONST(UGCProjectSeries);
typedef struct NOCONST(UGCProjectVersion) NOCONST(UGCProjectVersion);
typedef struct NOCONST(UGCSeriesSearchCache) NOCONST(UGCSeriesSearchCache);
typedef struct NOCONST(UGCTimeStamp) NOCONST(UGCTimeStamp);
typedef struct QueueDef QueueDef;
typedef struct RemoteCommandGroup RemoteCommandGroup;
typedef struct UGCMapLocation UGCMapLocation;
typedef struct UGCPlayer UGCPlayer;
typedef struct UGCProjectInfo UGCProjectInfo;
typedef struct UGCProjectSeriesVersion UGCProjectSeriesVersion;
typedef struct UGCProjectStatusQueryInfo UGCProjectStatusQueryInfo;
typedef struct UGCAuthorSubscription UGCAuthorSubscription;
typedef struct UGCProjectSubscription UGCProjectSubscription;
typedef struct UGCSubscription UGCSubscription;
typedef struct WorldUGCRestrictionProperties WorldUGCRestrictionProperties;
typedef struct WorldUGCRestrictionProperties WorldUGCRestrictionProperties;
typedef struct UGCAchievement UGCAchievement;

//ugc project names must be minimum this many characters
#define UGCPROJ_MIN_NAME_LENGTH 4
#define UGCPROJ_MAX_NAME_LENGTH 32

//searches for UGC projects by string must be minimum this many characters
#define UGCPROJ_MIN_ID_SEARCH_STRING_LEN 2
#define UGCPROJ_MAX_ID_SEARCH_STRING_LEN 32

//searches for UGC projects by string must be minimum this many characters
#define UGCPROJ_MIN_NAME_SEARCH_STRING_LEN 2
#define UGCPROJ_MAX_NAME_SEARCH_STRING_LEN 32

//searches for UGC authors by string must be minimum this many characters
#define UGCPROJ_MIN_AUTHOR_NAME_SEARCH_STRING_LEN 2
#define UGCPROJ_MAX_AUTHOR_NAME_SEARCH_STRING_LEN 32

//internally, simple searches are converted into multiple simple search string searches
//when being used on whatshot or whatsnew lists
#define UGCPROJ_MIN_SIMPLE_SEARCH_STRING_LEN 3
#define UGCPROJ_MAX_SIMPLE_SEARCH_STRING_LEN 32

//searches for UGC project by description must be minimum this many characters
#define UGCPROJ_MIN_DESCRIPTION_SEARCH_STRING_LEN 3

#define MAX_UGC_COMPLETION_BUCKETS 7

// MJF June/5/2013 -- Do not ever change these values.  The UGC db
// will get corrupted if MAX_UGC_DURATION_BUCKETS is changed from 12
// *or* UGC_DURATION_BUCKET_SIZE_IN_MINUTES is changed from 15.
#define MAX_UGC_DURATION_BUCKETS 12				// 3 hours
#define UGC_DURATION_BUCKET_SIZE_IN_MINUTES 15

//old versions are pruned when obsolete, but always keep around this many
//recent versions. Even though these is 0, we are still keeping Withdrawn project versions
// for 30 days and Unplayable project versions for 7 days. Finally, this number only applies
// to Publish_Failed, Unplayable, and Withdrawn project versions. Any other version is never deleted.
#define UGC_NUM_RECENT_VERSIONS_TO_ALWAYS_KEEP 1

//the number of buckets to create for rating histograms
#define UGCPROJ_NUM_RATING_BUCKETS 5

// The maximum number of subscriptions any one type can have.
#define UGC_SUBSCRIPTION_MAX 100

// Timestamp that identifies the time and build when a map was created,
// saved, or published. Can be used to invalidate and republish projects
// in the event a bug is found or if world bin CRCs change.

// Version numbers stored in TimeStamp. Can be used to
// force a project rebuild of a particular version.
#define UGC_SYSTEM_VERSION_MAJOR 1
#define UGC_SYSTEM_VERSION_MINOR 1

// How long does MapManager wait before it will start alerting when not receiving playable namespace heartbeat from UGCDataManager.
#define UGC_MAP_MANAGER_NOT_RECEIVING_PLAYABLE_NAMESPACES_ALERT_DELAY	15*60 // seconds

// The period between each MapManager check for if it should alert that it has not received playable namespace heartbeat from UGCDataManager.
#define UGC_MAP_MANAGER_NOT_RECEIVING_PLAYABLE_NAMESPACES_ALERT_PERIOD	15*60 // seconds

// How long does UGCDataManager wait before it will start alerting when it cannot send playable namespace heartbeat to all MapManagers.
// This is also how long UGCDataManager waits before it sends the first heartbeat. Therefore, this number should be smaller than the
// UGC_MAP_MANAGER_NOT_RECEIVING_PLAYABLE_NAMESPACES_ALERT_DELAY above.
#define UGC_DATA_MANAGER_NOT_SENDING_PLAYABLE_NAMESPACES_ALERT_DELAY	5*60 // seconds

// The period between each UGCDataManager check for if it should alert that it can not send playable namespace heartbeat to all MapManagers.
// This is also the period for UGCDataManager sending its heartbeat. Therefore, this number should be smaller than the
// UGC_MAP_MANAGER_NOT_RECEIVING_PLAYABLE_NAMESPACES_ALERT_PERIOD above.
#define UGC_DATA_MANAGER_NOT_SENDING_PLAYABLE_NAMESPACES_ALERT_PERIOD	5*60 // seconds

// Indicates if UGC has been turned on via shard launcher config for this shard. It will be false from MCP unless set via cmdline.
extern bool gbUGCGenerallyEnabled;

typedef struct UGCProjectDataHeader
{
	char *ns_name;
	char *project_prefix;
	UGCProjectInfo *project;
	// If you change the fields above (their order, number, or types) you must also edit UGCProjectData in each game-specific UGCProjectData struct)
} UGCProjectDataHeader;

AUTO_STRUCT;
typedef struct UGCContentInfo
{
	// The name of the UGC project this is associated with (if any)
	ContainerID iUGCProjectID;											AST(NAME(UGCProjectID))

	// The name of the UGC project this is associated with (if any)
	ContainerID iUGCProjectSeriesID;									AST(NAME(UGCProjectSeriesID))

} UGCContentInfo;
extern ParseTable parse_UGCContentInfo[];
#define TYPE_parse_UGCContentInfo UGCContentInfo

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCSubscriber
{
	const ContainerID uPlayer;				AST(KEY PERSIST SUBSCRIBE)
	const ContainerID uAccount;				AST(PERSIST SUBSCRIBE)
} UGCSubscriber;

/// Structure reperesnting everyone subscribed to something.
///
/// (i.e., a backindex of UGCSubscription)
AUTO_STRUCT AST_CONTAINER;
typedef struct UGCSubscriberList
{
	CONST_EARRAY_OF(UGCSubscriber) eaPlayers; AST(PERSIST)
} UGCSubscriberList;

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCAchievementInfo
{
	// Contains all Achievements currently in progress or achieved
	CONST_EARRAY_OF(UGCAchievement) eaAchievements;	AST(PERSIST SUBSCRIBE FORCE_CONTAINER LATEBIND)
} UGCAchievementInfo;

// Achievement container structure (per-project)
AUTO_STRUCT AST_CONTAINER;
typedef struct UGCProjectAchievementInfo
{
	const ContainerID projectID;						AST(KEY PERSIST SUBSCRIBE)

	CONST_STRING_MODIFIABLE pcName;						AST(PERSIST SUBSCRIBE)

	UGCAchievementInfo ugcAchievementInfo;				AST(PERSIST SUBSCRIBE)
} UGCProjectAchievementInfo;

// Achievement container structure (per-series)
AUTO_STRUCT AST_CONTAINER;
typedef struct UGCSeriesAchievementInfo
{
	const ContainerID seriesID;							AST(KEY PERSIST SUBSCRIBE)

	CONST_STRING_MODIFIABLE pcName;						AST(PERSIST SUBSCRIBE)

	UGCAchievementInfo ugcAchievementInfo;				AST(PERSIST SUBSCRIBE)
} UGCSeriesAchievementInfo;

/// Persist container representing all UGC data tied to a single
/// author.
AUTO_STRUCT AST_CONTAINER;
typedef struct UGCAuthor
{
	UGCSubscriberList subscribers;										AST(PERSIST)

	// Contains all per-project Achievements currently in progress or achieved
	CONST_EARRAY_OF(UGCProjectAchievementInfo) eaProjectAchievements;	AST(PERSIST SUBSCRIBE)

	// Contains all per-series Achievements currently in progress or achieved
	CONST_EARRAY_OF(UGCSeriesAchievementInfo) eaSeriesAchievements;		AST(PERSIST SUBSCRIBE)

	// Contains all account Achievements currently in progress or achieved
	UGCAchievementInfo ugcAccountAchievements;							AST(PERSIST SUBSCRIBE)

	// The time that achievement notifications were last sent. Zero if never.
	const U32 uLastAchievementNotifyTime;								AST(PERSIST SUBSCRIBE FORMATSTRING(HTML_SECS_AGO_SHORT = 1))
} UGCAuthor;

/// Persisted container representing UGC-related data tied to a player.
AUTO_STRUCT AST_CONTAINER;
typedef struct UGCPlayer
{
	const ContainerID playerID;				AST(KEY PERSIST SUBSCRIBE)
	CONST_OPTIONAL_STRUCT(UGCSubscription) pSubscription;	AST(PERSIST SUBSCRIBE ALWAYS_ALLOC)
} UGCPlayer;

/// Persisted container representing UGC-related data for an account.
///
/// Currently holds all subscription data.
AUTO_STRUCT AST_CONTAINER;
typedef struct UGCAccount
{
	const ContainerID accountID;							AST(KEY PERSIST SUBSCRIBE)

	// Player data
	CONST_EARRAY_OF(UGCPlayer) eaPlayers;					AST(PERSIST SUBSCRIBE)

	// Author data -- should not be subscribed to because only the
	// UGCDataManager reads from this.
	UGCAuthor author;										AST(PERSIST SUBSCRIBE)
} UGCAccount;
typedef struct UGCAccount_AutoGen_NoConst UGCAccount_AutoGen_NoConst;

/// Structure representing all UGC data that a player gets subscribed
/// to.
AUTO_STRUCT AST_CONTAINER;
typedef struct UGCSubscription
{
	CONST_EARRAY_OF(UGCAuthorSubscription) eaAuthors;		AST(PERSIST SUBSCRIBE)
} UGCSubscription;
extern ParseTable parse_UGCSubscription[];
#define TYPE_parse_UGCSubscription UGCSubscription

/// Structure container data stored for authors subscribed to.
///
/// This structure can get pretty big, but it's okay because we can
/// control how big the list of these can get.
AUTO_STRUCT AST_CONTAINER;
typedef struct UGCAuthorSubscription
{
	const ContainerID authorID;				AST(KEY PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(UGCProjectSubscription) eaCompletedProjects; AST(PERSIST SUBSCRIBE)
} UGCAuthorSubscription;
extern ParseTable parse_UGCAuthorSubscription[];
#define TYPE_parse_UGCAuthorSubscription UGCAuthorSubscription

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCProjectSubscription
{
	const ContainerID projectID;			AST(KEY PERSIST SUBSCRIBE)
	const U32 completedTime;				AST(PERSIST SUBSCRIBE FORMATSTRING(HTML_SECS = 1))
} UGCProjectSubscription;
extern ParseTable parse_UGCProjectSubscription[];
#define TYPE_parse_UGCProjectSubscription UGCProjectSubscription

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCTimeStamp
{
	const U32 iTimestamp; AST(PERSIST SUBSCRIBE)
	const int iMajorVer; AST(PERSIST SUBSCRIBE)
	const int iMinorVer; AST(PERSIST SUBSCRIBE)
	CONST_STRING_POOLED pBuildVer; AST(PERSIST SUBSCRIBE POOL_STRING)
	const U32 iWorldCellOverrideCRC; AST(PERSIST SUBSCRIBE)
	const U32 iBeaconProcessVersion; AST(PERSIST SUBSCRIBE)
} UGCTimeStamp;

AUTO_STRUCT;
typedef struct UGCTimeStampPlusShardName
{
	UGCTimeStamp timeStamp; AST(EMBEDDED_FLAT)
	char *pShardName;
} UGCTimeStampPlusShardName;

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCProjectCompletionStats
{
	CONST_INT_EARRAY eaiCompletedCountByDay;	    AST(PERSIST SUBSCRIBE)
	const U32 uCurrentDayTimestamp;					AST(PERSIST SUBSCRIBE)

	const U32 uRemainingCompletedCount;				AST(PERSIST SUBSCRIBE)

	// previous version data
	const U32 uPrevVersionsCompletedCount;			AST(PERSIST SUBSCRIBE)
} UGCProjectCompletionStats;

/// This structure holds data associated with being "featured".  This
/// can be put on random containers to make them available for
/// Featured Content.
AUTO_STRUCT AST_CONTAINER;
typedef struct UGCFeaturedData
{
	// Time this became featured
	const U32 iStartTimestamp;						AST(PERSIST SUBSCRIBE)

	// Time this stopped being featured -OR- zero, to indicate no end
	const U32 iEndTimestamp;						AST(PERSIST SUBSCRIBE)

	// Details about being featured (the contest, commentary, etc.)
	CONST_STRING_MODIFIABLE strDetails;				AST(PERSIST SUBSCRIBE)

	// If set this overrides the average playing time
	const float fAverageDurationInMinutes_Override;	AST(PERSIST SUBSCRIBE) 
} UGCFeaturedData;
extern ParseTable parse_UGCFeaturedData[];
#define TYPE_parse_UGCFeaturedData UGCFeaturedData

/// Debug data saved/loaded from disk.
AUTO_STRUCT;
typedef struct UGCFeaturedContentInfo
{
	UGCContentInfo sContentInfo;					AST(EMBEDDED_FLAT)
	UGCFeaturedData sFeaturedData;					AST(EMBEDDED_FLAT)
} UGCFeaturedContentInfo;
extern ParseTable parse_UGCFeaturedContentInfo[];
#define TYPE_parse_UGCFeaturedContentInfo UGCFeaturedContentInfo

AUTO_STRUCT;
typedef struct UGCFeaturedContentInfoList
{
	UGCFeaturedContentInfo** eaFeaturedContent;		AST(NAME(FeaturedContent))
} UGCFeaturedContentInfoList;
extern ParseTable parse_UGCFeaturedContentInfoList[];
#define TYPE_parse_UGCFeaturedContentInfoList UGCFeaturedContentInfoList

//moving some enums here so they're as global as possible
AUTO_ENUM;
typedef enum UGCProjectVersionState
{
	UGC_NEW, //newly created... has not been saved or published
	UGC_SAVED, //saved but not published
	UGC_PUBLISH_BEGUN, //the publish process has begun
	UGC_PUBLISH_FAILED, //attempted to publish, failed
	UGC_PUBLISHED, //publish completed

	UGC_WITHDRAWN, //used to be published, no longer is. User can probably set this, this happens to
		//non-most-recent versions when a version invalidation happens

	UGC_REPUBLISHING, //the version was invalidated, automatic re-publish is happening
	

	UGC_REPUBLISH_FAILED, // was in a republish, the republish failed.  Maintained to allow someone to debug what happened

	UGC_UNPLAYABLE, //this version has been specifically marked as unplayable by CSR/Netops/someone, or because
		//it was already withdrawn and then its version became obsolete. The only
		//thing this does is that the resource DB won't get permission to serve up resources in this namespace

	UGC_NEEDS_REPUBLISHING, //this version was PUBLISHED, but is now thought to be out of date,
		//and presumably needs republishing

	UGC_NEEDS_UNPLAYABLE, //this version was WITHDRAWN, or was not-played-for-a-while, and instead of
		//needing repuslishing, it presumably needs to be made unplayable

	UGC_NEEDS_FIRST_PUBLISH, //this version was published on another shard and was imported into this shard, therefore
		//it needs a first publish on this shard

	UGC_LAST, EIGNORE
} UGCProjectVersionState;
extern StaticDefineInt UGCProjectVersionStateEnum[];

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCProjectDurationBucket
{
	const U32 uCount;		AST(PERSIST SUBSCRIBE)
	const U32 uSum;			AST(PERSIST SUBSCRIBE)
	const U32 uAverage;		AST(PERSIST SUBSCRIBE)
} UGCProjectDurationBucket;

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCProjectDurationStats
{
	const float iAverageDurationInMinutes_IgnoreOutliers; AST(PERSIST SUBSCRIBE) //recalced every time buckets are modified
	CONST_EARRAY_OF(UGCProjectDurationBucket) eaDurationBuckets;	AST(PERSIST SUBSCRIBE)

	// previous version data
	CONST_EARRAY_OF(UGCProjectDurationBucket) eaPrevVersionsDurationBuckets;	AST(PERSIST SUBSCRIBE)
} UGCProjectDurationStats;

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCProjectMapDurationStats
{
	CONST_STRING_MODIFIABLE pName;					AST(PERSIST SUBSCRIBE)
	const UGCProjectDurationStats durationStats;	AST(PERSIST SUBSCRIBE)
} UGCProjectMapDurationStats;

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCProjectStats
{
	const UGCProjectCompletionStats completionStats;					AST(PERSIST SUBSCRIBE)
	const UGCProjectDurationStats durationStats;						AST(PERSIST SUBSCRIBE)
	const U32 uTotalDropCount;											AST(PERSIST SUBSCRIBE)

	// Duration stats by map is another method for tracking average playing time that ignores cryptic/static map usage, but can be very accurate (and harder to exploit).
	// When the OpenMission of a UGC map is completed, the total time for the OpenMission is sent for recording here, along with the map name itself.
	// When the author publishes a new version, this array has old maps removed, new maps added, existing maps unchanged.
	CONST_EARRAY_OF(UGCProjectMapDurationStats) eaDurationStatsByMap;	AST(PERSIST SUBSCRIBE)
	// This is the most recent sum of the average minutes ignoring outliers for each map.
	const float iAverageDurationInMinutes_UsingMaps;					AST(PERSIST SUBSCRIBE) // recalced every time someone completes an entire UGC mission (not just individual maps).
	// Needed for STO, since they decided to turn on gConf.bUGCAveragePlayingTimeUsesCustomMapPlayingTime.
	// This tells us if both the eaDurationStatsByMap and the most recent published version's ppMapNames has ever been filled in. That way, we can distinguish between no maps in
	// a project and a project that has had no one complete it since gConf.bUGCAveragePlayingTimeUsesCustomMapPlayingTime was turned on.
	const bool bMapsFilledIn;											AST(PERSIST SUBSCRIBE)
} UGCProjectStats;

// Container-ized version of WorldUGCFactionRestrictionProperties (wlGroupPropertyStructs.h)
AUTO_STRUCT AST_CONTAINER;
typedef struct UGCProjectVersionFactionRestrictionProperties
{
	CONST_STRING_MODIFIABLE pcFaction;					AST( PERSIST SUBSCRIBE NAME(Faction) STRUCTPARAM )
} UGCProjectVersionFactionRestrictionProperties;

// Container-ized version of WorldUGCRestrictionProperties (wlGroupPropertyStructs.h)
AUTO_STRUCT AST_CONTAINER;
typedef struct UGCProjectVersionRestrictionProperties
{
	const S32 iMinLevel;								AST( PERSIST SUBSCRIBE NAME(MinLevel) )
	const S32 iMaxLevel;								AST( PERSIST SUBSCRIBE NAME(MaxLevel) )
	CONST_EARRAY_OF(UGCProjectVersionFactionRestrictionProperties) eaFactions;	AST( PERSIST SUBSCRIBE NAME(Faction) )
} UGCProjectVersionRestrictionProperties;
extern ParseTable parse_UGCProjectVersionRestrictionProperties[];
#define TYPE_parse_UGCProjectVersionRestrictionProperties UGCProjectVersionRestrictionProperties

// Container-ized version of UGCMapLocation (UGCResource.h)
AUTO_STRUCT AST_CONTAINER;
typedef struct UGCProjectVersionMapLocation
{
	const float positionX;								AST( PERSIST SUBSCRIBE NAME(positionX) )
	const float positionY;								AST( PERSIST SUBSCRIBE NAME(positionY) )
	CONST_STRING_POOLED astrIcon;						AST( PERSIST SUBSCRIBE NAME(Icon) POOL_STRING )
} UGCProjectVersionMapLocation;

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCProjectVersionStateChangeHistory
{
	const UGCProjectVersionState eNewState; AST(PERSIST SUBSCRIBE)
	const U32 iTime;	AST(PERSIST SUBSCRIBE FORMATSTRING(HTML_SECS = 1))
	CONST_STRING_MODIFIABLE pComment; AST(ESTRING PERSIST SUBSCRIBE)
} UGCProjectVersionStateChangeHistory;

//These are kept on objectDB.  
AUTO_STRUCT AST_CONTAINER AST_IGNORE_STRUCT(ppMaps) AST_IGNORE_STRUCT(ugcStats);
typedef struct UGCProjectVersion
{
	// To find all the places you need to update to add a per
	// UGCProjectVersion field, search for this: {{UGCPROJECTVERSION}}

	const UGCProjectVersionState eState_USEACCESSOR;	AST(PERSIST SUBSCRIBE NAME("eState"))
	CONST_STRING_MODIFIABLE pUUID;						AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pNameSpace;					AST(ESTRING PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pPublishJobName;			AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pPublishResult;				AST(PERSIST SUBSCRIBE)
	const bool bPublishValidated;						AST(PERSIST SUBSCRIBE)
	const U32 iModTime;									AST(PERSIST SUBSCRIBE FORMATSTRING(HTML_SECS = 1)) //last time saved, or time published

	CONST_STRING_MODIFIABLE pName;						AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pDescription;				AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pNotes;						AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pImage;						AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pLocation;					AST(PERSIST SUBSCRIBE)
	CONST_OPTIONAL_STRUCT(UGCProjectVersionMapLocation) pMapLocation; AST(PERSIST SUBSCRIBE)
	const Language eLanguage;							AST(PERSIST SUBSCRIBE)
	
	// Mission grant info
	CONST_STRING_POOLED pCostumeOverride;				AST(PERSIST SUBSCRIBE)
	CONST_STRING_POOLED pPetOverride;					AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pBodyText;					AST(PERSIST SUBSCRIBE)

	// Initial destination info (used by lobby)
	CONST_STRING_POOLED strInitialMapName;				AST(PERSIST SUBSCRIBE)
	CONST_STRING_POOLED strInitialSpawnPoint;			AST(PERSIST SUBSCRIBE)

	CONST_OPTIONAL_STRUCT(UGCProjectVersionRestrictionProperties) pRestrictions; AST(PERSIST SUBSCRIBE)

	const U32 iLastTimeSentToOtherShard;				AST(PERSIST SUBSCRIBE) //if 0, never sent
	const bool bSendToOtherShardSucceeded;				AST(PERSIST SUBSCRIBE) 

	const UGCTimeStamp sLastPublishTimeStamp;			AST(PERSIST SUBSCRIBE)
	
	const U32 iMostRecentRepublishFlags;				AST(PERSIST SUBSCRIBE)

	//keep the most recent n state change comments/times/etc
	CONST_EARRAY_OF(UGCProjectVersionStateChangeHistory) ppRecentHistory; AST(PERSIST SUBSCRIBE)

	CONST_STRING_EARRAY ppMapNames;						AST(PERSIST SUBSCRIBE)

	AST_COMMAND("GetProjectToImport", "GetProjectToImport $FIELD(pNameSpace) $NOCONFIRM", "\q$SERVERTYPE\q = \qUGCDataManager\q")
} UGCProjectVersion;

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCDeletedProjectVersion
{
	const UGCProjectVersionState eState;	AST(PERSIST SUBSCRIBE NAME("eState"))
	CONST_STRING_MODIFIABLE pNameSpace;		AST(ESTRING PERSIST SUBSCRIBE)
	const U32 iDeletedTime;					AST(PERSIST SUBSCRIBE FORMATSTRING(HTML_SECS = 1))
} UGCDeletedProjectVersion;

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCProjectPublishPerformance
{
	const int iTimestamp;					AST(PERSIST FORMATSTRING(HTML_SECS = 1))
	CONST_STRING_MODIFIABLE strDetails;		AST(PERSIST)
} UGCProjectPublishPerformance;
extern ParseTable parse_UGCProjectPublishPerformance[];
#define TYPE_parse_UGCProjectPublishPerformance UGCProjectPublishPerformance

// UGCTag enum loaded directly from data\defs\config\UGCTags.def
// In order to get those enums defined, GameClient, GameServer, UGCDataManager, UGCSearchManager, UGCExport, and UGCImport,
// all need to run ugcLoadTags as an AUTO_STARTUP.
AUTO_ENUM AEN_PAD;
typedef enum UGCTag
{
	kUGCTag_NONE, EIGNORE
	kUGCTag_FIRST_DATA_DEFINED, EIGNORE
} UGCTag;
extern StaticDefineInt UGCTagEnum[];

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCTagData
{
	const UGCTag eUGCTag;	AST(KEY PERSIST SUBSCRIBE SUBTABLE(UGCTagEnum))
	const U32 iCount;		AST(PERSIST SUBSCRIBE)
} UGCTagData;
extern ParseTable parse_UGCTagData[];
#define TYPE_parse_UGCTagData UGCTagData

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCSingleReview
{
	const U32 iReviewerAccountID;					AST(PERSIST KEY)
	CONST_STRING_MODIFIABLE pReviewerAccountName;	AST(PERSIST)
	const int iTimestamp;							AST(PERSIST)
	const float fRating;							AST(PERSIST)//0 to 1
	const float fHighestRating;						AST(PERSIST)//0 to 1
	const bool bHidden;								AST(PERSIST)

	CONST_STRING_MODIFIABLE pComment;				AST(PERSIST)

	CONST_INT_EARRAY eaiUGCTags;					AST(PERSIST SUBTABLE(UGCTagEnum))
} UGCSingleReview;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(iNumPagesCached) AST_IGNORE(iNumPages);
typedef struct UGCProjectReviews
{
	const float fAverageRating; AST(PERSIST SUBSCRIBE)
	const float fAdjustedRatingUsingConfidence; AST(PERSIST SUBSCRIBE)
	const float fRatingSum; AST(PERSIST SUBSCRIBE)
	CONST_INT_EARRAY piNumRatings;	AST(PERSIST SUBSCRIBE ADDNAMES(iNumRatings)) 
		// Ratings are separated into buckets so that the UI can display histogram data. 
		// The max number of buckets is defined by UGCPROJ_NUM_RATING_BUCKETS.

	int iNumReviewPagesCached;
		// Cached value for a project details request that is the
		// total number of review pages for this project.  This
		// excludes hidden reviews, and reviews with no text.

	S32 iNumRatingsCached; 
		// Cached value for a project details request that is the total number of reviews for this project

	// Really, not properties of reviews, this should be moved into UGCProject.
	const U32 iTimeBecameReviewed;	AST(PERSIST SUBSCRIBE) 
		// At what time were there enough reviews for this to become playable

	CONST_EARRAY_OF(UGCTagData) eaTagData;	AST(PERSIST SUBSCRIBE)

	CONST_EARRAY_OF(UGCSingleReview) ppReviews; AST(PERSIST)
} UGCProjectReviews;
extern ParseTable parse_UGCProjectReviews[];
#define TYPE_parse_UGCProjectReviews UGCProjectReviews

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCProjectReport
{
	const U32 uAccountID; AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pchAccountName; AST(PERSIST SUBSCRIBE)
	const U32 uReportTime; AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pchReason; AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pchDetails; AST(PERSIST SUBSCRIBE)
} UGCProjectReport;

AUTO_STRUCT;
typedef struct UGCProjectReportList
{
	EARRAY_OF(UGCProjectReport) eaReports;
} UGCProjectReportList;

AUTO_STRUCT;
typedef struct UGCProjectReportQuery
{
	ContainerID iContainerID;
	char* pchProjName;
	U32 uOwnerAccountID;
	const char* pchOwnerAccountName;
	bool bBanned;
	bool bTemporarilyBanned;
	int iNaughtyValue;
	UGCProjectReport** eaReports;
} UGCProjectReportQuery;

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCProjectReporting
{
	const int iNaughtyValue; AST(PERSIST SUBSCRIBE)
	const U32 uNextNaughtyDecayTime; AST(PERSIST SUBSCRIBE)
	const U32 uTemporaryBanExpireTime; AST(PERSIST SUBSCRIBE)
	const int iTemporaryBanCount; AST(PERSIST SUBSCRIBE)
    const bool bDisableAutoBan; AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(UGCProjectReport) eaReports; AST(PERSIST SUBSCRIBE)
} UGCProjectReporting;

// Additional non-persist data that is returned in a UGCProject when a details request is made
AUTO_STRUCT;
typedef struct UGCExtraDetailData
{
	UGCSingleReview* pReviewForCurAccount;	// Can be NULL. The review (if any) that corresponds to the currently playing account.

	int iNumReviewPages;						AST(DEFAULT(-1))
} UGCExtraDetailData;

// If you change this structure, be sure to update these functions:
// gslCreateUGCProjectInfo() in gslSendToClient.c
// SaveUGCProject() in gslUgcTransactions.c
// PublishUGCProject() in gslUgcTransactions.c
AUTO_STRUCT AST_CONTAINER AST_IGNORE(pPrevPublicName) AST_IGNORE_STRUCT(ppProjectPermissions) AST_IGNORE_STRUCT(ugcTags);
typedef struct UGCProject
{
	const ContainerID id;						AST(KEY PERSIST SUBSCRIBE)
	const U32 iOwnerAccountID;					AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pOwnerAccountName;	AST(PERSIST SUBSCRIBE)
	const U32 iCreationTime;					AST(PERSIST SUBSCRIBE FORMATSTRING(HTML_SECS_AGO = 1))
	const ContainerID seriesID;					AST(PERSIST SUBSCRIBE)

	// this is the public name with SSSTree_InternalizeString called
	// on it. When we are doing various searches we always want to
	// have this name immediately handy so we don't have to constantly
	// recalculate it
	CONST_STRING_MODIFIABLE pPublicName_ForSearching;		AST(PERSIST SUBSCRIBE ESTRING) 
	CONST_STRING_MODIFIABLE pOwnerAccountName_ForSearching; AST(PERSIST SUBSCRIBE ESTRING) 
	const bool bFlaggedAsCryptic;							AST(PERSIST SUBSCRIBE) // means "@Cryptic" is automatically appended to owner account name whenever set

	// A cache of the published version's name.
	CONST_STRING_MODIFIABLE pPublishedVersionName; AST(PERSIST SUBSCRIBE NAME(pPublicName)) 

	// not the actual language of the project -- just the language the
	// player plays in.
	const int iOwnerLangID;						AST(PERSIST SUBSCRIBE)
	
	const U32 ugcLifetimeTips;					AST(PERSIST SUBSCRIBE)		// Total tips receieved over the lifetime of the project

	//////////////////////////////////////////////////////////////////////////////
	// The following fields are all for tracking UGC Export/Import projects
	//////////////////////////////////////////////////////////////////////////////

	CONST_STRING_MODIFIABLE strImportComment;	AST(PERSIST SUBSCRIBE NAME(ImportComment))

	const bool bNewlyImported;						AST(PERSIST SUBSCRIBE)
		// Whether or not this UGCProject was newly imported. Flag is cleared on each save

	CONST_STRING_MODIFIABLE strPreviousShard;	AST(PERSIST SUBSCRIBE NAME(PreviousShard))
		// Original Shard this project was imported from

	const ContainerID iIdOnPreviousShard;		AST(PERSIST SUBSCRIBE)
		// If a UGCProject gets transferred between shards, we want
		// to track its previous container ID so that we can find duplicates and so forth.

	//////////////////////////////////////////////////////////////////////////////
	// End UGC Export/Import fields
	//////////////////////////////////////////////////////////////////////////////

	const bool bBanned;							AST(PERSIST SUBSCRIBE ADDNAMES(CSRBanned, bCSRBanned))
		//Project is banned. There are two ways for a project to get banned:
		//1.) CSR has decided that there is something bad about this map. 
		//2.) The project has been temporarily banned enough times to warrant a permanent ban.

	const U32 iDeletionTime;					AST(PERSIST SUBSCRIBE FORMATSTRING(HTML_SECS = 1))
		//if set, this project has already been deleted, and we're just waiting for it to time out and get purged

	CONST_STRING_MODIFIABLE pIDString;			AST(PERSIST SUBSCRIBE)
		//take the container ID, run it through IDString_IntToString.

	const bool bTestOnly;						AST(PERSIST SUBSCRIBE)
		//this project was created internally for testing of searching/rating. Don't
		//try to republish it

	const U32 iMostRecentPlayedTime;			AST(PERSIST SUBSCRIBE FORMATSTRING(HTML_SECS_AGO = 1))
		//set by gameserver whenever the mission is added

	// If set, this project is featured.  This contains the data about when/why it was featured
	CONST_OPTIONAL_STRUCT(UGCFeaturedData) pFeatured;	AST(PERSIST SUBSCRIBE)

	// Pure-UI value.  If set, the author has read the Featuring EULA and said "yes" 
	const bool bAuthorAllowsFeatured;			AST(PERSIST SUBSCRIBE)

	// Set during the UGCFeaturedCopyProject process, to allow a single back door for publishing.
	const bool bUGCFeaturedCopyProjectInProgress; AST(PERSIST)

	// Set only if UGCFeaturedCopyProject was run.  The ID of the project this was based on.
	const ContainerID uUGCFeaturedOrigProjectID; AST(PERSIST SUBSCRIBE)

	U32 iNextTimeOKForPeriodicUpdate;			NO_AST
		//used internally by mapmanager to avoid repeated periodic updates over and over again

	UGCExtraDetailData* pExtraDetailData;
		// Extra data needed when project details are requested.
	
	CONST_EARRAY_OF(UGCProjectVersion) ppProjectVersions; AST(PERSIST SUBSCRIBE)

	CONST_EARRAY_OF(UGCDeletedProjectVersion) ppDeletedProjectVersions; AST(PERSIST SUBSCRIBE)

	const UGCProjectStats ugcStats;				AST(PERSIST SUBSCRIBE)

	const UGCProjectReporting ugcReporting;		AST(PERSIST SUBSCRIBE)

	CONST_EARRAY_OF(UGCProjectPublishPerformance) eaPublishPerformance; AST(PERSIST)

	const UGCProjectReviews ugcReviews;			AST(PERSIST SUBSCRIBE)

	AST_COMMAND("Apply Transaction","ServerMonTransactionOnEntity UGCProject $FIELD(id) $STRING(Transaction String)$CONFIRM(Really apply this transaction?)")
	AST_COMMAND("Check Container Location", "DebugCheckContainerLoc UGCProject $FIELD(id)", "\q$SERVERTYPE\q = \qObjectDB\q")
	AST_COMMAND("Withdraw Project", "WithdrawProject $FIELD(id) $STRING(Comment String) $CONFIRM(Really withdraw this project?)", "\q$SERVERTYPE\q = \qUGCDataManager\q")
} UGCProject;
extern ParseTable parse_UGCProject[];
#define TYPE_parse_UGCProject UGCProject

// This is the resource that is generated from the container by the
// server and sent down to the client. All editing is done on this
// structure and merged back into the container server-side during the
// Save and Publish operations.
AUTO_STRUCT AST_IGNORE(ID) AST_IGNORE(IDString) AST_IGNORE_STRUCT(Permission);
typedef struct UGCProjectInfo
{
	// To find all the places you need to update to add a per
	// UGCProjectVersion field, search for this: {{UGCPROJECTVERSION}}
	const char *pcName;							AST(KEY POOL_STRING)
	const char *pcFilename;						AST(CURRENTFILE NO_NETSEND)
	char *pOwnerAccountName;					AST(NAME(AccountName))
	U32 iCreationTime;							AST(NAME(CreationTime))
	bool bFromContainer;						AST(NAME(FromContainer)) // Should never be checked into Gimme
	
	char *pcPublicName;							AST(NAME(PublicName))
	char *strDescription;						AST(NAME(Description))
	char *strNotes;								AST(NAME(Notes))
	Language eLanguage;							AST(NAME(Language))
	UGCMapLocation* pMapLocation;				AST(NAME(MapLocation))
	char *strSearchLocation;					AST(NAME(SearchLocation))
	WorldUGCRestrictionProperties* pRestrictionProperties; AST(NAME(RestrictionProperties) LATEBIND)

	// Data never edited 
	U32 uLifetimeTipsReceived;
	float fAverageRating;
} UGCProjectInfo;
extern ParseTable parse_UGCProjectInfo[];
#define TYPE_parse_UGCProjectInfo UGCProjectInfo

// a location of a project on a map. Used to show on the NW world map.
AUTO_STRUCT;
typedef struct UGCMapLocation
{
	float positionX;						AST(NAME(positionX))
	float positionY;						AST(NAME(positionY))
	const char* astrIcon;					AST(NAME(Icon) POOL_STRING)
} UGCMapLocation;
extern ParseTable parse_UGCMapLocation[];
#define TYPE_parse_UGCMapLocation UGCMapLocation

/// A node in a UGCProjectSeries.  These can be arbitrarily nested, to
/// create UGC versions of GameProgressionNodes.
typedef struct UGCProjectSeriesNode UGCProjectSeriesNode;
AUTO_STRUCT AST_CONTAINER;
typedef struct UGCProjectSeriesNode
{
	// The following are set if this is a leaf node
	const ContainerID iProjectID;					AST(PERSIST SUBSCRIBE)

	// The following are set if this is an internal node
	const int iNodeID;								AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE strName;				AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE strDescription;			AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(UGCProjectSeriesNode) eaChildNodes; AST(PERSIST SUBSCRIBE)
} UGCProjectSeriesNode;
extern ParseTable parse_UGCProjectSeriesNode[];
#define TYPE_parse_UGCProjectSeriesNode UGCProjectSeriesNode

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCSeriesSearchCache
{
	CONST_STRING_MODIFIABLE strPublishedName;		AST(PERSIST SUBSCRIBE)
	CONST_CONTAINERID_EARRAY eaPublishedProjectIDs;	AST(PERSIST SUBSCRIBE)
} UGCSeriesSearchCache;
extern ParseTable parse_UGCSeriesSearchCache[];
#define TYPE_parse_UGCSeriesSearchCache UGCSeriesSearchCache

/// UGCProjectSeries stores an ordering of UGCProjects (possibly with
/// groups), so players can group projects into an overarching story.
AUTO_STRUCT AST_CONTAINER AST_IGNORE_STRUCT(ugcSubscribers);
typedef struct UGCProjectSeries
{
	const ContainerID id;							AST(KEY PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE strIDString;			AST(PERSIST SUBSCRIBE)

	// For internal tracking of the container creation time. Old UGCProjectSeries may have 0 for their iCreationTime.
	const U32 iCreationTime;						AST(PERSIST SUBSCRIBE FORMATSTRING(HTML_SECS_AGO = 1))
	
	const U32 iOwnerAccountID;						AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE strOwnerAccountName;	AST(PERSIST SUBSCRIBE)
	const bool bFlaggedAsCryptic;					AST(PERSIST SUBSCRIBE) // means "@Cryptic" is automatically appended to owner account name whenever set

	// The last time this project was published OR anything referenced
	// by this project was published.  This does not get updated on
	// republishes.
	const U32 iLastUpdatedTime;						AST(PERSIST SUBSCRIBE FORMATSTRING(HTML_SECS = 1))
	
	const U32 iDeletionTime;						AST(PERSIST SUBSCRIBE FORMATSTRING(HTML_SECS = 1))

	CONST_EARRAY_OF(UGCProjectSeriesVersion) eaVersions; AST(PERSIST SUBSCRIBE)
	
	const UGCProjectReviews ugcReviews;				AST(PERSIST SUBSCRIBE)

	// Cache used by the UGCSearchManager
	UGCSeriesSearchCache ugcSearchCache;			AST(PERSIST SUBSCRIBE)
	
	//////////////////////////////////////////////////////////////////////////////
	// The following fields are all for tracking UGC Export/Import project series
	//////////////////////////////////////////////////////////////////////////////

	CONST_STRING_MODIFIABLE strImportComment;	AST(PERSIST SUBSCRIBE NAME(ImportComment))

	CONST_STRING_MODIFIABLE strPreviousShard;	AST(PERSIST SUBSCRIBE NAME(PreviousShard))
	// Original Shard this project series was imported from

	const ContainerID iIdOnPreviousShard;		AST(PERSIST SUBSCRIBE)
	// If a UGCProjectSeries gets transferred between shards, we want
	// to track its previous container ID so that we can find duplicates and so forth.

	//////////////////////////////////////////////////////////////////////////////
	// End UGC Export/Import fields
	//////////////////////////////////////////////////////////////////////////////

	// Extra data needed when project series details are requested.
	UGCExtraDetailData* pExtraDetailData;
} UGCProjectSeries;
extern ParseTable parse_UGCProjectSeries[];
#define TYPE_parse_UGCProjectSeries UGCProjectSeries

AUTO_STRUCT AST_CONTAINER;
typedef struct UGCProjectSeriesVersion
{
	CONST_STRING_MODIFIABLE strName;				AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE strDescription;			AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE strImage;				AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(UGCProjectSeriesNode) eaChildNodes; AST(PERSIST SUBSCRIBE)

	// Publishing state
	const UGCProjectVersionState eState;			AST(PERSIST SUBSCRIBE)
 	const UGCTimeStamp sPublishTimeStamp;			AST(PERSIST SUBSCRIBE) 
} UGCProjectSeriesVersion;
extern ParseTable parse_UGCProjectSeriesVersion[];
#define TYPE_parse_UGCProjectSeriesVersion UGCProjectSeriesVersion

#define POSSIBLEUGCPROJECT_FLAG_NOPUBLISHING		(1 << 0)
#define POSSIBLEUGCPROJECT_FLAG_CANDELETE			(1 << 1)
#define POSSIBLEUGCPROJECT_FLAG_NEW_NEVER_SAVED		(1 << 2)
#define POSSIBLEUGCPROJECT_FLAG_USENEWMAPTRANSFER	(1 << 3)

//when the user is choosing which UGC project to edit, they choose one of these
AUTO_STRUCT;
typedef struct PossibleUGCProject
{
	ContainerID iID; //0 = new
	ContainerID iCopyID; // If not 0 and iID = 0, duplicate an existing project as new
	ContainerID iSeriesID;							AST(NAME("seriesID"))
	DynamicPatchInfo *pPatchInfo;
	U32 iPossibleUGCProjectFlags; //iFe, POSSIBLEUGCPROJECT_FLAG_XXX

	ContainerID iVirtualShardID; //if and only if set, game server will be in this virtual shard

	UGCProjectInfo* pProjectInfo;
	UGCProjectReviews* pProjectReviews;
	int iProjectReviewsPageNumber;
	UGCProjectStatusQueryInfo* pStatus;

	int iEditQueueCookie; //used on the client to store whether it's gotten to the front of the edit GS queue

	char *strEditVersionNamespace; // corresponding to iID
	bool bEditVersionIsNew; // corresponding to iID

	char *strCopyEditVersionNamespace; // corresponding to iCopyID
	bool bCopyEditVersionIsNew; // corresponding to iCopyID
} PossibleUGCProject;
extern ParseTable parse_PossibleUGCProject[];
#define TYPE_parse_PossibleUGCProject PossibleUGCProject

/// When a player wants to know more about a specific project or
/// series, this structure is used.
AUTO_STRUCT;
typedef struct UGCDetails
{
	UGCProject* pProject;
	UGCProjectSeries* pSeries;
} UGCDetails;
extern ParseTable parse_UGCDetails[];
#define TYPE_parse_UGCDetails UGCDetails

#define UGC_SERIES_ID_FROM_PROJECT (-1)


AUTO_STRUCT;
typedef struct PossibleUGCProjects
{
	PossibleUGCProject **ppProjects;
	int iProjectSlotsUsed;

	bool bNewProjectSeries;
	UGCProjectSeries** eaProjectSeries;

	// Only filled out by PossibleUGCProject search on login server.
	int iProjectSlotsMax;
	int iSeriesSlotsMax;
	U32 uAccountAcceptedProjectEULACrc;
} PossibleUGCProjects;
extern ParseTable parse_PossibleUGCProjects[];
#define TYPE_parse_PossibleUGCProjects PossibleUGCProjects

AUTO_ENUM;
typedef enum UGCProjectSearchFilterType
{
	UGCFILTER_STRING,
	UGCFILTER_RATING,
	UGCFILTER_AVERAGEPLAYTIME,

	//might go away... this is what strings in the "simple" box get turned into for not-actually-simple searches.
	//returns true if stricmps to any of author, description, or title
	//
	//string should already be SSSTree-internalized, just to make it all alphanum
	UGCFILTER_SIMPLESTRING,
} UGCProjectSearchFilterType;

AUTO_ENUM;
typedef enum UGCProjectSearchFilterComparison
{
	UGCCOMPARISON_EXACT,

	// For strings
	UGCCOMPARISON_CONTAINS,
	UGCCOMPARISON_NOTCONTAINS,
	UGCCOMPARISON_BEGINSWITH,
	UGCCOMPARISON_ENDSWITH,

	// For numbers
	UGCCOMPARISON_LESSTHAN=4,
	UGCCOMPARISON_GREATERTHAN=5
} UGCProjectSearchFilterComparison;

AUTO_STRUCT;
typedef struct UGCProjectSearchFilter
{
	char *pField;
	UGCProjectSearchFilterType eType;
	UGCProjectSearchFilterComparison eComparison;
	char *pStrValue; AST(ESTRING)
	U32 uIntValue; //language for language searches
	float fFloatValue;
} UGCProjectSearchFilter;
extern ParseTable parse_UGCProjectSearchFilter[];
#define TYPE_parse_UGCProjectSearchFilter UGCProjectSearchFilter

//if set, then the searching will be restricted to a sublist of some type
AUTO_ENUM;
typedef enum UGCProjectSearchSpecialType
{
	SPECIALSEARCH_NONE,
	SPECIALSEARCH_WHATSHOT,		// note that whatsHot maps will NOT be sorted by rating, but by "hotness"
	SPECIALSEARCH_NEW,			// maps that were finished reviewing in the past n days, where n will be set to some value
	SPECIALSEARCH_REVIEWER,		// only returns maps that need to be reviewed, NOT sorted by rating
	SPECIALSEARCH_BROWSE,		// returns content suitable for browsing
	SPECIALSEARCH_FEATURED,		// only returns featured content
	SPECIALSEARCH_SUBCRIBED,	// return the contents of the pSubscription struct
	SPECIALSEARCH_FEATURED_AND_MATCHING,	// returns featured content and normal content matching the filters.
} UGCProjectSearchSpecialType;
extern StaticDefineInt LanguageEnum[];

AUTO_STRUCT;
typedef struct UGCSubscriptionSearchInfo
{
	ContainerID* eaiAuthors;
} UGCSubscriptionSearchInfo;
extern ParseTable parse_UGCSubscriptionSearchInfo[];
#define TYPE_parse_UGCSubscriptionSearchInfo UGCSubscriptionSearchInfo

AUTO_STRUCT;
typedef struct UGCSearchResult
{
	UGCProjectSearchSpecialType eType;

	REF_TO(Message) hErrorMessage;			AST( NAME("ErrorMessage"))

	// List of search results
	UGCContentInfo** eaResults;				AST( NAME("Result"))

	// List of projects in the UGCProjectSeries returned
	ContainerID* eaProjectSeriesProjectIDs; AST( NAME("ProjectSeriesProject"))

	//if true, then we hit our maximum cap of # projects to return
	bool bCapped;							AST( NAME("Capped"))
} UGCSearchResult;
extern ParseTable parse_UGCSearchResult[];
#define  TYPE_parse_UGCSearchResult UGCSearchResult

AUTO_STRUCT;
typedef struct UGCProjectSearchInfo
{
	//make sure to always set this
	int iAccessLevel;

	// MJF July/26/2013 -- The following are only set by the login
	// server for PossibleUGCProject search.  It probably should use a
	// different data structure.
	int iMaxProjectSlots;
	int iMaxSeriesSlots;
	U32 uAccountAcceptedProjectEULACrc;

	//used internally on the search manager only
	bool bIsIDString; NO_AST

	//all of these fields are "optional", in the sense that some can be on and some can be off in various contexts
	U32 iOwnerAccountID;
	
	char *pSimple_Raw;
	char *pSimple_SSSTree;

	UGCProjectSearchFilter **ppFilters;

	UGCTag *eaiIncludeAllTags;
	UGCTag *eaiIncludeAnyTags;
	UGCTag *eaiIncludeNoneTags;

	// Used in the search manager. This is not meant to be filled in for searches
	UGCProjectSearchFilter **ppTempFilters;	AST(NO_NETSEND UNOWNED)

	ContainerID iVirtualShardID;

	const char *pchPlayerAllegiance;	AST(POOL_STRING)

    S32 iPlayerLevel;
	Language eLang;
	const char *pchLocation;			AST(POOL_STRING)

	// These flags are only used by the Featured And Matching search (NW barmaids).
	// The need for these flags is a side effect of UGC Search 1.0 architecture and is not needed or used by UGC Search 2.0.
	bool bExcludeFeaturedInResults; NO_AST // Controls whether or not filters should exclude featured projects in results.
	bool bExcludeLatterSeriesProjectsInResults; NO_AST // Controls whether or not filters should exclude Projects in a Series that are not the first one in the Series.

	// Whether or not the caller expects previously featured content to be treated as featured content
	bool bFeaturedIncludeArchives;

	UGCSubscriptionSearchInfo* pSubscription;

	UGCProjectSearchSpecialType eSpecialType;

	U32 loginServerID; // If sent as search info from a LoginServer. This allows command to go across shards and return.

	U64 loginCookie; // If sent as search info from a LoginServer. This allows command to go across shards and return.

	PossibleUGCProjects *pPossibleUGCProjects; // Possible UGC projects filled out upon return

	char *pcShardName; // This allows command to go across shards and return. This is set whether coming from LoginServer or a GameServer.

	ContainerID entContainerID; // If sent as search info from an Entity. This allows command to go across shards and return to wherever the Entity is at.

	ContainerID gameServerID; // If sent as search info from a GameServer. This allows command to go across shards and return to wherever the same GameServer.

	UGCSearchResult *pUGCSearchResult; // Filled out when searching projects from a GameServer
} UGCProjectSearchInfo;
extern ParseTable parse_UGCProjectSearchInfo[];
#define TYPE_parse_UGCProjectSearchInfo UGCProjectSearchInfo

//send along to the auto transaction which does a UGC project save or publish
AUTO_STRUCT;
typedef struct InfoForUGCProjectSaveOrPublish
{
	UGCProjectInfo sProjectInfo;
	
	char **ppMapNames;

	// To find all the places you need to update to add a per
	// UGCProjectVersion field, search for this: {{UGCPROJECTVERSION}}

	//publish only
	char *pEditingNameSpace; AST(ESTRING) 
	char *pPublishUUID;
	char *pPublishNameSpace; AST(ESTRING)
	char *pPublishJobName;

	// Mission grant info
	char* pCostumeOverride;
	const char* pPetOverride;			AST(POOL_STRING)
	char* pBodyText;
	char* strInitialMapName;
	char* strInitialSpawnPoint;
	
	//only the top-level fields of the struct, not the earrays of versions and so forth
	UGCProject *pProjectHeaderCopy;

	RemoteCommandGroup *pWhatToDoIfPublishJobDoesntStart; NO_AST

	ContainerID entContainerID;
} InfoForUGCProjectSaveOrPublish;
extern ParseTable parse_InfoForUGCProjectSaveOrPublish[];
#define TYPE_parse_InfoForUGCProjectSaveOrPublish InfoForUGCProjectSaveOrPublish

AUTO_STRUCT;
typedef struct UGCProjectList
{
	UGCProject** eaProjects;				AST( NAME("Project"))
	UGCProjectSeries** eaProjectSeries;		AST( NAME("ProjectSeries"))
} UGCProjectList;
extern ParseTable parse_UGCProjectList[];
#define TYPE_parse_UGCProjectList UGCProjectList

AUTO_STRUCT;
typedef struct UGCProjectStatusQueryInfo
{
	char* strPublishedName;
	UGCMapLocation* pPublishedMapLocation;
	U32 iLastPublishTime;
	U32 iLastSaveTime;
	bool bLastPublishSucceeded;
	bool bCurrentlyPublishing;
	bool bPublishingDisabled;
	int iCurPlaceInQueue; //if 0, not in a queue
	F32 fPublishPercentage;
	EntityRef iEntityRef;
	bool bCanBeWithdrawn;

	// Featured status
	UGCFeaturedData* pFeatured;
	bool bAuthorAllowsFeatured;
} UGCProjectStatusQueryInfo;
extern ParseTable parse_UGCProjectStatusQyueryInfo[];
#define TYPE_parse_UGCProjectStatusQueryInfo UGCProjectStatusQueryInfo

//NULL pUUID and pNameSpace means create them
NOCONST(UGCProjectVersion) *UGCProject_CreateEmptyVersion(ATR_ARGS, ATH_ARG NOCONST(UGCProject) *pProject, char *pUUIDString_In, char *pNameSpace_In);
enumTransactionOutcome UGCProject_trh_WithdrawProject(ATR_ARGS, ATH_ARG NOCONST(UGCProject) *pProject, ATH_ARG NOCONST(UGCProjectSeries) *pProjectSeries, bool bWithdraw, const char* pComment);
bool UGCProject_IsFeaturedNow(ATH_ARG const NOCONST(UGCProject)* pProject );
bool UGCProject_IsFeatured(ATH_ARG const NOCONST(UGCProject)* pProject, bool bIncludePreviouslyFeatured, bool bIncludeFutureFeatured );
bool UGCProject_IsFeaturedWindow(U32 iStartTimestamp, U32 iEndTimestamp, bool bIncludePreviouslyFeatured, bool bIncludeFutureFeatured );
bool UGCProject_IsImportant(ATH_ARG const NOCONST(UGCProject)* pProject);

//returns a POOLED string
char *UGCProject_GetUniqueMapDescription(char *pNameSpace);

void UGCProject_MakeNamespace(char **ppOutString, ContainerID iProjectID, char *pUUIDStr);



//creates a copy with all the top-level info but not the earrays of versions, etc.
UGCProject *UGCProject_CreateHeaderCopy(UGCProject *pProject, bool bAlsoIncludeMostRecentPublish);

//similar to UGCProject_CreateHeaderCopy but also includes more information
UGCProject *UGCProject_CreateDetailCopy(UGCProject *pProject, S32 iReviewsPage, bool bAlsoIncludeMostRecentPublish, U32 uReviewerAccountID);

UGCProjectSeries* UGCProjectSeries_CreateEditingCopy(const UGCProjectSeries* pSeries);
UGCProjectSeries* UGCProjectSeries_CreateHeaderCopy(const UGCProjectSeries* pSeries);
UGCProjectSeries* UGCProjectSeries_CreateDetailCopy(const UGCProjectSeries* pSeries, S32 iReviewsPage, U32 uReviewerAccountID);

//makes sure the name is legal, other pre-creation checks potentially
bool UGCProject_ValidateNewProjectRequest(PossibleUGCProject *pRequest, char **ppErrorString);
 
//makes sure the project has a published version that has not already been sent
bool UGCProject_IsReadyForSendToOtherShard(ATH_ARG UGCProject *pProject, char **ppWhyNot);

UGCProjectStatusQueryInfo* UGCProject_GetStatusFromProject(UGCProject *pProject, const char **ppchJobName, bool *pbCurrentlyPublishing);

//a UGC project can be withdrawn if any of its versions are published, publishing, or republishing
bool UGCProject_CanBeWithdrawn(NOCONST(UGCProject) *pProject);

bool UGCProject_trh_VersionIsPlayable(ATR_ARGS, NOCONST(UGCProject) *pProject, NOCONST(UGCProjectVersion) *pVersion);
#define UGCProject_VersionIsPlayable(pProject, pVersion) UGCProject_trh_VersionIsPlayable(ATR_EMPTY_ARGS, CONTAINER_NOCONST(UGCProject, pProject), CONTAINER_NOCONST(UGCProjectVersion, pVersion))

U32 UGCProject_trh_GetTotalPlayedCount(ATH_ARG const NOCONST(UGCProject)* pProject);
#define UGCProject_GetTotalPlayedCount(pProject) UGCProject_trh_GetTotalPlayedCount(CONTAINER_NOCONST(UGCProject, pProject))

// returns true if the UGCProject qualifies for mission rewards.
bool UGCProject_trh_QualifiesForRewards(ATR_ARGS, ATH_ARG NOCONST(UGCProject)* pProject);
#define UGCProject_QualifiesForRewards(pProject) UGCProject_trh_QualifiesForRewards(ATR_EMPTY_ARGS, CONTAINER_NOCONST(UGCProject, pProject))

bool UGCProject_trh_BeingPublished(ATR_ARGS, ATH_ARG NOCONST(UGCProject) *pProject);
bool UGCProject_trh_AnyVersionNeedsAttention(ATR_ARGS, ATH_ARG NOCONST(UGCProject)* pProject);

void UGCProject_ApplySaveOrPublishInfoToVersion(NOCONST(UGCProjectVersion) *pMostRecent, InfoForUGCProjectSaveOrPublish *pInfo);
void UGCProject_ApplyProjectInfoToVersion(NOCONST(UGCProjectVersion) *pMostRecent, UGCProjectInfo *pInfo);

//used by RemoteCommand_GetUGCNameSpacePublishState()
AUTO_ENUM;
typedef enum UGCNameSpacePublishStateType
{
	PUBLISHSTATE_PUBLISHED,
	PUBLISHSTATE_UNKNOWN,
	PUBLISHSTATE_WITHDRAWN,
	PUBLISHSTATE_PROJECTDELETED,
	PUBLISHSTATE_CORRUPT,
} UGCNameSpacePublishStateType;

AUTO_STRUCT;
typedef struct UGCNameSpacePublishState
{
	UGCNameSpacePublishStateType eState;
	U32 iPresumedDeletionTime; //if state is WITHDRAWN or PROJECTDELETED
} UGCNameSpacePublishState;

//if a publish fails and this string is in the failure string, no alert will be generated
#define PUBLISH_FAIL_NOALERT_STRING "UGC_NO_ALERT"

//flags that can be passed into the republish process. They are passed as an argument to RepublishAndWithdrawFilteredList
//or set on the mapmanager command line with -RepubFlagsForAutoRepub 
#define REPUB_FLAG_TEST (1)

AUTO_STRUCT;
typedef struct UGCIDList
{
	ContainerID* eaProjectIDs;
	ContainerID* eaProjectSeriesIDs;
} UGCIDList;
extern ParseTable parse_UGCIDList[];
#define TYPE_parse_UGCIDList UGCIDList

AUTO_STRUCT;
typedef struct UGCShardReturnAndErrorString
{
	bool bSucceeded;
	char* estrDetails;	AST(ESTRING)
} UGCShardReturnAndErrorString;
extern ParseTable parse_UGCShardReturnAndErrorString[];
#define TYPE_parse_UGCShardReturnAndErrorString UGCShardReturnAndErrorString

AUTO_STRUCT;
typedef struct UGCShardReturnProjectReviewed {
	ContainerID iProjectID;
	char* pchProjName;

	ContainerID entContainerID;
} UGCShardReturnProjectReviewed;
extern ParseTable parse_UGCShardReturnProjectReviewed[];
#define TYPE_parse_UGCShardReturnProjectReviewed UGCShardReturnProjectReviewed

AUTO_STRUCT;
typedef struct UGCShardReturnProjectSeriesReviewed {
	ContainerID iSeriesID;
	char* pchSeriesName;

	float fRating;
	int *eaiUGCTags;				AST(SUBTABLE(UGCTagEnum))
	char* pchComment;

	char *pcShardName;
	ContainerID entContainerID;
} UGCShardReturnProjectSeriesReviewed;
extern ParseTable parse_UGCShardReturnProjectSeriesReviewed[];
#define TYPE_parse_UGCShardReturnProjectSeriesReviewed UGCShardReturnProjectSeriesReviewed

AUTO_STRUCT;
typedef struct UGCSearchData {
	char *pcShardName;
	ContainerID entContainerID;

	UGCProjectSearchInfo* pSearch;
} UGCSearchData;
extern ParseTable parse_UGCSearchData[];
#define TYPE_parse_UGCSearchData UGCSearchData

AUTO_STRUCT;
typedef struct UGCSubscriptionData {
	ContainerID entContainerID;
	char* strSubscribedToName;
	ContainerID iAuthorID;
} UGCSubscriptionData;
extern ParseTable parse_UGCSubscriptionData[];
#define TYPE_parse_UGCSubscriptionData UGCSubscriptionData

AUTO_STRUCT;
typedef struct ReturnedPossibleUGCProject
{
	PossibleUGCProject *pPossibleUGCProject;
	const char *strError;
} ReturnedPossibleUGCProject;
extern ParseTable parse_ReturnedPossibleUGCProject[];
#define TYPE_parse_ReturnedPossibleUGCProject ReturnedPossibleUGCProject

AUTO_STRUCT;
typedef struct UGCProjectRequestData
{
	SlowRemoteCommandID iCmdID;

	NewOrExistingGameServerAddressRequesterInfo requesterInfo;
} UGCProjectRequestData;
extern ParseTable parse_UGCProjectRequestData[];
#define TYPE_parse_UGCProjectRequestData UGCProjectRequestData

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "AccountID, NameSpace, Playable, StatsQualifyForUGCRewards, NumberOfPlays, AverageDurationInMinutes, FeaturedStartTimestamp, FeaturedEndTimestamp");
typedef struct UGCPlayableNameSpaceData
{
	ContainerID uAccountID;				AST(NAME(AccountID))

	char *strNameSpace;					AST(NAME(NameSpace))
	bool bPlayable;						AST(NAME(Playable))

	bool bStatsQualifyForUGCRewards;	AST(NAME(StatsQualifyForUGCRewards))
	U32 iNumberOfPlays;					AST(NAME(NumberOfPlays))
	float fAverageDurationInMinutes;	AST(NAME(AverageDurationInMinutes))

	// These should never be read by GameServers. GameServers should read the 2 flags below these timestamps.
	U32 iFeaturedStartTimestamp;		AST(NAME(FeaturedStartTimestamp))
	U32 iFeaturedEndTimestamp;			AST(NAME(FeaturedEndTimestamp))

	// These 2 are set by the MapManager only when a GameServer requests namespace playable data so that they always reflect the current time.
	bool bProjectIsFeatured;
	bool bProjectWasFeatured;
} UGCPlayableNameSpaceData;
extern ParseTable parse_UGCPlayableNameSpaceData[];
#define TYPE_parse_UGCPlayableNameSpaceData UGCPlayableNameSpaceData

AUTO_STRUCT;
typedef struct UGCPlayableNameSpaceDataReturn
{
	SlowRemoteCommandID iCmdID;

	UGCPlayableNameSpaceData ugcPlayableNameSpaceData;
} UGCPlayableNameSpaceDataReturn;
extern ParseTable parse_UGCPlayableNameSpaceDataReturn[];
#define TYPE_parse_UGCPlayableNameSpaceDataReturn UGCPlayableNameSpaceDataReturn

AUTO_STRUCT;
typedef struct UGCPlayableNameSpaces
{
	UGCPlayableNameSpaceData **eaUGCPlayableNameSpaceData;
} UGCPlayableNameSpaces;
extern ParseTable parse_UGCPlayableNameSpaces[];
#define TYPE_parse_UGCPlayableNameSpaces UGCPlayableNameSpaces

AUTO_STRUCT;
typedef struct UGCSynchronizeAchievementsData
{
	char *pcShard;
	ContainerID entContainerID;
	ContainerID ugcAccountID;
} UGCSynchronizeAchievementsData;
extern ParseTable parse_UGCSynchronizeAchievementsData[];
#define TYPE_parse_UGCSynchronizeAchievementsData UGCSynchronizeAchievementsData

AUTO_STRUCT;
typedef struct UGCIntershardData
{
	char *pcShardName;

	// One of these 2 should be set:

	// Entity originating the inter-shard remote command
	ContainerID entContainerID;

	// Login server originating the inter-shard remote command
	U32 loginServerID;

	// Login link originating the inter-shard remote command
	U64 loginCookie;
} UGCIntershardData;
extern ParseTable parse_UGCIntershardData[];
#define TYPE_parse_UGCIntershardData UGCIntershardData

AUTO_STRUCT;
typedef struct UgcAchievementsNotifyData
{
	char *pcShardName;
	ContainerID entContainerID;

	ContainerID uUGCAuthorID;
	U32 uFromTime;
	U32 uToTime;
} UgcAchievementsNotifyData;
extern ParseTable parse_UgcAchievementsNotifyData[];
#define TYPE_parse_UgcAchievementsNotifyData UgcAchievementsNotifyData

AUTO_STRUCT;
typedef struct UGCPatchInfo
{
	char *server;
	int port;
	char *project;
	char *shard;
} UGCPatchInfo;
extern ParseTable parse_UGCPatchInfo[];
#define TYPE_parse_UGCPatchInfo UGCPatchInfo

AUTO_STRUCT;
typedef struct UGCProjectContainerCreateForImportData
{
	UGCProject *pUGCProject;

	DynamicPatchInfo *pDynamicPatchInfo;

	char *estrError;						AST(ESTRING)
} UGCProjectContainerCreateForImportData;
extern ParseTable parse_UGCProjectContainerCreateForImportData[];
#define TYPE_parse_UGCProjectContainerCreateForImportData UGCProjectContainerCreateForImportData

AUTO_STRUCT;
typedef struct UGCProjectSeriesContainerCreateForImportData
{
	UGCProjectSeries *pUGCProjectSeries;

	char *estrError;						AST(ESTRING)
} UGCProjectSeriesContainerCreateForImportData;
extern ParseTable parse_UGCProjectSeriesContainerCreateForImportData[];
#define TYPE_parse_UGCProjectSeriesContainerCreateForImportData UGCProjectSeriesContainerCreateForImportData

// UGC-specific data for a Mission, if it is from a UGC project
AUTO_STRUCT AST_CONTAINER AST_IGNORE(iFeaturedStartTimestamp) AST_IGNORE(iFeaturedEndTimestamp);
typedef struct UGCMissionData
{
	// This is true if the Mission is one's own UGC Mission. Used to prevent giving UGC rewards for this mission
	const bool bCreatedByAccount;			AST(PERSIST SERVER_ONLY)

	// If true, then the stats allow for UGC rewards
	const bool bStatsQualifyForUGCRewards;	AST(PERSIST SUBSCRIBE) // opening this up to client so mission journal can display
	const U32 iNumberOfPlays;				AST(PERSIST SERVER_ONLY)
	const float fAverageDurationInMinutes;	AST(PERSIST SERVER_ONLY)

	// If true, then this is a featured project at the time the project was accepted
	const bool bProjectIsFeatured;			AST(PERSIST SERVER_ONLY)
	// If true, then this was a featured project at some point in the past
	const bool bProjectWasFeatured;			AST(PERSIST SERVER_ONLY)

	// If true, then this player chose to play the mission as a beta reviewer. It is possible that the project is no longer in review once they turn it in.
	// So, we cache whether the player thought they were going to be a reviewer here.
	const bool bPlayingAsBetaReviewer;		AST(PERSIST SERVER_ONLY)

	// Accumulated playing time for the EntityPlayer playing this mission.
	const float fPlayingTimeInMinutes;		AST(PERSIST SERVER_ONLY)
} UGCMissionData;
extern ParseTable parse_UGCMissionData[];
#define TYPE_parse_UGCMissionData UGCMissionData

AUTO_STRUCT;
typedef struct UGCReviewData
{
	float fRating;
	char *pComment;
	int *eaiUGCTags;				AST(SUBTABLE(UGCTagEnum))
} UGCReviewData;
extern ParseTable parse_UGCReviewData[];
#define TYPE_parse_UGCReviewData UGCReviewData

// SQLite content table index. See aslUGCSearchManager2.c for schema (search file for "CREATE TABLE")
AUTO_STRUCT;
typedef struct UGCSearchIndex
{
	char *strName;					AST(NAME(Name))
	char *strCommaSeparatedColumns;	AST(NAME(Columns))
} UGCSearchIndex;
extern ParseTable parse_UGCSearchIndex[];
#define TYPE_parse_UGCSearchIndex UGCSearchIndex

// SQLite search config and optimization data. Uses an automatic reload file makes iterating over it easier.
AUTO_STRUCT;
typedef struct UGCSearchConfig
{
	UGCSearchIndex **eaIndexes;	AST(NAME(Index))
} UGCSearchConfig;
extern ParseTable parse_UGCSearchConfig[];
#define TYPE_parse_UGCSearchConfig UGCSearchConfig

UGCSearchConfig *ugcGetSearchConfig();

// The following setup and teardown accessors allow us to hot reload the config in dev mode... but only on the UGCSearchManager. GameServers only access UGCSearchConfig
// in order to make the bin file.
typedef void (*UGCSearchConfigCallback)(UGCSearchConfig *pUGCSearchConfig);
void ugcSetSearchConfigSetupFunction(UGCSearchConfigCallback setupFunction);
void ugcSetSearchConfigTeardownFunction(UGCSearchConfigCallback teardownFunction);

#define UGC_ACHIEVEMENT_TRANSACTION_NOTIFY_TIME_DID_NOT_INCREASE "UGC Achievement last notify time did not increase."

void ugcProjectSetVersionState(NOCONST(UGCProjectVersion) *pVersion, UGCProjectVersionState eNewState, const char *pComment);
UGCProjectVersionState ugcProjectGetVersionState(NOCONST(UGCProjectVersion) *pVersion);
// ugcProjectGetVersionStateConst accessor is in UGCProjectUtils.h

// The following 2 helper functions setup the bi-directional link between an author and a subscriber. The first one sets up the link on the subscriber account, the second one sets
// up the link on the author account.
// AUTO_TRANS_HELPER;
enumTransactionOutcome ugc_trh_SubscribeToAuthor(ATR_ARGS, NOCONST(UGCAccount) *pSubscriberAccount, ContainerID iSubscriberPlayerID, ContainerID iAuthorID);
// AUTO_TRANS_HELPER;
void ugc_trh_AuthorSubscribedTo(ATR_ARGS, ATH_ARG NOCONST(UGCAccount) *pAuthorAccount, ContainerID iSubscriberPlayerID, ContainerID iSubscriberAccountID);

// The following 2 helper functions teardown the bi-directional link between an author and a subscriber. The first one tears down the link on the subscriber account, the second one tears
// down the link on the author account.
// AUTO_TRANS_HELPER;
enumTransactionOutcome ugc_trh_UnsubscribeFromAuthor(ATR_ARGS, NOCONST(UGCAccount) *pSubscriberAccount, ContainerID iSubscriberPlayerID, ContainerID iAuthorID);
// AUTO_TRANS_HELPER;
void ugc_trh_AuthorUnsubscribedFrom(ATR_ARGS, ATH_ARG NOCONST(UGCAccount) *pAuthorAccount, ContainerID iSubscriberPlayerID);

NOCONST(UGCAccount) *ugcAccountClonePersistedAndSubscribedDataOnly(NOCONST(UGCAccount) *pUGCAccount);

//AUTO_TRANS_HELPER;
void ugc_trh_UGCProjectSeries_UpdateCache(ATR_ARGS, ATH_ARG NOCONST(UGCSeriesSearchCache)* ugcSeriesSearchCache, NON_CONTAINER UGCProjectSeriesVersion* newPublishedVersion);

//AUTO_TRANS_HELPER;
void UGCProject_trh_SetOwnerAccountName(ATR_ARGS, ATH_ARG NOCONST(UGCProject) *pUGCProject, const char *ownerAccountName);

//AUTO_TRANS_HELPER;
void UGCProjectSeries_trh_SetOwnerAccountName(ATR_ARGS, ATH_ARG NOCONST(UGCProjectSeries) *pUGCProjectSeries, const char *ownerAccountName);

//AUTO_TRANS_HELPER;
F32 UGCProject_trh_AverageDurationInMinutes(ATR_ARGS, ATH_ARG NOCONST(UGCProject) *pUGCProject);
#define UGCProject_AverageDurationInMinutes(pProject) UGCProject_trh_AverageDurationInMinutes(ATR_EMPTY_ARGS, CONTAINER_NOCONST(UGCProject, pProject))

float UGCProject_Rating( const UGCProject* pProject );
float UGCProjectSeries_Rating( const UGCProjectSeries* pProjectSeries );
float UGCProject_RatingForSorting( const UGCProject* pProject );
float UGCProjectSeries_RatingForSorting( const UGCProjectSeries* pProjectSeries );

// AUTO_TRANS_HELPER;
enumTransactionOutcome ugc_trh_ComputeAggregateTagData(ATR_ARGS, ATH_ARG NOCONST(UGCProjectReviews) *pUGCProjectReviews);
