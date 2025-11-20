/***************************************************************************



***************************************************************************/

#include "SparseGrid.h"

#include "StashTable.h"
#include "MemoryPool.h"
#include "Frustum.h"
#include "EArray.h"
#include "timing.h"
#include "bounds.h"

//////////////////////////////////////////////////////////////////////////
// Multi-resolution sparse grid spatial acceleration structure:
// - the world is divided into multiple fixed grids, one for each desired
//   power of two resolution
// - grid cells are only created if they have entries in them, or if they 
//   have child cells
// - grid cells are looked up by integer address in a hash table
// - each grid cell is connected to its parent and children
// - entries are inserted at a single resolution
// - queries are done at multiple resolutions
//////////////////////////////////////////////////////////////////////////


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Unsorted);); // there shouldn't actually be any memory budgeted to this file

typedef struct SparseGridCell SparseGridCell;
typedef struct SparseGridEntry SparseGridEntry;

struct SparseGridEntry
{
	U32 uiLastVisitedTag;

	union
	{
		struct
		{
			Vec3 vMid;
			F32 fRadius;
		} bounds;
		struct
		{
			Vec3 vMin;
			Vec3 vMax;
		} box_bounds;
	};
	bool is_box;

	void *pNode;
	SparseGridEntry **ppSelf;

	SparseGridCell** eaCells;
	SparseGrid* pGrid;

};

struct SparseGridCell
{
	IVec4 iLoc; // Must be first, used as hash.  The fourth element is the index into the aGridLevels array.

	U32 uiLastVisitedTag;
	U32 uiInQueryTag; // 0 for not in query, 1 for partly in query, 2 for entirely in query
	SparseGridEntry** eaEntries;
	SparseGridCell** eaChildCells;
	SparseGridCell* pParentCell;

	Vec3 vBoundsMin, vBoundsMax;
	bool bBoundsDirty, bBoundsEmpty;
};

typedef struct SparseGridLevel
{
	F32 fSizeMultiplier;
	U32 uiSize;
	int iCellCount;
} SparseGridLevel;

typedef struct SparseGrid
{
	U32 uiVisitTag;
	StashTable stCellTable;

	int iGridLevelCount;
	SparseGridLevel* aGridLevels;

	Vec3 vBoundsMin, vBoundsMax;
	bool bBoundsDirty;

	SparseGridCell **eaRootCells;

	U32 uiMinSize;
	U32 uiMaxSize;

	int iCellCount;

	SparseGridCell **eaTempCells; // earray for storing cells during processing so no extra allocations need to be made

	MemoryPool pCellMempool;
	MemoryPool pEntryMempool;

	MEM_DBG_STRUCT_PARMS

} SparseGrid;

typedef struct SparseGridCheckState
{
	SparseGrid* pGrid;
	SparseGridCell ***peaCells;

	enum { CHECK_SPHERE, CHECK_BOX, CHECK_FRUSTUM} checkType;

	union
	{
		struct 
		{
			Vec3 vMid;
			IVec4 iMidLoc;
			F32 fRadius;
			F32 fRadiusSquared;
		} sphere;

		struct 
		{
			Vec3 vMin;
			Vec3 vMax;
			Mat4 mInvWorldMat;
		} box;

		struct 
		{
			const Frustum *pFrustum;
			GfxOcclusionBuffer *pOcclusionBuffer;
			SparseGridOcclusionCallback occlusionFunc;
		} frustum;
	};

} SparseGridCheckState;


SparseGrid* sparseGridCreateDebug( U32 uiMinSize, U32 uiMaxSize MEM_DBG_PARMS )
{
	SparseGrid* pGrid = scalloc(sizeof(SparseGrid), 1);
	int i;

	pGrid->stCellTable = stashTableCreateFixedSizeEx(32, sizeof(IVec4) MEM_DBG_PARMS_CALL);

	pGrid->uiMinSize = pow2(uiMinSize);
	pGrid->uiMaxSize = pow2(uiMaxSize);
	if (pGrid->uiMaxSize < pGrid->uiMinSize)
		pGrid->uiMaxSize = pGrid->uiMinSize;
	
	pGrid->iGridLevelCount = log2(pGrid->uiMaxSize) - log2(pGrid->uiMinSize) + 1;
	pGrid->aGridLevels = scalloc(sizeof(SparseGridLevel), pGrid->iGridLevelCount);

	for (i = 0; i < pGrid->iGridLevelCount; ++i)
	{
		SparseGridLevel *pLevel = &pGrid->aGridLevels[i];
		pLevel->uiSize = pGrid->uiMinSize << i;
		pLevel->fSizeMultiplier = 1.f / pLevel->uiSize;
	}

	pGrid->pCellMempool = createMemoryPoolNamed("SparseGridCell" MEM_DBG_PARMS_CALL);
	initMemoryPool(pGrid->pCellMempool, sizeof(SparseGridCell), 256);

	pGrid->pEntryMempool = createMemoryPoolNamed("SparseGridEntry" MEM_DBG_PARMS_CALL);
	initMemoryPool(pGrid->pEntryMempool, sizeof(SparseGridEntry), 256);

	MEM_DBG_STRUCT_PARMS_INIT(pGrid);

	return pGrid;
}

