

#include "dynSequencer.h"

#include "MemoryPool.h"
#include "rand.h"
#include "EString.h"
#include "auto_float.h"
#include "stringcache.h"


#include "wlstate.h"
#include "wlCostume.h"
#include "dynAnimTrack.h"
#include "dynMove.h"
#include "dynDraw.h"
#include "dynNodeInline.h"
#include "dynAction.h"
#include "dynAnimOverride.h"
#include "dynAnimPhysInfo.h"
#include "dynAnimPhysics.h"
#include "dynFx.h"
#include "dynFxParticle.h"

#include "dynSeqData.h"

#include "dynSequencerPrivate.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

static DynSequencer** eaGlobalSequencerList;

MP_DEFINE(DynSequencer);
#define DYN_DEFAULT_DYN_SEQUENCER_COUNT 1024

static void dynSkeletonCalculateLOD(SA_PARAM_NN_VALID DynSkeleton* pSkeleton);

static DynBitFieldGroup debugBFG;

bool dynSeqLoggingEnabled;

const char *pcDynSeqBitForward;
const char *pcDynSeqBitBackward;
const char *pcDynSeqBitMoveLeft;
const char *pcDynSeqBitMoveRight;

static void	dynSeqSetNextAction(SA_PARAM_NN_VALID DynSequencer* pSqr, SA_PARAM_NN_VALID const DynAction* pNewAction);
static bool dynNewNextSeqBlockFromBits(SA_PARAM_NN_VALID DynSequencer* pSqr, SA_PARAM_OP_VALID const DynBitField* pBits, SA_PARAM_OP_VALID DynSkeleton* pSkeleton);
static bool dynSeqAdvanceAction(SA_PARAM_NN_VALID DynSequencer* pSqr, SA_PARAM_OP_VALID DynSkeleton* pSkeleton);
static void dynSeqDebugUpdateDebugBits(SA_PARAM_NN_VALID DynSequencer* pSqr);

void dynDebugSetSequencer(DynSkeleton* pSkeleton)
{
	dynSeqPushBitFieldFeed(pSkeleton, &debugBFG);
}

AUTO_CMD_INT(dynSeqLoggingEnabled, danimLogging) ACMD_CATEGORY(dynAnimation) ACMD_CMDLINE;

static int iMaxPrevActions = MAX_PREV_ACTIONS;

AUTO_COMMAND ACMD_CATEGORY(dynAnimation);
void danimSequencerStackMax(int iNum)
{
	iMaxPrevActions = CLAMP(iNum, 1, MAX_PREV_ACTIONS);
}

void dynSequencerLogEx(DynSequencer* pSeq, const char* format, ...)
{
	VA_START(va, format);
		estrConcatfv(&pSeq->esMMLog, format, va);
	VA_END();
}

const char* dynSequencerGetLog(DynSequencer* pSqr)
{
	if (!dynSeqLoggingEnabled)
		return "[sequencer] Sequencer logging disabled, use /danimLogging 1 to enable.";
	return pSqr->esMMLog;
}

void dynSequencerFlushLog(DynSequencer* pSqr)
{
	estrClear(&pSqr->esMMLog);
	estrConcatf(&pSqr->esMMLog, "[sequencer.%s] ", pSqr->pcSequencerName);
}

__forceinline static void dynAdvanceToNextSequence(SA_PARAM_NN_VALID DynSequencer* pSqr, DynFxManager* pFxManager)
{
	dynSequencerLog(pSqr, "Advancing to next sequence %s\n", SAFE_MEMBER(pSqr->nextSeq.pSeq, pcName));
	if (pFxManager && pSqr->currSeq.pSeq != pSqr->nextSeq.pSeq)
	{
		FOR_EACH_IN_EARRAY(pSqr->currSeq.pSeq->eaOnExitFXMessages, DynAnimFXMessage, pMessage)
		{
			dynFxManBroadcastMessage(pFxManager, pMessage->pcMessage);
		}
		FOR_EACH_END;
	}
	memcpy(&pSqr->currSeq, &pSqr->nextSeq, sizeof(pSqr->currSeq));
	pSqr->nextSeq.bAdvancedTo = true;
}

__forceinline static void dynSequencerAdvancePrevActions(DynSequencer* pSqr)
{
	int i;
	for (i=iMaxPrevActions-1; i>0; --i)
	{
		memcpy(&pSqr->prevActions[i],&pSqr->prevActions[i-1],sizeof(pSqr->prevActions[0]));
	}
	memcpy(&pSqr->prevActions[0], &pSqr->currAction, sizeof(pSqr->prevActions[0]));
}

__forceinline static void dynSequencerResetState(DynSequencer* pSqr)
{
	// Call this stream to init all of the blocks to the default idle animation
	memset(&pSqr->currAction, 0, sizeof(DynActionBlock));
	memset(&pSqr->prevActions, 0, sizeof(DynActionBlock) * MAX_PREV_ACTIONS);
	memset(&pSqr->nextAction, 0, sizeof(DynActionBlock));
	memset(&pSqr->currSeq, 0, sizeof(DynSeqBlock));
	memset(&pSqr->nextSeq, 0, sizeof(DynSeqBlock));

	pSqr->uiNumAnimOverrides = 0;


	dynNewNextSeqBlockFromBits(pSqr, NULL, NULL);
	dynSeqAdvanceAction(pSqr, NULL);
	dynSequencerAdvancePrevActions(pSqr);

	pSqr->bReset = false;
	pSqr->bRunSinceReset = false;
}

void dynSequencerResetAll(void)
{
	FOR_EACH_IN_EARRAY(eaGlobalSequencerList, DynSequencer, pSqr)
		pSqr->bReset = true;
	FOR_EACH_END
}

static DynBit iNOLODBit = 0;

void dynSeqFindNOLODBitIndex(void)
{
	iNOLODBit = dynBitFromName("NOLOD");
}


DynSequencer* dynSequencerCreate(const WLCostume* pCostume, const char* pcSequencerName, U32 uiRequiredLOD, bool bNeverOverride, bool bOverlay)
{
	DynSequencer* pSqr;
	SkelInfo* pSkelInfo = GET_REF(pCostume->hSkelInfo);
	
    MP_CREATE_ALIGNED(DynSequencer, DYN_DEFAULT_DYN_SEQUENCER_COUNT, 16);
	
    pSqr = MP_ALLOC(DynSequencer);
	pSqr->pcSequencerName = pcSequencerName;
	pSqr->uiRequiredLOD = uiRequiredLOD;
	pSqr->uiSeed = randomU32();
	pSqr->bNeverOverride = bNeverOverride;
	pSqr->bOverlay = bOverlay;
	pSqr->fOverlayBlend = 0.f;
	estrCreate(&pSqr->esMMLog);

	if (!SET_HANDLE_FROM_REFERENT("SkelInfo", (SkelInfo*)pSkelInfo, pSqr->hSkelInfo))
	{
		Errorf("Unable to create reference to skelinfo, can not create sequencer!");
		return NULL;
	}

	dynSequencerResetState(pSqr);

	/*
	dynSeqSetNextAction(pSqr, pSqr->nextSeq.pSeq->eaActions[0]);
	memcpy(&pSqr->currAction, &pSqr->nextAction, sizeof(pSqr->currAction));
	pSqr->currAction.bInterpolates = pSqr->nextAction.bInterpolates = false;
	dynAdvanceToNextSequence(pSqr);
	*/
	eaPush(&eaGlobalSequencerList, pSqr);
	return pSqr;
}

void dynSequencerFree(DynSequencer* pToFree)
{
	eaFindAndRemoveFast(&eaGlobalSequencerList, pToFree);
	estrDestroy(&pToFree->esMMLog);
	REMOVE_HANDLE(pToFree->hSkelInfo);
	MP_FREE(DynSequencer, pToFree);
}

void dynSeqPushBitFieldFeed(DynSkeleton* pSkeleton, DynBitFieldGroup* pBFGFeed)
{
	eaPush(&pSkeleton->eaBitFieldFeeds, pBFGFeed);
}

void dynSeqRemoveBitFieldFeed(DynSkeleton* pSkeleton, DynBitFieldGroup* pBFGFeed)
{
	eaFindAndRemove(&pSkeleton->eaBitFieldFeeds, pBFGFeed);
}

// Call this whenever the nextseq changes or the current action changes
static bool dynSeqCheckIfNextInterrupts( SA_PARAM_NN_VALID DynSequencer* pSqr ) 
{
	bool bInterrupts = true;
	bool bActionAllowsSeqRestart = (pSqr->currAction.pAction && pSqr->currAction.pAction->bSeqRestartAllowed);
		
	dynSequencerLog(pSqr, "Checking if next sequence %s interrupts current sequence %s\n", SAFE_MEMBER(pSqr->nextSeq.pSeq, pcName), SAFE_MEMBER(pSqr->currSeq.pSeq,pcName));
	// Check for interruption, first on the seq name itself, then the member groups
	if ( !bActionAllowsSeqRestart && pSqr->nextSeq.pSeq == pSqr->currSeq.pSeq)
	{
		// Can't interrupt ourselves
		dynSequencerLog(pSqr, "Next sequencer is the same as current sequencer, and ActionAllowsSeqRestart is not set, so no interruption!\n");
		bInterrupts = false;
	}
	else if ( pSqr->nextSeq.pSeq->bInterruptEverything )
	{
		dynSequencerLog(pSqr, "Next sequencer %s has InterruptEverything set!", SAFE_MEMBER(pSqr->nextSeq.pSeq,pcName));
		return true;
	}
	else if ( pSqr->currAction.pAction )
	{
		dynSequencerLog(pSqr, "Checking if current action %s is interrupted by next sequencer %s: ", SAFE_MEMBER(pSqr->currAction.pAction, pcName), SAFE_MEMBER(pSqr->nextSeq.pSeq,pcName));
		bInterrupts = dynActionIsInterruptibleBy(pSqr->currAction.pAction, pSqr->nextSeq.pSeq->pcName);
		{
			const U32 uiNumIRQGroups = eaSize(&pSqr->nextSeq.pSeq->eaMember);
			U32 uiIRQGroup;
			for (uiIRQGroup=0; !bInterrupts && uiIRQGroup<uiNumIRQGroups; ++uiIRQGroup)
			{
				const char* pcInterrupter = pSqr->nextSeq.pSeq->eaMember[uiIRQGroup];
				if ( dynActionIsInterruptibleBy(pSqr->currAction.pAction, pcInterrupter) )
				{
					bInterrupts = true;
				}
			}
		}
		if (bInterrupts)
			dynSequencerLog(pSqr, "YES\n")
		else
			dynSequencerLog(pSqr, "NO\n")
	}
	return bInterrupts;
}

