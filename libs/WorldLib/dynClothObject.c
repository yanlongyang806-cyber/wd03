#include "dynFxParticle.h"
#include "DynCloth.h"
#include "dynClothPrivate.h"
#include "DynClothMesh.h"
#include "dynClothInfo.h"
#include "dynClothCollide.h"
#include "dynWind.h"
#include "dynFxManager.h"
#include "wininclude.h"
#include "wlState.h"
#include "timing_profiler.h"
#include "error.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

static DynClothObject** eaDynClothObjects = NULL;

// For debugging.
DynClothObject ***dynClothGetAllClothObjects(void) {
	return &eaDynClothObjects;
}

#define CLOTH_WAKE_TIME 0.2f

//============================================================================
// DynClothLOD and DynClothObject Organization
//
// One DynClothObject contains multiple DynClothLODs, based on the LODs of the
// model the cloth is based on. There is a direct mapping from the model LODs
// to the DynClothLODs.
//
// Each vertex on the model becomes a simulated particle and each edge becomes
// a length constraint.
//
// The old system had LODs and SubLODs, so some of the old code and comments
// may still reference SubLODs.

//============================================================================
// CLOTH LOD
//============================================================================

static DynClothLOD *dynClothLODCreate(int minsublod, int maxsublod)
{
	DynClothLOD *lod = CLOTH_MALLOC(DynClothLOD, 1);
	lod->pCloth = dynClothCreateEmpty();
	lod->pMesh = NULL;
	return lod;
}

static void dynClothLODDelete(DynClothLOD *lod)
{
	if(lod->pMesh)
		dynClothMeshDelete(lod->pMesh);
	
	if(lod->pCloth->pHarness)
		dynClothAttachmentHarnessDestroy(lod->pCloth->pHarness);

	if (lod->pCloth)
		dynClothDelete(lod->pCloth);

	CLOTH_FREE(lod);
}

// Create the DynClothMesh data
static int dynClothLODCreateMeshes(DynClothLOD *lod, Model *model, int lodNum) {
	lod->pMesh = dynClothCreateMeshIndices(lod->pCloth, model, lodNum);
	return lod->pMesh ? 0 : -1;
}

DynClothMesh *dynClothLODGetMesh(DynClothLOD *lod) {
	return lod->pMesh;
};

//============================================================================
// CLOTH OBJECT
//============================================================================

DynClothObject *dynClothObjectCreate(void)
{
	DynClothObject *obj = CLOTH_MALLOC(DynClothObject, 1);

	obj->eaLODs = NULL;
	obj->iCurrentLOD = 0;
	obj->iLastLOD = -1;

	obj->bQueueModelReset = false;
	obj->fMaxConstraintRatio = 0.f;
	obj->fMinConstraintRatio = 0.f;

	obj->fSleepTimer = CLOTH_WAKE_TIME;

	dynClothLockThreadData(); {
		eaPush(&eaDynClothObjects, obj);
	} dynClothUnlockThreadData();

	return obj;
}

void dynClothObjectDelete(DynClothObject *obj)
{
	int i;

	// Free collision
	if(obj->pCollisionSet) {
		dynClothCollisionSetDestroy(obj->pCollisionSet);
	}
	for (i=0; i<obj->NumCollidables; i++) {
		dynClothColDelete(&obj->Collidables[i]);
		obj->OldCollidables[i].Mesh = NULL;
		dynClothColDelete(&obj->OldCollidables[i]);
	}
	CLOTH_FREE(obj->Collidables);
	CLOTH_FREE(obj->OldCollidables);

	// Free LODs
	for (i = 0; i < eaSize(&obj->eaLODs); i++)
		dynClothLODDelete(obj->eaLODs[i]);

	eaDestroy(&obj->eaLODs);

	dynClothLockThreadData(); {
		REMOVE_HANDLE(obj->hInfo);
		REMOVE_HANDLE(obj->hColInfo);
		eaFindAndRemoveFast(&eaDynClothObjects, obj);
	} dynClothUnlockThreadData();

	CLOTH_FREE(obj);
	if (obj == dynDebugState.cloth.pDebugClothObject)
		dynDebugState.cloth.pDebugClothObject = NULL;


}


