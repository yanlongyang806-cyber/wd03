
#pragma once
GCC_SYSTEM

#include "DynNode.h"
#include "dynFxEnums.h"
#include "dynDraw.h"
#include "WorldLibEnums.h"

//#define CHECK_FX_COUNT assert(dynFxManCountAll() == dynFxDebugFxCount())
#define CHECK_FX_COUNT

#define DYNFX_TRACKPARAMBLOCKS 0

typedef U32 DynFxTime;
#define DYNFXTIME(fTime) ((DynFxTime)(round(fTime * 1000000)))
#define FLOATTIME(uiTime) ( ((F32)uiTime) * 0.000001f )
/*
typedef F32 DynFxTime;
#define DYNFXTIME(fTime) fTime
#define FLOATTIME(uiTime) uiTime
*/

//#define DYN_FX_NORMALIZED_COLORS 1



extern StaticDefineInt ParseDynBlendMode[];
typedef struct DynFx DynFx;
typedef struct DynFxInfo DynFxInfo;
typedef struct DynNode DynNode;
typedef struct DynParticle DynParticle;
typedef struct DynFxFastParticleSet DynFxFastParticleSet;
typedef struct BasicTexture BasicTexture;
typedef struct Material Material;
typedef struct DynParamBlock DynParamBlock;
typedef struct DynDrawSkeleton DynDrawSkeleton;
typedef struct WLCostume WLCostume;
typedef struct WLDynDrawParams WLDynDrawParams;
typedef struct Frustum Frustum;
typedef struct DynFxMessage DynFxMessage;
typedef struct WorldRegion WorldRegion;
typedef struct DynSkeleton DynSkeleton;
typedef struct DynClothObject DynClothObject;
typedef struct DynDrawParticle DynDrawParticle;
typedef struct WorldFXEntry WorldFXEntry;
typedef struct GfxSplat GfxSplat;
typedef struct DynForcePayload DynForcePayload;
typedef struct GfxOcclusionBuffer GfxOcclusionBuffer;
typedef struct RdrDrawableParticle RdrDrawableParticle;
typedef struct PoolQueue PoolQueue;
typedef struct DynAnimChartRunTime DynAnimChartRunTime;
typedef struct DynJitterList DynJitterList;
typedef struct DynFxCreateParams DynFxCreateParams;

typedef U32 dtFxManager;

typedef U32 (*dynJitterListSelectionCallback)(DynJitterList* pJList, void* pUserData);

typedef enum eDynBitType eDynBitType;

//typedef enum LightType LightType;

typedef enum DynParticleType DynParticleType;

typedef enum eDebugSortMode eDebugSortMode;

typedef enum eFxManagerType eFxManagerType;


typedef struct DynFxMaintained
{
	REF_TO(DynFxInfo) hInfo;
	REF_TO(DynFx) hFx;
	F32 fHue;
	eDynFxSource eSource;
	dtNode targetGuid;
	DynParamBlock *paramblock;
} DynFxMaintained;

typedef struct DynFxSuppressor
{
	const char* pcTag;
	F32 fAmount;
	bool bSuppressed;
} DynFxSuppressor;

typedef struct DynFxIKTarget
{
	const char* pcTag;
	REF_TO(const DynNode) hIKNode;
	REF_TO(const DynFx) hFX;
} DynFxIKTarget;

typedef struct DynFxManager
{
	int					iPartitionIdx;
	DynFx**				eaDynFx;
	DynNode*			pNode;
	DynBitField			bits;
	DynNode*			pDummyTargetNode;
	DynNode*			pFakeExtentsNode;
	const DynNode*		pTargetNode;
	DynDrawSkeleton*	pDrawSkel;
	WorldFXEntry*		pCellEntry;
	eFxManagerType		eType;
	DynFxMaintained**	eaMaintainedFx;
	REF_TO(WLCostume)	hCostume;
	dtFxManager			guid;
	F32					fCameraDistanceSquared;
	F32					fCostumeFXHue;
	WorldRegion*		pWorldRegion;
	StashTable			stUniqueFX;
	DynFxSuppressor**	eaSuppressors;
	DynFxIKTarget**		eaIKTargets;
	DynFxRef**			eaFxToKill;
	dtFxManager			targetManagerGuid;
	bool				bPermanentRegion;
	bool				bRegionChangedThisFrame;
	bool				bAlwaysKillIfOrphaned;
	bool				bLocalPlayer;
	bool				bDoesntSelfUpdate;
	bool				bWaitingForSkelUpdate;
	bool				bEnemyFaction;
	bool				bLocalPlayerBased;
	unsigned			bNoSound : 1;
	U32					bSplatsInvalid : 2;
	DynFxCreateParams** eaAutoRetryFX;
} DynFxManager;


