
#include "dynAnimInterface.h"
#include "dynFxInterface.h"

#include "error.h"
#include "mathutil.h"

#include "dynSkeleton.h"
#include "dynAnimPhysics.h"
#include "dynDraw.h"
#include "dynFxManager.h"
#include "WorldCellEntryPrivate.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

static U32 uiSkelGuidCount = 0;
static U32 uiDrawSkelGuidCount = 0;

static StashTable stSkeletons;
static StashTable stDrawSkeletons;

DynSkeleton* dynSkeletonFromGuid(dtSkeleton guidSkel)
{
	DynSkeleton* pSkel;
	if ( stashIntFindPointer(stSkeletons, guidSkel, &pSkel))
		return pSkel;
	return NULL;
}

DynDrawSkeleton* dynDrawSkeletonFromGuid(dtDrawSkeleton guidDrawSkel)
{
	DynDrawSkeleton* pDrawSkel;
	if ( stashIntFindPointer(stDrawSkeletons, guidDrawSkel, &pDrawSkel))
		return pDrawSkel;
	return NULL;
}

void dynDrawSkeletonResetDrawableLists()
{
	int geo, max_geo;
	FOR_EACH_IN_STASHTABLE(stDrawSkeletons, DynDrawSkeleton, pDrawSkel)
	{
		for (geo = 0, max_geo = eaSize(&pDrawSkel->eaDynGeos); geo < max_geo; ++geo)
		{
			if (!pDrawSkel->eaDynGeos[geo]->bOwnsDrawables)
			{
				removeDrawableListRefDbg(pDrawSkel->eaDynGeos[geo]->pDrawableList MEM_DBG_PARMS_INIT);
				removeInstanceParamListRef(pDrawSkel->eaDynGeos[geo]->pInstanceParamList MEM_DBG_PARMS_INIT);
				pDrawSkel->eaDynGeos[geo]->pDrawableList = NULL;
				pDrawSkel->eaDynGeos[geo]->pInstanceParamList = NULL;
			}
		}
	}
	FOR_EACH_END
}

void dynSkeletonRemoveGuid(dtSkeleton guidSkel)
{
	bool bSuccess = stashIntRemovePointer(stSkeletons, guidSkel, NULL);
	if (!bSuccess)
		FatalErrorf("Failed to remove skeleton guid %d, must have already been removed!", guidSkel);
}

void dynDrawSkeletonRemoveGuid(dtDrawSkeleton guidDrawSkel)
{
	bool bSuccess = stashIntRemovePointer(stDrawSkeletons, guidDrawSkel, NULL);
	if (!bSuccess)
		FatalErrorf("Failed to remove draw skeleton guid %d, must have already been removed!", guidDrawSkel);
}


dtSkeleton dtSkeletonCreate( const WLCostume* pCostume, EntityRef uiEntRef, bool bLocalPlayer, dtFxManager guidFxMan, dtNode guidLocation, dtNode guidRoot)
{
	DynSkeleton* pSkel = pCostume ? dynSkeletonCreate(pCostume, bLocalPlayer, false, false, false, false, NULL) : NULL;
	ADVANCE_GUID(uiSkelGuidCount);

	if (pSkel)
	{
		ADD_TO_TABLE(pSkel, stSkeletons, uiSkelGuidCount);
		pSkel->guid = uiSkelGuidCount;
		pSkel->uiEntRef = uiEntRef;

		{
			DynNode* pParentNode = dynNodeFromGuid(guidRoot);
			DynFxManager* pFxMan = dynFxManFromGuid(guidFxMan);

			if (pParentNode)
				dynNodeParent(pSkel->pRoot, pParentNode);

			pSkel->pLocation = dynNodeFromGuid(guidLocation);

			if (pFxMan)
			{
				pSkel->pFxManager = pFxMan;
				if (!pSkel->bEverUpdated)
					pFxMan->bWaitingForSkelUpdate = true;
			}
		}
	}
	return uiSkelGuidCount;
}

