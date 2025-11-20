

#include "rand.h"
#include "timing.h"

#include "dynAction.h"
#include "wlSkelInfo.h"
#include "dynMove.h"
#include "wlState.h"
#include "dynSeqData.h"
#include "dynSkeleton.h"
#include "dynFxDebug.h"
#include "quat.h"

#include "dynAction_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

#define DYN_ACTION_DEFAULT_INTERP_FRAMES 5.0f

static F32 dynActionGetMaxLength(const DynAction* pAction);
static F32 dynActionMoveGetMaxLength(const DynActionMove* pActionMove);

static bool dynActCallFxVerify(SA_PARAM_NN_VALID DynActCallFx* pActCallFx, SA_PARAM_NN_STR const char* pcDebugFileName, F32 fMaxLength)
{
	if (!dynFxInfoExists(pActCallFx->pcFx))
	{
		AnimFileError(pcDebugFileName, "Unknown FX %s called", pActCallFx->pcFx);
		return false;
	}
	if ((F32)pActCallFx->iFrame > fMaxLength)
	{
		AnimFileError(pcDebugFileName, "Line %d: Frame %d in Fx Call %s exceeds max action length of %d", pActCallFx->iLineNum, pActCallFx->iFrame, pActCallFx->pcFx, qtrunc(fMaxLength));
		return false;
	}
	return true;
}

static bool dynActTriggerImpactVerify(SA_PARAM_NN_VALID DynActTriggerImpact* pImpact, SA_PARAM_NN_STR const char* pcDebugFileName, F32 fMaxLength)
{
	if (vec3IsZero(pImpact->vDirection))
	{
		AnimFileError(pcDebugFileName, "Must specify direction in Trigger block!");
		return false;
	}
	normalVec3(pImpact->vDirection);

	if ((F32)pImpact->iFrame > fMaxLength)
	{
		AnimFileError(pcDebugFileName, "Line %d: Frame %d in impact trigger exceeds max action length of %d", pImpact->iLineNum, pImpact->iFrame, qtrunc(fMaxLength));
		return false;
	}
	return true;
}

static bool dynActionMoveVerify(SA_PARAM_NN_VALID DynActionMove* pActionMove, SA_PARAM_NN_STR const char* pcDebugFileName)
{
	const char* pcBadBit = NULL;
	F32 fMaxLength;
	pActionMove->bVerified = false;

	if (!GET_REF(pActionMove->hMove))
	{
		AnimFileError(pcDebugFileName, "Can't find move %s", REF_STRING_FROM_HANDLE(pActionMove->hMove));
		return false;
	}

	// Verify setbits
	if (!dynBitFieldStaticSetFromStrings(&pActionMove->setBits, &pcBadBit))
	{
		AnimFileError(pcDebugFileName, "Invalid bit %s in DynActionMove %s SetBits", pcBadBit, REF_STRING_FROM_HANDLE(pActionMove->hMove));
		return false;
	}

	if (!dynBitFieldStaticSetFromStrings(&pActionMove->logic.off, &pcBadBit) || !dynBitFieldStaticSetFromStrings(&pActionMove->logic.on, &pcBadBit))
	{
		AnimFileError(pcDebugFileName, "Invalid If Bit  %s in DynAction %s", pcBadBit, REF_STRING_FROM_HANDLE(pActionMove->hMove));
		return false;
	}

	if (pActionMove->logic.on.uiNumBits > 0 || pActionMove->logic.off.uiNumBits > 0)
	{
		pActionMove->bRequiresBits = true;
	}


	fMaxLength = dynActionMoveGetMaxLength(pActionMove);


	// Verify CallFx Blocks
	if (!(wl_state.load_flags & WL_NO_LOAD_DYNFX))
	{
		FOR_EACH_IN_EARRAY(pActionMove->eaCallFx, DynActCallFx, pFxCall)
			if (!dynActCallFxVerify(pFxCall, pcDebugFileName, fMaxLength))
				return false;
		FOR_EACH_END
	}

	// Verify hit react impact triggers
	FOR_EACH_IN_EARRAY(pActionMove->eaImpact, DynActTriggerImpact, pImpact)
	{
		if (!dynActTriggerImpactVerify(pImpact, pcDebugFileName, fMaxLength))
			return false;
	}
	FOR_EACH_END;

	if (pActionMove->fChance <= 0.0f)
	{
		AnimFileError(pcDebugFileName, "DynActionMove %s Chance %.2f is invalid, must be greater than zero!", REF_STRING_FROM_HANDLE(pActionMove->hMove), pActionMove->fChance);
		return false;
	}




	pActionMove->bVerified = true;
	return true;
}

