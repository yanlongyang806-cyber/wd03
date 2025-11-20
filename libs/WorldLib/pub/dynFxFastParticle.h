#pragma once
GCC_SYSTEM

#include "textparser.h"
#include "referencesystem.h"
#include "PoolQueue.h"
#include "dynFxInfo.h"

#define MAX_FAST_PARTICLE_SETS 2048
#define MAX_ALLOCATED_FAST_PARTICLES_ENVIRONMENT 8192
#define MAX_ALLOCATED_FAST_PARTICLES_ENTITY 24576

typedef struct DynNodeRef DynNodeRef;
typedef struct DynFxFastParticleSet DynFxFastParticleSet;
typedef struct DynNode DynNode;
typedef struct DynFx DynFx;
typedef struct BasicTexture BasicTexture;
typedef enum DynParticleEmitFlag DynParticleEmitFlag;
typedef U64 TexHandle;
extern ParseTable parse_DynFxFastParticleInfo[];
#define TYPE_parse_DynFxFastParticleInfo DynFxFastParticleInfo

extern const F32 fMaxFeetPerSecondForEmission;
extern const F32 fMaxLengthForEmission;

AUTO_ENUM;
typedef enum DynFastStreakMode
{
	DynFastStreakMode_None,
	DynFastStreakMode_Velocity,
	DynFastStreakMode_Parent,
	DynFastStreakMode_Chain,
	DynFastStreakMode_VelocityNoScale,
} DynFastStreakMode;

AUTO_ENUM;
typedef enum DynFastEmitMode
{
	DynFastEmitMode_Point,
	DynFastEmitMode_Line,
} DynFastEmitMode;

typedef struct DynFxFastParticleCompiled
{
	/*
	Vec4				vVelocity;
	Vec4				vVelocityJitter;
	Vec4				vAcceleration;
	Vec4				vAccelerationJitter;
	*/

	Vec4				vColor[4];
	Vec4				vColorJitter[4];
	Vec4				vColorTime[4];

	Vec4				vScaleRot[4];
	Vec4				vScaleRotJitter[4];
	Vec4				vScaleRotTime[4];

	Vec4				vColorTimeScale;
	Vec4				vScaleRotTimeScale;

	Vec4				vTexParams;
	Vec4				vSpinIntegrals;
	Vec4				vScrollAndAnimation;
	Vec4				vMoreParams; // 0 - tighten up
} DynFxFastParticleCompiled;

AUTO_STRUCT;
typedef struct DynFxFastParticleCurve
{
	F32 fStart;
	F32 fStartDeriv;
	F32 fEnd;
	F32 fEndDeriv;
} DynFxFastParticleCurve;

#define NUM_CURVE_VALUES 10

AUTO_STRUCT;
typedef struct DynFxFastParticleCurveInfo
{
	Vec4 vColor;
	F32 fRot;
	F32 fSpin;
	Vec2 vScale;
} DynFxFastParticleCurveInfo;

