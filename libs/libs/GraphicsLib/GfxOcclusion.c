#include "rgb_hsv.h"
#include "GfxOcclusionDraw.h"
#include "GfxOcclusion.h"
#include "ScratchStack.h"
#include "cpu_count.h"
#include "ThreadManager.h"
#include "XboxThreads.h"
#include "EventTimingLog.h"
#include "ThreadSafeMemoryPool.h"
#include "MemRef.h"
#include "timing_profiler_interface.h"

#include "wlModelInline.h"

#include "GfxModelCache.h"
#include "GfxGeo.h"
#include "GfxDebug.h"
#include "Clipper.h" //include this before GfxSprite.h when inside GraphicsLib to get the fully inlined clipper functions
#include "GfxPrimitive.h"
#include "GfxSprite.h"
#include "bounds.h"
#include "mathutil.inl"

#include "GfxOcclusionClip.h"
#include "file.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:zoThread", BUDGET_Renderer););

#if 0
#define CHECKHEAP 	{assertHeapValidateAll();}
#else
#define CHECKHEAP {}
#endif

MP_DEFINE(Occluder);

static int s_iZOcclusionThread;
AUTO_CMD_INT(s_iZOcclusionThread, zocclusionThread) ACMD_CMDLINE;

static int s_iDebugCapsuleScreenExtents;
AUTO_CMD_INT(s_iDebugCapsuleScreenExtents, DebugCapsuleScreenExtents) ACMD_CMDLINE;

static int gbEnableExactTests=1;
AUTO_CMD_INT(gbEnableExactTests, zoEnableExactTests) ACMD_CMDLINE;

static int gbEnableJitter=1;
AUTO_CMD_INT(gbEnableJitter, zoEnableJitter) ACMD_CMDLINE;

static ThreadSafeMemoryPool mpOccluder;

AUTO_RUN;
void zoStartup(void)
{
	if (getNumRealCpus() > 1)
		s_iZOcclusionThread = 1;
	threadSafeMemoryPoolInit(&mpOccluder, 64, sizeof(Occluder), "Occluder");
}

////////////////////////////////////////////////////
////////////////////////////////////////////////////

static void zoDrawModel(SA_PARAM_NN_VALID GfxZBuffData *zo, ModelLOD *m, Mat4 matModelToEye, U32 ok_materials, bool check_facing);
static void zoDrawVolume(SA_PARAM_NN_VALID GfxZBuffData *zo, Vec3 minBounds, Vec3 maxBounds, VolumeFaces faceBits, Mat4 matModelToEye);

////////////////////////////////////////////////////
////////////////////////////////////////////////////

__forceinline static Occluder *createOccluder(ModelLOD *m, const Vec3 minBounds, const Vec3 maxBounds, VolumeFaces faceBits, const Mat4 modelToWorld, float screenSpace, U32 ok_materials, bool double_sided)
{
	Occluder *o;

	o = threadSafeMemoryPoolAlloc(&mpOccluder);
	ZeroStructForce(o);
	if (m)
		m->unpacked_last_used_timestamp = wl_frame_timestamp; // Flag as used so it won't be freed this frame
	o->m = m;
	if (minBounds)
		copyVec3(minBounds, o->minBounds);
	if (maxBounds)
		copyVec3(maxBounds, o->maxBounds);
	o->faceBits = faceBits;
	copyMat4(modelToWorld, o->modelToWorld);
	o->screenSpace = screenSpace;
	o->ok_materials = ok_materials;
	o->double_sided = double_sided;
	return o;
}

__forceinline static void freeOccluder(SA_PRE_OP_VALID SA_POST_P_FREE Occluder *o)
{
	threadSafeMemoryPoolFree(&mpOccluder, o);
}

////////////////////////////////////////////////////
////////////////////////////////////////////////////

__forceinline static void clipToZ(const Frustum *frustum, const Vec3 inside, const Vec3 outside, Vec3 tpos)
{
	Vec3 diff;
	float t;

	subVec3(inside, outside, diff);
	t = (frustum->znear - outside[2]) / diff[2];

	scaleVec3(diff, t, diff);
	addVec3(outside, diff, tpos);
}

__forceinline static int outOfBounds3H(const Vec4H tmin, const Vec4H tmax)
{
	static const Vec4H unitPlusEpsilon = { 1 + TEST_Z_OFFSET, 1 + TEST_Z_OFFSET, 1 + TEST_Z_OFFSET, 1 + TEST_Z_OFFSET };
	Vec4H Rmin = vecCmpGreaterEqExp4H(tmin, unitPlusEpsilon); // Care about first 3 components here
	Vec4H Rmax = vecCmpLessEqExp4H(tmax, g_vec4NegUnitH); // Care about first 2 components here

	Vec4H R = vecOrExp4H(Rmin, vecSwizzleExp4H(Rmax, S_XYXY)); // First 3
	R = vecOrExp4H(R, vecOrExp4H(vecSwizzleExp4H(R, S_YXXX), vecSwizzleExp4H(R, S_ZXXX))); // First 1
	return *(int*)&Vec4HToVec4(R)[0];
}

__forceinline static int outOfBounds(const Vec3 tmin, const Vec3 tmax)
{
#if PLATFORM_CONSOLE
	F32 isOutOfBounds;
	isOutOfBounds = FloatBranchFGE(tmin[0], 1,  1, 0);
	isOutOfBounds = FloatBranchFLE(tmax[0], -1, 1, isOutOfBounds);
	isOutOfBounds = FloatBranchFGE(tmin[1], 1,  1, isOutOfBounds);
	isOutOfBounds = FloatBranchFLE(tmax[1], -1, 1, isOutOfBounds);
	isOutOfBounds = FloatBranchFGE(tmin[2], 1,  1, isOutOfBounds);

	return isOutOfBounds != 0;
#else
	if (tmin[0] >= 1 || tmax[0] <= -1)
		return 1;

	if (tmin[1] >= 1 || tmax[1] <= -1)
		return 1;

	if (tmin[2] >= 1)
		return 1;

	return 0;
#endif
}


static int getScreenExtents(const Frustum *frustum, const Mat44 projection_mat, const Vec4 transBounds[8], float *fminX, float *fminY, 
							float *fmaxX, float *fmaxY, ZType *minZ, bool bClipToScreen)
{
	int		posClipped[8];
	int		x,y,z, zclipped = 0, idx, idx2;
	Vec3	tpos, tpos2;
	Vec3	tmin, tmax;

	setVec3(tmin, 1, 1, 1);
	setVec3(tmax, -1, -1, -1);

	for(idx=0;idx<8;idx++)
	{
		if (transBounds[idx][2] > frustum->znear)
		{
			posClipped[idx] = 1;
			zclipped++;
		}
		else
		{
			posClipped[idx] = 0;
		}
	}

	if (zclipped == 8)
		return 0;

	for(z=0;z<2;z++)
	{
		for(y=0;y<2;y++)
		{
			for(x=0;x<2;x++)
			{
				idx = boundsIndex(x, y, z);
				if (!posClipped[idx])
				{
					mulVec3ProjMat44(transBounds[idx], projection_mat, tpos2);
					vec3RunningMinMax(tpos2, tmin, tmax);

					idx2 = boundsIndex(x, y, !z);
					if (posClipped[idx2])
					{
						clipToZ(frustum, transBounds[idx], transBounds[idx2], tpos);
						mulVec3ProjMat44(tpos, projection_mat, tpos2);
						vec3RunningMinMax(tpos2, tmin, tmax);
					}

					idx2 = boundsIndex(x, !y, z);
					if (posClipped[idx2])
					{
						clipToZ(frustum, transBounds[idx], transBounds[idx2], tpos);
						mulVec3ProjMat44(tpos, projection_mat, tpos2);
						vec3RunningMinMax(tpos2, tmin, tmax);
					}

					idx2 = boundsIndex(!x, y, z);
					if (posClipped[idx2])
					{
						clipToZ(frustum, transBounds[idx], transBounds[idx2], tpos);
						mulVec3ProjMat44(tpos, projection_mat, tpos2);
						vec3RunningMinMax(tpos2, tmin, tmax);
					}
				}
			}
		}
	}

	if (outOfBounds(tmin, tmax))
		return 0;

	if (bClipToScreen)
	{
		MAX1F(tmin[0], -1);
		MAX1F(tmin[1], -1);
		MIN1F(tmax[0], 1);
		MIN1F(tmax[1], 1);
	}

	*fminX = tmin[0] * 0.5f + 0.5f;
	*fminY = tmin[1] * 0.5f + 0.5f;
	*fmaxX = tmax[0] * 0.5f + 0.5f;
	*fmaxY = tmax[1] * 0.5f + 0.5f;

	*minZ = tmin[2] * ZMAX;

	return 1;
}

//#define VERIFY_RESULTS 1
//#define USE_SCALAR_CODE 1
#if VERIFY_RESULTS
static F32 occ_verify_tol = 0.0003;
#endif

