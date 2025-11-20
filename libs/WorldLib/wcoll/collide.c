#include "collide.h"

#include "capsule.h"
#include "WorldGrid.h"
#include "PhysicsSDK.h"
#include "LineDist.h"
#include "ctri.h"
#include "mutex.h"
#include "error.h"
#include "collcache.h"
#include "timing.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

#define MAX_ACCELERATOR_TRIS 400

typedef struct CollAcceleratorTri
{
	Vec3 aabbMin,aabbMax;
	U32 uIndex;
} CollAcceleratorTri;

typedef struct CollAcceleratorShapeEntry
{
	const void * pShape;
	U32 bUseTriInfo : 1;
	U32 bHasOneWayCollision : 1;
	int iTriCount;
	CollAcceleratorTri eaTris[MAX_ACCELERATOR_TRIS];
} CollAcceleratorShapeEntry;

typedef struct CollAccelerator
{
	Vec3 aabb_lo,aabb_hi;
	CollAcceleratorShapeEntry** eaShapeEntries;
} CollAccelerator;

static 	CollAccelerator				g_accelerator = {0};
static bool							g_bAcceleratorActive=0;
static bool							g_bEnableAcclerator=1;

#define	GRID_MAXDIST 8e16

static __forceinline F32 worldSlope(const Vec3 n,F32 s,const Mat3 wmat)
{
	if (n[1] > .999999) {
		return wmat[1][1];
	} else if (n[1] < -.999999) {
		return -wmat[1][1];
	} else {
		return n[0] * wmat[0][1] + n[1] * wmat[1][1] + n[2] * wmat[2][1];
	}
}

static __forceinline int checkCollCloser(const CTri *ctri,const Mat4 mat,CollInfo *coll,Vec3 col,F32 sqdist)
{
	Vec3		dv;

	mulVecMat4(col,mat,dv);
	copyVec3(dv,col);
	if (coll->flags & COLL_DISTFROMSTART)
	{
		F32 line_dist = sqdist;

		subVec3(col,coll->start,dv);
		sqdist = lengthVec3Squared(dv);
		sqdist -= line_dist;
		{
			F32		t,slope = worldSlope(ctri->norm,ctri->scale,mat);

			if (sqdist < 0)
				sqdist = 0;
			t = sqrtf(sqdist) + (1-slope) / 64.f; // hack to give precedence to flatter surfaces
			sqdist = SQR(t);
		}
	}
	if (sqdist < coll->sqdist)
	{
		copyMat3(mat,coll->mat);
		copyVec3(col,coll->mat[3]);
		coll->sqdist = sqdist;
		coll->backside = coll->tri_state.backside;
		coll->ctri_copy = *ctri;
		coll->ctri = &coll->ctri_copy;
	}
	return 0;
}

static int checkTriColl(const CTri *ctri, CollInfo *coll, const Vec3 obj_start, const Vec3 obj_end, Mat4 mat)
{
	F32		sqdist;
	Vec3	col;

	if (coll->hasOneWayCollision)
	{
		Vec3 vNorm;
		
		if (coll->flags & COLL_IGNOREONEWAY)
			return 0;

		mulVecMat3(ctri->norm, mat, vNorm);
		if (dotVec3(coll->motion_dir, vNorm) >= 0.f)
			return 0;
	}

	sqdist = ctriCollideRad(ctri,obj_start,obj_end,col,coll->radius,&coll->tri_state);
	if (sqdist >= 0.f)
	{
		if (coll->flags & COLL_CYLINDER)
		{
			Vec3	tverts[3];
			F32		rad;
			int		ret;

			expandCtriVerts(ctri,tverts);
			ret = ctriCylCheck(col,obj_start,obj_end,coll->radius,tverts,&rad);
			if (!ret)
				return 0;
			if (ret > 0 && (coll->flags & COLL_DISTFROMSTART))
				sqdist = rad;
		}
		if (coll->flags & COLL_DISTFROMSTARTEXACT)
		{
			Vec3	tverts[3];

			expandCtriVerts(ctri,tverts);
			sqdist = ctriGetPointClosestToStart(col,obj_start,obj_end,coll->radius,tverts,ctri->norm,mat);
		}
		return checkCollCloser(ctri,mat,coll,col,sqdist);
	}
	return 0;
}

// --------------------------------------------------------------------------------------------------------------
// TriangleVsSphereSweep and related functions
// --------------------------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------------------------
__forceinline static F32 SignedDistance(const Vec3 vPlaneNormal, F32 fPlaneConst, const Vec3 vPt)
{
	return dotVec3(vPlaneNormal, vPt) + fPlaneConst;
}

// --------------------------------------------------------------------------------------------------------------
static S32 PointInTriangle(const Vec3 vTriP1, const Vec3 vTriP2, const Vec3 vTriP3, const Vec3 vPt)
{
	F32 dot;
	Vec3 cross1,cross2,cross3;
	Vec3 p1_point, p2_point, p3_point;
	Vec3 p1_p2, p2_p3, p3_p1;

	// three vectors from the point in the triangle to the vertices

	subVec3(vPt, vTriP1, p1_point);
	subVec3(vPt, vTriP2, p2_point);
	subVec3(vPt, vTriP3, p3_point);

	// three vectors from p1 to p2, from p2 to p3, and from p3 to p1
	subVec3(vTriP2, vTriP1, p1_p2);
	subVec3(vTriP3, vTriP2, p2_p3);
	subVec3(vTriP1, vTriP3, p3_p1);

	// get the three cross product values
	crossVec3(p1_point,p1_p2,cross1);
	crossVec3(p2_point,p2_p3,cross2);
	crossVec3(p3_point,p3_p1,cross3);

	// is the point on an edge of the triangle?
	
	if (lengthVec3Squared(cross1) == 0 || lengthVec3Squared(cross2) == 0 || lengthVec3Squared(cross3) == 0)
		return 1;

	// otherwise, make sure the normals are pointing the same way

	dot = dotVec3(cross2,cross1);

	if (dot > 0.f)
	{
		dot = dotVec3(cross2,cross3);

		if (dot > 0.f)
			return 1;
	}

	return 0;
}

// --------------------------------------------------------------------------------------------------------------
static __forceinline S32 getLowestRoot(F32 a, F32 b, F32 c, F32 maxR, F32 *root)
{
	F32 sqrtD;
	F32 r1;
	F32 r2;
	F32 d = b*b - 4.f * a * c;
	
	if (d < 0.f)  // no solutions
		return false;

	sqrtD = sqrtf(d);
	a *= 2.f;
	r1 = (-b - sqrtD) / a;
	r2 = (-b + sqrtD) / a;

	if (r1 > r2)
	{
		F32 temp = r2;
		r2 = r1;
		r1 = temp;
	}
	
	if (r1 > 0.f && r1 < maxR)
	{
		*root = r1;
		return true;
	}

	if (r2 > 0.f && r2 < maxR)
	{
		*root = r2; 
		return true;
	}

	return false;

}

