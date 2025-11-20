#pragma once
GCC_SYSTEM

#include "ReferenceSystem.h"
#include "CostumeCommon.h"

#include "Entity.h"
#include "EntitySavedData.h"

#include "CostumeCommon_h_ast.h"

extern ParseTable parse_UnlockMetaData[];
#define TYPE_parse_UnlockMetaData UnlockMetaData
extern ParseTable parse_UnlockedCostumePart[];
#define TYPE_parse_UnlockedCostumePart UnlockedCostumePart

typedef struct MicroTransactionUIProduct MicroTransactionUIProduct;

AUTO_STRUCT;
typedef struct UnlockMetaData {
	// The unlock this metadata refers to
	REF_TO(PCGeometryDef) hGeometry;				AST(NAME(Geometry) REFDICT(CostumeGeometry))
	REF_TO(PCMaterialDef) hMaterial;				AST(NAME(Material) REFDICT(CostumeMaterial))
	REF_TO(PCTextureDef) hTexture;					AST(NAME(Texture) REFDICT(CostumeTexture))

	// The costumes that grant the unlock
	PlayerCostumeRef **eaCostumes;

	// True if the player owns any of the costumes that unlock this
	bool bOwned;

	// The sources of the costume of the unlock
	U32 uMicroTransactionID;						AST(NAME(MicroTransactionID))

	// The prebuilt UI product information structure for the default MicroTransactionID
	MicroTransactionUIProduct *pProduct;

	// The full list of products that grants the unlock
	MicroTransactionUIProduct **eaFullProductList;
	S64 iMinimumProductPrice;						AST(NAME(MinimumProductPrice))
	S64 iMaximumProductPrice;						AST(NAME(MaximumProductPrice))
} UnlockMetaData;

AUTO_STRUCT;
typedef struct UnlockedCostumePart {
	REF_TO(PCSkeletonDef) hSkeleton;				AST(NAME(Skeleton) REFDICT(CostumeSkeleton))
	REF_TO(PCRegion) hRegion;						AST(NAME(Region) REFDICT(CostumeRegion))
	REF_TO(PCBoneDef) hBone;						AST(NAME(Bone) REFDICT(CostumeBone))

	bool bUsable : 1;
	bool bBoth : 1;
	bool bGeoUnlock : 1;
	bool bMatUnlock : 1;
	bool bTexPatternUnlock : 1;
	bool bTexDetailUnlock : 1;
	bool bTexSpecularUnlock : 1;
	bool bTexDiffuseUnlock : 1;
	bool bTexMovableUnlock : 1;

	REF_TO(PCGeometryDef) hUnlockedGeometry;		AST(NAME(UnlockedGeometry) REFDICT(CostumeGeometry))
	REF_TO(PCMaterialDef) hUnlockedMaterial;		AST(NAME(UnlockedMaterial) REFDICT(CostumeMaterial))
	REF_TO(PCTextureDef) hUnlockedPatternTexture;	AST(NAME(UnlockedPatternTexture) REFDICT(CostumeTexture))
	REF_TO(PCTextureDef) hUnlockedDetailTexture;	AST(NAME(UnlockedDetailTexture) REFDICT(CostumeTexture))
	REF_TO(PCTextureDef) hUnlockedSpecularTexture;	AST(NAME(UnlockedSpecularTexture) REFDICT(CostumeTexture))
	REF_TO(PCTextureDef) hUnlockedDiffuseTexture;	AST(NAME(UnlockedDiffuseTexture) REFDICT(CostumeTexture))
	REF_TO(PCTextureDef) hUnlockedMovableTexture;	AST(NAME(UnlockedMovableTexture) REFDICT(CostumeTexture))

	char *pchName;
	char *estrLocation;								AST(NAME(Location) ESTRING)

	UnlockMetaData *pUnlockData;					AST(UNOWNED)
	const char *pchUnlockOrder;						AST(UNOWNED)
	REF_TO(PlayerCostume) hSoftCostume;				AST(NAME(CostumeUnlock) REFDICT(PlayerCostume))

	PCCategoryRef **eaIncludedCategories;
	PCCategoryRef **eaUnavailableCategories;
	PCCategoryRef **eaExcludedCategories;
} UnlockedCostumePart;

void CostumeUI_SetUnlockedCostumes(bool bRefreshRefs, bool bRefreshList, Entity *pEnt, Entity *pSubEnt);
void CostumeUI_UpdateUnlockedCostumeParts(void);
void CostumeUI_ClearUnlockMetaData(void);
void CostumeUI_GetValidCostumeUnlocks(NOCONST(PlayerCostume) *pValidateCostume, NOCONST(PlayerCostume) *pValidateCostumeUnlockAll, NOCONST(PlayerCostume) *pValidateCostumeUnlockCMat, NOCONST(PlayerCostume) *pValidateCostumeUnlockCTex, NOCONST(PlayerCostume) *pCostume);

S32 CostumeUI_AddUnlockedCostumeParts(UnlockedCostumePart ***peaUnlocked, S32 iUsed, PlayerCostume *pPCCostume, PCCategory **eaAllValidCategories, PCBoneDef **eaValidBones, const char **eaFilter, PCBoneDef **eaSkeletonBones, bool bSoftCostumeRef);
bool CostumeCreator_ApplyCostumeUnlockToCostume(SA_PARAM_OP_VALID NOCONST(PlayerCostume) *pCostume, SA_PARAM_NN_VALID UnlockedCostumePart *pUnlock, PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCSlotType *pSlotType, bool bUnlockAll);

