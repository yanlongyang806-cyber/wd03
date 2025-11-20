#include "gclDebugDrawPrimitives.h"
#include "GfxPrimitive.h"
#include "stashTable.h"
#include "earray.h"
#include "mathutil.h"

typedef struct DebugDrawPrimitive DebugDrawPrimitive;

typedef void (*DrawPrimitiveFuncDraw)(DebugDrawPrimitive *pPrimitive);


typedef struct DebugDrawFuncs
{
	DrawPrimitiveFuncDraw fpDraw;
} DebugDrawFuncs;

typedef enum EDDrawPrimitiveType
{
	EDDrawPrimitiveType_NULL,
	EDDrawPrimitiveType_LOCATION,
	EDDrawPrimitiveType_CYLINDER,
	EDDrawPrimitiveType_CONE,
	EDDrawPrimitiveType_SPHERE,
	EDDrawPrimitiveType_CAPSULE,
	EDDrawPrimitiveType_COUNT
} EDDrawPrimitiveType;

#define DDPRIM_BASE(p) ((p)->base)
#define DDPRIM_BASEPTR(p) (&(p)->base)
#define DDPRIM_BASEHANDLE(p) ((p)->base.handle)

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static DebugDrawPrimitive* gclDebugDrawPrimitive_GetPrimitive(DDrawPrimHandle hPrim);

// -----------------------------------------------------------------------------
typedef struct DebugDrawPrimitive
{
	DDrawPrimHandle		handle;
	DebugDrawFuncs		funcs;
	EDDrawPrimitiveType	eType;
	Vec3 				vPos;
	F32					fTimeToLive;
	Color				color;
	bool				bFading;
} DebugDrawPrimitive;

// -----------------------------------------------------------------------------
typedef struct DebugDrawLocation
{
	DebugDrawPrimitive	base;
} DebugDrawLocation;

// -----------------------------------------------------------------------------
typedef struct DebugDrawCylinder
{
	DebugDrawPrimitive	base;
	Vec3				vTarget;
	F32					fRadius;
} DebugDrawCylinder;


// -----------------------------------------------------------------------------
typedef struct DebugDrawCone
{
	DebugDrawPrimitive	base;
	Vec3				vTarget;
	Vec3				vDirLen;
	F32					fArc; // rads
	F32					fLength;
	F32					fStartRadius;
} DebugDrawCone;

// -----------------------------------------------------------------------------
typedef struct DebugDrawSphere
{
	DebugDrawPrimitive	base;
	F32					fRadius;
} DebugDrawSphere;

// -----------------------------------------------------------------------------
typedef struct DebugDrawCapsule
{
	DebugDrawPrimitive	base;
	Vec3				vTop;
	F32					fRadius;
} DebugDrawCapsule;

// -----------------------------------------------------------------------------
typedef struct DebugDrawPrimitiveData
{
	StashTable			primitiveHash;
	DebugDrawPrimitive	**eaPrimitives;
	U32					curHandle;
} DebugDrawPrimitiveData;

static DebugDrawPrimitiveData *s_dDrawPrimData = NULL;

static DebugDrawPrimitive* DebugDrawPrimitive_Create(EDDrawPrimitiveType type, const Color *clr, const Vec3 vPos, F32 duration);

#define FADE_OUT_TIME	1.f

// ------------------------------------------------------------------------------------------------
void gclDebugDrawPrimitive_Initialize()
{
	if (s_dDrawPrimData)
		return;

	s_dDrawPrimData = calloc(1, sizeof(DebugDrawPrimitiveData));

	s_dDrawPrimData->primitiveHash = stashTableCreateInt(64);
	s_dDrawPrimData->curHandle = 1;

}

// ------------------------------------------------------------------------------------------------
void gclDebugDrawPrimitive_Shutdown()
{
	if (s_dDrawPrimData)
	{
		eaDestroyEx(&s_dDrawPrimData->eaPrimitives, NULL);
		stashTableDestroy(s_dDrawPrimData->primitiveHash);
		free(s_dDrawPrimData);
		s_dDrawPrimData = NULL;
	}
}

