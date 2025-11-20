#ifndef QUEUE_COMMON_STRUCTS_H
#define QUEUE_COMMON_STRUCTS_H

#pragma once
GCC_SYSTEM

#include "Message.h"
#include "referencesystem.h"

#include "autogen/PvPGameCommon_h_ast.h"

typedef struct CritterFaction CritterFaction;
typedef struct DisplayMessage DisplayMessage;
typedef struct Entity Entity;
typedef struct Expression Expression;
typedef struct QueueGameSetting QueueGameSetting;
typedef struct QueueMember QueueMember;
typedef U32 ContainerID;

AUTO_ENUM;
typedef enum QueueCannotUseReason
{
	QueueCannotUseReason_None = 0,
	QueueCannotUseReason_LevelTooHigh,
	QueueCannotUseReason_LevelTooLow,
	QueueCannotUseReason_SideKicking,
	QueueCannotUseReason_InvalidAffiliation,
	QueueCannotUseReason_AffiliationRequired,
	QueueCannotUseReason_GroupRequirements,
	QueueCannotUseReason_MissionRequirement,
	QueueCannotUseReason_Requirement,
	QueueCannotUseReason_LeaverPenalty,
	QueueCannotUseReason_MemberPrefs,
	QueueCannotUseReason_RandomClosed,
	QueueCannotUseReason_Cooldown,
	QueueCannotUseReason_ClassRequirements,
	QueueCannotUseReason_ActivityRequirement,
	QueueCannotUseReason_EventRequirement,
	QueueCannotUseReason_RequiresGuild,
	QueueCannotUseReason_RequiresUnteamed,
	QueueCannotUseReason_MixedTeamAffiliation,
	QueueCannotUseReason_GearRating,
	QueueCannotUseReason_Other,
} QueueCannotUseReason;


// This is used to keep track of the player's current state in the queue
AUTO_ENUM;
typedef enum PlayerQueueState
{
	PlayerQueueState_None,			// Not in the queue
	PlayerQueueState_InQueue,		// Normal state; waiting for an instance to open
	PlayerQueueState_Invited,		// Player is invited to this queue - it is a private instance
	PlayerQueueState_Offered,		// Queue is ready; sent a message to the player
	PlayerQueueState_Delaying,		// Player doesn't want to go right now
	PlayerQueueState_Accepted,		// Player has agreed to go to the queue map
	PlayerQueueState_Countdown,		// Player is waiting for the launch countdown
	PlayerQueueState_InMap,			// Player is on the queue map
	PlayerQueueState_WaitingForTeam,// Player queued with a team, and is waiting for a response from all of their servers
	PlayerQueueState_Limbo,			// After the queue server loads, players may be placed in limbo until they re-integrate
	PlayerQueueState_Exiting,		// Player is leaving the queue (transacted)
} PlayerQueueState;

AUTO_ENUM;
typedef enum QueueMapState
{
	kQueueMapState_None,
	kQueueMapState_StartingUp,
	kQueueMapState_Open,			// The map has been started, we are accepting member assignments. Set from aslQueue_tr_OpenNewMap
	kQueueMapState_LaunchPending,
	kQueueMapState_LaunchCountdown,
	kQueueMapState_Launched,
	kQueueMapState_Active,
	kQueueMapState_Finished,
	kQueueMapState_Limbo,			//After startup, needs the map to "check-in" before we can look at it some more
	kQueueMapState_Destroy,
} QueueMapState;

AUTO_STRUCT;
typedef struct QueueGroupDef
{
	DisplayMessage DisplayName;					AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))
	// Display name of this group (for the UI)
	S32 iMin;
	// Minimum number in this group
	S32 iMinTimed;
	// The minimum this group requires if it has gone into overtime
	S32 iMax;
	// Maximum number in this group
	S32 iMaxTimed;
	// The maximum this group will allow if it has gone into overtime (0 means use iMax)
	S32 iAutoBalanceMin;
	// The absolute minimum allowed during matching when auto-balancing (0 means use iMinTimed)
	const char* pchAffiliation;					AST(NAME(Allegiance, Affiliation) POOL_STRING)
	// Required allegiance for this group
	const char* pchSpawnTargetName;
	// Spawn point
	Expression* pRequires;						AST(NAME(Requires), REDUNDANT_STRUCT(pRequires, parse_Expression_StructParam), LATEBIND)
	// Optional requires expression for this group.
	//  NOTE!!! This is a highly problematic field at the moment since the QueueServer cannot evaluate it properly.
	//   It currently only gates GameServer-side checks on the availability of queues. Long term we should consider folding this into the affiliation
	//   system, or possibly make the QueueServer not need to know about Allegiance and Requires directly (perhaps we could generate a list of valid
	//   groupDefs for a member at Join time, and all checks would be directly on this list).
	REF_TO(CritterFaction) hFaction;			AST(NAME(Faction))
	// The faction to set so that groups can fight each other
	const char** ppchTeamFXName;					AST(NAME(TeamFX))
	// The fx to be applied to everyone on this team

	bool bUseGroupPrivateSettings;
	// This queue can have private instances, in these cases use the following overrides

	S32 iPrivateMinGroupSize;

	S32 iPrivateOverTimeSize;

} QueueGroupDef;

