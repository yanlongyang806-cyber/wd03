
#pragma once
GCC_SYSTEM

#define MAX_LOD_UPDATE_DEPTH 1

typedef struct DynSkeleton DynSkeleton;
typedef struct DynJointBlend DynJointBlend;
typedef struct DynSequencer DynSequencer;
typedef struct DynBitField DynBitField;
typedef struct DynBitFieldGroup DynBitFieldGroup;
typedef struct DynMove DynMove;
typedef struct SkelInfo SkelInfo;
typedef enum eDynBitType eDynBitType;
typedef struct DynFxManager DynFxManager;
typedef struct DynMoveSeq DynMoveSeq;
typedef struct DynSeqData DynSeqData;
typedef struct DynAction DynAction;
typedef struct DynAnimOverride DynAnimOverride;
typedef struct WLCostume WLCostume;
typedef struct DynFxDrivenScaleModifier DynFxDrivenScaleModifier;
typedef struct GenericLogReceiver GenericLogReceiver;


DynSequencer* dynSequencerCreate(SA_PARAM_NN_VALID const WLCostume* pCostume, const char* pcSequencerName, U32 uiRequiredLOD, bool bNeverOverride, bool bOverlay);
void dynSequencerFree(SA_PRE_NN_VALID SA_POST_P_FREE DynSequencer* pToFree);
void dynSeqUpdate(SA_PARAM_NN_VALID DynSequencer* pSqr, SA_PARAM_NN_VALID const DynBitField* pBits, F32 fDeltaTime, SA_PARAM_NN_VALID DynSkeleton* pSkeleton, bool bUpdateInterpParam, int iSeqIndex);

void dynSequencerProcessReloads(void);

extern bool dynSeqLoggingEnabled;

void dynSeqSetLogReceiver(SA_PARAM_NN_VALID DynSequencer* pSqr, GenericLogReceiver* glr);
S32 dynSeqLoggingIsEnabledEx(SA_PARAM_NN_VALID DynSequencer* pSqr);
#define dynSeqLoggingIsEnabled(pSqr) (dynSeqLoggingEnabled && dynSeqLoggingIsEnabledEx(pSqr))

void dynSequencerLogEx(SA_PARAM_NN_VALID DynSequencer* pSeq, FORMAT_STR const char* format, ...);
#define dynSequencerLog(pSeq, format, ...) { if (dynSeqLoggingIsEnabled(pSeq)) dynSequencerLogEx(pSeq, FORMAT_STRING_CHECKED(format), ##__VA_ARGS__); }
const char* dynSequencerGetLog(SA_PARAM_NN_VALID DynSequencer* pSqr);
void dynSequencerFlushLog(SA_PARAM_NN_VALID DynSequencer* pSqr);


const DynBitField* dynSeqGetCurrentBits(DynSequencer* pSeq);
const DynBitField* dynSeqGetNextBits(DynSequencer* pSeq);
const DynBitField* dynSeqGetPreviousBits(DynSequencer* pSeq);
const char* dynSeqGetCurrentSequenceName( SA_PARAM_NN_VALID DynSequencer* pSqr );
const char* dynSeqGetCurrentActionName( SA_PARAM_NN_VALID DynSequencer* pSqr );
F32 dynSeqGetCurrentActionFrame( DynSequencer* pSqr );
const char* dynSeqGetName( SA_PARAM_NN_VALID DynSequencer* pSqr );
const char* dynSeqGetCurrentMoveName( SA_PARAM_NN_VALID DynSequencer* pSqr );
const DynAction* dynSeqGetCurrentAction( SA_PARAM_NN_VALID DynSequencer* pSqr);
const char* dynSeqGetNextActionName( SA_PARAM_NN_VALID DynSequencer* pSeq );
const char* dynSeqGetPreviousActionName( SA_PARAM_NN_VALID DynSequencer* pSeq );
const DynMove* dynSeqGetCurrentMove(DynSequencer* pSeq);
const DynMove* dynSeqGetNextMove(DynSequencer* pSeq);
const DynMove* dynSeqGetPreviousMove(DynSequencer* pSeq);
const DynBitField* dynSeqGetBits(SA_PARAM_NN_VALID DynSequencer* pSqr);
void dynDebugSetSequencer(DynSkeleton* pSkeleton);
void dynSeqDebugInitDebugBits(void);
void dynSeqPushBitFieldFeed(SA_PARAM_NN_VALID DynSkeleton* pSkeleton, SA_PARAM_NN_VALID DynBitFieldGroup* pBFGFeed);
void dynSeqRemoveBitFieldFeed(SA_PARAM_NN_VALID DynSkeleton* pSkeletonpSqr, SA_PARAM_NN_VALID DynBitFieldGroup* pBFGFeed);
void dynSequencerResetAll(void);
void dynSeqFindNOLODBitIndex(void);

void dynSeqClearBitFieldFlashBits(SA_PARAM_NN_VALID DynSequencer* pSqr);
bool dynSequencersDemandNoLOD(SA_PARAM_NN_VALID DynSkeleton* pSkeleton);
void dynSeqPushOverride(SA_PARAM_NN_VALID DynSequencer* pSqr, SA_PARAM_NN_VALID DynAnimOverride* pOverride);
void dynSeqClearOverrides(SA_PARAM_NN_VALID DynSequencer* pSqr);

void dynSeqCalcIK(DynSkeleton* pSkeleton, bool bRedoSkinning);
void dynSeqCalcMatchJoint(DynSkeleton *pSkeleton, DynJointBlend *pMatchJoint);

typedef struct DynRagdollState DynRagdollState;
typedef struct DynTransform DynTransform;

void dynSeqUpdateBone( DynSequencer* pSqr, DynTransform* pResult, const char* pcBoneTag, U32 uiBoneLOD, const DynTransform* pxBaseTransform, DynRagdollState* pRagdollState, DynSkeleton* pSkeleton);


bool dynSeqNeverOverride(SA_PARAM_NN_VALID DynSequencer* pSqr);

void dynSeqSetStoppedYaw(DynSequencer *pSqr);
U32 dynSeqMovementWasStopped(DynSequencer *pSqr);
F32 dynSeqMovementCalcBlockYaw(DynSequencer *pSqr, F32 *fYawRateOut, F32 *fYawStoppedOut, U32 eTargetDir);

F32 dynSeqEnableTorsoPointingTime(DynSequencer *pSqr);
F32 dynSeqDisableTorsoPointingTime(DynSequencer *pSqr);

F32 dynSeqGetBlend(DynSequencer *pSqr);
bool dynSeqIsOverlay_DbgOnly(DynSequencer *pSqr); // please only use this for danimShowBits