// ------------------------------------------------------------------------------------------------
void gclDebugDrawPrimitive_Update(F32 fDTime)
{
	S32 i;

	if (!s_dDrawPrimData)
		return;

	for (i = eaSize(&s_dDrawPrimData->eaPrimitives) - 1; i >= 0; --i)
	{
		DebugDrawPrimitive *pprim = s_dDrawPrimData->eaPrimitives[i];

		devassert(pprim->funcs.fpDraw);

		if (pprim->fTimeToLive != -1.f)
		{	// set to have a time left to live, 

			pprim->fTimeToLive -= fDTime;
			if (pprim->fTimeToLive <= 0.f)
			{// 
				if (!pprim->bFading)
				{	// if we're not fading away, do it first then destroy it
					pprim->bFading = true;
					pprim->fTimeToLive = FADE_OUT_TIME;
				}
				else
				{
					eaRemoveFast(&s_dDrawPrimData->eaPrimitives, i);
					stashIntRemovePointer(s_dDrawPrimData->primitiveHash, pprim->handle, NULL);
					free(pprim);
				}
				continue;
			}

			if (pprim->bFading)
			{	
				F32 norm = pprim->fTimeToLive/FADE_OUT_TIME;
				pprim->color.rgba[3] = (U8)interpF32(CLAMP(norm, 0.f, 1.f), 0.f, 255.f);
			}
		}

		pprim->funcs.fpDraw(pprim);
	}
}

// ------------------------------------------------------------------------------------------------
void gclDebugDrawPrimitive_Kill(DDrawPrimHandle h, F32 fadeTime)
{
	if (s_dDrawPrimData && s_dDrawPrimData->primitiveHash)
	{
		DebugDrawPrimitive *pprim = NULL;
		if (stashIntFindPointer(s_dDrawPrimData->primitiveHash, h, &pprim))
		{
			pprim->bFading = true;
			pprim->fTimeToLive = fadeTime;
		}

	}
}


// ------------------------------------------------------------------------------------------------
static int AddNewPrimitive(DebugDrawPrimitive *p)
{
	S32 safety = 3;
	while(!stashIntAddPointer(s_dDrawPrimData->primitiveHash, s_dDrawPrimData->curHandle, p, false))
	{
		if (!(--safety)) break;
		s_dDrawPrimData->curHandle++;
		if (!s_dDrawPrimData->curHandle) s_dDrawPrimData->curHandle = 1;
	}
	if (!safety) 
		return false;

	eaPush(&s_dDrawPrimData->eaPrimitives, p);
	p->handle = s_dDrawPrimData->curHandle;
	return true;
}

// ------------------------------------------------------------------------------------------------
__forceinline static DebugDrawPrimitive* GetDebugDrawPrimitiveByHandle(DDrawPrimHandle h)
{
	DebugDrawPrimitive *p = NULL;
	if (stashIntFindPointer(s_dDrawPrimData->primitiveHash, h, &p))
	{
		return p;
	}
}


// ------------------------------------------------------------------------------------------------
// -1 for duration is infinite

#define LINE_WIDTH 2.f

// ------------------------------------------------------------------------------------------------
// EDDrawPrimitiveType_LOCATION
// ------------------------------------------------------------------------------------------------
// DrawPrimitiveFuncDraw
static void location_Draw(DebugDrawPrimitive *pPrimitive)
{
#define BOX_SIZE 1.f
	static const Vec3 vBoxSize = {BOX_SIZE, BOX_SIZE, BOX_SIZE};
	Vec3 min, max;
	Mat4 mtx;
	addVec3(pPrimitive->vPos, vBoxSize, max);
	subVec3(pPrimitive->vPos, vBoxSize, min);
	identityMat4(mtx);

	gfxDrawBox3D(min, max, mtx, pPrimitive->color, LINE_WIDTH);
}