static bool dynActIfBlockVerify(SA_PARAM_NN_VALID DynActIfBlock* pActIfBlock, SA_PARAM_NN_VALID DynAction* pAction)
{
	const char* pcBadBit = NULL;
	if (!dynBitFieldStaticSetFromStrings(&pActIfBlock->logic.off, &pcBadBit) || !dynBitFieldStaticSetFromStrings(&pActIfBlock->logic.on, &pcBadBit))
	{
		AnimFileError(pAction->pcFileName, "Invalid If Bit  %s in DynAction %s", pcBadBit, pAction->pcName);
		return false;
	}
	if (!dynBitFieldStaticSetFromStrings(&pActIfBlock->setBits, &pcBadBit))
	{
		AnimFileError(pAction->pcFileName, "Invalid If-SetBit %s in DynAction %s", pcBadBit, pAction->pcName);
		return false;
	}
	return true;
}

static bool dynActFirstIfBlockVerify(SA_PARAM_NN_VALID DynActFirstIfBlock* pActFirstIfBlock, SA_PARAM_NN_VALID DynAction* pAction)
{
	const char* pcBadBit = NULL;
	pActFirstIfBlock->pAction = pAction;
	if (!dynBitFieldStaticSetFromStrings(&pActFirstIfBlock->logic.off, &pcBadBit) || !dynBitFieldStaticSetFromStrings(&pActFirstIfBlock->logic.on, &pcBadBit))
	{
		AnimFileError(pAction->pcFileName, "Invalid FirstIf Bit %s in DynAction %s", pcBadBit, pAction->pcName);
		return false;
	}
	return true;
}



