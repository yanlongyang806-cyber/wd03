#pragma once
GCC_SYSTEM

#include "referencesystem.h"

#include "dynAnimChart.h"
#include "dynAnimGraphPub.h"

typedef struct DynAnimGraph DynAnimGraph;
typedef struct DynAnimGraphNode DynAnimGraphNode;
typedef struct DynAnimGraphMove DynAnimGraphMove;
typedef struct DynFxManager DynFxManager;
typedef struct DynTransform DynTransform;
typedef struct DynRagdollState DynRagdollState;
typedef struct DynSkeleton DynSkeleton;
typedef struct GenericLogReceiver GenericLogReceiver;
typedef struct DynJointBlend DynJointBlend;

typedef enum DynAnimGraphUpdaterInterruptBit {
	DAGUI_BLOCKING	= BIT(0),
	DAGUI_GRAPHEND	= BIT(1),
	DAGUI_MOVEMENT	= BIT(2),
	DAGUI_TIMEOUT	= BIT(3),
} DynAnimGraphUpdaterInterruptBit;

typedef struct DynAnimGraphUpdaterNode
{
	union {
		const DynAnimGraphNode*			pGraphNodeMutable;
		const DynAnimGraphNode*const	pGraphNode;
	};

	union {
		const DynAnimGraph*				pGraphMutable;
		const DynAnimGraph*const		pGraph;
	};

	const DynAnimGraphMove* pMove_debug;
	const DynMoveSeq *pMoveSeq;
	const char* pcSetFlag; 
	union {
		F32			fTimeMutable;
		const F32	fTime;
	};

	union {
		F32			fPlaybackSpeedMutable;
		const F32	fPlaybackSpeed;
	};

	union {
		F32			fBlendFactorMutable;
		const F32	fBlendFactor;
	};

	union {
		F32			fTimeOnBlendMutable;
		const F32	fTimeOnBlend;
	};

	const char *pcReason;
	const char *pcReasonDetails;
} DynAnimGraphUpdaterNode;

#define MAX_UPDATER_NODES 5

typedef struct DynAnimGraphUpdater
{
	DynAnimChartStack *pChartStack;
	
	union {
		const DynAnimChartRunTime*		pCurrentChartMutable;
		const DynAnimChartRunTime*const	pCurrentChart;
	};

	union {
		const DynAnimGraph*			pCurrentGraphMutable;
		const DynAnimGraph*const	pCurrentGraph;
	};

	const DynAnimGraph* pLastGraph; // For eaOnExitFxEvents
	const DynAnimGraph *pPostIdleCaller;
	const char **eaPostIdleCallerStances;
	DynAnimGraphUpdaterNode nodes[MAX_UPDATER_NODES];
	const char** eaFlagQueue;
	const char  *pcForceLoopFlag;	//for debugging
	GenericLogReceiver* glr;
	F32 fOverlayBlend;
	F32 fOverrideTime;
	U32 uidCurrentGraph;
	U32 bOnDefaultGraph : 1;
	U32 bOnMovementGraph : 1;
	U32 bOnMultiFlagStart : 1;
	U32 bInPostIdle : 1;
	U32 bInDefaultPostIdle : 1;
	U32 bInTPose : 1;
	U32 bIsPlayingDeathAnim : 1;
	U32 bForceLoopCurrentGraph  :1; // for editors
	U32 bLooped  :1; // for editors
	U32 bMovement:1;
	U32 bOverlay :1;
	U32 bResetToDefaultWasAttempted :1;
	U32 bResetToDefaultOnChartStackChange : 1;
	U32 bEnableOverrideTime :1;
	F32 fTimeOnCurrentGraph; // for editors and debugging purposes only
	F32 fTimeOnLastGraph;
	DynFxManager *pEditorFxManager; // for editors and debugging purposes only
} DynAnimGraphUpdater;

DynAnimGraphUpdater* dynAnimGraphUpdaterCreate(DynSkeleton *pSkeleton, DynAnimChartStack *pChartStack, bool bMovement, bool bOverlay);
void dynAnimGraphUpdaterReset(DynSkeleton *pSkeleton, DynAnimGraphUpdater *pUpdater);
void dynAnimGraphUpdaterDestroy(DynAnimGraphUpdater* pUpdater);

