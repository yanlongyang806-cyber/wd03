#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "entEnums.h"

// Forward Declarations

typedef struct Entity Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(PetRelationship) NOCONST(PetRelationship);
typedef struct PetDef PetDef;
typedef struct ItemChangeReason ItemChangeReason;
typedef struct LoginPuppetInfo LoginPuppetInfo;
typedef struct LoginPetInfo LoginPetInfo;
typedef struct NOCONST(Item) NOCONST(Item);
typedef enum enumTransactionOutcome enumTransactionOutcome;
typedef struct CharClassCategorySet CharClassCategorySet;
typedef enum CharClassTypes CharClassTypes;
typedef U32 ContainerID;

// Returns the next valid PetID for the Owner Entity.  Optionally excludes
//  one specific Pet Entity from that review by ContainerID, in order to allow
//  (but not require) an existing Pet to re-use its value if it's already in the
//  Owner's list.
U32 entity_GetNextPetIDHelper(ATH_ARG NOCONST(Entity) *pEntOwner, ContainerID cidExclude);

// Returns a new earray filled with all the Entity's OwnderContainers ContainerIDs
ContainerID* entity_GetOwnedContainerIDsArray(SA_PARAM_NN_VALID Entity *pEntOwner);

// Adds a new pet from the pet store to the entity
enumTransactionOutcome trEntity_AddPet(ATR_ARGS, NOCONST(Entity) * ent, const char *pchPetDef, int iLevel);

enumTransactionOutcome trAddAllowedCritterPet(ATR_ARGS, NOCONST(Entity) *pOwner, PetDef *pPetDef, U64 uiItemID, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

enumTransactionOutcome trEntity_SetPuppetStateActive(ATR_ARGS, NOCONST(Entity) *pEnt, NOCONST(Entity) *pOldEnt, U32 uNewID, U32 uOldID, U32 bSetLastActiveID);

bool trh_RemoveSavedPetByID(ATR_ARGS, ATH_ARG NOCONST(Entity) *pOwner, int iPetContainerID);

//Made public for character creation
bool Entity_MakePuppetMaster(ATH_ARG NOCONST(Entity) *ent);
//Upon next login, this puppet will be created and added to the puppet list
bool Entity_AddPuppetCreateRequest(ATH_ARG NOCONST(Entity) *ent, LoginPuppetInfo* pInfo);
//Upon next login, this pet will be created and added to the pet list
bool Entity_AddPetCreateRequest(ATH_ARG NOCONST(Entity) *ent, LoginPetInfo* pInfo);

void SavedPet_th_RemoveFromUnusedProps(ATH_ARG NOCONST(Entity) *eMasterEntity, ATH_ARG NOCONST(PetRelationship) *pRelationShip);

bool SavedPet_th_FlagRequestMaxCheck(ATH_ARG NOCONST(Entity) *eMasterEntity, ATH_ARG NOCONST(Entity) *pSavedPet, ATH_ARG NOCONST(PetRelationship) *pRelationShip, U32 uiPropEntID, bool bTeamRequest, int iSlotID, S32 ePropCategory, bool bMakeChanges);

bool trhAddSavedPetFromContainerItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pDstEnt, ATH_ARG NOCONST(Item)* pItem, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaContainerItemPets, int bMakeActive, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);