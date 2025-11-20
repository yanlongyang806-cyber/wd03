#include "dynAnimGraph.h"

#include "error.h"
#include "fileutil.h"
#include "foldercache.h"
#include "StringCache.h"
#include "mathutil.h"
#include "rand.h"
#include "ResourceManager.h"
#include "timing.h"
#include "SimpleParser.h"
#include "ResourceSystem_Internal.h"

#include "dynSeqData.h"
#include "dynSkeleton.h"
#include "dynAnimTemplate.h"
#include "dynAnimChart.h"
#include "dynFxManager.h"
#include "dynFxInfo.h"
#include "dynAnimTrack.h"
#include "wlState.h"

#include "dynAnimGraph_h_ast.h"

#define POWER_MOVEMENT_GENERATION_MAX_ITERATIONS 50

static int bLoadedOnce = false;
static bool bAnimGraphEditorMode = false;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

#define EPSILON 0.0001f

DictionaryHandle hAnimGraphDict;

typedef enum PowerMovementInfoGenerationResult
{
	PowerMovementInfoGenerationResult_Success,
	PowerMovementInfoGenerationResult_Failure_MoreThanOneMovePerNode,
	PowerMovementInfoGenerationResult_Failure_MoreThanSequencePerMove,
	PowerMovementInfoGenerationResult_Failure_TooManyCyclicNodeReferences,
	PowerMovementInfoGenerationResult_Failure_PreActivateCyclicReferenceComesAfterActivateCyclicReference,
	PowerMovementInfoGenerationResult_Failure_MaxIterationsReached,
	PowerMovementInfoGenerationResult_Failure_MultiplePreActivateCycles,
	PowerMovementInfoGenerationResult_Failure_MultipleActivateCycles,
	PowerMovementInfoGenerationResult_Failure_SelfReferencingActivateCycle,
	PowerMovementInfoGenerationResult_Failure_SelfReferencingPreActivateCycle,
	PowerMovementInfoGenerationResult_Failure_AnimationTrackFailedToLoad,
	PowerMovementInfoGenerationResult_Failure_AnimationTrackNotUncompressed,
	PowerMovementInfoGenerationResult_Failure_MovementBoneInSnapshotButNotInTrack
} PowerMovementInfoGenerationResult;

static void dynAnimGraphSetupNewInheritBits(DynAnimGraph* pGraph);
static void dynAnimGraphBuildDirectionalData(DynAnimGraph* pGraph);
static bool dynAnimGraphNodeRequiresValidPlaybackPath(const DynAnimGraph* pGraph, const DynAnimGraphNode* pFindNode, const DynAnimGraphNode* pNode, const DynAnimGraphNode*** peaSeenNodes, bool bSawMoveIn, S32 *piSawMoveOut, bool bAllowSwitches);
static bool dynAnimGraphNodeOnValidPlaybackPath(const DynAnimGraph* pGraph, const DynAnimGraphNode* pNode, const DynAnimGraphNode*** peaSeenNodes, bool bAllowEndNode, bool bReport);

void dynAnimCheckDataReload(void)
{
	PERFINFO_AUTO_START_FUNC();
	if (dynDebugState.bReloadAnimData)
	{
		dynAnimGraphReloadAll();
		dynAnimChartReloadAll();
		dynSkeletonResetAllAnims();
		dynDebugState.uiAnimDataResetFrame = wl_state.frame_count;
		dynDebugState.bReloadAnimData = false;
	}
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_CATEGORY(dynAnim);
void danimForceDataReload(void)
{
	dynDebugState.bReloadAnimData = true;
}

AUTO_COMMAND ACMD_CATEGORY(dynAnim);
void danimForceServerDataReload(void)
{
	dynDebugState.bReloadServerAnimData = true;
}

AUTO_RUN;
void dynAnimGraph_InitStrings(void)
{
}

void dynAnimGraphSetEditorMode(bool mode)
{
	bAnimGraphEditorMode = mode;
}

void dynAnimGraphInit(DynAnimGraph* pGraph)
{
	dynAnimGraphSetupNewInheritBits(pGraph);
}

static DynAnimGraphNode *dynAnimGraphNodeFromTemplateNode(const DynAnimGraph *pGraph, const DynAnimTemplateNode *pTemplateNode)
{
	FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pGraphNode)
	{
		if (pTemplateNode->pcName == pGraphNode->pcName)
			return pGraphNode;
	}
	FOR_EACH_END;
	return NULL;
}

static DynAnimGraphNode *dynAnimTemplateGraphNodeFromGraphNode(DynAnimGraph *pGraph, DynAnimGraphNode *pGraphNode)
{
	if (!pGraph->bPartialGraph)
	{
		DynAnimTemplate* pTemplate = GET_REF(pGraph->hTemplate);
		DynAnimGraph* pDefaultsGraph;

		assert(pTemplate);
		pDefaultsGraph = pTemplate->pDefaultsGraph;
		
		if (pDefaultsGraph)
		{
			FOR_EACH_IN_EARRAY(pDefaultsGraph->eaNodes, DynAnimGraphNode, pTemplateGraphNode)
			{
				if (pTemplateGraphNode->pcName == pGraphNode->pcName)
					return pTemplateGraphNode;
			}
			FOR_EACH_END;
		}
		Errorf("Could not find graph node %s in template %s", pGraphNode->pcName, pTemplate->pcName);
	}
	return NULL;
}

static DynAnimTemplateNode* dynAnimTemplateNodeFromGraphNode(DynAnimGraph* pGraph, DynAnimGraphNode* pGraphNode)
{
	if (!pGraph->bPartialGraph)
	{
		DynAnimTemplate* pTemplate = GET_REF(pGraph->hTemplate);
		assert(pTemplate);
		FOR_EACH_IN_EARRAY(pTemplate->eaNodes, DynAnimTemplateNode, pTemplateNode)
		{
			if (pTemplateNode->pcName == pGraphNode->pcName)
				return pTemplateNode;
		}
		FOR_EACH_END;
		Errorf("Could not find node %s in template %s", pGraphNode->pcName, pTemplate->pcName);
	}
	return NULL;
}


static void dynAnimGraphSetupGraphNodeInherits(DynAnimGraph *pGraph, DynAnimGraphNode *pGraphNode, bool defaultValue)
{
	if (!pGraph->bPartialGraph && !pGraphNode->pInheritBits)
	{
		DynAnimGraphNodeInheritBits* pGraphNodeInheritBits = StructCreate(parse_DynAnimGraphNodeInheritBits);
		pGraphNodeInheritBits->bInheritSwitch_WhenFalse						= defaultValue;
		pGraphNodeInheritBits->bInheritEaseIn_WhenFalse						= defaultValue;
		pGraphNodeInheritBits->bInheritEaseOut_WhenFalse					= defaultValue;
		pGraphNodeInheritBits->bInheritForceEndFreeze_WhenFalse				= defaultValue;
		pGraphNodeInheritBits->bInheritFxEvent_WhenFalse					= defaultValue;
		pGraphNodeInheritBits->bInheritGraphImpact_WhenFalse				= defaultValue;
		pGraphNodeInheritBits->bInheritInterpolation_WhenFalse				= defaultValue;
		pGraphNodeInheritBits->bInheritNoSelfInterp_WhenFalse				= defaultValue;
		pGraphNodeInheritBits->bInheritOverrideAllBones_WhenFalse			= defaultValue;
		pGraphNodeInheritBits->bInheritOverrideMovement_WhenFalse			= defaultValue;
		pGraphNodeInheritBits->bInheritOverrideDefaultMove_WhenFalse		= defaultValue;
		pGraphNodeInheritBits->bInheritAllowRandomMoveRepeats_WhenFalse		= defaultValue;
		pGraphNodeInheritBits->bInheritPath_WhenFalse						= defaultValue;
		pGraphNodeInheritBits->bInheritDisableTorsoPointing_WhenFalse		= defaultValue;
		pGraphNodeInheritBits->bInheritDisableGroundReg_WhenFalse			= defaultValue;
		pGraphNodeInheritBits->bInheritDisableUpperBodyGroundReg_WhenFalse	= defaultValue;
		pGraphNode->pInheritBits = pGraphNodeInheritBits;
	}
}

static void dynAnimGraphSetupPropertyInherits(DynAnimGraph* pGraph, bool defaultValue)
{
	if (!pGraph->bPartialGraph && !pGraph->pInheritBits)
	{
		DynAnimGraphInheritBits* pGraphInheritBits = StructCreate(parse_DynAnimGraphInheritBits);
		pGraphInheritBits->bInheritForceVisible_WhenFalse				= defaultValue;
		pGraphInheritBits->bInheritOnEnterFxEvent_WhenFalse				= defaultValue;
		pGraphInheritBits->bInheritOnExitFxEvent_WhenFalse				= defaultValue;
		pGraphInheritBits->bInheritOverrideAllBones_WhenFalse			= defaultValue;
		pGraphInheritBits->bInheritOverrideMovement_WhenFalse			= defaultValue;
		pGraphInheritBits->bInheritOverrideDefaultMove_WhenFalse		= defaultValue;
		pGraphInheritBits->bInheritPitchToTarget_WhenFalse				= defaultValue;
		pGraphInheritBits->bInheritDisableTorsoPointing_WhenFalse		= defaultValue;
		pGraphInheritBits->bInheritDisableGroundReg_WhenFalse			= defaultValue;
		pGraphInheritBits->bInheritDisableUpperBodyGroundReg_WhenFalse	= defaultValue;
		pGraphInheritBits->bInheritStance_WhenFalse						= defaultValue;
		pGraphInheritBits->bInheritSuppress_WhenFalse					= defaultValue;
		pGraphInheritBits->bInheritTimeout_WhenFalse					= defaultValue;
		pGraphInheritBits->bInheritGeneratePowerMovementInfo_WhenFalse	= defaultValue;
		pGraphInheritBits->bInheritResetWhenMovementStops_WhenFalse		= defaultValue;
		pGraph->pInheritBits = pGraphInheritBits;
	}
}

static void dynAnimGraphSetupInheritBits(DynAnimGraph *pGraph, bool defaultValue)
{
	if (!pGraph->bPartialGraph)
	{
		dynAnimGraphSetupPropertyInherits(pGraph, defaultValue);
		FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pGraphNode)
		{
			dynAnimGraphSetupGraphNodeInherits(pGraph, pGraphNode, defaultValue);
		}
		FOR_EACH_END;
	}
}

static void dynAnimGraphSetupNewInheritBits(DynAnimGraph *pGraph)
{
	pGraph->bReversedInheritBitPolarity = true;
	dynAnimGraphSetupInheritBits(pGraph, false);
}

static void dynAnimGraphSetupMissingInheritBits(DynAnimGraph *pGraph)
{
	dynAnimGraphSetupInheritBits(pGraph, false); // should only happen with very old graphs that need fixed up
}

static void dynAnimGraphFixupGraphNodeInheritBits(DynAnimGraphNodeInheritBits* pGraphNodeInheritBits)
{
#define FLIPBIT(x) x = !x
	FLIPBIT(pGraphNodeInheritBits->bInheritPath_WhenFalse);
	FLIPBIT(pGraphNodeInheritBits->bInheritInterpolation_WhenFalse);
	FLIPBIT(pGraphNodeInheritBits->bInheritEaseIn_WhenFalse);
	FLIPBIT(pGraphNodeInheritBits->bInheritEaseOut_WhenFalse);
	FLIPBIT(pGraphNodeInheritBits->bInheritNoSelfInterp_WhenFalse);
	FLIPBIT(pGraphNodeInheritBits->bInheritForceEndFreeze_WhenFalse);
	FLIPBIT(pGraphNodeInheritBits->bInheritOverrideAllBones_WhenFalse);
	FLIPBIT(pGraphNodeInheritBits->bInheritOverrideMovement_WhenFalse);
	FLIPBIT(pGraphNodeInheritBits->bInheritOverrideDefaultMove_WhenFalse);
	FLIPBIT(pGraphNodeInheritBits->bInheritAllowRandomMoveRepeats_WhenFalse);
	FLIPBIT(pGraphNodeInheritBits->bInheritDisableTorsoPointing_WhenFalse);
	FLIPBIT(pGraphNodeInheritBits->bInheritDisableGroundReg_WhenFalse);
	FLIPBIT(pGraphNodeInheritBits->bInheritDisableUpperBodyGroundReg_WhenFalse);
	FLIPBIT(pGraphNodeInheritBits->bInheritFxEvent_WhenFalse);
	FLIPBIT(pGraphNodeInheritBits->bInheritGraphImpact_WhenFalse);
#undef FLIPBIT
}

static void dynAnimGraphFixupGraphInheritBits(DynAnimGraphInheritBits* pGraphInheritBits)
{
#define FLIPBIT(x) x = !x
	FLIPBIT(pGraphInheritBits->bInheritTimeout_WhenFalse);
	FLIPBIT(pGraphInheritBits->bInheritOverrideAllBones_WhenFalse);
	FLIPBIT(pGraphInheritBits->bInheritOverrideMovement_WhenFalse);
	FLIPBIT(pGraphInheritBits->bInheritOverrideDefaultMove_WhenFalse);
	FLIPBIT(pGraphInheritBits->bInheritForceVisible_WhenFalse);
	FLIPBIT(pGraphInheritBits->bInheritPitchToTarget_WhenFalse);
	FLIPBIT(pGraphInheritBits->bInheritDisableTorsoPointing_WhenFalse);
	FLIPBIT(pGraphInheritBits->bInheritDisableGroundReg_WhenFalse);
	FLIPBIT(pGraphInheritBits->bInheritDisableUpperBodyGroundReg_WhenFalse);
	FLIPBIT(pGraphInheritBits->bInheritOnEnterFxEvent_WhenFalse);
	FLIPBIT(pGraphInheritBits->bInheritOnExitFxEvent_WhenFalse);
	FLIPBIT(pGraphInheritBits->bInheritSuppress_WhenFalse);
	FLIPBIT(pGraphInheritBits->bInheritStance_WhenFalse);
	FLIPBIT(pGraphInheritBits->bInheritGeneratePowerMovementInfo_WhenFalse);
	FLIPBIT(pGraphInheritBits->bInheritResetWhenMovementStops_WhenFalse);
#undef FLIPBIT
}

void dynAnimGraphFixupInheritBits(DynAnimGraph *pGraph)
{
	DynAnimTemplate *pTemplate = GET_REF(pGraph->hTemplate);

	if (!pGraph->bPartialGraph					&&
		SAFE_MEMBER(pTemplate,pDefaultsGraph)	&&
		FALSE_THEN_SET(pGraph->bReversedInheritBitPolarity))
	{
		assert(pGraph->pInheritBits);
		dynAnimGraphFixupGraphInheritBits(pGraph->pInheritBits);
		
		FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pGraphNode)
		{
			assert(pGraphNode->pInheritBits);
			dynAnimGraphFixupGraphNodeInheritBits(pGraphNode->pInheritBits);
		}
		FOR_EACH_END;
	}
}

