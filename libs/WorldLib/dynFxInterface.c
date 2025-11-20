#include "Error.h"

#include "dynFxInterface.h"
#include "dynAnimInterface.h"

#include "dynFx.h"
#include "dynFxParticle.h"
#include "dynFxFastParticle.h"
#include "dynFxInfo.h"
#include "dynEngine.h"

#include "WorldCellEntryPrivate.h"
#include "WorldCellEntry.h"
#include "partition_enums.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

static dtFxManager uiFxManagerGuidCount = 2;
static dtNode uiNodeGuidCount = 0;
static dtFx uiFxGuidCount = 0;

static StashTable stNodes;
static StashTable stFxMans;
static StashTable stFx;


DynNode* dynNodeFromGuid(dtNode guidNode)
{
	DynNode* pNode;
	if ( stashIntFindPointer(stNodes, guidNode, &pNode))
		return pNode;
	return NULL;
}

DynFxManager* dynFxManFromGuid(dtFxManager guidFxMan)
{
	DynFxManager* pFxMan;
	if (guidFxMan == 0)
		return NULL;
	if ( stashIntFindPointer(stFxMans, guidFxMan, &pFxMan))
		return pFxMan;
	return NULL;
}

DynFx* dynFxFromGuid(dtFx guidFx)
{
	DynFx* pFx;
	if ( stashIntFindPointer(stFx, guidFx, &pFx))
		return pFx;
	return NULL;
}

void dynNodeRemoveGuid(dtNode guidNode)
{
	bool bSuccess = stashIntRemovePointer(stNodes, guidNode, NULL);
	if (!bSuccess)
		FatalErrorf("Failed to remove node guid %d, must have already been removed!", guidNode);
}

void dynFxManRemoveGuid(dtFxManager guidFxMan)
{
	bool bSuccess = stashIntRemovePointer(stFxMans, guidFxMan, NULL);
	if (!bSuccess)
		FatalErrorf("Failed to remove fx manager guid %d, must have already been removed!", guidFxMan);
}

void dynFxRemoveGuid(dtFx guidFx)
{
	bool bSuccess = stashIntRemovePointer(stFx, guidFx, NULL);
	if (!bSuccess)
		FatalErrorf("Failed to remove fx guid %d, must have already been removed!", guidFx);
}

// //
// FX MANAGER
// //

// Create
dtFxManager dtFxManCreate( eFxManagerType eType, dtNode nodeGuid, WorldFXEntry* pCellEntry, bool bLocalPlayer, bool bNoSound)
{
	ADVANCE_GUID(uiFxManagerGuidCount);
	{
		dtFxManager guid = uiFxManagerGuidCount;
		DynNode* pNode = dynNodeFromGuid(nodeGuid);
		DynFxManager* pFxMan = dynFxManCreate(pNode, NULL, pCellEntry, eType, guid, PARTITION_CLIENT, bLocalPlayer, bNoSound);

		ADD_TO_TABLE(pFxMan, stFxMans, guid);
		return guid;
	}
}

DynFxManager* dtFxManCreateGlobal( DynNode* pParentNode, WorldRegion* pRegion, eFxManagerType eType)
{
	ADVANCE_GUID(uiFxManagerGuidCount);
	{
		dtFxManager guid = uiFxManagerGuidCount;
		DynFxManager* pFxMan = dynFxManCreate(pParentNode, pRegion, NULL, eType, guid, PARTITION_CLIENT, false, false);

		ADD_TO_TABLE(pFxMan, stFxMans, guid);
		return pFxMan;
	}
}

void dtFxManDestroy(dtFxManager guid)
{
	DynFxManager* pFxMan = dynFxManFromGuid(guid);
	if (pFxMan)
	{
		dynFxManDestroy(pFxMan);
	}
}

void dtFxManSetTestTargetNode(dtFxManager guid, dtNode target, dtFxManager targetManager)
{
	DynFxManager* pFxMan = dynFxManFromGuid(guid);
	DynNode* pNode = dynNodeFromGuid(target);
	if (pFxMan)
	{
		dynFxManSetTestTargetNode(pFxMan, pNode, targetManager);
	}
}

