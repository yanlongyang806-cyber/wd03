#pragma once

#include "dynNode.h"
#include "Quat.h"
#include "timing.h"
#include "dynSkeleton.h"
#include "WorldBounds.h"

#if 0
#ifdef CHECK_FINITEVEC3
#undef CHECK_FINITEVEC3
#endif
// JE: Added these macros to track down something going to non-finite
#define CHECK_FINITEVEC3(vec) assert(FINITEVEC3(vec))
#else
#define CHECK_FINITEVEC3(vec)
#endif

#define MAX_DYNDIST_ORIGIN			MAX_PLAYABLE_COORDINATE
#define MAX_DYNDIST_ORIGIN_SQR		MAX_PLAYABLE_DIST_ORIGIN_SQR

// See CHECK_DYNPOS_NONFATAL and CHECK_DYNPOS for proper use.
extern F32 g_dynNodeMaxDistOriginSqr;
F32 dynNodeSetWorldRangeLimit(F32 maxDistOriginSqr);
__forceinline bool dynNodeWorldPositionValid(const Vec3 vPosition)
{
	return lengthVec3Squared(vPosition) <= g_dynNodeMaxDistOriginSqr;
}
__forceinline bool dynNodeWorldPositionValidWithTolerance(const Vec3 vPosition, F32 fTolerance)
{
	F32 fDistToPos =
		SQR(fabs(vPosition[0])+fTolerance) +
		SQR(fabs(vPosition[1])+fTolerance) +
		SQR(fabs(vPosition[2])+fTolerance);
	return fDistToPos <= g_dynNodeMaxDistOriginSqr;
}

#define CHECK_DYNPOS_NONFATAL(vInput) \
	dynNodeWorldPositionValid((vInput))

#define CHECK_DYNPOS_NONFATAL_WITH_TOLERANCE(vInput, fTolerance) \
	dynNodeWorldPositionValidWithTolerance((vInput), fabs(fTolerance))

#define CHECK_DYNPOS(vInput)	\
	assertmsg(CHECK_DYNPOS_NONFATAL(vInput), "Invalid DynNode position.")



#pragma push_macro("vWorldSpacePos_DNODE")
#undef vWorldSpacePos_DNODE 
#pragma push_macro("qWorldSpaceRot_DNODE")
#undef qWorldSpaceRot_DNODE 
#pragma push_macro("vWorldSpaceScale_DNODE")
#undef vWorldSpaceScale_DNODE 

#pragma push_macro("vPos_DNODE")
#undef vPos_DNODE 
#pragma push_macro("qRot_DNODE")
#undef qRot_DNODE 
#pragma push_macro("vScale_DNODE")
#undef vScale_DNODE 

__forceinline void dynNodeSetDirtyInline(DynNode* pDynNode)
{
	DynNode* pNodes[200];
	int count = 1;
	int first = 1;

	if (pDynNode->uiCriticalBone)
		return;
	PERFINFO_AUTO_START_L3(__FUNCTION__,1);
	pNodes[0] = pDynNode;
	while(count)
	{
		pDynNode = pNodes[--count];
		
		if(!first){
			if(pDynNode->pSibling){
				assert(count < ARRAY_SIZE(pNodes));
			
				pNodes[count++] = pDynNode->pSibling;
			}
		}else{
			first = 0;
		}

		// No need to proceed further if we're already dirty...
		// all our children must also be dirty
		if (!pDynNode->uiDirtyBits && !pDynNode->uiCriticalBone)
		{
			pDynNode->uiDirtyBits = 1;

			if(pDynNode->pChild){
				assert(count < ARRAY_SIZE(pNodes));
			
				pNodes[count++] = pDynNode->pChild;
			}
		}
	}
	PERFINFO_AUTO_STOP_L3();
}


__forceinline U8 dynNodeGetFlagsInline( const DynNode* pDynNode )
{
	return pDynNode->uiTransformFlags;
}

__forceinline const char* dynNodeGetNameInline(const DynNode* pDynNode)
{
	return pDynNode->pcTag;
}

__forceinline void dynNodeGetLocalPosInline(const DynNode* pDynNode, Vec3 vDst)
{
	copyVec3(pDynNode->vPos_DNODE, vDst);
}

__forceinline void dynNodeGetLocalRotInline(const DynNode* pDynNode, Quat qDst)
{
	copyQuat(pDynNode->qRot_DNODE, qDst);
}

__forceinline void dynNodeGetLocalScaleInline(const DynNode* pDynNode, Vec3 vDst)
{
	copyVec3(pDynNode->vScale_DNODE, vDst);
}

__forceinline const F32 *dynNodeGetLocalPosRefInline(const DynNode* pDynNode)
{
	return pDynNode->vPos_DNODE;
}

