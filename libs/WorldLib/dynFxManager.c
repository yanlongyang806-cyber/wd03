#include "dynFxManager.h"

// Utilities
#include "quat.h"
#include "StringCache.h"
#include "cmdparse.h"
#include "ScratchStack.h"
#include "UtilitiesLib.h"
#include "qsortG.h"
#include "auto_float.h"

// world lib
#include "wlState.h"
#include "wlCostume.h"
#include "dynFx.h"
#include "dynMove.h"
#include "dynSeqData.h"
#include "dynAction.h"
#include "dynFxParticle.h"
#include "dynFxInterface.h"
#include "dynDraw.h"
#include "dynSkeleton.h"
#include "dynFxFastParticle.h"
#include "dynFxPhysics.h"
#include "dynFxDebug.h"
#include "WorldGridPrivate.h"
#include "partition_enums.h"
#include "dynNodeInline.h"

#if !PLATFORM_CONSOLE
#include <Windows.h>
#include <ShellAPI.h>
#endif

#include "dynFxEnums_h_ast.h"
#include "dynFxInfo_h_ast.h"

#if DYNFX_TRACKPARAMBLOCKS
#include <utils/Stackwalk.h>
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

static void dynFxVerifyAllManagers(void);

static CRITICAL_SECTION fxLabelsCs;

DynDebugState dynDebugState = {0};

#define MAX_DEBRIS 100

U32 uiMaxDebris = MAX_DEBRIS;
F32 fMaxDebrisDistance = 300.0f;

F32 gfScreenShakeMagnitude;
F32 gfScreenShakeVertical;
F32 gfScreenShakeRotation;
F32 gfScreenShakeSpeed;
F32 gfWaterAgitateMagnitude;

Mat4 gxCameraMatrixOverride;
bool gbOverrideCameraMatrix = false;
F32  gfOverrideCameraInfluence = 0;

void dynFxSetMaxDebris(int iMax)
{
	uiMaxDebris = MAX(iMax, 0);
}



DynFxMessage gStartMessage = 
{
	"Start",
	emtSelf,
	0.0f,
	0
};

DynFxMessage gKillMessage = 
{
	"Kill",
	emtSelf,
	0.0f,
	0
};



#define DEFAULT_DYN_FX_MANAGER_COUNT 1024
MP_DEFINE(DynFxManager);

#define DEFAULT_DYN_MESSAGE_COUNT 4096
MP_DEFINE(DynFxMessage);

#define DEFAULT_DYN_PARAM_BLOCK_COUNT 16384
MP_DEFINE(DynParamBlock);

#define DEFAULT_DYN_FX_MAINTAINED_COUNT 1024
MP_DEFINE(DynFxMaintained);

MP_DEFINE(DynFxSortBucket);
static DynFxSortBucket** eaSortBuckets;

U32 dynSortBucketCount(void)
{
	return eaSize(&eaSortBuckets);
}

static DynNode* pCameraNode;

DynFxManager* pUiFxManager2D;
DynFxManager* pUiFxManager3D;

static DynFxManager** eaOrphanedFxManagers = NULL;

DynFxManager* dtFxManCreateGlobal( DynNode* pParentNode, WorldRegion* pRegion, eFxManagerType eType);
static void dynFxManUpdateSuppressors(DynFxManager* pFxMan, F32 fDeltaTime);
static void dynFxManUpdateIKTargets(DynFxManager* pFxMan);
static void dynFxIKTargetFree(DynFxIKTarget* pTarget);

static void dynFxClearDrawArrayIndex(DynFx *pFx)
{
	pFx->iDrawArrayIndex = -1;
}

void dynFxRegionInit(DynFxRegion* pFxRegion, WorldRegion* pWorldRegion)
{
	DynNode* pGlobalNode = dynNodeAlloc();
	Vec3 vNodePos = { 0.0f, -5000.0f, 0.0f };
	F32 fEffectiveScale = worldRegionGetEffectiveScale(pWorldRegion);
	dynNodeSetPos(pGlobalNode, vNodePos);
	pFxRegion->eaDynFxManagers = NULL;
	pFxRegion->pGlobalFxManager = dtFxManCreateGlobal(pGlobalNode, pWorldRegion, eFxManagerType_Global);
	pFxRegion->pDebrisFxManager = dtFxManCreateGlobal(pGlobalNode, pWorldRegion, eFxManagerType_Debris);
	pFxRegion->bInitalized = true;
}

void dynFxRegionDestroy(DynFxRegion* pFxRegion)
{
	DynNode* pGlobalNode = dynFxManGetDynNode(pFxRegion->pGlobalFxManager);

	FOR_EACH_IN_EARRAY(pFxRegion->eaDynFxManagers, DynFxManager, pFxManager)
		pFxManager->pWorldRegion = NULL;
		eaPush(&eaOrphanedFxManagers, pFxManager);
	FOR_EACH_END;
	eaDestroyEx(&pFxRegion->eaOrphanedSets, dynFxFastParticleSetDestroy);
	dynFxManDestroy(pFxRegion->pGlobalFxManager);
	pFxRegion->pGlobalFxManager = NULL;
	dynFxManDestroy(pFxRegion->pDebrisFxManager);
	pFxRegion->pDebrisFxManager = NULL;
	dynNodeFree(pGlobalNode);
	eaDestroyEx(&pFxRegion->eaFXToDraw, dynFxClearDrawArrayIndex);
	eaDestroy(&pFxRegion->eaDynFxManagers);
	pFxRegion->bInitalized = false;
}

void dynFxRegionWipeOrphanedFastParticleSets(DynFxRegion* pFxRegion)
{
	eaDestroyEx(&pFxRegion->eaOrphanedSets, dynFxFastParticleSetDestroy);
}

void dynFxManagerRemoveFromGrid(DynFxManager *pFxManager) {

	int i;
	for(i = 0; i < eaSize(&pFxManager->eaDynFx); i++) {
		dynFxRemoveFromGridRecurse(pFxManager->eaDynFx[i]);
	}

}

void dynFxManagerInit(void)
{
	DynNode* pGlobalNode = dynNodeAlloc();
    DynNode* pCenterNode = dynNodeAlloc();

    {
        Vec3 center = { 0.5, 0.5, 0 };
        dynNodeSetPos( pCenterNode, center );
    }
    pUiFxManager2D = dynFxManCreate(pGlobalNode, NULL, NULL, eFxManagerType_UI, 2, PARTITION_CLIENT, false, false);
    pUiFxManager3D = dynFxManCreate(pGlobalNode, NULL, NULL, eFxManagerType_UI, 2, PARTITION_CLIENT, false, false);
    
    dynFxManSetTestTargetNode( pUiFxManager2D, pCenterNode, 0);
    dynFxManSetTestTargetNode( pUiFxManager3D, pCenterNode, 0);

	InitializeCriticalSection(&fxLabelsCs);
}

dtFxManager dynFxManGetGUID(DynFxManager* pManager)
{
	return SAFE_MEMBER(pManager, guid);
}

DynFxManager* dynFxManCreate( DynNode* pParentNode, WorldRegion* pRegion, WorldFXEntry* pCellEntry, eFxManagerType eType, dtFxManager guid, int iPartitionIdx, bool bLocalPlayer, bool bNoSound)
{
	// make a mem pool
	DynFxManager* pNewFxManager;
	bool bIsGlobal = (eType == eFxManagerType_UI || eType == eFxManagerType_Global || eType == eFxManagerType_Debris);
	bool bNoRegion = (eType == eFxManagerType_Headshot);

	if (bNoRegion)
		assert(pRegion == NULL );

	MP_CREATE(DynFxManager, DEFAULT_DYN_FX_MANAGER_COUNT);
	pNewFxManager = MP_ALLOC(DynFxManager);

	pNewFxManager->iPartitionIdx = iPartitionIdx;
	pNewFxManager->pNode = pParentNode;
	pNewFxManager->pCellEntry = pCellEntry;
	pNewFxManager->pDummyTargetNode = dynNodeAlloc();
	pNewFxManager->pFakeExtentsNode = dynNodeAlloc();

	dynNodeParent(pNewFxManager->pDummyTargetNode, pParentNode);

	{
		Vec3 vPos;
		vPos[0] = 0.0f;
		vPos[1] = 5.0f;
		vPos[2] = 20.0f;
		dynNodeSetPos(pNewFxManager->pDummyTargetNode, vPos);
	}

	pNewFxManager->eType = eType;
	pNewFxManager->guid = guid;

	if (eType == eFxManagerType_World || eType == eFxManagerType_Headshot || eType == eFxManagerType_Tailor)
		pNewFxManager->bAlwaysKillIfOrphaned = true;

	if (eType != eFxManagerType_UI)
	{
		if (pRegion || bNoRegion)
		{
			pNewFxManager->pWorldRegion = pRegion;
			pNewFxManager->bPermanentRegion = true;
		}
		else
		{
			Vec3 vPos;
			dynNodeGetWorldSpacePos(pParentNode, vPos);
			pNewFxManager->pWorldRegion = worldGetWorldRegionByPos(vPos);
			pNewFxManager->bPermanentRegion = false;
		}

		if (!bNoRegion)
		{
			if (!pNewFxManager->pWorldRegion)
			{
				Errorf("Failed to find world region when creating new fx manager, failing!");
				dynFxManDestroy(pNewFxManager);
				return NULL;
			}
			else if (!bIsGlobal)
				eaPush(&pNewFxManager->pWorldRegion->fx_region.eaDynFxManagers, pNewFxManager);
		}
	}
	else
	{
		pNewFxManager->pWorldRegion = NULL; // UI never has a region
		pNewFxManager->bPermanentRegion = true;
	}

	pNewFxManager->bLocalPlayer = bLocalPlayer;
	pNewFxManager->bNoSound = bNoSound;
	pNewFxManager->bDoesntSelfUpdate = (pNewFxManager->eType == eFxManagerType_Headshot || pNewFxManager->eType == eFxManagerType_Tailor);

	// add it to the manager

	return pNewFxManager;
}



void dynFxManDestroy( DynFxManager* pFxManager )
{
	const U32 uiNumFx = eaSize(&pFxManager->eaDynFx);
	U32 uiFx;
	int i;
	bool bIsGlobal = (pFxManager->eType == eFxManagerType_UI || pFxManager->eType == eFxManagerType_Global || pFxManager->eType == eFxManagerType_Debris);
	bool bForceDestroy = (pFxManager->eType == eFxManagerType_Headshot);
	CHECK_FX_COUNT;

	if(dynDebugState.pTestManager == pFxManager)
		dynDebugState.pTestManager = NULL;
	if(dynDebugState.pDefaultTestManager == pFxManager)
		dynDebugState.pDefaultTestManager = NULL;

	for (i=0; i < eaSize(&pFxManager->eaAutoRetryFX); i++)
	{
		dynParamBlockFree(pFxManager->eaAutoRetryFX[i]->pParamBlock);
		free(pFxManager->eaAutoRetryFX[i]);
	}
	eaDestroy(&pFxManager->eaAutoRetryFX);

	for (uiFx=0; uiFx<uiNumFx; ++uiFx)
	{
		DynFx* pFx = pFxManager->eaDynFx[uiFx];
		DynNode* pFxNode = dynFxGetNode(pFx);
		DynFxInfo *pInfo = GET_REF(pFx->hInfo);
		if (bIsGlobal || bForceDestroy || !pFxManager->pWorldRegion || pFxManager->bAlwaysKillIfOrphaned)
		{
			dynFxKill(pFx, true, false, false, eDynFxKillReason_ManagerKilled);
		}
		else if ( (pFxNode && pFxNode->pParent) || SAFE_MEMBER(pInfo,bKillIfOrphaned))
		{
			dynFxKill(pFx, true, false, true, eDynFxKillReason_ManagerKilled);
		}
		else
		{
			// Push it on to the global one
			eaPush(&pFxManager->pWorldRegion->fx_region.pGlobalFxManager->eaDynFx, pFx);
			dynFxChangeManager(pFx, pFxManager->pWorldRegion->fx_region.pGlobalFxManager);
		}
	}
	eaDestroy(&pFxManager->eaDynFx);
	eaDestroyEx(&pFxManager->eaSuppressors, NULL);
	eaDestroyEx(&pFxManager->eaIKTargets, dynFxIKTargetFree);
	eaDestroyEx(&pFxManager->eaFxToKill, dynFxReferenceFree);

	if (!bIsGlobal)
	{
		if (pFxManager->pWorldRegion)
			eaFindAndRemoveFast(&pFxManager->pWorldRegion->fx_region.eaDynFxManagers, pFxManager);
		else
			eaFindAndRemoveFast(&eaOrphanedFxManagers, pFxManager);
	}

	dynNodeFree(pFxManager->pDummyTargetNode);
	dynNodeFree(pFxManager->pFakeExtentsNode);

	REMOVE_HANDLE(pFxManager->hCostume);

	dynFxManRemoveMaintainedFX(pFxManager, NULL, false);

	if (eaSize(&pFxManager->eaMaintainedFx) > 0)
	{
		FatalErrorf("Failed to properly remove maintained fx from fx manager!");
	}
	eaDestroy(&pFxManager->eaMaintainedFx);

	stashTableDestroy(pFxManager->stUniqueFX);

	CHECK_FX_COUNT;

	if (pFxManager->guid)
		dynFxManRemoveGuid(pFxManager->guid);

	MP_FREE(DynFxManager, pFxManager);
}

DynNode* dynFxManGetDynNode( DynFxManager* pFxManager)
{
	return pFxManager->pNode;
}

void dynFxManSetDynNode( DynFxManager* pFxManager, DynNode* pNode)
{
	pFxManager->pNode = pNode;
}

DynFxRegion* dynFxManGetDynFxRegion( DynFxManager* pFxManager)
{
	return SAFE_MEMBER_ADDR(pFxManager->pWorldRegion, fx_region);
}

void dynFxManSetTestTargetNode( DynFxManager* pFxManager, const DynNode* pTargetNode, dtFxManager targetManagerGuid)
{
	pFxManager->pTargetNode = pTargetNode;
	pFxManager->targetManagerGuid = targetManagerGuid;
}
const DynNode* dynFxManGetTestTargetNode(DynFxManager* pFxManager)
{
	if (pFxManager->pTargetNode)
		return pFxManager->pTargetNode;
	return pFxManager->pDummyTargetNode;
}

DynNode *dynFxManGetExtentsNode(DynFxManager *pFxManager) {

	if(pFxManager->pDrawSkel && pFxManager->pDrawSkel->pSkeleton && pFxManager->pDrawSkel->pSkeleton->pExtentsNode) {
		return pFxManager->pDrawSkel->pSkeleton->pExtentsNode;
	}

	return pFxManager->pFakeExtentsNode;
}

