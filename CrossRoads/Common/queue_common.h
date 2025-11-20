/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef QUEUE_COMMON_H
#define QUEUE_COMMON_H

#pragma once
GCC_SYSTEM

#include "Combat/CombatEnums.h"
#include "stdtypes.h"
#include "message.h"
#include "FolderCache.h"
#include "ResourceManager.h"
#include "GlobalEnums.h"
#include "ChoiceTable_common.h"
#include "ActivityCommon.h"
#include "queue_common_structs.h"
#include "PvPGameCommon.h"

typedef struct CachedTeamStruct CachedTeamStruct;
typedef struct CharacterClassRef CharacterClassRef;
typedef struct Entity Entity;
typedef struct ExprContext ExprContext;
typedef struct MissionDef MissionDef;
typedef struct Player Player;
typedef struct PlayerQueueInfo PlayerQueueInfo;
typedef struct PlayerQueueMap PlayerQueueMap;
typedef struct QueueGroupDef QueueGroupDef;
typedef struct QueueInstanceParams QueueInstanceParams;
typedef struct TeamMember TeamMember;
typedef struct WorldVariable WorldVariable;
typedef struct NOCONST(QueueMember) NOCONST(QueueMember);
typedef struct NOCONST(QueueInfo) NOCONST(QueueInfo);
typedef struct NOCONST(QueueInstance) NOCONST(QueueInstance);
typedef struct NOCONST(QueueInstanceParams) NOCONST(QueueInstanceParams);
typedef struct NOCONST(QueueMap) NOCONST(QueueMap);
typedef struct ChoiceTable ChoiceTable;

typedef struct PVPGameTypeParams PVPGameTypeParams;
typedef struct QueueRewardTable QueueRewardTable;
typedef struct QueueGameType QueueGameType;
typedef struct CTF_FlagPowerStack CTF_FlagPowerStack;
typedef struct CharacterClass CharacterClass;

typedef enum PVPGameType PVPGameType;

extern StaticDefineInt CharClassCategoryEnum[];

AUTO_ENUM;
typedef enum QueueVoteType
{
	kQueueVoteType_Concede = 0,
	kQueueVoteType_VoteKick,
} QueueVoteType;

AUTO_ENUM;
typedef enum QueuePrivateNameInvalidReason
{
	kQueuePrivateNameInvalidReason_None = 0,
	kQueuePrivateNameInvalidReason_MinLength,
	kQueuePrivateNameInvalidReason_MaxLength,
	kQueuePrivateNameInvalidReason_Profanity,
} QueuePrivateNameInvalidReason;

AUTO_ENUM;
typedef enum QueueMissionReq
{
	kQueueMissionReq_None = 0,
	kQueueMissionReq_Complete = (1<<0),
	kQueueMissionReq_Available = (1<<1),
	kQueueMissionReq_CanAccept = (1<<2),
} QueueMissionReq;

AUTO_ENUM;
typedef enum QueueJoinFlags
{
	kQueueJoinFlags_None = 0,
	kQueueJoinFlags_JoinNewMap = (1 << 0),
	kQueueJoinFlags_AutoAcceptOffers = (1 << 1),
	kQueueJoinFlags_IgnoreLevelRestrictions = (1 << 2),
} QueueJoinFlags;

AUTO_ENUM;
typedef enum QueueVariableType
{
	kQueueVariableType_WorldVariable = 0,
	kQueueVariableType_GameInfo = 1,
} QueueVariableType;

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pQueueCategories);
typedef enum QueueCategory
{
	kQueueCategory_None, ENAMES(None)
	// Data defined...
} QueueCategory;

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pQueueRewards);
typedef enum QueueReward
{
	kQueueReward_Unspecified, ENAMES(Unspecified)
	kQueueReward_FIRST_DATA_DEFINED, EIGNORE
} QueueReward;

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pQueueDifficulty);
typedef enum QueueDifficulty
{
	kQueueDifficulty_None, ENAMES(None)
	kQueueDifficulty_FIRST_DATA_DEFINED, EIGNORE
} QueueDifficulty;

AUTO_STRUCT;
typedef struct QueueCategoryData
{
	const char* pchName; AST(STRUCTPARAM)
		// Internal name of the category

	DisplayMessage msgDisplayName; AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))  
		// Category Display name

	const char* pchIconName; AST(NAME(Icon) POOL_STRING)
		// Category Icon

	U32 uRandomActiveCount;	AST(NAME(RandomActiveCount))
		// The amount of queues that can be active at a time in a random queue list
		// 0 turns off random queues for this category

	U32 uRandomActiveTime;	AST(NAME(RandomActiveTime))
		// The amount of time the queue stays active before being replaced by a new queue

	U32 uRandomActiveInstanceMax;	AST(NAME(RandomActiveInstanceMax))
		// The maximum amount of instances by one random queue, 0 means just run out the timer

	U32 uPenaltyDuration; AST(NAME(PenaltyDuration))
		// Penalty Duration

	QueueCategory eCategory; NO_AST
		// Filled in at load-time
} QueueCategoryData;

AUTO_STRUCT;
typedef struct QueueCategories
{
	QueueCategoryData** eaData; AST(NAME(Category))
} QueueCategories;

// Concede information (stored per group)
AUTO_STRUCT;
typedef struct QueueGroupConcedeData
{
	S32 iGroupIndex;
	U32 uTimer;
	U32 uNextAllowedConcedeTime;
	ContainerID* puiEntIDs;
	bool bConceded;
	bool bAutoConcede;
} QueueGroupConcedeData;

// Vote-kick data (stored per group)
AUTO_STRUCT;
typedef struct QueueGroupKickData
{
	S32 iGroupIndex;
	U32 uTimeout;
	U32 uNextAllowedVoteKickTime;
	U32 uVoteKickPlayerID;
	ContainerID* puiEntIDs;
} QueueGroupKickData;

// Only supports integers at the moment
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct QueueSettingVarData
{
	DisplayMessage msgDisplayName;	AST(NAME("DisplayMessage") STRUCT(parse_DisplayMessage))
		// Display name to show on the UI
	int iMinValue;					AST(NAME("MinValue"))
		// The min value for this setting
	int iMaxValue;					AST(NAME("MaxValue"))
		// The max value for this setting
	int iRewardMinValue;			AST(NAME("RewardMinValue"))
		// The min value in order for this setting to qualify for rewards
} QueueSettingVarData;

// Defines which game settings should be applied to which maps,
// and can optionally allow players to alter the variable in private games
// NOTE: FieldName may only modify an integer, for now
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct QueueVariableData
{
	const char* pchName;					AST(STRUCTPARAM REQUIRED)
		// The internal name of this queue variable
	
	QueueVariableType eType;				AST(NAME("Type"))
		// The type of queue variable

	const char* pchFieldName;				AST(NAME("FieldName"))
		// The field to modify

	const char** ppchVarNames;				AST(NAME("WorldVariableName") POOL_STRING)
		// A list of variables
	
	const char** ppchCopyVarNames;			AST(NAME("CopyVariable") POOL_STRING)
		// Copies the value for this variable
	
	const char* pchStringValue;				AST(NAME("StringValue"))
		// Override the string value for the list of world variables

	const char** ppchCustomMapTypes;		AST(NAME("ApplyToCustomMapType", "ApplyToMapType"))
		// The custom map types to apply these variables to

	QueueSettingVarData* pSettingData;		AST(NAME("SettingData","ChallengeData"))
		// If these vars can be modified in challenges
} QueueVariableData;

