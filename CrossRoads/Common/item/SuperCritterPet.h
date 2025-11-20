#ifndef SUPERCRITTERPET_H
#define SUPERCRITTERPET_H

#include "referencesystem.h"
#include "itemCommon.h"
#include "rewardCommon.h"
#include "inventorycommon.h"

#include "Autogen/itemCommon_h_ast.h"

typedef struct PlayerCostume PlayerCostume;
typedef struct CritterDef CritterDef;
typedef struct DynFxInfo DynFxInfo;
typedef struct HeadshotStyleDef HeadshotStyleDef;
typedef struct EntitySavedSCPData EntitySavedSCPData;
typedef struct NOCONST(EntitySavedSCPData) NOCONST(EntitySavedSCPData);
typedef struct Entity Entity;
typedef struct NOCONST(Entity) NOCONST(Entity);

extern DictionaryHandle g_hSuperCritterPetDict;


AUTO_STRUCT;
typedef struct SCPEquipSlotDef
{
	InvBagIDs eID;							AST(STRUCTPARAM)
	ItemCategory* peCategories; 			AST(NAME(RestrictCategories) SUBTABLE(ItemCategoryEnum)) 
} SCPEquipSlotDef;

AUTO_STRUCT;
typedef struct SCPAltCostumeDef
{
	REF_TO(PlayerCostume) hCostume;			AST(REFDICT(PlayerCostume) NAME(Costume) STRUCTPARAM)
	REF_TO(DynFxInfo) hContinuingPlayerFX;	AST(NAME(ContinuingPlayerFX) REFDICT(DynFxInfo))
	DisplayMessage displayMsg;				AST(NAME(DisplayMsg) STRUCT(parse_DisplayMessage))

	//this is the level requirement to use this costume.  If we need later it could be a more 
	// complex requirement struct.
	U32 iLevel;								AST(NAME(Level))
} SCPAltCostumeDef;

AUTO_STRUCT;
typedef struct SuperCritterPetDef
{
	const char *pchName;					AST(NAME(Name), STRUCTPARAM, KEY, POOL_STRING)
	char *pchFileName;						AST( CURRENTFILE )
	REF_TO(CritterDef) hCritterDef;			AST(NAME(CritterDef) REFDICT(CritterDef))
	REF_TO(CharacterClass) hCachedClassDef;	AST(NO_TEXT_SAVE)
	SCPAltCostumeDef** ppAltCostumes;		AST(NAME(AltCostume))
	SCPEquipSlotDef** ppEquipSlots;			AST(NAME(EquipmentSlot))
	REF_TO(DynFxInfo) hContinuingPlayerFX;	AST(NAME(ContinuingPlayerFX) REFDICT(DynFxInfo))
	const char* pchStyleDef;				AST(NAME(HeadshotStyle))
	bool bLevelToPlayer;					AST(NAME(LevelToPlayer))
	const char *pchIconName;				AST(NAME(Icon) POOL_STRING )
} SuperCritterPetDef;

AUTO_STRUCT;
typedef struct SuperCritterPetConfig
{
	Expression* pExprTrainingDuration;			AST(LATEBIND NAME(TrainingDuration))
	Expression* pExprUnbindCost;				AST(LATEBIND NAME(PetUnbindCost))
	Expression* pExprGemUnslotCost;				AST(LATEBIND NAME(GemUnslotCost))
	REF_TO(RewardValTable) hRequiredXPTable;	AST(NAME(RequiredXPTable) REFDICT(RewardValTable))
	REF_TO(ItemDef) hRushTrainingCurrency;		AST(REFDICT(ItemDef))
	F32 fRushCostPerTrainingSecond;				AST(NAME(RushCostPerTrainingSecond))
	S32 iMinRushTrainingCost;					AST(NAME(MinRushTrainingCost))
	int iRenameCost;							AST(NAME(RenameCost))
	REF_TO(ItemDef) hRenamingCurrency;			AST(REFDICT(ItemDef))
	REF_TO(ItemDef) hUnbindingCurrency;			AST(REFDICT(ItemDef))
	REF_TO(ItemDef) hGemUnslottingCurrency;		AST(REFDICT(ItemDef))
	FLOAT_EARRAY eafMaxLevelsPerQuality;		AST(NAME(MaxLevelsPerQuality))
	F32 fLevelScalingStartsAtPlayerLevel;		AST(NAME(LevelScalingStartsAtPlayerLevel) DEFAULT(0))
	F32 fLevelsPerPlayerLevel;					AST(NAME(LevelsPerPlayerLevel) DEFAULT(1))
	F32 fPetXPMultiplier;						AST(NAME(PetXPGainMultiplier) DEFAULT(1.0))
	INT_EARRAY eaGemSlotUnlockLevels;
	INT_EARRAY eaEquipSlotUnlockLevels;
	
	// a list of categories that a passive power must have in order be evaluated for fake entity stats 
	// the power must have ALL the defined categories
	S32 *piFakeEntStatsPassiveCategories;		AST(NAME(FakeEntStatsPassiveCategories), SUBTABLE(PowerCategoriesEnum))
		
} SuperCritterPetConfig;
extern SuperCritterPetConfig g_SCPConfig;

