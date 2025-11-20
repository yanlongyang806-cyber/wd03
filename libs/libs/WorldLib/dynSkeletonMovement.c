
#include "dynSkeletonMovement.h"

#include "error.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "qsortG.h"
#include "StringCache.h"
#include "ThreadSafeMemoryPool.h"

#include "dynAnimChart.h"
#include "dynAnimGraph.h"
#include "dynAnimGraphUpdater.h"
#include "dynAnimTrack.h"
#include "dynFxManager.h"
#include "dynMove.h"
#include "dynMoveTransition.h"
#include "dynNodeInline.h"
#include "dynSeqData.h"
#include "dynSkeleton.h"

#include "wlCostume.h"
#include "wlState.h"

#include "dynSkeletonMovement_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

TSMP_DEFINE(DynMovementBlock);

DictionaryHandle hMovementSetDict;

extern SkelBoneVisibilitySets g_SkelBoneVisSets;

const char *pcStopped;


AUTO_RUN;
void dynSkeletonMovement_registerCommonStrings(void)
{
	if (!gConf.bUseMovementGraphs)
	{
		pcStopped = allocAddStaticString("Stopped");
	}
}

AUTO_RUN;
void registerMovementSetDict(void)
{
	// need to load this, or we'll get an error about unknown dictionary every time a chart loads & can't find it's old styled movement set
	hMovementSetDict = RefSystem_RegisterSelfDefiningDictionary("DynMovementSet", false, parse_DynMovementSet, true, true, NULL);
	if (!gConf.bUseMovementGraphs)
		TSMP_CREATE(DynMovementBlock, 128);
	if (isDevelopmentMode() || isProductionEditMode()) {
		resDictMaintainInfoIndex(hMovementSetDict, ".movementset", NULL, NULL, NULL, NULL);
	}
}

bool dynMovementSetVerify(DynMovementSet* pData)
{
	return true;
}

const char* pcMovementPrefixName = "dyn/movement";

static bool dynMovementSequenceFixup(DynMovementSequence* pMovementSequence)
{
	pMovementSequence->fAngle = RAD(pMovementSequence->fAngle);
	pMovementSequence->fAngleWidth = RAD(pMovementSequence->fAngleWidth);
	return true;
}

bool dynMovementSetFixup(DynMovementSet* pData, bool bPostText)
{
	char cName[256];

	getFileNameNoExt(cName, pData->pcFileName);
	pData->pcName = allocAddString(cName);

	if (bPostText)
	{
		FOR_EACH_IN_EARRAY(pData->eaMovementSequences, DynMovementSequence, pMovementSequence)
		{
			dynMovementSequenceFixup(pMovementSequence);
		}
		FOR_EACH_END;
	}

	FOR_EACH_IN_EARRAY(pData->eaMovementSequences, DynMovementSequence, pMovementSequence) {
		pMovementSequence->fCoverAngleMax = addAngle(pMovementSequence->fAngle,0.5f*pMovementSequence->fAngleWidth);
		pMovementSequence->fCoverAngleMin = subAngle(pMovementSequence->fAngle,0.5f*pMovementSequence->fAngleWidth);
	} FOR_EACH_END;

	return true;
}

static void dynMovementSetReloadCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);


	if(!ParserReloadFileToDictionary(relpath,hMovementSetDict))
	{
		CharacterFileError(relpath, "Error reloading DynMovementSet file: %s", relpath);
	}
	else
	{
		// nothing to do here
	}
	//costumeForceGlobalReload();
}

AUTO_FIXUPFUNC;
TextParserResult fixupMovementSet(DynMovementSet* pMovementSet, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
	if (!dynMovementSetVerify(pMovementSet) || !dynMovementSetFixup(pMovementSet, true))
		return PARSERESULT_INVALID; // remove this from the costume list
	xcase FIXUPTYPE_POST_BIN_READ:
	if (!dynMovementSetFixup(pMovementSet, false))
		return PARSERESULT_INVALID; // remove this from the costume list
	}

	return PARSERESULT_SUCCESS;
}

