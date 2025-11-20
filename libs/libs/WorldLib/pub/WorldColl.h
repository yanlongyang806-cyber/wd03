
#pragma once
GCC_SYSTEM

#include "stdtypes.h"

C_DECLARATIONS_BEGIN

typedef struct WorldColl							WorldColl;
typedef struct WorldCollIntegration					WorldCollIntegration;
typedef struct WorldCollObject						WorldCollObject;
typedef struct WorldCollGridCell					WorldCollGridCell;
typedef struct WorldCollScene						WorldCollScene;
typedef struct WorldCollActor						WorldCollActor;
typedef struct WorldCollFluid						WorldCollFluid;
typedef struct WorldCollMaterial					WorldCollMaterial;

typedef struct AssociationNode						AssociationNode;

typedef struct PSDKActorDesc						PSDKActorDesc;
typedef struct PSDKBodyDesc							PSDKBodyDesc;
typedef struct PSDKFluidDesc						PSDKFluidDesc;
typedef struct PSDKCookedMesh						PSDKCookedMesh;
typedef struct PSDKActor							PSDKActor;
typedef struct PSDKFluid							PSDKFluid;
typedef struct PSDKMaterialDesc						PSDKMaterialDesc;
typedef struct PSDKScene							PSDKScene;

typedef struct FrameLockedTimer						FrameLockedTimer;

typedef struct Capsule								Capsule;

// MS: These typedefs will be removed.

typedef struct CTri									CTri;
typedef struct WLCostume							WLCostume;

typedef struct PhysicalProperties		PhysicalProperties;
typedef struct Material					Material;

extern const F32 defaultSFriction;
extern const F32 defaultDFriction;
extern const F32 defaultRestitution;
extern const F32 defaultGravity;
#define ENT_WORLDCAP_DEFAULT_RADIUS 1.0f
#define ENT_WORLDCAP_DEFAULT_HEIGHT_OFFUP	6.f
#define ENT_WORLDCAP_DEFAULT_PLAYER_STEP_HEIGHT	1.5f
#define ENT_WORLDCAP_DEFAULT_CRITTER_STEP_HEIGHT 2.1f

//--------------------------------------------------------------------------------------------------
// WorldColl.c
//--------------------------------------------------------------------------------------------------

enum {
	WC_FILTER_BIT_ENTITY					=   1<<0,	// only set on entities
	WC_FILTER_BIT_WORLD_NORMAL				=   1<<1,	// set on all world that is not "traversable"
	WC_FILTER_BIT_WORLD_TRAVERSABLE			=   1<<2,	// gates and other movable geo, through which connections need to be made for pathfinding
	WC_FILTER_BIT_MOVEMENT					=   1<<3,	// anything you can bump into
	WC_FILTER_BIT_CAMERA_BLOCKING			=   1<<4,	// anything the camera cannot see through
	WC_FILTER_BIT_CAMERA_FADE				=   1<<5,	// anything that blocks the camera, and needs to fade when the camera is looking through it
	WC_FILTER_BIT_CAMERA_VANISH				=   1<<6,	// anything that blocks the camera, but we don't want to see at all when the camera is close to it
	WC_FILTER_BIT_POWERS					=   1<<7,	// Anything that should be considered to block powers
	WC_FILTER_BIT_TARGETING					=   1<<8,	// Anything that blocks the player from selecting a target
	WC_FILTER_BIT_FX_SPLAT					=   1<<9,	// Anything that should be a target for FX splats
	WC_FILTER_BIT_EDITOR					=   1<<10,	// Anything that is visible in the editor
	WC_FILTER_BIT_HEIGHTMAP					=   1<<11,  // A special kind of world collision
	WC_FILTER_BIT_TERRAIN					=   1<<12,  // A special kind of world collision
	WC_FILTER_BIT_VOLUME					=   1<<13,  // only set on volumes
	WC_FILTER_BIT_SHIELD					=   1<<14,  // STO-specific
	WC_FILTER_BIT_HULL						=   1<<15,  // STO-specific
	WC_FILTER_BIT_DEBRIS					=   1<<16,
	WC_FILTER_BIT_PLAYABLEVOLUMEGEO			=	1<<17,	// geo built from any volumes (or sub-volumes) with the playable property set (or inherited)

