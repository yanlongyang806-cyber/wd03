#include "Queue.h"
#include "rand.h"
#include "MemoryPool.h"
#include "stringCache.h"
#include "wlState.h"
#include "Color.h"
#include "Curve.h"
#include "windefinclude.h"
#include "Prefs.h"

#include "wlModelLoad.h"
#include "wlTime.h"
#include "WorldCellEntry.h"
#include "WorldCellEntryPrivate.h"
#include "WorldGridPrivate.h"
#include "dynDraw.h"

#include "dynNodeInline.h"
#include "dynAnimNodeAlias.h"
#include "dynFx.h"
#include "dynFxInfo.h"
#include "dynFxParticle.h"
#include "dynFxInterface.h"
#include "dynFxFastParticle.h"
#include "dynFxDebug.h"
#include "dynFxPhysics.h"
#include "dynFxEnums_h_ast.c"
#include "dynFxInfo_h_ast.c"

#include "dynCloth.h"

#include "wlCostume.h"

#include "rgb_hsv.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

extern const char* pcKillMessage;
extern const char* pcStartMessage;
extern const char* pcMovedTooFarMessage;

#define DYN_DEFAULT_MAX_FX_COUNT 4096
MP_DEFINE(DynFx);
MP_DEFINE(DynFxRef);

//StashTable stDynFxGuidTable;

typedef struct DynObject DynObject;
typedef struct GfxLight GfxLight;
typedef struct DynEventUpdater DynEventUpdater;

static void dynFxMeshTrailFree(DynParticle* pParticle );
static bool dynFxProcessMessage(DynFx* pFx, U32 uiIndex);
static void dynFxScaleToNode( const DynNode* pScaleToNode, DynNode* pFxNode, DynFx* pFx );
static void dynFxOrientToNode( const DynNode* pOrientToNode, DynNode* pFxNode, bool bLockToPlane ) ;

AUTO_RUN;
void dynFxInitPools(void)
{
	MP_CREATE_ALIGNED(DynFx, DYN_DEFAULT_MAX_FX_COUNT,128);
	MP_CREATE(DynFxRef, 128);
}


static StashTable stMissing = NULL;
bool dynFxMissingFileError(const char* pcFileName, const char* pcMissingFXName)
{
	if (!stMissing)
		stMissing = stashTableCreateWithStringKeys(16, StashDefault);

	if (stashAddInt(stMissing, allocAddString(pcMissingFXName), 1, false))
	{
		// Report the error, this is the first time
		ErrorFilenamef(pcFileName, "Unable to find valid fx %s", pcMissingFXName );
		return true;
	}
	return false;
}

void dynFxClearMissingFiles(void)
{
	stashTableDestroy(stMissing);
	stMissing = NULL;
}


/*
DynFxGuid addDynFxToGuidTable(DynFx* pNewFx)
{
	static DynFxGuid uiCurrentGuid = 1; // we use an incrementing guid generator
	// Generate guid
	DynFxGuid uiOurGuid = uiCurrentGuid++;
	bool bUnique;
	if (!uiOurGuid) // in unlikely event we wrap
		uiOurGuid = uiCurrentGuid = 1;

	if ( !stDynFxGuidTable )
		stDynFxGuidTable = stashTableCreateInt(512);

	bUnique = stashIntAddPointer(stDynFxGuidTable, uiOurGuid, pNewFx, false);
	if (!bUnique)
	{
		Errorf("DynFx guid %d somehow already in use!\n", uiOurGuid);
	}
	return uiOurGuid;
}

DynFx* dynFxFromGuid(DynFxGuid guid)
{
	DynFx* pResult;
	if (stashIntFindPointer(stDynFxGuidTable, guid, &pResult))
		return pResult;
	return NULL;
}
*/

static int dynFxGetPrefSet(void)
{
	static int s_iFXPrefSet = -1;
	if (s_iFXPrefSet < 0)
	{
		char pchBuffer[MAX_PATH];
		sprintf(pchBuffer, "%s/FXPrefs.pref", fileLocalDataDir());
		s_iFXPrefSet = PrefSetGet(pchBuffer);
	}
	return s_iFXPrefSet;
}

bool dynFxExclusionTagMatches(const char* pcExclusionTag)
{
	return PrefGetInt(dynFxGetPrefSet(), pcExclusionTag, false); 
}

void dynFxSetExclusionTag(const char* pcExclusionTag, bool bExclude)
{
	PrefStoreInt(dynFxGetPrefSet(), pcExclusionTag, bExclude);
	dynFxTestExclusionTagForAll(pcExclusionTag);
}

static U32 uiFXIDCount = 0;

DynFx* dynFxCreate(DynFxManager* pManager, const char* pcFxInfo, DynFx* pParent, DynParamBlock* pParamBlock, const DynNode* pTargetRoot, const DynNode* pSourceRoot, F32 fHue, F32 fSaturationShift, F32 fValueShift, DynFxSortBucket* pSortBucket, U32 uiHitReactID, eDynPriority ePriorityOverride, dynJitterListSelectionCallback cbJitterListSelectFunc, void* pJitterListSelectData)
{
	DynFx* pNewFx;
	DynFxInfo* pFxInfo;

	if(pParamBlock) {
		pParamBlock->bClaimedByFX = true;
	}

	if (!dynFxInfoExists(pcFxInfo))
	{
		Errorf("Tried to create fx without specifying valid fx info %s!", pcFxInfo);
		return NULL;
	}

	pNewFx = MP_ALLOC(DynFx);
	pNewFx->uiFXID = uiFXIDCount++;
	pNewFx->iDrawArrayIndex = -1;

	SET_HANDLE_FROM_STRING(hDynFxInfoDict, pcFxInfo, pNewFx->hInfo); 
	pFxInfo = GET_REF(pNewFx->hInfo);
	if (
		!pFxInfo
		|| pFxInfo->bVerifyFailed
		|| (pFxInfo->bUnique && !dynFxManUniqueFXCheck(pManager, pFxInfo))
		|| (pFxInfo->bLocalPlayerOnly && !dynFxManIsLocalPlayer(pManager))
		|| dynFxPriorityBelowDetailSetting((pNewFx->iPriorityLevel = ePriorityOverride != edpNotSet ? ePriorityOverride : pFxInfo->iPriorityLevel))
		|| dynFxDropPriorityAboveDetailSetting(pFxInfo->iDropPriorityLevel)
		|| (pFxInfo->pcExclusionTag && dynFxExclusionTagMatches(pFxInfo->pcExclusionTag) )
		)
	{
		if (!pFxInfo)
			Errorf("Can't find %s!", pcFxInfo);
		else if (pFxInfo->bVerifyFailed)
			Errorf("Fx %s failed verify, ignoring!", pcFxInfo);
		// if neither of those, it's not an error, just a unique or excluded FX
		REMOVE_HANDLE(pNewFx->hInfo);
		MP_FREE(DynFx, pNewFx);
		return NULL;
	}
	pNewFx->bDebug = pFxInfo->bDebugFx;
	pNewFx->b2D = pFxInfo->bForce2D;
	pNewFx->uiHitReactID = uiHitReactID;
	pNewFx->fPlaybackSpeed = 1.0f;

	if (pFxInfo->bInheritPlaybackSpeed && pParent)
		pNewFx->fPlaybackSpeed = pParent->fPlaybackSpeed;

	if (pFxInfo->fPlaybackJitter != 0.0f)
		pNewFx->fPlaybackSpeed += pFxInfo->fPlaybackJitter * randomF32Seeded(NULL, RandType_BLORN);

	if (dynDebugState.bRecordFXProfile)
	{
		dynFxRecordInfo(pFxInfo->pcDynName);
		fxRecording.uiNumDynFxCreated++;
	}



	/*
	if (pFxInfo->bForce2D)
		pNewFx->pManager = dynFxGetUiManager(false);
	else
	*/
		pNewFx->pManager = pManager;
	dynFxLog(pNewFx, "Creating FX %s", pcFxInfo);
	pNewFx->fFadeOut = 1.0f;
	pNewFx->fFadeOutTime = -1.0f;

	pNewFx->fFadeIn = 0.0f;
	pNewFx->fFadeInTime = -1.0f;

	if (pFxInfo->bDontHueShiftChildren)
	{
		pNewFx->fHue = pFxInfo->fDefaultHue;
	}
	else if (fHue == 0.0f)
		pNewFx->fHue = pFxInfo->fDefaultHue;
	else
		pNewFx->fHue = fHue;

	if(pFxInfo->fEntityFadeSpeedOverride) {
		pNewFx->fEntityFadeSpeedOverride = pFxInfo->fEntityFadeSpeedOverride;
	}

	pNewFx->fValueShift = fValueShift;
	pNewFx->fSaturationShift = fSaturationShift;

	if ( pParent )
		ADD_SIMPLE_POINTER_REFERENCE_DYN(pNewFx->hParentFx, pParent);

	if ( pParamBlock )
	{
		ADD_SIMPLE_POINTER_REFERENCE_DYN(pNewFx->hParamBlock, pParamBlock);
	}

	if ( pTargetRoot )
		ADD_SIMPLE_POINTER_REFERENCE_DYN(pNewFx->hTargetRoot, pTargetRoot);

	if ( pSourceRoot )
		ADD_SIMPLE_POINTER_REFERENCE_DYN(pNewFx->hSourceRoot, pSourceRoot);

	if (pSortBucket)
	{
		pNewFx->pSortBucket = dynSortBucketIncRefCount(pSortBucket);
	}

	pNewFx->cbJitterListSelectFunc = cbJitterListSelectFunc;
	pNewFx->pJitterListSelectData = pJitterListSelectData;

	if (pFxInfo->bEntNeedsAuxPass)
		dynFxApplyToCostume(pNewFx);

	// Debug tracking
	if (!dynFxManDoesntSelfUpdate(pManager))
		dynFxDebugPushTracker(pFxInfo->pcDynName);

	return pNewFx;
}

void dynFxPushChildFx(DynFx* pParentFx, DynFx* pSibling, DynFx* pChildFx )
{
	if (pSibling)
		ADD_SIMPLE_POINTER_REFERENCE_DYN(pChildFx->hSiblingFx, pSibling);
	eaPush(&pParentFx->eaChildFx, pChildFx);
}

bool dynFxKill(DynFx* pFx, bool bImmediate, bool bRemoveFromOwner, bool bOrphanChildren, eDynFxKillReason eReason)
{
	// Check if we need to kill children
 	U32 uiNumChildren = eaSize(&pFx->eaChildFx);
	U32 uiChild;
#if DYNFX_LOGGING
	if (pFx->bDebug)
	{
		switch (eReason)
		{
			xcase eDynFxKillReason_ExternalKill:
				dynFxLog(pFx, "Killed externally (game logic)");
			xcase eDynFxKillReason_ParentDied:
				dynFxLog(pFx, "Killed because Parent Died and KillIfOrphaned is set");
			xcase eDynFxKillReason_ManualKill:
				dynFxLog(pFx, "Killed Manually (debug commands)");
			xcase eDynFxKillReason_ManagerKilled:
				dynFxLog(pFx, "Killed because Manager Killed and KillIfOrphaned is set");
			xcase eDynFxKillReason_MaintainedFx:
				dynFxLog(pFx, "Killed since maintained FX (costume or world) shut off");
			xcase eDynFxKillReason_DebrisCountExceeded:
				dynFxLog(pFx, "Killed: Debris Count Exceeded");
			xcase eDynFxKillReason_DebrisNoNode:
				dynFxLog(pFx, "Killed: Debris and no Node found");
			xcase eDynFxKillReason_RequiresNode:
				dynFxLog(pFx, "Killed: RequiresNode but no Node found");
			xcase eDynFxKillReason_DebrisDistanceExceeded:
				dynFxLog(pFx, "Killed: Debris Distance Exceeded");
			xdefault:
				dynFxLog(pFx, "Killed: NO REASON SPECIFIED!!!!!!");
		}
		if (bImmediate)
			dynFxLog(pFx, "HARD KILL");
	}
#endif
	for (uiChild=0; uiChild<uiNumChildren; ++uiChild)
	{
		DynFx* pChildFx = pFx->eaChildFx[uiChild];
		DynFxInfo* pInfo = GET_REF(pChildFx->hInfo);
		if ( bImmediate || pInfo->bKillIfOrphaned )
		{
			if (!bOrphanChildren)
			{
				// If they are removed immediately, this changed our earray
				if (dynFxKill(pChildFx, bImmediate, true, bOrphanChildren, eDynFxKillReason_ParentDied))
				{
					--uiChild;
					--uiNumChildren;
					assert(uiNumChildren == eaSize(&pFx->eaChildFx));
				}
			}
			else
			{
				// We might want to orphan them instead of immediately destroying them
				DynNode* pFxNode = dynFxGetNode(pChildFx);
				if ( (pFxNode && pFxNode->pParent) || SAFE_MEMBER(pInfo,bKillIfOrphaned) )
				{
					// We need to recursively check this one for the same flags
					if (dynFxKill(pChildFx, bImmediate, true, bOrphanChildren, eDynFxKillReason_ParentDied))
					{
						--uiChild;
						--uiNumChildren;
						assert(uiNumChildren == eaSize(&pFx->eaChildFx));
					}
				}
				else // we decided to orphan this one, so no need to worry about it's children
				{
					DynFxManager* pFxManager = pFx->pManager;
					// Push it on to the global one
					eaPush(&pFxManager->pWorldRegion->fx_region.pGlobalFxManager->eaDynFx, pChildFx);
					dynFxChangeManager(pChildFx, pFxManager->pWorldRegion->fx_region.pGlobalFxManager);
					eaFindAndRemove(&pFx->eaChildFx, pChildFx);
					--uiChild;
					--uiNumChildren;
					assert(uiNumChildren == eaSize(&pFx->eaChildFx));
				}
			}
		}
	}

	if (!bImmediate)
	{
		dynPushMessage(pFx, pcKillMessage);
	}
	else
	{
		// remove it from parent's list
		if ( bRemoveFromOwner )
		{
			DynFx* pParentFx = GET_REF(pFx->hParentFx);
			if ( pParentFx )
			{
				eaFindAndRemove(&pParentFx->eaChildFx, pFx);
			}
			else
			{
				assert(pFx->pManager);
				dynFxManagerRemoveFxFromList(pFx->pManager, pFx);
			}
		}
		dynFxDelete(pFx, true);
		return true;
	}
	return false;
}


void dynParticleFree( DynParticle* pParticle, DynFxRegion* pFxRegion ) 
{
	if (!pParticle)
		return;

	FOR_EACH_IN_EARRAY(pParticle->eaParticleSets, DynFxFastParticleSet, pSet)
		if (pSet->bSoftKill && pFxRegion)
		{
			pSet->bStopEmitting = true;
			dynFxFastParticleSetOrphan(pSet, pFxRegion);
		}
		else
		{
			dynFxFastParticleSetDestroy(pSet);
		}
	FOR_EACH_END;
	eaDestroy(&pParticle->eaParticleSets);

	if (pParticle->pDraw->pSplat)
	{
		if (pParticle->pDraw->pSplat->pGfxSplat && wl_state.gfx_splat_destroy_callback)
			wl_state.gfx_splat_destroy_callback(pParticle->pDraw->pSplat->pGfxSplat);
		REMOVE_HANDLE(pParticle->pDraw->pSplat->hSplatProjectionNode);
		SAFE_FREE(pParticle->pDraw->pSplat);
	}

	SAFE_FREE(pParticle->pDraw->pDynFlare);
	SAFE_FREE(pParticle->pDraw->pDynLight);
	SAFE_FREE(pParticle->pDraw->pCameraInfo);
	SAFE_FREE(pParticle->pDraw->pSkyVolume);
	SAFE_FREE(pParticle->pDraw->pControlInfo);

	if (pParticle->pDraw->pMeshTrail)
		dynFxMeshTrailFree(pParticle);
	if (pParticle->pDraw->eaSkinChildren)
		eaDestroy(&pParticle->pDraw->eaSkinChildren);
	if (pParticle->pDraw->pDPO)
		dynFxDestroyPhysics(pParticle);
	if (pParticle->pDraw->pDrawSkeleton)
		dynDrawSkeletonFree(pParticle->pDraw->pDrawSkeleton);
	if (pParticle->pDraw->pSkeleton)
		dynSkeletonFree(pParticle->pDraw->pSkeleton);
	if(pParticle->pDraw->pCloth)
		dynClothObjectDelete(pParticle->pDraw->pCloth);
	SAFE_FREE(pParticle->pDraw->pBitFeed);
	SAFE_FREE(pParticle->pDraw->pAnimWordFeed);

	dynNodeClear(&pParticle->pDraw->node);

	if(pParticle->pDraw->eaExtraTextureSwaps) {

		int i;
		for(i = 0; i < eaSize(&pParticle->pDraw->eaExtraTextureSwaps); i++) {
			SAFE_FREE(pParticle->pDraw->eaExtraTextureSwaps[i]);
		}

		eaDestroy(&pParticle->pDraw->eaExtraTextureSwaps);
	}

	if(pParticle->pDraw->eaGeoAddMaterials) {
		eaDestroy(&pParticle->pDraw->eaGeoAddMaterials);
	}

	removeDrawableListRefDbg(pParticle->pDraw->pDrawableList MEM_DBG_PARMS_INIT);
	removeInstanceParamListRef(pParticle->pDraw->pInstanceParamList MEM_DBG_PARMS_INIT);

	RefSystem_RemoveReferent(&pParticle->pDraw->node, true);
	RefSystem_RemoveReferent(pParticle, true);

	free(pParticle->pDraw);
	free(pParticle);
}


