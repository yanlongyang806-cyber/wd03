#include "dynStrand.h"

#include "fileutil.h"
#include "FolderCache.h"
#include "error.h"
#include "StringCache.h"
#include "dynSeqData.h"
#include "dynFxManager.h"

#include "dynSkeleton.h"
#include "mathutil.h"
#include "dynNode.h"
#include "dynNodeInline.h"
#include "wlState.h"

#include "dynStrand_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

//////////////////////////////////////////////////////////////////////////////////
//
// Strand Data 
//
//////////////////////////////////////////////////////////////////////////////////

DictionaryHandle hStrandDataSetDict;

static bool dynStrandDataVerify(DynStrandDataSet *pDataSet, DynStrandData* pData)
{
	bool bRet = true;

	if (!pData->pcStartNodeName) {
		ErrorFilenamef(pDataSet->pcFileName, "Must specify a start node\n");
		bRet = false;
	}

	if (!pData->pcEndNodeName) {
		ErrorFilenamef(pDataSet->pcFileName, "Must specify an end node\n");
		bRet = false;
	}

	if (pData->fEndPointMass <= 0.f) {
		ErrorFilenamef(pDataSet->pcFileName, "Must assign a positive mass to end node %s\n", pData->pcEndNodeName);
		bRet = false;
	}

	if (pData->pcBreakNodeName && pData->fBreakPointMass <= 0.f) {
		ErrorFilenamef(pDataSet->pcFileName, "Must assign a postive mass to break node %s\n", pData->pcBreakNodeName);
		bRet = false;
	}

	if (pData->bFullGroundReg && pData->bPartialGroundReg) {
		ErrorFilenamef(pDataSet->pcFileName, "Only allowed to specify upto one ground registration mode per strand info\n");
		bRet = false;
	}

	return bRet;
}

bool dynStrandDataSetVerify(DynStrandDataSet *pDataSet)
{
	bool bRet = true;
	FOR_EACH_IN_EARRAY(pDataSet->eaDynStrandData, DynStrandData, pData) {
		bRet = bRet && dynStrandDataVerify(pDataSet, pData);
	} FOR_EACH_END;
	return bRet;
}

bool dynStrandDataSetFixup(DynStrandDataSet* pDataSet)
{
	char cName[256];
	getFileNameNoExt(cName, pDataSet->pcFileName);
	pDataSet->pcName = allocAddString(cName);

	return true;
}

static void dynStrandDataSetReloadCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath)) {
		; // File was deleted, do we care here?
	}

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if(!ParserReloadFileToDictionary(relpath,hStrandDataSetDict)) {
		CharacterFileError(relpath, "Error reloading DynStrandDataSet file: %s", relpath);
	}
}

AUTO_FIXUPFUNC;
TextParserResult fixupStrandDataSet(DynStrandDataSet* pStrandDataSet, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
			if (!dynStrandDataSetVerify(pStrandDataSet) || !dynStrandDataSetFixup(pStrandDataSet))
				return PARSERESULT_INVALID; // remove this
		xcase FIXUPTYPE_POST_BIN_READ:
			if (!dynStrandDataSetFixup(pStrandDataSet))
				return PARSERESULT_INVALID; // remove this
	}
	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void registerStrandDataSetDict(void)
{
	hStrandDataSetDict = RefSystem_RegisterSelfDefiningDictionary("DynStrandDataSet", false, parse_DynStrandDataSet, true, false, NULL);
}

