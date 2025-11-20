#include <stdlib.h>
#include "DynCloth.h"
#include "dynClothPrivate.h"
#include "dynFxManager.h"
#include "wlCostume.h"
#include "wlModel.h"
#include "wlModelLoad.h"
#include "ScratchStack.h"
#include "error.h"
#include "dynClothInfo.h"
#include "dynSkeleton.h"
#include "Quat.h"
#include "wininclude.h"
#include "dynclothmesh.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

//////////////////////////////////////////////////////////////////////////////
// This module contains utility functions for creating DynCloth structure
//
// This module also has code for attaching a "harness" to a DynCloth
//
//////////////////////////////////////////////////////////////////////////////

//============================================================================
// GENERAL PURPOSE BUILD FUNCTIONS
//============================================================================

//////////////////////////////////////////////////////////////////////////////
// Generates a cloth from an array of positions and an array of masses/
// Input: A two dimentional array of points with optional masses
void dynClothSetPointsMasses(DynCloth *cloth, Vec3 *points, F32 *masses)
{
	int p;
	int npoints = cloth->commonData.NumParticles;
	for (p=0; p<npoints; p++)
	{
		if (points)
		{
			copyVec3(points[p],cloth->OrigPos[p]);
			copyVec3(points[p],cloth->OldPos[p]);
			copyVec3(points[p],cloth->CurPos[p]);
		}
		if (masses)
			cloth->InvMasses[p] = masses[p] > 0.0f ? 1.0f / masses[p] : 0.0f; // Use 0 for infinite mass (fixed)
	}
}

//============================================================================
// MESH CONVERSION FUNCTIONS
//============================================================================

//////////////////////////////////////////////////////////////////////////////
// dynClothBuildGridFromTriList()

int dynClothBuildGridFromTriList(DynCloth *cloth,
							  int npoints, const Vec3 *points, const Vec2 *texcoords,
							  const F32 *masses, F32 mass_y_scale, F32 stiffness,
							  int ntris, const int *tris,
							  const Vec3 scale, const char *pcModelName, int lodNum)
{
	int i;
	Vec3 *newpoints;
	F32 *newmasses;
	S32 *eaEyeletOrder = NULL;
	
	newpoints = CLOTH_MALLOC(Vec3, npoints);
	newmasses = CLOTH_MALLOC(F32, npoints);
	cloth->skinnedPositions = NULL;

	for(i = 0; i < npoints; i++) {
		newmasses[i] = masses[i];

		// FIXME: This used to be conditional based on skinning information. Now it's redundant.
		ea32Push(&eaEyeletOrder, i);

		copyVec3(points[i], newpoints[i]);
	}

	// Save the eyelets.
	if (ea32Size(&eaEyeletOrder) > 0) {
		cloth->NumEyelets = ea32Size(&eaEyeletOrder);
		cloth->EyeletOrder = CLOTH_MALLOC(S32, cloth->NumEyelets);
		memcpy(cloth->EyeletOrder, eaEyeletOrder, sizeof(S32) * cloth->NumEyelets);
	}

	if(eaEyeletOrder) ea32Destroy(&eaEyeletOrder);

	// Create Particles the new way!
	if (!dynClothCreateParticles(cloth, npoints))
	{
		CLOTH_FREE(newpoints);
		CLOTH_FREE(newmasses);
		CLOTH_FREE(cloth->skinnedPositions);
		return dynClothErrorf("Unable to create cloth particles");
	}

	// Copy texture coordinates
	if (dynDebugState.cloth.bClothDebug)
		printf("DynCloth TextureCoordinates:\n");

	for (i=0; i<npoints; i++) {
		copyVec2(texcoords[i], cloth->renderData.TexCoords[i]);
	}

	// Copy points and masses
	dynClothSetPointsMasses(cloth, newpoints, newmasses);

	CLOTH_FREE(newpoints);
	CLOTH_FREE(newmasses);

	// constraints
	{
		int clothflags = CLOTH_FLAGS_CONNECTIONS(2);
		F32 constraint_scale[1];
		constraint_scale[0] = stiffness; // only one secondary constraint used
		dynClothCalcLengthConstraintsAndSetupTessellation(cloth, clothflags, 1, constraint_scale, ntris, tris, pcModelName, lodNum);
	}

	{
		// Fix up length constrains for things that aren't uniformly scaled.
		int j;
		for(j = 0; j < cloth->NumLengthConstraints; j++) {

			Vec3 delta;
			Vec3 scaledDelta;
			F32 scaledDeltaLength;
			F32 origLength;
			F32 scaleFactor;

			// Get the original delta.
			subVec3(
				cloth->OrigPos[cloth->LengthConstraintsInit[j].P1],
				cloth->OrigPos[cloth->LengthConstraintsInit[j].P2],
				delta);

			// Get our new delta.
			mulVecVec3(scale, delta, scaledDelta);

			// Get the scaling ratio of the two lengths.
			scaledDeltaLength = lengthVec3(scaledDelta);
			origLength = lengthVec3(delta);
			scaleFactor = scaledDeltaLength / origLength;

			// Multiply the rest length by that ratio.
			cloth->LengthConstraintsInit[j].RestLength *= scaleFactor;
		}

		// Scale positions for the particles too.
		for(j = 0; j < cloth->commonData.NumParticles; j++) {
			int k;
			for(k = 0; k < 3; k++) {
				cloth->OldPos[j][k] *= scale[k];
				cloth->CurPos[j][k] *= scale[k];
			}
		}
	}
	
	// Calculate stuff
	dynClothCopyToRenderData(cloth, NULL, CCTR_COPY_ALL);
	dynClothUpdateNormals(&cloth->renderData);

	return 0; // no error
}

