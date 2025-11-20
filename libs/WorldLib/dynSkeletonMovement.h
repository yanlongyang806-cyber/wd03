#pragma once

#include "referencesystem.h"

#include "DynMove.h"

typedef struct DynSkeleton DynSkeleton;
typedef struct SkelInfo SkelInfo;
typedef struct DynTransform DynTransform;
typedef struct DynRagdollState DynRagdollState;
typedef struct DynMovementState DynMovementState;
typedef struct DynAnimChartRunTime DynAnimChartRunTime;
typedef struct DynMoveTransition DynMoveTransition;
typedef struct DynAnimGraphNode DynAnimGraphNode;
typedef struct DynJointBlend DynJointBlend;
typedef struct DynAnimChartStack DynAnimChartStack;

extern DictionaryHandle hMovementSetDict;

extern ParseTable parse_DynMovementSequence[];
#define TYPE_parse_DynMovementSequence DynMovementSequence

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynMovementSequence
{
	const char* pcMovementType; AST(POOL_STRING STRUCTPARAM)
	F32 fAngle;
	F32 fAngleWidth;
	F32 fCoverAngleMin;
	F32 fCoverAngleMax;
} DynMovementSequence;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynMovementSet
{
	const char*		pcName;							AST(KEY POOL_STRING)
	const char*		pcFileName;						AST(CURRENTFILE)
	const char*		pcCollectionName;				AST(POOL_STRING)
	DynMovementSequence** eaMovementSequences;		AST(NAME(Movement))
	const char**	eaMovementStances;				AST(POOL_STRING)
	const char**	eaInterruptingMovementStances;	AST(POOL_STRING)
	const char**	eaDirectionMovementStances;		AST(POOL_STRING)
} DynMovementSet;

typedef struct DynMovementBlock
{
	F32 fFrameTime;
	F32 fBlendFactor;
	F32 fEarlyForceEndByFrame;
	F32 fEarlyForceEndAtFrame;
	DynAnimInterpolation interpBlockPre;
	DynAnimInterpolation interpBlockPost;
	const DynAnimChartRunTime* pChart;
	const DynMovementSequence* pMovementSequence;
	const DynMoveSeq* pMoveSeqCycle;
	const DynMoveTransition* pTransition;
	const DynMoveSeq* pMoveSeqTransition;
	const DynMoveTransition* pFromTransition;
	const char **eaSkeletalStancesOnTrigger;
	U32 inTransition : 1;
	U32 bJumping : 1;
	U32 bFalling : 1;
	U32 bRising : 1;
	U32 bEarlyForceEnd : 1;
	U32 bSyncToParent : 1;
} DynMovementBlock;


void dynMovementSetLoadAll(void);

void dynMovementStateInit(DynMovementState* pMovementState, DynSkeleton* pSkeleton, DynAnimChartStack* pChartStack, const SkelInfo* pSkelInfo);
void dynMovementStateDeinit(DynMovementState* pMovementState);
void dynSkeletonMovementUpdateBone(DynMovementState* m, DynTransform* pResult, const char* pcBoneTag, U32 uiBoneLOD, const DynTransform* pxBaseTransform, DynRagdollState* pRagdollState, DynSkeleton* pSkeleton);
void dynSkeletonMovementUpdate(DynSkeleton* pSkeleton, F32 fDeltaTime);

const DynMovementSequence* dynSkeletonGetCurrentMovementSequence(const DynSkeleton* pSkeleton, DynMovementSequence **eaMovementSequences);

U32 dynSkeletonMovementIsStopped(DynMovementState *m);
void dynSkeletonMovementCalcBlockBank(DynMovementState *m, F32 *fBankMaxAngleOut, F32 *fBankScaleOut);
F32 dynSkeletonMovementCalcBlockYaw(DynMovementState *m, F32 *fYawRateOut, F32 *fYawStoppedOut);

void dynSkeletonMovementInitMatchJoints(DynSkeleton *pSkeleton, DynMovementState *m);
void dynSkeletonMovementUpdateMatchJoints(DynSkeleton *pSkeleton, F32 fDeltaTime);
void dynSkeletonMovementCalcMatchJointOffset(DynSkeleton *pSkeleton, DynJointBlend *pMatchJoint, Vec3 vReturn);

// This needs to be moved
void dynSkeletonUpdateBoneVisibility(DynSkeleton* pSkeleton, const DynAnimGraphNode* pNode);