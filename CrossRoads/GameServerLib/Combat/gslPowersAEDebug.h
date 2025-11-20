#ifndef _GSL_POWERS_AE_DEBUG_H_
#define _GSL_POWERS_AE_DEBUG_H_

typedef struct Entity Entity;
typedef struct Power Power;
typedef struct CombatTarget CombatTarget;
typedef U32 EntityRef;

void gslPowersAEDebug_AddLocation(	Entity *e, 
									Power *p, 
									CombatTarget **eaTargets,
									const Vec3 vLoc);

void gslPowersAEDebug_AddCylinder(Entity *e, 
								  Power *p, 
								  CombatTarget **eaTargets,
								  const Vec3 vStartLoc, 
								  const Vec3 vEndLoc, 
								  F32 length, 
								  F32 radius);

void gslPowersAEDebug_AddSphere(Entity *e, 
								Power *p,
								CombatTarget **eaTargets,
								const Vec3 vLoc, 
								F32 fRadius);

void gslPowersAEDebug_AddCone(	Entity *e, 
								Power *p,
								CombatTarget **eaTargets,
								const Vec3 vStartLoc, 
								const Vec3 vEndLoc, 
								F32 angle, 
								F32 len);

Entity* gslPowersAEDebug_GetDebuggingEnt();
S32 gslPowersAEDebug_ShouldSendForEnt(Entity *e);

#endif

