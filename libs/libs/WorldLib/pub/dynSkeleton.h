#pragma once
GCC_SYSTEM

#include "referencesystem.h"
#include "resourceManager.h"

#include "dynAnimGraphPub.h"
#include "dynAnimInterface.h"
#include "dynBitField.h"
#include "dynRagdoll.h"
#include "dynRagdollData.h"
#include "dynSequencer.h"

#include "wlSkelInfo.h"


/******************

A DynBaseSkeleton is a generic skeleton corresponding to a certain name. This is copied onto DynSkeleton's when they are created,
to initialize the skeleton tree correctly.

A DynSkeleton is the current state of an animated object. It includes the various local and world space transforms, which anim is currently
playing, and how far along it is on that animation. This will probably change as the sequencer is formed.

******************/

typedef struct DynNode DynNode;
typedef struct DynDrawSkeleton DynDrawSkeleton;
typedef struct DynAnimTrack DynAnimTrack;
typedef struct DynPhysicsObject DynPhysicsObject;
typedef struct SkelInfo SkelInfo;
typedef struct WLCostume WLCostume;
typedef struct WorldCollObject WorldCollObject;
typedef struct WorldRegion WorldRegion;
typedef struct DynBouncerUpdater DynBouncerUpdater;
typedef struct DynAnimGraphUpdater DynAnimGraphUpdater;
typedef struct DynFxManager DynFxManager;
typedef struct StashTableImp* StashTable;
typedef struct DynMovementBlock DynMovementBlock;
typedef struct GenericLogReceiver GenericLogReceiver;
typedef struct WorldPerfFrameCounts WorldPerfFrameCounts;
typedef struct SkelBoneVisibilitySetInfo SkelBoneVisibilitySetInfo;
typedef struct SkelBoneVisibilitySets SkelBoneVisibilitySets;
typedef struct DynStrand DynStrand;
typedef struct DynGroundRegLimb DynGroundRegLimb;
typedef struct DynAnimExpressionSet DynAnimExpressionSet;
typedef struct DynAnimExpressionRuntimeData DynAnimExpressionRuntimeData;
typedef struct DynAnimChartStack DynAnimChartStack;

typedef U32 dtSkeleton;
typedef U32 EntityRef;

#define MAX_SKELETON_LOD_LEVEL 3

#define DEFAULT_VISIBILITY_EXTENT_SCALEFACTOR 1.2f

extern U32 uiMaxBonesPerSkeleton;

extern const char *pcNodeNameWepL;
extern const char *pcNodeNameWepR;

typedef struct	DefineContext DefineContext;
extern DefineContext *g_pDefineSkelBoneVisSets;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineSkelBoneVisSets);
typedef enum SkelBoneVisibilitySet
{
	kSkelBoneVisSet_None = -1, ENAMES(None)
	kSkelBoneVisSet_FIRST_DATA_DEFINED, EIGNORE
} SkelBoneVisibilitySet;


AUTO_STRUCT;
typedef struct SkelBoneVisibilitySetInfo
{
	const char* pchName;			AST(KEY POOL_STRING STRUCTPARAM)
	const char* pchReplaceSet;		AST(POOL_STRING)
	const char** eaHideBoneNames;	AST(POOL_STRING NAME("HideBone"))
	bool bOverrideAlways;
	bool bOverlay;
} SkelBoneVisibilitySetInfo;

AUTO_STRUCT;
typedef struct SkelBoneVisibilitySets
{
	SkelBoneVisibilitySetInfo** ppSetInfo;	AST(NAME("VisSet"))
} SkelBoneVisibilitySets;

extern StaticDefineInt SkelBoneVisibilitySetEnum[];

AUTO_STRUCT;
typedef struct DynBaseSkeleton
{
	const char*					pcName;			AST(KEY)	// The name of this base skeleton
	DynNode*					pRoot;						// The transform and hierarchy information
	StashTable					stBoneTable;				// For quickly finding a bone (much faster than full tree search)
	F32							fHipsHeightAdjustmentFactor; // calculated to make the feet stick on the ground when you scale the legs
	U32							uiNumBones;
	const char*					pcFileName;
} DynBaseSkeleton;										// The default instantiation state of a certain skeleton type

typedef struct DynAnimBoneInfo
{
	Vec3 vBaseOffset;
	DynTransform xBaseTransform; // 10
	int iSeqIndex;
	int iSeqAGUpdaterIndex;
	int iOverlayIndex;
	int iOverlayAGUpdaterIndex;
	bool bIgnoreMasterOverlay;
	bool bMovement;
} DynAnimBoneInfo;

#define MAX_SCALE_MODIFIERS 8
#define MAX_SCALE_MODIFIER_BONES 16

typedef struct DynFxDrivenScaleModifier
{
	const char* apcBoneName[MAX_SCALE_MODIFIER_BONES];
	U32 uiNumBones;
	Vec3 vScale;
} DynFxDrivenScaleModifier;

typedef enum EMovementSpeed
{
	EMovementSpeed_Stop,
	EMovementSpeed_Walk,
	EMovementSpeed_Trot,
	EMovementSpeed_Run,
} EMovementSpeed;

