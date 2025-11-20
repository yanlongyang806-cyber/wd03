
#undef vWorldSpacePos_DNODE 
#undef qWorldSpaceRot_DNODE 
#undef vWorldSpaceScale_DNODE 

#undef vPos_DNODE 
#undef qRot_DNODE 
#undef vScale_DNODE 

#define MAX_SKELETON_DEPTH 32

#define IS_DIRTY(a) (a->uiDirtyBits || (a->pSkeleton && a->uiFrame < a->pSkeleton->uiFrame) )

//#define VERIFY_TREE 1
#if VERIFY_TREE
static void dynNodeVerifyTreeIntegrityFunc(const DynNode* pNode);
#define dynNodeVerifyTreeIntegrity(a) dynNodeVerifyTreeIntegrityFunc(a)
#else
#define dynNodeVerifyTreeIntegrity(a) 
#endif

#if 0
#ifdef CHECK_FINITEVEC3
#undef CHECK_FINITEVEC3
#endif
// JE: Added these macros to track down something going to non-finite
#define CHECK_FINITEVEC3(vec) assert(FINITEVEC3(vec))
#else
#define CHECK_FINITEVEC3(vec)
#endif

#if !SPU
// Recursively applies transforms to calculate world space values for every node in tree
void dynNodeCalculateWorldSpace(SA_PARAM_NN_VALID DynNode* pNode)
{
	Vec3		vRotatedPos, vScaledPos;
	DynNode*	nodeStack[100];
	S32			stackPos = 0;

	PERFINFO_AUTO_START("dynNodeCalculateWorldSpace", 1);

	nodeStack[stackPos++] = pNode;

	while(stackPos){
		pNode = nodeStack[--stackPos];

		if(	pNode->pSibling &&
			stackPos < ARRAY_SIZE(nodeStack))
		{
			nodeStack[stackPos++] = pNode->pSibling;
		}

		if ( pNode->pParent )
		{
			Quat qCumulative;
			Vec3 vCumulativePos;
			Vec3 vCumulativeScale;

			// Rotation
			if ( pNode->uiTransformFlags & ednRot )
				copyQuat(pNode->pParent->qWorldSpaceRot_DNODE, qCumulative);
			else
				unitQuat(qCumulative);

			// Translation
			if ( pNode->uiTransformFlags & ednTrans )
				copyVec3(pNode->pParent->vWorldSpacePos_DNODE, vCumulativePos);
			else
				zeroVec3(vCumulativePos);

			// Scale
			if ( pNode->uiTransformFlags & ednScale )
				copyVec3(pNode->pParent->vWorldSpaceScale_DNODE, vCumulativeScale);
			else
				copyVec3(onevec3, vCumulativeScale);

			CHECK_FINITEVEC3(pNode->vPos_DNODE);
			CHECK_FINITEVEC3(pNode->vScale_DNODE);
			mulVecVec3(vCumulativeScale, pNode->vPos_DNODE, vScaledPos);
			mulVecVec3(vCumulativeScale, pNode->vScale_DNODE, pNode->vWorldSpaceScale_DNODE);
			quatMultiplyInline(pNode->qRot_DNODE, qCumulative, pNode->qWorldSpaceRot_DNODE);
			quatRotateVec3Inline(pNode->uiTransformFlags & ednLocalRot ? pNode->qWorldSpaceRot_DNODE : qCumulative, vScaledPos, vRotatedPos);
			addVec3(vRotatedPos, vCumulativePos, pNode->vWorldSpacePos_DNODE);
			CHECK_FINITEVEC3(pNode->vWorldSpacePos_DNODE);
		}
		else
		{
			CHECK_FINITEVEC3(pNode->vPos_DNODE);
			CHECK_FINITEVEC3(pNode->vScale_DNODE);
			copyVec3(pNode->vScale_DNODE, pNode->vWorldSpaceScale_DNODE);
			if (pNode->uiTransformFlags & ednLocalRot)
				quatRotateVec3Inline(pNode->qRot_DNODE, pNode->vPos_DNODE, pNode->vWorldSpacePos_DNODE);
			else
				copyVec3(pNode->vPos_DNODE, pNode->vWorldSpacePos_DNODE);
			copyQuat(pNode->qRot_DNODE, pNode->qWorldSpaceRot_DNODE);
			CHECK_FINITEVEC3(pNode->vWorldSpacePos_DNODE);
		}
		pNode->uiFrame = pNode->pSkeleton?pNode->pSkeleton->uiFrame:0;
		pNode->uiDirtyBits = 0;

		if(	pNode->pChild
			&& !pNode->pChild->uiCriticalBone
			&& stackPos < ARRAY_SIZE(nodeStack))
		{
			nodeStack[stackPos++] = pNode->pChild;
		}
	}

	PERFINFO_AUTO_STOP();
}
#endif

