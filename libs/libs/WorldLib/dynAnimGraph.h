#pragma once
GCC_SYSTEM

#include "referencesystem.h"

#include "dynMove.h"
#include "dynMovement.h"
#include "dynSkeleton.h"

#define ANIM_GRAPH_EDITED_DICTIONARY "AnimGraph"
#define DEFAULT_ANIM_TEMPLATE_NODE_Y 50
#define ANIMGRAPH_MIN_MOVEBLENDTRIGGER 0.01
#define ANIMGRAPH_MAX_MOVEBLENDTRIGGER 1.00

typedef struct DynAnimTemplate DynAnimTemplate;
typedef struct DynAnimTemplateNode DynAnimTemplateNode;
typedef struct DynAnimTemplatePath DynAnimTemplatePath;
typedef struct DynMove DynMove;
typedef struct DynAnimGraph DynAnimGraph;

extern DictionaryHandle hAnimGraphDict;

AUTO_STRUCT;
typedef struct DynAnimGraphFxEvent
{
	const char*	pcFx;		AST(REQUIRED POOL_STRING)
	int iFrame;
	F32 fMovementBlendTrigger;
	bool bMovementBlendTriggered;
	bool bMessage;
}
DynAnimGraphFxEvent;
extern ParseTable parse_DynAnimGraphFxEvent[];
#define TYPE_parse_DynAnimGraphFxEvent DynAnimGraphFxEvent

AUTO_ENUM;
typedef enum DynAnimGraphImpactDirection
{
	DAGID_Front_to_Back, //  0  0  1
	DAGID_Back_to_Front, //  0  0 -1
	DAGID_Right_to_Left, // -1  0  0
	DAGID_Left_to_Right, //  1  0  0
	DAGID_Up_to_Down,	 //  0 -1  0
	DAGID_Down_to_Up,	 //  0  1  0
}
DynAnimGraphImpactDirection;
extern StaticDefineInt DynAnimGraphImpactDirectionEnum[];

AUTO_STRUCT;
typedef struct DynAnimGraphTriggerImpact
{
	//Vec3 vDirection;
	DynAnimGraphImpactDirection eDirection;
	const char* pcBone;			AST(POOL_STRING)
	int iFrame;
	Vec3 vOffset;
}
DynAnimGraphTriggerImpact;
extern ParseTable parse_DynAnimGraphTriggerImpact[];
#define TYPE_parse_DynAnimGraphTriggerImpact DynAnimGraphTriggerImpact

AUTO_STRUCT;
typedef struct DynAnimGraphMove
{
	REF_TO(DynMove) hMove; AST(NAME(Move) REQUIRED NON_NULL_REF)
	DynAnimGraphFxEvent** eaFxEvent;
	DynMovementDirection eDirection;
	F32 fChance;
	bool bEditorPlaybackBias; AST(BOOLFLAG)
}
DynAnimGraphMove;
extern ParseTable parse_DynAnimGraphMove[];
#define TYPE_parse_DynAnimGraphMove DynAnimGraphMove

AUTO_STRUCT;
typedef struct DynAnimGraphSuppress
{
	const char*	pcSuppressionTag;	AST(REQUIRED POOL_STRING)
}
DynAnimGraphSuppress;
extern ParseTable parse_DynAnimGraphSuppress[];
#define TYPE_parse_DynAnimGraphSuppress DynAnimGraphSuppress

AUTO_STRUCT;
typedef struct DynAnimGraphStance
{
	const char*	pcStance;	AST(REQUIRED POOL_STRING)
}
DynAnimGraphStance;
extern ParseTable parse_DynAnimGraphStance[];
#define TYPE_parse_DynAnimGraphStance DynAnimGraphStance

AUTO_STRUCT;
typedef struct DynAnimGraphSwitch
{
	F32 fRequiredPlaytime;
	bool bInterrupt;
}
DynAnimGraphSwitch;
extern ParseTable parse_DynAnimGraphSwitch[];
#define TYPE_parse_DynAnimGraphSwitch DynAnimGraphSwitch

AUTO_STRUCT;
typedef struct DynAnimGraphPath
{
	F32 fChance;
	bool bEditorPlaybackBias; AST(BOOLFLAG)
}
DynAnimGraphPath;
extern ParseTable parse_DynAnimGraphPath[];
#define TYPE_parse_DynAnimGraphPath DynAnimGraphPath

AUTO_STRUCT;
typedef struct DynAnimGraphDirectionalData
{
	DynAnimGraphMove** eaMove;
}
DynAnimGraphDirectionalData;
extern ParseTable parse_DynAnimGraphDirectionalData[];
#define TYPE_parse_DynAnimGraphDirectionalData DynAnimGraphDirectionalData