// --------------------------------------------------------------------------------------------------------------
static __forceinline S32 EdgeVsSphereSweep(	const Vec3 vSpherePt, const Vec3 vSphereVel, 
											F32 fVelocityLenSQR, const Vec3 vEdgePt1, const Vec3 vEdgePt2,
											Vec3 vCollisionPtOut, F32 *pMinTimeInOut)
{
	Vec3 vSphereToVtx, vEdge;
	F32 fEdgeDotVel, a, b, c, fEdgeLenSQR;
	F32 fEdgeDotSphereToVtx;
	F32 fRoot;

	subVec3(vEdgePt2, vEdgePt1, vEdge);
	subVec3(vEdgePt1, vSpherePt, vSphereToVtx);
	fEdgeDotVel = dotVec3(vEdge, vSphereVel);
	fEdgeLenSQR = lengthVec3Squared(vEdge);
	fEdgeDotSphereToVtx = dotVec3(vEdge, vSphereToVtx);
	a = fEdgeLenSQR * -fVelocityLenSQR + SQR(fEdgeDotVel);
	b = fEdgeLenSQR * (2.f * dotVec3(vSphereVel, vSphereToVtx)) - 2.f * fEdgeDotVel * fEdgeDotSphereToVtx;
	c = fEdgeLenSQR * (1.f - lengthVec3Squared(vSphereToVtx)) + SQR(fEdgeDotSphereToVtx);

	if (getLowestRoot(a, b, c, *pMinTimeInOut, &fRoot))
	{
		// lower time, check if the point is within the line segment
		F32 f = (fEdgeDotVel * fRoot - fEdgeDotSphereToVtx) / fEdgeLenSQR;
		if (f >= 0 && f <= 1.f)
		{
			// calculate the point of intersection
			scaleAddVec3(vEdge, f, vEdgePt1, vCollisionPtOut);
			*pMinTimeInOut = fRoot;
			return true;
		}
	}

	return false;
}

static __forceinline S32 PointVsSphereSweep(const Vec3 vSpherePt, const Vec3 vSphereVel, 
											F32 fVelocityLenSQR, const Vec3 vPoint, F32 *pMinTimeInOut)
{
	Vec3 vVertToSphere;
	F32 b, c;
	subVec3(vSpherePt, vPoint, vVertToSphere);
	b = 2.f * dotVec3(vSphereVel, vVertToSphere);
	c = lengthVec3Squared(vVertToSphere) - 1.f;
	if (getLowestRoot(fVelocityLenSQR, b, c, *pMinTimeInOut, pMinTimeInOut))
	{
		return true;
	}
	return false;
}

// --------------------------------------------------------------------------------------------------------------
// intersection function to get the time and 
static S32 TriangleVsSphereSweep(	const Vec3 vTriP1, const Vec3 vTriP2, const Vec3 vTriP3, const Vec3 vTriNormal,
									const Vec3 vSpherePt, const Vec3 vSphereVel, F32 fSphereRadius, 
									F32 *pfHitTime, Vec3 vHitPosOut)
{
	F32 t0;

	{
		F32 fTriPlaneConst, fTriDotVelocity, t1;

		fTriDotVelocity =	dotVec3(vTriNormal, vSphereVel);
		
		// ignore backfacing triangles
		if (fTriDotVelocity >= 0.f)
			return false;

		fTriPlaneConst = -dotVec3(vTriP1, vTriNormal);

		if (fTriDotVelocity != 0.f)
		{	// sphere potentially crosses the triangle's plane
			F32 fSignedDist = SignedDistance(vTriNormal, fTriPlaneConst, vSpherePt);
			t0 = (-fSphereRadius - fSignedDist) / fTriDotVelocity;
			t1 = (fSphereRadius - fSignedDist) / fTriDotVelocity;

			if (t0 > t1)
			{
				F32 temp = t1;
				t1 = t0;
				t0 = temp;
			}

			if (t0 > 1.f || t1 < 0.f)
			{	// sphere does not cross plane
				return false;
			}

			t0 = CLAMP(t0, 0.f, 1.f);
			t1 = CLAMP(t1, 0.f, 1.f);

			// planeIntersectionPoint = basePoint - planeNormal + t0 * velocity
			{
				Vec3 vIsect;

				subVec3(vSpherePt, vTriNormal, vIsect);
				scaleAddVec3(vSphereVel, t0, vIsect, vIsect);
				// check if the point is inside the triangle
				if (PointInTriangle(vTriP1, vTriP2, vTriP3, vIsect))
				{
					if (*pfHitTime) *pfHitTime = t0;

					subVec3(vSpherePt, vTriNormal, vHitPosOut);
					scaleAddVec3(vSphereVel, t0, vHitPosOut, vHitPosOut);
					return true;
				}
			}
		}
	}
	
	// sphere does not collide with the inside of the triangle, test against the vertices and edges
	
	{
		Vec3 vLocalTriPt1, vLocalTriPt2, vLocalTriPt3;
		Vec3 vLocalSpherePt, vLocalSphereVel;
		F32 fVelocityLenSQR;
		const F32 *pvCollisionPt = NULL;
		bool bFoundCollision = false;


		t0 = 1.f / fSphereRadius;

		// todo(rp): figure out how I can not transform these points into the sphere space
		scaleVec3(vTriP1, t0, vLocalTriPt1);
		scaleVec3(vTriP2, t0, vLocalTriPt2);
		scaleVec3(vTriP3, t0, vLocalTriPt3);
		scaleVec3(vSpherePt, t0, vLocalSpherePt);
		scaleVec3(vSphereVel, t0, vLocalSphereVel);
		// 
		
		t0 = 1.f;
		fVelocityLenSQR = dotVec3(vLocalSphereVel, vLocalSphereVel);

		// test vs the vertices
		if (PointVsSphereSweep(vLocalSpherePt, vLocalSphereVel, fVelocityLenSQR, vLocalTriPt1, &t0))
		{
			pvCollisionPt = vTriP1;
			bFoundCollision = true;
		}
			
		if (PointVsSphereSweep(vLocalSpherePt, vLocalSphereVel, fVelocityLenSQR, vLocalTriPt2, &t0))
		{
			pvCollisionPt = vTriP2;
			bFoundCollision = true;
		}

		if (PointVsSphereSweep(vLocalSpherePt, vLocalSphereVel, fVelocityLenSQR, vLocalTriPt3, &t0))
		{
			pvCollisionPt = vTriP3;
			bFoundCollision = true;
		}

		// test vs the edges
		if (EdgeVsSphereSweep(vLocalSpherePt, vLocalSphereVel, fVelocityLenSQR, vLocalTriPt1, vLocalTriPt2, vHitPosOut, &t0))
		{
			pvCollisionPt = NULL;
			bFoundCollision = true;
		}

		if (EdgeVsSphereSweep(vLocalSpherePt, vLocalSphereVel, fVelocityLenSQR, vLocalTriPt2, vLocalTriPt3, vHitPosOut, &t0))
		{
			pvCollisionPt = NULL;
			bFoundCollision = true;
		}

		if (EdgeVsSphereSweep(vLocalSpherePt, vLocalSphereVel, fVelocityLenSQR, vLocalTriPt3, vLocalTriPt1, vHitPosOut, &t0))
		{
			pvCollisionPt = NULL;
			bFoundCollision = true;
		}

		if (bFoundCollision)
		{
			if (*pfHitTime) *pfHitTime = t0;

			if (pvCollisionPt)
			{	// hit one of the corners
				copyVec3(pvCollisionPt, vHitPosOut);
			}
			else
			{	// hit an edge, transform the hitpos back out of sphere space
				scaleVec3(vHitPosOut, fSphereRadius, vHitPosOut);
			}
			return true;
		}
	}
	

	
	return false;
	
}


