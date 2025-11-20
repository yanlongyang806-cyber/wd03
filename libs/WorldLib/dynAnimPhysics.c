#include "dynAnimPhysics.h"

#include "MemoryPool.h"
#include "StringCache.h"

#include "dynAnimPhysInfo.h"
#include "dynNode.h"
#include "dynNodeInline.h"
#include "dynSkeleton.h"
#include "dynFxPhysics.h"
#include "rand.h"
#include "wlState.h"
#include "AutoGen/WorldLib_autogen_QueuedFuncs.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

void dynAnimPhysicsCreateObject(DynSkeleton* pSkeleton)
{
	dynPhysicsObjectCreate(&pSkeleton->pDPO);
	
	pSkeleton->pDPO->dpoType = DPO_SKEL;
	pSkeleton->pDPO->skel.pSkeleton = pSkeleton;

	//wcoCreate(	&pSkeleton->pWCO,
	//	worldGetActiveCollRoamingCell(),
	//	NULL,
	//	pSkeleton,
	//	WG_WCO_DYNSKELETON_OBJECT,
	//	zerovec3,
	//	zerovec3,
	//	1,
	//	0);
}

bool dynAnimPhysicsIsFullSimulation(DynSkeleton *pSkeleton)
{
	return 	pSkeleton &&
			pSkeleton->pDPO &&
			pSkeleton->pDPO->wcActor
			?
			1 : 0;
}

void dynAnimPhysicsFreeObject(DynSkeleton* pSkeleton)
{
	assert(pSkeleton->pDPO->dpoType == DPO_SKEL);
	pSkeleton->pDPO->skel.pSkeleton = NULL;
	
	if(pSkeleton->pDPO)
	{
		QueuedCommand_dpoDestroy(pSkeleton->pDPO);
		pSkeleton->pDPO = NULL;
	}
}

void dynAnimPhysicsCreateRagdollBody(DynSkeleton *pSkeleton, DynClientSideRagdollBody **pRagdollBody)
{
	DynPhysicsObject *pDPO = NULL;
	*pRagdollBody = callocStruct(DynClientSideRagdollBody);
	dynPhysicsObjectCreate(&pDPO);
	pDPO->dpoType = DPO_RAGDOLLBODY;
	pDPO->body.pBody = *pRagdollBody;
	eaPush(&pSkeleton->eaClientSideRagdollBodies, pDPO);
}

void dynAnimPhysicsFreeRagdollBodies(DynSkeleton *pSkeleton)
{
	while (eaSize(&pSkeleton->eaClientSideRagdollBodies))
	{
		DynPhysicsObject *pDPO = NULL;
		pDPO = eaRemoveFast(&pSkeleton->eaClientSideRagdollBodies,0);
		assert(pDPO->dpoType == DPO_RAGDOLLBODY);
		if (pDPO)
			QueuedCommand_dpoDestroy(pDPO);
	}
	eaDestroy(&pSkeleton->eaClientSideRagdollBodies);
}

MP_DEFINE(DynBouncerUpdater);

AUTO_RUN;
void dynAnimPhysicsAutoRun(void)
{
	MP_CREATE(DynBouncerUpdater, 128);
}

DynBouncerUpdater* dynBouncerUpdaterCreate(DynBouncerInfo* pInfo)
{
	DynBouncerUpdater* pUpdater = MP_ALLOC(DynBouncerUpdater);
	pUpdater->pInfo = pInfo;
	pUpdater->bNeedsInit = true;
	pUpdater->bReprocessedSkeleton = false;
	return pUpdater;
}

void dynBouncerUpdaterDestroy(DynBouncerUpdater* pUpdater)
{
	MP_FREE(DynBouncerUpdater, pUpdater);
}


#define accelerate(x,v, s, d) (-s * x - d*v)

void evaluate(F32 fX, F32 fV, F32 fDX, F32 fDV, F32* pfDX, F32* pfDV, F32 fSpring, F32 fDamper, F32 fDeltaTime)
{
	F32 fIntegratedX = fX + (fDX) * fDeltaTime;
	F32 fIntegratedV = fV + (fDV) * fDeltaTime;

	*pfDX = fIntegratedV;
	*pfDV = accelerate(fIntegratedX, fIntegratedV, fSpring, fDamper);
}


void integrateRK4(F32* pfX, F32* pfV, F32 fSpring, F32 fDamper, F32 fDeltaTime)
{
	F32 fDX[4] = {0};
	F32 fDV[4] = {0};
	evaluate(*pfX, *pfV, 0.0f, 0.0f, &fDX[0], &fDV[0], fSpring, fDamper, 0.0f);
	evaluate(*pfX, *pfV, fDX[0], fDV[0], &fDX[1], &fDV[1], fSpring, fDamper, fDeltaTime * 0.5f);
	evaluate(*pfX, *pfV, fDX[1], fDV[1], &fDX[2], &fDV[2], fSpring, fDamper, fDeltaTime * 0.5f);
	evaluate(*pfX, *pfV, fDX[2], fDV[2], &fDX[3], &fDV[3], fSpring, fDamper, fDeltaTime);

	{
		F32 fDXDT = (fDX[0] + 2.0f*(fDX[1] + fDX[2]) + fDX[3]) / 6.0f;
		F32 fDVDT = (fDV[0] + 2.0f*(fDV[1] + fDV[2]) + fDV[3]) / 6.0f;

		*pfX += fDXDT * fDeltaTime;
		*pfV += fDVDT * fDeltaTime;
	}
}

