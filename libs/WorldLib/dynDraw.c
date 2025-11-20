
#include "dynDraw.h"

#include "memref.h"
#include "memlog.h"
#include "MemoryPool.h"
#include "ThreadSafeMemoryPool.h"
#include "stringcache.h"
#include "rgb_hsv.h"
#include "ScratchStack.h"

#include "dynCloth.h"
#include "wlModelLoad.h"
#include "dynNodeInline.h"
#include "dynFxInfo.h"
#include "dynFxParticle.h"
#include "dynAnimInterface.h"
#include "dynFx.h"
#include "wlCostume.h"
#include "wlAutoLOD.h"
#include "wlModelInline.h"
#include "WorldGrid.h"
#include "wlState.h"
#include "AutoGen/wlCostume_h_ast.h"
#include "DynFxInterface.h"
#include "WorldCellEntryPrivate.h"
#include "bounds.h"
#include "dynSkeletonMovement.h"
#include "dynAnimGraphUpdater.h"
#include "dynAnimGraph.h"
#include "dynStrand.h"
#include "dynGroundReg.h"
#include "dynCriticalNodes.h"

#include "autogen/dynDraw_h_ast.h"
#include "AutoGen/dynFxInfo_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation);); // Is this right?

#define DEFAULT_DYN_DRAW_GEO_COUNT 1024
#define DEFAULT_DYN_DRAW_SKELETON_COUNT 256

TSMP_DEFINE(DynDrawModel);
MP_DEFINE(DynDrawSkeleton);
TSMP_DEFINE(DynDrawVisibilityNode);
TSMP_DEFINE(DynSkinningMatSet);

WorldRegionLODSettings defaultLODSettings;

U32 LODSkeletonSlots[MAX_WORLD_REGION_LOD_LEVELS];

typedef struct BoneIdxAlloc
{
	U8* pBoneIdxs;
	U32 uiMaxIdxs;
	U32 uiCurrentBoneIdx;
} BoneIdxAlloc;

static U8* boneIdxAllocate(BoneIdxAlloc* pBoneAllocData, U32 uiNumBones)
{
	U8* pResult = &pBoneAllocData->pBoneIdxs[pBoneAllocData->uiCurrentBoneIdx];
	pBoneAllocData->uiCurrentBoneIdx += uiNumBones;
	if (pBoneAllocData->uiCurrentBoneIdx > pBoneAllocData->uiMaxIdxs)
		return NULL;
	return pResult;
}

AUTO_RUN;
void initDefaultLODSettings(void)
{
	defaultLODSettings.uiNumLODLevels = 6;
	defaultLODSettings.uiMaxLODLevel = 5;
	defaultLODSettings.uiBodySockLODLevel = 4;
	defaultLODSettings.uiWindLODLevel = defaultLODSettings.uiIKLODLevel = 2;

	defaultLODSettings.LodDistance[0] = 0.0f;
	defaultLODSettings.LodDistance[1] = 10.0f;
	defaultLODSettings.LodDistance[2] = 30.0f;
	defaultLODSettings.LodDistance[3] = 70.0f;
	defaultLODSettings.LodDistance[4] = 130.0f;
	defaultLODSettings.LodDistance[5] = 240.0f;


	defaultLODSettings.DefaultMaxLODSkelSlots[0] = 2;
	defaultLODSettings.DefaultMaxLODSkelSlots[1] = 8;
	defaultLODSettings.DefaultMaxLODSkelSlots[2] = 15;
	defaultLODSettings.DefaultMaxLODSkelSlots[3] = 15;
	defaultLODSettings.DefaultMaxLODSkelSlots[4] = 20;
	defaultLODSettings.DefaultMaxLODSkelSlots[5] = 10;

	defaultLODSettings.MaxLODSkelSlots[0] = 2;
	defaultLODSettings.MaxLODSkelSlots[1] = 8;
	defaultLODSettings.MaxLODSkelSlots[2] = 15;
	defaultLODSettings.MaxLODSkelSlots[3] = 15;
	defaultLODSettings.MaxLODSkelSlots[4] = 20;
	defaultLODSettings.MaxLODSkelSlots[5] = 10;
}

const char* pcSpriteNamedConstants[NUM_SPRITE_NAMED_CONSTANTS] = 
{
	"TexXfrm",
	"TexXfrm1",
	"Color1",
	"Color2",
	"Color3",
};

DynDrawSkeleton** eaDrawSkelList;

static const DynDrawSkeleton* pDebugDrawSkeleton;

static void dynDrawSetupDrawInfo(SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkel, bool bThreaded MEM_DBG_PARMS);
static void dynDrawModelClearPreSwapped(DynDrawModel *pGeo);

#define TRACK_INSTANCE_LIFETIME 1

static int debug_TrackGeoInstances = 0;
AUTO_CMD_INT(debug_TrackGeoInstances,debug_TrackGeoInstances);
#if TRACK_INSTANCE_LIFETIME
void DumpGeoInstance(DynDrawModel * pGeo, const char * eventName)
{
	OutputDebugStringf("GeoInstance %s: %s 0x%p 0x%p\n", eventName, pGeo->pModel ? pGeo->pModel->name : "UnnamedGeo", pGeo, pGeo->pInstanceParamList);
}

#define LOG_INSTANCE_FREE(M_geo)		if (debug_TrackGeoInstances) DumpGeoInstance((M_geo), "Free")
#define LOG_INSTANCE_SETUP(M_geo)		if (debug_TrackGeoInstances) DumpGeoInstance((M_geo), "Setup")

#else
#define LOG_INSTANCE_FREE(M_geo)
#define LOG_INSTANCE_SETUP(M_geo)
#endif


static const char* pcHair;
static const char* pcColor1;
static const char* pcColor2;
static const char* pcColor3;
static const char* pcInbetweenSubs;

AUTO_RUN;
void dynDrawInitStrings(void)
{
	pcHair = allocAddStaticString("Hair");
	pcColor1 = allocAddStaticString("Color1");
	pcColor2 = allocAddStaticString("Color2");
	pcColor3 = allocAddStaticString("Color3");
	pcInbetweenSubs = allocAddStaticString("InBetweenSubs");
}

AUTO_RUN;
void dynDrawInitMemPool(void)
{
	TSMP_CREATE(DynDrawModel, DEFAULT_DYN_DRAW_GEO_COUNT);
	MP_CREATE(DynDrawSkeleton, DEFAULT_DYN_DRAW_SKELETON_COUNT);
	TSMP_CREATE(DynDrawVisibilityNode, 128);
	TSMP_CREATE(DynSkinningMatSet, 384);
}

static void dynDrawSkeletonAddVisibilityNode(SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkel, const char* pcTag, Model* pModel)
{
	if (pModel && pcTag)
	{
		DynDrawVisibilityNode* pVisNode = TSMP_ALLOC(DynDrawVisibilityNode);
		pVisNode->pcBone = pcTag;
		pVisNode->fRadius = pModel->radius;
		eaPush(&pDrawSkel->eaVisNodes, pVisNode);
	}
}


DynDrawModel* dynDrawGeoCreate(void)
{
	DynDrawModel* pModel = TSMP_ALLOC(DynDrawModel);
	ZeroStruct(pModel);
	return pModel;
}

static void dynDrawSkeletonSetMaintainedFx(DynDrawSkeleton* pDrawSkel)
{
	F32 fHue;
	WLCostume* pCostume = pDrawSkel->pSkeleton?GET_REF(pDrawSkel->pSkeleton->hCostume):NULL;
	S32 iNumMaintainedFx;
	S32 iIndex;
	bool* pbFound;
	if (!pDrawSkel->pFxManager || !pCostume)
		return;
	fHue = dynFxManGetCostumeFXHue(pDrawSkel->pFxManager);
	iNumMaintainedFx = eaSize(&pDrawSkel->pFxManager->eaMaintainedFx);
	pbFound = _alloca(sizeof(bool) * iNumMaintainedFx);
	memset(pbFound, 0, sizeof(bool) * iNumMaintainedFx);

	// First add any new maintained fx
	FOR_EACH_IN_EARRAY(pCostume->eaFX, CostumeFX, pFx)
		S32 iOldIndex;
		const char *pRefString = REF_STRING_FROM_HANDLE(pFx->hFx);
		const char *pcFxName;
		bool bFound = false;
		assert(pRefString);
		pcFxName = wlCostumeSwapFX(pCostume, pRefString);
		for (iOldIndex = 0; iOldIndex < iNumMaintainedFx; ++iOldIndex)
		{
			DynFxMaintained* pMaintained = pDrawSkel->pFxManager->eaMaintainedFx[iOldIndex];
			if (pcFxName == REF_STRING_FROM_HANDLE(pMaintained->hInfo) &&
				StructCompare(parse_DynParamBlock, pMaintained->paramblock, pFx->pParams, 0, 0, 0) == 0)
			{
				pbFound[iOldIndex] = bFound = true;
				break;
			}
		}
		if (!bFound)
			dynFxManAddMaintainedFX(pDrawSkel->pFxManager, pcFxName, StructClone(parse_DynParamBlock, pFx->pParams), fHue ? fHue : pFx->fHue, 0, true);
	FOR_EACH_END;

	// Now remove any maintained FX that are not present on the costume list
	// Go backwards so we can remove without messing up the loop
	for (iIndex=iNumMaintainedFx-1; iIndex >= 0; --iIndex)
	{
		if(pDrawSkel->pFxManager->eaMaintainedFx[iIndex]->eSource == eDynFxSource_Costume) {
			if (!pbFound[iIndex])
				dynFxManRemoveMaintainedFXByIndex(pDrawSkel->pFxManager, iIndex, true);
		}
	}
}

// This is for STO
// Basically, walk through geometry pieces, and then adjust the skeleton position (as if we were animating it) to match the mount points on the geometry pieces
void dynDrawSkeletonGlueUpSkeletonToMatchMountPoints( DynDrawSkeleton* pDrawSkel )
{
	FOR_EACH_IN_EARRAY(pDrawSkel->eaDynGeos, DynDrawModel, pGeo)
	{
		const ModelHeader* pAPI;
		DynNode* pAttachmentNode;
		if (!pGeo->pModel)
			continue;
		pAPI = wlModelHeaderFromName(pGeo->pModel->header->modelname);
		if (!pAPI)
			continue;
		pAttachmentNode = (DynNode*)pGeo->pAttachmentNode;
		if (pAttachmentNode)
		{
			DynNode* pChild = pAttachmentNode->pChild;
			while (pChild)
			{
				FOR_EACH_IN_EARRAY(pAPI->altpivot, AltPivot, pAltPiv)
					if (strlen(pAltPiv->name) < 12 || strnicmp(pAltPiv->name, "MountPoint_", 11))
						continue;
					if (pChild->pcTag && !stricmp(pChild->pcTag, pAltPiv->name + 11))
					{
						// Matched bone name to mount point!
						dynNodeSetFromMat4(pChild, pAltPiv->mat);
						if (!vec3IsZero(pAltPiv->scale))
							dynNodeSetScale(pChild, pAltPiv->scale);
						pChild->uiLocked = 1;
						break;
					}
				FOR_EACH_END
				pChild = pChild->pSibling;
			}
		}
	}
	FOR_EACH_END
}

typedef struct SetupAnimBoneData
{
	DynAnimBoneInfo** eaAnimBoneInfos;
	DynScaleCollection* pScaleCollection;
	DynBaseSkeleton* pBaseSkel;
	DynBaseSkeleton* pUnscaledBaseSkeleton;
	SkelBlendInfo* pBlendInfo;
	F32 fMaxLength;
} SetupAnimBoneData;

