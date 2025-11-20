#include "ThreadSafeMemoryPool.h"

#include "StringCache.h"
#include "file.h"
#include "dynNode.h"
#include "dynNodeInline.h"
#include "dynFxInterface.h"
#include "dynSkeleton.h"
#include "endian.h"

const DynTransform xIdentity = 
{
	0.0f, 0.0f, 0.0f, -1.0f,
	0.0f, 0.0f, 0.0f,
	1.0f, 1.0f, 1.0f
};

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

#define MAX_NODE_TAG_LENGTH 128

#include "dynNodePrivate.h"
#include "WorldGridPrivate.h"

#include "AutoGen/dynNode_h_ast.c"

TSMP_DEFINE(DynNode);
static U32 uiSuperSizedDynNodeTSMP = 0;
static U32 uiSuperSizedDynNodeTSMPError = 0;

AUTO_RUN_EARLY;
void dynNodeInitTSMP(void)
{
	// this is a lot of memory, don't run it on things that won't need it
	if (GetAppGlobalType() == GLOBALTYPE_CLIENT			||
		GetAppGlobalType() == GLOBALTYPE_GAMESERVER		||
		GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER	||
		GetAppGlobalType() == GLOBALTYPE_GETVRML		||
		GetAppGlobalType() == GLOBALTYPE_SERVERBINNER	||
		GetAppGlobalType() == GLOBALTYPE_WEBREQUESTSERVER)
	{
		ASSERT_FALSE_AND_SET(uiSuperSizedDynNodeTSMP);
		TSMP_CREATE_ALIGNED(DynNode, 16384, 128); // 128 byte align these to fit in a cache line nicely, for animation performance		
	}
 	else
 	{
		// waste some memory & generate an error if nodes are ever allocated from a non-supersized dynnode memory pool but don't crash on costumers
 		TSMP_CREATE_ALIGNED(DynNode, 8, 128);
 	}
}

F32 g_dynNodeMaxDistOriginSqr = MAX_DYNDIST_ORIGIN_SQR * 4.0f;
F32 dynNodeSetWorldRangeLimit(F32 maxDistOriginSqr)
{
	g_dynNodeMaxDistOriginSqr = maxDistOriginSqr;
	return g_dynNodeMaxDistOriginSqr;
}


static void dynNodeRemoveChild(SA_PARAM_NN_VALID DynNode* pParent, SA_PARAM_NN_VALID DynNode* pToRemove)
{
	dynNodeVerifyTreeIntegrity(pParent);
	dynNodeVerifyTreeIntegrity(pToRemove);
	if ( pParent->pChild == pToRemove )
	{
		pParent->pChild->pParent = NULL;
		pParent->pChild = pToRemove->pSibling;
		pToRemove->pSibling = NULL;
		dynNodeVerifyTreeIntegrity(pParent);
		dynNodeVerifyTreeIntegrity(pToRemove);
		return;
	}
	else
	{
		DynNode* pPrevChild = pParent->pChild;
		DynNode* pChild = (pPrevChild)?pPrevChild->pSibling:NULL;

		while (pChild)
		{
			if ( pChild == pToRemove )
			{
				pChild->pParent = NULL;
				pPrevChild->pSibling = pChild->pSibling;
				pChild->pSibling = NULL;
				dynNodeVerifyTreeIntegrity(pParent);
				dynNodeVerifyTreeIntegrity(pToRemove);
				dynNodeVerifyTreeIntegrity(pPrevChild);
				return;
			}

			pPrevChild = pChild;
			pChild = pChild->pSibling;
		}
	}

	// If we got here, we failed to find that child
	Errorf("Failed to find child dynnode to remove");
	dynNodeVerifyTreeIntegrity(pParent);
	dynNodeVerifyTreeIntegrity(pToRemove);
}

static void dynNodePushChild(SA_PARAM_NN_VALID DynNode* pChild, SA_PARAM_NN_VALID DynNode* pParent)
{
	dynNodeVerifyTreeIntegrity(pChild);
	dynNodeVerifyTreeIntegrity(pParent);
	if (pChild->pSibling || pChild->pParent)
	{
		Errorf("Can not push dynNode as child of another dynnode when it has a sibling or parents!");
		return;
	}
	pChild->pParent = pParent;

	if ( !pParent->pChild )
	{
		pParent->pChild = pChild;
	}
	else
	{
		DynNode* pFirstSibling = pParent->pChild;
		while (pFirstSibling->pSibling)
			pFirstSibling = pFirstSibling->pSibling;
		pFirstSibling->pSibling = pChild;
	}

	dynNodeVerifyTreeIntegrity(pChild);
	dynNodeVerifyTreeIntegrity(pParent);
}


void dynNodeClearParent(DynNode* pChild)
{
	dynNodeVerifyTreeIntegrity(pChild);
	if ( pChild->pParent )
		dynNodeRemoveChild(pChild->pParent, pChild);
	if (!pChild->uiSkeletonBone)
		pChild->pSkeleton = NULL;
	dynNodeVerifyTreeIntegrity(pChild);
}


void dynNodeParent(DynNode* pChild, const DynNode* pParent)
{
	// This function does some const->non-const casting since you might want to attach to a node without being able to alter it
	// other than letting it know that the child is attached
	dynNodeVerifyTreeIntegrity(pChild);
	dynNodeVerifyTreeIntegrity(pParent);
	dynNodeClearParent(pChild);
	if ( pChild && pParent )
	{
		if ( !pChild->uiDirtyBits && pParent->uiDirtyBits )
			dynNodeSetDirtyInline(pChild);
		pChild->pSkeleton = pParent->pSkeleton;
		dynNodePushChild(pChild, (DynNode*)pParent);
	}
	dynNodeVerifyTreeIntegrity(pChild);
	dynNodeVerifyTreeIntegrity(pParent);
}

