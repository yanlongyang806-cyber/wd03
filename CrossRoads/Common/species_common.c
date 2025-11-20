/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#include "Character.h"
#include "CostumeCommon.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "Entity.h"
#include "error.h"
#include "estring.h"
#include "file.h"
#include "ReferenceSystem.h"
#include "ResourceManager.h"
#include "species_common.h"
#include "Powers.h"
#include "GameAccountDataCommon.h"
#include "Allegiance.h"
#include "CharacterClass.h"
#include "StringUtil.h"
#include "Entity.h"
#include "Character.h"
#include "PowerTree.h"
#include "Expression.h"

#include "AutoGen/species_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// --------------------------------------------------------------------------
// Static Data
// --------------------------------------------------------------------------

DictionaryHandle g_hSpeciesDict = NULL;
DictionaryHandle g_hSpeciesDefiningDict = NULL;
DictionaryHandle g_hSpeciesGenDataDict = NULL;
DictionaryHandle g_hCostumePresetCatDict = NULL;


// --------------------------------------------------------------------------
// Expression Functions
// --------------------------------------------------------------------------

// Returns true if the Entity's Character's Species has the SpeciesGroup.
//  This is NOT the same thing as the Species' internal name.
AUTO_EXPR_FUNC(entityutil);
S32 EntIsSpeciesGroup(SA_PARAM_OP_VALID Entity *entity, const char *speciesGroup)
{
	if(entity && entity->pChar)
	{
		SpeciesDef *pSpecies = GET_REF(entity->pChar->hSpecies);
		if(pSpecies)
			return (stricmp(pSpecies->pcSpeciesGroup,speciesGroup)==0);
	}
	return false;
}


// --------------------------------------------------------------------------
// Dictionary Management
// --------------------------------------------------------------------------

static void validatePresetCat(CostumePresetCategory *pDef)
{
	if (!GET_REF(pDef->displayNameMsg.hMessage)) {
		if (REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage)) {
			ErrorFilenamef(pDef->pcFileName,"PresetCategory '%s' refers to non-existent message '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage));
		}
	}

	if (pDef->pcSlotType)
	{
		PCSlotType *temp = costumeLoad_GetSlotType(pDef->pcSlotType);
		if (!temp) {
			ErrorFilenamef(pDef->pcFileName,"PresetCategory '%s' costume slot refers to non-existent slot type '%s'\n",pDef->pcName,pDef->pcSlotType);
		}
	}
}

