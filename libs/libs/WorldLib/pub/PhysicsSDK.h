#ifndef PHYSICSSDK_H
#define PHYSICSSDK_H
#pragma once
GCC_SYSTEM

// The main .exe project needs to define PHYSX_LIB_FOLDER and include this file.

typedef struct GMesh		GMesh;
typedef struct GConvexHull	GConvexHull;

// --- Options Begin -------------------------------------------------------------------------------
#ifndef PSDK_DISABLED
	#define PSDK_DISABLED		0
#endif

#define PHYSX_FORCE_DEBUG_LIBS	0
#define XBOX_PSDK_ENABLED		1
#define PS3_PSDK_ENABLED		1
// --- Options End ---------------------------------------------------------------------------------

#ifndef PHYSX_USE_RELEASE_LIBS
	#define PHYSX_USE_RELEASE_LIBS 1
#endif

#if PHYSX_FORCE_DEBUG_LIBS && !defined(PROFILE)
	#undef PHYSX_USE_RELEASE_LIBS
	#define PHYSX_USE_RELEASE_LIBS 0
#endif

#if _PS3 && !PS3_PSDK_ENABLED
	#undef PSDK_DISABLED
	#define PSDK_DISABLED 1
#endif

#if _XBOX && !XBOX_PSDK_ENABLED
	#undef PSDK_DISABLED
	#define PSDK_DISABLED 1
#endif

#if !PSDK_DISABLED

//#define PHYSX_VERSION_FOLDER		"PhysX-2.7.0"
//#define PHYSX_VERSION_FOLDER		"PhysX-2.7.2"
#define PHYSX_VERSION_FOLDER		"PhysX-2.8.1"

#ifdef PHYSX_SRC_FOLDER
	#define PHYSX_LIB_FOLDER			PHYSX_VERSION_FOLDER"/SDKs/lib"

	#ifdef _XBOX
		#define PHYSX_PLATFORM_FOLDER	"xbox360"
	#else
		#define PHYSX_PLATFORM_FOLDER	"win32"
	#endif

	#if PHYSX_USE_RELEASE_LIBS
		// Use the RELEASE libraries.

		#define PHYSX_DEBUG_SUFFIX		""
	#else
		// Use the DEBUG libraries.

		#define PHYSX_DEBUG_SUFFIX		"DEBUG"
	#endif

	#define PHYSX_PLATFORM_LIB_FOLDER	PHYSX_SRC_FOLDER"/"PHYSX_LIB_FOLDER"/"PHYSX_PLATFORM_FOLDER

	#ifdef _XBOX
		#pragma comment(lib, PHYSX_PLATFORM_LIB_FOLDER"/Framework"PHYSX_DEBUG_SUFFIX".lib")
	#endif

	#pragma comment(lib, PHYSX_PLATFORM_LIB_FOLDER"/PhysXLoader"PHYSX_DEBUG_SUFFIX".lib")
	#pragma comment(lib, PHYSX_PLATFORM_LIB_FOLDER"/PhysXCore"PHYSX_DEBUG_SUFFIX".lib")
	#pragma comment(lib, PHYSX_PLATFORM_LIB_FOLDER"/NxCooking"PHYSX_DEBUG_SUFFIX".lib")

	#undef PHYSX_PLATFORM_LIB_FOLDER
	#undef PHYSX_LIB_FOLDER
	#undef PHYSX_SRC_FOLDER
	#undef PHYSX_PLATFORM_FOLDER
	#undef PHYSX_DEBUG_SUFFIX
#endif

#ifdef _XBOX
	#define NX_USE_SDK_STATICLIBS
#endif

