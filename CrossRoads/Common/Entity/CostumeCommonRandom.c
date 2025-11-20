/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#include "CostumeCommon.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonRandom.h"
#include "CostumeCommonTailor.h"
#include "error.h"
#include "GlobalTypes.h"
#include "Guild.h"
#include "rand.h"
#include "species_common.h"
#include "StringCache.h"
#include "file.h"

#include "AutoGen/CostumeCommonEnums_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static MersenneTable *gRandTable = NULL;

extern PCColorQuad g_StaticQuad;

// --------------------------------------------------------------------------

void costumeRandom_SetRandomTable(MersenneTable *pTable)
{
	gRandTable = pTable;
}


static void costumeRandom_AddWeight(F32 *pfTotalWeight, F32 fNewWeight)
{
	if (fNewWeight == 0.0) {
		*pfTotalWeight += 1.0;
	} else if (fNewWeight > 0) {
		*pfTotalWeight += fNewWeight;
	}
}

static bool costumeRandom_MatchWeight(F32 *pfWorkingWeight, F32 fCheckWeight)
{
	if (fCheckWeight == 0.0) {
		*pfWorkingWeight -= 1.0;
	} else if (fCheckWeight > 0) {
		*pfWorkingWeight -= fCheckWeight;
	}
	
	return (*pfWorkingWeight <= 0);
}

static bool costumeRandom_StyleMatches(PCStyle **eaStyles, const char **eaItemStyles)
{
	int i, j;
	for (i = eaSize(&eaStyles) - 1; i >= 0; i--) {
		for (j = eaSize(&eaItemStyles) - 1; j >= 0; j--) {
			if (eaStyles[i]->pcName == eaItemStyles[j] || !stricmp(eaStyles[i]->pcName, eaItemStyles[j])) {
				return true;
			}
		}
	}
	return false;
}


static bool costumeRandom_MaterialSatisfiesExpectations(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll, PCGeometryDef *pGeoDef, PCMaterialDef *pMaterial, PCTextureDef *pExpectedPattern, PCTextureDef *pExpectedDetail, PCTextureDef *pExpectedSpecular, PCTextureDef *pExpectedDiffuse, PCTextureDef *pExpectedMovable)
{
	PCTextureDef **eaTexs = NULL;
	int iMatches = 0;
	int iExpectedTextures = 0;
	PCTextureType eNonRandomizedTypes = 0;
	int j;
	const char *pcExpectedPattern;
	const char *pcExpectedDetail;
	const char *pcExpectedSpecular;
	const char *pcExpectedDiffuse;
	const char *pcExpectedMovable;

	iExpectedTextures += pExpectedPattern ? 1 : 0;
	iExpectedTextures += pExpectedDetail ? 1 : 0;
	iExpectedTextures += pExpectedSpecular ? 1 : 0;
	iExpectedTextures += pExpectedDiffuse ? 1 : 0;
	iExpectedTextures += pExpectedMovable ? 1 : 0;

	if (!iExpectedTextures) {
		return true;
	}

	eNonRandomizedTypes |= pExpectedPattern ? kPCTextureType_Pattern : 0;
	eNonRandomizedTypes |= pExpectedDetail ? kPCTextureType_Detail : 0;
	eNonRandomizedTypes |= pExpectedSpecular ? kPCTextureType_Specular : 0;
	eNonRandomizedTypes |= pExpectedDiffuse ? kPCTextureType_Diffuse : 0;
	eNonRandomizedTypes |= pExpectedMovable ? kPCTextureType_Movable : 0;

	pcExpectedPattern = pExpectedPattern ? TranslateDisplayMessage(pExpectedPattern->displayNameMsg) : NULL;
	pcExpectedDetail = pExpectedDetail ? TranslateDisplayMessage(pExpectedDetail->displayNameMsg) : NULL;
	pcExpectedSpecular = pExpectedSpecular ? TranslateDisplayMessage(pExpectedSpecular->displayNameMsg) : NULL;
	pcExpectedDiffuse = pExpectedDiffuse ? TranslateDisplayMessage(pExpectedDiffuse->displayNameMsg) : NULL;
	pcExpectedMovable = pExpectedMovable ? TranslateDisplayMessage(pExpectedMovable->displayNameMsg) : NULL;

	costumeTailor_GetValidTextures(pPCCostume, pMaterial, pSpecies, NULL, NULL, pGeoDef, NULL, eaUnlockedCostumes, eNonRandomizedTypes, &eaTexs, false, false, bUnlockAll);

	for (j = eaSize(&eaTexs) - 1; j >= 0 && iMatches < iExpectedTextures; j--) {
		if (eaTexs[j] && pExpectedPattern && (eaTexs[j]->eTypeFlags & kPCTextureType_Pattern) && pExpectedPattern == eaTexs[j]) {
			iMatches++;
		} else if (eaTexs[j] && pExpectedDetail && (eaTexs[j]->eTypeFlags & kPCTextureType_Detail) && pExpectedDetail == eaTexs[j]) {
			iMatches++;
		} else if (eaTexs[j] && pExpectedSpecular && (eaTexs[j]->eTypeFlags & kPCTextureType_Specular) && pExpectedSpecular == eaTexs[j]) {
			iMatches++;
		} else if (eaTexs[j] && pExpectedDiffuse && (eaTexs[j]->eTypeFlags & kPCTextureType_Diffuse) && pExpectedDiffuse == eaTexs[j]) {
			iMatches++;
		} else if (eaTexs[j] && pExpectedMovable && (eaTexs[j]->eTypeFlags & kPCTextureType_Movable) && pExpectedMovable == eaTexs[j]) {
			iMatches++;
		} else if (eaTexs[j]) {
			const char *pcTexName = TranslateDisplayMessage(eaTexs[j]->displayNameMsg);

			if (pcTexName && pcExpectedPattern && (eaTexs[j]->eTypeFlags & kPCTextureType_Pattern) && !stricmp(pcTexName, pcExpectedPattern)) {
				iMatches++;
			} else if (pcTexName && pcExpectedDetail && (eaTexs[j]->eTypeFlags & kPCTextureType_Detail) && !stricmp(pcTexName, pcExpectedDetail)) {
				iMatches++;
			} else if (pcTexName && pcExpectedSpecular && (eaTexs[j]->eTypeFlags & kPCTextureType_Specular) && !stricmp(pcTexName, pcExpectedSpecular)) {
				iMatches++;
			} else if (pcTexName && pcExpectedDiffuse && (eaTexs[j]->eTypeFlags & kPCTextureType_Diffuse) && !stricmp(pcTexName, pcExpectedDiffuse)) {
				iMatches++;
			} else if (pcTexName && pcExpectedMovable && (eaTexs[j]->eTypeFlags & kPCTextureType_Movable) && !stricmp(pcTexName, pcExpectedMovable)) {
				iMatches++;
			}
		}
	}

	eaDestroy(&eaTexs);

	return iMatches == iExpectedTextures;
}

static bool costumeRandom_GeometrySatisfiesExpectations(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll, PCGeometryDef *pGeoDef, PCMaterialDef *pExpectedMaterial, PCTextureDef *pExpectedPattern, PCTextureDef *pExpectedDetail, PCTextureDef *pExpectedSpecular, PCTextureDef *pExpectedDiffuse, PCTextureDef *pExpectedMovable)
{
	PCMaterialDef **eaMaterials = NULL;
	int j;

	if (pExpectedMaterial) {
		const char *pcExpectedMaterial = TranslateDisplayMessage(pExpectedMaterial->displayNameMsg);

		costumeTailor_GetValidMaterials(pPCCostume, pGeoDef, pSpecies, NULL, NULL, eaUnlockedCostumes, &eaMaterials, false, false, bUnlockAll);

		// The expected material is available
		for (j = eaSize(&eaMaterials) - 1; j >= 0; j--) {
			if (eaMaterials[j] == pExpectedMaterial) {
				break;
			} else if (pcExpectedMaterial) {
				const char *pcMaterialName = TranslateDisplayMessage(eaMaterials[j]->displayNameMsg);

				if (pcMaterialName && pcExpectedMaterial && !stricmp(pcMaterialName, pcExpectedMaterial)) {
					break;
				}
			}
		}

		eaDestroy(&eaMaterials);

		return j >= 0;
	} else if (pExpectedPattern || pExpectedDetail || pExpectedSpecular || pExpectedDiffuse || pExpectedMovable) {
		costumeTailor_GetValidMaterials(pPCCostume, pGeoDef, pSpecies, NULL, NULL, eaUnlockedCostumes, &eaMaterials, false, false, bUnlockAll);

		// At least one material must satisfy the expectations
		for (j = eaSize(&eaMaterials) - 1; j >= 0; j--) {
			if (costumeRandom_MaterialSatisfiesExpectations(pPCCostume, pSpecies, eaUnlockedCostumes, bUnlockAll, pGeoDef, eaMaterials[j], pExpectedPattern, pExpectedDetail, pExpectedSpecular, pExpectedDiffuse, pExpectedMovable)) {
				break;
			}
		}

		eaDestroy(&eaMaterials);

		return j >= 0;
	}

	return true;
}

__forceinline static bool costumeRandom_GeometrySatisfiesPartExpectations(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll, PCGeometryDef *pGeoDef, PCPart *pPart)
{
	PCTextureDef *pExpectedPattern;
	PCTextureDef *pExpectedDetail;
	PCTextureDef *pExpectedSpecular;
	PCTextureDef *pExpectedDiffuse;
	PCTextureDef *pExpectedMovable;
	PCMaterialDef *pExpectedMaterial;

	if (!pPart || !(pPart->eControlledRandomLocks & (kPCControlledRandomLock_Pattern | kPCControlledRandomLock_Detail | kPCControlledRandomLock_Specular | kPCControlledRandomLock_Diffuse | kPCControlledRandomLock_Movable | kPCControlledRandomLock_Material))) {
		return true;
	}

	// Deal with locking of textures
	pExpectedPattern = (pPart->eControlledRandomLocks & kPCControlledRandomLock_Pattern) ? GET_REF(pPart->hPatternTexture) : NULL;
	pExpectedDetail = (pPart->eControlledRandomLocks & kPCControlledRandomLock_Detail) ? GET_REF(pPart->hDetailTexture) : NULL;
	pExpectedSpecular = (pPart->eControlledRandomLocks & kPCControlledRandomLock_Specular) ? GET_REF(pPart->hSpecularTexture) : NULL;
	pExpectedDiffuse = (pPart->eControlledRandomLocks & kPCControlledRandomLock_Diffuse) ? GET_REF(pPart->hDiffuseTexture) : NULL;
	pExpectedMovable = (pPart->eControlledRandomLocks & kPCControlledRandomLock_Movable) && pPart->pMovableTexture ? GET_REF(pPart->pMovableTexture->hMovableTexture) : NULL;
	pExpectedMaterial = (pPart->eControlledRandomLocks & kPCControlledRandomLock_Material) ? GET_REF(pPart->hMatDef) : NULL;

	return costumeRandom_GeometrySatisfiesExpectations(pPCCostume, pSpecies, eaUnlockedCostumes, bUnlockAll, pGeoDef, pExpectedMaterial, pExpectedPattern, pExpectedDetail, pExpectedSpecular, pExpectedDiffuse, pExpectedMovable);
}

__forceinline static void costumeRandom_GetValidMaterialsWithExpectations(NOCONST(PlayerCostume) *pPCCostume, PCGeometryDef *pGeoDef, SpeciesDef *pSpecies, PCGeometryDef *pMirrorGeo, PCBoneGroup *pBoneGroup, PlayerCostume **eaUnlockedCostumes, PCMaterialDef ***peaMats, bool bMirrorMode, bool bSortDisplay, bool bUnlockAll, PCTextureDef *pExpectedPattern, PCTextureDef *pExpectedDetail, PCTextureDef *pExpectedSpecular, PCTextureDef *pExpectedDiffuse, PCTextureDef *pExpectedMovable, bool bGlow[4])
{
	PCMaterialDef **eaMaterials = NULL;
	int j;

	costumeTailor_GetValidMaterials(pPCCostume, pGeoDef, pSpecies, pMirrorGeo, pBoneGroup, eaUnlockedCostumes, peaMats, bMirrorMode, bSortDisplay, bUnlockAll);

	if (!pExpectedPattern && !pExpectedDetail && !pExpectedSpecular && !pExpectedDiffuse && !pExpectedMovable) {
		return;
	}

	for (j = eaSize(&eaMaterials) - 1; j >= 0; j--) {
		if (bGlow[0] && !(eaMaterials[j]->pColorOptions && eaMaterials[j]->pColorOptions->bAllowGlow[0])
			|| bGlow[1] && !(eaMaterials[j]->pColorOptions && eaMaterials[j]->pColorOptions->bAllowGlow[1])
			|| bGlow[2] && !(eaMaterials[j]->pColorOptions && eaMaterials[j]->pColorOptions->bAllowGlow[2])
			|| bGlow[3] && !(eaMaterials[j]->pColorOptions && eaMaterials[j]->pColorOptions->bAllowGlow[3])
			|| costumeRandom_MaterialSatisfiesExpectations(pPCCostume, pSpecies, eaUnlockedCostumes, bUnlockAll, pGeoDef, eaMaterials[j], pExpectedPattern, pExpectedDetail, pExpectedSpecular, pExpectedDiffuse, pExpectedMovable)) {
			eaRemove(&eaMaterials, j);
		}
	}
}

static bool costumeRandom_CategorySatisfiesPart(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll, NOCONST(PCPart) *pPart, PCRegion *pRegion, PCCategory *pCategory, PCStyle **eaStyles)
{
	PCBoneDef *pBone = pPart ? GET_REF(pPart->hBoneDef) : NULL;
	PCGeometryDef **eaGeos = NULL;
	PCCategory *pBoneCategory = NULL;
	PCGeometryDef *pGeoNoneDef = RefSystem_ReferentFromString(g_hCostumeGeometryDict, "None");
	int j = -1;
	int i;

	if (!pPCCostume || !pBone || !pPart || !pPart->eControlledRandomLocks) {
		return true;
	}

	for (j=eaSize(&pPCCostume->eaRegionCategories)-1; j>=0; j--) {
		if (GET_REF(pBone->hRegion) == GET_REF(pPCCostume->eaRegionCategories[j]->hRegion)) {
			pBoneCategory = GET_REF(pPCCostume->eaRegionCategories[j]->hCategory);
			break;
		}
	}

	for (j=eaSize(&pCategory->eaExcludedCategories)-1; j>=0; j--) {
		if (GET_REF(pCategory->eaExcludedCategories[j]->hCategory) == pBoneCategory) {
			return false;
		}
	}

	for (j=eaSize(&pCategory->eaExcludedBones)-1; j>=0; j--) {
		if (pBone == GET_REF(pCategory->eaExcludedBones[j]->hBone) && GET_REF(pPart->hGeoDef) != pGeoNoneDef) {
			return false;
		}
	}

	if (GET_REF(pBone->hRegion) != pRegion) {
		// No geometry check if the regions are different, because
		// in theory the locked items won't be available in that
		// different region.
		return true;
	}

	if (!pBone->bIsChildBone) {
		costumeTailor_GetValidGeos(pPCCostume, GET_REF(pPCCostume->hSkeleton), pBone, pCategory, pSpecies, eaUnlockedCostumes, &eaGeos, false, false, false, bUnlockAll);

		// Handle possibly empty case, should fail material tests also if this happens
		if (!eaSize(&eaGeos)) {
			if (pGeoNoneDef) {
				eaPush(&eaGeos, pGeoNoneDef);
			}
		}
	} else {
		PCGeometryDef *pParentGeo = NULL;

		// Get parent geometry
		for(i=eaSize(&pPCCostume->eaParts)-1; i>=0 && !pParentGeo; --i) {
			PCBoneDef *pPartBone = NULL;

			if (!pPCCostume->eaParts[i]) {
				continue;
			}

			pPartBone = GET_REF(pPCCostume->eaParts[i]->hBoneDef);
			if (pPartBone) {
				for(j=eaSize(&pPartBone->eaChildBones)-1; j>=0; --j) {
					if (GET_REF(pPartBone->eaChildBones[j]->hChildBone) == pBone) {
						pParentGeo = GET_REF(pPCCostume->eaParts[i]->hGeoDef);
						break;
					}
				}
			}
		}

		costumeTailor_GetValidChildGeos(pPCCostume, pCategory, pParentGeo, pBone, pSpecies, eaUnlockedCostumes, &eaGeos, false, bUnlockAll);
	}

	if ((pPart->eControlledRandomLocks & kPCControlledRandomLock_Geometry) && GET_REF(pPart->hGeoDef))
	{
		PCGeometryDef *pGeoDef = GET_REF(pPart->hGeoDef);
		const char *pcExpectedGeometry = TranslateDisplayMessage(pGeoDef->displayNameMsg);

		// See if the geometry exists in the category
		for (j = eaSize(&eaGeos) - 1; j >= 0; j--)
		{
			if (eaGeos[j] == pGeoDef)
			{
				break;
			}
			else if (pcExpectedGeometry)
			{
				const char *pcGeometry = TranslateDisplayMessage(eaGeos[j]->displayNameMsg);

				if (pcGeometry && pcExpectedGeometry && !stricmp(pcGeometry, pcExpectedGeometry))
				{
					break;
				}
			}
		}
	}
	else
	{
		// See if there is a geometry satisfies the expectations and the expectations of the cloth layer (if there is one)
		for (j = eaSize(&eaGeos) - 1; j >= 0; j--) {
			if (costumeRandom_GeometrySatisfiesPartExpectations(pPCCostume, pSpecies, eaUnlockedCostumes, bUnlockAll, eaGeos[j], (PCPart *)pPart)) {
				if (!pPart->pClothLayer || costumeRandom_GeometrySatisfiesPartExpectations(pPCCostume, pSpecies, eaUnlockedCostumes, bUnlockAll, eaGeos[j], (PCPart *)pPart->pClothLayer)) {
					break;
				}
			}
		}
	}

	// Check to see if the category matches the style
	if (eaSize(&eaStyles) > 0) {
		// But only if the geometry isn't locked
		if (!(pPart->eControlledRandomLocks & kPCControlledRandomLock_Geometry)) {
			for (i=eaSize(&eaGeos) - 1; i>=0; i--) {
				if (!costumeRandom_StyleMatches(eaStyles, eaGeos[i]->eaStyles)) {
					eaRemove(&eaGeos, i);
				}
			}
		}
		i = eaSize(&eaGeos);
	} else {
		i = 1;
	}

	eaDestroy(&eaGeos);

	return j >= 0 && i > 0;
}