const DynNode* dynFxManGetDummyTargetDynNode(DynFxManager* pFxManager)
{
	return pFxManager->pDummyTargetNode;
}

U32 dynFxManGetFxCount(DynFxManager* pFxManager)
{
	return eaSize(&pFxManager->eaDynFx);
}

const char* dynFxManSwapFX(DynFxManager* pFxManager, const char* pFxInfo, bool bCheckLocalPlayerBased)
{
	WLCostume *pCostume = GET_REF(pFxManager->hCostume);
	const char* pcResult = pFxInfo;

	unsigned int allowedFxSwaps = 5;
	bool didASwap = true;
	const char *last = pcResult;

	while(allowedFxSwaps && didASwap) {

		DynFxInfo *pInfo = NULL;
		REF_TO(DynFxInfo) hInfo;
		SET_HANDLE_FROM_STRING("DynFxInfo", pFxInfo, hInfo);
		pInfo = GET_REF(hInfo);

		allowedFxSwaps--;
		didASwap = false;

		if(pCostume)
			pcResult = wlCostumeSwapFX(pCostume, pcResult);

		if(pFxManager->pWorldRegion)
			pcResult = worldRegionSwapFX(pFxManager->pWorldRegion, pcResult);

		if (pInfo)
		{
			if(pFxManager->bEnemyFaction && pInfo->pcEnemyVersion)
				pcResult = pInfo->pcEnemyVersion;
			else if(bCheckLocalPlayerBased)
			{
				if(!pFxManager->bLocalPlayerBased && pInfo->pcNonTargetVersion)
					pcResult = pInfo->pcNonTargetVersion;
				else if(pFxManager->bLocalPlayerBased && pInfo->pcSourcePlayerVersion)
					pcResult = pInfo->pcSourcePlayerVersion;
			}
		}
		

		if(last != pcResult) {
			didASwap = true;
			last = pcResult;
		}

		REMOVE_HANDLE(hInfo);
	}

	return pcResult;
}

void dynFxSetScreenShake(F32 fPower, F32 fVertical, F32 fRotation, F32 fSpeed)
{
	gfScreenShakeMagnitude = MAX(gfScreenShakeMagnitude, fPower * 0.001f);
	gfScreenShakeVertical = fVertical;
	gfScreenShakeRotation = fRotation;
	gfScreenShakeSpeed = fSpeed;
}

void dynFxSetCameraMatrixOverride(Mat4 xCameraMatrix, F32 fInfluence) {
	copyMat4(xCameraMatrix, gxCameraMatrixOverride);
	gbOverrideCameraMatrix = true;
	gfOverrideCameraInfluence = fInfluence;
}

void dynFxSetWaterAgitate(F32 fPower)
{
    gfWaterAgitateMagnitude = MAX(gfWaterAgitateMagnitude, fPower * 0.001f);
}

void dynFxManSetCostumeFXHue(DynFxManager* pFxManager, F32 fHue)
{
	pFxManager->fCostumeFXHue = fHue;
}

F32 dynFxManGetCostumeFXHue(DynFxManager* pFxManager)
{
	return pFxManager->fCostumeFXHue;
}

void dynFxManAddMaintainedFX(DynFxManager* pFxManager, const char* pcFxName, DynParamBlock *paramblock, 
								F32 fHue, dtNode targetGuid, eDynFxSource eSource)
{
	const char* pcSwappedFxName = dynFxManSwapFX(pFxManager, pcFxName, true);
	// First, make sure it doesn't already exist
	FOR_EACH_IN_EARRAY(pFxManager->eaMaintainedFx, DynFxMaintained, pMaintain)
		if ( stricmp(REF_STRING_FROM_HANDLE(pMaintain->hInfo), pcSwappedFxName)==0 &&
			StructCompare(parse_DynParamBlock, pMaintain->paramblock, paramblock, 0, 0, 0) == 0)
		{
			// Already present
			return;
		}
	FOR_EACH_END

	{
		DynFxMaintained* pFxMaintained;
		MP_CREATE(DynFxMaintained, DEFAULT_DYN_FX_MAINTAINED_COUNT);
		pFxMaintained = MP_ALLOC(DynFxMaintained);
		SET_HANDLE_FROM_STRING("DynFxInfo", pcSwappedFxName, pFxMaintained->hInfo);
//		if (!GET_REF(pFxMaintained->hInfo))
//			Errorf("Could not find maintained FX %s", pcSwappedFxName);
		pFxMaintained->fHue = fHue;
		pFxMaintained->eSource = eSource;
		pFxMaintained->targetGuid = targetGuid;
		pFxMaintained->paramblock = paramblock;
		eaPush(&pFxManager->eaMaintainedFx, pFxMaintained);
	}
}

static void dynFxMaintainedFree(DynFxMaintained* pMaintain, bool bHardKill)
{
	DynFx* pFx = GET_REF(pMaintain->hFx);
	if (pFx)
		dynFxKill(pFx, bHardKill, true, false, eDynFxKillReason_MaintainedFx);

	if(pMaintain->paramblock) {
		dynParamBlockFree(pMaintain->paramblock);
	}

	REMOVE_HANDLE(pMaintain->hFx);
	REMOVE_HANDLE(pMaintain->hInfo);

	MP_FREE(DynFxMaintained, pMaintain);
}

bool dynFxManRemoveMaintainedFX(DynFxManager* pFxManager, const char* pcFxName, bool bHardKill)
{
	const char* pcSwappedFxName = dynFxManSwapFX(pFxManager, pcFxName, true);
	bool bFound = false;
	int iMaintain;
	for (iMaintain = eaSize(&pFxManager->eaMaintainedFx) - 1; iMaintain >= 0; --iMaintain)
	{
		DynFxMaintained* pMaintain = pFxManager->eaMaintainedFx[iMaintain];
		if ( !pcSwappedFxName || stricmp(REF_STRING_FROM_HANDLE(pMaintain->hInfo), pcSwappedFxName)==0)
		{
			dynFxMaintainedFree(pMaintain, bHardKill);
			eaRemove(&pFxManager->eaMaintainedFx, iMaintain);
			bFound = true;
		}
	}
	return bFound;
}

void dynFxManClearAllMaintainedFX(DynFxManager* pFxManager, bool bHardKill)
{
	FOR_EACH_IN_EARRAY(pFxManager->eaMaintainedFx, DynFxMaintained, pMaintain)
	{
		dynFxMaintainedFree(pMaintain, bHardKill);
	}
	FOR_EACH_END;
	eaDestroy(&pFxManager->eaMaintainedFx);
}

void dynFxManRemoveMaintainedFXByIndex(DynFxManager* pFxManager, int iIndex, bool bHardKill)
{

	dynFxMaintainedFree(pFxManager->eaMaintainedFx[iIndex], bHardKill);
	eaRemoveFast(&pFxManager->eaMaintainedFx, iIndex);
}

void dynFxManSendMessageMaintainedFx(DynFxManager* pFxManager, const char* pcFxName, SA_PARAM_NN_VALID DynFxMessage** ppMessages)
{
	const char* pcSwappedFxName = dynFxManSwapFX(pFxManager, pcFxName, true);
	int iMaintain;
	for (iMaintain = eaSize(&pFxManager->eaMaintainedFx) - 1; iMaintain >= 0; --iMaintain)
	{
		DynFxMaintained* pMaintain = pFxManager->eaMaintainedFx[iMaintain];
		if ( !pcSwappedFxName || stricmp(REF_STRING_FROM_HANDLE(pMaintain->hInfo), pcSwappedFxName)==0)
		{
			DynFx* pFx = GET_REF(pMaintain->hFx);
			if (pFx)
			{
				dynFxSendMessages(pFx, &ppMessages);
			}
		}
	}
}

void dynFxManUpdateAutoRetryFX(DynFxManager* pFxManager)
{
	FOR_EACH_IN_EARRAY(pFxManager->eaAutoRetryFX, DynFxCreateParams, pCreateParams) // must walk backwards
	{
		bool bFreed = false;

		dtAddFxEx(pCreateParams, true, &bFreed);

		if (bFreed)
		{
			eaRemove(&pFxManager->eaAutoRetryFX, ipCreateParamsIndex);
		}
	}
	FOR_EACH_END
}

void dynFxManUpdateMaintainedFX(DynFxManager* pFxManager)
{
	FOR_EACH_IN_EARRAY(pFxManager->eaMaintainedFx, DynFxMaintained, pMaintain) // must walk backwards
	{
		if (!GET_REF(pMaintain->hFx) && GET_REF(pMaintain->hInfo))
		{
			const DynFxInfo* pFxInfo = GET_REF(pMaintain->hInfo);

			if (pFxInfo)
			{
				DynFx* pFx;
				DynAddFxParams params = {0};

				if(pMaintain->paramblock) {
					params.pParamBlock = dynParamBlockCopy(pMaintain->paramblock);
				}

				params.fHue = pMaintain->fHue;

				params.eSource = pMaintain->eSource;
				if (pMaintain->targetGuid)
					params.pTargetRoot = dynNodeFromGuid(pMaintain->targetGuid);

				if (pFxInfo->pcExclusionTag && dynFxExclusionTagMatches(pFxInfo->pcExclusionTag) )
				{
					// Cancel maintained FX
					dynFxMaintainedFree(pMaintain, true);
					dynParamBlockFree(params.pParamBlock);
					eaRemove(&pFxManager->eaMaintainedFx, ipMaintainIndex);
					continue; // safe because we're walking the earray backwards
				}

				pFx = dynAddFx(pFxManager, REF_STRING_FROM_HANDLE_KNOWN_NONNULL(pMaintain->hInfo), &params);
				REMOVE_HANDLE(pMaintain->hFx);
				if (pFx)
				{
					ADD_SIMPLE_POINTER_REFERENCE_DYN(pMaintain->hFx, pFx);
				}
			}
			else
				Errorf("Unable to find valid maintained FX %s", REF_STRING_FROM_HANDLE(pMaintain->hInfo));
		}
	}
	FOR_EACH_END
}

void dynFxManagerRemoveFxFromList(DynFxManager* pFxManager, DynFx* pFx)
{
	eaFindAndRemoveFast(&pFxManager->eaDynFx, pFx);
}

DynParamBlock* dynParamBlockCopyEx(const DynParamBlock *pBlock, const char *reason, int lineNum)
{
	if(pBlock) {

		DynParamBlock *pNewParamBlock = dynParamBlockCreateEx(reason, lineNum);
		const char *pcNewlyAllocatedReason = pNewParamBlock->pcReason;

		StructCopyFields(parse_DynParamBlock, pBlock, pNewParamBlock, 0, 0);
		pNewParamBlock->bRunTimeAllocated = true;

		if(pBlock->pcReason) {
			pNewParamBlock->pcReason = pBlock->pcReason;
		} else {
			pNewParamBlock->pcReason = pcNewlyAllocatedReason;
		}

		return pNewParamBlock;
	}

	return NULL;
}

#if DYNFX_TRACKPARAMBLOCKS

// Keep a list of all (properly) allocated DynParamBlocks. We'll keep a separate list of allocation reasons because they
// tend to get clobbered when they're inside the structure itself.
static DynParamBlock **eaAllocatedParamBlocks = NULL;
static const char **eaRealParamBlockReasons = NULL;

#endif

int dynFxTrackParamBlocks = 0;
AUTO_CMD_INT(dynFxTrackParamBlocks, dynFxTrackParamBlocks) ACMD_COMMANDLINE;

static void dynParamBlockFixupForNewCopy(DynParamBlock *pBlock) {

  #if DYNFX_TRACKPARAMBLOCKS

	int index = eaFind(&eaAllocatedParamBlocks, pBlock);

	const char *pcOldReason = NULL;

	if(index != -1) {
		pcOldReason = eaRealParamBlockReasons[index];
	}

	if(pcOldReason) {

		pBlock->pcReason = pcOldReason;

	} else if(!pBlock->pcReason) {

		if(dynFxTrackParamBlocks) {

			int bufSize = 2048*4;
			char *stackBuffer = calloc(1, bufSize);

			printf("*** Unknown FX param block allocation. Copying entire stack.\n");

			stackWalkDumpStackToBuffer(
				stackBuffer, bufSize,
				NULL, NULL);

			printf("*** Stack:\n%s\n", stackBuffer);

			pBlock->pcReason = allocAddString(stackBuffer);

			free(stackBuffer);

		} else {

			pBlock->pcReason = allocAddString("Unknown copy");

		}
	}

	if(index == -1) {
		eaPush(&eaAllocatedParamBlocks, pBlock);
		eaPush(&eaRealParamBlockReasons, pBlock->pcReason);
	}

  #endif

}

static void dynParamBlockFixupDestroy(DynParamBlock *pBlock) {

  #if DYNFX_TRACKPARAMBLOCKS

	int index = eaFind(&eaAllocatedParamBlocks, pBlock);
	if(index != -1) {
		eaRemoveFast(&eaAllocatedParamBlocks, index);
		eaRemoveFast(&eaRealParamBlockReasons, index);

	}

  #endif

}

AUTO_FIXUPFUNC;
TextParserResult fixupDynParamBlock(DynParamBlock* pBlock, enumTextParserFixupType eType, void *pExtraData) {

	switch (eType)
	{
		xcase FIXUPTYPE_POST_STRUCTCOPY:
		{
			dynParamBlockFixupForNewCopy(pBlock);
		}

		xcase FIXUPTYPE_DESTRUCTOR:
		{
			dynParamBlockFixupDestroy(pBlock);
		}

	}
	return PARSERESULT_SUCCESS;
}

DynParamBlock* dynParamBlockCreateEx(const char *reason, int lineNum)
{
	DynParamBlock* pNewParamBlock;
	pNewParamBlock = StructCreate(parse_DynParamBlock);
	pNewParamBlock->bRunTimeAllocated = true;
	pNewParamBlock->eaPassThroughParams = NULL;
	pNewParamBlock->eaRedirectParams = NULL;

  #if DYNFX_TRACKPARAMBLOCKS
	{
		char reasonLine[2048];
		sprintf(reasonLine, "%s:%d", reason, lineNum);
		pNewParamBlock->pcReason = allocAddString(reasonLine);
		eaPush(&eaAllocatedParamBlocks, pNewParamBlock);
		eaPush(&eaRealParamBlockReasons, pNewParamBlock->pcReason);
	}
  #endif

	return pNewParamBlock;
}

#if DYNFX_TRACKPARAMBLOCKS
static int dfxCompareReasons(const char **d1, const char **d2) {
	return strcmp(d1, d2);
}
#endif