#include "stdtypes.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:CookerThread", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PhysicsSDKHelpers", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PhysicsSDKCharacter", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("nxarray", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("icepruningengine", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("icesignature", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("icehandlemanager", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("icepruningpool", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("icestaticpruner", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("opc_aabbtree", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("icecontainer", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("icecustomarray", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("opc_basemodel", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("opc_optimizedtree", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("opc_hybridmodel", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("icerevisitedradix", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("NxMutex", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("NpPhysicsSDK", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PxcPoolMalloc", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("npscene", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PxcSet", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PxsContext", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PxsBroadPhaseArray", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PxcPool", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PxsDynamics", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PxcArray", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PxsSolverCoreSIMD", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("Scene", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PxcProfiler", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("NPhaseCore", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("NxThread", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("Profiler", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PxcGenericCache", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PxcUnionFind", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PxcCache", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("HeightField", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("Shape", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PxcBitMap", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PxsShape", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PxcStack", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("NPhaseContext", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("BroadPhase", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("FoundationSDK", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("Cooking", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("InternalTriangleMesh", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("IceMeshBuilder2", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("CookingUtils", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("InternalTriangleMeshBuilder", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("EdgeList", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("ConvexDecomposer", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("Sphere", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("TriangleMeshBuilder", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("hulllib", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("ConvexMeshBuilder", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("ConvexHullBuilder", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("IceAdjacencies", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("CollisionHullBuilder", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("ValencyBuilder", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("PairManager", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("TriangleMesh", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("IceHullGaussMapsBuilder", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("Valency", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("ConvexMesh", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("IceHullGaussMaps", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("ConvexHull", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("CCDSkeleton", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("nxSDK", BUDGET_Physics););


C_DECLARATIONS_BEGIN

#define PSDK_MAX_SCENE_COUNT		(64)
#define PSDK_MAX_ACTORS_PER_SCENE	((U32)(1 << 16))

typedef struct Capsule					Capsule;
typedef struct PSDKCookedMesh			PSDKCookedMesh;
typedef struct PSDKScene				PSDKScene;
typedef struct PSDKActor				PSDKActor;
typedef struct PSDKFluid				PSDKFluid;
typedef struct PSDKFluid				PSDKFluid;
typedef struct PSDKFluidEmitter			PSDKFluidEmitter;
typedef struct PSDKMaterial				PSDKMaterial;
typedef struct PSDKSimulationOwnership	PSDKSimulationOwnership;
typedef struct PSDKShape				PSDKShape;
typedef struct DynJointTuning			DynJointTuning;

typedef struct PSDKMeshDesc {
	const char*	name;
	const Vec3* vertArray;
	S32			vertCount;
	const S32*	triArray;
	S32			triCount;
	F32			sphereRadius;
	F32			capsuleHeight;
	Vec3		boxSize;
	void*		preCookedData; // physics system takes ownership of this memory
	S32			preCookedSize;
	bool		no_error_msg;

	U32			no_thread : 1;
} PSDKMeshDesc;

typedef struct PSDKHeightFieldDesc {
	IVec2		gridSize;
	Vec2		worldSize;
	const F32*	height;
	const bool*	holes;
	
	struct {
		Vec3	worldMin;
		Vec3	worldMax;
	} debug;
} PSDKHeightFieldDesc;

typedef struct PSDKBodyDesc {
	Vec3		linearVelocity;
	Vec3		angularVelocity;
	F32			scale;
} PSDKBodyDesc;

typedef enum PSDKScenePruningType {
	PSDK_SPT_STATIC_AABB_TREE = 0,
	PSDK_SPT_DYNAMIC_AABB_TREE,
	PSDK_SPT_QUADTREE,
	PSDK_SPT_OCTREE,
	PSDK_SPT_NONE,
} PSDKScenePruningType;

typedef struct PSDKSceneDesc {
	U32						useVariableTimestep : 1;
	U32						maxIterations;
	PSDKScenePruningType	staticPruningType;
	PSDKScenePruningType	dynamicPruningType;
	U32						subdivisionLevel;
	U32						maxBoundXYZ;
	F32						gravity;
} PSDKSceneDesc;