bool dynActionVerify(DynAction* pDynAction, DynSeqData* pSeqData)
{
	F32 fProbabilityTotal = 0.0f;
	bool bFoundNonLogicalActionMove = false;
	const char* pcBadBit = NULL;
	F32 fMaxActionLength;

	pDynAction->bVerified = false;
	// Verify name
	if (!pDynAction->pcName)
	{
		AnimFileError(pDynAction->pcFileName, "A DynAction must have a Name");
		return false;
	}

	// Process Moves
	FOR_EACH_IN_EARRAY(pDynAction->eaDynActionMoves, DynActionMove, pDynActionMove)
	{
		if (!dynActionMoveVerify(pDynActionMove, pDynAction->pcFileName))
			return false;
		if (pDynActionMove->bRequiresBits)
		{
			eaPush(&pDynAction->eaLogicDynActionMoves, pDynActionMove);
			eaRemove(&pDynAction->eaDynActionMoves, ipDynActionMoveIndex);
			continue;
		}
		else
		{
			bFoundNonLogicalActionMove = true;
			fProbabilityTotal += pDynActionMove->fChance;
		}
	}
	FOR_EACH_END;

	if (pDynAction->bOverrideSeqs && pDynAction->bForceLowerBody)
	{
		AnimFileError(pDynAction->pcFileName, "DynAction %s in DynSequence %s has both OverrideSeqs and ForceLowerBody set. These flags are not compatible.", pDynAction->pcName, pSeqData->pcName);
		return false;
	}

	if (!bFoundNonLogicalActionMove)
	{
		AnimFileError(pDynAction->pcFileName, "DynAction %s in DynSequence %s must have at least one DynMove with no If Section specified", pDynAction->pcName, pSeqData->pcName);
		return false;
	}

	// Normalize probabilities if necessary
	if ( fabsf(fProbabilityTotal - 1.0f ) > 0.0001f )
	{
		F32 fNormalizationFactor = 1.0f / fProbabilityTotal;
		const U32 uiNumMATs = eaSize(&pDynAction->eaDynActionMoves);
		U32 uiATIndex;
		for (uiATIndex=0; uiATIndex<uiNumMATs; ++uiATIndex)
		{
			pDynAction->eaDynActionMoves[uiATIndex]->fChance *= fNormalizationFactor;
		}
	}


	// Verify If Blocks
	{
		const U32 uiNumIfBlocks = eaSize(&pDynAction->eaIf);
		U32 uiIfIndex;
		for (uiIfIndex=0; uiIfIndex<uiNumIfBlocks; ++uiIfIndex)
		{
			if (!dynActIfBlockVerify(pDynAction->eaIf[uiIfIndex], pDynAction))
				return false;
		}
	}
	{
		const U32 uiNumFirstIfBlocks = eaSize(&pDynAction->eaFirstIf);
		U32 uiFirstIfIndex;
		for (uiFirstIfIndex=0; uiFirstIfIndex<uiNumFirstIfBlocks; ++uiFirstIfIndex)
		{
			if (!dynActFirstIfBlockVerify(pDynAction->eaFirstIf[uiFirstIfIndex], pDynAction))
				return false;
		}
	}


	fMaxActionLength = dynActionGetMaxLength(pDynAction);

	// Verify CallFx Blocks
	if (!(wl_state.load_flags & WL_NO_LOAD_DYNFX))
	{
		FOR_EACH_IN_EARRAY(pDynAction->eaCallFx, DynActCallFx, pFxCall)
			if (!dynActCallFxVerify(pFxCall, pDynAction->pcFileName, fMaxActionLength))
				return false;
		FOR_EACH_END
	}

	FOR_EACH_IN_EARRAY(pDynAction->eaImpact, DynActTriggerImpact, pImpact)
	{
		if (!dynActTriggerImpactVerify(pImpact, pDynAction->pcFileName, fMaxActionLength))
			return false;
	}
	FOR_EACH_END;

	// Verify setbits
	if (!dynBitFieldStaticSetFromStrings(&pDynAction->setBits, &pcBadBit))
	{
		AnimFileError(pDynAction->pcFileName, "Invalid If Bit %s in SetBits in DynAction %s", pcBadBit, pDynAction->pcName);
		return false;
	}

	pDynAction->bVerified = true;
	return true;
}

__forceinline static const DynAction* dynActionFromNameAndList(SA_PARAM_NN_STR const char* pcNextAction, SA_PARAM_NN_VALID const DynAction*** pppActionList)
{
	const U32 uiNumActions = eaSize(pppActionList);
	U32 uiActionIndex;
	for (uiActionIndex=0; uiActionIndex<uiNumActions; ++uiActionIndex)
	{
		const DynAction* pAction = (*pppActionList)[uiActionIndex];
		if ( pcNextAction == pAction->pcName )
			return pAction;
	}
	return NULL;
}

