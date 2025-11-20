
#pragma once
GCC_SYSTEM

#include "dynNode.h"
#include "dynFxManager.h"
#include "wlModel.h"

typedef struct DynEvent DynEvent;
typedef struct DynEventUpdater DynEventUpdater;
typedef struct DynFx DynFx;
typedef struct BasicTexture BasicTexture;
typedef struct Material Material;
typedef struct DynBaseSkeleton DynBaseSkeleton;
typedef struct DynFxFastParticleSet DynFxFastParticleSet;
typedef struct DynBitFieldGroup DynBitFieldGroup;
typedef struct DynAnimWordFeed DynAnimWordFeed;
typedef struct WorldCollObject WorldCollObject;
typedef struct DynParticleEmitterRef DynParticleEmitterRef;
typedef struct DynParticleEmitterRef DynParticleEmitterRef;
typedef struct DynMNCRename DynMNCRename;
typedef struct DynSkeleton DynSkeleton;
typedef struct DynDrawSkeleton DynDrawSkeleton;
typedef struct DynPhysicsObject DynPhysicsObject;
typedef struct WorldDrawableList WorldDrawableList;
typedef struct WorldInstanceParamList WorldInstanceParamList;
typedef U64 TexHandle;

typedef struct DynFxStaticPathPoint
{
	union
	{
		const char* pcString;
		int iInt;
		bool bBool;
		void* data;
	};
	U8 uiKeyFrameIndex;
} DynFxStaticPathPoint;

typedef struct DynFxStaticPath
{
	DynFxStaticPathPoint* pPathPoints;
	U8 uiNumPathPoints;
	U8 uiTokenIndex;
	U8 uiCurrentPathPointIndex;
	U8 uiDataSize;
} DynFxStaticPath;


typedef struct DynFxDynamicPathPoint
{
	// 32
	U8 uiKeyFrameIndex;
	U8 uiWhichFloat;
	U8 uiInterpType;
	// 32
	F32 fStartV;
	// 32
	F32 fDiffV;
} DynFxDynamicPathPoint;


typedef struct DynFxDynamicPath
{
	// 32
	DynFxDynamicPathPoint* pPathPoints; // one per-keyframe, every keyframe (for now). If vec3 or quatpyr, we multiply by that by the size
	// 32
	DynFxTime uiKeyTime; // Time past current keyframe
	// 32
	U8 uiTokenIndex;
	U8 uiFloatsPer;
	U8 uiNumPathPoints; // can get num keyframes by dividing numpathpoints by floatsper
	U8 uiCurrentPathPointIndex;
	// 32
	U8 uiHasValuePath;
	U8 uiEDO;
	U8 uiUpdateMask;
} DynFxDynamicPath;


typedef struct DynFxPathSet
{
	DynFxStaticPath* pStaticPaths; // should be sorted by token index... also, valueonly tokens are always first
	DynFxDynamicPath* pDynamicPaths; // should be sorted by token index
	U32 uiNumDynamicPaths;
	U32 uiNumStaticPaths;
	U32 uiTotalSize;
} DynFxPathSet;


typedef struct DynParticleHeader
{
	U16 uiOffset;
	U8 uiTokenIndex;
	U8 uiSize;
} DynParticleHeader;