AUTO_COMMAND ACMD_CLIENTCMD;
void dfxDumpParamBlockReasons(void) {

  #if DYNFX_TRACKPARAMBLOCKS

	int i = 0;
	StashTable reasonTable;
	StashTableIterator iterator;
	StashElement element;
	char **eaReasons = NULL;

	reasonTable = stashTableCreateWithStringKeys(16, StashDefault);

	// Go through everything and count up allocation locations.
	for(i = 0; i < eaSize(&eaAllocatedParamBlocks); i++) {
		int counter = 0;
		char reasonAmendedStr[2048];
		const char *pCachedStr;

		assert(eaAllocatedParamBlocks[i]->pcReason);

		// DynParamBlocks that are owned by some FX are probably fine as far as memory leak checking is concerned, but
		// we'll show them in the list anyway - just with a tag.
		if(eaAllocatedParamBlocks[i]->bClaimedByFX) {
			sprintf(reasonAmendedStr, "[USED] %s", eaAllocatedParamBlocks[i]->pcReason);
		} else {
			strcpy(reasonAmendedStr, eaAllocatedParamBlocks[i]->pcReason);
		}

		pCachedStr = allocAddString(reasonAmendedStr);

		if(!stashFindInt(reasonTable, pCachedStr, &counter)) {
			counter = 0;
		}

		counter++;
		stashAddInt(reasonTable, pCachedStr, counter, true);
	}

	// Make a sorted list from the contents of the table.
	stashGetIterator(reasonTable, &iterator);
	while(stashGetNextElement(&iterator, &element)) {
		const char *reason = stashElementGetStringKey(element);
		char reasonWithNumber[1024];
		int count = stashElementGetInt(element);
		sprintf(reasonWithNumber, "%10d - %s", count, reason);
		eaPush(&eaReasons, strdup(reasonWithNumber));
	}

	if(eaReasons) {
		eaQSort(eaReasons, dfxCompareReasons);
	}

	for(i = 0; i < eaSize(&eaReasons); i++) {
		printf("%s\n", eaReasons[i]);
		free(eaReasons[i]);
	}

	eaDestroy(&eaReasons);
	stashTableDestroy(reasonTable);

  #endif
}

void dynParamBlockFree( DynParamBlock* pToFree)
{
	if(pToFree) {

		if ( pToFree->bRunTimeAllocated )
		{

		  #if DYNFX_TRACKPARAMBLOCKS

			int index = eaFind(&eaAllocatedParamBlocks, pToFree);
			if(index != -1) {
				eaRemoveFast(&eaAllocatedParamBlocks, index);
				eaRemoveFast(&eaRealParamBlockReasons, index);
			}

		  #endif

			StructDestroy(parse_DynParamBlock, pToFree);
		}
	}
}

static void dynFxCleanupFailedAdd(const DynNode* pTargetRoot, const DynNode* pSourceRoot, DynParamBlock* pParamBlock)
{
	// Const is still useful to prevent altering of the nodes from within the FX system
	// but if they are unmanaged we are responsible for freeing them
	dynNodeAttemptToFreeUnmanagedNode((DynNode*)pTargetRoot);
	dynNodeAttemptToFreeUnmanagedNode((DynNode*)pSourceRoot);

	if ( pParamBlock && pParamBlock->bRunTimeAllocated )
		dynParamBlockFree(pParamBlock);
}

static DynDefineParam *dynFxFindParamFromParent(DynFx *pParent, const char *pcParamName) {	

	if(pParent) {

		int i;
		const DynFxInfo* pParentInfo = GET_REF(pParent->hInfo);
		const DynParamBlock *pParentDefaultParamBlock = &(pParentInfo->paramBlock);
		DynParamBlock *pParentParamBlock = GET_REF(pParent->hParamBlock);

		// Search parent's param block.
		if(pParentParamBlock) {
			for(i = 0; i < eaSize(&pParentParamBlock->eaDefineParams); i++) {
				if(pParentParamBlock->eaDefineParams[i]->pcParamName == pcParamName) {
					return pParentParamBlock->eaDefineParams[i];
				}
			}
		}

		// Fall back to parent's default params.
		if(pParentDefaultParamBlock) {
			for(i = 0; i < eaSize(&pParentDefaultParamBlock->eaDefineParams); i++) {
				if(pParentDefaultParamBlock->eaDefineParams[i]->pcParamName == pcParamName) {
					return pParentDefaultParamBlock->eaDefineParams[i];
				}
			}
		}
	}

	// Didn't find anything.
	return NULL;
}

static char *dynFxExclusionList = NULL;
AUTO_CMD_ESTRING(dynFxExclusionList, dynFxExclusionList) ACMD_COMMANDLINE;

static const char **eaDynFxExclusionList = NULL;

// This just clears the earray for excluded FX. Not the string that populates it. Use it to force the list to repopulate
// after changing the string.
static void dynFxClearInternalExclusionList(void) {
	if(eaDynFxExclusionList) {
		eaDestroy(&eaDynFxExclusionList);
		eaDynFxExclusionList = NULL;
	}
}

AUTO_COMMAND ACMD_CATEGORY(dynFx) ACMD_ACCESSLEVEL(0);
void dynFxSetFXExlusionList(const char *newList) {

	if(dynFxExclusionList) {
		estrDestroy(&dynFxExclusionList);
		dynFxExclusionList = NULL;
	}

	dynFxClearInternalExclusionList();

	estrCopy2(&dynFxExclusionList, newList);
}

AUTO_COMMAND ACMD_CATEGORY(dynFx) ACMD_ACCESSLEVEL(0);
void dynFxExcludeFX(const char *newFx ACMD_NAMELIST("DynFxInfo", RESOURCEINFO)) {

	if(eaDynFxExclusionList) {
		eaDestroy(&eaDynFxExclusionList);
		eaDynFxExclusionList = NULL;
	}

	dynFxClearInternalExclusionList();

	if(dynFxExclusionList && strlen(dynFxExclusionList)) {
		// Concat onto existing.
		estrConcatf(&dynFxExclusionList, ",%s", newFx);
	} else {
		// Create new string.
		estrCopy2(&dynFxExclusionList, newFx);
	}
}

AUTO_COMMAND ACMD_CATEGORY(dynFx) ACMD_ACCESSLEVEL(0);
void dynFxDumpExcludedFX(void) {
	if(dynFxExclusionList && strlen(dynFxExclusionList)) {
		printf("%s\n", dynFxExclusionList);
	}
}

static bool dynFxCheckFXExclusionList(const char *pcFxName) {

	int i ;
	const char *pooledName = allocAddString(pcFxName);

	if(!dynFxExclusionList) return true;

	if(!eaDynFxExclusionList) {

		// The string list is here, but the earray isn't.
		char *context = NULL;
		char *exclusionListCopy = strdup(dynFxExclusionList);
		char *tok = strtok_s(exclusionListCopy, ", ", &context);

		if(tok) {
			do {
				eaPush(&eaDynFxExclusionList, allocAddString(tok));
			} while(tok = strtok_s(NULL, ", ", &context));
		}

		free(exclusionListCopy);
	}

	for(i = 0; i < eaSize(&eaDynFxExclusionList); i++) {
		if(eaDynFxExclusionList[i] == pooledName) {
			return false;
		}
	}

	return true;
}