bool setupAnimBoneInfo(DynNode* pNode, SetupAnimBoneData* pData)
{
	if (pNode->uiCriticalBone)
	{
		DynAnimBoneInfo* pBoneInfo = ScratchAlloc(sizeof(DynAnimBoneInfo));
		{
			const DynTransform* pxBaseTransform;
			if (pData->pScaleCollection && pNode->pcTag && ( pxBaseTransform = dynScaleCollectionFindTransform(pData->pScaleCollection, pNode->pcTag) ) )
				dynTransformCopy(pxBaseTransform, &pBoneInfo->xBaseTransform);
			else
				dynTransformClearInline(&pBoneInfo->xBaseTransform);
		}
		{
			const DynNode* pUnscaledBaseBone;
			if (pData->pUnscaledBaseSkeleton && pNode->pcTag && (pUnscaledBaseBone = dynBaseSkeletonFindNode(pData->pUnscaledBaseSkeleton, pNode->pcTag) ) )
			{
				Vec3 vTemp;
				dynNodeGetWorldSpacePos(pUnscaledBaseBone, vTemp);
				scaleVec3(vTemp, -1.0f, pBoneInfo->vBaseOffset);
			}
			else
			{
				zeroVec3(pBoneInfo->vBaseOffset);
			}
		}
		if (pData->pBlendInfo)
		{
			SkelBlendSeqInfo* pSeqInfo;
			bool foundBoneSequencer, foundBoneOverlay;
			
			foundBoneSequencer = stashFindInt(pData->pBlendInfo->stBoneSequencerInfo, pNode->pcTag, &pBoneInfo->iSeqIndex);
			foundBoneOverlay   = stashFindInt(pData->pBlendInfo->stBoneOverlayInfo,   pNode->pcTag, &pBoneInfo->iOverlayIndex);

			pBoneInfo->bIgnoreMasterOverlay = false;

			if (!foundBoneSequencer)
			{
				pBoneInfo->iSeqIndex = 0;
				pBoneInfo->iSeqAGUpdaterIndex = 0;
			}
			else if (pBoneInfo->iSeqIndex > 0)
			{
				pSeqInfo = pData->pBlendInfo->eaSequencer[pBoneInfo->iSeqIndex-1];
				pBoneInfo->iSeqAGUpdaterIndex   = pSeqInfo->iAGUpdaterIndex;
				pBoneInfo->bMovement            = pSeqInfo->bMovement;
				pBoneInfo->bIgnoreMasterOverlay |= pSeqInfo->bIgnoreMasterOverlay; // pretty sure we don't need this here
			}

			if (!foundBoneOverlay)
			{
				pBoneInfo->iOverlayIndex = -1;
				pBoneInfo->iOverlayAGUpdaterIndex = -1;
			}
			else if (pBoneInfo->iOverlayIndex > 0)
			{
				pSeqInfo = pData->pBlendInfo->eaSequencer[pBoneInfo->iOverlayIndex-1];
				pBoneInfo->iOverlayAGUpdaterIndex = pSeqInfo->iAGUpdaterIndex;
				pBoneInfo->bIgnoreMasterOverlay   |= pSeqInfo->bIgnoreMasterOverlay;
			}
		}
		else
		{
			pBoneInfo->iSeqIndex              =  0;
			pBoneInfo->iSeqAGUpdaterIndex     =  0;
			pBoneInfo->iOverlayIndex          = -1;
			pBoneInfo->iOverlayAGUpdaterIndex = -1;
			pBoneInfo->bIgnoreMasterOverlay   = false;
		}

		pNode->iCriticalBoneIndex = eaSize(&pData->eaAnimBoneInfos);
		eaPush(&pData->eaAnimBoneInfos, pBoneInfo);
	}
	else
	{
		pNode->iCriticalBoneIndex = -1;
	}

	return (!!pNode->uiCriticalChildren); 
}

static void dynTransformMultiplyWithPosRotation(const DynTransform* pA, const DynTransform* pB, DynTransform* pResult)
{
	Vec3 vTemp;
	quatMultiplyInline(pA->qRot, pB->qRot, pResult->qRot);
	addVec3(pA->vPos, pB->vPos, pResult->vPos);
	quatRotateVec3(pA->qRot, pResult->vPos, vTemp);
	copyVec3(vTemp, pResult->vPos);
	mulVecVec3(pA->vScale, pB->vScale, pResult->vScale);
}

bool dynDrawSkeletonCalculateNonSkinnedExtents(DynDrawSkeleton* pDrawSkel, bool bUnion)
{
	DynTransform rootTrans, invRootTrans;
	bool bSetExtents = false;
	
	if (!bUnion) {
		setVec3same(pDrawSkel->pSkeleton->vCurrentExtentsMax, -FLT_MAX);
		setVec3same(pDrawSkel->pSkeleton->vCurrentExtentsMin, FLT_MAX);
	}

	dynNodeGetWorldSpaceTransform(pDrawSkel->pSkeleton->pRoot, &rootTrans);
	dynTransformInverse(&rootTrans, &invRootTrans);
	setVec3same(invRootTrans.vScale, 1.0f);
	
	FOR_EACH_IN_EARRAY(pDrawSkel->eaDynGeos, DynDrawModel, pGeo)
	{
		if (pGeo->pModel &&
			(	!bUnion ||
				SAFE_MEMBER(pGeo->pAttachmentNode,uiNonSkinnedGeo)))
		{
			DynTransform geoTrans, geoTransChar;
			Mat4 geoMat, finalMat;
			Vec4_aligned vRotatedBounds[8];
			int i;
			dynNodeGetWorldSpaceTransform(pGeo->pAttachmentNode, &geoTrans);
			if (!bUnion) {
				dynTransformMultiplyWithPosRotation(&invRootTrans, &geoTrans, &geoTransChar);
			} else {
				Vec3 vTemp;
				dynTransformMultiply(&geoTrans, &invRootTrans, &geoTransChar);
				quatRotateVec3(invRootTrans.qRot, geoTransChar.vPos, vTemp);
				copyVec3(vTemp, geoTransChar.vPos);
			}
			dynTransformToMat4(&geoTransChar, geoMat);
			mulMat4(geoMat, pGeo->mTransform, finalMat);
			mulBounds(pGeo->pModel->min, pGeo->pModel->max, finalMat, vRotatedBounds);
			for (i=0; i<8;++i)
				vec3RunningMinMax(vRotatedBounds[i], pDrawSkel->pSkeleton->vCurrentExtentsMin, pDrawSkel->pSkeleton->vCurrentExtentsMax);
			bSetExtents = true;
		}
	}
	FOR_EACH_END;

	if (!bUnion)
	{
		if (!bSetExtents)
		{
			zeroVec3(pDrawSkel->pSkeleton->vCurrentExtentsMax);
			zeroVec3(pDrawSkel->pSkeleton->vCurrentExtentsMin);

			zeroVec3(pDrawSkel->pSkeleton->vCurrentGroupExtentsMax);
			zeroVec3(pDrawSkel->pSkeleton->vCurrentGroupExtentsMin);
		}
		else
		{
			copyVec3(pDrawSkel->pSkeleton->vCurrentExtentsMax, pDrawSkel->pSkeleton->vCurrentGroupExtentsMax);
			copyVec3(pDrawSkel->pSkeleton->vCurrentExtentsMin, pDrawSkel->pSkeleton->vCurrentGroupExtentsMin);

			dynSkeletonSetExtentsNode(pDrawSkel->pSkeleton);
		}
	}

	return bSetExtents;
}

// Change this to take some descriptor
DynDrawSkeleton* dynDrawSkeletonCreateDbg( DynSkeleton* pSkeleton, const WLCostume* pCostume, DynFxManager* pFxManager, bool bAutoDraw, bool bIsLocalPlayer, bool bDontCreateBodysock MEM_DBG_PARMS)
{
	DynDrawSkeleton* pDrawSkel;
	pDrawSkel = MP_ALLOC(DynDrawSkeleton);
	pDrawSkel->pSkeleton = pSkeleton;
	pSkeleton->pDrawSkel = pDrawSkel;
	pDrawSkel->fTotalAlpha = pDrawSkel->fFXDrivenAlpha = pDrawSkel->fEntityAlpha = pDrawSkel->fOtherAlpha = pDrawSkel->fGeometryOnlyAlpha = 1.0f;
	pDrawSkel->bWorldLighting = pCostume->bWorldLighting;
	pDrawSkel->bIsLocalPlayer = bIsLocalPlayer;
	pDrawSkel->bDontCreateBodysock = bDontCreateBodysock;

	if ( bAutoDraw )
		eaPush(&eaDrawSkelList, pDrawSkel);

	pDrawSkel->pFxManager = pFxManager;

	// Setup costume
	dynDrawSetupDrawInfo(pDrawSkel, false MEM_DBG_PARMS_CALL);
	
	// Setup any sub skeletons (wings, animated costume parts, etc.)
	FOR_EACH_IN_EARRAY_FORWARDS(pCostume->eaSubCostumes, WLSubCostume, pSub)
	{
		WLCostume* pSubCostume = GET_REF(pSub->hSubCostume);
		if (pSubCostume)
		{
			DynSkeleton* pSubSkeleton = NULL;
			DynDrawSkeleton* pSubDrawSkeleton;
			DynNode* pInBetween;
			DynNode *pAttachmentBone;
			bool bAlreadyHasInbetween;
			FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pDependentSkeleton)
			{
				if (GET_REF(pDependentSkeleton->hCostume) == pSubCostume)
				{
					pSubSkeleton = pDependentSkeleton;
					break;
				}
			}
			FOR_EACH_END;
			if (!pSubSkeleton) {
				pSubSkeleton = dynSkeletonCreate(pSubCostume, false, true, false, false, pSkeleton->bHeadshot, NULL);
				bAlreadyHasInbetween = false;
			} else {
				bAlreadyHasInbetween = pSubSkeleton->bOwnedByParent;
			}
			pSubDrawSkeleton = dynDrawSkeletonCreate(pSubSkeleton, pSubCostume, NULL, bAutoDraw, bIsLocalPlayer, pDrawSkel->bDontCreateBodysock);
			pSubDrawSkeleton->bIsLocalPlayer = pDrawSkel->bIsLocalPlayer;
			// We need an in between to prevent the skeletons from being treated as one during various future function calls, basically a node that doesn't have the 'critical' and 'skeleton' flags set between the two
			if (bAlreadyHasInbetween)
			{
				pInBetween = pSubSkeleton->pRoot->pParent;
				assert(pInBetween->pcTag == pcInbetweenSubs);
				//dynNodeClearParent(pInBetween); gets called when re-parenting shortly below
			}
			else
			{
				pInBetween = dynNodeAlloc();
				pInBetween->pcTag = pcInbetweenSubs;
				dynNodeParent(pSubSkeleton->pRoot, pInBetween);
			}
			pAttachmentBone = dynSkeletonFindNodeNonConst(pSkeleton, pSub->pcAttachmentBone);
			if (!pAttachmentBone) {
				DynBaseSkeleton *pBaseSkeleton = GET_REF(pSkeleton->hBaseSkeleton);
				Errorf("Unable to find attachment bone %s on parent skeleton with base file %s, attaching to the parent skeleton's root instead\n",
						pSub->pcAttachmentBone,
						pBaseSkeleton ? pBaseSkeleton->pcFileName : "[Couldn't find]");
				pAttachmentBone = pSkeleton->pRoot;
			}
			dynNodeParent(pInBetween,pAttachmentBone);
			dynSkeletonPushDependentSkeleton(pSkeleton, pSubSkeleton, true, false);
			eaPush(&pDrawSkel->eaSubDrawSkeletons, pSubDrawSkeleton);
			pSubSkeleton->bOwnedByParent = true;
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pDependentSkeleton)
	{
		// Make sure there are no extra, left-over sub skeletons that don't have costumes
		if (!pDependentSkeleton->pDrawSkel)
		{
			Errorf("Somehow found a sub skeleton without a draw skeleton. Removing.");
			dynDependentSkeletonFree(pDependentSkeleton);
			eaRemove(&pSkeleton->eaDependentSkeletons, ipDependentSkeletonIndex);
		}
	}
	FOR_EACH_END;

	if (gConf.bNewAnimationSystem) {
		dynSkeletonUpdateBoneVisibility(pSkeleton, NULL);		
	}
	
	if (pFxManager)
	{
		dynFxManSetCostume(pFxManager, pCostume);
		dynFxManSetDrawSkeleton(pFxManager, pDrawSkel);
		dynDrawSkeletonSetMaintainedFx(pDrawSkel);
	}

	// This is for STO
	if (pCostume->bAutoGlueUp)
	{
		pSkeleton->bDontUpdateExtents = true;
		dynDrawSkeletonGlueUpSkeletonToMatchMountPoints(pDrawSkel);
	}
	else if (GET_REF(pCostume->hSkelInfo) && GET_REF(pCostume->hSkelInfo)->bStatic)
		pSkeleton->bDontUpdateExtents = true;

	if (pSkeleton->bDontUpdateExtents)
	{
		dynNodeTreeForceRecalcuation(pDrawSkel->pSkeleton->pRoot);
		dynDrawSkeletonCalculateNonSkinnedExtents(pDrawSkel, false);
	}

	if (wl_state.create_dyn_light_cache_func)
	{
		Vec3 vWorldMid, vWorldMin, vWorldMax;
		dynNodeGetWorldSpacePos(pSkeleton->pRoot, vWorldMid);
		subVec3same(vWorldMid, pSkeleton->fStaticVisibilityRadius, vWorldMin);
		addVec3same(vWorldMid, pSkeleton->fStaticVisibilityRadius, vWorldMax);
		pDrawSkel->pLightCache = wl_state.create_dyn_light_cache_func(	worldRegionGetGraphicsData(worldGetWorldRegionByPos(vWorldMid)),
																		vWorldMin, vWorldMax, unitmat, 
																		pDrawSkel->bWorldLighting ? LCT_INTERACTION_ENTITY : LCT_CHARACTER);
	}

	return pDrawSkel;
}