static void validateSpecies(SpeciesDef *pDef)
{
	bool bFound;
	int i, j, k;
	PCSkeletonDef *skel = GET_REF(pDef->hSkeleton);
	SpeciesBoneData *boneData;
	PCColorQuadSet *pQuadSet;
	UIColorSet *pSkinColor;
	UIColorSet *pBodyColor[4];

	if (!resIsValidName(pDef->pcName)) {
		ErrorFilenamef(pDef->pcFileName,"Species '%s' does not have a valid name\n",pDef->pcName);
	}

	if (!GET_REF(pDef->displayNameMsg.hMessage)) {
		if (REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage)) {
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent message '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage));
		} else {
			ErrorFilenamef(pDef->pcFileName,"Species '%s' has no display name\n",pDef->pcName);
		}
	}
	if (!GET_REF(pDef->genderNameMsg.hMessage)) {
		if (REF_STRING_FROM_HANDLE(pDef->genderNameMsg.hMessage)) {
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent message '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->genderNameMsg.hMessage));
		}
	}
	if (!GET_REF(pDef->descriptionMsg.hMessage)) {
		if (REF_STRING_FROM_HANDLE(pDef->descriptionMsg.hMessage)) {
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent message '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->descriptionMsg.hMessage));
		}
	}

	for(i=eaSize(&pDef->eaPresets)-1; i>=0; --i) {
		CostumePreset *pPreset = pDef->eaPresets[i];
		CostumePresetCategory *pPresetCat = GET_REF(pPreset->hPresetCategory);
		if ((!pPreset->pcName) || (!*pPreset->pcName))
		{
			if ((!pPresetCat) || (!pPresetCat->pcName) || (!*pPresetCat->pcName))
			{
				ErrorFilenamef(pDef->pcFileName,"Species '%s' preset %d does not have a valid name. (Must exist on preset or preset category)\n",pDef->pcName,i+1);
			}
		}
		if (!GET_REF(pPreset->displayNameMsg.hMessage)) {
			if (pPresetCat)
			{
				if (!GET_REF(pPresetCat->displayNameMsg.hMessage)) {
					if (REF_STRING_FROM_HANDLE(pPresetCat->displayNameMsg.hMessage)) {
						ErrorFilenamef(pDef->pcFileName,"Species '%s' preset %d category refers to non-existent message '%s'\n",pDef->pcName,i+1,REF_STRING_FROM_HANDLE(pPresetCat->displayNameMsg.hMessage));
					} else {
						ErrorFilenamef(pDef->pcFileName,"Species '%s' preset %d has no display name. (Must exist on preset or preset category)\n",pDef->pcName,i+1);
					}
				}
			}
			else
			{
				if (REF_STRING_FROM_HANDLE(pPreset->displayNameMsg.hMessage)) {
					ErrorFilenamef(pDef->pcFileName,"Species '%s' preset %d refers to non-existent message '%s'\n",pDef->pcName,i+1,REF_STRING_FROM_HANDLE(pPreset->displayNameMsg.hMessage));
				} else {
					ErrorFilenamef(pDef->pcFileName,"Species '%s' preset %d has no display name. (Must exist on preset or preset category)\n",pDef->pcName,i+1);
				}
			}
		}
		if (!GET_REF(pPreset->hCostume)) {
			if (REF_STRING_FROM_HANDLE(pPreset->hCostume)) {
				ErrorFilenamef(pDef->pcFileName,"Species '%s' preset refers to non-existent costume '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset->hCostume));
			} else {
				ErrorFilenamef(pDef->pcFileName,"Species '%s' preset %d has no costume\n",pDef->pcName,i+1);
			}
		} else {
			PlayerCostume *pCostume = GET_REF(pPreset->hCostume);
			char *estrReason=NULL;

			if (pCostume->eCostumeType != kPCCostumeType_Player) {
				ErrorFilenamef(pDef->pcFileName,"Species '%s' preset costume '%s' is not of 'Player' type.  Species presets must be player type (even if the species is not player legal).\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset->hCostume));
			}
			if (!costumeValidate_ValidatePlayerCreated(pCostume, pDef, NULL, NULL, NULL, &estrReason, pPreset->bOverrideValidateValues || !pPresetCat ? &pPreset->validatePlayerValues : &pPresetCat->validatePlayerValues, NULL, false)) {
				ErrorFilenamef(pDef->pcFileName,"Species '%s' preset costume '%s' is not player legal.  Species presets must be player legal (even if the species is not player legal).  Reason: %s\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset->hCostume), estrReason);
			}
			estrDestroy(&estrReason);
			
			if (!pCostume->bLoadedOnClient) {
				ErrorFilenamef(pDef->pcFileName,"Species '%s' preset costume '%s' is not marked as 'LoadedOnClient'.  Species preset costumes must be marked this way to save network bandwidth.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset->hCostume));
			}
		}

		if (pPreset->pcSlotType)
		{
			PCSlotType *temp = costumeLoad_GetSlotType(pPreset->pcSlotType);
			if (!temp) {
				ErrorFilenamef(pDef->pcFileName,"Species '%s' preset costume slot refers to non-existent slot type '%s'\n",pDef->pcName,pPreset->pcSlotType);
			}
		}
		else if (pPresetCat && pPresetCat->pcSlotType)
		{
			PCSlotType *temp = costumeLoad_GetSlotType(pPresetCat->pcSlotType);
			if (!temp) {
				ErrorFilenamef(pDef->pcFileName,"Species '%s' preset category costume slot refers to non-existent slot type '%s'\n",pDef->pcName,pPresetCat->pcSlotType);
			}
		}
	}

	for(i=eaSize(&pDef->eaCategories)-1; i>=0; --i)
	{
		CategoryLimits *pPreset = pDef->eaCategories[i];
		if (!GET_REF(pPreset->hCategory)) {
			if (REF_STRING_FROM_HANDLE(pPreset->hCategory)) {
				ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent category '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset->hCategory));
			}
		}
	}

	for(i=eaSize(&pDef->eaGeometries)-1; i>=0; --i)
	{
		GeometryLimits *pPreset = pDef->eaGeometries[i];
		if (!GET_REF(pPreset->hGeo)) {
			if (REF_STRING_FROM_HANDLE(pPreset->hGeo)) {
				ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent geometry '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset->hGeo));
			}
		}
		else
		{
			pBodyColor[0] = GET_REF(pPreset->hBodyColorSet0);
			if (!pBodyColor[0] && REF_STRING_FROM_HANDLE(pPreset->hBodyColorSet0)) {
				ErrorFilenamef(pDef->pcFileName,"Geometry '%s' in Species '%s' refers to non-existent color set '%s'\n",REF_STRING_FROM_HANDLE(pPreset->hGeo),pDef->pcName,REF_STRING_FROM_HANDLE(pPreset->hBodyColorSet0));
			}
			pBodyColor[1] = GET_REF(pPreset->hBodyColorSet1);
			if (!pBodyColor[1] && REF_STRING_FROM_HANDLE(pPreset->hBodyColorSet1)) {
				ErrorFilenamef(pDef->pcFileName,"Geometry '%s' in Species '%s' refers to non-existent color set '%s'\n",REF_STRING_FROM_HANDLE(pPreset->hGeo),pDef->pcName,REF_STRING_FROM_HANDLE(pPreset->hBodyColorSet1));
			}
			pBodyColor[2] = GET_REF(pPreset->hBodyColorSet2);
			if (!pBodyColor[2] && REF_STRING_FROM_HANDLE(pPreset->hBodyColorSet2)) {
				ErrorFilenamef(pDef->pcFileName,"Geometry '%s' in Species '%s' refers to non-existent color set '%s'\n",REF_STRING_FROM_HANDLE(pPreset->hGeo),pDef->pcName,REF_STRING_FROM_HANDLE(pPreset->hBodyColorSet2));
			}
			pBodyColor[3] = GET_REF(pPreset->hBodyColorSet3);
			if (!pBodyColor[3] && REF_STRING_FROM_HANDLE(pPreset->hBodyColorSet3)) {
				ErrorFilenamef(pDef->pcFileName,"Geometry '%s' in Species '%s' refers to non-existent color set '%s'\n",REF_STRING_FROM_HANDLE(pPreset->hGeo),pDef->pcName,REF_STRING_FROM_HANDLE(pPreset->hBodyColorSet3));
			}
			pQuadSet = GET_REF(pPreset->hColorQuadSet);
			if (!pQuadSet && REF_STRING_FROM_HANDLE(pPreset->hColorQuadSet)) {
				ErrorFilenamef(pDef->pcFileName,"Geometry '%s' refers to non-existent color quad set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset->hColorQuadSet));
			}
		}
		for(j=eaSize(&pPreset->eaMaterials)-1; j>=0; --j)
		{
			MaterialLimits *pPreset2 = pPreset->eaMaterials[j];
			if (!GET_REF(pPreset2->hMaterial)) {
				if (REF_STRING_FROM_HANDLE(pPreset2->hMaterial)) {
					ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent material '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset2->hMaterial));
				}
			}
			for(k=eaSize(&pPreset2->eaTextures)-1; k>=0; --k)
			{
				TextureLimits *pPreset3 = pPreset2->eaTextures[k];
				if (!GET_REF(pPreset3->hTexture)) {
					if (REF_STRING_FROM_HANDLE(pPreset3->hTexture)) {
						ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent texture '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset3->hTexture));
					}
				}
			}
		}
	}

	if (pDef->fMinHeight > pDef->fMaxHeight && !pDef->bNoHeightChange)
	{
		ErrorFilenamef(pDef->pcFileName,"Species '%s' has min height value greater than max height value\n",pDef->pcName);
	}

	if (pDef->fMinMuscle > pDef->fMaxMuscle && !pDef->bNoMuscle)
	{
		ErrorFilenamef(pDef->pcFileName,"Species '%s' has min muscle value greater than max muscle value\n",pDef->pcName);
	}

	if (!skel) {
		if (REF_STRING_FROM_HANDLE(pDef->hSkeleton)) {
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent skeleton '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hSkeleton));
		}
	}
	else
	{
		for(i=eaSize(&pDef->eaBodyScaleLimits)-1; i>=0; --i)
		{
			BodyScaleLimit *pPreset = pDef->eaBodyScaleLimits[i];
			if (!pDef->bIsGenSpecies)
			{
				for(j=eaSize(&skel->eaBodyScaleInfo)-1; j>=0; --j)
				{
					if (!strcmp(skel->eaBodyScaleInfo[j]->pcName, pPreset->pcName))
					{
						break;
					}
				}
				if (j<0)
				{
					ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to body scale name which is not in skeleton '%s'\n",pDef->pcName,pPreset->pcName);
				}
			}
			if (pPreset->fMin > pPreset->fMax)
			{
				ErrorFilenamef(pDef->pcFileName,"Species '%s' has body scale with min value greater than max value '%s'\n",pDef->pcName,pPreset->pcName);
			}
		}

		for(i=eaSize(&pDef->eaBoneScaleLimits)-1; i>=0; --i)
		{
			BoneScaleLimit *pPreset = pDef->eaBoneScaleLimits[i];
			if (!pDef->bIsGenSpecies)
			{
				for(j=eaSize(&skel->eaScaleInfo)-1; j>=0; --j)
				{
					if (!strcmp(skel->eaScaleInfo[j]->pcName, pPreset->pcName))
					{
						break;
					}
				}
				if (j<0)
				{
					for(k=eaSize(&skel->eaScaleInfoGroups)-1; k>=0; --k)
					{
						for(j=eaSize(&skel->eaScaleInfoGroups[k]->eaScaleInfo)-1; j>=0; --j)
						{
							if (!strcmp(skel->eaScaleInfoGroups[k]->eaScaleInfo[j]->pcName, pPreset->pcName))
							{
								break;
							}
						}
						if (j>=0) break;
					}
					if (k<0)
					{
						ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to bone scale name which is not in skeleton '%s'\n",pDef->pcName,pPreset->pcName);
					}
				}
			}
			if (pPreset->fMin > pPreset->fMax)
			{
				ErrorFilenamef(pDef->pcFileName,"Species '%s' has bone scale with min value greater than max value '%s'\n",pDef->pcName,pPreset->pcName);
			}
		}
	}

	pSkinColor = GET_REF(pDef->hSkinColorSet);
	if (!pSkinColor && REF_STRING_FROM_HANDLE(pDef->hSkinColorSet)) {
		ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hSkinColorSet));
	}
	pBodyColor[0] = GET_REF(pDef->hBodyColorSet);
	if (!pBodyColor[0] && REF_STRING_FROM_HANDLE(pDef->hBodyColorSet)) {
		ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hBodyColorSet));
	}
	pBodyColor[1] = GET_REF(pDef->hBodyColorSet1);
	if (!pBodyColor[1] && REF_STRING_FROM_HANDLE(pDef->hBodyColorSet1)) {
		ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hBodyColorSet1));
	}
	pBodyColor[2] = GET_REF(pDef->hBodyColorSet2);
	if (!pBodyColor[2] && REF_STRING_FROM_HANDLE(pDef->hBodyColorSet2)) {
		ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hBodyColorSet2));
	}
	pBodyColor[3] = GET_REF(pDef->hBodyColorSet3);
	if (!pBodyColor[3] && REF_STRING_FROM_HANDLE(pDef->hBodyColorSet3)) {
		ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent color set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hBodyColorSet3));
	}
	pQuadSet = GET_REF(pDef->hColorQuadSet);
	if (!pQuadSet && REF_STRING_FROM_HANDLE(pDef->hColorQuadSet)) {
		ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent color quad set '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hColorQuadSet));
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
				ErrorFilenamef(pDef->pcFileName,"Species '%s' has color quad set '%s' that doesn't match skin or body colors\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hColorQuadSet));
			}
		}
	}

	for (j = eaSize(&pDef->eaBoneData)-1; j >= 0; --j)
	{
		const char *temp;
		boneData = pDef->eaBoneData[j];
		temp = REF_STRING_FROM_HANDLE(boneData->hBone);
		if (!temp)
		{
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to bone data with undefined bone\n",pDef->pcName);
			continue;
		}
		if (!GET_REF(boneData->hBone))
		{
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to bone data that references non-existant bone '%s'\n",pDef->pcName,temp);
			continue;
		}
		pBodyColor[0] = GET_REF(boneData->hBodyColorSet0);
		if (!pBodyColor[0] && REF_STRING_FROM_HANDLE(boneData->hBodyColorSet0)) {
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent color set '%s' in bone data '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(boneData->hBodyColorSet0),temp);
		}
		pBodyColor[1] = GET_REF(boneData->hBodyColorSet1);
		if (!pBodyColor[1] && REF_STRING_FROM_HANDLE(boneData->hBodyColorSet1)) {
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent color set '%s' in bone data '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(boneData->hBodyColorSet1),temp);
		}
		pBodyColor[2] = GET_REF(boneData->hBodyColorSet2);
		if (!pBodyColor[2] && REF_STRING_FROM_HANDLE(boneData->hBodyColorSet2)) {
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent color set '%s' in bone data '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(boneData->hBodyColorSet2),temp);
		}
		pBodyColor[3] = GET_REF(boneData->hBodyColorSet3);
		if (!pBodyColor[3] && REF_STRING_FROM_HANDLE(boneData->hBodyColorSet3)) {
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent color set '%s' in bone data '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(boneData->hBodyColorSet3),temp);
		}
		pQuadSet = GET_REF(boneData->hColorQuadSet);
		if (!pQuadSet && REF_STRING_FROM_HANDLE(boneData->hColorQuadSet)) {
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent color quad set '%s' in bone data '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(boneData->hColorQuadSet),temp);
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
					ErrorFilenamef(pDef->pcFileName,"Species '%s' has color quad set '%s' that doesn't match skin or body colors\n",pDef->pcName,REF_STRING_FROM_HANDLE(boneData->hColorQuadSet));
				}
			}
		}
	}

	for(j=eaSize(&pDef->eaStanceInfo)-1; j>=0; --j) {
		PCStanceInfo *pStance = pDef->eaStanceInfo[j];
		if (!GET_REF(pStance->displayNameMsg.hMessage)) {
			ErrorFilenamef(pDef->pcFileName,"Species '%s' stance '%s' is missing its display name message\n",pDef->pcName,pStance->pcName);
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
			ErrorFilenamef(pDef->pcFileName,"Species '%s' has a default stance '%s' that is not defined\n",pDef->pcName,pDef->pcDefaultStance);
		}
	}

	for(j=eaSize(&pDef->eaNameTemplateLists)-1; j>=0; --j)
	{
		if (!GET_REF(pDef->eaNameTemplateLists[j]->hNameTemplateList)) {
			if (REF_STRING_FROM_HANDLE(pDef->eaNameTemplateLists[j]->hNameTemplateList)) {
				ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent NameTemplateList '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->eaNameTemplateLists[j]->hNameTemplateList));
			}
		}
	}

	for(j=eaSize(&pDef->eaAllowedVoices)-1; j>=0; --j)
	{
		if (!GET_REF(pDef->eaAllowedVoices[j]->hVoice)) {
			if (REF_STRING_FROM_HANDLE(pDef->eaAllowedVoices[j]->hVoice)) {
				ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent Voice '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->eaAllowedVoices[j]->hVoice));
			}
		}
	}
}

