#pragma once
GCC_SYSTEM

#include "referencesystem.h"
#include "structDefines.h"

typedef struct DynNode DynNode;
typedef U32 dtNode;
typedef struct DynSkeleton DynSkeleton;

typedef enum eDynNodeFlags
{
	ednTrans =	( 1 << 0 ), // Do not inherit translation from parent
	ednRot =		( 1 << 1 ), // Do not inherit rotation from parent
	ednScale =	( 1 << 2 ), // Do not inherit scale from parent
	ednLocalRot = (1 << 3),
	ednAll = ednTrans | ednRot | ednScale,
	ednNone = 0,
} eDynNodeFlags;

AUTO_STRUCT;
typedef struct DynTransform
{
	Quat qRot; AST(NAME(Rotation))
	Vec3 vPos; AST(NAME(Position))
	Vec3 vScale; AST(NAME(Scale))
} DynTransform;

extern const DynTransform xIdentity;



AUTO_STRUCT AST_CONTAINER AST_NONCONST_PREFIXSUFFIX( "#pragma pop_macro(\"vPos_DNODE\")\n#pragma pop_macro(\"qRot_DNODE\")\n#pragma pop_macro(\"vScale_DNODE\")\n#pragma pop_macro(\"vWorldSpacePos_DNODE\")\n#pragma pop_macro(\"qWorldSpaceRot_DNODE\")\n#pragma pop_macro(\"vWorldSpaceScale_DNODE\")\n","#pragma push_macro(\"vPos_DNODE\")\n#define vPos_DNODE PLEASE_USE_ACCESSORS_OR_MUTATORS\n#pragma push_macro(\"qRot_DNODE\")\n#define qRot_DNODE PLEASE_USE_ACCESSORS_OR_MUTATORS\n#pragma push_macro(\"vScale_DNODE\")\n#define vScale_DNODE PLEASE_USE_ACCESSORS_OR_MUTATORS\n#pragma push_macro(\"vWorldSpacePos_DNODE\")\n#define vWorldSpacePos_DNODE PLEASE_USE_ACCESSORS_OR_MUTATORS\n#pragma push_macro(\"qWorldSpaceRot_DNODE\")\n#define qWorldSpaceRot_DNODE PLEASE_USE_ACCESSORS_OR_MUTATORS\n#pragma push_macro(\"vWorldSpaceScale_DNODE\")\n#define vWorldSpaceScale_DNODE PLEASE_USE_ACCESSORS_OR_MUTATORS\n"); 
typedef struct DynNode // Please don't change this, the sizeof must be 128 for xbox performance
{
	// The order of these is tied to the order of DynTransform, and the functions:
	// dynNodeGetWorldSpaceTransform, dynNodeAssumeCleanGetWorldSpaceTransform, dynTransformSetFromBoneLocalPosInline,
	// dynNodeGetLocalTransformInline, and dynNodeSetFromTransformInline, as well as the global var "xIdentity"
	Quat qWorldSpaceRot_DNODE;
	Vec3 vWorldSpacePos_DNODE;
	Vec3 vWorldSpaceScale_DNODE;
	U32 uiFrame;					NO_AST
    dtNode guid;					NO_AST
	Quat qRot_DNODE;				AST(PERSIST, NO_TRANSACT)
	Vec3 vPos_DNODE;				AST(PERSIST, NO_TRANSACT)
	Vec3 vScale_DNODE;			AST(PERSIST, NO_TRANSACT)
    AST_STOP

    U32 uiTransformFlags:4;
	U32 uiLODLevel:4;
	U32 uiMaxLODLevelBelow:4;
	U32 uiTreeBranch:1;
	U32 uiUpdatedThisAnim:1;
	U32 uiSkeletonBone:1; // You can attach fx nodes to a skeleton, but we want to distinguish between those
	U32 uiCriticalBone:1;
	U32 uiCriticalChildren:1;
	U32 uiSkinningBone:1; // all skinning bones are critical, but not vice versa: there can be critical bones without skinning mats
	U32 uiLocked:1; // set this when you want the bone to not be animated, such as in STO glue up mount-point bones
	U32 uiHasBouncer:1;
	U32 uiUnManaged:1;
	U32 uiRagdollPoisonBit:1;
	U32 uiNonSkinnedGeo:1; // this gets set when geo is attached without being skinned to the model such as for weapons in NW
    U32 useme:4;
	U32 uiDirtyBits:1;
    U32 state:4;

	float fRadius;

    const char* pcTag;				AST(POOL_STRING)
	struct DynNode* pSibling;
	struct DynNode* pChild;
	struct DynNode* pParent;
	struct DynNode* pCriticalSibling;
	struct DynNode* pCriticalChild;
	DynSkeleton* pSkeleton;
	S16 iSkinningBoneIndex;
	S16 iCriticalBoneIndex;
	AST_START
} DynNode;