typedef enum ETargetDir
{
	ETargetDir_Forward,
	ETargetDir_Back,
	ETargetDir_Left,
	ETargetDir_Right,
	ETargetDir_LeftForward,
	ETargetDir_RightForward,
	ETargetDir_LeftBack,
	ETargetDir_RightBack,
} ETargetDir;


typedef enum ETurnState
{
	ETurnState_None,
	ETurnState_Left,
	ETurnState_Right,
} ETurnState;

typedef struct DynScaleCollection
{
	U32							uiNumTransforms;
	DynTransform*				pLocalTransforms;
	StashTable					stBoneLookup;
	F32							fHipsHeightAdjustmentFactor;
} DynScaleCollection;

typedef struct DynMovementBlock DynMovementBlock;

typedef struct DynMovementState
{
	DynAnimChartStack* pChartStack;
	DynMovementBlock** eaBlocks;
	const char *pcDebugMoveOverride;
} DynMovementState;

typedef enum DynSkeletonStanceSetType {
	DS_STANCE_SET_PRE_UPDATE,
	DS_STANCE_SET_BASE,
	DS_STANCE_SET_DEBUG,
	DS_STANCE_SET_GRAPH,
	DS_STANCE_SET_COSTUME,
	DS_STANCE_SET_AUDIO,
	DS_STANCE_SET_CUTSCENE,
	DS_STANCE_SET_RAGDOLL,
	DS_STANCE_SET_FX_FEED,
	DS_STANCE_SET_CRITTER,
	DS_STANCE_SET_COUNT,
} DynSkeletonStanceSetType;

#define DS_STANCE_SET_CONTAINS_MOVEMENT(x) (x == DS_STANCE_SET_PRE_UPDATE || x == DS_STANCE_SET_DEBUG)

typedef struct DynSkeletonAnimOverride DynSkeletonAnimOverride;


#define DDNAS_KEYWORD_MAXTIME 5.0f
#define DDNAS_FLAG_MAXTIME 5.0f
#define DDNAS_FX_MAXTIME 5.0f
#define DDNAS_FX_MAXSAVECOUNT 64
#define DDNAS_STANCE_STATE_NEW -1
#define DDNAS_STANCE_STATE_CURRENT 0
#define DDNAS_STANCE_STATE_OLD 1
#define DDNAS_STANCE_MAXTIME_NEW 2.0f
#define DDNAS_STANCE_MAXTIME_OLD 5.0f
#define DDNAS_ANIMGRAPH_MOVE_MAXCOUNT 7
#define DDNAS_MOVEMENTBLOCK_MAXCOUNT 7

extern ParseTable parse_DynSkeletonDebugKeyword[];
#define TYPE_parse_DynSkeletonDebugKeyword DynSkeletonDebugKeyword
AUTO_STRUCT;
typedef struct DynSkeletonDebugKeyword
{
	const char *pcKeyword;
	U32 uid;
	F32 fTimeSinceTriggered;
} DynSkeletonDebugKeyword;

extern ParseTable parse_DynSkeletonDebugFlag[];
#define TYPE_parse_DynSkeletonDebugFlag DynSkeletonDebugFlag
AUTO_STRUCT;
typedef struct DynSkeletonDebugFlag
{
	const char *pcFlag;
	U32 uid;
	F32 fTimeSinceTriggered;
} DynSkeletonDebugFlag;

extern ParseTable parse_DynSkeletonDebugStance[];
#define TYPE_parse_DynSkeletonDebugStance DynSkeletonDebugStance
AUTO_STRUCT;
typedef struct DynSkeletonDebugStance
{
	const char *pcStanceName;
	int iState;
	F32 fTimeInState;
	F32 fTimeActive;
} DynSkeletonDebugStance;

extern ParseTable parse_DynSkeletonDebugGraphNode[];
#define TYPE_parse_DynSkeletonDebugGraphNode DynSkeletonDebugGraphNode
AUTO_STRUCT;
typedef struct DynSkeletonDebugGraphNode
{
	const char *pcGraphName;
	const char *pcNodeName;
	const char *pcMoveName;
	const char *reason;
	const char *reasonDetails;
} DynSkeletonDebugGraphNode;

extern ParseTable parse_DynSkeletonDebugFX[];
#define TYPE_parse_DynSkeletonDebugFX DynSkeletonDebugFX
AUTO_STRUCT;
typedef struct DynSkeletonDebugFX
{
	const char *pcFXName;
	F32  fTimeSinceTriggered;
	bool bPlayed; AST(NAME("played"))
} DynSkeletonDebugFX;

extern ParseTable parse_DynSkeletonDebugGraphUpdater[];
#define TYPE_parse_DynSkeletonDebugGraphUpdater DynSkeletonDebugGraphUpdater
AUTO_STRUCT;
typedef struct DynSkeletonDebugGraphUpdater
{
	DynSkeletonDebugKeyword		**eaKeywords;
	DynSkeletonDebugFlag		**eaFlags;
	DynSkeletonDebugGraphNode	**eaNodes;
	DynSkeletonDebugFX			**eaFX;
} DynSkeletonDebugGraphUpdater;