AUTO_STRUCT;
typedef struct DynAnimGraphNodeInheritBits
{
	bool bInheritSwitch_WhenFalse;						AST(NAME(InheritSwitch))
	bool bInheritPath_WhenFalse;						AST(NAME(InheritPath))
	bool bInheritInterpolation_WhenFalse;				AST(NAME(InheritInterpolation))
	bool bInheritEaseIn_WhenFalse;						AST(NAME(InheritEaseIn))
	bool bInheritEaseOut_WhenFalse;						AST(NAME(InheritEaseOut))
	bool bInheritNoSelfInterp_WhenFalse;				AST(NAME(InheritNoSelfInterp))
	bool bInheritForceEndFreeze_WhenFalse;				AST(NAME(InheritForceEndFreeze))
	bool bInheritOverrideAllBones_WhenFalse;			AST(NAME(InheritOverrideAllBones))
	bool bInheritOverrideMovement_WhenFalse;			AST(NAME(InheritOverrideMovement))
	bool bInheritOverrideDefaultMove_WhenFalse;			AST(NAME(InheritOverrideDefaultMove))
	bool bInheritAllowRandomMoveRepeats_WhenFalse;		AST(NAME(InheritAllowRandomMoveRepeats))
	bool bInheritDisableTorsoPointing_WhenFalse;		AST(NAME(InheritDisableTorsoPointing))
	bool bInheritDisableGroundReg_WhenFalse;			AST(NAME(InheritDisableGroundReg))
	bool bInheritDisableUpperBodyGroundReg_WhenFalse;	AST(NAME(InheritDisableUpperBodyGroundReg))
	bool bInheritFxEvent_WhenFalse;						AST(NAME(InheritFxEvent))
	bool bInheritGraphImpact_WhenFalse;					AST(NAME(InheritGraphImpact))
}
DynAnimGraphNodeInheritBits;
extern ParseTable parse_DynAnimGraphNodeInheritBits[];
#define TYPE_parse_DynAnimGraphNodeInheritBits DynAnimGraphNodeInheritBits

AUTO_STRUCT;
typedef struct DynAnimGraphNode
{
	// IMPORTANT: if you add a new field, make sure to update dynAnimGraphApplyGraphNodeInheritBits

	const char* pcName; AST(POOL_STRING)
	DynAnimTemplateNode* pTemplateNode; NO_AST
	DynAnimGraphSwitch** eaSwitch;
	DynAnimGraphPath **eaPath;
	F32 fX;
	F32 fY;

	// IMPORTANT: if you add a new field, make sure to update dynAnimGraphApplyGraphNodeInheritBits

	DynAnimGraphMove** eaMove;
	DynAnimGraphDirectionalData** eaDirectionalData; AST(NO_TEXT_SAVE)
	DynMovementNumDirections eNumDirections; AST(NO_TEXT_SAVE)
	DynAnimInterpolation interpBlock; AST( EMBEDDED_FLAT )

	// IMPORTANT: if you add a new field, make sure to update dynAnimGraphApplyGraphNodeInheritBits

	DynAnimGraphFxEvent** eaFxEvent;
	DynAnimGraphTriggerImpact **eaGraphImpact;
	REF_TO(DynAnimGraph) hPostIdle; AST(NAME(PostIdle))

	// IMPORTANT: if you add a new field, make sure to update dynAnimGraphApplyGraphNodeInheritBits

	F32 fDisableTorsoPointingTimeout;
	F32 fDisableGroundRegTimeout;
	F32 fDisableUpperBodyGroundRegTimeout;

	// IMPORTANT: if you add a new field, make sure to update dynAnimGraphApplyGraphNodeInheritBits

	bool bNoSelfInterp;
	bool bForceEndFreeze;
	bool bOverrideAllBones;
	bool bSnapOverrideAllBones;
	bool bOverrideMovement;
	bool bSnapOverrideMovement;
	bool bOverrideDefaultMove;
	bool bSnapOverrideDefaultMove;
	bool bAllowRandomMoveRepeats;
	bool bInvalidForPlayback; AST(NO_TEXT_SAVE)

	// IMPORTANT: if you add a new field, make sure to update dynAnimGraphApplyGraphNodeInheritBits

	DynAnimGraphNodeInheritBits *pInheritBits;
	U32	bfParamsSpecified[2];		AST( USEDFIELD )
	SkelBoneVisibilitySet eVisSet;	AST(DEFAULT(-1) NAME("BoneVisSet"))
}
DynAnimGraphNode;
extern ParseTable parse_DynAnimGraphNode[];
#define TYPE_parse_DynAnimGraphNode DynAnimGraphNode