static void validateSpeciesFeature(SpeciesDefiningFeature *pDef)
{
	int i, j, k, l;
	PCSkeletonDef *skel = GET_REF(pDef->hSkeleton);

	if (!resIsValidName(pDef->pcName)) {
		ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' does not have a valid name\n",pDef->pcName);
	}

	if (pDef->eType == kSpeciesDefiningType_Invalid)
	{
		ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' does not have a valid type\n",pDef->pcName);
	}

	if (!GET_REF(pDef->hExcludeBone)) {
		if (REF_STRING_FROM_HANDLE(pDef->hExcludeBone)) {
			ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' refers to non-existent bone '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hExcludeBone));
		}
	}

	for(i=eaSize(&pDef->eaCategories)-1; i>=0; --i)
	{
		CategoryLimits *pPreset = pDef->eaCategories[i];
		if (!GET_REF(pPreset->hCategory)) {
			if (REF_STRING_FROM_HANDLE(pPreset->hCategory)) {
				ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' preset refers to non-existent category '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset->hCategory));
			}
		}
	}

	for(i=eaSize(&pDef->eaGeometries)-1; i>=0; --i)
	{
		GeometryLimits *pPreset = pDef->eaGeometries[i];
		PCGeometryDef *geo = GET_REF(pPreset->hGeo);
		if (!geo) {
			if (REF_STRING_FROM_HANDLE(pPreset->hGeo)) {
				ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' preset refers to non-existent geometry '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset->hGeo));
			}
		}
		//else if (!(geo->eRestriction & kPCRestriction_Player_Initial))
		//{
		//	ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' refers to geometry '%s' that does not have Restriction Player_Initial\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset->hGeo));
		//}

		if (skel && geo)
		{
			//Make sure geo attaches to a bone on the skeleton
			PCBoneDef *bone = GET_REF(geo->hBone);
			if (bone)
			{
				for (l = eaSize(&skel->eaRequiredBoneDefs)-1; l >= 0; --l)
				{
					if (bone == GET_REF(skel->eaRequiredBoneDefs[l]->hBone)) break;
				}
				if (l < 0)
				{
					for (l = eaSize(&skel->eaOptionalBoneDefs)-1; l >= 0; --l)
					{
						if (bone == GET_REF(skel->eaOptionalBoneDefs[l]->hBone)) break;
					}
					if (l < 0)
					{
						ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' refers to geometry '%s' that points to a bone that does not exist on the skeleton '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset->hGeo),skel->pcName);
					}
				}
			}
		}
		for(j=eaSize(&pPreset->eaMaterials)-1; j>=0; --j)
		{
			MaterialLimits *pPreset2 = pPreset->eaMaterials[j];
			PCMaterialDef *mat = GET_REF(pPreset2->hMaterial);
			if (!mat) {
				if (REF_STRING_FROM_HANDLE(pPreset2->hMaterial)) {
					ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' preset refers to non-existent material '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset2->hMaterial));
				}
			}
			//Make sure material is valid in geometry
			if (geo && mat)
			{
				for (l = eaSize(&geo->eaAllowedMaterialDefs)-1; l >= 0; --l)
				{
					if (!stricmp(geo->eaAllowedMaterialDefs[l],mat->pcName)) break;
				}
				if (l < 0)
				{
					ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' refers to material '%s' that does not belong in the geometry '%s' it is placed in.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset2->hMaterial),REF_STRING_FROM_HANDLE(pPreset->hGeo));
				}
			}
			for(k=eaSize(&pPreset2->eaTextures)-1; k>=0; --k)
			{
				TextureLimits *pPreset3 = pPreset2->eaTextures[k];
				PCTextureDef *tex = GET_REF(pPreset3->hTexture);
				if (!tex) {
					if (REF_STRING_FROM_HANDLE(pPreset3->hTexture)) {
						ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' preset refers to non-existent texture '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset3->hTexture));
					}
				}
				//Make sure texture is valid in material
				if (geo && mat && tex)
				{
					for (l = eaSize(&mat->eaAllowedTextureDefs)-1; l >= 0; --l)
					{
						if (!stricmp(mat->eaAllowedTextureDefs[l],tex->pcName)) break;
					}
					if (l < 0)
					{
						ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' refers to texture '%s' that does not belong in the material '%s' it is placed in.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset3->hTexture),REF_STRING_FROM_HANDLE(pPreset2->hMaterial));
					}
				}
				//Make sure texture matches texture type if one exists
				if (tex)
				{
					if (pDef->eType == kSpeciesDefiningType_Pattern && tex->eTypeFlags != kPCTextureType_Pattern)
					{
						ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' refers to texture '%s' which is a non-Pattern type, but the type of the SpeciesFeature is Pattern.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset3->hTexture));
					}
					if (pDef->eType == kSpeciesDefiningType_Detail && tex->eTypeFlags != kPCTextureType_Detail)
					{
						ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' refers to texture '%s' which is a non-Detail type, but the type of the SpeciesFeature is Detail.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset3->hTexture));
					}
					if (pDef->eType == kSpeciesDefiningType_Specular && tex->eTypeFlags != kPCTextureType_Specular)
					{
						ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' refers to texture '%s' which is a non-Specular type, but the type of the SpeciesFeature is Specular.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset3->hTexture));
					}
					if (pDef->eType == kSpeciesDefiningType_Diffuse && tex->eTypeFlags != kPCTextureType_Diffuse)
					{
						ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' refers to texture '%s' which is a non-Diffuse type, but the type of the SpeciesFeature is Diffuse.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset3->hTexture));
					}
					if (pDef->eType == kSpeciesDefiningType_Movable && tex->eTypeFlags != kPCTextureType_Movable)
					{
						ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' refers to texture '%s' which is a non-Movable type, but the type of the SpeciesFeature is Movable.\n",pDef->pcName,REF_STRING_FROM_HANDLE(pPreset3->hTexture));
					}
				}
			}
		}
	}

	if (pDef->fMinHeight > pDef->fMaxHeight)
	{
		ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' has min height value greater than max height value\n",pDef->pcName);
	}

	if (pDef->fMinMuscle > pDef->fMaxMuscle)
	{
		ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' has min muscle value greater than max muscle value\n",pDef->pcName);
	}

	if (!skel) {
		if (REF_STRING_FROM_HANDLE(pDef->hSkeleton)) {
			ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' refers to non-existent skeleton '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hSkeleton));
		}
	}
	else
	{
		for(i=eaSize(&pDef->eaBodyScaleLimits)-1; i>=0; --i)
		{
			BodyScaleLimit *pPreset = pDef->eaBodyScaleLimits[i];
			for(j=eaSize(&skel->eaBodyScaleInfo)-1; j>=0; --j)
			{
				if (!strcmp(skel->eaBodyScaleInfo[j]->pcName, pPreset->pcName))
				{
					break;
				}
			}
			if (j<0)
			{
				ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' refers to body scale name which is not in skeleton '%s'\n",pDef->pcName,pPreset->pcName);
			}
			if (pPreset->fMin > pPreset->fMax)
			{
				ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' has body scale with min value greater than max value '%s'\n",pDef->pcName,pPreset->pcName);
			}
		}

		for(i=eaSize(&pDef->eaBoneScaleLimits)-1; i>=0; --i)
		{
			BoneScaleLimit *pPreset = pDef->eaBoneScaleLimits[i];
			for(j=eaSize(&skel->eaScaleInfo)-1; j>=0; --j)
			{
				if (!strcmp(skel->eaScaleInfo[j]->pcName, pPreset->pcName))
				{
					break;
				}
			}
			if (j<0)
			{
				for(k=eaSize(&skel->eaScaleInfoGroups)-1; k>=0; --k)
				{
					for(j=eaSize(&skel->eaScaleInfoGroups[k]->eaScaleInfo)-1; j>=0; --j)
					{
						if (!strcmp(skel->eaScaleInfoGroups[k]->eaScaleInfo[j]->pcName, pPreset->pcName))
						{
							break;
						}
					}
					if (j>=0) break;
				}
				if (k<0)
				{
					ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' refers to bone scale name which is not in skeleton '%s'\n",pDef->pcName,pPreset->pcName);
				}
			}
			if (pPreset->fMin > pPreset->fMax)
			{
				ErrorFilenamef(pDef->pcFileName,"SpeciesFeature '%s' has bone scale with min value greater than max value '%s'\n",pDef->pcName,pPreset->pcName);
			}
		}
	}
}

