#pragma once
GCC_SYSTEM

#include "textparser.h"
#include "components/referencesystem.h"

#define MAX_SEQUENCERS_PER_SKELETON 5

extern bool bLoadedSkelInfos;
extern DictionaryHandle hSkelInfoDict;

typedef struct StashTableImp* StashTable;
typedef struct DynAnimTrackHeader DynAnimTrackHeader;
typedef struct DynBaseSkeleton DynBaseSkeleton;
typedef struct ScaleValue ScaleValue;
typedef struct DynRagdollData DynRagdollData;
typedef struct DynSkeleton DynSkeleton;
typedef struct DynAnimOverrideList DynAnimOverrideList;
typedef struct DynBouncerGroupInfo DynBouncerGroupInfo;
typedef struct DynAnimChartRunTime DynAnimChartRunTime;
typedef struct DynMovementSet DynMovementSet;
typedef struct DynStrandDataSet DynStrandDataSet;
typedef struct DynGroundRegData DynGroundRegData;
typedef struct DynCriticalNodeList DynCriticalNodeList;
typedef struct DynAnimExpressionSet DynAnimExpressionSet;
typedef struct DynAnimNodeAliasList DynAnimNodeAliasList;

typedef struct CalcBoneScale
{
	Vec3 vScale;
	Vec3 vUniversalScale;
	Vec3 vTrans;
	const char* pcBone;
} CalcBoneScale;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct SkelScaleBone
{
	const char* pcBone; AST(POOL_STRING STRUCTPARAM)
	Vec3 vSmallMin;
	Vec3 vSmallMax;
	Vec3 vLargeMin;
	Vec3 vLargeMax;
	const char** eapcCounterScaleBones; AST(POOL_STRING NAME(CounterScale))
	bool bUniversal; AST(BOOLFLAG)
	bool bTranslation; AST(BOOLFLAG)
} SkelScaleBone;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct SkelScaleGroupInclude
{
	const char* pcGroup; AST(POOL_STRING STRUCTPARAM)
	F32 fFraction; AST(STRUCTPARAM)
} SkelScaleGroupInclude;


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct SkelScaleGroup
{
	const char* pcGroupName; AST(POOL_STRING STRUCTPARAM)
	SkelScaleBone** eaBone;
	SkelScaleGroupInclude** eaGroup;
} SkelScaleGroup;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct SkelScaleAnimTrack
{
	const char* pcName; AST(POOL_STRING STRUCTPARAM)
	const char* pcScaleAnimFile; AST(POOL_STRING STRUCTPARAM)
	DynAnimTrackHeader* pScaleTrackHeader; NO_AST
} SkelScaleAnimTrack;


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct SkelScaleInfo
{
	const char* pcScaleInfoName; AST(KEY POOL_STRING)
	SkelScaleAnimTrack** eaScaleAnimTrack; AST(NAME(ScaleAnimTrack))
	const char* pcHeightFixupBone; AST(POOL_STRING)
	SkelScaleGroup** eaScaleGroup;
	const char* pcFileName;		AST(CURRENTFILE)
} SkelScaleInfo;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct SkelBlendRunAndGunBone 
{
	const char *pcName;			AST(NAME(Bone) POOL_STRING)
	bool bSecondary;			AST(BOOLFLAG)
	F32 fLimitAngle;
} SkelBlendRunAndGunBone;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct SkelBlendSeqInfo
{
	const char* pcSeqName; AST(STRUCTPARAM POOL_STRING)
	REF_TO(DynAnimChartRunTime) hChart; AST(NAME(Chart))
	U32 uiLODLevel; AST(NAME(MaxLOD) DEFAULT(10))
	const char** eaBoneName; AST(NAME(Bone) POOL_STRING)
	bool bOverlay; AST(BOOLFLAG)
	bool bSubOverlay; AST(BOOLFLAG)
	bool bIgnoreMasterOverlay; AST(BOOLFLAG)
	bool bNeverOverride; AST(BOOLFLAG)
	bool bMovement; AST(BOOLFLAG)
	U32 iAGUpdaterIndex; NO_AST
} SkelBlendSeqInfo;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct SkelBlendInfo
{
	const char* pcBlendInfoName; AST(KEY POOL_STRING)
	const char* pcMainSequencer; AST(POOL_STRING)
	SkelBlendSeqInfo** eaSequencer;

	StashTable stBoneSequencerInfo; NO_AST
	StashTable stBoneOverlayInfo; NO_AST
	U32 uiMaxWeightIndex;
	REF_TO(DynAnimOverrideList) hOverrideList; AST(NAME(DynAnimOverrideList))

	REF_TO(DynAnimChartRunTime) hDefaultChart; AST(NAME(DefaultChart))
	REF_TO(DynAnimChartRunTime) hMountedChart; AST(NAME(MountedChart))

	SkelBlendRunAndGunBone **eaRunAndGunBone;
	bool bPreventRunAndGunFootShuffle;	AST(BOOLFLAG)
	bool bPreventRunAndGunUpperBody;	AST(BOOLFLAG)
	bool bTorsoPointing;	AST(BOOLFLAG)
	bool bTorsoDirections;	AST(BOOLFLAG)
	bool bMovementBlending;	AST(BOOLFLAG)

	const char* pcFileName;		AST(CURRENTFILE)
} SkelBlendInfo;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct HeadShotFrameBone
{
	const char* pcBoneName; AST(POOL_STRING STRUCTPARAM)
	F32 fRadius; AST(STRUCTPARAM)
} HeadShotFrameBone;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct HeadShotFrame
{
	const char* pcFrameName; AST(KEY POOL_STRING STRUCTPARAM)
	HeadShotFrameBone** eaFrameBone;
	Vec3 vCameraDirection;
	Vec4 vMargins; // Left Right Top Bottom 
} HeadShotFrame;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct SkelHeadshotInfo
{
	const char* pcHeadshotInfoName; AST(KEY POOL_STRING)
	HeadShotFrame** eaHeadShotFrame;
	const char* pcFileName;		AST(CURRENTFILE)
} SkelHeadshotInfo;