#if SPU
    #define DYNNODE_SAVE offsetof(DynNode, pcTag) // how much of DynNode to save
#endif

#ifndef _WIN64
STATIC_ASSERT(sizeof(DynNode)==128)
#endif

#pragma push_macro("vWorldSpacePos_DNODE")
#define vWorldSpacePos_DNODE PLEASE_USE_ACCESSORS_OR_MUTATORS
#pragma push_macro("qWorldSpaceRot_DNODE")
#define qWorldSpaceRot_DNODE PLEASE_USE_ACCESSORS_OR_MUTATORS
#pragma push_macro("vWorldSpaceScale_DNODE")
#define vWorldSpaceScale_DNODE PLEASE_USE_ACCESSORS_OR_MUTATORS

#pragma push_macro("vPos_DNODE")
#define vPos_DNODE PLEASE_USE_ACCESSORS_OR_MUTATORS
#pragma push_macro("qRot_DNODE")
#define qRot_DNODE PLEASE_USE_ACCESSORS_OR_MUTATORS
#pragma push_macro("vScale_DNODE")
#define vScale_DNODE PLEASE_USE_ACCESSORS_OR_MUTATORS

typedef DynNode* (*DynNodeAllocator)(void** ppUserData);
SA_RET_OP_VALID DynNode* dynNodeLinearAllocator(void** ppCurrentPointer);

void dynNodeParent(SA_PARAM_NN_VALID DynNode* pChild, SA_PARAM_NN_VALID const DynNode* pParent);
void dynNodeClearParent(SA_PARAM_NN_VALID DynNode* pChild);

void dynNodeReset(SA_PARAM_NN_VALID DynNode* pNode);
void dynNodeInitPersisted(SA_PARAM_NN_VALID DynNode* pNode);
SA_RET_OP_VALID DynNode* dynNodeAlloc();
void dynNodeFree( SA_PRE_NN_VALID SA_POST_P_FREE DynNode* pNode);
void dynNodeFreeTree( SA_PRE_NN_VALID SA_POST_P_FREE DynNode* pNode);
void dynNodeClearTree( SA_PARAM_NN_VALID DynNode* pNode);
void dynNodeAttemptToFreeUnmanagedNode(DynNode* pNode);

SA_RET_OP_VALID DynNode* dynNodeGetParent(SA_PARAM_NN_VALID DynNode* pNode);


// Allocs and copies the non-tree parts of a node.
void dynNodeWriteTree(FILE* file, DynNode* pNode);
SA_RET_OP_VALID DynNode* dynNodeReadTree(SA_PARAM_NN_NN_STR const char** ppcFileData, SA_PARAM_OP_VALID DynNode* pParent, U32* puiNumBones);

bool dynNodeSetFromMat4(DynNode* pNode, const Mat4 mat);

void dynNodeCopyWorldSpace(SA_PARAM_NN_VALID const DynNode* pSrc, SA_PARAM_NN_VALID DynNode* pDst);
void dynNodeCopyWorldSpaceWithFlags(SA_PARAM_NN_VALID const DynNode* pSrc, SA_PARAM_NN_VALID DynNode* pDst, U32 uiDynNodeXFormFlags);
void dynNodeMakeRotationRelative(SA_PARAM_NN_VALID DynNode* pNode, const Quat qParentRot);
SA_RET_OP_VALID DynNode* dynNodeTreeCopy(SA_PARAM_NN_VALID const DynNode* pSrc, SA_PARAM_OP_VALID DynNode* pParent, bool bSkeletonTree, SA_PARAM_OP_VALID DynSkeleton* pSkeleton, DynNodeAllocator dynNodeAllocator, void** ppUserData);
SA_RET_OP_VALID DynNode* dynNodeAllocTreeCopy(SA_PARAM_NN_VALID const DynNode* pSrc, SA_PARAM_OP_VALID DynNode* pParent, bool bSkeletonTree, SA_PARAM_OP_VALID DynSkeleton* pSkeleton);
void dynNodeTreeLinearCopy(SA_PARAM_NN_VALID const DynNode* pSrc, SA_PARAM_NN_VALID DynNode* pDst, U32 uiNumNodes, DynSkeleton* pSkeleton);
void dynNodeTreeLinearCopyTransformsOnly(SA_PARAM_NN_VALID const DynNode* pSrc, SA_PARAM_NN_VALID DynNode* pDst, U32 uiNumNodes);
void dynNodeTreeCopyTransformsOnLocalSkeletonOnly(SA_PARAM_NN_VALID const DynNode* pSrc, SA_PARAM_NN_VALID DynNode* pDst);

