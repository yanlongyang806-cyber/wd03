#define NO_MEMCHECK_OK // to allow including stdtypes.h in headers

#include "PhysicsSDKPrivate.h"

#if !PSDK_DISABLED

#include "PhysXLoader.h"
#include "NxPhysics.h"
#include "NxPhysicsSDK.h"
#include "NxMaterial.h"
#include "NxScene.h"

#include "NxActor.h"
#include "NxTriangleMeshDesc.h"
#include "NxTriangleMeshShapeDesc.h"
#include "NxHeightField.h"
#include "NxHeightFieldDesc.h"
#include "NxCooking.h"
#include "NxUserOutputStream.h"
#include "sysutil.h"

// After Novodex, before any of ours
#undef NO_MEMCHECK_OK
#undef NO_MEMCHECK_H
#include "memcheck.h"
#include "timing.h"
#include "file.h"

#include "WinInclude.h"
#include "WorldLib.h"
#include "AssociationList.h"
#include "WorkerThread.h"
#include "XboxThreads.h"

#include "Capsule.h"
#include "GenericMesh.h"
#include "utils.h"
#include "error.h"
#include "memlog.h"
#include "earray.h"
#include "strings_opt.h"
#include "StashTable.h"
#include "MemoryPool.h"
#include "Quat.h"
#include "wlState.h"
#include "dynRagdollData.h"
#include "MemoryMonitor.h"
#include "osdependent.h"
#include "process_util.h"
#include "sysutil.h"
#include "UTF8.h"

C_DECLARATIONS_BEGIN
	#include "zutils.h"
	#include "wlModelLoad.h"
C_DECLARATIONS_END

// These are the #includes that I will remove.

#include "wlBeacon.h"
#include "WorldColl.h"

#if !_PS3
	#define USE_CUSTOM_SCHEDULER 0
#endif

#if 0
	// ST: Added these macros to track down something going to non-finite
	#define CHECK_FINITEVEC3(vec) assert(FINITEVEC3(vec))
	#define CHECK_FINITEQUAT(vec) assert(FINITEVEC4(vec))
#else
	#define CHECK_FINITEVEC3(vec)
	#define CHECK_FINITEQUAT(vec)
#endif

// The max scene count is a hardcoded value in PhysX.


static bool disableThreadedCooking = false;

typedef enum PSDKShapeDescType {
	PSDK_SD_NONE = 0,
	PSDK_SD_COOKED_MESH,
	PSDK_SD_CAPSULE,
	PSDK_SD_BOX,
	PSDK_SD_SPHERE,
} PSDKShapeDescType;

typedef struct PSDKShapeDesc PSDKShapeDesc;

typedef struct PSDKShapeDesc {
	PSDKShapeDesc*				next;
	PSDKShapeDescType			shapeDescType;
	
	union {
		PSDKCookedMesh*			mesh;
		
		struct {
			F32					length;
			F32					radius;
		} capsule;
		
		struct {
			F32					radius;
		} sphere;
		
		struct {
			Vec3				xyzHalfSize;
		} box;
	};
	
	Mat4						mat;
	F32							density;
	U32							materialIndex;
	U32							filterBits;
	U16							shapeGroup;
	U32							shapeTag;
} PSDKShapeDesc;

typedef struct PSDKActorDesc {
	Mat4						mat;
	AssociationList*			alMeshes;
	F32							density;
	PSDKShapeDesc				sdHead;
	PSDKShapeDesc*				sdTail;
	U32							startAsleep:1;
} PSDKActorDesc;

typedef void (*PSDKQueuedReleaseHandler)(void* userPointer);

typedef enum PSDKQueuedReleaseType {
	PSDK_QR_SCENE,
	PSDK_QR_TRIANGLE_MESH,
	PSDK_QR_CONVEX_MESH,
	PSDK_QR_HEIGHTFIELD,
	PSDK_QR_BOX,
	PSDK_QR_COOKED_MESH,
	PSDK_QR_COOKED_STREAM,
	PSDK_QR_COUNT,
} PSDKQueuedReleaseType;

typedef struct PSDKQueuedRelease {
	PSDKQueuedReleaseType	releaseType;
	void*					userPointer;
} PSDKQueuedRelease;

NxPhysicsSDK*				nxSDK;

static PackType getPackType(S32 elemSize,
							S32 isF32)
{
	PackType packType;

	if(isF32){
		assert(elemSize == sizeof(F32));
		packType = PACK_F32;
	}
	else if(elemSize == sizeof(U32)){
		packType = PACK_U32;
	}
	else if(elemSize == sizeof(U16)){
		packType = PACK_U16;
	}
	else if(elemSize == sizeof(U8)){
		packType = PACK_U8;
	}else{
		assert(0);
	}

	return packType;
}

static U8* compressDeltasWrapper(	const void* data,
									S32* length,
									S32 stride,
									S32 count,
									S32 elem_size,
									bool is_float)
{
	F32		float_scale = 1;
	F32		inv_float_scale = 1;
	U32		orig_zip_len;
	U32		delta_zip_len;
	void*	orig_zip;
	void*	delta_zip;
	U8*		outdata;

	if(is_float)
	{
		float_scale = 32768.f;
		inv_float_scale = 1.f / float_scale;
	}

	outdata = wlCompressDeltas(	data,
								length,
								stride,
								count,
								getPackType(elem_size, is_float),
								float_scale,
								inv_float_scale);

	if(*length % 2)
	{
		U8* reallocOutData = (U8*)realloc(outdata, (*length) + 1);
		assert(reallocOutData);
		ANALYSIS_ASSUME(reallocOutData);
		outdata = reallocOutData;
		outdata[*length] = 0;
		(*length)++;
	}

	orig_zip = zipData((void*)data, stride * count * elem_size, &orig_zip_len);
	delta_zip = zipData(outdata, *length, &delta_zip_len);

	SAFE_FREE(orig_zip);
	SAFE_FREE(delta_zip);

	if(orig_zip_len <= delta_zip_len)
	{
		SAFE_FREE(outdata);
	}

	return outdata;
}

static S32 uncompressDeltasWrapper(void* dst, const U8* src, S32 stride, S32 count, S32 elem_size, bool is_float)
{
	return wlUncompressDeltas(dst, src, stride, count, getPackType(elem_size, is_float));
}

static void psdkEnterCS(void){
	EnterCriticalSection(&psdkState.csMainPSDK);
}

static void psdkLeaveCS(void){
	LeaveCriticalSection(&psdkState.csMainPSDK);
}

static void psdkCookingEnterCS(void){
	EnterCriticalSection(&psdkState.csCooking);
}

static void psdkCookingLeaveCS(void){
	LeaveCriticalSection(&psdkState.csCooking);
}

static void psdkCookedMeshDestroyInternal(PSDKCookedMesh* mesh);

static void psdkQueuedReleaseProcess(	PSDKQueuedReleaseType releaseType,
										void* userPointer)
{
	switch(releaseType){
		xcase PSDK_QR_SCENE:{
			nxSDK->releaseScene(*(NxScene*)userPointer);

			assert(psdkState.scene.count > 0);

			psdkState.scene.count--;
		}
		
		xcase PSDK_QR_TRIANGLE_MESH:{
			nxSDK->releaseTriangleMesh(*(NxTriangleMesh*)userPointer);
		}

		xcase PSDK_QR_CONVEX_MESH:{
			nxSDK->releaseConvexMesh(*(NxConvexMesh*)userPointer);
		}

		xcase PSDK_QR_HEIGHTFIELD:{
			nxSDK->releaseHeightField(*(NxHeightField*)userPointer);
		}
		
		xcase PSDK_QR_COOKED_MESH:{
			PSDKCookedMesh* mesh = (PSDKCookedMesh*)userPointer;

			ASSERT_TRUE_AND_RESET(mesh->flags.queuedForRelease);

			alInvalidate(mesh->alRefs);

			if(alIsEmpty(mesh->alRefs)){
				psdkCookedMeshDestroyInternal(mesh);
			}
		}

		xcase PSDK_QR_COOKED_STREAM:{
			PSDKAllocedStream* stream = (PSDKAllocedStream*)userPointer;
			delete stream;
		}
	}
}

static void psdkEnterQueuedReleaseCS(void){
	EnterCriticalSection(&psdkState.csQueuedReleases);
}

static void psdkLeaveQueuedReleaseCS(void){
	LeaveCriticalSection(&psdkState.csQueuedReleases);
}

static void psdkQueuedReleaseAdd(	PSDKQueuedReleaseType releaseType,
									void** userPointer)
{
	if(!SAFE_DEREF(userPointer)){
		return;
	}
	
	if(	GetCurrentThreadId() == psdkState.soThreadID &&
		!psdkState.simulationLockCountFG)
	{
		psdkQueuedReleaseProcess(releaseType, *userPointer);
	}else{
		PSDKQueuedRelease* qr = callocStruct(PSDKQueuedRelease);
		
		qr->releaseType = releaseType;
		qr->userPointer = *userPointer;
		
		psdkEnterQueuedReleaseCS();
		{
			eaPush(&psdkState.queuedReleases, qr);
		}
		psdkLeaveQueuedReleaseCS();
	}
	
	*userPointer = NULL;
}

static void psdkQueuedReleaseProcessAll(void){
	assert(GetCurrentThreadId() == psdkState.soThreadID);
	assert(!psdkState.simulationLockCountFG);

	if(psdkState.queuedReleases){
		PERFINFO_AUTO_START_FUNC();
		psdkEnterQueuedReleaseCS();
		{
			EARRAY_CONST_FOREACH_BEGIN(psdkState.queuedReleases, i, isize);
			{
				PSDKQueuedRelease* qr = psdkState.queuedReleases[i];
				
				psdkLeaveQueuedReleaseCS();
				{
					// This can be run without the lock on the queue.
					
					psdkQueuedReleaseProcess(qr->releaseType, qr->userPointer);
				}
				psdkEnterQueuedReleaseCS();

				SAFE_FREE(psdkState.queuedReleases[i]);
			}
			EARRAY_FOREACH_END;

			eaDestroy((cEArrayHandle*)&psdkState.queuedReleases);
		}
		psdkLeaveQueuedReleaseCS();
		PERFINFO_AUTO_STOP();
	}
}

struct PSDKUserNotify : public NxUserNotify {
	virtual bool onJointBreak(	NxReal breakingForce,
								NxJoint& brokenJoint)
	{
		// Returning true frees the joint.

		return true;
	}

	virtual void onWake(NxActor** actors,
						NxU32 count)
	{
	}
	
	virtual void onSleep(	NxActor**actors,
							NxU32 count)
	{
	}
};

static PSDKUserNotify psdkUserNotify;

S32 psdkSetContactCallback(PSDKContactCB callback)
{
	psdkState.cbContact = callback;
	return 1;
}

struct PSDKUserContactReport : public NxUserContactReport {
	virtual void onContactNotify(	NxContactPair& pair,
									NxU32 events)
	{
		S32 i;
		if(psdkState.cbContact)
		{
			if(pair.isDeletedActor[0] || pair.isDeletedActor[1]) 
				return;

			for (i=0; i<2; ++i)
			{
				PSDKActor* pActor = (PSDKActor*)pair.actors[i]->userData;
				NxActorGroup group;

				if(!pActor)	
				{
					devassertmsg(0, "Somehow contacting deleted actor without deleted flag existing.");
					return;
				}

				group = pActor->nxActor->getGroup();
				if(pActor->flags.hasContactEvent)
				{
					if( events & (NX_NOTIFY_ON_TOUCH|NX_NOTIFY_ON_START_TOUCH) )
					{
						NxVec3 vContactPos, vContactNorm;
						NxVec3 vBestContactPos, vBestContactNorm;
						bool bContactPointExists = false;

						float fContactForceTotal = pair.sumFrictionForce.magnitudeSquared() + pair.sumNormalForce.magnitudeSquared();

						NxContactStreamIterator streamIter(pair.stream);
						while(streamIter.goNextPair())
						{
							while(streamIter.goNextPatch())
							{
								vContactNorm = streamIter.getPatchNormal();

								while(streamIter.goNextPoint())
								{
									vContactPos = streamIter.getPoint();

									vBestContactNorm = vContactNorm;
									vBestContactPos = vContactPos;
									bContactPointExists = true;
								}
							}
						}

						if(bContactPointExists)
						{
							Vec3 vFinalContactPos, vFinalContactNorm;
							if(fabsf(vBestContactNorm.magnitudeSquared() - 1.0f) > 0.1f)
							{
								vBestContactNorm.zero();
								vBestContactNorm.y = 1.0f;
							}

							// The contact normal should always point toward the dynamic object, for consitency.

							if( pair.actors[0]->isDynamic() && !pair.actors[1]->isDynamic() )
							{
								// actor 0 is dynamic, flip the normal if it points away
								NxVec3 vTowardsDynamicActor = pair.actors[0]->getGlobalPosition() - vBestContactPos;
								if( vBestContactNorm.dot(vTowardsDynamicActor) < 0.0f  )
									vBestContactNorm *= -1.0f;
							}
							else if( !pair.actors[0]->isDynamic() && pair.actors[1]->isDynamic() )
							{
								// actor 1 is dynamic, flip the normal if it points away
								NxVec3 vTowardsDynamicActor = pair.actors[1]->getGlobalPosition() - vBestContactPos;
								if( vBestContactNorm.dot(vTowardsDynamicActor) < 0.0f  )
									vBestContactNorm *= -1.0f;		
							}
							// else, they are both dynamic, in which case we don't know what to do!
							vBestContactPos += vBestContactNorm * 0.1f;
							vBestContactPos += NxVec3(qfrand() * 0.02f, qfrand() * 0.02f, qfrand() * 0.02f);

							setVec3(vFinalContactPos, vBestContactPos.x, vBestContactPos.y, vBestContactPos.z);
							setVec3(vFinalContactNorm, vBestContactNorm.x, vBestContactNorm.y, vBestContactNorm.z);
							psdkState.cbContact(pActor, (void*)pair.actors[!i], true, fContactForceTotal, bContactPointExists, vFinalContactPos, vFinalContactNorm);
						}
						else
						{
							psdkState.cbContact(pActor, (void*)pair.actors[!i], true, 0.0f, bContactPointExists, zerovec3, zerovec3);
						}
					}
					else if(events & NX_NOTIFY_ON_END_TOUCH)
					{
						psdkState.cbContact(pActor, (void*)pair.actors[!i], false, 0.0f, false, zerovec3, zerovec3);
					}
				}
			}
		}
	}
};

S32 psdkCanModify(PSDKSimulationOwnership* so){
	if(GetCurrentThreadId() == psdkState.soThreadID){
		if(psdkState.simulationLockCountFG){
			return 0;
		}
	}
	else if(!psdkState.so ||
			psdkState.so != so ||
			psdkState.simulationLockCountBG)
	{
		return 0;
	}
	
	return 1;
}

#if USE_CUSTOM_SCHEDULER
struct PSDKScheduler : public NxUserScheduler {
	virtual void addTask(NxTask* task){
		PERFINFO_AUTO_START_FUNC();
		task->execute();
		PERFINFO_AUTO_STOP();
	}
 
	virtual void addBackgroundTask(NxTask* task){
		PERFINFO_AUTO_START_FUNC();
		task->execute();
		PERFINFO_AUTO_STOP();
	}
	
	virtual void waitTasksComplete(){
	}
};

static PSDKScheduler psdkScheduler;
#endif

static PSDKUserContactReport psdkUserContactReport;

S32 psdkSceneCreate(PSDKScene** sceneOut,
					PSDKSimulationOwnership* so,
					const PSDKSceneDesc* sceneDesc,
					void* userPointer,
					PSDKGeoInvalidatedFunc geoInvalidatedFunc)
{
	if(psdkState.scene.count >= PSDK_MAX_SCENE_COUNT){
		psdkState.flags.sceneRequestFailed = 1;
		return 0;
	}

	if(	!nxSDK ||
		!sceneOut ||
		!sceneDesc ||
		!psdkCanModify(so))
	{
		return 0;
	}

	PERFINFO_AUTO_START("psdkCreateScene", 1);
	{
		NxScene*	nxScene;
		NxSceneDesc nxSceneDesc;
		NxBounds3	maxBounds;
		const F32	maxBoundXYZ = 256 * 1024;
		
		#if USE_CUSTOM_SCHEDULER
			nxSceneDesc.customScheduler = &psdkScheduler;
		#endif

		#if _XBOX
			nxSceneDesc.simThreadMask = THREADMASK_PHYSX;
			nxSceneDesc.threadMask = THREADMASK_PHYSX;
			nxSceneDesc.backgroundThreadMask = THREADMASK_PHYSX;
		#endif
		
		if(0 && sceneDesc->useVariableTimestep){
			nxSceneDesc.timeStepMethod = NX_TIMESTEP_FIXED;
			
			if(sceneDesc->maxIterations){
				nxSceneDesc.maxIter = sceneDesc->maxIterations;
			}
		}else{
			nxSceneDesc.timeStepMethod = NX_TIMESTEP_VARIABLE;
		}

		nxSceneDesc.gravity.set(0, sceneDesc->gravity, 0);
		
		maxBounds.set(	-(F32)sceneDesc->maxBoundXYZ,
						-(F32)sceneDesc->maxBoundXYZ,
						-(F32)sceneDesc->maxBoundXYZ,
						(F32)sceneDesc->maxBoundXYZ,
						(F32)sceneDesc->maxBoundXYZ,
						(F32)sceneDesc->maxBoundXYZ);
		
		// Set the static pruning structure.
		
		switch(sceneDesc->staticPruningType){
			xcase PSDK_SPT_DYNAMIC_AABB_TREE:
				nxSceneDesc.staticStructure = NX_PRUNING_DYNAMIC_AABB_TREE;
				nxSceneDesc.dynamicTreeRebuildRateHint = 5;
			xdefault:
				nxSceneDesc.staticStructure = NX_PRUNING_STATIC_AABB_TREE;
		}

		// Set the dynamic pruning structure.

		switch(sceneDesc->dynamicPruningType){
			xcase PSDK_SPT_DYNAMIC_AABB_TREE:
				nxSceneDesc.dynamicStructure = NX_PRUNING_DYNAMIC_AABB_TREE;
			xcase PSDK_SPT_QUADTREE:
				nxSceneDesc.dynamicStructure = NX_PRUNING_QUADTREE;
				nxSceneDesc.upAxis = 1;
				nxSceneDesc.maxBounds = &maxBounds;
				nxSceneDesc.subdivisionLevel = MINMAX(sceneDesc->subdivisionLevel, 1, 8);
			xcase PSDK_SPT_OCTREE:
				nxSceneDesc.dynamicStructure = NX_PRUNING_OCTREE;
				nxSceneDesc.maxBounds = &maxBounds;
				nxSceneDesc.subdivisionLevel = MINMAX(sceneDesc->subdivisionLevel, 1, 8);
		}
		
		// Set limits.
		
		NxSceneLimits limits;
		
		if(!psdkState.flags.disableLimits){
			nxSceneDesc.limits = &limits;

			limits.maxNbActors = psdkState.limits.maxActors;
			limits.maxNbBodies = psdkState.limits.maxBodies;
			limits.maxNbStaticShapes = psdkState.limits.maxStaticShapes;
			limits.maxNbDynamicShapes = psdkState.limits.maxDynamicShapes;
			limits.maxNbJoints = psdkState.limits.maxJoints;
		}
		
#if _PS3
		// enable broadphase collision detection on spu.
		// this turn out to be a lot slower but I'm leaving the code here in case we need to look at it again.
		//nxSceneDesc.bpType = NX_BP_TYPE_SPU_SAP_MULTI;
#endif
		// Done, so now create the NxScene.
		
		nxScene = nxSDK->createScene(nxSceneDesc);
		
		if(!nxScene){
			*sceneOut = NULL;
		}else{
			PSDKScene* scene;
			
			nxScene->setUserNotify(&psdkUserNotify);

			scene = callocStruct(PSDKScene);

			scene->nxScene = nxScene;

			// Tag with a global instance ID.

			scene->instanceID = ++psdkState.scene.lastInstanceID;
			
			// Store create params.
			
			scene->config.staticPruningType = sceneDesc->staticPruningType;
			scene->config.dynamicPruningType = sceneDesc->dynamicPruningType;

			scene->userPointer = userPointer;
			nxScene->userData = scene;

			NxMaterial* defaultMaterial = nxScene->getMaterialFromIndex(0);
			
			assert(defaultMaterial);
			defaultMaterial->setDynamicFriction(defaultDFriction);
			defaultMaterial->setStaticFriction(defaultSFriction);
			defaultMaterial->setRestitution(defaultRestitution);
	
			scene->geoInvalidatedFunc = geoInvalidatedFunc;

			*sceneOut = scene;

			nxScene->setGroupCollisionFlag(WC_SHAPEGROUP_DEBRIS, WC_SHAPEGROUP_DEBRIS, false);

			nxScene->setGroupCollisionFlag(WC_SHAPEGROUP_DEBRIS_VERY_LARGE,	WC_SHAPEGROUP_ENTITY, false);

			nxScene->setGroupCollisionFlag(WC_SHAPEGROUP_RAGDOLL_BODY, WC_SHAPEGROUP_ENTITY,			false);
			nxScene->setGroupCollisionFlag(WC_SHAPEGROUP_RAGDOLL_BODY, WC_SHAPEGROUP_RAGDOLL_LIMB,		false);
			nxScene->setGroupCollisionFlag(WC_SHAPEGROUP_RAGDOLL_BODY, WC_SHAPEGROUP_TEST_RAGDOLL_BODY,	false);
			nxScene->setGroupCollisionFlag(WC_SHAPEGROUP_RAGDOLL_BODY, WC_SHAPEGROUP_TEST_RAGDOLL_LIMB,	false);

			nxScene->setGroupCollisionFlag(WC_SHAPEGROUP_RAGDOLL_LIMB, WC_SHAPEGROUP_ENTITY,			false);
			nxScene->setGroupCollisionFlag(WC_SHAPEGROUP_RAGDOLL_LIMB, WC_SHAPEGROUP_TEST_RAGDOLL_BODY,	false);
			nxScene->setGroupCollisionFlag(WC_SHAPEGROUP_RAGDOLL_LIMB, WC_SHAPEGROUP_TEST_RAGDOLL_LIMB,	false);
			
			nxScene->setGroupCollisionFlag(WC_SHAPEGROUP_TEST_RAGDOLL_BODY, WC_SHAPEGROUP_ENTITY,			false);
			nxScene->setGroupCollisionFlag(WC_SHAPEGROUP_TEST_RAGDOLL_BODY, WC_SHAPEGROUP_TEST_RAGDOLL_LIMB,false);

			nxScene->setGroupCollisionFlag(WC_SHAPEGROUP_TEST_RAGDOLL_LIMB, WC_SHAPEGROUP_ENTITY,			false);

			nxScene->setUserContactReport(&psdkUserContactReport);

			//debris contact group pairs to report
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_DEBRIS_LARGE,			WC_SHAPEGROUP_DEBRIS_LARGE,			NX_NOTIFY_ON_TOUCH|NX_NOTIFY_ON_START_TOUCH|NX_NOTIFY_ON_END_TOUCH);
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_DEBRIS_LARGE,			WC_SHAPEGROUP_DEBRIS_VERY_LARGE,	NX_NOTIFY_ON_TOUCH|NX_NOTIFY_ON_START_TOUCH|NX_NOTIFY_ON_END_TOUCH);
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_DEBRIS_VERY_LARGE,	WC_SHAPEGROUP_DEBRIS_VERY_LARGE,	NX_NOTIFY_ON_TOUCH|NX_NOTIFY_ON_START_TOUCH|NX_NOTIFY_ON_END_TOUCH);

			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_DEBRIS,				WC_SHAPEGROUP_WORLD_BASIC,			NX_NOTIFY_ON_TOUCH|NX_NOTIFY_ON_START_TOUCH|NX_NOTIFY_ON_END_TOUCH);
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_DEBRIS_LARGE,			WC_SHAPEGROUP_WORLD_BASIC,			NX_NOTIFY_ON_TOUCH|NX_NOTIFY_ON_START_TOUCH|NX_NOTIFY_ON_END_TOUCH);
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_DEBRIS_VERY_LARGE,	WC_SHAPEGROUP_WORLD_BASIC,			NX_NOTIFY_ON_TOUCH|NX_NOTIFY_ON_START_TOUCH|NX_NOTIFY_ON_END_TOUCH);

			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_DEBRIS,				WC_SHAPEGROUP_TERRAIN,				NX_NOTIFY_ON_TOUCH|NX_NOTIFY_ON_START_TOUCH|NX_NOTIFY_ON_END_TOUCH);
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_DEBRIS_LARGE,			WC_SHAPEGROUP_TERRAIN,				NX_NOTIFY_ON_TOUCH|NX_NOTIFY_ON_START_TOUCH|NX_NOTIFY_ON_END_TOUCH);
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_DEBRIS_VERY_LARGE,	WC_SHAPEGROUP_TERRAIN,				NX_NOTIFY_ON_TOUCH|NX_NOTIFY_ON_START_TOUCH|NX_NOTIFY_ON_END_TOUCH);

			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_DEBRIS,				WC_SHAPEGROUP_HEIGHTMAP,			NX_NOTIFY_ON_TOUCH|NX_NOTIFY_ON_START_TOUCH|NX_NOTIFY_ON_END_TOUCH);
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_DEBRIS_LARGE,			WC_SHAPEGROUP_HEIGHTMAP,			NX_NOTIFY_ON_TOUCH|NX_NOTIFY_ON_START_TOUCH|NX_NOTIFY_ON_END_TOUCH);
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_DEBRIS_VERY_LARGE,	WC_SHAPEGROUP_HEIGHTMAP,			NX_NOTIFY_ON_TOUCH|NX_NOTIFY_ON_START_TOUCH|NX_NOTIFY_ON_END_TOUCH);

			//ragdoll contact group pairs to report
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_TEST_RAGDOLL_BODY,	WC_SHAPEGROUP_WORLD_BASIC,			NX_NOTIFY_ON_START_TOUCH);
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_TEST_RAGDOLL_BODY,	WC_SHAPEGROUP_TERRAIN,				NX_NOTIFY_ON_START_TOUCH);
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_TEST_RAGDOLL_BODY,	WC_SHAPEGROUP_HEIGHTMAP,			NX_NOTIFY_ON_START_TOUCH);
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_TEST_RAGDOLL_BODY,	WC_SHAPEGROUP_DEBRIS_LARGE,			NX_NOTIFY_ON_START_TOUCH);
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_TEST_RAGDOLL_BODY,	WC_SHAPEGROUP_DEBRIS_VERY_LARGE,	NX_NOTIFY_ON_START_TOUCH);

			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_TEST_RAGDOLL_LIMB,	WC_SHAPEGROUP_WORLD_BASIC,			NX_NOTIFY_ON_START_TOUCH);
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_TEST_RAGDOLL_LIMB,	WC_SHAPEGROUP_TERRAIN,				NX_NOTIFY_ON_START_TOUCH);
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_TEST_RAGDOLL_LIMB,	WC_SHAPEGROUP_HEIGHTMAP,			NX_NOTIFY_ON_START_TOUCH);
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_TEST_RAGDOLL_LIMB,	WC_SHAPEGROUP_DEBRIS_LARGE,			NX_NOTIFY_ON_START_TOUCH);
			nxScene->setActorGroupPairFlags(WC_SHAPEGROUP_TEST_RAGDOLL_LIMB,	WC_SHAPEGROUP_DEBRIS_VERY_LARGE,	NX_NOTIFY_ON_START_TOUCH);

			NxGroupsMask mask;

			// being used to store a pointer (gross)
			mask.bits0 = 0;

			// used for queries (filterbits) - what are you?
			mask.bits1 = 0xffffffff;

			// for colliding against 0 (which we aren't using for filtering)
			mask.bits2 = 0;

			// what do you want to collide against? (for colliding against 1)
			mask.bits3 = 0xffffffff;

			nxScene->setFilterOps(NX_FILTEROP_AND,NX_FILTEROP_AND,NX_FILTEROP_SWAP_AND);
			nxScene->setFilterConstant0(mask);
			nxScene->setFilterConstant1(mask);
			nxScene->setFilterBool(true);

			psdkState.scene.count++;
		}
	}
	PERFINFO_AUTO_STOP();

	return !!*sceneOut;
}