AUTO_STRUCT;
typedef struct QueueMatchMember
{
	ContainerID uEntID;
	// Member entity container ID
	ContainerID uTeamID;
	// Member's team container ID

	S32 iGroupRole;		// Eventually should be turned to an enum. And/Or governed by game. For now it's just an int that is defined by convention.
	S32 iGroupClass;	// Eventually should be turned to an enum. And/or governed per game. For now it's just an int that is defined by convention.
	
} QueueMatchMember;

AUTO_STRUCT;
typedef struct QueueGroup
{
	QueueMatchMember** eaMembers;
	// The Queue member ContainerID

	ContainerID* puiInMapTeamIDs;	AST(NO_NETSEND)
	// Unique list of team IDs of teams that are in map and thus aren't in eaMembers

	S32 iGroupIndex;				AST(DEFAULT(-1))
	// The index of the group, 0 based

	S32 iGroupSize;
	// The size of the group, basically eaiSize(piMembers);

	S32 iLimboCount;
	// The number of members in limbo

	QueueGroupDef *pGroupDef;
	// The def of the group

	U32 uiTeamUpID;
} QueueGroup;


AUTO_STRUCT; 
typedef struct QueueMatch
{
	QueueGroup	**eaGroups;
	//The groups the match maker split them into

	U32			iMatchSize;
	//How many players are in the match (may differ from the group size totals for balancing reasons)

	U32			bAllGroupsValid : 1;
	//Are all the groups' sizes valid

	F32			fFitness;
	//Used to differentiate between matches of better fitness

	U64			uUpdateID;
	// Each time an update is sent the ID is incremented. This is used by gslQueue_UpdateQueueMatch to prevent overwrites

	bool		bHasNewGroupData;
	// When the match groups are updated, this gets set. So we know to process things in gslTeam
} QueueMatch;

AUTO_STRUCT;
typedef struct QueueMapMatch
{
	S64 iMapKey;
		// The map key

	QueueMatch* pMatch;
		// Players being added to the map
} QueueMapMatch;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(bPrivate) AST_IGNORE(iMapIndex) AST_IGNORE(bRematch) ;
typedef struct QueueInstanceParams
{
	const S32 iLevelBandIndex;				AST(PERSIST SUBSCRIBE)
		//Index into the QueueDef eaLevelBands array //TODO(MK): use a key instead of an index

	STRING_POOLED pchMapName;				AST(PERSIST SUBSCRIBE POOL_STRING)
		//The map to be used by this instance

	const U32 eGameType;					AST(PERSIST SUBSCRIBE SUBTABLE(PVPGameTypeEnum))
		//The Game type to be used by this instance

	const U32 uiOwnerID;					AST(PERSIST SUBSCRIBE)
		//Non-zero if this is a private game - this is the container ID of the owner entity

	const U32 uiGuildID;					AST(PERSIST SUBSCRIBE)
		//Non-zero if this queue is exclusive to a specific guild

	CONST_STRING_MODIFIABLE pchPrivateName;	AST(PERSIST SUBSCRIBE)
		// The identifier for a private instance

	CONST_STRING_MODIFIABLE pchPassword;	AST(PERSIST SUBSCRIBE SERVER_ONLY)
		// The password for a private game

} QueueInstanceParams;


AUTO_STRUCT AST_CONTAINER;
typedef struct QueueGameSetting
{
	CONST_STRING_MODIFIABLE pchQueueVarName;	AST(PERSIST SUBSCRIBE)
		// The name of the QueueVariable that this is altering
	const S32 iValue;							AST(PERSIST SUBSCRIBE)
		// The altered QueueVariable value
} QueueGameSetting;

AUTO_STRUCT;
typedef struct QueueGameSettings
{
	QueueGameSetting** eaSettings;
} QueueGameSettings;

AUTO_STRUCT;
typedef struct QueueIntArray
{
	INT_EARRAY piArray;
} QueueIntArray;

