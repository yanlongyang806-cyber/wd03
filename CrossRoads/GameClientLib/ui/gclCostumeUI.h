#pragma once
GCC_SYSTEM

#include "CostumeCommon.h"
#include "AutoGen/CostumeCommonEnums_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"

typedef struct Entity Entity;
typedef struct ExprContext ExprContext;
typedef struct CBox CBox;
typedef struct CostumeUIScaleGroup CostumeUIScaleGroup;
typedef struct CostumeViewGraphics CostumeViewGraphics;
typedef struct Guild Guild;
typedef struct PCSlotType PCSlotType;
typedef struct UIGen UIGen;
typedef struct ItemDefRef ItemDefRef;
typedef struct CharacterClass CharacterClass;
typedef struct PlayerCostumeSlot PlayerCostumeSlot;
typedef struct CostumePreset CostumePreset;

NOCONST(PlayerCostume)* CostumeUI_GetCostume(void);
SpeciesDef* CostumeUI_GetSpecies(void);
PCSlotType *CostumeUI_GetSlotType(void);
void CostumeUI_SetCostumeEx( NOCONST(PlayerCostume)* pCostume, CharacterClass* pClass, bool bRegen, bool bNoModify );
#define CostumeUI_SetCostume(pCostume)			CostumeUI_SetCostumeEx(pCostume, NULL, false, false)
#define CostumeUI_SetCostumeAndRegen(pCostume)	CostumeUI_SetCostumeEx(pCostume, NULL, true, false)
PCMood* CostumeUI_GetMood(void);
void CostumeUI_ClearCostume(void);
PCSkeletonDef *CharacterCreation_GetPlainSkeleton(void);
void CostumeUI_BuildPlainCostumes(void);
void CharacterCreation_SetCostumePtr(PlayerCostume* pCostume);
void CharacterCreation_CopyCostume(PCCostumeStorageType eCostumeType, int iPetNum, int iCostumeIndex);
bool CharacterCreation_CopyUGCCostume(NOCONST(PlayerCostume) *pPlayerCostume);
void CostumeUI_UpdateLists(NOCONST(PlayerCostume) *pCostume, bool bUGC, bool bValidateSafeMode);
void CostumeCreator_SetSkeletonPtr(PCSkeletonDef *pSkel);
S32 CostumeCreator_GetCost(void);
bool CostumeCreator_SetHoverGeo(const char *pchGeo);
void CostumeCreator_ResetBoneFilters(void);
void CostumeCreator_ClearRegionList(void);
void CostumeCreator_ClearScaleGroupList(void);
void CostumeCreator_AddRegion(const char *pchName);
void CostumeCreator_AddScaleGroup(const char *pchName);
void CostumeCreator_SaveCostume(PCCostumeStorageType eCostumeType, int iPetNum, int iCostumeIndex, PCPaymentMethod ePayMethod);
void CostumeCreator_SaveCostumeDefault(PCPaymentMethod ePayMethod);
int CostumeCreator_GetSpeciesBodyScaleIndex(const char *pchName);
void CostumeUI_FilterBoneList(PCBoneDef ***peaBones, const char **eaIncludeBones, const char **eaExcludeBones);
bool CostumeCreator_CommonSetBoneScale(NOCONST(PlayerCostume) *pCostume, F32 fMin, F32 fMax, const char *pcScaleName, F32 fBoneScale);
void CostumeUI_ValidateAllParts(NOCONST(PlayerCostume) *pCostume, bool bUGC, bool bSafeMode);
bool CostumeCreator_SetBone(const char *pchBone);
bool CostumeCreator_SetMaterial(const char *pchMat);
bool CostumeCreator_SetPattern(const char *pchPattern);
bool CostumeCreator_SetDetail(const char *pchDetail);
bool CostumeCreator_SetSpecular(const char *pchSpecular);
bool CostumeCreator_SetDiffuse(const char *pchDiffuse);
bool CostumeCreator_SetMovable(const char *pchMovable);
void CostumeCreator_SetBodyScaleByName(const char *pchName, F32 fBodyScale);
bool CostumeCreator_SetHoverCategory(const char *pchCat);
bool CostumeCreator_SetHoverMaterial(const char *pchMat);
bool CostumeCreator_SetHoverPattern(const char *pchPattern);
bool CostumeCreator_SetHoverDetail(const char *pchDetail);
bool CostumeCreator_SetHoverSpecular(const char *pchSpecular);
bool CostumeCreator_SetHoverDiffuse(const char *pchDiffuse);
bool CostumeCreator_SetHoverMovable(const char *pchMovable);
void CostumeCreator_SetHoverBodyScaleByName(const char *pchName, F32 fBodyScale);
void CostumeCreator_SetHoverBodyScale(int index, F32 fBodyScale);
S32 CostumeCreator_GetEditColorNumber(int iColor);
void CostumeCreator_GetColorModel(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, int iColor);
const char *CostumeCreator_GetColorModelName(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, int iColor);
SA_RET_NN_VALID UIColor *CostumeCreator_GetEditColor(int iColor);
int CostumeCreator_GetEditColorValue(int iColor);
PCBoneDef *CostumeUI_FindBone(const char *pcName, PCSkeletonDef *pSkel);
bool CostumeCreator_SetHoverColor(S32 iColor, F32 fR, F32 fG, F32 fB, F32 fA);
void CostumeCreator_SetColor(S32 iColor, F32 fR, F32 fG, F32 fB, F32 fA);
bool CostumeUI_IsCreatorActive(void);
PlayerCostume *CostumeCreator_GetCostume(void);
PCMood *CostumeCreator_GetMood(void);
void CostumeUI_AddInventoryItems(ItemDefRef **eaShowItems);
void CostumeCreation_GetBoneScreenLocation(const char* pchBoneName, F32* xOut, F32* yOut);
void Costume_OncePerFrame(void);
void CostumeUI_UpdateWorldRegion(bool bForceOff);
void CostumeCreator_FilterBodyScaleList(SA_PARAM_OP_VALID UIGen *pGen, const char *pchIncludeList, const char *pchExcludeList);
void CostumeCreator_ApplyPresetOverlay(NOCONST(PlayerCostume) *pDst, CostumePreset *pPreset, bool bForceIgnoreHeight);

