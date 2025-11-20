#include "FolderCache.h"
#include "fileutil.h"
#include "StringCache.h"
#include "rand.h"

#include "dynAnimGraph.h"
#include "dynAnimTrack.h"
#include "dynFxInfo.h"
#include "dynMove.h"
#include "dynSeqData.h"
#include "dynNodeInline.h"
#include "wlSkelInfo.h"
#include "wlState.h"

#include "dynMove_h_ast.c"

#define BAD_SEQ_TYPE_RANK 0xFFFFFFFF

#include <math.h>

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

U32 uiFixDynMoveOffByOneError = 0;
AUTO_CMD_INT( uiFixDynMoveOffByOneError, danimFixDynMoveOffByOneError ) ACMD_COMMANDLINE ACMD_CATEGORY(dynAnimation);

U32 uiShowMatchJointsBaseSkeleton = 0;
AUTO_CMD_INT( uiShowMatchJointsBaseSkeleton, danimShowMatchJointsBaseSkeleton ) ACMD_CATEGORY(dynAnimation);

DictionaryHandle hDynMoveDict;

static bool bDynMoveFinishedStartupLoading = false;

static void dynAnimFrameSnapshotCreate(DynAnimTrack* pAnimTrack, U32 uiFrame, DynAnimFrameSnapshot* pSnap);
static void dynAnimFrameSnapshotGetTransform(const DynAnimFrameSnapshot* pSnap, const char* pcBoneName, const DynTransform* pxBaseTransform, DynTransform* pxResult);

AUTO_COMMAND ACMD_CATEGORY(DEBUG);
void dynMoveRequestRandomTracks(U32 iNumRequest)
{
	DynMove *pTryMove;
	RefDictIterator iter;
	U32 uiBadRequest = 0;

	RefSystem_InitRefDictIterator(hDynMoveDict, &iter);

	while (	uiBadRequest < iNumRequest &&
			(pTryMove = (DynMove*)RefSystem_GetNextReferentFromIterator(&iter)))
	{
		if (eaSize(&pTryMove->eaDynMoveSeqs))
		{
			if (pTryMove->eaDynMoveSeqs[0]->dynMoveAnimTrack.pAnimTrackHeader->pcName &&
				!dynAnimTrackHeaderRequest(pTryMove->eaDynMoveSeqs[0]->dynMoveAnimTrack.pAnimTrackHeader))
			{
				//printfColor(COLOR_RED,"Requesting: %s\n", pTryMove->eaDynMoveSeqs[0]->dynMoveAnimTrack.pAnimTrackHeader->pcName);
				uiBadRequest++;
			}
		}
	}
}

static bool dynMoveAnimTrackVerify(DynMoveAnimTrack* t, const char* pcDebugFileName, const char* pcDebugMoveName, const char* pcDynMoveSeqName, U32 bBuildSnapShot)
{
	bool bRet = true;
	t->bVerified = false;

	if (bBuildSnapShot)
	{
		U32 uiTotalFrames;
		
		// Try to find the animtrack from the name
		if (!t->pcAnimTrackName)
		{
			AnimFileError(pcDebugFileName, " In DynMoveSeq %s, All DynMoveAnimTrack's must have a name of an animtrack", pcDynMoveSeqName);
			return false;
		}
		if (!(t->pAnimTrackHeader = dynAnimTrackHeaderFind(t->pcAnimTrackName)))
		{
			AnimFileError(pcDebugFileName, "Unable to find DynAnimTrack with name %s in DynMoveSeq %s, check in /data/animation_library/", t->pcAnimTrackName, pcDynMoveSeqName);
			return false;
		}

		// Since we're verifying, we're post text and not post bin, so we can load the file to pull out the first frame and validate frame counts
		if (!t->pAnimTrackHeader->bLoaded && !t->pAnimTrackHeader->bLoading)
			dynAnimTrackHeaderForceLoadTrack(t->pAnimTrackHeader);
		else if (t->pAnimTrackHeader->bLoading)
		{
			while (!t->pAnimTrackHeader->bLoaded)
				Sleep(1);
		}

		if (!t->pAnimTrackHeader->pAnimTrack)
		{
			Errorf("Somehow failed to load anim track %s", t->pcAnimTrackName);
			return false;
		}

		uiTotalFrames = t->pAnimTrackHeader->pAnimTrack->uiTotalFrames;
		assert(uiTotalFrames > 0);


		// Check out frame lengths
		t->uiFirstFrame = t->uiFirst;
		if ( t->uiFirstFrame > 0 )
			t->uiFirstFrame--; // convert to 0-first format
		if ( t->uiFirstFrame >= uiTotalFrames )
		{
			AnimFileError(	pcDebugFileName,
							"Invalid first frame %d (num frames is %d) in"
							" DynMove %s, DynMoveSeq %s, DynAnimTrack %s",
							t->uiFirstFrame+1,
							uiTotalFrames,
							pcDebugMoveName,
							pcDynMoveSeqName,
							t->pcAnimTrackName);

			t->uiFirstFrame = 0;
		}

		// Preload that first frame
		dynAnimFrameSnapshotCreate(t->pAnimTrackHeader->pAnimTrack, t->uiFirstFrame, &t->frameSnapshot);

		t->uiLastFrame = t->uiLast;
		if ( t->uiLastFrame > 0 )
			t->uiLastFrame--; // convert to 0-first format
		else if ( t->uiLastFrame == 0 )
			t->uiLastFrame = uiTotalFrames-1;
		if ( t->uiLastFrame >= uiTotalFrames )
		{
			AnimFileError(	pcDebugFileName,
							"Invalid last frame (%d) (track has %d frames) in"
							" DynMove %s, DynMoveSeq %s, DynAnimTrack %s",
							t->uiLastFrame+1,
							uiTotalFrames,
							pcDebugMoveName,
							pcDynMoveSeqName,
							t->pcAnimTrackName);

			t->uiLastFrame = uiTotalFrames - 1;
		}
		if(t->uiLastFrame < t->uiFirstFrame)
		{
			AnimFileError(	pcDebugFileName,
							"Last frame (%d) is before first frame (%d) (track has %d frames) in"
							" DynMove %s, DynMoveSeq %s, DynAnimTrack %s",
							t->uiLastFrame+1,
							t->uiFirstFrame+1,
							uiTotalFrames,
							pcDebugMoveName,
							pcDynMoveSeqName,
							t->pcAnimTrackName);

			t->uiLastFrame = t->uiFirstFrame;
		}
		else if(!uiFixDynMoveOffByOneError &&
				t->uiLastFrame == t->uiFirstFrame)
		{
			AnimFileError(	pcDebugFileName,
							"Last frame (%d) is the same as the first frame (%d) (track has %d frames) in"
							" DynMove %s, DynMoveSeq %s, DynAnimTrack %s",
							t->uiLastFrame+1,
							t->uiFirstFrame+1,
							uiTotalFrames,
							pcDebugMoveName,
							pcDynMoveSeqName,
							t->pcAnimTrackName);

			// I don't want to auto-adjust this to be +1 since the next frame of
			// animation may not actually exist, however, if someone has a zero-
			// frame animation in a graph node that's loopable then it'll eventually
			// trigger an infinite loop warning in the dynAnimGraphUpdater code.. so
			// this is an error that should be fixed
		}
	}
	
	//get the start offset frame range setup
	t->uiStartOffsetFirstFrame = t->uiStartOffsetFirst;
	if (t->uiStartOffsetFirstFrame > 0)
		t->uiStartOffsetFirstFrame--; //0-first format

	t->uiStartOffsetLastFrame = t->uiStartOffsetLast;
	if (t->uiStartOffsetLastFrame > 0)
		t->uiStartOffsetLastFrame--; //0-first format
	else if (t->uiStartOffsetLastFrame == 0 && t->uiStartOffsetFirstFrame != 0)
		t->uiStartOffsetLastFrame = t->uiLastFrame;

	if (t->uiStartOffsetFirstFrame < t->uiFirstFrame &&
		(t->uiStartOffsetFirstFrame != 0 || t->uiStartOffsetLastFrame != 0))
	{
		AnimFileError(	pcDebugFileName,
						"Invalid start offset first frame (%d) before the animtrack's first frame (%d) in"
						" DynMove %s, DynMoveSeq %s, DynAnimTrack %s",
						t->uiStartOffsetFirstFrame+1,
						t->uiFirstFrame+1,
						pcDebugMoveName,
						pcDynMoveSeqName,
						t->pcAnimTrackName);

		t->uiStartOffsetFirstFrame = t->uiFirstFrame;
	}

	if (t->uiStartOffsetLastFrame != 0 &&
		t->uiStartOffsetLastFrame > t->uiLastFrame) {
			AnimFileError(	pcDebugFileName,
							"Invalid start offset last frame (%d) after the animtrack's last frame (%d) in"
							" DynMove %s, DynMoveSeq %s, DynAnimTrack %s",
							t->uiStartOffsetLastFrame+1,
							t->uiLastFrame+1,
							pcDebugMoveName,
							pcDynMoveSeqName,
							t->pcAnimTrackName);

			t->uiStartOffsetLastFrame = t->uiLastFrame;
	}
	else if (t->uiStartOffsetLastFrame < t->uiStartOffsetFirstFrame) {
		AnimFileError(	pcDebugFileName,
						"Invalid start offset last frame (%d) before the start offset first frame (%d) in"
						" DynMove %s, DynMoveSeq %s, DynAnimTrack %s",
						t->uiStartOffsetLastFrame+1,
						t->uiStartOffsetFirstFrame+1,
						pcDebugMoveName,
						pcDynMoveSeqName,
						t->pcAnimTrackName);

		t->uiStartOffsetLastFrame = t->uiStartOffsetFirstFrame;
	}

	// Look at frame ranges
	{
		U32 uiFrameRange;
		const U32 uiNumFrameRanges = eaSize(&t->eaNoInterpFrameRange);
		for (uiFrameRange=0; uiFrameRange<uiNumFrameRanges; ++uiFrameRange)
		{
			DynMoveFrameRange* pFrameRange = t->eaNoInterpFrameRange[uiFrameRange];
			pFrameRange->iFirstFrame = pFrameRange->iFirst;
			pFrameRange->iLastFrame = pFrameRange->iLast;
			if (pFrameRange->iFirstFrame > 0)
				pFrameRange->iFirstFrame--; // 0-first format
			if (pFrameRange->iLastFrame > 0)
				pFrameRange->iLastFrame--; // 0-first format
			if ( pFrameRange->iFirstFrame < 0 )
			{
				t->bNoInterp = true;
				continue;
			}
			else if ( pFrameRange->iFirstFrame < (int)t->uiFirstFrame || pFrameRange->iFirstFrame > (int)t->uiLastFrame )
			{
				AnimFileError(pcDebugFileName, "Invalid NoInterp first frame %d in DynMove %s", pFrameRange->iFirstFrame, pcDebugMoveName);
				bRet = false;
			}
			if ( pFrameRange->iLastFrame == 0 )
			{
				pFrameRange->iLastFrame = pFrameRange->iFirstFrame;
			}
			else if ( pFrameRange->iLastFrame < (int)t->uiFirstFrame || pFrameRange->iLastFrame > (int)t->uiLastFrame || pFrameRange->iLastFrame < pFrameRange->iFirstFrame )
			{
				AnimFileError(pcDebugFileName, "Invalid NoInterp last frame %d in DynMove %s", pFrameRange->iLastFrame, pcDebugMoveName);
				bRet = false;
			}
		}
	}


	// Now that were done, unload the file
	t->bVerified = bRet;
	return bRet;
}