AUTO_STRUCT;
typedef struct QueueInviteInfo
{
	ContainerID uEntID;
	ContainerID uTeamID;
	S32 iLevel;
	S32 iRank;
	const char* pchAffiliation;
} QueueInviteInfo;

AUTO_STRUCT;
typedef struct QueueFailRequirementsData
{
	S32 eMapType;
	S32 eReason;
	U32 bFailsAnyReqs : 1; AST(SERVER_ONLY DEFAULT(1))
	U32 bFailsAllReqs : 1; AST(SERVER_ONLY)
} QueueFailRequirementsData;

AUTO_STRUCT;
typedef struct QueuePenaltyCategoryData
{
	S32 eCategory; AST(KEY)
	U32 uPenaltyEndTime;
} QueuePenaltyCategoryData;

AUTO_STRUCT;
typedef struct PlayerQueuePenaltyData
{
	QueuePenaltyCategoryData** eaCategories;
} PlayerQueuePenaltyData;

//  Info for when a player is attached to a queue.
AUTO_STRUCT;
typedef struct QueueInstantiationInfo
{
	const char* pchQueueDef; AST(POOL_STRING)
	const char* pchMapName; AST(POOL_STRING)
	const char* pchSpawnName; AST(POOL_STRING)
	S64 iMapKey;   		// If the player can return to a queue map, then the iMapKey will be non-zero
	U32 uInstanceID;
	bool bOnCorrectMapPartition; // If we are on the Map/Partition represented by the Queue Instance. Used to determine if we can/need to "return to instance"
	int iGroupIndex;			 // Which group index are we in. This should theoretically be expanded to include more info from the GroupDef for UI purposes.
} QueueInstantiationInfo;

AUTO_STRUCT;
typedef struct PlayerQueueMap
{
	S64 iKey;						AST(KEY)
	QueueMapState eMapState;
	U32 uMapLaunchTime;
	S32* piGroupPlayerCounts;
	U32 bDirty : 1;					NO_AST
} PlayerQueueMap;

AUTO_STRUCT;
typedef struct PlayerQueueMember
{
	U32	uiID;
	U32 uiTeamID;
	S64 iMapKey;
	S64 iJoinMapKey;
	S32 iGroupIndex;
	S32 iLevel;
	S32 iRank;
	S32 eState;
	REF_TO(Entity) hEntity;			AST(COPYDICT(ENTITYPLAYER))
	U32 bDirty : 1;					NO_AST
} PlayerQueueMember;

AUTO_STRUCT;
typedef struct PlayerQueueInstance
{
	U32 uiID;						AST(KEY)
	S64 iOfferedMapKey;
	U32 uiOrigOwnerID;
	U32 uSecondsRemaining;
	U32	uTimelimit;
	U32 uAverageWaitTime;
	S32 iGroupIndex;				AST(DEFAULT(-1))
	S32* piGroupPlayerCounts;
	PlayerQueueState eQueueState;
	QueueGameSetting** eaSettings;
	QueueInstanceParams* pParams;

	PlayerQueueMap** eaPlayerQueueMaps;
	PlayerQueueMember** eaMembers;
	U32 bOvertime : 1;
	U32 bNewMapLoading : 1;
	U32 bHasPassword : 1;
	U32 bIgnoreLevelRestrictions : 1; AST(SERVER_ONLY)
} PlayerQueueInstance;

AUTO_STRUCT AST_CONTAINER;
typedef struct QueueMemberPrefs
{
	const F32 fCurrentRank;				AST(PERSIST SUBSCRIBE)

	CONST_INT_EARRAY ePreferredGameTypes;		AST(PERSIST SUBSCRIBE)
	CONST_INT_EARRAY iPreferredMaps;			AST(PERSIST SUBSCRIBE)

	bool bFoundMatch;
}QueueMemberPrefs;

// Criteria used to determine how a member is joining the queue.
//   for restriction checking and such. Used to be passed as separate parameters but
//   we exceeded the number of parameters limit
AUTO_STRUCT;
typedef struct QueueMemberJoinCriteria
{
	S32 iLevel;
	S32 iRank;
	const char* pchAffiliation;  AST(POOL_STRING)
	S32 iGroupRole;		// Eventually should be turned to an enum. And/Or governed by game. For now it's just an int that is defined by convention.
	S32 iGroupClass;	// Eventually should be turned to an enum. And/or governed per game. For now it's just an int that is defined by convention.
} QueueMemberJoinCriteria;



#endif //QUEUE_COMMON_STRUCTS_H