static S32 CTriVsSphereSweep(const CTri *ctri,CollInfo *coll, const Vec3 vSpherePt,const Vec3 vSphereEnd, const Mat4 mat)
{
	Vec3 vTri[3];
	Vec3 vSphereVel;
	F32 fHitTime;
	Vec3 vHitPos;

	expandCtriVerts(ctri, vTri);
			
	subVec3(vSphereEnd, vSpherePt, vSphereVel);
	if (TriangleVsSphereSweep(vTri[0], vTri[1], vTri[2], ctri->norm, vSpherePt, vSphereVel, coll->radius, &fHitTime, vHitPos))
	{
		if (fHitTime < coll->toi)
		{
			coll->toi = fHitTime;
			coll->sqdist = 1;
			coll->ctri_copy = *ctri;
			coll->ctri = &coll->ctri_copy;
			copyVec3(vHitPos, coll->vHitPos);
			return true;
		}
	}


	return false;
}

typedef struct ShapeList {
	S32			shapeCount;
	void**		shapes;
	void*		shapesBuffer[100];
	void**		eaShapes;
} ShapeList;

#if !PSDK_DISABLED
static void handleShapes(const PSDKQueryShapesCBData* psdkData){
	ShapeList*	sl = psdkData->input.userPointer;
	S32			newCount = sl->shapeCount + psdkData->shapeCount;

	if(sl->eaShapes){
		eaPushArray(&sl->eaShapes, psdkData->shapes, psdkData->shapeCount);
		sl->shapes = sl->eaShapes;
	}
	else if(newCount > ARRAY_SIZE(sl->shapesBuffer)){
		eaPushArray(&sl->eaShapes, sl->shapesBuffer, sl->shapeCount);
		eaPushArray(&sl->eaShapes, psdkData->shapes, psdkData->shapeCount);
		sl->shapes = sl->eaShapes;
	}else{
		sl->shapes = sl->shapesBuffer;
		CopyStructs(sl->shapesBuffer + sl->shapeCount,
					psdkData->shapes,
					psdkData->shapeCount);
	}

	sl->shapeCount = newCount;
}

static void gatherShapes(	CollInfo *coll,
							ShapeList *shape_list,
							const Vec3 start,
							const Vec3 end,
							F32 radius)
{
	PSDKQueryShapesInCapsuleParams params = {0};
	
	params.filterBits = coll->filterBits;
	copyVec3(start, params.source);
	copyVec3(end, params.target);
	params.radius = radius;
	params.callback = handleShapes;
	params.userPointer = shape_list;
	
	psdkSceneQueryShapesInCapsule(	coll->psdkScene,
									coll->sceneOffset,
									&params);
}

static void gatherShapesAABB(	CollInfo *coll,
							ShapeList *shape_list,
							const Vec3 aabbMin,
							const Vec3 aabbMax)
{
	PSDKQueryShapesInAABBParams params = {0};
	
	params.filterBits = coll->filterBits;
	copyVec3(aabbMin, params.aabbMin);
	copyVec3(aabbMax, params.aabbMax);
	params.callback = handleShapes;
	params.userPointer = shape_list;
	
	psdkSceneQueryShapesInAABB(	coll->psdkScene,
									coll->sceneOffset,
									&params);
}
#endif

typedef struct HandleShapeTrianglesData {
	const void*	shape;
	CollInfo*	coll;
} HandleShapeTrianglesData;

static void destroyCtriCache(	CTri **ctris,
								U32 triCount)
{
	U32		i;

	if (!ctris)
		return;
	for(i=0;i<triCount;i++)
	{
		SAFE_FREE(ctris[i]);
	}
	free(ctris);
}

