#ifndef AIMOVEMENTMODES_H
#define AIMOVEMENTMODES_H

#include "aiEnums.h"

typedef struct AIConfig AIConfig;
typedef struct AIMovementMode AIMovementMode;
typedef struct AIVarsBase AIVarsBase;
typedef struct Entity Entity;


typedef struct AIMovementModeManager
{
	AIMovementMode **eaMovementModes;
	Entity *pOwner;
	AIMovementMode *pActiveExclusiveMode;
} AIMovementModeManager;

void aiMovementModeManager_CreateAndInitFromConfig(Entity *e, AIVarsBase *aib, AIConfig *pConfig);
void aiMovementModeManager_Destroy(AIMovementModeManager **ppManager);

// returns true/false if the movement mode was added successfully
// Cannot add duplicate movement mode types
int aiMovementModeManager_AddMovementMode(AIMovementModeManager *manager, AIMovementModeType type);

void aiMovementModeManager_Update(AIMovementModeManager *manager);

#endif