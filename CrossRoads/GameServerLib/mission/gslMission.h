/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "referencesystem.h"
#include "mission_enums.h"

typedef struct ActivityDef ActivityDef;
typedef struct CompletedMission CompletedMission;
typedef struct ContactRewardChoices ContactRewardChoices;
typedef struct CritterDef CritterDef;
typedef struct CritterGroup CritterGroup;
typedef struct Entity Entity;
typedef U32 EntityRef; // This is an opaque entity identifier, which is implemented elsewhere
typedef struct Mission Mission;
typedef struct MissionInfo MissionInfo;
typedef struct MissionDef MissionDef;
typedef struct MissionOfferParams MissionOfferParams;
typedef enum MissionState MissionState;
typedef enum MissionActionTrigger MissionActionTrigger;
typedef struct MissionIter MissionIter;
typedef struct OpenMission OpenMission;
typedef struct Critter Critter;
typedef struct Entity Entity;
typedef struct NOCONST(Mission) NOCONST(Mission);
typedef struct ContactDef ContactDef;
typedef struct QueuedMissionOffer QueuedMissionOffer;
typedef U32 ContainerID;
typedef struct ZoneMap ZoneMap;
typedef struct Team Team;
typedef struct TransactionReturnVal TransactionReturnVal;
typedef enum GuildStatUpdateOperation GuildStatUpdateOperation;

// Messages
#define MISSION_COMPLETE_MSG "MissionSystem.MissionComplete"
#define MISSION_FAILED_MSG "MissionSystem.MissionFailed"
#define MISSION_SUBOBJECTIVE_COMPLETE_MSG "MissionSystem.SubObjectiveComplete"
#define MISSION_INVIS_SUBOBJECTIVE_COMPLETE_MSG "MissionSystem.InvisibleSubObjectiveComplete"

typedef void(*MissionActionFunc)(void*);
typedef void (*TransactionReturnCallback)(TransactionReturnVal *returnVal, void *userData);

								
//////////////////////////////////////////////////////////////////////////////////////
//  General configuration for the mission system. Including offer and max reward
//  offset lookup							
AUTO_STRUCT;
typedef struct LevelOffset
{
	S32 iUpThroughLevel;
	S32 iOffset;				AST(NAME(MissionLeveloffset))
} LevelOffset;
								
AUTO_STRUCT;
typedef struct MissionLevelOffsetDef
{
	LevelOffset **ppOffsets;	AST(NAME(Leveloffset))
	S32 iDefaultOffset;			
} MissionLevelOffsetDef;
								
AUTO_STRUCT;
typedef struct MissionConfig
{
	U32 uPlayerCalloutTimeSeconds;						AST(NAME(PlayerCalloutTimeSeconds))
	F32 fCalloutPercent;								AST(NAME(CalloutPercent))

	// If True, performs slow searching of doors for mission waypoints, if the mission is a Perk.
	// Our games can have 1000 perks and this function is called for all perks every time a player enters a map.
	// If map also has lots of interactibles, setting this can hang the server for several seconds.
	bool bShouldSearchDoorsForPerkMissionWaypoints;		AST(NAME(ShouldSearchDoorsForPerkMissionWaypoints))
	bool bDoNotSearchDoorsForWayPoints;					AST(NAME(DoNotSearchDoorsForWaypoints))

	MissionLevelOffsetDef	MissionOfferOffsets;
	MissionLevelOffsetDef	MissionMaxRewardOffsets;

	F32 fGlobalSecondaryNumericRewardScale;				AST(NAME(GlobalSecondaryNumericRewardScale))
} MissionConfig;

extern MissionConfig g_MissionConfig;

//////////////////////////////////////////////////////////////////////////////////////

typedef struct MissionActionDelayed {
	MissionActionFunc actionFunc;
	int iPartitionIdx;
	EntityRef entRef;
	void *pData1;
	void *pData2;
	// add more as needed?
} MissionActionDelayed;