#if !PSDK_DISABLED
static bool handleShapeTriangles(PSDKShapeQueryTrianglesCBData* psdkData){
	HandleShapeTrianglesData*	hstData = psdkData->input.userPointer;
	Mat4						inv_mat;
	Mat4						mat;
	Vec3						obj_start;
	Vec3						obj_end;	// line in FOR of shape
	CollInfo*					coll = hstData->coll;
	CTri*						ctri;
	CTri**						ctris;
	PSDKCookedMesh*				mesh;
	U32							prev_flags;
	S32							prev_exit_state;
	S32							hackery = 0;
	S32							bFoundCollision = 0;
			
	PERFINFO_AUTO_START_FUNC();

	psdkSetDestroyCollisionDataCallback(destroyCtriCache);
	psdkShapeGetCookedMesh(	hstData->shape,&mesh);
	psdkShapeGetMat(hstData->shape,mat);

	if(psdkData->input.flags.hasOffset){
		subVec3(mat[3], psdkData->input.sceneOffset, mat[3]);
	}

	transposeMat4Copy(mat,inv_mat);
	mulVecMat4(coll->start,inv_mat,obj_start);
	mulVecMat4(coll->end,inv_mat,obj_end);

    if (!psdkCookedMeshGetCollisionData( mesh,(void **) &ctris))
		return true;// can't do anything with no mesh!
	if (!ctris)
	{
		static CrypticLock csCreateCTris;

		Lock(&csCreateCTris);
		{			
			psdkCookedMeshGetCollisionData( mesh,(void **) &ctris);

			if(!ctris){
				int mesh_tricount=0;
				psdkCookedMeshGetTriCount(mesh,&mesh_tricount);
				ctris = calloc(sizeof(U32) + sizeof(*ctris) * mesh_tricount, 1);
				psdkCookedMeshSetCollisionData( mesh, ctris);
			}
		}
		Unlock(&csCreateCTris);
	}
	
	// Skip over the lock U32.
	
	ctris = (CTri**)((U32*)ctris + 1);
	
	if(psdkCookedMeshIsConvex(mesh)){
		prev_flags = coll->flags;
		coll->flags |= COLL_BOTHSIDES;

		prev_exit_state = coll->tri_state.early_exit;
		coll->tri_state.early_exit = 0;

		hackery = 1;
	}
	
	coll->hasOneWayCollision = !!psdkData->input.flags.hasOneWayCollision;
	
	FOR_BEGIN(i, (S32)psdkData->triCount);
	{
		int		idx;

		idx = psdkData->triIndexes[i];
		ctri = ctris[idx];

		if(!ctri){
			Vec3 tri[3];
			S32 wasDegenerate = 0;

			writeLockU32((U32*)ctris - 1, 0);
			{			
				ctri = ctris[idx];

				if(!ctri){
					ctri = callocStruct(CTri);
					psdkShapeQueryTrianglesByIndex(psdkData->input.shape,psdkData->triIndexes + i,1,&tri,0);
					wasDegenerate = !createCTri(ctri, tri[0], tri[1], tri[2]);
					ctris[idx] = ctri;
				}
			}						
			writeUnlockU32((U32*)ctris - 1);
			
			#if 0
			if(wasDegenerate){
				Vec3		triWorld[3];
				S32			mesh_tricount = 0;
				PSDKActor*	psdkActor = NULL;
				Vec3		boundsMin;
				Vec3		boundsMax;

				psdkShapeGetActor(hstData->shape, &psdkActor);
				
				psdkCookedMeshGetTriCount(mesh,&mesh_tricount);
				psdkActorGetBounds(psdkActor, boundsMin, boundsMax);
				FOR_BEGIN(j, 3);
					mulVecMat4(tri[j], mat, triWorld[j]);
				FOR_END;
				ErrorDetailsf(	"Linear triangle %d of %d (zero-based).\n"
								"MeshPos=(%1.2f, %1.2f, %1.2f)\n"
								"MeshBounds=(%1.2f, %1.2f, %1.2f) - (%1.2f, %1.2f, %1.2f)\n"
								"Vert[0]=(%1.2f, %1.2f, %1.2f)\n"
								"Vert[1]=(%1.2f, %1.2f, %1.2f)\n"
								"Vert[2]=(%1.2f, %1.2f, %1.2f)"
								,
								idx,
								mesh_tricount - 1,
								vecParamsXYZ(mat[3]),
								vecParamsXYZ(boundsMin),
								vecParamsXYZ(boundsMax),
								vecParamsXYZ(triWorld[0]),
								vecParamsXYZ(triWorld[1]),
								vecParamsXYZ(triWorld[2]));
				Errorf("Linear triangle, an artist should probably remove it");
			}
			#endif
		}

		if(ctri->scale > 0.f){
			if (!(coll->flags & COLL_TIMEOFIMPACT))
			{
				checkTriColl(ctri, coll, obj_start, obj_end, mat);
			}
			else
			{
				if (CTriVsSphereSweep(ctri, coll, obj_start, obj_end, mat))
					bFoundCollision = true;
			}
		}
	}
	FOR_END;

	if (bFoundCollision)
	{
		Vec3 vTemp;

		mulVecMat3(coll->ctri_copy.norm, mat, vTemp);
		copyVec3(vTemp, coll->ctri_copy.norm);
		
		mulVecMat4(coll->vHitPos, mat, vTemp);
		copyVec3(vTemp, coll->vHitPos);
	}

	if(hackery){
		coll->tri_state.early_exit = prev_exit_state;
		coll->flags = prev_flags;
	}

	PERFINFO_AUTO_STOP();

	return true;
}

static void collideShapeTris(	CollInfo *coll,
								const PSDKShape* shape)
{
	HandleShapeTrianglesData hstData;

	hstData.coll = coll;
	hstData.shape = shape;

	psdkShapeQueryTrianglesInAABB(	shape,
									coll->sceneOffset,
									coll->aabb_lo,
									coll->aabb_hi,
									handleShapeTriangles,
									&hstData);
}

static void collideAcceleratorTris(	CollInfo *coll, CollAcceleratorShapeEntry * pEntry)
{
	HandleShapeTrianglesData hstData;
	PSDKShapeQueryTrianglesCBData cbData;
	U32 outputIndices[MAX_ACCELERATOR_TRIS];
	int i;

	PERFINFO_AUTO_START_FUNC();

	hstData.coll = coll;
	hstData.shape = pEntry->pShape;

	cbData.triCount = 0;
	cbData.triIndexes = outputIndices;
	cbData.input.userPointer = &hstData;
	cbData.input.shape = pEntry->pShape;

	copyVec3(coll->aabb_lo, cbData.input.aabbMin);
	copyVec3(coll->aabb_hi, cbData.input.aabbMax);

	if(	!coll->sceneOffset ||
		vec3IsZero(coll->sceneOffset))
	{
		cbData.input.flags.hasOffset = 0;
		zeroVec3(cbData.input.sceneOffset);
	}else{
		cbData.input.flags.hasOffset = 1;
		copyVec3(coll->sceneOffset, cbData.input.sceneOffset);
	}

	{
		PSDKActor *pActor = NULL;
		
		cbData.input.flags.hasOneWayCollision = false;

		if (psdkShapeGetActor(pEntry->pShape, &pActor))
		{
			cbData.input.flags.hasOneWayCollision  = pEntry->bHasOneWayCollision;
		}
	}

	for (i=0;i<pEntry->iTriCount;i++)
	{
		if (boxBoxCollision(pEntry->eaTris[i].aabbMin,pEntry->eaTris[i].aabbMax,coll->aabb_lo,coll->aabb_hi))
		{
			// do it
			outputIndices[cbData.triCount++] = pEntry->eaTris[i].uIndex;
		}
	}

	if (cbData.triCount)
	{
		handleShapeTriangles(&cbData);
	}

	PERFINFO_AUTO_STOP();
}

typedef struct HandleTriDetectedInfo
{
	CollAccelerator * pAccelerator;
	CollInfo * pCollInfo;
} HandleTriDetectedInfo;