	// Some handy groupings for doing queries
	WC_QUERY_BITS_WORLD_ALL								=	WC_FILTER_BIT_WORLD_NORMAL |
															WC_FILTER_BIT_TERRAIN |
															WC_FILTER_BIT_WORLD_TRAVERSABLE,

	WC_QUERY_BITS_ENTITY_MOVEMENT						=	WC_FILTER_BIT_MOVEMENT |
															WC_FILTER_BIT_ENTITY,

	WC_QUERY_BITS_AI_CIV								=	WC_FILTER_BIT_WORLD_NORMAL |
															WC_FILTER_BIT_TERRAIN,

	WC_QUERY_BITS_EDITOR_ALL							=   WC_FILTER_BIT_EDITOR |
															WC_FILTER_BIT_VOLUME,

	WC_QUERY_BITS_COMBAT								=   WC_FILTER_BIT_POWERS |
															WC_FILTER_BIT_ENTITY,

	WC_QUERY_BITS_TARGETING								=   WC_FILTER_BIT_TARGETING |
															WC_FILTER_BIT_ENTITY,

	WC_QUERY_BITS_CAMERA_BLOCKING						=   WC_FILTER_BIT_CAMERA_BLOCKING,

	// standard object filter bit groups
	WC_FILTER_BITS_WORLD_STANDARD						=   WC_FILTER_BIT_WORLD_NORMAL |
															WC_FILTER_BIT_MOVEMENT |
															WC_FILTER_BIT_EDITOR | 
															WC_FILTER_BIT_CAMERA_BLOCKING |
															WC_FILTER_BIT_POWERS |
															WC_FILTER_BIT_TARGETING |
															WC_FILTER_BIT_FX_SPLAT,

	WC_FILTER_BITS_HEIGHTMAP							=   WC_FILTER_BIT_WORLD_NORMAL |
															WC_FILTER_BIT_MOVEMENT |
															WC_FILTER_BIT_EDITOR |
															WC_FILTER_BIT_CAMERA_BLOCKING |
															WC_FILTER_BIT_POWERS |
															WC_FILTER_BIT_TARGETING |
															WC_FILTER_BIT_FX_SPLAT |
															WC_FILTER_BIT_HEIGHTMAP,

	WC_FILTER_BITS_TERRAIN								=   WC_FILTER_BIT_MOVEMENT |
															WC_FILTER_BIT_CAMERA_BLOCKING |
															WC_FILTER_BIT_POWERS |
															WC_FILTER_BIT_TARGETING |
															WC_FILTER_BIT_FX_SPLAT |
															WC_FILTER_BIT_TERRAIN,

	WC_FILTER_BITS_ENTITY								=  	WC_FILTER_BIT_CAMERA_BLOCKING |
															WC_FILTER_BIT_ENTITY,

	WC_FILTER_BITS_VOLUME								=   WC_FILTER_BIT_VOLUME,
};

enum
{
	WC_SHAPEGROUP_NONE					=   0,
	WC_SHAPEGROUP_WORLD_BASIC			=   1,
	WC_SHAPEGROUP_HEIGHTMAP				=   2,
	WC_SHAPEGROUP_TERRAIN				=   3,
	WC_SHAPEGROUP_EDITOR_ONLY			=	4,
	WC_SHAPEGROUP_ENTITY				=	5,
	WC_SHAPEGROUP_DEBRIS				=   6,
	WC_SHAPEGROUP_DEBRIS_LARGE			=	7,
	WC_SHAPEGROUP_DEBRIS_VERY_LARGE		=	8,
	WC_SHAPEGROUP_RAGDOLL_BODY			=	9,
	WC_SHAPEGROUP_RAGDOLL_LIMB			=	10,
	WC_SHAPEGROUP_TEST_RAGDOLL_BODY		=	11,
	WC_SHAPEGROUP_TEST_RAGDOLL_LIMB		=	12
};

typedef struct WorldCollFilter
{
	U32 filterBits;
	U16 shapeGroup;
} WorldCollFilter;

typedef struct WorldCollStoredModelData {
	char*						name;
	char*						detail;
	const char*					filename;
	Vec3						min;
	Vec3						max;

	// Tri mesh, also for hmap since it's needed anyways

	Vec3*						verts;
	S32							vert_count;
	S32*						tris;
	S32							tri_count;
	Vec3*						norms;
	
	// Height map only

	S32							map_size;
	S32							grid_size;
	F32*						heights;
	bool*						holes;
} WorldCollStoredModelData;