//////////////////////////////////////////////////////////////////////////////

int dynClothObjectAddLOD(DynClothObject *obj, int minsublod, int maxsublod) {

	eaPush(&obj->eaLODs, dynClothLODCreate(minsublod, maxsublod));

	return eaSize(&obj->eaLODs) - 1;
}

// This is in case LOD creation fails when partially complete. Clean up
// the partial LOD and make everything consistent so we can still use
// everything that's been made successfully up to that point.
int dynClothObjectRemoveLastLOD(DynClothObject *obj) {

	if(!eaSize(&obj->eaLODs)) {
		return 0;
	}

	// Clean up the LOD.
	if(obj->eaLODs[eaSize(&obj->eaLODs) - 1]) {
		dynClothLODDelete(obj->eaLODs[eaSize(&obj->eaLODs) - 1]);
		eaPop(&obj->eaLODs);
	}
	
	return eaSize(&obj->eaLODs) - 1;
}

DynCloth *dynClothObjGetLODCloth(DynClothObject *obj, int lod)
{
	if (lod == -1)
		lod = obj->iCurrentLOD;
	return obj->eaLODs[lod]->pCloth;
}

DynClothMesh *dynClothObjGetLODMesh(DynClothObject *obj, int lod, int sublod)
{
	if (lod == -1)
		lod = obj->iCurrentLOD;
	return dynClothLODGetMesh(obj->eaLODs[lod]);
}

// Set the actual LOD
int dynClothObjectSetLOD(DynClothObject *obj, int lod) {
	if(lod != obj->iCurrentLOD) obj->fSleepTimer = CLOTH_WAKE_TIME;
	obj->iCurrentLOD = MINMAX(lod, 0, eaSize(&obj->eaLODs) - 1);
	return obj->iCurrentLOD;
}

//////////////////////////////////////////////////////////////////////////////
// Create Meshes for each LOD DynCloth.

int dynClothObjectCreateMeshes(DynClothObject *obj, Model *model) {
	
	int i, res = 0;
	
	for(i = 0; i < eaSize(&obj->eaLODs); i++) {
		
		DynClothLOD *lod = obj->eaLODs[i];
		res = dynClothLODCreateMeshes(lod, model, i);
	
		if (res < 0)
			return res;
	}

	return res;
}

//////////////////////////////////////////////////////////////////////////////
// Set values for all LOD dynCloths

void dynClothObjectSetColRad(
	DynClothObject *obj,
	F32 fParticleCollisionRadius,
	F32 fParticleCollisionRadiusMax,
	F32 fParticleCollisionMaxSpeed) {

	int i;
	for(i = 0; i < eaSize(&obj->eaLODs); i++)
		dynClothSetColRad(
			obj->eaLODs[i]->pCloth,
			fParticleCollisionRadius,
			fParticleCollisionRadiusMax,
			fParticleCollisionMaxSpeed);


}
void dynClothObjectSetGravity(DynClothObject *obj, F32 g) {
	
	int i;
	bool actuallyChanged = false;

	for (i = 0; i < eaSize(&obj->eaLODs); i++) {

		if(g != obj->eaLODs[i]->pCloth->Gravity) {
			actuallyChanged = true;
		}

		dynClothSetGravity(obj->eaLODs[i]->pCloth, g);
	}

	if(actuallyChanged) {
		obj->fSleepTimer = CLOTH_WAKE_TIME;
	}
}