SA_RET_OP_VALID DynNode* dynNodeFindByName(SA_PARAM_NN_VALID DynNode* pRoot, SA_PARAM_NN_STR const char* pcTag);
SA_RET_OP_VALID const DynNode* dynNodeFindByNameConstEx(SA_PARAM_NN_VALID const DynNode* pRoot, SA_PARAM_NN_STR const char* pcTag, SA_PARAM_OP_STR const char *pcAlias, bool bUseMountNodeAliases);
#define dynNodeFindByNameConst(pRoot, pcTag, bUseMountNodeAliases) dynNodeFindByNameConstEx(pRoot, pcTag, NULL, bUseMountNodeAliases)
U32 dynNodeGetList(SA_PARAM_NN_VALID const DynNode* pRoot, SA_PARAM_NN_VALID const DynNode** ppNodes, U32 uiMaxNodes);

SA_RET_OP_VALID DynNode* dynNodeFindRoot(SA_PARAM_NN_VALID DynNode* pStart);
void dynNodeCleanDirtyBits(SA_PARAM_NN_VALID DynNode* pDynNode);


// Basic accessors. Note that these take const pointers, but internally cast them to non-const
// because calculations are cached on the struct

// These would be really great to inline, but we currently can't because someone decided to implement C++ via macros
void dynNodeGetWorldSpacePos( SA_PARAM_NN_VALID const DynNode* pDynNode, Vec3 vDst);
void dynNodeGetWorldSpaceRot( SA_PARAM_NN_VALID const DynNode* pDynNode, Quat qDst );
void dynNodeGetWorldSpaceScale( SA_PARAM_NN_VALID const DynNode* pDynNode, Vec3 vDst );
void dynNodeGetWorldSpaceMat(SA_PARAM_NN_VALID const DynNode* pDynNode, Mat4 mat, bool bScale);
void dynNodeGetWorldSpaceTransform(SA_PARAM_NN_VALID const DynNode* pDynNode, DynTransform* pTransform);
void dynNodeGetLocalPos(SA_PARAM_NN_VALID const DynNode* pDynNode, Vec3 vDst);
void dynNodeGetLocalRot(SA_PARAM_NN_VALID const DynNode* pDynNode, Quat qDst);
void dynNodeGetLocalScale(SA_PARAM_NN_VALID const DynNode* pDynNode, Vec3 vDst);
void dynNodeAssumeCleanGetWorldSpaceTransform(SA_PARAM_NN_VALID const DynNode *pDynNode, DynTransform *pTransform);

// Basic mutators
void dynNodeSetPos(SA_PARAM_NN_VALID DynNode* pDynNode, const Vec3 vInput);
void dynNodeSetRot(SA_PARAM_NN_VALID DynNode* pDynNode, const Quat qInput);
void dynNodeSetScale(SA_PARAM_NN_VALID DynNode* pDynNode, const Vec3 vInput);
void dynNodeClear(SA_PARAM_NN_VALID DynNode* pNode);
void dynNodeSetName( SA_PARAM_NN_VALID DynNode* pDynNode, const char* pcName );
int dynNodeGetAllocCount(void);
U32 dynNodeCalculateLODLevels(SA_PARAM_NN_VALID DynNode* pNode, F32* pfTotalLength);
F32 dynNodeCalculateMaxVisibilityRadius(SA_PARAM_NN_VALID const DynNode* pNode);
void dynNodeClearCriticalBits(SA_PARAM_NN_VALID DynNode* pRoot);
void dynNodeSetCriticalBit(SA_PARAM_NN_VALID DynNode* pRoot);
void dynNodeIndexSkinningNodes(SA_PARAM_NN_VALID DynNode* pRoot, int iIndex, int iMaxIndex);
void dynNodeFindCriticalTree(SA_PARAM_NN_VALID DynNode* pRoot);

typedef bool (*DynNodeCallback)(SA_PARAM_NN_VALID DynNode* pNode, void* pUserData);

void dynNodeProcessTree(SA_PARAM_NN_VALID DynNode* pRoot, DynNodeCallback callback, void* pUserData);

// for memory aligned nodes
void dynNodeCreateSkinningMat( SkinningMat4 mat, const DynNode* pNode, const Vec3 vBaseOffset, const Mat4 root_mat);
// for non-memory aligned nodes (such as those inlined as part of a DynDrawParticle)
void dynNodeCreateSkinningMatSlow( SkinningMat4 mat, const DynNode* pNode, const Vec3 vBaseOffset, const Mat4 root_mat);

//checks for memory alignment before running some functions
void dynNodeCalcWorldSpaceOneNode(DynNode* pNode);