bool dynActionLookupNextActions( DynAction* pAction, const DynAction*** pppActionList)
{
	if ( pAction->pcNextAction )
	{
		pAction->pNextAction = dynActionFromNameAndList(pAction->pcNextAction, pppActionList);
		if ( !pAction->pNextAction )
		{
			AnimFileError(pAction->pcFileName, "Failed to find NextAction %s in Action %s", pAction->pcNextAction, pAction->pcName);
			return false;
		}
	}

	if (eaSize(&pAction->eaNextActionChance) == 1)
	{
		AnimFileError(pAction->pcFileName, "In Action %s, only found one NextActionChance. Please make at least 2, or just use NextAction.", pAction->pcName);
		return false;
	}

	{
		F32 fProbabilityTotal = 0.0f;
		FOR_EACH_IN_EARRAY(pAction->eaNextActionChance, DynNextActionChance, pNextActionChance)
			pNextActionChance->pNextAction = dynActionFromNameAndList(pNextActionChance->pcNextAction, pppActionList);
			if (!pNextActionChance->pNextAction)
			{
				AnimFileError(pAction->pcFileName, "Failed to find NextActionChance %s in Action %s", pNextActionChance->pcNextAction, pAction->pcName);
				return false;
			}
			if (pNextActionChance->fChance <= 0.0f)
			{
				AnimFileError(pAction->pcFileName, "In Action %s, NextActionChance %s has zero or less probability!", pAction->pcName, pNextActionChance->pcNextAction);
				return false;
			}
			fProbabilityTotal += pNextActionChance->fChance;
		FOR_EACH_END

		// Now, normalize the chances
		FOR_EACH_IN_EARRAY(pAction->eaNextActionChance, DynNextActionChance, pNextActionChance)
			pNextActionChance->fChance /= fProbabilityTotal;
		FOR_EACH_END
	}

	{
		const U32 uiNumIfBlocks = eaSize(&pAction->eaIf);
		U32 uiIfIndex;
		for (uiIfIndex=0; uiIfIndex<uiNumIfBlocks; ++uiIfIndex)
		{
			DynActIfBlock* pIfBlock = pAction->eaIf[uiIfIndex];
			if ( pIfBlock->pcNextAction )
			{
				pIfBlock->pNextAction = dynActionFromNameAndList(pIfBlock->pcNextAction, pppActionList);
				if ( !pIfBlock->pNextAction )
				{
					AnimFileError(pAction->pcFileName, "Failed to find NextAction %s in Action %s If Block", pIfBlock->pcNextAction, pAction->pcName);
					return false;
				}
			}
		}
	}
	return true;
}

static void dynActionProcessFxCallsHelper( DynSkeleton* pSkeleton, F32 fStartTime, F32 fEndTime, DynFxManager* pFxManager, const DynActCallFx** eaCallFx, const U32 uiNumFxCalls, const DynActTriggerImpact** eaImpact, const U32 uiNumImpacts) 
{
	if ( uiNumFxCalls || uiNumImpacts)
	{
		int iStartFrame = ceil(fStartTime);
		int iEndFrame = floor(fEndTime);
		U32 uiFxCall;
		U32 uiImpact;

		if ( iStartFrame > iEndFrame || fStartTime == fEndTime) // have not passed a frame yet
		{
			return;
		}


		for (uiFxCall=0; uiFxCall< uiNumFxCalls; ++ uiFxCall)
		{
			const DynActCallFx* pFxCall = eaCallFx[uiFxCall];
			if ( pFxCall->iFrame >= iStartFrame && pFxCall->iFrame <= iEndFrame )
			{
				// Make the call
				dynSkeletonQueueAnimationFx(pSkeleton, pFxManager, pFxCall->pcFx);
			}
		}

		for (uiImpact=0; uiImpact< uiNumImpacts; ++ uiImpact)
		{
			const DynActTriggerImpact* pImpact = eaImpact[uiImpact];
			if ( pImpact->iFrame >= iStartFrame && pImpact->iFrame <= iEndFrame )
			{
				// Trigger impact
				if (pSkeleton->uiEntRef)
				{
					const DynNode* pBone = dynSkeletonFindNode(pSkeleton, pImpact->pcBone);
					Quat qRootRot;
					dynNodeGetWorldSpaceRot(pSkeleton->pRoot, qRootRot);
					if (pBone)
					{
						DynTransform xBone;
						Vec3 vImpactDir;
						Vec3 vOffset;
						Vec3 vBonePos;
						dynNodeGetWorldSpaceTransform(pBone, &xBone);
						quatRotateVec3Inline(qRootRot, pImpact->vDirection, vImpactDir);
						quatRotateVec3Inline(xBone.qRot, pImpact->vOffset, vOffset);

						addVec3(xBone.vPos, vOffset, vBonePos);
						dynSkeletonQueueAnimReactTrigger(vBonePos, vImpactDir, pSkeleton, 0);
					}
				}
			}
		}
	}
}

