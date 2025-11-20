#pragma once

#include "Entity.h"

typedef struct AIVolumeAvoidInstance	AIVolumeAvoidInstance;
typedef struct AIVolumeInstance			AIVolumeInstance;
typedef struct AIStatusTableEntry		AIStatusTableEntry;
typedef struct AIVarsBase				AIVarsBase;
typedef struct Entity					Entity;
typedef struct Beacon					Beacon;
typedef struct MovementRequesterMsg		MovementRequesterMsg;


//#define AI_PARANOID_AVOID

typedef struct AIVolumeEntry
{
	EntityRef			entRef;
	AIStatusTableEntry* status;
	AIVolumeInstance*	instance;
	void*				pIdPtr; // Used for identifying direct removal
	int					partitionIdx;

	union {
		struct AIAvoidSphereParams{
			Vec3				spherePos;
			F32					sphereRadius;
		};
		struct AIAvoidBoxParams{
			Mat4				boxMat;
			Mat4				boxInvMat;
			Vec3				boxLocMin;
			Vec3				boxLocMax;
		};
	};

	U8					isSphere : 1;
	U8					destroyOnRemove : 1;

#ifdef AI_PARANOID_AVOID
	S64					addTimeStamp;
	S64					remTimeStamp;
	U8					inBG : 1;
	U8					sentToBG : 1;
#endif

}AIVolumeEntry;

void aiAvoidPartitionLoad(int partitionIdx);

// ---------------------------------------------------------------------------------
void aiAvoidAddInstance(Entity* be, AIVarsBase* aib, int maxLevelDiff, F32 radius, U32 uid);
void aiAvoidRemoveInstance(Entity* be, AIVarsBase* aib, U32 uid);
void aiAvoidUpdateInstance(Entity* be, AIVarsBase* aib, U32 oldUid, U32 newUid);
void aiAvoidDestroy(Entity* be, AIVarsBase* aib);

void aiAvoidUpdate(Entity* be, AIVarsBase* aib);
void aiAvoidAddEnvironmentEntries(Entity* e, AIVarsBase* aib);

int aiShouldAvoidEntity(Entity* be, AIVarsBase* aib, Entity* target, AIVarsBase* aibTarget);

void aiAvoidAddProximityVolumeEntry(Entity* be, AIVarsBase* aib, AIStatusTableEntry* status);
void aiAvoidRemoveProximityVolumeEntry(Entity* be, AIVarsBase* aib, AIStatusTableEntry* status);

void aiAvoidEntryRemove(AIVolumeEntry* entry);

Beacon* aiFindAvoidBeaconInRange(Entity* e, AIVarsBase* aib);

int aiAvoidEntryCheckPoint(Entity *e, const Vec3 sourcePos, const AIVolumeEntry* entry, int isFG, const MovementRequesterMsg* msg);
int aiAvoidEntryCheckLine(Entity *e, const Vec3 rayStart, const Vec3 rayEnd, const AIVolumeEntry* entry, int isFG, const MovementRequesterMsg* msg);
int aiAvoidEntryCheckSelf(Entity* e, const MovementRequesterMsg* msg, const AIVolumeEntry* entry);


// ---------------------------------------------------------------------------------
void aiAttractAddInstance(Entity* be, AIVarsBase *aib, F32 radius, U32 uid);
void aiAttractRemoveInstance(Entity* be, AIVarsBase *aib, U32 uid);
void aiAttractUpdateInstance(Entity* be, AIVarsBase* aib, U32 oldUid, U32 newUid);
void aiAttractDestroy(Entity* be, AIVarsBase* aib);

int aiShouldAttractToEntity(Entity *be, AIVarsBase* aib, Entity* target, AIVarsBase* aibTarget);

void aiAttractAddProximityVolumeEntry(Entity* be, AIVarsBase* aib, AIStatusTableEntry* status);
void aiAttractRemoveProximityVolumeEntry(Entity* be, AIVarsBase* aib, AIStatusTableEntry* status);

AIVolumeEntry* aiAttractGetClosestWithin(Entity* be, AIVarsBase* aib, F32 withinXFeet, F32 *distTo);

void aiVolumeEntry_GetPosition(const AIVolumeEntry *volume, Vec3 vPos);
F32 aiVolumeEntry_GetRadius(const AIVolumeEntry *volume);

void aiSoftAvoidAddInstance(Entity* be, AIVarsBase* aib, int magnitude, F32 radius, U32 uid);
void aiSoftAvoidRemoveInstance(Entity *be, AIVarsBase *aib, U32 uid);
void aiSoftAvoidUpdateInstance(Entity* be, AIVarsBase* aib, U32 oldUid, U32 newUid);
int aiShouldSoftAvoidEntity(Entity* be, AIVarsBase* aib, Entity* target, AIVarsBase* aibTarget);
void aiSoftAvoidAddProximityVolumeEntry(Entity* be, AIVarsBase* aib, AIStatusTableEntry* status);
void aiSoftAvoidRemoveProximityVolumeEntry(Entity* be, AIVarsBase* aib, AIStatusTableEntry* status);
void aiSoftAvoidDestroy(Entity* be, AIVarsBase* aib);