static void sparseGridCellFree(SparseGrid* pGrid, SparseGridCell* pCell, SparseGridCell **eaExcludeCells)
{
	SparseGridCell* pParent;

	stashRemovePointer(pGrid->stCellTable, pCell, NULL);
	eaDestroy(&pCell->eaEntries);
	eaDestroy(&pCell->eaChildCells);

	for (pParent = pCell->pParentCell; pParent; pParent = pParent->pParentCell)
		pParent->bBoundsDirty = true;
	pGrid->bBoundsDirty = true;

	pParent = pCell->pParentCell;
	if (pParent)
	{
		eaFindAndRemoveFast(&pParent->eaChildCells, pCell);

		if (eaSize(&pParent->eaEntries) == 0 &&
			eaSize(&pParent->eaChildCells) == 0 && 
			eaFind(&eaExcludeCells, pParent) < 0)
		{
			sparseGridCellFree(pGrid, pParent, eaExcludeCells);
		}
	}
	else
	{
		eaFindAndRemoveFast(&pGrid->eaRootCells, pCell);
	}

#ifdef _FULLDEBUG
	assert(pCell->iLoc[3] >= 0 && pCell->iLoc[3] < pGrid->iGridLevelCount);
#endif

	--pGrid->aGridLevels[pCell->iLoc[3]].iCellCount;
	--pGrid->iCellCount;
	mpFree(pGrid->pCellMempool, pCell);
}

__forceinline static void sparseGridRemoveFromCells(SparseGridEntry* pEntry, SparseGridCell **eaExcludeCells)
{
	SparseGrid *pGrid = pEntry->pGrid;
	SparseGridCell *pParent;

	while (eaSize(&pEntry->eaCells))
	{
		SparseGridCell *pCurrentCell = eaPop(&pEntry->eaCells);

		eaFindAndRemoveFast(&pCurrentCell->eaEntries, pEntry);

		if (eaSize(&pCurrentCell->eaEntries) == 0 && 
			eaSize(&pCurrentCell->eaChildCells) == 0 && 
			eaFind(&eaExcludeCells, pCurrentCell) < 0)
		{
			sparseGridCellFree(pGrid, pCurrentCell, eaExcludeCells);
		}
		else
		{
			for (pParent = pCurrentCell; pParent; pParent = pParent->pParentCell)
				pParent->bBoundsDirty = true;
			pGrid->bBoundsDirty = true;
		}
	}
}

static void sparseGridRemoveInternal(SparseGridEntry* pEntry)
{
	if (!pEntry)
		return;

	PERFINFO_AUTO_START_FUNC();

	if (pEntry->ppSelf)
		*pEntry->ppSelf = NULL;

	sparseGridRemoveFromCells(pEntry, NULL);
	eaDestroy(&pEntry->eaCells);
	mpFree(pEntry->pGrid->pEntryMempool, pEntry);

	PERFINFO_AUTO_STOP();
}

void sparseGridRemove(SparseGridEntry** ppEntry)
{
	SparseGridEntry *pEntry = *ppEntry;

	if (!pEntry)
		return;

	assert(pEntry->pGrid);
	assert(pEntry->ppSelf == ppEntry);

	sparseGridRemoveInternal(pEntry);
}

void sparseGridDestroy(SparseGrid* pGrid)
{
	StashTableIterator iter;
	StashElement el;
	int i;

	stashGetIterator(pGrid->stCellTable, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		SparseGridCell* pCell = stashElementGetPointer(el);
		for (i = eaSize(&pCell->eaEntries) - 1; i >= 0; --i)
			sparseGridRemoveInternal(pCell->eaEntries[i]);
		// no need to free the earray, sparseGridRemove handles that when the array is empty
	}

	stashGetIterator(pGrid->stCellTable, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		SparseGridCell* pCell = stashElementGetPointer(el);
		eaDestroy(&pCell->eaChildCells);
		mpFree(pGrid->pCellMempool, pCell);
	}

	destroyMemoryPool(pGrid->pCellMempool);
	destroyMemoryPool(pGrid->pEntryMempool);

	stashTableDestroy(pGrid->stCellTable);
	free(pGrid->aGridLevels);
	eaDestroy(&pGrid->eaTempCells);
	eaDestroy(&pGrid->eaRootCells);

	free(pGrid);
}

__forceinline static void vecLocToIntLoc(SparseGrid* pGrid, int iLevelIdx, const Vec3 vLoc, IVec4 iLoc)
{
	F32 fSizeMultiplier;

#ifdef _FULLDEBUG
	assert(iLevelIdx >= 0 && iLevelIdx < pGrid->iGridLevelCount);
#endif

	fSizeMultiplier = pGrid->aGridLevels[iLevelIdx].fSizeMultiplier;
	iLoc[0] = round(floor(vLoc[0] * fSizeMultiplier));
	iLoc[1] = round(floor(vLoc[1] * fSizeMultiplier));
	iLoc[2] = round(floor(vLoc[2] * fSizeMultiplier));
	iLoc[3] = iLevelIdx;
}

__forceinline static F32 vecDistanceSquaredToIntLoc(SparseGrid* pGrid, const Vec3 vSrcLoc, const IVec4 iSrcLoc, const IVec4 iDstLoc)
{
	F32 fSize;
	Vec3 vDst;

	if (sameVec3(iSrcLoc, iDstLoc))
		return 0;

#ifdef _FULLDEBUG
	assert(iDstLoc[3] >= 0 && iDstLoc[3] < pGrid->iGridLevelCount);
#endif

	fSize = pGrid->aGridLevels[iDstLoc[3]].uiSize;

	if (iSrcLoc[0] < iDstLoc[0])
		vDst[0] = iDstLoc[0] * fSize;
	else if (iSrcLoc[0] > iDstLoc[0])
		vDst[0] = (iDstLoc[0] + 1) * fSize;
	else
		vDst[0] = vSrcLoc[0];

	if (iSrcLoc[1] < iDstLoc[1])
		vDst[1] = iDstLoc[1] * fSize;
	else if (iSrcLoc[1] > iDstLoc[1])
		vDst[1] = (iDstLoc[1] + 1) * fSize;
	else
		vDst[1] = vSrcLoc[1];

	if (iSrcLoc[2] < iDstLoc[2])
		vDst[2] = iDstLoc[2] * fSize;
	else if (iSrcLoc[2] > iDstLoc[2])
		vDst[2] = (iDstLoc[2] + 1) * fSize;
	else
		vDst[2] = vSrcLoc[2];

	return distance3Squared(vDst, vSrcLoc);
}