void dynStrandDataSetLoadAll(void)
{
	loadstart_printf("Loading DynStrandDataSet...");

	// optional for outsource build
	ParserLoadFilesToDictionary("dyn/strand", ".strand", "DynStrandData.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, hStrandDataSetDict);

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/strand/*.strand", dynStrandDataSetReloadCallback);
	}

	loadend_printf("done (%d DynStrandDataSets)", RefSystem_GetDictionaryNumberOfReferents(hStrandDataSetDict) );
}

//////////////////////////////////////////////////////////////////////////////////
//
// Skeleton Strand
//
//////////////////////////////////////////////////////////////////////////////////=

static bool bDisableStrandGroundReg = false;
static bool bDynOnlyStrands = false;
static bool bDrawStrands = false;

AUTO_CMD_INT(bDisableStrandGroundReg, danimDisableStrandGroundReg) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);
AUTO_CMD_INT(bDynOnlyStrands, danimDynOnlyStrands) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);
AUTO_CMD_INT(bDrawStrands, danimDrawStrands) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);

void dynStrandInitStrandPoint(	DynStrand		*pStrand,
								DynStrandPoint	*pStrandPoint,
								DynTransform	*pxRoot,
								F32				fAdditionalLength)
{
	DynNode *pNode;
	Vec3 vPosLast;

	//determine the initial animation positions
	dynNodeGetWorldSpacePos(pStrandPoint->pFromNode, pStrandPoint->vFromPos);
	dynNodeGetWorldSpacePos(pStrandPoint->pToNode,   pStrandPoint->vToPos  );

	//find the length along the strand to reach the point
	pStrandPoint->fRestLength = 0.f;
	copyVec3(pStrandPoint->vToPos, vPosLast);
	pNode = pStrandPoint->pToNode;
	do {
		Vec3 vPosCur, vDiff;
		pNode = pNode->pParent;
		dynNodeGetWorldSpacePos(pNode, vPosCur);
		subVec3(vPosLast, vPosCur, vDiff);
		pStrandPoint->fRestLength += lengthVec3(vDiff);
		copyVec3(vPosCur, vPosLast);
	} while (pNode != pStrandPoint->pFromNode);

	//compute the procedural end point
	if (pStrand->bAxisIsInWorldSpace) {
		copyVec3(pStrand->vAxis, pStrand->vAxisDuringSim);
	} else {
		quatRotateVec3(pxRoot->qRot, pStrand->vAxis, pStrand->vAxisDuringSim);
	}
	scaleAddVec3(pStrand->vAxisDuringSim, pStrandPoint->fRestLength+fAdditionalLength, pStrand->vRootPos, pStrandPoint->vProcPos);

	//setup the physics
	if (pStrand->bPrealignToProceduralAxis) {
		copyVec3(pStrandPoint->vProcPos, pStrandPoint->vPos);
	} else {
		copyVec3(pStrandPoint->vToPos, pStrandPoint->vPos);
	}
	zeroVec3(pStrandPoint->vVel);
	zeroVec3(pStrandPoint->vProcVel);
	zeroVec3(pStrandPoint->vAcc);
	zeroVec3(pStrandPoint->vForce);
}

static void dynStrandUpdateProceduralPoint(	DynStrand *pStrand,
											DynStrandPoint *pStrandPoint,
											DynTransform *pxRoot,
											F32 fAdditionalLength,
											F32 fDeltaTime)
{
	Vec3 vOldProcPos;

	//update the old position
	copyVec3(pStrandPoint->vProcPos, vOldProcPos);

	//compute the procedural end point
	if (pStrand->bAxisIsInWorldSpace) {
		copyVec3(pStrand->vAxis, pStrand->vAxisDuringSim);
	} else {
		quatRotateVec3(pxRoot->qRot, pStrand->vAxis, pStrand->vAxisDuringSim);
	}
	scaleAddVec3(pStrand->vAxisDuringSim, pStrandPoint->fRestLength+fAdditionalLength, pStrand->vRootPos, pStrandPoint->vProcPos);
	subVec3(pStrandPoint->vProcPos, vOldProcPos, pStrandPoint->vProcVel);
	scaleVec3(pStrandPoint->vProcVel, 1.f/fDeltaTime, pStrandPoint->vProcVel);
}

static void dynStrandCalcStrandPointForce(	DynStrand *pStrand,
											DynStrandPoint *pStrandPoint,
											Vec3 vStrandPointPos, //local for use with runge kutta
											Vec3 vStrandPointVel, //local for use with runge kutta
											DynTransform *pxRoot,
											Vec3 vRootVel,
											Vec3 vParentPos,
											Vec3 vParentVel,
											F32 fAdditionalLength,
											U32 bIsWeakPoint)
{
	//add force between the end point and its procedural target
	{
		Vec3 vSpring12, vVelocity12;
		F32 fSpringLength;

		//compute difference in position
		subVec3(pStrandPoint->vProcPos, vStrandPointPos, vSpring12);
		fSpringLength = normalVec3(vSpring12);

		//compute difference in velocity
		subVec3(pStrandPoint->vProcVel, vStrandPointVel, vVelocity12);

		//compute spring force (overwriting the current force from last update)
		scaleVec3(	vSpring12
					,
					pStrandPoint->fSpringK*(fSpringLength-0.f/*rest length*/) +
					pStrandPoint->fDamperC*dotVec3(vVelocity12,vSpring12)
					,
					pStrandPoint->vForce);
		assert(FINITEVEC3(pStrandPoint->vForce));
	}

	//add force between the end point and its attachment (from point)
	{
		Vec3 vSpring12, vVelocity12;
		F32 fSpringLength;
		Vec3 vForce;
		F32 fScalar;

		if (bIsWeakPoint) {
			fScalar = pStrand->fTorsionRatio;
		} else {
			fScalar = 1.0;
		}

		//compute difference in position
		subVec3(pStrand->vRootPos, vStrandPointPos, vSpring12);
		fSpringLength = normalVec3(vSpring12);

		//compute difference in velocity
		subVec3(vRootVel, vStrandPointVel, vVelocity12);

		//compute spring force
		scaleVec3(	vSpring12
					,
					fScalar * 
					(	pStrand->fSelfSpringK*(fSpringLength-(pStrandPoint->fRestLength+fAdditionalLength)) +
						pStrand->fSelfDamperC*dotVec3(vVelocity12,vSpring12))
					,
					vForce);
		assert(FINITEVEC3(vForce));
		addVec3(vForce, pStrandPoint->vForce, pStrandPoint->vForce);
	}

	//add force between the end point and its parent
	if (bIsWeakPoint)
	{
		Vec3 vSpring12, vVelocity12;
		F32 fSpringLength;
		Vec3 vForce;

		//compute difference in position
		subVec3(vParentPos, vStrandPointPos, vSpring12);
		fSpringLength = normalVec3(vSpring12);

		//compute difference in velocity
		subVec3(vParentVel, vStrandPointVel, vVelocity12);

		//compute spring force
		scaleVec3(	vSpring12
					,
					pStrand->fSelfSpringK*(fSpringLength-pStrandPoint->fRestLength) +
					pStrand->fSelfDamperC*dotVec3(vVelocity12,vSpring12)
					,
					vForce);
		assert(FINITEVEC3(vForce));
		addVec3(vForce, pStrandPoint->vForce, pStrandPoint->vForce);
	}

	//motion damping
	scaleAddVec3(vStrandPointVel, -pStrand->fWindResistance, pStrandPoint->vForce, pStrandPoint->vForce);

	//apply gravity, feet per second
	pStrandPoint->vForce[1] -= pStrand->fGravity;
}