typedef void (*DynFxOperatorFunc)(DynFx* pFx, void* pUserData);
typedef int (*SparseGridOcclusionCallback)(GfxOcclusionBuffer *zo, const Vec4 eyespaceBounds[8], int isZClipped);

typedef struct DynDebugBit
{
	const char* pcBitName;
	F32 fTimeSinceSet; // negative means still set
} DynDebugBit;

typedef struct DynMeshTrailDebugInfo
{
	REF_TO(DynFx) hFx;
	Vec3 vPos;
	Vec3 vDir;
	F32* eafPoints;
} DynMeshTrailDebugInfo;

#define DYN_DEBUG_MAX_BIT_DISPLAY_TIME 5.0f

// String, color, and position for displaying FX information in the world.
typedef struct DynFxDebugWorldSpaceMessage {
	Vec3 vPos;
	char *pcMessage;
	Color color;
	F32 fTimeLeft;
	F32 fStartTime;
} DynFxDebugWorldSpaceMessage;

typedef struct DynDebugState
{
	bool bDrawSkeleton;
	bool bDrawSkeletonAxes;
	bool bDrawSkeletonNonCritical;
	bool danimShowBits;
	bool danimShowBitsHideMainSkeleton;
	bool danimShowBitsShowSubSkeleton;
	bool danimShowBitsHideHead;
	bool danimShowBitsHideMainSequencer;
	bool danimShowBitsShowSubSequencer;
	bool danimShowBitsShowOverlaySequencer;
	bool danimShowBitsShowTrackingIds;
	bool danimShowBitsHideMovement;
	bool audioShowAnimBits;
	bool costumeShowSkeletonFiles;
	bool bDrawNumFx;
	bool bDrawNumSkels;
	bool bDrawTrailTris;
	bool bNoNewFx;
	bool bNoAnimation;
	bool bAutoClearTrackers;
	bool bEditorHasOpened;
	bool bFxNotWireframe;
	bool bPrintBoneUnderMouse;
	bool bLockDebugSkeleton;
	bool bDrawWindGrid;
	bool bAnimLODOff;
	bool bLODPlayer;
	bool bFxLogging;
	bool bFxLogState;
	bool bDrawVisibilityExtents;
	bool bDrawCollisionExtents;
	bool bDrawRagdollDataGfx;
	bool bDrawRagdollDataAnim;
	bool bDrawSplatCollision;
	bool bDrawFxTransforms;
	bool bDrawMeshTrailDebugInfo;
	bool bDrawFxVisibility;
	bool bDrawSkyVolumes;
	bool bDrawRays;
	bool bDrawBouncers;
	bool bDrawBodysockAtlas;
	bool bFxDrawOnlySelected;
	bool bFastParticleForceUpdate;
	bool bPIXLabelParticles;
	bool bNoCameraFX;
	bool bDebugIK;
	bool bDrawIKTargets;
	bool bBreakOnDebugFx;
	bool bDrawImpactTriggers;
	bool bRecordFXProfile;
	bool bDebugPhysics;
	bool bDebugTailorSkeleton;
	bool bTooManyFastParticlesEnvironment;
	bool bTooManyFastParticles;

	bool bLabelDebugFX;
	bool bLabelAllFX;

	bool bNoCostumeFX;
	bool bNoEnvironmentFX;
	bool bNoUIFX;

	F32 fDrawWindGridForceYLevel;

	bool bReloadAnimData;
	bool bReloadServerAnimData;
	U32 uiAnimDataResetFrame;

	struct
	{
		bool bClothDebug;
		bool bDrawNormals;
		bool bDrawTanSpace;
		bool bDrawCollision;
		bool bTessellateAttachments;
		DynClothObject* pDebugClothObject;
		bool bDisableCloth;

		bool bDisablePartialCollision;
		bool bDisablePartialControl;
		bool bAllBonesAreFullySkinned;
		bool bLerpVisualsWithBones;
		bool bJustDrawWithSkinnedPositions;
		bool bDrawClothAsNormalGeo;
		bool bForceCollideToSkinnedPosition;
		bool bForceNoCollideToSkinnedPosition;
		bool bDebugStiffBoneSelection;

		bool bDisableNormalModelForLowLOD;

		int iMaxLOD;

		float fMaxMovementToSkinnedPos;

	} cloth;

	const DynNode* pBoneUnderMouse;
	F32 fDynFxRate;
	F32 fAnimRate;

	U32 uiNumDebris;
	U32 uiNumFastParticles;
	U32 uiNumFastParticleSets;

	U32 uiNumFxSoundStarts;
	U32 uiNumFxSoundMoves;

	U32 uiNumAllocatedFastParticlesEnvironment;
	U32 uiNumAllocatedFastParticlesEntities;
	U32 uiNumAllocatedFastParticleSets;

	U32 uiNumPhysicsObjects;
	U32 uiNumExcessivePhysicsObjectsFX;

	struct 
	{
		// total fx drawn by owner
		U32 uiNumDrawnFx;
		U32 uiNumDrawnDebris;
		U32 uiNumDrawnCostumeFx;

		// total fx drawn by type
		U32 uiNumDrawnFastParticles; // number of quads
		U32 uiNumDrawnFastParticleSets; // number of draw calls
		U32 uiNumDrawnSlowParticles;
		U32 uiNumBatchedSlowParticles; // number of draw calls saved by sort bucket batching
		U32 uiNumDrawnCylinderTrails;
		U32 uiNumDrawnTriStrips;
		U32 uiNumDrawnSkinnedGeoParticles;
		U32 uiNumDrawnGeoParticles;
		U32 uiNumInstancedGeoParticles; // number of draw calls saved by instancing

		// total character draw calls by type
		U32 uiNumDrawnSkinnedModels;
		U32 uiNumDrawnModels;
		U32 uiNumInstancedModels; // number of draw calls saved by instancing
		U32 uiNumDrawnClothMeshes;

		U32 uiNumDrawnVisualPassSkels;
		U32 uiNumDrawnShadowPassSkels;
	} frameCounters;

	S32 iFxQuality;

	S32 iNumAnimThreads;
	bool bAnimThreadsDefaultChanged;

	U32 uiNumCulledSkels;
	U32 uiNumUpdatedSkels[MAX_WORLD_REGION_LOD_LEVELS];
	U32 uiNumBonesUpdated[MAX_WORLD_REGION_LOD_LEVELS];
	U32 uiNumAnimatedBones;
	U32 uiNumSkinnedBones;
	U32 uiNumSeqDataCacheMisses[16];
	U32 uiAnimCacheSize;
	U32 uiAnimCacheSizeUsed;

	U32 uiMaxPriorityDrawn;
	U32 uiMaxDrawn;
	F32 fTestHue;
	F32 fTestSaturation;
	F32 fTestValue;
	bool bGlobalHueOverride;
	bool bFxDebugOn;

	bool bDrawTorsoPointing;
	bool bDisableTorsoPointing;
	bool bEnableOldAnimTorsoPointingFix;
	bool bDisableTorsoPointingFix;
	bool bDisableAutoBanking;

	bool bDrawTerrainTilt;
	bool bDisableTerrainTilt;
	bool bDisableTerrainTiltOffset;

	bool bDisableClientSideRagdoll;
	bool bDisableClientSideRagdollInitialVelocities;

	bool bNoMovementSync;

	DynSkeleton* pDebugSkeleton;
	DynDebugBit** eaSkelBits;
	const char **eaDebugStances;
	DynFxManager* pTestManager;
	DynFxManager* pDefaultTestManager;
	DynMeshTrailDebugInfo** eaMeshTrailDebugInfo;
	DynFxDebugWorldSpaceMessage **eaFXWorldSpaceMessages;

} DynDebugState;