static SparseGridCell* getCellForLoc(SparseGrid* pGrid, const IVec4 iLoc, bool bNeedCreate)
{
	SparseGridCell *pCell = NULL;

#ifdef _FULLDEBUG
	assert(iLoc[3] >= 0 && iLoc[3] < pGrid->iGridLevelCount);
#endif

	if (!stashFindPointer(pGrid->stCellTable, iLoc, &pCell) && bNeedCreate)
	{
		pCell = mpAlloc(pGrid->pCellMempool);
		pCell->bBoundsEmpty = true;
		copyVec4(iLoc, pCell->iLoc);
		assert(stashAddPointer(pGrid->stCellTable, pCell, pCell, false));
		++pGrid->aGridLevels[iLoc[3]].iCellCount;
		++pGrid->iCellCount;

		// add parent
		if (iLoc[3] < pGrid->iGridLevelCount - 1)
		{
			IVec4 iParentLoc;
			setVec4(iParentLoc, iLoc[0] >> 1, iLoc[1] >> 1, iLoc[2] >> 1, iLoc[3] + 1);
			pCell->pParentCell = getCellForLoc(pGrid, iParentLoc, true);
			steaPush(&pCell->pParentCell->eaChildCells, pCell, pGrid);
		}
		else
		{
			steaPush(&pGrid->eaRootCells, pCell, pGrid);
			pGrid->bBoundsDirty = true;
		}
	}
	return pCell;
}

__forceinline static bool checkBoundsNotVisible(SparseGridCheckState *pState, const Vec3 world_min, const Vec3 world_max, int clipped, bool is_leaf)
{
	if (pState->frustum.occlusionFunc)
	{
		int nearClipped;
		Vec4_aligned eye_bounds[8];

		mulBounds(world_min, world_max, pState->frustum.pFrustum->viewmat, eye_bounds);

		nearClipped = frustumCheckBoxNearClipped(pState->frustum.pFrustum, eye_bounds);
		if (nearClipped == 2)
		{
			return true;
		}
		// only test non-leaf nodes if they are not clipped by the near plane
		else if ((is_leaf || !nearClipped) && !pState->frustum.occlusionFunc(pState->frustum.pOcclusionBuffer, eye_bounds, 1))
		{
			return true;
		}

		return false;
	}

	return clipped != FRUSTUM_CLIP_NONE && !frustumCheckBoxWorld(pState->frustum.pFrustum, clipped, world_min, world_max, NULL, false);
}

static void checkAddCellRecursive(SparseGridCheckState *pState, SparseGridCell* pCell)
{
	int i;
	U32 uiInQueryTag = pCell->uiInQueryTag;

	if (uiInQueryTag != 2)
	{
		F32 fSize;

		PERFINFO_AUTO_START_L2("cell culling", 1);

		fSize = pState->pGrid->aGridLevels[pCell->iLoc[3]].uiSize;

		switch (pState->checkType)
		{
			xcase CHECK_SPHERE:
			{
				uiInQueryTag = (vecDistanceSquaredToIntLoc(pState->pGrid, pState->sphere.vMid, pState->sphere.iMidLoc, pCell->iLoc) <= pState->sphere.fRadiusSquared);
				if (uiInQueryTag && eaSize(&pCell->eaChildCells))
				{
					F32 fRadiusSquared2 = pState->sphere.fRadius - fSize * fSize * fSize;
					MAX1(fRadiusSquared2, 0);
					fRadiusSquared2 = SQR(fRadiusSquared2);
					uiInQueryTag += (vecDistanceSquaredToIntLoc(pState->pGrid, pState->sphere.vMid, pState->sphere.iMidLoc, pCell->iLoc) <= fRadiusSquared2);
				}
			}

			xcase CHECK_BOX:
			{
				Vec3 vCellMin, vCellMax, vLocalCellMin, vLocalCellMax;
				scaleVec3(pCell->iLoc, fSize, vCellMin);
				addVec3same(vCellMin, fSize, vCellMax);
				mulBoundsAA(vCellMin, vCellMax, pState->box.mInvWorldMat, vLocalCellMin, vLocalCellMax);
				uiInQueryTag = boxBoxCollision(vLocalCellMin, vLocalCellMax, pState->box.vMin, pState->box.vMax);
				if (uiInQueryTag && eaSize(&pCell->eaChildCells))
				{
					Vec3 vMin2, vMax2;
					addVec3same(pState->box.vMin, fSize, vMin2);
					addVec3same(pState->box.vMax, -fSize, vMax2);
					uiInQueryTag += boxBoxCollision(vLocalCellMin, vLocalCellMax, vMin2, vMax2);
				}
			}

			xcase CHECK_FRUSTUM:
			{
				Vec3 vCellMin, vCellMax, vCellMid;
				F32 fCellRadius = fSize * 0.5f * 1.73205f;
				int clipped;

				scaleVec3(pCell->iLoc, fSize, vCellMin);
				addVec3same(vCellMin, fSize * 0.5f, vCellMid);
				addVec3same(vCellMin, fSize, vCellMax);

				if (!(clipped = frustumCheckSphereWorld(pState->frustum.pFrustum, vCellMid, fCellRadius)))
					uiInQueryTag = 0;
				else 
					uiInQueryTag = !checkBoundsNotVisible(pState, vCellMin, vCellMax, clipped, false);
			}
		}

		PERFINFO_AUTO_STOP_L2();
	}

	if (uiInQueryTag)
	{
		pCell->uiLastVisitedTag = pState->pGrid->uiVisitTag;
		steaPush(pState->peaCells, pCell, pState->pGrid);

		if (eaSize(&pCell->eaChildCells))
		{
			IVec4 iOldMidLoc;
			if (pState->checkType == CHECK_SPHERE)
			{
				copyVec4(pState->sphere.iMidLoc, iOldMidLoc);
				vecLocToIntLoc(pState->pGrid, pCell->eaChildCells[0]->iLoc[3], pState->sphere.vMid, pState->sphere.iMidLoc);
			}

			for (i = 0; i < eaSize(&pCell->eaChildCells); ++i)
			{
				pCell->eaChildCells[i]->uiInQueryTag = uiInQueryTag;
				checkAddCellRecursive(pState, pCell->eaChildCells[i]);
			}

			if (pState->checkType == CHECK_SPHERE)
				copyVec4(iOldMidLoc, pState->sphere.iMidLoc);
		}
	}
}