typedef struct PSDKRaycastResults {
	PSDKActor*	actor;
	Vec3		posWorldImpact;
	Vec3		normalWorld;
	Vec3		posWorldEnd;
	F32			distance;

	struct {
		U32		index;

		// Barycentric impact coords.

		F32		u;
		F32		v;
	} tri;
} PSDKRaycastResults;

typedef enum PSDKMaterialCombineType {
	PSDK_MCT_AVERAGE,
	PSDK_MCT_MIN,
	PSDK_MCT_MULTIPLY,
	PSDK_MCT_MAX,
	PSDK_MCT_COUNT,
} PSDKMaterialCombineType;

typedef struct PSDKMaterialDesc {
	F32						dynamicFriction;
	F32						staticFriction;
	F32						restitution;
	F32						dynamicFrictionV;
	F32						staticFrictionV;
	Vec3					dirOfAnisotropy;
	
	PSDKMaterialCombineType	frictionCombineType;
	PSDKMaterialCombineType	restitutionCombineType;
	
	struct {
		U32					isAnisotropic			: 1;
		U32					disableFriction			: 1;
		U32					disableStrongFriction	: 1;
	} flags;
} PSDKMaterialDesc;

typedef struct PSDKFluidParticle {
	Vec3 pos;
	Vec3 vel;
} PSDKFluidParticle;

typedef struct PSDKFluidEmitterDesc {
	Vec3 center_pos;
	F32 rate;
	U32 maxParticles;
} PSDKFluidEmitterDesc;

typedef struct PSDKFluidDesc {
	PSDKFluidEmitterDesc **emitters;
	PSDKFluidParticle *particleData;
	U32 *particleCount;
	U32 maxParticles;
	F32 fadeInTime;
	F32 restDensity;
	F32 kernelRadiusMultiplier;
	F32 restParticlesPerMeter;
	F32 motionLimitMultiplier;
	F32 collisionDistanceMultiplier;
	F32 stiffness;
	F32 viscosity;
	F32 damping;
	F32 restitutionForStaticShapes;
	F32 dynamicFrictionForStaticShapes;
	F32 surfaceTension;
	U32 collisionGroup;

	U32 disableGravity : 1;
} PSDKFluidDesc;

// Core SDK functionality.

void 	psdkInit(S32 useCCD);
void 	psdkUninit(void);

void	psdkDisableThreadedCooking(void);
S32		psdkHasHardwareSupport(void);

S32		psdkSimulationOwnershipCreate(PSDKSimulationOwnership** soOut);
S32		psdkSimulationOwnershipDestroy(PSDKSimulationOwnership** soInOut);

void	psdkSimulationLockFG(PSDKSimulationOwnership* so);
void	psdkSimulationUnlockFG(PSDKSimulationOwnership* so);

void	psdkSimulationLockBG(PSDKSimulationOwnership* so);
void	psdkSimulationUnlockBG(PSDKSimulationOwnership* so);

S32		psdkSceneLimitReached(void);

U32		psdkGetSceneCount(void);

// Cooked mesh functionality.

void	psdkCookedMeshDestroy(PSDKCookedMesh** meshInOut);

S32		psdkCookedMeshCreate(	PSDKCookedMesh** meshOut,
								const PSDKMeshDesc* meshDesc);

S32		psdkCookedMeshIsValid(	PSDKCookedMesh* mesh);

S32		psdkCookedMeshIsConvex(	PSDKCookedMesh* mesh);

S32		psdkCookedHeightFieldCreate(PSDKCookedMesh** meshOut,
									const PSDKHeightFieldDesc* heightFieldDesc);

U32		psdkCookedMeshGetBytes(PSDKCookedMesh* mesh);

typedef void (*PSDKDestroyCollisionDataCB)(	void* collisionData,
											U32 triCount);

void	psdkSetDestroyCollisionDataCallback(PSDKDestroyCollisionDataCB callback);

