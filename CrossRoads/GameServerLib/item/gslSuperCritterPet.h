#ifndef GSLSUPERCRITTERPET_H
#define GSLSUPERCRITTERPET_H

#include "SuperCritterPet.h"
#include "itemCommon.h"
#include "EntitySavedData.h"
#include "AutoTransDefs.h"
#include "entity.h"

void Entity_SuperCritterPetFixup(Entity* pEnt);
void scp_KillPet(Entity* pPlayerEnt, int iPet);
void gslHandleSuperCritterPetsAtLogout(Entity *pOwner);

void scp_AwardActivePetXP(Entity* pEnt, int delta);

void scp_CheckForFinishedTraining(Entity* pPlayerEnt);

void scp_SetPetState(Entity* pPlayerEnt, int iPetIdx, bool bSummoned, bool bFromClient);
void scp_PetDiedForceDismissCurrentPet(Entity* pPlayerEnt);
void scp_resetSummonedPetInventory(Entity *pPlayerEnt);
void gslSCPPlayerRespawn(Entity* pPlayerEnt);


//#ifndef AILIB
//#include "AutoGen/gslSuperCritterPet_h_ast.h"
//#endif

#endif