void psdkSceneDestroy(PSDKScene** sceneInOut){
	PSDKScene* scene = SAFE_DEREF(sceneInOut);

	if(	!nxSDK ||
		!scene)
	{
		return;
	}

	PERFINFO_AUTO_START("psdkSceneDestroy", 1);
	
		assert(!scene->actor.count);

		psdkQueuedReleaseAdd(PSDK_QR_SCENE, (void**)&scene->nxScene);
		
		SAFE_FREE(*sceneInOut);
		
	PERFINFO_AUTO_STOP();
}

S32 psdkSceneGetUserPointer(PSDKScene* scene,
							void** userPointerOut)
{
	if(	!scene ||
		!userPointerOut)
	{
		return 0;
	}

	*userPointerOut = scene->userPointer;

	return !!*userPointerOut;
}

S32 psdkSceneGetInstanceID(	PSDKScene* scene,
							U32* instanceIDOut)
{
	if(	!scene ||
		!instanceIDOut)
	{
		return 0;
	}
	
	*instanceIDOut = scene->instanceID;
	
	return 1;
}

S32	psdkSceneGetActorCounts(PSDKScene* scene,
							U32* actorCountOut,
							U32* nxActorCountOut,
							U32* actorCreateCountOut,
							U32* actorCreateFailedCountOut,
							U32* actorDestroyCountOut)
{
	if(!SAFE_MEMBER(scene, nxScene)){
		return 0;
	}
	
	#define SET(x, y) if(x){*x=y;}
		SET(actorCountOut,				scene->actor.count);
		SET(nxActorCountOut,			scene->nxScene->getNbActors());
		SET(actorCreateCountOut,		scene->actor.createCount);
		SET(actorCreateFailedCountOut,	scene->actor.createFailedCount);
		SET(actorDestroyCountOut,		scene->actor.destroyCount);
	#undef SET
	
	return 1;
}

S32 psdkSceneGetConfig(	PSDKScene* scene,
						PSDKScenePruningType* staticPruningTypeOut,
						PSDKScenePruningType* dynamicPruningTypeOut)
{
	if(!scene){
		return 0;
	}
	
	#define SET(x, y) if(x){*x=y;}
		SET(staticPruningTypeOut,	scene->config.staticPruningType);
		SET(dynamicPruningTypeOut,	scene->config.dynamicPruningType);
	#undef SET
	
	return 1;
}

S32 psdkSceneSimulate(	PSDKScene* scene,
						F32 timeStep)
{
	if(	!scene ||
		scene->flags.simulating ||
		!scene->nxScene ||
		!psdkState.simulationLockCountBG)
	{
		return 0;
	}

	scene->nxScene->simulate(timeStep);
	scene->flags.simulating = 1;
	
	assert(!scene->simulate.startedThreadID);
	scene->simulate.startedThreadID = GetCurrentThreadId();
	
	psdkState.simulatingScenesCountBG++;

	return 1;
}

S32 psdkSceneFetchResults(	PSDKScene* scene,
							S32 doBlock)
{
	if(	!scene ||
		!scene->flags.simulating ||
		!scene->nxScene)
	{
		return 0;
	}
	
	// Check the thread ID.
	
	assert(GetCurrentThreadId() == scene->simulate.startedThreadID);
	scene->simulate.startedThreadID = 0;
	
	if(scene->nxScene->fetchResults(NX_ALL_FINISHED, !!doBlock)){
		scene->flags.simulating = 0;
		
		assert(psdkState.simulatingScenesCountBG);
		
		psdkState.simulatingScenesCountBG--;
		
		assert(psdkState.simulationLockCountBG);
		assert(psdkState.simulationLockCountFG);
		
		return 1;
	}
	
	assert(!doBlock);

	return 0;
}

static void vec3FromNxVec3(Vec3 out, const NxVec3& in){
	setVec3(out, in.x, in.y, in.z);
}

S32 psdkScenePrintActors(PSDKScene* scene){
	S32			size;
	NxActor**	actors = NULL;

	if(!SAFE_MEMBER(scene, nxScene)){
		return 0;
	}

	size = scene->nxScene->getNbActors();
	actors = scene->nxScene->getActors();

	FOR_BEGIN(i, size);
	{
		NxActor*	actor = actors[i];
		Vec3		pos;
		
		vec3FromNxVec3(pos, actor->getGlobalPosition());

		printf("Actor %p at (%f %f %f)\n", actor->userData, vecParamsXYZ(pos));
	}
	FOR_END;

	return 1;
}


struct FilteredRaycastReport : public NxUserRaycastReport {
	U32				shapeTag;
	F32				bestDistance;
	NxRaycastHit	bestHit;
	U32				hitSomething : 1;

    virtual bool onHit(const NxRaycastHit& hit){
		if(	hit.distance < bestDistance &&
			hit.shape->getGroupsMask().bits0 == shapeTag)
		{
			bestHit = hit;
			hitSomething = 1;
		}

        return true;
    }

	void init(U32 tag){
		shapeTag = tag;
		bestDistance = FLT_MAX;
		hitSomething = 0;
	}
};

static void psdkFillRaycastResults(	PSDKRaycastResults* resultsOut,
									const NxShape* nxShape,
									const Vec3 sceneOffset,
									const NxVec3& posWorldImpact,
									const NxVec3* posWorldEnd,
									F32 distance,
									const NxVec3 vecWorldNormal,
									U32 triIndex,
									F32 triU,
									F32 triV)
{
	if(!resultsOut){
		return;
	}

	ZeroStruct(resultsOut);

	if(nxShape){
		resultsOut->actor = (PSDKActor*)nxShape->getActor().userData;
		assert(resultsOut->actor);
	}

	copyVec3(	posWorldImpact,
				resultsOut->posWorldImpact);

	if(sceneOffset){
		subVec3(resultsOut->posWorldImpact,
				sceneOffset,
				resultsOut->posWorldImpact);
	}

	if(posWorldEnd){
		copyVec3(	*posWorldEnd,
					resultsOut->posWorldEnd);

		if(sceneOffset){
			subVec3(resultsOut->posWorldEnd,
					sceneOffset,
					resultsOut->posWorldEnd);
		}
	}else{
		copyVec3(	resultsOut->posWorldImpact,
					resultsOut->posWorldEnd);
	}

	// andrewa - there is no way to tell PhysX to flip normals for heightfields, so we do it ourselves.
	// TODO: do we have to invert or swap the barycentric coordinates (triU and triV) as well?
	if(nxShape && nxShape->getType() == NX_SHAPE_HEIGHTFIELD) {
		scaleVec3(	vecWorldNormal, -1.0f,
					resultsOut->normalWorld);
	} else {
		copyVec3(	vecWorldNormal,
					resultsOut->normalWorld);
	}

	resultsOut->distance = distance;

	resultsOut->tri.index = triIndex;
	resultsOut->tri.u = triU;
	resultsOut->tri.v = triV;
}

S32 psdkRaycastClosestShape(const PSDKScene* scene,
							const Vec3 sceneOffset,
							const Vec3 source,
							const Vec3 target,
							U32 filterBits,
							U32 shapeTag,
							PSDKRaycastResults* resultsOut)
{
	NxRay			nxRay;
	NxShape*		nxShape = NULL;
	NxRaycastHit	nxHit;
	Vec3			rayDir;
	F32				len;
	Vec3			sourceOffset;
	Vec3			targetOffset;

	if(!SAFE_MEMBER(scene, nxScene)){
		return 0;
	}

	if(sceneOffset){
		addVec3(source, sceneOffset, sourceOffset);
		addVec3(target, sceneOffset, targetOffset);
		source = sourceOffset;
		target = targetOffset;
	}

	copyVec3(source, nxRay.orig);
	subVec3(target, source, rayDir);
	
	len = normalVec3(rayDir);
	
	if(len <= 0.f){
		return 0;
	}
	
	copyVec3(rayDir, nxRay.dir);

	NxGroupsMask mask;
	mask.bits0 = 0;
	mask.bits1 = 0;
	mask.bits2 = 0;
	mask.bits3 = filterBits; // checks against bits1

	PERFINFO_AUTO_START("raycastShapes", 1);

	if(shapeTag){
		FilteredRaycastReport rayReport;

		rayReport.init(shapeTag);

		scene->nxScene->raycastAllShapes(	nxRay,
											rayReport,
											NX_ALL_SHAPES,
											0xffffffff,
											len,
											0xffffffff,
											&mask);

		if(rayReport.hitSomething){
			nxHit = rayReport.bestHit;
			nxShape = nxHit.shape;
		}
	}else{
		nxShape = scene->nxScene->raycastClosestShape(	nxRay,
														NX_ALL_SHAPES,
														nxHit,
														0xffffffff,
														len,
														0xffffffff,
														&mask);
	}

	PERFINFO_AUTO_STOP();

	if(!nxShape){
		return 0;
	}
	
	Vec3 worldImpact;

	copyVec3(nxHit.worldImpact, worldImpact);
	
	if(distance3Squared(worldImpact, source) > SQR(len)){
		return 0;
	}

	psdkFillRaycastResults(	resultsOut,
							nxShape,
							sceneOffset,
							nxHit.worldImpact,
							NULL,
							nxHit.distance,
							nxHit.worldNormal,
							nxHit.faceID,
							nxHit.u,
							nxHit.v);
	
	return 1;
}

struct MultipleResultRaycastReport : public NxUserRaycastReport {
	PSDKRaycastResultsCB	callback;
	void*					userPointer;
	U32						tag;
	const F32*				sceneOffset;

    virtual bool onHit(const NxRaycastHit& nxHit){
		if(	tag == 0 ||
			nxHit.shape->getGroupsMask().bits0 == tag)
		{
			PSDKRaycastResults results;

			psdkFillRaycastResults(	&results,
									nxHit.shape,
									sceneOffset,
									nxHit.worldImpact,
									NULL,
									nxHit.distance,
									nxHit.worldNormal,
									nxHit.faceID,
									nxHit.u,
									nxHit.v);

			if(!callback(userPointer, &results)){
				return 0;
			}
		}

		return 1;
    }
};

S32 psdkRaycastShapeMultiResult(const PSDKScene* scene,
								const Vec3 sceneOffset,
								const Vec3 source,
								const Vec3 target,
								U32 filterBits,
								U32 shapeTag,
								PSDKRaycastResultsCB callback,
								void* userPointer)
{
	NxRay			nxRay;
	Vec3			rayDir;
	F32				len;
	int				resultCount = 0;
	Vec3			sourceOffset;
	Vec3			targetOffset;
	F32				lenSQR;

	if(!SAFE_MEMBER(scene, nxScene)){
		return 0;
	}

	if(sceneOffset){
		addVec3(source, sceneOffset, sourceOffset);
		addVec3(target, sceneOffset, targetOffset);
		source = sourceOffset;
		target = targetOffset;
	}

	copyVec3(source, nxRay.orig);
	subVec3(target, source, rayDir);
	
	len = normalVec3(rayDir);
	
	if(len <= 0.f){
		return 0;
	}

	lenSQR = SQR(len);
	
	copyVec3(rayDir, nxRay.dir);

	PERFINFO_AUTO_START("raycastAllShapes", 1);

	MultipleResultRaycastReport rayReport;

	rayReport.callback = callback;
	rayReport.userPointer = userPointer;
	rayReport.sceneOffset = sceneOffset;
	rayReport.tag = shapeTag;

	NxGroupsMask mask;
	mask.bits0 = 0;
	mask.bits1 = 0;
	mask.bits2 = 0;
	mask.bits3 = filterBits; // checks against bits1

	scene->nxScene->raycastAllShapes(	nxRay,
										rayReport,
										NX_ALL_SHAPES,
										0xffffffff,
										len,
										0xffffffff,
										&mask);
	PERFINFO_AUTO_STOP();

	return 1;
}

S32 psdkCapsulecastClosestShape(const PSDKScene* scene,
								const Vec3 sceneOffset,
								const Capsule capsule,
								const Vec3 source,
								const Vec3 target,
								U32 filterBits,
								PSDKRaycastResults* resultsOut)
{
	Vec3			capDir;
	F32				len;
	NxCapsule		testCapsule;
	NxSweepQueryHit nxSweepHit;
	U32				flags;
	Vec3			sourceOffset;
	Vec3			targetOffset;

	if(	!scene ||
		!scene->nxScene)
	{
		return 0;
	}

	assert(FINITEVEC3(source));
	assert(FINITEVEC3(target));

	if(sceneOffset){
		addVec3(source, sceneOffset, sourceOffset);
		addVec3(target, sceneOffset, targetOffset);
		source = sourceOffset;
		target = targetOffset;
	}

	subVec3(target, source, capDir);
	len = lengthVec3(capDir);

	flags = NX_SF_STATICS|NX_SF_DYNAMICS;

	addVec3(source, capsule.vStart, testCapsule.p0);
	scaleAddVec3(capsule.vDir, capsule.fLength, testCapsule.p0, testCapsule.p1);

	testCapsule.radius = capsule.fRadius;

	ZeroStruct(&nxSweepHit);

	NxGroupsMask mask;
	mask.bits0 = 0;
	mask.bits1 = 0;
	mask.bits2 = 0;
	mask.bits3 = filterBits; // checks against bits1

	PERFINFO_AUTO_START("capcastClosestShape", 1);
	scene->nxScene->linearCapsuleSweep(	testCapsule,
										capDir,
										flags,
										NULL, 
										1,
										&nxSweepHit,
										NULL,
										0xffffffff,
										&mask);
	PERFINFO_AUTO_STOP();

	if(!nxSweepHit.hitShape){
		return 0;
	}

	if(resultsOut){
		NxVec3 endPos;

		scaleAddVec3(	capDir,
						nxSweepHit.t,
						source,
						endPos);

		psdkFillRaycastResults(	resultsOut,
								nxSweepHit.hitShape,
								sceneOffset,
								nxSweepHit.point,
								&endPos,
								nxSweepHit.t,
								nxSweepHit.normal,
								nxSweepHit.faceID,
								0.0,
								0.0);
	}

	return 1;
}

static void psdkNxCapsuleFromCapsule(	NxCapsule &nxCap,
										const Vec3 pos,
										const Capsule* cap)
{
	addVec3(pos, cap->vStart, nxCap.p0);
	scaleAddVec3(cap->vDir, cap->fLength, nxCap.p0, nxCap.p1);
	nxCap.radius = cap->fRadius;
}

S32	psdkCapsuleCheck(const PSDKScene* scene,
					 const Vec3 sceneOffset,
					 const Capsule* capsule,
					 const Vec3 pos,
					 U32 filterBits)
{
	NxCapsule		testCapsule;
	NxShapesType	types;
	S32				result;
	Vec3			posOffset;

	if(	!scene ||
		!scene->nxScene ||
		!pos ||
		!capsule)
	{
		return 0;
	}

	if(sceneOffset){
		addVec3(pos, sceneOffset, posOffset);
		pos = posOffset;
	}

	types = (NxShapesType)(NX_STATIC_SHAPES | NX_DYNAMIC_SHAPES);

	psdkNxCapsuleFromCapsule(testCapsule, pos, capsule);

	NxGroupsMask mask;
	mask.bits0 = 0;
	mask.bits1 = 0;
	mask.bits2 = 0;
	mask.bits3 = filterBits; // checks against bits1

	PERFINFO_AUTO_START("checkOverlapCapsule", 1);
		result = scene->nxScene->checkOverlapCapsule(	testCapsule,
														types,
														0xffffffff,
														&mask);
	PERFINFO_AUTO_STOP();

	return result;
}

S32 psdkOBBCastClosestShape(const PSDKScene* scene,
							const Vec3 sceneOffset,
							const Vec3 minLocalOBB,
							const Vec3 maxLocalOBB,
							const Mat4 matWorldOBB,
							const Vec3 target,
							U32 filterBits,
							PSDKRaycastResults* resultsOut)
{
	F32				len;
	NxSweepQueryHit nxSweepHit;
	Vec3			dir;
	Vec3			midLocalOBB;
	NxBox			box;
	Vec3			midWorldOBB;
	Vec3			targetOffset;
	
	if(	!scene ||
		!scene->nxScene)
	{
		return 0;
	}

	// Get local mid of box, transform to world-space.
	
	addVec3(maxLocalOBB, minLocalOBB, midLocalOBB);
	scaleVec3(midLocalOBB, 0.5f, midLocalOBB);
	mulVecMat4(midLocalOBB, matWorldOBB, midWorldOBB);

	if(sceneOffset){
		addVec3(midWorldOBB, sceneOffset, midWorldOBB);
		addVec3(target, sceneOffset, targetOffset);
		addVec3(sceneOffset, matWorldOBB[3], box.center);
		target = targetOffset;
	}else{
		box.center = matWorldOBB[3];
	}

	subVec3(target, midWorldOBB, dir);
	len = lengthVec3(dir);

	box.rot.setColumnMajor(matWorldOBB[0]);
	subVec3(maxLocalOBB, minLocalOBB, box.extents);

	ZeroStruct(&nxSweepHit);

	NxGroupsMask mask;
	mask.bits0 = 0;
	mask.bits1 = 0;
	mask.bits2 = 0;
	mask.bits3 = filterBits; // checks against bits1

	PERFINFO_AUTO_START("psdkOBBCastClosestShape", 1);
	scene->nxScene->linearOBBSweep(	box,
									dir,
									NX_SF_STATICS|NX_SF_DYNAMICS,
									NULL, 
									1,
									&nxSweepHit,
									NULL,
									0xffffffff,
									&mask);
	PERFINFO_AUTO_STOP();

	NxShape*	hitShape = nxSweepHit.hitShape;
	NxVec3		impactPoint = nxSweepHit.point;

	if(!hitShape){
		return 0;
	}
	
	Vec3 worldImpact;

	copyVec3(nxSweepHit.point, worldImpact);

	if(distance3(worldImpact, midWorldOBB) > len){
		return 0;
	}

	if(resultsOut){
		NxVec3 endPos;

		scaleAddVec3(	dir,
						nxSweepHit.t,
						midWorldOBB,
						endPos);

		psdkFillRaycastResults(	resultsOut,
								nxSweepHit.hitShape,
								sceneOffset,
								nxSweepHit.point,
								&endPos,
								nxSweepHit.t,
								nxSweepHit.normal,
								nxSweepHit.faceID,
								0.0,
								0.0);
	}

	return 1;
}

/*
 * Internal helper function so that there's only one place in code where we swap the winding order for Heightfield triangles. Stop the copy pasta.
 */
static void FillHeightfieldTriangles(NxHeightFieldShape *nxHeightFieldShape, const U32* tris, U32 triCount, Vec3 (*triVertsOut)[3], bool doWorldTransform,
									bool hasOffset, const Vec3 sceneOffset)
{
	NxTriangle tri;

	FOR_BEGIN(i, (S32)triCount);
	{
		nxHeightFieldShape->getTriangle(tri, NULL, NULL, tris[i], doWorldTransform, doWorldTransform);

		FOR_BEGIN(j, 3);
		{
			// andrewa - notice the change in winding order. PhysX gives no way to tell it the heightfield shape should flip normals.
			copyVec3(tri.verts[j], triVertsOut[i][2 - j]);
			if(hasOffset)
				subVec3(triVertsOut[i][2 - j], sceneOffset, triVertsOut[i][2 - j]);
		}
		FOR_END;
	}
	FOR_END;
}

struct PSDKQueryTrianglesTriangleOverlapReport : public NxUserEntityReport<NxU32> {
	NxShape*				shape;
	PSDKQueryTrianglesCB	callback;
	void*					userPointer;
	Vec3					sceneOffset;
	U32						hasOffset : 1;

	PSDKQueryTrianglesTriangleOverlapReport(void) :
		shape(NULL),
		callback(NULL),
		userPointer(NULL),
		hasOffset(0)
	{
	}
	
