#pragma once
GCC_SYSTEM

typedef U32 dtDrawSkeleton;
typedef U32 dtSkeleton;
typedef U32 dtFxManager;
typedef U32 dtNode;
typedef U32 EntityRef;

typedef U16 DynBit;

typedef struct WLCostume WLCostume;
typedef struct DynSkeleton DynSkeleton;
typedef struct DynDrawSkeleton DynDrawSkeleton;
typedef struct DynBitField DynBitField;

typedef void (*DynSkeletonChangeFunc   )(DynSkeleton* skeleton, const char* name);
typedef void (*DynSkeletonChangeFuncId )(DynSkeleton* skeleton, const char* name, U32 uid);
typedef void (*DynSkeletonChangeFuncF32U32)(DynSkeleton* skeleton, F32 f, U32 u);

typedef struct DynSkeletonPreUpdateParams {
	DynSkeleton*					skeleton;
	DynBitField*					targetBitField;
	void*							userData;
	U32								callCountOnThisFrame;
	U32								doReset : 1;
	
	struct {
		DynSkeletonChangeFuncId		startGraph;
		DynSkeletonChangeFuncId		startDetailGraph;
		DynSkeletonChangeFuncId		playFlag;
		DynSkeletonChangeFuncId		playDetailFlag;
		DynSkeletonChangeFunc		setStance;
		DynSkeletonChangeFunc		clearStance;
		DynSkeletonChangeFuncF32U32	setOverrideTime;
	} func;
} DynSkeletonPreUpdateParams;

typedef S32 (*DynSkeletonPreUpdateFunc)(const DynSkeletonPreUpdateParams* params);

typedef S32 (*DynSkeletonRagdollStateFunc)(DynSkeleton* pSkeleton,
											void* userData);

typedef S32 (*DynSkeletonGetAudioDebugInfoFunc)(DynSkeleton *pSkeleton);

#define LOWEST_GUID 100
#define ADD_TO_TABLE(newPtr, stTable, uiGUID ) if (!stashIntAddPointer(stTable, uiGUID, newPtr, false)) FatalErrorf("stTable guid %d is not unique!", uiGUID); 
#define ADVANCE_GUID(guid) guid = ((guid+1)>LOWEST_GUID)?(guid+1):LOWEST_GUID

void dtAnimInitSys(void);

DynSkeleton* dynSkeletonFromGuid(dtSkeleton guidSkel);
DynDrawSkeleton* dynDrawSkeletonFromGuid(dtDrawSkeleton guidDrawSkel);

void dynDrawSkeletonResetDrawableLists();

void dynSkeletonRemoveGuid(dtSkeleton guidSkel);
void dynDrawSkeletonRemoveGuid(dtDrawSkeleton guidDrawSkel);

dtSkeleton dtSkeletonCreate(const WLCostume* pCostume, EntityRef uiEntRef, bool bLocalPlayer, dtFxManager guidFxMan, dtNode guidLocation, dtNode guidRoot);
void dtSkeletonDestroy(dtSkeleton guidSkel);
void dtSkeletonSetCallbacks(	dtSkeleton guid,
								void* preUpdateData,
								DynSkeletonPreUpdateFunc preUpdateFunc,
								DynSkeletonRagdollStateFunc ragdollStateFunc,
								DynSkeletonGetAudioDebugInfoFunc getAudioDebugInfoFunc	);
void dtSkeletonChangeCostume(ACMD_FORCETYPE(U32) dtSkeleton guid, const WLCostume* pCostume);
void dtSkeletonSetTarget(dtSkeleton guid, bool bEnable, Vec3 vTargetPos);
void dtSkeletonSetSendDistance(dtSkeleton guid, F32 fSendDistance);
U32 dtSkeletonIsRagdoll(dtSkeleton guid);
void dtSkeletonEndDeathAnimation(dtSkeleton guid);

void dtSkeletonDisallowRagdoll(dtSkeleton guid);

dtDrawSkeleton dtDrawSkeletonCreate(dtSkeleton guidSkel, const WLCostume* pCostume, dtFxManager guidFxMan, bool bAutoDraw, F32 fSendDistance, bool bIsLocalPlayer);
void dtDrawSkeletonDestroy(dtDrawSkeleton guidDrawSkel);
void dtDrawSkeletonSetDebugFlag(dtDrawSkeleton guidDrawSkel);
void dtDrawSkeletonSetAlpha( dtDrawSkeleton guid, F32 fAlpha, F32 fGeometryOnlyAlpha );
void dtNodeSetParentBone(dtNode guid, dtSkeleton parentSkel, dtSkeleton dependentSkel, const char *boneName, DynBit attachBit);

const char* dtCalculateHitReactDirectionBit(const Mat3 mFaceSpace, const Vec3 vDirection);