static void dynAnimGraphApplyGraphNodeInheritBits(DynAnimGraph *pGraph, DynAnimGraphNode *pGraphNode)
{
	if (!pGraph->bPartialGraph && pGraphNode->pInheritBits)
	{
		DynAnimGraphNode *pTemplateGraphNode = dynAnimTemplateGraphNodeFromGraphNode(pGraph, pGraphNode);
		DynAnimGraphNodeInheritBits* pGraphNodeInheritBits = pGraphNode->pInheritBits;
		int bitFieldIndex = ParserGetUsedBitFieldIndex(parse_DynAnimGraphNode);

		//not copy from defaults when created
		int nameColumn, xColumn, yColumn;
		int moveColumn, directionalDataColumn, numDirectionsColumn;
		int postIdleColumn;
		int inheritBitsColumn;
		int boneVisSetColumn;
		int invalidForPlaybackColumn;

		//copy from defaults when created
		int interpolationColumn, easeInColumn, easeOutColumn;
		int noSelfInterpColumn;
		int forceEndFreezeColumn;
		int overrideAllBonesColumn, snapOverrideAllBonesColumn;
		int overrideMovementColumn, snapOverrideMovementColumn;
		int overrideDefaultMoveColumn, snapOverrideDefaultMoveColumn;
		int allowRandomMoveRepeatsColumn;
		int disableTorsoPointingColumn;
		int disableGroundRegColumn;
		int disableUpperBodyGroundRegColumn;
		int	fxEventColumn;
		int graphImpactColumn;
		int pathColumn;
		int switchColumn;

		//not copy from defaults when created
		ParserFindColumn(parse_DynAnimGraphNode, "Name",				&nameColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "X",					&yColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "Y",					&xColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "Move",				&moveColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "DirectionalData",		&directionalDataColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "NumDirections",		&numDirectionsColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "PostIdle",			&postIdleColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "InheritBits",			&inheritBitsColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "BoneVisSet",			&boneVisSetColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "InvalidForPlayback",	&invalidForPlaybackColumn);

		//copy from defaults when created
		ParserFindColumn(parse_DynAnimGraphNode, "Interpolation",					&interpolationColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "EaseIn",							&easeInColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "EaseOut",							&easeOutColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "NoSelfInterp",					&noSelfInterpColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "ForceEndFreeze",					&forceEndFreezeColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "OverrideAllBones",				&overrideAllBonesColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "SnapOverrideAllBones",			&snapOverrideAllBonesColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "OverrideMovement",				&overrideMovementColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "SnapOverrideMovement",			&snapOverrideMovementColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "OverrideDefaultMove",				&overrideDefaultMoveColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "SnapOverrideDefaultMove",			&snapOverrideDefaultMoveColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "AllowRandomMoveRepeats",			&allowRandomMoveRepeatsColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "DisableTorsoPointingTimeout",		&disableTorsoPointingColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "DisableGroundRegTimeout",			&disableGroundRegColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "DisableUpperBodyGroundRegTimeout",&disableUpperBodyGroundRegColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "FxEvent",							&fxEventColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "GraphImpact",						&graphImpactColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "Path",							&pathColumn);
		ParserFindColumn(parse_DynAnimGraphNode, "Switch",							&switchColumn);
		
		//set the not copy from defaults tokens
		TokenSetSpecified(parse_DynAnimGraphNode, nameColumn,			pGraphNode, bitFieldIndex, true);
		TokenSetSpecified(parse_DynAnimGraphNode, xColumn,				pGraphNode, bitFieldIndex, true);
		TokenSetSpecified(parse_DynAnimGraphNode, yColumn,				pGraphNode, bitFieldIndex, true);
		TokenSetSpecified(parse_DynAnimGraphNode, moveColumn,			pGraphNode, bitFieldIndex, true);
		TokenSetSpecified(parse_DynAnimGraphNode, directionalDataColumn,pGraphNode, bitFieldIndex, true);
		TokenSetSpecified(parse_DynAnimGraphNode, numDirectionsColumn,	pGraphNode, bitFieldIndex, true);
		TokenSetSpecified(parse_DynAnimGraphNode, postIdleColumn,		pGraphNode, bitFieldIndex, true);
		TokenSetSpecified(parse_DynAnimGraphNode, inheritBitsColumn,	pGraphNode, bitFieldIndex, true);
		TokenSetSpecified(parse_DynAnimGraphNode, boneVisSetColumn,		pGraphNode, bitFieldIndex, true);
		TokenSetSpecified(parse_DynAnimGraphNode, invalidForPlaybackColumn,	pGraphNode, bitFieldIndex, true);

		//set the copy from defaults tokens
		TokenSetSpecified(parse_DynAnimGraphNode, interpolationColumn,				pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritInterpolation_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, easeInColumn,						pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritEaseIn_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, easeOutColumn,					pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritEaseOut_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, noSelfInterpColumn,				pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritNoSelfInterp_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, forceEndFreezeColumn,				pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritForceEndFreeze_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, overrideAllBonesColumn,			pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritOverrideAllBones_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, snapOverrideAllBonesColumn,		pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritOverrideAllBones_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, overrideMovementColumn,			pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritOverrideMovement_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, snapOverrideMovementColumn,		pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritOverrideMovement_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, overrideDefaultMoveColumn,		pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritOverrideDefaultMove_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, snapOverrideDefaultMoveColumn,	pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritOverrideDefaultMove_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, allowRandomMoveRepeatsColumn,		pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritAllowRandomMoveRepeats_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, disableTorsoPointingColumn,		pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritDisableTorsoPointing_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, disableGroundRegColumn,			pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritDisableGroundReg_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, disableUpperBodyGroundRegColumn,	pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritDisableUpperBodyGroundReg_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, fxEventColumn,					pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritFxEvent_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, graphImpactColumn,				pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritGraphImpact_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, pathColumn,						pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritPath_WhenFalse);
		TokenSetSpecified(parse_DynAnimGraphNode, switchColumn,						pGraphNode, bitFieldIndex, pGraphNodeInheritBits->bInheritSwitch_WhenFalse);

		StructApplyDefaults(parse_DynAnimGraphNode, pGraphNode, pTemplateGraphNode, 0, 0, false);
	}
}

static void dynAnimGraphApplyPropertyInheritBits(DynAnimGraph *pGraph)
{
	if (!pGraph->bPartialGraph && pGraph->pInheritBits)
	{
		DynAnimTemplate *pTemplate = GET_REF(pGraph->hTemplate);
		if (pTemplate)
		{
			DynAnimGraph* pTemplateDefaultsGraph = pTemplate->pDefaultsGraph;
			DynAnimGraphInheritBits* pGraphInheritBits = pGraph->pInheritBits;
			int bitFieldIndex = ParserGetUsedBitFieldIndex(parse_DynAnimGraph);

			//not copy from defaults when created
			int nameColumn, filenameColumn, commentsColumn, scopeColumn, partialGraphColumn;
			int templateColumn;
			int nodesColumn;
			int powerMovementInfoColumn;
			int inheritBitsColumn;
			int reversedInheritBitPolarityColumn;
		
			//copy from defaults when created
			int timeoutColumn;
			int overrideAllBonesColumn, snapOverrideAllBonesColumn;
			int overrideMovementColumn, snapOverrideMovementColumn;
			int overrideDefaultMoveColumn, snapOverrideDefaultMoveColumn;
			int forceVisibleColumn;
			int pitchToTargetColumn;
			int disableTorsoPointingColumn, disableTorsoPointingRateColumn;
			int disableTorsoPointingTimeoutColumn;
			int disableGroundRegTimeoutColumn;
			int disableUpperBodyGroundRegTimeoutColumn;
			int onEnterFxEventColumn;
			int onExitFxEventColumn;
			int suppressColumn;
			int stanceColumn;
			int generatePowerMovementInfoColumn;
			int resetWhenMovementStopsColumn;

			//not copy from defaults
			ParserFindColumn(parse_DynAnimGraph, "Name",						&nameColumn);
			ParserFindColumn(parse_DynAnimGraph, "Filename",					&filenameColumn);
			ParserFindColumn(parse_DynAnimGraph, "Comments",					&commentsColumn);
			ParserFindColumn(parse_DynAnimGraph, "Scope",						&scopeColumn);
			ParserFindColumn(parse_DynAnimGraph, "PartialGraph",				&partialGraphColumn);
			ParserFindColumn(parse_DynAnimGraph, "Template",					&templateColumn);
			ParserFindColumn(parse_DynAnimGraph, "Nodes",						&nodesColumn);
			ParserFindColumn(parse_DynAnimGraph, "InheritBits",					&inheritBitsColumn);
			ParserFindColumn(parse_DynAnimGraph, "PowerMovementInfo",			&powerMovementInfoColumn);
			ParserFindColumn(parse_DynAnimGraph, "ReversedInheritBitPolarity",	&reversedInheritBitPolarityColumn);

			//copy from defaults
			ParserFindColumn(parse_DynAnimGraph, "Timeout",							&timeoutColumn);
			ParserFindColumn(parse_DynAnimGraph, "OverrideAllBones",				&overrideAllBonesColumn);
			ParserFindColumn(parse_DynAnimGraph, "SnapOverrideAllBones",			&snapOverrideAllBonesColumn);
			ParserFindColumn(parse_DynAnimGraph, "OverrideMovement",				&overrideMovementColumn);
			ParserFindColumn(parse_DynAnimGraph, "SnapOverrideMovement",			&snapOverrideMovementColumn);
			ParserFindColumn(parse_DynAnimGraph, "OverrideDefaultMove",				&overrideDefaultMoveColumn);
			ParserFindColumn(parse_DynAnimGraph, "SnapOverrideDefaultMove",			&snapOverrideDefaultMoveColumn);
			ParserFindColumn(parse_DynAnimGraph, "ForceVisible",					&forceVisibleColumn);
			ParserFindColumn(parse_DynAnimGraph, "PitchToTarget",					&pitchToTargetColumn);
			ParserFindColumn(parse_DynAnimGraph, "DisableTorsoPointing",			&disableTorsoPointingColumn);
			ParserFindColumn(parse_DynAnimGraph, "DisableTorsoPointingRate",		&disableTorsoPointingRateColumn);
			ParserFindColumn(parse_DynAnimGraph, "DisableTorsoPointingTimeout",		&disableTorsoPointingTimeoutColumn);
			ParserFindColumn(parse_DynAnimGraph, "DisableGroundRegTimeout",			&disableGroundRegTimeoutColumn);
			ParserFindColumn(parse_DynAnimGraph, "DisableUpperBodyGroundRegTimeout",&disableUpperBodyGroundRegTimeoutColumn);
			ParserFindColumn(parse_DynAnimGraph, "OnEnterFXEvent",					&onEnterFxEventColumn);
			ParserFindColumn(parse_DynAnimGraph, "OnExitFxEvent",					&onExitFxEventColumn);
			ParserFindColumn(parse_DynAnimGraph, "Suppress",						&suppressColumn);
			ParserFindColumn(parse_DynAnimGraph, "Stance",							&stanceColumn);
			ParserFindColumn(parse_DynAnimGraph, "GeneratePowerMovementInfo",		&generatePowerMovementInfoColumn);
			ParserFindColumn(parse_DynAnimGraph, "ResetWhenMovementStops",			&resetWhenMovementStopsColumn);

			//set the not copy from defaults tokens
			TokenSetSpecified(parse_DynAnimGraph, nameColumn,						pGraph, bitFieldIndex, true);
			TokenSetSpecified(parse_DynAnimGraph, filenameColumn,					pGraph, bitFieldIndex, true);
			TokenSetSpecified(parse_DynAnimGraph, commentsColumn,					pGraph, bitFieldIndex, true);
			TokenSetSpecified(parse_DynAnimGraph, scopeColumn,						pGraph, bitFieldIndex, true);
			TokenSetSpecified(parse_DynAnimGraph, partialGraphColumn,				pGraph, bitFieldIndex, true);
			TokenSetSpecified(parse_DynAnimGraph, templateColumn,					pGraph, bitFieldIndex, true);
			TokenSetSpecified(parse_DynAnimGraph, nodesColumn,						pGraph, bitFieldIndex, true);
			TokenSetSpecified(parse_DynAnimGraph, inheritBitsColumn,				pGraph, bitFieldIndex, true);
			TokenSetSpecified(parse_DynAnimGraph, powerMovementInfoColumn,			pGraph, bitFieldIndex, true);
			TokenSetSpecified(parse_DynAnimGraph, reversedInheritBitPolarityColumn, pGraph, bitFieldIndex, true);

			//set the copy from defaults tokens
			TokenSetSpecified(parse_DynAnimGraph, timeoutColumn,							pGraph, bitFieldIndex, pGraphInheritBits->bInheritTimeout_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, overrideAllBonesColumn,					pGraph, bitFieldIndex, pGraphInheritBits->bInheritOverrideAllBones_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, snapOverrideAllBonesColumn,				pGraph, bitFieldIndex, pGraphInheritBits->bInheritOverrideAllBones_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, overrideMovementColumn,					pGraph, bitFieldIndex, pGraphInheritBits->bInheritOverrideMovement_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, snapOverrideMovementColumn,				pGraph, bitFieldIndex, pGraphInheritBits->bInheritOverrideMovement_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, overrideDefaultMoveColumn,				pGraph, bitFieldIndex, pGraphInheritBits->bInheritOverrideDefaultMove_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, snapOverrideDefaultMoveColumn,			pGraph, bitFieldIndex, pGraphInheritBits->bInheritOverrideDefaultMove_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, forceVisibleColumn,						pGraph, bitFieldIndex, pGraphInheritBits->bInheritForceVisible_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, pitchToTargetColumn,						pGraph, bitFieldIndex, pGraphInheritBits->bInheritPitchToTarget_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, disableTorsoPointingColumn,				pGraph, bitFieldIndex, pGraphInheritBits->bInheritDisableTorsoPointing_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, disableTorsoPointingRateColumn,			pGraph, bitFieldIndex, pGraphInheritBits->bInheritDisableTorsoPointing_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, disableTorsoPointingTimeoutColumn,		pGraph, bitFieldIndex, pGraphInheritBits->bInheritDisableTorsoPointing_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, disableGroundRegTimeoutColumn,			pGraph, bitFieldIndex, pGraphInheritBits->bInheritDisableGroundReg_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, disableUpperBodyGroundRegTimeoutColumn,	pGraph, bitFieldIndex, pGraphInheritBits->bInheritDisableUpperBodyGroundReg_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, onEnterFxEventColumn,						pGraph, bitFieldIndex, pGraphInheritBits->bInheritOnEnterFxEvent_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, onExitFxEventColumn,						pGraph, bitFieldIndex, pGraphInheritBits->bInheritOnExitFxEvent_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, suppressColumn,							pGraph, bitFieldIndex, pGraphInheritBits->bInheritSuppress_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, stanceColumn,								pGraph, bitFieldIndex, pGraphInheritBits->bInheritStance_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, generatePowerMovementInfoColumn,			pGraph, bitFieldIndex, pGraphInheritBits->bInheritGeneratePowerMovementInfo_WhenFalse);
			TokenSetSpecified(parse_DynAnimGraph, resetWhenMovementStopsColumn,				pGraph, bitFieldIndex, pGraphInheritBits->bInheritResetWhenMovementStops_WhenFalse);

			//copy the values over
			StructApplyDefaults(parse_DynAnimGraph, pGraph, pTemplateDefaultsGraph, 0, 0, false);
		}
	}
}

void dynAnimGraphApplyInheritBits(DynAnimGraph *pGraph)
{
	if (!pGraph->bPartialGraph)
	{
		dynAnimGraphApplyPropertyInheritBits(pGraph);
		FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pGraphNode)
		{
			dynAnimGraphApplyGraphNodeInheritBits(pGraph, pGraphNode);
		}
		FOR_EACH_END;
	}
}

void dynAnimGraphDefaultsFixup(DynAnimGraph *pGraph)
{
	if (!pGraph->bPartialGraph)
	{
		DynAnimTemplate* pTemplate = GET_REF(pGraph->hTemplate);

		FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pNode)
		{
			if (pTemplate)
				pNode->pTemplateNode = dynAnimTemplateNodeFromGraphNode(pGraph, pNode);
			else
				pNode->pTemplateNode = NULL;
		}
		FOR_EACH_END;

		if (pTemplate)
		{
			DynAnimGraphNode **eaNodes = NULL;
			eaCreate(&eaNodes);
			FOR_EACH_IN_EARRAY_FORWARDS(pTemplate->eaNodes, DynAnimTemplateNode, pTemplateNode)
			{
				DynAnimGraphNode *pGraphNode = dynAnimGraphNodeFromTemplateNode(pGraph, pTemplateNode);
				if (pGraphNode)
				{
					int i = eaFind(&pGraph->eaNodes, pGraphNode);
					eaRemove(&pGraph->eaNodes, i);
					eaPush(&eaNodes, pGraphNode);
				}
				else
				{
					AnimFileError(pGraph->pcFilename, "Node Structure Fixup: Auto-adding node %s to graph %s (in-game graph editor save still required)!", pTemplateNode->pcName, pGraph->pcName);
					pGraphNode = StructCreate(parse_DynAnimGraphNode);
					pGraphNode->pcName = pTemplateNode->pcName;
					pGraphNode->fX = pTemplateNode->fX;
					pGraphNode->fY = pTemplateNode->fY;
					pGraphNode->pTemplateNode = pTemplateNode;
					eaPush(&eaNodes, pGraphNode);

					AnimFileError(pGraph->pcFilename, "Node Structure Fixup: Auto-inheriting for new node %s on graph %s (in-game graph editor save still required)!", pTemplateNode->pcName, pGraph->pcName);
					dynAnimGraphSetupGraphNodeInherits(pGraph, pGraphNode, true);
				}
			}
			FOR_EACH_END;
			FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pGraphNode)
			{
				AnimFileError(pGraph->pcFilename, "Node Structure Fixup: Auto-removing node %s from graph %s (in-game graph editor save still required)!", pGraphNode->pcName, pGraph->pcName);
			}
			FOR_EACH_END;
			eaDestroy(&pGraph->eaNodes);
			pGraph->eaNodes = eaNodes;
		}

		dynAnimGraphSetupMissingInheritBits(pGraph);
		dynAnimGraphFixupInheritBits(pGraph);
		dynAnimGraphApplyInheritBits(pGraph);

		if (pGraph->bDisableTorsoPointing &&
			pGraph->fDisableTorsoPointingTimeout <= 0.0f && 
			pGraph->fDisableTorsoPointingRate > 0.0f)
		{
			pGraph->fDisableTorsoPointingTimeout = 1.0f/pGraph->fDisableTorsoPointingRate;
		}

		if (pTemplate)
		{
			FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pGraphNode)
			{
				DynAnimTemplateNode *pTemplateNode = pGraphNode->pTemplateNode;
				if (eaSize(&pGraphNode->eaPath) != eaSize(&pTemplateNode->eaPath))
				{
					AnimFileError(pGraph->pcFilename, "Node Structure Fixup: Auto-reverting node %s from graph %s to inheritance (in-game graph editor save still required)!", pGraphNode->pcName, pGraph->pcName);
					pGraphNode->pInheritBits->bInheritPath_WhenFalse = false;
					dynAnimGraphApplyGraphNodeInheritBits(pGraph, pGraphNode);
				}
			}
			FOR_EACH_END;
		}

		pGraph->bNeedsReloadRefresh = true;
	}
}

void dynAnimGraphNodeNameFixup(DynAnimGraph *pGraph, DynAnimGraphNode *pGraphNode, const char *oldName, const char *newName)
{
	AnimFileError(pGraph->pcFilename, "Changing node name %s to %s in graph %s (in-game graph editor save still required)!", oldName, newName, pGraph->pcName);
	pGraphNode->pcName = newName;
}