	virtual bool onEvent(	NxU32 triCount,
							NxU32* tris)
	{
		unsigned int triangleArrayMax = 100;
		F32 *triangles = (F32*)calloc(3 * 3 * triangleArrayMax, sizeof(F32));

		Mat4					matWorld;
		psdkShapeGetMat((PSDKShape*)shape, matWorld);

		switch(shape->getType()){
			xcase NX_SHAPE_MESH:{
				NxTriangleMeshShape*	tms = (NxTriangleMeshShape*)shape;
				NxTriangleMesh&			tm = tms->getTriangleMesh();

				unsigned int currentSubmesh = 0;
				for(currentSubmesh = 0; currentSubmesh < tm.getSubmeshCount(); currentSubmesh++) {

					const U32*const triVertIndexes = (const U32*)tm.getBase(
						currentSubmesh, NX_ARRAY_TRIANGLES);
					const NxVec3*const verts = (const NxVec3*)tm.getBase(
						currentSubmesh, NX_ARRAY_VERTICES);

					NxU32 strideVerts = tm.getStride(currentSubmesh, NX_ARRAY_VERTICES);
					NxU32 strideTris  = tm.getStride(currentSubmesh, NX_ARRAY_TRIANGLES);
					U32 remain        = tm.getCount(currentSubmesh, NX_ARRAY_TRIANGLES);

					assert(strideTris == 3 * sizeof(U32));
					assert(strideVerts == 3 * sizeof(F32));

					U32 base = 0;

					while(remain){

						S32 count = MIN(remain, triangleArrayMax);

						memset(triangles, 0, 3 * 3 * triangleArrayMax * sizeof(F32));

						FOR_BEGIN(i, count);
						{
							U32		triIndex = 3 * (base + i);
							Vec3	vert;

							FOR_BEGIN(j, 3);
							{
								U32 id0 = triIndex + j;
								U32 realIndex = triVertIndexes[id0];

								assert(realIndex < tm.getCount(currentSubmesh, NX_ARRAY_VERTICES));

								copyVec3(verts[realIndex], vert);
								F32 *triPtr = triangles + (i * 3 * 3 + j * 3);
								mulVecMat4(vert, matWorld, triPtr);
								if(hasOffset) {
									subVec3(triPtr, sceneOffset, triPtr);
								}

							}
							FOR_END;
						}
						FOR_END;

						callback(userPointer, (Vec3(*)[3])triangles, count);

						remain -= count;
						base += count;
					}
				}
			}

			xcase NX_SHAPE_HEIGHTFIELD:{
				NxHeightFieldShape*		heightFieldShape = (NxHeightFieldShape*)shape;
				U32						remain = triCount;
				U32						base = 0;
				NxTriangle				triangle;

				while(remain) {
					U32 count = MIN(remain, triangleArrayMax);

					FillHeightfieldTriangles(
						heightFieldShape, &tris[base],
						count, (Vec3(*)[3])triangles,
						/*doWorldTransform=*/true,
						/*hasOffset=*/!!hasOffset,
						sceneOffset);

					callback(userPointer, (Vec3(*)[3])triangles, count);

					remain -= count;
					base += count;
				}
			}
		}


		free(triangles);

		return true;
	}
};

struct PSDKQueryTrianglesShapeOverlapReport : public NxUserEntityReport<NxShape*> {
	NxBounds3								bounds;
	PSDKQueryTrianglesTriangleOverlapReport	triReport;

	void reportConvexShape(void){
		NxConvexShape*		cs = (NxConvexShape*)triReport.shape;
		NxConvexMesh&		cm = cs->getConvexMesh();
		const U32			triCount = cm.getCount(0, NX_ARRAY_TRIANGLES);
		const U32*const		triVertIndexes = (const U32*)cm.getBase(0, NX_ARRAY_TRIANGLES);
		const NxVec3*const	verts = (const NxVec3*)cm.getBase(0, NX_ARRAY_VERTICES);
		U32					remain = triCount;
		U32					triIndex = 0;
		Vec3				triBuffer[100][3];
		Mat4				matWorld;
		
		psdkShapeGetMat((PSDKShape*)(NxShape*)cs, matWorld);
		
		while(remain){
			S32 curTriCount = MIN(remain, ARRAY_SIZE(triBuffer));
			
			if(triReport.hasOffset){
				FOR_BEGIN(i, curTriCount);
				{
					FOR_BEGIN(j, 3);
					{
						Vec3 vertLocal;
					
						copyVec3(	verts[triVertIndexes[triIndex + j]],
									vertLocal);

						mulVecMat4(	vertLocal,
									matWorld,
									triBuffer[i][j]);

						subVec3(triBuffer[i][j],
								triReport.sceneOffset,
								triBuffer[i][j]);
					}
					FOR_END;
				
					triIndex += 3;
				}
				FOR_END;
			}else{
				FOR_BEGIN(i, curTriCount);
				{
					FOR_BEGIN(j, 3);
					{
						Vec3 vertLocal;
					
						copyVec3(	verts[triVertIndexes[triIndex + j]],
									vertLocal);

						mulVecMat4(	vertLocal,
									matWorld,
									triBuffer[i][j]);
					}
					FOR_END;
				
					triIndex += 3;
				}
				FOR_END;
			}
						
			remain -= curTriCount;
			
			triReport.callback(	triReport.userPointer,
								triBuffer,
								curTriCount);
		}
	}
	
	virtual bool onEvent(	NxU32 shapeCount,
							NxShape** shapes)
	{
		U32 i;
		
		for(i = 0; i < shapeCount; i++){
			triReport.shape = shapes[i];

			switch(triReport.shape->getType()){
				xcase NX_SHAPE_MESH:{
					NxTriangleMeshShape* triMeshShape = (NxTriangleMeshShape*)triReport.shape;
					
					triMeshShape->overlapAABBTriangles(	bounds,
														NX_QUERY_WORLD_SPACE,
														&triReport);
				}
				
				xcase NX_SHAPE_CONVEX:{
					reportConvexShape();
				}
				
				xcase NX_SHAPE_HEIGHTFIELD:{
					NxHeightFieldShape*	heightFieldShape = (NxHeightFieldShape*)triReport.shape;
					
					heightFieldShape->overlapAABBTriangles(	bounds,
															NX_QUERY_WORLD_SPACE,
															&triReport);
				}
			}
		}
		
		return true;
	}
};

S32 psdkSceneQueryTrianglesInYAxisCylinder(	PSDKScene* scene,
											const Vec3 sceneOffset,
											U32 filterBits,
											const Vec3 source,
											const Vec3 target,
											F32 radius,
											PSDKQueryTrianglesCB callback,
											void* userPointer)
{
	if(	!scene ||
		!source ||
		!target ||
		!callback)
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();

	NxCapsule								nxCapsule;
	Vec3									sourceOffset;
	Vec3									targetOffset;
	PSDKQueryTrianglesShapeOverlapReport	report;

	if(sceneOffset){
		addVec3(source, sceneOffset, sourceOffset);
		addVec3(target, sceneOffset, targetOffset);
		source = sourceOffset;
		target = targetOffset;
		report.triReport.hasOffset = 1;
		copyVec3(sceneOffset, report.triReport.sceneOffset);
	}
	
	copyVec3(source, nxCapsule.p0);
	copyVec3(target, nxCapsule.p1);
	nxCapsule.radius = radius;

	// Figure out the capsule bounding box.
	{
		Vec3 dir;
		float reff;
		float dv;
		int i;
		subVec3(target, source, dir);
		normalVec3(dir);

		for(i = 0; i < 3; i++) {
			Vec3 axis = {0};
			axis[i] = 1;
			dv = dotVec3(dir, axis);
			reff = radius * sqrt(1.0 - dv*dv);
			report.bounds.max[i] = MAXF(source[i], target[i]) + reff;
			report.bounds.min[i] = MINF(source[i], target[i]) - reff;
		}
	}

	report.triReport.callback = callback;
	report.triReport.userPointer = userPointer;

	NxGroupsMask mask;
	mask.bits0 = 0;
	mask.bits1 = 0;
	mask.bits2 = 0;
	mask.bits3 = filterBits; // checks against bits1

	scene->nxScene->overlapCapsuleShapes(	nxCapsule,
											NX_ALL_SHAPES,
											0,
											NULL,
											&report,
											0xffffffff,
											&mask,
											false);

	PERFINFO_AUTO_STOP();

	return 1;
}

S32 psdkSceneQueryTrianglesInCapsule(	const PSDKScene* scene,
										const Vec3 sceneOffset,
										U32 filterBits,
										const Vec3 source,
										const Vec3 target,
										F32 radius,
										PSDKQueryTrianglesCB callback,
										void* userPointer)
{
	if(	!scene ||
		!source ||
		!target ||
		!callback)
	{
		return 0;
	}

	NxCapsule								nxCapsule;
	Vec3									sourceOffset;
	Vec3									targetOffset;
	PSDKQueryTrianglesShapeOverlapReport	report;

	if(sceneOffset){
		addVec3(source, sceneOffset, sourceOffset);
		addVec3(target, sceneOffset, targetOffset);
		source = sourceOffset;
		target = targetOffset;
		report.triReport.hasOffset = 1;
		copyVec3(sceneOffset, report.triReport.sceneOffset);
	}

	copyVec3(source, nxCapsule.p0);
	copyVec3(target, nxCapsule.p1);
	nxCapsule.radius = radius;
	
	report.bounds.min = NxVec3(	MINF(source[0], target[0]) - radius,
								MINF(source[1], target[1]) - radius,
								MINF(source[2], target[2]) - radius);
	
	report.bounds.max = NxVec3(	MAXF(source[0], target[0]) + radius,
								MAXF(source[1], target[1]) + radius,
								MAXF(source[2], target[2]) + radius);
								
	report.triReport.callback = callback;
	report.triReport.userPointer = userPointer;

	NxGroupsMask mask;
	mask.bits0 = 0;
	mask.bits1 = 0;
	mask.bits2 = 0;
	mask.bits3 = filterBits; // checks against bits1

	scene->nxScene->overlapCapsuleShapes(	nxCapsule,
											NX_ALL_SHAPES,
											0,
											NULL,
											&report,
											0xffffffff,
											&mask,
											false);
											
	return 1;
}

struct PSDKCheckTrianglesShapeOverlapReport : public NxUserEntityReport<NxShape*>
{
	bool bWasTriangles;
	
	virtual bool onEvent(	NxU32 shapeCount,
							NxShape** shapes)
	{
		bWasTriangles = true;
		return false;
	}
};

S32 psdkCapsuleCheckWS(	const PSDKScene* scene,
							const Vec3 sceneOffset,
							const Vec3 source,
							const Vec3 target,
							F32 radius,
							U32 filterBits)
{
	if(	!scene ||
		!source ||
		!target)
	{
		return 0;
	}

	NxCapsule								nxCapsule;
	PSDKCheckTrianglesShapeOverlapReport	report;

	copyVec3(source, nxCapsule.p0);
	copyVec3(target, nxCapsule.p1);
	if(sceneOffset){
		addVec3(nxCapsule.p0, sceneOffset, nxCapsule.p0);
		addVec3(nxCapsule.p1, sceneOffset, nxCapsule.p1);
//		report.triReport.hasOffset = 1;
//		copyVec3(sceneOffset, report.triReport.sceneOffset);
	}

	nxCapsule.radius = radius;
	
	report.bWasTriangles = false;

	NxGroupsMask mask;
	mask.bits0 = 0;
	mask.bits1 = 0;
	mask.bits2 = 0;
	mask.bits3 = filterBits; // checks against bits1

	PERFINFO_AUTO_START("overlapCapsuleShapes", 1);
	scene->nxScene->overlapCapsuleShapes(	nxCapsule,
											NX_ALL_SHAPES,
											0,
											NULL,
											&report,
											0xffffffff,
											&mask,
											false);
	PERFINFO_AUTO_STOP();
											
	return report.bWasTriangles;
}

S32 psdkShapeRaycast(	const PSDKShape* shape,
						const Vec3 sceneOffset,
						const Vec3 source,
						const Vec3 target,
						PSDKRaycastResults* resultsOut)
{
	if(	!shape ||
		!source ||
		!target)
	{
		return 0;
	}
	
	NxRay	nxRay;
	Vec3	rayDir;
	F32		len;
	Vec3	sourceOffset;
	Vec3	targetOffset;

	if(sceneOffset){
		addVec3(source, sceneOffset, sourceOffset);
		addVec3(target, sceneOffset, targetOffset);
		source = sourceOffset;
		target = targetOffset;
	}
	
	copyVec3(source, nxRay.orig);
	subVec3(target, source, rayDir);
	
	len = normalVec3(rayDir);
	
	if(len <= 0.f){
		return 0;
	}
	
	const NxShape*	nxShape = (const NxShape*)shape;
	NxRaycastHit	nxHit;
	
	copyVec3(rayDir, nxRay.dir);
	
	if(!nxShape->raycast(	nxRay,
							len,
							~NX_RAYCAST_SHAPE,
							nxHit,
							false))
	{
		return 0;
	}
	
	psdkFillRaycastResults(	resultsOut,
							nxShape,
							sceneOffset,
							nxHit.worldImpact,
							NULL,
							nxHit.distance,
							nxHit.worldNormal,
							nxHit.faceID,
							nxHit.u,
							nxHit.v);
	
	return 1;
}

static S32 psdkShapeRaycastIsBackfaceClosest(	const PSDKShape* shape,
												const Vec3 sceneOffset,
												const Vec3 source,
												const Vec3 target)
{
	PSDKRaycastResults results;

	if(	!shape ||
		!source ||
		!target)
	{
		return 0;
	}

	if(!psdkShapeRaycast(shape, sceneOffset, source, target, &results)){
		// Didn't hit anything on the way out, see if there's anything the other way.
		
		if(!psdkShapeRaycast(shape, sceneOffset, target, source, &results)){
			// Nothing this way.
		}else{
			// Done, pos is inside.
			
			return 1;
		}
	}else{
		// Hit something, so cast backwards.
		
		if(!psdkShapeRaycast(shape, sceneOffset, results.posWorldImpact, source, NULL)){
			// Closest tri is facing pos this way.
		}else{
			// Hit something on the way back, so pos is inside.
			
			return 1;
		}
	}
	
	return 0;
}