typedef struct WorldCollModelInstanceData {
	Mat4						world_mat;
	
	U32							transient : 1;
	U32							noGroundConnections : 1;
} WorldCollModelInstanceData;

typedef enum WorldCollObjectMsgType {
	WCO_MSG_GET_SHAPE,
	WCO_MSG_GET_NEW_MAT,
	WCO_MSG_DESTROYED,
	WCO_MSG_GET_MODEL_DATA,
	WCO_MSG_GET_INSTANCE_DATA,
	WCO_MSG_GET_DEBUG_STRING,
	WCO_MSG_COUNT,
} WorldCollObjectMsgType;

typedef enum WorldCollObjectShapeType {
	WCO_ST_NONE = 0,
	WCO_ST_COOKED_MESH,
	WCO_ST_CAPSULE,
	WCO_ST_SPHERE,
	WCO_ST_BOX,
} WorldCollObjectShapeType;

typedef struct WorldCollObjectMsgGetShapeOutInst {
	WorldCollObjectShapeType			shapeType;

	union {
		PSDKCookedMesh*					mesh;
		
		struct {
			F32							length;
			F32							radius;
		} capsule;
		
		struct {
			F32							radius;
		} sphere;
		
		struct {
			Vec3						xyzSize;
		} box;
	};

	Mat4								mat;
	F32									density;
	WorldCollMaterial*					material;
	WorldCollFilter						filter;
} WorldCollObjectMsgGetShapeOutInst;

typedef struct WorldCollObjectMsgGetShapeOut {
	Mat4								mat;
	WorldCollObjectMsgGetShapeOutInst**	meshes;
	PSDKBodyDesc*						bodyDesc;
	
	struct {
		U32								useBodyDesc : 1;
		U32								isKinematic : 1;
		U32								hasOneWayCollision : 1;
	} flags;
} WorldCollObjectMsgGetShapeOut;

typedef struct WorldCollObjectMsgGetNewMatOut {
	Mat4								mat;
} WorldCollObjectMsgGetNewMatOut;

typedef struct WorldCollObjectMsgGetModelDataOut {
	WorldCollStoredModelData*			modelData;
	WorldCollModelInstanceData*			instData;
} WorldCollObjectMsgGetModelDataOut;

typedef struct WorldCollObjectMsgGetInstDataOut {
	WorldCollModelInstanceData*			instData;
} WorldCollObjectMsgGetInstDataOut;

typedef struct WorldCollObjectMsg {
	WorldCollObjectMsgType				msgType;
	
	WorldCollObject*					wco;
	void*								userPointer;
	
	union {
		struct {
			U32							filterBits;
		} getModelData;
		
		struct {
			U32							filterBits;
		} getInstData;

		struct {
			char*						buffer;
			S32							bufferLen;
		} getDebugString;
	} in;
	
	union {
		WorldCollObjectMsgGetShapeOut*		getShape;
		WorldCollObjectMsgGetModelDataOut*	getModelData;
		WorldCollObjectMsgGetInstDataOut*	getInstData;
		WorldCollObjectMsgGetNewMatOut*		getNewMat;
	} out;
} WorldCollObjectMsg;

typedef void (*WorldCollObjectMsgHandler)(const WorldCollObjectMsg* msg);

typedef struct WorldCollCollideResultsErrorFlags {
	U32									invalidCoord	: 1;
	U32									noScene			: 1;
	U32									noCell			: 1;
} WorldCollCollideResultsErrorFlags;

typedef struct WorldCollCollideResults {
	U32									hitSomething	: 1;
	F32									distance;
	Vec3								posWorldImpact;		//The coordinates of the impact
	Vec3								posWorldEnd;		//The coordinates of the 'shape' along the ray
	Vec3								normalWorld;

	// Barycentric coords of impacted tri

	struct {
		S32								index;
		F32								u;
		F32								v;
	} tri;

	AssociationNode*					node;
	WorldCollObject*					wco;
	WorldCollGridCell*					cell;
	PSDKActor*							psdkActor;
	WorldCollCollideResultsErrorFlags	errorFlags;
} WorldCollCollideResults;

