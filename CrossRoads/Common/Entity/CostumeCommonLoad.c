/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/
#include "Capsule.h"
#include "CostumeCommon.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "EntityIterator.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "StringCache.h"
#include "UtilitiesLib.h"
#include "wlEditorIncludes.h"
#include "wlUGC.h"
#include "species_common.h"
#include "expression.h"
#include "Guild.h"
#include "dynFxInfo.h"

#ifdef GAMECLIENT
#include "gclDemo.h"
#include "GameClientLib.h"
#endif

#include "AutoGen/Capsule_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"
#include "AutoGen/CostumeCommonEnums_h_ast.h"
#include "AutoGen/CostumeCommonLoad_h_ast.h"

#define TESTDELTA 0.00001f

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("CostumeCommon.h", BUDGET_GameSystems););

// --------------------------------------------------------------------------
// Static Data
// --------------------------------------------------------------------------

DictionaryHandle g_hCostumeColorsDict = NULL;
DictionaryHandle g_hCostumeColorQuadsDict = NULL;
DictionaryHandle g_hCostumeMoodDict = NULL;
DictionaryHandle g_hCostumeVoiceDict = NULL;
DictionaryHandle g_hCostumeRegionDict = NULL;
DictionaryHandle g_hCostumeCategoryDict = NULL;
DictionaryHandle g_hCostumeStyleDict = NULL;
DictionaryHandle g_hCostumeLayerDict = NULL;
DictionaryHandle g_hCostumeTextureDict = NULL;
DictionaryHandle g_hCostumeMaterialDict = NULL;
DictionaryHandle g_hCostumeMaterialAddDict = NULL;
DictionaryHandle g_hCostumeGeometryDict = NULL;
DictionaryHandle g_hCostumeGeometryAddDict = NULL;
DictionaryHandle g_hCostumeBoneDict = NULL;
DictionaryHandle g_hCostumeSkeletonDict = NULL;
DictionaryHandle g_hCostumeSetsDict = NULL;
DictionaryHandle g_hPlayerCostumeDict = NULL;
DictionaryHandle g_hCostumeGroupsDict = NULL;

extern DictionaryHandle g_hItemDict;
extern ExprContext *s_pCostumeContext;

static bool gCostumeValidate = false;
static bool gPreloadPartsOnClient = true;
static bool gReloadCostumeParts = false;
static bool gStripNonPlayerOnClient = true;
static bool gIsLoading = false;

static const char **g_eaTexFileNames = NULL;

PCSlotSets g_CostumeSlotSets;
PCSlotTypes g_CostumeSlotTypes;

CostumeConfig g_CostumeConfig;

MP_DEFINE(PCBoneRef);
MP_DEFINE(PCCategoryRef);
extern MP_DEFINE_MEMBER(UIColor);
MP_DEFINE(PCExtraTexture);
MP_DEFINE(PCRegionRef);
MP_DEFINE(PCScaleEntry);
MP_DEFINE(PCScaleInfo);
MP_DEFINE(PCScaleInfoGroup);

MP_DEFINE(PCTextureDef);
MP_DEFINE(PCGeometryDef);
MP_DEFINE(PCMaterialDef);

// --------------------------------------------------------------------------
// Type definitions and prototypes
// --------------------------------------------------------------------------

#define COSTUMES_BASE_DIR "defs/costumes"
#define COSTUME_DEFS_BASE_DIR "defs/costumes/definitions"
#define COSTUMES_EXTENSION "costume"
#define GEO_EXTENSION "cgeo"
#define MAT_EXTENSION "cmat"
#define TEX_EXTENSION "ctex"

void generateSkelFixupInfos(void);

#define NOT_UGC_RESTRICTIONS	(~(kPCRestriction_UGC | kPCRestriction_UGC_Initial))

#ifdef GAMECLIENT
void CosutmeUI_GameAccountDictChanged(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData);
#endif


// --------------------------------------------------------------------------
// File Scans
// --------------------------------------------------------------------------


static FileScanAction costumeLoad_TexFileScanAction(char *dir, struct _finddata32_t *data, void *pUserData)
{
	static char *ext = ".wtex";
	static int ext_len = 5; // strlen(ext);
	char filename[MAX_PATH];
	int len;

	if (data->name[0]=='_') return FSA_NO_EXPLORE_DIRECTORY;
	if (data->attrib & _A_SUBDIR) {
		return FSA_EXPLORE_DIRECTORY;
	}
	len = (int)strlen(data->name);
	if (len<ext_len || strnicmp(data->name + len - ext_len, ext, ext_len)!=0) {
		return FSA_EXPLORE_DIRECTORY;
	}

	//STR_COMBINE_SSS(filename, dir, "/", data->name);
	strcpy(filename, data->name);

	// Clip off the "wtex" ending
	filename[strlen(filename)-5] = '\0';

	// Store the geometry file name
	eaPush(&g_eaTexFileNames,allocAddFilename(filename));

	return FSA_EXPLORE_DIRECTORY;
}


static void costumeLoad_TexFileListCallback(const char *relpath, int when)
{
	eaClear(&g_eaTexFileNames);
	fileScanAllDataDirs("texture_library/costumes", costumeLoad_TexFileScanAction, NULL);
	fileScanAllDataDirs("texture_library/core_costumes", costumeLoad_TexFileScanAction, NULL);
	fileScanAllDataDirs("texture_library/character_library", costumeLoad_TexFileScanAction, NULL);
}


static void costumeLoad_TexFileListLoad(void)
{
	costumeLoad_TexFileListCallback(NULL, 0);

	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "texture_library/costumes/*.wtex", costumeLoad_TexFileListCallback);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "texture_library/character_library/*.wtex", costumeLoad_TexFileListCallback);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "texture_library/core_costumes/*.wtex", costumeLoad_TexFileListCallback);
}


// --------------------------------------------------------------------------
// Permanent Conversion Logic
// --------------------------------------------------------------------------

NOCONST(PCPart) *costumeLoad_UpgradePartV0toV5(NOCONST(PCPartV0) *pPartV0)
{
	NOCONST(PCPart) *pPart;
	Vec4 v4Zero = { 0,0,0,0 };
	U8 zeroColor[4] = { 0,0,0,0 };
	int i;

	pPart = StructCreateNoConst(parse_PCPart);
	assert(pPart);

	// Copy over basic stuff
	COPY_HANDLE(pPart->hBoneDef, pPartV0->hBoneDef);
	COPY_HANDLE(pPart->hGeoDef, pPartV0->hGeoDef);
	COPY_HANDLE(pPart->hMatDef, pPartV0->hMatDef);
	COPY_HANDLE(pPart->hPatternTexture, pPartV0->hPatternTexture);
	COPY_HANDLE(pPart->hDetailTexture, pPartV0->hDetailTexture);
	COPY_HANDLE(pPart->hSpecularTexture, pPartV0->hSpecularTexture);
	COPY_HANDLE(pPart->hDiffuseTexture, pPartV0->hDiffuseTexture);

	pPart->eColorLink = pPartV0->eColorLink;
	pPart->eMaterialLink = pPartV0->eMaterialLink;
	pPart->eControlledRandomLocks = pPartV0->eControlledRandomLocks;
	pPart->iBoneGroupIndex = pPartV0->iBoneGroupIndex;

	// Convert Colors
	VEC4_TO_COSTUME_COLOR(pPartV0->color0, pPart->color0);
	VEC4_TO_COSTUME_COLOR(pPartV0->color1, pPart->color1);
	VEC4_TO_COSTUME_COLOR(pPartV0->color2, pPart->color2);
	VEC4_TO_COSTUME_COLOR(pPartV0->color3, pPart->color3);

	// Ignore geolink

	// Convert Custom Colors
	if (pPartV0->glowScale[0] || pPartV0->glowScale[1] || pPartV0->glowScale[2] || pPartV0->glowScale[3] ||
		pPartV0->bCustomReflection ||
		pPartV0->bCustomSpecularity
		) {
		pPart->pCustomColors = StructCreateNoConst(parse_PCCustomColorInfo);
		
		COPY_COSTUME_COLOR(pPartV0->glowScale, pPart->pCustomColors->glowScale);

		if (pPartV0->bCustomReflection) {
			pPart->pCustomColors->bCustomReflection = true;
			COPY_COSTUME_COLOR(pPartV0->reflection, pPart->pCustomColors->reflection);
		}

		if (pPartV0->bCustomSpecularity) {
			pPart->pCustomColors->bCustomSpecularity = true;
			COPY_COSTUME_COLOR(pPartV0->specularity, pPart->pCustomColors->specularity);
		}
	}

	// Convert Texture Values
	if (pPartV0->fPatternValue ||
		pPartV0->fDetailValue ||
		pPartV0->fDiffuseValue ||
		pPartV0->fSpecularValue
		) {
		pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);

		pPart->pTextureValues->fPatternValue = pPartV0->fPatternValue;
		pPart->pTextureValues->fDetailValue = pPartV0->fDetailValue;
		pPart->pTextureValues->fDiffuseValue = pPartV0->fDiffuseValue;
		pPart->pTextureValues->fSpecularValue = pPartV0->fSpecularValue;
	}

	// Convert Movable Texture Data
	if (REF_STRING_FROM_HANDLE(pPartV0->hMovableTexture)) {
		pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);

		COPY_HANDLE(pPart->pMovableTexture->hMovableTexture, pPartV0->hMovableTexture);

		pPart->pMovableTexture->fMovableValue = pPartV0->fMovableValue;
		pPart->pMovableTexture->fMovableX = pPartV0->fMovableX;
		pPart->pMovableTexture->fMovableY = pPartV0->fMovableY;
		pPart->pMovableTexture->fMovableScaleX = pPartV0->fMovableScaleX;
		pPart->pMovableTexture->fMovableScaleY = pPartV0->fMovableScaleY;
		pPart->pMovableTexture->fMovableRotation = pPartV0->fMovableRotation;
	}

	// Convert Artist Data
	if (pPartV0->bNoShadow ||
		pPartV0->eaExtraColors ||
		pPartV0->eaExtraConstants ||
		pPartV0->eaExtraTextures
		) {
		pPart->pArtistData = StructCreateNoConst(parse_PCArtistPartData);

		pPart->pArtistData->bNoShadow = pPartV0->bNoShadow;

		for(i=0; i<eaSize(&pPartV0->eaExtraColors); ++i) {
			NOCONST(PCMaterialColor) *pMatColor = StructCreateNoConst(parse_PCMaterialColor);
			NOCONST(PCMaterialColorV0) *pMatColorV0 = pPartV0->eaExtraColors[i];
			pMatColor->pcName = pMatColorV0->pcName; // Pooled String
			VEC4_TO_COSTUME_COLOR(pMatColorV0->color, pMatColor->color);
			eaPush(&pPart->pArtistData->eaExtraColors, pMatColor);
		}

		for(i=0; i<eaSize(&pPartV0->eaExtraConstants); ++i) {
			eaPush(&pPart->pArtistData->eaExtraConstants, StructCloneNoConst(parse_PCMaterialConstant, pPartV0->eaExtraConstants[i]));
		}

		for(i=0; i<eaSize(&pPartV0->eaExtraTextures); ++i) {
			eaPush(&pPart->pArtistData->eaExtraTextures, StructCloneNoConst(parse_PCTextureRef, pPartV0->eaExtraTextures[i]));
		}
	}

	// Convert Layer used for cloth
	if (pPartV0->pClothLayer) {
		pPart->pClothLayer = costumeLoad_UpgradePartV0toV5(pPartV0->pClothLayer);
	}

	return pPart;
}


NOCONST(PlayerCostume) *costumeLoad_UpgradeCostumeV0toV5(NOCONST(PlayerCostumeV0) *pCostumeV0)
{
	NOCONST(PlayerCostume) *pCostume;
	Vec4 v4Zero = { 0,0,0,0 };
	int i;

	if (!pCostumeV0) {
		return NULL;
	}

	pCostume = StructCreateNoConst(parse_PlayerCostume);
	assert(pCostume);

	// Copy over basic stuff
	pCostume->pcName = pCostumeV0->pcName;
	pCostume->pcScope = pCostumeV0->pcScope;
	COPY_HANDLE(pCostume->hSkeleton, pCostumeV0->hSkeleton);
	COPY_HANDLE(pCostume->hSpecies, pCostumeV0->hSpecies);

	// Copy over settings
	pCostume->eCostumeType = pCostumeV0->eCostumeType;
	pCostume->eGender = pCostumeV0->eGender;
	pCostume->eDefaultColorLinkAll = pCostumeV0->eDefaultColorLinkAll;
	pCostume->eDefaultMaterialLinkAll = pCostumeV0->eDefaultMaterialLinkAll;

	// Copy over scales
	for(i=0; i<eafSize(&pCostumeV0->eafBodyScales); ++i) {
		eafPush(&pCostume->eafBodyScales, pCostumeV0->eafBodyScales[i]);
	}
	for(i=0; i<eaSize(&pCostumeV0->eaScaleValues); ++i) {
		eaPush(&pCostume->eaScaleValues, StructCloneNoConst(parse_PCScaleValue, pCostumeV0->eaScaleValues[i]));
	}
	pCostume->fMuscle = pCostumeV0->fMuscle;
	pCostume->fHeight = pCostumeV0->fHeight;

	// Copy over other
	pCostume->pcStance = pCostumeV0->pcStance;
	pCostume->bAccountWideUnlock = pCostumeV0->bAccountWideUnlock;
	for(i=0; i<eaSize(&pCostumeV0->eaTexWords); ++i) {
		eaPush(&pCostume->eaTexWords, StructCloneNoConst(parse_PCTexWords, pCostumeV0->eaTexWords[i]));
	}
	for(i=0; i<eaSize(&pCostumeV0->eaRegionCategories); ++i) {
		eaPush(&pCostume->eaRegionCategories, StructCloneNoConst(parse_PCRegionCategory, pCostumeV0->eaRegionCategories[i]));
	}

	// Convert skin color format
	VEC4_TO_COSTUME_COLOR(pCostumeV0->skinColor, pCostume->skinColor);
	
	// Convert artist data
	if (pCostumeV0->fTransparency || 
		pCostumeV0->bNoCollision ||
		pCostumeV0->bNoBodySock ||
		pCostumeV0->bNoRagdoll ||
		pCostumeV0->bShellColl ||
		pCostumeV0->fCollRadius ||
		pCostumeV0->pcCollisionGeo ||
		pCostumeV0->eaFX ||
		pCostumeV0->eaFXSwap ||
		pCostumeV0->eaConstantBits ||
		pCostumeV0->ppCollCapsules
		) {
		pCostume->pArtistData = StructCreateNoConst(parse_PCArtistCostumeData);

		pCostume->pArtistData->fTransparency = pCostumeV0->fTransparency;
		pCostume->pArtistData->bNoCollision = pCostumeV0->bNoCollision;
		pCostume->pArtistData->bNoBodySock = pCostumeV0->bNoBodySock;
		pCostume->pArtistData->bNoRagdoll = pCostumeV0->bNoRagdoll;
		pCostume->pArtistData->bShellColl = pCostumeV0->bShellColl;
		pCostume->pArtistData->fCollRadius = pCostumeV0->fCollRadius;
		pCostume->pArtistData->pcCollisionGeo = pCostumeV0->pcCollisionGeo; // Pooled string

		for(i=0; i<eaSize(&pCostumeV0->eaFX); ++i) {
			eaPush(&pCostume->pArtistData->eaFX, StructCloneNoConst(parse_PCFX, pCostumeV0->eaFX[i]));
		}

		for(i=0; i<eaSize(&pCostumeV0->eaFXSwap); ++i) {
			eaPush(&pCostume->pArtistData->eaFXSwap, StructCloneNoConst(parse_PCFXSwap, pCostumeV0->eaFXSwap[i]));
		}

		for(i=0; i<eaSize(&pCostumeV0->eaConstantBits); ++i) {
			eaPush(&pCostume->pArtistData->eaConstantBits, StructCloneNoConst(parse_PCBitName, pCostumeV0->eaConstantBits[i]));
		}

		for(i=0; i<eaSize(&pCostumeV0->ppCollCapsules); ++i) {
			eaPush(&pCostume->pArtistData->ppCollCapsules, StructCloneNoConst(parse_SavedCapsule, pCostumeV0->ppCollCapsules[i]));
		}
	}

	// Convert the parts
	for(i=0; i<eaSize(&pCostumeV0->eaParts); ++i) {
		NOCONST(PCPart) *pPart = costumeLoad_UpgradePartV0toV5(pCostumeV0->eaParts[i]);
		eaPush(&pCostume->eaParts, pPart);
	}

	return pCostume;
}


// --------------------------------------------------------------------------
// Validation Logic
// --------------------------------------------------------------------------


AUTO_TRANS_HELPER_SIMPLE;
bool costumeLoad_ColorInSet(const U8 color[4], UIColorSet *pColorSet)
{
	int i;

	if (!pColorSet) {
		return false;
	}

	for(i = 0; i < eaSize(&pColorSet->eaColors); ++i) {
		F32 colorDif0, colorDif1, colorDif2, colorDif3;
	
		colorDif0 = pColorSet->eaColors[i]->color[0] - color[0];
		colorDif1 = pColorSet->eaColors[i]->color[1] - color[1];
		colorDif2 = pColorSet->eaColors[i]->color[2] - color[2];
		colorDif3 = pColorSet->eaColors[i]->color[3] - color[3];
		if(colorDif0 >= -COSTUME_MAX_COLOR_DIF && colorDif0 <= COSTUME_MAX_COLOR_DIF &&
			colorDif1 >= -COSTUME_MAX_COLOR_DIF && colorDif1 <= COSTUME_MAX_COLOR_DIF &&
			colorDif2 >= -COSTUME_MAX_COLOR_DIF && colorDif2 <= COSTUME_MAX_COLOR_DIF &&
			colorDif3 >= -COSTUME_MAX_COLOR_DIF && colorDif3 <= COSTUME_MAX_COLOR_DIF
			) {
			return true;
		}
	}

	return false;
}


bool costumeLoad_ColorInSets(const U8 color[4], UIColorSet *set1, UIColorSet *set2)
{
	if(costumeLoad_ColorInSet(color, set1)) {
		return true;
	}

	if(costumeLoad_ColorInSet(color, set2)) {
		return false;
	}

	return false;
}


bool costumeLoad_ValidateCostumeForApply(PlayerCostume *pPCCostume, const char *pcFilename)
{
	bool bResult = true;
	int i,j;

	// This is called by item and power to ensure that an Item typed costume does not contain things
	// that would be okay if that item typed costume was only used for unlocks

	if (!pPCCostume || (pPCCostume->eCostumeType != kPCCostumeType_Item)) {
		return bResult;
	}

	for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
		PCBoneDef *pBone = GET_REF(pPCCostume->eaParts[i]->hBoneDef);

		if (pBone) {
			for(j=i-1; j>=0; --j) {
				PCBoneDef *pOtherBone = GET_REF(pPCCostume->eaParts[j]->hBoneDef);
				if (pBone && pOtherBone && (pBone == pOtherBone)) {
					ErrorFilenamef(pcFilename, "Attempting to use Item typed costume '%s' and the costume has more than one part on bone '%s'", pPCCostume->pcName, pBone->pcName);
				}
			}
		}
	}

	return bResult;
}


bool costumeLoad_ValidatePartTexture(PlayerCostume *pCostume, PCMaterialDef *pMaterial, PCTextureDef *pTexture, const char *pcTexName)
{
	bool bResult = true;
	bool bFound;
	int j,k;

	if (stricmp("None", pcTexName) == 0) {
		return true;
	}
	if (!pTexture) {
		if (pCostume->pcFileName) {
			ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-existent texture def '%s'\n",pCostume->pcName, pcTexName);
		}
		bResult = false;
	}
	if (pMaterial && pTexture) {
		bFound = false;
		for(j=eaSize(&pMaterial->eaAllowedTextureDefs)-1; j>=0; --j) {
			if (stricmp(pMaterial->eaAllowedTextureDefs[j], pTexture->pcName) == 0) {
				bFound = true;
				break;
			}
		}
		if (!bFound) {
			DictionaryEArrayStruct *pMatAddStruct = resDictGetEArrayStruct("CostumeMaterialAdd");
			for(j=eaSize(&pMatAddStruct->ppReferents)-1; j>=0; --j) {
				PCMaterialAdd *pMatAdd = (PCMaterialAdd*)pMatAddStruct->ppReferents[j];
				if (pMatAdd->pcMatName && (stricmp(pMatAdd->pcMatName, pMaterial->pcName) == 0)) {
					for(k=eaSize(&pMatAdd->eaAllowedTextureDefs)-1; k>=0; --k) {
						if (stricmp(pMatAdd->eaAllowedTextureDefs[k], pTexture->pcName) == 0) {
							bFound = true;
							break;
						}
					}
				}
				if (bFound) {
					break;
				}
			}
		}
		if (!bFound) {
			if (pCostume->pcFileName) {
				ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to texture def '%s' that is not allowed on material def '%s'\n",pCostume->pcName, pcTexName, pMaterial->pcName);
			}
			bResult = false;
		}
	}

	return bResult;
}