void dtFxManStopUsingFxInfo(dtFxManager guid, const char* pcDynFxName, bool bImmediate)
{
	DynFxManager* pFxMan = dynFxManFromGuid(guid);
	if (pFxMan || guid == 0)
		dynFxManStopUsingFxInfo(pFxMan, pcDynFxName, bImmediate);
}

void dtFxManSetDebugFlag(dtFxManager guid)
{
	DynFxManager* pFxMan = dynFxManFromGuid(guid);
	dynEngineSetTestManager(pFxMan);
}

void dtFxManSetCostumeFXHue(dtFxManager guid, F32 fHue)
{
	DynFxManager* pFxMan = dynFxManFromGuid(guid);
	if (pFxMan)
		dynFxManSetCostumeFXHue(pFxMan,fHue);
}

void dtFxManAddMaintainedFx(dtFxManager guid, const char* pcDynFxName, DynParamBlock *paramblock, F32 fHue, dtNode targetGuid, eDynFxSource eSource)
{
	DynFxManager* pFxMan = dynFxManFromGuid(guid);
	if (pFxMan)
		dynFxManAddMaintainedFX(pFxMan, pcDynFxName, paramblock, fHue, targetGuid, eSource);
}

void dtFxManSendMessageMaintainedFx(dtFxManager guid, const char* pcDynFxName, DynFxMessage** ppMessages)
{
	DynFxManager* pFxMan = dynFxManFromGuid(guid);
	if (pFxMan)
		dynFxManSendMessageMaintainedFx(pFxMan, pcDynFxName, ppMessages);
}

void dtFxManRemoveMaintainedFx(dtFxManager guid, const char* pcDynFxName)
{
	DynFxManager* pFxMan = dynFxManFromGuid(guid);
	if (pFxMan)
		dynFxManRemoveMaintainedFX(pFxMan, pcDynFxName, false);
}

void dtTestFx(const char* pcInfo)
{
	dfxTestFx(pcInfo);
}


// //
// FX
// //



void dynFxManAddAutoRetryFX(DynFxManager* pFxMan, DynFxCreateParams* pCreateParams)
{
	eaPush(&pFxMan->eaAutoRetryFX, pCreateParams);
}

dtFx dtFxGetFxGuid(DynFx *pFx) {
	if(!pFx->guid) {
		ADVANCE_GUID(uiFxGuidCount);
		pFx->guid = uiFxGuidCount;
		ADD_TO_TABLE(pFx, stFx, pFx->guid);
	}
	return pFx->guid;
}