typedef enum WorldCollIntegrationMsgType {
	// FG msgs.  BG thread can be active.
	
	WCI_MSG_FG_WORLDCOLL_EXISTS,

	WCI_MSG_FG_BEFORE_SIM_SLEEPS,
	WCI_MSG_FG_AFTER_SIM_WAKES,

	// BG msgs.  FG thread can be active.
	
	WCI_MSG_BG_BETWEEN_SIM,

	// NOBG msgs.  Sent in FG thread while BG thread is asleep.

	WCI_MSG_NOBG_WHILE_SIM_SLEEPS,
	WCI_MSG_NOBG_WORLDCOLL_DESTROYED,
	WCI_MSG_NOBG_WORLDCOLLOBJECT_DESTROYED,
	WCI_MSG_NOBG_ACTOR_CREATED,
	WCI_MSG_NOBG_ACTOR_DESTROYED,
} WorldCollIntegrationMsgType;
											
typedef struct WorldCollObjectPrev {
	Mat4									mat;
} WorldCollObjectPrev;
												
typedef struct WorldCollIntegrationMsg {	
	WorldCollIntegrationMsgType				msgType;

	WorldCollIntegration*					wci;
	void*									userPointer;
	
	union {
		// fg.

		union {
			struct {
				WorldColl*					wc;
			} worldCollExists;

			struct {
				WorldColl*					wc;
			} worldCollIsDefault;

			struct {
				const FrameLockedTimer*		flt;
			} beforeSimSleeps;
			
			struct {
				const FrameLockedTimer*		flt;
			} afterSimWakes;
		} fg;

		// bg.

		union {
			struct {
				U32							instanceThisFrame;
				F32							deltaSeconds;
				
				struct {
					U32						noProcessThisFrame;
				} flags;
			} betweenSim;
		} bg;
		
		// nobg.
		
		struct {
			struct {
				const FrameLockedTimer*		flt;
			} whileSimSleeps;
			
			struct {
				WorldColl*					wc;
			} worldCollDestroyed;

			struct {
				WorldCollObject*			wco;
			} worldCollObjectDestroyed;

			struct {
				WorldColl*					wc;
				WorldCollObject*			wco;
				Vec3						boundsMin;
				Vec3						boundsMax;
				PSDKActor*					psdkActor;
				Vec3						sceneOffset;
				const WorldCollObjectPrev*	prev;
				
				U32							isShell : 1;
			} actorCreated;

			struct {
				WorldColl*					wc;
				WorldCollObject*			wco;
				Vec3						boundsMin;
				Vec3						boundsMax;
				PSDKActor*					psdkActor;
				Vec3						sceneOffset;
			} actorDestroyed;
		} nobg;
	};
} WorldCollIntegrationMsg;

// WorldCollObject.

S32		wcoCreate(	WorldCollObject** wcoOut,
					WorldColl* wc,
					WorldCollObjectMsgHandler msgHandler,
					void* userPointer,
					const Vec3 aabbMin,
					const Vec3 aabbMax,
					S32 isDynamic,
					S32 isShell);
					
void	wcoDestroy(WorldCollObject** wcoInOut);

void	wcoDestroyAndNotify(WorldCollObject* wco);

S32		wcoIsDestroyed(WorldCollObject* wco);

void	wcoInvalidate(WorldCollObject* wco);

S32		wcoIsShell(const WorldCollObject *wco);

S32		wcoIsDynamic(const WorldCollObject *wco);

S32		wcoGetPos(	const WorldCollObject* wco,
					Vec3 posOut);

S32		wcoGetMat(	const WorldCollObject* wco,
					Mat4 matOut);

S32		wcoGetActor(WorldCollObject *wco,
					PSDKActor **actorOut,
					Vec3 sceneOffsetOut);

S32		wcoGetBounds(WorldCollObject *wco,
					 Vec3 boundsMinOut,
					 Vec3 boundsMaxOut);

void	wcoChangeBounds(WorldCollObject* wco,
						const Vec3 aabbMin,
						const Vec3 aabbMax);

S32		wcoGetUserPointer(	const WorldCollObject* wco,
							WorldCollObjectMsgHandler msgHandler,
							void** userPointerOut);

S32		wcoUsesMsgHandler(	const WorldCollObject* wco,
							WorldCollObjectMsgHandler msgHandler);

S32		wcoGetStoredModelData(	const WorldCollStoredModelData** smdOut,
								const WorldCollModelInstanceData** instOut,
								WorldCollObject* wco,
								U32 filterBits);

S32		wcoGetInstData(	const WorldCollModelInstanceData** instOut,
						WorldCollObject* wco,
						U32 filterBits);