__forceinline static PCGeometryDef *costumeRandom_PickRandomGeo(PCGeometryDef **eaGeos)
{
	int i;
	F32 fWeight = 0.0;
	for(i = eaSize(&eaGeos) - 1; i >= 0; i--) {
		costumeRandom_AddWeight(&fWeight, eaGeos[i]->fRandomWeight);
	}
	if (fWeight > 0) {
		F32 fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
		for(i=eaSize(&eaGeos)-1; i>=0; i--) {
			if (costumeRandom_MatchWeight(&fValue, eaGeos[i]->fRandomWeight)) {
				return eaGeos[i];
			}
		}
	}
	return NULL;
}

__forceinline static PCMaterialDef *costumeRandom_PickRandomMaterial(PCMaterialDef **eaMaterials)
{
	int i;
	F32 fWeight = 0.0;
	for(i=eaSize(&eaMaterials)-1; i>=0; i--) {
		costumeRandom_AddWeight(&fWeight, eaMaterials[i]->fRandomWeight);
	}
	if (fWeight > 0) {
		F32 fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
		for(i=eaSize(&eaMaterials)-1; i>=0; i--) {
			if (costumeRandom_MatchWeight(&fValue, eaMaterials[i]->fRandomWeight)) {
				return eaMaterials[i];
			}
		}
	}
	return NULL;
}

__forceinline static PCTextureDef *costumeRandom_PickTexture(PCTextureDef **eaTextures, PCGeometryDef *pGeometry, PCMaterialDef *pMaterial, SpeciesDef *pSpecies, PCTextureDef *pMirrorMaterial, PCTextureType eTypeFlags, const char **ppcGroup)
{
	PCTextureDef *pResult = NULL;
	PCTextureDef **eaValid = NULL;
	PCTextureDef *pTextureNone = RefSystem_ReferentFromString(g_hCostumeTextureDict, "None");
	int i;

	if (pTextureNone && (!costumeTailor_DoesMaterialRequireType(pGeometry, pMaterial, pSpecies, eTypeFlags))) {
		eaPush(&eaValid, pTextureNone);
	}

	for (i=eaSize(&eaTextures)-1; i>=0; i--) {
		if ((eaTextures[i]->eTypeFlags & eTypeFlags)) {
			eaPush(&eaValid, eaTextures[i]);
		}
	}

	if (!eaSize(&eaValid)) {
		return NULL;
	}

	if (pMirrorMaterial) {
		int iMirror = costumeTailor_GetMatchingTexIndex(pMirrorMaterial, eaValid);
		if (iMirror >= 0) {
			pResult = eaValid[iMirror];
		}
	}

	if (!pResult) {
		F32 fWeight = 0.0;
		for(i=eaSize(&eaValid)-1; i>=0; i--) {
			costumeRandom_AddWeight(&fWeight, eaValid[i] ? eaValid[i]->fRandomWeight : -1);
		}
		if (fWeight > 0) {
			F32 fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
			for(i=eaSize(&eaValid)-1; i>=0; i--) {
				if (costumeRandom_MatchWeight(&fValue, eaValid[i] ? eaValid[i]->fRandomWeight : -1)) {
					pResult = eaValid[i];
					break;
				}
			}
		}
	}

	eaDestroy(&eaValid);

	return pResult;
}

static void costumeRandom_ControlledRandomMaterialTexture(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, const PCSlotType *pSlotType, Guild *pGuild, PlayerCostume **eaUnlockedCostumes, PCPart *pMirrorPart, PCGeometryDef *pCurrentGeometry, PCMaterialDef *pExpectedMaterial, PCTextureDef *pExpectedPattern, PCTextureDef *pExpectedDetail, PCTextureDef *pExpectedSpecular, PCTextureDef *pExpectedDiffuse, PCTextureDef *pExpectedMovable, bool bUnlockAll)
{
	PCBoneDef *pBone = GET_REF(pPart->hBoneDef);
	bool bShowGuild = pGuild ? (randomIntRange(0,1) ? true : false) : false;
	int i, j;

	if (!pPart) {
		return;
	}

	if (!pExpectedMaterial) {
		PCMaterialDef **eaMaterials = NULL;
		PCMaterialDef *pMaterial = NULL;
		bool bGlow[4] = { 0,0,0,0 };
		
		if (pPart->pCustomColors) {
			bGlow[0] = !!(pPart->eControlledRandomLocks & kPCControlledRandomLock_Color0) && !!pPart->pCustomColors->glowScale[0];
			bGlow[1] = !!(pPart->eControlledRandomLocks & kPCControlledRandomLock_Color1) && !!pPart->pCustomColors->glowScale[1];
			bGlow[2] = !!(pPart->eControlledRandomLocks & kPCControlledRandomLock_Color2) && !!pPart->pCustomColors->glowScale[2];
			bGlow[3] = !!(pPart->eControlledRandomLocks & kPCControlledRandomLock_Color3) && !!pPart->pCustomColors->glowScale[3];
		};

		// Fill in material list
		costumeRandom_GetValidMaterialsWithExpectations(pPCCostume, pCurrentGeometry, pSpecies, NULL, NULL, eaUnlockedCostumes, &eaMaterials, false, false, bUnlockAll, pExpectedPattern, pExpectedDetail, pExpectedSpecular, pExpectedDiffuse, pExpectedMovable, bGlow);

		// Look for the mirror
		if (pMirrorPart && GET_REF(pMirrorPart->hMatDef)) {
			int iMatMatch = costumeTailor_GetMatchingMatIndex(GET_REF(pMirrorPart->hMatDef), eaMaterials);
			if (iMatMatch >= 0) {
				pMaterial = eaMaterials[iMatMatch];
			}
		}

		// Random choice
		if (!pMaterial) {
			pMaterial = costumeRandom_PickRandomMaterial(eaMaterials);
		}

		if (pMaterial) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, pMaterial, pPart->hMatDef);
		} else {
			REMOVE_HANDLE(pPart->hMatDef);
		}
	}

	// Randomize the textures
	if ((!pExpectedPattern || !pExpectedDetail || !pExpectedSpecular || !pExpectedDiffuse || !pExpectedMovable) && GET_REF(pPart->hMatDef)) {
		PCTextureDef **eaMaterialTextures = NULL;
		PCTextureDef *pTextureNone = RefSystem_ReferentFromString(g_hCostumeTextureDict, "None");
		PCMaterialDef *pMaterial = GET_REF(pPart->hMatDef);
		PCTextureType eRandomizedTypes = 0;
		PCTextureDef *pResult = NULL;

		eRandomizedTypes |= pExpectedPattern ? 0 : kPCTextureType_Pattern;
		eRandomizedTypes |= pExpectedDetail ? 0 : kPCTextureType_Detail;
		eRandomizedTypes |= pExpectedSpecular ? 0 : kPCTextureType_Specular;
		eRandomizedTypes |= pExpectedDiffuse ? 0 : kPCTextureType_Diffuse;
		eRandomizedTypes |= pExpectedMovable ? 0 : kPCTextureType_Movable;

		costumeTailor_GetValidTextures(pPCCostume, pMaterial, pSpecies, NULL, NULL, GET_REF(pPart->hGeoDef), NULL, eaUnlockedCostumes, eRandomizedTypes, &eaMaterialTextures, false, false, bUnlockAll);

		// Remove the possible "None" texture.
		if (eaSize(&eaMaterialTextures) > 0 && eaMaterialTextures[0] == pTextureNone) {
			eaRemove(&eaMaterialTextures, 0);
		}

		// Remove textures that don't satisfy the locks
		for(i=eaSize(&eaMaterialTextures)-1; i>=0; i--) {
			if (!eaMaterialTextures[i]) {
				eaRemove(&eaMaterialTextures, i);
			}
		}

		if (!pExpectedPattern) {
			if (pBone && pBone->bIsGuildEmblemBone)
			{
				if (bShowGuild)
				{
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pGuild->pcEmblem, pPart->hPatternTexture);
					if (!pPart->pTextureValues) {
						pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
					}
					if (pPart->pTextureValues) {
						pPart->pTextureValues->fPatternValue = pGuild->fEmblemRotation;
					}
					*((U32*)pPart->color2) = pGuild->iEmblemColor0;
					*((U32*)pPart->color3) = pGuild->iEmblemColor1;
					pPart->eColorLink = kPCColorLink_None;
				}
				else
				{
					//Strip invalid textures
					for(i=eaSize(&eaMaterialTextures)-1; i>=0; i--) {
						for (j = eaSize(&g_GuildEmblems.eaEmblems)-1; j >= 0; --j)
						{
							if (!stricmp(eaMaterialTextures[i]->pcName,REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[j]->hTexture))) {
								eaRemove(&eaMaterialTextures, i);
							}
						}
					}
				}
			}
			if ((pBone && !pBone->bIsGuildEmblemBone) || (!bShowGuild))
			{
				pResult = costumeRandom_PickTexture(eaMaterialTextures, GET_REF(pPart->hGeoDef), pMaterial, pSpecies, pMirrorPart ? GET_REF(pMirrorPart->hPatternTexture) : NULL, kPCTextureType_Pattern, NULL);
				if (pResult) {
					SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pResult, pPart->hPatternTexture);
				} else {
					REMOVE_HANDLE(pPart->hPatternTexture);
				}
				if (pResult && pResult->pValueOptions && pResult->pValueOptions->pcValueConstant)
				{
					if (!pPart->pTextureValues) {
						pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
					}
					if (pPart->pTextureValues) {
						pPart->pTextureValues->fPatternValue = randomF32() * 100.0f;
					}
				}
			}
		}

		if (!pExpectedDetail) {
			if (pBone && pBone->bIsGuildEmblemBone)
			{
				if (bShowGuild)
				{
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pGuild->pcEmblem3, pPart->hDetailTexture);
				}
				else
				{
					//Strip invalid textures
					for(i=eaSize(&eaMaterialTextures)-1; i>=0; i--) {
						for (j = eaSize(&g_GuildEmblems.eaEmblems)-1; j >= 0; --j)
						{
							if (!stricmp(eaMaterialTextures[i]->pcName,REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[j]->hTexture))) {
								eaRemove(&eaMaterialTextures, i);
							}
						}
					}
				}
			}
			if ((pBone && !pBone->bIsGuildEmblemBone) || (!bShowGuild))
			{
				pResult = costumeRandom_PickTexture(eaMaterialTextures, GET_REF(pPart->hGeoDef), pMaterial, pSpecies, pMirrorPart ? GET_REF(pMirrorPart->hDetailTexture) : NULL, kPCTextureType_Detail, NULL);
				if (pResult) {
					SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pResult, pPart->hDetailTexture);
				} else {
					REMOVE_HANDLE(pPart->hDetailTexture);
				}
				if (pResult && pResult->pValueOptions && pResult->pValueOptions->pcValueConstant)
				{
					if (!pPart->pTextureValues) {
						pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
					}
					if (pPart->pTextureValues) {
						pPart->pTextureValues->fDetailValue = randomF32() * 100.0f;
					}
				}
			}
		}

		if (!pExpectedSpecular) {
			pResult = costumeRandom_PickTexture(eaMaterialTextures, GET_REF(pPart->hGeoDef), pMaterial, pSpecies, pMirrorPart ? GET_REF(pMirrorPart->hSpecularTexture) : NULL, kPCTextureType_Specular, NULL);
			if (pResult) {
				SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pResult, pPart->hSpecularTexture);
			} else {
				REMOVE_HANDLE(pPart->hSpecularTexture);
			}
			if (pResult && pResult->pValueOptions && pResult->pValueOptions->pcValueConstant)
			{
				if (!pPart->pTextureValues) {
					pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
				}
				if (pPart->pTextureValues) {
					pPart->pTextureValues->fSpecularValue = randomF32() * 100.0f;
				}
			}
		}

		if (!pExpectedDiffuse) {
			pResult = costumeRandom_PickTexture(eaMaterialTextures, GET_REF(pPart->hGeoDef), pMaterial, pSpecies, pMirrorPart ? GET_REF(pMirrorPart->hDiffuseTexture) : NULL, kPCTextureType_Diffuse, NULL);
			if (pResult) {
				SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pResult, pPart->hDiffuseTexture);
			} else {
				REMOVE_HANDLE(pPart->hDiffuseTexture);
			}
			if (pResult && pResult->pValueOptions && pResult->pValueOptions->pcValueConstant)
			{
				if (!pPart->pTextureValues) {
					pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
				}
				if (pPart->pTextureValues) {
					pPart->pTextureValues->fDiffuseValue = randomF32() * 100.0f;
				}
			}
		}

		if (!pExpectedMovable) {
			if (pBone && pBone->bIsGuildEmblemBone)
			{
				if (bShowGuild)
				{
					if (!pPart->pMovableTexture) {
						pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
					}
					if (pPart->pMovableTexture) {
						SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pGuild->pcEmblem2, pPart->pMovableTexture->hMovableTexture);
						pPart->pMovableTexture->fMovableRotation = pGuild->fEmblem2Rotation;
						pPart->pMovableTexture->fMovableX = pGuild->fEmblem2X;
						pPart->pMovableTexture->fMovableY = pGuild->fEmblem2Y;
						pPart->pMovableTexture->fMovableScaleX = pGuild->fEmblem2ScaleX;
						pPart->pMovableTexture->fMovableScaleY = pGuild->fEmblem2ScaleY;
					}
					*((U32*)pPart->color0) = pGuild->iEmblem2Color0;
					*((U32*)pPart->color1) = pGuild->iEmblem2Color1;
					pPart->eColorLink = kPCColorLink_None;
				}
				else if (pPart->pMovableTexture)
				{
					//Strip invalid textures
					for(i=eaSize(&eaMaterialTextures)-1; i>=0; i--) {
						for (j = eaSize(&g_GuildEmblems.eaEmblems)-1; j >= 0; --j)
						{
							if (!stricmp(eaMaterialTextures[i]->pcName,REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[j]->hTexture))) {
								eaRemove(&eaMaterialTextures, i);
							}
						}
					}
				}
			}
			if ((pBone && !pBone->bIsGuildEmblemBone) || (!bShowGuild))
			{
				pResult = costumeRandom_PickTexture(eaMaterialTextures, GET_REF(pPart->hGeoDef), pMaterial, pSpecies, pMirrorPart && pMirrorPart->pMovableTexture ? GET_REF(pMirrorPart->pMovableTexture->hMovableTexture) : NULL, kPCTextureType_Movable, NULL);
				if (pResult) {
					if (!pPart->pMovableTexture) {
						pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
					}
					SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pResult, pPart->pMovableTexture->hMovableTexture);
				} else if (pPart->pMovableTexture) {
					REMOVE_HANDLE(pPart->pMovableTexture->hMovableTexture);
				}
				if (pResult && pPart->pMovableTexture)
				{
					F32 fMovableMinX, fMovableMaxX, fMovableMinY, fMovableMaxY;
					F32 fMovableMinScaleX, fMovableMaxScaleX, fMovableMinScaleY, fMovableMaxScaleY;
					bool bMovableCanEditPosition, bMovableCanEditRotation, bMovableCanEditScale;

					costumeTailor_GetTextureMovableValues((PCPart*)pPart, pResult, pSpecies,
													&fMovableMinX, &fMovableMaxX, &fMovableMinY, &fMovableMaxY,
													&fMovableMinScaleX, &fMovableMaxScaleX, &fMovableMinScaleY, &fMovableMaxScaleY,
													&bMovableCanEditPosition, &bMovableCanEditRotation, &bMovableCanEditScale);

					if (pResult && pResult->pValueOptions && pResult->pValueOptions->pcValueConstant)
					{
						pPart->pMovableTexture->fMovableValue = randomF32() * 100.0f;
					}
					if (bMovableCanEditPosition)
					{
						pPart->pMovableTexture->fMovableX = randomF32() * 100.0f;
						pPart->pMovableTexture->fMovableY = randomF32() * 100.0f;
					}
					else if (pResult->pMovableOptions)
					{
						pPart->pMovableTexture->fMovableX = (fMovableMaxX==fMovableMinX) ? 0 : (((pResult->pMovableOptions->fMovableDefaultX - fMovableMinX) * 200.0f)/(fMovableMaxX - fMovableMinX)) - 100.0f;
						pPart->pMovableTexture->fMovableY = (fMovableMaxY==fMovableMinY) ? 0 : (((pResult->pMovableOptions->fMovableDefaultY - fMovableMinY) * 200.0f)/(fMovableMaxY - fMovableMinY)) - 100.0f;
					}
					if (bMovableCanEditScale)
					{
						pPart->pMovableTexture->fMovableScaleX = randomPositiveF32() * 100.0f;
						pPart->pMovableTexture->fMovableScaleY = randomPositiveF32() * 100.0f;
					}
					else if (pResult->pMovableOptions)
					{
						pPart->pMovableTexture->fMovableScaleX = (fMovableMaxScaleX==fMovableMinScaleX) ? 0 : (((pResult->pMovableOptions->fMovableDefaultScaleX - fMovableMinScaleX) * 100.0f)/(fMovableMaxScaleX - fMovableMinScaleX));
						pPart->pMovableTexture->fMovableScaleY = (fMovableMaxScaleY==fMovableMinScaleY) ? 0 : (((pResult->pMovableOptions->fMovableDefaultScaleY - fMovableMinScaleY) * 100.0f)/(fMovableMaxScaleY - fMovableMinScaleY));
					}
					if (bMovableCanEditRotation)
					{
						pPart->pMovableTexture->fMovableRotation = randomPositiveF32() * 100.0f;
					}
					else if (pResult->pMovableOptions)
					{
						pPart->pMovableTexture->fMovableRotation = pResult->pMovableOptions->fMovableDefaultRotation;
					}
				}
			}
		}

		// Clean the lists
		eaDestroy(&eaMaterialTextures);

	} else if (!GET_REF(pPart->hMatDef)) {
		REMOVE_HANDLE(pPart->hPatternTexture);
		REMOVE_HANDLE(pPart->hDetailTexture);
		REMOVE_HANDLE(pPart->hSpecularTexture);
		REMOVE_HANDLE(pPart->hDiffuseTexture);
		if (pPart->pMovableTexture) {
			REMOVE_HANDLE(pPart->pMovableTexture->hMovableTexture);
		}
	}

	if (!pGuild)
	{
		// Fill colors
		if ((pPart->eControlledRandomLocks & kPCControlledRandomLock_Colors)) {
			PCColorQuadSet *pQuadSet;
			int iColor = 0;
			PCSkeletonDef *pSkeleton = GET_REF(pPCCostume->hSkeleton);

			pQuadSet = costumeTailor_GetColorQuadSetForPart(pPCCostume, pSpecies, pPart);

			if (pQuadSet) {
				F32 fWeight = 0.0;
				for(i=eaSize(&pQuadSet->eaColorQuads)-1; i>=0; i--) {
					costumeRandom_AddWeight(&fWeight, pQuadSet->eaColorQuads[i]->fRandomWeight);
				}
				if (fWeight > 0) {
					F32 fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
					for(i=eaSize(&pQuadSet->eaColorQuads)-1; i>=0; i--) {
						if (costumeRandom_MatchWeight(&fValue, pQuadSet->eaColorQuads[i]->fRandomWeight)) {
							iColor = i;
							break;
						}
					}
				}
			}

			if (iColor >= 0 && pPart->eColorLink == kPCColorLink_None) {
				U8 glow[4] = {0, 0, 0, 0};
				assert(pQuadSet->eaColorQuads);

				// Save the glows that are locked
				if ((pPart->eControlledRandomLocks & kPCControlledRandomLock_Color0) && pPart->pCustomColors) {
					glow[0] = pPart->pCustomColors->glowScale[0];
				}
				if ((pPart->eControlledRandomLocks & kPCControlledRandomLock_Color1) && pPart->pCustomColors) {
					glow[1] = pPart->pCustomColors->glowScale[1];
				}
				if ((pPart->eControlledRandomLocks & kPCControlledRandomLock_Color2) && pPart->pCustomColors) {
					glow[2] = pPart->pCustomColors->glowScale[2];
				}
				if ((pPart->eControlledRandomLocks & kPCControlledRandomLock_Color3) && pPart->pCustomColors) {
					glow[3] = pPart->pCustomColors->glowScale[3];
				}

				// Randomize this part's color
				costumeTailor_SetPartColors(pPCCostume, pSpecies, pSlotType, pPart, 
					(pPart->eControlledRandomLocks & kPCControlledRandomLock_Color0) ? pPart->color0 : pQuadSet->eaColorQuads[iColor]->color0,
					(pPart->eControlledRandomLocks & kPCControlledRandomLock_Color1) ? pPart->color1 : pQuadSet->eaColorQuads[iColor]->color1, 
					(pPart->eControlledRandomLocks & kPCControlledRandomLock_Color2) ? pPart->color2 : pQuadSet->eaColorQuads[iColor]->color2,
					(pPart->eControlledRandomLocks & kPCControlledRandomLock_Color3) ? pPart->color3 : pQuadSet->eaColorQuads[iColor]->color3,
					glow);
			} else {
				pPart->eColorLink = kPCColorLink_All;
			}

		} else {
			pPart->eColorLink = kPCColorLink_All;
		}
	}
}