static bool handleTriDetected(PSDKShapeQueryTrianglesCBData* data)
{
	HandleTriDetectedInfo*	pInfo = (HandleTriDetectedInfo *)data->input.userPointer;
	CollAccelerator * pAccelerator = pInfo->pAccelerator;
	CollAcceleratorShapeEntry * pNewEntry = NULL;
	U32 uShapeTriCount;
	PSDKCookedMesh*				mesh;
	PSDKActor*					pActor;
	int iEntries;

	PERFINFO_AUTO_START_FUNC();

	iEntries = eaSize(&pAccelerator->eaShapeEntries);
	if (iEntries && pAccelerator->eaShapeEntries[iEntries-1]->pShape == data->input.shape)
	{
		// we already collected some tris for this shape
		// Note: This isn't a very good way to handle this - however, this case does not get hit for us currently
		pAccelerator->eaShapeEntries[iEntries-1]->bUseTriInfo = false;
		PERFINFO_AUTO_STOP();
		return false;
	}

	pNewEntry = (CollAcceleratorShapeEntry *)malloc(sizeof(CollAcceleratorShapeEntry));
	pNewEntry->pShape = data->input.shape;
	psdkShapeGetActor(data->input.shape,&pActor);
	pNewEntry->bHasOneWayCollision = psdkActorHasOneWayCollision(pActor);

	psdkShapeGetCookedMesh(	pNewEntry->pShape,&mesh);
	psdkCookedMeshGetTriCount( mesh, &uShapeTriCount);

	if (data->triCount <= MAX_ACCELERATOR_TRIS)
	{
		int i;
		Vec3 aVerts[MAX_ACCELERATOR_TRIS][3];

		pNewEntry->iTriCount = data->triCount;
		pNewEntry->bUseTriInfo = true;

		psdkShapeQueryTrianglesByIndex(pNewEntry->pShape,data->triIndexes,data->triCount,aVerts,true);

		for (i=0;i<pNewEntry->iTriCount;i++)
		{
			int j;
			for (j=0;j<3;j++)
			{
				pNewEntry->eaTris[i].aabbMin[j] = min(min(aVerts[i][0][j],aVerts[i][1][j]),aVerts[i][2][j]);
				pNewEntry->eaTris[i].aabbMax[j] = max(max(aVerts[i][0][j],aVerts[i][1][j]),aVerts[i][2][j]);
			}
			pNewEntry->eaTris[i].uIndex = data->triIndexes[i];
			subVec3(pNewEntry->eaTris[i].aabbMin,pInfo->pCollInfo->sceneOffset,pNewEntry->eaTris[i].aabbMin);
			subVec3(pNewEntry->eaTris[i].aabbMax,pInfo->pCollInfo->sceneOffset,pNewEntry->eaTris[i].aabbMax);
		}
	}
	else
	{
		pNewEntry->bUseTriInfo = false;
	}

	eaPush(&pAccelerator->eaShapeEntries,pNewEntry);

	PERFINFO_AUTO_STOP();

	return true;
}

#endif

static void setTriCollState(CollInfo *coll)
{
	F32		invmag,mag;

	subVec3(coll->end,coll->start,coll->dir);

	mag = fsqrt(SQR(coll->dir[0]) + SQR(coll->dir[1]) + SQR(coll->dir[2]));
	if (mag > 0)
		invmag = 1.f/mag;
	else
		invmag = 1.f;
	coll->tri_state.inv_linemag = invmag;

	coll->line_len	= normalVec3(coll->dir);
	coll->tri_state.linelen = coll->line_len;
	coll->tri_state.linelen2 = coll->line_len * 0.5;

	if (coll->flags & (COLL_BOTHSIDES | COLL_DISTFROMCENTER))
		coll->tri_state.early_exit = 0;
	else
		coll->tri_state.early_exit = 1;
}

static void setupCollInfo(	CollInfo *coll,
							const Vec3 start,
							const Vec3 end,
							F32 radius,
							CollFlags flags)
{
	int i;
	flags |= COLL_NORMALTRI;

	coll->toi		= FLT_MAX;
	coll->sqdist	= GRID_MAXDIST;
	coll->ctri		= NULL;
	coll->radius	= radius;
	coll->flags		= flags;
	copyVec3(start,coll->start);
	copyVec3(end,coll->end);
	coll->coll_count= 0;
	for(i=0;i<3;i++)
	{
		coll->aabb_lo[i] = MINF(start[i], end[i]) - radius;
		coll->aabb_hi[i] = MAXF(start[i], end[i]) + radius;
	}
	setTriCollState(coll);
}

static int collideCheckInfo(CollInfo *coll)
{
	if (coll->sqdist < GRID_MAXDIST)
	{
		Mat3	m, m2;

		normScaleToMat(coll->ctri->norm,coll->ctri->scale,m);
		mulMat3(coll->mat,m,m2);
		copyMat3(m2, coll->mat);
		return 1;
	}
	return 0;
}

// I think this function could conceivably be useful to someone someday, so I'm leaving it here - [RMARR - 11/11/11]
#if 0
static bool _capsuleInCapsule(const Vec3 vParentStart,const Vec3 vParentEnd,F32 fParentRadius,const Vec3 vChildStart,const Vec3 vChildEnd,F32 fChildRadius)
{
	F32 fRadiusDiffSq = (fParentRadius-fChildRadius)*(fParentRadius-fChildRadius);

	Vec3	dv;
	F32		len;

	subVec3(vParentEnd,vParentStart,dv);
	len = normalVec3(dv);

	if (PointLineDistSquared(vChildStart,vParentStart,dv,len,NULL) > fRadiusDiffSq)
	{
		return false;
	}
	if (PointLineDistSquared(vChildEnd,vParentStart,dv,len,NULL) > fRadiusDiffSq)
	{
		return false;
	}

	return true;
}
#endif

void collideBuildAccelerator(const Vec3 start, const Vec3 end,const Vec3 vPadding,CollInfo *coll)
{
#if PSDK_DISABLED
	return;
#else
	ShapeList shape_list = {0};
	int		j;
	Vec3 vNewAabbMin,vNewAabbMax;

	HandleTriDetectedInfo info;
	info.pCollInfo = coll;
	info.pAccelerator = &g_accelerator;

	PERFINFO_AUTO_START_FUNC();

	for(j=0;j<3;j++)
	{
		vNewAabbMin[j] = MINF(start[j], end[j]) - vPadding[j];
		vNewAabbMax[j] = MAXF(start[j], end[j]) + vPadding[j];
	}

	// Try being super lazy about building the accelerator (TODO - this check is stupid because it throws out good results)
	if (distance3Squared(vNewAabbMin,g_accelerator.aabb_lo) < 6.0f && distance3Squared(vNewAabbMax,g_accelerator.aabb_hi) < 6.0f)
	{
		PERFINFO_AUTO_STOP_FUNC();
		g_bAcceleratorActive = 1;
		return;
	}

	collideClearAccelerator();
	g_bAcceleratorActive = 1;
	copyVec3(vNewAabbMin,g_accelerator.aabb_lo);
	copyVec3(vNewAabbMax,g_accelerator.aabb_hi);
	setupCollInfo(coll, start, end, 0.0f, 0);

	gatherShapesAABB(	coll,
					&shape_list,
					g_accelerator.aabb_lo,
					g_accelerator.aabb_hi);

	FOR_BEGIN(i, shape_list.shapeCount);
	{
		PSDKActor* psdkActor = NULL;

		
		psdkShapeGetActor(shape_list.shapes[i], &psdkActor);

		if(psdkActorGetIgnore(psdkActor)){
			continue;
		}

		if(	coll->actorIgnoredCB &&
			coll->actorIgnoredCB(coll->userPointer, psdkActor))
		{
			continue;
		}

		psdkShapeQueryTrianglesInAABB(	shape_list.shapes[i],
										coll->sceneOffset,
										coll->aabb_lo,
										coll->aabb_hi,
										handleTriDetected,
										&info);
	}
	FOR_END;

	if(shape_list.eaShapes){
		eaDestroy(&shape_list.eaShapes);
	}

	PERFINFO_AUTO_STOP_FUNC();
#endif
}