void dynFxDelete(DynFx* pFx, bool bImmediate)
{
	DynFxInfo *pInfo;
	
	// free any references
	REMOVE_HANDLE(pFx->hGoToNode);
	REMOVE_HANDLE(pFx->hOrientToNode);
	REMOVE_HANDLE(pFx->hParentFx);
	REMOVE_HANDLE(pFx->hSiblingFx);
	REMOVE_HANDLE(pFx->hScaleToNode);
	{
		DynNode* pNode = GET_REF(pFx->hTargetRoot);
		REMOVE_HANDLE(pFx->hTargetRoot);
		dynNodeAttemptToFreeUnmanagedNode(pNode);
	}
	{
		DynNode* pTargetNode = GET_REF(pFx->hSourceRoot);
		REMOVE_HANDLE(pFx->hSourceRoot);
		dynNodeAttemptToFreeUnmanagedNode(pTargetNode);
	}


	if ( IS_HANDLE_ACTIVE(pFx->hParamBlock))
	{
		DynParamBlock* pParamBlock = GET_REF(pFx->hParamBlock);
		REMOVE_HANDLE(pFx->hParamBlock);
		if ( pParamBlock && pParamBlock->bRunTimeAllocated )
			dynParamBlockFree(pParamBlock);
	}
	if ( pFx->pLight )
	{
		wl_state.remove_light_func(pFx->pLight);
		pFx->pLight = NULL;
	}
	if (pFx->eaDynEventUpdaters)
	{
		const U32 uiNumEventUpdaters = eaSize(&pFx->eaDynEventUpdaters);
		U32 uiEventIndex;
		for (uiEventIndex=0; uiEventIndex<uiNumEventUpdaters; ++uiEventIndex)
		{
			dynEventUpdaterClear(pFx->eaDynEventUpdaters[uiEventIndex], pFx);
		}
		// Free all children
		eaDestroyEx(&pFx->eaDynEventUpdaters, NULL);
	}

	dynFxRemoveFromGrid(pFx);

	// Free alt pivs
	eaDestroyEx(&pFx->eaAltPivs, dynNodeFree);
	eaDestroy(&pFx->eaRayCastNodes);

	ea32Destroy(&pFx->eaMessageOverflow);

	if ( pFx->eaChildFx )
	{
		U32 uiNumChildren = eaSize(&pFx->eaChildFx);
		U32 uiChild;
		for (uiChild=0; uiChild<uiNumChildren; ++uiChild)
		{
			DynFx* pChild = pFx->eaChildFx[uiChild];

			if (pFx->bSystemFade)
			{
				pChild->bSystemFade = true;
				pChild->fFadeOut = MAX(pChild->fFadeOut * pFx->fFadeOut, 0.0f);
				pChild->fFadeIn  = MAX(pChild->fFadeIn * pFx->fFadeIn, 0.0f);
			}

			if ( (pInfo = GET_REF(pChild->hInfo)) && pInfo->bKillIfOrphaned )
			{
				dynFxKill(pChild, false, false, false, eDynFxKillReason_ParentDied);
			}
		}
		dynFxManagerPushFxArray(pFx->pManager, &pFx->eaChildFx );
		eaDestroy(&pFx->eaChildFx);
	}

	RefSystem_RemoveReferent(pFx, true);
	// Delete any particle

	dynParticleFree(pFx->pParticle, bImmediate?NULL:dynFxManGetDynFxRegion(pFx->pManager));
	pFx->pParticle = NULL;

	if ((pInfo = GET_REF(pFx->hInfo)))
	{
		if (!dynFxManDoesntSelfUpdate(pFx->pManager))
			dynFxDebugRemoveTracker(pInfo->pcDynName);

		if (pInfo->bUnique && pFx->pManager)
		{
			bool bFound = dynFxManUniqueFXRemove(pFx->pManager, pInfo);
			/*
			if (!bFound)
				Errorf("FX %s is unique, but we didn't find it in the manager's unique table!\n", pInfo->pcDynName);
				*/
		}

		if(pFx->bPhysicsEnabled) {
			DynFxTracker* pFxTracker = dynFxGetTracker(pInfo->pcDynName);
			if(pFxTracker) {
				if (pFxTracker->uiNumPhysicsObjects == 100)
				{
					dynDebugState.uiNumExcessivePhysicsObjectsFX--;
				}
				pFxTracker->uiNumPhysicsObjects--;
				dynDebugState.uiNumPhysicsObjects--;
			}
		}

	}

	if (pFx->guid)
		dynFxRemoveGuid(pFx->guid);

	REMOVE_HANDLE(pFx->hInfo);

	removeDrawableListRefDbg(pFx->pDrawableList MEM_DBG_PARMS_INIT);
	removeInstanceParamListRef(pFx->pInstanceParamList MEM_DBG_PARMS_INIT);
	dynSortBucketDecRefCount(pFx->pSortBucket);
	eaClearEx(&pFx->eaSortBuckets, dynSortBucketDecRefCount);

	if (pFx->pJitterListSelectData)
		free(pFx->pJitterListSelectData);

	MP_FREE(DynFx, pFx);
}

bool dynFxStopIfNameMatches(DynFx* pFx, const char* pcDynFxInfoName, bool bImmediate)
{
	if ( pFx )
	{
		DynFxInfo* pInfo;

		if ( (pInfo = GET_REF(pFx->hInfo)) && ( !pcDynFxInfoName || stricmp(pInfo->pcDynName, pcDynFxInfoName) == 0 ) )
		{
			return dynFxKill(pFx, bImmediate, false, false, eDynFxKillReason_ManualKill);
		}
	}



	return false;
}


static bool dynFxProcessMessage(DynFx* pFx, U32 uiIndex)
{
	DynFxInfo* pInfo = GET_REF(pFx->hInfo);

	if (!pInfo)
	{
		dynFxLog(pFx, "Couldn't find Fx info!");
		return false;
	}

	if (uiIndex == DYNFX_KILL_MESSAGE_INDEX)
	{
		dynFxLog(pFx, "No Kill Event Found, Starting Kill",);
		return false;
	}
	else if (uiIndex < eaUSize(&pInfo->events))
	{
		DynEvent* pEvent = pInfo->events[uiIndex];
		DynEventUpdater* pNewUpdater;
		dynFxLog(pFx, "Found Event %s",pEvent->pcMessageType);
		pNewUpdater = dynFxCreateEventUpdater(pEvent, pFx);
		if (pNewUpdater)
		{
			eaPush(&pFx->eaDynEventUpdaters, pNewUpdater);
		}
		return true;
	}

	Errorf("Somehow tried to process message index %d, but there are only %d events in fx %s", uiIndex, eaSize(&pInfo->events), pInfo->pcDynName);
	return false;
}

static DynFx* dynFxFindLastChild(DynFx* pFx)
{
	const U32 uiNumChildren = eaUSize(&pFx->eaChildFx);
	if ( uiNumChildren > 0 )
		return dynFxFindLastChild(pFx->eaChildFx[uiNumChildren-1]);
	return pFx;
}

bool findParamForConditional(DynParamBlock* pParamBlock, DynParamConditional* pConditonal, bool* pbResult)
{
	FOR_EACH_IN_EARRAY(pParamBlock->eaDefineParams, DynDefineParam, pParam)
	{
		if (pParam->pcParamName == pConditonal->pcParamName)
		{
			if (pParam->mvVal.type != MULTI_FLOAT)
			{
				Errorf("CallIf requires a parameter of type FLT, check Parameter %s", pParam->pcParamName);
				continue;
			}
			// Found match, do test
			switch (pConditonal->condition)
			{
				xcase edpctEquals:
					*pbResult = (F32)MultiValGetFloat(&pParam->mvVal, NULL) == pConditonal->fValue;
					return true;
				xcase edpctLessThan:
					*pbResult = (F32)MultiValGetFloat(&pParam->mvVal, NULL) < pConditonal->fValue;
					return true;
				xcase edpctGreaterThan:
					*pbResult = (F32)MultiValGetFloat(&pParam->mvVal, NULL) > pConditonal->fValue;
					return true;
			}
		}
	}
	FOR_EACH_END;
	return false;
};

bool dynParamConditionalTest(DynFx* pFx, DynParamConditional* pConditonal)
{
	DynParamBlock* pParamBlock = GET_REF(pFx->hParamBlock)?GET_REF(pFx->hParamBlock):&GET_REF(pFx->hInfo)->paramBlock;
	if (pParamBlock)
	{
		bool bResult = false;
		if (findParamForConditional(pParamBlock, pConditonal, &bResult))
			return bResult;

		// If no match found, check the info for defaultparams
		pParamBlock = GET_REF(pFx->hInfo)?&GET_REF(pFx->hInfo)->paramBlock:NULL;
		if (pParamBlock && findParamForConditional(pParamBlock, pConditonal, &bResult))
			return bResult;
	}
	return false;
}

static DynFx* dynFxCallChild(DynFx* pFx, DynChildCall* pChildCall, DynFx* pPrevSibling, bool bInList)
{
	DynFx* pResult = NULL;
	DynFx* pParentFx = pFx;
	int iCall;

	if (pChildCall->pCallIf)
	{
		if (!dynParamConditionalTest(pFx, pChildCall->pCallIf))
			return NULL;
	}

	if ( pChildCall->bParentToLastChild )
		pParentFx = dynFxFindLastChild(pFx);


	{
		const DynFxInfo* pFxInfo = GET_REF(pChildCall->hChildFx);
		const char* pcChildFx = REF_STRING_FROM_HANDLE(pChildCall->hChildFx);
		DynFxSortBucket* pSortBucket = NULL;
		if (!pFxInfo && pcChildFx && dynFxInfoExists(pcChildFx))
		{
			// It clearly exists, but somehow our reference has broken down. Fix it up.
			resNotifyRefsExist(hDynFxInfoDict, pcChildFx);
			pFxInfo = GET_REF(pChildCall->hChildFx);
		}
		if (!pFxInfo)
		{
			DynFxInfo *pMyInfo = GET_REF(pFx->hInfo);
			dynFxMissingFileError( pMyInfo ? pMyInfo->pcFileName:"UNKNOWN FILE", pcChildFx);
			return NULL;
		}

		if (pChildCall->pcGroupTexturesTag)
		{
			// Try to find it in the parent's list
			FOR_EACH_IN_EARRAY(pFx->eaSortBuckets, DynFxSortBucket, pParentBucket)
			{
				if (pParentBucket->pcGroupTexturesTag == pChildCall->pcGroupTexturesTag)
				{
					pSortBucket = pParentBucket;
					break;
				}
			}
			FOR_EACH_END;

			if (!pSortBucket)
			{
				// Add it to parent's list
				pSortBucket = dynSortBucketCreate(); // ref count is 1 since the parent owns it.
				pSortBucket->pcGroupTexturesTag = pChildCall->pcGroupTexturesTag;
				eaPush(&pFx->eaSortBuckets, pSortBucket);
			}
		}

		for (iCall=0; iCall<pChildCall->iTimesToCall; ++iCall)
		{
			if (bInList || pChildCall->fChance >= 1.0f || randomPositiveF32() < pChildCall->fChance)
			{
				DynAddFxParams params = {0};
				params.pParent = pParentFx;
				params.pSibling = (pResult?pResult:pPrevSibling);
				params.pParamBlock = &pChildCall->paramBlock;
				params.pTargetRoot = GET_REF(pFx->hTargetRoot);
				params.pSourceRoot = GET_REF(pFx->hSourceRoot);
				params.fHue = pFx->fHue;
				params.fSaturation = pFx->fSaturationShift;
				params.fValue = pFx->fValueShift;
				params.pSortBucket = pSortBucket;
				params.uiHitReactID = pFx->uiHitReactID;
				params.eSource = pFx->eSource;
				if (pChildCall->ePriorityOverride)
				{
					params.bOverridePriority = true;
					params.ePriorityOverride = pChildCall->ePriorityOverride;
				}
				pResult = dynAddFx(pFx->pManager, pcChildFx, &params);
			}
		}
	}


	return pResult;
}

DynFx* dynFxCallChildDyns(DynFx* pFx, DynChildCallCollection* pCollection, DynFx* pPrevSibling)
{
	DynFx* pResult = NULL;
	FOR_EACH_IN_EARRAY_FORWARDS(pCollection->eaChildCall, DynChildCall, pChildCall)
	{
		pResult = dynFxCallChild(pFx, pChildCall, pPrevSibling, false);
	}
	FOR_EACH_END;
	FOR_EACH_IN_EARRAY(pCollection->eaChildCallList, DynChildCallList, pChildCallList)
	{
		if (eaSize(&pChildCallList->eaChildCall) > 0)
		{
			int iNumCalls = pChildCallList->iTimesToCall>1?pChildCallList->iTimesToCall:1;
			int i;
			for (i=0; i<iNumCalls; ++i)
			{
				DynChildCall* pChosenChild = NULL;
				// Play one of the FX in the call list
				if (pChildCallList->bEqualChance)
				{
					pChosenChild = pChildCallList->eaChildCall[randomU32() % eaSize(&pChildCallList->eaChildCall)];
				}
				else
				{
					F32 fRandNum = randomPositiveF32();
					F32 fTotalProb = 0.0f;
					FOR_EACH_IN_EARRAY(pChildCallList->eaChildCall, DynChildCall, pChildCall)
					{
						fTotalProb += pChildCall->fChance;
						if ( fRandNum <= fTotalProb )
						{
							pChosenChild = pChildCall;
							break;
						}
					}
					FOR_EACH_END;
				}
				assert(pChosenChild);
				pResult = dynFxCallChild(pFx, pChosenChild, pResult?pResult:pPrevSibling, true);
			}
		}
	}
	FOR_EACH_END;
	return pResult;
}


void dynFxAddNodesFromGeometry(DynFx* pFx, const char* pcModelName)
{
	const ModelHeader* pAPI = wlModelHeaderFromName(pcModelName);
	if (pAPI)
	{
		FOR_EACH_IN_EARRAY(pAPI->altpivot, AltPivot, pAltPiv) {

			int i;
			bool bAlreadyHaveThat = false;

			// Make sure we don't already have an node with this name.
			for(i = 0; i < eaSize(&pFx->eaAltPivs); i++) {

				if(pFx->eaAltPivs[i]) {

					const char *pcExistingName = pFx->eaAltPivs[i]->pcTag;
					if(pcExistingName == pAltPiv->name) {
						
						bAlreadyHaveThat = true;						
						break;
					}
				}
			}

			if(!bAlreadyHaveThat) {

				DynNode* pNewNode = dynNodeAlloc();
				Quat qRot;
				dynNodeSetPos(pNewNode, pAltPiv->mat[3]);
				mat3ToQuat(pAltPiv->mat, qRot);
				dynNodeSetRot(pNewNode, qRot);
				if (!vec3IsZero(pAltPiv->scale))
					dynNodeSetScale(pNewNode, pAltPiv->scale);
				dynNodeParent(pNewNode, &pFx->pParticle->pDraw->node);
				dynNodeSetName(pNewNode, pAltPiv->name);
				eaPush(&pFx->eaAltPivs, pNewNode);
			}

		} FOR_EACH_END
	}
}