static void dynDrawSkeletonFreeGeo(SA_PRE_NN_VALID SA_POST_FREE DynDrawModel* pGeo MEM_DBG_PARMS)
{
	int i;
	for (i=0; i<2; ++i)
	{
		int j;
		eaDestroy(&pGeo->eaTextureSwaps[i]);

		for(j=0; j<eaSize(&pGeo->eaMatConstant[i]); ++j) 
		{
			StructDestroy(parse_MaterialNamedConstant, pGeo->eaMatConstant[i][j]);
		}
		eaDestroy(&pGeo->eaMatConstant[i]);

		for(j=0; j<eaSize(&pGeo->eaCostumeTextureSwaps[i]); ++j) 
		{
			StructDestroy(parse_CostumeTextureSwap, pGeo->eaCostumeTextureSwaps[i][j]);
		}
		eaDestroy(&pGeo->eaCostumeTextureSwaps[i]);
	}

	if (pGeo->pCloth)
		dynClothObjectDelete(pGeo->pCloth);

	if (pGeo->pClothSavedState)
		dynClothObjectDestroySavedState(pGeo->pClothSavedState);

	dynDrawModelClearPreSwapped(pGeo);

	TSMP_FREE(DynDrawModel, pGeo);
}

static void dynDrawModelClearPreSwappedMempoolCallback(void *pool_UNUSED, void *data, void *userData_UNUSED)
{
	dynDrawModelClearPreSwapped((DynDrawModel*)data);
}

static void dynDrawModelClearPreSwapped(DynDrawModel *pGeo)
{
	if (pGeo->pDrawableList)
	{
		if (pGeo->bOwnsDrawables)
		{
			pGeo->bOwnsDrawables = 0;
			if (pGeo->pInstanceParamList)
				LOG_INSTANCE_FREE(pGeo);
			freeDrawableListDbg(pGeo->pDrawableList, false MEM_DBG_PARMS_INIT);
			freeInstanceParamList(pGeo->pInstanceParamList, false);
		}
		else
		{
			removeDrawableListRefDbg(pGeo->pDrawableList MEM_DBG_PARMS_INIT);
			removeInstanceParamListRef(pGeo->pInstanceParamList MEM_DBG_PARMS_INIT);
		}
		pGeo->pDrawableList = NULL;
		pGeo->pInstanceParamList = NULL;
	}
}

// Frees all pre-swapped character materials
AUTO_COMMAND ACMD_CATEGORY(Debug);
void dynDrawClearPreSwapped(void)
{
	threadSafeMemoryPoolForEachAllocationUNSAFE(&TSMP_NAME(DynDrawModel), dynDrawModelClearPreSwappedMempoolCallback, NULL);
}

static void dynDrawSkeletonFreeSingleGeo(SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkel, SA_PARAM_NN_VALID DynDrawModel* pGeo MEM_DBG_PARMS)
{
	int iGeoIndex = eaFindAndRemove(&pDrawSkel->eaDynGeos, pGeo);
	assert(iGeoIndex >= 0);
	if (pGeo->bCloth)
		eaFindAndRemove(&pDrawSkel->eaClothModels, pGeo);
	dynDrawSkeletonFreeGeo(pGeo MEM_DBG_PARMS_CALL);
}

static void dynDrawSkeletonFreeGeos( SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkel MEM_DBG_PARMS )
{
	FOR_EACH_IN_EARRAY(pDrawSkel->eaDynGeos, DynDrawModel, pGeo)
		dynDrawSkeletonFreeGeo(pGeo MEM_DBG_PARMS_CALL);
	FOR_EACH_END

	eaDestroy(&pDrawSkel->eaDynGeos);
	eaDestroy(&pDrawSkel->eaClothModels);
	pDrawSkel->eaDynGeos = NULL;
	pDrawSkel->eaClothModels = NULL;
	SAFE_FREE(pDrawSkel->puiBoneIdxPool);
}

void dynDrawSkeletonUpdateLODLevel( DynDrawSkeleton* pDrawSkel, bool bBodySock)
{
	const WLCostume* pCostume;
	if (pDrawSkel->bBodySock == bBodySock)
		return;

	pCostume = pDrawSkel->pSkeleton?GET_REF(pDrawSkel->pSkeleton->hCostume):NULL;

	if (pCostume && pCostume->bHasLOD && !pDrawSkel->bDontCreateBodysock)
	{
		pDrawSkel->bBodySock = bBodySock;
		pDrawSkel->bUpdateDrawInfo = !gConf.bNewAnimationSystem;
	}
}

void dynDrawSkeletonUpdateDrawInfoDbg( DynDrawSkeleton* pDrawSkel MEM_DBG_PARMS)
{
	dynDrawSkeletonFreeGeos(pDrawSkel MEM_DBG_PARMS_CALL);
	dynDrawSetupDrawInfo(pDrawSkel, true MEM_DBG_PARMS_CALL);
	pDrawSkel->pSkeleton->bForceTransformUpdate = true;
	pDrawSkel->bUpdateDrawInfo = false;
}

void dynDrawSkeletonUpdateCloth( DynDrawSkeleton* pDrawSkel, F32 fDeltaTime, Vec3 vDist, bool bMoving, bool bMounted)
{
	Vec3 vForwardVec;
	bool bCalculatedForwardInfo = false;

	FOR_EACH_IN_EARRAY(pDrawSkel->eaClothModels, DynDrawModel, pGeo) {

		if (!pGeo->pCloth && pGeo->pModel && pDrawSkel->pCurrentSkinningMatSet && pDrawSkel->pCurrentSkinningMatSet->pSkinningMats && !vec4IsZero(pDrawSkel->pCurrentSkinningMatSet->pSkinningMats[0][0])) // quick way to check if the skinning mats have been created yet
		{
			// Make sure ALL LODs are loaded before setting up cloth.
			bool allLODsLoaded = true;
			int lodNum;
			for(lodNum = 0; lodNum < eaSize(&pGeo->pModel->model_lods); lodNum++) {
				ModelLOD *model_lod = modelLoadLOD(pGeo->pModel, lodNum);
				if(!modelLODIsLoaded(model_lod)) {
					allLODsLoaded = false;
				}
			}

			if(allLODsLoaded) {

				// All LODs have been loaded. We can set up the cloth now.
				pGeo->pCloth = dynClothObjectSetupFromDynDrawModel(pGeo, pDrawSkel);

				if (!pGeo->pCloth) {
					dynDrawSkeletonFreeSingleGeo(pDrawSkel, pGeo MEM_DBG_PARMS_INIT);
				}
			}
		}

		if (pGeo->pCloth &&
			(	pDrawSkel->pSkeleton->ragdollState.bRagdollOn ||
				pDrawSkel->uiLODLevel <= (unsigned int)dynDebugState.cloth.iMaxLOD - 1 ||
				dynDebugState.cloth.bDisableNormalModelForLowLOD ||
				!dynDebugState.cloth.iMaxLOD))
		{
			Vec3 scaleOverride = {1, 1, 1};

			pGeo->pCloth->iCurrentLOD = (int)pDrawSkel->uiLODLevel-1;
			
			if(pGeo->pCloth->iCurrentLOD < 0) {
				pGeo->pCloth->iCurrentLOD = 0;
			}
			if(pGeo->pCloth->iCurrentLOD >= eaSize(&pGeo->pCloth->eaLODs)) {
				pGeo->pCloth->iCurrentLOD = eaSize(&pGeo->pCloth->eaLODs) - 1;
			}
			
			if (!bCalculatedForwardInfo)
			{
				Quat qRot;
				const DynNode* pHips = dynSkeletonFindNode(pDrawSkel->pSkeleton, "Hips");
				if (pHips)
				{
					dynNodeGetWorldSpaceRot(pHips, qRot);
				}
				else
					dynNodeGetWorldSpaceRot(pDrawSkel->pSkeleton->pRoot, qRot);
				quatRotateVec3(qRot, forwardvec, vForwardVec);
				bCalculatedForwardInfo = true;
			}

			dynNodeGetWorldSpaceScale(pGeo->pAttachmentNode, scaleOverride);

			if(pGeo->pCloth->iLastLOD == -1)
			{
				int i;

				// Do extra iterations of cloth just for the frame
				// it comes on-screen. Timesteps for these
				// iterations start large and become smaller.
				pGeo->pCloth->bQueueModelReset = false;
				for(i = 0; i < 4; i++) {
					dynClothObjectUpdate(pGeo->pCloth, 0.1 * (float)(4 - i), 0, vForwardVec, scaleOverride, false, bMounted, false, false, false, NULL);
					//printfColor(COLOR_GREEN, "%i : %f, %f\n", i+1, pGeo->pCloth->fMaxConstraintRatio, pGeo->pCloth->fMinConstraintRatio);
					//if (gConf.bNewAnimationSystem &&
					//	-0.5f < pGeo->pCloth->fMinConstraintRatio &&
					//	pGeo->pCloth->fMaxConstraintRatio < 1.f)
					//{
					//	break;
					//}
				}
			}
			else if(pDrawSkel->pSkeleton->ragdollState.bRagdollOn &&
					!pDrawSkel->pSkeleton->bSleepingClientSideRagdoll)
			{
				//do an extra update for ragdolls that are active in physx since the cloth can get a little jerky
				dynClothObjectUpdate(pGeo->pCloth, 0.4f, 0, vForwardVec, scaleOverride, false, bMounted, false, false, false, NULL);
			}
			else if(gConf.bNewAnimationSystem &&
					(	pGeo->pCloth->bQueueModelReset ||
						(	!pDrawSkel->pSkeleton->bUnmanaged &&
							pDrawSkel->pSkeleton->bVisible &&
							!pDrawSkel->pSkeleton->bWasVisible)))
			{
				//do extra updates for cloth that went off screen and stopped simulating then re-appears
				//or whenever the static cloth model was swapped for the dynamic version
				int i;
				pGeo->pCloth->bQueueModelReset = false;
				for(i = 0; i < 4; i++) {
					dynClothObjectUpdate(pGeo->pCloth, 0.1 * (float)(4 - i), 0, vForwardVec, scaleOverride, false, bMounted, true, false, false, NULL);
					//printfColor(COLOR_BLUE, "%i : %f, %f\n", i+1, pGeo->pCloth->fMaxConstraintRatio, pGeo->pCloth->fMinConstraintRatio);
					//if (-0.5f < pGeo->pCloth->fMinConstraintRatio &&
					//	pGeo->pCloth->fMaxConstraintRatio < 1.f)
					//{
					//	break;
					//}
				}
			}

			dynClothObjectUpdate(pGeo->pCloth, fDeltaTime, dotVec3(vDist, vForwardVec), vForwardVec, scaleOverride, bMoving, bMounted, false, false, false, NULL);

			if(pGeo->pCloth && pGeo->pClothSavedState) {
				dynClothObjectRestoreState(pGeo->pCloth, pGeo->pClothSavedState);
				dynClothObjectDestroySavedState(pGeo->pClothSavedState);
				pGeo->pClothSavedState = NULL;
				dynClothObjectUpdate(pGeo->pCloth, 0.1, dotVec3(vDist, vForwardVec), vForwardVec, scaleOverride, bMoving, bMounted, false, false, false, NULL);
			}

			if (pDrawSkel->pSkeleton == dynDebugState.pDebugSkeleton ||
				(	pDrawSkel->pSkeleton->bRider &&
					//assuming pDrawSkel->pSkeleton->pParentSkeleton &&
					//assuming pDrawSkel->pSkeleton->pParentSkeleton->bMount &&
					pDrawSkel->pSkeleton->pParentSkeleton == dynDebugState.pDebugSkeleton)) //safe to check when assumptions are false
			{
				dynDebugState.cloth.pDebugClothObject = pGeo->pCloth;
			}
		}

	} FOR_EACH_END
}


static void dynDrawVisibilityNodeFree(DynDrawVisibilityNode* pVisNode)
{
	TSMP_FREE(DynDrawVisibilityNode, pVisNode);
}

void dynDrawSkeletonFreeDbg( DynDrawSkeleton* pToFree MEM_DBG_PARMS)
{
	if (pToFree == pDebugDrawSkeleton)
		pDebugDrawSkeleton = NULL;
	eaDestroyEx(&pToFree->eaDynFxRefs, dynFxReferenceFree);

	eaDestroyEx(&pToFree->eaSubDrawSkeletons, dynDrawSkeletonFreeCB);

	eaDestroyEx(&pToFree->eaVisNodes, dynDrawVisibilityNodeFree);

	eaFindAndRemoveFast(&eaDrawSkelList, pToFree);
	dynDrawSkeletonFreeGeos(pToFree MEM_DBG_PARMS_CALL);
	if (pToFree->pSkeleton)
		pToFree->pSkeleton->pDrawSkel = NULL;
	if (pToFree->guid)
		dynDrawSkeletonRemoveGuid(pToFree->guid);
	if (pToFree->pLightCache && wl_state.free_dyn_light_cache_func)
		wl_state.free_dyn_light_cache_func(pToFree->pLightCache);
	if (pToFree->pSplatShadow && wl_state.gfx_splat_destroy_callback)
		wl_state.gfx_splat_destroy_callback(pToFree->pSplatShadow);

	FOR_EACH_IN_EARRAY(pToFree->eaAOSplats,DynDrawSkeletonAOSplat, skelSplat)
	{
		if (skelSplat->pAOSplat && wl_state.gfx_splat_destroy_callback)
			wl_state.gfx_splat_destroy_callback(skelSplat->pAOSplat);
	}
	FOR_EACH_END

	eaDestroy(&pToFree->eaAOSplats);

	eaDestroy(&pToFree->eaSeveredBones);
	eaDestroy(&pToFree->eaHiddenBoneVisSetBones);
	eaDestroy(&pToFree->eaHiddenBoneVisSetBonesOld);

	eaDestroy(&pToFree->preload.eaTextures);
	eaDestroy(&pToFree->preload.eaModels);

	dynSkinningMatSetDecrementRefCount(pToFree->pSkinningMatSets[0]);
	dynSkinningMatSetDecrementRefCount(pToFree->pSkinningMatSets[1]);
	if (pToFree->pCurrentSkinningMatSet)
		dynSkinningMatSetDecrementRefCount(pToFree->pCurrentSkinningMatSet);
	MP_FREE(DynDrawSkeleton, pToFree);
}