bool costumeLoad_ValidatePCPart(PlayerCostume *pCostume, PCPart *pPart, PCPart *pParentPart, PCSkeletonDef *pSkeleton, PCPartType ePartType, bool bInEditor, Guild *pGuild, bool bLoading)
{
	PCGeometryDef *pGeometry = NULL;
	PCMaterialDef *pMaterial = NULL;
	PCTextureDef *pTexture = NULL;
	PCBoneDef *pBone = NULL;
	bool bResult = true;
	bool bFound;
	int j,k;
	int eTexFlags = 0;
	bool bGuildTexture = costumeTailor_PartHasGuildEmblem(CONTAINER_NOCONST(PCPart, pPart), pGuild);

	if (!REF_STRING_FROM_HANDLE(pPart->hBoneDef)) {
		if (ePartType != kPCPartType_Cloth) {
			if (pCostume->pcFileName) {
				ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' missing the bone name on a part\n",pCostume->pcName);
			}
			bResult = false;
		}
	} else {
		pBone = GET_REF(pPart->hBoneDef);
		if (!pBone) {
			if (pCostume->pcFileName) {
				ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-existent bone def '%s'\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hBoneDef));
			}
			bResult = false;
		}
		if (pBone && !costumeTailor_TestRestriction(pBone->eRestriction, true, pCostume)) {
			// No longer issue an error on this
			//ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to bone def '%s' but it is restricted from using it\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hBoneDef));
		}
		if (pSkeleton && pBone) {
			// Ensure bone is legal for this skeleton
			bFound = false;
			for(j=eaSize(&pSkeleton->eaRequiredBoneDefs)-1; j>=0; --j) {
				if (GET_REF(pSkeleton->eaRequiredBoneDefs[j]->hBone) == pBone) {
					bFound = true;
					break;
				}
			}
			if (!bFound) {
				for(j=eaSize(&pSkeleton->eaOptionalBoneDefs)-1; j>=0; --j) {
					if (GET_REF(pSkeleton->eaOptionalBoneDefs[j]->hBone) == pBone) {
						bFound = true;
						break;
					}
				}
			}
			if (!bFound) {
				if (pCostume->pcFileName) {
					ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to bone def '%s' that does not exist on skeleton def '%s'\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hBoneDef), pSkeleton->pcName);
				}
				bResult = false;
			}
		}
	}
	if (!REF_STRING_FROM_HANDLE(pPart->hGeoDef)) {
		if (ePartType != kPCPartType_Cloth) {
			if (pCostume->pcFileName) {
				ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' missing the geometry name on a part\n",pCostume->pcName);
			}
			bResult = false;
		}
	} else {
		pGeometry = GET_REF(pPart->hGeoDef);
		if (!pGeometry) {
			if (pCostume->pcFileName) {
				ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-existent geometry def '%s'\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hGeoDef));
			}
			bResult = false;
		}
		if (pGeometry && !costumeTailor_TestRestriction(pGeometry->eRestriction, true, pCostume)) {
			// No longer issue an error on this
			//ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to geometry def '%s' but it is restricted from using it\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hGeoDef));
		}
		if (pBone && pGeometry && stricmp("None",REF_STRING_FROM_HANDLE(pPart->hGeoDef))) {
			if (GET_REF(pGeometry->hBone) != pBone) {
				if (pCostume->pcFileName) {
					ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' geometry def '%s' is not allowed on bone def '%s'\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hGeoDef), pBone->pcName);
				}
				bResult = false;
			}
		}
	}
	pMaterial = GET_REF(pPart->hMatDef);
	if (!pMaterial && REF_STRING_FROM_HANDLE(pPart->hMatDef)) {
		if (pCostume->pcFileName) {
			ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-existent material def '%s'\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hMatDef));
		}
		bResult = false;
	}
	if (pMaterial && (stricmp("None",pMaterial->pcName) != 0)) {
		if (pMaterial && !costumeTailor_TestRestriction(pMaterial->eRestriction, true, pCostume)) {
			// No longer issue an error for this
			//ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to material def '%s' but it is restricted from using it\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hMatDef));
		}
		if (pGeometry && pMaterial) {
			bFound = false;
			for(j=eaSize(&pGeometry->eaAllowedMaterialDefs)-1; j>=0; --j) {
				if (pGeometry->eaAllowedMaterialDefs[j] && stricmp(pGeometry->eaAllowedMaterialDefs[j], pMaterial->pcName) == 0) {
					bFound = true;
					break;
				}
			}
			if (!bFound) {
				DictionaryEArrayStruct *pGeoAddStruct = resDictGetEArrayStruct("CostumeGeometryAdd");
				for(j=eaSize(&pGeoAddStruct->ppReferents)-1; j>=0; --j) {
					PCGeometryAdd *pGeoAdd = (PCGeometryAdd*)pGeoAddStruct->ppReferents[j];
					if (pGeoAdd->pcGeoName && (stricmp(pGeoAdd->pcGeoName, pGeometry->pcName) == 0)) {
						for(k=eaSize(&pGeoAdd->eaAllowedMaterialDefs)-1; k>=0; --k) {
							if (pGeoAdd->eaAllowedMaterialDefs[k] && stricmp(pGeoAdd->eaAllowedMaterialDefs[k], pMaterial->pcName) == 0) {
								bFound = true;
								break;
							}
						}
					}
					if (bFound) {
						break;
					}
				}
			}
			if (!bFound) {
				if (pCostume->pcFileName) {
					ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to material def '%s' that is not allowed on geometry def '%s'\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hMatDef), pGeometry->pcName);
				}
				bResult = false;
			}
		}
	}

	if (REF_STRING_FROM_HANDLE(pPart->hPatternTexture) && (stricmp("None",REF_STRING_FROM_HANDLE(pPart->hPatternTexture)) != 0) ) {
		if (bGuildTexture) {
			if (pPart->pTextureValues && (pPart->pTextureValues->fPatternValue > pGuild->fEmblemRotation + TESTDELTA || pPart->pTextureValues->fPatternValue < pGuild->fEmblemRotation - TESTDELTA)) {
				if (pCostume->pcFileName) {
					ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-guild Pattern value\n",pCostume->pcName);
				}
				bResult = false;
			}
		} else {
			if (pBone && pBone->bIsGuildEmblemBone && (pCostume->eCostumeType == kPCCostumeType_Player || !bInEditor) && !bLoading) {
				const char *pTempTex = REF_STRING_FROM_HANDLE(pPart->hPatternTexture);
				if (pTexture) {
					for (j = eaSize(&g_GuildEmblems.eaEmblems)-1; j >= 0; --j) {
						if (!stricmp(REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[j]->hTexture),pTempTex)) {
							if (pCostume->pcFileName) {
								ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to pattern texture '%s' that can only be a guild texture\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hPatternTexture));
							}
							bResult = false;
							break;
						}
					}
				}
			}
			bResult &= costumeLoad_ValidatePartTexture(pCostume, pMaterial, GET_REF(pPart->hPatternTexture), REF_STRING_FROM_HANDLE(pPart->hPatternTexture));
			if (GET_REF(pPart->hPatternTexture)) {
				eTexFlags = GET_REF(pPart->hPatternTexture)->eTypeFlags;
				if ((eTexFlags & kPCTextureType_Pattern) == 0) {
					if (pCostume->pcFileName) {
						ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to pattern texture '%s' but this texture is not of pattern type\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hPatternTexture));
					}
					bResult = false;
				}
			}
		}
	}
	if (REF_STRING_FROM_HANDLE(pPart->hDetailTexture) && (stricmp("None",REF_STRING_FROM_HANDLE(pPart->hDetailTexture)) != 0) ) {
		if (eTexFlags & kPCTextureType_Detail) {
			if (pCostume->pcFileName) {
				ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to detail texture '%s' even though another texture is already of type detail\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hDetailTexture));
			}
			bResult = false;
		}
		if (!bGuildTexture) {
			if (pBone && pBone->bIsGuildEmblemBone && (pCostume->eCostumeType == kPCCostumeType_Player || !bInEditor) && !bLoading) {
				const char *pTempTex = REF_STRING_FROM_HANDLE(pPart->hDetailTexture);
				if (pTexture) {
					for (j = eaSize(&g_GuildEmblems.eaEmblems)-1; j >= 0; --j) {
						if (!stricmp(REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[j]->hTexture),pTempTex)) {
							if (pCostume->pcFileName) {
								ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to detail texture '%s' that can only be a guild texture\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hPatternTexture));
							}
							bResult = false;
							break;
						}
					}
				}
			}
			bResult &= costumeLoad_ValidatePartTexture(pCostume, pMaterial, GET_REF(pPart->hDetailTexture), REF_STRING_FROM_HANDLE(pPart->hDetailTexture));
			if (GET_REF(pPart->hDetailTexture)) {
				eTexFlags |= GET_REF(pPart->hDetailTexture)->eTypeFlags;
				if ((eTexFlags & kPCTextureType_Detail) == 0) {
					if (pCostume->pcFileName) {
						ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to detail texture '%s' but this texture is not of detail type\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hDetailTexture));
					}
					bResult = false;
				}
			}
		}
	}
	if (REF_STRING_FROM_HANDLE(pPart->hSpecularTexture) && (stricmp("None",REF_STRING_FROM_HANDLE(pPart->hSpecularTexture)) != 0) ) {
		if (eTexFlags & kPCTextureType_Specular) {
			if (pCostume->pcFileName) {
				ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to specular texture '%s' even though another texture is already of type specular\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hSpecularTexture));
			}
			bResult = false;
		}
		bResult &= costumeLoad_ValidatePartTexture(pCostume, pMaterial, GET_REF(pPart->hSpecularTexture), REF_STRING_FROM_HANDLE(pPart->hSpecularTexture));
		if (GET_REF(pPart->hSpecularTexture)) {
			eTexFlags |= GET_REF(pPart->hSpecularTexture)->eTypeFlags;
			if ((eTexFlags & kPCTextureType_Specular) == 0) {
				if (pCostume->pcFileName) {
					ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to specular texture '%s' but this texture is not of specular type\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hSpecularTexture));
				}
				bResult = false;
			}
		}
	}
	if (REF_STRING_FROM_HANDLE(pPart->hDiffuseTexture) && (stricmp("None",REF_STRING_FROM_HANDLE(pPart->hDiffuseTexture)) != 0) ) {
		if (eTexFlags & kPCTextureType_Diffuse) {
			if (pCostume->pcFileName) {
				ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to diffuse texture '%s' even though another texture is already of type diffuse\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hDiffuseTexture));
			}
			bResult = false;
		}
		bResult &= costumeLoad_ValidatePartTexture(pCostume, pMaterial, GET_REF(pPart->hDiffuseTexture), REF_STRING_FROM_HANDLE(pPart->hDiffuseTexture));
		if (GET_REF(pPart->hDiffuseTexture)) {
			eTexFlags |= GET_REF(pPart->hDiffuseTexture)->eTypeFlags;
			if ((eTexFlags & kPCTextureType_Diffuse) == 0) {
				if (pCostume->pcFileName) {
					ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to diffuse texture '%s' but this texture is not of diffuse type\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hDiffuseTexture));
				}
				bResult = false;
			}
		}
	}
	if (pPart->pMovableTexture) {
		if (REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture) && (stricmp("None",REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture)) != 0) ) {
			if (eTexFlags & kPCTextureType_Movable) {
				if (pCostume->pcFileName) {
					ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to movable texture '%s' even though another texture is already of type movable\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture));
				}
				bResult = false;
			}
			pTexture = GET_REF(pPart->pMovableTexture->hMovableTexture);

			if (bGuildTexture) {
				if ((!pGuild->pcEmblem2) || (!*pGuild->pcEmblem2) || GET_REF(pPart->pMovableTexture->hMovableTexture) != RefSystem_ReferentFromString(g_hCostumeTextureDict, pGuild->pcEmblem2)) {
					if (pCostume->pcFileName) {
						ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-guild Movable texture '%s'\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture));
					}
					bResult = false;
				}
			} else {
				if (pBone && pBone->bIsGuildEmblemBone && (pCostume->eCostumeType == kPCCostumeType_Player || !bInEditor) && !bLoading) {
					const char *pTempTex = REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture);
					if (pTexture) {
						for (j = eaSize(&g_GuildEmblems.eaEmblems)-1; j >= 0; --j) {
							if (!stricmp(REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[j]->hTexture),pTempTex)) {
								if (pCostume->pcFileName) {
									ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to movable texture '%s' that can only be a guild texture\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hPatternTexture));
								}
								bResult = false;
								break;
							}
						}
					}
				}
				bResult &= costumeLoad_ValidatePartTexture(pCostume, pMaterial, pTexture, REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture));
			}
			if (pTexture) {
				eTexFlags |= pTexture->eTypeFlags;
				if ((!bGuildTexture) && (eTexFlags & kPCTextureType_Movable) == 0 || !pTexture->pMovableOptions) {
					if (pCostume->pcFileName) {
						ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to movable texture '%s' but this texture is not of movable type\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture));
					}
					bResult = false;
				} else {
					PCTextureMovableOptions *pMovable = pTexture->pMovableOptions;
					if ((bGuildTexture) || !pMovable->bMovableCanEditPosition) {
						float defaultValueX;
						float defaultValueY;
						if (bGuildTexture) {
							defaultValueX = pGuild->fEmblem2X;
							defaultValueY = pGuild->fEmblem2Y;
						} else {
							F32 xDelta = pMovable->fMovableMaxX - pMovable->fMovableMinX;
							F32 yDelta = pMovable->fMovableMaxY - pMovable->fMovableMinY;
							
							if(xDelta){
								defaultValueX = (((pMovable->fMovableDefaultX - pMovable->fMovableMinX) * 200.0f)/xDelta) - 100.0f;
							}else{
								defaultValueX = 0.f;
							}

							if(yDelta){
								defaultValueY = (((pMovable->fMovableDefaultY - pMovable->fMovableMinY) * 200.0f)/yDelta) - 100.0f;
							}else{
								defaultValueY = 0.f;
							}
						}
						if (pPart->pMovableTexture->fMovableX > defaultValueX + TESTDELTA || pPart->pMovableTexture->fMovableX < defaultValueX - TESTDELTA) {
							if (pCostume->pcFileName) {
								if (bGuildTexture) {
									ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-guild MovableX value\n",pCostume->pcName);
								} else {
									ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-default MovableX value even though CanEditPosition is False\n",pCostume->pcName);
								}
							}
							bResult = false;
						}
						if (pPart->pMovableTexture->fMovableY > defaultValueY + TESTDELTA || pPart->pMovableTexture->fMovableY < defaultValueY - TESTDELTA) {
							if (pCostume->pcFileName) {
								if (bGuildTexture) {
									ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-guild MovableY value\n",pCostume->pcName);
								} else {
									ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-default MovableY value even though CanEditPosition is False\n",pCostume->pcName);
								}
							}
							bResult = false;
						}
					}
					if ((bGuildTexture) || !pMovable->bMovableCanEditScale) {
						float defaultScaleX;
						float defaultScaleY;
						if (bGuildTexture) {
							defaultScaleX = pGuild->fEmblem2ScaleX;
							defaultScaleY = pGuild->fEmblem2ScaleY;
						} else {
							F32 xDelta = pMovable->fMovableMaxScaleX - pMovable->fMovableMinScaleX;
							F32 yDelta = pMovable->fMovableMaxScaleY - pMovable->fMovableMinScaleY;
							
							if(xDelta){
								defaultScaleX = (((pMovable->fMovableDefaultScaleX - pMovable->fMovableMinScaleX) * 100.0f)/xDelta);
							}else{
								defaultScaleX = 1.f;
							}
							
							if(yDelta){
								defaultScaleY = (((pMovable->fMovableDefaultScaleY - pMovable->fMovableMinScaleY) * 100.0f)/yDelta);
							}else{
								defaultScaleY = 1.f;
							}
						}
						if (pPart->pMovableTexture->fMovableScaleX > defaultScaleX + TESTDELTA || pPart->pMovableTexture->fMovableScaleX < defaultScaleX - TESTDELTA) {
							if (pCostume->pcFileName) {
								if (bGuildTexture) {
									ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-guild MovableScaleX value\n",pCostume->pcName);
								} else {
									ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-default MovableScaleX value even though CanEditScale is False\n",pCostume->pcName);
								}
							}
							bResult = false;
						}
						if (pPart->pMovableTexture->fMovableScaleY > defaultScaleY + TESTDELTA || pPart->pMovableTexture->fMovableScaleY < defaultScaleY - TESTDELTA) {
							if (pCostume->pcFileName) {
								if (bGuildTexture) {
									ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-guild MovableScaleY value\n",pCostume->pcName);
								} else {
									ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-default MovableScaleY value even though CanEditScale is False\n",pCostume->pcName);
								}
							}
							bResult = false;
						}
					}
					if ((bGuildTexture) || !pMovable->bMovableCanEditRotation) {
						float defaultRotation;
						if (bGuildTexture) {
							defaultRotation = pGuild->fEmblem2Rotation;
						} else {
							defaultRotation = pMovable->fMovableDefaultRotation;
						}
						if (pPart->pMovableTexture->fMovableRotation > defaultRotation + TESTDELTA || pPart->pMovableTexture->fMovableRotation < defaultRotation - TESTDELTA) {
							if (pCostume->pcFileName) {
								if (bGuildTexture) {
									ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-guild MovableRotation value\n",pCostume->pcName);
								} else {
									ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-default MovableRotation value even though CanEditRotation is False\n",pCostume->pcName);
								}
							}
							bResult = false;
						}
					}
				}
			}
		}
	}
	if (pPart->pArtistData) {
		for(j=eaSize(&pPart->pArtistData->eaExtraTextures)-1; j>=0; --j) {
			if (REF_STRING_FROM_HANDLE(pPart->pArtistData->eaExtraTextures[j]->hTexture) && !GET_REF(pPart->pArtistData->eaExtraTextures[j]->hTexture)) {
				if (pCostume->pcFileName) {
					ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-existent texture def '%s'\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->pArtistData->eaExtraTextures[j]->hTexture));
				}
				bResult = false;
			}
		}
	}

	if (ePartType == kPCPartType_Child) {
		// Make sure child part is properly related to parent part
		if (pGeometry) {
			PCGeometryDef *pParentGeo = GET_REF(pParentPart->hGeoDef);
			PCGeometryOptions *pOptions = pGeometry->pOptions;
			if (!pOptions->bIsChild) {
				if (pCostume->pcFileName) {
					ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' uses geometry '%s' as a child part and this is not a child geometry\n",pCostume->pcName, pGeometry->pcName);
				}
				bResult = false;
			}
			if (pParentGeo) {
				PCGeometryOptions *pParentOptions = pParentGeo->pOptions;
				bFound = false;
				for(j=eaSize(&pParentOptions->eaChildGeos)-1; j>=0 && !bFound; --j) {
					PCGeometryChildDef *pChild = pParentOptions->eaChildGeos[j];
					for(k=eaSize(&pChild->eaChildGeometries)-1; k>=0; --k) {
						if (pGeometry == GET_REF(pChild->eaChildGeometries[k]->hGeo)) {
							bFound = true;
							break;
						}
					}
				}
				if (!bFound) {
					if (pCostume->pcFileName) {
						ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' uses geometry '%s' as a child part and this is not a valid child geometry for '%s'\n",pCostume->pcName, pGeometry->pcName, pParentGeo->pcName);
					}
					bResult = false;
				}
			}
		}
	}
	if (ePartType == kPCPartType_Cloth) {
		// Make sure cloth part is properly related to parent part
		if (pGeometry) {
			PCGeometryClothData *pClothData = pGeometry->pClothData;
			PCGeometryDef *pParentGeo = GET_REF(pParentPart->hGeoDef);
			if (!pClothData->bIsCloth) {
				if (pCostume->pcFileName) {
					ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' uses geometry '%s' as a cloth child part and this is not a cloth geometry\n",pCostume->pcName, pGeometry->pcName);
				}
				bResult = false;
			}
			if (pParentGeo && (pParentGeo != pGeometry)) {
				if (pCostume->pcFileName) {
					ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' uses geometry '%s' as a cloth child part and it does not match the parent part\n",pCostume->pcName, pGeometry->pcName);
				}
				bResult = false;
			}
		}
	}

	if (pPart->pClothLayer) {
		bResult &= costumeLoad_ValidatePCPart(pCostume, pPart->pClothLayer, pPart, pSkeleton, kPCPartType_Cloth, bInEditor, pGuild, bLoading);
	}

	return bResult;
}


bool costumeLoad_ValidateBoneScale(PlayerCostume *pCostume, SpeciesDef *pSpecies, PCScaleInfo *pScale)
{
	PCScaleValue *pValue = NULL;
	F32 fMin, fMax;
	bool bOverride = false;
	bool bResult = true;
	int i;

	// find the scale on the costume (if any)
	for(i=eaSize(&pCostume->eaScaleValues)-1; i>=0; --i) {
		if (stricmp(pCostume->eaScaleValues[i]->pcScaleName, pScale->pcName) == 0) {
			pValue = pCostume->eaScaleValues[i];
			break;
		}
	}

	fMin = pScale->fPlayerMin;
	fMax = pScale->fPlayerMax;
	if (pSpecies) {
		for(i=0; i < eaSize(&pSpecies->eaBoneScaleLimits); ++i) {
			if (stricmp(pSpecies->eaBoneScaleLimits[i]->pcName, pScale->pcName) == 0) {
				fMin = pSpecies->eaBoneScaleLimits[i]->fMin;
				fMax = pSpecies->eaBoneScaleLimits[i]->fMax;
				bOverride = true;
				break;
			}
		}
	}

	if ((pValue && (pValue->fValue < fMin || pValue->fValue > fMax)) ||
		(!pValue && (fMin > 0 || fMax < 0))) {
		if (bOverride) {
			ErrorFilenamef(pCostume->pcFileName, "Costume '%s' has Bone Scale Value '%s' of %g out of valid range for species %g -> %g.", pCostume->pcName, pScale->pcName, pValue ? pValue->fValue : 0.0, fMin, fMax);
			bResult = false;
		} else {
			ErrorFilenamef(pCostume->pcFileName, "Costume '%s' has Bone Scale Value '%s' of %g out of valid range %g -> %g.", pCostume->pcName, pScale->pcName, pValue ? pValue->fValue : 0.0, fMin, fMax);
			bResult = false;
		}
	}

	return bResult;
}