void dynBouncerUpdaterUpdateBone(DynBouncerUpdater* pUpdater, DynNode* pBone, F32 fDeltaTime)
{
	F32 fDeltaTimeSafe = CLAMP(fDeltaTime, 0.0f, 0.05f);
	Vec3 vMassOffset;
	Vec3 vMassPosWS;
	Vec3 vMassPosDeltaWS;
	Vec3 vMassPosDeltaLS;
	DynTransform xCurrentWS;
	DynTransform xSpringWS;

	// Calculate where a weight 0.2 units along the forward axis would be in WS
	vMassOffset[0] = vMassOffset[1] = 0.0f;
	vMassOffset[2] = 0.0f;
	dynNodeGetWorldSpaceTransform(pBone, &xCurrentWS);
	dynTransformApplyToVec3(&xCurrentWS, vMassOffset, vMassPosWS);
	subVec3(vMassPosWS, pUpdater->vMassPosWS, vMassPosDeltaWS);
	copyVec3(vMassPosWS, pUpdater->vMassPosWS);

	// Transform this into the spring system's local space
	{
		Quat qSpringWSInv;
		dynTransformCopy(&xCurrentWS, &xSpringWS);
		quatMultiply(pUpdater->pInfo->qRot, xCurrentWS.qRot, xSpringWS.qRot);
		quatInverse(xSpringWS.qRot, qSpringWSInv);
		quatRotateVec3Inline(qSpringWSInv, vMassPosDeltaWS, vMassPosDeltaLS);
	}

	if (dynDebugState.bDrawBouncers && wl_state.drawAxesFromTransform_func)
	{
		wl_state.drawAxesFromTransform_func(&xSpringWS, 1.0f);
		if (wl_state.drawLine3D_2_func)
		{
			Vec3 vOffset;
			scaleAddVec3(vMassPosDeltaWS, 10.0f, xCurrentWS.vPos, vOffset);
			wl_state.drawLine3D_2_func(xCurrentWS.vPos, vOffset, 0xFFFF00FF, 0xFFFF00FF);

		}
	}


	switch (pUpdater->pInfo->eType)
	{
		xcase eBouncerType_Linear:
		case eBouncerType_Linear2:
		{
			Vec2 vImpulse;
			//Vec2 vAccel;
			int i;
			int iNumDOF = (pUpdater->pInfo->eType==eBouncerType_Linear2)?2:1;
			// Convert change into impulse
			vImpulse[0] = -vMassPosDeltaLS[0] * 2.0f;
			if (iNumDOF>1)
				vImpulse[1] = -vMassPosDeltaLS[1] * 2.0f;

			for (i=0; i<iNumDOF; ++i)
			{
				// Apply impulse
				pUpdater->state.rotational.vVel[i] += vImpulse[i];
				// Integrate
				integrateRK4(&pUpdater->state.rotational.vPos[i], &pUpdater->state.rotational.vVel[i], pUpdater->pInfo->fSpring, pUpdater->pInfo->fDampRate, fDeltaTimeSafe);
				/*
				vAccel[i] = -pUpdater->pInfo->fSpring * pUpdater->state.rotational.vPos[i];
				pUpdater->state.rotational.vVel[i] += vAccel[i] * fDeltaTimeSafe;
				pUpdater->state.rotational.vVel[i] *= (1.0f - pUpdater->pInfo->fDampRate);
				pUpdater->state.rotational.vPos[i] += pUpdater->state.rotational.vVel[i] * fDeltaTimeSafe;
				*/
				pUpdater->state.rotational.vPos[i] = CLAMP(pUpdater->state.rotational.vPos[i], -pUpdater->pInfo->fMaxDist, pUpdater->pInfo->fMaxDist);
			}


			// Apply state transform to bone
			{
				Vec3 vPos, vResult;
				Quat qResult;
				zeroVec3(vPos);
				for (i=0; i<iNumDOF; ++i)
					vPos[i] = pUpdater->state.rotational.vPos[i];
				quatMultiply(dynNodeGetLocalRotRefInline(pBone), pUpdater->pInfo->qRot, qResult);
				quatRotateVec3Inline(qResult, vPos, vResult);
				addVec3(dynNodeGetLocalPosRefInline(pBone), vResult, vResult);
				dynNodeSetPosInline(pBone, vResult);
				dynNodeCalcWorldSpaceOneNode(pBone);
				if (dynDebugState.bDrawBouncers && wl_state.drawLine3D_2_func)
				{
					Vec3 vOffset;
					quatRotateVec3Inline(xCurrentWS.qRot, forwardvec, vOffset);
					scaleAddVec3(vOffset, 2.0f, dynNodeAssumeCleanGetWorldSpacePosRefInline(pBone), vOffset);
					wl_state.drawLine3D_2_func(dynNodeAssumeCleanGetWorldSpacePosRefInline(pBone), vOffset, 0xFFFFFFFF, 0xFFFFFFFF);
				}
			}
		}
		xcase eBouncerType_Hinge:
		case eBouncerType_Hinge2:
		{
			Vec2 vImpulse;
			F32 fMaxDist = RAD(pUpdater->pInfo->fMaxDist);
			//Vec2 vAccel;
			int i;
			int iNumDOF = (pUpdater->pInfo->eType==eBouncerType_Hinge2)?2:1;
			// Convert change into impulse
			scaleVec3(vMassPosDeltaLS, -1.0f, vMassPosDeltaLS);
			vMassPosDeltaLS[2] += 0.2f;
			vImpulse[0] = getVec3Pitch(vMassPosDeltaLS);
			if (iNumDOF>1)
				vImpulse[1] = getVec3Yaw(vMassPosDeltaLS);

			for (i=0; i<iNumDOF; ++i)
			{
				// Apply impulse
				pUpdater->state.rotational.vVel[i] += vImpulse[i];
				// Integrate
				integrateRK4(&pUpdater->state.rotational.vPos[i], &pUpdater->state.rotational.vVel[i], pUpdater->pInfo->fSpring, pUpdater->pInfo->fDampRate, fDeltaTimeSafe);
				/*
				vAccel[i] = -pUpdater->pInfo->fSpring * pUpdater->state.rotational.vPos[i];
				pUpdater->state.rotational.vVel[i] += vAccel[i] * fDeltaTimeSafe;
				pUpdater->state.rotational.vVel[i] *= (1.0f - pUpdater->pInfo->fDampRate);
				pUpdater->state.rotational.vPos[i] += pUpdater->state.rotational.vVel[i] * fDeltaTimeSafe;
				*/
				if (fabsf(pUpdater->state.rotational.vPos[i]) > fMaxDist)
				{
					pUpdater->state.rotational.vPos[i] = SIGN(pUpdater->state.rotational.vPos[i]) * fMaxDist;
					pUpdater->state.rotational.vVel[i] = 0.0f;
				}
			}


			// Apply state transform to bone
			{
				Vec3 vPYR;
				Quat qSpring, qResult, qSpring2;
				Quat qSpringRotInv;
				zeroVec3(vPYR);
				for (i=0; i<iNumDOF; ++i)
					vPYR[i] = pUpdater->state.rotational.vPos[i];
				PYRToQuat(vPYR, qSpring);
				quatInverse(pUpdater->pInfo->qRot, qSpringRotInv);
				quatMultiply(pUpdater->pInfo->qRot, qSpring, qSpring2);
				quatMultiply(dynNodeGetLocalRotRefInline(pBone), qSpring2, qResult);
				quatMultiply(qResult, qSpringRotInv, qSpring);
				dynNodeSetRotInline(pBone, qSpring);
				dynNodeCalcWorldSpaceOneNode(pBone);
				if (dynDebugState.bDrawBouncers && wl_state.drawLine3D_2_func)
				{
					Vec3 vZOffset, vYOffset;
					quatRotateVec3Inline(dynNodeAssumeCleanGetWorldSpaceRotRefInline(pBone), forwardvec, vZOffset);
					quatRotateVec3Inline(dynNodeAssumeCleanGetWorldSpaceRotRefInline(pBone), upvec, vYOffset);
					scaleAddVec3(vZOffset, 2.0f, dynNodeAssumeCleanGetWorldSpacePosRefInline(pBone), vZOffset);
					scaleAddVec3(vYOffset, 2.0f, dynNodeAssumeCleanGetWorldSpacePosRefInline(pBone), vYOffset);
					wl_state.drawLine3D_2_func(dynNodeAssumeCleanGetWorldSpacePosRefInline(pBone), vZOffset, 0xFFFFFFFF, 0xFFFFFFFF);
					wl_state.drawLine3D_2_func(dynNodeAssumeCleanGetWorldSpacePosRefInline(pBone), vYOffset, 0xFF00FFFF, 0xFF00FFFF);

				}
			}
		}
	}
}