extern ParseTable parse_DynSkeletonDebugMovementBlock[];
#define TYPE_parse_DynSkeletonDebugMovementBlock DynSkeletonDebugMovementBlock
AUTO_STRUCT;
typedef struct DynSkeletonDebugMovementBlock
{
	const char *pcName;
	const char *pcType;
	const char *pcTDes;
	const char *pcCDes;
} DynSkeletonDebugMovementBlock;

extern ParseTable parse_DynSkeletonDebugNewAnimSys[];
#define TYPE_parse_DynSkeletonDebugNewAnimSys DynSkeletonDebugNewAnimSys
AUTO_STRUCT;
typedef struct DynSkeletonDebugNewAnimSys
{
	DynSkeletonDebugStance			**eaDebugStances;
	DynSkeletonDebugGraphUpdater	**eaGraphUpdaters;
	DynSkeletonDebugMovementBlock	**eaMovementBlocks;
	DynSkeletonDebugFX				**eaMovementFX;
} DynSkeletonDebugNewAnimSys;

typedef struct DynSkeletonDebugCostumeInfo
{
	const char* pcDebugCostume;
	const char* pcDebugCostumeFilename;
	const char* pcDebugCSkel;
	const char* pcDebugCSkelFilename;
	const char* pcDebugSpecies;
	const char* pcDebugSpeciesFilename;
	const char** eaDebugRequiredClickBoneNames;
	const char** eaDebugOptionalClickBoneNames;
} DynSkeletonDebugCostumeInfo;

extern ParseTable parse_DynSkeletonRunAndGunBone[];
#define TYPE_parse_DynSkeletonRunAndGunBone DynSkeletonRunAndGunBone
AUTO_STRUCT;
typedef struct DynSkeletonRunAndGunBone
{
	const char*	pcRGBoneName;
	const char*	pcRGParentBoneName;
	bool bSecondary;
	F32 fEnableAngle;
	F32 fLimitAngle;
	Quat qPostRoot;
} DynSkeletonRunAndGunBone;

typedef struct DynJointBlend {
	const char *pcName; //of the joint
	Vec3 vOriginalPos;
	Vec3 vPosOffset;
	bool bActive;
	bool bPlayBlendOutWhileActive;
	F32 fBlendInRate;
	F32 fBlendOutRate;
	F32 fBlend;
} DynJointBlend;

#define DAWF_MaxActiveStances	32
#define DAWF_MaxStanceChanges	32
#define DAWF_MaxFlags			32
typedef struct DynStanceAction {
	const char *pcWord;
	bool bSet;
} DynStanceAction;

typedef struct DynAnimWordFeed {
	DynStanceAction pStanceActions[DAWF_MaxStanceChanges];
	const char *pcKeyword;
	const char *ppcFlags[DAWF_MaxFlags];
	const char *ppcStances[DAWF_MaxActiveStances];
	U32 uiNumStanceActions;
	U32 uiNumStances;
	U32 uiNumFlags;
} DynAnimWordFeed;

typedef struct DynSkeletonCachedFxEvent {
	const char *pcName;
	DynAnimGraphUpdater *pUpdater;
	bool bMessage;
} DynSkeletonCachedFxEvent;