bool costumeLoad_ValidatePlayerCostume(PlayerCostume *pCostume, SpeciesDef *pSpecies, bool bInEditor, bool bLoading, bool bCheckFX)
{
	int i,j,k;
	PCSkeletonDef *pSkeleton;
	PCGeometryDef *pGeometry;
	PCFXSwap *pFXSwap;
	PCFX *pFX;
	const char *pcTempFileName;
	const char **eaTexWordsKeys = NULL;
	bool bResult = true;
	bool bFound;

	if (!resIsValidName(pCostume->pcName)) {
		ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' does not have a valid name\n",pCostume->pcName);
		bResult = false;
	}
	if (!resIsValidScope(pCostume->pcScope)) {
		ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' does not have a valid scope '%s'\n",pCostume->pcName, pCostume->pcScope);
		bResult = false;
	}

	pcTempFileName = pCostume->pcFileName;
	if (resFixPooledFilename(&pcTempFileName, pCostume->pcScope && resIsInDirectory(pCostume->pcScope, "maps/") ? NULL : COSTUMES_BASE_DIR, pCostume->pcScope, pCostume->pcName, COSTUMES_EXTENSION)) {
		ErrorFilenamef( pCostume->pcFileName, "Costume filename does not match name '%s' scope '%s'", pCostume->pcName, pCostume->pcScope);
		bResult = false;
	}

	if (!REF_STRING_FROM_HANDLE(pCostume->hSkeleton)) {
		ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' missing the skeleton name\n",pCostume->pcName);
		return false; // Can't continue without a skeleton
	} else {
		pSkeleton = GET_REF(pCostume->hSkeleton);
		if (!pSkeleton) {
			ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to non-existent skeleton def '%s'\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pCostume->hSkeleton));
			return false; // Can't continue without a skeleton
		}
		if (pSkeleton && !costumeTailor_TestRestriction(pSkeleton->eRestriction, true, pCostume)) {
			// Don't issue an error for this any more
			//ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to skeleton def '%s' but it is restricted from using it\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pCostume->hSkeleton));
		}
		if (pSkeleton && pCostume->pcStance) {
			bFound = false;
			if (pSpecies && eaSize(&pSpecies->eaStanceInfo))
			{
				for(i=eaSize(&pSpecies->eaStanceInfo)-1; i>=0; --i) {
					if (stricmp(pSpecies->eaStanceInfo[i]->pcName,pCostume->pcStance) == 0) {
						bFound = true;
						break;
					}
				}
				if (!bFound) {
					ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to stance '%s' that is not defined for the species '%s'\n",pCostume->pcName, pCostume->pcStance, pSpecies->pcName);
					bResult = false;
				}
			}
			else
			{
				for(i=eaSize(&pSkeleton->eaStanceInfo)-1; i>=0; --i) {
					if (stricmp(pSkeleton->eaStanceInfo[i]->pcName,pCostume->pcStance) == 0) {
						bFound = true;
						break;
					}
				}
				if (!bFound) {
					ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to stance '%s' that is not defined for the skeleton\n",pCostume->pcName, pCostume->pcStance);
					bResult = false;
				}
			}
		}

		if (pSkeleton && (pSkeleton->fHeightBase > 0.0) && ((pCostume->fHeight < pSkeleton->fHeightMin) || (pCostume->fHeight > pSkeleton->fHeightMax))) {
			ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' height '%g' is not within the skeleton range of %g to %g\n",pCostume->pcName, pCostume->fHeight, pSkeleton->fHeightMin, pSkeleton->fHeightMax);
			bResult = false;
		}

		/*if (pSkeleton && (eaSize(&pSkeleton->eaBodyScaleInfo) < eafSize(&pCostume->eafBodyScales))) {
			ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' has too many body scale values\n",pCostume->pcName);
			bResult = false;
		}*/ // BZ comment out this error check because of bad data we don't have time to fix
	}

	if (pCostume->pArtistData && pCostume->pArtistData->pcCollisionGeo) {
		pGeometry = (PCGeometryDef*)RefSystem_ReferentFromString(g_hCostumeGeometryDict, pCostume->pArtistData->pcCollisionGeo);
		if (!pGeometry) {
			ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to collision geometry def '%s' that does not exist\n",pCostume->pcName, pCostume->pArtistData->pcCollisionGeo);
			bResult = false;
		} else if (!pGeometry->pcModel) {
			ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to collision geometry def '%s' that has no model\n",pCostume->pcName, pCostume->pArtistData->pcCollisionGeo);
			bResult = false;
		}
	}

	if (bCheckFX && pCostume->pArtistData && eaSize(&pCostume->pArtistData->eaFX) > 0) {
		for (i=eaSize(&pCostume->pArtistData->eaFX)-1; i>=0; --i) {
			pFX = pCostume->pArtistData->eaFX[i];
			if (!dynFxInfoExists(pFX->pcName)) {
				ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to fx '%s' that does not exist\n",pCostume->pcName,pFX->pcName);
				bResult = false;
			}
		}
	}

	if (bCheckFX && pCostume->pArtistData && eaSize(&pCostume->pArtistData->eaFXSwap) > 0) {
		for (i=eaSize(&pCostume->pArtistData->eaFXSwap)-1; i>=0; --i) {
			pFXSwap = pCostume->pArtistData->eaFXSwap[i];
			if (!dynFxInfoExists(pFXSwap->pcNewName)) {
				ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to fx '%s' that does not exist\n",pCostume->pcName,pFXSwap->pcNewName);
				bResult = false;
			}
			if (!dynFxInfoExists(pFXSwap->pcOldName)) {
				ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to fx '%s' that does not exist\n",pCostume->pcName,pFXSwap->pcOldName);
				bResult = false;
			}
		}
	}

	for(i=eaSize(&pCostume->eaRegionCategories)-1; i>=0; --i) {
		PCRegion *pReg = GET_REF(pCostume->eaRegionCategories[i]->hRegion);
		PCCategory *pCat = GET_REF(pCostume->eaRegionCategories[i]->hCategory);

		if (!pReg) {
			ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to region '%s' that does not exist\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pCostume->eaRegionCategories[i]->hRegion));
			bResult = false;
		}
		if (!pCat) {
			ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to category '%s' that does not exist\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pCostume->eaRegionCategories[i]->hCategory));
			bResult = false;
		} else if (pReg) {
			bFound = false;
			for(j=eaSize(&pReg->eaCategories)-1; j>=0; --j) {
				if (GET_REF(pReg->eaCategories[j]->hCategory) == pCat) {
					bFound = true;
					break;
				}
			}
			if (!bFound) {
				ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to category '%s' that is not valid in region '%s'\n",pCostume->pcName, REF_STRING_FROM_HANDLE(pCostume->eaRegionCategories[i]->hCategory), REF_STRING_FROM_HANDLE(pCostume->eaRegionCategories[i]->hRegion));
				bResult = false;
			}
		}

		for(j=i-1; j>=0; --j) {
			if (pReg == GET_REF(pCostume->eaRegionCategories[j]->hRegion)) {
				ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' refers to region '%s' more than once\n",pCostume->pcName, pReg->pcName);
				bResult = false;
			}
		}
	}

	// Do not perform region/category validation when loading costumes
	//for(i=eaSize(&pSkeleton->eaRegions)-1; i>=0; --i) {
	//	costumeTailor_PickBestCategory(pCostume, pSkeleton->eaRegions[i]->pcName, true);
	//}
	
	for(i=eaSize(&pCostume->eaParts)-1; i>=0; --i) {
		PCBoneDef *pBone = GET_REF(pCostume->eaParts[i]->hBoneDef);

		bResult &= costumeLoad_ValidatePCPart(pCostume, pCostume->eaParts[i], NULL, pSkeleton, kPCPartType_Primary, bInEditor, NULL, bLoading);

		if (pCostume->eCostumeType != kPCCostumeType_Item) {
			// Disallow two parts on the same bone
			for(j=i-1; j>=0; --j) {
				PCBoneDef *pOtherBone = GET_REF(pCostume->eaParts[j]->hBoneDef);
				if (pBone && pOtherBone && (pBone == pOtherBone)) {
					ErrorFilenamef(pCostume->pcFileName,"Player costume '%s' is attempting to put more than one part on bone '%s'\n",pCostume->pcName, pBone->pcName);
					bResult = false;
				}
			}
		}
	}
	
	// If this is an item costume, check that this doesn't contain any
	// parts that would fail to unlock anything
	if (pCostume->eCostumeType == kPCCostumeType_Item || pCostume->eCostumeType == kPCCostumeType_Overlay) {
		for (i = eaSize(&pCostume->eaParts)-1; i >= 0; i--) {
			PCPart *pPart = pCostume->eaParts[i];
			if (pPart) {
				bool bPlayerInitial = true;
				
				if (GET_REF(pPart->hGeoDef)) {
					PCGeometryDef *pGeo = GET_REF(pPart->hGeoDef);
					if (!(pGeo->eRestriction & kPCRestriction_Player_Initial)) {
						bPlayerInitial = false;
						if (!(pGeo->eRestriction & kPCRestriction_Player)) {
							ErrorFilenamef(pCostume->pcFileName, "Item costume '%s' contains geometry '%s' which can not be used by players", pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hGeoDef));
							bResult = false;
						}
					}
				}
				
				if (GET_REF(pPart->hMatDef)) {
					PCMaterialDef *pMat = GET_REF(pPart->hMatDef);
					if (!(pMat->eRestriction & kPCRestriction_Player_Initial)) {
						bPlayerInitial = false;
						if (!(pMat->eRestriction & kPCRestriction_Player)) {
							ErrorFilenamef(pCostume->pcFileName, "Item costume '%s' contains material '%s' which can not be used by players", pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hMatDef));
							bResult = false;
						}
					}
				}
				
				if (GET_REF(pPart->hPatternTexture)) {
					PCTextureDef *pTex = GET_REF(pPart->hPatternTexture);
					if (!(pTex->eRestriction & kPCRestriction_Player_Initial)) {
						bPlayerInitial = false;
						if (!(pTex->eRestriction & kPCRestriction_Player)) {
							ErrorFilenamef(pCostume->pcFileName, "Item costume '%s' contains pattern texture '%s' which can not be used by players", pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hPatternTexture));
							bResult = false;
						}
					}
				}
				
				if (GET_REF(pPart->hDetailTexture)) {
					PCTextureDef *pTex = GET_REF(pPart->hDetailTexture);
					if (!(pTex->eRestriction & kPCRestriction_Player_Initial)) {
						bPlayerInitial = false;
						if (!(pTex->eRestriction & kPCRestriction_Player)) {
							ErrorFilenamef(pCostume->pcFileName, "Item costume '%s' contains detail texture '%s' which can not be used by players", pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hDetailTexture));
							bResult = false;
						}
					}
				}
				
				if (GET_REF(pPart->hSpecularTexture)) {
					PCTextureDef *pTex = GET_REF(pPart->hSpecularTexture);
					if (!(pTex->eRestriction & kPCRestriction_Player_Initial)) {
						bPlayerInitial = false;
						if (!(pTex->eRestriction & kPCRestriction_Player)) {
							ErrorFilenamef(pCostume->pcFileName, "Item costume '%s' contains specular texture '%s' which can not be used by players", pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hSpecularTexture));
							bResult = false;
						}
					}
				}
				
				if (GET_REF(pPart->hDiffuseTexture)) {
					PCTextureDef *pTex = GET_REF(pPart->hDiffuseTexture);
					if (!(pTex->eRestriction & kPCRestriction_Player_Initial)) {
						bPlayerInitial = false;
						if (!(pTex->eRestriction & kPCRestriction_Player)) {
							ErrorFilenamef(pCostume->pcFileName, "Item costume '%s' contains diffuse texture '%s' which can not be used by players", pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hDiffuseTexture));
							bResult = false;
						}
					}
				}

				if (pPart->pMovableTexture) {
					if (GET_REF(pPart->pMovableTexture->hMovableTexture)) {
						PCTextureDef *pTex = GET_REF(pPart->pMovableTexture->hMovableTexture);
						if (!(pTex->eRestriction & kPCRestriction_Player_Initial)) {
							bPlayerInitial = false;
							if (!(pTex->eRestriction & kPCRestriction_Player)) {
								ErrorFilenamef(pCostume->pcFileName, "Item costume '%s' contains movable texture '%s' which can not be used by players", pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture));
								bResult = false;
							}
						}
					}
				}
				
				if (bPlayerInitial && pCostume->eCostumeType != kPCCostumeType_Overlay) {
					ErrorFilenamef(pCostume->pcFileName, "Item costume '%s' contains a piece with geometry '%s' where the geometry, material and all textures are already player initial.", pCostume->pcName, REF_STRING_FROM_HANDLE(pPart->hGeoDef));
					bResult = false;
				}
			}
		}
	}

	// Ensure tex words keys are valid
	costumeTailor_GetValidTexWordsKeys(CONTAINER_NOCONST(PlayerCostume, pCostume), &eaTexWordsKeys);
	for(i=eaSize(&pCostume->eaTexWords)-1; i>=0; --i) {
		PCTexWords *pTexWords = pCostume->eaTexWords[i];

		for(j=eaSize(&eaTexWordsKeys)-1; j>=0; --j) {
			if (pTexWords->pcKey && (stricmp(pTexWords->pcKey, eaTexWordsKeys[j]) == 0)) {
				break;
			}
		}
		if (j < 0) {
			ErrorFilenamef(pCostume->pcFileName, "Costume '%s' sets TexWords key '%s' that is not used by any texture it has.", pCostume->pcName, pTexWords->pcKey);
			bResult = false;
		}
	}
	eaDestroy(&eaTexWordsKeys);

	if (pSkeleton) {
		// Look for scale values that should not be present
		for(i=eaSize(&pCostume->eaScaleValues)-1; i>=0; --i) {
			PCScaleValue *pValue;
			PCScaleInfo *pScale = NULL;

			pValue = pCostume->eaScaleValues[i];

			for (j=eaSize(&pSkeleton->eaScaleInfoGroups)-1; j >= 0 && !pScale; --j) {
				for (k=eaSize(&pSkeleton->eaScaleInfoGroups[j]->eaScaleInfo)-1; k >= 0; --k) {
					if (stricmp(pSkeleton->eaScaleInfoGroups[j]->eaScaleInfo[k]->pcName, pValue->pcScaleName) == 0) {
						pScale = pSkeleton->eaScaleInfoGroups[j]->eaScaleInfo[k];
						break;
					}
				}
			}
			if (!pScale) {
				for (k=eaSize(&pSkeleton->eaScaleInfo)-1; k >= 0; --k) {
					if (stricmp(pSkeleton->eaScaleInfo[k]->pcName, pValue->pcScaleName) == 0) {
						pScale = pSkeleton->eaScaleInfo[k];
						break;
					}
				}
			}

			if (!pScale) {
				ErrorFilenamef(pCostume->pcFileName, "Costume '%s' uses bone scale '%s' that is not legal on the skeleton.", pCostume->pcName, pValue->pcScaleName);
				bResult = false;
			}
		}
	}

	if (pSkeleton && pCostume->eCostumeType == kPCCostumeType_Player) {
		// Look for scale values that should be present and make sure they are legal (only for player type)
		for (i=eaSize(&pSkeleton->eaScaleInfoGroups)-1; i >= 0; --i) {
			for (j=eaSize(&pSkeleton->eaScaleInfoGroups[i]->eaScaleInfo)-1; j >= 0; --j) {
				PCScaleInfo *pScale = pSkeleton->eaScaleInfoGroups[i]->eaScaleInfo[j];
				if (!costumeLoad_ValidateBoneScale(pCostume, pSpecies, pScale)) {
					bResult = false;
				}
			}
		}
		for (i=eaSize(&pSkeleton->eaScaleInfo)-1; i >= 0; --i) {
			PCScaleInfo *pScale = pSkeleton->eaScaleInfo[i];
			if (!costumeLoad_ValidateBoneScale(pCostume, pSpecies, pScale)) {
				bResult = false;
			}
		}
	}

	if (pCostume->pArtistData && pCostume->pArtistData->ppCollCapsules)
	{
		// Check for invalid capsule values
		for(i=0; i<eaSize(&pCostume->pArtistData->ppCollCapsules); i++)
		{
			if (pCostume->pArtistData->ppCollCapsules[i]->fLength < 0.0f)
			{
				ErrorFilenamef(pCostume->pcFileName, "Costume '%s' has an override capsule with an invalid length of %f\n", pCostume->pcName,pCostume->pArtistData->ppCollCapsules[i]->fLength);
			}

			if (pCostume->pArtistData->ppCollCapsules[i]->fRadius < 0.0f)
			{
				ErrorFilenamef(pCostume->pcFileName, "Costume '%s' has an override capsule with an invalid radius of %f\n", pCostume->pcName,pCostume->pArtistData->ppCollCapsules[i]->fRadius);
			}
		}
	}

	// TODO: Ensure FX exist
	// TODO: Ensure extra mat constants and extra colors exist

	return bResult;
}

static void costumeLoad_ValidatePlayerCostumes(void)
{
	DictionaryEArrayStruct *pCostumes = resDictGetEArrayStruct("PlayerCostume");
	int i;

	for(i=eaSize(&pCostumes->ppReferents)-1; i>=0; --i) {
		PlayerCostume *pc = pCostumes->ppReferents[i];
		costumeLoad_ValidatePlayerCostume(pc, pc ? GET_REF(pc->hSpecies) : NULL, false, true, true);
	}
}


bool costumeLoad_ValidateCostumeSet(PCCostumeSet *pCostumeSet)
{
	int i;
	bool bResult = true;

	if (!resIsValidName(pCostumeSet->pcName)) {
		ErrorFilenamef(pCostumeSet->pcFileName,"Costume set '%s' does not have a valid name\n",pCostumeSet->pcName);
		bResult = false;
	}
	if ((!(pCostumeSet->eCostumeSetFlags & (kPCCostumeSetFlags_Unlockable | kPCCostumeSetFlags_TailorPresets))) && !GET_REF(pCostumeSet->displayNameMsg.hMessage)) {
		ErrorFilenamef(pCostumeSet->pcFileName,"Costume set '%s' is missing the display name message\n",pCostumeSet->pcName);
		bResult = false;
	}

	// generate the requires expression for the costume
	if (pCostumeSet->pExprUnlock)
	{
		if (!(pCostumeSet->eCostumeSetFlags & kPCCostumeSetFlags_Unlockable))
		{
			ErrorFilenamef(pCostumeSet->pcFileName,"Costume set '%s' has an expression but is not set to unlockable\n",pCostumeSet->pcName);
			bResult = false;
		}
	}
	else if ((pCostumeSet->eCostumeSetFlags & kPCCostumeSetFlags_Unlockable))
	{
		ErrorFilenamef(pCostumeSet->pcFileName,"Costume set '%s' is unlockable but doesn't have an expression\n",pCostumeSet->pcName);
		bResult = false;
	}

	for (i = eaSize(&pCostumeSet->eaPlayerCostumes)-1; i >= 0; --i)
	{
		CostumeRefForSet *crfs = pCostumeSet->eaPlayerCostumes[i];
		PlayerCostume *pc = GET_REF(crfs->hPlayerCostume);

		if ((!(pCostumeSet->eCostumeSetFlags & (kPCCostumeSetFlags_Unlockable | kPCCostumeSetFlags_TailorPresets))) && !resIsValidName(crfs->pcName)) {
			ErrorFilenamef(pCostumeSet->pcFileName,"Costume set '%s' has a player costume identifier that does not have a valid name '%s'\n",pCostumeSet->pcName,crfs->pcName);
			bResult = false;
		}
		if ((!(pCostumeSet->eCostumeSetFlags & (kPCCostumeSetFlags_Unlockable | kPCCostumeSetFlags_TailorPresets))) && !GET_REF(crfs->displayNameMsg.hMessage)) {
			ErrorFilenamef(pCostumeSet->pcFileName,"Costume set '%s' has a player costume identifier that is missing the display name message '%s'\n",pCostumeSet->pcName,crfs->pcName);
			bResult = false;
		}
		if (!pc)
		{
			if (REF_STRING_FROM_HANDLE(crfs->hPlayerCostume))
			{
				ErrorFilenamef(pCostumeSet->pcFileName,"Costume set '%s' refers to non-existant player costume '%s'\n",pCostumeSet->pcName,REF_STRING_FROM_HANDLE(crfs->hPlayerCostume));
				bResult = false;
			}
			continue;
		} else {
			if (pc->eCostumeType != pCostumeSet->eCostumeType)
			{
				ErrorFilenamef(pCostumeSet->pcFileName,"Costume set '%s' has a player costume '%s' that is not the correct type\n",pCostumeSet->pcName,REF_STRING_FROM_HANDLE(crfs->hPlayerCostume));
				bResult = false;
			}
			if (((pCostumeSet->eCostumeSetFlags & kPCCostumeSetFlags_DontPreloadOnClient) == 0) && !pc->bLoadedOnClient) 
			{
				ErrorFilenamef(pCostumeSet->pcFileName,"Costume set '%s' has a player costume '%s' that is not marked with LoadedOnClient.  This is required for costume set costumes unless the costume set is marked as 'DontPreloadOnClient'.\n",pCostumeSet->pcName,REF_STRING_FROM_HANDLE(crfs->hPlayerCostume));
				bResult = false;
			}
		}
	}

	return bResult;
}

void fixupCostumeSet(PCCostumeSet *pCostumeSet)
{
	// generate the requires expression for the costume
	if (pCostumeSet->pExprUnlock)
	{
		exprGenerate(pCostumeSet->pExprUnlock, s_pCostumeContext);
	}
}