__forceinline const F32 *dynNodeGetLocalRotRefInline(const DynNode* pDynNode)
{
	return pDynNode->qRot_DNODE;
}

__forceinline const F32 *dynNodeGetLocalScaleRefInline(const DynNode* pDynNode)
{
	return pDynNode->vScale_DNODE;
}

__forceinline void dynNodeAssumeCleanGetWorldSpacePosInline(const DynNode* pDynNode, Vec3 vDst)
{
	copyVec3(pDynNode->vWorldSpacePos_DNODE, vDst);
}

__forceinline const F32* dynNodeAssumeCleanGetWorldSpacePosRefInline(const DynNode* pDynNode)
{
	return pDynNode->vWorldSpacePos_DNODE;
}

__forceinline const F32* dynNodeAssumeCleanGetWorldSpaceRotRefInline(const DynNode* pDynNode)
{
	return pDynNode->qWorldSpaceRot_DNODE;
}

__forceinline void dynNodeSetPosInline(DynNode* pDynNode, const Vec3 vInput)
{
	PERFINFO_AUTO_START_L3(__FUNCTION__, 1);
		CHECK_DYNPOS(vInput);
		copyVec3(vInput, pDynNode->vPos_DNODE);
	PERFINFO_AUTO_STOP_L3();
}

__forceinline void dynNodeSetRotInline(DynNode* pDynNode, const Quat qInput)
{
	PERFINFO_AUTO_START_L3(__FUNCTION__, 1);
		copyQuat(qInput, pDynNode->qRot_DNODE);
	PERFINFO_AUTO_STOP_L3();
}

__forceinline void dynNodeSetScaleInline(DynNode* pDynNode, const Vec3 vInput)
{
	PERFINFO_AUTO_START_L3(__FUNCTION__, 1);
		copyVec3(vInput, pDynNode->vScale_DNODE);
	PERFINFO_AUTO_STOP_L3();
}

__forceinline void dynNodeGetLocalTransformInline(const DynNode* pNode, DynTransform* pTransform)
{
	memcpy(pTransform, &pNode->qRot_DNODE, sizeof(DynTransform));
}

__forceinline void dynNodeSetFromTransformInline(DynNode* pNode, const DynTransform* pTransform)
{
	CHECK_DYNPOS(pTransform->vPos);
	memcpy(&pNode->qRot_DNODE, pTransform, sizeof(DynTransform));
}

__forceinline void dynTransformClearInline(DynTransform* pTransform)
{
	zeroVec3(pTransform->vPos);
	unitQuat(pTransform->qRot);
	copyVec3(onevec3, pTransform->vScale);
}

__forceinline void dynNodeSetFlagsInline(DynNode* pDynNode, U8 uiNewFlags)
{
	pDynNode->uiTransformFlags = uiNewFlags;
}

__forceinline void dynTransformSetFromBoneLocalPosInline(DynTransform* pTransform, const DynNode* pNode)
{
	memcpy(pTransform, &pNode->qRot_DNODE, sizeof(DynTransform));
}

__forceinline void dynTransformInterp(F32 fInterpParam, const DynTransform* pA, const DynTransform* pB, DynTransform* pResult)
{
	Quat qTemp;
	quatInterp(fInterpParam, pA->qRot, pB->qRot, qTemp);
	copyQuat(qTemp, pResult->qRot);
	interpVec3(fInterpParam, pA->vPos, pB->vPos, pResult->vPos); 
	interpVec3(fInterpParam, pA->vScale, pB->vScale, pResult->vScale); 
}

__forceinline void dynTransformMultiply(const DynTransform* pA, const DynTransform* pB, DynTransform* pResult)
{
	quatMultiplyInline(pA->qRot, pB->qRot, pResult->qRot);
	addVec3(pA->vPos, pB->vPos, pResult->vPos);
	mulVecVec3(pA->vScale, pB->vScale, pResult->vScale);
}


__forceinline void dynTransformToMat4Unscaled(const DynTransform* pA, Mat4 mat)
{
	quatToMat(pA->qRot, mat);
	copyVec3(pA->vPos, mat[3]);

}

__forceinline void dynTransformToMat4(const DynTransform* pA, Mat4 mat)
{
	quatToMat(pA->qRot, mat);
	copyVec3(pA->vPos, mat[3]);

	scaleMat3Vec3(mat, pA->vScale);
}

__forceinline void dynTransformInverse(const DynTransform* pA, DynTransform* pResult)
{
	quatInverse(pA->qRot, pResult->qRot);
	scaleVec3(pA->vPos, -1.0f, pResult->vPos);
	pResult->vScale[0] = 1.0f / pA->vScale[0];
	pResult->vScale[1] = 1.0f / pA->vScale[1];
	pResult->vScale[2] = 1.0f / pA->vScale[2];
}