void dynMovementSetLoadAll(void)
{
	loadstart_printf("Loading DynMovementSet...");

	// optional for outsource build
	ParserLoadFilesSharedToDictionary("Sm_DynMovementSet", pcMovementPrefixName, ".movement", "DynMovementSet.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, hMovementSetDict);

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/movement/*.movement", dynMovementSetReloadCallback);
	}

	loadend_printf("done (%d DynMovementSets)", RefSystem_GetDictionaryNumberOfReferents(hMovementSetDict) );
}


void dynMovementBlockFree(DynMovementBlock* pMovementBlock)
{
	assert(!gConf.bUseMovementGraphs);

	eaDestroy(&pMovementBlock->eaSkeletalStancesOnTrigger);
	TSMP_FREE(DynMovementBlock, pMovementBlock);
}

const DynMovementSequence* dynSkeletonGetCurrentMovementSequence(const DynSkeleton* pSkeleton, DynMovementSequence **eaMovementSequences)
{
	const DynMovementState* m = &pSkeleton->movement;
	const DynAnimChartStack *cs = pSkeleton->movement.pChartStack;
	DynMovementSequence* pBestSequence = NULL;
	F32 fBestAngleDiff = -1.0f;

	assert(!gConf.bUseMovementGraphs);

	if (eaSize(&eaMovementSequences) == 1) {
		pBestSequence = eaMovementSequences[0];
	} else {
		FOR_EACH_IN_EARRAY(eaMovementSequences, DynMovementSequence, pMovementSequence)
		{
			if(m->pcDebugMoveOverride == pMovementSequence->pcMovementType)
			{
				pBestSequence = pMovementSequence;
				break;
			}
			if (cs &&
				(	cs->directionMovementStanceCount &&
					pMovementSequence->fAngleWidth > 0.f
					||
					!cs->directionMovementStanceCount &&
					pMovementSequence->fAngleWidth <= 0.f)
					||
					!cs &&
					pMovementSequence->fAngleWidth <= 0.f)
			{	
				F32 fAngleDiff = -1.0f;
				if (pMovementSequence->fAngleWidth)
				{
					fAngleDiff = fabsf(subAngle(pSkeleton->pGenesisSkeleton->fMovementAngle, pMovementSequence->fAngle));
				}
				if (!pBestSequence || fAngleDiff >= 0.0f && fAngleDiff < fBestAngleDiff)
				{
					pBestSequence = pMovementSequence;
					fBestAngleDiff = fAngleDiff;
				}
			}
		}
		FOR_EACH_END;
	}

	return pBestSequence;
}

static DynMovementBlock* dynMovementBlockCreate(const DynMovementSequence* pMovementSequence,
												const DynMoveSeq* pMoveSeqCycle,
												const DynAnimChartRunTime* pChart)
{
	DynMovementBlock* b = TSMP_ALLOC(DynMovementBlock);

	assert(!gConf.bUseMovementGraphs);

	ZeroStruct(b);
	b->pMovementSequence = pMovementSequence;
	b->pMoveSeqCycle = pMoveSeqCycle;
	b->pChart = pChart;
	b->bJumping = pChart->bHasJumpingStance;
	b->bFalling = pChart->bHasFallingStance;
	b->bRising  = pChart->bHasRisingStance;
	return b;
}

void dynMovementStateInit(DynMovementState* m, DynSkeleton* pSkeleton, DynAnimChartStack* pChartStack, const SkelInfo* pSkelInfo)
{
	assert(	!gConf.bUseMovementGraphs &&
			pChartStack &&
			eaSize(&pChartStack->eaChartStack));

	m->pChartStack = pChartStack;
	eaDestroyEx(&m->eaBlocks, dynMovementBlockFree);
	{
		const DynMovementSet *pMovementSet = GET_REF(m->pChartStack->eaChartStack[0]->hMovementSet);
		const DynMovementSequence* pSequence = dynSkeletonGetCurrentMovementSequence(pSkeleton,SAFE_MEMBER(pMovementSet,eaMovementSequences));
		const DynMoveSeq* pMoveSeq;
		const DynAnimChartRunTime* pChart;
		DynMovementBlock* b;

		// Find the initial movement move.

		if(dynAnimChartStackGetMoveSeq(pSkeleton, m->pChartStack, &pSequence, pSkelInfo, &pMoveSeq, &pChart, NULL)){
			b = dynMovementBlockCreate(pSequence, pMoveSeq, pChart);
			b->fBlendFactor = 1.f;
			eaPush(&m->eaBlocks, b);
		}
	}
}

void dynMovementStateDeinit(DynMovementState* pMovementState)
{
	assert(!gConf.bUseMovementGraphs);

	pMovementState->pChartStack = NULL;
	eaDestroyEx(&pMovementState->eaBlocks, dynMovementBlockFree);
}

void dynSkeletonMovementCalcBlockBank(DynMovementState *m, F32 *fBankMaxAngleOut, F32 *fBankScaleOut)
{
	DynMovementBlock *bPrev = NULL;
	bool bFirst = true;

	assert(!gConf.bUseMovementGraphs);

	//computation is based on running backwards and degree measurements
	FOR_EACH_IN_EARRAY(m->eaBlocks, DynMovementBlock, b)
	{
		const DynMoveSeq* seq = b->inTransition ?
									b->pMoveSeqTransition :
									b->pMoveSeqCycle;
		if (seq) {
			if (TRUE_THEN_RESET(bFirst)) {
				*fBankMaxAngleOut = seq->fBankMaxAngle;
				*fBankScaleOut = seq->fBankScale;
			}
			else {
				F32 fEasedBlendFactor;

				if      (b->inTransition)     fEasedBlendFactor = dynAnimInterpolationCalcInterp(b->fBlendFactor, &b->interpBlockPre);
				else if (bPrev->inTransition) fEasedBlendFactor = dynAnimInterpolationCalcInterp(b->fBlendFactor, &bPrev->interpBlockPost);
				else                          fEasedBlendFactor = b->fBlendFactor;
				assert(fEasedBlendFactor >= 0);

				*fBankMaxAngleOut = (1.0f-fEasedBlendFactor)*(*fBankMaxAngleOut) + fEasedBlendFactor*seq->fBankMaxAngle;
				*fBankScaleOut = (1.0f-fEasedBlendFactor)*(*fBankScaleOut) + fEasedBlendFactor*seq->fBankScale;
			}
			bPrev = b;
		}
	}
	FOR_EACH_END;
}

U32 dynSkeletonMovementIsStopped(DynMovementState *m)
{
	U32 uiResult = 1;

	assert(!gConf.bUseMovementGraphs);
	
	FOR_EACH_IN_EARRAY(m->eaBlocks, DynMovementBlock, b)
	{
		const DynMoveSeq *seq = b->inTransition ?
									b->pMoveSeqTransition :
									b->pMoveSeqCycle;
		if (seq) {
			if (b->pMovementSequence->pcMovementType != pcStopped) {
				uiResult = 0;
			}
		}
	}
	FOR_EACH_END;

	return uiResult;
}

F32 dynSkeletonMovementCalcBlockYaw(DynMovementState *m, F32 *fYawRateOut, F32 *fYawStoppedOut)
{
	F32 fResult = 0.0f, fAngle, fAnglePrev;
	DynMovementBlock *bPrev = NULL;
	bool bFirst = true;

	assert(!gConf.bUseMovementGraphs);

	*fYawRateOut += 15.0f;
	MIN1(*fYawRateOut,100.0f);

	//computation is based on running backwards and degree measurements
	FOR_EACH_IN_EARRAY(m->eaBlocks, DynMovementBlock, b)
	{
		const DynMoveSeq* seq = b->inTransition ?
									b->pMoveSeqTransition :
									b->pMoveSeqCycle;
		if (seq) {
			if (TRUE_THEN_RESET(bFirst)) {
				if (b->pMovementSequence->pcMovementType == pcStopped) {
					fAngle = *fYawStoppedOut;
				} else {
					fAngle = b->pMovementSequence->fAngle;
				}
				fResult = fAnglePrev = fAngle;
			}
			else {
				bool bSwap = false;
				F32 fEasedBlendFactor, fInitAngle;
				if      (b->inTransition)     fEasedBlendFactor = dynAnimInterpolationCalcInterp(b->fBlendFactor, &b->interpBlockPre);
				else if (bPrev->inTransition) fEasedBlendFactor = dynAnimInterpolationCalcInterp(b->fBlendFactor, &bPrev->interpBlockPost);
				else                          fEasedBlendFactor = b->fBlendFactor;
				assert(fEasedBlendFactor >= 0);

				fInitAngle = b->pMovementSequence->pcMovementType == pcStopped ?
																		fResult :
																		b->pMovementSequence->fAngle;

				if (fInitAngle < 0) fInitAngle += TWOPI;
				if (fResult    < 0) fResult += TWOPI;
				fAngle = fInitAngle;

				if (fabsf(fAngle-fAnglePrev) > PI)
				{
					F32 fAngleTemp = fAngle;
					if (fAngle < fAnglePrev) fAngleTemp += TWOPI;
					else                     fAnglePrev += TWOPI;
					if (fabsf(fAngleTemp-fAnglePrev) > 0.6*PI) {
						*fYawRateOut = 12.5f;
						bSwap = true;
					}
				}
				else if (fabsf(fAngle-fAnglePrev) > 0.6*PI) {
					*fYawRateOut = 12.5f;
					bSwap = true;
				}

				if (fabsf(fAngle-fResult) > PI) {
					if (fAngle < fResult) fAngle  += TWOPI;
					else                  fResult += TWOPI;
				}

				if (!bSwap)//(fabsf(fAngle-fResult) <= 0.9*PI)
					fResult = (1.0f-fEasedBlendFactor)*fResult + fEasedBlendFactor*fAngle;
				else
					fResult = fAngle;

				while (fResult >= TWOPI) fResult -= TWOPI;
				if (fResult > PI) fResult -= TWOPI;

				fAnglePrev = fInitAngle;
			}
			bPrev = b;
		}
	}
	FOR_EACH_END;

	*fYawStoppedOut = fResult;
	return fResult;
}

void dynSkeletonMovementUpdateBone( DynMovementState* m,
									DynTransform* pResult,
									const char* pcBoneTag,
									U32 uiBoneLOD,
									const DynTransform* pxBaseTransform,
									DynRagdollState* pRagdollState,
									DynSkeleton* pSkeleton) 
{
	DynTransform xTransform;
	DynMovementBlock *bPrev = NULL;
	bool bFirst = true;

	assert(!gConf.bUseMovementGraphs);

	FOR_EACH_IN_EARRAY(m->eaBlocks, DynMovementBlock, b)
	{
		const DynMoveSeq* seq = b->inTransition ?
									b->pMoveSeqTransition :
									b->pMoveSeqCycle;

		if (seq)
		{
			dynMoveSeqCalcTransform(seq,
									b->fFrameTime,
									&xTransform,
									pcBoneTag,
									pxBaseTransform,
									pRagdollState,
									pSkeleton->pRoot,
									true);

			if (TRUE_THEN_RESET(bFirst))
			{
				dynTransformCopy(&xTransform, pResult);
			}
			else
			{
				DynTransform xTemp;
				F32 fEasedBlendFactor;
				if      (b->inTransition)     fEasedBlendFactor = dynAnimInterpolationCalcInterp(b->fBlendFactor, &b->interpBlockPre);
				else if (bPrev->inTransition) fEasedBlendFactor = dynAnimInterpolationCalcInterp(b->fBlendFactor, &bPrev->interpBlockPost);
				else                          fEasedBlendFactor = b->fBlendFactor;
				assert(fEasedBlendFactor >= 0);
				dynTransformInterp(fEasedBlendFactor, pResult, &xTransform, &xTemp);
				dynTransformCopy(&xTemp, pResult);
			}

			bPrev = b;
		}
	}
	FOR_EACH_END;
}

static void dynSkeletonMovementTriggerFxEventsAtTime(	DynSkeleton* pSkeleton,
														const DynMoveSeq* pMoveSeq,
														F32 aFrame,
														F32 bFrame)
{
	assert(!gConf.bUseMovementGraphs);

	FOR_EACH_IN_EARRAY(pMoveSeq->eaDynMoveFxEvents, DynMoveFxEvent, pFx)
	{
		if (aFrame <= pFx->uiFrameTrigger && pFx->uiFrameTrigger < bFrame)
		{
			bool bUnique = true;

			//check for uniqueness
			//we don't want to send the same foot fall / grunt 2x when blending between directions
			//note that the cache is built and cleared every frame
			FOR_EACH_IN_EARRAY(pSkeleton->eaCachedSkelMoveFx, DynSkeletonCachedFxEvent, pChkFxEvent) 
			{
				if (pChkFxEvent->pcName   == pFx->pcFx &&
					pChkFxEvent->bMessage == pFx->bMessage)
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
				pCachedFx->pcName  = pFx->pcFx;
				pCachedFx->pUpdater = NULL;
				pCachedFx->bMessage = pFx->bMessage;
				eaPush(&pSkeleton->eaCachedSkelMoveFx, pCachedFx);
			}			
		}
	}
	FOR_EACH_END;
}

static U32 dynSkeletonMatchingStanceCount(	const DynSkeleton *pSkeleton,
											const char*const* stances,
											const DynAnimTimedStance **timedStances)
{
	U32 count = 0;

	assert(!gConf.bUseMovementGraphs);

	EARRAY_CONST_FOREACH_BEGIN(stances, i, iSize);
		ARRAY_FOREACH_BEGIN(pSkeleton->eaStances, j);
			if (eaFindString(&pSkeleton->eaStances[j], stances[i]) >= 0) {
				count++;
				break;
			}
		ARRAY_FOREACH_END;
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(timedStances, i, isize);
		ARRAY_FOREACH_BEGIN(pSkeleton->eaStances, j);
			int k = eaFindString(&pSkeleton->eaStances[j], timedStances[i]->pcName);
			if (k >= 0 && timedStances[i]->fTime <= pSkeleton->eaStanceTimers[j][k]) {
				count++;
				break;
			}
		ARRAY_FOREACH_END;
	EARRAY_FOREACH_END;

	return count;
}

static U32 dynAnimChartMatchingStanceCount(	const DynSkeleton *pSkeleton,
											const DynAnimChartRunTime *c,
											const char*const* stances,
											const DynAnimTimedStance **timedStances,
											bool bSourceComparison)
{
	U32 count = 0;

	assert(!gConf.bUseMovementGraphs);

	EARRAY_CONST_FOREACH_BEGIN(stances, i, iSize);
		if (eaFindString(&c->eaStanceWords, stances[i]) >= 0) {
			count++;
		}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(timedStances, i, isize);
		if (eaFindString(&c->eaStanceWords, timedStances[i]->pcName) >= 0) {
			if (timedStances[i]->fTime <= 0)
				count++;
			else {
				bool b = false;
				int k;
				ARRAY_FOREACH_BEGIN(pSkeleton->eaStances, j);
					k = eaFindString(&pSkeleton->eaStances[j], timedStances[i]->pcName);
					if (k >= 0 && timedStances[i]->fTime <= pSkeleton->eaStanceTimers[j][k]) {
						count++;
						b = true;
						break;
					}
				ARRAY_FOREACH_END;
				if (!b && bSourceComparison) {
					k = eaFindString(&pSkeleton->eaStancesCleared, timedStances[i]->pcName);
					if (k >= 0 && timedStances[i]->fTime <= pSkeleton->eaStanceTimersCleared[k]) {
						count++;
					}
				}
			}
		}
	EARRAY_FOREACH_END;

	return count;
}

static S32 dynMovementNoTransitions;
AUTO_CMD_INT(dynMovementNoTransitions, dynMovementNoTransitions);

static S32 dynAnimChartFindTransition(	const DynSkeleton *pSkeleton,
										DynAnimChartStack* chartStack,
										const DynAnimChartRunTime* chartSource,
										const DynMovementSequence* seqSource,
										const DynAnimChartRunTime* chartTarget,
										const DynMovementSequence* seqTarget,
										bool bForced,
										const DynMoveTransition**  tOut)
{
	assert(!gConf.bUseMovementGraphs);

	*tOut = NULL;

	if(dynMovementNoTransitions){
		return 0;
	}

	// update the chart stack (has internal check for dirty bit)
	// this isn't really needed here, but keeping in case something with transitions changes
	dynAnimChartStackSetFromStanceWords(chartStack);

	EARRAY_CONST_FOREACH_BEGIN(chartStack->eaChartStack[0]->eaMoveTransitions, i, isize);
	{
		const DynMoveTransition* t = chartStack->eaChartStack[0]->eaMoveTransitions[i];

		//make sure the forced case matches
		if (t->bForced == bForced)
		{
			//make sure the movement types match
			if ((!eaSize(&t->eaMovementTypesSource) || eaFind(&t->eaMovementTypesSource, seqSource->pcMovementType) >= 0) &&
				(bForced || !eaSize(&t->eaMovementTypesTarget) || eaFind(&t->eaMovementTypesTarget, seqTarget->pcMovementType) >= 0))
			{
				U32 srcCount = bForced ?
									dynSkeletonMatchingStanceCount(pSkeleton, t->eaStanceWordsSource, t->eaTimedStancesSource) :
									dynAnimChartMatchingStanceCount(pSkeleton, chartSource, t->eaStanceWordsSource, t->eaTimedStancesSource, true);

				U32 tgtCount = bForced ?
									0 :
									dynAnimChartMatchingStanceCount(pSkeleton, chartTarget, t->eaStanceWordsTarget, t->eaTimedStancesTarget, false);

				U32 srcReq = eaSize(&t->eaStanceWordsSource) + eaSize(&t->eaTimedStancesSource);
				U32 tgtReq = bForced ? 0 : eaSize(&t->eaStanceWordsTarget) + eaSize(&t->eaTimedStancesTarget);

				//make sure all transition words are part of both charts
				if (srcCount == srcReq && tgtCount == tgtReq)
				{
					if (*tOut == NULL) {
						*tOut = t;
					} else {
						if (dynAnimCompareJointStancesPriorityLarge((*tOut)->eaAllStanceWordsSorted,
																	(*tOut)->eaAllTimedStancesSorted,
																	t->eaAllStanceWordsSorted,
																	t->eaAllTimedStancesSorted) > 0) {
							*tOut = t;
						}
					}
				}
			}
		}
	}
	EARRAY_FOREACH_END;

	return !!*tOut;
}

static bool dynSkeletonMovementShouldEndForcedTransition(	const DynSkeleton *pSkeleton,
															const DynMovementBlock *pBlock,
															const DynMovementSequence *pMovementSequence)
{
	bool foundOnSource =
		!eaSize(&pBlock->pTransition->eaMovementTypesSource) ||
		eaFind(&pBlock->pTransition->eaMovementTypesSource, pMovementSequence->pcMovementType);

	bool foundOnTarget =
		!eaSize(&pBlock->pTransition->eaMovementTypesTarget) ||
		eaFind(&pBlock->pTransition->eaMovementTypesTarget, pMovementSequence->pcMovementType);

	assert(!gConf.bUseMovementGraphs);

	if (!foundOnSource && !foundOnTarget)
		return true;

	//don't check stances if either one is set to accept 'any'
	if (!eaSize(&pBlock->pTransition->eaStanceWordsSource) ||
		!eaSize(&pBlock->pTransition->eaStanceWordsTarget))
		return false;

	FOR_EACH_IN_EARRAY(pBlock->pTransition->eaJointStanceWordsSorted, const char, pcStance)
	{
		bool foundRequiredStance = false;

		ARRAY_FOREACH_BEGIN(pSkeleton->eaStances, i);
		{
			foundRequiredStance |=
				0 <= eaFind(&pSkeleton->eaStances[i], pcStance);
		}
		ARRAY_FOREACH_END;

		if (!foundRequiredStance)
			return true;
	}
	FOR_EACH_END;

	ARRAY_FOREACH_BEGIN(pSkeleton->eaStances, i);
		FOR_EACH_IN_EARRAY(pSkeleton->eaStances[i], const char, pcStance)
		{
			bool foundOnTrigger =
				0 <= eaFind(&pBlock->eaSkeletalStancesOnTrigger, pcStance);

			bool foundOnStance  =
				0 <= eaFind(&pBlock->pTransition->eaAllStanceWordsSorted, pcStance);

			if (!foundOnStance) {
				FOR_EACH_IN_EARRAY(pBlock->pTransition->eaAllTimedStancesSorted, DynAnimTimedStance, pTimedStance) {
					if (pTimedStance->pcName == pcStance) {
						foundOnStance = true;
						break;
					}
				} FOR_EACH_END;
			}

			if (!foundOnStance && !foundOnTrigger)
				return true;
		} 
		FOR_EACH_END;
	ARRAY_FOREACH_END;

	return false;
}

static F32 fLowerBlendSpeed = 3.0f;
AUTO_CMD_FLOAT(fLowerBlendSpeed, dynMovementLowerBlendSpeed);

void dynSkeletonMovementUpdate(DynSkeleton* pSkeleton, F32 fDeltaTime)
{
	DynMovementState*			m = &pSkeleton->movement;
	F32							fFrameDelta = fDeltaTime * 30.0f * dynDebugState.fAnimRate;
	WLCostume*					pCostume = GET_REF(pSkeleton->hCostume);
	const SkelInfo*				pSkelInfo = pCostume ? GET_REF(pCostume->hSkelInfo) : NULL;
	const DynMovementSet*		pMovementSet = (m->pChartStack && eaSize(&m->pChartStack->eaChartStack) ? GET_REF(m->pChartStack->eaChartStack[0]->hMovementSet) : NULL);
	const DynMovementSequence*	pSequenceNew;
	const DynMoveSeq*			pMoveSeqNew	= NULL;
	const DynAnimChartRunTime*	pChartNew	= NULL;
	bool						bParentInTransition = false;
	bool						bGraphBlendsMastered = false;
	int							iRegBlendDepth = 1;

	assert(!gConf.bUseMovementGraphs);

	if (!pSkelInfo)
		return;
	
	PERFINFO_AUTO_START_FUNC();

	if (pSkeleton->bMovementBlending)
	{
		const F32 blendStart = pSkeleton->fLowerBodyBlendFactor;

		S32	blendingOut =	pSkeleton->bOverrideMovement
							||
							!(SAFE_MEMBER(m->pChartStack,interruptingMovementStanceCount))	&&
							!(SAFE_MEMBER(m->pChartStack,directionMovementStanceCount))		&&
							!m->pcDebugMoveOverride &&
							(	eaSize(&m->eaBlocks) == 0 ||
									(	!m->eaBlocks[0]->inTransition &&
										!m->eaBlocks[0]->pMoveSeqCycle->bShowWhileStopped));

		if(blendingOut){
			// Blend out of movement.
			if (pSkeleton->bOverrideMovement &&
				pSkeleton->bSnapOverrideMovement) {
					pSkeleton->fLowerBodyBlendFactorMutable = 0.0f;
			} else if(pSkeleton->fLowerBodyBlendFactor > 0.0f){
				pSkeleton->fLowerBodyBlendFactorMutable -= fLowerBlendSpeed * fDeltaTime * pSkeleton->fFrozenTimeScale;
				MAX1(pSkeleton->fLowerBodyBlendFactorMutable, 0.0f);
			}
		}else{
			// Blend into movement.
			if(pSkeleton->fLowerBodyBlendFactor < 1.0f){
				pSkeleton->fLowerBodyBlendFactorMutable += fLowerBlendSpeed * fDeltaTime * pSkeleton->fFrozenTimeScale;
				MIN1(pSkeleton->fLowerBodyBlendFactorMutable, 1.0f);
			}
		}

		glrLog(	pSkeleton->glr,
				"[dyn.AGU] Blending %s movement %1.3f (from %1.3f, override %d, xz=%1.3f, y=%1.3f, interrupt=%u, direction=%u).",
				blendingOut ? "out of" : "into",
				pSkeleton->fLowerBodyBlendFactor,
				blendStart,
				pSkeleton->bOverrideMovement,
				pSkeleton->fCurrentSpeedXZ,
				pSkeleton->fCurrentSpeedY,
				SAFE_MEMBER(m->pChartStack,interruptingMovementStanceCount),
				SAFE_MEMBER(m->pChartStack,directionMovementStanceCount));
	}

	pSequenceNew = dynSkeletonGetCurrentMovementSequence(	pSkeleton,
															SAFE_MEMBER(pMovementSet,eaMovementSequences));

	bParentInTransition =	pSkeleton->pParentSkeleton &&
							eaSize(&pSkeleton->pParentSkeleton->movement.eaBlocks) &&
							pSkeleton->pParentSkeleton->movement.eaBlocks[0]->inTransition;

	// Push current sequence onto the blend queue.
	if (pSequenceNew)
	{
		const DynMovementBlock* bCur = eaHead(&m->eaBlocks);
		const DynMoveTransition *tForced = NULL;

		if (bCur &&
			!pSkeleton->bIsLunging		&&
			!pSkeleton->bWasLunging		&&
			!pSkeleton->bIsLurching		&&
			!pSkeleton->bWasLurching	&&
			(	!bCur->inTransition ||
				!bCur->pTransition->bForced))
		{
			dynAnimChartFindTransition(	pSkeleton,
										m->pChartStack,
										bCur->pChart,
										bCur->pMovementSequence,
										NULL,
										NULL,
										true,
										&tForced);

			//don't allow a forced transition to occur on a block that was blended out of it
			if (tForced && tForced == bCur->pFromTransition) {
				tForced = NULL;
			}
		}

		if (pSkeleton->bFrozen &&
			pSkeleton->fFrozenTimeScale <= 0.0f)
		{
			//don't add a new block even if the movement stance has changed
		} 
		else if(!dynAnimChartStackGetMoveSeq(	pSkeleton,
												m->pChartStack,
												&pSequenceNew,
												pSkelInfo,
												&pMoveSeqNew,
												&pChartNew,
												SAFE_MEMBER2(bCur,pMoveSeqCycle,pDynMove)))
		{
			// Some kind of error.
		}
		else if(!bCur									||
				tForced									||
				pSequenceNew != bCur->pMovementSequence ||
				pMoveSeqNew  != bCur->pMoveSeqCycle		||
				// special case to help sync rider with mount when animators repeat moves across types
				(	pSkeleton->bRider 
				 	&&
					(	bCur->bFalling != pChartNew->bHasFallingStance ||
						bCur->bJumping != pChartNew->bHasJumpingStance ||
						bCur->bRising  != pChartNew->bHasRisingStance)))
		{
			DynMovementBlock* b = dynMovementBlockCreate(pSequenceNew, pMoveSeqNew, pChartNew);
			const DynMovementSequence* pTmpMovSeq = bCur->pMovementSequence;
			const DynAnimChartRunTime *pTmpChart  = bCur->pChart;
			bool bEndForcedTransition = false;

			if(	bCur										&&
				!bCur->inTransition							&&
				bCur->bJumping == b->bJumping				&&
				bCur->bFalling == b->bFalling				&&
				bCur->bRising  == b->bRising				&&
				bCur->pMoveSeqCycle->fLength > 0.f			&&
				bCur->pMovementSequence->fAngleWidth > 0.f	&& //width is check for stopped
				b->pMovementSequence->fAngleWidth    > 0.f	)
			{
				F32 ratioMovePlayed = bCur->fFrameTime / bCur->pMoveSeqCycle->fLength;
				b->fFrameTime = ratioMovePlayed * b->pMoveSeqCycle->fLength;
			}

			if (SAFE_MEMBER(bCur, inTransition) &&
				bCur->pTransition->bForced)
			{
				DynMovementBlock *bCurMutable = eaHead(&m->eaBlocks);
				bCurMutable->pMoveSeqCycle	= b->pMoveSeqCycle;
				bCurMutable->pChart			= b->pChart;
				bCurMutable->bJumping		= b->bJumping;
				bCurMutable->bFalling		= b->bFalling;
				bCurMutable->bRising		= b->bRising;

				bEndForcedTransition = dynSkeletonMovementShouldEndForcedTransition(pSkeleton, bCurMutable, pSequenceNew);

				if (bEndForcedTransition)
				{
					bCurMutable->fEarlyForceEndByFrame = bCur->fFrameTime + bCur->interpBlockPost.fInterpolation;
					MIN1(bCurMutable->fEarlyForceEndByFrame, bCurMutable->pMoveSeqTransition->fLength);
					bCurMutable->fEarlyForceEndAtFrame = bCur->fFrameTime;
					bCurMutable->bEarlyForceEnd = 1;
				}
			}

			if (bCur					&&
				!pSkeleton->bIsLunging	&&
				!pSkeleton->bWasLunging	&&
				!pSkeleton->bIsLurching	&&
				!pSkeleton->bWasLurching)
			{
				const DynMoveTransition* t = NULL;

				if (tForced)
				{
					dynSkeletonCopyStanceWords(pSkeleton, &b->eaSkeletalStancesOnTrigger);
					t = tForced;
				}
				else
				{
					dynAnimChartFindTransition(	pSkeleton,
												m->pChartStack,
												pTmpChart,
												pTmpMovSeq,
												b->pChart,
												b->pMovementSequence,
												false,
												&t);
				}

				// Check if there's a transition available.
				if (t)
				{
					const DynMove* move = GET_REF(t->hMove);
					const DynMoveSeq* seq = move ? dynMoveSeqFromDynMove(move, pSkelInfo) : NULL;

					if(seq)
					{
						eaInsert(&m->eaBlocks, b, 0);

						b->fFrameTime = 0.f;
						b->pTransition = t;
						b->inTransition = 1;
						b->pMoveSeqTransition = seq;
						b->interpBlockPre  = t->interpBlockPre;
						b->interpBlockPost = t->interpBlockPost;

						if (bParentInTransition) {
							// we may want to error check here that both transitions feature the same set of stances & movement types & such
							DynMovementBlock *pParentBlock = pSkeleton->pParentSkeleton->movement.eaBlocks[0];
							F32 useRatioPre  = pParentBlock->interpBlockPre.fInterpolation  / pParentBlock->pMoveSeqTransition->fLength;
							F32 useRatioPost = pParentBlock->interpBlockPost.fInterpolation / pParentBlock->pMoveSeqTransition->fLength;
							b->interpBlockPre.fInterpolation  = useRatioPre  * b->pMoveSeqTransition->fLength;
							b->interpBlockPost.fInterpolation = useRatioPost * b->pMoveSeqTransition->fLength;
							b->bSyncToParent = true;
						}
					}
					else
					{
						dynMovementBlockFree(b);
					}
				}
				else if((	!SAFE_MEMBER(bCur, inTransition)	||
							!bCur->pTransition->bForced			||
							bEndForcedTransition
						) && (
							!bParentInTransition			||
							pChartNew->bHasJumpingStance	||
							pChartNew->bHasFallingStance	||
							pChartNew->bHasRisingStance
						))
				{
					eaInsert(&m->eaBlocks, b, 0);
				}
				else
				{
					dynMovementBlockFree(b);
				}
			}
			else if((	!SAFE_MEMBER(bCur, inTransition)||
						!bCur->pTransition->bForced		||
						bEndForcedTransition
					) && (
						!bParentInTransition			||
						pChartNew->bHasJumpingStance	||
						pChartNew->bHasFallingStance	||
						pChartNew->bHasRisingStance
					))
			{
				eaInsert(&m->eaBlocks, b, 0);
			}
			else
			{
				dynMovementBlockFree(b);
			}
		}
	}

	// Update blends.
	EARRAY_CONST_FOREACH_BEGIN(m->eaBlocks, i, isize);
	{
		DynMovementBlock* b = m->eaBlocks[i];
		DynMovementBlock* bNext = (i < isize - 1) ? m->eaBlocks[i+1] : NULL;
		F32 fBodyBlend;

		if (b->inTransition) {
			if (b->fFrameTime < b->interpBlockPre.fInterpolation)
			{
				b->fBlendFactor = b->fFrameTime / b->interpBlockPre.fInterpolation;
				fBodyBlend = MIN(b->fBlendFactor, 1.0f);

				if (!bGraphBlendsMastered)
				{
					if (pSkeleton->bMovementBlending &&
							(b->pTransition->bBlendLowerBodyFromGraph ||
							 b->pTransition->bBlendWholeBodyFromGraph))
					{
						MAX1(pSkeleton->fLowerBodyBlendFactorMutable, fBodyBlend);
						bGraphBlendsMastered = true;
					}
					if (b->pTransition->bBlendWholeBodyFromGraph)
					{
						MAX1(pSkeleton->fMovementSystemOverrideFactorMutable, fBodyBlend);
						bGraphBlendsMastered = true;
					}
				}
			} else {
				b->fBlendFactor = 1.0f;

				if (!bGraphBlendsMastered &&
					b->interpBlockPre.fInterpolation == 0)
				{
					if (b->pTransition->bBlendLowerBodyFromGraph ||
						b->pTransition->bBlendWholeBodyFromGraph)
					{
						pSkeleton->fLowerBodyBlendFactorMutable = 1.0f;
						bGraphBlendsMastered = true;
					}
					if (b->pTransition->bBlendWholeBodyFromGraph)
					{
						pSkeleton->fMovementSystemOverrideFactorMutable = 1.0f;
						bGraphBlendsMastered = true;
					}
				}
			}
		}
		else
		{
			if (SAFE_MEMBER(bNext, inTransition) &&
				bNext->pMoveSeqCycle == b->pMoveSeqCycle)
			{
				F32 fFramesIntp = bNext->bEarlyForceEnd ?
									bNext->fEarlyForceEndByFrame - bNext->fEarlyForceEndAtFrame :
									bNext->interpBlockPost.fInterpolation;
				F32 fFramesLeft = bNext->bEarlyForceEnd ?
									bNext->fEarlyForceEndByFrame - bNext->fFrameTime :
									bNext->pMoveSeqTransition->fLength - bNext->fFrameTime;

				if (0 < fFramesLeft && fFramesLeft <= fFramesIntp)
				{
					b->fBlendFactor = (fFramesIntp - fFramesLeft) / fFramesIntp;
					MIN1(b->fBlendFactor, 1.0f);
					fBodyBlend = MAX(1.0f-b->fBlendFactor, 0.0f);
					
					if (!bGraphBlendsMastered)
					{
						if (pSkeleton->bMovementBlending &&
								(bNext->pTransition->bBlendLowerBodyToGraph ||
								 bNext->pTransition->bBlendWholeBodyToGraph))
						{
							MIN1(pSkeleton->fLowerBodyBlendFactorMutable, fBodyBlend);
							bGraphBlendsMastered = true;
						}
						if (bNext->pTransition->bBlendWholeBodyToGraph)
						{
							MIN1(pSkeleton->fMovementSystemOverrideFactorMutable, fBodyBlend);
							bGraphBlendsMastered = true;
						}
					}
				} else {
					b->fBlendFactor = 1.0f;

					if (!bGraphBlendsMastered &&
						bNext->interpBlockPost.fInterpolation == 0)
					{
						if (bNext->pTransition->bBlendLowerBodyToGraph ||
							bNext->pTransition->bBlendWholeBodyToGraph)
						{
							pSkeleton->fLowerBodyBlendFactorMutable = 0.0f;
							bGraphBlendsMastered = true;
						}
						if (bNext->pTransition->bBlendWholeBodyToGraph)
						{
							pSkeleton->fMovementSystemOverrideFactorMutable = 0.0f;
							bGraphBlendsMastered = true;
						}
					}
				}
			} else {
				F32 fBlendRate;

				if (bNext) {
					fBlendRate = MAX(b->pMoveSeqCycle->fBlendRate, bNext->pMoveSeqCycle->fBlendRate);
				} else {
					fBlendRate = b->pMoveSeqCycle->fBlendRate;
				}

				if (!dynDebugState.bDisableTorsoPointingFix &&
					SAFE_MEMBER(b->pChart,uiNumMovementDirections) == 8)
				{
					fBlendRate *= iRegBlendDepth;
					iRegBlendDepth++;
				}

				b->fBlendFactor += fBlendRate * fDeltaTime;
				MIN1(b->fBlendFactor, 1.0f);
				iRegBlendDepth++;
			}
		}

		if (b->fBlendFactor >= 1.0f)
		{
			// Remove everything after me.
			eaRemoveTailEx(&m->eaBlocks, i+1, dynMovementBlockFree);
			break;
		}
	}
	EARRAY_FOREACH_END;

	// Advance all blocks, wrap times.
	FOR_EACH_IN_EARRAY(m->eaBlocks, DynMovementBlock, b)
	{
		F32 fFrameTimeBefore = b->fFrameTime;
		bool bTransitionEnding = false;
		bool bRandomMove = false;

		if(b->inTransition){
			assert(b->pMoveSeqTransition);

			if (b->bSyncToParent &&
				pSkeleton->pParentSkeleton)
			{
				U32 uiNumParentBlocks = eaSize(&pSkeleton->pParentSkeleton->movement.eaBlocks);
				U32 uiUseBlock = 0;

				//initialize as though we need to break out
				b->fFrameTime = b->pMoveSeqTransition->fLength;

				//attempt to determine an actual value, we'll sync to the 1st transition we find (since it may be blending out and hence > block 0)
				while (uiUseBlock < uiNumParentBlocks)
				{
					if (pSkeleton->pParentSkeleton->movement.eaBlocks[uiUseBlock]->inTransition)
					{
						DynMovementBlock *pParentBlock = pSkeleton->pParentSkeleton->movement.eaBlocks[uiUseBlock];
						F32 ratioMovePlayed = pParentBlock->fFrameTime / pParentBlock->pMoveSeqTransition->fLength;
						b->fFrameTime = ratioMovePlayed * b->pMoveSeqTransition->fLength;
						break;
					}
					uiUseBlock++;
				}
			}
			else
			{
				dynMoveSeqAdvanceTime(	b->pMoveSeqTransition,
										fFrameDelta,
										pSkeleton,
										&b->fFrameTime,
										b->pMoveSeqTransition->fSpeed * pSkeleton->fFrozenTimeScale,
										1);
			}
			
			dynSkeletonMovementTriggerFxEventsAtTime(	pSkeleton,
														b->pMoveSeqTransition,
														fFrameTimeBefore,
														b->fFrameTime);

			if ((ibIndex == 0 || b->pMoveSeqCycle != m->eaBlocks[ibIndex-1]->pMoveSeqCycle) &&
				b->fFrameTime >= b->pMoveSeqTransition->fLength - b->interpBlockPost.fInterpolation)
			{
				//insert a new movement at head
				DynMovementBlock *bPostT;
				bPostT = dynMovementBlockCreate(b->pMovementSequence, b->pMoveSeqCycle, b->pChart);
				bPostT->pFromTransition = b->pTransition;
				bPostT->bSyncToParent = pSkeleton->pParentSkeleton && eaSize(&pSkeleton->pParentSkeleton->movement.eaBlocks) && !pSkeleton->pParentSkeleton->movement.eaBlocks[0]->inTransition;
				eaInsert(&m->eaBlocks, bPostT, ibIndex);
				fFrameDelta = b->fFrameTime - (b->pMoveSeqTransition->fLength - b->interpBlockPost.fInterpolation);
				bTransitionEnding = true;
			}

			//make sure not to run past frames on the transition
			MIN1(b->fFrameTime, b->pMoveSeqTransition->fLength);
		}
		
		if (!b->inTransition || bTransitionEnding)
		{
			if (bTransitionEnding) {
				//switch the the post transition block we just added
				b = m->eaBlocks[ibIndex];
				fFrameTimeBefore = 0.f;
			}

			if (TRUE_THEN_RESET(b->bSyncToParent))
			{
				DynMovementBlock *pParentBlock = pSkeleton->pParentSkeleton->movement.eaBlocks[0];
				F32 ratioMovePlayed = pParentBlock->fFrameTime / pParentBlock->pMoveSeqCycle->fLength;
				b->fFrameTime = ratioMovePlayed * b->pMoveSeqCycle->fLength;
			}
			else
			{
				dynMoveSeqAdvanceTime(	b->pMoveSeqCycle,
										fFrameDelta,
										pSkeleton,
										&b->fFrameTime,
										b->pMoveSeqCycle->fSpeed * pSkeleton->fFrozenTimeScale,
										1);
			}

			dynSkeletonMovementTriggerFxEventsAtTime(	pSkeleton,
														b->pMoveSeqCycle,
														fFrameTimeBefore,
														b->fFrameTime);

			if(b->pMoveSeqCycle->fLength <= 0.f){
				b->fFrameTime = 0.f;
			}else{
				bool bLooped = false;

				while (b->fFrameTime >= b->pMoveSeqCycle->fLength)
				{
					fFrameTimeBefore = 0.0f;
					b->fFrameTime -= b->pMoveSeqCycle->fLength;
					bLooped = true;

					dynSkeletonMovementTriggerFxEventsAtTime(	pSkeleton,
																b->pMoveSeqCycle,
																fFrameTimeBefore,
																b->fFrameTime);
				}

				if (pSequenceNew	&&
					bLooped			&&
					ibIndex == 0	&&
					!bParentInTransition &&
					dynAnimChartStackGetMoveSeq(pSkeleton, m->pChartStack, &pSequenceNew, pSkelInfo, &pMoveSeqNew, &pChartNew, NULL) &&
					pMoveSeqNew != b->pMoveSeqCycle	&&
					pMoveSeqNew->fLength > b->fFrameTime)
				{
							//insert a new movement at head
							DynMovementBlock *bRand;
							bRand = dynMovementBlockCreate(pSequenceNew, pMoveSeqNew, pChartNew);
							fFrameDelta = b->fFrameTime;
							eaInsert(&m->eaBlocks, bRand, 0);
							bRandomMove = true;
				}
			}
		}

		if (bRandomMove)
		{
			b = m->eaBlocks[0];

			dynMoveSeqAdvanceTime(	b->pMoveSeqCycle,
									fFrameDelta,
									pSkeleton,
									&b->fFrameTime,
									b->pMoveSeqCycle->fSpeed * pSkeleton->fFrozenTimeScale,
									1);

			dynSkeletonMovementTriggerFxEventsAtTime(	pSkeleton,
														b->pMoveSeqCycle,
														0,
														b->fFrameTime);
		}
	}
	FOR_EACH_END;

	PERFINFO_AUTO_STOP();
}

void dynSkeletonMovementInitMatchJoints(DynSkeleton *pSkeleton, DynMovementState *m)
{
	assert(!gConf.bUseMovementGraphs);

	if (eaSize(&m->eaBlocks))
	{
		const DynMoveSeq *pMoveSeq = m->eaBlocks[0]->inTransition ?
										m->eaBlocks[0]->pMoveSeqTransition :
										m->eaBlocks[0]->pMoveSeqCycle;

		if (pMoveSeq)
		{
			FOR_EACH_IN_EARRAY(pMoveSeq->eaMatchBaseSkelAnim, DynMoveMatchBaseSkelAnim, pMatchBaseSkelAnim)
			{
				bool bCreate = true;

				FOR_EACH_IN_EARRAY(pSkeleton->eaSkeletalMovementMatchJoints, DynJointBlend, pMatchedJoint)
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
					const DynNode *pBone = dynSkeletonFindNode(pSkeleton, pMatchBaseSkelAnim->pcBoneName);
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
						eaPush(&pSkeleton->eaSkeletalMovementMatchJoints, pNewMatchJoint);
					}
				}
			}
			FOR_EACH_END;
		}
	}
}

void dynSkeletonMovementUpdateMatchJoints(DynSkeleton *pSkeleton, F32 fDeltaTime)
{
	assert(!gConf.bUseMovementGraphs);

	FOR_EACH_IN_EARRAY(pSkeleton->eaSkeletalMovementMatchJoints, DynJointBlend, pMatchJoint)
	{
		DynNode *pBone = dynSkeletonFindNodeNonConst(pSkeleton, pMatchJoint->pcName);
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
					eaRemoveFast(&pSkeleton->eaSkeletalMovementMatchJoints, ipMatchJointIndex);
					SAFE_FREE(pMatchJoint);
				}
			}
		}
		else //if (!pBone)
		{
			eaRemoveFast(&pSkeleton->eaSkeletalMovementMatchJoints, ipMatchJointIndex);
			SAFE_FREE(pMatchJoint);
		}
	}
	FOR_EACH_END;
}

void dynSkeletonMovementCalcMatchJointOffset(DynSkeleton *pSkeleton, DynJointBlend *pMatchJoint, Vec3 vReturn)
{
	const DynBaseSkeleton *pBaseSkeleton = NULL;
	const DynMoveSeq* pMoveSeq = NULL;
	F32 fFrame;
	Mat4 mRoot;

	assert(	!gConf.bUseMovementGraphs &&
			gConf.bNewAnimationSystem);

	pBaseSkeleton = dynSkeletonGetBaseSkeleton(pSkeleton);
	dynNodeGetWorldSpaceMat(pSkeleton->pRoot, mRoot, false);

	if (eaSize(&pSkeleton->movement.eaBlocks))
	{
		DynMovementBlock* pBlock = pSkeleton->movement.eaBlocks[0];
		pMoveSeq = pBlock->inTransition ?
					pBlock->pMoveSeqTransition :
					pBlock->pMoveSeqCycle;
		fFrame = pBlock->fFrameTime;
	}

	if (pBaseSkeleton && pMoveSeq)
	{
		const DynNode* aNodes[32];
		S32 iNumNodes, iProcessNode;

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

// This needs to be moved
//Sets the visibility of each bone on a skeleton based on stancewords, current chart, or (optionally) a passed-in GraphNode.
void dynSkeletonUpdateBoneVisibility(DynSkeleton* pSkeleton, const DynAnimGraphNode* pNode)
{
	int i, j;
	bool bUseDefault = true;
	bool bExcludeNonOverrideStances = false;

	//Base skeletons only. Processing bone vis nodes for sub-skeletons causes unfortunate issues.
	if (pSkeleton &&
		pSkeleton->pDrawSkel &&
		(	pSkeleton->pcCostumeFxTag || // since we can have pseudo-characters that are FX driven and attached to the pGenesisSkeleton
			!pSkeleton->pParentSkeleton))
	{
		SkelBoneVisibilitySetInfo** eaSetsToApply = NULL;
		SkelBoneVisibilitySetInfo** eaReplacementSets = NULL;
		eaStackCreate(&eaSetsToApply, 4);
		eaStackCreate(&eaReplacementSets, 4);

		eaCopy(&pSkeleton->pDrawSkel->eaHiddenBoneVisSetBonesOld,&pSkeleton->pDrawSkel->eaHiddenBoneVisSetBones);
		eaClear(&pSkeleton->pDrawSkel->eaHiddenBoneVisSetBones);

		if (!pNode)
		{
			DynAnimGraphUpdater*const*const* peaUpdaters = &pSkeleton->eaAGUpdater;
			for (i = 0; i < eaSize(peaUpdaters); i++)
			{
				if ((*peaUpdaters)[i]->nodes[0].pGraphNode && (*peaUpdaters)[i]->nodes[0].pGraphNode->eVisSet > -1)
				{
					pNode = (*peaUpdaters)[i]->nodes[0].pGraphNode;
					break;
				}
			}
		}
		if (pNode && pNode->eVisSet != -1)
		{
			bExcludeNonOverrideStances = true;

			//If a node with a vis set was passed in, it overrides everything else.
			for (j = 0; j < eaSize(&g_SkelBoneVisSets.ppSetInfo[pNode->eVisSet]->eaHideBoneNames); j++)
			{
				eaPushUnique(&pSkeleton->pDrawSkel->eaHiddenBoneVisSetBones, g_SkelBoneVisSets.ppSetInfo[pNode->eVisSet]->eaHideBoneNames[j]);
			}

			bUseDefault = false;
		}

		//Check all active stancewords for ones that affect bone visibility. If we were passed a Node with a valid vis set, only apply stances that have the override flag set.
		ARRAY_FOREACH_BEGIN(pSkeleton->eaStances, k);
		{
			for (i = 0; i < eaSize(&pSkeleton->eaStances[k]); i++)
			{
				DynAnimStanceData *stance_data;
				if (stashFindPointer(stance_list.stStances, pSkeleton->eaStances[k][i], &stance_data) && stance_data->eVisSet >= 0)
				{
					SkelBoneVisibilitySetInfo* pSet = g_SkelBoneVisSets.ppSetInfo[stance_data->eVisSet];
					if (pSet && (!bExcludeNonOverrideStances || pSet->bOverrideAlways))
					{
						if (pSet->pchReplaceSet)
							eaPush(&eaReplacementSets, pSet);
						else
							eaPush(&eaSetsToApply, pSet);
					}
				}
			}
		}
		ARRAY_FOREACH_END;

		//see if replacer sets should be applied
		for (i = eaSize(&eaReplacementSets)-1; i >= 0; i--)
		{
			for (j = eaSize(&eaSetsToApply)-1; j >= 0; j--)
			{
				if (eaReplacementSets[i]->pchReplaceSet == eaSetsToApply[j]->pchName)
				{
					eaRemoveFast(&eaSetsToApply, j);
					eaPush(&eaSetsToApply, eaReplacementSets[i]);
					break;
				}
			}
		}

		for (i = eaSize(&eaSetsToApply)-1; i >= 0; i--)
		{
			SkelBoneVisibilitySetInfo* pSet = eaSetsToApply[i];
			for (j = 0; j < eaSize(&pSet->eaHideBoneNames); j++)
			{
				eaPushUnique(&pSkeleton->pDrawSkel->eaHiddenBoneVisSetBones, pSet->eaHideBoneNames[j]);
			}

			if (!pSet->bOverlay) {
				bUseDefault = false;
			}
		}

		//If we didn't find anything yet, default to our chart's visibility set.
		if (bUseDefault && pSkeleton->eaAGUpdater && pSkeleton->eaAGUpdater[0] && pSkeleton->eaAGUpdater[0]->pCurrentChart && pSkeleton->eaAGUpdater[0]->pCurrentChart->eVisSet > -1)
		{
			S32 iBoneVis = pSkeleton->eaAGUpdater[0]->pCurrentChart->eVisSet;

			for (j = 0; j < eaSize(&g_SkelBoneVisSets.ppSetInfo[iBoneVis]->eaHideBoneNames); j++)
			{
				eaPushUnique(&pSkeleton->pDrawSkel->eaHiddenBoneVisSetBones, g_SkelBoneVisSets.ppSetInfo[iBoneVis]->eaHideBoneNames[j]);
			}
		}

		//Sort the hidden bones, compare them vs. last time. Only regenerate the skeleton if necessary.
		//ptrCmp because these are pooled strings.
		eaQSort(pSkeleton->pDrawSkel->eaHiddenBoneVisSetBones, ptrCmp);
		if (eaSize(&pSkeleton->pDrawSkel->eaHiddenBoneVisSetBones) != eaSize(&pSkeleton->pDrawSkel->eaHiddenBoneVisSetBonesOld))
		{
			pSkeleton->pDrawSkel->bUpdateDrawInfo = true;
		}
		else
		{
			for (i = 0; i < eaSize(&pSkeleton->pDrawSkel->eaHiddenBoneVisSetBones); i++)
			{
				if (pSkeleton->pDrawSkel->eaHiddenBoneVisSetBones[i] != pSkeleton->pDrawSkel->eaHiddenBoneVisSetBonesOld[i])
				{
					pSkeleton->pDrawSkel->bUpdateDrawInfo = true;
					break;
				}
			}
		}
	}
}

#include "dynSkeletonMovement_h_ast.c"
