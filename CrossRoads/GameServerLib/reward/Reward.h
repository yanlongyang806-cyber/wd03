#ifndef REWARD_H
#define REWARD_H

#include "mission_enums.h"
#include "entEnums.h"
#include "rewardcommon.h"
#include "objTransactions.h"
#include "itemGenCommon.h"
#include "itemGenCommon_h_ast.h"


typedef struct Entity Entity;
typedef struct Mission Mission;
typedef struct MissionDef MissionDef;
typedef struct Reward Reward;
typedef struct Critter Critter;
typedef struct CritterLootOwner CritterLootOwner;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct ItemChangeReason ItemChangeReason;
typedef struct ItemDef ItemDef;
typedef struct ItemPowerDef ItemPowerDef;
typedef struct PlayerCostume PlayerCostume;
typedef struct UGCProject UGCProject;
typedef struct InventoryBag InventoryBag;
typedef struct RewardTable RewardTable;
typedef struct RewardModifierList RewardModifierList;
typedef struct RewardEntry RewardEntry;
typedef struct GiveRewardBagsData GiveRewardBagsData;
typedef struct ZoneMap ZoneMap;
typedef struct ExprContext ExprContext;
typedef struct NOCONST(Entity) NOCONST(Entity);

#define MAX_RECURSE_DEPTH	32

#define NUM_CRITTER_RANKS	6
#define NUM_CRITTER_SUBRANKS	3



AUTO_ENUM;
typedef enum RewardNumericType
{
	kRewardNumeric_XP,
	kRewardNumeric_Rep,				
	kRewardNumeric_EconomyPoints,
	kRewardNumeric_Resource,
	kRewardNumeric_Ingenuity = kRewardNumeric_Resource,
	kRewardNumeric_Total,
}RewardNumericType;


AUTO_ENUM;
typedef enum RewardEventType
{
	kRewardEvent_Mission,
	kRewardEvent_Critter,
	kRewardEvent_Total,
}RewardEventType;



AUTO_ENUM;
typedef enum RecruitType
{
	kRecruitType_None = 0,
	kRecruitType_New = 1 << 0,
	kRecruitType_Recruit =   1 << 1,
	kRecruitType_Recruiter = 1 << 2,

	// Convenience enums
	kRecruitType_NewRecruit = kRecruitType_New|kRecruitType_Recruit,
	kRecruitType_NewRecruiter = kRecruitType_New|kRecruitType_Recruiter,
	kRecruitType_NewRecruitOrRecruiter = kRecruitType_New|kRecruitType_Recruit|kRecruitType_Recruiter,
	kRecruitType_RecruitOrRecruiter = kRecruitType_Recruit|kRecruitType_Recruiter,

	kRecruitType_Count, EIGNORE

} RecruitType;

typedef struct AlgoRewardContext 
{
	ItemDef **ppBaseItems;
	ItemPowerDef **ppExtras;
	PlayerCostume **ppCostumes;
} AlgoRewardContext;

AUTO_STRUCT;
typedef struct RewardNumericScale
{
	const char* pchNumericItem; AST(KEY POOL_STRING)
	F32 fScale;
} RewardNumericScale;