void dynDrawSkeletonFreeCB(DynDrawSkeleton* pToFree)
{
	dynDrawSkeletonFreeDbg(pToFree MEM_DBG_PARMS_INIT);
}

U32 dynDrawGetSkeletonList(DynDrawModel** ppDrawGeos, U32 uiMaxToReturn)
{
	// For now just copy the earray from above, later we will do proper culling, etc.
	U32 uiNumSkels = eaDrawSkelList?eaUSize(&eaDrawSkelList):0;
	U32 uiTotalGeos = 0;
	U32 uiIndex;

	for (uiIndex=0; uiIndex<uiNumSkels; ++uiIndex)
	{
		U32 uiNumGeos = eaDrawSkelList[uiIndex]->eaDynGeos?eaSize(&eaDrawSkelList[uiIndex]->eaDynGeos):0;
		U32 uiGeoIndex;
		for (uiGeoIndex=0; uiGeoIndex<uiNumGeos; ++uiGeoIndex)
		{
			assert(uiTotalGeos < ARRAY_SIZE(ppDrawGeos));
			ppDrawGeos[uiTotalGeos++] = eaDrawSkelList[uiIndex]->eaDynGeos[uiGeoIndex];
			if ( uiTotalGeos == uiMaxToReturn )
				return uiTotalGeos;
		}
	}

	return uiTotalGeos;
}


void dynDrawSkeletonClearCostumeBodysock(DynDrawSkeleton * pDrawSkel)
{
	WLCostume *pCostume;
	if (!pDrawSkel->pSkeleton)
		return;

	pCostume = GET_REF(pDrawSkel->pSkeleton->hCostume);
	if (pCostume)
	{
		if (pCostume->pBodysockTexture)
		{
			if (pDrawSkel->eaDynGeos && pDrawSkel->eaDynGeos[0])
			{
				DynDrawModel *pGeo = pDrawSkel->eaDynGeos[0];
				pGeo->bHasBodysockSwapSet = false;
				eaDestroy(&pGeo->eaTextureSwaps[0]);
				if (pGeo->pDrawableList && pGeo->bOwnsDrawables)
					dynDrawModelClearPreSwapped(pGeo);
			}

			wl_state.gfx_bodysock_texture_release_callback(pCostume->pBodysockTexture, pCostume->iBodysockSectionIndex);

			// clear the bodysock texture reference
			pCostume->iBodysockSectionIndex = 0;
			pCostume->pBodysockTexture = NULL;
			pCostume->bBodysockTexCreated = false;
		}
	}

}

void dynDrawClearCostumeBodysocks()
{
	U32 uiIndex;
	U32 uiNumSkels = eaSize(&eaDrawSkelList);

	for (uiIndex=0; uiIndex<uiNumSkels; ++uiIndex)
		dynDrawSkeletonClearCostumeBodysock(eaDrawSkelList[uiIndex]);
}

static DynDrawModel* dynAttachModelToBone( SA_PARAM_NN_VALID Model* pModel, const char* pcMaterial, const char* pcMaterial2, const WLCostumePart* pCostumePart, SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkel, const char* pcBoneName, const DynBaseSkeleton* pBaseSkeleton, DynNode*** peaNodes, BoneIdxAlloc* pTempBoneData, bool *bErrored MEM_DBG_PARMS) 
{
	DynDrawModel* pGeo = dynDrawGeoCreate();
	U32 uiNumLOD0Bones = 0;

	pGeo->pModel = pModel;

	pGeo->pcMaterial[0] = pcMaterial;
	pGeo->pcMaterial[1] = pcMaterial2;

	pGeo->uiRequiredLOD = pCostumePart->uiRequiredLOD;

	pGeo->pcOrigAttachmentBone = pCostumePart->pcOrigAttachmentBone;

	if (pGeo->pcOrigAttachmentBone == pcHair)
	{
		pGeo->fSortBias = 2.0f;
	}

	if ( pCostumePart)
	{
		int j;

		pGeo->bLOD = pCostumePart->bLOD;

		// Need to clone data instead of having pointer into data that might go away
		pGeo->eaMatConstant[0] = NULL;
		for(j=0; j<eaSize(&pCostumePart->eaMatConstant); ++j) 
		{
			eaPush(&pGeo->eaMatConstant[0], StructClone(parse_MaterialNamedConstant, pCostumePart->eaMatConstant[j]));
		}

		// Need to clone data instead of having pointer into data that might go away
		pGeo->eaCostumeTextureSwaps[0] = NULL;
		for(j=0; j<eaSize(&pCostumePart->eaTextureSwaps); ++j) 
		{
			eaPush(&pGeo->eaCostumeTextureSwaps[0], StructClone(parse_CostumeTextureSwap, pCostumePart->eaTextureSwaps[j]));
		}

		pGeo->eaMatConstant[1] = NULL;
		pGeo->eaCostumeTextureSwaps[1] = NULL;

		pGeo->bRaycastable = pCostumePart->bRaycastable;

		// If there is a second material, copy over those tex swaps and constants
		if (pGeo->pcMaterial[1] && pCostumePart->pSecondMaterialInfo)
		{
			for(j=0; j<eaSize(&pCostumePart->pSecondMaterialInfo->eaMatConstant); ++j) 
			{
				eaPush(&pGeo->eaMatConstant[1], StructClone(parse_MaterialNamedConstant, pCostumePart->pSecondMaterialInfo->eaMatConstant[j]));
			}

			// Need to clone data instead of having pointer into data that might go away
			for(j=0; j<eaSize(&pCostumePart->pSecondMaterialInfo->eaTextureSwaps); ++j) 
			{
				eaPush(&pGeo->eaCostumeTextureSwaps[1], StructClone(parse_CostumeTextureSwap, pCostumePart->pSecondMaterialInfo->eaTextureSwaps[j]));
			}

		}

		if (memcmp(pCostumePart->mTransform, zeromat, sizeof(Mat4))==0)
			copyMat4(unitmat, pGeo->mTransform);
		else
			copyMat4(pCostumePart->mTransform, pGeo->mTransform);

		pGeo->pDrawableList = pCostumePart->pWorldDrawableList;
		pGeo->pInstanceParamList = pCostumePart->pInstanceParamList;
		pGeo->iDrawableResetCounter = worldGetResetCount(true); // protect against dereferencing freed drawables
		pGeo->bNoShadow = pCostumePart->bNoShadow;
		if (pCostumePart->pWorldDrawableList)
			addDrawableListRef(pCostumePart->pWorldDrawableList MEM_DBG_PARMS_CALL);
		if (pCostumePart->pInstanceParamList)
			addInstanceParamListRef(pGeo->pInstanceParamList MEM_DBG_PARMS_CALL);

		if (pGeo->pInstanceParamList)
			LOG_INSTANCE_SETUP(pGeo);
	}
	else
	{
		copyMat4(unitmat, pGeo->mTransform);
	}

	pGeo->pAttachmentNode = dynSkeletonFindNode(pDrawSkel->pSkeleton, pcBoneName);

	{
		U32 uiNumLODs = pModel->lod_info?eaSize(&pModel->lod_info->lods):1;
		U32 uiLODIndex;

		assert(uiNumLODs <= MAX_DRAW_MODEL_LODS);
		pGeo->uiNumLODLevels = uiNumLODs;

		for (uiLODIndex=0; uiLODIndex<uiNumLODs; ++uiLODIndex)
		{
			Model* pCurrentModel;
			bool bUnique = false;
			if (uiLODIndex == 0)
			{
				pCurrentModel = pModel;
				bUnique = true;
			}
			else
			{
				ModelLOD* pLOD = modelGetLOD(pModel, uiLODIndex);
				pCurrentModel = pLOD?pLOD->model_parent:pModel;
				if (pCurrentModel != pModel)
					bUnique = true;
			}

			if (bUnique)
			{
				int iNumBones = MIN(pCurrentModel->header?eaSize(&pCurrentModel->header->bone_names):0, MAX_OBJBONES);
				int j;
				pGeo->apuiBoneIdxs[uiLODIndex] = boneIdxAllocate(pTempBoneData, iNumBones);
				if (!pGeo->apuiBoneIdxs[uiLODIndex])
				{
					Errorf("Exceeded maximum bone index size %d with allocation of %d bones on model %s", pTempBoneData->uiMaxIdxs, iNumBones, pModel->name);
					dynDrawSkeletonFreeGeo(pGeo MEM_DBG_PARMS_CALL);
					*bErrored = true;
					return NULL;
				}
				if (uiLODIndex == 0)
					uiNumLOD0Bones = iNumBones;

				for (j=0; j<iNumBones; ++j)
				{
					const char* pcSkinBoneName = pCurrentModel->header->bone_names[j];
					DynNode *pNode = NULL;
					int iNodeIdx;
					bool bIsCloth = false;
					bool bAttachmentNodeSkinned = false;

					// First check to see if it's a cloth piece. If not, fall back on a normal bone name lookup.
					if(!strcmp(pcSkinBoneName, "Cloth")) {
						bIsCloth = true;
					} else {
						pNode = dynSkeletonFindNodeNonConst(pDrawSkel->pSkeleton, pcSkinBoneName);
					}

					if (uiLODIndex == 0 && !pGeo->pAttachmentNode) {
						pGeo->pAttachmentNode = pNode;
						bAttachmentNodeSkinned = true;
					}

					if ( !pNode )
					{
						// Cloth is a special case node, which triggers the cloth system.
						if (bIsCloth)
						{
							if (!pGeo->pAttachmentNode) {
								Errorf("Skeleton %s missing geo requested attachment node %s, skipping out of %s", pBaseSkeleton->pcName, bAttachmentNodeSkinned ? pcSkinBoneName : pcBoneName, __FUNCTION__);
								dynDrawSkeletonFreeGeo(pGeo MEM_DBG_PARMS_CALL);
								*bErrored = true;
								return NULL;
							}
							pGeo->bCloth = true;
							pNode = (DynNode*)pGeo->pAttachmentNode;
							pcSkinBoneName = pGeo->pAttachmentNode->pcTag;
							pGeo->pcClothInfo = pCostumePart->pcClothInfo;
							pGeo->pcClothColInfo = pCostumePart->pcClothColInfo;
						}
						else
						{
							Errorf("Failed to find referenced bone %s in skeleton %s in model %s in file %s", pGeo->pModel->header->bone_names[j], pBaseSkeleton->pcName, pGeo->pModel->name, pGeo->pModel->header->filename);
							dynDrawSkeletonFreeGeo(pGeo MEM_DBG_PARMS_CALL);
							*bErrored = true;
							return NULL;
						}
					}

					iNodeIdx = eaFind(peaNodes, pNode);
					if (iNodeIdx < 0)
					{
						const DynNode *pBaseNode = dynBaseSkeletonFindNode(pBaseSkeleton, pcSkinBoneName);
						if ( !pBaseNode )
						{
							Errorf("Failed to find referenced base bone %s in skeleton %s", pGeo->pModel->header->bone_names[j], pBaseSkeleton->pcName);
							dynDrawSkeletonFreeGeo(pGeo MEM_DBG_PARMS_CALL);
							*bErrored = true;
							return NULL;
						}

						iNodeIdx = eaPush(peaNodes, pNode);

						dynNodeSetCriticalBit(pNode);
						pNode->uiSkinningBone = 1;
					}

					assert(iNodeIdx >= 0);
					pGeo->apuiBoneIdxs[uiLODIndex][j] = (U8)iNodeIdx;
				}
				pGeo->uiNumNodesUsed[uiLODIndex] = iNumBones;
			}
			else
			{
				pGeo->apuiBoneIdxs[uiLODIndex] = NULL;
				pGeo->uiNumNodesUsed[uiLODIndex] = pGeo->uiNumNodesUsed[0];
			}
		}
	}


	if (uiNumLOD0Bones < 2 && pGeo->pModel)
	{
#if USE_SPU_ANIM
        const char* pcSkinBoneName;
		if (uiNumLOD0Bones == 0)
		{
			pcSkinBoneName = pGeo->pAttachmentNode->pcTag;
		}
		else if (pGeo->pModel->header->bone_names[0])
		{
			pcSkinBoneName = allocAddString(pGeo->pModel->header->bone_names[0]);
		}
        DynNode *pNode = dynSkeletonFindNodeNonConst(pDrawSkel->pSkeleton, pcSkinBoneName);
        if(pNode)
            pNode->fRadius = pGeo->pModel->radius;
#endif
		if (uiNumLOD0Bones == 0)
		{
			if (pGeo->pAttachmentNode) {
				dynDrawSkeletonAddVisibilityNode(pDrawSkel, pGeo->pAttachmentNode->pcTag, pGeo->pModel);
				if (gConf.bNewAnimationSystem) {
					((DynNode*)pGeo->pAttachmentNode)->uiNonSkinnedGeo = 1;
					dynNodeSetCriticalBit((DynNode*)pGeo->pAttachmentNode);
				}
			} else {
				Errorf("Skeleton %s missing geo requested attachment node %s, skipping out of %s", pBaseSkeleton->pcName, pcBoneName, __FUNCTION__);
				dynDrawSkeletonFreeGeo(pGeo MEM_DBG_PARMS_CALL);
				*bErrored = true;
				return NULL;
			}
		}
		else if (pGeo->pModel->header->bone_names[0])
		{
			dynDrawSkeletonAddVisibilityNode(pDrawSkel, allocAddString(pGeo->pModel->header->bone_names[0]), pGeo->pModel);
		}
	}

	// Set up the base attachment offset
	{
		const DynNode* pBaseNode;
		pBaseNode = dynBaseSkeletonFindNode(pBaseSkeleton, pcBoneName);
		if (!pBaseNode)
		{
			Errorf("Failed to find referenced base bone %s in skeleton %s", pcBoneName, pBaseSkeleton->pcName);
			dynDrawSkeletonFreeGeo(pGeo MEM_DBG_PARMS_CALL);
			*bErrored = true;
			return NULL;
		}

		dynNodeGetWorldSpacePos(pBaseNode, pGeo->vBaseAttachOffset);
	}

	pGeo->pBaseSkeleton = pBaseSkeleton;


	eaPush(&pDrawSkel->eaDynGeos, pGeo);
	return pGeo;
}