AUTO_STRUCT;
typedef struct BodySockInfo
{
	const char* pcBodySockGeo; AST(POOL_STRING)
	const char* pcBodySockPose; AST(POOL_STRING)
	Vec3 vBodySockMin;
	Vec3 vBodySockMax;
} BodySockInfo;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct SkelInfo
{
	const char* pcSkelInfoName; AST(NAME("Name") KEY POOL_STRING STRUCTPARAM)
	REF_TO(const DynBaseSkeleton)	hBaseSkeleton;	AST( NAME("Skeleton") REQUIRED NON_NULL_REF REFDICT(BaseSkeleton) )
	REF_TO(const DynBaseSkeleton)	hRegistrationSkeleton;	AST( NAME("RegSkeleton") REFDICT(BaseSkeleton) )
	const char* pcSequencer;	AST(POOL_STRING)
	const char** eapcSeqType;	AST(NAME("SeqType") POOL_STRING)
	const char* pcFileName;		AST(CURRENTFILE)
	REF_TO(SkelScaleInfo)		hScaleInfo;		AST(NAME("ScaleInfo"))
	REF_TO(SkelBlendInfo)		hBlendInfo;		AST(NAME("BlendInfo"))
	REF_TO(DynRagdollData)		hRagdollDataHD;	AST(NAME("RagdollHD"))
	REF_TO(DynRagdollData)		hRagdollData;	AST(NAME("Ragdoll"))
	REF_TO(SkelHeadshotInfo)	hHeadshotInfo;	AST(NAME("HeadshotInfo"))
	REF_TO(DynBouncerGroupInfo)	hBouncerInfo;	AST(NAME("BouncerInfo"))
	REF_TO(DynAnimChartRunTime)	hDefaultChart;	AST(NAME(DefaultChart))
	REF_TO(DynAnimChartRunTime)	hMountedChart;	AST(NAME(MountedChart))
	REF_TO(DynStrandDataSet)	hStrandDataSet;	AST(NAME(StrandInfo))
	REF_TO(DynGroundRegData)	hGroundRegData;	AST(NAME(GroundRegInfo))
	REF_TO(DynCriticalNodeList)	hCritNodeList;	AST(NAME(CriticalNodeList))
	REF_TO(DynAnimExpressionSet)hExpressionSet;	AST(NAME(NodeExpressions))
	REF_TO(DynAnimNodeAliasList)hAnimNodeAliasList;	AST(NAME(AnimNodeAliases))
	BodySockInfo bodySockInfo; AST(EMBEDDED_FLAT NAME(BodySockInfo))
	const char* pcAltBankingNodeAlias; AST(POOL_STRING)
	bool	bLOD;				AST(BOOLFLAG)
	bool	bStatic;			AST(BOOLFLAG) // means that this object does not animate, and the visibility extents should be calc'd once

	U32 uiReportCount; NO_AST
} SkelInfo;

void wlSkelInfoLoadAll();

bool wlSkelInfoFindSeqTypeRank(SA_PARAM_NN_VALID const SkelInfo* pSkelInfo, SA_PARAM_NN_VALID const char* pcSeqType, U32* uiRank);
bool wlSkelInfoGetBoneScales(const SkelInfo* pSkelInfo, ScaleValue* pScaleValue, F32 fBodyType, StashTable stBoneScaleTable);

void findCameraPosFromHeadShotFrame(const HeadShotFrame* pFrame, const DynSkeleton* pSkeleton, F32 fFOV, F32 fAspectRatio, Vec3 vResult);