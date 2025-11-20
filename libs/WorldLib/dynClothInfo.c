#include "dynClothInfo.h"

#include "fileutil.h"
#include "FolderCache.h"
#include "StringCache.h"

#include "dynSeqData.h"
#include "dynClothCollide.h"
#include "dynNodeInline.h"
#include "wlCostume.h"
#include "DynFxInterface.h"
#include "DynFx.h"

#include "dynClothInfo_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

//////////////////////////////////////////////////////////////////////////////////
//
// Cloth Collision Info 
//
//////////////////////////////////////////////////////////////////////////////////
DictionaryHandle hClothColDict;


bool dynClothColInfoVerify(DynClothCollisionInfo* pInfo)
{
	FOR_EACH_IN_EARRAY(pInfo->eaShape, DynClothCollisionShape, pShape)
		normalVec3(pShape->vDirection);
		scaleAddVec3(pShape->vDirection, -pShape->fExten1, pShape->vOffset, pShape->vPoint1);
		scaleAddVec3(pShape->vDirection, pShape->fExten2, pShape->vOffset, pShape->vPoint2);
	FOR_EACH_END;
	return true;
}

bool dynClothColInfoFixup(DynClothCollisionInfo* pInfo)
{
	{
		char cName[256];
		getFileNameNoExt(cName, pInfo->pcFileName);
		pInfo->pcInfoName = allocAddString(cName);
	}
	return true;
}

static void dynClothCollisionInfoReloadCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);


	if(!ParserReloadFileToDictionary(relpath,hClothColDict))
	{
		AnimFileError(relpath, "Error reloading DynClothCollisionInfo file: %s", relpath);
	}
	else
	{
		// nothing to do here
	}
	dynClothObjectResetAll();
}

AUTO_FIXUPFUNC;
TextParserResult fixupClothCollisionInfo(DynClothCollisionInfo* pClothColInfo, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
			if (!dynClothColInfoVerify(pClothColInfo) || !dynClothColInfoFixup(pClothColInfo))
				return PARSERESULT_INVALID; // remove this from the costume list
		xcase FIXUPTYPE_POST_BIN_READ:
			if (!dynClothColInfoFixup(pClothColInfo))
				return PARSERESULT_INVALID; // remove this from the costume list
	}

	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void registerClothColInfoDict(void)
{
	hClothColDict = RefSystem_RegisterSelfDefiningDictionary("DynClothCollision", false, parse_DynClothCollisionInfo, true, false, NULL);

	if (IsServer())
	{
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(hClothColDict, NULL, NULL, NULL, NULL, NULL);
		}
	}
}

