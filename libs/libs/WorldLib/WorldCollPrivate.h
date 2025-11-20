#pragma once
GCC_SYSTEM

#include "WorldColl.h"
#include "PhysicsSDK.h"
#include "AssociationList.h"
#include "MemoryPool.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "XboxThreads.h"
#include "ThreadManager.h"
#include "CommandQueue.h"
#include "EventTimingLog.h"
#include "wcoll\entworldcoll.h"

// MS: These are #includes that I will get rid of.

#include "WorldGrid.h"
#include "wlCostume.h"
#include "wlPhysicalProperties.h"

#define WORLDCOLL_VERIFY_ACTORS 0

#if 0
#ifdef CHECK_FINITEVEC3
#undef CHECK_FINITEVEC3
#endif
#ifdef CHECK_FINITEQUAT
#undef CHECK_FINITEQUAT
#endif
	// ST: Added these macros to track down something going to non-finite
	#define CHECK_FINITEVEC3(vec) assert(FINITEVEC3(vec))
	#define CHECK_FINITEQUAT(vec) assert(FINITEQUAT(vec))
#else
	#define CHECK_FINITEVEC3(vec)
	#define CHECK_FINITEQUAT(vec)
#endif

#define WC_FG_SLOT (wcgState.flags.fgSlot)
#define WC_BG_SLOT (wcgState.flags.bgSlot)

#if !PSDK_DISABLED
	#define WC_DEFAULT_MAX_ACTORS_PER_SCENE		(MIN(4000, PSDK_MAX_ACTORS_PER_SCENE))
#else
	#define WC_DEFAULT_MAX_ACTORS_PER_SCENE		4000
#endif

#define WC_MAX_COORD				(128 * 1024)
#define WC_GRID_CELL_SIZE			(0x3 << 9)
#define WC_GRID_CELL_MAX_COORD		(WC_MAX_COORD / WC_GRID_CELL_SIZE)
#define WC_GRID_CELL_SCENE_AND_OFFSET(cell) (cell)->placement->ss->psdkScene,\
											(cell)->placement->flags.hasOffset ?\
												(cell)->placement->sceneOffset :\
												NULL

typedef struct WorldCollObjectDynamic {
	Vec3						aabbMin;
	Vec3						aabbMax;

	struct {
		Vec3					pos;
		Vec3					pyr;
	} prev;
} WorldCollObjectDynamic;

typedef struct WorldCollObject {
	void*						userPointer;

	WorldCollObjectMsgHandler	msgHandler;
	
	// The userPointer for alSecondaryCells nodes are PSDKActor.

	AssociationList*			alSecondaryCells;
	
	// The userPointer for alSecondaryScenes nodes are WorldCollSceneObjectNode.

	AssociationList*			alSecondaryScenes;

	// Only created for dynamic objects.

	WorldCollObjectDynamic*		dynamic;

	struct {
		U32						destroyed			: 1;
		U32						destroyedByUser		: 1;
		U32						boundsChanged		: 1;
		U32						hadPrevActor		: 1;
		U32						sentMsgActorCreated	: 1;

		// Determines how creation affects nearby objects.

		U32						isShell				: 1;
	} flags;
} WorldCollObject;

typedef struct WorldCollSceneObjectNode WorldCollSceneObjectNode;

typedef struct WorldCollSceneObjectNode {
	WorldCollSceneObjectNode*				next;
	WorldCollSceneObjectNode*				prev;
	U32										listIndex : 1;
	WorldCollScene*							scene;
	AssociationNode*						alNode;
	PSDKActor*								psdkActor;
} WorldCollSceneObjectNode;

typedef struct WorldCollScene {
	WorldCollIntegration*					wci;

	PSDKScene*								psdkScene;
	
	char*									name;
	
	WorldCollActor**						actors;

	WorldCollFluid**						fluids;

	U32										materialSetCount;
	
	struct {
		AssociationList*					alPrimaryWCOs;
		StashTable							st;
		WorldCollSceneObjectNode*			nodes[2];
		U32									activeNodeListIndex			: 1;
	} wcos;
	
	struct {
		U32									useVariableTimestep			: 1;
		U32									isUpdatingWorldCollObjects	: 1;
	} flags;
} WorldCollScene;