static int getScreenExtentsNoZClip(const Mat44 projection_mat, const Vec4 transBounds[8], float *fminX, float *fminY, 
								   float *fmaxX, float *fmaxY, ZType *minZ)
{
	int ret=1;

#if VERIFY_RESULTS
	F32 opt_fminX=0, opt_fmaxX=0, opt_fminY=0, opt_fmaxY=0, opt_minZ=0;
#endif
#if !USE_SCALAR_CODE || VERIFY_RESULTS
	Vec4H vtmin, vtmax;
	//Mat44H vproj_mat;
	assert(IS_ALIGNED((intptr_t)&projection_mat[0][0], 16));
	assert(IS_ALIGNED((intptr_t)&transBounds[1], 16)); // doing index 1 in case this gets changed to Vec3s or something that wouldn't align

	//mat4to44H(projection_mat, vproj_mat);
#if _XBOX // about 20% faster on Xbox, 50% slower on PC, unknown on PS3
	{
		const Vec4H *aligned_transBounds = (const Vec4H*)&transBounds[0][0];
		Mat44H m;
		m[0] = vec4toVec4H_aligned(aligned_transBounds[0]);
		m[1] = vec4toVec4H_aligned(aligned_transBounds[1]);
		m[2] = vec4toVec4H_aligned(aligned_transBounds[2]);
		m[3] = vec4toVec4H_aligned(aligned_transBounds[3]);
		mulVec3HPProjMat44H(m, (Mat44H*)projection_mat, m);
		vtmax = vtmin = m[0];
		vec4HRunningMinMax(m[1], vtmin, vtmax);
		vec4HRunningMinMax(m[2], vtmin, vtmax);
		vec4HRunningMinMax(m[3], vtmin, vtmax);
		m[0] = vec4toVec4H_aligned(aligned_transBounds[4]);
		m[1] = vec4toVec4H_aligned(aligned_transBounds[5]);
		m[2] = vec4toVec4H_aligned(aligned_transBounds[6]);
		m[3] = vec4toVec4H_aligned(aligned_transBounds[7]);
		mulVec3HPProjMat44H(m, (Mat44H*)projection_mat, m);
		vec4HRunningMinMax(m[0], vtmin, vtmax);
		vec4HRunningMinMax(m[1], vtmin, vtmax);
		vec4HRunningMinMax(m[2], vtmin, vtmax);
		vec4HRunningMinMax(m[3], vtmin, vtmax);

	}
#else
	{
		Vec4H vtpos, temp;
		temp = vec4toVec4H_aligned(transBounds[0]);
		vtmin = mulVec3HProjMat44H(temp, temp, temp, (Mat44H*)projection_mat);
		vtmax = vtmin;

		temp = vec4toVec4H_aligned(transBounds[1]);
		vtpos = mulVec3HProjMat44H(temp, temp, temp, (Mat44H*)projection_mat);
		vec4HRunningMinMax(vtpos, vtmin, vtmax);

		temp = vec4toVec4H_aligned(transBounds[2]);
		vtpos = mulVec3HProjMat44H(temp, temp, temp, (Mat44H*)projection_mat);
		vec4HRunningMinMax(vtpos, vtmin, vtmax);

		temp = vec4toVec4H_aligned(transBounds[3]);
		vtpos = mulVec3HProjMat44H(temp, temp, temp, (Mat44H*)projection_mat);
		vec4HRunningMinMax(vtpos, vtmin, vtmax);

		temp = vec4toVec4H_aligned(transBounds[4]);
		vtpos = mulVec3HProjMat44H(temp, temp, temp, (Mat44H*)projection_mat);
		vec4HRunningMinMax(vtpos, vtmin, vtmax);

		temp = vec4toVec4H_aligned(transBounds[5]);
		vtpos = mulVec3HProjMat44H(temp, temp, temp, (Mat44H*)projection_mat);
		vec4HRunningMinMax(vtpos, vtmin, vtmax);

		temp = vec4toVec4H_aligned(transBounds[6]);
		vtpos = mulVec3HProjMat44H(temp, temp, temp, (Mat44H*)projection_mat);
		vec4HRunningMinMax(vtpos, vtmin, vtmax);

		temp = vec4toVec4H_aligned(transBounds[7]);
		vtpos = mulVec3HProjMat44H(temp, temp, temp, (Mat44H*)projection_mat);
		vec4HRunningMinMax(vtpos, vtmin, vtmax);
	}
#endif
	if (outOfBounds3H(vtmin, vtmax))
	{
		ret = 0;
	} else {
		static const Vec4H zb_one_over_dim_vec4h = {-ZB_ONE_OVER_DIM, -ZB_ONE_OVER_DIM, ZB_ONE_OVER_DIM, ZB_ONE_OVER_DIM};
		static const Vec4H vec4h_one_half = {0.5, 0.5, 0.5, 0.5};
#if _PS3
		Vec4H t = vtmin;
		assertmsg(0, "Need a shuffle operation to get float4(tmin.xy, tmax.xy)");
#else
		Vec4H t = vecMixExp4H(vtmin, vtmax, S_XYXY);
#endif

		t = vecAddExp4H(t, zb_one_over_dim_vec4h);
		t = vecAddExp4H(vecMulExp4H(t, vec4h_one_half), vec4h_one_half);
		t = vecSaturateExp4H(t);

#if VERIFY_RESULTS
		opt_fminX = Vec4HToVec4(t)[0];
		opt_fminY = Vec4HToVec4(t)[1];
		opt_fmaxX = Vec4HToVec4(t)[2];
		opt_fmaxY = Vec4HToVec4(t)[3];

		opt_minZ = Vec4HToVec4(vtmin)[2] * ZMAX;
#else
		*fminX = Vec4HToVec4(t)[0];
		*fminY = Vec4HToVec4(t)[1];
		*fmaxX = Vec4HToVec4(t)[2];
		*fmaxY = Vec4HToVec4(t)[3];

		*minZ = Vec4HToVec4(vtmin)[2] * ZMAX;
#endif
	}
#endif

#if !VERIFY_RESULTS && !USE_SCALAR_CODE
	return ret;
#else
	{
		Vec3	tpos;
		Vec3	tmin, tmax;

		mulVec3ProjMat44(transBounds[0], projection_mat, tmin);
		copyVec3(tmin, tmax);

		mulVec3ProjMat44(transBounds[1], projection_mat, tpos);
		vec3RunningMinMax(tpos, tmin, tmax);

		mulVec3ProjMat44(transBounds[2], projection_mat, tpos);
		vec3RunningMinMax(tpos, tmin, tmax);

		mulVec3ProjMat44(transBounds[3], projection_mat, tpos);
		vec3RunningMinMax(tpos, tmin, tmax);

		mulVec3ProjMat44(transBounds[4], projection_mat, tpos);
		vec3RunningMinMax(tpos, tmin, tmax);

		mulVec3ProjMat44(transBounds[5], projection_mat, tpos);
		vec3RunningMinMax(tpos, tmin, tmax);

		mulVec3ProjMat44(transBounds[6], projection_mat, tpos);
		vec3RunningMinMax(tpos, tmin, tmax);

		mulVec3ProjMat44(transBounds[7], projection_mat, tpos);
		vec3RunningMinMax(tpos, tmin, tmax);

		if (outOfBounds(tmin, tmax))
		{
#if VERIFY_RESULTS
			//if (ret != 0)
			//	printf("Bad");
#endif
			ret = 0;
		} else {
#if VERIFY_RESULTS
			//if (ret != 1)
			//	printf("Bad");
#endif

			tmin[0] -= ZB_ONE_OVER_DIM;
			tmin[1] -= ZB_ONE_OVER_DIM;
			tmax[0] += ZB_ONE_OVER_DIM;
			tmax[1] += ZB_ONE_OVER_DIM;

			MAX1F(tmin[0], -1);
			MAX1F(tmin[1], -1);
			MIN1F(tmax[0], 1);
			MIN1F(tmax[1], 1);

			*fminX = tmin[0] * 0.5f + 0.5f;
			*fminY = tmin[1] * 0.5f + 0.5f;
			*fmaxX = tmax[0] * 0.5f + 0.5f;
			*fmaxY = tmax[1] * 0.5f + 0.5f;

			*minZ = tmin[2] * ZMAX;
#if VERIFY_RESULTS
			if (ret)
			{
				if (!nearSameF32Tol(*fminX, opt_fminX, occ_verify_tol))
					printf("Bad");
				if (!nearSameF32Tol(*fminY, opt_fminY, occ_verify_tol))
					printf("Bad");
				if (!nearSameF32Tol(*fmaxX, opt_fmaxX, occ_verify_tol))
					printf("Bad");
				if (!nearSameF32Tol(*fmaxY, opt_fmaxY, occ_verify_tol))
					printf("Bad");
				if (!nearSameF32Tol(*minZ, opt_minZ, occ_verify_tol))
					printf("minZ Bad (%0.6f)\n", opt_minZ - *minZ);
			}
// 			if (dbg_state.test1)
// 			{
// 				*fminX = opt_fminX;
// 				*fminY = opt_fminY;
// 				*fmaxX = opt_fmaxX;
// 				*fmaxY = opt_fmaxY;
// 				*minZ= opt_minZ;
// 			}
#endif

		}
	}
	return ret;
#endif
}

int gfxGetScreenSpace(const Frustum *frustum, const Mat44 projection_mat, int near_clipped, const Vec4 transBounds[8], F32 *screen_space)
{
	F32 fminX, fminY, fmaxX, fmaxY;
	ZType minZ;
	int ret;


	if (near_clipped)
		ret = getScreenExtents(frustum, projection_mat, transBounds, &fminX, &fminY, &fmaxX, &fmaxY, &minZ, true);
	else
		ret = getScreenExtentsNoZClip(projection_mat, transBounds, &fminX, &fminY, &fmaxX, &fmaxY, &minZ);

	if (!ret)
		return 0;

	if (screen_space)
		*screen_space = (fmaxX - fminX) * (fmaxY - fminY);

	return 1;
}

int gfxGetScreenExtents(const Frustum *frustum, const Mat44 projection_mat, const Mat4 local_to_world_mat, const Vec3 world_min, const Vec3 world_max, Vec2 screen_min, Vec2 screen_max, F32 *screen_z, bool bClipToScreen)
{
	Vec4_aligned transBounds[8];
	int ret;
	Mat4 local_to_camera_mat;
	Mat4 const * pCameraMatToUse;

	if (local_to_world_mat)
	{
		mulMat4Inline(frustum->viewmat, local_to_world_mat, local_to_camera_mat);
		pCameraMatToUse = &local_to_camera_mat;
	}
	else
	{
		pCameraMatToUse = &frustum->viewmat;
	}
	mulBounds(world_min, world_max, *pCameraMatToUse, transBounds);
	ret = getScreenExtents(frustum, projection_mat, transBounds, &screen_min[0], &screen_min[1], &screen_max[0], &screen_max[1], screen_z, bClipToScreen);

	// back project z depth to linear space
	if (ret)
		*screen_z = projection_mat[3][2] / (*screen_z + projection_mat[2][2]);

	return ret;
}

int gfxCapsuleGetScreenExtents(const Frustum* pFrustum, const Mat44 xProjection, const Mat4 xLocalToWorld, 
							   const Vec3 vWorldMin, const Vec3 vWorldMax, Vec2 vScreenMin, Vec2 vScreenMax, 
							   F32* pfScreenZ, bool bFitInsideBox, bool bClipToScreen)
{
	int i, iLargestExtent = 0;
	F32 fLargestExtent = -1.0f;
	F32 fZA, fZB, fScreenRadius, fRadius = 0.0f;
	Vec3 vE, vA, vB,vMin, vMax;
	Vec2 vAMin, vAMax, vBMin, vBMax;

	//find the largest extent
	subVec3(vWorldMax,vWorldMin,vE);
	for (i = 0; i < 3; i++)
	{
		F32 fExtent = ABS(vE[i]);
		if (fLargestExtent < 0 || fExtent > fLargestExtent)
		{
			fLargestExtent = fExtent;
			iLargestExtent = i;
		}
	}
#define WRAP_INDEX_VEC3(i) (i >= 3) ? i-3 : i
	for (i = 1; i < 3; i++)
	{
		int iIndex = WRAP_INDEX_VEC3(iLargestExtent+i);
		F32 fHalfExtent = vE[iIndex]/2.0f;
		vA[iIndex] = vWorldMin[iIndex] + fHalfExtent;
		vB[iIndex] = vWorldMin[iIndex] + fHalfExtent;
		fRadius += SQR(fHalfExtent);
	}
	fRadius = sqrt(fRadius);
	if (bFitInsideBox)
	{
		vA[iLargestExtent] = vWorldMin[iLargestExtent] + fRadius;
		vB[iLargestExtent] = vWorldMax[iLargestExtent] - fRadius;
	}
	else
	{
		vA[iLargestExtent] = vWorldMin[iLargestExtent] - fRadius;
		vB[iLargestExtent] = vWorldMax[iLargestExtent] + fRadius;
	}
#define CAPSULE_MAT_OP(op,vA,vB,m)\
	{\
	Vec3 vAT, vBT;\
	copyVec3(vA, vAT);\
	copyVec3(vB, vBT);\
	##op(vAT, m, vA);\
	##op(vBT, m, vB);\
	}\

	if (s_iDebugCapsuleScreenExtents) //draw a capsule for debugging
	{
		Color c;
		Vec3 vAC, vBC;
		setColorFromARGB(&c, 0xFF0000FF);
		copyVec3(vA, vAC);
		copyVec3(vB, vBC);
		CAPSULE_MAT_OP(mulVecMat4, vAC, vBC, xLocalToWorld);
		gfxDrawCapsule3D(vAC, vBC, fRadius, 20, c, 1);
	}

	if (xLocalToWorld)
	{
		Mat4 xLocalToCamera;
		mulMat4Inline(pFrustum->viewmat, xLocalToWorld, xLocalToCamera);
		CAPSULE_MAT_OP(mulVecMat4, vA, vB, xLocalToCamera);
	}
	else
	{
		CAPSULE_MAT_OP(mulVecMat4, vA, vB, pFrustum->viewmat);
	}

	CAPSULE_MAT_OP(mulVec3ProjMat44, vA, vB, xProjection);

	// back project z depth to linear space
	fZA = xProjection[3][2] / ((vA[2] * ZMAX) + xProjection[2][2]);
	fScreenRadius = fRadius / (fZA * pFrustum->htan);
	vAMin[0] = vA[0] - fScreenRadius;
	vAMax[0] = vA[0] + fScreenRadius;
	fScreenRadius = fRadius / (fZA * pFrustum->vtan);
	vAMin[1] = vA[1] - fScreenRadius;
	vAMax[1] = vA[1] + fScreenRadius;

	// back project z depth to linear space
	fZB = xProjection[3][2] / ((vB[2] * ZMAX) + xProjection[2][2]);
	fScreenRadius = fRadius / (fZB * pFrustum->htan);
	vBMin[0] = vB[0] - fScreenRadius;
	vBMax[0] = vB[0] + fScreenRadius;
	fScreenRadius = fRadius / (fZB * pFrustum->vtan);
	vBMin[1] = vB[1] - fScreenRadius;
	vBMax[1] = vB[1] + fScreenRadius;
	
	vMin[0] = MINF(vAMin[0], vBMin[0]);
	vMin[1] = MINF(vAMin[1], vBMin[1]);
	vMin[2] = MINF(vA[2], vB[2]);
	vMax[0] = MAXF(vAMax[0], vBMax[0]);
	vMax[1] = MAXF(vAMax[1], vBMax[1]);
	vMax[2] = MAXF(vA[2], vB[2]);

	if (outOfBounds(vMin, vMax))
	{
		return 0;
	}

	//use the min z distance as the screen z
	(*pfScreenZ) = MINF(fZA, fZB);

	if (bClipToScreen)
	{
		MAX1F(vMin[0], -1);
		MAX1F(vMin[1], -1);
		MIN1F(vMax[0], 1);
		MIN1F(vMax[1], 1);
	}

	vScreenMin[0] = vMin[0] * 0.5f + 0.5f;
	vScreenMin[1] = vMin[1] * 0.5f + 0.5f;
	vScreenMax[0] = vMax[0] * 0.5f + 0.5f;
	vScreenMax[1] = vMax[1] * 0.5f + 0.5f;
	return 1;
}