S32		psdkCookedMeshSetCollisionData(	PSDKCookedMesh* mesh,
										void* collisionData);

S32		psdkCookedMeshGetCollisionData(	PSDKCookedMesh* mesh,
										void** collisionDataOut);

S32		psdkCookedMeshGetTriCount(	PSDKCookedMesh* mesh,
									U32* triCountOut);

S32		psdkCookedMeshGetTriangles( PSDKCookedMesh* mesh,
									const S32** trisOut,
									U32* triCountOut);

S32		psdkCookedMeshGetVertices(	PSDKCookedMesh* mesh,
									const Vec3** vertsOut,
									U32* vertCountOut);

S32		pdskPreCookTriangleMesh(const PSDKMeshDesc* meshDesc,
								void** data_out,
								U32* data_size_out);

GMesh*	psdkCookedMeshToGmesh(PSDKCookedMesh *mesh);

GMesh*	psdkCreateConvexHullGMesh(	const char* name, 
									S32 vert_count, 
									const Vec3 *verts);

GConvexHull* psdkCreateConvexHull(	const char* name, 
									S32 vert_count, 
									const Vec3 *verts);

void pdskFreeBuffer(void* data);

// Character controller stuff.

typedef enum PSDKControllerMsgType {
	PSDK_CONTROLLER_MSG_HIT_SURFACE,
	PSDK_CONTROLLER_MSG_COUNT,
} PSDKControllerMsgType;

typedef struct PSDKControllerMsgHitSurface {
	Vec3									worldImpact;
	Vec3									worldNormal;
	Vec3									tri[3];
	PSDKActor*								actor;
} PSDKControllerMsgHitSurface;

typedef struct PSDKControllerMsg {
	PSDKControllerMsgType					msgType;
	void*									userPointer;

	union {
		PSDKControllerMsgHitSurface			hitSurface;
	};
} PSDKControllerMsg;

// ActorDesc functionality.

typedef struct PSDKActorDesc PSDKActorDesc;

S32		psdkActorDescCreate(PSDKActorDesc** actorDescOut);
void	psdkActorDescDestroy(PSDKActorDesc** actorDescInOut);

void	psdkActorDescSetMat4(PSDKActorDesc* actorDesc, const Mat4 mat);

void	psdkActorDescSetStartAsleep(PSDKActorDesc* actorDesc,
									S32 startAsleep);

S32		psdkActorDescAddMesh(	PSDKActorDesc* actorDesc,
								PSDKCookedMesh* mesh,
								const Mat4 mat,
								F32 density,
								U32 materialIndex,
								U32 filterBits,
								U16 shapeGroup,
								U32 shapeTag);
								
S32		psdkActorDescAddCapsule(PSDKActorDesc* actorDesc,
								F32 yLengthEndToEnd,
								F32 radius,
								const Mat4 mat,
								F32 density,
								U32 materialIndex,
								U32 filterBits,
								U16 shapeGroup);

S32		psdkActorDescAddSphere(	PSDKActorDesc* actorDesc,
								F32 radius,
								const Mat4 mat,
								F32 density,
								U32 materialIndex,
								U32 filterBits,
								U16 shapeGroup);

S32		psdkActorDescAddBox(PSDKActorDesc* actorDesc,
							const Vec3 xyzHalfSize,
							const Mat4 mat,
							F32 density,
							U32 materialIndex,
							U32 filterBits,
							U16 shapeGroup);

S32		psdkActorCreateAndAddCCDSkeleton(	PSDKActor* actor,
											const Vec3 center);

// Scene functionality.

typedef void (*PSDKGeoInvalidatedFunc)(	PSDKScene* scene,
										void* sceneUserPointer,
										PSDKActor* actor,
										void* actorUserPointer);

S32		psdkSceneCreate(PSDKScene** sceneOut,
						PSDKSimulationOwnership* so,
						const PSDKSceneDesc* sceneDesc,
						void* userPointer,
						PSDKGeoInvalidatedFunc geoInvalidatedFunc);

