/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#pragma once
GCC_SYSTEM

#include "CostumeCommon.h"
#include "CostumeCommonEnums.h"
#include "Message.h"
#include "ReferenceSystem.h"
#include "ResourceManager.h"


typedef struct Entity Entity;
typedef struct GameAccountData GameAccountData;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct PlayerCostume PlayerCostume;
typedef struct PlayerCostumeRef PlayerCostumeRef;
typedef struct PlayerCostumeSlot PlayerCostumeSlot;
typedef struct WLCostume WLCostume;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(GameAccountData) NOCONST(GameAccountData);
typedef struct NOCONST(PlayerCostume) NOCONST(PlayerCostume);
typedef struct NOCONST(PlayerCostumeRef) NOCONST(PlayerCostumeRef);


extern bool gValidateCostumePartsNextTick;
extern bool gCostumeAssetsModified;


// Get a player costume from the name
PlayerCostume *costumeEntity_CostumeFromName(const char *pcName);

// Get the proper slot set for this entity
const char *costumeEntity_GetSlotSetName( Entity *pEnt, bool bIsPet );
PCSlotSet *costumeEntity_GetSlotSet( Entity *pEnt, bool bIsPet );
PCSlotSet *costumeEntity_trh_GetSlotSet( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bIsPet );
PCSlotDef *costumeEntity_GetSlotIndexDef( Entity *pEnt, int iIndex, bool bIsPet );
PCSlotDef *costumeEntity_trh_GetSlotIndexDef( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int iIndex, bool bIsPet );
PCSlotType *costumeEntity_GetSlotType( Entity *pEnt, int iIndex, bool bIsPet, int *piSlotID );
PCSlotType *costumeEntity_trh_GetSlotType( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int iIndex, bool bIsPet, int *piSlotID );
bool costumeEntity_trh_FixupCostumeSlots(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEntOwner, ATH_ARG NOCONST(Entity)* pEnt, const char *pcSlotSet);

bool costumeEntity_IsSlotUnlocked(Entity *pPlayerEnt, Entity *pEnt, PCSlotDef *pDef);
bool costumeEntity_IsSlotHidden(Entity *pPlayerEnt, Entity *pEnt, PCSlotDef *pDef);
PCSlotDef *costumeEntity_GetSlotDef(Entity *pEnt, int iSlotID);
PCSlotDef *costumeEntity_GetExtraSlotDef(Entity *pEnt);
bool costumeEntity_IsCostumeSlotUnlocked(Entity *pPlayerEnt, Entity *pEnt, int iIndex);


// Costume unlock support
void costumeEntity_GetUnlockCostumes(CONST_EARRAY_OF(PlayerCostumeRef) ppCostumes, GameAccountData *pData, Entity *pPlayerEnt, Entity *pEnt, PlayerCostume ***peaUnlockedCostumes);
void costumeEntity_GetUnlockCostumesRef(CONST_EARRAY_OF(PlayerCostumeRef) ppCostumes, GameAccountData *pData, Entity *pPlayerEnt, Entity *pEnt, PlayerCostumeRef ***peaUnlockedCostumeRefs);
bool costumeEntity_IsUnlockedCostumeRef(CONST_EARRAY_OF(PlayerCostumeRef) ppCostumes, GameAccountData *pData, Entity *pPlayerEnt, Entity *pEnt, const char *pchCostumeName);


// Gets the entity's WL costume, or NULL if the entity has no costume.
WLCostume* costumeEntity_GetWLCostume( Entity *pEnt );

// Get the entity's costume (including all powers and modifications)
// Only works on live entities
PlayerCostume *costumeEntity_GetEffectiveCostume( Entity *pEnt );

// Get the entity's costume (without powers and modifications)
// Only works on live entities
PlayerCostume *costumeEntity_GetBaseCostume( Entity *pEnt );

//Get the entity's mount costume
PlayerCostume *costumeEntity_GetMountCostume(Entity *pEnt, F32 *fMountScaleOverride);

// Gets the active saved costume from an entity
// Only works on persisted entities
PlayerCostume* costumeEntity_trh_GetActiveSavedCostume(ATH_ARG NOCONST(Entity) *pEnt);
#define costumeEntity_GetActiveSavedCostume(pEnt) costumeEntity_trh_GetActiveSavedCostume(CONTAINER_NOCONST(Entity, (pEnt)))