//bAutoRetrying is true if this is being called from inside the AutoRetry update loop.
dtFx dtAddFxEx(DynFxCreateParams *pCreateParams, bool bAutoRetrying, bool* pbParamsWereFreed) {

	if (dynDebugState.bNoNewFx)
	{

		//AutoRetry createparams have been alloc'd and need to be freed.
		if (pCreateParams->bAutoRetry)
			free(pCreateParams);

		if (pbParamsWereFreed)
			*pbParamsWereFreed = true;
		return 0;
	}

	devassert(pCreateParams->pcInfo && pCreateParams->pcInfo[0]);

	ADVANCE_GUID(uiFxGuidCount);

	{
		DynAddFxParams params = {0};
		dtFx guid = uiFxGuidCount;
		DynFxManager* pFxMan = dynFxManFromGuid(pCreateParams->guidFxMan);

		DynNode* pTargetNode =
			pCreateParams->guidTargetRoot ?
			dynNodeFromGuid(pCreateParams->guidTargetRoot) :
			NULL;

		DynNode* pSourceNode =
			pCreateParams->guidSourceRoot ?
			dynNodeFromGuid(pCreateParams->guidSourceRoot) :
			NULL;
	
		// If those target/source are NULL, use location
		// (vSource, vTarget) stuff.

		if(!pTargetNode && pCreateParams->vTarget) {
			pTargetNode = dynNodeAlloc();
			pTargetNode->uiUnManaged = 1;
			dynNodeSetPos(pTargetNode, pCreateParams->vTarget);

			if(pCreateParams->qTarget) {
				dynNodeSetRot(pTargetNode, pCreateParams->qTarget);
			}
		}

		if(!pSourceNode && pCreateParams->vSource) {
			pSourceNode = dynNodeAlloc();
			pSourceNode->uiUnManaged = 1;
			dynNodeSetPos(pSourceNode, pCreateParams->vSource);
		}

		// Set up the usual FX creation params.
		params.pParamBlock  = pCreateParams->pParamBlock;
		params.pTargetRoot  = pTargetNode;
		params.pSourceRoot  = pSourceNode;

		params.fHue         = pCreateParams->fHue;
		params.fValue       = pCreateParams->fValue;
		params.fSaturation  = pCreateParams->fSaturation;

		params.uiHitReactID = pCreateParams->uiHitReactID;

		params.eSource      = pCreateParams->eSource;

		params.cbJitterListSelectFunc = pCreateParams->cbJitterListSelectFunc;
		params.pJitterListSelectData = pCreateParams->pJitterListSelectData;

		if(!pFxMan) {

			// No FX Manager? Use the global one.
			// We NEED a source location, though.

			if(pSourceNode && pCreateParams->vSource) {

				pFxMan = dynFxGetGlobalFxManager(pCreateParams->vSource);

			}

		}

		if (pFxMan && !pFxMan->bWaitingForSkelUpdate) {

			DynFx* pFx;

			const char* pcFxInfo = dynFxManSwapFX(pFxMan, pCreateParams->pcInfo, false);

			if (pcFxInfo) {

				pFx = dynAddFx(pFxMan, pcFxInfo, &params);

				if (pFx) {

					ADD_TO_TABLE(pFx, stFx, guid);
					pFx->guid = guid;

				} else {

					//AutoRetry createparams have been alloc'd and need to be freed.
					if (pCreateParams->bAutoRetry)
						free(pCreateParams);

					if (pbParamsWereFreed)
						*pbParamsWereFreed = true;

					return 0;

				}

			} else {

				dynParamBlockFree(pCreateParams->pParamBlock);
				dynFxInfoReportFileError(pCreateParams->pcInfo);

				//AutoRetry createparams have been alloc'd and need to be freed.
				if (pCreateParams->bAutoRetry)
					free(pCreateParams);

				if (pbParamsWereFreed)
					*pbParamsWereFreed = true;

				dynNodeAttemptToFreeUnmanagedNode(pTargetNode);
				dynNodeAttemptToFreeUnmanagedNode(pSourceNode);

				return 0;

			}

		} else {

			// Couldn't get an FX manager, or we're waiting for a skeleton update.

			if (pFxMan && pCreateParams->bAutoRetry)
			{
				if (!bAutoRetrying)
					dynFxManAddAutoRetryFX(pFxMan, pCreateParams);
			}
			else
			{
				dynParamBlockFree(pCreateParams->pParamBlock);

				if (pCreateParams->pbNeedsRetry)
					*pCreateParams->pbNeedsRetry = 1;

				//AutoRetry createparams have been alloc'd and need to be freed.
				if (pCreateParams->bAutoRetry)
					free(pCreateParams);

				if (pbParamsWereFreed)
					*pbParamsWereFreed = true;

			}

			dynNodeAttemptToFreeUnmanagedNode(pTargetNode);
			dynNodeAttemptToFreeUnmanagedNode(pSourceNode);

			return 0;
		}

		// I'm wondering if we should also be calling dynParamBlockFree here like the other cases in this function ?

		//AutoRetry createparams have been alloc'd and need to be freed.
		if (pCreateParams->bAutoRetry)
			free(pCreateParams);

		if (pbParamsWereFreed)
			*pbParamsWereFreed = true;

		dynNodeAttemptToFreeUnmanagedNode(pTargetNode);
		dynNodeAttemptToFreeUnmanagedNode(pSourceNode);

		return guid;
	}
}






// Add FX
dtFx dtAddFx(dtFxManager guidFxMan,  const char* pcInfo,  DynParamBlock* pParamBlock,  dtNode guidTargetRoot, dtNode guidSourceRoot, F32 fHue, U32 uiHitReactID, bool* pbNeedsRetry, eDynFxSource eSource, dynJitterListSelectionCallback cbJitterListSelectFunc, void* pJitterListSelectData)
{
	DynFxCreateParams createParams = {0};

	devassert(pcInfo && pcInfo[0]);

	createParams.guidFxMan = guidFxMan;
	createParams.pcInfo = pcInfo;
	createParams.pParamBlock = pParamBlock;
	createParams.guidTargetRoot = guidTargetRoot;
	createParams.guidSourceRoot = guidSourceRoot;

	createParams.cbJitterListSelectFunc = cbJitterListSelectFunc;
	createParams.pJitterListSelectData = pJitterListSelectData;

	createParams.fHue = fHue;
	createParams.uiHitReactID = uiHitReactID;
	createParams.pbNeedsRetry = pbNeedsRetry;
	createParams.eSource = eSource;

	return dtAddFxEx(&createParams, false, NULL);

}