static void dynAttachGeoToBone(SA_PARAM_NN_VALID const char *pcFilename, SA_PARAM_NN_VALID const char* pcCostumeName, SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkel, SA_PARAM_OP_VALID ModelHeaderSet* pSet, const char* pcModel, const WLCostumePart* pCostumePart, const char* pcMaterial, const char* pcMaterial2, DynNode*** peaNodes, BoneIdxAlloc* pTempBones, bool *bErrored MEM_DBG_PARMS)
{
	const char* pcBoneName = pCostumePart->pchBoneName;
	int i;
	const DynBaseSkeleton* pBaseSkeleton = GET_REF(pDrawSkel->pSkeleton->hBaseSkeleton);
	Model* pAttachModel = NULL;

	if (pCostumePart && (!!pDrawSkel->bBodySock) != (!!pCostumePart->bLOD) && !gConf.bNewAnimationSystem)
		return;

	// First, see if there is a model specified
	if (pcModel)
	{
		pAttachModel = modelFindEx(SAFE_MEMBER(pSet, filename), pcModel, false, WL_FOR_ENTITY);
		if (!pAttachModel && !pSet)
		{
			ErrorFilenamef(pcFilename, "Costume Error: Failed to find costume part %s in costume %s, no skin will be present", pcModel, pcCostumeName);
		}
	}


	for (i=0; !pAttachModel && pSet && i<eaSize(&pSet->model_headers); ++i)
	{
		ModelHeader *pModelHeader = pSet->model_headers[i];
		if ( eaSize(&pModelHeader->bone_names) > 0 && stricmp(pModelHeader->attachment_bone, pcBoneName) == 0)
		{
			// Found it
			pAttachModel = modelFromHeader(pModelHeader, false, WL_FOR_ENTITY);
		}
	}

	// Use first model if you can't find one for this part.
	if (!pAttachModel && pCostumePart && pSet && eaSize(&pSet->model_headers) > 0)
	{
		pAttachModel = modelFromHeader(pSet->model_headers[0], false, WL_FOR_ENTITY);
	}

	if (pAttachModel)
	{
		DynDrawModel* pGeo = dynAttachModelToBone(pAttachModel, pcMaterial, pcMaterial2, pCostumePart, pDrawSkel, pcBoneName, pBaseSkeleton, peaNodes, pTempBones, bErrored MEM_DBG_PARMS_CALL);
		if (pGeo && pGeo->bCloth)
			eaPush(&pDrawSkel->eaClothModels, pGeo);
	}
}

static bool dynDrawSkeletonParentBoneIsSeveredOrHidden(const char* pcSeveredBone, const DynNode* pBone)
{
	if (pBone)
		pBone = pBone->pParent;
	while (pBone)
	{
		if (pBone->pcTag == pcSeveredBone)
		{
			return true;
		}
		pBone = pBone->pParent;
	}
	return false;
}

static void dynDrawSkeletonProcessCostumePart(SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkel, const WLCostumePart* pCostumePart, const char* pcCostumeName, ModelHeaderSet *pDefaultModelSet, DynNode*** peaNodes, BoneIdxAlloc* pTempBones, const char* pcFilename, bool *bErrored MEM_DBG_PARMS) 
{
	ModelHeaderSet *pSet;
	const char* pcMaterial[2];
	const char* pcModel = pCostumePart->pcModel;
	bool bSevered = false, bHidden = false;
	const DynNode* pBone = dynSkeletonFindNode(pDrawSkel->pSkeleton, pCostumePart->pcOrigAttachmentBone);
	pcMaterial[0] = pcMaterial[1] = NULL;
	
	// Check to see if we've been severed
	FOR_EACH_IN_EARRAY(pDrawSkel->eaSeveredBones, const char, pcSeveredBone)
	{
		if (pCostumePart->pcOrigAttachmentBone == pcSeveredBone)
			bSevered = true;
		// Don't attach this part if the parent bone is hidden
		else if (dynDrawSkeletonParentBoneIsSeveredOrHidden(pcSeveredBone, pBone))
			return;
	}
	FOR_EACH_END;
	//or hidden
	FOR_EACH_IN_EARRAY(pDrawSkel->eaHiddenBoneVisSetBones, const char, pcHiddenBone)
	{
		if (pCostumePart->pcOrigAttachmentBone == pcHiddenBone)
			bHidden = true;
		// Don't attach this part if the parent bone is hidden
		else if (dynDrawSkeletonParentBoneIsSeveredOrHidden(pcHiddenBone, pBone))
			return;
	}
	FOR_EACH_END;

	if ( bSevered && pCostumePart->pcStumpGeo )
	{
		pSet = modelHeaderSetFind(pCostumePart->pcStumpGeo);
		pcModel = pCostumePart->pcStumpModel;
		if (!pSet && strStartsWith(pCostumePart->pcStumpGeo, "character_library"))
		{
			ErrorFilenamef(pcFilename, "Costume Error: Failed to find geometry file %s in costume %s, no skin will be present", pCostumePart->pcStumpGeo, pcCostumeName);
			*bErrored = true;
			return;
		}
	}
	else if (bSevered || bHidden)
	{
		return; // no stump geo, but it's severed, so don't draw it. for armor and other pieces attached to the same bone
	}
	else if ( pCostumePart->pchGeometry )
	{
		pSet = modelHeaderSetFind(pCostumePart->pchGeometry);
		if (!pSet && strStartsWith(pCostumePart->pchGeometry, "character_library"))
		{
			ErrorFilenamef(pcFilename, "Costume Error: Failed to find geometry file %s in costume %s, no skin will be present", pCostumePart->pchGeometry, pcCostumeName);
			*bErrored = true;
			return;
		}
	}
	else
		pSet = pDefaultModelSet;

	if ( pCostumePart->pchMaterial )
	{
		//pMaterial[0] = materialFindNoDefault(pCostumePart->pchMaterial, WL_FOR_ENTITY);

		if ( materialExists(pCostumePart->pchMaterial) )
		{
			pcMaterial[0] = pCostumePart->pchMaterial;
		}
		else
		{
			Errorf("dynDraw: Failed to find material %s on model %s on bone %s in costume %s", pCostumePart->pchMaterial, SAFE_MEMBER(pCostumePart->pCachedModel, name), pCostumePart->pcOrigAttachmentBone, pcCostumeName);
		}
	}

	if ( pCostumePart->pSecondMaterialInfo  && pCostumePart->pSecondMaterialInfo->pchMaterial )
	{
		//pMaterial[1] = materialFindNoDefault(pCostumePart->pSecondMaterialInfo->pchMaterial, WL_FOR_ENTITY);

		if ( materialExists(pCostumePart->pSecondMaterialInfo->pchMaterial) )
		{
			pcMaterial[1] = pCostumePart->pSecondMaterialInfo->pchMaterial;
		}
		else
		{
			Errorf("dynDraw: Failed to find material %s on model %s on bone %s in costume %s", pCostumePart->pSecondMaterialInfo->pchMaterial, SAFE_MEMBER(pCostumePart->pCachedModel, name), pCostumePart->pcOrigAttachmentBone, pcCostumeName);
		}
	}

	dynAttachGeoToBone(pcFilename, pcCostumeName, pDrawSkel, pSet, pcModel, pCostumePart, pcMaterial[0], pcMaterial[1], peaNodes, pTempBones, bErrored MEM_DBG_PARMS_CALL);
}

const char* pcAlwaysCriticalBones[] = 
{
	"WepR",
	"WepL",
	"Emit_R",
	"Emit_L",
	"BendMystic",
	"Mystic",
	"Base",
	"FX_RootScale",
	"Ride",
	"Vehicle",
	"Shield_L",
	"HeadShot",
	"Target_HandL",
	"Target_HandR",
	"SwipeR",
	"SwipeL",
	"Mount_Back_R",
	"Mount_Back_L",
	"Mount_Side_R",
	"Mount_Side_L",
	"HandGrip_L",
	"HandGrip_R",
	"WingLWeapon",
	"WingRWeapon",
	"HullWeapon",
	"HullWeapon2"
};

				
void dynDrawSkeletonSetupBodysockTexture(WLCostume* pMyCostume)
{
	pMyCostume->bBodysockTexCreated = true;
	pMyCostume->pBodysockTexture = wl_state.gfx_bodysock_texture_create_callback(pMyCostume, &pMyCostume->iBodysockSectionIndex, pMyCostume->vBodysockTexXfrm);
}

void dynDrawSetupAnimBoneInfo(DynSkeleton *pSkeleton, bool bDontCreateBodysock, bool bThreaded)
{
	SetupAnimBoneData data;
	WLCostume *pMyCostume;
	SkelInfo *pSkelInfo;
	data.eaAnimBoneInfos = NULL;
	data.pScaleCollection = &pSkeleton->scaleCollection;
	data.pUnscaledBaseSkeleton = GET_REF(pSkeleton->hBaseSkeleton);

	// Set up references
	if ((pMyCostume = GET_REF(pSkeleton->hCostume)) && (pSkelInfo = GET_REF(pMyCostume->hSkelInfo)))
		data.pBlendInfo = GET_REF(pSkelInfo->hBlendInfo);

	// Process the skeleton tree, pre-processing data and storing it on setupAnimBoneInfo
	dynNodeProcessTree(pSkeleton->pRoot, setupAnimBoneInfo, &data);

	// Copy the data onto pSkeleton->pAnimBoneInfos
	SAFE_FREE(pSkeleton->pAnimBoneInfos);

	pSkeleton->uiNumAnimBoneInfos = eaUSize(&data.eaAnimBoneInfos);
	pSkeleton->pAnimBoneInfos = calloc(sizeof(DynAnimBoneInfo), pSkeleton->uiNumAnimBoneInfos);

	FOR_EACH_IN_EARRAY_FORWARDS(data.eaAnimBoneInfos, DynAnimBoneInfo, pBoneInfo)
	{
		memcpy(&pSkeleton->pAnimBoneInfos[ipBoneInfoIndex], pBoneInfo, sizeof(DynAnimBoneInfo));
		ScratchFree(pBoneInfo);
	}
	FOR_EACH_END;

	// Destroy temp data
	eaDestroy(&data.eaAnimBoneInfos);

	if (pMyCostume && pMyCostume->pBodySockInfo && pMyCostume->pBodySockInfo->pcBodySockGeo && !bDontCreateBodysock)
	{
		if (!pMyCostume->pBodysockTexture && !pMyCostume->bBodysockTexCreated)
		{
			if (bThreaded)
				dynSkeletonQueueBodysockUpdate(pSkeleton, pMyCostume);
			else
				dynDrawSkeletonSetupBodysockTexture(pMyCostume);
		}
	}
}