static void getSphereScreenExtents(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo, const Vec3 centerEye, float radius, float *fminX, float *fminY, 
								   float *fmaxX, float *fmaxY, ZType *minZ)
{
	Vec3 center;
	Vec3 tpos, tmin, tmax;
	float radius2 = radius + radius;

	setVec3(tmin, 1, 1, 1);
	setVec3(tmax, -1, -1, -1);

	copyVec3(centerEye, center);

	center[2] += radius;
	mulVec3ProjMat44(center, zo->projection_mat, tpos);
	vec3RunningMinMax(tpos, tmin, tmax);

	center[2] -= radius2;
	mulVec3ProjMat44(center, zo->projection_mat, tpos);
	vec3RunningMinMax(tpos, tmin, tmax);

	center[0] -= radius;
	center[1] -= radius;
	center[2] += radius;
	mulVec3ProjMat44(center, zo->projection_mat, tpos);
	vec3RunningMinMax(tpos, tmin, tmax);

	center[0] += radius2;
	mulVec3ProjMat44(center, zo->projection_mat, tpos);
	vec3RunningMinMax(tpos, tmin, tmax);

	center[1] += radius2;
	mulVec3ProjMat44(center, zo->projection_mat, tpos);
	vec3RunningMinMax(tpos, tmin, tmax);

	center[0] -= radius2;
	mulVec3ProjMat44(center, zo->projection_mat, tpos);
	vec3RunningMinMax(tpos, tmin, tmax);

#if ZO_SAFE
	assert(tmin[0] <= 1 && tmax[0] >= -1);
	assert(tmin[1] <= 1 && tmax[1] >= -1);
#endif

	MAX1F(tmin[0], -1);
	MAX1F(tmin[1], -1);
	MIN1F(tmax[0], 1);
	MIN1F(tmax[1], 1);

	*fminX = tmin[0] * 0.5f + 0.5f;
	*fminY = tmin[1] * 0.5f + 0.5f;
	*fmaxX = tmax[0] * 0.5f + 0.5f;
	*fmaxY = tmax[1] * 0.5f + 0.5f;
	*minZ = tmin[2] * ZMAX;
}

__forceinline static void filterDown(ZType *minStart, ZType *maxStart, int dim, ZType *minStart2, ZType *maxStart2, int dim2)
{
	int evenLine, x, y;
	ZType *line, *line2;

	// min
	evenLine = 1;
	for (y = 0; y < dim; y++)
	{
		line2 = minStart2 + ((int)(y/2)) * dim2;
		line = minStart + y * dim;

		if (evenLine)
		{
			for (x = 0; x < dim; x+=2)
			{
				*line2 = MINF(line[0], line[1]);

				line+=2;
				line2++;
			}
		}
		else
		{
			for (x = 0; x < dim; x+=2)
			{
				MIN1F(*line2, line[0]);
				MIN1F(*line2, line[1]);

				line+=2;
				line2++;
			}
		}

		evenLine = !evenLine;
	}

    // max
	evenLine = 1;
	for (y = 0; y < dim; y++)
	{
		line2 = maxStart2 + ((int)(y/2)) * dim2;
		line = maxStart + y * dim;

		if (evenLine)
		{
			for (x = 0; x < dim; x+=2)
			{
				*line2 = MAXF(line[0], line[1]);
				line+=2;
				line2++;
			}
		}
		else
		{
			for (x = 0; x < dim; x+=2)
			{
				MAX1F(*line2, line[0]);
				MAX1F(*line2, line[1]);
				line+=2;
				line2++;
			}
		}

		evenLine = !evenLine;
	}
}

#define elem(x,y,width) ((x)+((y)*width))
static float *zoTestBufferArraysMinZ[8] = {0};
static float *zoTestBufferArraysMaxZ[8] = {0};
static int zoTestBufferTileCount = 4;
static void createZHierarchy(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo)
{
	int i;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

#if GFX_OCCLUSION_SPU_TEST || !_PS3

	// do the high res buffer (one less than the zbuffer)
	i = zo->zhierarchy.array_count-1;
	filterDown(zo->zbuffer.data, zo->zbuffer.data, ZB_DIM, zo->zhierarchy.arrays[i].minZ, zo->zhierarchy.arrays[i].maxZ, 
			   zo->zhierarchy.arrays[i].dim);

	// fill in the other buffers
	for (i = zo->zhierarchy.array_count-2; i >= 0; i--)
		filterDown(zo->zhierarchy.arrays[i+1].minZ, zo->zhierarchy.arrays[i+1].maxZ, zo->zhierarchy.arrays[i+1].dim,
				   zo->zhierarchy.arrays[i].minZ, zo->zhierarchy.arrays[i].maxZ, zo->zhierarchy.arrays[i].dim);
#endif

#if _PS3
    if ( zoOcclusionSpuJobsCreate(zo) )
    {
        if ( zoOcclusionSpuJobsStart() )
        {
            zoOcclusionSpuJobsWaitForCompletion();
        }
    }

    if ( GFX_OCCLUSION_SPU_TEST )
    {
        extern GfxOcclusionBuffer* zoSpuTestBuffer;
        if ( zoSpuTestBuffer )
        {
            int numMips = zo->zhierarchy.array_count;
            int mip = numMips-1;
            for ( mip = (numMips-1); mip >= 0; mip--)
            {
                int cell;
                int size = SQR(zo->zhierarchy.arrays[mip].dim);
                for ( cell = 0; cell < size; cell++ )
                {
                    assert(zo->zhierarchy.arrays[mip].minZ[cell] == zoSpuTestBuffer->zhierarchy.arrays[mip].minZ[cell]);
                    assert(zo->zhierarchy.arrays[mip].maxZ[cell] == zoSpuTestBuffer->zhierarchy.arrays[mip].maxZ[cell]);
                }
            }
        }
    }
#endif

	zo->zhierarchy.minz = *zo->zhierarchy.arrays[0].minZ;
	zo->zhierarchy.maxz = *zo->zhierarchy.arrays[0].maxZ;

	PERFINFO_AUTO_STOP();
}
#undef elem

#define SUPPORT_SHOW_ZRECT 0
int debugShowZRect = 0;

__forceinline static int testZRect(ZType z, int minX, int minY, int maxX, int maxY, ZType *buffer, int width)
{
	int y, n, linesize;
	ZType *line, *ptr;
#if PLATFORM_CONSOLE
	F32 zOut = 0.0f;
#endif

	linesize = maxX - minX;
	line = buffer + minY * width + minX;

	for (y = minY; y < maxY; y++)
	{
		ptr = line;
		n = linesize;
		while (n > 3)
		{
#if PLATFORM_CONSOLE
			zOut = ZTEST_XBOX(z, ptr[0]);
			zOut += ZTEST_XBOX(z, ptr[1]);
			zOut += ZTEST_XBOX(z, ptr[2]);
			zOut += ZTEST_XBOX(z, ptr[3]);
			if (zOut)
				return 1;
#else
			if (ZTEST(z, ptr[0]))
				return 1;
			if (ZTEST(z, ptr[1]))
				return 1;
			if (ZTEST(z, ptr[2]))
				return 1;
			if (ZTEST(z, ptr[3]))
				return 1;
#endif
			ptr+=4;
			n-=4;
		}
		while (n)
		{
#if PLATFORM_CONSOLE
			zOut += ZTEST_XBOX(z, *ptr);
#else
			if (ZTEST(z, *ptr))
				return 1;
#endif
			ptr++;
			n--;
		}

		line += width;

#if PLATFORM_CONSOLE
		if (zOut)
			return 1;
#endif
	}

	return 0;
}

// bMax means we are testing against a max buffer
__inline static int testZGradientRect(Vec3 const vPos, F32 dzdx, F32 dzdy, int minX, int minY, int maxX, int maxY, F32 fMinZ, ZType *buffer, int width, bool bMax)
{
	int y, n, linesize;
	ZType *line, *ptr;
	F32 z;
	F32 fBaseZforX0Y0;
	F32 fBasePixelLogicalMaxZ;
	F32 fAbsdzdx,fAbsdzdy;
	
	dzdx /= width;
	dzdy /= width;

	fAbsdzdx = fabsf(dzdx);
	fAbsdzdy = fabsf(dzdy);

	linesize = maxX - minX;
	line = buffer + minY * width + minX;

	fBaseZforX0Y0 = vPos[2] - vPos[1]*width*dzdy - vPos[0]*width*dzdx;
	fBasePixelLogicalMaxZ = fBaseZforX0Y0;

	if (dzdy > 0)
		fBasePixelLogicalMaxZ += dzdy;
	if (dzdx > 0)
		fBasePixelLogicalMaxZ += dzdx;

	// get the minimum Z for this pixel
	if (bMax && dzdy < 0)
		fBaseZforX0Y0 += dzdy;
	else if (!bMax && dzdy > 0)
		fBaseZforX0Y0 -= dzdy;

	if (bMax && dzdx < 0)
		fBaseZforX0Y0 += dzdx;
	else if (!bMax && dzdx > 0)
		fBaseZforX0Y0 -= dzdx;

	for (y = minY; y < maxY; y++)
	{
		z = fBaseZforX0Y0 + y*dzdy + (F32)minX*dzdx;

		ptr = line;
		n = linesize;

		// these branches are totally not worth it.  Much faster just to do the if check in the tight loop
#if 0
		if (fAbsdzdx > 1e-7f)
		{
			F32 fPixelLogicalMaxZ;
			F32 fXPosOfMinZ;

			fPixelLogicalMaxZ = fBasePixelLogicalMaxZ + y*dzdy;

			// restrict the walk for this line based on meaningful Z values
			// (This is a little dumb.  I'm ALMOST rasterizing here.  I should just find the real edges of the quad and get even better results)
			fXPosOfMinZ = (fMinZ-fPixelLogicalMaxZ)/dzdx;

			if (dzdx > 0)
			{
				F32 fLineMinX = floatmax(floor(fXPosOfMinZ),minX);
				int iDelta = (int)floatmin(fLineMinX-minX,(F32)linesize);
				ptr += iDelta;
				n -= iDelta;
				z += dzdx*iDelta;
			}
			else
			{
				F32 fLineMaxX = (F32)floatmin(Ceilf(fXPosOfMinZ),maxX);
				n -= (int)floatmin(maxX-fLineMaxX,linesize);
			}
		}
		else if (z+fAbsdzdy < fMinZ)
		{
			// This whole scanline is bogus
			n=0;
		}
#endif

		while (n)
		{
			if (z+fAbsdzdy+fAbsdzdx >= fMinZ && ZTEST(z, *ptr))
				return 1;

			z += dzdx;

			ptr++;
			n--;
		}

		line += width;
	}

	return 0;
}

static int iEndHoffset = ZO_DEFAULT_END_HOFFSET;
AUTO_CMD_INT(iEndHoffset, zoEndOffset) ACMD_CMDLINE;

static int testZRectHierarchical(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo, float x1, float y1, float x2, float y2, ZType z)
{
	int i, width, minX, minY, maxX, maxY;
	int testZRectResult = 0;
	
	if (!zo->zhierarchy.arrays)
		return 1;
	
	if (ZTEST(z, zo->zhierarchy.minz))
		return 1;
	if (!ZTEST(z, zo->zhierarchy.maxz))
		return 0;

	for (i = 0; i < ZO_START_HOFFSET; i++)
	{
		x1 *= 2;
		y1 *= 2;
		x2 *= 2;
		y2 *= 2;
	}
	for (i = ZO_START_HOFFSET; i < zo->zhierarchy.array_count - iEndHoffset - 1; i++)
	{
		minX = floor(x1);
		minY = floor(y1);
		maxX = Ceilf(x2);
		maxY = Ceilf(y2);
		width = zo->zhierarchy.arrays[i].dim;

		// if it is in front of the minZ, it is visible
		if (testZRect(z, minX, minY, maxX, maxY, zo->zhierarchy.arrays[i].minZ, width))
			return 1;

		// if it is behind the maxZ, it is not visible
		if (!testZRect(z, minX, minY, maxX, maxY, zo->zhierarchy.arrays[i].maxZ, width))
			return 0;

		// between min and max, go to next level
		x1 *= 2;
		y1 *= 2;
		x2 *= 2;
		y2 *= 2;
	}

	// last level of the hierarchy, we can just test against max
	minX = floor(x1);
	minY = floor(y1);
	maxX = Ceilf(x2);
	maxY = Ceilf(y2);
	width = zo->zhierarchy.arrays[i].dim;

	testZRectResult = testZRect(z, minX, minY, maxX, maxY, zo->zhierarchy.arrays[i].maxZ, width);
	return testZRectResult;
}

static int testZRectNonHierarchal(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo, int x1, int y1, int x2, int y2, ZType z)
{
	return testZRect(z, x1, y1, x2, y2, zo->zbuffer.data, ZB_DIM);
}