extern DynDebugState dynDebugState;


typedef struct DynSoundEvent 
{
	const char *pcEventName;
	int guid;
} DynSoundEvent;

typedef struct DynSoundDSP
{
	int guid;
	const char *pcDSPName;
} DynSoundDSP;

typedef struct DynSoundUpdater
{
	DynSoundEvent ** eaSoundEvents;
	DynSoundDSP **eaSoundDSPs;
	U32			playedSound	: 1;
} DynSoundUpdater;

typedef enum DynBlendMode
{
	DynBlendMode_Normal = 0,
	DynBlendMode_Additive,
	DynBlendMode_Subtractive,
} DynBlendMode;


typedef enum DynParticleType
{
	DynParticleType_Sprite = 0,
	DynParticleType_Geometry,
	DynParticleType_MeshTrail,
};

typedef enum DynMeshTrailMode
{
	DynMeshTrail_None = 0,
	DynMeshTrail_Normal,
	DynMeshTrail_CamOriented,
	DynMeshTrail_Cylinder,
} DynMeshTrailMode;

typedef enum DynStreakMode
{
	DynStreakMode_None = 0,
	DynStreakMode_Velocity,
	DynStreakMode_Parent,
	DynStreakMode_ScaleToTarget,
	DynStreakMode_Chain,
} DynStreakMode;