void dynClothObjectSetDrag(DynClothObject *obj, F32 drag) {

	int i;
	bool actuallyChanged = false;

	for(i = 0; i < eaSize(&obj->eaLODs); i++) {

		if(drag != obj->eaLODs[i]->pCloth->Drag) {
			actuallyChanged = true;
		}

		dynClothSetDrag(obj->eaLODs[i]->pCloth, drag);
	}

	if(actuallyChanged) {
		obj->fSleepTimer = CLOTH_WAKE_TIME;
	}
}

void dynClothObjectSetWind(DynClothObject *obj, Vec3 dir, F32 speed, F32 maxspeed) {

	int i;

	speed *= obj->fWindSpeedScale;
	maxspeed *= obj->fWindSpeedScale;

	for(i = 0; i < eaSize(&obj->eaLODs); i++)
		dynClothSetWind(obj->eaLODs[i]->pCloth, dir, speed, maxspeed);

	if(speed)
		obj->fSleepTimer = CLOTH_WAKE_TIME;
}

//////////////////////////////////////////////////////////////////////////////
// Call Update functions on the current LOD DynCloth

void dynClothObjectUpdatePosition(DynClothObject *clothobj, F32 dt, Mat4 newmat, Vec3 *hooks, int freeze)
{
	if(clothobj->iCurrentLOD == clothobj->iLastLOD) {

		dynClothUpdatePosition(
			clothobj->eaLODs[clothobj->iCurrentLOD]->pCloth,
			dt, newmat, hooks, freeze,
			clothobj->fWorldMovementScale);

		subVec3(newmat[3],
				clothobj->eaLODs[clothobj->iCurrentLOD]->pCloth->vLastRootPos,
				clothobj->eaLODs[clothobj->iCurrentLOD]->pCloth->vMovementSinceLastFrame);
	}

	copyVec3(newmat[3], clothobj->eaLODs[clothobj->iCurrentLOD]->pCloth->vLastRootPos);
}

void dynClothObjectUpdatePhysics(DynClothObject *clothobj, F32 dt, F32 clothRadius, bool gotoOrigPos, bool skipCollisions, bool collideSkels, Vec3 vScale)
{
	F32 fMaxConstraintRatio = 0.f;
	F32 fMinConstraintRatio = 0.f;

	int physicsLod = clothobj->iCurrentLOD;

	int i;

	if(!clothobj->pGeo) {
		// FX cloth does not get the extra constraints iterations.
		if(physicsLod < 1) physicsLod = 1;
	}

	for(i = 0; i < clothobj->iNumIterations; i++) {
		float alpha = (float)(i+1) / (float)clothobj->iNumIterations;
		dynClothUpdatePhysics(
			clothobj->eaLODs[clothobj->iCurrentLOD]->pCloth,
			dt,
			clothRadius,
			gotoOrigPos,
			collideSkels,
			skipCollisions,
			physicsLod,
			clothobj->fSleepTimer <= 0 && !clothobj->pGeo,
			alpha,
			1.0 / (float)clothobj->iNumIterations,
			vScale,
			clothobj->eaLODs[clothobj->iCurrentLOD]->pCloth->pHarness->xAvgMat,
			&fMaxConstraintRatio,
			&fMinConstraintRatio);
	}

	// The current LOD has been updated, so it's okay
	// to let it render immediately. (Instead of rendering,
	// then switching to the new LOD.)
	clothobj->iLastLOD = clothobj->iCurrentLOD;

	//remember the constraint ratio
	clothobj->fMaxConstraintRatio = fMaxConstraintRatio;
	clothobj->fMinConstraintRatio = fMinConstraintRatio;
}

// Update the Mesh pointers for all meshes in the active LOD.
void dynClothObjectUpdateDraw(DynClothObject *clothobj)
{
	DynClothLOD *lod = clothobj->eaLODs[clothobj->iCurrentLOD];
	DynCloth *cloth = lod->pCloth;
	DynClothRenderData *renderData = &cloth->renderData;
	renderData->currentMesh = dynClothObjGetLODMesh(clothobj, -1, -1);

	dynClothUpdateDraw(renderData);

	if (renderData->currentMesh) {
		int nump = dynClothNumRenderedParticles(&renderData->commonData);
		dynClothMeshSetPoints(renderData->currentMesh, nump, renderData->RenderPos, renderData->Normals, renderData->TexCoords, renderData->BiNormals, renderData->Tangents);
	}
}

