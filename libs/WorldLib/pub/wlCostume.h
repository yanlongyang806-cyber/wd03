#pragma once
GCC_SYSTEM

#include "referencesystem.h"
#include "Materials.h"
#include "dynBitField.h"
#include "Color.h"
#include "Capsule.h"
#include "wlSkelInfo.h"
#include "dynAnimNodeAlias.h"
#include "dynAnimNodeAuxTransform.h"

typedef struct StashTableImp* StashTable;
typedef struct DynBaseSkeleton DynBaseSkeleton;
typedef struct DynFxInfo DynFxInfo;
typedef struct ShaderTemplate ShaderTemplate;
typedef struct MaterialDraw MaterialDraw;
typedef struct Model Model;
typedef struct WorldDrawableList WorldDrawableList;
typedef struct WorldInstanceParamList WorldInstanceParamList;
typedef U64 TexHandle;
typedef struct DynParamBlock DynParamBlock;


AUTO_STRUCT AST_STARTTOK("");
typedef struct ScaleValue
{
	const char*	pcScaleGroup; 							AST(POOL_STRING STRUCTPARAM)		
	Vec3		vScaleInputs; 							AST(STRUCTPARAM)
} ScaleValue;

AUTO_STRUCT AST_STARTTOK("");
typedef struct CostumeTextureSwap
{
	const char*	pcOldTexture; 							AST(STRUCTPARAM POOL_STRING)		// The texture we want to replace
	const char*	pcNewTexture; 							AST(STRUCTPARAM POOL_STRING)		// The texture we want to replace the old texture with
	char* pcNewTextureNonPooled;
} CostumeTextureSwap;

AUTO_STRUCT AST_STARTTOK("");
typedef struct CostumeFXSwap
{
	REF_TO(DynFxInfo) hOldFx; 							AST(STRUCTPARAM REQUIRED)
	REF_TO(DynFxInfo) hNewFx; 							AST(STRUCTPARAM REQUIRED)
} CostumeFXSwap;

AUTO_STRUCT;
typedef struct WLCostumeMaterialInfo
{
	const char* pchMaterial;							AST(POOL_STRING)
	CostumeTextureSwap** eaTextureSwaps;				AST(NAME(TexSwap))
	MaterialNamedConstant** eaMatConstant;				AST(NAME("Set"))
} WLCostumeMaterialInfo;

AUTO_STRUCT AST_IGNORE(Color);
typedef struct WLCostumePart
{
	const char* pchBoneName;							AST(STRUCTPARAM POOL_STRING)
	const char*	pcOrigAttachmentBone;					AST(STRUCTPARAM POOL_STRING)

		// The name of the bone this costume part is attached to
	const char* pchGeometry;							AST(POOL_STRING)
		// The name of the geometry this costume part uses, if null it's the default of the costume
	const char* pcModel;								AST(POOL_STRING)
		// Override the default piece of geometry attached to the bone with this model in the geometry
	const char* pchMaterial;							AST(POOL_STRING)
		// Right now, only used for 2-sided cloth (capes)
	WLCostumeMaterialInfo* pSecondMaterialInfo;

	//REF_TO(SkelInfo)				hSubSkelInfo;		AST( NAME("SubSkelInfo") NON_NULL_REF REFDICT(SkelInfo) )

	const char* pcClothInfo;							AST(POOL_STRING)
	const char* pcClothColInfo;							AST(POOL_STRING)

	const char* pcStumpGeo;								AST(POOL_STRING)
	const char* pcStumpModel;							AST(POOL_STRING)

	REF_TO(DynAnimNodeAliasList)		hAnimNodeAliasList;
	REF_TO(DynAnimNodeAuxTransformList)	hAnimNodeAuxTransformList;

	// Cached model pointer
	Model* pCachedModel;								NO_AST

	Mat4 mTransform;

	// The name of the material this costume part uses
	CostumeTextureSwap** eaTextureSwaps;				AST(NAME(TexSwap))

	// A list of texture pairs that need to be swapped
	MaterialNamedConstant** eaMatConstant;				AST(NAME("Set"))

	// Pre-swapped drawables for interaction costumes
	WorldDrawableList* pWorldDrawableList;				NO_AST
	WorldInstanceParamList *pInstanceParamList;			NO_AST

	// The name of the material this costume part uses
	CostumeTextureSwap** eaTextureSwaps2;				AST(NAME(TexSwap2))

	// A list of texture pairs that need to be swapped
	MaterialNamedConstant** eaMatConstant2;				AST(NAME(Set2))

	U32 uiRequiredLOD;

	U32 bFreeable : 1;									NO_AST
	U32 bLOD : 1;										NO_AST
	U32 bCollisionOnly : 1;								NO_AST
	U32 bNoShadow : 1;
	U32 bRaycastable : 1;								NO_AST
	U32 bOptionalPart : 1;								NO_AST
} WLCostumePart;

