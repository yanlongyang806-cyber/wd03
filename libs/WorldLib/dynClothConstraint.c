#include "DynCloth.h"
#include "mathutil.h"
#include "dynNode.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

// This determines how significantly a collision affects the effective mass
// of a particle. 
#define COL_MASS_FACTOR 0.1f

//============================================================================
// CLOTH CONSTRAINT CODE
//============================================================================
// This code profoundly affects the behavior of the cloth.
// Currently the only constraints supported are length constraints.
// There are two types of length constraints supported:
//  1. MinMax constraints (RestLength >= 0).
//     These constraints push the particles towards a specific distance apart
//  2. Min constraints (RestLength < 0)
//     These constraints only activate when the particles are less than
//     -RestLength units apart.
// Angular constraints are implemented by using a Min constraint between
//   particles that share a comon horizontal or vertical neighbor.
//   e.g. if (A,B) are constrained with RestLength = X
//        and (B,C) are constrained with RestLength = Y
//        constrain (A,C) with RestLength = (X+Y)*.707
//        The angle between (A,B) and (B,C) will be limited to (roughly) 90 degrees.

// Setup a constraint between two particles.
// Use frac = 1.0 to create a MinMax (i.e. fixed distance) constraint
// Use frac < 0 to create an angular constraint, where -frac ~= cos((180 - minang)*.5)
//  minang = minimum angle between the segments connecting the particles
//   e.g. frac = -.866 would create a minimum angle between segments at about 120 degrees.
void dynClothLengthConstraintInitialize(DynClothLengthConstraint *constraint, DynCloth *cloth, S32 p1, S32 p2, F32 frac)
{
	Vec3 dvec;
	F32 dlen;
	constraint->P1 = (S16)p1;
	constraint->P2 = (S16)p2;
	subVec3(cloth->CurPos[p2], cloth->CurPos[p1], dvec);
	dlen = lengthVec3(dvec);
	constraint->RestLength = dlen * frac;
	constraint->InvLength = (dlen > 0.0f) ? 1.0f / dlen : 0.0f;

	if(cloth->InvMasses[p1] + cloth->InvMasses[p2] != 0.0f) {
		constraint->MassTot2 = 2.0f / (cloth->InvMasses[p1] + cloth->InvMasses[p2]);
	} else {
		// This shouldn't be used later, but we don't want a floating
		// point exception or an insane value in case it does.
		constraint->MassTot2 = 1.0f;
	}
}

// Process the constraint.
// Each constraint update moves the particles towards or away from each other
//   as dictated by the constraint.
// The more often the Update is called, the closer to their correct position
//   the particles will be.
// More massive particles are moved less than less massive particles.
// A particle that collided will have a greater effective mass by up to 2X.
//
// DynCloth->SubLOD affects the speed and accuracy of this code.
// Currently at SubLOD 0,1 the code is fully accurate,
//   at SubLOD 2 a sqrt approximation is used and collision amount is ignored
//   at SubLod 3 dynClothLengthConstraintFastUpdate() is called which also igores masses

