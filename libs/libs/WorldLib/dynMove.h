
#pragma once
GCC_SYSTEM

/******************

A DynMoveAnimTrack is a DynAnimTrack along with frame ranges, temporal offsets, per-bone blend factors, flags, etc. 

A DynMove is a collection of these DynMoveAnimTrack's under one name, which provides a layer of indirection between anim tracks and 
the sequencer.

******************/

#include "ReferenceSystem.h"

#include "dynNode.h"

#define DYNMOVE_DICTNAME "DynMove"
#define DYNMOVE_TYPENAME "DynMove"
#define POWER_MOVEMENT_BONE_NAME "Movement"

typedef struct DynAnimTrackHeader DynAnimTrackHeader;
typedef struct DynNode DynNode;
typedef struct DynTransform DynTransform;
typedef struct DynAnimTrackLocalCache DynAnimTrackLocalCache;
typedef struct SkelInfo SkelInfo;
typedef struct DynMove DynMove;
typedef struct DynRagdollState DynRagdollState;

extern DictionaryHandle hDynMoveDict;
extern U32 uiFixDynMoveOffByOneError;
extern U32 uiShowMatchJointsBaseSkeleton;
extern StaticDefineInt eEaseTypeEnum[];

AUTO_ENUM;
typedef enum eEaseType
{
	eEaseType_None,		ENAMES(Normal	None)
	eEaseType_Low,		ENAMES(Slow		Low)
	eEaseType_Medium,	ENAMES(Slower	Medium)
	eEaseType_High,		ENAMES(Slowest	High)
	eEaseType_LowInv,	ENAMES(Fast)
	eEaseType_MediumInv,ENAMES(Faster)
	eEaseType_HighInv,	ENAMES(Fastest)
}
eEaseType;

AUTO_STRUCT;
typedef struct DynAnimInterpolation
{
	F32 fInterpolation;
	eEaseType easeIn; AST(NAME(EaseIn))
	eEaseType easeOut; AST(NAME(EaseOut))
}
DynAnimInterpolation;
extern ParseTable parse_DynAnimInterpolation[];
#define TYPE_parse_DynAnimInterpolation DynAnimInterpolation


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct DynMoveFrameRange
{
	int iFirst;						AST(STRUCTPARAM)
	int iLast;						AST(STRUCTPARAM)
	int iFirstFrame;				NO_AST
	int iLastFrame;					NO_AST
}
DynMoveFrameRange;
extern ParseTable parse_DynMoveFrameRange[];
#define TYPE_parse_DynMoveFrameRange DynMoveFrameRange

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct DynMoveBoneOffset
{
	const char* pcBoneName;			AST(POOL_STRING STRUCTPARAM)
	Vec3 vOffset;					AST(STRUCTPARAM)
}
DynMoveBoneOffset;
extern ParseTable parse_DynMoveBoneOffset[];
#define TYPE_parse_DynMoveBoneOffset DynMoveBoneOffset

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct DynMoveBoneRotOffset
{
	const char* pcBoneName;			AST(STRUCTPARAM POOL_STRING)
	Vec3 rotOffset;					AST(STRUCTPARAM)				// version for the animators to edit
	Quat rotOffsetRuntime;			AST(STRUCTPARAM NO_TEXT_SAVE)	// version for the engine to use
}
DynMoveBoneRotOffset;
extern ParseTable parse_DynMoveBoneRotOffset[];
#define TYPE_parse_DynMoveBoneRotOffset DynMoveBoneRotOffset

AUTO_STRUCT;
typedef struct DynAnimFrameSnapshotBone
{
	const char* pcName; AST(POOL_STRING KEY)
	Vec3 qCompressedRot;
	Vec3 vPos;
	Vec3 vScale;
}
DynAnimFrameSnapshotBone;
extern ParseTable parse_DynAnimFrameSnapshotBone[];
#define TYPE_parse_DynAnimFrameSnapshotBone DynAnimFrameSnapshotBone

AUTO_STRUCT;
typedef struct DynAnimFrameSnapshotBoneRotationOnly
{
	const char* pcName; AST(POOL_STRING KEY)
	Vec3 qCompressedRot;
}
DynAnimFrameSnapshotBoneRotationOnly;
extern ParseTable parse_DynAnimFrameSnapshotBoneRotationOnly[];
#define TYPE_parse_DynAnimFrameSnapshotBoneRotationOnly DynAnimFrameSnapshotBoneRotationOnly