void dynBouncerUpdaterUpdateBoneNew(DynBouncerUpdater* pUpdater, DynNode* pBone, F32 fDeltaTime)
{
	DynSkeleton *pRootSkel = pBone->pSkeleton->pGenesisSkeleton;
	DynTransform xCurrentWS, xRootWS;
	Vec3 vMassPosWS, vRootPosWS;
	Vec3 vOverallScaleWS;
	F32 fScaleMaxDist;

	//grab the bones world space data
	dynNodeGetWorldSpaceTransform(pBone, &xCurrentWS);
	if (pRootSkel->pRoot->pParent) {
		dynNodeGetWorldSpaceTransform(pRootSkel->pRoot->pParent, &xRootWS);
	} else {
		dynNodeGetWorldSpaceTransform(pRootSkel->pRoot, &xRootWS);
	}
	copyVec3(xCurrentWS.vPos,vMassPosWS);
	copyVec3(xRootWS.vPos, vRootPosWS);

	//determine the amount to scale the max displacement distance by
	dynNodeGetWorldSpaceScale(pBone->pSkeleton->pRoot, vOverallScaleWS);
	fScaleMaxDist = MIN(vOverallScaleWS[0],MIN(vOverallScaleWS[1],vOverallScaleWS[2]));

	if (pUpdater->bNeedsInit)
	{
		//setup the initial values based on our 1st frame of animation
		pUpdater->fLODMissedTime = 0.f;
		copyVec3(vMassPosWS, pUpdater->vMassPosWS);
		copyVec3(vRootPosWS, pUpdater->vRootPosWS);
		zeroVec2(pUpdater->state.rotational.vPos);
		zeroVec2(pUpdater->state.rotational.vVel);

		//display debug graphics
		if (dynDebugState.bDrawBouncers && wl_state.drawAxesFromTransform_func)
		{
			DynTransform xSpringWS;
			dynTransformCopy(&xCurrentWS, &xSpringWS);
			quatMultiply(pUpdater->pInfo->qRot, xCurrentWS.qRot, xSpringWS.qRot);
			wl_state.drawAxesFromTransform_func(&xSpringWS, 1.0f);
		}

		//mark initialization as completed
		pUpdater->bNeedsInit = false;
	}
	else if (!pBone->uiUpdatedThisAnim)
	{
		//Normally a bone at this stage should only have transforms as computed by the animation data (no dynamic behavior).
		//It is possible, however, to get here without that being the case due to LOD striding over the bones
		//when that happens we'll keep track of the skipped time, and use it during the next update that has been setup correctly
		//(this relies on the LOD striding to never skip a nodes ancestors when updating)
		pUpdater->fLODMissedTime += fDeltaTime;
	}
	else
	{
		DynTransform xSpringWS;
		Quat qSpringWSInv;
		Vec3 vMassPosDeltaWS;
		Vec3 vMassPosDeltaLS;
		F32 fDeltaTimeSafe = CLAMP(fDeltaTime + pUpdater->fLODMissedTime, 0.0f, 0.05f);

		//determine how far the bone has moved and update the previous position
		if (pUpdater->bReprocessedSkeleton ||
			lengthVec3Squared(pUpdater->vRootPosWS) < 0.01f)
		{
			zeroVec3(vMassPosDeltaWS);
			pUpdater->bReprocessedSkeleton = false;
		} else {
			subVec3(vMassPosWS, pUpdater->vMassPosWS, vMassPosDeltaWS);
			vMassPosDeltaWS[0] = CLAMP(vMassPosDeltaWS[0], -2.5f*fScaleMaxDist*pUpdater->pInfo->fMaxDist, 2.5f*fScaleMaxDist*pUpdater->pInfo->fMaxDist);
			vMassPosDeltaWS[1] = CLAMP(vMassPosDeltaWS[1], -2.5f*fScaleMaxDist*pUpdater->pInfo->fMaxDist, 2.5f*fScaleMaxDist*pUpdater->pInfo->fMaxDist);
			vMassPosDeltaWS[2] = CLAMP(vMassPosDeltaWS[2], -2.5f*fScaleMaxDist*pUpdater->pInfo->fMaxDist, 2.5f*fScaleMaxDist*pUpdater->pInfo->fMaxDist);
		}

		copyVec3(vMassPosWS, pUpdater->vMassPosWS);
		copyVec3(vRootPosWS, pUpdater->vRootPosWS);

		//transform this into the spring system's local space
		dynTransformCopy(&xCurrentWS, &xSpringWS);
		quatMultiply(pUpdater->pInfo->qRot, xCurrentWS.qRot, xSpringWS.qRot);
		quatInverse(xSpringWS.qRot, qSpringWSInv);
		quatRotateVec3Inline(qSpringWSInv, vMassPosDeltaWS, vMassPosDeltaLS);

		//update the bouncer transforms
		switch (pUpdater->pInfo->eType)
		{
			xcase eBouncerType_Linear:
			case eBouncerType_Linear2:
			{
				Vec2 vImpulse;
				int iNumDOF = (pUpdater->pInfo->eType==eBouncerType_Linear2)?2:1;
				int i;

				//convert the change in position into an impulse
				vImpulse[0] = -vMassPosDeltaLS[0] * 2.0f;
				if (iNumDOF > 1) {
					vImpulse[1] = -vMassPosDeltaLS[1] * 2.0f;
				}

				for (i = 0; i < iNumDOF; ++i)
				{
					//apply impulse
					pUpdater->state.rotational.vVel[i] += vImpulse[i];

					//integrate
					integrateRK4(&pUpdater->state.rotational.vPos[i], &pUpdater->state.rotational.vVel[i], pUpdater->pInfo->fSpring, pUpdater->pInfo->fDampRate, fDeltaTimeSafe);
				
					//clamp result
					pUpdater->state.rotational.vPos[i] = CLAMP(pUpdater->state.rotational.vPos[i], -fScaleMaxDist*pUpdater->pInfo->fMaxDist, fScaleMaxDist*pUpdater->pInfo->fMaxDist);
				}

				//apply result to bone
				{
					Vec3 vPos, vResult;
					Quat qResult;

					//convert position data from 2D to 3D
					zeroVec3(vPos);
					for (i = 0; i < iNumDOF; ++i) {
						vPos[i] = pUpdater->state.rotational.vPos[i];
					}
				
					//compute new transform
					quatMultiply(dynNodeGetLocalRotRefInline(pBone), pUpdater->pInfo->qRot, qResult);
					quatRotateVec3Inline(qResult, vPos, vResult);
					addVec3(dynNodeGetLocalPosRefInline(pBone), vResult, vResult);
					dynNodeSetPosInline(pBone, vResult);
					dynNodeCalcWorldSpaceOneNode(pBone);

					//display debug graphics
					if (dynDebugState.bDrawBouncers && wl_state.drawLine3D_2_func)
					{
						Vec3 vOffset;
						quatRotateVec3Inline(xCurrentWS.qRot, forwardvec, vOffset);
						scaleAddVec3(vOffset, 2.0f, dynNodeAssumeCleanGetWorldSpacePosRefInline(pBone), vOffset);
						wl_state.drawLine3D_2_func(dynNodeAssumeCleanGetWorldSpacePosRefInline(pBone), vOffset, 0xFFFFFFFF, 0xFFFFFFFF);
					}
				}
			}

			xcase eBouncerType_Hinge:
			case eBouncerType_Hinge2:
			{
				Vec2 vImpulse;
				F32 fMaxDist = RAD(pUpdater->pInfo->fMaxDist);
				int	iNumDOF = (pUpdater->pInfo->eType==eBouncerType_Hinge2)?2:1;
				int i;

				//convert the change in position into an impulse
				scaleVec3(vMassPosDeltaLS, -1.0f, vMassPosDeltaLS);
				vMassPosDeltaLS[2] += 0.2f;
				vImpulse[0] = getVec3Pitch(vMassPosDeltaLS);
				if (iNumDOF > 1) {
					vImpulse[1] = getVec3Yaw(vMassPosDeltaLS);
				}

				for (i = 0; i < iNumDOF; ++i)
				{
					//apply impulse
					pUpdater->state.rotational.vVel[i] += vImpulse[i];

					//integrate
					integrateRK4(&pUpdater->state.rotational.vPos[i], &pUpdater->state.rotational.vVel[i], pUpdater->pInfo->fSpring, pUpdater->pInfo->fDampRate, fDeltaTimeSafe);
				
					//clamp result
					if (fabsf(pUpdater->state.rotational.vPos[i]) > fMaxDist)
					{
						pUpdater->state.rotational.vPos[i] = SIGN(pUpdater->state.rotational.vPos[i]) * fMaxDist;
						pUpdater->state.rotational.vVel[i] = 0.0f;
					}
				}

				//apply result to bone
				{
					Vec3 vPYR;
					Quat qSpring, qResult, qSpring2;
					Quat qSpringRotInv;

					//convert position data from 2D to 3D
					zeroVec3(vPYR);
					for (i = 0; i < iNumDOF; ++i) {
						vPYR[i] = pUpdater->state.rotational.vPos[i];
					}

					//compute new transform
					PYRToQuat(vPYR, qSpring);
					quatInverse(pUpdater->pInfo->qRot, qSpringRotInv);
					quatMultiply(pUpdater->pInfo->qRot, qSpring, qSpring2);
					quatMultiply(dynNodeGetLocalRotRefInline(pBone), qSpring2, qResult);
					quatMultiply(qResult, qSpringRotInv, qSpring);
					dynNodeSetRotInline(pBone, qSpring);
					dynNodeCalcWorldSpaceOneNode(pBone);

					//display debug graphics
					if (dynDebugState.bDrawBouncers && wl_state.drawLine3D_2_func)
					{
						Vec3 vZOffset, vYOffset;
						quatRotateVec3Inline(dynNodeAssumeCleanGetWorldSpaceRotRefInline(pBone), forwardvec, vZOffset);
						quatRotateVec3Inline(dynNodeAssumeCleanGetWorldSpaceRotRefInline(pBone), upvec, vYOffset);
						scaleAddVec3(vZOffset, 2.0f, dynNodeAssumeCleanGetWorldSpacePosRefInline(pBone), vZOffset);
						scaleAddVec3(vYOffset, 2.0f, dynNodeAssumeCleanGetWorldSpacePosRefInline(pBone), vYOffset);
						wl_state.drawLine3D_2_func(dynNodeAssumeCleanGetWorldSpacePosRefInline(pBone), vZOffset, 0xFFFFFFFF, 0xFFFFFFFF);
						wl_state.drawLine3D_2_func(dynNodeAssumeCleanGetWorldSpacePosRefInline(pBone), vYOffset, 0xFF00FFFF, 0xFF00FFFF);
					}
				}
			}
		}
	}
}


