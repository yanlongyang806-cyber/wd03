/***************************************************************************
 *     Copyright (c) Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#ifndef REWARDCOMMON_H__
#define REWARDCOMMON_H__


GCC_SYSTEM

#include "ReferenceSystem.h"
#include "Team.h"
#include "message.h"
#include "entEnums.h"
#include "Expression.h"
#include "mission_enums.h"
#include "ItemCommon.h"
#include "ItemEnums.h"

typedef struct Expression Expression;
typedef struct Entity Entity;
typedef struct RewardEntry RewardEntry;
typedef struct LevelRewardEntry LevelRewardEntry;
typedef struct RewardTable RewardTable;
typedef struct RewardValTable RewardValTable;
typedef struct ItemDef ItemDef;
typedef struct ItemPowerDef ItemPowerDef;
typedef struct PlayerCostume PlayerCostume;
typedef struct InventoryBag InventoryBag;
typedef struct ItemGenRarityDef ItemGenRarityDef;
typedef struct RewardContext RewardContext;

extern StaticDefineInt ItemCategoryEnum[];
extern StaticDefineInt ItemQualityEnum[];
extern StaticDefineInt NumericOpEnum[];
typedef enum enumResourceValidateType enumResourceValidateType;

#define REWARDS_BASE_DIR "defs/rewards"
#define REWARDS_EXTENSION "rewards"

AUTO_STRUCT;
typedef struct RewardTags {
	char **tags;			AST(NAME(Tag))
} RewardTags;

// Enum table for the reward tags
extern StaticDefineInt RewardTagsEnum[];

AUTO_ENUM;
typedef enum RewardAlgorithm
{
	kRewardAlgorithm_Weighted,
	kRewardAlgorithm_GiveAll,
	kRewardAlgorithm_Gated,			// This reward type requires that rewardGateType enum to be set
}RewardAlgorithm;


AUTO_ENUM;
typedef enum RewardFlag
{
	kRewardFlag_NoRepeats	= (1<<0),	//never choose the same reward entry more than once
	kRewardFlag_DupForTeam	= (1<<1),	//specifies that every team member gets a copy of this reward table
	kRewardFlag_SeparateBag	= (1<<2),	//specifies that this tables rewards are always in a unique bag
	kRewardFlag_UsePlayerLevel = (1<<3),	// specifies that item levels should be based upon the player targeted for the reward
	kRewardFlag_PlayerOwned = (1<<4),	// Used by reward info to indicate this is a player owned bag and is never a team bag
	kRewardFlag_Deprecated = (1<<5),
	kRewardFlag_PickupTypeFromThisTable = (1<<6),	// All tables underneath this table will use this tables pickuptype
	kRewardFlag_PlayerKillCreditAlways = (1 << 7),		// As long as the player team is listed the players will get an item even if they are the high damagers.
	kRewardFlag_AllBagsUseThisCostume = (1 << 8),		// works for open missions, power_exec and kills. All bags created will use this reward tables costume
}RewardFlag;

// flags that propogate from rewardtables to parent bags
#define PROPOGATING_REWARDFLAGS (kRewardFlag_NeedOrGreed)

AUTO_ENUM;
typedef enum RewardKillerType
{
	RewardKillerType_Players,
	RewardKillerType_Critters,
	RewardKillerType_AllEnts,
}RewardKillerType;


AUTO_ENUM;
typedef enum RewardExecuteType
{
	kRewardExecuteType_None,
	kRewardExecuteType_AutoExec,  // e.g. boosts that you pickup via rollover
}RewardExecuteType;


AUTO_ENUM;
typedef enum RewardPickupType
{
	kRewardPickupType_None,
	kRewardPickupType_Interact,
	kRewardPickupType_Rollover,
	kRewardPickupType_Direct,
	kRewardPickupType_Clickable,
	kRewardPickupType_Choose,
	kRewardPickupType_FromOrigin,
	kRewardPickupType_Count,	EIGNORE
}RewardPickupType;

AUTO_ENUM;
typedef enum RewardOwnerType
{
	kRewardOwnerType_None, EIGNORE
	kRewardOwnerType_Player,
	kRewardOwnerType_Team,
	kRewardOwnerType_TeamLeader,
	kRewardOwnerType_AllPlayers,
	kRewardOwnerType_AllEnts,
	kRewardOwnerType_Enemies,
}RewardOwnerType;

AUTO_ENUM;
typedef enum RewardLaunchType
{
	kRRewardLaunchType_Drop,
	kRRewardLaunchType_Scatter,
}RewardLaunchType;


AUTO_ENUM;
typedef enum RewardChoiceType
{
	kRewardChoiceType_None, EIGNORE
	kRewardChoiceType_Choice,
	kRewardChoiceType_ChoiceVariableCount,
	kRewardChoiceType_Include,
	kRewardChoiceType_CharacterBasedInclude,
	kRewardChoiceType_LevelRange,
	kRewardChoiceType_Empty,
	kRewardChoiceType_AlgoBase,
	kRewardChoiceType_AlgoChar,
	kRewardChoiceType_AlgoCost,
	kRewardChoiceType_SkillRange,
	kRewardChoiceType_EPRange,
	kRewardChoiceType_Expression,
	kRewardChoiceType_ExpressionInclude,
	kRewardChoiceType_Disabled,
	kRewardChoiceType_TimeRange,
	kRewardChoiceType_Count
}RewardChoiceType;

AUTO_ENUM;
typedef enum CharacterBasedIncludeType
{
	kCharacterBasedIncludeType_None, EIGNORE
	kCharacterBasedIncludeType_Class,//appends _<CLASS_NAME>
	kCharacterBasedIncludeType_Species,//appends _<SPECIES_NAME>
	kCharacterBasedIncludeType_AllClassPaths, ENAMES(ClassPath)//appends _<CLASSPATH_NAME>
	kCharacterBasedIncludeType_Gender,//appends _MALE or _FEMALE
}CharacterBasedIncludeType;

AUTO_ENUM;
typedef enum RewardType
{
	kRewardType_None, EIGNORE
	kRewardType_Item,
	kRewardType_AlgoItem,
	kRewardType_RewardTable,
	kRewardType_Numeric,
	kRewardType_AlgoNumeric,
	kRewardType_AlgoItemForce,
	kRewardType_ItemDifficultyScaled,
	kRewardType_AlgoItemDifficultyScaled,
	kRewardType_AlgoItemForceDifficultyScaled,
	kRewardType_ItemWithGems,	// requires a reward table
	kRewardType_AlgoNumericNoScale,
	kRewardType_UnidentifiedItemWrapper,
}RewardType;

typedef struct	DefineContext DefineContext;
extern DefineContext *g_RewardExtraNumericTypes;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_RewardExtraNumericTypes);
typedef enum RewardValueType
{
	kRewardValueType_Misc = -1,
	kRewardValueType_XP = 0,
	kRewardValueType_RP,
	kRewardValueType_Res,
	kRewardValueType_Star,
	kRewardValueType_SP,
	kRewardValueType_OSP,
	kRewardValueType_DXP,
	kRewardValueType_FirstGameSpecific,
}RewardValueType;

typedef enum RewardOverflowBagPermission
{
	kRewardOverflow_DisallowOverflowBag,
	kRewardOverflow_AllowOverflowBag,
	kRewardOverflow_ForceOverflowBag,
} RewardOverflowBagPermission;

// the following are bonuses to numeric types, these are bit fields
// all rewards listed here are additive not multiplicative.
AUTO_ENUM;
typedef enum RewardBonusType
{
	RewardBonusType_None = 0,			
	RewardBonusType_Recruit = 1,		
	RewardBonusType_Lifetime = 2,
	RewardBonusType_Modifier = 4,
} RewardBonusType;

AUTO_ENUM;
typedef enum RewardContextType
{
	RewardContextType_EntKill, 
	RewardContextType_MissionDrop,
	RewardContextType_MissionExpr,
	RewardContextType_MissionReward,
	RewardContextType_OpenMission,
	RewardContextType_RandomMissionGen,
	RewardContextType_Clickable,
	RewardContextType_PowerExec,
	RewardContextType_LevelUp,
	RewardContextType_DeathPenalty,
	RewardContextType_Crafting,
	RewardContextType_AlgoBase,
	RewardContextType_AlgoExtra,
	RewardContextType_Store,
	RewardContextType_SubMissionTurnIn,

}RewardContextType;


extern DefineContext *g_RewardGatedTypes;

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_RewardGatedTypes);
typedef enum RewardGatedType
{
	RewardGatedType_None = 0,	// used for validation, this value 
	RewardGatedType_FirstGameSpecific,		EIGNORE
}RewardGatedType;

//Used by RewardBonusType_Modifier
AUTO_STRUCT AST_CONTAINER;
typedef struct RewardModifier
{
	CONST_STRING_POOLED pchNumeric;			AST(PERSIST POOL_STRING KEY)
		//The name of the numeric being affected

	const F32 fFactor;						AST(PERSIST)
		// What to multiply this reward type by.  A factor of 1.10 equals 10% more

} RewardModifier;

//Used by Player to record time that a gated type has been set and how many times
AUTO_STRUCT AST_CONTAINER;
typedef struct RewardGatedTypeData
{
	// The type use as a key
	U32 eType;									AST(KEY PERSIST SOMETIMES_TRANSACT SUBTABLE(RewardGatedTypeEnum))

	// seconds since 2000 that this was set
	U32 uTimeSet;								AST(PERSIST SOMETIMES_TRANSACT)

	// Number of times awarded
	U32 uNumTimes;								AST(PERSIST SOMETIMES_TRANSACT)

} RewardGatedTypeData;

// The following is used to communicate what gate values to use when passed in and what values are changed when 
AUTO_STRUCT;
typedef struct RewardGatedDataInOut
{
	INT_EARRAY eaCurrentGatedType;		
	// Matches one to one with eaCurrentGatedType
	INT_EARRAY eaCurrentGatedValues;		

	// The list of changed gated types (unique)
	INT_EARRAY eaGateTypesChanged;		 
	
} RewardGatedDataInOut;



AUTO_STRUCT;
typedef struct RewardModifierList
{
	RewardModifier **ppRewardMods;
} RewardModifierList;

AUTO_STRUCT;
typedef struct CharacterBasedIncludeContext
{
	const char* pchClass;			NO_AST
	const char** eaClassPathNames;	AST(POOL_STRING)
	const char* pchSpecies;			NO_AST
	Gender eGender;
	S32 iContainerID;
}CharacterBasedIncludeContext;

typedef struct RewardContextData
{
	// optional entity for non-transactions
	Entity *pEnt;

	// the entity's game account data extract for transactions
	GameAccountDataExtract* pExtract;

	// player level for auto_trans (that can't have ent passed in)
	S32 iPlayerLevel;

	// reward mods for transactions
	EARRAY_OF(RewardModifier) eaRewardMods;

	CharacterBasedIncludeContext CBIData;

} RewardContextData;

// *************************************************************************
//  Rewards
// *************************************************************************

AUTO_STRUCT;
typedef struct RewardExtraNumericNameTable
{
	const char *pcRewardExtraNumericTableName;		AST(POOL_STRING)
	
}RewardExtraNumericNameTable;

AUTO_STRUCT;
typedef struct RewardExtraNumericTable
{
	// earray of the table names
	CONST_EARRAY_OF(RewardExtraNumericNameTable) eRewardNumericTableNames;	
	
}RewardExtraNumericTable;

AUTO_STRUCT AST_IGNORE(DisableCostume) AST_IGNORE(displayNameMsg);
typedef struct RewardEntry
{
	RewardChoiceType	ChoiceType;
	RewardType			Type;
    NumericOp           numeric_op;
	F32					fWeight;		AST( DEF(1) )
	int					Count;			AST( DEF(1) )
	Expression*			pCountExpr;		AST(NAME(CountExpression) LATEBIND)
	REF_TO(ItemDef)		hItemDef;		AST(NAME(ItemDef) REFDICT(ItemDef) )
	REF_TO(ItemDef)		hUnidentifiedResultDef;		AST(REFDICT(ItemDef) )
	int					MinLevel;
	int					MaxLevel;
	REF_TO(RewardTable) hRewardTable;	AST(NAME(RewardTable) REFDICT(RewardTable) STRUCT_NORECURSE )
	REF_TO(RewardValTable) hRewardValTableOverride; AST(NAME(RewardValTableOverride) REFDICT(RewardValTable))
	F32					Value;
	bool				bBaseItemsOnly;
	bool				bScaleNumeric;
	bool				bHideInUI;
	REF_TO(ItemPowerDef) hItemPowerDef;	AST(NAME(ItemPowerDef) REFDICT(ItemPowerDef) )
	REF_TO(PlayerCostume)hCostumeDef;	AST(NAME(CostumeDef) REFDICT(PlayerCostume) )
	Expression*			pRequiresExpr;	AST(NAME(RequiresExpression) LATEBIND)
	const char**		ppchRequiredGamePermissionTokens; AST(NAME(RequiredGamePermissionToken) POOL_STRING)
	ItemQuality			Quality;
	RewardTable*		parent; NO_AST	// debug

	const char*			pchActivityName; AST(NAME(ActivityName, RequiredActivity) POOL_STRING)
	const char*			pchShardVariable;	//Must be an int that isn't 0 to pass
	CharacterBasedIncludeType	eCharacterBasedIncludeType;AST(DEFAULT(1))
	char* pchCharacterBasedIncludePrefix;
	DisplayMessage		msgBroadcastChatMessage; AST(NAME(BroadcastChatMessage) STRUCT(parse_DisplayMessage))
	
	// The scale value to use for additional powers
	// use this for the reward_dif_scale_xxx tables. 
	// this is multiplied by the value in the reward table RewardDifficultyPowerScale
	F32					fRewardDifficultyPowerScaleMultiplier;	AST(NAME(RewardDiffPowerScaleMult) DEF(1.0))

	// If true this will cause a gate reward table to reset
	bool				bResetGatedTable;

	// The gated type that changes algo numerics based on index
	RewardGatedType		eGatedForNumeric;

	// The percent change per index value of the gated numeric
	S32					iGatedPercentChange;

	// maximum percent change for gated numerics (absolute). Only used if > 0
	S32					iMaxGatedPercentChange;

	// for algonumerics, a flat scale - could be added to more if it turned out to be useful
	F32					fScale;		AST( DEF(1) )

	S32					iUnidentifiedResultLevel;
	
}RewardEntry;
extern ParseTable parse_RewardEntry[];
#define TYPE_parse_RewardEntry RewardEntry

AUTO_STRUCT;
typedef struct RewardTableOverride
{
	// the reward flags only includes the reard overrides
	RewardFlag			flags;				AST(FLAGS)

	// override pickup type
	RewardPickupType	PickupType;
	
}RewardTableOverride;

AUTO_STRUCT AST_IGNORE(Tags) AST_IGNORE_STRUCT(UGCProperties);
typedef struct RewardTable
{
	const char	*pchName;				AST( POOL_STRING STRUCTPARAM KEY )
	char		*pchFileName;			AST( CURRENTFILE )
	const char	*pchScope;				AST( POOL_STRING )
	char		*pchNotes;

	RewardAlgorithm Algorithm;
	int				NumChoices;			AST( DEF(1) )
	RewardFlag		flags;				AST(FLAGS)

	RewardKillerType	KillerType;
	RewardExecuteType	ExecuteType;
	RewardPickupType	PickupType;
	RewardOwnerType		OwnerType;		AST(DEF(kRewardOwnerType_Player))
	RewardLaunchType	LaunchType;
	int					NumPicks;

	F32				 LingerTime;		AST( DEF(300) )

	REF_TO(PlayerCostume) hNotYoursCostumeRef;	AST(REFDICT(PlayerCostume) NAME(NotYoursCostume))
	REF_TO(PlayerCostume) hYoursCostumeRef;		AST(REFDICT(PlayerCostume) NAME(YoursCostume))
	RewardEntry	**ppRewardEntry;
	int dummyVar;
	
	// value for the reward table scaling used for all items in the reward table, this is
	// multiplied with the entry's  fRewardDifficultyPowerScaleMultiplier to get a final value (defailt 1.0)
	F32 fRewardDifficultyPowerScale;	AST(NAME(RewardDiffPowerScale) DEF(1.0))

	// The list of tags for this reward table
	S32 *piRewardTags;					AST( NAME(RewardTags), SUBTABLE(RewardTagsEnum) )

	// This can't go into the flags field due to the way new RewardBags are directly compared to their originating table's flags.
	U32 bShowRewardPackUI : 1;

	// This is the overall "quality" of this table, independent from the quality of any granted items.
	ItemQuality eRewardPackOverallQuality;		AST(DEFAULT(-1))

	// Gated type
	RewardGatedType eRewardGatedType;

}RewardTable;
extern ParseTable parse_RewardTable[];
#define TYPE_parse_RewardTable RewardTable

AUTO_STRUCT;
typedef struct RewardTableRef
{
	REF_TO(RewardTable) hRewardTable; AST(STRUCTPARAM)
} RewardTableRef;

AUTO_STRUCT;
typedef struct CritterLootOwner
{
	RewardOwnerType eOwnerType;
	int *peaiOwnerIDs;
} CritterLootOwner;

AUTO_STRUCT;
typedef struct RewardBagInfo
{
	RewardFlag			flags;
	RewardExecuteType	ExecuteType;
	RewardPickupType	PickupType;
	RewardOwnerType		OwnerType;
	RewardLaunchType	LaunchType;
	F32					LingerTime;
	int					NumPicks;
	LootMode			loot_mode;
	ItemQuality			eLootModeThreshold;
	const char*			pcRank;				AST(POOL_STRING) // Used for logging
	const char*			pcRewardTable;		AST(POOL_STRING) // Used for error details
	int					LevelDelta; // + means killed was higher than killer
	int					nextID;	//for assigning IDs to reward items
	U32 bShowRewardPackUI : 1;				AST(NAME(ShowRewardPackUI))
	ItemQuality			eRewardPackOverallQuality;
	CritterLootOwner **peaLootOwners;

	REF_TO(PlayerCostume) hNotYoursCostumeRef; AST(REFDICT(PlayerCostume) NAME(NotYoursCostume))
	REF_TO(PlayerCostume) hYoursCostumeRef;  AST(REFDICT(PlayerCostume) NAME(YoursCostume))

}RewardBagInfo;


AUTO_STRUCT;
typedef struct RewardValTable
{
	char* Name; AST(POOL_STRING STRUCTPARAM KEY)
	const char* pcRank; AST(STRUCTPARAM POOL_STRING)
	const char* pcSubRank; AST(STRUCTPARAM POOL_STRING)
	RewardValueType ValType; AST(STRUCTPARAM)
	bool shouldIndex;
	F32 *Val;
	F32 *Adj;
}RewardValTable;
AUTO_STRUCT;
typedef struct XPdata
{
	RewardValTable **xpTables;
	F32 typeStarts[4];	//store the first occurrance of each value type
}XPdata;


AUTO_STRUCT;
typedef struct KillCreditEntity
{
	EntityRef entRef;

	S32 iContainerID;			//Player Container ID

	// Raw stats
	F32 fTotalDamageSelf;      // Total damage I've done
	F32 fMyTeamDamageShare;    // My split of the team damage

	// Percentage values
	F32 fPercentOfTeamCredit;  // Percentage of team's credit I earned
	F32 fPercentCreditSelf;    // Percentage of credit I earned
	F32 fPercentCreditTeam;    // Percentage of credit I should get if teams count

	bool bFinalBlow;			// If my damage was the final blow

	// Binary "HasCredit" values for Mission Credit
	bool bHasCredit;
	bool bHasTeamCredit;

	CharacterBasedIncludeContext CBIData; AST(STRUCT(parse_CharacterBasedIncludeContext))
} KillCreditEntity; 


AUTO_STRUCT;
typedef struct KillCreditTeam
{
	U32 iTeamID;
	bool bTeamUp;
	EntityRef firstMemberEntRef;

	F32 fTotalTeamDamage;
	F32 fTotalTeamPercentage;
	U32 iTotalLevels;
	KillCreditEntity **eaMembers;

	bool bHighestDamager;
	F32 fTotalCombatValue;			// combat value of all team members added together
	U32 iTeamCombatLevel;			// average combat level of the team
	int	iValidTeamSize;				// number of members in team that are qualified for this kill (in level range)

} KillCreditTeam;

AUTO_STRUCT;
typedef struct ItemSummary
{
	const char *pchName;
	bool bAlgo;
	int iNumericValue;
	ItemQuality Quality;
} ItemSummary;

AUTO_STRUCT;
typedef struct InventoryBagSummary
{
	const char *pcTargetRank;
	ItemSummary **ppItemSummaries;
} InventoryBagSummary;

AUTO_STRUCT;
typedef struct RewardGenericNumeric
{
	// The name of the numeric
	const char *pcRewardGenericName;			AST(POOL_STRING KEY)
	
	// the bonus types that are affected
	RewardBonusType eRewardGenericBonus;		
	
	// The type of bonus for kills, used to find the correct table indexed by rank
	RewardValueType eRewardGenericKillType;		
	
	// reward table name for missions
	const char *pcRewardGenericMission;			AST(POOL_STRING)
	
	// reward table name for power exec
	const char *pcRewardGenericPower;			AST(POOL_STRING)
	
	// is the mission numeric value scaled by difficulty?
	bool bRewardGenericMissionDifficultyScaled;	

}RewardGenericNumeric;

AUTO_STRUCT;
typedef struct RewardModifications
{
	// the table to use for determining the power at level x
	char *pcCombatValueLevelTable;		AST(NAME(CombatValueLevelTable) POOL_STRING)

	// if true use the CombatMod_getMod table to modify the combat value
	bool bModifyByDifficulty;			AST(NAME(ModifyByDifficulty))

	// Optional table used to modify team damage based on target killed level.
	// This is used to prevent high level team from stealing from low level team
	// Only used when the target killed is lower level than the average of the killing team
	//
	// DANGER! Using this may have subtle and extremely undesirable effects
	//         on how players play the game. Using it might encourage twinking
	//         by splitting teams into singles.  The high-level character does
	//         most or all the work. The low-level character does just
	//         a little, but because of this modification gets the loot.
	//
	//         Don't use this unless you've taken that into account in
	//         some other way and are sure it works.
	//
	char *pcItemRewardModificationTable;		AST(NAME(ItemRewardModificationTable) POOL_STRING)
		
	// 
	// if true then all items generated can have additional powers
	// based on Reward_Difficulty_Power_Generic_xxx
	bool bUseGenericDifficultyPowerScaling;

	// extra reward numeric for algonumerics 
	CONST_EARRAY_OF(RewardGenericNumeric) eaRewardExtraAlgoNumerics;

	// name of the optional static map reward table
	const char *pcStaticMapRewardTableName;		AST(POOL_STRING)

	// the maximum number of levels lower that a will allow the static map reward table to be
	S32 iStaticMapRewardTableLevelDiffMax;

	// Shard variable that must be set
	const char *pcStaticMapRewardTableShardVariable;		AST(POOL_STRING)
	// and the int value it must equal
	S32 iStaticMapRewardTableShardValue;					AST(POOL_STRING)
	
	// Non-numeric Direct rewards send notify message key
	const char *pDirectRewardNotifyMessageKey;				AST(POOL_STRING)

} RewardModifications;

AUTO_STRUCT;
typedef struct RecruitMods
{
	F32 fRecruitOrRecruiter;     AST(DEFAULT(1.0f))
		F32 fNewRecruitOrRecruiter;  AST(DEFAULT(1.0f))
} RecruitMods;

AUTO_STRUCT;
typedef struct MissionModsScalingData{
	MissionCreditType eType;   AST(STRUCTPARAM)
	F32 fXPScale;       AST(DEFAULT(1.0f))
	F32 fRPScale;       AST(DEFAULT(1.0f))
	F32 fResourceScale; AST(DEFAULT(1.0f))
	F32 fStarsScale;    AST(DEFAULT(1.0f))
	F32 fSPScale;       AST(DEFAULT(1.0f))
	F32 fOSPScale;      AST(DEFAULT(1.0f))
} MissionModsScalingData;

// How much to scale each numeric type for secondary mission rewards
AUTO_STRUCT;
typedef struct MissionMods{
	// These default values apply to any non-Primary mission
	// if there is no MissionModsScalingData for the specific CreditType
	F32 fSecondaryXPScale;       AST(DEFAULT(1.0f))
	F32 fSecondaryRPScale;       AST(DEFAULT(1.0f))
	F32 fSecondaryResourceScale; AST(DEFAULT(1.0f))
	F32 fSecondaryStarsScale;    AST(DEFAULT(1.0f))
	F32 fSecondarySPScale;       AST(DEFAULT(1.0f))
	F32 fSecondaryOSPScale;      AST(DEFAULT(1.0f))

	// Scaling values for each MissionCreditType
	MissionModsScalingData** eaScalingData;  AST(NAME("ScalingData"))
} MissionMods;

AUTO_STRUCT;
typedef struct TeamMods
{
	//int TeamMods[MAX_TEAM_SIZE];  #define not available in this scope, hard code for now
	F32 TeamMods[5];
} TeamMods;

AUTO_STRUCT;
typedef struct UGCRewardConfig
{
	F32 fMinimumRewardScale;
	F32 fMaximumRewardScale;
	F32 fMinimumRewardTime;
	F32 fMaximumRewardTime;
} UGCRewardConfig;

AUTO_STRUCT;
typedef struct UGCNonQualifyingRewardConfig
{
	F32 fLowRewardScale; // This reward scale is given when average time is at least fThresholdForZeroTime, but less than fThresholdBetweenLowAndHighTime
	F32 fHighRewardScale; // This reward scale is given when average time is at least fThresholdBetweenLowAndHighTime
	F32 fThresholdBetweenLowAndHighTime; // this is the time between which we jump from fLowRewardScale to fHighRewardScale
	F32 fThresholdForZeroTime; // below this time, we will always use reward scale of zero. This must be less than fThresholdBetweenLowAndHighTime
} UGCNonQualifyingRewardConfig;

AUTO_STRUCT;
typedef struct OriginPickupTypeStruct
{
	RewardContextType eOrigin;	AST(STRUCTPARAM)
	RewardPickupType ePickup;	AST(STRUCTPARAM)

} OriginPickupTypeStruct;

AUTO_STRUCT;
typedef struct OriginToPickupTypeMappings
{
	EARRAY_OF(OriginPickupTypeStruct) eaOriginToPickupType;		

} OriginToPickupTypeMappings;

AUTO_STRUCT;
typedef struct CatToCostumeStruct
{
	ItemCategory eCat; AST(STRUCTPARAM)
	REF_TO(PlayerCostume) hCostume;	AST(STRUCTPARAM)
} CatToCostumeStruct;

AUTO_STRUCT;
typedef struct TypeToCostumeStruct
{
	ItemType eType;	AST(STRUCTPARAM)
	EARRAY_OF(CatToCostumeStruct) eaCats; AST(NAME(CatToCostume))
	REF_TO(PlayerCostume) hDefault;	AST(NAME(Default))
} TypeToCostumeStruct;

AUTO_STRUCT;
typedef struct TypeToCostumeMappings
{
	EARRAY_OF(TypeToCostumeStruct) eaTypeToCostume;		
	REF_TO(PlayerCostume) hYoursDefault;	AST(NAME(DefaultYours))
	REF_TO(PlayerCostume) hNotYoursDefault;	AST(NAME(DefaultNotYours))
	StashTable stTypeToCostume;	NO_AST
} TypeToCostumeMappings;

AUTO_STRUCT;
typedef struct LifetimeRewardStruct
{
	// how many days to apply reward?
	S32 iLifetimeRequiredDays;

	// lowest level that reward applies to
	S32 iLifetimeRewardLowLevel;
	
	// Highest level reward applies to
	S32 iLifetimeRewardHighLevel;
	
	// What percent experience does the player get? Note that only the highest modifier is used.
	F32 fLifetimeRewardModifier;		AST(DEFAULT(1.0f))
	
	// what type of reward to apply this to
	char *pchLifetimeRewardType;		AST( POOL_STRING )

} LifetimeRewardStruct;


AUTO_STRUCT;
typedef struct LifetimeRewardsInfo
{
	EARRAY_OF(LifetimeRewardStruct) eaLifetimeReward;		

} LifetimeRewardsInfo;

AUTO_STRUCT;
typedef struct ExperienceGift
{
	// Character must be this level to fill
	S32 uRequiredLevelToFill;	

	// Character must be this level or lower to receive experience
	S32 uMaxGiveLevel;	

	// How much exp to give 1 experience in item
	U32 uConversionRate;	

}ExperienceGift;

AUTO_STRUCT;
typedef struct RewardGatedTypeName
{
	const char *pcName;

}RewardGatedTypeName;

AUTO_STRUCT;
typedef struct RewardGatedTypeNames
{
	EARRAY_OF(RewardGatedTypeName) eaNames;

}RewardGatedTypeNames;

AUTO_STRUCT;
typedef struct RewardGatedInfo
{
	RewardGatedType eRewardGateType;		AST(KEY)

	// How many hours in a block, 0 == an infinate block size that is only reset by commands or the reward entry bResetGatedTable or by uResetAt
	U32 uHoursPerBlock;

	// How many times does it take to get to the next index of the reward table
	// 0 and 1 are the same (one increment each)
	U32 uNumberOfTimesToIncrement;

	// If uNumberOfTimesToIncrement hits this value the time and count is reset to zero
	U32 uResetAt;

	bool bGateFromPlayerGrantTime;
}RewardGatedInfo;

AUTO_STRUCT;
typedef struct RewardOddsItem
{
	const char	*pcItemName;					AST(KEY POOL_STRING)
	F32			fTotalQuantity;
	
}RewardOddsItem;


AUTO_STRUCT;
typedef struct RewardOddsTable
{
	// Generate odds for this table
	RewardTable *pRewardTable;			NO_AST

	// current odds for this table
	F32 fCurrentChance;

	// The deapth of this table
	U32 uDepth;

}RewardOddsTable;

AUTO_STRUCT;
typedef struct RewardOddsEntry
{
	F32 fChance;					// 0 to 1
	U32 uDepth;						// Depth of the reward table when this is generated

	// the reward entry that will be generated
	RewardEntry *pRewardEntry;			

	// Table that this was generated from
	RewardTable *pRewardTable;			NO_AST

	// Does this entry need to be run in the system (i.e. its another table)
	bool bHasRewardTable;

	// This is an item. Used for output
	bool bIsItem;

}RewardOddsEntry;


AUTO_STRUCT;
typedef struct RewardOdds
{
	// The starting reward table
	RewardTable *pStartingTable;		NO_AST

	// The current reward table
	RewardTable *pCurrentTable;			NO_AST

	// The reward context when a character is used
	RewardContext *pRewardContext;		NO_AST

	// For use with character
	Entity *pPlayerEnt;					NO_AST

	// current depth of tables
	U32 uDepth;					

	// current partition that character is being run on
	S32 iPartition;

	// The final total of all items generated
	// If the items are all given out at 1 each the total should be 1.0
	F32 fTotalItemsGiven;

	// total numerics given
	F32 fTotalNumericsGiven;
											
	// All of the reward entries and their chance to be generated
	EARRAY_OF(RewardOddsEntry)	eaEntries;								

	// All of items and the quantity that they will average
	EARRAY_OF(RewardOddsItem)	eaItems;								

	// Tables that need generation
	EARRAY_OF(RewardOddsTable)	eaRewardTables;

	// Is this struct initialized?
	bool bInitialized;		

	// Some really complex tables where the value returned will be meaningless
	bool bUnableToCalculate;

}RewardOdds;

AUTO_STRUCT;
typedef struct RewardBlockRegionTable
{
	// String instead of ref due to load order
	const char *pcRewardTable;					AST(NAME(RewardTable))

}RewardBlockRegionTable;

AUTO_STRUCT AST_IGNORE(AllowOneItemNoWeightTable);
typedef struct RewardConfig
{
	RewardModifications Modifications;
	MissionMods MissionMods;
	TeamMods TeamMods;
	RecruitMods RecruitMods;
	UGCRewardConfig UGCRewardConfig;
	UGCNonQualifyingRewardConfig UGCNonQualifyingRewardConfig;
	OriginToPickupTypeMappings OriginMappings;
	TypeToCostumeMappings TypeCostumeMappings;

	bool bIgnoreSubRank; // If true, ignore subrank for reward value tables
	bool bComputeRewardQuality; // If true, compute Item Quality based on the drop rate tables for STO
	
	// the rewards bonuses for long time subscribers, this is read in
	LifetimeRewardsInfo lifetimeRewards;

	// the fixed up list (by numeric type)
	// This is created after load to sort type
	EARRAY_OF(LifetimeRewardsInfo) eaLifetimeRewardsList;

	// By default all numerics in a reward table are granted to all team members (such as XP). These are the list of numerics
	// which will be evaluated as all other types of rewards to see if they need to be given to all team members. It is still
	// possible that the numerics listed here may be given to all team members if kRewardFlag_DupForTeam flag is on for the reward.
	const char **ppchNumericsNotGivenToWholeTeam;		AST(NAME(NumericsNotGivenToWholeTeam), POOL_STRING)

	// The experience gift data
	ExperienceGift GiftData;

	EARRAY_OF(RewardGatedInfo) eRewardGateInfo;

	// An earray of reward tables that will prevent region tables from being added
	EARRAY_OF(RewardBlockRegionTable) eaBlockRegionTables;

	// By default each item dropped from a kill is distributed via round robin. If this flag is on, assuming there are 2 members in a team,
	// first kill goes to first player, second goes to second player, third goes to first player etc.
	U32 bRoundRobinEachKill : 1;

	// This flag applies to add op only numerics which are explicitly looted (interactable loots). 
	// If this flag is on and a team member loots this type of numeric, the numeric is shared between team members.
	U32 bShareLootedNumericsWithTeamMembers : 1;

	// This flag will prevent ItemOpenRewardPack_CB from doing ClientCmd_gclReceiveRewardPackData. Used on CO for reward packs 
	// as CO wants a simpler UI
	U32 bBlockRewardPackClientSend : 1;

	// A CO flag to prevent any sort of star lose in a powerhouse
	U32 bNoDeathPenaltyInPowerHouse : 1;

	// When set, interacting with a "loot ent" (created by Drop or Rollover tables) will auto-takeall instead of opening a prompt.
	U32 bLootEntsAlwaysAutoLoot : 1;

	// When set, instead of attempting to recursively weight table entries (which is a horrible, insane idea), reward table entries will use the weight they were given by designers.
	U32 bUseEntryWeightsOnly : 1;

	U32 bCheckMinMaxLevelForAllEntries : 1;
	// When set, all reward entries will be checked against min/max level instead of just range table entries.

	U32 bUseUGCRewardConfig : 1;
	// When set, uses special UGC reward configuration rules for reward scaling in UGC missions

	U32 bUseNonQualifyingUGCRewardConfig : 1;

	U32 bUseRewardGatingMissions : 1;
	// Allow the use of rewardgated missions (transacted)

} RewardConfig;

extern RewardConfig g_RewardConfig;
extern XPdata g_XPdata;
extern DictionaryHandle g_hRewardValTableDict;
extern DictionaryHandle g_hRewardTierDict;
extern DictionaryHandle g_hDropRateTableDict;
extern DictionaryHandle g_hDropSetDict;
extern DictionaryHandle g_hRewardTableDict;


//the choice between REl 0 and Rel 1 level numbers have been a source of confusion in the past
//level entered in data files and displayed to user is Rel 1, since most non-programmers think this way
//internal data tables are indexed Rel 0

//these defines seem totally un-necessary but they make the code more readable
//and also confine this conversion to one place

#define NUM_PLAYER_LEVELS  MAX_LEVELS

#define USER_TO_TAB_LEVEL(iLevel) ((iLevel)-1)
#define TAB_TO_USER_LEVEL(iLevel) ((iLevel)+1)

#define MIN_USER_LEVEL 1
#define MAX_USER_LEVEL (NUM_PLAYER_LEVELS)

#define MIN_TAB_LEVEL USER_TO_TAB_LEVEL(MIN_USER_LEVEL)
#define MAX_TAB_LEVEL USER_TO_TAB_LEVEL(MAX_USER_LEVEL)

//level entered in data files and displayed to user is Rel 1
#define USER_LEVEL_VALID(iLevel) (((iLevel) >= (MIN_USER_LEVEL)) && ((iLevel) <= (MAX_USER_LEVEL)))
#define CLAMP_USER_LEVEL(iLevel) CLAMP((iLevel), (MIN_USER_LEVEL), (MAX_USER_LEVEL))

//level used to reference tables is Rel 0
#define TAB_LEVEL_VALID(iLevel) (((iLevel) >= (MIN_TAB_LEVEL)) && ((iLevel) <= (MAX_TAB_LEVEL)))
#define CLAMP_TAB_LEVEL(iLevel) CLAMP((iLevel), (MIN_TAB_LEVEL), (MAX_TAB_LEVEL))

#define NUMERIC_AT_LEVEL(iLevel) LevelingNumericFromLevel(iLevel)

//Magic numbers used to get adjustment values from tables
//there are currently 11 adjustment numbers based on level difference, -5, 0, +5
#define REWARDVALTABLE_ADJUST_ZERO_IDX 5
#define REWARDVALTABLE_ADJUST_MIN_IDX 0
#define REWARDVALTABLE_ADJUST_MAX_IDX 10


// RewardCommon.c
bool reward_BagIsMyDrop(Entity* pEnt, InventoryBag* pBag);
bool reward_MyDrop(Entity *pEnt, Entity *pLootEnt);
S32 LevelFromLevelingNumeric(S32 XP);
S32 LevelingNumericFromLevel(S32 iLevel);


// RewardTable.c
bool rewardTable_HasItemsWithType(RewardTable *pTable, RewardPickupType type, RewardContextType ctxtType);
void rewardTable_GetAllGrantedItemsWithType(RewardTable *pTable, ItemType kItemType, ItemDef*** peaItemDefsOut);
bool rewardTable_HasExpression(RewardTable *pTable);
bool rewardTable_HasAlgorithm(RewardTable *pTable, RewardAlgorithm algo);
bool rewardTable_HasUnsafeDirectGrants(RewardTable *pTable, RewardContextType ctxtType);
bool rewardTable_Validate(RewardTable *pTable);

void rewardentry_GetAllPossibleCharBasedTableNames(RewardEntry* entry, char*** peaNames);

bool rewardTable_HasMissionItems(RewardTable *pTable, bool bIgnoreMissionGrant, MissionDef* pMissionDef);
	// Checks whether this reward table has any Mission Items (items that have a Mission reference on them)

bool rewardTable_HasUniqueItems(RewardTable *pTable);
	// Checks whether this reward table has any Items flagged as Unique

bool rewardTable_HasCharacterBasedEntry(RewardTable *pTable);
bool rewardEntry_UsesCountExpression(RewardEntry * pEntry);

bool rewardTable_HasNonGems(RewardTable *pTable);

S32 Reward_GetGatedIndexTransacted(RewardGatedDataInOut *pRewardGatedData, S32 gatedType);
	// the gated index in a transaction

S32 Reward_GetGatedIndex(Entity *pPlayerEnt, S32 gatedType);
	// return the index to used for reward gated type 

/* 
   Note to any programmers adding more recursive reward table validation:
  
   Please look at the comment above rewardTable_RunCallbackOnTableRecursive() 
  in rewardtable.c to see what you need to do. Doing simple recursion will 
  cause a stack overflow because rewardtables themselves can be recursive.

   Thanks!
   ~Cmiller
*/

// RewardValTable.c
RewardValTable* rewardvaltable_Lookup(const char *pcRank, const char *pcSubRank, RewardValueType Type);

RewardPickupType reward_GetPickupTypeFromRewardOrigin(const RewardTable* pTable, RewardContextType eType);

int rewardTableResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, RewardTable *pReward, U32 userID);

bool reward_SetLootEntCostumeRefsForItem(Item* pItem, Entity* pEnt);

// Get the coolodwn for this reward gated type for this player ent
U32 Reward_GetGatedCooldown(Entity *pPlayer, S32 gatedType);

#endif /* #ifndef REWARDCOMMON_H__ */

/* End of File */
