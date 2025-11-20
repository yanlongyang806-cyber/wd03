#include "mathutil.h"
#include "dynFxPhysics.h"
#if !SPU
#include "auto_float.h"

#include "StringCache.h"
#include "MemoryPool.h"
#include "partition_enums.h"

#include "dynFx.h"
#include "dynFxInfo.h"
#include "dynNodeInline.h"
#include "dynFxParticle.h"
#include "dynWind.h"

#include "wlState.h"
#include "wlModelLoad.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "PhysicsSDK.h"
#include "WLCostume.h"
#include "wlVolumes.h"

#include "dynRagdollData.h"

#include "WorldCollPrivate.h"

#include "PhysicsSDK.h"

#include "WorldLib_autogen_QueuedFuncs.h"
#include "dynFxPhysics_h_ast.h"
#endif // !SPU

#if 0
// ST: Added these macros to track down something going to non-finite
#define FX_CHECK_FINITEVEC3(vec) assert(FINITEVEC3(vec))
#define FX_CHECK_FINITEQUAT(vec) assert(FINITEVEC4(vec))
#else
#define FX_CHECK_FINITEVEC3(vec)
#define FX_CHECK_FINITEQUAT(vec)
#endif

#if !SPU
MP_DEFINE(DynContactUpdater);

static MemoryPool memPoolDynContactPair[2];

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

static void dynFxPhysicsContact(DynPhysicsObject* pDPO, DynContactPair* pPair);

static struct {
	WorldCollIntegration*			wci;
	const WorldCollIntegrationMsg*	wciMsg;

	WorldCollScene*					wcScene;
	Vec3							center;

	DynPhysicsObject**				dpos;
	DynPhysicsObject**				dposNew;
	DynPhysicsObject**				dposQueuedLoad;
	DynPhysicsObject**				dposRagdollBody;
	DynPhysicsObject**				dposPostProcessRagdollBody;
	
	U32								fgSlot : 1;
	U32								bgSlot : 1;
} dynPhysics;

CommandQueue* dpoCommandQueue;

void dynPhysicsSetCenter(const Vec3 center){
	copyVec3(center, dynPhysics.center);
}

AUTO_COMMAND_QUEUED(dpoCommandQueue);
void dpoDestroy(ACMD_POINTER DynPhysicsObject* dpo){
	if(!dpo){
		return;
	}
	
	wcActorDestroy(dynPhysics.wciMsg, &dpo->wcActor);
	
	if (dpo->bActive && eaFindAndRemove(&dynPhysics.dpos, dpo) < 0)
	{
		assert(0);
	}
	else if (!dpo->bActive && eaFindAndRemove(&dynPhysics.dposQueuedLoad, dpo) < 0)
	{
		assert(0);
	}

	if (dpo->dpoType == DPO_RAGDOLLBODY)
	{
		dpo->body.pBody->pcBone			= NULL;
		dpo->body.pBody->pcParentBone	= NULL;
		dpo->body.pBody->pTuning		= NULL;
		SAFE_FREE(dpo->body.pBody);

		if (eaSize(&dynPhysics.dposPostProcessRagdollBody)) {
			eaFindAndRemove(&dynPhysics.dposPostProcessRagdollBody, dpo);
		}

		if (eaSize(&dynPhysics.dposRagdollBody)) {
			eaFindAndRemove(&dynPhysics.dposRagdollBody, dpo);
		}
	}

	FOR_EACH_IN_EARRAY(dpo->eaContactUpdaters, DynContactUpdater, pUpdater)
	{
		dynNodeClear(&pUpdater->contactNode);

		RefSystem_RemoveReferent(&pUpdater->contactNode, true);
		MP_FREE(DynContactUpdater, pUpdater);
	}
	FOR_EACH_END;
	eaDestroy(&dpo->eaContactUpdaters);

	{
		int i;
		for (i=0; i<2; ++i)
		{
			DynFxPhysicsUpdateInfo* pInfo = &dpo->updateInfo[i];
			FOR_EACH_IN_EARRAY(pInfo->eaContactPairs, DynContactPair, pPair)
			{
				mpFree(memPoolDynContactPair[i], pPair);
			}
			FOR_EACH_END;
			eaDestroy(&pInfo->eaContactPairs);
		}
	}

	SAFE_FREE(dpo);
}

AUTO_COMMAND_QUEUED(dpoCommandQueue);
void dpoSetVelocity(ACMD_POINTER DynPhysicsObject* dpo,
					Vec3 vel)
{
	if(!dpo){
		return;
	}

	FX_CHECK_FINITEVEC3(vel);
	if (dpo->bActive)
		wcActorSetVels(dynPhysics.wciMsg, dpo->wcActor, vel, NULL);
	else
	{
		dpo->bInitialVelocitySet = 1;
		copyVec3(vel, dpo->vInitialVelocity);
	}
}

AUTO_COMMAND_QUEUED(dpoCommandQueue);
void dpoSetAngVelocity(	ACMD_POINTER DynPhysicsObject* dpo,
						Vec3 vel)
{
	if(!dpo){
		return;
	}

	FX_CHECK_FINITEVEC3(vel);
	if (dpo->bActive)
		wcActorSetVels(dynPhysics.wciMsg, dpo->wcActor, NULL, vel);
	else
	{
		dpo->bInitialVelocitySet = 1;
		copyVec3(vel, dpo->vInitialAngularVelocity);
	}
}

AUTO_COMMAND_QUEUED(dpoCommandQueue);
void dpoAddVelocity(ACMD_POINTER DynPhysicsObject* dpo,
					Vec3 vel)
{
	if(!dpo){
		return;
	}

	FX_CHECK_FINITEVEC3(vel);
	if (dpo->bActive)
		wcActorAddVel(dynPhysics.wciMsg, dpo->wcActor, vel);
	else
	{
		dpo->bInitialVelocitySet = 1;
		addToVec3(vel, dpo->vInitialVelocity);
	}
}

AUTO_COMMAND_QUEUED(dpoCommandQueue);
void dpoAddForce(ACMD_POINTER DynPhysicsObject* dpo,
				 Vec3 force,
				 U32 isAcceleration,
				 U32 shouldWakeup)
{
	if(!dpo){
		return;
	}

	FX_CHECK_FINITEVEC3(force);
	if (dpo->bActive)
		wcActorAddForce(dynPhysics.wciMsg, dpo->wcActor, force, isAcceleration, shouldWakeup);
	// TODO should there be an else?
}

AUTO_COMMAND_QUEUED(dpoCommandQueue);
void dpoAddAngVelocity(	ACMD_POINTER DynPhysicsObject* dpo,
						Vec3 vel)
{
	if(!dpo){
		return;
	}

	FX_CHECK_FINITEVEC3(vel);
	if (dpo->bActive)
		wcActorAddAngVel(dynPhysics.wciMsg, dpo->wcActor, vel);
	else
	{
		dpo->bInitialVelocitySet = 1;
		addToVec3(vel, dpo->vInitialAngularVelocity);
	}
}

AUTO_COMMAND_QUEUED(dpoCommandQueue);
void dpoMovePosition(	ACMD_POINTER DynPhysicsObject* dpo,
						Vec3 pos)
{
	if(!dpo){
		return;
	}

	CHECK_FINITEVEC3(pos);
	if (dpo->bActive)
		wcActorMove(dynPhysics.wciMsg, dpo->wcActor, pos);
	copyVec3(pos, dpo->mat[3]);
}

AUTO_COMMAND_QUEUED(dpoCommandQueue);
void dpoSetMat(	ACMD_POINTER DynPhysicsObject* dpo,
				Mat4 mat)
{
	if(!dpo){
		return;
	}

	CHECK_FINITEVEC3(mat[0]);
	CHECK_FINITEVEC3(mat[1]);
	CHECK_FINITEVEC3(mat[2]);
	CHECK_FINITEVEC3(mat[3]);
	if (dpo->bActive)
		wcActorSetMat(dynPhysics.wciMsg, dpo->wcActor, mat);
}

AUTO_COMMAND_QUEUED(dpoCommandQueue);
void dpoWakeUp(	ACMD_POINTER DynPhysicsObject* dpo)
{
	if(!dpo){
		return;
	}

	if (dpo->bActive)
		wcActorWakeUp(dynPhysics.wciMsg, dpo->wcActor);
}

AUTO_COMMAND_QUEUED(dpoCommandQueue);
void dpoMovePositionRotation(	ACMD_POINTER DynPhysicsObject* dpo,
						Vec3 pos, Quat rot)
{
	if(!dpo){
		return;
	}

	CHECK_FINITEVEC3(pos);
	CHECK_FINITEQUAT(rot);
	if (dpo->bActive)
	{
		wcActorMove(dynPhysics.wciMsg, dpo->wcActor, pos);
		wcActorRotate(dynPhysics.wciMsg, dpo->wcActor, rot);
	}
	copyVec3(pos, dpo->mat[3]);
}