AUTO_STRUCT;
typedef struct DynDrawParticle
{
	// the dynNode appears to have been embedded here since DynParticleHeaders can create
	// memory references to it in dynParticleCreate
	// the dynNode must appear 1st (so the quat will be memaligned)
	DynNode					node;
	Vec3					vVelocity;
	Vec3					vSpin;
	Vec4					vColor;
	Vec3					vHSVShift;
	F32						fSpriteOrientation;
	F32						fSpriteSpin;
	F32						fDrag;
	F32						fGravity;
	F32						fTightenUp;
	int						iBlendMode;
	F32						fStreakScale;
	int						iStreakMode;
	Vec3					vStreakDir;
	Vec3					vScale;
	Vec2					vTexOffset;
	Vec2					vTexScale;
	const char*				pcTextureName;
	const char*				pcTextureName2;
	const char*				pcMaterialName;
	const char*				pcGeoDissolveMaterialName;
	const char**			ppcGeoAddMaterialNames;
	const char*				pcMaterial2Name;
	const char*				pcModelName;
	const char*				pcClothName;
	const char*				pcClothInfo;
	const char*				pcClothCollisionInfo;
	const char*				pcBoneForCostumeModelGrab;
	DynSkeleton*			pSkeleton; NO_AST
	DynDrawSkeleton*		pDrawSkeleton; NO_AST
	DynBitFieldGroup*		pBitFeed; NO_AST
	DynAnimWordFeed*		pAnimWordFeed; NO_AST
	Model*					pModel; NO_AST
	ModelLoadTracker		modelTracker; NO_AST
	BasicTexture*			pTexture; NO_AST
	BasicTexture*			pTexture2; NO_AST
	Material*				pMaterial; NO_AST
	Material*				pGeoDissolveMaterial; NO_AST
	Material*				pMaterial2; NO_AST
	Material**				eaGeoAddMaterials; NO_AST
	TexHandle				hTexHandle; NO_AST
	TexHandle				hTexHandle2; NO_AST
	bool					bUseSpriteSpin;
	DynFlare*				pDynFlare; NO_AST
	DynLight*				pDynLight; NO_AST
	DynMeshTrail*			pMeshTrail; NO_AST
	DynCameraInfo*			pCameraInfo; NO_AST
	DynFxControlInfo*		pControlInfo; NO_AST
	DynClothObject*			pCloth; NO_AST
	DynSplat*				pSplat; NO_AST
	DynSkyVolume*			pSkyVolume; NO_AST
	DynPhysicsObject*		pDPO; NO_AST
	DynNode**				eaSkinChildren; NO_AST
	const char*				pcBaseSkeleton;
	const DynBaseSkeleton*	pBaseSkeleton; NO_AST
	F32						fGoToSpeed;
	F32						fGoToGravity;
	F32						fGoToGravityFalloff;
	F32						fGoToApproachSpeed;
	F32						fGoToSpringEquilibrium;
	F32						fGoToSpringConstant;
	F32						fParentVelocityOffset;
	F32						fHueShift;
	F32						fSaturationShift;
	F32						fValueShift;
	Vec4					vColor1;
	Vec4					vColor2;
	Vec4					vColor3;
	int						iEntLightMode;
	int						iEntMaterial;
	int						iEntTintMode;
	int						iEntScaleMode;
	eDynOrientMode			eOriented; AST(INT)
	bool					bHInvert;
	bool					bVInvert;
	bool					bFixedAspectRatio;
	bool					bLocalOrientation;
	bool					bVelocityDriveOrientation;
	bool					bStreakTile;
	bool					bCastShadows;
	bool					bClothCollide;
	bool					bClothCollideSelfOnly;
	bool					bExplicitlyInstanceable;
	WorldDrawableList*		pDrawableList; NO_AST
	WorldInstanceParamList*	pInstanceParamList; NO_AST
	const char*				pcTexWords;
	Vec3					vClothWindOverride;
	bool					bUseClothWindOverride;
	F32						fLightModulation;
	const char**			eaExtraTextureSwaps; // Used only with GetModelFromCostumeBone
	F32						fTimeSinceLastDraw; NO_AST
	Vec3					vDrawOffset;
} DynDrawParticle;

/*

This struct previously allocated into a block big enough for it's pointers (the entries and data)... this has a variable size,
but keeps it altogether in memory. The layout is:

DynParticleStuff
DynDrawParticle (note that the address of this is considered the start of data)
pEntries points after the data

That layout has now been broken into 2 parts
(i)
	DynParticleStuff
(ii)
	DynDrawParticle
	extra entry data
	pEntries info

*/

typedef struct DynParticle
{
	DynParticleHeader* pEntries;
	U32 uiNumEntries;
	U32 uiDataSize;
	F32 fZScaleTo; // for the "ScaleToNode"
	F32 fFadeOut;
	Vec3 vWorldSpaceVelocity;
	Vec3 vOldPos;
	F32 fDistTraveled;
	F32 fGravityVel;
	F32 fDensity;
	F32 fVisibilityRadius;
	DynFxFastParticleSet** eaParticleSets;
	DynMNCRename***  peaMNCRename;
	int	 iVisibilityClipValue;
	bool bVelCalculated;
	bool bMultiColor;  // If false, use vColor for colors 1-3 in the material constants
	bool bOcclusionChecked;
	bool bLowRes;
	// the draw/data must be last!
	// changing this to be a pointer instead so that I can make it memaligned
	union
	{
		DynDrawParticle *pDraw;
		char *pData;
	};
} DynParticle;