void dynActionProcessFxAndImpactCalls(DynSkeleton* pSkeleton, const DynAction* pAction, U32 uiActionMoveIndex, DynFxManager* pFxManager, F32 fStartTime, F32 fEndTime)
{
	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	dynActionProcessFxCallsHelper(pSkeleton, fStartTime, fEndTime, pFxManager, pAction->eaCallFx, eaSize(&pAction->eaCallFx), pAction->eaImpact, eaSize(&pAction->eaImpact));

	// call action move fx too
	dynActionProcessFxCallsHelper(pSkeleton, fStartTime, fEndTime, pFxManager, pAction->eaDynActionMoves[uiActionMoveIndex]->eaCallFx, eaSize(&pAction->eaDynActionMoves[uiActionMoveIndex]->eaCallFx), pAction->eaDynActionMoves[uiActionMoveIndex]->eaImpact, eaSize(&pAction->eaDynActionMoves[uiActionMoveIndex]->eaImpact));

	// Process fx messages too

	{
		FOR_EACH_IN_EARRAY(pAction->eaDynActionMoves[uiActionMoveIndex]->eaFXMessages, DynAnimFXMessage, pMessage)
		{
			int iStartFrame = ceil(fStartTime);
			int iEndFrame = floor(fEndTime);
			if ( iStartFrame > iEndFrame || fStartTime == fEndTime) // have not passed a frame yet
			{
				PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
				return;
			}
			if (pMessage->iFrame >= iStartFrame && pMessage->iFrame <= iEndFrame)
			{
				// Send the message
				dynFxManBroadcastMessage(pFxManager, pMessage->pcMessage);
			}
		}
		FOR_EACH_END
	}
	PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
}

void dynActionSetBits(const DynAction* pAction, U32 uiActionMoveIndex, DynBitField* pBF, DynSequencer* pSqr)
{
	PERFINFO_AUTO_START_L3(__FUNCTION__,1);
	dynBitFieldSetAllFromBitFieldStatic(pBF, &pAction->setBits);
	dynBitFieldSetAllFromBitFieldStatic(pBF, &pAction->eaDynActionMoves[uiActionMoveIndex]->setBits);
	if (dynSeqLoggingIsEnabled(pSqr))
	{
		char cTemp[1024];
		dynBitFieldStaticWriteBitString(SAFESTR(cTemp), &pAction->setBits);
		if (cTemp[0])
			dynSequencerLog(pSqr, "Setting Detail bits %s\n", cTemp);
		dynBitFieldStaticWriteBitString(SAFESTR(cTemp), &pAction->eaDynActionMoves[uiActionMoveIndex]->setBits);
		if (cTemp[0])
			dynSequencerLog(pSqr, "Setting Detail bits %s\n", cTemp);
	}
	PERFINFO_AUTO_STOP_CHECKED_L3(__FUNCTION__);
}

static F32 dynActionMoveGetMaxLength(const DynActionMove* pActionMove)
{
	F32 fLength = 0.0f;
	const DynMove* pMove = GET_REF(pActionMove->hMove);
	if (pMove)
	{
		FOR_EACH_IN_EARRAY(pMove->eaDynMoveSeqs, const DynMoveSeq, pMoveSeq)
		{
			const F32 fMoveLength = dynMoveSeqGetLength(pMoveSeq);
			MAX1(fLength, fMoveLength);
		}
		FOR_EACH_END;
	}
	return fLength;
}

static F32 dynActionGetMaxLength(const DynAction* pAction)
{
	F32 fLength = 0.0f;
	FOR_EACH_IN_EARRAY(pAction->eaDynActionMoves, const DynActionMove, pActionMove)
	{
		const F32 fActionMoveLength = dynActionMoveGetMaxLength(pActionMove);
		MAX1(fLength, fActionMoveLength);
	}
	FOR_EACH_END;
	return fLength;
}



/*
void dynActionManagerReloadedDynMove(const char* pcDynMoveName)
{
	// Check to see if any actions reference this move, if so, reload them
	U32 uiNumActions = eaSize(&dynActionManager.eaDynActions);
	U32 uiActionIndex;
	for (uiActionIndex=0; uiActionIndex<uiNumActions; ++uiActionIndex)
	{
		dynActionReloadedDynMove(dynActionManager.eaDynActions[uiActionIndex], pcDynMoveName);
	}
}
*/

#define RETURN_IF_VALID(ref) if(GET_REF(ref)) return GET_REF(ref); Errorf("Unable to find DynMove %s in DynAction %s", REF_STRING_FROM_HANDLE(ref), pAction->pcName); return NULL;