typedef struct DynSkeleton
{
	// for this skeleton only, in the genesis skeleton's space
	Vec3						vCurrentExtentsMin;
	Vec3						vCurrentExtentsMax;

	// including this skeleton's children, in the genesis skeleton's space
	Vec3						vCurrentGroupExtentsMin;
	Vec3						vCurrentGroupExtentsMax;

    DynSkeletonPreUpdateFunc	preUpdateFunc;
	DynSkeletonRagdollStateFunc	ragdollStateFunc;
	void*						preUpdateData;

	DynNode*					pLocation;
	DynNode*					pRoot;						// the dynnode tree of our current state
	DynNode*					pExtentsNode;				// USES GROUP EXTENTS, helper node that provides a center point and extents in scale form
	DynNode*					pFacespaceNode;

	//Ground registration & weapon/grip IK nodes (also uses pRoot for the base node)
	DynNode*					pHipsNode;
	DynNode*					pWepLNode; //also used for weapon/grip IK
	DynNode*					pWepRNode; //also used for weapon/grip IK
	DynGroundRegLimb**			eaGroundRegLimbs;

	// Old system

	DynSequencer**				eaSqr;						// The sequencers for this skeleton

	// New system

	DynMovementState			movement;

	union {
		DynAnimGraphUpdater**			eaAGUpdaterMutable;
		DynAnimGraphUpdater*const*const	eaAGUpdater;
	};

	DynAnimChartStack**			eaAnimChartStacks; // these can be shared by multiple graph updaters & the movement state

	const char**				eaStances[DS_STANCE_SET_COUNT];
	const char**				eaStancesCleared;
	F32*						eaStanceTimers[DS_STANCE_SET_COUNT];
	F32*						eaStanceTimersCleared;
	DynSkeletonAnimOverride**	eaAnimOverrides;
	U32							wasInOverride : 1;

	DynSkeletonDebugNewAnimSys* pDebugSkeleton;
	DynSkeletonDebugCostumeInfo debugCostumeInfo;
	DynSkeletonGetAudioDebugInfoFunc getAudioDebugInfoFunc;

	// Visual stuff.
	REF_TO(DynBaseSkeleton)		hBaseSkeleton;				// base skeleton, from which we were created
	DynDrawSkeleton*			pDrawSkel;					// We need this so that when the skeletons are updated, they can create skinning information
	REF_TO(WLCostume)			hCostume;					// Needed for knowing which sequencer types to use, scale values as well as ragdoll info
	DynFxManager*				pFxManager;
	StashTable					stBoneTable;				// For quickly finding a bone (much faster than full tree search)
	//DynBaseSkeleton*			pScaledBase;				// This is our preprocessed, scaled and body-typed base skeleton. There is one of these for every instance of a skeleton
	DynScaleCollection			scaleCollection;

	// For parenting skeletons to eachother
	DynSkeleton**				eaDependentSkeletons;
	DynSkeleton*				pParentSkeleton;
	DynSkeleton*				pGenesisSkeleton; // this will be a self link if no parent, otherwise it'll be the skeleton's parent's parent's ... parent
	DynBit						attachmentBit;

	DynBitField					entityBits;
	// Client side override for entity bits
	DynBitField					entityBitsOverride;
	DynBitField					costumeBits;
	DynBitField					actionFlashBits;
	DynBitField					talkingBits;
	DynBitFieldGroup**			eaBitFieldFeeds; // will usually be empty except for when FX set bits or debug bits
	DynAnimWordFeed**			eaAnimWordFeeds;

	WorldRegion*				pWorldRegion;

	DynPhysicsObject*			pDPO;
	Vec3						vOldPos;
	F32							fDistanceTraveledXZ;
	F32							fCurrentSpeedXZ;
	F32							fDistanceTraveledY;
	F32							fCurrentSpeedY;
	F32							fHeightScale;
	Vec3						vTargetPos;
	F32							fCurrentCamDistanceSquared;
	U32							uiLODLevel;
	U32							uiFrame;
	dtSkeleton					guid;
	DynAnimBoneInfo*			pAnimBoneInfos;
	U32							uiNumAnimBoneInfos;
	DynRagdollState				ragdollState;
	DynBouncerUpdater**			eaBouncerUpdaters;

	const char *				pcClientSideRagdollAnimTrack;
	F32							fClientSideRagdollAnimTime;
	F32							fClientSideRagdollAnimTimePreCollision;
	F32							fClientSideRagdollAdditionalGravity;
	DynAnimTrackHeader *		pClientSideRagdollPoseAnimTrackHeader;
	F32							fClientSideRagdollPoseAnimTrackFrame;
	DynPhysicsObject**			eaClientSideRagdollBodies;

	DynSkeletonCachedFxEvent	**eaCachedAnimGraphFx;
	DynSkeletonCachedFxEvent	**eaCachedSkelMoveFx;

	// Old System
	EMovementSpeed				eMovementSpeed;
	ETargetDir					eTargetDir;
	ETurnState					eTurnState;

	// New System
	F32							fMovementAngle;
	F32							fMovementAngleOld;
	F32							fMovementYawStopped;

	//Run'n'Gun
	DynSkeletonRunAndGunBone	**eaRunAndGunBones;	
	bool						bPreventRunAndGunFootShuffle;
	bool						bPreventRunAndGunUpperBody;
	Vec3						vToTargetWS;
	Vec3						vOldToTargetWS;
	Vec3						vMovementDir;
	Vec3						vOldMovementDir;
	Mat3						mFaceSpace;
	Quat						qFaceSpace;
	Quat						qFaceSpaceInv;
	Quat						qTorso;
	Quat						qPrevLocation;
	F32							fMovementYawFS;
	F32							fMovementYawOffsetFS;
	F32							fMovementYawOffsetFSOld;
	F32							fMovementYawOffsetFSAccum;
	F32							fTimeIdle;
	F32							fFacingPitch;
	F32							fYawInterp;
	int							iYawMovementState;
	F32							fRunAndGunMultiJointBlend;
	
	//Banking
	const char*					pcBankingOverrideNode;
	U32							uiBankingOverrideStanceCount;
	F32							fBankingOverrideTimeActive;	// for use in future extension to let you interpolate between on/off modes (hasn't been needed yet)
	F32							fMovementBankBlendFactor;
	F32							fMovementBank;
	F32							fMovementBankOld;
	Vec2						vMovementPosXZ[3];

	//Terrain pitch setup
	Vec3						vTerrainImpactPosA;
	Vec3						vTerrainImpactPosB;
	Vec3						vTerrainImpactPosC;
	Vec3						vTerrainLook;
	Vec3						vTerrainNorm;
	U32							bTerrainHitPosA:1;
	U32							bTerrainHitPosB:1;
	U32							bTerrainHitPosC:1;

	//Terrain pitch
	F32							fTerrainPitch;
	Quat						qTerrainPitch;
	F32							fTerrainOffsetZ;
	F32							fTerrainTiltBlend;
	//F32						fTerrainHeightBump;
	
	F32							fWepRegisterBlend;
	F32							fIKBothHandsBlend;

	//Joint matching
	DynJointBlend**				eaMatchBaseSkelAnimJoints; //old animation system
	DynJointBlend**				eaAnimGraphUpdaterMatchJoints;
	DynJointBlend**				eaSkeletalMovementMatchJoints;

	//Mounts
	Vec3						vAppliedRiderScale;

	//Strands
	DynStrand**					eaStrands;

	F32							fGroundRegFloorDeltaNear;
	F32							fGroundRegFloorDeltaFar;
	F32							fGroundRegBlendFactor;
	F32							fGroundRegBlendFactorUpperBody;
	F32							fHeightBump;

	F32							fFrozenTimeScale;

	F32							fMovementSyncPercentage;

	F32							fStaticVisibilityRadius; // note this errs on the side of way too big. if you want accurate bounds, use the vCurrentExtentsMin/Max which are calc'd every frame

	union {
		F32						fLowerBodyBlendFactorMutable;
		const F32				fLowerBodyBlendFactor;
	};
	
	// for the movement system taking over the default graph updater when in idle
	union {
		F32						fMovementSystemOverrideFactorMutable; 
		const F32				fMovementSystemOverrideFactor;
	};
	union {
		F32						fMovementSystemOverrideFactorOldMutable;
		const F32				fMovementSystemOverrideFactorOld;
	};

	union {
		F32						fOverrideAllBlendFactorMutable;
		F32						fOverrideAllBlendFactor;
	};

	F32							fTorsoPointingBlendFactor;

	int							iOverrideSeqIndex;

	EntityRef					uiEntRef;

	const char*					pcIKTarget;
	const char*					pcIKTargetNodeLeft;
	const char*					pcIKTargetNodeRight;

	const char					*pcCostumeFxTag;

	int							iOverlaySeqIndex; // expand to array if we want to support more than one

	GenericLogReceiver*			glr;

	int							iVisibilityClipValue;

	const DynAnimExpressionSet	 *pAnimExpressionSet;
	DynAnimExpressionRuntimeData **eaAnimExpressionData;

	U32							bPlayer:1;
	U32							bMovementDirChanged:1;
	U32							bSavedOnCostumeChange:1;
	U32							bHasStrands:1;
	U32							bInitStrands:1;
	U32							bInitStrandsDelay:1;
	U32							bUpdatedGroundRegHips:1;
	U32							bClothMoving:1;
	U32							bMount:1;
	U32							bRider:1;
	U32							bRiderChild:1;
	U32							bInitTerrainPitch:1;
	U32							bHeadshot:1;
	U32							bFrozen:1;
	U32							bCreateClientSideRagdoll:1;
	U32							bCreateClientSideTestRagdoll : 1;
	U32							bHasClientSideRagdoll:1;
	U32							bHasClientSideTestRagdoll:1;
	U32							bSleepingClientSideRagdoll:1;
	U32							bUnmanaged:1; // this flag means that the skeleton is updated manually and doesn't really exist in the world: usually costume editor or headshot skeletons
	U32							bVisible:1;
	U32							bWasVisible:1;
	U32							bForceVisible:1;
	U32							bWasForceVisible:1;
	U32							bVisibilityChecked:1;
	U32							bOcclusionChecked:1;
	U32							bInvalid:1; // used when the costume file is broken, to keep the object around but not try to draw it
	U32							bBodySock:1;
	U32							bEverUpdated:1;
	U32							bHasTarget:1;
	U32							bInheritBits:1;
	U32							bOwnedByParent:1; // if true, when the parent skeleton is destroyed, it must destroy this one too
	U32							bMovementSyncSet:1;
	U32							bIsDragonTurn:1;
	U32							bIsLunging:1;
	U32							bWasLunging:1;
	U32							bIsLurching:1;
	U32							bWasLurching:1;
	U32							bIsRising:1;
	U32							bIsFalling:1;
	U32							bIsJumping:1;
	U32							bLanded:1;
	U32							bIsDead:1;
	U32							bIsNearDead:1;
	U32							bIsPlayingDeathAnim:1;
	U32							bEndDeathAnimation:1;
	U32							bDisallowRagdoll:1;
    U32							bFlying:1;
	U32							bTorsoPointing:1; // true if the skeleton supports torso pointing (in old animation system, this is both pointing of the torso, and allowing multiple sequencers)
	U32							bTorsoDirections:1; //true if the skeleton supports torso pointing without bending, use this to get directions during movement for quads on old games
	U32							bMovementBlending:1; // true if the skeleton supports movement blending (new animation system)
	U32							bUseTorsoPointing:1; // true if we want to actually torso point
	U32							bUseTorsoDirections:1; // true if we want to actually use torso directions
	U32							bDontUpdateExtents:1;
	U32							bForceTransformUpdate:1;
	U32							bActionModeLastFrame:1;
	U32							bHasLastPassIK:1;		//true = the IK needs to run on the last pass since the IK-targets weren't animated at the time the solver was going to be ran
	U32							bIKBothHands:1;			//true = the WepL & WepR nodes are trying to attach to handle bars (or something similar that is not a weapon)
	U32							bRegisterWep:1;			//value set based on same named variable in WepL's current dynMove (or presence of IK Target)
	U32							bIKMeleeMode:1;			//true = melee weapon mode, false = firearm weapon mode
	U32							bEnableIKSliding:1;		//true = left hand allowed to slide between 2 weapon nodes, false = left hand position locked on lower weapon node
	U32							bDisableIKRightArm:1;	//true = disable IK for the right arm, false = enable IK for the right arm
	U32							bDisableIKLeftWrist:1;	//true = left wrist rotation driven by animation, false = left wrist rotation set based on mode
	U32							bOverrideAll:1;
	U32							bSnapOverrideAll:1;
	U32							bSnapOverrideAllOld:1; //same, putting here to eliminate dependencies between anim code for merging
	U32							bOverrideMovement:1;
	U32							bSnapOverrideMovement:1;
	U32							bOverrideDefaultMove:1;
	U32							bSnapOverrideDefaultMove:1;
	U32							bAnimDisabled:1;
	U32							bNotifyChartStackChanged:1;
	U32							bRequestResetOnPreUpdate:1;
	U32							bStartedGraphInPreUpdate:1;
	U32							bStartedKeywordGraph:1;
	U32							bMissingAnimData:1;
	U32							bSnapshot;
	U8							uiRandomSeed;

    U8							uiNumBitFields;
    U8							uiNumScaleModifiers;
    DynBitField					aBitFieldStream[32];
    DynFxDrivenScaleModifier	aScaleModifiers[MAX_SCALE_MODIFIERS];

} DynSkeleton;											// The current state of an animated object, including transforms and animation info