DynFx* dynAddFx(DynFxManager* pFxManager, const char* pcInfo, DynAddFxParams* pParams)
{
	DynFx* pNewFx;
	DynParamBlock* pParamBlockToUse = pParams->pParamBlock;

  #if DYNFX_TRACKPARAMBLOCKS
	if(pParamBlockToUse) {
		if(pParamBlockToUse->bRunTimeAllocated) {
			assert(pParamBlockToUse->pcReason);
		}
	}
  #endif

	CHECK_FX_COUNT;

	if (dynDebugState.bNoNewFx
		|| !pcInfo
		|| ( dynDebugState.bNoCostumeFX && pParams->eSource == eDynFxSource_Costume )
		|| ( dynDebugState.bNoEnvironmentFX && ( pParams->eSource == eDynFxSource_Environment || pParams->eSource == eDynFxSource_Volume ) )
		|| ( dynDebugState.bNoUIFX && pParams->eSource == eDynFxSource_UI )
		|| !dynFxCheckFXExclusionList(pcInfo)
		)
	{
		dynFxCleanupFailedAdd(pParams->pTargetRoot, pParams->pSourceRoot, pParams->pParamBlock);
		return NULL;
	}

	if (pFxManager->pWorldRegion && !pFxManager->pWorldRegion->fx_region.bInitalized)
	{
		Errorf("Trying to add FX %s to uninitialized world region!", pcInfo);
		dynFxCleanupFailedAdd(pParams->pTargetRoot, pParams->pSourceRoot, pParams->pParamBlock);
		return NULL;
	}



	// We need to square up the pParamBlock with any passed-through params

	if(pParams->pParamBlock && (pParams->pParamBlock->eaPassThroughParams || pParams->pParamBlock->eaRedirectParams))
	{
		DynParamBlock* pParentParamBlock = pParams->pParent ? GET_REF(pParams->pParent->hParamBlock) : NULL;
		const U32 uiNumPassThroughParams = eaUSize(&pParams->pParamBlock->eaPassThroughParams);
		const U32 uiNumRedirectParams = eaUSize(&pParams->pParamBlock->eaRedirectParams);
		U32 uiPassThroughIndex;
		U32 uiRedirectIndex;

		if(uiNumPassThroughParams > 0 || uiNumRedirectParams > 0) {

			pParamBlockToUse = dynParamBlockCopy(pParams->pParamBlock);

		  #if DYNFX_TRACKPARAMBLOCKS

			if(!pParams->pParamBlock->pcReason) {
				assert(!pParams->pParamBlock->bRunTimeAllocated);
				pParamBlockToUse->pcReason = allocAddString("Internal copy");
			}

			assert(pParamBlockToUse->pcReason);

		  #endif

			// Check all redirected parameters for this parent parameter.
			for(uiRedirectIndex = 0; uiRedirectIndex < uiNumRedirectParams; uiRedirectIndex++) {
				DynDefineParam *pParam = dynFxFindParamFromParent(pParams->pParent, pParams->pParamBlock->eaRedirectParams[uiRedirectIndex]->pcParamSrcName);
				if(pParam) {
					pParam = StructClone(parse_DynDefineParam, pParam);
					pParam->pcParamName = pParams->pParamBlock->eaRedirectParams[uiRedirectIndex]->pcParamDstName;
					eaPush(&pParamBlockToUse->eaDefineParams, pParam);
				}
			}

			// Check all passthrough parameters for this parent parameter.
			for(uiPassThroughIndex = 0; uiPassThroughIndex < uiNumPassThroughParams; uiPassThroughIndex++) {
				DynDefineParam *pParam = dynFxFindParamFromParent(pParams->pParent, pParams->pParamBlock->eaPassThroughParams[uiPassThroughIndex]);
				if(pParam) {
					pParam = StructClone(parse_DynDefineParam, pParam);
					eaPush(&pParamBlockToUse->eaDefineParams, pParam);
				}
			}
		}
	}

	if(pParamBlockToUse != pParams->pParamBlock) {
		dynParamBlockFree(pParams->pParamBlock);
	}

	if (pParams->eSource == eDynFxSource_UI)
	{
		pParams->bOverridePriority = true;
		pParams->ePriorityOverride = edpOverride;
	}

	pNewFx = dynFxCreate(pFxManager, pcInfo, pParams->pParent, pParamBlockToUse,
		pParams->pTargetRoot,
		pParams->pSourceRoot,
		(!pParams->pParent&&dynDebugState.bGlobalHueOverride)?dynDebugState.fTestHue:pParams->fHue,
		pParams->fSaturation,
		pParams->fValue,
		pParams->pSortBucket,
		pParams->uiHitReactID,
		pParams->bOverridePriority?pParams->ePriorityOverride:edpNotSet,
		pParams->cbJitterListSelectFunc,
		pParams->pJitterListSelectData);

	if (!pNewFx)
	{

		dynFxCleanupFailedAdd(pParams->pTargetRoot, pParams->pSourceRoot, pParamBlockToUse);
		return NULL;
	}

	pNewFx->eSource = pParams->eSource;

	// If possible, let the parent track us
	if ( pParams->pParent )
	{
		dynFxPushChildFx(pParams->pParent, pParams->pSibling, pNewFx);
	}
	else
	{
		eaPush(&pNewFx->pManager->eaDynFx, pNewFx);
	}
	CHECK_FX_COUNT;

	dynFxUpdate(pFxManager->iPartitionIdx, pNewFx, 1, 1.0f, false, true);

	CHECK_FX_COUNT;
	return pNewFx;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void dynAddFxAtLocationSimple(const char* pcDynFxInfoName, const Mat4 sourceMat, const char* filename)
{
	Quat sourceQuat;
	if (dynDebugState.bNoNewFx)
		return;
	if (!dynFxInfoExists(pcDynFxInfoName))
	{
		ErrorFilenamef(filename, "Can't find fx %s", pcDynFxInfoName);
		return;
	}

	mat3ToQuat(sourceMat, sourceQuat);
	dynAddFxAtLocation(NULL, pcDynFxInfoName, NULL, NULL, sourceMat[3], sourceQuat, NULL, NULL, 0, 0, 0, 0, eDynFxSource_Expression);
}
AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void dynAddFxAtLocationWithTarget(const char* pcDynFxInfoName, const Mat4 sourceMat, const Mat4 targetMat, const char* filename)
{
	Quat sourceQuat;
	Quat targetQuat;
	if (dynDebugState.bNoNewFx)
		return;
	if (!dynFxInfoExists(pcDynFxInfoName))
	{
		ErrorFilenamef(filename, "Can't find fx %s", pcDynFxInfoName);
		return;
	}

	mat3ToQuat(sourceMat, sourceQuat);
	mat3ToQuat(targetMat, targetQuat);
	dynAddFxAtLocation(NULL, pcDynFxInfoName, NULL, NULL, sourceMat[3], sourceQuat, targetMat[3], targetQuat, 0, 0, 0, 0, eDynFxSource_Expression);
}

static StashTable stManagedRemoteFx = NULL;
int dynGetManagedFxGUID(void)
{
	static int guid = 0;
	guid++;
	if(!guid)
		guid++;
	return guid;
}

void dynFxKillManaged(void)
{
	StashTableIterator iter;
	StashElement elem;

	if(!stManagedRemoteFx)
		return;

	stashGetIterator(stManagedRemoteFx, &iter);
	while(stashGetNextElement(&iter, &elem))
	{
		DynFx *pFx = stashElementGetPointer(elem);

		dynFxKill(pFx, true, true, false, eDynFxKillReason_ManualKill);
	}

	stashTableClear(stManagedRemoteFx);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void dynAddFxAtLocationManaged(int guid, const char* pcDynFxInfoName, const Mat4 sourceMat, const char* filename)
{
	DynFx *pFx;
	Quat sourceQuat;
	if (dynDebugState.bNoNewFx)
		return;
	if (!dynFxInfoExists(pcDynFxInfoName))
	{
		ErrorFilenamef(filename, "Can't find fx %s", pcDynFxInfoName);
		return;
	}

	if(stashIntFindPointer(stManagedRemoteFx, guid, NULL))
	{
		ErrorFilenamef(filename, "Trying to add duplicate remote FX");
		return;
	}

	mat3ToQuat(sourceMat, sourceQuat);
	pFx = dynAddFxAtLocation(NULL, pcDynFxInfoName, NULL, NULL, sourceMat[3], sourceQuat, NULL, NULL, 0, 0, 0, 0, eDynFxSource_Expression);

	if(!stManagedRemoteFx)
		stManagedRemoteFx = stashTableCreateInt(10);

	if(pFx)
		stashIntAddPointer(stManagedRemoteFx, guid, pFx, 0);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void dynRemoveManagedFx(int guid)
{
	DynFx *pFx = NULL;

	if(!stManagedRemoteFx)
		return;

	stashIntRemovePointer(stManagedRemoteFx, guid, &pFx);
	dynFxKill(pFx, false, true, false, eDynFxKillReason_ExternalKill);
}

DynFx* dynAddFxFromLocation(const char* pcFxInfo, DynFx* pParent, DynParamBlock* pParamBlock, const DynNode* pTargetRoot, const Vec3 vecSource, const Vec3 vecTarget, const Quat targetQuat, F32 fHue, F32 fSaturationShift, F32 fValueShift, U32 uiHitReactID, eDynFxSource eSource)
{
	DynNode *pSourceNode = dynNodeAlloc();
	pSourceNode->uiUnManaged = 1;
	dynNodeSetPos(pSourceNode, vecSource);
	if (!pTargetRoot && vecTarget)
	{
		DynNode *pNode = dynNodeAlloc();
		pNode->uiUnManaged = 1;
		dynNodeSetPos(pNode, vecTarget);
		if(targetQuat)
			dynNodeSetRot(pNode, targetQuat);
		pTargetRoot = pNode;
	}
	{
		DynAddFxParams params = {0};
		params.pParent = pParent;
		params.pParamBlock = pParamBlock;
		params.pTargetRoot = pTargetRoot;
		params.pSourceRoot = pSourceNode;
		params.fHue        = fHue;
		params.fSaturation = fSaturationShift;
		params.fValue      = fValueShift;

		params.uiHitReactID = uiHitReactID;
		params.eSource = eSource;
		return dynAddFx(dynFxGetGlobalFxManager(vecSource),pcFxInfo,&params);
	}
}

DynFx* dynAddFxAtLocation(DynFxManager* pFxManager, const char* pcFxInfo, DynFx* pParent, DynParamBlock* pParamBlock, const Vec3 vecSource, const Quat sourceQuat, const Vec3 vecTarget, const Quat targetQuat, F32 fHue, F32 fSaturationShift, F32 fValueShift, U32 uiHitReactID, eDynFxSource eSource)
{
	DynNode *pTargetNode = NULL;

	// Set the target node if one was provided
	if(vecTarget)
	{
		pTargetNode = dynNodeAlloc();
		pTargetNode->uiUnManaged = 1;
		dynNodeSetPos(pTargetNode, vecTarget);
		if(targetQuat)
			dynNodeSetRot(pTargetNode, targetQuat);
	}

	if (!pFxManager)
	{
		DynNode *pSourceNode = dynNodeAlloc();
		pSourceNode->uiUnManaged = 1;
		dynNodeSetPos(pSourceNode, vecSource);
		if(sourceQuat)
			dynNodeSetRot(pSourceNode, sourceQuat);

		{
			DynAddFxParams params = {0};
			params.pParent = pParent;
			params.pParamBlock = pParamBlock;
			params.pTargetRoot = pTargetNode;
			params.pSourceRoot = pSourceNode;
			params.fHue = fHue;
			params.fSaturation = fSaturationShift;
			params.fValue = fValueShift;

			params.uiHitReactID = uiHitReactID;
			params.eSource = eSource;
			return dynAddFx(dynFxGetGlobalFxManager(vecSource),pcFxInfo,&params);
		}
	}
	else
	{
		DynAddFxParams params = {0};
		params.pParent = pParent;
		params.pParamBlock = pParamBlock;
		params.pTargetRoot = pTargetNode;
		params.fHue = fHue;
		params.fSaturation = fSaturationShift;
		params.fValue = fValueShift;

		params.uiHitReactID = uiHitReactID;
		params.eSource = eSource;
		return dynAddFx(pFxManager, pcFxInfo,&params);
	}
}

static void dynFxDebrisManagerUpdateDebrisCounts(DynFxManager* pDebrisManager)
{
	Vec3 vCamLoc;
	dynNodeGetWorldSpacePos(dynCameraNodeGet(), vCamLoc);

	FOR_EACH_IN_EARRAY(pDebrisManager->eaDynFx, DynFx, pFx)
		if (!pFx->bKill)
		{
			if (dynDebugState.uiNumDebris >= uiMaxDebris)
				dynFxKill(pFx, false, true, false, eDynFxKillReason_DebrisCountExceeded);
			else 
			{
				DynNode* pNode = dynFxGetNode(pFx);
				if (!pNode)
					dynFxKill(pFx, false, true, false, eDynFxKillReason_DebrisNoNode);
				else
				{
					Vec3 vLoc;
					dynNodeGetWorldSpacePos(pNode, vLoc);
					subVec3(vCamLoc, vLoc, vLoc);
					if (lengthVec3Squared(vLoc) > fMaxDebrisDistance*fMaxDebrisDistance)
					{
						dynFxKill(pFx, false, true, false, eDynFxKillReason_DebrisDistanceExceeded);
					}
					else
						++dynDebugState.uiNumDebris;
				}
			}
		}
	FOR_EACH_END
}

void dynFxKillFxAfterUpdate(SA_PARAM_NN_VALID DynFx* pFx)
{
	if (pFx->pManager)
	{
		eaPush(&pFx->pManager->eaFxToKill, dynFxReferenceCreate(pFx));
	}
}

void dynFxManagerUpdate(SA_PARAM_NN_VALID DynFxManager* pFxManager, DynFxTime uiDeltaTime )
{
	int iIndex;
	int iNumFx = eaSize(&pFxManager->eaDynFx);
	dynFxManUpdateAutoRetryFX(pFxManager);
	dynFxManUpdateMaintainedFX(pFxManager);

	if (pFxManager->bSplatsInvalid)
		--pFxManager->bSplatsInvalid;

	for (iIndex=0; iIndex<iNumFx; ++iIndex)
	{
		DynFx* pFx = pFxManager->eaDynFx[iIndex];
		if (!dynFxUpdate(pFxManager->iPartitionIdx, pFx, uiDeltaTime, 1.0f, false, false))
		{
			// false means delete this seq
			// remove it too
			eaRemove(&pFxManager->eaDynFx, iIndex);

			dynFxDeleteOrDebris(pFx);

			// fix for loop
			iNumFx = eaSize(&pFxManager->eaDynFx);
			--iIndex;
			CHECK_FX_COUNT;
		}
	}

	FOR_EACH_IN_EARRAY(pFxManager->eaFxToKill, DynFxRef, pRef)
	{
		DynFx* pFx = GET_REF(pRef->hDynFx);
		if (pFx)
			dynFxKill(pFx, true, true, false, eDynFxKillReason_RequiresNode);
	}
	FOR_EACH_END;
	eaDestroyEx(&pFxManager->eaFxToKill, dynFxReferenceFree);

	dynFxManUpdateSuppressors(pFxManager, FLOATTIME(uiDeltaTime));
	dynFxManUpdateIKTargets(pFxManager);

}

void dynFxDeleteOrDebris(DynFx* pFx)
{
	if (pFx->bDebris && uiMaxDebris > 0)
	{
		DynFxManager* pDebrisManager = pFx->pManager?dynFxGetDebrisFxManager(pFx->pManager):NULL;
		if (pDebrisManager && (pFx->pManager != pDebrisManager || GET_REF(pFx->hParentFx)))
		{
			REMOVE_HANDLE(pFx->hParentFx);
			eaPush(&pDebrisManager->eaDynFx, pFx);
			if (pFx->pManager != pDebrisManager)
			{
				dynFxChangeManager(pFx, pDebrisManager);
				if (pFx->iDrawArrayIndex < 0)
				{
					dynFxUpdateGrid(pFx);
				}
			}
		}
		else
			dynFxDelete(pFx, false);
	}
	else
		dynFxDelete(pFx, false);

}

static void dynFxForEachParticleHelper(DynFx* pFx, DynParticleForEachFunc callback, DynFastParticleForEachFunc fastcallback, void *user_data)
{
	DynFxInfo *pInfo = GET_REF(pFx->hInfo);
	if( pInfo && !pInfo->bDontDraw )
	{
		DynParticle* pParticle = dynFxGetParticle( pFx );
		if( pParticle )
		{
			callback( pParticle, user_data );
			FOR_EACH_IN_EARRAY(pParticle->eaParticleSets, DynFxFastParticleSet, pSet)
				fastcallback(pSet, user_data);
			FOR_EACH_END
		}
	}
	FOR_EACH_IN_EARRAY(pFx->eaChildFx, DynFx, pChildFx)
		dynFxForEachParticleHelper(pChildFx, callback, fastcallback, user_data);
	FOR_EACH_END
}


void dynFxManForEachParticle(DynFxManager* pFxManager, DynParticleForEachFunc callback, DynFastParticleForEachFunc fastcallback, void *user_data)
{
	FOR_EACH_IN_EARRAY(pFxManager->eaDynFx, DynFx, pFx)
		dynFxForEachParticleHelper(pFx, callback, fastcallback, user_data);
	FOR_EACH_END
}

const char* dynPriorityLookup(int iValue)
{
	return StaticDefineIntRevLookup(eDynPriorityEnum, iValue);
}

const char* dynFxTypeLookup(int iValue)
{
	return StaticDefineIntRevLookup(eDynFxTypeEnum, iValue);
}


// Takes a value from 1 to 3. Sets the maximum Priority level that will be drawn. Useful for turning off Priority 3 or Priority 2 FX to see how they look without those FX.
AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxMaxPriority(const char* pcPri ACMD_NAMELIST(eDynPriorityEnum, STATICDEFINE) )
{
	eDynPriority ePri = StaticDefineIntGetInt(eDynPriorityEnum, pcPri);

	dynDebugState.uiMaxPriorityDrawn = CLAMP(ePri, 0, 3);

}


// Sets the absolute maximum number of FX objects (sprites, meshes, mesh-trails, or fast particle SYSTEMS) that will be drawn in any given frame. Defaults to 150 currently.
AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxMaxDrawn(U32 uiNum)
{
	dynDebugState.uiMaxDrawn = CLAMP(uiNum, 0, 1000);
}

#define NUM_FX_CATEGORIES 8

#define MAX_FX_SORT_OBJECTS 1000
typedef struct DynFxDrawObject
{
	F32 fDistFromCamSqr;
	F32 fAlpha;
	union
	{
		DynFx* pFx;
		DynFxFastParticleSet* pSet;
	};
	U8 uiLevel;
	bool bFastParticleSet;
} DynFxDrawObject;

static DynFxDrawObject drawObjects[MAX_FX_SORT_OBJECTS];
static U32 uiNumDrawObjects = 0;

static DynFxDrawObject debrisDrawObjects[MAX_DEBRIS];
static U32 uiNumDebrisDrawObjects = 0;

static DynFxDrawObject* getDrawObject(bool bDebris)
{
	if (bDebris)
	{
		if (uiNumDebrisDrawObjects < MAX_DEBRIS)
			return &debrisDrawObjects[uiNumDebrisDrawObjects++];
		return NULL;
	}

	if (uiNumDrawObjects < MAX_FX_SORT_OBJECTS)
		return &drawObjects[uiNumDrawObjects++];
	return NULL;
}


bool dynParticleCheckDistance(const DynFxInfo* pInfo, DynParticle* pParticle, F32* pfDistSqr, Frustum* pFrustum)
{
	if ( pInfo && (!pInfo->bDontDraw || pParticle->pDraw->pDynLight) )
	{
		Vec3 vPos, vDist;
		dynNodeGetWorldSpacePos(&pParticle->pDraw->node, vPos);
		subVec3(vPos, pFrustum->cammat[3], vDist);
		*pfDistSqr = lengthVec3Squared(vDist);

		if (pInfo->fDrawDistance > 0.0f || pInfo->fMinDrawDistance > 0.0f )
		{
			// Do a distance check
			F32 fDrawDistSqr = SQR(pInfo->fDrawDistance);

			if ( (pInfo->fDrawDistance > 0.0f && *pfDistSqr > SQR(pInfo->fDrawDistance) ) || ( pInfo->fMinDrawDistance > 0.0f && *pfDistSqr < SQR(pInfo->fMinDrawDistance) ) ) {
				pParticle->fFadeOut = 0.0f;
				return false;
			} else {
				if (pInfo->fDrawDistance > 0.0f)
				{
					F32 fFadeDistSqr = SQR(pInfo->fFadeDistance);
					if (*pfDistSqr > fFadeDistSqr)
					{
						pParticle->fFadeOut *= calcInterpParam(sqrtf(*pfDistSqr), pInfo->fDrawDistance, pInfo->fFadeDistance);
					}
				}
				if (pInfo->fMinDrawDistance > 0.0f)
				{
					F32 fMinFadeDistSqr = SQR(pInfo->fMinFadeDistance);
					if (*pfDistSqr < fMinFadeDistSqr)
					{
						pParticle->fFadeOut *= calcInterpParam(sqrtf(*pfDistSqr), pInfo->fMinDrawDistance, pInfo->fMinFadeDistance);
					}
				}
			}
		}
		return !pInfo->bDontDraw;
	}
	return false;
}

bool dynFastParticleSetCheckDistance(DynFxFastParticleSet* pSet, F32* pfDistSqr, Frustum* pFrustum)
{
	Vec3 vDist;
	subVec3(pSet->vPos, pFrustum->cammat[3], vDist);
	*pfDistSqr = lengthVec3Squared(vDist);

	if (pSet->fDrawDistance > 0.0f || pSet->fMinDrawDistance > 0.0f)
	{
		// Do a distance check
		F32 fDrawDistSqr = SQR(pSet->fDrawDistance);
		if ( (pSet->fDrawDistance > 0.0f && *pfDistSqr > SQR(pSet->fDrawDistance) ) || ( pSet->fMinDrawDistance > 0.0f && *pfDistSqr < SQR(pSet->fMinDrawDistance) ) )
			return false;
		else
		{
			if (pSet->fDrawDistance > 0.0f)
			{
				F32 fFadeDistSqr = SQR(pSet->fFadeDistance);
				if (*pfDistSqr > fFadeDistSqr)
				{
					pSet->fSystemAlpha *= calcInterpParam(sqrtf(*pfDistSqr), pSet->fDrawDistance, pSet->fFadeDistance);
				}
			}
			if (pSet->fMinDrawDistance > 0.0f)
			{
				F32 fMinFadeDistSqr = SQR(pSet->fMinFadeDistance);
				if (*pfDistSqr < fMinFadeDistSqr)
				{
					pSet->fSystemAlpha *= calcInterpParam(sqrtf(*pfDistSqr), pSet->fMinDrawDistance, pSet->fMinFadeDistance);
				}
			}
		}
	}
	return true;
}

int dynParticleCategorizeHelper( int iPriorityLevel, eFxManagerType eType )
{
	int iLevel = NUM_FX_CATEGORIES - 1;

	// Check for debugging overrides
	if (dynDebugState.uiMaxPriorityDrawn < 3 && ((U32)(iPriorityLevel) > dynDebugState.uiMaxPriorityDrawn) )
	{
		return -1; // do not bother to add it
	}

	if (iPriorityLevel == 0)
	{
		iLevel = 0;
	}
	else if (eType == eFxManagerType_Player)
	{
		iLevel = 1;
	}
	else
	{
		if (eType == eFxManagerType_Entity)
		{
			switch (iPriorityLevel)
			{
				xcase 1:
					iLevel = 1;
				xcase 2:
					iLevel = 3;
				xcase 3:
				default:
					iLevel = 4;
			}
		}
		else // treat all non-entities (usually static world or global)
		{
			switch (iPriorityLevel)
			{
				xcase 1:
					iLevel = 3;
				xcase 2:
					iLevel = 5;
				xcase 3:
				default:
					iLevel = 6;
			}
		}
	}
	return iLevel;
}

void dynFastParticleSetCategorize(DynFxFastParticleSet* pSet, eFxManagerType eType, Frustum* pFrustum, F32 fAlpha)
{
	int iLevel = dynParticleCategorizeHelper(pSet->iPriorityLevel, eType);
	if (iLevel >= 0)
	{
		F32 fDistFromCamSqr;
		if (dynFastParticleSetCheckDistance(pSet, &fDistFromCamSqr, pFrustum))
		{
			DynFxDrawObject* pObject = getDrawObject(false);
			if (pObject)
			{
				pObject->fDistFromCamSqr = fDistFromCamSqr;
				pObject->bFastParticleSet = true;
				pObject->uiLevel = (U8)iLevel;
				pObject->pSet = pSet;
				pObject->fAlpha = fAlpha;
			}
		}
	}
}

static void dynPropagateZeroAlphaRecurse(DynFx *pFx, bool bZeroAlpha)
{
	FOR_EACH_IN_EARRAY(pFx->eaChildFx, DynFx, pChildFx)
	{
		pChildFx->bZeroAlpha = bZeroAlpha;
		dynPropagateZeroAlphaRecurse(pChildFx, bZeroAlpha);
	}
	FOR_EACH_END;
}

void dynParticleCategorize( DynFx* pFx, Frustum* pFrustum)
{
	F32 fAlpha = 1.0f;
	if(pFx->bZeroAlpha)
		fAlpha = 0.0f;
	else
	{
		const DynFxInfo* pInfo = GET_REF(pFx->hInfo);
		if (pFx->pManager && pFx->pManager->pDrawSkel && pInfo && !pInfo->bNoAlphaInherit) {
			fAlpha = pFx->pManager->pDrawSkel->fTotalAlpha;
			if(pInfo->bUseSkeletonGeometryAlpha) {
				fAlpha *= pFx->pManager->pDrawSkel->fGeometryOnlyAlpha;
			}
		}

		//If we're pulling a geo from a costume and that bone is hidden, hide our particles.
		if (pFx->pParticle && pFx->pParticle->pDraw->pcBoneForCostumeModelGrab && pFx->pManager && pFx->pManager->pDrawSkel)
		{
			if (eaFind(&pFx->pManager->pDrawSkel->eaHiddenBoneVisSetBones, pFx->pParticle->pDraw->pcBoneForCostumeModelGrab) >= 0)
				fAlpha = 0.0f;
		}

		if (pInfo && pInfo->pcSuppressionTag && pFx->pManager && eaSize(&pFx->pManager->eaSuppressors) > 0)
		{
			FOR_EACH_IN_EARRAY(pFx->pManager->eaSuppressors, DynFxSuppressor, pSuppressor)
			{
				if (pSuppressor->pcTag == pInfo->pcSuppressionTag)
					fAlpha *= (1.0f - pSuppressor->fAmount);
			}
			FOR_EACH_END;
		}

		if (pInfo->bPropagateZeroAlpha)
			dynPropagateZeroAlphaRecurse(pFx, fAlpha <= 0.0f);
	}
	if (fAlpha <= 0.0f)
		return;
	if (pFx->pParticle)
	{
		F32 fDistFromCamSqr;
		int iLevel = pFx->b2D?0:dynParticleCategorizeHelper(pFx->iPriorityLevel, pFx->pManager->eType);
		if (iLevel >= 0)
		{
			if (dynParticleCheckDistance(GET_REF(pFx->hInfo), pFx->pParticle, &fDistFromCamSqr, pFrustum))
			{
				DynFxDrawObject* pObject = getDrawObject(pFx->bDebris);
				if (pObject)
				{
					pObject->fDistFromCamSqr = fDistFromCamSqr;
					pObject->bFastParticleSet = false;
					pObject->uiLevel = (U8)iLevel;
					pObject->pFx = pFx;
					pObject->fAlpha = fAlpha;
				}
			}
		}
		FOR_EACH_IN_EARRAY(pFx->pParticle->eaParticleSets, DynFxFastParticleSet, pSet)
		{
			dynFastParticleSetCategorize(pSet, pFx->pManager->eType, pFrustum, fAlpha);
		}
		FOR_EACH_END;
	}
}

static int cmpDrawObject(const DynFxDrawObject* a, const DynFxDrawObject* b)
{
	F32 fDist;
	// First look at level: lower level is higher priority, so it comes first (-1)
	if (a->uiLevel > b->uiLevel)
		return 1;
	else if (a->uiLevel < b->uiLevel)
		return -1;

	// Look at distance
	fDist = a->fDistFromCamSqr - b->fDistFromCamSqr;
	if (fDist == 0.0f || !FINITE(fDist))
		return 0;
	return SIGN(fDist);
}


static void dynFxQueue( DynFx* pFx, WLDynDrawParams* params, DynParticleForEachFunc callback, F32 fAlpha) 
{
	DynFxInfo* pInfo = GET_REF(pFx->hInfo);
	DynParticle* pParticle = dynFxGetParticle(pFx);
	Vec3 vPos;

#if !PLATFORM_CONSOLE
	if (pInfo && pInfo->bDebugFx && dynDebugState.bBreakOnDebugFx)
	{
		_DbgBreak(); // Should break if you have a debugger attached.
	}
#endif
	if (pFx->pManager)
	{
		params->splats_invalid |= pFx->pManager->bSplatsInvalid;
	}
	pParticle->fFadeOut *= fAlpha;
	params->is_costume = (pFx->eSource == eDynFxSource_Costume);
	params->is_debris = pFx->bDebris;
	params->is_screen_space = pFx->b2D;
	params->pInfo = pInfo;
	params->iPriorityLevel = pFx->iPriorityLevel;

	// For instancing
	params->pDrawableList = pFx->pDrawableList;
	params->pInstanceParamList = pFx->pInstanceParamList;
	params->iDrawableResetCounter = pFx->iDrawableResetCounter;
	params->sort_bucket = pFx->pSortBucket;

	dynNodeGetWorldSpacePos(&pParticle->pDraw->node, vPos);
	if(!FINITEVEC3(vPos)) {

		const char *fxName = "unknown FX";
		if(pInfo) {
			fxName = pInfo->pcDynName;
		}

		Errorf("Particle from %s was at a non-finite position at queue time. Killing the FX.", fxName);
		dynFxKill(pFx, false, false, false, eDynFxKillReason_Error);

	} else {
	
		callback(pParticle, params);

	}

	params->pDrawableList = NULL;
	params->pInstanceParamList = NULL;
}

static bool dynFxDrawObjectIsSevered(DynFxDrawObject *pDrawObject) {

	bool bSeverWithBone = false;
	DynNode *pNode = NULL;
	DynDrawSkeleton *pDrawSkel = NULL;

	if(pDrawObject->bFastParticleSet) {
		DynFx *pFx = GET_REF(pDrawObject->pSet->hParentFX);
		if(pFx) {
			if(pFx->pCurrentParentBhvr && pFx->pManager && pFx->pParticle) {
				bSeverWithBone = pFx->pCurrentParentBhvr->bSeverWithBone && pFx->pManager->pDrawSkel;
				pNode = &pFx->pParticle->pDraw->node;
				pDrawSkel = pFx->pManager->pDrawSkel;
			}
		}
	} else {
		if(pDrawObject->pFx && pDrawObject->pFx->pCurrentParentBhvr && pDrawObject->pFx->pManager && pDrawObject->pFx->pParticle) {
			bSeverWithBone = pDrawObject->pFx->pCurrentParentBhvr->bSeverWithBone && pDrawObject->pFx->pManager->pDrawSkel;
			pNode = &pDrawObject->pFx->pParticle->pDraw->node;
			pDrawSkel = pDrawObject->pFx->pManager->pDrawSkel;
		}
	}

	if(bSeverWithBone) {
		while(pNode) {
			int i;
			for(i = 0; i < eaSize(&pDrawSkel->eaSeveredBones); i++) {
				if(pDrawSkel->eaSeveredBones[i] == pNode->pcTag) {
					return true;
				}
			}
			pNode = pNode->pParent;
		}
	}

	return false;
}

void dynParticleForEachInFrustum(DynParticleForEachFunc callback, DynFastParticleForEachFunc fastcallback, WorldRegion* pWorldRegion, WLDynDrawParams *params, Frustum* pFrustum, SparseGridOcclusionCallback occlusionFunc, GfxOcclusionBuffer *pOcclusionBuffer)
{
	DynFxRegion* pFxRegion = pWorldRegion?&pWorldRegion->fx_region:NULL;
	U32 uiDrawObject;
	if (!pFxRegion)
		return;

	// ADD Z OCCLUSION!!!

	PERFINFO_AUTO_START_FUNC();

	FOR_EACH_IN_EARRAY_FORWARDS(pFxRegion->eaFXToDraw, DynFx, pFXToDraw)
	{
		if (!pFXToDraw) // null pointers are fx that have been removed from the draw list due to deletion
			continue;
		assert(pFXToDraw->iDrawArrayIndex == ipFXToDrawIndex);
		dynParticleCategorize(pFXToDraw, pFrustum);
	}
	FOR_EACH_END;
	eaClearEx(&pFxRegion->eaFXToDraw, dynFxClearDrawArrayIndex);



	FOR_EACH_IN_EARRAY(pFxRegion->eaOrphanedSets, DynFxFastParticleSet, pSet)
	{
		dynFastParticleSetCategorize(pSet, eFxManagerType_Entity, pFrustum, 1.0f);
	}
	FOR_EACH_END;


	// Sort the non-debris list
	qsortG(&drawObjects, uiNumDrawObjects, sizeof(DynFxDrawObject), cmpDrawObject);

	// Queue all non-debris objects
	for (uiDrawObject=0; uiDrawObject<uiNumDrawObjects; ++uiDrawObject)
	{
		DynFxDrawObject* pDrawObject = &drawObjects[uiDrawObject];
		DynFx *pParent = GET_REF(pDrawObject->pSet->hParentFX);

		if (dynDebugState.frameCounters.uiNumDrawnFx < dynDebugState.uiMaxDrawn || SAFE_MEMBER(pParent, iPriorityLevel) == edpOverride)
		{
			if(!dynFxDrawObjectIsSevered(pDrawObject))
			{

				if (pDrawObject->bFastParticleSet)
				{
					pDrawObject->pSet->fSystemAlpha *= pDrawObject->fAlpha;
					params->is_costume = SAFE_MEMBER(pParent, eSource) == eDynFxSource_Costume;
					params->is_debris = false;
					params->is_screen_space = pDrawObject->pSet->b2D;

					fastcallback(pDrawObject->pSet, params);
				}
				else // normal fx
					dynFxQueue(pDrawObject->pFx, params, callback, pDrawObject->fAlpha);
			}
		}
	}

	// Queue all debris objects
	for (uiDrawObject=0; uiDrawObject<uiNumDebrisDrawObjects; ++uiDrawObject)
	{
		dynFxQueue(debrisDrawObjects[uiDrawObject].pFx, params, callback, debrisDrawObjects[uiDrawObject].fAlpha);
	}

	// "Clear" draw object arrays
	uiNumDrawObjects = uiNumDebrisDrawObjects = 0;

	PERFINFO_AUTO_STOP_FUNC();
}

static void dynFxRemoveIfNameMatches( DynFx*** peaDynFx, const char* pcDynFxName, bool bImmediate) 
{
	U32 uiNumDynFxs = eaSize(peaDynFx);
	U32 uiIndex;

	for (uiIndex=0; uiIndex<uiNumDynFxs; ++uiIndex)
	{
		DynFx* pFx = (*peaDynFx)[uiIndex];
		// Remove any children
		dynFxRemoveIfNameMatches(&pFx->eaChildFx, pcDynFxName, bImmediate);

		if ( dynFxStopIfNameMatches(pFx, pcDynFxName, bImmediate ) )
		{
			eaRemove(peaDynFx, uiIndex);

			// fix for loop
			--uiNumDynFxs;
			--uiIndex;
		}
	}
}



static void dynFxManForEachFx(DynFxManager* pFxMan, DynFxOperatorFunc func, void* pUserData)
{
	FOR_EACH_IN_EARRAY(pFxMan->eaDynFx, DynFx, pFx)
		func(pFx, pUserData);
	FOR_EACH_END
}

static void dynFxRegionForEachFx(DynFxRegion* pFxRegion, DynFxOperatorFunc func, void* pUserData)
{
	FOR_EACH_IN_EARRAY(pFxRegion->eaDynFxManagers, DynFxManager, pFxMan)
		dynFxManForEachFx(pFxMan, func, pUserData);
	FOR_EACH_END

		// Just in case
		if (pFxRegion->pGlobalFxManager)
			dynFxManForEachFx(pFxRegion->pGlobalFxManager, func, pUserData);

	if (pFxRegion->pDebrisFxManager)
		dynFxManForEachFx(pFxRegion->pDebrisFxManager, func, pUserData);
}

void worldRegionClearGrid(WorldRegion* pRegion)
{
	DynFxRegion* pFxRegion = &pRegion->fx_region;
	FOR_EACH_IN_EARRAY(pFxRegion->eaFXToDraw, DynFx, pFx)
		if(pFx) {
			pFx->iDrawArrayIndex = -1;
		}
	FOR_EACH_END;
	eaClear(&pFxRegion->eaFXToDraw);
}

void dynFxForEachFx(DynFxOperatorFunc func, void* pUserData)
{
	WorldRegion** eaWorldRegions = worldGetAllWorldRegions();
	FOR_EACH_IN_EARRAY(eaWorldRegions, WorldRegion, pWorldRegion)
		dynFxRegionForEachFx(&pWorldRegion->fx_region, func, pUserData);
	FOR_EACH_END;
	FOR_EACH_IN_EARRAY(eaOrphanedFxManagers, DynFxManager, pFxManager)
		dynFxManForEachFx(pFxManager, func, pUserData);
	FOR_EACH_END;
    if (pUiFxManager2D)
        dynFxManForEachFx(pUiFxManager2D, func, pUserData);
    if (pUiFxManager3D)
        dynFxManForEachFx(pUiFxManager3D, func, pUserData);
}

void dynFxForEachChild(DynFx* pFx, DynFxOperatorFunc func, void* pUserData)
{
	FOR_EACH_IN_EARRAY(pFx->eaChildFx, DynFx, pChildFx)
		func(pChildFx, pUserData);
	FOR_EACH_END;
}

void dynFxKillFunc(DynFx* pFxToKill, void* pUnused)
{
	dynFxKill(pFxToKill, true, true, false, eDynFxKillReason_ManualKill);
}

void dynFxKillAllFx(void)
{
	dynFxForEachFx(dynFxKillFunc, NULL);
}

void dynFxTestExclusionTagFunc(DynFx* pFxToTest, const char* pcExclusionTag)
{
	DynFxInfo* pInfo = GET_REF(pFxToTest->hInfo);
	if (pInfo && pInfo->pcExclusionTag == pcExclusionTag)
		dynFxKill(pFxToTest, true, true, false, eDynFxKillReason_ManualKill);
}

void dynFxTestExclusionTagForAll(const char* pcExclusionTag)
{
	dynFxForEachFx(dynFxTestExclusionTagFunc, (void*)pcExclusionTag);
}


void dynFxManStopUsingFxInfo(DynFxManager* pFxManager, const char* pcDynFxName, bool bImmediate)
{
	if (pFxManager)
	{
		DynFx*** peaDynFx = &pFxManager->eaDynFx;
		dynFxRemoveIfNameMatches(peaDynFx, pcDynFxName, bImmediate);
	}
	else
	{
		WorldRegion** eaWorldRegions = worldGetAllWorldRegions();
		FOR_EACH_IN_EARRAY(eaWorldRegions, WorldRegion, pWorldRegion)
			FOR_EACH_IN_EARRAY(pWorldRegion->fx_region.eaDynFxManagers, DynFxManager, pFxMan)
				dynFxRemoveIfNameMatches(&pFxMan->eaDynFx, pcDynFxName, bImmediate);
			FOR_EACH_END;
			dynFxRemoveIfNameMatches(&pWorldRegion->fx_region.pGlobalFxManager->eaDynFx, pcDynFxName, bImmediate);
			dynFxRemoveIfNameMatches(&pWorldRegion->fx_region.pDebrisFxManager->eaDynFx, pcDynFxName, bImmediate);
		FOR_EACH_END;
		if(pUiFxManager2D){
			dynFxRemoveIfNameMatches(&pUiFxManager2D->eaDynFx, pcDynFxName, bImmediate);
		}
		if(pUiFxManager3D){
			dynFxRemoveIfNameMatches(&pUiFxManager3D->eaDynFx, pcDynFxName, bImmediate);
		}
	}
}

// temporary struct for changing regions
typedef struct RegionChange
{
	DynFxManager* pFxManager;
	WorldRegion* pNewRegion;
} RegionChange;

/*
static void dynFxVerifyAllManagers(void)
{
	WorldRegion** eaWorldRegions = worldGetAllWorldRegions();
	StashTable stFxManagers = stashTableCreateAddress(128);
	FOR_EACH_IN_EARRAY(eaWorldRegions, WorldRegion, pWorldRegion)
		if (pWorldRegion)
		{
			FOR_EACH_IN_EARRAY(pWorldRegion->fx_region.eaDynFxManagers, DynFxManager, pFxManager)
				assert(stashAddressAddInt(stFxManagers, pFxManager, 1, false)); // check uniqueness
				assert(eaSize(&pFxManager->eaDynFx) > -1);
			FOR_EACH_END;
		}
	FOR_EACH_END;
	stashTableDestroy(stFxManagers);
}
*/

void dynFxManUpdateAll(F32 fDeltaTime)
{
	DynFxTime dt = DYNFXTIME(fDeltaTime);
	DynFxTime uiClampedDT = MIN(dt, DYNFXTIME(2.0f));
	DynFxTime uiDeltaTimeToUse;
	WorldRegion** eaWorldRegions = worldGetAllWorldRegions();
	PERFINFO_AUTO_START_FUNC();
	dynDebugState.uiNumFxSoundStarts = dynDebugState.uiNumFxSoundMoves = dynDebugState.uiNumFastParticles = 0;

	dynDebugState.bTooManyFastParticlesEnvironment = false;
	dynDebugState.bTooManyFastParticles = false;


	// Only clamp fx system in dev mode and if you are not focused on game, for perf. reasons, for now
	if (isDevelopmentMode())
		uiDeltaTimeToUse = uiClampedDT;
	else
		uiDeltaTimeToUse = dt;

	if (dynDebugState.bRecordFXProfile)
		uiDeltaTimeToUse = DYNFXTIME(1/60.0f); // force 60FPS for fx recording

	dynCameraNodeUpdate();

	if (wl_state.dfx_sky_volume_once_per_frame_func)
		wl_state.dfx_sky_volume_once_per_frame_func();

	// First, check out the orphaned fx managers
	{
		FOR_EACH_IN_EARRAY(eaOrphanedFxManagers, DynFxManager, pOrphanedManager)
		{
			if (pOrphanedManager->eType != eFxManagerType_Headshot)
			{
				Vec3 vPos;
				WorldRegion* pNewRegion;
				dynNodeGetWorldSpacePos(pOrphanedManager->pNode, vPos);
				pNewRegion = worldGetWorldRegionByPos(vPos);
				if (pNewRegion)
				{
					eaPush(&pNewRegion->fx_region.eaDynFxManagers, pOrphanedManager);
					eaForEach(&pOrphanedManager->eaDynFx, dynFxRemoveFromGridRecurse); // make sure the fx in this manager are no longer attached to the old region
					pOrphanedManager->pWorldRegion = pNewRegion;
					eaRemoveFast(&eaOrphanedFxManagers, ipOrphanedManagerIndex);
				}
			}
		}
		FOR_EACH_END;
	}
	// Second, see if any fx managers have changed regions
	{
		RegionChange** eaRegionChanges = NULL; // we store the changes, to avoid earray issues with removing and adding during iteration, and to make sure we only visit each manager once
		FOR_EACH_IN_EARRAY(eaWorldRegions, WorldRegion, pWorldRegion)
			FOR_EACH_IN_EARRAY(pWorldRegion->fx_region.eaDynFxManagers, DynFxManager, pFxManager)
			if (!pFxManager->bPermanentRegion)
			{
				Vec3 vPos;
				WorldRegion* pNewRegion;
				dynNodeGetWorldSpacePos(pFxManager->pNode, vPos);
				pNewRegion = worldGetWorldRegionByPos(vPos);
				if (pNewRegion != pFxManager->pWorldRegion)
				{
					RegionChange* pRegionChange = ScratchAlloc(sizeof(RegionChange));
					eaPush(&eaRegionChanges, pRegionChange);
					pRegionChange->pFxManager = pFxManager;
					pRegionChange->pNewRegion = pNewRegion;
					pFxManager->bRegionChangedThisFrame = true;
				}
				else
					pFxManager->bRegionChangedThisFrame = false;

			}
			FOR_EACH_END;
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY(eaRegionChanges, RegionChange, pRegionChange)
			eaFindAndRemoveFast(&pRegionChange->pFxManager->pWorldRegion->fx_region.eaDynFxManagers, pRegionChange->pFxManager);
			eaPush(&pRegionChange->pNewRegion->fx_region.eaDynFxManagers, pRegionChange->pFxManager);
			eaForEach(&pRegionChange->pFxManager->eaDynFx, dynFxRemoveFromGridRecurse); // make sure the fx in this manager are no longer attached to the old region
			pRegionChange->pFxManager->pWorldRegion = pRegionChange->pNewRegion;
			ScratchFree(pRegionChange);
		FOR_EACH_END;
		eaDestroy(&eaRegionChanges);
	}

	if ( dynDebugState.fDynFxRate )
	{
		dynFxClearAlienColors();

		FOR_EACH_IN_EARRAY(eaWorldRegions, WorldRegion, pWorldRegion)
			eaClearEx(&pWorldRegion->fx_region.eaFXToDraw, dynFxClearDrawArrayIndex);
			dynFxManagerUpdate(pWorldRegion->fx_region.pDebrisFxManager, uiDeltaTimeToUse);
			dynFxManagerUpdate(pWorldRegion->fx_region.pGlobalFxManager, uiDeltaTimeToUse);
			if (pWorldRegion->fx_region.eaDynFxManagers)
			{
				FOR_EACH_IN_EARRAY(pWorldRegion->fx_region.eaDynFxManagers, DynFxManager, pFxManager)
					if (!dynFxManDoesntSelfUpdate(pFxManager))
						dynFxManagerUpdate(pFxManager, uiDeltaTimeToUse);
				FOR_EACH_END;
			}
		FOR_EACH_END;
		dynFxManagerUpdate(pUiFxManager3D, uiDeltaTimeToUse);
		dynFxManagerUpdate(pUiFxManager2D, uiDeltaTimeToUse);

		dynFxDriveAlienColor();
	}


	dynDebugState.uiNumDebris = 0;
	FOR_EACH_IN_EARRAY(eaWorldRegions, WorldRegion, pWorldRegion)
		dynFxDebrisManagerUpdateDebrisCounts(pWorldRegion->fx_region.pDebrisFxManager);
		eaDestroyEx(&pWorldRegion->fx_region.eaForcePayloads[pWorldRegion->fx_region.uiCurrentPayloadArray], NULL);
		eaDestroyEx(&pWorldRegion->fx_region.eaNearMessagePayloads[pWorldRegion->fx_region.uiCurrentPayloadArray], NULL);
		pWorldRegion->fx_region.uiCurrentPayloadArray = !pWorldRegion->fx_region.uiCurrentPayloadArray;
	FOR_EACH_END;

	if (wl_state.dfx_screen_shake_func && gfScreenShakeMagnitude > 0.0f)
	{
		wl_state.dfx_screen_shake_func(0.01f, gfScreenShakeMagnitude, gfScreenShakeVertical, gfScreenShakeRotation, gfScreenShakeSpeed);
        gfScreenShakeMagnitude = 0.0f;
	}
    if(wl_state.dfx_water_agitate_func && gfWaterAgitateMagnitude > 0)
    {
        wl_state.dfx_water_agitate_func(gfWaterAgitateMagnitude);
        gfWaterAgitateMagnitude = 0.0f;
    }

	if(wl_state.dfx_camera_matrix_override_func) {
		wl_state.dfx_camera_matrix_override_func(
			gxCameraMatrixOverride,
			gbOverrideCameraMatrix,
			gfOverrideCameraInfluence);
		gbOverrideCameraMatrix = false;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static int dynFxCountFx(DynFx* pFx)
{
	int iCount = 1;
	FOR_EACH_IN_EARRAY(pFx->eaChildFx, DynFx, pChildFx)
		iCount += dynFxCountFx(pChildFx);
	FOR_EACH_END
	return iCount;
}

int dynFxManCountAll(void)
{
	int iCount = 0;
	WorldRegion** eaWorldRegions = worldGetAllWorldRegions();
	FOR_EACH_IN_EARRAY(eaWorldRegions, WorldRegion, pWorldRegion)
		FOR_EACH_IN_EARRAY(pWorldRegion->fx_region.eaDynFxManagers, DynFxManager, pFxMan)
			FOR_EACH_IN_EARRAY(pFxMan->eaDynFx, DynFx, pFx)
				iCount += dynFxCountFx(pFx);
			FOR_EACH_END
		FOR_EACH_END
	FOR_EACH_END
	return iCount;
}


/// Message stuff
void dynPushMessage(DynFx* pFx, const char* pcMessage)
{
	DynFxInfo* pInfo = GET_REF(pFx->hInfo);
	dynFxLog(pFx, "Received message %s", pcMessage);


	if (SAFE_MEMBER(pInfo, bForwardMessages))
	{
		FOR_EACH_IN_EARRAY(pFx->eaChildFx, DynFx, pChildFx)
			dynPushMessage(pChildFx, pcMessage);
		FOR_EACH_END;
	}

	{
		U32 uiBitmask = (DYNFX_MESSAGE_MASK << (pFx->uiMessageCount * DYNFX_MESSAGE_BITS) );
		int iMessageIndex = dynEventIndexFind(pInfo, pcMessage);
		U32 uiMessage;
		if (iMessageIndex < 0)
		{
			dynFxLog(pFx, "Ignoring message %s, no matching event.", pcMessage);
			return;
		}

		uiMessage = (U32)iMessageIndex;

		if (uiMessage != (uiMessage & DYNFX_MESSAGE_MASK))
		{
			Errorf("Somehow got message index not representable by our bit limit of %d", DYNFX_MESSAGE_BITS);
			return;
		}

		// Actually write the message
		if (pFx->uiMessageCount < DYNFX_MAX_MESSAGES)
		{

			pFx->uiMessages |= (uiMessage << (pFx->uiMessageCount * DYNFX_MESSAGE_BITS));

			++pFx->uiMessageCount;
		}
		else
		{
			// We need to make room for important messages.
			// This should very rarely happen, but we don't want FX sticking around in the rare event, so handle overflow
			ea32Push(&pFx->eaMessageOverflow, uiMessage);
		}
	}

	if((dynDebugState.bLabelDebugFX && pFx && pFx->bDebug) || dynDebugState.bLabelAllFX) {
		if(pInfo) {

			char pcMessageWithName[1024];
			F32 r = 0;
			F32 g = 0;

			// Color Kill messages as red, everything else as green.
			if(stricmp(pcMessage, "Kill")) {
				g = 1;
			} else {
				r = 1;
			}

			sprintf(pcMessageWithName, "%s - %s", pcMessage, pInfo->pcDynName);
			dynFxAddWorldDebugMessageForFX(pFx, pcMessageWithName, 2, r, g, 0);
		}
	}
}

void dynNearMessagePush(DynFxRegion* pFxRegion, const char* pcMessage, const Vec3 vNearPos, F32 fDistance)
{
	DynNearMessage* pPayload = malloc(sizeof(*pPayload));
	pPayload->pcMessage = pcMessage;
	pPayload->fDistanceSquared = SQR(fDistance);
	copyVec3(vNearPos, pPayload->vNearPos);
	eaPush(&pFxRegion->eaNearMessagePayloads[!pFxRegion->uiCurrentPayloadArray], pPayload);
}

AUTO_COMMAND;
void dfxPrintMessageCount(void)
{
	int iCount = (int)mpGetAllocatedCount(MP_NAME(DynFxMessage));
	printf("%d total messages allocated\n", iCount);
}

// Sends multiple messages to all fx currently playing on the player
AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxTestMessage(ACMD_SENTENCE pcMessages)
{
	char *strtokcontext = 0;

	DynFxManager* pTestManager = dynDebugState.pTestManager?dynDebugState.pTestManager:dynDebugState.pDefaultTestManager;
	char* pcMessage;

	if (!pTestManager)
		return;

	//meaningless call to reset strTok
	pcMessage = strTokWithSpacesAndPunctuation(NULL, NULL);
	pcMessage = strTokWithSpacesAndPunctuation(pcMessages, " ");

	while (pcMessage)
	{
		dynFxManBroadcastMessage(pTestManager, allocAddString(pcMessage));

		pcMessage = strTokWithSpacesAndPunctuation(pcMessages, " ");
	}
}

static DynNode* pDummyCamNode;

AUTO_RUN;
void dynSetupDummyNode(void)
{
	if (GetAppGlobalType() == GLOBALTYPE_CLIENT)
	{
		Vec3 vDummyPos;
		pDummyCamNode = dynNodeAlloc();
		setVec3(vDummyPos, 15000, 15000, 15000);
		dynNodeSetPos(pDummyCamNode, vDummyPos);
	}
}

static void dynSetCameraNodeMatrix(DynNode *cameraNode, const Mat4 cameraMatrix) {

	if(CHECK_DYNPOS_NONFATAL((cameraMatrix[3]))) {

		dynNodeSetFromMat4(cameraNode, cameraMatrix);

	} else {

		if(!isProductionMode()) {

			printf(
				"Trying to set the FX system's camera node to some ridiculous position: %f %f %f\n",
				cameraMatrix[3][0], cameraMatrix[3][1], cameraMatrix[3][2]);

		}

		dynNodeSetFromMat4(cameraNode, unitmat);
	}
}

const DynNode* dynCameraNodeGet()
{
	if (dynDebugState.bNoCameraFX)
	{
		return pDummyCamNode;
	}

	if (!pCameraNode)
	{
		pCameraNode = dynNodeAlloc();
		dynSetCameraNodeMatrix(pCameraNode, wl_state.last_camera_frustum.cammat);
	}
	return pCameraNode;
}

void dynCameraNodeUpdate(void) {
	
	if(!pCameraNode) {
		dynCameraNodeGet();
	}

	if(pCameraNode) {
		dynSetCameraNodeMatrix(pCameraNode, wl_state.last_camera_frustum.cammat);
	}
}

DynDrawSkeleton* dynFxManagerGetDrawSkeleton(DynFxManager* pFxMan)
{
	return pFxMan->pDrawSkel;
}

void dynFxManSetDrawSkeleton(DynFxManager* pFxMan, DynDrawSkeleton* pDrawSkel)
{
	pFxMan->pDrawSkel = pDrawSkel;
}

WorldFXEntry* dynFxManagerGetCellEntry(DynFxManager* pFxMan)
{
	return pFxMan->pCellEntry;
}



// Writes all of the previous, current, and next bits to the screen
// Along with which actions were selected. A precursor to the 
// animation debug window
AUTO_CMD_INT(dynDebugState.danimShowBits,						danimShowBits)						ACMD_CATEGORY(dynAnimation) ACMD_COMMANDLINE;
AUTO_CMD_INT(dynDebugState.danimShowBitsHideMainSkeleton,		danimShowBitsHideMainSkeleton)		ACMD_CATEGORY(dynAnimation) ACMD_COMMANDLINE;
AUTO_CMD_INT(dynDebugState.danimShowBitsShowSubSkeleton,		danimShowBitsShowSubSkeleton)		ACMD_CATEGORY(dynAnimation) ACMD_COMMANDLINE;
AUTO_CMD_INT(dynDebugState.danimShowBitsHideHead,				danimShowBitsHideHead)				ACMD_CATEGORY(dynAnimation) ACMD_COMMANDLINE;
AUTO_CMD_INT(dynDebugState.danimShowBitsHideMainSequencer,		danimShowBitsHideMainSequencer)		ACMD_CATEGORY(dynAnimation) ACMD_COMMANDLINE;
AUTO_CMD_INT(dynDebugState.danimShowBitsShowSubSequencer,		danimShowBitsShowSubSequencer)		ACMD_CATEGORY(dynAnimation) ACMD_COMMANDLINE;
AUTO_CMD_INT(dynDebugState.danimShowBitsShowOverlaySequencer,	danimShowBitsShowOverlaySequencer)	ACMD_CATEGORY(dynAnimation) ACMD_COMMANDLINE;
AUTO_CMD_INT(dynDebugState.danimShowBitsShowTrackingIds,		danimShowBitsShowTrackingIds)		ACMD_CATEGORY(dynAnimation) ACMD_COMMANDLINE;
AUTO_CMD_INT(dynDebugState.danimShowBitsHideMovement,			danimShowBitsHideMovement)			ACMD_CATEGORY(dynAnimation) ACMD_COMMANDLINE;

//Writes audio debug info to the screen
AUTO_CMD_INT(dynDebugState.audioShowAnimBits,					audioShowAnimBits)					ACMD_CATEGORY(audio)		ACMD_COMMANDLINE;

//Writes costume debug info to the screen
AUTO_CMD_INT(dynDebugState.costumeShowSkeletonFiles,			costumeShowSkeletonFiles)			ACMD_CATEGORY(costume)		ACMD_COMMANDLINE;

AUTO_CMD_INT(dynDebugState.bDebugTailorSkeleton, danimDebugTailorSkeleton) ACMD_CATEGORY(dynAnimation);

AUTO_CMD_INT(dynDebugState.bNoCameraFX, dfxNoNewCameraFX) ACMD_CATEGORY(dynFx);

AUTO_CMD_INT(dynDebugState.bFastParticleForceUpdate, dfxForceFPUpdate) ACMD_CATEGORY(dynFx) ACMD_CMDLINEORPUBLIC;




// Open the named sequence file in your favorite editor.
AUTO_COMMAND ACMD_CATEGORY(dynAnimation);
void danimSeqEdit(const char* seq_name ACMD_NAMELIST("SeqData", REFDICTIONARY))
{
	const DynSeqData* pData = dynSeqDataFromName(seq_name);
	if (pData)
	{
		char cFileNameBuf[512];
		if (fileLocateWrite(pData->pcFileName, cFileNameBuf))
		{
			fileOpenWithEditor(cFileNameBuf);
		}
		else
		{
			Errorf("Could not find file %s for editing", pData->pcFileName);
		}
	}
	else
	{
		Errorf("Could not find DynSequence %s", seq_name);
	}
}

AUTO_COMMAND ACMD_CATEGORY(dynAnimation);
void danimPrintSeqDataBits(const char* seq_name ACMD_NAMELIST("SeqData", REFDICTIONARY))
{
	const DynSeqData* pData = dynSeqDataFromName(seq_name);
	char cBuffer[2048];
	printf("DynSequence %s:\n", pData->pcName);
	dynBitFieldStaticWriteBitString(SAFESTR(cBuffer), &pData->requiresBits);
	printf("	Requires %s\n", cBuffer);
	dynBitFieldStaticWriteBitString(SAFESTR(cBuffer), &pData->optionalBits);
	printf("	Optional %s\n", cBuffer);
}

// Open the named move file in your favorite editor.
AUTO_COMMAND ACMD_CATEGORY(dynAnimation);
void danimMoveEdit(const char* move_name ACMD_NAMELIST("DynMove", REFDICTIONARY))
{
	const DynMove* pMove = dynMoveFromName(move_name);
	if (pMove)
	{
		char cFileNameBuf[512];
		if (fileLocateWrite(pMove->pcFilename, cFileNameBuf))
		{
			fileOpenWithEditor(cFileNameBuf);
		}
		else
		{
			Errorf("Could not find file %s for editing", pMove->pcFilename);
		}
	}
	else
	{
		Errorf("Could not find DynSequence %s", move_name);
	}
}

// Draw the skeleton of the current character
AUTO_CMD_INT(dynDebugState.bDrawSkeleton, danimDrawSkeleton) ACMD_CATEGORY(dynAnimation);
AUTO_CMD_INT(dynDebugState.bDrawSkeletonAxes, danimDrawSkeletonAxes) ACMD_CATEGORY(dynAnimation);
AUTO_CMD_INT(dynDebugState.bDrawSkeletonNonCritical, danimDrawSkeletonNonCritical) ACMD_CATEGORY(dynAnimation);

// Debug hand registration IK
AUTO_CMD_INT(dynDebugState.bDebugIK, danimDebugIK) ACMD_CATEGORY(dynAnimation);
AUTO_CMD_INT(dynDebugState.bDrawIKTargets, danimDrawIKTargets) ACMD_CATEGORY(dynAnimation);

// Draw the skeleton of the current character
AUTO_CMD_INT(dynDebugState.bDrawCollisionExtents, danimDrawCollisionExtents) ACMD_CATEGORY(dynAnimation);
AUTO_CMD_INT(dynDebugState.bDrawVisibilityExtents, danimDrawVisibilityExtents) ACMD_CATEGORY(dynAnimation);

// Draw the triangles for any mesh trails
AUTO_CMD_INT(dynDebugState.bDrawTrailTris, dfxDrawTrailTriangles) ACMD_CATEGORY(dynFx);

// Write to the screen how many fx objects currently exist
AUTO_CMD_INT(dynDebugState.bDrawNumFx, dfxShowCount) ACMD_CATEGORY(dynFx) ACMD_CMDLINEORPUBLIC;

// For programmers. If set to 1, fx that have been flagged as 'debug' will trigger a break point if the debugger is attached.
AUTO_CMD_INT(dynDebugState.bBreakOnDebugFx, dfxBreakOnDebugFX) ACMD_CATEGORY(dynFx);

// Draw the shape of the splat collision cylinder/cone
AUTO_CMD_INT(dynDebugState.bDrawSplatCollision, dfxDrawSplatCollision) ACMD_CATEGORY(dynFx);

// Draw 3-axis vectors at each FX
AUTO_CMD_INT(dynDebugState.bDrawFxTransforms, dfxDrawFxTransforms) ACMD_CATEGORY(dynFx);

// Draw debug info for mesh trails
AUTO_CMD_INT(dynDebugState.bDrawMeshTrailDebugInfo, dfxDrawMeshTrailDebug) ACMD_CATEGORY(dynFx);

// Draw visibility spheres at each FX
AUTO_CMD_INT(dynDebugState.bDrawFxVisibility, dfxDrawFxVisibility) ACMD_CATEGORY(dynFx);

// Draw the shape of the fx sky volumes
AUTO_CMD_INT(dynDebugState.bDrawSkyVolumes, dfxDrawSkyVolumes) ACMD_CATEGORY(dynFx);

// Draw the fx raycast rays, green if they hit, red if they miss
AUTO_CMD_INT(dynDebugState.bDrawRays, dfxDrawRays) ACMD_CATEGORY(dynFx);

// Draw debug info for bouncers
AUTO_CMD_INT(dynDebugState.bDrawBouncers, danimDrawBouncers) ACMD_CATEGORY(dynFx);

// Draw impact triggers
AUTO_CMD_INT(dynDebugState.bDrawImpactTriggers, dfxDrawImpactTriggers) ACMD_CATEGORY(dynFx);

// Draw names for FX with the "Debug" flag set.
AUTO_CMD_INT(dynDebugState.bLabelDebugFX, dfxDrawLabelsForDebug) ACMD_CATEGORY(dynFx);

// Draw names for FX with the "Debug" flag set.
AUTO_CMD_INT(dynDebugState.bLabelAllFX, dfxDrawLabelsForAllFX) ACMD_CATEGORY(dynFx);

// Pause all fx systems
AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxPauseAll( int pause )
{
	static F32 fPreviousRate = 1.0f;
	if (pause)
	{
		if (dynDebugState.fDynFxRate > 0.0f)
			fPreviousRate = dynDebugState.fDynFxRate;
		dynDebugState.fDynFxRate = 0.0f;
	}
	else
	{
		if (dynDebugState.fDynFxRate == 0.0f)
			dynDebugState.fDynFxRate = fPreviousRate;
	}
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(dfxPauseAll);
void dfxPauseAllQuery(CmdContext *cmd)
{
	// Make it look just like an AUTO_CMD_INT
	estrPrintf(cmd->output_msg, "dfxPauseAll %d", (int)(dynDebugState.fDynFxRate == 0.0f));
}


// Sets how at what rate to run the fx systems
// Defaults to 1.0, or normal rate.
AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxSetFxRate( F32 rate )
{
	dynDebugState.fDynFxRate = rate;
}


// Kill all fx systems
AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxKillAll()
{
	dynFxKillAllFx();
}


// Turn off all fx
AUTO_CMD_INT(dynDebugState.bNoNewFx, noNewFx) ACMD_CATEGORY(dynFx) ACMD_CMDLINE;
AUTO_CMD_INT(dynDebugState.bNoNewFx, noFx) ACMD_CATEGORY(dynFx) ACMD_CMDLINE;
AUTO_CMD_INT(dynDebugState.bNoNewFx, dfxOff) ACMD_CATEGORY(dynFx) ACMD_CMDLINE;


// Turn off all animation
AUTO_CMD_INT(dynDebugState.bNoAnimation, noAnimation) ACMD_CATEGORY(dynAnimation) ACMD_CMDLINE;
AUTO_CMD_INT(dynDebugState.bNoAnimation, danimOff) ACMD_CATEGORY(dynAnimation) ACMD_CMDLINE;

// Turn off wireframe for fx
AUTO_CMD_INT(dynDebugState.bFxNotWireframe, dfxNoWireframe) ACMD_CATEGORY(dynFx);

// tests new torso pointing code
AUTO_CMD_INT(dynDebugState.bPIXLabelParticles, dynPIXLabelParticles) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);

#if !PLATFORM_CONSOLE

// Open the named fx file in your favorite editor.
AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxEdit(const char* fx_name ACMD_NAMELIST("DynFxInfo", RESOURCEINFO))
{
	const char* pcFileName = dynFxInfoGetFileName(fx_name);
	if (pcFileName)
	{
		char cFileNameBuf[512];
		if (fileLocateWrite(pcFileName, cFileNameBuf))
		{
			fileOpenWithEditor(cFileNameBuf);
		}
		else
		{
			Errorf("Could not find file %s for editing", pcFileName);
		}
	}
	else
	{
		Errorf("Could not find fx %s for editing", fx_name);
	}
}


AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxOpenDirectoryOf(const char* fx_name ACMD_NAMELIST("DynFxInfo", RESOURCEINFO))
{
	const char* pcFileName = dynFxInfoGetFileName(fx_name);
	if (pcFileName)
	{
		char cFileNameBuf[512];
		if (fileLocateWrite(pcFileName, cFileNameBuf))
		{
			char* directory;
			forwardSlashes(cFileNameBuf);

			directory = strrchr(cFileNameBuf, '/');
			if (!directory)
				return;
			*directory = '\0';

			ulShellExecute(NULL, "open", cFileNameBuf, NULL, NULL, SW_SHOWNORMAL);
		}
		else
		{
			Errorf("Could not find file %s", pcFileName);
		}
	}
	else
	{
		Errorf("Could not find fx %s", fx_name);
	}
}

#endif

void dynFxManagerPushFxArray( DynFxManager* pFxManager, DynFx*** peaFx )
{
	eaPushEArray(&pFxManager->eaDynFx, peaFx);
}

F32 dynFxGetMaxDrawDistance( const char* pcFxName )
{
	return 1000.0f; // for now, hard coded
}

void dynFxManBroadcastMessage( DynFxManager* pFxManager, const char* pcMessage )
{
	FOR_EACH_IN_EARRAY(pFxManager->eaDynFx, DynFx, pFx)
		dynPushMessage(pFx, pcMessage);
	FOR_EACH_END
}


void dynFxManSetCostume(DynFxManager* pFxManager, const WLCostume* pCostume)
{
	REMOVE_HANDLE(pFxManager->hCostume);
	if (pCostume)
	{
		SET_HANDLE_FROM_REFERENT("Costume", (WLCostume*)pCostume, pFxManager->hCostume);
	}
}

DynFxManager* dynFxGetUiManager(bool b3D)
{
	return b3D?pUiFxManager3D:pUiFxManager2D;
}

DynFxManager* dynFxGetGlobalFxManager(const Vec3 vPos)
{
	WorldRegion* pWorldRegion = worldGetWorldRegionByPos(vPos);
	if (pWorldRegion)
	{
		return pWorldRegion->fx_region.pGlobalFxManager;
	}
	return NULL;
}

DynFxManager* dynFxGetDebrisFxManager(DynFxManager* pFxManager)
{
	if (!pFxManager->pWorldRegion)
		return NULL;
	return pFxManager->pWorldRegion->fx_region.pDebrisFxManager;
}

bool dynFxManChangedThisFrame(DynFxManager* pFxManager)
{
	return pFxManager->bRegionChangedThisFrame;
}

void dynFxManSetRegion(DynFxManager* pFxManager, WorldRegion* pRegion)
{
	eaFindAndRemoveFast(&eaOrphanedFxManagers, pFxManager);
	eaForEach(&pFxManager->eaDynFx, dynFxRemoveFromGridRecurse); // make sure the fx in this manager are no longer attached to the old region
	if( pFxManager->pWorldRegion != pRegion )
	{
		pFxManager->bRegionChangedThisFrame = true;
		if (pFxManager->pWorldRegion) {
			eaFindAndRemoveFast(&pFxManager->pWorldRegion->fx_region.eaDynFxManagers, pFxManager);
		}
		if (pRegion) {
			eaPush(&pRegion->fx_region.eaDynFxManagers, pFxManager);
		}
	}
	
	if (pRegion)
	{
		pFxManager->pWorldRegion = pRegion;
		pFxManager->bPermanentRegion = true;
	}
	else
	{
		pFxManager->pWorldRegion = NULL;
		pFxManager->bPermanentRegion = false;
	}
			
}

bool dynFxManUniqueFXCheck(DynFxManager* pFxMan, const DynFxInfo* pFxInfo)
{
	if (!pFxMan->stUniqueFX)
		pFxMan->stUniqueFX = stashTableCreateWithStringKeys(8, StashDefault);
	return stashAddPointer(pFxMan->stUniqueFX, pFxInfo->pcDynName, pFxInfo, false);
}

bool dynFxManUniqueFXRemove(DynFxManager* pFxMan, const DynFxInfo* pFxInfo)
{
	if (pFxMan->stUniqueFX)
		return stashRemovePointer(pFxMan->stUniqueFX, pFxInfo->pcDynName, NULL);
	return false;
}

bool dynFxManIsLocalPlayer(DynFxManager* pFxMan)
{
	return pFxMan->bLocalPlayer;
}

bool dynFxManDoesntSelfUpdate(SA_PARAM_NN_VALID DynFxManager* pFxMan)
{
	return pFxMan->bDoesntSelfUpdate;
}

void dynFxManSuppress(DynFxManager* pFxMan, const char* pcTag)
{
	FOR_EACH_IN_EARRAY(pFxMan->eaSuppressors, DynFxSuppressor, pSuppressor)
	{
		if (pSuppressor->pcTag == pcTag)
		{
			pSuppressor->bSuppressed = true;
			return;
		}
	}
	FOR_EACH_END;

	// Got this far then we need to make a new one
	{
		DynFxSuppressor* pNew = malloc(sizeof(DynFxSuppressor));
		pNew->bSuppressed = true;
		pNew->pcTag = pcTag;
		pNew->fAmount = 0.0f;
		eaPush(&pFxMan->eaSuppressors, pNew);
	}
}


static void dynFxManUpdateSuppressors(DynFxManager* pFxMan, F32 fDeltaTime)
{
	AUTO_FLOAT(fSuppressionRate, 4.0f);

	FOR_EACH_IN_EARRAY(pFxMan->eaSuppressors, DynFxSuppressor, pSuppressor)
	{
		if (pSuppressor->bSuppressed)
		{
			pSuppressor->fAmount += fSuppressionRate * fDeltaTime;	
			MIN1(pSuppressor->fAmount, 1.0f);
			//printf("%.2f\n", pSuppressor->fAmount);
			pSuppressor->bSuppressed = false;
		}
		else
		{
			pSuppressor->fAmount -= fSuppressionRate * fDeltaTime;	
			//printf("%.2f\n", pSuppressor->fAmount);
			if (pSuppressor->fAmount <= 0.0f)
			{
				free(pSuppressor);
				eaRemoveFast(&pFxMan->eaSuppressors, ipSuppressorIndex);
			}
		}
	}
	FOR_EACH_END;
}

static void dynFxIKTargetFree(DynFxIKTarget* pTarget)
{
	REMOVE_HANDLE(pTarget->hIKNode);
	REMOVE_HANDLE(pTarget->hFX);
	free(pTarget);
}

static void dynFxManUpdateIKTargets(DynFxManager* pFxMan)
{
	FOR_EACH_IN_EARRAY(pFxMan->eaIKTargets, DynFxIKTarget, pTarget)
	{
		if (!GET_REF(pTarget->hIKNode) || !GET_REF(pTarget->hFX))
		{
			dynFxIKTargetFree(pTarget);
			eaRemoveFast(&pFxMan->eaIKTargets, ipTargetIndex);
		}
	}
	FOR_EACH_END;
}

void dynFxManAddIKTarget(DynFxManager* pFxMan, const char* pcTag, const DynNode* pIKNode, const DynFx* pFX)
{
	DynFxIKTarget* pTarget = malloc(sizeof(DynFxIKTarget));
	pTarget->pcTag = pcTag;
	ADD_SIMPLE_POINTER_REFERENCE_DYN(pTarget->hIKNode, pIKNode);
	ADD_SIMPLE_POINTER_REFERENCE_DYN(pTarget->hFX, pFX);
	eaPush(&pFxMan->eaIKTargets, pTarget);
}

U32 dynFxManNumIKTargets(DynFxManager* pFxMan, const char* pcTag)
{
	U32 count = 0;
	FOR_EACH_IN_EARRAY(pFxMan->eaIKTargets, DynFxIKTarget, pTarget)
	{
		if (pTarget->pcTag == pcTag)
		{
			const DynNode* pNode = GET_REF(pTarget->hIKNode);
			const DynFx* pFx = GET_REF(pTarget->hFX);
			if (pNode && pFx)
			{
				count++;
			}
		}
	}
	FOR_EACH_END;
	return count;
}

const DynNode* dynFxManFindIKTarget(DynFxManager* pFxMan, const char* pcTag)
{
	FOR_EACH_IN_EARRAY(pFxMan->eaIKTargets, DynFxIKTarget, pTarget)
	{
		if (pTarget->pcTag == pcTag)
		{
			const DynNode* pNode = GET_REF(pTarget->hIKNode);
			const DynFx* pFx = GET_REF(pTarget->hFX);
			if (pNode && pFx)
			{
				bool bNotSuppressed = true;
				FOR_EACH_IN_EARRAY(pFxMan->eaSuppressors, DynFxSuppressor, pSuppressor)
				{
					if (GET_REF(pFx->hInfo) && pSuppressor->pcTag == GET_REF(pFx->hInfo)->pcSuppressionTag)
					{
						bNotSuppressed = false;
						break;
					}
				}
				FOR_EACH_END;
				if (bNotSuppressed)
					return pNode;
			}
		}
	}
	FOR_EACH_END;

	return NULL;
}

const DynNode* dynFxManFindIKTargetByPos(DynFxManager* pFxMan, const char* pcTag, U32 pos)
{
	U32 count = 0;
	FOR_EACH_IN_EARRAY(pFxMan->eaIKTargets, DynFxIKTarget, pTarget)
	{
		if (pTarget->pcTag == pcTag)
		{
			const DynNode* pNode = GET_REF(pTarget->hIKNode);
			const DynFx* pFx = GET_REF(pTarget->hFX);
			if (pNode && pFx)
			{
				bool bNotSuppressed = true;
				FOR_EACH_IN_EARRAY(pFxMan->eaSuppressors, DynFxSuppressor, pSuppressor)
				{
					if (GET_REF(pFx->hInfo) && pSuppressor->pcTag == GET_REF(pFx->hInfo)->pcSuppressionTag)
					{
						bNotSuppressed = false;
						break;
					}
				}
				FOR_EACH_END;
				if (bNotSuppressed){
					count++;
					if (count == pos)
						return pNode;
				}
			}
		}
	}
	FOR_EACH_END;

	return NULL;
}

DynFxSortBucket* dynSortBucketCreate(void)
{
	DynFxSortBucket *pSortBucket;
	MP_CREATE(DynFxSortBucket, 64);
	pSortBucket = MP_ALLOC(DynFxSortBucket);
	pSortBucket->iRefCount = 1;
	eaPush(&eaSortBuckets, pSortBucket);
	return pSortBucket;
}

DynFxSortBucket* dynSortBucketIncRefCount(DynFxSortBucket* pSortBucket)
{
	if (pSortBucket)
	{
		assert(pSortBucket->iRefCount > 0);
		pSortBucket->iRefCount++;
	}
	return pSortBucket;
}

void dynSortBucketDecRefCount(DynFxSortBucket* pSortBucket)
{
	if (!pSortBucket)
		return;

	assert(pSortBucket->iRefCount > 0);
	pSortBucket->iRefCount--;

	if (!pSortBucket->iRefCount)
	{
		eaFindAndRemoveFast(&eaSortBuckets, pSortBucket);
		eaDestroy(&pSortBucket->eaCachedDrawables);
		MP_FREE(DynFxSortBucket, pSortBucket);
	}
}

void dynSortBucketClearCache(void)
{
	FOR_EACH_IN_EARRAY(eaSortBuckets, DynFxSortBucket, pSortBucket)
		eaClear(&pSortBucket->eaCachedDrawables);
	FOR_EACH_END
}

void dynMeshTrailDebugInfoFree(DynMeshTrailDebugInfo* pDebugInfo)
{
	REMOVE_HANDLE(pDebugInfo->hFx);
	eafDestroy(&pDebugInfo->eafPoints);
	free(pDebugInfo);
}



void dtAddWorldSpaceMessage(const char *pcMessage, Vec3 vPos, F32 fTime, float r, float g, float b) {
	DynFxDebugWorldSpaceMessage *pMessage = calloc(1, sizeof(DynFxDebugWorldSpaceMessage));
	pMessage->pcMessage = strdup(pcMessage);
	copyVec3(vPos, pMessage->vPos);
	pMessage->fTimeLeft = fTime;
	pMessage->fStartTime = fTime;
	pMessage->color.r = (U8)(r * 255.0);
	pMessage->color.g = (U8)(g * 255.0);
	pMessage->color.b = (U8)(b * 255.0);
	pMessage->color.a = 255;

	EnterCriticalSection(&fxLabelsCs);
	eaPush(&dynDebugState.eaFXWorldSpaceMessages, pMessage);
	LeaveCriticalSection(&fxLabelsCs);
}

void dtDestroyWorldSpaceMessage(DynFxDebugWorldSpaceMessage * pMessage) {
	free(pMessage->pcMessage);
	free(pMessage);
}

void dtIterateWorldSpaceMessages(F32 fTime) {

	DynFxDebugWorldSpaceMessage **eaNewMessages = NULL;
	int i;

	EnterCriticalSection(&fxLabelsCs);

	fTime *= wl_state.timeStepScaleDebug;

	for(i = 0; i < eaSize(&dynDebugState.eaFXWorldSpaceMessages); i++) {
		if(dynDebugState.eaFXWorldSpaceMessages[i]->fTimeLeft <= 0) {
			dtDestroyWorldSpaceMessage(dynDebugState.eaFXWorldSpaceMessages[i]);
		} else {

			// Slowly fade out the one-shot messages (stuff that had a time
			// attached to it that doesn't get remade every frame).
			dynDebugState.eaFXWorldSpaceMessages[i]->color.a = (U8)((
				dynDebugState.eaFXWorldSpaceMessages[i]->fTimeLeft /
				dynDebugState.eaFXWorldSpaceMessages[i]->fStartTime) * 255.0);

			dynDebugState.eaFXWorldSpaceMessages[i]->fTimeLeft -= fTime;

			// Move them upwards like a damage counter.
			dynDebugState.eaFXWorldSpaceMessages[i]->vPos[1] += fTime * 2.0;

			// Save them for next frame.
			eaPush(&eaNewMessages, dynDebugState.eaFXWorldSpaceMessages[i]);
		}
	}

	eaDestroy(&dynDebugState.eaFXWorldSpaceMessages);
	dynDebugState.eaFXWorldSpaceMessages = eaNewMessages;

	LeaveCriticalSection(&fxLabelsCs);
}






#include "dynFxManager_h_ast.c"

