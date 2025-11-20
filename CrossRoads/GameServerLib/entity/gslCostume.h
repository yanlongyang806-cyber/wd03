/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#pragma once
GCC_SYSTEM

#include "CostumeCommon.h"
#include "TransactionOutcomes.h"

typedef struct Entity Entity;
typedef struct GameAccountData GameAccountData;
typedef struct ItemChangeReason ItemChangeReason;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(GameAccountData) NOCONST(GameAccountData);
typedef struct NOCONST(PlayerCostume) NOCONST(PlayerCostume);

AUTO_STRUCT;
typedef struct SetActiveCostumeParam {
	U32 uiValidateTag;

	int eCostumeType;	/* PCCostumeStorageType enum */
	int iIndex;
	int eGender;		/* Gender enum */
} SetActiveCostumeParam;

AUTO_STRUCT;
typedef struct ChangePlayerCostumeParam {
	U32 uiValidateTag;

	U8 bCreateSlot;
	U8 bIncrementCostumeCount;
} ChangePlayerCostumeParam;

AUTO_STRUCT;
typedef struct StorePlayerCostumeParam {
	U32 uiValidateTag;

	int eCostumeType;	/* PCCostumeStorageType */
	int iIndex;
	PlayerCostume *pCostume;
	int iCost;
	int ePayMethod;		/* PCPaymentMethod */
	GlobalType ePayType;
	ContainerID iPayContainerID;
	const char *pcSlotType;

	int eClassType;
	U8 bIsActive;
	U8 bCreateOrRemoveSlot;
	U8 bNeedsGAD;

	char entDebugName[MAX_NAME_LEN];
	char petEntDebugName[MAX_NAME_LEN];
} StorePlayerCostumeParam;

// ---- Costume Entity utility functions ----

void costumeEntity_SetCostume(Entity *e, const PlayerCostume* pCostume, bool bClearSubstitute);

void costumeEntity_SetCostumeByName(Entity* pEnt, const char* pchCostume);

void costumeEntity_SetDestructibleObjectCostumeByName(Entity* pEnt, const char* pchCostume);

void costumeEntity_RecreateCostumesUsingBaseCostume(const char* pcBaseCostumeName);

void costumeEntity_ResetCostumeData(Entity *pEnt);

// On the server this applies all costume modifying things then
// calls entFixCostume.  Clients should call entFixCostume instead.
void costumeEntity_ApplyItemsAndPowersToCostume(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pEnt, S32 bFixOptional, GameAccountDataExtract *pExtract);

// Takes an entity's base costume and returns a new costume with the entity's powers and items applied to it. 
NOCONST(PlayerCostume)* costumeEntity_CreateCostumeWithItemsAndPowers(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pEnt, CostumeDisplayData ***peaMountData, GameAccountDataExtract *pExtract);

// Used by tailor and costume transaction to verify player can edit
bool costumeEntity_CanPlayerEditCostume(Entity *pEnt, Entity *pCostumeEnt);

// Flag the costume ref as dirty.
void costumeEntity_SetCostumeRefDirty(Entity* e);

NOCONST(PlayerCostume) *costumeEntity_trh_MakePlainCostume(ATH_ARG NOCONST(Entity) *pEnt);
#define costumeEntity_MakePlainCostume(pItem) costumeEntity_trh_MakePlainCostume(CONTAINER_NOCONST(Entity, (pItem)))


// ---- Costume Transaction functions ----

void costumetransaction_ChangePlayerCostume(Entity *pEnt, PlayerCostume *pCostume);

void costumetransaction_ReplaceCostumes(Entity *pEnt, PlayerCostume *pCostume);

void costumetransaction_ChangeMood(Entity *pEnt, const char *pcMood);

bool costumetransaction_InitStorePlayerCostumeParam(StorePlayerCostumeParam *pParam, Entity *pEnt, Entity *pPetEnt, GameAccountData *pData, PCCostumeStorageType eCostumeType, int iIndex, PlayerCostume *pCostume, const char *pcSlotType, S32 iCost, PCPaymentMethod ePayMethod);
void costumetransaction_StorePlayerCostume(Entity *pEnt, Entity *pPetEnt, PCCostumeStorageType eCostumeType, int iIndex, PlayerCostume *pCostume, const char *pcSlotType, S32 cost, PCPaymentMethod ePayMethod);
enumTransactionOutcome costume_tr_StorePlayerCostumeSimpleCostNoCreate(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Entity)* pCostEnt, const ItemChangeReason *pReason, StorePlayerCostumeParam *pParam);
enumTransactionOutcome costume_tr_StorePlayerCostumeSimpleCostWithCreate(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Entity)* pCostEnt, const ItemChangeReason *pReason, StorePlayerCostumeParam *pParam);
enumTransactionOutcome costume_tr_StorePlayerCostumeGADCostNoCreate(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(GameAccountData) *pData, StorePlayerCostumeParam *pParam, const char *pcCostEntDebugName, int iCostEntContainerID);
enumTransactionOutcome costume_tr_StorePlayerCostumeGADCostWithCreate(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(GameAccountData) *pData, StorePlayerCostumeParam *pParam, const char *pcCostEntDebugName, int iCostEntContainerID);

bool costumetransaction_InitSetActiveCostumeParam(SetActiveCostumeParam *pParam, Entity *pEnt, PCCostumeStorageType eCostumeType, int iIndex);
void costumetransaction_SetPlayerActiveCostume(Entity *pEnt, PCCostumeStorageType eCostumeType, int iIndex);
enumTransactionOutcome costume_trh_SetPlayerActiveCostume(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, SetActiveCostumeParam *pParam);

void costumetransaction_CostumeAndCredit(Entity *pEnt);

void costumetransaction_FixupCostumeSlots(Entity *pOwnerEnt, Entity *pEnt, bool bIsPet);
bool costumetransaction_ShouldUpdateCostumeSlots(Entity *pEnt, bool bIsPet);
void gslCheckForDeprecatedCostumeParts(Entity* pEnt);

bool costume_trh_CostumeReplace(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, U32 uiValidateTag, GameAccountDataExtract *pExtract);