#define GET_ANIM_BASE(pDynSkeleton) (pDynSkeleton->pScaledBase?pDynSkeleton->pScaledBase:GET_REF(pDynSkeleton->hBaseSkeleton))

void dynLoadAllBaseSkeletons(void);
void dynLoadSkelBoneVisibilitySets(void);

SA_RET_OP_VALID DynSkeleton* dynSkeletonCreate(SA_PARAM_NN_VALID const WLCostume* pCostume, bool bLocalPlayer, bool bUnmanaged, bool bDontCreateSequencers, bool bForceNoScale, bool bHeadshot, const char *pcCostumeFxTag);
void dynSkeletonReprocessEx(SA_PARAM_NN_VALID DynSkeleton* pSkeleton, SA_PARAM_NN_VALID const WLCostume* pCostume, bool bIgnoreDependents);
#define dynSkeletonReprocess(pSkeleton, pCostume) dynSkeletonReprocessEx(pSkeleton, pCostume, false)
bool dynSkeletonChangeCostumeEx( DynSkeleton* pSkeleton, const WLCostume* pCostume, bool bIgnoreDependents);
#define dynSkeletonChangeCostume(pSkeleton, pCostume) dynSkeletonChangeCostumeEx(pSkeleton, pCostume, false)
void dynSkeletonUpdateAll(F32 fDeltaTime);
void dynServerUpdateAllSkeletons(F32 fDeltaTime);
void dynSkeletonUpdate(DynSkeleton* pSkeleton, F32 fDeltaTime, WorldPerfFrameCounts *perf_info);
void dynSkeletonCreateSequencers(DynSkeleton* pSkeleton);
void dynSkeletonResetSequencers(DynSkeleton *pSkeleton);