static int testZGradientRectHierarchical(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo, float x1, float y1, float x2, float y2, F32 fMinZ, Vec3 const vPos, F32 dxdz, F32 dydz)
{
	int i, width, minX, minY, maxX, maxY;
	int testZRectResult = 0;
	
	if (!zo->zhierarchy.arrays)
		return 1;
	
	// See if we're definitely visible (this does NOT mean we're definitely visible. The place where minZ was may be well off-screen)
	//if (ZTEST(fMinZ, zo->zhierarchy.minz))
		//return 1;
	// See if we're definitely NOT visible
	if (!ZTEST(fMinZ, zo->zhierarchy.maxz))
		return 0;

	for (i = 0; i < ZO_START_HOFFSET; i++)
	{
		x1 *= 2;
		y1 *= 2;
		x2 *= 2;
		y2 *= 2;
	}
	for (i = ZO_START_HOFFSET; i < zo->zhierarchy.array_count - iEndHoffset - 1; i++)
	{
		minX = floor(x1);
		minY = floor(y1);
		maxX = Ceilf(x2);
		maxY = Ceilf(y2);
		width = zo->zhierarchy.arrays[i].dim;

		// if it is in front of the minZ, it is visible
		if (testZGradientRect(vPos, dxdz, dydz, minX, minY, maxX, maxY, fMinZ, zo->zhierarchy.arrays[i].minZ, width, false))
			return 1;

		// if it is behind the maxZ, it is not visible
		if (!testZGradientRect(vPos, dxdz, dydz, minX, minY, maxX, maxY, fMinZ, zo->zhierarchy.arrays[i].maxZ, width, true))
			return 0;

		// between min and max, go to next level
		x1 *= 2;
		y1 *= 2;
		x2 *= 2;
		y2 *= 2;
	}

	// last level of the hierarchy, we can just test against max
	minX = floor(x1);
	minY = floor(y1);
	maxX = Ceilf(x2);
	maxY = Ceilf(y2);
	width = zo->zhierarchy.arrays[i].dim;

	testZRectResult = testZGradientRect(vPos, dxdz, dydz, minX, minY, maxX, maxY, fMinZ, zo->zhierarchy.arrays[i].maxZ, width, true);
	return testZRectResult;
}

typedef struct ZPoint
{
	int x,y;
	ZType z;
} ZPoint;

// fancier occlusion test
//__forceinline static int testZQuad(ZPoint pts[4], ZType *buffer, int bufferwidth)

// more expensive than test, finds out what the limits are
__forceinline static void limitsZRect(ZType z, int minX, int minY, int maxX, int maxY, ZType *buffer, int bufferwidth,
								int * piMinPassX, int * piMinPassY, int * piMaxPassX, int * piMaxPassY)
{
	int x,y, linesize;
	ZType *line, *ptr;

	*piMinPassX = bufferwidth;
	*piMinPassY = bufferwidth;
	*piMaxPassX = 0;
	*piMaxPassY = 0;

	linesize = maxX - minX;
	line = buffer + minY * bufferwidth + minX;

	for (y = minY; y < maxY; y++)
	{
		ptr = line;
		x = minX;

		while (x < maxX)
		{
			if (ZTEST(z, *ptr))
			{
				// TODO - this is slow
				*piMinPassX = MIN(*piMinPassX,x);
				*piMinPassY = MIN(*piMinPassY,y);
				*piMaxPassX = MAX(*piMaxPassX,x);
				*piMaxPassY = MAX(*piMaxPassY,y);
			}
			ptr++;
			x++;
		}

		line += bufferwidth;
	}
}

void zoLimitsScreenRect(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo,F32 fViewSpaceZ,Vec2 vMin,Vec2 vMax)
{
	const int iLevel = MIN(6,zo->zhierarchy.array_count);
	int iMinPassX,iMinPassY,iMaxPassX,iMaxPassY;
	int bufferwidth = zo->zhierarchy.arrays[iLevel].dim;
	int bufferheight = zo->zhierarchy.arrays[iLevel].dim;
	limitsZRect(ZMAX*(fViewSpaceZ+zo->frustum.znear)/fViewSpaceZ,0,0,bufferwidth,bufferheight,zo->zhierarchy.arrays[iLevel].maxZ,bufferwidth,
					&iMinPassX,&iMinPassY,&iMaxPassX,&iMaxPassY);

	vMin[0] = (2.0f*iMinPassX/(F32)bufferwidth)-1.0f;
	vMin[1] = (2.0f*iMinPassY/(F32)bufferheight)-1.0f;

	vMax[0] = (2.0f*(iMaxPassX+1)/(F32)bufferwidth)-1.0f;
	vMax[1] = (2.0f*(iMaxPassY+1)/(F32)bufferheight)-1.0f;
}
////////////////////////////////////////////////////
////////////////////////////////////////////////////

static void zoBlitZBufferToTex(U8 * dstTex, const F32 * srcData, int srcWidth, int srcHeight, int mode, F32 minZ, F32 z_z, F32 z_w)
{
	int x, y;
	U8 * p;
	ZType zval;
	Vec3 hsv, rgb;
	setVec3(hsv, 0.0f, 1.0f, 0.5f);

	for (y = 0; y < srcHeight; y++)
	{
		p = dstTex + ((srcHeight - 1 - y) * ZB_DIM * 4);
		for (x = 0; x < srcWidth; x++)
		{
			zval = srcData[x + y * srcWidth];
			hsv[0] = fmodf(z_w / (zval + z_z) - minZ, 360.0f);
			if (mode)
				hsv[1] = zval;
			hsvToRgb(hsv, rgb);
			p[0] = round(rgb[0]*255.0f);
			p[1] = round(rgb[1]*255.0f);
			p[2] = round(rgb[2]*255.0f);
			p[3] = ZTOALPHA(zval);
			p+=4;
		}
	}
}

BasicTexture *zoGetZBufferHSV(GfxOcclusionBuffer *zo, int mode, int zhier_level)
{
	U8 *buf;
	float z_z = zo->projection_mat[2][2];
	float z_w = zo->projection_mat[2][3];
	float minZ, maxZ;

	// use fixed depth offset from near plane for easier absolute
	// interpretation of values on screen, reduce 
	// abrupt shifts as depth range changes with viewpoint
	minZ = -zo->frustum.znear;
	maxZ = minZ + 64.0f;
	maxZ = 360.0f / (maxZ - minZ);
	// simplify the Z range conversion
	z_w *= maxZ;
	minZ *= maxZ;

	if (!zo || !zo->zbuffer.data)
		return 0;

	if (!zo->debug.zbuf_tex)
	{
		zo->debug.zbuf_tex = texGenNew(ZB_DIM, ZB_DIM, "zOcclusion ZBuffer", TEXGEN_VOLATILE_SHARED, WL_FOR_UTIL);
		zo->debug.zbuf_tex->bt_texopt_flags |= TEXOPT_CLAMPS | TEXOPT_CLAMPT | TEXOPT_MAGFILTER_POINT;
	}

	zoWaitUntilDone(zo);
	buf = memrefAlloc(ZB_DIM * ZB_DIM * 4);
	if (zhier_level == 0)
		zoBlitZBufferToTex(buf, zo->zbuffer.data, ZB_DIM, ZB_DIM, mode, minZ, z_z, z_w);
	else
	{
		const HierarchicalZBuffer * zhier_data = &zo->zhierarchy.arrays[zhier_level - 1];
		const F32 * srcZ = gfx_state.debug.zocclusion_hier_max ? zhier_data->maxZ : zhier_data->minZ;
		int zres = zhier_data->dim;
		zoBlitZBufferToTex(buf, srcZ, zres, zres, mode, minZ, z_z, z_w);
	}

	texGenUpdate(zo->debug.zbuf_tex, buf, RTEX_2D, RTEX_BGRA_U8, 1, true, false, true, true);
	memrefDecrement(buf);

	return zo->debug.zbuf_tex;
}

void zoShowDebug(GfxOcclusionBuffer *zo, int debug_type)
{
	int zhier_level, zres;
	int window_width, window_height;
	int i;
	if (!zo || !zo->zbuffer.data)
		return;

	gfxXYprintf(0,1, "Occluders: %d", zo->occluders.last_occluder_count);
	gfxXYprintf(0,2, "Occluder Tris: %d", zo->debug.occluder_tris_drawn);
	gfxXYprintf(0,3, "Occlusion Tests: %d/%d/%d", 
		zo->debug.last_num_test_calls, zo->debug.last_num_tests, zo->debug.last_num_culls);
	gfxXYprintf(0,4, "Test requests/Actual tests if ZO is ready/Culled objects");

	if (zo->debug.iNumRejectedOccluders)
	{
		gfxXYprintf(0,6, "REJECTED OCCLUDERS (%d max verts allowed)",MAX_OCCLUDER_VERTS);
		gfxXYprintf(0,7, "[Occlusion potential] .. [Tri count] .. [Vert count] .. [Name] ..........");
		for (i=0;i<zo->debug.iNumRejectedOccluders;i++)
		{
			int iTris;
			int iVerts;
			char const * pchName;
			if (zo->debug.aRejectedOccluders[i].pchName)
			{
				iTris = zo->debug.aRejectedOccluders[i].iTris;
				iVerts = zo->debug.aRejectedOccluders[i].iVerts;
				pchName = zo->debug.aRejectedOccluders[i].pchName;
			}
			else
			{
				iTris = 12;
				iVerts = 8;
				pchName = "[Volume]";
			}
			gfxXYprintf(0,8+i, "        %f ......   %6d ....   %6d ....   %s",zo->debug.aRejectedOccluders[i].fZWeightedScreenSpace, iTris, iVerts, pchName);
		}
	}

	zo->debug.iNumRejectedOccluders = 0;

	zhier_level = debug_type - 1;
	if (zhier_level == 0)
		zres = ZB_DIM;
	else
		zres = zo->zhierarchy.arrays[zhier_level - 1].dim;

	gfxGetActiveSurfaceSize(&window_width, &window_height);

	{
		static U32 color = 0xffffffff;
		display_sprite_tex(zoGetZBufferHSV(zo, 0, zhier_level), 0, 0, 1, (float)window_width / zres, (float)window_height / zres, color);
	}
}

static GfxOcclusionBuffer *zo_buffers[10];

#if !_PS3
Vec4H maskV[ 4 ] = { 0 };
#endif

GfxOcclusionBuffer *zoCreate(Frustum *frustum, bool is_primary)
{
	int i, j;

#if _PS3
#elif _XBOX
	maskV[ 0 ] = XMVectorSelectControl(1,0,0,0);
	maskV[ 1 ] = XMVectorSelectControl(1,1,0,0);
	maskV[ 2 ] = XMVectorSelectControl(1,1,1,0);
	maskV[ 3 ] = XMVectorSelectControl(1,1,1,1);
#else
	if (!isSSEavailable())
		return NULL;

	setIVec4H(maskV[ 0 ], -1,  0,  0,  0);
	setIVec4H(maskV[ 1 ], -1, -1,  0,  0);
	setIVec4H(maskV[ 2 ], -1, -1, -1,  0);
	setIVec4H(maskV[ 3 ], -1, -1, -1, -1);
#endif

	for (i = 0; i < ARRAY_SIZE(zo_buffers); ++i)
	{
		if (!zo_buffers[i])
		{
			GfxOcclusionBuffer *zo = zo_buffers[i] = aligned_calloc(sizeof(*zo_buffers[i]), 1, 16);
			char buffer[1024];

			zo->occluders.occluders = zo->occluders.occluders1;
			zo->occluders.draw_occluders = zo->occluders.occluders2;

#if ZO_USE_JITTER
			zo->use_jitter = is_primary;
#endif

			zo->zbuffer.size = SQR(ZB_DIM);
			{
				// Want a free page after it we can set permissions on
				size_t data_size = zo->zbuffer.size * sizeof(*zo->zbuffer.data);

				guarded_aligned_malloc(&zo->zbuffer.data_alloc, data_size, 1, 1);
				zo->zbuffer.data = (ZType*)zo->zbuffer.data_alloc.aligned_protected_memory;
			}

			zo->zhierarchy.array_count = log2(ZB_DIM);
			zo->zhierarchy.arrays = malloc(zo->zhierarchy.array_count * sizeof(*zo->zhierarchy.arrays));

			for (j = 0; j < zo->zhierarchy.array_count; j++)
			{
				int size;
				zo->zhierarchy.arrays[j].dim = 1 << j;
                size = SQR(zo->zhierarchy.arrays[j].dim) * sizeof(*zo->zhierarchy.arrays[j].minZ);
				size = ALIGNUP(size, 16);
                zo->zhierarchy.arrays[j].minZ = aligned_malloc(size, 16);
                zo->zhierarchy.arrays[j].maxZ = aligned_malloc(size, 16);
			}

			zo->frustum = *frustum;

			sprintf(buffer, "ZOcclusion Buffer %d", i);
			zo->event_timer = etlCreateEventOwner(buffer, "ZOcclusion Buffer", "GraphicsLib");

			return zo;
		}
	}

	return NULL;
}