StashTable stUsedAnimTracks = NULL;

static bool dynMoveFxEventVerify(const DynMove *pDynMove, const DynMoveSeq *pDynMoveSeq, DynMoveFxEvent* pFxEvent)
{
	bool bRet = true;

	if (pDynMoveSeq)
	{
		U32 frames = pDynMoveSeq->dynMoveAnimTrack.uiLastFrame - pDynMoveSeq->dynMoveAnimTrack.uiFirstFrame + uiFixDynMoveOffByOneError;

		if (pFxEvent->uiFrame > 0) {
			pFxEvent->uiFrameTrigger = pFxEvent->uiFrame - 1; //convert to 0 first format
		} else {
			pFxEvent->uiFrameTrigger = 0;
		}

		if (pFxEvent->uiFrameTrigger > (int)frames)
		{
			AnimFileError(pDynMove->pcFilename, "Move %s, MoveSeq %s: FX %s is at frame %d which is past the end of the sequence", pDynMove->pcName, pDynMoveSeq->pcDynMoveSeq, pFxEvent->pcFx, pFxEvent->uiFrame);
			bRet = false;
		}
	}

	if (!pFxEvent->bMessage)
	{
		verbose_printf("Move %s, MoveSeq %s: Found non-message FX %s, are you sure you meant to do this?\n", pDynMove->pcName, pDynMoveSeq->pcDynMoveSeq, pFxEvent->pcFx);
		if (!dynFxInfoExists(pFxEvent->pcFx))
		{
			AnimFileError(pDynMove->pcFilename, "Move %s, MoveSeq %s: Unknown FX %s called", pDynMove->pcName, pDynMoveSeq->pcDynMoveSeq, pFxEvent->pcFx);
			bRet = false;
		}
	}

	return bRet;
}