AUTO_STRUCT;
typedef struct QueueVariables
{
	WorldVariable** eaWorldVars;		AST(NAME(WorldVars) NO_INDEX)
		// Override mission reward map vars
	QueueVariableData** eaQueueData;	AST(NAME(QueueVarData))
		// The variables that can be modified in private matches
} QueueVariables;

AUTO_STRUCT;
typedef struct QueueTrackedEvent
{
	const char *pchMapValue;			AST(STRUCTPARAM)
	const char *pchEventString;			AST(STRUCTPARAM)
}QueueTrackedEvent;

AUTO_STRUCT;
typedef struct QueueMemberForceLeave
{
	ContainerID uEntID; AST(KEY)
	U32 uTimeoutTime;
} QueueMemberForceLeave;

AUTO_STRUCT;
typedef struct QueueLevelBand
{
	S32 iMinLevel;					AST(NAME(MinLevel))
		// 0 means no minimum
	S32 iMaxLevel;					AST(NAME(MaxLevel))
		// 0 means no maximum
	BolsterType eBolsterType;		AST(NAME(BolsterType) DEFAULT(kBolsterType_None))
		// What type of bolster this is
	S32 iBolsterLevel;				AST(NAME(BolsterLevel))
		// The level the players are bolstered to.  0 means no bolstering effect - NOTE: Removes sidekicking
	S32 iOverrideMapLevel;			AST(NAME(MapLevel))
		// Sets the level for encounters (overrides iOverrideMapLevel on the QueueDef)
	QueueVariables VarData;			AST(EMBEDDED_FLAT)
		// WorldVariables and QueueVariableData
} QueueLevelBand;

AUTO_STRUCT;
typedef struct QueueLevelBandEditorData
{
	S32 iLevelBandIndex;		AST(NAME(LevelBandIndex))
	QueueLevelBand* pLevelBand;	AST(NAME(LevelBand))
	WorldVariable* pWorldVar;	AST(NAME(WorldVar))
} QueueLevelBandEditorData;


// A structure to organize display information about maps
AUTO_STRUCT;
typedef struct QueueCustomMapData
{
	const char* pchName;					AST(STRUCTPARAM REQUIRED)
		// The internal name of the map type
	const char** ppchMaps;					AST(NAME(MapName))
		// The maps associated with this map type
	U32* puiPVPGameModes;					AST(NAME(GameMode) SUBTABLE(PVPGameTypeEnum))
		// The possible PVP game modes
	const char* pchIcon;					AST(NAME(Icon))
		// The icon for this map type
	DisplayMessage msgDisplayName;			AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))
		// Display name for the map type
} QueueCustomMapData;

AUTO_STRUCT;
typedef struct QueueMatchMakingRules
{
	S32 iDefaultLevelRange;
		// 
	F32 fRankRange;
		// 
	F32 fRankTimeOut;
	F32 fLevelTimeOut;
	F32 fMapTimeOut;
	F32 fRankMax;
	S32 iLevelMax;

	const char *pchMatchMakingLeaderboard;
}QueueMatchMakingRules;

AUTO_STRUCT;
typedef struct QueueCooldownDef
{
	const char* pchName;			AST(STRUCTPARAM KEY POOL_STRING)
		// The internal name
	U32 uCooldownTime;				AST(NAME(CooldownTime))
		// Cooldown seconds
	DisplayMessage msgDisplayName;	AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))
	// Display name for the CooldownDef
	DisplayMessage msgDescription;	AST(NAME(Description) STRUCT(parse_DisplayMessage))
	// Description for the CooldownDef
} QueueCooldownDef;

AUTO_STRUCT;
typedef struct QueueCooldownDefs
{
	QueueCooldownDef** eaCooldowns; AST(NAME(CooldownDef))
} QueueCooldownDefs;



AUTO_STRUCT;
typedef struct QueueMapSettings
{
	QueueVariables VarData;			AST(EMBEDDED_FLAT)
	//World Variable data that isn't specific to the level band

	S32 iOverrideMapLevel;
	// Overrides the map level.  Used to set the level of encounters

	S32 iMapDifficulty;
	// Sets the map difficulty.

	BolsterType eBolsterType;	AST(NAME(BolsterType) DEFAULT(kBolsterType_SetTo))
	// The bolster type. Can be overridden by the level band.

	ZoneMapType eMapType;			AST(SUBTABLE(ZoneMapTypeEnum) DEFAULT(ZMTYPE_PVP))
	// This is the type of map it is spawning.  Generally PvP

	U32 bBolsterToMapLevel : 1;
	// Automatically bolster to the specified map level

	U32 bCheckOffersBeforeMapLaunch : 1;
	// Make sure that enough offers are accepted before launch so that a map doesn't with unbalanced teams


}QueueMapSettings;

AUTO_STRUCT;
typedef struct QueueSettings
{

	S32 iLeaverPenaltyMinGroupMemberCount; AST(NAME(LeaverPenaltyMinGroupMemberCount))
		// If there are less than this many players remaining on a group, disable the leaver penalty

	U32 uPlayerLimboTimeoutOverride; AST(NAME(PlayerLimboTimeout))
	// Override the default player limbo/disconnect timeout (in seconds)

	U32 uOverridePenaltyDuration;	AST(NAME(PenaltyDuration))
	// If set, overrides the penalty duration on the QueueConfig

	U32 uMissionReturnLogoutTime; AST(NAME(MissionReturnLogoutTime, KickLogoutTime, QueueKickLogoutTime))
	// Overrides the QueueConfig setting. 
	// This no longer applies to the KickLogoutTime, but keeps alternate names for backwards compatibility.

	U32 bPublic			: 1;
// Whether the player can see this queue and join it of their own volition

	U32 bAlwaysCreate	: 1;
	// If set, this queue will always create all possible instances and never destroy them

	U32 bDebug			: 1;
	// If set, this queue only appears to players with access level and debugging set

	U32 bStayInQueueOnMapLeave : 1;
	// Leaving a queue map will not remove you from the queue

	U32 bDestroyEmptyMaps : 1;			AST(DEFAULT(1))
		// Whether or not empty maps should be destroyed

	U32 bRandomMap		: 1;
	// If set, the map is randomized from the list of maps (ppchMapNames)

	U32 bSplitTeams		: 1;
	// If set, a team will get split into separate groups. (while still going to the same map instance)

	U32 bIgnoreLevelBandsForTeams : 1;
	// When joining as a team, teammates can join any other teammate's level band without restriction

	U32 bEnableLeaverPenalty : 1;
	// Whether or not the leaver penalty is enabled for this queue

}QueueSettings;

AUTO_STRUCT;
typedef struct QueueMapRules
{
	PVPGameRules QGameRules;		AST(EMBEDDED_FLAT)
	// Game rules basic and
	// Additional rules by each type of match, note that by moving the rules into here there each type of match can only have one set of rules.

	QueueMatchMakingRules *pMatchMakingRules;
	// If this structure exists, enable matchmaking rules

	S32 iMaxLocalTeamsPerGroup; AST(NAME(MaxLocalTeamsPerGroup))
		// Put a hard-cap on the number of local teams allowed per group

	REF_TO(CritterFaction) hNeutralFaction;	AST(NAME(NeutralFaction))
	// The faction to set so that all players in a game are neutral to each other

	U32 bEnableAutoTeam : 1;
	// Enables the auto-teaming in pvp/queue pve maps.

	U32 bEnableTeamUp : 1;
	// Enables the team up system in pvp/queue pve maps.

	U32 bAllowConcede : 1;				AST(NAME(AllowConcede))
	// Allow players to concede matches

	U32 bAllowVoteKick : 1;				AST(NAME(AllowVoteKick))
	// Allow players to initiate votes to kick players

	U32 bIgnoresPVPAttribMods : 1;		AST(DEFAULT(1))
	// If set, the map launched will ignore attrib mods set to be ignored during pvp matches

	U32 bChallengeMatch : 1;			AST(NAME(ChallengeMatch, PrivateMatch))
	// If set, this is a queue that can only be created as a private match that is owned by a player

	U32 bDisableAutoBalance : 1;		AST(DEFAULT(1))
	// If set, this will prevent groups from being auto-balanced

}QueueMapRules;

