#ifndef _POWERSDEBUGCOMMON_H_
#define _POWERSDEBUGCOMMON_H_

#include "Powers.h"

typedef struct Entity Entity;
typedef struct Power Power;
typedef struct CombatTarget CombatTarget;
typedef U32 EntityRef;

AUTO_STRUCT;
typedef struct AEPowersDebugHit
{
	EntityRef		erEnt;
	Vec3			vPos;
} AEPowersDebugHit;

AUTO_STRUCT;
typedef struct PowerDebugAE
{
	EntityRef		erEnt;
	Vec3			vCasterPos;
	EffectArea		eType;
	Vec3			vCastLoc;
	Vec3			vTargetLoc;
	F32				fLength;
	F32				fRadius;
	F32				fArc;
	U32				isClient;
	AEPowersDebugHit	**eaHitEnts;
} PowerDebugAE;

void PowersAEDebug_AddLocation(Entity *e, 
									  PowerDef *p, 
									  CombatTarget **eaTargets,
									  const Vec3 vLoc);

void PowersAEDebug_AddCylinder(Entity *e, 
									  PowerDef *p, 
									  CombatTarget **eaTargets,
									  const Vec3 vStartLoc, 
									  const Vec3 vEndLoc, 
									  F32 length, 
									  F32 radius);

void PowersAEDebug_AddCone(Entity *e, 
								  PowerDef *p,
								  CombatTarget **eaTargets,
								  const Vec3 vStartLoc, 
								  const Vec3 vDir, 
								  F32 angle, 
								  F32 len,
								  F32 startRadius);

void PowersAEDebug_AddSphere(Entity *e, 
									PowerDef *p,
									CombatTarget **eaTargets,
									const Vec3 vLoc, 
									F32 fRadius);


extern S32 g_powersDebugAEOn;


#endif