void collideClearAccelerator()
{
	int i;
	const int iNumEntries = eaSize(&g_accelerator.eaShapeEntries);

	for (i=0;i<iNumEntries;i++)
	{
		free(g_accelerator.eaShapeEntries[i]);
	}
	eaDestroy(&g_accelerator.eaShapeEntries);

	g_accelerator.eaShapeEntries = NULL;

	g_bAcceleratorActive = 0;
}

void collideDisableAccelerator()
{
	g_bAcceleratorActive = 0;
}

static bool _aabbIsInside(const Vec3 inner_min,const Vec3 inner_max,const Vec3 outer_min,const Vec3 outer_max)
{
	int i;
	for (i=0;i<3;i++)
	{
		if (inner_min[i] < outer_min[i] || inner_max[i] > outer_max[i])
		{
			return false;
		}
	}

	return true;
}

int collideWithAccelerator(const Vec3 start,const Vec3 end,CollInfo *coll,F32 radius,int flags)
{
#if PSDK_DISABLED
	return 0;
#else
	ShapeList shape_list = {0};
	int i;
	PERFINFO_AUTO_START("collideWithAccelerator",1);

	setupCollInfo(coll, start, end, radius, flags);

	if (!g_bEnableAcclerator || !g_bAcceleratorActive || !_aabbIsInside(coll->aabb_lo,coll->aabb_hi,g_accelerator.aabb_lo,g_accelerator.aabb_hi))
	{
		// The accelerator is just a guess, so sometimes it won't work.  In that case, we just fall back to the "slow" regular collide.
		int iReturn = collide(start,end,coll,radius,flags);

		PERFINFO_AUTO_STOP();

		return iReturn;
	}

	// If we got here, we're in luck. We can use the reduced set of shapes, and we don't have to go look them up again
	for (i=0;i<eaSize(&g_accelerator.eaShapeEntries);i++)
	{
		PSDKActor* psdkActor = NULL;
		
		psdkShapeGetActor(g_accelerator.eaShapeEntries[i]->pShape, &psdkActor);

		if(psdkActorGetIgnore(psdkActor)){
			continue;
		}

		if(	coll->actorIgnoredCB &&
			coll->actorIgnoredCB(coll->userPointer, psdkActor))
		{
			continue;
		}

		if (g_accelerator.eaShapeEntries[i]->bUseTriInfo)
		{
			collideAcceleratorTris(coll, g_accelerator.eaShapeEntries[i]);
		}
		else
		{
			collideShapeTris(coll, g_accelerator.eaShapeEntries[i]->pShape);
		}
	}

	PERFINFO_AUTO_STOP();

	return collideCheckInfo(coll);
#endif
}

int collide(const Vec3 start,const Vec3 end,CollInfo *coll,F32 radius,int flags)
{
#if PSDK_DISABLED
	return 0;
#else
	ShapeList shape_list = {0};

	PERFINFO_AUTO_START_FUNC();

	setupCollInfo(coll, start, end, radius, flags);

	if(objCacheFind(coll->wc,
					start,
					end,
					radius,
					coll->filterBits,
					shape_list.shapesBuffer,
					&shape_list.shapeCount))
	{
		shape_list.shapes = shape_list.shapesBuffer;
	}else{
		S32		fitsInCache;
		F32		block_radius;
		Vec3	block_start,block_end;

		copyVec3(start,block_start);
		copyVec3(end,block_end);
		block_radius = radius;

		fitsInCache = objCacheFitBlock(	block_start,
										block_end,
										&block_radius);

		gatherShapes(	coll,
						&shape_list,
						block_start,
						block_end,
						block_radius);

		if(	fitsInCache &&
			shape_list.shapeCount < ARRAY_SIZE(shape_list.shapesBuffer))
		{
			objCacheSet(coll->wc,
						block_start,
						block_end,
						coll->filterBits,
						shape_list.shapesBuffer,
						shape_list.shapeCount);
		}
	}

	FOR_BEGIN(i, shape_list.shapeCount);
	{
		PSDKActor* psdkActor = NULL;
		
		psdkShapeGetActor(shape_list.shapes[i], &psdkActor);

		if(psdkActorGetIgnore(psdkActor)){
			continue;
		}

		if(	coll->actorIgnoredCB &&
			coll->actorIgnoredCB(coll->userPointer, psdkActor))
		{
			continue;
		}

		collideShapeTris(coll, shape_list.shapes[i]);
	}
	FOR_END;

	if(shape_list.eaShapes){
		eaDestroy(&shape_list.eaShapes);
	}

	PERFINFO_AUTO_STOP();

	return collideCheckInfo(coll);
#endif
}

int collideIgnoreActor(	const Vec3 start,
						const Vec3 end,
						CollInfo *coll,
						F32 radius,
						CollFlags flags,
						const PSDKActor *actorIgnore)
{
#if PSDK_DISABLED
	return 0;
#else
	ShapeList shape_list = {0};

	setupCollInfo(coll, start, end, radius, flags);

	gatherShapes(	coll,
					&shape_list,
					start,
					end,
					radius);

	FOR_BEGIN(i, shape_list.shapeCount);
	{
		PSDKActor* actor = NULL;

		psdkShapeGetActor(shape_list.shapes[i], &actor);

		if(actor != actorIgnore){
			collideShapeTris(coll,shape_list.shapes[i]);
		}
	}
	FOR_END;

	if(shape_list.eaShapes){
		eaDestroy(&shape_list.eaShapes);
	}

	return collideCheckInfo(coll);
#endif
}

int collideShape(	const Vec3 start,
					const Vec3 end,
					CollInfo *coll,
					F32 radius,
					CollFlags flags,
					const void *shape)
{
#if !PSDK_DISABLED
	setupCollInfo(coll, start, end, radius, flags);
	collideShapeTris(coll, shape);
	if(coll->sqdist < GRID_MAXDIST)
	{
		Mat3	m, m2;

		normScaleToMat(coll->ctri->norm,coll->ctri->scale,m);
		mulMat3(coll->mat,m,m2);
		copyMat3(m2, coll->mat);
		return 1;
	}
#endif
	return 0;
}

