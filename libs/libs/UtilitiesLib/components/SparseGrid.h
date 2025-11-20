/***************************************************************************



***************************************************************************/

#pragma once
GCC_SYSTEM

#include "stdtypes.h"

typedef struct SparseGrid SparseGrid;
typedef struct SparseGridEntry SparseGridEntry;
typedef struct Frustum Frustum;
typedef struct GfxOcclusionBuffer GfxOcclusionBuffer;

typedef void (*SparseGridFindCallback)(void* pNode, void* pUserData);
typedef int (*SparseGridOcclusionCallback)(GfxOcclusionBuffer *zo, const Vec4 eyespaceBounds[8], int isZClipped);

SparseGrid* sparseGridCreateDebug( U32 uiMinSize, U32 uiMaxSize MEM_DBG_PARMS );
#define sparseGridCreate(uiMinSize, uiMaxSize) sparseGridCreateDebug(uiMinSize, uiMaxSize MEM_DBG_PARMS_INIT)
void sparseGridDestroy(SparseGrid* pGrid);

// the ppEntry address must remain invariant over the lifetime of the SparseGridEntry
void sparseGridMove(SparseGrid* pGrid, SparseGridEntry **ppEntry, void *pNode, const Vec3 vMid, F32 fRadius);
void sparseGridMoveBox(SparseGrid* pGrid, SparseGridEntry **ppEntry, void *pNode, const Vec3 vMin, const Vec3 vMax);
void sparseGridRemove(SparseGridEntry **ppEntry);

// Returns all nodes overlapping your sphere
bool sparseGridFindInSphereEA(SparseGrid* pGrid, const Vec3 vSphere, F32 fRadius, void*** peaNodes);

// Returns all nodes overlapping your bounding box
bool sparseGridFindInBoxEA(SparseGrid* pGrid, const Vec3 vMin, const Vec3 vMax, const Mat4 mWorldMat, void*** peaNodes);

// Returns all nodes overlapping your frustum
bool sparseGridFindInFrustumEA(SparseGrid* pGrid, const Frustum *pFrustum, SparseGridOcclusionCallback occlusionFunc, GfxOcclusionBuffer *pOcclusionBuffer, void*** peaNodes);

// For custom handling of every node in the sparse grid found within the frustum
bool sparseGridFindInFrustumCustom(SparseGrid* pGrid, const Frustum *pFrustum, SparseGridOcclusionCallback occlusionFunc, GfxOcclusionBuffer *pOcclusionBuffer, SparseGridFindCallback callback, void* pUserData);