AUTO_STRUCT;
typedef struct DynFxFastParticleInfo
{
	const char* pcName;				AST(KEY POOL_STRING NO_WRITE)
	const char* pcTexture;			AST(POOL_STRING)
	BasicTexture* pTexture;			NO_AST
	F32 fLifeSpan;
	F32 fLifeSpanInv;				NO_AST
	DynFxFastParticleCurveInfo	curvePath[4]; AST( AUTO_INDEX(curvePath))
	DynFxFastParticleCurveInfo	curveTime[4]; AST( AUTO_INDEX(curveTime))
	DynFxFastParticleCurveInfo	curveJitter[4]; AST( AUTO_INDEX(curveJitter))

	F32 fLoopHue;
	F32 fLoopSaturation;
	F32 fLoopValue;
	F32 fLoopAlpha;
	F32 fLoopScaleX;
	F32 fLoopScaleY;
	F32 fLoopRotation;
	F32 fLoopSpin;

	DynFxFastParticleCurveInfo  curveTimeScale;
	const char* pcFileName;			AST(CURRENTFILE)
	DynFxFastParticleCompiled	compiled; NO_AST
	Vec3 vPosition;
	Vec3 vPositionJitter;
	Vec3 vPositionSphereJitter;
	Vec3 vVelocity;
	Vec3 vVelocityJitter;
	Vec3 vAcceleration;
	Vec3 vAccelerationJitter;
	F32 fGravity;
	F32 fGravityJitter;
	F32 fVelocityJitterUpdate; AST(NAME("VelocityUpdateRate"))
	F32 fVelocityJitterUpdateInverse; NO_AST
	F32 fAccelerationJitterUpdate; AST(NAME("AccelerationUpdateRate"))
	F32 fAccelerationJitterUpdateInverse; NO_AST
	F32 fGoTo;
	F32 fMagnetism;
	F32 fVelocityOut;
	F32 fKillRadius;
	F32 fEmissionRate; AST(NAME("Rate"))
	F32 fEmissionRateJitter; AST(NAME("RateJitter"))
	F32 fEmissionRatePerFoot; AST(NAME("RatePerFoot"))
	F32 fEmissionRatePerFootJitter; AST(NAME("RatePerFootJitter"))
	F32 fMinEmissionSpeed; AST(NAME("MinSpeed"))
	U32 uiEmissionCount; AST(NAME("Count"))
	U32 uiEmissionCountJitter; AST(NAME("CountJitter"))
	F32 fCountPerFoot;
	F32 fCountPerFootJitter;
	F32 fDrag;
	F32 fDragJitter;
	F32 fInheritVelocity;
	F32 fStickiness;
	F32 fZBias;
	F32 fSortBias;
	F32 fRadius; NO_AST
	Vec2 vScroll;
	Vec2 vScrollJitter;
	Vec2 vAnimParams;
	bool bHFlipTex; AST(BOOLFLAG)
	bool bVFlipTex; AST(BOOLFLAG)
	bool bQuadTex; AST(BOOLFLAG)
	bool bLinkScale; AST(BOOLFLAG)
	bool bRGBBlend; AST(BOOLFLAG)
	bool bJitterSphere; NO_AST
	bool bJitterSphereShell; AST(BOOLFLAG NAME("Shell"))
	bool bUniformJitter; AST(BOOLFLAG)
	bool bUniformLine; AST(BOOLFLAG)
	bool bLockEnds; AST(BOOLFLAG)
	bool bNoToneMap; AST(BOOLFLAG)
	bool bCountByDistance; AST(BOOLFLAG)
	bool bConstantScreenSize; AST(BOOLFLAG)
	bool bOldestFirst; AST(BOOLFLAG)
	bool bLowRes; AST(BOOLFLAG)
	bool bAnimatedTexture; AST(BOOLFLAG)
	bool bInited; NO_AST

	bool bSync[8]; AST(BOOLFLAG INDEX(0, SyncHue) INDEX(1, SyncSaturation) INDEX(2, SyncValue) INDEX(3, SyncAlpha) INDEX(4, SyncScaleX) INDEX(5, SyncScaleY) INDEX(6, SyncRotation) INDEX(7, SyncSpin) )

	DynFastStreakMode eStreakMode; AST(NAME("StreakMode") SUBTABLE(DynFastStreakModeEnum) DEFAULT(DynFastStreakMode_None))
	DynBlendMode eBlendMode; AST(NAME("BlendMode") SUBTABLE(ParseDynBlendMode) DEFAULT(DynBlendMode_Normal))
} DynFxFastParticleInfo;

typedef struct DynFxFastParticle
{
	union
	{
		struct
		{
			Quat qRot;
			Vec3 vPos;
			Vec3 vVel;
			Vec3 vAccel;
			Vec3 vScale;
			F32 fTime;
			F32 fDrag;
			F32 fTimeSinceVelUpdate;
			F32 fTimeSinceAccUpdate;
		};
		Vec4 vec[5];		// so we can alias values into fast hardward vector registers
	};

	F32 fMass;
	F32 fSeed;	
	U32 uiMovementSeed;
	U32 uiNodeIndex:4;
	U32 bInvisible:1;
	U32 bFirstInLine:1;
	U32 bLastInLine:1;
} DynFxFastParticle;