static void recurseDown(SparseGridCheckState *pState, int iInitialSize, int iEndSize)
{
	int i, j;
	SparseGridCell* pCell;

	PERFINFO_AUTO_START_FUNC_L2();

	// recurse down
	for (i = iInitialSize; i < iEndSize; ++i)
	{
		ANALYSIS_ASSUME(*pState->peaCells);
		pCell = (*pState->peaCells)[i];

		if (eaSize(&pCell->eaChildCells))
		{
			if (pState->checkType == CHECK_SPHERE)
				vecLocToIntLoc(pState->pGrid, pCell->iLoc[3], pState->sphere.vMid, pState->sphere.iMidLoc);

			for (j = 0; j < eaSize(&pCell->eaChildCells); ++j)
			{
				pCell->eaChildCells[j]->uiInQueryTag = 1;
				checkAddCellRecursive(pState, pCell->eaChildCells[j]);
			}
		}
	}

	PERFINFO_AUTO_STOP_L2();
}

static void getCellsForSphere(SparseGrid* pGrid, const Vec3 vMid, F32 fRadius, SparseGridCell ***peaCells, bool bForInsert)
{
	SparseGridCheckState state;
	IVec4 iMinLoc, iMaxLoc, iMidLoc, iLoc;
	SparseGridCell* pCell = NULL;
	Vec3 vMin, vMax;
	U32 uiSize;
	int iLevelIdx, iInitialLevelIdx, iMaxLevelIdx, iInitialSize = eaSize(peaCells), iEndSize = -1;

	PERFINFO_AUTO_START_FUNC_L2();

	uiSize = pow2(round(ceil(fRadius*0.5f)));
	uiSize = CLAMP(uiSize, pGrid->uiMinSize, pGrid->uiMaxSize);
	iInitialLevelIdx = log2(uiSize) - log2(pGrid->uiMinSize);
	iMaxLevelIdx = bForInsert ? iInitialLevelIdx + 1 : pGrid->iGridLevelCount;

	addVec3same(vMid, -fRadius, vMin);
	addVec3same(vMid, fRadius, vMax);

	state.checkType = CHECK_SPHERE;
	state.pGrid = pGrid;
	state.peaCells = peaCells;
	copyVec3(vMid, state.sphere.vMid);
	state.sphere.fRadius = fRadius;
	state.sphere.fRadiusSquared = SQR(fRadius);

	PERFINFO_AUTO_START_L2("cell culling", 1);
	for (iLevelIdx = iInitialLevelIdx; iLevelIdx < iMaxLevelIdx; ++iLevelIdx)
	{
		vecLocToIntLoc(pGrid, iLevelIdx, vMin, iMinLoc);
		vecLocToIntLoc(pGrid, iLevelIdx, vMax, iMaxLoc);
		vecLocToIntLoc(pGrid, iLevelIdx, vMid, iMidLoc);

		iLoc[3] = iMinLoc[3];
		for (iLoc[2] = iMinLoc[2]; iLoc[2] <= iMaxLoc[2]; ++iLoc[2])
		{
			for (iLoc[1] = iMinLoc[1]; iLoc[1] <= iMaxLoc[1]; ++iLoc[1])
			{
				for (iLoc[0] = iMinLoc[0]; iLoc[0] <= iMaxLoc[0]; ++iLoc[0])
				{
					if (!bForInsert)
						pCell = getCellForLoc(pGrid, iLoc, false);
					if ((bForInsert || pCell) && vecDistanceSquaredToIntLoc(pGrid, vMid, iMidLoc, iLoc) <= state.sphere.fRadiusSquared)
					{
						if (bForInsert)
							pCell = getCellForLoc(pGrid, iLoc, true);
						if (pCell)
						{
							pCell->uiLastVisitedTag = pGrid->uiVisitTag;
							steaPush(peaCells, pCell, pGrid);
						}
					}
				}
			}
		}

		if (iEndSize < 0)
			iEndSize = eaSize(peaCells);
	}
	PERFINFO_AUTO_STOP_L2();

	if (!bForInsert && pGrid->iGridLevelCount > 1 && iInitialLevelIdx > 0)
		recurseDown(&state, iInitialSize, iEndSize);

	PERFINFO_AUTO_STOP_L2();
}