void dynClothObjectUpdate(DynClothObject *clothobj, F32 dt, F32 fForwardSpeed, Vec3 vForwardVec, Vec3 vScale, bool moving, bool mounted, bool gotoOrigPos, bool skipCollisions, bool collideSkels, Vec3 *pvWindOverride)
{
	Mat4 mat;
	DynCloth* pCloth;
	int freeze = (dt < 0.00001f);
	bool sleepThisFrame = clothobj->fSleepTimer <= 0 && !clothobj->pGeo;

	if(dynDebugState.cloth.bDisableCloth) {
		return;
	}

	// Run a quick check to make sure we're about to simulate a valid LOD.
	if(clothobj->iCurrentLOD == -1 || !eaSize(&clothobj->eaLODs)) {

		// Something horrible has happened and I have no idea what the state of the cloth is in, so extract the model
		// name in a safe way, spit out an error, and bail out.

		const char *modelName = "(unknown)";
		if(clothobj->pModel) {
			modelName = clothobj->pModel->name;
		}

		Errorf("Tried to simulate cloth with no LODs: %s", modelName);
		return;
	}

	pCloth = clothobj->eaLODs[clothobj->iCurrentLOD]->pCloth;

	if (!freeze)
	{
		F32 avgdt;
		//printf("dtfact: %d ", nodedata->avgdtfactor);
		if (clothobj->fAvgdtFactor < 30) {
			clothobj->fAvgdtFactor++;
			avgdt = clothobj->fAvgdt * ((clothobj->fAvgdtFactor-1)/(float)clothobj->fAvgdtFactor) + dt * (1.f/clothobj->fAvgdtFactor);
		} else {
			avgdt = clothobj->fAvgdt * (29.f/30.f) + dt * (1.f/30.f);
		}
		clothobj->fAvgdt = avgdt;
		dt = avgdt;
	}

	PERFINFO_AUTO_START("Harness update", 1);
	if(dynClothAttachmentHarnessUpdate(clothobj->eaLODs[clothobj->iCurrentLOD]->pCloth, vScale) > 0.01) {
		// Cloth was moved significantly by whatever it's attached to.
		clothobj->fSleepTimer = CLOTH_WAKE_TIME;
	}
	PERFINFO_AUTO_STOP();

	if(clothobj->pCollisionSet) {
		dynClothCollisionSetUpdate(clothobj->pCollisionSet, clothobj, moving, fForwardSpeed < 0.0f, mounted);
	}

	if (clothobj->eaLODs[clothobj->iCurrentLOD]->pCloth->pHarness->pAttachmentNode)
		dynNodeGetWorldSpaceMat(clothobj->eaLODs[clothobj->iCurrentLOD]->pCloth->pHarness->pAttachmentNode, mat, false);
	else
		copyMat4(unitmat, mat);
	dynClothObjectUpdatePosition(clothobj, dt, mat, clothobj->eaLODs[clothobj->iCurrentLOD]->pCloth->pHarness->vHookPositions, freeze);

	{
		Vec3 vWindDir, vTotal;
		F32 fMag;

		if(pvWindOverride) {
			copyVec3(*pvWindOverride, vTotal);
		} else {
			fMag = dynWindGetAtPositionPastEdge(mat[3], vWindDir, true);
			scaleVec3(vWindDir, fMag, vTotal);
		}

		fMag = normalVec3(vTotal);
		dynClothObjectSetWind(clothobj, vTotal, fMag, dynWindGetMaxWind());
	}

	freeze = dynClothCheckMaxMotion(pCloth, dt, mat, freeze);

	// Update at 30 or 60 fps, depending on the quality settings
	{
		F32 fFps = 60.0f;
		U32 uiIterationCount = freeze?1:MAX(round(fFps * dt), 1);
		U32 uiIter;

		F32 fFrac = 1.0f / (F32)uiIterationCount;

		F32 fPartialDT = dt * fFrac;

		if(uiIterationCount > 4) {
			uiIterationCount = 4;
		}

		for (uiIter=0; uiIter<uiIterationCount; ++uiIter)
		{
			F32 fAttachFrac = 1.0f / (F32)(uiIterationCount - uiIter);
			float maxScale = clothobj->pModel->radius;

			// Get the real radius scale from the biggest dimension.
			if(vScale[0] > vScale[1] || vScale[2] > vScale[1]) {
				if(vScale[0] > vScale[2]) {
					maxScale *= vScale[0];
				} else {
					maxScale *= vScale[2];
				}
			} else {
				maxScale *= vScale[1];
			}

			PERFINFO_AUTO_START("Attachment update", 1);
			dynClothUpdateAttachments(pCloth, clothobj->eaLODs[clothobj->iCurrentLOD]->pCloth->pHarness->vHookPositions, fAttachFrac, freeze);
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_START("Physics update", 1);
			dynClothObjectUpdatePhysics(clothobj, fPartialDT, maxScale, gotoOrigPos, skipCollisions, collideSkels, vScale);
			PERFINFO_AUTO_STOP();
		}

		{
			float moveTotal = dynClothGetAverageMotion(clothobj->eaLODs[clothobj->iCurrentLOD]->pCloth);
			if(moveTotal > 0.0001) {
				clothobj->fSleepTimer = CLOTH_WAKE_TIME;
			} else {
				clothobj->fSleepTimer -= dt;
			}
		}
	}

	if(!sleepThisFrame) {
		PERFINFO_AUTO_START("Render update", 1);
		dynClothCopyToRenderData(clothobj->eaLODs[clothobj->iCurrentLOD]->pCloth, clothobj->eaLODs[clothobj->iCurrentLOD]->pCloth->pHarness->vHookNormals, CCTR_COPY_ALL);
		dynClothObjectUpdateDraw(clothobj);
		PERFINFO_AUTO_STOP();
	}
}