AUTO_COMMAND_QUEUED(dpoCommandQueue);
void dpoSetCollidable(
	ACMD_POINTER DynPhysicsObject *dpo,
	bool collidable) {

	if(!dpo) return;

	wcActorSetCollidable(dynPhysics.wciMsg, dpo->wcActor, collidable);
}


StaticDefineInt ParseDynPhysType[] =
{
	DEFINE_INT
	{ "Normal",				edpt_Normal },
	{ "Large",				edpt_Large },
	{ "VeryLarge",			edpt_VeryLarge },
	{ "Off",				edpt_Off },
	DEFINE_END
};

void dynPhysicsObjectCreate(DynPhysicsObject** dpoOut)
{
	*dpoOut = callocStruct(DynPhysicsObject);
	eaPush(&dynPhysics.dposNew, *dpoOut);
}	

void dynFxCreateDPO(DynFx* pFx)
{
	DynParticle* pParticle = pFx->pParticle;
	DynPhysicsObject* dpo;
	
	if (!pParticle)
		return;
	
	dynPhysicsObjectCreate(&dpo);
	pParticle->pDraw->pDPO = dpo;
	
	dpo->dpoType = DPO_FX;
	dpo->fx.pFx = pFx;
	dynNodeGetWorldSpaceMat(&pParticle->pDraw->node, dpo->mat, false);

	FX_CHECK_FINITEVEC3(dpo->mat[3]);
	
	//wcoCreate(	&pParticle->draw.pDPO,
	//			worldGetActiveCollRoamingCell(),
	//			NULL,
	//			pFx,
	//			WG_WCO_DYNFX_OBJECT,
	//			vPos,
	//			vPos,
	//			0,
	//			1);

	pParticle->fDensity = (pFx->pPhysicsInfo && pFx->pPhysicsInfo->fDensity > 0.0f)?pFx->pPhysicsInfo->fDensity:1.0f;

	// Apply initial velocity, spin
	{

		if (pParticle->pDraw->bLocalOrientation && !(pParticle->pDraw->node.uiTransformFlags & ednLocalRot))
		{
			Vec3 vVel, vSpin;
			Quat qPartRot;
			dynNodeGetWorldSpaceRot(&pParticle->pDraw->node, qPartRot);
			quatRotateVec3(qPartRot, pParticle->pDraw->vVelocity, vVel);
			quatRotateVec3(qPartRot, pParticle->pDraw->vSpin, vSpin);
			FX_CHECK_FINITEVEC3(vVel);
			FX_CHECK_FINITEVEC3(vSpin);

			if(!(pFx->pPhysicsInfo && pFx->pPhysicsInfo->bKinematicActor)) {
				QueuedCommand_dpoAddVelocity(pParticle->pDraw->pDPO, vVel);
				QueuedCommand_dpoAddAngVelocity(pParticle->pDraw->pDPO, vSpin);
			}
		}
		else
		{
			FX_CHECK_FINITEVEC3(pParticle->pDraw->vVelocity);
			FX_CHECK_FINITEVEC3(pParticle->pDraw->vSpin);

			if(!(pFx->pPhysicsInfo && pFx->pPhysicsInfo->bKinematicActor)) {
				QueuedCommand_dpoAddVelocity(pParticle->pDraw->pDPO, pParticle->pDraw->vVelocity);
				QueuedCommand_dpoAddAngVelocity(pParticle->pDraw->pDPO, pParticle->pDraw->vSpin);
			}
		}
	}
}

void dynFxDestroyPhysics(DynParticle* pParticle)
{
	assert(pParticle->pDraw->pDPO->dpoType == DPO_FX);
	
	pParticle->pDraw->pDPO->fx.pFx = NULL;
	
	QueuedCommand_dpoDestroy(pParticle->pDraw->pDPO);
	pParticle->pDraw->pDPO = NULL;
}

static void dynPhysicsObjectCreateFx(	const WorldCollIntegrationMsg* msg,
										DynPhysicsObject* dpo,
										int iQueueIndex)
{
	DynFx*			pFx = NULL;

#if !PSDK_DISABLED
	DynParticle*	pParticle;

	pFx = dpo->fx.pFx;
	pParticle = SAFE_MEMBER(pFx, pParticle);
	
	if (pFx && pParticle)
	{
		PSDKActorDesc*		actorDesc;
		PSDKBodyDesc		bodyDesc = {0};
		Mat4				mat;
		Quat				qRot;
		F32					density;
		U32					shapeGroup;
		U32					filterBits = WC_FILTER_BIT_DEBRIS;
		U32					wcMaterialIndex = 0;
		Vec3				vCenter;
		DynFxInfo*			pInfo = GET_REF(pFx->hInfo);
		DynFx*				parentFX = NULL;
		DynPhysicsObject*	parentDPO = NULL;

		// Get the parent DynPhysicsObject, if one exists.

		parentFX = GET_REF(pFx->hParentFx);

		if(parentFX && parentFX->pParticle && parentFX->pParticle->pDraw->pDPO) {
			
			parentDPO = parentFX->pParticle->pDraw->pDPO;
			
			if(!(parentDPO && parentDPO->wcActor && parentDPO->wcActor->psdkActor)) {
			
				// We need to wait for the parent to be set up!
				if (iQueueIndex < 0) {
					eaPush(&dynPhysics.dposQueuedLoad, dpo);
				}

				return;
			}
		}

		if (!pParticle->pDraw->pModel && pParticle->pDraw->pcModelName)
			pParticle->pDraw->pModel = modelFind(pParticle->pDraw->pcModelName, true, WL_FOR_FX);

		if (pParticle->pDraw->pModel)
		{
			if (!modelLODLoadAndMaybeWait(pParticle->pDraw->pModel, 0, false))
			{
				// model is not loaded, wait to create the actor
				if (iQueueIndex < 0)
					eaPush(&dynPhysics.dposQueuedLoad, dpo);
				return;
			}
		}

		psdkActorDescCreate(&actorDesc);
		dynNodeGetWorldSpacePos(&pParticle->pDraw->node,  mat[3]);
		dynNodeGetWorldSpaceRot(&pParticle->pDraw->node, qRot);
		quatToMat(qRot, mat);
		
		copyMat4(mat, dpo->mat);
		
		psdkActorDescSetMat4(actorDesc, mat);
		psdkActorDescSetStartAsleep(actorDesc, pFx->pPhysicsInfo->bStartAsleep);
		bodyDesc.scale = 1.0f;
		density = (pFx->pPhysicsInfo && pFx->pPhysicsInfo->fDensity > 0.0f)?pFx->pPhysicsInfo->fDensity:1.0f;
		shapeGroup = WC_SHAPEGROUP_DEBRIS;
		if (pFx->pPhysicsInfo)
		{
			switch (pFx->pPhysicsInfo->eType)
			{
				xcase edpt_Large:
					shapeGroup = WC_SHAPEGROUP_DEBRIS_LARGE;
				xcase edpt_VeryLarge:
					shapeGroup = WC_SHAPEGROUP_DEBRIS_VERY_LARGE;
			}
		}
		if (pFx->pPhysicsInfo && pFx->pPhysicsInfo->pcPhysicalProperty)
		{
			WorldCollMaterial*	wcMaterial;
			wcMaterialFromPhysicalPropertyName(&wcMaterial, pFx->pPhysicsInfo->pcPhysicalProperty);
			wcMaterialGetIndex(wcMaterial, &wcMaterialIndex);
		}


		if (pParticle->pDraw->pModel)
		{
			PSDKCookedMesh* mesh;
			Vec3 vScale;
			dynNodeGetWorldSpaceScale(&pParticle->pDraw->node, vScale);

			mesh = geoCookConvexMesh(pParticle->pDraw->pModel, vScale, 0, true); // should not stall thanks to the loading above
			
			copyVec3(pParticle->pDraw->pModel->mid, vCenter);

			psdkActorDescAddMesh(	actorDesc,
									mesh,
									NULL,
									density,
									wcMaterialIndex,
									filterBits,
									shapeGroup,
									0);
		}
		else
		{
			F32 radius = (pFx->pPhysicsInfo && pFx->pPhysicsInfo->fRadius > 0.0f)?pFx->pPhysicsInfo->fRadius:1.0f;
			zeroVec3(vCenter);

			psdkActorDescAddSphere(	actorDesc,
									radius,
									NULL,
									density,
									wcMaterialIndex,
									filterBits,
									shapeGroup);
		}

		wcActorCreate(	msg,
						dynPhysics.wcScene,
						&dpo->wcActor,
						dpo,
						actorDesc,
						&bodyDesc,
						pFx->pPhysicsInfo ? pFx->pPhysicsInfo->bKinematicActor : 0,
						pInfo?eaSize(&pInfo->eaContactEvents)>0:0,
						1);

		wcActorCreateAndAddCCDSkeleton(msg, dpo->wcActor, vCenter);

		if(pFx->pPhysicsInfo && pFx->pPhysicsInfo->bPhysicsAttached) {

			// This object is attached to the parent FX (or world, if there is
			// none) with a joint.

			PSDKJointDesc jointDesc;
			DynJointTuning tuning;

			assert((pFx->pPhysicsInfo && pFx->pPhysicsInfo->bPhysicsAttached));

			if(parentDPO && parentDPO->wcActor && parentDPO->wcActor->psdkActor) {
				jointDesc.actor1 = dpo->wcActor->psdkActor;
				jointDesc.actor0 = parentDPO->wcActor->psdkActor;
			} else {
				jointDesc.actor0 = dpo->wcActor->psdkActor;
				jointDesc.actor1 = NULL;
			}

			memset(&tuning, 0, sizeof(tuning));

			jointDesc.tuning = &tuning;

			if(pFx->pPhysicsInfo->bRevoluteJoint) {
				tuning.jointType = eJointType_Revolute;
			} else {
				tuning.jointType = eJointType_Spherical;
			}

			tuning.axis = pFx->pPhysicsInfo->jointAxis;

			if(pFx->pPhysicsInfo->fJointSpringiness) {

				tuning.spring.spring = pFx->pPhysicsInfo->fJointSpringiness;
				tuning.springEnabled = true;

				// TODO: Expose this to FX info.
				tuning.spring.damper = 10;
			}

			if(pFx->pPhysicsInfo->bLimitJoint) {

				tuning.limitEnabled = true;

				tuning.limitHigh.value = pFx->pPhysicsInfo->fJointMax;
				tuning.limitHigh.restitution = 0;
				tuning.limitHigh.hardness = 1;

				tuning.limitLow.value = pFx->pPhysicsInfo->fJointMin;
				tuning.limitLow.restitution = 0;
				tuning.limitLow.hardness = 1;
			}

			if(parentDPO) {
				copyVec3(pFx->pPhysicsInfo->vChildAttachPoint, jointDesc.anchor1);
				copyVec3(pFx->pPhysicsInfo->vParentAttachPoint, jointDesc.anchor0);
			} else {
				copyVec3(pFx->pPhysicsInfo->vChildAttachPoint, jointDesc.anchor0);
				addVec3(pParticle->vOldPos, pFx->pPhysicsInfo->vParentAttachPoint, jointDesc.anchor1);
			}

			psdkJointCreate(&jointDesc, 0, 0);

			// TODO: Expose this as an option.
			psdkActorSetLinearDamping(dpo->wcActor->psdkActor, 1);

		}
	
		if(pFx->pPhysicsInfo && pFx->pPhysicsInfo->bNoCollide) {
			psdkActorSetCollidable(dpo->wcActor->psdkActor, false);
		}

		psdkActorSetSleepVelocities(dpo->wcActor->psdkActor, 0.01f, 0.01f);

		psdkActorDescDestroy(&actorDesc);
	
	}
#endif

	if (iQueueIndex >= 0)
		eaRemoveFast(&dynPhysics.dposQueuedLoad, iQueueIndex);

	dpo->bActive = 1;
	eaPush(&dynPhysics.dpos, dpo);

	if (dpo->bInitialVelocitySet && !(pFx && pFx->pPhysicsInfo && pFx->pPhysicsInfo->bKinematicActor))
	{
		dpoSetVelocity(dpo, dpo->vInitialVelocity);
		dpoSetAngVelocity(dpo, dpo->vInitialAngularVelocity);
	}
}