static bool dynNewNextSeqBlockFromBits(DynSequencer* pSqr, const DynBitField* pBits, DynSkeleton* pSkeleton)
{
	// First, update from feeds
	DynBitField oldBits;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	dynBitFieldCopy(&pSqr->bits, &oldBits);
	dynBitFieldClear(&pSqr->bits);

	dynSequencerLog(pSqr, "Determining what next sequence currently is\n");

	// Set the bit field from the feeds
	if (pBits)
		dynBitFieldCopy(pBits, &pSqr->bits);

	/*
	if (pSkeleton)
	{
		dynBitFieldSetAllFromBitField(&pSqr->bits, &pSkeleton->actionFlashBits);
	}
	*/

	if (dynSeqLoggingEnabled)
	{
		char cBitString[1024];
		dynBitFieldWriteBitString(SAFESTR(cBitString), &oldBits);
		dynSequencerLog(pSqr, "Old Bits: %s\n", cBitString);

		dynBitFieldWriteBitString(SAFESTR(cBitString), &pSqr->bits);
		dynSequencerLog(pSqr, "New Bits: %s\n", cBitString);
	}

	if (pSkeleton)
	{
		DynSkeleton* pSkeletonWithTarget = pSkeleton->pGenesisSkeleton;

		if (pSkeletonWithTarget && (pSkeletonWithTarget->bUseTorsoPointing || pSkeletonWithTarget->bUseTorsoDirections))
		{
			{
				dynBitActOnByName(&pSqr->bits, "FORWARD", edba_Clear);

				switch (pSkeletonWithTarget->eTargetDir)
				{
					xcase ETargetDir_Forward:
				{
					dynBitActOnByName(&pSqr->bits, "FORWARD", edba_Set);
				}
				xcase ETargetDir_Back:
				{
					dynBitActOnByName(&pSqr->bits, "BACKWARD", edba_Set);
				}
				xcase ETargetDir_Left:
				{
					dynBitActOnByName(&pSqr->bits, "MOVELEFT", edba_Set);
				}
				xcase ETargetDir_Right:
				{
					dynBitActOnByName(&pSqr->bits, "MOVERIGHT", edba_Set);
				}
				xcase ETargetDir_LeftBack:
				{
					dynBitActOnByName(&pSqr->bits, "MOVELEFT", edba_Set);
					dynBitActOnByName(&pSqr->bits, "BACKWARD", edba_Set);
				}
				xcase ETargetDir_RightBack:
				{
					dynBitActOnByName(&pSqr->bits, "MOVERIGHT", edba_Set);
					dynBitActOnByName(&pSqr->bits, "BACKWARD", edba_Set);
				}
				xcase ETargetDir_LeftForward:
				{
					dynBitActOnByName(&pSqr->bits, "MOVELEFT", edba_Set);
					dynBitActOnByName(&pSqr->bits, "FORWARD", edba_Set);
				}
				xcase ETargetDir_RightForward:
				{
					dynBitActOnByName(&pSqr->bits, "MOVERIGHT", edba_Set);
					dynBitActOnByName(&pSqr->bits, "FORWARD", edba_Set);
				}
				}

				/*
				if (dynBitActOnByName(&pSqr->bits, "MOVE", edba_Test))
				{
				switch (pSkeletonWithTarget->eMovementSpeed)
				{
				xcase EMovementSpeed_Trot:
				{
				//dynBitActOnByName(&pSqr->bits, "TROT", edba_Set);
				}
				xcase EMovementSpeed_Run:
				{
				dynBitActOnByName(&pSqr->bits, "RUN", edba_Set);
				}
				}
				}
				*/

				switch (pSkeletonWithTarget->eTurnState)
				{
					xcase ETurnState_Left:
				{
					dynBitActOnByName(&pSqr->bits, "TURNLEFT", edba_Set);
				}
				xcase ETurnState_Right:
				{
					dynBitActOnByName(&pSqr->bits, "TURNRIGHT", edba_Set);
				}
				}
			}
		}
	}

	// If something has changed, clear the nextseq so it will be reprocessed
	if ( !dynBitFieldsAreEqual(&pSqr->bits, &oldBits))
	{
		pSqr->nextSeq.pSeq = NULL;
	}

	if ( !pSqr->nextSeq.pSeq )
	{
		dynSequencerLog(pSqr, "Bits have changed, new next DynSequence!\n");
		pSqr->nextSeq.pSeq = dynSeqDataFromBits(pSqr->pcSequencerName, &pSqr->bits, &pSqr->nextSeq.bDefaultSeq);
		pSqr->nextSeq.bAdvancedTo = false;
		dynSequencerLog(pSqr, "Next DynSequence: %s\n", pSqr->nextSeq.pSeq->pcName);
		PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
		return true;
	}
	dynSequencerLog(pSqr, "Bits are same, no change in next DynSequence.\n");
	PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
	return false;
}

const char* dynSeqGetCurrentSequenceName( DynSequencer* pSqr )
{
	if (pSqr->currAction.pAction && pSqr->currAction.pAction->pParentSeq)
		return pSqr->currAction.pAction->pParentSeq->pcName;
	return NULL;
}

const char* dynSeqGetCurrentActionName( DynSequencer* pSqr )
{
	return pSqr->currAction.pAction?pSqr->currAction.pAction->pcName:NULL;
}

F32 dynSeqGetCurrentActionFrame( DynSequencer* pSqr )
{
	return pSqr->currAction.fFrameTime;
}

const char* dynSeqGetName( DynSequencer* pSqr )
{
	return pSqr->pcSequencerName?pSqr->pcSequencerName:"Default";
}

const char* dynSeqGetCurrentMoveName( DynSequencer* pSqr )
{
	if ( pSqr->currAction.pMoveSeq && pSqr->currAction.pMoveSeq->pDynMove )
		return pSqr->currAction.pMoveSeq->pDynMove->pcName;
	return NULL;
}

const char* dynSeqGetNextActionName( DynSequencer* pSqr )
{
	return pSqr->nextAction.pAction?pSqr->nextAction.pAction->pcName:NULL;
}

const char* dynSeqGetPreviousActionName( DynSequencer* pSqr )
{
	return pSqr->prevActions[0].pAction?pSqr->prevActions[0].pAction->pcName:NULL;
}

const DynBitField* dynSeqGetBits(DynSequencer* pSqr)
{
	return &pSqr->bits;
}

const DynAction* dynSeqGetCurrentAction( SA_PARAM_NN_VALID DynSequencer* pSqr)
{
	return pSqr->currAction.pAction;
}

static const DynAction* dynSeqGetFirstAction( SA_PARAM_NN_VALID DynBitField* pBits, SA_PARAM_NN_VALID const DynSeqData* pSequence, SA_PARAM_NN_VALID DynSequencer* pSqr)
{
	const U32 uiNumActions = eaSize(&pSequence->eaActions);
	U32 uiActionIndex;
	const DynActFirstIfBlock* pBestFirstIf = NULL;
	dynSequencerLog(pSqr, "Calculating first action to use in Sequence %s.\n", pSequence->pcName);
	for (uiActionIndex=0; uiActionIndex<uiNumActions; ++uiActionIndex)
	{
		const DynAction* pAction = pSequence->eaActions[uiActionIndex];
		const U32 uiNumFirstIfBlocks = eaSize(&pAction->eaFirstIf);
		U32 uiFirstIfIndex;
		for (uiFirstIfIndex=0; uiFirstIfIndex<uiNumFirstIfBlocks; ++uiFirstIfIndex)
		{
			DynActFirstIfBlock* pFirstIf = pAction->eaFirstIf[uiFirstIfIndex];
			if (dynBitFieldSatisfiesLogicBlock(pBits, &pFirstIf->logic))
			{
				if (!pBestFirstIf)
					pBestFirstIf = pFirstIf;
				else if ( pFirstIf->fPriority > pBestFirstIf->fPriority)
				{
					pBestFirstIf = pFirstIf;
				}
			}
		}
	}
	if ( pBestFirstIf )
	{
		dynSequencerLog(pSqr, "Chose FirstIf Action %s.\n", pBestFirstIf->pAction->pcName);
		return pBestFirstIf->pAction;
	}
	else
		dynSequencerLog(pSqr, "No FirstIf's in sequence, so using DefaultFirstAction %s.\n", pSequence->pDefaultFirstAction->pcName);
	return pSequence->pDefaultFirstAction;
}




static void applyPreYaw(DynTransform* pTransform, F32 fYaw)
{
	Vec3 vInPYR;
	Quat qPitch, qInvPitch;
	Quat qRoll, qInvRoll;
	Quat qYaw;
	Quat qTempA, qTempB;
	Vec3 vTemp;
	quatToPYR(pTransform->qRot, vInPYR);
	rollQuat(-vInPYR[2], qInvRoll);
	rollQuat(vInPYR[2], qRoll);
	pitchQuat(-vInPYR[0], qInvPitch);
	pitchQuat(vInPYR[0], qPitch);

	yawQuat(-fYaw, qYaw);

	quatMultiply(qInvRoll, pTransform->qRot, qTempA);
	quatMultiply(qInvPitch, qTempA, qTempB);
	quatMultiply(qTempB, qYaw, qTempA);
	quatMultiply(qPitch, qTempA, qTempB);
	quatMultiply(qRoll, qTempB, pTransform->qRot);

	quatRotateVec3(qYaw, pTransform->vPos, vTemp);
	copyVec3(vTemp, pTransform->vPos);
}

static void applyPreYawPitch(DynTransform* pTransform, F32 fYaw, F32 fPitch)
{
	Vec3 vInPYR;
	Quat qPitch, qInvPitch;
	Quat qRoll, qInvRoll;
	Quat qYaw;
	Quat qTempA, qTempB;
	quatToPYR(pTransform->qRot, vInPYR);
	rollQuat(-vInPYR[2], qInvRoll);
	rollQuat(vInPYR[2], qRoll);
	pitchQuat(-vInPYR[0], qInvPitch);
	pitchQuat(vInPYR[0] + fPitch, qPitch);

	yawQuat(-fYaw, qYaw);

	quatMultiply(qInvRoll, pTransform->qRot, qTempA);
	quatMultiply(qInvPitch, qTempA, qTempB);
	quatMultiply(qTempB, qYaw, qTempA);
	quatMultiply(qPitch, qTempA, qTempB);
	quatMultiply(qRoll, qTempB, pTransform->qRot);

	//quatRotateVec3(qYaw, pTransform->vPos, vTemp);
	//copyVec3(vTemp, pTransform->vPos);
}