static void costumeLoad_ValidateRegion(PCRegion *pDef)
{
	int j;

	if (!resIsValidName(pDef->pcName)) {
		ErrorFilenamef(pDef->pcFileName,"Costume region '%s' does not have a valid name\n",pDef->pcName);
	}
	if (!GET_REF(pDef->displayNameMsg.hMessage)) {
		ErrorFilenamef(pDef->pcFileName,"Costume region '%s' missing the display name message\n",pDef->pcName);
	}
	if (REF_STRING_FROM_HANDLE(pDef->hDefaultCategory)) {
		PCCategory *pCat = GET_REF(pDef->hDefaultCategory);
		if (!pCat) {
			ErrorFilenamef(pDef->pcFileName,"Costume region '%s' refers to non-existent category '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultCategory));
		}
	}
	if (!eaSize(&pDef->eaCategories)) {
		ErrorFilenamef(pDef->pcFileName,"Costume region '%s' must have at least one category\n",pDef->pcName);
	}
	for(j=eaSize(&pDef->eaCategories)-1; j>=0; --j) {
		PCCategory *pCat = GET_REF(pDef->eaCategories[j]->hCategory);
		if (!pCat && REF_STRING_FROM_HANDLE(pDef->eaCategories[j]->hCategory)) {
			ErrorFilenamef(pDef->pcFileName,"Costume region '%s' refers to non-existent category '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->eaCategories[j]->hCategory));
		}
	}
}


static void costumeLoad_ValidateRegions(void)
{
	DictionaryEArrayStruct *pRegions = resDictGetEArrayStruct("CostumeRegion");
	int i;

	for(i=eaSize(&pRegions->ppReferents)-1; i>=0; --i) {
		costumeLoad_ValidateRegion(pRegions->ppReferents[i]);
	}
}


static void costumeLoad_ValidateCategory(PCCategory *pDef)
{
	if (!resIsValidName(pDef->pcName)) {
		ErrorFilenamef(pDef->pcFileName,"Costume category '%s' does not have a valid name\n",pDef->pcName);
	}
	if (!GET_REF(pDef->displayNameMsg.hMessage)) {
		ErrorFilenamef(pDef->pcFileName,"Costume category '%s' missing the display name message\n",pDef->pcName);
	}
}


static void costumeLoad_ValidateCategories(void)
{
	DictionaryEArrayStruct *pCats = resDictGetEArrayStruct("CostumeCategory");
	int i;

	for(i=eaSize(&pCats->ppReferents)-1; i>=0; --i) {
		costumeLoad_ValidateCategory(pCats->ppReferents[i]);
	}
}


static void costumeLoad_ValidateStyle(PCStyle *pDef)
{
	if (!resIsValidName(pDef->pcName)) {
		ErrorFilenamef(pDef->pcFileName,"Costume style '%s' does not have a valid name\n",pDef->pcName);
	}
	if (!GET_REF(pDef->displayNameMsg.hMessage)) {
		ErrorFilenamef(pDef->pcFileName,"Costume style '%s' missing the display name message\n",pDef->pcName);
	}
}


static void costumeLoad_ValidateStyles()
{
	DictionaryEArrayStruct *pStyles = resDictGetEArrayStruct("CostumeStyle");
	int i;

	for(i=eaSize(&pStyles->ppReferents)-1; i>=0; --i) {
		costumeLoad_ValidateStyle(pStyles->ppReferents[i]);
	}
}


static void costumeLoad_ValidateTexture(PCTextureDef *pDef)
{
	int i,j;
	int flags;

	if (!resIsValidName(pDef->pcName)) {
		ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' does not have a valid name\n",pDef->pcName);
	}
	if (!GET_REF(pDef->displayNameMsg.hMessage)) {
		ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' missing the display name message\n",pDef->pcName);
	}
	if (!pDef->pcNewTexture || !pDef->pcOrigTexture) {
		ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' missing either old or new texture name\n",pDef->pcName);
	}
	if (!pDef->eTypeFlags && pDef->pcName && (stricmp(pDef->pcName,"None") != 0)) {
		ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' has no type\n",pDef->pcName);
	}
	flags = pDef->eTypeFlags;
	flags &= (~kPCTextureType_Other);

	for(j=eaSize(&pDef->eaExtraSwaps)-1; j>=0; --j) {
		if (!pDef->eaExtraSwaps[j]->pcNewTexture || !strlen(pDef->eaExtraSwaps[j]->pcNewTexture) ||
			!pDef->eaExtraSwaps[j]->pcOrigTexture || !strlen(pDef->eaExtraSwaps[j]->pcOrigTexture)) {
			ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' missing either old or new texture name\n",pDef->pcName);
		}
		if (!pDef->eaExtraSwaps[j]->eTypeFlags) {
			ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' has an extra texture that has no type\n",pDef->pcName);
		}
		if (flags & pDef->eaExtraSwaps[j]->eTypeFlags) {
			ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' has an extra texture that is of a conflicting type\n",pDef->pcName);
		}
		// Allow as many uses of "Other" as desired
		flags &= (~kPCTextureType_Other);
	}

	// Make sure the texture file really exists
	// "None" is special and is treated as not being present.
	// Starting with backslash means it's a TexWords magic, so not validated
	if (pDef->pcNewTexture && (stricmp("None",pDef->pcNewTexture) != 0) && (pDef->pcNewTexture[0] != '\\') && !pDef->pcTexWordsKey) {
		if (!g_eaTexFileNames) {
			costumeLoad_TexFileListLoad();
		}
		for(j=eaSize(&g_eaTexFileNames)-1; j>=0; --j) {
			if (stricmp(g_eaTexFileNames[j], pDef->pcNewTexture) == 0) {
				break;
			}
		}
		if (j < 0) {
			ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' refers to non-existent graphics texture '%s'\n",pDef->pcName, pDef->pcNewTexture);
		}
	}
	for(i=eaSize(&pDef->eaExtraSwaps)-1; i>=0; --i) {
		if (pDef->eaExtraSwaps[i]->pcNewTexture && (stricmp("None",pDef->eaExtraSwaps[i]->pcNewTexture) != 0) && (pDef->eaExtraSwaps[i]->pcNewTexture[0] != '\\') && !pDef->eaExtraSwaps[i]->pcTexWordsKey) {
			if (!g_eaTexFileNames) {
				costumeLoad_TexFileListLoad();
			}
			for(j=eaSize(&g_eaTexFileNames)-1; j>=0; --j) {
				if (stricmp(g_eaTexFileNames[j], pDef->eaExtraSwaps[i]->pcNewTexture) == 0) {
					break;
				}
			}
			if (j < 0) {
				ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' refers to non-existent graphics texture '%s'\n",pDef->pcName, pDef->eaExtraSwaps[i]->pcNewTexture);
			}
		}
	}

	if (pDef->pValueOptions )
	{
		if (!(pDef->pValueOptions->pcValueConstant))
		{
			ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' has material values specified but no constant name\n", pDef->pcName);
		}
		if (pDef->pValueOptions->fValueMax <= pDef->pValueOptions->fValueMin || pDef->pValueOptions->fValueDefault < pDef->pValueOptions->fValueMin || pDef->pValueOptions->fValueDefault > pDef->pValueOptions->fValueMax)
		{
			ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' refers to material constant '%s' with invalid values min/max/default\n", pDef->pcName, pDef->pValueOptions->pcValueConstant);
		}
	}

	if (pDef->eTypeFlags & kPCTextureType_Movable)
	{
		if (!pDef->pMovableOptions)
		{
			ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' is a movable texture but has no movable options set\n", pDef->pcName);
		}
		else
		{
			if (pDef->pMovableOptions->fMovableMaxX < pDef->pMovableOptions->fMovableMinX || pDef->pMovableOptions->fMovableDefaultX < pDef->pMovableOptions->fMovableMinX || pDef->pMovableOptions->fMovableDefaultX > pDef->pMovableOptions->fMovableMaxX || (pDef->pMovableOptions->bMovableCanEditPosition && pDef->pMovableOptions->fMovableMaxX == pDef->pMovableOptions->fMovableMinX))
			{
				ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' has invalid values for MovableX min/max/default\n", pDef->pcName);
			}
			if (pDef->pMovableOptions->fMovableMaxY < pDef->pMovableOptions->fMovableMinY || pDef->pMovableOptions->fMovableDefaultY < pDef->pMovableOptions->fMovableMinY || pDef->pMovableOptions->fMovableDefaultY > pDef->pMovableOptions->fMovableMaxY || (pDef->pMovableOptions->bMovableCanEditPosition && pDef->pMovableOptions->fMovableMaxY == pDef->pMovableOptions->fMovableMinY))
			{
				ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' has invalid values for MovableY min/max/default\n", pDef->pcName);
			}
			if (pDef->pMovableOptions->fMovableMaxScaleX < pDef->pMovableOptions->fMovableMinScaleX || pDef->pMovableOptions->fMovableDefaultScaleX < pDef->pMovableOptions->fMovableMinScaleX || pDef->pMovableOptions->fMovableDefaultScaleX > pDef->pMovableOptions->fMovableMaxScaleX || (pDef->pMovableOptions->bMovableCanEditScale && pDef->pMovableOptions->fMovableMaxScaleX == pDef->pMovableOptions->fMovableMinScaleX))
			{
				ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' has invalid values for MovableScaleX min/max/default\n", pDef->pcName);
			}
			if (pDef->pMovableOptions->fMovableMaxScaleY < pDef->pMovableOptions->fMovableMinScaleY || pDef->pMovableOptions->fMovableDefaultScaleY < pDef->pMovableOptions->fMovableMinScaleY || pDef->pMovableOptions->fMovableDefaultScaleY > pDef->pMovableOptions->fMovableMaxScaleY || (pDef->pMovableOptions->bMovableCanEditScale && pDef->pMovableOptions->fMovableMaxScaleY == pDef->pMovableOptions->fMovableMinScaleY))
			{
				ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' has invalid values for MovableScaleY min/max/default\n", pDef->pcName);
			}
		}
	}
	else if (pDef->pMovableOptions)
	{
		ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' has MovableOptions set but its texture type is not Movable.\n", pDef->pcName);
	}

	if (pDef->uColorSwap0 < 0 || pDef->uColorSwap0 > 3 || pDef->uColorSwap1 < 0 || pDef->uColorSwap1 > 3 || 
		pDef->uColorSwap2 < 0 || pDef->uColorSwap2 > 3 || pDef->uColorSwap3 < 0 || pDef->uColorSwap3 > 3)
	{
		ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' refers to a color swap that is out of range (0-3)\n",pDef->pcName);
	}
	//if (pDef->uColorSwap0 == pDef->uColorSwap1 || pDef->uColorSwap0 == pDef->uColorSwap2 || pDef->uColorSwap0 == pDef->uColorSwap3 ||
	//	pDef->uColorSwap1 == pDef->uColorSwap2 || pDef->uColorSwap1 == pDef->uColorSwap3 || pDef->uColorSwap2 == pDef->uColorSwap3)
	//{
	//	ErrorFilenamef(pDef->pcFileName,"Costume texture '%s' refers to color swaps with the same value\n",pDef->pcName);
	//}
}


static void costumeLoad_ValidateTextures()
{
	DictionaryEArrayStruct *pTexs = resDictGetEArrayStruct("CostumeTexture");
	int i;

	for(i=eaSize(&pTexs->ppReferents)-1; i>=0; --i) {
		costumeLoad_ValidateTexture(pTexs->ppReferents[i]);
	}
}


static void costumeLoad_ValidateMaterial(PCMaterialDef *pDef)
{
	int i,j,k;
	bool bFoundPattern, bFoundDetail, bFoundSpecular, bFoundDiffuse, bFoundMovable;
	Material *pMaterial;

	if (!resIsValidName(pDef->pcName)) {
		ErrorFilenamef(pDef->pcFileName,"Costume material '%s' does not have a valid name\n",pDef->pcName);
	}
	if (!pDef->pcMaterial) {
		ErrorFilenamef(pDef->pcFileName,"Costume material '%s' missing the material name\n",pDef->pcName);
	}
	if (!GET_REF(pDef->displayNameMsg.hMessage)) {
		ErrorFilenamef(pDef->pcFileName,"Costume material '%s' missing the display name message\n",pDef->pcName);
	}
	if (GET_REF(pDef->hDefaultPattern)) {
		U8 eRestriction = (pDef->eRestriction & NOT_UGC_RESTRICTIONS);
		bFoundPattern = false;
		if ((eRestriction & GET_REF(pDef->hDefaultPattern)->eRestriction) != eRestriction) {
			ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to default texture def '%s', but that texture is restricted more than the material is.  The default texture cannot be more restricted than the material it is on.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultPattern));
		}
	} else {
		if (REF_STRING_FROM_HANDLE(pDef->hDefaultPattern)) {
			ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to non-existent texture def '%s'.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultPattern));
		}
		bFoundPattern = true;
	}
	if (GET_REF(pDef->hDefaultDetail)) {
		U8 eRestriction = (pDef->eRestriction & NOT_UGC_RESTRICTIONS);
		bFoundDetail = false;
		if ((eRestriction & GET_REF(pDef->hDefaultDetail)->eRestriction) != eRestriction) {
			ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to default texture def '%s', but that texture is restricted more than the material is.  The default texture cannot be more restricted than the material it is on.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultDetail));
		}
	} else {
		if (REF_STRING_FROM_HANDLE(pDef->hDefaultDetail)) {
			ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to non-existent texture def '%s'.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultDetail));
		}
		bFoundDetail = true;
	}
	if (GET_REF(pDef->hDefaultSpecular)) {
		U8 eRestriction = (pDef->eRestriction & NOT_UGC_RESTRICTIONS);
		bFoundSpecular = false;
		if ((eRestriction & GET_REF(pDef->hDefaultSpecular)->eRestriction) != eRestriction) {
			ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to default texture def '%s', but that texture is restricted more than the material is.  The default texture cannot be more restricted than the material it is on.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultSpecular));
		}
	} else {
		if (REF_STRING_FROM_HANDLE(pDef->hDefaultSpecular)) {
			ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to non-existent texture def '%s'.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultSpecular));
		}
		bFoundSpecular = true;
	}
	if (GET_REF(pDef->hDefaultDiffuse)) {
		U8 eRestriction = (pDef->eRestriction & NOT_UGC_RESTRICTIONS);
		bFoundDiffuse = false;
		if ((eRestriction & GET_REF(pDef->hDefaultDiffuse)->eRestriction) != eRestriction) {
			ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to default texture def '%s', but that texture is restricted more than the material is.  The default texture cannot be more restricted than the material it is on.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultDiffuse));
		}
	} else {
		if (REF_STRING_FROM_HANDLE(pDef->hDefaultDiffuse)) {
			ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to non-existent texture def '%s'.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultDiffuse));
		}
		bFoundDiffuse = true;
	}
	if (GET_REF(pDef->hDefaultMovable)) {
		U8 eRestriction = (pDef->eRestriction & NOT_UGC_RESTRICTIONS);
		bFoundMovable = false;
		if ((eRestriction & GET_REF(pDef->hDefaultMovable)->eRestriction) != eRestriction) {
			ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to default texture def '%s', but that texture is restricted more than the material is.  The default texture cannot be more restricted than the material it is on.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultMovable));
		}
	} else {
		if (REF_STRING_FROM_HANDLE(pDef->hDefaultMovable)) {
			ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to non-existent texture def '%s'.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultMovable));
		}
		bFoundMovable = true;
	}

	for(j=eaSize(&pDef->eaAllowedTextureDefs)-1; j>=0; --j) {
		PCTextureDef *pTexture = (PCTextureDef*)RefSystem_ReferentFromString(g_hCostumeTextureDict, pDef->eaAllowedTextureDefs[j]);
		if (!pTexture) {
			ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to non-existent texture def '%s'\n",pDef->pcName,pDef->eaAllowedTextureDefs[j]);
		} else {
			if (!bFoundPattern && stricmp(pDef->eaAllowedTextureDefs[j], REF_STRING_FROM_HANDLE(pDef->hDefaultPattern)) == 0) {
				bFoundPattern = true;
			}
			if (!bFoundDetail && stricmp(pDef->eaAllowedTextureDefs[j], REF_STRING_FROM_HANDLE(pDef->hDefaultDetail)) == 0) {
				bFoundDetail = true;
			}
			if (!bFoundSpecular && stricmp(pDef->eaAllowedTextureDefs[j], REF_STRING_FROM_HANDLE(pDef->hDefaultSpecular)) == 0) {
				bFoundSpecular = true;
			}
			if (!bFoundDiffuse && stricmp(pDef->eaAllowedTextureDefs[j], REF_STRING_FROM_HANDLE(pDef->hDefaultDiffuse)) == 0) {
				bFoundDiffuse = true;
			}
			if (!bFoundMovable && stricmp(pDef->eaAllowedTextureDefs[j], REF_STRING_FROM_HANDLE(pDef->hDefaultMovable)) == 0) {
				bFoundMovable = true;
			}

			if (pTexture->pValueOptions && pTexture->pValueOptions->pcValueConstant)
			{
				if (!costumeTailor_IsSliderConstValid(pTexture->pValueOptions->pcValueConstant, pDef, pTexture->pValueOptions->iValConstIndex))
				{
					ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to texture def '%s' which has an invalid or nonexistant material constant '%s' or invalid constant-index\n",pDef->pcName,pTexture->pcName,pTexture->pValueOptions->pcValueConstant);
				}
			}
		}
	}
	if (!bFoundPattern) {
		ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to default texture def '%s' which is not allowed on this material\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultPattern));
	}
	if (!bFoundDetail) {
		ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to default texture def '%s' which is not allowed on this material\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultDetail));
	}
	if (!bFoundSpecular) {
		ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to default texture def '%s' which is not allowed on this material\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultSpecular));
	}
	if (!bFoundDiffuse) {
		ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to default texture def '%s' which is not allowed on this material\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultDiffuse));
	}
	if (!bFoundMovable) {
		ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to default texture def '%s' which is not allowed on this material\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultMovable));
	}

	// Make sure the material file really exists
	if (pDef->pcMaterial && (stricmp("None", pDef->pcMaterial) != 0)) {
		pMaterial = materialFindNoDefault(pDef->pcMaterial, 0);
		if (!pMaterial) {
			ErrorFilenamef(pDef->pcFileName,"Costume material '%s' refers to non-existent graphics material '%s'\n",pDef->pcName,pDef->pcMaterial);
		} else {
			StashTable pStash;
			StashElement pElement;
			StashTableIterator pIter;
			char **eaTexNames = NULL;

			// Get the texture names off the material
			pStash = stashTableCreateWithStringKeys(10, StashDefault);
			materialGetTextureNames(pMaterial, pStash, NULL);
			stashGetIterator(pStash, &pIter);
			while(stashGetNextElement(&pIter, &pElement)) {
				eaPush(&eaTexNames, stashElementGetStringKey(pElement));
			}

			// Check all legal textures to make sure they only use valid texture swaps
			for(i=eaSize(&pDef->eaAllowedTextureDefs)-1; i>=0; --i) {
				PCTextureDef *pTexture = (PCTextureDef*)RefSystem_ReferentFromString(g_hCostumeTextureDict, pDef->eaAllowedTextureDefs[i]);
				if(pTexture)
				{
					// Check base swap
					for(j=eaSize(&eaTexNames)-1; j>=0; --j) {
						if (stricmp(pTexture->pcOrigTexture, eaTexNames[j]) == 0) {
							break;
						}
					}
					if (j < 0) {
						ErrorFilenamef(pTexture->pcFileName,"Costume texture '%s' refers to non-existent replacement texture '%s' on graphics material '%s' as used by costume material '%s'\n",pTexture->pcName,pTexture->pcOrigTexture,pDef->pcMaterial,pDef->pcName);
					}

					// check extra swaps
					for(k=eaSize(&pTexture->eaExtraSwaps)-1; k>=0; --k) {
						for(j=eaSize(&eaTexNames)-1; j>=0; --j) {
							if (stricmp(pTexture->eaExtraSwaps[k]->pcOrigTexture, eaTexNames[j]) == 0) {
								break;
							}
						}
						if (j < 0) {
							ErrorFilenamef(pTexture->pcFileName,"Costume texture '%s' refers to non-existent replacement texture '%s' on graphics material '%s' as used by costume material '%s'\n",pTexture->pcName,pTexture->eaExtraSwaps[k]->pcOrigTexture,pDef->pcMaterial,pDef->pcName);
						}
					}
				}
			}

			eaDestroy(&eaTexNames);
			stashTableDestroy(pStash);
		}
	}
}


static void costumeLoad_ValidateMaterials(void)
{
	DictionaryEArrayStruct *pMaterials = resDictGetEArrayStruct("CostumeMaterial");
	int i;

	for(i=eaSize(&pMaterials->ppReferents)-1; i>=0; --i) {
		costumeLoad_ValidateMaterial(pMaterials->ppReferents[i]);
	}
}


static void costumeLoad_ValidateMaterialAdd(PCMaterialAdd *pDef)
{
	int j;

	if (!resIsValidName(pDef->pcName)) {
		ErrorFilenamef(pDef->pcFileName,"Costume material add '%s' does not have a valid name\n",pDef->pcName);
	}
	if (!RefSystem_ReferentFromString(g_hCostumeMaterialDict, pDef->pcMatName)) {
		ErrorFilenamef(pDef->pcFileName,"Costume material add '%s' refers to non-existent material def '%s'\n",pDef->pcName, pDef->pcMatName);
	}
	for(j=eaSize(&pDef->eaAllowedTextureDefs)-1; j>=0; --j) {
		PCTextureDef *pTexture = (PCTextureDef*)RefSystem_ReferentFromString(g_hCostumeTextureDict, pDef->eaAllowedTextureDefs[j]);
		if (!pTexture) {
			ErrorFilenamef(pDef->pcFileName,"Costume material add '%s' refers to non-existent texture def '%s'\n",pDef->pcName,pDef->eaAllowedTextureDefs[j]);
		}
	}
}


static void costumeLoad_ValidateMaterialAdds()
{
	DictionaryEArrayStruct *pMatAdds = resDictGetEArrayStruct("CostumeMaterialAdd");
	int i;

	for(i=eaSize(&pMatAdds->ppReferents)-1; i>=0; --i) {
		costumeLoad_ValidateMaterialAdd(pMatAdds->ppReferents[i]);
	}
}