void dynSkeletonFree(SA_PRE_NN_VALID SA_POST_P_FREE DynSkeleton* pToFree);
//const DynNode* dynBaseSkeletonFindNode(SA_PARAM_NN_VALID const DynBaseSkeleton* pBaseSkeleton, SA_PARAM_NN_STR const char* pcTag);
void dynDependentSkeletonFree(SA_PRE_NN_VALID SA_POST_P_FREE DynSkeleton* pDependent);
const DynBaseSkeleton* dynBaseSkeletonFind(SA_PARAM_NN_STR const char* pcBaseSkeletonName);
bool dynSkeletonIsNodeAttached(const DynSkeleton *pSkeleton, const DynNode *pNode);//slow - for debug safety check only
const DynNode* dynSkeletonFindNode(const DynSkeleton* pSkeleton, const char* pcTag);
const DynNode *dynSkeletonFindNodeCheckChildren(const DynSkeleton *pSkeleton, const char *pcTag);
DynNode* dynSkeletonFindNodeNonConst(DynSkeleton* pSkeleton, const char* pcTag);
DynBaseSkeleton* dynScaledBaseSkeletonCreate(const DynSkeleton* pSkeleton);
void dynBaseSkeletonFree(SA_PRE_NN_VALID SA_POST_P_FREE DynBaseSkeleton* pToFree);
void dynScaleCollectionInit(DynBaseSkeleton* pScaledBase, DynScaleCollection* pCollection);
void dynScaleCollectionClear(DynScaleCollection* pCollection);
const DynTransform* dynScaleCollectionFindTransform(const DynScaleCollection* pCollection, const char* pcName);