#define MAX_PARTICLES_PER_SET 1024

typedef struct DynFxFastParticleSet
{
	REF_TO(DynFxFastParticleInfo) hInfo;
	REF_TO(DynNode) hLocation;
	REF_TO(DynNode) hMagnet;
	REF_TO(DynNode) hEmitTarget;
	REF_TO(DynNode) hTransformTarget;
	REF_TO(DynFx) hParentFX;
	PoolQueue particleQueue;
	U32 uiNumAtNodes;
	DynNodeRef* pAtNodes;
	DynNodeRef* pEmitTargetNodes;
	F32* pfWeights;
	F32 fSetTime;
	F32 fLastEmissionTime;
	F32 fLastEmissionDistance;
	F32 fCurrentDistanceEmissionRate;
	F32 fCurrentEmissionRate;
	F32 fCurrentEmissionRateInv;
	F32 fLeftOverDistanceTimes;
	Vec3 vPos;
	Quat qRot;
	Vec3 vScale;
	Vec3 vStickiness;
	F32 fHueShift;
	F32 fSaturationShift;
	F32 fValueShift;
	Vec4 vModulateColor;
	F32 fSystemAlpha;
	DynParticleEmitFlag ePosFlag;
	DynParticleEmitFlag eRotFlag;
	DynParticleEmitFlag eScaleFlag;
	F32 fScalePosition;
	F32 fScaleSprite;
	int iPriorityLevel;
	F32 fDrawDistance;
	F32 fMinDrawDistance;
	F32 fFadeDistance;
	F32 fMinFadeDistance;
	const char* pcEmitterName;
	U32 uiNumAllocated;
	U32 bEmitted:1;
	U32 bStopEmitting:1;
	U32 bSoftKill:1;
	U32 bApplyCountEvenly:1;
	U32 bOverflowError:1;
	U32 b2D:1;
	U32 bEnvironmentFX:1;
	U32 bJumpStart:1;
	Vec3 *pvModelEmitOffsetVerts;
	S32 *pModelEmitOffsetTris;
	F32 *pModelEmitOffsetTriSizes;
	U32 uiNumEmitVerts;
	U32 uiNumTriangles;
	F32 fModelTotalArea;
	F32 fCutoutDepthScale;
	F32 fParticleMass;
	F32 fSystemAlphaFromFx;
	const char *pcModelPattern;
	U32 bUseModelTriangles:1;
	U32 bUseModel:1;
	U32 bOverrideSpecialParam:1;
	U32 bLightModulation:1;
	U32 bColorModulation:1;
	U32 bNormalizeTransformTarget:1;
	U32 bNormalizeTransformTargetOtherAxes:1;

	struct {
		U32 bDelayedUpdate:1;
		U32 bVisible:1;
		Vec3 vWorldSpaceVelocity;
		F32 fDeltaTime;
	} delayedUpdate;

	Vec3 vLastEmitTargetLocation;
	Vec3 vLastLocation;

} DynFxFastParticleSet;

AUTO_STRUCT;
typedef struct DynFxFastParticleEmissionInfo
{
	Vec3 vVelocity;
} DynFxFastParticleEmissionInfo;