static bool dynMoveSeqVerify(DynMoveSeq* pDynMoveSeq, const DynMove* pDynMove, U32 bBuildSnapShot)
{
	DynMoveAnimTrack* t = &pDynMoveSeq->dynMoveAnimTrack;
	const U32 uiNumDynMoveFxEvents = eaSize(&pDynMoveSeq->eaDynMoveFxEvents);
	U32 uiDynMoveFxEventsIndex;
	bool bRet = true;

	pDynMoveSeq->bVerified = false;
	pDynMoveSeq->pDynMove = pDynMove;
	pDynMoveSeq->fBlendRate = (pDynMoveSeq->fBlendFrames > 0.1f ? 30.f/pDynMoveSeq->fBlendFrames : 300.f);
	if (pDynMoveSeq->fDisableTorsoPointingTimeout < 0.f)
	{
		AnimFileError(	pDynMove->pcFilename,
						"In DynMove %s, DynMoveSeq %s, Disable Torso Pointing Time Out must be non-negative!",
						pDynMove->pcName,
						pDynMoveSeq->pcDynMoveSeq);
		bRet = false;
	}
	if (pDynMoveSeq->bIKBothHands)
	{
		if (pDynMoveSeq->bRegisterWep) {
			AnimFileError(	pDynMove->pcFilename,
							"DynMove %s, DynMoveSeq %s, setting IKBothHands and RegisterWep at same time is not supported!",
							pDynMove->pcName, pDynMoveSeq->pcDynMoveSeq);
			bRet = false;
		}
		if (pDynMoveSeq->bIKMeleeMode) {
			AnimFileError(	pDynMove->pcFilename,
							"DynMove %s, DynMoveSeq %s, setting IKBothHands and IKMeleeMode at same time is not supported!",
							pDynMove->pcName, pDynMoveSeq->pcDynMoveSeq);
			bRet = false;
		}
		if (pDynMoveSeq->bDisableIKLeftWrist) {
			AnimFileError(	pDynMove->pcFilename,
							"DynMove %s, DynMoveSeq %s, setting IKBothHands and DisableIKLeftWrist at same time is not supported!",
							pDynMove->pcName, pDynMoveSeq->pcDynMoveSeq);
			bRet = false;
		}
		if (pDynMoveSeq->bDisableIKRightArm) {
			AnimFileError(	pDynMove->pcFilename,
							"DynMove %s, DynMoveSeq %s, setting IKBothHands and DisableIKRightArm at same time is not supported!",
							pDynMove->pcName, pDynMoveSeq->pcDynMoveSeq);
			bRet = false;
		}
		if (pDynMoveSeq->bEnableIKSliding) {
			AnimFileError(	pDynMove->pcFilename,
							"DynMove %s, DynMoveSeq %s, setting IKBothHands and EnableIKSliding at same time is not supported!",
							pDynMove->pcName, pDynMoveSeq->pcDynMoveSeq);
			bRet = false;
		}
	}
	if (pDynMoveSeq->pcIKTargetNodeLeft ||
		pDynMoveSeq->pcIKTargetNodeRight)
	{
		if (pDynMoveSeq->pcIKTarget) {
			AnimFileError(	pDynMove->pcFilename,
							"DynMove %s, DynMoveSeq %s, setting the IK Target as an alias and a node at the same time is not supported!",
							pDynMove->pcName, pDynMoveSeq->pcDynMoveSeq);
			bRet = false;
		}
		if (!pDynMoveSeq->bIKBothHands) {
			AnimFileError(	pDynMove->pcFilename,
							"DynMove %s, DynMoveSeq %s, IKBothHands mode required to use IK target nodes!",
							pDynMove->pcName, pDynMoveSeq->pcDynMoveSeq);
			bRet = false;
		}
	}
	if (!dynMoveAnimTrackVerify(t, pDynMove->pcFilename, pDynMove->pcName, pDynMoveSeq->pcDynMoveSeq, bBuildSnapShot))
	{
		bRet = false;
	}
	else
	{
		pDynMoveSeq->fLength = (F32)(t->uiFrameOffset + t->uiLastFrame - t->uiFirstFrame + uiFixDynMoveOffByOneError);
		pDynMoveSeq->fRagdollStartTime = (F32)(pDynMoveSeq->uiRagdollFrame - t->uiFirst);
		if (pDynMoveSeq->fLength < 0.0f)
		{
			AnimFileError(	pDynMove->pcFilename,
							"In DynMove %s, DynMoveSeq %s, Animation length must be non-negative!",
							pDynMove->pcName,
							pDynMoveSeq->pcDynMoveSeq);
			pDynMoveSeq->fLength = 0.f;
			bRet = false;
		}
		if (pDynMoveSeq->bRagdoll &&
			gConf.bNewAnimationSystem)
		{
			if (pDynMoveSeq->uiRagdollFrame < t->uiFirst+1) {
				AnimFileError(	pDynMove->pcFilename,
					"In DynMove %s, DynMoveSeq %s, Ragdoll frame must be atleast 1 frame after the animtracks first frame!",
					pDynMove->pcName,
					pDynMoveSeq->pcDynMoveSeq);
				pDynMoveSeq->fRagdollStartTime = 0.0f;
				bRet = false;
			}
			if (pDynMoveSeq->uiRagdollFrame > t->uiLast-6) {
				AnimFileError(	pDynMove->pcFilename,
					"In DynMove %s, DynMoveSeq %s, Ragdoll frame must be atleast 6 frames before the animtracks last frame!",
					pDynMove->pcName,
					pDynMoveSeq->pcDynMoveSeq);
				pDynMoveSeq->fRagdollStartTime = 0.0f;
				bRet = false;
			}
		}
	}
	if (pDynMoveSeq->fSpeedHigh > 0.0f)
	{
		pDynMoveSeq->bRandSpeed = true;
		if (pDynMoveSeq->fSpeedHigh <= pDynMoveSeq->fSpeed)
		{
			AnimFileError(	pDynMove->pcFilename,
							"In DynMove %s, DynMoveSeq %s, either unset the high speed (%f) or make it greater than the low speed (%f)!",
							pDynMove->pcName,
							pDynMoveSeq->pcDynMoveSeq,
							pDynMoveSeq->fSpeedHigh,
							pDynMoveSeq->fSpeed);
			pDynMoveSeq->fSpeedHigh = 0.0f;
			pDynMoveSeq->bRandSpeed = false;
			bRet = false;
		}
	} else {
		pDynMoveSeq->bRandSpeed = false;
	}
	for (uiDynMoveFxEventsIndex=0; uiDynMoveFxEventsIndex<uiNumDynMoveFxEvents; ++uiDynMoveFxEventsIndex)
	{
		if (!dynMoveFxEventVerify(pDynMove, pDynMoveSeq, pDynMoveSeq->eaDynMoveFxEvents[uiDynMoveFxEventsIndex]))
			bRet = false;
	}
	if (pDynMoveSeq->fBankMaxAngle < 0.0f) {
		AnimFileError(	pDynMove->pcFilename,
						"In DynMove %s, DynMoveSeq %s, BankMaxAngle must be non-negative!",
						pDynMove->pcName,
						pDynMoveSeq->pcDynMoveSeq);
		pDynMoveSeq->fBankMaxAngle = 0.0f;
		bRet = false;
	}
	if (pDynMoveSeq->fBankScale < 0.0f) {
		AnimFileError(	pDynMove->pcFilename,
						"In DynMove %s, DynMoveSeq %s, BankScale must be non-negative!",
						pDynMove->pcName,
						pDynMoveSeq->pcDynMoveSeq);
		pDynMoveSeq->fBankScale = 0.0f;
		bRet = false;
	}
	pDynMoveSeq->bVerified = bRet;
	return bRet;
}

bool dynMoveVerify(DynMove* pDynMove, U32 bBuildSnapShot)
{
	bool bRet = true;
	const U32 uiNumDynMoveSeqs = eaSize(&pDynMove->eaDynMoveSeqs);
	U32 uiDynMoveSeqIndex;
	pDynMove->bVerified = false;
	if (eaSize(&pDynMove->eaDynMoveSeqs)<=0)
	{
		AnimFileError(pDynMove->pcFilename, "A DynMove must have at least one DynMoveSeq section");
		bRet = false;
	}
	for (uiDynMoveSeqIndex=0; uiDynMoveSeqIndex<uiNumDynMoveSeqs; ++uiDynMoveSeqIndex)
	{
		if (!dynMoveSeqVerify(pDynMove->eaDynMoveSeqs[uiDynMoveSeqIndex], pDynMove, bBuildSnapShot))
			bRet = false;
	}
	pDynMove->bVerified = bRet;
	return bRet;
}

static void dynMoveFixupScope(DynMove *pMove)
{
	char file[MAX_PATH], scope[MAX_PATH];
	int pos;

	//grab the file's name minus (-) the "dyn/move/" (9 chars) part
	strcpy(scope, pMove->pcFilename+9);

	//set pos to the 1st character in the files name
	pos = (int)(strlen(scope) - 1);
	while (pos > 0 && scope[pos-1] != '/')
		pos--;

	//break it into components
	strcpy(file, scope+pos);
	scope[pos] = '\0';

	//change the version used for opening moves with the in-game editor
	if (pos > 0)
		strcat(scope, "File: ");
	else
		strcpy(scope, "File: ");
	strcat(scope, file);
	pMove->pcScope = allocAddString(scope);

	//store filename for the move multi-editor
	file[strlen(file)-5] = '\0'; //remove the .Move part
	pMove->pcUserFilename = allocAddString(file);

	//store scope for the move multi-editor
	pos = (int)(strlen(scope) - 1);
	while (pos > 0 && scope[pos] != '/')
		pos--;
	scope[pos] = '\0';
	pMove->pcUserScope = allocAddString(scope);
	if (pMove->pcUserScope)
	{
		if (strlen(pMove->pcUserScope) == 0)
		{
			pMove->pcUserScope = NULL;
		}
	}
}