// ------------------------------------------------------------------------------------------------
DDrawPrimHandle gclDebugDrawPrimitive_AddLocation(const Vec3 vLoc, 
												  const Color *clr, 
												  F32 duration)
{
	DebugDrawPrimitive *prim;
	
	if (!s_dDrawPrimData)
		return 0;
	
	prim = DebugDrawPrimitive_Create(EDDrawPrimitiveType_LOCATION, clr,vLoc, duration);
	return prim ? prim->handle : 0;
}


// ------------------------------------------------------------------------------------------------
//EDDrawPrimitiveType_CYLINDER,
// ------------------------------------------------------------------------------------------------
static void cylinder_Draw(DebugDrawPrimitive *pPrimitive)
{
	DebugDrawCylinder *pCylinder = (DebugDrawCylinder*)pPrimitive;
	gfxDrawCylinder3D(pPrimitive->vPos, pCylinder->vTarget, pCylinder->fRadius, 20, 
						true, pCylinder->base.color, LINE_WIDTH);
}


// ------------------------------------------------------------------------------------------------
DDrawPrimHandle gclDebugDrawPrimitive_AddCylinder(const Vec3 vStart, 
												  const Vec3 vEnd, 
												  F32 radius, 
												  const Color *clr, 
												  F32 duration)
{	
	DebugDrawCylinder *prim;

	if (!s_dDrawPrimData)
		return 0;

	prim = (DebugDrawCylinder*)DebugDrawPrimitive_Create(EDDrawPrimitiveType_CYLINDER, clr, vStart, duration);
	if (prim)
	{	
		copyVec3(vEnd, prim->vTarget);
		prim->fRadius = radius;
		return DDPRIM_BASEHANDLE(prim);
	}
	return 0;
}


// ------------------------------------------------------------------------------------------------
void gclDebugDrawPrimitive_CylinderUpdate(	DDrawPrimHandle hPrim,
											const Vec3 vStart, 
											const Vec3 vEnd, 
											F32 radius)
{
	DebugDrawPrimitive *pPrim = gclDebugDrawPrimitive_GetPrimitive(hPrim);

	if (pPrim && pPrim->eType == EDDrawPrimitiveType_CYLINDER)
	{
		DebugDrawCylinder *cylinder = (DebugDrawCylinder*)pPrim;
		
		copyVec3(vEnd, cylinder->vTarget);
		copyVec3(vStart, cylinder->base.vPos);
		cylinder->fRadius = radius;
	}
}


// ------------------------------------------------------------------------------------------------
//EDDrawPrimitiveType_CONE,
// ------------------------------------------------------------------------------------------------
static void cone_Draw(DebugDrawPrimitive *pPrimitive)
{
	Mat4 mtx;
	Vec3 vNorm;
	DebugDrawCone *cone = (DebugDrawCone*)pPrimitive;
	identityMat4(mtx);
	
	//orientMat3(mtx, cone->vDirLen);
	orientMat3ToNormalAndForward(mtx, upvec, cone->vDirLen);

	// flip the matrix around a bit to appease gfxDrawCone3D
	copyVec3(mtx[2], vNorm);
	copyVec3(mtx[1], mtx[2]);
	copyVec3(vNorm, mtx[1]);
	negateVec3(mtx[1], mtx[1]);
	
	copyVec3(pPrimitive->vPos, mtx[3]);
	gfxDrawCone3DEx(mtx, cone->fLength, cone->fArc * 0.5f, cone->fStartRadius, 16, pPrimitive->color);
}