static void costumeLoad_ValidateGeometry(PCGeometryDef *pDef)
{
	int j,k;
	PCBoneDef *pBone;
	PCMaterialDef *pMaterial;
	bool bFoundDefault;
	PCColorQuadSet *pQuadSet;
	UIColorSet *pBodyColor[4];

	if (!resIsValidName(pDef->pcName)) {
		ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' does not have a valid name\n",pDef->pcName);
	}
	//We want to allow a NULL geometry. It will just not be displayed.
	//"We need to allow certain parts, like antenna, to be required on some species (andorian) but allow a none option on others (human)"
	//"We need to require that bone have a geo and then make a geo with nothing in it"
	//if (!pDef->pcGeometry) {
	//	ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' missing the geometry name\n",pDef->pcName);
	//}
	if (!GET_REF(pDef->displayNameMsg.hMessage)) {
		ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' missing the display name message\n",pDef->pcName);
	}
	pBone = GET_REF(pDef->hBone);
	if (!pBone && REF_STRING_FROM_HANDLE(pDef->hBone)) {
		ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to non-existent bone def '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hBone));
	}
	if (GET_REF(pDef->hDefaultMaterial)) {
		U8 eRestriction = (pDef->eRestriction & NOT_UGC_RESTRICTIONS);
		bFoundDefault = false;
		if ((eRestriction & GET_REF(pDef->hDefaultMaterial)->eRestriction) != eRestriction) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to default material def '%s', but that material is restricted more than the geometry is.  The default material cannot be more restricted than the geometry it is on.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultMaterial));
		}
	} else {
		if (REF_STRING_FROM_HANDLE(pDef->hDefaultMaterial)) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to non-existent material def '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultMaterial));
		}
		bFoundDefault = true;
	}

	for(j=eaSize(&pDef->eaStyles)-1; j>=0; --j) {
		PCStyle *pStyle = RefSystem_ReferentFromString(g_hCostumeStyleDict, pDef->eaStyles[j]);
		if (!pStyle) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to non-existent style '%s'\n",pDef->pcName,pDef->eaStyles[j]);
		}
	}

	for(j=eaSize(&pDef->eaCategories)-1; j>=0; --j) {
		PCCategory *pCat = GET_REF(pDef->eaCategories[j]->hCategory);
		if (!pCat && REF_STRING_FROM_HANDLE(pDef->eaCategories[j]->hCategory)) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to non-existent category '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->eaCategories[j]->hCategory));
		} else if (pBone) {
			PCRegion *pRegion = GET_REF(pBone->hRegion);
			if (pRegion) {
				bool bFound = false;
				for(k=eaSize(&pRegion->eaCategories)-1; k>=0; --k) {
					if (GET_REF(pRegion->eaCategories[k]->hCategory) == pCat) {
						bFound = true;
						break;
					}
				}
				if (!bFound) {
					ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to category '%s' which is not legal for its bone\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->eaCategories[j]->hCategory));
				}
			}
		}
	}

	for(j=eaSize(&pDef->eaAllowedMaterialDefs)-1; j>=0; --j) {
		if (!pDef->eaAllowedMaterialDefs[j]) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to a material def with no name\n",pDef->pcName);
		} else if (stricmp("None",pDef->eaAllowedMaterialDefs[j]) != 0) {
			pMaterial = (PCMaterialDef*)RefSystem_ReferentFromString(g_hCostumeMaterialDict, pDef->eaAllowedMaterialDefs[j]);
			if (!pMaterial) {
				ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to non-existent material def '%s'\n",pDef->pcName,pDef->eaAllowedMaterialDefs[j]);
			} else if (!bFoundDefault && (pMaterial == GET_REF(pDef->hDefaultMaterial))) {
				bFoundDefault = true;
			}
		}
	}
	if (!bFoundDefault) {
		ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to default material def '%s' which is not allowed\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultMaterial));
	}

	if (pDef->pcGeometry && (stricmp(pDef->pcGeometry, "None") != 0)) {
		// Make sure the geometry file really exists
		ModelHeaderSet *pModelHeaderSet;
		pModelHeaderSet = modelHeaderSetFind(pDef->pcGeometry);
		if (!pModelHeaderSet && (!pDef->pcModel || !strStartsWith(pDef->pcGeometry, "object_library"))) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to non-existent geo file '%s'\n", pDef->pcName,pDef->pcGeometry);
		} else if (pDef->pcModel && !wlModelHeaderFromNameEx(pDef->pcGeometry, pDef->pcModel)) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to non-existent model '%s' in geo file '%s'\n", pDef->pcName,pDef->pcModel, pDef->pcGeometry);
		}
	}

	if (pDef->pClothData && pDef->pClothData->bIsCloth) {
		if (!pDef->pClothData->pcClothInfo) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' is cloth but has no cloth info\n", pDef->pcName);
		} else if (!RefSystem_ReferentFromString("DynClothInfo", pDef->pClothData->pcClothInfo)) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' references non-existent cloth info '%s'\n", pDef->pcName, pDef->pClothData->pcClothInfo);
		}
		if (!pDef->pClothData->pcClothColInfo) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' is cloth but has no cloth collision info\n", pDef->pcName);
		} else if (!RefSystem_ReferentFromString("DynClothCollision", pDef->pClothData->pcClothColInfo)) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' references non-existent cloth collision info '%s'\n", pDef->pcName, pDef->pClothData->pcClothColInfo);
		}
	}

	if (pDef->pOptions)
	{
		pBodyColor[0] = GET_REF(pDef->pOptions->hBodyColorSet0);
		if (!pBodyColor[0] && REF_STRING_FROM_HANDLE(pDef->pOptions->hBodyColorSet0)) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->pOptions->hBodyColorSet0));
		}
		pBodyColor[1] = GET_REF(pDef->pOptions->hBodyColorSet1);
		if (!pBodyColor[1] && REF_STRING_FROM_HANDLE(pDef->pOptions->hBodyColorSet1)) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->pOptions->hBodyColorSet1));
		}
		pBodyColor[2] = GET_REF(pDef->pOptions->hBodyColorSet2);
		if (!pBodyColor[2] && REF_STRING_FROM_HANDLE(pDef->pOptions->hBodyColorSet2)) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->pOptions->hBodyColorSet2));
		}
		pBodyColor[3] = GET_REF(pDef->pOptions->hBodyColorSet3);
		if (!pBodyColor[3] && REF_STRING_FROM_HANDLE(pDef->pOptions->hBodyColorSet3)) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->pOptions->hBodyColorSet3));
		}
		pQuadSet = GET_REF(pDef->pOptions->hColorQuadSet);
		if (!pQuadSet && REF_STRING_FROM_HANDLE(pDef->pOptions->hColorQuadSet)) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to non-existent color quad set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->pOptions->hColorQuadSet));
		}
	}

	// Validate child geo references
	if (pDef->pOptions)
	{
		for(j=eaSize(&pDef->pOptions->eaChildGeos)-1; j>=0; --j) {
			PCGeometryChildDef *pChildDef = pDef->pOptions->eaChildGeos[j];

			if (REF_STRING_FROM_HANDLE(pChildDef->hDefaultChildGeo) && !GET_REF(pChildDef->hDefaultChildGeo)) {
				ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to non-existent child geometry '%s'\n", pDef->pcName, REF_STRING_FROM_HANDLE(pChildDef->hDefaultChildGeo));
			}
			if (pBone && eaSize(&pChildDef->eaChildGeometries) && !GET_REF(pChildDef->hChildBone)) {
				ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to child geometries but bone '%s' has no child bone defined\n", pDef->pcName, pBone->pcName);
			}
			for(k=eaSize(&pChildDef->eaChildGeometries)-1; k>=0; --k) {
				PCGeometryDef *pChildGeo = GET_REF(pChildDef->eaChildGeometries[k]->hGeo);
				if (!pChildGeo && REF_STRING_FROM_HANDLE(pChildDef->eaChildGeometries[k]->hGeo)) {
					ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to non-existent child geometry '%s'\n", pDef->pcName, REF_STRING_FROM_HANDLE(pChildDef->eaChildGeometries[k]->hGeo));
				}
				if (pChildGeo && pBone && (GET_REF(pChildDef->hChildBone) != GET_REF(pChildGeo->hBone))) { 
					ErrorFilenamef(pDef->pcFileName,"Costume geometry '%s' refers to child geometry '%s' which is not on the expected bone\n", pDef->pcName, REF_STRING_FROM_HANDLE(pChildDef->eaChildGeometries[k]->hGeo));
				}
			}
		}
	}
}


static void costumeLoad_ValidateGeometries(void)
{
	DictionaryEArrayStruct *pGeometries = resDictGetEArrayStruct("CostumeGeometry");
	int i;

	for(i=eaSize(&pGeometries->ppReferents)-1; i>=0; --i) {
		costumeLoad_ValidateGeometry(pGeometries->ppReferents[i]);
	}
}


static void costumeLoad_ValidateGeometryAdd(PCGeometryAdd *pDef)
{
	int j;
	PCMaterialDef *pMaterial;

	if (!resIsValidName(pDef->pcName)) {
		ErrorFilenamef(pDef->pcFileName,"Costume geometry add '%s' does not have a valid name\n",pDef->pcName);
	}
	if (!RefSystem_ReferentFromString(g_hCostumeGeometryDict, pDef->pcGeoName)) {
		ErrorFilenamef(pDef->pcFileName,"Costume geometry add '%s' refers to non-existent geometry def '%s'\n",pDef->pcName,pDef->pcGeoName);
	}
	for(j=eaSize(&pDef->eaAllowedMaterialDefs)-1; j>=0; --j) {
		if (!pDef->eaAllowedMaterialDefs[j]) {
			ErrorFilenamef(pDef->pcFileName,"Costume geometry add '%s' refers to material with no name\n",pDef->pcGeoName);
		} else {
			pMaterial = (PCMaterialDef*)RefSystem_ReferentFromString(g_hCostumeMaterialDict, pDef->eaAllowedMaterialDefs[j]);
			if (!pMaterial) {
				ErrorFilenamef(pDef->pcFileName,"Costume geometry add '%s' refers to non-existent material def '%s'\n",pDef->pcName,pDef->eaAllowedMaterialDefs[j]);
			}
		}
	}
}


static void costumeLoad_ValidateGeometryAdds()
{
	DictionaryEArrayStruct *pGeoAdds = resDictGetEArrayStruct("CostumeGeometryAdd");
	int i;

	for(i=eaSize(&pGeoAdds->ppReferents)-1; i>=0; --i) {
		costumeLoad_ValidateGeometryAdd(pGeoAdds->ppReferents[i]);
	}
}


static void costumeLoad_ValidateBone(PCBoneDef *pDef)
{
	PCColorQuadSet *pQuadSet;
	UIColorSet *pBodyColor[4];
	PCBoneDef *pBone;
	int i, j;

	if (!resIsValidName(pDef->pcName)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' does not have a valid name\n",pDef->pcName);
	}
	if (!pDef->pcBoneName) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' missing the bone name\n",pDef->pcName);
	}
	if (GET_REF(pDef->displayNameMsg.hMessage) && !REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' missing the display name message\n",pDef->pcName);
	}

	if (REF_STRING_FROM_HANDLE(pDef->geometryFieldDispName.hMessage) && !GET_REF(pDef->geometryFieldDispName.hMessage)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent layer '%s'\n", pDef->pcName, REF_STRING_FROM_HANDLE(pDef->geometryFieldDispName.hMessage));
	}
	if (REF_STRING_FROM_HANDLE(pDef->materialFieldDispName.hMessage) && !GET_REF(pDef->materialFieldDispName.hMessage)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent layer '%s'\n", pDef->pcName, REF_STRING_FROM_HANDLE(pDef->materialFieldDispName.hMessage));
	}
	if (REF_STRING_FROM_HANDLE(pDef->patternFieldDispName.hMessage) && !GET_REF(pDef->patternFieldDispName.hMessage)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent layer '%s'\n", pDef->pcName, REF_STRING_FROM_HANDLE(pDef->patternFieldDispName.hMessage));
	}
	if (REF_STRING_FROM_HANDLE(pDef->detailFieldDisplayName.hMessage) && !GET_REF(pDef->detailFieldDisplayName.hMessage)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent layer '%s'\n", pDef->pcName, REF_STRING_FROM_HANDLE(pDef->detailFieldDisplayName.hMessage));
	}
	if (REF_STRING_FROM_HANDLE(pDef->specularFieldDisplayName.hMessage) && !GET_REF(pDef->specularFieldDisplayName.hMessage)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent layer '%s'\n", pDef->pcName, REF_STRING_FROM_HANDLE(pDef->specularFieldDisplayName.hMessage));
	}
	if (REF_STRING_FROM_HANDLE(pDef->diffuseFieldDisplayName.hMessage) && !GET_REF(pDef->diffuseFieldDisplayName.hMessage)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent layer '%s'\n", pDef->pcName, REF_STRING_FROM_HANDLE(pDef->diffuseFieldDisplayName.hMessage));
	}
	if (REF_STRING_FROM_HANDLE(pDef->movableFieldDisplayName.hMessage) && !GET_REF(pDef->movableFieldDisplayName.hMessage)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent layer '%s'\n", pDef->pcName, REF_STRING_FROM_HANDLE(pDef->movableFieldDisplayName.hMessage));
	}

	// Mirror Fields
	if (REF_STRING_FROM_HANDLE(pDef->hMirrorBone)) {
		pBone = GET_REF(pDef->hMirrorBone);
		if (!pBone) {
			ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent mirror bone def '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hMirrorBone));
		} else {
			if (GET_REF(pBone->hMirrorBone) != pDef) {
				ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to mirror bone def '%s' but that bone does not refer to this one as a mirror\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hMirrorBone));
			}
			if (GET_REF(pBone->hMergeLayer) != GET_REF(pDef->hMergeLayer)) {
				ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to mirror bone def '%s' but that bone does not have the same merge layer defined\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hMirrorBone));
			}
			if (GET_REF(pBone->hSelfLayer) == GET_REF(pDef->hSelfLayer)) {
				ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to mirror bone def '%s' but that bone has the same self layer defined (they must be different)\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hMirrorBone));
			}
		}
	}
	if (REF_STRING_FROM_HANDLE(pDef->hSelfLayer)) {
		PCLayer *pLayer = GET_REF(pDef->hSelfLayer);
		if (!pLayer) {
			ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent layer def '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hSelfLayer));
		}
	} else if (GET_REF(pDef->hMirrorBone)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' has a mirror bone but is missing the self layer definition\n",pDef->pcName);
	}
	if (REF_STRING_FROM_HANDLE(pDef->hMergeLayer)) {
		PCLayer *pLayer = GET_REF(pDef->hMergeLayer);
		if (!pLayer) {
			ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent layer def '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hMergeLayer));
		}
	} else if (GET_REF(pDef->hMirrorBone)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' has a mirror bone but is missing the merge layer definition\n",pDef->pcName);
	}
	if (REF_STRING_FROM_HANDLE(pDef->mergeNameMsg.hMessage)) {
		if (!GET_REF(pDef->mergeNameMsg.hMessage)) {
			ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent display message '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->mergeNameMsg.hMessage));
		}
	} else if (GET_REF(pDef->hMirrorBone)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' has a mirror bone but is missing the merge display name message\n",pDef->pcName);
	}

	// Child fields
	for(i=eaSize(&pDef->eaChildBones)-1; i>=0; --i) {
		PCChildBone *pChildBone = pDef->eaChildBones[i];
		PCLayer *pFrontLayer = GET_REF(pChildBone->hChildLayerFront);
		PCLayer *pBackLayer = GET_REF(pChildBone->hChildLayerBack);

		if (REF_STRING_FROM_HANDLE(pChildBone->hChildBone)) {
			pBone = GET_REF(pChildBone->hChildBone);
			if (!pBone) {
				ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent child bone def '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pChildBone->hChildBone));
			} else if (!pBone->bIsChildBone) {
				ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to child bone def '%s', but that bone is not marked as a child bone\n",pDef->pcName,REF_STRING_FROM_HANDLE(pChildBone->hChildBone));
			}
		}
		if (REF_STRING_FROM_HANDLE(pChildBone->hChildLayerFront) && !GET_REF(pChildBone->hChildLayerFront)) {
			ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent layer '%s'\n", pDef->pcName, REF_STRING_FROM_HANDLE(pChildBone->hChildLayerFront));
		}
		if (REF_STRING_FROM_HANDLE(pChildBone->hChildLayerBack) && !GET_REF(pChildBone->hChildLayerBack)) {
			ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent layer '%s'\n", pDef->pcName, REF_STRING_FROM_HANDLE(pChildBone->hChildLayerBack));
		}

		// If more than one child, then all children must have unique layers
		if (eaSize(&pDef->eaChildBones) > 1) {
			if (!GET_REF(pChildBone->hChildLayerFront)) {
				ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' has a child bone that does not have its front layer defined.  A front layer must be defined if there is more than one child bone.\n", pDef->pcName);
			}
			for(j=i-1; j>=0; --j) {
				PCLayer *pOtherFront = GET_REF(pDef->eaChildBones[j]->hChildLayerFront);
				PCLayer *pOtherBack = GET_REF(pDef->eaChildBones[j]->hChildLayerBack);
				if (!pOtherFront) {
					pOtherFront = RefSystem_ReferentFromString(g_hCostumeLayerDict, "Child_Front");
				}
				if (!pOtherBack) {
					pOtherBack = RefSystem_ReferentFromString(g_hCostumeLayerDict, "Child_Back");
				}
				if (pFrontLayer && pOtherFront && (pFrontLayer == pOtherFront)) {
					ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' uses layer '%s' for more than one child bone definition.  (note that not declaring a front layer defaults it to 'Child_Front')\n", pDef->pcName, pFrontLayer->pcName);
				} else if (pFrontLayer && pOtherBack && (pFrontLayer == pOtherBack)) {
					ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' uses layer '%s' for more than one child bone definition.  (note that not declaring a front layer defaults it to 'Child_Front')\n", pDef->pcName, pFrontLayer->pcName);
				} else if (pBackLayer && pOtherFront && (pBackLayer == pOtherFront)) {
					ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' uses layer '%s' for more than one child bone definition.  (note that not declaring a back layer defaults it to 'Child_Back')\n", pDef->pcName, pBackLayer->pcName);
				} else if (pBackLayer && pOtherBack && (pBackLayer == pOtherBack)) {
					ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' uses layer '%s' for more than one child bone definition.  (note that not declaring a back layer defaults it to 'Child_Back')\n", pDef->pcName, pBackLayer->pcName);
				}
			}
		}
	}
	if (REF_STRING_FROM_HANDLE(pDef->hMainLayerFront) && !GET_REF(pDef->hMainLayerFront)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent layer '%s'\n", pDef->pcName, REF_STRING_FROM_HANDLE(pDef->hMainLayerFront));
	}
	if (REF_STRING_FROM_HANDLE(pDef->hMainLayerBack) && !GET_REF(pDef->hMainLayerBack)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent layer '%s'\n", pDef->pcName, REF_STRING_FROM_HANDLE(pDef->hMainLayerBack));
	}
	if (pDef->bIsChildBone) {
		DictionaryEArrayStruct *pBoneStruct = resDictGetEArrayStruct("CostumeBone");
		int count = 0;
		for(i=eaSize(&pBoneStruct->ppReferents)-1; i>=0; --i) {
			PCBoneDef *pParentBone = pBoneStruct->ppReferents[i];
			for(j=eaSize(&pParentBone->eaChildBones)-1; j>=0; --j) {
				if (GET_REF(pParentBone->eaChildBones[j]->hChildBone) == pDef) {
					++count;
				}
			}
		}
		if (count == 0) {
			ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' is a child bone with no parent bone defined\n", pDef->pcName);
		} else if (count > 1) {
			ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' is a child bone that has more than one parent bone defined\n", pDef->pcName);
		}
		if (!GET_REF(pDef->hMainLayerBoth)) {
			ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' is a child bone but does not have a valid main both layer\n", pDef->pcName);
		}
		if (!GET_REF(pDef->hMainLayerFront)) {
			ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' is a child bone but does not have a valid main front layer\n", pDef->pcName);
		}
		if (!GET_REF(pDef->hMainLayerBack)) {
			ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' is a child bone but does not have a valid main back layer\n", pDef->pcName);
		}
	}

	// Default Geo
	if (REF_STRING_FROM_HANDLE(pDef->hDefaultGeo)) {
		PCGeometryDef *pGeo = GET_REF(pDef->hDefaultGeo);
		if (!pGeo) {
			ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent default geometry def '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultGeo));
		} else {
			if (GET_REF(pGeo->hBone) != pDef) {
				ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to default geometry def '%s' which is not legal on this bone\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultGeo));
			}
			if ((pDef->eRestriction & pGeo->eRestriction) != pDef->eRestriction) {
				ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to default geometry def '%s' which is restricted more than the bone is.  Default geometries must be allowed on all costume types that the bone is allowed on.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultGeo));
			}
		}
	}

	// Region
	if (!REF_STRING_FROM_HANDLE(pDef->hRegion)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' missing the region name\n",pDef->pcName);
	} else {
		PCRegion *pReg = GET_REF(pDef->hRegion);
		if (!pReg && REF_STRING_FROM_HANDLE(pDef->hRegion)) {
			ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent region '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hRegion));
		}
	}

	// Colors
	pBodyColor[0] = GET_REF(pDef->hBodyColorSet0);
	if (!pBodyColor[0] && REF_STRING_FROM_HANDLE(pDef->hBodyColorSet0)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hBodyColorSet0));
	}
	pBodyColor[1] = GET_REF(pDef->hBodyColorSet1);
	if (!pBodyColor[1] && REF_STRING_FROM_HANDLE(pDef->hBodyColorSet1)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hBodyColorSet1));
	}
	pBodyColor[2] = GET_REF(pDef->hBodyColorSet2);
	if (!pBodyColor[2] && REF_STRING_FROM_HANDLE(pDef->hBodyColorSet2)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hBodyColorSet2));
	}
	pBodyColor[3] = GET_REF(pDef->hBodyColorSet3);
	if (!pBodyColor[3] && REF_STRING_FROM_HANDLE(pDef->hBodyColorSet3)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hBodyColorSet3));
	}
	pQuadSet = GET_REF(pDef->hColorQuadSet);
	if (!pQuadSet && REF_STRING_FROM_HANDLE(pDef->hColorQuadSet)) {
		ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' refers to non-existent color quad set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hColorQuadSet));
	}

	// check all quad color to make sure they exist in skeleton
	if(pQuadSet && (pDef->eRestriction & (kPCRestriction_Player|kPCRestriction_Player_Initial)) != 0)
	{
		for(i = 0; i < eaSize(&pQuadSet->eaColorQuads); ++i)
		{
			if(!costumeLoad_ColorInSets(pQuadSet->eaColorQuads[i]->color0, pBodyColor[0], NULL) ||
				!costumeLoad_ColorInSets(pQuadSet->eaColorQuads[i]->color1, pBodyColor[1], NULL) ||
				!costumeLoad_ColorInSets(pQuadSet->eaColorQuads[i]->color2, pBodyColor[2], NULL) ||
				!costumeLoad_ColorInSets(pQuadSet->eaColorQuads[i]->color3, pBodyColor[3], NULL))
			{
				ErrorFilenamef(pDef->pcFileName,"Costume bone '%s' has color quad set '%s' that doesn't match body color\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hColorQuadSet));
			}
		}
	}
}


static void costumeLoad_ValidateBones()
{
	DictionaryEArrayStruct *pBones = resDictGetEArrayStruct("CostumeBone");
	int i;

	for(i=eaSize(&pBones->ppReferents)-1; i>=0; --i) {
		costumeLoad_ValidateBone(pBones->ppReferents[i]);
	}
}



static void costumeLoad_ValidateScaleInfo(PCSkeletonDef *pSkel, PCScaleInfo *pScale)
{
	int i = 0;
	if (!pScale->pcName) {
		ErrorFilenamef(pSkel->pcFileName,"Costume skeleton '%s' has an unnamed scale\n", pSkel->pcName);
	}
	if (!pScale->pcDisplayName) {
		ErrorFilenamef(pSkel->pcFileName,"Costume skeleton '%s' scale '%s' is missing its display name\n", pSkel->pcName, pScale->pcName);
	}
	if (!GET_REF(pScale->displayNameMsg.hMessage) && REF_STRING_FROM_HANDLE(pScale->displayNameMsg.hMessage))  {
		ErrorFilenamef(pSkel->pcFileName,"Costume skeleton '%s' scale '%s' refers to non-existent message '%s'\n", pSkel->pcName, pScale->pcName, REF_STRING_FROM_HANDLE(pScale->displayNameMsg.hMessage));
	}

	if ((pSkel->eRestriction & (kPCRestriction_Player|kPCRestriction_Player_Initial)) &&
		(pScale->eRestriction & (kPCRestriction_Player|kPCRestriction_Player_Initial))) {
		if ((pScale->fPlayerMin == 0) && (pScale->fPlayerMax == 0)) {
			ErrorFilenamef(pSkel->pcFileName,"Costume skeleton '%s' scale '%s' is player usable so either min or max value (or both) must be non-zero\n", pSkel->pcName, pScale->pcName);
		}
		if (!GET_REF(pScale->displayNameMsg.hMessage)) {
			ErrorFilenamef(pSkel->pcFileName,"Costume skeleton '%s' scale '%s' is player usable so it must have a displayNameMsg field\n", pSkel->pcName, pScale->pcName);
		}
	}
	for (i = 0; i < eaSize(&pScale->eaScaleEntries); i++)
	{
		if (pScale->eaScaleEntries[i]->iIndex < 0 || pScale->eaScaleEntries[i]->iIndex > 2)
		{
			ErrorFilenamef(pSkel->pcFileName,"Costume skeleton '%s' scale '%s' is outside of the range [0...2]! The value will be clamped for now to prevent horrible things.\n", pSkel->pcName, pScale->pcName);
			pScale->eaScaleEntries[i]->iIndex = CLAMP(pScale->eaScaleEntries[i]->iIndex, 0, 2);
		}
	}
}