dtFx dtAddFxAutoRetry(dtFxManager guidFxMan,  const char* pcInfo,  DynParamBlock* pParamBlock,  dtNode guidTargetRoot, dtNode guidSourceRoot, F32 fHue, U32 uiHitReactID, eDynFxSource eSource, dynJitterListSelectionCallback cbJitterListSelectFunc, void* pJitterListSelectData)
{
	DynFxCreateParams* pCreateParams = malloc(sizeof(DynFxCreateParams));

	memset(pCreateParams, 0, sizeof(DynFxCreateParams));

	devassert(pcInfo && pcInfo[0]);

	pCreateParams->guidFxMan = guidFxMan;
	pCreateParams->pcInfo = pcInfo;
	pCreateParams->pParamBlock = pParamBlock;
	pCreateParams->guidTargetRoot = guidTargetRoot;
	pCreateParams->guidSourceRoot = guidSourceRoot;

	pCreateParams->cbJitterListSelectFunc = cbJitterListSelectFunc;
	pCreateParams->pJitterListSelectData = pJitterListSelectData;

	pCreateParams->fHue = fHue;
	pCreateParams->uiHitReactID = uiHitReactID;
	pCreateParams->bAutoRetry = true;
	pCreateParams->eSource = eSource;

	return dtAddFxEx(pCreateParams, false, NULL);
}

dtFx dtAddFxAtLocation( dtFxManager guidFxMan,  const char* pcInfo,  DynParamBlock* pParamBlock, const Vec3 vSource, const Vec3 vTarget, const Quat qTarget, F32 fHue, U32 uiHitReactID, bool* pbNeedsRetry, eDynFxSource eSource)
{
	DynFxCreateParams createParams = {0};

	createParams.guidFxMan = guidFxMan;
	createParams.pcInfo = pcInfo;
	createParams.pParamBlock = pParamBlock;

	createParams.vSource = (F32*)vSource;
	createParams.vTarget = (F32*)vTarget;
	createParams.qTarget = (F32*)qTarget;

	createParams.fHue = fHue;
	createParams.uiHitReactID = uiHitReactID;
	createParams.pbNeedsRetry = pbNeedsRetry;
	createParams.eSource = eSource;

	return dtAddFxEx(&createParams, false, NULL);

}

dtFx dtAddFxFromLocation( const char* pcInfo,  DynParamBlock* pParamBlock, dtNode guidTargetRoot, const Vec3 vSource, const Vec3 vTarget, const Quat qTarget, F32 fHue, U32 uiHitReactID, eDynFxSource eSource)
{
	DynFxCreateParams createParams = {0};

	createParams.pcInfo = pcInfo;
	createParams.pParamBlock = pParamBlock;

	createParams.guidTargetRoot = guidTargetRoot;

	createParams.vSource = (F32*)vSource;
	createParams.vTarget = (F32*)vTarget;
	createParams.qTarget = (F32*)qTarget;

	createParams.fHue = fHue;
	createParams.uiHitReactID = uiHitReactID;
	createParams.eSource = eSource;

	return dtAddFxEx(&createParams, false, NULL);

}



void dtFxKillEx(dtFx guid, bool bImmediate, bool bRemoveFromOwner)
{
	DynFx* pFx = dynFxFromGuid(guid);
	if (pFx)
		dynFxKill(pFx, bImmediate, bRemoveFromOwner, false, eDynFxKillReason_ExternalKill);
}

void dtFxSendMessage(dtFx guid, const char* pcMessage)
{
	DynFx* pFx = dynFxFromGuid(guid);
	if (pFx)
	{
		dynPushMessage(pFx, pcMessage);
	}
}

void dtFxKillAll(void)
{
	dynFxKillManaged();
	dynFxKillAllFx();
}

