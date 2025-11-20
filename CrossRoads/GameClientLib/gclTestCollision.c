// to assist in testing various collision vs collision primitives
#include "gclTestCollision.h"


#ifdef ENABLE_CLIENT_TEST_COLLISION
#include "gclDebugDrawPrimitives.h"
#include "Capsule.h"
#include "mathutil.h"
#include "GameClientLib.h"
#include "gclEntity.h"
#include "ProjectileEntity.h"

typedef struct TestCollCylinder
{
	DDrawPrimHandle	hDrawCylinder;
	Vec3			vStart;
	Vec3			vDir;
	Vec3			vEnd;
	F32				fLen;
	F32				fRadius;

} TestCollCylinder;


#define NUM_CYLINDERS	2

static struct 
{
	DDrawPrimHandle		hDDrawCap;
	Capsule				cap;

	TestCollCylinder	cylinder;
	
	// sphere
	DDrawPrimHandle	hDDrawSphereStart;
	DDrawPrimHandle hDDrawSphereEnd;
	Vec3		vSphereStart;
	Vec3		vSphereEnd;
	F32			fSphereRadius;

	U32			sphereEndValid : 1;
	U32			enabled : 1;
} s_testCollData = {0};

static Color s_red = {255, 0, 0, 255};
static Color s_yellow = {255, 255, 0, 255};
static Color s_green = {0, 255, 0, 255};
static Color s_blue = {0, 0, 255, 255};
static Color s_black = {0, 0, 0, 255};
static Color s_white = {255, 255, 255, 255};
static Color s_gray = {175, 175, 175, 255};

// --------------------------------------------------------------------------------------------------------
static bool gclTestCollision_IsCapsuleValid(Capsule *pcap)
{
	if (!pcap->fLength || !pcap->fRadius)
		return false;
	if (vec3IsZero(pcap->vDir))
		return false;

	return true;
}

// --------------------------------------------------------------------------------------------------------
static bool gclTestCollision_IsSphereValid()
{
	return s_testCollData.fSphereRadius != 0.f;
}

// --------------------------------------------------------------------------------------------------------
static void gclTestCollision_UpdateCylinder(TestCollCylinder *cylinder)
{
	if (!cylinder->hDrawCylinder)
	{
		
		cylinder->hDrawCylinder = gclDebugDrawPrimitive_AddCylinder(cylinder->vStart, 
																	cylinder->vEnd, 
																	cylinder->fRadius,
																	&s_blue, -1);
	}
	else
	{
		gclDebugDrawPrimitive_CylinderUpdate(	cylinder->hDrawCylinder,
												cylinder->vStart, 
												cylinder->vEnd, 
												cylinder->fRadius);
	}
}

// --------------------------------------------------------------------------------------------------------
static void gclTestCollision_UpdateCapsule(Capsule *pcap)
{
	if (gclTestCollision_IsCapsuleValid(pcap))
	{
		if (!s_testCollData.hDDrawCap)
		{
			s_testCollData.hDDrawCap = gclDebugDrawPrimitive_AddCapsule(pcap->vStart, 
																		pcap->vDir, 
																		pcap->fLength, 
																		pcap->fRadius, 
																		&s_blue, -1);
		}
		else
		{
			gclDebugDrawPrimitive_CapsuleUpdate(s_testCollData.hDDrawCap, 
												pcap->vStart, 
												pcap->vDir, 
												pcap->fLength, 
												pcap->fRadius);
		}
	}
	else if (s_testCollData.hDDrawCap)
	{
		gclDebugDrawPrimitive_Kill(s_testCollData.hDDrawCap, 1.f);
		s_testCollData.hDDrawCap = 0;
	}
}