void dynNodeCalculateWorldSpaceNodeArray(DynNode** ppNodeArray, int iNodeCount)
{
	Vec3		vRotatedPos, vScaledPos;
	PERFINFO_AUTO_START(__FUNCTION__, 1);

	while(iNodeCount > 0){
		DynNode* pNode = ppNodeArray[--iNodeCount];
#if PLATFORM_CONSOLE
		if (iNodeCount > 0)
			PREFETCH(ppNodeArray[iNodeCount - 1]);
#endif

		if ( pNode->pParent )
		{
			Quat qCumulative;
			Vec3 vCumulativePos;
			Vec3 vCumulativeScale;

            assert(!IS_DIRTY(pNode->pParent));

			// Rotation
			if ( pNode->uiTransformFlags & ednRot )
				copyQuat(pNode->pParent->qWorldSpaceRot_DNODE, qCumulative);
			else
				unitQuat(qCumulative);

			// Translation
			if ( pNode->uiTransformFlags & ednTrans )
				copyVec3(pNode->pParent->vWorldSpacePos_DNODE, vCumulativePos);
			else
				zeroVec3(vCumulativePos);

			// Scale
			if ( pNode->uiTransformFlags & ednScale )
				copyVec3(pNode->pParent->vWorldSpaceScale_DNODE, vCumulativeScale);
			else
				copyVec3(onevec3, vCumulativeScale);

			CHECK_FINITEVEC3(pNode->vPos_DNODE);
			CHECK_FINITEVEC3(pNode->vScale_DNODE);
			mulVecVec3(vCumulativeScale, pNode->vPos_DNODE, vScaledPos);
			mulVecVec3(vCumulativeScale, pNode->vScale_DNODE, pNode->vWorldSpaceScale_DNODE);
			quatMultiplyInline(pNode->qRot_DNODE, qCumulative, pNode->qWorldSpaceRot_DNODE);
			quatRotateVec3Inline(pNode->uiTransformFlags & ednLocalRot ? pNode->qWorldSpaceRot_DNODE : qCumulative, vScaledPos, vRotatedPos);
			addVec3(vRotatedPos, vCumulativePos, pNode->vWorldSpacePos_DNODE);
			CHECK_FINITEVEC3(pNode->vWorldSpacePos_DNODE);
		}
		else
		{
			CHECK_FINITEVEC3(pNode->vPos_DNODE);
			CHECK_FINITEVEC3(pNode->vScale_DNODE);
			copyVec3(pNode->vScale_DNODE, pNode->vWorldSpaceScale_DNODE);
			if (pNode->uiTransformFlags & ednLocalRot)
				quatRotateVec3Inline(pNode->qRot_DNODE, pNode->vPos_DNODE, pNode->vWorldSpacePos_DNODE);
			else
				copyVec3(pNode->vPos_DNODE, pNode->vWorldSpacePos_DNODE);
			copyQuat(pNode->qRot_DNODE, pNode->qWorldSpaceRot_DNODE);
			CHECK_FINITEVEC3(pNode->vWorldSpacePos_DNODE);
		}
		pNode->uiFrame = pNode->pSkeleton?pNode->pSkeleton->uiFrame:0;
		pNode->uiDirtyBits = 0;
	}

	PERFINFO_AUTO_STOP();
}