void dynFxSetupCloth(DynFx* pFx, const char* pcClothName, const char *pcClothInfo, const char *pcClothColInfo) {

	ModelHeader* pAPI = wlModelHeaderFromName(pcClothName);
	Model *pModel;
	DynClothObject *pClothObject;
	Vec3 scale = {1, 1, 1};
	const DynNode* pNode = dynFxGetNode(pFx);
	bool bLODLoadFail = false;
	int i;

	if(!pAPI) {
		Errorf("Cloth FX: Model not found: %s", pcClothName);
		return;
	}

	pModel = modelFromHeader(pAPI, true, WL_FOR_FX);

	if(!pModel) {
		Errorf("Cloth FX: Error loading model: %s", pcClothName);
		return;
	}

	if(pFx->pParticle) {
		copyVec3(pFx->pParticle->pDraw->vScale, scale);
	}

	for(i = 0; i < eaSize(&pModel->model_lods); i++) {
		ModelLOD *modelLod = modelLODLoadAndMaybeWait(pModel, i, false);
		if(!modelLod) {
			bLODLoadFail = true;
		}
	}

	if(bLODLoadFail) return;

	pClothObject = dynClothObjectSetup(
		pcClothInfo ? pcClothInfo : "cape_default",
		(pcClothColInfo && pcClothColInfo[0]) ? pcClothColInfo : NULL,
		pModel, NULL, NULL, pNode, scale, pFx);

	if(pFx->pParticle && pClothObject) {

		pFx->pParticle->pDraw->pCloth = pClothObject;
		pClothObject->pParticle = pFx->pParticle->pDraw;

		if( !(pFx->pParticle->pDraw->pcMaterialName &&
			pFx->pParticle->pDraw->pcMaterialName[0]) &&
			pClothObject->pModel &&
			pClothObject->pModel->model_lods[0] &&
			pClothObject->pModel->model_lods[0]->materials &&
			pClothObject->pModel->model_lods[0]->materials[0]) {

			// Copy over the cloth's default material if we don't have
			// any material already.
			pFx->pParticle->pDraw->pMaterial = pClothObject->pModel->model_lods[0]->materials[0];
		}
	}
}

static const char * pcParentFXStr = NULL;
static const char * pcParentRelStr = NULL;
static const char * pcParentParentFXStr = NULL;
static const char * pcSiblingFXStr = NULL;

AUTO_RUN;
void SetupFXCompareStrs(void)
{
	pcParentFXStr = allocAddStaticString("ParentFX");
	pcParentRelStr = allocAddStaticString("..");
	pcParentParentFXStr = allocAddStaticString("ParentParentFX");
	pcSiblingFXStr = allocAddStaticString("SiblingFX");
}

__forceinline stringCacheCompare2Strings(const char * pcFirst, const char * pcSecond)
{
	return pcFirst == pcSecond || stringCacheCompareString(pcFirst, pcSecond);
}

static DynFx* dynFxGetRelativeFxByName(const char* pcName, DynFx* pFx)
{
	if ( stringCacheCompare2Strings(pcName,pcParentFXStr) || 
		stringCacheCompare2Strings(pcName,pcParentRelStr))
		return GET_REF(pFx->hParentFx);
	else if ( stringCacheCompare2Strings(pcName,pcParentParentFXStr) )
	{
		DynFx *pParent = GET_REF(pFx->hParentFx);
		if (pParent)
		{
			return (DynFx*)GET_REF(pParent->hParentFx);
		}
		else
		{
			return NULL;
		}
	}
	else if ( stringCacheCompare2Strings(pcName,pcSiblingFXStr) )
	{
		return GET_REF(pFx->hSiblingFx);
	}
	else if ( strnicmp( pcName, "BoneName_", 9) == 0 )
	{
		DynFx* pBoneNameFX = dynFxFindChildrenWithBoneName(pFx, pcName+9);
		if (pBoneNameFX)
			return pBoneNameFX;
	}
	return NULL;
}

const char* pcDummyTarget;
const char* pcTarget;
const char* pcCamera;
const char* pcRoot;
const char* pcExtents;

AUTO_RUN;
void registerCommonNodeStrings(void)
{
	pcDummyTarget = allocAddStaticString("DummyTarget");
	pcTarget = allocAddStaticString("Target");
	pcCamera = allocAddStaticString("Camera");
	pcRoot = allocAddStaticString("Root");
	pcExtents = allocAddStaticString("Extents");

}

static const DynNode* dynFxNodeByNameHelper(const char* pcName, DynFx* pFx, bool *pbHadToUseFallback)
{
	DynFxInfo *pFxInfo = GET_REF(pFx->hInfo);
	bool bUseMountNodeAliases = SAFE_MEMBER(pFxInfo,bUseMountNodeAliases);

	if(pbHadToUseFallback) *pbHadToUseFallback = false;

	if ( pcName == pcDummyTarget )
	{
		return dynFxManGetDummyTargetDynNode(pFx->pManager);
	}
	else if ( pcName == pcTarget )
	{
		const DynNode* pTargetNode = GET_REF(pFx->hTargetRoot);
		return pTargetNode;
	}
	else if ( pcName == pcCamera )
	{
		return dynCameraNodeGet();
	}
	else if ( strnicmp( pcName, "T_", 2) == 0 )
	{
		const DynNode* pTargetNode = GET_REF(pFx->hTargetRoot);
		if ( !pTargetNode )
		{
			return NULL;
		}
		else
		{
			const char* pcTargetSpecificNode = pcName+2;
			const DynNode* pSpecificNode = dynNodeFindByNameConst(pTargetNode, pcTargetSpecificNode, bUseMountNodeAliases);
			if ( pSpecificNode )
				return pSpecificNode;

			if(pbHadToUseFallback) *pbHadToUseFallback = true;
			return pTargetNode;
		}
	}
	else if ( pcName == pcRoot )
	{
		const DynNode* pSourceNode = GET_REF(pFx->hSourceRoot);
		if ( pSourceNode )
		{
			return pSourceNode;
		}
		else
		{
			DynNode* pRootNode = dynFxManGetDynNode(pFx->pManager);
			return pRootNode;
		}
	}
	else if ( strnicmp( pcName, "altPiv", 6) == 0 )
	{
		const DynNode* pFxNode = dynFxGetNode(pFx);
		if (pFxNode)
		{
			const DynNode* pFoundNode = dynNodeFindByNameConst(pFxNode, pcName, bUseMountNodeAliases);
			if (pFoundNode)
				return pFoundNode;
		}
	}
	else if ( strnicmp( pcName, "DCost_", 6) == 0)
	{
		const DynNode* pFxNode = dynFxGetNode(pFx);
		if (pFxNode)
		{
			// this should always be for the current FX's dcost, and not an ancestors via costume tags
			const DynNode* pFoundNode = dynNodeFindByNameConst(pFxNode, pcName+6, bUseMountNodeAliases);
			if (pFoundNode)
				return pFoundNode;
		}
	}
	else if (strnicmp(pcName, "AnimBone_", 9) == 0)
	{
		const DynSkeleton *pSkel = SAFE_MEMBER(pFx->pManager->pDrawSkel,pSkeleton);
		if (pSkel)
		{
			const char* pcPlayOnCostumeTag = SAFE_MEMBER(pFxInfo,pcPlayOnCostumeTag);
			const DynNode *pRootNode;
			
			if (pcPlayOnCostumeTag) {
				const DynSkeleton* pCostumeTagSkel = dynSkeletonFindByCostumeTag(pSkel,pcPlayOnCostumeTag);
				pSkel = FIRST_IF_SET(pCostumeTagSkel,pSkel);
			}

			if (pRootNode = pSkel->pRoot) {
				const DynNode *pFoundNode = dynNodeFindByNameConst(pRootNode, pcName+9, bUseMountNodeAliases);
				if (pFoundNode)
					return pFoundNode;
			}
		}
	}
	else if ( strnicmp( pcName, "Ray", 3)== 0)
	{
		FOR_EACH_IN_EARRAY(pFx->eaRayCastNodes, DynNode, pRayNode)
			/*
			if (pRayNode->pcTag == pRayNode->pcTag)
			{
			*/
				return pRayNode;
		/*
			}
			*/
		FOR_EACH_END
	}
	else if ( strnicmp( pcName, "Contact", 7)== 0)
	{
		if (pFx->pParticle && pFx->pParticle->pDraw->pDPO)
		{
			if (eaSize(&pFx->pParticle->pDraw->pDPO->eaContactUpdaters) > 0)
				return &pFx->pParticle->pDraw->pDPO->eaContactUpdaters[0]->contactNode;
		}
	}
	else if ( strnicmp( pcName, "BoneName_", 9) == 0 )
	{
		DynFx* pBoneNameFX = dynFxFindChildrenWithBoneName(pFx, pcName+9);
		if (pBoneNameFX)
			return dynFxGetNode(pBoneNameFX);
	}
	else if ( strnicmp( pcName, "BoneNameOnSkeleton_", 18) == 0 )
	{
		const DynSkeleton *pSkel = SAFE_MEMBER(pFx->pManager->pDrawSkel,pSkeleton);
		DynFxManager *pManager = pFx->pManager;
		int i;

		if (pSkel)
		{
			const char* pcPlayOnCostumeTag = SAFE_MEMBER(pFxInfo,pcPlayOnCostumeTag);
			const DynNode *pRootNode;

			if (pcPlayOnCostumeTag) {
				const DynSkeleton* pCostumeTagSkel = dynSkeletonFindByCostumeTag(pSkel,pcPlayOnCostumeTag);
				pSkel = FIRST_IF_SET(pCostumeTagSkel, pSkel);
			}

			if(pRootNode = pSkel->pRoot) {
				const DynNode *pFoundNode = dynNodeFindByNameConst(pRootNode, pcName+19, bUseMountNodeAliases);
				if (pFoundNode)
					return pFoundNode;
			}
		}

		for(i = 0; i < eaSize(&pFx->pManager->eaDynFx); i++) {
			DynFx *pOtherFx = dynFxFindChildrenWithBoneName(pFx->pManager->eaDynFx[i], pcName+19);
			if(pOtherFx) {
				return dynFxGetNode(pOtherFx);
			}
		}
	}
	else if ( pcName == pcExtents )
	{
		return dynFxManGetExtentsNode(pFx->pManager);
	}

	// resort to searching the manager node list
	{
		const DynNode* pManNode = dynFxManGetDynNode(pFx->pManager);
		const DynNode* pSourceNode = GET_REF(pFx->hSourceRoot);
		const DynSkeleton* pSkel;

		if (pSourceNode)
		{
			pManNode = pSourceNode;
		}

		if ( pManNode )
		{
			const DynNode* pFoundNode;
			pFoundNode = dynNodeFindByNameConst(pManNode, pcName, bUseMountNodeAliases);
			if (pFoundNode)
				return pFoundNode;
			{
				DynFxInfo* pInfo = GET_REF(pFx->hInfo);
				if (pInfo && pInfo->bRequiresNode)
				{
					return NULL;
				}
			}

			if(pbHadToUseFallback)
				*pbHadToUseFallback = true;

			pSkel = SAFE_MEMBER(pFx->pManager->pDrawSkel,pSkeleton);
			if (pSkel)
			{
				const char* pcPlayOnCostumeTag = SAFE_MEMBER(pFxInfo,pcPlayOnCostumeTag);
				const DynNode* pRootNode;

				if (pcPlayOnCostumeTag) {
					const DynSkeleton* pCostumeTagSkel = dynSkeletonFindByCostumeTag(pSkel,pcPlayOnCostumeTag);
					pSkel = FIRST_IF_SET(pCostumeTagSkel, pSkel);
				}

				if (pRootNode = pSkel->pRoot) {
					const char* pcFallbackNode = dynSkeletonGetDefaultNodeAlias(pSkel, bUseMountNodeAliases);
					const DynNode* pFallbackNode = pcFallbackNode ? dynNodeFindByNameConst(pManNode, pcFallbackNode, bUseMountNodeAliases) : NULL;
					return FIRST_IF_SET(pFallbackNode, pManNode);
				}
			}

			return pManNode;
		}
	}

	if(pbHadToUseFallback) *pbHadToUseFallback = true;
	return NULL;
}

