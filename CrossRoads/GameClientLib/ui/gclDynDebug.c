#include "gclDynDebug.h"
#include "dynFxManager.h"
#include "dynSkeleton.h"
#include "dynAnimInterface.h"
#include "Entity.h"
#include "ClientTargeting.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


static void gclDynSkeletonDebug(void)
{
	if (!dynDebugState.bLockDebugSkeleton && ( dynDebugState.cloth.bDrawCollision || dynDebugState.bDrawSkeleton || dynDebugState.bDrawRagdollDataGfx || dynDebugState.bDrawRagdollDataAnim || dynDebugState.bPrintBoneUnderMouse || dynDebugState.danimShowBits || dynDebugState.bDrawCollisionExtents || dynDebugState.bDrawVisibilityExtents || dynDebugState.audioShowAnimBits || dynDebugState.audioShowSkeletonFiles ))
	{
		Entity* pEnt = getEntityUnderMouse(false);
		DynSkeleton* pSkel = pEnt?dynSkeletonFromGuid(pEnt->dyn.guidSkeleton):NULL;
		if (pSkel)
			dynDebugStateSetSkeleton(pSkel);
	}

	if (dynDebugState.bPrintBoneUnderMouse)
	{
		if (!dynDebugState.bLockDebugSkeleton)
		{
			Entity* pEnt = getEntityUnderMouse(false);
			DynSkeleton* pSkel = pEnt?dynSkeletonFromGuid(pEnt->dyn.guidSkeleton):NULL;
			dynDebugState.pBoneUnderMouse = NULL;
			if (pSkel)
			{
				Vec3 vCursorStart, vCursorDir, vCursorEnd;
				const DynNode* pNewNode;
				target_GetCursorRay(NULL, vCursorStart, vCursorDir);
				scaleAddVec3(vCursorDir,500.0f,vCursorStart,vCursorEnd);
				pNewNode = dynSkeletonGetClosestBoneToLineSegment(pSkel, vCursorStart, vCursorEnd, NULL, NULL);
				if (pNewNode)
					dynDebugState.pBoneUnderMouse = pNewNode;
			}
		}
	}
}

void gclDynDebugOncePerFrame(void)
{
	PerfInfoGuard* piGuard;
	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);
	gclDynSkeletonDebug();
	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}

AUTO_CMD_INT(dynDebugState.bPrintBoneUnderMouse, danimShowBoneUnderMouse);

// Lock which skeleton debug information comes up for
AUTO_CMD_INT(dynDebugState.bLockDebugSkeleton, danimLockDebugSkeleton);


#include "rand.h"
#include "GfxConsole.h"
const F32 fQuatTestEpsilon = 0.01f;
AUTO_COMMAND;
void quatTest(int iNumTimes)
{
	int i;
	int badcount = 0;
	for (i=0; i<iNumTimes; ++i)
	{
		Vec3 vPYR, vPYR2;
		Quat q1, q2;
		Mat3 mat;
		Vec3 vError;
		int j;
		for (j=0; j<3; ++j)
			vPYR[j] = randomF32() * PI;
		createMat3YPR(mat, vPYR);
		mat3ToQuat(mat, q2);
		PYRToQuat(vPYR, q1);
		quatToPYR(q2, vPYR2);
		/*
		quatForceWPositive(q1);
		quatForceWPositive(q2);
		for (j=0; j<4; ++j)
		{
			if (fabsf(q1[j] - q2[j]) > fQuatTestEpsilon)
			{
				++badcount;
				break;
			}
		}
		*/
		for (j=0; j<3; ++j)
			vError[j] = fabsf(vPYR2[j] - vPYR[j]);
		for (j=0; j<3; ++j)
		{
			if (fabsf(vPYR2[j] - vPYR[j]) > fQuatTestEpsilon)
			{
				++badcount;
				break;
			}
		}
	}

	conPrintf ("%d / %d Quats (%.2f Percent) have errors", badcount, iNumTimes, 100.0f * (F32)badcount / (F32)iNumTimes);
}