F32 dynClothLengthConstraintUpdate(
	const DynClothLengthConstraint *constraint,
	DynCloth *cloth,
	float dt,
	float fLengthScale,
	bool bCrossConstraint) {

	F32 r = cloth->PointColRad;
	int P1 = constraint->P1;
	int P2 = constraint->P2;
	F32 imass1 = cloth->InvMasses[P1];
	F32 imass2 = cloth->InvMasses[P2];
	F32 tot_imass;
	F32 x1,y1,z1,x2,y2,z2,dx,dy,dz,dp;
	F32 fdiff;
	F32 RestLength;
	F32 LengthDiffRatio;

	tot_imass = imass1 + imass2;
	if (imass1 + imass2 == 0.0f)
		return 0.f; // both particles are immovable
	
	if (cloth->ColAmt[P1] > 0.0f)
		imass1 *= (1.f / (1.f + cloth->ColAmt[P1]*cloth->ColAmt[P1]*COL_MASS_FACTOR));
	if (cloth->ColAmt[P2] > 0.0f)
		imass2 *= (1.f / (1.f + cloth->ColAmt[P2]*cloth->ColAmt[P2]*COL_MASS_FACTOR));
	tot_imass = imass1 + imass2;

	// FIXME -Cliff
	if(!FINITEVEC3(cloth->CurPos[P1])) {
		setVec3same(cloth->CurPos[P1], 20);
	}
	if(!FINITEVEC3(cloth->CurPos[P2])) {
		setVec3same(cloth->CurPos[P2], 20);
	}

	x1 = cloth->CurPos[P1][0];
	y1 = cloth->CurPos[P1][1];
	z1 = cloth->CurPos[P1][2];
	x2 = cloth->CurPos[P2][0];
	y2 = cloth->CurPos[P2][1];
	z2 = cloth->CurPos[P2][2];
	dx = x2 - x1;
	dy = y2 - y1;
	dz = z2 - z1;
	dp = (dx*dx + dy*dy + dz*dz);

	RestLength = constraint->RestLength * fLengthScale;

	{
		F32 fEffectiveStiffness = cloth->stiffness;

		if(fEffectiveStiffness < 0.001) {
			fEffectiveStiffness = 0.001;
		}

		if(fEffectiveStiffness > 1) {
			fEffectiveStiffness = 1;
		}

		tot_imass /= fEffectiveStiffness;
	}

#if 1
	// Uses slow, accurate sqrt
	if (RestLength >= 0) {
		F32 deltalength = sqrtf(dp);
		if(deltalength != 0) {
			fdiff = (deltalength - RestLength) / (deltalength * tot_imass);
			LengthDiffRatio = (deltalength - RestLength) / RestLength;
		} else {
			// Something is wrong, and I hope it'll just sort itself out.
			fdiff = RestLength;
			LengthDiffRatio = -1.f;//co-located points when deltalength = 0
		}
	}else if (RestLength*RestLength > dp) {
		F32 deltalength = sqrtf(dp);
		if(deltalength != 0) {
			fdiff = (deltalength + RestLength) / (deltalength * tot_imass);
			LengthDiffRatio = (deltalength - fabsf(RestLength)) / RestLength;
		} else {
			// Something is wrong, and I hope it'll just sort itself out.
			fdiff = RestLength;
			LengthDiffRatio = -1.f;//co-located points when deltalength = 0
		}
	}else{
		//LengthDiffRatio = 1.f;
		return 0.f;//no restlength for comparison
		//fdiff = 0.0f;
	}
#else
	// Uses Newton-Raphson method for sqrt(dp) using sqrt(dp ~= RestLength)
	//  deltalength ~= .5*(RestLength + dp/RestLength)
	F32 MassTot2 = constraint->MassTot2;
	F32 rl2 = RestLength*RestLength;
	if (RestLength >= 0)
		fdiff = (0.5f - rl2/(rl2 + dp))*MassTot2;
	else if (rl2 > dp)
		fdiff = (0.5f - rl2/(rl2 + dp))*MassTot2;
	else
		//fdiff = 0;
		return 0.f;
#endif

	{
		F32 idiff1 = imass1*fdiff*dt;
		F32 idiff2 = -imass2*fdiff*dt;

		cloth->CurPos[P1][0] = x1 + dx*idiff1;
		cloth->CurPos[P1][1] = y1 + dy*idiff1;
		cloth->CurPos[P1][2] = z1 + dz*idiff1;

		cloth->CurPos[P2][0] = x2 + dx*idiff2;
		cloth->CurPos[P2][1] = y2 + dy*idiff2;
		cloth->CurPos[P2][2] = z2 + dz*idiff2;
	}

	return LengthDiffRatio;
}