void dynSeqUpdateBone( DynSequencer* pSqr, DynTransform* pResult, const char* pcBoneTag, U32 uiBoneLOD, const DynTransform* pxBaseTransform, DynRagdollState* pRagdollState, DynSkeleton* pSkeleton) 
{
	DynTransform animOverride;
	F32 fAnimOverrideWeight = 0.0f;
	DynNode* pRoot = pSkeleton->pRoot;

	if (pSqr->uiNumAnimOverrides > 0)
	{
		U8 i;
		for (i=0; i<pSqr->uiNumAnimOverrides; ++i)
		{
			DynAnimOverrideUpdater* pUpdater = &pSqr->animOverrideUpdater[i];
			bool bUpdatedBone = false;
			FOR_EACH_IN_EARRAY(pUpdater->pOverride->eaBones, const char, pcOverrideBoneTag)
			{
				if (pcOverrideBoneTag == pcBoneTag)
				{
					// We found an overrider, apply it
					F32 fBlendValue = pUpdater->fBlendValue;

					//But first check if it's suppressed
					if (pSkeleton->pFxManager)
					{
						FOR_EACH_IN_EARRAY(pSkeleton->pFxManager->eaSuppressors, DynFxSuppressor, pSuppressor)
						{
							if (pSuppressor->pcTag == pUpdater->pOverride->pcSuppressionTag)
							{
								fBlendValue *= (1.0f - pSuppressor->fAmount);
								break;
							}
						}
						FOR_EACH_END;
					}

					if (dynMoveSeqCalcTransform(pUpdater->pDynMoveSeq, pUpdater->fCurrentFrame, &animOverride, pcBoneTag, pxBaseTransform, pRagdollState, pRoot, true))
						++dynDebugState.uiNumBonesUpdated[CLAMP(uiBoneLOD, 0, 4)];
					bUpdatedBone = true;
					if (fBlendValue >= 1.0f)
					{
						dynTransformCopy(&animOverride, pResult);
						return;
					}
					fAnimOverrideWeight = fBlendValue;
					break;
				}
			}
			FOR_EACH_END;
			if (bUpdatedBone)
				break;
		}
	}

    if (dynMoveSeqCalcTransform(pSqr->currAction.pMoveSeq, pSqr->currAction.fFrameTime, pResult, pcBoneTag, pxBaseTransform, pRagdollState, pRoot, true))
		++dynDebugState.uiNumBonesUpdated[CLAMP(uiBoneLOD, 0, 4)];


	// If there are transforms we're interpolating out of, we need to calculate those
	if ( pSqr->iNumPrevActions > 0 && pSqr->fInterpParam >= 0.0f)
	{
		DynTransform prevTransform;
		DynTransform tempTransform;
		F32 fPrevInterpParam;
		int iCurrentPrevAction = pSqr->iNumPrevActions-1;


		// Set the first prev bone

		if (dynMoveSeqCalcTransform(pSqr->prevActions[iCurrentPrevAction].pMoveSeq, pSqr->prevActions[iCurrentPrevAction].fFrameTime, &prevTransform, pcBoneTag, pxBaseTransform, pRagdollState, pRoot, true))
			++dynDebugState.uiNumBonesUpdated[CLAMP(uiBoneLOD, 0, 4)];



		--iCurrentPrevAction;

		assert(iMaxPrevActions > 1 || iCurrentPrevAction < 0);

		while (iCurrentPrevAction >= 0)
		{
			DynTransform prevTransform2;

			if (!pSqr->prevActions[iCurrentPrevAction].pMoveSeq)
				continue;


			// This one is not done, so calculate the position
			if (dynMoveSeqCalcTransform(pSqr->prevActions[iCurrentPrevAction].pMoveSeq, pSqr->prevActions[iCurrentPrevAction].fFrameTime, &prevTransform2, pcBoneTag, pxBaseTransform, pRagdollState, pRoot, true))
				++dynDebugState.uiNumBonesUpdated[CLAMP(uiBoneLOD, 0, 4)];


			fPrevInterpParam = dynAnimInterpolationCalcInterp(pSqr->prevActions[iCurrentPrevAction].fInterpParam, &pSqr->prevActions[iCurrentPrevAction].pAction->interpBlock);
			dynTransformInterp(fPrevInterpParam, &prevTransform, &prevTransform2, &tempTransform);
			dynTransformCopy(&tempTransform, &prevTransform);



			--iCurrentPrevAction;
		}


		// Do the final blending between our prev bones and our current bones
		fPrevInterpParam = dynAnimInterpolationCalcInterp(pSqr->fInterpParam, &pSqr->currAction.pAction->interpBlock);

		dynTransformInterp(fPrevInterpParam, &prevTransform, pResult, &tempTransform);
		dynTransformCopy(&tempTransform, pResult);
	}

	if (fAnimOverrideWeight > 0.0f)
	{
		DynTransform xNormal;
		dynTransformCopy(pResult, &xNormal);
		dynTransformInterp(fAnimOverrideWeight, &xNormal, &animOverride, pResult);
	}
}

#define WEIGHTEPSILON 0.01f

static F32 findNormalYawDiff(const Vec3 vA, const Vec3 vB)
{
	F32 fYawA = getVec3Yaw(vA);
	F32 fYawB = getVec3Yaw(vB);
	F32 fDiffYaw = fYawB - fYawA;

	if (fDiffYaw > PI)
		fDiffYaw -= TWOPI;
	else if (fDiffYaw < -PI)
		fDiffYaw += TWOPI;

	return fDiffYaw;
}

static void dynSeqRecalcSkinning(SkinningMat4* pSkinningMats, DynAnimBoneInfo* pAnimBoneInfos, DynNode* pTop, DynNode* pBottom, const Mat4 mRoot)
{
	DynNode*	nodeStack[100];
	S32			stackPos = 0;
	nodeStack[stackPos++] = pTop;
	while(stackPos){
		DynNode*	pBone = nodeStack[--stackPos];
		if (pBone->uiCriticalBone)
		{
			dynNodeCalcWorldSpaceOneNode(pBone);
			assert(pBone->pcTag);
			if (pSkinningMats && pBone->iSkinningBoneIndex >= 0)
			{
				dynNodeCreateSkinningMat(pSkinningMats[pBone->iSkinningBoneIndex], pBone, pAnimBoneInfos[pBone->iCriticalBoneIndex].vBaseOffset, mRoot);
			}
			if (pBone->pCriticalChild 
				&& stackPos < ARRAY_SIZE(nodeStack))
			{
				nodeStack[stackPos++] = pBone->pCriticalChild;
			}
		}

		if(	pBone->pCriticalSibling 
			&& stackPos < ARRAY_SIZE(nodeStack))
		{
			nodeStack[stackPos++] = pBone->pCriticalSibling;
		}

		if (pBone == pBottom)
			return;
		continue;
	}
}

void dynSeqDumpSkeleton(DynSkeleton* pSkeleton)
{
	DynNode*	nodeStack[100];
	S32			stackPos = 0;
	S32			first = 1;
    float *fp;

    printf("\nDynSkeleton %08x\n", (U32)(uintptr_t)pSkeleton);

    if(pSkeleton->pRoot->pParent) {
        DynNode *const pBone = pSkeleton->pRoot->pParent;

        fp = (float*)pBone;

        printf(""
            "%08x %08x %08x %08x "
            " (%.2f %.2f %.2f | %.2f %.2f %.2f %.2f)"
            " %s\n", 
            pBone->uiFrame,(U32)(uintptr_t)pBone->pSkeleton,((U32*)&pBone->uiFrame)[-1],((U32*)&pBone->pSkeleton)[1],
            fp[0], fp[1], fp[2], fp[3], fp[4], fp[5], fp[6], 
            pBone->pcTag);
    }

	nodeStack[stackPos++] = pSkeleton->pRoot;

	while(stackPos){
		DynNode *const pBone = nodeStack[--stackPos];
		assert(pBone);

        if(	pBone->pChild &&
			stackPos < ARRAY_SIZE(nodeStack))
		{
			nodeStack[stackPos++] = pBone->pChild;
		}

		if(first)
		{
			first = 0;
		}
		else
		{
			if(	pBone->pSibling
				&& stackPos < ARRAY_SIZE(nodeStack))
			{
				nodeStack[stackPos++] = pBone->pSibling;
			}
		}

        fp = (float*)pBone;

        printf(""
            "%08x %08x %08x %08x "
            " (%.2f %.2f %.2f | %.2f %.2f %.2f %.2f)", 
            pBone->uiFrame,(U32)(uintptr_t)pBone->pSkeleton,((U32*)&pBone->uiFrame)[-1],((U32*)&pBone->pSkeleton)[1],
            fp[0], fp[1], fp[2], fp[3], fp[4], fp[5], fp[6]
        );

        //if (pBone->uiCriticalBone)
        if (pSkeleton->pDrawSkel && pBone->iSkinningBoneIndex >= 0 && pBone->uiSkinningBone) {

            fp = &pSkeleton->pDrawSkel->pCurrentSkinningMatSet->pSkinningMats[pBone->iSkinningBoneIndex][0][0];
            printf(""
                " (%.2f %.2f %.2f | %.2f %.2f %.2f | %.2f %.2f %.2f | %.2f %.2f %.2f ) ",
                fp[0], fp[1], fp[2], 
                fp[3], fp[4], fp[5], 
                fp[6], fp[7], fp[8], 
                fp[9], fp[10], fp[11]
            );
        }
        printf(" %s\n", pBone->pcTag);
    }
}

static void dynSeqCalcIKHelper(
	DynSkeleton *pSkeleton, const DynBaseSkeleton *pRegSkeleton, Mat4 mRoot, DynNode *pNode,
	const DynNode *pIKTargetA, const DynNode *pIKTargetB, U32 uiNumIKTargets, F32 fBlend,
	bool bRedoSkinning,
	bool bIKBothHands, bool bLeftHand,
	bool bIKMeleeMode, bool bEnableIKSliding, bool bDisableIKLeftWrist
	)
{
	//find the target transform
	DynTransform xTarget;
	dynAnimFindTargetTransform(
		pSkeleton, &pSkeleton->scaleCollection, pRegSkeleton, pNode,
		pIKTargetA, pIKTargetB, uiNumIKTargets, &xTarget, fBlend,
		bIKBothHands, bLeftHand,
		bIKMeleeMode, bEnableIKSliding
		);

	//warp the arm into position
	if (dynAnimFixupArm(pNode, &xTarget, fBlend, false, bIKBothHands, bIKMeleeMode, bDisableIKLeftWrist))
	{
		if (bRedoSkinning &&
			pSkeleton->pDrawSkel->pCurrentSkinningMatSet)
		{
			dynSeqRecalcSkinning(pSkeleton->pDrawSkel?pSkeleton->pDrawSkel->pCurrentSkinningMatSet->pSkinningMats:NULL, pSkeleton->pAnimBoneInfos, pNode->pParent->pParent->pParent, NULL, mRoot);
		}
		//dynAnimPrintArmFixupDebugInfo(pNode->pParent, &xTarget);
	}
}

static void dynSeqFindIKTargetData(DynSkeleton *pSkeleton, const DynNode **ppIKTargetA, const DynNode **ppIKTargetB, U32 *puiNumIKTargets)
{
	DynFxManager *pFxManager = pSkeleton->pFxManager;
	*puiNumIKTargets = pFxManager ? dynFxManNumIKTargets(pFxManager, pSkeleton->pcIKTarget) : 0;

	if (*puiNumIKTargets == 1) {
		*ppIKTargetA = dynFxManFindIKTarget(pFxManager, pSkeleton->pcIKTarget);
		*ppIKTargetB = NULL;
	}
	else if (*puiNumIKTargets > 1) {
		*ppIKTargetA = dynFxManFindIKTargetByPos(pFxManager, pSkeleton->pcIKTarget, 1);
		*ppIKTargetB = dynFxManFindIKTargetByPos(pFxManager, pSkeleton->pcIKTarget, 2);
	}
	else {
		*ppIKTargetA = NULL;
		*ppIKTargetB = NULL;
	}
}

static void dynSeqFindIKTargetNodeData(DynSkeleton *pSkeleton, const DynNode **ppIKTargetA, const DynNode **ppIKTargetB, U32 *puiNumIKTargets)
{
	*puiNumIKTargets = 0;

	if (pSkeleton->pcIKTargetNodeLeft && (*ppIKTargetB = dynSkeletonFindNodeCheckChildren(pSkeleton, pSkeleton->pcIKTargetNodeLeft))) {
		(*puiNumIKTargets)++;
	} else {
		*ppIKTargetB = NULL;
	}

	if (pSkeleton->pcIKTargetNodeRight && (*ppIKTargetA = dynSkeletonFindNodeCheckChildren(pSkeleton, pSkeleton->pcIKTargetNodeRight))) {
		(*puiNumIKTargets)++;
	} else {
		*ppIKTargetA = NULL;
	}
}