AUTO_STRUCT;
typedef struct QueueLimitations
{
	U32 iMinMembersAllGroups;
	// Minimum number of members from all groups required to start the game

	U32 iMaxTimeToWait;
	// If there have been people in the queue longer than this time,
	//	it lowers the minimum members required to spawn a map to the SUM
	//  of all iMinTimed of the groups.

	U32 uJoinTimeLimit;				AST(NAME(JoinTimeLimit))
		// The latest time (in seconds) that a player could possibly join a game in this queue


	// private info

	U32 iPrivateMaxWaitTime;
	U32 iPrivateMinMembersAllGroups;

	U32 bUsePrivateSettings : 1;
	// This queue can have private instances, in these cases use the following overrides

}QueueLimitations;


AUTO_STRUCT;
typedef struct QueueRequirements
{
	const char* pchRequiredActivity; AST(NAME(RequiredActivity) POOL_STRING)
		// The activity required to be active in order to join this queue

	const char* pchRequiredEvent;	AST(NAME(RequiredEvent) POOL_STRING)
	// The event required to be active in order to join this queue

	Expression* pRequires;			AST(NAME(Requires), REDUNDANT_STRUCT(pRequires, parse_Expression_StructParam), LATEBIND)
	// The requirement to use this queue

	REF_TO(MissionDef) hMissionRequired;	AST(NAME(RequiredMission))
	// This queue requires the mission def to join this queue

	QueueMissionReq eMissionReqFlags;		AST(NAME(MissionReqFlags) FLAGS)
	// What state the mission requirement can be in.  Complete, available, either?

	CharacterClassRef** ppClassesRequired;	AST(NAME(ClassRequired))
	// The classes required

	S32* piClassCategoriesRequired;			AST(NAME(ClassCategoryRequired) SUBTABLE(CharClassCategoryEnum))
	// The class categories required

	U32 uRequiredGearRating;				AST(NAME(RequiredGearRating))
	// (Note, this works independently of the level requirements which are handled in LevelBands)
	// Run the GearRatingCalcExpr specified in the QueueConfig on the Ent and compare it against this to determine eligibility

	U32 bMissionReqNoAccess : 1;
	// If set, the mission requirement prevents all access to the queue (otherwise you can still team queue to get in)

	U32 bRequireSameGuild : 1;				AST(NAME(RequireSameGuild, RequireGuild))
	// If set, require that all instances of this queue belong to a specific guild, and that all members of the queue are part of this guild

	U32 bRequireAnyGuild : 1;				AST(NAME(RequireAnyGuild))
	// If set, the player must be in a guild to join this queue, but not all members are required to share the same guild

	U32 bUnteamedQueuingOnly : 1;		// Do not allow teams to queue for this queue. Do not allow queueing if on a team.

}QueueRequirements;

AUTO_STRUCT;
typedef struct QueueMapInfo
{
	const char** ppchMapNames;		AST(NAME(MapName,pchMapName))
	// A list of maps that have no map type information associated with them
	// This is deprecated, please use eaCustomMapTypes instead

	REF_TO(ChoiceTable) hMapChoiceTable;	AST(NAME(MapChoiceTable))
	// A ChioceTable to use to determine the map to load

	const char* pchTableEntry;				AST(NAME(TableEntry))
	// The entry in the table to get the map from

	QueueCustomMapData** eaCustomMapTypes; AST(NAME(QueueCustomMapData, QueueCustomMapType, QueueMapType))
	// A list of custom map types, each of which contains a list of maps

}QueueMapInfo;

AUTO_STRUCT AST_IGNORE(MinLevel) AST_IGNORE(MaxLevel) AST_IGNORE(BolsterLevel);
typedef struct QueueDef
{
	const char* pchName;			AST(KEY STRUCTPARAM POOL_STRING)
		// The name of the queue

	const char *pchFilename;		AST(SERVER_ONLY CURRENTFILE)
		// File loaded from

	const char *pchScope;			AST(SERVER_ONLY, POOL_STRING)
		//The scope of the queue

	const char* pchIcon;			AST(POOL_STRING)
	//The icon name

	DisplayMessage displayNameMesg;	AST(STRUCT(parse_DisplayMessage))
	// The name of the queue

	DisplayMessage descriptionMesg;	AST(STRUCT(parse_DisplayMessage))
	// The description of the queue

	QueueSettings Settings;			AST(EMBEDDED_FLAT)
	// General queue settings

	QueueMapRules MapRules;			AST(EMBEDDED_FLAT)
	// map rules

	QueueMapSettings MapSettings;	AST(EMBEDDED_FLAT)
	// The map settings used for this queue

	QueueLimitations Limitations;	AST(EMBEDDED_FLAT)
	// limitations on the queue

	const char* pchCooldownDef;		AST(NAME(CooldownDef) POOL_STRING)
	// The cooldown to apply when this queue is completed

	QueueRequirements Requirements;	AST(EMBEDDED_FLAT)
	// requirements for the queue

	QueueMapInfo QueueMaps;			AST(EMBEDDED_FLAT)
	// information about queue maps

	QueueGroupDef** eaGroupDefs;
		// Defines how the PvP groups are aligned against each other

	QueueCategory eCategory;		AST(NAME(Category))
		// A category assigned to this QueueDef for use by the UI

	QueueReward eReward;			AST(NAME(Reward))
		// In general the rewards this QueueDef will grant, for use by the UI

	QueueDifficulty eDifficulty;	AST(NAME(Difficulty))
		// A difficulty assigned to this QueueDef for use by the UI

	S32 iExpectedGameTime;			AST(NAME(ExpectedGameTime))
		// The number of seconds the game is expected to last

		// ---- Level Gating ----
	QueueLevelBand** eaLevelBands;	AST(NAME(LevelBand))
	// (Note, we also have a non-banded GearRating requirement that is queueDef global and is in the Requirements struct)

	QueueTrackedEvent **ppTrackedEvents; AST(NAME(TrackedEvent))
		//Events to be tracked for points
		// Note that the queueconfig will have defaults for these as they are hard to create

	EARRAY_OF(QueueRewardTable) eaRewardTables;	AST(NAME(QueueRewardTables))
		// reward table info for each game type
		// Note queueconfig has a list for each game type that are the defaults

	U32 bEnableStrictTeamRules : 1;
	
	// Editor data
	QueueLevelBandEditorData** eaEditorData; AST(NAME(EditorData) NO_TEXT_SAVE)


} QueueDef;


AUTO_STRUCT;
typedef struct QueueDefGen
{
	QueueDef sBaseDef; AST(EMBEDDED_FLAT)
	REF_TO(QueueDef) hBaseDef;			AST(NAME(BaseDef))
	REF_TO(ChoiceTable) hChoiceTable;	AST(NAME(ChoiceTable))

	bool bMarkAsPublic;					AST(NAME(MarkAsPublic))
	QueueCategory eCategoryChange;		AST(NAME(QueueCategoryChange) SUBTABLE(QueueCategoryEnum))
}QueueDefGen;

