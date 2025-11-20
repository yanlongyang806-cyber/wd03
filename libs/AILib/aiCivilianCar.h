#pragma once

#include "aiCivilianTraffic.h"
typedef struct AICivilianFunctionSet AICivilianFunctionSet;


typedef struct AICivilianCar
{
	AICivilian				civBase;

	const AICivilianPathLeg**			eaTrackedLegs;
	const AICivilianPathIntersection**	eaTrackedIntersections;

	AICivilianBlockInfo	blockInfo;
	/*
	EntityRef			entRefBlocker;
	F32					fBlockDistSq;
	AICivilianPathLeg*	pBlockingCrosswalk;
	EBlockType			eBlockType;
	*/

	F32					width;							// Along object z - perp to leg
	F32					depth;							// Along object x - para to leg
	S32					lane;

	// Car cross traffic handling
	AICivilianTrafficQuery	*crosstraffic_request;
	AICivStopSignUser		*stopSign_user;

	// Timers;
	U32 stopSignTime;
	U32 blockedTime;

} AICivilianCar;

void aiCivCarInitializeData();
void aiCivCarShutdown();
void aiCivCar_Tick(Entity *e, AICivilianCar *civ);
AICivilianFunctionSet* aiCivCarGetFunctionSet();

void aiCivCar_FixupCarDef(AICivilianDef *pDef);
int aiCivCar_ValidateCarDef(AICivilianDef *pDef, const char *pszFilename);
void aiCivCar_FixupTypeDef(AICivVehicleTypeDef *pDef);
int aiCivCar_ValidateTypeDef(AICivVehicleTypeDef *pDef, const char *pszFilename);


void aiCivCar_InitWaypointDefault(AICivilianCar *civ, AICivilianWaypoint *wp);