typedef enum eDynOrientMode
{
	DynOrientMode_Camera,
	DynOrientMode_Local,
	DynOrientMode_ZAxis,
	DynOrientMode_LockToX,
	DynOrientMode_LockToY,
} eDynOrientMode;

typedef enum eMessageTypes
{
	eMessage_None,
	eMessage_Start,
	eMessage_Last,
} eMessageTypes;

AUTO_ENUM;
typedef enum eSkyFalloffType
{
	eSkyFalloffType_Linear, ENAMES(Linear)
	eSkyFalloffType_None,	ENAMES(None)
} eSkyFalloffType;


extern DynFxMessage gStartMessage;
extern DynFxMessage gKillMessage;


typedef struct DynDrawable
{
	const char*			pcTextureName;
	const char*			pcTextureName2;
	const char*			pcModelName;
	const char*			pcClothName;
	const char*			pcClothInfo;
	const char*			pcClothCollisionInfo;
	const char*			pcMaterialName;
	const char*			pcGeoDissolveMaterialName;
	const char**		ppcGeoAddMaterialNames;
	const char*			pcMaterial2Name;
	const char*			pcBoneName;
	const char*			pcBaseSkeleton;
	const char*			pcBoneForCostumeModelGrab;
	BasicTexture*		pTexture; 
	BasicTexture*		pTexture2; 
	Material*			pMaterial; 
	Vec4				color;
	Vec3				vHSVShift;
	Vec3				vScale;
	DynBlendMode		blend;
	DynStreakMode		streakMode;
	F32					fStreakScale;
	Vec3				vStreakDirection;
	Vec3				vDeltaPos;
	Vec3				vPos;
	Vec3				vVel;
	Quat				qRot;
	Vec3				vSpin;
	Vec2				vTexOffset;
	Vec2				vTexScale;
	F32					fDrag;
	F32					fTightenUp;
	F32					fGravity;
	F32					fSpriteOrientation;
	F32					fSpriteSpin;
	F32					fGoToSpeed;
	F32					fGoToGravity;
	F32					fGoToGravityFalloff;
	F32					fGoToApproachSpeed;
	F32					fGoToSpringEquilibrium;
	F32					fGoToSpringConstant;
	F32					fParentVelocityOffset;
	F32					fPhysRadius;
	Vec4				vColor1;
	Vec4				vColor2;
	Vec4				vColor3;
	eDynOrientMode		eOriented;
	const char*			pcAtName;
	bool				bHInvert;
	bool				bVInvert;
	bool				bFixedAspectRatio;
	bool				bLocalOrientation;
	bool				bVelocityDriveOrientation;
	bool				bStreakTile;
	bool				bCastShadows;
	bool				bClothCollide;
	bool				bClothCollideSelfOnly;
	bool				bExplicitlyInstanceable;
	eDynEntityLightMode eEntityLightMode;
	eDynEntityTintMode  eEntityTintMode;
	eDynEntityScaleMode eEntityScaleMode;
	eDynEntityMaterialMode eEntityMaterialMode;
	const char*			pcTexWords;
	Vec3				vClothWindOverride;
	bool				bUseClothWindOverride;
	F32					fLightModulation;
	Vec3				vDrawOffset;
} DynDrawable;


typedef struct DynFlare
{
	eDynFlareType	eType;
	F32				*size;					// Screen size scale of the flare piece. 1 indicates the width of the screen, 0 indicates no extent and intermediate values scale between these values.
	Vec3			hsv_color;				// HSV color tint.
	F32				*position;				// Relative position along the line between the light source on screen and the center of the screen. 0 indicates at the light source, 1 indicates the point opposite the light source across the center of the screen, and intermediate values indicate positions along the line. Values outside [0,1] indicate values past either end of the line.
	const char**	ppcMaterials;
} DynFlare;

typedef struct DynLight
{
	LightType eLightType;
	Vec3 vSpecularHSV;
	Vec3 vDiffuseHSV;
	F32 fRadius;
	F32 fInnerRadiusPercentage;
	F32 fInnerConeAngle;
	F32 fOuterConeAngle;
	bool bCastShadows;
} DynLight;