void dtSkeletonSetCallbacks(	dtSkeleton guid,
								void* preUpdateData,
								DynSkeletonPreUpdateFunc preUpdateFunc,
								DynSkeletonRagdollStateFunc ragdollStateFunc,
								DynSkeletonGetAudioDebugInfoFunc getAudioDebugInfoFunc	)
{
	DynSkeleton* pSkel = dynSkeletonFromGuid(guid);
	
	if(pSkel)
	{
		pSkel->preUpdateData = preUpdateData;
		pSkel->preUpdateFunc = preUpdateFunc;
		pSkel->ragdollStateFunc = ragdollStateFunc;
		pSkel->getAudioDebugInfoFunc = getAudioDebugInfoFunc;
	}
}

void dtSkeletonChangeCostume(dtSkeleton guid, const WLCostume* pCostume)
{
	DynSkeleton* pSkel = dynSkeletonFromGuid(guid);
	if (pSkel && pCostume)
		dynSkeletonChangeCostume(pSkel, pCostume);
}

void dtSkeletonSetTarget(dtSkeleton guid, bool bEnable, Vec3 vTargetPos)
{
	DynSkeleton* pSkel = dynSkeletonFromGuid(guid);
	if (pSkel)
	{
		pSkel->bUseTorsoPointing   = bEnable && pSkel->bTorsoPointing;
		pSkel->bUseTorsoDirections = bEnable && pSkel->bTorsoDirections;

		if(vTargetPos)
		{
			copyVec3(vTargetPos, pSkel->vTargetPos);
			pSkel->bHasTarget = true;
		}
	}
}

void dtSkeletonSetSendDistance(dtSkeleton guid, F32 fSendDistance)
{
	DynSkeleton* pSkel = dynSkeletonFromGuid(guid);
	
	if(SAFE_MEMBER(pSkel, pDrawSkel)){
		MAX1(fSendDistance, 0.f);
		pSkel->pDrawSkel->fSendDistance = fSendDistance;
	}
}

U32 dtSkeletonIsRagdoll(dtSkeleton guid)
{
	DynSkeleton *pSkel = dynSkeletonFromGuid(guid);

	if (SAFE_MEMBER(pSkel, ragdollState.bRagdollOn))
		return 1;
	else
		return 0;
}

void dtSkeletonEndDeathAnimation(dtSkeleton guid)
{
	DynSkeleton *pSkel = dynSkeletonFromGuid(guid);
	
	if (pSkel && (pSkel->bIsPlayingDeathAnim || pSkel->bHasClientSideRagdoll || pSkel->bSleepingClientSideRagdoll)) {
		pSkel->bEndDeathAnimation = 1;
	}
}

void dtSkeletonDisallowRagdoll(dtSkeleton guid)
{
	DynSkeleton *pSkel = dynSkeletonFromGuid(guid);

	if (pSkel) 
	{
		pSkel->bDisallowRagdoll = 1;
	}
}

void dtSkeletonDestroy( dtSkeleton guid)
{
	DynSkeleton* pSkel = dynSkeletonFromGuid(guid);
	if (pSkel)
	{
		dynSkeletonFree(pSkel);
	}
}

dtDrawSkeleton dtDrawSkeletonCreate(dtSkeleton guidSkel,  const WLCostume* pCostume, dtFxManager guidFxMan, bool bAutoDraw, F32 fSendDistance, bool bIsLocalPlayer)
{
	DynSkeleton* pSkel = dynSkeletonFromGuid(guidSkel);
	DynFxManager* pFxManager = dynFxManFromGuid(guidFxMan);
	ADVANCE_GUID(uiDrawSkelGuidCount);
	if (pSkel && pCostume)
	{
		DynDrawSkeleton* pDrawSkel;
		ANALYSIS_ASSUME(pSkel);
		pDrawSkel = dynDrawSkeletonCreate(pSkel, pCostume, pFxManager, bAutoDraw, bIsLocalPlayer, false);

		ADD_TO_TABLE(pDrawSkel, stDrawSkeletons, uiDrawSkelGuidCount);
		pDrawSkel->guid = uiDrawSkelGuidCount;
		//pDrawSkel->bDontDraw = 1;
		pDrawSkel->fSendDistance = fSendDistance;
	}
	return uiDrawSkelGuidCount;
}