AUTO_STRUCT;
typedef struct DynAnimGraphInheritBits
{
	bool bInheritTimeout_WhenFalse;						AST(NAME(InheritTimeout))
	bool bInheritOverrideAllBones_WhenFalse;			AST(NAME(InheritOverrideAllBones))
	bool bInheritOverrideMovement_WhenFalse;			AST(NAME(InheritOverrideMovement))
	bool bInheritOverrideDefaultMove_WhenFalse;			AST(NAME(InheritOverrideDefaultMove))
	bool bInheritForceVisible_WhenFalse;				AST(NAME(InheritForceVisible))
	bool bInheritPitchToTarget_WhenFalse;				AST(NAME(InheritPitchToTarget))
	bool bInheritDisableTorsoPointing_WhenFalse;		AST(NAME(InheritDisableTorsoPointing))
	bool bInheritDisableGroundReg_WhenFalse;			AST(NAME(InheritDisableGroundReg))
	bool bInheritDisableUpperBodyGroundReg_WhenFalse;	AST(NAME(InheritDisableUpperBodyGroundReg))
	bool bInheritOnEnterFxEvent_WhenFalse;				AST(NAME(InheritOnEnterFxEvent))
	bool bInheritOnExitFxEvent_WhenFalse;				AST(NAME(InheritOnExitFxEvent))
	bool bInheritSuppress_WhenFalse;					AST(NAME(InheritSuppress))
	bool bInheritStance_WhenFalse;						AST(NAME(InheritStance))
	bool bInheritGeneratePowerMovementInfo_WhenFalse;	AST(NAME(InheritGeneratePowerMovementInfo))
	bool bInheritResetWhenMovementStops_WhenFalse;		AST(NAME(InheritResetWhenMovementStops))
}
DynAnimGraphInheritBits;
extern ParseTable parse_DynAnimGraphInheritBits[];
#define TYPE_parse_DynAnimGraphInheritBits DynAnimGraphInheritBits

AUTO_STRUCT;
typedef struct DynPowerMovementFrame
{
	// The frame number
	S32 iFrame;	AST(KEY)

	// The position
	Vec3 vPos;
}
DynPowerMovementFrame;

AUTO_STRUCT;
typedef struct DynPowerMovement
{
	// The list of frames (sorted by frame number)
	DynPowerMovementFrame **eaFrameList;

	// If this flag is set, no power movement happens
	// during the pre-activation phase. This is caused
	// by having an activation cycle which makes determining
	// the pre-activate timeline impossible.
	U32 bSkipPreActivationPhase : 1;
}
DynPowerMovement;

AUTO_STRUCT;
typedef struct DynPowerMovementInfo
{
	// Default movement
	DynPowerMovement *pDefaultMovement;

	// Hit pause movement
	DynPowerMovement *pHitPauseMovement;
}
DynPowerMovementInfo;

AUTO_STRUCT;
typedef struct DynAnimGraph
{
	// IMPORTANT: if you add a new field, make sure to update dynAnimGraphApplyPropertyInheritBits

	const char* pcName; AST(POOL_STRING KEY)
	const char* pcFilename; AST(POOL_STRING CURRENTFILE)
	const char* pcComments;	AST(SERVER_ONLY)
	const char* pcScope;	AST(POOL_STRING SERVER_ONLY)
	REF_TO(DynAnimTemplate) hTemplate; AST(NAME(Template) REQUIRED NON_NULL_REF)
	DynAnimGraphNode** eaNodes;
	bool bPartialGraph;

	// IMPORTANT: if you add a new field, make sure to update dynAnimGraphApplyPropertyInheritBits

	DynAnimGraphSuppress **eaSuppress;
	DynAnimGraphStance **eaStance; // stances set while graph is active
	DynAnimGraphFxEvent **eaOnEnterFxEvent;
	DynAnimGraphFxEvent **eaOnExitFxEvent;

	// IMPORTANT: if you add a new field, make sure to update dynAnimGraphApplyPropertyInheritBits

	// Power movement information is extracted from animation tracks
	// which contain movement bone. The information is processed after
	// reading from text and saved with the binned data.
	DynPowerMovementInfo *pPowerMovementInfo;	AST(NO_TEXT_SAVE)

	// IMPORTANT: if you add a new field, make sure to update dynAnimGraphApplyPropertyInheritBits

	F32 fTimeout;
	F32 fDisableTorsoPointingRate;
	F32 fDisableTorsoPointingTimeout;
	F32 fDisableGroundRegTimeout;
	F32 fDisableUpperBodyGroundRegTimeout;

	// IMPORTANT: if you add a new field, make sure to update dynAnimGraphApplyPropertyInheritBits

	bool bOverrideAllBones;
	bool bSnapOverrideAllBones;
	bool bOverrideMovement;
	bool bSnapOverrideMovement;
	bool bOverrideDefaultMove;
	bool bSnapOverrideDefaultMove;
	bool bForceVisible;
	bool bPitchToTarget;
	bool bDisableTorsoPointing;
	bool bResetWhenMovementStops;
	bool bGeneratePowerMovementInfo;
	bool bReversedInheritBitPolarity;
	
	// IMPORTANT: if you add a new field, make sure to update dynAnimGraphApplyPropertyInheritBits

	bool bNeedsReloadRefresh;	NO_AST
	bool bInvalid;				NO_AST
	DynAnimGraphInheritBits *pInheritBits;
	U32	bfParamsSpecified[2];				AST( USEDFIELD )
	U32 uiReportCount;			NO_AST
}
DynAnimGraph;
extern ParseTable parse_DynAnimGraph[];
#define TYPE_parse_DynAnimGraph DynAnimGraph