void	psdkSceneDestroy(PSDKScene** sceneInOut);

S32		psdkSceneGetUserPointer(PSDKScene* scene,
								void** userPointerOut);

S32		psdkSceneGetInstanceID(	PSDKScene* scene,
								U32* instanceIDOut);

S32		psdkSceneGetActorCounts(PSDKScene* scene,
								U32* actorCountOut,
								U32* nxActorCountOut,
								U32* actorCreateCountOut,
								U32* actorCreateFailedCountOut,
								U32* actorDestroyCountOut);

S32		psdkSceneGetConfig(	PSDKScene* scene,
							PSDKScenePruningType* staticPruningTypeOut,
							PSDKScenePruningType* dynamicPruningTypeOut);

S32		psdkSceneGetActorByIndex(	PSDKScene* scene,
									U32 index,
									PSDKActor* actorOut);

S32		psdkSceneSimulate(	PSDKScene* scene,
							F32 timeStep);

S32		psdkSceneFetchResults(	PSDKScene* scene,
								S32 doBlock);

S32		psdkScenePrintActors(PSDKScene* scene);	

void	psdkSceneDrawDebug(PSDKScene* scene);

// Query functionality.

typedef void (*PSDKQueryTrianglesCB)(	void* userPointer,
										Vec3 (*tris)[3],
										U32 triCount);

S32		psdkRaycastClosestShape(const PSDKScene* scene,
								const Vec3 sceneOffset,
								const Vec3 source,
								const Vec3 target,
								U32 filterBits,
								U32 shapeTag,
								PSDKRaycastResults* resultsOut);

typedef S32 (*PSDKRaycastResultsCB)(void* userPointer,
									const PSDKRaycastResults* results);

S32		psdkRaycastShapeMultiResult(const PSDKScene* scene,
									const Vec3 sceneOffset,
									const Vec3 source,
									const Vec3 target,
									U32 filterBits,
									U32 shapeTag,
									PSDKRaycastResultsCB callback,
									void* userPointer);

S32		psdkCapsulecastClosestShape(const PSDKScene* scene,
									const Vec3 sceneOffset,
									const Capsule capsule,
									const Vec3 source,
									const Vec3 target,
									U32 filterBits,
									PSDKRaycastResults* resultsOut);

S32		psdkCapsuleCheck(	const PSDKScene* scene,
							const Vec3 sceneOffset,
							const Capsule* capsule,
							const Vec3 source,
							U32 filterBits);

S32		psdkOBBCastClosestShape(const PSDKScene* scene,
								const Vec3 sceneOffset,
								const Vec3 minLocal,
								const Vec3 maxLocal,
								const Mat4 matWorld,
								const Vec3 target,
								U32 filterBits,
								PSDKRaycastResults* resultsOut);

S32		psdkSceneQueryTrianglesInYAxisCylinder(	PSDKScene* scene,
												const Vec3 sceneOffset,
												U32 filterBits,
												const Vec3 source,
												const Vec3 target,
												F32 radius,
												PSDKQueryTrianglesCB callback,
												void* userPointer);

S32		psdkSceneQueryTrianglesInCapsule(	const PSDKScene* scene,
											const Vec3 sceneOffset,
											U32 filterBits,
											const Vec3 source,
											const Vec3 target,
											F32 radius,
											PSDKQueryTrianglesCB callback,
											void* userPointer);
											
S32		psdkShapeRaycast(	const PSDKShape* shape,
							const Vec3 sceneOffset,
							const Vec3 source,
							const Vec3 target,
							PSDKRaycastResults* resultsOut);

typedef enum PSDKQueryShapesType {
	PSDK_QS_CAPSULE,
	PSDK_QS_AABB,
} PSDKQueryShapesType;

