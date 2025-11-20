#pragma once
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "referencesystem.h"
#include "stdtypes.h"

typedef U32 ContainerID;
typedef enum GlobalType GlobalType;
typedef struct GameAccountData GameAccountData;
typedef struct AvailableCharSlots AvailableCharSlots;
typedef enum UnlockedAllegianceFlags UnlockedAllegianceFlags;
typedef enum UnlockedCreateFlags UnlockedCreateFlags;
typedef struct LoginSpeciesUnlockedRef LoginSpeciesUnlockedRef;
typedef struct PossibleVirtualShard PossibleVirtualShard;
typedef struct LoginPetInfo LoginPetInfo;
typedef struct LoginPuppetInfo LoginPuppetInfo;
typedef struct AssignedStats AssignedStats;
typedef struct NOCONST(AssignedStats) NOCONST(AssignedStats);
typedef struct PossibleCharacterCostume PossibleCharacterCostume;
typedef enum ClusterShardType ClusterShardType;

extern U32 gAddictionMaxPlayTime;

// This structure contains the basic character data that is used during character selection.
AUTO_STRUCT;
typedef struct Login2CharacterChoice
{
    ContainerID containerID;    AST(KEY)
    ContainerID accountID;

    // Which server currently owns the player container.
    GlobalType ownerType;
    ContainerID ownerID;

    // Information from the Entity container header or account stub.
    U32 createdTime;
    U32 level;
    U32 fixupVersion;
    U32 lastPlayedTime;
    U32 virtualShardID;

    const char * shardName;      AST(POOL_STRING)
    const char * pubAccountName;
    const char * privAccountName;
    const char * savedName;
    const char * extraData1;
    const char * extraData2;
    const char * extraData3;
    const char * extraData4;
    const char * extraData5;

    const char * oldBadName;

    // flags
    bool isOffline;
    bool isDeleted;
    bool isUGCEditAllowed;
    bool hasBadName;
    bool detailRequested;
    bool detailReceived;

    // Client data - these fields are only used by the client.
    bool isSaveLoginRecommended;
} Login2CharacterChoice;

// This structure is used to pass around basic information about a player's characters during login.
AUTO_STRUCT;
typedef struct Login2CharacterChoices
{
    ContainerID accountID;

    // List of shards that did not provide a response.
    STRING_EARRAY missingShardNames;    AST(POOL_STRING)

    // List of available characters.
    EARRAY_OF(Login2CharacterChoice) characterChoices;
} Login2CharacterChoices;

// This is the structure that the loginserver sends to the client.  It includes character choices and info on available character slots.
AUTO_STRUCT;
typedef struct Login2CharacterSelectionData
{
    ContainerID accountID;
    const char *publicAccountName;
    const char *privateAccountName;   

    Login2CharacterChoices *characterChoices;

    AvailableCharSlots *availableCharSlots;

    EARRAY_OF(PossibleVirtualShard) virtualShardInfos;

    // Unlocks for character creation.
    UnlockedAllegianceFlags unlockAllegianceFlags;		AST(FLAGS)
    UnlockedCreateFlags unlockCreateFlags;				AST(FLAGS)
    LoginSpeciesUnlockedRef **unlockedSpecies;

    REF_TO(GameAccountData) hGameAccountData;		AST(COPYDICT(GameAccountData))

    bool UGCLoginOnly; 
    bool hasCompletedTutorial;
} Login2CharacterSelectionData;

AUTO_STRUCT;
typedef struct Login2CharacterCreationData
{
    char *name;
    char *subName;
    char *description;
    char *gameSpecificChoice;

    // Note that the "hName" field names are for compatibility with old quickplay files which used to embed PossibleCharacterChoice
    STRING_POOLED className;                        AST(POOL_STRING NAME("ClassName") NAME("hClass"))
    STRING_POOLED allegianceName;                   AST(POOL_STRING NAME("AllegianceName"))
    STRING_POOLED characterPathName;                AST(POOL_STRING NAME("CharacterPathName"))
    STRING_POOLED speciesName;                      AST(POOL_STRING NAME("SpeciesName") NAME("hSpecies"))
    STRING_POOLED powerTreeName;                    AST(POOL_STRING NAME("PowerTreeName") NAME("hPowerTree"))
    STRING_POOLED moodName;                         AST(POOL_STRING NAME("MoodName") NAME("hMood"))
    STRING_EARRAY powerNodes;

    EARRAY_OF(PossibleCharacterCostume) costumes;   AST(NAME("Costumes") NAME("Costume") NAME("hCostume"))

    // Nonsense to make structparser happy.
    union
    {
        NOCONST(AssignedStats) **assignedStats;         NO_AST
        AssignedStats **constAssignedStats;             AST(LATEBIND NAME("AssignedStat"))
    };

    EARRAY_OF(LoginPetInfo) petInfo;
    EARRAY_OF(LoginPuppetInfo) puppetInfo;

    U32 costumePreset;
    ContainerID virtualShardID;
    STRING_POOLED shardName;                        AST(POOL_STRING)
    bool skipTutorial;
} Login2CharacterCreationData;

// This is the status that is the same for everyone.
AUTO_STRUCT;
typedef struct Login2ShardStatus
{
    // The name of the shard.
    STRING_POOLED shardName;            AST(KEY POOL_STRING)

    // What type of shard.
    ClusterShardType shardType;

    // How many people are currently in the Queue on this shard.
    U32 queueSize;

    // Is the shard up and running?
    bool isReady;

    // Is this the shard that the client is currently connected to?
    bool isCurrent;

    // Is character creation disabled on this shard?
    bool creationDisabled;
} Login2ShardStatus;

// This is the status that has to be per-player.
AUTO_STRUCT;
typedef struct Login2ShardPlayerStatus
{
    // The name of the shard.
    STRING_POOLED shardName;            AST(KEY POOL_STRING)

    // The number of friends the player has on this shard.
    U32 friendCount;
} Login2ShardPlayerStatus;

AUTO_STRUCT;
typedef struct Login2ClusterStatus
{
    // This flag is set if this shard is part of a cluster.
    bool isCluster;

    // The set of shards in the cluster, or an earray of one if the shard is not in a cluster.
    EARRAY_OF(Login2ShardStatus) shardStatus;

    // Player specific status for the shards.
    EARRAY_OF(Login2ShardPlayerStatus) shardPlayerStatus;
} Login2ClusterStatus;

const char *Login2_GetAllegianceFromCharacterChoice(Login2CharacterChoice *characterChoice);

// Extract the character class name used during character selection from the extra header fields.
const char *Login2_GetClassNameFromCharacterChoice(Login2CharacterChoice *characterChoice);

// Extract the character path name used during character selection from the extra header fields.
const char *Login2_GetPathNameFromCharacterChoice(Login2CharacterChoice *characterChoice);

// Extract the character species name used during character selection from the extra header fields.
const char *Login2_GetSpeciesNameFromCharacterChoice(Login2CharacterChoice *characterChoice);

// Get the total number of project slots, which includes slots from gamepermissions, game account data key/value and account server key/value.
int Login2_UGCGetProjectMaxSlots(GameAccountData *gameAccountData);

// Get the total number of series slots, which includes slots from gamepermissions, game account data key/value and account server key/value.
int Login2_UGCGetSeriesMaxSlots(GameAccountData *gameAccountData);