static void dynStrandStepStrandPoint(	DynStrandPoint *pStrandPoint,
										F32 fDeltaTime)
{
	//Euler integration
	scaleVec3(pStrandPoint->vForce, pStrandPoint->fMassInv, pStrandPoint->vAcc);
	scaleAddVec3(pStrandPoint->vAcc, fDeltaTime, pStrandPoint->vVel, pStrandPoint->vVel);
	scaleAddVec3(pStrandPoint->vVel, fDeltaTime, pStrandPoint->vPos, pStrandPoint->vPos);

	//make sure the physics didn't freak out
	assert(FINITEVEC3(pStrandPoint->vPos));

	//crash prevention
	if (fabsf(pStrandPoint->vPos[0]) > 15000.f) pStrandPoint->vPos[0] = 0.f;
	if (fabsf(pStrandPoint->vPos[1]) > 15000.f) pStrandPoint->vPos[1] = 0.f;
	if (fabsf(pStrandPoint->vPos[2]) > 15000.f) pStrandPoint->vPos[2] = 0.f;
}

void dynStrandPrealignToProceduralAxis(DynStrand *pStrand)
{
	DynNode *pApplyNode = pStrand->eaJoints[0];
	Quat qNodeLS;
	Quat qNodeWS, qNodeWSInv;
	Quat qReset;

	Vec3 vZero = {0,0,0};
	Quat qAxis;
	S32 i;

	//undo any animation on the strand at the 1st joint
	pApplyNode = pStrand->eaJoints[0];
	dynNodeGetLocalRot(pApplyNode, qNodeLS);
	dynNodeGetWorldSpaceRot(pApplyNode, qNodeWS);
	quatInverse(qNodeWS, qNodeWSInv);
	quatMultiply(qNodeWSInv, qNodeLS, qReset);

	//apply a rotation so it'll match up with the procedural axis
	quatLookAt(vZero, pStrand->vAxisDuringSim, qAxis);
	quatMultiply(qAxis, qReset, qNodeLS);
	dynNodeSetRotInline(pApplyNode, qNodeLS);

	//update the transforms along the strand
	for (i = 0; i < eaSize(&pStrand->eaJoints); i++)
	{
		pApplyNode = pStrand->eaJoints[i];
		dynNodeCalcWorldSpaceOneNode(pApplyNode);
	}
}