static void dynAnimGraphPowerMovementFixupError(SA_PARAM_NN_VALID DynAnimGraph* pGraph, PowerMovementInfoGenerationResult eResult)
{
	devassert(eResult != PowerMovementInfoGenerationResult_Success);

	switch (eResult)
	{
	case PowerMovementInfoGenerationResult_Failure_MoreThanOneMovePerNode:
		ErrorFilenamef(pGraph->pcFilename, 
			"Power movement info generation failed for animation graph '%s' because the graph has nodes with multiple moves.", pGraph->pcName);
		break;
	case PowerMovementInfoGenerationResult_Failure_MoreThanSequencePerMove:
		ErrorFilenamef(pGraph->pcFilename, 
			"Power movement info generation failed for animation graph '%s' because the graph references move(s) with multiple sequences.", pGraph->pcName);
		break;
	case PowerMovementInfoGenerationResult_Failure_TooManyCyclicNodeReferences:
		ErrorFilenamef(pGraph->pcFilename, 
			"Power movement info generation failed for animation graph '%s' because the graph has too many cyclic references.", pGraph->pcName);
		break;
	case PowerMovementInfoGenerationResult_Failure_MaxIterationsReached:
		ErrorFilenamef(pGraph->pcFilename, 
			"Power movement info generation failed for animation graph '%s' because maximum number of iterations has been reached and the graph did not exit."
			"Please make sure the graph is acyclic.", pGraph->pcName);
		break;
	case PowerMovementInfoGenerationResult_Failure_PreActivateCyclicReferenceComesAfterActivateCyclicReference:
		ErrorFilenamef(pGraph->pcFilename, 
			"Power movement info generation failed for animation graph '%s' because the preactivation cyclic reference comes after the activation cyclic reference.", pGraph->pcName);
		break;
	case PowerMovementInfoGenerationResult_Failure_SelfReferencingActivateCycle:
		ErrorFilenamef(pGraph->pcFilename, 
			"Power movement info generation failed for animation graph '%s' because the graph has a self referencing activate cycle.", pGraph->pcName);
		break;
	case PowerMovementInfoGenerationResult_Failure_SelfReferencingPreActivateCycle:
		ErrorFilenamef(pGraph->pcFilename, 
			"Power movement info generation failed for animation graph '%s' because the graph has a self referencing preactivate cycle.", pGraph->pcName);
		break;
	case PowerMovementInfoGenerationResult_Failure_MultipleActivateCycles:
		ErrorFilenamef(pGraph->pcFilename, 
			"Power movement info generation failed for animation graph '%s' because the graph has multiple activate cycles.", pGraph->pcName);
		break;
	case PowerMovementInfoGenerationResult_Failure_MultiplePreActivateCycles:
		ErrorFilenamef(pGraph->pcFilename, 
			"Power movement info generation failed for animation graph '%s' because the graph has multiple preactivate cycles.", pGraph->pcName);
		break;
	case PowerMovementInfoGenerationResult_Failure_AnimationTrackFailedToLoad:
		ErrorFilenamef(pGraph->pcFilename, 
			"Power movement info generation failed for animation graph '%s' because one of the animation tracks could not be loaded.", pGraph->pcName);
		break;
	case PowerMovementInfoGenerationResult_Failure_AnimationTrackNotUncompressed:
		ErrorFilenamef(pGraph->pcFilename, 
			"Power movement info generation failed for animation graph '%s' because one of the animation tracks is not uncompressed.", pGraph->pcName);
		break;
	case PowerMovementInfoGenerationResult_Failure_MovementBoneInSnapshotButNotInTrack:
		ErrorFilenamef(pGraph->pcFilename, 
			"Power movement info generation failed for animation graph '%s' because the movement bone was in an animation track snapshot "
			"but could not be found in the actual track data.", pGraph->pcName);
		break;
	default:
		ErrorFilenamef(pGraph->pcFilename, 
			"Power movement info generation failed for animation graph '%s'. Unknown error.", pGraph->pcName);
	}
}

static int DynPosKeyFrameCmp(const void * pKey, const DynPosKeyFrame *frame)
{
	return (S32)pKey - (S32)frame->uiFrame;
}

static PowerMovementInfoGenerationResult dynAnimGraphPowerMovementFixupProcessNode(	SA_PARAM_NN_VALID DynAnimGraph *pGraph,
																					SA_PARAM_NN_VALID DynAnimGraphNode *pGraphNode,
																					bool bGenerateHitPausePath,
																					SA_PARAM_NN_VALID bool *pbMovementBoneFound,
																					SA_PARAM_NN_VALID bool *pbHasNodeWithMultipleMoves,
																					SA_PARAM_NN_VALID bool *pbHasMoveWithMultipleSequences,
																					SA_PARAM_NN_VALID S32 *puiCurrentFrame,
																					SA_PARAM_NN_VALID Vec3 vFirstFrameOffset)
{
	S32 iMoveCountPerNode = 0;
	S32 iSeqCountPerMove = 0;

	FOR_EACH_IN_EARRAY_FORWARDS(pGraphNode->eaMove, DynAnimGraphMove, pGraphMove)
	{
		DynMove *pDynMove = GET_REF(pGraphMove->hMove);

		++iMoveCountPerNode;

		if (iMoveCountPerNode > 1)
		{
			*pbHasNodeWithMultipleMoves = true;
		}

		if (*pbMovementBoneFound && *pbHasNodeWithMultipleMoves)
		{
			// We don't support multiple moves per node for power movement
			return PowerMovementInfoGenerationResult_Failure_MoreThanOneMovePerNode;
		}

		if (pDynMove)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(pDynMove->eaDynMoveSeqs, DynMoveSeq, pDynMoveSeq)
			{
				U32 uiFirstFrame = pDynMoveSeq->dynMoveAnimTrack.uiFirstFrame;
				U32 uiLastFrame = pDynMoveSeq->dynMoveAnimTrack.uiLastFrame;

				++iSeqCountPerMove;

				if (iSeqCountPerMove > 1)
				{
					*pbHasMoveWithMultipleSequences = true;
				}

				if (*pbMovementBoneFound && *pbHasMoveWithMultipleSequences)
				{
					// We don't support multiple sequences per move for power movement
					return PowerMovementInfoGenerationResult_Failure_MoreThanSequencePerMove;
				}

				// See if there is a power movement bone 
				if (eaIndexedFindUsingString(&pDynMoveSeq->dynMoveAnimTrack.frameSnapshot.eaBones, POWER_MOVEMENT_BONE_NAME) >= 0)
				{
					DynAnimTrack *pAnimTrack;
					const DynBoneTrack* pBoneTrack;

					*pbMovementBoneFound = true;					

					// Force loading the track if not already loaded
					pDynMoveSeq->dynMoveAnimTrack.pAnimTrackHeader = dynAnimTrackHeaderFind(pDynMoveSeq->dynMoveAnimTrack.pcAnimTrackName);
					if (!pDynMoveSeq->dynMoveAnimTrack.pAnimTrackHeader->bLoaded)
					{
						dynAnimTrackHeaderForceLoadTrack(pDynMoveSeq->dynMoveAnimTrack.pAnimTrackHeader);
					}

					if (!pDynMoveSeq->dynMoveAnimTrack.pAnimTrackHeader->bLoaded)
					{
						return PowerMovementInfoGenerationResult_Failure_AnimationTrackFailedToLoad;
					}

					pAnimTrack = pDynMoveSeq->dynMoveAnimTrack.pAnimTrackHeader->pAnimTrack;
					assert(pAnimTrack);

					if (pAnimTrack->eType != eDynAnimTrackType_Uncompressed)
					{
						return PowerMovementInfoGenerationResult_Failure_AnimationTrackNotUncompressed;
					}

					if (!stashFindPointerConst(pAnimTrack->boneTable, POWER_MOVEMENT_BONE_NAME, &pBoneTrack))
					{
						return PowerMovementInfoGenerationResult_Failure_MovementBoneInSnapshotButNotInTrack;
					}

					// Copy the frame data
					if (pBoneTrack->uiPosKeyCount > 0)
					{
						S32 iIndexStart = (S32)bfind((void *)uiFirstFrame,
							pBoneTrack->posKeyFrames,
							pBoneTrack->uiPosKeyCount,
							sizeof(DynPosKeyFrame),
							DynPosKeyFrameCmp);

						if (iIndexStart < ((S32)pBoneTrack->uiPosKeyCount - 1) || 
							(iIndexStart == ((S32)pBoneTrack->uiPosKeyCount - 1) && 
							pBoneTrack->posKeyFrames[iIndexStart].uiFrame == uiFirstFrame))
						{
							S32 iIndexEnd = (S32)bfind((void *)uiLastFrame,
								pBoneTrack->posKeyFrames,
								pBoneTrack->uiPosKeyCount,
								sizeof(DynPosKeyFrame),
								DynPosKeyFrameCmp);

							// Sanity check
							if (iIndexEnd >= iIndexStart)
							{
								DynPowerMovement **ppPowerMovement;
								S32 itFrame;

								if (iIndexEnd >= (S32)pBoneTrack->uiPosKeyCount ||
									uiLastFrame < pBoneTrack->posKeyFrames[iIndexEnd].uiFrame)
								{
									--iIndexEnd;
								}

								if (pGraph->pPowerMovementInfo == NULL)
								{
									pGraph->pPowerMovementInfo = StructCreate(parse_DynPowerMovementInfo);
								}

								ppPowerMovement = bGenerateHitPausePath ? &pGraph->pPowerMovementInfo->pHitPauseMovement
									: &pGraph->pPowerMovementInfo->pDefaultMovement;

								if (*ppPowerMovement == NULL)
								{
									*ppPowerMovement = StructCreate(parse_DynPowerMovement);
									eaIndexedEnable(&(*ppPowerMovement)->eaFrameList, parse_DynPowerMovementFrame);
								}

								for (itFrame = iIndexStart; itFrame <= iIndexEnd; itFrame++)
								{
									S32 iFrame = (*puiCurrentFrame) + ((S32)pBoneTrack->posKeyFrames[itFrame].uiFrame - (S32)uiFirstFrame)/pDynMoveSeq->fSpeed;
									if (eaIndexedFindUsingInt(&(*ppPowerMovement)->eaFrameList, iFrame) < 0)
									{
										DynPowerMovementFrame *pFrame = StructCreate(parse_DynPowerMovementFrame);

										if (eaSize(&(*ppPowerMovement)->eaFrameList) == 0)
										{
											copyVec3(pBoneTrack->posKeys[itFrame].vPos, vFirstFrameOffset);
										}

										pFrame->iFrame = iFrame;
										copyVec3(pBoneTrack->posKeys[itFrame].vPos, pFrame->vPos);
										eaIndexedAdd(&(*ppPowerMovement)->eaFrameList, pFrame);

										// Apply the first frame offset
										subVec3(pFrame->vPos, vFirstFrameOffset, pFrame->vPos);
									}
								}
							}
						}
					}
				}
				else
				{
					verbose_printf("Warning: Failed to find 'Movement' bone in Graph %s, DynMove %s, DynMoveSeq %s\n", pGraph->pcName, pDynMove->pcName, pDynMoveSeq->pcDynMoveSeq);
				}

				// Update the current frame information
				(*puiCurrentFrame) += (uiLastFrame - uiFirstFrame + 1)/pDynMoveSeq->fSpeed;
			}
			FOR_EACH_END
		}
	}
	FOR_EACH_END

	return PowerMovementInfoGenerationResult_Success;
}

static void dynAnimGraphPowerMovementDiscardMovementData(SA_PARAM_NN_VALID DynAnimGraph* pGraph, bool bGenerateHitPausePath, 
	SA_PARAM_NN_VALID U32 *puiCurrentFrame)
{
	// Discard any power movement information
	// we've generated this far. We cannot guess
	// how long the cycles will take.
	*puiCurrentFrame = 0;

	if (pGraph->pPowerMovementInfo)
	{
		if (bGenerateHitPausePath)
		{
			StructDestroySafe(parse_DynPowerMovement, &pGraph->pPowerMovementInfo->pHitPauseMovement);
		}
		else
		{
			StructDestroySafe(parse_DynPowerMovement, &pGraph->pPowerMovementInfo->pDefaultMovement);
		}

		if (pGraph->pPowerMovementInfo->pDefaultMovement == NULL && pGraph->pPowerMovementInfo->pHitPauseMovement == NULL)
		{
			StructDestroySafe(parse_DynPowerMovementInfo, &pGraph->pPowerMovementInfo);
		}
	}
}

// Traverse the graph and generates either the default movement or 
// the HitPause movement information
static PowerMovementInfoGenerationResult dynAnimGraphPowerMovementTraversal(SA_PARAM_NN_VALID DynAnimGraph* pGraph, 
	SA_PARAM_NN_VALID DynAnimTemplate * pAnimTemplate, bool bGenerateHitPausePath)
{
	// Movement info generation result
	PowerMovementInfoGenerationResult eResult = PowerMovementInfoGenerationResult_Success;

	// Nodes used for traversal
	DynAnimTemplateNode *pCurrentTemplateNode = NULL;
	DynAnimGraphNode *pCurrentGraphNode = NULL;

	bool bHasNodeWithMultipleMoves = false;
	bool bHasMoveWithMultipleSequences = false;
	bool bHasCyclicNodeForPreActivate = false;
	bool bHasCyclicNodeForActivate = false;
	bool bMovementBoneFound = false;
	bool bUsedHitPausePath = false;

	// Current frame number
	S32 iCurrentFrame = 0;

	// First node is always the start node and second node is always the end node
	// Start with the first node
	S32 iCurrentNode = 0;
	S32 iItCount = 0;
	S32 iCandidateNode;

	Vec3 vFirstFrameOffset;
	zeroVec3(vFirstFrameOffset);

	while (iCurrentNode >= 0 && 
		iCurrentNode < eaSize(&pAnimTemplate->eaNodes) && 
		iCurrentNode != 1) // 1 is the index of the end node
	{
		++iItCount;
		pCurrentTemplateNode = pAnimTemplate->eaNodes[iCurrentNode];
		pCurrentGraphNode = pGraph->eaNodes[iCurrentNode];

		if (iItCount > POWER_MOVEMENT_GENERATION_MAX_ITERATIONS)
		{
			eResult = PowerMovementInfoGenerationResult_Failure_MaxIterationsReached;
			break;
		}

		if (pCurrentGraphNode->pTemplateNode && pCurrentGraphNode->pTemplateNode->bInterruptible)
		{	// we reached an interruptible node, stop getting movement frames
			break;	
		}

		if (iCurrentNode > 1 &&
			(eResult = dynAnimGraphPowerMovementFixupProcessNode(pGraph, pCurrentGraphNode, bGenerateHitPausePath,
			&bMovementBoneFound, &bHasNodeWithMultipleMoves, &bHasMoveWithMultipleSequences, &iCurrentFrame, vFirstFrameOffset)) 
			!= PowerMovementInfoGenerationResult_Success)
		{
			break;
		}

		iCandidateNode = pCurrentTemplateNode->defaultNext.index;
		if (bGenerateHitPausePath)
		{
			// Prefer to use the HitPause paths
			FOR_EACH_IN_EARRAY_FORWARDS(pCurrentTemplateNode->eaSwitch, DynAnimTemplateSwitch, pSwitch)
			{
				if (pSwitch->pcFlag == allocFindString("HitPause"))
				{
					iCandidateNode = pSwitch->next.index;
					bUsedHitPausePath = true;
					break;
				}
			}
			FOR_EACH_END
		}

		// See if the current node references itself
		if (iCurrentNode == iCandidateNode)
		{
			if (bHasCyclicNodeForPreActivate && bHasCyclicNodeForActivate)
			{
				// No more cyclic references are allowed
				eResult = PowerMovementInfoGenerationResult_Failure_TooManyCyclicNodeReferences;
				break;
			}

			eResult = PowerMovementInfoGenerationResult_Failure_TooManyCyclicNodeReferences;

			FOR_EACH_IN_EARRAY_FORWARDS(pCurrentTemplateNode->eaSwitch, DynAnimTemplateSwitch, pSwitch)
			{
				if (pSwitch->pcFlag == allocFindString("PreActivate"))
				{
					if (pSwitch->next.index != iCurrentNode)
					{
						if (!bHasCyclicNodeForActivate && !bHasCyclicNodeForPreActivate)
						{
							bHasCyclicNodeForPreActivate = true;

							// Set the next node
							iCurrentNode = pSwitch->next.index;

							// Discard all movement data generated so far
							dynAnimGraphPowerMovementDiscardMovementData(pGraph, bGenerateHitPausePath, &iCurrentFrame);

							eResult = PowerMovementInfoGenerationResult_Success;
						}
						else if (bHasCyclicNodeForPreActivate)
						{
							eResult = PowerMovementInfoGenerationResult_Failure_MultiplePreActivateCycles;
						}
						else
						{
							eResult = PowerMovementInfoGenerationResult_Failure_PreActivateCyclicReferenceComesAfterActivateCyclicReference;
						}
					}
					else
					{
						eResult = PowerMovementInfoGenerationResult_Failure_SelfReferencingPreActivateCycle;
					}

					break;
				}
				else if (pSwitch->pcFlag == allocFindString("Activate"))
				{
					if (pSwitch->next.index != iCurrentNode)
					{
						if (!bHasCyclicNodeForActivate)
						{
							bHasCyclicNodeForActivate = true;

							// Set the next node
							iCurrentNode = pSwitch->next.index;

							// Discard all movement data generated so far
							dynAnimGraphPowerMovementDiscardMovementData(pGraph, bGenerateHitPausePath, &iCurrentFrame);

							eResult = PowerMovementInfoGenerationResult_Success;
						}
						else
						{
							eResult = PowerMovementInfoGenerationResult_Failure_MultipleActivateCycles;
						}
					}
					else
					{
						eResult = PowerMovementInfoGenerationResult_Failure_SelfReferencingActivateCycle;
					}

					break;
				}
			}
			FOR_EACH_END

			if (eResult != PowerMovementInfoGenerationResult_Success)
			{
				eResult = PowerMovementInfoGenerationResult_Failure_TooManyCyclicNodeReferences;
				break;
			}			
		}
		else
		{
			// Go to the next node in the graph
			iCurrentNode = iCandidateNode;
		}		
	}

 	if (eResult != PowerMovementInfoGenerationResult_Success ||
 		(bGenerateHitPausePath && !bUsedHitPausePath))
	{
		// Discard all movement data generated so far
		dynAnimGraphPowerMovementDiscardMovementData(pGraph, bGenerateHitPausePath, &iCurrentFrame);
	}

	if (pGraph->pPowerMovementInfo && bHasCyclicNodeForPreActivate)
	{
		if (bGenerateHitPausePath && pGraph->pPowerMovementInfo->pHitPauseMovement)
		{
			pGraph->pPowerMovementInfo->pHitPauseMovement->bSkipPreActivationPhase = true;
		}
		else if (!bGenerateHitPausePath && pGraph->pPowerMovementInfo->pDefaultMovement)
		{
			pGraph->pPowerMovementInfo->pDefaultMovement->bSkipPreActivationPhase = true;
		}
	}	

	return eResult;
}