#if !SPU

#define DYNNODE_TREE_STAT_ENABLE 0

#if DYNNODE_TREE_STAT_ENABLE
// use these to calculate average skeleton layout
#define DYNNODE_TREE_STAT(X) ++(X)
static int gnumParents = 0, gnumSiblings = 0, gnumLeaves = 0, gnumSingleChildren = 0, gnumNotDirty = 0;
static int gnumFastPath = 0, gnumSlowPath = 0, gnumLocalRotate = 0;
#else
#define DYNNODE_TREE_STAT(X)
#endif

// Recursively applies transforms to calculate world space values for every node in tree
void dynNodeCalculateWorldSpaceXbox(SA_PRE_NN_VALID SA_POST_OP_VALID DynNode* pNode)
{
#if DYNNODE_TREE_STAT_ENABLE
	int numParents = 0, numSiblings = 0, numLeaves = 0, numSingleChildren = 0, numNotDirty = 0;
	int numFastPath = 0, numSlowPath = 0, numLocalRotate = 0;
#endif
#define DYN_STACK_SIZE 128

	DynNode*	nodeStack[DYN_STACK_SIZE];
	S32			stackPos = 0;
	S32			stackEnd = 1;

	PERFINFO_AUTO_START_L3("dynNodeCalculateWorldSpace", 1);

	nodeStack[stackPos] = pNode;

	do 
	{
		Quat qCumulative;
		Vec3 vCumulativePos;
		Vec3 vCumulativeScale;
		DynNode * pParent, * pChild, * pSibling;

		pNode = nodeStack[stackPos];
		++stackPos;
		stackPos &= (DYN_STACK_SIZE - 1);
		pParent = pNode->pParent;

		if (!pNode->pSibling)
			DYNNODE_TREE_STAT(numSingleChildren);

		if ( pParent )
		{
			copyQuat(pParent->qWorldSpaceRot_DNODE, qCumulative);
			copyVec3(pParent->vWorldSpacePos_DNODE, vCumulativePos);
			copyVec3(pParent->vWorldSpaceScale_DNODE, vCumulativeScale);
		}
		else
		{
			unitQuat(qCumulative);
			copyVec3(zerovec3, vCumulativePos);
			copyVec3(onevec3, vCumulativeScale);
		}

		// process the node and all siblings
		while (pNode)
		{
			pChild = pNode->pChild;
			pSibling = pNode->pSibling;

			DYNNODE_TREE_STAT(numSiblings);

			if(	pChild && !pChild->uiCriticalBone)
			{
				DYNNODE_TREE_STAT(numParents);
				nodeStack[stackEnd++] = pChild;
				stackEnd &= (DYN_STACK_SIZE - 1);
			}
			else
				DYNNODE_TREE_STAT(numLeaves);

			PREFETCH(pSibling ? pSibling : nodeStack[stackPos]);

			CHECK_FINITEVEC3(pNode->vPos_DNODE);
			CHECK_FINITEVEC3(pNode->vScale_DNODE);

			// fast-path: all transforms inherited
			if ( ( pNode->uiTransformFlags & ednAll ) == ednAll )
			{
				Vec3 vRotatedScaledPos;
				Quat qWorld;

				// Sw = Sp * Sl
				mulVecVec3(vCumulativeScale, pNode->vScale_DNODE, pNode->vWorldSpaceScale_DNODE);

				// Qw = Qp * Ql
				quatMultiplyInline(pNode->qRot_DNODE, qCumulative, qWorld);
				copyQuat(qWorld, pNode->qWorldSpaceRot_DNODE);

				// Pr = Sp * Pl
				mulVecVec3(vCumulativeScale, pNode->vPos_DNODE, vRotatedScaledPos);
				// Pr = Qw * Ps
				if (!(pNode->uiTransformFlags & ednLocalRot))
					// Pr = Qp * Ps
					copyQuat(qCumulative, qWorld);
				quatRotateVec3Inline(qWorld, vRotatedScaledPos, vRotatedScaledPos);

				// Pw = Pp + Pr
				addVec3(vRotatedScaledPos, vCumulativePos, pNode->vWorldSpacePos_DNODE);

				DYNNODE_TREE_STAT(numFastPath);
			}
			else
			{
				Vec3 vRotatedPos, vScaledPos;
				Quat qWorld;
				if ( pNode->uiTransformFlags & ednScale )
				{
					// Ps = Sp * Pl
					mulVecVec3(vCumulativeScale, pNode->vPos_DNODE, vScaledPos);
					// Sw = Sp * Sl
					mulVecVec3(vCumulativeScale, pNode->vScale_DNODE, pNode->vWorldSpaceScale_DNODE);
				}
				else
				{
					// Since Sp = < 1 1 1 >
					// Ps = Pl
					copyVec3(pNode->vPos_DNODE, vScaledPos);
					// Sw = Sl
					copyVec3(pNode->vScale_DNODE, pNode->vWorldSpaceScale_DNODE);
				}

				// Rotation
				if ( pNode->uiTransformFlags & ednRot )
					// Qw = Qp * Ql
					quatMultiplyInline(pNode->qRot_DNODE, qCumulative, qWorld);
				else
					// Qw = Ql
					copyQuat(pNode->qRot_DNODE, qWorld);
				copyQuat(qWorld, pNode->qWorldSpaceRot_DNODE);

				if (!(pNode->uiTransformFlags & ednLocalRot))
					copyQuat(qCumulative, qWorld);

				// Pr = Qw * Sw * Pl
				quatRotateVec3Inline(qWorld, vScaledPos, vRotatedPos);

				// Translation
				if ( pNode->uiTransformFlags & ednTrans )
				{
					addVec3(vRotatedPos, vCumulativePos, pNode->vWorldSpacePos_DNODE);
				}
				else
				{
					copyVec3(vRotatedPos, pNode->vWorldSpacePos_DNODE);
				}

				DYNNODE_TREE_STAT(numSlowPath);
			}

			CHECK_FINITEVEC3(pNode->vWorldSpacePos_DNODE);
			if (IS_DIRTY(pNode))
				DYNNODE_TREE_STAT(numNotDirty);

            pNode->uiDirtyBits = 0;
			pNode = pSibling;
		}
	} while ( stackPos != stackEnd );

#if DYNNODE_TREE_STAT_ENABLE
	gnumParents += numParents;
	gnumSiblings += numSiblings;
	gnumLeaves += numLeaves;
	gnumSingleChildren += numSingleChildren;
	gnumNotDirty += numNotDirty;
	gnumFastPath += numFastPath;
	gnumSlowPath += numSlowPath;
	gnumLocalRotate += numLocalRotate;
#endif

	PERFINFO_AUTO_STOP_L3();
#undef DYN_STACK_SIZE
}