//////////////////////////////////////////////////////////////////////////////

void dynClothObjectResetAll(void)
{
	dynClothLockThreadData(); {

		FOR_EACH_IN_EARRAY(eaDynClothObjects, DynClothObject, pClothObject) {

			if(pClothObject->pGeo) {
				// Cloth on a DynDrawModel.
				pClothObject->pGeo->pCloth = NULL;
			}

			if(pClothObject->pParticle) {
				// Cloth on an FX particle.
				pClothObject->pParticle->pCloth = NULL;
			}

			dynClothObjectDelete(pClothObject);

		} FOR_EACH_END;

		dynClothClearCache();

	} dynClothUnlockThreadData();
}

//////////////////////////////////////////////////////////////////////////////
// Cloth state load/save so we can rebuild the skeleton without cloth
// appearing to reset.

DynClothObjectSavedState *dynClothObjectSaveState(DynClothObject *pClothObject) {

	DynClothObjectSavedState *pState = CLOTH_MALLOC(DynClothObjectSavedState, 1);
	int i;

	if(pClothObject->pModel) {
		pState->pcClothGeoName = pClothObject->pModel->name;
	}

	pState->iNumLODs = eaSize(&pClothObject->eaLODs);
	pState->pLODs = CLOTH_MALLOC(DynClothObjectSavedStateLOD*, pState->iNumLODs);

	for(i = 0; i < pState->iNumLODs; i++) {

		int j;
		DynClothObjectSavedStateLOD *pLOD = CLOTH_MALLOC(DynClothObjectSavedStateLOD, 1);

		pState->pLODs[i] = pLOD;

		pLOD->iNumPoints = pClothObject->eaLODs[i]->pMesh->NumPoints;
		pLOD->pvPoints = CLOTH_MALLOC(Vec3, pLOD->iNumPoints);
		pLOD->pvPointsOld = CLOTH_MALLOC(Vec3, pLOD->iNumPoints);

		for(j = 0; j < pLOD->iNumPoints; j++) {
			copyVec3(pClothObject->eaLODs[i]->pCloth->CurPos[j], pLOD->pvPoints[j]);
			copyVec3(pClothObject->eaLODs[i]->pCloth->OldPos[j], pLOD->pvPointsOld[j]);
		}
	}

	return pState;

}