// --------------------------------------------------------------------------------------------------------
static void gclTestCollision_UpdateSphere()
{
	if (gclTestCollision_IsSphereValid())
	{
		if (!s_testCollData.hDDrawSphereStart)
		{
			s_testCollData.hDDrawSphereStart = gclDebugDrawPrimitive_AddSphere(	s_testCollData.vSphereStart, 
																				s_testCollData.fSphereRadius, 
																				&s_blue, -1);
		}
		else
		{
			gclDebugDrawPrimitive_SphereUpdate(	s_testCollData.hDDrawSphereStart, 
												s_testCollData.vSphereStart, 
												s_testCollData.fSphereRadius);
		}

		if (!s_testCollData.hDDrawSphereEnd)
		{
			s_testCollData.hDDrawSphereEnd = gclDebugDrawPrimitive_AddSphere(	s_testCollData.vSphereEnd, 
																				s_testCollData.fSphereRadius, 
																				&s_blue, -1);
		}
		else
		{
			gclDebugDrawPrimitive_SphereUpdate(	s_testCollData.hDDrawSphereEnd, 
												s_testCollData.vSphereEnd, 
												s_testCollData.fSphereRadius);
		}
	}
	else
	{
		if (s_testCollData.hDDrawSphereEnd)
		{
			gclDebugDrawPrimitive_Kill(s_testCollData.hDDrawSphereEnd, 1.f);
			s_testCollData.hDDrawSphereEnd = 0;
		}
		if (s_testCollData.hDDrawSphereStart)
		{
			gclDebugDrawPrimitive_Kill(s_testCollData.hDDrawSphereStart, 1.f);
			s_testCollData.hDDrawSphereStart = 0;
		}
	}
}

// --------------------------------------------------------------------------------------------------------
void gclTestCollision_Update()
{
	if (!s_testCollData.enabled)
		return;

	gclTestCollision_UpdateCylinder(&s_testCollData.cylinder);
	gclTestCollision_UpdateCapsule(&s_testCollData.cap);
	gclTestCollision_UpdateSphere();

	if (gclTestCollision_IsSphereValid() && gclTestCollision_IsCapsuleValid(&s_testCollData.cap))
	{
		Vec3			pos0, dir;
		Quat			rot0;
		F32				len;
		F32				fHitTime;

		zeroVec3(pos0);
		unitQuat(rot0);

		subVec3(s_testCollData.vSphereEnd, s_testCollData.vSphereStart, dir);
		len = normalVec3(dir);
		
		if (SphereSweepVsCapsule(	&s_testCollData.cap, pos0, rot0, 
									s_testCollData.vSphereStart, 
									s_testCollData.vSphereEnd, 
									s_testCollData.fSphereRadius, &fHitTime))
		{
			gclDebugDrawPrimitive_SetColor(s_testCollData.hDDrawCap, &s_red);
		}
		else
		{
			gclDebugDrawPrimitive_SetColor(s_testCollData.hDDrawCap, &s_blue);
		}
	}

	// test cylinders vs capsules
	if (gclTestCollision_IsCapsuleValid(&s_testCollData.cap))
	{
 		TestCollCylinder *pCylinder = &s_testCollData.cylinder;
		Vec3			pos0, vHitPt;
		Quat			rot0;
			

		zeroVec3(pos0);
		unitQuat(rot0);
		if (CapsuleVsCylinder(&s_testCollData.cap, pos0, rot0, 
								pCylinder->vStart, pCylinder->vDir, pCylinder->fLen, pCylinder->fRadius, vHitPt))
		{
			gclDebugDrawPrimitive_SetColor(pCylinder->hDrawCylinder, &s_red);
		}
		else
		{
			gclDebugDrawPrimitive_SetColor(pCylinder->hDrawCylinder, &s_blue);
		}
	}

}

// --------------------------------------------------------------------------------------------------------
static void gclTestCollision_GetCurrentPosition(Vec3 vPos)
{
	if (!gGCLState.bUseFreeCamera)
	{
		Entity *e = entActivePlayerPtr();
		if (e)
			entGetPos(e, vPos);
		else
			zeroVec3(vPos);
	}
	else
	{
		gfxGetActiveCameraPos(vPos);
	}
}
#endif 

