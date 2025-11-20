#include "dynAnimGraphPub.h"

#include "globaltypes.h"
#include "StringCache.h"

#include "dynFxManager.h"
#include "dynAnimTemplate.h"
#include "dynAnimGraph.h"
#include "dynAnimGraphUpdater.h"
#include "dynSkeleton.h"
#include "dynSkeletonMovement.h"
#include "dynMoveTransition.h"
#include "dynAnimChart.h"

NameList *dynAnimKeywordList;
NameList *dynAnimStanceList;
NameList *dynAnimFlagList;

const DynAnimChartStack* dynAnimGraphUpdaterGetChartStack(const DynAnimGraphUpdater* pUpdater)
{
	if (pUpdater)
		return pUpdater->pChartStack;
	return NULL;
}

S32 dynAnimGraphUpdaterGetChartStackSize(const DynAnimGraphUpdater *pUpdater)
{
	if (SAFE_MEMBER(pUpdater,pChartStack))
		return eaSize(&pUpdater->pChartStack->eaChartStack);
	return 0;
}

const DynAnimChartRunTime *dynAnimGraphUpdaterGetChartStackChart(const DynAnimGraphUpdater *pUpdater, S32 i)
{
	if (0 <= i &&
		SAFE_MEMBER(pUpdater,pChartStack) &&
		i < eaSize(&pUpdater->pChartStack->eaChartStack))
	{
		return pUpdater->pChartStack->eaChartStack[i];
	}
	return NULL;
}

const char* dynAnimGraphUpdaterGetCurrentGraphName(DynAnimGraphUpdater* pUpdater)
{
	if (pUpdater && pUpdater->pCurrentGraph)
		return pUpdater->pCurrentGraph->pcName;
	return "None";
}

DynAnimGraphUpdaterNode* dynAnimGraphUpdaterGetCurrentNode(DynAnimGraphUpdater* pUpdater)
{
	if (pUpdater)
		return &pUpdater->nodes[0];
	return NULL;
}

DynAnimGraphUpdaterNode* dynAnimGraphUpdaterGetNode(DynAnimGraphUpdater* pUpdater, int index)
{
	if (pUpdater && index >= 0 && index < ARRAY_SIZE(pUpdater->nodes))
		return &pUpdater->nodes[index];
	return NULL;
}

bool dynAnimGraphUpdaterIsOnADefaultGraph(const DynAnimGraphUpdater* pUpdater)
{
	if (pUpdater)
		return pUpdater->bOnDefaultGraph;
	return true;
}

bool dynAnimGraphUpdaterIsOnADeathGraph(const DynAnimGraphUpdater *pUpdater)
{

	if (pUpdater && pUpdater->pCurrentGraph)
	{
		DynAnimTemplate *curTemp;
		if (curTemp = GET_REF(pUpdater->pCurrentGraph->hTemplate))
		{
			return (curTemp->eType == eAnimTemplateType_Death);
		}
	}
	return false;
}

bool dynAnimGraphUpdaterIsInPostIdle(const DynAnimGraphUpdater* pUpdater)
{
	if (pUpdater)
		return pUpdater->bInPostIdle;
	return false;
}

bool dynAnimGraphUpdaterIsOverlay(const DynAnimGraphUpdater *pUpdater)
{
	if (pUpdater)
		return pUpdater->bOverlay;
	return false;
}

F32 dynAnimGraphUpdaterGetOverlayBlend(const DynAnimGraphUpdater *pUpdater)
{
	if (pUpdater)
		return pUpdater->fOverlayBlend;
	return 0.0;
}

const char *dynAnimGraphNodeGetName(const DynAnimGraphUpdaterNode *pNode)
{
	if (pNode && pNode->pGraphNode)
		return pNode->pGraphNode->pcName;
	return "NONE";
}

const char *dynAnimGraphNodeGetGraphName(const DynAnimGraphUpdaterNode *pNode)
{
	if (pNode && pNode->pGraph)
		return pNode->pGraph->pcName;
	return "NONE";
}