typedef struct PSDKQueryShapesCBData {
	struct {
		void*						userPointer;
		Vec3						sceneOffset;

		PSDKQueryShapesType			queryType;

		union {
			struct {
				Vec3				source;
				Vec3				target;
				F32					radius;
			} capsule;
			
			struct {
				Vec3				aabbMin;
				Vec3				aabbMax;
			} aabb;
		};

		struct {
			U32						hasOffset : 1;
		} flags;
	} input;

	void**					shapes;
	U32						shapeCount;
} PSDKQueryShapesCBData;

typedef void (*PSDKQueryShapesCB)(const PSDKQueryShapesCBData* data);

typedef struct PSDKQueryShapesInCapsuleParams {
	U32					filterBits;
	Vec3				source;
	Vec3				target;
	F32					radius;
	PSDKQueryShapesCB	callback;
	void*				userPointer;
} PSDKQueryShapesInCapsuleParams;

S32		psdkSceneQueryShapesInCapsule(	PSDKScene* scene,
										const Vec3 sceneOffset,
										const PSDKQueryShapesInCapsuleParams* params);

typedef struct PSDKQueryShapesInAABBParams {
	U32					filterBits;
	Vec3				aabbMin;
	Vec3				aabbMax;
	PSDKQueryShapesCB	callback;
	void*				userPointer;
} PSDKQueryShapesInAABBParams;

S32		psdkSceneQueryShapesInAABB(	const PSDKScene* scene,
									const Vec3 sceneOffset,
									const PSDKQueryShapesInAABBParams* params);

S32		psdkShapeGetActor(	const PSDKShape* shape,
							PSDKActor** actorOut);

S32		psdkShapeGetMat(const PSDKShape* shape,
						Mat4 matOut);

S32		psdkShapeGetBounds(	const PSDKShape* shape,
							Vec3 worldMinOut,
							Vec3 worldMaxOut);

S32		psdkShapeGetCookedMesh(	const PSDKShape* shape,
								PSDKCookedMesh** meshOut);

typedef struct PSDKShapeQueryTrianglesCBData {
	struct {
		Vec3				aabbMin;
		Vec3				aabbMax;
		Vec3				sceneOffset;
		const PSDKShape*	shape;
		void*				userPointer;

		struct {
			U32				hasOffset : 1;
			U32				hasOneWayCollision : 1;
		} flags;
	} input;
	
	const U32*				triIndexes;
	U32						triCount;
} PSDKShapeQueryTrianglesCBData;

typedef bool (*PSDKShapeQueryTrianglesCB)(PSDKShapeQueryTrianglesCBData* data);

S32		psdkShapeQueryTrianglesInAABB(	const PSDKShape* shape,
										const Vec3 sceneOffset,
										const Vec3 aabbMin,
										const Vec3 aabbMax,
										PSDKShapeQueryTrianglesCB callback,
										void* userPointer);
										
S32		psdkShapeQueryTrianglesByIndex(	const PSDKShape* shape,
										const U32* indexes,
										U32 triCount,
										Vec3 (*triVertsOut)[3],
										S32 doWorldTransform);

// Assuming you already have indices, this might be faster than psdkShapeQueryTrianglesInAABB
S32		psdkShapeQueryTrianglesInAABBWithIndices(	const PSDKShape* shape,
										const U32* inputIndices,
										U32 inputTriCount,
										const Vec3 sceneOffset,
										const Vec3 aabbMin,
										const Vec3 aabbMax,
										PSDKShapeQueryTrianglesCB callback,
										void* userPointer);

// Actor functionality.

S32 	psdkActorCreate(PSDKActor** actorOut,
						PSDKSimulationOwnership* so,
						const PSDKActorDesc* actorDesc,
						const PSDKBodyDesc* bodyDesc,
						PSDKScene* scene,
						void* userPointer,
						S32 isKinematic,
						S32 hasContactEvent,
						S32 hasOneWayCollision,
						S32 disableGravity);