static StashTable stBadConvex = NULL;
static void dynPhysicsObjectCreateSkeleton(	const WorldCollIntegrationMsg* msg,
											DynPhysicsObject* dpo,
											int iQueueIndex)
{
#if !PSDK_DISABLED
	DynSkeleton* pSkeleton = dpo->skel.pSkeleton;
	
	if (pSkeleton && pSkeleton->pRoot && GET_REF(pSkeleton->hCostume))
	{
		WLCostume*		costume = GET_REF(pSkeleton->hCostume);
		PSDKCookedMesh*	mesh = NULL;
		PSDKActorDesc*	actorDesc;
		PSDKBodyDesc	bodyDesc = {0};
		U32				uiMatIndex = 0;
		bool			bLoadFailed = false;
		Model*			pShieldModel = NULL;

		// queue all models for loading
		/*
		for (i = 0; i < (costume->pCollGeo?1:eaSize(&costume->eaCostumeParts)); ++i)
		{
			Model*	model;

			if (costume->pCollGeo)
				model = costume->pCollGeo;
			else if (costume->eaCostumeParts[i]->bCollisionOnly && costume->eaCostumeParts[i]->pCachedModel)
				model = costume->eaCostumeParts[i]->pCachedModel;
			else if (costume->bAutoGlueUp && costume->eaCostumeParts[i]->pCachedModel)
				model = costume->eaCostumeParts[i]->pCachedModel;
			else
				continue;

			if (!modelLODLoadAndMaybeWait(model, 0, false))
				bLoadFailed = true;
		}
		*/

		if (costume->pcShieldGeometry)
		{
			if (!pSkeleton->bEverUpdated)
				bLoadFailed = true;
			if (pSkeleton->pDrawSkel)
			{
				FOR_EACH_IN_EARRAY(pSkeleton->pDrawSkel->eaDynGeos, DynDrawModel, pGeo)
				{
					if (pGeo->bRaycastable && !modelLODLoadAndMaybeWait(pGeo->pModel, eaSize(&pGeo->pModel->model_lods)-1, false))
						bLoadFailed = true;
				}
				FOR_EACH_END;
			}
			pShieldModel = modelFind(costume->pcShieldGeometry, false, WL_FOR_FX);
			if (!modelLODLoadAndMaybeWait(pShieldModel, 0, false))
				bLoadFailed = true;
		}

		if (bLoadFailed)
		{
			// model is not loaded, wait to create the actor
			if (iQueueIndex < 0)
				eaPush(&dynPhysics.dposQueuedLoad, dpo);
			return;
		}

		{
			Vec3			pos;
			Quat			rot;
			Mat4			mat;
			dynNodeGetWorldSpacePos(pSkeleton->pRoot, pos);
			dynNodeGetWorldSpaceRot(pSkeleton->pRoot, rot);

			quatToMat(rot, mat);
			copyVec3(pos, mat[3]);

			psdkActorDescCreate(&actorDesc);

			psdkActorDescSetMat4(actorDesc, mat);
		}


		bodyDesc.scale = 1.0f;
		{
			WorldCollMaterial* wcm;
			if (wcMaterialFromPhysicalPropertyName(&wcm, "Ragdoll"))
			{
				wcMaterialGetIndex(wcm, &uiMatIndex);
			}
		}

		if (pShieldModel && pSkeleton->pDrawSkel)
		{
			FOR_EACH_IN_EARRAY(pSkeleton->pDrawSkel->eaDynGeos, DynDrawModel, pGeo)
			{
				Vec3	vScale;
				Mat4	mTransform, mBone;
				DynTransform xBone;

				if (!pGeo->bRaycastable)
					continue;

				{
					const DynNode** eaNodesToMultiply = NULL;
					const DynNode* pCurNode = pGeo->pAttachmentNode;
					while (pCurNode && pCurNode != pSkeleton->pRoot)
					{
						eaPush(&eaNodesToMultiply, pCurNode);
						pCurNode = pCurNode->pParent;
					}

					dynTransformClearInline(&xBone);

					FOR_EACH_IN_EARRAY_FORWARDS(eaNodesToMultiply, const DynNode, pNode)
					{
						DynTransform xLocal, xTemp;
						dynNodeGetLocalTransformInline(pNode, &xLocal);
						dynTransformMultiply(&xBone, &xLocal, &xTemp);
						dynTransformCopy(&xTemp, &xBone);
					}
					FOR_EACH_END;
					eaDestroy(&eaNodesToMultiply);
				}

				dynTransformToMat4Unscaled(&xBone, mBone);

				mulMat4(mBone, pGeo->mTransform, mTransform);

				extractScale(mTransform, vScale);
				mulVecVec3(vScale, xBone.vScale, vScale);


				// Has to be convex to be dynamic
				mesh = geoCookConvexMesh(pGeo->pModel, vScale, eaSize(&pGeo->pModel->model_lods)-1, true);
				if (mesh)
				{
					psdkActorDescAddMesh(actorDesc, mesh, mTransform, 1, uiMatIndex, WC_FILTER_BIT_HULL, WC_SHAPEGROUP_NONE, (intptr_t)pSkeleton->pRoot->pParent);
				}
				else
				{
					// Use a static stashtable to make sure we only report the error once per session.
					if (!stBadConvex)
						stBadConvex = stashTableCreateWithStringKeys(16, StashDefault);
					if (stashAddInt(stBadConvex, pGeo->pModel->name, 1, false))
					{
						ErrorFilenamef(pGeo->pModel->header->filename, "Model %s lowest LOD has too many triangles. Can't cook convex mesh for raycasting FX.", pGeo->pModel->name);
					}
				}
			}
			FOR_EACH_END;
		}				

		if (pShieldModel)
		{
			DynNode* pAttachNode = dynSkeletonFindNodeNonConst(pSkeleton, costume->pcShieldAttachBone);
			Vec3 vScale;
			Mat4 transform;
			if (pAttachNode)
			{
				DynTransform xAttach, xRoot, xRootInv, xAttachChar;
				dynNodeGetWorldSpaceTransform(pAttachNode, &xAttach);
				copyVec3(xAttach.vScale, vScale);

				dynNodeGetWorldSpaceTransform(pSkeleton->pRoot, &xRoot);
				dynTransformInverse(&xRoot, &xRootInv);

				dynTransformMultiply(&xAttach, &xRootInv, &xAttachChar);
				dynTransformToMat4Unscaled(&xAttachChar, transform);
			}
			else
			{
				unitVec3(vScale);
				copyMat4(unitmat, transform);
			}

			mulVecVec3(vScale, costume->vShieldScale, vScale);
			{
				PSDKCookedMesh*	pShieldMesh = geoCookConvexMesh(pShieldModel, vScale, 0, true);

				if (pShieldMesh)
				{
					psdkActorDescAddMesh( actorDesc, pShieldMesh, transform, 1, 0, WC_FILTER_BIT_SHIELD, WC_SHAPEGROUP_NONE, (intptr_t)pSkeleton->pRoot->pParent );
				}
				else
				{
					// Use a static stashtable to make sure we only report the error once per session.
					if (!stBadConvex)
						stBadConvex = stashTableCreateWithStringKeys(16, StashDefault);
					if (stashAddInt(stBadConvex, pShieldModel->name, 1, false))
					{
						ErrorFilenamef(pShieldModel->header->filename, "ShieldModel %s lowest LOD has too many triangles. Can't cook convex mesh for raycasting FX.", pShieldModel->name);
					}
				}
			}
		}

		if (!mesh)
		{
			F32 fHeight, fRadius;
			WLCostume* pCostume = GET_REF(pSkeleton->hCostume);
			Mat4 transform;
			copyMat4(unitmat, transform);

			if (0 && pCostume)
			{
				fRadius = MAX(pCostume->vExtentsMax[0], pCostume->vExtentsMax[2]);
				fHeight = pCostume->vExtentsMax[1];
			}
			else
			{
				fRadius = 1.5f;
				fHeight = 3.0f;
				transform[3][1] = 3.0f;
			}
			
			if (!pShieldModel)
			{
				psdkActorDescAddCapsule(
					actorDesc,
					fHeight,
					fRadius,
					transform,
					1,
					uiMatIndex,
					WC_FILTER_BITS_ENTITY,
					WC_SHAPEGROUP_ENTITY
					);
			}
		}
		
		wcActorCreate(	msg,
						dynPhysics.wcScene,
						&dpo->wcActor,
						NULL,
						actorDesc,
						&bodyDesc,
						1,
						0,
						0);
		
		psdkActorDescDestroy(&actorDesc);
	}
#endif

	if (iQueueIndex >= 0)
		eaRemoveFast(&dynPhysics.dposQueuedLoad, iQueueIndex);

	dpo->bActive = 1;
	eaPush(&dynPhysics.dpos, dpo);

	if (dpo->bInitialVelocitySet)
	{
		dpoSetVelocity(dpo, dpo->vInitialVelocity);
		dpoSetAngVelocity(dpo, dpo->vInitialAngularVelocity);
	}
}