static void dynMoveRefDictCallback(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	if (eType == RESEVENT_RESOURCE_MODIFIED)
	{
		DynMove *pMove = (DynMove*)pReferent;
		FOR_EACH_IN_EARRAY(pMove->eaDynMoveSeqs, DynMoveSeq, pMoveSeq) {
			pMoveSeq->pDynMove = pMove;
			pMoveSeq->dynMoveAnimTrack.pAnimTrackHeader = dynAnimTrackHeaderFind(pMoveSeq->dynMoveAnimTrack.pcAnimTrackName);
			FOR_EACH_IN_EARRAY(pMoveSeq->dynMoveAnimTrack.eaNoInterpFrameRange, DynMoveFrameRange, pFrameRange)
			{
				pFrameRange->iFirstFrame = pFrameRange->iFirst;
				if (pFrameRange->iFirstFrame > 0)
					pFrameRange->iFirstFrame--;

				pFrameRange->iLastFrame = pFrameRange->iLast;
				if (pFrameRange->iLastFrame > 0)
					pFrameRange->iLastFrame--;
			}
			FOR_EACH_END;
		} FOR_EACH_END;
	}

	if (bDynMoveFinishedStartupLoading && (eType == RESEVENT_RESOURCE_ADDED || eType == RESEVENT_RESOURCE_MODIFIED || eType == RESEVENT_RESOURCE_REMOVED))
	{
		// Make sure to tell costumes to reload, which should tell sequencers to reload
		if (!gConf.bNewAnimationSystem)
			dynSeqDataReloadAll();
		else {
			danimForceDataReload();
			danimForceServerDataReload();
		}
	}
}


static void dynMoveReloadCallback(const char *relpath, int when)
{
#define DYNMOVE_MAX_ACCESS_ATTEMPTS 10000

	int i = 0;

	if (strstr(relpath, "/_")) {
		return;
	}

	loadstart_printf("Reloading DynMoves in %s...", relpath);

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	while (!fileCanGetExclusiveAccess(relpath) && i < DYNMOVE_MAX_ACCESS_ATTEMPTS) {
		Sleep(1);
		i++;
	}

	if (i < DYNMOVE_MAX_ACCESS_ATTEMPTS)
	{
		//fileWaitForExclusiveAccess(relpath);
		errorLogFileIsBeingReloaded(relpath);

		if(!ParserReloadFileToDictionary(relpath,hDynMoveDict)) {
			AnimFileError(relpath, "Error reloading DynMove file: %s", relpath);
			loadend_printf("done. ERROR OCCURED!");
		} else {
			dynAnimTrackHeaderUnloadPreloads();
			loadend_printf("done!");
		}
	}
	else
	{
		AnimFileError(relpath, "Error reloading DynMove file %s post-anim.track reload, make sure EditPlus : File -> Lock is disabled!", relpath);
		loadend_printf("done. ERROR OCCURED (make sure the file isn't locked in EditPlus)!");
	}

#undef DYNMOVE_MAX_ACCESS_ATTEMPTS
}

static bool dynMovePostBinRead(DynMove* pMove)
{
	FOR_EACH_IN_EARRAY(pMove->eaDynMoveSeqs, DynMoveSeq, pMoveSeq)
	{
		DynAnimFrameSnapshot* pSnap = &pMoveSeq->dynMoveAnimTrack.frameSnapshot;
		pMoveSeq->pDynMove = pMove;
		if (!(pMoveSeq->dynMoveAnimTrack.pAnimTrackHeader = dynAnimTrackHeaderFind(pMoveSeq->dynMoveAnimTrack.pcAnimTrackName)))
		{
			AnimFileError(pMove->pcFilename, "Unable to find DynAnimTrack with name %s in DynMoveSeq %s, check in /data/animation_library/", pMoveSeq->dynMoveAnimTrack.pcAnimTrackName, pMoveSeq->pcDynMoveSeq);
			return false;
		}

		if (pSnap->eaBones)
			eaIndexedEnable(&pSnap->eaBones, parse_DynAnimFrameSnapshotBone);
		if (pSnap->eaBonesRotOnly)
			eaIndexedEnable(&pSnap->eaBonesRotOnly, parse_DynAnimFrameSnapshotBoneRotationOnly);
	}
	FOR_EACH_END;

	return true;
}

AUTO_FIXUPFUNC;
TextParserResult fixupDynMove(DynMove* pMove, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			dynMoveFixupScope(pMove);
			if (!dynMoveVerify(pMove,1))
			{
				if (isProductionMode() ||
					gbMakeBinsAndExit) {
					return PARSERESULT_INVALID;
				} else {
					return PARSERESULT_ERROR; // remove this from the costume list
				}
			}
		}
		xcase FIXUPTYPE_POST_BIN_READ:
		{
			if (!dynMoveVerify(pMove,0) ||
				!dynMovePostBinRead(pMove))
			{
				if (isProductionMode() ||
					gbMakeBinsAndExit) {
					return PARSERESULT_INVALID;
				} else {
					return PARSERESULT_ERROR;
				}
			}
		}
	}

	return PARSERESULT_SUCCESS;
}