void dynAnimGraphUpdaterUpdate(DynAnimGraphUpdater* pUpdater, F32 fDeltaTime, DynSkeleton* pSkeleton, S32 disableRagdoll);
void dynAnimGraphUpdaterUpdateBone( DynAnimGraphUpdater* pUpdater, DynTransform* pResult, const char* pcBoneTag, U32 uiBoneLOD, const DynTransform* pxBaseTransform, DynRagdollState* pRagdollState, DynSkeleton* pSkeleton);
F32 dynAnimGraphUpdaterGetMovePercent(DynAnimGraphUpdater* pUpdater);
F32 dynAnimGraphUpdaterGetMoveTime(DynAnimGraphUpdater* pUpdater);
F32 dynAnimGraphUpdaterGetMoveLength(DynAnimGraphUpdater* pUpdater);
F32 dynAnimGraphUpdaterDebugCalcBlend(DynAnimGraphUpdater* pUpdater, DynAnimGraphNode* pGraphNode);
void dynAnimGraphUpdaterSetForceOverrideGraph(DynSkeleton *pSkeleton, DynAnimGraphUpdater* pUpdater, DynAnimGraph* pForceOverrideGraph);
bool dynAnimGraphUpdaterStartGraph(DynSkeleton *pSkeleton, DynAnimGraphUpdater* pUpdater, const char* pcGraph, U32 uid, S32 mustBeDetailGraph);
bool dynAnimGraphUpdaterResetToADefaultGraph(DynSkeleton *pSkeleton, DynAnimGraphUpdater* pUpdater, const DynAnimGraph *pPostIdleGraph, const char *pcCallersReason, S32 onlyIfLooping, S32 forceIfSameChart, S32 interruptBits);
void dynAnimGraphUpdaterChartStackChanged(DynSkeleton *pSkeleton, DynAnimGraphUpdater *pUpdater);
bool dynAnimGraphUpdaterSetFlag(DynAnimGraphUpdater* pUpdater, const char* pcFlagToSet, U32 uid);
bool dynAnimGraphUpdaterSetDetailFlag(DynAnimGraphUpdater *pUpdater, const char *pcFlagToSet, U32 uid);
void dynAnimGraphUpdaterSetOverrideTime(DynAnimGraphUpdater *pUpdater, F32 fTime, U32 uiApply);
void dynAnimGraphUpdaterGetChartStackString(DynAnimGraphUpdater *pUpdater, char* buffer, U32 bufferLen);
void dynAnimGraphUpdaterGetStanceWordsString(DynAnimGraphUpdater *pUpdater, char* buffer, U32 bufferLen);

void dynAnimGraphUpdaterSignalMovementStopped(DynSkeleton *pSkeleton, DynAnimGraphUpdater *pUpdater, bool bMovementStopped);
void dynAnimGraphUpdaterDoEnterFX(DynSkeleton *pSkeleton, DynAnimGraphUpdater *pUpdater, bool bInternal, F32 fNewBlend, F32 fOldBlend);
void dynAnimGraphUpdaterDoExitFX(DynSkeleton *pSkeleton, DynAnimGraphUpdater *pUpdater, bool bInternal, F32 fNewBlend, F32 fOldBlend);

void dynAnimGraphUpdaterInitMatchJoints(DynSkeleton* pSkeleton, DynAnimGraphUpdater* pUpdater, DynJointBlend*** peaMatchJoints);
void dynAnimGraphUpdaterUpdateMatchJoints(DynSkeleton* pSkeleton, DynJointBlend*** peaMatchJoints, F32 fDeltaTime);
void dynAnimGraphUpdaterCalcMatchJointOffset(DynSkeleton* pSkeleton, DynAnimGraphUpdater* pUpdater, DynJointBlend* pMatchJoint, Vec3 vReturn);

void dynAnimGraphUpdaterCalcNodeBank(const DynAnimGraphUpdater* pUpdater, F32* fBankMaxAngle, F32* fBankScale);
U32 dynAnimGraphUpdaterMovementIsStopped(const DynAnimGraphUpdater* pUpdater);
F32 dynAnimGraphUpdaterCalcBlockYaw(const DynAnimGraphUpdater* pUpdater, F32* fMoveYawRate, F32* fMovementYawStopped);
