#pragma once

#include "entEnums.h"

typedef struct AIConfig AIConfig;
typedef struct CritterDef CritterDef;
typedef struct Entity Entity;
typedef struct PlayerPetInfo PlayerPetInfo;
typedef U32 EntityRef;

void entity_RebuildPetPowerStates(SA_PARAM_NN_VALID Entity *pentOwner, SA_PARAM_NN_VALID PlayerPetInfo *pInfo);

void PetCommands_SetSpecificPetState(SA_PARAM_NN_VALID Entity *pOwner, EntityRef ent, SA_PARAM_NN_STR const char* state);
void PetCommands_SetSpecificPetStance(SA_PARAM_NN_VALID Entity *pOwner, EntityRef ent, PetStanceType stanceType, SA_PARAM_NN_STR const char* stance);

void PetCommands_SetDefaultPetState(SA_PARAM_NN_VALID Entity* pOwner, SA_PARAM_NN_VALID Entity *pet);
void PetCommands_SetDefaultPetStance(SA_PARAM_NN_VALID Entity* pOwner, SA_PARAM_NN_VALID Entity *pet, PetStanceType stanceType);
void PetCommands_SetDefaultPetStances(SA_PARAM_NN_VALID Entity* pOwner, SA_PARAM_NN_VALID Entity *pet);

int PetCommands_InitializeControlledPetInfo(Entity *e, CritterDef* critter, AIConfig *config);
int PetCommands_UpdatePlayerPetInfo(SA_PARAM_NN_VALID Entity* pOwner, int added, int petRef);

void PetCommands_Targeting_Cleanup( SA_PARAM_NN_VALID Entity* pEnt );
void PetCommands_RespawnPets(Entity *pOwner);

void PetCommands_RemoveAllCommandsTargetingEnt(Entity* e);

void PetCommands_GetAllPets(Entity* pOwner, Entity*** pets);