void dynSkeletonReloadAllUsingCostume(const WLCostume* pCostume, enumResourceEventType eType);
void dynSkeletonReloadSkelInfo( DynSkeleton* pSkeleton);
U32 dynSkeletonGetTotalCount(void);
U32 dynSkeletonGetAllocationCount(void);
U32 dynSkeletonGetUnpooledCount(void);
void dynSkeletonPushDependentSkeleton(DynSkeleton* pParent, DynSkeleton* pChild, bool bInheritBits, bool bInsert);
void dynSkeletonFreeDependence(DynSkeleton* pChild);
void dynSkeletonQueueBodysockUpdate(DynSkeleton* pSkeleton, WLCostume* pMyCostume);

// Pass in a function of this form , and have it return true if you want to exclude that node from consideration as the closest bone to a line
typedef bool (*ExcludeNode)(const DynNode* pNode, void *pUserData);
const DynNode* dynSkeletonGetClosestBoneToLineSegment(const DynSkeleton* pSkeleton, const Vec3 vCursorStart, const Vec3 vCursorEnd, ExcludeNode excludeNode, void *pUserData);

void dynSkeletonSetExtentsNode(DynSkeleton* pSkeleton);
void dynSkeletonGetCollisionExtents(const DynSkeleton* pSkeleton, Vec3 vMin, Vec3 vMax);
void dynSkeletonGetExpensiveExtents(const DynSkeleton* pSkeleton, Vec3 vMin, Vec3 vMax, F32 fFudgeFactor);
void dynSkeletonGetVisibilityExtents(const DynSkeleton* pSkeleton, Vec3 vMin, Vec3 vMax, bool bIncludeSubskeletons);
bool dynSkeletonIsForceVisible(const DynSkeleton *pSkeleton);

SA_RET_OP_VALID __forceinline static const DynNode* dynBaseSkeletonFindNode(const DynBaseSkeleton* pBaseSkeleton, const char* pcTag)
{
	const DynNode* pNode;
	if ( stashFindPointerConst(pBaseSkeleton->stBoneTable, pcTag, &pNode) )
		return pNode;
	return NULL;
}

bool dynSkeletonForceAnimationEx(DynSkeleton* pSkeleton, const char* pcTrackname, F32 fFrame, U32 uiUseBaseSkeleton, U32 uiApplyHeightBump);
#define dynSkeletonForceAnimation(pSkeleton, pcTrackname, fFrame) dynSkeletonForceAnimationEx(pSkeleton, pcTrackname, fFrame, 0, 0)
bool dynSkeletonForceAnimationPrepare(const char* pcTrackname);

void dynDebugStateSetSkeleton(DynSkeleton* pSkeleton);
void dynDebugSkeletonUpdateBits(const DynSkeleton* pSkeleton, F32 fDeltaTime);
void dynDebugSkeletonUpdateNewAnimSysData(const DynSkeleton *pSkeleton, F32 fDeltaTime);

void dynDebugSkeletonStartGraph(const DynSkeleton *pSkeleton, const DynAnimGraphUpdater *pGraphUpdater, const char *pcKeyword, U32 uid);
void dynDebugSkeletonStartNode(const DynSkeleton *pSkeleton, const DynAnimGraphUpdater *pGraphUpdater, const DynAnimGraphUpdaterNode *pNode);
void dynDebugSkeletonSetFlag(const DynSkeleton *pSkeleton, const DynAnimGraphUpdater *pGraphUpdater, const char *pcFlag, U32 uid);
void dynDebugSkeletonUseFlag(const DynSkeleton *pSkeleton, const DynAnimGraphUpdater *pGraphUpdater, const char *pcFlag);
void dynDebugSkeletonClearFlags(const DynSkeleton *pSkeleton, const DynAnimGraphUpdater *pGraphUpdater);
void dynDebugSkeletonStartGraphFX(const DynSkeleton *pSkeleton, const DynAnimGraphUpdater *pGraphUpdater, const char *pcFX, bool bPlayed);
void dynDebugSkeletonStartMoveFX(const DynSkeleton *pSkeleton, const char *pcFX, bool bPlayed);

void dynSkeletonQueueAnimationFx(DynSkeleton* pSkeleton, DynFxManager* pFxManager, const char* pcFxName);
void dynSkeletonQueueAnimReactTrigger(Vec3 vBonePos, Vec3 vImpactDir, DynSkeleton* pSkeleton, U32 uid);

const DynBaseSkeleton* dynSkeletonGetRegSkeleton(const DynSkeleton* pSkeleton);
const DynBaseSkeleton* dynSkeletonGetBaseSkeleton(const DynSkeleton* pSkeleton);

S32 getNumEntities(void);
S32 getNumClientOnlyEntities(void);

void dynSkeletonShowModelsAttachedToBone(DynSkeleton* pSkeleton, const char *pcBoneTagName, bool bShow);

void dynSkeletonSetTalkBit(DynSkeleton* pSkeleton, bool bEnabled);