void	wcoAddShapeInstance(WorldCollObjectMsgGetShapeOut* getShape,
							WorldCollObjectMsgGetShapeOutInst** instOut);

S32		wcoGetFromPSDKActor(WorldCollObject** wcoOut,
							const PSDKActor* psdkActor);

// WorldCollMaterial.

S32		wcMaterialCreate(	WorldCollMaterial** wcmOut,
							const PSDKMaterialDesc* materialDesc);

S32		wcMaterialGetByIndex(	WorldCollMaterial** wcmOut,
								U32 index,
								const PSDKMaterialDesc* materialDesc);

S32		wcMaterialGetIndex(	WorldCollMaterial* wcm,
							U32* indexOut);

S32		wcMaterialSetDesc(	WorldCollMaterial* wcm,
							const PSDKMaterialDesc* materialDesc);

S32		wcMaterialFromPhysicalPropertyName( WorldCollMaterial** wcmOut,
											const char* physPropertyName );

S32		wcMaterialUpdatePhysicalProperties( void );

// WorldCollScene.

S32		wcSceneCreate(	const WorldCollIntegrationMsg* msg,
						WorldCollScene** sceneOut,
						S32 useVariableTimestep,
						F32 gravity,
						const char* name);

S32		wcSceneDestroy(	const WorldCollIntegrationMsg* msg,
						WorldCollScene** sceneInOut);

S32		wcSceneSimulate(const WorldCollIntegrationMsg* msg,
						WorldCollScene* scene);

S32		wcSceneUpdateWorldCollObjectsBegin(	const WorldCollIntegrationMsg* msg,
											WorldCollScene* scene);

S32		wcSceneUpdateWorldCollObjectsEnd(	const WorldCollIntegrationMsg* msg,
											WorldCollScene* scene);

S32		wcSceneGatherWorldCollObjects(	const WorldCollIntegrationMsg* msg,
										WorldCollScene* scene,
										WorldColl* wc,
										const Vec3 aabbMin,
										const Vec3 aabbMax);

S32		wcSceneGatherWorldCollObjectsByRadius(	const WorldCollIntegrationMsg* msg,
												WorldCollScene* scene,
												WorldColl* wc,
												const Vec3 pos,
												F32 radius);

S32		wcSceneGetPSDKScene( WorldCollScene* scene, SA_PARAM_NN_VALID PSDKScene** psdkSceneOut);

// WorldCollActor.

S32		wcActorCreate(	const WorldCollIntegrationMsg* msg,
						WorldCollScene* scene,
						WorldCollActor** wcActorOut,
						void* userPointer,
						PSDKActorDesc* actorDesc,
						PSDKBodyDesc* bodyDesc,
						S32 isKinematic,
						S32 hasContactEvent,
						S32 disableGravity);

S32		wcActorDestroy(	const WorldCollIntegrationMsg* msg,
						WorldCollActor** actorInOut);

S32		wcActorGetUserPointer(WorldCollActor* actor,
							  void** userPointerOut);

S32		wcActorGetPSDKActor(const WorldCollIntegrationMsg* msg,
							WorldCollActor* actor,
							PSDKActor** psdkActorOut);

S32		wcActorSetMat(	const WorldCollIntegrationMsg* msg,
						WorldCollActor* actor,
						const Mat4 mat);

S32		wcActorGetMat(	const WorldCollIntegrationMsg* msg,
						WorldCollActor* actor,
						Mat4 matOut);
						
S32		wcActorGetVels(	const WorldCollIntegrationMsg* msg,
						WorldCollActor* actor,
						Vec3 velOut,
						Vec3 angVelOut);

S32		wcActorMove(const WorldCollIntegrationMsg* msg,
					WorldCollActor* actor,
					const Vec3 pos);

S32		wcActorSetCollidable(	const WorldCollIntegrationMsg* msg,
								WorldCollActor* actor,
								S32 collidable);

S32		wcActorRotate(const WorldCollIntegrationMsg* msg,
					  WorldCollActor* actor,
					  const Quat rot);

S32		wcActorSetVels(	const WorldCollIntegrationMsg* msg,
						WorldCollActor* actor,
						const Vec3 vel,
						const Vec3 angVel);

S32		wcActorAddVel(	const WorldCollIntegrationMsg* msg,
						WorldCollActor* actor,
						const Vec3 vel);