static void dynStrandDeformStrandPoint(	DynStrand *pStrand,
										DynStrandPoint *pStrandPoint,
										DynTransform *pxRootInv,
										Vec3 vParentPos,
										U32 uiStartIndex,
										U32 uiEndIndex)
{
	Vec3 vFromPosLS, vProcPosLS, vPosLS;
	Vec3 vProc, vDyn;
	F32 fProcLength, fDynLength;
	F32 fDot;

	U32 uiApplyIndex;
	Quat qApply;

	dynTransformApplyToVec3(pxRootInv, vParentPos, vFromPosLS);
	dynTransformApplyToVec3(pxRootInv, pStrandPoint->vProcPos, vProcPosLS);
	dynTransformApplyToVec3(pxRootInv, pStrandPoint->vPos, vPosLS);

	subVec3(vProcPosLS, vFromPosLS, vProc);
	subVec3(vPosLS, vFromPosLS, vDyn);
	fProcLength = normalVec3(vProc);
	fDynLength = normalVec3(vDyn);
	fDot = dotVec3(vDyn, vProc);

	if (fProcLength > 0.f &&
		fDynLength > 0.f &&
		fDot <= 0.9999f)
	{
		Vec3 vAxis;
		F32 fAngle;

		crossVec3(vDyn, vProc, vAxis);
		normalVec3(vAxis);
		fAngle = acosf(CLAMP(fDot,-1,1));
		fAngle *= pStrand->fStrength / pStrand->uiNumJoints;
		fAngle = CLAMP(fAngle, 0.f, RAD(pStrand->fMaxJointAngle));
		axisAngleToQuat(vAxis, fAngle, qApply);
	}
	else
	{
		unitQuat(qApply);
	}

	for (uiApplyIndex = uiStartIndex; uiApplyIndex <= uiEndIndex; uiApplyIndex++)
	{
		DynNode *pApplyNode = pStrand->eaJoints[uiApplyIndex];
		Quat qNode, qNew;

		//modify the joint's rotation
		if (bDynOnlyStrands)
		{
			dynNodeSetRotInline(pApplyNode, qApply);
		}
		else if (pApplyNode->uiUpdatedThisAnim)
		{
			dynNodeGetLocalRotInline(pApplyNode, qNode);
			quatMultiplyInline(qNode, qApply, qNew);
			dynNodeSetRotInline(pApplyNode, qNew);
		}
		dynNodeCalcWorldSpaceOneNode(pApplyNode);
	}
}