//this data is passed from the reward adapter code, down through all levels of the reward system code
//this allows the innermost nested code to have info about where the reward was generated.
//this will probably be ignored by most code, but this will support the case where the reward code needs to
//skew the rewards based on it, (for example, having rewards differ based on the type of player character)
// NOTE: this gets passed through transactions. do not refer to large containers.
// This is auto struct enabled just for expression
//
// NOTE THE SECOND: Per Stephen's request, do not add AST(DEF()) values to this struct. 
//  Instead, initialize default values in the constructor: Reward_InitRewardContext().
AUTO_STRUCT;
typedef struct RewardContext
{
	RewardContextType type;
	int    depth;
	bool killerIsPlayer;
	Entity *pKilled;				AST(UNOWNED)
	int    KillerLevel;
	int    RewardLevel;
	const char *pcRank;				AST(POOL_STRING)
	const char *pcSubRank;			AST(POOL_STRING)
	ItemQuality	   Quality;
	U32	   RewardQuality;			AST(NAME(RewardQuality) SUBTABLE(ItemGenRarityEnum)) // New rarity value
	F32    RewardScale;
	F32	   TeamMemberPercentage;
	bool   bGiveItems;
	bool   bBestTeam;
	int	   TeamSize;
	int    SkillLevel;
	int	   EP;
	int	   TimeLevel;
	bool   bSidekicked;
	AlgoRewardContext *pAlgoRewards;	NO_AST
	S32    iKillerCombatLevel;					// the killers actual combat level
	U32    iTeamRealSize;						// size of the team based on qualifications
	U32    iTeamCombatLevel;					// combat level of the team
	S32    iAlgoItemLevel;						// if non-zero used instead of KilledLevel
	S32    iPointBuyRemaining;					// Number of points left to spend on the current item.
	S32    iPointBuyMinRange;
	S32    iPointBuyMaxRange;
	bool bBaseItemsOnly;
	bool bShowRewardPackUI;
	ItemQuality eRewardPackOverallQuality;
	RecruitType eRecruitTypes;					// A set of flags which describe what kind of recruits are in the team.
	const char* pcMission;				AST(POOL_STRING) // MissionDef ref string for a mission if this reward was granted from a mission
	S32    iPlayerLevelForItem;					// Only set when reward table indicates to use player level
	MissionCreditType eMissionCreditType;		// The Credit Type for the mission if this reward is from a mission 
	LootMode lootMode;							// loot mode of the team, used for deciding to create separate bags
	U32 iKillerSubscriptionDays;				// Number of days player has been a subscriber
	U32 iPlayerDifficultyIdx;					// The difficulty for this reward 0 == none
	S32 *piRewardTags;							// The list of reward tags
	bool bWouldGiveItems;						// This character would get items and will if kRewardFlag_PlayerKillCreditAlways is set on the reward table
	S32 iForceSoloOwnershipContainerID;			// If set, all rewards generated will be forcibly owned by this container ID and nobody else.
	RewardModifier **eaRewardMods;				// A list of reward modifiers from the combat system
	RewardNumericScale **eaNumericScales;		// List of numerics with scale values

	KillCreditTeam * pTeamCredit;	NO_AST
	GameAccountDataExtract *pExtract; NO_AST
	CharacterBasedIncludeContext* pCBIData;	NO_AST

	// if set then these override reward table during bag creation
	REF_TO(PlayerCostume) hNotYoursCostumeRef;	AST(REFDICT(PlayerCostume) )
	REF_TO(PlayerCostume) hYoursCostumeRef;		AST(REFDICT(PlayerCostume) )

}RewardContext;


extern const char* RankNames[NUM_CRITTER_RANKS];
extern const char* SubRankNames[NUM_CRITTER_RANKS];


AUTO_ENUM;
typedef enum LootInteractType
{
	LootInteractType_Ent, 
	LootInteractType_Clickable, 
}LootInteractType;

AUTO_STRUCT;
typedef struct LootInteractCBData{
	int                 iPartitionIdx;
	LootInteractType	type;
	EntityRef			PlayerEntRef;
	ContainerID			uiTeamID;
	EntityRef			LootEntRef;
	U64					iItemID;
	int					iItemCount;
	char				*pcLootedItemsLogList;
	const char*			pcInteractableNodeName;	AST(UNOWNED)
} LootInteractCBData;

// *************************************************************************
// Drop System: 
// - DropRate decides the rate at which quality-based loot is dropped level (tier), and critter class.
// - DropSet decides by crittergroup what itemgen sets to pick from for rewarding.
// 
// e.g. player kills a level 3 Gnoll minion:
// 1. Drop Rate:
//  - look up the drop rates for a tier 3 minion: white = .176%, green = .157%
//  - roll the rates: get green
// 2. Drop Set 
//  - gnolls drop: handaxe, spear, shield, leatherarmor
//  - weight pick: handaxe
//  - run the rewardtable: handaxe_green
// 
// NOTE: the permutations of the elements in a dropset and the rarities (white, green, etc.)
//       are assumed to exist externally and are currently made through the itemgen system.
// *************************************************************************

AUTO_STRUCT;
typedef struct RewardTier
{
    char *name; AST(STRUCTPARAM KEY )
	char *file; AST( CURRENTFILE )
    int min_level; AST(STRUCTPARAM)
    int max_level; AST(STRUCTPARAM)
} RewardTier;

AUTO_STRUCT;
typedef struct DropRateQuality
{
	ItemGenRarity	quality;	AST(STRUCTPARAM) 
    F32				rate;		AST(NAME(Rate)	STRUCTPARAM)
} DropRateQuality;

AUTO_STRUCT;
typedef struct DropRateTier
{
    REF_TO(RewardTier)  tier;			AST(STRUCTPARAM KEY NAME(Tier)	NON_NULL_REF)
    DropRateQuality		**qualities;	AST(NAME(Entry))
} DropRateTier;