// Fix up the power movement information in the graph
static void dynAnimGraphPowerMovementFixup(DynAnimGraph* pGraph)
{
	// The animation template
	DynAnimTemplate *pAnimTemplate;
	S32 iTemplateNodeCount;
	S32 iGraphNodeCount;

	PowerMovementInfoGenerationResult eResult;

	// Destroy the current power movement info
	StructDestroySafe(parse_DynPowerMovementInfo, &pGraph->pPowerMovementInfo);

	if (!pGraph->bGeneratePowerMovementInfo)
	{
		return;
	}

	// Get the template so we can traverse the graph
	pAnimTemplate = GET_REF(pGraph->hTemplate);

	if (pAnimTemplate == NULL)
	{
		ErrorFilenamef(pGraph->pcFilename, 
			"Power movement information could not be processed for animation graph '%s' " 
			"because the animation template is invalid: %s.",
			pGraph->pcName, REF_STRING_FROM_HANDLE(pGraph->hTemplate));
		return;
	}

	// Get the node counts
	iTemplateNodeCount = eaSize(&pAnimTemplate->eaNodes);
	iGraphNodeCount = eaSize(&pGraph->eaNodes);

	// Graph and template node counts must match and there must be at least 3 nodes.
	// 2 for starting nodes and at least 1 node that does something
	if (iTemplateNodeCount < 3 || iTemplateNodeCount != iGraphNodeCount)
	{
		// Not displaying an error out here because the anim graph system 
		// should handle this invalid scenario
		return;
	}
	
	// Default path traversal
	eResult = dynAnimGraphPowerMovementTraversal(pGraph, pAnimTemplate, false);

	if (eResult == PowerMovementInfoGenerationResult_Success)
	{
		// HitPause path traversal
		eResult = dynAnimGraphPowerMovementTraversal(pGraph, pAnimTemplate, true);
	}

	if (eResult != PowerMovementInfoGenerationResult_Success)
	{
		// Display a reasonable error
		dynAnimGraphPowerMovementFixupError(pGraph, eResult);
	}	
}

void dynAnimGraphFixupValidPlaybackPaths(DynAnimGraph* pGraph)
{
	if (!pGraph->bPartialGraph)
	{
		FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pNode)
		{
			if (pNode->pTemplateNode &&
				(	pNode->pTemplateNode->eType == eAnimTemplateNodeType_Normal ||
					pNode->pTemplateNode->eType == eAnimTemplateNodeType_Randomizer))
			{
				DynAnimGraphNode** eaSeenNodes = NULL;
				S32 iSawMoveOnPath = -1;

				pNode->bInvalidForPlayback =	!dynAnimGraphNodeOnValidPlaybackPath(pGraph, pNode, &eaSeenNodes, true, false) || // NOT ( all default switch paths from pNode either reach the end node OR hit at least 1 move before looping )
												(	dynAnimGraphNodeRequiresValidPlaybackPath(pGraph, pNode, pGraph->eaNodes[0], &eaSeenNodes, false, &iSawMoveOnPath, true) &&  // a path exist from the start node to pNode including any switches along the way
													iSawMoveOnPath != 1 && // there wasn't a path or there was some path without a move between the start node and pNode
													!dynAnimGraphNodeOnValidPlaybackPath(pGraph, pNode, &eaSeenNodes, false, false)); // NOT ( all default switch paths from pNode either hit at least 1 move before reaching the end node OR hit at least 1 move before looping )

				eaDestroy(&eaSeenNodes);
			}
			else
			{
				pNode->bInvalidForPlayback = false;
			}
		}
		FOR_EACH_END;
	}
}

void dynAnimGraphFixup(DynAnimGraph* pGraph)
{
	if (!pGraph->bPartialGraph)
	{
		DynAnimTemplate* pTemplate = GET_REF(pGraph->hTemplate);

		FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pNode)
		{
			DynAnimTemplateNode* pTemplateNode;

			if (pTemplate) {
				pNode->pTemplateNode = pTemplateNode = dynAnimTemplateNodeFromGraphNode(pGraph, pNode);
			} else {
				pNode->pTemplateNode = pTemplateNode = NULL;
			}

			//make sure that we've got switch data for older graphs that pre-date it
			if (pTemplateNode &&
				!eaSize(&pNode->eaSwitch))
			{
				FOR_EACH_IN_EARRAY_FORWARDS(pTemplateNode->eaSwitch, DynAnimTemplateSwitch, pTemplateSwitch)
				{
					DynAnimGraphSwitch* pGraphSwitch = StructCreate(parse_DynAnimGraphSwitch);
					pGraphSwitch->fRequiredPlaytime = 0;
					pGraphSwitch->bInterrupt = pTemplateSwitch->bInterrupt_Depreciated;
					eaPush(&pNode->eaSwitch, pGraphSwitch);
				}
				FOR_EACH_END;
			}

			//change from having the fx events on nodes to the moves within a node
			FOR_EACH_IN_EARRAY(pNode->eaFxEvent, DynAnimGraphFxEvent, pFxEvent)
			{
				eaRemoveFast(&pNode->eaFxEvent, ipFxEventIndex);
				FOR_EACH_IN_EARRAY(pNode->eaMove, DynAnimGraphMove, pGraphMove)
				{
					DynAnimGraphFxEvent *pFxEventDup = StructCreate(parse_DynAnimGraphFxEvent);
					StructCopyAll(parse_DynAnimGraphFxEvent, pFxEvent, pFxEventDup);
					eaPush(&pGraphMove->eaFxEvent, pFxEventDup);
				}
				FOR_EACH_END;
				StructDestroy(parse_DynAnimGraphFxEvent, pFxEvent);
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;

		pGraph->bNeedsReloadRefresh = true;
	}

	dynAnimGraphFixupValidPlaybackPaths(pGraph);

	FOR_EACH_IN_EARRAY(pGraph->eaStance, DynAnimGraphStance, pStance)
	{
		if(pStance->pcStance) {
			char stanceWord[64];
			strcpy(stanceWord, removeLeadingWhiteSpaces(pStance->pcStance));
			removeTrailingWhiteSpaces(stanceWord);
			pStance->pcStance = allocAddString(stanceWord);
		}
	}
	FOR_EACH_END;
}

static void dynAnimGraphRefDictCallback(enumResourceEventType eType, const char *pDictName, const char *pRefData, DynAnimGraph* pGraph, void *pUserData)
{
	if (eType == RESEVENT_RESOURCE_ADDED || eType == RESEVENT_RESOURCE_MODIFIED)
	{
		//dynAnimGraphFixup(pGraph);
	}
	if (bLoadedOnce)
		danimForceDataReload();
}



AUTO_FIXUPFUNC;
TextParserResult fixupDynAnimGraph(DynAnimGraph* pGraph, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_PRE_STRUCTCOPY:
		{
			;
		}

		xcase FIXUPTYPE_POST_STRUCTCOPY:
		{
			dynAnimGraphFixup(pGraph);
		}

		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			dynAnimGraphFixup(pGraph);
			dynAnimGraphBuildDirectionalData(pGraph);
			if (!dynAnimGraphVerify(pGraph, true))
				return PARSERESULT_ERROR;
		}
		xcase FIXUPTYPE_POST_BIN_READ:
		{
			dynAnimGraphFixup(pGraph);
		}
		xcase FIXUPTYPE_PRE_TEXT_WRITE:
		case FIXUPTYPE_PRE_BIN_WRITE:
		{
			dynAnimGraphFixup(pGraph);
			if (!dynAnimGraphVerify(pGraph, true))
				return PARSERESULT_ERROR;
		}
	}
	return PARSERESULT_SUCCESS;
}

bool dynAnimGraphIndividualMoveVerifySingleFx(DynAnimGraph *pGraph, DynAnimGraphNode *pNode, DynAnimGraphMove *pGraphMove, DynAnimGraphFxEvent *pFxEvent, bool bReport)
{
	bool bRet = true;

	if (pGraphMove && !pGraph->bPartialGraph)
	{
		DynMove *pMove = GET_REF(pGraphMove->hMove);
		if (pMove)
		{
			//checks for move to prevent crash here, throws error during move verify if one doesn't exist
			//if (eaSize(&pMove->eaDynMoveSeqs) > 0) {
			//	verbose_printf("Warning: Graph %s Node %s Move %s has more than one move sequence sharing the same set of FX\n", pGraph->pcName, pNode->pcName, pMove->pcName);
			//}
			FOR_EACH_IN_EARRAY(pMove->eaDynMoveSeqs, DynMoveSeq, pMoveSeq)
			{
				U32 frames = pMoveSeq->dynMoveAnimTrack.uiLastFrame - pMoveSeq->dynMoveAnimTrack.uiFirstFrame;
				if (pFxEvent->iFrame > (int)frames)
				{
					if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: FX %s is at frame %d which is past the end of the associated move %s", pGraph->pcName, pNode->pcName, pFxEvent->pcFx, pFxEvent->iFrame, pMove->pcName);
					bRet = false;
				}
			}
			FOR_EACH_END;
		}
	}

	if (!pFxEvent->bMessage)
	{
		if (!dynFxInfoExists(pFxEvent->pcFx))
		{
			if (bReport)
			{
				if (pNode)
					AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Unknown FX %s called", pGraph->pcName, pNode->pcName, pFxEvent->pcFx);
				else
					AnimFileError(pGraph->pcFilename, "Graph %s: Unknown FX %s called", pGraph->pcName, pFxEvent->pcFx);
			}
			bRet = false;
		}
	}

	if (pFxEvent->bMovementBlendTriggered) {
		if (pNode) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Movement Blend Trigger not allowed on node FX", pGraph->pcName, pNode->pcName);
			bRet = false;
		}
		if (pFxEvent->fMovementBlendTrigger < 0.999*ANIMGRAPH_MIN_MOVEBLENDTRIGGER) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, FX %s: Movement blend trigger lower than required min %f", pGraph->pcName, pFxEvent->pcFx, ANIMGRAPH_MIN_MOVEBLENDTRIGGER);
			bRet = false;
		}
		if (pFxEvent->fMovementBlendTrigger > ANIMGRAPH_MAX_MOVEBLENDTRIGGER) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, FX %s: Movement blend trigger greater than max limit %f", pGraph->pcName, pFxEvent->pcFx, ANIMGRAPH_MAX_MOVEBLENDTRIGGER);
			bRet = false;
		}
		if (bReport) {
			if (pFxEvent->bMessage)
				verbose_printf("Graph %s: Using movement blend triggered FX Message %s, make sure exit & enter triggers match as needed or you can end up in a bad state\n", pGraph->pcName, pFxEvent->pcFx);
			else
				verbose_printf("Graph %s: Using movement blend triggered FX %s, make sure exit & enter triggers match as needed or you can end up in a bad state\n", pGraph->pcName, pFxEvent->pcFx);
		}
	}

	return bRet;
}

static bool dynAnimGraphNodeAttachedToSpecialNode(	const DynAnimGraph *pGraph,
													const DynAnimGraphNode *pGraphNode,
													eAnimTemplateNodeType eType)
{
	DynAnimTemplate *pTemplate = GET_REF(pGraph->hTemplate);
	DynAnimTemplateNode *pTemplateNode = pGraphNode->pTemplateNode;
	bool bRet = false;

	devassert(	eType == eAnimTemplateNodeType_Start ||
				eType == eAnimTemplateNodeType_End);

	if (pTemplate &&
		pTemplateNode)
	{
		FOR_EACH_IN_EARRAY(pTemplate->eaNodes, DynAnimTemplateNode, pTestNode)
		{
			if (pTestNode->eType == eType)
			{
				if (eType == eAnimTemplateNodeType_Start)
				{
					bRet |= dynAnimTemplateNodesAttached(pTemplate, pTestNode, pTemplateNode, false);
				}
				else if (eType == eAnimTemplateNodeType_End)
				{
					bRet |= dynAnimTemplateNodesAttached(pTemplate, pTemplateNode, pTestNode, false);
				}
			}
		}
		FOR_EACH_END;
	}

	return bRet;
}

static bool dynAnimGraphFxEventOnLastFrameOfMove(	DynAnimGraph *pGraph,
													DynAnimGraphNode *pNode,
													DynAnimGraphMove *pMove,
													DynAnimGraphFxEvent *pFxEvent)
{
	if (pNode && !pGraph->bPartialGraph)
	{
		if (pMove)
		{
			DynMove *pActualMove = GET_REF(pMove->hMove);
			if (pActualMove) {
				//checks for move to prevent crash here, throws error during move verify if one doesn't exist
				FOR_EACH_IN_EARRAY(pActualMove->eaDynMoveSeqs, DynMoveSeq, pMoveSeq)
				{
					U32 frames = pMoveSeq->dynMoveAnimTrack.uiLastFrame - pMoveSeq->dynMoveAnimTrack.uiFirstFrame;
					if (pFxEvent->iFrame == (int)frames ||
						pFxEvent->iFrame == (int)(frames-1))
					{
						return true;
					}
				}
				FOR_EACH_END;
			}
		}
		else
		{
			FOR_EACH_IN_EARRAY(pNode->eaMove, DynAnimGraphMove, pAGMove)
			{
				DynMove *pActualMove = GET_REF(pAGMove->hMove);
				if (pActualMove)
				{
					//checks for move to prevent crash here, throws error during move verify if one doesn't exist
					FOR_EACH_IN_EARRAY(pActualMove->eaDynMoveSeqs, DynMoveSeq, pMoveSeq)
					{
						U32 frames = pMoveSeq->dynMoveAnimTrack.uiLastFrame - pMoveSeq->dynMoveAnimTrack.uiFirstFrame;
						if (pFxEvent->iFrame == (int)frames ||
							pFxEvent->iFrame == (int)(frames-1))
						{
							return true;
						}
					}
					FOR_EACH_END;
				}
			}
			FOR_EACH_END;
		}
	}
	return false;
}

bool dynAnimGraphFxEventVerifyUnique(	DynAnimGraph *pGraph,
										DynAnimGraphNode *pNode,
										DynAnimGraphMove *pMove,
										DynAnimGraphFxEvent *pFxEvent,
										DynAnimGraphFxEvent **eaCheckFxList,
										bool bForceFirstFrame,
										bool bForceLastFrame,
										bool bReport)
{
	bool bRet = true;
	FOR_EACH_IN_EARRAY(eaCheckFxList, DynAnimGraphFxEvent, pCheckFxEvent)
	{
		if (pFxEvent != pCheckFxEvent &&
			pFxEvent->pcFx == pCheckFxEvent->pcFx &&
			pFxEvent->bMessage == pCheckFxEvent->bMessage &&
			(	pCheckFxEvent->bMovementBlendTriggered == true	||
				!bForceFirstFrame && !bForceLastFrame && pFxEvent->iFrame == pCheckFxEvent->iFrame ||
				bForceFirstFrame && dynAnimGraphNodeAttachedToSpecialNode(pGraph, pNode, eAnimTemplateNodeType_Start) && pFxEvent->iFrame == 0 ||
				bForceLastFrame  && dynAnimGraphNodeAttachedToSpecialNode(pGraph, pNode, eAnimTemplateNodeType_End)   && dynAnimGraphFxEventOnLastFrameOfMove(pGraph, pNode, pMove, pFxEvent)))		
		{
			if (bReport) {
				if      (bForceFirstFrame)	AnimFileError(pGraph->pcFilename, "Graph %s, Node %s, FX %s is duplicate of an on-enter FX!", pGraph->pcName, pNode->pcName, pFxEvent->pcFx);
				else if (bForceLastFrame )	AnimFileError(pGraph->pcFilename, "Graph %s, Node %s, FX %s is duplicate of an on-exit  FX!", pGraph->pcName, pNode->pcName, pFxEvent->pcFx);
				else						AnimFileError(pGraph->pcFilename, "Graph %s, Node %s, FX %s is duplicated within the node!",  pGraph->pcName, pNode->pcName, pFxEvent->pcFx);
			}
			bRet = false;
		}
	}
	FOR_EACH_END;
	return bRet;
}