Item* scp_GetActivePetItem(Entity* pPlayer, int idx);
bool scp_itemIsSCP(Item* pItem);
SuperCritterPet* scp_GetPetFromItem(Item* pItem);
SuperCritterPetDef* scp_GetPetDefFromItem(Item* pPetItem);
NOCONST(SuperCritterPet)* scp_CreateFromDef(SuperCritterPetDef* pDef, Item* pPetItem, int iLevel)
;
int scp_GetPetCombatLevel(SA_PARAM_OP_VALID Item* pItem);
const char* scp_GetPetItemName(Item* pItem);


int scp_GetPetNumUnlockedSkins(Item* pItem);

F32 scp_GetPetPercentToNextLevel(Item* pPetItem);

int scp_MaxLevel(Item* pPetItem);
bool scp_LevelIsValid(int iLevel, Item* pPetItem);
U32 scp_PetXPToLevelLookup(U32 uiCurXP, Item* pPetItem);
U32 scp_GetPetLevelAfterTraining(Item* pPetItem);
U32 scp_GetPetStartLevelForPlayerLevel(int playerLevel, SA_PARAM_OP_VALID Item* pPetItem);
F32 scp_GetTotalXPRequiredForLevel(int iLevel, Item* pPetItem);

U32 scp_EvalTrainingTime(Entity* pPlayerEnt, Item* pPetItem);
U32 scp_EvalUnbindCost(Entity* pPlayerEnt, Item* pPetItem);
U32 scp_EvalGemRemoveCost(Entity* pPlayerEnt, Item* pPetItem, ItemDef* pGemItemDef, const char* pchCurrency);

EntityRef scp_GetSummonedPetEntRef(Entity* pEnt);
SA_RET_OP_VALID Item* scp_GetSummonedPetItem(Entity* pEnt);


enumTransactionOutcome scp_trh_AwardActivePetXP(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, float delta, bool bonus);

const char* scp_GetActivePetDefName(Entity* pPlayer, int idx);
const char* scp_GetActivePetName(Entity* pPlayer, int idx);
U32 scp_GetActivePetTrainingTimeRemaining(Entity* pPlayer, int idx);
bool scp_trh_IsAltCostumeUnlocked(ATH_ARG NOCONST(Entity)* pEnt, int idx, int iCostume, GameAccountDataExtract* pExtract);
#define scp_IsAltCostumeUnlocked(pEnt, idx, iCostume, pExtract) scp_trh_IsAltCostumeUnlocked(CONTAINER_NOCONST(Entity, pEnt), idx, iCostume, pExtract)

bool scp_trh_IsEquipSlotLocked(ATR_ARGS, NOCONST(Entity)* pPlayerEnt, int iPet, int iEquipSlot);
#define scp_IsEquipSlotLocked(pPlayerEnt, iPet, iEquipSlot) scp_trh_IsEquipSlotLocked(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pPlayerEnt), iPet, iEquipSlot)

bool scp_trh_IsGemSlotLocked(ATR_ARGS, NOCONST(Entity)* pPlayerEnt, int iPet, int iGemSlot);
#define scp_IsGemSlotLocked(pPlayerEnt, iPet, iGemSlot) scp_trh_IsGemSlotLocked(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pPlayerEnt), iPet, iGemSlot)

bool scp_IsGemSlotLockedOnPet(SuperCritterPet* pPet, int iGemSlot);

bool scp_CanEquip(SuperCritterPet *pPet, int iEquipSlot, Item* pItem);

int scp_GetRushTrainingCost(Entity* pPlayerEnt, int iSlot);


bool scp_trh_CheckFlag(ATR_ARGS, ATH_ARG NOCONST(Item)* pItem, S32 eFlag);
#define scp_CheckFlag(pItem, eFlag) scp_trh_CheckFlag(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Item, pItem), eFlag)

void scp_trh_SetFlag(ATR_ARGS, ATH_ARG NOCONST(Item)* pItem, S32 eFlag, bool bSet);

enumTransactionOutcome scp_trh_ResetActivePet(ATR_ARGS, ATH_ARG NOCONST(Entity)* pPlayerEnt, int iNumEquipSlots, int iSlot);

F32 scp_GetBonusXPPercentFromGems(Entity* pEnt, Item* pPetItem);

NOCONST(EntitySavedSCPData)* scp_trh_GetOrCreateEntSCPDataStruct(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bCreateIfMissing);
#define scp_GetEntSCPDataStruct(pEnt) ((EntitySavedSCPData*)scp_trh_GetOrCreateEntSCPDataStruct(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), false))

enumTransactionOutcome scp_trh_SetNumActiveSlots(ATR_ARGS, ATH_ARG NOCONST(EntitySavedSCPData)* pData, int iNum);

#ifndef AILIB
#include "AutoGen/SuperCritterPet_h_ast.h"
#endif

#endif