static void costumeRandom_ControlledFillRandomPart(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, const PCSlotType *pSlotType, Guild *pGuild, PlayerCostume **eaUnlockedCostumes, PCPart *pMirrorPart, PCStyle **eaStyles, bool bSortDisplay, bool bSymmetry, bool bUnlockAll)
{
	PCTextureType eRandomizedTypes = 0, eRandomizedClothTypes = 0;
	PCTextureDef *pExpectedPattern, *pExpectedClothPattern;
	PCTextureDef *pExpectedDetail, *pExpectedClothDetail;
	PCTextureDef *pExpectedSpecular, *pExpectedClothSpecular;
	PCTextureDef *pExpectedDiffuse, *pExpectedClothDiffuse;
	PCTextureDef *pExpectedMovable, *pExpectedClothMovable;
	PCMaterialDef *pExpectedMaterial, *pExpectedClothMaterial;
	NOCONST(PCPart) *pCloth = pPart ? pPart->pClothLayer : NULL;
	PCGeometryDef *pCurrentGeo;
	int i, j;
	float fWeight, fValue;
	PCBoneDef *pBone = pPart ? GET_REF(pPart->hBoneDef) : NULL;
	PCCategory *pCategory = pBone ? costumeTailor_GetCategoryForRegion((PlayerCostume *)pPCCostume, GET_REF(pBone->hRegion)) : NULL;
	PCSkeletonDef *pSkeleton = pPCCostume ? GET_REF(pPCCostume->hSkeleton) : NULL;
	bool bIsChildBone = pBone ? pBone->bIsChildBone : false;
	bool bRequired = false;
	bool bUseStyle;

	if (!pPCCostume || !pPart || !pBone || !pSkeleton) {
		return;
	}

	// Deal with locking of textures
	pExpectedPattern = (pPart->eControlledRandomLocks & kPCControlledRandomLock_Pattern) ? GET_REF(pPart->hPatternTexture) : NULL;
	pExpectedDetail = (pPart->eControlledRandomLocks & kPCControlledRandomLock_Detail) ? GET_REF(pPart->hDetailTexture) : NULL;
	pExpectedSpecular = (pPart->eControlledRandomLocks & kPCControlledRandomLock_Specular) ? GET_REF(pPart->hSpecularTexture) : NULL;
	pExpectedDiffuse = (pPart->eControlledRandomLocks & kPCControlledRandomLock_Diffuse) ? GET_REF(pPart->hDiffuseTexture) : NULL;
	pExpectedMovable = (pPart->eControlledRandomLocks & kPCControlledRandomLock_Movable) && pPart->pMovableTexture ? GET_REF(pPart->pMovableTexture->hMovableTexture) : NULL;
	pExpectedMaterial = (pPart->eControlledRandomLocks & kPCControlledRandomLock_Material) ? GET_REF(pPart->hMatDef) : NULL;

	eRandomizedTypes |= pExpectedPattern ? 0 : kPCTextureType_Pattern;
	eRandomizedTypes |= pExpectedDetail ? 0 : kPCTextureType_Detail;
	eRandomizedTypes |= pExpectedSpecular ? 0 : kPCTextureType_Specular;
	eRandomizedTypes |= pExpectedDiffuse ? 0 : kPCTextureType_Diffuse;
	eRandomizedTypes |= pExpectedMovable ? 0 : kPCTextureType_Movable;

	if (pCloth) {
		pExpectedClothPattern = (pCloth->eControlledRandomLocks & kPCControlledRandomLock_Pattern) ? GET_REF(pCloth->hPatternTexture) : NULL;
		pExpectedClothDetail = (pCloth->eControlledRandomLocks & kPCControlledRandomLock_Detail) ? GET_REF(pCloth->hDetailTexture) : NULL;
		pExpectedClothSpecular = (pCloth->eControlledRandomLocks & kPCControlledRandomLock_Specular) ? GET_REF(pCloth->hSpecularTexture) : NULL;
		pExpectedClothDiffuse = (pCloth->eControlledRandomLocks & kPCControlledRandomLock_Diffuse) ? GET_REF(pCloth->hDiffuseTexture) : NULL;
		pExpectedClothMovable = (pCloth->eControlledRandomLocks & kPCControlledRandomLock_Movable) && pCloth->pMovableTexture ? GET_REF(pCloth->pMovableTexture->hMovableTexture) : NULL;
		pExpectedClothMaterial = (pCloth->eControlledRandomLocks & kPCControlledRandomLock_Material) ? GET_REF(pCloth->hMatDef) : NULL;

		eRandomizedClothTypes |= pExpectedClothPattern ? 0 : kPCTextureType_Pattern;
		eRandomizedClothTypes |= pExpectedClothDetail ? 0 : kPCTextureType_Detail;
		eRandomizedClothTypes |= pExpectedClothSpecular ? 0 : kPCTextureType_Specular;
		eRandomizedClothTypes |= pExpectedClothDiffuse ? 0 : kPCTextureType_Diffuse;
		eRandomizedClothTypes |= pExpectedClothMovable ? 0 : kPCTextureType_Movable;
	} else {
		pExpectedClothPattern = NULL;
		pExpectedClothDetail = NULL;
		pExpectedClothSpecular = NULL;
		pExpectedClothDiffuse = NULL;
		pExpectedClothMovable = NULL;
		pExpectedClothMaterial = NULL;
	}

	// Is the part required?
	for(i=eaSize(&pSkeleton->eaRequiredBoneDefs)-1; i>=0; i--) {
		if (GET_REF(pSkeleton->eaRequiredBoneDefs[i]->hBone) == pBone) {
			bRequired = true;
			break;
		}
	}
	if (pCategory && !bRequired) {
		for(i=eaSize(&pCategory->eaRequiredBones)-1; i>=0; i--) {
			if (GET_REF(pCategory->eaRequiredBones[i]->hBone) == pBone) {
				bRequired = true;
				break;
			}
		}
	}
	if (pSpecies && !bRequired)
	{
		for(i=eaSize(&pSpecies->eaBoneData)-1; i>=0; i--) {
			if (GET_REF(pSpecies->eaBoneData[i]->hBone) == pBone) {
				bRequired = pSpecies->eaBoneData[i]->bRequires;
				break;
			}
		}
	}

	// Start randomizing stuff

	if (!(pPart->eControlledRandomLocks & kPCControlledRandomLock_Geometry)) {
		PCGeometryDef *pGeo = NULL;

		if (pMirrorPart) {
			PCGeometryDef **eaGeometry = NULL;
			PCCategory *pTargetCategory = costumeTailor_GetCategoryForRegion((PlayerCostume *)pPCCostume, GET_REF(pBone->hRegion));
			int iMirror;

			// Use mirror geometry
			costumeTailor_GetValidGeos(pPCCostume, GET_REF(pPCCostume->hSkeleton), pBone, pTargetCategory, pSpecies, eaUnlockedCostumes, &eaGeometry, false, false, false, bUnlockAll);
			iMirror = costumeTailor_GetMatchingGeoIndex(GET_REF(pMirrorPart->hGeoDef), eaGeometry);
			pGeo = iMirror >= 0 ? eaGeometry[iMirror] : NULL;

			eaDestroy(&eaGeometry);
		}

		if (!pMirrorPart || !pGeo && bRequired) {
			PCGeometryDef **eaGeos = NULL;

			if (!bIsChildBone) {
				costumeTailor_GetValidGeos(pPCCostume, GET_REF(pPCCostume->hSkeleton), pBone, pCategory, pSpecies, eaUnlockedCostumes, &eaGeos, false, false, bSortDisplay, bUnlockAll);
			} else {
				PCGeometryDef *pParentGeo = NULL;

				// Get parent geometry
				for(i=eaSize(&pPCCostume->eaParts)-1; i>=0 && !pParentGeo; i--) {
					PCBoneDef *pPartBone = NULL;

					if (!pPCCostume->eaParts[i]) {
						continue;
					}

					pPartBone = GET_REF(pPCCostume->eaParts[i]->hBoneDef);
					if (pPartBone) {
						for(j=eaSize(&pPartBone->eaChildBones)-1; j>=0; --j) {
							if (GET_REF(pPartBone->eaChildBones[j]->hChildBone) == pBone) {
								pParentGeo = GET_REF(pPCCostume->eaParts[i]->hGeoDef);
								break;
							}
						}
					}
				}
				if (!pParentGeo) {
					return; // no parent, no child
				}

				costumeTailor_GetValidChildGeos(pPCCostume, pCategory, pParentGeo, pBone, pSpecies, eaUnlockedCostumes, &eaGeos, bSortDisplay, bUnlockAll);
			}

			if (pExpectedMaterial || pExpectedPattern || pExpectedDetail || pExpectedSpecular || pExpectedDiffuse || pExpectedMovable) {
				// Filter geometry based on allowed materials.
				for(i=eaSize(&eaGeos)-1; i>=0; i--) {
					if (!costumeRandom_GeometrySatisfiesExpectations(pPCCostume, pSpecies, eaUnlockedCostumes, bUnlockAll, eaGeos[i], pExpectedMaterial, pExpectedPattern, pExpectedDetail, pExpectedSpecular, pExpectedDiffuse, pExpectedMovable)) {
						eaRemove(&eaGeos, i);
					}
				}
			}

			if (pExpectedClothMaterial || pExpectedClothPattern || pExpectedClothDetail || pExpectedClothSpecular || pExpectedClothDiffuse || pExpectedClothMovable) {
				// Filter geometry based on allowed cloth layer materials.
				for(i=eaSize(&eaGeos)-1; i>=0; i--) {
					if (!costumeRandom_GeometrySatisfiesExpectations(pPCCostume, pSpecies, eaUnlockedCostumes, bUnlockAll, eaGeos[i], pExpectedClothMaterial, pExpectedClothPattern, pExpectedClothDetail, pExpectedClothSpecular, pExpectedClothDiffuse, pExpectedClothMovable)) {
						eaRemove(&eaGeos, i);
					}
				}
			}

			// Randomly pick geometry
			fWeight = 0.0;
			bUseStyle = true;
			for(i = eaSize(&eaGeos) - 1; i >= 0; i--) {
				if (!eaSize(&eaStyles) || costumeRandom_StyleMatches(eaStyles, eaGeos[i]->eaStyles)) {
					costumeRandom_AddWeight(&fWeight, eaGeos[i]->fRandomWeight);
				}
			}
			if (fWeight == 0 && eaSize(&eaGeos)) {
				bUseStyle = false;
				for(i = eaSize(&eaGeos) - 1; i >= 0; i--) {
					costumeRandom_AddWeight(&fWeight, eaGeos[i]->fRandomWeight);
				}
			}
			if (fWeight > 0) {
				fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
				for(i=eaSize(&eaGeos)-1; i>=0; i--) {
					if ((!bUseStyle || !eaSize(&eaStyles) || costumeRandom_StyleMatches(eaStyles, eaGeos[i]->eaStyles))
						&& costumeRandom_MatchWeight(&fValue, eaGeos[i]->fRandomWeight)) {
						pGeo = eaGeos[i];
						break;
					}
				}
			}

			eaDestroy(&eaGeos);
		}

		// Set the geometry
		if (!pGeo) {
			return;
		}
		SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, pGeo, pPart->hGeoDef);
	}

	pCurrentGeo = GET_REF(pPart->hGeoDef);

	// Randomize the part
	costumeRandom_ControlledRandomMaterialTexture(pPCCostume, pPart, pSpecies, pSlotType, pGuild, eaUnlockedCostumes, pMirrorPart, pCurrentGeo, pExpectedMaterial, pExpectedPattern, pExpectedDetail, pExpectedSpecular, pExpectedDiffuse, pExpectedMovable, bUnlockAll);

	// Destroy or randomize the cloth layer as appropriate
	if (pCurrentGeo && pCurrentGeo->pClothData && pCurrentGeo->pClothData->bIsCloth && pCurrentGeo->pClothData->bHasClothBack) {
		PCPart *pClothMirror = NULL;
		bool bMirrorOuterPart = bSymmetry ||
				bRequired && randomMersennePositiveF32(gRandTable) < gConf.fCostumeMirrorChanceRequired ||
				!bRequired && randomMersennePositiveF32(gRandTable) < gConf.fCostumeMirrorChanceOptional;

		if (pMirrorPart) {
			if (pMirrorPart->pClothLayer) {
				pClothMirror = pMirrorPart->pClothLayer;
			} else if (bMirrorOuterPart) {
				pClothMirror = (PCPart *) pPart;
			}
		} else if (bMirrorOuterPart) {
			pClothMirror = (PCPart *) pPart;
		}

		// Make sure the cloth layer exists
		if (!pPart->pClothLayer) {
			pPart->pClothLayer = StructCloneNoConst(parse_PCPart, pPart);
			REMOVE_HANDLE(pPart->pClothLayer->hBoneDef);
			REMOVE_HANDLE(pPart->pClothLayer->hGeoDef);
		}

		// Fill in the cloth layer
		costumeRandom_ControlledRandomMaterialTexture(pPCCostume, pPart->pClothLayer, pSpecies, pSlotType, pGuild, eaUnlockedCostumes, pClothMirror, pCurrentGeo, pExpectedClothMaterial, pExpectedClothPattern, pExpectedClothDetail, pExpectedClothSpecular, pExpectedClothDiffuse, pExpectedClothMovable, bUnlockAll);

	} else {
		if (pPart->pClothLayer) {
			StructDestroyNoConst(parse_PCPart, pPart->pClothLayer);
			pPart->pClothLayer = NULL;
		}
	}
}

static bool costumeRandom_ControlledShouldPartMirror(PlayerCostume *pPCCostume, PCPart *pMirrorPart, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, bool bRequired, bool bSymmetry, bool bUnlockAll)
{
	PCGeometryDef *pMirrorGeo = NULL;

	// Find the mirror of the geometry
	if (pMirrorPart && GET_REF(pMirrorPart->hGeoDef)) {
		PCGeometryDef **eaGeometry = NULL;
		PCBoneDef *pBone = GET_REF(pMirrorPart->hBoneDef);
		PCCategory *pTargetCategory = pBone ? costumeTailor_GetCategoryForRegion(pPCCostume, GET_REF(pBone->hRegion)) : NULL;
		int iMirror;

		if (!pBone) {
			return false;
		}

		costumeTailor_GetValidGeos(CONTAINER_NOCONST(PlayerCostume, pPCCostume), GET_REF(pPCCostume->hSkeleton), pBone, pTargetCategory, pSpecies, eaUnlockedCostumes, &eaGeometry, false, false, false, bUnlockAll);
		iMirror = costumeTailor_GetMatchingGeoIndex(GET_REF(pMirrorPart->hGeoDef), eaGeometry);
		pMirrorGeo = iMirror >= 0 ? eaGeometry[iMirror] : NULL;

		eaDestroy(&eaGeometry);
	}

	// Is there mirror geometry and does it want to mirror?
	return pMirrorGeo &&
			(	bSymmetry ||
				bRequired && randomMersennePositiveF32(gRandTable) < gConf.fCostumeMirrorChanceRequired ||
				!bRequired && randomMersennePositiveF32(gRandTable) < gConf.fCostumeMirrorChanceOptional
			);
}