bool dynAnimGraphIndividualMoveVerifyFx(DynAnimGraph *pGraph, DynAnimGraphNode *pNode, DynAnimGraphMove *pMove, DynAnimGraphFxEvent *pFx, bool bReport)
{
	if (!dynAnimGraphIndividualMoveVerifySingleFx(pGraph, pNode, pMove, pFx, bReport)									||
		!dynAnimGraphFxEventVerifyUnique(pGraph, pNode, pMove, pFx, pMove->eaFxEvent,         false, false, bReport)	||
		!dynAnimGraphFxEventVerifyUnique(pGraph, pNode, pMove, pFx, pGraph->eaOnEnterFxEvent, true,  false, bReport)	||
		!dynAnimGraphFxEventVerifyUnique(pGraph, pNode, pMove, pFx, pGraph->eaOnExitFxEvent,  false, true,  bReport)	 )
	{
		return false;
	}
	return true;
}

bool dynAnimGraphIndividualMoveVerify(DynAnimGraph* pGraph, DynAnimGraphNode* pNode, DynAnimGraphMove* pMove, bool bReport)
{
	DynAnimTemplate* pTemplate = GET_REF(pGraph->hTemplate);
	bool bRet=true;

	if (!GET_REF(pMove->hMove))
	{
		if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Move %s could not be found!", pGraph->pcName, pNode->pcName, REF_HANDLE_GET_STRING(pMove->hMove));
		bRet = false;
	} else {
		if (SAFE_MEMBER(pTemplate,eType) != eAnimTemplateType_Movement)
		{
			DynMove *pDynMove = GET_REF(pMove->hMove);

			FOR_EACH_IN_EARRAY(pDynMove->eaDynMoveSeqs, DynMoveSeq, pMoveSeq)
			{
				if (pMoveSeq->fDistance) {
					verbose_printf("Graph %s, Node %s, Move %s: Has distance set, this causes graph nodes to freeze on no motion!\n", pGraph->pcName, pNode->pcName, pDynMove->pcName);
				}
			}
			FOR_EACH_END;

			if (pMove->eDirection != eDynMovementDirection_Default)
			{
				if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Move %s has an invalid movement direction (must be default for non-movement typed graphs)!", pGraph->pcName, pNode->pcName, pDynMove->pcName);
				bRet = false;
			}
		}
		else if (SAFE_MEMBER(pTemplate,eType) == eAnimTemplateType_Movement)
		{
			DynMove *pDynMove = GET_REF(pMove->hMove);

			if (pMove->eDirection < 0 ||
				DYNMOVEMENT_NUMDIRECTIONS < pMove->eDirection)
			{
				if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Move %s uses movement direction that's not part of the node's allowed 8 directional set", pGraph->pcName, pNode->pcName, pDynMove->pcName);
				bRet = false;
			}
		}
	}

	if (pMove->fChance <= 0.0f)
	{
		if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Move %s must have chance greater than zero!", pGraph->pcName, pNode->pcName, REF_HANDLE_GET_STRING(pMove->hMove));
		bRet = false;
	}
	if (pMove->fChance > 1.0f)
	{
		if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Move %s must have chance between 0 and 1.0!", pGraph->pcName, pNode->pcName, REF_HANDLE_GET_STRING(pMove->hMove));
		bRet = false;
	}
	FOR_EACH_IN_EARRAY(pMove->eaFxEvent, DynAnimGraphFxEvent, pFx) {
		bRet &= dynAnimGraphIndividualMoveVerifyFx(pGraph, pNode, pMove, pFx, bReport);
	} FOR_EACH_END;
	return bRet;
}

bool dynAnimGraphGroupMoveVerifyChance(DynAnimGraph *pGraph, DynAnimGraphNode *pNode, S32 iDirection, bool bReport)
{
	DynAnimTemplate* pTemplate = GET_REF(pGraph->hTemplate);
	F32 fTotalChance = 0.0f;
	bool bFoundOne = false;
	bool bRet = true;

	FOR_EACH_IN_EARRAY(pNode->eaMove, DynAnimGraphMove, pMove)
	{
		if (pMove->eDirection == iDirection) {
			fTotalChance += pMove->fChance;
			bFoundOne = true;
		}
	}
	FOR_EACH_END;

	if (bFoundOne &&
		fabsf(fTotalChance - 1.0f) > EPSILON)
	{
		if (bReport)
		{
			if (SAFE_MEMBER(pTemplate,eType) == eAnimTemplateType_Movement)
				AnimFileError(pGraph->pcFilename, "Graph %s, Node %s, Direction %s: Chances add up to %.2f, should add up to 1.0!", pGraph->pcName, pNode->pcName, StaticDefineIntRevLookup(DynMovementDirectionEnum,iDirection), fTotalChance);
			else
				AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Chances add up to %.2f, should add up to 1.0!", pGraph->pcName, pNode->pcName, fTotalChance);
		}
		bRet = false;
	}

	return bRet;
}

bool dynAnimGraphGroupMoveVerify(DynAnimGraph *pGraph, DynAnimGraphNode *pNode, bool bReport)
{
	S32 iDirection;
	bool bRet = true;

	FOR_EACH_IN_EARRAY(pNode->eaMove, DynAnimGraphMove, pMove)
	{
		bRet &= dynAnimGraphIndividualMoveVerify(pGraph, pNode, pMove, bReport);
	}
	FOR_EACH_END;

	for (iDirection = 0; iDirection < DYNMOVEMENT_NUMDIRECTIONS; iDirection++)
	{
		bRet &= dynAnimGraphGroupMoveVerifyChance(pGraph, pNode, iDirection, bReport);
	}

	return bRet;
}

bool dynAnimGraphIndividualPathVerify(DynAnimGraph *pGraph, DynAnimGraphNode *pNode, DynAnimGraphPath *pPath, bool bReport)
{
	bool bRet=true;
	if (pPath->fChance < 0.0f)
	{
		if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Every path must have a chance greater than or equal to zero!", pGraph->pcName, pNode->pcName);
		bRet = false;
	}
	if (pPath->fChance > 1.0f)
	{
		if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Every path must have a chance between 0 and 1.0!", pGraph->pcName, pNode->pcName);
		bRet = false;
	}
	return bRet;
}

bool dynAnimGraphGroupPathVerify(DynAnimGraph *pGraph, DynAnimGraphNode *pNode, bool bReport)
{
	F32 fSumPathChances = 0.0f;
	bool bRet = true;
	FOR_EACH_IN_EARRAY(pNode->eaPath, DynAnimGraphPath, pPath)
	{
		fSumPathChances += pPath->fChance;
		if (!dynAnimGraphIndividualPathVerify(pGraph, pNode, pPath, true))
			bRet = false;
	}
	FOR_EACH_END;
	if (fabsf(1.0f - fSumPathChances) > EPSILON)
	{
		if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Path chances to do not sum to 1.0", pGraph->pcName, pNode->pcName);
		bRet = false;
	}
	return bRet;
}

bool dynAnimGraphFxEventVerify(DynAnimGraph* pGraph, DynAnimGraphNode* pNode, DynAnimGraphFxEvent *pFxEvent, bool bReport)
{
	bool bRet=true;
	// verify time is not past the end of the possible animations
	if (pNode && !pGraph->bPartialGraph)
	{
		if (!eaSize(&pNode->eaMove))
		{
			if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Found FX on node with no moves", pGraph->pcName, pNode->pcName);
			bRet = false;
		}
		else
		{
			FOR_EACH_IN_EARRAY(pNode->eaMove, DynAnimGraphMove, pAGMove)
			{
				DynMove *pMove = GET_REF(pAGMove->hMove);
				if (pMove)
				{
					//checks for move to prevent crash here, throws error during move verify if one doesn't exist
					FOR_EACH_IN_EARRAY(pMove->eaDynMoveSeqs, DynMoveSeq, pMoveSeq)
					{
						U32 frames = pMoveSeq->dynMoveAnimTrack.uiLastFrame - pMoveSeq->dynMoveAnimTrack.uiFirstFrame;
						if (pFxEvent->iFrame > (int)frames)
						{
							if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: FX %s is at frame %d which is past the end of the associated move %s", pGraph->pcName, pNode->pcName, pFxEvent->pcFx, pFxEvent->iFrame, pMove->pcName);
							bRet = false;
						}
					}
					FOR_EACH_END;
				}
			}
			FOR_EACH_END;
		}
	}

	if (!pFxEvent->bMessage)
	{
		if (!dynFxInfoExists(pFxEvent->pcFx))
		{
			if (bReport)
			{
				if (pNode)
					AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Unknown FX %s called", pGraph->pcName, pNode->pcName, pFxEvent->pcFx);
				else
					AnimFileError(pGraph->pcFilename, "Graph %s: Unknown FX %s called", pGraph->pcName, pFxEvent->pcFx);
			}
			bRet = false;
		}
	}

	if (pFxEvent->bMovementBlendTriggered) {
		if (pNode) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Movement Blend Trigger not allowed on node FX", pGraph->pcName, pNode->pcName);
			bRet = false;
		}
		if (pFxEvent->fMovementBlendTrigger < 0.999*ANIMGRAPH_MIN_MOVEBLENDTRIGGER) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, FX %s: Movement blend trigger lower than required min %f", pGraph->pcName, pFxEvent->pcFx, ANIMGRAPH_MIN_MOVEBLENDTRIGGER);
			bRet = false;
		}
		if (pFxEvent->fMovementBlendTrigger > ANIMGRAPH_MAX_MOVEBLENDTRIGGER) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, FX %s: Movement blend trigger greater than max limit %f", pGraph->pcName, pFxEvent->pcFx, ANIMGRAPH_MAX_MOVEBLENDTRIGGER);
			bRet = false;
		}
		if (bReport) {
			if (pFxEvent->bMessage)
				verbose_printf("Graph %s: Using movement blend triggered FX Message %s, make sure exit & enter triggers match as needed or you can end up in a bad state\n", pGraph->pcName, pFxEvent->pcFx);
			else
				verbose_printf("Graph %s: Using movement blend triggered FX %s, make sure exit & enter triggers match as needed or you can end up in a bad state\n", pGraph->pcName, pFxEvent->pcFx);
		}
	}

	return bRet;
}

bool dynAnimGraphImpactVerify(DynAnimGraph *pGraph, DynAnimGraphNode *pNode, DynAnimGraphTriggerImpact *pImpact, bool bReport)
{
	bool bRet=true;
	// verify time is not past the end of the possible animations
	if (pNode && !pGraph->bPartialGraph)
	{
		if (!eaSize(&pNode->eaMove))
		{
			if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Found impact on node with no moves", pGraph->pcName, pNode->pcName);
			bRet = false;
		}
		else
		{
			FOR_EACH_IN_EARRAY(pNode->eaMove, DynAnimGraphMove, pAGMove)
			{
				DynMove *pMove = GET_REF(pAGMove->hMove);
				if (pMove)
				{
					//checks for move to prevent crash here, throws error during move verify if one doesn't exist
					FOR_EACH_IN_EARRAY(pMove->eaDynMoveSeqs, DynMoveSeq, pMoveSeq)
					{
						U32 frames = pMoveSeq->dynMoveAnimTrack.uiLastFrame - pMoveSeq->dynMoveAnimTrack.uiFirstFrame;
						if (pImpact->iFrame > (int)frames)
						{
							if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Impact is at frame %d which is past the end of the associated move %s", pGraph->pcName, pNode->pcName, pImpact->iFrame, pMove->pcName);
							bRet = false;
						}
					}
					FOR_EACH_END;
				}

			}
			FOR_EACH_END;
		}
	}

	if (!pImpact->pcBone || !strlen(pImpact->pcBone))
	{
		if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Impact does not specify a bone", pGraph->pcName, pNode->pcName);
	}

	return bRet;
}


bool dynAnimGraphNodeVerify(DynAnimGraph* pGraph, DynAnimGraphNode* pNode, bool bReport)
{
	bool bRet = true;
	DynAnimTemplate* pTemplate = GET_REF(pGraph->hTemplate);

	if (!pTemplate && !pGraph->bPartialGraph)
	{
		if (bReport) AnimFileError(pGraph->pcFilename, "Unable to find Template %s for Graph %s", REF_HANDLE_GET_STRING(pGraph->hTemplate), pGraph->pcName);
		bRet = false;
	}
	else if (!pGraph->bPartialGraph) // && pTemplate
	{
		DynAnimTemplateNode* pTemplateNode = dynAnimTemplateNodeFromGraphNode(pGraph, pNode);
		if (!pTemplateNode)
		{
			if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s does not have a matching Node in Template %s!", pGraph->pcName, pNode->pcName, pTemplate->pcName);
			bRet = false;
		}
		else
		{
			if (pTemplateNode != pNode->pTemplateNode)
			{
				if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s pTemplate pointer is not correct!", pGraph->pcName, pNode->pcName);
				bRet = false;
			}
		}
	}
	else //pGraph->bPartialGraph
	{
		if (REF_HANDLE_IS_ACTIVE(pNode->hPostIdle))
		{
			if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Template graphs not allowed to specify node-based post-idles", pGraph->pcName, pNode->pcName);
			bRet = false;
		}
	}

	if (SAFE_MEMBER(pNode->pTemplateNode, eType) == eAnimTemplateNodeType_Normal)
	{
		if (eaSize(&pNode->eaFxEvent) > 0) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Graphs are no longer allowed to have FX on nodes, these must be relocated onto the nodes moves (in the graph)");
			bRet = false;
		}

		FOR_EACH_IN_EARRAY(pNode->eaFxEvent, DynAnimGraphFxEvent, pFxEvent)
		{
			if (!dynAnimGraphFxEventVerify(pGraph, pNode, pFxEvent, bReport)														||
				!dynAnimGraphFxEventVerifyUnique(pGraph, pNode, NULL, pFxEvent, pNode->eaFxEvent,         false, false, bReport)	||
				!dynAnimGraphFxEventVerifyUnique(pGraph, pNode, NULL, pFxEvent, pGraph->eaOnEnterFxEvent, true,  false, bReport)	||
				!dynAnimGraphFxEventVerifyUnique(pGraph, pNode, NULL, pFxEvent, pGraph->eaOnExitFxEvent,  false, true,  bReport)	 )
			{
				bRet = false;
			}
		}
		FOR_EACH_END;

		if (SAFE_MEMBER(pTemplate,eType) == eAnimTemplateType_Movement)
		{
			if (pNode->bOverrideAllBones) {
				if (bReport) AnimFileError(pGraph->pcFilename, "Override all bones enabled on node %s of movement template typed graph, this is not allowed", pNode->pcName);
				bRet = false;
			}

			if (pNode->bSnapOverrideAllBones) {
				if (bReport) AnimFileError(pGraph->pcFilename, "Snap override all bones enabled on node %s of movement template typed graph, this is not allowed", pNode->pcName);
				bRet = false;
			}

			if (pNode->bOverrideDefaultMove) {
				if (bReport) AnimFileError(pGraph->pcFilename, "Override default movement enabled on node %s of movement template typed graph, this is not allowed", pNode->pcName);
				bRet = false;
			}

			if (pNode->bSnapOverrideDefaultMove) {
				if (bReport) AnimFileError(pGraph->pcFilename, "Snap override default movement enabled on node %s of movement template typed graph, this is not allowed", pNode->pcName);
				bRet = false;
			}

			if (pNode->bOverrideMovement) {
				if (bReport) AnimFileError(pGraph->pcFilename, "Override movement enabled on node %s of movement template typed graph, this is not allowed", pNode->pcName);
				bRet = false;
			}

			if (pNode->bSnapOverrideMovement) {
				if (bReport) AnimFileError(pGraph->pcFilename, "Snap override movement enabled on node %s of movement template typed graph, this is not allowed", pNode->pcName);
				bRet = false;
			}

			if (pNode->bAllowRandomMoveRepeats) {
				if (bReport) AnimFileError(pGraph->pcFilename, "Allow random move repeats enabled on node %s of movement template typed graph, this is not allowed", pNode->pcName);
				bRet = false;
			}
			
			if (pNode->fDisableTorsoPointingTimeout) {
				if (bReport) AnimFileError(pGraph->pcFilename, "Disable torso pointing timeout enabled on node %s of movement template typed graph, this is not allowed", pNode->pcName);
				bRet = false;
			}

			if (eaSize(&pNode->eaGraphImpact)) {
				if (bReport) AnimFileError(pGraph->pcFilename, "Found impact triggers on movement template typed graph node %s, this is not allowed", pNode->pcName);
				bRet = false;
			}
		}

		if (pNode->pTemplateNode &&
			eaSize(&pNode->eaSwitch) != eaSize(&pNode->pTemplateNode->eaSwitch)) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Found mismatched number of switches in the graph and template data, likely the template was updated behind the graph's back", pGraph->pcName, pNode->pcName);
			bRet = false;
		}

		FOR_EACH_IN_EARRAY(pNode->eaSwitch, DynAnimGraphSwitch, pSwitch)
		{
			if (pSwitch->fRequiredPlaytime < 0.f) {
				if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Found switch with negative required playtime", pGraph->pcName, pNode->pcName);
				bRet = false;
			}

			if (pTemplate && pTemplate->eType != eAnimTemplateType_Movement &&
				pSwitch->fRequiredPlaytime > 0.f)
			{
				if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Found switch with required playtime on non-movement typed graph", pGraph->pcName, pNode->pcName);
				bRet = false;
			}
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY(pNode->eaGraphImpact, DynAnimGraphTriggerImpact, pImpact)
		{
			if (!dynAnimGraphImpactVerify(pGraph, pNode, pImpact, bReport))
				bRet = false;
		}
		FOR_EACH_END;

		if (REF_HANDLE_IS_ACTIVE(pNode->hPostIdle))
		{
			if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Found post-idle on non-exit node", pGraph->pcName, pNode->pcName);
			bRet = false;
		}

		if (!pGraph->bPartialGraph)
		{
			DynAnimGraphNode** eaSeenNodes = NULL;
			S32 iSawMoveOnPath = -1;

			if (!eaSize(&pNode->eaMove) &&
				dynAnimGraphNodeRequiresValidPlaybackPath(pGraph, pNode, pGraph->eaNodes[0], &eaSeenNodes, false, &iSawMoveOnPath, false) && // a path exist from the start node to pNode that only uses the default switches and randomizer paths
				(	(	iSawMoveOnPath != 1 && // there wasn't a path or there was some path without a move between the start node and pNode
						!dynAnimGraphNodeOnValidPlaybackPath(pGraph, pNode, &eaSeenNodes, false, false)) // NOT ( all default switch paths from pNode either hit at least 1 move before reaching the end node OR hit at least 1 move before looping )))
						||
					(	!dynAnimGraphNodeOnValidPlaybackPath(pGraph, pNode, &eaSeenNodes, true, false)))) // NOT ( all default switch paths from pNode either reach the end node OR hit at least 1 move before looping )
			{
				if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s Node %s : Lies along a non-valid default switch / randomizer only path that's reachable from the start node without requiring any flags, you need to add at least one move on that path or to change path probabilities to make it unreachable",pGraph->pcName,pNode->pcName);
				bRet = false;
			}

			eaDestroy(&eaSeenNodes);

			if (!dynAnimGraphGroupMoveVerify(pGraph, pNode, bReport))
				bRet = false;
		}
	}
	else if (SAFE_MEMBER(pNode->pTemplateNode,eType) == eAnimTemplateNodeType_Randomizer)
	{
		if (eaSize(&pNode->eaPath) == 0)
		{
			if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Missing paths on randomizer node", pGraph->pcName, pNode->pcName);
			bRet = false;
		}

		if (eaSize(&pNode->eaPath) != eaSize(&pNode->pTemplateNode->eaPath))
		{
			if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Number of node paths differ on graph (%d) from template design (%d)", pGraph->pcName, pNode->pcName, eaSize(&pNode->eaPath), eaSize(&pNode->pTemplateNode->eaPath));
			bRet = false;
		}

		if (!dynAnimGraphGroupPathVerify(pGraph, pNode, bReport))
		{
			bRet = false;
		}

		if (REF_HANDLE_IS_ACTIVE(pNode->hPostIdle))
		{
			if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s, Node %s: Found post-idle on non-exit node", pGraph->pcName, pNode->pcName);
			bRet = false;
		}

		if (!pGraph->bPartialGraph)
		{
			DynAnimGraphNode** eaSeenNodes = NULL;
			S32 iSawMoveOnPath = -1;

			if (!eaSize(&pNode->eaMove) &&
				dynAnimGraphNodeRequiresValidPlaybackPath(pGraph, pNode, pGraph->eaNodes[0], &eaSeenNodes, false, &iSawMoveOnPath, false) && // a path exist from the start node to pNode that only uses the default switches and randomizer paths
				(	(	iSawMoveOnPath != 1 && // there wasn't a path or there was some path without a move between the start node and pNode
						!dynAnimGraphNodeOnValidPlaybackPath(pGraph, pNode, &eaSeenNodes, false, false)) // NOT ( all default switch paths from pNode either hit at least 1 move before reaching the end node OR hit at least 1 move before looping )))
					||
					(	!dynAnimGraphNodeOnValidPlaybackPath(pGraph, pNode, &eaSeenNodes, true, false)))) // NOT ( all default switch paths from pNode either reach the end node OR hit at least 1 move before looping )
			{
				if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s Node %s : Lies along a non-valid path that's reachable from the start node without requiring any flags, you need to add at least one move on that path or to change path probabilities to make it unreachable",pGraph->pcName,pNode->pcName);
				bRet = false;
			}

			eaDestroy(&eaSeenNodes);
		}
	}
	else if (SAFE_MEMBER(pNode->pTemplateNode,eType) == eAnimTemplateNodeType_End)
	{
		DynAnimGraph *postIdleGraph;
		if (postIdleGraph = GET_REF(pNode->hPostIdle))
		{
			if (postIdleGraph == pGraph)
			{
				if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s has set itself as the post idle in Node %s! This will loop forever!", pGraph->pcName, pNode->pcName);
				bRet = false;
			}

			if (postIdleGraph->bPartialGraph == true)
			{
				if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s has set partial graph '%s' as the post idle in Node %s! An actual graph must be used instead", pGraph->pcFilename, postIdleGraph->pcFilename, pNode->pcName);
				bRet = false;
			}	
		}
	}

	return bRet;
}