//////////////////////////////////////////////////////////////////////////////
// dynClothBuildAttachHarness() takes as input an array of Hooks
// and associates each hook with the appropriate eyelet.

typedef struct {
	int idx;
	int iEyeletOrderIdx;
	Vec3 pos;
} HookInfo;

int compare_hookinfo(const void *va, const void *vb)
{
	HookInfo *a = (HookInfo*)va;
	HookInfo *b = (HookInfo*)vb;
	S32 dx = a->iEyeletOrderIdx - b->iEyeletOrderIdx;
	if (dx < 0) return -1;
	else if (dx == 0) return 0;
	else return 1;
}

int dynClothBuildAttachHarness(DynCloth *cloth, int hooknum, Vec3 *hooks)
{
	int i,h;
	int hookidx = 0;
	int eyenum = cloth->commonData.NumParticles;
	HookInfo *hooklist;

	if (eyenum != cloth->NumEyelets) {
		return dynClothErrorf("dynClothBuildAttachHarness: Somehow the NumEyelets calculation went bad");
	}

	hooklist = ScratchAlloc(sizeof(HookInfo)*hooknum);

	for (i = 0, h = 0; h < eyenum && i < hooknum; i++) {
		// New, unique point
		hooklist[h].idx = i;
		hooklist[h].iEyeletOrderIdx = cloth->EyeletOrder[h];
		copyVec3(hooks[i], hooklist[h].pos);
		h++;
	}

	if (dynDebugState.cloth.bClothDebug) {
		printf("Found %d hooks on harness, %d eyelets on cape\n", h, eyenum);
	}

	hooknum = h;

	if (hooknum > eyenum)
		hooknum = eyenum; // skip extra hooks

	qsort(hooklist, hooknum, sizeof(HookInfo), compare_hookinfo);

	for (i=0; i<cloth->commonData.NumParticles; i++)
	{
		if (hookidx >= hooknum) {
			ScratchFree(hooklist);
			return dynClothErrorf("dynClothBuildAttachHarness: too many eyelets (%d hooks on harness, %d eyelets on cape)", h, eyenum);
		}

		for (h=0; h<hooknum; ++h)
			if (hookidx == hooklist[h].idx)
				break;
		dynClothAddAttachment(cloth, i, h, h, 1.0f);
		hookidx++;
	}

	ScratchFree(hooklist);
	return 0; // no error
}