const char* pcHitTop;
const char* pcHitBottom;
const char* pcHitLeft;
const char* pcHitRight;
const char* pcHitFront;
const char* pcHitRear;

AUTO_RUN;
void dynAnimPhysics_InitStrings(void)
{
	if (gConf.bNewAnimationSystem) {
		//using non-static strings so the string cache won't error
		//about upper/lower case letters with the anim bit registry version
		pcHitTop    = allocAddString("HitTop");
		pcHitBottom = allocAddString("HitBottom");
		pcHitLeft   = allocAddString("HitLeft");
		pcHitRight  = allocAddString("HitRight");
		pcHitFront  = allocAddString("HitFront");
		pcHitRear   = allocAddString("HitRear");
	} else {
		pcHitTop   = allocAddStaticString("Hit_Top");
		pcHitLeft  = allocAddStaticString("Hit_Left");
		pcHitRight = allocAddStaticString("Hit_Right");
		pcHitFront = allocAddStaticString("Hit_Front");
		pcHitRear  = allocAddStaticString("Hit_Rear");
	}
}

const char* dynCalculateHitReactDirectionBit(const Mat3 mFaceSpace, const Vec3 vDirection)
{
	F32 fDotUp, fDotForward, fDotRight;
	fDotUp = dotVec3(mFaceSpace[1], vDirection);
	if (fabsf(fDotUp) > 0.8f)
	{
		if (fDotUp < 0.0)
			return pcHitTop;
		else if (gConf.bNewAnimationSystem)
			return pcHitBottom;
	}
	fDotForward = dotVec3(mFaceSpace[2], vDirection);
	fDotRight = dotVec3(mFaceSpace[0], vDirection);
	if (fabsf(fDotForward) > fabsf(fDotRight))
	{
		if (fDotForward < 0.0f)
			return gConf.bNewAnimationSystem ? pcHitFront : NULL;
		return pcHitRear;
	}
	else
	{
		if (fDotRight < 0.0f)
			return pcHitRight;
		return pcHitLeft;
	}
	return NULL;
}

static F32 getVec3Angle(const Vec3 a, const Vec3 b)
{
	// Solve for THETA -->  X . Y = |X| |Y| cos(theta)
	F32 val = dotVec3(a, b) / ( lengthVec3(a) * lengthVec3(b));
	return acosf( MINMAX(val, -1, 1) );
}

static int iPrintDebugInfo = 0;