// Copies the non-tree aspects
void dynNodeCopyWorldSpace(const DynNode* pSrc, DynNode* pDst)
{
	if (IS_DIRTY(pSrc))
	{
		dynNodeCleanDirtyBitsDownToNode((DynNode*)pSrc);
	}
	CHECK_FINITEVEC3(pSrc->vWorldSpacePos_DNODE);
	dynNodeSetPosInline(pDst, pSrc->vWorldSpacePos_DNODE);
	copyQuatAligned(pSrc->qWorldSpaceRot_DNODE, pDst->qRot_DNODE);
	copyVec3(pSrc->vWorldSpaceScale_DNODE, pDst->vScale_DNODE);
	dynNodeSetDirtyInline(pDst);
}


void dynNodeCopyWorldSpaceWithFlags(const DynNode* pSrc, DynNode* pDst, U32 uiDynNodeXFormFlags)
{
	if (IS_DIRTY(pSrc))
	{
		dynNodeCleanDirtyBitsDownToNode((DynNode*)pSrc);
	}
	if ( uiDynNodeXFormFlags & ednTrans )
		dynNodeSetPosInline(pDst, pSrc->vWorldSpacePos_DNODE);
	if ( uiDynNodeXFormFlags & ednRot )
		copyQuat(pSrc->qWorldSpaceRot_DNODE, pDst->qRot_DNODE); // people can call this and the Fx system has unaligned DynNodes!  boo!
	if ( uiDynNodeXFormFlags & ednScale )
		copyVec3(pSrc->vWorldSpaceScale_DNODE, pDst->vScale_DNODE);
	dynNodeSetDirtyInline(pDst);
}

void dynNodeMakeRotationRelative(SA_PARAM_NN_VALID DynNode* pNode, const Quat qParentRot)
{
	Quat qParentInv, qTemp;
	quatInverse(qParentRot, qParentInv);

	quatMultiply(pNode->qRot_DNODE, qParentInv, qTemp);

	copyQuat(qTemp, pNode->qRot_DNODE);
	dynNodeSetDirtyInline(pNode);
}

void dynNodeReset(DynNode* pNode)
{
	memset(pNode, 0, sizeof(DynNode));
	unitQuat(pNode->qRot_DNODE);
	zeroVec3(pNode->vPos_DNODE);
	setVec3same(pNode->vScale_DNODE, 1.0f);
	pNode->uiTransformFlags = ednAll;
	dynNodeSetDirtyInline(pNode);
}

void dynNodeInitPersisted(DynNode* pNode)
{
	pNode->qRot_DNODE[3] = -1.0f;
	pNode->uiTransformFlags = ednAll;
	setVec3same(pNode->vScale_DNODE, 1.0f);
	dynNodeSetDirtyInline(pNode);
	assert(!pNode->pChild && !pNode->pSibling && !pNode->pParent);
}


void dynNodeCopy(SA_PARAM_NN_VALID const DynNode* pSrc, SA_PARAM_NN_VALID DynNode* pDst)
{
	memcpy(pDst, pSrc, sizeof(DynNode));
	pDst->pParent = pDst->pChild = pDst->pSibling = NULL;
	dynNodeSetDirtyInline(pDst);
}

DynNode* dynNodeAllocCopy(SA_PARAM_NN_VALID const DynNode* pNode)
{
	DynNode* pNewNode = dynNodeAlloc();
	dynNodeCopy(pNode, pNewNode);
	return pNewNode;
}


DynNode* dynNodeAlloc(void)
{
	DynNode* pNode;

	if (!uiSuperSizedDynNodeTSMP) {
		if (isDevelopmentMode()) {
			assertmsgf(uiSuperSizedDynNodeTSMP, "Caught unexpected call to allocate DynNodes with global type %u, modify code in dynNodeInitTSMP to properly handle this", GetAppGlobalType());
		} else if (FALSE_THEN_SET(uiSuperSizedDynNodeTSMPError)) {
			Errorf("Caught unexpected call to allocate DynNodes with global type %u, modify code in dynNodeInitTSMP to properly handle this", GetAppGlobalType());
		}
	}

	pNode = TSMP_CALLOC(DynNode);

	pNode->qRot_DNODE[3] = -1.0f;
	setVec3same(pNode->vScale_DNODE, 1.0f);
	pNode->uiTransformFlags = ednAll;
	pNode->uiDirtyBits = 1;
	pNode->iSkinningBoneIndex = -1;
	pNode->iCriticalBoneIndex = -1;

	return pNode;
}

DynNode* dynNodeAllocWrapper(void** unused)
{
	return dynNodeAlloc();
}

DynNode* dynNodeLinearAllocator(void** ppCurrentPointer)
{
	DynNode* pNew = *(DynNode**)ppCurrentPointer;
    *(DynNode**)ppCurrentPointer = pNew+1;
	return pNew;
}

static void dynNodeReparentChildren( SA_PARAM_NN_VALID DynNode* pParent, SA_PARAM_OP_VALID DynNode* pNewParent)
{
	DynNode* pChild = pParent->pChild;
	dynNodeVerifyTreeIntegrity(pParent);
	dynNodeVerifyTreeIntegrity(pNewParent);

	while (pChild)
	{
		DynNode* pOldChild = pChild;
		pChild->pParent = pNewParent;
		pChild = pChild->pSibling;
		if (pNewParent == NULL)
		{
			pOldChild->pSibling = NULL;
		}
	}
	pParent->pChild = NULL;
	dynNodeVerifyTreeIntegrity(pChild);
	dynNodeVerifyTreeIntegrity(pParent);
	dynNodeVerifyTreeIntegrity(pNewParent);
}

void dynNodeClear(DynNode* pNode)
{
	// First, patch up tree
	DynNode* pOldParent = pNode->pParent;
	if ( pOldParent )
	{
		DynNode* pChild = pNode->pChild;
		dynNodeRemoveChild(pOldParent, pNode);

		// Add node's child to our parent's children list
		while (pChild)
		{
			DynNode* pSibling = pChild->pSibling;
			dynNodeRemoveChild(pNode, pChild);
			dynNodePushChild(pChild, pOldParent);
			pChild = pSibling;
		}
	}
	dynNodeReparentChildren(pNode, pNode->pParent);

	if (pOldParent)
		dynNodeVerifyTreeIntegrity(pOldParent);
	if (pNode->pParent)
		dynNodeVerifyTreeIntegrity(pNode->pParent);
	dynNodeVerifyTreeIntegrity(pNode);

	// Make sure any references are NULLified
	RefSystem_RemoveReferent(pNode, true);
}