void zoDestroy(GfxOcclusionBuffer *zo)
{
	int i;

	if (!zo)
		return;

	zoWaitUntilDone(zo);

	if (zo->zthread.occlusion_thread)
	{
#if _PS3
        DestroyEvent(zo->zthread.wait_for_zo_done);
#else
		CloseHandle(zo->zthread.wait_for_zo_done);
#endif
		zo->zthread.wait_for_zo_done = NULL;
		tmDestroyThread(zo->zthread.occlusion_thread, false);
		zo->zthread.occlusion_thread = NULL;
#if !_PS3
		tmDestroyThread(zo->zthread.occlusion_thread2, false);
		zo->zthread.occlusion_thread2 = NULL;
#endif
	}
	
	for (i = 0; i < ARRAY_SIZE(zo_buffers); ++i)
	{
		if (zo_buffers[i] == zo)
			zo_buffers[i] = NULL;
	}

	guarded_aligned_free(&zo->zbuffer.data_alloc);

	for (i = 0; i < zo->zhierarchy.array_count; ++i)
	{
		SAFE_FREE(zo->zhierarchy.arrays[i].minZ);
		SAFE_FREE(zo->zhierarchy.arrays[i].maxZ);
	}

	SAFE_FREE(zo->zhierarchy.arrays);

	for (i = 0; i < zo->occluders.occluder_count; ++i)
		freeOccluder(zo->occluders.occluders[i]);

	for (i = 0; i < zo->occluders.draw_occluder_count; ++i)
		freeOccluder(zo->occluders.draw_occluders[i]);

	etlFreeEventOwner(zo->event_timer);

	SAFE_FREE(zo);
}

void zoNotifyModelFreed(ModelLOD *m)
{
	int i,j,k;

	for (k = 0; k < ARRAY_SIZE(zo_buffers); ++k)
	{
		GfxOcclusionBuffer *zo = zo_buffers[k];
		if (zo)
		{
			zoWaitUntilOccludersDrawn(zo);
			zoWaitUntilDone(zo);
			for (i = zo->occluders.occluder_count - 1; i >= 0; --i)
			{
				if (m == zo->occluders.occluders[i]->m)
				{
					freeOccluder(zo->occluders.occluders[i]);
					zo->occluders.occluder_count--;
					for (j = i; j < zo->occluders.occluder_count; j++)
						zo->occluders.occluders[j] = zo->occluders.occluders[j+1];
				}
			}
			for (i = zo->occluders.draw_occluder_count - 1; i >= 0; --i)
			{
				if (m == zo->occluders.draw_occluders[i]->m)
				{
					freeOccluder(zo->occluders.draw_occluders[i]);
					zo->occluders.draw_occluder_count--;
					for (j = i; j < zo->occluders.draw_occluder_count; j++)
						zo->occluders.draw_occluders[j] = zo->occluders.draw_occluders[j+1];
				}
			}
		}
	}
}

void zoClearOccluders(void)
{
	int i,k;

	for (k = 0; k < ARRAY_SIZE(zo_buffers); ++k)
	{
		GfxOcclusionBuffer *zo = zo_buffers[k];
		if (zo)
		{
			zoWaitUntilOccludersDrawn(zo);
			zoWaitUntilDone(zo);

			zo->debug.occluder_tris_drawn = 0;

			for (i = 0; i < zo->occluders.occluder_count; i++)
				freeOccluder(zo->occluders.occluders[i]);

			for (i = 0; i < zo->occluders.draw_occluder_count; i++)
				freeOccluder(zo->occluders.draw_occluders[i]);

			zo->occluders.draw_occluder_count = zo->occluders.occluder_count = 0;
			zo->occluders.min_occluder_screenspace = 0;

#if ZO_USE_JITTER
			if (zo->use_jitter)
			{
				zo->jitter.frame_num += 3;
				zo->jitter.draw_frame_num += 3;
			}
#endif
		}
	}
}

int zoGetThreadCount(SA_PARAM_OP_VALID GfxOcclusionBuffer *zo)
{
	if(s_iZOcclusionThread)
	{
		if(zo && zo->zthread.two_zo_draw_threads)
			return 2;
		return 1;
	}
	return 0;
}


static DWORD WINAPI zoThread( LPVOID lpParam )
{
	EXCEPTION_HANDLER_BEGIN
		for(;;)
			SleepEx(INFINITE, TRUE);
	return 0; 
	EXCEPTION_HANDLER_END
}

// DJR I prevented inlining because it breaks the __restrict declaration and
// causes many reloads and stores.
//__forceinline
extern void zoFillZbuffer(Vec4H * __restrict data, int size)
{
	static const Vec4H zinit = { ZINIT, ZINIT, ZINIT, ZINIT };
	int i;
#if PLATFORM_CONSOLE
	Vec4H * __restrict data1 = data + 1;
	Vec4H * __restrict data2 = data + 2;
	Vec4H * __restrict data3 = data + 3;
	Vec4H * __restrict data4 = data + 4;
	Vec4H * __restrict data5 = data + 5;
	Vec4H * __restrict data6 = data + 6;
	Vec4H * __restrict data7 = data + 7;
#endif

	Vec4H zv = zinit;
	PERFINFO_AUTO_PIX_START(__FUNCTION__);

	assert( !( (ptrdiff_t)data & 127 ) );

	size /= 4;
#if PLATFORM_CONSOLE
	for (i = 0; i < size; i += 8)
	{
#if !_PS3
		__dcbz128(0, data);
#endif
		data[0] = zv;
		data1[0] = zv;
		data2[0] = zv;
		data3[0] = zv;
		data4[0] = zv;
		data5[0] = zv;
		data6[0] = zv;
		data7[0] = zv;

		data += 8;
		data1 += 8;
		data2 += 8;
		data3 += 8;
		data4 += 8;
		data5 += 8;
		data6 += 8;
		data7 += 8;
	}
#else
	for (i = 0; i < size; i += 8)
	{
		data[i+0] = zv;
		data[i+1] = zv;
		data[i+2] = zv;
		data[i+3] = zv;
		data[i+4] = zv;
		data[i+5] = zv;
		data[i+6] = zv;
		data[i+7] = zv;
	}
#endif
	PERFINFO_AUTO_PIX_STOP();
}

static int zoDrawOccluders(GfxOcclusionBuffer *zo, int offset)
{
	int i, zocount, zocclusion_norestrict = gfx_state.debug.zocclusion_norestrict;
	Mat4 matModelToEye;
	GfxZBuffData zoData;
	F32 * z_data = zo->zbuffer.data;
	int width = ZB_DIM;
	OccluderPtr * draw_occluders = zo->occluders.draw_occluders;
	int last_occluder_count = 0;

	Frustum * pFrustum;

	if (gfx_state.debug.frustum_debug.frustum_debug_mode == 2)
	{
		pFrustum = &gfx_state.debug.frustum_debug.static_frustum;
	}
	else
	{
		pFrustum = &zo->frustum;
	}

	copyMat44(zo->projection_mat, zoData.projection_mat);
	zoData.ydim = ZB_DIM * ZB_SCAN_RES - 1;
	zoData.yoffset = 0.0f;
	if (offset)
	{
		if ( offset == 2 )
			z_data += ZB_DIM;
		width = ZB_DIM * 2;
		zoData.ydim *= 0.5f;
		zoData.yoffset = offset == 1 ? 64 : -192;
	}

#if ZO_USE_JITTER
	zoData.use_jitter = zo->use_jitter;
	zoData.amount = zo->jitter.amount;
#else
	zoData.use_jitter = 0;
	zoData.amount = 0.f;
#endif

	zoData.projection_far = gfx_state.far_plane_dist;
	zoData.one_over_projection_far = 1.f / zoData.projection_far;
	zoData.data = z_data;
	zoData.width = width;
	zoData.occluder_tris_drawn = 0;
	zoData.zo = zo;
	
#if ZO_SPU_DRAW
    zoSpuOcclusionDrawBegin(&zoData);
#endif

	for (i = 0, zocount = zo->occluders.draw_occluder_count; i < zocount; i++)
	{
		Occluder * o = draw_occluders[i];
		ModelLOD *m;
		int tri_count, vert_count;

		if (!o)
			continue;

		m = o->m;
		tri_count = (m&&m->data)?m->data->tri_count:12;
		vert_count = (m&&m->data)?m->data->vert_count:8;

		if (((zoData.occluder_tris_drawn + tri_count) < MAX_OCCLUDER_TRIS && vert_count < MAX_OCCLUDER_VERTS) || zocclusion_norestrict)
		{
			mulMat4Inline(pFrustum->viewmat, o->modelToWorld, matModelToEye);
			if (m)
				zoDrawModel(&zoData, m, matModelToEye, o->ok_materials, !o->double_sided);
			else
				zoDrawVolume(&zoData, o->minBounds, o->maxBounds, o->faceBits, matModelToEye);

			last_occluder_count++;
		}
	}

	if (offset < 2)
	{
		// only the primary zo render thread does this
		zo->occluders.last_occluder_count = last_occluder_count;
	}

#if ZO_SPU_DRAW
    zoSpuOcclusionDrawEnd();
#endif

	return zoData.occluder_tris_drawn;
}


static VOID CALLBACK zoDrawOccludersAPC(ULONG_PTR dwParam)
{
	GfxOcclusionBuffer * zo = (GfxOcclusionBuffer*)dwParam;

	etlAddEvent(zo->event_timer, "Draw occluders 2", ELT_CODE, ELTT_BEGIN);

	zoDrawOccluders(zo, 2);
#if !_PS3
	SetEvent(zo->zthread.wait_for_zo_done2);
#endif
	etlAddEvent(zo->event_timer, "Draw occluders 2", ELT_CODE, ELTT_END);
}

// This mask leaves overflow and invalid exceptions
#define FP_EXCEPTION_MASK_FOR_OVERFLOWANDINVALID (_MCW_EM & ~(_EM_OVERFLOW| _EM_ZERODIVIDE| _EM_INVALID))

#ifdef _WIN64
#define SET_FP_CONTROL_WORD_CATCH_OVERFLOWANDINVALID 	\
	_controlfp_s(&oldControlState, FP_DEFAULT_ROUND_MODE | FP_EXCEPTION_MASK_FOR_OVERFLOWANDINVALID, _MCW_RC | _MCW_EM);
#else
#define SET_FP_CONTROL_WORD_CATCH_OVERFLOWANDINVALID 	\
	_controlfp_s(&oldControlState, FP_DEFAULT_ROUND_MODE | _PC_64 | FP_EXCEPTION_MASK_FOR_OVERFLOWANDINVALID, _MCW_RC | _MCW_EM | _MCW_PC);
#endif


static void zoInitFrameInternal(GfxOcclusionBuffer *zo)
{
	int two_threads = 0;

	PERFINFO_AUTO_START_FUNC();
	etlAddEvent(zo->event_timer, "Draw occluders", ELT_CODE, ELTT_BEGIN);

	CHECKHEAP;

#if ZO_USE_JITTER
	if (zo->use_jitter && gbEnableJitter)
	{
		int m = zo->jitter.draw_frame_num % ZO_NUM_JITTER_FRAMES;
		zo->jitter.amount = (m - 1) * 0.5f; // (-0.5, 0, 0.5)
	}
	else
		zo->jitter.amount = 0.f;
#endif

	SET_FP_CONTROL_WORD_CATCH_OVERFLOWANDINVALID;

	zoFillZbuffer((Vec4H*)zo->zbuffer.data, zo->zbuffer.size);

	zo->debug.occluder_tris_drawn = 0;

#if !_PS3
    // store new state of the two-thread option
	two_threads = gfx_state.debug.two_zo_draw_threads;
	zo->zthread.two_zo_draw_threads = two_threads;
#endif

	gfxModelEnterUnpackCS();

#if !_PS3
	if (two_threads)
		tmQueueUserAPC(zoDrawOccludersAPC, zo->zthread.occlusion_thread2, (ULONG_PTR)zo);
#endif

	zo->debug.occluder_tris_drawn = zoDrawOccluders(zo, two_threads ? 1 : 0);

	etlAddEvent(zo->event_timer, "Draw occluders", ELT_CODE, ELTT_END);

#if !_PS3
	if (two_threads)
	{
		etlAddEvent(zo->event_timer, "Wait for second thread", ELT_CODE, ELTT_BEGIN);
        WaitForEvent(zo->zthread.wait_for_zo_done2,INFINITE);
		etlAddEvent(zo->event_timer, "Wait for second thread", ELT_CODE, ELTT_END);
	}
#endif

	gfxModelLeaveUnpackCS();

	zo->zthread.in_drawoccluders = false;

	CHECKHEAP;
	PERFINFO_AUTO_STOP();
}

static VOID CALLBACK zoInitFrameThread(ULONG_PTR dwParam)
{
	GfxOcclusionBuffer *zo = (GfxOcclusionBuffer *)dwParam;

	if (s_iZOcclusionThread && zo->zthread.wait_for_zo_done)
	{
		autoTimerThreadFrameEnd();
		autoTimerThreadFrameBegin("Z-Occlusion Thread");
	}

	zoInitFrameInternal(zo);

	if (s_iZOcclusionThread && zo->zthread.wait_for_zo_done)
		SetEvent(zo->zthread.wait_for_zo_done);
}