int collideCap(const Vec3 pos, const Capsule *cap, CollInfo *coll, CollFlags flags)
{
	Vec3 bot, top;

	addVec3(pos, cap->vStart, bot);
	scaleAddVec3(cap->vDir, cap->fLength, bot, top);
	return collide(top, bot, coll, cap->fRadius, flags);
}

int collideShapeCap(const Vec3 pos, const Capsule *cap, CollInfo *coll, CollFlags flags, const void *shape)
{
	Vec3 bot, top;

	addVec3(pos, cap->vStart, bot);
	scaleAddVec3(cap->vDir, cap->fLength, bot, top);
	return collideShape(top, bot, coll, cap->fRadius, flags, shape);
}

int collideIgnoreActorCap(const Vec3 pos, const Capsule *cap, CollInfo *coll, CollFlags flags, const PSDKActor *actorIgnore)
{
	Vec3 bot, top;

	addVec3(pos, cap->vStart, bot);
	scaleAddVec3(cap->vDir, cap->fLength, bot, top);
	return collideIgnoreActor(top, bot, coll, cap->fRadius, flags, actorIgnore);
}

int collideSweepIgnoreActorCap(	const Vec3 start, 
								const Vec3 end, 
								const Capsule *cap, 
								CollInfo *coll, 
								CollFlags flags, 
								const PSDKActor *actorIgnore)
{
	Vec3 pos;
	Vec3 dir;
	F32 len;
	int i;
	int iterations;

	subVec3(end, start, dir);
	len = normalVec3(dir);

	copyVec3(start, pos);
	iterations = ceil(len / cap->fRadius);
	for(i=0; i<iterations; i++)
	{
		if(collideIgnoreActorCap(pos, cap, coll, flags, actorIgnore))
			return 1;

		if(len>cap->fRadius)
		{
			scaleAddVec3(dir, cap->fRadius, pos, pos);
			len -= cap->fRadius;
		}
		else if(len>0)
		{
			scaleAddVec3(dir, len, pos, pos);
			len = 0;
		}
	}

	return 0;
}

static F32 pointTriNearestPoint(const Vec3 pt, const Vec3 p1, const Vec3 p2, const Vec3 p3, Vec3 isect)
{
	Vec3 start;
	Vec3 d21, d31, d32, dp1, dp2, dp3;
	Vec3 norm;
	Vec3 cross;

	subVec3(p2, p1, d21);
	subVec3(p3, p1, d31);
	subVec3(p3, p2, d32);

	crossVec3(d21, d31, norm);
	normalVec3(norm);

	subVec3(pt, p1, dp1);
	scaleAddVec3(norm, -dotVec3(dp1, norm), pt, start);

	subVec3(start, p1, dp1);
	subVec3(start, p2, dp2);
	subVec3(start, p3, dp3);

	crossVec3(dp1, d31, cross);
	if(dotVec3(cross, norm)<0)
	{
		return FLT_MAX;
	}

	crossVec3(d21, dp1, cross);
	if(dotVec3(cross, norm)<0)
	{
		return FLT_MAX;
	}

	crossVec3(d32, dp2, cross);
	if(dotVec3(cross, norm)<0)
	{
		return FLT_MAX;
	}

	if(isect)
	{
		copyVec3(start, isect);
	}

	return distance3(pt, start);
}

static F32 pointPlaneNearestPoint(const Vec3 min, const Vec3 max, const Vec3 pt, Vec3 isect)
{
	static CTri *tri1 = NULL, *tri2 = NULL;
	F32 dist;
	Vec3 minsect;
	F32 mindist;
	Vec3 tempIsect;
	int i, j;
	int same_axis = 0, otheraxis1, otheraxis2;
	Vec3 plane[2][2];
	

	for(i=0; i<3; i++)
	{
		if(nearf(min[i], max[i]))
		{
			same_axis = i;
			break;
		}
	}
	otheraxis1 = (same_axis + 1) % 3;
	otheraxis2 = (same_axis + 2) % 3;

	for(i=0; i<2; i++)
	{
		for(j=0; j<2; j++)
		{
			copyVec3(min, plane[i][j]);

			plane[i][j][otheraxis1] = j ? min[otheraxis1] : max[otheraxis1];
			plane[i][j][otheraxis2] = i ? min[otheraxis2] : max[otheraxis2];
		}
	}

	mindist = pointTriNearestPoint(pt, plane[0][0], plane[0][1], plane[1][0], minsect);

	dist = pointTriNearestPoint(pt, plane[1][1], plane[1][0], plane[0][1], tempIsect);
	if(dist<mindist)
	{
		mindist = dist;
		copyVec3(tempIsect, minsect);
	}

	dist = pointLineDistSquared(pt, plane[0][0], plane[0][1], tempIsect);
	if(mindist == FLT_MAX || dist < SQR(mindist))
	{
		mindist = sqrt(dist);
		copyVec3(tempIsect, minsect);
	}

	dist = pointLineDistSquared(pt, plane[0][1], plane[1][1], tempIsect);
	if(mindist == FLT_MAX || dist < SQR(mindist))
	{
		mindist = sqrt(dist);
		copyVec3(tempIsect, minsect);
	}

	dist = pointLineDistSquared(pt, plane[1][1], plane[1][0], tempIsect);
	if(mindist == FLT_MAX || dist < SQR(mindist))
	{
		mindist = sqrt(dist);
		copyVec3(tempIsect, minsect);
	}

	dist = pointLineDistSquared(pt, plane[1][0], plane[0][0], tempIsect);
	if(mindist == FLT_MAX || dist < SQR(mindist))
	{
		mindist = sqrt(dist);
		copyVec3(tempIsect, minsect);
	}

	if(isect)
	{
		copyVec3(minsect, isect);
	}

	return mindist;
}

F32 triLineNearestPoint(const Vec3 start, const Vec3 end, Vec3 verts[3], Vec3 isect)
{
	static CTri *tri = NULL;
	Vec3 dir;
	F32 len;
	F32 dist;
	Vec3 linedir;
	F32 linedist;
	Vec3 minsect;
	F32 mindist;
	Vec3 tempIsect;
	Vec3 dummy;

	subVec3(end, start, dir);
	len = normalVec3(dir);

	if(!tri)
	{
		allocCTri(&tri);
	}

	createCTri(tri, verts[0], verts[1], verts[2]);

	if(ctriCollide(tri, start, end, tempIsect)==1)
	{
		if(isect)
		{
			copyVec3(tempIsect, isect);
		}
		return distance3(tempIsect, start);
	}

	subVec3(verts[1], verts[0], linedir);
	linedist = normalVec3(linedir);
	mindist = LineLineDistSquared(verts[0], linedir, linedist, minsect, start, dir, len, dummy);

	subVec3(verts[2], verts[0], linedir);
	linedist = normalVec3(linedir);
	dist = LineLineDistSquared(verts[0], linedir, linedist, tempIsect, start, dir, len, dummy);
	
	if(dist<mindist)
	{
		mindist = dist;
		copyVec3(tempIsect, minsect);
	}

	subVec3(verts[2], verts[1], linedir);
	linedist = normalVec3(linedir);
	dist = LineLineDistSquared(verts[1], linedir, linedist, tempIsect, start, dir, len, dummy);

	if(dist<mindist)
	{
		mindist = dist;
		copyVec3(tempIsect, minsect);
	}

	if(isect)
	{
		copyVec3(minsect, isect);
	}

	return mindist;
}