void dynStrandEvaluateRungeKuttaStep(	DynStrand *pStrand,
										DynTransform *pxRootBone,
										Vec3 vRootVel,
										const Vec3 vStrongDposIn,
										const Vec3 vStrongDvelIn,
										Vec3 vStrongDposOut,
										Vec3 vStrongDvelOut,
										const Vec3 vWeakDposIn,
										const Vec3 vWeakDvelIn,
										Vec3 vWeakDposOut,
										Vec3 vWeakDvelOut,
										F32 fDeltaTime)
{
	Vec3 vStrongPos, vWeakPos;
	Vec3 vStrongVel, vWeakVel;

	scaleAddVec3(vStrongDposIn, fDeltaTime, pStrand->strongPoint.vPos, vStrongPos);
	scaleAddVec3(vStrongDvelIn, fDeltaTime, pStrand->strongPoint.vVel, vStrongVel);
	if (pStrand->bHasWeakPoint) {
		scaleAddVec3(vWeakDposIn, fDeltaTime, pStrand->weakPoint.vPos, vWeakPos);
		scaleAddVec3(vWeakDvelIn, fDeltaTime, pStrand->weakPoint.vVel, vWeakVel);
	}

	dynStrandCalcStrandPointForce(	pStrand,
									&pStrand->strongPoint,
									vStrongPos,
									vStrongVel,
									pxRootBone,
									vRootVel,
									pStrand->vRootPos,
									vRootVel,
									0.f,
									0);
	scaleVec3(pStrand->strongPoint.vForce, pStrand->strongPoint.fMassInv, pStrand->strongPoint.vAcc);
	copyVec3(vStrongVel, vStrongDposOut);
	copyVec3(pStrand->strongPoint.vAcc, vStrongDvelOut);

	if (pStrand->bHasWeakPoint) {
		dynStrandCalcStrandPointForce(	pStrand,
										&pStrand->weakPoint,
										vWeakPos,
										vWeakVel,
										pxRootBone,
										vRootVel,
										vStrongPos,
										vStrongVel,
										pStrand->strongPoint.fRestLength,
										1);
		scaleVec3(pStrand->weakPoint.vForce, pStrand->weakPoint.fMassInv, pStrand->weakPoint.vAcc);
		copyVec3(vWeakVel, vWeakDposOut);
		copyVec3(pStrand->weakPoint.vAcc, vWeakDvelOut);
	}
}

static void dynStrandSumRungeKuttaTerms(Vec3 vValue, Vec3 vK1, Vec3 vK2, Vec3 vK3, Vec3 vK4, F32 fDeltaTime)
{
	Vec3 vTemp;
	vTemp[0] = (vK1[0] + 2.f*(vK2[0] + vK3[0]) + vK4[0]) / 6.f;
	vTemp[1] = (vK1[1] + 2.f*(vK2[1] + vK3[1]) + vK4[1]) / 6.f;
	vTemp[2] = (vK1[2] + 2.f*(vK2[2] + vK3[2]) + vK4[2]) / 6.f;
	scaleAddVec3(vTemp, fDeltaTime, vValue, vValue);
}