void dynClothCollisionInfoLoadAll(void)
{
	resLoadResourcesFromDisk(hClothColDict, "dyn/cloth", ".clothcol", "DynClothCol.bin", PARSER_BINS_ARE_SHARED | PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/cloth/*.clothcol", dynClothCollisionInfoReloadCallback);
	}
}

//////////////////////////////////////////////////////////////////////////////////
//
// Cloth Info 
//
//////////////////////////////////////////////////////////////////////////////////
DictionaryHandle hClothInfoDict;

bool dynClothInfoVerify(DynClothInfo* pInfo)
{
	return true;
}

bool dynClothInfoFixup(DynClothInfo* pInfo)
{
	{
		char cName[256];
		getFileNameNoExt(cName, pInfo->pcFileName);
		pInfo->pcInfoName = allocAddString(cName);
	}

	if(pInfo->bAllowExtraStiffness) {
		pInfo->fStiffness = CLAMPF32(pInfo->fStiffness, 0.01, 10);
	} else {
		pInfo->fStiffness = CLAMPF32(pInfo->fStiffness, 0.01, 1);
	}

	return true;
}

static void dynClothInfoReloadCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);


	if(!ParserReloadFileToDictionary(relpath,hClothInfoDict))
	{
		CharacterFileError(relpath, "Error reloading DynClothInfo file: %s", relpath);
	}
	else
	{
		// nothing to do here
	}
	dynClothObjectResetAll();
}

AUTO_FIXUPFUNC;
TextParserResult fixupClothInfo(DynClothInfo* pClothInfo, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
			if (!dynClothInfoVerify(pClothInfo) || !dynClothInfoFixup(pClothInfo))
				return PARSERESULT_INVALID; // remove this from the costume list
		xcase FIXUPTYPE_POST_BIN_READ:
			if (!dynClothInfoFixup(pClothInfo))
				return PARSERESULT_INVALID; // remove this from the costume list
	}

	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void registerClothInfoDict(void)
{
	hClothInfoDict = RefSystem_RegisterSelfDefiningDictionary("DynClothInfo", false, parse_DynClothInfo, true, false, NULL);

	if (IsServer())
	{
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(hClothInfoDict, NULL, NULL, NULL, NULL, NULL);
		}
	}
}

void dynClothInfoLoadAll(void)
{
	dynClothCollisionInfoLoadAll();

	resLoadResourcesFromDisk(hClothInfoDict, "dyn/cloth", ".cloth", "DynClothInfo.bin", PARSER_BINS_ARE_SHARED | PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/cloth/*.cloth", dynClothInfoReloadCallback);
	}
}





//////////////////////////////////////////////////////////////////////////////////
//
// Create collision set 
//
//////////////////////////////////////////////////////////////////////////////////

static int dynClothNodesAllocated = 0;

DynClothCollisionPiece* dynClothCollisionPieceCreate(DynClothCollisionShape* pShape, DynSkeleton* pSkeleton, S32 iShapeIndex, const DynNode *pBoneOverride, DynFx *pFx)
{
	DynClothCollisionPiece* pPiece = calloc(sizeof(DynClothCollisionPiece), 1);
	const DynNode* pBone = pBoneOverride;
	pPiece->pShape = pShape;
	pPiece->iIndex = iShapeIndex;
	
	if(pSkeleton && !pBoneOverride) {
		pBone = dynNodeFindByName(pSkeleton->pRoot, pShape->pcBone);
	} else if(pFx) {
		pBone = dynFxNodeByName(pShape->pcBone, pFx);
	}

	if (pBone)
	{
		DynNode *pNode = dynNodeAlloc();
		dynNodeParent(pNode, pBone);
		pPiece->pNode = pNode;

		dynClothNodesAllocated++;
	}
	else
	{
		if(!pSkeleton) {
			Errorf("No bone override and no skeleton found for cloth collision piece!\n");
		} else {
			WLCostume *pCostume = GET_REF(pSkeleton->hCostume);
			Errorf("Unable to find bone %s for cloth collision on skeleton %s!", pShape->pcBone, pCostume?pCostume->pcName:"WITH NO COSTUME");
		}
		free(pPiece);
		return NULL;
	}
	return pPiece;
}

DynClothCollisionSet* dynClothCollisionSetCreate(
	DynClothCollisionInfo* pColInfo,
	DynClothObject* pClothObject,
	DynSkeleton* pSkeleton,
	const DynNode *pBoneOverride,
	DynFx *pFx)
{
	DynClothCollisionSet* pSet = calloc(sizeof(DynClothCollisionSet), 1);
	int iShapeIndex = 0;

	FOR_EACH_IN_EARRAY_FORWARDS(pColInfo->eaShape, DynClothCollisionShape, pShape)
		DynClothCollisionPiece* pPiece = dynClothCollisionPieceCreate(pShape, pSkeleton, iShapeIndex, pBoneOverride, pFx);
		if (pPiece)
		{
			eaPush(&pSet->eaPieces, pPiece);
			dynClothAddCollidable(pClothObject);
			++iShapeIndex;
		}
	FOR_EACH_END;

	return pSet;
}

void dynClothCollisionSetAppend(
	DynClothCollisionSet *pColSet,
	DynClothCollisionInfo *pColInfo,
	DynClothObject *pClothObject,
	DynSkeleton *pMountSkeleton)
{
	int iShapeIndex = eaSize(&pColSet->eaPieces);

	FOR_EACH_IN_EARRAY_FORWARDS(pColInfo->eaShape, DynClothCollisionShape, pShape)
	{
		DynClothCollisionPiece *pPiece = dynClothCollisionPieceCreate(pShape, pMountSkeleton, iShapeIndex, NULL, NULL);
		if (pPiece)
		{
			eaPush(&pColSet->eaPieces, pPiece);
			dynClothAddCollidable(pClothObject);
			++iShapeIndex;
		}
	}
	FOR_EACH_END;
}

void dynClothCollisionSetDestroy( DynClothCollisionSet* pSet )
{
	FOR_EACH_IN_EARRAY(pSet->eaPieces, DynClothCollisionPiece, pPiece) {

		dynNodeFree(pPiece->pNode);
		dynClothNodesAllocated--;
		free(pPiece);

	} FOR_EACH_END;

	eaDestroy(&pSet->eaPieces);
	free(pSet);
}

void dynClothCollisionSetUpdate(DynClothCollisionSet* pSet, DynClothObject* pClothObject, bool bMoving, bool bMovingBackwards, bool bMounted)
{
	FOR_EACH_IN_EARRAY(pSet->eaPieces, DynClothCollisionPiece, pPiece)
	{
		DynClothCollisionShape* pShape = pPiece->pShape;
		const DynNode* pNode = pPiece->pNode;
		DynClothCol* pCol = dynClothGetCollidable(pClothObject, pPiece->iIndex);
		DynClothCol* pOldCol = dynClothGetOldCollidable(pClothObject, pPiece->iIndex);

		if (pNode && pShape && pCol &&
			(!pShape->bMoving  || bMoving) &&
			(!pShape->bMovingBackwards || bMovingBackwards) &&
			(!pShape->bWalkingOnly || pShape->bWalkingOnly && !bMounted) &&
			(!pShape->bMountedOnly || pShape->bMountedOnly &&  bMounted))
		{
			DynTransform xform;
			dynNodeGetWorldSpaceTransform(pNode, &xform);
			pCol->Type &= ~CLOTH_COL_SKIP;

			pCol->insideVolume = pShape->bInsideVolume;
			pCol->pushToSkinnedPos = pShape->bPushToSkinnedPos;
			
			// Copy the current values into the old collision.
			memcpy(pOldCol, pCol, sizeof(DynClothCol));

			{
				Vec3 vPoint1, vPoint2;
				Vec3 vDir1;
				Vec3 vDir2;
				Vec3 vDirOut;
				F32 fRadius;
				bool bRebuildMesh = false;

				dynTransformApplyToVec3(&xform, pShape->vPoint1, vPoint1);
				dynTransformApplyToVec3(&xform, pShape->vPoint2, vPoint2);
				
				dynTransformApplyToVec3(&xform, pShape->vDirection, vDir1);
				dynTransformApplyToVec3(&xform, zerovec3, vDir2);
				subVec3(vDir1, vDir2, vDirOut);
				
				{
					Vec3 vScaledDir, vRadiusScale;
					F32 fDot = dotVec3(pShape->vDirection, xform.vScale);
					scaleVec3(pShape->vDirection, fDot, vScaledDir);
					subVec3(xform.vScale, vScaledDir, vRadiusScale);
					fRadius = pShape->fRadius * MAX(vRadiusScale[0], MAX(vRadiusScale[1], vRadiusScale[2]));
				}

				if(pShape->type != pCol->Type) {
					bRebuildMesh = true;
				}

				switch (pShape->type)
				{
					case CLOTH_COL_CYLINDER:
						dynClothColSetCylinder(pCol, vPoint1, vPoint2, fRadius, 0.0f, 0.0f, 1.0f);
						break;
					case CLOTH_COL_PLANE:
						dynClothColSetPlane(pCol, vPoint1, vDirOut, 1.0f);
						break;
					case CLOTH_COL_BALLOON:
						dynClothColSetBalloon(pCol, vPoint1, vPoint2, fRadius, 0.0f, 0.0f, 1.0f);
						break;
					case CLOTH_COL_SPHERE:
						dynClothColSetSphere(pCol, vPoint1, fRadius, 1.0f);
						break;
#if CLOTH_SUPPORT_BOX_COL
					case CLOTH_COL_BOX:
						// FIXME: It'd be really nice if this one worked, but it's been broken since forever. -Cliff
						//dynClothColSetBox(pCol, vPoint1, <insert something useful here>, 1.0f);
						break;
#endif
					default:
						break;
				}

				if(bRebuildMesh) {
					dynClothColCreateMesh(pCol, 1);
				}
			}

		}
		else if (pCol)
		{
			pCol->Type |= CLOTH_COL_SKIP;
		}

	}
	FOR_EACH_END;
}

#include "dynClothInfo_h_ast.c"