// ------------------------------------------------------------------------------------------------
DDrawPrimHandle gclDebugDrawPrimitive_AddCone(const Vec3 vStart, 
											  const Vec3 Target, 
											  F32 angle, 
											  F32 len, 
											  F32 startRadius,
											  const Color *clr, 
											  F32 duration)
{
	DebugDrawCone *prim;
	if (!s_dDrawPrimData)
		return 0;

	prim = (DebugDrawCone*)DebugDrawPrimitive_Create(EDDrawPrimitiveType_CONE, clr, vStart, duration);
	if (prim)
	{	
		copyVec3(Target, prim->vTarget);
		prim->fArc = RAD(angle);
		prim->fLength = len;
		prim->fStartRadius = startRadius;
		subVec3(Target, vStart, prim->vDirLen);
		return DDPRIM_BASEHANDLE(prim);
	}
	return 0;
}


// ------------------------------------------------------------------------------------------------
//EDDrawPrimitiveType_SPHERE,
// ------------------------------------------------------------------------------------------------
static void sphere_Draw(DebugDrawPrimitive *pPrimitive)
{
	DebugDrawSphere *sphere = (DebugDrawSphere*)pPrimitive;
	gfxDrawSphere3D(pPrimitive->vPos, sphere->fRadius, 16, pPrimitive->color, LINE_WIDTH);
}


DDrawPrimHandle gclDebugDrawPrimitive_AddSphere(const Vec3 vLoc, 
												F32 fRadius, 
												const Color *clr, 
												F32 duration)
{
	DebugDrawSphere *prim;
	if (!s_dDrawPrimData)
		return 0;

	prim = (DebugDrawSphere*)DebugDrawPrimitive_Create(EDDrawPrimitiveType_SPHERE, clr, vLoc, duration);
	if (prim)
	{	
		prim->fRadius = fRadius;
		return DDPRIM_BASEHANDLE(prim);
	}
	return 0;
}


// ------------------------------------------------------------------------------------------------
void gclDebugDrawPrimitive_SphereUpdate(DDrawPrimHandle hPrim, const Vec3 pos, F32 fRadius)
{
	DebugDrawPrimitive *pPrim = gclDebugDrawPrimitive_GetPrimitive(hPrim);

	if (pPrim && pPrim->eType == EDDrawPrimitiveType_SPHERE)
	{
		DebugDrawSphere *sphere = (DebugDrawSphere*)pPrim;
		sphere->fRadius = fRadius;
		copyVec3(pos, sphere->base.vPos);
	}
}

// ------------------------------------------------------------------------------------------------
//EDDrawPrimitiveType_CAPSULE,
// ------------------------------------------------------------------------------------------------
static void capsule_Draw(DebugDrawPrimitive *pPrimitive)
{
	DebugDrawCapsule *capsule = (DebugDrawCapsule*)pPrimitive;
	gfxDrawCapsule3D(pPrimitive->vPos, capsule->vTop, capsule->fRadius, 8, pPrimitive->color, LINE_WIDTH);
}


DDrawPrimHandle gclDebugDrawPrimitive_AddCapsule(const Vec3 vStart, 
												 const Vec3 vDir, 
												 F32 fLength, 
												 F32 fRadius, 
												 const Color *clr, 
												 F32 duration)
{
	DebugDrawCapsule *prim;
	if (!s_dDrawPrimData)
		return 0;

	prim = (DebugDrawCapsule*)DebugDrawPrimitive_Create(EDDrawPrimitiveType_CAPSULE, clr, vStart, duration);
	if (prim)
	{	
		Vec3 vDirNorm;
		copyVec3(vDir, vDirNorm);
		normalVec3(vDirNorm);
		prim->fRadius = fRadius;
		scaleAddVec3(vDirNorm, fLength, vStart, prim->vTop);
		return DDPRIM_BASEHANDLE(prim);
	}
	return 0;
}

// ------------------------------------------------------------------------------------------------
void gclDebugDrawPrimitive_CapsuleUpdate(DDrawPrimHandle hPrim, const Vec3 vStart, const Vec3 vDir, F32 fLength, F32 fRadius)
{
	DebugDrawPrimitive *pPrim = gclDebugDrawPrimitive_GetPrimitive(hPrim);

	if (pPrim && pPrim->eType == EDDrawPrimitiveType_CAPSULE)
	{
		DebugDrawCapsule *cap = (DebugDrawCapsule*)pPrim;
		Vec3 vDirNorm;

		cap->fRadius = fRadius;

		copyVec3(vDir, vDirNorm);
		normalVec3(vDirNorm);

		scaleAddVec3(vDirNorm, fLength, vStart, cap->vTop);

		copyVec3(vStart, pPrim->vPos);
	}
}