AUTO_STRUCT;
typedef struct CostumeFX
{
	REF_TO(DynFxInfo) hFx;								AST(STRUCTPARAM REQUIRED)
	F32 fHue; 
	DynParamBlock *pParams;
} CostumeFX;

typedef struct WLCostume WLCostume;

AUTO_STRUCT;
typedef struct WLSubCostume
{
	REF_TO(WLCostume) hSubCostume;
	const char* pcAttachmentBone; AST(POOL_STRING)
} WLSubCostume;

AUTO_STRUCT;
typedef struct WLScaleAnimInterp
{
	const char* pcName; AST(POOL_STRING)
	F32 fValue;
} WLScaleAnimInterp;

AUTO_STRUCT;
typedef struct WLCostume
{
	const char*			pcName;							AST(KEY STRUCTPARAM POOL_STRING)
		// NPC and preset costumes will have name

	WLCostumePart**	eaCostumeParts;						AST( NAME("CostumePart") NAME(CostumePart))

	WLSubCostume**	eaSubCostumes;						AST( NAME("SubCostume") )
	const char* pcSubCostumeAttachmentBone;				AST( POOL_STRING )

		// Each part defines a single costume element

		// For this costume, replace any old fx calls with the new fx call name

	REF_TO(SkelInfo)				hSkelInfo;			AST( NAME("SkelInfo") NON_NULL_REF REFDICT(SkelInfo) )
	const char*						pcCollGeo;			AST( NAME("CollGeo") POOL_STRING)
	Model*							pCollGeo;			NO_AST // cached collision geometry
	U32								uNoCollision;		AST( NAME("NoCollision") )

	BodySockInfo* pBodySockInfo;

	ScaleValue**					eaScaleValue;		AST( NAME("Scale") )
	WLScaleAnimInterp**				eaScaleAnimInterp;

	const char**					eaConstantBits; 	AST( NAME("ConstantBits") POOL_STRING)
	DynBitFieldStatic				constantBits;		NO_AST

	CostumeFX**						eaFX;				AST( NAME("FX") )

	// TODO: How do we deal with scaling and animation specifics?
	CostumeFXSwap**					eaFXSwap;			AST( NAME("FXSwap") )

	const char*						pcBaseFileName; 	AST( POOL_STRING ) // this is used for recording what costume file this new costume is based off of on the server
	
	const char*						pcFileName;			AST( CURRENTFILE )
	int								iFileAge;			AST( TIMESTAMP )

	//	Set in costume dictionary update callback, used by headshots to know that they have to be reset.
	U32								uChangedTime;		NO_AST

	bool							bWorldLighting;		AST( NAME("WorldLighting") )

	// Bodysocks
	bool							bHasLOD;			NO_AST
	bool							bForceNoLOD;		NO_AST
	BasicTexture*					pBodysockTexture;	NO_AST
	bool							bBodysockTexCreated; NO_AST
	const char*						pcBodysockPose;		NO_AST
	S32								iBodysockSectionIndex; NO_AST
	Vec4							vBodysockTexXfrm;	NO_AST

	bool							bCollision;			NO_AST
	Vec3							vCollBoundsMin;		NO_AST
	Vec3							vCollBoundsMax;		NO_AST

	const char*						pcShieldGeometry;	AST(POOL_STRING)
	const char*						pcShieldAttachBone;	AST(POOL_STRING)
	Vec3							vShieldScale;

	Vec3							vExtentsMin;
	Vec3							vExtentsMax;

	// Perform starship glue-up adjustment (for STO)
	bool bAutoGlueUp;
	
	// Prevents cleanup of costume when it is unreferenced
	bool bNoAutoCleanup;

	// Used by runtime to know if costume is fully generated
	bool bComplete;

	// Used to determine a mounts global scale at runtime based on both costumes, 1.0 = 100% mount, 0.0 = 100% rider, -1.0 = OFF (uses a hard coded value when mounting happens)
	// this value should come from the CostumeConfig or a per-CSkel override
	F32 fMountRiderScaleBlend;

	// Used to determine a mounts global scale at runtime based on the mounts costume
	// this value should come from a power driven charater attrib
	F32 fMountScaleOverride;

	// Provides a description of collidable objects on the costume's skeleton
	// to apply to the cloth of any attached riders
	const char *pcMountClothCollisionInfo;	AST(POOL_STRING)

	// Used at runtime to tell if the costume is a mount or  a rider
	bool bMount;
	bool bRider;
	bool bRiderChild;

	// Terrain tilting controls
	bool bTerrainTiltApply;
	bool bTerrainTiltModifyRoot;
	F32 fTerrainTiltBaseLength;
	F32 fTerrainTiltStrength;
	F32 fTerrainTiltMaxBlendAngle;

	// Used to avoid wasting time
	bool bHasNodeAliases;
	bool bHasNodeAuxTransforms;

} WLCostume;