static PCPart *costumeRandom_ControlledRandomPartMirroring(PlayerCostume *pPCCostume, PCPart *pPart, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, PCPart **eaPreRandomizedParts, bool bSymmetry, bool bUnlockAll)
{
	PCSkeletonDef *pSkeleton = pPCCostume ? GET_REF(pPCCostume->hSkeleton) : NULL;
	PCBoneDef *pBone = pPart ? GET_REF(pPart->hBoneDef) : NULL;
	bool bRequired = false;
	int i;

	if (!pPCCostume || !pBone || !pSkeleton) {
		return NULL;
	}

	// Check to see if symmetry is valid
	if (GET_REF(pBone->hMirrorBone)) {
		PCBoneDef *pMirrorBone = GET_REF(pBone->hMirrorBone);
		PCPart *pMirrorPart = NULL;

		// Is the part required?
		for(i=eaSize(&pSkeleton->eaRequiredBoneDefs)-1; i>=0; i--) {
			if (GET_REF(pSkeleton->eaRequiredBoneDefs[i]->hBone) == pBone) {
				break;
			}
		}
		if (i < 0) {
			PCCategory *pCategory = costumeTailor_GetCategoryForRegion(pPCCostume, GET_REF(pBone->hRegion));
			if (pCategory) {
				for (i = eaSize(&pCategory->eaRequiredBones) - 1; i >= 0; i--) {
					if (GET_REF(pCategory->eaRequiredBones[i]->hBone) == pBone) {
						break;
					}
				}
			}
		}
		if (i < 0 && pSpecies)
		{
			for (i = eaSize(&pSpecies->eaBoneData) - 1; i >= 0; i--) {
				if (GET_REF(pSpecies->eaBoneData[i]->hBone) == pBone) {
					if (!pSpecies->eaBoneData[i]->bRequires) i = -1;
					break;
				}
			}
		}
		bRequired = (i >= 0);

		// Has the mirror been randomized?
		for(i=eaSize(&eaPreRandomizedParts)-1; i>=0; i--) {
			if (GET_REF(eaPreRandomizedParts[i]->hBoneDef) == pMirrorBone) {
				pMirrorPart = eaPreRandomizedParts[i];
				break;
			}
		}
		if (!pMirrorPart) {
			for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; i--) {
				if (GET_REF(pPCCostume->eaParts[i]->hBoneDef) == pMirrorBone) {
					if ((pPCCostume->eaParts[i]->eControlledRandomLocks & kPCControlledRandomLock_Geometry)) {
						pMirrorPart = pPCCostume->eaParts[i];
					}
					break;
				}
			}
		}

		if (costumeRandom_ControlledShouldPartMirror(pPCCostume, pMirrorPart, pSpecies, eaUnlockedCostumes, bRequired, bSymmetry, bUnlockAll)) {
			return pMirrorPart;
		}
	}

	return NULL;
}


static void costumeRandom_ControlledRandomBonePart(NOCONST(PlayerCostume) *pPCCostume, PCBoneDef *pBone, PCCategory *pTargetCategory, SpeciesDef *pSpecies, const PCSlotType *pSlotType, Guild *pGuild, PlayerCostume **eaUnlockedCostumes, PCPart ***peaFreshParts, PCStyle **eaStyles, bool bSymmetry, bool bBoneGroupMatching, bool bSortDisplay, bool bUnlockAll)
{
	int iPart = -1;
	PCSkeletonDef *pSkeleton = pPCCostume ? GET_REF(pPCCostume->hSkeleton) : NULL;
	bool bRequired = false;
	bool bExcluded = false;
	int j;
	F32 fChance;
	PCGeometryDef **eaStyleGeos = NULL;

	if (!pPCCostume || !pBone || !pSkeleton) {
		return;
	}

	// Is it required?
	for(j=eaSize(&pSkeleton->eaRequiredBoneDefs)-1; j>=0; j--) {
		if (GET_REF(pSkeleton->eaRequiredBoneDefs[j]->hBone) == pBone) {
			bRequired = true;
			break;
		}
	}
	if (!bRequired)
	{
		for(j=eaSize(&pTargetCategory->eaRequiredBones)-1; j>=0; j--) {
			if (GET_REF(pTargetCategory->eaRequiredBones[j]->hBone) == pBone) {
				bRequired = true;
				break;
			}
		}
	}
	if (pSpecies && !bRequired)
	{
		for(j=eaSize(&pSpecies->eaBoneData)-1; j>=0; j--) {
			if (GET_REF(pSpecies->eaBoneData[j]->hBone) == pBone) {
				bRequired = pSpecies->eaBoneData[j]->bRequires;
				break;
			}
		}
	}

	// Is it excluded?
	for(j=eaSize(&pTargetCategory->eaExcludedBones)-1; j>=0; j--) {
		if (GET_REF(pTargetCategory->eaExcludedBones[j]->hBone) == pBone) {
			bExcluded = true;
			break;
		}
	}

	if (!bExcluded && !bRequired && pBone->fRandomChance < 0) {
		return; // not randomizable
	}

	// Search for part
	for(j=eaSize(&pPCCostume->eaParts)-1; j>=0; j--) {
		if (GET_REF(pPCCostume->eaParts[j]->hBoneDef) == pBone) {
			iPart = j;
			break;
		}
	}

	// Part already exists?
	if (iPart >= 0) {
		PCPart *pMirrorPart = NULL;
		NOCONST(PCPart) *pPart = pPCCostume->eaParts[iPart];
		bool bRandomize = false;

		if (!bExcluded) {
			// Part locked?
			bRandomize = (!!pPart->eControlledRandomLocks);

			// Is it wanting to mirror?
			pMirrorPart = costumeRandom_ControlledRandomPartMirroring((PlayerCostume *)pPCCostume, (PCPart *)pPart, pSpecies, eaUnlockedCostumes, *peaFreshParts, bSymmetry, bUnlockAll);
			bRandomize = bRandomize || (!!pMirrorPart);

			// Is it required?
			bRandomize = bRandomize || bRequired;

			// Part just wants to exist?
			fChance = pBone->fRandomChance == 0 ? 0.1 : pBone->fRandomChance;
			bRandomize = bRandomize || randomMersennePositiveF32(gRandTable) < fChance;
		}

		// Require a geo matching the styles
		if (bRandomize && !bRequired && eaSize(&eaStyles) > 0 && !(pPart->eControlledRandomLocks & kPCControlledRandomLock_Geometry)) {
			costumeTailor_GetValidGeos(pPCCostume, pSkeleton, pBone, pTargetCategory, pSpecies, eaUnlockedCostumes, &eaStyleGeos, pMirrorPart != NULL, false, false, bUnlockAll);
			for (j = eaSize(&eaStyleGeos) - 1; j >= 0; j--) {
				if (costumeRandom_StyleMatches(eaStyles, eaStyleGeos[j]->eaStyles)) {
					break;
				}
			}
			eaDestroy(&eaStyleGeos);
			bRandomize = !bRequired && j >= 0;
		}

		if (bRandomize) {
			// Randomize the part
			costumeRandom_ControlledFillRandomPart(pPCCostume, pPart, pSpecies, pSlotType, pGuild, eaUnlockedCostumes, pMirrorPart, eaStyles, bSortDisplay, bSymmetry, bUnlockAll);
			eaPush(peaFreshParts, (PCPart *)pPart);
		} else {
			// Delete the part
			StructDestroyNoConst(parse_PCPart, pPCCostume->eaParts[iPart]);
			eaRemove(&pPCCostume->eaParts, iPart);
		}
	}
	else // if (iPart < 0)
	{
		PCPart *pMirrorPart = NULL;
		NOCONST(PCPart) *pPart = NULL;

		if (bExcluded) {
			return; // Part is excluded by category
		}

		// Search for the already initialized mirror part
		for(j=eaSize(peaFreshParts)-1; j>=0; j--) {
			PCBoneDef *pFreshBone = GET_REF((*peaFreshParts)[j]->hBoneDef);
			if (pFreshBone && GET_REF(pFreshBone->hMirrorBone) == pBone) {
				if (costumeRandom_ControlledShouldPartMirror((PlayerCostume *)pPCCostume, (*peaFreshParts)[j], pSpecies, eaUnlockedCostumes, bRequired, bSymmetry, bUnlockAll)) {
					pMirrorPart = (*peaFreshParts)[j];
				}
				break;
			}
		}

		// Can the part be randomly excluded?
		if (!bRequired && !pMirrorPart) {
			fChance = pBone->fRandomChance == 0 ? 0.1 : pBone->fRandomChance;
			if (fChance < 0 || randomMersennePositiveF32(gRandTable) >= fChance) {
				return; // Part is not wanted
			}
		}

		// Require a geo matching the styles
		if (eaSize(&eaStyles) > 0) {
			costumeTailor_GetValidGeos(pPCCostume, pSkeleton, pBone, pTargetCategory, pSpecies, eaUnlockedCostumes, &eaStyleGeos, pMirrorPart != NULL, false, false, bUnlockAll);
			for (j = eaSize(&eaStyleGeos) - 1; j >= 0; j--) {
				if (costumeRandom_StyleMatches(eaStyles, eaStyleGeos[j]->eaStyles)) {
					break;
				}
			}
			eaDestroy(&eaStyleGeos);
			if (!bRequired && j < 0) {
				return; // Nothing matching the requested styles
			}
		}

		// Create a new part
		pPart = StructCreateNoConst(parse_PCPart);
		eaPush(&pPCCostume->eaParts, pPart);
		SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, pBone, pPart->hBoneDef);

		// Randomize the part
		costumeRandom_ControlledFillRandomPart(pPCCostume, pPart, pSpecies, pSlotType, pGuild, eaUnlockedCostumes, pMirrorPart, eaStyles, bSortDisplay, bSymmetry, bUnlockAll);
		eaPush(peaFreshParts, (PCPart *)pPart);
	}
}

void costumeRandom_ControlledRandomParts(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, Guild *pGuild, PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, PCRegion **eaLockedRegions, const char **eaPowerFXBones, PCStyle **eaStyles, bool bSymmetry, bool bBoneGroupMatching, bool bSortDisplay, bool bUnlockAll)
{
	PCBoneDef **eaBones = NULL;
	PCPart **eaFreshParts = NULL;
	PCRegion **eaRegions = NULL;
	PCSkeletonDef *pSkeleton = pPCCostume ? GET_REF(pPCCostume->hSkeleton) : NULL;
	int i, iRegion, j, k;
	PCBoneDef *pBone;
	F32 fWeight, fValue;
	U8 color0[4];
	U8 color1[4];
	U8 color2[4];
	U8 color3[4];
	U8 noglow[4] = {0, 0, 0, 0};
	PCStyle **eaCategoryStyles = NULL;
	char *estrError = NULL;

	if (!pPCCostume || !pSkeleton) {
		return;
	}

	for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; i--) {
		if (pPCCostume->eaParts[i]->eColorLink == kPCColorLink_All) {
			COPY_COSTUME_COLOR(pPCCostume->eaParts[i]->color0, color0);
			COPY_COSTUME_COLOR(pPCCostume->eaParts[i]->color1, color1);
			COPY_COSTUME_COLOR(pPCCostume->eaParts[i]->color2, color2);
			COPY_COSTUME_COLOR(pPCCostume->eaParts[i]->color3, color3);
			break;
		}
	}

	// Randomize the region categories
	costumeTailor_GetValidRegions(pPCCostume, pSpecies, eaUnlockedCostumes, NULL, pSlotType, &eaRegions, CGVF_OMIT_EMPTY | CGVF_REQUIRE_POWERFX | (bUnlockAll ? CGVF_UNLOCK_ALL : 0));
	for(iRegion=eaSize(&eaRegions)-1; iRegion >= 0; iRegion--) {
		PCRegion *pRegion = eaRegions[iRegion];
		NOCONST(PCPart) **eaRegionParts = NULL;
		bool categoryLocked = false;
		PCCategory **eaCategories = NULL;
		PCCategory **eaFallbackCategories = NULL;
		PCCategory *pTargetCategory = NULL;

		estrClear(&estrError);

		// Check to see if the category is locked
		for (i = eaSize(&eaLockedRegions) - 1; i >= 0; i--) {
			if (eaLockedRegions[i] == pRegion) {
				categoryLocked = true;
				break;
			}
		}
		if (categoryLocked) {
			continue;
		}

		// Get region parts
		for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; i--) {
			pBone = GET_REF(pPCCostume->eaParts[i]->hBoneDef);
			if (GET_REF(pBone->hRegion) == pRegion) {
				eaPush(&eaRegionParts, pPCCostume->eaParts[i]);
			}
		}

		costumeTailor_GetValidCategories(pPCCostume, pRegion, pSpecies, eaUnlockedCostumes, eaPowerFXBones, pSlotType, &eaCategories, CGVF_OMIT_EMPTY | CGVF_REQUIRE_POWERFX | (bUnlockAll ? CGVF_UNLOCK_ALL : 0));
		for(i=eaSize(&eaCategories)-1; i>=0; i--) {
			bool unsatisfied = false;

			for(j=eaSize(&pPCCostume->eaParts)-1; j>=0; j--) {
				if (!pPCCostume->eaParts[j]->eControlledRandomLocks) {
					continue;
				}
				if (!costumeRandom_CategorySatisfiesPart(pPCCostume, pSpecies, eaUnlockedCostumes, bUnlockAll, pPCCostume->eaParts[j], pRegion, eaCategories[i], eaStyles)) {
					estrConcatf(&estrError, "\n%s: part locking", eaCategories[i]->pcName);
					unsatisfied = true;
					break;
				}
			}

			for (j=iRegion+1; j<eaSize(&eaRegions) && !unsatisfied; j++) {
				PCCategory *pFinalCategory = costumeTailor_GetCategoryForRegion((PlayerCostume *)pPCCostume, eaRegions[j]);
				for (k=eaSize(&pFinalCategory->eaExcludedCategories)-1; k>=0; k--) {
					if (eaCategories[i] == GET_REF(pFinalCategory->eaExcludedCategories[k]->hCategory)) {
						estrConcatf(&estrError, "\n%s: excluded by category %s", eaCategories[i]->pcName, pFinalCategory->pcName);
						unsatisfied = true;
						break;
					}
				}
				for (k=eaSize(&eaCategories[i]->eaExcludedCategories)-1; k>=0 && !unsatisfied; k--) {
					if (GET_REF(eaCategories[i]->eaExcludedCategories[k]->hCategory) == pFinalCategory) {
						estrConcatf(&estrError, "\n%s: excludes category %s", eaCategories[i]->pcName, pFinalCategory->pcName);
						unsatisfied = true;
						break;
					}
				}
			}

			if (eaSize(&eaStyles) > 0)
			{
				for (j=0; j<eaSize(&pSkeleton->eaRequiredBoneDefs) && !unsatisfied; j++) {
					PCBoneDef *pRegBone = GET_REF(pSkeleton->eaRequiredBoneDefs[j]->hBone);
					if (!pRegBone || GET_REF(pRegBone->hRegion) != pRegion) {
						continue;
					}
					for (k=0; k<eaSize(&eaCategories[i]->eaExcludedBones); k++) {
						if (GET_REF(eaCategories[i]->eaExcludedBones[k]->hBone) == pRegBone) {
							break;
						}
					}
					if (k<eaSize(&eaCategories[i]->eaExcludedBones)) {
						continue;
					}
					costumeTailor_GetValidStyles(pPCCostume, pRegion, eaCategories[i], pRegBone, NULL, pSpecies, eaUnlockedCostumes, eaPowerFXBones, &eaCategoryStyles, 0);
					for (k=0; k<eaSize(&eaStyles); k++) {
						if (eaFind(&eaCategoryStyles, eaStyles[k]) >= 0) {
							break;
						}
					}
					if (k>=eaSize(&eaStyles)) {
						estrConcatf(&estrError, "\n%s: required bone %s lacks geo with selected styles", eaCategories[i]->pcName, pRegBone->pcName);
						eaPush(&eaFallbackCategories, eaCategories[i]);
						unsatisfied = true;
						break;
					}
				}
				for (j=0; j<eaSize(&eaCategories[i]->eaRequiredBones) && !unsatisfied; j++) {
					PCBoneDef *pRegBone = GET_REF(eaCategories[i]->eaRequiredBones[j]->hBone);
					if (!pRegBone || GET_REF(pRegBone->hRegion) != pRegion) {
						continue;
					}
					for (k=0; k<eaSize(&eaCategories[i]->eaExcludedBones); k++) {
						if (GET_REF(eaCategories[i]->eaExcludedBones[k]->hBone) == pRegBone) {
							break;
						}
					}
					if (k<eaSize(&eaCategories[i]->eaExcludedBones)) {
						continue;
					}
					costumeTailor_GetValidStyles(pPCCostume, pRegion, eaCategories[i], pRegBone, NULL, pSpecies, eaUnlockedCostumes, eaPowerFXBones, &eaCategoryStyles, 0);
					for (k=0; k<eaSize(&eaStyles); k++) {
						if (eaFind(&eaCategoryStyles, eaStyles[k]) >= 0) {
							break;
						}
					}
					if (k>=eaSize(&eaStyles)) {
						estrConcatf(&estrError, "\n%s: required bone %s lacks geo with selected styles", eaCategories[i]->pcName, pRegBone->pcName);
						eaPush(&eaFallbackCategories, eaCategories[i]);
						unsatisfied = true;
						break;
					}
				}
				if (pSpecies) {
					for (j=0; j<eaSize(&pSpecies->eaBoneData) && !unsatisfied; j++) {
						PCBoneDef *pRegBone = GET_REF(pSpecies->eaBoneData[j]->hBone);
						if (!pSpecies->eaBoneData[j]->bRequires) {
							continue;
						}
						for (k=0; k<eaSize(&eaCategories[i]->eaExcludedBones); k++) {
							if (GET_REF(eaCategories[i]->eaExcludedBones[k]->hBone) == pRegBone) {
								break;
							}
						}
						if (k<eaSize(&eaCategories[i]->eaExcludedBones)) {
							continue;
						}
						costumeTailor_GetValidStyles(pPCCostume, pRegion, eaCategories[i], pRegBone, NULL, pSpecies, eaUnlockedCostumes, eaPowerFXBones, &eaCategoryStyles, 0);
						for (k=0; k<eaSize(&eaStyles); k++) {
							if (eaFind(&eaCategoryStyles, eaStyles[k]) >= 0) {
								break;
							}
						}
						if (k>=eaSize(&eaStyles)) {
							estrConcatf(&estrError, "\n%s: required bone %s lacks geo with selected styles", eaCategories[i]->pcName, pRegBone->pcName);
							eaPush(&eaFallbackCategories, eaCategories[i]);
							unsatisfied = true;
							break;
						}
					}
				}
			}

			if (unsatisfied) {
				eaRemove(&eaCategories, i);
			}
		}

		// Choose category
		fWeight = 0.0;
		for(i=eaSize(&eaCategories)-1; i>=0; i--) {
			costumeRandom_AddWeight(&fWeight, eaCategories[i]->fRandomWeight);
		}
		if (fWeight > 0 && eaSize(&eaCategories) > 0) {
			fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
			for(i=eaSize(&eaCategories)-1; i>=0; i--) {
				if (costumeRandom_MatchWeight(&fValue, eaCategories[i]->fRandomWeight)) {
					pTargetCategory = eaCategories[i];
					break;
				}
			}
		}
		eaDestroy(&eaCategories);

		if (!pTargetCategory) {
			if (isDevelopmentMode()) {
				Errorf("Region '%s' should have at least one Category (all valid categories culled): %s", pRegion->pcName, estrError);
			}
			if (eaSize(&eaFallbackCategories) <= 0) {
				eaDestroy(&eaRegionParts);
				eaDestroy(&eaFallbackCategories);
				continue;
			}

			fWeight = 0.0;
			for(i=eaSize(&eaFallbackCategories)-1; i>=0; i--) {
				costumeRandom_AddWeight(&fWeight, eaFallbackCategories[i]->fRandomWeight);
			}
			if (fWeight > 0 && eaSize(&eaFallbackCategories) > 0) {
				fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
				for(i=eaSize(&eaFallbackCategories)-1; i>=0; i--) {
					if (costumeRandom_MatchWeight(&fValue, eaFallbackCategories[i]->fRandomWeight)) {
						pTargetCategory = eaFallbackCategories[i];
						break;
					}
				}
			}
			eaDestroy(&eaFallbackCategories);
		}

		// Update the category
		costumeTailor_SetRegionCategory(pPCCostume, pRegion, pTargetCategory);
		eaDestroy(&eaRegionParts);
	}

	eaDestroy(&eaCategoryStyles);

	for(iRegion=eaSize(&eaRegions)-1; iRegion >= 0; iRegion--) {
		// Get the list of bones to add/change
		costumeTailor_GetValidBones(pPCCostume, pSkeleton, eaRegions[iRegion], NULL, pSpecies, eaUnlockedCostumes, eaPowerFXBones, &eaBones, CGVF_OMIT_EMPTY | CGVF_SORT_DISPLAY | CGVF_REQUIRE_POWERFX | (bUnlockAll ? CGVF_UNLOCK_ALL : 0));

		// Fill parent bones
		for(i=eaSize(&eaBones)-1; i>=0; i--) {
			PCCategory *pTargetCategory = NULL;

			if (!eaBones[i]) {
				continue;
			}

			pTargetCategory = costumeTailor_GetCategoryForRegion((PlayerCostume *)pPCCostume, GET_REF(eaBones[i]->hRegion));

			if (!eaBones[i]->bIsChildBone) {
				costumeRandom_ControlledRandomBonePart(pPCCostume, eaBones[i], pTargetCategory, pSpecies, pSlotType, pGuild, eaUnlockedCostumes, &eaFreshParts, eaStyles, bSymmetry, bBoneGroupMatching, bSortDisplay, bUnlockAll);
			}
		}

		// Fill child bones
		for(i=eaSize(&eaBones)-1; i>=0; i--) {
			PCCategory *pTargetCategory = NULL;

			if (!eaBones[i]) {
				continue;
			}

			pTargetCategory = costumeTailor_GetCategoryForRegion((PlayerCostume *)pPCCostume, GET_REF(eaBones[i]->hRegion));

			if (eaBones[i]->bIsChildBone) {
				costumeRandom_ControlledRandomBonePart(pPCCostume, eaBones[i], pTargetCategory, pSpecies, pSlotType, pGuild, eaUnlockedCostumes, &eaFreshParts, eaStyles, bSymmetry, bBoneGroupMatching, bSortDisplay, bUnlockAll);
			}
		}
	}

	// Fill in existing colors
	for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; i--) {
		if (pPCCostume->eaParts[i]->eColorLink == kPCColorLink_All) {
			costumeTailor_SetPartColors(pPCCostume, pSpecies, pSlotType, pPCCostume->eaParts[i], color0, color1, color2, color3, noglow);
			break;
		}
	}

	// Cleanup
	estrDestroy(&estrError);
	eaDestroy(&eaBones);
	eaDestroy(&eaFreshParts);
}