void dynAnimFindTargetTransform(
	const DynSkeleton *pSkeleton, const DynScaleCollection* pScaleCollection, const DynBaseSkeleton* pRegSkeleton, const DynNode* pHand,
	const DynNode *pIKTargetA, const DynNode *pIKTargetB, U32 uiNumIKTargets, DynTransform* pxTarget, F32 fBlend,
	bool bIKBothHands, bool bIsLeftHand,
	bool bIKMeleeMode, bool bEnableIKSliding
	)
{
	//case for targeting both hands or a weapon node
	if (uiNumIKTargets == 1 && pIKTargetA ||
		uiNumIKTargets >  1 && pIKTargetA && pIKTargetB)
	{
		DynTransform xIKTarget;
		Vec3 Shaft;

		if (uiNumIKTargets > 1 && bIKBothHands)
		{
			if (bIsLeftHand) {
				dynNodeGetWorldSpaceTransform(pIKTargetB, &xIKTarget);
			} else {
				dynNodeGetWorldSpaceTransform(pIKTargetA, &xIKTarget);
			}
		}
		else if (uiNumIKTargets == 1 || !bIKMeleeMode)
		{
			//single grip target
			dynNodeGetWorldSpaceTransform(pIKTargetA, &xIKTarget);
		}
		else
		{
			//sliding attachment constraint target
			DynTransform xIKTargetA, xIKTargetB, xHand;
			DynTransform *xIKTargetTop, *xIKTargetBottom;
			Vec3 BoneToHand;
			F32 shaftBlend;
			
			//look up the nodes positions in world space
			dynNodeGetWorldSpaceTransform(pIKTargetA, &xIKTargetA);
			dynNodeGetWorldSpaceTransform(pIKTargetB, &xIKTargetB);
			dynNodeGetWorldSpaceTransform(pHand, &xHand);

			//set order
			if (xIKTargetA.vPos[1] > xIKTargetB.vPos[1]) {
				xIKTargetTop    = &xIKTargetA;
				xIKTargetBottom = &xIKTargetB;
			} else {
				xIKTargetTop    = &xIKTargetB;
				xIKTargetBottom = &xIKTargetA;
			}

			//determine relevant vectors
			subVec3(xHand.vPos,              (*xIKTargetTop).vPos, BoneToHand);
			subVec3((*xIKTargetBottom).vPos, (*xIKTargetTop).vPos, Shaft);//points down, should be same direction as characters palm
			
			//compute the blend ratio
			if (bEnableIKSliding) {
				shaftBlend = dotVec3(BoneToHand,Shaft) / dotVec3(Shaft,Shaft);
				shaftBlend = CLAMP(shaftBlend, 0.0, 1.0);
			} else {
				shaftBlend = 1.0;
			}

			//blend to a point
			dynTransformInterp(shaftBlend, xIKTargetTop, xIKTargetBottom, &xIKTarget);

			//set the axis of the weapon for use during IK
			axisAngleToQuat(Shaft, 10.0, xIKTarget.qRot);
		}
		
		if (fBlend < 1.0f)
		{
			DynTransform xHand;
			dynNodeGetWorldSpaceTransform(pHand, &xHand);
			dynTransformInterp(fBlend, &xHand, &xIKTarget, pxTarget);

			//rotation blend for melee weapons with sliding attachment done during IK-solver
			if (uiNumIKTargets > 1 && bIKMeleeMode)
				copyQuat(xIKTarget.qRot, pxTarget->qRot);
		}
		else
			dynTransformCopy(&xIKTarget, pxTarget);
	}
	else //case for targeting the non-scaled skeleton's hand
	{
		const DynNode* aNodes[6];
		const int iNumNodes = ARRAY_SIZE(aNodes);
		DynTransform xRunning;
		int i;
		aNodes[iNumNodes-1] = pHand;
		for (i=(iNumNodes-2); i>=0; --i)
		{
			aNodes[i] = aNodes[i+1]->pParent;
		}

		dynNodeGetWorldSpaceTransform(aNodes[0], &xRunning);

		{
			// Calculate average scale
			Vec3 vScale, vAvgScale;
			zeroVec3(vAvgScale);
			for (i=0; i<iNumNodes; ++i)
			{
				dynNodeGetWorldSpaceScale(aNodes[i], vScale);
				addVec3(vAvgScale, vScale, vAvgScale);
			}
			scaleVec3(vAvgScale, (1.0f / (iNumNodes)), xRunning.vScale);
		}

		if (pRegSkeleton && pScaleCollection)
		{
			for (i=1; i<iNumNodes; ++i)
			{
				DynTransform xNode, xBaseNode, xRegNode;
				DynTransform xBaseInv, xTemp;
				Vec3 vPos;
				Quat qRot;

				dynNodeGetLocalTransformInline(aNodes[i], &xNode);
				dynTransformCopy(dynScaleCollectionFindTransform(pScaleCollection, aNodes[i]->pcTag), &xBaseNode);
				dynNodeGetLocalTransformInline(dynBaseSkeletonFindNode(pRegSkeleton, aNodes[i]->pcTag), &xRegNode);
				dynTransformInverse(&xBaseNode, &xBaseInv);

				dynTransformMultiply(&xBaseInv, &xNode, &xTemp);
				dynTransformMultiply(&xTemp, &xRegNode, &xNode);

				mulVecVec3(xNode.vPos, xRunning.vScale, xNode.vPos);
				quatRotateVec3Inline(xRunning.qRot, xNode.vPos, vPos);
				addVec3(vPos, xRunning.vPos, xRunning.vPos);
				quatMultiplyInline(xNode.qRot, xRunning.qRot, qRot);
				copyQuat(qRot, xRunning.qRot);

				//wl_state.drawAxesFromTransform_func(&xRunning, 0.4f);
			}
		}
		else
		{
			for (i=1; i<iNumNodes; ++i)
			{
				DynTransform xNode;
				Vec3 vPos;
				Quat qRot;
				dynNodeGetLocalTransformInline(aNodes[i], &xNode);
				mulVecVec3(xNode.vPos, xRunning.vScale, xNode.vPos);
				quatRotateVec3Inline(xRunning.qRot, xNode.vPos, vPos);
				addVec3(vPos, xRunning.vPos, xRunning.vPos);
				quatMultiplyInline(xNode.qRot, xRunning.qRot, qRot);
				copyQuat(qRot, xRunning.qRot);
				//wl_state.drawAxesFromTransform_func(&xRunning, 0.4f);

			}
		}

		if (fBlend < 1.0f)
		{
			DynTransform xScaledTarget;
			// Blend!
			dynNodeGetWorldSpaceTransform(pHand, &xScaledTarget);
			dynTransformInterp(fBlend, &xScaledTarget, &xRunning, pxTarget);
		}
		else
			dynTransformCopy(&xRunning, pxTarget);
	}
}