S32		wcActorAddForce(const WorldCollIntegrationMsg* msg,
						WorldCollActor* actor,
						const Vec3 force,
						S32 isAcceleration,
						S32 shouldWakeup);

S32		wcActorAddAngVel(	const WorldCollIntegrationMsg* msg,
							WorldCollActor* actor,
							const Vec3 vel);
							
S32		wcActorCreateAndAddCCDSkeleton(	const WorldCollIntegrationMsg* msg,
										WorldCollActor* actor,
										const Vec3 center);

S32		wcActorWakeUp(	const WorldCollIntegrationMsg* msg,
						WorldCollActor* actor);

// WorldCollFluid(.c)

S32		wcFluidCreate(	const WorldCollIntegrationMsg* msg,
						WorldCollScene *scene,
						WorldCollFluid **wcFluidOut,
						void* userPointer,
						PSDKFluidDesc *fluidDesc);

S32		wcFluidDestroy(	const WorldCollIntegrationMsg* msg,
						WorldCollFluid **wcFluidInOut);

S32		wcFluidSetMaxParticles(	const WorldCollIntegrationMsg *msg,
								WorldCollFluid *wcFluid,
								U32 maxParticles);

S32		wcFluidSetDamping(	const WorldCollIntegrationMsg *msg,
							WorldCollFluid *wcFluid, 
							F32 damping);

S32		wcFluidSetStiffness(	const WorldCollIntegrationMsg *msg,
								WorldCollFluid *wcFluid,
								F32 stiffness);

S32		wcFluidSetViscosity(const WorldCollIntegrationMsg *msg,
							WorldCollFluid *wcFluid,
							F32 viscosity);

// WorldColl setup.

S32		wcCreate(WorldColl** wcOut);
					
void	wcDestroy(WorldColl** wcInOut);

void	wcCreateAllScenes(WorldColl* wc);

// WorldCollIntegration stuff.

typedef void (*WorldCollIntegrationMsgHandler)(const WorldCollIntegrationMsg* msg);

void	wcIntegrationCreate(WorldCollIntegration** wciOut,
							WorldCollIntegrationMsgHandler msgHandler,
							void* userPointer,
							const char* name);

// WorldColl simulation stuff.

void	wcSwapSimulation(const FrameLockedTimer* flt);

void	wcWaitForSimulationToEndFG(	S32 waitForever,
									S32* isDoneSimulatingOut,
									S32 skipUpdateWhileSimSleeps);

S32		wcForceSimulationUpdate(void);

// WorldColl random queries.

S32		wcIsThreaded(void);

S32		wcIsValidObject(const WorldCollObject* wco);

void	wcSetThreadIsBG(void);

S32		wcIsThreadBG(void);


// WorldColl cell stuff.

S32		wcCellGetWorldColl(	WorldCollGridCell* cell,
							WorldColl** wcOut);

S32		wcCellGetSceneAndOffset(WorldCollGridCell* cell,
								PSDKScene** psdkSceneOut,
								Vec3 sceneOffsetOut);

S32		wcCellRequestSceneCreateBG(	const WorldCollIntegrationMsg* msg,
									WorldCollGridCell* cell);

S32		wcGetGridCellByWorldPosBG(	const WorldCollIntegrationMsg* msg,
									WorldColl* wc,
									WorldCollGridCell** cellOut,
									const Vec3 pos);

S32		wcGetGridCellByWorldPosFG(	WorldColl* wc,
									WorldCollGridCell** cellOut,
									const Vec3 pos);

S32		wcCellHasScene(	WorldCollGridCell* cell,
						const char* createReason);

// WorldColl geometry queries.

S32		wcRayCollide(	WorldColl* wc,
						const Vec3 source,
						const Vec3 target,
						U32 filterBits,
						WorldCollCollideResults* resultsOut);

typedef S32 (*WorldCollCollideResultsCB)(	void* userPointer,
											const WorldCollCollideResults* results);

S32		wcRayCollideMultiResult(WorldColl* wc,
								const Vec3 source,
								const Vec3 target,
								U32 filterBits,
								WorldCollCollideResultsCB callback,
								void* userPointer,
								WorldCollCollideResultsErrorFlags* errorFlagsOut);

S32		wcCapsuleCollideEx(	WorldColl* wc,
							const Capsule cap,
							const Vec3 source,
							const Vec3 target,
							U32 filterBits,
							WorldCollCollideResults* resultsOut);