void dtFxSetInstanceData( dtFx guid, WorldDrawableList* pDrawableList, WorldInstanceParamList* pInstanceParamList, int iDrawableResetCounter )
{
	DynFx* pFx = dynFxFromGuid(guid);
	if (pFx)
	{
		addDrawableListRef(pDrawableList MEM_DBG_PARMS_INIT);
		pFx->pDrawableList = pDrawableList;
		addInstanceParamListRef(pInstanceParamList MEM_DBG_PARMS_INIT);
		pFx->pInstanceParamList = pInstanceParamList;
		pFx->iDrawableResetCounter = iDrawableResetCounter;
	}
}

// //
// NODE
// //


// Create
dtNode dtNodeCreate(void)
{
	DynNode* pNode = dynNodeAlloc();
	ADVANCE_GUID(uiNodeGuidCount);
	ADD_TO_TABLE(pNode, stNodes, uiNodeGuidCount);
	pNode->guid = uiNodeGuidCount;
	return uiNodeGuidCount;
}

void dtNodeDestroy(dtNode guid)
{
	DynNode* pNode = dynNodeFromGuid(guid);
	if (pNode)
	{
		dynNodeFree(pNode);
	}
}

void dtNodeSetPos(dtNode guid, const Vec3 vPos)
{
	DynNode* pNode = dynNodeFromGuid(guid);
	if (pNode)
	{
		dynNodeSetPos(pNode, vPos);
	}
}

void dtNodeSetRot(dtNode guid, const Quat qRot)
{
	DynNode* pNode = dynNodeFromGuid(guid);
	if (pNode)
	{
		dynNodeSetRot(pNode, qRot);
	}
}

void dtNodeSetPosAndRot(dtNode guid, const Vec3 vPos, const Quat qRot)
{
	DynNode* pNode = dynNodeFromGuid(guid);
	if (pNode)
	{
		dynNodeSetPos(pNode, vPos);
		dynNodeSetRot(pNode, qRot);
	}
}

void dtNodeSetFromMat4(dtNode guid, const Mat4 mat)
{
	DynNode* pNode = dynNodeFromGuid(guid);
	if (pNode)
	{
		dynNodeSetFromMat4(pNode, mat);
	}
}

void dtNodeSetParent(dtNode guid, dtNode parentGuid )
{
	DynNode* pNode = dynNodeFromGuid(guid);
	DynNode* pParentNode = dynNodeFromGuid(parentGuid);
	if (pNode)
	{
		if (!pParentNode)
		{
			dynNodeClearParent(pNode);
		}
		else
		{	
			dynNodeParent(pNode, pParentNode);
		}
	}

}

void dtNodeSetTag(dtNode guid, const char* pcTag)
{
	DynNode* pNode = dynNodeFromGuid(guid);
	if (pNode)
	{
		pNode->pcTag = pcTag;
	}
}


void dtFxInitSys(void)
{
	stFx = stashTableCreateInt(8192);
	stNodes = stashTableCreateInt(8192);
	stFxMans = stashTableCreateInt(256);
}

int dynFxNeedsAuxPass(dtFx guid)
{
	DynFxManager* pFxMan = dynFxManFromGuid(guid);
	DynFx**	eaDynFx = pFxMan->eaDynFx;
	int f, fxCount = eaSize(&eaDynFx);
	for (f = 0; f < fxCount; ++f)
	{
		DynFx * pFx = eaDynFx[f];
		if (pFx && GET_REF(pFx->hInfo) && GET_REF(pFx->hInfo)->bEntNeedsAuxPass)
			return 1;
	}
	return 0;
}

void dtFxClearWorldModel(dtFx guid) {
	
	DynFxManager *pFxMan = dynFxManFromGuid(guid);
	DynFx **eaDynFx = pFxMan->eaDynFx;
	int f, i, fxCount = eaSize(&eaDynFx);

	for (f = 0; f < fxCount; ++f) {

		DynFx * pFx = eaDynFx[f];
		if(pFx) {

			DynFxInfo *pInfo = GET_REF(pFx->hInfo);
			if(pInfo && pInfo->bAllowWorldModelSwitch) {

				if(pInfo->bGetModelFromWorld) {
					pFx->pParticle->pDraw->pcModelName = NULL;
					pFx->pParticle->pDraw->pModel = NULL;
					
					for(i = 0; i < eaSize(&pFx->pParticle->eaParticleSets); i++) {
						if(pFx->pParticle->eaParticleSets[i]->bUseModel) {
							dynFxFastParticleChangeModel(pFx->pParticle->eaParticleSets[i], NULL);
						}
					}
				}
			}
		}
	}
}