bool dynAnimFixupArm(
	DynNode* pWep, const DynTransform* pxTarget, F32 fWepRegisterBlend,
	bool bIKGroundRegMode, bool bIKBothHandsMode, bool bIKMeleeMode,
	bool bDisableIKLeftWrist
	)
{
	DynNode* pHand = pWep->pParent;
	DynNode* pLArm = pHand->pParent;
	DynNode* pUArm = pLArm->pParent;
	Vec3 vPlaneNormal;
	Vec3 vToElbow;
	DynTransform xTargetHand;

	if (!pxTarget)
		return false;

	if (dynDebugState.bDebugIK ||
		dynDebugState.bDrawIKTargets)
	{
		wl_state.drawAxesFromTransform_func(pxTarget, dynDebugState.bDrawIKTargets ? 3.f : .2f);
	}
	
	//Determine the hand's target transform
	if (bIKBothHandsMode || bIKMeleeMode || bIKGroundRegMode)
	{
		//melee weapon version where the hand's WepL node is to be placed
		//on top of the target and aligned along it OR
		//ground reg version where the joint is to be dropped

		copyVec3(pxTarget->vPos, xTargetHand.vPos);
		copyQuat(pxTarget->qRot, xTargetHand.qRot);
	}
	else
	{
		//gun version that offsets the hands position from the weapon
		//and later on copies the weapon's attachment node's transform back
		//onto the hand.. also used for the case where the hand is moved
		//on top of it's unscaled version as the IK target (WepR's normal
		//behavior)

		DynTransform xLocalWep;
		Quat qInv;
		Vec3 vScaledPos;
		Vec3 vRotatedPos;

		dynNodeGetLocalTransformInline(pWep, &xLocalWep);

		// First, fixup rotations
		quatInverse(xLocalWep.qRot, qInv);
		quatMultiplyInline(qInv, pxTarget->qRot, xTargetHand.qRot);

		// Now, scale
		xTargetHand.vScale[0] = pxTarget->vScale[0] / xLocalWep.vScale[0];
		xTargetHand.vScale[1] = pxTarget->vScale[1] / xLocalWep.vScale[1];
		xTargetHand.vScale[2] = pxTarget->vScale[2] / xLocalWep.vScale[2];

		// Now position, using the scale and rotation
		mulVecVec3(xLocalWep.vPos, xTargetHand.vScale, vScaledPos);
		quatRotateVec3Inline(xTargetHand.qRot, vScaledPos, vRotatedPos);
		subVec3(pxTarget->vPos, vRotatedPos, xTargetHand.vPos);
	}

	{
		Vec3 vHandPos, vLArmPos, vUArmPos;
		Vec3 vUArm, vLArm, vToTarget;
		Vec3 vWepPos, vHand;

		//initial positions (pre-IK)
		dynNodeGetWorldSpacePos(pHand, vHandPos);
		dynNodeGetWorldSpacePos(pLArm, vLArmPos);
		dynNodeGetWorldSpacePos(pUArm, vUArmPos);
		
		//initial vectors (pre-IK)
		subVec3(vLArmPos, vUArmPos, vUArm);
		subVec3(vHandPos, vLArmPos, vLArm);
		subVec3(xTargetHand.vPos, vUArmPos, vToTarget);

		if (dynDebugState.bDebugIK)
		{
			Mat4 mUArm;
			Vec3 vLArmNorm, vUArmX, vUArmY, vUArmZ;
			Vec3 ve1;
			F32 dotValue;
			dynNodeGetWorldSpaceMat(pUArm, mUArm, true);
			copyVec3(mUArm[0], vUArmX); normalVec3(vUArmX);
			copyVec3(mUArm[1], vUArmY); normalVec3(vUArmY);
			copyVec3(mUArm[2], vUArmZ); normalVec3(vUArmZ);
			copyVec3(vLArm, vLArmNorm); normalVec3(vLArmNorm);

			dotValue = dotVec3(vUArmZ, vLArmNorm);

			scaleAddVec3(vUArmZ,    1.5, mUArm[3], ve1);
			wl_state.drawLine3D_2_func(mUArm[3], ve1, (dotValue > 0) ? 0xFFFFFFFF : 0xFF444444, (dotValue > 0) ? 0xFFFFFFFF : 0xFF444444);
		}

		//slight mod to support melee weapons, note that
		//the larm bone now has the hand bone welded on
		if (bIKBothHandsMode || bIKMeleeMode){
			dynNodeGetWorldSpacePos(pWep, vWepPos);
			subVec3(vWepPos, vHandPos, vHand);
			addVec3(vLArm, vHand, vLArm);
		}

		if (dynDebugState.bDebugIK)
		{
			Vec3 vMin, vMax;
			Mat4 mBox;

			vMin[0] = vMin[1] = vMin[2] = -0.01f;
			vMax[0] = vMax[1] = vMax[2] =  0.01f;
			identityMat4(mBox);

			//green = original bones
			wl_state.drawLine3D_2_func(vUArmPos, vLArmPos, 0xFF00FF00, 0xFF00FF00);
			wl_state.drawLine3D_2_func(vLArmPos, vHandPos, 0xFF00FF00, 0xFF00FF00);
			if (bIKMeleeMode)
				wl_state.drawLine3D_2_func(vHandPos, vWepPos, 0xFF00FF00, 0xFF00FF00);

			//blue = straight shot to target from chain origin
			wl_state.drawLine3D_2_func(vUArmPos, xTargetHand.vPos, 0xFF0000FF, 0xFF0000FF);

			//green boxes = original bone positions
			copyVec3(vUArmPos,mBox[3]);
			wl_state.drawBox3D_func(vMin, vMax, mBox, 0xFF00FF00, 1);
			copyVec3(vLArmPos,mBox[3]);
			wl_state.drawBox3D_func(vMin, vMax, mBox, 0xFF00FF00, 1);
		}

		// We have a triangle: Upper arm, Lower arm, and From shoulder to Target
		// Since we know the lengths, we can use the law of cosines to calculate the angles
		{
			//determine the triangle edge lengths
			F32 fU, fL, fT;
			F32 fLAngle;
			F32 fElbowAngle;
			fU = lengthVec3(vUArm);
			fL = lengthVec3(vLArm);
			fT = lengthVec3(vToTarget);

			// No triangle, target is out of reach
			if (fU + fL <= fT)
			{
				fT = (fU + fL) * 0.9999f;
			}

			// This is the angle between the vector to the target and the vector to the elbow
			{
				F32 c = ( SQR(fL) - SQR(fU) - SQR(fT) ) / (-2.0f * fU * fT);
				fLAngle = acosf(CLAMPF32(c,-1,1));
			}

			// Law of sines
			{
				F32 s = fT * sinf(fLAngle) / fL;
				fElbowAngle = asinf(CLAMPF32(s,-1,1));
			}

			if (iPrintDebugInfo>0)
			{
				printf("U - %.5f\n", fU);
				printf("L - %.5f\n", fL);
				printf("T - %.5f\n", fT);
				printf("LAngle - %.5f\n", DEG(fLAngle));
				printf("Elbow - %.5f\n", DEG(fElbowAngle));
			}

			// Now, find normal of plane the triangle is in
			crossVec3(vToTarget, vUArm, vPlaneNormal);
			normalVec3(vPlaneNormal);

			//purple = normal of plane the law of (co)sines triangle is in
			if (dynDebugState.bDebugIK)
			{
				Vec3 vPlaneNormalOffset;
				scaleAddVec3(vPlaneNormal, 1.99, vUArmPos, vPlaneNormalOffset);
				wl_state.drawLine3D_2_func(vUArmPos, vPlaneNormalOffset, 0xFFFF00FF, 0xFFFF00FF);
			}

			// Create a vector pointing toward our new elbow position
			{
				Quat qTowardElbow;
				axisAngleToQuat(vPlaneNormal, -fLAngle, qTowardElbow);
				quatRotateVec3Inline(qTowardElbow, vToTarget, vToElbow);
				normalVec3(vToElbow);
				scaleVec3(vToElbow, fU, vToElbow);
			}

			//white = shoulder rotated bones
			if (dynDebugState.bDebugIK)
			{
				Vec3 vTest;
				addVec3(vUArmPos, vToElbow, vTest);
				wl_state.drawLine3D_2_func(vUArmPos, vTest, 0xFFFFFFFF, 0xFFFFFFFF);
			}

			// Fix Shoulder
			{
				Vec3 vShoulderFixupAxis;
				F32 fAngle;
				Quat qCurrentShoulderWorld;
				Quat qNewShoulder;
				Quat qNewShoulderWorld;
				Quat qAdjust;
				Quat qCollarWorld, qCollarWorldInv;

				//get coord space info
				dynNodeGetWorldSpaceRot(pUArm, qCurrentShoulderWorld);
				dynNodeGetWorldSpaceRot(pUArm->pParent, qCollarWorld);
				quatInverse(qCollarWorld, qCollarWorldInv);

				//find the axis to rotate the shoulder on
				crossVec3(vToElbow, vUArm, vShoulderFixupAxis);
				normalVec3(vShoulderFixupAxis);

				//yellow = shoulder rotation axis
				if (dynDebugState.bDebugIK)
				{
					Vec3 vTest;
					scaleAddVec3(vShoulderFixupAxis, 2.0f, vUArmPos, vTest);
					wl_state.drawLine3D_2_func(vUArmPos, vTest, 0xFFFFFF00, 0xFFFFFF00);
				}

				//rotate the shoulder into it's new posture
				fAngle = getVec3Angle(vUArm, vToElbow);
				axisAngleToQuat(vShoulderFixupAxis, fAngle, qAdjust);
				quatMultiply(qCurrentShoulderWorld, qAdjust, qNewShoulderWorld);
				quatMultiply(qNewShoulderWorld, qCollarWorldInv, qNewShoulder);

				//apply the shoulder's local rotation to the skeleton
				dynNodeSetRotInline(pUArm, qNewShoulder);

				//make sure the position of the joint stays the same
				if (bIKGroundRegMode) {
					dynNodeCalcWorldSpaceOneNode(pUArm);
					dynNodeCalcLocalSpacePosFromWorldSpacePos(pUArm, vUArmPos);
					dynNodeSetPosInline(pUArm, vUArmPos);
				}

				// Now calc elbow
				{
					Vec3 vNewElbowPos, vNewHandPos, vNewWepPos;
					Vec3 vNewLArm, vNewTargetArm, vNewHand;

					//update our joint transforms post shoulder rotation
					dynNodeCalcWorldSpaceOneNode(pUArm);
					dynNodeCalcWorldSpaceOneNode(pLArm);
					dynNodeCalcWorldSpaceOneNode(pHand);

					//grab the updated positions for the remaining joints that need fixed
					dynNodeGetWorldSpacePos(pLArm, vNewElbowPos);
					dynNodeGetWorldSpacePos(pHand, vNewHandPos);

					if (dynDebugState.bDebugIK)
					{
						Vec3 vMin, vMax;
						Mat4 mBox;

						dynNodeGetWorldSpacePos(pUArm, vUArmPos);
						identityMat4(mBox);
						vMin[0] = vMin[1] = vMin[2] = -0.015f;
						vMax[0] = vMax[1] = vMax[2] =  0.015f;

						//blue-gray boxes = arm & elbow positions post-step 1 of analytical ik solver
						copyVec3(vUArmPos,mBox[3]);
						wl_state.drawBox3D_func(vMin, vMax, mBox, 0xFF8888FF, 1);
						copyVec3(vNewElbowPos,mBox[3]);
						wl_state.drawBox3D_func(vMin, vMax, mBox, 0xFF8888FF, 1);
					}

					//update our vectors post shoulder rotation
					subVec3(vNewHandPos, vNewElbowPos, vNewLArm);
					subVec3(xTargetHand.vPos, vNewElbowPos, vNewTargetArm);

					//slight mod to support melee weapons, note that
					//the larm bone now has the hand bone welded on
					if (bIKBothHandsMode || bIKMeleeMode){
						dynNodeCalcWorldSpaceOneNode(pWep);
						dynNodeGetWorldSpacePos(pWep, vNewWepPos);
						subVec3(vNewWepPos, vNewHandPos, vNewHand);
						addVec3(vNewLArm, vNewHand, vNewLArm);
					}

					//white = shoulder rotated bones
					if (dynDebugState.bDebugIK)
					{
						wl_state.drawLine3D_2_func(vNewElbowPos, vNewHandPos, 0xFFFFFFFF, 0xFFFFFFFF);
						if (bIKMeleeMode)
							wl_state.drawLine3D_2_func(vNewHandPos, vNewWepPos, 0xFFFFFFFF, 0xFFFFFFFF);
					}

					{
						Vec3 vElbowFixupAxis;
						F32 fElbowFixupAngle;
						Quat qElbowFixup;
						Quat qElbowWorld;
						Quat qNewElbowWorld;
						Quat qNewElbowLocal;
						Quat qNewShoulderWorldInv;

						//find the axis to rotate the elbow on
						crossVec3(vNewLArm, vNewTargetArm, vElbowFixupAxis);
						fElbowFixupAngle = -getVec3Angle(vNewLArm, vNewTargetArm);
						normalVec3(vElbowFixupAxis);

						//rotate the elbow into it's new posture
						axisAngleToQuat(vElbowFixupAxis, fElbowFixupAngle, qElbowFixup);
						dynNodeGetWorldSpaceRot(pLArm, qElbowWorld);
						quatMultiply(qElbowWorld, qElbowFixup, qNewElbowWorld);
						quatInverse(qNewShoulderWorld, qNewShoulderWorldInv);
						quatMultiply(qNewElbowWorld, qNewShoulderWorldInv, qNewElbowLocal);

						//apply the elbow's local rotation to the skeleton
						dynNodeSetRotInline(pLArm, qNewElbowLocal);

						//make sure the position of the joint stays the same
						if (bIKGroundRegMode) {
							dynNodeCalcWorldSpaceOneNode(pLArm);
							dynNodeCalcLocalSpacePosFromWorldSpacePos(pLArm, vNewElbowPos);
							dynNodeSetPosInline(pLArm, vNewElbowPos);
						}

						if (dynDebugState.bDebugIK)
						{
							//redish-gray = post-ik bone data
							Vec3 v1, v2, v3;
							dynNodeCalcWorldSpaceOneNode(pUArm);
							dynNodeCalcWorldSpaceOneNode(pLArm);
							dynNodeCalcWorldSpaceOneNode(pHand);
							dynNodeGetWorldSpacePos(pUArm, v3);
							dynNodeGetWorldSpacePos(pLArm, v2);
							dynNodeGetWorldSpacePos(pHand, v1);
							wl_state.drawLine3D_2_func(v1, v2, 0xFF888888, 0xFFFF0000);
							wl_state.drawLine3D_2_func(v2, v3, 0xFF888888, 0xFFFF0000);
							wl_state.drawLine3D_2_func(v1, v3, 0xFF888888, 0xFFFF0000);
							{
								Vec3 vMin, vMax;
								Mat4 mBox;
								vMin[0] = vMin[1] = vMin[2] = -0.015f;
								vMax[0] = vMax[1] = vMax[2] =  0.015f;
								identityMat4(mBox);
								copyVec3(v2,mBox[3]);
								wl_state.drawBox3D_func(vMin, vMax, mBox, 0xFFFF8888, 1);
							}
						}

						if (bIKGroundRegMode)
						{
							Quat qLArm, qLArmInv, qNew;

							//update the location of required joints
							dynNodeCalcWorldSpaceOneNode(pLArm);
							dynNodeCalcWorldSpaceOneNode(pHand);

							//determine the hand's new orientation
							dynNodeGetWorldSpaceRot(pLArm, qLArm);
							quatInverse(qLArm, qLArmInv);

							//reset the hand to point back along it's original direction
							quatMultiply(xTargetHand.qRot, qLArmInv, qNew);
							dynNodeSetRotInline(pHand, qNew);
						}
						else if (!bDisableIKLeftWrist)
						{
							if (bIKMeleeMode)
							{
								//melee version, rotate the hand to point along the weapons shaft
								Quat qWepWorld, qWepWorldInv, qNewWepWorld, qApplyWepWorld;
								Quat qApplyToHand, qHandWorld, qHandTemp, qNewHandLocal;
								Quat qNewElbowWorldInv;
								Mat3 mWepWorld, mNewWepWorld;
								Vec3 aWepX, aNewWepX;
								Vec3 aWepY, aNewWepY;
								Vec3 aWepZ, aNewWepZ;
								Vec3 aWeapon;
								F32 angleWeapon;

								//update the location of required joints
								dynNodeCalcWorldSpaceOneNode(pLArm);
								dynNodeCalcWorldSpaceOneNode(pHand);
								dynNodeCalcWorldSpaceOneNode(pWep);

								//grab the weapon axis in world space
								//note: this is set by dynAnimFindTargetTransform when finding the target in dynSeqCalcIK
								quatToAxisAngle(xTargetHand.qRot, aWeapon, &angleWeapon);

								//grab the wep joint orientation in world space
								dynNodeGetWorldSpaceRot(pWep, qWepWorld);
								quatToMat(qWepWorld, mWepWorld);
								aWepX[0] = mWepWorld[0][0]; aWepX[1] = mWepWorld[0][1]; aWepX[2] = mWepWorld[0][2];
								aWepY[0] = mWepWorld[1][0]; aWepY[1] = mWepWorld[1][1]; aWepY[2] = mWepWorld[1][2];
								aWepZ[0] = mWepWorld[2][0]; aWepZ[1] = mWepWorld[2][1]; aWepZ[2] = mWepWorld[2][2];

								//redo the wep's orientation in world space to run along the weapon's axis
								copyVec3(aWeapon, aNewWepZ);
								crossVec3(aWepY, aNewWepZ, aNewWepX);
								crossVec3(aNewWepZ, aNewWepX, aNewWepY);
								mNewWepWorld[0][0] = aNewWepX[0]; mNewWepWorld[0][1] = aNewWepX[1]; mNewWepWorld[0][2] = aNewWepX[2];
								mNewWepWorld[1][0] = aNewWepY[0]; mNewWepWorld[1][1] = aNewWepY[1]; mNewWepWorld[1][2] = aNewWepY[2];
								mNewWepWorld[2][0] = aNewWepZ[0]; mNewWepWorld[2][1] = aNewWepZ[1]; mNewWepWorld[2][2] = aNewWepZ[2];
								mat3ToQuat(mNewWepWorld, qNewWepWorld);

								//compute the difference in the wep's orientation
								quatInterp(fWepRegisterBlend, qWepWorld, qNewWepWorld, qApplyWepWorld); //doing this here instead of during Find Target since its loads easier
								quatInverse(qWepWorld, qWepWorldInv);
								quatMultiply(qWepWorldInv, qApplyWepWorld, qApplyToHand);

								//apply the difference to the hand
								dynNodeGetWorldSpaceRot(pHand, qHandWorld);
								quatInverse(qNewElbowWorld, qNewElbowWorldInv);
								quatMultiply(qHandWorld, qApplyToHand, qHandTemp);
								quatMultiply(qHandTemp, qNewElbowWorldInv, qNewHandLocal);
								dynNodeSetRotInline(pHand, qNewHandLocal);

								if (dynDebugState.bDebugIK)
								{
									DynTransform pt;
									Vec3 pa, pb;

									dynNodeCalcWorldSpaceOneNode(pHand);
									dynNodeCalcWorldSpaceOneNode(pWep);
									dynNodeGetWorldSpacePos(pWep, pa);

									pb[0] = pa[0] + 3*aNewWepX[0];
									pb[1] = pa[1] + 3*aNewWepX[1];
									pb[2] = pa[2] + 3*aNewWepX[2];
									wl_state.drawLine3D_2_func(pa, pb, 0xFFFF8888, 0xFFFF8888);
									pb[0] = pa[0] + 3*aNewWepY[0];
									pb[1] = pa[1] + 3*aNewWepY[1];
									pb[2] = pa[2] + 3*aNewWepY[2];
									wl_state.drawLine3D_2_func(pa, pb, 0xFF88FF88, 0xFF88FF88);
									pb[0] = pa[0] + 3*aNewWepZ[0];
									pb[1] = pa[1] + 3*aNewWepZ[1];
									pb[2] = pa[2] + 3*aNewWepZ[2];
									wl_state.drawLine3D_2_func(pa, pb, 0xFF8888FF, 0xFF8888FF);

									dynNodeGetWorldSpaceTransform(pWep, &pt);
									wl_state.drawAxesFromTransform_func(&pt, 3.0f);
								}
							}
							else
							{
								Quat qNewElbowWorldInv;
								Quat qNewHandLocal;

								//gun version, Match hand to target orientation
								quatInverse(qNewElbowWorld, qNewElbowWorldInv);
								quatMultiply(xTargetHand.qRot, qNewElbowWorldInv, qNewHandLocal);
								dynNodeSetRotInline(pHand, qNewHandLocal);
							}
						}
					}
				}
			}
		}
	}
	return true;
}

