#pragma once
GCC_SYSTEM

#include "wlModelEnums.h"

#include "referencesystem.h"
#include "resourceManager.h"

#include "Materials.h"

typedef struct DynSkeleton DynSkeleton;
typedef struct DynFxInfo DynFxInfo;
typedef struct Model Model;
typedef struct DynNode DynNode;
typedef struct DynFx DynFx;
typedef struct Material Material;
typedef struct WLCostume WLCostume;
typedef struct BasicTexture BasicTexture;
typedef struct CostumeTextureSwap CostumeTextureSwap;
typedef struct DynBaseSkeleton DynBaseSkeleton;
typedef struct MaterialNamedConstant MaterialNamedConstant;
typedef struct DynFxManager DynFxManager;
typedef struct WorldDrawableList WorldDrawableList;
typedef struct WorldInstanceParamList WorldInstanceParamList;
typedef struct GfxDynObjLightCache GfxDynObjLightCache;
typedef struct DynClothObject DynClothObject;
typedef struct DynClothObjectSavedState DynClothObjectSavedState;
typedef struct GfxSplat GfxSplat;
typedef struct DynFxSortBucket DynFxSortBucket;
typedef struct DynFxRef DynFxRef;
typedef U32 dtDrawSkeleton;

#define NUM_SPRITE_NAMED_CONSTANTS 5
#define MAX_MATERIAL_ADDS 4

extern const char* pcSpriteNamedConstants[];

typedef struct WLFXMaterialSwap
{
	Material* material_to_use;
	BasicTexture** texture_swaps[2];
	const char* texture_swap_names[2];
	MaterialNamedConstant mnc[3];
	F32 alpha;
	bool use_mnc[3];

	U32 dont_use_costume_constants : 1;
	U32 use_fx_constants : 1;
	U32 dissolve : 1;
	U32 do_texture_swap : 1;

} WLFXMaterialSwap;

typedef struct WLDynDrawParams
{
	Vec3 color;
	F32 alpha;
	Vec3 ambient_offset;
	Vec3 ambient_multiplier;
	WLFXMaterialSwap material_swap;
	WLFXMaterialSwap material_adds[MAX_MATERIAL_ADDS];
	WLFXMaterialSwap* current_material_swap;
	DynFxSortBucket* sort_bucket;
	U32 num_material_adds : 8;
	U32 is_costume : 1;
	U32 is_debris : 1;
	U32 is_screen_space : 1; // 2d
	U32 splats_invalid : 1;
	U32 draw_in_aux_visual_pass : 1;
	U32 iPriorityLevel : 3;
	U32 force_no_alpha : 1;
	U32 mod_color_on_all_costume_colors : 1;

	// for instancing of fx:
	WorldDrawableList	*pDrawableList;
	WorldInstanceParamList *pInstanceParamList;
	int					iDrawableResetCounter;

	// for debugging
	DynFxInfo* pInfo;
} WLDynDrawParams;

#define MAX_DRAW_MODEL_LODS 4

typedef struct DynDrawModel
{
	Model*				pModel;							// The model to draw
	Vec3				vBaseAttachOffset;				// The reference pose pos of the attachment bone, for skinning
	U8					uiNumNodesUsed[MAX_DRAW_MODEL_LODS];					// How many bones is this model skinned to
	U8*					apuiBoneIdxs[MAX_DRAW_MODEL_LODS];					// Indices into skeleton's skinning matrices for this frame
	const char*			pcMaterial[2];					// Material name for lazy init
	Material*			pMaterial[2];					// Material information
	BasicTexture**		eaTextureSwaps[2];					// Texture swaps for this model
	CostumeTextureSwap**	eaCostumeTextureSwaps[2];		// The texture swap strings, must be kept around so gfxlib can find the basic textures
	const DynNode*		pAttachmentNode;				// The node this model is attached to, for lighting
	const DynBaseSkeleton*	pBaseSkeleton;				// The base skeleton of this model, for skinning matrix generation
	MaterialNamedConstant** eaMatConstant[2];				// Any material constants sent from the costume to the material
	Mat4				mTransform;
	WorldDrawableList	*pDrawableList;
	WorldInstanceParamList *pInstanceParamList;
	DynClothObject*		pCloth;
	DynClothObjectSavedState *pClothSavedState;
	const char*			pcClothInfo;
	const char*			pcClothColInfo;
	const char*			pcOrigAttachmentBone;
	F32					fSortBias;
	U32					uiRequiredLOD;
	int					iDrawableResetCounter;
	U32					bOwnsDrawables : 1;
	U32					bLOD : 1;
	U32					bNoShadow : 1;
	U32					bCloth : 1;
	U32					bHasBodysockSwapSet : 1;
	U32					bRaycastable : 1;
	U32					bIsHidden : 1;
	U32					uiNumLODLevels:3;
} DynDrawModel;										// Information used to draw a skinned model, from dynamics (worldlib) to graphicslib