typedef struct DynMeshTrailUnit
{
	Vec3 vPos;
	Vec3 vOrientation;
	F32 fAge;
	F32 fTexCoord;
} DynMeshTrailUnit;


typedef struct DynMeshTrailKey
{
	F32 fTime;
	F32 fWidth;
	Vec4 vColor;
} DynMeshTrailKey;

typedef struct DynMeshTrailInfo
{
	DynMeshTrailKey		keyFrames[4];
	F32					fFadeInTime;
	F32					fFadeOutTime;
	F32					fTexDensity;
	F32					fEmitRate;
	F32					fEmitDistance;
	F32					fEmitSpeedThreshold;
	const char*			pcTextureName;
	BasicTexture*		pTexture;
	DynMeshTrailMode	mode;
	bool				bStopEmitting;
	bool				bSubFrameCurve;
	F32					fMinForwardSpeed;
	Vec3				vCurveDir;
	F32					fShiftSpeed;
} DynMeshTrailInfo;

typedef struct DynOperator DynOperator;

typedef struct DynCameraInfo
{
	F32				fShakePower;
	F32				fShakeRadius;
	F32				fShakeVertical;
	F32				fShakePan;
	F32				fShakeSpeed;
	F32				fCameraFOV;
	bool			bAttachCamera;
	F32				fCameraInfluence;
	F32				fCameraDelaySpeed;
	F32				fCameraDelayDistanceBasis;
	const char*		pcCameraLookAtNode; 
	F32				fCameraLookAtSpeed;
} DynCameraInfo;


typedef struct DynSplat
{
	eDynSplatType eType;
	F32 fSplatRadius;
	F32 fSplatInnerRadius;
	F32 fSplatLength;
	bool bForceDown;
	bool bUpdateScale;
	bool bCenterLength;
	GfxSplat* pGfxSplat;
	F32 fSplatFadePlanePt;
	const char *pcSplatProjectionBone;
	REF_TO(DynNode) hSplatProjectionNode;
	bool bDisableCulling;

} DynSplat;

typedef struct DynSkyVolume
{
	const char** ppcSkyName; 
	F32 fSkyRadius;
	F32 fSkyLength;
	F32 fSkyWeight;
	eSkyFalloffType eSkyFalloff; 
} DynSkyVolume;

typedef struct DynFxControlInfo
{
	F32 fTimeScale;
	bool bTimeScaleChildren;
} DynFxControlInfo;

typedef struct DynObject
{
	DynNode* node;
	DynDrawable draw;
	DynLight light;
	DynMeshTrailInfo meshTrail;
	DynCameraInfo cameraInfo;
	DynSplat splatInfo;
	DynSkyVolume skyInfo;
	DynFlare flare;
	DynFxControlInfo controls;
	const char* pcObjectTag;
} DynObject;

typedef struct DynMeshTrail 
{
	PoolQueue* qUnits;
	DynMeshTrailInfo meshTrailInfo;
	Vec3 vLastFramePos;
	Vec3 vLastFrameDir;
	Vec3 vLastFrameNormal;
	F32 fTrailAge;
	F32 fEmitStartAge;
	F32 fAccum;
	U8 uiNumKeyFrames;
	bool bStop;
} DynMeshTrail;

AUTO_STRUCT;
typedef struct DynFxLogState
{
	DynTransform xform;
	DynDrawParticle* pDraw;
} DynFxLogState;

AUTO_STRUCT;
typedef struct DynFxLogLine
{
	U32 uiGuid;
	F32 fTime;
	const char* pcFXInfo; AST(POOL_STRING)
	char* pcFXID; AST(ESTRING)
	char* esLine; AST(ESTRING)
	DynFxLogState* pState;
} DynFxLogLine;

typedef struct DynFxLog
{
	DynFxLogLine** eaLines;
} DynFxLog;


typedef struct DynNearMessage
{
	const char* pcMessage;
	Vec3 vNearPos;
	F32 fDistanceSquared;
} DynNearMessage;

typedef struct DynFxRegion
{
	DynFxManager** eaDynFxManagers;
	DynFxManager* pGlobalFxManager;
	DynFxManager* pDebrisFxManager;
	DynFxFastParticleSet** eaOrphanedSets;

	DynFx** eaFXToDraw;

	DynForcePayload** eaForcePayloads[2];
	DynNearMessage** eaNearMessagePayloads[2];
	U8 uiCurrentPayloadArray; // Double buffered

	bool bInitalized;
} DynFxRegion;