typedef struct WorldCollActor {
	WorldCollScene*							scene;
	
	void*									userPointer;

	PSDKActor*								psdkActor;
} WorldCollActor;

typedef struct WorldCollFluid {
	WorldCollScene*							scene;
	void*									userPointer;
	PSDKFluid*								psdkFluid;
} WorldCollFluid;

typedef struct WorldCollGridCellThreadData {
	struct {
		U32									lockFlags;

		struct {
			U32								needsSceneCreate		: 1;
		} flags;
	} toFG;
} WorldCollGridCellThreadData;

typedef struct WorldCollStaticScene WorldCollStaticScene;

typedef struct WorldCollScenePlacement {
	WorldColl*								wc;

	union {
		WorldCollGridCell**					cellsMutable;
		WorldCollGridCell*const*const		cells;
	};

	WorldCollStaticScene*					ss;
	IVec3									gridOffset;
	Vec3									sceneOffset;

	struct {
		U32									hasOffset				: 1;
		U32									needsRecheckGridOffset	: 1;
	} flags;
} WorldCollScenePlacement;

typedef struct WorldCollStaticScene {
	union {
		PSDKScene*							psdkSceneMutable;
		PSDKScene*const						psdkScene;
	};

	union {
		WorldCollScenePlacement**			placementsMutable;
		WorldCollScenePlacement*const*const	placements;
	};

	union {
		U32									wcoCountMutable;
		const U32							wcoCount;
	};

	union {
		U32									cellCountMutable;
		const U32							cellCount;
	};

	U32										materialSetCount;

	struct {
		U32									needsToSimulate				: 1;
		U32									spNeedsRecheckGridOffset	: 1;
	} flags;
} WorldCollStaticScene;

typedef struct WorldCollGridCell {
	WorldColl*								wc;

	IVec3									gridPos;
	IVec3									worldPos;

	S32										size;

	union {
		WorldCollScenePlacement*			placementMutable;
		WorldCollScenePlacement*const		placement;
	};

	U32										wcoCount;
	AssociationList*						alPrimaryStaticWCOs;
	AssociationList*						alPrimaryDynamicWCOs;

	WorldCollGridCellThreadData				threadData[2];

	struct {
		U32									wcoStaticNeedsActorRefresh	: 1;
		U32									wcoDynamicNeedsActorRefresh	: 1;
		U32									needsSceneCreate			: 1;
	} flags;
} WorldCollGridCell;

typedef struct WorldCollMaterial {
	S32										index;
	#if !PSDK_DISABLED
		PSDKMaterialDesc					materialDesc;
	#endif
	U32										updated : 1;
} WorldCollMaterial;

typedef struct WorldCollGrid {
	WorldCollGridCell**						cellsXZ;
	IVec3									lo;
	IVec3									hi;
	IVec3									size;
	
	struct {
		U32									newCellToSendToBG : 1;
	} flags;
} WorldCollGrid;

typedef struct WorldCollIntegration {
	WorldCollIntegrationMsgHandler			msgHandler;
	void*									userPointer;
	char*									name;
	
	WorldCollScene**						scenes;
	
	struct {
		struct {
			U32								inListBG				: 1;
		} flags;
	} fg;
} WorldCollIntegration;

typedef struct WorldCollFG {
	WorldCollGrid							grid;

	struct {
		U32									cellNeedsSceneCreate	: 1;
		U32									cellNeedsActorRefresh	: 1;
		U32									wcoDynamicChangedBounds	: 1;
	} flags;
} WorldCollFG;

typedef struct WorldCollBG {
	U32										threadID;

	WorldCollIntegration**					wcis;

	struct {
		WorldCollObject**					active;
	} wcos;
	
	WorldCollGrid							grid;
} WorldCollBG;