bool dynAnimGraphVerify(DynAnimGraph* pGraph, bool bReport)
{
	bool bRet=true;
	DynAnimTemplate* pTemplate = GET_REF(pGraph->hTemplate);

	if (!pGraph->bPartialGraph)
	{
		if(!resIsValidName(pGraph->pcName))
		{
			AnimFileError(pGraph->pcFilename, "Graph name \"%s\" is illegal.", pGraph->pcName);
			bRet = false;
		}

		if(!resIsValidScope(pGraph->pcScope))
		{
			AnimFileError(pGraph->pcFilename, "Graph scope \"%s\" is illegal.", pGraph->pcScope);
			bRet = false;
		}

		{
			const char* pcTempFileName = pGraph->pcFilename;
			if (resFixPooledFilename(&pcTempFileName, "dyn/animgraph", pGraph->pcScope, pGraph->pcName, "agraph"))
			{
				if (IsServer())
				{
					AnimFileError(pGraph->pcFilename, "Graph filename does not match name '%s' scope '%s'", pGraph->pcName, pGraph->pcScope);
					bRet = false;
				}
			}
		}
	}

	if (!pTemplate && !pGraph->bPartialGraph)
	{
		if (bReport) AnimFileError(pGraph->pcFilename, "Unable to find Template %s for Graph %s", REF_HANDLE_GET_STRING(pGraph->hTemplate), pGraph->pcName);
		bRet = false;
	}
	else if (pTemplate && !pGraph->bPartialGraph)
	{
		if (eaSize(&pTemplate->eaNodes) != eaSize(&pGraph->eaNodes))
		{
			if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s has %d nodes, while its Template %s has %d nodes!", pGraph->pcName, eaSize(&pGraph->eaNodes), pTemplate->pcName, eaSize(&pTemplate->eaNodes));
			bRet = false;
		}

		if (pGraph->bResetWhenMovementStops)
		{
			if (pTemplate->eType != eAnimTemplateType_Idle)
			{
				if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s: Only Idle-typed graphs are allowed to set the 'reset when movement stops' flag!", pGraph->pcName);
				bRet = false;
			}

			if (dynAnimTemplateHasMultiFlagStartNode(pTemplate))
			{
				if (bReport) AnimFileError(pGraph->pcFilename, "Graph %s can either be set to 'reset when movement stops' or have a multi-flag start node, not both!", pGraph->pcName);
				bRet = false;
			}
		}

		if (eaSize(&pGraph->eaNodes)) {
			DynAnimGraphNode** eaSeenNodes = NULL;
			if (!dynAnimGraphNodeOnValidPlaybackPath(pGraph, pGraph->eaNodes[0], &eaSeenNodes, false, bReport)) // NOT ( all default switch only paths from the start node either hit at least 1 move before reaching the end node OR hit at least 1 move before looping )
				bRet = false;
			eaDestroy(&eaSeenNodes);
		}
	}

	FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pNode)
	{
		if (!dynAnimGraphNodeVerify(pGraph, pNode, bReport))
			bRet = false;
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pGraph->eaOnEnterFxEvent, DynAnimGraphFxEvent, pFxEvent)
	{
		if (!dynAnimGraphFxEventVerify(pGraph, NULL, pFxEvent, bReport))
			bRet = false;
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pGraph->eaOnExitFxEvent, DynAnimGraphFxEvent, pFxEvent)
	{
		if (!dynAnimGraphFxEventVerify(pGraph, NULL, pFxEvent, bReport))
			bRet = false;
	}
	FOR_EACH_END;

	if (SAFE_MEMBER(pTemplate,eType) == eAnimTemplateType_Movement)
	{
		if (pGraph->bResetWhenMovementStops) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Reset when movement stops enabled on a movement template typed graph, this is not allowed");
			bRet = false;
		}

		if (pGraph->bOverrideAllBones) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Override all bones enabled on a movement template typed graph, this is not allowed");
			bRet = false;
		}

		if (pGraph->bSnapOverrideAllBones) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Snap override all bones enabled on a movement template typed graph, this is not allowed");
			bRet = false;
		}

		if (pGraph->bOverrideDefaultMove) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Override default movement enabled on a movement template typed graph, this is not allowed");
			bRet = false;
		}

		if (pGraph->bSnapOverrideDefaultMove) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Snap override default movement enabled on a movement template typed graph, this is not allowed");
			bRet = false;
		}

		if (pGraph->bOverrideMovement) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Override movement enabled on a movement template typed graph, this is not allowed");
			bRet = false;
		}

		if (pGraph->bSnapOverrideMovement) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Snap override movement enabled on a movement template typed graph, this is not allowed");
			bRet = false;
		}

		if (pGraph->bForceVisible) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Force visible enabled on a movement template typed graph, this is not allowed");
			bRet = false;
		}

		if (pGraph->bPitchToTarget) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Pitch to target enabled on a movement template typed graph, this is not allowed");
			bRet = false;
		}

		if (pGraph->bGeneratePowerMovementInfo) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Generate power movement info enabled on a movement template typed graph, this is not allowed");
			bRet = false;
		}

		if (pGraph->bDisableTorsoPointing || pGraph->fDisableTorsoPointingTimeout > 0.f) {
			if (bReport) AnimFileError(pGraph->pcFilename, "Disable torso pointing enabled on a movement template typed graph, this is not allowed");
			bRet = false;
		}
	}

	if (pGraph->fTimeout > 0 && pTemplate && pTemplate->eType != eAnimTemplateType_Idle)
	{
		if (bReport) AnimFileError(pGraph->pcFilename, "Enter post-idle timeouts only allowed when the template type is set to idle");
		bRet = false;
	}
	
	if (pGraph->bDisableTorsoPointing && pGraph->fDisableTorsoPointingRate <= 0.0f)
	{
		if (bReport) AnimFileError(pGraph->pcFilename, "Disable Torso Pointing Rate must be > 0");
		bRet = false;
	}

	if (pGraph->fDisableTorsoPointingTimeout < 0.0f)
	{
		if (bReport) AnimFileError(pGraph->pcFilename, "Disable Torso Pointing Timeout must be non-negative");
		bRet = false;
	}

	if (pGraph->fDisableGroundRegTimeout < 0.0f)
	{
		if (bReport) AnimFileError(pGraph->pcFilename, "Disable Ground Registration Timeout (All Bones) must be non-negative");
		bRet = false;
	}

	if (pGraph->fDisableUpperBodyGroundRegTimeout < 0.0f)
	{
		if (bReport) AnimFileError(pGraph->pcFilename, "Disable Ground Registration Timeout (Upper Body) must be non-negative");
		bRet = false;
	}

	return bRet;
}