typedef struct DynFxSortBucket
{
	RdrDrawableParticle** eaCachedDrawables;
	const char* pcGroupTexturesTag;
	int iRefCount;
} DynFxSortBucket;

DynFxLog fxLog;


DynFxManager* dynFxManCreate( DynNode* pParentNode, WorldRegion* pRegion, WorldFXEntry* pCellEntry, eFxManagerType eType, dtFxManager guid, int iPartitionIdx, bool bLocalPlayer, bool bNoSound);
void dynFxManDestroy( SA_PRE_NN_VALID SA_POST_FREE DynFxManager* pFxManager );


typedef struct DynAddFxParams
{
	DynFx* pParent;
	DynFx* pSibling;
	DynParamBlock* pParamBlock;
	const DynNode* pTargetRoot;
	const DynNode* pSourceRoot;
	F32 fHue;
	F32 fSaturation;
	F32 fValue;
	DynFxSortBucket* pSortBucket;
	U32 uiHitReactID;
	eDynFxSource eSource;
	bool bOverridePriority;
	eDynPriority ePriorityOverride;
	dynJitterListSelectionCallback cbJitterListSelectFunc;
	void* pJitterListSelectData;
} DynAddFxParams;

DynFx* dynAddFx(SA_PARAM_NN_VALID DynFxManager* pFxManager, SA_PARAM_NN_STR const char* pcInfo, SA_PARAM_NN_VALID DynAddFxParams* pParams);
DynFx* dynAddFxAtLocation(DynFxManager* pFxManager, const char* pcFxInfo, DynFx* pParent, DynParamBlock* pParamBlock, const Vec3 vecSource, const Quat sourceQuat, const Vec3 vecTarget, const Quat targetQuat, F32 fHue, F32 fSaturationShift, F32 fValueShift, U32 uiHitReactID, eDynFxSource eSource);
DynFx* dynAddFxFromLocation(SA_PARAM_NN_STR const char* pcFxInfo, DynFx* pParent, DynParamBlock* pParamBlock, const DynNode* pTargetRoot, const Vec3 vecSource, const Vec3 vecTarget, const Quat targetQuat, F32 fHue, F32 fSaturationShift, F32 fValueShift, U32 uiHitReactID, eDynFxSource eSource);

U32 dynFxManGetFxCount(SA_PARAM_NN_VALID DynFxManager* pFxManager);
DynNode* dynFxManGetDynNode( SA_PARAM_NN_VALID DynFxManager* pFxManager);
void dynFxManSetDynNode( DynFxManager* pFxManager, DynNode* pNode);
const DynNode* dynFxManGetDummyTargetDynNode(SA_PARAM_NN_VALID DynFxManager* pFxManager);
int dynGetManagedFxGUID(void);
void dynFxKillManaged(void);

typedef void (*DynParticleForEachFunc)(DynParticle *pParticle, void *user_data);
typedef void (*DynFastParticleForEachFunc)(DynFxFastParticleSet* pSet, void *user_data);
//void dynParticleForEach(DynParticleForEachFunc callback, void *user_data);
void dynFxManForEachParticle(DynFxManager* pFxManager, DynParticleForEachFunc callback, DynFastParticleForEachFunc fastcallback, void *user_data);
void dynParticleForEachInFrustum(DynParticleForEachFunc callback, DynFastParticleForEachFunc fastcallback, WorldRegion* pWorldRegion, WLDynDrawParams *params, Frustum* pFrustum, SparseGridOcclusionCallback occlusionFunc, GfxOcclusionBuffer *pOcclusionBuffer);

void dynFxManUpdateAll(F32 fDeltaTime);
void dynFxManagerUpdate(SA_PARAM_NN_VALID DynFxManager* pFxManager, DynFxTime uiDeltaTime );

// Stop only fx managed by this DynFxManager with the given name.
void dynFxManStopUsingFxInfo(SA_PARAM_OP_VALID DynFxManager* pFxManager, SA_PARAM_OP_STR const char* pcDynFxName, bool bImmediate);


const DynNode* dynCameraNodeGet();


void dynEngineSetTestManager(DynFxManager* pTest);

// Message stuff
void dynPushMessage(DynFx* pFx, const char* pcMessage);
void dynFxManBroadcastMessage(SA_PARAM_NN_VALID DynFxManager* pFxManager, SA_PARAM_NN_VALID const char* pcMessage );
void dynNearMessagePush(DynFxRegion* pFxRegion, const char* pcMessage, const Vec3 vNearPos, F32 fDistance);