void zoInitFrame(GfxOcclusionBuffer *zo, const Mat44 projection_mat, const Frustum *frustum)
{
	int window_width, window_height;

	if (!zo)
		return;

	zoWaitUntilOccludersDrawn(zo);
	zoWaitUntilDone(zo);

	gfxGetActiveSurfaceSize(&window_width, &window_height);
	zo->zbuffer.width_mul = (float)ZB_DIM / (float)window_width;
	zo->zbuffer.height_mul = (float)ZB_DIM / (float)window_height;

	zo->frustum = *frustum;
	copyMat44(projection_mat, zo->projection_mat);

#if ZO_USE_JITTER
	++zo->jitter.frame_num;
#endif

	if (s_iZOcclusionThread && !zo->zthread.occlusion_thread)
	{
		zo->zthread.occlusion_thread = tmCreateThread(zoThread, NULL);
		assert(zo->zthread.occlusion_thread);
		zo->zthread.wait_for_zo_done = CreateEvent(0, 0, 0, 0);
		tmSetThreadProcessorIdx(zo->zthread.occlusion_thread, THREADINDEX_ZOCCLUSION);

#if !_PS3
		zo->zthread.occlusion_thread2 = tmCreateThread(zoThread, NULL);
		assert(zo->zthread.occlusion_thread2);
		tmSetThreadProcessorIdx(zo->zthread.occlusion_thread2, 3);
		zo->zthread.wait_for_zo_done2 = CreateEvent(0, 0, 0, 0);
#endif
	}

	if (s_iZOcclusionThread && zo->zthread.wait_for_zo_done)
	{
		zo->zthread.needs_occluder_wait = true;
		zo->zthread.in_drawoccluders = true;
		tmQueueUserAPC(zoInitFrameThread, zo->zthread.occlusion_thread, (ULONG_PTR)zo);
	}
	else
		zoInitFrameThread((ULONG_PTR)zo);
}

void zoAccumulateStats(GfxOcclusionBuffer *zo, FrameCounts *frame_counts)
{
	if (zo)
	{
		frame_counts->zo_occluders += zo->occluders.draw_occluder_count + zo->occluders.occluder_count;
		frame_counts->zo_cull_tests += frame_counts->zo_cull_tests + zo->debug.last_num_tests;
		frame_counts->zo_culls += frame_counts->zo_culls + zo->debug.last_num_culls;
	}
}

static void zoCreateZHierarchyInternal(GfxOcclusionBuffer * zo)
{
	int i;

	PERFINFO_AUTO_START_FUNC();
	etlAddEvent(zo->event_timer, "Create hierarchy", ELT_CODE, ELTT_BEGIN);

	zo->zthread.in_createzhierarchy = true;

	// release non-drawn occluders after the main thread is done zo testing & adding new occluders
	for (i = 0; i < zo->occluders.draw_occluder_count; ++i)
	{
		freeOccluder(zo->occluders.draw_occluders[i]);
		zo->occluders.draw_occluders[i] = NULL;
	}

	// swap occluder arrays
	zo->occluders.draw_occluder_count = zo->occluders.occluder_count;
	if (zo->occluders.occluders == zo->occluders.occluders1 )
	{
		zo->occluders.occluders = zo->occluders.occluders2;
		zo->occluders.draw_occluders = zo->occluders.occluders1;
	}
	else
	{
		zo->occluders.occluders = zo->occluders.occluders1;
		zo->occluders.draw_occluders = zo->occluders.occluders2;
	}
	zo->occluders.occluder_count = 0;

	zo->occluders.min_occluder_screenspace = 0;
	zo->debug.last_num_tests = zo->debug.num_tests;
	zo->debug.last_num_exact_tests = zo->debug.num_exact_tests;
	zo->debug.last_num_test_calls = zo->debug.num_test_calls;
	zo->debug.last_num_culls = zo->debug.num_culls;

	zo->debug.num_tests = 0;
	zo->debug.num_exact_tests = 0;
	zo->debug.num_test_calls = 0;
	zo->debug.num_culls = 0;

#if ZO_USE_JITTER
	if (zo->use_jitter)
		zo->jitter.draw_frame_num = zo->jitter.frame_num;
#endif

	createZHierarchy(zo);
	zo->zthread.in_createzhierarchy = false;

	etlAddEvent(zo->event_timer, "Create hierarchy", ELT_CODE, ELTT_END);
	PERFINFO_AUTO_STOP();
}

static VOID CALLBACK zoCreateZHierarchyThreadAPC(ULONG_PTR dwParam)
{
	GfxOcclusionBuffer *zo = (GfxOcclusionBuffer *)dwParam;

	zoCreateZHierarchyInternal(zo);

	if (s_iZOcclusionThread && zo->zthread.wait_for_zo_done)
		SetEvent(zo->zthread.wait_for_zo_done);
}

void zoCreateZHierarchy(GfxOcclusionBuffer * zo)
{
	zoWaitUntilOccludersDrawn(zo);

	if (s_iZOcclusionThread && zo->zthread.wait_for_zo_done)
	{
		zo->zthread.needs_wait = true;
		zo->zthread.in_createzhierarchy = true;
		tmQueueUserAPC(zoCreateZHierarchyThreadAPC, zo->zthread.occlusion_thread, (ULONG_PTR)zo);
	}
	else
		zoCreateZHierarchyInternal(zo);
}

void zoWaitUntilDone(GfxOcclusionBuffer *zo)
{
	if (!zo)
		return;

	if (zo->zthread.needs_wait)
	{
		etlAddEvent(zo->event_timer, "Wait for thread", ELT_CODE, ELTT_BEGIN);
        WaitForEvent(zo->zthread.wait_for_zo_done, INFINITE);
		etlAddEvent(zo->event_timer, "Wait for thread", ELT_CODE, ELTT_END);
		zo->zthread.needs_wait = false;
	}
}

void zoWaitUntilOccludersDrawn(GfxOcclusionBuffer *zo)
{
	if (!zo)
		return;

	if (zo->zthread.needs_occluder_wait)
	{
		etlAddEvent(zo->event_timer, "Wait for thread", ELT_CODE, ELTT_BEGIN);
        WaitForEvent(zo->zthread.wait_for_zo_done, INFINITE);
		etlAddEvent(zo->event_timer, "Wait for thread", ELT_CODE, ELTT_END);
		zo->zthread.needs_occluder_wait = false;
	}
}

static float zoGetZWeightMultiplier(float lastZ)
{
	// If the values of last_z are to be trusted, 99% of our z-buffer is open space in front of the camera, at the time I am writing this.  [RMARR - 6/6/12]
	const F32 fSignificantZDist = 0.99f;
	const F32 fScale = 1.f/(1.f/fSignificantZDist-1.f);

	if (lastZ < fSignificantZDist)
	{
		return 1.f;
	}
	else
	{
		return fScale*(1.f/saturate(lastZ)-1.f);
	}
}

void zoAddRejectedOccluder(GfxOcclusionBuffer *zo, ModelLOD * m, float fZWeightedScreenSpace)
{
	// record rejected occluders (for debugging)
	int i,j;
	for (i = 0; i < zo->debug.iNumRejectedOccluders; i++)
	{
		if (fZWeightedScreenSpace > zo->debug.aRejectedOccluders[i].fZWeightedScreenSpace)
		{
			for (j = MIN(zo->debug.iNumRejectedOccluders,MAX_REJECTED_OCCLUDERS-1); j > i; j--)
				zo->debug.aRejectedOccluders[j] = zo->debug.aRejectedOccluders[j-1];
			break;
		}
	}

	if (i < MAX_REJECTED_OCCLUDERS)
	{
		zo->debug.aRejectedOccluders[i].pchName = m->debug_name;
		zo->debug.aRejectedOccluders[i].iVerts = m->tri_count;
		zo->debug.aRejectedOccluders[i].iTris = m->vert_count;
		zo->debug.aRejectedOccluders[i].fZWeightedScreenSpace = fZWeightedScreenSpace;
		if (zo->debug.iNumRejectedOccluders < MAX_REJECTED_OCCLUDERS)
			zo->debug.iNumRejectedOccluders++;
	}
}

void zoFinishRejectedOccluders(GfxOcclusionBuffer *zo)
{
	int i;
	int iTriCount=0;
	
	if (zo==NULL)
		return;
	
	// I wrote this function to try to account for the occluders that are forced out because other occluders used up the draw budget,
	// but it doesn't work, because I can't know at this point whether an object will actually draw.  The correct solution is to double
	// buffer the same way the occlusion system does, and use the real data.  [RMARR - 9/11/13]
	for (i = 0; i < zo->occluders.occluder_count; i++)
	{
		Occluder * o = zo->occluders.occluders[i];
		int tri_count = (o->m&&o->m->data)?o->m->data->tri_count:12;
		int vert_count = (o->m&&o->m->data)?o->m->data->vert_count:8;

		if (((iTriCount + tri_count) < MAX_OCCLUDER_TRIS && vert_count < MAX_OCCLUDER_VERTS) || gfx_state.debug.zocclusion_norestrict)
		{
			//iTriCount += tri_count;
		}
		else
		{
			zoAddRejectedOccluder(zo, o->m, o->fZWeightedScreenSpace);
		}
	}
}

static int zoCheckAddOccluderInternal(GfxOcclusionBuffer *zo, ModelLOD *m, const Vec3 minBounds, const Vec3 maxBounds, VolumeFaces faceBits, const Mat4 matModelToWorld, U32 ok_materials, bool double_sided, F32 fMultiplier, F32 fTriMultiplier)
{
	int i, j, ret = 0;

	PERFINFO_AUTO_START_FUNC_L2();

	if (m || (minBounds && maxBounds))
	{
		// fun hack!  The screen space is stored from the call to zoTest.
		F32 fWeightedScreenSpace = zo->occluders.last_screen_space * fMultiplier * fTriMultiplier;

		ret = -1;

		if (zo->occluders.last_screen_space >  MIN_OCCLUDER_REAL_SCREEN_SPACE // automatic disqualification.  Don't draw if small, period.
			&& fWeightedScreenSpace > MIN_OCCLUDER_SCREEN_SPACE
			&& (fWeightedScreenSpace > zo->occluders.min_occluder_screenspace || zo->occluders.occluder_count < MAX_OCCLUDERS))
		{
			if (zo->occluders.occluder_count == MAX_OCCLUDERS)
			{
				// record rejected occluders (for debugging)
				zoAddRejectedOccluder(zo, zo->occluders.occluders[zo->occluders.occluder_count-1]->m, zo->occluders.occluders[zo->occluders.occluder_count-1]->fZWeightedScreenSpace);

				// remove the smallest
				freeOccluder(zo->occluders.occluders[--zo->occluders.occluder_count]);
			}

			// add in the appropriate place
			for (i = 0; i < zo->occluders.occluder_count; i++)
			{
				if (fWeightedScreenSpace > zo->occluders.occluders[i]->screenSpace)
				{
					for (j = zo->occluders.occluder_count; j > i; j--)
						zo->occluders.occluders[j] = zo->occluders.occluders[j-1];
					break;
				}
			}

			zo->occluders.occluders[i] = createOccluder(m, minBounds, maxBounds, faceBits, matModelToWorld, fWeightedScreenSpace, ok_materials, double_sided);
			zo->occluders.occluders[i]->fZWeightedScreenSpace = zo->occluders.last_screen_space*fMultiplier;
			zo->occluders.min_occluder_screenspace = zo->occluders.occluders[zo->occluders.occluder_count]->screenSpace;
			zo->occluders.occluder_count++;
		}
		else if (zo->occluders.last_screen_space >  MIN_OCCLUDER_REAL_SCREEN_SPACE)
		{
			// record rejected occluders (for debugging)
			zoAddRejectedOccluder(zo, m, zo->occluders.last_screen_space * fMultiplier);
		}
	}

	PERFINFO_AUTO_STOP_L2();

	return ret;
}

int zoCheckAddOccluder(GfxOcclusionBuffer *zo, ModelLOD *m, const Mat4 matModelToWorld, U32 ok_materials, bool double_sided)
{
	F32 multiplier, fTriMultiplier = 1.0f;
	if (!modelLODIsLoaded(m) || m->tri_count == 0)
		return 0;

	multiplier = zoGetZWeightMultiplier(zo->occluders.last_z);

 	if (!gfx_state.debug.zocclusion_norestrict)
	{
		// If a model has anything like our total occluder tri count, we basically can't use it at all
		fTriMultiplier *= 1.f - saturate(m->tri_count / 1000.0f);
	}
	return zoCheckAddOccluderInternal(zo, m, NULL, NULL, VOLFACE_ALL, matModelToWorld, ok_materials, double_sided, multiplier, fTriMultiplier);
}