typedef struct DynDrawSkeletonAOSplat
{
	DynNode* pBone;
	GfxSplat* pAOSplat;
	U32 splatType;
} DynDrawSkeletonAOSplat;

#define MAX_WORLD_REGION_LOD_LEVELS 6

AUTO_STRUCT;
typedef struct WorldRegionLODSettings
{
	U32 uiNumLODLevels; AST( NAME(NumLevels))
	U32 uiBodySockLODLevel; AST( NAME(BodysockLevel))
	U32 uiIKLODLevel; NO_AST
	U32 uiWindLODLevel; NO_AST
	F32* eaLodDistance; AST(NAME(Distances))
	U32* eaDefaultMaxLODSkelSlots; AST(NAME(PerLevelMax))

	AST_STOP
	U32 uiMaxLODLevel;
	F32 LodDistance[MAX_WORLD_REGION_LOD_LEVELS];
	U32 DefaultMaxLODSkelSlots[MAX_WORLD_REGION_LOD_LEVELS];
	U32 MaxLODSkelSlots[MAX_WORLD_REGION_LOD_LEVELS];
} WorldRegionLODSettings;

extern WorldRegionLODSettings defaultLODSettings;
extern U32 LODSkeletonSlots[MAX_WORLD_REGION_LOD_LEVELS];


// This is for calculating visibility extents
typedef struct DynDrawVisibilityNode
{
	const char* pcBone;
	F32 fRadius;
} DynDrawVisibilityNode;

typedef struct DynDrawSkeleton DynDrawSkeleton;

typedef struct DynSkinningMatSet
{
	U8*					pSkinningMatsMemUnaligned;
	SkinningMat4*		pSkinningMats;
	volatile int		iRefCount;
} DynSkinningMatSet;

void dynSkinningMatSetIncrementRefCount(DynSkinningMatSet* pSet);
void dynSkinningMatSetDecrementRefCount(DynSkinningMatSet* pSet);
void dynSkinningMatSetInit(DynSkinningMatSet* pSet, U32 uiSkinningMatCount);

extern ParseTable parse_DynDrawSkeletonSaveData[];
#define TYPE_parse_DynDrawSkeletonSaveData DynDrawSkeletonSaveData
AUTO_STRUCT;
typedef struct DynDrawSkeletonSaveData
{
	DynSkeleton		*pSkeleton;		NO_AST
	DynFxRef		**eaDynFxRefs;	NO_AST
	const char		**eaSeveredBones;
} DynDrawSkeletonSaveData;

void dynDrawSkeletonSaveDataDestroy(DynDrawSkeletonSaveData *pData);
void dynDrawFxRefSaveDataDestroy(DynFxRef *pFxRef);

typedef struct DynDrawSkeleton
{
	DynSkeleton*		pSkeleton;						// The current animation and transform state
	DynDrawModel**		eaDynGeos;						// The collection of models to be drawn (the geometry for this skeleton)
	DynSkinningMatSet*	pCurrentSkinningMatSet;
	DynSkinningMatSet*	pSkinningMatSets[2];
	U32					uiSkinningMatCount;				// Number of skinning matrices allocated in the pBoneInfos array
	U8*					puiBoneIdxPool;
	DynFxRef**			eaDynFxRefs;					// An array of dynfx references that we care about
	DynFxManager*		pFxManager;						// Only used when reloading the dyndraw skeleton after a costume change
	dtDrawSkeleton		guid;
	GfxDynObjLightCache	*pLightCache;
	DynDrawModel**		eaClothModels;					// A subset of eaDynGeos, these are the models that are cloth objects
	DynDrawVisibilityNode** eaVisNodes;
	F32					fSendDistance;					// used to calculate the max LOD distance, for rescaling the other LODs
	U32					uiLODLevel;
	F32					fEntityAlpha;
	F32					fGeometryOnlyAlpha;
	F32					fFXDrivenAlpha;
	F32					fOtherAlpha;
	F32					fTotalAlpha;
	GfxSplat			*pSplatShadow;
	DynDrawSkeletonAOSplat** eaAOSplats;
	DynDrawSkeleton**	eaSubDrawSkeletons;
	const char**		eaSeveredBones;
	const char**		eaHiddenBoneVisSetBones;
	const char**		eaHiddenBoneVisSetBonesOld;
	struct {
		// Data cached for what assets should be kept loaded for this skeleton
		BasicTexture **eaTextures;
		Model **eaModels;
	} preload;
	U32					bPreloadFilledIn		: 1;
	U32					bInvalid				: 1; // used when the costume file is broken, to keep the object around but not try to draw it
	U32					bWorldLighting			: 1;
	U32					bDontDraw				: 1;
	U32					bBodySock				: 1;
	U32					bIsLocalPlayer			: 1;
	U32					bForceUnlit				: 1;
	U32					bFXCostume				: 1;
	U32					bDontCreateBodysock		: 1;
	U32					bUpdateDrawInfo			: 1;
} DynDrawSkeleton;									// Information used to draw a skinned skeleton, from dynamics (worldlib) to graphicslib