void CostumeUI_ResetCostumeSkinColor(NOCONST(PlayerCostume) *pTarget, PlayerCostume *pCostume);
void CostumeUI_ResetCostumeHeight(NOCONST(PlayerCostume) *pTarget, PlayerCostume *pCostume);
void CostumeUI_ResetCostumeMuscle(NOCONST(PlayerCostume) *pTarget, PlayerCostume *pCostume);
void CostumeUI_ResetCostumeStance(NOCONST(PlayerCostume) *pTarget, PlayerCostume *pCostume);
void CostumeUI_ResetCostumeBodyScales(const char* pchBodyScaleName, NOCONST(PlayerCostume) *pTarget, PlayerCostume *pCostume);
void CostumeUI_ResetCostumeBoneScales(const char* pchBoneScaleName, NOCONST(PlayerCostume) *pTarget, PlayerCostume *pCostume);
void CostumeUI_ResetCostumePart(const char* pchBoneName, NOCONST(PlayerCostume) *pTarget, PlayerCostume *pCostume);

Entity *CostumeCreator_GetStoreCostumeEntityFromContainer(PCCostumeStorageType eStorageType, U32 uContainerID, S32 iIndex);
bool CostumeCreator_GetStoreCostumeSlotFromContainer(PCCostumeStorageType eStorageType, U32 uContainerID, S32 iIndex, PCSlotDef **ppSlotDef, PlayerCostumeSlot **ppCostumeSlot);
Entity *CostumeCreator_GetStoreCostumeEntityFromPet(PCCostumeStorageType eStorageType, int iPetNum, S32 iIndex);
bool CostumeCreator_GetStoreCostumeSlotFromPet(PCCostumeStorageType eStorageType, int iPetNum, S32 iIndex, PCSlotDef **ppSlotDef, PlayerCostumeSlot **ppCostumeSlot);
Entity *CostumeCreator_GetEditPlayerEntity(void);
Entity *CostumeCreator_GetEditEntity(void);
#define CostumeUI_GetSourceEnt CostumeCreator_GetEditEntity

// Costume Editor <---> Server: Costume transferring
bool CostumeCreator_SetStartCostume(NOCONST(PlayerCostume) *pCostume);
void CostumeCreator_CopyCostumeFromEnt(PCCostumeStorageType eStorageType, Entity *pEnt, S32 iIndex);
void CostumeCreator_CopyCostumeFromContainer(PCCostumeStorageType eStorageType, ContainerID uContainerID, S32 iIndex);
void CostumeCreator_CopyCostumeFromPet(PCCostumeStorageType eStorageType, int iPetNum, S32 iIndex);
void CostumeCreator_StoreCostumeToEnt(PCCostumeStorageType eStorageType, Entity *pEnt, S32 iIndex, PCPaymentMethod ePayMethod);
void CostumeCreator_StoreCostumeToContainer(PCCostumeStorageType eStorageType, ContainerID uContainerID, S32 iIndex, PCPaymentMethod ePayMethod);
void CostumeCreator_StoreCostumeToPet(PCCostumeStorageType eStorageType, int iPetNum, S32 iIndex, PCPaymentMethod ePayMethod);
void CostumeCreator_RenameCostumeEnt(PCCostumeStorageType eStorageType, Entity *pEnt, S32 iIndex, PCPaymentMethod ePayMethod, const char *pchName);
void CostumeCreator_RenameCostumeContainer(PCCostumeStorageType eStorageType, ContainerID uContainerID, S32 iIndex, PCPaymentMethod ePayMethod, const char *pchName);
void CostumeCreator_RenameCostumePet(PCCostumeStorageType eStorageType, int iPetNum, S32 iIndex, PCPaymentMethod ePayMethod, const char *pchName);

// Fixup costume after a CostumeCreator_CopyCostumeFrom*() which copies it in such
// a way that prevents the costume from being invalidated.
//
// CostumeCreator_StartEditCostume() should be called before any functions operate
// on a costume to edit it.
void CostumeCreator_StartEditCostume(void);
bool CostumeCreator_BeginCostumeEditing(NOCONST(PlayerCostume) *pCostume);