#define DYNFXPHYSICS_RAGDOLL_MAX_ANGULAR_VELOCITY 18.f
#define DYNFXPHYSICS_RAGDOLL_MAX_LINEAR_VELOCITY 40.f

static void dynPhysicsObjectCreateRagdollBody(	const WorldCollIntegrationMsg* msg,
												DynPhysicsObject* dpo,
												int iQueueIndex)
{
#if !PSDK_DISABLED

	DynClientSideRagdollBody *pBody = dpo->body.pBody;
	WorldCollMaterial*	wcm;
	U32					uiMatIndex = 0;
	PSDKBodyDesc	bodyDesc = {0};
	PSDKActorDesc*	actorDesc;
	Mat4 mBaseBodyLocalSpace;
	Mat4 mBaseBodyWorldSpace;

	if (pBody->pcPhysicalProperties &&
		wcMaterialFromPhysicalPropertyName(&wcm, pBody->pcPhysicalProperties))
	{
		wcMaterialGetIndex(wcm, &uiMatIndex);
	}
	else if (wcMaterialFromPhysicalPropertyName(&wcm, "Ragdoll"))
	{
		wcMaterialGetIndex(wcm, &uiMatIndex);
	}

	quatToMat(pBody->qBaseRot, mBaseBodyLocalSpace);
	copyVec3(pBody->vBasePos, mBaseBodyLocalSpace[3]);
	mulMat4(pBody->mEntityWS, mBaseBodyLocalSpace, mBaseBodyWorldSpace);

	psdkActorDescCreate(&actorDesc);
	psdkActorDescSetMat4(actorDesc, mBaseBodyWorldSpace);

	if (pBody->eShape == eRagdollShape_Box)
	{
		Vec3 vBoxHalfSize;

		scaleVec3(pBody->vBoxDimensions, 0.5f, vBoxHalfSize);

		psdkActorDescAddBox(actorDesc,
							vBoxHalfSize,
							pBody->mBox,
							pBody->fDensity,
							uiMatIndex,
							WC_FILTER_BITS_ENTITY,
							pBody->uiCollisionTester ?
								(pBody->uiTorsoBone ? WC_SHAPEGROUP_TEST_RAGDOLL_BODY : WC_SHAPEGROUP_TEST_RAGDOLL_LIMB) :
								(pBody->uiTorsoBone ? WC_SHAPEGROUP_RAGDOLL_BODY      : WC_SHAPEGROUP_RAGDOLL_LIMB));
	}
	else if (pBody->eShape == eRagdollShape_Capsule)
	{
		Mat4			mBody;
		Vec3			vPYR;

		getVec3YP(pBody->vCapsuleDir, &vPYR[1], &vPYR[0]);
		vPYR[0] = subAngle(RAD(90), vPYR[0]);
		vPYR[1] = addAngle(vPYR[1], PI);
		vPYR[2] = 0;
		createMat3YPR(mBody, vPYR);

		copyVec3(pBody->vCapsuleStart, mBody[3]);
		scaleAddVec3(mBody[1], 0.5f*pBody->fCapsuleLength, mBody[3], mBody[3]);

		psdkActorDescAddCapsule(actorDesc,
								pBody->fCapsuleLength,
								pBody->fCapsuleRadius,
								mBody,
								pBody->fDensity,
								uiMatIndex,
								WC_FILTER_BITS_ENTITY,
								pBody->uiCollisionTester ?
									(pBody->uiTorsoBone ? WC_SHAPEGROUP_TEST_RAGDOLL_BODY : WC_SHAPEGROUP_TEST_RAGDOLL_LIMB) :
									(pBody->uiTorsoBone ? WC_SHAPEGROUP_RAGDOLL_BODY      : WC_SHAPEGROUP_RAGDOLL_LIMB));
	}

	wcActorCreate(	msg,
					dynPhysics.wcScene,
					&dpo->wcActor,
					dpo,
					actorDesc,
					&bodyDesc,
					0,
					pBody->uiCollisionTester,
					pBody->uiCollisionTester);

	psdkActorDescDestroy(&actorDesc);

	dpo->bActive = 1;
	eaPush(&dynPhysics.dpos, dpo);
	eaPush(&dynPhysics.dposPostProcessRagdollBody, dpo);

#endif
}

static void dynPhysicsObjectCreateRagdollJoint(DynPhysicsObject *pDPO)
{
#if !PSDK_DISABLED

	DynClientSideRagdollBody *pBody = pDPO->body.pBody;
	DynPhysicsObject *pDPOParent = pBody->pParentDPO;

	if (pDPOParent) {
		PSDKActor *psdkActor = pDPO->wcActor->psdkActor;
		PSDKActor *psdkActorParent = pDPOParent->wcActor->psdkActor;

		PSDKJointDesc jd = {0};
		jd.actor0 = psdkActor;
		jd.actor1 = psdkActorParent;
		copyVec3(pBody->vSelfAnchor,   jd.anchor0);
		copyVec3(pBody->vParentAnchor, jd.anchor1);
		jd.tuning = pBody->pTuning;
		copyVec3(pBody->vJointAxis, jd.globalAxis);
		jd.volumeScale = pBody->fVolumeMassScale;

		psdkJointCreate(&jd, pBody->uiUseCustomJointAxis, 1);
	}

#endif
}

