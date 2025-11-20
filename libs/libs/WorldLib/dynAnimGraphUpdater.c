
#include "dynAnimGraphUpdater.h"

#include "EArray.h"
#include "MemoryPool.h"
#include "rand.h"
#include "StringCache.h"
#include "utils.h"
#include "windefinclude.h"

#include "dynAnimChart.h"
#include "dynAnimGraph.h"
#include "dynAnimTemplate.h"
#include "dynAnimTrack.h"
#include "dynFxManager.h"
#include "dynMove.h"
#include "dynNodeInline.h"
#include "dynSkeleton.h"
#include "dynSkeletonMovement.h"

#include "wlCostume.h"
#include "wlState.h"

#define DYNANIMGRAPHUPDATER_DEBUG_PRINTANIMWORDS	0

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

MP_DEFINE(DynAnimGraphUpdater);

AUTO_RUN;
void initDynAnimGraphUpdaterPools(void)
{
	MP_CREATE(DynAnimGraphUpdater, 128);
}

extern SkelBoneVisibilitySets g_SkelBoneVisSets;
static const char* s_pchFlagActivate;
static const char* s_pchFlagDeactivate;
static const char* s_pchFlagInterrupt;
static const char* s_pchFlagHitFront;
static const char* s_pchFlagHitRear;
static const char* s_pchFlagHitLeft;
static const char* s_pchFlagHitRight;
static const char* s_pchFlagHitTop;
static const char* s_pchFlagHitBottom;
static const char* s_pchStanceFlagLanded;

AUTO_RUN;
void initDynAnimGraphStrings(void)
{
	s_pchFlagActivate	= allocAddStaticString("Activate");
	s_pchFlagDeactivate = allocAddStaticString("Deactivate");
	s_pchFlagInterrupt	= allocAddStaticString("Interrupt");

	//using non-static strings so the string cache won't error
	//about upper/lower case letters with the anim bit registry version
	s_pchFlagHitFront	= allocAddString("HitFront");
	s_pchFlagHitRear	= allocAddString("HitRear");
	s_pchFlagHitLeft	= allocAddString("HitLeft");
	s_pchFlagHitRight	= allocAddString("HitRight");
	s_pchFlagHitTop		= allocAddString("HitTop");
	s_pchFlagHitBottom	= allocAddString("HitBottom");
	s_pchStanceFlagLanded = allocAddString("Landed");
}


DynAnimGraphUpdater* dynAnimGraphUpdaterCreate(	DynSkeleton *pSkeleton,
												DynAnimChartStack *pChartStack,
												bool bMovement,
												bool bOverlay)
{
	DynAnimGraphUpdater* pUpdater = MP_ALLOC(DynAnimGraphUpdater);
	assert(pChartStack);
	pChartStack->bMovement |= bMovement;
	pUpdater->pChartStack = pChartStack;
	pUpdater->bMovement = bMovement;
	pUpdater->bOverlay = bOverlay;
	return pUpdater;
}

void dynAnimGraphUpdaterReset(DynSkeleton *pSkeleton, DynAnimGraphUpdater *pUpdater)
{
	int i;

	for (i = 0; i < MAX_UPDATER_NODES; i++) {
		pUpdater->nodes[i].fBlendFactorMutable	= 0;
		pUpdater->nodes[i].fTimeOnBlendMutable	= 0;
		pUpdater->nodes[i].fTimeMutable			= 0;
		pUpdater->nodes[i].pGraphNodeMutable	= NULL;
		pUpdater->nodes[i].pGraphMutable		= NULL;
		pUpdater->nodes[i].pMove_debug	= NULL;
		pUpdater->nodes[i].pMoveSeq		= NULL;
		pUpdater->nodes[i].pcSetFlag	= NULL;
	}

	pUpdater->pCurrentGraphMutable	= NULL;
	pUpdater->pLastGraph			= NULL;
	pUpdater->fTimeOnCurrentGraph	= 0.0;

	if (pUpdater->bOverlay)
		pUpdater->fOverlayBlend = 1.0;
	else
		pUpdater->fOverlayBlend = 0.0;

	pUpdater->bOnDefaultGraph = false;
	pUpdater->bInPostIdle = false;
	pUpdater->bInDefaultPostIdle = false;
	pUpdater->bInTPose = false;
	pUpdater->bForceLoopCurrentGraph = false;
	pUpdater->bLooped = false;
	pUpdater->bResetToDefaultWasAttempted = false;
	pUpdater->bResetToDefaultOnChartStackChange = false;

	dynAnimGraphUpdaterResetToADefaultGraph(pSkeleton, pUpdater, NULL, "updater reset", 0, 0, 0);
}

void dynAnimGraphUpdaterDestroy(DynAnimGraphUpdater* pUpdater)
{
	MP_FREE(DynAnimGraphUpdater, pUpdater);
}

static void dynAnimGraphUpdaterNodeAdvanceBlendFactor(	DynAnimGraphUpdaterNode* pNode,
														DynAnimGraphUpdaterNode* pPrevNode,
														F32 fTimeDiff)
{
	if (pNode->pGraphNode->interpBlock.fInterpolation > 0.0f &&
		(	!pNode->pGraphNode->bNoSelfInterp ||
			pNode->pGraphNode != pPrevNode->pGraphNode)
		)
	{
		F32 fBlendDiff = fTimeDiff / pNode->pGraphNode->interpBlock.fInterpolation;

		pNode->fBlendFactorMutable += fBlendDiff;
		if (pNode->fBlendFactor > 1.0f)
		{
			pNode->fBlendFactorMutable = 1.0f;
			pNode->fTimeOnBlendMutable = pNode->pGraphNode->interpBlock.fInterpolation;
		}
		else
			pNode->fTimeOnBlendMutable += fTimeDiff;
	}else{
		pNode->fBlendFactorMutable = 1.0f;
	}
}

static void dynAnimGraphUpdaterSetPostIdleCallerStances(DynAnimGraphUpdater *pUpdater)
{
	DynAnimTemplate *pTemplate = GET_REF(pUpdater->pCurrentGraph->hTemplate);
	static const char *pcInCombat = NULL;

	//note that the incombat stance is not always set ontime during someone's initial attack
	if (!pcInCombat) {
		pcInCombat = allocAddString("InCombat");
	}
	
	eaClear(&pUpdater->eaPostIdleCallerStances);

	if (pTemplate->eType == eAnimTemplateType_Power)
	{
		bool bAppend = true;
		
		if (pUpdater->pChartStack)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(pUpdater->pChartStack->eaStanceWords, const char, pcAddStance)
			{
				if (bAppend) {
					S32 iCompare = dynAnimCompareStanceWordPriority(&pcAddStance, &pcInCombat);
					if ( 0 < iCompare) {
						eaPush(&pUpdater->eaPostIdleCallerStances, pcInCombat);
						bAppend = false;
					} else if (0 == iCompare) {
						bAppend = false;
					}
				}
				eaPush(&pUpdater->eaPostIdleCallerStances, pcAddStance);
			}
			FOR_EACH_END;
		}

		if (bAppend) {
			eaPush(&pUpdater->eaPostIdleCallerStances, pcInCombat);
		}
	}
	else if (pUpdater->pChartStack)
	{
		eaPushEArray(&pUpdater->eaPostIdleCallerStances, &pUpdater->pChartStack->eaStanceWords);
	}
	// else we've already cleared out eaPostIdleCallerStances above
}

static void dynAnimGraphDoFxEvent(DynSkeleton *pSkeleton, DynAnimGraphUpdater *pUpdater, DynFxManager *pFxManager, const char *pcFxName, bool bIsMessage)
{
	if (pFxManager) // pFxManager == NULL happens in the editor
	{
		DynSkeletonCachedFxEvent*** peaUseCachedFxEvent = pUpdater->bMovement ? &pSkeleton->eaCachedSkelMoveFx : &pSkeleton->eaCachedAnimGraphFx;
		bool bUnique = true;

		//check for uniqueness
		//we don't want the lower body & upper body & overlays all sending the same FX / FX message such as death on glued up creatures (driders, centaurs, mermaids)
		//note that the cache is built and cleared every frame
		FOR_EACH_IN_EARRAY(*peaUseCachedFxEvent, DynSkeletonCachedFxEvent, pChkFxEvent) 
		{
			if (pChkFxEvent->pcName   == pcFxName &&
				pChkFxEvent->bMessage == bIsMessage)
			{
				bUnique = false;
				break;
			}
		}
		FOR_EACH_END;

		if (bUnique)
		{
			//queue fx onto skeleton for later testing against blend factors since we won't compute the blends until after the graphs and movement have been updated
			DynSkeletonCachedFxEvent *pCachedFx = calloc(sizeof(DynSkeletonCachedFxEvent),1);
			pCachedFx->pcName   = pcFxName;
			pCachedFx->pUpdater = pUpdater;
			pCachedFx->bMessage = bIsMessage;
			eaPush(peaUseCachedFxEvent, pCachedFx);
		}
	}
	else if (pUpdater->pEditorFxManager)
	{
		if (bIsMessage) {
			dynFxManBroadcastMessage(pUpdater->pEditorFxManager, pcFxName);
		} else {
			//unsupported dynSkeletonQueueAnimationFx(pUpdater->pEditorFxManager, pcFxName);
		}

		if (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits)
		{
			dynDebugSkeletonStartGraphFX(pSkeleton, pUpdater, pcFxName, bIsMessage);
		}
	}
}

