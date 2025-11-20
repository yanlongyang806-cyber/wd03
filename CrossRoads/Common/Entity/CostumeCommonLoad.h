/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#pragma once
GCC_SYSTEM

#include "CostumeCommon.h"
#include "CharacterClass.h"


#define COSTUME_MAX_COLOR_DIF (0.99)


// ---- Data Structures ----

typedef struct ResourceCache ResourceCache;
typedef struct SpeciesDef SpeciesDef;
typedef struct Guild Guild;
typedef struct NOCONST(PlayerCostume) NOCONST(PlayerCostume);
typedef struct NOCONST(PlayerCostumeV0) NOCONST(PlayerCostumeV0);


AUTO_STRUCT;
typedef struct CostumePrices
{
	PCCostumeStorageType eStorageType;

	U32 iBase;
	U32 iGeometry;
	U32 iMaterial;
	U32 iPattern;
	U32 iDetail;
	U32 iSpecular;
	U32 iDiffuse;
	U32 iMovable;
	U32 iColor0;
	U32 iColor1;
	U32 iColor2;
	U32 iColor3;
	U32 iSkinColor;

	U32 iBodyScale;
	U32 iBoneScale;

	F32 fLevelMultipliers[MAX_LEVELS];
} CostumePrices;

AUTO_STRUCT;
typedef struct CostumeGenderPrefix
{
	Gender eGender;

	const char *pcBonePrefix;

}CostumeGenderPrefix;

AUTO_STRUCT;
typedef struct CostumeConfig
{
	U32 iChangeCooldown;
	CostumePrices **eaPrices;			AST(NAME("Prices"))
	
	F32 fMountRiderScaleBlend;			AST(DEFAULT(-1.f))
		// Used to determine the mounts global scale at runtime based on both costumes, 1.0 = 100% mount, 0.0 = 100% rider
		// this can be overridden by the similarly named value set in PCSkelDef

	U32 bDisablePlayerActiveChange : 1;
		// Disables the ability for Players to change their active costume slot
	
	U32 bInvalidCostumesAreFreeToChange : 1;
		// costumes that are invalid on the player cost 0 to change

	U32 bEnableItemCategoryAddedBones : 1;
		// process primary/secondary added bones on itemcategories for equipped item costumes

	CONST_EARRAY_OF(CostumeGenderPrefix) eaCostumeGenderPrefixes;
		// the earray of costume prefixes by gender

	U32	bfParamsSpecified[1];				AST( USEDFIELD )
} CostumeConfig;
extern ParseTable parse_CostumeConfig[];
#define TYPE_parse_CostumeConfig CostumeConfig

extern CostumeConfig g_CostumeConfig;


// ---- Dictionary Definitions ----

extern DictionaryHandle g_hCostumeColorsDict;
extern DictionaryHandle g_hCostumeColorQuadsDict;
extern DictionaryHandle g_hCostumeVoiceDict;
extern DictionaryHandle g_hCostumeMoodDict;
extern DictionaryHandle g_hCostumeRegionDict;
extern DictionaryHandle g_hCostumeCategoryDict;
extern DictionaryHandle g_hCostumeStyleDict;
extern DictionaryHandle g_hCostumeLayerDict;
extern DictionaryHandle g_hCostumeTextureDict;
extern DictionaryHandle g_hCostumeMaterialDict;
extern DictionaryHandle g_hCostumeMaterialAddDict;
extern DictionaryHandle g_hCostumeGeometryDict;
extern DictionaryHandle g_hCostumeGeometryAddDict;
extern DictionaryHandle g_hCostumeBoneDict;
extern DictionaryHandle g_hCostumeSkeletonDict;
extern DictionaryHandle g_hCostumeSetsDict;
extern DictionaryHandle g_hPlayerCostumeDict;
extern DictionaryHandle g_hCostumeGroupsDict;

// ---- Functions ----

// Load and validate
void costumeLoad_LoadData(void);
void costumeLoad_LoadPlayerCostumesForUGC(void);

void costumeLoad_ValidateAll(void);
bool costumeLoad_ValidateCostumeForApply(PlayerCostume *pPCCostume, const char *pcFilename);
bool costumeLoad_ValidatePCPart(PlayerCostume *pCostume, PCPart *pPart, PCPart *pParentPart, PCSkeletonDef *pSkeleton, PCPartType ePartType, bool bInEditor, Guild *pGuild, bool bLoading);

// Validate a player costume
bool costumeLoad_ValidatePlayerCostume(PlayerCostume *pPCCostume, SpeciesDef *pSpecies, bool bInEditor, bool bLoading, bool bCheckFX);

// check this color against these color sets
bool costumeLoad_ColorInSet(const U8 color[4], UIColorSet *pColorSet);
bool costumeLoad_ColorInSets(const U8 color[4], UIColorSet *set1, UIColorSet *set2);

// Access functions
PCSlotSet *costumeLoad_GetSlotSet(const char *pcName);
PCSlotType *costumeLoad_GetSlotType(const char *pcName);

// V0 to V5 Conversion function
NOCONST(PlayerCostume) *costumeLoad_UpgradeCostumeV0toV5(NOCONST(PlayerCostumeV0) *pCostumeV0);