static void dynPhysicsObjectPostProcessRagdollBody(DynPhysicsObject *pDPO)
{
#if !PSDK_DISABLED

	DynClientSideRagdollBody *pBody = pDPO->body.pBody;
	PSDKActor *psdkActor = pDPO->wcActor->psdkActor;
	F32 fAngVelMagSqr;
	F32 fLinVelMagSqr;
	
	psdkActorCreateAndAddCCDSkeleton(psdkActor, zerovec3);
	psdkActorSetRagdollInitialValues(	psdkActor,
										pBody->vPosePos,
										pBody->qPoseRot,
										pBody->fSkinWidth,
										1.1f, 1.1f,
										DYNFXPHYSICS_RAGDOLL_MAX_ANGULAR_VELOCITY,
										8,
										pBody->uiCollisionTester ?
											(pBody->uiTorsoBone ? WC_SHAPEGROUP_TEST_RAGDOLL_BODY : WC_SHAPEGROUP_TEST_RAGDOLL_LIMB) :
											(pBody->uiTorsoBone ? WC_SHAPEGROUP_RAGDOLL_BODY      : WC_SHAPEGROUP_RAGDOLL_LIMB)
										);

	if (!pBody->pcParentBone)
	{
		Mat4 mActor, mActorInv;
		Mat4 mEntityActorSpace;

		psdkActorGetMat(psdkActor, mActor);
		invertMat4(mActor, mActorInv);
		mulMat4(mActorInv, pBody->mEntityWS, mEntityActorSpace);

		getMat3YPR(mEntityActorSpace, pBody->pyrOffsetToAnimRoot);
		copyVec3(mEntityActorSpace[3], pBody->posOffsetToAnimRoot);
	}

	fAngVelMagSqr = lengthVec3Squared(pBody->vInitAngVel);
	if (fAngVelMagSqr > SQR(DYNFXPHYSICS_RAGDOLL_MAX_ANGULAR_VELOCITY)) {
		scaleVec3(pBody->vInitAngVel, DYNFXPHYSICS_RAGDOLL_MAX_ANGULAR_VELOCITY/sqrtf(fAngVelMagSqr), pBody->vInitAngVel);
	}

	fLinVelMagSqr = lengthVec3Squared(pBody->vInitVel);
	if (fLinVelMagSqr > SQR(DYNFXPHYSICS_RAGDOLL_MAX_LINEAR_VELOCITY)) {
		scaleVec3(pBody->vInitVel, DYNFXPHYSICS_RAGDOLL_MAX_LINEAR_VELOCITY/sqrtf(fLinVelMagSqr), pBody->vInitVel);
	}

	psdkActorSetVels(psdkActor, pBody->vInitVel, pBody->vInitAngVel);
	pBody->fAngDamp = 0.f;

#endif
}

static void dynPhysicsUpdateRagdollBody(DynPhysicsObject *pDPO)
{
#if !PSDK_DISABLED

	if (SAFE_MEMBER(pDPO,body.pBody))
	{
		DynClientSideRagdollBody *pBody = pDPO->body.pBody;
		PSDKActor *psdkActor = pDPO->wcActor->psdkActor;
		Mat4 mBody;
		Quat qTemp;

		psdkActorGetMat(psdkActor, mBody);
		mat3ToQuat(mBody, qTemp);
		quatMultiply(pBody->qBindPose, qTemp, pBody->qWorldSpace);
		copyVec3(mBody[3], pBody->vWorldSpace);

		pBody->uiSleeping = psdkActorIsSleeping(psdkActor);

		if (pBody->uiCollisionTester)
		{
			DynFxPhysicsUpdateInfo *pInfo = &pDPO->updateInfo[dynPhysics.fgSlot];
			FOR_EACH_IN_EARRAY(pInfo->eaContactPairs, DynContactPair, pPair)
			{
				pDPO->body.pBody->uiTesterCollided = 1;
				mpFree(memPoolDynContactPair[dynPhysics.fgSlot], pPair);
			}
			FOR_EACH_END;
			eaClear(&pInfo->eaContactPairs);
		}
		else
		{
			DynPhysicsObject *pParentDPO = pDPO->body.pBody->pParentDPO;
			F32 fLinearVelocity = psdkActorGetLinearVelocity(psdkActor);
			F32 fAngularVelocity = psdkActorGetAngularVelocity(psdkActor);
			F32 fAngDamp;

			fAngDamp =	fLinearVelocity  < 5.f &&
						fAngularVelocity < 5.f ?
							2.f*(5.f-fLinearVelocity) :
							0.f;
			fAngDamp *= fAngDamp * pBody->fVolumeMassScale;
			pBody->fAngDamp =	!pBody->uiTorsoBone &&
								SAFE_MEMBER(pParentDPO,body.pBody) ?
									fAngDamp + pParentDPO->body.pBody->fAngDamp :
									fAngDamp;
			psdkActorSetAngularDamping(psdkActor, fAngDamp);
			
			//dirty version, better would be to use contact events on ragdolls, also set it as a ratio of the initial velocity's magnitude (or 40 when that's zero)
			if (fLinearVelocity > DYNFXPHYSICS_RAGDOLL_MAX_LINEAR_VELOCITY) {
				psdkActorScaleVel(psdkActor, DYNFXPHYSICS_RAGDOLL_MAX_LINEAR_VELOCITY/fLinearVelocity);
			}

			psdkActorAddForce(psdkActor, pBody->vAdditionalGravity, 1, 0);
		}
	}

#endif
}

extern F32 fMaxDebrisDistance;

static void dynWorldCollIntegrationMsgHandler(const WorldCollIntegrationMsg* msg)
{
	switch(msg->msgType)
	{
		xcase WCI_MSG_FG_BEFORE_SIM_SLEEPS:
		{
		}

		xcase WCI_MSG_NOBG_WHILE_SIM_SLEEPS:
		{
			if(!dynPhysics.wcScene)
			{
				wcSceneCreate(msg, &dynPhysics.wcScene, 1, defaultGravity, "Dyn");
			}

			wcSceneUpdateWorldCollObjectsBegin(msg, dynPhysics.wcScene);
			
			wcSceneGatherWorldCollObjectsByRadius(	msg,
													dynPhysics.wcScene,
													worldGetActiveColl(PARTITION_CLIENT),
													dynPhysics.center,
													fMaxDebrisDistance + 30.0f);

			wcSceneUpdateWorldCollObjectsEnd(msg, dynPhysics.wcScene);
			
			dynPhysics.wciMsg = msg;

			if (eaSize(&dynPhysics.dposQueuedLoad))
			{
				EARRAY_FOREACH_REVERSE_BEGIN(dynPhysics.dposQueuedLoad, i); // must be in reverse order
				{
					DynPhysicsObject* dpo = dynPhysics.dposQueuedLoad[i];
					
					switch(dpo->dpoType)
					{
						xcase DPO_FX:
							dynPhysicsObjectCreateFx(msg, dpo, i);
						
						xcase DPO_SKEL:
							dynPhysicsObjectCreateSkeleton(msg, dpo, i);

						xcase DPO_RAGDOLLBODY:
							dynPhysicsObjectCreateRagdollBody(msg, dpo, i);

						xdefault:
							assert(0);
					}
				}
				EARRAY_FOREACH_END;
			}

			if (eaSize(&dynPhysics.dposNew))
			{
				EARRAY_CONST_FOREACH_BEGIN(dynPhysics.dposNew, i, isize);
				{
					DynPhysicsObject* dpo = dynPhysics.dposNew[i];
					
					switch(dpo->dpoType)
					{
						xcase DPO_FX:
							dynPhysicsObjectCreateFx(msg, dpo, -1);
						
						xcase DPO_SKEL:
							dynPhysicsObjectCreateSkeleton(msg, dpo, -1);

						xcase DPO_RAGDOLLBODY:
							dynPhysicsObjectCreateRagdollBody(msg, dpo, -1);

						xdefault:
							assert(0);
					}
				}
				EARRAY_FOREACH_END;	
				
				eaSetSize(&dynPhysics.dposNew, 0);
			}

			if (eaSize(&dynPhysics.dposPostProcessRagdollBody))
			{
				EARRAY_CONST_FOREACH_BEGIN(dynPhysics.dposPostProcessRagdollBody, i, isize);
				{
					dynPhysicsObjectCreateRagdollJoint(dynPhysics.dposPostProcessRagdollBody[i]);
				}
				EARRAY_FOREACH_END;

				EARRAY_CONST_FOREACH_BEGIN(dynPhysics.dposPostProcessRagdollBody, i, isize);
				{
					dynPhysicsObjectPostProcessRagdollBody(dynPhysics.dposPostProcessRagdollBody[i]);
				}
				EARRAY_FOREACH_END;

				eaPushEArray(&dynPhysics.dposRagdollBody, &dynPhysics.dposPostProcessRagdollBody);
				eaSetSize(&dynPhysics.dposPostProcessRagdollBody, 0);
			}

			if (eaSize(&dynPhysics.dposRagdollBody))
			{
				EARRAY_CONST_FOREACH_BEGIN(dynPhysics.dposRagdollBody, i, isize);
				{
					dynPhysicsUpdateRagdollBody(dynPhysics.dposRagdollBody[i]);
				}
				EARRAY_FOREACH_END;
			}
			
			CommandQueue_ExecuteAllCommands(dpoCommandQueue);
			dynPhysics.wciMsg = NULL;

			EARRAY_CONST_FOREACH_BEGIN(dynPhysics.dpos, i, isize);
			{
				DynPhysicsObject* dpo = dynPhysics.dpos[i];

				wcActorGetMat(msg, dpo->wcActor, dpo->mat);
			}
			EARRAY_FOREACH_END;	

			dynPhysics.fgSlot = !dynPhysics.fgSlot;
			dynPhysics.bgSlot = !dynPhysics.fgSlot;
		}

		xcase WCI_MSG_FG_AFTER_SIM_WAKES:
		{
		}

		xcase WCI_MSG_BG_BETWEEN_SIM:
		{
			EARRAY_CONST_FOREACH_BEGIN(dynPhysics.dpos, i, isize);
			{
				DynPhysicsObject* dpo = dynPhysics.dpos[i];
				DynFxPhysicsUpdateInfo* pInfo = &dpo->updateInfo[dynPhysics.bgSlot];

				if (pInfo)
				{
					/*
					F32 fDragAmount = -CLAMP(pInfo->fDrag * fDeltaTime, 0.0f, 1.0f);
					if (fDragAmount != 0.0f)
					{
						Vec3 vVel;
						wcoGetVel(msg->wco, vVel);
						scaleVec3(vVel, pInfo->fDrag, vVel);
						wcoAddVelocity(msg->wco, vVel);
					}
					*/
					if (pInfo->bAccel)
					{
						FX_CHECK_FINITEVEC3(pInfo->vAccel);
						wcActorAddForce(msg, dpo->wcActor, pInfo->vAccel, true, pInfo->bNeedsWakeup);
					}
				}
			}
			EARRAY_FOREACH_END;
		}
	}
}