static void getCellsForBox(SparseGrid* pGrid, const Vec3 vMin, const Vec3 vMax, const Mat4 mWorldMat, const Mat4 mInvWorldMat, SparseGridCell ***peaCells, bool bForInsert)
{
	SparseGridCheckState state;
	IVec4 iMinLoc, iMaxLoc, iLoc;
	Vec3 vMinWorld, vMaxWorld;
	SparseGridCell* pCell = NULL;
	U32 uiSize;
	int iLevelIdx, iInitialLevelIdx, iMaxLevelIdx, iInitialSize = eaSize(peaCells), iEndSize = -1;
	F32 fSize;

	PERFINFO_AUTO_START_FUNC_L2();

	fSize = vMax[0] - vMin[0];
	MAX1(fSize, vMax[1] - vMin[1]);
	MAX1(fSize, vMax[2] - vMin[2]);

	uiSize = pow2(round(ceil(fSize*0.25f)));
	uiSize = CLAMP(uiSize, pGrid->uiMinSize, pGrid->uiMaxSize);
	iInitialLevelIdx = log2(uiSize) - log2(pGrid->uiMinSize);
	iMaxLevelIdx = bForInsert ? iInitialLevelIdx + 1 : pGrid->iGridLevelCount;

	mulBoundsAA(vMin, vMax, mWorldMat, vMinWorld, vMaxWorld);
	if (!bForInsert)
	{
		MINVEC3(vMaxWorld, pGrid->vBoundsMax, vMaxWorld);
		MAXVEC3(vMinWorld, pGrid->vBoundsMin, vMinWorld);
	}

	state.checkType = CHECK_BOX;
	state.pGrid = pGrid;
	state.peaCells = peaCells;
	copyVec3(vMin, state.box.vMin);
	copyVec3(vMax, state.box.vMax);
	copyMat4(mInvWorldMat, state.box.mInvWorldMat);

	PERFINFO_AUTO_START_L2("cell culling", 1);
	for (iLevelIdx = iInitialLevelIdx; iLevelIdx < iMaxLevelIdx; ++iLevelIdx)
	{
		vecLocToIntLoc(pGrid, iLevelIdx, vMinWorld, iMinLoc);
		vecLocToIntLoc(pGrid, iLevelIdx, vMaxWorld, iMaxLoc);

		iLoc[3] = iMinLoc[3];
		for (iLoc[2] = iMinLoc[2]; iLoc[2] <= iMaxLoc[2]; ++iLoc[2])
		{
			for (iLoc[1] = iMinLoc[1]; iLoc[1] <= iMaxLoc[1]; ++iLoc[1])
			{
				for (iLoc[0] = iMinLoc[0]; iLoc[0] <= iMaxLoc[0]; ++iLoc[0])
				{
					if (!bForInsert)
						pCell = getCellForLoc(pGrid, iLoc, false);
					if (bForInsert || pCell)
					{
						bool collides;
						if (bForInsert)
						{
							Vec3 vCellMin, vCellMax, vLocalCellMin, vLocalCellMax;
							scaleVec3(iLoc, (F32)pGrid->aGridLevels[iLevelIdx].uiSize, vCellMin);
							addVec3same(vCellMin, (F32)pGrid->aGridLevels[iLevelIdx].uiSize, vCellMax);
							mulBoundsAA(vCellMin, vCellMax, state.box.mInvWorldMat, vLocalCellMin, vLocalCellMax);
							collides = boxBoxCollision(vLocalCellMin, vLocalCellMax, vMin, vMax);
						}
						else
						{
							collides = !pCell->bBoundsEmpty && boxBoxCollision(pCell->vBoundsMin, pCell->vBoundsMax, vMinWorld, vMaxWorld);
						}

						if (collides)
						{
							if (bForInsert)
								pCell = getCellForLoc(pGrid, iLoc, true);
							if (pCell)
							{
								pCell->uiLastVisitedTag = pGrid->uiVisitTag;
								steaPush(peaCells, pCell, pGrid);
							}
						}
					}
				}
			}
		}

		if (iEndSize < 0)
			iEndSize = eaSize(peaCells);
	}
	PERFINFO_AUTO_STOP_L2();

	if (!bForInsert && pGrid->iGridLevelCount > 1 && iInitialLevelIdx > 0)
		recurseDown(&state, iInitialSize, iEndSize);

	PERFINFO_AUTO_STOP_L2();
}

static void getCellsForFrustum(SparseGrid* pGrid, const Frustum* pFrustum, SparseGridOcclusionCallback occlusionFunc, GfxOcclusionBuffer *pOcclusionBuffer, SparseGridCell ***peaCells)
{
	SparseGridCheckState state;
	int i, iInitialSize = eaSize(peaCells);

	PERFINFO_AUTO_START_FUNC_L2();

	state.checkType = CHECK_FRUSTUM;
	state.pGrid = pGrid;
	state.peaCells = peaCells;
	state.frustum.pFrustum = pFrustum;
	state.frustum.occlusionFunc = occlusionFunc;
	state.frustum.pOcclusionBuffer = pOcclusionBuffer;

	PERFINFO_AUTO_START_L2("cell culling", 1);
	for (i = 0; i < eaSize(&pGrid->eaRootCells); ++i)
	{
		SparseGridCell* pCell = pGrid->eaRootCells[i];
		Vec3 vCellMin, vCellMax, vCellMid;
		F32 fCellRadius = (F32)pGrid->aGridLevels[pCell->iLoc[3]].uiSize * 0.5f * 1.73205f;
		int clipped;

		scaleVec3(pCell->iLoc, (F32)pGrid->aGridLevels[pCell->iLoc[3]].uiSize, vCellMin);
		addVec3same(vCellMin, (F32)pGrid->aGridLevels[pCell->iLoc[3]].uiSize * 0.5f, vCellMid);
		addVec3same(vCellMin, (F32)pGrid->aGridLevels[pCell->iLoc[3]].uiSize, vCellMax);

		if (!(clipped = frustumCheckSphereWorld(pFrustum, vCellMid, fCellRadius)))
			continue;

		if (checkBoundsNotVisible(&state, vCellMin, vCellMax, clipped, false))
			continue;

		pCell->uiLastVisitedTag = pGrid->uiVisitTag;
		steaPush(peaCells, pCell, pGrid);
	}
	PERFINFO_AUTO_STOP_L2();

	if (pGrid->iGridLevelCount > 1)
		recurseDown(&state, iInitialSize, eaSize(peaCells));

	PERFINFO_AUTO_STOP_L2();
}