void dynSeqCalcIK(	DynSkeleton* pSkeleton,
					bool bRedoSkinning)
{
	const DynNode *pIKTargetA, *pIKTargetB;
	U32 uiNumIKTargets;
	Mat4 mRoot;

	dynNodeGetWorldSpaceMat(pSkeleton->pRoot, mRoot, false);

	if (pSkeleton->pcIKTargetNodeLeft ||
		pSkeleton->pcIKTargetNodeRight)
	{
		dynSeqFindIKTargetNodeData(	pSkeleton,
									&pIKTargetA,
									&pIKTargetB,
									&uiNumIKTargets);
	}
	else
	{
		dynSeqFindIKTargetData(	pSkeleton,
								&pIKTargetA,
								&pIKTargetB,
								&uiNumIKTargets);
	}

	if (pIKTargetA && pIKTargetA->pSkeleton && pIKTargetA->pSkeleton->uiFrame < pSkeleton->uiFrame ||
		pIKTargetB && pIKTargetB->pSkeleton && pIKTargetB->pSkeleton->uiFrame < pSkeleton->uiFrame )
	{
		//the animation transforms haven't been updated yet for the skeleton the ik-targets are on
		pSkeleton->bHasLastPassIK = true;
		return;
	}

	if (pSkeleton->fWepRegisterBlend > 0.0f)
	{
		const DynBaseSkeleton* pRegSkeleton = dynSkeletonGetRegSkeleton(pSkeleton);
		DynNode *pWepL;
		DynNode *pWepR;
		
		if (gConf.bNewAnimationSystem) {
			pWepL = pSkeleton->pWepLNode;
			pWepR = pSkeleton->pWepRNode;
		} else {
			pWepL = dynSkeletonFindNodeNonConst(pSkeleton, pcNodeNameWepL);
			pWepR = dynSkeletonFindNodeNonConst(pSkeleton, pcNodeNameWepR);
		}

		if (!pSkeleton->bDisableIKRightArm)
		{
			if (pWepR)
			{
				dynSeqCalcIKHelper(
					pSkeleton, pRegSkeleton, mRoot, pWepR,
					NULL, NULL, 0, pSkeleton->fWepRegisterBlend,
					bRedoSkinning,
					false, false,
					false, false, false
					);
			}
		}

		if (pWepL)
		{
			const DynTransform* pBaseWep = dynScaleCollectionFindTransform(&pSkeleton->scaleCollection, pcNodeNameWepL);
			if (pBaseWep)
			{
				dynSeqCalcIKHelper(
					pSkeleton, pRegSkeleton, mRoot, pWepL,
					pIKTargetA, pIKTargetB, uiNumIKTargets, pSkeleton->fWepRegisterBlend,
					bRedoSkinning,
					false, true,
					pSkeleton->bIKMeleeMode, pSkeleton->bEnableIKSliding, pSkeleton->bDisableIKLeftWrist
					);
			}
		}
	}
	else if (pSkeleton->fIKBothHandsBlend > 0.0f)
	{
		const DynBaseSkeleton* pRegSkeleton = dynSkeletonGetRegSkeleton(pSkeleton);
		DynNode *pWepL;
		DynNode *pWepR;

		if (gConf.bNewAnimationSystem) {
			pWepL = pSkeleton->pWepLNode;
			pWepR = pSkeleton->pWepRNode;
		} else {
			pWepL = dynSkeletonFindNodeNonConst(pSkeleton, pcNodeNameWepL);
			pWepR = dynSkeletonFindNodeNonConst(pSkeleton, pcNodeNameWepR);
		}

		if (pWepL)
		{
			const DynTransform* pBaseWep = dynScaleCollectionFindTransform(&pSkeleton->scaleCollection, pcNodeNameWepL);
			if (pBaseWep)
			{
				dynSeqCalcIKHelper(
					pSkeleton, pRegSkeleton, mRoot, pWepL,
					pIKTargetA, pIKTargetB, uiNumIKTargets, pSkeleton->fIKBothHandsBlend,
					bRedoSkinning,
					true, true,
					false, false, false
					);
			}
		}

		if (pWepR)
		{
			const DynTransform* pBaseWep = dynScaleCollectionFindTransform(&pSkeleton->scaleCollection, pcNodeNameWepR);
			if (pBaseWep)
			{
				dynSeqCalcIKHelper(
					pSkeleton, pRegSkeleton, mRoot, pWepR,
					pIKTargetA, pIKTargetB, uiNumIKTargets, pSkeleton->fIKBothHandsBlend,
					bRedoSkinning,
					true, false,
					false, false, false
					);
			}
		}
	}
}

void dynSeqCalcMatchJoint(DynSkeleton *pSkeleton, DynJointBlend *pMatchJoint)
{
	const DynBaseSkeleton *pBaseSkeleton = NULL;
	const DynMoveSeq* pMoveSeq = NULL;
	F32 fFrame;
	Mat4 mRoot;

	assert(!gConf.bNewAnimationSystem);

	pBaseSkeleton = dynSkeletonGetBaseSkeleton(pSkeleton);
	dynNodeGetWorldSpaceMat(pSkeleton->pRoot, mRoot, false);

	if (eaSize(&pSkeleton->eaSqr))
	{
		DynActionBlock* pCurrAction = &pSkeleton->eaSqr[0]->currAction;
		pMoveSeq = pCurrAction->pMoveSeq;
		fFrame = pCurrAction->fFrameTime;
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
			DynNode *pParentNode;

			DynTransform xRunning;
			DynTransform xParentNode, xParentNodeInv;
			Mat4 mParentNodeInv;
			Vec3 vPosMatchWS, vPosMatchLS;
			Vec3 vPosDiff, vPosDiffInv;

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

			pSkelMatchNode = dynSkeletonFindNodeNonConst(pSkeleton, pMatchJoint->pcName);
			pParentNode = pSkelMatchNode->pParent;

			dynNodeGetWorldSpacePos(pSkelMatchNode, vPosMatchWS);
			dynNodeGetLocalPos(pSkelMatchNode, vPosMatchLS);
			copyVec3(vPosMatchLS, pMatchJoint->vOriginalPos);

			dynNodeGetWorldSpaceTransform(pParentNode, &xParentNode);
			dynTransformInverse(&xParentNode, &xParentNodeInv);
			dynTransformToMat4(&xParentNodeInv, mParentNodeInv);

			subVec3(xRunning.vPos, vPosMatchWS, vPosDiff);
			scaleVec3(vPosDiff, pMatchJoint->fBlend, vPosDiff);
			mulVecMat3(vPosDiff, mParentNodeInv, vPosDiffInv);
			addVec3(vPosMatchLS, vPosDiffInv, vPosMatchLS);
			dynNodeSetPos(pSkelMatchNode, vPosMatchLS);
		}
	}
}

void dynSeqCalcTransformsProcessPhysics(DynSkeleton* pSkeleton, F32 fDeltaTime)
{
	DynNode*	nodeStack[100];
	S32			stackPos = 0;
	S32			first = 1;
	Mat4		mRoot;
	bool		bRoot = true;

    PERFINFO_AUTO_START_FUNC();

    // NB: bones are updated, and dynSeqRecalcSkinning() does all sub-bones

	//dynNodeSetDirtyInline(pBone);
	nodeStack[stackPos++] = pSkeleton->pRoot;

	while(stackPos){
		DynNode *const pBone = nodeStack[--stackPos];

		/*
		if (pBone == pPrefetch)
		iCorrect++;
		else
		iWrong++;
		*/
		assert(pBone);
		// look at the two actions, and blend them
		// for now, just the first one
		if (
			!pBone->uiCriticalBone ||
			!pBone->uiSkeletonBone ||
			!pBone->pcTag ||
			pBone->uiLocked || 
			pBone->uiMaxLODLevelBelow + MAX_LOD_UPDATE_DEPTH < pSkeleton->uiLODLevel
			)
		{
			// We aren't going to process this bone, but we need to still do some work

			if (pBone->uiCriticalBone)
			{
				if (pBone->pCriticalChild 
					&& stackPos < ARRAY_SIZE(nodeStack))
				{
					nodeStack[stackPos++] = pBone->pCriticalChild;
				}
			}

			if(first)
			{
				first = 0;
			}
			else
			{
				if(	pBone->pCriticalSibling 
					&& stackPos < ARRAY_SIZE(nodeStack))
				{
					nodeStack[stackPos++] = pBone->pCriticalSibling;
				}
			}

			continue;
		}

		if(	pBone->pCriticalChild &&
			stackPos < ARRAY_SIZE(nodeStack))
		{
			nodeStack[stackPos++] = pBone->pCriticalChild;
		}

		if(first)
		{
			first = 0;
		}
		else
		{
			if(	pBone->pCriticalSibling
				&& stackPos < ARRAY_SIZE(nodeStack))
			{
				nodeStack[stackPos++] = pBone->pCriticalSibling;
			}
		}
/*
        if(
            !strcmp(pBone->pcTag, "WepR") ||
            !strcmp(pBone->pcTag, "HandR")||
            !strcmp(pBone->pcTag, "Larmr")
            ) {

            DynTransform xHand;
            dynNodeGetWorldSpaceTransform(pBone, &xHand);

            wl_state.drawAxesFromTransform_func(&xHand, 1.0f);
        }
*/
		if (bRoot)
		{
/*
            {
                DynTransform xHand;
                dynNodeGetWorldSpaceTransform(pBone, &xHand);

                wl_state.drawAxesFromTransform_func(&xHand, 1.0f);
            }
*/
            dynNodeGetWorldSpaceMat(pBone, mRoot, false);
            bRoot = false;
		}

        if (pBone->uiHasBouncer)
		{
			FOR_EACH_IN_EARRAY(pSkeleton->eaBouncerUpdaters, DynBouncerUpdater, pUpdater)
			{
				if (pUpdater->pInfo->pcBoneName == pBone->pcTag)
				{
					dynBouncerUpdaterUpdateBone(pUpdater, pBone, fDeltaTime);
				}
			}
			FOR_EACH_END;
		}
    }

	PERFINFO_AUTO_STOP_CHECKED(__FUNCTION__);
}

static void	dynSeqSetNextAction(DynSequencer* pSqr, const DynAction* pNewAction)
{
	const SkelInfo *pSkelInfo;
	int iOldActionMoveIndex = -1;
	pSqr->nextAction.pAction = pNewAction;
	if ( pSqr->nextAction.pAction == pSqr->currAction.pAction && pSqr->nextAction.pAction->bNoSelfInterp)
		pSqr->nextAction.bInterpolates = false;
	else if (pSqr->currSeq.bDefaultSeq && !pSqr->nextSeq.bDefaultSeq)
		pSqr->nextAction.bInterpolates = false;
	else
		pSqr->nextAction.bInterpolates = true;
	if ( pSqr->nextAction.pAction == pSqr->currAction.pAction )
	{
		iOldActionMoveIndex = pSqr->currAction.uiActionMoveIndex;
	}

	pSkelInfo = GET_REF(pSqr->hSkelInfo);
	pSqr->nextAction.pMoveSeq = pSkelInfo?dynMoveSeqFromAction(pSqr->nextAction.pAction, pSkelInfo, iOldActionMoveIndex, &pSqr->nextAction.uiActionMoveIndex, &pSqr->uiSeed, &pSqr->bits):NULL;
	if ( pSqr->nextAction.pMoveSeq ) {
		pSqr->nextAction.pcMoveSeq = pSqr->nextAction.pMoveSeq->pcDynMoveSeq;
		pSqr->nextAction.fPlaybackSpeed = dynMoveSeqGetRandPlaybackSpeed(pSqr->nextAction.pMoveSeq);
	} else
		pSqr->nextAction.pcMoveSeq = NULL;
}

__forceinline static F32 dynSeqFrameTimeLeft(SA_PARAM_NN_VALID DynSequencer* pSqr)
{
	return dynMoveSeqGetLength(pSqr->currAction.pMoveSeq) - pSqr->currAction.fFrameTime;
}

__forceinline static F32 dynActionBlockGetInterpolation(SA_PARAM_NN_VALID const DynActionBlock* pActionBlock)
{
	return pActionBlock->bInterpolates?pActionBlock->pAction->interpBlock.fInterpolation:0.0f;
}

__forceinline static F32 dynActionBlockGetInterpParam( SA_PARAM_NN_VALID DynActionBlock* pAction) 
{
	if (dynActionBlockGetInterpolation(pAction) > 0.0f)
	{
		F32 fInterpFrame = pAction->fFrameTime - pAction->fInterpFrameTimeOffset;
		if (fInterpFrame < 0.0f)
			fInterpFrame += pAction->pMoveSeq->fLength;
		return CLAMP(fInterpFrame / dynActionBlockGetInterpolation(pAction), 0.0f, 1.0f);
	}
	else
	{
		return 1.0f;
	}
}