static void dynDrawSetupDrawInfo(SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkel, bool bThreaded MEM_DBG_PARMS)
{
	// Look up geo file
	ModelHeaderSet* pDefaultSet=0;
	const WLCostume* pCostume = GET_REF(pDrawSkel->pSkeleton->hCostume);
	const DynBaseSkeleton* pBaseSkeleton = GET_REF(pDrawSkel->pSkeleton->hBaseSkeleton);
	DynNode** eaNodes = NULL;
	DynNode** eaAttachmentNodes = NULL;
	BoneIdxAlloc tempBones;
	BoneIdxAlloc permBones;
	bool bErrored = false;
	tempBones.uiMaxIdxs = eaSize(&pCostume->eaCostumeParts) * MAX_OBJBONES * MAX_DRAW_MODEL_LODS;
	tempBones.pBoneIdxs = ScratchAlloc(sizeof(U8) * tempBones.uiMaxIdxs);
	tempBones.uiCurrentBoneIdx = 0;

	// Error checking
 	if (!pBaseSkeleton)
	{
		FatalErrorf("Could not find base skeleton, the reference dictionary should have restarted this system to prevent this!");
		ScratchFree(tempBones.pBoneIdxs);
		return;
	}
	if (!pCostume)
	{
		Errorf("dynDrawSetupDrawInfo: No costume found");
		ScratchFree(tempBones.pBoneIdxs);
		return;
	}
	else if (!pDrawSkel->pSkeleton)
	{
		Errorf("dynDrawSetupDrawInfo: No skeleton found");
		ScratchFree(tempBones.pBoneIdxs);
		return;
	}

	// Init
	dynNodeClearCriticalBits(pDrawSkel->pSkeleton->pRoot);
	eaClearEx(&pDrawSkel->eaVisNodes, dynDrawVisibilityNodeFree);

	// Process each geometry part, which also sets the critical and skinning bits on the skeleton (for those bones that have costume parts skinned to them)
	FOR_EACH_IN_EARRAY(pCostume->eaCostumeParts, const WLCostumePart, pCostumePart)
	{
		if ( pCostumePart && !pCostumePart->bCollisionOnly ) // we found a costume part for this bone
		{
			DynNode* pAttachmentNode;
			dynDrawSkeletonProcessCostumePart(pDrawSkel, pCostumePart, pCostume->pcName, pDefaultSet, &eaNodes, &tempBones, pCostume->pcFileName, &bErrored MEM_DBG_PARMS_CALL);
			pAttachmentNode = dynSkeletonFindNodeNonConst(pDrawSkel->pSkeleton, pCostumePart->pchBoneName);
			if (pAttachmentNode)
				eaPush(&eaAttachmentNodes, pAttachmentNode);
			else
			{
				Errorf("Failed to find attachment bone %s in skeleton %s, for costume part %s!", pCostumePart->pchBoneName, GET_REF(pDrawSkel->pSkeleton->hBaseSkeleton)->pcName, pCostumePart->pchGeometry);
			}

		}
	}
	FOR_EACH_END;

	// Now that we know the correct bone counts, make a new permanent allocation
	permBones.uiMaxIdxs = tempBones.uiCurrentBoneIdx;
	pDrawSkel->puiBoneIdxPool = permBones.pBoneIdxs = malloc(sizeof(U8) * tempBones.uiCurrentBoneIdx);
	permBones.uiCurrentBoneIdx = 0;

	// Set the critical bits on any bones that are special cases
	{
		const SkelInfo *pSkelInfo = pCostume ? GET_REF(pCostume->hSkelInfo) : NULL;
		const DynCriticalNodeList *pCriticalNodeList = pSkelInfo ? GET_REF(pSkelInfo->hCritNodeList) : NULL;

		//apply any force overrides
		if (pCriticalNodeList)
		{
			FOR_EACH_IN_EARRAY(pCriticalNodeList->eaCriticalNode, const char, pcNodeName)
			{
				DynNode *pForceCritNode = dynSkeletonFindNodeNonConst(pDrawSkel->pSkeleton, pcNodeName);
				if (pForceCritNode) dynNodeSetCriticalBit(pForceCritNode);
			}
			FOR_EACH_END;
		}
		else
		{
			//no force overrides were set, apply the old original list, at some point I'd like this to no longer run on the new animation system
			int i;
			for (i=0; i< ARRAY_SIZE(pcAlwaysCriticalBones); ++i)
			{
				DynNode* pNode = dynSkeletonFindNodeNonConst(pDrawSkel->pSkeleton, pcAlwaysCriticalBones[i]);
				if (pNode)
					dynNodeSetCriticalBit(pNode);
			}
		}

		//make sure that tails and such are hooked through
		FOR_EACH_IN_EARRAY(pCostume->eaSubCostumes, WLSubCostume, pSub)
		{
			DynNode* pNode = dynSkeletonFindNodeNonConst(pDrawSkel->pSkeleton, pSub->pcAttachmentBone);
			if (pNode)
				dynNodeSetCriticalBit(pNode);
		}
		FOR_EACH_END;


		// update the auxiliary transform critical nodes (since they may have FX attached similar to the WepL/R)
		if (pCostume->bHasNodeAuxTransforms) {
			FOR_EACH_IN_EARRAY_FORWARDS(pCostume->eaCostumeParts, WLCostumePart, pCostumePart)
			{
				DynAnimNodeAuxTransformList *pAnimNodeAuxTransformList = GET_REF(pCostumePart->hAnimNodeAuxTransformList);
				if (pAnimNodeAuxTransformList) {
					FOR_EACH_IN_EARRAY_FORWARDS(pAnimNodeAuxTransformList->eaTransforms, DynAnimNodeAuxTransform, pAuxTransform)
					{
						DynNode* pNode = dynSkeletonFindNodeNonConst(pDrawSkel->pSkeleton, pAuxTransform->pcNode);
						if (pNode)
							dynNodeSetCriticalBit(pNode);
					}
					FOR_EACH_END;
				}
			}
			FOR_EACH_END;
		}

		if (gConf.bNewAnimationSystem)
		{
			const DynGroundRegData *pGroundRegData = pSkelInfo ? GET_REF(pSkelInfo->hGroundRegData) : NULL;

			/*
			//note: this might be good to add, but it would only be needed if a skeleton somehow had no other critical nodes as children of the hips and still required ground registration to be ran on it.
			//make sure the hipsNode is critical since it's used for static ground registration
			if (pDrawSkel->pSkeleton->pHipsNode) {
				dynNodeSetCriticalBit(pDrawSkel->pSkeleton->pHipsNode);
			}
			*/
			
			//make sure the dynamic ground registration nodes are critical nodes
			if (pGroundRegData)
			{
				FOR_EACH_IN_EARRAY(pGroundRegData->eaLimbs, DynGroundRegDataLimb, pLimb)
				{
					DynNode *pEndGR = dynSkeletonFindNodeNonConst(pDrawSkel->pSkeleton, pLimb->pcEndEffectorNode);
					DynNode *pFixGR = dynSkeletonFindNodeNonConst(pDrawSkel->pSkeleton, pLimb->pcHeightFixupNode);
					if (pEndGR) dynNodeSetCriticalBit(pEndGR);
					if (pFixGR) dynNodeSetCriticalBit(pFixGR);
				}
				FOR_EACH_END;
			}

			//set the joint chains in each strand to be critical
			//this should only do something when they are not also skinned
			FOR_EACH_IN_EARRAY(pDrawSkel->pSkeleton->eaStrands, DynStrand, pStrand) {
				dynNodeSetCriticalBit(pStrand->pEndNode);
			} FOR_EACH_END;
		}
	}

	// Clear any previous skinning info
	if (pDrawSkel->pCurrentSkinningMatSet)
	{
		dynSkinningMatSetDecrementRefCount(pDrawSkel->pCurrentSkinningMatSet);
		pDrawSkel->pCurrentSkinningMatSet = NULL;
	}

	if (pDrawSkel->pSkinningMatSets[0])
		dynSkinningMatSetDecrementRefCount(pDrawSkel->pSkinningMatSets[0]);
	if (pDrawSkel->pSkinningMatSets[1])
		dynSkinningMatSetDecrementRefCount(pDrawSkel->pSkinningMatSets[1]);

	// Now that we've found all the bones, placed them in eaNodes, and set the critical bits, let's rearrange them for cache performance:
	pDrawSkel->uiSkinningMatCount = eaSize(&eaNodes);
	if (pDrawSkel->uiSkinningMatCount > 0)
	{
		dynNodeIndexSkinningNodes(pDrawSkel->pSkeleton->pRoot, 0, pDrawSkel->uiSkinningMatCount-1);
		dynNodeFindCriticalTree(pDrawSkel->pSkeleton->pRoot);


		// Rather than order the boneinfos by the random order of eaNodes, order them by critical bone index for cache performance
		FOR_EACH_IN_EARRAY(pDrawSkel->eaDynGeos, DynDrawModel, pGeo)
		{
			U32 uiLODIndex;
			for (uiLODIndex=0; uiLODIndex < pGeo->uiNumLODLevels; ++uiLODIndex)
			{
				U32 uiNodeIndex;
				if (uiLODIndex>0 && pGeo->apuiBoneIdxs[uiLODIndex] == NULL)
				{
					pGeo->apuiBoneIdxs[uiLODIndex] = pGeo->apuiBoneIdxs[0];
				}
				else
				{
					U8* pOldTempBoneIdxs = pGeo->apuiBoneIdxs[uiLODIndex];
					pGeo->apuiBoneIdxs[uiLODIndex] = boneIdxAllocate(&permBones, pGeo->uiNumNodesUsed[uiLODIndex]);
					assert(pGeo->apuiBoneIdxs[uiLODIndex]);
					for (uiNodeIndex=0; uiNodeIndex < pGeo->uiNumNodesUsed[uiLODIndex]; ++uiNodeIndex)
					{
						pGeo->apuiBoneIdxs[uiLODIndex][uiNodeIndex] = eaNodes[pOldTempBoneIdxs[uiNodeIndex]]->iSkinningBoneIndex;
						assert(pGeo->apuiBoneIdxs[uiLODIndex][uiNodeIndex] < pDrawSkel->uiSkinningMatCount);
					}
				}
			}
		}
		FOR_EACH_END
	}
	else if (eaSize(&eaAttachmentNodes) > 0)
	{
		FOR_EACH_IN_EARRAY(eaAttachmentNodes, DynNode, pAttach)
		{
			dynNodeSetCriticalBit(pAttach);
		}
		FOR_EACH_END;
		dynNodeFindCriticalTree(pDrawSkel->pSkeleton->pRoot);
	}

	pDrawSkel->pSkinningMatSets[0] = TSMP_ALLOC(DynSkinningMatSet);
	dynSkinningMatSetInit(pDrawSkel->pSkinningMatSets[0], pDrawSkel->uiSkinningMatCount);

	pDrawSkel->pSkinningMatSets[1] = TSMP_ALLOC(DynSkinningMatSet);
	dynSkinningMatSetInit(pDrawSkel->pSkinningMatSets[1], pDrawSkel->uiSkinningMatCount);


	// Setup animation bone info data for optimization of animation: do as much pre-processing as possible and store it in the skeleton's pAnimBoneInfos

	//Also, since we may have re-created our geos, we need to update bone visibility again.
	//Search all our updaters for one with a current graph node that has a vis set.
	if (pDrawSkel->pSkeleton)
	{
		dynDrawSetupAnimBoneInfo(pDrawSkel->pSkeleton, pDrawSkel->bDontCreateBodysock, bThreaded);

		if (gConf.bNewAnimationSystem) {
			dynSkeletonUpdateBoneVisibility(pDrawSkel->pSkeleton, NULL);
		}
	}

	eaDestroy(&eaNodes);
	eaDestroy(&eaAttachmentNodes);
	pDrawSkel->bInvalid = false;
	if (permBones.uiCurrentBoneIdx != tempBones.uiCurrentBoneIdx)
	{
		if (permBones.uiCurrentBoneIdx > tempBones.uiCurrentBoneIdx)
		{
			ErrorDetailsf("Bones used: %d, Bones Allocated: %d, Costume name: %s", permBones.uiCurrentBoneIdx, permBones.uiMaxIdxs, pCostume?pCostume->pcName:"NONE");
			FatalErrorf("Got bone index buffer overflow!");
		}
		else // underflow
		{
			if (!bErrored) // Didn't already report a useful error
				Errorf("Got bone index buffer underflow on costume %s", pCostume?pCostume->pcName:"NONE");
		}
	}

	ScratchFree(tempBones.pBoneIdxs);
}

void dynDrawSkeletonSeverBones(DynDrawSkeleton* pDrawSkel, const char** ppcSeverBones, U32 uiNumBones)
{
	U32 uiIndex;
	const WLCostume* pCostume = GET_REF(pDrawSkel->pSkeleton->hCostume);
	for (uiIndex=0; uiIndex<uiNumBones; ++uiIndex)
	{
		if (pCostume)
		{
			// Only push bones if the costume supports severing that bone
			FOR_EACH_IN_EARRAY(pCostume->eaCostumeParts, const WLCostumePart, pCostumePart)
			{
				const DynNode *pRealBone = dynSkeletonFindNode(pDrawSkel->pSkeleton, pCostumePart->pcOrigAttachmentBone);
				
				if (pCostumePart->pcOrigAttachmentBone == ppcSeverBones[uiIndex] && (pCostumePart->pcStumpGeo || !pRealBone))
				{
					eaPushUnique(&pDrawSkel->eaSeveredBones, ppcSeverBones[uiIndex]);
					break;
				}
			}
			FOR_EACH_END;
		}
	}
	pDrawSkel->bUpdateDrawInfo = true;
}