const char* dynAnimGraphNodeGetMoveName(const DynAnimGraphUpdaterNode* pNode)
{
	const char* name = NULL;
	if (pNode && pNode->pMove_debug)
		name = REF_STRING_FROM_HANDLE(pNode->pMove_debug->hMove);
	return FIRST_IF_SET(name, "NONE");
}

F32 dynAnimGraphNodeGetFrameTime(const DynAnimGraphUpdaterNode* pNode)
{
	if (pNode)
		return pNode->fTime;
	return -1.0f;
}

F32 dynAnimGraphNodeGetMoveTotalTime(const DynAnimGraphUpdaterNode* pNode)
{
	if(SAFE_MEMBER(pNode, pMoveSeq)){
		return pNode->pMoveSeq->fLength;
	}
	return -1.0f;
}

F32 dynAnimGraphNodeGetBlendFactor(const DynAnimGraphUpdaterNode* pNode)
{
	if (pNode)
		return pNode->fBlendFactor;
	return -1.0f;
}

F32 dynAnimGraphNodeGetBlendTime(const DynAnimGraphUpdaterNode *pNode)
{
	if (pNode)
		return pNode->fTimeOnBlend;
	return -1.0f;
}

F32 dynAnimGraphNodeGetBlendTotalTime(const DynAnimGraphUpdaterNode *pNode)
{
	if (pNode && SAFE_MEMBER(pNode, pGraphNode))
		return pNode->pGraphNode->interpBlock.fInterpolation;
	return -1.0f;
}

const char* dynAnimGraphNodeGetReason(const DynAnimGraphUpdaterNode* pNode)
{
	return FIRST_IF_SET(SAFE_MEMBER(pNode, pcReason), "unknown");
}

const char *dynAnimGraphNodeGetReasonDetails(const DynAnimGraphUpdaterNode *pNode)
{
	return FIRST_IF_SET(SAFE_MEMBER(pNode, pcReasonDetails), "unknown");
}

// Executes a keyword
AUTO_COMMAND ACMD_CATEGORY(dynAnimation);
const char *danimStartGraph(ACMD_NAMELIST(dynAnimKeywordList) const char* pcGraph)
{
	if (gConf.bNewAnimationSystem && dynDebugState.pDebugSkeleton)
	{
		if (!dynSkeletonStartGraph(dynDebugState.pDebugSkeleton, allocFindString(pcGraph), 0))
		{
			// need some kind of logging?
			return "Could not execute keyword - keyword not found in the active charts, or could not interrupt.";
		}
	}
	return "";
}

AUTO_COMMAND ACMD_CATEGORY(dynAnimation);
void danimResetToIdle(void)
{
	if (gConf.bNewAnimationSystem && dynDebugState.pDebugSkeleton)
	{
		dynSkeletonResetToADefaultGraph(dynDebugState.pDebugSkeleton, "danimResetToIdle", 0, 1);
	}
}

AUTO_COMMAND ACMD_CATEGORY(dynAnimation);
void danimSetFlag(ACMD_NAMELIST(dynAnimFlagList) const char* pcFlag)
{
	if (gConf.bNewAnimationSystem && dynDebugState.pDebugSkeleton)
	{
		dynSkeletonSetFlag(dynDebugState.pDebugSkeleton, pcFlag, 0);
	}
}

const DynMoveTransition *dynMovementBlockGetTransition(const DynMovementBlock *pBlock)
{
	return pBlock->pTransition;
}

const char* dynMovementBlockGetMovementType(const DynMovementBlock* pBlock)
{
	return pBlock->pMovementSequence->pcMovementType;
}

const char* dynMovementBlockGetMoveName(const DynMovementBlock* pBlock)
{
	return pBlock->inTransition ? 
				pBlock->pMoveSeqTransition->pDynMove->pcName :
				pBlock->pMoveSeqCycle->pDynMove->pcName;
}

F32 dynMovementBlockGetBlendFactor(const DynMovementBlock* pBlock)
{
	return pBlock->fBlendFactor;
}