S32 psdkShapeIsPointInside(	const PSDKShape* shape,
							const Vec3 sceneOffset,
							const Vec3 pos)
{
	Vec3 boundsMin;
	Vec3 boundsMax;

	if(	!shape ||
		!pos)
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();
	
	if(!psdkShapeGetBounds(shape, boundsMin, boundsMax)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	FOR_BEGIN(i, 3);
	{
		if(	pos[i] < boundsMin[i] ||
			pos[i] > boundsMax[i])
		{
			PERFINFO_AUTO_STOP();
			return 0;
		}
	}
	FOR_END;
	
	FOR_BEGIN(i, 3);
	{
		Vec3 posSide;
		
		copyVec3(pos, posSide);

		posSide[i] = boundsMin[i];
		
		if(psdkShapeRaycastIsBackfaceClosest(shape, sceneOffset, pos, posSide)){
			PERFINFO_AUTO_STOP();
			return 1;
		}

		posSide[i] = boundsMax[i];
		
		if(psdkShapeRaycastIsBackfaceClosest(shape, sceneOffset, pos, posSide)){
			PERFINFO_AUTO_STOP();
			return 1;
		}
	}
	FOR_END;
	
	PERFINFO_AUTO_STOP();
	
	return 0;
}

S32 psdkActorIsPointInside(	PSDKActor* actor,
							const Vec3 sceneOffset,
							const Vec3 pos)
{
	U32		shapeCount;
	Vec3	minDist[2];
	Vec3	boundsMin;
	Vec3	boundsMax;
	
	PERFINFO_AUTO_START_FUNC();
	
	shapeCount = psdkActorGetShapeCount(actor);
	setVec3same(minDist[0], FLT_MAX);
	setVec3same(minDist[1], FLT_MAX);

	// First get the front facing distance for each direction and shape.
	
	FOR_BEGIN(i, (S32)shapeCount);
	{
		const PSDKShape*	shape;
		U32					inRangeCount = 0;
		U32					outOfRangeIndex = 3;
		PSDKRaycastResults	results;

		if(	!psdkActorGetShapeByIndex(actor, i, &shape) ||
			!psdkShapeGetBounds(shape, boundsMin, boundsMax))
		{
			continue;
		}
		
		FOR_BEGIN(j, 3);
		{
			if(	pos[j] > boundsMin[j] &&
				pos[j] < boundsMax[j])
			{
				inRangeCount++;
			}else{
				outOfRangeIndex = j;
			}
		}
		FOR_END;
		
		if(inRangeCount < 2){
			// No axis-rays can hit this shape, just skip it.
			continue;
		}
		
		FOR_BEGIN(j, 3);
		{
			Vec3 posSide;
			
			if(	inRangeCount != 3 &&
				j != outOfRangeIndex)
			{
				continue;
			}
			
			if(pos[j] > boundsMin[j]){
				copyVec3(pos, posSide);
				posSide[j] = boundsMin[j];
				
				if(psdkShapeRaycast(shape, sceneOffset, pos, posSide, &results)){
					MIN1(minDist[0][j], results.distance);
				}
			}
			
			if(pos[j] < boundsMax[j]){
				copyVec3(pos, posSide);
				posSide[j] = boundsMax[j];
				
				if(psdkShapeRaycast(shape, sceneOffset, pos, posSide, &results)){
					MIN1(minDist[1][j], results.distance);
				}
			}
		}
		FOR_END;
	}
	FOR_END;
	
	// Next see if there are any closer backfacing triangles.
	
	FOR_BEGIN(i, (S32)shapeCount);
	{
		const PSDKShape*	shape;
		U32					inRangeCount = 0;
		U32					outOfRangeIndex = 3;
		PSDKRaycastResults	results;

		if(	!psdkActorGetShapeByIndex(actor, i, &shape) ||
			!psdkShapeGetBounds(shape, boundsMin, boundsMax))
		{
			continue;
		}
		
		FOR_BEGIN(j, 3);
		{
			if(	pos[j] > boundsMin[j] &&
				pos[j] < boundsMax[j])
			{
				inRangeCount++;
			}else{
				outOfRangeIndex = j;
			}
		}
		FOR_END;
		
		if(inRangeCount < 2){
			// No axis-rays can hit this shape, just skip it.
			continue;
		}
		
		FOR_BEGIN(j, 3);
		{
			Vec3 posSide;
			
			if(	inRangeCount != 3 &&
				j != outOfRangeIndex)
			{
				continue;
			}
			
			if(pos[j] > boundsMin[j]){
				copyVec3(pos, posSide);
				if(minDist[0][j] != FLT_MAX){
					posSide[j] -= minDist[0][j] - 0.01f;
				}else{
					posSide[j] = boundsMin[j];
				}
				
				if(psdkShapeRaycast(shape, sceneOffset, posSide, pos, &results)){
					PERFINFO_AUTO_STOP();
					return 1;
				}
			}
			
			if(pos[j] < boundsMax[j]){
				copyVec3(pos, posSide);
				if(minDist[1][j] != FLT_MAX){
					posSide[j] += minDist[1][j] - 0.01f;
				}else{
					posSide[j] = boundsMax[j];
				}
				
				if(psdkShapeRaycast(shape, sceneOffset, posSide, pos, &results)){
					PERFINFO_AUTO_STOP();
					return 1;
				}
			}
		}
		FOR_END;
	}
	FOR_END;
	
	PERFINFO_AUTO_STOP();
	return 0;
}

struct PSDKQueryShapesShapeOverlapReport : public NxUserEntityReport<NxShape*> {
	PSDKQueryShapesCBData	data;
	PSDKQueryShapesCB		callback;
	
	virtual bool onEvent(	NxU32 shapeCount,
							NxShape** shapes)
	{
		void*	goodShapes[100];
		S32		curCount = 0;
		
		data.shapes = (void**)goodShapes;
		
		FOR_BEGIN(i, (S32)shapeCount);
		{
			NxShape* nxShape = shapes[i];
			
			switch(nxShape->getType()){
				xcase NX_SHAPE_MESH:
				acase NX_SHAPE_CONVEX:
				acase NX_SHAPE_HEIGHTFIELD:{
					goodShapes[curCount] = nxShape;
					
					if(++curCount == ARRAY_SIZE(goodShapes)){
						data.shapeCount = curCount;
						PERFINFO_AUTO_START("callback", 1);
							callback(&data);
						PERFINFO_AUTO_STOP();
						curCount = 0;
					}
				}
			}
		}
		FOR_END;

		if(curCount){
			data.shapeCount = curCount;
			PERFINFO_AUTO_START("callback", 1);
				callback(&data);
			PERFINFO_AUTO_STOP();
			curCount = 0;
		}

		return true;
	}
};

S32	psdkSceneQueryShapesInCapsule(	PSDKScene* scene,
									const Vec3 sceneOffset,
									const PSDKQueryShapesInCapsuleParams* params)
{
	if(!scene){
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	NxCapsule nxCapsule;
	
	copyVec3(params->source, nxCapsule.p0);
	copyVec3(params->target, nxCapsule.p1);
	nxCapsule.radius = params->radius;

	if(sceneOffset){
		addVec3(sceneOffset, nxCapsule.p0, nxCapsule.p0);
		addVec3(sceneOffset, nxCapsule.p1, nxCapsule.p1);
	}
	
	PSDKQueryShapesShapeOverlapReport report;
	
	report.callback = params->callback;
	
	report.data.input.userPointer = params->userPointer;
	
	if(	!sceneOffset ||
		vec3IsZero(sceneOffset))
	{
		report.data.input.flags.hasOffset = 0;
		zeroVec3(report.data.input.sceneOffset);
	}else{
		report.data.input.flags.hasOffset = 1;
		copyVec3(sceneOffset, report.data.input.sceneOffset);
	}

	report.data.input.queryType = PSDK_QS_CAPSULE;
	report.data.input.capsule.radius = params->radius;
	copyVec3(params->source, report.data.input.capsule.source);
	copyVec3(params->target, report.data.input.capsule.target);

	NxGroupsMask mask;
	mask.bits0 = 0;
	mask.bits1 = 0;
	mask.bits2 = 0;
	mask.bits3 = params->filterBits; // checks against bits1

	scene->nxScene->overlapCapsuleShapes(	nxCapsule,
											NX_STATIC_SHAPES,
											0,
											NULL,
											&report,
											0xffffffff,
											&mask,
											false);
		
	PERFINFO_AUTO_STOP();
											
	return 1;
}

S32	psdkSceneQueryShapesInAABB(	const PSDKScene* scene,
								const Vec3 sceneOffset,
								const PSDKQueryShapesInAABBParams* params)
{
	if(!scene){
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();

	NxBounds3 nxBounds;
	
	copyVec3(params->aabbMin, nxBounds.min);
	copyVec3(params->aabbMax, nxBounds.max);

	if(sceneOffset){
		addVec3(sceneOffset, nxBounds.min, nxBounds.min);
		addVec3(sceneOffset, nxBounds.max, nxBounds.max);
	}
	
	PSDKQueryShapesShapeOverlapReport report;
	
	report.callback = params->callback;

	report.data.input.userPointer = params->userPointer;

	if(	!sceneOffset ||
		vec3IsZero(sceneOffset))
	{
		report.data.input.flags.hasOffset = 0;
		zeroVec3(report.data.input.sceneOffset);
	}else{
		report.data.input.flags.hasOffset = 1;
		copyVec3(sceneOffset, report.data.input.sceneOffset);
	}

	report.data.input.queryType = PSDK_QS_AABB;
	copyVec3(params->aabbMin, report.data.input.aabb.aabbMin);
	copyVec3(params->aabbMax, report.data.input.aabb.aabbMax);

	NxGroupsMask mask;
	mask.bits0 = 0;
	mask.bits1 = 0;
	mask.bits2 = 0;
	mask.bits3 = params->filterBits; // checks against bits1

	scene->nxScene->overlapAABBShapes(	nxBounds,
										NX_STATIC_SHAPES,
										0,
										NULL,
										&report,
										0xffffffff,
										&mask,
										false);

	PERFINFO_AUTO_STOP();
											
	return 1;
}

S32 psdkShapeGetActor(	const PSDKShape* shape,
						PSDKActor** actorOut)
{
	if(	!shape ||
		!actorOut)
	{
		return 0;
	}
	
	const NxShape* nxShape = (const NxShape*)shape;

	*actorOut = (PSDKActor*)nxShape->getActor().userData;
	
	return 1;
}

S32 psdkShapeGetMat(const PSDKShape* shape,
					Mat4 matOut)
{
	const NxShape* nxShape = (const NxShape*)shape;
	
	nxShape->getGlobalPosition().get(matOut[3]);
	nxShape->getGlobalOrientation().getColumnMajor(matOut[0]);
	
	return 1;
}

S32 psdkShapeGetBounds(const PSDKShape* shape,
					   Vec3 worldMinOut,
					   Vec3 worldMaxOut)
{
	const NxShape*	nxShape = (const NxShape*)shape;
	NxBounds3		bounds;

	if(	!shape ||
		!worldMinOut ||
		!worldMaxOut)
	{
		return 0;
	}

	nxShape->getWorldBounds(bounds);

	vec3FromNxVec3(worldMinOut, bounds.min);
	vec3FromNxVec3(worldMaxOut, bounds.max);

	return 1;
}

S32 psdkShapeGetCookedMesh(	const PSDKShape* shape,
							PSDKCookedMesh** meshOut)
{
	const NxShape*	nxShape = (const NxShape*)shape;
	PSDKCookedMesh*	mesh = (PSDKCookedMesh*)nxShape->userData;
	
	assert(!alIsEmpty(SAFE_MEMBER(mesh, alRefs)));
	
	*meshOut = mesh;
	
	return !!*meshOut;
}

struct PSDKQueryShapeTrianglesTriangleOverlapReport : public NxUserEntityReport<NxU32> {
	const NxShape*					shape;
	PSDKShapeQueryTrianglesCB		callback;
	PSDKShapeQueryTrianglesCBData	data;
	void*							userPointer;
	
	virtual bool onEvent(	NxU32 triCount,
							NxU32* tris)
	{
		bool bContinue;

		data.triCount = triCount;
		data.triIndexes = tris;
		
		PERFINFO_AUTO_START("callback", 1);
			bContinue = callback(&data);
		PERFINFO_AUTO_STOP();
		
		return bContinue;
	}
};

S32 psdkShapeQueryTrianglesInAABB(	const PSDKShape* shape,
									const Vec3 sceneOffset,
									const Vec3 aabbMin,
									const Vec3 aabbMax,
									PSDKShapeQueryTrianglesCB callback,
									void* userPointer)
{
	PERFINFO_AUTO_START_FUNC();

	const NxShape*									nxShape = (const NxShape*)shape;
	PSDKQueryShapeTrianglesTriangleOverlapReport	triReport;
	NxBounds3										bounds;
	
	triReport.callback = callback;
	
	triReport.data.input.userPointer = userPointer;
	triReport.data.input.shape = shape;

	copyVec3(aabbMin, triReport.data.input.aabbMin);
	copyVec3(aabbMax, triReport.data.input.aabbMax);
	
	bounds.min.set(aabbMin);
	bounds.max.set(aabbMax);

	if(	!sceneOffset ||
		vec3IsZero(sceneOffset))
	{
		triReport.data.input.flags.hasOffset = 0;
		zeroVec3(triReport.data.input.sceneOffset);
	}else{
		triReport.data.input.flags.hasOffset = 1;
		copyVec3(sceneOffset, triReport.data.input.sceneOffset);
		addVec3(sceneOffset, bounds.min, bounds.min);
		addVec3(sceneOffset, bounds.max, bounds.max);
	}

	{
		PSDKActor *pActor = NULL;
		
		triReport.data.input.flags.hasOneWayCollision = false;

		if (psdkShapeGetActor(shape, &pActor))
		{
			triReport.data.input.flags.hasOneWayCollision  = pActor->flags.hasOneWayCollision;
		}
	}
	

	switch(nxShape->getType()){
		xcase NX_SHAPE_MESH:{
			NxTriangleMeshShape* tms = (NxTriangleMeshShape*)nxShape;
			
			PERFINFO_AUTO_START("overlapAABBTriangles",1);
			tms->overlapAABBTriangles(	bounds,
										NX_QUERY_WORLD_SPACE,
										&triReport);
			PERFINFO_AUTO_STOP();
		}
		
		xcase NX_SHAPE_CONVEX:{
			PERFINFO_AUTO_START("NX_SHAPE_CONVEX",1);
			NxConvexShape*	cs = (NxConvexShape*)nxShape;
			NxConvexMesh&	cm = cs->getConvexMesh();
			U32				triCount = cm.getCount(0, NX_ARRAY_TRIANGLES);
			U32				indexes[100];
			U32				remain = triCount;
			U32				start = 0;
			
			while(remain){
				S32 count = MIN(ARRAY_SIZE(indexes), remain);
				
				FOR_BEGIN(i, count);
				{
					indexes[i] = start + i;
				}
				FOR_END;
				
				triReport.data.triCount = count;
				triReport.data.triIndexes = indexes;
				
				PERFINFO_AUTO_START("callback (convex)", 1);
					callback(&triReport.data);
				PERFINFO_AUTO_STOP();
				
				remain -= count;
				start += count;
			}
			PERFINFO_AUTO_STOP();
		}
		
		xcase NX_SHAPE_HEIGHTFIELD:{
			NxHeightFieldShape*	hfs = (NxHeightFieldShape*)nxShape;
			
			PERFINFO_AUTO_START("overlapAABBTrianglesHF",1);
			hfs->overlapAABBTriangles(	bounds,
										NX_QUERY_WORLD_SPACE,
										&triReport);
			PERFINFO_AUTO_STOP();
		}
	}
	
	PERFINFO_AUTO_STOP();
	
	return 1;
}
										
S32 psdkShapeQueryTrianglesByIndex(	const PSDKShape* shape,
									const U32* indexes,
									U32 triCount,
									Vec3 (*triVertsOut)[3],
									S32 doWorldTransform)
{
	PERFINFO_AUTO_START_FUNC();
	
	const NxShape* nxShape = (const NxShape*)shape;

	switch(nxShape->getType()){
		xcase NX_SHAPE_MESH:{
			NxTriangleMeshShape*	tms = (NxTriangleMeshShape*)nxShape;
			NxTriangle				tri;
			
			FOR_BEGIN(i, (S32)triCount);
			{
				tms->getTriangle(	tri,
									NULL,
									NULL,
									indexes[i],
									!!doWorldTransform,
									!!doWorldTransform);
		
				FOR_BEGIN(j, 3);
				{
					tri.verts[j].get(triVertsOut[i][j]);
				}
				FOR_END;
			}
			FOR_END;
		}
		
		xcase NX_SHAPE_CONVEX:{
			NxConvexShape*	cs = (NxConvexShape*)nxShape;
			NxConvexMesh&	cm = cs->getConvexMesh();
			const U32*		tris = (const U32*)cm.getBase(0, NX_ARRAY_TRIANGLES);
			const NxVec3*	verts = (const NxVec3*)cm.getBase(0, NX_ARRAY_VERTICES);
			
			FOR_BEGIN(i, (S32)triCount);
			{
				U32 triIndex = indexes[i] * 3;
				
				FOR_BEGIN(j, 3);
				{
					verts[tris[triIndex + j]].get(triVertsOut[i][j]);
				}
				FOR_END;
			}
			FOR_END;
		}
		
		xcase NX_SHAPE_HEIGHTFIELD: {
			Vec3 unused;
			FillHeightfieldTriangles((NxHeightFieldShape*)nxShape, indexes, triCount, triVertsOut, /*doWorldTransform=*/!!doWorldTransform, /*hasOffset=*/false, unused);
		}
	}
	
	PERFINFO_AUTO_STOP();
	
	return 1;
}

// a sloppy AABB check
bool _isTriangleDefinitelyOutsideAABB(NxVec3 verts[3],const Vec3 aabbMin,const Vec3 aabbMax)
{
	if (verts[0].x < aabbMin[0] && verts[1].x < aabbMin[0] && verts[2].x < aabbMin[0])
	{
		return true;
	}
	if (verts[0].y < aabbMin[1] && verts[1].y < aabbMin[1] && verts[2].y < aabbMin[1])
	{
		return true;
	}
	if (verts[0].z < aabbMin[2] && verts[1].z < aabbMin[2] && verts[2].z < aabbMin[2])
	{
		return true;
	}

	if (verts[0].x > aabbMax[0] && verts[1].x > aabbMax[0] && verts[2].x > aabbMax[0])
	{
		return true;
	}
	if (verts[0].y > aabbMax[1] && verts[1].y > aabbMax[1] && verts[2].y > aabbMax[1])
	{
		return true;
	}
	if (verts[0].z > aabbMax[2] && verts[1].z > aabbMax[2] && verts[2].z > aabbMax[2])
	{
		return true;
	}

	return false;
}

S32	psdkShapeQueryTrianglesInAABBWithIndices(	const PSDKShape* shape,
										const U32* inputIndices,
										U32 inputTriCount,
										const Vec3 sceneOffset,
										const Vec3 aabbMin,
										const Vec3 aabbMax,
										PSDKShapeQueryTrianglesCB callback,
										void* userPointer)
{
	PERFINFO_AUTO_START_FUNC();
	
	const NxShape* nxShape = (const NxShape*)shape;
	PSDKShapeQueryTrianglesCBData cbData;

	devassert(inputTriCount <= 200);

	U32 outputIndices[200];

	cbData.triCount = 0;
	cbData.triIndexes = outputIndices;
	cbData.input.userPointer = userPointer;
	cbData.input.shape = shape;

	copyVec3(aabbMin, cbData.input.aabbMin);
	copyVec3(aabbMax, cbData.input.aabbMax);

	if(	!sceneOffset ||
		vec3IsZero(sceneOffset))
	{
		cbData.input.flags.hasOffset = 0;
		zeroVec3(cbData.input.sceneOffset);
	}else{
		cbData.input.flags.hasOffset = 1;
		copyVec3(sceneOffset, cbData.input.sceneOffset);
	}

	{
		PSDKActor *pActor = NULL;
		
		cbData.input.flags.hasOneWayCollision = false;

		if (psdkShapeGetActor(shape, &pActor))
		{
			cbData.input.flags.hasOneWayCollision  = pActor->flags.hasOneWayCollision;
		}
	}

	switch(nxShape->getType()){
		xcase NX_SHAPE_MESH:{
			NxTriangleMeshShape*	tms = (NxTriangleMeshShape*)nxShape;
			NxTriangle				tri;
			
			FOR_BEGIN(i, (S32)inputTriCount);
			{
				tms->getTriangle(	tri,
									NULL,
									NULL,
									inputIndices[i]);
		
				// do a sloppy AABB check
				if (!_isTriangleDefinitelyOutsideAABB(tri.verts,aabbMin,aabbMax))
				{
					outputIndices[cbData.triCount++] = inputIndices[i];
				}
			}
			FOR_END;
		}
		
		xcase NX_SHAPE_CONVEX:{
			NxConvexShape*	cs = (NxConvexShape*)nxShape;
			NxConvexMesh&	cm = cs->getConvexMesh();
			const U32*		tris = (const U32*)cm.getBase(0, NX_ARRAY_TRIANGLES);
			const NxVec3*	verts = (const NxVec3*)cm.getBase(0, NX_ARRAY_VERTICES);
			
			FOR_BEGIN(i, (S32)inputTriCount);
			{
				NxVec3 triVerts[3];
				U32 triIndex = inputIndices[i] * 3;
				
				FOR_BEGIN(j, 3);
				{
					 triVerts[j] = verts[tris[triIndex + j]];
				}
				FOR_END;

				// do a sloppy AABB check
				if (!_isTriangleDefinitelyOutsideAABB(triVerts,aabbMin,aabbMax))
				{
					outputIndices[cbData.triCount++] = inputIndices[i];
				}
			}
			FOR_END;
		}
		
		xcase NX_SHAPE_HEIGHTFIELD:{
			NxHeightFieldShape*	hfs = (NxHeightFieldShape*)nxShape;
			NxTriangle			tri;
			
			FOR_BEGIN(i, (S32)inputTriCount);
			{
				NxVec3 triVerts[3];
				hfs->getTriangle(	tri,
									NULL,
									NULL,
									inputIndices[i]);

				// do a sloppy AABB check
				if (!_isTriangleDefinitelyOutsideAABB(tri.verts,aabbMin,aabbMax))
				{
					outputIndices[cbData.triCount++] = inputIndices[i];
				}
			}
			FOR_END;
		}
	}

	callback(&cbData);

	PERFINFO_AUTO_STOP();
	
	return 1;
}

static S32 psdkSetParameter(U32 param, F32 value){
	if(!nxSDK){
		return 0;
	}
	
	return nxSDK->setParameter((NxParameter)param, value);
}

void psdkSceneDrawDebug(PSDKScene* scene)
{
	if(!SAFE_MEMBER(scene, nxScene)){
		return;
	}

	wcWaitForSimulationToEndFG(1, NULL, false);

	const NxDebugRenderable* dbgRenderable = scene->nxScene->getDebugRenderable();
	if(!dbgRenderable)
		return;

	psdkSetParameter(NX_VISUALIZATION_SCALE, 1.0f);

	Vec3 pt0;
	Vec3 pt1;
	Vec3 pt2;

	psdkSetParameter(NX_VISUALIZE_COLLISION_SHAPES, 1);
	//psdkSetParameter(NX_VISUALIZE_COLLISION_AABBS, 1);
	//psdkSetParameter(NX_VISUALIZE_COLLISION_AXES, 1);

	// Render points
    {
        NxU32 NbPoints = dbgRenderable->getNbPoints();
        const NxDebugPoint* Points = dbgRenderable->getPoints();

        while(NbPoints--)
        {
			vec3FromNxVec3(pt0, Points->p);
			copyVec3(pt0, pt1);
			pt1[1] += 1;
			wlDrawLine3D_2(pt0, 0xffffffff, pt1, 0xffff0000);
			//wlDrawLine3D_2(zerovec3, 0xffffffff, pt0, 0xff00ff00);
            Points++;
        }
    }

    // Render lines
    {
        NxU32 NbLines = dbgRenderable->getNbLines();
        const NxDebugLine* Lines = dbgRenderable->getLines();

        while(NbLines--)
        {
			vec3FromNxVec3(pt0, Lines->p0);
			vec3FromNxVec3(pt1, Lines->p1);
			wlDrawLine3D_2(pt0, 0xffffffff, pt1, 0xff00ff00);

			if(0){
				copyVec3(pt0, pt1);
				pt1[1] = 0;
				wlDrawLine3D_2(pt0, 0xffff0000, pt1, 0xffff0000);

				vec3FromNxVec3(pt0, Lines->p1);
				copyVec3(pt0, pt1);
				pt1[1] = 0;
				wlDrawLine3D_2(pt0, 0xffff0000, pt1, 0xffff0000);
			}

			//wlDrawLine3D_2(zerovec3, 0xffffffff, pt0, 0xff00ff00);

            Lines++;
        }
    }

    // Render triangles
    {
        NxU32 NbTris = dbgRenderable->getNbTriangles();
        const NxDebugTriangle* Triangles = dbgRenderable->getTriangles();

        while(NbTris--)
        {
			vec3FromNxVec3(pt0, Triangles->p0);
			vec3FromNxVec3(pt1, Triangles->p1);
			vec3FromNxVec3(pt2, Triangles->p2);

			//wlDrawLine3D_2(zerovec3, 0xffffffff, pt0, 0xff00ff00);

			wlDrawLine3D_2(pt0, 0xffffffff, pt1, 0xff00ffff);
			wlDrawLine3D_2(pt1, 0xffffffff, pt2, 0xff00ffff);
			wlDrawLine3D_2(pt2, 0xffffffff, pt0, 0xff00ffff);

            Triangles++;
        }
    }
}

static S32 psdkVerifyDLLsForEachModuleCallback(const ForEachModuleCallbackData* data){
	const char* exeFolder = (const char*)data->userPointer;
	char		modulePath[CRYPTIC_MAX_PATH];
	char		expectedPath[CRYPTIC_MAX_PATH];
	const char*	moduleName;
	
	strcpy(modulePath, data->modulePath);
	forwardSlashes(modulePath);
	
	moduleName = getFileName(modulePath);
	
	if(	!stricmp(moduleName, "PhysXCore.dll") ||
		!stricmp(moduleName, "PhysXCoreDEBUG.dll") ||
		!stricmp(moduleName, "NxCooking.dll") ||
		!stricmp(moduleName, "NxCookingDEBUG.dll") ||
		!stricmp(moduleName, "PhysXLoader.dll") ||
		!stricmp(moduleName, "PhysXLoaderDEBUG.dll"))
	{
		sprintf(expectedPath, "%s/%s", exeFolder, moduleName);

		if (fileExists(expectedPath) && (fileLastChanged(expectedPath) != fileLastChanged(modulePath)))
		{
			const char* moduleFolder = getDirectoryName(modulePath);
			assert(stricmp(expectedPath, modulePath)!=0);
			FatalErrorf("\"%s\" was loaded from\n"
						"\"%s\"\n"
						"but should be from\n"
						"\"%s\".",
						moduleName,
						moduleFolder,
						exeFolder);
		}
	}
	
	return 1;
}

static bool psdkLoadDLL(const char *pcPath, const char *pcFile)
{
	#if PHYSX_USE_RELEASE_LIBS
		#define PHYSX_DEBUG_SUFFIX	""
	#else
		#define PHYSX_DEBUG_SUFFIX	"DEBUG"
	#endif

	char loadMe[CRYPTIC_MAX_PATH];
	sprintf(loadMe, "%s/%s"PHYSX_DEBUG_SUFFIX".dll", pcPath, pcFile);
	return (LoadLibrary_UTF8(loadMe) ? true : false);

	#undef PHYSX_DEBUG_SUFFIX
}

void psdkInit(S32 useCCD){
	// Create the physics SDK and set relevant parameters.

	if(nxSDK){
		return;
	}

	loadstart_printf("Initializing PhysX... ");

	InitializeCriticalSection(&psdkState.csQueuedReleases);
	InitializeCriticalSection(&psdkState.csMainPSDK);
	InitializeCriticalSection(&psdkState.csCooking);

	char exePath[CRYPTIC_MAX_PATH];
	getExecutableDir(exePath);

	bool bLoadedNxCooking	= psdkLoadDLL(exePath, "NxCooking");
	bool bLoadedPhysXCore	= psdkLoadDLL(exePath, "PhysXCore");
	bool bLoadedPhysXLoader	= psdkLoadDLL(exePath, "PhysXLoader");

	NxUserAllocator* nxUserAllocator = psdkGetAllocator();
	NxUserOutputStream* nxUserOutputStream = psdkGetOutputStream();
	NxPhysicsSDKDesc sdkDesc;
	NxSDKCreateError errorCode;
	const char *pcError = "NEEDS VALUE SET";

	sdkDesc.flags |= NX_SDKF_NO_HARDWARE;
	psdkState.flags.noHardwareSupport = 1;
	#if !_PS3
		sdkDesc.gpuHeapSize = 0;
	#endif

	unsigned long availableMemA,availableMemB;
	getPhysicalMemoryEx(NULL, NULL, &availableMemA);
	nxSDK = NxCreatePhysicsSDK(	NX_PHYSICS_SDK_VERSION,
								nxUserAllocator,
								nxUserOutputStream,
								sdkDesc,
								&errorCode);
	getPhysicalMemoryEx(NULL, NULL, &availableMemB);
	memMonitorTrackUserMemory("nxSDK", 1, availableMemA-availableMemB, 1);

	switch (errorCode) {
		xcase NXCE_NO_ERROR:			pcError = "No errors occurred when creating the Physics SDK.";
		xcase NXCE_PHYSX_NOT_FOUND:		pcError = "Unable to find the PhysX libraries. The PhysX drivers are not installed correctly.";
		xcase NXCE_WRONG_VERSION:		pcError = "The application supplied a version number that does not match with the libraries.";
		xcase NXCE_DESCRIPTOR_INVALID:	pcError = "The supplied SDK descriptor is invalid.";
		xcase NXCE_CONNECTION_ERROR:	pcError = "A PhysX card was found, but there are problems when communicating with the card.";
		xcase NXCE_RESET_ERROR:			pcError = "A PhysX card was found, but it did not reset (or initialize) properly.";
		xcase NXCE_IN_USE_ERROR:		pcError = "A PhysX card was found, but it is already in use by another application.";
		xcase NXCE_BUNDLE_ERROR:		pcError = "A PhysX card was found, but there are issues with loading the firmware.";
	}

	// if someone hits this w/ lib errorCode, you can check which libs loaded ok above
	assertmsgf(	nxSDK,
				"Failed to load PhysX SDK version 0x%x with error \"%s\"",
				NX_PHYSICS_SDK_VERSION,
				pcError);

	#if !PLATFORM_CONSOLE
	{
		forEachModule(	psdkVerifyDLLsForEachModuleCallback,
						GetCurrentProcessId(),
						exePath);

		// Uses 3-4MB less than this if CUDA is unavailable, but no easy way to test here?
		//memMonitorTrackUserMemory("nxSDK", 1, (IsUsingVista()?24:8)*1024*1024, 1);
	}
	#elif _PS3
    {
	    static const uint8_t priorities[8] = {0,1,1,1,1,};

	    S32 r = NxCellSpursControl::initWithSpurs(GetPs3SpursInstance(), 5, const_cast<uint8_t*>(priorities), false);
        assert(r == NX_CELL_SPURS_OK);
    }
	#endif

	if(useCCD){
		psdkSetParameter(NX_CONTINUOUS_CD, 1);
	}
	//psdkSetParameter(NX_BOUNCE_THRESHOLD, (-2/0.3048));
	
	loadend_printf("done");
}

void psdkUninit(void){
	if(!nxSDK){
		return;
	}

	nxSDK->release();
	nxSDK = NULL;
}

void psdkDisableThreadedCooking(void){
	disableThreadedCooking = 1;
}

S32 psdkHasHardwareSupport(void){
	if(	psdkState.flags.noHardwareSupport ||
		!nxSDK)
	{
		return 0;
	}
	
	return nxSDK->getHWVersion() != NX_HW_VERSION_NONE;
}

S32 psdkSimulationOwnershipCreate(PSDKSimulationOwnership** soOut){
	if(!nxSDK){
		return 0;
	}

	psdkEnterCS();
	
	if(	psdkState.so ||
		!soOut)
	{
		psdkLeaveCS();
		return 0;
	}
	
	psdkState.so = callocStruct(PSDKSimulationOwnership);
	*soOut = psdkState.so;
	
	psdkState.soThreadID = GetCurrentThreadId();
	
	psdkLeaveCS();
	
	return 1;
}

S32 psdkSimulationOwnershipDestroy(PSDKSimulationOwnership** soInOut){
	PSDKSimulationOwnership* so = SAFE_DEREF(soInOut);

	psdkEnterCS();

	if(	!psdkState.so ||
		so != psdkState.so)
	{
		psdkLeaveCS();
		return 0;
	}
	
	assert(psdkState.soThreadID == GetCurrentThreadId());
	
	psdkState.soThreadID = NULL;
	SAFE_FREE(psdkState.so);
	*soInOut = NULL;

	psdkLeaveCS();

	return 1;
}

void psdkSimulationLockFG(PSDKSimulationOwnership* so){
	assert(GetCurrentThreadId() == psdkState.soThreadID);
	assert(so == psdkState.so);
	ASSERT_FALSE_AND_SET(psdkState.simulationLockCountFG);
}

void psdkSimulationUnlockFG(PSDKSimulationOwnership* so){
	assert(GetCurrentThreadId() == psdkState.soThreadID);
	assert(so == psdkState.so);

	assert(!psdkState.simulationLockCountBG);
	assert(!psdkState.simulatingScenesCountBG);

	assert(psdkState.simulationLockCountFG == 1);
	psdkState.simulationLockCountFG = 0;

	psdkQueuedReleaseProcessAll();
}

void psdkSimulationLockBG(PSDKSimulationOwnership* so){
	assert(psdkState.simulationLockCountFG == 1);

	ASSERT_FALSE_AND_SET(psdkState.simulationLockCountBG);
	ASSERT_FALSE_AND_SET(psdkState.simulatingScenesCountBG);
}

void psdkSimulationUnlockBG(PSDKSimulationOwnership* so){
	assert(psdkState.simulationLockCountFG == 1);
	assert(psdkState.simulationLockCountBG == 1);
	assert(psdkState.simulatingScenesCountBG == 1);
	
	psdkState.simulationLockCountBG = 0;
	psdkState.simulatingScenesCountBG = 0;
}

S32	psdkSceneLimitReached(void){
	return psdkState.flags.sceneRequestFailed;
}

U32 psdkGetSceneCount(void){
	return psdkState.scene.count;
}

static S32 psdkVerifyMeshDesc(const PSDKMeshDesc* meshDesc){
	Vec3 minVert = {FLT_MAX, FLT_MAX, FLT_MAX};
	Vec3 maxVert = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
	
	for(S32 i = 0; i < meshDesc->triCount * 3; i++){
		S32 triIndex = meshDesc->triArray[i];

		assert(triIndex >= 0 && triIndex < meshDesc->vertCount);
	}

	for(S32 i = 0; i < meshDesc->vertCount; i++){
		const F32* vert = meshDesc->vertArray[i];

		for(S32 j = 0; j < 3; j++){
			assert(fabsf(vert[j]) < 1e6);
			
			MAX1(maxVert[j], vert[j]);
			MIN1(minVert[j], vert[j]);
		}
	}
	
	{
		S32 goodDeltaCount = 0;
		
		for(S32 i = 0; i < 3; i++){
			if(maxVert[i] - minVert[i] > 0.01f){
				goodDeltaCount++;
			}
		}
		
		if(goodDeltaCount < 2){
			if(stricmp(meshDesc->name, "pre-cooked mesh")){
				printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
							"Request convex hull for invalid object - must have thickness on all axes: %s\n",
							meshDesc->name);
			}

			return 0;
		}

		if(	!meshDesc->triCount &&
			goodDeltaCount < 3)
		{
			if(stricmp(meshDesc->name, "pre-cooked mesh")){
				printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
							"Request convex hull for invalid object - must have thickness on all axes: %s\n",
							meshDesc->name);
			}

			return 0;
		}
	}

	return 1;
}

static AssociationListType psdkCookedMeshRefListType;

static void psdkCookedMeshRefNotifyListEmpty(	void* voidMesh,
												AssociationList* emptyList)
{
	PSDKCookedMesh* mesh = (PSDKCookedMesh*)voidMesh;

	if(mesh->flags.destroyed){
		psdkCookedMeshDestroyInternal(mesh);
	}
}

static void psdkCreateMeshRefsList(PSDKCookedMesh* mesh){
	if(!psdkCookedMeshRefListType.notifyEmptyFunc){
		psdkCookedMeshRefListType.notifyEmptyFunc = psdkCookedMeshRefNotifyListEmpty;
		psdkCookedMeshRefListType.flags.isPrimary = 0;
	}

	alCreate(	&mesh->alRefs,
				mesh,
				&psdkCookedMeshRefListType);
}

MP_DEFINE(PSDKCookedMesh);

static PSDKCookedMesh* psdkCookedMeshAlloc(PSDKCookedMeshType meshType){
	PSDKCookedMesh* mesh;

	if(	meshType < 0 ||
		meshType >= PSDK_CMT_COUNT)
	{
		return NULL;
	}

	psdkEnterCS();
	{
		MP_CREATE(PSDKCookedMesh, 100);

		mesh = MP_ALLOC(PSDKCookedMesh);

		psdkState.totalMeshCount++;
		psdkState.meshTypeCount[meshType]++;
	}
	psdkLeaveCS();

	mesh->flags.meshType = meshType;
	psdkCreateMeshRefsList(mesh);

	return mesh;
}

#if 0
#include "rand.h"
static bool psdkCookingRandomFailure(void)
{
	return randomF32() > 0.9;
}
#else
#define psdkCookingRandomFailure() 0
#endif

SA_ORET_OP_VALID static NxTriangleMesh* psdkCookedMeshGetNxTriangleMesh(PSDKCookedMesh* mesh)
{
	S64 timeStart;
	S64 timeEnd;

	if(!mesh->triangleMesh.nxTriangleMesh){
		assert(	mesh->threadFlags.triangleMeshIsCooking ||
				mesh->threadFlags.triangleMeshIsCooked ||
				mesh->threadFlags.cookingFailed);
				
		if(mesh->threadFlags.cookingFailed){
			return NULL;
		}
		
		while(mesh->threadFlags.triangleMeshIsCooking){
			PERFINFO_AUTO_START("while (mesh->threadFlags.triangleMeshIsCooking)", 1);
			Sleep(1);
			PERFINFO_AUTO_STOP();
		}

		// Apply cooked data, finish, etc
		PERFINFO_AUTO_START("createTriangleMesh", 1);
			if(mesh->threadFlags.cookingFailed){
				// Nothing to do
			}else{
				assert(mesh->threadFlags.triangleMeshIsCooked);
				assert(mesh->triangleMesh.cookedStream);
				U32 byteSize = mesh->triangleMesh.cookedStream->getBufferSize();
				mesh->triangleMesh.cookedStream->resetByteCounter();
				if(psdkState.flags.printCooking){
					printf("Creating mesh: ");
				}
				GET_CPU_TICKS_64(timeStart);
				psdkCookingEnterCS();

				NxTriangleMesh* nxMesh = nxSDK->createTriangleMesh(	*mesh->triangleMesh.cookedStream,
																	uncompressDeltasWrapper);
				
				psdkCookingLeaveCS();
				GET_CPU_TICKS_64(timeEnd);
				if(psdkState.flags.printCooking){
					printf(	"%"FORM_LL"d ticks, %d/%d bytes\n",
							timeEnd - timeStart,
							byteSize,
							psdkState.totalCookedBytes);
				}
				delete mesh->triangleMesh.cookedStream;
				mesh->triangleMesh.cookedStream = NULL;

				if(psdkCookingRandomFailure()){
					mesh->flags.hadRandomFailure = 1;
					nxMesh = NULL;
				}

				if(!nxMesh){
					printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
								"createTriangleMesh failed: 0x%p\n",
								mesh);

					mesh->threadFlags.cookingFailed = 1;
					mesh->flags.createTriangleMeshFailed = 1;
				}else{
					mesh->triangleMesh.nxTriangleMesh = nxMesh;
					mesh->byteSize = byteSize;
					psdkState.totalCookedBytes += byteSize;
				}
				mesh->threadFlags.triangleMeshIsCooked = 0;
			}
		PERFINFO_AUTO_STOP();
	}
	return mesh->triangleMesh.nxTriangleMesh;
}