__forceinline void dynTransformApplyToVec3(const DynTransform* pTransform, const Vec3 vIn, Vec3 vOut)
{
	Vec3 vTemp;
	mulVecVec3(pTransform->vScale, vIn, vTemp);
	quatRotateVec3Inline(pTransform->qRot, vTemp, vOut);
	addVec3(vOut, pTransform->vPos, vOut);
}

__forceinline void dynTransformCopy(const DynTransform* pSrc, DynTransform* pDst)
{
	memcpy(pDst, pSrc, sizeof(DynTransform));
}

// This is intended only for animation: it ignores dirty bits, it ignores flags, it assumes a parent and it only does one calculation
__forceinline void dynNodeCalcLocalSpacePosFromWorldSpacePos(DynNode* pNode, Vec3 vPosInOut)
{
	Vec3 v1, v2;
	Quat qInv;

	subVec3(vPosInOut, pNode->pParent->vWorldSpacePos_DNODE, v1);
	if (pNode->uiTransformFlags & ednLocalRot) {
		quatInverse(pNode->qWorldSpaceRot_DNODE, qInv);
	} else {
		quatInverse(pNode->pParent->qWorldSpaceRot_DNODE, qInv);
	}
	quatRotateVec3Inline(qInv, v1, v2);
	vPosInOut[0] = v2[0]/pNode->pParent->vWorldSpaceScale_DNODE[0];
	vPosInOut[1] = v2[1]/pNode->pParent->vWorldSpaceScale_DNODE[1];
	vPosInOut[2] = v2[2]/pNode->pParent->vWorldSpaceScale_DNODE[2];

	/*
	if (pNode->pParent)
	{
		Vec3 v1, v2;
		Quat qInv;

		if (pNode->uiTransformFlags & ednTrans) {
			subVec3(vPosInOut, pNode->pParent->vWorldSpacePos_DNODE, v1);
		} else {
			copyVec3(vPosInOut, v1);
		}

		if (pNode->uiTransformFlags & ednLocalRot) {
			quatInverse(pNode->qWorldSpaceRot_DNODE, qInv);
		} else if (pNode->uiTransformFlags & ednRot) {
			quatInverse(pNode->pParent->qWorldSpaceRot_DNODE, qInv);
		} else {
			unitQuat(qInv);
		}
		quatRotateVec3Inline(qInv, v1, v2);

		if (pNode->uiTransformFlags & ednScale) {
			vPosInOut[0] = fabs(pNode->pParent->vWorldSpaceScale_DNODE[0] > 0.0f) ? v2[0]/pNode->pParent->vWorldSpaceScale_DNODE[0] : 0.0f;
			vPosInOut[1] = fabs(pNode->pParent->vWorldSpaceScale_DNODE[1] > 0.0f) ? v2[1]/pNode->pParent->vWorldSpaceScale_DNODE[1] : 0.0f;
			vPosInOut[2] = fabs(pNode->pParent->vWorldSpaceScale_DNODE[2] > 0.0f) ? v2[2]/pNode->pParent->vWorldSpaceScale_DNODE[2] : 0.0f;
		} else {
			copyVec3(v2, vPosInOut);
		}
	}
	*/
}

__forceinline void dynNodeTreeForceRecalcuation(DynNode* pDynNode)
{
	DynNode* pNodes[200];
	int count = 1;
	int first = 1;

	PERFINFO_AUTO_START_L3(__FUNCTION__,1);
	pNodes[0] = pDynNode;
	while(count)
	{
		pDynNode = pNodes[--count];
		
		if(!first){
			if(pDynNode->pSibling){
				assert(count < ARRAY_SIZE(pNodes));
			
				pNodes[count++] = pDynNode->pSibling;
			}
		}else{
			first = 0;
		}

		{
			if (!pDynNode->pParent)
				dynNodeCleanDirtyBits(pDynNode);
			else if (pDynNode->uiCriticalBone)
				dynNodeCalcWorldSpaceOneNode(pDynNode);
			else
				dynNodeSetDirtyInline(pDynNode);


			if(pDynNode->pChild){
				assert(count < ARRAY_SIZE(pNodes));
			
				pNodes[count++] = pDynNode->pChild;
			}
		}
	}
	PERFINFO_AUTO_STOP_L3();
}

#pragma pop_macro("vWorldSpacePos_DNODE")
#pragma pop_macro("qWorldSpaceRot_DNODE")
#pragma pop_macro("vWorldSpaceScale_DNODE")

#pragma pop_macro("vPos_DNODE")
#pragma pop_macro("qRot_DNODE")
#pragma pop_macro("vScale_DNODE")