AUTO_STRUCT;
typedef struct QueueDefGenContainer
{
	QueueDefGen **ppDefs;	AST(NAME(QueueGen))
}QueueDefGenContainer;

AUTO_STRUCT;
typedef struct QueueDefRef
{
	REF_TO(QueueDef) hDef;	AST(KEY)		
}QueueDefRef;

AUTO_STRUCT;
typedef struct ActiveRandomQueueDef
{
	REF_TO(QueueDef) hDef;

	U32 uTimeExpire;
	U32 uInstancesRemaining;
}ActiveRandomQueueDef;

AUTO_STRUCT;
typedef struct RandomActiveQueueList
{
	QueueCategory eCategory;

	ActiveRandomQueueDef **ppActiveDefs;
	QueueDefRef **ppChoiceDefs;
}RandomActiveQueueList;

AUTO_STRUCT;
typedef struct RandomActiveQueues
{
	RandomActiveQueueList **ppLists;
}RandomActiveQueues;

AUTO_STRUCT AST_IGNORE(ShowClientsInvalidQueuesWithReason);
typedef struct QueueConfig
{
	U32 uMemberResponseTimeout;			AST(NAME(MemberResponseTimeout) DEFAULT(60))
	U32 uCheckOffersResponseTimeout;	AST(NAME(CheckOffersResponseTimeout) DEFAULT(30))
	U32 uPrivateResponseTimeout;		AST(NAME(PrivateResponseTimeout) DEFAULT(300))
	U32 uMemberDelayTimeout;			AST(NAME(MemberDelayTimeout) DEFAULT(180))
	U32 uMemberInviteTimeout;			AST(NAME(MemberInviteTimeout) DEFAULT(30))
	S32 iMaxQueueCount;					AST(NAME(MaxQueueCount) DEFAULT(3))
	U32 uMapLaunchCountdown;			AST(NAME(MapLaunchCountdown) DEFAULT(10))
	U32 uKickLogoutTime;				AST(NAME(KickLogoutTime) DEFAULT(10))
	U32 uMissionReturnLogoutTime;		AST(NAME(MissionReturnLogoutTime) DEFAULT(60))
	U32 uLeaverPenaltyDuration;			AST(NAME(LeaverPenaltyDuration, LeaverPenaltyTime) DEFAULT(900))
	U32 uMemberAcceptTimeout;			AST(NAME(MemberAcceptTimeout) DEFAULT(120))
	U32 uMemberAcceptValidateTimeout;	AST(NAME(MemberAcceptValidateTimeout) DEFAULT(240))
	U32 uMemberInMapTimeout;			AST(NAME(MemberInMapTimeout) DEFAULT(45))
	U32 uAutoConcedeTime;				AST(NAME(AutoConcedeTime) DEFAULT(10))
	U32 uManualConcedeTime;				AST(NAME(ManualConcedeTime) DEFAULT(30))
	U32 uConcedeRetryTime;				AST(NAME(ConcedeRetryTime) DEFAULT(60))
	F32 fConcedeVoteRatio;				AST(NAME(ConcedeVoteRatio) DEFAULT(1.0))
	U32 uMaxPasswordLength;				AST(NAME(MaxPasswordLength) DEFAULT(16))
	U32 uAutoAcceptTime;				AST(NAME(AutoAcceptTime) DEFAULT(0)) //If this time passes as the player does not click "join", they will accept and join the match. 0 Turns the feature off
	U32 uMinGameInitTime;				AST(NAME(MinGameInitTime) DEFAULT(8.0))
	U32 uAllowConcedeStartTime;			AST(NAME(AllowConcedeStartTime) DEFAULT(120))
	U32 uAllowVoteKickStartTime;		AST(NAME(AllowVoteKickStartTime) DEFAULT(60))
	U32 uVoteKickTimeout;				AST(NAME(VoteKickTimeout) DEFAULT(30))
	U32 uVoteKickRetryTime;				AST(NAME(VoteKickRetry) DEFAULT(30))
	F32 fKickVoteRatio;					AST(NAME(KickVoteRatio) DEFAULT(1.0))

	const char* pchQueueGen;			AST(NAME(QueueGenName) DEFAULT("QueueUI"))
		// Hack to show a UIGen for PvP queues through doors
	const char* pchPvEQueueGen;			AST(NAME(PvEQueueGenName))
		// Hack to show a different UIGen for PvE queues through doors

	const char* pchFriendlyFX;			AST(NAME(FriendlyFX))
	const char* pchEnemyFX;				AST(NAME(EnemyFX))
		// the FX that is applied to entities when in PvP

	ZoneMapType* peAllowQueuingOnQueueMaps; AST(NAME(AllowQueuingOnQueueMap))
		// Specify which queue map types allow queuing while a player is on the map. Only applies to queue maps.

	S32 iPrivateQueueLevelLimit;			AST(NAME(PrivateQueueLevelLimit))
		// This is the number of levels that are allowed to be lower when creating a private queue

	// Flags
	U32 bProvideIndividualMapInfo : 1;	AST(NAME(ProvideIndividualMapInfo))
		// Provide information about each individual map for each queue instance
	U32 bEnableMissionReturnAtEnd : 1;	AST(NAME(EnableMissionReturnAtEnd))
		// When the match ends, create a mission return logout timer for every player
	U32 bEnableInMapPlayerChatUpdates : 1;	AST(NAME(EnableInMapPlayerChatUpdates))
		// System chat messages will be sent when players join and leave games
	U32 bEnableStrictTeamRules : 1;		AST(NAME(EnableStrictTeamRules))
		// Enables struct rules for teams joining queues, including no individual queuing, and team leader is only one able to queue entire team
	U32 bQueueJoinTimeIsExact : 1;		AST(NAME(QueueJoinTimeIsExact))
		// If bQueueJoinTimeIsExact is true then uJoinTimeLimit == 0 is treated as zero not infinite (Only Used for Champions Legacy. Could be removed)
	U32 bScoreboardRemovesInactivePlayerScores : 1; AST(NAME(ScoreboardRemovesInactivePlayerScores))
		// If true, the scoreboard will remove entries for entities that are no longer on the map
	U32 bDisbandAutoTeamsWhenMatchEnds : 1; AST(NAME(DisbandAutoTeamsWhenMatchEnds) DEFAULT(1))
		// If this is set, break apart auto-teams when the the queue map state is set to 'Finished'
	U32 bDisableJoinOrCreateOnAllQueueMaps : 1; AST(NAME(DisableJoinOrCreateOnAllQueueMaps))
		// This flag is deprecated, use AllowPlayerToQueueOnMap instead
	U32 bRemainInPrivateQueuesAfterMapFinishes : 1; AST(NAME(RemainInPrivateQueuesAfterMapFinishes))
		// If set, keep players in private queues after a map finishes so that they don't have to reform the group
	U32 bAlwaysSendPlayerMemberInfo : 1;
		// If set, players will always receive info about the other members in the queue instance
	U32 bAlwaysSendMessageAsNotification : 1;
		// If set, when the queue system sends a message to a client, always send as a notification instead of sometimes sending as a chat message
		// RRP: things should always sent as a notification- the other games would just need to fix up their notify definition. 
		//		this is just so the other games don't change their behavior
	U32 bStayInQueueOnMapLeave : 1;
		// Leaving a queue map will not remove you from the queue
	U32 bAllowQueueAbandonment : 1;
		// Enables "Abandon Queue" functionality. If on, replaces "Leave Team" functionality when attached to a queue instance. Will remove the player from the team and queue

	U32 bMaintainQueueTrackingOnMapFinish : 1;
		// Does not end queue tracking when a map finishes. 
		
	U32 bAutoJoinQueueRequiresPlayerAccept : 1;
		// The player still gets the accept dialog when joining a door queue

	U32 bKeepQueueInfo : 1;
		// if set then gslqueue will not make a character unimportant (i.e. delete playerqueue info). and if the player does not have queue info it will be refreshed

	U32 bDisablePrivateQueues : 1;					AST(NAME(DisablePrivateQueues))
		// Disable all private queues

	U32 bEnablePrivateQueueLevelLimit : 1;			AST(NAME(EnablePrivateQueueLevelLimit))
		// private queues are subject to PrivateQueueLevelLimit (see above)

	U32 bUseGuildAllegianceForAffiliation : 1;
		// For purposes of groupDef affiliation, use an entity's guild's allegiance instead of the entity's allegiance.
		//   All Enity->Affiliation checking should use queue_EntGetQueueAffiliation in queue_common.c

	U32 bRoleClassSmartGroups : 1;
		// Use role and class information to make teams.

	U32 bAutoReleaseOnFinishedMapExit : 1;
		// If a queue instance map is finished and no team members are on the map, automatically release the team from the queue instance.

	Expression* pGearRatingCalcExpr;	AST(NAME(GearRatingCalc), REDUNDANT_STRUCT(pGearRatingCalcExpr, parse_Expression_StructParam), LATEBIND)
	// The expression run to determine an entities gear rating. Compared against values set in QueueDef requirements.
	
} QueueConfig;