// Uses fast square root and ignores masses
void dynClothLengthConstraintFastUpdate(const DynClothLengthConstraint *constraint, DynCloth *cloth)
{
	int P1 = constraint->P1;
	int P2 = constraint->P2;
	
	// Ignore non 0 masses
	F32 imass1 = cloth->InvMasses[P1];
	F32 imass2 = cloth->InvMasses[P2];
	if (imass1 + imass2 == 0.0f)
		return;

	{
		F32 RestLength = constraint->RestLength;
		
		F32 x1,y1,z1,x2,y2,z2,dx,dy,dz;
		x1 = cloth->CurPos[P1][0];
		y1 = cloth->CurPos[P1][1];
		z1 = cloth->CurPos[P1][2];
		x2 = cloth->CurPos[P2][0];
		y2 = cloth->CurPos[P2][1];
		z2 = cloth->CurPos[P2][2];
		dx = x2 - x1;
		dy = y2 - y1;
		dz = z2 - z1;
		// Uses Newton-Raphson method for sqrt(dp) using sqrt(dp ~= RestLength)
		//  deltalength ~= .5*(RestLength + dp/RestLength)
		{
			F32 dp = (dx*dx + dy*dy + dz*dz);
			F32 rl2 = RestLength*RestLength;
			F32 diff;
			if (RestLength >= 0)
				diff = (0.5f - rl2/(rl2 + dp));
			else if (rl2 > dp)
				diff = (0.5f - rl2/(rl2 + dp));
			else
				diff = 0;

			if (imass1 != 0)
			{
				cloth->CurPos[P1][0] = x1 + dx*diff;
				cloth->CurPos[P1][1] = y1 + dy*diff;
				cloth->CurPos[P1][2] = z1 + dz*diff;
			}
			if (imass2 != 0)
			{
				cloth->CurPos[P2][0] = x2 - dx*diff;
				cloth->CurPos[P2][1] = y2 - dy*diff;
				cloth->CurPos[P2][2] = z2 - dz*diff;
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////////

F32 dynClothAttachmentHarnessUpdate(DynCloth *pCloth, Vec3 vScale)
{
	DynClothAttachmentHarness *pHarness = pCloth->pHarness;
	U32 uiHook;
	Vec3 vTotalMoveChange = {0, 0, 0};

	F32 fMatWeight = 0;
	Mat4 xAvgMat = {0};
	Mat34 xAvgMat2 = {0};
	int i;

	bool canUseBoneAsFallback = false;

	// Figure out a matrix to use for stuff that's skinned entirely to cloth, if there were no cloth physics.
	if(!pHarness->pDrawSkel && pHarness->pAttachmentNode) {

		dynNodeGetWorldSpaceMat(pHarness->pAttachmentNode, xAvgMat, false);

		mat43to34(
			xAvgMat,
			xAvgMat2);

		canUseBoneAsFallback = true;

	} else if(pHarness->pDrawSkel) {

		mat34to43(
			pHarness->pDrawSkel->pCurrentSkinningMatSet->pSkinningMats[pHarness->uiBiggestMatIndex],
			xAvgMat);
		copyMat34(
			pHarness->pDrawSkel->pCurrentSkinningMatSet->pSkinningMats[pHarness->uiBiggestMatIndex],
			xAvgMat2);

		canUseBoneAsFallback = true;

	} else {

		copyMat4(unitmat, xAvgMat);

	}

	if(!devassertmsg(
		   pCloth->commonData.NumParticles == pHarness->uiNumHooks,
		   "Cloth particle count does not equal hook count!")) {
		return 0.0f;
	}

	// Iterate through hooks and figure out the new position based on the real bones.
	for (uiHook=0; uiHook<pHarness->uiNumHooks; ++uiHook)
	{
		bool foundABone = false;
		DynSoftwareSkinData* pSkinData = &pHarness->skinHooks[uiHook];
		Vec3 vTemp;
		Vec3 vVert;
		Vec3 vOrigHookPos;


		if(pCloth->InvMasses[uiHook] != 1.0) {

			float fTotalBoneInfluence = 0;
			float fInfluenceScale = 1.0;
			int clothBoneIgnore = -1;

			addVec3(pHarness->vBasePos, pSkinData->vVert, vVert);
			copyVec3(pHarness->vHookPositions[uiHook], vOrigHookPos);
			zeroVec3(pHarness->vHookPositions[uiHook]);
			zeroVec3(pHarness->vHookNormals[uiHook]);

			// Determine the total influence of bones that ARE NOT cloth so we can scale up to get the correct position,
			// ignoring cloth influence.
			for (i=0; i<4; ++i) {
				if(pSkinData->uiSkinningMatIndex[i] != pHarness->uiClothBoneIndex) {
					fTotalBoneInfluence += pSkinData->fWeight[i];
				}
			}
			if(fTotalBoneInfluence > 0.0) {
				fInfluenceScale = 1.0 / fTotalBoneInfluence;
			}

			for (i=0; i<4; ++i) {

				if(pSkinData->uiSkinningMatIndex[i] != pHarness->uiClothBoneIndex && pSkinData->fWeight[i] > 0.0f) {

					if (pHarness->pDrawSkel) {

						// Cloth is attached to a skeleton.

						foundABone = true;

						mulVecMat34(vVert, pHarness->pDrawSkel->pCurrentSkinningMatSet->pSkinningMats[pSkinData->uiSkinningMatIndex[i]], vTemp);
						scaleAddVec3(vTemp, pSkinData->fWeight[i] * fInfluenceScale, pHarness->vHookPositions[uiHook], pHarness->vHookPositions[uiHook]);

						mulVecW0Mat34(pSkinData->vNorm, pHarness->pDrawSkel->pCurrentSkinningMatSet->pSkinningMats[pSkinData->uiSkinningMatIndex[i]], vTemp);
						scaleAddVec3(vTemp, -pSkinData->fWeight[i] * fInfluenceScale, pHarness->vHookNormals[uiHook], pHarness->vHookNormals[uiHook]);

					} else if(pHarness->pAttachmentNode) {

						// Cloth isn't attached to a skeleton, but it does
						// have an attachment node, so just move all
						// non-cloth vertices to match the attachment node.
						// (FX cloth.)

						Vec3 vDiff;

						Mat4 mat;
						dynNodeGetWorldSpaceMat(pHarness->pAttachmentNode, mat, false);

						vVert[0] *= vScale[0];
						vVert[1] *= vScale[1];
						vVert[2] *= vScale[2];

						mulVecMat4(vVert, mat, vTemp);
						scaleAddVec3(vTemp, pSkinData->fWeight[i] * fInfluenceScale, pHarness->vHookPositions[uiHook], vTemp);

						// Track distance moved.
						subVec3(vTemp, vOrigHookPos, vDiff);
						addVec3(vDiff, vTotalMoveChange, vTotalMoveChange);

						copyVec3(vTemp, pHarness->vHookPositions[uiHook]);

						foundABone = true;

					}
				}
			}

			normalVec3(pHarness->vHookNormals[uiHook]);

			// FIXME: Hack for attachment normals ending up inverted.
			//scaleVec3(pHarness->vHookNormals[uiHook], -1.0f, pHarness->vHookNormals[uiHook]);

		}

		if(!foundABone && !(canUseBoneAsFallback && pHarness->pAttachmentNode)) {

			// No skeleton and no attachment node? This probably
			// shouldn't happen if the cloth is set up correctly.
			zeroVec3(pHarness->vHookPositions[uiHook]);

		} else if(!foundABone && canUseBoneAsFallback && pHarness->pAttachmentNode) {

			dynNodeGetWorldSpaceMat(
				pHarness->pAttachmentNode, xAvgMat, false);
			addVec3(pHarness->vBasePos, pSkinData->vVert, vVert);
			mulVecMat34(vVert, xAvgMat2, vTemp);
			copyVec3(vTemp, pHarness->vHookPositions[uiHook]);
		}

	}

	copyMat4(xAvgMat, pHarness->xAvgMat);

	return lengthVec3(vTotalMoveChange);
}