// List of reasons why a Mission can't be offered to a player
typedef enum MissionOfferStatus {
	MissionOfferStatus_OK,
	MissionOfferStatus_HasMission,
	MissionOfferStatus_HasCompletedMission,
	MissionOfferStatus_TooLowLevel,
	MissionOfferStatus_FailsRequirements,
	MissionOfferStatus_MissionOnCooldown,
	MissionOfferStatus_SecondaryCooldown, // Has completed this as a Secondary too recently
	MissionOfferStatus_HasNemesisArc,
	MissionOfferStatus_InvalidAllegiance,
} MissionOfferStatus;

#define NOCONSTMISSION(mission) CONTAINER_NOCONST(Mission, mission)

// Creates a Mission
NOCONST(Mission)* mission_CreateMission(int iPartitionIdx, const MissionDef *pDef, U8 iMissionLevel, U32 iEntID);

// Creates and adds a non-persisted Mission
void mission_AddNonPersistedMission(int iPartitionIdx, MissionInfo *pInfo, const MissionDef *pMissionDef);

// Sets the discovered flag on a mission, and if it hasn't been persisted yet, this moves it
// from the non-persisted list to the discovered list (also non-persisted)
void mission_DiscoverMission(Mission *pMission);

// Adds a sub-mission to a non-persisted Mission
void mission_AddNonPersistedSubMission(int iPartitionIdx, Mission *pParentMission, const MissionDef *pMissionDef, bool bSkipActions);

// Does post create/load from DB create or character loading
void mission_PostMissionCreateInit(int iPartitionIdx, Mission *pMission, MissionInfo *pInfo, OpenMission *pOpenMission, Mission *pParentMission, bool bStartTracking);
void mission_PostMissionCreateInitRecursive(int iPartitionIdx, Mission *pMission, MissionInfo *pInfo, OpenMission *pOpenMission, Mission *pParentMission, bool bStartTracking);

// Undoes all the initialization of a mission before it is destroyed
void mission_PreMissionDestroyDeinit(int iPartitionIdx, Mission *pMission);
void mission_PreMissionDestroyDeinitRecursive(int iPartitionIdx, Mission *pMission);

int missiondef_PostProcessFixup(MissionDef* pDef, MissionDef *pParentDef);

// Turn a mission in to a contact.  Does no validation that mission is actually complete
void mission_TurnInMission(Entity *pPlayerEnt, MissionDef *pMissionDef, ContactRewardChoices *pRewardChoices);

// Helper function which refreshes the entity's remote contact list before completing the mission
void mission_CompleteMission(Entity *pEnt, Mission *pMission, bool bForcePermanentComplete);

// Shares a Mission with the player's teammates
void mission_ShareMission(Entity *pEnt, const char *pcMissionDefName, bool silent, bool bAlwaysNotifyIfOutOfRange);

// Set this mission as primary or clear primary mission
void mission_SetPrimaryMission(Entity *pEnt, const char *pcMissionDefName);

// Mission updates all children and then evaluates to see if it needs to do a state transition
// Returns true if it has begun a transaction to update the state of a mission
// Will only change one mission's state at a time, returning out until the next iteration
bool mission_UpdateState(int iPartitionIdx, Entity *pEnt, Mission *pMission);

void mission_UpdateCount(int iPartitionIdx, MissionDef *pDef, Mission *pMission, bool bUpdateTimestamp);

void mission_UpdateTimestamp(MissionDef *pDef, Mission *pMission);

void mission_TriggerCurrentEvent(Entity *pPlayerEnt, Mission *pMission);
void mission_CompleteCurrentPhase(Entity *pPlayerEnt, Mission *pMission);

// Amount of time that has elapsed since the mission started
U32 mission_TimeElapsed(Mission *pMission);

// Amount of time remaining if this is a timed mission
int mission_TimeRemaining(MissionDef *pDef, Mission *pMission);

// Time when the mission started
U32 mission_GetStartTime(const Mission *pMission);

// Time when the mission timer expires for timed missions
U32 mission_GetEndTime(MissionDef *pDef, const Mission *pMission);

