#pragma once

#include "aiCivilianTraffic.h"

typedef struct AICivilianFunctionSet AICivilianFunctionSet;


typedef struct AICivilianTrolley
{
	AICivilian				civBase;
	F32						length;
	
	U32						stopTime;
	AICivilianBlockInfo		blockInfo;

} AICivilianTrolley;

void aiCivTrolley_InitializeData();
void aiCivTrolley_Shutdown();
void aiCivTrolley_Tick(Entity *e, AICivilianTrolley *civ);
AICivilianFunctionSet* aiCivTrolley_GetFunctionSet();

void aiCivTrolley_FixupTrolleyDef(AICivilianDef *pDef);
int aiCivTrolley_ValidateTrolleyDef(AICivilianDef *pDef, const char *pszFilename);
void aiCivTrolley_FixupTypeDef(AICivVehicleTypeDef *pDef);
int aiCivTrolley_ValidateTypeDef(AICivVehicleTypeDef *pDef, const char *pszFilename);


void aiCivTrolley_InitWaypointDefault(AICivilianTrolley *civ, AICivilianWaypoint *wp);