static const DynMove* dynMoveFromAction(SA_PARAM_NN_VALID const DynAction* pAction, int iOldActionMoveIndex, SA_PARAM_NN_VALID U8* puiActionMoveIndex, U32* puiSeed, const DynBitField* pBits)
{
	const U32 uiNumMoves = eaSize(&pAction->eaDynActionMoves);

	FOR_EACH_IN_EARRAY_FORWARDS(pAction->eaLogicDynActionMoves, DynActionMove, pLogicActionMove)
	{
		if (dynBitFieldSatisfiesLogicBlock(pBits, &pLogicActionMove->logic))
		{
			RETURN_IF_VALID(pLogicActionMove->hMove);
		}
	}
	FOR_EACH_END;

	// If we got this far, we did not find a logic move

	// Generate a random number from 0.0 to 1.0, and then choose a move based on that, in order...
	// this assumes the probabilities have been normalized, which occured int he verify step
	{
		if ( iOldActionMoveIndex >= 0 && (U32)iOldActionMoveIndex >= uiNumMoves )
		{
			Errorf("Invalid OldActionMoveIndex %d, since there are only %d moves in action %s!", iOldActionMoveIndex, uiNumMoves, pAction->pcName);
			*puiActionMoveIndex = 0;
			return NULL;
		}
		if (pAction->bAllowRandomRepeats)
			iOldActionMoveIndex = -1;
		if ( uiNumMoves == 1 ) // The most common case
		{
			*puiActionMoveIndex = 0;
			RETURN_IF_VALID(pAction->eaDynActionMoves[0]->hMove);
		}
		else if (uiNumMoves == 2 && iOldActionMoveIndex >= 0)
		{
			if ( iOldActionMoveIndex == 0 )
				*puiActionMoveIndex = 1;
			else
				*puiActionMoveIndex = 0;
			RETURN_IF_VALID(pAction->eaDynActionMoves[*puiActionMoveIndex]->hMove);
		}
		else if (uiNumMoves > 1)
		{
			U32 uiMoveIndex;
			F32 fRandNum = randomPositiveF32Seeded(puiSeed, RandType_LCG);
			F32 fTotalProb = 0.0f;
			if ( iOldActionMoveIndex >= 0 )
			{
				// We have an old move, so effectively remove it from the probability index by removing the chance from fRandNum
				fRandNum *= ( 1.0f - pAction->eaDynActionMoves[iOldActionMoveIndex]->fChance );
			}
			for (uiMoveIndex = 0; uiMoveIndex < uiNumMoves; ++uiMoveIndex)
			{
				if ( uiMoveIndex == (U32)iOldActionMoveIndex )
					continue;
				fTotalProb += pAction->eaDynActionMoves[uiMoveIndex]->fChance;
				if ( fRandNum <= fTotalProb )
				{
					*puiActionMoveIndex = uiMoveIndex;
					RETURN_IF_VALID(pAction->eaDynActionMoves[uiMoveIndex]->hMove);
				}
			}
			// If we get here, something went wrong in our probability calc
			Errorf("Failure in probability calculation in action %s", pAction->pcName);
			*puiActionMoveIndex = 0;
			RETURN_IF_VALID(pAction->eaDynActionMoves[0]->hMove);
		}
		else
		{
			Errorf("Action %s has no moves!", pAction->pcName);
			*puiActionMoveIndex = 0;
			return NULL;
		}
	}
}

#define BAD_SEQ_TYPE_RANK 0xFFFFFFFF


const DynMoveSeq* dynMoveSeqFromAction(const DynAction* pAction, const SkelInfo* pSkelInfo, int iOldActionMoveIndex, U8* puiActionMoveIndex, U32* puiSeed, const DynBitField* pBits)
{
	const DynMove* pDynMove = dynMoveFromAction(pAction, iOldActionMoveIndex, puiActionMoveIndex, puiSeed, pBits);
	if (!pDynMove)
	{
		Errorf("Failed to find move from action %s", pAction->pcName);
		return NULL;
	}
	return dynMoveSeqFromDynMove(pDynMove, pSkelInfo);
}

#include "dynAction_h_ast.c"