void dtDrawSkeletonDestroy( dtDrawSkeleton guid)
{
	DynDrawSkeleton* pDrawSkel = dynDrawSkeletonFromGuid(guid);
	if (pDrawSkel)
	{
		dynDrawSkeletonFree(pDrawSkel);
	}
}

void dtDrawSkeletonSetDebugFlag( dtDrawSkeleton guid)
{
	DynDrawSkeleton* pDrawSkel = dynDrawSkeletonFromGuid(guid);
	if (pDrawSkel)
	{
		dynDebugSetDebugSkeleton(pDrawSkel);
	}
}

static void dtDrawSkeletonSetAlphaHelper(DynDrawSkeleton *pDrawSkel, F32 fAlpha, F32 fGeometryOnlyAlpha)
{
	pDrawSkel->fEntityAlpha = fAlpha;
	pDrawSkel->fGeometryOnlyAlpha = fGeometryOnlyAlpha;

	FOR_EACH_IN_EARRAY(pDrawSkel->eaSubDrawSkeletons, DynDrawSkeleton, pSubSkeleton) {
		dtDrawSkeletonSetAlphaHelper(pSubSkeleton, fAlpha, fGeometryOnlyAlpha);
	} FOR_EACH_END;
}

void dtDrawSkeletonSetAlpha( dtDrawSkeleton guid, F32 fAlpha, F32 fGeometryOnlyAlpha )
{
	DynDrawSkeleton* pDrawSkel = dynDrawSkeletonFromGuid(guid);
	if (pDrawSkel) {
		dtDrawSkeletonSetAlphaHelper(pDrawSkel, fAlpha, fGeometryOnlyAlpha);
	}
}

void dtNodeSetParentBone( dtNode guid,  dtSkeleton parentGuid,  dtSkeleton dependentGuid, const char *boneName,  DynBit attachBit)
{
	DynNode* pNode = dynNodeFromGuid(guid);
	DynSkeleton* pParentSkeleton = dynSkeletonFromGuid(parentGuid);
	DynSkeleton* pDependentSkeleton = dynSkeletonFromGuid(dependentGuid);
	const DynNode *pParentNode = NULL;
	if (pNode)
	{
		if (pParentSkeleton)
		{
			pParentNode = dynSkeletonFindNode(pParentSkeleton, boneName);
			if (!pParentNode)
			{
				pParentNode = dynSkeletonFindNode(pParentSkeleton, "Hips");
			}
			if (pDependentSkeleton)
			{
				dynSkeletonPushDependentSkeleton(pParentSkeleton, pDependentSkeleton, true, false);
				pDependentSkeleton->attachmentBit = attachBit;
			}
		}
		if (pDependentSkeleton)
		{
			if (!pParentNode)
			{
				dynSkeletonFreeDependence(pDependentSkeleton);
				dynNodeClearParent(pNode);
				pNode->uiTransformFlags = ednAll;
			}
			else
			{	
				dynNodeParent(pNode, pParentNode);
				pNode->uiTransformFlags = ednTrans | ednRot;
			}
		}
	}

}

void dtAnimInitSys(void)
{
	stSkeletons = stashTableCreateInt(256);
	stDrawSkeletons = stashTableCreateInt(256);
}

const char* dtCalculateHitReactDirectionBit(const Mat3 mFaceSpace, const Vec3 vDirection)
{
	return dynCalculateHitReactDirectionBit(mFaceSpace, vDirection);
}