static void sparseGridInsert(SparseGrid* pGrid, SparseGridEntry **ppEntry, void *pNode, const Vec3 vMid, F32 fRadius)
{
	SparseGridEntry *pEntry;
	SparseGridCell *pParent;
	int i;

	PERFINFO_AUTO_START_FUNC();

	pEntry = mpAlloc(pGrid->pEntryMempool);

	copyVec3(vMid, pEntry->bounds.vMid);
	pEntry->bounds.fRadius = fRadius;
	pEntry->is_box = false;

	pEntry->pNode = pNode;
	pEntry->pGrid = pGrid;
	pEntry->ppSelf = ppEntry;
	
	getCellsForSphere(pGrid, vMid, fRadius, &pEntry->eaCells, true);
	for (i = 0; i < eaSize(&pEntry->eaCells); ++i)
	{
		steaPush(&pEntry->eaCells[i]->eaEntries, pEntry, pGrid);

		for (pParent = pEntry->eaCells[i]; pParent; pParent = pParent->pParentCell)
			pParent->bBoundsDirty = true;
	}

	pGrid->bBoundsDirty = true;

	*ppEntry = pEntry;
	PERFINFO_AUTO_STOP();
}

static void sparseGridInsertBox(SparseGrid* pGrid, SparseGridEntry **ppEntry, void *pNode, const Vec3 vMin, const Vec3 vMax)
{
	SparseGridEntry *pEntry;
	SparseGridCell *pParent;
	int i;

	PERFINFO_AUTO_START_FUNC();

	pEntry = mpAlloc(pGrid->pEntryMempool);

	copyVec3(vMin, pEntry->box_bounds.vMin);
	copyVec3(vMax, pEntry->box_bounds.vMax);
	pEntry->is_box = true;

	pEntry->pNode = pNode;
	pEntry->pGrid = pGrid;
	pEntry->ppSelf = ppEntry;

	getCellsForBox(pGrid, vMin, vMax, unitmat, unitmat, &pEntry->eaCells, true);
	for (i = 0; i < eaSize(&pEntry->eaCells); ++i)
	{
		steaPush(&pEntry->eaCells[i]->eaEntries, pEntry, pGrid);

		for (pParent = pEntry->eaCells[i]; pParent; pParent = pParent->pParentCell)
			pParent->bBoundsDirty = true;
	}

	pGrid->bBoundsDirty = true;

	*ppEntry = pEntry;
	PERFINFO_AUTO_STOP();
}

__forceinline static void sparseGridMoveInternal(SparseGrid* pGrid, SparseGridEntry **ppEntry, void *pNode, const Vec3 vMid, F32 fRadius, const Vec3 vMin, const Vec3 vMax)
{
	bool bChanged = false;
	SparseGridEntry *pEntry = *ppEntry;
	SparseGridCell *pParent;
	int i;

	if (!pEntry)
	{
		if (vMid)
			sparseGridInsert(pGrid, ppEntry, pNode, vMid, fRadius);
		else
			sparseGridInsertBox(pGrid, ppEntry, pNode, vMin, vMax);
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	assert(pEntry->pGrid == pEntry->pGrid);
	assert(pEntry->ppSelf == ppEntry);
	assert((pEntry->is_box && vMin && vMax) || (!pEntry->is_box && vMid));

	if (vMid)
	{
		copyVec3(vMid, pEntry->bounds.vMid);
		pEntry->bounds.fRadius = fRadius;
	}
	else
	{
		copyVec3(vMin, pEntry->box_bounds.vMin);
		copyVec3(vMax, pEntry->box_bounds.vMax);
	}
	
	pEntry->pNode = pNode;

	steaSetSize(&pGrid->eaTempCells, 0, pGrid);

	if (vMid)
		getCellsForSphere(pGrid, vMid, fRadius, &pGrid->eaTempCells, true);
	else
		getCellsForBox(pGrid, vMin, vMax, unitmat, unitmat, &pGrid->eaTempCells, true);

	if (eaSize(&pGrid->eaTempCells) != eaSize(&pEntry->eaCells))
	{
		bChanged = true;
	}
	else
	{
		for (i = 0; i < eaSize(&pEntry->eaCells); ++i)
		{
			ANALYSIS_ASSUME(pEntry->eaCells && pGrid->eaTempCells);
			if (pEntry->eaCells[i] != pGrid->eaTempCells[i])
			{
				bChanged = true;
				break;
			}
		}
	}

	if (bChanged)
	{
		// remove from old cells
		sparseGridRemoveFromCells(pEntry, pGrid->eaTempCells);

		// copy new cells from static array to the entry's array
		steaSetSize(&pEntry->eaCells, eaSize(&pGrid->eaTempCells), pGrid);
		memcpy(pEntry->eaCells, pGrid->eaTempCells, eaSize(&pGrid->eaTempCells) * sizeof(SparseGridCell*));

		// add to new cells
		for (i = 0; i < eaSize(&pEntry->eaCells); ++i)
			steaPush(&pEntry->eaCells[i]->eaEntries, pEntry, pGrid);
	}

	for (i = 0; i < eaSize(&pEntry->eaCells); ++i)
	{
		for (pParent = pEntry->eaCells[i]; pParent; pParent = pParent->pParentCell)
			pParent->bBoundsDirty = true;
	}
	pGrid->bBoundsDirty = true;

	PERFINFO_AUTO_STOP();
}

void sparseGridMove(SparseGrid* pGrid, SparseGridEntry **ppEntry, void *pNode, const Vec3 vMid, F32 fRadius)
{
	sparseGridMoveInternal(pGrid, ppEntry, pNode, vMid, fRadius, NULL, NULL);
}

void sparseGridMoveBox( SparseGrid* pGrid, SparseGridEntry **ppEntry, void *pNode, const Vec3 vMin, const Vec3 vMax )
{
	sparseGridMoveInternal(pGrid, ppEntry, pNode, NULL, 0, vMin, vMax);
}

static void updateCellBounds(SparseGrid *pGrid, SparseGridCell *pCell)
{
	int i;

	if (!pCell || !pCell->bBoundsDirty)
		return;

	setVec3same(pCell->vBoundsMin, 8e16);
	setVec3same(pCell->vBoundsMax, -8e16);

	for (i = 0; i < eaSize(&pCell->eaEntries); ++i)
	{
		SparseGridEntry *pEntry = pCell->eaEntries[i];
		if (pEntry->is_box)
		{
			vec3RunningMin(pEntry->box_bounds.vMin, pCell->vBoundsMin);
			vec3RunningMax(pEntry->box_bounds.vMax, pCell->vBoundsMax);
		}
		else
		{
			Vec3 vMin, vMax;
			addVec3same(pEntry->bounds.vMid, -pEntry->bounds.fRadius, vMin);
			addVec3same(pEntry->bounds.vMid, pEntry->bounds.fRadius, vMax);
			vec3RunningMin(vMin, pCell->vBoundsMin);
			vec3RunningMax(vMax, pCell->vBoundsMax);
		}
	}

	for (i = 0; i < eaSize(&pCell->eaChildCells); ++i)
	{
		SparseGridCell *pChildCell = pCell->eaChildCells[i];
		updateCellBounds(pGrid, pChildCell);
		if (!pChildCell->bBoundsEmpty)
		{
			vec3RunningMin(pChildCell->vBoundsMin, pCell->vBoundsMin);
			vec3RunningMax(pChildCell->vBoundsMax, pCell->vBoundsMax);
		}
	}

	pCell->bBoundsEmpty = pCell->vBoundsMax[0] < pCell->vBoundsMin[0] || pCell->vBoundsMax[1] < pCell->vBoundsMin[1] || pCell->vBoundsMax[2] < pCell->vBoundsMin[2];
	pCell->bBoundsDirty = false;
}

static void sparseGridUpdateBounds(SparseGrid* pGrid)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	setVec3same(pGrid->vBoundsMin, 8e16);
	setVec3same(pGrid->vBoundsMax, -8e16);

	for (i = 0; i < eaSize(&pGrid->eaRootCells); ++i)
	{
		SparseGridCell* pCell = pGrid->eaRootCells[i];
		updateCellBounds(pGrid, pCell);
		if (!pCell->bBoundsEmpty)
		{
			vec3RunningMin(pCell->vBoundsMin, pGrid->vBoundsMin);
			vec3RunningMax(pCell->vBoundsMax, pGrid->vBoundsMax);
		}
	}

	pGrid->bBoundsDirty = false;

	PERFINFO_AUTO_STOP();
}