typedef struct WorldCollThreadData {
	struct {
		U32									lockFlags;
		
		struct {
			U32								cellNeedsSceneCreate	: 1;
		} flags;
	} toFG;
} WorldCollThreadData;

typedef struct WorldColl {
	WorldCollFG								fg;
	WorldCollBG								bg;
	
	WorldCollThreadData						threadData[2];

	union {
		WorldCollObject**					wcosDynamicMutable;
		WorldCollObject*const*const			wcosDynamic;
	};
	
	union {
		WorldCollScenePlacement**			placementsMutable;
		WorldCollScenePlacement*const*const	placements;
	};
} WorldColl;

typedef struct WorldCollGlobalStateThreadShared {
	HANDLE									eventStartFrameToBG;
	HANDLE									eventFrameFinishedToFG;
	S32										threadShouldDestroySelf;
	F32										deltaSecondsPerStep;
	U32										stepCount;
	F32										deltaSeconds;
} WorldCollGlobalStateThreadShared;

typedef struct WorldCollGlobalState {
	ManagedThread*							managedThread;
	WorldCollGlobalStateThreadShared		threadShared;
	
	union {
		WorldColl**							wcsMutable;
		WorldColl*const*const				wcs;
	};
	
	#if !PSDK_DISABLED
		PSDKSimulationOwnership*			psdkSimulationOwnership;
	#endif
	
	WorldCollMaterial**						materials;

	StashTable								stStoredModelData;

	S32										assertOnBadGridIndex;
	
	EventOwner*								eventTimer;

	AssociationListType						alTypeCellWCOs;
	AssociationListType						alTypeWCOCells;
	AssociationListType						alTypeWCOScenes;
	AssociationListType						alTypeSceneWCOs;
	
	#if WORLDCOLL_VERIFY_ACTORS
		StashTable							stActors;
		StashTable							stOwnerToActor;
	#endif

	struct {
		U32									threadID;
		
		struct {
			U32								maxActors;
			U32								staticPruningType;
			U32								dynamicPruningType;
			U32								subdivisionLevel;
			U32								maxBoundXYZ;
		} sceneConfig;	

		union {
			WorldCollIntegration**				wcisMutable;
			WorldCollIntegration*const*const	wcis;
		};

		WorldCollStaticScene**				staticScenes;
		
		struct {
			U32								simulating					: 1;

			U32								wciListUpdatedToBG			: 1;

			U32								connectToDebugger			: 1;
			
			U32								ssNeedsToSimulate			: 1;
			U32								ssNeedsToSplit				: 1;
			U32								ssRecreateAll				: 1;

			U32								spNeedsRecheckGridOffset	: 1;
		} flags;
	} fg;
	
	struct {
		union {
			WorldCollIntegration**				wcisMutable;
			WorldCollIntegration*const*const	wcis;
		};

		union {
			WorldCollScene**					scenesMutable;
			WorldCollScene*const*const			scenes;
		};
	} bg;

	struct {
		U32									fgSlot						: 1;
		U32									bgSlot						: 1;

		U32									initialized					: 1;
		U32									notThreaded					: 1;
		U32									needsMaterialUpdate			: 1;
		
		U32									printSceneInfoOnSwap		: 1;

		U32									actorChangeMsgsDisabled		: 1;
	} flags;
} WorldCollGlobalState;

extern WorldCollGlobalState wcgState;

// WorldColl.c -------------------------------------------------------------------------------------

S32		wcWorldCoordToGridIndex(F32 coord,
								F32 gridBlockSize);

S32		wcGridGetCellByGridPos(	WorldCollGrid* g,
								WorldCollGridCell** cellOut,
								S32 x,
								S32 z);

S32		wcGetCellByGridPosFG(	WorldColl* wc,
								WorldCollGridCell** cellOut,
								S32 x,
								S32 z);