static void psdkCookedMeshDestroyInternal(PSDKCookedMesh* mesh){
	if(	!mesh ||
		mesh->flags.queuedForRelease)
	{
		return;
	}

	assert(mesh->flags.destroyed);
	assert(alIsEmpty(mesh->alRefs));

	alDestroy(&mesh->alRefs);
	
	if(	mesh->collisionData &&
		verify(psdkState.cbDestroyCollisionData))
	{
		U32 triCount;
		
		psdkCookedMeshGetTriCount(mesh, &triCount);
		
		psdkState.cbDestroyCollisionData(mesh->collisionData, triCount);
		
		mesh->collisionData = NULL;
	}

	switch(mesh->flags.meshType){
		xcase PSDK_CMT_TRIANGLE:{
			if(!mesh->triangleMesh.nxTriangleMesh){
				assert(	mesh->threadFlags.triangleMeshIsCooking ||
						mesh->threadFlags.triangleMeshIsCooked ||
						mesh->threadFlags.cookingFailed);
				if(mesh->threadFlags.cookingFailed){
					return;
				}
				while (mesh->threadFlags.triangleMeshIsCooking){
					PERFINFO_AUTO_START("while (mesh->threadFlags.triangleMeshIsCooking)", 1);
					Sleep(1);
					PERFINFO_AUTO_STOP();
				}
				psdkQueuedReleaseAdd(PSDK_QR_COOKED_STREAM, (void**)&mesh->triangleMesh.cookedStream);
				mesh->triangleMesh.cookedStream = NULL;
			}else{
				NxTriangleMesh* nxTriangleMesh = psdkCookedMeshGetNxTriangleMesh(mesh);
				assert(	!mesh->threadFlags.triangleMeshIsCooking &&
						!mesh->threadFlags.triangleMeshIsCooked);
				psdkQueuedReleaseAdd(PSDK_QR_TRIANGLE_MESH, (void**)&nxTriangleMesh);
				mesh->triangleMesh.nxTriangleMesh = NULL;
			}
		}

		xcase PSDK_CMT_CONVEX:{
			psdkQueuedReleaseAdd(PSDK_QR_CONVEX_MESH, (void**)&mesh->nxConvexMesh);
		}

		xcase PSDK_CMT_HEIGHTFIELD:{
			psdkQueuedReleaseAdd(PSDK_QR_HEIGHTFIELD, (void**)&mesh->heightField.nxHeightField);
		}

		xcase PSDK_CMT_SPHERE:{
			// just a description, there is nothing to release
		}

		xcase PSDK_CMT_CAPSULE:{
			// just a description, there is nothing to release
		}

		xcase PSDK_CMT_BOX:{
			// just a description, there is nothing to release
		}

		xdefault:{
			assertmsg(0, "Invalid mesh type.");
			return;
		}
	}

	assert(psdkState.meshTypeCount[mesh->flags.meshType] > 0);

	psdkState.meshTypeCount[mesh->flags.meshType]--;
	
	assert(psdkState.totalCookedBytes >= mesh->byteSize);
	
	psdkState.totalCookedBytes -= mesh->byteSize;

	MP_FREE(PSDKCookedMesh, mesh);
}