AUTO_STRUCT;
typedef struct DropRateTable
{
    char          *name;  AST( POOL_STRING STRUCTPARAM KEY )
	char          *file;  AST( CURRENTFILE )
    DropRateTier **tiers; AST( NAME(Tier))
} DropRateTable;


// ----------------------------------------
// Drop Set

AUTO_STRUCT;
typedef struct DropSetElt
{
    char *name;  AST( STRUCTPARAM KEY POOL_STRING)
    F32  weight; AST( STRUCTPARAM )
} DropSetElt;

AUTO_STRUCT;
typedef struct DropSet
{
    char          *name;  AST( POOL_STRING STRUCTPARAM KEY )
	char          *file;  AST( CURRENTFILE )    
    DropSetElt   **elts;  AST( NAME(DropSetElt) )
} DropSet;


// *************************************************************************
//  methods
// *************************************************************************

// AB NOTE: InventoryBag structures are used in the reward system, but it is
// not correct to think of them as the same structure. In the context of the reward system
// they:
// - are not transacted
// - don't obey any of the usual inventory bag destination rules 
// - don't stack (or, at least, all items in a stack are always treated as one)
// So: think of InventoryBags in the reward system as RewardBags for now, this will
// get changed at some point 12/13/10

InventoryBag * rewardbag_CreateEx(const char* pchRewardTable);
#define rewardbag_Create() rewardbag_CreateEx(NULL)
void rewardbag_AddItem(InventoryBag *rw_bag, Item* item, bool bSetID);
void rewardbag_RemoveItem(InventoryBag *rw_bag, int index, Item **res_item);
bool rewardbag_RemoveItemByID(InventoryBag *rw_bag, int id, Item **res_item, int* pCountOut);
int rewardbag_Size(InventoryBag *rw_bag);

void reward_KillCreditLimitTick(Entity* pEnt, F32 fElapsedTime);
F32 reward_CalculateKillCredit(Entity *pKilled, KillCreditTeam ***peaTeams);
void reward_Award( Entity *e,  Reward *pReward );
void reward_SetupTeamLootBag(int iPartitionIdx, InventoryBag *pBag, Team *pTeam, LootMode eMode);
void reward_UpdateLootGained(int iPartitionIdx, InventoryBag *pBag);
void reward_GiveBagCB(Entity *pEnt, InventoryBag *pRewardBag, Vec3 DropPos, bool bAutoLootInteract, Entity *pEntKilled, const ItemChangeReason *pReason, TransactionReturnCallback pFunc, void* pData);
#define reward_GiveBag(pEnt, pRewardBag, DropPos, bAutoInteract, pReason) reward_GiveBagCB(pEnt, pRewardBag, DropPos, bAutoInteract, NULL, pReason, NULL, NULL)
#define reward_GiveBagWithKilledEntity(pEnt, pRewardBag, DropPos, pEntKilled, pReason) reward_GiveBagCB(pEnt, pRewardBag, DropPos, false, pEntKilled, pReason, NULL, NULL)
Entity *reward_GiveInteractible(int iPartitionIdx, CritterLootOwner **peaOwners, InventoryBag *pRewardBag, Vec3 vDropPos);
//For player kill, only find the player reward table on the map
void reward_PlayerKill(Entity *pKilled, KillCreditTeam*** peaCreditTeams, F32 fTotalDamage);
void reward_EntKill(Entity *pKilled, KillCreditTeam*** peaCreditTeams, F32 fTotalDamage);
void reward_Load();
void reward_EntLootCB(Entity *pEnt, InventoryBag *pRewardBag, const ItemChangeReason *pReason, TransactionReturnCallback pFunc, void* pData);
#define reward_EntLoot(pEnt, pRewardBag, pReason) reward_EntLootCB(pEnt, pRewardBag, pReason, NULL, NULL)

// Generates reward bags for persisted stores
void reward_GenerateBagsForStore(int iPartitionIdx, Entity *pPlayerEnt, SA_PARAM_NN_VALID RewardTable* pTable, S32 iLevel, U32* pSeed, InventoryBag*** peaBags);

// Stuff for Random Mission generation
// Generates a bag for use in populating random variables in a MadLibs Mission
void reward_GenerateMissionVarsBag(int iPartitionIdx, Entity *pPlayerEnt, RewardTable *pTable, InventoryBag ***rewardBagList, int iLevel);

// Generates a bag for an interactable "treasure chest"
void reward_GenerateInteractableBag(int iPartitionIdx, Entity *pPlayerEnt, RewardTable *pTable, InventoryBag ***rewardBagList, int iLevel, int skillLevel, U32* pSeed, Entity* interactingEnt);