U32 dynFxDebugFxCount();

typedef enum eDynFxKillReason
{
	eDynFxKillReason_ExternalKill,
	eDynFxKillReason_ParentDied,
	eDynFxKillReason_ManualKill,
	eDynFxKillReason_ManagerKilled,
	eDynFxKillReason_MaintainedFx,
	eDynFxKillReason_DebrisCountExceeded,
	eDynFxKillReason_DebrisNoNode,
	eDynFxKillReason_RequiresNode,
	eDynFxKillReason_DebrisDistanceExceeded,
	eDynFxKillReason_Error,
} eDynFxKillReason;
bool dynFxKill(DynFx* pFx, bool bImmediate, bool bRemoveFromOwner, bool bOrphanChildren, eDynFxKillReason eReason);

F32 dynFxGetMaxDrawDistance(const char* pcFxName);

void dynFxManagerPushFxArray( SA_PARAM_NN_VALID DynFxManager* pFxManager, SA_PARAM_NN_VALID DynFx*** peaFx );

DynParamBlock* dynParamBlockCreateEx(const char *reason, int lineNum);
DynParamBlock* dynParamBlockCopyEx(SA_PARAM_NN_VALID const DynParamBlock *pBlock, const char *reason, int lineNum);

#if DYNFX_TRACKPARAMBLOCKS
#define dynParamBlockCreate() dynParamBlockCreateEx(__FILE__, __LINE__)
#define dynParamBlockCopy(pBlock) dynParamBlockCopyEx(pBlock, __FILE__, __LINE__)
#else
#define dynParamBlockCreate() dynParamBlockCreateEx(NULL, 0)
#define dynParamBlockCopy(pBlock) dynParamBlockCopyEx(pBlock, NULL, 0)
#endif

void dynParamBlockFree( SA_PARAM_NN_VALID DynParamBlock* pToFree);

DynDrawSkeleton* dynFxManagerGetDrawSkeleton(SA_PARAM_NN_VALID DynFxManager* pFxMan);
void dynFxManSetDrawSkeleton(DynFxManager* pFxMan, DynDrawSkeleton* pDrawSkel);
WorldFXEntry* dynFxManagerGetCellEntry( SA_PARAM_NN_VALID DynFxManager* pFxMan);
void dynFxManSetTestTargetNode( SA_PARAM_NN_VALID DynFxManager* pFxManager, SA_PARAM_OP_VALID const DynNode* pTargetNode, dtFxManager targetManager);
const DynNode* dynFxManGetTestTargetNode(SA_PARAM_NN_VALID DynFxManager* pFxManager);
DynNode *dynFxManGetExtentsNode(DynFxManager *pFxManager);
void dynFxManagerGatherWorldFX();
void dynFxManSetKeepAlive(SA_PARAM_NN_VALID DynFxManager* pFxManager, SA_PARAM_OP_STR const char* pcKeepAlive);
void dynFxManagerRemoveFxFromList(SA_PARAM_NN_VALID DynFxManager* pFxManager, DynFx* pFx);
const char* dynFxManSwapFX(DynFxManager* pFxManager, SA_PARAM_NN_STR const char* pFxInfo, bool bCheckLocalPlayerBased);
void dynFxRegionInit(DynFxRegion* pFxRegion, WorldRegion* pWorldRegion);
void dynFxRegionDestroy(DynFxRegion* pFxRegion);
void dynFxRegionWipeOrphanedFastParticleSets(DynFxRegion* pFxRegion);
void worldRegionClearGrid(WorldRegion* pRegion);
void dynFxManagerInit(void);
void dynFxForEachFx(DynFxOperatorFunc func, void* pUserData);
void dynFxKillAllFx(void);
void dynFxTestExclusionTagForAll(const char* pcExclusionTag);
int dynFxManCountAll(void);
void dynFxDeleteOrDebris(DynFx* pFx);
void dynFxManAddMaintainedFX(DynFxManager* pFxManager, const char* pcFxName, DynParamBlock *paramblock, F32 fHue, dtNode targetGuid, eDynFxSource eSource);
bool dynFxManRemoveMaintainedFX(DynFxManager* pFxManager, const char* pcFxName, bool bHardKill);
void dynFxManClearAllMaintainedFX(DynFxManager* pFxManager, bool bHardKill);
void dynFxManRemoveMaintainedFXByIndex(DynFxManager* pFxManager, int iIndex, bool bHardKill);
void dynFxManSendMessageMaintainedFx(DynFxManager* pFxManager, const char* pcFxName, SA_PARAM_NN_VALID DynFxMessage** ppMessages);
void dynFxManUpdateMaintainedFX(DynFxManager* pFxManager);
void dynFxManSetCostume(DynFxManager* pFxManager, const WLCostume* pCostume);
DynFxManager* dynFxGetGlobalFxManager(const Vec3 vPos);
DynFxManager* dynFxGetDebrisFxManager(DynFxManager* pFxManager);
DynFxManager* dynFxGetUiManager(bool b3D);
void dynFxSetScreenShake(F32 fPower, F32 fVertical, F32 fRotation, F32 fSpeed);
void dynFxSetCameraMatrixOverride(Mat4 xCameraMatrix, F32 fInfluence);
void dynFxSetWaterAgitate(F32 fPower);
void dynFxManSetCostumeFXHue(DynFxManager* pFxManager, F32 fHue);
F32 dynFxManGetCostumeFXHue(DynFxManager* pFxManager);
const DynNode* dynFxNodeByName(const char* pcName, DynFx* pFx);
DynFxRegion* dynFxManGetDynFxRegion( DynFxManager* pFxManager);
bool dynFxManChangedThisFrame(DynFxManager* pFxManager);
void dynFxManSetRegion(DynFxManager* pFxManager, WorldRegion* pRegion);
bool dynFxManUniqueFXCheck(DynFxManager* pFxMan, const DynFxInfo* pFxInfo);
bool dynFxManUniqueFXRemove(DynFxManager* pFxMan, const DynFxInfo* pFxInfo);
bool dynFxManIsLocalPlayer(SA_PARAM_NN_VALID DynFxManager* pFxMan);
bool dynFxManDoesntSelfUpdate(SA_PARAM_NN_VALID DynFxManager* pFxMan);
SA_RET_OP_VALID dtFxManager dynFxManGetGUID(SA_PARAM_OP_VALID DynFxManager* pManager);
void dynFxForEachChild(DynFx* pFx, DynFxOperatorFunc func, void* pUserData);
void dynFxKillFxAfterUpdate(SA_PARAM_NN_VALID DynFx* pFx);
void dynFxManagerRemoveFromGrid(DynFxManager *pFxManager);

