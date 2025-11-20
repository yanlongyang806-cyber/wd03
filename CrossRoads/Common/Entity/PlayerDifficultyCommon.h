#include "ReferenceSystem.h"
#include "StashTable.h"
#include "WorldLibEnums.h"
#include "itemGenCommon.h"

typedef struct Message Message;
typedef struct PowerDef PowerDef;
typedef struct RewardTable RewardTable;
typedef int PlayerDifficultyIdx;

AUTO_STRUCT;
typedef struct DropRateDifficultyMultiplier
{
	U32 eQuality;			AST(STRUCTPARAM NAME(Quality) SUBTABLE(ItemGenRarityEnum))
	F32 fMultiplier;		AST(STRUCTPARAM NAME(Multiplier))
} DropRateDifficultyMultiplier;

AUTO_STRUCT;
typedef struct PlayerDifficultyMapData
{
	// Public name and Region type: Describes which maps this difficulty setting applies to - Only one should be set at a time
	// public name of the map to which the difficulties should be applied; NULL to represent the default
	char *pchMapName;									AST(NAME(MapName) POOL_STRING)
	// The region type which this difficulty applies to.
	S32 eRegionType;									AST(NAME(Region) DEFAULT(WRT_None) SUBTABLE(WorldRegionTypeEnum))

	// overriden display properties
	REF_TO(Message) hName;								AST(NAME(Name) REFDICT(Message))
	REF_TO(Message) hDescription;						AST(NAME(Description) REFDICT(Message))

	// how much to scale numeric rewards
	F32 fNumericRewardScale;							AST(NAME(NumericRewardScale) DEFAULT(1.0f))

	// how to affect critters
	int iLevelModifier;									AST(NAME(LevelModifier))
	REF_TO(PowerDef) hPowerDef;							AST(NAME(Power))

	// additional reward
	REF_TO(RewardTable) hRewardTable;					AST(NAME(RewardTable))

	// drop rate multipliers
	DropRateDifficultyMultiplier** eaDropRateMultipliers; AST(NAME(DropRateMultiplier))

	// death penalty "rewards"
	REF_TO(RewardTable) hDeathPenaltyTable;				AST(NAME(PlayerDeathPenalty))
	REF_TO(RewardTable) hSavedPetDeathPenaltyTable;		AST(NAME(SavedPetDeathPenalty))
} PlayerDifficultyMapData;

AUTO_STRUCT;
typedef struct PlayerDifficulty
{
	// key lookup value
	PlayerDifficultyIdx	iIndex;							AST(NAME(Index) INT)

	// internal name for designer ease-of-use
	const char *pchInternalName;						AST(NAME(InternalName) POOL_STRING)

	// display properties
	REF_TO(Message) hName;								AST(NAME(Name) REFDICT(Message))
	REF_TO(Message) hDescription;						AST(NAME(Description) REFDICT(Message))

	// team size setting override (only if > 0)
	S32 iTeamSizeOverride;								AST(NAME(TeamSizeOverride))
	const char *DisableTeamSizeMapVarName;					AST(NAME(DisableTeamSizeMapVarName) POOL_STRING)

	PlayerDifficultyMapData **peaMapSettings;			AST(NAME(MapSetting))
} PlayerDifficulty;

AUTO_STRUCT;
typedef struct PlayerDifficultySet
{
	PlayerDifficulty **peaPlayerDifficulties;			AST(NAME(Difficulty))
} PlayerDifficultySet;

extern ParseTable parse_PlayerDifficultyMapData[];
#define TYPE_parse_PlayerDifficultyMapData PlayerDifficultyMapData
extern ParseTable parse_PlayerDifficulty[];
#define TYPE_parse_PlayerDifficulty PlayerDifficulty
extern ParseTable parse_PlayerDifficultySet[];
#define TYPE_parse_PlayerDifficultySet PlayerDifficultySet

extern PlayerDifficultySet g_PlayerDifficultySet;
extern StashTable g_PlayerDifficultyStash;

bool pd_MapDifficultyApplied(void);
SA_RET_OP_VALID PlayerDifficulty *pd_GetDifficulty(PlayerDifficultyIdx iDifficulty);
SA_RET_OP_VALID PlayerDifficultyMapData *pd_GetDifficultyMapData(PlayerDifficultyIdx iDifficulty, const char* pchMapName, WorldRegionType eTargetRegion);
Message* pd_GetDifficultyDescMsg(PlayerDifficultyIdx eIndex, const char* pchMapName, WorldRegionType eTargetRegion);
Message* pd_GetDifficultyNameMsg(PlayerDifficultyIdx eIndex, const char* pchMapName, WorldRegionType eTargetRegion);
PlayerDifficultyIdx pd_GetPlayerDifficultyIndexFromName(const char *pchInternalName);