//////////////////////////////////////////////////////////////////////////////

DynClothAttachmentHarness* dynClothAttachmentHarnessCreate(const GeoMeshTempData* pMeshTempData, const DynDrawSkeleton* pDrawSkel, const DynDrawModel* pGeo, U32 uiNumHooks, S32* puiHookIndices, int lodNum)
{
  #define BAD_BONE 0xffffffff

	DynClothAttachmentHarness* pNew = CLOTH_MALLOC(DynClothAttachmentHarness, 1);
	U32 uiHook;
	U32 clothBoneIndex = BAD_BONE;
	pNew->uiNumHooks = uiNumHooks;
	pNew->pDrawSkel = pDrawSkel;

	if(pGeo) {
		copyVec3(pGeo->vBaseAttachOffset, pNew->vBasePos);
	} else {
		setVec3same(pNew->vBasePos, 0);
	}

	pNew->skinHooks      = CLOTH_MALLOC(DynSoftwareSkinData, uiNumHooks);
	pNew->vHookNormals   = CLOTH_MALLOC(Vec3, uiNumHooks);
	pNew->vHookPositions = CLOTH_MALLOC(Vec3, uiNumHooks);

	if(pGeo) {

		// Find the cloth bone's index.
		FOR_EACH_IN_EARRAY(pMeshTempData->model->model_parent->header->bone_names, const char, pcBoneName)
		{
			if (stricmp(pcBoneName, "Cloth")==0) {
				clothBoneIndex = ipcBoneNameIndex;
			}
		}
		FOR_EACH_END;

		if(clothBoneIndex != BAD_BONE) {
			pNew->uiClothBoneIndex = pGeo->apuiBoneIdxs[lodNum][clothBoneIndex];
		} else {
			pNew->uiClothBoneIndex = BAD_BONE;
		}

	} else {

		pNew->uiClothBoneIndex = BAD_BONE;

	}

	for (uiHook=0; uiHook<uiNumHooks; ++uiHook)
	{
		S32 iVert = puiHookIndices[uiHook];
		DynSoftwareSkinData* pHook = &pNew->skinHooks[uiHook];
		S32 i;

		copyVec3(pMeshTempData->verts[iVert], pHook->vVert);

		// FIXME: Why does this need to be inverted? -Cliff
		scaleVec3(pMeshTempData->norms[iVert], -1, pHook->vNorm);

		for (i=0; i<4; ++i)
		{
			if(pMeshTempData->weights) {
				pHook->fWeight[i] = (F32)pMeshTempData->weights[iVert * 4 + i] / 255.0f;
				if (pHook->fWeight[i] > 0.0f)
				{
					U8 uiBoneIndex = pMeshTempData->boneidxs[iVert * 4 + i] / 3; // They are stored on disk as direct indices into the skinning data, so we need to divide by 3 to correct for that.

					if(pGeo) {
						pHook->uiSkinningMatIndex[i] = pGeo->apuiBoneIdxs[lodNum][uiBoneIndex];
					} else {
						pHook->uiSkinningMatIndex[i] = 0;
					}

					pHook->uiBoneIndex[i] = uiBoneIndex;

				} else {
					pHook->uiSkinningMatIndex[i] = 0;
					pHook->uiBoneIndex[i] = BAD_BONE;
				}
			} else {
				pHook->uiSkinningMatIndex[i] = 0;
				pHook->uiBoneIndex[i] = BAD_BONE;
			}
		}
	}

	// Now find the most influential bone so we have something to use for a "stiff" position.
	if(pNew->pDrawSkel) {

		// Determine which of the skinning matrices has the most influence on
		// the cloth and use that as the "totally stiff" position.

		int i;
		DynClothAttachmentHarness *pHarness = pNew;

		U32 uiCurMat;
		F32 *fMatWeights = NULL;
		U32 *uiBoneIndexes = NULL; // Maps skinning matrix index back to bone index.

		F32 fBiggestMatWeight = 0;
		U32 uiBiggestMatIndex = 0;

		const DynDrawSkeleton *pCurSkel = pNew->pDrawSkel;

		// Go through all hooks and add up skinning weights per-bone.
		for(uiHook=0; uiHook<pHarness->uiNumHooks; ++uiHook) {

			DynSoftwareSkinData* pSkinData = &pHarness->skinHooks[uiHook];

			for (i=0; i<4; ++i) {
				if(pSkinData->fWeight[i] > 0.0f) {

					eafSetSize(&fMatWeights, pSkinData->uiSkinningMatIndex[i]+1);

					// Ignore cloth bones. Add influence for anything else.
					if(pSkinData->uiBoneIndex[i] != clothBoneIndex) {
						fMatWeights[pSkinData->uiSkinningMatIndex[i]] += pSkinData->fWeight[i];
					}

					ea32SetSize(&uiBoneIndexes, pSkinData->uiSkinningMatIndex[i]+1);
					uiBoneIndexes[pSkinData->uiSkinningMatIndex[i]] = pSkinData->uiBoneIndex[i];
				}
			}
		}

		// Find biggest weighted bone.
		for(uiCurMat = 0; uiCurMat < (unsigned int)eafSize(&fMatWeights); uiCurMat++) {
			if(fMatWeights[uiCurMat] > fBiggestMatWeight) {
				fBiggestMatWeight = fMatWeights[uiCurMat];
				uiBiggestMatIndex = uiCurMat;
			}
		}

		if(dynDebugState.cloth.bDebugStiffBoneSelection) {

			printf(
				"Selected biggest bone: %u/%u (%f) %s\n",
				uiBiggestMatIndex,
				uiBoneIndexes[uiBiggestMatIndex],
				fMatWeights[uiBiggestMatIndex],
				pMeshTempData->model->model_parent->header->bone_names[
					uiBoneIndexes[uiBiggestMatIndex]]);

			printf(
				"Cloth bone:            %u (%f)\n",
				clothBoneIndex,
				fMatWeights[
					pGeo->apuiBoneIdxs[lodNum][clothBoneIndex]]);
		}

		eafDestroy(&fMatWeights);
		ea32Destroy(&uiBoneIndexes);

		pHarness->uiBiggestMatIndex = uiBiggestMatIndex;
	}

	return pNew;
}