SA_RET_NN_VALID DynFxSortBucket* dynSortBucketCreate(void);
SA_RET_OP_VALID DynFxSortBucket* dynSortBucketIncRefCount(SA_PARAM_OP_VALID DynFxSortBucket* pSortBucket);
void dynSortBucketDecRefCount(SA_PRE_OP_VALID SA_POST_P_FREE DynFxSortBucket* pSortBucket);
void dynSortBucketClearCache(void);

const char* dynPriorityLookup(int iValue);
const char* dynFxTypeLookup(int iValue);
void dynFxClearLog(void);

void dynFxSetMaxDebris(int iMax);

void dynFxManSuppress(SA_PARAM_NN_VALID DynFxManager* pFxMan, SA_PARAM_NN_VALID const char* pcTag);
void dynFxManAddIKTarget(DynFxManager* pFxMan, const char* pcTag, const DynNode* pIKNode, const DynFx* pFX);
U32 dynFxManNumIKTargets(DynFxManager* pFxMan, const char* pcTag);
const DynNode* dynFxManFindIKTarget(DynFxManager* pFxMan, const char* pcTag);
const DynNode* dynFxManFindIKTargetByPos(DynFxManager* pFxMan, const char* pcTag, U32 pos);
U32 dynSortBucketCount(void);

#define COUNTER_TO_INVALIDATE_SPLATS	2

__forceinline static void dynFxManInvalidateSplats(SA_PARAM_NN_VALID DynFxManager* pFxMan)
{
	pFxMan->bSplatsInvalid = COUNTER_TO_INVALIDATE_SPLATS;
}

void dynMeshTrailDebugInfoFree(DynMeshTrailDebugInfo* pDebugInfo);
void dynCameraNodeUpdate(void);


// FX debugging messages (world-space labels for info about FX, messages, and such).
void dtAddWorldSpaceMessage(const char *pcMessage, Vec3 vPos, F32 fTime, float r, float g, float b);
void dtDestroyWorldSpaceMessage(DynFxDebugWorldSpaceMessage * pMessage);
void dtIterateWorldSpaceMessages(F32 fTime);
void dynFxAddWorldDebugMessageForFX(DynFx *pFx, const char *pcMessage, F32 fTime, float r, float g, float b);