AUTO_STRUCT;
typedef struct DynAnimFrameSnapshot
{
	DynAnimFrameSnapshotBone** eaBones;
	DynAnimFrameSnapshotBoneRotationOnly** eaBonesRotOnly;
}
DynAnimFrameSnapshot;
extern ParseTable parse_DynAnimFrameSnapshot[];
#define TYPE_parse_DynAnimFrameSnapshot DynAnimFrameSnapshot

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynMoveFxEvent
{
	const char*	pcFx;	AST(REQUIRED POOL_STRING)
	U32 uiFrame;		AST(NAME("Frame"))
	U32 uiFrameTrigger; AST(NO_TEXT_SAVE)//Set in verify step, stored in bin
	bool bMessage;
}
DynMoveFxEvent;
extern ParseTable parse_DynMoveFxEvent[];
#define TYPE_parse_DynMoveFxEvent DynMoveFxEvent

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynMoveTag
{
	const char* pcTag;	AST(POOL_STRING)
}
DynMoveTag;
extern ParseTable parse_DynMoveTag[];
#define TYPE_parse_DynMoveTag DynMoveTag

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynMoveMatchBaseSkelAnim
{
	const char* pcBoneName;	AST(POOL_STRING)
	F32 fBlendInTime;
	F32 fBlendOutTime;
	bool bPlayBlendOutDuringMove;	AST(BOOLFLAG)
	bool bStartFullyBlended;		AST(BOOLFLAG)
}
DynMoveMatchBaseSkelAnim;
extern ParseTable parse_DynMoveMatchBaseSkelAnim[];
#define TYPE_parse_DynMoveMatchBaseSkelAnim DynMoveMatchBaseSkelAnim

// Using TOK_USEROPTIONBIT_1 to tell the textparser to not save this field in the editor
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynMoveAnimTrack
{
	DynAnimTrackHeader*		pAnimTrackHeader;		NO_AST							// The actual DynAnimTrack this references
	U32						uiFirst;				AST(NAME("First"))				// The start of the frame range
	U32						uiLast;					AST(NAME("Last"))				// The end of the frame range
	U32						uiFirstFrame;			AST(NO_TEXT_SAVE)
	U32						uiLastFrame;			AST(NO_TEXT_SAVE)
	U32						uiStartOffsetFirst;		AST(NAME("StartOffsetFirst"))
	U32						uiStartOffsetLast;		AST(NAME("StartOffsetLast"))
	U32						uiStartOffsetFirstFrame;AST(NO_TEXT_SAVE)
	U32						uiStartOffsetLastFrame;	AST(NO_TEXT_SAVE)
	U32						uiFrameOffset;			AST(NAME("Offset"))				// The offset relative to the other DynMoveAnimTrack's
	DynAnimFrameSnapshot	frameSnapshot;			AST(NO_TEXT_SAVE)//USERFLAG(TOK_USEROPTIONBIT_1))
	DynMoveFrameRange**		eaNoInterpFrameRange;	AST(NAME("NonInterpFrames"))		// Ranges of frames to do no interpolation for
	DynMoveBoneOffset**		eaBoneOffset;
	DynMoveBoneRotOffset**	eaBoneRotation;
	bool					bNoInterp;				AST(NO_TEXT_SAVE)
	const char*				pcAnimTrackName;		AST(POOL_STRING STRUCTPARAM)	// For loading, we lookup the anim track by this name
	bool					bVerified;				AST(NO_TEXT_SAVE)//USERFLAG(TOK_USEROPTIONBIT_1))
}
DynMoveAnimTrack; // A DynAnimTrack with frame ranges, offsets, and blend weights
extern ParseTable parse_DynMoveAnimTrack[];
#define TYPE_parse_DynMoveAnimTrack DynMoveAnimTrack