bool dtFxUpdateAndCheckModel(dtFx guid, WorldDrawableEntry *entry) {

	DynFxManager *pFxMan = dynFxManFromGuid(guid);
	DynFx **eaDynFx = pFxMan->eaDynFx;
	int f, fxCount = eaSize(&eaDynFx);
	bool bSkipDraw = false;

	const char *pcModelName = NULL;
	F32 *vModelScale = NULL;
	const Model *pModel = worldDrawableEntryGetModel(entry, NULL, NULL, NULL, &vModelScale);
	
	if(pModel) {
		
		pcModelName = pModel->name;

		if(pFxMan) {

			// Update the fake extents node on the FX manager.

			Vec3 vWorldScale;

			// Get the local bounds.
			subVec3(entry->base_entry.shared_bounds->local_max, entry->base_entry.shared_bounds->local_min, vWorldScale);

			// Add a little bit so it doesn't z-fight with whatever we put it on.
			vWorldScale[0] += 0.1f;
			vWorldScale[1] += 0.1f;
			vWorldScale[2] += 0.1f;

			// Set it by the world matrix to get the orientation correct.
			// Then change the position to the correct midpoint and set
			// the scale to the bounds.
			dynNodeSetFromMat4(pFxMan->pFakeExtentsNode, entry->base_entry.bounds.world_matrix);
			dynNodeSetPos(pFxMan->pFakeExtentsNode, entry->base_entry.bounds.world_mid);
			dynNodeSetScale(pFxMan->pFakeExtentsNode, vWorldScale);
		}
	}

	for (f = 0; f < fxCount; ++f) {

		DynFx * pFx = eaDynFx[f];
		if(pFx) {

			DynFxInfo *pInfo = GET_REF(pFx->hInfo);
			if(pInfo) {

				// Get scale info from the world.
				if(pInfo->bUseWorldModelScale) {
					if(vModelScale) {
						copyVec3(vModelScale, pFx->pParticle->pDraw->vScale);
					} else {
						setVec3same(pFx->pParticle->pDraw->vScale, 1);
					}
				}

				// Normal geometry particle.
				if(pInfo->bGetModelFromWorld) {
					if(pFx->pParticle && pFx->pParticle->pDraw->pcModelName != pcModelName) {
						
						int i;

						pFx->pParticle->pDraw->pcModelName = pcModelName;

						// Go through this FX's emitters and update the
						// models if they're using model emitters.
						for(i = 0; i < eaSize(&pFx->pParticle->eaParticleSets); i++) {
							if(pFx->pParticle->eaParticleSets[i]->bUseModel) {
								dynFxFastParticleChangeModel(pFx->pParticle->eaParticleSets[i], pcModelName);
							}
						}
					}

					// In cases where multiple world objects use the same FX,
					// allow the FX manager to switch which one it uses for
					// position and orientation. (Example: Hiding an object and
					// showing another, the FX should switch to the new one.)
					if(pInfo->bAllowWorldModelSwitch) {
						dynNodeSetFromMat4(pFxMan->pNode, entry->base_entry.bounds.world_matrix);
					}
				}

				// Cloth model.
				if(pInfo->bGetClothModelFromWorld) {
					if(pFx->pParticle && pFx->pParticle->pDraw->pcClothName != pcModelName) {
						pFx->pParticle->pDraw->pcClothName = pcModelName;
					}
				}

				bSkipDraw |= pInfo->bHideWorldModel;
			}
		}
	}

	return bSkipDraw;
}


void dtUpdateCameraInfo(void) {

	dynCameraNodeUpdate();

}

bool dtFxGetColor(dtFx guid, int index, Vec4 color) {
	DynFx *pFx = dynFxFromGuid(guid);
	if(pFx) {
		return dynFxGetColor(pFx, index, color);
	}
	return false;
}

bool dtFxSetColor(dtFx guid, int index, const Vec4 color) {
	DynFx *pFx = dynFxFromGuid(guid);
	if(pFx) {
		return dynFxSetColor(pFx, index, color);
	}
	return false;
}