static int dynAnimGraphResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, DynAnimGraph* pGraph, U32 userID)
{
	switch (eType)
	{
		xcase RESVALIDATE_FIX_FILENAME:
		{
			resFixPooledFilename((char**)&pGraph->pcFilename, "dyn/animgraph", pGraph->pcScope, pGraph->pcName, "agraph");
		}
		return VALIDATE_HANDLED;

		xcase RESVALIDATE_POST_TEXT_READING:
		{
			if (!(wl_state.load_flags & WL_NO_LOAD_DYNANIMATIONS))
			{
				if (pGraph->bPartialGraph || GET_REF(pGraph->hTemplate)) {
					dynAnimGraphFixup(pGraph);
					dynAnimGraphDefaultsFixup(pGraph);
					pGraph->bInvalid = false;
				} else {
					pGraph->bInvalid = true;
				}
			}

			dynAnimGraphPowerMovementFixup(pGraph);
		}
		return VALIDATE_HANDLED;

		xcase RESVALIDATE_POST_BINNING:
		{
			if (!(wl_state.load_flags & WL_NO_LOAD_DYNANIMATIONS))
			{
				if (pGraph->bPartialGraph || GET_REF(pGraph->hTemplate)) {
					dynAnimGraphFixup(pGraph);
					dynAnimGraphDefaultsFixup(pGraph);
					pGraph->bInvalid = false;
				} else {
					pGraph->bInvalid = true;
				}

				// This could have been in the RESVALIDATE_POST_TEXT_READING
				// and saved with the bin file. However if the Move files are
				// updated we want it to update the graph movement data. The
				// easy way of achieving this is to do it in RESVALIDATE_POST_BINNING.
				// If this ever becomes a performance issue we should move this 
				// function call into RESVALIDATE_POST_TEXT_READING.
				dynAnimGraphPowerMovementFixup(pGraph);
			}
		}
		return VALIDATE_HANDLED;

		xcase RESVALIDATE_FINAL_LOCATION:
		{
			if (!(wl_state.load_flags & WL_NO_LOAD_DYNANIMATIONS))
			{
				dynAnimGraphVerify(pGraph, true);
			}
		}
		return VALIDATE_HANDLED;

		xcase RESVALIDATE_CHECK_REFERENCES:
		{
			if (!(wl_state.load_flags & WL_NO_LOAD_DYNANIMATIONS))
			{
				FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pChkNode)
				{
					if (REF_HANDLE_IS_ACTIVE(pChkNode->hPostIdle))
					{
						DynAnimGraph *pPostIdleGraph = GET_REF(pChkNode->hPostIdle);
						if (!pPostIdleGraph) {
							AnimFileError(pGraph->pcFilename, "Graph %s specifies non-existant post idle graph! Fix or remove reference!", pGraph->pcName);
						} else {
							if (pPostIdleGraph == pGraph) {
								AnimFileError(pGraph->pcFilename, "Graph %s has set itself as the post idle in Node %s! This will loop forever!", pGraph->pcName, pChkNode->pcName);
							}
							if (pPostIdleGraph->bPartialGraph == true) {
								AnimFileError(pGraph->pcFilename, "Graph %s has set partial graph '%s' as the post idle in Node %s! An actual graph must be used instead", pGraph->pcName, pPostIdleGraph->pcFilename, pChkNode->pcName);
							}
						}
					}
				}
				FOR_EACH_END;
			}
		}
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void registerAnimGraphDictionaries(void)
{
	//ResourceDictionary *pDictionary;
	hAnimGraphDict = RefSystem_RegisterSelfDefiningDictionary(ANIM_GRAPH_EDITED_DICTIONARY, false, parse_DynAnimGraph, true, true, NULL);
	//pDictionary = resGetDictionary(hAnimGraphDict);
	//pDictionary->iParserWriteFlags = WRITETEXTFLAG_WRITEDEFAULTSIFUSED | WRITETEXTLFAG_ONLY_WRITE_USEDFIELDS;

	resDictManageValidation(hAnimGraphDict, dynAnimGraphResValidateCB);

	if (IsServer())
	{
		resDictProvideMissingResources(hAnimGraphDict);
		if (isDevelopmentMode() || isProductionEditMode())
		{
			resDictMaintainInfoIndex(hAnimGraphDict, ".name", ".scope", NULL, NULL, NULL);
		}
	}
	else if (IsClient())
	{
		resDictRequestMissingResources(hAnimGraphDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
	}
	//resDictProvideMissingRequiresEditMode(hAnimGraphDict);
	resDictRegisterEventCallback(hAnimGraphDict, dynAnimGraphRefDictCallback, NULL);
}

/* reloading is handled by the resource dict.
static void dynAnimGraphReloadCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if(!ParserReloadFileToDictionary(relpath,hAnimGraphDict))
	{
		AnimFileError(relpath, "Error reloading DynAnimGraph file: %s", relpath);
	}
	//danimForceDataReload(); // happens in callback?
}
*/

void dynAnimGraphLoadAll(void)
{
	if (!bLoadedOnce)
	{
		S32 iParserFlags = PARSER_OPTIONALFLAG;
		if (IsServer())
		{			
			iParserFlags |= RESOURCELOAD_SHAREDMEMORY;
		}

		loadstart_printf("Loading DynAnimGraphs...");
		resLoadResourcesFromDisk(hAnimGraphDict, "dyn/animgraph", ".agraph", "DynAnimGraph.bin", iParserFlags);
		loadend_printf(" done (%d DynAnimGraphs)", RefSystem_GetDictionaryNumberOfReferents(hAnimGraphDict));

		bLoadedOnce = true;
	}
}

void dynAnimGraphReloadAll(void)
{
	DictionaryEArrayStruct *pAnimGraphArray = resDictGetEArrayStruct(hAnimGraphDict);

	//this should only be run without shared memory since it reprocesses the
	//existing data.. to work with shared memory it would have to reload the
	//files from disk (which is significantly slower)
	assert(sharedMemoryGetMode() != SMM_ENABLED);

	loadstart_printf("Reloading DynAnimGraphs...");
	FOR_EACH_IN_EARRAY(pAnimGraphArray->ppReferents, DynAnimGraph, pGraph)
	{
		if (pGraph->bPartialGraph || GET_REF(pGraph->hTemplate)) {
			dynAnimGraphFixup(pGraph);
			dynAnimGraphDefaultsFixup(pGraph);
			pGraph->bInvalid = false;
		} else {
			pGraph->bInvalid = true;
		}
		dynAnimGraphPowerMovementFixup(pGraph);
		dynAnimGraphVerify(pGraph, true);
	}
	FOR_EACH_END
	loadend_printf("done (%d DynAnimGraphs)", RefSystem_GetDictionaryNumberOfReferents(hAnimGraphDict) );
}

void dynAnimGraphServerReload(void)
{
	RefDictIterator iterator;
	const char **eaReloadGraphFiles = NULL;
	DynAnimGraph* pGraph;
	int iCount = 0;

	assert(IsGameServerBasedType());

	loadstart_printf("Reloading DynAnimGraphs...");
	eaCreate(&eaReloadGraphFiles);

	RefSystem_InitRefDictIterator(hAnimGraphDict, &iterator);
	while ((pGraph = RefSystem_GetNextReferentFromIterator(&iterator)))
	{
		if (pGraph->bGeneratePowerMovementInfo) {
			eaPushUnique(&eaReloadGraphFiles, pGraph->pcFilename);
		}
	}

	#define DYNANIMGRAPH_MAX_ACCESS_ATTEMPTS 10000
	FOR_EACH_IN_EARRAY(eaReloadGraphFiles, const char, pcFilename)
	{
		int i = 0;
		while (!fileCanGetExclusiveAccess(pcFilename) && i < DYNANIMGRAPH_MAX_ACCESS_ATTEMPTS) {
			Sleep(1);
			i++;
		}
		if (i < DYNANIMGRAPH_MAX_ACCESS_ATTEMPTS) {
			//fileWaitForExclusiveAccess(pcFilename);
			if (!ParserReloadFileToDictionary(pcFilename, hAnimGraphDict)) {
				AnimFileError(pcFilename, "Error reloading DynAnimGraph file %s", pcFilename);
			} else {
				iCount++;
			}
		} else {
			AnimFileError(pcFilename, "Error reloading DynAnimGraph file %s, make sure EditPlus : File -> Lock is disabled!", pcFilename);
		}
	}
	FOR_EACH_END;
	#undef DYNANIMGRAPH_MAX_ACCESS_ATTEMPTS

	eaDestroy(&eaReloadGraphFiles);
	loadend_printf("done. Reloaded %d DynAnimGraphs!", iCount);
}

void dynAnimGraphTemplateChanged(DynAnimGraph* pGraph)
{
	if (!pGraph->bPartialGraph)
	{
		DynAnimTemplate *pTemplate;
		eaDestroyStruct(&pGraph->eaNodes, parse_DynAnimGraphNode);
		eaDestroyStruct(&pGraph->eaOnEnterFxEvent, parse_DynAnimGraphFxEvent);
		eaDestroyStruct(&pGraph->eaOnExitFxEvent, parse_DynAnimGraphFxEvent);
		eaDestroyStruct(&pGraph->eaStance, parse_DynAnimGraphStance);
		eaDestroyStruct(&pGraph->eaSuppress, parse_DynAnimGraphSuppress);

		pTemplate = GET_REF(pGraph->hTemplate);

		if (pTemplate)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(pTemplate->eaNodes, DynAnimTemplateNode, pTemplateNode)
			{
				DynAnimGraphNode *pTemplateGraphNode;
				DynAnimGraphNode* pGraphNode = StructCreate(parse_DynAnimGraphNode);
				pGraphNode->pcName = pTemplateNode->pcName;
				pGraphNode->fX = pTemplateNode->fX;
				pGraphNode->fY = pTemplateNode->fY;
				pGraphNode->pTemplateNode = pTemplateNode;
				pTemplateGraphNode = dynAnimTemplateGraphNodeFromGraphNode(pGraph, pGraphNode);
				FOR_EACH_IN_EARRAY_FORWARDS(pTemplateGraphNode->eaSwitch, DynAnimGraphSwitch, pTemplateSwitch)
				{
					DynAnimGraphSwitch* pSwitch = StructClone(parse_DynAnimGraphSwitch, pTemplateSwitch);
					eaPush(&pGraphNode->eaSwitch, pSwitch);
				}
				FOR_EACH_END;
				eaPush(&pGraph->eaNodes, pGraphNode);
			}
			FOR_EACH_END;
		}

		dynAnimGraphSetupNewInheritBits(pGraph);
		dynAnimGraphApplyInheritBits(pGraph);
	}
}

void dynAnimGraphNodeNormalizeChances(DynAnimGraphNode* pNode)
{
	if (pNode->pTemplateNode->eType == eAnimTemplateNodeType_Normal)
	{
		S32 i;
		for (i = 0; i < DYNMOVEMENT_NUMDIRECTIONS; i++)
		{
			F32 fTotalChance = 0.0f;
			S32 iNodeCount = 0;

			FOR_EACH_IN_EARRAY(pNode->eaMove, DynAnimGraphMove, pMove) {
				if (pMove->eDirection == i) {
					fTotalChance += pMove->fChance;
					iNodeCount++;
				}
			} FOR_EACH_END;

			if (fabsf(fTotalChance - 1.0f) > EPSILON)
			{
				FOR_EACH_IN_EARRAY(pNode->eaMove, DynAnimGraphMove, pMove) {
					if (pMove->eDirection == i) {
						if (fTotalChance > 0.0f)
							pMove->fChance /= fTotalChance;
						else
							pMove->fChance = 1.0f / (F32)(iNodeCount);
					}
				} FOR_EACH_END;
			}
		}
	}
	else if (pNode->pTemplateNode->eType == eAnimTemplateNodeType_Randomizer)
	{
		F32 fTotalChance = 0.0f;
		FOR_EACH_IN_EARRAY(pNode->eaPath, DynAnimGraphPath, pPath)
		{
			fTotalChance += pPath->fChance;
		}
		FOR_EACH_END;
		if (fabsf(fTotalChance - 1.0f) > EPSILON)
		{
			FOR_EACH_IN_EARRAY(pNode->eaPath, DynAnimGraphPath, pPath)
			{
				if (fTotalChance > 0.0)
					pPath->fChance /= fTotalChance;
				else
					pPath->fChance = 1.0f / (F32)(eaSize(&pNode->eaPath));
			}
			FOR_EACH_END;
		}
	}
}

const DynAnimGraphMove* dynAnimGraphNodeChooseMove(	const DynAnimGraph *pGraph,
													const DynAnimGraphNode* pNode,
													const DynAnimGraphMove *pMovePrev)
{
	if (eaSize(&pNode->eaMove) == 0) {
		return NULL;
	}
	else if (eaSize(&pNode->eaMove) == 1) {
		return pNode->eaMove[0];
	}
	else
	{
		DynAnimGraphMove *pFoundMove = NULL;
		F32 fRandNum = randomPositiveF32();
		F32 fTotalChance = 0.0f;
		S32 iPrevMoveIndex;

		if (!pNode->bAllowRandomMoveRepeats) {
			iPrevMoveIndex = eaFind(&pNode->eaMove, pMovePrev);
			if (iPrevMoveIndex >= 0)
				fRandNum *= 1.0f - pNode->eaMove[iPrevMoveIndex]->fChance;
		}

		FOR_EACH_IN_EARRAY_FORWARDS(pNode->eaMove, DynAnimGraphMove, pMove)
		{
			if (bAnimGraphEditorMode && pMove->bEditorPlaybackBias) {
				pFoundMove = pMove;
			} else if (pNode->bAllowRandomMoveRepeats || iPrevMoveIndex != ipMoveIndex) {
				fTotalChance += pMove->fChance;
				if (!pFoundMove && fRandNum < fTotalChance)
					pFoundMove = pMove;
			}
		}
		FOR_EACH_END;

		if (pFoundMove) return pFoundMove;
		else            return pNode->eaMove[eaSize(&pNode->eaMove)-1];
	}
}

const DynAnimGraphNode* dynAnimGraphFindNode(const DynAnimGraph* pGraph, const char* pcNodeName)
{
	FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pNode)
	{
		if (pNode->pcName == pcNodeName)
			return pNode;
	}
	FOR_EACH_END;
	return NULL;
}

bool dynAnimGraphOnPathRandomizer(const DynAnimGraphNode *pNode)
{
	return pNode->pTemplateNode->eType == eAnimTemplateNodeType_Randomizer;
}

bool dynAnimGraphNextNodeIsEndFated(const DynAnimGraph* pGraph,
									const DynAnimGraphNode* pNode,
									const char* pcFlag,
									F32 fFate)
{
	DynAnimTemplateNode* pTemplateNode = pNode->pTemplateNode;
	DynAnimTemplateNode *pNext;
	
	assert(!pNode->bInvalidForPlayback);

	if (fFate < 0)
	{
		pNext = pTemplateNode->defaultNext.p;
		if (pcFlag)
		{
			FOR_EACH_IN_EARRAY(pTemplateNode->eaSwitch, DynAnimTemplateSwitch, pSwitch)
			{
				if (pSwitch->pcFlag == pcFlag)
				{
					const DynAnimGraphNode* pChkValid = dynAnimGraphNodeFromTemplateNode(pGraph, pSwitch->next.p);
					if (pChkValid && !pChkValid->bInvalidForPlayback)
					{
						pNext = pSwitch->next.p;
						break;
					}
				}
			}
			FOR_EACH_END;
		}
	}
	else // fFate >= 0, we were on a randomizer node, it's assumed valid randomizers won't goto invalid paths due to verification
	{
		DynAnimTemplateNode *pFoundNode = NULL;
		F32 fTotalChance = 0.0f;

		pNext = pTemplateNode->eaPath[eaSize(&pNode->eaPath)-1]->next.p;

		FOR_EACH_IN_EARRAY_FORWARDS(pNode->eaPath, DynAnimGraphPath, pPath)
		{
			if (bAnimGraphEditorMode		&&
				pPath->bEditorPlaybackBias	&&
				0.f < pPath->fChance)
			{
				pFoundNode = pTemplateNode->eaPath[ipPathIndex]->next.p;
			}
			else
			{
				fTotalChance += pPath->fChance;
				if (!pFoundNode && fFate < fTotalChance)
					pFoundNode = pTemplateNode->eaPath[ipPathIndex]->next.p;
			}
		}
		FOR_EACH_END;

		if (pFoundNode) return (pFoundNode->eType == eAnimTemplateNodeType_End);
	}
	
	return (pNext->eType == eAnimTemplateNodeType_End);
}

const DynAnimGraphNode* dynAnimGraphNodeGetNextNodeFated(	const DynAnimGraph* pGraph,
															const DynAnimGraphNode* pNode,
															const char* pcFlag,
															const char* pcRestartFlag,
															F32 fFate)
{
	DynAnimTemplateNode* pTemplateNode = pNode->pTemplateNode;
	DynAnimTemplateNode* pNext;

	assert(!pNode->bInvalidForPlayback);
	
	if (fFate < 0.0f)
	{
		pNext = pTemplateNode->defaultNext.p;
		if (pcFlag)
		{
			FOR_EACH_IN_EARRAY(pTemplateNode->eaSwitch, DynAnimTemplateSwitch, pSwitch)
			{
				if (pSwitch->pcFlag == pcFlag)
				{
					const DynAnimGraphNode* pChkValid = dynAnimGraphNodeFromTemplateNode(pGraph, pSwitch->next.p);
					if (pChkValid && !pChkValid->bInvalidForPlayback)
					{
						pNext = pSwitch->next.p;
						break;
					}
				}
			}
			FOR_EACH_END;
		}
	}
	else //fFate >= 0, we were on a randomizer node, it's assumed valid randomizers won't goto invalid paths due to verification
	{
		DynAnimTemplateNode *pFoundNode = NULL;
		F32 fTotalChance = 0.0f;

		pNext = pTemplateNode->eaPath[eaSize(&pNode->eaPath)-1]->next.p;

		FOR_EACH_IN_EARRAY_FORWARDS(pNode->eaPath, DynAnimGraphPath, pPath)
		{
			if (bAnimGraphEditorMode		&&
				pPath->bEditorPlaybackBias	&&
				0.f < pPath->fChance)
			{
				pFoundNode = pTemplateNode->eaPath[ipPathIndex]->next.p;
			}
			else
			{
				fTotalChance += pPath->fChance;
				if (!pFoundNode && fFate < fTotalChance)
					pFoundNode = pTemplateNode->eaPath[ipPathIndex]->next.p;
			}
		}
		FOR_EACH_END;

		if (pFoundNode)
		{
			const DynAnimGraphNode* pRet = NULL;

			if (pFoundNode->eType == eAnimTemplateNodeType_End)
			{
				DynAnimTemplate *pTemplate = GET_REF(pGraph->hTemplate);
				assert(pTemplate);

				pFoundNode = pTemplate->eaNodes[0]->defaultNext.p;

				FOR_EACH_IN_EARRAY(pTemplate->eaNodes[0]->eaSwitch, DynAnimTemplateSwitch, pSwitch)
				{
					if (pSwitch->pcFlag == pcFlag)
					{
						const DynAnimGraphNode* pChkValid = dynAnimGraphNodeFromTemplateNode(pGraph, pSwitch->next.p);
						if (pChkValid && !pChkValid->bInvalidForPlayback)
						{			
							pFoundNode = pSwitch->next.p;
						}
					} 
				}
				FOR_EACH_END;
			}

			assert(pNext);
			return dynAnimGraphFindNode(pGraph, pFoundNode->pcName);
		}
	}

	if (pNext->eType == eAnimTemplateNodeType_End)
	{
		DynAnimTemplate* pTemplate = GET_REF(pGraph->hTemplate);
		assert(pTemplate);

		pNext = pTemplate->eaNodes[0]->defaultNext.p;

		FOR_EACH_IN_EARRAY(pTemplate->eaNodes[0]->eaSwitch, DynAnimTemplateSwitch, pSwitch)
		{
			if (pSwitch->pcFlag == (pcRestartFlag?pcRestartFlag:pcFlag))
			{
				DynAnimGraphNode* pChkValid = dynAnimGraphNodeFromTemplateNode(pGraph, pSwitch->next.p);
				if (pChkValid && !pChkValid->bInvalidForPlayback)
				{
					pNext = pSwitch->next.p;
				}
			}
		}
		FOR_EACH_END;
	}
	assert(pNext);
	return dynAnimGraphFindNode(pGraph, pNext->pcName);
}

const DynAnimGraph* dynAnimGraphGetPostIdleFated(	const DynAnimGraph* pGraph,
													const DynAnimGraphNode* pNode,
													const char* pcFlag,
													F32 fFate)
{
	DynAnimTemplateNode* pTemplateNode = pNode->pTemplateNode;
	DynAnimTemplateNode* pNext;
	const DynAnimGraphNode *pEndNode;

	assert(!pNode->bInvalidForPlayback);

	if (fFate < 0.0f)
	{
		pNext = pTemplateNode->defaultNext.p;
		if (pcFlag)
		{
			FOR_EACH_IN_EARRAY(pTemplateNode->eaSwitch, DynAnimTemplateSwitch, pSwitch)
			{
				if (pSwitch->pcFlag == pcFlag)
				{
					DynAnimGraphNode* pChkValid = dynAnimGraphNodeFromTemplateNode(pGraph, pSwitch->next.p);
					if (pChkValid && !pChkValid->bInvalidForPlayback)
					{
						pNext = pSwitch->next.p;
						break;
					}
				}
			}
			FOR_EACH_END;
		}
	}
	else //fFate >= 0, we were on a randomizer node, it's assumed valid randomizers won't goto invalid paths due to verification
	{
		DynAnimTemplateNode *pFoundNode = NULL;
		F32 fTotalChance = 0.0f;

		pNext = pTemplateNode->eaPath[eaSize(&pNode->eaPath)-1]->next.p;

		FOR_EACH_IN_EARRAY_FORWARDS(pNode->eaPath, DynAnimGraphPath, pPath)
		{
			if (bAnimGraphEditorMode		&&
				pPath->bEditorPlaybackBias	&&
				0.f < pPath->fChance)
			{
				pFoundNode = pTemplateNode->eaPath[ipPathIndex]->next.p;
			}
			else
			{
				fTotalChance += pPath->fChance;
				if (!pFoundNode && fFate < fTotalChance)
					pFoundNode = pTemplateNode->eaPath[ipPathIndex]->next.p;
			}
		}
		FOR_EACH_END;

		if (pFoundNode) {
			assert(pFoundNode->eType == eAnimTemplateNodeType_End);
			pEndNode = dynAnimGraphFindNode(pGraph, pFoundNode->pcName);
			return GET_REF(pEndNode->hPostIdle);
		}
	}

	assert(pNext);
	pEndNode = dynAnimGraphFindNode(pGraph, pNext->pcName);
	return GET_REF(pEndNode->hPostIdle);
}

static void dynAnimGraphBuildDirectionalData(DynAnimGraph* pGraph)
{
	DynAnimTemplate* pTemplate = GET_REF(pGraph->hTemplate);

	FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pNode)
	{
		if (pNode->eaDirectionalData) {
			FOR_EACH_IN_EARRAY(pNode->eaDirectionalData, DynAnimGraphDirectionalData, pDirectionalData) {
				StructDestroy(parse_DynAnimGraphDirectionalData, pDirectionalData);
			} FOR_EACH_END;
			eaDestroy(&pNode->eaDirectionalData);
		}

		if (SAFE_MEMBER(pTemplate,eType) == eAnimTemplateType_Movement)
		{
			S32 i, maxMoveDirection;

			for (i = 0; i < DYNMOVEMENT_NUMDIRECTIONS; i++) {
				eaPush(&pNode->eaDirectionalData, StructCreate(parse_DynAnimGraphDirectionalData));
			}

			maxMoveDirection = -1;
			FOR_EACH_IN_EARRAY(pNode->eaMove, DynAnimGraphMove, pMove)
			{
				assert(pMove->eDirection < eaSize(&pNode->eaDirectionalData));
				eaPush(&pNode->eaDirectionalData[pMove->eDirection]->eaMove, StructClone(parse_DynAnimGraphMove,pMove));
				MAX1(maxMoveDirection, pMove->eDirection);
			}
			FOR_EACH_END;

			if (maxMoveDirection < 0) {
				pNode->eNumDirections = eDynMovementNumDirections_None;
			} else if (maxMoveDirection == 0) {
				pNode->eNumDirections = eDynMovementNumDirections_0;
			} else if (maxMoveDirection == 1) {
				pNode->eNumDirections = eDynMovementNumDirections_1;
			} else if (maxMoveDirection == 2) {
				pNode->eNumDirections = eDynMovementNumDirections_2;
			} else if (maxMoveDirection <= 4) {
				pNode->eNumDirections = eDynMovementNumDirections_4;
			} else {
				pNode->eNumDirections = eDynMovementNumDirections_8;
			}
		}
	}
	FOR_EACH_END;
}