void dynFxCreatePathSetFromEvent(SA_PARAM_NN_VALID DynFxPathSet* pPathSet, SA_PARAM_NN_VALID const DynEvent* pEvent, const DynParamBlock* pParamBlock, const DynFx* pParentFx);
DynEventUpdater* dynFxCreateEventUpdater( SA_PARAM_NN_VALID const DynEvent* pEvent, SA_PARAM_NN_VALID DynFx* pFx );
bool dynEventUpdaterUpdate( int iPartitionIdx, SA_PARAM_NN_VALID DynFx* pFx, SA_PARAM_NN_VALID DynEventUpdater* pUpdater, DynFxTime uiDeltaTime );
DynParticle* dynParticleCreate( SA_PARAM_NN_VALID const DynEventUpdater* pUpdater, F32 fHueShift, F32 fSaturationShift, F32 fValueShift, DynFxSortBucket* pSortBucket, bool bDontDraw, bool bLowRes, const DynEvent **eaAllEvents);
const char* dynParticleGetTextureName( DynParticle* pParticle );
const char* dynParticleGetMaterialName( SA_PARAM_NN_VALID DynParticle* pParticle );
void dynFxParticlePrepDrawParticle(DynParticle* pParticle, DynDrawParticle* pDraw);
void initDynObjectToDynDrawParticleTokenMap(void);
void initDynObjectToDynFlareTokenMap(void);
void initDynObjectToDynLightTokenMap(void);
void initDynObjectToDynCameraInfoTokenMap(void);
void initDynObjectToDynSplatTokenMap(void);
void initDynObjectToDynSkyVolumeTokenMap(void);
void initDynObjectToDynFxControlInfoTokenMap(void);
void dynParticleCopyToDynFlare(SA_PARAM_NN_VALID DynParticle* pParticle, SA_PARAM_NN_VALID DynFlare* pFlare);
void dynParticleCopyToDynLight(SA_PARAM_NN_VALID DynParticle* pParticle, SA_PARAM_NN_VALID DynLight* pLight);
void dynParticleCopyToDynCameraInfo( DynParticle* pParticle, SA_PARAM_NN_VALID DynCameraInfo* pCameraInfo);
void dynParticleCopyToDynFxControlInfo( DynParticle* pParticle, SA_PARAM_NN_VALID DynFxControlInfo* pControlInfo);
void dynParticleCopyToDynSplat( DynParticle* pParticle, SA_PARAM_NN_VALID DynSplat* pSplat);
void dynParticleCopyToDynSkyVolume( DynParticle* pParticle, SA_PARAM_NN_VALID DynSkyVolume* pSkyVolume);
void dynParticleCopyToDynMeshTrail(SA_PARAM_NN_VALID DynParticle* pParticle, SA_PARAM_NN_VALID DynMeshTrail* pMeshTrail);
void initDynObjectToDynMeshTrailInfoTokenMap(void);
void dynFxKillAllSounds(DynSoundUpdater* pUpdater);
DynFx* dynFxFindChildrenWithBoneNameEx( SA_PARAM_NN_VALID DynFx* pFx, SA_PARAM_NN_STR const char* pcBoneName, SA_PARAM_OP_STR const char* pcAliasName );
#define dynFxFindChildrenWithBoneName(pFx, pcBoneName) dynFxFindChildrenWithBoneNameEx(pFx, pcBoneName, NULL)

// Find a better place for these?
typedef int (*dynFxSoundGetGuidFunc)(DynFx *dynFx);
typedef void (*dynFxSoundCreateFunc)(int guid, Vec3 pos, char *event_name, char *file_name);
typedef void (*dynFxSoundDestroyFunc)(int guid);
typedef void (*dynFxSoundCleanFunc)(int guid);
typedef void (*dynFxSoundMoveFunc)(int guid, Vec3 new_pos, Vec3 new_vel, Vec3 new_dir);
typedef void (*dynFxDSPCreateFunc)(int guid, const char* dspname, char* file_name);
typedef bool (*dynFxDSPDestroyFunc)(int guid);
typedef void (*dynFxDSPCleanFunc)(int guid);
void dynFxSetSoundStartFunc(dynFxSoundCreateFunc func);
void dynFxSetSoundStopFunc(dynFxSoundDestroyFunc func);
void dynFxSetSoundCleanFunc(dynFxSoundCleanFunc func);
void dynFxSetSoundMoveFunc(dynFxSoundMoveFunc func);
void dynFxSetSoundGuidFunc(dynFxSoundGetGuidFunc func);
void dynFxSetDSPStartFunc(dynFxDSPCreateFunc func);
void dynFxSetDSPStopFunc(dynFxDSPDestroyFunc func);
void dynFxSetDSPCleanFunc(dynFxDSPCleanFunc func);
void dynEventUpdaterClear(DynEventUpdater* pUpdater, DynFx* pFx);
U32 dynEventUpdaterHasSounds(DynEventUpdater* pUpdater);

void dynSoundStart( DynFx* pFx, SA_PARAM_NN_VALID DynSoundUpdater* pUpdater, const char* pcSoundStart );
void dynSoundStop( DynSoundUpdater* pSoundUpdater, const char* pcSoundEnd );
void dynSoundMove(DynSoundUpdater* pSoundUpdater, DynFx* pFx);
void dynFxCallEmitterStarts(DynParticle* pParticle, DynParticleEmitterRef*** peaEmitterStarts, F32 fHueShift, F32 fSaturationShift, F32 fValueShift, DynNode* pLocation, int iPriorityLevel, DynFx* pFx);
void dynFxCallEmitterStops(DynParticle* pParticle, DynParticleEmitterRef*** peaEmitterStops, DynFx* pFx);