// This has a much larger triangle allowance than zoCheckAddOccluder, added as a separate interface for simplicity during clustering development.
int zoCheckAddOccluderCluster(GfxOcclusionBuffer *zo, ModelLOD *m, const Mat4 matModelToWorld, U32 ok_materials, bool double_sided)
{
	F32 multiplier;
	if (!modelLODIsLoaded(m) || m->tri_count == 0)
		return 0;
	multiplier = 1.f - saturate(zo->occluders.last_z);

	return zoCheckAddOccluderInternal(zo, m, NULL, NULL, VOLFACE_ALL, matModelToWorld, ok_materials, double_sided, multiplier, 1.f - saturate((m->tri_count - 50) / 32000.f));
}

int zoCheckAddOccluderVolume(GfxOcclusionBuffer *zo, const Vec3 minBounds, const Vec3 maxBounds, VolumeFaces faceBits, const Mat4 matModelToWorld)
{
	return zoCheckAddOccluderInternal(zo, NULL, minBounds, maxBounds, faceBits, matModelToWorld, 0xffffffff, false, 2.f, 1.f);
}

int zoTestSphere(GfxOcclusionBuffer *zo, const Vec3 centerEyeCoords, float radius, int isZClipped, bool *occlusionReady)
{
	float fx1, fx2, fy1, fy2;
	ZType z1;
	int ret;

	PERFINFO_AUTO_START_FUNC_L2();

	zo->occluders.last_screen_space = -1;
	zo->debug.num_test_calls++;

	if (isZClipped)
	{
		PERFINFO_AUTO_STOP_L2();
		return 1;
	}

	if (occlusionReady)
	{
		*occlusionReady = !zo->zthread.in_createzhierarchy;
		if (zo->zthread.in_createzhierarchy)
		{
			PERFINFO_AUTO_STOP_L2();
			return 1;
		}

		zoWaitUntilDone(zo);
	}
	else
	{
		assert(!zo->zthread.in_createzhierarchy);
		zoWaitUntilDone(zo);
	}

	// if zoTestSphere is called, the sphere is never completely off screen
	getSphereScreenExtents(zo, centerEyeCoords, radius, &fx1, &fy1, &fx2, &fy2, &z1);

	// test
	zo->debug.num_tests++;
	ret = testZRectHierarchical(zo, fx1, fy1, fx2, fy2, z1 - TEST_Z_OFFSET);
	if (!ret)
		++zo->debug.num_culls;

	PERFINFO_AUTO_STOP_L2();

	return ret;
}

static Frustum * zoGetFrustum(GfxOcclusionBuffer *zo)
{
	if (gfx_state.debug.frustum_debug.frustum_debug_mode == 2)
	{
		return &gfx_state.debug.frustum_debug.static_frustum;
	}
	else
	{
		return &zo->frustum;
	}
}

static bool calcDeltasForBoundsTriangle(F32 * dzdx,F32 * dzdy,Vec3 const p0,Vec3 const p1,Vec3 const p2)
{
	float fdx1, fdx2, fdy1, fdy2, d1, d2, fz;

	fdx1 = p1[0] - p0[0];
	fdy1 = p1[1] - p0[1];

	fdx2 = p2[0] - p0[0];
	fdy2 = p2[1] - p0[1];

	fz = fdx1 *	fdy2 - fdx2	* fdy1;

	// Cull back-facing tris (z-positive in screen space)
	if (fz >= 0.f)
		return 0;

	fz = 1.0 / fz;

	fdx1 *=	fz;
	fdy1 *=	fz;
	fdx2 *=	fz;
	fdy2 *=	fz;

	d1 = p1[2] - p0[2];
	d2 = p2[2] - p0[2];
	*dzdx = fdy2 * d1 - fdy1 * d2;
	*dzdy = fdx1 * d2 - fdx2 * d1;

	return 1;
}

// TODO - improve (I only need 12 of these positions, but I have 64)
typedef Vec4H * ClippedPositions;
static Vec4H * _getClippedPosition(ClippedPositions pos,int i,int j)
{
	return &pos[j*8+i];
}

int zoTestSideExact(GfxOcclusionBuffer *zo, const Vec4 transBounds[8], Vec4H const * screenSpaceBounds, ClippedPositions avClippedPos, int posClipped[8], int aiIndices[4])
{
	int i;
	Vec4H tmin,tmax;
	Vec3 tmin3,tmax3; // shouldn't need this
	F32 fMinX,fMinY,fMaxX,fMaxY,fMinZ;
	F32 dzdx,dzdy;
	Vec4H avRefPts[3];
	Vec3 vRefPt;
	static F32 fMultTest = 1.0f;

	Frustum * frustum = zoGetFrustum(zo);

	// figure out my ref points.
	for (i=0;i<4;i++)
	{
		//Find a vert that is not clipped.
		if (!posClipped[aiIndices[i]])
		{
			// copy the clipped and unclipped verts as appropriate.  We just need 3 that make a visible triangle
			int iCurIdx = aiIndices[i];
			int iPrevIdx = aiIndices[(i+3)%4];
			int iNextIdx = aiIndices[(i+1)%4];
			if (!posClipped[iPrevIdx])
				copyVec4H(screenSpaceBounds[iPrevIdx],avRefPts[0]);
			else
				copyVec4H(*_getClippedPosition(avClippedPos,iCurIdx,iPrevIdx),avRefPts[0]);
			copyVec4H(screenSpaceBounds[iCurIdx],avRefPts[1]);
			if (!posClipped[iNextIdx])
				copyVec4H(screenSpaceBounds[iNextIdx],avRefPts[2]);
			else
				copyVec4H(*_getClippedPosition(avClippedPos,iCurIdx,iNextIdx),avRefPts[2]);

			break;
		}
	}

	if (i==4)
	{
		// face not on-screen
		return 0;
	}

	// Need to get z-deltas here
	if (!calcDeltasForBoundsTriangle(&dzdx,&dzdy,Vec4HToVec4(avRefPts[0]),Vec4HToVec4(avRefPts[1]),Vec4HToVec4(avRefPts[2])))
	{
		// This is not a valid side to test, so we'll let the other side tests handle it
		return 0;
	}

	setVec4H(tmin, 1, 1, 1, 1);
	setVec4H(tmax, -1, -1, -1, -1);

	for (i=0;i<4;i++)
	{
		int idx = aiIndices[i];

		int x = idx & 0x1;
		int y = (idx & 0x2) >> 1;
		int z = (idx & 0x4) >> 2;

		if (!posClipped[idx])
		{
			int idx2;
			vec4HRunningMinMax(screenSpaceBounds[idx], tmin, tmax);

			idx2 = boundsIndex(x, y, !z);
			if (posClipped[idx2])
			{
				vec4HRunningMinMax(*_getClippedPosition(avClippedPos,idx,idx2), tmin, tmax);
			}

			idx2 = boundsIndex(x, !y, z);
			if (posClipped[idx2])
			{
				vec4HRunningMinMax(*_getClippedPosition(avClippedPos,idx,idx2), tmin, tmax);
			}

			idx2 = boundsIndex(!x, y, z);
			if (posClipped[idx2])
			{
				vec4HRunningMinMax(*_getClippedPosition(avClippedPos,idx,idx2), tmin, tmax);
			}
		}
	}

	if (outOfBounds3H(tmin, tmax))
		return 0;

	copyVec3(Vec4HToVec4(tmin),tmin3);
	copyVec3(Vec4HToVec4(tmax),tmax3);

	// clip to screen
	MAX1F(tmin3[0], -1);
	MAX1F(tmin3[1], -1);
	MIN1F(tmax3[0], 1);
	MIN1F(tmax3[1], 1);

	fMinX = tmin3[0] * 0.5f + 0.5f;
	fMinY = tmin3[1] * 0.5f + 0.5f;
	fMaxX = tmax3[0] * 0.5f + 0.5f;
	fMaxY = tmax3[1] * 0.5f + 0.5f;

	fMinZ = tmin3[2] * ZMAX;

	// convert dzdx and dzdy to the new space we're working in
	dzdx *= 2.f;
	dzdy *= 2.f;

	vRefPt[0] = Vec4HToVec4(avRefPts[1])[0] * 0.5f + 0.5f;
	vRefPt[1] = Vec4HToVec4(avRefPts[1])[1] * 0.5f + 0.5f;
	vRefPt[2] = Vec4HToVec4(avRefPts[1])[2] * ZMAX; // TODO - this right?

	return testZGradientRectHierarchical(zo,fMinX,fMinY,fMaxX,fMaxY,fMinZ,vRefPt,dzdx,dzdy);
}

int zoTestBoundsExact(GfxOcclusionBuffer *zo, const Vec4 transBounds[8])
{
	int x,y,z;
	int i,idx;
	// CCW
	int aaiIndices[6][4] = {	{0,4,6,2}, // x=0
								{1,3,7,5}, // x=1
								{0,1,5,4}, // y=0
								{2,6,7,3}, // y=1
								{0,2,3,1}, // z=0
								{4,5,7,6}, // z=1
	};

	Vec4H screenspaceBounds[8];
	int posClipped[8];
	Vec4H vClippedPos[64];

	Frustum * frustum = zoGetFrustum(zo);

	for (i=0;i<8;i++)
	{
		if (transBounds[i][2] > frustum->znear)
		{
			posClipped[i] = 1;
		}
		else
		{
			posClipped[i] = 0;
		}
		mulVec3ProjMat44(transBounds[i], zo->projection_mat, Vec4HToVec4(screenspaceBounds[i]));
		Vec4HToVec4(screenspaceBounds[i])[3] = 0.0f;
	}

	// Find all the clipped edge points
	for (idx=0;idx<8;idx++)
	{
		if (!posClipped[idx])
		{
			Vec3 tpos;
			int idx2;
			x = idx & 0x1;
			y = (idx & 0x2) >> 1;
			z = (idx & 0x4) >> 2;

			idx2 = boundsIndex(x, y, !z);
			if (posClipped[idx2])
			{
				clipToZ(frustum, transBounds[idx], transBounds[idx2], tpos);
				// TODO - optimize
				mulVec3ProjMat44(tpos, zo->projection_mat, Vec4HToVec4(*_getClippedPosition(vClippedPos,idx,idx2)));
				Vec4HToVec4(*_getClippedPosition(vClippedPos,idx,idx2))[3] = 0.0f;
			}

			idx2 = boundsIndex(x, !y, z);
			if (posClipped[idx2])
			{
				clipToZ(frustum, transBounds[idx], transBounds[idx2], tpos);
				mulVec3ProjMat44(tpos, zo->projection_mat, Vec4HToVec4(*_getClippedPosition(vClippedPos,idx,idx2)));
				Vec4HToVec4(*_getClippedPosition(vClippedPos,idx,idx2))[3] = 0.0f;
			}

			idx2 = boundsIndex(!x, y, z);
			if (posClipped[idx2])
			{
				clipToZ(frustum, transBounds[idx], transBounds[idx2], tpos);
				mulVec3ProjMat44(tpos, zo->projection_mat, Vec4HToVec4(*_getClippedPosition(vClippedPos,idx,idx2)));
				Vec4HToVec4(*_getClippedPosition(vClippedPos,idx,idx2))[3] = 0.0f;
			}
		}
	}

	for (i=0;i<6;i++)
	{
		if (zoTestSideExact(zo,transBounds,screenspaceBounds,vClippedPos,posClipped,aaiIndices[i]))
			return 1;
	}

	return 0;
}