AUTO_STRUCT;
typedef struct QueueMemberTeam
{
	ContainerID iTeamID;		AST(KEY)
		// Which team containerID it is

	U32 iTeamSize;
		// The team size (doesn't count members on maps)
} QueueMemberTeam;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(hAllegiance) AST_IGNORE(iMapID) AST_IGNORE(iJoinMapID) AST_IGNORE(bJoinNewMap) AST_IGNORE(bAutoAcceptOffer);
typedef struct QueueMember
{
	const ContainerID iEntID;					AST(PERSIST SUBSCRIBE KEY)
		// The entity ID of the player who queued

	const PlayerQueueState eState;				AST(PERSIST SUBSCRIBE)
		//The state the player is in
	const U32 iStateEnteredTime;				AST(PERSIST SUBSCRIBE)
		// How long the player has been in this state

	const U32 iQueueEnteredTime;				AST(PERSIST SUBSCRIBE)
		// This is when you entered the queue (reset when every time to move to the "inqueue" state).
		//  If this time is after "now", then the member doesn't get participate in match making

	const S64 iMapKey;							AST(PERSIST SUBSCRIBE)
		// The map/partition the member is on or currently being offered

	const S64 iJoinMapKey;						AST(PERSIST SUBSCRIBE)
		// The map/partition the member wants to join

	const S32 iGroupIndex;						AST(PERSIST SUBSCRIBE DEFAULT(-1))
		// The index of the group (0 based) that the queue member is on

	const ContainerID iTeamID;					AST(PERSIST SUBSCRIBE)
		// Non-zero if the player queued in a team

	STRING_POOLED pchAffiliation;				AST(PERSIST SUBSCRIBE NAME(pchAllegiance) POOL_STRING)
		// The queue affiliation of the player (for allowing/disallowing on groups)

	const S32 iGroupRole;						AST(PERSIST SUBSCRIBE)
	const S32 iGroupClass;						AST(PERSIST SUBSCRIBE)
		// The role and class this player has joined as. These could be put in an enum. It is a fluid design concept
		//   now [21Feb13]. Let's stick with ints. 0 is STO (or undefined) Role: 1 is tank, 2 is healer, 3 is dps.
		// Class: 0 is undefined. Other classes are 1-5. We just need it to be diverse so it is not strictly-defined.
		// Set at join time.

	const S32 iLevel;							AST(PERSIST SUBSCRIBE)
		// The level of the player

	const S32 iRank;							AST(PERSIST SUBSCRIBE)
		// The officer rank of the player

	U32 iLastMapUpdate;
		//The last time a map updated this player, used for validation when "in-map"

	U32 iTeamSize;
		// A cached value of the team's size (doesn't count members on maps), 1 if not on a team

	U32 uFirstUpdateTimeInState;
		// The first time that the member was updated while in the current state

	const QueueJoinFlags eJoinFlags;			AST(PERSIST SUBSCRIBE)
		// Various flags set when the member joined this queue

	CONST_OPTIONAL_STRUCT(QueueMemberPrefs) pJoinPrefs;		AST(PERSIST SUBSCRIBE)
		// Various rules for joining preferences
} QueueMember;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(iMapID);
typedef struct QueueMap
{
	const S64 iMapKey;							AST(PERSIST SUBSCRIBE KEY)
		// MapID and PartitionID
	const U32 iMapCreateTime;					AST(PERSIST SUBSCRIBE)
		// The time the map was created
	const U32 iMapLaunchTime;					AST(PERSIST SUBSCRIBE)
		// The time the map was launched
	STRING_POOLED pchMapName;					AST(PERSIST SUBSCRIBE POOL_STRING)
		// The map description name used to create the map
	
	U32 uiMapCreateRequestID;
		// Whenever the queue server creates a map it uses a request ID
		//  This is the one used to request this specific map instance

	const QueueMapState eMapState;				AST(PERSIST SUBSCRIBE)
		// The state the map is in, determines when players can be added to the map

	const U32 iStateEnteredTime;				AST(PERSIST SUBSCRIBE)
		// When I last changed states

	U32 iLastServerUpdateTime;					AST(PERSIST SUBSCRIBE NO_TRANSACT)
		//Whenever the server checks in or updates its scoreboard, this gets updated

	WorldVariable **eaVariables;
		//The variables the map has

	U32 uOfferValidationStartTime;
		// When the map goes live, keep track of offer validation

	U32* eaOfferValidationMemberIDs;
		// The entity IDs of members that have gone through the current offer validation period

	U32 bDirty : 1;
		// The map is dirty, awaiting a transaction to complete
	U32 bGameServerDied : 1;
		// The controller informed us that the GameServer died, clean it up next tick

	U32 bPendingActive : 1;
		// The GameServer requested we become active. Will transition to active once we are launched.
	U32 bPendingFinished : 1;
		// The GameServer requested we Finish. Should only happen if g_QueueConfig.bMaintainQueueTrackingOnMapFinish is on.
	
} QueueMap;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(eaOrderedMembers);
typedef struct QueueInstance
{
	const U32 uiID;											AST(PERSIST SUBSCRIBE KEY)
		//The unique ID of this instance in the queue

	CONST_OPTIONAL_STRUCT(QueueInstanceParams) pParams;		AST(PERSIST SUBSCRIBE FORCE_CONTAINER)
		//Specific parameters set for this instance

	const U32 uOrigOwnerID;									AST(PERSIST SUBSCRIBE)
		//If this is set, then the current owner is not the original owner, and this is the original owner ID.

	CONST_EARRAY_OF(QueueGameSetting) eaSettings;			AST(PERSIST SUBSCRIBE FORCE_CONTAINER)
		//Modified aspects of the game for use in private games

	// - Map Information about new and previously launched maps
	
	CONST_EARRAY_OF(QueueMap)	eaMaps;				AST(PERSIST SUBSCRIBE)
		//The maps this queue has launched

	EARRAY_OF(QueueMap)	eaNewMaps;
		//These are the new maps being launched

	///////////////////////////////////////
	// - Queue membership information

	// We keep an Indexed list of QueueMembers in conjuction with an ordered list of IDs for performance reasons.
	// The GameServer does not need an ordered list. And subscribing to an unindexed array of QueueMember structs
	// causes huges diffs to be sent to every GameServer whenever an element is added or removed.

	CONST_EARRAY_OF(QueueMember) eaUnorderedMembers;	AST(PERSIST SUBSCRIBE SERVER_ONLY)
		// The members in the queue, indexed by EntityID.
	
	CONST_CONTAINERID_EARRAY eaOrderedMemberEntIds; AST(PERSIST NO_INDEX)
		// The members in the queue, ordered by when they came in. Stored as ContainerIDs which are the Index of eaUnorderedMembers

	U32 iOldestMemberTstamp;
		// This is the oldest member in the "in-queue" state, used for queues defined with a max-time definition

	///////////////////////////////////////
	//- Live Queue Info

	U32 iFailedMatchCount;
	// How many times has simple matching failed to produce a valid solution

	const U32 iAverageWaitTime;						AST(PERSIST SUBSCRIBE)
		// How long from entering the queue to receiving an offer - on average
	U32* piHistoricalWaits;
		// A list of historical waits

	U32	uiTimeoutStartTime;
	U32 uiExpireStartTime;

	U32 uiFailedMapLaunchCount;
		// This gets incremented whenever a map fails to launch correctly.  When it reachs 10, the queue is broken.

	U32 uiNextMapLaunchTime;
		//If a map fails to launch, this causes the instance not to launch new maps until this time is reached.

	U8 uiPlayersPreventingMapLaunch;
		// The number of players currently preventing the map from launching

	const U32 bNewMap : 1;							AST(PERSIST SUBSCRIBE)
		//Flag to tell players that a new map is being created
	const U32 bOvertime : 1;						AST(PERSIST SUBSCRIBE)
		//If the queue is in the overtime state
	const U32 bEnableLeaverPenalty : 1;				AST(PERSIST SUBSCRIBE)
		// Private matches must explicitly enable the leaver penalty

	U32 bAutoFill : 1;
		//If this is set, the instance will automatically create a map - ignoring queue population

	EARRAY_OF(QueueMemberTeam) eaTeams;
		//Temporary storage for all the teams in this instance for this frame

	EARRAY_OF(QueueMemberForceLeave) eaForceLeaves;
		// List of members that were forced to leave this instance because they did not satisfy restrictions
} QueueInstance;