void initDynFxPhysics(void)
{
	if(!dpoCommandQueue)
	{
		dpoCommandQueue = CommandQueue_Create(512, false);
	}


	if(!dynPhysics.wci)
	{
		wcIntegrationCreate(&dynPhysics.wci,
							dynWorldCollIntegrationMsgHandler,
							NULL,
							"Dyn");
	}

#if !PSDK_DISABLED
	psdkSetContactCallback(dynFxPhysicsContactQueue);
#endif
	//worldSetCollObjectMsgHandler(WG_WCO_DYNFX_OBJECT, dynFxObjectCollObjectMsgHandler);
	MP_CREATE(DynContactUpdater, 512);

	memPoolDynContactPair[0] = createMemoryPoolNamed("DynContactPair0" MEM_DBG_PARMS_INIT);
	initMemoryPool(memPoolDynContactPair[0], sizeof(DynContactPair), 512);
	mpSetMode(memPoolDynContactPair[0], ZeroMemoryBit);
	memPoolDynContactPair[1] = createMemoryPoolNamed("DynContactPair1" MEM_DBG_PARMS_INIT);
	initMemoryPool(memPoolDynContactPair[1], sizeof(DynContactPair), 512);
	mpSetMode(memPoolDynContactPair[1], ZeroMemoryBit);
}

bool dynFxPhysicsUpdate(DynParticle* pParticle, F32 fDeltaTime)
{
	Mat4 m;
	bool bFoundMat = false;
	DynPhysicsObject* pDPO = pParticle->pDraw->pDPO;
	DynFxPhysicsUpdateInfo* pInfo;
	if ( !pParticle || !pDPO)
		return true;
	copyMat4(pDPO->mat, m);

	if (!FINITEVEC3(pDPO->mat[3]) || !CHECK_DYNPOS_NONFATAL(pDPO->mat[3]))
	{
		Errorf("Got non-finite position from physX for fx debris. Destroying FX.");
		return false;
	}

	if(pDPO->fx.pFx->pPhysicsInfo->bKinematicActor) {

		// Kinematic actor. Take the matrix from the node and move the actor
		// to match that. Not the other way around.

#if !PSDK_DISABLED

		// Only do anything if the actor's actually been set up by now.
		if(pDPO && pDPO->wcActor && pDPO->wcActor->psdkActor && psdkActorIsKinematic(pDPO->wcActor->psdkActor)) {

			Mat4 mat;

			dynNodeGetWorldSpaceMat(&(pParticle->pDraw->node), mat, false);

			QueuedCommand_dpoSetMat(pParticle->pDraw->pDPO, mat);
		}

#endif

		return true;
	
	}

	// Normal actor.

	if(pDPO->fx.pFx->pPhysicsInfo->bPhysicsAttached) {

		// FIXME: Stuff isn't waking up when an attached kinematic actor is
		//   moved. According to the PhysX docs this should not be the case.
		//   So just keep attached stuff awake.

		QueuedCommand_dpoWakeUp(pParticle->pDraw->pDPO);
	}

	{
		Quat qRot;
		Vec3 vPos;
		mat3ToQuat(m, qRot);
		copyVec3(m[3], vPos);
		dynNodeSetPos(&pParticle->pDraw->node, vPos);
		dynNodeSetRot(&pParticle->pDraw->node, qRot);
		bFoundMat = true;
	}

	{
		//FXTHREAD // Needs to be queued for FX threading!

		Vec3 vOldAccel;
		bool bStartAsleep = false;

		if(pDPO->dpoType == DPO_FX) {
			DynFx *pFx = pDPO->fx.pFx;
			if(pFx && pFx->pPhysicsInfo) {
				bStartAsleep = pFx->pPhysicsInfo->bStartAsleep;
			}
		}

		pInfo = &pDPO->updateInfo[dynPhysics.fgSlot];
		copyVec3(pInfo->vAccel, vOldAccel);
		pInfo->vAccel[1] = -pParticle->pDraw->fGravity;

		if(!sameVec3(pInfo->vAccel, vOldAccel)) {
			pInfo->bNeedsWakeup = !bStartAsleep;
		} else {
			pInfo->bNeedsWakeup = false;
		}
	}


	pInfo->bAccel = !!(lengthVec3Squared(pInfo->vAccel) > 0.0f);
	pInfo->fDrag = pParticle->pDraw->fDrag;

	if (bFoundMat)
	{
		Vec3 vWind;
		F32 fMag = dynWindGetAtPosition(m[3], vWind, true);
		AUTO_FLOAT(fWindScale, 0.05f);
		fMag *= fWindScale / (pParticle->fDensity>0?pParticle->fDensity:1.0);
		scaleVec3(vWind, fMag, vWind);
		FX_CHECK_FINITEVEC3(vWind);
		QueuedCommand_dpoAddForce(pParticle->pDraw->pDPO, vWind, true, false);
	}


	FOR_EACH_IN_EARRAY(pInfo->eaContactPairs, DynContactPair, pPair)
	{
		dynFxPhysicsContact(pParticle->pDraw->pDPO, pPair);
		mpFree(memPoolDynContactPair[dynPhysics.fgSlot], pPair);
	}
	FOR_EACH_END;
	eaClear(&pInfo->eaContactPairs);
	/*
	{
		F32 fGravityDiff = -pParticle->draw.fGravity - defaultGravity;
		if (fabsf(fGravityDiff) > EPSILON)
		{
			Vec3 vAddedVel = { 0.0f, fGravityDiff * fDeltaTime, 0.0f };
			QueuedCommand_wcoAddVelocity(pParticle->draw.pDPO, vAddedVel);
		}
	}
	if ( pParticle->draw.fDrag != 0.0f )
	{
		F32 fDragAmount = -CLAMP(pParticle->draw.fDrag * fDeltaTime, 0.0f, 1.0f);
		Vec3 vVel;
		wcoGetVel(pParticle->draw.pDPO, vVel);
		scaleVec3(vVel, fDragAmount, vVel);
		QueuedCommand_wcoAddVelocity(pParticle->draw.pDPO, vVel);
	}
	*/
	return true;
}