void dynNodeFree(DynNode* pNode)
{
	dynNodeClear(pNode);
	if (pNode->guid)
		dynNodeRemoveGuid(pNode->guid);
	TSMP_FREE(DynNode, pNode);
}

void dynNodeFreeTree(DynNode* pNode)
{
	DynNode* pLink = pNode->pChild;
	while (pLink )
	{
		DynNode* pSibling = pLink->pSibling;
		if (pLink->uiTreeBranch)
			dynNodeFreeTree(pLink);
		pLink = pSibling;
	}
	dynNodeVerifyTreeIntegrity(pNode);
	dynNodeFree(pNode);
}

void dynNodeClearTree(DynNode* pNode)
{
	DynNode* pLink = pNode->pChild;
	while (pLink )
	{
		DynNode* pSibling = pLink->pSibling;
		if (pLink->uiTreeBranch)
			dynNodeClearTree(pLink);
		pLink = pSibling;
	}
	dynNodeVerifyTreeIntegrity(pNode);
	dynNodeClear(pNode);
}

void dynNodeAttemptToFreeUnmanagedNode(DynNode* pNode)
{
	if (pNode && pNode->uiUnManaged)
	{
		int iNumRefs = RefSystem_GetReferenceCountForReferent(pNode);
		if (iNumRefs==0)
		{
			dynNodeFree(pNode);
		}
	}
}

static const DynNode* dynNodeFindRootConst(const DynNode* pNode)
{
	if (pNode->pParent)
		return dynNodeFindRootConst(pNode->pParent);
	return pNode;
}

static bool dynNodeVerifyTreeIntegrityHelper(const DynNode* pNode, StashTable stPreviousNodes)
{
	const DynNode* pChild = pNode->pChild;
	if (!stashAddressAddInt(stPreviousNodes, (void*)pNode, 1, false))
	{
		assert(0);
	}

	while (pChild)
	{
		if (pChild->pParent != pNode)
		{
			assert(0);
			return false;
		}
		if (!dynNodeVerifyTreeIntegrityHelper(pChild, stPreviousNodes))
			return false;
		pChild = pChild->pSibling;
	}
	return true;
}

static void dynNodeVerifyTreeIntegrityFunc(const DynNode* pNode)
{
	if (pNode)
	{
		const DynNode* pRoot = dynNodeFindRootConst(pNode);
		StashTable stPreviousNodes = stashTableCreateAddress(128);
		assert(dynNodeVerifyTreeIntegrityHelper(pRoot, stPreviousNodes));
		assert(stashAddressFindInt(stPreviousNodes, (void*)pNode, NULL));
		stashTableDestroy(stPreviousNodes);
		if (!pNode->pParent)
			assert(!pNode->pSibling);
		while (pNode)
			pNode = pNode->pSibling; // Make sure pointers are good at least
	}
}

// arbitrary symbols used to mark whether pointers exist in the tree
typedef enum eTreeFlags
{
	eTreeFlags_None = 0,
	eTreeFlags_Sibling = (1 << 0),
	eTreeFlags_Child = (1 << 1)
} eTreeFlags;


void dynNodeWriteTree(FILE* file, DynNode* pNode)
{
	// First write length of name, then string
	{
		U32 uiLen = (U32)strlen(pNode->pcTag);
		fwrite(&uiLen, sizeof(U32), 1, file);
		fwrite(pNode->pcTag, sizeof(char), uiLen+1, file);
	}
	// Write pos/rot
	fwrite(&pNode->vPos_DNODE, sizeof(Vec3), 1, file);
	fwrite(&pNode->qRot_DNODE, sizeof(Quat), 1, file);
	// Write tree flags
	{
		U8 uiTreeFlags = eTreeFlags_None;
		if ( pNode->pSibling )
			uiTreeFlags |= eTreeFlags_Sibling;
		if ( pNode->pChild )
			uiTreeFlags |= eTreeFlags_Child;
		fwrite(&uiTreeFlags, sizeof(U8), 1, file);
	}

	// Write the sibling part of the tree
	if ( pNode->pSibling )
		dynNodeWriteTree(file, pNode->pSibling);
	// Write the child part of the tree
	if ( pNode->pChild )
		dynNodeWriteTree(file, pNode->pChild);
}


DynNode* dynNodeReadTree(const char** ppcFileData, DynNode* pParent, U32* puiNumBones)
{
	DynNode* pNode = dynNodeAlloc();
	U8 uiTreeFlags;
	if (puiNumBones)
		++(*puiNumBones);
	pNode->pParent = pParent;
	// First read length of name, then string
	{
		U32 uiLen;
		char pcTag[MAX_NODE_TAG_LENGTH];
		fa_read(&uiLen, *ppcFileData, sizeof(uiLen));
		xbEndianSwapU32(uiLen);
		assert(uiLen < sizeof(pcTag));
		fa_read(pcTag, *ppcFileData, uiLen+1);
		pNode->pcTag = allocAddString(pcTag);
	}

	// read pos/rot
	fa_read(&pNode->vPos_DNODE, *ppcFileData, sizeof(Vec3));
	fa_read(&pNode->qRot_DNODE, *ppcFileData, sizeof(Quat));

	// Read tree flags
	fa_read(&uiTreeFlags, *ppcFileData, sizeof(U8));

	// Flip endianness, if xbox
	xbEndianSwapStruct(parse_DynNode, pNode);
	setVec3same(pNode->vScale_DNODE, 1.0f);

	pNode->uiDirtyBits = 1;
	pNode->uiTreeBranch = 1;

	// Read the sibling part of the tree
	if ( uiTreeFlags & eTreeFlags_Sibling)
		pNode->pSibling = dynNodeReadTree(ppcFileData, pNode->pParent, puiNumBones);
	// Read the child part of the tree
	if ( uiTreeFlags & eTreeFlags_Child )
		pNode->pChild = dynNodeReadTree(ppcFileData, pNode, puiNumBones);

	return pNode;
}