void dynDrawSkeletonRestoreSeveredBones(DynDrawSkeleton* pDrawSkel, const char** ppcSeverBones, U32 uiNumBones) {

	U32 uiIndex;
	const WLCostume* pCostume = GET_REF(pDrawSkel->pSkeleton->hCostume);
	for (uiIndex=0; uiIndex<uiNumBones; ++uiIndex) {

		if (pCostume) {

			int i;
			for(i = 0; i < eaSize(&pDrawSkel->eaSeveredBones); i++) {
				if (pDrawSkel->eaSeveredBones[i] == ppcSeverBones[uiIndex]) {
					eaRemoveFast(&pDrawSkel->eaSeveredBones, i);
					i--;
				}
			}
		}
	}
	pDrawSkel->bUpdateDrawInfo = true;

}

void dynDrawSkeletonReloadAllUsingCostume(const WLCostume* pReloadedCostume, enumResourceEventType eType)
{
	int iNumSkels = eaSize(&eaDrawSkelList);
	int iSkelIndex;
	for (iSkelIndex=0; iSkelIndex<iNumSkels; ++iSkelIndex)
	{
		if ( !pReloadedCostume || GET_REF(eaDrawSkelList[iSkelIndex]->pSkeleton->hCostume) == pReloadedCostume )
		{
			if (eType != RESEVENT_RESOURCE_REMOVED)
			{
				dynDrawSkeletonFreeGeos(eaDrawSkelList[iSkelIndex] MEM_DBG_PARMS_INIT);
				dynDrawSetupDrawInfo(eaDrawSkelList[iSkelIndex], false MEM_DBG_PARMS_INIT);
				dynDrawSkeletonSetMaintainedFx(eaDrawSkelList[iSkelIndex]);
			}
			else
				eaDrawSkelList[iSkelIndex]->bInvalid = true;
		}
	}
}

void wlFxMaterialSwapSetMNC( WLFXMaterialSwap* pSwap, const DynFx* pFx ) 
{
	pSwap->mnc[0].name = pcColor1;
	pSwap->mnc[1].name = pcColor2;
	pSwap->mnc[2].name = pcColor3;
	pSwap->use_mnc[0] = pSwap->use_mnc[1] = pSwap->use_mnc[2] = false;
	if (pFx->pParticle->peaMNCRename)
	{
		int i;
		// Replace geometryNamedConstants with our own version
		FOR_EACH_IN_EARRAY((*pFx->pParticle->peaMNCRename), DynMNCRename, pMNCRename)
		{
			for (i=0; i<3; ++i)
			{
				if (pMNCRename->pcAfter && pMNCRename->pcBefore == pSwap->mnc[i].name)
				{
					pSwap->mnc[i].name = pMNCRename->pcAfter;
					pSwap->use_mnc[i] = true;
				}
			}
		}
		FOR_EACH_END;
	}

	if (pFx->fHue || pFx->fSaturationShift || pFx->fValueShift)
	{
		hsvShiftRGB(pFx->pParticle->pDraw->vColor1, pSwap->mnc[0].value, pFx->fHue, pFx->fSaturationShift, pFx->fValueShift);
		pSwap->mnc[0].value[3] = pFx->pParticle->pDraw->vColor1[3];
		hsvShiftRGB(pFx->pParticle->pDraw->vColor2, pSwap->mnc[1].value, pFx->fHue, pFx->fSaturationShift, pFx->fValueShift);
		pSwap->mnc[1].value[3] = pFx->pParticle->pDraw->vColor2[3];
		hsvShiftRGB(pFx->pParticle->pDraw->vColor3, pSwap->mnc[2].value, pFx->fHue, pFx->fSaturationShift, pFx->fValueShift);
		pSwap->mnc[2].value[3] = pFx->pParticle->pDraw->vColor3[3];

		scaleVec4(pSwap->mnc[0].value, U8TOF32_COLOR, pSwap->mnc[0].value);
		scaleVec4(pSwap->mnc[1].value, U8TOF32_COLOR, pSwap->mnc[1].value);
		scaleVec4(pSwap->mnc[2].value, U8TOF32_COLOR, pSwap->mnc[2].value);
	}
	else
	{
		scaleVec4(pFx->pParticle->pDraw->vColor1, U8TOF32_COLOR, pSwap->mnc[0].value);
		scaleVec4(pFx->pParticle->pDraw->vColor2, U8TOF32_COLOR, pSwap->mnc[1].value);
		scaleVec4(pFx->pParticle->pDraw->vColor3, U8TOF32_COLOR, pSwap->mnc[2].value);
	}
	pSwap->use_fx_constants = true;
}



void dynDrawSkeletonGetFXDrivenDrawParams( DynDrawSkeleton* pDrawSkel, WLDynDrawParams* pParams, const char* pcBoneName, DynDrawModel *pGeo )
{
	U32 uiNumDynFx = eaSize(&pDrawSkel->eaDynFxRefs);
	U32 uiFxRefIndex;
	pParams->alpha = 1.0f;
	for (uiFxRefIndex=0; uiFxRefIndex<uiNumDynFx; ++uiFxRefIndex)
	{
		const DynFx* pFx = GET_REF(pDrawSkel->eaDynFxRefs[uiFxRefIndex]->hDynFx);
		if (pFx)
		{
			if ( pFx->pParticle )
			{
				Vec3 vShiftedColor;
				
				hsvShiftRGB(
					pFx->pParticle->pDraw->vColor,
					vShiftedColor,
					pFx->pParticle->pDraw->vHSVShift[0], 
					pFx->pParticle->pDraw->vHSVShift[1],
					pFx->pParticle->pDraw->vHSVShift[2]);

				if (eaSize(&pFx->eaEntCostumeParts) > 0)
				{
					bool bFound = false;
					FOR_EACH_IN_EARRAY(pFx->eaEntCostumeParts, const char, pcAttachmentName)
					{
						if (pcAttachmentName == pcBoneName)
						{
							bFound = true;
							break;
						}
					}
					FOR_EACH_END;
					if (pFx->bCostumePartsExclusive && bFound)
						continue;
					else if (!pFx->bCostumePartsExclusive && !bFound)
						continue;
				}

				if(pFx->bEntMaterialExcludeOptionalParts && pGeo) {

					DynSkeleton *pSkeleton = pDrawSkel->pSkeleton;
					WLCostume *pCostume = NULL;
					WLCostumePart *pPart = NULL;

					pCostume = GET_REF(pSkeleton->hCostume);
					if(pCostume) {
						int i;
						for(i = 0; i < eaSize(&pCostume->eaCostumeParts); i++) {
							if(pCostume->eaCostumeParts[i] &&
								pCostume->eaCostumeParts[i]->pchBoneName == pcBoneName &&
								pCostume->eaCostumeParts[i]->pcModel == pGeo->pModel->name) {

									pPart = pCostume->eaCostumeParts[i];
							}
						}
					}

					if(pPart && pPart->bOptionalPart)
						continue;
				}

				switch (pFx->pParticle->pDraw->iEntLightMode)
				{
					xcase edelmNone:
					{
					}
					xcase edelmAdd:
					{
						Vec3 vScaledColor;
						scaleVec3(vShiftedColor, U8TOF32_COLOR, vScaledColor);
						addVec3(vScaledColor, pParams->ambient_offset, pParams->ambient_offset);
					}
					xcase edelmMultiply:
					{
						Vec3 vScaledColor;
						scaleVec3(vShiftedColor, U8TOF32_COLOR, vScaledColor);
						mulVecVec3(vScaledColor, pParams->ambient_multiplier, pParams->ambient_multiplier);
					}
				}
				switch (pFx->pParticle->pDraw->iEntTintMode)
				{
					xcase edetmNone:
					{
					}
					xcase edetmMultiply:
					{
						Vec3 vScaledColor;
						scaleVec3(vShiftedColor, U8TOF32_COLOR, vScaledColor);
						mulVecVec3(vScaledColor, pParams->color, pParams->color);
					}
					xcase edetmAdd:
					{
						Vec3 vScaledColor;
						scaleVec3(vShiftedColor, U8TOF32_COLOR, vScaledColor);
						addVec3(vScaledColor, pParams->color, pParams->color);
					}
					xcase edetmAlpha:
					{
						// only care about this if bones were specified
						// otherwise, global alpha tinting is already handled at a different level, see dynDrawSkeletonGetFXDrivenAlpha
						if (eaSize(&pFx->eaEntCostumeParts) > 0)
							pParams->alpha = pFx->pParticle->pDraw->vColor[3] * U8TOF32_COLOR;
					}
					xcase edetmSet:
					{
						WLFXMaterialSwap* pSwap = &pParams->material_swap;
						if (pSwap && pFx->pParticle->bMultiColor && !pSwap->use_fx_constants)
						{
							wlFxMaterialSwapSetMNC(pSwap, pFx);
						}
					}
				}
				if (pFx->pParticle->pDraw->iEntMaterial)
				{
					WLFXMaterialSwap* pSwap = &pParams->material_swap;
					if ((pFx->pParticle->pDraw->iEntMaterial == edemmAdd) || (pFx->pParticle->pDraw->iEntMaterial == edemmAddWithConstants) || (pFx->pParticle->pDraw->iEntMaterial == edemmDissolve))
						pSwap = (pParams->num_material_adds < MAX_MATERIAL_ADDS)?&pParams->material_adds[pParams->num_material_adds++]:NULL;
					if (pSwap)
					{
						if (pFx->pParticle->pDraw->iEntMaterial == edemmTextureSwap)
						{
							pSwap->do_texture_swap = true;
							pSwap->texture_swaps[0] = &pFx->pParticle->pDraw->pTexture;
							pSwap->texture_swaps[1] = &pFx->pParticle->pDraw->pTexture2;
							pSwap->texture_swap_names[0] = pFx->pParticle->pDraw->pcTextureName;
							pSwap->texture_swap_names[1] = pFx->pParticle->pDraw->pcTextureName2;
						}
						else if (pFx->pParticle->pDraw->pMaterial)
							pSwap->material_to_use = pFx->pParticle->pDraw->pMaterial;
						if ((pFx->pParticle->pDraw->iEntMaterial == edemmSwap) || (pFx->pParticle->pDraw->iEntMaterial == edemmAdd) || (pFx->pParticle->pDraw->iEntMaterial == edemmDissolve))
							pSwap->dont_use_costume_constants = true;

						if (pFx->pParticle->pDraw->iEntMaterial == edemmDissolve)
							pParams->force_no_alpha = 1; // no alpha allowed with dissolve

						pSwap->alpha = pFx->pParticle->pDraw->vColor[3] * U8TOF32_COLOR * pFx->fFadeOut;
						pSwap->dissolve = (pFx->pParticle->pDraw->iEntMaterial == edemmDissolve);

						if (pFx->pParticle->bMultiColor && !pSwap->use_fx_constants)
						{
							wlFxMaterialSwapSetMNC(pSwap, pFx);
						}
					}
				}
			}

			if (GET_REF(pFx->hInfo) && GET_REF(pFx->hInfo)->bEntNeedsAuxPass)
			{
				pParams->draw_in_aux_visual_pass = true;
			}
		}
		else
		{
			dynFxReferenceFree(pDrawSkel->eaDynFxRefs[uiFxRefIndex]);
			eaRemove(&pDrawSkel->eaDynFxRefs, uiFxRefIndex);
			--uiFxRefIndex;
			--uiNumDynFx;
		}
	}
}

static F32 dynDrawSkeletonGetFXDrivenAlpha( DynDrawSkeleton* pDrawSkel )
{
	F32 fAlpha = 1.0f;
	U32 uiNumDynFx = eaSize(&pDrawSkel->eaDynFxRefs);
	U32 uiFxRefIndex;
	for (uiFxRefIndex=0; uiFxRefIndex<uiNumDynFx; ++uiFxRefIndex)
	{
		const DynFx* pFx = GET_REF(pDrawSkel->eaDynFxRefs[uiFxRefIndex]->hDynFx);
		if ( pFx && pFx->pParticle && pFx->pParticle->pDraw->iEntTintMode == edetmAlpha && eaSize(&pFx->eaEntCostumeParts) == 0)
			fAlpha *= pFx->pParticle->pDraw->vColor[3] * U8TOF32_COLOR;
	}
	return fAlpha;
}