bool dynSkeletonStartGraph(DynSkeleton* pSkeleton, const char* pcGraph, U32 uid);
bool dynSkeletonStartDetailGraph(DynSkeleton* pSkeleton, const char* pcGraph, U32 uid);
void dynSkeletonResetToADefaultGraph(DynSkeleton* pSkeleton, const char *pcCallersReason, S32 onlyIfLooping, S32 callChildren);
void dynSkeletonSetFlag(DynSkeleton* pSkeleton, const char* pcFlag, U32 uid);
void dynSkeletonSetDetailFlag(DynSkeleton *pSkeleton, const char *pcFlag, U32 uid);
void dynSkeletonSetOverrideTime(DynSkeleton *pSkeleton, F32 fTime, U32 uiApply);

void dynSkeletonResetAllAnims(void);

void dynSkeletonCopyStanceWords(const DynSkeleton *pSkeleton, const char ***pDestWords);

void dynSkeletonSetAudioStanceWord(DynSkeleton *pSkeleton, const char *pcStance);
void dynSkeletonClearAudioStanceWord(DynSkeleton *pSkeleton, const char *pcStance);
void dynSkeletonClearAudioStances(DynSkeleton *pSkeleton);

void dynSkeletonApplyCostumeBits(DynSkeleton *pSkeleton, const WLCostume* pCostume);
void dynSkeletonSetCostumeStanceWord(DynSkeleton* pSkeleton, const char* pcState);
void dynSkeletonClearCostumeStanceWord(DynSkeleton* pSkeleton, const char* pcState);
void dynSkeletonClearCostumeStances(DynSkeleton* pSkeleton);
bool dynSkeletonCostumeBitsMatch(DynSkeleton *pSkeleton, const WLCostume *pCostume, const char *pcExtraStanceWords);

void dynSkeletonSetCutsceneStanceWord(DynSkeleton *pSkeleton, const char *pcStance);
void dynSkeletonClearCutsceneStanceWord(DynSkeleton *pSkeleton, const char *pcStance);
void dynSkeletonClearCutsceneStances(DynSkeleton *pSkeleton);

void dynSkeletonSetCritterStanceWord(DynSkeleton *pSkeleton, const char *pcStance);
void dynSkeletonClearCritterStanceWord(DynSkeleton *pSkeleton, const char *pcStance);
void dynSkeletonClearCritterStances(DynSkeleton *pSkeleton);

void dynSkeletonValidateAll(void);

void dynSkeletonSetLogReceiver(DynSkeleton* s, GenericLogReceiver* glr);

void dynSkeletonResetBouncers(void);

void dynSkeletonGetBoneWorldPosByName(const DynNode* pRoot, const char* pcBone, Vec3 vPosOut);

U32 dynSkeletonAnimOverrideCreate(DynSkeleton* pSkeleton);

void dynSkeletonAnimOverrideDestroy(DynSkeleton* pSkeleton,
									U32* overrideHandleInOut);

void dynSkeletonAnimOverrideStartGraph(	DynSkeleton* pSkeleton,
										U32 overrideHandle,
										const char* keyword,
										S32 onlyRestartIfNew);

void dynSkeletonAnimOverrideSetStance(	DynSkeleton* pSkeleton,
										U32 overrideHandle,
										const char* stance);

void dynSkeletonAnimOverrideClearStance(DynSkeleton* pSkeleton,
										U32 overrideHandle,
										const char* stance);

void dynSkeletonPushAnimWordFeed(DynSkeleton *pSkeleton, DynAnimWordFeed *pAnimWordFeed);
void dynSkeletonPlayKeywordInAnimWordFeed(DynAnimWordFeed *pAnimWordFeed, const char *pcKeyword);
bool dynSkeletonPlayFlagInAnimWordFeed(DynAnimWordFeed *pAnimWordFeed, const char *pcKeyword);
bool dynSkeletonSetStanceInAnimWordFeed(DynAnimWordFeed *pAnimWordFeed, const char *pcStance);
bool dynSkeletonClearStanceInAnimWordFeed(DynAnimWordFeed *pAnimWordFeed, const char *pcStance);
bool dynSkeletonToggleStanceInAnimWordFeed(DynAnimWordFeed *pAnimWordFeed, const char *pcStance);

const char *dynSkeletonGetGroundRegLimbName(DynGroundRegLimb *pLimb);
F32 dynSkeletonGetGroundRegLimbRelWeight(DynGroundRegLimb *pLimb);
F32 dynSkeletonGetGroundRegLimbOffset(DynGroundRegLimb *pLimb);

const char* dynSkeletonGetNodeAlias(const DynSkeleton* pSkeleton, const char* pcTag, bool bUseMountNodeAliases);
const char* dynSkeletonGetDefaultNodeAlias(const DynSkeleton* pSkeleton, bool bUseMountNodeAliases);

void dynSkeletonSetSnapshot(const DynSkeleton* pSrcSkeleton, DynSkeleton* pTgtSkeleton);
void dynSkeletonReleaseSnapshot(DynSkeleton* pSkeleton);

DynSkeleton* dynSkeletonGetRider(const DynSkeleton* pSkeleton);

const DynSkeleton* dynSkeletonFindByCostumeTag(const DynSkeleton* pSkeleton, const char* pcCostumeFxTag);