void dynClothAttachmentHarnessDestroy(DynClothAttachmentHarness* pHarness) {
	CLOTH_FREE(pHarness->skinHooks);
	CLOTH_FREE(pHarness->vHookNormals);
	CLOTH_FREE(pHarness->vHookPositions);
	CLOTH_FREE(pHarness);
}

typedef struct ClothCreationData
{
	DynCloth* pCloth;
	DynClothAttachmentHarness* pHarness;
	DynDrawSkeleton* pDrawSkel;
	DynDrawModel* pGeo;
	bool bAbort;
	float fStiffness;
	Vec3 vScale;

	const char *pcModelName;
	int lodNum;

} ClothCreationData;

void createClothFromGeoData( ClothCreationData* pData, const GeoMeshTempData* pMeshTempData )
{
	F32* pfWeights = ScratchAlloc(pMeshTempData->vert_count * sizeof(F32));
	S32 iVert;
	S32* eaAttachmentVerts = NULL;
	DynCloth* pCloth = pData->pCloth;
	Vec3 vScale;

	const char *pcModelName = pData->pcModelName;
	int lodNum = pData->lodNum;

	U32 *eaClothBoneIdx = NULL;

	// Find the cloth bone's index.
	FOR_EACH_IN_EARRAY(pMeshTempData->model->model_parent->header->bone_names, const char, pcBoneName)
	{
		if (stricmp(pcBoneName, "Cloth")==0) {
			ea32Push(&eaClothBoneIdx, ipcBoneNameIndex);
		}
	}
	FOR_EACH_END

	if(ea32Size(&eaClothBoneIdx) < 1) {

		// No cloth bones? Bail out at this stage. No further LODs will be created. If
		// this is the first LOD, then it's probably an error to use this as a cloth
		// piece. Either it lacks cloth bones because it was never meant to be cloth or
		// the artist forgot to add cloth skinning.
		if(lodNum == 0) {
			Errorf("Cloth: Attempting to load a model as cloth, but it has no cloth bones: %s", pMeshTempData->model->model_parent->name);
		}

		ScratchFree(pfWeights);
		return;
	}

	// Go through all the vertices and find the ones that aren't cloth
	// at all (attachments) and store the cloth weights for all the
	// vertices in pfWeights.
	for (iVert=0; iVert<pMeshTempData->vert_count; ++iVert)
	{
		int i;
		F32 fClothWeight = 0.0f;

		if(pMeshTempData->boneidxs) {

			// Go through the 4 bones this vertex might be attached to.
			for (i=0; i<4; ++i)
			{
				// Bone index is a direct index into skinning data, so we
				// have to divide by three.
				U8 uiBoneIdx = pMeshTempData->boneidxs[iVert * 4 + i] / 3;
				bool bClothBone = false;
				int j;

				for(j = 0; j < ea32Size(&eaClothBoneIdx); j++) {
					if(eaClothBoneIdx[j] == uiBoneIdx) {
						bClothBone = true;
						break;
					}
				}

				if(bClothBone) {
					if (pMeshTempData->weights[iVert * 4 + i] > 0) {
						// Found the cloth bone. Get how clothy this vert is.
						fClothWeight = (F32)pMeshTempData->weights[iVert * 4 + i] / 255.0f;
						break;
					}
				}
			}
		}

		ea32Push(&eaAttachmentVerts, iVert);

		pfWeights[iVert] = fClothWeight;
	}

	// Get the appropriate scaling amount based on what it's attached
	// to.
	if (pData->pGeo && pData->pGeo->pAttachmentNode) {
		dynNodeGetWorldSpaceScale(pData->pGeo->pAttachmentNode, vScale);
	} else {
		copyVec3(pData->vScale, vScale);
	}

	copyVec3(vScale, pData->pCloth->vOriginalScale);

	dynClothBuildGridFromTriList(pCloth, pMeshTempData->vert_count, pMeshTempData->verts, pMeshTempData->sts, pfWeights, 0.8f, 0.9f, pMeshTempData->tri_count, pMeshTempData->tris, vScale, pcModelName, lodNum);
	pData->pHarness = dynClothAttachmentHarnessCreate(pMeshTempData, pData->pDrawSkel, pData->pGeo, ea32Size(&eaAttachmentVerts), eaAttachmentVerts, lodNum);
	pCloth->pHarness = pData->pHarness;
	if(pData->pHarness) {
		Vec3 attachmentScale = {1, 1, 1};
		dynClothAttachmentHarnessUpdate(pCloth, attachmentScale);
		dynClothBuildAttachHarness(pCloth, pData->pHarness->uiNumHooks, pData->pHarness->vHookPositions);
	}

	// Save model's texel density.
	pCloth->fUvDensity = pMeshTempData->model->uv_density;

	ScratchFree(pfWeights);
	ea32Destroy(&eaAttachmentVerts);
	ea32Destroy(&eaClothBoneIdx);
}