// Using TOK_USEROPTIONBIT_1 to tell the textparser to not save this field in the editor
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynMoveSeq
{
	const char*				pcDynMoveSeq;			AST(POOL_STRING STRUCTPARAM)	// The name of the DynMoveSeq, to compare against what sequencer is being used
	const DynMove*			pDynMove;				NO_AST							// Our parent move
	DynMoveAnimTrack		dynMoveAnimTrack;		AST(NAME("DynAnimTrack"))
	DynMoveFxEvent**        eaDynMoveFxEvents;      AST(NAME("DynFxEvent")) //EArray of the Fx Events
	DynMoveMatchBaseSkelAnim** eaMatchBaseSkelAnim;
	U32						uiRagdollFrame;			AST(NAME("RagdollFrame"))
	F32						fRagdollStartTime;		AST(NO_TEXT_SAVE)
	F32						fRagdollAdditionalGravity;
	F32						fDisableTorsoPointingTimeout;
	F32						fSpeed;					AST(DEF(1.0f) NAME(Speed,SpeedLow))
	F32						fSpeedHigh;
	const char*				pcIKTarget;				AST(POOL_STRING)
	const char*				pcIKTargetNodeLeft;		AST(POOL_STRING)
	const char*				pcIKTargetNodeRight;	AST(POOL_STRING)
	F32						fBankMaxAngle;
	F32						fBankScale;
	F32						fDistance;		
	F32						fMinRate;		
	F32						fMaxRate;
	F32						fBlendFrames;			AST(DEFAULT(7.5f))
	F32						fBlendRate;				AST(NO_TEXT_SAVE)
	F32						fLength;				AST(NO_TEXT_SAVE)
	bool					bRandSpeed;				AST(NO_TEXT_SAVE)
	bool					bVerified;				AST(NO_TEXT_SAVE)// USERFLAG(TOK_USEROPTIONBIT_1))//Set in verify step, stored in bin
	bool					bRagdoll;				AST(BOOLFLAG)
	bool					bIKBothHands;			AST(BOOLFLAG)
	bool					bRegisterWep;			AST(BOOLFLAG)
	bool					bIKMeleeMode;			AST(BOOLFLAG)
	bool					bEnableIKSliding;		AST(BOOLFLAG)
	bool					bDisableIKLeftWrist;	AST(BOOLFLAG)
	bool					bDisableIKRightArm;		AST(BOOLFLAG)
	bool					bShowWhileStopped;		AST(NAME(PlayWhileStopped, ShowWhileStopped) BOOLFLAG) //causes the lower body blend to stay on when not moving
	bool					bEnableTerrainTiltBlend;AST(BOOLFLAG)	
}
DynMoveSeq; // A DynMoveSeq is a way of having a move behave differently for different sequencer types
extern ParseTable parse_DynMoveSeq[];
#define TYPE_parse_DynMoveSeq DynMoveSeq

// Using TOK_USEROPTIONBIT_1 to tell the textparser to not save this field in the editor
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynMove
{
	const char*				pcName;				AST(POOL_STRING STRUCTPARAM KEY)	// The name of the DynMove
	const char*				pcFilename;			AST(POOL_STRING CURRENTFILE)				// The name of the file this was loaded from 
	const char*				pcComments;			AST(SERVER_ONLY)
	const char*				pcScope;			AST(POOL_STRING SERVER_ONLY NO_TEXT_SAVE)
	const char*				pcUserFilename;		AST(POOL_STRING SERVER_ONLY NO_TEXT_SAVE)
	const char*				pcUserScope;		AST(POOL_STRING SERVER_ONLY NO_TEXT_SAVE)
	DynMoveTag**			eaDynMoveTags;		AST(NAME("DynMoveTag"))
	DynMoveSeq**			eaDynMoveSeqs;		AST(NAME("DynMoveSeq"))							// EArray of the DynMoveSeqs
	bool					bVerified;			AST(NO_TEXT_SAVE) //USERFLAG(TOK_USEROPTIONBIT_1))//Set in verify step, stored in bin

	U32 uiReportCount; NO_AST
}
DynMove; // A collection of DynMoveAnimTrack's and information about how they interact.
extern ParseTable parse_DynMove[];
#define TYPE_parse_DynMove DynMove


void dynMoveLoadAll(void);
void dynMoveManagerReloadedAnimTrack(const char* pcAnimTrackName);

const DynMove* dynMoveFromName(const char* pcMoveName);

// TEMP for now, use first animtrack
__forceinline static F32 dynMoveSeqGetLength(const DynMoveSeq* pMoveSeq)
{
	return pMoveSeq->fLength;
}
const DynMoveSeq* dynMoveSeqFromDynMove(const DynMove* pDynMove, const SkelInfo* pSkelInfo);

U32 dynMoveSeqGetStartOffset(const DynMoveSeq *pDynMoveSeq);
F32 dynMoveSeqGetRandPlaybackSpeed(const DynMoveSeq *pDynMoveSeq);

bool dynMoveSeqCalcTransform(const DynMoveSeq* pDynMoveSeq, F32 fFrameTime, DynTransform* pTransform, const char* pcBoneTag, const DynTransform* pxBaseTransform, DynRagdollState* pRagdollState, DynNode* pRoot, bool bApplyOffsets);

F32 dynAnimInterpolationCalcInterp(F32 fInputValue, const DynAnimInterpolation* pInterp);
F32 dynMoveSeqAdvanceTime(const DynMoveSeq* pMoveSeq, F32 fDeltaTime, DynSkeleton* pSkeleton, F32* pfFrameTimeInOut, F32 fPlaybackSpeed, S32 useDistance);

bool dynMoveVerify(DynMove* pDynMove, U32 bBuildSnapShot);

int dynMoveGetSearchStringCount(const DynMove *pDynMove, const char *pcSearchText);