static int dynMoveResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, DynMove* pMove, U32 userID)
{
	switch (eType)
	{
		xcase RESVALIDATE_FIX_FILENAME:
		{
			char tempName[MAX_PATH];

			strcpy(tempName, "dyn/move/");
			if (pMove->pcUserScope && strlen(pMove->pcUserScope) > 0) {
				strcat(tempName, pMove->pcUserScope);
				strcat(tempName, "/");
			}
			strcat(tempName, pMove->pcUserFilename);
			strcat(tempName, ".Move");
			pMove->pcFilename = allocAddString(tempName);

			if (pMove->pcUserScope && strlen(pMove->pcUserScope) > 0) {
				strcpy(tempName, pMove->pcUserScope);
				strcat(tempName, "/File: ");
			} else {
				strcpy(tempName, "File: ");
			}
			strcat(tempName, pMove->pcUserFilename);
			strcat(tempName, ".Move");
			pMove->pcScope = allocAddString(tempName);

			//resFixPooledFilename((char**)&pMove->pcFilename, "dyn/move", pMove->pcUserScope, pMove->pcUserFilename, "move");
		}
		return VALIDATE_HANDLED;

		xcase RESVALIDATE_POST_TEXT_READING:
		{
			FOR_EACH_IN_EARRAY(pMove->eaDynMoveSeqs, DynMoveSeq, pMoveSeq) {
				FOR_EACH_IN_EARRAY(pMoveSeq->dynMoveAnimTrack.eaBoneRotation, DynMoveBoneRotOffset, pRotOffset)
				{
					Vec3 vRotOffsetInRadians;
					vRotOffsetInRadians[0] = RAD(pRotOffset->rotOffset[0]);
					vRotOffsetInRadians[1] = RAD(pRotOffset->rotOffset[1]);
					vRotOffsetInRadians[2] = RAD(pRotOffset->rotOffset[2]);
					PYRToQuat(vRotOffsetInRadians, pRotOffset->rotOffsetRuntime);
				}
				FOR_EACH_END;
			} FOR_EACH_END;
		}
		return VALIDATE_HANDLED;

		xcase RESVALIDATE_POST_BINNING:
		{
			;
		}
		return VALIDATE_HANDLED;

		xcase RESVALIDATE_FINAL_LOCATION:
		{
			;
		}
		return VALIDATE_HANDLED;

		xcase RESVALIDATE_CHECK_REFERENCES:
		{
			;
		}
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void registerMoveDictionary(void)
{
	hDynMoveDict = RefSystem_RegisterSelfDefiningDictionary(DYNMOVE_DICTNAME, false, parse_DynMove, true, true, NULL);

	resDictManageValidation(hDynMoveDict, dynMoveResValidateCB);

	if (IsServer())
	{
		resDictProvideMissingResources(hDynMoveDict);
		if (isDevelopmentMode() || isProductionEditMode())
		{
			resDictMaintainInfoIndex(hDynMoveDict, ".Name", ".Scope", NULL, NULL, NULL);
		}
	}
	else if (IsClient())
	{
	 	resDictRequestMissingResources(hDynMoveDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
	}

	resDictRegisterEventCallback(hDynMoveDict, dynMoveRefDictCallback, NULL);
}


void dynMoveLoadAll()
{
	S32 iParserFlags = PARSER_OPTIONALFLAG;
	if (IsServer())	{			
		iParserFlags |= RESOURCELOAD_SHAREDMEMORY;
	}

	loadstart_printf("Loading DynMoves...");
	resLoadResourcesFromDisk(hDynMoveDict, "dyn/move", ".move", "DynMove.bin", iParserFlags);
	loadend_printf(" done (%d DynMoves)", RefSystem_GetDictionaryNumberOfReferents(hDynMoveDict));

	dynAnimTrackHeaderUnloadPreloads();

	bDynMoveFinishedStartupLoading = true;
}

bool dynMoveShouldReloadDueToAnimTrackUpdate(DynMove* pMove, const char* pcAnimTrackName)
{
	bool bReprocess = false;
	if ( !pMove->bVerified )
		bReprocess = true;
	else
	{
		U32 uiNumDynMoveSeqs = eaSize(&pMove->eaDynMoveSeqs);
		U32 uiDynMoveSeqIndex;
		for (uiDynMoveSeqIndex=0; uiDynMoveSeqIndex<uiNumDynMoveSeqs && !bReprocess; ++uiDynMoveSeqIndex)
		{
			DynMoveSeq* pDynMoveSeq = pMove->eaDynMoveSeqs[uiDynMoveSeqIndex];
			if ( stricmp(pDynMoveSeq->dynMoveAnimTrack.pcAnimTrackName, pcAnimTrackName) == 0)
				bReprocess = true;
		}
	}

	return bReprocess;
}

void dynMoveManagerReloadedAnimTrack(const char* pcAnimTrackName)
{
	RefDictIterator iterator;
	const char **eaReloadMoveFiles = NULL;
	DynMove* pMove;
	int iCount = 0;
	bool bParserError = false;
	bool bAccessError = false;

	loadstart_printf("Reloading DynMoves...");
	eaCreate(&eaReloadMoveFiles);

	RefSystem_InitRefDictIterator(hDynMoveDict, &iterator);
	while ((pMove = RefSystem_GetNextReferentFromIterator(&iterator)))
	{
		if (dynMoveShouldReloadDueToAnimTrackUpdate(pMove, pcAnimTrackName)) {
			eaPushUnique(&eaReloadMoveFiles, pMove->pcFilename);
		}
	}

	#define DYNMOVE_MAX_ACCESS_ATTEMPTS 10000
	FOR_EACH_IN_EARRAY(eaReloadMoveFiles, const char, pcFilename)
	{
		int i = 0;
		while (!fileCanGetExclusiveAccess(pcFilename) && i < DYNMOVE_MAX_ACCESS_ATTEMPTS) {
			Sleep(1);
			i++;
		}
		if (i < DYNMOVE_MAX_ACCESS_ATTEMPTS) {
			//fileWaitForExclusiveAccess(pcFilename);
			if (!ParserReloadFileToDictionary(pcFilename, hDynMoveDict)) {
				AnimFileError(pcFilename, "Error reloading DynMove file %s post-anim.track reload", pcFilename);
				bParserError = true;
			} else {
				iCount++;
			}
		} else {
			AnimFileError(pcFilename, "Error reloading DynMove file %s post-anim.track reload, make sure EditPlus : File -> Lock is disabled!", pcFilename);
			bAccessError = true;
		}
	}
	FOR_EACH_END;
	#undef DYNMOVE_MAX_ACCESS_ATTEMPTS

	loadend_printf("done. Reloaded %d of %d DynMoves! %s %s", iCount, eaSize(&eaReloadMoveFiles), bParserError?"(Had parser error)":"\b", bAccessError?"(Had file access error)":"");
	eaDestroy(&eaReloadMoveFiles);

	if (iCount)
	{
		dynAnimTrackHeaderUnloadPreloads();
		if (!gConf.bNewAnimationSystem)
			dynSeqDataReloadAll();
		else {
			danimForceDataReload();
			danimForceServerDataReload();
		}
	}
}

const DynMove* dynMoveFromName(const char* pcMoveName)
{
	return RefSystem_ReferentFromString(hDynMoveDict, pcMoveName);
}


static bool dynMoveAnimTrackNoInterp(const DynMoveAnimTrack* pDynMoveAnimTrack, int iBoneTrackTime) 
{
	if ( pDynMoveAnimTrack->bNoInterp )
		return true;
	FOR_EACH_IN_EARRAY(pDynMoveAnimTrack->eaNoInterpFrameRange, DynMoveFrameRange, pFrameRange)
		if ( iBoneTrackTime >= pFrameRange->iFirstFrame && iBoneTrackTime <= pFrameRange->iLastFrame )
			return true;
	FOR_EACH_END
	return false;
}

const DynMoveSeq* dynMoveSeqFromDynMove(const DynMove* pDynMove, const SkelInfo* pSkelInfo)
{
	U32 uiNumDynMoveSeqs;
	U32 uiDynMoveSeqIndex;
	U32 uiBestDynMoveSeqIndex=0;
	U32 uiBestDynMoveSeqRank=BAD_SEQ_TYPE_RANK;
	DynMoveSeq* pDynMoveSeq;

	PERFINFO_AUTO_START_FUNC();
	uiNumDynMoveSeqs = eaSize(&pDynMove->eaDynMoveSeqs);
	for (uiDynMoveSeqIndex=0; uiDynMoveSeqIndex<uiNumDynMoveSeqs; ++uiDynMoveSeqIndex)
	{
		U32 uiRank = 0;
		if ( !pSkelInfo || wlSkelInfoFindSeqTypeRank(pSkelInfo, pDynMove->eaDynMoveSeqs[uiDynMoveSeqIndex]->pcDynMoveSeq, &uiRank) && uiRank < uiBestDynMoveSeqRank )
		{
			uiBestDynMoveSeqIndex = uiDynMoveSeqIndex;
			uiBestDynMoveSeqRank = uiRank;
		}
	}

	if ( uiNumDynMoveSeqs == 0 )
	{
		AnimFileError(pDynMove->pcFilename, "There should be no way for DynMove %s to have zero DynMoveSeq sections!", pDynMove->pcName);
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	pDynMoveSeq = pDynMove->eaDynMoveSeqs[uiBestDynMoveSeqIndex];
	if ( uiBestDynMoveSeqRank == BAD_SEQ_TYPE_RANK )
	{
		AnimFileError(pDynMove->pcFilename, "Failed to find appropriate seqtype for skeleton info %s, in move %s, using first seqtype %s", pSkelInfo->pcSkelInfoName, pDynMove->pcName, pDynMoveSeq->pcDynMoveSeq);
	}

	PERFINFO_AUTO_STOP();
	return pDynMove->eaDynMoveSeqs[uiBestDynMoveSeqIndex];
}

U32 dynMoveSeqGetStartOffset(const DynMoveSeq *pDynMoveSeq)
{
	if (pDynMoveSeq->dynMoveAnimTrack.uiStartOffsetLastFrame == 0)
	{
		return 0;
	}
	else if (
		pDynMoveSeq->dynMoveAnimTrack.uiStartOffsetFirstFrame ==
		pDynMoveSeq->dynMoveAnimTrack.uiStartOffsetLastFrame)
	{
		return	pDynMoveSeq->dynMoveAnimTrack.uiStartOffsetFirstFrame - 
				pDynMoveSeq->dynMoveAnimTrack.uiFirstFrame;
	}
	else
	{
		return	pDynMoveSeq->dynMoveAnimTrack.uiStartOffsetFirstFrame +
				rand() % (	pDynMoveSeq->dynMoveAnimTrack.uiStartOffsetLastFrame  -
							pDynMoveSeq->dynMoveAnimTrack.uiStartOffsetFirstFrame +
							1)
				- pDynMoveSeq->dynMoveAnimTrack.uiFirstFrame;
	}
}

F32 dynMoveSeqGetRandPlaybackSpeed(const DynMoveSeq *pDynMoveSeq)
{
	if (!pDynMoveSeq->bRandSpeed)
	{
		return pDynMoveSeq->fSpeed;
	}
	else
	{
		F32 fRand01 = randomPositiveF32();
		return	(1.0f-fRand01)*pDynMoveSeq->fSpeed +
				fRand01*pDynMoveSeq->fSpeedHigh;
	}
}

bool dynMoveSeqCalcTransform(const DynMoveSeq* pDynMoveSeq, F32 fFrameTime, DynTransform* pTransform, const char* pcBoneTag, const DynTransform* pxBaseTransform, DynRagdollState* pRagdollState, DynNode* pRoot, bool bApplyOffsets)
{
	bool bUpdatedBone = false;
	PERFINFO_AUTO_START("dynMoveSeqCalcTransform", 1);
	{
		const DynMoveAnimTrack* pDynMoveAnimTrack = &pDynMoveSeq->dynMoveAnimTrack;
		S32 iBoneTrackTime = (S32)ceil(fFrameTime) + pDynMoveAnimTrack->uiFirstFrame;
		if ( iBoneTrackTime > (S32)(pDynMoveAnimTrack->uiLastFrame + uiFixDynMoveOffByOneError) )
			Errorf("Invalid frame time for DynMove %s, DynMoveSeq %s,  %d exceeds last frame time stamp of %d", pDynMoveSeq->pDynMove->pcName, pDynMoveSeq->pcDynMoveSeq, iBoneTrackTime,pDynMoveAnimTrack->uiLastFrame );
		// Figure out if the current bone track time is within the range

		PERFINFO_AUTO_START("dynBoneTrackUpdate", 1);
		if ( pDynMoveAnimTrack->pAnimTrackHeader )
		{
			// Check if the animaiton is ready. If not, request it for background loading
			if (dynAnimTrackHeaderRequest(pDynMoveAnimTrack->pAnimTrackHeader))
			{
				// Just update like normal
				bUpdatedBone = dynBoneTrackUpdate(pDynMoveAnimTrack->pAnimTrackHeader->pAnimTrack, fFrameTime + (F32)pDynMoveAnimTrack->uiFirstFrame, pTransform, pcBoneTag, pxBaseTransform, dynMoveAnimTrackNoInterp(pDynMoveAnimTrack, iBoneTrackTime));
			}
			else
			{
				// Use the DynMove first frame while waiting for the load
				dynAnimFrameSnapshotGetTransform(&pDynMoveAnimTrack->frameSnapshot, pcBoneTag, pxBaseTransform, pTransform);
			}
			 
			if (SAFE_MEMBER(pRagdollState,bRagdollOn))
			{
				U32 uiPart;
				bool bFoundPart = false;

				//apply ragdoll physics data when possible
				for (uiPart=0; uiPart<pRagdollState->uiNumParts; ++uiPart)
				{
					DynRagdollPartState* pPart = &pRagdollState->aParts[uiPart];
					if (pPart->pcBoneName == pcBoneTag)
					{
						if (!pPart->pcParentBoneName)
						{
							//Rotation
							DynTransform xRoot;
							Quat qInv;
							dynNodeGetWorldSpaceTransform(pRoot, &xRoot);
							quatInverse(xRoot.qRot, qInv);

							quatMultiply(pPart->qWorldSpace, qInv, pTransform->qRot);

							// Translation
							{
								Vec3 vPos;
								subVec3(pRagdollState->vHipsWorldSpace, xRoot.vPos, vPos);
								vPos[0] /= xRoot.vScale[0];
								vPos[1] /= xRoot.vScale[1];
								vPos[2] /= xRoot.vScale[2];
								quatRotateVec3Inline(qInv, vPos, pTransform->vPos);
							}
						}
						else
						{
							quatInterp(pRagdollState->fBlend, pTransform->qRot, pPart->qLocalSpace, pTransform->qRot);
						}
						bFoundPart = true;
						break;
					}
				}

				if (!bFoundPart)
				{
					quatInterp(pRagdollState->fBlend, pTransform->qRot, pxBaseTransform->qRot, pTransform->qRot);
					interpVec3(pRagdollState->fBlend, pTransform->vPos, pxBaseTransform->vPos, pTransform->vPos);
					interpVec3(pRagdollState->fBlend, pTransform->vScale, pxBaseTransform->vScale, pTransform->vScale);
				}
			}

			if (bApplyOffsets)
			{
				FOR_EACH_IN_EARRAY(pDynMoveAnimTrack->eaBoneOffset, DynMoveBoneOffset, pBoneOffset)
				{
					if (pBoneOffset->pcBoneName == pcBoneTag)
					{
						addVec3(pTransform->vPos, pBoneOffset->vOffset, pTransform->vPos);
					}
				}
				FOR_EACH_END;

				FOR_EACH_IN_EARRAY(pDynMoveAnimTrack->eaBoneRotation, DynMoveBoneRotOffset, pBoneRotation)
				{
					if (pBoneRotation->pcBoneName == pcBoneTag)
					{
						Quat qTemp;
						quatMultiply(pTransform->qRot, pBoneRotation->rotOffsetRuntime, qTemp);
						copyQuat(qTemp, pTransform->qRot);
					}
				}
				FOR_EACH_END;
			}
		}
		PERFINFO_AUTO_STOP();		
	}
	PERFINFO_AUTO_STOP();
	return bUpdatedBone;
}

static void dynAnimFrameSnapshotCreate(DynAnimTrack* pAnimTrack, U32 uiFrame, DynAnimFrameSnapshot* pSnap)
{
	U32 i;
	// First clear the snapshot if it already exists
	if (eaSize(&pSnap->eaBones) > 0 ||
		eaSize(&pSnap->eaBonesRotOnly) > 0)
	{
		while (eaSize(&pSnap->eaBones)) {
			DynAnimFrameSnapshotBone *pBone = eaPop(&pSnap->eaBones);
			StructDestroy(parse_DynAnimFrameSnapshotBone, pBone);
		}

		while (eaSize(&pSnap->eaBonesRotOnly)) {
			DynAnimFrameSnapshotBoneRotationOnly *pBone = eaPop(&pSnap->eaBonesRotOnly);
			StructDestroy(parse_DynAnimFrameSnapshotBoneRotationOnly, pBone);
		}

		//slight memory leak, happens when animators are updating a danim file, I
		//am doing this as a temp work around to prevent crashes from mem tracker
		//that are related to the guardband data being zeroed out.. which I think
		//is from moving an indexed earrary into shared memory.  Should never get
		//here during actual game play.
		pSnap->eaBones = NULL;
		pSnap->eaBonesRotOnly = NULL;
		eaCreate(&pSnap->eaBones);
		eaCreate(&pSnap->eaBonesRotOnly);
	}
	for (i=0; i<pAnimTrack->uiBoneCount; ++i)
	{
		DynTransform xBone;
		const char* pcBoneName = (pAnimTrack->eType == eDynAnimTrackType_Compressed)?pAnimTrack->bonesCompressed[i].pcBoneName:pAnimTrack->bones[i].pcBoneName;
		dynBoneTrackUpdate(pAnimTrack, (F32)uiFrame, &xBone, pcBoneName, NULL, true);

		{
			Vec3 vScaleDiff;
			subVec3same(xBone.vScale, 1.0f, vScaleDiff);
			if (strcmp(pcBoneName, POWER_MOVEMENT_BONE_NAME) != 0 && // We always want the movement bone to be in the full bone list
				lengthVec3Squared(xBone.vPos) < 0.001f && lengthVec3Squared(vScaleDiff) < 0.001f)
			{
				// Can ignore position and scale. Check rotation
				if (quatIsIdentity(xBone.qRot, 0.001f))
				{
					// Totally ignore this bone, it's basically identity
					continue;
				}
				else
				{
					// Compress quat and keep it around
					DynAnimFrameSnapshotBoneRotationOnly* pBoneRotOnly = StructAlloc(parse_DynAnimFrameSnapshotBoneRotationOnly);
					quatForceWPositive(xBone.qRot);
					copyVec3(xBone.qRot, pBoneRotOnly->qCompressedRot);
					pBoneRotOnly->pcName = pcBoneName;
					eaPush(&pSnap->eaBonesRotOnly, pBoneRotOnly);
				}
			}
			else // full bone
			{
				DynAnimFrameSnapshotBone* pBone = StructAlloc(parse_DynAnimFrameSnapshotBone);
				quatForceWPositive(xBone.qRot);
				copyVec3(xBone.qRot, pBone->qCompressedRot);
				copyVec3(xBone.vPos, pBone->vPos);
				copyVec3(xBone.vScale, pBone->vScale);
				pBone->pcName = pcBoneName;
				eaPush(&pSnap->eaBones, pBone);
			}
		}
	}

	// Sort bone tables
	if (pSnap->eaBones)
		eaIndexedEnable(&pSnap->eaBones, parse_DynAnimFrameSnapshotBone);
	if (pSnap->eaBonesRotOnly)
		eaIndexedEnable(&pSnap->eaBonesRotOnly, parse_DynAnimFrameSnapshotBoneRotationOnly);
}

static void dynAnimFrameSnapshotGetTransform(const DynAnimFrameSnapshot* pSnap, const char* pcBoneName, const DynTransform* pxBaseTransform, DynTransform* pxResult)
{
	// First try rotonly bones
	int iIndex = eaIndexedFindUsingString(&pSnap->eaBonesRotOnly, pcBoneName);
	if (!pxBaseTransform)
		pxBaseTransform = &xIdentity;
	if (iIndex >= 0)
	{
		const DynAnimFrameSnapshotBoneRotationOnly* pBone = pSnap->eaBonesRotOnly[iIndex];
		DynTransform xBone;
		copyVec3(pBone->qCompressedRot, xBone.qRot);
		quatCalcWFromXYZ(xBone.qRot);
		zeroVec3(xBone.vPos);
		setVec3same(xBone.vScale, 1.0f);
		dynTransformMultiply(&xBone, pxBaseTransform, pxResult);
		return;
	}

	// Second try full bones
	iIndex = eaIndexedFindUsingString(&pSnap->eaBones, pcBoneName);
	if (iIndex >= 0)
	{
		const DynAnimFrameSnapshotBone* pBone = pSnap->eaBones[iIndex];
		DynTransform xBone;
		copyVec3(pBone->qCompressedRot, xBone.qRot);
		quatCalcWFromXYZ(xBone.qRot);
		copyVec3(pBone->vPos, xBone.vPos);
		copyVec3(pBone->vScale, xBone.vScale);
		dynTransformMultiply(&xBone, pxBaseTransform, pxResult);
		return;
	}

	// Can't find it, use identity
	dynTransformCopy(pxBaseTransform, pxResult);
}

static int iForceAllInterp = 0;

AUTO_CMD_INT(iForceAllInterp, danimForceEasing) ACMD_CATEGORY(dynAnimation);

F32 dynAnimInterpolationCalcInterp(F32 fInputValue, const DynAnimInterpolation* pInterp)
{
	if (iForceAllInterp)
	{
		if (fInputValue < 0.5f)
		{
			switch (iForceAllInterp)
			{
				xcase eEaseType_Low:		return 0.5f*SQR(2.0f*fInputValue);
				xcase eEaseType_Medium:		return 0.5f*CUBE(2.0f*fInputValue);
				xcase eEaseType_High:		return 0.5f*QUINT(2.0f*fInputValue);
				xcase eEaseType_LowInv:		return 0.5f - 0.5f*SQR(1.0f-2.0f*fInputValue);
				xcase eEaseType_MediumInv:	return 0.5f - 0.5f*CUBE(1.0f-2.0f*fInputValue);
				xcase eEaseType_HighInv:	return 0.5f - 0.5f*QUINT(1.0f-2.0f*fInputValue);
			}
		}
		else
		{
			switch (iForceAllInterp)
			{
				xcase eEaseType_Low:		return 1.0f - 0.5f*SQR(2.0f*(1.0f-fInputValue));
				xcase eEaseType_Medium:		return 1.0f - 0.5f*CUBE(2.0f*(1.0f-fInputValue));
				xcase eEaseType_High:		return 1.0f - 0.5f*QUINT(2.0f*(1.0f-fInputValue));
				xcase eEaseType_LowInv:		return 0.5f + 0.5f*SQR(1.0f-2.0f*fInputValue); // the + is important
				xcase eEaseType_MediumInv:	return 0.5f - 0.5f*CUBE(1.0f-2.0f*fInputValue);
				xcase eEaseType_HighInv:	return 0.5f - 0.5f*QUINT(1.0f-2.0f*fInputValue);
			}
		}
		Errorf("danimForceEasing must be 0, 1, 2, 3, 4, 5, or 6.. not %d", iForceAllInterp);
		iForceAllInterp = 0;
		return fInputValue;
	}

	// Linear
	if (pInterp->easeIn == eEaseType_None && pInterp->easeOut == eEaseType_None)
		return fInputValue;

	// Ease in
	if (pInterp->easeIn != eEaseType_None && pInterp->easeOut == eEaseType_None)
	{
		switch (pInterp->easeIn)
		{
			xcase eEaseType_Low:	return 1.0f - SQR(1.0f-fInputValue);
			xcase eEaseType_Medium: return 1.0f - CUBE(1.0f-fInputValue);
			xcase eEaseType_High:	return 1.0f - QUINT(1.0f-fInputValue);
			xcase eEaseType_LowInv:		return SQR(fInputValue);
			xcase eEaseType_MediumInv:	return CUBE(fInputValue);
			xcase eEaseType_HighInv:	return QUINT(fInputValue);
		}
		assert(0);
	}

	// Ease out
	if (pInterp->easeOut != eEaseType_None && pInterp->easeIn == eEaseType_None)
	{
		switch (pInterp->easeOut)
		{
			xcase eEaseType_Low:	return SQR(fInputValue);
			xcase eEaseType_Medium:	return CUBE(fInputValue);
			xcase eEaseType_High:	return QUINT(fInputValue);
			xcase eEaseType_LowInv:		return 1.0f - SQR(1.0f-fInputValue);
			xcase eEaseType_MediumInv:	return 1.0f - CUBE(1.0f-fInputValue);
			xcase eEaseType_HighInv:	return 1.0f - QUINT(1.0f-fInputValue);
		}
		assert(0);
	}


	// Ease in / Ease out
	if (fInputValue < 0.5f)
	{
		switch (pInterp->easeOut)
		{
			xcase eEaseType_Low:	return 0.5f*SQR(2.0f*fInputValue);
			xcase eEaseType_Medium:	return 0.5f*CUBE(2.0f*fInputValue);
			xcase eEaseType_High:	return 0.5f*QUINT(2.0f*fInputValue);
			xcase eEaseType_LowInv:		return 0.5f - 0.5f*SQR(1-2.0f*fInputValue);
			xcase eEaseType_MediumInv:	return 0.5f - 0.5f*CUBE(1-2.0f*fInputValue);
			xcase eEaseType_HighInv:	return 0.5f - 0.5f*QUINT(1-2.0f*fInputValue);
		}
	}
	else
	{
		switch (pInterp->easeIn)
		{
			xcase eEaseType_Low:	return 1.0f - 0.5f*SQR(2.0f*(1.0f-fInputValue));
			xcase eEaseType_Medium:	return 1.0f - 0.5f*CUBE(2.0f*(1.0f-fInputValue));
			xcase eEaseType_High:	return 1.0f - 0.5f*QUINT(2.0f*(1.0f-fInputValue));
			xcase eEaseType_LowInv:		return 0.5f + 0.5f*SQR(1.0f-2.0f*fInputValue); // the + is important
			xcase eEaseType_MediumInv:	return 0.5f - 0.5f*CUBE(1.0f-2.0f*fInputValue);
			xcase eEaseType_HighInv:	return 0.5f - 0.5f*QUINT(1.0f-2.0f*fInputValue);
		}
	}

	assert(0);
	return fInputValue;
}

F32 dynMoveSeqAdvanceTime(	const DynMoveSeq* pMoveSeq,
							F32 fDeltaTime,
							DynSkeleton* pSkeleton,
							F32* pfFrameTimeInOut,
							F32 fPlaybackSpeed,
							S32 useDistance)
{
	F32 fFinalTimeDiff = 0.0f;
	assert(pfFrameTimeInOut);
	if (fDeltaTime > 0.0f)
	{
		F32 fSpeedMultiplier = pMoveSeq?fPlaybackSpeed:1.0f;//pMoveSeq->fSpeed:1.0f;
		DynSkeleton* pDistanceSkeleton = pSkeleton->pGenesisSkeleton;
		if (useDistance &&
			SAFE_MEMBER(pMoveSeq, fDistance) &&
			!pDistanceSkeleton->bUnmanaged)
		{
			F32 fTimeToUse;
			F32 fDivide = pDistanceSkeleton->fDistanceTraveledXZ / pMoveSeq->fDistance;
			if (fDivide >= 1.0f )
			{
				fDivide -= floor(fDivide);
			}
			fTimeToUse = fDivide * pMoveSeq->fLength * (1.0f / pDistanceSkeleton->fHeightScale);
			if (pMoveSeq->fMinRate > 0.0f || pMoveSeq->fMaxRate > 0.0f)
			{
				F32 fRate = fTimeToUse / fDeltaTime;
				if (pMoveSeq->fMinRate > 0.0f && fRate < pMoveSeq->fMinRate)
					fRate = pMoveSeq->fMinRate;
				if (pMoveSeq->fMaxRate > 0.0f && fRate > pMoveSeq->fMaxRate)
					fRate = pMoveSeq->fMaxRate;
				fTimeToUse = fRate * fDeltaTime;
			}

			(*pfFrameTimeInOut) += fTimeToUse * fSpeedMultiplier;
			fFinalTimeDiff = fTimeToUse * fSpeedMultiplier;
			pSkeleton->fMovementSyncPercentage = (*pfFrameTimeInOut) / pMoveSeq->fLength;
			pSkeleton->fMovementSyncPercentage -= floorf(pSkeleton->fMovementSyncPercentage);
			pSkeleton->fMovementSyncPercentage = CLAMP(pSkeleton->fMovementSyncPercentage, 0.0f, 1.0f);
			//assert(pSkeleton->fMovementSyncPercentage >= 0.0f && pSkeleton->fMovementSyncPercentage < 1.0f);
			pSkeleton->bMovementSyncSet = true;
		}
		else
		{
			(*pfFrameTimeInOut) += fDeltaTime * fSpeedMultiplier;
			fFinalTimeDiff = fDeltaTime * fSpeedMultiplier;
		}
	}

	return fFinalTimeDiff;
}

static int dynMoveFxEventGetSearchStringCount(const DynMoveFxEvent *pDynMoveFxEvent, const char *pcSearchText)
{
	int count = 0;

	if (pDynMoveFxEvent->pcFx	&& strstri(pDynMoveFxEvent->pcFx,	pcSearchText)) count++;

	//frames ignored

	if (pDynMoveFxEvent->bMessage	&& strstri("Message",	pcSearchText))	count++;

	return count;
}

static int dynMoveTagGetSearchStringCount(const DynMoveTag *pDynMoveTag, const char *pcSearchText)
{
	int count = 0;

	if (pDynMoveTag->pcTag	&& strstri(pDynMoveTag->pcTag,	pcSearchText))	count++;

	return count;
}

static int dynMoveAnimTrackGetSearchStringCount(const DynMoveAnimTrack *pAnimTrack, const char *pcSearchText)
{
	int count = 0;

	if (pAnimTrack->pcAnimTrackName	&& strstri(pAnimTrack->pcAnimTrackName,	pcSearchText)) count++;

	//other fields ignored

	return count;
}

static int dynMoveSeqGetSearchStringCount(const DynMoveSeq	*pDynMoveSeq, const char *pcSearchText)
{
	int count = 0;

	if (pDynMoveSeq->pcDynMoveSeq	&& strstri(pDynMoveSeq->pcDynMoveSeq,	pcSearchText))	count++;
	
	//parent move ignored
	
	count += dynMoveAnimTrackGetSearchStringCount(&(pDynMoveSeq->dynMoveAnimTrack), pcSearchText);

	FOR_EACH_IN_EARRAY(pDynMoveSeq->eaDynMoveFxEvents, DynMoveFxEvent, pFx) {
		count += dynMoveFxEventGetSearchStringCount(pFx, pcSearchText);
	} FOR_EACH_END;

	//speed ignored

	if (pDynMoveSeq->pcIKTarget				&& strstri(pDynMoveSeq->pcIKTarget,				pcSearchText))	count++;
	if (pDynMoveSeq->pcIKTargetNodeLeft		&& strstri(pDynMoveSeq->pcIKTargetNodeLeft,		pcSearchText))	count++;
	if (pDynMoveSeq->pcIKTargetNodeRight	&& strstri(pDynMoveSeq->pcIKTargetNodeRight,	pcSearchText))	count++;

	//distance ignored
	//min rate ignored
	//max rate ignored
	//length ignored

	//verified ignored

	if (pDynMoveSeq->bRagdoll				&& strstri("Ragdoll",				pcSearchText))	count++;
	if (pDynMoveSeq->bRegisterWep			&& strstri("RegisterWep",			pcSearchText))	count++;
	if (pDynMoveSeq->bIKBothHands			&& strstri("IKBothHands",			pcSearchText))	count++;
	if (pDynMoveSeq->bIKMeleeMode			&& strstri("IKMeleeMode",			pcSearchText))	count++;
	if (pDynMoveSeq->bEnableIKSliding		&& strstri("EnableIKSliding",		pcSearchText))	count++;
	if (pDynMoveSeq->bDisableIKLeftWrist	&& strstri("DisableIKLeftWrist",	pcSearchText))	count++;
	if (pDynMoveSeq->bDisableIKRightArm		&& strstri("DisableIKRightArm",		pcSearchText))	count++;

	return count;
}

int dynMoveGetSearchStringCount(const DynMove *pDynMove, const char *pcSearchText)
{
	int count = 0;

	if (pDynMove->pcName		&& strstri(pDynMove->pcName,		pcSearchText))	count++;
	if (pDynMove->pcFilename	&& strstri(pDynMove->pcFilename,	pcSearchText))	count++;
	if (pDynMove->pcComments	&& strstri(pDynMove->pcComments,	pcSearchText))	count++;
	if (pDynMove->pcScope		&& strstri(pDynMove->pcScope,		pcSearchText))	count++;
	//user filename ignored
	//user scope ignored

	FOR_EACH_IN_EARRAY(pDynMove->eaDynMoveTags, DynMoveTag, pMoveTag) {
		count += dynMoveTagGetSearchStringCount(pMoveTag, pcSearchText);
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pDynMove->eaDynMoveSeqs, DynMoveSeq, pMoveSeq) {
		count += dynMoveSeqGetSearchStringCount(pMoveSeq, pcSearchText);
	} FOR_EACH_END;

	//verified ignored

	return count;
}