// This function is not capable of dealing with the case of the bounding box intersecting the near plane inside the screenbox.  Check for this before calling
int zoTestBounds(GfxOcclusionBuffer *zo, const Vec4 eyespaceBounds[8], int isZClipped, U32 *last_update_time, U8 *occluded_bits, U8 *inherited_bits, F32 *screen_space, bool *occlusionReady)
{
	int ret = 0;
	float fx1, fx2, fy1, fy2;
	ZType z1;

	Frustum * pFrustum;

	if (!zo)
		return 1;

	pFrustum = zoGetFrustum(zo);

	PERFINFO_AUTO_START_FUNC_L2();


	if (occlusionReady)
	{
		*occlusionReady = !zo->zthread.in_createzhierarchy;
		if (zo->zthread.in_createzhierarchy)
		{
			PERFINFO_AUTO_STOP_L2();
			return 1;
		}

		// Not thread safe, and apparently not needed: zoWaitUntilDone(zo);
	}
	else
	{
		assert(!zo->zthread.in_createzhierarchy);
		zoWaitUntilDone(zo);
	}


	zo->occluders.last_screen_space = -1;

	// transform and clip
	zo->debug.num_test_calls++;
	if (isZClipped)
		ret = getScreenExtents(pFrustum, zo->projection_mat, eyespaceBounds, &fx1, &fy1, &fx2, &fy2, &z1, true);
	else
		ret = getScreenExtentsNoZClip(zo->projection_mat, eyespaceBounds, &fx1, &fy1, &fx2, &fy2, &z1);

	if (ret)
	{
		// fun hack!  Store the screen space so we don't have to transform the bounds again when zoCheckAddOccluder is called.
		zo->occluders.last_screen_space = (fx2-fx1) * (fy2-fy1);
		if (screen_space)
			*screen_space = zo->occluders.last_screen_space;
		zo->occluders.last_z = z1;

#if SUPPORT_SHOW_ZRECT
		if ( debugShowZRect )
		{
			static float fZTest = 0.01f;
			int width, height, color_scale = 100+(int)(MAX(0,(z1-0.98)/0.02f * 155));
			Color black;
			black.r = 0;
			black.g = CLAMP(color_scale, 0, 255);
			black.b = 0;
			black.a = 255;
			gfxGetActiveSurfaceSize(&width, &height);
		
			gfxDrawBox((int)( fx1 * width ), height - (int)( fy1 * height ), (int)( fx2 * width ), height - (int)( fy2 * height ), fZTest, black );
		}
#endif

		if (ret)
		{
			zo->debug.num_tests++;
			ret = testZRectHierarchical(zo, fx1, fy1, fx2, fy2, z1 - TEST_Z_OFFSET);

			if (ret)
			{
				// The improvement from this is marginal, but it's better when the occlusion is better.  There are 2 things I could do to improve the bang
				// for the buck on this:
				// 1) optimize the code
				// 2) only test pixels where the face actually is
				// For now, I'm turning the feature on, but the CPU time is at a premium. However, there are parts of the model queuing pipeline that are
				// sadly MUCH slower than doing this test
				if (gbEnableExactTests)
				{
					zo->debug.num_exact_tests++;

					// We passed our crude rectangle test, which means we may be visible. A more careful test is in order.
					ret = zoTestBoundsExact(zo, eyespaceBounds);
				}
			}
		}
	}

#if ZO_USE_JITTER
	if (zo->use_jitter && last_update_time && occluded_bits)
	{
		U32 delta = zo->jitter.frame_num - *last_update_time;
		MIN1(delta, ZO_NUM_JITTER_FRAMES);
		*occluded_bits <<= delta;
		*last_update_time = zo->jitter.frame_num;

		if (!ret)
		{
			// not visible (occluded)
			U8 test_bits;

			*occluded_bits |= 1;
			test_bits = *occluded_bits;
			if (inherited_bits)
				test_bits |= *inherited_bits;

			// if occluded this frame but not last 2 frames, override visibility
			ret = (test_bits & ZO_TEST_BITS) != ZO_TEST_BITS;
		}

		if (inherited_bits)
			*inherited_bits |= *occluded_bits;
	}
#endif

	PERFINFO_AUTO_STOP_L2();

	if (!ret)
		++zo->debug.num_culls;
	return ret;
}

int zoTestBoundsSimple(GfxOcclusionBuffer *zo, const Vec4 eyespaceBounds[8], int isZClipped)
{
	return zoTestBounds(zo, eyespaceBounds, isZClipped, NULL, NULL, NULL, NULL, NULL);
}

int zoTestBoundsSimpleWorld(GfxOcclusionBuffer *zo, const Vec3 minBounds, const Vec3 maxBounds, const Mat4 matLocalToEye, int isZClipped, bool *occlusionReady)
{
	Vec4_aligned eyespaceBounds[8];
	if (frustumCheckBoxNearClippedInView(zoGetFrustum(zo),minBounds, maxBounds, matLocalToEye))
	{
		return false;
	}
	mulBounds(minBounds, maxBounds, matLocalToEye, eyespaceBounds);
	return zoTestBounds(zo, eyespaceBounds, isZClipped, NULL, NULL, NULL, NULL, occlusionReady);
}

void zoSetOccluded(GfxOcclusionBuffer *zo, U32 *last_update_time, U8 *occluded_bits)
{
#if ZO_USE_JITTER
	if (zo->use_jitter && last_update_time && occluded_bits)
	{
		U32 delta = zo->jitter.frame_num - *last_update_time;
		MIN1(delta, ZO_NUM_JITTER_FRAMES);
		*occluded_bits <<= delta;
		*occluded_bits |= 1;
		*last_update_time = zo->jitter.frame_num;
	}
#endif
}

////////////////////////////////////////////////////
////////////////////////////////////////////////////

// if most of the materials are ok, this is better for performance
#define DO_PRETRANSFORM 1

static void zoDraw(SA_PARAM_NN_VALID GfxZBuffData *zo, Mat4 matToEye, SA_PARAM_NN_VALID Vec3 *verts, int vert_count, SA_PARAM_NN_VALID int *idxs, int *facemask, int tex_count, 
	TexID *tex_idx, U32 ok_materials, int check_facing)
{
	ZBufferPointCached *transformCache;
	ZBufferPoint *p0, *p1, *p2;
	int idx, tex, trinum = 0;
	U32 tex_bit;
	Mat44 mat44ToEye;
	Mat44 matToClip;
	F32 ydim = zo->ydim, yoffset = zo->yoffset;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	mat43to44(matToEye, mat44ToEye);
	mulMat44Inline(zo->projection_mat, mat44ToEye, matToClip);

	zo->point_cache_debug = transformCache = ScratchAlloc(sizeof(*transformCache) * vert_count);
	assert(transformCache);

#if DO_PRETRANSFORM
#if ZO_USE_JITTER
	if (zo->use_jitter)
	{
		F32 jitter_amount = zo->amount;
		for (idx = 0; idx < vert_count; ++idx)
		{
			// When pretransforming, there is no need to test, or store this value
			//transformCache[idx].cached = 1;
            transformPointJitter(&transformCache[idx].p, verts[idx], matToClip, ydim, yoffset, jitter_amount, zo->projection_far, zo->one_over_projection_far);
		}
	}
	else
#endif
	{
		for (idx = 0; idx < vert_count; ++idx)
		{
			// When pretransforming, there is no need to test, or store this value
			//transformCache[idx].cached = 1;
			transformPoint(&transformCache[idx].p, verts[idx], matToClip, ydim, yoffset);
		}
	}
#endif

	tex_bit = 1;
	for (tex = 0; tex < tex_count; tex++)
	{
		int idx_count;

		if (tex_idx)
			idx_count = tex_idx[tex].count * 3;
		else
			idx_count = 12 * 3;

		if (ok_materials & tex_bit)
		{
			for (; idx_count > 0; idx_count-=3)
			{
				idx = *idxs;
				++idxs;
				p0 = &transformCache[idx].p;
#if !DO_PRETRANSFORM
				if (!transformCache[idx].cached)
				{
					transformCache[idx].cached = 1;
					transformPoint(zo, p0, verts[idx], matToClip, ydim, yoffset);
				}
#endif

				idx = *idxs;
				++idxs;
				p1 = &transformCache[idx].p;
#if !DO_PRETRANSFORM
				if (!transformCache[idx].cached)
				{
					transformCache[idx].cached = 1;
					transformPoint(zo, p1, verts[idx], matToClip, ydim, yoffset);
				}
#endif

				idx = *idxs;
				++idxs;
				p2 = &transformCache[idx].p;
#if !DO_PRETRANSFORM
				if (!transformCache[idx].cached)
				{
					transformCache[idx].cached = 1;
					transformPoint(zo, p2, verts[idx], matToClip, ydim, yoffset);
				}
#endif

				if (!facemask || facemask[trinum++])
                {
#if ZO_DRAW_INTERLEAVED
                    drawZTriangleClip(zo->data, zo->width, p0, p1, p2, 0, check_facing, ydim, yoffset, &zo->occluder_tris_drawn, 0, 1);
#else
					drawZTriangleClip(zo->data, zo->width, p0, p1, p2, 0, check_facing, ydim, yoffset, &zo->occluder_tris_drawn);
#endif
                }
			}
		}
		else
		{
			idxs += idx_count;
		}

		tex_bit <<= 1;
	}

	ScratchFree(transformCache);

	PERFINFO_AUTO_STOP();
}

static void zoDrawModel(GfxZBuffData *zo, ModelLOD *m, Mat4 matModelToEye, U32 ok_materials, bool check_facing)
{
	if (!modelLODIsLoaded(m) || !m->data || !m->data->tex_idx)
		return;
	
	//modelLockUnpacked(m); // May want to do this, but it's slow (critical section lock) - change it to more complicated InterlockedIncrements instead if needed?
	gfxModelSetupZOTris(m); // Also flags model->last_used_timestamp
	if (!m->unpack.verts || !m->unpack.tris) {
		//modelUnlockUnpacked(m);
		return;
	}
#if ZO_SPU_DRAW
//	zoDraw(zo, matModelToEye, m->unpack.verts, m->data->vert_count, m->unpack.tris, NULL,
//			m->data->tex_count, m->data->tex_idx, ok_materials, check_facing); 
	zoSpuDraw(zo, matModelToEye, m->unpack.verts, m->data->vert_count, m->unpack.tris, NULL,
			m->data->tex_count, m->data->tex_idx, ok_materials, check_facing);
#else
	zoDraw(zo, matModelToEye, m->unpack.verts, m->data->vert_count, m->unpack.tris, NULL,
			m->data->tex_count, m->data->tex_idx, ok_materials, check_facing);
#endif
	//modelUnlockUnpacked(m);
}

static Vec3 box_verts[] = { {-1,-1,1}, {1,-1,1}, {-1,-1,-1}, {1,-1,-1}, {-1,1,1}, {1,1,1}, {-1,1,-1}, {1,1,-1} };
static int box_tris[] = { 0,2,3, 3,1,0, 4,5,7, 7,6,4, 0,1,5, 5,4,0, 1,3,7, 7,5,1, 3,2,6, 6,7,3, 2,0,4, 4,6,2 };
static VolumeFaces box_facebits[] = {	VOLFACE_NEGY, VOLFACE_NEGY, VOLFACE_POSY, VOLFACE_POSY,
													VOLFACE_POSZ, VOLFACE_POSZ, VOLFACE_POSX, VOLFACE_POSX,
													VOLFACE_NEGZ, VOLFACE_NEGZ, VOLFACE_NEGX, VOLFACE_NEGX };

static void zoDrawVolume(GfxZBuffData *zo, Vec3 minBounds, Vec3 maxBounds, VolumeFaces faceBits, Mat4 matModelToEye)
{
	Vec3 verts[8];
	int facemask[] = { 1,1,1,1,1,1,1,1,1,1,1,1 };
	bool check_facing = false;
	int i;

	if (!faceBits || (faceBits & VOLFACE_ALL) == VOLFACE_ALL)
	{
		Vec3 localEyePos;

		// multiply eyespace origin position by inverse model-to-eye matrix
		// to test if the eye point is inside the box
		mulVecMat3Transpose(matModelToEye[3], matModelToEye, localEyePos);
		negateVec3(localEyePos, localEyePos);

		check_facing = !pointBoxCollision(localEyePos, minBounds, maxBounds);
	}

	for (i = 0; i < 8; ++i)
	{
		setVec3(verts[i], 
			(box_verts[i][0]<0)?minBounds[0]:maxBounds[0],
			(box_verts[i][1]<0)?minBounds[1]:maxBounds[1],
			(box_verts[i][2]<0)?minBounds[2]:maxBounds[2]);
	}

	for (i = 0; i < 12; ++i)
		facemask[i] = box_facebits[i] & faceBits;

#if ZO_SPU_DRAW
//    zoDraw(zo, matModelToEye, verts, 8, box_tris, facemask, 1, NULL, 0xffffffff, check_facing);
    zoSpuDraw(zo, matModelToEye, verts, 8, box_tris, facemask, 1, NULL, 0xffffffff, check_facing);
#else
    zoDraw(zo, matModelToEye, verts, 8, box_tris, facemask, 1, NULL, 0xffffffff, check_facing);
#endif
}

bool zoIsReadyForRead(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo)
{
	// TODO - replace with uInterlockedReady?
	return zo->debug.occluder_tris_drawn;
	//return zo->uInterlockedReady;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////


#if 0
// Needs data uncommented in GfxOcclusionTestData
/* Results:

PC Debug SSE aligned
aligned: 49.606155ms
PC Debug Scalar
gseTest: 152.661159ms

Xbox Profile scalar
696ms
Xbox Profile VMX after alignment
255ms

Xbox Debug
525.332275ms
WHY?!?!  Probably because we're linking against debug libraries and XMVector*Blah don't get intrinsics

*/
//AUTO_RUN;
void getScreenExtentsSpeedTest(void)
{
	float fminX, fminY, fmaxX, fmaxY;
	ZType minZ;
	int timer = timerAlloc();
	int i;
	extern Mat44 proj;
	extern Vec4 transBounds[][8];
	extern int transbounds_size;
	printf("Starting test...\n");
	for (i=0; i<1000; i++)
	{
		int k;
		for (k=0; k<transbounds_size; k++)
		{
			getScreenExtentsNoZClip(proj, transBounds[k], &fminX, &fminY, &fmaxX, &fmaxY, &minZ);
		}
	}

	printf("gseTest: %fms\n", timerElapsed(timer) * 1000.f);
	timerFree(timer);
}
#endif