static void costumeLoad_ValidateSkeleton(PCSkeletonDef *pDef)
{
	int i,j,k,n,m;
	PCBoneDef *pBone;
	bool bFound;
	SkelInfo* pSkelInfo;
	PCColorQuadSet *pQuadSet;
	UIColorSet *pSkinColor;
	UIColorSet *pBodyColor[4];

	if (!resIsValidName(pDef->pcName)) {
		ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' does not have a valid name\n",pDef->pcName);
	}
	if (!pDef->pcSkeleton) {
		ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' missing the skeleton name\n",pDef->pcName);
	}
	if (!GET_REF(pDef->displayNameMsg.hMessage)) {
		ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' missing the display name message\n",pDef->pcName);
	}
	pSkelInfo = RefSystem_ReferentFromString("SkelInfo", pDef->pcSkeleton);
	if (!pSkelInfo) {
		ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' is using non-existent system skeleton '%s'\n",pDef->pcName,pDef->pcSkeleton);
	}
	for(j=eaSize(&pDef->eaRequiredBoneDefs)-1; j>=0; --j) {
		pBone = GET_REF(pDef->eaRequiredBoneDefs[j]->hBone);
		if (!pBone && REF_STRING_FROM_HANDLE(pDef->eaRequiredBoneDefs[j]->hBone)) {
			ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' refers to non-existent bone def '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->eaRequiredBoneDefs[j]->hBone));
		} else if (pBone && GET_REF(pBone->hRegion)) {
			bFound = false;
			for(k=eaSize(&pDef->eaRegions)-1; k>=0; --k) {
				if (GET_REF(pDef->eaRegions[k]->hRegion) == GET_REF(pBone->hRegion)) {
					bFound = true;
					break;
				}
			}
			if (!bFound) {
				ErrorFilenamef(pBone->pcFileName,"Costume bone '%s' is in region '%s' that is not defined for skeleton def '%s'\n",pBone->pcName,REF_STRING_FROM_HANDLE(pBone->hRegion),pDef->pcName);
			}
		}
		if (pBone && pBone->bIsChildBone) {
			ErrorFilenamef(pBone->pcFileName,"Costume bone '%s' is a child bone on skeleton '%s'.  Child bones cannot be required on a skeleton.\n",pBone->pcName,pDef->pcName);
		}
	}
	for(j=eaSize(&pDef->eaOptionalBoneDefs)-1; j>=0; --j) {
		pBone = GET_REF(pDef->eaOptionalBoneDefs[j]->hBone);
		if (!pBone && REF_STRING_FROM_HANDLE(pDef->eaOptionalBoneDefs[j]->hBone)) {
			ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' refers to non-existent bone def '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->eaOptionalBoneDefs[j]->hBone));
		} else if (GET_REF(pBone->hRegion)) {
			bFound = false;
			for(k=eaSize(&pDef->eaRegions)-1; k>=0; --k) {
				if (GET_REF(pDef->eaRegions[k]->hRegion) == GET_REF(pBone->hRegion)) {
					bFound = true;
					break;
				}
			}
			if (!bFound) {
				ErrorFilenamef(pBone->pcFileName,"Costume bone '%s' is in region '%s' that is not defined for skeleton def '%s'\n",pBone->pcName,REF_STRING_FROM_HANDLE(pBone->hRegion),pDef->pcName);
			}
		}
	}
	for(j=eaSize(&pDef->eaBoneGroups)-1; j>=0; --j) {
		PCBoneGroup *pBoneGroup = pDef->eaBoneGroups[j];
		if (!pBoneGroup) continue;
		for(k=eaSize(&pBoneGroup->eaBoneInGroup)-1; k>=0; --k) {
			pBone = GET_REF(pBoneGroup->eaBoneInGroup[k]->hBone);
			if (!pBone && REF_STRING_FROM_HANDLE(pBoneGroup->eaBoneInGroup[k]->hBone)) {
				ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' refers to non-existent bone def '%s' in bone group '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pBoneGroup->eaBoneInGroup[k]->hBone),pBoneGroup->pcName);
			}
		}
		if (!GET_REF(pBoneGroup->displayNameMsg.hMessage) && REF_STRING_FROM_HANDLE(pBoneGroup->displayNameMsg.hMessage)) {
			ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' bone group '%s' refers to non-existent message '%s'\n",pDef->pcName,pBoneGroup->pcName,REF_STRING_FROM_HANDLE(pBoneGroup->displayNameMsg.hMessage));
		}
	}
	for(j=eaSize(&pDef->eaRegions)-1; j>=0; --j) {
		PCRegion *pRegion = GET_REF(pDef->eaRegions[j]->hRegion);
		if (!pRegion && REF_STRING_FROM_HANDLE(pDef->eaRegions[j]->hRegion)) {
			ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' refers to non-existent region '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->eaRegions[j]->hRegion));
		} else if (pRegion) {
			// Check to make sure required bones in the category are available on this skeleton as optional
			for(k=eaSize(&pRegion->eaCategories)-1; k>=0; --k) {
				PCCategory *pCategory = GET_REF(pRegion->eaCategories[k]->hCategory);
				if (pCategory) {
					for(n=eaSize(&pCategory->eaRequiredBones)-1; n>=0; --n) {
						bFound = false;
						for(m=eaSize(&pDef->eaOptionalBoneDefs)-1; m>=0; --m) {
							if (GET_REF(pDef->eaOptionalBoneDefs[m]->hBone) == GET_REF(pCategory->eaRequiredBones[n]->hBone)) {
								bFound = true;
								break;
							}
						}
						if (!bFound) {
							ErrorFilenamef(pCategory->pcFileName,"Costume category '%s' requires a bone '%s' that is not optional on skeleton '%s'\n",pCategory->pcName,REF_STRING_FROM_HANDLE(pCategory->eaRequiredBones[n]->hBone),pDef->pcName);
						}
					}
					for(n=eaSize(&pCategory->eaExcludedBones)-1; n>=0; --n) {
						bFound = false;
						for(m=eaSize(&pDef->eaRequiredBoneDefs)-1; m>=0; --m) {
							if (GET_REF(pDef->eaRequiredBoneDefs[m]->hBone) == GET_REF(pCategory->eaExcludedBones[n]->hBone)) {
								bFound = true;
								break;
							}
						}
						if (!bFound) {
							for(m=eaSize(&pDef->eaOptionalBoneDefs)-1; m>=0; --m) {
								if (GET_REF(pDef->eaOptionalBoneDefs[m]->hBone) == GET_REF(pCategory->eaExcludedBones[n]->hBone)) {
									bFound = true;
									break;
								}
							}
						}
						if (!bFound) {
							ErrorFilenamef(pCategory->pcFileName,"Costume category '%s' lists an excluded bone '%s' that is not part of skeleton '%s'\n",pCategory->pcName,REF_STRING_FROM_HANDLE(pCategory->eaExcludedBones[n]->hBone),pDef->pcName);
						}
					}
				}
			}
		}
	}

	if (pDef->fHeightBase < pDef->fHeightMin) {
		ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' base scale is less than min scale\n",pDef->pcName);
	}
	if (pDef->fHeightMax < pDef->fHeightBase) {
		ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' max scale is less than base scale\n",pDef->pcName);
	}

	if (eaSize(&pDef->eaBodyScaleInfo) < eafSize(&pDef->eafDefaultBodyScales)) {
		ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' has too many values for default body scale\n",pDef->pcName);
	}
	if (eaSize(&pDef->eaBodyScaleInfo) < eafSize(&pDef->eafPlayerMinBodyScales)) {
		ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' has too many values for player min body scale\n",pDef->pcName);
	}
	if (eaSize(&pDef->eaBodyScaleInfo) < eafSize(&pDef->eafPlayerMaxBodyScales)) {
		ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' has too many values for player max body scale\n",pDef->pcName);
	}

	{
		SkelScaleInfo* pScaleInfo = NULL;
		if (eaSize(&pDef->eaBodyScaleInfo) > 0) // Do we have at least one bodyscale?
		{
			// If so, make sure our skel info has a scale info
			if (pSkelInfo)
			{
				pScaleInfo = GET_REF(pSkelInfo->hScaleInfo);
				if (!pScaleInfo)
					ErrorFilenamef(pDef->pcFileName,"Costume skeleton %s has BodyScales, but SkelInfo %s has no Scale info!\n",pDef->pcName, pSkelInfo->pcSkelInfoName);
			}
		}

		for(j=eaSize(&pDef->eaBodyScaleInfo)-1; j>=0; --j) {
			PCBodyScaleInfo *pScale = pDef->eaBodyScaleInfo[j];
			if (!GET_REF(pScale->displayNameMsg.hMessage)) {
				ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' body scale '%s' is missing its display name message\n",pDef->pcName,pScale->pcName);
			}
			for(k=eaSize(&pScale->eaValues)-1; k>=0; --k) {
				PCBodyScaleValue *pValue = pScale->eaValues[k];
				if (!GET_REF(pValue->displayNameMsg.hMessage)) {
					if (REF_STRING_FROM_HANDLE(pValue->displayNameMsg.hMessage)) {
						ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' body scale '%s' references non-existent message '%s'\n",pDef->pcName,pScale->pcName,REF_STRING_FROM_HANDLE(pValue->displayNameMsg.hMessage));
					} else {
						ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' body scale '%s' value '%s' is missing its display name message\n",pDef->pcName,pScale->pcName,pValue->pcName);
					}
				}
			}
			if (pScaleInfo)
			{
				bool bMatchFound = false;
				// Make sure that each BodyScale is present
				FOR_EACH_IN_EARRAY_FORWARDS(pScaleInfo->eaScaleAnimTrack, SkelScaleAnimTrack, pScaleAnimTrack)
				{
					if (pScaleAnimTrack->pcName == pScale->pcName)
					{
						bMatchFound = true;
						break;
					}
				}
				FOR_EACH_END;
				if (!bMatchFound)
				{
					ErrorFilenamef(pDef->pcFileName, "Costume skeleton '%s' body scale '%s' does not have matching ScaleAnimTrack in ScaleInfo '%s'.\n", pDef->pcName, pScale->pcName, pScaleInfo->pcScaleInfoName);
				}
			}
		}
	}

	for(j=eaSize(&pDef->eaStanceInfo)-1; j>=0; --j) {
		PCStanceInfo *pStance = pDef->eaStanceInfo[j];
		if (!GET_REF(pStance->displayNameMsg.hMessage)) {
			ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' stance '%s' is missing its display name message\n",pDef->pcName,pStance->pcName);
		}
	}
	if (pDef->pcDefaultStance) {
		bFound = false;
		for(j=eaSize(&pDef->eaStanceInfo)-1; j>=0; --j) {
			if (stricmp(pDef->eaStanceInfo[j]->pcName, pDef->pcDefaultStance) == 0) {
				bFound = true;
				break;
			}
		}
		if (!bFound) {
			ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' has a default stance '%s' that is not defined\n",pDef->pcName,pDef->pcDefaultStance);
		}
	}

	for(j=eaSize(&pDef->eaScaleInfo)-1; j>=0; --j) {
		PCScaleInfo *pScale = pDef->eaScaleInfo[j];
		costumeLoad_ValidateScaleInfo(pDef, pScale);
	}
	for(j=eaSize(&pDef->eaScaleInfoGroups)-1; j>=0; --j) {
		PCScaleInfoGroup *pGroup = pDef->eaScaleInfoGroups[j];
		if (!pGroup->pcName) {
			ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' has an unnamed scale group\n", pDef->pcName);
		}
		if (!pGroup->pcDisplayName) {
			ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' scale group '%s' is missing its display name\n", pDef->pcName, pGroup->pcName);
		}
		for(k=eaSize(&pGroup->eaScaleInfo)-1; k>=0; --k) {
			PCScaleInfo *pScale = pGroup->eaScaleInfo[k];
			costumeLoad_ValidateScaleInfo(pDef, pScale);
		}
	}

	pSkinColor = GET_REF(pDef->hSkinColorSet);
	if (!pSkinColor && REF_STRING_FROM_HANDLE(pDef->hSkinColorSet)) {
		ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hSkinColorSet));
	}
	pBodyColor[0] = GET_REF(pDef->hBodyColorSet);
	if (!pBodyColor[0] && REF_STRING_FROM_HANDLE(pDef->hBodyColorSet)) {
		ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hBodyColorSet));
	}
	pBodyColor[1] = GET_REF(pDef->hBodyColorSet1);
	if (!pBodyColor[1] && REF_STRING_FROM_HANDLE(pDef->hBodyColorSet1)) {
		ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hBodyColorSet1));
	}
	pBodyColor[2] = GET_REF(pDef->hBodyColorSet2);
	if (!pBodyColor[2] && REF_STRING_FROM_HANDLE(pDef->hBodyColorSet2)) {
		ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hBodyColorSet2));
	}
	pBodyColor[3] = GET_REF(pDef->hBodyColorSet3);
	if (!pBodyColor[3] && REF_STRING_FROM_HANDLE(pDef->hBodyColorSet3)) {
		ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hBodyColorSet3));
	}
	pQuadSet = GET_REF(pDef->hColorQuadSet);
	if (!pQuadSet && REF_STRING_FROM_HANDLE(pDef->hColorQuadSet)) {
		ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' refers to non-existent color quad set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hColorQuadSet));
	}

	// check all quad color to make sure they exist in skeleton
	if(pQuadSet && (pDef->eRestriction & (kPCRestriction_Player|kPCRestriction_Player_Initial)) != 0)
	{
		for(i = 0; i < eaSize(&pQuadSet->eaColorQuads); ++i)
		{
			if(!costumeLoad_ColorInSets(pQuadSet->eaColorQuads[i]->color0, pBodyColor[0], pSkinColor) ||
				!costumeLoad_ColorInSets(pQuadSet->eaColorQuads[i]->color1, (!pBodyColor[1]?pBodyColor[0]:pBodyColor[1]), pSkinColor) ||
				!costumeLoad_ColorInSets(pQuadSet->eaColorQuads[i]->color2, (!pBodyColor[2]?pBodyColor[0]:pBodyColor[2]), pSkinColor) ||
				!costumeLoad_ColorInSets(pQuadSet->eaColorQuads[i]->color3, (!pBodyColor[3]?pBodyColor[0]:pBodyColor[3]), pSkinColor))
			{
				ErrorFilenamef(pDef->pcFileName,"Costume skeleton '%s' has color quad set '%s' that doesn't match skin or body colors\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hColorQuadSet));
			}
		}
	}

	// TODO: Make sure the skeleton really exists in the system
	// TODO: Make sure the provided scale values really exist on the skeleton
	// TODO: Make sure the provided bones really exist on the skeleton
}


static void costumeLoad_ValidateSkeletons(void)
{
	DictionaryEArrayStruct *pSkeletons = resDictGetEArrayStruct("CostumeSkeleton");
	int i;

	for(i=eaSize(&pSkeletons->ppReferents)-1; i>=0; --i) {
		costumeLoad_ValidateSkeleton(pSkeletons->ppReferents[i]);
	}
}

AUTO_TRANS_HELPER_SIMPLE;
PCSlotType *costumeLoad_GetSlotType(const char *pcName)
{
	const char *pcPoolName = allocFindString(pcName);
	int i;

	if (!pcName) {
		return NULL;
	}

	for(i=eaSize(&g_CostumeSlotTypes.eaSlotTypes)-1; i>=0; --i) {
		if (g_CostumeSlotTypes.eaSlotTypes[i]->pcName == pcPoolName) {
			return g_CostumeSlotTypes.eaSlotTypes[i];
		}
	}
	return NULL;
}

static void costumeLoad_ValidateSlotType(PCSlotType *pType)
{
	int i;

	for(i=eaSize(&pType->eaCategories)-1; i>=0; --i) {
		PCCategoryRef *pRef = pType->eaCategories[i];

		if (!GET_REF(pRef->hCategory) && REF_STRING_FROM_HANDLE(pRef->hCategory)) {
			ErrorFilenamef(pType->pcFileName,"Costume slot type '%s' refers to non-existent category '%s'\n",pType->pcName,REF_STRING_FROM_HANDLE(pRef->hCategory));
		}
	}
}

static void costumeLoad_ValidateSlotTypes(void)
{
	int i;

	for(i=eaSize(&g_CostumeSlotTypes.eaSlotTypes)-1; i>=0; --i) {
		costumeLoad_ValidateSlotType(g_CostumeSlotTypes.eaSlotTypes[i]);
	}
}

AUTO_TRANS_HELPER_SIMPLE;
PCSlotSet *costumeLoad_GetSlotSet(const char *pcName)
{
	const char *pcPoolName = allocFindString(pcName);
	int i;

	if (!pcName) {
		return NULL;
	}

	for(i=eaSize(&g_CostumeSlotSets.eaSlotSets)-1; i>=0; --i) {
		if (g_CostumeSlotSets.eaSlotSets[i]->pcName == pcPoolName) {
			return g_CostumeSlotSets.eaSlotSets[i];
		}
	}
	return NULL;
}

static void costumeLoad_GenerateSlotSet(PCSlotSet *pSet)
{
	int i;

	for(i=eaSize(&pSet->eaSlotDefs)-1; i>=0; --i) {
		PCSlotDef *pDef = pSet->eaSlotDefs[i];

		// Generate expressions
		if (pDef->pExprUnhide) {
			exprGenerate(pDef->pExprUnhide, s_pCostumeContext);
		}
		if (pDef->pExprUnlock) {
			exprGenerate(pDef->pExprUnlock, s_pCostumeContext);
		}
	}
}

static void costumeLoad_GenerateSlotSets(void)
{
	int i;

	for(i=eaSize(&g_CostumeSlotSets.eaSlotSets)-1; i>=0; --i) {
		costumeLoad_GenerateSlotSet(g_CostumeSlotSets.eaSlotSets[i]);
	}
}

static void costumeLoad_ValidateSlotSet(PCSlotSet *pSet)
{
	int i, j;

	for(i=eaSize(&pSet->eaSlotDefs)-1; i>=0; --i) {
		PCSlotDef *pDef = pSet->eaSlotDefs[i];

		if (!GET_REF(pDef->descriptionMsg.hMessage) && REF_STRING_FROM_HANDLE(pDef->descriptionMsg.hMessage)) {
			ErrorFilenamef(pSet->pcFileName,"Costume slot set '%s' refers to non-existent message '%s'\n",pSet->pcName,REF_STRING_FROM_HANDLE(pDef->descriptionMsg.hMessage));
		}

		if (!GET_REF(pDef->lockedDescriptionMsg.hMessage) && REF_STRING_FROM_HANDLE(pDef->lockedDescriptionMsg.hMessage)) {
			ErrorFilenamef(pSet->pcFileName,"Costume slot set '%s' refers to non-existent message '%s'\n",pSet->pcName,REF_STRING_FROM_HANDLE(pDef->lockedDescriptionMsg.hMessage));
		}

		pDef->pSlotType = costumeLoad_GetSlotType(pDef->pcSlotType);
		if (pDef->pcSlotType && !pDef->pSlotType) {
			ErrorFilenamef(pSet->pcFileName,"Costume slot set '%s' refers to non-existent slot type '%s'\n",pSet->pcName,pDef->pcSlotType);
		}

		for (j=eaSize(&pDef->eaOptionalSlotTypes)-1; j>=0; --j) {
			if (pDef->eaOptionalSlotTypes[j] && !costumeLoad_GetSlotType(pDef->eaOptionalSlotTypes[j])) {
				ErrorFilenamef(pSet->pcFileName,"Costume slot set '%s' refers to non-existent slot type '%s'\n",pSet->pcName,pDef->eaOptionalSlotTypes[j]);
			}
		}

		for(j=i-1; j>=0; --j) {
			if (pSet->eaSlotDefs[j]->iSlotID == pDef->iSlotID) {
				ErrorFilenamef(pSet->pcFileName,"Costume slot set '%s' has more than one slot def with ID %d\n",pSet->pcName,pDef->iSlotID);
			}
		}
		if (pSet->pExtraSlotDef && (pSet->pExtraSlotDef->iSlotID == pDef->iSlotID)) {
			ErrorFilenamef(pSet->pcFileName,"Costume slot set '%s' has more than one slot def with ID %d\n",pSet->pcName,pDef->iSlotID);
		}
	}
	//costumeLoad_GenerateSlotSet(pSet);
}

static void costumeLoad_ValidateSlotSets(void)
{
	PCSlotSet *pDefault = NULL;
	int i;

	for(i=eaSize(&g_CostumeSlotSets.eaSlotSets)-1; i>=0; --i) {
		PCSlotSet *pSet = g_CostumeSlotSets.eaSlotSets[i];

		costumeLoad_ValidateSlotSet(pSet);

		if (pSet->bIsDefault) {
			if (pDefault) {
				ErrorFilenamef(pSet->pcFileName,"Costume slot set '%s' is declared as default, but slot set '%s' is also default\n",pSet->pcName,pDefault->pcName);
			} else {
				pDefault = pSet;
			}
		}
	}
}

// --------------------------------------------------------------------------
// Dictionary Load logic
// --------------------------------------------------------------------------


void costumeLoad_LoadColors(void)
{
	loadstart_printf("Loading costume colors...");
	ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".ccolor", "CostumeColor.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeColorsDict);
	loadend_printf(" done (%d costume colors)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeColorsDict));
}


static int costumeLoad_ColorResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, UIColor *pColor, U32 userID)
{
	return VALIDATE_NOT_HANDLED;
}


void costumeLoad_LoadColorQuads(void)
{
	loadstart_printf("Loading costume color quads...");
	ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cquad", "CostumeColorQuad.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeColorQuadsDict);
	loadend_printf(" done (%d costume color quads)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeColorQuadsDict));
}