static void dynNodeDumpTree(SA_PRE_NN_VALID SA_POST_OP_VALID DynNode* pNode, int level)
{
	OutputDebugStringf("%*sNode 0x%p %s\n", level * 2, "", pNode, pNode->pcTag);
	OutputDebugStringf("%*s{\n", level * 2, "");
	++level;

	while ( pNode )
	{
		if (pNode->pChild)
		{
			ANALYSIS_ASSUME(pNode->pChild);
			dynNodeDumpTree(pNode->pChild, level);
		}
		pNode = pNode->pSibling;
	}

	--level;
	OutputDebugStringf("%*s}\n", level * 2, "");
}


typedef struct DynNodeTreeCopyHelper
{
	const DynNode* pSrc;
	DynNode* pNew;
} DynNodeTreeCopyHelper;

DynNode* dynNodeTreeCopy(const DynNode* pSrc, DynNode* pParent, bool bSkeletonTree, DynSkeleton* pSkeleton, DynNodeAllocator dynNodeAllocator, void** ppUserData)
{
	DynNodeTreeCopyHelper	nodeStack[100];
	int iStackPos = 0;

	DynNode* pNewRoot = dynNodeAllocator(ppUserData);
	dynNodeCopy(pSrc, pNewRoot);

	nodeStack[iStackPos].pSrc = pSrc;
	nodeStack[iStackPos++].pNew = pNewRoot;

	pNewRoot->pParent = pParent;

	//pattern used here should place nodes close in memory based on the order
	//they are accessed while updating a skeleton's transforms during animation

	while(iStackPos)
	{
		DynNodeTreeCopyHelper helper = nodeStack[--iStackPos];
		const DynNode* pLink = NULL;
		DynNode* pNewLink = NULL;

		if (bSkeletonTree)
		{
			helper.pNew->uiSkeletonBone = true;
			helper.pNew->pSkeleton = pSkeleton;
		}
		helper.pNew->uiTreeBranch = 1;

		if (helper.pSrc->pChild && iStackPos < ARRAY_SIZE(nodeStack))
		{
			DynNode* pNewChild = dynNodeAllocator(ppUserData);
			dynNodeCopy(helper.pSrc->pChild, pNewChild);

			nodeStack[iStackPos].pSrc = helper.pSrc->pChild;
			nodeStack[iStackPos++].pNew = pNewChild;

			pNewChild->pParent = helper.pNew;
			helper.pNew->pChild = pNewChild;
		}

		if (helper.pSrc->pSibling && iStackPos < ARRAY_SIZE(nodeStack))
		{
			DynNode* pNewSibling = dynNodeAllocator(ppUserData);
			dynNodeCopy(helper.pSrc->pSibling, pNewSibling);

			nodeStack[iStackPos].pSrc = helper.pSrc->pSibling;
			nodeStack[iStackPos++].pNew = pNewSibling;

			pNewSibling->pParent = helper.pNew->pParent;
			helper.pNew->pSibling = pNewSibling;
		}
	}

	dynNodeVerifyTreeIntegrity(pNewRoot);
	return pNewRoot;
}

DynNode* dynNodeAllocTreeCopy(const DynNode* pSrc, DynNode* pParent, bool bSkeletonTree, DynSkeleton* pSkeleton)
{
	return dynNodeTreeCopy(pSrc, pParent, bSkeletonTree, pSkeleton, dynNodeAllocWrapper, NULL);
}

void dynNodeTreeCopyTransformsOnLocalSkeletonOnly(const DynNode *pSrc, DynNode* pDst)
{
	//pNew should really be known as "pDst" here, but it's close enough I didn't feel the need to make a union variable or something like that

	static const char* spcWaist = NULL;
	DynNodeTreeCopyHelper nodeStack[100];
	int iStackPos = 0;

	// used to deal with run'n'gun breaking the rotation inheritance
	// should rework this to be data driven in the future similar to the other bones run'n'gun uses
	if (!spcWaist) {
		spcWaist = allocFindString("Waist");
	}

	nodeStack[iStackPos].pSrc = pSrc;
	nodeStack[iStackPos++].pNew = pDst;

	while(iStackPos)
	{
		DynNodeTreeCopyHelper helper = nodeStack[--iStackPos];

		if (helper.pSrc && helper.pSrc->uiSkeletonBone &&
			helper.pNew && helper.pNew->uiSkeletonBone)
		{
			assert(helper.pSrc->pcTag == helper.pNew->pcTag);

			copyQuatAligned(helper.pSrc->qWorldSpaceRot_DNODE, helper.pNew->qWorldSpaceRot_DNODE);
			copyVec3(helper.pSrc->vWorldSpacePos_DNODE, helper.pNew->vWorldSpacePos_DNODE);
			copyVec3(helper.pSrc->vWorldSpaceScale_DNODE, helper.pNew->vWorldSpaceScale_DNODE);

			copyQuatAligned(helper.pSrc->qRot_DNODE, helper.pNew->qRot_DNODE);
			copyVec3(helper.pSrc->vPos_DNODE, helper.pNew->vPos_DNODE);
			copyVec3(helper.pSrc->vScale_DNODE, helper.pNew->vScale_DNODE);

			// disable any waist bone weirdness from run'n'gun (since it breaks the rotation inheritance)
			// sorry, this is overly messy for being in DynNode code
			if (helper.pNew->pcTag == spcWaist)
			{
				DynTransform xParentWS, xParentWSInv;
				DynTransform xApplyWS, xApplyLS;
				assert(helper.pNew->pParent);

				dynNodeGetWorldSpaceTransform(helper.pNew->pParent, &xParentWS);
				dynTransformInverse(&xParentWS, &xParentWSInv);
				dynNodeGetWorldSpaceTransform(helper.pNew, &xApplyWS);
				dynTransformMultiply(&xApplyWS, &xParentWSInv, &xApplyLS);

				dynNodeSetFromTransformInline(helper.pNew, &xApplyLS);
				helper.pNew->uiTransformFlags |= ednRot;
				dynNodeCalcWorldSpaceOneNode(helper.pNew);
			}

			if (helper.pSrc->pChild	&& iStackPos < ARRAY_SIZE(nodeStack))
			{
				if (helper.pSrc->pChild->uiSkeletonBone) {
					assert(helper.pNew->pChild);
					nodeStack[iStackPos].pSrc = helper.pSrc->pChild;
					nodeStack[iStackPos++].pNew = helper.pNew->pChild;
				}
				// else skip attachments
			}

			if (helper.pSrc->pSibling && iStackPos < ARRAY_SIZE(nodeStack))
			{
				if (helper.pSrc->pSibling->uiSkeletonBone) {
					assert(helper.pNew->pSibling);
					nodeStack[iStackPos].pSrc = helper.pSrc->pSibling;
					nodeStack[iStackPos++].pNew = helper.pNew->pSibling;
				} else {
					// skip attachments
					nodeStack[iStackPos].pSrc = helper.pSrc->pSibling;
					nodeStack[iStackPos++].pNew = helper.pNew;
				}
			}
		}
		else if (!helper.pSrc->uiSkeletonBone && helper.pSrc->pSibling)
		{
			// skip attachments
			nodeStack[iStackPos].pSrc = helper.pSrc->pSibling;
			nodeStack[iStackPos++].pNew = helper.pNew;
		}
		else if (!helper.pNew->uiSkeletonBone && helper.pNew->pSibling)
		{
			// skip attachments
			nodeStack[iStackPos].pSrc = helper.pSrc;
			nodeStack[iStackPos++].pNew = helper.pNew->pSibling;
		}
	}
}