bool dynAnimPrintArmFixupDebugInfo(DynNode* pHand, const DynTransform* pxTarget)
{
	DynNode* pLArm = pHand->pParent;
	DynNode* pUArm = pHand->pParent->pParent;
	Vec3 vHandPos, vLArmPos, vUArmPos;
	Vec3 vUArm, vLArm, vToTarget;
	DynTransform xHand;

	dynNodeGetWorldSpaceTransform(pHand, &xHand);

	dynNodeGetWorldSpacePos(pHand, vHandPos);
	dynNodeGetWorldSpacePos(pLArm, vLArmPos);
	dynNodeGetWorldSpacePos(pUArm, vUArmPos);

	subVec3(vLArmPos, vUArmPos, vUArm);
	subVec3(vHandPos, vLArmPos, vLArm);
	subVec3(pxTarget->vPos, vUArmPos, vToTarget);


	if (dynDebugState.bDebugIK)
	{
		wl_state.drawLine3D_2_func(vUArmPos, vLArmPos, 0xFFA0FFA0, 0xFFA0FFA0);
		wl_state.drawLine3D_2_func(vLArmPos, vHandPos, 0xFFA0A0FF, 0xFFA0A0FF);
		wl_state.drawAxesFromTransform_func(&xHand, 0.7f);
	}


	// We have a triangle: Upper arm, Lower arm, and From shoulder to Target
	// Since we know the lengths, we can use the law of cosines to calculate the angles
	{
		F32 fU, fL, fT;
		fU = lengthVec3(vUArm);
		fL = lengthVec3(vLArm);
		fT = lengthVec3(vToTarget);

		if (iPrintDebugInfo>0)
		{
			printf("U - %.5f\n", fU);
			printf("L - %.5f\n", fL);
			printf("T - %.5f\n", fT);
			printf("\n");
		}
	}

	if (iPrintDebugInfo>0)
		--iPrintDebugInfo;
	return true;
}