static int costumeLoad_ColorQuadResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PCColorQuad *pQuad, U32 userID)
{
	return VALIDATE_NOT_HANDLED;
}


#ifdef GAMECLIENT
extern void CostumeUI_DataChanged(void);
#endif

void costumeLoad_LoadCostumeSets(void)
{
	loadstart_printf("Loading costume sets...");
	if (gStripNonPlayerOnClient) {
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".costumeset", "CostumeSetClient.bin", PARSER_OPTIONALFLAG, g_hCostumeSetsDict);
	} else {
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".costumeset", "CostumeSet.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeSetsDict);
	}
	loadend_printf(" done (%d costume sets)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeSetsDict));
}


static void costumeLoad_ReloadSlotTypes(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading Costume Slot Types...");
	
	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);
	
	eaDestroyStruct(&g_CostumeSlotTypes.eaSlotTypes, parse_PCSlotType);
	ParserLoadFiles("defs/costumes/definitions", ".cslot", "CostumeSlotType.bin", PARSER_OPTIONALFLAG, parse_PCSlotTypes, &g_CostumeSlotTypes);
	
#ifdef GAMECLIENT
	CostumeUI_DataChanged();
#endif
	
	loadend_printf(" done (%d slot types)", eaSize(&g_CostumeSlotTypes.eaSlotTypes));
}