static void dynStrandUpdateSpringMassRungeKutta(DynStrand *pStrand,
												DynTransform *pxRootBone,
												Vec3 vRootVel,
												F32 fDeltaTime)
{
	Vec3 vStrongDposK[4], vWeakDposK[4];
	Vec3 vStrongDvelK[4], vWeakDvelK[4];

	//compute our k terms
	dynStrandEvaluateRungeKuttaStep(pStrand, pxRootBone, vRootVel, zerovec3,        zerovec3,        vStrongDposK[0], vStrongDvelK[0], zerovec3,      zerovec3,      vWeakDposK[0], vWeakDvelK[0], 0.f);
	dynStrandEvaluateRungeKuttaStep(pStrand, pxRootBone, vRootVel, vStrongDposK[0], vStrongDvelK[0], vStrongDposK[1], vStrongDvelK[1], vWeakDposK[0], vWeakDvelK[0], vWeakDposK[1], vWeakDvelK[1], 0.5f*fDeltaTime);
	dynStrandEvaluateRungeKuttaStep(pStrand, pxRootBone, vRootVel, vStrongDposK[1], vStrongDvelK[1], vStrongDposK[2], vStrongDvelK[2], vWeakDposK[1], vWeakDvelK[1], vWeakDposK[2], vWeakDvelK[2], 0.5f*fDeltaTime);
	dynStrandEvaluateRungeKuttaStep(pStrand, pxRootBone, vRootVel, vStrongDposK[2], vStrongDvelK[2], vStrongDposK[3], vStrongDvelK[3], vWeakDposK[2], vWeakDvelK[2], vWeakDposK[3], vWeakDvelK[3], fDeltaTime);

	//sum the k's to obtain our results
	dynStrandSumRungeKuttaTerms(pStrand->strongPoint.vPos, vStrongDposK[0], vStrongDposK[1], vStrongDposK[2], vStrongDposK[3], fDeltaTime);
	dynStrandSumRungeKuttaTerms(pStrand->strongPoint.vVel, vStrongDvelK[0], vStrongDvelK[1], vStrongDvelK[2], vStrongDvelK[3], fDeltaTime);
	if (pStrand->bHasWeakPoint) {
		dynStrandSumRungeKuttaTerms(pStrand->weakPoint.vPos, vWeakDposK[0], vWeakDposK[1], vWeakDposK[2], vWeakDposK[3], fDeltaTime);
		dynStrandSumRungeKuttaTerms(pStrand->weakPoint.vVel, vWeakDvelK[0], vWeakDvelK[1], vWeakDvelK[2], vWeakDvelK[3], fDeltaTime);
	}
}

static void dynStrandUpdateSpringMassEuler(	DynStrand *pStrand,
											DynTransform *pxRootBone,
											Vec3 vRootVel,
											F32 fDeltaTime)
{
	dynStrandCalcStrandPointForce(	pStrand,
									&pStrand->strongPoint,
									pStrand->strongPoint.vPos,
									pStrand->strongPoint.vVel,
									pxRootBone,
									vRootVel,
									pStrand->vRootPos,
									vRootVel,
									0.f,
									0);

	if (pStrand->bHasWeakPoint) {
		dynStrandCalcStrandPointForce(	pStrand,
										&pStrand->weakPoint,
										pStrand->weakPoint.vPos,
										pStrand->weakPoint.vVel,
										pxRootBone,
										vRootVel,
										pStrand->strongPoint.vPos,
										pStrand->strongPoint.vVel,
										pStrand->strongPoint.fRestLength,
										1);
	}
	
	dynStrandStepStrandPoint(&pStrand->strongPoint, fDeltaTime);
	if (pStrand->bHasWeakPoint) {
		dynStrandStepStrandPoint(&pStrand->weakPoint, fDeltaTime);
	}
}