// Returns all nodes overlapping your aabb
bool sparseGridFindInBoxEA(SparseGrid* pGrid, const Vec3 vMin, const Vec3 vMax, const Mat4 mWorldMat, void*** peaNodes)
{
	bool bSuccess = false;
	Mat4 mInvWorldMat;
	Vec3 vWorldMin, vWorldMax;
	int i, j;

	if (!pGrid->iCellCount)
		return false;

	PERFINFO_AUTO_START_FUNC();

	if (pGrid->bBoundsDirty)
		sparseGridUpdateBounds(pGrid);

	if (pGrid->vBoundsMax[0] < pGrid->vBoundsMin[0])
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	mulBoundsAA(vMin, vMax, mWorldMat, vWorldMin, vWorldMax);
	if (!boxBoxCollision(pGrid->vBoundsMin, pGrid->vBoundsMax, vWorldMin, vWorldMax))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	for (i = 0; i < eaSize(&pGrid->eaRootCells); ++i)
	{
		SparseGridCell *pCell = pGrid->eaRootCells[i];
		if (!pCell->bBoundsEmpty && boxBoxCollision(pCell->vBoundsMin, pCell->vBoundsMax, vWorldMin, vWorldMax))
			break;
	}
	if (i == eaSize(&pGrid->eaRootCells))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	pGrid->uiVisitTag++;

	if (!mWorldMat)
		mWorldMat = unitmat;

	steaSetSize(&pGrid->eaTempCells, 0, pGrid);
	getCellsForBox(pGrid, vMin, vMax, mWorldMat, mInvWorldMat, &pGrid->eaTempCells, false);

	PERFINFO_AUTO_START_L2("entry culling", 1);
	for (i = 0; i < eaSize(&pGrid->eaTempCells); ++i)
	{
		SparseGridCell *pCell = pGrid->eaTempCells[i];
		for (j = 0; j < eaSize(&pCell->eaEntries); ++j)
		{
			SparseGridEntry *pEntry = pCell->eaEntries[j];
			Vec3 vLocalMid;

			if (pEntry->uiLastVisitedTag == pGrid->uiVisitTag)
				continue;

			pEntry->uiLastVisitedTag = pGrid->uiVisitTag;
			if (pEntry->is_box)
			{
				if (orientBoxBoxCollision(vMin, vMax, mWorldMat, pEntry->box_bounds.vMin, pEntry->box_bounds.vMax, unitmat))
				{
					bSuccess = true;
					steaPush(peaNodes, pEntry->pNode, pGrid);
				}
			}
			else
			{
				mulVecMat4(pEntry->bounds.vMid, mInvWorldMat, vLocalMid);
				if (boxSphereCollision(vMin, vMax, vLocalMid, pEntry->bounds.fRadius))
				{
					bSuccess = true;
					steaPush(peaNodes, pEntry->pNode, pGrid);
				}
			}
			
		}
	}
	PERFINFO_AUTO_STOP_L2();

	PERFINFO_AUTO_STOP();

	return bSuccess;
}