static void validateSpeciesGenData(SpeciesGenData *pDef)
{
	PCSkeletonDef *maleskel = GET_REF(pDef->hMaleSkeleton);
	PCSkeletonDef *femaleskel = GET_REF(pDef->hFemaleSkeleton);
	SpeciesDefiningFeature *sdf;
	int i, j;

	if (!resIsValidName(pDef->pcName)) {
		ErrorFilenamef(pDef->pcFileName,"SpeciesGenData'%s' does not have a valid name\n",pDef->pcName);
	}

	if (!maleskel) {
		if (REF_STRING_FROM_HANDLE(pDef->hMaleSkeleton)) {
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent skeleton '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hMaleSkeleton));
		}
		else
		{
			ErrorFilenamef(pDef->pcFileName,"Species '%s' does not refer to a male skeleton\n",pDef->pcName);
		}
	}

	if (!femaleskel) {
		if (REF_STRING_FROM_HANDLE(pDef->hFemaleSkeleton)) {
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent skeleton '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hFemaleSkeleton));
		}
		else
		{
			ErrorFilenamef(pDef->pcFileName,"Species '%s' does not refer to a female skeleton\n",pDef->pcName);
		}
	}

	if (!GET_REF(pDef->hDefaultMale)) {
		if (REF_STRING_FROM_HANDLE(pDef->hDefaultMale)) {
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent SpeciesFeature '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultMale));
		}
		else
		{
			ErrorFilenamef(pDef->pcFileName,"Species '%s' does not refer to a default male SpeciesFeature\n",pDef->pcName);
		}
	}
	else
	{
		sdf = GET_REF(pDef->hDefaultMale);
		if (sdf->eType != kSpeciesDefiningType_Default)
		{
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to default SpeciesFeature '%s' that is not type Default\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultMale));
		}
	}

	if (!GET_REF(pDef->hDefaultFemale)) {
		if (REF_STRING_FROM_HANDLE(pDef->hDefaultFemale)) {
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent SpeciesFeature '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultFemale));
		}
		else
		{
			ErrorFilenamef(pDef->pcFileName,"Species '%s' does not refer to a default female SpeciesFeature\n",pDef->pcName);
		}
	}
	else
	{
		sdf = GET_REF(pDef->hDefaultFemale);
		if (sdf->eType != kSpeciesDefiningType_Default)
		{
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to default SpeciesFeature '%s' that is not type Default\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->hDefaultFemale));
		}
	}

	for (i = eaSize(&pDef->eaFeaturesToUse)-1; i >= 0; --i)
	{
		sdf = GET_REF(pDef->eaFeaturesToUse[i]->hSpeciesDefiningFeatureRef);
		if ((!sdf) && REF_STRING_FROM_HANDLE(pDef->eaFeaturesToUse[i]->hSpeciesDefiningFeatureRef)) {
			ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent SpeciesFeature '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->eaFeaturesToUse[i]->hSpeciesDefiningFeatureRef));
		}
	}

	for (i = eaSize(&pDef->eaUniformGroups)-1; i >= 0; --i)
	{
		for (j = eaSize(&pDef->eaUniformGroups[i]->eaUniforms)-1; j >= 0; --j)
		{
			PlayerCostume *pc = GET_REF(pDef->eaUniformGroups[i]->eaUniforms[j]->hPlayerCostume);
			if ((!pc) && REF_STRING_FROM_HANDLE(pDef->eaUniformGroups[i]->eaUniforms[j]->hPlayerCostume)) {
				ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent PlayerCostume '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->eaUniformGroups[i]->eaUniforms[j]->hPlayerCostume));
			}
		}
	}

	for (i = eaSize(&pDef->eaCritterGroupGen)-1; i >= 0; --i)
	{
		for (j = eaSize(&pDef->eaCritterGroupGen[i]->eaCritterDef)-1; j >= 0; --j)
		{
			CritterDef *cd = GET_REF(pDef->eaCritterGroupGen[i]->eaCritterDef[j]->hCritterDef);
			if ((!cd) && REF_STRING_FROM_HANDLE(pDef->eaCritterGroupGen[i]->eaCritterDef[j]->hCritterDef)) {
				ErrorFilenamef(pDef->pcFileName,"Species '%s' refers to non-existent CritterDef '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->eaCritterGroupGen[i]->eaCritterDef[j]->hCritterDef));
			}
		}
	}
}