S32		wcGetOrCreateCellByGridPosFG(	WorldColl* wc,
										WorldCollGridCell** cellOut,
										S32 x,
										S32 z,
										const char* reason);

void	wcGridCellActorDestroy(	WorldCollGridCell* cell,
								WorldCollObject* wco,
								AssociationNode* node,
								PSDKActor** psdkActorInOut);

void	wcCellSetActorRefresh(	WorldCollGridCell* cell,
								S32 isDynamicRefresh);

S32		wcNotificationsEnabled(void);

// Iterators.

typedef void (*ForEachWorldCollCallback)(	WorldColl* wc,
											void* userPointer);

void	wcForEachWorldColl(	ForEachWorldCollCallback callback,
							void* userPointer);

typedef void (*ForEachStaticSceneCallback)(	WorldCollStaticScene* ss,
											void* userPointer);

void	wcForEachStaticScene(	ForEachStaticSceneCallback callback,
								void* userPointer);

typedef void (*ForEachWorldCollIntegrationCallback)(WorldCollIntegration* wci,
													void* userPointer);

void	wcForEachIntegration(	ForEachWorldCollIntegrationCallback callback,
								void* userPointer);

#if WORLDCOLL_VERIFY_ACTORS
	typedef enum WorldCollPSDKActorOwnerType {
		WC_PSDK_ACTOR_OWNER_INVALID,
		WC_PSDK_ACTOR_OWNER_STATIC,
		WC_PSDK_ACTOR_OWNER_SCENE_STATIC,
		WC_PSDK_ACTOR_OWNER_SCENE_DYNAMIC,
	} WorldCollPSDKActorOwnerType;

	void wcPSDKActorTrackCreate(void* owner,
								WorldCollPSDKActorOwnerType ownerType,
								AssociationListIterator* iter,
								PSDKActor* psdkActor);

	void wcPSDKActorTrackIncrement(	void* owner,
									WorldCollPSDKActorOwnerType ownerType,
									AssociationListIterator* iter,
									PSDKActor* psdkActor);

	void wcPSDKActorTrackDecrement(	void* owner,
									WorldCollPSDKActorOwnerType ownerType,
									AssociationListIterator* iter,
									PSDKActor* psdkActor);

	void wcPSDKActorTrackDestroy(	void* owner,
									WorldCollPSDKActorOwnerType ownerType,
									AssociationListIterator* iter,
									PSDKActor* psdkActor);

	void wcPSDKActorTrackPrintOwners(PSDKActor* psdkActor);
#else
	#define wcPSDKActorTrackCreate(owner, ownerType, iter, psdkActor)
	#define wcPSDKActorTrackIncrement(owner, ownerType, iter, psdkActor)
	#define wcPSDKActorTrackDecrement(owner, ownerType, iter, psdkActor)
	#define wcPSDKActorTrackDestroy(owner, ownerType, iter, psdkActor)
	#define wcPSDKActorTrackPrintOwners(psdkActor)
#endif

// WorldCollObject.c -------------------------------------------------------------------------------

void	wcoGetDebugString(	WorldCollObject* wco,
							char* buffer,
							S32 bufferLen);

S32		wcoIsInStaticScene(	const WorldCollObject* wco,
							const WorldCollStaticScene* ss);

S32		wcoFindActorInScenePlacement(	const WorldCollObject* wco,
										const WorldCollScenePlacement* ss,
										PSDKActor** psdkActorOut);

void	wcoSetActorInScenePlacement(WorldCollObject*const wco,
									WorldCollGridCell*const cellToIgnore,
									WorldCollScenePlacement*const sp,
									const PSDKActor*const psdkActorFrom,
									PSDKActor*const psdkActorTo);

S32		wcoCreateCellActor(	WorldCollObject* wco,
							WorldCollGridCell* cell,
							AssociationNode* node);

void	wcoUpdateGridFG(WorldColl* wc,
						WorldCollObject* wco,
						const Vec3 aabbMin,
						const Vec3 aabbMax);