extern DynDrawSkeleton** eaDrawSkelList;

typedef struct DynSoftwareSkinData
{
	U8 uiSkinningMatIndex[4];
	F32 fWeight[4];
	U32 uiBoneIndex[4];
	Vec3 vVert;
	Vec3 vNorm;
} DynSoftwareSkinData;


SA_RET_NN_VALID DynDrawModel* dynDrawGeoCreate();

DynDrawSkeleton* dynDrawSkeletonCreateDbg( SA_PARAM_NN_VALID DynSkeleton* pSkeleton, SA_PARAM_NN_VALID const WLCostume* pCostume, DynFxManager* pFxManager, bool bAutoDraw, bool bIsLocalPlayer, bool bDontCreateBodysock MEM_DBG_PARMS);
#define dynDrawSkeletonCreate(pSkeleton, pCostume, pFxManager, bAutoDraw, bIsLocalPlayer, bDontCreateBodysock) dynDrawSkeletonCreateDbg(pSkeleton, pCostume, pFxManager, bAutoDraw, bIsLocalPlayer, bDontCreateBodysock MEM_DBG_PARMS_INIT)
void dynDrawSkeletonFreeDbg( SA_PRE_NN_VALID SA_POST_P_FREE DynDrawSkeleton* pToFree MEM_DBG_PARMS);
void dynDrawSkeletonFreeCB(DynDrawSkeleton* pToFree);
#define dynDrawSkeletonFree(pToFree) dynDrawSkeletonFreeDbg(pToFree MEM_DBG_PARMS_INIT)

void dynDrawClearCostumeBodysocks();
U32 dynDrawGetSkeletonList(SA_PARAM_NN_VALID DynDrawModel** ppDrawGeos, U32 uiMaxToReturn);

void dynDrawSkeletonReloadAllUsingCostume(const WLCostume* pReloadedCostume, enumResourceEventType eType);
void dynDrawSkeletonPushDynFx( SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkel, SA_PARAM_NN_VALID DynFx* pNewFx, const char *pcTreatAsCostumeFxTag);
void dynDrawSkeletonHardCopyDynFxRef(SA_PARAM_OP_VALID DynDrawSkeleton *pDrawSkel, SA_PARAM_NN_VALID DynFxRef *pNewFxRef);
void dynDrawSkeletonGetFXDrivenDrawParams( DynDrawSkeleton* pDrawSkel, WLDynDrawParams* pParams, const char* pcBoneName, DynDrawModel *pGeo );
void dynDrawSkeletonBasePose(SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkel);

void dynDrawClearPreSwapped(void);

// debugging stuff

void dynDebugSetDebugSkeleton(const DynDrawSkeleton* pDrawSkeleton);
SA_RET_NN_VALID const DynDrawSkeleton* dynDebugGetDebugSkeleton(void);
void dynDrawSkeletonUpdateLODLevel( SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkel, bool bBodySock);
void dynDrawSkeletonUpdateDrawInfoDbg( SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkel MEM_DBG_PARMS);
#define dynDrawSkeletonUpdateDrawInfo(pDrawSkel) dynDrawSkeletonUpdateDrawInfoDbg(pDrawSkel MEM_DBG_PARMS_INIT)
void dynDrawSkeletonUpdateCloth( SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkel, F32 fDeltaTime, Vec3 vDist, bool bMoving, bool bMounted);
void dynDrawSkeletonUpdate(SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkel, F32 fDeltaTime);
void dynDrawSkeletonAllocSkinningMats(SA_PARAM_NN_VALID DynDrawSkeleton* pDrawSkel);
F32 dynDrawSkeletonGetNodeRadius(DynDrawSkeleton* pDrawSkel, const char* pcTag);
void dynDrawSkeletonSetupBodysockTexture(WLCostume* pMyCostume);

void dynDrawSkeletonShowModelsAttachedToBone(DynDrawSkeleton* pSkeleton, const char *pcBoneTagName, bool bShow);

void dynDrawModelInitMaterialPointers( DynDrawModel* pGeo );
void dynDrawSkeletonSeverBones(DynDrawSkeleton* pDrawSkel, const char** ppcSeverBones, U32 uiNumBones);
void dynDrawSkeletonRestoreSeveredBones(DynDrawSkeleton* pDrawSkel, const char** ppcSeverBones, U32 uiNumBones);
bool dynDrawSkeletonCalculateNonSkinnedExtents(DynDrawSkeleton* pDrawSkel, bool bUnion);

void dynDrawSetupAnimBoneInfo(DynSkeleton *pSkeleton, bool bDontCreateBodysock, bool bThreaded);