// Returns true if a mission needs to be evaluated
bool mission_NeedsEvaluation(Mission *pMission);

// Flag a mission as needing to be evaluated next tick
void mission_FlagAsNeedingEval(Mission *pMission);

// Flag a mission as needing to be resent to the client
void mission_FlagAsDirty(Mission *pMission);

// Flag a CompletedMission as needing to be resent to the client
void mission_FlagCompletedMissionAsDirty(MissionInfo *pInfo, CompletedMission* pCompletedMission);

// Flag a MissionInfo as needing to be resent to the client
void mission_FlagInfoAsDirty(MissionInfo *pInfo);

// Updates the number of active child Missions for a Mission
void mission_UpdateOpenChildren(Mission *pMission);

// Returns TRUE if this Mission is persisted on the player
bool mission_IsPersisted(SA_PARAM_NN_VALID const Mission *pMission);

// Main processing loop that updates and evaluates missions as necessary
void mission_OncePerFrame(F32 fTimeStep);

// Called whenever the player enters the map or MissionInfo is reset
void mission_ClearPlaceholderPlayerStats(Entity *pEnt);

// Hook for the mission system to do any needed setup once a player enters a map
void mission_PlayerEnteredMap(Entity *pPlayerEnt);

// Grants all missions that given when a player enters the map
void mission_GrantOnEnterMissions(Entity *pPlayerEnt, MissionInfo *pInfo);

// Grants all Perks, which players should always have
void mission_GrantPerkMissions(int iPartitionIdx, MissionInfo *pInfo);

// Hook for the mission system to do any needed setup once a player leaves a map
void mission_PlayerLeftMap(Entity *pPlayerEnt);

// Notify the mission system that an activity has ended
void gslMission_NotifyActivityEnded(ActivityDef* pActivityDef);

// Returns TRUE if the Entity can be offered the given MissionDef, even if only for secondary credit
bool missiondef_CanBeOfferedAtAll(Entity *pPlayerEnt, MissionDef *pMissionDef, int *piNextOfferLevel, MissionOfferStatus *pOfferStatus, MissionCreditType *pCreditType);

// Returns TRUE if the Entity is eligible for the given MissionDef and can accept it for Primary credit
bool missiondef_CanBeOfferedAsPrimary(Entity *pPlayerEnt, MissionDef *pMissionDef, int *piNextOfferLevel, MissionOfferStatus *pOfferStatus);

// Returns TRUE if the Entity meets the Requires expression on the MissionDef
int mission_EvalRequirements(int iPartitionIdx, MissionDef *pMissionDef, Entity *pPlayerEnt);

// Returns TRUE if the Map meets the Map Requires expression on the MissionDef for open missions
int mission_EvalMapRequirements(int iPartitionIdx, MissionDef *pDef);

// Runs either the success or failure expression when an open mission succeeds
int mission_RunMapComplete(int iPartitionIdx, MissionDef *pDef, MissionState iState);

// Checks whether this MissionDef counts towards the "Max Active Missions" limit
bool missiondef_CountsTowardsMaxActive(MissionDef *pDef);

// TRUE if this missiondef is normally shareable
bool missiondef_IsShareable(MissionDef *pDef);

// Does all the initialization work for the mission info after it is created/loaded from the DB
void mission_PostEntityCreateMissionInit(Entity *pPlayerEnt, bool bStartTrackingEvents);

// Undoes all the initialization of the mission info before it is destroyed
void mission_PreEntityDestroyMissionDeinit(int iPartitionIdx, Entity *pPlayerEnt);

// Cleans up invalid/out-of-date Mission data on the player
bool mission_VerifyEntityMissionData(Entity *pEnt);

// Restarts a mission
void mission_RestartMissionEx(int iPartitionIdx, Entity *pEnt, MissionInfo *pInfo, Mission *pMission, const MissionOfferParams *pParams, TransactionReturnCallback cb, void* cbData);
#define mission_RestartMission(iPartitionIdx, pEnt, pInfo, pMission, pParams) mission_RestartMissionEx(iPartitionIdx, pEnt, pInfo, pMission, pParams, NULL, NULL)