void dynStrandDeform(DynStrand *pStrand, F32 fDeltaTime)
{
	DynTransform xRootBone, xRootBoneInv;
	Vec3 vOldRootPos, vRootVel;

	//update the root data
	copyVec3(pStrand->vRootPos, vOldRootPos);
	dynNodeGetWorldSpaceTransform(pStrand->pRootNode, &xRootBone);
	dynTransformInverse(&xRootBone, &xRootBoneInv);
	copyVec3(xRootBone.vPos, pStrand->vRootPos);
	subVec3(pStrand->vRootPos, vOldRootPos, vRootVel);
	scaleVec3(vRootVel, 1.f/fDeltaTime, vRootVel);

	//update the end data
	dynNodeGetWorldSpacePos(pStrand->pEndNode, pStrand->vEndPos);

	//update the strong procedural point data
	dynStrandUpdateProceduralPoint(	pStrand,
									&pStrand->strongPoint,
									&xRootBone,
									0.f,
									fDeltaTime);

	//update the weak procedural point data
	if (pStrand->bHasWeakPoint) {
		dynStrandUpdateProceduralPoint(	pStrand,
										&pStrand->weakPoint,
										&xRootBone,
										pStrand->strongPoint.fRestLength,
										fDeltaTime);
	}

	//update the spring mass system
	if (pStrand->bUseEulerIntegration) {
		dynStrandUpdateSpringMassEuler(	pStrand,
										&xRootBone,
										vRootVel,
										fDeltaTime);
	} else {
		dynStrandUpdateSpringMassRungeKutta(pStrand,
											&xRootBone,
											vRootVel,
											fDeltaTime);
	}

	//update the joint transforms
	if (pStrand->bPrealignToProceduralAxis) {
		dynStrandPrealignToProceduralAxis(pStrand);
	}
	dynStrandDeformStrandPoint(pStrand, &pStrand->strongPoint, &xRootBoneInv, pStrand->vRootPos, 0, pStrand->uiNumJoints-1);
	if (pStrand->bHasWeakPoint) {
		dynStrandDeformStrandPoint(pStrand, &pStrand->weakPoint, &xRootBoneInv, pStrand->vRootPos, pStrand->strongPoint.uiNumJoints-1, pStrand->uiNumJoints-1);
	}
}

void dynStrandDebugRenderStrandPoint(	DynStrandPoint *pStrandPoint,
										Vec3 vParentPos,
										Vec3 vRootPos)
{
	//render the springs are red lines & the procedural tail as a yellow line
	if (bDrawStrands) {
		wl_state.drawLine3D_2_func(pStrandPoint->vProcPos, vRootPos, 0xFFFFFF00, 0xFFFFFF00);
		wl_state.drawLine3D_2_func(pStrandPoint->vPos, pStrandPoint->vProcPos, 0xFFFF0000, 0xFFFF0000);
		wl_state.drawLine3D_2_func(pStrandPoint->vPos, vParentPos, 0xFFFF0000, 0xFFFF0000);
	}
}

void dynStrandDebugRenderStrand(DynStrand *pStrand,
								U32 uiColor)
{
	if (bDrawStrands)
	{
		U32 uiRenderIndex;
		for (uiRenderIndex = 0; uiRenderIndex < pStrand->uiNumJoints; uiRenderIndex++)
		{
			DynNode *pRenderNode = pStrand->eaJoints[uiRenderIndex];
			DynNode *pParentNode = pRenderNode->pParent;
			Vec3 vPos1, vPos2;
			dynNodeGetWorldSpacePos(pRenderNode, vPos1);
			dynNodeGetWorldSpacePos(pParentNode, vPos2);
			wl_state.drawLine3D_2_func(vPos1, vPos2, uiColor, uiColor);
			pRenderNode = pParentNode;
		}
	}
}