S32		wcCapsuleCollideHR(	WorldColl* wc,
							const Vec3 source,
							const Vec3 target,
							U32 filterBits,
							F32 fHeight,
							F32 fRadius,
							WorldCollCollideResults* resultsOut);

S32		wcCapsuleCollide(	WorldColl* wc,
							const Vec3 source,
							const Vec3 target,
							U32 filterBits,
							WorldCollCollideResults* resultsOut);

S32		wcBoxCollide(	WorldColl* wc,
						const Vec3 minLocalOBB,
						const Vec3 maxLocalOBB,
						const Mat4 matWorldOBB,
						const Vec3 target,
						U32 filterBits,
						WorldCollCollideResults* resultsOut);

S32		wcCapsuleCollideCheck(	WorldColl *wc,
								const Capsule* capsule,
								const Vec3 source,
								U32 filterBits,
								WorldCollCollideResults* resultsOut);

typedef void (*WorldCollQueryTrianglesCB)(	void* userPointer,
											Vec3 (*tris)[3],
											U32 triCount);

S32		wcQueryTrianglesInYAxisCylinder(	WorldColl* wc,
											U32 filterBits,
											const Vec3 source,
											const Vec3 target,
											F32 radius,
											WorldCollQueryTrianglesCB callback,
											void* userPointer);

S32		wcQueryTrianglesInCapsule(	WorldColl* wc,
									U32 filterBits,
									const Vec3 source,
									const Vec3 target,
									F32 radius,
									WorldCollQueryTrianglesCB callback,
									void* userPointer);

// Traverse.

#define WCO_TRAVERSE_STATIC		(1<<0)
#define WCO_TRAVERSE_DYNAMIC	(1<<1)
#define WCO_TRAVERSE_ALL		0xFFFFFFFF

typedef struct WorldCollObjectTraverseParams {
	WorldCollObject*	wco;
	U32					first : 1;
	U32					traverse_type;
} WorldCollObjectTraverseParams;

typedef void (*WorldCollObjectTraverseCB)(	void* userPointer,
											const WorldCollObjectTraverseParams* params);

S32		wcTraverseObjects(	WorldColl* wc,
							WorldCollObjectTraverseCB callback,
							void* userPointer,
							const Vec3 min_xyz,
							const Vec3 max_xyz,
							U32 unique,
							U32 traverse_types);

// WorldCollStoredModelData.

typedef struct WorldCollStoredModelDataDesc {
	const char*					filename;

	Vec3						min;
	Vec3						max;

	const Vec3*					verts;
	S32							vert_count;
	const S32*					tris;
	S32							tri_count;

	// Height map only.

	S32							map_size;
	S32							grid_size;
	F32*						heights;
	bool*						holes;
} WorldCollStoredModelDataDesc;

S32		wcStoredModelDataFind(	const char* name,
								WorldCollStoredModelData** smdOut);

S32		wcStoredModelDataCreate(const char* name,
								const char* detail,
								const WorldCollStoredModelDataDesc* desc,
								WorldCollStoredModelData** smdOut);

S32		wcStoredModelDataDestroyByName(const char* name);
S32		wcStoredModelDataDestroy(WorldCollStoredModelData** smdInOut);
S32		wcStoredModelDataDestroyAll(void);

//--------------------------------------------------------------------------------------------------
// WorldCollBad.c, things that shouldn't be in WorldColl.
//--------------------------------------------------------------------------------------------------

PhysicalProperties* wcoGetPhysicalProperties(WorldCollObject *wco, 
											 S32 triangleIndex,
											 Vec3 worldPos,
											 Material **material_out);

const char* wcoGetPhysicalPropertyName(	WorldCollObject* wco,
										S32 triangle_index,
										Vec3 worldPos);

U32		wcGenerateCollCRC(	S32 printInfo,
							S32 logInfo,
							S32 spawnPos,
							char* logName);

//--------------------------------------------------------------------------------------------------
// WorldCollTest.c
//--------------------------------------------------------------------------------------------------

void	wcCreateTestObject(	const char* modelName,
							const Vec3 pos,
							F32 scale);

void	wcCreateTestKinematic(	int iPartitionIdx,
								const char* modelName,
								const Vec3 pos);

void	wcDestroyAllTestObjects(void);

void	wcDrawTestObjects(void);

//--------------------------------------------------------------------------------------------------

C_DECLARATIONS_END