static bool dynAnimGraphNodeRequiresValidPlaybackPath(const DynAnimGraph* pGraph, const DynAnimGraphNode* pFindNode, const DynAnimGraphNode* pNode, const DynAnimGraphNode*** peaSeenNodes, bool bSawMoveIn, S32* piSawMoveOut, bool bAllowSwitches)
{
	// this function
	// * searches all possible depth 1st traversals from pNode to any loops or end nodes in the graph using default switches and randomizer paths
	// * will include keyword-driven switches if you set bAllowSwitches
	// * returns true if it finds pFindNode on at least one of those traversals
	// * lets you know if there was at least one move one the path between pNode & pFindNode with the puiSawMoveOut variable (-1 = never found move, 0 = some path exists without a move, 1 = all paths include a move)

	DynAnimTemplateNode* pTemplateNode;

	if (!pGraph	|| !pNode || !pNode->pTemplateNode) {
		return false;
	}

	// used to prevent loops, note that we add nodes as we enter the graph & remove nodes as we backout of the graph
	if (0 <= eaFind(peaSeenNodes, pNode)) {
		return false;
	}

	eaPush(peaSeenNodes, pNode);

	pTemplateNode = pNode->pTemplateNode;

	switch (pTemplateNode->eType)
	{
		xcase eAnimTemplateNodeType_Start:
		acase eAnimTemplateNodeType_Normal:
		{
			bool bJustSawMove = eaSize(&pNode->eaMove);

			if (pNode == pFindNode)
			{
				if (piSawMoveOut)
				{
					if ((bJustSawMove||bSawMoveIn) && (*piSawMoveOut < 0)) {
						// only set this true (1) if we found a move along the 1st found path from pNode to pFindNode
						*piSawMoveOut = 1;
					}
					else if (!bJustSawMove && !bSawMoveIn) {
						// always set this to false (0) if we find any path without a move from pNode to pFindNode
						*piSawMoveOut = 0;
					}
				}

				assert(eaPop(peaSeenNodes) == pNode);
				return true;
			}
			else
			{
				const DynAnimTemplateNode* pNextTemplateNode = pTemplateNode->defaultNext.p;
				const DynAnimGraphNode* pNextGraphNode = dynAnimGraphNodeFromTemplateNode(pGraph, pNextTemplateNode);
				bool bRet = dynAnimGraphNodeRequiresValidPlaybackPath(pGraph, pFindNode, pNextGraphNode, peaSeenNodes, bJustSawMove||bSawMoveIn, piSawMoveOut, bAllowSwitches);

				if (bAllowSwitches)
				{
					FOR_EACH_IN_EARRAY(pTemplateNode->eaSwitch, DynAnimTemplateSwitch, pSwitch)
					{
						pNextTemplateNode = pSwitch->next.p;
						pNextGraphNode = dynAnimGraphNodeFromTemplateNode(pGraph, pNextTemplateNode);
						if (dynAnimGraphNodeRequiresValidPlaybackPath(pGraph, pFindNode, pNextGraphNode, peaSeenNodes, bJustSawMove||bSawMoveIn, piSawMoveOut, bAllowSwitches)) {
							// need to check all paths due to pbSawMoveOut
							bRet |= true;
							//break;
						}
					}
					FOR_EACH_END;
				}

				assert(eaPop(peaSeenNodes) == pNode);
				return bRet;
			}
		}
		xcase eAnimTemplateNodeType_Randomizer:
		{
			if (pNode == pFindNode)
			{
				if (piSawMoveOut)
				{
					if (bSawMoveIn && (*piSawMoveOut < 0)) {
						// only set this true (1) if we found a move along the 1st found path from pNode to pFindNode
						*piSawMoveOut = 1;
					}
					else if (!bSawMoveIn) {
						// always set this to false (0) if we find any path without a move from pNode to pFindNode
						*piSawMoveOut = 0;
					}
				}

				assert(eaPop(peaSeenNodes) == pNode);
				return true;
			}
			else 
			{
				bool bRet = false;
				
				FOR_EACH_IN_EARRAY(pTemplateNode->eaPath, DynAnimTemplatePath, pTemplatePath)
				{
					const DynAnimTemplateNode* pNextTemplateNode = pTemplatePath->next.p;
					const DynAnimGraphNode* pNextGraphNode = dynAnimGraphNodeFromTemplateNode(pGraph, pNextTemplateNode);
					if (ipTemplatePathIndex < eaSize(&pNode->eaPath)		&&
						0.f < pNode->eaPath[ipTemplatePathIndex]->fChance	&&
						dynAnimGraphNodeRequiresValidPlaybackPath(pGraph, pFindNode, pNextGraphNode, peaSeenNodes, bSawMoveIn, piSawMoveOut, bAllowSwitches))
					{
						// need to check all paths due to pbSawMoveOut
						bRet |= true;
						//break;
					}
				}
				FOR_EACH_END;

				assert(eaPop(peaSeenNodes) == pNode);
				return bRet;
			}
		}
		xcase eAnimTemplateNodeType_End:
		{
			assert(eaPop(peaSeenNodes) == pNode);
			return false;
		}
		xdefault:
		{
			assert(0);
		}
	}
}

static bool dynAnimGraphNodeOnValidPlaybackPath(const DynAnimGraph* pGraph, const DynAnimGraphNode* pNode, const DynAnimGraphNode*** peaSeenNodes, bool bAllowEnd, bool bReport)
{
	// this function
	// * searches all possible depth 1st traversals from pNode to any loops or end nodes in the graph using only default switches and randomizer paths
	// * returns true if all of those paths contain a move before the loop or end node
	// * optionally returns true if bAllowEnd is set and you don't hit a move before an end node

	DynAnimTemplateNode* pTemplateNode;

	if (!pGraph	|| !pNode || !pNode->pTemplateNode) {
		return false;
	}

	// used to prevent loops, note that we add nodes as we enter the graph & remove nodes as we backout of the graph
	if (0 <= eaFind(peaSeenNodes, pNode)) {
		return false;
	}

	eaPush(peaSeenNodes, pNode);

	pTemplateNode = pNode->pTemplateNode;

	switch (pTemplateNode->eType)
	{
		xcase eAnimTemplateNodeType_Normal:
		acase eAnimTemplateNodeType_Start:
		{
			if (eaSize(&pNode->eaMove)) {
				assert(eaPop(peaSeenNodes) == pNode);
				return true;
			} else {
				const DynAnimTemplateNode* pNextTemplateNode = pTemplateNode->defaultNext.p;
				const DynAnimGraphNode* pNextGraphNode = dynAnimGraphNodeFromTemplateNode(pGraph, pNextTemplateNode);
				bool bRet = dynAnimGraphNodeOnValidPlaybackPath(pGraph, pNextGraphNode, peaSeenNodes, bAllowEnd, bReport);
				assert(eaPop(peaSeenNodes) == pNode);
				return bRet;
			}
		}
		xcase eAnimTemplateNodeType_Randomizer:
		{
			bool bRet = true;
			FOR_EACH_IN_EARRAY(pTemplateNode->eaPath, DynAnimTemplatePath, pTemplatePath)
			{
				const DynAnimTemplateNode* pNextTemplateNode = pTemplatePath->next.p;
				const DynAnimGraphNode* pNextGraphNode = dynAnimGraphNodeFromTemplateNode(pGraph, pNextTemplateNode);
				if (ipTemplatePathIndex < eaSize(&pNode->eaPath)		&&
					0.f < pNode->eaPath[ipTemplatePathIndex]->fChance	&&
					!dynAnimGraphNodeOnValidPlaybackPath(pGraph, pNextGraphNode, peaSeenNodes, bAllowEnd, bReport))
				{
					bRet = false;
					break;
				}
			}
			FOR_EACH_END;
			assert(eaPop(peaSeenNodes) == pNode);
			return bRet;
		}
		xcase eAnimTemplateNodeType_End:
		{
			if (bAllowEnd) {
				assert(eaPop(peaSeenNodes) == pNode);
				return true;
			} else {
				// this error is only an accurate description when bRet = true, otherwise, it's from whatever node the initial call was on
				if (bReport) AnimFileError(pGraph->pcFilename, "Not all default paths through graph from start node hit a move before reaching the end node");
				assert(eaPop(peaSeenNodes) == pNode);
				return false;
			}
		}
		xdefault:
		{
			assert(0);
		}
	}
}

bool dynAnimGraphNodeIsValidForPlayback(const DynAnimGraph* pGraph, const DynAnimTemplateNode* pTemplateNode)
{
	const DynAnimGraphNode* pGraphNode = dynAnimGraphNodeFromTemplateNode(pGraph,pTemplateNode);
	return (pGraphNode && !pGraphNode->bInvalidForPlayback);
}

int dynAnimGraphCompareMoveDisplayOrder(const void** pa, const void** pb)
{
	const DynAnimGraphMove *a = *(const DynAnimGraphMove **)pa;
	const DynAnimGraphMove *b = *(const DynAnimGraphMove **)pb;

	S32 iOrderA = dynMovementDirectionDisplayOrder(a->eDirection);
	S32 iOrderB = dynMovementDirectionDisplayOrder(b->eDirection);

	const char *pcA = NULL_TO_EMPTY(REF_HANDLE_GET_STRING(a->hMove));
	const char *pcB = NULL_TO_EMPTY(REF_HANDLE_GET_STRING(b->hMove));

	if (iOrderA < iOrderB)
		return 1;
	if (iOrderB < iOrderA)
		return -1;
	return stricmp(pcA, pcB);
}

static int dynAnimGraphFxEventGetSearchStringCount(const DynAnimGraphFxEvent *pGraphFxEvent, const char *pcSearchText)
{
	int count = 0;

	if (pGraphFxEvent->pcFx	&& strstri(pGraphFxEvent->pcFx,	pcSearchText))	count++;

	if (pGraphFxEvent->bMovementBlendTriggered	&& strstri("MovementBlendTriggered",	pcSearchText))	count++;
	if (pGraphFxEvent->bMessage					&& strstri("Message",					pcSearchText))	count++;
	
	return count;
}

static int dynAnimGraphTriggerImpactGetSearchStringCount(const DynAnimGraphTriggerImpact *pTriggerImpact, const char *pcSearchText)
{
	int count = 0;

	if (pTriggerImpact->pcBone	&& strstri(pTriggerImpact->pcBone,	pcSearchText))	count++;

	return count;
}

static int dynAnimGraphMoveGetSearchStringCount(const DynAnimGraphMove *pGraphMove, const char *pcSearchText)
{
	int count = 0;

	if (REF_HANDLE_IS_ACTIVE(pGraphMove->hMove) && strstri(REF_HANDLE_GET_STRING(pGraphMove->hMove),	pcSearchText))	count++;

	FOR_EACH_IN_EARRAY(pGraphMove->eaFxEvent, DynAnimGraphFxEvent, pFx) {
		count += dynAnimGraphFxEventGetSearchStringCount(pFx, pcSearchText);
	} FOR_EACH_END;

	return count;
}

static int dynAnimGraphSuppressGetSearchStringCount(const DynAnimGraphSuppress *pGraphSuppress, const char *pcSearchText)
{
	int count = 0;

	if (pGraphSuppress->pcSuppressionTag	&& strstri(pGraphSuppress->pcSuppressionTag,	pcSearchText))	count++;
	
	return count;
}

static int dynAnimGraphStanceGetSearchStringCount(const DynAnimGraphStance *pGraphStance, const char *pcSearchText)
{
	int count = 0;

	if (pGraphStance->pcStance	&& strstri(pGraphStance->pcStance,	pcSearchText))	count++;

	return count;
}

static int dynAnimGraphNodeGetSearchStringCount(const DynAnimGraphNode *pGraphNode, const char *pcSearchText)
{
	int count = 0;

	if (pGraphNode->pcName	&& strstri(pGraphNode->pcName,	pcSearchText))	count++;

	//fX ignored
	//fY ignored

	FOR_EACH_IN_EARRAY(pGraphNode->eaMove, DynAnimGraphMove, pMove) {
		count += dynAnimGraphMoveGetSearchStringCount(pMove, pcSearchText);
	} FOR_EACH_END;

	//template node ignored

	if (REF_HANDLE_IS_ACTIVE(pGraphNode->hPostIdle) && strstri(REF_HANDLE_GET_STRING(pGraphNode->hPostIdle), pcSearchText)) count++;

	//path ignored
	//interpBlock ignored

	if (pGraphNode->bNoSelfInterp			&& strstri("NoSelfInterp",			pcSearchText))	count++;
	if (pGraphNode->bForceEndFreeze			&& strstri("ForceEndFreeze",		pcSearchText))	count++;
	if (pGraphNode->bOverrideAllBones		&& strstri("OverrideAllBones",		pcSearchText))	count++;
	if (pGraphNode->bSnapOverrideAllBones	&& strstri("SnapOverrideAllBones",	pcSearchText))	count++;
	if (pGraphNode->bOverrideMovement		&& strstri("OverrideMovement",		pcSearchText))	count++;
	if (pGraphNode->bSnapOverrideMovement	&& strstri("SnapOverrideMovement",	pcSearchText))	count++;
	if (pGraphNode->bOverrideDefaultMove	&& strstri("OverrideDefaultMove",	pcSearchText))	count++;
	if (pGraphNode->bSnapOverrideDefaultMove&& strstri("SnapOverrideDefaultMove",pcSearchText))	count++;

	FOR_EACH_IN_EARRAY(pGraphNode->eaFxEvent, DynAnimGraphFxEvent, pFx) {
		count += dynAnimGraphFxEventGetSearchStringCount(pFx, pcSearchText);
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pGraphNode->eaGraphImpact, DynAnimGraphTriggerImpact, pGraphImpact) {
		count += dynAnimGraphTriggerImpactGetSearchStringCount(pGraphImpact, pcSearchText);
	} FOR_EACH_END;

	return count;
}

int dynAnimGraphGetSearchStringCount(const DynAnimGraph *pGraph, const char *pcSearchText)
{
	int count = 0;

	if (pGraph->pcName		&& strstri(pGraph->pcName,		pcSearchText))	count++;
	if (pGraph->pcFilename	&& strstri(pGraph->pcFilename,	pcSearchText))	count++;
	if (pGraph->pcComments	&& strstri(pGraph->pcComments,	pcSearchText))	count++;
	if (pGraph->pcScope		&& strstri(pGraph->pcScope,		pcSearchText))	count++;

	if (REF_HANDLE_IS_ACTIVE(pGraph->hTemplate) && strstri(REF_HANDLE_GET_STRING(pGraph->hTemplate), pcSearchText)) count++;

	//partial graph ignored

	FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pGraphNode) {
		count += dynAnimGraphNodeGetSearchStringCount(pGraphNode, pcSearchText);
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pGraph->eaSuppress, DynAnimGraphSuppress, pSuppress) {
		count += dynAnimGraphSuppressGetSearchStringCount(pSuppress, pcSearchText);
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pGraph->eaStance, DynAnimGraphStance, pStance) {
		count += dynAnimGraphStanceGetSearchStringCount(pStance, pcSearchText);
	} FOR_EACH_END;

	//timeout ignored
	//disable torso pointing rate ignored

	if (pGraph->bOverrideAllBones		&& strstri("OverrideAllBones",		pcSearchText))	count++;
	if (pGraph->bSnapOverrideAllBones	&& strstri("SnapOverrideAllBones",	pcSearchText))	count++;
	if (pGraph->bOverrideMovement		&& strstri("OverrideMovement",		pcSearchText))	count++;
	if (pGraph->bSnapOverrideMovement	&& strstri("SnapOverrideMovement",	pcSearchText))	count++;
	if (pGraph->bOverrideDefaultMove	&& strstri("OverrideDefaultMove",	pcSearchText))	count++;
	if (pGraph->bSnapOverrideDefaultMove&& strstri("SnapOverrideDefaultMove",pcSearchText))	count++;

	if (pGraph->bForceVisible	&& strstri("ForceVisible",	pcSearchText)) count++;
	if (pGraph->bPitchToTarget	&& strstri("PitchToTarget",	pcSearchText)) count++;
	if (pGraph->bDisableTorsoPointing && strstri("DisableTorsoPointing", pcSearchText)) count++;
	if (pGraph->bGeneratePowerMovementInfo && strstri("GeneratePowerMovementInfo", pcSearchText)) count++;
	if (pGraph->bResetWhenMovementStops && strstri("ResetWhenMovementStops", pcSearchText)) count++;

	FOR_EACH_IN_EARRAY(pGraph->eaOnEnterFxEvent, DynAnimGraphFxEvent, pFx) {
		count += dynAnimGraphFxEventGetSearchStringCount(pFx, pcSearchText);
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pGraph->eaOnExitFxEvent, DynAnimGraphFxEvent, pFx) {
		count += dynAnimGraphFxEventGetSearchStringCount(pFx, pcSearchText);
	} FOR_EACH_END;

	return count;
}

#include "dynAnimGraph_h_ast.c"