void dynNodeTreeLinearCopy(const DynNode* pSrc, DynNode* pDst, U32 uiNumNodes, DynSkeleton* pSkeleton)
{
	ptrdiff_t ptrDiff = (U8*)pDst - (U8*)pSrc;
	memcpy(pDst, pSrc, sizeof(DynNode) * uiNumNodes);
	{
		// Use pointer arithmetic to fixup the pointers of the copy to be internal, and set the parent
		U32 uiIndex;
		for (uiIndex=0; uiIndex<uiNumNodes; ++uiIndex)
		{
			DynNode* pNode = &pDst[uiIndex];
			pNode->pSkeleton = pSkeleton;
			if (pNode->pParent)
				pNode->pParent = (DynNode*)((U8*)pNode->pParent + ptrDiff);
			if (pNode->pSibling)
				pNode->pSibling = (DynNode*)((U8*)pNode->pSibling + ptrDiff);
			if (pNode->pChild)
				pNode->pChild = (DynNode*)((U8*)pNode->pChild + ptrDiff);
		}
	}
	dynNodeVerifyTreeIntegrity(pDst);
}

// I rearranged things in DynNode, so this won't work anymore.
#if 0
void dynNodeTreeLinearCopyTransformsOnly(const DynNode* pSrc, DynNode* pDst, U32 uiNumNodes)
{
	U32 uiIndex;
	for (uiIndex=0; uiIndex<uiNumNodes; ++uiIndex)
	{
		const DynNode* pSrcNode = &pSrc[uiIndex];
		DynNode* pDstNode = &pDst[uiIndex];
		memcpy(&pDstNode->vWorldSpacePos_DNODE, &pSrcNode->vWorldSpacePos_DNODE, sizeof(DynTransform) * 2);
	}
}
#endif

DynNode* dynNodeFindByName(DynNode* pRoot, const char* pcTag)
{
	DynNode* pLink = pRoot->pChild;
	if ( pRoot->pcTag && inline_stricmp(pRoot->pcTag, pcTag) == 0 )
		return pRoot;
	while ( pLink )
	{
		DynNode* pResult = dynNodeFindByName(pLink, pcTag);
		if ( pResult )
			return pResult;
		pLink = pLink->pSibling;
	}

	return NULL;
}


const DynNode* dynNodeFindByNameConstEx(const DynNode* pRoot, const char* pcTag, const char *pcAlias, bool bUseMountNodeAliases)
{
	static const char* pchBetweenSubsPooledString = NULL;
	const DynNode* pLink = pRoot->pChild;
	const char *pcFindNode = FIRST_IF_SET(pcAlias, pcTag);

	if (!pchBetweenSubsPooledString)
		pchBetweenSubsPooledString = allocFindString("InBetweenSubs");

	if (!pcAlias				&&
		pRoot->uiSkeletonBone	&&
		pRoot->pSkeleton)
	{
		//should only ever happen if the initial node has a skeleton, optimizations below should catch it after that
		pcFindNode = pcAlias = dynSkeletonGetNodeAlias(pRoot->pSkeleton, pcTag, bUseMountNodeAliases);
	}

	if ( pRoot->pcTag && pRoot->pcTag == allocFindString(pcFindNode))
	{
		if (pLink && pLink->pcTag && pLink->pcTag == pchBetweenSubsPooledString)
		{
			pcAlias = NULL;

			while ( pLink )
			{
				if (!pcAlias				&&
					pLink->uiSkeletonBone	&&
					pLink->pSkeleton)
				{
					//do here so we don't run the alias finder once for every sibling of skeleton-less parent bone
					pcFindNode = pcAlias = dynSkeletonGetNodeAlias(pLink->pSkeleton, pcTag, bUseMountNodeAliases);
				}

				if (pLink->pChild)
				{
					const DynNode* pSubSkelResult = dynNodeFindByNameConstEx(pLink->pChild, pcTag, pcAlias, bUseMountNodeAliases);
					if (pSubSkelResult)
						return pSubSkelResult;
				}
				pLink = pLink->pSibling;
			}
		}
		return pRoot;
	}

	while ( pLink )
	{
		const DynNode* pResult;

		if (!pcAlias				&&
			pLink->uiSkeletonBone	&&
			pLink->pSkeleton)
		{
			//do here so we don't run the alias finder once for every sibling of skeleton-less parent bone
			pcFindNode = pcAlias = dynSkeletonGetNodeAlias(pLink->pSkeleton, pcTag, bUseMountNodeAliases);
		}

		pResult = dynNodeFindByNameConstEx(pLink, pcTag, pcAlias, bUseMountNodeAliases);
		if ( pResult )
		{
			return pResult;
		}
		pLink = pLink->pSibling;
	}

	return NULL;
}