// Generates reward bags for item assignments
void reward_GenerateBagsForItemAssignment(int iPartitionIdx, Entity *pPlayerEnt, SA_PARAM_NN_VALID RewardTable* pTable, S32 iLevel, F32 fScale, RewardNumericScale **eaNumericScales, bool bUseRewardMods, U32* pSeed, InventoryBag*** peaBags);

// Generates reward bags for micro transactions
void reward_GenerateBagsForMicroTransaction(int iPartitionIdx, Entity *pPlayerEnt, SA_PARAM_NN_VALID RewardTable* pTable, InventoryBag*** peaBags);

// Generates reward bags for a reward pack item
void reward_GenerateBagsForRewardPack(int iPartitionIdx, Entity *pPlayerEnt, SA_PARAM_NN_VALID RewardTable* pTable, S32 iLevel, InventoryBag*** peaBags);

// Generates reward bags for a personal project
void reward_GenerateBagsForPersonalProject(int iPartitionIdx, Entity *pPlayerEnt, SA_PARAM_NN_VALID RewardTable* pTable, S32 iLevel, InventoryBag*** peaBags);

bool loot_InteractBegin(Entity *pEnt, InventoryBag *pLootBag, bool bClientUpdate, bool bForceSendToClient);
bool loot_InteractBeginMultiBags(Entity *pEnt, InventoryBag*** peaLootBags, bool bClientUpdate, bool bForceSendToClient, bool bForceAutoLoot);
void loot_InteractTakeAll(Entity* pPlayerEnt, S32 iBagID, bool bEndInteraction);
void loot_InteractTakeAllOfType(Entity* pPlayerEnt, S32 eItemType, S32 iBagID);
void loot_InteractTakeAllExceptType(Entity* pPlayerEnt, S32 eItemType, S32 iBagID);
void loot_InteractTake(Entity* pPlayerEnt, S32 iSlot, S32 iBagID, S32 iDstSlot, const ItemChangeReason *pReason);
void loot_InteractOnceCallback(TransactionReturnVal *returnVal, LootInteractCBData *pData);
void loot_InteractCallback(TransactionReturnVal *returnVal, LootInteractCBData *pData);

F32 reward_GetRewardScaleOverTime(S32 iElapsedTime, S32 iScaleRewardOverTimeMinutes);
void reward_GenerateMissionActionRewards(int iPartitionIdx, Entity *pPlayerEnt, MissionDef* missionDef, MissionState trigger, InventoryBag ***bags, U32* seed, MissionCreditType eCreditType,
	int mission_level, int time_level, U32 iMissionStartTime, U32 iMissionEndTime, RecruitType eRecruitType, bool bUGCProject, bool bMissionQualifiesForUGCReward, bool bMissionQualifiesForUGCFeaturedReward,
	bool bMissionQualifiesForUGCNonCombatReward, bool bSubMissionTurnin, bool bGenerateChestRewards, RewardContextData *pRewardContextData, F32 fAverageDurationInMinutes, RewardGatedDataInOut *pGatedData);
void reward_OpenMissionExec(int iPartitionIdx, Entity* pPlayerEnt, MissionDef* pRootMissionDef, RewardTable *pRewardTable, int level, F32 fRewardScale, Vec3 DropPos, const ItemChangeReason *pReason);

void reward_GiveBags(SA_PARAM_NN_VALID Entity *pPlayerEnt, bool bAutoLootInteract, SA_PARAM_NN_VALID InventoryBag ***peaRewardBags, const ItemChangeReason *pReason);
bool reward_PowerExec_GenerateBags(Entity* e, RewardTable *table, int level, F32 scale, U32 iDifficulty, U32* pSeed, InventoryBag ***peaBags);
void reward_PowerExec(Entity* pPlayerEnt, RewardTable *pRewardTable, int level, F32 fRewardScale, bool bInformClient, const ItemChangeReason *pReason);
void reward_LevelUp(Entity* pPlayerEnt, char* RewardTableName, int level);
void reward_DeathPenaltyExec(Entity* pEnt);
void reward_RespawnPenaltyExec(Entity* pEnt);

