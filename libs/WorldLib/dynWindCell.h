// dynWindCell.h
#pragma once
GCC_SYSTEM

// header dependencies
#include "simplexNoise.h"
#include "mathutil.h"
#include "dynWind.h"
#include "WorldLibEnums.h"

typedef struct DynForcePayload DynForcePayload;
typedef enum WorldWindEffectType;

typedef struct WindSourceInput
{
	WorldWindEffectType		effect_type;
	Vec3					world_mid;
	float					radius;
	float					radius_inner;
	Mat4					world_matrix;
	float					turbulence;
	float					speed;
	float					speed_variation;
	Vec3					direction_variation;
} WindSourceInput;

typedef struct DynWindQueuedForce
{
	Vec3 pos;
	Vec3 velocity;
	F32 radius;
	bool onlySmallObjects;
} DynWindQueuedForce;

typedef struct DynWindOscillator
{
	U32 seed;
	F32 velocity;
	F32 amplitude;
	F32 value;
	F32 offset;
} DynWindOscillator;


#define NUM_OSCILLATORS_PER_CELL 4 // 0 = x, 1 = z, 2 = x small-object only, 3 = z small-object only

typedef struct DynWindSampleGridCell
{
	Vec3 currentWindVec; //these are updated each simulation step
	Vec3 currentWindVecSmall; //these are updated each simulation step

	DynWindOscillator forceOscillators[NUM_OSCILLATORS_PER_CELL];
	bool forceInUse[NUM_OSCILLATORS_PER_CELL];
	Vec3 forceDirections[NUM_OSCILLATORS_PER_CELL];
} DynWindSampleGridCell;

typedef struct dynWindGlobalInputs
{
	F32						fGlobalTime;
	SimplexNoise3DTable_x4	noiseTable_x4;
	int						gridSize;
	float					gridDivisionDistance;
	Vec3					cameraPos;
	F32						fDeltaTime;
	DynWindSettings			settings;
	F32						queuedForceScale;
	F32						queuedForceMaxMag;
	F32						queuedForceSpeedScale;
	F32						queuedForceOffsetScale;
	F32						forceMaxPower;
	F32						forceMaxDisplacement;
	F32						forceMaxFreq;
	F32						forceScale;
	int						numWindSources;
	int						numQueuedForces;
	int						numForcePayloads;
	
	WindSourceInput			*pWindSources;
	DynWindQueuedForce		*pQueuedForces;
	DynForcePayload			*pForcePayloads;

} __ALIGN(16) dynWindGlobalInputs;

// structure that gets passed in user data to specific job
typedef struct dynWindSpursJobArgs
{
	float					zPos;
	DynWindSampleGridCell*	pCells;
} dynWindSpursJobArgs;

void dynWindUpdateRow( const dynWindGlobalInputs *pInput, DynWindSampleGridCell *dstCells, float zPos);
void dynWindUpdateGridCell( const dynWindGlobalInputs *pInput, DynWindSampleGridCell* pCell, Vec3 vPos );