bool dynAnimPhysicsRaycastToGround(int iPartitionIdx, const Vec3 vStart, F32 fRange, Vec3 vImpactPos, Vec3 vImpactNorm)
{
#if !PSDK_DISABLED
	WorldCollCollideResults results;
	Vec3 vDir, vEnd;
	bool bHit;

	vDir[0] = 0.f;
	vDir[1] = -fabsf(fRange);
	vDir[2] = 0.f;
	addVec3(vStart, vDir, vEnd);

	bHit = wcRayCollide(worldGetActiveColl(iPartitionIdx),
						vStart, vEnd,
						WC_QUERY_BITS_WORLD_ALL, //WC_FILTER_BIT_MOVEMENT | WC_FILTER_BIT_TERRAIN | WC_FILTER_BIT_HEIGHTMAP,
						&results);

	if (bHit) {
		copyVec3(results.posWorldImpact, vImpactPos);
		copyVec3(results.normalWorld, vImpactNorm);
		if (dynDebugState.bDrawTerrainTilt && wl_state.drawLine3D_2_func) {
			wl_state.drawLine3D_2_func(vStart, vImpactPos, 0xFF00FF00, 0xFF00FF00);
		}
	} else {
		if (dynDebugState.bDrawTerrainTilt && wl_state.drawLine3D_2_func) {
			wl_state.drawLine3D_2_func(vStart, vEnd, 0xFFFF0000, 0xFFFF0000);
		}
	}

	return bHit;

#else

	return false;

#endif
}