bool dynFxRaycast(int iPartitionIdx, DynFx* pFx, const DynNode* pOriginNode, F32 fRange, DynNode* pResultNode, bool bOrientToNormal, bool bUseParentRotation, const char** ppcPhysProp, bool bForceRayDown, bool bCopyScale, eDynRaycastFilter eFilter)
{
#if !PSDK_DISABLED
	Vec3 vStart, vEnd, vZAxis, vDir;
	Quat qRot;
	bool bHit;

	Vec3 vImpactPos;
	Vec3 vImpactNormal;

	vZAxis[0] = 0.0f;
	vZAxis[1] = 0.0f;
	vZAxis[2] = fRange;

	// Figure out start and end vecs

	dynNodeGetWorldSpacePos(pOriginNode, vStart);
	if (bForceRayDown)
	{
		vDir[0] = vDir[2] = 0.0f;
		vDir[1] = -fabsf(fRange);
		quatW(qRot) = 0.0f;
		quatX(qRot) = 1.0f;
		quatY(qRot) = 0.0f;
		quatZ(qRot) = 0.0f;
	}
	else
	{
		dynNodeGetWorldSpaceRot(pOriginNode, qRot);
		quatRotateVec3(qRot, vZAxis, vDir);
	}

	addVec3(vDir, vStart, vEnd);

	{
		int iShapeGroup = 0;

		if (eFilter & eDynRaycastFilter_World)
			iShapeGroup |= WC_QUERY_BITS_WORLD_ALL;
		if (eFilter & eDynRaycastFilter_Shield)
			iShapeGroup |= WC_FILTER_BIT_SHIELD;
		if (eFilter & eDynRaycastFilter_Hull)
			iShapeGroup |= WC_FILTER_BIT_HULL;

		if (iShapeGroup & WC_FILTER_BIT_SHIELD || iShapeGroup & WC_FILTER_BIT_HULL) // right now we make this exclusive, but we probably need to change it eventually
		{
			PSDKScene* psdkScene = NULL;
			PSDKRaycastResults psdkResults;
			DynNode* pTargetRoot = GET_REF(pFx->hOrientToNode);
			while (pTargetRoot && pTargetRoot->pParent)
				pTargetRoot = pTargetRoot->pParent;
			wcSceneGetPSDKScene(dynPhysics.wcScene, &psdkScene);
			bHit = psdkRaycastClosestShape(	psdkScene,
											NULL,
											vStart,
											vEnd,
											iShapeGroup,
											(intptr_t)pTargetRoot,
											&psdkResults);
			if (bHit)
			{
				copyVec3(psdkResults.posWorldImpact, vImpactPos);
				copyVec3(psdkResults.normalWorld, vImpactNormal);
			}
		}
		else
		{
			WorldCollCollideResults results;
			bHit = wcRayCollide(worldGetActiveColl(PARTITION_CLIENT), vStart, vEnd, iShapeGroup, &results);
			if (bHit)
			{
				if (ppcPhysProp)
					*ppcPhysProp = allocAddString(wcoGetPhysicalPropertyName(results.wco, results.tri.index, results.posWorldImpact));
				copyVec3(results.posWorldImpact, vImpactPos);
				copyVec3(results.normalWorld, vImpactNormal);
			}
		}

		if(eFilter & eDynRaycastFilter_WaterVolume) {

			U32 water_volume_type = wlVolumeTypeNameToBitMask( "Water" );
			Vec3 vVolumeHitLocation;
			bool bVolumeHit = wlVolumeRayCollide(iPartitionIdx, vStart, vEnd, water_volume_type, vVolumeHitLocation);

			if(bVolumeHit) {

				// Volume ray collision doesn't give us any useful
				// normal information. So just do this instead. Normal
				// will be the inverted direction of the raycast.
				Vec3 vFakeImpactNormal;
				subVec3(vStart, vEnd, vFakeImpactNormal);
				normalVec3(vFakeImpactNormal);

				if(bHit) {

					// Already hit something. See if this is closer.
					if(distance3Squared(vStart, vVolumeHitLocation) < distance3Squared(vStart, vImpactPos)) {
						copyVec3(vVolumeHitLocation, vImpactPos);
						copyVec3(vFakeImpactNormal, vImpactNormal);
						if(ppcPhysProp) {
							*ppcPhysProp = allocAddString("Water");
						}
					}

				} else {
					copyVec3(vVolumeHitLocation, vImpactPos);
					copyVec3(vFakeImpactNormal, vImpactNormal);
					if(ppcPhysProp) {
						*ppcPhysProp = allocAddString("Water");
					}
				}

				bHit = true;
			}
		}

	}

	if (bHit)
	{
		if (dynDebugState.bDrawRays && wl_state.drawLine3D_2_func)
		{
			wl_state.drawLine3D_2_func(vStart, vImpactPos, 0xFFFFFFFF, 0xFF00FF00);
		}
		dynNodeSetPos(pResultNode, vImpactPos);

		if (bOrientToNormal)
		{
			{
				Quat qOrigRot, qResult;
				Vec3 vOrigForward;
				Mat3 mat;

				dynNodeGetWorldSpaceRot(pOriginNode, qOrigRot);
				quatRotateVec3(qOrigRot, forwardvec, vOrigForward);
				if (fabsf(vOrigForward[1]) >= 0.9999f)
					copyVec3(forwardvec, vOrigForward);

				orientMat3ToNormalAndForward(mat, vImpactNormal, bUseParentRotation?vOrigForward:forwardvec);
				mat3ToQuat(mat, qResult);
				dynNodeSetRot(pResultNode, qResult);
			}
			/*
			{
				Quat qTemp1, qTemp2, qTemp3;
				Quat qYaw, qOriginRot;
				Vec3 vXAxis = { 1.0f, 0.0f, 0.0f };
				Vec3 vPYR;
				dynNodeGetWorldSpaceRot(pOriginNode, qOriginRot);
				quatToPYR(qOriginRot, vPYR);
				yawQuat(vPYR[1], qYaw);
				orientMat3(mat, results.normal);
				mat3ToQuat(mat, qTemp1);
				axisAngleToQuat(vXAxis, -HALFPI, qTemp2);
				quatMultiply(qYaw, qTemp1, qTemp3);
				quatMultiply(qTemp2, qTemp3, qTemp1);
				quatMultiply(qYaw, qTemp1, qRot);
				dynNodeSetRot(pResultNode, qRot);
			}
			*/
		}
		else
			dynNodeSetRot(pResultNode, qRot);
	}
	else
	{
		if (dynDebugState.bDrawRays && wl_state.drawLine3D_2_func)
		{
			wl_state.drawLine3D_2_func(vStart, vEnd, 0xFFFFFFFF, 0xFFFF0000);
		}

		if(CHECK_DYNPOS_NONFATAL(vEnd)) {
			dynNodeSetPos(pResultNode, vEnd);
		} else {
			DynFxInfo *pFxInfo = GET_REF(pFx->hInfo);
			Vec3 vSafePos = {0,0,0};
			if(pFxInfo) {
				ErrorFilenamef(pFxInfo->pcFileName, "Raycast went way out of bounds. Killing FX.");
			}
			dynFxKill(pFx, false, false, false, eDynFxKillReason_Error);
			dynNodeSetPos(pResultNode, vSafePos);
		}
		dynNodeSetRot(pResultNode, qRot);
	}

	if (bCopyScale)
	{
		Vec3 vOriginalScale;
		dynNodeGetWorldSpaceScale(pOriginNode, vOriginalScale);
		dynNodeSetScale(pResultNode, vOriginalScale);
	}

	return bHit;
#else
	return false;
#endif
}

void dynFxForceVecToParticle(const Vec3 vForcePos, const DynNode* pFxNode, Vec3 vVecTo)
{
	Vec3 vFxPos;
	dynNodeGetWorldSpacePos(pFxNode, vFxPos);
	subVec3(vFxPos, vForcePos, vVecTo);
}

void dynFxApplyForce(const DynForce* pForce, const Vec3 vForcePos, const DynNode* pFxNode, DynPhysicsObject* pDPO, F32 fDensity, F32 fDeltaTime, Quat qOrientation)
{
	Vec3 vParticlePos;
	Vec3 vForce;

	dynNodeGetWorldSpacePos(pFxNode, vParticlePos);
	if (dynFxGetForceEffect(pForce, vForcePos, vParticlePos, fDensity, fDeltaTime, vForce, qOrientation))
		QueuedCommand_dpoAddForce(pDPO, vForce, true, true);
}
#endif // !SPU