extern ParseTable parse_WLCostume[];
#define TYPE_parse_WLCostume WLCostume
extern ParseTable parse_WLCostumePart[];
#define TYPE_parse_WLCostumePart WLCostumePart

void wlCostumeFree(WLCostume* pCostume);
WLCostume* wlCostumeFromName(SA_PARAM_NN_STR const char* pcName);
const SkelInfo* wlCostumeGetSkeletonInfo(SA_PARAM_NN_VALID const WLCostume* pCostume);
const DynBaseSkeleton* wlCostumeGetBaseSkeleton(SA_PARAM_NN_VALID const WLCostume* pCostume);
const char* wlCostumeSwapFX(SA_PARAM_NN_VALID const WLCostume* pCostume, SA_PARAM_NN_STR const char* pcOldInfo);
bool verifyCostume(SA_PARAM_NN_VALID WLCostume* pCostume, bool bRescaleConstants);
void wlCostumeAddToDictionary(WLCostume* pCostume, const char* pcNewName);
bool wlCostumeRemoveByName(const char* pcCostume);
bool wlCostumeGenerateBoneScaleTable(const WLCostume* pCostume, StashTable* stBoneScaleTable);

bool wlCostumeDoesAnyOverrideValue(ShaderTemplate *shader_template, const char *op_name);
void wlCostumeApplyOverridePart(WLCostume* pCostume, WLCostumePart* pPart);

const char* wlCostumeGetThrowableGeometryHack(const WLCostume* pCostume);
U8* wlCostumeGetBodySockColorMap(WLCostume* pCostume, F32* pfRow);

void wlCostumePushSubCostume( WLCostume* pSubCostume, WLCostume* pCostume );
void wlCostumeAddRiderToMount(WLCostume *pRiderCostume, WLCostume *pMountCostume);

U32 wlCostumeNumCostumes(void);