AUTO_STRUCT;
typedef struct QueueInstanceWrapper
{
	ContainerID uEntID;
	QueueInstance *pQueueInstance;

}QueueInstanceWrapper;

AUTO_STRUCT;
typedef struct PlayerLeaderboard
{	
	ContainerID iEntID;			AST(KEY)
		// The ID of the entity this leaderboard/score is for

	U32 iLastTimeOnMap;
		// Timestamp the entity was last seen on the map

	U32 bOnMap : 1;
		// Is this ent still on map?
} PlayerLeaderboard;

//Add per entity stat info?
AUTO_STRUCT;
typedef struct MapLeaderboard
{
	S64 iMapKey;		AST(KEY)
		//Which map/partition am I?

	PlayerLeaderboard **eaEntities;
		//Which entities are on my map

} MapLeaderboard;

AUTO_STRUCT AST_CONTAINER AST_IGNORE_STRUCT(eaOrderedMembers) AST_IGNORE_STRUCT(eaMaps) AST_IGNORE(iAverageWaitTime); 
typedef struct QueueInfo
{
	const ContainerID iContainerID;				AST(PERSIST SUBSCRIBE KEY)
		// Queue's container ID

	STRING_POOLED pchName;						AST(PERSIST SUBSCRIBE POOL_STRING)
		// The name of the queue

	CONST_REF_TO(QueueDef) hDef;				AST(PERSIST SUBSCRIBE REFDICT(QueueDef))
		// The definition of the queue

	CONST_EARRAY_OF(QueueInstance) eaInstances;	AST(PERSIST SUBSCRIBE SERVER_ONLY)
		// List of instances of this queue

	const U32 uiCurrentID;						AST(PERSIST SUBSCRIBE SERVER_ONLY)
		// The current instance ID
	
	U32 iNextFrameTime;
		// Set so that a queue won't update until this time occurs

	U32 iMinMapSize;
		//Changes to the sum of all group's iMinTimed (instead of iMin)
		//  if the queue hasn't launched a map in over QueueDef->iMaxTimeToWait seconds
	U32 iMaxMapSize;
		// Convenience values used to track the min/max number of people for this queue's map

	U32 bDirty : 1;
		//If the instances are dirty, awaiting a transaction to finish cleaning them
	U32 bUpdating : 1;
		//This is set when a series updates are being committed for this queue
	U32 bAutoFill : 1;
		//Create a map for the next instance that has any ready members - ignore queue population
	U32 bNeverLaunchMaps : 1;
		//Never launch any maps for this queue (debugging) 
} QueueInfo;

AUTO_STRUCT;
typedef struct QueueInfoWrapper
{
	ContainerID uEntID;
	QueueInfo *pQueueInfo;

}QueueInfoWrapper;

AUTO_STRUCT;
typedef struct QueueRef
{
	const char *pchQueueName;		AST(POOL_STRING KEY)
		//The queue's name
	ContainerID iContID;
		// Its containerID
	REF_TO(QueueInfo) hDef;			AST(COPYDICT(QueueInfo))
		// A Reference to the QueueInfo container, filled in on the game server
} QueueRef;

AUTO_STRUCT;
typedef struct QueueList
{
	QueueRef **eaQueueRefs;
		//A list of all queues that a game server can reference
} QueueList;

AUTO_STRUCT;
typedef struct QueueGameInfo
{
	const char* pchQueueDef; AST(NAME(QueueDef) POOL_STRING)
		// The name of the QueueDef

	S32 iLevelBandIndex; AST(NAME(LevelBand) DEFAULT(-1))
		// The level band index that this queue uses

	S32 ePvPGameType; AST(NAME(GameType) SUBTABLE(PVPGameTypeEnum) DEFAULT(kPVPGameType_None))
		// The game type to set

	BolsterType eBolsterType; AST(NAME(BolsterType) DEFAULT(kBolsterType_None))
		// Bolster type
	
	S32 iBolsterLevel; AST(NAME(BolsterLevel))
		// Bolster level

	U32 uLeaverPenaltyDuration;	AST(NAME(LeaverPenaltyDuration))
		// If set, then a leaver penalty is enabled for the current map for the specified duration
} QueueGameInfo;

void ASTRT_Queues_Load(void);
void Queues_LoadCategories(void);
void Queues_Load(resCallback_HandleEvent pReloadCB, FolderCacheCallback pFolderReloadCB);
void Queues_LoadConfig(void);
void Queues_ReloadQueues(const char *pchRelPath, int UNUSED_when);

S32 queue_Validate(QueueDef *pDef);

QueueCooldownDef* queue_CooldownDefFromName(const char* pchName);
bool queue_IsQueueMap(ZoneMapType eMapType);
void queue_GetCooldownNames(const char*** pppchCooldownNames);
extern QueueCategories s_QueueCategories;

ExprContext *queue_GetContext(SA_PARAM_OP_VALID Entity *pEnt);

bool queue_CheckInstanceParamsValid(Entity* pEnt, QueueDef* pDef, QueueInstanceParams* pParams);
QueueInstanceParams* queue_CreateInstanceParams(Entity* pEnt, QueueDef* pDef, const char* pchPrivateName, const char* pchPassword, S32 iLevelBandIndex, S32 iMapIndex, bool bPrivate);