DynClothObject* dynClothObjectSetup(const char *pcClothInfo, const char *pcClothColInfo, Model *pModel, DynDrawModel* pGeo, DynDrawSkeleton* pDrawSkel, const DynNode* pAttachNode, Vec3 vScale, DynFx *pFx) {

	int i;
	DynClothObject* pNew;
	DynCloth* pCloth;
	ClothCreationData data;
	F32 fStiffness = 0.8f;
	F32 fDrag = 0.1f;
	F32 fParticleCollisionRadius = 0.2f;
	F32 fParticleCollisionRadiusMax = 0.0f;
	F32 fParticleCollisionMaxSpeed = 0.0f;
	bool bLODLoadFail = false;

	// Stuff that we want to copy from the DynClothInfo while we have the lock on all the
	// reference junk, but is only required once we're done.
	F32 fWindRippleScale = 1.0f;
	F32 fWindRippleWavePeriodScale = 1.0f;
	F32 fWindRippleWaveTimeScale = 1.0f;
	F32 fFakeWindFromMovement = 1.0f;
	F32 fNormalWindFromMovement = 1.0f;
	F32 fTimeScale = 1.0f;
	F32 fGravityScale = 1.0f;
	F32 fClothBoneInfluenceExponent = 2.0f;

	if(dynDebugState.cloth.bDisableCloth) {
		return NULL;
	}

	// Make sure all the models are loaded.
	for(i = 0; i < eaSize(&pModel->model_lods); i++) {
		ModelLOD *modelLod = modelLODLoadAndMaybeWait(pModel, i, false);
		if(!modelLod) {
			bLODLoadFail = true;
		}
	}

	if(bLODLoadFail) {
		// Need to wait for the model LODs to load.
		return NULL;
	}

	pNew = dynClothObjectCreate();

	dynClothLockThreadData(); {

		DynClothInfo* pInfo = NULL;

		if (pcClothInfo)
			SET_HANDLE_FROM_STRING("DynClothInfo", pcClothInfo, pNew->hInfo);

		if (pcClothColInfo)
			SET_HANDLE_FROM_STRING("DynClothCollision", pcClothColInfo, pNew->hColInfo);

		pInfo = GET_REF(pNew->hInfo);

		if (pInfo) {
			fStiffness = pInfo->fStiffness;
			fDrag = pInfo->fDrag;
			pNew->bTessellate = pInfo->bTessellate;
			pNew->iNumIterations = pInfo->iNumIterations;
			pNew->fWorldMovementScale = pInfo->fWorldMovementScale;
			pNew->fWindSpeedScale = pInfo->fWindSpeedScale;

			fParticleCollisionRadius = pInfo->fParticleCollisionRadius;
			fParticleCollisionRadiusMax = pInfo->fParticleCollisionRadiusMax;
			fParticleCollisionMaxSpeed = pInfo->fParticleCollisionMaxSpeed;

			fWindRippleScale = pInfo->fWindRippleScale;
			fWindRippleWavePeriodScale = pInfo->fWindRippleWavePeriodScale;
			fWindRippleWaveTimeScale = pInfo->fWindRippleWaveTimeScale;
			fFakeWindFromMovement = pInfo->fFakeWindFromMovement;
			fNormalWindFromMovement = pInfo->fNormalWindFromMovement;
			fTimeScale = pInfo->fTimeScale;
			fGravityScale = pInfo->fGravityScale;

			fClothBoneInfluenceExponent = pInfo->fClothBoneInfluenceExponent;
		}

	} dynClothUnlockThreadData();

	pNew->pModel = pModel;

	for(i = 0; i < eaSize(&pModel->model_lods); i++) {

		// One last check to make sure everything is really loaded.
		ModelLOD *modelLod = modelLODLoadAndMaybeWait(pModel, i, false);
		if(!modelLod) break;

		dynClothObjectAddLOD(pNew, 0, 1);

		pCloth = pNew->eaLODs[i]->pCloth;

		memset(&data, 0, sizeof(data));

		data.pCloth = pCloth;
		data.pDrawSkel = pDrawSkel;
		data.pGeo = pGeo;
		data.bAbort = false;
		data.pHarness = NULL;

		data.pcModelName = pModel->name;
		data.lodNum = i;

		data.fStiffness = fStiffness;

		copyVec3(vScale, data.vScale);

		pCloth->stiffness = fStiffness;
		pCloth->fClothBoneInfluenceExponent = fClothBoneInfluenceExponent;

		geoProcessTempData(createClothFromGeoData, &data, pModel, i, NULL, true, true, true, true, NULL);
		if (data.bAbort)
		{
			dynClothObjectDelete(pNew);
			return NULL;
		}

		pNew->eaLODs[i]->pCloth->pHarness = data.pHarness;

		if(!data.pHarness) {
			// Didn't create the harness. Stop making LODs here.
			dynClothObjectRemoveLastLOD(pNew);
			break;
		}

		if(pGeo) {
			pNew->eaLODs[i]->pCloth->pHarness->pAttachmentNode = pGeo->pAttachmentNode;
			if (pGeo->pcOrigAttachmentBone)
			{
				const DynNode* pOrigAttach = dynSkeletonFindNode(pDrawSkel->pSkeleton, pGeo->pcOrigAttachmentBone);
				if (pOrigAttach)
					pNew->eaLODs[i]->pCloth->pHarness->pAttachmentNode = pOrigAttach;
			}
		} else {
			pNew->eaLODs[i]->pCloth->pHarness->pAttachmentNode = pAttachNode;
		}
	}

	dynClothObjectSetColRad(
		pNew,
		fParticleCollisionRadius,
		fParticleCollisionRadiusMax,
		fParticleCollisionMaxSpeed);

	dynClothObjectSetGravity(pNew, -32.0f);
	dynClothObjectSetDrag(pNew, fDrag);

	dynClothObjectCreateMeshes(pNew, pModel);

	{
		int iCurrentLOD = 0;

		// FIXME: We can probably arrange this in such a way that does not require locking
		// and unlocking the shared cloth data twice for this one function.
		dynClothLockThreadData(); {

			DynClothCollisionInfo* pColInfo = GET_REF(pNew->hColInfo);

			// Set up collision stuff
			if (pColInfo) {

				DynClothCollisionSet *pClothCollisionSet = dynClothCollisionSetCreate(
					pColInfo,
					pNew,
					pDrawSkel ? pDrawSkel->pSkeleton : NULL,
					(!pDrawSkel) ? pAttachNode : NULL,
					pFx);

				if (SAFE_MEMBER2(pDrawSkel,pSkeleton,pParentSkeleton)	&&
					pDrawSkel->pSkeleton->bRider						&&
					pDrawSkel->pSkeleton->pParentSkeleton->bMount)
				{
					WLCostume *pMountCostume = GET_REF(pDrawSkel->pSkeleton->pParentSkeleton->hCostume);
					DynClothCollisionInfo *pMountColInfo;

					if (pMountCostume &&
						pMountCostume->pcMountClothCollisionInfo &&
						(pMountColInfo = RefSystem_ReferentFromString("DynClothCollision", pMountCostume->pcMountClothCollisionInfo)))
					{
						dynClothCollisionSetAppend(
							pClothCollisionSet,
							pMountColInfo,
							pNew,
							pDrawSkel->pSkeleton->pParentSkeleton);
					}
				}

				pNew->pCollisionSet = pClothCollisionSet;
			}

		} dynClothUnlockThreadData();

		// Copy wind and simulation scaling info.
		for(iCurrentLOD = 0; iCurrentLOD < eaSize(&pNew->eaLODs); iCurrentLOD++) {

			pNew->eaLODs[iCurrentLOD]->pCloth->fWindRippleScale           = fWindRippleScale;
			pNew->eaLODs[iCurrentLOD]->pCloth->fWindRippleWavePeriodScale = fWindRippleWavePeriodScale;
			pNew->eaLODs[iCurrentLOD]->pCloth->fWindRippleWaveTimeScale   = fWindRippleWaveTimeScale;
			pNew->eaLODs[iCurrentLOD]->pCloth->fFakeWindFromMovement      = fFakeWindFromMovement;
			pNew->eaLODs[iCurrentLOD]->pCloth->fNormalWindFromMovement    = fNormalWindFromMovement;
			pNew->eaLODs[iCurrentLOD]->pCloth->fTimeScale                 = fTimeScale;
			pNew->eaLODs[iCurrentLOD]->pCloth->fGravityScale              = fGravityScale;

			pNew->eaLODs[iCurrentLOD]->pCloth->pClothObject = pNew;
		}
	}

	pNew->pGeo = pGeo;

	return pNew;
}

DynClothObject* dynClothObjectSetupFromDynDrawModel(DynDrawModel* pGeo, DynDrawSkeleton* pDrawSkel) {
	Vec3 scale = {1, 1, 1};
	return dynClothObjectSetup(pGeo->pcClothInfo, pGeo->pcClothColInfo, pGeo->pModel, pGeo, pDrawSkel, NULL, scale, NULL);
}