static void dynStrandGroundRegStrandJoints(	DynNode *pNodeA,
											DynNode *pNodeB,
											Vec3 vBasePos)
{
	Vec3 vPosA, vPosB;

	dynNodeGetWorldSpacePos(pNodeA, vPosA);
	dynNodeGetWorldSpacePos(pNodeB, vPosB);

	if (vPosA[1] >= vBasePos[1] &&
		vPosB[1] <  vBasePos[1] &&
		(	fabsf(vPosA[0]-vPosB[0]) >= 0.01f ||
			fabsf(vPosA[2]-vPosB[2]) >= 0.01f))
	{
		F32 fHeightOrig, fAbove, fLengthSqr;
		F32 fGroundLengthOrig, fGroundLengthWant, fGroundScale;
		Vec3 vPosC, vHave, vWant;

		Vec3 vAxis;
		F32 fAngle;

		Quat qNodeA, qNodeAInv;
		Vec3 vAxisLS;

		Quat qCurrent, qApply, qNew;

		fHeightOrig = vPosA[1] - vPosB[1];
		fAbove = vPosA[1] - vBasePos[1];

		subVec3(vPosB, vPosA, vHave);
		fLengthSqr = lengthVec3Squared(vHave);

		fGroundLengthOrig = sqrtf(fLengthSqr - fHeightOrig*fHeightOrig);
		fGroundLengthWant = sqrtf(fLengthSqr - fAbove*fAbove);
		fGroundScale = fGroundLengthWant/fGroundLengthOrig;

		vPosC[0] = vPosA[0] + vHave[0]*fGroundScale;
		vPosC[2] = vPosA[2] + vHave[2]*fGroundScale;
		vPosC[1] = vBasePos[1] + 0.1f;

		if (bDrawStrands) {
			wl_state.drawLine3D_2_func(vPosA, vPosB, 0xFF0000FF, 0xFF0000FF);
			wl_state.drawLine3D_2_func(vPosA, vPosC, 0xFF00FF00, 0xFF00FF00);
		}

		subVec3(vPosB, vPosA, vHave);
		subVec3(vPosC, vPosA, vWant); 
		normalVec3(vWant);
		normalVec3(vHave);
		fAngle = acosf(MINMAX(dotVec3(vWant,vHave),-1.f,1.f));
		crossVec3(vWant, vHave, vAxis);
		normalVec3(vAxis);

		dynNodeGetWorldSpaceRot(pNodeA, qNodeA);
		quatInverse(qNodeA, qNodeAInv);
		quatRotateVec3Inline(qNodeAInv, vAxis, vAxisLS);

		dynNodeGetLocalRotInline(pNodeA, qCurrent);
		axisAngleToQuat(vAxisLS, fAngle, qApply);
		quatMultiplyInline(qCurrent, qApply, qNew);
		dynNodeSetRotInline(pNodeA, qNew);
	}
}

static void dynStrandCalcStrandTransforms(	DynStrand *pStrand,
											U32 uiStart,
											U32 uiEnd)
{
	U32 uiCurNode;
	for (uiCurNode = uiStart; uiCurNode <= uiEnd; uiCurNode++) {
		dynNodeCalcWorldSpaceOneNode(pStrand->eaJoints[uiCurNode]);
	}
}

void dynStrandGroundRegStrandFull(	DynStrand *pStrand,
									Vec3 vBasePos)
{
	if (!bDisableStrandGroundReg)
	{
		//work on every node
		U32 uiCurNode;
		for (uiCurNode = 0; uiCurNode < pStrand->uiNumJoints-1; uiCurNode++)
		{
			dynStrandGroundRegStrandJoints(	pStrand->eaJoints[uiCurNode],
											pStrand->eaJoints[uiCurNode+1],
											vBasePos);

			dynStrandCalcStrandTransforms(	pStrand,
											uiCurNode,
											pStrand->uiNumJoints-1);
		}
	}
}

void dynStrandGroundRegStrandQuick(	DynStrand *pStrand,
									Vec3 vBasePos)
{
	if (!bDisableStrandGroundReg)
	{
		DynNode *pMinNode = NULL;
		F32 fMinBelowGround = 0.f;
		U32 uiCurNode;

		//find the node closest to the ground skipping the root node
		for (uiCurNode = 1; uiCurNode < pStrand->uiNumJoints; uiCurNode++)
		{
			F32 fChkBelowGround;
			Vec3 vChkPos;
			dynNodeGetWorldSpacePos(pStrand->eaJoints[uiCurNode], vChkPos);
			fChkBelowGround = vChkPos[1] - vBasePos[1];
			if (fChkBelowGround < fMinBelowGround) {
				fMinBelowGround = fChkBelowGround;
				pMinNode = pStrand->eaJoints[uiCurNode];
			}
		}

		if (pMinNode)
		{
			dynStrandGroundRegStrandJoints(	pStrand->eaJoints[0],
											pMinNode,
											vBasePos);

			dynStrandCalcStrandTransforms(	pStrand,
											0,
											pStrand->uiNumJoints-1);
		}
	}
}

#include "dynStrand_h_ast.c"