static void dynSeqInterruptWithNextAction( SA_PARAM_NN_VALID DynSequencer* pSqr, SA_PARAM_NN_VALID const DynAction* pNextAction, SA_PARAM_OP_VALID DynSkeleton* pSkeleton)
{
	//F32 fInterpTime = dynActionBlockGetInterpParam(&pSqr->currAction);
	dynSequencerLog(pSqr, "Interrupting sequencer with next action %s.\n", pNextAction->pcName);
	dynSeqSetNextAction(pSqr, pNextAction);
	dynSequencerAdvancePrevActions(pSqr);
	memcpy(&pSqr->currAction, &pSqr->nextAction, sizeof(pSqr->currAction));
	pSqr->currAction.fInterpFrameTimeOffset = 0.0f;
	if ( pSqr->currAction.pMoveSeq && pSqr->currAction.pMoveSeq == pSqr->prevActions[0].pMoveSeq && pSqr->currAction.pAction->bCycle)
	{
		F32 fMoveLength = dynMoveSeqGetLength(pSqr->currAction.pMoveSeq);
		DynFxManager* pFxManager = pSkeleton?(pSkeleton->pFxManager?pSkeleton->pFxManager:(SAFE_MEMBER(pSkeleton->pParentSkeleton, pFxManager))):NULL;
		pSqr->currAction.fFrameTime = pSqr->prevActions[0].fFrameTime;
		while (pSqr->currAction.fFrameTime >= fMoveLength)
			pSqr->currAction.fFrameTime -= fMoveLength;
		dynSequencerLog(pSqr, "New Action is same as old and is a Cycle, so keeping FrameTime at %.2f\n", pSqr->currAction.fFrameTime);
		if ( pFxManager )
			dynActionProcessFxAndImpactCalls(pSkeleton, pSqr->currAction.pAction, pSqr->currAction.uiActionMoveIndex, pFxManager, 0.0f, pSqr->currAction.fFrameTime);
	}
	else if (pSqr->currAction.pMoveSeq && pSqr->currAction.pMoveSeq->fDistance > 0.0f && pSkeleton && !dynDebugState.bNoMovementSync)
	{
		pSqr->currAction.fFrameTime = pSkeleton->fMovementSyncPercentage * pSqr->currAction.pMoveSeq->fLength;
		pSqr->currAction.fInterpFrameTimeOffset = pSqr->currAction.fFrameTime;
		dynSequencerLog(pSqr, "Setting Frame Time to movement sync frame time of %.2f\n", pSqr->currAction.fFrameTime);
	}
	else
	{
		F32 fStartOffsetTime;
		if (!pSqr->currAction.pMoveSeq || pSqr->currAction.pMoveSeq == pSqr->prevActions[0].pMoveSeq)
			fStartOffsetTime = 0.0f;
		else
			fStartOffsetTime = dynMoveSeqGetStartOffset(pSqr->currAction.pMoveSeq);

		pSqr->currAction.fFrameTime = fStartOffsetTime;//0.0f;
		dynSequencerLog(pSqr, "Resetting Frame Time to %f\n", pSqr->currAction.fFrameTime);
	}
}

static void dynSeqCycleAction( SA_PARAM_NN_VALID DynSequencer* pSqr, SA_PARAM_OP_VALID DynSkeleton* pSkeleton)
{
	//F32 fInterpTime = dynActionBlockGetInterpParam(&pSqr->currAction);
	dynSequencerLog(pSqr, "NoSelfInterp set, so just cycling action %s.\n", pSqr->currAction.pAction->pcName);
	{
		F32 fMoveLength = dynMoveSeqGetLength(pSqr->currAction.pMoveSeq);
		DynFxManager* pFxManager = pSkeleton?(pSkeleton->pFxManager?pSkeleton->pFxManager:(SAFE_MEMBER(pSkeleton->pParentSkeleton, pFxManager))):NULL;
		while (pSqr->currAction.fFrameTime >= fMoveLength)
		{
			pSqr->currAction.fFrameTime -= fMoveLength;
			pSqr->currAction.fInterpFrameTimeOffset -= fMoveLength; // we need to keep track of whether we've finished interpolating or not
		}
		if ( pFxManager )
			dynActionProcessFxAndImpactCalls(pSkeleton, pSqr->currAction.pAction, pSqr->currAction.uiActionMoveIndex, pFxManager, 0.0f, pSqr->currAction.fFrameTime);
	}
}

__forceinline static bool dynSeqActionComplete(SA_PARAM_NN_VALID DynSequencer* pSqr, SA_PARAM_NN_VALID DynActionBlock* pActionBlock)
{
	F32 fFrameTimeLeft = dynSeqFrameTimeLeft(pSqr);
	F32 fInterpolationTime = dynActionBlockGetInterpolation(pActionBlock);
	dynSequencerLog(pSqr, "Checking if Current Action %s is complete\n", SAFE_MEMBER(pSqr->currAction.pAction, pcName));
	if (fFrameTimeLeft <= fInterpolationTime)
	{
		dynSequencerLog(pSqr, "Frame Time Left %.2f is less than or equal to Next Action Interpolation time %.2f, so Action is complete\n", fFrameTimeLeft, fInterpolationTime);
		return true;
	}
	else
	{
		dynSequencerLog(pSqr, "Frame Time Left %.2f is greater than Next Action Interpolation time %.2f, so Action is NOT complete\n", fFrameTimeLeft, fInterpolationTime);
		return false;
	}
}



static bool dynSeqAdvanceAction(DynSequencer* pSqr, DynSkeleton* pSkeleton)
{
	PERFINFO_AUTO_START_L2(__FUNCTION__,1);
	dynSequencerLog(pSqr, "Processing Action.\n");
	// First, look if there is a new sequence, and if it interrupts
	if (!pSqr->nextSeq.bAdvancedTo && dynSeqCheckIfNextInterrupts(pSqr))
	{
		// If it interrupts, then we simply jump to the first action of the next sequence
		dynSeqInterruptWithNextAction(pSqr, dynSeqGetFirstAction(&pSqr->bits, pSqr->nextSeq.pSeq, pSqr), pSkeleton);
		dynAdvanceToNextSequence(pSqr, pSkeleton?pSkeleton->pFxManager:NULL);
		PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
		return true;
	}
	else
	{
		// We stay in the current sequence
		dynSequencerLog(pSqr, "Processing current sequence.\n");

		if ( pSqr->currAction.pAction )
		{
			// First, look at the if blocks to see if we need to jump
			const U32 uiNumIfBlocks = eaSize(&pSqr->currAction.pAction->eaIf);
			U32 uiIfIndex;
			dynSequencerLog(pSqr, "Processing current action %s.\n", SAFE_MEMBER(pSqr->currAction.pAction, pcName));
			for (uiIfIndex=0; uiIfIndex<uiNumIfBlocks; ++uiIfIndex)
			{
				DynActIfBlock* pIfBlock = pSqr->currAction.pAction->eaIf[uiIfIndex];
				if (dynBitFieldSatisfiesLogicBlock(&pSqr->bits, &pIfBlock->logic))
				{
					dynSequencerLog(pSqr, "Current bits satisfied If Block\n");
					// Satisfied that all off bits are off
					if (pIfBlock->bEndSequence)
					{
						dynSequencerLog(pSqr, "Ending Sequence due to If Block\n");
						dynSeqInterruptWithNextAction(pSqr, dynSeqGetFirstAction(&pSqr->bits, pSqr->nextSeq.pSeq, pSqr), pSkeleton);
						dynAdvanceToNextSequence(pSqr, pSkeleton?pSkeleton->pFxManager:NULL);
						PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
						return true;
					}
					if ( pIfBlock->pNextAction )
					{
						// It interupts
						dynSequencerLog(pSqr, "Going to next action %s due to If Block\n", pIfBlock->pNextAction->pcName);
						dynSeqInterruptWithNextAction(pSqr, pIfBlock->pNextAction, pSkeleton);
						PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
						return true;
					}
					if (pSkeleton)
					{
						dynBitFieldSetAllFromBitField(&pSqr->bits, &pSkeleton->actionFlashBits);
					}
					if (dynSeqLoggingEnabled)
					{
						char cTemp[1024];
						dynBitFieldStaticWriteBitString(SAFESTR(cTemp), &pIfBlock->setBits);
						if (strlen(cTemp)>0)
							dynSequencerLog(pSqr, "Setting Detail bits %s\n", cTemp);
					}
					dynSequencerLog(pSqr, "Done with If Block\n");
				}
			}

			// Next, look to current action's next action
			if (pSqr->currAction.pAction->pNextAction)
			{
				dynSequencerLog(pSqr, "Current action %s already has a next action: %s\n", SAFE_MEMBER(pSqr->currAction.pAction, pcName), SAFE_MEMBER(pSqr->currAction.pAction->pNextAction, pcName));
				dynSeqSetNextAction(pSqr, pSqr->currAction.pAction->pNextAction);
				if (dynSeqActionComplete(pSqr, &pSqr->nextAction))
				{
					dynSeqInterruptWithNextAction(pSqr, pSqr->currAction.pAction->pNextAction, pSkeleton);
					PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
					return true;
				}
			}
			else if (eaSize(&pSqr->currAction.pAction->eaNextActionChance) > 0)
			{
				dynSequencerLog(pSqr, "Choosing at random from one of %d NextActionChance choices.\n", eaSize(&pSqr->currAction.pAction->eaNextActionChance));
				{
					F32 fRandNum = randomF32Seeded(&pSqr->uiSeed, RandType_LCG);
					F32 fTotalProb = 0.0f;
					FOR_EACH_IN_EARRAY_FORWARDS(pSqr->currAction.pAction->eaNextActionChance, DynNextActionChance, pNextActionChance)
						fTotalProb += pNextActionChance->fChance;
						if ( fRandNum <= fTotalProb )
						{
							dynSequencerLog(pSqr, "Randomly chose action %s with probability %.3f\n", pNextActionChance->pcNextAction, pNextActionChance->fChance);
							dynSeqSetNextAction(pSqr, pNextActionChance->pNextAction);
							if (dynSeqActionComplete(pSqr, &pSqr->nextAction))
							{
								dynSeqInterruptWithNextAction(pSqr, pNextActionChance->pNextAction, pSkeleton);
								PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
								return true;
							}
							break;
						}
					FOR_EACH_END
				}
			}
			else 
			{
				F32 fFrameTimeLeft = dynSeqFrameTimeLeft(pSqr);
				dynSequencerLog(pSqr, "Frame Time Left is %.2f\n", fFrameTimeLeft);
				if ( fFrameTimeLeft <= 0.0f )
				{
					dynSequencerLog(pSqr, "Time is out for this action!\n");
					if ( pSqr->nextSeq.pSeq != pSqr->currSeq.pSeq  && (!pSqr->currAction.pAction->bLoopUntilInterrupted || dynSeqCheckIfNextInterrupts(pSqr)))
					{
						dynSequencerLog(pSqr, "Next Sequence %s is different than current sequence, so advancing to first action of that new sequence\n", SAFE_MEMBER(pSqr->nextSeq.pSeq, pcName));
						dynSeqInterruptWithNextAction(pSqr, dynSeqGetFirstAction(&pSqr->bits, pSqr->nextSeq.pSeq, pSqr), pSkeleton);
						dynAdvanceToNextSequence(pSqr, pSkeleton?pSkeleton->pFxManager:NULL);
						PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
						return true;
					}
					else
					{
						if (pSqr->currAction.pAction->bCycle )
						{
							dynSequencerLog(pSqr, "Action %s is a cycle, cyling\n", SAFE_MEMBER(pSqr->currAction.pAction,pcName));
							if (pSqr->currAction.pAction->bNoSelfInterp)
								dynSeqCycleAction(pSqr, pSkeleton);
							else
								dynSeqInterruptWithNextAction(pSqr, pSqr->currAction.pAction, pSkeleton);
							PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
							return true;
						}
						else
						{
							dynSequencerLog(pSqr, "Action %s is not a cycle, so advancing to first action of looping sequence\n", SAFE_MEMBER(pSqr->currAction.pAction,pcName));
							dynSeqInterruptWithNextAction(pSqr, dynSeqGetFirstAction(&pSqr->bits, pSqr->currSeq.pSeq, pSqr), pSkeleton);
							PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
							return true;
						}
					}
				}
			}
		}
	}
	PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
	return false;
}