// Gets a saved costume from an entity
// Only works on persisted entities
PlayerCostume *costumeEntity_GetSavedCostume( Entity *pEnt, int index );

// Get the slot type for the active saved costume
PCSlotType *costumeEntity_trh_GetActiveSavedSlotType(ATH_ARG NOCONST(Entity) *pEnt);
#define costumeEntity_GetActiveSavedSlotType(pEnt) costumeEntity_trh_GetActiveSavedSlotType(CONTAINER_NOCONST(Entity, (pEnt)))

// Get the gender of an entity's costume
Gender costumeEntity_GetEffectiveCostumeGender(SA_PARAM_NN_VALID Entity* pEnt);

void costumeEntity_ResetStoredCostume( Entity *pEnt );

// Force regenerate the entity's costume
void costumeEntity_RegenerateCostume(Entity *pEnt);
void costumeEntity_RegenerateCostumeEx(int iPartitionIdx, Entity *pEnt, GameAccountDataExtract *pExtract);

// Gets the cost of changing from the first costume to the second
S32 costumeEntity_GetCostToChange(Entity *pEnt, PCCostumeStorageType eStorageType, NOCONST(PlayerCostume) *pSrcCostume, NOCONST(PlayerCostume) *pDstCostume, bool *pbCostumeChanged);

// Gets the entity/slot information to modify
S32 costumeEntity_GetStoreCostumeEntities(Entity *pEnt, PCCostumeStorageType eStorageType, Entity ***peaStoreEntities);
Entity *costumeEntity_GetStoreCostumeEntity(Entity *pEnt, PCCostumeStorageType eStorageType, U32 uStoreContainerID);
bool costumeEntity_GetStoreCostumeSlot(Entity *pEnt, Entity *pStoreEnt, S32 iIndex, PCSlotDef **ppSlotDef, PlayerCostumeSlot **ppCostumeSlot);

// ---- Expressions for Costumes ----

int costumeEntity_EvaluateExpr(Entity *pPlayerEnt, Entity *pEnt, Expression *pExpr);

// ---- Costume asset fixup functions ----

void costumeEntity_TickCheckEntityCostumes(void);
void costumeEntity_ForceGlobalReload(void);
void costumeEntity_UpdateEntityCostumeParts(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData);
void costumeEntity_UpdateEntityCostumes(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData);


// ---- Transaction helpers ----

// Gets the cost of changing from the first costume to the second
#define costumeEntity_ApplyEntityInfoToCostumeNoConst(pEnt,pCostume) costumeEntity_trh_ApplyEntityInfoToCostume( ATR_EMPTY_ARGS, pEnt, pCostume)
#define costumeEntity_ApplyEntityInfoToCostume(pEnt,pCostume) costumeEntity_trh_ApplyEntityInfoToCostume( ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), CONTAINER_NOCONST(PlayerCostume, pCostume))
void costumeEntity_trh_ApplyEntityInfoToCostume(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(PlayerCostume) *pCostume);

#ifdef GAMESERVER
bool costumeEntity_ApplyCritterInfoToCostume( NOCONST(Entity) *pEnt, NOCONST(PlayerCostume) *pCostume, bool bMakeClone);
#endif

S32 costumeEntity_trh_GetNumCostumeSlots(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(GameAccountData) *pData);
#define costumeEntity_GetNumCostumeSlots(entity, data) costumeEntity_trh_GetNumCostumeSlots(CONTAINER_NOCONST(Entity, (entity)), CONTAINER_NOCONST(GameAccountData,(data)))

bool costumeEntity_trh_CostumeUnlockedLocal(ATH_ARG NOCONST(Entity) *pEnt, PlayerCostume *pCostume);
#define costumeEntity_CostumeUnlockedLocal(entity, costume) costumeEntity_trh_CostumeUnlockedLocal(CONTAINER_NOCONST(Entity, entity), costume)

int costumeEntity_CanChangeForFree(SA_PARAM_OP_VALID Entity *pEnt);

int costumeEntity_GetFreeChangeTokens(SA_PARAM_OP_VALID Entity *pOwnerEnt, SA_PARAM_OP_VALID Entity *pEnt);
int costumeEntity_GetFreeFlexChangeTokens(SA_PARAM_OP_VALID Entity *pEnt);
int costumeEntity_GetAccountChangeTokens(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID const GameAccountData *pData);

// Testing costumes
bool costumeEntity_TestCostumeForFreeChange(Entity *pPlayerEnt, Entity *pEnt, S32 iSlot);