void dynAnimGraphInit(DynAnimGraph* pGraph);
bool dynAnimGraphNodeVerify(DynAnimGraph* pGraph, DynAnimGraphNode* pNode, bool bReport);
bool dynAnimGraphVerify(DynAnimGraph* pGraph, bool bReport);
void dynAnimGraphLoadAll(void);
void dynAnimGraphReloadAll(void);
bool dynAnimGraphIndividualMoveVerifyFx(DynAnimGraph *pGraph, DynAnimGraphNode *pNode, DynAnimGraphMove *pMove, DynAnimGraphFxEvent *pFx, bool bReport);
bool dynAnimGraphIndividualMoveVerify(DynAnimGraph* pGraph, DynAnimGraphNode* pNode, DynAnimGraphMove* pMove, bool bReport);
bool dynAnimGraphIndividualPathVerify(DynAnimGraph *pGraph, DynAnimGraphNode *pNode, DynAnimGraphPath *pPath, bool bReport);
bool dynAnimGraphGroupMoveVerifyChance(DynAnimGraph *pGraph, DynAnimGraphNode *pNode, S32 iDirection, bool bReport);
bool dynAnimGraphGroupMoveVerify(DynAnimGraph *pGraph, DynAnimGraphNode *pNode, bool bReport);
bool dynAnimGraphGroupPathVerify(DynAnimGraph *pGraph, DynAnimGraphNode *pNode, bool bReport);

void dynAnimGraphTemplateChanged(DynAnimGraph* pGraph);
void dynAnimGraphNodeNormalizeChances(DynAnimGraphNode* pNode);

bool dynAnimGraphOnPathRandomizer(const DynAnimGraphNode *pNode);
bool dynAnimGraphNextNodeIsEndFated(const DynAnimGraph* pGraph,
									const DynAnimGraphNode* pNode,
									const char* pcFlag,
									F32 fFate);
const DynAnimGraph *dynAnimGraphGetPostIdleFated(	const DynAnimGraph *pGraph,
													const DynAnimGraphNode *pNode,
													const char *pcFlag,
													F32 fFate);
const DynAnimGraphNode* dynAnimGraphNodeGetNextNodeFated(	const DynAnimGraph* pGraph,
															const DynAnimGraphNode* pNode,
															const char* pcFlag,
															const char* pcResetFlag,
															F32 fFate);
const DynAnimGraphNode* dynAnimGraphFindNode(const DynAnimGraph* pGraph, const char* pcNodeName);
const DynAnimGraphMove* dynAnimGraphNodeChooseMove(	const DynAnimGraph *pGraph,
													const DynAnimGraphNode* pNode,
													const DynAnimGraphMove *pMovePrev);

bool dynAnimGraphNodeIsValidForPlayback(const DynAnimGraph* pGraph, const DynAnimTemplateNode* pTemplateNode);

void dynAnimCheckDataReload(void);
void danimForceDataReload(void);

void dynAnimGraphServerReload(void);
void danimForceServerDataReload(void);

void dynAnimGraphFixup(DynAnimGraph *pGraph);
void dynAnimGraphFixupValidPlaybackPaths(DynAnimGraph* pGraph);
void dynAnimGraphNodeNameFixup(DynAnimGraph *pGraph, DynAnimGraphNode *pGraphNode, const char *oldName, const char *newName);
void dynAnimGraphDefaultsFixup(DynAnimGraph *pGraph);
void dynAnimGraphApplyInheritBits(DynAnimGraph *pGraph);

void dynAnimGraphSetEditorMode(bool mode);

int dynAnimGraphCompareMoveDisplayOrder(const void** pa, const void** pb);

int dynAnimGraphGetSearchStringCount(const DynAnimGraph *pGraph, const char *pcSearchText);