void costumeRandom_ControlledRandomColors(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType,
	PlayerCostume **eaUnlockedCostumes, PCControlledRandomLock eAllColorLocks, bool bSymmetry, bool bBoneGroupMatching, bool bSortDisplay, bool bUnlockAll)
{
	int i, j;
	float fWeight = 0, fValue;
	U8 color0[4];
	U8 color1[4];
	U8 color2[4];
	U8 color3[4];
	U8 tempColor[4];
	bool bHaveSharedColors = false;
	U8 glowScale[4] = {0, 0, 0, 0};
	U8 unsetColor[4] = {0, 0, 0, 0};
	PCColorQuadSet *pQuadSet;
	PCSkeletonDef *pSkeleton = pPCCostume ? GET_REF(pPCCostume->hSkeleton) : NULL;

	if (!pPCCostume || !pSkeleton) {
		return;
	}

	// Save current shared colors
	for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; i--) {
		if (pPCCostume->eaParts[i]->eColorLink == kPCColorLink_All) {
			COPY_COSTUME_COLOR(pPCCostume->eaParts[i]->color0, color0);
			COPY_COSTUME_COLOR(pPCCostume->eaParts[i]->color1, color1);
			COPY_COSTUME_COLOR(pPCCostume->eaParts[i]->color2, color2);
			COPY_COSTUME_COLOR(pPCCostume->eaParts[i]->color3, color3);
			bHaveSharedColors = true;
			break;
		}
	}

	if (!bHaveSharedColors) {
		return;
	}

	// Pick new shared colors
	pQuadSet = costumeTailor_GetColorQuadSetForPart(pPCCostume, pSpecies, NULL);
	if (pQuadSet) {
		int iColor;
		fWeight = 0.0;
		for(j=eaSize(&pQuadSet->eaColorQuads)-1; j>=0; --j) {
			costumeRandom_AddWeight(&fWeight, pQuadSet->eaColorQuads[j]->fRandomWeight);
		}
		if (fWeight > 0) {
			fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
			iColor = 0;
			assert(pQuadSet->eaColorQuads);
			for(j=eaSize(&pQuadSet->eaColorQuads)-1; j>=0; --j) {
				if (costumeRandom_MatchWeight(&fValue, pQuadSet->eaColorQuads[j]->fRandomWeight)) {
					iColor = j;
					break;
				}
			}
			if (!(eAllColorLocks & kPCControlledRandomLock_AllColor0) || IS_SAME_COSTUME_COLOR(unsetColor, color0)) {
				COPY_COSTUME_COLOR(pQuadSet->eaColorQuads[iColor]->color0, color0);
			}
			if (!(eAllColorLocks & kPCControlledRandomLock_AllColor1) || IS_SAME_COSTUME_COLOR(unsetColor, color1)) {
				COPY_COSTUME_COLOR(pQuadSet->eaColorQuads[iColor]->color1, color1);
			}
			if (!(eAllColorLocks & kPCControlledRandomLock_AllColor2) || IS_SAME_COSTUME_COLOR(unsetColor, color2)) {
				COPY_COSTUME_COLOR(pQuadSet->eaColorQuads[iColor]->color2, color2);
			}
			if (!(eAllColorLocks & kPCControlledRandomLock_AllColor3) || IS_SAME_COSTUME_COLOR(unsetColor, color3)) {
				COPY_COSTUME_COLOR(pQuadSet->eaColorQuads[iColor]->color3, color3);
			}
		}
	}

	// No color picked?
	if (eaSize(&pPCCostume->eaParts) && (!pQuadSet || (fWeight == 0))) {
		if (!(eAllColorLocks & kPCControlledRandomLock_AllColor1)) {
			COPY_COSTUME_COLOR(g_StaticQuad.color0, color0);
		}
		if (!(eAllColorLocks & kPCControlledRandomLock_AllColor1)) {
			COPY_COSTUME_COLOR(g_StaticQuad.color1, color1);
		}
		if (!(eAllColorLocks & kPCControlledRandomLock_AllColor1)) {
			COPY_COSTUME_COLOR(g_StaticQuad.color2, color2);
		}
		if (!(eAllColorLocks & kPCControlledRandomLock_AllColor1)) {
			COPY_COSTUME_COLOR(g_StaticQuad.color3, color3);
		}
	}

	// Set the shared costume colors
	for (j = eaSize(&pPCCostume->eaParts) - 1; j >= 0; j--) {
		PCBoneDef *pBone = GET_REF(pPCCostume->eaParts[j]->hBoneDef);
		if (pBone && pBone->bIsGuildEmblemBone) continue;
		if (pPCCostume->eaParts[j]->eColorLink == kPCColorLink_All) {
			if (!(pPCCostume->eaParts[j]->eControlledRandomLocks & kPCControlledRandomLock_Color0)) {
				COPY_COSTUME_COLOR(color0, pPCCostume->eaParts[j]->color0);
				if (!pPCCostume->eaParts[j]->pCustomColors && (glowScale[0] > 1)) {
					pPCCostume->eaParts[j]->pCustomColors = StructCreateNoConst(parse_PCCustomColorInfo);
				}
				if (pPCCostume->eaParts[j]->pCustomColors) {
					pPCCostume->eaParts[j]->pCustomColors->glowScale[0] = glowScale[0];
				}
			}
			if (!(pPCCostume->eaParts[j]->eControlledRandomLocks & kPCControlledRandomLock_Color1)) {
				COPY_COSTUME_COLOR(color1, pPCCostume->eaParts[j]->color1);
				if (!pPCCostume->eaParts[j]->pCustomColors && (glowScale[1] > 1)) {
					pPCCostume->eaParts[j]->pCustomColors = StructCreateNoConst(parse_PCCustomColorInfo);
				}
				if (pPCCostume->eaParts[j]->pCustomColors) {
					pPCCostume->eaParts[j]->pCustomColors->glowScale[1] = glowScale[1];
				}
			}
			if (!(pPCCostume->eaParts[j]->eControlledRandomLocks & kPCControlledRandomLock_Color2)) {
				COPY_COSTUME_COLOR(color2, pPCCostume->eaParts[j]->color2);
				if (!pPCCostume->eaParts[j]->pCustomColors && (glowScale[2] > 1)) {
					pPCCostume->eaParts[j]->pCustomColors = StructCreateNoConst(parse_PCCustomColorInfo);
				}
				if (pPCCostume->eaParts[j]->pCustomColors) {
					pPCCostume->eaParts[j]->pCustomColors->glowScale[2] = glowScale[2];
				}
			}
			if (!(pPCCostume->eaParts[j]->eControlledRandomLocks & kPCControlledRandomLock_Color3)) {
				COPY_COSTUME_COLOR(color3, pPCCostume->eaParts[j]->color3);
				if (!pPCCostume->eaParts[j]->pCustomColors && (glowScale[3] > 1)) {
					pPCCostume->eaParts[j]->pCustomColors = StructCreateNoConst(parse_PCCustomColorInfo);
				}
				if (pPCCostume->eaParts[j]->pCustomColors) {
					pPCCostume->eaParts[j]->pCustomColors->glowScale[3] = glowScale[3];
				}
			}
		}
	}

	// Pick skin color
	if (!(eAllColorLocks & kPCControlledRandomLock_SkinColor)) {
		UIColorSet *pColorSet;
		pColorSet = costumeTailor_GetOverrideSkinColorSet(pSpecies, pSlotType);
		if(!pColorSet)
		{
			pColorSet = GET_REF(pSkeleton->hSkinColorSet);
		}
		if (!pColorSet) {
			pColorSet = RefSystem_ReferentFromString(g_hCostumeColorsDict, "Core_Skin");
		}
		if (pColorSet) {
			int iColor;
			fWeight = 0.0;
			for(j=eaSize(&pColorSet->eaColors)-1; j>=0; --j) {
				costumeRandom_AddWeight(&fWeight, pColorSet->eaColors[j]->fRandomWeight);
			}
			if (fWeight > 0) {
				fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
				iColor = 0;
				assert(pColorSet->eaColors);
				for(j=eaSize(&pColorSet->eaColors)-1; j>=0; --j) {
					if (costumeRandom_MatchWeight(&fValue, pColorSet->eaColors[j]->fRandomWeight)) {
						iColor = j;
						break;
					}
				}
				VEC4_TO_COSTUME_COLOR(pColorSet->eaColors[iColor]->color, tempColor);
				costumeTailor_SetSkinColor(pPCCostume, tempColor);
			}
		} else {
			U8 skinPink[4] = { 212, 137, 114, 255 };
			costumeTailor_SetSkinColor(pPCCostume, skinPink);
		}
	}
}


void costumeRandom_ControlledRandomBoneColors(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, PlayerCostume **eaUnlockedCostumes, PCBoneDef *pBone, bool bSymmetry, bool bBoneGroupMatching, bool bSortDisplay, bool bUnlockAll)
{
	U8 noglow[4] = {0, 0, 0, 0};
	PCColorQuadSet *pQuadSet;
	int iColor = 0;
	PCSkeletonDef *pSkeleton = pPCCostume ? GET_REF(pPCCostume->hSkeleton) : NULL;
	float fWeight, fValue;
	int i, j;
	NOCONST(PCPart) *pPart = pBone && pPCCostume ? costumeTailor_GetPartByBone(pPCCostume, pBone, NULL) : NULL;
	NOCONST(PCPart) *pMirrorPart = NULL;

	if (!pPCCostume || !pSkeleton || !pPart || pBone->bIsGuildEmblemBone) {
		return;
	}

	if (bSymmetry) {
		pMirrorPart = costumeTailor_GetPartByBone(pPCCostume, GET_REF(pBone->hMirrorBone), NULL);
	}

	pQuadSet = costumeTailor_GetColorQuadSetForPart(pPCCostume, pSpecies, pPart);
	if (pQuadSet) {
		fWeight = 0.0;
		for(i = eaSize(&pQuadSet->eaColorQuads) - 1; i >= 0; i--) {
			costumeRandom_AddWeight(&fWeight, pQuadSet->eaColorQuads[i]->fRandomWeight);
		}
		if (fWeight > 0) {
			fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
			for(i = eaSize(&pQuadSet->eaColorQuads) - 1; i >= 0; i--) {
				if (costumeRandom_MatchWeight(&fValue, pQuadSet->eaColorQuads[i]->fRandomWeight)) {
					iColor = i;
					break;
				}
			}
		}
	}

	if (iColor >= 0 && pPart->eColorLink == kPCColorLink_None) {
		assert(pQuadSet->eaColorQuads);
		// Randomize this part's color
		costumeTailor_SetPartColors(pPCCostume, pSpecies, pSlotType, pPart, 
			(pPart->eControlledRandomLocks & kPCControlledRandomLock_Color0) ? pPart->color0 : pQuadSet->eaColorQuads[iColor]->color0,
			(pPart->eControlledRandomLocks & kPCControlledRandomLock_Color1) ? pPart->color1 : pQuadSet->eaColorQuads[iColor]->color1, 
			(pPart->eControlledRandomLocks & kPCControlledRandomLock_Color2) ? pPart->color2 : pQuadSet->eaColorQuads[iColor]->color2,
			(pPart->eControlledRandomLocks & kPCControlledRandomLock_Color3) ? pPart->color3 : pQuadSet->eaColorQuads[iColor]->color3,
			noglow);
	}
	if (iColor >= 0 && pMirrorPart && pMirrorPart->eColorLink == kPCColorLink_None) {
		// Randomize the mirror part's color
		costumeTailor_SetPartColors(pPCCostume, pSpecies, pSlotType, pMirrorPart, 
			(pMirrorPart->eControlledRandomLocks & kPCControlledRandomLock_Color0) ? pMirrorPart->color0 : pQuadSet->eaColorQuads[iColor]->color0,
			(pMirrorPart->eControlledRandomLocks & kPCControlledRandomLock_Color1) ? pMirrorPart->color1 : pQuadSet->eaColorQuads[iColor]->color1, 
			(pMirrorPart->eControlledRandomLocks & kPCControlledRandomLock_Color2) ? pMirrorPart->color2 : pQuadSet->eaColorQuads[iColor]->color2,
			(pMirrorPart->eControlledRandomLocks & kPCControlledRandomLock_Color3) ? pMirrorPart->color3 : pQuadSet->eaColorQuads[iColor]->color3,
			noglow);
	}
	if (iColor >= 0 && bBoneGroupMatching) {
		NOCONST(PCPart) *pGroupPart = NULL;
		PCSkeletonDef *skel = GET_REF(pPCCostume->hSkeleton);

		if (skel)
		{
			PCBoneGroup **bg = skel->eaBoneGroups;
			if (bg)
			{
				for(j=eaSize(&bg)-1; j>=0; --j)
				{
					if ((bg[j]->eBoneGroupFlags & kPCBoneGroupFlags_MatchGeos))
					{
						for (i = eaSize(&bg[j]->eaBoneInGroup)-1; i >= 0; --i)
						{
							pGroupPart = costumeTailor_GetPartByBone(pPCCostume, GET_REF(bg[j]->eaBoneInGroup[i]->hBone), NULL);
							if (pGroupPart && pGroupPart->eColorLink == kPCColorLink_None && pGroupPart != pPart) {
								// Randomize the bone group part's color
								costumeTailor_SetPartColors(pPCCostume, pSpecies, pSlotType, pGroupPart, 
									(pMirrorPart->eControlledRandomLocks & kPCControlledRandomLock_Color0) ? pGroupPart->color0 : pQuadSet->eaColorQuads[iColor]->color0,
									(pMirrorPart->eControlledRandomLocks & kPCControlledRandomLock_Color1) ? pGroupPart->color1 : pQuadSet->eaColorQuads[iColor]->color1, 
									(pMirrorPart->eControlledRandomLocks & kPCControlledRandomLock_Color2) ? pGroupPart->color2 : pQuadSet->eaColorQuads[iColor]->color2,
									(pMirrorPart->eControlledRandomLocks & kPCControlledRandomLock_Color3) ? pGroupPart->color3 : pQuadSet->eaColorQuads[iColor]->color3,
									noglow);
							}
						}
					}
				}
			}
		}
	}
}