U32 dynNodeGetList(const DynNode* pRoot, const DynNode** ppNodes, U32 uiMaxNodes)
{
	const DynNode* pLink = pRoot->pChild;
	U32 uiNumNodesAdded = 0;
	ppNodes[uiNumNodesAdded++] = pRoot;

	while (pLink && uiNumNodesAdded < uiMaxNodes)
	{
		uiNumNodesAdded += dynNodeGetList(pLink, ppNodes+uiNumNodesAdded, uiMaxNodes - uiNumNodesAdded);
		pLink = pLink->pSibling;
	}

	return uiNumNodesAdded;
}

DynNode* dynNodeFindRoot(DynNode* pStart)
{
	if ( pStart->pParent )
		return dynNodeFindRoot(pStart->pParent);
	// No parent, so return this
	return pStart;
}

bool dynNodeSetFromMat4( DynNode* pNode, const Mat4 mat )
{
	CHECK_FINITEVEC3(mat[3]);
	if(CHECK_DYNPOS_NONFATAL(mat[3])) {
		dynNodeSetPosInline(pNode, mat[3]);
		mat3ToQuat(mat, pNode->qRot_DNODE);
		getScale(mat, pNode->vScale_DNODE);
		dynNodeSetDirtyInline(pNode);
		return true;
	}
	return false;
}

void dynNodeCleanDirtyBits(DynNode* pDynNode)
{
	DynNode* pParent = dynNodeGetParent(pDynNode);
	while ( pParent && IS_DIRTY(pParent) )
	{
		pDynNode = pParent;
		pParent = dynNodeGetParent(pDynNode);
	}

#if PLATFORM_CONSOLE
	dynNodeCalculateWorldSpaceXbox(pDynNode);
#else
	dynNodeCalculateWorldSpace(pDynNode);
#endif
}

U8 dynNodeGetFlags( const DynNode* pDynNode )
{
	return dynNodeGetFlagsInline(pDynNode);
}

const char* dynNodeGetName(const DynNode* pDynNode)
{
	return dynNodeGetNameInline(pDynNode);
}

void dynNodeGetLocalPos(const DynNode* pDynNode, Vec3 vDst)
{
	dynNodeGetLocalPosInline(pDynNode, vDst);
}

void dynNodeGetLocalRot(const DynNode* pDynNode, Quat qDst)
{
	dynNodeGetLocalRotInline(pDynNode, qDst);
}

void dynNodeGetLocalScale(const DynNode* pDynNode, Vec3 vDst)
{
	dynNodeGetLocalScaleInline(pDynNode, vDst);
}

//
// MUTATORS
//
void dynNodeSetPos(DynNode* pDynNode, const Vec3 vInput)
{
	if(vInput)
	{
		dynNodeSetPosInline(pDynNode, vInput);
		if (!pDynNode->uiDirtyBits)
			dynNodeSetDirtyInline(pDynNode);
	}
}

void dynNodeSetRot(DynNode* pDynNode, const Quat qInput)
{
	if(qInput)
	{
		dynNodeSetRotInline(pDynNode, qInput);
		if (!pDynNode->uiDirtyBits)
			dynNodeSetDirtyInline(pDynNode);
	}
}

void dynNodeSetScale(DynNode* pDynNode, const Vec3 vInput)
{
	dynNodeSetScaleInline(pDynNode, vInput);
	if (!pDynNode->uiDirtyBits)
		dynNodeSetDirtyInline(pDynNode);
}

void dynNodeSetFlags(DynNode* pDynNode, U8 uiNewFlags)
{
	dynNodeSetFlagsInline(pDynNode, uiNewFlags);
}

void dynNodeSetName( DynNode* pDynNode, const char* pcName )
{
	pDynNode->pcTag = allocAddString(pcName);
}


int dynNodeGetAllocCount(void)
{
	return (int)TSMP_ALLOCATION_COUNT(DynNode);
}


// At level 0, everything is updated
// At level 1, every bone longer than 3 inches (or so) is updated
// At level 2, every bone longer than 8 inches (or so) is updated
// At level 3, only the root is updated (basic sway)
// Leaf nodes are automatically bumped down a level (drawn less often)

#define BONELENGTH_LEVEL_2 1.0f
#define BONELENGTH_LEVEL_1 0.4f

const char* pcHighAnimLODNodes[] = 
{
	"Head",
	"WepL",
	"WepR",
	"BendMystic",
	"Vehicle",
	"Emit_L",
	"Emit_R"
};

bool dynNodeTagForceHighLOD(const char* pcTag)
{
	int i;
	for (i=0; i< ARRAY_SIZE(pcHighAnimLODNodes); ++i)
	{
		if (inline_stricmp(pcTag, pcHighAnimLODNodes[i]) == 0)
			return true;
	}
	return false;
}

