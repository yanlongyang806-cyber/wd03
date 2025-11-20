/***************************************************************************
*     Copyright (c) 2005-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GSLSAVEDPET_H
#define GSLSAVEDPET_H

#include "ReferenceSystem.h"
#include "GlobalTypeEnum.h"
#include "GlobalEnums.h"
#include "TransactionOutcomes.h"
#include "SavedPetCommon.h"

typedef struct AlgoPet AlgoPet;
typedef struct AlwaysPropSlotDef AlwaysPropSlotDef;
typedef struct AwayTeamMembers AwayTeamMembers;
typedef struct CritterDef CritterDef;
typedef struct CritterPetRelationship CritterPetRelationship;
typedef struct Entity Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct ItemChangeReason ItemChangeReason;
typedef struct NemesisPowerSet NemesisPowerSet;
typedef struct NemesisMinionPowerSet NemesisMinionPowerSet;
typedef struct NemesisMinionCostumeSet NemesisMinionCostumeSet;
typedef struct PetDef PetDef;
typedef struct PetDiag PetDiag;
typedef struct PetRelationship PetRelationship;
typedef struct PropPowerSaveData PropPowerSaveData;
typedef struct PropPowerSaveList PropPowerSaveList;
typedef struct SavedPetCBData SavedPetCBData;
typedef struct TransactionReturnVal TransactionReturnVal;
typedef void (*SavedPetReturnCallback)(TransactionReturnVal *returnVal, SavedPetCBData *cbData);
typedef U32 ContainerID;

extern int bPetTransactionDebug;

typedef struct SavedPetCBData
{
	GlobalType ownerType;
	ContainerID ownerID;

	GlobalType iPetContainerType;
	ContainerID iPetContainerID;

	GlobalType locationType;
	ContainerID locationID;

	OwnedContainerState oldState;
	OwnedContainerState newState;
	PropEntIDs oldPropEntIDs;
	bool bOldTeamRequest;
	PropEntIDs newPropEntIDs;
	bool bNewTeamRequest;

	int iPartitionIdx;
	U32 iItemOwnerID;
	S32 iItemOwnerType;
	U64 uiItemId;
	int iSeedID;
	int iSlotID;
	PetDef *pPetDef;
	char chSavedName[128];
	char pchReason[128];

	bool bIsNemesis;
	int eNemesisState;

	bool bMakeActive;

	SavedPetReturnCallback CallbackFunc;
	void *pUserData;
} SavedPetCBData;

typedef struct PuppetTransformData
{
	GlobalType ownerType;
	ContainerID ownerID;

	EntityRef entRefMaster;

	ContainerID OldPuppetID;
	GlobalType OldPuppetType;

	ContainerID NewPuppetID;
	GlobalType NewPuppetType;
}PuppetTransformData;

typedef struct SavedPetTeamList {
	U32 eGlobalType;
	U32 iUniqueID;
	int iPartitionIdx;

	U32 *uiPetIDs;
}SavedPetTeamList;

typedef struct SavedPetTeamManager {
	SavedPetTeamList **ppTeamList;
}SavedPetTeamManager;

// Support functions for dealing with saved pets

// Create a new saved pet, as a copy of one entity and owned by another
bool gslCreateSavedPetForOwner(Entity *pOwner, Entity *pEntityToCopy); 

//Creates a saved pet with all the characteristics of the provided def except for name and costume which are taken from a seperate entity.
bool gslCreateDoppelgangerSavedPetFromDef(int iPartitionIdx, Entity *ent, Entity *pEntSrc, AlgoPet *pAlgoPet, PetDef *pPetDef, int iLevel, SavedPetReturnCallback func, void *pCallBackData, U64 uiItemId, OwnedContainerState eState, PropEntIDs *pPropEntIDs, bool bTeamRequest, Entity* pDoppelgangerSrc, GameAccountDataExtract *pExtract);

bool gslCreateSavedPetFromDef(int iPartitionIdx, Entity *ent, Entity* pEntSrc, AlgoPet *pAlgoPet, PetDef *pPetDef, int iLevel, SavedPetReturnCallback func, void *pCallBackData, U64 uiItemId, OwnedContainerState eState, PropEntIDs *pPropEntIDs, bool bTeamRequest, GameAccountDataExtract *pExtract);

void gslCreateNewPuppetFromDef(int iPartitionIdx, Entity *pMasterEntity, Entity* pEntSrc, PetDef *pPetDef, int iLevel, U64 iItemID, GameAccountDataExtract *pExtract);

bool gslCreateSavedPetFromAlgoPet(int iPartitionIdx, Entity *ent, Entity *pEntSrc, AlgoPet *pAlgoPet, PetDef *pPetDef, int iLevel, SavedPetReturnCallback func, void *pCallBackData, U64 uiItemId, OwnedContainerState eState, PropEntIDs *pPropEntIDs, bool bTeamRequest, GameAccountDataExtract *pExtract);

// Create a new Saved Pet for the Nemesis system
bool gslCreateSavedPetNemesis(Entity *pOwnerEnt, const char* pchNemesisName, const char* pchNemesisDescription, NemesisMotivation motivation, NemesisPersonality personality, NemesisPowerSet *pPowerSet, NemesisMinionPowerSet *pMinionPowerSet, NemesisMinionCostumeSet *pMinionCostumeSet, PlayerCostume* pCostume, PCMood *pMood, F32 fPowerHue, F32 fMinionPowerHue, NemesisState eState);

// Destroy an existing player pet
bool gslDestroySavedPet(Entity *pEntityToDestroy);

// Destroys an existing Nemesis
bool gslDestroySavedPetNemesis(Entity *pEntityToDestroy);

// Summon Pet
bool gslSummonSavedPet(Entity *pOwner, GlobalType petType, ContainerID petID, int iSummonCount);

// Summon Critter Pet
bool gslSummonCritterPet(Entity *pOwner, CritterPetRelationship **ppCritterRelationship, PetDef *pPetDef);

// Creates the actual entity for a critter pet relationship
void gslCritterPetCreateEntity(SA_PARAM_NN_VALID Entity *pOwner, SA_PARAM_NN_VALID CritterPetRelationship *pCritterRelationship);

// Return pet to ObjectDB
bool gslUnSummonSavedPet(Entity *pEntityToDestroy);

// Set a pet as the primary pet
bool gslSetPrimarySavedPet(Entity *pOwner, GlobalType petType, ContainerID petID);

// Used by PetsDisabled volumes to record and remove pets when entering the volume
void gslTeam_UnSummonPetsForEnt(int iPartitionIdx, Entity* pEnt);

// Used by PetsDisabled volumes to re-add any previously removed pets when exiting the volume
void gslTeam_ReSummonPetsForEnt(Entity *pOwner);

// Call this when a pet is logged in
void gslSavedPetLoggedIn(Entity* e, Entity* entOwner);

//Update all the characters combat level to be the same as the owners
void gslSavedPet_UpdateCombatLevel(Entity *eOwner);

// Save away team saved pets
bool Entity_SaveAwayTeamPets(Entity *pEntity, AwayTeamMembers* pMembers);
// Call to create critter pet structs from an AwayTeamMembers struct
void Entity_SaveAwayTeamCritterPets(Entity *pEntity,AwayTeamMembers *pAwayTeamMembers);

// Call this when a player logs in
void gslHandlePetsAtLogin(Entity *pOwner);

void gslTransformToPuppet_HandleLoadMods(SA_PARAM_NN_VALID Entity *pEnt);

// Helper for performing fixup on SavedPets during login
void SavedPetFixupHelper(ATH_ARG NOCONST(Entity) *pOwner, NOCONST(Entity) *pSavedPet, bool bIsPuppet, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract, bool bAllowOwnerChange);


void gslPetTeamList_LeaveTeam(Entity *pLeaveEnt, Team *pTeam);

void gslTeam_CheckPetCount(int iPartitionIdx, Team *pTeam, Entity* pJoiningEntity);

void Entity_Login_ApplyRegionRules(Entity *pEnt);


// Call this when a player logs out
void gslHandleSavedPetsAtLogout(Entity* e);
void gslHandleCritterPetsAtLogout(Entity *pOwner);

bool Entity_PuppetRegionValidate(Entity* pEnt);

void entity_PuppetSubscribe(Entity *pEnt);

void Entity_PuppetCheck(Entity *pEnt);

void entity_PetSubscribe(Entity *pEnt);

void Entity_PetCheck(Entity *pEnt);

void Entity_PuppetMasterAndPetTick(Entity *pent);

//Propagate powers through all pets
//Pass in the master entity, and all pets will get there given powers
void ent_PropagatePowers(int iPartitionIdx, Entity *pMasterEntity, Power** ppOldPropPowers, GameAccountDataExtract *pExtract);

void gslSavedPetLogout(int iPartitionIdx, Entity *ent);

void gslCritterPetCleanup(Entity *pCritterPet);

Entity *gslSavedPetGetOwner(Entity *pSavedPet);

LATELINK;
void FixUpEntityName(Entity *pParentEnt, NOCONST(Entity) *pTempEntity, PetDef *pPetDef, const char* pchPetName);

bool ent_PetGetPropPowersToSave(Entity* pOwner, Entity* pPetEnt, PetRelationship* pPetRel, AlwaysPropSlotDef* pPropDef, PropPowerSaveData*** pppSaveData);

enumTransactionOutcome trRemoveDeletedSavedPet(ATR_ARGS, NOCONST(Entity)* pOwner, int iPetContainerType, int iPetContainerID, int iPetID);
enumTransactionOutcome trChangeSavedPetState(ATR_ARGS, NOCONST(Entity)* pOwner, NOCONST(Entity) *pSavedPet, int eState, PropEntIDs *pPropEntIDs, int bTeamRequest, int iSlotID, int ePropCategory, PropPowerSaveList* pSavePowerList);
bool trhRemoveSavedPet(ATR_ARGS, ATH_ARG NOCONST(Entity)* pOwner, ATH_ARG NOCONST(Entity)* pPet);

bool Entity_PuppetCopyEx(ATH_ARG NOCONST(Entity) *pSrc, ATH_ARG NOCONST(Entity) *pDest, bool bFixupItemIDs);
bool Entity_PuppetCopy(ATH_ARG NOCONST(Entity) *pSrc, ATH_ARG NOCONST(Entity) *pDest);


void savedpet_destroyOfflineCopy(int iPartitionIdx, ContainerID uiID);
Entity *savedpet_createOfflineCopy(Entity *pOwner, PetRelationship *pRelationship);

LATELINK;
void FixUpOldWeaponBags(Entity* pOwner, Entity* pPetEnt, PetRelationship* pPet, bool bIsPuppet);

void DismissPetEx(SA_PARAM_NN_VALID Entity *pOwner, SA_PARAM_NN_VALID Entity *pPet);

void gslSavedPet_SetSpawnLocationRotationForPet(S32 iPartitionIdx, Entity *pOwner, Entity *pPet);


#endif