// Get the player's queue from the queue info struct that has the specified name
PlayerQueueInstance* queue_FindPlayerQueueInstance(SA_PARAM_OP_VALID PlayerQueueInfo* pQueueInfo, SA_PARAM_OP_STR const char* pchQueueName, QueueInstanceParams* pParams, bool bValidateCanUseQueue);
PlayerQueueInstance* queue_FindPlayerQueueInstanceByID(SA_PARAM_OP_VALID PlayerQueueInfo* pQueueInfo, SA_PARAM_OP_STR const char* pchQueueName, U32 uiInstanceID, bool bValidateCanUseQueue);

// Find a player in a queue
NOCONST(QueueMember) *queue_trh_FindPlayer(ATH_ARG NOCONST(QueueInfo) *pQueue, ContainerID iEntID, NOCONST(QueueInstance)** ppInstance);
#define queue_FindPlayer(pQueue, iEntID) ((pQueue) ? CONTAINER_RECONST(QueueMember, queue_trh_FindPlayer(CONTAINER_NOCONST(QueueInfo, (pQueue)), iEntID, NULL)) : NULL)
#define queue_FindPlayerAndInstance(pQueue, iEntID, ppInstance) ((pQueue) ? (QueueMember*)queue_trh_FindPlayer(CONTAINER_NOCONST(QueueInfo, (pQueue)), iEntID, CONTAINER_NOCONST2(QueueInstance,(ppInstance))) : NULL)

// Find a player in an instance
NOCONST(QueueMember) *queue_trh_FindPlayerInInstance(ATH_ARG NOCONST(QueueInstance) *pInstance, ContainerID iEntID);
#define queue_FindPlayerInInstance(pInstance, iEntID) ((pInstance) ? CONTAINER_RECONST(QueueMember, queue_trh_FindPlayerInInstance(CONTAINER_NOCONST(QueueInstance, (pInstance)), iEntID)) : NULL)

NOCONST(QueueInstance) *queue_trh_FindInstance(ATH_ARG NOCONST(QueueInfo) *pQueue, NON_CONTAINER QueueInstanceParams* pParams);
#define queue_FindInstance(pQueue, pParams) ((pQueue) ? CONTAINER_RECONST(QueueInstance, queue_trh_FindInstance(CONTAINER_NOCONST(QueueInfo, (pQueue)), (pParams))) : NULL)

QueueMap* queue_FindActiveMapForPrivateInstance(QueueInstance* pInstance, QueueDef* pDef);
PlayerQueueMap* queue_FindActiveMapForPrivatePlayerInstance(PlayerQueueInstance* pInstance, QueueDef* pDef);

void queue_InitMatchGroups(SA_PARAM_NN_VALID QueueDef* pQueueDef, SA_PARAM_NN_VALID QueueMatch* pMatch);
S32 queue_Match_FindMemberInGroup(SA_PARAM_OP_VALID QueueGroup* pGroup, U32 uMemberID);
S32 queue_Match_FindMember(SA_PARAM_OP_VALID QueueMatch* pMatch, U32 uMemberID, S32* piGroupIndex);
S32 queue_Match_FindGroupByTeam(SA_PARAM_OP_VALID QueueMatch* pMatch, U32 uTeamID);
S32 queue_Match_FindGroupByIndex(SA_PARAM_OP_VALID QueueMatch* pMatch, S32 iGroupIndex);
QueueMatchMember* queue_Match_AddMemberToGroup(SA_PARAM_NN_VALID QueueMatch* pMatch, SA_PARAM_OP_VALID QueueGroup* pGroup, U32 uEntID, U32 uTeamID, S32 iGroupRole, S32 iGroupClass);
QueueMatchMember* queue_Match_AddMember(SA_PARAM_NN_VALID QueueMatch* pMatch, S32 iGroupIndex, U32 uEntID, U32 uTeamID);
bool queue_Match_RemoveMemberFromGroup(SA_PARAM_NN_VALID QueueMatch* pMatch, S32 iGroupIndex, U32 uMemberID);
bool queue_Match_RemoveMember(SA_PARAM_NN_VALID QueueMatch *pMatch, U32 uMemberID);
void queue_trh_GroupCache(ATH_ARG NOCONST(QueueInstance)* pInstance, ATH_ARG NOCONST(QueueMap)* pMap, PlayerQueueState eState, PlayerQueueState eExcludeState, SA_PARAM_NN_VALID QueueDef* pQueueDef, SA_PARAM_NN_VALID QueueMatch* pMatch);
#define queue_GroupCacheWithState(pInstance, pQueueDef, pMap, eIncludeState, eExcludeState, pMatch) queue_trh_GroupCache(CONTAINER_NOCONST(QueueInstance,(pInstance)),CONTAINER_NOCONST(QueueMap,(pMap)),eIncludeState,eExcludeState,pQueueDef,pMatch)
#define queue_GroupCache(pInstance, pQueueDef, pMap, pMatch) queue_GroupCacheWithState(pInstance, pQueueDef, pMap, PlayerQueueState_None, PlayerQueueState_None, pMatch)

S32 queue_Match_GetAverageWaitTime(QueueInstance* pInstance, QueueMatch* pMatch);
bool queue_Match_Validate(SA_PARAM_NN_VALID QueueMatch* pMatch, SA_PARAM_NN_VALID QueueDef* pDef, SA_PARAM_OP_VALID QueueInstanceParams* pParams, bool bOvertime, bool bAutoBalance, bool bIgnoreMin);
S32 queue_GetBestGroupIndexForPlayer(const char* pchAffiliation, U32 uTeamID, S32 iCurrentGroupIndex, bool bOvertime, SA_PARAM_NN_VALID QueueDef *pQueueDef, SA_PARAM_NN_VALID QueueMatch *pMatch);
bool queue_IsValidGroupIndexForPlayer(S32 iGroupID, const char* pchAffiliation, bool bOvertime, SA_PARAM_NN_VALID QueueDef *pQueueDef, SA_PARAM_NN_VALID QueueMatch *pMatch);

bool queue_trh_ShouldCleanupInstance(ATH_ARG NOCONST(QueueInstance)* pInstance, bool bModify);
#define queue_ShouldCleanupInstance(pInstance) queue_trh_ShouldCleanupInstance(CONTAINER_NOCONST(QueueInstance, (pInstance), true))

bool queue_Instance_AllMembersInState(QueueInstance* pInstance, PlayerQueueState eState);
bool queue_PlayerInstance_AllMembersInState(PlayerQueueInstance* pInstance, PlayerQueueState eState);

// Get the leaver penalty duration for a given QueueDef
U32 queue_GetLeaverPenaltyDuration(QueueDef* pDef);

//Checks to see if the entity in question can use the queue (and optionally at a specific level band).  Pass -1 to check to see if in general the entity
//  could use this queue
QueueCannotUseReason queue_EntCannotUseQueue(Entity* pEnt, S32 iEntLevel, const char* pchEntAffiliation, QueueDef* pDef, int iLevelBand, S32 bIgnoreLevelRestrictions, bool bIgnoreRequiresExpr);

// Convenience function that passes pParams->iLevelBandIndex into entCannotUseQueue
QueueCannotUseReason queue_EntCannotUseQueueInstance(Entity* pEnt, S32 iEntLevel, const char* pchEntAffiliation, QueueInstanceParams* pParams, QueueDef* pDef, bool bIgnoreLevelRestrictions, bool bIgnoreRequiresExpr);