U32 dynNodeCalculateLODLevels(DynNode* pNode, F32* pfTotalLength)
{
	DynNode* pLink = pNode->pChild;
	F32 fTotalLength = 0.0f;

	pNode->uiMaxLODLevelBelow = 0;
	while ( pLink )
	{
		F32 fLength = 0.0f;
		U32 uiLinkLevel = dynNodeCalculateLODLevels(pLink, &fLength);
		pNode->uiMaxLODLevelBelow = MAX(pNode->uiMaxLODLevelBelow, uiLinkLevel);
		fTotalLength = MAX(fTotalLength, fLength);
		pLink = pLink->pSibling;
	}
	// Now process this node
	fTotalLength += lengthVec3(pNode->vPos_DNODE);

	if (!pNode->pParent)
	{
		pNode->uiLODLevel = 4;
	}
	else if (dynNodeTagForceHighLOD(pNode->pcTag)) // special case!
	{
		pNode->uiLODLevel = 4;
	}
	else
	{
		if (fTotalLength > BONELENGTH_LEVEL_2)
			pNode->uiLODLevel = 4;
		else if (fTotalLength > BONELENGTH_LEVEL_1)
			pNode->uiLODLevel = 2;
		else 
			pNode->uiLODLevel = 1;
	}
	pNode->uiMaxLODLevelBelow = MAX(pNode->uiMaxLODLevelBelow, pNode->uiLODLevel);
	if (pfTotalLength)
		*pfTotalLength = fTotalLength;
	return pNode->uiMaxLODLevelBelow;
}

F32 dynNodeCalculateMaxVisibilityRadius(const DynNode* pNode)
{
	DynNode* pLink = pNode->pChild;
	F32 fTotalLength = 0.0f;
	while ( pLink )
	{
		F32 fLength = dynNodeCalculateMaxVisibilityRadius(pLink);
		fTotalLength = MAX(fTotalLength, fLength);
		pLink = pLink->pSibling;
	}
	// Now process this node
	fTotalLength += lengthVec3(pNode->vPos_DNODE);

	return fTotalLength;
}


void dynNodeClearCriticalBits(DynNode* pRoot)
{
	DynNode* nodeStack[100];
	int iStackPos=0;

	nodeStack[iStackPos++] = pRoot;

	while(iStackPos)
	{
		DynNode* pNode = nodeStack[--iStackPos];

		if (pNode->pSibling && iStackPos < ARRAY_SIZE(nodeStack))
			nodeStack[iStackPos++] = pNode->pSibling;

		if (!pNode->uiSkeletonBone)
			continue;
		pNode->uiCriticalBone = 0;
		pNode->uiCriticalChildren = 0;
		pNode->uiSkinningBone = 0;
		pNode->uiNonSkinnedGeo = 0;

		if (pNode->pChild && iStackPos < ARRAY_SIZE(nodeStack))
			nodeStack[iStackPos++] = pNode->pChild;

	}

}

void dynNodeSetCriticalBit(DynNode* pRoot)
{
	DynNode* nodeStack[100];
	int iStackPos=0;

	nodeStack[iStackPos++] = pRoot;

	while(iStackPos)
	{
		DynNode* pNode = nodeStack[--iStackPos];

		if (!pNode->uiSkeletonBone || pNode->uiCriticalBone)
			continue;
		pNode->uiCriticalBone = 1;
		if (pNode->pParent && pNode->pParent->uiTreeBranch)
		{
			nodeStack[iStackPos++] = pNode->pParent;
			pNode->pParent->uiCriticalChildren = 1;
		}
	}
}

void dynNodeIndexSkinningNodes(DynNode* pRoot, int iIndex, int iMaxIndex)
{
	DynNode* nodeStack[100];
	int iStackPos=0;

	nodeStack[iStackPos++] = pRoot;

	while(iStackPos)
	{
		DynNode* pNode = nodeStack[--iStackPos];

		if (pNode->uiSkinningBone)
		{
			pNode->iSkinningBoneIndex = iIndex++;
			assert(pNode->iSkinningBoneIndex <= iMaxIndex);
		}
		else
			pNode->iSkinningBoneIndex = -1;

		if (pNode->uiCriticalChildren && iStackPos < ARRAY_SIZE(nodeStack))
			nodeStack[iStackPos++] = pNode->pChild;

		if (pNode->pSibling && iStackPos < ARRAY_SIZE(nodeStack))
		{
			nodeStack[iStackPos++] = pNode->pSibling;
		}
	}
}


void dynNodeFindCriticalTree(DynNode* pRoot)
{
	DynNode* nodeStack[100];
	int iStackPos=0;

	nodeStack[iStackPos++] = pRoot;

	while(iStackPos)
	{
		DynNode* pNode = nodeStack[--iStackPos];
		DynNode* pSibling = pNode->pSibling;
		DynNode* pChild = pNode->pChild;

		if (pChild && iStackPos < ARRAY_SIZE(nodeStack))
			nodeStack[iStackPos++] = pChild;

		if (pSibling && iStackPos < ARRAY_SIZE(nodeStack))
			nodeStack[iStackPos++] = pSibling;

		while (pChild && !pChild->uiCriticalBone && !pChild->uiCriticalChildren)
		{
			pChild = pChild->pSibling;
		}
		pNode->pCriticalChild = pChild;


		while (pSibling && !pSibling->uiCriticalBone)
		{
			pSibling = pSibling->pSibling;
		}
		pNode->pCriticalSibling = pSibling;
	}
}
					
void dynNodeProcessTree(DynNode* pRoot, DynNodeCallback callback, void* ppUserData)
{
	DynNode* nodeStack[100];
	int iStackPos=0;

	nodeStack[iStackPos++] = pRoot;

	while(iStackPos)
	{
		DynNode* pNode = nodeStack[--iStackPos];
		DynNode* pSibling = pNode->pSibling;
		DynNode* pChild = pNode->pChild;

	
		if (pSibling && iStackPos < ARRAY_SIZE(nodeStack))
			nodeStack[iStackPos++] = pSibling;

		if (callback(pNode, ppUserData))
		{
			if (pChild && iStackPos < ARRAY_SIZE(nodeStack))
				nodeStack[iStackPos++] = pChild;
		}
	}
}