static void dynAnimGraphUpdatedNodeTriggerFxEvents(DynSkeleton *pSkeleton, DynAnimGraphUpdater *pUpdater, DynAnimGraphUpdaterNode* pNode, const DynAnimGraphMove *pGraphMove, const DynMoveSeq *pMoveSeq, F32 fOldTime)
{
	FOR_EACH_IN_EARRAY(pNode->pGraphNode->eaFxEvent, DynAnimGraphFxEvent, pFxEvent)
	{
		if (fOldTime <= pFxEvent->iFrame && pNode->fTime > pFxEvent->iFrame) {
			dynAnimGraphDoFxEvent(pSkeleton, pUpdater, pSkeleton->pFxManager, pFxEvent->pcFx, pFxEvent->bMessage);
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pGraphMove->eaFxEvent, DynAnimGraphFxEvent, pGraphMoveFxEvent)
	{
		if (fOldTime <= pGraphMoveFxEvent->iFrame && pNode->fTime > pGraphMoveFxEvent->iFrame) {
			dynAnimGraphDoFxEvent(pSkeleton, pUpdater, pSkeleton->pFxManager, pGraphMoveFxEvent->pcFx, pGraphMoveFxEvent->bMessage);
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pMoveSeq->eaDynMoveFxEvents, DynMoveFxEvent, pMoveFxEvent)
	{
		if (fOldTime <= pMoveFxEvent->uiFrameTrigger && pMoveFxEvent->uiFrameTrigger < pNode->fTime) {
			dynAnimGraphDoFxEvent(pSkeleton, pUpdater, pSkeleton->pFxManager, pMoveFxEvent->pcFx, pMoveFxEvent->bMessage);
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pNode->pGraphNode->eaGraphImpact, DynAnimGraphTriggerImpact, pImpact)
	{
		if (fOldTime <= pImpact->iFrame && pNode->fTime > pImpact->iFrame)
		{
			// Trigger impact
			if (pSkeleton->uiEntRef)
			{
				const DynNode* pBone = dynSkeletonFindNode(pSkeleton, pImpact->pcBone);
				Quat qRootRot;
				dynNodeGetWorldSpaceRot(pSkeleton->pFacespaceNode, qRootRot);
				if (pBone)
				{
					DynTransform xBone;
					Vec3 vImpactDir;
					Vec3 vOffset;
					Vec3 vBonePos;
					static const Vec3 impact_direction[] = {
						{  0,  0,  1}, // DAGID_Front_to_Back
						{  0,  0, -1}, // DAGID_Back_to_Front
						{ -1,  0,  0}, // DAGID_Right_to_Left
						{  1,  0,  0}, // DAGID_Left_to_Right
						{  0, -1,  0}, // DAGID_Up_to_Down
						{  0,  1,  0}, // DAGID_Down_to_Up
					};
					dynNodeGetWorldSpaceTransform(pBone, &xBone);
					quatRotateVec3Inline(qRootRot, impact_direction[pImpact->eDirection], vImpactDir);
					quatRotateVec3Inline(xBone.qRot, pImpact->vOffset, vOffset);

					addVec3(xBone.vPos, vOffset, vBonePos);
					dynSkeletonQueueAnimReactTrigger(vBonePos, vImpactDir, pSkeleton, pUpdater->uidCurrentGraph);
				}
			}
		}
	}
	FOR_EACH_END;
}

static const DynMoveSeq* dynAnimGraphUpdaterAdvanceNode(DynSkeleton *pSkeleton, DynAnimGraphUpdater* pUpdater, const SkelInfo *pSkelInfo);
static const DynMoveSeq* dynAnimGraphUpdaterAdvanceNodeWithinGraph(	DynSkeleton *pSkeleton,
																	DynAnimGraphUpdater* pUpdater,
																	const DynAnimGraph* pGraph,
																	const DynAnimGraphNode* pGraphNode,
																	const SkelInfo *pSkelInfo,
																	const char *pcReason,
																	const char *pcReasonDetails)
{
	const DynAnimGraphMove *pGraphMove;
	DynMove* pMove;
	DynAnimGraphUpdaterNode* pCurrentNode = &pUpdater->nodes[0];
	SkelBoneVisibilitySet eCurrentVisSet = -1;
	
	if (pCurrentNode && pCurrentNode->pGraphNode)
		eCurrentVisSet = pCurrentNode->pGraphNode->eVisSet;

	glrLog(	pUpdater->glr,
			"[dyn.AGU] %s: start node [%s::%s] from [%s::%s::%s].",
			__FUNCTION__,
			pGraph->pcName,
			pGraphNode->pcName,
			SAFE_MEMBER(pCurrentNode->pGraph, pcName),
			SAFE_MEMBER(pCurrentNode->pGraphNode, pcName),
			SAFE_MEMBER(pCurrentNode->pMoveSeq, pDynMove->pcName));

	if(	pUpdater->nodes[0].pGraphNode &&
		!dynAnimGraphOnPathRandomizer(pUpdater->nodes[0].pGraphNode))
	{
		CopyStructsFromOffset(	pUpdater->nodes + 1,
								-1,
								ARRAY_SIZE(pUpdater->nodes)-1);
	}

	pCurrentNode->pGraphMutable = pGraph;
	pCurrentNode->pGraphNodeMutable = pGraphNode;
	pCurrentNode->fTimeMutable = 0.0f;
	pCurrentNode->fBlendFactorMutable = 0.0f;
	pCurrentNode->fTimeOnBlendMutable = 0.0f;
	pCurrentNode->pcSetFlag = NULL;
	pCurrentNode->pcReason = pcReason;
	pCurrentNode->pcReasonDetails = pcReasonDetails;

	if (dynAnimGraphOnPathRandomizer(pGraphNode))
	{
		return dynAnimGraphUpdaterAdvanceNode(pSkeleton, pUpdater, pSkelInfo);
	}

	if (pGraphMove = dynAnimGraphNodeChooseMove(pGraph, pGraphNode, pCurrentNode->pMove_debug))
	{
		F32 fTimeStartOffset = 0.0f;

		//set the move seq
		pCurrentNode->pMove_debug = pGraphMove;
		pMove = GET_REF(pGraphMove->hMove);
		pCurrentNode->pMoveSeq = pMove?dynMoveSeqFromDynMove(pMove, pSkelInfo):NULL;

		if (pCurrentNode->pMoveSeq) {
			//determine any frame offset (used for de-syncing the frame on idle graph startups across multiple characters)
			if (pUpdater->nodes[0].pMoveSeq != pUpdater->nodes[1].pMoveSeq) {
				fTimeStartOffset = dynMoveSeqGetStartOffset(pCurrentNode->pMoveSeq);
				pCurrentNode->fTimeMutable += fTimeStartOffset;
			}

			//determine the playback speed (used for de-syncing the playback rate across multiple characters)
			pCurrentNode->fPlaybackSpeedMutable = dynMoveSeqGetRandPlaybackSpeed(pCurrentNode->pMoveSeq);
		}

		if (pUpdater->nodes[1].pGraphNode) // for initialization
		{
			dynAnimGraphUpdaterNodeAdvanceBlendFactor(	pCurrentNode,
														&pUpdater->nodes[1],
														pCurrentNode->fTime - fTimeStartOffset);
		}else{
			pCurrentNode->fBlendFactorMutable = 1.0f;
		}

		if (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits) {
			dynDebugSkeletonStartNode(pSkeleton, pUpdater, pCurrentNode);
		}

		if (eCurrentVisSet != pCurrentNode->pGraphNode->eVisSet)
			dynSkeletonUpdateBoneVisibility(pSkeleton, pCurrentNode->pGraphNode);

		return pCurrentNode->pMoveSeq; 
	}
	else
	{
		const DynMoveSeq *pNext = dynAnimGraphUpdaterAdvanceNode(pSkeleton, pUpdater, pSkelInfo);
		pCurrentNode->fBlendFactorMutable = 1.0f;

		if (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits) {
			dynDebugSkeletonStartNode(pSkeleton, pUpdater, pCurrentNode);
		}

		return pNext;
	}
}

static const DynMoveSeq* dynAnimGraphUpdaterAdvanceNode(DynSkeleton *pSkeleton,
														DynAnimGraphUpdater* pUpdater,
														const SkelInfo *pSkelInfo)
{
	DynAnimGraphUpdaterNode* pCurrentNode = &pUpdater->nodes[0];
	const DynAnimGraph* pGraph = pUpdater->pCurrentGraph;
	const DynMoveSeq *pMoveSeq = NULL;
	bool bCachedNextNodeIsEnd;
	F32 fFate;

	// This function might be called a ton of times because of 1-frame looping
	//   animations (overlay sequencer idle animation) - this could be optimized if needed.

	PERFINFO_AUTO_START_FUNC();

	glrLog(	pUpdater->glr,
			"[dyn.AGU] %s: from node %s.",
			__FUNCTION__,
			pCurrentNode->pGraphNode->pTemplateNode->pcName);

	//pre-determine the fate of any randomizer node
	if (dynAnimGraphOnPathRandomizer(pCurrentNode->pGraphNode))
		fFate = randomPositiveF32();
	else
		fFate = -1.0f;

	bCachedNextNodeIsEnd = dynAnimGraphNextNodeIsEndFated(	pGraph,
															pCurrentNode->pGraphNode,
															pCurrentNode->pcSetFlag,
															fFate);

	if(	!pUpdater->bForceLoopCurrentGraph && bCachedNextNodeIsEnd)
	{
		//Goto a post-idle or default graph
		const DynAnimGraph *pPostIdleGraph = dynAnimGraphGetPostIdleFated(	pGraph,
																			pCurrentNode->pGraphNode,
																			pCurrentNode->pcSetFlag,
																			fFate);
		dynAnimGraphUpdaterResetToADefaultGraph(	pSkeleton,
													pUpdater,
													pPostIdleGraph,
													"graph ended",
													0,
													1,
													DAGUI_GRAPHEND );
		pMoveSeq = pCurrentNode->pMoveSeq;
	}
	else 
	{
		// We stay on the same graph, either because we are forced to loop it (editors)
		//	or because it's not the end of the graph yet.
		// These next 3 lines are just to record when the animation has 'looped'
		//	for time keeping purposes in the editor.

		const DynAnimGraphNode* pNewNode;
		const char *pcResetFlag = NULL;

		if (pUpdater->bForceLoopCurrentGraph && bCachedNextNodeIsEnd)
		{
			pcResetFlag = pUpdater->pcForceLoopFlag;
			pUpdater->fTimeOnCurrentGraph = 0.0f;
			eaClear(&pUpdater->eaFlagQueue);
			if (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits) {
				dynDebugSkeletonClearFlags(pSkeleton, pUpdater);
			}
		}

		// Advance to the next node within the same graph.
		pNewNode = dynAnimGraphNodeGetNextNodeFated(	pGraph,
														pCurrentNode->pGraphNode,
														pCurrentNode->pcSetFlag,
														pcResetFlag,
														fFate);

		if (pcResetFlag) {
			pCurrentNode->pcSetFlag = pcResetFlag;
		}

		glrLog(	pUpdater->glr,
				"[dyn.AGU] %s: to node %s.",
				__FUNCTION__,
				pNewNode->pcName);

		pMoveSeq = dynAnimGraphUpdaterAdvanceNodeWithinGraph(	pSkeleton,
																pUpdater,
																pGraph,
																pNewNode,
																pSkelInfo,
																"next node",
																"");
	}

	PERFINFO_AUTO_STOP();
	return pMoveSeq;
}

void dynAnimGraphUpdaterSignalMovementStopped(DynSkeleton *pSkeleton, DynAnimGraphUpdater *pUpdater, bool bMovementStopped)
{
	if (bMovementStopped &&
		pUpdater->bOnDefaultGraph &&
		!pUpdater->bResetToDefaultOnChartStackChange &&
		SAFE_MEMBER(pUpdater->pCurrentGraph,bResetWhenMovementStops))
	{
		dynAnimGraphUpdaterResetToADefaultGraph(pSkeleton, pUpdater, NULL, "movement stopped", 0, 1, 0);
	}

	pUpdater->bResetToDefaultOnChartStackChange = 0;
}

void dynAnimGraphUpdaterDoEnterFX(DynSkeleton *pSkeleton, DynAnimGraphUpdater *pUpdater, bool bInternal, F32 fNewBlend, F32 fOldBlend)
{
	FOR_EACH_IN_EARRAY_FORWARDS(pUpdater->pCurrentGraph->eaOnEnterFxEvent, DynAnimGraphFxEvent, pFxEvent)
	{
		if (bInternal && (!pFxEvent->bMovementBlendTriggered || fNewBlend < pFxEvent->fMovementBlendTrigger) ||
			pFxEvent->bMovementBlendTriggered && fNewBlend < pFxEvent->fMovementBlendTrigger && pFxEvent->fMovementBlendTrigger <= fOldBlend)
		{
			glrLog(pUpdater->glr, "[dyn.AGU] OnEnter FX event: %s.", pFxEvent->pcFx);
			dynAnimGraphDoFxEvent(pSkeleton, pUpdater, pSkeleton?pSkeleton->pFxManager:NULL, pFxEvent->pcFx, pFxEvent->bMessage);
		}
	}
	FOR_EACH_END;
}

void dynAnimGraphUpdaterDoExitFX(DynSkeleton *pSkeleton, DynAnimGraphUpdater *pUpdater, bool bInternal, F32 fNewBlend, F32 fOldBlend)
{
	if (bInternal) {
		//use last graph
		FOR_EACH_IN_EARRAY_FORWARDS(pUpdater->pLastGraph->eaOnExitFxEvent, DynAnimGraphFxEvent, pFxEvent)
		{
			if (!pFxEvent->bMovementBlendTriggered || fNewBlend < pFxEvent->fMovementBlendTrigger)
			{
				glrLog(pUpdater->glr, "[dyn.AGU] OnExit FX event: %s.", pFxEvent->pcFx);
				dynAnimGraphDoFxEvent(pSkeleton, pUpdater, pSkeleton?pSkeleton->pFxManager:NULL, pFxEvent->pcFx, pFxEvent->bMessage);
			}
		}
		FOR_EACH_END;
	}
	else
	{
		//use current graph
		FOR_EACH_IN_EARRAY_FORWARDS(pUpdater->pCurrentGraph->eaOnExitFxEvent, DynAnimGraphFxEvent, pFxEvent)
		{
			if (pFxEvent->bMovementBlendTriggered && fOldBlend < pFxEvent->fMovementBlendTrigger && pFxEvent->fMovementBlendTrigger <= fNewBlend)
			{
				glrLog(pUpdater->glr, "[dyn.AGU] OnExit FX event: %s.", pFxEvent->pcFx);
				dynAnimGraphDoFxEvent(pSkeleton, pUpdater, pSkeleton?pSkeleton->pFxManager:NULL, pFxEvent->pcFx, pFxEvent->bMessage);
			}
		}
		FOR_EACH_END;
	}
}

static void dynAnimGraphUpdaterGraphChanged(DynSkeleton *pSkeleton,
											DynAnimGraphUpdater* pUpdater,
											const char *pcReason,
											const char* pcReasonDetails)
{
	const DynAnimGraph* pGraph = pUpdater->pCurrentGraph;
	const WLCostume* pCostume = pSkeleton?GET_REF(pSkeleton->hCostume):NULL;
	const SkelInfo* pSkelInfo = pCostume?GET_REF(pCostume->hSkelInfo):NULL;
	DynFxManager *pFxManager = pSkeleton?pSkeleton->pFxManager:NULL;

	glrLog(	pUpdater->glr,
			"[dyn.AGU] %s BEGIN.",
			__FUNCTION__);

	if (pUpdater->pLastGraph) {
		dynAnimGraphUpdaterDoExitFX(pSkeleton, pUpdater, true, pSkeleton->fMovementSystemOverrideFactor, -1);
	}
	pUpdater->pLastGraph = pGraph;
	dynAnimGraphUpdaterDoEnterFX(pSkeleton, pUpdater, true, pSkeleton->fMovementSystemOverrideFactor, -1);

	pUpdater->bOnDefaultGraph = 0; // we will set this to 1 AFTER this call if we just set it to the default graph
	pUpdater->bOnMovementGraph = SAFE_MEMBER(GET_REF(pGraph->hTemplate),eType) == eAnimTemplateType_Movement;
	pUpdater->bInTPose = 0;
	pUpdater->bIsPlayingDeathAnim = 0;
	if (pGraph)
	{
		const DynAnimTemplate *pNewTemplate;
		const char* pcFirstNodeName = NULL;
		const DynAnimGraphNode* pNewNode;

		pUpdater->fTimeOnLastGraph = pUpdater->fTimeOnCurrentGraph;
		pUpdater->fTimeOnCurrentGraph = 0.0f;

		pNewTemplate = SAFE_MEMBER(pUpdater,pCurrentGraph) ? GET_REF(pUpdater->pCurrentGraph->hTemplate) : NULL;

		if (pNewTemplate)
		{
			if (pNewTemplate->eType == eAnimTemplateType_TPose) {
				pUpdater->bInTPose = 1;
			} else if (pNewTemplate->eType == eAnimTemplateType_Death) {
				pUpdater->bIsPlayingDeathAnim = 1;
			}
		}

		eaClear(&pUpdater->eaFlagQueue);
		if (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits) {
			dynDebugSkeletonClearFlags(pSkeleton, pUpdater);
		}

		FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pNode)
		{
			if (pNode->pTemplateNode->eType == eAnimTemplateNodeType_Start)
			{
				if (eaSize(&pNode->pTemplateNode->eaSwitch) > 0)
					pUpdater->bOnMultiFlagStart = 1;
				else
					pcFirstNodeName = pNode->pTemplateNode->defaultNext.p->pcName;
			}
		}
		FOR_EACH_END;

		if (!pUpdater->bOnMultiFlagStart)
		{
			assert(pcFirstNodeName);

			pNewNode = dynAnimGraphFindNode(pGraph, pcFirstNodeName);
			dynAnimGraphUpdaterAdvanceNodeWithinGraph(pSkeleton, pUpdater, pGraph, pNewNode, pSkelInfo, pcReason, pcReasonDetails);

			if (!pUpdater->nodes[2].pGraph &&
				pUpdater->fTimeOnLastGraph < 30.0 &&
				SAFE_MEMBER(pNewTemplate,eType) == eAnimTemplateType_Death)
				{
					dynAnimGraphUpdaterUpdate(pUpdater, 10.0, pSkeleton, 1);
				}
		}
	}

	glrLog(	pUpdater->glr,
			"[dyn.AGU] %s END.",
			__FUNCTION__);
}

void dynAnimGraphUpdaterSetForceOverrideGraph(	DynSkeleton *pSkeleton,
												DynAnimGraphUpdater* pUpdater,
												DynAnimGraph* pForceOverrideGraph)
{
	glrLog(	pUpdater->glr,
			"[dyn.AGU] %s: %s: graph %s.",
			__FUNCTION__,
			pUpdater->pChartStack && eaSize(&pUpdater->pChartStack->eaChartStack) ?
						pUpdater->pChartStack->eaChartStack[0]->pcName :
						"no charts in stack",
					pForceOverrideGraph->pcName);

	pUpdater->pLastGraph = NULL;
	pUpdater->pCurrentChartMutable = NULL;
	pUpdater->pCurrentGraphMutable = pForceOverrideGraph;
	pUpdater->bForceLoopCurrentGraph = true;

	ZeroArray(pUpdater->nodes);
	dynAnimGraphUpdaterGraphChanged(pSkeleton, pUpdater, "force override", "");
	dynAnimGraphUpdaterSetPostIdleCallerStances(pUpdater);
}

// This can change throughout the function, so it's simpler to just macro it.
#define CURRENT_NODE (&pUpdater->nodes[0])

static bool dynAnimGraphUpdaterIsADefaultGraph(DynAnimGraphUpdater* pUpdater, bool bMovement);
static bool dynAnimGraphUpdaterIsADefaultGraphAtStackLayer(DynAnimGraphUpdater *pUpdater, int stackLayer, bool bMovement);
static bool dynAnimGraphUpdaterAllDefaultGraphsAreBlockTypes(DynAnimGraphUpdater *pUpdater);

static S32 disableInterrupts;
AUTO_CMD_INT(disableInterrupts, dynAnimGraphUpdaterDisableInterrupts);

static bool danimSentAGUInfLoopError = false;

void dynAnimGraphUpdaterUpdate(	DynAnimGraphUpdater* pUpdater,
								F32 fDeltaTime,
								DynSkeleton* pSkeleton,
								S32 disableRagdoll)
{
	DynAnimGraphUpdaterNode* pPreviousNode = &pUpdater->nodes[1];
	const DynAnimGraph* pGraph = pUpdater->pCurrentGraph;
	const WLCostume* pCostume = GET_REF(pSkeleton->hCostume);
	const SkelInfo* pSkelInfo = pCostume?GET_REF(pCostume->hSkelInfo):NULL;
	assert(pSkelInfo);

	if (pUpdater->bEnableOverrideTime &&
		pUpdater->fOverrideTime >= 0.f)
	{
		fDeltaTime += pUpdater->fOverrideTime;
		pUpdater->fOverrideTime = -1.f;
	}
	pUpdater->bEnableOverrideTime = false;

	if (TRUE_THEN_RESET(pUpdater->bOnMultiFlagStart))
	{
		const DynAnimTemplate *pTemplate = pGraph ? GET_REF(pGraph->hTemplate) : NULL;
		const DynAnimGraphNode *pStartNode, *pNewNode;
		const char *pcUseFlag = NULL;
		
		FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pNode) {
			if (pNode->pTemplateNode->eType == eAnimTemplateNodeType_Start)
				pStartNode = pNode;
		} FOR_EACH_END;
		
		FOR_EACH_IN_EARRAY_FORWARDS(pUpdater->eaFlagQueue, const char, pcFlag) {
			FOR_EACH_IN_EARRAY(pStartNode->pTemplateNode->eaSwitch, DynAnimTemplateSwitch, pSwitch) {
				if (pSwitch->pcFlag == pcFlag &&
					dynAnimGraphNodeIsValidForPlayback(pGraph, pSwitch->next.p))
				{
					pcUseFlag = pcFlag;
					break;
				}
			} FOR_EACH_END;
			if (pcUseFlag) {
				//eaRemove(&pUpdater->eaFlagQueue, ipcFlagIndex);
				//if (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits) {
				//	dynDebugSkeletonUseFlag(pSkeleton, pUpdater, pcUseFlag);
				//}
				break;
			}
		} FOR_EACH_END;

		pNewNode = dynAnimGraphNodeGetNextNodeFated(pGraph, pStartNode, pcUseFlag, NULL, -1.0f);
		dynAnimGraphUpdaterAdvanceNodeWithinGraph(pSkeleton, pUpdater, pGraph, pNewNode, pSkelInfo, "on multi-flag", "");

		if (!pUpdater->nodes[2].pGraph &&
			pUpdater->fTimeOnLastGraph < 30.0 &&
			SAFE_MEMBER(pTemplate,eType) == eAnimTemplateType_Death)
		{
			fDeltaTime += 10.f;
			disableRagdoll = 1;
		}
	}

	if (!pGraph || !CURRENT_NODE->pGraphNode)
		return;

	if(	pUpdater->bOverlay &&
		pUpdater->bInTPose &&
		pUpdater->fOverlayBlend == 0.0f)
	{
		return; 
	}

	if(pUpdater->glr){
		//const DynAnimGraph* gDefault;
		char stanceWords[1000];
		bool foundDefaultGraphs = false;
		int i;

		dynAnimChartStackSetFromStanceWords(pUpdater->pChartStack);
		dynAnimGraphUpdaterGetStanceWordsString(pUpdater, SAFESTR(stanceWords));

		glrLog(	pUpdater->glr,
				"[dyn.AGU] %s BEGIN: %s.\n"
				"Stances: %s\n"
				"Current graph node: [%s::%s]\n"
				"interruptingMovementStanceCount: %u\n"
				"In post-idle: %u\n"
				"Template node interruptible: %u\n"
				"Attempted reset to default: %u"
				,
				__FUNCTION__,
				pUpdater->pChartStack && eaSize(&pUpdater->pChartStack->eaChartStack) ?
							pUpdater->pChartStack->eaChartStack[0]->pcName :
							"no charts in stack",
				stanceWords,
				CURRENT_NODE->pGraph->pcName,
				CURRENT_NODE->pGraphNode->pcName,
				gConf.bUseMovementGraphs ?
					(eaSize(&pSkeleton->eaAGUpdater) > 1 ? SAFE_MEMBER(pSkeleton->eaAGUpdater[1]->pChartStack,interruptingMovementStanceCount) : 0) :
					SAFE_MEMBER(pSkeleton->movement.pChartStack,interruptingMovementStanceCount),
				pUpdater->bInPostIdle,
				CURRENT_NODE->pGraphNode->pTemplateNode->bInterruptible,
				pUpdater->bResetToDefaultWasAttempted);

		//print all possible default graphs (it could be one of several based on probability)
		//looping backwards since charts higher in the stack have greater priority,
		//and we want to only print the graphs for the chart with highest priority that has them
		if (pUpdater->pChartStack)
		{
			for (i = eaSize(&pUpdater->pChartStack->eaChartStack)-1; i >= 0; i--)
			{
				const DynAnimChartRunTime* pChart = pUpdater->pChartStack->eaChartStack[i];
				if (eaSize(&pChart->eaDefaultChances) > 0)
				{
					FOR_EACH_IN_EARRAY(pChart->eaDefaultChances, DynAnimGraphChanceRef, pGraphChanceRef)
					{
						const DynAnimGraph    *pGraphToPrint    = GET_REF(pGraphChanceRef->hGraph);
						const DynAnimTemplate *pTemplateToPrint = pGraphToPrint ? GET_REF(pGraphToPrint->hTemplate) : NULL;

						glrLog(	pUpdater->glr,
									"[dyn.AGU] Default graph: %s (type %s, stacked chance %g).",
									pGraphToPrint->pcName,
									pTemplateToPrint ? StaticDefineIntRevLookup(eAnimTemplateTypeEnum, pTemplateToPrint->eType) : "no template",
									pGraphChanceRef->fChance
									);
					}
					FOR_EACH_END;

					//only print the defaults for the highest priority chart stack then break out
					foundDefaultGraphs = true;
					break;
				}
			}
		}
		
		if (!foundDefaultGraphs)
		{
			glrLog(	pUpdater->glr,
					"[dyn.AGU] No default graphs.");
		}
	}

	if (pSkeleton->bFrozen) {
		pSkeleton->fFrozenTimeScale -= fDeltaTime;
	} else {
		pSkeleton->fFrozenTimeScale += fDeltaTime;
	}
	pSkeleton->fFrozenTimeScale = CLAMP(pSkeleton->fFrozenTimeScale, 0.f, 1.f);

	if ((	pUpdater->bInPostIdle
			||
			!disableInterrupts &&
			CURRENT_NODE->pGraphNode->pTemplateNode->bInterruptible)
		&&
		pSkeleton->fCurrentSpeedXZ > 0.1f
		&&
		(	!gConf.bUseMovementGraphs &&
			SAFE_MEMBER(pSkeleton->movement.pChartStack,interruptingMovementStanceCount)
			||
			gConf.bUseMovementGraphs &&
			eaSize(&pSkeleton->eaAGUpdater) > 1 &&
			SAFE_MEMBER(pSkeleton->eaAGUpdater[1]->pChartStack,interruptingMovementStanceCount)))
	{
		glrLog(	pUpdater->glr,
				"[dyn.AGU] Interrupting graph %s, node %s due to movement: speedXZ %1.3f%s%s.",
				pGraph->pcName,
				CURRENT_NODE->pGraphNode->pTemplateNode->pcName,
				pSkeleton->fCurrentSpeedXZ,
				pUpdater->bInPostIdle ? ", in post idle" : "",
				CURRENT_NODE->pGraphNode->pTemplateNode->bInterruptible ? ", interruptible" : "");

		dynAnimGraphUpdaterResetToADefaultGraph(pSkeleton, pUpdater, NULL, "movement started", 0, 0, DAGUI_MOVEMENT);
	}

	if(	pUpdater->bInPostIdle ||
		pUpdater->bResetToDefaultWasAttempted)
	{
		if (!dynAnimGraphUpdaterIsADefaultGraph(pUpdater, false) &&
			dynAnimGraphUpdaterAllDefaultGraphsAreBlockTypes(pUpdater)
			)
		{		
			dynAnimGraphUpdaterResetToADefaultGraph(pSkeleton, pUpdater, NULL, "block related", 0, 0, DAGUI_BLOCKING);
		}
	}

	{
		F32 fFrameDelta = fDeltaTime * 30.0f * dynDebugState.fAnimRate; // The total amount of frames we should pass this update
		F32 fFrameTimeLeft = fFrameDelta; // init the amount of time left (this will decrease on each cycle in the following loop
		F32 fOldTimeOnCurrentGraph = pUpdater->fTimeOnCurrentGraph;
		const DynMoveSeq* pMoveSeq = CURRENT_NODE->pMoveSeq;
		int iFirstNodeToUpdate = 1; // only update previous nodes if they are not updated here
		U32 uiNumLoops = 0;
		while (	pMoveSeq &&
				fFrameTimeLeft > 0.0f)
		{
			F32 fTimeLeftOnMove;
			F32 fModifiedTimePassed;

			F32 fOldCurrentNodeTime = CURRENT_NODE->fTime;
			
			// Check flag queue if we haven't already set a flag
			if (!CURRENT_NODE->pcSetFlag)
			{
				bool bHitInterrupt = false;
				bool bTrippedSwitch = false;
				FOR_EACH_IN_EARRAY_FORWARDS(pUpdater->eaFlagQueue, const char, pcFlag)
				{
					DynAnimTemplateNode* pTemplateNode = CURRENT_NODE->pGraphNode->pTemplateNode;
					FOR_EACH_IN_EARRAY(pTemplateNode->eaSwitch, DynAnimTemplateSwitch, pSwitch)
					{
						if (pSwitch->pcFlag == pcFlag &&
							dynAnimGraphNodeIsValidForPlayback(pGraph, pSwitch->next.p))
						{
							CURRENT_NODE->pcSetFlag = pcFlag;
							// Found a match
							if (CURRENT_NODE->pGraphNode->eaSwitch[ipSwitchIndex]->bInterrupt)
							{
								// Interrupt immediately
								pMoveSeq = dynAnimGraphUpdaterAdvanceNode(pSkeleton, pUpdater, pSkelInfo);

								// advanced node
								++iFirstNodeToUpdate;

								bHitInterrupt = true;
							}
							bTrippedSwitch = true;
							//eaRemove(&pUpdater->eaFlagQueue, ipcFlagIndex);
							//if (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits) {
							//	dynDebugSkeletonUseFlag(pSkeleton, pUpdater, pcFlag);
							//}
							break;
						}
					}
					FOR_EACH_END;
					if (bTrippedSwitch) {
						break;
					}
				}
				FOR_EACH_END;
				// We should loop back to test the next node (we advanced due to
				// interrupt flag) against the flag queue.
				if (bHitInterrupt) 
					continue;
			}

			// Time left on this move before advancing.
			fTimeLeftOnMove = pMoveSeq->fLength - CURRENT_NODE->fTime;

			// Different moves can 'advance time' at a different rate. This gives us how
			// much time would pass if all of the frame time left went through this one move.
			fModifiedTimePassed = dynMoveSeqAdvanceTime(pMoveSeq,
														fFrameTimeLeft,
														pSkeleton,
														&CURRENT_NODE->fTimeMutable,
														pSkeleton->fFrozenTimeScale * CURRENT_NODE->fPlaybackSpeed,
														1);

			if (!dynDebugState.bDisableClientSideRagdoll &&
				!disableRagdoll &&
				!pUpdater->bForceLoopCurrentGraph &&  //don't allow during editor playback
				!pSkeleton->bHasClientSideRagdoll &&
				!pSkeleton->bSleepingClientSideRagdoll &&
				pMoveSeq->bRagdoll)
			{
				if (fOldCurrentNodeTime <= pMoveSeq->fRagdollStartTime &&
					pMoveSeq->fRagdollStartTime < CURRENT_NODE->fTimeMutable)
				{
					pSkeleton->bCreateClientSideRagdoll = true;
					pSkeleton->pcClientSideRagdollAnimTrack = pMoveSeq->dynMoveAnimTrack.pcAnimTrackName;
					pSkeleton->fClientSideRagdollAnimTime = CURRENT_NODE->fTimeMutable;
					pSkeleton->fClientSideRagdollAdditionalGravity = pMoveSeq->fRagdollAdditionalGravity;
				}
				else if (!pSkeleton->bHasClientSideTestRagdoll)
				{
					pSkeleton->bCreateClientSideTestRagdoll = true;
					pSkeleton->pcClientSideRagdollAnimTrack = pMoveSeq->dynMoveAnimTrack.pcAnimTrackName;
					pSkeleton->fClientSideRagdollAnimTimePreCollision = 0.f;
					pSkeleton->fClientSideRagdollAnimTime = CURRENT_NODE->fTimeMutable;
					pSkeleton->fClientSideRagdollAdditionalGravity = pMoveSeq->fRagdollAdditionalGravity;
				}
			}

			dynAnimGraphUpdaterNodeAdvanceBlendFactor(CURRENT_NODE, pPreviousNode, fModifiedTimePassed);
			dynAnimGraphUpdatedNodeTriggerFxEvents(pSkeleton, pUpdater, CURRENT_NODE, CURRENT_NODE->pMove_debug, pMoveSeq, fOldCurrentNodeTime); // Must be after dynMoveSeqAdvanceTime to get the new time

			// This means we're going onto a new move this loop cycle.
			if (fModifiedTimePassed > fTimeLeftOnMove) 
			{
				F32 fModifiedTimeLeft = fModifiedTimePassed - fTimeLeftOnMove;
				F32 fModifier = fModifiedTimePassed / fFrameTimeLeft;

				// Advance the editor tracking of time passed on this graph.
				pUpdater->fTimeOnCurrentGraph += fTimeLeftOnMove; 

				fFrameTimeLeft = fModifiedTimeLeft / fModifier; // new time left
				if (nearSameF32(pMoveSeq->fLength, 0.f))
				{
					if (FALSE_THEN_SET(danimSentAGUInfLoopError)) {
						const char *pcMoveName = SAFE_MEMBER2(pMoveSeq, pDynMove, pcName);
						ErrorDetailsf("Move = %s", FIRST_IF_SET(pcMoveName, "NOT FOUND"));
						Errorf("Caught skeleton in an infinite loop while updating animation in %s, probably failed to load data for move, attempting to skip move!\n", __FUNCTION__);
					}
					fFrameTimeLeft = 0.f;
				}

				// Force end freeze for now. TODO: For moves that cycle, they
				// should continue cycling while being blended out of.
				CURRENT_NODE->fTimeMutable = pMoveSeq->fLength;

				pMoveSeq = dynAnimGraphUpdaterAdvanceNode(pSkeleton, pUpdater, pSkelInfo);

				// advanced node
				++iFirstNodeToUpdate;
			}
			else
			{
				pUpdater->fTimeOnCurrentGraph += fFrameTimeLeft;
				fFrameTimeLeft = 0.0f;
			}

			// This is so we don't loop when scrubbing in the editor
			if (pUpdater->bForceLoopCurrentGraph && pUpdater->fTimeOnCurrentGraph < fOldTimeOnCurrentGraph)
				break;
			// Timeout testing
			else if (!pUpdater->bForceLoopCurrentGraph && pUpdater->pCurrentGraph->fTimeout && pUpdater->fTimeOnCurrentGraph > 30.0*pUpdater->pCurrentGraph->fTimeout) {
				dynAnimGraphUpdaterResetToADefaultGraph(pSkeleton, pUpdater, NULL, "timed out", 0, 0, DAGUI_TIMEOUT);
			}

			if (++uiNumLoops > 100) {
				if (FALSE_THEN_SET(danimSentAGUInfLoopError)) {
					const char *pcMoveName = SAFE_MEMBER2(pMoveSeq, pDynMove, pcName);
					ErrorDetailsf("Move = %s", FIRST_IF_SET(pcMoveName, "NOT FOUND"));
					Errorf("Caught skeleton in what's likely an infinite loop while updating animation in %s, check move for 0 frame length, attempting reset to default!\n", __FUNCTION__);
				}
				dynAnimGraphUpdaterResetToADefaultGraph(pSkeleton, pUpdater, NULL, "likely inf. loop", 0, 0, DAGUI_TIMEOUT);
				fFrameTimeLeft = 0.f;
			}
		}

		// update previous nodes
		if (CURRENT_NODE->fBlendFactor < 1.0f)
		{
			int iIndex;
			for (iIndex = 1; iIndex < MAX_UPDATER_NODES; ++iIndex)
			{
				DynAnimGraphUpdaterNode* pNode = &pUpdater->nodes[iIndex];
				// Do not update nodes that were updated above.
				if (iIndex >= iFirstNodeToUpdate) 
				{
					pMoveSeq = pNode->pMoveSeq;
					if (pMoveSeq)
					{
						F32 fTimeDiff = dynMoveSeqAdvanceTime(	pMoveSeq,
																fFrameDelta,
																pSkeleton,
																&pNode->fTimeMutable,
																pSkeleton->fFrozenTimeScale * pNode->fPlaybackSpeed,
																1);

						F32 fLeftover = pNode->fTime - pMoveSeq->fLength;

						dynAnimGraphUpdaterNodeAdvanceBlendFactor(pNode, pPreviousNode, fTimeDiff);

						if (fLeftover > 0.0f)
						{
							// force end freeze
							pNode->fTimeMutable = pMoveSeq->fLength;
							// put cycling here
						}
					}
				}

				if (pNode->fBlendFactor >= 1.0f)
					break;
			}
		}
	}

	if (pSkeleton->pFxManager && SAFE_MEMBER(pUpdater->pCurrentGraph, eaSuppress))
	{
		FOR_EACH_IN_EARRAY(pUpdater->pCurrentGraph->eaSuppress, DynAnimGraphSuppress, pSuppress)
		{
			glrLog(	pUpdater->glr,
					"[dyn.AGU] Graph %s suppressing fx %s.",
					pUpdater->pCurrentGraph->pcName,
					pSuppress->pcSuppressionTag);

			dynFxManSuppress(pSkeleton->pFxManager, pSuppress->pcSuppressionTag);
		}
		FOR_EACH_END;
	}

	// Check for override all status
	{
		if(SAFE_MEMBER(pUpdater->pCurrentGraph, bOverrideAllBones)){
			glrLog(	pUpdater->glr,
					"[dyn.AGU] Graph %s overrides all bones.",
					pUpdater->pCurrentGraph->pcName);

			pSkeleton->bOverrideAll = true;
			pSkeleton->bSnapOverrideAll = pUpdater->pCurrentGraph->bSnapOverrideAllBones;
			pSkeleton->iOverrideSeqIndex = eaFind(&pSkeleton->eaAGUpdater,pUpdater);
			MAX1(pSkeleton->iOverrideSeqIndex,0);
		}
		else if(SAFE_MEMBER(CURRENT_NODE->pGraphNode, bOverrideAllBones)){
			glrLog(	pUpdater->glr,
					"[dyn.AGU] Graph node [%s::%s] overrides all bones.",
					SAFE_MEMBER(pUpdater->pCurrentGraph, pcName),
					CURRENT_NODE->pGraphNode->pcName);

			pSkeleton->bOverrideAll = true;
			pSkeleton->bSnapOverrideAll = CURRENT_NODE->pGraphNode->bSnapOverrideAllBones;
			pSkeleton->iOverrideSeqIndex = eaFind(&pSkeleton->eaAGUpdater,pUpdater);
			MAX1(pSkeleton->iOverrideSeqIndex,0);
		}

		if(SAFE_MEMBER(pUpdater->pCurrentGraph, bOverrideMovement)){
			glrLog(	pUpdater->glr,
					"[dyn.AGU] Graph %s has movement override enabled.",
					pUpdater->pCurrentGraph->pcName);

			pSkeleton->bOverrideMovement = true;
			pSkeleton->bSnapOverrideMovement = pUpdater->pCurrentGraph->bSnapOverrideMovement;
		}
		else if(SAFE_MEMBER(CURRENT_NODE->pGraphNode, bOverrideMovement)){
			glrLog(	pUpdater->glr,
					"[dyn.AGU] Graph node [%s::%s] has movement override enabled.",
					SAFE_MEMBER(pUpdater->pCurrentGraph, pcName),
					CURRENT_NODE->pGraphNode->pcName);

			pSkeleton->bOverrideMovement = true;
			pSkeleton->bSnapOverrideMovement = CURRENT_NODE->pGraphNode->bSnapOverrideMovement;
		}

		if(SAFE_MEMBER(pUpdater->pCurrentGraph, bOverrideDefaultMove)){
			glrLog(	pUpdater->glr,
				"[dyn.AGU] Graph %s has default move override enabled.",
				pUpdater->pCurrentGraph->pcName);

			pSkeleton->bOverrideDefaultMove = true;
			pSkeleton->bSnapOverrideDefaultMove = pUpdater->pCurrentGraph->bSnapOverrideDefaultMove;
		}
		else if(SAFE_MEMBER(CURRENT_NODE->pGraphNode, bOverrideDefaultMove)){
			glrLog(	pUpdater->glr,
				"[dyn.AGU] Graph node [%s::%s] has default move override enabled.",
				SAFE_MEMBER(pUpdater->pCurrentGraph, pcName),
				CURRENT_NODE->pGraphNode->pcName);

			pSkeleton->bOverrideDefaultMove = true;
			pSkeleton->bSnapOverrideDefaultMove = CURRENT_NODE->pGraphNode->bSnapOverrideDefaultMove;
		}

		if(SAFE_MEMBER(pUpdater->pCurrentGraph, bForceVisible)){
			glrLog(	pUpdater->glr,
					"[dyn.AGU] Graph %s is forcing the skeleton visible.",
					pUpdater->pCurrentGraph->pcName);

			pSkeleton->bForceVisible = true;
		}
		else if (CURRENT_NODE->fBlendFactor < 1.0f)
		{
			int iIndex;
			for (iIndex = 1; iIndex < MAX_UPDATER_NODES; ++iIndex)
			{
				DynAnimGraphUpdaterNode* pNode = &pUpdater->nodes[iIndex];
				if (pNode->pGraph->bForceVisible) {
					glrLog(	pUpdater->glr,
							"[dyn.AGU] Graph %s is forcing the skeleton visible.",
							pNode->pGraph->pcName);
					pSkeleton->bForceVisible = true;
					break;
				}
				else if (pNode->fBlendFactor >= 1.0f)
					break;
			}
		}

		if (pUpdater->bOverlay)
		{
			if (pUpdater->bInTPose)
				pUpdater->fOverlayBlend -= 5.0f*fDeltaTime;
			else
				pUpdater->fOverlayBlend += 5.0f*fDeltaTime;
		
			pUpdater->fOverlayBlend = CLAMP(pUpdater->fOverlayBlend, 0.0f, 1.0f);
		}
	}

	glrLog(	pUpdater->glr,
			"[dyn.AGU] %s END",
			__FUNCTION__);
}

void dynAnimGraphUpdaterInitMatchJoints(DynSkeleton* pSkeleton, DynAnimGraphUpdater* pUpdater, DynJointBlend*** peaMatchJoints)
{
	const DynMoveSeq* pMoveSeq = pUpdater->nodes[0].pMoveSeq;
	if (pMoveSeq)
	{
		FOR_EACH_IN_EARRAY(pMoveSeq->eaMatchBaseSkelAnim, DynMoveMatchBaseSkelAnim, pMatchBaseSkelAnim)
		{
			bool bCreate = true;
			FOR_EACH_IN_EARRAY(*peaMatchJoints, DynJointBlend, pMatchedJoint)
			{
				if (pMatchedJoint->pcName == pMatchBaseSkelAnim->pcBoneName)
				{
					pMatchedJoint->bActive = true;
					bCreate = false;
					//in case the calling animation has changed
					pMatchedJoint->bPlayBlendOutWhileActive = pMatchBaseSkelAnim->bPlayBlendOutDuringMove;
					pMatchedJoint->fBlendInRate  = (pMatchBaseSkelAnim->fBlendInTime  < 0.01) ? 100.f : 1.f/pMatchBaseSkelAnim->fBlendInTime;
					pMatchedJoint->fBlendOutRate = (pMatchBaseSkelAnim->fBlendOutTime < 0.01) ? 100.f : 1.f/pMatchBaseSkelAnim->fBlendOutTime;
					zeroVec3(pMatchedJoint->vPosOffset);
					FOR_EACH_IN_EARRAY(pMoveSeq->dynMoveAnimTrack.eaBoneOffset, DynMoveBoneOffset, pBoneOffset) {
						if (pBoneOffset->pcBoneName == pMatchedJoint->pcName) {
							copyVec3(pBoneOffset->vOffset, pMatchedJoint->vPosOffset);
							break;
						}
					} FOR_EACH_END;
					break;
				}
			}
			FOR_EACH_END;

			if (bCreate)
			{
				const DynNode* pBone = dynSkeletonFindNode(pSkeleton, pMatchBaseSkelAnim->pcBoneName);
				if (pBone)
				{
					DynJointBlend *pNewMatchJoint = calloc(1,sizeof(DynJointBlend));
					pNewMatchJoint->pcName = pMatchBaseSkelAnim->pcBoneName;
					pNewMatchJoint->bActive = true;
					pNewMatchJoint->fBlend = pMatchBaseSkelAnim->bStartFullyBlended ? 1.f : 0.f;
					pNewMatchJoint->bPlayBlendOutWhileActive = pMatchBaseSkelAnim->bPlayBlendOutDuringMove;
					pNewMatchJoint->fBlendInRate  = (pMatchBaseSkelAnim->fBlendInTime  < 0.01) ? 100.f : 1.f/pMatchBaseSkelAnim->fBlendInTime;
					pNewMatchJoint->fBlendOutRate = (pMatchBaseSkelAnim->fBlendOutTime < 0.01) ? 100.f : 1.f/pMatchBaseSkelAnim->fBlendOutTime;
					dynNodeGetLocalPos(pBone, pNewMatchJoint->vOriginalPos);
					zeroVec3(pNewMatchJoint->vPosOffset);
					FOR_EACH_IN_EARRAY(pMoveSeq->dynMoveAnimTrack.eaBoneOffset, DynMoveBoneOffset, pBoneOffset) {
						if (pBoneOffset->pcBoneName == pNewMatchJoint->pcName) {
							copyVec3(pBoneOffset->vOffset, pNewMatchJoint->vPosOffset);
							break;
						}
					} FOR_EACH_END;
					eaPush(peaMatchJoints, pNewMatchJoint);
				}
			}
		}
		FOR_EACH_END;
	}
}

void dynAnimGraphUpdaterUpdateMatchJoints(DynSkeleton* pSkeleton, DynJointBlend*** peaMatchJoints, F32 fDeltaTime)
{
	FOR_EACH_IN_EARRAY(*peaMatchJoints, DynJointBlend, pMatchJoint)
	{
		DynNode* pBone = dynSkeletonFindNodeNonConst(pSkeleton, pMatchJoint->pcName);
		if (pBone)
		{
			//return mount points to their original positions
			if (pBone->uiLocked) {
				dynNodeSetPos(pBone, pMatchJoint->vOriginalPos);
			}

			//check to see if we need to blend in or out
			if (pMatchJoint->bActive && pSkeleton->uiLODLevel <= worldLibGetLODSettings()->uiIKLODLevel)
			{
				if (pMatchJoint->bPlayBlendOutWhileActive) {
					pMatchJoint->fBlend -= pMatchJoint->fBlendOutRate * fDeltaTime;
					pMatchJoint->bActive = false;	
					MAX1(pMatchJoint->fBlend, 0.f); //don't delete here, otherwise it'll recreate itself
				} else {
					pMatchJoint->fBlend += pMatchJoint->fBlendInRate * fDeltaTime;
					pMatchJoint->bActive = false;	
					MIN1(pMatchJoint->fBlend, 1.f);
				}
			}
			else
			{
				pMatchJoint->fBlend -= pMatchJoint->fBlendOutRate * fDeltaTime;
				pMatchJoint->bActive = false;
				if (pMatchJoint->fBlend <= 0) {
					eaRemoveFast(peaMatchJoints, ipMatchJointIndex);
					SAFE_FREE(pMatchJoint);
				}
			}
		}
		else //if (!pBone)
		{
			eaRemoveFast(peaMatchJoints, ipMatchJointIndex);
			SAFE_FREE(pMatchJoint);
		}
	}
	FOR_EACH_END;
}

void dynAnimGraphUpdaterCalcMatchJointOffset(DynSkeleton* pSkeleton, DynAnimGraphUpdater* pUpdater, DynJointBlend* pMatchJoint, Vec3 vReturn)
{
	const DynBaseSkeleton* pBaseSkeleton = NULL;
	const DynMoveSeq* pMoveSeq = pUpdater->nodes[0].pMoveSeq;
	Mat4 mRoot;

	assert(gConf.bNewAnimationSystem);

	pBaseSkeleton = dynSkeletonGetBaseSkeleton(pSkeleton);
	dynNodeGetWorldSpaceMat(pSkeleton->pRoot, mRoot, false);

	if (pBaseSkeleton && pMoveSeq)
	{
		const DynNode* aNodes[32];
		S32 iNumNodes, iProcessNode;
		F32 fFrame = pUpdater->nodes[0].fTime;

		aNodes[0] = dynBaseSkeletonFindNode(pBaseSkeleton, pMatchJoint->pcName);
		iNumNodes = 1;

		if (aNodes[0])
		{
			DynNode *pSkelRootNode;
			DynNode *pSkelMatchNode;

			DynTransform xRunning;
			Vec3 vPosMatchWS;
			Vec3 vPosDiff;

			while (aNodes[iNumNodes-1]->pParent)
			{
				aNodes[iNumNodes] = aNodes[iNumNodes-1]->pParent;
				iNumNodes++;
			}

			pSkelRootNode = dynSkeletonFindNodeNonConst(pSkeleton,aNodes[iNumNodes-1]->pcTag);
			dynNodeGetWorldSpaceTransform(pSkelRootNode, &xRunning);
			unitVec3(xRunning.vScale);

			for (iProcessNode = iNumNodes-1; iProcessNode >= 0; iProcessNode--)
			{
				DynTransform xBase, xBaseAnim;
				Vec3 vPos, vPosOld;
				Quat qRot;

				copyVec3(xRunning.vPos, vPosOld);

				if (!(aNodes[iProcessNode]->uiTransformFlags & ednRot))   unitQuat(xRunning.qRot);
				if (!(aNodes[iProcessNode]->uiTransformFlags & ednTrans)) zeroVec3(xRunning.vPos);

				dynNodeGetLocalTransformInline(aNodes[iProcessNode], &xBase);
				dynMoveSeqCalcTransform(pMoveSeq, fFrame, &xBaseAnim, aNodes[iProcessNode]->pcTag, &xBase, NULL, pSkeleton->pRoot, false);

				quatRotateVec3Inline(xRunning.qRot, xBaseAnim.vPos, vPos);
				addVec3(vPos, xRunning.vPos, xRunning.vPos);
				quatMultiplyInline(xBaseAnim.qRot, xRunning.qRot, qRot);
				copyQuat(qRot, xRunning.qRot);

				if (uiShowMatchJointsBaseSkeleton) {
					wl_state.drawAxesFromTransform_func(&xRunning, 5.f);
					wl_state.drawLine3D_2_func(vPosOld, xRunning.vPos, 0xFFFFFFFF, 0xFFFFFFFF);
				}
			}

			addVec3(xRunning.vPos, pMatchJoint->vPosOffset, xRunning.vPos);

			pSkelMatchNode = dynSkeletonFindNodeNonConst(pSkeleton, pMatchJoint->pcName);
			dynNodeGetLocalPos(pSkelMatchNode, pMatchJoint->vOriginalPos);
			dynNodeGetWorldSpacePos(pSkelMatchNode, vPosMatchWS);
			subVec3(xRunning.vPos, vPosMatchWS, vPosDiff);
			scaleVec3(vPosDiff, pMatchJoint->fBlend, vPosDiff);
			addVec3(vPosDiff, vPosMatchWS, vReturn);
			dynNodeCalcLocalSpacePosFromWorldSpacePos(pSkelMatchNode, vReturn);
			subVec3(vReturn, pMatchJoint->vOriginalPos, vReturn);
		}
	}
	else
	{
		zeroVec3(vReturn);
	}
}

void dynAnimGraphUpdaterCalcNodeBank(const DynAnimGraphUpdater* pUpdater, F32* fBankMaxAngle, F32* fBankScale)
{
	// MOVEMENT GRAPHS TODO
	assert(pUpdater->bMovement);
	*fBankMaxAngle = 0.f;
	*fBankScale = 0.f;
}

U32 dynAnimGraphUpdaterMovementIsStopped(const DynAnimGraphUpdater* pUpdater)
{
	// MOVEMENT GRAPHS TODO
	assert(pUpdater->bMovement);
	return 1;
}

F32 dynAnimGraphUpdaterCalcBlockYaw(const DynAnimGraphUpdater* pUpdater, F32* fMoveYawRate, F32* fMovementYawStopped)
{
	// MOVEMENT GRAPHS TODO
	assert(pUpdater->bMovement);
	*fMoveYawRate = 0.f;
	*fMovementYawStopped = 0.f;
	return 0.f;
}

void dynAnimGraphUpdaterUpdateBoneHelper(	DynAnimGraphUpdater* pUpdater,
											DynAnimGraphUpdaterNode* pNode,
											DynTransform* pResult,
											const char* pcBoneTag,
											U32 uiBoneLOD,
											const DynTransform* pxBaseTransform,
											DynRagdollState* pRagdollState,
											DynSkeleton* pSkeleton,
											int iNextNodeIndex) 
{
	DynTransform xTransform;
	if (pNode->pMoveSeq->bEnableTerrainTiltBlend)
	{
		DynTransform xTemp;
		dynMoveSeqCalcTransform(pNode->pMoveSeq, pNode->fTime, &xTemp, pcBoneTag, pxBaseTransform, pRagdollState, pSkeleton->pRoot, true);
		dynTransformInterp(pSkeleton->fTerrainTiltBlend, &xIdentity, &xTemp, &xTransform);
	}
	else
	{
		dynMoveSeqCalcTransform(pNode->pMoveSeq, pNode->fTime, &xTransform, pcBoneTag, pxBaseTransform, pRagdollState, pSkeleton->pRoot, true);
	}

	if (pNode->fBlendFactor < 1.0f && (iNextNodeIndex+1) < MAX_UPDATER_NODES)
	{
		DynTransform xTemp;
		DynAnimGraphUpdaterNode* pNextNode = &pUpdater->nodes[iNextNodeIndex];
		F32 fEasedBlendFactor = dynAnimInterpolationCalcInterp(pNode->fBlendFactor, &pNode->pGraphNode->interpBlock);
		dynAnimGraphUpdaterUpdateBoneHelper(pUpdater, pNextNode, &xTemp, pcBoneTag, uiBoneLOD, pxBaseTransform, pRagdollState, pSkeleton, iNextNodeIndex+1);
		dynTransformInterp(fEasedBlendFactor, &xTemp, &xTransform, pResult);
	}
	else
	{
		dynTransformCopy(&xTransform, pResult);
	}
}

void dynAnimGraphUpdaterUpdateBone( DynAnimGraphUpdater* pUpdater, DynTransform* pResult, const char* pcBoneTag, U32 uiBoneLOD, const DynTransform* pxBaseTransform, DynRagdollState* pRagdollState, DynSkeleton* pSkeleton) 
{
	DynAnimGraphUpdaterNode* pNode = &pUpdater->nodes[0];
	if (pNode->pMoveSeq)
		dynAnimGraphUpdaterUpdateBoneHelper(pUpdater, pNode, pResult, pcBoneTag, uiBoneLOD, pxBaseTransform, pRagdollState, pSkeleton, 1);
	else if (pxBaseTransform)
		dynTransformCopy(pxBaseTransform, pResult);
	else
		dynTransformClearInline(pResult);
}


F32 dynAnimGraphUpdaterGetMovePercent(DynAnimGraphUpdater* pUpdater)
{
	DynAnimGraphUpdaterNode* pCurrentNode = &pUpdater->nodes[0];
	const DynMoveSeq* pMoveSeq = pCurrentNode->pMoveSeq;
	if (pMoveSeq)
		return pCurrentNode->fTime / pMoveSeq->fLength;
	return 0.0f;
}

F32 dynAnimGraphUpdaterGetMoveTime(DynAnimGraphUpdater* pUpdater)
{
	DynAnimGraphUpdaterNode* pCurrentNode = &pUpdater->nodes[0];
	const DynMoveSeq* pMoveSeq = pCurrentNode->pMoveSeq;
	if (pMoveSeq)
		return pCurrentNode->fTime;
	return 0.0f;
}

F32 dynAnimGraphUpdaterGetMoveLength(DynAnimGraphUpdater* pUpdater)
{
	DynAnimGraphUpdaterNode* pCurrentNode = &pUpdater->nodes[0];
	const DynMoveSeq* pMoveSeq = pCurrentNode->pMoveSeq;
	if (pMoveSeq)
		return pMoveSeq->fLength;
	return 0.0f;
}

F32 dynAnimGraphUpdaterDebugCalcBlend(DynAnimGraphUpdater* pUpdater, DynAnimGraphNode* pGraphNode)
{
	F32 fBlendTotal = 0.0f;
	F32 fRunningBlendFactor = 1.0f;
	int iIndex;
	for (iIndex=0; iIndex < MAX_UPDATER_NODES; ++iIndex)
	{
		DynAnimGraphUpdaterNode* pNode = &pUpdater->nodes[iIndex];
		F32 fEasedBlendFactor = dynAnimInterpolationCalcInterp(pNode->fBlendFactor, &pNode->pGraphNode->interpBlock);
		if (pNode->pGraphNode == pGraphNode)
			fBlendTotal += (fRunningBlendFactor * fEasedBlendFactor);
		fRunningBlendFactor *= (1.0f - fEasedBlendFactor);
		if (pNode->fBlendFactor == 1.0f)
			break;
	}
	return fBlendTotal;
}

bool dynAnimGraphUpdaterCanInterrupt(	const DynAnimGraphUpdater* u,
										const DynAnimGraph *gNew,
										const char** interruptReasonOut)
{
	const DynAnimTemplate* tNew = GET_REF(gNew->hTemplate);
	const DynAnimTemplate* tCur = GET_REF(u->pCurrentGraph->hTemplate);

	//Death can't interrupt itself
	if (SAFE_MEMBER(tCur,eType) == eAnimTemplateType_Death &&
		SAFE_MEMBER(tNew,eType) == eAnimTemplateType_Death)
		return false;
	
	//play hit reacts on the overlay sequencer instead
	switch(SAFE_MEMBER(tNew, eType)){
	case eAnimTemplateType_PowerInterruptingHitReact:
	case eAnimTemplateType_HitReact:
		{
			if (tNew->eType == eAnimTemplateType_PowerInterruptingHitReact)
			{
				if (tCur->eType == eAnimTemplateType_Power) {
					*interruptReasonOut = "power interrupting hit react interrupts power";
					return true;
				} else if (tCur->eType == eAnimTemplateType_Emote) {
					*interruptReasonOut = "power interrupting hit react interrupts emote";
					return true;
				}
			}

			if(u->bOverlay){
				*interruptReasonOut = "hit react always interrupts in overlay sequencer";
				return true;
			}

			if(u->bOnDefaultGraph){
				*interruptReasonOut = "hit react interrupts default graph";
				return true;
			}

			if(u->bInPostIdle){
				*interruptReasonOut = "hit react interrupts post-idle";
				return true;
			}

			if(!tCur){
				*interruptReasonOut = "no current template";
				return true;
			}

			if(tCur->eType == eAnimTemplateType_HitReact){
				if (tNew->eType == eAnimTemplateType_HitReact)
					*interruptReasonOut = "hit react interrupts existing hit react";
				else if (tNew->eType == eAnimTemplateType_PowerInterruptingHitReact)
					*interruptReasonOut = "power interrupting hit react interrupts existing hit react";
				return true; 
			}

			if (tCur->eType == eAnimTemplateType_PowerInterruptingHitReact &&
				tNew->eType == eAnimTemplateType_PowerInterruptingHitReact)
			{
					*interruptReasonOut = "power interrupting hit react interrupts existing power interrupting hit react";
					return true;
			}

			// We're playing something important, hope they have an overlay to handle this.
			return false; 
		} break;
	}

	*interruptReasonOut = "always interrupts";

	return true;
}

void dynAnimGraphUpdaterGetChartStackString(DynAnimGraphUpdater *pUpdater,
											char* buffer,
											U32 bufferLen)
{
	// update the chart stack (has internal check for dirty bit)
	dynAnimChartStackSetFromStanceWords(pUpdater->pChartStack);

	if(!bufferLen){
		return;
	}

	buffer[0] = 0;

	if (pUpdater->pChartStack)
	{
		EARRAY_CONST_FOREACH_BEGIN(pUpdater->pChartStack->eaChartStack, i, isize);
		{
			const DynAnimChartRunTime* c = pUpdater->pChartStack->eaChartStack[i];
			strcatf_s(buffer, bufferLen, "%s%s", i ? ", " : "", c->pcName);
		}
		EARRAY_FOREACH_END;
	}
}

void dynAnimGraphUpdaterGetStanceWordsString(	DynAnimGraphUpdater *pUpdater,
												char* buffer,
												U32 bufferLen)
{
	if(!bufferLen){
		return;
	}

	buffer[0] = 0;

	if (pUpdater->pChartStack)
	{
		EARRAY_CONST_FOREACH_BEGIN(pUpdater->pChartStack->eaStanceWords, i, isize);
		{
			const char* s = pUpdater->pChartStack->eaStanceWords[i];
			strcatf_s(buffer, bufferLen, "%s%s", i ? ", " : "", s);
		}
		EARRAY_FOREACH_END;
	}
}

static S32 disableHitReactError;
AUTO_CMD_INT(disableHitReactError, dynAnimGraphUpdaterDisableHitReactError);

bool dynAnimGraphUpdaterStartGraph(	DynSkeleton *pSkeleton,
									DynAnimGraphUpdater *pUpdater,
									const char *pcGraph,
									U32 uid,
									S32 mustBeDetailGraph)
{
	DynAnimChartRunTime* pChart;
	DynAnimGraph* pGraph;
	const char* interruptReason = NULL;

#if DYNANIMGRAPHUPDATER_DEBUG_PRINTANIMWORDS
		printfColor(COLOR_BLUE, "%s ", __FUNCTION__);
		printfColor(COLOR_BLUE|COLOR_BRIGHT, "%s (%u)\n", pcGraph, uid);
#endif

	pGraph = dynAnimChartStackFindGraph(pUpdater->pChartStack, pcGraph, pUpdater->bMovement, &pChart);
	assert(pGraph != (DynAnimGraph*)(intptr_t)0xdddddddd); // Tracking references to freed graphs

	if(pUpdater->glr){
		char buffer[1000];

		dynAnimGraphUpdaterGetStanceWordsString(pUpdater, SAFESTR(buffer));

		glrLog(	pUpdater->glr,
				"[dyn.AGU] %s: stances: \"%s\".",
				__FUNCTION__,
				buffer);

		dynAnimGraphUpdaterGetChartStackString(pUpdater, SAFESTR(buffer));

		glrLog(	pUpdater->glr,
				"[dyn.AGU] %s: charts: \"%s\".",
				__FUNCTION__,
				buffer);
	}

	pUpdater->bResetToDefaultWasAttempted = 0;

	if (!pGraph || !GET_REF(pGraph->hTemplate))
	{
		glrLog(	pUpdater->glr,
				"[dyn.AGU] %s: no graph found for keyword \"%s\".",
				__FUNCTION__,
				pcGraph);

		return false;
	}

	if(	mustBeDetailGraph &&
		GET_REF(pGraph->hTemplate) &&
		!(	GET_REF(pGraph->hTemplate)->eType == eAnimTemplateType_HitReact ||
			GET_REF(pGraph->hTemplate)->eType == eAnimTemplateType_PowerInterruptingHitReact) &&
		!disableHitReactError)
	{
		Errorf("Anim graph \"%s\" is not a hit react or power interrupting hit react but was played as one.", pGraph->pcName);
		//verbose_printf("Anim graph \"%s\" is not a hit react or power interrupting hit react but was played as one.", pGraph->pcName);
	}

	if (!dynAnimGraphUpdaterCanInterrupt(pUpdater, pGraph, &interruptReason))
	{
		glrLog(	pUpdater->glr,
				"[dyn.AGU] %s: graph \"%s\" cannot interrupt.",
				__FUNCTION__,
				pGraph->pcName);
		return false;
	}

	glrLog(	pUpdater->glr,
			"[dyn.AGU] %s: starting \"%s\" (reason: %s).",
			__FUNCTION__,
			pGraph->pcName,
			interruptReason);

	assert(pGraph->bReversedInheritBitPolarity);

	pUpdater->pCurrentGraphMutable = pGraph;
	pUpdater->pCurrentChartMutable = pChart;
	pUpdater->uidCurrentGraph = uid;
	pUpdater->bInPostIdle = false;
	pUpdater->bInDefaultPostIdle = false;
	dynAnimGraphUpdaterGraphChanged(pSkeleton, pUpdater, "start graph", interruptReason);
	dynAnimGraphUpdaterSetPostIdleCallerStances(pUpdater);
	pUpdater->bEnableOverrideTime = true;
	pSkeleton->bStartedKeywordGraph = true;
	return true;
}

static bool dynAnimGraphUpdaterIsADefaultGraphAtStackLayer(DynAnimGraphUpdater *pUpdater, int layer, bool bMovement)
{
	const DynAnimChartRunTime* pChart;
	DynAnimGraphChanceRef** const* peaUseDefaultChances;
	DynAnimGraph* pGraph;

	//no need to call dynAnimChartStackSetFromStanceWords(&pUpdater->chartStack) here
	//this function should be wrapped by one which already does that

	//set the initial chart
	assert(pUpdater->pChartStack);
	pChart = pUpdater->pChartStack->eaChartStack[layer];

	//find the graph based on rf and stacked probabilities
	if (bMovement) peaUseDefaultChances = &pChart->eaMoveDefaultChances;
	else           peaUseDefaultChances = &pChart->eaDefaultChances;

	FOR_EACH_IN_EARRAY(*peaUseDefaultChances, DynAnimGraphChanceRef, pGraphChanceRef)
	{
		if (pGraph = GET_REF(pGraphChanceRef->hGraph))
		{
			if (pGraph == pUpdater->pCurrentGraph)
				return true;
		}
	}
	FOR_EACH_END;

	//none of the default graphs at the desired level matched (or their were none)
	return false;
}

static bool dynAnimGraphUpdaterIsADefaultGraph(DynAnimGraphUpdater* pUpdater, bool bMovement)
{
	if (pUpdater->pChartStack)
	{
		// update the chart stack (has internal check for dirty bit)
		dynAnimChartStackSetFromStanceWords(pUpdater->pChartStack);

		EARRAY_FOREACH_REVERSE_BEGIN(pUpdater->pChartStack->eaChartStack, i);
		{
			const DynAnimChartRunTime* pChart = pUpdater->pChartStack->eaChartStack[i];

			//make sure there are graphs at this level
			if (!eaSize(&pChart->eaDefaultChances))
				continue;
			else
				return dynAnimGraphUpdaterIsADefaultGraphAtStackLayer(pUpdater, i, bMovement);
		}
		EARRAY_FOREACH_END;
	}

	return false;
}

static bool dynAnimGraphUpdaterCheckDefaultGraphsForBlockTypes(DynAnimGraphUpdater *pUpdater, bool checkForBlocks)
{
	// MOVEMENT GRAPHS TODO

	bool allDefaultGraphs = true;
	int i;

	if (pUpdater->pChartStack)
	{
		// update the chart stack (has internal check for dirty bit)
		dynAnimChartStackSetFromStanceWords(pUpdater->pChartStack);

		//looping backwards since charts higher in the stack have greater priority
		for (i = eaSize(&pUpdater->pChartStack->eaChartStack)-1; i >= 0; i--)
		{
			const DynAnimChartRunTime* pChart = pUpdater->pChartStack->eaChartStack[i];
			if (eaSize(&pChart->eaDefaultChances) > 0)
			{
				FOR_EACH_IN_EARRAY(pChart->eaDefaultChances, DynAnimGraphChanceRef, pGraphChanceRef)
				{
					const DynAnimGraph    *pCheckGraph    = GET_REF(pGraphChanceRef->hGraph);
					const DynAnimTemplate *pCheckTemplate = pCheckGraph ? GET_REF(pCheckGraph->hTemplate) : NULL;

					//fail the check if we find a non-blocker as a potential option
					if (checkForBlocks && SAFE_MEMBER(pCheckTemplate, eType) != eAnimTemplateType_Block)
					{
						allDefaultGraphs = false;
						break;
					}
					else if (!checkForBlocks && SAFE_MEMBER(pCheckTemplate, eType) == eAnimTemplateType_Block)
					{
						allDefaultGraphs = false;
						break;
					}
				}
				FOR_EACH_END;

				//stop tunneling if we found the correct layer
				break;
			}
		}
	}

	return allDefaultGraphs;
}
static bool dynAnimGraphUpdaterNoDefaultGraphsAreBlockTypes (DynAnimGraphUpdater *pUpdater){return dynAnimGraphUpdaterCheckDefaultGraphsForBlockTypes(pUpdater, false);}
static bool dynAnimGraphUpdaterAllDefaultGraphsAreBlockTypes(DynAnimGraphUpdater *pUpdater){return dynAnimGraphUpdaterCheckDefaultGraphsForBlockTypes(pUpdater, true );}

static void dynAnimGraphUpdaterMovementFallback(	DynSkeleton* pSkeleton,
													DynAnimGraphUpdater* pUpdater,
													bool bSetKeys )
{
	DynAnimChartStack* pChartStack = pUpdater->pChartStack;
	if (pChartStack)
	{
		bool bLanded = 0 <= eaFind(&pChartStack->eaStanceFlags, s_pchStanceFlagLanded);
		assert(pUpdater->bMovement);

		// update the chart stack (has internal check for dirty bit)
		dynAnimChartStackSetFromStanceWords(pChartStack);

		eaClear(&pChartStack->eaStanceKeys);
		eaClear(&pChartStack->eaRemovedStanceKeys);
		eaClear(&pChartStack->eaStanceFlags);
		eaClear(&pChartStack->eaRemovedStanceFlags);
		pChartStack->bStopped = false;

		FOR_EACH_IN_EARRAY(pSkeleton->eaStances[DS_STANCE_SET_PRE_UPDATE], const char, pcMovementStance) {
			if (dynAnimMovementStanceKeyValid(pcMovementStance)) {
				if (bSetKeys)
					eaPush(&pChartStack->eaStanceKeys, pcMovementStance);
			} else // assumed dynAnimMovementStanceValid is true
				eaPush(&pChartStack->eaStanceFlags, pcMovementStance);
		} FOR_EACH_END;

		FOR_EACH_IN_EARRAY(pSkeleton->eaStances[DS_STANCE_SET_DEBUG], const char, pcDebugStance) {
			if (dynAnimMovementStanceKeyValid(pcDebugStance)) {
				if (bSetKeys)
					eaPush(&pChartStack->eaStanceKeys, pcDebugStance);
			} else if (dynAnimMovementStanceValid(pcDebugStance))
				eaPush(&pChartStack->eaStanceFlags, pcDebugStance);
		} FOR_EACH_END;

		if (bLanded)
			eaPush(&pChartStack->eaStanceFlags, s_pchStanceFlagLanded);
	}
}

static DynAnimGraph *dynAnimGraphUpdaterGetADefaultGraph(	DynAnimGraphUpdater* pUpdater,
															const DynAnimChartRunTime** pChartOut)
{
	DynAnimChartStack* pChartStack = pUpdater->pChartStack;
	if (pChartStack)
	{
		// update the chart stack (has internal check for dirty bit)
		dynAnimChartStackSetFromStanceWords(pChartStack);

		EARRAY_FOREACH_REVERSE_BEGIN(pChartStack->eaChartStack, i);
		{
			const DynAnimChartRunTime* pChart = pUpdater->pChartStack->eaChartStack[i];
			DynAnimGraphChanceRef** const* peaUseDefaultChances;
			DynAnimGraph *pGraph;

			if (pUpdater->bMovement && pChartStack->directionMovementStanceCount) {
				peaUseDefaultChances = &pChart->eaMoveDefaultChances;
			} else {
				peaUseDefaultChances = &pChart->eaDefaultChances;
			}

			if (eaSize(peaUseDefaultChances) > 0)
			{
				//determine 0 <= rf < 1.0
				F32 rf = randomPositiveF32();

				//find the graph based on rf and stacked probabilities
				FOR_EACH_IN_EARRAY(*peaUseDefaultChances, DynAnimGraphChanceRef, pGraphChanceRef)
				{
					if (rf <= pGraphChanceRef->fChance &&
						(pGraph = GET_REF(pGraphChanceRef->hGraph)))
					{
						if(pChartOut){
							*pChartOut = pChart;
						}
						return pGraph;
					}
				}
				FOR_EACH_END;
			}
		}
		EARRAY_FOREACH_END;
	}

	assertmsg(0, "At least the base chart should have a default graph!");
}

static S32 dynAnimTemplateNodeIsLoop(const DynAnimTemplateNode* n){
	const DynAnimTemplateNode*	nStart = n;
	S32							everyOther = 0;
	
	while(n){
		if(n->defaultNext.p == nStart){
			return 1;
		}

		if(++everyOther & 1){
			nStart = nStart->defaultNext.p;
		}

		n = n->defaultNext.p;
	}

	return 0;
}

bool dynAnimGraphUpdaterResetToADefaultGraph(	DynSkeleton *pSkeleton,
												DynAnimGraphUpdater* pUpdater,
												const DynAnimGraph *pPostIdleGraph,
												const char *pcCallersReason,
												S32 onlyIfLooping,
												S32 forceIfSameChart,
												S32 interruptBits )
{
	const DynAnimGraph*		g = pUpdater->pCurrentGraph;
	const DynAnimGraphNode* gn = pUpdater->nodes[0].pGraphNode;
	const char*				reasonNoPostIdle = NULL;
	bool bStanceDisable = false;
	bool bDeathDisable = false;

	glrLog(	pUpdater->glr,
			"[dyn.AGU] %s: %s: current graph/node: [%s::%s].",
			__FUNCTION__,
			pUpdater->pChartStack && eaSize(&pUpdater->pChartStack->eaChartStack) ?
					pUpdater->pChartStack->eaChartStack[0]->pcName :
					"no charts in stack",
			g ? g->pcName : "none",
			gn ? gn->pTemplateNode->pcName : "none");

	pUpdater->bResetToDefaultWasAttempted = 1;

	if (pUpdater->bInPostIdle
		&& (
		!interruptBits ||
		interruptBits == DAGUI_MOVEMENT
		) && (
		pUpdater->pCurrentGraph->bOverrideAllBones			||
		pUpdater->pCurrentGraph->bOverrideDefaultMove		||
		pUpdater->pCurrentGraph->bOverrideMovement			||
		pUpdater->nodes[0].pGraphNode->bOverrideAllBones	||
		pUpdater->nodes[0].pGraphNode->bOverrideDefaultMove	||
		pUpdater->nodes[0].pGraphNode->bOverrideMovement
		) &&
		pUpdater->pChartStack &&
		dynAnimCompareStanceWordsPriorityLarge(pUpdater->eaPostIdleCallerStances, pUpdater->pChartStack->eaStanceWordsMutable, 1, 1, 1) == 0)
	{
		glrLog(	pUpdater->glr,
				"[dyn.AGU] %s: Skipping reset because current graph is a post-idle that overrides default movement and only stance changes were non-interrupting movement or details.",
				__FUNCTION__);
		return false;
	}

	if (!pPostIdleGraph)
	{
		if (pUpdater->bInPostIdle &&
			pUpdater->pChartStack)
		{
			S32 iStanceCompare = dynAnimCompareStanceWordsPriorityLarge(pUpdater->eaPostIdleCallerStances, pUpdater->pChartStack->eaStanceWordsMutable, 1, 1, 0);
			if (!interruptBits &&
				iStanceCompare == 0)
			{
				glrLog(	pUpdater->glr,
						"[dyn.AGU] %s: Skipping reset because current graph is a post-idle with identical calling stances to current stance chart (not including movement when initially called and any details).",
						__FUNCTION__);
				return false;
			}
			else if (iStanceCompare > 0)
			{
				bStanceDisable  = true;
			}
		}
	}
	else //if (pPostIdleGraph)
	{
		if (pSkeleton->bIsDead ||
			pSkeleton->bIsNearDead)
		{
			DynAnimTemplate *pPostIdleTemplate = GET_REF(pPostIdleGraph->hTemplate);
			if (!(SAFE_MEMBER(pPostIdleTemplate,eType) == eAnimTemplateType_Death ||
				SAFE_MEMBER(pPostIdleTemplate,eType) == eAnimTemplateType_NearDeath))
			{
				bDeathDisable = true;
			}
		}
	}

	if(onlyIfLooping)
	{
		if(pUpdater->bInPostIdle && !bStanceDisable)
		{
			glrLog(	pUpdater->glr,
					"[dyn.AGU] %s: Skipping reset because current graph is a post-idle.",
					__FUNCTION__);
			return false;
		}
		else if(!dynAnimTemplateNodeIsLoop(SAFE_MEMBER(gn, pTemplateNode))){
			glrLog(	pUpdater->glr,
					"[dyn.AGU] %s: Skipping reset because current graph is not looping.",
					__FUNCTION__);
			return false;
		}

		glrLog(	pUpdater->glr,
				"[dyn.AGU] %s: Resetting because graph is looping.",
				__FUNCTION__);

		if (bStanceDisable) {
			reasonNoPostIdle = "canceled looping post-idle due to stance";
		} else {
			reasonNoPostIdle = "canceled looping graph, start default graph";
		}
	}
	else if(!g){
		reasonNoPostIdle = "no current graph, start default graph";
	}
	else if (bStanceDisable) {
		reasonNoPostIdle = "post-idle disabled due to stance, start default graph";
	}
	else if (bDeathDisable) {
		reasonNoPostIdle = "only death or near death post-idles allowed during death or near death state";
	}
	else if(!pPostIdleGraph){
		reasonNoPostIdle = "no post-idle, start default graph";
	}
	else if(!dynAnimGraphUpdaterNoDefaultGraphsAreBlockTypes(pUpdater)){
		reasonNoPostIdle = "default has block graphs, start default graph";
	}

	if(!reasonNoPostIdle){
		DynAnimChartStack* pChartStack = pUpdater->pChartStack;

		if (pUpdater->bMovement &&
			pChartStack)
		{
			dynAnimGraphUpdaterMovementFallback(pSkeleton, pUpdater, false);
		}

		pUpdater->bInDefaultPostIdle |= pUpdater->bOnDefaultGraph;
		pUpdater->pPostIdleCaller = pUpdater->bInPostIdle ? pUpdater->pPostIdleCaller : pUpdater->pCurrentGraph;
		pUpdater->bInPostIdle = 1;
		pUpdater->pCurrentChartMutable = NULL;
		g = pUpdater->pCurrentGraphMutable = pPostIdleGraph;
		dynAnimGraphUpdaterGraphChanged(pSkeleton, pUpdater, pcCallersReason, "start post-idle graph");

		if (pUpdater->bMovement &&
			pChartStack)
		{
			if (pUpdater->bOnMovementGraph) {
				while (eaSize(&pChartStack->eaStanceFlags)) {
					dynAnimGraphUpdaterSetFlag(pUpdater, eaPop(&pChartStack->eaStanceFlags), 0);
				}
			} else {
				eaClear(&pChartStack->eaStanceFlags);
			}
		}
		
		glrLog(	pUpdater->glr,
				"[dyn.AGU] %s: Switched to post-idle graph: %s.",
				__FUNCTION__,
				g ? g->pcName : "none");
	}else{
		DynAnimChartStack* pChartStack = pUpdater->pChartStack;
		const DynAnimChartRunTime* pChart;

		if (pUpdater->bMovement	&&
			pChartStack)
		{
			bool bSetGraph = false;

			dynAnimGraphUpdaterMovementFallback(pSkeleton, pUpdater, pChartStack->interruptingMovementStanceCount);

			if (eaSize(&pChartStack->eaStanceKeys))
			{
				eaQSort(pChartStack->eaStanceKeys, dynAnimCompareStanceWordPriority);

				FOR_EACH_IN_EARRAY_FORWARDS(pChartStack->eaStanceKeys, const char, pcStanceKey)
				{
					if (dynAnimGraphUpdaterStartGraph(pSkeleton, pUpdater, pcStanceKey, 0, 0))
					{
						pChartStack->pcPlayingStanceKey = pcStanceKey;
						bSetGraph = true;

						eaCopy(&pChartStack->eaStanceFlags, &pSkeleton->eaStances[DS_STANCE_SET_PRE_UPDATE]);
						eaPushEArray(&pChartStack->eaStanceFlags, &pSkeleton->eaStances[DS_STANCE_SET_DEBUG]);
						if (TRUE_THEN_RESET(pSkeleton->bLanded))
							eaPush(&pChartStack->eaStanceFlags, s_pchStanceFlagLanded);

						if (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits) {
							dynDebugSkeletonStartGraph(pSkeleton, pUpdater, pcStanceKey, pUpdater->uidCurrentGraph);
						}

						break;
					}
				}
				FOR_EACH_END;

				eaClear(&pChartStack->eaStanceKeys);

				if (bSetGraph)
				{
					while (eaSize(&pChartStack->eaStanceFlags))
					{
						const char* pcStanceFlag = eaPop(&pChartStack->eaStanceFlags);
						if (dynAnimGraphUpdaterSetFlag(pUpdater, pcStanceFlag, 0) &&
							(	dynDebugState.danimShowBits ||
								dynDebugState.audioShowAnimBits))
						{
							dynDebugSkeletonSetFlag(pSkeleton, pUpdater, pcStanceFlag, 0);
						}
					}
					return true;
				}
			}

			pChartStack->pcPlayingStanceKey = NULL;
		}

		g = dynAnimGraphUpdaterGetADefaultGraph(pUpdater, &pChart);

		if(	!pUpdater->bOnDefaultGraph ||
			pChart != pUpdater->pCurrentChart ||
			forceIfSameChart)
		{
			pUpdater->pCurrentGraphMutable = g;
			pUpdater->pCurrentChartMutable = pChart;
			pUpdater->uidCurrentGraph = 0;
			pUpdater->bInPostIdle = 0;
			pUpdater->bInDefaultPostIdle = 0;
			dynAnimGraphUpdaterGraphChanged(pSkeleton, pUpdater, pcCallersReason, reasonNoPostIdle);
			dynAnimGraphUpdaterSetPostIdleCallerStances(pUpdater);
			dynSkeletonUpdateBoneVisibility(pSkeleton, NULL);
			pUpdater->bOnDefaultGraph = 1;
			
			if (pUpdater->bMovement &&
				pChartStack)
			{
				if (pUpdater->bOnMovementGraph) {
					while (eaSize(&pChartStack->eaStanceFlags)) {
						dynAnimGraphUpdaterSetFlag(pUpdater, eaPop(&pChartStack->eaStanceFlags), 0);
					}
				} else {
					eaClear(&pChartStack->eaStanceFlags);
				}
			}

			if(pUpdater->glr){
				char stanceWords[200];

				dynAnimChartGetStanceWords(pChart, SAFESTR(stanceWords));

				glrLog(	pUpdater->glr,
						"[dyn.AGU] %s: Switched to default graph: %s (type %s) (reason \"%s\") (stances: %s).",
						__FUNCTION__,
						g ? g->pcName : "none",
						g && GET_REF(g->hTemplate) ?
							StaticDefineIntRevLookup(eAnimTemplateTypeEnum, GET_REF(g->hTemplate)->eType) :
							"none",
						reasonNoPostIdle,
						stanceWords);
			}
		}
		else
		{
			if (pUpdater->bMovement &&
				pChartStack)
			{
				eaClear(&pChartStack->eaStanceFlags);
			}

			return false;
		}
	}

	if (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits) {
		dynDebugSkeletonStartGraph(pSkeleton, pUpdater, allocAddString("Null"), pUpdater->uidCurrentGraph);
	}
	return true;
}

void dynAnimGraphUpdaterChartStackChanged(DynSkeleton *pSkeleton, DynAnimGraphUpdater *pUpdater)
{
	// Check to see if we have a new default graph
	if(	(pUpdater->bOnDefaultGraph || pUpdater->bInPostIdle) &&
		!dynAnimGraphUpdaterIsADefaultGraph(pUpdater, false) &&
		(!pUpdater->bMovement || !dynAnimGraphUpdaterIsADefaultGraph(pUpdater, true)))
	{
		pUpdater->bResetToDefaultOnChartStackChange = dynAnimGraphUpdaterResetToADefaultGraph(pSkeleton, pUpdater, NULL, "stances changed", 0, 0, 0);
	}
}

bool dynAnimGraphUpdaterSetFlag(DynAnimGraphUpdater* pUpdater, const char* pcFlagToSet, U32 uid)
{
	DynAnimTemplate* pTemplate = pUpdater->pCurrentGraph?GET_REF(pUpdater->pCurrentGraph->hTemplate):NULL;

#if DYNANIMGRAPHUPDATER_DEBUG_PRINTANIMWORDS
		printfColor(COLOR_BLUE, "%s ", __FUNCTION__);
		printfColor(COLOR_BLUE|COLOR_BRIGHT, "%s (%u)\n", pcFlagToSet, uid);
#endif

	if (eaSize(SAFE_MEMBER_ADDR(pTemplate, eaFlags)) &&
		(	!uid ||
			pUpdater->uidCurrentGraph == uid))
	{
		const char* pcFlagToSetPooled = allocFindString(pcFlagToSet);
		if (eaFind(&pTemplate->eaFlags,    pcFlagToSetPooled) >= 0 &&
			eaFind(&pUpdater->eaFlagQueue, pcFlagToSetPooled) <  0 )
		{
			eaPush(&pUpdater->eaFlagQueue, pcFlagToSetPooled);
			return true;
		}
	}

	return false;
}

bool dynAnimGraphUpdaterSetDetailFlag(DynAnimGraphUpdater *pUpdater, const char *pcFlagToSet, U32 uid)
{
	DynAnimTemplate* pTemplate = pUpdater->pCurrentGraph?GET_REF(pUpdater->pCurrentGraph->hTemplate):NULL;

#if DYNANIMGRAPHUPDATER_DEBUG_PRINTANIMWORDS
		printfColor(COLOR_BLUE, "%s ", __FUNCTION__);
		printfColor(COLOR_BLUE|COLOR_BRIGHT, "%s (%u)\n", pcFlagToSet, uid);
#endif

	if (eaSize(SAFE_MEMBER_ADDR(pTemplate, eaFlags)) &&
		(	!uid ||
			pUpdater->uidCurrentGraph == uid))
	{
		const char* pcFlagToSetPooled = allocFindString(pcFlagToSet);
		if((pcFlagToSetPooled == s_pchFlagInterrupt  ||
			pcFlagToSetPooled == s_pchFlagActivate   ||
			pcFlagToSetPooled == s_pchFlagDeactivate ||
			pcFlagToSetPooled == s_pchFlagHitFront   ||
			pcFlagToSetPooled == s_pchFlagHitRear    ||
			pcFlagToSetPooled == s_pchFlagHitLeft    ||
			pcFlagToSetPooled == s_pchFlagHitRight   ||
			pcFlagToSetPooled == s_pchFlagHitTop     ||
			pcFlagToSetPooled == s_pchFlagHitBottom )
			&&
			eaFind(&pUpdater->eaFlagQueue, pcFlagToSetPooled) < 0)
		{
			eaPush(&pUpdater->eaFlagQueue, pcFlagToSetPooled);
			return true;
		}
	}

	return false;
}

void dynAnimGraphUpdaterSetOverrideTime(DynAnimGraphUpdater *pUpdater, F32 fTime, U32 uiApply)
{
	if (uiApply) {
		MIN1(fTime,10.f);
		pUpdater->fOverrideTime = pUpdater->fTimeOnCurrentGraph/30.0f + fTime;
		MAX1(pUpdater->fOverrideTime, 0.f);
		#if DYNANIMGRAPHUPDATER_DEBUG_PRINTANIMWORDS
			printfColor(COLOR_BLUE, "%s ", __FUNCTION__);
			printfColor(COLOR_BLUE|COLOR_BRIGHT, "%f = g%f + o%f (%u)\n", pUpdater->fOverrideTime, pUpdater->fTimeOnCurrentGraph/30.f, fTime, uiApply);
		#endif
	} else {
		pUpdater->fOverrideTime = -1.0f;
	}
}