// Fill the GameInfo
void queue_FillGameInfo(QueueGameInfo* pGameInfo, QueueDef* pDef, QueueInstance* pInstance, QueueMap* pMap);

// Returns the WorldVariable array that a queue def would like (bolster level, queue name etc)
void queue_WorldVariablesFromDef(SA_PARAM_NN_VALID QueueDef* pDef, 
	SA_PARAM_NN_VALID QueueInstance* pInstance,
	const char* pchMapName,
	SA_PARAM_NN_VALID WorldVariable*** peaVars);

QueueDef* queue_DefFromName(const char *pchName);
QueueCategoryData* queue_GetCategoryData(QueueCategory eCategory);

U32	queue_QueueGetMaxPlayers(QueueDef* pDef, bool bOvertime);
U32 queue_QueueGetMinPlayersEx(QueueDef* pDef, bool bOvertime, bool bAutoBalance, bool bIsPrivate);
#define queue_QueueGetMinPlayers(pDef, bOvertime) queue_QueueGetMinPlayersEx(pDef, bOvertime, false, false)
S32 queue_GetGroupMaxSize(QueueGroupDef* pDef, bool bOvertime);
S32 queue_GetGroupMinSizeEx(QueueGroupDef* pDef, bool bOvertime, bool bAutoBalance, bool bIsPrivate);
#define queue_GetGroupMinSize(pDef, bOvertime) queue_GetGroupMinSizeEx(pDef, bOvertime, false, false)

BolsterType queue_GetBolsterType(QueueDef* pDef, QueueLevelBand* pLevelBand);

bool queue_InstanceShouldCheckOffers(QueueDef* pDef, QueueInstanceParams* pParams);

// Find the best level band index for a QueueDef given a player level
S32 queue_GetLevelBandIndexForLevel(QueueDef* pDef, S32 iLevel);

// Finds a world variable on the QueueDef at the specified level band that can be modified in private games
QueueVariableData* queue_FindVariableDataEx(QueueDef* pDef, S32 iLevelBandIndex, const char* pchFindVar, const char* pchFindMap, bool bByVar);
#define queue_FindVariableData(pDef, iLevelBandIndex, pchFindVar) queue_FindVariableDataEx(pDef, iLevelBandIndex, pchFindVar, NULL, false)
bool queue_IsVariableSupportedOnMap(QueueDef* pDef, QueueLevelBand* pLevelBand, const char* pchVarName, const char* pchMapName);
S64 queue_GetMapKey(ContainerID uMapID, U32 uPartitionID);
U32 queue_GetMapIDFromMapKey(S64 iMapKey);
U32 queue_GetPartitionIDFromMapKey(S64 iMapKey);
const char* queue_GetMapNameByIndex(QueueDef* pDef, S32 iMapIndex);
S32 queue_GetMapIndexByName(QueueDef* pDef, const char* pchMapName);
QueueCustomMapData* queue_GetCustomMapTypeFromMapName(QueueDef* pDef, const char* pchMapName);
bool queue_IsMapFull(QueueMap* pMap, QueueInstance* pInstance, QueueDef* pDef, S32 iTeamSize, const char* pchAffiliation, bool bOvertime);
bool queue_IsPlayerQueueMapFull(PlayerQueueMap* pMap, QueueDef* pDef, S32 iTeamSize, const char* pchAffiliation, bool bOvertime);
bool queue_GetVariableOverrideValue(const QueueGameSetting** eaSettings, QueueVariableData* pQueueVarData, S32* piValue);

bool queue_trh_IsPrivateMatch(ATH_ARG NOCONST(QueueInstanceParams)* pParams);
#define queue_IsPrivateMatch(pParams) queue_trh_IsPrivateMatch(CONTAINER_NOCONST(QueueInstanceParams, (pParams)))
bool queue_trh_CanLeaveQueue(ATH_ARG NOCONST(QueueInstanceParams)* pParams, PlayerQueueState eMemberState);
#define queue_CanLeaveQueue(pParams, eMemberState) queue_trh_CanLeaveQueue(CONTAINER_NOCONST(QueueInstanceParams, (pParams)), eMemberState)

QueuePrivateNameInvalidReason queue_GetPrivateNameInvalidReason(const char* pchPrivateName);

U32 queue_GetOriginalOwnerID(SA_PARAM_OP_VALID QueueInstance* pInstance);
void queue_GetPrivateChatChannelName(char** pestrChannel, U32 uOwnerID, U32 uInstanceID);

void queue_SendResultMessageEx(U32 iEntID, U32 iTargetEntID, const char *pcQueueName, const char *pcMessageKey, const char *pchCallerFunction);
#define queue_SendResultMessage(iEntID, iTargetEntID, pcQueueName, pcMessageKey)  queue_SendResultMessageEx(iEntID, iTargetEntID, pcQueueName, pcMessageKey, __FUNCTION__)

void queue_SendTeamResultMessageEx(CachedTeamStruct *pTeam, U32 iTargetEntID, const char *pcQueueName, const char *pcMessageKey, const char *pchCallerFunction);
#define queue_SendTeamResultMessage(pTeam, iTargetEntID, pcQueueName, pcMessageKey)  queue_SendTeamResultMessageEx(pTeam, iTargetEntID, pcQueueName, pcMessageKey, __FUNCTION__)

// Deal with the Join Time Limit.
int queue_GetJoinTimeLimit(QueueDef* pDef);		// Returns -1 if there is no limit. Zero is a valid limit for Champions (via bQueueJoinTimeIsExact).
bool queue_MapJoinTimeLimitIsOkay(QueueMap* pMap, QueueDef* pDef);
// The Map is accepting new members based on the maps state and the join time limit
bool queue_MapAcceptingNewMembers(QueueMap *pMap, QueueDef *pDef);

// Get the new map based on uiMapCreateRequestID
QueueMap *queue_GetNewMap(SA_PARAM_OP_VALID QueueInstance *pInstance, U32 uiMapCreateRequestID);

// safely destroy the new map
void queue_DestroyNewMap(SA_PARAM_OP_VALID QueueInstance *pInstance, U32 uiMapCreateRequestID);

// Return the index to the new map, -1 is not found
S32 queue_GetNewMapIndex(SA_PARAM_OP_VALID QueueInstance *pInstance, U32 uiMapCreateRequestID);

// Is this private instance?
bool queue_InstanceIsPrivate(SA_PARAM_OP_VALID QueueInstance *pInstance);

// player version of is this queue private
bool queue_PlayerInstanceIsPrivate(SA_PARAM_OP_VALID PlayerQueueInstance *pInstance);

// Get the maximum time to wait for the instance
U32 queue_GetMaxTimeToWait(SA_PARAM_OP_VALID QueueDef *pQueueDef, SA_PARAM_OP_VALID QueueInstance *pInstance);

// level range of instance
S32 queue_GetSettingMapLevel(PlayerQueueInstance *pInstance);

// Is the level range ok to start this private queue instance
bool queue_PrivateQueueLevelCheck(Entity *pEntity, PlayerQueueInstance *pInstance);
// version for the ent
bool queue_EntPrivateQueueLevelCheck(Entity *pEntity, PlayerQueueInstance *pInstance);

// Get the queueAffiliation of the Entity.
const char* queue_EntGetQueueAffiliation(Entity *pEntity);

// Fill out the join criteria for a given Entity
void queue_EntFillJoinCriteria(Entity *pEnt, QueueMemberJoinCriteria* pMemberJoinCriteria);


extern QueueConfig g_QueueConfig;
extern DictionaryHandle g_hQueueDefDict;

#endif // QUEUE_COMMON_H