// --------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(collTest_Enable);
void gclTestCollision_Enable(S32 enable)
{
#ifdef ENABLE_CLIENT_TEST_COLLISION
	s_testCollData.enabled = !!enable;
	if (!s_testCollData.enabled)
	{
		if (s_testCollData.hDDrawCap)
		{
			gclDebugDrawPrimitive_Kill(s_testCollData.hDDrawCap, 1.f);
			s_testCollData.hDDrawCap = 0;
		}
		if (s_testCollData.hDDrawSphereEnd)
		{
			gclDebugDrawPrimitive_Kill(s_testCollData.hDDrawSphereEnd, 1.f);
			s_testCollData.hDDrawSphereEnd = 0;
		}
		if (s_testCollData.hDDrawSphereStart)
		{
			gclDebugDrawPrimitive_Kill(s_testCollData.hDDrawSphereStart, 1.f);
			s_testCollData.hDDrawSphereStart = 0;
		}
		
		// update all the cylinders
		gclDebugDrawPrimitive_Kill(s_testCollData.cylinder.hDrawCylinder, 1.f);
	}
#endif
}


// --------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(collTest_SetCapPos);
void gclTestCollision_SetCapPos()
{
#ifdef ENABLE_CLIENT_TEST_COLLISION
	Vec3 vPos;
	gclTestCollision_GetCurrentPosition(vPos);

	copyVec3(vPos, s_testCollData.cap.vStart);

	// hack dir to always up for now
	setVec3(s_testCollData.cap.vDir, 0.f, 1.f, 0.f);
#endif
}

// --------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(collTest_SetCapLengthRadius);
void gclTestCollision_SetCapLengthRadius(F32 length, F32 radius)
{
#ifdef ENABLE_CLIENT_TEST_COLLISION
	s_testCollData.cap.fLength = length;
	s_testCollData.cap.fRadius = radius;
#endif
}


// --------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(collTest_SetSpherePosStart);
void gclTestCollision_SetSpherePosStart() 
{
#ifdef ENABLE_CLIENT_TEST_COLLISION
	Vec3 vPos;
	gclTestCollision_GetCurrentPosition(vPos);

	copyVec3(vPos, s_testCollData.vSphereStart);

#endif
}

// --------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(collTest_SetSpherePosEnd);
void gclTestCollision_SetSpherePosEnd() 
{
#ifdef ENABLE_CLIENT_TEST_COLLISION
	Vec3 vPos;
	gclTestCollision_GetCurrentPosition(vPos);
	copyVec3(vPos, s_testCollData.vSphereEnd);
#endif
}

// --------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(collTest_SetSphereRadius);
void gclTestCollision_SetSphereRadius(F32 radius) 
{
#ifdef ENABLE_CLIENT_TEST_COLLISION
	s_testCollData.fSphereRadius = radius;
#endif
}

// --------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(collTest_SetCylinderPos);
void gclTestCollision_SetCylinderPos() 
{
#ifdef ENABLE_CLIENT_TEST_COLLISION
	Vec3 vPos;
	gclTestCollision_GetCurrentPosition(vPos);
	copyVec3(vPos, s_testCollData.cylinder.vStart);
#endif
}

// --------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(collTest_SetCylinderFaceDir);
void gclTestCollision_SetCylinderFaceDir() 
{
#ifdef ENABLE_CLIENT_TEST_COLLISION
	Vec3 vPos;
	gclTestCollision_GetCurrentPosition(vPos);
	copyVec3(vPos, s_testCollData.cylinder.vEnd);
	subVec3(vPos, s_testCollData.cylinder.vStart, s_testCollData.cylinder.vDir);
	s_testCollData.cylinder.fLen = normalVec3(s_testCollData.cylinder.vDir);
#endif
}


// --------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(collTest_SetCylinderRadius);
void gclTestCollision_SetCylinderRadius(S32 cylinderIndex, F32 radius) 
{
#ifdef ENABLE_CLIENT_TEST_COLLISION
	s_testCollData.cylinder.fRadius = radius;
#endif
}
