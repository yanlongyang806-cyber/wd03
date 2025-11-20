#pragma once
GCC_SYSTEM

//moved a lot of ent-related enums here so that they can easily be shared around between projects without including gobs of other crazy stuff
AUTO_ENUM;
typedef enum PetRelationshipStatus {
	PTSTATUS_ALWAYSPROP = 1 << 0, // This this container to always propagate its powers
	PTSTATUS_TEAMREQUEST = 1 << 1, // Request to be added to the team
} PetRelationshipStatus;
extern StaticDefineInt PetRelationshipStatusEnum[];

AUTO_ENUM;
typedef enum kCritterOverrideFlag
{
	kCritterOverrideFlag_None				= (1<<0),
	kCritterOverrideFlag_Destructible		= (1<<1),
	kCritterOverrideFlag_Throwable			= (1<<2),
}kCritterOverrideFlag;

AUTO_ENUM;
typedef enum CritterSpawnLimit
{
	kCritterSpawnLimit_No_Limit = -1,
	kCritterSpawnLimit_NotAutomatic,
	kCritterSpawnLimit_One,
	kCritterSpawnLimit_Two,
	kCritterSpawnLimit_Three,
	kCritterSpawnLimit_Four,
}CritterSpawnLimit;

AUTO_ENUM;
typedef enum LogoutTimerType
{
	LogoutTimerType_None,				// No logout timer (used in UI)
	LogoutTimerType_NotOnInstanceTeam,	// On an instance map but not on the right team.  Rejoin team or be booted from the map
	LogoutTimerType_MissionReturn,		// Map is shutting down, all players must leave
	LogoutTimerType_MapDoesNotMatchProgression,	// If a player/team is on a map that is not allowed by their progression, they are booted from the map
	LogoutTimerType_NotOnInstanceGuild, // On a guild map but not part of the guild. Rejoin the guild or be booted from the map
} LogoutTimerType;

extern StaticDefineInt LogoutTimerTypeEnum[];

// Nemesis Motivation is used mostly to determine what sorts of missions the Nemesis will initiate
AUTO_ENUM;
typedef enum NemesisMotivation
{
	NemesisMotivation_Power,		// Nemesis wants political power/to rule the world
	NemesisMotivation_Wealth,		// Nemesis wants money
	NemesisMotivation_Revenge,		// Nemesis wants to kill/get even with the player
	NemesisMotivation_Infamy,		// Nemesis wants to be known and remembered for his deeds
	
	NemesisMotivation_Count,		EIGNORE
} NemesisMotivation;

extern StaticDefineInt NemesisMotivationEnum[];

// Nemesis Personality is used mostly to determine what flavor text the nemesis should say
AUTO_ENUM;
typedef enum NemesisPersonality
{
	NemesisPersonality_Mastermind,	// "You won't stop me this time"
	NemesisPersonality_Savage,		// "I'm gonna crush you!"
	NemesisPersonality_Maniac,		// "Burn! BURN! Hahahaha!"
	
	NemesisPersonality_Count,		EIGNORE
} NemesisPersonality;

extern StaticDefineInt NemesisPersonalityEnum[];

AUTO_ENUM;
typedef enum OwnedContainerState {
	OWNEDSTATE_OFFLINE, // Container is offline, and should stay so until state is changed
	OWNEDSTATE_STATIC, // Container is in one place, and should stay there until state is changed
	OWNEDSTATE_AUTO_SUMMON, // When transferring between maps, should be automatically summoned
	OWNEDSTATE_AUTO_CONTROL, // Should be automatically summoned and controlled
	OWNEDSTATE_ACTIVE, // Active pets will be automatically summoned during map relocations if the region rules allow
} OwnedContainerState;
extern StaticDefineInt OwnedContainerStateEnum[];

// Defines the different ways in which one Entity can consider another, used in factions,
//  affects combat targeting and other systems.
// Do not change the values for this unless you want to update all existing .faction files,
//  as some of them use the values directly rather than the names.
AUTO_ENUM;
typedef enum EntityRelation 
{
	kEntityRelation_Unknown = -1,
	kEntityRelation_Friend = 0,
	kEntityRelation_Foe = 1,
	kEntityRelation_FriendAndFoe,
	kEntityRelation_Neutral,
} EntityRelation;

AUTO_ENUM;
typedef enum PVPDuelEntityState {
	DuelState_Failed,
	DuelState_Active,
	DuelState_Invite,
	DuelState_Accepted,
	DuelState_FailedInvite,
	DuelState_Request,
	DuelState_FailedRequest,
	DuelState_Dead, //Used in team duels
	DuelState_Decline,
} PVPDuelEntityState;

AUTO_ENUM;
typedef enum PVPDuelVictoryType {
	PVPDuelVictoryType_KO,
	PVPDuelVictoryType_RingOut,
} PVPDuelVictoryType;

extern StaticDefineInt PVPDuelEntityStateEnum[];

AUTO_ENUM;
typedef enum PvPQueueMatchResult {
	kPvPQueueMatchResult_Win,
	kPvPQueueMatchResult_Loss,
} PvPQueueMatchResult;

extern StaticDefineInt PvPQueueMatchResultEnum[];

typedef enum PetStanceType
{
	PetStanceType_PRECOMBAT = 0,
	PetStanceType_COMBATROLES,
	PetStanceType_COUNT
} PetStanceType;

AUTO_ENUM;
typedef enum PetRelationshipType {
	CONRELATION_PET, ENAMES(PET) // This is my pet
	CONRELATION_PRIMARY_PET, ENAMES(PRIMARY_PET) // This is my current primary pet
} PetRelationshipType;
extern StaticDefineInt PetRelationshipTypeEnum[];