static int presetCatResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, CostumePresetCategory *pPresetCat, U32 userID)
{
	switch(eType)
	{
		xcase RESVALIDATE_CHECK_REFERENCES:
	// Only validate on game server
	if (IsGameServerBasedType() && !isProductionMode()) {
		validatePresetCat(pPresetCat);
		return VALIDATE_HANDLED;
	}

	xcase RESVALIDATE_FIX_FILENAME:
	resFixPooledFilename((char**)&pPresetCat->pcFileName, "defs/costumes/presetcat", pPresetCat->pcScope, pPresetCat->pcName, "presetcat");
	return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

static int speciesResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, SpeciesDef *pSpecies, U32 userID)
{
	switch(eType)
	{
		xcase RESVALIDATE_CHECK_REFERENCES:
			// Only validate on game server
			if (IsGameServerBasedType() && !isProductionMode()) {
				validateSpecies(pSpecies);
				return VALIDATE_HANDLED;
			}

		xcase RESVALIDATE_FIX_FILENAME:
			resFixPooledFilename((char**)&pSpecies->pcFileName, "defs/species", pSpecies->pcScope, pSpecies->pcName, "species");
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

static int speciesFeatureResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, SpeciesDefiningFeature *pSpeciesFeature, U32 userID)
{
	switch(eType)
	{
		xcase RESVALIDATE_CHECK_REFERENCES:
			// Only validate on game server
			if (IsGameServerBasedType() && !isProductionMode()) {
				validateSpeciesFeature(pSpeciesFeature);
				return VALIDATE_HANDLED;
			}

		xcase RESVALIDATE_FIX_FILENAME:
			resFixPooledFilename((char**)&pSpeciesFeature->pcFileName, "defs/species/speciesfeature", pSpeciesFeature->pcScope, pSpeciesFeature->pcName, "speciesfeature");
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

static int speciesGenDataResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, SpeciesGenData *pSpeciesGenData, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_CHECK_REFERENCES:
		// Only validate on game server
		if (IsGameServerBasedType()) {
			validateSpeciesGenData(pSpeciesGenData);
			return VALIDATE_HANDLED;
		}

	xcase RESVALIDATE_FIX_FILENAME:
		resFixPooledFilename((char**)&pSpeciesGenData->pcFileName, "defs/species/speciesgendata", pSpeciesGenData->pcScope, pSpeciesGenData->pcName, "speciesgendata");
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

static void species_DefLoad(void)
{
	if (IsClient() || IsGameServerBasedType() || IsLoginServer() || IsAuctionServer())
	{
		resLoadResourcesFromDisk(g_hCostumePresetCatDict, "defs/costumes/presetcat", ".presetcat", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hSpeciesDict, "defs/species", ".species", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
	}
}

AUTO_STARTUP(AS_SpeciesLite);
void species_LiteLoad(void)
{
	species_DefLoad();
}

AUTO_STARTUP(Species) ASTRT_DEPS(AS_Messages, EntityCostumes, NameGen);
void species_Load(void)
{
	species_DefLoad();
#ifndef NO_EDITORS
	if (IsGameServerBasedType() || IsLoginServer() || IsAuctionServer())
	{
		resLoadResourcesFromDisk(g_hSpeciesDefiningDict, "defs/species/speciesfeature", ".speciesfeature", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
		resLoadResourcesFromDisk(g_hSpeciesGenDataDict, "defs/species/speciesgendata", ".speciesgendata", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);
	}
#endif
}


AUTO_RUN;
int RegisterSpeciesDict(void)
{
	// Set up reference dictionary for parts and such
	g_hCostumePresetCatDict = RefSystem_RegisterSelfDefiningDictionary("PresetCat", false, parse_CostumePresetCategory, true, true, NULL);
	g_hSpeciesDict = RefSystem_RegisterSelfDefiningDictionary("Species", false, parse_SpeciesDef, true, true, NULL);
#ifndef NO_EDITORS
	g_hSpeciesDefiningDict = RefSystem_RegisterSelfDefiningDictionary("SpeciesFeature", false, parse_SpeciesDefiningFeature, true, true, NULL);
	g_hSpeciesGenDataDict = RefSystem_RegisterSelfDefiningDictionary("SpeciesGenData", false, parse_SpeciesGenData, true, true, NULL);
#endif

	resDictManageValidation(g_hCostumePresetCatDict, presetCatResValidateCB);
	resDictManageValidation(g_hSpeciesDict, speciesResValidateCB);
#ifndef NO_EDITORS
	resDictManageValidation(g_hSpeciesDefiningDict, speciesFeatureResValidateCB);
	resDictManageValidation(g_hSpeciesGenDataDict, speciesGenDataResValidateCB);
#endif

	if (IsServer()) {
		resDictProvideMissingResources(g_hCostumePresetCatDict);
		resDictProvideMissingResources(g_hSpeciesDict);
#ifndef NO_EDITORS
		resDictProvideMissingResources(g_hSpeciesDefiningDict);
		resDictProvideMissingResources(g_hSpeciesGenDataDict);
		resDictProvideMissingRequiresEditMode(g_hSpeciesDefiningDict);
		resDictProvideMissingRequiresEditMode(g_hSpeciesGenDataDict);
#endif
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hCostumePresetCatDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hSpeciesDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
		}
	} else if (IsClient()) {
		resDictRequestMissingResources(g_hCostumePresetCatDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hSpeciesDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
#ifndef NO_EDITORS
		resDictRequestMissingResources(g_hSpeciesDefiningDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hSpeciesGenDataDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
#endif
	}

	return 1;
}

// Probably should make this a transaction helper
static void species_GetAvailableSpeciesList(GameAccountData *pGameAccountData, AllegianceDef *pAllegiance, AllegianceDef *pSubAllegiance, CharacterClass *pCharClass, const char *pcSpeciesName, PCSkeletonDef *pSkeleton, SpeciesDef ***peaSpecies)
{
	DictionaryEArrayStruct *pArray;
	SpeciesDef *pSpecies;
	int i, j;

	if (!peaSpecies) {
		return;
	}

	eaClear(peaSpecies);

	// Restrict by character class
	if (pCharClass && eaSize(&pCharClass->eaPermittedSpecies)) {
		// Restrict character class by allegiance
		if (pAllegiance && eaSize(&pAllegiance->eaClassesAllowed)) {
			for (i=eaSize(&pAllegiance->eaClassesAllowed)-1; i>=0; i--) {
				if (GET_REF(pAllegiance->eaClassesAllowed[i]->hClass) == pCharClass) {
					break;
				}
			}
			if (i < 0) {
				if (pSubAllegiance && eaSize(&pSubAllegiance->eaClassesAllowed)) {
					for (i=eaSize(&pSubAllegiance->eaClassesAllowed)-1; i>=0; i--) {
						if (GET_REF(pSubAllegiance->eaClassesAllowed[i]->hClass) == pCharClass) {
							break;
						}
					}
				}
				if (i < 0) {
					// If the chosen class isn't available in the Allegiance,
					// then the restrictions it imposes on the permitted species
					// will reject all the species.
					return;
				}
			}
		}

		// Scan the list of permitted species
		for (i=eaSize(&pCharClass->eaPermittedSpecies)-1; i>=0; --i) {
			pSpecies = GET_REF(pCharClass->eaPermittedSpecies[i]->hSpecies);
			if (!pSpecies) {
				continue;
			}

			// Not in allegiance
			if (pAllegiance && eaSize(&pAllegiance->eaStartSpecies)) {
				for (j=eaSize(&pAllegiance->eaStartSpecies)-1; j>=0; --j) {
					if (GET_REF(pAllegiance->eaStartSpecies[j]->hSpecies) == pSpecies) {
						break;
					}
				}
				if (j < 0) {
					if (pSubAllegiance && eaSize(&pSubAllegiance->eaStartSpecies)) {
						for (j=eaSize(&pSubAllegiance->eaStartSpecies)-1; j>=0; j--) {
							if (GET_REF(pSubAllegiance->eaStartSpecies[j]->hSpecies) == pSpecies) {
								break;
							}
						}
					}
					if (j < 0) {
						continue;
					}
				}
			}

			// Not available to the player
			if (!(pSpecies->eRestriction & (kPCRestriction_Player_Initial | kPCRestriction_Player))) {
				continue;
			}

			// Not unlocked by the player
			if ((pSpecies->eRestriction & kPCRestriction_Player) && !(pSpecies->eRestriction & kPCRestriction_Player_Initial)) {
				if (!pGameAccountData) {
					continue;
				}
				if (gad_GetAttribInt(pGameAccountData, pSpecies->pcUnlockCode) <= 0) {
					continue;
				}
			}

			// Too soon
			if( (pSpecies->uUnlockTimestamp != 0) && (pSpecies->uUnlockTimestamp > timeServerSecondsSince2000()) )
				continue;

			// Filtered out by parameters
			if (pSkeleton && GET_REF(pSpecies->hSkeleton) != pSkeleton) {
				continue;
			}
			if (stricmp_safe(pSpecies->pcSpeciesName, pcSpeciesName) != 0) {
				continue;
			}

			eaPush(peaSpecies, pSpecies);
		}

		// Sort the species
		costumeTailor_SortSpecies(*peaSpecies, true);
		return;
	}

	pArray = resDictGetEArrayStruct("SpeciesDef");
	for (i=eaSize(&pArray->ppReferents)-1; i>=0; --i) {
		pSpecies = pArray->ppReferents[i];
		if (!pSpecies) {
			continue;
		}

		// Not in allegiance
		if (pAllegiance && eaSize(&pAllegiance->eaStartSpecies)) {
			for (j=eaSize(&pAllegiance->eaStartSpecies)-1; j>=0; --j) {
				if (GET_REF(pAllegiance->eaStartSpecies[j]->hSpecies) == pSpecies) {
					break;
				}
			}
			if (j < 0) {
				if (pSubAllegiance && eaSize(&pSubAllegiance->eaStartSpecies)) {
					for (j=eaSize(&pSubAllegiance->eaStartSpecies)-1; j>=0; j--) {
						if (GET_REF(pSubAllegiance->eaStartSpecies[j]->hSpecies) == pSpecies) {
							break;
						}
					}
				}
				if (j < 0) {
					continue;
				}
			}
		}

		// Not available to the player
		if (!(pSpecies->eRestriction & (kPCRestriction_Player_Initial | kPCRestriction_Player))) {
			continue;
		}

		// Not unlocked by the player
		if ((pSpecies->eRestriction & kPCRestriction_Player) && !(pSpecies->eRestriction & kPCRestriction_Player_Initial)) {
			if (!pGameAccountData) {
				continue;
			}
			if (gad_GetAttribInt(pGameAccountData, pSpecies->pcUnlockCode) <= 0) {
				continue;
			}
		}

		// Filtered out by parameters
		if (pSkeleton && GET_REF(pSpecies->hSkeleton) != pSkeleton) {
			continue;
		}
		if (stricmp_safe(pSpecies->pcSpeciesName, pcSpeciesName) != 0) {
			continue;
		}

		eaPush(peaSpecies, pSpecies);
	}

	// Sort the species
	costumeTailor_SortSpecies(*peaSpecies, true);
}

// Probably should make this a transaction helper
void species_GetAvailableSpecies(GameAccountData *pGameAccountData, SpeciesDef ***peaSpecies)
{
	species_GetAvailableSpeciesList(pGameAccountData, NULL, NULL, NULL, NULL, NULL, peaSpecies);
}

// Probably should make this a transaction helper
void species_GetSpeciesList(Entity *pEnt, const char *pcSpeciesName, PCSkeletonDef *pSkeleton, SpeciesDef ***peaSpecies)
{
	GameAccountData *pGameAccountData = entity_GetGameAccount(pEnt);
	CharacterClass *pCharClass = NULL;
	AllegianceDef *pAllegiance = NULL;
	AllegianceDef *pSubAllegiance = NULL;

	if (pEnt) {
		if (pEnt->pChar) {
			pCharClass = GET_REF(pEnt->pChar->hClass);
		}
		pAllegiance = GET_REF(pEnt->hAllegiance);
		pSubAllegiance = GET_REF(pEnt->hSubAllegiance);
	}

	species_GetAvailableSpeciesList(pGameAccountData, pAllegiance, pSubAllegiance, pCharClass, pcSpeciesName, pSkeleton, peaSpecies);
}

// Probably should make this a transaction helper
void species_EntGetAvailableSpecies(Entity *pEnt, SpeciesDef ***peaSpecies)
{
	GameAccountData *pGameAccountData = entity_GetGameAccount(pEnt);
	CharacterClass *pCharClass = NULL;
	AllegianceDef *pAllegiance = NULL;
	AllegianceDef *pSubAllegiance = NULL;

	if (pEnt) {
		if (pEnt->pChar) {
			pCharClass = GET_REF(pEnt->pChar->hClass);
		}
		pAllegiance = GET_REF(pEnt->hAllegiance);
		pSubAllegiance = GET_REF(pEnt->hSubAllegiance);
	}

	species_GetAvailableSpeciesList(pGameAccountData, pAllegiance, pSubAllegiance, pCharClass, NULL, NULL, peaSpecies);
}

AUTO_EXPR_FUNC(Player) ACMD_NAME(PlayerGetSpeciesName);
const char* exprPlayerGetSpeciesName(ExprContext *pContext)
{
	Entity* pEnt = exprContextGetVarPointerUnsafe(pContext, "Player");
	Character *pChar = pEnt ? pEnt->pChar : NULL;
	SpeciesDef *pSpeciesDef = pChar ? GET_REF( pChar->hSpecies ) : NULL;
	return pSpeciesDef ? pSpeciesDef->pcName : "";
}

#include "AutoGen/species_common_h_ast.c"
