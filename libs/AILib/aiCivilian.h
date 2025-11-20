#pragma once

#include "stdtypes.h"
#include "textparser.h"

typedef struct Entity Entity;
typedef struct AIJob AIJob;
typedef struct GameInteractable GameInteractable;

void aiCivilianStartup(void);
void aiCivilian_OncePerFrame(void);
void aiCivScarePedestrian(Entity *e, Entity *scarer, const Vec3 vScarePos);

bool aiCivGetPlayableBounds(Vec3 min, Vec3 max);

const char* aiCivGetPedestrianFaction();

void aiCivScareNearbyPedestrians(Entity *e, F32 distance);

typedef struct AICivCalloutInfo
{
	const char *pszEmoteAnimReaction;

	const char *messageKey;
	EntityRef	entCalloutEntity;
	U32			lastCalloutTimestamp;
	U64			calloutItemID;

	Quat		lastRot;
	Vec2		lastFace;
	AIJob		*job;		// saved for the non-autogen civilian types
} AICivCalloutInfo;

GameInteractable* aiCivPedestrian_GetCurrentAmbientJob(Entity *e);