const DynNode* dynFxNodeByName(const char* pcName, DynFx* pFx) {

	// Split up the names into a list of potential node names.
	char *allNamesToken;
	char allNamesBuf[1024];
	char *allNamesStrTokParam = NULL;
	const DynNode* pResult = NULL;
	bool success = false;
	DynFx* pCurFx = pFx;

	// We only want to return NULL if we tried and failed to find a node by name in our last attempt.
	bool triedNodeName = false;

	PERFINFO_AUTO_START_FUNC();

	strcpy(allNamesBuf, pcName);
	allNamesToken = strtok_r(allNamesBuf, ":", &allNamesStrTokParam);

	while(!success && allNamesToken) {

		// Parse through the path for this FX until we get to some other FX so that we can refer to a node from that
		// FX's context. Bail out once we find a match or run out of things to parse.
		char *token;
		char *strtokparam=NULL;
		char cNameBuf[128];
		pCurFx = pFx;
		strcpy(cNameBuf, allNamesToken);
		token = strtok_r(cNameBuf, "\\/", &strtokparam);

		while(token && !success) {

			DynFx* pResultFx = dynFxGetRelativeFxByName(allocAddString(token), pCurFx);
			if (pResultFx) {

				// This is the name of another FX. Keep going.
				pCurFx = pResultFx;

				triedNodeName = false;

			} else {

				bool bFallbackUsed = false;

				// It's not the name of another FX, so it must be the name of a node.
				pResult = dynFxNodeByNameHelper(allocAddString(token), pCurFx, &bFallbackUsed);

				if(pResult && !bFallbackUsed) {

					// Done iterating through all possible node names because we found a good match.
					success = true;
				}

				triedNodeName = true;

			}

			// Next part of the node path.
			token = strtok_r(NULL, "\\/", &strtokparam);
		}

		// Try the next name in the list.
		allNamesToken = strtok_r(NULL, ":", &allNamesStrTokParam);
	}

	// Failed to find anything useful at all. Just use the node for this FX.
	if(!success) {

		DynFxInfo* pInfo = GET_REF(pFx->hInfo);
		if (pInfo && pInfo->bRequiresNode)
			dynFxKillFxAfterUpdate(pFx);
	}

	if(!pResult && !triedNodeName) {
		pResult = dynFxGetNode(pCurFx);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return pResult;
}

void dynFxUpdateParentBhvrKeyframe(DynFx* pFx, DynKeyFrame* pKeyFrame)
{
	const DynNode* pAtNode = NULL;
	const DynNode* pGoToNode = NULL;
	const DynNode* pOrientToNode = NULL;
	const DynNode* pScaleToNode = NULL;
	const DynNode* pSplatProjectionNode = NULL;
	DynNode* pFxNode = dynFxGetNode(pFx);
	bool bMissingGotoNode = false;

	if ( !pFxNode )
		return;

	if (pFx->b2D)
		return;

	if ( !pKeyFrame->pParentBhvr )
	{
		if ( !pFx->pCurrentParentBhvr )
		{
			if ( !pFxNode->pParent )
			{
				dynNodeParent(pFxNode, dynFxManGetDynNode(pFx->pManager));

				if ( pFx->pParticle && pFx->pParticle->pDraw->pDPO )
				{
					dynNodeCopyWorldSpaceWithFlags(pFxNode, pFxNode, ednAll);
					dynNodeClearParent(pFxNode);
					pFxNode->uiTransformFlags &= ~ednLocalRot;
				}
			}
		}
		return;
	}

	pFx->pCurrentParentBhvr = pKeyFrame->pParentBhvr;
	pFx->uiTriggeredNearEvents = 0;


	// Find node pointers
	{
		const char* pcAtNode = pKeyFrame->pParentBhvr->pcAtNode;
		const char* pcToNode = pKeyFrame->pParentBhvr->pcGoToNode;
		const char* pcOrientNode = pKeyFrame->pParentBhvr->pcOrientToNode;
		const char* pcScaleToNode = pKeyFrame->pParentBhvr->pcScaleToNode;
		const char* pcSplatProjectionBone = NULL;

		const U32 uiNumParams = eaUSize(&pKeyFrame->pParentBhvr->eaParams);
		DynParamBlock* pParamBlock = GET_REF(pFx->hParamBlock);
		const U32 uiNumJLists = eaUSize(&pKeyFrame->pParentBhvr->eaJLists);

		pcSplatProjectionBone = pKeyFrame->objInfo[0].obj.splatInfo.pcSplatProjectionBone;
		if(pcSplatProjectionBone) {
			pSplatProjectionNode = dynFxNodeByName(pcSplatProjectionBone, pFx);
		}

		if ( uiNumParams && pParamBlock )
		{
			dynFxApplyCopyParamsGeneral(uiNumParams, pKeyFrame->pParentBhvr->eaParams, PARSE_DYNPARENTBHVR_AT_INDEX, pParamBlock, (void*)&pcAtNode, parse_DynParentBhvr);
			dynFxApplyCopyParamsGeneral(uiNumParams, pKeyFrame->pParentBhvr->eaParams, PARSE_DYNPARENTBHVR_GOTO_INDEX, pParamBlock, (void*)&pcToNode, parse_DynParentBhvr);
			dynFxApplyCopyParamsGeneral(uiNumParams, pKeyFrame->pParentBhvr->eaParams, PARSE_DYNPARENTBHVR_ORIENTTO_INDEX, pParamBlock, (void*)&pcOrientNode, parse_DynParentBhvr);
			dynFxApplyCopyParamsGeneral(uiNumParams, pKeyFrame->pParentBhvr->eaParams, PARSE_DYNPARENTBHVR_SCALETO_INDEX, pParamBlock, (void*)&pcScaleToNode, parse_DynParentBhvr);
		}
		if ( uiNumJLists )
		{
			DynFx* pFxRoot = pFx;
			while (GET_REF(pFxRoot->hParentFx))
			{
				pFxRoot = GET_REF(pFxRoot->hParentFx);
			}
			dynFxApplyJitterLists(uiNumJLists, pKeyFrame->pParentBhvr->eaJLists, PARSE_DYNPARENTBHVR_AT_INDEX, (void*)&pcAtNode, parse_DynParentBhvr, pFxRoot->cbJitterListSelectFunc, pFxRoot->pJitterListSelectData);
			dynFxApplyJitterLists(uiNumJLists, pKeyFrame->pParentBhvr->eaJLists, PARSE_DYNPARENTBHVR_GOTO_INDEX, (void*)&pcToNode, parse_DynParentBhvr, pFxRoot->cbJitterListSelectFunc, pFxRoot->pJitterListSelectData);
			dynFxApplyJitterLists(uiNumJLists, pKeyFrame->pParentBhvr->eaJLists, PARSE_DYNPARENTBHVR_ORIENTTO_INDEX, (void*)&pcOrientNode, parse_DynParentBhvr, pFxRoot->cbJitterListSelectFunc, pFxRoot->pJitterListSelectData);
			dynFxApplyJitterLists(uiNumJLists, pKeyFrame->pParentBhvr->eaJLists, PARSE_DYNPARENTBHVR_SCALETO_INDEX, (void*)&pcScaleToNode, parse_DynParentBhvr, pFxRoot->cbJitterListSelectFunc, pFxRoot->pJitterListSelectData);
		}
		if (pcAtNode)
		{
			pAtNode = dynFxNodeByName(pcAtNode, pFx);
		}
		if (pcToNode)
		{
			pGoToNode = dynFxNodeByName(pcToNode, pFx);
		}
		if (!pGoToNode && pcToNode)
			bMissingGotoNode = true;
		if (pcOrientNode)
		{
			pOrientToNode = dynFxNodeByName(pcOrientNode, pFx);
		}
		if (pcScaleToNode)
		{
			pScaleToNode = dynFxNodeByName(pcScaleToNode, pFx);
		}
	}

	// Scale to
	if ( pScaleToNode )
	{
		dynFxLog(pFx, "Found Scale To Node %s", pScaleToNode->pcTag);
		if (IS_HANDLE_ACTIVE(pFx->hScaleToNode) )
				REMOVE_HANDLE(pFx->hScaleToNode);

		// Don't add a ref if it's a one-time thing
		if (!(pFx->pCurrentParentBhvr->uiDynFxParentFlags & edpfScaleToOnce))
			ADD_SIMPLE_POINTER_REFERENCE_DYN(pFx->hScaleToNode, pScaleToNode);

		dynFxScaleToNode(pScaleToNode, pFxNode, pFx);
	}
	else if (IS_HANDLE_ACTIVE(pFx->hScaleToNode) )
	{
		REMOVE_HANDLE(pFx->hScaleToNode);
	}


	// Orient to
	if ( pOrientToNode )
	{
		dynFxLog(pFx, "Found OrientToNode %s", pOrientToNode->pcTag);
		if (IS_HANDLE_ACTIVE(pFx->hOrientToNode) )
				REMOVE_HANDLE(pFx->hOrientToNode);

		if (!(pFx->pCurrentParentBhvr->uiDynFxParentFlags & edpfOrientToOnce))
			ADD_SIMPLE_POINTER_REFERENCE_DYN(pFx->hOrientToNode, pOrientToNode);
	}
	else if (IS_HANDLE_ACTIVE(pFx->hOrientToNode) )
	{
		REMOVE_HANDLE(pFx->hOrientToNode);
	}

	// Alternate splat projection
	if(pFx && pFx->pParticle && pFx->pParticle->pDraw->pSplat) {
		if(pSplatProjectionNode) {

			dynFxLog(pFx, "Found SplatProjectionNode %s", pSplatProjectionNode->pcTag);

			if(IS_HANDLE_ACTIVE(pFx->pParticle->pDraw->pSplat->hSplatProjectionNode))
				REMOVE_HANDLE(pFx->pParticle->pDraw->pSplat->hSplatProjectionNode);

			ADD_SIMPLE_POINTER_REFERENCE_DYN(
				pFx->pParticle->pDraw->pSplat->hSplatProjectionNode,
				pSplatProjectionNode);

		} else if(IS_HANDLE_ACTIVE(pFx->pParticle->pDraw->pSplat->hSplatProjectionNode)) {
			REMOVE_HANDLE(pFx->pParticle->pDraw->pSplat->hSplatProjectionNode);
		}
	}

	// Go to
	if ( pGoToNode )
	{
		dynFxLog(pFx, "Found GoToNode %s", pGoToNode->pcTag);
		pFx->bGotoActive = true;
		if (IS_HANDLE_ACTIVE(pFx->hGoToNode) )
			REMOVE_HANDLE(pFx->hGoToNode);

		ADD_SIMPLE_POINTER_REFERENCE_DYN(pFx->hGoToNode, pGoToNode);
	}
	else if (IS_HANDLE_ACTIVE(pFx->hGoToNode) )
	{
		pFx->bGotoActive = false;
		REMOVE_HANDLE(pFx->hGoToNode);
	}
	else if (bMissingGotoNode)
	{
		pFx->bGotoActive = true; // pretend like it was trying to goto
	}

	// At
	if ( pAtNode )
	{
		dynFxLog(pFx, "Found AtNode %s", pAtNode->pcTag);
		// if there is any updating, we need a ref
		if (pFx->pCurrentParentBhvr->uiDynFxUpdateFlags & ednAll)
		{
			dynNodeParent(pFxNode, pAtNode);
			pFxNode->uiTransformFlags = pFx->pCurrentParentBhvr->uiDynFxUpdateFlags;
		}
		else
			dynNodeClearParent(pFxNode);
		if (pFx->pCurrentParentBhvr->uiDynFxInheritFlags & ednAll)
		{
			// Don't copy over any flags that are updated by the parent node
			U32 uiFlagsToInherit = pFx->pCurrentParentBhvr->uiDynFxInheritFlags & ~(pFx->pCurrentParentBhvr->uiDynFxUpdateFlags);
			Vec3 vInheritScale;
			dynNodeCopyWorldSpaceWithFlags(pAtNode, pFxNode, uiFlagsToInherit);
			if (pFx->pParticle && (uiFlagsToInherit & ednScale))
			{
				dynNodeGetWorldSpaceScale(pAtNode, vInheritScale);
				mulVecVec3(vInheritScale, pFx->pParticle->pDraw->vScale, pFx->pParticle->pDraw->vScale);
			}
		}
	}

	if (pFx->pCurrentParentBhvr->uiDynFxParentFlags & edpfLocalPosition)
		pFxNode->uiTransformFlags |= ednLocalRot;
	else
		pFxNode->uiTransformFlags &= ~ednLocalRot;

	if ( pOrientToNode && pFx->pCurrentParentBhvr->uiDynFxParentFlags & edpfOrientToOnce )
	{
		dynFxOrientToNode(pOrientToNode, pFxNode, pFx->pCurrentParentBhvr->uiDynFxParentFlags & edpfOrientToLockToPlane);
		if (pFx->pCurrentParentBhvr->uiDynFxParentFlags & edpfAttachAfterOrient && pAtNode)
		{
			Quat qAtRot;
			ANALYSIS_ASSUME(pAtNode != NULL);
			dynNodeGetWorldSpaceRot(pAtNode, qAtRot);
			dynNodeMakeRotationRelative(pFxNode, qAtRot);
			dynNodeParent(pFxNode, pAtNode);
			pFxNode->uiTransformFlags |= ednRot;
		}
	}

	dynFxUpdateParentBhvr(pFx, 0);
}

void dynFxSendMessages(DynFx* pFx, DynFxMessage*** pppMessages)
{
	DynFxRegion* pFxRegion = dynFxManGetDynFxRegion(pFx->pManager);
	FOR_EACH_IN_EARRAY((*pppMessages), DynFxMessage, pMessage)
		dynFxLog(pFx, "Sending Message %s", pMessage->pcMessageType);
		if (pMessage->eSendTo & emtSelf)
			dynPushMessage(pFx, pMessage->pcMessageType);
		if (pMessage->eSendTo & emtParent)
			if (GET_REF(pFx->hParentFx))
				dynPushMessage(GET_REF(pFx->hParentFx), pMessage->pcMessageType);
		if (pMessage->eSendTo & emtChildren)
		{
			FOR_EACH_IN_EARRAY(pFx->eaChildFx, DynFx, pChildFx)
				dynPushMessage(pChildFx, pMessage->pcMessageType);
			FOR_EACH_END
		}
		if (pMessage->eSendTo & emtSiblings)
		{
			DynFx* pParentFx;
			if (pParentFx = GET_REF(pFx->hParentFx))
			{
				FOR_EACH_IN_EARRAY(pParentFx->eaChildFx, DynFx, pChildFx)
					if (pChildFx != pFx)
						dynPushMessage(pChildFx, pMessage->pcMessageType);
				FOR_EACH_END
			}
		}
		if (pFxRegion && (pMessage->eSendTo & emtNear))
		{
			DynNode* pFxNode = dynFxGetNode(pFx);
			if (!pFxNode && pFx->pManager)
				pFxNode = dynFxManGetDynNode(pFx->pManager);
			if (pFxNode)
			{
				Vec3 vFxPos;
				DynFx** eaFx = NULL;
				dynNodeGetWorldSpacePos(pFxNode, vFxPos);
				dynNearMessagePush(pFxRegion, pMessage->pcMessageType, vFxPos, pMessage->fDistance);
			}
		}
		if (pMessage->eSendTo & emtEntity)
		{
			dynFxManBroadcastMessage(pFx->pManager, pMessage->pcMessageType);
		}

		if(dynDebugState.bLabelDebugFX || dynDebugState.bLabelAllFX) {
			DynFxInfo *pInfo = GET_REF(pFx->hInfo);
			if(pInfo) {
				char pcMessageWithName[1024];
				sprintf(pcMessageWithName, "%s - %s", pMessage->pcMessageType, pInfo->pcDynName);
				dynFxAddWorldDebugMessageForFX(pFx, pcMessageWithName, 2, 0, 0, 1);
			}
		}

	FOR_EACH_END
}

static void dynFxScaleToNode( const DynNode* pScaleToNode, DynNode* pFxNode, DynFx* pFx ) 
{
	Vec3 vDistVec, vScaleToNode, vFxNode;
	dynNodeGetWorldSpacePos(pScaleToNode, vScaleToNode);
	dynNodeGetWorldSpacePos(pFxNode, vFxNode);
	subVec3(vScaleToNode, vFxNode, vDistVec);
	pFx->pParticle->fZScaleTo = lengthVec3(vDistVec);
}

static void dynFxOrientToNode( const DynNode* pOrientToNode, DynNode* pFxNode, bool bLockToPlane ) 
{
	// Point this node toward the orient node
	Vec3 vDir;
	Mat3 mOriented;
	Quat qRot;
	Vec3 vOrientToNode, vFxNode;


	Vec3 vUp = {0, 1, 0};
	Vec3 vConvertedUp = {0, 1, 0};
	Quat qParentOrientation = {0, 0, 0, -1};
	DynNode *pParentNode = dynNodeGetParent(pFxNode);


	dynNodeGetWorldSpacePos(pOrientToNode, vOrientToNode);
	dynNodeGetWorldSpacePos(pFxNode, vFxNode);
	subVec3(vOrientToNode, vFxNode, vDir);

	if(bLockToPlane) {

		// Get the UP vector by multiplying up (0, 1, 0) by the PARENT
		// node's orientation or just use a normal up vector if it's
		// parentless. Feed this into orientMat3YVec.
		if(pParentNode) {
			dynNodeGetWorldSpaceRot(pParentNode, qParentOrientation);
			quatRotateVec3(qParentOrientation, vUp, vConvertedUp);
		}

		// Take the direction to the thing we're orienting to (vDir),
		// and project it onto the plane defined by the up
		// vector. Then take the resulting vector and make it
		// vDir. Orienting to this will keep us locked to the y plane
		// we want.
		{

			F32 dp = dotVec3(vConvertedUp, vDir);
			scaleAddVec3(vConvertedUp, -dp, vDir, vDir);
		}

		orientMat3Yvec(mOriented, vDir, vConvertedUp);

	} else {

		orientMat3(mOriented, vDir);

	}

	mat3ToQuat(mOriented, qRot);
	dynNodeSetRot(pFxNode, qRot);
	pFxNode->uiTransformFlags &= ~ednRot; // clear the rot inheritance flag, since we are oriented
}

void dynFxUpdateParentBhvr(DynFx* pFx, DynFxTime uiDeltaTime)
{
	const char* pcNearMessageToSend = NULL;
	const DynNode* pGoToNode = GET_REF(pFx->hGoToNode);
	const DynNode* pOrientToNode = GET_REF(pFx->hOrientToNode);
	const DynNode* pScaleToNode = GET_REF(pFx->hScaleToNode);
	DynNode* pFxNode = dynFxGetNode(pFx);
	F32 fDeltaTime;
	if (!pFxNode)
		return;

	fDeltaTime = FLOATTIME(uiDeltaTime);

	if ( pScaleToNode )
	{
		dynFxScaleToNode(pScaleToNode, pFxNode, pFx);
	}

	if ( pOrientToNode )
	{
		dynFxOrientToNode(pOrientToNode, pFxNode, pFx->pCurrentParentBhvr && (pFx->pCurrentParentBhvr->uiDynFxParentFlags & edpfOrientToLockToPlane));
	}

	if ( pGoToNode )
	{
		Vec3 vToVector;
		F32 fDistance;
		F32 fTravelDistance;
		Vec3 vGoToNode, vFxNode;
		pFx->bGotoActive = true;
		dynNodeGetWorldSpacePos(pGoToNode, vGoToNode);
		dynNodeGetWorldSpacePos(pFxNode, vFxNode);
		subVec3(vGoToNode, vFxNode, vToVector);
		fDistance = normalVec3(vToVector);
		fTravelDistance = fDeltaTime * pFx->pParticle->pDraw->fGoToSpeed;

		{
			const U32 uiNumNearEvents = eaSize(&pFx->pCurrentParentBhvr->eaNearEvents);
			U32 uiNearEvent;
			for (uiNearEvent=0; uiNearEvent<uiNumNearEvents; ++uiNearEvent)
			{
				DynParentNearEvent* pNearEvent = pFx->pCurrentParentBhvr->eaNearEvents[uiNearEvent];
				U8 uiMask = 0x1 << uiNearEvent;
				if ( fDistance < pNearEvent->fDistance + fTravelDistance && !(pFx->uiTriggeredNearEvents & uiMask) )
				{
					Vec3 vImpactDir;
					copyVec3(pFx->pParticle->vWorldSpaceVelocity, vImpactDir);
					normalVec3(vImpactDir);
					if (pFx->uiHitReactID && wl_state.dfx_hit_react_impact_func)
					{
						wl_state.dfx_hit_react_impact_func(pFx->uiHitReactID,vGoToNode,vImpactDir);
						if (dynDebugState.bDrawImpactTriggers)
							dynFxLogTriggerImpact(vGoToNode, vImpactDir);
					}
					if ( pNearEvent->bLock ) // Lock the node to the target node
					{
						DynParticle *pParticle = dynFxGetParticle(pFx);
						Mat4 nodeMat;
						Mat4 gotoMat;
						Mat4 gotoMatInv;
						Mat4 finalMat;

						REMOVE_HANDLE(pFx->hGoToNode);

						dynNodeGetWorldSpaceMat(pFxNode, nodeMat, false);
						dynNodeGetWorldSpaceMat(pGoToNode, gotoMat, false);

						pFx->bGotoActive = false;
						dynNodeParent(pFxNode, pGoToNode);

						if (pGoToNode->pSkeleton		&&
							pParticle					&&
							pParticle->pDraw->pSkeleton	&&
							pParticle->pDraw->pSkeleton->pParentSkeleton &&
							pGoToNode->pSkeleton != pParticle->pDraw->pSkeleton->pParentSkeleton)
						{
							// relink the parent skeleton so we don't create the potential for a thread based crash in animation
							// that crash is on an assert that checks for in-order updating since it'd look dumb to update this FX before the skeleton it just locked onto
							// note: this also stops any animation inheritance from the original skeleton, but that's ok for the time being (given current FX that doe this)
							// note: when the skeleton is pushed, it should free any previous dependencies
							dynSkeletonPushDependentSkeleton(pGoToNode->pSkeleton, pParticle->pDraw->pSkeleton, false, false);
						}

						if(pNearEvent->bLockKeepsOrientation) {
							invertMat4(gotoMat, gotoMatInv);
							mulMat4(gotoMatInv, nodeMat, finalMat);
							dynNodeSetFromMat4(pFxNode, finalMat);
						}

						dynNodeSetPos(pFxNode, zerovec3);

						pFxNode->uiTransformFlags |= ednTrans;

						if(pFx->pParticle && pNearEvent->bLockKeepsOrientation) {
							pFx->pParticle->pDraw->bVelocityDriveOrientation = false;
							zeroVec3(pFx->pParticle->pDraw->vVelocity);
						}
					}

					dynFxSendMessages(pFx, &pNearEvent->eaMessage);

					dynFxCallChildDyns(pFx, &pNearEvent->childCallCollection, NULL);
					pFx->uiTriggeredNearEvents |= uiMask;
				}
			}
		}

		if(GET_REF(pFx->hGoToNode)) {  // make sure we haven't locked

			if ( pFx->pParticle->pDraw->fGoToSpeed)
			{
				Vec3 vPos;
				const F32 *vFxPos;
				scaleVec3(vToVector,fTravelDistance, vToVector);
				vFxPos = dynNodeGetLocalPosRefInline(pFxNode);
				addVec3(vToVector, vFxPos, vPos);
				dynNodeSetPos(pFxNode, vPos);
			}

			// Approach a GoTo node by reducing the distance to it by
			// some fraction every frame.
			if(pFx->pParticle->pDraw->fGoToApproachSpeed) {
			
				Vec3 vTargetPos;
				const F32 *vFxPos;
				Vec3 vDelta;
				Vec3 vFinalPos;
				F32 fDeltaScale;
				
				dynNodeGetWorldSpacePos(pGoToNode, vTargetPos);
				vFxPos = dynNodeGetLocalPosRefInline(pFxNode);
				subVec3(vTargetPos, vFxPos, vDelta);

				fDeltaScale = (pFx->pParticle->pDraw->fGoToApproachSpeed * fDeltaTime);
				
				// Prevent overshooting.
				if(fDeltaScale > 1) fDeltaScale = 1;

				scaleVec3(vDelta, fDeltaScale, vDelta);
				
				addVec3(vFxPos, vDelta, vFinalPos);

				dynNodeSetPos(pFxNode, vFinalPos);
			}

			// Gravity/magnet acceleration for goto. Attraction falls
			// off with distance.
			if(pFx->pParticle->pDraw->fGoToGravity && pFx->pParticle->pDraw->fGoToGravityFalloff) {

				Vec3 vTargetPos;
				const F32 *vFxPos;
				Vec3 vAccel;
				F32 fGravity;
				dynNodeGetWorldSpacePos(pGoToNode, vTargetPos);
				vFxPos = dynNodeGetLocalPosRefInline(pFxNode);

				subVec3(vTargetPos, vFxPos, vAccel);
				if(lengthVec3Squared(vAccel) > 0.1) {
					fGravity = (pFx->pParticle->pDraw->fGoToGravity * fDeltaTime) / (lengthVec3Squared(vAccel) * pFx->pParticle->pDraw->fGoToGravityFalloff);
					scaleVec3(vAccel, fGravity, vAccel);
					addVec3(pFx->pParticle->pDraw->vVelocity, vAccel, pFx->pParticle->pDraw->vVelocity);
				}

			}

			// GoTo node springiness.
			if(pFx->pParticle->pDraw->fGoToSpringConstant) {

				Vec3 vTargetPos;
				const F32 *vFxPos;
				Vec3 vDelta;
				F32 fSpringForce;

				dynNodeGetWorldSpacePos(pGoToNode, vTargetPos);
				vFxPos = dynNodeGetLocalPosRefInline(pFxNode);
				subVec3(vTargetPos, vFxPos, vDelta);

				fSpringForce =
					fDeltaTime *
					(pFx->pParticle->pDraw->fGoToSpringEquilibrium - lengthVec3(vDelta)) *
					pFx->pParticle->pDraw->fGoToSpringConstant;

				normalVec3(vDelta);
				scaleAddVec3(vDelta, fSpringForce, pFx->pParticle->pDraw->vVelocity, pFx->pParticle->pDraw->vVelocity);
			}

		}
	}
	else if (pFx->bGotoActive)
	{
		// We must have somehow lost our go to target.
		// Activate our near events
		FOR_EACH_IN_EARRAY(pFx->pCurrentParentBhvr->eaNearEvents, DynParentNearEvent, pNearEvent)
			dynFxSendMessages(pFx, &pNearEvent->eaMessage);
			dynFxCallChildDyns(pFx, &pNearEvent->childCallCollection, NULL);
		FOR_EACH_END
		pFx->bGotoActive = false;
	}
}





/*
void dynFxObjPrepForDraw(DynObject* pObject)
{
	switch ( pObject->draw.streakMode )
	{
		xcase DynStreakMode_Origin:
		{
		}
		xcase DynStreakMode_Velocity:
		{
			scaleVec3(pObject->draw.vDeltaPos, pObject->draw.fStreakScale, pObject->draw.vStreakDirection);
		}
		xcase DynStreakMode_None:
		default:
		{
			// nothing
		}
	}
}
*/

static void freeMeshTrailUnit(void* pToFree)
{
	DynMeshTrailUnit* pMTU = (DynMeshTrailUnit*)pToFree;
	free(pMTU);
}

static void dynFxMeshTrailFree(DynParticle* pParticle )
{
	if ( pParticle->pDraw->pMeshTrail )
	{
		poolQueueDestroy(pParticle->pDraw->pMeshTrail->qUnits);
		free(pParticle->pDraw->pMeshTrail);
		pParticle->pDraw->pMeshTrail = NULL;
	}
}

static void dynFxMeshTrailUnitUpdate( DynMeshTrailUnit* pTrailUnit, F32 fAge, DynMeshTrailInfo* pMeshInfo, DynMeshTrailUnit* pPrevUnit) 
{
	pTrailUnit->fAge = fAge;

	if (pMeshInfo->fTexDensity > 0.0f)
	{
		if (!pPrevUnit)
			pTrailUnit->fTexCoord = 0.0f;
		else
		{
			Vec3 vDist;
			subVec3(pTrailUnit->vPos, pPrevUnit->vPos, vDist);
			pTrailUnit->fTexCoord = pPrevUnit->fTexCoord + (lengthVec3(vDist) / pMeshInfo->fTexDensity);
		}
	}
}

static void dynMeshTrailUnitCalcPosAndOrientationFromNode(DynNode* pNode, DynMeshTrailUnit* pTrailUnit, bool bCalcOrientation)
{
	dynNodeGetWorldSpacePos(pNode, pTrailUnit->vPos);
	if (bCalcOrientation)
	{
		Quat qRot;
		dynNodeGetWorldSpaceRot(pNode, qRot);
		quatRotateVec3(qRot, upvec, pTrailUnit->vOrientation);
	}
}



void dynFxMeshTrailUpdate(DynFx* pFx, DynFxTime uiDeltaTime)
{
	DynMeshTrail* pMeshTrail = pFx->pParticle ? pFx->pParticle->pDraw->pMeshTrail : NULL;
	DynMeshTrailInfo* pMeshInfo = pMeshTrail ? &pMeshTrail->meshTrailInfo : NULL;
	U32 uiMaxTrailSize = (pMeshInfo && pMeshInfo->mode==DynMeshTrail_Cylinder) ? 50 : 100;
	F32 fDeltaTime = FLOATTIME(uiDeltaTime);
	F32 fOldAge = 0.0f;
	if ( pMeshTrail )
	{
		fOldAge = pMeshTrail->fTrailAge;
		pMeshTrail->fTrailAge += fDeltaTime;
	}

	// Check to see if we should have a mesh trail
	if ( !pMeshInfo || pMeshInfo->mode == DynMeshTrail_None )
	{
		// if not, but we have one, clean it up
		if ( pMeshTrail &&  poolQueueGetNumElements(pMeshTrail->qUnits) == 0)
		{
			dynFxMeshTrailFree(pFx->pParticle);
			pMeshTrail = NULL;
			return;
		}
	}
	else // we should have one
	{
		bool bEmitted = false;

		// Update animated mesh trail info
		dynParticleCopyToDynMeshTrail(pFx->pParticle, pMeshTrail);

		if (pMeshInfo->fMinForwardSpeed > 0.0f)
		{
			// Figure out 'forward speed' of FX particle
			Vec3 vForwardVec;
			Quat qFxRot;
			F32 fForwardSpeed;
			bool bOldStop = pMeshTrail->bStop;
			dynNodeGetWorldSpaceRot(&pFx->pParticle->pDraw->node, qFxRot);
			quatRotateVec3(qFxRot, forwardvec, vForwardVec);
			fForwardSpeed = dotVec3(pFx->pParticle->vWorldSpaceVelocity, vForwardVec);
			pMeshTrail->bStop = (fForwardSpeed < pMeshInfo->fMinForwardSpeed);
			if (!pMeshTrail->bStop && bOldStop && poolQueueGetNumElements(pMeshTrail->qUnits) < 2)
				pMeshTrail->fEmitStartAge = pMeshTrail->fTrailAge;
		}

		// Mesh trails should always have a pool queue with at least one unit
		if ( !pMeshTrail->qUnits )
		{
			DynMeshTrailUnit* pNewUnit;
			pMeshTrail->qUnits = poolQueueCreate();
			poolQueueInit(pMeshTrail->qUnits, sizeof(DynMeshTrailUnit), uiMaxTrailSize, 0);
			pMeshTrail->fTrailAge = 0.0f;
			pMeshTrail->fEmitStartAge = 0.0f;

			// Add first unit
			pNewUnit = poolQueuePreEnqueue(pMeshTrail->qUnits);
			dynFxMeshTrailUnitUpdate(pNewUnit, pMeshTrail->fTrailAge, pMeshInfo, NULL);
			dynMeshTrailUnitCalcPosAndOrientationFromNode(dynFxGetNode(pFx), pNewUnit, pMeshInfo->mode == DynMeshTrail_Normal);
			
			// And make sure we have the last frame pos set to something.
			copyVec3(pNewUnit->vPos, pMeshTrail->vLastFramePos);

			// Give it an INITIAL direction based on the FX node's
			// orientation.
			{
				Quat qRot;
				dynNodeGetWorldSpaceRot(dynFxGetNode(pFx), qRot);
				quatRotateVec3(qRot, forwardvec, pMeshTrail->vLastFrameDir);
			}
		}

		if(pMeshInfo->fShiftSpeed) {
			Quat qRot;
			PoolQueueIterator iter;
			DynMeshTrailUnit* pUnit;
			dynNodeGetWorldSpaceRot(dynFxGetNode(pFx), qRot);
			poolQueueGetIterator(pMeshTrail->qUnits, &iter);
			while(poolQueueGetNextElement(&iter, &pUnit)) {
				Vec3 vOffsetAmt = {0};
				dynNodeGetWorldSpaceRot(dynFxGetNode(pFx), qRot);
				quatRotateVec3(qRot, forwardvec, vOffsetAmt);
				scaleVec3(vOffsetAmt, FLOATTIME(uiDeltaTime) * pMeshInfo->fShiftSpeed, vOffsetAmt);
				addVec3(pUnit->vPos, vOffsetAmt, pUnit->vPos);
			}
		}

		// Push current place into queue
		if (!pMeshTrail->bStop)
		{
			bool bEmitByDistance = pMeshInfo->fEmitDistance > 0.0f;
			F32 fAmountThisFrame = bEmitByDistance?pFx->pParticle->fDistTraveled:fDeltaTime;
			F32 fAmountPer = bEmitByDistance?pMeshInfo->fEmitDistance:1.0f/pMeshInfo->fEmitRate;

			// Check to see if we're moving fast enough to add more units.
			if(pMeshInfo->fEmitSpeedThreshold && poolQueueGetNumElements(pMeshTrail->qUnits)) {

				Vec3 vCurPos;
				PoolQueueIterator pqiter;
				DynMeshTrailUnit* pNewest = NULL;
				F32 fDistMoved = 0;
				F32 fSpeed = 0;
				dynNodeGetWorldSpacePos(dynFxGetNode(pFx), vCurPos);
				poolQueueGetBackwardsIterator(pMeshTrail->qUnits, &pqiter);
				poolQueueGetNextElement(&pqiter, &pNewest);

				if(pNewest) {
					fDistMoved = distance3(vCurPos, pNewest->vPos);
					fSpeed = fDistMoved / fAmountThisFrame;
				}

				if(fSpeed < pMeshInfo->fEmitSpeedThreshold) {
					// Not moving fast enough.
					fAmountThisFrame = 0;
				}

			}

			if (pMeshInfo->bSubFrameCurve)
			{
				F32 fTotalAmount, fTotalProcessed;
				Vec3 vControlPoints[4];
				Vec3 vNewPos, vNewDir, vNewNormal;
				Quat qRot;
				DynMeshTrailDebugInfo* pDebugInfo = NULL;
				bool bShouldUpdateLastFrame = false;

				dynNodeGetWorldSpacePos(dynFxGetNode(pFx), vNewPos);
				dynNodeGetWorldSpaceRot(dynFxGetNode(pFx), qRot);

				// Old advanced mesh trails use the orientation of
				// the FX node...
				//if (vec3IsZero(pMeshInfo->vCurveDir))
				// 	quatRotateVec3(qRot, forwardvec, vNewDir);
				//else
				// 	quatRotateVec3(qRot, pMeshInfo->vCurveDir, vNewDir);

				// In simpler advanced mesh trails, the new direction
				// is just the delta from the last position to the new
				// position, normalized. pMeshTrail->vLastFrameDir
				// still has the direction from the last frame.
				subVec3(vNewPos, pMeshTrail->vLastFramePos, vNewDir);
				normalVec3(vNewDir);

				if (pMeshInfo->mode == DynMeshTrail_Normal)
					quatRotateVec3(qRot, upvec, vNewNormal);

				if (dynDebugState.bDrawMeshTrailDebugInfo)
				{
					FOR_EACH_IN_EARRAY(dynDebugState.eaMeshTrailDebugInfo, DynMeshTrailDebugInfo, pPossibleDebugInfo)
					{
						if (GET_REF(pPossibleDebugInfo->hFx) == pFx)
							pDebugInfo = pPossibleDebugInfo;
					}
					FOR_EACH_END;

					if (!pDebugInfo)
					{
						pDebugInfo = calloc(sizeof(DynMeshTrailDebugInfo), 1);
						eaPush(&dynDebugState.eaMeshTrailDebugInfo, pDebugInfo);
						ADD_SIMPLE_POINTER_REFERENCE_DYN(pDebugInfo->hFx, pFx);
					}
					// pDebugInfo must exist, update the information
					copyVec3(vNewDir, pDebugInfo->vDir);
					copyVec3(vNewPos, pDebugInfo->vPos);
				}

				if (pMeshTrail->fTrailAge > 0.0f)
				{
					// Save the delta from the last generated point to
					// the actual end. This will eventually become
					// pMeshTrail->vLastFrameDir, but only after the
					// very last one, so we get the direction heading
					// into the last point.
					Vec3 vLastPointDir;
					copyVec3(vNewDir, vLastPointDir);

					pMeshTrail->fAccum -= fAmountThisFrame;

					// Set up control points for the bezier curve.
					if (pMeshTrail->fAccum < 0.0f)
					{
						F32 fCurveDistance = distance3(pMeshTrail->vLastFramePos, vNewPos);
						fTotalAmount = -pMeshTrail->fAccum;
						fTotalProcessed = 0.0f;

						copyVec3(pMeshTrail->vLastFramePos, vControlPoints[0]);

						// Bezier control point for the starting point
						// is just the last direction from the last
						// frame. (Direction heading into the last
						// point from whatever the curve was.)
						scaleAddVec3(pMeshTrail->vLastFrameDir, fCurveDistance * 0.5, pMeshTrail->vLastFramePos, vControlPoints[1]);

						// We don't have direction information for the
						// points that haven't happened yet, so just
						// stick the control point on the end point.
						copyVec3(vNewPos, vControlPoints[2]);
						copyVec3(vNewPos, vControlPoints[3]);

						// Old control points were these...
						//scaleAddVec3(pMeshTrail->vLastFrameDir, fCurveDistance * 0.333f, pMeshTrail->vLastFramePos, vControlPoints[1]);
						//scaleAddVec3(vNewDir, fCurveDistance * -0.333f, vNewPos, vControlPoints[2]);

						if (dynDebugState.bDrawMeshTrailDebugInfo && pDebugInfo)
						{
							int i, j;
							for (i=0; i<4; ++i)
							{
								for (j=0; j<3; ++j)
								eafPush(&pDebugInfo->eafPoints, vControlPoints[i][j]);
							}
						}

						bShouldUpdateLastFrame = true;

					} else {

						// It hasn't moved enough or had enough time
						// to justify dropping a new point here. Just
						// update the head position to match where the
						// FX node is at now.
						if (poolQueueGetNumElements(pMeshTrail->qUnits) > 1) {
							PoolQueueIterator iter;
							DynMeshTrailUnit* pNewestUnit;
							poolQueueGetBackwardsIterator(pMeshTrail->qUnits, &iter);
							poolQueueGetNextElement(&iter, &pNewestUnit);
							dynMeshTrailUnitCalcPosAndOrientationFromNode(dynFxGetNode(pFx), pNewestUnit, pMeshInfo->mode == DynMeshTrail_Normal);
						}

					}

					// Generate inbetweens based on the curve.
					while (pMeshTrail->fAccum < 0.0f)
					{
						DynMeshTrailUnit* pNewUnit = NULL;
						DynMeshTrailUnit* pPrevUnit = NULL;
						F32 fFraction;
						F32 fFrac2;
						PoolQueueIterator pqiter;

						pMeshTrail->fAccum += fAmountPer;
						fTotalProcessed += fAmountPer;

						fFraction = fTotalProcessed / fTotalAmount;
						fFraction = CLAMP(fFraction, 0.0f, 1.0f);

						fFrac2 = fFraction;

						// Apply a very slight curve to the t value in
						// the bezier curve to account for the way we
						// dumped the control point for the second
						// point onto the point itself. (Causing
						// things to bunch up onto it.)
						fFraction *= fFraction;
						fFraction = interpF32(0.5, fFraction, fFrac2);

						// Then, move it away from the edges so it
						// doesn't overlap a bit with the next and
						// previous frames. (Moved more away from end
						// than beginning. Actual range is now 0.025
						// to 0.925.)
						fFraction *= 0.9;
						fFraction += 0.025;

						// If we've exceeded maximum size, remove one
						if ( (U32)poolQueueGetNumElements(pMeshTrail->qUnits) >= uiMaxTrailSize )
							poolQueueDequeue(pMeshTrail->qUnits, NULL);

						// Get the last newest one.
						poolQueueGetBackwardsIterator(pMeshTrail->qUnits, &pqiter);
						poolQueueGetNextElement(&pqiter, &pPrevUnit);

						// Add our new unit
						pNewUnit = poolQueuePreEnqueue(pMeshTrail->qUnits);

						// Linear
						//interpVec3(fFraction, pMeshTrail->vLastFramePos, vNewPos, pNewUnit->vPos);
						//copyVec3(pMeshTrail->vLastFrameNormal, pNewUnit->vOrientation);

						// Bezier
						calcCubicBezierCurvePoint(vControlPoints, fFraction, pNewUnit->vPos);
						if (pMeshInfo->mode == DynMeshTrail_Normal)
						{
							Quat qOrientationFromDir;
							Vec3 vAxis;
							F32 fAngle;
							crossVec3(pMeshTrail->vLastFrameNormal, vNewNormal, vAxis);
							normalVec3(vAxis);
							fAngle = getAngleBetweenNormalizedVec3(pMeshTrail->vLastFrameNormal, vNewNormal) * fFraction;
							axisAngleToQuat(vAxis, -fAngle, qOrientationFromDir);
							quatRotateVec3(qOrientationFromDir, pMeshTrail->vLastFrameNormal, pNewUnit->vOrientation);
						}

						// Save the direction from the point we just
						// placed to the end, so we get a final
						// direction of the curve.
						if(distance3(pNewUnit->vPos, vNewPos) > 0) {
							subVec3(vNewPos, pNewUnit->vPos, vLastPointDir);
						}

						dynFxMeshTrailUnitUpdate(pNewUnit, interpF32(fFraction, fOldAge, pMeshTrail->fTrailAge), pMeshInfo, pPrevUnit);

					}

					// Now that we're done, update the last known
					// position, direction, and normal.
					normalVec3(vLastPointDir);
					scaleVec3(vLastPointDir, lengthVec3(vNewDir), vLastPointDir);
					copyVec3(vLastPointDir, vNewDir);
					if (pMeshInfo->mode == DynMeshTrail_Normal)
						copyVec3(vNewNormal, pMeshTrail->vLastFrameNormal);

				}

				if(bShouldUpdateLastFrame) {

					// Now that we're done, update the last known pos/dir
					copyVec3(vNewPos, pMeshTrail->vLastFramePos);
					copyVec3(vNewDir, pMeshTrail->vLastFrameDir);
					if (pMeshInfo->mode == DynMeshTrail_Normal)
						copyVec3(vNewNormal, pMeshTrail->vLastFrameNormal);
				}

			}
			else
			{
				pMeshTrail->fAccum -= fAmountThisFrame;

				// Should we add a new unit or just update the newest one
				if (pMeshTrail->fAccum < 0.0f)
				{ 
					DynMeshTrailUnit* pNewUnit;
					pMeshTrail->fAccum = fAmountPer;

					// If we've exceeded maximum size, remove one
					if ( (U32)poolQueueGetNumElements(pMeshTrail->qUnits) >= uiMaxTrailSize )
						poolQueueDequeue(pMeshTrail->qUnits, NULL);

					// Add our new unit
					pNewUnit = poolQueuePreEnqueue(pMeshTrail->qUnits);
					dynFxMeshTrailUnitUpdate(pNewUnit, pMeshTrail->fTrailAge, pMeshInfo, NULL);
					dynMeshTrailUnitCalcPosAndOrientationFromNode(dynFxGetNode(pFx), pNewUnit, pMeshInfo->mode == DynMeshTrail_Normal);
				}
				else
				{
					// Update head unit to current position
					if (poolQueueGetNumElements(pMeshTrail->qUnits) > 1)
					{
						PoolQueueIterator iter;
						DynMeshTrailUnit* pPrevUnit;
						DynMeshTrailUnit* pNewestUnit;
						poolQueueGetBackwardsIterator(pMeshTrail->qUnits, &iter);
						poolQueueGetNextElement(&iter, &pNewestUnit);
						poolQueueGetNextElement(&iter, &pPrevUnit);

						dynFxMeshTrailUnitUpdate(pNewestUnit, pMeshTrail->fTrailAge, pMeshInfo, pPrevUnit);
						dynMeshTrailUnitCalcPosAndOrientationFromNode(dynFxGetNode(pFx), pNewestUnit, pMeshInfo->mode == DynMeshTrail_Normal);
					}
				}
			}
		}
	}

	if ( pMeshTrail )
	{
		pMeshTrail->uiNumKeyFrames = 1;
		while (pMeshTrail->uiNumKeyFrames < 4 && pMeshInfo->keyFrames[pMeshTrail->uiNumKeyFrames].fTime > pMeshInfo->keyFrames[pMeshTrail->uiNumKeyFrames-1].fTime)
			++pMeshTrail->uiNumKeyFrames; // count the number of keyframes spec'd by the artist

		{
			F32 fMaxAge = pMeshInfo->keyFrames[pMeshTrail->uiNumKeyFrames-1].fTime;
			DynMeshTrailUnit* pUnit;

			// Clean up units that are past the max age
			if (poolQueueGetNumElements(pMeshTrail->qUnits) < 2)
			{
				while (poolQueuePeek(pMeshTrail->qUnits, &pUnit) && pMeshTrail->fTrailAge - pUnit->fAge > fMaxAge)
				{
					poolQueueDequeue(pMeshTrail->qUnits, NULL);
				}
			}
			else
			{
				PoolQueueIterator iter;
				DynMeshTrailUnit* pPrevUnit;
				poolQueueGetIterator(pMeshTrail->qUnits, &iter);
				poolQueueGetNextElement(&iter, &pPrevUnit);
				while (pPrevUnit && poolQueueGetNextElement(&iter, &pUnit) && pMeshTrail->fTrailAge - pUnit->fAge > fMaxAge)
				{
					poolQueueDequeue(pMeshTrail->qUnits, NULL);
					pPrevUnit = pUnit;
				}
			}
		}
	}
}

static F32 dynFxCalcRadius( SA_PARAM_NN_VALID DynFx* pFx, const DynNode* pFxNode )
{
	DynFxInfo *pInfo = GET_REF(pFx->hInfo);
	F32 fMaxRadius = pInfo?pInfo->fRadius:0.0f;
	Vec3 vScale;


	if (pFx->pParticle)
	{
		F32 fPartRadius = 0.0f;
		DynDrawParticle* pDraw = pFx->pParticle->pDraw;
		if (pFx->pParticle->pDraw->pcModelName)
		{
			if (!pFx->pParticle->pDraw->pModel)
			{
				pFx->pParticle->pDraw->pModel = modelFind(pFx->pParticle->pDraw->pcModelName, true, WL_FOR_FX);
			}
			if (pDraw->pModel)
			{
				dynNodeGetWorldSpaceScale(pFxNode, vScale);
				fPartRadius = (pFx->pParticle->pDraw->pModel->radius + lengthVec3(pFx->pParticle->pDraw->pModel->mid)) * maxAbsElemVec3(vScale);
			}
		}
		else if (pFx->pParticle->pDraw->pcClothName)
		{
			if (!pFx->pParticle->pDraw->pCloth)
			{
				// General estimate for cloth radius so we don't have
				// to actually load and setup the model if it's not on
				// screen.
				fPartRadius = 5;
			}
			if (pDraw->pCloth && pFx->pParticle->pDraw->pCloth->pModel)
			{
				dynNodeGetWorldSpaceScale(pFxNode, vScale);
				fPartRadius = pFx->pParticle->pDraw->pCloth->pModel->radius * maxAbsElemVec3(vScale);
			}
		}
		else if (pFx->pParticle->pDraw->pSplat)
		{
			fPartRadius = MAX(pFx->pParticle->pDraw->pSplat->fSplatRadius, pFx->pParticle->pDraw->pSplat->fSplatLength);
		}
		else if (pFx->pParticle->pDraw->pcTextureName)
		{
			dynNodeGetWorldSpaceScale(pFxNode, vScale);
			fPartRadius = maxAbsElemVec3(vScale);
		}
		if (pFx->pParticle->pDraw->iStreakMode != DynStreakMode_None)
		{
			F32 fStreakLength = lengthVec3(pFx->pParticle->pDraw->vStreakDir);
			fPartRadius = MAX(fPartRadius, fStreakLength);
		}
		fMaxRadius = MAX(fMaxRadius, fPartRadius);
	}

	if (pFx->pManager->pDrawSkel)
		fMaxRadius = MAX(fMaxRadius, pFx->pManager->pDrawSkel->pSkeleton->fStaticVisibilityRadius);
	return fMaxRadius;
}

void dynFxUpdateGrid(DynFx* pFx) 
{
	const DynNode* pFxNode = dynFxGetNode(pFx);
	DynFxRegion* pFxRegion = dynFxManGetDynFxRegion(/*pFx->b2D?dynFxGetUiManager(false):*/pFx->pManager);
	DynFxInfo* pInfo = GET_REF(pFx->hInfo);

    if( !pFxRegion || !pInfo || pFx->bHidden)
        return;
    
	if (!pFxNode)
		pFxNode = dynFxManGetDynNode(pFx->pManager);

	if (pFx->pParticle)
	{
		pFx->pParticle->fVisibilityRadius = dynFxCalcRadius(pFx, pFxNode);
		if (pFx->b2D || pFx->pManager->eType == eFxManagerType_Headshot || wl_state.check_particle_visibility_func(pFx->pParticle, pFxRegion))
		{
			assert(pFx->iDrawArrayIndex < 0); // make sure we're not already in this array!
			pFx->iDrawArrayIndex = eaPush(&pFxRegion->eaFXToDraw, pFx);
		}
	}
}

void dynFxRemoveFromGrid(DynFx* pFx)
{
	DynFxRegion* pFxRegion = dynFxManGetDynFxRegion(/*pFx->b2D?dynFxGetUiManager(false):*/pFx->pManager);
	if (pFxRegion && pFx->iDrawArrayIndex >= 0)
	{
		assert(pFxRegion->eaFXToDraw[pFx->iDrawArrayIndex] == pFx);
		pFxRegion->eaFXToDraw[pFx->iDrawArrayIndex] = NULL;
		pFx->iDrawArrayIndex = -1;
	}
	else
	{
		assert(pFx->iDrawArrayIndex < 0);
	}
}

void dynFxRemoveFromGridRecurse(DynFx* pFx)
{
	dynFxRemoveFromGrid(pFx);
	FOR_EACH_IN_EARRAY(pFx->eaChildFx, DynFx, pChildFx)
	{
		dynFxRemoveFromGridRecurse(pChildFx);
	}
	FOR_EACH_END;
}

void dynFxAddWorldDebugMessageForFX(DynFx *pFx, const char *pcMessage, F32 fTime, float r, float g, float b) {

	if(pFx && pFx->pParticle && (
		(pFx->bDebug && dynDebugState.bLabelDebugFX) || 
		dynDebugState.bLabelAllFX)) {

		DynParticle *pParticle = pFx->pParticle;
		DynNode *pNode = &pParticle->pDraw->node;
		Vec3 vWorldPos;
		dynNodeGetWorldSpacePos(pNode, vWorldPos);
		dtAddWorldSpaceMessage(pcMessage, vWorldPos, fTime, r, g, b);
	}

}

static bool dynFxUpdateInternal(int iPartitionIdx, DynFx* pFx, DynFxTime uiDeltaTime, DynFxTime uiOriginalDeltaTime, F32 fFadeOut, bool bSystemFade, bool bFirstUpdate)
{
	U32 uiNumEventUpdaters;
	U32 uiPrevTime;
	const DynFxInfo* pInfo;
	DynFxRegion* pFxRegion = dynFxManGetDynFxRegion(pFx->pManager);
	Vec3 vParticleOrigPos = {0, 0, 0};

	if (!pFx)
		return false;

	pInfo = GET_REF(pFx->hInfo);

	if (!pInfo)
		return false;

	if (pInfo->bDebugFx)
		pFx->bDebug = true;

#if PLATFORM_CONSOLE
	if (pFx->pParticle)
	{
		PREFETCH(((char*)pFx->pParticle) + 0);
		PREFETCH(((char*)pFx->pParticle) + 128);
		PREFETCH(((char*)pFx->pParticle) + 256);
		PREFETCH(((char*)pFx->pParticle) + 384);
	}
#endif

#if !PLATFORM_CONSOLE
	if (pInfo->bDebugFx && dynDebugState.bBreakOnDebugFx)
	{
		_DbgBreak(); // Should break if you have a debugger attached.
	}
#endif

	if(pFx->pParticle) {
		dynNodeGetWorldSpacePos(&pFx->pParticle->pDraw->node, vParticleOrigPos);
	}

	if(pFx->pParticle && pFx->pParticle->pDraw->pControlInfo) {
		if(pFx->pParticle->pDraw->pControlInfo->fTimeScale > 0) {
			uiDeltaTime = uiDeltaTime * pFx->pParticle->pDraw->pControlInfo->fTimeScale;
		}
	}

	uiPrevTime = pFx->uiTimeSinceStart;
	pFx->uiTimeSinceStart += uiDeltaTime;

	// Handle queued up forces
	if (pFx->pParticle && pFx->pParticle->pDraw->pDPO && pFxRegion && pFx->pPhysicsInfo && FLOATTIME(pFx->uiTimeSinceStart) > pFx->pPhysicsInfo->fForceImmunity)
	{
		FOR_EACH_IN_EARRAY(pFxRegion->eaForcePayloads[pFxRegion->uiCurrentPayloadArray], DynForcePayload, pForcePayload)
			dynFxApplyForce(&pForcePayload->force,
							pForcePayload->vForcePos,
							&pFx->pParticle->pDraw->node,
							pFx->pParticle->pDraw->pDPO,
								pFx->pPhysicsInfo->fDensity > 0.0f ?
									pFx->pPhysicsInfo->fDensity :
									1.0f,
							FLOATTIME(uiDeltaTime),
							pForcePayload->qOrientation);
		FOR_EACH_END;
	}


	// Process the start message on the first update
	if (bFirstUpdate)
	{
		int iStartIndex = dynEventIndexFind(pInfo, pcStartMessage);
		if (iStartIndex >= 0)
			dynFxProcessMessage(pFx, iStartIndex);
	}

	// Process any near messages
	if (pFx->pParticle && pFxRegion && eaSize(&pFxRegion->eaNearMessagePayloads[pFxRegion->uiCurrentPayloadArray]) > 0)
	{
		Vec3 vWorldSpacePos;
		dynNodeGetWorldSpacePos(&pFx->pParticle->pDraw->node, vWorldSpacePos);
		FOR_EACH_IN_EARRAY(pFxRegion->eaNearMessagePayloads[pFxRegion->uiCurrentPayloadArray], const DynNearMessage, pNearMessage)
		{
			if (distance3Squared(vWorldSpacePos, pNearMessage->vNearPos) < pNearMessage->fDistanceSquared)
			{
				int iNearIndex = dynEventIndexFind(pInfo, pNearMessage->pcMessage);
				if (iNearIndex >= 0 && !dynFxProcessMessage(pFx, iNearIndex))
				{
					return false; // Kill it!
				}
			}
		}
		FOR_EACH_END;
	}

	// Process the message queue
	// Since during queue processing, it's possible we'll get more messages added, process them in blocks
	{
		U32 uiMessageIndex;
		for (uiMessageIndex=0; uiMessageIndex < pFx->uiMessageCount; ++uiMessageIndex)
		{
			U32 uiMessage = (pFx->uiMessages & (DYNFX_MESSAGE_MASK << (uiMessageIndex * DYNFX_MESSAGE_BITS))) >> (uiMessageIndex * DYNFX_MESSAGE_BITS);
			if (!dynFxProcessMessage(pFx, uiMessage))
				return false;
		}
		pFx->uiMessages = 0;
		pFx->uiMessageCount = 0;

		if (pFx->eaMessageOverflow)
		{
			for (uiMessageIndex=0; uiMessageIndex<eaiUSize(&pFx->eaMessageOverflow); ++uiMessageIndex)
			{
				if (!dynFxProcessMessage(pFx, pFx->eaMessageOverflow[uiMessageIndex]))
					return false;
			}
			ea32Destroy(&pFx->eaMessageOverflow);
		}
	}


	if (pInfo->bHasAutoEvents)
	{
		F32 fPrevTime = FLOATTIME(uiPrevTime);
		F32 fTimeSinceStart = FLOATTIME(pFx->uiTimeSinceStart);
		FOR_EACH_IN_EARRAY(pInfo->events, const DynEvent, pEvent)
			if (pEvent->fAutoCallTime > fPrevTime && pEvent->fAutoCallTime < fTimeSinceStart && !( pFx->uiEventTriggered & (0x1 << pEvent->uiEventIndex) ) )
			{
				DynEventUpdater* pNewUpdater = dynFxCreateEventUpdater(pEvent, pFx);
				if (pNewUpdater)
					eaPush(&pFx->eaDynEventUpdaters, pNewUpdater);
			}
		FOR_EACH_END
	}


	// Update Event updaters time first
	{
		U32 uiEUIndex;
		uiNumEventUpdaters = eaSize(&pFx->eaDynEventUpdaters);
		if (uiNumEventUpdaters)
		{
			for (uiEUIndex = 0; uiEUIndex< uiNumEventUpdaters; ++uiEUIndex)
			{
				DynEventUpdater* pUpdater = pFx->eaDynEventUpdaters[uiEUIndex];
				// update the time of all of the event updaters
				if (!dynEventUpdaterUpdate(iPartitionIdx, pFx, pFx->eaDynEventUpdaters[uiEUIndex], uiDeltaTime))
				{
					// we need to remove it
					DynEventUpdater* pToDelete = eaRemove(&pFx->eaDynEventUpdaters, uiEUIndex);
					dynEventUpdaterClear(pToDelete, pFx);
					free(pToDelete);
					--uiNumEventUpdaters;
					--uiEUIndex;
				}
			}
		}
		else if (pFx->pParticle && pFx->pParticle->pDraw->pDPO && uiDeltaTime > 0)
		{
			// Physics update!
			if (!dynFxPhysicsUpdate(pFx->pParticle, FLOATTIME(uiDeltaTime)))
			{
				dynFxLog(pFx, "Killed: Physics Error causing abort.");
				return false;
			}
		}
	}

	if (wl_state.dfx_update_light_func)
		wl_state.dfx_update_light_func(pFx->bHidden ? NULL : pFx->pParticle, &pFx->pLight);

	if (pFx->pParticle)
	{
		if (pFx->pParticle->pDraw->pDynFlare)
			dynParticleCopyToDynFlare(pFx->pParticle, pFx->pParticle->pDraw->pDynFlare);

		if(pFx->pParticle->pDraw->fTimeSinceLastDraw > 0.25) {

			// It's been a while since this cloth object has been seen. Clean it up and stop simulating.
			if (pFx->pParticle->pDraw->pCloth) {				
				dynClothObjectDelete(pFx->pParticle->pDraw->pCloth);
				pFx->pParticle->pDraw->pCloth = NULL;
			}

		} else {

			if (pFx->pParticle->pDraw->pCloth) {

				Vec3 clothZero2 = {0};
				unitVec3(clothZero2);
				dynClothObjectUpdate(
					pFx->pParticle->pDraw->pCloth,
					FLOATTIME(uiDeltaTime),
					0, clothZero2,
					pFx->pParticle->pDraw->vScale,
					false,
					pFx->pParticle->pDraw->pSkeleton && (pFx->pParticle->pDraw->pSkeleton->bRider || pFx->pParticle->pDraw->pSkeleton->bRiderChild),
					false,
					!pFx->pParticle->pDraw->bClothCollide,
					!pFx->pParticle->pDraw->bClothCollideSelfOnly,
					pFx->pParticle->pDraw->bUseClothWindOverride ? &pFx->pParticle->pDraw->vClothWindOverride : NULL);

			} else if(pFx->pParticle->pDraw->pcClothName) {

				dynFxSetupCloth(pFx, pFx->pParticle->pDraw->pcClothName, pFx->pParticle->pDraw->pcClothInfo, pFx->pParticle->pDraw->pcClothCollisionInfo);

			}
		}

		if (pFx->pParticle->pDraw->pCameraInfo)
		{
			DynCameraInfo *pCameraInfo = pFx->pParticle->pDraw->pCameraInfo;
			dynParticleCopyToDynCameraInfo(pFx->pParticle, pCameraInfo);
			if (pCameraInfo->fShakePower > 0.0f)
			{
				F32 fPower = 0.0f;
				if (pCameraInfo->fShakeRadius <= 0.0f)
				{
					// zero radius means that it applies universally
					fPower = pCameraInfo->fShakePower;
				}
				else
				{
					// Calc distance from camera
					Vec3 vCamPos, vFXPos, vDiff;
					F32 fDist;
					dynNodeGetWorldSpacePos(dynCameraNodeGet(), vCamPos);
					dynNodeGetWorldSpacePos(&pFx->pParticle->pDraw->node, vFXPos);
					subVec3(vCamPos, vFXPos, vDiff);
					fDist = lengthVec3(vDiff);
					if (fDist < pCameraInfo->fShakeRadius)
					{
						fPower = (1.0f - (fDist / pCameraInfo->fShakeRadius)) * pCameraInfo->fShakePower;
					}
				}
				if (fPower > 0.001f)
				{
					dynFxSetScreenShake(fPower, pCameraInfo->fShakeVertical, pCameraInfo->fShakePan, pCameraInfo->fShakeSpeed);
					dynFxSetWaterAgitate(fPower);
				}
			}

			if (wl_state.dfx_camera_fov_func && pCameraInfo->fCameraFOV)
			{
				wl_state.dfx_camera_fov_func(pCameraInfo->fCameraFOV);
			}
						
			if (wl_state.dfx_camera_delay_func && pCameraInfo->fCameraDelaySpeed >= 0.f)
			{
				wl_state.dfx_camera_delay_func(pCameraInfo->fCameraDelaySpeed, pCameraInfo->fCameraDelayDistanceBasis);
			}

			if (wl_state.dfx_camera_lookAt_func && pCameraInfo->pcCameraLookAtNode)
			{
				const DynNode* pLookAt = dynFxNodeByName(pCameraInfo->pcCameraLookAtNode, pFx);
				if (pLookAt)
				{
					Vec3 vWorldPos;
					dynNodeGetWorldSpacePos(pLookAt, vWorldPos);
					wl_state.dfx_camera_lookAt_func(vWorldPos, pCameraInfo->fCameraLookAtSpeed);
				}
			}


			// Do this after all the other camera-related stuff so we have an up to date position.
			if (pFx->pParticle->pDraw->pCameraInfo->bAttachCamera) {

				Mat4 mat;
				int i;

				dynNodeGetWorldSpaceMat(&pFx->pParticle->pDraw->node, mat, false);

				// Camera orientation is weird, so we have to flip it around.
				for(i = 0; i < 3; i++) {
					mat[2][i] *= -1;
					mat[0][i] *= -1;
				}

				dynFxSetCameraMatrixOverride(mat, pFx->pParticle->pDraw->pCameraInfo->fCameraInfluence);
			}

		}

		if (pFx->pParticle->pDraw->pControlInfo) {
			dynParticleCopyToDynFxControlInfo(pFx->pParticle, pFx->pParticle->pDraw->pControlInfo);
		}

		if (pFx->pParticle->pDraw->pSplat)
		{
			dynParticleCopyToDynSplat(pFx->pParticle, pFx->pParticle->pDraw->pSplat);
		}

		if (pFx->pParticle->pDraw->pSkyVolume)
		{
			DynSkyVolume* pVol = pFx->pParticle->pDraw->pSkyVolume;
			DynTransform xWS;
			dynNodeGetWorldSpaceTransform(&pFx->pParticle->pDraw->node, &xWS);
			dynParticleCopyToDynSkyVolume(pFx->pParticle, pVol);
			if (wl_state.dfx_sky_volume_push_func)
			{
				Vec3 vForward;
				Vec3 vOffset;
				setVec3(vForward, 0.0f, 0.0f, pVol->fSkyLength);
				dynTransformApplyToVec3(&xWS, vForward, vOffset);
				wl_state.dfx_sky_volume_push_func(&pVol->ppcSkyName, pFx->pManager->pWorldRegion, xWS.vPos, vOffset, pVol->fSkyRadius, pVol->fSkyWeight, (pVol->eSkyFalloff == eSkyFalloffType_Linear));
			}
		}

		{
			WorldFXEntry* pCellEntry = dynFxManagerGetCellEntry(pFx->pManager);
			if (pCellEntry)
			{
				if (pFx->pParticle->pDraw->iEntLightMode == edelmAdd) {

					Vec3 vShiftedColor;

					hsvShiftRGB(
						pFx->pParticle->pDraw->vColor,
						vShiftedColor,
						pFx->pParticle->pDraw->vHSVShift[0], 
						pFx->pParticle->pDraw->vHSVShift[1],
						pFx->pParticle->pDraw->vHSVShift[2]);

					scaleVec3(vShiftedColor, U8TOF32_COLOR, pCellEntry->ambient_offset);
				}
				else if (pFx->pParticle->pDraw->iEntMaterial != edemmNone)
				{
					pCellEntry->material = materialFindNoDefault(pFx->pParticle->pDraw->pcMaterialName, WL_FOR_FX);
					pCellEntry->add_material = (pFx->pParticle->pDraw->iEntMaterial == edemmAdd || pFx->pParticle->pDraw->iEntMaterial == edemmAddWithConstants || pFx->pParticle->pDraw->iEntMaterial == edemmDissolve);
					pCellEntry->dissolve_material = (pFx->pParticle->pDraw->iEntMaterial == edemmDissolve);
				}

				if (pFx->pParticle->pDraw->iEntTintMode == edetmAlpha)
				{
					pCellEntry->fx_alpha *= pFx->pParticle->pDraw->vColor[3] * U8TOF32_COLOR * pFx->fFadeOut * pFx->fFadeIn;
				}
			}
		}

		dynFxMeshTrailUpdate(pFx, uiDeltaTime);


		if (pFx->pParticle->pDraw->pDrawSkeleton)
		{
			pFx->pParticle->pDraw->pDrawSkeleton->fOtherAlpha = pFx->pParticle->pDraw->vColor[3] / 255.0f;
		}

		pFx->pParticle->pDraw->fTimeSinceLastDraw += FLOATTIME(uiOriginalDeltaTime);
	}

	if ( pFx->bKill || (!uiNumEventUpdaters && pFx->bHasHadEvent && !pFx->uiMessageCount && pFx->pManager != dynFxGetDebrisFxManager(pFx->pManager) ) )
	{
#if DYNFX_LOGGING
		if (!pFx->bKill)
		{
			dynFxLog(pFx, "Killed: Dying Natural Death");
		}
#endif

		// Make sure each particle set gets at least one update, especially for one-frame FX like explosions
		if (pFx->pParticle)
		{
			FOR_EACH_IN_EARRAY(pFx->pParticle->eaParticleSets, DynFxFastParticleSet, pSet)
			{
				if (!pSet->bEmitted) {
					if(pSet->bColorModulation) {
						scaleVec3(pFx->pParticle->pDraw->vColor, 1.0/255.0, pSet->vModulateColor);
					}
					if(pInfo->bLateUpdateFastParticles) {
						copyVec3(pFx->pParticle->vWorldSpaceVelocity, pSet->delayedUpdate.vWorldSpaceVelocity);
						pSet->delayedUpdate.fDeltaTime = FLOATTIME(uiDeltaTime);
						pSet->delayedUpdate.bVisible = true;
						pSet->delayedUpdate.bDelayedUpdate = true;
					} else {
						dynFxFastParticleSetUpdate(pSet, pFx->pParticle->vWorldSpaceVelocity, FLOATTIME(uiDeltaTime), false, true);
					}
				}
			}
			FOR_EACH_END
		}

		return false;
	}
	if (!bFirstUpdate)
		dynFxUpdateGrid(pFx);

	if (pFx->pParticle)
	{
		FOR_EACH_IN_EARRAY(pFx->pParticle->eaParticleSets, DynFxFastParticleSet, pSet) {
			if(pSet->bColorModulation) {
				scaleVec3(pFx->pParticle->pDraw->vColor, 1.0/255.0, pSet->vModulateColor);
			}
			if(pInfo->bLateUpdateFastParticles) {
				copyVec3(pFx->pParticle->vWorldSpaceVelocity, pSet->delayedUpdate.vWorldSpaceVelocity);
				pSet->delayedUpdate.fDeltaTime = FLOATTIME(uiDeltaTime);
				pSet->delayedUpdate.bVisible = (pFx->iDrawArrayIndex >= 0);
				pSet->delayedUpdate.bDelayedUpdate = true;
			} else {
				dynFxFastParticleSetUpdate(pSet, pFx->pParticle->vWorldSpaceVelocity, FLOATTIME(uiDeltaTime), false, pFx->iDrawArrayIndex >= 0);
			}
		} FOR_EACH_END
	}


	if (dynDebugState.bFxDebugOn && pFx->pParticle && wl_state.particle_mem_usage_callback)
	{
		DynFxTracker* pFxTracker = dynFxGetTracker(pInfo->pcDynName);
		if (pFxTracker)
		{
			int iTotal = wl_state.particle_mem_usage_callback(pFx->pParticle);
			pFxTracker->iMemUsage = MAX(pFxTracker->iMemUsage, iTotal);
		}
	}

	{
		F32 fCumulativeFadeOut = fFadeOut * pFx->fFadeOut * pFx->fFadeIn;
	
		if (pFx->pParticle)
		{
			pFx->pParticle->fFadeOut = fCumulativeFadeOut;
			FOR_EACH_IN_EARRAY(pFx->pParticle->eaParticleSets, DynFxFastParticleSet, pSet)

				pSet->fSystemAlpha = fCumulativeFadeOut * lerp(
					1.0f,
					pFx->pParticle->pDraw->vColor[3],
					pSet->fSystemAlphaFromFx);

			FOR_EACH_END
		}
		if ( pFx->eaChildFx )
		{
			U32 uiNumChildFx = eaSize(&pFx->eaChildFx);
			U32 uiChildFx;
			F32 fChildTimeScale = 1.0f;

			if(pFx->pParticle && pFx->pParticle->pDraw->pControlInfo && pFx->pParticle->pDraw->pControlInfo->bTimeScaleChildren) {
				
				fChildTimeScale = pFx->pParticle->pDraw->pControlInfo->fTimeScale;
				
				// Safety net for invalid values.
				if(fChildTimeScale < 0) fChildTimeScale = 1;
			}

			for (uiChildFx=0; uiChildFx<uiNumChildFx; ++uiChildFx)
			{
				if (!dynFxUpdate(iPartitionIdx, pFx->eaChildFx[uiChildFx], uiOriginalDeltaTime * fChildTimeScale, (bSystemFade || pFx->bSystemFade)?fCumulativeFadeOut:1.0f, bSystemFade || pFx->bSystemFade, bFirstUpdate))
				{
					dynFxDeleteOrDebris(pFx->eaChildFx[uiChildFx]);
					eaRemove(&pFx->eaChildFx, uiChildFx);

					// fix for loop
					--uiNumChildFx;
					--uiChildFx;
					CHECK_FX_COUNT;

				}
			}
		}
	}

	if (pFx->bDebris && (GET_REF(pFx->hParentFx) || pFx->pManager != dynFxGetDebrisFxManager(pFx->pManager)))
	{
		dynFxLog(pFx, "Converted to Debris");
		return false; // need to move into the debris manager
	}

	// Handle "too far" message.
	if(pInfo && pInfo->fMovedTooFarMessageDistance && pFx->pParticle && !bFirstUpdate) {

		Vec3 vParticleNewPos;
		F32 fDist = 0;
		
		dynNodeGetWorldSpacePos(&pFx->pParticle->pDraw->node, vParticleNewPos);
		fDist = distance3(vParticleOrigPos, vParticleNewPos);

		if(fDist > pInfo->fMovedTooFarMessageDistance) {
			int iMessageIndex;
			iMessageIndex = dynEventIndexFind(pInfo, pcMovedTooFarMessage);

			if(iMessageIndex >= 0) {
				dynFxProcessMessage(pFx, iMessageIndex);
			}
		}
	}

#if DYNFX_LOGGING
	if (dynDebugState.bFxLogState)
	{
		dynFxLog(pFx, "FrameEnd");
	}
#endif

	if(pInfo) {

		// Add a label for this FX for debug display.
		dynFxAddWorldDebugMessageForFX(pFx, pInfo->pcDynName, 0, 1, 1, 1);

		// Do Alienware marketing color stuff.
		if(pFx->pParticle && pInfo->fAlienColorPriority > 0) {
			dynFxAddAlienColor(
				pInfo->fAlienColorPriority,
				pFx->pParticle->pDraw->vColor);
		}
	}

	return true;
}

bool dynFxUpdate(int iPartitionIdx, DynFx* pFx, DynFxTime uiDeltaTime, F32 fFadeOut, bool bSystemFade, bool bFirstUpdate)
{
	bool bRet;
#if _XBOX && PROFILE
	if (dynDebugState.bPIXLabelParticles)
	{
		const DynFxInfo* pInfo;
		pInfo = GET_REF(pFx->hInfo);
		PIXBeginNamedEvent(0, "%s", pInfo->pcDynName);
	}
#endif

	if (dynDebugState.bRecordFXProfile && !bFirstUpdate)
	{
		if (GET_REF(pFx->hInfo))
			dynFxRecordInfo(GET_REF(pFx->hInfo)->pcDynName);
		fxRecording.uiNumDynFxUpdated++;
	}

	bRet = dynFxUpdateInternal(iPartitionIdx, pFx, (U32)(uiDeltaTime * pFx->fPlaybackSpeed), uiDeltaTime, fFadeOut, bSystemFade, bFirstUpdate);

#if _XBOX && PROFILE
	if (dynDebugState.bPIXLabelParticles)
		PIXEndNamedEvent();
#endif

	return bRet;
}

DynParticle* dynFxGetParticle( DynFx* pFx )
{
	return pFx?pFx->pParticle:NULL;
}

const DynParticle* dynFxGetParticleConst( const DynFx* pFx )
{
	return pFx?pFx->pParticle:NULL;
}

DynNode* dynFxGetNode( DynFx* pFx )
{
	return pFx->pParticle ? &pFx->pParticle->pDraw->node : NULL;
}

const DynFx* dynFxGetParentFxConst( const DynFx* pFx )
{
	DynFx* pFxNonConst = (DynFx*)pFx;
	return (const DynFx*) (pFxNonConst?GET_REF(pFxNonConst->hParentFx):NULL);
}


void dynFxChangeManager(DynFx* pFx, DynFxManager* pNewManager)
{
	const U32 uiNumChildren = eaSize(&pFx->eaChildFx);
	U32 uiChild;
	dynFxRemoveFromGrid(pFx);
	pFx->pManager = pNewManager;
	for (uiChild=0; uiChild<uiNumChildren; ++uiChild)
	{
		dynFxChangeManager(pFx->eaChildFx[uiChild], pNewManager);
	}
}

void dynFxApplyToCostume( SA_PARAM_NN_VALID DynFx* pFx ) 
{
	DynDrawSkeleton* pDrawSkel = dynFxManagerGetDrawSkeleton(pFx->pManager);
	if ( pDrawSkel )
	{
		dynDrawSkeletonPushDynFx(pDrawSkel, pFx, NULL);
	}
}

bool dynFxPriorityBelowDetailSetting(int iPriorityLevel)
{
	return (iPriorityLevel > (dynDebugState.iFxQuality+1));
}

bool dynFxDropPriorityAboveDetailSetting(int iPriorityLevel)
{
	if(iPriorityLevel == edpNotSet) return false;
	return (iPriorityLevel < (dynDebugState.iFxQuality+1));
}

DictionaryHandle hDynNullDictHandle = NULL;

AUTO_RUN;
void DynFxNullDictInit(void)
{
	hDynNullDictHandle = RefSystem_RegisterNullDictionary("DynFxNullDict");
}

DynFxRef* dynFxReferenceCreate(const DynFx *pFx)
{
	DynFxRef *pFxRef;
	pFxRef = MP_ALLOC(DynFxRef);
	ADD_SIMPLE_POINTER_REFERENCE_DYN(pFxRef->hDynFx, pFx);
	return pFxRef;
}

void dynFxReferenceFree(DynFxRef *pFxRef)
{
	REMOVE_HANDLE(pFxRef->hDynFx);
	MP_FREE(DynFxRef, pFxRef);
}

WLCostumePart* dynFxGrabCostumePartFromSkeletonBone(DynSkeleton* pSkeleton, const char* pcBoneName)
{
	WLCostumePart *pPart = NULL;
	WLCostume* pCostume = GET_REF(pSkeleton->hCostume);

	if(pCostume) {

		int j;
		for(j = 0; j < eaSize(&pCostume->eaCostumeParts); j++) {

			if(pCostume->eaCostumeParts[j] &&
				pCostume->eaCostumeParts[j]->pcOrigAttachmentBone == pcBoneName) {

				return pCostume->eaCostumeParts[j];
			}
		}
	}
	return NULL;
}

void dynFxGrabCostumeModel(SA_PARAM_NN_VALID DynFx *pFx) {
	
	if(pFx && pFx->pParticle) {
		const char *pcBoneName = pFx->pParticle->pDraw->pcBoneForCostumeModelGrab;
		if(pcBoneName) {

			DynDrawSkeleton* pDrawSkel = dynFxManagerGetDrawSkeleton(pFx->pManager);
			if(pDrawSkel) {

				int i;
				const char *pcSelectedGeo      = NULL;
				const char *pcSelectedMaterial = NULL;
				MaterialNamedConstant ***peaSelectedMatConstants = NULL;

				DynSkeleton *pSkeleton = pDrawSkel->pSkeleton;
				WLCostumePart *pPart = NULL;

				CostumeTextureSwap ***pppSelectedPartSwaps = NULL;

				pPart = dynFxGrabCostumePartFromSkeletonBone(pSkeleton, pcBoneName);
				if (!pPart)
				{
					//try dependent skeletons too
					for (i = 0; i < eaSize(&pSkeleton->eaDependentSkeletons); i++)
					{
						pPart = dynFxGrabCostumePartFromSkeletonBone(pSkeleton->eaDependentSkeletons[i], pcBoneName);
						if (pPart)
							break;
					}
				}

				if(pPart) {

					if(!pPart->bOptionalPart) {

						// Found a non-optional piece.
						pcSelectedGeo = pPart->pcModel;
						pcSelectedMaterial = pPart->pchMaterial;
						pppSelectedPartSwaps = &pPart->eaTextureSwaps;
						peaSelectedMatConstants = &pPart->eaMatConstant;

					} else {

						if(!pcSelectedGeo) {

							// Fall back on optional geo.
							pcSelectedGeo = pPart->pcModel;
							pcSelectedMaterial = pPart->pchMaterial;
							pppSelectedPartSwaps = &pPart->eaTextureSwaps;
							peaSelectedMatConstants = &pPart->eaMatConstant;

						}									
					}
				}
				if(pcSelectedGeo) {

					pFx->pParticle->pDraw->pcModelName = pcSelectedGeo;

				} else {

					// The FX should probably use a stand-in model or just
					// kill if there's no model to show. Let the FX artist
					// decide.
					dynPushMessage(pFx, allocAddString("NoModel"));
				}

				if(pcSelectedMaterial) {

					if(!pFx->pParticle->pDraw->pcMaterialName || !(pFx->pParticle->pDraw->pcMaterialName[0])) {

						// The FX doesn't already have a material. Use the
						// selected part's material from the costume. Just
						// takes the name (no MNCs).
						pFx->pParticle->pDraw->pcMaterialName = pcSelectedMaterial;
						pFx->pParticle->pDraw->pMaterial = materialFindNoDefault(pcSelectedMaterial, WL_FOR_FX);
					}
				}

				if(pppSelectedPartSwaps) {
					for(i = 0; i < eaSize(pppSelectedPartSwaps); i++) {
						
						eaPush(&pFx->pParticle->pDraw->eaExtraTextureSwaps,
							strdup((*pppSelectedPartSwaps)[i]->pcOldTexture));
						
						eaPush(&pFx->pParticle->pDraw->eaExtraTextureSwaps,
							strdup(
								(*pppSelectedPartSwaps)[i]->pcNewTextureNonPooled ?
								(*pppSelectedPartSwaps)[i]->pcNewTextureNonPooled :
								(*pppSelectedPartSwaps)[i]->pcNewTexture));
					}
				}

				if(peaSelectedMatConstants) {
					for(i = 0; i < eaSize(peaSelectedMatConstants); i++) {

						// Copy material constants over that the FX
						// system can already handle easily.
						if((*peaSelectedMatConstants)[i]) {

							if(stricmp("Color1", (*peaSelectedMatConstants)[i]->name) == 0) {
								scaleVec4((*peaSelectedMatConstants)[i]->value, 255.0, pFx->pParticle->pDraw->vColor1);
								pFx->pParticle->bMultiColor = true;
							}

							if(stricmp("Color2", (*peaSelectedMatConstants)[i]->name) == 0) {
								scaleVec4((*peaSelectedMatConstants)[i]->value, 255.0, pFx->pParticle->pDraw->vColor2);
								pFx->pParticle->bMultiColor = true;
							}

							if(stricmp("Color3", (*peaSelectedMatConstants)[i]->name) == 0) {
								scaleVec4((*peaSelectedMatConstants)[i]->value, 255.0, pFx->pParticle->pDraw->vColor3);
								pFx->pParticle->bMultiColor = true;
							}

						}

					}
				}

			}
		}
	}
}

bool dynFxGetColor(const DynFx *pFx, int index, Vec4 color) {

	if(!pFx) return false;
	if(!pFx->pParticle) return false;

	switch(index) {
		case 0:
			copyVec4(pFx->pParticle->pDraw->vColor, color);
			break;
		case 1:
			copyVec4(pFx->pParticle->pDraw->vColor1, color);
			break;
		case 2:
			copyVec4(pFx->pParticle->pDraw->vColor2, color);
			break;
		case 3:
			copyVec4(pFx->pParticle->pDraw->vColor3, color);
			break;
	}

	return true;
}

bool dynFxSetColor(DynFx *pFx, int index, const Vec4 color) {

	if(!pFx) return false;
	if(!pFx->pParticle) return false;

	switch(index) {
		case 0:
			copyVec4(color, pFx->pParticle->pDraw->vColor);
			break;
		case 1:
			copyVec4(color, pFx->pParticle->pDraw->vColor1);
			break;
		case 2:
			copyVec4(color, pFx->pParticle->pDraw->vColor2);
			break;
		case 3:
			copyVec4(color, pFx->pParticle->pDraw->vColor3);
			break;
	}

	return true;
}

//
// DynFx controls for Alienware color control.
//

typedef struct DynFxAlienColor {
	float priority;
	Vec4 color;
} DynFxAlienColor;

static DynFxAlienColor **s_fxAlienColors = NULL;

void dynFxClearAlienColors(void) {

	int i;
	for(i = 0; i < eaSize(&s_fxAlienColors); i++) {
		free(s_fxAlienColors[i]);
	}
	eaClear(&s_fxAlienColors);

}

void dynFxAddAlienColor(
	float priority,
	Vec4 color) {

	DynFxAlienColor *newColor = calloc(1, sizeof(DynFxAlienColor));
	newColor->priority = priority;
	copyVec4(color, newColor->color);
	eaPush(&s_fxAlienColors, newColor);
}

static int dynFxAlienColorCompare(const DynFxAlienColor **c1, const DynFxAlienColor **c2) {
	return (*c1)->priority - (*c2)->priority;
}

void dynFxDriveAlienColor(void) {

	Vec4 finalColor = {0};

	if(eaSize(&s_fxAlienColors)) {

		int i;

		qsort(
			s_fxAlienColors, eaSize(&s_fxAlienColors),
			sizeof(DynFxAlienColor*), dynFxAlienColorCompare);

		for(i = 0; i < eaSize(&s_fxAlienColors); i++) {
			lerpVec3(
				s_fxAlienColors[i]->color,
				s_fxAlienColors[i]->color[3] / 255.0f,
				finalColor, finalColor);
		}
	}

	if(wl_state.dfx_set_alien_color) {
		Color c = {0};

		if(!vec3IsZero(finalColor)) {
			c.r = (U8)CLAMP(finalColor[0], 0.0f, 255.0f);
			c.g = (U8)CLAMP(finalColor[1], 0.0f, 255.0f);
			c.b = (U8)CLAMP(finalColor[2], 0.0f, 255.0f);
			c.a = 255;
		}

		wl_state.dfx_set_alien_color(c);
	}
}