F32 dynMovementBlockGetFrameTime(const DynMovementBlock* pBlock)
{
	return pBlock->fFrameTime;
}

F32 dynMovementBlockGetTotalTime(const DynMovementBlock* pBlock)
{
	return pBlock->inTransition ? 
				pBlock->pMoveSeqTransition->fLength:
				pBlock->pMoveSeqCycle->fLength;
}

S32 dynMovementBlockIsInTransition(const DynMovementBlock* pBlock)
{
	return pBlock->inTransition;
}

const DynAnimChartRunTime* dynMovementBlockGetChart(const DynMovementBlock* pBlock)
{
	return pBlock->pChart;
}

void dynMovementBlockGetTransitionString(	const DynMoveTransition *pTran,
											char* buffer,
											U32 bufferSize)
{
	const DynMoveTransition*			t = pTran;
	char								swSource[200];
	char								swTarget[200];
	char								mtSource[200];
	char								mtTarget[200];

	if(!bufferSize){
		return;
	}

	buffer[0] = 0;

	if(!t){
		return;
	}

	swSource[0] = 0;
	swTarget[0] = 0;
	mtSource[0] = 0;
	mtTarget[0] = 0;

	dynAnimGetStanceWordsString(t->eaStanceWordsSource, t->eaTimedStancesSource, "+", SAFESTR(swSource));
	dynAnimGetStanceWordsString(t->eaStanceWordsTarget, t->eaTimedStancesTarget, "+", SAFESTR(swTarget));
	dynAnimGetStanceWordsString(t->eaMovementTypesSource, NULL, "|", SAFESTR(mtTarget));
	dynAnimGetStanceWordsString(t->eaMovementTypesTarget, NULL, "|", SAFESTR(mtTarget));

	strcatf_s(	buffer,
				bufferSize,
				"%s: source(%s / %s) --> target(%s / %s)",
				t->pcName,
				swSource[0] ? swSource : "any",
				mtSource[0] ? mtSource : "any",
				swTarget[0] ? swTarget : "any",
				mtTarget[0] ? mtTarget : "any");
}

const DynMoveTransition *dynMoveTransitionGet(const char *pcName)
{
	return RefSystem_ReferentFromString(hMoveTransitionDict, pcName);
}

const DynAnimChartRunTime *dynAnimChartGet(const char *pcName)
{
	return RefSystem_ReferentFromString(hAnimChartDictRunTime, pcName);
}

const char* dynAnimChartGetName(const DynAnimChartRunTime* pChart)
{
	return pChart->pcName;
}

F32 dynAnimChartGetPriority(const DynAnimChartRunTime* pChart)
{
	return pChart->fChartPriority;
}

void dynAnimGetStanceWordsString(	const char*const* eaStanceWords,
									const DynAnimTimedStance*const* eaTimedStances,
									const char* separator,
									char* buffer,
									U32 bufferSize)
{
	if(!bufferSize){
		return;
	}

	buffer[0] = 0;

	EARRAY_CONST_FOREACH_BEGIN(eaStanceWords, i, isize);
		strcatf_s(	buffer,
					bufferSize,
					"%s%s",
					i ? separator : "",
					eaStanceWords[i]);
	EARRAY_FOREACH_END;

	if (eaSize(&eaStanceWords) &&
		eaSize(&eaTimedStances)) {
		strcatf_s(	buffer,
					bufferSize,
					"+");
	}

	EARRAY_CONST_FOREACH_BEGIN(eaTimedStances, i, isize);
	strcatf_s(	buffer,
				bufferSize,
				"%s%s",
				i ? separator : "",
				eaTimedStances[i]->pcName);
	EARRAY_FOREACH_END;
}

void dynAnimChartGetStanceWords(const DynAnimChartRunTime* pChart,
								char* buffer,
								U32 bufferSize)
{
	if (pChart) {
		dynAnimGetStanceWordsString(pChart->eaStanceWords, NULL, "+", buffer, bufferSize);
	}
}