S32		psdkActorDestroy(	PSDKActor** actorInOut,
							PSDKSimulationOwnership* so);

S32		psdkActorCopy(	PSDKScene* scene,
						PSDKActor** actorOut,
						void* userPointer,
						PSDKActor* actorToCopy,
						const Vec3 sceneOffset);
						
void	psdkActorInvalidate(PSDKActor* actor);

S32		psdkActorGetIgnore(PSDKActor* actor);

S32		psdkActorSetIgnore(	PSDKActor* actor,
							S32 ignore);

S32		psdkActorGetBounds(	PSDKActor* actor,
							Vec3 boundsMinOut,
							Vec3 boundsMaxOut);

S32		psdkActorOverlapCapsule(PSDKActor* actor, 
								const Vec3 worldPos,
								const Capsule* cap);

U32		psdkActorGetShapeCount(PSDKActor* actor);

S32		psdkActorGetShapesArray(PSDKActor* actor,
								const PSDKShape*const** shapesOut);

S32		psdkActorGetShapeByIndex(	PSDKActor* actor,
									U32 index,
									const PSDKShape** shapeOut);

S32		psdkActorGetPos(PSDKActor* actor,
						Vec3 posOut);

S32		psdkActorGetMat(PSDKActor* actor,
						Mat4 matOut);

S32		psdkActorGetFilterBits(	PSDKActor* actor,
									U32* bitsOut);

U16		psdkActorGetShapeGroup(	PSDKActor* actor);

bool    psdkActorHasOneWayCollision(PSDKActor* actor);

S32		psdkActorSetCollidable(	PSDKActor* actor,
								S32 collidable);

S32		psdkActorGetVels(	PSDKActor* actor,
							Vec3 velOut,
							Vec3 angVelOut);

S32		psdkActorSetVels(	PSDKActor* actor,
							const Vec3 vel,
							const Vec3 angVel);

S32		psdkActorAddVel(PSDKActor* actor,
						const Vec3 vel);

S32		psdkActorScaleVel(	PSDKActor *actor,
							F32 scale);

S32		psdkActorAddForce(PSDKActor* actor,
						  const Vec3 force,
						  S32 isAccelerationNotForce,
						  S32 shouldWakeup);

S32		psdkActorAddAngVel(	PSDKActor* actor,
							const Vec3 angvel);

S32		psdkActorSetMaxAngularVel(	PSDKActor* actor,
									F32 maxAngVel);

S32		psdkActorSetSolverIterationCount(	PSDKActor* actor,
											U32 count );

S32		psdkActorSetMat(PSDKActor* actor,
						const Mat4 mat);

S32		psdkActorSetPos(PSDKActor* actor,
						const Vec3 pos);

S32		psdkActorSetRot(PSDKActor* actor,
						const Quat rot);

S32		psdkActorIsPointInside(	PSDKActor* actor,
								const Vec3 sceneOffset,
								const Vec3 pos);

S32		psdkActorSetLinearDamping( PSDKActor* actor,
								   float damping);

S32		psdkActorWakeUp(PSDKActor* actor);

U32		psdkActorIsSleeping(PSDKActor *actor);
S32		psdkActorSetSkinWidth(PSDKActor *actor, F32 skinWidth);
S32		psdkActorSetAngularDamping(PSDKActor *actor, F32 angDamping);
F32		psdkActorGetKineticEnergy(PSDKActor *actor);
F32		psdkActorGetLinearVelocity(PSDKActor *actor);
void	psdkActorGetLinearVelocityFull(PSDKActor *actor, Vec3 vVel);
F32		psdkActorGetAngularVelocity(PSDKActor *actor);
S32		psdkActorSetSleepVelocities(PSDKActor *actor, F32 linearVelocity, F32 angularVelocity);
S32		psdkActorSetCollisionGroup(PSDKActor *actor, U16 collisionGroup);
S32		psdkActorSetRagdollInitialValues(	PSDKActor *actor,
											Vec3 vPos,
											Quat qRot,
											F32 fSkinWidth,
											F32 fLinearSleepVelocity,
											F32 fAngularSleepVelocity,
											F32 fMaxAngularVelocity,
											U32 uiSolverIterationCount,
											U16 uiCollisionGroup);