static void costumeRandom_FillRandomPart(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, NOCONST(PCPart) *pMirrorPart, SpeciesDef *pSpecies, const PCSlotType *pSlotType, Guild *pGuild, PlayerCostume **eaUnlockedCostumes, bool bSortDisplay, bool bUnlockAll)
{
	PCBoneDef *pBone = GET_REF(pPart->hBoneDef);
	PCMaterialDef **eaMats = NULL;
	PCTextureDef **eaPattern = NULL;
	PCTextureDef **eaDetail = NULL;
	PCTextureDef **eaSpecular = NULL;
	PCTextureDef **eaDiffuse = NULL;
	PCTextureDef **eaMovable = NULL;
	bool bShowGuild = pGuild ? (randomIntRange(0,1) ? true : false) : false;
	float fWeight;
	float fValue;
	int i, j;
	int iMat = -1, iTex;

	// Pick a material for the bone
	costumeTailor_GetValidMaterials(pPCCostume, GET_REF(pPart->hGeoDef), pSpecies, NULL, NULL, eaUnlockedCostumes, &eaMats, false, bSortDisplay, bUnlockAll);
	if (pMirrorPart && GET_REF(pMirrorPart->hMatDef)) {
		iMat = costumeTailor_GetMatchingMatIndex(GET_REF(pMirrorPart->hMatDef), eaMats);
	}
	if (iMat < 0) {
		fWeight = 0.0;
		for(j=eaSize(&eaMats)-1; j>=0; --j) {
			costumeRandom_AddWeight(&fWeight, eaMats[j]->fRandomWeight);
		}
		if (fWeight > 0) {
			fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
			iMat = 0;
			for(j=eaSize(&eaMats)-1; j>=0; --j) {
				if (costumeRandom_MatchWeight(&fValue, eaMats[j]->fRandomWeight)) {
					iMat = j;
					break;
				}
			}
		}
	}

	if (iMat >= 0) {
		assert(eaMats);
		SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, eaMats[iMat], pPart->hMatDef);

		// Pick textures for the bone
		costumeTailor_GetValidTextures(pPCCostume, eaMats[iMat], pSpecies, NULL, NULL, GET_REF(pPart->hGeoDef), NULL, eaUnlockedCostumes, kPCTextureType_Pattern, &eaPattern, false, bSortDisplay, bUnlockAll);
		if (pBone && pBone->bIsGuildEmblemBone)
		{
			if (bShowGuild)
			{
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pGuild->pcEmblem, pPart->hPatternTexture);
				if (!pPart->pTextureValues) {
					pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
				}
				if (pPart->pTextureValues) {
					pPart->pTextureValues->fPatternValue = pGuild->fEmblemRotation;
				}
				*((U32*)pPart->color0) = pGuild->iEmblemColor0;
				*((U32*)pPart->color1) = pGuild->iEmblemColor1;
				pPart->eColorLink = kPCColorLink_None;
			}
			else
			{
				//Strip invalid textures
				for(i=eaSize(&eaPattern)-1; i>=0; i--) {
					for (j = eaSize(&g_GuildEmblems.eaEmblems)-1; j >= 0; --j)
					{
						if (!stricmp(eaPattern[i]->pcName,REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[j]->hTexture))) {
							eaRemove(&eaPattern, i);
						}
					}
				}
			}
		}
		if ((pBone && !pBone->bIsGuildEmblemBone) || (!bShowGuild))
		{
			iTex = -1;
			if (pMirrorPart && GET_REF(pMirrorPart->hPatternTexture)) {
				iTex = costumeTailor_GetMatchingTexIndex(GET_REF(pMirrorPart->hPatternTexture), eaPattern);
			}
			if (iTex < 0) {
				fWeight = 0.0;
				for(j=eaSize(&eaPattern)-1; j>=0; --j) {
					costumeRandom_AddWeight(&fWeight, eaPattern[j]->fRandomWeight);
				}
				if (fWeight > 0) {
					assert(eaPattern);
					fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
					iTex = 0;
					for(j=eaSize(&eaPattern)-1; j>=0; --j) {
						if (costumeRandom_MatchWeight(&fValue, eaPattern[j]->fRandomWeight)) {
							iTex = j;
							break;
						}
					}
				}
			}
			if (iTex >= 0) {
				SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, eaPattern[iTex], pPart->hPatternTexture);
				if (eaPattern[iTex]->pValueOptions && eaPattern[iTex]->pValueOptions->pcValueConstant)
				{
					if (!pPart->pTextureValues) {
						pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
					}
					if (pPart->pTextureValues) {
						pPart->pTextureValues->fPatternValue = randomF32() * 100.0f;
					}
				}
			}
		}

		costumeTailor_GetValidTextures(pPCCostume, eaMats[iMat], pSpecies, NULL, NULL, GET_REF(pPart->hGeoDef), NULL, eaUnlockedCostumes, kPCTextureType_Detail, &eaDetail, false, bSortDisplay, bUnlockAll);
		if (pBone && pBone->bIsGuildEmblemBone)
		{
			if (bShowGuild)
			{
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pGuild->pcEmblem3, pPart->hDetailTexture);
			}
			else
			{
				//Strip invalid textures
				for(i=eaSize(&eaDetail)-1; i>=0; i--) {
					for (j = eaSize(&g_GuildEmblems.eaEmblems)-1; j >= 0; --j)
					{
						if (!stricmp(eaDetail[i]->pcName,REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[j]->hTexture))) {
							eaRemove(&eaDetail, i);
						}
					}
				}
			}
		}
		if ((pBone && !pBone->bIsGuildEmblemBone) || (!bShowGuild))
		{
			if (!GET_REF(pPart->hPatternTexture) || !(costumeTailor_GetTextureFlags(GET_REF(pPart->hPatternTexture)) & kPCTextureType_Detail)) {
				iTex = -1;
				if (pMirrorPart && GET_REF(pMirrorPart->hDetailTexture)) {
					iTex = costumeTailor_GetMatchingTexIndex(GET_REF(pMirrorPart->hDetailTexture), eaDetail);
				}
				if (iTex < 0) {
					fWeight = 0.0;
					for(j=eaSize(&eaDetail)-1; j>=0; --j) {
						costumeRandom_AddWeight(&fWeight, eaDetail[j]->fRandomWeight);
					}
					if (fWeight > 0) {
						assert(eaDetail);
						fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
						iTex = 0;
						for(j=eaSize(&eaDetail)-1; j>=0; --j) {
							if (costumeRandom_MatchWeight(&fValue, eaDetail[j]->fRandomWeight)) {
								iTex = j;
								break;
							}
						}
					}
				}
				if (iTex >= 0) {
					SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, eaDetail[iTex], pPart->hDetailTexture);
					if (eaDetail[iTex]->pValueOptions && eaDetail[iTex]->pValueOptions->pcValueConstant)
					{
						if (!pPart->pTextureValues) {
							pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
						}
						if (pPart->pTextureValues) {
							pPart->pTextureValues->fDetailValue = randomF32() * 100.0f;
						}
					}
				}
			}
		}

		if ((!GET_REF(pPart->hPatternTexture) || !(costumeTailor_GetTextureFlags(GET_REF(pPart->hPatternTexture)) & kPCTextureType_Specular)) &&
		    (!GET_REF(pPart->hDetailTexture) || !(costumeTailor_GetTextureFlags(GET_REF(pPart->hDetailTexture)) & kPCTextureType_Specular))) {
			costumeTailor_GetValidTextures(pPCCostume, eaMats[iMat], pSpecies, NULL, NULL, GET_REF(pPart->hGeoDef), NULL, eaUnlockedCostumes, kPCTextureType_Specular, &eaSpecular, false, bSortDisplay, bUnlockAll);
			iTex = -1;
			if (pMirrorPart && GET_REF(pMirrorPart->hSpecularTexture)) {
				iTex = costumeTailor_GetMatchingTexIndex(GET_REF(pMirrorPart->hSpecularTexture), eaSpecular);
			}
			if (iTex < 0) {
				fWeight = 0.0;
				for(j=eaSize(&eaSpecular)-1; j>=0; --j) {
					costumeRandom_AddWeight(&fWeight, eaSpecular[j]->fRandomWeight);
				}
				if (fWeight > 0) {
					assert(eaSpecular);
					fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
					iTex = 0;
					for(j=eaSize(&eaSpecular)-1; j>=0; --j) {
						if (costumeRandom_MatchWeight(&fValue, eaSpecular[j]->fRandomWeight)) {
							iTex = j;
							break;
						}
					}
				}
			}
			if (iTex >= 0) {
				SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, eaSpecular[iTex], pPart->hSpecularTexture);
				if (eaSpecular[iTex]->pValueOptions && eaSpecular[iTex]->pValueOptions->pcValueConstant)
				{
					if (!pPart->pTextureValues) {
						pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
					}
					if (pPart->pTextureValues) {
						pPart->pTextureValues->fSpecularValue = randomF32() * 100.0f;
					}
				}
			}
		}

		if ((!GET_REF(pPart->hPatternTexture) || !(costumeTailor_GetTextureFlags(GET_REF(pPart->hPatternTexture)) & kPCTextureType_Diffuse)) &&
		    (!GET_REF(pPart->hDetailTexture) || !(costumeTailor_GetTextureFlags(GET_REF(pPart->hDetailTexture)) & kPCTextureType_Diffuse)) &&
		    (!GET_REF(pPart->hSpecularTexture) || !(costumeTailor_GetTextureFlags(GET_REF(pPart->hSpecularTexture)) & kPCTextureType_Diffuse))) {
			costumeTailor_GetValidTextures(pPCCostume, eaMats[iMat], pSpecies, NULL, NULL, GET_REF(pPart->hGeoDef), NULL, eaUnlockedCostumes, kPCTextureType_Diffuse, &eaDiffuse, false, bSortDisplay, bUnlockAll);
			iTex = -1;
			if (pMirrorPart && GET_REF(pMirrorPart->hDiffuseTexture)) {
				iTex = costumeTailor_GetMatchingTexIndex(GET_REF(pMirrorPart->hDiffuseTexture), eaDiffuse);
			}
			if (iTex < 0) {
				fWeight = 0.0;
				for(j=eaSize(&eaDiffuse)-1; j>=0; --j) {
					costumeRandom_AddWeight(&fWeight, eaDiffuse[j]->fRandomWeight);
				}
				if (fWeight > 0) {
					assert(eaDiffuse);
					fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
					iTex = 0;
					for(j=eaSize(&eaDiffuse)-1; j>=0; --j) {
						if (costumeRandom_MatchWeight(&fValue, eaDiffuse[j]->fRandomWeight)) {
							iTex = j;
							break;
						}
					}
				}
			}
			if (iTex >= 0) {
				SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, eaDiffuse[iTex], pPart->hDiffuseTexture);
				if (eaDiffuse[iTex]->pValueOptions && eaDiffuse[iTex]->pValueOptions->pcValueConstant)
				{
					if (!pPart->pTextureValues) {
						pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
					}
					if (pPart->pTextureValues) {
						pPart->pTextureValues->fDiffuseValue = randomF32() * 100.0f;
					}
				}
			}
		}

		costumeTailor_GetValidTextures(pPCCostume, eaMats[iMat], pSpecies, NULL, NULL, GET_REF(pPart->hGeoDef), NULL, eaUnlockedCostumes, kPCTextureType_Movable, &eaMovable, false, bSortDisplay, bUnlockAll);
		if (pBone && pBone->bIsGuildEmblemBone)
		{
			if (bShowGuild)
			{
				if (!pPart->pMovableTexture) {
					pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
				}
				if (pPart->pMovableTexture) {
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pGuild->pcEmblem2, pPart->pMovableTexture->hMovableTexture);
					pPart->pMovableTexture->fMovableRotation = pGuild->fEmblem2Rotation;
					pPart->pMovableTexture->fMovableX = pGuild->fEmblem2X;
					pPart->pMovableTexture->fMovableY = pGuild->fEmblem2Y;
					pPart->pMovableTexture->fMovableScaleX = pGuild->fEmblem2ScaleX;
					pPart->pMovableTexture->fMovableScaleY = pGuild->fEmblem2ScaleY;
				}
				*((U32*)pPart->color2) = pGuild->iEmblem2Color0;
				*((U32*)pPart->color3) = pGuild->iEmblem2Color1;
				pPart->eColorLink = kPCColorLink_None;
			}
			else
			{
				//Strip invalid textures
				for(i=eaSize(&eaMovable)-1; i>=0; i--) {
					for (j = eaSize(&g_GuildEmblems.eaEmblems)-1; j >= 0; --j)
					{
						if (!stricmp(eaMovable[i]->pcName,REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[j]->hTexture))) {
							eaRemove(&eaMovable, i);
						}
					}
				}
			}
		}
		if ((pBone && !pBone->bIsGuildEmblemBone) || (!bShowGuild))
		{
			if ((!GET_REF(pPart->hPatternTexture) || !(costumeTailor_GetTextureFlags(GET_REF(pPart->hPatternTexture)) & kPCTextureType_Movable)) &&
				(!GET_REF(pPart->hDetailTexture) || !(costumeTailor_GetTextureFlags(GET_REF(pPart->hDetailTexture)) & kPCTextureType_Movable)) &&
				(!GET_REF(pPart->hSpecularTexture) || !(costumeTailor_GetTextureFlags(GET_REF(pPart->hSpecularTexture)) & kPCTextureType_Movable)) &&
				(!GET_REF(pPart->hDiffuseTexture) || !(costumeTailor_GetTextureFlags(GET_REF(pPart->hDiffuseTexture)) & kPCTextureType_Movable))) {
					iTex = -1;
					if (pMirrorPart && pMirrorPart->pMovableTexture && GET_REF(pMirrorPart->pMovableTexture->hMovableTexture)) {
						iTex = costumeTailor_GetMatchingTexIndex(GET_REF(pMirrorPart->pMovableTexture->hMovableTexture), eaMovable);
					}
					if (iTex < 0) {
						fWeight = 0.0;
						for(j=eaSize(&eaMovable)-1; j>=0; --j) {
							costumeRandom_AddWeight(&fWeight, eaMovable[j]->fRandomWeight);
						}
						if (fWeight > 0) {
							assert(eaMovable);
							fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
							iTex = 0;
							for(j=eaSize(&eaMovable)-1; j>=0; --j) {
								if (costumeRandom_MatchWeight(&fValue, eaMovable[j]->fRandomWeight)) {
									iTex = j;
									break;
								}
							}
						}
					}
					if (iTex >= 0) {
						F32 fMovableMinX, fMovableMaxX, fMovableMinY, fMovableMaxY;
						F32 fMovableMinScaleX, fMovableMaxScaleX, fMovableMinScaleY, fMovableMaxScaleY;
						bool bMovableCanEditPosition, bMovableCanEditRotation, bMovableCanEditScale;

						costumeTailor_GetTextureMovableValues((PCPart*)pPart, eaMovable[iTex], pSpecies,
							&fMovableMinX, &fMovableMaxX, &fMovableMinY, &fMovableMaxY,
							&fMovableMinScaleX, &fMovableMaxScaleX, &fMovableMinScaleY, &fMovableMaxScaleY,
							&bMovableCanEditPosition, &bMovableCanEditRotation, &bMovableCanEditScale);

						if (!pPart->pMovableTexture) {
							pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
						}
						SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, eaMovable[iTex], pPart->pMovableTexture->hMovableTexture);
						if (eaMovable[iTex]->pValueOptions && eaMovable[iTex]->pValueOptions->pcValueConstant)
						{
							pPart->pMovableTexture->fMovableValue = randomF32() * 100.0f;
						}
						if (bMovableCanEditPosition)
						{
							pPart->pMovableTexture->fMovableX = randomF32() * 100.0f;
							pPart->pMovableTexture->fMovableY = randomF32() * 100.0f;
						}
						else
						{
							pPart->pMovableTexture->fMovableX = (fMovableMaxX==fMovableMinX) ? 0 : (((eaMovable[iTex]->pMovableOptions->fMovableDefaultX - fMovableMinX) * 200.0f)/(fMovableMaxX - fMovableMinX)) - 100.0f;
							pPart->pMovableTexture->fMovableY = (fMovableMaxY==fMovableMinY) ? 0 : (((eaMovable[iTex]->pMovableOptions->fMovableDefaultY - fMovableMinY) * 200.0f)/(fMovableMaxY - fMovableMinY)) - 100.0f;
						}
						if (bMovableCanEditScale)
						{
							pPart->pMovableTexture->fMovableScaleX = randomPositiveF32() * 100.0f;
							pPart->pMovableTexture->fMovableScaleX = randomPositiveF32() * 100.0f;
						}
						else if (eaMovable[iTex]->pMovableOptions)
						{
							pPart->pMovableTexture->fMovableScaleX = (fMovableMaxScaleX==fMovableMinScaleX) ? 0 : (((eaMovable[iTex]->pMovableOptions->fMovableDefaultScaleX - fMovableMinScaleX) * 100.0f)/(fMovableMaxScaleX - fMovableMinScaleX));
							pPart->pMovableTexture->fMovableScaleY = (fMovableMaxScaleY==fMovableMinScaleY) ? 0 : (((eaMovable[iTex]->pMovableOptions->fMovableDefaultScaleY - fMovableMinScaleY) * 100.0f)/(fMovableMaxScaleY - fMovableMinScaleY));
						}
						if (bMovableCanEditRotation)
						{
							pPart->pMovableTexture->fMovableRotation = randomPositiveF32() * 100.0f;
						}
						else if (eaMovable[iTex]->pMovableOptions)
						{
							pPart->pMovableTexture->fMovableRotation = eaMovable[iTex]->pMovableOptions->fMovableDefaultRotation;
						}
					}
			}
		}
	}

	if (!pGuild)
	{
		if (pPCCostume->eDefaultColorLinkAll)
		{
			costumeTailor_SetPartColorLinking(pPCCostume, pPart, kPCColorLink_All, pSpecies, pSlotType);
		}
		else
		{
			costumeTailor_SetPartColorLinking(pPCCostume, pPart, kPCColorLink_MirrorGroup, pSpecies, pSlotType);
		}
	}

	if (pPCCostume->eDefaultMaterialLinkAll)
	{
		costumeTailor_SetPartMaterialLinking(pPCCostume, pPart, kPCColorLink_All, pSpecies, eaUnlockedCostumes, bUnlockAll);
	}
	else
	{
		costumeTailor_SetPartMaterialLinking(pPCCostume, pPart, kPCColorLink_MirrorGroup, pSpecies, eaUnlockedCostumes, bUnlockAll);
	}

	// Clean up;
	eaDestroy(&eaMats);
	eaDestroy(&eaPattern);
	eaDestroy(&eaDetail);
	eaDestroy(&eaSpecular);
	eaDestroy(&eaDiffuse);
	eaDestroy(&eaMovable);
}

// A convenience functions for the character creation random functions
static F32 costumeRandom_Gaussian(F32 fMin, F32 fMax, F32 fMode, F32 fSigma)
{
	F32 r = randomGaussian(gRandTable, NULL, fMode, fSigma);

	// if we go outside the range, distribute evenly. 
	// This is done to avoid excessive buildup at the tips
	// that would occur if it was clamped. 
	if (r < fMin || r > fMax)
		return (randomMersennePositiveF32(gRandTable) * (fMax - fMin)) + fMin;
	else
		return r;
}

static F32 costumeRandom_TrimodalGaussian(F32 fMin, F32 fMax, F32 fMode1, F32 fSigma1, F32 fWeight1, F32 fMode2, F32 fSigma2, F32 fWeight2, F32 fMode3, F32 fSigma3, F32 fWeight3)
{
	F32 fWeightSum = fWeight1 + fWeight2 + fWeight3;
	F32 fChoice = randomMersennePositiveF32(gRandTable);
	F32 fSigma, fMode;

	if (fChoice < (fWeight1/fWeightSum)) {
		fSigma = fSigma1;
		fMode = fMode1;
	} else if (fChoice < (fWeight1+fWeight2)/fWeightSum) {
		fSigma = fSigma2;
		fMode = fMode2;
	} else {
		fSigma = fSigma3;
		fMode = fMode3;
	}
	return costumeRandom_Gaussian(fMin, fMax, fMode, fSigma);
}