bool reward_InRange(RewardEntry *pRewardEntry, int Level);
bool reward_RangeTable(RewardTable *pRewardTable);
void reward_GetRangeTables(RewardTable *pRewardTable, int value, RewardTable ***peaTablesOut);
RewardTable *reward_GetRangeTableEx(RewardTable *pRewardTable, int value, U32 *pSeed);
#define reward_GetRangeTable(pRewardTable, value) reward_GetRangeTableEx(pRewardTable, value, NULL)
int reward_GetRangeTableType(RewardTable *pRewardTable);
// If bUseAll is true then all expressions are true while doing calculation
void reward_CalcWeightTotal(int iPartitionIdx, Entity *pPlayerEnt, RewardTable *pRewardTable, RewardContext *pContext, F32 *pTotal, int depth, bool bUseAll);
void reward_addentry(int iPartitionIdx, Entity *pPlayerEnt, RewardContext *pContext, RewardEntry *pRewardEntry, RewardTable *pRewardTable, InventoryBag ***pppRewardBags, 
	InventoryBag *pRewardBag, U32 *pSeed, bool bMissionKillReward, RewardTableOverride *pInRewardTableOverride, RewardGatedDataInOut *pGatedData);
void reward_generateEx(int iPartitionIdx, Entity *pPlayerEnt, RewardContext *pContext, RewardTable *pRewardTable, InventoryBag ***pppRewardBags, InventoryBag *pParentBag, U32 *pSeed, bool bMissionKillReward, RewardTableOverride *pInRewardTableOverride, RewardGatedDataInOut *pGatedData);
#define reward_generate(iPartitionIdx, pPlayerEnt, pContext, pRewardTable, pppRewardBags, pParentBag, pSeed) reward_generateEx(iPartitionIdx, pPlayerEnt, pContext, pRewardTable, pppRewardBags, pParentBag, pSeed, false, NULL, NULL)

void reward_generate_specialcase(RewardContext *pContext, const char *RewardTableName, InventoryBag ***pppRewardBags, InventoryBag *pParentBag );
void reward_AwardMissionDrops( RewardContext *pContext, Entity *pPlayerEnt, InventoryBag ***pppRewardBags, U32 seed);

void reward_CraftingContextInitialize(Entity* pEnt, RewardContext *pRewardContext, int iEPValue, int iSkillLevelOffset);

S32 reward_GetItemLevel(RewardContext *pContext);

void reward_logItem(RewardContext* pContext, Entity* pAssignee, Item* pItem, ItemDef* pItemDef, int num);
void reward_logBag(int iPartitionIdx, InventoryBag* pBag, RewardContext* pContext, Entity* pAssignee);
void reward_logDroprate(Entity* pEnt, bool droppedItem);

void reward_cullDuplicateUniquesFromBag(Entity *pKiller, InventoryBag* pBag);

// Set the killer reward context, U32 iKillerLevel, RecruitType recruitType are both optional and only used when pEntity is NULL (for transactions)
void SetKillerRewardContextEx(Entity *pEntity, RewardContext *pContext, U32 iKillerLevel, RecruitType recruitType, bool bIsPlayer);
#define SetKillerRewardContext(pEntity, pContext) SetKillerRewardContextEx(pEntity, pContext, 0, 0, false);

RecruitType GetRecruitTypes(Entity *pEntity);

// Helper for creating an item from a reward context
NOCONST(Item)* rewarditem_FromCtxt(RewardContext *pContext, char const *def_name, U32 *pSeed);

//Wrapper function to regenerate the reward mod list that hangs off pEnt->pPlayer
void rewards_RegenRewardModList(Entity *pEnt, RewardModifierList *pRewardModList, S32 bTest);

void LaunchLoot(Entity* e);

void reward_MapValidate(ZoneMap* pZoneMap);

void ent_trh_GetCharacterRewardContextInfo(CharacterBasedIncludeContext* pContext, ATH_ARG NOCONST(Entity)* pEnt);
#define  ent_GetCharacterRewardContextInfo(pContext, pEnt) ent_trh_GetCharacterRewardContextInfo(pContext, CONTAINER_NOCONST(Entity, pEnt))

void reward_FixupRewardItemIDs(InventoryBag*** peaBags);
// Needed by UGC:
void GrantRewardTable(Entity *pEnt, const char *reward_table_name);
bool reward_QualityShouldUseLootMode(ItemQuality eQuality, ItemQuality eThreshold);

// Generate reward odds
void reward_GenerateOdds(RewardTable *pTable, RewardOdds *pOddsOut, F32 fCurrentOdds, U32 uDepth, Entity *pUseEnt);

void reward_SendLootFXMessageToBagOwners(Entity* pTriggeringEnt, Entity* pLootEnt, InventoryBag* pBag);

RewardContext* Reward_CreateOrResetRewardContext(RewardContext* pContext);

#include "AutoGen/reward_h_ast.h"

#endif