void psdkCookedMeshDestroy(PSDKCookedMesh** meshInOut){
	PSDKCookedMesh* mesh = SAFE_DEREF(meshInOut);

	if(!mesh){
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	assert(	mesh->flags.meshType >= 0 &&
			mesh->flags.meshType < PSDK_CMT_COUNT);
	
	psdkEnterQueuedReleaseCS();
	{
		ASSERT_FALSE_AND_SET(mesh->flags.destroyed);
	}
	psdkLeaveQueuedReleaseCS();

	mesh->flags.queuedForRelease = 1;

	psdkQueuedReleaseAdd(PSDK_QR_COOKED_MESH, (void**)meshInOut);
	PERFINFO_AUTO_STOP();
}

static void psdkCookTriangleMeshSlowPart(	PSDKCookedMesh* mesh,
											const PSDKMeshDesc* meshDesc)
{
	// if(PERFINFO_IS_GOOD_THREAD)
	//	verbose_printf("perf warning: mesh cook in main thread\n");

	assert(mesh->threadFlags.triangleMeshIsCooking);

	S64 timeStart;
	S64 timeEnd;
	NxTriangleMeshDesc nxMeshDesc;

	nxMeshDesc.numVertices         = meshDesc->vertCount;
	nxMeshDesc.pointStrideBytes    = sizeof(meshDesc->vertArray[0]);
	nxMeshDesc.points              = meshDesc->vertArray;
	nxMeshDesc.numTriangles        = meshDesc->triCount;
	nxMeshDesc.triangles           = meshDesc->triArray;
	nxMeshDesc.triangleStrideBytes = sizeof(meshDesc->triArray[0]) * 3;
	nxMeshDesc.compressDeltas      = NULL;

	PERFINFO_AUTO_START("NxCookTriangleMesh", 1);
		if(psdkState.flags.printCooking){
			printf(	"Cooking triangle mesh \"%s\" with %d verts, %d tris: ",
					meshDesc->name ? meshDesc->name : "(unnamed)",
					meshDesc->vertCount,
					meshDesc->triCount);
		}
		//PSDKSharedStream* stream = new PSDKSharedStream();
		PSDKAllocedStream* stream = new PSDKAllocedStream();
		psdkCookingEnterCS();
		NxInitCooking(psdkGetAllocator(), psdkGetOutputStream());
		GET_CPU_TICKS_64(timeStart);
		bool status;
		FP_NO_EXCEPTIONS_BEGIN;
		status = NxCookTriangleMesh(nxMeshDesc, *stream);
		FP_NO_EXCEPTIONS_END;
		GET_CPU_TICKS_64(timeEnd);
		if(psdkCookingRandomFailure()){
			status = false;
		}
		NxCloseCooking();
		psdkCookingLeaveCS();
		if(psdkState.flags.printCooking){
			printf("%"FORM_LL"d ticks.  ", timeEnd - timeStart);
		}
	PERFINFO_AUTO_STOP();
	if(!status){
		mesh->threadFlags.cookingFailed = 1;
		delete stream;
	}else{
		mesh->triangleMesh.cookedStream = stream;
		mesh->threadFlags.triangleMeshIsCooked = 1;
	}
	mesh->threadFlags.triangleMeshIsCooking = 0;
}

typedef struct CookTriangleMeshData {
	PSDKCookedMesh* mesh;
	PSDKMeshDesc meshDesc;
} CookTriangleMeshData;

void psdkCookTriangleMeshInThread(void* user_data, void* data_in, WTCmdPacket* packet)
{
	CookTriangleMeshData* data = (CookTriangleMeshData*)data_in;
	psdkCookTriangleMeshSlowPart(data->mesh, &data->meshDesc);
}

typedef enum
{
	// lock and unlock
	COOKERCMD_COOK_TRIANGLE_MESH = WT_CMD_USER_START,

	COOKERCMD_MAX,
} CookerCmd;

static WorkerThread* wtCookingThread;
static void psdkInitCookerThread(void)
{
	ATOMIC_INIT_BEGIN;
	{
		wtCookingThread = wtCreate(64*1024, 2, NULL, "CookerThread"); // 1 MB (16 bytes per cmd)
		wtRegisterCmdDispatch(wtCookingThread, COOKERCMD_COOK_TRIANGLE_MESH, psdkCookTriangleMeshInThread);
		wtSetThreaded(wtCookingThread, true, 0, false);
		wtSetProcessor(wtCookingThread, THREADINDEX_PHYSX_COOKER);
		wtStart(wtCookingThread);
	}
	ATOMIC_INIT_END;
}

S32 pdskPreCookTriangleMesh(const PSDKMeshDesc* meshDesc,
							void** data_out,
							U32* data_size_out)
{
	if( !meshDesc ||
		!data_out ||
		!data_size_out)
	{
		return 0;
	}

	if(!psdkVerifyMeshDesc(meshDesc)){
		*data_out = NULL;
		*data_size_out = 0;
		return 0;
	}

	S64 timeStart;
	S64 timeEnd;
	NxTriangleMeshDesc nxMeshDesc;

	nxMeshDesc.numVertices         = meshDesc->vertCount;
	nxMeshDesc.pointStrideBytes    = sizeof(meshDesc->vertArray[0]);
	nxMeshDesc.points              = meshDesc->vertArray;
	nxMeshDesc.numTriangles        = meshDesc->triCount;
	nxMeshDesc.triangles           = meshDesc->triArray;
	nxMeshDesc.triangleStrideBytes = sizeof(meshDesc->triArray[0]) * 3;



	nxMeshDesc.compressDeltas      = compressDeltasWrapper;


	PERFINFO_AUTO_START("NxCookTriangleMesh", 1);
		if(psdkState.flags.printCooking){
			printf(	"Cooking triangle mesh \"%s\" with %d verts, %d tris: ",
					meshDesc->name ? meshDesc->name : "(unnamed)",
					meshDesc->vertCount,
					meshDesc->triCount);
		}
		//PSDKSharedStream* stream = new PSDKSharedStream();
		PSDKAllocedStream* stream = new PSDKAllocedStream();
		psdkCookingEnterCS();
		NxInitCooking(psdkGetAllocator(), psdkGetOutputStream());
		GET_CPU_TICKS_64(timeStart);
		bool status;
		FP_NO_EXCEPTIONS_BEGIN;
		status = NxCookTriangleMesh(nxMeshDesc, *stream);
		FP_NO_EXCEPTIONS_END;
		GET_CPU_TICKS_64(timeEnd);
		if(psdkCookingRandomFailure()){
			status = false;
		}
		NxCloseCooking();
		psdkCookingLeaveCS();
		if(psdkState.flags.printCooking){
			printf("%"FORM_LL"d ticks.  ", timeEnd - timeStart);
		}
	PERFINFO_AUTO_STOP();
	if(!status){
		delete stream;
		*data_out = NULL;
		*data_size_out = 0;
		return 0;
	}

	*data_size_out = stream->getBufferSize();
	*data_out = stream->stealBuffer();
	delete stream;
	return 1;
}

void pdskFreeBuffer(void* data)
{
    PSDKAllocedStream::freeBuffer(data);
}

static S32 psdkCreatePreCookedTriangleMesh(PSDKCookedMesh** meshOut,
										   const PSDKMeshDesc* meshDesc)
{
	if(	!meshDesc ||
		!meshOut)
	{
		return 0;
	}

	PSDKCookedMesh* mesh = *meshOut = psdkCookedMeshAlloc(PSDK_CMT_TRIANGLE);

	mesh->triangleMesh.cookedStream = new PSDKAllocedStream(meshDesc->preCookedData, meshDesc->preCookedSize);
	mesh->threadFlags.triangleMeshIsCooked = 1;
	mesh->threadFlags.triangleMeshIsCooking = 0;
	return 1;
}

static S32 psdkCookTriangleMesh(PSDKCookedMesh** meshOut,
								const PSDKMeshDesc* meshDesc)
{
	if(	!meshDesc ||
		!meshOut)
	{
		return 0;
	}

	if(!psdkVerifyMeshDesc(meshDesc)){
		return 0;
	}

	PSDKCookedMesh* mesh = *meshOut = psdkCookedMeshAlloc(PSDK_CMT_TRIANGLE);

	psdkEnterCS();
	PERFINFO_AUTO_START("NxCookTriangleMesh", 1);
	if(	!disableThreadedCooking &&
		!meshDesc->no_thread)
	{
		CookTriangleMeshData*	ctmd;
		char*					buffer;
		size_t					name_size;
		size_t					vert_size;
		size_t					tri_size;
		size_t					buffer_size;

		PERFINFO_AUTO_START("top", 1);
		{
			psdkInitCookerThread();
			mesh->threadFlags.triangleMeshIsCooking = 1;

			name_size = meshDesc->name ? strlen(meshDesc->name) + 1 : 0;
			vert_size = sizeof(meshDesc->vertArray[0]) * meshDesc->vertCount;
			tri_size = sizeof(meshDesc->triArray[0]) * meshDesc->triCount * 3;
			buffer_size =	sizeof(*ctmd) + 
							name_size + 
							vert_size + 
							tri_size;
		}
		PERFINFO_AUTO_STOP_START("wtAllocCmd", 1);
		{
			buffer = (char*)wtAllocCmd(	wtCookingThread,
										COOKERCMD_COOK_TRIANGLE_MESH,
										(S32)buffer_size);
		}
		PERFINFO_AUTO_STOP_START("copy", 1);
		{
			ctmd = (CookTriangleMeshData*)buffer;
			ctmd->mesh = mesh;
			ctmd->meshDesc = *meshDesc;
			buffer += sizeof(*ctmd);

			if(meshDesc->name){
				ctmd->meshDesc.name = buffer;
				strcpy_s(buffer, name_size, meshDesc->name);
				buffer += name_size;
			}

			ctmd->meshDesc.vertArray = (Vec3*)buffer;
			memcpy(buffer, meshDesc->vertArray, vert_size);
			buffer += vert_size;

			ctmd->meshDesc.triArray = (S32*)buffer;
			memcpy(buffer, meshDesc->triArray, tri_size);
			buffer += tri_size;
		}
		PERFINFO_AUTO_STOP_START("send", 1);
		{
			wtSendCmd(wtCookingThread);
		}
		PERFINFO_AUTO_STOP();
	}else{
		mesh->threadFlags.triangleMeshIsCooking = 1;
		psdkCookTriangleMeshSlowPart(mesh, meshDesc);
	}
	PERFINFO_AUTO_STOP();
	psdkLeaveCS();

	return 1;
}

static S32 psdkCookConvexMesh(	PSDKCookedMesh** meshOut,
								const PSDKMeshDesc* meshDesc)
{
	if(	!meshDesc ||
		!meshOut)
	{
		return 0;
	}

	NxConvexMeshDesc nxMeshDesc;

	nxMeshDesc.numVertices         	= meshDesc->vertCount;
	nxMeshDesc.pointStrideBytes    	= sizeof(meshDesc->vertArray[0]);
	nxMeshDesc.points              	= meshDesc->vertArray;
	nxMeshDesc.flags				= NX_CF_COMPUTE_CONVEX;

	if(!psdkVerifyMeshDesc(meshDesc)){
		return 0;
	}

	psdkCookingEnterCS();
	NxInitCooking(psdkGetAllocator(), psdkGetOutputStream());

	PSDKSharedStream stream;

	S64 timeStart;
	S64 timeEnd;
	U32 byteSize;

	PERFINFO_AUTO_START("NxCookConvexMesh", 1);
		if(psdkState.flags.printCooking){
			printf("Cooking convex mesh with %d verts: ", meshDesc->vertCount);
		}
		GET_CPU_TICKS_64(timeStart);
		bool status;
		FP_NO_EXCEPTIONS_BEGIN;
		status = NxCookConvexMesh(nxMeshDesc, stream);
		FP_NO_EXCEPTIONS_END;
		GET_CPU_TICKS_64(timeEnd);
		if(psdkState.flags.printCooking){
			printf("%"FORM_LL"d ticks.  ", timeEnd - timeStart);
		}
		if(!status){
			if(psdkState.flags.assertOnFailure){
				assertmsgf(0, "Convex mesh failed cooking: %s", meshDesc->name);
			}
			if(!meshDesc->no_error_msg)
				Errorf("Convex mesh failed cooking: %s", meshDesc->name);
			PERFINFO_AUTO_STOP();
			NxCloseCooking();
			psdkCookingLeaveCS();
			return 0;
		}
		byteSize = stream.getBufferSize();
		stream.resetByteCounter();
	PERFINFO_AUTO_STOP_START("createConvexMesh", 1);
		if(psdkState.flags.printCooking){
			printf("Creating mesh: ");
		}
		GET_CPU_TICKS_64(timeStart);
		NxConvexMesh* nxMesh = nxSDK->createConvexMesh(stream);
		GET_CPU_TICKS_64(timeEnd);
		if(psdkState.flags.printCooking){
			printf(	"%"FORM_LL"d ticks, %d/%d bytes\n",
					timeEnd - timeStart,
					byteSize,
					psdkState.totalCookedBytes);
		}
	PERFINFO_AUTO_STOP();

	if(!nxMesh){
		NxCloseCooking();
		psdkCookingLeaveCS();
		return 0;		
	}

	PSDKCookedMesh* mesh = *meshOut = psdkCookedMeshAlloc(PSDK_CMT_CONVEX);

	mesh->nxConvexMesh = nxMesh;
	mesh->byteSize = byteSize;

	psdkState.totalCookedBytes += byteSize;

	NxCloseCooking();
	psdkCookingLeaveCS();

	return 1;
}

static S32 psdkCookSphere(	PSDKCookedMesh** meshOut,
							const PSDKMeshDesc* meshDesc)
{
	if(	!meshDesc ||
		!meshOut)
	{
		return 0;
	}
	
	PSDKCookedMesh* mesh = *meshOut = psdkCookedMeshAlloc(PSDK_CMT_SPHERE);
	
	mesh->sphere.radius = meshDesc->sphereRadius;
	
	return 1;
}

static S32 psdkCookCapsule(	PSDKCookedMesh** meshOut,
							const PSDKMeshDesc* meshDesc)
{
	if(	!meshDesc ||
		!meshOut)
	{
		return 0;
	}
	
	PSDKCookedMesh* mesh = *meshOut = psdkCookedMeshAlloc(PSDK_CMT_CAPSULE);
	
	mesh->capsule.radius = meshDesc->sphereRadius;
	mesh->capsule.height = meshDesc->capsuleHeight;
	
	return 1;
}

static S32 psdkCookBox(	PSDKCookedMesh** meshOut,
						const PSDKMeshDesc* meshDesc)
{
	if(	!meshDesc ||
		!meshOut)
	{
		return 0;
	}
	
	PSDKCookedMesh* mesh = *meshOut = psdkCookedMeshAlloc(PSDK_CMT_BOX);
	
	copyVec3(meshDesc->boxSize, mesh->box.size);
	
	return 1;
}

S32 psdkCookedMeshCreate(	PSDKCookedMesh** meshOut,
							const PSDKMeshDesc* meshDesc)
{
	if(!meshDesc){
		return 0;
	}

	if(meshDesc->preCookedSize){
		return psdkCreatePreCookedTriangleMesh(meshOut, meshDesc);
	}

	if(meshDesc->triCount){
		return psdkCookTriangleMesh(meshOut, meshDesc);
	}

	if(meshDesc->vertCount){
		return psdkCookConvexMesh(meshOut, meshDesc);
	}


	if(meshDesc->capsuleHeight){
		return psdkCookCapsule(meshOut, meshDesc);
	}

	if(meshDesc->sphereRadius){
		return psdkCookSphere(meshOut, meshDesc);
	}

	return psdkCookBox(meshOut, meshDesc);
}

S32 psdkCookedMeshIsValid(PSDKCookedMesh* mesh){
	if(!mesh){
		return 0;
	}
	
	if(mesh->flags.meshType == PSDK_CMT_TRIANGLE){
		// Flush threaded cooking
		psdkCookedMeshGetNxTriangleMesh(mesh);
	}
	
	assert(	!mesh->threadFlags.triangleMeshIsCooking &&
			!mesh->threadFlags.triangleMeshIsCooked);
			
	if(mesh->threadFlags.cookingFailed){
		return 0;
	}
	
	return 1;
}

S32	psdkCookedMeshIsConvex(	PSDKCookedMesh* mesh)
{
	return mesh->flags.meshType == PSDK_CMT_CONVEX;		
}

typedef struct PSDKHeightFieldSample {
	S16	height		: 16;
	U8	material0	: 7;
	U8	tessFlag	: 1;
	U8	material1	: 7;
	U8	unused		: 1;
} PSDKHeightFieldSample;

S32 psdkCookedHeightFieldCreate(PSDKCookedMesh** meshOut,
								const PSDKHeightFieldDesc* heightFieldDesc)
{
	NxHeightFieldDesc	nxHeightFieldDesc;
	S32					sx;
	S32					sz;
	S32					gridArea;
	S32					maxIntHeight = (1 << 15) - 1;
	S64 				startTime;
	S64 				endTime;

	if(	!meshOut ||
		!heightFieldDesc)
	{
		return 0;
	}

	GET_CPU_TICKS_64(startTime);

	sx = heightFieldDesc->gridSize[0];
	sz = heightFieldDesc->gridSize[1];

	if(	sx <= 1 ||
		sz <= 1)
	{
		return 0;
	}
	
	nxHeightFieldDesc.format = NX_HF_S16_TM;
	nxHeightFieldDesc.nbColumns = sx;
	nxHeightFieldDesc.nbRows = sz;
	nxHeightFieldDesc.sampleStride = sizeof(PSDKHeightFieldSample);

	gridArea = sx * sz;

	PSDKHeightFieldSample* samples = callocStructs(PSDKHeightFieldSample, gridArea);

	// Find the min and max height;

	F32 maxHeight = -FLT_MAX;
	F32 minHeight = FLT_MAX;

	for(S32 z = 0; z < sz; z++){
		S32			index = z * sx;
		const F32*	height = heightFieldDesc->height + index;

		for(S32 x = 0; x < sx; x++, height++){
			F32 h = *height;

			MAX1F(maxHeight, h);
			MIN1F(minHeight, h);
		}
	}

	F32 heightDiff = maxHeight - minHeight;

	if(heightDiff < 0.001f){
		heightDiff = 0;
		maxHeight = minHeight;
	}
	else if(heightDiff < 1.0f){
		maxIntHeight = (1 << 10) - 1;
	}

	for(S32 z = 0; z < sz; z++){
		PSDKHeightFieldSample*	sample = samples + z;
		S32						index = z * sx;
		const F32*				height = heightFieldDesc->height + index;
		const bool*				hole = heightFieldDesc->holes + index;

		for(S32 x = 0; x < sx; x++){
			F32 scaledHeight;
			S32 heightS32;

			if(heightDiff){
				scaledHeight = (*height - minHeight) / heightDiff;
				heightS32 = (S32)ceil(maxIntHeight * scaledHeight);
				
				MINMAX1(heightS32, 0, maxIntHeight);
				
				sample->height = heightS32;
			}else{
				sample->height = 0;
			}
			
			if(*hole){
				sample->material0 = sample->material1 = 0x7f;
			}
			
			if( psdkState.debug.flags.findHeightFieldPointEnabled
				&&
				fabs(	heightFieldDesc->debug.worldMin[0] +
							(heightFieldDesc->debug.worldMax[0] - heightFieldDesc->debug.worldMin[0]) *
							(F32)x / (F32)(sx - 1) -
							psdkState.debug.findHeightFieldPoint[0]) <
					psdkState.debug.findHeightFieldPointRadius
				&&
				fabs(	heightFieldDesc->debug.worldMin[2] +
							(heightFieldDesc->debug.worldMax[2] - heightFieldDesc->debug.worldMin[2]) *
							(F32)z / (F32)(sz - 1) -
							psdkState.debug.findHeightFieldPoint[2]) <
					psdkState.debug.findHeightFieldPointRadius)
			{
				printf(	"Found heightfield point:"
						" s16=%d (%d * %f [%x], s32=%d),"
						" h=%f [%x],"
						" r=(%f - %f) [%x - %x],"
						" d=%f [%x]"
						"\n"
						,
						sample->height,
						maxIntHeight,
						scaledHeight,
						*(S32*)&scaledHeight,
						heightS32
						,
						*height,
						*(S32*)height
						,
						minHeight,
						maxHeight,
						*(S32*)&minHeight,
						*(S32*)&maxHeight
						,
						heightDiff,
						*(S32*)&heightDiff);
			}

			sample->tessFlag = NX_HF_0TH_VERTEX_SHARED;
			sample += sz;
			height++;
			hole++;
		}
	}

	nxHeightFieldDesc.samples = samples;
	nxHeightFieldDesc.thickness = 2;

	NxHeightField* nxHeightField = nxSDK->createHeightField(nxHeightFieldDesc);

	SAFE_FREE(samples);

	if(!nxHeightField){
		*meshOut = NULL;
		return 0;
	}

	PSDKCookedMesh* mesh = *meshOut = psdkCookedMeshAlloc(PSDK_CMT_HEIGHTFIELD);

	mesh->heightField.nxHeightField = nxHeightField;
	copyVec2(heightFieldDesc->gridSize, mesh->heightField.gridSize);
	copyVec2(heightFieldDesc->worldSize, mesh->heightField.worldSize);
	mesh->heightField.minHeight = minHeight;
	mesh->heightField.maxHeight = maxHeight;			
	mesh->heightField.maxIntHeight = maxIntHeight;

	GET_CPU_TICKS_64(endTime);

	//printf("Time to cook: %s cycles.\n", getCommaSeparatedInt(endTime - startTime));

	return 1;
}

U32 psdkCookedMeshGetBytes(PSDKCookedMesh* mesh){
	return SAFE_MEMBER(mesh, byteSize);
}

void psdkSetDestroyCollisionDataCallback(PSDKDestroyCollisionDataCB callback){
	psdkState.cbDestroyCollisionData = callback;
}

S32 psdkCookedMeshSetCollisionData(	PSDKCookedMesh* mesh,
									void* collisionData)
{
	if(mesh){
		mesh->collisionData = collisionData;
		return 1;
	}
	
	return 0;
}

S32 psdkCookedMeshGetCollisionData(	PSDKCookedMesh* mesh,
									void** collisionDataOut)
{
	if(mesh){
		*collisionDataOut = mesh->collisionData;
		return 1;
	}
	
	return 0;
}

S32 psdkCookedMeshGetTriCount(	PSDKCookedMesh* mesh,
								U32* triCountOut)
{
	switch(mesh->flags.meshType){
		xcase PSDK_CMT_TRIANGLE:{
			NxTriangleMesh* nxTriangleMesh = psdkCookedMeshGetNxTriangleMesh(mesh);
			*triCountOut = SAFE_MEMBER(nxTriangleMesh, getCount(0, NX_ARRAY_TRIANGLES));
		}
		
		xcase PSDK_CMT_CONVEX:{
			*triCountOut = mesh->nxConvexMesh->getCount(0, NX_ARRAY_TRIANGLES);
		}

		xcase PSDK_CMT_HEIGHTFIELD:{
			*triCountOut =  mesh->heightField.nxHeightField->getNbColumns() *
							mesh->heightField.nxHeightField->getNbRows() *
							2;
		}
		
		xdefault:{
			*triCountOut = 0;
			return 0;
		}
	}
	
	return 1;
}

S32	psdkCookedMeshGetTriangles( PSDKCookedMesh* mesh,
								const S32** trisOut,
								U32* triCountOut)
{
	switch(mesh->flags.meshType)
	{
		xcase PSDK_CMT_TRIANGLE: {
			NxTriangleMesh* nxTriangleMesh = psdkCookedMeshGetNxTriangleMesh(mesh);
			if(!nxTriangleMesh) return 0;
			if(nxTriangleMesh->getStride(0, NX_ARRAY_TRIANGLES)!=sizeof(S32)*3)
			{
				ignorableAssertmsg(0, "Definition of NXTriangle is no longer S32*3");
				return 0;
			}

			*triCountOut = nxTriangleMesh->getCount(0, NX_ARRAY_TRIANGLES);
			*trisOut = (S32*)nxTriangleMesh->getBase(0, NX_ARRAY_TRIANGLES);
		}
		
		xcase PSDK_CMT_CONVEX: {
			NxConvexMesh* nxConvexMesh = mesh->nxConvexMesh;
			if(!nxConvexMesh) return 0;
			if(nxConvexMesh->getStride(0, NX_ARRAY_TRIANGLES)!=sizeof(S32)*3)
			{
				ignorableAssertmsg(0, "Definition of NXTriangle is no longer S32*3");
				return 0;
			}

			*triCountOut = nxConvexMesh->getCount(0, NX_ARRAY_TRIANGLES);
			*trisOut = (S32*)nxConvexMesh->getBase(0, NX_ARRAY_TRIANGLES);
		}

		xdefault:{
			*triCountOut = 0;
			return 0;
		}
	}

	return 1;
}

S32	psdkCookedMeshGetVertices(	PSDKCookedMesh* mesh,
								const Vec3** vertsOut,
								U32* vertCountOut)
{
	switch(mesh->flags.meshType)
	{
		xcase PSDK_CMT_TRIANGLE: {
			NxTriangleMesh* nxTriangleMesh = psdkCookedMeshGetNxTriangleMesh(mesh);
			if(!nxTriangleMesh) return 0;
			if(nxTriangleMesh->getStride(0, NX_ARRAY_VERTICES)!=sizeof(Vec3))
			{
				ignorableAssertmsg(0, "Definition of NXVec3 is no longer Vec3");
				return 0;
			}

			*vertsOut = (Vec3*)nxTriangleMesh->getBase(0, NX_ARRAY_VERTICES);
			*vertCountOut = nxTriangleMesh->getCount(0, NX_ARRAY_VERTICES);
		}
		
		xcase PSDK_CMT_CONVEX: {
			NxConvexMesh* nxConvexMesh = mesh->nxConvexMesh;
			if(!nxConvexMesh) return 0;
			if(nxConvexMesh->getStride(0, NX_ARRAY_VERTICES)!=sizeof(Vec3))
			{
				ignorableAssertmsg(0, "Definition of NXVec3 is no longer Vec3");
				return 0;
			}

			*vertsOut = (Vec3*)nxConvexMesh->getBase(0, NX_ARRAY_VERTICES);
			*vertCountOut = nxConvexMesh->getCount(0, NX_ARRAY_VERTICES);
		}

		xdefault: {
			*vertCountOut = 0;
			return 0;
		}
	}

	return 1;
}

static AssociationListType psdkActorDescShapeListType;

//void psdkActorDescMeshNotifyListEmpty(	void* voidActorDesc,
//										AssociationList* emptyList)
//{
//	PSDKActorDesc* actorDesc = (PSDKActorDesc*)voidActorDesc;
//}

//void psdkActorDescMeshNotifyInvalidated(void* voidActorDesc,
//										AssociationList* al,
//										AssociationNode* node)
//{
//	PSDKActorDesc* actorDesc = (PSDKActorDesc*)voidActorDesc;
//}

MP_DEFINE(PSDKShapeDesc);

S32 psdkActorDescCreate(PSDKActorDesc** actorDescOut){
	PSDKActorDesc* actorDesc;

	if(!actorDescOut){
		return 0;
	}

	actorDesc = *actorDescOut = callocStruct(PSDKActorDesc);

	if(!psdkActorDescShapeListType.notifyEmptyFunc){
		//psdkActorDescShapeListType.notifyEmptyFunc = psdkActorDescMeshNotifyListEmpty;
		//psdkActorDescShapeListType.notifyInvalidatedFunc = psdkActorDescMeshNotifyInvalidated;
		//psdkActorDescShapeListType.userPointerDestructor = psdkActorDescMeshInstanceDestructor;
		psdkActorDescShapeListType.flags.isPrimary = 1;
	}

	alCreate(	&actorDesc->alMeshes,
				actorDesc,
				&psdkActorDescShapeListType);

	copyMat4(unitmat, actorDesc->mat);
	
	actorDesc->density = 1.0f;

	return 1;
}

void psdkActorDescDestroy(PSDKActorDesc** actorDescInOut){
	PSDKActorDesc* actorDesc = SAFE_DEREF(actorDescInOut);

	if(actorDesc){
		PSDKShapeDesc* sd;
		PSDKShapeDesc* sdNext;
		
		alDestroy(&actorDesc->alMeshes);
		
		for(sd = actorDesc->sdHead.next; sd; sd = sdNext){
			sdNext = sd->next;
			
			psdkEnterCS();
			{
				MP_FREE(PSDKShapeDesc, sd);
			}
			psdkLeaveCS();
		}

		SAFE_FREE(*actorDescInOut);
	}
}

void psdkActorDescSetMat4(	PSDKActorDesc* actorDesc,
							const Mat4 mat)
{
	if(actorDesc){
		copyMat4(FIRST_IF_SET(mat, unitmat), actorDesc->mat);
	}
}

void psdkActorDescSetDensity(	PSDKActorDesc* actorDesc,
								F32 density)
{
	if(actorDesc){
		actorDesc->density = density;
	}
}

void psdkActorDescSetStartAsleep(	PSDKActorDesc* actorDesc,
									S32 startAsleep)
{
	if(actorDesc){
		actorDesc->startAsleep = !!startAsleep;
	}
}

static S32 psdkActorDescCreateShapeDesc(PSDKActorDesc* actorDesc,
										PSDKShapeDesc** sdOut,
										PSDKShapeDescType shapeDescType,											
										const Mat4 mat,
										F32 density,
										U32 materialIndex,
										U32 filterBits,
										U16 shapeGroup)
{
	PSDKShapeDesc* sd;
	
	if(actorDesc->sdTail){
		psdkEnterCS();
		{
			MP_CREATE(PSDKShapeDesc, 100);

			sd = *sdOut = MP_ALLOC(PSDKShapeDesc);
		}
		psdkLeaveCS();
		
		actorDesc->sdTail->next = *sdOut;
		actorDesc->sdTail = *sdOut;
	}else{
		sd = *sdOut = actorDesc->sdTail = &actorDesc->sdHead;
	}
	
	sd->shapeDescType = shapeDescType;
	sd->density = density;
	sd->materialIndex = materialIndex;
	sd->filterBits = filterBits;
	sd->shapeGroup = shapeGroup;

	copyMat4(FIRST_IF_SET(mat, unitmat), sd->mat);
	
	return 1;
}

S32 psdkActorDescAddMesh(	PSDKActorDesc* actorDesc,
							PSDKCookedMesh* mesh,
							const Mat4 mat,
							F32 density,
							U32 materialIndex,
							U32 filterBits,
							U16 shapeGroup,
							U32 shapeTag)
{
	PSDKShapeDesc* sd;

	if(	!mesh ||
		!psdkActorDescCreateShapeDesc(	actorDesc,
										&sd,
										PSDK_SD_COOKED_MESH,
										mat,
										density,
										materialIndex,
										filterBits,
										shapeGroup))
	{
		return 0;
	}

	sd->mesh = mesh;
	sd->shapeTag = shapeTag;

	alAssociate(NULL,
				actorDesc->alMeshes,
				mesh->alRefs,
				NULL);

	return 1;
}

S32 psdkActorDescAddCapsule(PSDKActorDesc* actorDesc,
							F32 yLengthEndToEnd,
							F32 radius,
							const Mat4 mat,
							F32 density,
							U32 materialIndex,
							U32 filterBits,
							U16 shapeGroup)
{
	PSDKShapeDesc* sd;

	if(!psdkActorDescCreateShapeDesc(	actorDesc,
										&sd,
										PSDK_SD_CAPSULE,
										mat,
										density,
										materialIndex,
										filterBits,
										shapeGroup))
	{
		return 0;
	}

	sd->capsule.length = yLengthEndToEnd;
	sd->capsule.radius = radius;

	copyMat4(FIRST_IF_SET(mat, unitmat), sd->mat);

	return 1;
}

S32 psdkActorDescAddSphere(	PSDKActorDesc* actorDesc,
							F32 radius,
							const Mat4 mat,
							F32 density,
							U32 materialIndex,
							U32 filterBits,
							U16 shapeGroup)
{
	PSDKShapeDesc* sd;

	if(!psdkActorDescCreateShapeDesc(	actorDesc,
										&sd,
										PSDK_SD_SPHERE,
										mat,
										density,
										materialIndex,
										filterBits,
										shapeGroup))
	{
		return 0;
	}

	sd->sphere.radius = radius;

	return 1;
}

S32 psdkActorDescAddBox(PSDKActorDesc* actorDesc,
						const Vec3 xyzHalfSize,
						const Mat4 mat,
						F32 density,
						U32 materialIndex,
						U32 filterBits,
						U16 shapeGroup)
{
	PSDKShapeDesc* sd;

	if(	!xyzHalfSize ||
		xyzHalfSize[0] <= 0.f ||
		xyzHalfSize[1] <= 0.f ||
		xyzHalfSize[2] <= 0.f ||
		!psdkActorDescCreateShapeDesc(	actorDesc,
										&sd,
										PSDK_SD_BOX,
										mat,
										density,
										materialIndex,
										filterBits,
										shapeGroup))
	{
		return 0;
	}

	copyVec3(xyzHalfSize, sd->box.xyzHalfSize);

	return 1;
}

S32 psdkActorCreateAndAddCCDSkeleton(	PSDKActor* actor,
										const Vec3 center)
{
	if(!SAFE_MEMBER(actor, nxActor)){
		return 0;
	}

	NxSimpleTriangleMesh	ccdMesh;
	NxCCDSkeleton*			ccdSkel;
	NxVec3					point;
	
	ccdMesh.setToDefault();
	ccdMesh.points = &point;
	ccdMesh.numVertices = 1;
	ccdMesh.pointStrideBytes = sizeof(point);

	ccdMesh.numTriangles = 0;

	ccdMesh.flags = 0;

	point.x = center[0];
	point.y = center[1];
	point.z = center[2];

	ccdSkel = nxSDK->createCCDSkeleton(ccdMesh);

	NxActor*		nxActor = actor->nxActor;
	S32				nxShapeCount = nxActor->getNbShapes();
	NxShape*const*	nxShapes = nxActor->getShapes();

	FOR_BEGIN(i, nxShapeCount);
	{
		NxShape* nxShape = nxShapes[i];
		nxShape->setCCDSkeleton(ccdSkel);
	}
	FOR_END;
	
	return 1;
}

MP_DEFINE(PSDKActor);

static AssociationListType psdkActorMeshListType;

static void psdkActorNotifyMeshNodeInvalidated(	void* voidActor,
												AssociationList* al,
												AssociationNode* node)
{
	psdkActorInvalidate((PSDKActor*)voidActor);
}

static PSDKActor* psdkActorAlloc(PSDKScene* scene){
	PSDKActor* actor;

	MP_CREATE(PSDKActor, 100);

	actor = MP_ALLOC(PSDKActor);

	//actor->scene = scene;

	if(!psdkActorMeshListType.notifyInvalidatedFunc){
		psdkActorMeshListType.notifyInvalidatedFunc = psdkActorNotifyMeshNodeInvalidated;
		psdkActorMeshListType.flags.isPrimary = 1;
	}

	alCreate(	&actor->alMeshes,
				actor,
				&psdkActorMeshListType);

	return actor;
}

static void psdkActorFree(PSDKActor* actor){
	MP_FREE(PSDKActor, actor);
}

#if PSDK_KEEP_ACTOR_LIST
	static StashTable stActors;
#endif

S32 psdkActorCreate(PSDKActor** actorOut,
					PSDKSimulationOwnership* so,
					const PSDKActorDesc* actorDesc,
					const PSDKBodyDesc* bodyDesc,
					PSDKScene* scene,
					void* userPointer,
					S32 isKinematic,
					S32 hasContactEvent,
					S32 hasOneWayCollision,
					S32 disableGravity)
{
	if(	!actorOut ||
		!actorDesc ||
		!scene ||
		!psdkCanModify(so))
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();
	
	PSDKActor*					actor = *actorOut = psdkActorAlloc(scene);
	NxActorDesc					nxActorDesc;
	const PSDKShapeDesc*		sd;
	
	#if PSDK_KEEP_ACTOR_LIST
	{
		if(!stActors){
			stActors = stashTableCreateAddress(1000);
		}
		
		if(!stashAddInt(stActors, actor, 0, false)){
			assert(0);
		}
	}
	#endif
	
	actor->userPointer = userPointer;

	for(sd = &actorDesc->sdHead; sd; sd = sd->next){
		NxShapeDesc*	nxShapeDesc = NULL;
		Mat4			shapeMat;
		
		copyMat4(sd->mat, shapeMat);
		
		switch(sd->shapeDescType){
			xcase PSDK_SD_CAPSULE:{
				NxCapsuleShapeDesc* nxDesc = new NxCapsuleShapeDesc;
				
				nxShapeDesc = nxDesc;
				nxDesc->radius = sd->capsule.radius;
				nxDesc->height = sd->capsule.length;
			}

			xcase PSDK_SD_SPHERE:{
				NxSphereShapeDesc* nxDesc = new NxSphereShapeDesc;
				
				nxShapeDesc = nxDesc;
				nxDesc->radius = sd->sphere.radius;
			}

			xcase PSDK_SD_BOX:{
				NxBoxShapeDesc* nxDesc = new NxBoxShapeDesc;
				
				nxShapeDesc = nxDesc;
				nxDesc->dimensions.x = sd->box.xyzHalfSize[0];
				nxDesc->dimensions.y = sd->box.xyzHalfSize[1];
				nxDesc->dimensions.z = sd->box.xyzHalfSize[2];
				
				#if 0
				{
					Vec3 pos;
					
					mulVecMat4(	instance->mat[3],
								actorDesc->mat,
								pos);
					
					printf(	"%8.8x: Creating box (%1.2f,%1.2f,%1.2f) at (%1.2f,%1.2f,%1.2f)\n",
							instance->shapeGroupBit,
							vecParamsXYZ(mesh->box.size),
							vecParamsXYZ(pos));
				}
				#endif
			}

			xcase PSDK_SD_COOKED_MESH:{
				PSDKCookedMesh* mesh = sd->mesh;

				switch(mesh->flags.meshType){
					xcase PSDK_CMT_TRIANGLE:{
						NxTriangleMeshShapeDesc* nxDesc = new NxTriangleMeshShapeDesc;
						
						nxShapeDesc = nxDesc;

						if(beaconIsBeaconizer()){
							nxDesc->meshFlags = NX_MESH_DOUBLE_SIDED;
						}

						nxDesc->meshData = psdkCookedMeshGetNxTriangleMesh(mesh);
					}

					xcase PSDK_CMT_CONVEX:{
						NxConvexShapeDesc* nxDesc = new NxConvexShapeDesc;

						nxShapeDesc = nxDesc;

						#ifdef NX_SUPPORT_CONVEX_SCALE
							if(bodyDesc){
								nxDesc->scale = bodyDesc->scale;
							}
						#endif

						nxDesc->meshData = mesh->nxConvexMesh;
					}

					xcase PSDK_CMT_HEIGHTFIELD:{
						NxHeightFieldShapeDesc* nxDesc = new NxHeightFieldShapeDesc;

						if(beaconIsBeaconizer()){
							nxDesc->meshFlags = NX_MESH_DOUBLE_SIDED;
						}

						nxShapeDesc = nxDesc;

						nxDesc->materialIndexHighBits = sd->materialIndex & ~0x7f;

						nxDesc->heightField = mesh->heightField.nxHeightField;

						nxDesc->rowScale =	mesh->heightField.worldSize[0] /
											(mesh->heightField.gridSize[0] - 1);

						nxDesc->columnScale =	mesh->heightField.worldSize[1] /
												(mesh->heightField.gridSize[1] - 1);

						nxDesc->heightScale =	(	mesh->heightField.maxHeight -
													mesh->heightField.minHeight) /
												(F32)mesh->heightField.maxIntHeight;

						if(nxDesc->heightScale <= 0.0f){
							nxDesc->heightScale = 1.0f;
						}

						nxDesc->holeMaterial = 0x7f;

						shapeMat[3][1] += mesh->heightField.minHeight;
					}

					xcase PSDK_CMT_SPHERE:{
						NxSphereShapeDesc* nxDesc = new NxSphereShapeDesc;
						
						nxShapeDesc = nxDesc;
						nxDesc->radius = mesh->sphere.radius;
					}

					xcase PSDK_CMT_CAPSULE:{
						NxCapsuleShapeDesc* nxDesc = new NxCapsuleShapeDesc;
						
						nxShapeDesc = nxDesc;
						nxDesc->radius = mesh->capsule.radius;
						nxDesc->height = mesh->capsule.height;
					}
					
					xcase PSDK_CMT_BOX:{
						NxBoxShapeDesc* nxDesc = new NxBoxShapeDesc;
						
						nxShapeDesc = nxDesc;
						nxDesc->dimensions.x = mesh->box.size[0];
						nxDesc->dimensions.y = mesh->box.size[1];
						nxDesc->dimensions.z = mesh->box.size[2];
					}
				}

				if(nxShapeDesc){
					// Point the nxShape to the mesh.
					
					nxShapeDesc->userData = mesh;

					// Association the mesh with the actor.

					alAssociate(NULL,
								actor->alMeshes,
								mesh->alRefs,
								NULL);
				}
			}
		}

		if(nxShapeDesc){
			// Set the local mat of the shape.

			setNxMat34FromMat4(	nxShapeDesc->localPose,
								shapeMat);
								
			// Enable visualization.
			
			if(isKinematic){
				nxShapeDesc->shapeFlags = NX_SF_VISUALIZATION;
			}
						
			// Set the shape group.
			
			nxShapeDesc->group = sd->shapeGroup;
			nxActorDesc.group = sd->shapeGroup;

			// utterly hijacking part of the group mask.  bleah
			nxShapeDesc->groupsMask.bits0 = sd->shapeTag;

			// real filtering.  We're only using 32 right now (what am I?)
			nxShapeDesc->groupsMask.bits1 = sd->filterBits;

			nxShapeDesc->groupsMask.bits2 = 0x0;

			// What do I want to collide against?: for debris and entities and ragdoll
			if (sd->shapeGroup >= WC_SHAPEGROUP_ENTITY && sd->shapeGroup <= WC_SHAPEGROUP_TEST_RAGDOLL_LIMB)
			{
				nxShapeDesc->groupsMask.bits3 = WC_QUERY_BITS_ENTITY_MOVEMENT | WC_FILTER_BIT_DEBRIS;
			}
			else
			{
				// I don't want to collide against anything that isn't trying to collide against me
				nxShapeDesc->groupsMask.bits3 = 0x0;
			}

			// Set the material index.

			nxShapeDesc->materialIndex = sd->materialIndex;
			
			// Set the density..

			if(bodyDesc){
				nxShapeDesc->density = sd->density;
			}
			
			// Add to the nx actor desc.
			
			nxActorDesc.shapes.pushBack(nxShapeDesc);
		}
	}
	
	// Set the mat of the actor.

	setNxMat34FromMat4(nxActorDesc.globalPose, actorDesc->mat);
	
	// Set the density of the actor.

	nxActorDesc.density = actorDesc->density;


	// Set the body of the actor.

	NxBodyDesc nxBodyDesc;

	if(hasContactEvent){
		actor->flags.hasContactEvent = 1;
	}

	if(bodyDesc){
		actor->flags.isDynamic = 1;

		nxActorDesc.body = &nxBodyDesc;

		if(isKinematic){
			actor->flags.isKinematic = 1;

			nxBodyDesc.flags |= NX_BF_KINEMATIC;
			//nxBodyDesc.flags |= NX_BF_FROZEN_POS;
		}else{
			nxBodyDesc.sleepEnergyThreshold = 0.1f;
		}

		//nxBodyDesc.mass				= 1.0f;
		//nxBodyDesc.massSpaceInertia.set(0, 1, 0);
		//nxActorDesc.density			= 0.0f;

		nxBodyDesc.linearVelocity.set(0, 0, 0);
		nxBodyDesc.angularVelocity.set(0, 0, 0);

		assert(nxBodyDesc.isValid());
	}

	// Create the actor.

	actor->nxActor = scene->nxScene->createActor(nxActorDesc);
	
	//printfColor(COLOR_BRIGHT|COLOR_GREEN,
	//			"Created actor %p in scene %p\n",
	//			actor->nxActor,
	//			scene->nxScene);

	if(!actor->nxActor){
		scene->actor.createFailedCount++;
		
		psdkActorDestroy(&actor, so);
		
		*actorOut = NULL;
	}else{
		if(psdkState.flags.printNewActorBounds){
			Vec3 boundsMin;
			Vec3 boundsMax;
			
			psdkActorGetBounds(actor, boundsMin, boundsMax);
			
			printf(	"Created actor with bounds (%1.4f, %1.4f, %1.4f) (%1.4f, %1.4f, %1.4f)\n",
					vecParamsXYZ(boundsMin),
					vecParamsXYZ(boundsMax));
		}

		// Set the userData on the nxActor.

		actor->nxActor->userData = actor;

		if(actorDesc->startAsleep){
			actor->nxActor->putToSleep();
		}

		scene->actor.createCount++;
		scene->actor.count++;
		
		if(psdkState.flags.printTestThing){
			if(!(scene->actor.count % 100)){
				U32 nxActorCount = scene->nxScene->getNbActors();
				
				printf(	"Added actor, now there are %d actors (%d PhysX).\n",
						scene->actor.count,
						nxActorCount);
			}
		}

		if(isKinematic){
			//actor->nxActor->raiseBodyFlag(NX_BF_DISABLE_GRAVITY);
			//actor->nxActor->raiseBodyFlag(NX_BF_KINEMATIC);
			//actor->nxActor->raiseActorFlag(NX_AF_DISABLE_RESPONSE);
		}

		if (disableGravity) {
			actor->nxActor->raiseBodyFlag(NX_BF_DISABLE_GRAVITY);
		}
	}

	if (hasOneWayCollision){
		actor->flags.hasOneWayCollision = true;
	}
	// Free the shape description array.

	while(nxActorDesc.shapes.size()){
		NxShapeDesc* nxShapeDesc = nxActorDesc.shapes.back();

		nxActorDesc.shapes.popBack();

		SAFE_DELETE(nxShapeDesc);
	}
	
	PERFINFO_AUTO_STOP();

	return !!actor;
}

S32 psdkActorDestroy(	PSDKActor** actorInOut,
						PSDKSimulationOwnership* so)
{
	PSDKActor* actor = SAFE_DEREF(actorInOut);

	if(!actor){
		return 0;
	}

	*actorInOut = NULL;

	if(!psdkCanModify(so)){
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	psdkEnterCS();
	{
		#if PSDK_KEEP_ACTOR_LIST
		{
			if(!stashRemoveInt(stActors, actor, NULL)){
				assert(0);
			}
		}
		#endif

		if(actor->nxActor){
			NxScene&	nxScene = actor->nxActor->getScene();
			PSDKScene*	scene = (PSDKScene*)nxScene.userData;
			
			actor->nxActor->userData = NULL;
			nxScene.releaseActor(*actor->nxActor);
			
			scene->actor.destroyCount++;
			
			actor->nxActor = NULL;
			
			assert(scene->actor.count);
			scene->actor.count--;

			if(psdkState.flags.printTestThing){
				if(!(scene->actor.count % 100)){
					U32 nxActorCount = scene->nxScene->getNbActors();

					printf(	"Removed actor, now there are %d actors (%d PhysX).\n",
							scene->actor.count,
							nxActorCount);
				}
			}

			//printfColor(COLOR_BRIGHT|COLOR_RED,
			//			"%d actors after release\n",
			//			nxScene.getNbActors());
		}

		alDestroy(&actor->alMeshes);
		
		actor->flags.destroyed = 1;

		psdkActorFree(actor);
	}
	psdkLeaveCS();
	
	PERFINFO_AUTO_STOP();

	return 1;
}

S32 psdkActorCopy(	PSDKScene* scene,
					PSDKActor** actorOut,
					void* userPointer,
					PSDKActor* actorToCopy,
					const Vec3 sceneOffset)
{
	const S32					nxShapeCount = actorToCopy->nxActor->getNbShapes();
	const NxShape*const*		nxShapes = actorToCopy->nxActor->getShapes();
	NxActorDesc					nxActorDesc;
	PSDKActor*					actor = *actorOut = psdkActorAlloc(scene);
	
	PERFINFO_AUTO_START_FUNC();
	
	#if PSDK_KEEP_ACTOR_LIST
	{
		if(!stActors){
			stActors = stashTableCreateAddress(1000);
		}
		
		if(!stashAddInt(stActors, actor, 0, false)){
			assert(0);
		}
	}
	#endif

	actor->userPointer = userPointer;

	FOR_BEGIN(i, nxShapeCount);
	{
		const NxShape*	nxShape = nxShapes[i];
		NxShapeDesc*	nxShapeDesc = NULL;
		PSDKCookedMesh*	mesh = (PSDKCookedMesh*)nxShape->userData;
		
		switch(nxShape->getType()){
			xcase NX_SHAPE_MESH:{
				NxTriangleMeshShapeDesc* nxDesc = new NxTriangleMeshShapeDesc;

				nxShapeDesc = nxDesc;

				nxDesc->meshData = psdkCookedMeshGetNxTriangleMesh(mesh);
			}
			
			xcase NX_SHAPE_CONVEX:{
				NxConvexShapeDesc* nxDesc = new NxConvexShapeDesc;

				nxShapeDesc = nxDesc;

				//#ifdef NX_SUPPORT_CONVEX_SCALE
				//	if(bodyDesc){
				//		nxDesc->scale = bodyDesc->scale;
				//	}
				//#endif

				nxDesc->meshData = mesh->nxConvexMesh;
			}
		}
		
		if(!nxShapeDesc){
			continue;
		}

		// Point the nxShape to the mesh.
		
		nxShapeDesc->userData = mesh;

		// Set the local mat of the shape.

		nxShapeDesc->localPose = nxShape->getLocalPose();
							
		// Set the shape group.
		
		nxShapeDesc->group = nxShape->getGroup();
		nxActorDesc.group = nxShape->getGroup();

		nxShapeDesc->groupsMask = nxShape->getGroupsMask();
		
		// Set the material index.

		nxShapeDesc->materialIndex = nxShape->getMaterial();
		
		nxActorDesc.shapes.pushBack(nxShapeDesc);

		// Association the mesh with the actor.

		alAssociate(NULL,
					actor->alMeshes,
					mesh->alRefs,
					NULL);
	}
	FOR_END;
	
	if(!alIsEmpty(actor->alMeshes)){
		// Set the mat of the actor.

		nxActorDesc.globalPose = actorToCopy->nxActor->getGlobalPose();
		
		if(sceneOffset){
			subVec3(nxActorDesc.globalPose.t,
					sceneOffset,
					nxActorDesc.globalPose.t);
		}
		
		// Set the density of the actor.

		nxActorDesc.density = 1;

		// Create the actor.

		actor->nxActor = scene->nxScene->createActor(nxActorDesc);
		
		// Set the userData on the nxActor.

		if(!actor->nxActor){
			scene->actor.createFailedCount++;
		}else{
			actor->nxActor->userData = actor;

			scene->actor.createCount++;
			scene->actor.count++;
		}

		// Free the shape description array.

		while(nxActorDesc.shapes.size()){
			NxShapeDesc* nxShapeDesc = nxActorDesc.shapes.back();

			nxActorDesc.shapes.popBack();

			SAFE_DELETE(nxShapeDesc);
		}
	}
	
	PERFINFO_AUTO_STOP();

	return 1;
}

void psdkActorInvalidate(PSDKActor* actor){
	if(SAFE_MEMBER(actor, nxActor)){
		PSDKScene* scene = (PSDKScene*)actor->nxActor->getScene().userData;

		actor->flags.geoInvalidated = 1;

		if(SAFE_MEMBER(scene, geoInvalidatedFunc)){
			scene->geoInvalidatedFunc(	scene,
										scene->userPointer,
										actor,
										actor->userPointer);
		}
	}
}

S32 psdkActorGetIgnore(PSDKActor* actor){
	return SAFE_MEMBER(actor, flags.ignoreForChar);
}

S32	psdkActorSetIgnore(	PSDKActor* actor,
						S32 ignore)
{
	if(!actor){
		return 0;
	}

	actor->flags.ignoreForChar = !!ignore;

	return 1;
}

S32	psdkNxActorGetBounds(	void* nxActorVoid,
							Vec3 boundsMinOut,
							Vec3 boundsMaxOut)
{
	S32						nxShapeCount;
	NxActor*				nxActor = (NxActor*)nxActorVoid;
	const NxShape*const*	nxShapes;

	if( !boundsMinOut ||
		!boundsMaxOut)
	{
		return 0;
	}

	nxShapeCount = nxActor->getNbShapes();
	nxShapes = nxActor->getShapes();

	FOR_BEGIN(i, nxShapeCount);
	{
		Vec3		vecMin;
		Vec3		vecMax;
		NxBounds3	bounds;

		nxShapes[i]->getWorldBounds(bounds);
		vec3FromNxVec3(vecMin, bounds.min);
		vec3FromNxVec3(vecMax, bounds.max);

		if(!i){
			copyVec3(vecMin, boundsMinOut);
			copyVec3(vecMax, boundsMaxOut);
		}else{
			vec3RunningMinMax(vecMin, boundsMinOut, boundsMaxOut);
			vec3RunningMinMax(vecMax, boundsMinOut, boundsMaxOut);
		}
	}
	FOR_END;

	return 1;
}

S32	psdkActorGetBounds(	PSDKActor* actor,
						Vec3 boundsMinOut,
						Vec3 boundsMaxOut)
{
	if(	!boundsMinOut ||
		!boundsMaxOut ||
		!SAFE_MEMBER(actor, nxActor))
	{
		return 0;
	}

	return psdkNxActorGetBounds(actor->nxActor, boundsMinOut, boundsMaxOut);
}

S32 psdkActorGetPos(PSDKActor* actor,
					Vec3 posOut)
{
	if(	!posOut ||
		!SAFE_MEMBER(actor, nxActor))
	{
		return 0;
	}

	actor->nxActor->getGlobalPosition().get(posOut);

	return 1;
}

S32 psdkActorGetMat(PSDKActor* actor,
					Mat4 matOut)
{
	if(	!matOut ||
		!SAFE_MEMBER(actor, nxActor))
	{
		return 0;
	}

	actor->nxActor->getGlobalPosition().get(matOut[3]);

	actor->nxActor->getGlobalOrientation().getColumnMajor(matOut[0]);

	return 1;
}

void* psdkActorGetNxActor(PSDKActor* actor)
{
	return actor->nxActor;
}

S32 psdkActorGetVels(	PSDKActor* actor,
						Vec3 velOut,
						Vec3 angVelOut)
{
	if(	!velOut ||
		!SAFE_MEMBER(actor, nxActor))
	{
		return 0;
	}

	if(actor->nxActor->isSleeping()){
		if(velOut){
			zeroVec3(velOut);
		}
		
		if(angVelOut){
			zeroVec3(angVelOut);
		}
	}else{
		if(velOut){
			actor->nxActor->getLinearVelocity().get(velOut);
		}
		
		if(angVelOut){
			actor->nxActor->getAngularVelocity().get(angVelOut);
		}
	}

	return 1;
}

S32 psdkActorSetVels(	PSDKActor* actor,
						const Vec3 vel,
						const Vec3 angVel)
{
	NxActor* nxActor = SAFE_MEMBER(actor, nxActor);
	
	if(nxActor){
		assert(!actor->flags.destroyed);
		
		psdkEnterCS();
		{
			if(vel){
				//printf("setting linear velocity: %p %f %f %f\n", actor, vecParamsXYZ(vel));
				CHECK_FINITEVEC3(vel);
				nxActor->setLinearVelocity(vel);
			}
			
			if(angVel){
				//printf("setting angular velocity: %p %f %f %f\n", actor, vecParamsXYZ(angVel));
				CHECK_FINITEVEC3(angVel);
				nxActor->setAngularVelocity(angVel);
			}
		}
		psdkLeaveCS();
		
		return 1;
	}
	
	return 0;
}

S32 psdkActorAddVel(PSDKActor* actor,
					const Vec3 vel)
{
	NxActor* nxActor = SAFE_MEMBER(actor, nxActor);
	CHECK_FINITEVEC3(vel);
	
	if(nxActor){
		assert(!actor->flags.destroyed);

		NxVec3 oldVel = nxActor->getLinearVelocity();
		
		psdkEnterCS();
		{
			//printf("setting linear velocity: %p %f %f %f\n", actor, vecParamsXYZ(vel));
			nxActor->setLinearVelocity(oldVel + NxVec3(vel));
		}
		psdkLeaveCS();
		
		return 1;
	}
	
	return 0;
}

S32 psdkActorScaleVel(	PSDKActor* actor,
	const F32 scale)
{
	NxActor* nxActor = SAFE_MEMBER(actor, nxActor);

	if(nxActor){
		assert(!actor->flags.destroyed);

		NxVec3 oldVel = nxActor->getLinearVelocity();

		psdkEnterCS();
		{
			//printf("scaling linear velocity: %p %f*(%f %f %f)\n", actor, scale, vecParamsXYZ(vel));
			nxActor->setLinearVelocity(oldVel * scale);
		}
		psdkLeaveCS();

		return 1;
	}

	return 0;
}

S32 psdkActorSetLinearDamping(PSDKActor* actor,
							  float damping)
{
	NxActor* nxActor = SAFE_MEMBER(actor, nxActor);

	if(nxActor){
		assert(!actor->flags.destroyed);

		NxVec3 oldVel = nxActor->getLinearVelocity();

		psdkEnterCS();
		{
			nxActor->setLinearDamping(damping);
		}
		psdkLeaveCS();

		return 1;
	}

	return 0;
}

static S32 actorWantsToSleep(const NxActor* nxActor){
	const F32 fSleepLinVel = nxActor->getSleepLinearVelocity();
	const F32 fSleepAngVel = nxActor->getSleepAngularVelocity();

	return	nxActor->getLinearVelocity().magnitudeSquared() < SQR(fSleepLinVel) &&
			nxActor->getAngularVelocity().magnitudeSquared() < SQR(fSleepAngVel);
}

S32 psdkActorAddForce(PSDKActor* actor,
					  const Vec3 force,
					  S32 isAccelerationNotForce,
					  S32 shouldWakeup)
{
	NxActor* nxActor = SAFE_MEMBER(actor, nxActor);
	
	CHECK_FINITEVEC3(force);
	
	if(nxActor){
		assert(!actor->flags.destroyed);

		// Only apply the force if it's not going to cause a wake up.
		// This must be done because this is often called every frame and will never let
		// an object sleep in PhysX's crazy world

		if(	shouldWakeup ||
			!actorWantsToSleep(nxActor))
		{
			psdkEnterCS();
			{
				//printf("adding force: %p %f %f %f\n", actor, vecParamsXYZ(force));
				nxActor->addForce(	NxVec3(force),
									isAccelerationNotForce ?
										NX_ACCELERATION :
										NX_FORCE,
									!!shouldWakeup);
			}
			psdkLeaveCS();
		}
		
		return 1;
	}
	
	return 0;
}

S32 psdkActorSetMat(PSDKActor* actor,
					const Mat4 mat)
{
	NxActor*	nxActor = SAFE_MEMBER(actor, nxActor);
	NxMat34		nxMat;
	
	CHECK_FINITEVEC3(mat[0]);
	CHECK_FINITEVEC3(mat[1]);
	CHECK_FINITEVEC3(mat[2]);
	CHECK_FINITEVEC3(mat[3]);
	
	setNxMat34FromMat4(nxMat, mat);
	
	if(nxActor){
		assert(!actor->flags.destroyed);
		
		psdkEnterCS();
		{
			//printf("setting matrix: %p\n", actor);
			nxActor->setGlobalPose(nxMat);
		}
		psdkLeaveCS();
		
		return 1;
	}
	
	return 0;
}

S32 psdkActorWakeUp(PSDKActor* actor)
{
	NxActor*	nxActor = SAFE_MEMBER(actor, nxActor);

	if(nxActor){
		assert(!actor->flags.destroyed);

		psdkEnterCS();
		{
			nxActor->wakeUp();
		}
		psdkLeaveCS();

		return 1;
	}

	return 0;
}

S32	psdkActorSetRagdollInitialValues(	PSDKActor *actor,
										Vec3 vPos,
										Quat qRot,
										F32 fSkinWidth,
										F32 fLinearSleepVelocity,
										F32 fAngularSleepVelocity,
										F32 fMaxAngularVelocity,
										U32 uiSolverIterationCount,
										U16 uiCollisionGroup)
{
	NxActor *nxActor = SAFE_MEMBER(actor, nxActor);
	NxQuat nxquat;

	CHECK_FINITEVEC3(pos);
	CHECK_FINITEQUAT(rot);

	// Note the negative on the W, since physx is using a different coordinate system
	nxquat.setXYZW(quatX(qRot), quatY(qRot), quatZ(qRot), -quatW(qRot));

	if (nxActor) {
		psdkEnterCS();
		{
			NxShape *const *pShapes = nxActor->getShapes();
			U32 numShapes = nxActor->getNbShapes();

			nxActor->setGlobalPosition(vPos);
			nxActor->setGlobalOrientationQuat(nxquat);
			nxActor->clearBodyFlag(NX_BF_ENERGY_SLEEP_TEST);
			nxActor->raiseBodyFlag(NX_BF_FILTER_SLEEP_VEL);
			nxActor->setSleepLinearVelocity(fLinearSleepVelocity);
			nxActor->setSleepAngularVelocity(fAngularSleepVelocity);
			nxActor->setMaxAngularVelocity(fMaxAngularVelocity);
			nxActor->setSolverIterationCount(uiSolverIterationCount);

			for (U32 i = 0; i < numShapes; i++) {
				pShapes[i]->setSkinWidth(fSkinWidth);
				pShapes[i]->setGroup(uiCollisionGroup);
			}
		}
		psdkLeaveCS();
		return 1;
	}

	return 0;
}

S32 psdkActorSetSkinWidth(PSDKActor *actor, F32 skinWidth)
{
	NxActor *nxActor = SAFE_MEMBER(actor, nxActor);

	if (nxActor) {
		psdkEnterCS();
		{
			NxShape *const *pShapes = nxActor->getShapes();
			U32 numShapes = nxActor->getNbShapes();
			for (U32 i = 0; i < numShapes; i++) {
				pShapes[i]->setSkinWidth(skinWidth);
			}
		}
		psdkLeaveCS();
		return 1;
	}

	return 0;
}

S32 psdkActorSetCollisionGroup(PSDKActor *actor, U16 collisionGroup)
{
	NxActor *nxActor = SAFE_MEMBER(actor, nxActor);

	if (nxActor) {
		psdkEnterCS();
		{
			NxShape *const *pShapes = nxActor->getShapes();
			U32 numShapes = nxActor->getNbShapes();
			for (U32 i = 0; i < numShapes; i++) {
				pShapes[i]->setGroup(collisionGroup);
			}
		}
		psdkLeaveCS();
		return 1;
	}

	return 0;
}

S32 psdkActorSetPos(PSDKActor* actor,
					const Vec3 pos)
{
	NxActor* nxActor = SAFE_MEMBER(actor, nxActor);
	CHECK_FINITEVEC3(pos);
	
	if(nxActor){
		assert(!actor->flags.destroyed);

		psdkEnterCS();
		{
			//printf("setting position: %x %f %f %f\n", actor, vecParamsXYZ(pos));
			nxActor->setGlobalPosition(pos);
		}
		psdkLeaveCS();
		
		return 1;
	}
	
	return 0;
}

S32 psdkActorAddAngVel(	PSDKActor* actor,
						const Vec3 angVel)
{
	NxActor* nxActor = SAFE_MEMBER(actor, nxActor);
	CHECK_FINITEVEC3(angVel);
	
	if(nxActor){
		NxVec3 oldVel = nxActor->getAngularVelocity();
		
		psdkEnterCS();
		{
			//printf("setting angular velocity: %p %f %f %f\n", actor, vecParamsXYZ(angVel));
			nxActor->setAngularVelocity(oldVel + NxVec3(angVel));
		}
		psdkLeaveCS();
		
		return 1;
	}
	
	return 0;
}

S32 psdkActorSetMaxAngularVel(	PSDKActor* actor,
								F32 maxAngVel)
{
	NxActor* nxActor = SAFE_MEMBER(actor, nxActor);
	
	if(nxActor){
		psdkEnterCS();
		{
			nxActor->setMaxAngularVelocity(maxAngVel);
		}
		psdkLeaveCS();
		
		return 1;
	}
	
	return 0;
}

S32 psdkActorSetAngularDamping(	PSDKActor *actor,
								F32 angDamping)
{
	NxActor *nxActor = SAFE_MEMBER(actor, nxActor);

	if (nxActor) {
		psdkEnterCS();
		{
			nxActor->setAngularDamping(angDamping);
		}
		psdkLeaveCS();

		return 1;
	}

	return 0;
}

S32 psdkActorSetSleepVelocities(PSDKActor *actor,
								F32 linearVelocity,
								F32 angularVelocity)
{
	NxActor *nxActor = SAFE_MEMBER(actor, nxActor);

	if (nxActor) {
		psdkEnterCS();
		{
			nxActor->clearBodyFlag(NX_BF_ENERGY_SLEEP_TEST);
			nxActor->raiseBodyFlag(NX_BF_FILTER_SLEEP_VEL);
			nxActor->setSleepLinearVelocity(linearVelocity);
			nxActor->setSleepAngularVelocity(angularVelocity);
		}
		psdkLeaveCS();
		return 1;
	}

	return 0;
}

U32 psdkActorIsSleeping(PSDKActor *actor)
{
	NxActor *nxActor = SAFE_MEMBER(actor, nxActor);

	if (nxActor)
	{
		return nxActor->isSleeping();
	}

	return 0;
}

F32 psdkActorGetKineticEnergy(PSDKActor *actor)
{
	NxActor *nxActor = SAFE_MEMBER(actor, nxActor);
	
	if (nxActor)
	{
		return nxActor->computeKineticEnergy();
	}

	return 0.0f;
}

F32 psdkActorGetLinearVelocity(PSDKActor *actor)
{
	NxActor *nxActor = SAFE_MEMBER(actor, nxActor);

	if (nxActor)
	{
		NxVec3 lv = nxActor->getLinearVelocity();
		return sqrtf(lv.x*lv.x + lv.y*lv.y + lv.z*lv.z);
	}

	return 0.0f;
}

void psdkActorGetLinearVelocityFull(PSDKActor *actor, Vec3 vVel)
{
	NxActor *nxActor = SAFE_MEMBER(actor, nxActor);

	if (nxActor)
	{
		NxVec3 lv = nxActor->getLinearVelocity();
		vVel[0] = lv[0];
		vVel[1] = lv[1];
		vVel[2] = lv[2];
	}

	zeroVec3(vVel);
}

F32 psdkActorGetAngularVelocity(PSDKActor *actor)
{
	NxActor *nxActor = SAFE_MEMBER(actor, nxActor);

	if (nxActor)
	{
		NxVec3 av = nxActor->getAngularVelocity();
		return sqrtf(av.x*av.x + av.y*av.y + av.z*av.z);
	}

	return 0.0f;
}

S32 psdkActorSetSolverIterationCount(	PSDKActor* actor,
										U32 count )
{
	NxActor* nxActor = SAFE_MEMBER(actor, nxActor);
	
	if(nxActor){
		psdkEnterCS();
		{
			nxActor->setSolverIterationCount(count);
		}
		psdkLeaveCS();
		
		return 1;
	}
	
	return 0;
}

S32 psdkActorSetRot(PSDKActor* actor,
					const Quat rot)
{
	NxActor* nxActor = SAFE_MEMBER(actor, nxActor);
	NxQuat nxquat;
	CHECK_FINITEQUAT(rot);

	// Note the negative on the W, since physx is using a different coordinate system
	nxquat.setXYZW(quatX(rot), quatY(rot), quatZ(rot), -quatW(rot));
	
	if(nxActor){
		psdkEnterCS();
		{
			//printf("setting rotation: %p\n", actor);
			nxActor->setGlobalOrientationQuat(nxquat);
		}
		psdkLeaveCS();
		
		return 1;
	}
	
	return 0;
}

S32 psdkActorOverlapCapsule(PSDKActor* actor, 
							const Vec3 worldPos,
							const Capsule* cap)
{
	U32						shapeCount;
	const NxShape*const*	shapes;
	NxActor*				nxActor = SAFE_MEMBER(actor, nxActor);
	NxCapsule				nxCap;

	if(!nxActor){
		return 0;
	}

	psdkNxCapsuleFromCapsule(nxCap, worldPos, cap);

	shapeCount = nxActor->getNbShapes();
	shapes = nxActor->getShapes();

	FOR_BEGIN(i, (S32)shapeCount);
	{
		const NxShape* shape = shapes[i];

		if(shape->checkOverlapCapsule(nxCap)){
			return 1;
		}
	}
	FOR_END;

	return 0;
}

U32	psdkActorGetShapeCount(PSDKActor* actor){
	NxActor* nxActor = SAFE_MEMBER(actor, nxActor);

	if(!nxActor){
		return 0;
	}

	return nxActor->getNbShapes();
}

S32 psdkActorGetShapesArray(PSDKActor* actor,
							const PSDKShape*const** shapesOut)
{
	NxActor* nxActor = SAFE_MEMBER(actor, nxActor);

	if(!nxActor){
		return 0;
	}

	if(shapesOut){
		*shapesOut = (const PSDKShape*const*)nxActor->getShapes();
	}

	return 1;
}

S32	psdkActorGetShapeByIndex(	PSDKActor* actor,
								U32 index,
								const PSDKShape** shapeOut)
{
	U32			shapeCount;
	NxActor*	nxActor = SAFE_MEMBER(actor, nxActor);

	if(!nxActor){
		return 0;
	}

	shapeCount = nxActor->getNbShapes();

	if(index >= shapeCount){
		return 0;
	}
	
	if(shapeOut){
		*shapeOut = (const PSDKShape*)nxActor->getShapes()[index];
	}

	return 1;
}

S32 psdkJointCreate(const PSDKJointDesc* jd,
					S32 applyCustomJointAxis,
					S32 applyVolumeScale)
{
	if(	!jd->actor0 )
	{
		return 0;
	}
	
	NxScene&		nxScene = jd->actor0->nxActor->getScene();
	PSDKJointType	jointType = PSDK_JT_SPHERICAL;
	NxMat33			parentOrientation = jd->actor0->nxActor->getGlobalOrientation();
	NxMat33			childOrientation = jd->actor1 ? jd->actor1->nxActor->getGlobalOrientation() : NxMat33();
	NxMat33			mUnitMat;

	if(jd->actor1 && &nxScene != &jd->actor1->nxActor->getScene()){
		return 0;
	}

	mUnitMat.id();

	jd->actor0->nxActor->setGlobalOrientation(mUnitMat);
	if(jd->actor1) jd->actor1->nxActor->setGlobalOrientation(mUnitMat);

	if(jd->tuning)
	{
		switch (jd->tuning->jointType)
		{
			xcase eJointType_None:
			{
				// Not currently supported
				jointType = PSDK_JT_SPHERICAL;
			}
			xcase eJointType_Revolute:
			{
				jointType = PSDK_JT_REVOLUTE;
			}
			xcase eJointType_Spherical:
			{
				jointType = PSDK_JT_SPHERICAL;
			}
			xcase eJointType_D6:
			{
				jointType = PSDK_JT_D6;
			}
		}
	}

	switch(jointType){
		xcase PSDK_JT_D6:{
			NxD6JointDesc nxJD;
			jd->actor0->nxActor->setGlobalOrientation(parentOrientation);
			if(jd->actor1) jd->actor1->nxActor->setGlobalOrientation(childOrientation);
		}
		
		xcase PSDK_JT_REVOLUTE:{
			NxRevoluteJointDesc nxJD;
			
			//nxJD.jointFlags |= NX_JF_COLLISION_ENABLED;
			
			nxJD.actor[0] = jd->actor0->nxActor;
			if(jd->actor1){
				nxJD.actor[1] = jd->actor1->nxActor;
			}else{
				nxJD.actor[1] = NULL;
			}

			if(jd->tuning){
				nxJD.setGlobalAxis(applyCustomJointAxis ? NxVec3(jd->globalAxis) : mUnitMat.getColumn(jd->tuning->axis));

				if(jd->tuning->limitEnabled){
					nxJD.flags |= NX_RJF_LIMIT_ENABLED;
					nxJD.limit.low.value = RAD(jd->tuning->limitLow.value);
					nxJD.limit.low.hardness = jd->tuning->limitLow.hardness;
					nxJD.limit.low.restitution = jd->tuning->limitLow.restitution;

					nxJD.limit.high.value = RAD(jd->tuning->limitHigh.value);
					nxJD.limit.high.hardness = jd->tuning->limitHigh.hardness;
					nxJD.limit.high.restitution = jd->tuning->limitHigh.restitution;
				}

				if(jd->tuning->springEnabled){
					nxJD.flags |= NX_RJF_SPRING_ENABLED;
					nxJD.spring.spring = applyVolumeScale ? jd->volumeScale*jd->tuning->spring.spring : jd->tuning->spring.spring;
					nxJD.spring.damper = applyVolumeScale ? jd->volumeScale*jd->tuning->spring.damper : jd->tuning->spring.damper;
				}
			}
			
			copyVec3(	jd->anchor0,
						nxJD.localAnchor[0]);
			
			copyVec3(	jd->anchor1,
						nxJD.localAnchor[1]);

			jd->actor0->nxActor->setGlobalOrientation(parentOrientation);
			if(jd->actor1) jd->actor1->nxActor->setGlobalOrientation(childOrientation);

			psdkEnterCS();
			{
				nxScene.createJoint(nxJD);
			}
			psdkLeaveCS();
		}

		xcase PSDK_JT_SPHERICAL:{
			NxSphericalJointDesc nxJD;
			
			//nxJD.jointFlags |= NX_JF_COLLISION_ENABLED;
			
			nxJD.actor[0] = jd->actor0->nxActor;
			if(jd->actor1) nxJD.actor[1] = jd->actor1->nxActor;
			
			copyVec3(	jd->anchor0,
						nxJD.localAnchor[0]);
			
			copyVec3(	jd->anchor1,
						nxJD.localAnchor[1]);

			if(jd->tuning){
				nxJD.setGlobalAxis(applyCustomJointAxis ? NxVec3(jd->globalAxis) : parentOrientation.getColumn(jd->tuning->axis));

				if(jd->tuning->swingLimitEnabled){
					nxJD.flags |= NX_SJF_SWING_LIMIT_ENABLED;
					nxJD.swingLimit.value = RAD(jd->tuning->swingLimit.value);
					nxJD.swingLimit.hardness = jd->tuning->swingLimit.hardness;
					nxJD.swingLimit.restitution = jd->tuning->swingLimit.restitution;
				}

				if(jd->tuning->swingSpringEnabled){
					nxJD.flags |= NX_SJF_SWING_SPRING_ENABLED;
					nxJD.swingSpring.spring = applyVolumeScale ? jd->volumeScale*jd->tuning->swingSpring.spring : jd->tuning->swingSpring.spring;
					nxJD.swingSpring.damper = applyVolumeScale ? jd->volumeScale*jd->tuning->swingSpring.damper : jd->tuning->swingSpring.damper;
				}

				if(jd->tuning->limitEnabled){
					nxJD.flags |= NX_SJF_TWIST_LIMIT_ENABLED;
					nxJD.twistLimit.low.value = RAD(jd->tuning->limitLow.value);
					nxJD.twistLimit.low.hardness = jd->tuning->limitLow.hardness;
					nxJD.twistLimit.low.restitution = jd->tuning->limitLow.restitution;

					nxJD.twistLimit.high.value = RAD(jd->tuning->limitHigh.value);
					nxJD.twistLimit.high.hardness = jd->tuning->limitHigh.hardness;
					nxJD.twistLimit.high.restitution = jd->tuning->limitHigh.restitution;
				}

				if(jd->tuning->springEnabled)
				{
					nxJD.flags |= NX_SJF_TWIST_SPRING_ENABLED;
					nxJD.twistSpring.spring = applyVolumeScale ? jd->volumeScale*jd->tuning->spring.spring : jd->tuning->spring.spring;
					nxJD.twistSpring.damper = applyVolumeScale ? jd->volumeScale*jd->tuning->spring.damper : jd->tuning->spring.damper;
				}
			}

			jd->actor0->nxActor->setGlobalOrientation(parentOrientation);
			if(jd->actor1) jd->actor1->nxActor->setGlobalOrientation(childOrientation);

			psdkEnterCS();
			{
				nxScene.createJoint(nxJD);
			}
			psdkLeaveCS();
		}
	}


	
	return 1;
}

S32 psdkActorIsDynamic(PSDKActor* actor){
	return SAFE_MEMBER(actor, flags.isDynamic);
}

S32 psdkActorIsKinematic(PSDKActor* actor){
	return SAFE_MEMBER(actor, flags.isKinematic);
}

S32	psdkActorMove(	PSDKActor* actor,
					const Vec3 pos)
{
	NxActor* nxActor = SAFE_MEMBER(actor, nxActor);

	if(	nxActor &&
		pos)
	{
		CHECK_FINITEVEC3(pos);
		
		NxVec3 nxPos(pos);

		psdkEnterCS();
		{
			//printf("moving: %p %f %f %f\n", actor, vecParamsXYZ(pos));
			nxActor->moveGlobalPosition(nxPos);
		}
		psdkLeaveCS();

		return 1;
	}

	return 0;
}

S32	psdkActorRotate(	PSDKActor* actor,
						const Quat rot)
{
	NxActor* nxActor = SAFE_MEMBER(actor, nxActor);

	if(	nxActor &&
		rot)
	{
		CHECK_FINITEQUAT(rot);
		
		NxQuat nxquat;

		// Note the negative on the W, since physx is using a different coordinate system
		nxquat.setXYZW(quatX(rot), quatY(rot), quatZ(rot), -quatW(rot));


		psdkEnterCS();
		{
			//printf("rotating: %p\n", actor);
			nxActor->moveGlobalOrientationQuat(nxquat);
		}
		psdkLeaveCS();

		return 1;
	}

	return 0;
}

S32	psdkActorMoveMat(PSDKActor* actor,
					 const Mat4 mat)
{
	NxActor* nxActor = SAFE_MEMBER(actor, nxActor);
	
	PERFINFO_AUTO_START_FUNC();

	CHECK_FINITEVEC3(mat[0]);
	CHECK_FINITEVEC3(mat[1]);
	CHECK_FINITEVEC3(mat[2]);
	CHECK_FINITEVEC3(mat[3]);

	if(nxActor)
	{
		NxMat34 nxmat;

		nxmat.M.setRowMajorStride4((F32*)mat);
		nxmat.t.set(mat[3]);

		psdkEnterCS();
		{
			//printf("moving global pose: %p\n", actor);
			nxActor->moveGlobalPose(nxmat);
		}
		psdkLeaveCS();
		
		PERFINFO_AUTO_STOP();

		return 1;
	}

	PERFINFO_AUTO_STOP();

	return 0;
}

S32 psdkActorGetUserPointer(void** userPointerOut,
							const PSDKActor* actor)
{
	if(	!actor ||
		!userPointerOut)
	{
		return 0;
	}

	*userPointerOut = actor->userPointer;

	return 1;
}

S32 psdkActorSetUserPointer(PSDKActor* actor,
							void* userPointer)
{
	if(!actor){
		return 0;
	}

	actor->userPointer = userPointer;

	return 1;
}

S32 psdkActorGetFilterBits(	PSDKActor* actor,
								U32* filterBitsOut)
{
	if(	!actor ||
		!filterBitsOut)
	{
		return 0;
	}

	const NxShape*const*	nxShapes = actor->nxActor->getShapes();
	const S32				nxShapeCount = actor->nxActor->getNbShapes();
	
	*filterBitsOut = 0;

	FOR_BEGIN(i, nxShapeCount);
	{
		const NxShape* nxShape = nxShapes[i];

		*filterBitsOut |= nxShape->getGroupsMask().bits1;
	}
	FOR_END;

	return 1;
}

U16	psdkActorGetShapeGroup(	PSDKActor* actor)
{
	devassert(actor);

	return actor->nxActor->getGroup();
}

bool    psdkActorHasOneWayCollision(PSDKActor* actor)
{
	devassert(actor);

	return actor->flags.hasOneWayCollision;
}

S32 psdkActorSetCollidable(	PSDKActor* actor,
							S32 collidable)
{
	if(!actor){
		return 0;
	}

	if(!collidable){
		actor->nxActor->raiseActorFlag(NX_AF_DISABLE_COLLISION);
	}else{
		actor->nxActor->clearActorFlag(NX_AF_DISABLE_COLLISION);
	}

	return 1;
}


S32 psdkActorHasInvalidGeo(PSDKActor* actor){
	return SAFE_MEMBER(actor, flags.geoInvalidated);
}

S32 psdkActorGetInfoString(	char* strOut,
							S32 strOutLen,
							PSDKActor* actor)
{
	AssociationListIterator*	iter;
	PSDKCookedMesh*				mesh;
	
	if(	!actor ||
		!strOut ||
		strOutLen <= 0)
	{
		return 0;
	}
	
	strOut[0] = 0;

	for(alItCreate(&iter, actor->alMeshes);
		alItGetOwner(iter, (void**)&mesh);
		alItGotoNextThenDestroy(&iter))
	{
		if(!mesh){
			continue;
		}
		
		switch(mesh->flags.meshType){
			xcase PSDK_CMT_TRIANGLE:{
				NxTriangleMesh* nxTriangleMesh = psdkCookedMeshGetNxTriangleMesh(mesh);
				strcatf_s(	strOut,
							strOutLen,
							"(triangle mesh %d %d)",
							SAFE_MEMBER(nxTriangleMesh, getCount(0, NX_ARRAY_TRIANGLES)),
							SAFE_MEMBER(nxTriangleMesh, getCount(0, NX_ARRAY_VERTICES)));
			}
			
			xcase PSDK_CMT_CONVEX:{
				strcatf_s(	strOut,
							strOutLen,
							"(convex mesh %d)",
							mesh->nxConvexMesh->getCount(0, NX_ARRAY_VERTICES));
			}
			
			xcase PSDK_CMT_HEIGHTFIELD:{
				strcatf_s(	strOut,
							strOutLen,
							"(hf:%dx%d-%1.1fx%1.1f)",
							mesh->heightField.gridSize[0],
							mesh->heightField.gridSize[1],
							mesh->heightField.worldSize[0],
							mesh->heightField.worldSize[1]);
			}
		}
	}
	
	return 1;
}

S32	psdkActorGetScene(PSDKScene** sceneOut, 
					  PSDKActor* actor)
{
	if(!*sceneOut){
		return 0;
	}

	if(!actor){
		*sceneOut = NULL;
		return 0;
	}

	*sceneOut = (PSDKScene*)actor->nxActor->getScene().userData;

	if(!*sceneOut){
		// not good
	}

	return 1;
}

static NxCombineMode psdkGetNxCombineMode(PSDKMaterialCombineType combineType){
	switch(combineType){
		case PSDK_MCT_MIN:
			return NX_CM_MIN;
		case PSDK_MCT_MULTIPLY:
			return NX_CM_MULTIPLY;
		case PSDK_MCT_MAX:
			return NX_CM_MAX;
		default:
			return NX_CM_AVERAGE;
	}
}

S32 psdkSceneSetMaterial(	PSDKScene* scene,
							U32 materialIndex,
							const PSDKMaterialDesc* md)
{
	if(	!psdkCanModify(NULL) ||
		!SAFE_MEMBER(scene, nxScene) ||
		!md ||
		materialIndex > 0xffff)
	{
		return 0;
	}
	
	NxMaterialDesc nxMaterialDesc;
	
	//note that material indices are not guaranteed to actually work like they are used here
	//I think this code was written this way, including the assert above, under the assumption
	//that materials wouldn't get deleted & their indices would match their order of creation

	while(scene->nxScene->getNbMaterials() <= materialIndex){
		scene->nxScene->createMaterial(nxMaterialDesc);
	}
	
	NxMaterial* nxMaterial = scene->nxScene->getMaterialFromIndex(materialIndex);
	
	if(!nxMaterial){
		return 0;
	}
	
	nxMaterialDesc.dynamicFriction = md->dynamicFriction;
	nxMaterialDesc.staticFriction = md->staticFriction;
	nxMaterialDesc.restitution = md->restitution;
	nxMaterialDesc.dynamicFrictionV = md->dynamicFrictionV;
	nxMaterialDesc.staticFrictionV = md->staticFrictionV;
	copyVec3(md->dirOfAnisotropy, nxMaterialDesc.dirOfAnisotropy);
	
	nxMaterialDesc.flags =	(md->flags.isAnisotropic ? NX_MF_ANISOTROPIC : 0) |
							(md->flags.disableFriction ? NX_MF_DISABLE_FRICTION : 0) |
							(md->flags.disableStrongFriction ? NX_MF_DISABLE_STRONG_FRICTION : 0);
							
	nxMaterialDesc.frictionCombineMode = psdkGetNxCombineMode(md->frictionCombineType);
	nxMaterialDesc.restitutionCombineMode = psdkGetNxCombineMode(md->restitutionCombineType);
							
	nxMaterial->loadFromDesc(nxMaterialDesc);
	
	return 1;
}

S32	psdkConnectRemoteDebugger(const char* address, U32 port)
{
	PERFINFO_AUTO_START_FUNC();
	
	nxSDK->getFoundationSDK().getRemoteDebugger()->connect(address, port);
	
	PERFINFO_AUTO_STOP();

	return 1;
}

GMesh* psdkCookedMeshToGmesh(PSDKCookedMesh *mesh){
	const Vec3*	psdkMeshVerts;
	U32			psdkMeshVertCount;
	const S32*	psdkMeshTris;
	U32			psdkMeshTriCount;

	if(	psdkCookedMeshGetVertices(	mesh,
									&psdkMeshVerts,
									&psdkMeshVertCount) &&
		psdkCookedMeshGetTriangles(	mesh,
									&psdkMeshTris,
									&psdkMeshTriCount))
	{
		GMesh* gmesh = callocStruct(GMesh);

		gmeshSetUsageBits(gmesh, USE_POSITIONS);
		FOR_BEGIN(i, (S32)psdkMeshVertCount);
		{
			gmeshAddVertSimple(gmesh, psdkMeshVerts[i], NULL, NULL, NULL, NULL, NULL, false, false, false);
		}
		FOR_END;
		FOR_BEGIN(i, (S32)psdkMeshTriCount);
		{
			gmeshAddTri(gmesh, psdkMeshTris[3 * i + 2], psdkMeshTris[3 * i + 1], psdkMeshTris[3 * i], 0, false);
		}
		FOR_END;

		return gmesh;
	}
	return NULL;
}

GMesh* psdkCreateConvexHullGMesh(	const char* name,
									S32 vert_count,
									const Vec3 *verts)
{
	PSDKCookedMesh*	convex_hull;
	PSDKMeshDesc	mesh_desc = {0};

	// populate mesh description
	mesh_desc.name = name;
	mesh_desc.vertCount = vert_count;
	mesh_desc.vertArray = verts;

	// generate the mesh
	wcWaitForSimulationToEndFG(1, NULL, true);
	if (psdkCookedMeshCreate(&convex_hull, &mesh_desc))
	{
		// convert PSDK mesh to GMesh
		GMesh *gmesh = psdkCookedMeshToGmesh(convex_hull);

		// cleanup
		psdkCookedMeshDestroy(&convex_hull);

		return gmesh;
	}
	return NULL;
}

GConvexHull* psdkCreateConvexHull(	const char* name,
									S32 vert_count,
									const Vec3 *verts)
{
	GMesh* mesh = psdkCreateConvexHullGMesh(name, vert_count, verts);

	if(mesh)
	{
		GConvexHull *hull = gmeshToGConvexHull(mesh);

		gmeshFreeData(mesh);
		free(mesh);

		return hull;
	}
	return NULL;
}

#endif