void dynClothObjectRestoreState(DynClothObject *pClothObject, DynClothObjectSavedState *pState) {

	int i;

	if( pClothObject->pModel->name != pState->pcClothGeoName) {
		return;
	}

	for(i = 0; i < pState->iNumLODs && i < eaSize(&pClothObject->eaLODs); i++) {

		int j;
		DynClothObjectSavedStateLOD *pLOD = pState->pLODs[i];

		for(j = 0; j < pLOD->iNumPoints && j < pClothObject->eaLODs[i]->pMesh->NumPoints; j++) {
			copyVec3(pLOD->pvPoints[j], pClothObject->eaLODs[i]->pCloth->CurPos[j]);
			copyVec3(pLOD->pvPointsOld[j], pClothObject->eaLODs[i]->pCloth->OldPos[j]);
		}
	}
}

void dynClothObjectDestroySavedState(DynClothObjectSavedState *pState) {

	int i;

	for(i = 0; i < pState->iNumLODs; i++) {
		CLOTH_FREE(pState->pLODs[i]->pvPoints);
		CLOTH_FREE(pState->pLODs[i]->pvPointsOld);
		CLOTH_FREE(pState->pLODs[i]);
	}

	CLOTH_FREE(pState->pLODs);
	CLOTH_FREE(pState);
}

void dynClothObjectSaveAllStates(DynDrawSkeleton *pSkel, DynClothObjectSavedState ***peaStates) {

	int i;

	for(i = 0; i < eaSize(&pSkel->eaClothModels); i++) {
		if(pSkel->eaClothModels[i]->pCloth) {

			DynClothObjectSavedState *pState = dynClothObjectSaveState(pSkel->eaClothModels[i]->pCloth);
			eaPush(peaStates, pState);

		} else if(pSkel->eaClothModels[i]->pClothSavedState) {

			// Cloth state that never got applied because the cloth never made it to an
			// update.
			eaPush(peaStates, pSkel->eaClothModels[i]->pClothSavedState);

			// Take ownership of it.
			pSkel->eaClothModels[i]->pClothSavedState = NULL;
		}
	}

}

void dynClothObjectApplyAllStates(DynDrawSkeleton *pSkel, DynClothObjectSavedState ***peaStates) {

	int i;

	for(i = 0; i < eaSize(&pSkel->eaClothModels); i++) {

		int j;
		for(j = 0; j < eaSize(peaStates); j++) {
			if((*peaStates)[j]) {
				if((*peaStates)[j]->pcClothGeoName == pSkel->eaClothModels[i]->pModel->name) {

					// The DynDrawModel takes ownership of the saved state.
					pSkel->eaClothModels[i]->pClothSavedState = (*peaStates)[j];
					(*peaStates)[j] = NULL;
				}
			}
		}
	}

	// Destroy any remaining states.
	for(i = 0; i < eaSize(peaStates); i++) {
		if((*peaStates)[i]) {
			dynClothObjectDestroySavedState((*peaStates)[i]);
			(*peaStates)[i] = NULL;
		}
	}

	eaDestroy(peaStates);

}

DynClothCounters g_dynClothCounters;

void dynClothResetCounters(void) {

	dynClothLockDebugThreadData(); {
		memset(&g_dynClothCounters, 0, sizeof(g_dynClothCounters));
	} dynClothUnlockDebugThreadData();
}