// ------------------------------------------------------------------------------------------------
static DebugDrawPrimitive* gclDebugDrawPrimitive_GetPrimitive(DDrawPrimHandle hPrim)
{
	if (s_dDrawPrimData && s_dDrawPrimData->primitiveHash)
	{
		DebugDrawPrimitive *pprim = NULL;
		if (stashIntFindPointer(s_dDrawPrimData->primitiveHash, hPrim, &pprim))
		{
			return pprim;
		}
	}
	return NULL;
}

// ------------------------------------------------------------------------------------------------
void gclDebugDrawPrimitive_SetPos(DDrawPrimHandle hPrim, const Vec3 vPos)
{
	DebugDrawPrimitive *pPrim = gclDebugDrawPrimitive_GetPrimitive(hPrim);

	if (pPrim && vPos)
	{
		copyVec3(vPos, pPrim->vPos);
	}
}

// ------------------------------------------------------------------------------------------------
void gclDebugDrawPrimitive_SetColor(DDrawPrimHandle hPrim, const Color *clr)
{
	DebugDrawPrimitive *pPrim = gclDebugDrawPrimitive_GetPrimitive(hPrim);

	if (pPrim && clr)
	{
		pPrim->color = *clr;
	}
}



// ------------------------------------------------------------------------------------------------
static DebugDrawPrimitive* DebugDrawPrimitive_Create(EDDrawPrimitiveType type, const Color *clr, const Vec3 vPos, F32 duration)
{
	DebugDrawPrimitive *p = NULL;
	switch(type)
	{
		xcase EDDrawPrimitiveType_LOCATION:
		{
			DebugDrawLocation	*loc = calloc(1, sizeof(DebugDrawLocation));
			if (!loc) return false;
			p = (DebugDrawPrimitive*)loc;

			p->funcs.fpDraw = location_Draw;
		}
		xcase EDDrawPrimitiveType_CYLINDER:
		{
			DebugDrawCylinder	*loc = calloc(1, sizeof(DebugDrawCylinder));
			if (!loc) return false;

			p = (DebugDrawPrimitive*)loc;

			p->funcs.fpDraw = cylinder_Draw;

		}
		xcase EDDrawPrimitiveType_CONE:
		{
			DebugDrawCone	*loc = calloc(1, sizeof(DebugDrawCone));
			if (!loc) return false;

			p = (DebugDrawPrimitive*)loc;

			p->funcs.fpDraw = cone_Draw;

		}
		xcase EDDrawPrimitiveType_SPHERE:
		{
			DebugDrawSphere	*loc = calloc(1, sizeof(DebugDrawSphere));
			if (!loc) return false;

			p = (DebugDrawPrimitive*)loc;

			p->funcs.fpDraw = sphere_Draw;
		}
		xcase EDDrawPrimitiveType_CAPSULE:
		{
			DebugDrawCapsule	*loc = calloc(1, sizeof(DebugDrawCapsule));
			if (!loc) return false;

			p = (DebugDrawPrimitive*)loc;

			p->funcs.fpDraw = capsule_Draw;
		}
	}

	if (!p)
		return NULL;

	if (!AddNewPrimitive(p))
	{
		free(p);
		return NULL;
	}

	if (clr) 
	{
		p->color = *clr;
	}
	else
	{
		p->color.r = 0;
		p->color.g = 0;
		p->color.b = 0;
		p->color.a = 255;
	}

	p->fTimeToLive = -1;
	p->eType = type;
	p->fTimeToLive = duration;
	copyVec3(vPos, p->vPos);
	return p;
}