static F32 linePlaneNearestPoint(const Vec3 min, const Vec3 max, const Vec3 start, const Vec3 end, Vec3 isect)
{
	static CTri *tri1 = NULL, *tri2 = NULL;
	Vec3 dir;
	F32 len;
	F32 dist;
	Vec3 linedir;
	F32 linedist;
	Vec3 minsect;
	F32 mindist;
	Vec3 tempIsect;
	Vec3 dummy;
	int i, j;
	int same_axis = 0, otheraxis1, otheraxis2;
	Vec3 plane[2][2];

	subVec3(end, start, dir);
	len = normalVec3(dir);

	for(i=0; i<3; i++)
	{
		if(nearf(min[i], max[i]))
		{
			same_axis = i;
			break;
		}
	}
	otheraxis1 = (same_axis + 1) % 3;
	otheraxis2 = (same_axis + 2) % 3;

	for(i=0; i<2; i++)
	{
		for(j=0; j<2; j++)
		{
			copyVec3(min, plane[i][j]);

			plane[i][j][otheraxis1] = j ? min[otheraxis1] : max[otheraxis1];
			plane[i][j][otheraxis2] = i ? min[otheraxis2] : max[otheraxis2];
		}
	}

	if(!tri1)
	{
		allocCTri(&tri1);
	}

	if(!tri2)
	{
		allocCTri(&tri2);
	}

	createCTri(tri1, plane[0][0], plane[0][1], plane[1][1]);
	createCTri(tri2, plane[0][0], plane[1][0], plane[1][1]);

	if(ctriCollide(tri1, start, end, tempIsect)==1)
	{
		if(isect)
		{
			copyVec3(tempIsect, isect);
		}
		return distance3(tempIsect, start);
	}

	if(ctriCollide(tri2, start, end, tempIsect)==1)
	{
		if(isect)
		{
			copyVec3(tempIsect, isect);
		}
		return distance3(tempIsect, start);
	}

	subVec3(plane[0][1], plane[0][0], linedir);
	linedist = normalVec3(linedir);
	mindist = LineLineDistSquared(plane[0][0], linedir, linedist, minsect, start, dir, len, dummy);

	subVec3(plane[1][0], plane[0][0], linedir);
	linedist = normalVec3(linedir);
	dist = LineLineDistSquared(plane[0][0], linedir, linedist, tempIsect, start, dir, len, dummy);

	if(dist<mindist)
	{
		mindist = dist;
		copyVec3(tempIsect, minsect);
	}

	subVec3(plane[0][1], plane[1][1], linedir);
	linedist = normalVec3(linedir);
	dist = LineLineDistSquared(plane[1][1], linedir, linedist, tempIsect, start, dir, len, dummy);

	if(dist<mindist)
	{
		mindist = dist;
		copyVec3(tempIsect, minsect);
	}

	subVec3(plane[1][0], plane[1][1], linedir);
	linedist = normalVec3(linedir);
	dist = LineLineDistSquared(plane[1][1], linedir, linedist, tempIsect, start, dir, len, dummy);

	if(dist<mindist)
	{
		mindist = dist;
		copyVec3(tempIsect, minsect);
	}

	if(isect)
	{
		copyVec3(minsect, isect);
	}

	return sqrt(mindist);
}

F32 boxLineNearestPoint(const Vec3 local_min, const Vec3 local_max, const Mat4 world_mat, const Mat4 inv_mat, 
						const Vec3 start, const Vec3 end, Vec3 isect)
{
	int i, j;
	F32 mindist = FLT_MAX, dist;
	Vec3 min, max;
	Vec3 minisect;
	Vec3 startTrans;
	Vec3 endTrans;
	Vec3 isectTrans;
	Mat4 invMat;

	invertMat4(world_mat, invMat);
	mulVecMat4(start, invMat, startTrans);
	mulVecMat4(end, invMat, endTrans);

	if(pointBoxCollision(startTrans, local_min, local_max))
	{
		if(isect)
		{
			copyVec3(start, isect);
		}

		return 0;
	}

	if(pointBoxCollision(endTrans, local_min, local_max))
	{
		if(isect)
		{
			copyVec3(end, isect);
		}

		return distance3(start, end);
	}

	for(i=0; i<3; i++)
	{
		for(j=0; j<2; j++)
		{
			copyVec3(local_min, min);
			copyVec3(local_max, max);

			min[i] = j ? min[i] : max[i];
			max[i] = j ? min[i] : max[i];

			dist = linePlaneNearestPoint(min, max, startTrans, endTrans, isectTrans);
			if(dist < mindist)
			{
				mindist = dist;
				copyVec3(isectTrans, minisect);
			}
		}
	}

	if(isect)
	{
		mulVecMat4(minisect, world_mat, isect);
	}	

	return mindist;
}

F32 boxPointNearestPoint(const Vec3 local_min, const Vec3 local_max, const Mat4 world_mat, const Mat4 inv_mat, 
						const Vec3 pt, Vec3 isect)
{
	int i, j;
	F32 mindist = FLT_MAX, dist;
	Vec3 min, max;
	Vec3 minisect;
	Vec3 startTrans;
	Vec3 isectTrans;
	Mat4 invMat;

	invertMat4(world_mat, invMat);
	mulVecMat4(pt, invMat, startTrans);

	if(pointBoxCollision(startTrans, local_min, local_max))
	{
		if(isect)
		{
			copyVec3(pt, isect);
		}

		return 0;
	}

	for(i=0; i<3; i++)
	{
		for(j=0; j<2; j++)
		{
			copyVec3(local_min, min);
			copyVec3(local_max, max);

			min[i] = j ? min[i] : max[i];
			max[i] = j ? min[i] : max[i];

			dist = pointPlaneNearestPoint(min, max, startTrans, isectTrans);
			if(dist < mindist)
			{
				mindist = dist;
				copyVec3(isectTrans, minisect);
			}
		}
	}

	if(isect)
	{
		mulVecMat4(minisect, world_mat, isect);
	}	

	return mindist;
}