DynNode* dynNodeGetParent(SA_PARAM_NN_VALID DynNode* pNode)
{
	return pNode->pParent;
}

#endif

void dynNodeCleanDirtyBitsDownToNode(DynNode* pDynNode)
{
	int iNodeCount = 1;
	DynNode* pParent = pDynNode->pParent;
	DynNode* pNodeArray[MAX_SKELETON_DEPTH];
    pNodeArray[0] = pDynNode;
	while ( pParent && IS_DIRTY(pParent) )
	{
        assert(iNodeCount < ARRAY_SIZE(pNodeArray));

        pNodeArray[iNodeCount++] = pParent;
		pParent = pParent->pParent;
	}

	dynNodeCalculateWorldSpaceNodeArray(pNodeArray, iNodeCount);
}

//
// ACCESSORS
//
void dynNodeGetWorldSpacePos( const DynNode* pDynNode, Vec3 vDst)
{
	PERFINFO_AUTO_START_L2(__FUNCTION__, 1);
	if ( IS_DIRTY(pDynNode) )
	{
		dynNodeCleanDirtyBitsDownToNode((DynNode*)pDynNode);
	}
	CHECK_FINITEVEC3(pDynNode->vWorldSpacePos_DNODE);
	copyVec3(pDynNode->vWorldSpacePos_DNODE, vDst);
	PERFINFO_AUTO_STOP_L2();
}