void costumeLoad_LoadSlotTypes(void)
{
	loadstart_printf("Loading costume slots...");

	eaDestroyStruct(&g_CostumeSlotTypes.eaSlotTypes, parse_PCSlotType);
	ParserLoadFiles("defs/costumes/definitions", ".cslot", "CostumeSlotType.bin", PARSER_OPTIONALFLAG, parse_PCSlotTypes, &g_CostumeSlotTypes);
	
	// Reload callbacks
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/costumes/definitions/*.cslot", costumeLoad_ReloadSlotTypes);
	}

	loadend_printf(" done (%d slot types)", eaSize(&g_CostumeSlotTypes.eaSlotTypes));
}

static void costumeLoad_ReloadSlotSets(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading Costume Slot Sets...");
	
	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);
	
	eaDestroyStruct(&g_CostumeSlotSets.eaSlotSets, parse_PCSlotSet);
	ParserLoadFiles(NULL, "defs/config/CostumeSlots.def", "CostumeSlotDef.bin", PARSER_OPTIONALFLAG, parse_PCSlotSets, &g_CostumeSlotSets);
	costumeLoad_GenerateSlotSets();
	
#ifdef GAMECLIENT
	CostumeUI_DataChanged();
#endif
	
	loadend_printf(" done (%d slot sets)", eaSize(&g_CostumeSlotSets.eaSlotSets));
}

void costumeLoad_LoadSlotSets(void)
{
	loadstart_printf("Loading costume slot sets...");

	eaDestroyStruct(&g_CostumeSlotSets.eaSlotSets, parse_PCSlotSet);
	ParserLoadFiles(NULL, "defs/config/CostumeSlots.def", "CostumeSlotDef.bin", PARSER_OPTIONALFLAG, parse_PCSlotSets, &g_CostumeSlotSets);
	costumeLoad_GenerateSlotSets();

	// Reload callbacks
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/CostumeSlots.def", costumeLoad_ReloadSlotSets);
	}

	loadend_printf(" done (%d slot sets)", eaSize(&g_CostumeSlotSets.eaSlotSets));
}


void costumeLoad_LoadMoods(void)
{
	loadstart_printf("Loading costume moods...");
	ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".mood", "CostumeMood.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeMoodDict);
	loadend_printf(" done (%d costume moods)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeMoodDict));
}


static int costumeLoad_MoodResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PCMood *pMood, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_POST_TEXT_READING:
		if (!GET_REF(pMood->displayNameMsg.hMessage)) {
			if (REF_STRING_FROM_HANDLE(pMood->displayNameMsg.hMessage)) {
				ErrorFilenamef(pMood->pcFilename, "Mood '%s' is refers to non-existent message key '%s'", pMood->pcName, REF_STRING_FROM_HANDLE(pMood->displayNameMsg.hMessage));
			} else {
				ErrorFilenamef(pMood->pcFilename, "Mood '%s' does not have a display name defined", pMood->pcName);
			}
		}
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

void costumeLoad_LoadVoices(void)
{
	loadstart_printf("Loading costume voices...");
	ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cvoice", "CostumeVoice.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeVoiceDict);
	loadend_printf(" done (%d costume voices)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeVoiceDict));
}


static int costumeLoad_VoiceResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PCVoice *pVoice, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_POST_TEXT_READING:
		if (!GET_REF(pVoice->displayNameMsg.hMessage)) {
			if (REF_STRING_FROM_HANDLE(pVoice->displayNameMsg.hMessage)) {
				ErrorFilenamef(pVoice->pcFilename, "Voice '%s' is refers to non-existent message key '%s'", pVoice->pcName, REF_STRING_FROM_HANDLE(pVoice->displayNameMsg.hMessage));
			} else {
				ErrorFilenamef(pVoice->pcFilename, "Voice '%s' does not have a display name defined", pVoice->pcName);
			}
		}
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}


void costumeLoad_LoadRegions(void)
{
	loadstart_printf("Loading costume regions...");
	ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".creg", "CostumeRegion.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeRegionDict);
	loadend_printf(" done (%d costume regions)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeRegionDict));
}


static int costumeLoad_RegionResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PCRegion *pReg, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_CHECK_REFERENCES:
		if (gCostumeValidate) {
			costumeLoad_ValidateRegion(pReg);
		}
		if (gCostumeValidate && !gIsLoading) {
			gValidateCostumePartsNextTick = true;
		}
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}


void costumeLoad_LoadCategories(void)
{
	loadstart_printf("Loading costume categories...");
	ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".ccat", "CostumeCategory.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeCategoryDict);
	loadend_printf(" done (%d costume categories)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeCategoryDict));
}


static int costumeLoad_CategoryResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PCCategory *pCat, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_CHECK_REFERENCES:
		if (gCostumeValidate) {
			costumeLoad_ValidateCategory(pCat);
		}
		if (gCostumeValidate && !gIsLoading) {
			gValidateCostumePartsNextTick = true;
		}
		return VALIDATE_HANDLED;
	}


	return VALIDATE_NOT_HANDLED;
}


void costumeLoad_LoadStyles(void)
{
	loadstart_printf("Loading costume styles...");
	ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cstyle", "CostumeStyle.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeStyleDict);
	loadend_printf(" done (%d costume styles)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeStyleDict));
}

void costumeLoad_LoadCostumeGroups(void)
{
	loadstart_printf("Loading costume styles...");
	ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cgroups", "CostumeGroups.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeGroupsDict);
	loadend_printf(" done (%d costume styles)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeGroupsDict));
}

static int costumeLoad_CostumeGroupsResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PCCostumeGroupInfo *pCGroup, U32 userID)
{
	return VALIDATE_NOT_HANDLED;
}

static int costumeLoad_StyleResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PCStyle *pStyle, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_CHECK_REFERENCES:
		if (gCostumeValidate) {
			costumeLoad_ValidateStyle(pStyle);
		}
		if (gCostumeValidate && !gIsLoading) {
			gValidateCostumePartsNextTick = true;
		}
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}


void costumeLoad_ValidateLayer(PCLayer *pLayer)
{
	if (!GET_REF(pLayer->displayNameMsg.hMessage) && REF_STRING_FROM_HANDLE(pLayer->displayNameMsg.hMessage)) {
		ErrorFilenamef(pLayer->pcFileName,"Layer '%s' refers to non-existent message '%s'\n",pLayer->pcName,REF_STRING_FROM_HANDLE(pLayer->displayNameMsg.hMessage));
	}
}


void costumeLoad_LoadLayers(void)
{
	loadstart_printf("Loading costume layers...");
	ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".clayer", "CostumeLayer.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeLayerDict);
	loadend_printf(" done (%d costume layers)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeLayerDict));
}


static int costumeLoad_LayerResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PCLayer *pLayer, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_CHECK_REFERENCES:
		if (gCostumeValidate && !gIsLoading) {
			costumeLoad_ValidateLayer(pLayer);
			gValidateCostumePartsNextTick = true;
		}
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}


void costumeLoad_LoadTextures(void)
{
	loadstart_printf("Loading costume textures...");
	if (gStripNonPlayerOnClient) {
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".ctex", "CostumeTextureClient.bin", PARSER_OPTIONALFLAG, g_hCostumeTextureDict);
	} else {
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".ctex", "CostumeTexture.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeTextureDict);
	}
	loadend_printf(" done (%d costume textures)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeTextureDict));
}


static int costumeLoad_TextureResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PCTextureDef *pTex, U32 userID)
{
	// TODO(jfw): Remove after migration.
	static PCTextureMovableOptions *s_pMovableDefault;
	static PCTextureValueOptions *s_pValueDefault;
	if (!s_pValueDefault)
		s_pValueDefault = StructCreate(parse_PCTextureValueOptions);
	if (!s_pMovableDefault)
		s_pMovableDefault = StructCreate(parse_PCTextureMovableOptions);
	switch(eType)
	{
	xcase RESVALIDATE_POST_TEXT_READING:
		if (!pTex->pValueOptions
			&& StructCompare(parse_PCTextureValueOptions, s_pValueDefault, &pTex->deprecated_ValueOptions, 0, 0, 0)) {
			pTex->pValueOptions = StructClone(parse_PCTextureValueOptions, &pTex->deprecated_ValueOptions);
			StructReset(parse_PCTextureValueOptions, &pTex->deprecated_ValueOptions);
		}

		if (!pTex->pMovableOptions
			&& StructCompare(parse_PCTextureMovableOptions, s_pMovableDefault, &pTex->deprecated_MovableOptions, 0, 0, 0)) {
			pTex->pMovableOptions = StructClone(parse_PCTextureMovableOptions, &pTex->deprecated_MovableOptions);
			StructReset(parse_PCTextureMovableOptions, &pTex->deprecated_MovableOptions);
		}

		if (IsClient() && gStripNonPlayerOnClient && gIsLoading) {
			if ((pTex->eRestriction & (kPCRestriction_Player | kPCRestriction_Player_Initial)) == 0) {
				resDoNotLoadCurrentResource(); // Performs free
			}
		}
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_CHECK_REFERENCES:
		if (gCostumeValidate) {
			costumeLoad_ValidateTexture(pTex);
		}
		if (gCostumeValidate && !gIsLoading) {
			gValidateCostumePartsNextTick = true;
		}
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_FIX_FILENAME:
		resFixPooledFilename((char**)&pTex->pcFileName, COSTUME_DEFS_BASE_DIR, pTex->pcScope, pTex->pcName, TEX_EXTENSION);
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}


void costumeLoad_LoadMaterials(void)
{
	loadstart_printf("Loading costume materials...");
	if (gStripNonPlayerOnClient) {
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cmat", "CostumeMaterialClient.bin", PARSER_OPTIONALFLAG, g_hCostumeMaterialDict);
	} else {
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cmat", "CostumeMaterial.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeMaterialDict);
	}
	loadend_printf(" done (%d costume materials)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeMaterialDict));
}


static int costumeLoad_MaterialResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PCMaterialDef *pMat, U32 userID)
{
	// TODO(jfw): Remove after migration.
	static PCMaterialColorOptions *s_pColorDefault;
	static PCMaterialOptions *s_pOptionsDefault;
	if (!s_pColorDefault)
		s_pColorDefault = StructCreate(parse_PCMaterialColorOptions);
	if (!s_pOptionsDefault)
		s_pOptionsDefault = StructCreate(parse_PCMaterialOptions);

	switch(eType)
	{
	xcase RESVALIDATE_POST_TEXT_READING:
		if (!pMat->pOptions
			&& StructCompare(parse_PCMaterialOptions, s_pOptionsDefault, &pMat->deprecated_Options, 0, 0, 0)) {
			pMat->pOptions = StructClone(parse_PCMaterialOptions, &pMat->deprecated_Options);
			StructReset(parse_PCMaterialOptions, &pMat->deprecated_Options);
		}
		if (!pMat->pColorOptions
			&& StructCompare(parse_PCMaterialColorOptions, s_pColorDefault, &pMat->deprecated_ColorOptions, 0, 0, 0)) {
			pMat->pColorOptions = StructClone(parse_PCMaterialColorOptions, &pMat->deprecated_ColorOptions);
			StructReset(parse_PCMaterialColorOptions, &pMat->deprecated_ColorOptions);
		}

		if (IsClient() && gStripNonPlayerOnClient && gIsLoading) {
			if ((pMat->eRestriction & (kPCRestriction_Player | kPCRestriction_Player_Initial)) == 0) {
				resDoNotLoadCurrentResource(); // Performs free
			}
		}
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_CHECK_REFERENCES:
		if (gCostumeValidate) {
			costumeLoad_ValidateMaterial(pMat);
		}
		if (gCostumeValidate && !gIsLoading) {
			gValidateCostumePartsNextTick = true;
		}
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_FIX_FILENAME:
		resFixPooledFilename((char**)&pMat->pcFileName, COSTUME_DEFS_BASE_DIR, pMat->pcScope, pMat->pcName, MAT_EXTENSION);

		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}


void costumeLoad_LoadMaterialAdds(void)
{
	loadstart_printf("Loading costume material adds...");
	if (gStripNonPlayerOnClient) {
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".addmat", "CostumeMaterialAddClient.bin", PARSER_OPTIONALFLAG, g_hCostumeMaterialAddDict);
	} else {
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".addmat", "CostumeMaterialAdd.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeMaterialAddDict);
	}
	loadend_printf(" done (%d costume material adds)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeMaterialAddDict));
}


static int costumeLoad_MaterialAddResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PCMaterialAdd *pMatAdd, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_POST_TEXT_READING:
		if (!pMatAdd->pcMatName) {
			// Force name on geometry adds to be set
			pMatAdd->pcMatName = allocAddString(pMatAdd->pcName);
		}
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_CHECK_REFERENCES:
		if (gCostumeValidate) {
			costumeLoad_ValidateMaterialAdd(pMatAdd);
		}
		if (gCostumeValidate && !gIsLoading) {
			gValidateCostumePartsNextTick = true;
		}
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}


void costumeLoad_LoadGeometries(void)
{
	loadstart_printf("Loading costume geometries...");
	if (gStripNonPlayerOnClient) {
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cgeo", "CostumeGeometryClient.bin", PARSER_OPTIONALFLAG, g_hCostumeGeometryDict);
	} else {
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cgeo", "CostumeGeometry.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeGeometryDict);
	}
	loadend_printf(" done (%d costume geometries)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeGeometryDict));
}


static int costumeLoad_GeometryResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PCGeometryDef *pGeo, U32 userID)
{
	// TODO(jfw): Remove after migration.
	static PCGeometryOptions *s_pOptionsDefault;
	static PCGeometryClothData *s_pClothDefault;
	if (!s_pOptionsDefault)
		s_pOptionsDefault = StructCreate(parse_PCGeometryOptions);
	if (!s_pClothDefault)
		s_pClothDefault = StructCreate(parse_PCGeometryClothData);

	switch(eType)
	{
	xcase RESVALIDATE_POST_TEXT_READING:
		if (!pGeo->pOptions
			&& StructCompare(parse_PCGeometryOptions, s_pOptionsDefault, &pGeo->deprecated_Options, 0, 0, 0)) {
			pGeo->pOptions = StructClone(parse_PCGeometryOptions, &pGeo->deprecated_Options);
			StructReset(parse_PCGeometryOptions, &pGeo->deprecated_Options);
		}
		if (!pGeo->pClothData
			&& StructCompare(parse_PCGeometryClothData, s_pClothDefault, &pGeo->deprecated_ClothData, 0, 0, 0)) {
			pGeo->pClothData = StructClone(parse_PCGeometryClothData, &pGeo->deprecated_ClothData);
			StructReset(parse_PCGeometryClothData, &pGeo->deprecated_ClothData);
		}

		if (eaSize(&pGeo->eaCategories) == 0) {
			// Backward compatibility: Put all geos without any category into this one
			PCCategoryRef *pCatRef = StructCreate(parse_PCCategoryRef);
			SET_HANDLE_FROM_STRING(g_hCostumeCategoryDict, "Unassigned", pCatRef->hCategory);
			eaPush(&pGeo->eaCategories, pCatRef);
		}

		if (IsClient() && gStripNonPlayerOnClient && gIsLoading) {
			if (((pGeo->eRestriction & (kPCRestriction_Player | kPCRestriction_Player_Initial)) == 0) &&
				(!pGeo->pOptions || !pGeo->pOptions->bIsChild) &&
				(!GET_REF(pGeo->hBone) || !GET_REF(pGeo->hBone)->bIsChildBone))
			{
				resDoNotLoadCurrentResource(); // Performs free
			}
		}
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_CHECK_REFERENCES:
		if (gCostumeValidate) {
			costumeLoad_ValidateGeometry(pGeo);
		}
		if (gCostumeValidate && !gIsLoading) {
			gValidateCostumePartsNextTick = true;
		}
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_FIX_FILENAME:
		resFixPooledFilename((char**)&pGeo->pcFileName, COSTUME_DEFS_BASE_DIR, pGeo->pcScope, pGeo->pcName, GEO_EXTENSION);
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

		
void costumeLoad_LoadGeometryAdds(void)
{
	loadstart_printf("Loading costume geometry add...");
	if (gStripNonPlayerOnClient) {
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".addgeo", "CostumeGeometryAddClient.bin", PARSER_OPTIONALFLAG, g_hCostumeGeometryAddDict);
	} else {
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".addgeo", "CostumeGeometryAdd.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeGeometryAddDict);
	}
	loadend_printf(" done (%d costume geometry adds)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeGeometryAddDict));
}


static int costumeLoad_GeometryAddResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PCGeometryAdd *pGeoAdd, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_POST_TEXT_READING:
		if (!pGeoAdd->pcGeoName) {
			// Force name on geometry adds to be set
			pGeoAdd->pcGeoName = allocAddString(pGeoAdd->pcName);
		}
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_CHECK_REFERENCES:
		if (gCostumeValidate) {
			costumeLoad_ValidateGeometryAdd(pGeoAdd);
		}
		if (gCostumeValidate && !gIsLoading) {
			gValidateCostumePartsNextTick = true;
		}
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}


void costumeLoad_LoadBones(void)
{
	loadstart_printf("Loading costume bones...");
	ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cbone", "CostumeBone.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeBoneDict);
	loadend_printf(" done (%d costume bones)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeBoneDict));
}


static int costumeLoad_BoneResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PCBoneDef *pBone, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_CHECK_REFERENCES:
		if (gCostumeValidate) {
			costumeLoad_ValidateBone(pBone);
		}
		if (gCostumeValidate && !gIsLoading) {
			gValidateCostumePartsNextTick = true;
		}
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}


static void costumeLoad_SkeletonPostTextRead(PCSkeletonDef *pDef)
{
	if (!REF_STRING_FROM_HANDLE(pDef->hSkinColorSet)) {
		SET_HANDLE_FROM_STRING(g_hCostumeColorsDict, "Core_Skin", pDef->hSkinColorSet);
	}
	if (!REF_STRING_FROM_HANDLE(pDef->hBodyColorSet)) {
		SET_HANDLE_FROM_STRING(g_hCostumeColorsDict, "Core_Body", pDef->hBodyColorSet);
	}
	if (!REF_STRING_FROM_HANDLE(pDef->hColorQuadSet)) {
		SET_HANDLE_FROM_STRING(g_hCostumeColorQuadsDict, "Core_ColorQuadSet", pDef->hColorQuadSet);
	}
	if (!pDef->fPlayerMinHeight) {
		pDef->fPlayerMinHeight = pDef->fHeightMin;
	}
	if (!pDef->fDefaultHeight) {
		pDef->fDefaultHeight = pDef->fHeightBase;
	}
	if (!pDef->fPlayerMaxHeight) {
		pDef->fPlayerMaxHeight = pDef->fHeightMax;
	}
	while (eafSize(&pDef->eafPlayerMinBodyScales) < eaSize(&pDef->eaBodyScaleInfo)) {
		eafPush(&pDef->eafPlayerMinBodyScales, 0);
	}
	while (eafSize(&pDef->eafPlayerMaxBodyScales) < eaSize(&pDef->eaBodyScaleInfo)) {
		eafPush(&pDef->eafPlayerMaxBodyScales, 100);
	}
	if (!pDef->fPlayerMaxMuscle) {
		pDef->fPlayerMaxMuscle = 100;
	}

	{
		SkelInfo* pSkelInfo = RefSystem_ReferentFromString("SkelInfo", pDef->pcSkeleton);
		if (pSkelInfo && GET_REF(pSkelInfo->hBlendInfo))
		{
			pDef->bTorsoPointing = !!(GET_REF(pSkelInfo->hBlendInfo)->bTorsoPointing) || !!(GET_REF(pSkelInfo->hBlendInfo)->bTorsoDirections);
		}
	}

	{
		int i;
		for (i=0;i<eaSize(&pDef->ppCollCapsules);i++)
		{
			// make sure our capsules have normalized direction vectors
			normalVec3(pDef->ppCollCapsules[i]->vDir);
		}
	}
}


void costumeLoad_LoadSkeletons(void)
{
	loadstart_printf("Loading costume skeletons...");
	ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cskel", (bLoadedSkelInfos||isProductionMode())?"CostumeSkeleton.bin":"CostumeSkeletonNoSkif.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hCostumeSkeletonDict);
	loadend_printf(" done (%d costume skeletons)", RefSystem_GetDictionaryNumberOfReferents(g_hCostumeSkeletonDict));
}


static int costumeLoad_SkeletonResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PCSkeletonDef *pSkel, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_POST_TEXT_READING:
		costumeLoad_SkeletonPostTextRead(pSkel);
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_CHECK_REFERENCES:
		if (gCostumeValidate) {
			costumeLoad_ValidateSkeleton(pSkel);
		}
		if (gCostumeValidate && !gIsLoading) {
			gValidateCostumePartsNextTick = true;
		}
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}


static int costumeLoad_CostumeSetResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PCCostumeSet *pCostumeSet, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_POST_TEXT_READING:
		if (IsClient() && gStripNonPlayerOnClient && gIsLoading) {
			if ((pCostumeSet->eCostumeSetFlags & kPCCostumeSetFlags_DontPreloadOnClient)) {
				resDoNotLoadCurrentResource(); // Performs free
			}
		}
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_CHECK_REFERENCES:
		if (gCostumeValidate) {
			costumeLoad_ValidateCostumeSet(pCostumeSet);
		}
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_POST_BINNING:
		fixupCostumeSet(pCostumeSet);
		return VALIDATE_HANDLED;
	}

	return VALIDATE_HANDLED;
}

static int costumeLoad_CostumeResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PlayerCostume *pCostume, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_POST_TEXT_READING:
	{
		// If costume is missing its height and the skeleton has a base height,
		// Set the costume to the skeleton's base height
		PCSkeletonDef *pSkeleton = GET_REF(pCostume->hSkeleton);
		if (pSkeleton && (pSkeleton->fHeightBase > 0.0) && (pCostume->fHeight == 0.0)) {
			CONTAINER_NOCONST(PlayerCostume, pCostume)->fHeight = pSkeleton->fHeightBase;
		}

		if (IsClient() && gStripNonPlayerOnClient && gIsLoading) {
			if (!pCostume->bLoadedOnClient) {
				resDoNotLoadCurrentResource(); // Makes the costume not show up in the client
			}
		}
	}
	return VALIDATE_HANDLED;

	xcase RESVALIDATE_CHECK_REFERENCES:
		if (gCostumeValidate && !gIsLoading) {
			gValidateCostumePartsNextTick = true;
		}
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_POST_BINNING:
		if (gCostumeValidate) {
			costumeLoad_ValidatePlayerCostume(pCostume, GET_REF(pCostume->hSpecies), false, true, false);
			return VALIDATE_HANDLED;
		}

	xcase RESVALIDATE_FIX_FILENAME:
		resFixPooledFilename((char**)&pCostume->pcFileName, pCostume->pcScope && resIsInDirectory(pCostume->pcScope, "maps/") ? NULL : COSTUMES_BASE_DIR, pCostume->pcScope, pCostume->pcName, COSTUMES_EXTENSION);
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}


void costumeLoad_LoadPlayerCostumes(void)
{
	loadstart_printf("Loading costumes...");
	if (gStripNonPlayerOnClient) {
		ParserLoadFilesToDictionary(COSTUMES_BASE_DIR, ".costume", "PlayerCostumeClient.bin", PARSER_OPTIONALFLAG | PARSER_NO_RELOAD, g_hPlayerCostumeDict);
	} else {
		ParserLoadFilesToDictionary(COSTUMES_BASE_DIR, ".costume", "PlayerCostume.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, g_hPlayerCostumeDict);
	}
	loadend_printf(" done (%d costumes)", RefSystem_GetDictionaryNumberOfReferents(g_hPlayerCostumeDict));
}


void costumeLoad_ValidateAll(void)
{
	PERFINFO_AUTO_START_FUNC();
	coarseTimerAddInstance(NULL, __FUNCTION__);

	costumeLoad_ValidateRegions();
	costumeLoad_ValidateCategories();
	costumeLoad_ValidateStyles();
	costumeLoad_ValidateTextures();
	costumeLoad_ValidateMaterials();
	costumeLoad_ValidateMaterialAdds();
	costumeLoad_ValidateGeometries();
	costumeLoad_ValidateGeometryAdds();
	costumeLoad_ValidateBones();
	costumeLoad_ValidateSkeletons();
	costumeLoad_ValidateSlotTypes();
	costumeLoad_ValidateSlotSets();
	costumeLoad_ValidatePlayerCostumes();

	coarseTimerStopInstance(NULL, __FUNCTION__);
	PERFINFO_AUTO_STOP();
}


// --------------------------------------------------------------------------
// Startup Logic
// --------------------------------------------------------------------------

AUTO_STARTUP(EntityCostumes) ASTRT_DEPS(AS_Messages, WorldLib, Transformation);
void costumeLoad_LoadData(void)
{
	bool bLoadCostumes = false;
	bool bLoadCostumeParts = false;

	loadstart_printf("Loading Costume System...");

#if defined(GAMESERVER)
	if (!isProductionMode())
		gCostumeValidate = true;
#endif

#ifdef GAMECLIENT
	if (gPreloadPartsOnClient) {
		bLoadCostumeParts = true;
	}
	// The actual costumes load and reload for demo playback and bin making
	if (demo_playingBack() || gbMakeBinsAndExit || gGCLState.bForceLoadCostumes) {
		bLoadCostumes = true;
		bLoadCostumeParts = true;
		gReloadCostumeParts = true;
	}
	if (demo_playingBack() || gGCLState.bForceLoadCostumes) {
		gStripNonPlayerOnClient = false;
	}
#endif

	if (IsGameServerBasedType() || IsLoginServer() || IsAuctionServer()) {
		resLoadResourcesFromDisk(g_hCostumeColorQuadsDict, COSTUME_DEFS_BASE_DIR, ".cquad", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hCostumeColorsDict, COSTUME_DEFS_BASE_DIR, ".ccolor", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hCostumeMoodDict, COSTUME_DEFS_BASE_DIR, ".mood", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hCostumeVoiceDict, COSTUME_DEFS_BASE_DIR, ".cvoice", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hCostumeCategoryDict, COSTUME_DEFS_BASE_DIR, ".ccat", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hCostumeRegionDict, COSTUME_DEFS_BASE_DIR, ".creg", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hCostumeStyleDict, COSTUME_DEFS_BASE_DIR, ".cstyle", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hCostumeLayerDict, COSTUME_DEFS_BASE_DIR, ".clayer", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hCostumeTextureDict, COSTUME_DEFS_BASE_DIR, ".ctex", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hCostumeMaterialDict, COSTUME_DEFS_BASE_DIR, ".cmat", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hCostumeMaterialAddDict, COSTUME_DEFS_BASE_DIR, ".addmat", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hCostumeBoneDict, COSTUME_DEFS_BASE_DIR, ".cbone", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hCostumeGeometryDict, COSTUME_DEFS_BASE_DIR, ".cgeo", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hCostumeGeometryAddDict, COSTUME_DEFS_BASE_DIR, ".addgeo", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hCostumeSkeletonDict, COSTUME_DEFS_BASE_DIR, ".cskel", (bLoadedSkelInfos||isProductionMode())?NULL:"CostumeSkeletonNoSkif.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hCostumeSetsDict, COSTUME_DEFS_BASE_DIR, ".costumeset", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		costumeLoad_LoadSlotTypes();
		costumeLoad_LoadSlotSets();

		resLoadResourcesFromDisk(g_hCostumeGroupsDict, COSTUME_DEFS_BASE_DIR, ".cgroups", NULL, RESOURCELOAD_SHAREDMEMORY | RESOURCELOAD_USERDATA | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
	} else if (bLoadCostumeParts) {
		gIsLoading = true;

		costumeLoad_LoadColors();
		costumeLoad_LoadColorQuads();
		costumeLoad_LoadMoods();
		costumeLoad_LoadVoices();
		costumeLoad_LoadRegions();
		costumeLoad_LoadCategories();
		costumeLoad_LoadStyles();
		costumeLoad_LoadLayers();
		costumeLoad_LoadTextures();
		costumeLoad_LoadMaterials();
		costumeLoad_LoadMaterialAdds();
		costumeLoad_LoadBones(); // Before geometries
		costumeLoad_LoadGeometries();
		costumeLoad_LoadGeometryAdds();
		costumeLoad_LoadSkeletons();
		costumeLoad_LoadCostumeSets();
		costumeLoad_LoadSlotTypes();
		costumeLoad_LoadSlotSets();
		costumeLoad_LoadCostumeGroups();

		gIsLoading = false;
	}
	if (IsGameServerBasedType() || IsLoginServer() || IsAuctionServer() || bLoadCostumes) {
		resLoadResourcesFromDisk(g_hPlayerCostumeDict, COSTUMES_BASE_DIR, ".costume", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);

		generateSkelFixupInfos();
	} else if (bLoadCostumeParts) {
		gIsLoading = true;
		costumeLoad_LoadPlayerCostumes();
		gIsLoading = false;
	}

	// We need to update entities if any assets change
	resDictRegisterEventCallback(g_hPlayerCostumeDict, costumeEntity_UpdateEntityCostumes, NULL);
	resDictRegisterEventCallback(g_hCostumeSkeletonDict, costumeEntity_UpdateEntityCostumeParts, NULL);
	resDictRegisterEventCallback(g_hCostumeBoneDict, costumeEntity_UpdateEntityCostumeParts, NULL);
	resDictRegisterEventCallback(g_hCostumeGeometryDict, costumeEntity_UpdateEntityCostumeParts, NULL);
	resDictRegisterEventCallback(g_hCostumeMaterialDict, costumeEntity_UpdateEntityCostumeParts, NULL);
	resDictRegisterEventCallback(g_hCostumeTextureDict, costumeEntity_UpdateEntityCostumeParts, NULL);
	resDictRegisterEventCallback(g_hCostumeGeometryAddDict, costumeEntity_UpdateEntityCostumeParts, NULL);
	resDictRegisterEventCallback(g_hCostumeMaterialAddDict, costumeEntity_UpdateEntityCostumeParts, NULL);

	// Don't need to update if Region/Category/Style change

	// Item def changes may cause costume to be rechecked
	resDictRegisterEventCallback(g_hItemDict, costumeEntity_UpdateEntityCostumes, NULL);
	resDictRegisterEventCallback("SkelInfo", costumeEntity_UpdateEntityCostumeParts, NULL);
	
	// Load the costume configuration info
	ParserLoadFiles(NULL, "defs/config/CostumeConfig.def", "CostumeConfig.bin", 0, parse_CostumeConfig, &g_CostumeConfig);

#ifdef GAMECLIENT
	// Listen for changes in game account data
	resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA), CosutmeUI_GameAccountDictChanged, NULL);
#endif

	loadend_printf("done.");

	if( gbMakeBinsAndExit && IsClient() ) {
		loadstart_printf("Reloading Costume System for MakeBinsAndExit...");
		
		gStripNonPlayerOnClient = false;
		gIsLoading = true;
		
		RefSystem_ClearDictionary(g_hCostumeColorsDict, false);
		RefSystem_ClearDictionary(g_hCostumeColorQuadsDict, false);
		RefSystem_ClearDictionary(g_hCostumeMoodDict, false);
		RefSystem_ClearDictionary(g_hCostumeVoiceDict, false);
		RefSystem_ClearDictionary(g_hCostumeRegionDict, false);
		RefSystem_ClearDictionary(g_hCostumeCategoryDict, false);
		RefSystem_ClearDictionary(g_hCostumeGroupsDict, false);
		RefSystem_ClearDictionary(g_hCostumeLayerDict, false);
		RefSystem_ClearDictionary(g_hCostumeTextureDict, false);
		RefSystem_ClearDictionary(g_hCostumeMaterialDict, false);
		RefSystem_ClearDictionary(g_hCostumeMaterialAddDict, false);
		RefSystem_ClearDictionary(g_hCostumeBoneDict, false);
		RefSystem_ClearDictionary(g_hCostumeGeometryDict, false);
		RefSystem_ClearDictionary(g_hCostumeGeometryAddDict, false);
		RefSystem_ClearDictionary(g_hCostumeSkeletonDict, false);
		RefSystem_ClearDictionary(g_hCostumeSetsDict, false);
		eaDestroyStruct(&g_CostumeSlotTypes.eaSlotTypes, parse_PCSlotType);
		eaDestroyStruct(&g_CostumeSlotSets.eaSlotSets, parse_PCSlotSet);
		RefSystem_ClearDictionary(g_hCostumeGroupsDict, false);

		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".ccolor", NULL, PARSER_OPTIONALFLAG, g_hCostumeColorsDict);
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cquad", NULL, PARSER_OPTIONALFLAG, g_hCostumeColorQuadsDict);
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".mood", NULL, PARSER_OPTIONALFLAG, g_hCostumeMoodDict);
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cvoice", NULL, PARSER_OPTIONALFLAG, g_hCostumeVoiceDict);
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".creg", NULL, PARSER_OPTIONALFLAG, g_hCostumeRegionDict);
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".ccat", NULL, PARSER_OPTIONALFLAG, g_hCostumeCategoryDict);
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cstyle", NULL, PARSER_OPTIONALFLAG, g_hCostumeStyleDict);
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".clayer", NULL, PARSER_OPTIONALFLAG, g_hCostumeLayerDict);
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".ctex", NULL, PARSER_OPTIONALFLAG, g_hCostumeTextureDict);
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cmat", NULL, PARSER_OPTIONALFLAG, g_hCostumeMaterialDict);
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".addmat", NULL, PARSER_OPTIONALFLAG, g_hCostumeMaterialAddDict);
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cbone", NULL, PARSER_OPTIONALFLAG, g_hCostumeBoneDict);
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cgeo", NULL, PARSER_OPTIONALFLAG, g_hCostumeGeometryDict);
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".addgeo", NULL, PARSER_OPTIONALFLAG, g_hCostumeGeometryAddDict);
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cskel", NULL, PARSER_OPTIONALFLAG, g_hCostumeSkeletonDict);
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".costumeset", NULL, PARSER_OPTIONALFLAG, g_hCostumeSetsDict);
		ParserLoadFiles("defs/costumes/definitions", ".cslot", NULL, PARSER_OPTIONALFLAG, parse_PCSlotTypes, &g_CostumeSlotTypes);
		ParserLoadFiles(NULL, "defs/config/CostumeSlots.def", NULL, PARSER_OPTIONALFLAG, parse_PCSlotSets, &g_CostumeSlotSets);
		ParserLoadFilesToDictionary(COSTUME_DEFS_BASE_DIR, ".cgroups", NULL, PARSER_OPTIONALFLAG, g_hCostumeGroupsDict);

		gIsLoading = false;
		loadend_printf("done.");
	}
}

int autoStruct_fixup_PCColor();
int autoStruct_fixup_UIColorSet();

AUTO_RUN;
int costumeLoad_RegisterCostumeDicts(void)
{
	MP_CREATE(PCBoneRef, 500);
	MP_CREATE(PCCategoryRef, 2000);
	MP_CREATE(UIColor, 300);
	MP_CREATE(PCExtraTexture, 500);
	MP_CREATE(PCRegionRef, 300);
	MP_CREATE(PCScaleEntry, 1000);
	MP_CREATE(PCScaleInfo, 300);
	MP_CREATE(PCScaleInfoGroup, 100);

	MP_CREATE(PCTextureDef, 128);
	MP_CREATE(PCGeometryDef, 128);
	MP_CREATE(PCMaterialDef, 128);

	// Set up reference dictionaries for parts and such
	g_hCostumeColorsDict = RefSystem_RegisterSelfDefiningDictionary("CostumeColors",false, parse_UIColorSet, true, true, NULL);
	g_hCostumeColorQuadsDict = RefSystem_RegisterSelfDefiningDictionary("CostumeColorQuads",false, parse_PCColorQuadSet, true, true, NULL);
	g_hCostumeMoodDict = RefSystem_RegisterSelfDefiningDictionary("CostumeMood",false, parse_PCMood, true, true, NULL);
	g_hCostumeVoiceDict = RefSystem_RegisterSelfDefiningDictionary("CostumeVoice",false, parse_PCVoice, true, true, NULL);
	g_hCostumeRegionDict = RefSystem_RegisterSelfDefiningDictionary("CostumeRegion",false, parse_PCRegion, true, true, NULL);
	g_hCostumeCategoryDict = RefSystem_RegisterSelfDefiningDictionary("CostumeCategory",false, parse_PCCategory, true, true, NULL);
	g_hCostumeStyleDict = RefSystem_RegisterSelfDefiningDictionary("CostumeStyle",false, parse_PCStyle, true, true, NULL);
	g_hCostumeLayerDict = RefSystem_RegisterSelfDefiningDictionary("CostumeLayer",false, parse_PCLayer, true, true, NULL);
	g_hCostumeTextureDict = RefSystem_RegisterSelfDefiningDictionary("CostumeTexture",false, parse_PCTextureDef, true, true, NULL);
	g_hCostumeMaterialDict = RefSystem_RegisterSelfDefiningDictionary("CostumeMaterial",false, parse_PCMaterialDef, true, true, NULL);
	g_hCostumeMaterialAddDict = RefSystem_RegisterSelfDefiningDictionary("CostumeMaterialAdd",false, parse_PCMaterialAdd, true, true, NULL);
	g_hCostumeGeometryDict = RefSystem_RegisterSelfDefiningDictionary("CostumeGeometry",false, parse_PCGeometryDef, true, true, NULL);
	g_hCostumeGeometryAddDict = RefSystem_RegisterSelfDefiningDictionary("CostumeGeometryAdd",false, parse_PCGeometryAdd, true, true, NULL);
	g_hCostumeBoneDict = RefSystem_RegisterSelfDefiningDictionary("CostumeBone",false, parse_PCBoneDef, true, true, NULL);
	g_hCostumeSkeletonDict = RefSystem_RegisterSelfDefiningDictionary("CostumeSkeleton",false, parse_PCSkeletonDef, true, true, NULL);
	g_hCostumeSetsDict = RefSystem_RegisterSelfDefiningDictionary("CostumeSet",false, parse_PCCostumeSet, true, true, NULL);
	g_hCostumeGroupsDict = RefSystem_RegisterSelfDefiningDictionary("CostumeGroupDict",false, parse_PCCostumeGroupInfo, true, true, NULL);

	// This is the actual costume dictionary
	g_hPlayerCostumeDict = RefSystem_RegisterSelfDefiningDictionary("PlayerCostume",false, parse_PlayerCostume, true, true, NULL);

	resDictManageValidation(g_hCostumeColorsDict, costumeLoad_ColorResValidateCB);
	resDictManageValidation(g_hCostumeColorQuadsDict, costumeLoad_ColorQuadResValidateCB);
	resDictManageValidation(g_hCostumeMoodDict, costumeLoad_MoodResValidateCB);
	resDictManageValidation(g_hCostumeVoiceDict, costumeLoad_VoiceResValidateCB);
	resDictManageValidation(g_hCostumeRegionDict, costumeLoad_RegionResValidateCB);
	resDictManageValidation(g_hCostumeCategoryDict, costumeLoad_CategoryResValidateCB);
	resDictManageValidation(g_hCostumeStyleDict, costumeLoad_StyleResValidateCB);
	resDictManageValidation(g_hCostumeLayerDict, costumeLoad_LayerResValidateCB);
	resDictManageValidation(g_hCostumeTextureDict, costumeLoad_TextureResValidateCB);
	resDictManageValidation(g_hCostumeMaterialDict, costumeLoad_MaterialResValidateCB);
	resDictManageValidation(g_hCostumeMaterialAddDict, costumeLoad_MaterialAddResValidateCB);
	resDictManageValidation(g_hCostumeGeometryDict, costumeLoad_GeometryResValidateCB);
	resDictManageValidation(g_hCostumeGeometryAddDict, costumeLoad_GeometryAddResValidateCB);
	resDictManageValidation(g_hCostumeBoneDict, costumeLoad_BoneResValidateCB);
	resDictManageValidation(g_hCostumeSkeletonDict, costumeLoad_SkeletonResValidateCB);
	resDictManageValidation(g_hCostumeSetsDict, costumeLoad_CostumeSetResValidateCB);
	resDictManageValidation(g_hPlayerCostumeDict, costumeLoad_CostumeResValidateCB);
	resDictManageValidation(g_hCostumeGroupsDict, costumeLoad_CostumeGroupsResValidateCB);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hCostumeColorsDict);
		resDictProvideMissingResources(g_hCostumeColorQuadsDict);
		resDictProvideMissingResources(g_hCostumeMoodDict);
		resDictProvideMissingResources(g_hCostumeVoiceDict);
		resDictProvideMissingResources(g_hCostumeRegionDict);
		resDictProvideMissingResources(g_hCostumeCategoryDict);
		resDictProvideMissingResources(g_hCostumeStyleDict);
		resDictProvideMissingResources(g_hCostumeLayerDict);
		resDictProvideMissingResources(g_hCostumeTextureDict);
		resDictProvideMissingResources(g_hCostumeMaterialDict);
		resDictProvideMissingResources(g_hCostumeMaterialAddDict);
		resDictProvideMissingResources(g_hCostumeGeometryDict);
		resDictProvideMissingResources(g_hCostumeGeometryAddDict);
		resDictProvideMissingResources(g_hCostumeBoneDict);
		resDictProvideMissingResources(g_hCostumeSkeletonDict);
		resDictProvideMissingResources(g_hCostumeSetsDict);
		resDictProvideMissingResources(g_hPlayerCostumeDict);
		resDictProvideMissingResources(g_hCostumeGroupsDict);

		// Make sure to request from Resource DB if costume is in a namespace
		resDictGetMissingResourceFromResourceDBIfPossible((void*)g_hPlayerCostumeDict);

		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hCostumeColorsDict, NULL, NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hCostumeColorQuadsDict, NULL, NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hCostumeMoodDict, ".Name", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hCostumeVoiceDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hCostumeRegionDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hCostumeCategoryDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hCostumeStyleDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hCostumeLayerDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hCostumeTextureDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hCostumeMaterialDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hCostumeMaterialAddDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hCostumeGeometryDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hCostumeGeometryAddDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hCostumeBoneDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hCostumeSkeletonDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hCostumeSetsDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hPlayerCostumeDict, ".Name", ".Scope", ".Tags", NULL, NULL);
			resDictMaintainInfoIndex(g_hCostumeGroupsDict, ".Name", NULL, NULL, NULL, NULL);
		}
	} 
	else if (IsClient())
	{
		// Part dictionaries keep all values once downloaded
		resDictRequestMissingResources(g_hCostumeColorsDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCostumeColorQuadsDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCostumeMoodDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCostumeVoiceDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCostumeRegionDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCostumeCategoryDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCostumeStyleDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCostumeLayerDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCostumeTextureDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCostumeMaterialDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCostumeMaterialAddDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCostumeGeometryDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCostumeGeometryAddDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCostumeBoneDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCostumeSkeletonDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCostumeSetsDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCostumeGroupsDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);

		// Keep only recently used costumes
		resDictRequestMissingResources(g_hPlayerCostumeDict, 30, false, resClientRequestSendReferentCommand); // temp. fix for missing costumes bug in the resource system io. should be returned to 30 once it is fixed
		resSetDictionaryMustHaveEditCopyInEditMode(g_hPlayerCostumeDict);
	}

	return 1;
}


#include "AutoGen/CostumeCommon_h_ast.c"
#include "AutoGen/CostumeCommonEnums_h_ast.c"
#include "AutoGen/CostumeCommonLoad_h_ast.c"

