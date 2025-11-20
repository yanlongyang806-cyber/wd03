#ifndef _GCL_DEBUGDRAW_H_
#define _GCL_DEBUGDRAW_H_


#include "stdtypes.h"
typedef U32 DDrawPrimHandle;

void gclDebugDrawPrimitive_Initialize();
void gclDebugDrawPrimitive_Shutdown();

void gclDebugDrawPrimitive_Update(F32 fDTime);

// -1 for duration is infinite

DDrawPrimHandle gclDebugDrawPrimitive_AddLocation(const Vec3 vLoc, 
												  const Color *clr, 
												  F32 duration);

DDrawPrimHandle gclDebugDrawPrimitive_AddCylinder(const Vec3 vStart, 
												  const Vec3 vEnd, 
												  F32 radius, 
												  const Color *clr, 
												  F32 fDuration);

DDrawPrimHandle gclDebugDrawPrimitive_AddCone(const Vec3 vStart, 
											  const Vec3 Target, 
											  F32 angle, 
											  F32 len, 
											  F32 startRadius,
											  const Color *clr, 
											  F32 duration);

DDrawPrimHandle gclDebugDrawPrimitive_AddSphere(const Vec3 vLoc, 
												F32 fRadius, 
												const Color *clr, 
												F32 duration);

DDrawPrimHandle gclDebugDrawPrimitive_AddCapsule(	const Vec3 vStart, 
													const Vec3 vDir, 
													F32 fLength, 
													F32 fRadius, 
													const Color *clr, 
													F32 duration);

void gclDebugDrawPrimitive_Kill(DDrawPrimHandle h, F32 fadeTime);

void gclDebugDrawPrimitive_SetPos(DDrawPrimHandle hPrim, const Vec3 pos);
void gclDebugDrawPrimitive_SetColor(DDrawPrimHandle hPrim, const Color *clr);


// sphere
void gclDebugDrawPrimitive_SphereUpdate(DDrawPrimHandle hPrim, const Vec3 pos, F32 fRadius);


// capsule
void gclDebugDrawPrimitive_CapsuleUpdate(	DDrawPrimHandle hPrim, 
											const Vec3 vStart, 
											const Vec3 vDir, 
											F32 length, 
											F32 radius);
// cylinder
void gclDebugDrawPrimitive_CylinderUpdate(	DDrawPrimHandle hPrim,
											const Vec3 vStart, 
											const Vec3 vEnd, 
											F32 radius);


#endif