void dynNodeGetWorldSpaceRot( const DynNode* pDynNode, Quat qDst )
{
	PERFINFO_AUTO_START_L2(__FUNCTION__, 1);
	if ( IS_DIRTY(pDynNode) )
	{
		dynNodeCleanDirtyBitsDownToNode((DynNode*)pDynNode);
	}
	copyQuat(pDynNode->qWorldSpaceRot_DNODE, qDst);
	PERFINFO_AUTO_STOP_L2();
}

void dynNodeGetWorldSpaceScale( const DynNode* pDynNode, Vec3 vDst )
{
	PERFINFO_AUTO_START_L2(__FUNCTION__, 1);
	if ( IS_DIRTY(pDynNode) )
	{
		dynNodeCleanDirtyBitsDownToNode((DynNode*)pDynNode);
	}
	copyVec3(pDynNode->vWorldSpaceScale_DNODE, vDst);
	PERFINFO_AUTO_STOP_L2();
}

__forceinline void dynNodeGetWorldSpacePosRotScale( const DynNode* pDynNode, Vec4HP vPos, Vec4HP qRot, Vec4HP vScale )
{
	if ( IS_DIRTY(pDynNode) )
	{
		dynNodeCleanDirtyBitsDownToNode((DynNode*)pDynNode);
	}
	copyVec4H(*(Vec4H *)pDynNode->qWorldSpaceRot_DNODE, *qRot);
	copyVec4H(*(Vec4H *)pDynNode->vWorldSpacePos_DNODE, *vPos);
	copyVec3(pDynNode->vWorldSpaceScale_DNODE, Vec4HToVec4(*vScale));
}

void dynNodeGetWorldSpaceMat(const DynNode* pDynNode, Mat4 mat, bool bScale)
{
	PERFINFO_AUTO_START(__FUNCTION__, 1);
	if ( IS_DIRTY(pDynNode) )
	{
		dynNodeCleanDirtyBitsDownToNode((DynNode*)pDynNode);
	}

	quatToMat(pDynNode->qWorldSpaceRot_DNODE, mat);
	copyVec3(pDynNode->vWorldSpacePos_DNODE, mat[3]);

	if (bScale)
	{
		scaleMat3Vec3(mat, pDynNode->vWorldSpaceScale_DNODE);
	}

	PERFINFO_AUTO_STOP();
}

void dynNodeGetWorldSpaceTransform(const DynNode* pDynNode, DynTransform* pTransform)
{
	PERFINFO_AUTO_START(__FUNCTION__, 1);
	if ( IS_DIRTY(pDynNode) )
	{
		dynNodeCleanDirtyBitsDownToNode((DynNode*)pDynNode);
	}
	memcpy(pTransform, &pDynNode->qWorldSpaceRot_DNODE, sizeof(DynTransform));
	PERFINFO_AUTO_STOP();
}

void dynNodeAssumeCleanGetWorldSpaceTransform(const DynNode *pDynNode, DynTransform *pTransform)
{
	memcpy(pTransform, &pDynNode->qWorldSpaceRot_DNODE, sizeof(DynTransform));
}