void dynNodeCalcWorldSpaceOneNode(DynNode* pNode)
{
	Vec3 vRotatedPos, vScaledPos;
	assert(pNode->uiCriticalBone && pNode->pParent);
	pNode->uiFrame = pNode->pSkeleton ? pNode->pSkeleton->uiFrame : 0;

	mulVecVec3(pNode->pParent->vWorldSpaceScale_DNODE, pNode->vPos_DNODE, vScaledPos);
	mulVecVec3(pNode->pParent->vWorldSpaceScale_DNODE, pNode->vScale_DNODE, pNode->vWorldSpaceScale_DNODE);
	if ( pNode->uiTransformFlags & ednRot )
	{
		quatMultiplyInlineAligned(pNode->qRot_DNODE, pNode->pParent->qWorldSpaceRot_DNODE, pNode->qWorldSpaceRot_DNODE);
	} else {
		copyQuatAligned(pNode->qRot_DNODE, pNode->qWorldSpaceRot_DNODE);
	}
	quatRotateVec3Inline(pNode->uiTransformFlags & ednLocalRot ? pNode->qWorldSpaceRot_DNODE : pNode->pParent->qWorldSpaceRot_DNODE, vScaledPos, vRotatedPos);
	addVec3(vRotatedPos, pNode->pParent->vWorldSpacePos_DNODE, pNode->vWorldSpacePos_DNODE);

	/*
	Vec3 vRotatedPos, vScaledPos;
	assert(pNode->uiCriticalBone && pNode->pParent);
	pNode->uiFrame = pNode->pSkeleton?pNode->pSkeleton->uiFrame:0;

	if (pNode->pParent->uiTransformFlags & ednScale) {
		mulVecVec3(pNode->pParent->vWorldSpaceScale_DNODE, pNode->vPos_DNODE, vScaledPos);
		mulVecVec3(pNode->pParent->vWorldSpaceScale_DNODE, pNode->vScale_DNODE, pNode->vWorldSpaceScale_DNODE);
	} else {
		copyVec3(pNode->vPos_DNODE, vScaledPos);
		copyVec3(pNode->vScale_DNODE, pNode->vWorldSpaceScale_DNODE);
	}
	
	if ( pNode->uiTransformFlags & ednRot ) {
		quatMultiplyInline(pNode->qRot_DNODE, pNode->pParent->qWorldSpaceRot_DNODE, pNode->qWorldSpaceRot_DNODE);
		quatRotateVec3Inline(pNode->uiTransformFlags & ednLocalRot ? pNode->qWorldSpaceRot_DNODE : pNode->pParent->qWorldSpaceRot_DNODE, vScaledPos, vRotatedPos);
	} else {
		copyQuat(pNode->qRot_DNODE, pNode->qWorldSpaceRot_DNODE);
		quatRotateVec3Inline(pNode->uiTransformFlags & ednLocalRot ? pNode->qWorldSpaceRot_DNODE : unitquat, vScaledPos, vRotatedPos);
	}
	
	if (pNode->uiTransformFlags & ednTrans) {
		addVec3(vRotatedPos, pNode->pParent->vWorldSpacePos_DNODE, pNode->vWorldSpacePos_DNODE);
	} else {
		copyVec3(vRotatedPos, pNode->vWorldSpacePos_DNODE);
	}
	*/
}

static void _createBasePoseSkinningMat(SkinningMat4 mat, const Mat4 root_mat)
{
	if (root_mat)
		mat4toSkinningMat4(root_mat, mat);
	else
		copyMat34H((Mat34H *)&unitmat44H, (Mat34H *)mat);
}

extern int forceBasePose;
void dynNodeCreateSkinningMat( SkinningMat4 mat, const DynNode* pNode, const Vec3 vBaseOffset, const Mat4 root_mat) 
{
	Mat44H mWorldTrans;
	Vec4H vScale;
	Vec4H vOffset, vRotOffset;
	Vec4H vTrans;
	Vec4H qRot;

	PERFINFO_AUTO_START_FUNC_L2();

	if (forceBasePose)
	{
		_createBasePoseSkinningMat(mat, root_mat);
		return;
	}
	
	dynNodeGetWorldSpacePosRotScale(pNode,&vTrans, &qRot, &vScale);
	Vec4HToVec4(vScale)[3] = 1.0f;
	Vec4HToVec4(vTrans)[3] = 0.0f;
	quatToMat44Inline(Vec4HToVec4(qRot), Vec4HToVec4(mWorldTrans[0])); // for now

	// If I scale the matrix first, will that scale baseoffset appropriately?  (looks like no)
	setVec4H(vOffset,vBaseOffset[0],vBaseOffset[1],vBaseOffset[2],1.f);
	vecVecMul4H(vOffset,vScale,vOffset);

	vRotOffset = rotateVec4HTransformMat44H(vOffset, &mWorldTrans);

	scale3Mat34HVec4H((Mat34H *)&mWorldTrans, vScale);
	addVec4H(vRotOffset,vTrans,mWorldTrans[3]);

	transformMat44HtoSkinningMat4(&mWorldTrans, mat);

	PERFINFO_AUTO_STOP_L2();
}

void dynNodeCreateSkinningMatSlow( SkinningMat4 mat, const DynNode* pNode, const Vec3 vBaseOffset, const Mat4 root_mat) 
{
	Mat4 mWorldTrans, mWorldScale;
	Vec3 vRotOffset;
	Vec3 vScale;
	Vec3 vOffset;
	Quat qRot;

	PERFINFO_AUTO_START_FUNC_L2();

	if (forceBasePose)
	{
		_createBasePoseSkinningMat(mat, root_mat);
		return;
	}

	copyMat4(unitmat, mWorldScale);
	dynNodeGetWorldSpaceScale(pNode, vScale);

	dynNodeGetWorldSpaceRot(pNode, qRot);

	quatToMat(qRot, mWorldTrans);
	dynNodeGetWorldSpacePos(pNode, mWorldTrans[3]);

	scaleMat3Vec3(mWorldTrans, vScale);

	mulVecVec3(vBaseOffset, vScale, vOffset);
	quatRotateVec3Inline(qRot, vOffset, vRotOffset);

	addVec3(mWorldTrans[3], vRotOffset, mWorldTrans[3]);
	mat4toSkinningMat4(mWorldTrans, mat);


	PERFINFO_AUTO_STOP_L2();
}