typedef struct DynFPSetParams
{
	const DynFxFastParticleInfo* pInfo;
	DynNode* pLocation;
	const DynNode* pMagnet;
	const DynNode* pEmitTarget;
	const DynNode* pTransformTarget;
	DynFx* pParentFX;
	DynParticleEmitFlag ePosFlag;
	DynParticleEmitFlag eRotFlag;
	DynParticleEmitFlag eScaleFlag;
	F32 fScalePosition;
	F32 fScaleSprite;
	bool bMaxBuffer;
	int iPriorityLevel;
	F32 fDrawDistance;
	F32 fMinDrawDistance;
	const char* pcEmitterName;
	char const** const* peaAtNodes;
	char const** const* peaEmitTargetNodes;
	F32* const* peaWeights;
	F32 fHueShift;
	F32 fSaturationShift;
	F32 fValueShift;
	F32 fCutoutDepthScale;
	F32 fParticleMass;
	F32 fSystemAlphaFromFx;
	U32 bSoftKill:1;
	U32 bApplyCountEvenly:1;
	U32 b2D:1;
	U32 bEnvironmentFX:1;
	U32 bJumpStart:1;
	U32 bPatternModelUseTriangles:1;
	U32 bOverrideSpecialParam:1;
	U32 bLightModulation:1;
	U32 bColorModulation:1;
	U32 bUseModel:1;
	U32 bNormalizeTransformTarget:1;
	U32 bNormalizeTransformTargetOtherAxes:1;
	const char *pcPatternModelName;
} DynFPSetParams;

void dynFxFastParticleLoadAll(void);

// Info related calls
const DynFxFastParticleInfo* dynFxFastParticleInfoFromName(SA_PARAM_NN_STR const char* pcName);
DynFxFastParticleInfo* dynFxFastParticleInfoFromNameNonConst(SA_PARAM_NN_STR const char* pcName);
bool dynFxFastParticleInfoInit(DynFxFastParticleInfo* pInfo);
void dynFxInitNewFastParticleInfo(DynFxFastParticleInfo* pInfo);
U32 dynFxFastParticleMaxPossibleRate(const DynFxFastParticleInfo* pInfo, F32 fMaxFeetPerSecond);

// Orphan management calls
void dynFxFastParticleSetOrphan(SA_PARAM_NN_VALID DynFxFastParticleSet* pSet, SA_PARAM_NN_VALID DynFxRegion* pFxRegion);
void dynFxFastParticleSetOrphansUpdate(F32 fDeltaTime);


// Main Fast particle calls
SA_ORET_OP_VALID DynFxFastParticleSet* dynFxFastParticleSetCreate(SA_PARAM_NN_VALID DynFPSetParams* pParams);
void dynFxFastParticleSetDestroy(SA_PRE_NN_VALID SA_POST_P_FREE DynFxFastParticleSet* pSet);
void dynFxFastParticleSetUpdate(SA_PARAM_NN_VALID DynFxFastParticleSet* pSet, const Vec3 vVelocity, F32 fDeltaTime, bool bTestVelocity, bool bIsVisible);
void dynFxFastParticleChangeModel(DynFxFastParticleSet* pSet, const char *pcModelName);

__forceinline static int dynFxFastParticleSetGetNumParticles(SA_PARAM_NN_VALID DynFxFastParticleSet* pSet)
{
	return pSet->particleQueue.pStorage?poolQueueGetNumElements(&pSet->particleQueue):0;
}

// Calls needed for drawing
const DynNode* dynFxFastParticleSetGetAtNode(DynFxFastParticleSet* pSet, U32 uiNodeIndex);

// Editor only calls: NOTE These are not jumpstart-safe! Should be ok since editor particles are never flagged as invisible
void dynFxFastParticleSetChangeInfo( SA_PARAM_NN_VALID DynFxFastParticleSet* pSet, SA_PARAM_NN_VALID const DynFxFastParticleInfo* pInfo );
void dynFxFastParticleSetRecalculate(SA_PARAM_NN_VALID DynFxFastParticleSet* pSet, const Vec3 vInitPos, const Vec3 vInheritVelocity);
void dynFxFastParticleSetReset(SA_PARAM_NN_VALID DynFxFastParticleSet* pSet);
void dynFxFastParticleFakeVelocity(SA_PARAM_NN_VALID DynFxFastParticleSet* pSet, const Vec3 vFakeVelocity, F32 fDeltaTime);
void dynFxFastParticleFakeVelocityRecalculate(SA_PARAM_NN_VALID DynFxFastParticleSet* pSet, const Vec3 vFakeVelocity);
bool dynFxFastParticleUseConstantScreenSize(DynFxFastParticleSet* pSet);