// Gets the number of missions the player has that count towards the "Max Active Missions" limit
U32 mission_GetNumMissionsTowardsMaxActive(MissionInfo *pInfo);

void mission_ApplyAllInteractableOverrides(void);
void missiondef_ApplyOverrides(MissionDef* pDef);
void missiondef_RemoveOverrides(MissionDef* pDef);
void missiondef_InitOverrides(MissionDef* pDef);
void missiondef_RefreshOverrides(MissionDef* pDef);

// Offers player a random mission
void mission_OfferRandomAvailableMission(Entity *pEnt, const char *pcJournalCat);

// Sees if player already has one
bool mission_CanPlayerTakeRandomMission(Entity *pEnt, const char *pcJournalCat);

// TRUE if accepting the mission will immediately start a timer
bool missiondef_HasOnStartTimerRecursive(MissionDef *pDef);

// Returns the length of the shortest timer that will begin when accepting the mission
U32 missiondef_OnStartTimerLengthRecursive(MissionDef *pDef);

// Returns true if the mission satisfies its activities requirements
bool missiondef_CheckRequiredActivitiesActive(MissionDef* pMissionDef);

// Calculates the level for the mission instance based on parameters in the missionDef
U8 missiondef_CalculateLevel(int iPartitionIdx, int iPlayerLevel, const MissionDef *pDef);

// Returns the offer level for this mission based on the target level
int missiondef_GetOfferLevel(int iPartitionIdx, MissionDef *pMissionDef, int iPlayerLevel);

// Get the level at which the player has outleveled this mission
int missiondef_GetMaxRewardLevel(int iPartitionIdx, MissionDef *pMissionDef, int iPlayerLevel);

void mission_ApplyAllNamespacedContactOverrides(void);

// Searches the mission tree for a specific missiondef. Does not have to be a root def. Recurses to child missions.
// TODO: Come up with a better naming scheme for the find child missions
Mission* mission_FindMissionFromDef(MissionInfo *pInfo, MissionDef *pDefToMatch);

// Returns true if the mission has a chance of giving a unique item reward when entering the passed in state.
bool missiondef_HasUniqueItemsInRewardsForState(MissionDef* pDef, MissionState eState, MissionCreditType eCreditType);

// ------------------------------
// Mission Sharing/Queueing
// ------------------------------

// Queues a Mission Offer for this player
void mission_QueueMissionOffer(Entity *pEnt, Entity *pSharer, MissionDef *pMissionDef, MissionCreditType eCreditType, U32 uTimerStartTime, bool bSilent);

// queue from a shared mission
void mission_QueueMissionOfferShared(Entity *pEnt, Entity *pSharer, const char *pcMissionDefName, MissionCreditType eCreditType, U32 uTimerStartTime, bool bSilent);

// If this Mission has recently been shared/offered to this Player, get the QueuedMissionOffer
QueuedMissionOffer *mission_GetQueuedMissionOffer(Entity *pEnt, MissionDef *pDef);

// This sends a message that a Shared Mission has been accepted.  (It does not change any data.)
void mission_SharedMissionAccepted(Entity *pPlayerEnt, QueuedMissionOffer *pShareInfo);

// This sends a message that a Shared Mission has been declined.  (It does not change any data.)
void mission_SharedMissionDeclined(Entity *pPlayerEnt, QueuedMissionOffer *pShareInfo);

// Update activities with mission dependencies
void missiondef_UpdateActivityMissionDependencies(MissionDef* pMissionDef);

// --------------------------------------------------------------------
//  Load and Validation
// --------------------------------------------------------------------

void mission_PartitionLoad(int iPartitionIdx);
void mission_PartitionUnload(int iPartitionIdx);
void mission_MapValidate(ZoneMap *pZoneMap);
void mission_MapLoad(bool bFullInit);
void mission_MapUnload(void);

extern bool g_bHasMadLibsMissions;