// Joints.

typedef enum PSDKJointType {
	PSDK_JT_D6,
	PSDK_JT_REVOLUTE,
	PSDK_JT_SPHERICAL,
} PSDKJointType;

typedef struct PSDKJointDesc {
	PSDKActor*		actor0;
	PSDKActor*		actor1;
	
	Vec3			anchor0;
	Vec3			anchor1;

	Vec3			globalAxis;

	F32				volumeScale;

	const DynJointTuning* tuning;

	union {
		struct {
			Vec3	something;
		} d6;
	};
} PSDKJointDesc;

S32		psdkJointCreate(const PSDKJointDesc* jd,
						S32 applyCustomGlobalAxis,
						S32 applyVolumeScale);

// Dynamic actors.

S32		psdkActorIsDynamic(PSDKActor* actor);

S32		psdkActorIsKinematic(PSDKActor* actor);

S32		psdkActorMove(	PSDKActor* actor,
						const Vec3 dir);

S32		psdkActorRotate(PSDKActor* actor,
						const Quat rot);

S32		psdkActorMoveMat(	PSDKActor* actor,
							const Mat4 mat);

S32		psdkActorGetUserPointer(void** userPointerOut,
								const PSDKActor* actor);

S32		psdkActorSetUserPointer(PSDKActor* actor,
								void* userPointer);

S32		psdkActorHasInvalidGeo(PSDKActor* actor);

S32		psdkActorGetInfoString(	char* strOut,
								S32 strOutLen,
								PSDKActor* actor);

S32		psdkActorGetScene(	PSDKScene** sceneOut, 
							PSDKActor* actor);
								
// Material functionality.

S32		psdkSceneSetMaterial(	PSDKScene* scene,
								U32 materialIndex,
								const PSDKMaterialDesc* materialDesc);

S32		psdkConnectRemoteDebugger(	const char* address,
									U32 port);

typedef void (*PSDKContactCB)(	PSDKActor* pActor,
								void* pUID,
								bool bTouching,
								F32 fContactForceTotal,
								bool bContactPoint,
								const Vec3 vContactPos,
								const Vec3 vContactNorm);

S32		psdkSetContactCallback(PSDKContactCB callback);

// FluidEmitterDesc functionality

S32		psdkFluidEmitterDescCreate(PSDKFluidEmitterDesc **descOut);

S32		psdkFluidEmitterDescDestroy(PSDKFluidEmitterDesc **descInOut);

// FluidDesc functionality

S32		psdkFluidDescCreate(PSDKFluidDesc **descOut);

// Fluid functionality

S32		psdkFluidCreate(PSDKFluid **fluidOut,
						PSDKSimulationOwnership *so,
						PSDKScene *scene,
						PSDKFluidDesc *fluidDesc);

S32		psdkFluidDestroy(	PSDKFluid **fluidInOut,
							PSDKSimulationOwnership *so);

S32		psdkFluidSetMaxParticles(	PSDKFluid *fluid,
									PSDKSimulationOwnership *so,
									U32 maxParticles);

S32		psdkFluidSetDamping(PSDKFluid *fluid, 
							PSDKSimulationOwnership *so,
							F32 damping);

S32		psdkFluidSetStiffness(	PSDKFluid *fluid, 
								PSDKSimulationOwnership *so,
								F32 stiffness);

S32		psdkFluidSetViscosity(	PSDKFluid *fluid, 
								PSDKSimulationOwnership *so,
								F32 viscosity);

// FluidEmitterDesc functionality

C_DECLARATIONS_END

#endif // !PSDK_DISABLED

#endif // PHYSICSSDK_H