// Returns all nodes overlapping your sphere
bool sparseGridFindInSphereEA(SparseGrid* pGrid, const Vec3 vSphere, F32 fRadius, void*** peaNodes)
{
	bool bSuccess = false;
	int i, j;

	if (!pGrid->iCellCount)
		return false;

	PERFINFO_AUTO_START_FUNC();

	if (pGrid->bBoundsDirty)
		sparseGridUpdateBounds(pGrid);

	if (pGrid->vBoundsMax[0] < pGrid->vBoundsMin[0])
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (!boxSphereCollision(pGrid->vBoundsMin, pGrid->vBoundsMax, vSphere, fRadius))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	pGrid->uiVisitTag++;

	steaSetSize(&pGrid->eaTempCells, 0, pGrid);
	getCellsForSphere(pGrid, vSphere, fRadius, &pGrid->eaTempCells, false);

	PERFINFO_AUTO_START_L2("entry culling", 1);
	for (i = 0; i < eaSize(&pGrid->eaTempCells); ++i)
	{
		SparseGridCell *pCell = pGrid->eaTempCells[i];
		for (j = 0; j < eaSize(&pCell->eaEntries); ++j)
		{
			SparseGridEntry *pEntry = pCell->eaEntries[j];
			if (pEntry->uiLastVisitedTag == pGrid->uiVisitTag)
				continue;

			pEntry->uiLastVisitedTag = pGrid->uiVisitTag;
			if (pEntry->is_box)
			{
				if (boxSphereCollision(pEntry->box_bounds.vMin, pEntry->box_bounds.vMax, vSphere, fRadius))
				{
					bSuccess = true;
					steaPush(peaNodes, pEntry->pNode, pGrid);
				}
			}
			else
			{
				if (sphereSphereCollision(vSphere, fRadius, pEntry->bounds.vMid, pEntry->bounds.fRadius))
				{
					bSuccess = true;
					steaPush(peaNodes, pEntry->pNode, pGrid);
				}
			}
		}
	}
	PERFINFO_AUTO_STOP_L2();

	PERFINFO_AUTO_STOP();

	return bSuccess;
}

// Returns all nodes overlapping your frustum
bool sparseGridFindInFrustumEA(SparseGrid* pGrid, const Frustum *pFrustum, SparseGridOcclusionCallback occlusionFunc, GfxOcclusionBuffer *pOcclusionBuffer, void*** peaNodes)
{
	bool bSuccess = false;
	int i, j;

	if (!pGrid->iCellCount)
		return false;

	PERFINFO_AUTO_START_FUNC();

	if (pGrid->bBoundsDirty)
		sparseGridUpdateBounds(pGrid);

	if (pGrid->vBoundsMax[0] < pGrid->vBoundsMin[0])
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (!frustumCheckBoundingBox(pFrustum, pGrid->vBoundsMin, pGrid->vBoundsMax, NULL, false))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	pGrid->uiVisitTag++;

	steaSetSize(&pGrid->eaTempCells, 0, pGrid);
	getCellsForFrustum(pGrid, pFrustum, occlusionFunc, pOcclusionBuffer, &pGrid->eaTempCells);

	PERFINFO_AUTO_START_L2("entry culling", 1);
	for (i = 0; i < eaSize(&pGrid->eaTempCells); ++i)
	{
		SparseGridCell *pCell = pGrid->eaTempCells[i];
		for (j = 0; j < eaSize(&pCell->eaEntries); ++j)
		{
			SparseGridEntry *pEntry = pCell->eaEntries[j];
			if (pEntry->uiLastVisitedTag == pGrid->uiVisitTag)
				continue;

			pEntry->uiLastVisitedTag = pGrid->uiVisitTag;
			if (pEntry->is_box)
			{
				if (frustumCheckBoundingBox(pFrustum, pEntry->box_bounds.vMin, pEntry->box_bounds.vMax, NULL, false))
				{
					bSuccess = true;
					steaPush(peaNodes, pEntry->pNode, pGrid);
				}
			}
			else
			{
				if (frustumCheckSphereWorld(pFrustum, pEntry->bounds.vMid, pEntry->bounds.fRadius))
				{
					bSuccess = true;
					steaPush(peaNodes, pEntry->pNode, pGrid);
				}
			}
		}
	}
	PERFINFO_AUTO_STOP_L2();

	PERFINFO_AUTO_STOP();

	return bSuccess;
}

bool sparseGridFindInFrustumCustom(SparseGrid* pGrid, const Frustum *pFrustum, SparseGridOcclusionCallback occlusionFunc, GfxOcclusionBuffer *pOcclusionBuffer, SparseGridFindCallback callback, void* pUserData)
{
	bool bSuccess = false;
	int i, j;

	if (!pGrid->iCellCount)
		return false;

	PERFINFO_AUTO_START_FUNC();

	if (pGrid->bBoundsDirty)
		sparseGridUpdateBounds(pGrid);

	if (pGrid->vBoundsMax[0] < pGrid->vBoundsMin[0])
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (!frustumCheckBoundingBox(pFrustum, pGrid->vBoundsMin, pGrid->vBoundsMax, NULL, false))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	pGrid->uiVisitTag++;

	steaSetSize(&pGrid->eaTempCells, 0, pGrid);
	getCellsForFrustum(pGrid, pFrustum, occlusionFunc, pOcclusionBuffer, &pGrid->eaTempCells);

	PERFINFO_AUTO_START_L2("entry culling", 1);
	for (i = 0; i < eaSize(&pGrid->eaTempCells); ++i)
	{
		SparseGridCell *pCell = pGrid->eaTempCells[i];
		for (j = 0; j < eaSize(&pCell->eaEntries); ++j)
		{
			SparseGridEntry *pEntry = pCell->eaEntries[j];
			if (pEntry->uiLastVisitedTag == pGrid->uiVisitTag)
				continue;

			pEntry->uiLastVisitedTag = pGrid->uiVisitTag;

			if (pEntry->is_box)
			{
				if (frustumCheckBoundingBox(pFrustum, pEntry->box_bounds.vMin, pEntry->box_bounds.vMax, NULL, false))
				{
					bSuccess = true;
					callback(pEntry->pNode, pUserData);
				}
			}
			else
			{
				if (frustumCheckSphereWorld(pFrustum, pEntry->bounds.vMid, pEntry->bounds.fRadius))
				{
					bSuccess = true;
					callback(pEntry->pNode, pUserData);
				}
			}
		}
	}
	PERFINFO_AUTO_STOP_L2();

	PERFINFO_AUTO_STOP();

	return bSuccess;
}