static F32 fBodySockDistSqr = SQR(150.0f);

// Override the distance at which a body sock gets swapped in for a character
AUTO_COMMAND ACMD_CATEGORY(dynAnimation);
void danimSetBodySockDistance(F32 fDistance)
{
	fBodySockDistSqr = SQR(fDistance);
}

bool dynSequencersDemandNoLOD(SA_PARAM_NN_VALID DynSkeleton* pSkeleton)
{
	FOR_EACH_IN_EARRAY(pSkeleton->eaSqr, DynSequencer, pSqr)
		if (!pSqr->bRunSinceReset)
			return true;
		if ( /*iNOLODBit >= 0 && */dynBitFieldBitTest(&pSqr->bits, iNOLODBit))
			return true;
	FOR_EACH_END;
	return false;
}



__forceinline static void dynActionBlockAdvanceTime(DynActionBlock* pBlock, F32 fDeltaTime, DynSkeleton* pSkeleton)
{
}


void dynSeqUpdate(DynSequencer* pSqr, const DynBitField* pBits, F32 fDeltaTime, DynSkeleton* pSkeleton, bool bUpdateInterpParam, int iSeqIndex)
{
	bool bNextSeqChanged;
	F32 fFrameDelta = fDeltaTime * 30.0f * dynDebugState.fAnimRate;
	U32 uiCount=0;
	F32 fPreviousFrameTime = pSqr->currAction.fFrameTime;
	DynFxManager* pFxManager;
	bool bDisableHandIK = false;

	if (pSkeleton->uiLODLevel > pSqr->uiRequiredLOD)
		return;

	pFxManager = pSkeleton->pFxManager?pSkeleton->pFxManager:(SAFE_MEMBER(pSkeleton->pParentSkeleton, pFxManager));

	if (pSqr->bReset)
	{
		dynSequencerResetState(pSqr);
		dynSequencerLog(pSqr, "RESETTING SEQUENCER\n");
	}

	PERFINFO_AUTO_START_FUNC();

	if (dynSeqLoggingEnabled)
	{
		dynSequencerLogEx(pSqr, "\n-------------------------\n");
		dynSequencerLogEx(pSqr, "Start of sequencer %s update\n", pSqr->pcSequencerName);
		dynSequencerLogEx(pSqr, "-------------------------\n");
		dynSequencerLogEx(pSqr, "Sequence\n");
		dynSequencerLogEx(pSqr, "       Current: %s\n", SAFE_MEMBER(pSqr->currSeq.pSeq, pcName));
		dynSequencerLogEx(pSqr, "       Next: %s\n", SAFE_MEMBER(pSqr->nextSeq.pSeq, pcName));
		dynSequencerLogEx(pSqr, "Action\n");
		dynSequencerLogEx(pSqr, "       Current: %s\n", SAFE_MEMBER(pSqr->currAction.pAction, pcName));
		dynSequencerLogEx(pSqr, "       Next  : %s\n", SAFE_MEMBER(pSqr->nextAction.pAction, pcName));
		//dynSequencerLog(pSqr, "       Move    : %s\n", SAFE_MEMBER(pSqr->nextAction.pMoveSeq,pcDynMoveSeq));

		dynSequencerLogEx(pSqr, "Playing for %.2f seconds\n", fDeltaTime);
	}

	// First, look to see if the next seq bits have changed
	bNextSeqChanged = dynNewNextSeqBlockFromBits(pSqr, pBits, pSkeleton);

	// Update current action
	assert(pSqr->currAction.pAction);
	dynMoveSeqAdvanceTime(	pSqr->currAction.pMoveSeq,
							fFrameDelta,
							pSkeleton,
							&pSqr->currAction.fFrameTime,
							pSqr->currAction.fPlaybackSpeed,
							1);

	// Update anim overrides
	{
		U8 i;
		for (i=0; i<pSqr->uiNumAnimOverrides; ++i)
		{
			DynAnimOverrideUpdater* pUpdater = &pSqr->animOverrideUpdater[i];
			if (pUpdater->bActive)
			{
				if (pUpdater->fBlendValue < 1.0f)
				{
					if (pUpdater->pOverride->fInterpolation <= 0.0f)
						pUpdater->fBlendValue = 1.0f;
					else
					{
						pUpdater->fBlendValue += fFrameDelta / (pUpdater->pOverride->fInterpolation);
						pUpdater->fBlendValue = CLAMP(pUpdater->fBlendValue, 0.0f, 1.0f);
					}
				}

				if (pUpdater->pOverride->bDisableHandIK)
					bDisableHandIK = true;
			}
			else
			{
				if (pUpdater->pOverride->fInterpolation <= 0.0f)
					pUpdater->fBlendValue = 0.0f;
				else
				{
					pUpdater->fBlendValue -= fFrameDelta / (pUpdater->pOverride->fInterpolation);
				}
				if (pUpdater->fBlendValue <= 0.0f)
				{
					// Remove it

					// Count how many more anim overrides there are
					int iNumLeft = pSqr->uiNumAnimOverrides - (i+1);
					if (iNumLeft > 0)
					{
						memcpy(&pSqr->animOverrideUpdater[i], &pSqr->animOverrideUpdater[i+1], sizeof(DynAnimOverrideUpdater) * iNumLeft);
					}
					pSqr->uiNumAnimOverrides--;
					i--;
					break;
				}
			}

			if (pUpdater->pDynMoveSeq->fLength > 1.0f)
			{
				pUpdater->fCurrentFrame += fFrameDelta;

				while (pUpdater->fCurrentFrame > pUpdater->pDynMoveSeq->fLength)
					pUpdater->fCurrentFrame -= pUpdater->pDynMoveSeq->fLength;
			}
		}
	}

	if ( fDeltaTime > 0.0f && pFxManager )
	{
		if ( pSqr->currAction.fFrameTime > dynMoveSeqGetLength(pSqr->currAction.pMoveSeq) )
		{
			dynActionProcessFxAndImpactCalls(pSkeleton, pSqr->currAction.pAction, pSqr->currAction.uiActionMoveIndex, pFxManager, fPreviousFrameTime, dynMoveSeqGetLength(pSqr->currAction.pMoveSeq));
		}
		else
		{
			dynActionProcessFxAndImpactCalls(pSkeleton, pSqr->currAction.pAction, pSqr->currAction.uiActionMoveIndex, pFxManager, fPreviousFrameTime, pSqr->currAction.fFrameTime);
		}
	}

	while (dynSeqAdvanceAction(pSqr, pSkeleton))
	{
		++uiCount;
		if ( uiCount > 100 )
		{
			char cInfiniteLoopMessage[256];
			char cInfiniteLoopDetails[1024];
			char cBitBuffer[512];
			dynSequencerLog(pSqr, "Triggered infinite loop!!!\n");
			
			sprintf(cInfiniteLoopDetails, "Current Bits are: ");
			dynBitFieldWriteBitString(cBitBuffer, ARRAY_SIZE_CHECKED(cBitBuffer), dynSeqGetBits(pSqr));
			strcat(cInfiniteLoopDetails, cBitBuffer);
			strcat(cInfiniteLoopDetails, "\nCurrent Action is ");
			strcat(cInfiniteLoopDetails, pSqr->currAction.pAction->pcName);
			strcat(cInfiniteLoopDetails, "\n Previous Action is ");
			strcat(cInfiniteLoopDetails,pSqr->prevActions[0].pAction->pcName);
			ErrorDetailsf("%s", cInfiniteLoopDetails);

			sprintf(cInfiniteLoopMessage, "Infinite loop created in animation sequence %s\n", pSqr->currSeq.pSeq->pcName);
			Errorf("%s", cInfiniteLoopMessage);
			break; // break out, hope things recover
		}
	}

	// Only apply IK Weapon Registration if you're the default (upper body) sequencer
	if (iSeqIndex == 0 && pSqr->currAction.pMoveSeq && !bDisableHandIK)
	{
		if (pSqr->currAction.pMoveSeq->bRegisterWep ||
			(	pSqr->currAction.pMoveSeq->pcIKTarget &&
				!pSqr->currAction.pMoveSeq->bIKBothHands))
		{
			pSkeleton->bRegisterWep = true;
		}
		pSkeleton->bIKBothHands			|= pSqr->currAction.pMoveSeq->bIKBothHands;
		pSkeleton->bIKMeleeMode			|= pSqr->currAction.pMoveSeq->bIKMeleeMode;
		pSkeleton->bEnableIKSliding		|= pSqr->currAction.pMoveSeq->bEnableIKSliding;
		pSkeleton->bDisableIKLeftWrist	|= pSqr->currAction.pMoveSeq->bDisableIKLeftWrist;
		pSkeleton->bDisableIKRightArm	|= pSqr->currAction.pMoveSeq->bDisableIKRightArm;
		if (pSqr->currAction.pMoveSeq->pcIKTarget)			pSkeleton->pcIKTarget			= pSqr->currAction.pMoveSeq->pcIKTarget;
		if (pSqr->currAction.pMoveSeq->pcIKTargetNodeLeft)	pSkeleton->pcIKTargetNodeLeft	= pSqr->currAction.pMoveSeq->pcIKTargetNodeLeft;
		if (pSqr->currAction.pMoveSeq->pcIKTargetNodeRight)	pSkeleton->pcIKTargetNodeRight	= pSqr->currAction.pMoveSeq->pcIKTargetNodeRight;
	}

	// Only apply base skeleton joint animation matching if you're the default (upper body) sequencer
	if (iSeqIndex == 0 && pSqr->currAction.pMoveSeq)
	{
		FOR_EACH_IN_EARRAY(pSqr->currAction.pMoveSeq->eaMatchBaseSkelAnim, DynMoveMatchBaseSkelAnim, pMatchBaseSkelAnim) {
			const DynNode *pBone = NULL;
			bool bCreate = true;
			FOR_EACH_IN_EARRAY(pSkeleton->eaMatchBaseSkelAnimJoints, DynJointBlend, pMatchedJoint) {
				if (pMatchedJoint->pcName == pMatchBaseSkelAnim->pcBoneName) {
					pMatchedJoint->bActive = true;
					bCreate = false;
					//in case the calling animation has changed
					pMatchedJoint->bPlayBlendOutWhileActive = pMatchBaseSkelAnim->bPlayBlendOutDuringMove;
					pMatchedJoint->fBlendInRate  = (pMatchBaseSkelAnim->fBlendInTime  < 0.01) ? 100.f : 1.f/pMatchBaseSkelAnim->fBlendInTime;
					pMatchedJoint->fBlendOutRate = (pMatchBaseSkelAnim->fBlendOutTime < 0.01) ? 100.f : 1.f/pMatchBaseSkelAnim->fBlendOutTime;
					break;
				}
			} FOR_EACH_END;
			if (bCreate && (pBone = dynSkeletonFindNode(pSkeleton, pMatchBaseSkelAnim->pcBoneName))) {
				DynJointBlend *pNewMatchJoint = calloc(1,sizeof(DynJointBlend));
				pNewMatchJoint->pcName = pMatchBaseSkelAnim->pcBoneName;
				pNewMatchJoint->bActive = true;
				pNewMatchJoint->fBlend = pMatchBaseSkelAnim->bStartFullyBlended ? 1.f : 0.f;
				pNewMatchJoint->bPlayBlendOutWhileActive = pMatchBaseSkelAnim->bPlayBlendOutDuringMove;
				pNewMatchJoint->fBlendInRate  = (pMatchBaseSkelAnim->fBlendInTime  < 0.01) ? 100.f : 1.f/pMatchBaseSkelAnim->fBlendInTime;
				pNewMatchJoint->fBlendOutRate = (pMatchBaseSkelAnim->fBlendOutTime < 0.01) ? 100.f : 1.f/pMatchBaseSkelAnim->fBlendOutTime;
				dynNodeGetLocalPos(pBone, pNewMatchJoint->vOriginalPos);
				eaPush(&pSkeleton->eaMatchBaseSkelAnimJoints, pNewMatchJoint);
			}
		} FOR_EACH_END;
	}

	// Now that we've advanced the action (possibly), set any action bits
	dynActionSetBits(pSqr->currAction.pAction, pSqr->currAction.uiActionMoveIndex, &pSkeleton->actionFlashBits, pSqr);

	if(bUpdateInterpParam)
	{
		F32 fInterpParam = dynActionBlockGetInterpParam(&pSqr->currAction);

		// Look to see if we need to blend
		if ( fInterpParam < 0.999f )
		{
			int iNumPrevActions = 0;
			dynSequencerLog(pSqr, "Blending: Interpolation parameter is %.2f\n", fInterpParam);
			// We must do a blend, otherwise, no blend
			// advance previous clock
			{
				int i;
				for (i=0; i<iMaxPrevActions && !pSqr->prevActions[i].bPrevActionDone; ++i)
				{
					dynMoveSeqAdvanceTime(	pSqr->prevActions[i].pMoveSeq,
											fFrameDelta,
											pSkeleton,
											&pSqr->prevActions[i].fFrameTime,
											pSqr->prevActions[i].fPlaybackSpeed,
											1);

					if (!pSqr->prevActions[i].pAction)
					{
						pSqr->prevActions[i].bPrevActionDone = true;
						break;
					}
					if ( pSqr->prevActions[i].pAction->bCycle && !pSqr->prevActions[i].pAction->bForceEndFreeze )
					{
						while ( pSqr->prevActions[i].fFrameTime > dynMoveSeqGetLength(pSqr->prevActions[i].pMoveSeq) )
							pSqr->prevActions[i].fFrameTime -= dynMoveSeqGetLength(pSqr->prevActions[i].pMoveSeq);
					}
					else
					{
						if ( pSqr->prevActions[i].fFrameTime > dynMoveSeqGetLength(pSqr->prevActions[i].pMoveSeq) )
							pSqr->prevActions[i].fFrameTime = dynMoveSeqGetLength(pSqr->prevActions[i].pMoveSeq);
					}
					pSqr->prevActions[i].fInterpParam = dynActionBlockGetInterpParam(&pSqr->prevActions[i]);
					if (pSqr->prevActions[i].fInterpParam >= 0.9999f && (i+1)<iMaxPrevActions)
					{
						pSqr->prevActions[i+1].bPrevActionDone = true;
					}
				}
				if (i<1)
				{
					Errorf("First previous action should never be done when interpparam is %.2f", fInterpParam);
					i=1;
				}
				iNumPrevActions = i;
			}

			pSqr->fInterpParam = fInterpParam;
			pSqr->iNumPrevActions = iNumPrevActions;
		}
		else // just play the curr anim
		{
			dynSequencerLog(pSqr, "Not blending\n");
			pSqr->prevActions[0].bPrevActionDone = true;
			pSqr->fInterpParam = -1.0f;
		}
	}
	pSqr->bRunSinceReset = true;


	if (dynSeqLoggingEnabled)
	{
		dynSequencerLogEx(pSqr, "-----------------------\n");
		dynSequencerLogEx(pSqr, "End of sequencer %s update\n", pSqr->pcSequencerName);
		dynSequencerLogEx(pSqr, "Sequence\n");
		dynSequencerLogEx(pSqr, "       Current: %s\n", SAFE_MEMBER(pSqr->currSeq.pSeq, pcName));
		dynSequencerLogEx(pSqr, "       Next: %s\n", SAFE_MEMBER(pSqr->nextSeq.pSeq, pcName));
		//dynSequencerLogEx(pSqr, "       Move    : %s\n", SAFE_MEMBER(pSqr->currAction.pMoveSeq,pcDynMoveSeq));
		dynSequencerLogEx(pSqr, "Action Blend\n");
		{
			int iAction;
			F32 fActionBlends[MAX_PREV_ACTIONS+1];
			F32 fInterpFraction = 1.0f - pSqr->fInterpParam;
			for (iAction=0; iAction<pSqr->iNumPrevActions; ++iAction)
			{
				if (!pSqr->prevActions[iAction].bPrevActionDone)
				{
					F32 fInterpParamToUse = pSqr->prevActions[iAction].fInterpParam;
					// Check to see if it's the last one
					if ((iAction+1) >= pSqr->iNumPrevActions || pSqr->prevActions[iAction+1].bPrevActionDone)
						fInterpParamToUse = 1.0f;

					fActionBlends[iAction] = fInterpParamToUse * fInterpFraction;
					fInterpFraction *= (1.0f - pSqr->prevActions[iAction].fInterpParam);
				}
			}

			for (iAction=pSqr->iNumPrevActions-1; iAction>=0; --iAction)
			{
				if (!pSqr->prevActions[iAction].bPrevActionDone)
					dynSequencerLog(pSqr, "  %.1f - Stack %d: %s\n", 100.0f * fActionBlends[iAction], iAction+1, SAFE_MEMBER(pSqr->prevActions[iAction].pAction, pcName));
			}
		}
		dynSequencerLogEx(pSqr, "  %.1f - Current: %s\n", 100.0f * (pSqr->fInterpParam>=0.0f?pSqr->fInterpParam:1.0f), SAFE_MEMBER(pSqr->currAction.pAction, pcName));
		dynSequencerLogEx(pSqr, "          Next  : %s\n", SAFE_MEMBER(pSqr->nextAction.pAction, pcName));
		//dynSequencerLog(pSqr, "       Move    : %s\n", SAFE_MEMBER(pSqr->nextAction.pMoveSeq,pcDynMoveSeq));

		dynSequencerLogEx(pSqr, "-----------------------\n\n");
	}

	if (pSkeleton && pSqr->currAction.pAction)
	{
		if (pSkeleton->pFxManager)
		{
			FOR_EACH_IN_EARRAY(pSqr->currAction.pAction->eaSuppress, const char, pcSuppressTag)
			{
				dynFxManSuppress(pSkeleton->pFxManager, pcSuppressTag);
			}
			FOR_EACH_END;
		}

		if (pSqr->currAction.pAction->bOverrideAll)
		{
			pSkeleton->iOverrideSeqIndex = iSeqIndex;
			pSkeleton->bOverrideAll = true;
		}

		if (pSqr->currAction.pAction->bSnapOverrideAll)
		{
			pSkeleton->iOverrideSeqIndex = iSeqIndex;
			pSkeleton->bSnapOverrideAllOld = true;
		}

		if (pSqr->currAction.pAction->bForceVisible)
		{
			pSkeleton->bForceVisible = true;
		}
	}

	if (pSqr->bOverlay)
	{
		/*
		AUTO_FLOAT(overlayBlendRate, 5.0f);
		if (pSqr->currSeq.pSeq->bDefaultSequence)
			pSkeleton->fOverlayBlend -= overlayBlendRate * fDeltaTime;
		else
			pSkeleton->fOverlayBlend += overlayBlendRate * fDeltaTime;
		pSkeleton->fOverlayBlend = CLAMP(pSkeleton->fOverlayBlend, 0.0f, 1.0f);
			*/
		// Instead of blending overtime, do an instantenous flip
		pSqr->fOverlayBlend = pSqr->currSeq.pSeq->bDefaultSequence ? 0.f : 1.f;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

void dynSeqDebugInitDebugBits()
{
	dynBitFieldGroupClearAll(&debugBFG);
}




__forceinline static void danimAddDebugBits(ACMD_SENTENCE pcBits, bool bToggle)
{
	dynBitFieldGroupAddBits( &debugBFG, pcBits, bToggle );
}

// Flashes all bits listed after the command.
AUTO_COMMAND ACMD_CATEGORY(dynAnimation);
void danimFlashBits(ACMD_SENTENCE pcBits)
{
	danimAddDebugBits(pcBits, false);
}

// Toggles all bits listed after the command.
AUTO_COMMAND ACMD_CATEGORY(dynAnimation);
void danimToggleBits(ACMD_SENTENCE pcBits)
{
	danimAddDebugBits(pcBits, true);
}

AUTO_COMMAND ACMD_CATEGORY(dynAnimation);
void danimToggleStance(ACMD_NAMELIST(dynAnimStanceList) ACMD_SENTENCE pcStances)
{
	char *strtokcontext = 0;

	//meaningless call to reset strTok
	char* pcStance = strTokWithSpacesAndPunctuation(NULL, NULL);

	pcStance = strTokWithSpacesAndPunctuation(pcStances, " ");

	while (pcStance)
	{
		const char* pcSCStance = allocAddString(pcStance);
		int iIndex = eaFind(&dynDebugState.eaDebugStances, pcSCStance);
		if (iIndex >= 0)
			eaRemoveFast(&dynDebugState.eaDebugStances, iIndex);
		else
			eaPush(&dynDebugState.eaDebugStances, pcSCStance);
		pcStance = strTokWithSpacesAndPunctuation(pcStances, " ");
	}
}

void dynSeqPushOverride(SA_PARAM_NN_VALID DynSequencer* pSqr, SA_PARAM_NN_VALID DynAnimOverride* pOverride)
{
	U8 i;

	// No overrides on overlay sequencers, will just get applied multiple times!
	if (pSqr->bOverlay)
		return;

	for (i=0; i<pSqr->uiNumAnimOverrides; ++i)
	{
		DynAnimOverrideUpdater* pUpdater = &pSqr->animOverrideUpdater[i];
		if (pUpdater->pOverride == pOverride)
		{
			pUpdater->bActive = true;
			return;
		}
	}
	// Didn't find it, so create a new one if there's room
	if (pSqr->uiNumAnimOverrides < MAX_ANIM_OVERRIDES && GET_REF(pOverride->hMove) && GET_REF(pSqr->hSkelInfo))
	{
		DynAnimOverrideUpdater* pUpdater = &pSqr->animOverrideUpdater[pSqr->uiNumAnimOverrides++];
		pUpdater->pOverride = pOverride;
		pUpdater->bActive = true;
		pUpdater->fBlendValue = 0.0f;
		pUpdater->fCurrentFrame = 0.0f;
		pUpdater->pDynMoveSeq = dynMoveSeqFromDynMove(GET_REF(pOverride->hMove), GET_REF(pSqr->hSkelInfo));
	}
}

void dynSeqClearOverrides(SA_PARAM_NN_VALID DynSequencer* pSqr)
{
	U8 i;
	for (i=0; i<pSqr->uiNumAnimOverrides; ++i)
	{
		pSqr->animOverrideUpdater[i].bActive = false;
	}
}

bool dynSeqNeverOverride(SA_PARAM_NN_VALID DynSequencer* pSqr)
{
	return !!pSqr->bNeverOverride;
}

AUTO_CMD_INT(dynDebugState.bDrawNumSkels, danimShowCount) ACMD_CATEGORY(dynAnimation);

void dynSeqSetLogReceiver(SA_PARAM_NN_VALID DynSequencer* pSqr, GenericLogReceiver* glr)
{
	pSqr->glr = glr;
}

S32 dynSeqLoggingIsEnabledEx(SA_PARAM_NN_VALID DynSequencer* pSqr){
	return !!pSqr->glr;
}

AUTO_RUN;
void dynSeq_InitStrings(void)
{
	pcDynSeqBitForward   = allocAddStaticString("Forward");
	pcDynSeqBitBackward  = allocAddStaticString("Backward");
	pcDynSeqBitMoveLeft  = allocAddStaticString("Moveleft");
	pcDynSeqBitMoveRight = allocAddStaticString("Moveright");
}

void dynSeqSetStoppedYaw(DynSequencer *pSqr)
{
	pSqr->currAction.bStoppedTP = true;
}

U32 dynSeqMovementWasStopped(DynSequencer *pSqr)
{
	const DynActionBlock *pActionBlock;
	const DynAction *pAction;
	const DynSeqData *pSeqData;
	F32 fInterpolation;
	U32 uiHasMotion;

	pActionBlock = &pSqr->currAction;
	if (pActionBlock->bStoppedTP) return 1;
	pAction = pActionBlock->pAction;
	pSeqData = pAction->pParentSeq;
	if (pSeqData->bDisableTorsoPointing) return 1;
	fInterpolation = dynAnimInterpolationCalcInterp(pSqr->fInterpParam, &pSqr->currAction.pAction->interpBlock);

	uiHasMotion = 0;
	FOR_EACH_IN_EARRAY(pSeqData->requiresBits.ppcBits, const char, pcRequiredBit)
	{
		if (pcRequiredBit == pcDynSeqBitForward  ||
			pcRequiredBit == pcDynSeqBitBackward ||
			pcRequiredBit == pcDynSeqBitMoveLeft ||
			pcRequiredBit == pcDynSeqBitMoveRight)
		{
			uiHasMotion = 1;
			break;
		}
	}
	FOR_EACH_END;
	if (!uiHasMotion)
		return 1;

	if (pSqr->iNumPrevActions > 0 && pSqr->fInterpParam >= 0.0f)
	{
		int i;
		for (i = 0; i < pSqr->iNumPrevActions; i++)
		{
			pActionBlock = &pSqr->prevActions[i];
			if (pActionBlock->bStoppedTP) return 1;
			pAction = pActionBlock->pAction;
			pSeqData = pAction->pParentSeq;
			if (pSeqData->bDisableTorsoPointing) return 1;
			fInterpolation = dynAnimInterpolationCalcInterp(pSqr->prevActions[i].fInterpParam, &pSqr->prevActions[i].pAction->interpBlock);

			uiHasMotion = 0;
			FOR_EACH_IN_EARRAY(pSeqData->requiresBits.ppcBits, const char, pcRequiredBit)
			{
				if (pcRequiredBit == pcDynSeqBitForward  ||
					pcRequiredBit == pcDynSeqBitBackward ||
					pcRequiredBit == pcDynSeqBitMoveLeft ||
					pcRequiredBit == pcDynSeqBitMoveRight)
				{
					uiHasMotion = 1;
					break;
				}
			}
			FOR_EACH_END;
			if (!uiHasMotion)
				return 1;
		}
	}

	return 0;
}

F32 dynSeqMovementCalcBlockYaw(DynSequencer *pSqr, F32 *fYawRateOut, F32 *fYawStoppedOut, U32 eTargetDir)
{
	F32 fResult = 0.0f, fAngle, fAnglePrev;
	bool bFirst = true;

	const DynActionBlock *pActionBlock;
	const DynAction *pAction;
	const DynSeqData *pSeqData;
	F32 fInterpolation;
	int i;
	
	bool bForward,  bPrevForward  = false;
	bool bBackward, bPrevBackward = false;
	bool bLeft,     bPrevLeft     = false;
	bool bRight,    bPrevRight    = false;

	*fYawRateOut += 15.0f;
	MIN1(*fYawRateOut,100.0f);

	//if (eTargetDir == ETargetDir_Forward)		printfColor(COLOR_BLUE, "target forward\n");
	//if (eTargetDir == ETargetDir_RightForward)	printfColor(COLOR_BLUE, "target foward-right\n");
	//if (eTargetDir == ETargetDir_Right)			printfColor(COLOR_BLUE, "target right\n");
	//if (eTargetDir == ETargetDir_RightBack)		printfColor(COLOR_BLUE, "target back-right\n");
	//if (eTargetDir == ETargetDir_Back)			printfColor(COLOR_BLUE, "target back\n");
	//if (eTargetDir == ETargetDir_LeftBack)		printfColor(COLOR_BLUE, "target back-left\n");
	//if (eTargetDir == ETargetDir_Left)			printfColor(COLOR_BLUE, "target left\n");
	//if (eTargetDir == ETargetDir_LeftForward)	printfColor(COLOR_BLUE, "target foward-left\n");

	for (i = pSqr->fInterpParam >= 0.f ? pSqr->iNumPrevActions-1 : -1; i >= -1; i--)
	{
		F32 fInitAngle;
		bool bSwap = false;

		if (i >= 0) {
			pActionBlock = &pSqr->prevActions[i];
			pAction = pActionBlock->pAction;
			pSeqData = pAction->pParentSeq;
			fInterpolation = dynAnimInterpolationCalcInterp(pSqr->prevActions[i].fInterpParam, &pSqr->prevActions[i].pAction->interpBlock);
		} else {
			pActionBlock = &pSqr->currAction;
			pAction = pActionBlock->pAction;
			pSeqData = pAction->pParentSeq;
			fInterpolation = dynAnimInterpolationCalcInterp(pSqr->fInterpParam, &pSqr->currAction.pAction->interpBlock);
		}

		bForward  = false;
		bBackward = false;
		bLeft     = false;
		bRight    = false;

		FOR_EACH_IN_EARRAY(pSeqData->requiresBits.ppcBits, const char, pcRequiredBit)
		{
			if      (pcRequiredBit == pcDynSeqBitForward)   bForward  = true;
			else if (pcRequiredBit == pcDynSeqBitBackward)  bBackward = true;
			else if (pcRequiredBit == pcDynSeqBitMoveLeft)  bLeft     = true;
			else if (pcRequiredBit == pcDynSeqBitMoveRight) bRight    = true;
		}
		FOR_EACH_END;

		if (i == -1 &&
			(	!bForward && !bBackward && !bLeft && !bRight
				||
				(	eTargetDir == ETargetDir_Forward      && bBackward && !bLeft && !bRight    ||
					eTargetDir == ETargetDir_RightForward && bBackward && bLeft                ||
					eTargetDir == ETargetDir_Right        && bLeft && !bForward && !bBackward  ||
					eTargetDir == ETargetDir_RightBack    && bForward && bLeft                 ||
					eTargetDir == ETargetDir_Back         && bForward && !bLeft && !bRight     ||
					eTargetDir == ETargetDir_LeftBack     && bForward && bRight                ||
					eTargetDir == ETargetDir_Left         && bRight && !bForward && !bBackward ||
					eTargetDir == ETargetDir_LeftForward  && bBackward && bRight
				)))
		{
			switch (eTargetDir) {
				xcase ETargetDir_Forward:		fAngle = RAD(0.f);
				xcase ETargetDir_RightForward:	fAngle = RAD(45.f);
				xcase ETargetDir_Right:			fAngle = RAD(90.f);
				xcase ETargetDir_RightBack:		fAngle = RAD(135.f);
				xcase ETargetDir_Back:			fAngle = RAD(180.f);
				xcase ETargetDir_LeftBack:		fAngle = RAD(-135.f);
				xcase ETargetDir_Left:			fAngle = RAD(-90.f);
				xcase ETargetDir_LeftForward:	fAngle = RAD(-45.f);
				xdefault:						fAngle = RAD(0.f); 
			}
			//printfColor(COLOR_BLUE|COLOR_BRIGHT, "fResult = %f (ETargetDirection Flip)", DEG(fAngle));
			fResult = fAnglePrev = fAngle;
		}
		else if (TRUE_THEN_RESET(bFirst) ||
			!bPrevLeft    && !bPrevRight    && (bForward && bPrevBackward || bBackward && bPrevForward) ||
			!bPrevForward && !bPrevBackward && (bLeft    && bPrevRight    || bRight    && bPrevLeft))
		{
			if (bForward) {
				if      (bLeft)  fAngle = RAD(-45.f);
				else if (bRight) fAngle = RAD( 45.f);
				else             fAngle = RAD(  0.f);
			}
			else if (bBackward) {
				if      (bLeft)  fAngle = RAD(-135.f);
				else if (bRight) fAngle = RAD( 135.f);
				else             fAngle = RAD( 180.f);
			}
			else if (bLeft) {
				fAngle = RAD(-90.f);
			}
			else if (bRight) {
				fAngle = RAD(90.f);
			}
			else {
				fAngle = *fYawStoppedOut;
			}
			//printfColor(COLOR_BLUE|COLOR_BRIGHT, "fResult = %f (bFIRST)", DEG(fAngle));
			fResult = fAnglePrev = fAngle;
		}
		else
		{
			if (bForward) {
				if      (bLeft)  fInitAngle = RAD(-45.f);
				else if (bRight) fInitAngle = RAD( 45.f);
				else             fInitAngle = RAD(  0.f);
			}
			else if (bBackward) {
				if      (bLeft)  fInitAngle = RAD(-135.f);
				else if (bRight) fInitAngle = RAD( 135.f);
				else             fInitAngle = RAD( 180.f);
			}
			else if (bLeft) {
				fInitAngle = RAD(-90.f);
			}
			else if (bRight) {
				fInitAngle = RAD(90.f);
			}
			else {
				fInitAngle = fResult;
			}

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

			if (!bSwap)
			{
				F32 fTemp = fResult;
				fResult = (1.0f-fInterpolation)*fResult + fInterpolation*fAngle;
				//printfColor(COLOR_BLUE|COLOR_BRIGHT, "fResult = %f = %f*%f + %f*%f", DEG(fResult), 1.f-fInterpolation, DEG(fTemp), fInterpolation, DEG(fAngle));
			}
			else
			{
				//printfColor(COLOR_BLUE|COLOR_BRIGHT, "fResult = %f (swap)", DEG(fAngle));
				fResult = fAngle;
			}

			while (fResult >= TWOPI) fResult -= TWOPI;
			if (fResult > PI) fResult -= TWOPI;

			fAnglePrev = fInitAngle;
		}

		//if (bForward)  printfColor(COLOR_BLUE|COLOR_BRIGHT, " forward");
		//if (bBackward) printfColor(COLOR_BLUE|COLOR_BRIGHT, " backward");
		//if (bLeft)     printfColor(COLOR_BLUE|COLOR_BRIGHT, " left");
		//if (bRight)    printfColor(COLOR_BLUE|COLOR_BRIGHT, " right");
		//printf("\n");

		bPrevForward  = bForward;
		bPrevBackward = bBackward;
		bPrevLeft     = bLeft;
		bPrevRight    = bRight;
	}

	//*fYawStoppedOut = fResult;
	return fResult;
}

F32 dynSeqEnableTorsoPointingTime(DynSequencer *pSqr)
{
	if (SAFE_MEMBER(pSqr,prevActions[0].pMoveSeq))
	{
		return pSqr->prevActions[0].pMoveSeq->fDisableTorsoPointingTimeout;
	}
	return 0.0f;
}

F32 dynSeqDisableTorsoPointingTime(DynSequencer *pSqr)
{
	if (SAFE_MEMBER(pSqr,currAction.pMoveSeq))
	{
		return pSqr->currAction.pMoveSeq->fDisableTorsoPointingTimeout;
	}
	return 0.0f;
}

F32 dynSeqGetBlend(DynSequencer *pSqr)
{
	if (pSqr->bOverlay) {
		return pSqr->fOverlayBlend;
	}
	return 1.0f;
}

bool dynSeqIsOverlay_DbgOnly(DynSequencer *pSqr)
{
	// please only use this for danimShowBits
	return pSqr->bOverlay;
}