void costumeRandom_RandomHeight(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType)
{
	PCSkeletonDef *pSkel = GET_REF(pPCCostume->hSkeleton);
	F32 fHMin, fHMax;
	
	assert(pSkel);
	if(costumeTailor_GetOverrideHeightNoChange(pSpecies, pSlotType)) return;
	fHMin = costumeTailor_GetOverrideHeightMin(pSpecies, pSlotType);
	fHMax = costumeTailor_GetOverrideHeightMax(pSpecies, pSlotType);
	if(fHMax > fHMin)
	{
		pPCCostume->fHeight = costumeRandom_TrimodalGaussian(DEFAULT_TRIMODAL_DISRIBUTION(fHMin, fHMax));
		return;
	}
	pPCCostume->fHeight = costumeRandom_TrimodalGaussian(DEFAULT_TRIMODAL_DISRIBUTION(pSkel->fPlayerMinHeight, pSkel->fPlayerMaxHeight));
}

void costumeRandom_RandomMuscle(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType)
{
	PCSkeletonDef *pSkel = GET_REF(pPCCostume->hSkeleton);
	F32 fHMin, fHMax;
	
	assert(pSkel);
	if(costumeTailor_GetOverrideMuscleNoChange(pSpecies, pSlotType)) return;
	fHMin = costumeTailor_GetOverrideMuscleMin(pSpecies, pSlotType);
	fHMax = costumeTailor_GetOverrideMuscleMax(pSpecies, pSlotType);
	if(fHMax > fHMin)
	{
		pPCCostume->fMuscle = costumeRandom_TrimodalGaussian(DEFAULT_TRIMODAL_DISRIBUTION(fHMin, fHMax));
		return;
	}
	pPCCostume->fMuscle= costumeRandom_TrimodalGaussian(DEFAULT_TRIMODAL_DISRIBUTION(pSkel->fPlayerMinMuscle, pSkel->fPlayerMaxMuscle));
}

void costumeRandom_RandomBodyScales(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, const char* pchBodyScaleName)
{
	PCSkeletonDef *pSkel;
	PCStanceInfo **eaStances = NULL;
	NOCONST(PCScaleValue) *pScaleValue = NULL;
	int j;
	F32 fMin, fMax;

	pSkel = GET_REF(pPCCostume->hSkeleton);
	assert(pSkel);
	for(j=0; j<eaSize(&pSkel->eaBodyScaleInfo); ++j) {
		if (stricmp(pSkel->eaBodyScaleInfo[j]->pcName, pchBodyScaleName) == 0 || stricmp(pchBodyScaleName, "all") == 0)
		{
			while (eafSize(&pPCCostume->eafBodyScales) <= j) {
				eafPush(&pPCCostume->eafBodyScales, pSkel->eafDefaultBodyScales[eafSize(&pPCCostume->eafBodyScales)]);
			}
			
			if(costumeTailor_GetOverrideBodyScale(pSkel, pSkel->eaBodyScaleInfo[j]->pcName, pSpecies, pSlotType, &fMin, &fMax))
			{
				pPCCostume->eafBodyScales[j] = costumeRandom_TrimodalGaussian(DEFAULT_TRIMODAL_DISRIBUTION(fMin, fMax));
				continue;
			}
			if ((j < eafSize(&pSkel->eafPlayerMinBodyScales)) && (j < eafSize(&pSkel->eafPlayerMaxBodyScales)) && (j < eafSize(&pSkel->eafDefaultBodyScales))) {
				pPCCostume->eafBodyScales[j] = costumeRandom_TrimodalGaussian(DEFAULT_TRIMODAL_DISRIBUTION(pSkel->eafPlayerMinBodyScales[j], pSkel->eafPlayerMaxBodyScales[j]));
			} else {
				pPCCostume->eafBodyScales[j] = costumeRandom_TrimodalGaussian(DEFAULT_TRIMODAL_DISRIBUTION(0, 100));
			}
		}
	}
}

void costumeRandom_RandomBoneScales(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, const char* pchGroupname)
{
	PCSkeletonDef *pSkel;
	PCScaleInfo *pScale;
	PCStanceInfo **eaStances = NULL;
	NOCONST(PCScaleValue) *pScaleValue = NULL;
	F32 fValue;
	int j, k, l;
	F32 fMin, fMax;

	pSkel = GET_REF(pPCCostume->hSkeleton);
	assert(pSkel);

	for (j=0; j<eaSize(&pSkel->eaScaleInfoGroups); ++j) {
		if (stricmp(pSkel->eaScaleInfoGroups[j]->pcName, pchGroupname) == 0 || stricmp(pchGroupname, "all") == 0) {
			for (k=0; k<eaSize(&pSkel->eaScaleInfoGroups[j]->eaScaleInfo); ++k) {
				pScale = pSkel->eaScaleInfoGroups[j]->eaScaleInfo[k];
				if (pScale->eRestriction & (kPCRestriction_Player|kPCRestriction_Player_Initial)) {
					for (l=eaSize(&pPCCostume->eaScaleValues)-1; l>=0; l--) {
						if (!stricmp(pPCCostume->eaScaleValues[l]->pcScaleName, pScale->pcName)) {
							pScaleValue = pPCCostume->eaScaleValues[l];
							break;
						}
					}
					if(costumeTailor_GetOverrideBoneScale(pSkel, pScale, pScale->pcName, pSpecies, pSlotType, &fMin, &fMax))
					{
						fValue = costumeRandom_TrimodalGaussian(DEFAULT_TRIMODAL_DISRIBUTION(fMin, fMax));
					}
					else
					{
						fValue = costumeRandom_TrimodalGaussian(DEFAULT_TRIMODAL_DISRIBUTION(pScale->fPlayerMin, pScale->fPlayerMax));
					}
					if (!fValue) {
						if (l>=0) {
							StructDestroyNoConst(parse_PCScaleValue, pScaleValue);
							eaRemoveFast(&pPCCostume->eaScaleValues, l);
						}
					} else {
						if (l<0) {
							pScaleValue = StructCreateNoConst(parse_PCScaleValue);
							pScaleValue->pcScaleName = allocAddString(pScale->pcName);
							eaPush(&pPCCostume->eaScaleValues, pScaleValue);
						}
						pScaleValue->fValue = fValue;
					}
				}
			}
		}
	}
}

void costumeRandom_RandomStance(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, bool bSortDisplay, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll)
{
	PCSkeletonDef *pSkel;
	PCStanceInfo **eaStances = NULL;
	NOCONST(PCScaleValue) *pScaleValue = NULL;
	F32 fWeight, fValue;
	int j;
	F32 fRandomMorphologyRange = 0.60f; // Hard code a change of 60% for now

	pSkel = GET_REF(pPCCostume->hSkeleton);
	assert(pSkel);

	costumeTailor_GetValidStances(pPCCostume, pSpecies, pSlotType, &eaStances, bSortDisplay, eaUnlockedCostumes, bUnlockAll);
	fWeight = 0.0;
	for(j=eaSize(&eaStances)-1; j>=0; --j) {
		costumeRandom_AddWeight(&fWeight, eaStances[j]->fRandomWeight);
	}
	if ((fWeight > 0) && (eaSize(&eaStances) > 0)) {
		int iStance = 0;
		fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
		for(j=eaSize(&eaStances)-1; j>=0; --j) {
			if (costumeRandom_MatchWeight(&fValue, eaStances[j]->fRandomWeight)) {
				iStance = j;
				break;
			}
		}
		pPCCostume->pcStance = allocAddString(eaStances[iStance]->pcName);
	} else if (pPCCostume->pcStance) {
		pPCCostume->pcStance = NULL;
		if (pSpecies && pSpecies->pcDefaultStance) {
			pPCCostume->pcStance = allocAddString(pSpecies->pcDefaultStance);
		} else if (pSkel->pcDefaultStance) {
			pPCCostume->pcStance = allocAddString(pSkel->pcDefaultStance);
		}
	}
	eaDestroy(&eaStances);
}

// Randomize the morphology of the costume
void costumeRandom_RandomMorphology(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, const char *pchScaleGroup, bool bSortDisplay, bool bRandomizeBoneScales, bool bRandomizeNonBoneScales, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll)
{
	PCSkeletonDef *pSkel = GET_REF(pPCCostume->hSkeleton);
	assert(pSkel);

	if (bRandomizeNonBoneScales)
	{
		costumeRandom_RandomHeight(pPCCostume, pSpecies, pSlotType);
		costumeRandom_RandomMuscle(pPCCostume, pSpecies, pSlotType);
		costumeRandom_RandomBodyScales(pPCCostume, pSpecies, pSlotType, "all");
		costumeRandom_RandomStance(pPCCostume, pSpecies, pSlotType, bSortDisplay, eaUnlockedCostumes, bUnlockAll);
	}
	if (bRandomizeBoneScales)
	{
		costumeRandom_RandomBoneScales(pPCCostume, pSpecies, pSlotType, pchScaleGroup ? pchScaleGroup : "all");
	}
}