bool dynFxGetForceEffect(const DynForce* pForce, const Vec3 vForcePos, const Vec3 vSamplePos, F32 fDensity, F32 fDeltaTime, Vec3 vEffectOut, Quat qOrientation)
{
	Vec3 vForce;
	F32 fLength;
	subVec3(vSamplePos, vForcePos, vForce);
	fLength = lengthVec3(vForce);
	
	setVec3same(vEffectOut, 0);

	if (fLength < pForce->fForceRadius)
	{
		Vec3 vForceNotOriented;
		F32 fForcePower = (pForce->eForceFallOff == eDynForceFallOff_None) ? pForce->fForcePower : pForce->fForcePower * (1.0f - (fLength / pForce->fForceRadius)) / fDensity; // assumes power scales linear from 100% to 0% from center to outer edge of radius
		if (!pForce->bImpulse)
			fForcePower *= MAX(fDeltaTime, 0.5f);
		fForcePower *= 100.0f; // just to make the numbers a bit nicer for the artists
		setVec3same(vForceNotOriented, 0);

		switch(pForce->eForceType)
		{
			xcase eDynForceType_Out:
			{
				if(fLength < 0.001) {
					scaleVec3(vForce, fForcePower / 0.001, vForce);
				} else {
					scaleVec3(vForce, fForcePower / fLength, vForce);
				}
				FX_CHECK_FINITEVEC3(vForce);
				copyVec3(vForce, vEffectOut);
			}
			xcase eDynForceType_Up:
			{
				scaleVec3(upvec, fForcePower, vForce);
				FX_CHECK_FINITEVEC3(vForce);
				copyVec3(vForce, vForceNotOriented);

				// Rotate it into alignment.
				quatRotateVec3(qOrientation, vForceNotOriented, vEffectOut);
			}
			xcase eDynForceType_Side:
			{
				scaleVec3(sidevec, fForcePower, vForce);
				FX_CHECK_FINITEVEC3(vForce);
				copyVec3(vForce, vForceNotOriented);

				// Rotate it into alignment.
				quatRotateVec3(qOrientation, vForceNotOriented, vEffectOut);
			}
			xcase eDynForceType_Forward:
			{
				scaleVec3(forwardvec, fForcePower, vForce);
				FX_CHECK_FINITEVEC3(vForce);
				copyVec3(vForce, vForceNotOriented);

				// Rotate it into alignment.
				quatRotateVec3(qOrientation, vForceNotOriented, vEffectOut);
			}
			xcase eDynForceType_Swirl:
			{
				Vec3 vTemp;
				if(fLength < 0.001) {
					scaleVec3(vForce, 1000.0, vForce);
				} else {
					scaleVec3(vForce, 1.0 / fLength, vForce);
				}
				crossVec3Up(vForce, vTemp);
				scaleVec3(vTemp, fForcePower, vForce);
				FX_CHECK_FINITEVEC3(vForce);
				copyVec3(vForce, vEffectOut);
			}
		}

	}
	else
	{
		return false;
	}
	return true;
}

#if !SPU
void dynForceEnqueuePayload(DynFxRegion* pFxRegion, const DynForce* pForce, const Vec3 vForcePos, const Quat qOrientation)
{
	DynForcePayload* pPayload = malloc(sizeof(*pPayload));
	memcpy(&pPayload->force, pForce, sizeof(DynForce));
	copyVec3(vForcePos, pPayload->vForcePos);
	copyQuat(qOrientation, pPayload->qOrientation);
	eaPush(&pFxRegion->eaForcePayloads[!pFxRegion->uiCurrentPayloadArray], pPayload);
}


void dynFxForceUpdate(const DynNode* pNode, DynFxRegion* pFxRegion, const DynForce* pForce, DynFxTime uiDeltaTime)
{
	Vec3 vForcePos;
	Quat qOrientation;
	DynFx** eaNodes = NULL;
	if (pForce->eForceType == eDynForceType_None || !pFxRegion)
		return;
	dynNodeGetWorldSpacePos(pNode, vForcePos);
	dynNodeGetWorldSpaceRot(pNode, qOrientation);
	dynForceEnqueuePayload(pFxRegion, pForce, vForcePos, qOrientation);
}


static void dynFxPhysicsContact(DynPhysicsObject* pDPO, DynContactPair* pPair)
{
	DynFx* pFx = pDPO->fx.pFx;
	if (pFx)
	{
		DynFxInfo* pInfo = GET_REF(pFx->hInfo);
		DynContactUpdater* pUpdater = NULL;
		U32 uiHit = 0;
		if (!pFx->pParticle || !pDPO || !pInfo)
			return;

		// First, try to find this contact event
		FOR_EACH_IN_EARRAY(pDPO->eaContactUpdaters, DynContactUpdater, pPossibleUpdater)
		{
			if (1 || pPossibleUpdater->pUID == pPair->pUID)
			{
				pUpdater = pPossibleUpdater;
				break;
				// Found it
			}
		}
		FOR_EACH_END;

		if (!pUpdater)
		{
			// add one
			pUpdater = MP_ALLOC(DynContactUpdater);
			pUpdater->pUID = pPair->pUID;
			pUpdater->uiEventActive = 0;
			eaPush(&pDPO->eaContactUpdaters, pUpdater);
		}

		if (pPair->bContactPoint)
		{
			Mat4 mat;
			mat3FromUpVector(pPair->vContactNorm, mat);
			copyVec3(pPair->vContactPos, mat[3]);
			if(!dynNodeSetFromMat4(&pUpdater->contactNode, mat)) {
				if(pFx && pInfo) {
					const char *fxName = "<unknown>";
					if(pInfo) {
						fxName = pInfo->pcDynName;
					}
					Errorf("dynPhysicsContact failed for FX %s. Debris may have fallen off the map.", fxName);
					pFx->bKill = true;
				}
			}
		}

		// Mark all the events which qualify as hitting
		FOR_EACH_IN_EARRAY(pInfo->eaContactEvents, DynContactEvent, pEvent)
		{
			if (pPair->bTouching && pPair->fContactForceTotal >= pEvent->fMinForce && (pEvent->fMaxForce <= 0.0f || pPair->fContactForceTotal <= pEvent->fMaxForce) )
			{
				if (!pEvent->bMissEvent)
					SETB(&uiHit, ipEventIndex);
			}
			else if (pEvent->bMissEvent)
			{
				SETB(&uiHit, ipEventIndex);
			}
		}
		FOR_EACH_END;


		// Now process the state of the events
		FOR_EACH_IN_EARRAY(pInfo->eaContactEvents, DynContactEvent, pEvent)
		{
			if ( (!pEvent->bFireOnce || !TSTB(&pDPO->uiFiredOnce, ipEventIndex) ) && !TSTB(&pUpdater->uiEventActive, ipEventIndex) && TSTB(&uiHit, ipEventIndex)) // new event
			{
				SETB(&pUpdater->uiEventActive, ipEventIndex);
				SETB(&pDPO->uiFiredOnce, ipEventIndex);
				dynFxCallChildDyns(pFx, &pEvent->childCallCollection, NULL);
				dynFxCallEmitterStarts(pFx->pParticle, &pEvent->eaEmitterStart, pFx->fHue, pFx->fSaturationShift, pFx->fValueShift, &pUpdater->contactNode, pFx->iPriorityLevel, pFx);
				//dynFxCallLoopStarts(pEventUpdater, &pEvent->eaLoopStart, pFx);
				FOR_EACH_IN_EARRAY(pEvent->eaSoundStart, const char, pcSound)
				{
					dynSoundStart(pFx, &pUpdater->soundUpdater, pcSound);
				}
				FOR_EACH_END;
				dynFxSendMessages(pFx, &pEvent->eaMessage);
			}
			else if (TSTB(&pUpdater->uiEventActive, ipEventIndex) && !TSTB(&uiHit, ipEventIndex)) // event ends
			{
				CLRB(&pUpdater->uiEventActive, ipEventIndex);
				dynFxCallEmitterStops(pFx->pParticle, &pEvent->eaEmitterStart, pFx);
				//dynFxCallLoopEnds(pFx, &pEvent->eaLoopStart);
				FOR_EACH_IN_EARRAY(pEvent->eaSoundStart, const char, pcSound)
				{
					dynSoundStop(&pUpdater->soundUpdater, pcSound);
				}
				FOR_EACH_END;
			}
		}
		FOR_EACH_END;
	}
}

void dynFxPhysicsContactQueue(PSDKActor* pActor, void* pUID, bool bTouching, F32 fContactForceTotal, bool bContactPoint, const Vec3 vContactPos, const Vec3 vContactNorm)
{
#if !PSDK_DISABLED
	if (pActor)
	{
		WorldCollActor* pWCA;
		if (psdkActorGetUserPointer((void**)&pWCA, pActor) && pWCA)
		{
			DynPhysicsObject* pDPO;
			if (wcActorGetUserPointer(pWCA, (void**)(&pDPO)) && pDPO)
			{
				DynFxPhysicsUpdateInfo* pInfo = &pDPO->updateInfo[dynPhysics.bgSlot];
				DynContactPair* pPair = mpAlloc(memPoolDynContactPair[dynPhysics.bgSlot]);
				pPair->pUID = pUID;
				pPair->bTouching = bTouching;
				pPair->fContactForceTotal = fContactForceTotal;
				pPair->bContactPoint = bContactPoint;
				copyVec3(vContactPos, pPair->vContactPos);
				copyVec3(vContactNorm, pPair->vContactNorm);
				eaPush(&pInfo->eaContactPairs, pPair);
			}
		}
	}
#endif
}

void dynFxPhysicsDrawScene(void)
{
#if !PSDK_DISABLED
	PSDKScene* pScene = NULL;
	if (wcSceneGetPSDKScene(dynPhysics.wcScene, &pScene))
	{
		psdkSceneDrawDebug(pScene);
	}
#endif
}


#include "dynFxPhysics_h_ast.c"
#endif // !SPU