void dynDrawSkeletonPushDynFx( DynDrawSkeleton* pDrawSkel, DynFx* pNewFx, const char *pcTreatAsCostumeTag )
{
	DynFxInfo *pFxInfo = GET_REF(pNewFx->hInfo);
	U32 uiNumDynFx = eaSize(&pDrawSkel->eaDynFxRefs);
	U32 uiFxRefIndex;

	if (!pFxInfo || pFxInfo->pcPlayOnCostumeTag == pcTreatAsCostumeTag) // might need 3rd case from below
	{
		FOR_EACH_IN_EARRAY(pDrawSkel->eaSubDrawSkeletons, DynDrawSkeleton, pSubSkeleton)
			dynDrawSkeletonPushDynFx(pSubSkeleton, pNewFx, pcTreatAsCostumeTag);
		FOR_EACH_END;
	}
	else if (pFxInfo->pcPlayOnCostumeTag)
	{
		FOR_EACH_IN_EARRAY(pDrawSkel->pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton)
			if (pFxInfo->pcPlayOnCostumeTag == pChildSkeleton->pcCostumeFxTag ||
				strcmpi(pFxInfo->pcPlayOnCostumeTag, "all") == 0)
			{
				dynDrawSkeletonPushDynFx(pChildSkeleton->pDrawSkel, pNewFx, pFxInfo->pcPlayOnCostumeTag);
			}
		FOR_EACH_END;
	}

	if (!pFxInfo											||
		pFxInfo->pcPlayOnCostumeTag == pcTreatAsCostumeTag	||
		pFxInfo->pcPlayOnCostumeTag == pDrawSkel->pSkeleton->pcCostumeFxTag)
	{
		for (uiFxRefIndex=0; uiFxRefIndex<uiNumDynFx; ++uiFxRefIndex)
		{
			const DynFx* pFx = GET_REF(pDrawSkel->eaDynFxRefs[uiFxRefIndex]->hDynFx);
			if ( pFx == pNewFx ) {
				return; // already have it
			}
		}
		eaPush(&pDrawSkel->eaDynFxRefs, dynFxReferenceCreate(pNewFx));	
	}
}

void dynDrawSkeletonHardCopyDynFxRef(DynDrawSkeleton *pDrawSkel, DynFxRef *pNewFxRef)
{
	DynFx *pNewFx = GET_REF(pNewFxRef->hDynFx);
	if (pDrawSkel && pNewFx) {
		dynDrawSkeletonPushDynFx(pDrawSkel, pNewFx, NULL);
	}
}

void dynDrawSkeletonAllocSkinningMats(DynDrawSkeleton* pDrawSkel)
{
	if (pDrawSkel->pCurrentSkinningMatSet)
		dynSkinningMatSetDecrementRefCount(pDrawSkel->pCurrentSkinningMatSet);
	pDrawSkel->pCurrentSkinningMatSet = NULL;

	if (pDrawSkel->uiSkinningMatCount > 0)
	{
		if (pDrawSkel->pSkinningMatSets[0]->iRefCount == 1)
		{
			pDrawSkel->pCurrentSkinningMatSet = pDrawSkel->pSkinningMatSets[0];
			dynSkinningMatSetIncrementRefCount(pDrawSkel->pSkinningMatSets[0]);
		}
		else if (pDrawSkel->pSkinningMatSets[1]->iRefCount == 1)
		{
			pDrawSkel->pCurrentSkinningMatSet = pDrawSkel->pSkinningMatSets[1];
			dynSkinningMatSetIncrementRefCount(pDrawSkel->pSkinningMatSets[1]);
		}
		else
		{
			// exceeded supply!
			pDrawSkel->pCurrentSkinningMatSet = TSMP_ALLOC(DynSkinningMatSet);
			dynSkinningMatSetInit(pDrawSkel->pCurrentSkinningMatSet, pDrawSkel->uiSkinningMatCount);
		}
	}
}


int forceBasePose=0;

// Force all animations into the base pose
AUTO_CMD_INT(forceBasePose, danimTPose) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);

// force all animations into the T pose
AUTO_CMD_INT(forceBasePose, danimBasePose) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);

// torso pointing debug commands
AUTO_CMD_INT(dynDebugState.bDrawTorsoPointing, danimDrawTorsoPointing) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);
AUTO_CMD_INT(dynDebugState.bDisableTorsoPointing, danimDisableTorsoPointing) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);
AUTO_CMD_INT(dynDebugState.bEnableOldAnimTorsoPointingFix, danimEnableOldAnimTorsoPointingFix) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);
AUTO_CMD_INT(dynDebugState.bDisableTorsoPointingFix, danimDisableTorsoPointingFix) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);

//banking debug commands
AUTO_CMD_INT(dynDebugState.bDisableAutoBanking, danimDisableAutoBanking) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);

//terrain tilting commands
AUTO_CMD_INT(dynDebugState.bDrawTerrainTilt, danimDrawTerrainTilt) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);
AUTO_CMD_INT(dynDebugState.bDisableTerrainTilt, danimDisableTerrainTilt) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);
AUTO_CMD_INT(dynDebugState.bDisableTerrainTiltOffset, danimDisableTerrainTiltOffset) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);

//client side ragdoll commands
AUTO_CMD_INT(dynDebugState.bDisableClientSideRagdoll, danimDisableClientSideRagdoll) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);
AUTO_CMD_INT(dynDebugState.bDisableClientSideRagdollInitialVelocities, danimDisableClientSideRagdollInitialVelocities) ACMD_CATEGORY(DEBUG) ACMD_CATEGORY(dynAnimation);

// test for movement syncing in the sequencer
AUTO_CMD_INT(dynDebugState.bNoMovementSync, danimNoMovementSync) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);

#if 0
void dynDrawCreateSkinningMatRef( SkinningMat4 mat, const DynNode* pNode, const Vec3 vBaseOffset, const Mat4 root_mat, bool bForceBasePose) 
{
	Mat4 mWorldTrans;
	Vec3 vRotOffset;
	Vec3 vScale;
	Vec3 vOffset,vTrans;
	Quat qRot;

	PERFINFO_AUTO_START_FUNC_L2();

	if (forceBasePose || bForceBasePose)
	{
		if (root_mat)
			mat4toSkinningMat4(root_mat, mat);
		else
			copyMat34(unitmat44, mat);
		return;
	}

	dynNodeGetWorldSpacePosRotScale(pNode,vTrans, qRot, vScale);

	quatToMat(qRot, mWorldTrans);
	copyVec3(vTrans, mWorldTrans[3]);

	scaleMat3Vec3(mWorldTrans, vScale);

	mulVecVec3(vBaseOffset, vScale, vOffset);
	quatRotateVec3Inline(qRot, vOffset, vRotOffset);

	addVec3(mWorldTrans[3], vRotOffset, mWorldTrans[3]);
	mat4toSkinningMat4(mWorldTrans, mat);


	PERFINFO_AUTO_STOP_L2();
}

void dynDrawCreateSkinningMatRef2( SkinningMat4 mat, const DynNode* pNode, const Vec3 vBaseOffset, const Mat4 root_mat, bool bForceBasePose) 
{
	Mat34H mWorldTrans;
	Vec4H vScale;
	Vec4H vOffset, vRotOffset;
	Vec4H vTrans;
	Vec4H qRot;

	PERFINFO_AUTO_START_FUNC_L2();

	if (forceBasePose || bForceBasePose)
	{
		if (root_mat)
			mat4toSkinningMat4(root_mat, mat);
		else
			copyMat34(unitmat44, mat);
		return;
	}

	//copyMat4(unitmat, mWorldScale);
	
	dynNodeGetWorldSpacePosRotScale(pNode,Vec4HToVec4(vTrans), Vec4HToVec4(qRot), Vec4HToVec4(vScale));
	Vec4HToVec4(vScale)[3] = 1.0f;
	quatToMat34Inline(Vec4HToVec4(qRot), Vec4HToVec4(mWorldTrans[0])); // for now

	mulVecVec3(vBaseOffset, Vec4HToVec4(vScale), Vec4HToVec4(vOffset));
	//quatRotateVec3Inline(qRot, vOffset, vRotOffset);
	vRotOffset = mulVec4HMat34H(vOffset, (Mat34H *)&mWorldTrans);
	addVec4H(vRotOffset,vTrans,vRotOffset);

	scaleMat34HVec4H2(&mWorldTrans, vScale);

	//addVec3(vTrans, vRotOffset, mWorldTrans[3]);
	Vec4HToVec4(mWorldTrans[0])[3] = Vec4HToVec4(vRotOffset)[0];
	Vec4HToVec4(mWorldTrans[1])[3] = Vec4HToVec4(vRotOffset)[1];
	Vec4HToVec4(mWorldTrans[2])[3] = Vec4HToVec4(vRotOffset)[2];
	mat34HtoSkinningMat4(&mWorldTrans, mat);
	//mat4toSkinningMat4(mWorldTrans, mat);

	PERFINFO_AUTO_STOP_L2();
}
#endif

void dynSkinningMatSetIncrementRefCount(DynSkinningMatSet* pSet)
{
	InterlockedIncrement(&pSet->iRefCount);
}

void dynSkinningMatSetDecrementRefCount(DynSkinningMatSet* pSet)
{
	if (InterlockedDecrement(&pSet->iRefCount) <= 0)
	{
		free(pSet->pSkinningMatsMemUnaligned);
		TSMP_FREE(DynSkinningMatSet, pSet);
	}
}

void dynSkinningMatSetInit(DynSkinningMatSet* pSet, U32 uiSkinningMatCount)
{
	pSet->pSkinningMatsMemUnaligned = calloc(sizeof(SkinningMat4) * uiSkinningMatCount + VEC_ALIGNMENT - 1, 1);
	pSet->pSkinningMats = (SkinningMat4*)AlignPointerUpPow2(pSet->pSkinningMatsMemUnaligned, VEC_ALIGNMENT);
	assertVecAligned(pSet->pSkinningMats);
	pSet->iRefCount = 1;
}

void dynDrawSkeletonBasePose(DynDrawSkeleton* pDrawSkel)
{
	U32 i;
	Mat4 mRoot;

	dynDrawSkeletonAllocSkinningMats(pDrawSkel);

	dynNodeGetWorldSpaceMat(pDrawSkel->pSkeleton->pRoot, mRoot, false);
	for (i=0; i<pDrawSkel->uiSkinningMatCount; ++i)
	{
		//DUH
		//dynNodeCreateSkinningMat(pDrawSkel->pCurrentSkinningMatSet->pSkinningMats[i], NULL, zerovec3, mRoot, true);
		mat4toSkinningMat4(mRoot, pDrawSkel->pCurrentSkinningMatSet->pSkinningMats[i]);
	}
}

F32 dynDrawSkeletonGetNodeRadius(DynDrawSkeleton* pDrawSkel, const char* pcTag)
{
	FOR_EACH_IN_EARRAY(pDrawSkel->eaVisNodes, DynDrawVisibilityNode, pVisNode)
	{
		if (pVisNode->pcBone == pcTag)
			return pVisNode->fRadius;
	}
	FOR_EACH_END;
	return 0.0f;
}

// DEBUG STUFF
typedef struct DynDrawSkeleton DynDrawSkeleton;

void dynDebugSetDebugSkeleton(const DynDrawSkeleton* pDrawSkeleton)
{
	pDebugDrawSkeleton = pDrawSkeleton;
	if (pDrawSkeleton)
		dynDebugStateSetSkeleton(pDrawSkeleton->pSkeleton);
}

const DynDrawSkeleton* dynDebugGetDebugSkeleton(void)
{
	return pDebugDrawSkeleton;
}


void dynDrawSkeletonUpdate(SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkel, F32 fDeltaTime)
{
	pDrawSkel->fFXDrivenAlpha = dynDrawSkeletonGetFXDrivenAlpha(pDrawSkel);
	pDrawSkel->fTotalAlpha = pDrawSkel->fFXDrivenAlpha * pDrawSkel->fEntityAlpha * pDrawSkel->fOtherAlpha;
}

void dynDrawSkeletonShowModelsAttachedToBone(DynDrawSkeleton* pDrawSkel, const char *pcBoneTagName, bool bShow)
{
	const char* pchTag = allocFindString(pcBoneTagName);

	FOR_EACH_IN_EARRAY(pDrawSkel->eaDynGeos, DynDrawModel, pGeo)
		if (pGeo->pAttachmentNode && pGeo->pAttachmentNode->pcTag == pchTag)
		{
			pGeo->bIsHidden = !bShow;
		}
	FOR_EACH_END
}

void dynDrawModelInitMaterialPointers( DynDrawModel* pGeo )
{
	int i;
	for (i=0; i<2; ++i)
	{
		if (!pGeo->pMaterial[i] && pGeo->pcMaterial[i])
			pGeo->pMaterial[i] = materialFindNoDefault(pGeo->pcMaterial[i], WL_FOR_ENTITY);
	}
}

void dynDrawFxRefSaveDataDestroy(DynFxRef *pFxRef)
{
	dynFxReferenceFree(pFxRef);
}

void dynDrawSkeletonSaveDataDestroy(DynDrawSkeletonSaveData *pData)
{
	pData->pSkeleton = NULL; //destroyed elsewhere, only used as link to find matching parts
	eaDestroyEx(&pData->eaDynFxRefs, dynFxReferenceFree);
	eaDestroy(&pData->eaSeveredBones);
}

#include "autogen/dynDraw_h_ast.c"