// Randomize the parts of the costume
void costumeRandom_RandomParts(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, Guild *pGuild, PCRegion *pRegion, PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, bool bSymmetry, bool bBoneGroupMatching, bool bSortDisplay, bool bUnlockAll, bool bRandomizeNonParts)
{
	PCSkeletonDef *pSkel;
	PCBoneDef **eaBones = NULL;
	PCGeometryDef **eaGeos = NULL;
	PCRegion **eaRegions = NULL;
	PCCategory **eaCategories = NULL;
	NOCONST(PCPart) *pPart;
	PCBoneDef *pChildBone;
	PCCategory *pCat;
	PCGeometryDef *pParentGeo;
	PCColorQuadSet *pQuadSet;
	UIColorSet *pColorSet = NULL;
	const char *pcGroup = NULL;
	int i, j, iCat, iBone, iGeo, iColor;
	F32 fRand;
	U8 noglow[4] = {0, 0, 0, 0};
	U8 skinPink[4] = { 212, 137, 114, 255 };
	U8 tempColor[4];
	F32 fWeight;
	F32 fValue;

	// Get the skeleton
	pSkel = GET_REF(pPCCostume->hSkeleton);
	if (!pSkel) {
		return;
	}

	if (pRegion && !pRegion->pcName) pRegion = NULL;

	// First remove all current parts and current region categories
	for (i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
		if (pRegion)
		{
			PCBoneDef *b = GET_REF(pPCCostume->eaParts[i]->hBoneDef);
			if (b)
			{
				if (GET_REF(b->hRegion) != pRegion) continue;
			}
		}
		StructDestroyNoConst(parse_PCPart, pPCCostume->eaParts[i]);
		eaRemove(&pPCCostume->eaParts, i);
	}
	for (i=eaSize(&pPCCostume->eaRegionCategories)-1; i>=0; --i) {
		if (pRegion)
		{
			NOCONST(PCRegionCategory) *rc = pPCCostume->eaRegionCategories[i];
			if (rc)
			{
				if (GET_REF(rc->hRegion) != pRegion) continue;
			}
		}
		StructDestroyNoConst(parse_PCRegionCategory, pPCCostume->eaRegionCategories[i]);
		eaRemove(&pPCCostume->eaRegionCategories, i);
	}

	// Pick a category for each region
	costumeTailor_GetValidRegions(pPCCostume, pSpecies, eaUnlockedCostumes, NULL, pSlotType, &eaRegions, CGVF_OMIT_EMPTY | CGVF_REQUIRE_POWERFX | (bUnlockAll ? CGVF_UNLOCK_ALL : 0));
	costumeTailor_SortRegionsOnDependency(eaRegions);
	for(i=0; i<eaSize(&eaRegions); ++i) { // This needs to be positive direction
		if (pRegion && (!eaRegions[i]->pcName || stricmp(pRegion->pcName,eaRegions[i]->pcName))) continue;
		costumeTailor_GetValidCategories(pPCCostume, eaRegions[i], pSpecies, NULL, NULL, pSlotType, &eaCategories, CGVF_OMIT_EMPTY | CGVF_REQUIRE_POWERFX | (bUnlockAll ? CGVF_UNLOCK_ALL : 0));
		fWeight = 0.0;
		for(j=eaSize(&eaCategories)-1; j>=0; --j) {
			costumeRandom_AddWeight(&fWeight, eaCategories[j]->fRandomWeight);
		}
		if ((fWeight > 0) && (eaSize(&eaCategories) > 0)) {
			NOCONST(PCRegionCategory) *pRegCat;

			fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
			iCat = 0;
			for(j=eaSize(&eaCategories)-1; j>=0; --j) {
				if (costumeRandom_MatchWeight(&fValue, eaCategories[j]->fRandomWeight)) {
					iCat = j;
					break;
				}
			}

			pRegCat = StructCreateNoConst(parse_PCRegionCategory);
			SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, eaRegions[i], pRegCat->hRegion);
			SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, eaCategories[iCat], pRegCat->hCategory);
			eaPush(&pPCCostume->eaRegionCategories, pRegCat);
		}
		eaDestroy(&eaCategories);
	}
	eaDestroy(&eaRegions);

	// Get the list of bones
	costumeTailor_GetValidBones(pPCCostume, GET_REF(pPCCostume->hSkeleton), NULL, NULL, pSpecies, eaUnlockedCostumes, NULL, &eaBones, CGVF_REQUIRE_POWERFX | CGVF_SORT_DISPLAY | (bUnlockAll ? CGVF_UNLOCK_ALL : 0));

	// Add each bone
	for (iBone=0; iBone<eaSize(&eaBones); ++iBone) {
		bool bFillBone = false;
		NOCONST(PCPart)*pMirrorPart = NULL;
		iGeo = -1;

		if (eaBones[iBone]->bIsChildBone) {
			continue; // Don't add random to child bones directly
		}

		if (pRegion && GET_REF(eaBones[iBone]->hRegion) != pRegion)
		{
			continue;
		}

		pCat = costumeTailor_GetCategoryForRegion((PlayerCostume*)pPCCostume, GET_REF(eaBones[iBone]->hRegion));

		// Find out if the bone is required
		for(i=eaSize(&pSkel->eaRequiredBoneDefs)-1; i>=0; --i) {
			if (GET_REF(pSkel->eaRequiredBoneDefs[i]->hBone) == eaBones[iBone]) {
				bFillBone = true;
				break;
			}
		}
		if (!bFillBone && pCat) {
			for(i=eaSize(&pCat->eaRequiredBones)-1; i>=0; --i) {
				if (GET_REF(pCat->eaRequiredBones[i]->hBone) == eaBones[iBone]) {
					bFillBone = true;
					break;
				}
			}
		}
		if (!bFillBone && pSpecies) {
			for(i=eaSize(&pSpecies->eaBoneData)-1; i>=0; --i) {
				if (GET_REF(pSpecies->eaBoneData[i]->hBone) == eaBones[iBone]) {
					bFillBone = pSpecies->eaBoneData[i]->bRequires;
					break;
				}
			}
		}

		// See if should happen because of a mirror bone relationship
		if (bSymmetry && GET_REF(eaBones[iBone]->hMirrorBone)) {
			PCBoneDef *pMirrorBone = GET_REF(eaBones[iBone]->hMirrorBone);
			// See if mirror bone already processed
			for(j=iBone-1; j>=0; --j) {
				if (eaBones[j] == pMirrorBone) {
					// already processed... see if it chose a part
					for(j=eaSize(&pPCCostume->eaParts)-1; j>=0; --j) {
						if (GET_REF(pPCCostume->eaParts[j]->hBoneDef) == pMirrorBone) {
							pMirrorPart = pPCCostume->eaParts[j];
							break;
						}
					}
					break;
				}
			}
			if ((bFillBone && (randomMersennePositiveF32(gRandTable) < gConf.fCostumeMirrorChanceRequired)) ||
				(!bFillBone && (randomMersennePositiveF32(gRandTable) < gConf.fCostumeMirrorChanceOptional))) {
				// Want to follow the mirror
				if (pMirrorPart) {
					costumeTailor_GetValidGeos(pPCCostume, GET_REF(pPCCostume->hSkeleton), eaBones[iBone], pCat, pSpecies, eaUnlockedCostumes, &eaGeos, false, false, bSortDisplay, bUnlockAll);
					iGeo = costumeTailor_GetMatchingGeoIndex(GET_REF(pMirrorPart->hGeoDef), eaGeos);
					if (iGeo >= 0) {
						bFillBone = true;
					} else {
						eaDestroy(&eaGeos);
						pMirrorPart = NULL;
					}
					// Else There is no matching part so drop through and leave to chance
				} else if (!bFillBone) {
					// Don't want the mirror part either
					continue;
				}
			} else {
				// Want to ignore the mirror
				if (pMirrorPart) {
					pMirrorPart = NULL;
				}
			}
		}

		if (bBoneGroupMatching && !pMirrorPart)
		{
			int k = -1, l, m, n;
			//Find if bone is part of a group
			for(l=eaSize(&pSkel->eaBoneGroups)-1; l>=0; --l)
			{
				if (!(pSkel->eaBoneGroups[l]->eBoneGroupFlags & kPCBoneGroupFlags_MatchGeos)) continue;
				for(k=eaSize(&pSkel->eaBoneGroups[l]->eaBoneInGroup)-1; k>=0; --k)
				{
					if (eaBones[iBone] == GET_REF(pSkel->eaBoneGroups[l]->eaBoneInGroup[k]->hBone))
					{
						break;
					}
				}
				if (k>=0) break;
			}
			if (k >= 0)
			{
				//Find if a bone in the group was already processed
				for(j=iBone-1; j>=0; --j)
				{
					for(k=eaSize(&pSkel->eaBoneGroups[l]->eaBoneInGroup)-1; k>=0; --k)
					{
						if (eaBones[j] == GET_REF(pSkel->eaBoneGroups[l]->eaBoneInGroup[k]->hBone))
						{
							break;
						}
					}
					if (k >= 0)
					{
						// already processed... see if it chose a part
						for(m=eaSize(&pPCCostume->eaParts)-1; m>=0; --m)
						{
							if (GET_REF(pPCCostume->eaParts[m]->hBoneDef) == eaBones[j])
							{
								pMirrorPart = pPCCostume->eaParts[m];
								break;
							}
						}
						break;
					}
					else
					{
						//Maybe it is a child bone
						n = -1;
						for(k=eaSize(&pSkel->eaBoneGroups[l]->eaBoneInGroup)-1; k>=0; --k)
						{
							for(n=eaSize(&eaBones[j]->eaChildBones)-1; n>=0; --n)
							{
								if (GET_REF(eaBones[j]->eaChildBones[n]->hChildBone) == GET_REF(pSkel->eaBoneGroups[l]->eaBoneInGroup[k]->hBone))
								{
									break;
								}
							}
							if (n>=0) break;
						}
						if (n >= 0)
						{
							// already processed... see if it chose a part
							for(m=eaSize(&pPCCostume->eaParts)-1; m>=0; --m)
							{
								if (GET_REF(pPCCostume->eaParts[m]->hBoneDef) == GET_REF(eaBones[j]->eaChildBones[n]->hChildBone))
								{
									pMirrorPart = pPCCostume->eaParts[m];
									break;
								}
							}
							break;
						}
					}
				}
				//Want to follow the group part
				if (pMirrorPart) {
					costumeTailor_GetValidGeos(pPCCostume, GET_REF(pPCCostume->hSkeleton), eaBones[iBone], pCat, pSpecies, eaUnlockedCostumes, &eaGeos, false, false, bSortDisplay, bUnlockAll);
					iGeo = costumeTailor_GetMatchingGeoIndex(GET_REF(pMirrorPart->hGeoDef), eaGeos);
					if (iGeo >= 0) {
						bFillBone = true;
					} else {
						eaDestroy(&eaGeos);
						pMirrorPart = NULL;
					}
					// Else There is no matching part so drop through and leave to chance
				} else if (!bFillBone) {
					// Don't want the group part either
					continue;
				}
			}
		}

		if (!bFillBone && (eaBones[iBone]->fRandomChance >= 0)) {
			F32 fChance = (eaBones[iBone]->fRandomChance == 0) ? 0.1 : eaBones[iBone]->fRandomChance;
			// Random chance of bone getting a part
			fRand = randomMersennePositiveF32(gRandTable);
			if (fRand < fChance) {
				bFillBone = true;
			}
		}

		if (!bFillBone) {
			continue; // Decided not to make a part
		}

		// Pick a geometry for the bone
		// Skip if picked a geo due to mirror part
		if (iGeo < 0) {
			costumeTailor_GetValidGeos(pPCCostume, GET_REF(pPCCostume->hSkeleton), eaBones[iBone], pCat, pSpecies, eaUnlockedCostumes, &eaGeos, false, false, bSortDisplay, bUnlockAll);
			fWeight = 0.0;
			for(j=eaSize(&eaGeos)-1; j>=0; --j) {
				costumeRandom_AddWeight(&fWeight, eaGeos[j]->fRandomWeight);
			}
			if (fWeight <= 0) {
				continue; // No geo = no part
			}
			fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
			iGeo = 0;
			for(j=eaSize(&eaGeos)-1; j>=0; --j) {
				if (costumeRandom_MatchWeight(&fValue, eaGeos[j]->fRandomWeight)) {
					iGeo = j;
					break;
				}
			}
		}

		// Make the part
		pPart = StructCreateNoConst(parse_PCPart);
		SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, eaBones[iBone], pPart->hBoneDef);
		SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, eaGeos[iGeo], pPart->hGeoDef);
		pParentGeo = eaGeos[iGeo];

		costumeRandom_FillRandomPart(pPCCostume, pPart, pMirrorPart, pSpecies, pSlotType, pGuild, eaUnlockedCostumes, bSortDisplay, bUnlockAll);

		// Clean up;
		eaDestroy(&eaGeos);

		// Keep the part
		eaPush(&pPCCostume->eaParts, pPart);

		for(j=eaSize(&eaBones[iBone]->eaChildBones)-1; j>=0; --j) {
			PCChildBone *pBoneInfo = eaBones[iBone]->eaChildBones[j];
			bool bRequired = false;
			int k;

			// Decide if should pick a child part
			pChildBone = GET_REF(pBoneInfo->hChildBone);
			if (!pChildBone || (pChildBone->fRandomChance < 0)) {
				continue;
			}

			if (pParentGeo->pOptions)
			{
				for(k=eaSize(&pParentGeo->pOptions->eaChildGeos)-1; k>=0; --k) {
					if (pParentGeo->pOptions->eaChildGeos[k]->bRequiresChild) {
						bRequired = true;
						break;
					}
				}
			}

			iGeo = -1;
			pMirrorPart = NULL;
			if (bBoneGroupMatching)
			{
				int l, m, n, o;
				k = -1;
				//Find if bone is part of a group
				for(l=eaSize(&pSkel->eaBoneGroups)-1; l>=0; --l)
				{
					if (!(pSkel->eaBoneGroups[l]->eBoneGroupFlags & kPCBoneGroupFlags_MatchGeos)) continue;
					for(k=eaSize(&pSkel->eaBoneGroups[l]->eaBoneInGroup)-1; k>=0; --k)
					{
						if (pChildBone == GET_REF(pSkel->eaBoneGroups[l]->eaBoneInGroup[k]->hBone))
						{
							break;
						}
					}
					if (k>=0) break;
				}
				if (k >= 0)
				{
					//Find if a bone in the group was already processed
					for(n=iBone-1; n>=0; --n)
					{
						for(k=eaSize(&pSkel->eaBoneGroups[l]->eaBoneInGroup)-1; k>=0; --k)
						{
							if (eaBones[n] == GET_REF(pSkel->eaBoneGroups[l]->eaBoneInGroup[k]->hBone))
							{
								break;
							}
						}
						if (k >= 0)
						{
							// already processed... see if it chose a part
							for(m=eaSize(&pPCCostume->eaParts)-1; m>=0; --m)
							{
								if (GET_REF(pPCCostume->eaParts[m]->hBoneDef) == eaBones[n])
								{
									pMirrorPart = pPCCostume->eaParts[m];
									break;
								}
							}
							break;
						}
						else
						{
							//Maybe it is a child bone
							o = -1;
							for(k=eaSize(&pSkel->eaBoneGroups[l]->eaBoneInGroup)-1; k>=0; --k)
							{
								for(o=eaSize(&eaBones[n]->eaChildBones)-1; o>=0; --o)
								{
									if (GET_REF(eaBones[n]->eaChildBones[o]->hChildBone) == GET_REF(pSkel->eaBoneGroups[l]->eaBoneInGroup[k]->hBone))
									{
										break;
									}
								}
								if (o>=0) break;
							}
							if (o >= 0)
							{
								// already processed... see if it chose a part
								for(m=eaSize(&pPCCostume->eaParts)-1; m>=0; --m)
								{
									if (GET_REF(pPCCostume->eaParts[m]->hBoneDef) == GET_REF(eaBones[n]->eaChildBones[o]->hChildBone))
									{
										pMirrorPart = pPCCostume->eaParts[m];
										break;
									}
								}
								break;
							}
						}
					}
					//Want to follow the group part
					if (pMirrorPart) {
						costumeTailor_GetValidGeos(pPCCostume, GET_REF(pPCCostume->hSkeleton), pChildBone, pCat, pSpecies, eaUnlockedCostumes, &eaGeos, false, false, bSortDisplay, bUnlockAll);
						iGeo = costumeTailor_GetMatchingGeoIndex(GET_REF(pMirrorPart->hGeoDef), eaGeos);
						if (iGeo >= 0) {
							bFillBone = true;
						} else {
							eaDestroy(&eaGeos);
							pMirrorPart = NULL;
						}
						// Else There is no matching part so drop through and leave to chance
					} else if (k >= 0) {
						// Don't want the group part either
						if (!bRequired) continue;
					}
				}
			}

			if (!pMirrorPart)
			{
				if (!bRequired) {
					F32 fChance = (pChildBone->fRandomChance == 0) ? 0.1 : pChildBone->fRandomChance;
					// Random chance of bone getting a part
					fRand = randomMersennePositiveF32(gRandTable);
					if (fRand >= fChance) {
						continue;
					}
				}
			}

			// Fill child part
			// Skip if picked a geo due to bone group part
			if (iGeo < 0) {
				costumeTailor_GetValidChildGeos(pPCCostume, pCat, pParentGeo, GET_REF(pBoneInfo->hChildBone), pSpecies, eaUnlockedCostumes, &eaGeos, bSortDisplay, bUnlockAll);
				fWeight = 0.0;
				for(k=eaSize(&eaGeos)-1; k>=0; --k) {
					costumeRandom_AddWeight(&fWeight, eaGeos[k]->fRandomWeight);
				}
				if (fWeight <= 0) {
					continue; // No geo = no part
				}
				fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
				iGeo = 0;
				for(k=eaSize(&eaGeos)-1; k>=0; --k) {
					if (costumeRandom_MatchWeight(&fValue, eaGeos[k]->fRandomWeight)) {
						iGeo = k;
						break;
					}
				}
			}

			// Make the part
			pPart = StructCreateNoConst(parse_PCPart);
			SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, pChildBone, pPart->hBoneDef);
			SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, eaGeos[iGeo], pPart->hGeoDef);

			costumeRandom_FillRandomPart(pPCCostume, pPart, pMirrorPart, pSpecies, pSlotType, pGuild, eaUnlockedCostumes, bSortDisplay, bUnlockAll);

			// Clean up;
			eaDestroy(&eaGeos);

			// Keep the part
			eaPush(&pPCCostume->eaParts, pPart);
		}
	}

	//
	// Pick colors
	//
	fWeight = 0.0f;
	pQuadSet = NULL;

	//Find Default Color Quad Set
	pQuadSet = costumeTailor_GetColorQuadSetForPart(pPCCostume, pSpecies, NULL);
	if (pQuadSet)
	{
		fWeight = 0.0f;
		for(j=eaSize(&pQuadSet->eaColorQuads)-1; j>=0; --j) {
			costumeRandom_AddWeight(&fWeight, pQuadSet->eaColorQuads[j]->fRandomWeight);
		}
		if (fWeight <= 0.0f)
		{
			pQuadSet = NULL;
		}
	}

	{
		PCColorQuadSet *pPartQuadSet, **eaComputedQuadList = NULL;
		float fPartWeight;
		int k, iPartColor, *eaPickedColorList = NULL;

		//We Have Default Color Quad Set; Now Find Random Color Quad
		iColor = 0;
		if (pQuadSet)
		{
			fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
			iColor = 0;
			assert(pQuadSet->eaColorQuads);
			for(j=eaSize(&pQuadSet->eaColorQuads)-1; j>=0; --j) {
				if (costumeRandom_MatchWeight(&fValue, pQuadSet->eaColorQuads[j]->fRandomWeight)) {
					iColor = j;
					break;
				}
			}
		}

		for (j=eaSize(&pPCCostume->eaParts)-1; j>=0; --j)
		{
			PCBoneDef *b = GET_REF(pPCCostume->eaParts[j]->hBoneDef);
			if (b && b->bIsGuildEmblemBone) continue;
			pPartQuadSet = NULL;
			fPartWeight = 0.0f;
			if (pRegion)
			{
				//If Region is defined only choose colors for this Region
				if (b)
				{
					if (GET_REF(b->hRegion) != pRegion) continue;
				}
			}
			pPartQuadSet = costumeTailor_GetColorQuadSetForPart(pPCCostume, pSpecies, pPCCostume->eaParts[j]);
			if (pPartQuadSet == pQuadSet) pPartQuadSet = NULL;
			if (pPartQuadSet)
			{
				fPartWeight = 0.0f;
				for(k=eaSize(&pPartQuadSet->eaColorQuads)-1; k>=0; --k) {
					costumeRandom_AddWeight(&fPartWeight, pPartQuadSet->eaColorQuads[k]->fRandomWeight);
				}
				if (fPartWeight <= 0.0f)
				{
					pPartQuadSet = NULL;
				}
			}
			if (!pPartQuadSet)
			{
				//If there is no Species Bone or Bone with a Color Quad Set use the default
				pPartQuadSet = pQuadSet;
				iPartColor = iColor;
			}
			else
			{
				for (k=eaSize(&eaComputedQuadList)-1; k>=0; --k)
				{
					if (eaComputedQuadList[k] == pPartQuadSet) break;
				}

				if (k < 0)
				{
					fValue = (randomMersennePositiveF32(gRandTable) * fPartWeight);
					iPartColor = 0;
					assert(pPartQuadSet->eaColorQuads);
					for(k=eaSize(&pPartQuadSet->eaColorQuads)-1; k>=0; --k) {
						if (costumeRandom_MatchWeight(&fValue, pPartQuadSet->eaColorQuads[k]->fRandomWeight)) {
							iPartColor = k;
							break;
						}
					}

					//Keep the color selection for each ColorQuad and reuse it
					eaPush(&eaComputedQuadList, pPartQuadSet);
					ea32Push(&eaPickedColorList, iPartColor);
				}
				else
				{
					iPartColor = eaPickedColorList[k];
				}
			}
			if (!pPartQuadSet)
			{
				U8 color0[4] = {g_StaticQuad.color0[0],g_StaticQuad.color0[1],g_StaticQuad.color0[2],g_StaticQuad.color0[3]};
				U8 color1[4] = {g_StaticQuad.color1[0],g_StaticQuad.color1[1],g_StaticQuad.color1[2],g_StaticQuad.color1[3]};
				U8 color2[4] = {g_StaticQuad.color2[0],g_StaticQuad.color2[1],g_StaticQuad.color2[2],g_StaticQuad.color2[3]};
				U8 color3[4] = {g_StaticQuad.color3[0],g_StaticQuad.color3[1],g_StaticQuad.color3[2],g_StaticQuad.color3[3]};
				int result_flags = costumeTailor_IsColorValidForPart(pPCCostume, pSpecies, pSlotType, pPCCostume->eaParts[j],
																color0, color1, color2, color3, true);
				if (!(result_flags & kPCColorFlags_Color0)) costumeTailor_FindClosestColor(color0, costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPCCostume->eaParts[j], 0), color0);
				if (!(result_flags & kPCColorFlags_Color1)) costumeTailor_FindClosestColor(color1, costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPCCostume->eaParts[j], 1), color1);
				if (!(result_flags & kPCColorFlags_Color2)) costumeTailor_FindClosestColor(color2, costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPCCostume->eaParts[j], 2), color2);
				if (!(result_flags & kPCColorFlags_Color3)) costumeTailor_FindClosestColor(color3, costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPCCostume->eaParts[j], 3), color3);
				costumeTailor_SetPartColors(pPCCostume, pSpecies, pSlotType, pPCCostume->eaParts[j], 
									color0, color1, color2, color3, noglow);
			}
			else
			{
				U8 color0[4] = {pPartQuadSet->eaColorQuads[iPartColor]->color0[0],pPartQuadSet->eaColorQuads[iPartColor]->color0[1],pPartQuadSet->eaColorQuads[iPartColor]->color0[2],pPartQuadSet->eaColorQuads[iPartColor]->color0[3]};
				U8 color1[4] = {pPartQuadSet->eaColorQuads[iPartColor]->color1[0],pPartQuadSet->eaColorQuads[iPartColor]->color1[1],pPartQuadSet->eaColorQuads[iPartColor]->color1[2],pPartQuadSet->eaColorQuads[iPartColor]->color1[3]};
				U8 color2[4] = {pPartQuadSet->eaColorQuads[iPartColor]->color2[0],pPartQuadSet->eaColorQuads[iPartColor]->color2[1],pPartQuadSet->eaColorQuads[iPartColor]->color2[2],pPartQuadSet->eaColorQuads[iPartColor]->color2[3]};
				U8 color3[4] = {pPartQuadSet->eaColorQuads[iPartColor]->color3[0],pPartQuadSet->eaColorQuads[iPartColor]->color3[1],pPartQuadSet->eaColorQuads[iPartColor]->color3[2],pPartQuadSet->eaColorQuads[iPartColor]->color3[3]};
				int result_flags = costumeTailor_IsColorValidForPart(pPCCostume, pSpecies, pSlotType, pPCCostume->eaParts[j],
																color0, color1, color2, color3, true);
				if (!(result_flags & kPCColorFlags_Color0)) costumeTailor_FindClosestColor(color0, costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPCCostume->eaParts[j], 0), color0);
				if (!(result_flags & kPCColorFlags_Color1)) costumeTailor_FindClosestColor(color1, costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPCCostume->eaParts[j], 1), color1);
				if (!(result_flags & kPCColorFlags_Color2)) costumeTailor_FindClosestColor(color2, costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPCCostume->eaParts[j], 2), color2);
				if (!(result_flags & kPCColorFlags_Color3)) costumeTailor_FindClosestColor(color3, costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPCCostume->eaParts[j], 3), color3);
				costumeTailor_SetPartColors(pPCCostume, pSpecies, pSlotType, pPCCostume->eaParts[j], 
									color0, color1, color2, color3, noglow);
			}
		}

		eaDestroy(&eaComputedQuadList);
		ea32Destroy(&eaPickedColorList);
	}

	// Pick skin color
	if (bRandomizeNonParts)
	{
		pColorSet = costumeTailor_GetOverrideSkinColorSet(pSpecies, pSlotType);
		if (!pColorSet) pColorSet = GET_REF(pSkel->hSkinColorSet);
		if (!pColorSet) {
			pColorSet = RefSystem_ReferentFromString(g_hCostumeColorsDict, "Core_Skin");
		}
		if (pColorSet) {
			fWeight = 0.0;
			for(j=eaSize(&pColorSet->eaColors)-1; j>=0; --j) {
				costumeRandom_AddWeight(&fWeight, pColorSet->eaColors[j]->fRandomWeight);
			}
			if (fWeight > 0) {
				fValue = (randomMersennePositiveF32(gRandTable) * fWeight);
				iColor = 0;
				assert(pColorSet->eaColors);
				for(j=eaSize(&pColorSet->eaColors)-1; j>=0; --j) {
					if (costumeRandom_MatchWeight(&fValue, pColorSet->eaColors[j]->fRandomWeight)) {
						iColor = j;
						break;
					}
				}
				VEC4_TO_COSTUME_COLOR(pColorSet->eaColors[iColor]->color, tempColor);
				costumeTailor_SetSkinColor(pPCCostume, tempColor);
			}
		} else {
			costumeTailor_SetSkinColor(pPCCostume, skinPink);
		}
	}

	eaDestroy(&eaBones);
}


// Populates the costume with random parts based on the skeleton and type
void costumeRandom_FillRandom(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, Guild *pGuild, PCRegion *pRegion, const char *pchScaleGroup, PlayerCostume **eaUnlockedCostumes, 
	PCSlotType *pSlotType, bool bSymmetry, bool bBoneGroupMatching, bool bSortDisplay, bool bUnlockAll, bool bRandomizeNonParts, bool bRandomizeBoneScales, bool bRandomizeNonBoneScales)
{
	costumeRandom_RandomParts(pPCCostume, pSpecies, pGuild, pRegion, eaUnlockedCostumes, pSlotType, bSymmetry, bBoneGroupMatching, bSortDisplay, bUnlockAll, bRandomizeNonParts);
	costumeRandom_RandomMorphology(pPCCostume, pSpecies, pSlotType, pchScaleGroup, bSortDisplay, bRandomizeBoneScales, bRandomizeNonBoneScales, eaUnlockedCostumes, bUnlockAll);
}

