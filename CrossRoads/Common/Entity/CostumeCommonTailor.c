/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#include "Character.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "EString.h"
#include "GameAccountDataCommon.h"
#include "GfxMaterials.h"
#include "Guild.h"
#include "microtransactions_common.h"
#include "Player.h"
#include "PowerVars.h"
#include "ResourceInfo.h"
#include "rewardCommon.h"
#include "rgb_hsv.h"
#include "species_common.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "timing.h"
#include "windefinclude.h"
#include "wlGroupPropertyStructs.h"

#ifdef GAMECLIENT
#include "GlobalStateMachine.h"
#include "gclBaseStates.h"
#endif

#include "AutoGen/wlCostume_h_ast.h"
#include "AutoGen/CostumeCommonEnums_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/EntitySavedData_h_ast.h"

#define COSTUME_GLOW_ETC_COUNT 4	// used by glow, reflection, spec

extern DictionaryHandle g_hItemDict;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

PCColorQuad g_StaticQuad = { {10, 40, 90, 255}, {10, 10, 10, 255}, {240, 240, 240, 255}, {220, 0, 0, 255}, 0 };

void costumeTailor_PartFillClothLayer(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll);
void costumeTailor_FillMatForPart(NOCONST(PCPart) *pNewPart, NOCONST(PlayerCostume) *pPCCostume, PCMaterialDef *pMatDef, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, bool bSortDisplay, bool bUnlockAll);
bool costumeValidate_PlayerCreatedError(PlayerCostume *pPCCostume, char **ppEStringError, char *pError, PCPart *pPart);

// --------------------------------------------------------------------------


bool costumeTailor_PartHasNoColor(NOCONST(PCPart) *pPart)
{
	return  (pPart->color0[0] == 0) && (pPart->color0[1] == 0) && (pPart->color0[2] == 0) && (pPart->color0[3] == 0) &&
			(pPart->color1[0] == 0) && (pPart->color1[1] == 0) && (pPart->color1[2] == 0) && (pPart->color1[3] == 0) &&
			(pPart->color2[0] == 0) && (pPart->color2[1] == 0) && (pPart->color2[2] == 0) && (pPart->color2[3] == 0) &&
			(pPart->color3[0] == 0) && (pPart->color3[1] == 0) && (pPart->color3[2] == 0) && (pPart->color3[3] == 0);
}

int costumeTailor_IsMirroringColorsAllowed(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, NOCONST(PCPart) *pPart1, NOCONST(PCPart) *pPart2, bool bCheckColor3)
{
	UIColorSet *cs1[4];
	UIColorSet *cs2[4];
	int validColors = 0;

	if (!pPart1) return 0;
	if (!pPart2) return 0;

	cs1[0] = costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPart1, 0);
	cs1[1] = costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPart1, 1);
	cs1[2] = costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPart1, 2);
	cs1[3] = costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPart1, 3);
	if (!cs1[0] || !cs1[1] || !cs1[2] || !cs1[3]) return 0;

	cs2[0] = costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPart2, 0);
	cs2[1] = costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPart2, 1);
	cs2[2] = costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPart2, 2);
	cs2[3] = costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPart2, 3);
	if (!cs2[0] || !cs2[1] || !cs2[2] || !cs2[3]) return 0;

	if (!stricmp(cs1[0]->pcName,cs2[0]->pcName))
	{
		validColors |= kPCColorFlags_Color0;
	}
	if (!stricmp(cs1[1]->pcName,cs2[1]->pcName))
	{
		validColors |= kPCColorFlags_Color1;
	}
	if (!stricmp(cs1[2]->pcName,cs2[2]->pcName))
	{
		validColors |= kPCColorFlags_Color2;
	}
	if (bCheckColor3)
	{
		if (!stricmp(cs1[3]->pcName,cs2[3]->pcName))
		{
			validColors |= kPCColorFlags_Color3;
		}
	}

	return validColors;
}

//Returns flags for valid colors
int costumeTailor_IsColorValidForPart(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, NOCONST(PCPart) *pPart, const U8 color0[4], const U8 color1[4], const U8 color2[4], const U8 color3[4], bool bCheckColor3)
{
	UIColorSet *cs[4];
	int validColors = 0;

	if (!pPart) return 0;
	if (!color0) return 0;
	if (!color1) return 0;
	if (!color2) return 0;
	if (!color3) return 0;

	if (!IS_PLAYER_COSTUME(pPCCostume)) {
		// All colors are valid unless it's a player costume
		return 0x0f;
	}

	cs[0] = costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPart, 0);
	cs[1] = costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPart, 1);
	cs[2] = costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPart, 2);
	cs[3] = costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPart, 3);
	if (!cs[0]) return 0;

	if (costumeLoad_ColorInSet(color0, cs[0]))
	{
		validColors |= kPCColorFlags_Color0;
	}
	if (costumeLoad_ColorInSet(color1, cs[1]))
	{
		validColors |= kPCColorFlags_Color1;
	}
	if (costumeLoad_ColorInSet(color2, cs[2]))
	{
		validColors |= kPCColorFlags_Color2;
	}
	if (bCheckColor3)
	{
		if (costumeLoad_ColorInSet(color3, cs[3]))
		{
			validColors |= kPCColorFlags_Color3;
		}
	}

	return validColors;
}


void costumeTailor_FindClosestColor(const U8 colorSrc[4], const UIColorSet *colorSet, U8 colorDst[4])
{
	int i, m = 0;
	float minDist = -1.0f;
	float dist;
	float w, x, y, z;

	if ((!colorSrc) || (!colorDst) || (!colorSet)) return;
	if ((!colorSet) || !eaSize(&colorSet->eaColors))
	{
		if (colorDst != colorSrc)
		{
			colorDst[0] = colorSrc[0];
			colorDst[1] = colorSrc[1];
			colorDst[2] = colorSrc[2];
			colorDst[3] = colorSrc[3];
		}
		return;
	}

	for (i = eaSize(&colorSet->eaColors)-1; i >= 0; --i)
	{
		w = colorSet->eaColors[i]->color[0] - colorSrc[0];
		x = colorSet->eaColors[i]->color[1] - colorSrc[1];
		y = colorSet->eaColors[i]->color[2] - colorSrc[2];
		z = colorSet->eaColors[i]->color[3] - colorSrc[3];
		dist = w*w + x*x + y*y + z*z;
		if (minDist == -1 || dist < minDist)
		{
			minDist = dist;
			m = i;
		}
	}

	colorDst[0] = colorSet->eaColors[m]->color[0];
	colorDst[1] = colorSet->eaColors[m]->color[1];
	colorDst[2] = colorSet->eaColors[m]->color[2];
	colorDst[3] = colorSet->eaColors[m]->color[3];
}


bool costumeTailor_PartCopyColors(NOCONST(PCPart) *pSrcPart, NOCONST(PCPart) *pDestPart, int iCopyColorFlags)
{
	bool bChanged = false;

	if ((iCopyColorFlags & kPCColorFlags_Color0) && !IS_SAME_COSTUME_COLOR(pSrcPart->color0, pDestPart->color0)) {
		COPY_COSTUME_COLOR(pSrcPart->color0, pDestPart->color0);
		bChanged = true;
	}
	if ((iCopyColorFlags & kPCColorFlags_Color1) && !IS_SAME_COSTUME_COLOR(pSrcPart->color1, pDestPart->color1)) {
		COPY_COSTUME_COLOR(pSrcPart->color1, pDestPart->color1);
		bChanged = true;
	} 
	if ((iCopyColorFlags & kPCColorFlags_Color2) && !IS_SAME_COSTUME_COLOR(pSrcPart->color2, pDestPart->color2)) {
		COPY_COSTUME_COLOR(pSrcPart->color2, pDestPart->color2);
		bChanged = true;
	}
	if ((iCopyColorFlags & kPCColorFlags_Color3) && !IS_SAME_COSTUME_COLOR(pSrcPart->color3, pDestPart->color3)) {
		COPY_COSTUME_COLOR(pSrcPart->color3, pDestPart->color3);
		bChanged = true;
	}

	if (pSrcPart->pCustomColors) {
		if (!pDestPart->pCustomColors) {
			pDestPart->pCustomColors = StructCreateNoConst(parse_PCCustomColorInfo);
		}
		if ((iCopyColorFlags & kPCColorFlags_Color0) && (pDestPart->pCustomColors->glowScale[0] != pSrcPart->pCustomColors->glowScale[0])) {
			pDestPart->pCustomColors->glowScale[0] = pSrcPart->pCustomColors->glowScale[0];
			bChanged = true;
		}
		if ((iCopyColorFlags & kPCColorFlags_Color1) && (pDestPart->pCustomColors->glowScale[1] != pSrcPart->pCustomColors->glowScale[1])) {
			pDestPart->pCustomColors->glowScale[1] = pSrcPart->pCustomColors->glowScale[1];
			bChanged = true;
		}
		if ((iCopyColorFlags & kPCColorFlags_Color2) && (pDestPart->pCustomColors->glowScale[2] != pSrcPart->pCustomColors->glowScale[2])) {
			pDestPart->pCustomColors->glowScale[2] = pSrcPart->pCustomColors->glowScale[2];
			bChanged = true;
		}
		if ((iCopyColorFlags & kPCColorFlags_Color3) && (pDestPart->pCustomColors->glowScale[3] != pSrcPart->pCustomColors->glowScale[3])) {
			pDestPart->pCustomColors->glowScale[3] = pSrcPart->pCustomColors->glowScale[3];
			bChanged = true;
		}
	}

	return bChanged;
}

PCColorQuadSet *costumeTailor_GetColorQuadSetForPart(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, NOCONST(PCPart) *pPart)
{
	int i;
	PCSkeletonDef *skel = pPCCostume ? GET_REF(pPCCostume->hSkeleton) : NULL;
	PCBoneDef *bone = pPart ? GET_REF(pPart->hBoneDef) : NULL;
	PCGeometryDef *geo = pPart ? GET_REF(pPart->hGeoDef) : NULL;
	PCColorQuadSet *temp = NULL;

	if (pSpecies && geo)
	{
		for (i = eaSize(&pSpecies->eaGeometries)-1; i >= 0; --i)
		{
			if (GET_REF(pSpecies->eaGeometries[i]->hGeo) == geo)
			{
				break;
			}
		}
		if (i >= 0)
		{
			temp = GET_REF(pSpecies->eaGeometries[i]->hColorQuadSet);
			if (temp) return temp;
		}
	}

	if (geo && geo->pOptions)
	{
		temp = GET_REF(geo->pOptions->hColorQuadSet);
		if (temp) return temp;
	}

	if (pSpecies && bone)
	{
		for (i = eaSize(&pSpecies->eaBoneData)-1; i >= 0; --i)
		{
			if (GET_REF(pSpecies->eaBoneData[i]->hBone) == bone)
			{
				break;
			}
		}
		if (i >= 0)
		{
			temp = GET_REF(pSpecies->eaBoneData[i]->hColorQuadSet);
			if (temp) return temp;
		}
	}

	if (bone)
	{
		temp = GET_REF(bone->hColorQuadSet);
		if (temp) return temp;
	}

	if (pSpecies)
	{
		temp = GET_REF(pSpecies->hColorQuadSet);
		if (temp) return temp;
	}

	if (skel)
	{
		temp = GET_REF(skel->hColorQuadSet);
		if (temp) return temp;
	}

	temp = RefSystem_ReferentFromString(g_hCostumeColorQuadsDict, "Core_Body");
	if (temp) return temp;

	return NULL;
}

UIColorSet *costumeTailor_GetColorSetForPart(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, NOCONST(PCPart) *pPart, int colorNum)
{
	int i;
	PCSkeletonDef *skel = pPCCostume ? GET_REF(pPCCostume->hSkeleton) : NULL;
	PCBoneDef *bone = pPart ? GET_REF(pPart->hBoneDef) : NULL;
	PCGeometryDef *geo = pPart ? GET_REF(pPart->hGeoDef) : NULL;
	PCMaterialDef *mat = pPart ? GET_REF(pPart->hMatDef) : NULL;
	UIColorSet *temp = NULL;

	if (pSpecies && geo)
	{
		for (i = eaSize(&pSpecies->eaGeometries)-1; i >= 0; --i)
		{
			if (GET_REF(pSpecies->eaGeometries[i]->hGeo) == geo)
			{
				break;
			}
		}
		if (i >= 0)
		{
			if (colorNum == 0) temp = GET_REF(pSpecies->eaGeometries[i]->hBodyColorSet0);
			else if (colorNum == 1) temp = GET_REF(pSpecies->eaGeometries[i]->hBodyColorSet1);
			else if (colorNum == 2) temp = GET_REF(pSpecies->eaGeometries[i]->hBodyColorSet2);
			else if (colorNum == 3) temp = GET_REF(pSpecies->eaGeometries[i]->hBodyColorSet3);
			if (temp) return temp;
		}
	}

	if (geo && geo->pOptions)
	{
		if (colorNum == 0) temp = GET_REF(geo->pOptions->hBodyColorSet0);
		else if (colorNum == 1) temp = GET_REF(geo->pOptions->hBodyColorSet1);
		else if (colorNum == 2) temp = GET_REF(geo->pOptions->hBodyColorSet2);
		else if (colorNum == 3) temp = GET_REF(geo->pOptions->hBodyColorSet3);
		if (temp) return temp;
	}

	if (pSpecies && bone)
	{
		for (i = eaSize(&pSpecies->eaBoneData)-1; i >= 0; --i)
		{
			if (GET_REF(pSpecies->eaBoneData[i]->hBone) == bone)
			{
				break;
			}
		}
		if (i >= 0)
		{
			if (colorNum == 0) temp = GET_REF(pSpecies->eaBoneData[i]->hBodyColorSet0);
			else if (colorNum == 1) temp = GET_REF(pSpecies->eaBoneData[i]->hBodyColorSet1);
			else if (colorNum == 2) temp = GET_REF(pSpecies->eaBoneData[i]->hBodyColorSet2);
			else if (colorNum == 3) temp = GET_REF(pSpecies->eaBoneData[i]->hBodyColorSet3);
			if (temp) return temp;
		}
	}

	if (bone)
	{
		if (colorNum == 0) temp = GET_REF(bone->hBodyColorSet0);
		else if (colorNum == 1) temp = GET_REF(bone->hBodyColorSet1);
		else if (colorNum == 2) temp = GET_REF(bone->hBodyColorSet2);
		else if (colorNum == 3) temp = GET_REF(bone->hBodyColorSet3);
		if (temp) return temp;
	}

	if (pSlotType && pSlotType->bUseCostumeSlotOverride)
	{
		if (colorNum == 0) temp = GET_REF(pSlotType->hBodyColorSet);
		else if (colorNum == 1) temp = GET_REF(pSlotType->hBodyColorSet1);
		else if (colorNum == 2) temp = GET_REF(pSlotType->hBodyColorSet2);
		else if (colorNum == 3) temp = GET_REF(pSlotType->hBodyColorSet3);
		if (temp) return temp;
	}

	if (pSpecies)
	{
		if (colorNum == 0) temp = GET_REF(pSpecies->hBodyColorSet);
		else if (colorNum == 1) temp = GET_REF(pSpecies->hBodyColorSet1);
		else if (colorNum == 2) temp = GET_REF(pSpecies->hBodyColorSet2);
		else if (colorNum == 3) temp = GET_REF(pSpecies->hBodyColorSet3);
		if (temp) return temp;
	}

	if (skel)
	{
		if (colorNum == 0) temp = GET_REF(skel->hBodyColorSet);
		else if (colorNum == 1) temp = GET_REF(skel->hBodyColorSet1);
		else if (colorNum == 2) temp = GET_REF(skel->hBodyColorSet2);
		else if (colorNum == 3) temp = GET_REF(skel->hBodyColorSet3);
		if (temp) return temp;
	}

	if (colorNum == 1) return costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPart, 0);
	else if (colorNum == 2) return costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPart, 1);
	else if (colorNum == 3) return costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPart, 2);

	return NULL;
}

UIColorSet *costumeTailor_GetColorSetForPartConsideringSkin(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, NOCONST(PCPart) *pPart, int colorNum)
{
	if (!pPart) {
		return NULL;
	}

	// Skin only appears in color 3
	if (colorNum == 3) {
		PCMaterialDef *pMatDef;

		// And only if the material can have skin
		pMatDef = GET_REF(pPart->hMatDef);
		if (pMatDef && pMatDef->bHasSkin) {
			// Then check both the species and slot for an override
			UIColorSet *pColorSet = costumeTailor_GetOverrideSkinColorSet(pSpecies, pSlotType);
			if (!pColorSet) {
				// Else use the skeleton
				PCSkeletonDef *pSkel = GET_REF(pPCCostume->hSkeleton);
				if (pSkel) {
					pColorSet = GET_REF(pSkel->hSkinColorSet);
				}
			}
			if (pColorSet) {
				return pColorSet;
			}
			// If no skin defined, fall through to normal check
		}
	} 

	return costumeTailor_GetColorSetForPart(pPCCostume, pSpecies, pSlotType, pPart, colorNum);
}

void costumeTailor_GetTextureValueMinMax(PCPart *pPCPart, PCTextureDef *pTexture, SpeciesDef *pSpecies, float *pfMin, float *pfMax)
{
	if (pfMin) *pfMin = 0;
	if (pfMax) *pfMax = 0;

	if (!pfMin) return;
	if (!pfMax) return;
	if (!pPCPart) return;
	if (!pTexture) return;

	if (pSpecies)
	{
		int i, j, k;
		for (i = eaSize(&pSpecies->eaGeometries)-1; i >= 0; --i)
		{
			GeometryLimits *gl = pSpecies->eaGeometries[i];
			if (GET_REF(gl->hGeo) != GET_REF(pPCPart->hGeoDef)) continue;
			for (j = eaSize(&gl->eaMaterials)-1; j >= 0; --j)
			{
				MaterialLimits *ml = gl->eaMaterials[j];
				if (GET_REF(ml->hMaterial) != GET_REF(pPCPart->hMatDef)) continue;
				for (k = eaSize(&ml->eaTextures)-1; k >= 0; --k)
				{
					TextureLimits *tl = ml->eaTextures[k];
					if (GET_REF(tl->hTexture) != pTexture) continue;
					if (tl->bOverrideConstValues)
					{
						*pfMin = tl->fValueMin;
						*pfMax = tl->fValueMax;
					}
					else if (pTexture->pValueOptions)
					{
						*pfMin = pTexture->pValueOptions->fValueMin;
						*pfMax = pTexture->pValueOptions->fValueMax;
					}
					return;
				}
			}
		}
	}

	if (pTexture->pValueOptions)
	{
		*pfMin = pTexture->pValueOptions->fValueMin;
		*pfMax = pTexture->pValueOptions->fValueMax;
	}
}

void costumeTailor_GetTextureMovableValues(PCPart *pPCPart, PCTextureDef *pTexture, SpeciesDef *pSpecies,
									float *pfMovableMinX, float *pfMovableMaxX, float *pfMovableMinY, float *pfMovableMaxY,
									float *pfMovableMinScaleX, float *pfMovableMaxScaleX, float *pfMovableMinScaleY, float *pfMovableMaxScaleY,
									bool *pbMovableCanEditPosition, bool *pbMovableCanEditRotation, bool *pbMovableCanEditScale)
{
	if (pfMovableMinX) *pfMovableMinX = 0;
	if (pfMovableMaxX) *pfMovableMaxX = 0;
	if (pfMovableMinY) *pfMovableMinY = 0;
	if (pfMovableMaxY) *pfMovableMaxY = 0;
	if (pfMovableMinScaleX) *pfMovableMinScaleX = 0;
	if (pfMovableMaxScaleX) *pfMovableMaxScaleX = 0;
	if (pfMovableMinScaleY) *pfMovableMinScaleY = 0;
	if (pfMovableMaxScaleY) *pfMovableMaxScaleY = 0;
	if (pbMovableCanEditPosition) *pbMovableCanEditPosition = 0;
	if (pbMovableCanEditRotation) *pbMovableCanEditRotation = 0;
	if (pbMovableCanEditScale) *pbMovableCanEditScale = 0;

	if (!pfMovableMinX) return;
	if (!pfMovableMaxX) return;
	if (!pfMovableMinY) return;
	if (!pfMovableMaxY) return;
	if (!pfMovableMinScaleX) return;
	if (!pfMovableMaxScaleX) return;
	if (!pfMovableMinScaleY) return;
	if (!pfMovableMaxScaleY) return;
	if (!pbMovableCanEditPosition) return;
	if (!pbMovableCanEditRotation) return;
	if (!pbMovableCanEditScale) return;
	if (!pPCPart) return;
	if (!pTexture) return;

	if (pSpecies)
	{
		int i, j, k;
		for (i = eaSize(&pSpecies->eaGeometries)-1; i >= 0; --i)
		{
			GeometryLimits *gl = pSpecies->eaGeometries[i];
			if (GET_REF(gl->hGeo) != GET_REF(pPCPart->hGeoDef)) continue;
			for (j = eaSize(&gl->eaMaterials)-1; j >= 0; --j)
			{
				MaterialLimits *ml = gl->eaMaterials[j];
				if (GET_REF(ml->hMaterial) != GET_REF(pPCPart->hMatDef)) continue;
				for (k = eaSize(&ml->eaTextures)-1; k >= 0; --k)
				{
					TextureLimits *tl = ml->eaTextures[k];
					if (GET_REF(tl->hTexture) != pTexture) continue;
					if (tl->bOverrideMovableValues)
					{
						*pfMovableMinX = tl->fMovableMinX;
						*pfMovableMaxX = tl->fMovableMaxX;
						*pfMovableMinY = tl->fMovableMinY;
						*pfMovableMaxY = tl->fMovableMaxY;
						*pfMovableMinScaleX = tl->fMovableMinScaleX;
						*pfMovableMaxScaleX = tl->fMovableMaxScaleX;
						*pfMovableMinScaleY = tl->fMovableMinScaleY;
						*pfMovableMaxScaleY = tl->fMovableMaxScaleY;
						*pbMovableCanEditPosition = tl->bMovableCanEditPosition;
						*pbMovableCanEditRotation = tl->bMovableCanEditRotation;
						*pbMovableCanEditScale = tl->bMovableCanEditScale;
					}
					else if (pTexture->pMovableOptions)
					{
						*pfMovableMinX = pTexture->pMovableOptions->fMovableMinX;
						*pfMovableMaxX = pTexture->pMovableOptions->fMovableMaxX;
						*pfMovableMinY = pTexture->pMovableOptions->fMovableMinY;
						*pfMovableMaxY = pTexture->pMovableOptions->fMovableMaxY;
						*pfMovableMinScaleX = pTexture->pMovableOptions->fMovableMinScaleX;
						*pfMovableMaxScaleX = pTexture->pMovableOptions->fMovableMaxScaleX;
						*pfMovableMinScaleY = pTexture->pMovableOptions->fMovableMinScaleY;
						*pfMovableMaxScaleY = pTexture->pMovableOptions->fMovableMaxScaleY;
						*pbMovableCanEditPosition = pTexture->pMovableOptions->bMovableCanEditPosition;
						*pbMovableCanEditRotation = pTexture->pMovableOptions->bMovableCanEditRotation;
						*pbMovableCanEditScale = pTexture->pMovableOptions->bMovableCanEditScale;
					}
					return;
				}
			}
		}
	}

	if (pTexture->pMovableOptions)
	{
		*pfMovableMinX = pTexture->pMovableOptions->fMovableMinX;
		*pfMovableMaxX = pTexture->pMovableOptions->fMovableMaxX;
		*pfMovableMinY = pTexture->pMovableOptions->fMovableMinY;
		*pfMovableMaxY = pTexture->pMovableOptions->fMovableMaxY;
		*pfMovableMinScaleX = pTexture->pMovableOptions->fMovableMinScaleX;
		*pfMovableMaxScaleX = pTexture->pMovableOptions->fMovableMaxScaleX;
		*pfMovableMinScaleY = pTexture->pMovableOptions->fMovableMinScaleY;
		*pfMovableMaxScaleY = pTexture->pMovableOptions->fMovableMaxScaleY;
		*pbMovableCanEditPosition = pTexture->pMovableOptions->bMovableCanEditPosition;
		*pbMovableCanEditRotation = pTexture->pMovableOptions->bMovableCanEditRotation;
		*pbMovableCanEditScale = pTexture->pMovableOptions->bMovableCanEditScale;
	}
}

bool costumeTailor_PartCopyMaterials(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pSrcPart, NOCONST(PCPart) *pDestPart, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll)
{
	return costumeTailor_CopyToGroupPart(pPCCostume, pSrcPart, pDestPart, pSpecies, EDIT_FLAG_MATERIAL, eaUnlockedCostumes, false, bUnlockAll, false, true);
}


bool costumeTailor_PartApplyColorRules(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, const PCSlotType *pSlotType)
{
	bool bChanged = false;
	int i, j;

	// Apply color linking rules
	if (pPart->eColorLink == kPCColorLink_All) {
		// Copy colors to all other bones with the "All" linkage
		for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
			NOCONST(PCPart) *pOtherPart = pPCCostume->eaParts[i];
			
			if ((pOtherPart != pPart) && (pOtherPart->eColorLink == kPCColorLink_All)) {
				bChanged |= costumeTailor_PartCopyColors(pPart, pOtherPart, costumeTailor_IsMirroringColorsAllowed(pPCCostume, pSpecies, pSlotType, pOtherPart, pPart, true));
			}

			if (pOtherPart->pClothLayer) {
				if ((pOtherPart->pClothLayer != pPart) && (pOtherPart->pClothLayer->eColorLink == kPCColorLink_All)) {
					bChanged |= costumeTailor_PartCopyColors(pPart, pOtherPart->pClothLayer, costumeTailor_IsMirroringColorsAllowed(pPCCostume, pSpecies, pSlotType, pOtherPart->pClothLayer, pPart, true));
				}
			}
		}
	} else
	{
		if (pPart->eColorLink == kPCColorLink_Mirror || pPart->eColorLink == kPCColorLink_MirrorGroup) {
			PCBoneDef *pDef = GET_REF(pPart->hBoneDef);

			if (pDef && GET_REF(pDef->hMirrorBone)) {
				// Find the mirror bone part (if any) and copy colors there
				for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
					NOCONST(PCPart) *pOtherPart = pPCCostume->eaParts[i];
				
					if (pOtherPart != pPart && GET_REF(pOtherPart->hBoneDef) == GET_REF(pDef->hMirrorBone)) {
						bChanged |= costumeTailor_PartCopyColors(pPart, pOtherPart, costumeTailor_IsMirroringColorsAllowed(pPCCostume, pSpecies, pSlotType, pOtherPart, pPart, true));
						break;
					}
				}
			}
		}
		if (pPart->eColorLink == kPCColorLink_Group || pPart->eColorLink == kPCColorLink_MirrorGroup) {
			PCBoneDef *pDef = GET_REF(pPart->hBoneDef);

			if (pDef) {
				PCSkeletonDef *skel = GET_REF(pPCCostume->hSkeleton);
				if (skel)
				{
					PCBoneGroup **bg = skel->eaBoneGroups;
					if (bg)
					{
						for(j=eaSize(&bg)-1; j>=0; --j)
						{
							if (!(bg[j]->eBoneGroupFlags & kPCBoneGroupFlags_MatchGeos)) continue;
							// Find the bone group parts (if any) and copy colors from there
							for (i = eaSize(&bg[j]->eaBoneInGroup)-1; i >= 0; --i)
							{
								NOCONST(PCPart) *pOtherPart = costumeTailor_GetPartByBone(pPCCostume, GET_REF(bg[j]->eaBoneInGroup[i]->hBone), NULL);
								if (pOtherPart && pOtherPart != pPart) {
									bChanged |= costumeTailor_PartCopyColors(pPart, pOtherPart, costumeTailor_IsMirroringColorsAllowed(pPCCostume, pSpecies, pSlotType, pOtherPart, pPart, true));
								}
							}
						}
					}
				}
			}
		}
	}

	return bChanged;
}

void costumeTailor_PartApplyMaterialRules(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll)
{
	int i, j;

	// Apply material linking rules
	if (pPart->eMaterialLink == kPCColorLink_All) {
		// Copy materials to all other bones with the "All" linkage
		for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
			NOCONST(PCPart) *pOtherPart = pPCCostume->eaParts[i];

			if ((pOtherPart != pPart) && (pOtherPart->eMaterialLink == kPCColorLink_All)) {
				costumeTailor_PartCopyMaterials(pPCCostume, pPart, pOtherPart, pSpecies, eaUnlockedCostumes, bUnlockAll);
			}

			if (pOtherPart->pClothLayer) {
				if ((pOtherPart->pClothLayer != pPart) && (pOtherPart->pClothLayer->eMaterialLink == kPCColorLink_All)) {
					costumeTailor_PartCopyMaterials(pPCCostume, pPart, pOtherPart->pClothLayer, pSpecies, eaUnlockedCostumes, bUnlockAll);
				}
			}
		}
	} else
	{
		if (pPart->eMaterialLink == kPCColorLink_Mirror || pPart->eMaterialLink == kPCColorLink_MirrorGroup) {
			PCBoneDef *pDef = GET_REF(pPart->hBoneDef);

			if (pDef && GET_REF(pDef->hMirrorBone)) {
				// Find the mirror bone part (if any) and copy materials there
				for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
					NOCONST(PCPart) *pOtherPart = pPCCostume->eaParts[i];

					if (pOtherPart != pPart && GET_REF(pOtherPart->hBoneDef) == GET_REF(pDef->hMirrorBone)) {
						costumeTailor_PartCopyMaterials(pPCCostume, pPart, pOtherPart, pSpecies, eaUnlockedCostumes, bUnlockAll);
						break;
					}
				}
			}
		}
		if (pPart->eMaterialLink == kPCColorLink_Group || pPart->eMaterialLink == kPCColorLink_MirrorGroup) {
			PCBoneDef *pDef = GET_REF(pPart->hBoneDef);

			if (pDef) {
				PCSkeletonDef *skel = GET_REF(pPCCostume->hSkeleton);
				if (skel)
				{
					PCBoneGroup **bg = skel->eaBoneGroups;
					if (bg)
					{
						for(j=eaSize(&bg)-1; j>=0; --j)
						{
							if (!(bg[j]->eBoneGroupFlags & kPCBoneGroupFlags_MatchGeos)) continue;
							// Find the bone group parts (if any) and copy materials from there
							for (i = eaSize(&bg[j]->eaBoneInGroup)-1; i >= 0; --i)
							{
								NOCONST(PCPart) *pOtherPart = costumeTailor_GetPartByBone(pPCCostume, GET_REF(bg[j]->eaBoneInGroup[i]->hBone), NULL);
								if (pOtherPart && pOtherPart != pPart) {
									costumeTailor_PartCopyMaterials(pPCCostume, pPart, pOtherPart, pSpecies, eaUnlockedCostumes, bUnlockAll);
								}
							}
						}
					}
				}
			}
		}
	}
}

AUTO_TRANS_HELPER_SIMPLE;
bool costumeTailor_TestRestriction(PCRestriction eRestriction, bool bUnlocked, PlayerCostume *pPCCostume)
{
	return !pPCCostume ||
			(pPCCostume->eCostumeType == kPCCostumeType_Unrestricted) ||
			(pPCCostume->eCostumeType == kPCCostumeType_Item) ||
			(pPCCostume->eCostumeType == kPCCostumeType_Overlay) ||
			(((eRestriction & kPCRestriction_Player_Initial) != 0) && (pPCCostume->eCostumeType == kPCCostumeType_Player || pPCCostume->eCostumeType == kPCCostumeType_UGC)) ||
			(((eRestriction & kPCRestriction_Player) != 0) && (pPCCostume->eCostumeType == kPCCostumeType_Player) && bUnlocked) ||
			(((eRestriction & kPCRestriction_UGC_Initial) != 0) && (pPCCostume->eCostumeType == kPCCostumeType_UGC)) ||
			(((eRestriction & kPCRestriction_UGC) != 0) && (pPCCostume->eCostumeType == kPCCostumeType_UGC) && bUnlocked) ||
			(((eRestriction & kPCRestriction_NPC) != 0) && (pPCCostume->eCostumeType == kPCCostumeType_NPC)) ||
			(((eRestriction & kPCRestriction_NPCObject) != 0) && (pPCCostume->eCostumeType == kPCCostumeType_NPCObject))
		   ;
}

#define costumeTestRestrictionEfficiently(eRestriction, bUnlocked, pPCCostume) \
	(costumeTailor_TestRestriction((eRestriction), false, (pPCCostume)) \
	|| ((((eRestriction) & kPCRestriction_Player) != 0) \
		&& ((pPCCostume)->eCostumeType == kPCCostumeType_Player) \
		&& (bUnlocked)) \
	)


// Sets the color on a part, honoring color linking and skin color rules
// "colorNum" is 0 through 3.  "glowScale" is 0 through 10.
bool costumeTailor_SetPartColor(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, NOCONST(PCPart) *pPart, int colorNum, U8 color[4], U8 glowScale)
{
	bool bChanged = false;

	switch(colorNum) {
		case 0: if (!IS_SAME_COSTUME_COLOR(color, pPart->color0)) { 
					COPY_COSTUME_COLOR(color, pPart->color0);
					bChanged = true;
				} 
				break;
		case 1: if (!IS_SAME_COSTUME_COLOR(color, pPart->color1)) { 
					COPY_COSTUME_COLOR(color, pPart->color1); 
					bChanged = true;
				} 
				break;
		case 2: if (!IS_SAME_COSTUME_COLOR(color, pPart->color2)) { 
					COPY_COSTUME_COLOR(color, pPart->color2); 
					bChanged = true;
				} 
				break;
		case 3: if (!IS_SAME_COSTUME_COLOR(color, pPart->color3)) { 
					COPY_COSTUME_COLOR(color, pPart->color3); 
					bChanged = true;
				} 
				break;
		default: assertmsg(0, "Invalid value");
	}
	if ((glowScale > 1) && !pPart->pCustomColors) {
		pPart->pCustomColors = StructCreateNoConst(parse_PCCustomColorInfo);
	}
	if (pPart->pCustomColors) {
		pPart->pCustomColors->glowScale[colorNum] = glowScale;
	}

	bChanged |= costumeTailor_PartApplyColorRules(pPCCostume, pPart, pSpecies, pSlotType);

	return bChanged;
}

bool costumeTailor_GetPartColor(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, int colorNum, U8 color[4])
{
	switch(colorNum) {
		case 0: 
			COPY_COSTUME_COLOR(pPart->color0, color);
			return true;
		case 1: 
			COPY_COSTUME_COLOR(pPart->color1, color);
			return true;
		case 2: 
			COPY_COSTUME_COLOR(pPart->color2, color);
			return true;
		case 3: 
			COPY_COSTUME_COLOR(pPart->color3, color);
			return true;
		default: assertmsg(0, "Invalid value");
	}
	return false;
}


// This is a variant of setting color that supports mirror bones and getting
// a "real part".  It also modifies skin color instead of channel 3 when appropriate.
// sdangelo: I'm not very happy with this, but I'm afraid to modify the core function at this
// time but I also wanted to move this logic out of UI layers into a shared location.
bool costumeTailor_SetRealPartColor(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, int iColorNum, U8 color[4],
									PCSlotType *pSlotType, bool bMirrorSelectMode)
{
	NOCONST(PCPart) *pRealPart;
	NOCONST(PCPart) *pMirrorPart = NULL;
	PCBoneDef *pBone;
	PCMaterialDef *pMirrorMat = NULL;
	PCGeometryDef *pGeo;
	PCMaterialDef *pMat;
	bool bBothMode = false;
	bool bChanged = false;

	if (!pPart) {
		return false;
	}
	pBone = GET_REF(pPart->hBoneDef);
	if (!pBone || (stricmp(pBone->pcName,"None") == 0)) {
		return false;
	}
	pMat = GET_REF(pPart->hMatDef);

	// Make sure color is in range
	assert(iColorNum >=0 && iColorNum <= 3);

	// Get the real part info
	pRealPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);
	assert(pRealPart);
	if (bMirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Right)) {
		pGeo = GET_REF(pPart->hGeoDef);
	} else {
		pGeo = GET_REF(pRealPart->hGeoDef);
	}
	assert(pGeo);
	if (pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer && (pRealPart->eEditMode == kPCEditMode_Both)) {
		bBothMode = true;
	}

	// Apply mirror change if appropriate
	if (bMirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Both)) {
		pMirrorPart = costumeTailor_GetMirrorPart(pCostume, pPart);
		if (pMirrorPart) {
			pMirrorMat = GET_REF(pMirrorPart->hMatDef);
			if ((iColorNum == 3) && pMirrorMat && pMat->bHasSkin && pMirrorMat->bHasSkin) {
				bChanged |= costumeTailor_SetSkinColor(pCostume, color);
			} else {
				bChanged |= costumeTailor_SetPartColor(pCostume, pSpecies, pSlotType, pMirrorPart, iColorNum, color, GET_PART_GLOWSCALE(pMirrorPart,iColorNum));
			}
		}
	}

	if ((iColorNum == 3) && (pMat && pMat->bHasSkin && (!pMirrorMat || pMirrorMat->bHasSkin))) {
		bChanged |= costumeTailor_SetSkinColor(pCostume, color);
	} else {
		if (bBothMode) {
			bChanged |= costumeTailor_SetPartColor(pCostume, pSpecies, pSlotType, pRealPart, iColorNum, color, GET_PART_GLOWSCALE(pRealPart,iColorNum));
			bChanged |= costumeTailor_SetPartColor(pCostume, pSpecies, pSlotType, pRealPart->pClothLayer, iColorNum, color, GET_PART_GLOWSCALE(pRealPart->pClothLayer,iColorNum));
		} else {
			bChanged |= costumeTailor_SetPartColor(pCostume, pSpecies, pSlotType, pPart, iColorNum, color, GET_PART_GLOWSCALE(pPart,iColorNum));
		}
	}

	return bChanged;
}

// This returns skin color instead of channel 3 when appropriate.
// andrewa: sdangelo: I'm not very happy with this, but I'm afraid to modify the core function at this
// time but I also wanted to move this logic out of UI layers into a shared location.
bool costumeTailor_GetRealPartColor(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, int iColorNum, U8 color[4])
{
	NOCONST(PCPart) *pRealPart;
	NOCONST(PCPart) *pMirrorPart = NULL;
	PCBoneDef *pBone;
	PCMaterialDef *pMirrorMat = NULL;
	PCGeometryDef *pGeo;
	PCMaterialDef *pMat;

	if (!pPart) {
		return false;
	}
	pBone = GET_REF(pPart->hBoneDef);
	if (!pBone || (stricmp(pBone->pcName,"None") == 0)) {
		return false;
	}
	pMat = GET_REF(pPart->hMatDef);

	// Make sure color is in range
	assert(iColorNum >=0 && iColorNum <= 3);

	// Get the real part info
	pRealPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);
	assert(pRealPart);
	if (pRealPart->eEditMode == kPCEditMode_Right) {
		pGeo = GET_REF(pPart->hGeoDef);
	} else {
		pGeo = GET_REF(pRealPart->hGeoDef);
	}
	assert(pGeo);

	if ((iColorNum == 3) && pMat && pMat->bHasSkin) {
		return costumeTailor_GetSkinColor(pCostume, color);
	} else {
		return costumeTailor_GetPartColor(pCostume, pPart, iColorNum, color);
	}
}

// Sets all four colors and glow scaling on a part, honoring color linking a skin color rules
void costumeTailor_SetPartColors(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, NOCONST(PCPart) *pPart, U8 color0[4], U8 color1[4], U8 color2[4], U8 color3[4], U8 glowScale[4])
{
	// Check if this is a no-op change
	if (IS_SAME_COSTUME_COLOR(color0, pPart->color0) && IS_SAME_COSTUME_COLOR(color1, pPart->color1) && 
		IS_SAME_COSTUME_COLOR(color2, pPart->color2) && IS_SAME_COSTUME_COLOR(color3, pPart->color3) &&
		(!pPart->pCustomColors ||
		 ((glowScale[0] == pPart->pCustomColors->glowScale[0]) && (glowScale[1] == pPart->pCustomColors->glowScale[1]) &&
		  (glowScale[2] == pPart->pCustomColors->glowScale[2]) && (glowScale[3] == pPart->pCustomColors->glowScale[3])))
		) {
		return;
	}

	COPY_COSTUME_COLOR(color0, pPart->color0);
	COPY_COSTUME_COLOR(color1, pPart->color1);
	COPY_COSTUME_COLOR(color2, pPart->color2);
	COPY_COSTUME_COLOR(color3, pPart->color3);
	if (!pPart->pCustomColors && ((glowScale[0] > 1) || (glowScale[1] > 1) || (glowScale[2] > 1) || (glowScale[3] > 1))) {
		pPart->pCustomColors = StructCreateNoConst(parse_PCCustomColorInfo);
	}
	if (pPart->pCustomColors) {
		pPart->pCustomColors->glowScale[0] = glowScale[0];
		pPart->pCustomColors->glowScale[1] = glowScale[1];
		pPart->pCustomColors->glowScale[2] = glowScale[2];
		pPart->pCustomColors->glowScale[3] = glowScale[3];
	}

	costumeTailor_PartApplyColorRules(pPCCostume, pPart, pSpecies, pSlotType);
}

bool costumeTailor_SetSkinColor(NOCONST(PlayerCostume) *pPCCostume, U8 skinColor[4])
{
	if (!IS_SAME_COSTUME_COLOR(skinColor, pPCCostume->skinColor)) {
		// Copy to skin color
		COPY_COSTUME_COLOR(skinColor, pPCCostume->skinColor);
		return true;
	}
	return false;
}

bool costumeTailor_GetSkinColor(NOCONST(PlayerCostume) *pPCCostume, U8 skinColor[4])
{
	// Copy to skin color
	COPY_COSTUME_COLOR(pPCCostume->skinColor, skinColor);
	return true;
}

void costumeTailor_SetPartColorLinking(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, PCColorLink eColorLink, SpeciesDef *pSpecies, const PCSlotType *pSlotType)
{
	PCColorLink eOldLink;
	int i, j, result, done = 0;

	// Do nothing if nothing changes
	if (pPart->eColorLink == eColorLink) {
		return;
	}

	// Save old value then change current value
	eOldLink = pPart->eColorLink;
	pPart->eColorLink = eColorLink;

	if (pPart->pClothLayer)
		pPart->pClothLayer->eColorLink = eColorLink;

	// Apply link change rules
	if (eColorLink == kPCColorLink_All) {
		// Copy colors from another part that is already set to All
		for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
			NOCONST(PCPart) *pOtherPart = pPCCostume->eaParts[i];
			if (pOtherPart != pPart && pOtherPart->eColorLink == kPCColorLink_All) {
				if ((result = costumeTailor_IsMirroringColorsAllowed(pPCCostume, pSpecies, pSlotType, pPart, pOtherPart, true)))
				{
					costumeTailor_PartCopyColors(pOtherPart, pPart, result);
					if ((done |= result) == 0x0F)
					{
						break;
					}
				}
			}
		}
	} else
	{
		if (eColorLink == kPCColorLink_Mirror || eColorLink == kPCColorLink_MirrorGroup) {
			PCBoneDef *pDef = GET_REF(pPart->hBoneDef);

			if (pDef && GET_REF(pDef->hMirrorBone)) {
				// Find the mirror bone part (if any) and copy colors from there
				for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
					NOCONST(PCPart) *pOtherPart = pPCCostume->eaParts[i];
					if (pOtherPart != pPart && GET_REF(pOtherPart->hBoneDef) == GET_REF(pDef->hMirrorBone)) {
						// Copy colors from mirror part
						costumeTailor_PartCopyColors(pOtherPart, pPart, costumeTailor_IsMirroringColorsAllowed(pPCCostume, pSpecies, pSlotType, pPart, pOtherPart, true));

						// Make mirror part have mirrored linking too
						costumeTailor_SetPartColorLinking(pPCCostume, pOtherPart, eColorLink, pSpecies, pSlotType);
						break;
					}
				}
			}
		}
		if (eColorLink == kPCColorLink_Group || eColorLink == kPCColorLink_MirrorGroup) {
			PCBoneDef *pDef = GET_REF(pPart->hBoneDef);
			bool found = false;

			if (pDef) {
				PCSkeletonDef *skel = GET_REF(pPCCostume->hSkeleton);
				if (skel)
				{
					PCBoneGroup **bg = skel->eaBoneGroups;
					if (bg)
					{
						for(j=eaSize(&bg)-1; j>=0; --j)
						{
							if (!(bg[j]->eBoneGroupFlags & kPCBoneGroupFlags_MatchGeos)) continue;
							// Find the bone group parts (if any) and copy colors from there
							for (i = eaSize(&bg[j]->eaBoneInGroup)-1; i >= 0; --i)
							{
								NOCONST(PCPart) *pOtherPart = costumeTailor_GetPartByBone(pPCCostume, GET_REF(bg[j]->eaBoneInGroup[i]->hBone), NULL);
								if (pOtherPart && pOtherPart != pPart) {
									// Copy colors from Group part
									if (!found)
									{
										if ((result = costumeTailor_IsMirroringColorsAllowed(pPCCostume, pSpecies, pSlotType, pPart, pOtherPart, true)))
										{
											costumeTailor_PartCopyColors(pOtherPart, pPart, result);
											if ((done |= result) == 0x0F)
											{
												found = true;
											}
										}
									}

									// Make bone group part have Group linking too
									costumeTailor_SetPartColorLinking(pPCCostume, pOtherPart, eColorLink, pSpecies, pSlotType);
								}
							}
						}
					}
				}
			}
		}
	}

	// Need to apply same linking to the mirror part if it was mirror linked before
	if (eOldLink == kPCColorLink_Mirror || eOldLink == kPCColorLink_MirrorGroup) {
		PCBoneDef *pDef = GET_REF(pPart->hBoneDef);

		if (pDef && GET_REF(pDef->hMirrorBone)) {
			// Find the mirror bone part (if any) and alter linkage there
			for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
				NOCONST(PCPart) *pOtherPart = pPCCostume->eaParts[i];
				if (pOtherPart != pPart && GET_REF(pOtherPart->hBoneDef) == GET_REF(pDef->hMirrorBone)) {
					costumeTailor_SetPartColorLinking(pPCCostume, pOtherPart, eColorLink, pSpecies, pSlotType);
					break;
				}
			}
		}
	}
	// Need to apply same linking to the bone group parts if it was group linked before
	if (eOldLink == kPCColorLink_Group || eOldLink == kPCColorLink_MirrorGroup) {
		PCBoneDef *pDef = GET_REF(pPart->hBoneDef);

		if (pDef) {
			PCSkeletonDef *skel = GET_REF(pPCCostume->hSkeleton);
			if (skel)
			{
				PCBoneGroup **bg = skel->eaBoneGroups;
				if (bg)
				{
					for(j=eaSize(&bg)-1; j>=0; --j)
					{
						if (!(bg[j]->eBoneGroupFlags & kPCBoneGroupFlags_MatchGeos)) continue;
						// Find the bone group part (if any) and alter linkage there
						for (i = eaSize(&bg[j]->eaBoneInGroup)-1; i >= 0; --i)
						{
							NOCONST(PCPart) *pOtherPart = costumeTailor_GetPartByBone(pPCCostume, GET_REF(bg[j]->eaBoneInGroup[i]->hBone), NULL);
							if (pOtherPart && pOtherPart != pPart) {
								costumeTailor_SetPartColorLinking(pPCCostume, pOtherPart, eColorLink, pSpecies, pSlotType);
							}
						}
					}
				}
			}
		}
	}

}


void costumeTailor_SetPartMaterialLinking(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, PCColorLink eMaterialLink, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll)
{
	PCColorLink eOldLink;
	int i, j;

	// Do nothing if nothing changes
	if (!pPart || pPart->eMaterialLink == eMaterialLink) {
		return;
	}

	// Save old value then change current value
	eOldLink = pPart->eMaterialLink;
	pPart->eMaterialLink = eMaterialLink;

	// Apply link change rules
	if (eMaterialLink == kPCColorLink_All) {
		// Copy materials from another part that is already set to All
		for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
			NOCONST(PCPart) *pOtherPart = pPCCostume->eaParts[i];
			if (pOtherPart != pPart && pOtherPart->eMaterialLink == kPCColorLink_All) {
				if (costumeTailor_PartCopyMaterials(pPCCostume, pOtherPart, pPart, pSpecies, eaUnlockedCostumes, bUnlockAll))
				{
					break;
				}
			}
		}
	}
	else
	{
		if (eMaterialLink == kPCColorLink_Mirror || eMaterialLink == kPCColorLink_MirrorGroup) {
			PCBoneDef *pDef = GET_REF(pPart->hBoneDef);

			if (pDef && GET_REF(pDef->hMirrorBone)) {
				// Find the mirror bone part (if any) and copy materials from there
				for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
					NOCONST(PCPart) *pOtherPart = pPCCostume->eaParts[i];
					if (pOtherPart != pPart && GET_REF(pOtherPart->hBoneDef) == GET_REF(pDef->hMirrorBone)) {
						// Copy materials from mirror part
						if (costumeTailor_PartCopyMaterials(pPCCostume, pOtherPart, pPart, pSpecies, eaUnlockedCostumes, bUnlockAll))
						{
							// Make mirror part have mirrored linking too
							costumeTailor_SetPartMaterialLinking(pPCCostume, pOtherPart, eMaterialLink, pSpecies, eaUnlockedCostumes, bUnlockAll);
							break;
						}
					}
				}
			}
		}
		if (eMaterialLink == kPCColorLink_Group || eMaterialLink == kPCColorLink_MirrorGroup) {
			PCBoneDef *pDef = GET_REF(pPart->hBoneDef);
			bool found = false;

			if (pDef) {
				PCSkeletonDef *skel = GET_REF(pPCCostume->hSkeleton);
				if (skel)
				{
					PCBoneGroup **bg = skel->eaBoneGroups;
					if (bg)
					{
						for(j=eaSize(&bg)-1; j>=0; --j)
						{
							if (!(bg[j]->eBoneGroupFlags & kPCBoneGroupFlags_MatchGeos)) continue;
							// Find the bone group parts (if any) and copy materials from there
							for (i = eaSize(&bg[j]->eaBoneInGroup)-1; i >= 0; --i)
							{
								NOCONST(PCPart) *pOtherPart = costumeTailor_GetPartByBone(pPCCostume, GET_REF(bg[j]->eaBoneInGroup[i]->hBone), NULL);
								if (pOtherPart && pOtherPart != pPart) {
									// Copy materials from Group part
									if (!found)
									{
										found = costumeTailor_PartCopyMaterials(pPCCostume, pOtherPart, pPart, pSpecies, eaUnlockedCostumes, bUnlockAll);
									}

									if (found)
									{
										// Make bone group part have Group linking too
										costumeTailor_SetPartMaterialLinking(pPCCostume, pOtherPart, eMaterialLink, pSpecies, eaUnlockedCostumes, bUnlockAll);
									}
								}
							}
						}
					}
				}
			}
		}
	}

	// Need to apply same linking to the mirror part if it was mirror linked before
	if (eOldLink == kPCColorLink_Mirror || eOldLink == kPCColorLink_MirrorGroup) {
		PCBoneDef *pDef = GET_REF(pPart->hBoneDef);

		if (pDef && GET_REF(pDef->hMirrorBone)) {
			// Find the mirror bone part (if any) and alter linkage there
			for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
				NOCONST(PCPart) *pOtherPart = pPCCostume->eaParts[i];
				if (pOtherPart != pPart && GET_REF(pOtherPart->hBoneDef) == GET_REF(pDef->hMirrorBone)) {
					costumeTailor_SetPartMaterialLinking(pPCCostume, pOtherPart, eMaterialLink, pSpecies, eaUnlockedCostumes, bUnlockAll);
					break;
				}
			}
		}
	}
	// Need to apply same linking to the bone group parts if it was group linked before
	if (eOldLink == kPCColorLink_Group || eOldLink == kPCColorLink_MirrorGroup) {
		PCBoneDef *pDef = GET_REF(pPart->hBoneDef);

		if (pDef) {
			PCSkeletonDef *skel = GET_REF(pPCCostume->hSkeleton);
			if (skel)
			{
				PCBoneGroup **bg = skel->eaBoneGroups;
				if (bg)
				{
					for(j=eaSize(&bg)-1; j>=0; --j)
					{
						if (!(bg[j]->eBoneGroupFlags & kPCBoneGroupFlags_MatchGeos)) continue;
						// Find the bone group part (if any) and alter linkage there
						for (i = eaSize(&bg[j]->eaBoneInGroup)-1; i >= 0; --i)
						{
							NOCONST(PCPart) *pOtherPart = costumeTailor_GetPartByBone(pPCCostume, GET_REF(bg[j]->eaBoneInGroup[i]->hBone), NULL);
							if (pOtherPart && pOtherPart != pPart) {
								costumeTailor_SetPartMaterialLinking(pPCCostume, pOtherPart, eMaterialLink, pSpecies, eaUnlockedCostumes, bUnlockAll);
							}
						}
					}
				}
			}
		}
	}

}


void costumeTailor_SortCostumesFromSet(CostumeRefForSet **peaCostumes, bool bSortDisplay)
{
	int i, j;

	for(i=eaSize(&peaCostumes)-1; i>=1; --i) {
		for(j=i-1; j>=0; --j) {
			if (peaCostumes[i]->fOrder == peaCostumes[j]->fOrder) {
				if (bSortDisplay) {
					const char *pcName1 = TranslateDisplayMessage(peaCostumes[i]->displayNameMsg);
					const char *pcName2 = TranslateDisplayMessage(peaCostumes[j]->displayNameMsg);
					if (pcName1 && pcName2 && stricmp(pcName1,pcName2) < 0) {
						eaSwap(&peaCostumes, i, j);
					}
				} else {
					if (stricmp(peaCostumes[i]->pcName,peaCostumes[j]->pcName) < 0) {
						eaSwap(&peaCostumes, i, j);
					}
				}
			} else if ((peaCostumes[i]->fOrder > 0) && ((peaCostumes[j]->fOrder == 0) || (peaCostumes[i]->fOrder < peaCostumes[j]->fOrder))) {
				eaSwap(&peaCostumes, i, j);
			}
		}
	}
}


void costumeTailor_SortSkeletons(PCSkeletonDef **eaSkels, bool bSortDisplay)
{
	int i, j;

	for(i=eaSize(&eaSkels)-1; i>=1; --i) {
		for(j=i-1; j>=0; --j) {
			if (eaSkels[i]->fOrder == eaSkels[j]->fOrder) {
				if (bSortDisplay) {
					const char *pcName1 = TranslateDisplayMessage(eaSkels[i]->displayNameMsg);
					const char *pcName2 = TranslateDisplayMessage(eaSkels[j]->displayNameMsg);
					if (pcName1 && pcName2 && stricmp(pcName1,pcName2) < 0) {
						eaSwap(&eaSkels, i, j);
					}
				} else {
					if (stricmp(eaSkels[i]->pcName,eaSkels[j]->pcName) < 0) {
						eaSwap(&eaSkels, i, j);
					}
				}
			} else if ((eaSkels[i]->fOrder > 0) && ((eaSkels[j]->fOrder == 0) || (eaSkels[i]->fOrder < eaSkels[j]->fOrder))) {
				eaSwap(&eaSkels, i, j);
			}
		}
	}
}


void costumeTailor_SortSpecies(SpeciesDef **eaSpecies, bool bSortDisplay)
{
	int i, j;

	for(i=eaSize(&eaSpecies)-1; i>=1; --i) {
		for(j=i-1; j>=0; --j) {
			if (eaSpecies[i]->fOrder == eaSpecies[j]->fOrder) {
				if (bSortDisplay) {
					const char *pcName1 = TranslateDisplayMessage(eaSpecies[i]->displayNameMsg);
					const char *pcName2 = TranslateDisplayMessage(eaSpecies[j]->displayNameMsg);
					if (pcName1 && pcName2 && stricmp(pcName1,pcName2) < 0) {
						eaSwap(&eaSpecies, i, j);
					}
				} else {
					if (stricmp(eaSpecies[i]->pcName,eaSpecies[j]->pcName) < 0) {
						eaSwap(&eaSpecies, i, j);
					}
				}
			} else if ((eaSpecies[i]->fOrder > 0) && ((eaSpecies[j]->fOrder == 0) || (eaSpecies[i]->fOrder < eaSpecies[j]->fOrder))) {
				eaSwap(&eaSpecies, i, j);
			}
		}
	}
}


void costumeTailor_SortPresets(CostumePreset **eaPresets, bool bSortDisplay)
{
	int i, j;

	for(i=eaSize(&eaPresets)-1; i>=1; --i) {
		CostumePreset *pPreset1 = eaPresets[i];
		CostumePresetCategory *pPresetCat1 = GET_REF(pPreset1->hPresetCategory);
		for(j=i-1; j>=0; --j) {
			CostumePreset *pPreset2 = eaPresets[j];
			if (pPreset1->fOrder == pPreset2->fOrder) {
				CostumePresetCategory *pPresetCat2 = GET_REF(pPreset2->hPresetCategory);
				if (bSortDisplay) {
					const char *pcTempName1 = TranslateDisplayMessage(pPreset1->displayNameMsg);
					const char *pcTempName2 = TranslateDisplayMessage(pPreset2->displayNameMsg);
					const char *pcName1 = pcTempName1 || !pPresetCat1 ? pcTempName1 : TranslateDisplayMessage(pPresetCat1->displayNameMsg);
					const char *pcName2 = pcTempName2 || !pPresetCat2 ? pcTempName2 : TranslateDisplayMessage(pPresetCat2->displayNameMsg);
					if (pcName1 && pcName2 && stricmp(pcName1,pcName2) < 0) {
						eaSwap(&eaPresets, i, j);
					}
				} else {
					const char *pcName1 = (pPreset1->pcName && *pPreset1->pcName) || !pPresetCat1 ? pPreset1->pcName : pPresetCat1->pcName;
					const char *pcName2 = (pPreset2->pcName && *pPreset2->pcName) || !pPresetCat2 ? pPreset2->pcName : pPresetCat2->pcName;
					if (pcName1 && pcName2 && stricmp(pcName1,pcName2) < 0) {
						eaSwap(&eaPresets, i, j);
					}
				}
			} else if ((pPreset1->fOrder > 0) && ((pPreset2->fOrder == 0) || (pPreset1->fOrder < pPreset2->fOrder))) {
				eaSwap(&eaPresets, i, j);
			}
		}
	}
}


void costumeTailor_SortStances(PCStanceInfo **eaStances, bool bSortDisplay)
{
	int i, j;

	for(i=eaSize(&eaStances)-1; i>=1; --i) {
		for(j=i-1; j>=0; --j) {
			if (eaStances[i]->fOrder == eaStances[j]->fOrder) {
				if (bSortDisplay) {
					const char *pcName1 = TranslateDisplayMessage(eaStances[i]->displayNameMsg);
					const char *pcName2 = TranslateDisplayMessage(eaStances[j]->displayNameMsg);
					if (pcName1 && pcName2 && stricmp(pcName1,pcName2) < 0) {
						eaSwap(&eaStances, i, j);
					}
				} else {
					if (stricmp(eaStances[i]->pcName,eaStances[j]->pcName) < 0) {
						eaSwap(&eaStances, i, j);
					}
				}
			} else if ((eaStances[i]->fOrder > 0) && ((eaStances[j]->fOrder == 0) || (eaStances[i]->fOrder < eaStances[j]->fOrder))) {
				eaSwap(&eaStances, i, j);
			}
		}
	}
}


void costumeTailor_SortMoods(PCMood **eaMoods, bool bSortDisplay)
{
	int i, j;

	for(i=eaSize(&eaMoods)-1; i>=1; --i) {
		for(j=i-1; j>=0; --j) {
			if (eaMoods[i]->fOrder == eaMoods[j]->fOrder) {
				if (bSortDisplay) {
					const char *pcName1 = TranslateDisplayMessage(eaMoods[i]->displayNameMsg);
					const char *pcName2 = TranslateDisplayMessage(eaMoods[j]->displayNameMsg);
					if (pcName1 && pcName2 && stricmp(pcName1,pcName2) < 0) {
						eaSwap(&eaMoods, i, j);
					}
				} else {
					if (stricmp(eaMoods[i]->pcName,eaMoods[j]->pcName) < 0) {
						eaSwap(&eaMoods, i, j);
					}
				}
			} else if ((eaMoods[i]->fOrder > 0) && ((eaMoods[j]->fOrder == 0) || (eaMoods[i]->fOrder < eaMoods[j]->fOrder))) {
				eaSwap(&eaMoods, i, j);
			}
		}
	}
}

void costumeTailor_SortVoices(PCVoice **eaVoices, bool bSortDisplay)
{
	int i, j;

	for(i=eaSize(&eaVoices)-1; i>=1; --i) {
		for(j=i-1; j>=0; --j) {
			if (eaVoices[i]->fOrder == eaVoices[j]->fOrder) {
				if (bSortDisplay) {
					const char *pcName1 = TranslateDisplayMessage(eaVoices[i]->displayNameMsg);
					const char *pcName2 = TranslateDisplayMessage(eaVoices[j]->displayNameMsg);
					if (pcName1 && pcName2 && stricmp(pcName1,pcName2) < 0) {
						eaSwap(&eaVoices, i, j);
					}
				} else {
					if (stricmp(eaVoices[i]->pcName,eaVoices[j]->pcName) < 0) {
						eaSwap(&eaVoices, i, j);
					}
				}
			} else if ((eaVoices[i]->fOrder > 0) && ((eaVoices[j]->fOrder == 0) || (eaVoices[i]->fOrder < eaVoices[j]->fOrder))) {
				eaSwap(&eaVoices, i, j);
			}
		}
	}
}


void costumeTailor_SortBodyScales(PCBodyScaleInfo **eaBodyScales, bool bSortDisplay)
{
	int i, j;

	for(i=eaSize(&eaBodyScales)-1; i>=1; --i) {
		for(j=i-1; j>=0; --j) {
			if (eaBodyScales[i]->fOrder == eaBodyScales[j]->fOrder) {
				if (bSortDisplay) {
					const char *pcName1 = TranslateDisplayMessage(eaBodyScales[i]->displayNameMsg);
					const char *pcName2 = TranslateDisplayMessage(eaBodyScales[j]->displayNameMsg);
					if (pcName1 && pcName2 && stricmp(pcName1,pcName2) < 0) {
						eaSwap(&eaBodyScales, i, j);
					}
				} else {
					if (stricmp(eaBodyScales[i]->pcName,eaBodyScales[j]->pcName) < 0) {
						eaSwap(&eaBodyScales, i, j);
					}
				}
			} else if ((eaBodyScales[i]->fOrder > 0) && ((eaBodyScales[j]->fOrder == 0) || (eaBodyScales[i]->fOrder < eaBodyScales[j]->fOrder))) {
				eaSwap(&eaBodyScales, i, j);
			}
		}
	}
}


void costumeTailor_SortRegions(PCRegion **eaRegions, bool bSortDisplay)
{
	int i, j;

	for(i=eaSize(&eaRegions)-1; i>=1; --i) {
		for(j=i-1; j>=0; --j) {
			if (eaRegions[i]->fOrder == eaRegions[j]->fOrder) {
				if (bSortDisplay) {
					const char *pcName1 = TranslateDisplayMessage(eaRegions[i]->displayNameMsg);
					const char *pcName2 = TranslateDisplayMessage(eaRegions[j]->displayNameMsg);
					if (pcName1 && pcName2 && stricmp(pcName1,pcName2) < 0) {
						eaSwap(&eaRegions, i, j);
					}
				} else {
					if (stricmp(eaRegions[i]->pcName,eaRegions[j]->pcName) < 0) {
						eaSwap(&eaRegions, i, j);
					}
				}
			} else if ((eaRegions[i]->fOrder > 0) && ((eaRegions[j]->fOrder == 0) || (eaRegions[i]->fOrder < eaRegions[j]->fOrder))) {
				eaSwap(&eaRegions, i, j);
			}
		}
	}
}


void costumeTailor_SortRegionsOnDependency(PCRegion **eaRegions)
{
	int i, j, k, n, m;
	int infiniteCheckCounter = eaSize(&eaRegions) * eaSize(&eaRegions);
	bool bRecheck;

	for(i=eaSize(&eaRegions)-1; (i>=1) && (infiniteCheckCounter > 0); ) {
		bRecheck = false;
		for(j=eaSize(&eaRegions[i]->eaCategories)-1; j>=0 && !bRecheck; --j) {
			PCCategory *pCat = GET_REF(eaRegions[i]->eaCategories[j]->hCategory);
			if (pCat) {
				for(k=eaSize(&pCat->eaExcludedCategories)-1; k>=0 && !bRecheck; --k) {
					// Found an excluded category... see if this is on another region higher up
					for(n=i-1; n>=0 && !bRecheck; --n) {
						for(m=eaSize(&eaRegions[n]->eaCategories)-1; m>=0; --m) {
							if (GET_REF(eaRegions[n]->eaCategories[n]->hCategory) == GET_REF(pCat->eaExcludedCategories[k]->hCategory)) {
								// Dependency!  Move this region up the sort order
								eaSwap(&eaRegions, i, n);
								bRecheck = true;
								--infiniteCheckCounter;
								break;
							}
						}
					}
				}
			}
		}
		if (!bRecheck) {
			--i;
		}
	}
	if (infiniteCheckCounter < 0) {
		Alertf("You appear to have two regions with co-dependent categories.  Review your category ExcludeCategory lines for possible problems.");
	}
}


void costumeTailor_SortCategories(PCCategory **eaCategories, bool bSortDisplay)
{
	int i, j;

	for(i=eaSize(&eaCategories)-1; i>=1; --i) {
		for(j=i-1; j>=0; --j) {
			if (eaCategories[i]->fOrder == eaCategories[j]->fOrder) {
				if (bSortDisplay) {
					const char *pcName1 = TranslateDisplayMessage(eaCategories[i]->displayNameMsg);
					const char *pcName2 = TranslateDisplayMessage(eaCategories[j]->displayNameMsg);
					if (pcName1 && pcName2 && stricmp(pcName1,pcName2) < 0) {
						eaSwap(&eaCategories, i, j);
					}
				} else {
					if (stricmp(eaCategories[i]->pcName,eaCategories[j]->pcName) < 0) {
						eaSwap(&eaCategories, i, j);
					}
				}
			} else if ((eaCategories[i]->fOrder > 0) && ((eaCategories[j]->fOrder == 0) || (eaCategories[i]->fOrder < eaCategories[j]->fOrder))) {
				eaSwap(&eaCategories, i, j);
			}
		}
	}
}

void costumeTailor_SortCategoryRefs(PCCategoryRef **eaCategories, bool bSortDisplay)
{
	int i, j;

	for(i=eaSize(&eaCategories)-1; i>=1; --i) {
		PCCategory *pCategoryI = GET_REF(eaCategories[i]->hCategory);
		if (pCategoryI) {
			for(j=i-1; j>=0; --j) {
				PCCategory *pCategoryJ = GET_REF(eaCategories[j]->hCategory);
				if (pCategoryJ) {
					if (pCategoryI->fOrder == pCategoryJ->fOrder) {
						if (bSortDisplay) {
							const char *pcName1 = TranslateDisplayMessage(pCategoryI->displayNameMsg);
							const char *pcName2 = TranslateDisplayMessage(pCategoryJ->displayNameMsg);
							if (pcName1 && pcName2 && stricmp(pcName1,pcName2) < 0) {
								eaSwap(&eaCategories, i, j);
							}
						} else {
							if (stricmp(pCategoryI->pcName,pCategoryJ->pcName) < 0) {
								eaSwap(&eaCategories, i, j);
							}
						}
					} else if ((pCategoryI->fOrder > 0) && ((pCategoryJ->fOrder == 0) || (pCategoryI->fOrder < pCategoryJ->fOrder))) {
						eaSwap(&eaCategories, i, j);
					}
				}
			}
		}
	}
}

void costumeTailor_SortBones(PCBoneDef **eaBones, bool bSortDisplay)
{
	int i, j;

	for(i=eaSize(&eaBones)-1; i>=1; --i) {
		for(j=i-1; j>=0; --j) {
			if (eaBones[i]->fOrder == eaBones[j]->fOrder) {
				if (bSortDisplay) {
					const char *pcName1 = TranslateDisplayMessage(eaBones[i]->displayNameMsg);
					const char *pcName2 = TranslateDisplayMessage(eaBones[j]->displayNameMsg);
					if (pcName1 && pcName2 && stricmp(pcName1,pcName2) < 0) {
						eaSwap(&eaBones, i, j);
					}
				} else {
					if (stricmp(eaBones[i]->pcName,eaBones[j]->pcName) < 0) {
						eaSwap(&eaBones, i, j);
					}
				}
			} else if ((eaBones[i]->fOrder > 0) && ((eaBones[j]->fOrder == 0) || (eaBones[i]->fOrder < eaBones[j]->fOrder))) {
				eaSwap(&eaBones, i, j);
			}
		}
	}
}

static int costumeTailor_CompareGeos(UserData rawSortDisplay, const PCGeometryDef** ppGeo1, const PCGeometryDef** ppGeo2)
{
	bool bSortDisplay = (bool)rawSortDisplay;

	if( (*ppGeo1)->fOrder == (*ppGeo2)->fOrder ) {
		if( bSortDisplay ) {
			const char* pcName1 = TranslateDisplayMessage( (*ppGeo1)->displayNameMsg );
			const char* pcName2 = TranslateDisplayMessage( (*ppGeo2)->displayNameMsg );
			return stricmp_safe( pcName1, pcName2 );
		} else {
			return stricmp_safe( (*ppGeo1)->pcName, (*ppGeo2)->pcName );
		}
	} else {
		if( (*ppGeo1)->fOrder <= 0 && (*ppGeo2)->fOrder <= 0 ) {
			return 0;
		} else if( (*ppGeo1)->fOrder <= 0 ) {
			return +1;
		} else if( (*ppGeo2)->fOrder <= 0 ) {
			return -1;
		} else {
			return (*ppGeo1)->fOrder - (*ppGeo2)->fOrder;
		}
	}
}


void costumeTailor_SortGeos(PCGeometryDef **eaGeos, bool bSortDisplay)
{
	eaQSort_s(eaGeos, costumeTailor_CompareGeos, (UserData)(intptr_t)bSortDisplay);
}


void costumeTailor_SortMaterials(PCMaterialDef **eaMats, bool bSortDisplay)
{
	int i, j;

	for(i=eaSize(&eaMats)-1; i>=1; --i) {
		for(j=i-1; j>=0; --j) {
			if (eaMats[i]->fOrder == eaMats[j]->fOrder) {
				if (bSortDisplay) {
					const char *pcName1 = TranslateDisplayMessage(eaMats[i]->displayNameMsg);
					const char *pcName2 = TranslateDisplayMessage(eaMats[j]->displayNameMsg);
					if (pcName1 && pcName2 && stricmp(pcName1,pcName2) < 0) {
						eaSwap(&eaMats, i, j);
					}
				} else {
					if (stricmp(eaMats[i]->pcName,eaMats[j]->pcName) < 0) {
						eaSwap(&eaMats, i, j);
					}
				}
			} else if ((eaMats[i]->fOrder > 0) && ((eaMats[j]->fOrder == 0) || (eaMats[i]->fOrder < eaMats[j]->fOrder))) {
				eaSwap(&eaMats, i, j);
			}
		}
	}
}


void costumeTailor_SortTextures(PCTextureDef **eaTexs, bool bSortDisplay)
{
	int i, j;

	for(i=eaSize(&eaTexs)-1; i>=1; --i) {
		for(j=i-1; j>=0; --j) {
			if (eaTexs[i]->fOrder == eaTexs[j]->fOrder) {
				if (bSortDisplay) {
					const char *pcName1 = TranslateDisplayMessage(eaTexs[i]->displayNameMsg);
					const char *pcName2 = TranslateDisplayMessage(eaTexs[j]->displayNameMsg);
					if (pcName1 && pcName2 && stricmp(pcName1,pcName2) < 0) {
						eaSwap(&eaTexs, i, j);
					}
				} else {
					if (stricmp(eaTexs[i]->pcName,eaTexs[j]->pcName) < 0) {
						eaSwap(&eaTexs, i, j);
					}
				}
			} else if ((eaTexs[i]->fOrder > 0) && ((eaTexs[j]->fOrder == 0) || (eaTexs[i]->fOrder < eaTexs[j]->fOrder))) {
				eaSwap(&eaTexs, i, j);
			}
		}
	}
}

void costumeTailor_SortStyles(PCStyle **eaStyles, bool bSortDisplay)
{
	int i, j;

	for(i=eaSize(&eaStyles)-1; i>=1; --i) {
		for(j=i-1; j>=0; --j) {
			if (eaStyles[i]->fOrder == eaStyles[j]->fOrder) {
				if (bSortDisplay) {
					const char *pcName1 = TranslateDisplayMessage(eaStyles[i]->displayNameMsg);
					const char *pcName2 = TranslateDisplayMessage(eaStyles[j]->displayNameMsg);
					if (pcName1 && pcName2 && stricmp(pcName1,pcName2) < 0) {
						eaSwap(&eaStyles, i, j);
					}
				} else {
					if (stricmp(eaStyles[i]->pcName,eaStyles[j]->pcName) < 0) {
						eaSwap(&eaStyles, i, j);
					}
				}
			} else if ((eaStyles[i]->fOrder > 0) && ((eaStyles[j]->fOrder == 0) || (eaStyles[i]->fOrder < eaStyles[j]->fOrder))) {
				eaSwap(&eaStyles, i, j);
			}
		}
	}
}


// Gets the flags for a texture type
PCTextureType costumeTailor_GetTextureFlags(PCTextureDef *pTexDef)
{
	int flags, i;

	if (!pTexDef) {
		return 0;
	}
	flags = pTexDef->eTypeFlags;
	for(i=eaSize(&pTexDef->eaExtraSwaps)-1; i>=0; --i) {
		flags |= pTexDef->eaExtraSwaps[i]->eTypeFlags;
	}
	return flags;
}


// Gets the category for a given region
PCCategory *costumeTailor_GetCategoryForRegion(PlayerCostume *pPCCostume, PCRegion *pRegion)
{
	int i;

	if (!pRegion) {
		return NULL;
	}

	for(i=eaSize(&pPCCostume->eaRegionCategories)-1; i>=0; --i) {
		if (pRegion == GET_REF(pPCCostume->eaRegionCategories[i]->hRegion)) {
			return GET_REF(pPCCostume->eaRegionCategories[i]->hCategory);
		}
	}

	return NULL;
}


// Set the category for a given region
void costumeTailor_SetRegionCategory(NOCONST(PlayerCostume) *pPCCostume, PCRegion *pRegion, PCCategory *pCategory)
{
	int i;

	for(i=eaSize(&pPCCostume->eaRegionCategories)-1; i>=0; --i) {
		if (GET_REF(pPCCostume->eaRegionCategories[i]->hRegion) == pRegion) {
			if (pCategory) {
				SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, pCategory, pPCCostume->eaRegionCategories[i]->hCategory);
			} else {
				StructDestroyNoConst(parse_PCRegionCategory, pPCCostume->eaRegionCategories[i]);
				eaRemove(&pPCCostume->eaRegionCategories, i);
			}
			return;
		}
	}

	if (pCategory) {
		NOCONST(PCRegionCategory) *pRegCat;
		pRegCat = StructCreateNoConst(parse_PCRegionCategory);
		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, pRegion, pRegCat->hRegion);
		SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, pCategory, pRegCat->hCategory);
		eaPush(&pPCCostume->eaRegionCategories, pRegCat);
	}
}


// Evaluate current parts and pick a category that works best
PCCategory *costumeTailor_PickBestCategory(PlayerCostume *pPCCostume, PCRegion *pRegion, SpeciesDef *pSpecies, PCSlotType *pSlotType, bool bIssueWarning)
{
	PCSkeletonDef *pSkel;
	PCGeometryDef **eaGeos = NULL;
	PCCategory **eaCategories = NULL;
	PCCategory *pCategory = NULL;
	int iBestCount = 0;
	int i,j,k;

	pSkel = GET_REF(pPCCostume->hSkeleton);
	if (!pSkel) {
		return NULL;
	}
	if (!pRegion) {
		return NULL;
	}

	// Find the geos for all parts in the region
	for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
		PCBoneDef *pBone;
		PCPart *pPart;
		PCGeometryDef *pGeo;

		// Collect Info
		pPart = pPCCostume->eaParts[i];
		pBone = GET_REF(pPart->hBoneDef);
		if (!pBone) {
			continue;
		}
		if (GET_REF(pBone->hRegion) == pRegion) {
			continue; // Not in region
		}

		pGeo = GET_REF(pPart->hGeoDef);
		if (pGeo) {
			eaPush(&eaGeos, pGeo);
		}
	}

	// Return NULL if no parts
	if (!eaSize(&eaGeos)) {
		return NULL;
	}

	// Get the legal categories
	costumeTailor_GetValidCategories(CONTAINER_NOCONST(PlayerCostume, pPCCostume), pRegion, pSpecies, NULL, NULL, pSlotType, &eaCategories, 0);

	// Look for a legal category that satisfies all geos
	for(i=0; i<eaSize(&pRegion->eaCategories); ++i) {
		PCCategory *pTestCat = GET_REF(pRegion->eaCategories[i]->hCategory);
		int iLocalCount = 0;

		if (eaFind(&eaCategories, pTestCat) == -1) {
			continue; // Not a legal category on this costume
		}

		for(j=eaSize(&eaGeos)-1; j>=0; --j) {
			for(k=eaSize(&eaGeos[j]->eaCategories)-1; k>=0; --k) {
				if (GET_REF(eaGeos[j]->eaCategories[k]->hCategory) == pTestCat) {
					++iLocalCount;
					break;
				}
			}
		}
		if (iLocalCount > iBestCount) {
			iBestCount = iLocalCount;
			pCategory = GET_REF(pRegion->eaCategories[i]->hCategory);
		}
	}

	// Debug warning
	if (bIssueWarning && (iBestCount < eaSize(&eaGeos)) && (pPCCostume->eCostumeType != kPCCostumeType_Unrestricted)) {
		if (pCategory) {
			ErrorFilenamef(pPCCostume->pcFileName, "Costume '%s' has %d parts in region '%s' that are not in category '%s'\n",
				 pPCCostume->pcName, eaSize(&eaGeos) - iBestCount, pRegion->pcName, pCategory->pcName);
		} else {
			ErrorFilenamef(pPCCostume->pcFileName, "Costume '%s' has %d parts in region '%s' and none fit in a category\n",
				 pPCCostume->pcName,  eaSize(&eaGeos) - iBestCount, pRegion->pcName);
		}
	}

	eaDestroy(&eaGeos);
	eaDestroy(&eaCategories);

	return pCategory;
}


// Get the part (if any) that is the mirror of the provided part
NOCONST(PCPart) *costumeTailor_GetMirrorPart(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart)
{
	PCBoneDef *pBoneDef;
	int i;

	// Find the mirror bone's def
	pBoneDef = GET_REF(pPart->hBoneDef);
	if (!pBoneDef) {
		return NULL;
	}
	pBoneDef = GET_REF(pBoneDef->hMirrorBone);
	if (!pBoneDef) {
		return NULL;
	}

	// Find the part matching the bone
	for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
		if (GET_REF(pPCCostume->eaParts[i]->hBoneDef) == pBoneDef) {
			return pPCCostume->eaParts[i];
		}
	}

	return NULL;
}

void costumeTailor_CopyToMirrorPart(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, NOCONST(PCPart) *pMirrorPart, SpeciesDef *pSpecies, int eModeFlags, PlayerCostume **eaUnlockedCostumes, bool bSortDisplay, bool bUnlockAll, bool bSoftCopy)
{
	int eSoftModeFlags = 0;

	if (!pMirrorPart) {
		return;
	}

	// Check if geometry should be copied
	if (eModeFlags & EDIT_FLAG_GEOMETRY) {
		PCGeometryDef *pGeoDef;
		PCGeometryDef *pMirrorDef = NULL;

		// Get the mirror geometry
		pGeoDef = GET_REF(pPart->hGeoDef);
		if (pGeoDef) {
			PCGeometryDef **eaGeoDefs = NULL;
			PCCategory *pCat;
			int i;
			pCat = costumeTailor_GetCategoryForRegion((PlayerCostume*)pPCCostume, GET_REF(GET_REF(pPart->hBoneDef)->hRegion));
			costumeTailor_GetValidGeos(pPCCostume, GET_REF(pPCCostume->hSkeleton), GET_REF(pMirrorPart->hBoneDef), pCat, pSpecies, eaUnlockedCostumes, &eaGeoDefs, false, false, bSortDisplay, bUnlockAll);
			for(i=eaSize(&eaGeoDefs)-1; i>=0; --i) {
				if ((pGeoDef == eaGeoDefs[i]) || (pGeoDef->pcMirrorGeometry && (stricmp(pGeoDef->pcMirrorGeometry, eaGeoDefs[i]->pcName) == 0))) {
					// Found perfect match so done
					pMirrorDef = eaGeoDefs[i];
					break;
				} else if (stricmp(TranslateDisplayMessage(pGeoDef->displayNameMsg), TranslateDisplayMessage(eaGeoDefs[i]->displayNameMsg)) == 0) {
					// Candidate but continue looking for perfect one
					pMirrorDef = eaGeoDefs[i];
				}
			}
			eaDestroy(&eaGeoDefs);
		}
		if (!pMirrorDef) {
			pMirrorDef = RefSystem_ReferentFromString(g_hCostumeGeometryDict, "None");
		}

		// Apply the change if necessary
		if (pMirrorDef != GET_REF(pMirrorPart->hGeoDef)) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, pMirrorDef, pMirrorPart->hGeoDef);

			// If change the geo, force match on material and textures
			if (bSoftCopy) {
				// Only perform the soft copy on unset options
				eSoftModeFlags |= ~eModeFlags & (EDIT_FLAG_MATERIAL | EDIT_FLAG_TEXTURE1 | EDIT_FLAG_TEXTURE2 | EDIT_FLAG_TEXTURE3 | EDIT_FLAG_TEXTURE4 | EDIT_FLAG_TEXTURE5);
			}
			eModeFlags |= EDIT_FLAG_MATERIAL | EDIT_FLAG_TEXTURE1 | EDIT_FLAG_TEXTURE2 | EDIT_FLAG_TEXTURE3 | EDIT_FLAG_TEXTURE4 | EDIT_FLAG_TEXTURE5;
		}
	}

	// Check if material should be copied
	if (eModeFlags & EDIT_FLAG_MATERIAL) {
		PCMaterialDef *pMatDef;
		PCMaterialDef *pCurMatDef;
		PCMaterialDef *pMirrorDef = NULL;

		pMatDef = GET_REF(pPart->hMatDef);
		pCurMatDef = GET_REF(pMirrorPart->hMatDef);
		if (pMatDef) {
			PCMaterialDef **eaMatDefs = NULL;
			int i;
			costumeTailor_GetValidMaterials(pPCCostume, GET_REF(pMirrorPart->hGeoDef), pSpecies, NULL, NULL, eaUnlockedCostumes, &eaMatDefs, false, bSortDisplay, bUnlockAll);
			if ((eSoftModeFlags & EDIT_FLAG_MATERIAL) && pCurMatDef) {
				for(i=eaSize(&eaMatDefs)-1; i>=0; --i) {
					if (stricmp(pCurMatDef->pcName, eaMatDefs[i]->pcName) == 0) {
						// Found perfect match so done
						pMirrorDef = eaMatDefs[i];
						break;
					} else if (stricmp(TranslateDisplayMessage(pCurMatDef->displayNameMsg), TranslateDisplayMessage(eaMatDefs[i]->displayNameMsg)) == 0) {
						// Candidate but continue looking for perfect one
						pMirrorDef = eaMatDefs[i];
					}
				}
			}
			if (!pMirrorDef) {
				for(i=eaSize(&eaMatDefs)-1; i>=0; --i) {
					if (stricmp(pMatDef->pcName, eaMatDefs[i]->pcName) == 0) {
						// Found perfect match so done
						pMirrorDef = eaMatDefs[i];
						break;
					} else if (stricmp(TranslateDisplayMessage(pMatDef->displayNameMsg), TranslateDisplayMessage(eaMatDefs[i]->displayNameMsg)) == 0) {
						// Candidate but continue looking for perfect one
						pMirrorDef = eaMatDefs[i];
					}
				}
			}
			eaDestroy(&eaMatDefs);
		}
		if (!pMirrorDef) {
			pMirrorDef = RefSystem_ReferentFromString(g_hCostumeMaterialDict, "None");
		}
		if (pMirrorDef != GET_REF(pMirrorPart->hMatDef)) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, pMirrorDef, pMirrorPart->hMatDef);

			// If change the mat, force match on textures
			if (bSoftCopy) {
				eSoftModeFlags |= ~eModeFlags & (EDIT_FLAG_TEXTURE1 | EDIT_FLAG_TEXTURE2 | EDIT_FLAG_TEXTURE3 | EDIT_FLAG_TEXTURE4 | EDIT_FLAG_TEXTURE5);
			}
			eModeFlags |= EDIT_FLAG_TEXTURE1 | EDIT_FLAG_TEXTURE2 | EDIT_FLAG_TEXTURE3 | EDIT_FLAG_TEXTURE4 | EDIT_FLAG_TEXTURE5;
		}
	}

	// Check if texture 1 should be copied
	if (eModeFlags & EDIT_FLAG_TEXTURE1) {
		PCTextureDef *pTexDef;
		PCTextureDef *pCurTexDef;
		PCTextureDef *pMirrorDef = NULL;

		pTexDef = GET_REF(pPart->hPatternTexture);
		pCurTexDef = GET_REF(pMirrorPart->hPatternTexture);
		if (pTexDef) {
			PCTextureDef **eaTexDefs = NULL;
			int i;
			costumeTailor_GetValidTextures(pPCCostume, GET_REF(pMirrorPart->hMatDef), pSpecies, NULL, NULL, GET_REF(pMirrorPart->hGeoDef), NULL, eaUnlockedCostumes,
									kPCTextureType_Pattern, &eaTexDefs, false, bSortDisplay, bUnlockAll);
			if ((eSoftModeFlags & EDIT_FLAG_TEXTURE1) && pCurTexDef) {
				for(i=eaSize(&eaTexDefs)-1; i>=0; --i) {
					if (stricmp(pCurTexDef->pcName, eaTexDefs[i]->pcName) == 0) {
						// Found perfect match so done
						pMirrorDef = eaTexDefs[i];
						break;
					} else if (stricmp(TranslateDisplayMessage(pCurTexDef->displayNameMsg), TranslateDisplayMessage(eaTexDefs[i]->displayNameMsg)) == 0) {
						// Candidate but continue looking for perfect one
						pMirrorDef = eaTexDefs[i];
					}
				}
			}
			if (!pMirrorDef) {
				for(i=eaSize(&eaTexDefs)-1; i>=0; --i) {
					if (stricmp(pTexDef->pcName, eaTexDefs[i]->pcName) == 0) {
						// Found perfect match so done
						pMirrorDef = eaTexDefs[i];
						break;
					} else if (stricmp(TranslateDisplayMessage(pTexDef->displayNameMsg), TranslateDisplayMessage(eaTexDefs[i]->displayNameMsg)) == 0) {
						// Candidate but continue looking for perfect one
						pMirrorDef = eaTexDefs[i];
					}
				}
			}
			eaDestroy(&eaTexDefs);
		}
		if (!pMirrorDef) {
			pMirrorDef = RefSystem_ReferentFromString(g_hCostumeTextureDict, "None");
		}
		if (pMirrorDef != GET_REF(pMirrorPart->hPatternTexture)) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pMirrorDef, pMirrorPart->hPatternTexture);
		}
	}

	// Check if texture 2 should be copied
	if (eModeFlags & EDIT_FLAG_TEXTURE2) {
		PCTextureDef *pTexDef;
		PCTextureDef *pCurTexDef;
		PCTextureDef *pMirrorDef = NULL;

		pTexDef = GET_REF(pPart->hDetailTexture);
		pCurTexDef = GET_REF(pMirrorPart->hDetailTexture);
		if (pTexDef) {
			PCTextureDef **eaTexDefs = NULL;
			int i;
			costumeTailor_GetValidTextures(pPCCostume, GET_REF(pMirrorPart->hMatDef), pSpecies, NULL, NULL, GET_REF(pMirrorPart->hGeoDef), NULL, eaUnlockedCostumes,
									kPCTextureType_Detail, &eaTexDefs, false, bSortDisplay, bUnlockAll);
			if ((eSoftModeFlags & EDIT_FLAG_TEXTURE2) && pCurTexDef) {
				for(i=eaSize(&eaTexDefs)-1; i>=0; --i) {
					if (stricmp(pCurTexDef->pcName, eaTexDefs[i]->pcName) == 0) {
						// Found perfect match so done
						pMirrorDef = eaTexDefs[i];
						break;
					} else if (stricmp(TranslateDisplayMessage(pCurTexDef->displayNameMsg), TranslateDisplayMessage(eaTexDefs[i]->displayNameMsg)) == 0) {
						// Candidate but continue looking for perfect one
						pMirrorDef = eaTexDefs[i];
					}
				}
			}
			if (!pMirrorDef) {
				for(i=eaSize(&eaTexDefs)-1; i>=0; --i) {
					if (stricmp(pTexDef->pcName, eaTexDefs[i]->pcName) == 0) {
						// Found perfect match so done
						pMirrorDef = eaTexDefs[i];
						break;
					} else if (stricmp(TranslateDisplayMessage(pTexDef->displayNameMsg), TranslateDisplayMessage(eaTexDefs[i]->displayNameMsg)) == 0) {
						// Candidate but continue looking for perfect one
						pMirrorDef = eaTexDefs[i];
					}
				}
			}
			eaDestroy(&eaTexDefs);
		}
		if (!pMirrorDef) {
			pMirrorDef = RefSystem_ReferentFromString(g_hCostumeTextureDict, "None");
		}
		if (pMirrorDef != GET_REF(pMirrorPart->hDetailTexture)) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pMirrorDef, pMirrorPart->hDetailTexture);
		}
	}

	// Check if texture 3 should be copied
	if (eModeFlags & EDIT_FLAG_TEXTURE3) {
		PCTextureDef *pTexDef;
		PCTextureDef *pCurTexDef;
		PCTextureDef *pMirrorDef = NULL;

		pTexDef = GET_REF(pPart->hSpecularTexture);
		pCurTexDef = GET_REF(pMirrorPart->hSpecularTexture);
		if (pTexDef) {
			PCTextureDef **eaTexDefs = NULL;
			int i;
			costumeTailor_GetValidTextures(pPCCostume, GET_REF(pMirrorPart->hMatDef), pSpecies, NULL, NULL, GET_REF(pMirrorPart->hGeoDef), NULL, eaUnlockedCostumes,
									kPCTextureType_Specular, &eaTexDefs, false, bSortDisplay, bUnlockAll);
			if ((eSoftModeFlags & EDIT_FLAG_TEXTURE3) && pCurTexDef) {
				for(i=eaSize(&eaTexDefs)-1; i>=0; --i) {
					if (stricmp(pCurTexDef->pcName, eaTexDefs[i]->pcName) == 0) {
						// Found perfect match so done
						pMirrorDef = eaTexDefs[i];
						break;
					} else if (stricmp(TranslateDisplayMessage(pCurTexDef->displayNameMsg), TranslateDisplayMessage(eaTexDefs[i]->displayNameMsg)) == 0) {
						// Candidate but continue looking for perfect one
						pMirrorDef = eaTexDefs[i];
					}
				}
			}
			if (!pMirrorDef) {
				for(i=eaSize(&eaTexDefs)-1; i>=0; --i) {
					if (stricmp(pTexDef->pcName, eaTexDefs[i]->pcName) == 0) {
						// Found perfect match so done
						pMirrorDef = eaTexDefs[i];
						break;
					} else if (stricmp(TranslateDisplayMessage(pTexDef->displayNameMsg), TranslateDisplayMessage(eaTexDefs[i]->displayNameMsg)) == 0) {
						// Candidate but continue looking for perfect one
						pMirrorDef = eaTexDefs[i];
					}
				}
			}
			eaDestroy(&eaTexDefs);
		}
		if (!pMirrorDef) {
			pMirrorDef = RefSystem_ReferentFromString(g_hCostumeTextureDict, "None");
		}
		if (pMirrorDef != GET_REF(pMirrorPart->hSpecularTexture)) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pMirrorDef, pMirrorPart->hSpecularTexture);
		}
	}

	// Check if texture 4 should be copied
	if (eModeFlags & EDIT_FLAG_TEXTURE4) {
		PCTextureDef *pTexDef;
		PCTextureDef *pCurTexDef;
		PCTextureDef *pMirrorDef = NULL;

		pTexDef = GET_REF(pPart->hDiffuseTexture);
		pCurTexDef = GET_REF(pMirrorPart->hDiffuseTexture);
		if (pTexDef) {
			PCTextureDef **eaTexDefs = NULL;
			int i;
			costumeTailor_GetValidTextures(pPCCostume, GET_REF(pMirrorPart->hMatDef), pSpecies, NULL, NULL, GET_REF(pMirrorPart->hGeoDef), NULL, eaUnlockedCostumes,
									kPCTextureType_Diffuse, &eaTexDefs, false, bSortDisplay, bUnlockAll);
			if ((eSoftModeFlags & EDIT_FLAG_TEXTURE4) && pCurTexDef) {
				for(i=eaSize(&eaTexDefs)-1; i>=0; --i) {
					if (stricmp(pCurTexDef->pcName, eaTexDefs[i]->pcName) == 0) {
						// Found perfect match so done
						pMirrorDef = eaTexDefs[i];
						break;
					} else if (stricmp(TranslateDisplayMessage(pCurTexDef->displayNameMsg), TranslateDisplayMessage(eaTexDefs[i]->displayNameMsg)) == 0) {
						// Candidate but continue looking for perfect one
						pMirrorDef = eaTexDefs[i];
					}
				}
			}
			if (!pMirrorDef) {
				for(i=eaSize(&eaTexDefs)-1; i>=0; --i) {
					if (stricmp(pTexDef->pcName, eaTexDefs[i]->pcName) == 0) {
						// Found perfect match so done
						pMirrorDef = eaTexDefs[i];
						break;
					} else if (stricmp(TranslateDisplayMessage(pTexDef->displayNameMsg), TranslateDisplayMessage(eaTexDefs[i]->displayNameMsg)) == 0) {
						// Candidate but continue looking for perfect one
						pMirrorDef = eaTexDefs[i];
					}
				}
			}
			eaDestroy(&eaTexDefs);
		}
		if (!pMirrorDef) {
			pMirrorDef = RefSystem_ReferentFromString(g_hCostumeTextureDict, "None");
		}
		if (pMirrorDef != GET_REF(pMirrorPart->hDiffuseTexture)) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pMirrorDef, pMirrorPart->hDiffuseTexture);
		}
	}

	// Check if texture 5 should be copied
	if (eModeFlags & EDIT_FLAG_TEXTURE5) {
		PCTextureDef *pTexDef;
		PCTextureDef *pCurTexDef;
		PCTextureDef *pMirrorDef = NULL;

		if(pPart->pMovableTexture)
		{
			pTexDef = GET_REF(pPart->pMovableTexture->hMovableTexture);
			if (pMirrorPart->pMovableTexture) {
				pCurTexDef = GET_REF(pMirrorPart->pMovableTexture->hMovableTexture);
			} else {
				pCurTexDef = NULL;
			}
			if (pTexDef) {
				PCTextureDef **eaTexDefs = NULL;
				int i;
				costumeTailor_GetValidTextures(pPCCostume, GET_REF(pMirrorPart->hMatDef), pSpecies, NULL, NULL, GET_REF(pMirrorPart->hGeoDef), NULL, eaUnlockedCostumes,
					kPCTextureType_Movable, &eaTexDefs, false, bSortDisplay, bUnlockAll);
				if ((eSoftModeFlags & EDIT_FLAG_TEXTURE5) && pCurTexDef) {
					for(i=eaSize(&eaTexDefs)-1; i>=0; --i) {
						if (stricmp(pCurTexDef->pcName, eaTexDefs[i]->pcName) == 0) {
							// Found perfect match so done
							pMirrorDef = eaTexDefs[i];
							break;
						} else if (stricmp(TranslateDisplayMessage(pCurTexDef->displayNameMsg), TranslateDisplayMessage(eaTexDefs[i]->displayNameMsg)) == 0) {
							// Candidate but continue looking for perfect one
							pMirrorDef = eaTexDefs[i];
						}
					}
				}
				if (!pMirrorDef) {
					for(i=eaSize(&eaTexDefs)-1; i>=0; --i) {
						if (stricmp(pTexDef->pcName, eaTexDefs[i]->pcName) == 0) {
							// Found perfect match so done
							pMirrorDef = eaTexDefs[i];
							break;
						} else if (stricmp(TranslateDisplayMessage(pTexDef->displayNameMsg), TranslateDisplayMessage(eaTexDefs[i]->displayNameMsg)) == 0) {
							// Candidate but continue looking for perfect one
							pMirrorDef = eaTexDefs[i];
						}
					}
				}
				eaDestroy(&eaTexDefs);
			}
			if (!pMirrorDef) {
				pMirrorDef = RefSystem_ReferentFromString(g_hCostumeTextureDict, "None");
			}
			if (!pMirrorPart->pMovableTexture) {
				pMirrorPart->pMovableTexture = StructCloneNoConst(parse_PCMovableTextureInfo, pPart->pMovableTexture);
			}
			if (pMirrorPart->pMovableTexture && pMirrorDef != GET_REF(pMirrorPart->pMovableTexture->hMovableTexture)) {
				SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pMirrorDef, pMirrorPart->pMovableTexture->hMovableTexture);
			}
		}
	}
}

bool costumeTailor_CopyToGroupPart(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, NOCONST(PCPart) *pGroupPart, SpeciesDef *pSpecies, int eModeFlags, PlayerCostume **eaUnlockedCostumes, bool bSortDisplay, bool bUnlockAll, bool bCopyUnmatched, bool bSoftCopy)
{
	bool bFound = false;
	int eSoftModeFlags = 0;

	if (!pGroupPart) {
		return false;
	}
	if (pPart == pGroupPart) {
		return false;
	}

	// Check if geometry should be copied
	if (eModeFlags & EDIT_FLAG_GEOMETRY) {
		PCGeometryDef *pGeoDef;
		PCGeometryDef *pGroupDef = NULL;

		// Get the mirror geometry
		pGeoDef = GET_REF(pPart->hGeoDef);
		if (pGeoDef) {
			PCGeometryDef **eaGeoDefs = NULL;
			PCCategory *pCat;
			int i;
			pCat = costumeTailor_GetCategoryForRegion((PlayerCostume*)pPCCostume, GET_REF(GET_REF(pPart->hBoneDef)->hRegion));
			costumeTailor_GetValidGeos(pPCCostume, GET_REF(pPCCostume->hSkeleton), GET_REF(pGroupPart->hBoneDef), pCat, pSpecies, eaUnlockedCostumes, &eaGeoDefs, false, false, bSortDisplay, bUnlockAll);
			i = costumeTailor_GetMatchingGeoIndex(pGeoDef, eaGeoDefs);
			if (i >= 0)
			{
				pGroupDef = eaGeoDefs[i];
				bFound = true;
			}
			eaDestroy(&eaGeoDefs);
		}

		if (bCopyUnmatched && !pGroupDef) {
			pGroupDef = RefSystem_ReferentFromString(g_hCostumeGeometryDict, "None");
		}

		// Apply the change if necessary
		if (pGroupDef && pGroupDef != GET_REF(pGroupPart->hGeoDef)) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, pGroupDef, pGroupPart->hGeoDef);

			// If change the geo, force match on material and textures
			if (bSoftCopy) {
				// Only perform the soft copy on unset flags
				eSoftModeFlags |= ~eModeFlags & (EDIT_FLAG_MATERIAL | EDIT_FLAG_TEXTURE1 | EDIT_FLAG_TEXTURE2 | EDIT_FLAG_TEXTURE3 | EDIT_FLAG_TEXTURE4 | EDIT_FLAG_TEXTURE5);
			}
			eModeFlags |= EDIT_FLAG_MATERIAL | EDIT_FLAG_TEXTURE1 | EDIT_FLAG_TEXTURE2 | EDIT_FLAG_TEXTURE3 | EDIT_FLAG_TEXTURE4 | EDIT_FLAG_TEXTURE5;
		}
	}

	// Check if material should be copied
	if (eModeFlags & EDIT_FLAG_MATERIAL) {
		PCMaterialDef *pMatDef;
		PCMaterialDef *pGroupDef = NULL;

		pMatDef = GET_REF(pPart->hMatDef);
		if (pMatDef) {
			PCMaterialDef **eaMatDefs = NULL;
			int i = -1;
			costumeTailor_GetValidMaterials(pPCCostume, GET_REF(pGroupPart->hGeoDef), pSpecies, NULL, NULL, eaUnlockedCostumes, &eaMatDefs, false, bSortDisplay, bUnlockAll);
			if ((eSoftModeFlags & EDIT_FLAG_MATERIAL) && GET_REF(pGroupPart->hMatDef)) {
				i = costumeTailor_GetMatchingMatIndex(GET_REF(pGroupPart->hMatDef), eaMatDefs);
			}
			if (i < 0) {
				i = costumeTailor_GetMatchingMatIndex(pMatDef, eaMatDefs);
			}
			if (i >= 0)
			{
				pGroupDef = eaMatDefs[i];
				bFound = true;
			}
			eaDestroy(&eaMatDefs);
		}
		if (bCopyUnmatched && !pGroupDef) {
			pGroupDef = RefSystem_ReferentFromString(g_hCostumeMaterialDict, "None");
		}
		if (pGroupDef && pGroupDef != GET_REF(pGroupPart->hMatDef)) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, pGroupDef, pGroupPart->hMatDef);

			// If change the mat, force match on textures
			if (bSoftCopy) {
				eSoftModeFlags |= ~eModeFlags & (EDIT_FLAG_TEXTURE1 | EDIT_FLAG_TEXTURE2 | EDIT_FLAG_TEXTURE3 | EDIT_FLAG_TEXTURE4 | EDIT_FLAG_TEXTURE5);
			}
			eModeFlags |= EDIT_FLAG_TEXTURE1 | EDIT_FLAG_TEXTURE2 | EDIT_FLAG_TEXTURE3 | EDIT_FLAG_TEXTURE4 | EDIT_FLAG_TEXTURE5;
		}
	}

	// Check if texture 1 should be copied
	if (eModeFlags & EDIT_FLAG_TEXTURE1) {
		PCTextureDef *pTexDef;
		PCTextureDef *pGroupDef = NULL;

		pTexDef = GET_REF(pPart->hPatternTexture);
		if (pTexDef) {
			PCTextureDef **eaTexDefs = NULL;
			int i = -1;
			costumeTailor_GetValidTextures(pPCCostume, GET_REF(pGroupPart->hMatDef), pSpecies, NULL, NULL, GET_REF(pGroupPart->hGeoDef), NULL, eaUnlockedCostumes,
				kPCTextureType_Pattern, &eaTexDefs, false, bSortDisplay, bUnlockAll);
			if ((eSoftModeFlags & EDIT_FLAG_TEXTURE1) && GET_REF(pGroupPart->hPatternTexture)) {
				i = costumeTailor_GetMatchingTexIndex(GET_REF(pGroupPart->hPatternTexture), eaTexDefs);
			}
			if (i < 0) {
				i = costumeTailor_GetMatchingTexIndex(pTexDef, eaTexDefs);
			}
			if (i >= 0)
			{
				pGroupDef = eaTexDefs[i];
				bFound = true;
			}
			eaDestroy(&eaTexDefs);
		}
		if (bCopyUnmatched && !pGroupDef) {
			pGroupDef = RefSystem_ReferentFromString(g_hCostumeTextureDict, "None");
		}
		if (pGroupDef && pGroupDef != GET_REF(pGroupPart->hPatternTexture)) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pGroupDef, pGroupPart->hPatternTexture);
		}
	}

	// Check if texture 2 should be copied
	if (eModeFlags & EDIT_FLAG_TEXTURE2) {
		PCTextureDef *pTexDef;
		PCTextureDef *pGroupDef = NULL;

		pTexDef = GET_REF(pPart->hDetailTexture);
		if (pTexDef) {
			PCTextureDef **eaTexDefs = NULL;
			int i = -1;
			costumeTailor_GetValidTextures(pPCCostume, GET_REF(pGroupPart->hMatDef), pSpecies, NULL, NULL, GET_REF(pGroupPart->hGeoDef), NULL, eaUnlockedCostumes,
				kPCTextureType_Detail, &eaTexDefs, false, bSortDisplay, bUnlockAll);
			if ((eSoftModeFlags & EDIT_FLAG_TEXTURE2) && GET_REF(pGroupPart->hDetailTexture)) {
				i = costumeTailor_GetMatchingTexIndex(GET_REF(pGroupPart->hDetailTexture), eaTexDefs);
			}
			if (i < 0) {
				i = costumeTailor_GetMatchingTexIndex(pTexDef, eaTexDefs);
			}
			if (i >= 0)
			{
				pGroupDef = eaTexDefs[i];
				bFound = true;
			}
			eaDestroy(&eaTexDefs);
		}
		if (bCopyUnmatched && !pGroupDef) {
			pGroupDef = RefSystem_ReferentFromString(g_hCostumeTextureDict, "None");
		}
		if (pGroupDef && pGroupDef != GET_REF(pGroupPart->hDetailTexture)) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pGroupDef, pGroupPart->hDetailTexture);
		}
	}

	// Check if texture 3 should be copied
	if (eModeFlags & EDIT_FLAG_TEXTURE3) {
		PCTextureDef *pTexDef;
		PCTextureDef *pGroupDef = NULL;

		pTexDef = GET_REF(pPart->hSpecularTexture);
		if (pTexDef) {
			PCTextureDef **eaTexDefs = NULL;
			int i = -1;
			costumeTailor_GetValidTextures(pPCCostume, GET_REF(pGroupPart->hMatDef), pSpecies, NULL, NULL, GET_REF(pGroupPart->hGeoDef), NULL, eaUnlockedCostumes,
				kPCTextureType_Specular, &eaTexDefs, false, bSortDisplay, bUnlockAll);
			if ((eSoftModeFlags & EDIT_FLAG_TEXTURE3) && GET_REF(pGroupPart->hSpecularTexture)) {
				i = costumeTailor_GetMatchingTexIndex(GET_REF(pGroupPart->hSpecularTexture), eaTexDefs);
			}
			if (i < 0) {
				i = costumeTailor_GetMatchingTexIndex(pTexDef, eaTexDefs);
			}
			if (i >= 0)
			{
				pGroupDef = eaTexDefs[i];
				bFound = true;
			}
			eaDestroy(&eaTexDefs);
		}
		if (bCopyUnmatched && !pGroupDef) {
			pGroupDef = RefSystem_ReferentFromString(g_hCostumeTextureDict, "None");
		}
		if (pGroupDef && pGroupDef != GET_REF(pGroupPart->hSpecularTexture)) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pGroupDef, pGroupPart->hSpecularTexture);
		}
	}

	// Check if texture 4 should be copied
	if (eModeFlags & EDIT_FLAG_TEXTURE4) {
		PCTextureDef *pTexDef;
		PCTextureDef *pGroupDef = NULL;

		pTexDef = GET_REF(pPart->hDiffuseTexture);
		if (pTexDef) {
			PCTextureDef **eaTexDefs = NULL;
			int i = -1;
			costumeTailor_GetValidTextures(pPCCostume, GET_REF(pGroupPart->hMatDef), pSpecies, NULL, NULL, GET_REF(pGroupPart->hGeoDef), NULL, eaUnlockedCostumes,
				kPCTextureType_Diffuse, &eaTexDefs, false, bSortDisplay, bUnlockAll);
			if ((eSoftModeFlags & EDIT_FLAG_TEXTURE4) && GET_REF(pGroupPart->hDiffuseTexture)) {
				i = costumeTailor_GetMatchingTexIndex(GET_REF(pGroupPart->hDiffuseTexture), eaTexDefs);
			}
			if (i < 0) {
				i = costumeTailor_GetMatchingTexIndex(pTexDef, eaTexDefs);
			}
			if (i >= 0)
			{
				pGroupDef = eaTexDefs[i];
				bFound = true;
			}
			eaDestroy(&eaTexDefs);
		}
		if (bCopyUnmatched && !pGroupDef) {
			pGroupDef = RefSystem_ReferentFromString(g_hCostumeTextureDict, "None");
		}
		if (pGroupDef && pGroupDef != GET_REF(pGroupPart->hDiffuseTexture)) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pGroupDef, pGroupPart->hDiffuseTexture);
		}
	}

	// Check if texture 5 should be copied
	if (eModeFlags & EDIT_FLAG_TEXTURE5) {
		PCTextureDef *pTexDef = NULL;
		PCTextureDef *pGroupDef = NULL;

		if (pPart->pMovableTexture) {
			pTexDef = GET_REF(pPart->pMovableTexture->hMovableTexture);
		}
		if (pTexDef) {
			PCTextureDef **eaTexDefs = NULL;
			int i = -1;
			costumeTailor_GetValidTextures(pPCCostume, GET_REF(pGroupPart->hMatDef), pSpecies, NULL, NULL, GET_REF(pGroupPart->hGeoDef), NULL, eaUnlockedCostumes,
				kPCTextureType_Movable, &eaTexDefs, false, bSortDisplay, bUnlockAll);
			if ((eSoftModeFlags & EDIT_FLAG_TEXTURE5) && pGroupPart->pMovableTexture && GET_REF(pGroupPart->pMovableTexture->hMovableTexture)) {
				i = costumeTailor_GetMatchingTexIndex(GET_REF(pGroupPart->pMovableTexture->hMovableTexture), eaTexDefs);
			}
			if (i < 0) {
				i = costumeTailor_GetMatchingTexIndex(pTexDef, eaTexDefs);
			}
			if (i >= 0)
			{
				pGroupDef = eaTexDefs[i];
				bFound = true;
			}
			eaDestroy(&eaTexDefs);
		}
		if (bCopyUnmatched && !pGroupDef) {
			pGroupDef = RefSystem_ReferentFromString(g_hCostumeTextureDict, "None");
		}
		if (pGroupPart->pMovableTexture && pGroupDef && pGroupDef != GET_REF(pGroupPart->pMovableTexture->hMovableTexture)) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pGroupDef, pGroupPart->pMovableTexture->hMovableTexture);
		}
	}

	return bFound;
}


// Determine if geometries count as mirrored
bool costumeTailor_IsMirrorGeometry(PCGeometryDef *pGeo1, PCGeometryDef *pGeo2)
{
	if (pGeo1->pcMirrorGeometry) {
		PCGeometryDef *pDef = RefSystem_ReferentFromString(g_hCostumeGeometryDict, pGeo1->pcMirrorGeometry);
		if (pDef == pGeo2) {
			return true;
		} else if (stricmp(TranslateDisplayMessage(pGeo1->displayNameMsg), TranslateDisplayMessage(pGeo2->displayNameMsg)) == 0) {
			return true;
		}
	}
	return false;
}


// Determines if materials count as mirrored
bool costumeTailor_IsMirrorMaterial(PCMaterialDef *pMat1, PCMaterialDef *pMat2)
{
	if (pMat1 == pMat2) {
		return true;
	} else if (stricmp(TranslateDisplayMessage(pMat1->displayNameMsg), TranslateDisplayMessage(pMat2->displayNameMsg)) == 0) {
		return true;
	}
	return false;
}


bool costumeValidate_AreRestrictionsValid(NOCONST(PlayerCostume) *pPCCostume, PlayerCostume **ppCostumes, bool bAlwaysUnlock, char **ppEStringError, PCBoneRef ***peaBonesUsed)
{
	PCSkeletonDef *pSkeleton;
	NOCONST(PCPart) *pPart;
	PCBoneDef *pBone;
	PCGeometryDef *pGeometry;
	PCMaterialDef *pMaterial;
	PCTextureDef *pTexture;
	int i;
	bool bRetVal = true;

	pSkeleton = GET_REF(pPCCostume->hSkeleton);
	if (pSkeleton && !costumeTailor_TestRestriction(pSkeleton->eRestriction, true, (PlayerCostume*)pPCCostume))
	{
		if(!costumeValidate_PlayerCreatedError((PlayerCostume*)pPCCostume, ppEStringError, "Skeleton is restricted", NULL))
		{
			return false;
		}
		bRetVal = false;
	}

	for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
		pPart = pPCCostume->eaParts[i];

		pBone = GET_REF(pPart->hBoneDef);

		// Restrictions must pass on all bones, not just the "bones used"
		// This is so the costume doesn't end up containing illegal or unexpected data

		if (pBone && !costumeTailor_TestRestriction(pBone->eRestriction, true, (PlayerCostume*)pPCCostume))
		{
			if(!costumeValidate_PlayerCreatedError((PlayerCostume*)pPCCostume, ppEStringError, "hBoneDef is restricted", (PCPart *)pPart))
			{
				return false;
			}
			bRetVal = false;
		}
		pGeometry = GET_REF(pPart->hGeoDef);
		if (pGeometry && !costumeTestRestrictionEfficiently(pGeometry->eRestriction, bAlwaysUnlock?bAlwaysUnlock : costumeTailor_GeometryIsUnlocked(ppCostumes,pGeometry), (PlayerCostume*)pPCCostume))
		{
			if(!costumeValidate_PlayerCreatedError((PlayerCostume*)pPCCostume, ppEStringError, "hGeoDef is restricted", (PCPart *)pPart))
			{
				return false;
			}
			bRetVal = false;
		}
		pMaterial = GET_REF(pPart->hMatDef);
		if (pMaterial && !costumeTestRestrictionEfficiently(pMaterial->eRestriction, bAlwaysUnlock?bAlwaysUnlock : costumeTailor_MaterialIsUnlocked(ppCostumes,pMaterial,pGeometry), (PlayerCostume*)pPCCostume))
		{
			if(!costumeValidate_PlayerCreatedError((PlayerCostume*)pPCCostume, ppEStringError, "hMatDef is restricted", (PCPart *)pPart))
			{
				return false;
			}
			bRetVal = false;
		}
		pTexture = GET_REF(pPart->hPatternTexture);
		if (pTexture && !costumeTestRestrictionEfficiently(pTexture->eRestriction, bAlwaysUnlock?bAlwaysUnlock : costumeTailor_TextureIsUnlocked(ppCostumes,pTexture,pMaterial,pGeometry), (PlayerCostume*)pPCCostume))

		{
			if(!costumeValidate_PlayerCreatedError((PlayerCostume*)pPCCostume, ppEStringError, "hPatternTexture is restricted", (PCPart *)pPart))
			{
				return false;
			}
			bRetVal = false;
		}
		pTexture = GET_REF(pPart->hDetailTexture);
		if (pTexture && !costumeTestRestrictionEfficiently(pTexture->eRestriction, bAlwaysUnlock?bAlwaysUnlock : costumeTailor_TextureIsUnlocked(ppCostumes,pTexture,pMaterial,pGeometry), (PlayerCostume*)pPCCostume))
		{
			if(!costumeValidate_PlayerCreatedError((PlayerCostume*)pPCCostume, ppEStringError, "hDetailTexture is restricted", (PCPart *)pPart))
			{
				return false;
			}
			bRetVal = false;
		}
		pTexture = GET_REF(pPart->hSpecularTexture);
		if (pTexture && !costumeTestRestrictionEfficiently(pTexture->eRestriction, bAlwaysUnlock?bAlwaysUnlock : costumeTailor_TextureIsUnlocked(ppCostumes,pTexture,pMaterial,pGeometry), (PlayerCostume*)pPCCostume))
		{
			if(!costumeValidate_PlayerCreatedError((PlayerCostume*)pPCCostume, ppEStringError, "hSpecularTexture is restricted", (PCPart *)pPart))
			{
				return false;
			}
			bRetVal = false;
		}
		pTexture = GET_REF(pPart->hDiffuseTexture);
		if (pTexture && !costumeTestRestrictionEfficiently(pTexture->eRestriction, bAlwaysUnlock?bAlwaysUnlock : costumeTailor_TextureIsUnlocked(ppCostumes,pTexture,pMaterial,pGeometry), (PlayerCostume*)pPCCostume))
		{
			if(!costumeValidate_PlayerCreatedError((PlayerCostume*)pPCCostume, ppEStringError, "hDiffuseTexture is restricted", (PCPart *)pPart))
			{
				return false;
			}
			bRetVal = false;
		}
		if (pPart->pMovableTexture) {
			pTexture = GET_REF(pPart->pMovableTexture->hMovableTexture);
			if (pTexture && !costumeTestRestrictionEfficiently(pTexture->eRestriction, bAlwaysUnlock?bAlwaysUnlock : costumeTailor_TextureIsUnlocked(ppCostumes,pTexture,pMaterial,pGeometry), (PlayerCostume*)pPCCostume))
			{
				if(!costumeValidate_PlayerCreatedError((PlayerCostume*)pPCCostume, ppEStringError, "hMovableTexture is restricted", (PCPart *)pPart))
				{
					return false;
				}
				bRetVal = false;
			}
		}
	}

	return bRetVal;
}


// Returns true if category choices are valid, false otherwise.
// Assigns a category if the costume currently has none for a region.
bool costumeValidate_AreCategoriesValid(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, PCSlotType *pSlotType, bool bCreateIfNeeded)
{
	PCSkeletonDef *pSkeleton;
	PCRegion *pRegion;
	int i,j,k;
	bool bResult = true;

	pSkeleton = GET_REF(pPCCostume->hSkeleton);
	if (!pSkeleton) {
		return false;
	}

	for(i=eaSize(&pSkeleton->eaRegions)-1; i>=0; --i) {
		PCCategory *pCategory = costumeTailor_GetCategoryForRegion((PlayerCostume*)pPCCostume, GET_REF(pSkeleton->eaRegions[i]->hRegion));
		bool bGood = false;

		// Get the region
		pRegion = GET_REF(pSkeleton->eaRegions[i]->hRegion);
		if (!pRegion) {
			continue;
		}

		// See if current choice is legal for the region
		if (pCategory) {
			for(j=eaSize(&pRegion->eaCategories)-1; j>=0; --j) {
				if (GET_REF(pRegion->eaCategories[j]->hCategory) == pCategory) {
					bGood = true; // Current choice is legal
					break;
				}
			}
		}

		// If no category for the region, make an entry
		if (!pCategory && bCreateIfNeeded) {
			pCategory = costumeTailor_PickBestCategory((PlayerCostume*)pPCCostume, GET_REF(pSkeleton->eaRegions[i]->hRegion), pSpecies, pSlotType, false);
			if (pCategory) {
				NOCONST(PCRegionCategory) *pRegCat;
				pRegCat = StructCreateNoConst(parse_PCRegionCategory);
				SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, pRegion, pRegCat->hRegion);
				SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, pCategory, pRegCat->hCategory);
				eaPush(&pPCCostume->eaRegionCategories, pRegCat);
				bGood = true;
			}
		}

		// Give up on this region if no match
		if (!bGood) {
			bResult = false;
			continue;
		}

		// Now see if all geos are legal in this category
		// Find the geos for all parts in the region
		for(j=eaSize(&pPCCostume->eaParts)-1; j>=0 && bGood; --j) {
			PCBoneDef *pBone;
			NOCONST(PCPart) *pPart;
			PCGeometryDef *pGeo;

			// Collect Info
			pPart = pPCCostume->eaParts[j];
			pBone = GET_REF(pPart->hBoneDef);
			if (!pBone) {
				continue;
			}
			if (GET_REF(pBone->hRegion) != pRegion) {
				continue; // Not in region
			}

			pGeo = GET_REF(pPart->hGeoDef);
			if (pGeo) {
				// Make sure the category is defined for this geo
				bGood = false;
				for(k=eaSize(&pGeo->eaCategories)-1; k>=0; --k) {
					if (pCategory == GET_REF(pGeo->eaCategories[k]->hCategory)) {
						bGood = true;
						break;
					}
				}
				if (!bGood) {
					bResult = false;
				}
			}
		}
	}

	return bResult;
}

// Fills the earray with the legal set of costume from a costume set
// Applies restrictions based on the costume
void costumeTailor_GetValidCostumesFromSet(PCCostumeSet *pCostumeSet, SpeciesDef *pSpecies, CostumeRefForSet ***peaCostumes, bool bSortDisplay, bool bLooseRestrict)
{
	int i;

	eaClear(peaCostumes);
	for(i=0; i<eaSize(&pCostumeSet->eaPlayerCostumes); ++i) {
		CostumeRefForSet *pDef = pCostumeSet->eaPlayerCostumes[i];
		PlayerCostume *pCostume = GET_REF(pDef->hPlayerCostume);
		if (!pCostume) continue;
		if ((!pSpecies) || GET_REF(pCostume->hSpecies) == pSpecies) {
			eaPush(peaCostumes, pDef);
		}
	}
	if (pSpecies && bLooseRestrict && !eaSize(peaCostumes))
	{
		//Look for costumes of common gender
		for(i=0; i<eaSize(&pCostumeSet->eaPlayerCostumes); ++i) {
			CostumeRefForSet *pDef = pCostumeSet->eaPlayerCostumes[i];
			PlayerCostume *pCostume = GET_REF(pDef->hPlayerCostume);
			if ((!pCostume) || (!GET_REF(pCostume->hSpecies))) continue;
			if (GET_REF(pCostume->hSpecies)->eGender == pSpecies->eGender) {
				eaPush(peaCostumes, pDef);
			}
		}
		if (!eaSize(peaCostumes))
		{
			//Okay just take tham all
			for(i=0; i<eaSize(&pCostumeSet->eaPlayerCostumes); ++i) {
				CostumeRefForSet *pDef = pCostumeSet->eaPlayerCostumes[i];
				PlayerCostume *pCostume = GET_REF(pDef->hPlayerCostume);
				if ((!pCostume) || (!GET_REF(pCostume->hSpecies))) continue;
				eaPush(peaCostumes, pDef);
			}
		}
	}
	costumeTailor_SortCostumesFromSet(*peaCostumes, bSortDisplay);
}

// Fills the earray with the legal set of skeletons for the costume
// Applies restrictions based on the costume
// If bSameSpecies is set, then it will match against all species with the same pcSpeciesName as the provided species
AUTO_TRANS_HELPER_SIMPLE;
void costumeTailor_GetValidSkeletons(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, PCSkeletonDef ***peaSkels, bool bSameSpecies, bool bSortDisplay)
{
	PCSkeletonDef *pDef;
	int i;

	eaClear(peaSkels);

	if (pSpecies) {
		if (bSameSpecies && pSpecies->pcSpeciesName) {
			DictionaryEArrayStruct *pSpeciesStruct = resDictGetEArrayStruct("Species");

			for (i=0; i<eaSize(&pSpeciesStruct->ppReferents); ++i) {
				SpeciesDef *pSpeciesDef = (SpeciesDef *)pSpeciesStruct->ppReferents[i];
				pDef = GET_REF(pSpeciesDef->hSkeleton);
				if (pDef && pSpeciesDef->pcSpeciesName
					&& !stricmp(pSpecies->pcSpeciesName, pSpeciesDef->pcSpeciesName)
					&& costumeTailor_TestRestriction(pDef->eRestriction, true, (PlayerCostume*)pPCCostume)) {
						eaPush(peaSkels, pDef);
				}
			}
		} else if (GET_REF(pSpecies->hSkeleton)) {
			pDef = GET_REF(pSpecies->hSkeleton);
			if (costumeTailor_TestRestriction(pDef->eRestriction, true, (PlayerCostume*)pPCCostume)) {
				eaPush(peaSkels, pDef);
			}
		}
	} else {
		DictionaryEArrayStruct *pSkelStruct = resDictGetEArrayStruct("CostumeSkeleton");

		for(i=0; i<eaSize(&pSkelStruct->ppReferents); ++i) {
			pDef = (PCSkeletonDef*)pSkelStruct->ppReferents[i];
			if (costumeTailor_TestRestriction(pDef->eRestriction, true, (PlayerCostume*)pPCCostume)) {
				eaPush(peaSkels, pDef);
			}
		}
	}

	costumeTailor_SortSkeletons(*peaSkels, bSortDisplay);
}


// Fills the earray with the legal set of species for the costume
// Applies restrictions based on the costume
void costumeTailor_GetValidSpecies(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef ***peaSpecies, bool bSortDisplay, bool bAllowCustom)
{
	DictionaryEArrayStruct *pSpeciesStruct = resDictGetEArrayStruct("Species");
	int i;

	eaClear(peaSpecies);
	for(i=0; i<eaSize(&pSpeciesStruct->ppReferents); ++i) {
		SpeciesDef *pDef = (SpeciesDef*)pSpeciesStruct->ppReferents[i];
		if (costumeTailor_TestRestriction(pDef->eRestriction, true, (PlayerCostume*)pPCCostume)) {
			if (GET_REF(pDef->hSkeleton) == GET_REF(pPCCostume->hSkeleton))
			{
				eaPush(peaSpecies, pDef);
			}
		}
	}
	costumeTailor_SortSpecies(*peaSpecies, bSortDisplay);
	if (bAllowCustom)
	{
		SpeciesDef *pDef = RefSystem_ReferentFromString(g_hSpeciesDict, "None");
		if (pDef) {
			eaInsert(peaSpecies, pDef, 0);
		}
	}
}

// Fills the earray with the legal set of costume presets for the costume
// Applies restrictions based on the costume
void costumeTailor_GetValidPresets(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, CostumePreset ***peaPresets, bool bSortDisplay, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll)
{
	int i, j;
	eaClear(peaPresets);
	if (pSpecies)
	{
		for (i = eaSize(&pSpecies->eaPresets)-1; i >=0 ; --i)
		{
			PlayerCostume *pc = pSpecies->eaPresets[i] ? GET_REF(pSpecies->eaPresets[i]->hCostume) : NULL;
			if (!pc) continue;
			if (!bUnlockAll)
			{
				for (j = eaSize(&pc->eaParts)-1; j >= 0; --j)
				{
					PCPart *pPart = pc->eaParts[j];
					PCGeometryDef *pGeometry;
					PCMaterialDef *pMaterial;
					PCTextureDef *pTexture;
					pGeometry = GET_REF(pPart->hGeoDef);
					if (pGeometry && !costumeTestRestrictionEfficiently(pGeometry->eRestriction, bUnlockAll?bUnlockAll : costumeTailor_GeometryIsUnlocked(eaUnlockedCostumes,pGeometry), (PlayerCostume*)pPCCostume))
					{
						break;
					}
					pMaterial = GET_REF(pPart->hMatDef);
					if (pMaterial && !costumeTestRestrictionEfficiently(pMaterial->eRestriction, bUnlockAll?bUnlockAll : costumeTailor_MaterialIsUnlocked(eaUnlockedCostumes,pMaterial,pGeometry), (PlayerCostume*)pPCCostume))
					{
						break;
					}
					pTexture = GET_REF(pPart->hPatternTexture);
					if (pTexture && !costumeTestRestrictionEfficiently(pTexture->eRestriction, bUnlockAll?bUnlockAll : costumeTailor_TextureIsUnlocked(eaUnlockedCostumes,pTexture,pMaterial,pGeometry), (PlayerCostume*)pPCCostume))
					{
						break;
					}
					pTexture = GET_REF(pPart->hDetailTexture);
					if (pTexture && !costumeTestRestrictionEfficiently(pTexture->eRestriction, bUnlockAll?bUnlockAll : costumeTailor_TextureIsUnlocked(eaUnlockedCostumes,pTexture,pMaterial,pGeometry), (PlayerCostume*)pPCCostume))
					{
						break;
					}
					pTexture = GET_REF(pPart->hSpecularTexture);
					if (pTexture && !costumeTestRestrictionEfficiently(pTexture->eRestriction, bUnlockAll?bUnlockAll : costumeTailor_TextureIsUnlocked(eaUnlockedCostumes,pTexture,pMaterial,pGeometry), (PlayerCostume*)pPCCostume))
					{
						break;
					}
					pTexture = GET_REF(pPart->hDiffuseTexture);
					if (pTexture && !costumeTailor_TestRestriction(pTexture->eRestriction, bUnlockAll?bUnlockAll : costumeTailor_TextureIsUnlocked(eaUnlockedCostumes,pTexture,pMaterial,pGeometry), (PlayerCostume*)pPCCostume))
					{
						break;
					}
					if (pPart->pMovableTexture) {
						pTexture = GET_REF(pPart->pMovableTexture->hMovableTexture);
						if (pTexture && !costumeTailor_TestRestriction(pTexture->eRestriction, bUnlockAll?bUnlockAll : costumeTailor_TextureIsUnlocked(eaUnlockedCostumes,pTexture,pMaterial,pGeometry), (PlayerCostume*)pPCCostume))
						{
							break;
						}
					}
				}
				if (j >= 0) continue;
			}
			eaPush(peaPresets,pSpecies->eaPresets[i]);
		}
		costumeTailor_SortPresets(*peaPresets, bSortDisplay);
	}
}

bool costumeTailor_StanceIsUnlocked(PlayerCostume **eaUnlockedCostumes, PCStanceInfo *pStanceDef);

// Fills the earray with the legal set of stances for the costume
// Applies restrictions based on the costume
void costumeTailor_GetValidStances(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, PCStanceInfo ***peaStances, bool bSortDisplay, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll)
{
	PCSkeletonDef *pSkel;
	int i;
	const PCSlotStanceStruct *pStance;

	eaClear(peaStances);
	pSkel = GET_REF(pPCCostume->hSkeleton);
	if (!pSkel) {
		return;
	}
	
	pStance = costumeTailor_GetStanceFromSlot(pSlotType, costumeTailor_GetGenderFromSpeciesOrCostume(pSpecies, (PlayerCostume *)pPCCostume));
	if(pStance)
	{
		for(i=0; i<eaSize(&pStance->eaStanceInfo); ++i) {
			bool bUnlocked = bUnlockAll ? true : costumeTailor_StanceIsUnlocked(eaUnlockedCostumes, pStance->eaStanceInfo[i]);
			if (costumeTailor_TestRestriction(pStance->eaStanceInfo[i]->eRestriction, bUnlocked, (PlayerCostume*)pPCCostume)) {
				eaPush(peaStances, pStance->eaStanceInfo[i]);
			}
		}
	}
	else if (pSpecies && eaSize(&pSpecies->eaStanceInfo))
	{
		for(i=0; i<eaSize(&pSpecies->eaStanceInfo); ++i) {
			bool bUnlocked = bUnlockAll ? true : costumeTailor_StanceIsUnlocked(eaUnlockedCostumes, pSpecies->eaStanceInfo[i]);
			if (costumeTailor_TestRestriction(pSpecies->eaStanceInfo[i]->eRestriction, bUnlocked, (PlayerCostume*)pPCCostume)) {
				eaPush(peaStances, pSpecies->eaStanceInfo[i]);
			}
		}
	}
	else
	{
		for(i=0; i<eaSize(&pSkel->eaStanceInfo); ++i) {
			bool bUnlocked = bUnlockAll ? true : costumeTailor_StanceIsUnlocked(eaUnlockedCostumes, pSkel->eaStanceInfo[i]);
			if (costumeTailor_TestRestriction(pSkel->eaStanceInfo[i]->eRestriction, bUnlocked, (PlayerCostume*)pPCCostume)) {
				eaPush(peaStances, pSkel->eaStanceInfo[i]);
			}
		}
	}
	costumeTailor_SortStances(*peaStances, bSortDisplay);
}


// Fills the earray with the legal set of moods
void costumeTailor_GetValidMoods(PCMood ***peaMoods, bool bSortDisplay)
{
	DictionaryEArrayStruct *pMoodStruct = resDictGetEArrayStruct("CostumeMood");
	int i;

	eaClear(peaMoods);
	for(i=0; i<eaSize(&pMoodStruct->ppReferents); ++i) {
		eaPush(peaMoods, (PCMood*)pMoodStruct->ppReferents[i]);
	}
	costumeTailor_SortMoods(*peaMoods, bSortDisplay);
}

// Fills the earray with the legal set of voices
void costumeTailor_GetValidVoices(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, PCVoice ***peaVoices, bool bSortDisplay, GameAccountDataExtract *pExtract)
{
	const char *pKeyValue;
	PCSkeletonDef *pSkel;
	int i;

	eaClear(peaVoices);

	pSkel = GET_REF(pPCCostume->hSkeleton);
	if (!pSkel) {
		return;
	}

	if (pSpecies && (pSpecies->bAllowAllVoices == false || eaSize(&pSpecies->eaAllowedVoices)))
	{
		for(i=0; i<eaSize(&pSpecies->eaAllowedVoices); ++i) {
			PCVoice *voice = GET_REF(pSpecies->eaAllowedVoices[i]->hVoice);
			if (!voice) continue;
			if (pExtract && voice->pcUnlockCode && *voice->pcUnlockCode)
			{
				pKeyValue = gad_GetAttribStringFromExtract(pExtract, voice->pcUnlockCode);
				if((!pKeyValue) || atoi(pKeyValue) <= 0)
				{
					continue;
				}
			}
			if (costumeTailor_TestRestriction(voice->eRestriction, true, (PlayerCostume*)pPCCostume) &&
				pSpecies->eGender == voice->eGender)
			{
				eaPush(peaVoices, voice);
			}
		}
	}
	else
	{
		DictionaryEArrayStruct *pVoiceStruct = resDictGetEArrayStruct("CostumeVoice");
		for(i=0; i<eaSize(&pVoiceStruct->ppReferents); ++i) {
			PCVoice *voice = (PCVoice*)pVoiceStruct->ppReferents[i];
			Gender g = pPCCostume->eGender;
			if (!voice) continue;
			if (pExtract && voice->pcUnlockCode && *voice->pcUnlockCode)
			{
				pKeyValue = gad_GetAttribStringFromExtract(pExtract, voice->pcUnlockCode);
				if((!pKeyValue) || atoi(pKeyValue) <= 0)
				{
					continue;
				}
			}
			if (g != Gender_Female && g != Gender_Male)
			{
				PCSkeletonDef *skel =  GET_REF(pPCCostume->hSkeleton);
				if (skel)
				{
					g = skel->eGender;
				}
			}
			if (costumeTailor_TestRestriction(voice->eRestriction, true, (PlayerCostume*)pPCCostume) &&
				g == voice->eGender)
			{
				eaPush(peaVoices, voice);
			}
		}
	}
	costumeTailor_SortVoices(*peaVoices, bSortDisplay);
}


// Fills the earray with the legal set of body scales for the costume
// Applies restrictions based on the costume
void costumeTailor_GetValidBodyScales(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, PCBodyScaleInfo ***peaBodyScales, bool bSortDisplay)
{
	PCSkeletonDef *pSkel;
	int i;

	eaClear(peaBodyScales);
	pSkel = GET_REF(pPCCostume->hSkeleton);
	if (!pSkel) {
		return;
	}
	for(i=0; i<eaSize(&pSkel->eaBodyScaleInfo); ++i) {
		if (costumeTailor_TestRestriction(pSkel->eaBodyScaleInfo[i]->eRestriction, true, (PlayerCostume*)pPCCostume)) {
			eaPush(peaBodyScales, pSkel->eaBodyScaleInfo[i]);
		}
	}
	costumeTailor_SortBodyScales(*peaBodyScales, bSortDisplay);
}

int costumeTailor_GetMatchingBodyScaleIndex(NOCONST(PlayerCostume) *pPCCostume, PCBodyScaleInfo *pScale) {
	PCSkeletonDef *pSkel;
	int i;

	pSkel = GET_REF(pPCCostume->hSkeleton);
	if (!pSkel) {
		return -1;
	}
	for(i=0; i<eaSize(&pSkel->eaBodyScaleInfo); ++i) {
		if (pSkel->eaBodyScaleInfo[i] == pScale) {
			return i;
		}
	}
	return -1;
}


// Fills the earray with the legal set of regions for the costume
// Applies restrictions based on the costume
void costumeTailor_GetValidRegions(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCSlotType *pSlotType, PCRegion ***peaRegions, U32 uCGVFlags)
{
	PCSkeletonDef *pSkel;
	PCRegion *pRegion;
	PCCategory **eaCategories = NULL;
	int i;

	eaClear(peaRegions);

	pSkel = GET_REF(pPCCostume->hSkeleton);
	if (!pSkel) {
		return;
	}
	for(i=0; i<eaSize(&pSkel->eaRegions); ++i) {
		pRegion = GET_REF(pSkel->eaRegions[i]->hRegion);
		if (pRegion) {
			if((uCGVFlags & CGVF_OMIT_EMPTY))
			{
				// This could be a new costumeRegionHasValidCategories() function, but for now this is all we need
				costumeTailor_GetValidCategories(pPCCostume,pRegion, pSpecies,eaUnlockedCostumes,eaPowerFXBones,pSlotType,&eaCategories,uCGVFlags);
				if(eaSize(&eaCategories)<=0)
					continue;
			}
			eaPush(peaRegions, pRegion);
		}
	}
	eaDestroy(&eaCategories);
	costumeTailor_SortRegions(*peaRegions, !!(uCGVFlags & CGVF_SORT_DISPLAY));
}

static bool costumeTailor_BoneIsNotPowerFxLocked(NOCONST(PlayerCostume) *pPCCostume, PCBoneDef *pDef, const char **eaPowerFXBones, bool bUnlockAll)
{
	return (bUnlockAll || !pDef->bPowerFX || -1!=eaFind(&eaPowerFXBones,pDef->pcName) || costumeTailor_TestRestriction(0,true,(PlayerCostume*)pPCCostume));
}

static bool costumeTailor_TestValidGeoInSpecies(PCGeometryDef *pGeo, SpeciesDef *pSpecies)
{
	int j, k;
	if (!pSpecies) return true;
	if (!pGeo) return false;

	if ((!eaSize(&pSpecies->eaGeometries)) && (!eaSize(&pSpecies->eaCategories)))
	{
		//If no Geometries or Categories then allow all
		return true;
	}
	for (j=eaSize(&pSpecies->eaGeometries)-1; j>=0; --j)
	{
		if (GET_REF(pSpecies->eaGeometries[j]->hGeo) == pGeo)
		{
			return true;
		}
	}
	for (j=eaSize(&pSpecies->eaCategories)-1; j>=0; --j)
	{
		for (k=eaSize(&pGeo->eaCategories)-1; k>=0; --k)
		{
			if (GET_REF(pSpecies->eaCategories[j]->hCategory) == GET_REF(pGeo->eaCategories[k]->hCategory))
			{
				return true;
			}
		}
	}

	return false;
}

static bool costumeTailor_TestValidMatInSpecies(PCMaterialDef *pMat, PCGeometryDef *pGeo, SpeciesDef *pSpecies)
{
	int j, k;
	if (!pSpecies) return true;
	if (!pMat) return false;
	if (!pGeo) return false;

	if ((!eaSize(&pSpecies->eaGeometries)) && (!eaSize(&pSpecies->eaCategories)))
	{
		//If no Geometries or Categories then allow all
		return true;
	}
	for (j=eaSize(&pSpecies->eaGeometries)-1; j>=0; --j)
	{
		if (GET_REF(pSpecies->eaGeometries[j]->hGeo) == pGeo)
		{
			break;
		}
	}
	if (j<0)
	{
		for (j=eaSize(&pSpecies->eaCategories)-1; j>=0; --j)
		{
			for (k=eaSize(&pGeo->eaCategories)-1; k>=0; --k)
			{
				if (GET_REF(pSpecies->eaCategories[j]->hCategory) == GET_REF(pGeo->eaCategories[k]->hCategory))
				{
					//Assume all materials are valid
					return true;
				}
			}
		}
	}
	else
	{
		GeometryLimits *pTemp = pSpecies->eaGeometries[j];
		if (pTemp->bAllowAllMat) return true;
		for (k=eaSize(&pTemp->eaMaterials)-1; k>=0; --k)
		{
			if (GET_REF(pTemp->eaMaterials[k]->hMaterial) == pMat)
			{
				return true;
			}
		}
	}

	return false;
}

static bool costumeTailor_TestValidTexInSpecies(PCTextureDef *pTex, PCMaterialDef *pMat, PCGeometryDef *pGeo, SpeciesDef *pSpecies)
{
	int j, k;
	if (!pSpecies) return true;
	if (!pTex) return false;
	if (!pMat) return false;
	if (!pGeo) return false;

	if ((!eaSize(&pSpecies->eaGeometries)) && (!eaSize(&pSpecies->eaCategories)))
	{
		//If no Geometries or Categories then allow all
		return true;
	}
	for (j=eaSize(&pSpecies->eaGeometries)-1; j>=0; --j)
	{
		if (GET_REF(pSpecies->eaGeometries[j]->hGeo) == pGeo)
		{
			break;
		}
	}
	if (j<0)
	{
		for (j=eaSize(&pSpecies->eaCategories)-1; j>=0; --j)
		{
			for (k=eaSize(&pGeo->eaCategories)-1; k>=0; --k)
			{
				if (GET_REF(pSpecies->eaCategories[j]->hCategory) == GET_REF(pGeo->eaCategories[k]->hCategory))
				{
					//Assume all textures are valid
					return true;
				}
			}
		}
	}
	else
	{
		GeometryLimits *pTemp = pSpecies->eaGeometries[j];
		if (pTemp->bAllowAllMat) return true;
		for (k=eaSize(&pTemp->eaMaterials)-1; k>=0; --k)
		{
			if (GET_REF(pTemp->eaMaterials[k]->hMaterial) == pMat)
			{
				break;
			}
		}
		if (k>=0)
		{
			MaterialLimits *pTemp2 = pTemp->eaMaterials[k];
			if (pTemp2->bAllowAllTex) return true;
			for (k=eaSize(&pTemp2->eaTextures)-1; k>=0; --k)
			{
				if (GET_REF(pTemp2->eaTextures[k]->hTexture) == pTex)
				{
					return true;
				}
			}
		}
	}

	return false;
}

static int costumeTailor_BoneHasValidGeo(NOCONST(PlayerCostume) *pPCCostume, PCBoneDef *pBone, PCCategory *pCategory, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, bool bSortDisplay, bool bUnlockAll, bool countThem, bool incluedNone) 
{
	DictionaryEArrayStruct *pGeoStruct = resDictGetEArrayStruct("CostumeGeometry");
	DictionaryEArrayStruct *pBoneStruct = resDictGetEArrayStruct("CostumeBone");
	int i,j, count = 0;
	NOCONST(PCPart) *pParentPart = NULL;
	PCBoneDef *pParentBone = NULL;
	PCGeometryDef **eaParentGeos = NULL;
	PERFINFO_AUTO_START_FUNC();

	// For child bone, need to check parent bone
	if (pBone->bIsChildBone) {
		for(i=eaSize(&pBoneStruct->ppReferents)-1; i>=0 && !pParentBone; --i) {
			PCBoneDef *pOtherBone = pBoneStruct->ppReferents[i];
			for(j=eaSize(&pOtherBone->eaChildBones)-1; j>=0; --j) {
				if (GET_REF(pOtherBone->eaChildBones[j]->hChildBone) == pBone) {
					pParentBone = pOtherBone;
					break;
				}
			}
		}
		if (pParentBone) {
			pParentPart = costumeTailor_GetPartByBone(pPCCostume, pParentBone, NULL);
			if (pParentPart) {
				costumeTailor_GetValidChildGeos(pPCCostume, pCategory, GET_REF(pParentPart->hGeoDef), pBone, pSpecies, eaUnlockedCostumes, &eaParentGeos, bSortDisplay, bUnlockAll);
			}
		}

		if (eaSize(&eaParentGeos) == 0) {
			// No valid parts
			PERFINFO_AUTO_STOP_FUNC();
			return 0;
		}
	}

	if (incluedNone && pBone->bIsChildBone && !strcmp(eaParentGeos[0]->pcName,"None"))
	{
		if (!countThem)
		{
			eaDestroy(&eaParentGeos);
			PERFINFO_AUTO_STOP_FUNC();
			return 1;
		}
		++count;
	}

	for(i=0; i<eaSize(&pGeoStruct->ppReferents); ++i) {
		PCGeometryDef *pDef = (PCGeometryDef*)pGeoStruct->ppReferents[i];
		if (pDef && (GET_REF(pDef->hBone) == pBone)) {
			if (pBone->bIsChildBone && (eaFind(&eaParentGeos, pDef) < 0)) {
				// Can't use this geo since it's not allowed
				continue;
			}
			if (pCategory && pPCCostume && (pPCCostume->eCostumeType != kPCCostumeType_Unrestricted)) {
				for(j=eaSize(&pDef->eaCategories)-1; j>=0; --j) {
					if (GET_REF(pDef->eaCategories[j]->hCategory) == pCategory &&
						costumeTailor_TestValidGeoInSpecies(pDef, pSpecies)) {
						if (costumeTestRestrictionEfficiently(pDef->eRestriction, bUnlockAll ? bUnlockAll : costumeTailor_GeometryIsUnlocked(eaUnlockedCostumes, pDef), (PlayerCostume*)pPCCostume)) {
							// Do care about category and part is in the category, so return true
							if (!countThem)
							{
								eaDestroy(&eaParentGeos);
								PERFINFO_AUTO_STOP_FUNC();
								return 1;
							}
							++count;
						}
					}
				}
			} else {
				// Don't care about category, so there's a valid part and return true
				if (costumeTailor_TestValidGeoInSpecies(pDef, pSpecies))
				{
					if (costumeTestRestrictionEfficiently(pDef->eRestriction, bUnlockAll ? bUnlockAll : costumeTailor_GeometryIsUnlocked(eaUnlockedCostumes, pDef), (PlayerCostume*)pPCCostume)) {
						if (!countThem)
						{
							eaDestroy(&eaParentGeos);
							PERFINFO_AUTO_STOP_FUNC();
							return 1;
						}
						++count;
					}
				}
			}
		}
	}

	eaDestroy(&eaParentGeos);
	PERFINFO_AUTO_STOP_FUNC();
	return count;
}

bool costumeTailor_CategoryHasValidGeos(NOCONST(PlayerCostume) *pPCCostume, PCRegion *pRegion, PCCategory *pCategory, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, U32 uCGVFlags)
{
	PCSkeletonDef *pSkel;
	int i;
	bool bRequirePowerFXBones = !!(uCGVFlags & CGVF_REQUIRE_POWERFX);
	bool bSortDisplay = !!(uCGVFlags & CGVF_SORT_DISPLAY);
	bool bUnlockAll = !!(uCGVFlags & CGVF_UNLOCK_ALL);
	bool bMatched = false;
	PERFINFO_AUTO_START_FUNC();

	pSkel = GET_REF(pPCCostume->hSkeleton);
	if (!pSkel) {
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	// Scan required bones and return true if all required bones have a valid geo
	for(i=0; i<eaSize(&pSkel->eaRequiredBoneDefs); ++i) {
		PCBoneDef *pDef = GET_REF(pSkel->eaRequiredBoneDefs[i]->hBone);
		if (!pDef || !GET_REF(pDef->hRegion) || !pRegion || (pRegion == GET_REF(pDef->hRegion))) {
			if (pDef && costumeTailor_TestRestriction(pDef->eRestriction, true, (PlayerCostume*)pPCCostume) &&
				costumeTailor_BoneIsNotPowerFxLocked(pPCCostume,pDef,eaPowerFXBones,bUnlockAll) &&
				((bRequirePowerFXBones && pDef->bPowerFX) || costumeTailor_BoneHasValidGeo(pPCCostume, pDef, pCategory, pSpecies, eaUnlockedCostumes, bSortDisplay, bUnlockAll, false, false))) {
				bMatched = true;
			} else {
				PERFINFO_AUTO_STOP_FUNC();
				return false; // Missing a required bone
			}
		}
	}
	if (pCategory) {
		for(i=0; i<eaSize(&pCategory->eaRequiredBones); ++i) {
			PCBoneDef *pDef = GET_REF(pCategory->eaRequiredBones[i]->hBone);
			if (!pDef || !GET_REF(pDef->hRegion) || !pRegion || (pRegion == GET_REF(pDef->hRegion))) {
				if (pDef && costumeTailor_TestRestriction(pDef->eRestriction, true, (PlayerCostume*)pPCCostume) &&
					costumeTailor_BoneIsNotPowerFxLocked(pPCCostume,pDef,eaPowerFXBones,bUnlockAll) &&
					((bRequirePowerFXBones && pDef->bPowerFX) || costumeTailor_BoneHasValidGeo(pPCCostume, pDef, pCategory, pSpecies, eaUnlockedCostumes, bSortDisplay, bUnlockAll, false, false))) {
					bMatched = true;
				} else {
					PERFINFO_AUTO_STOP_FUNC();
					return false; // Missing a required bone
				}
			}
		}
	}
	if (bMatched) {
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}

	// Scan optional bones and return true if any one of them has a valid geo
	for(i=0; i<eaSize(&pSkel->eaOptionalBoneDefs); ++i)
	{
		PCBoneDef *pDef = GET_REF(pSkel->eaOptionalBoneDefs[i]->hBone);
		if(pDef && (!GET_REF(pDef->hRegion) || !pRegion || (pRegion == GET_REF(pDef->hRegion))))
		{
			if(
				costumeTailor_TestRestriction(pDef->eRestriction, true, (PlayerCostume*)pPCCostume) &&
				costumeTailor_BoneIsNotPowerFxLocked(pPCCostume,pDef,eaPowerFXBones,bUnlockAll) &&
				((bRequirePowerFXBones && pDef->bPowerFX) || costumeTailor_BoneHasValidGeo(pPCCostume, pDef, pCategory, pSpecies, eaUnlockedCostumes, bSortDisplay, bUnlockAll, false, false))
			)
			{
				bMatched = true;
			}
			else if (costumeTailor_IsBoneRequired(pPCCostume, pDef, pSpecies))
			{
				PERFINFO_AUTO_STOP_FUNC();
				return false; // Missing a required bone - Sometimes a species will make a normally optional bone required
			}
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
	return bMatched;
}


bool costumeTailor_CategoryIsExcluded(NOCONST(PlayerCostume) *pPCCostume, PCCategory *pCategory)
{
	int i,j;

	for(i=eaSize(&pPCCostume->eaRegionCategories)-1; i>=0; --i) {
		PCCategory *pCat = GET_REF(pPCCostume->eaRegionCategories[i]->hCategory);
		if (pCat) {
			for(j=eaSize(&pCat->eaExcludedCategories)-1; j>=0; --j) {
				if (pCategory == GET_REF(pCat->eaExcludedCategories[j]->hCategory)) {
					return true;
				}
			}
		}
	}
	return false;
}


bool costumeTailor_BoneIsExcluded(NOCONST(PlayerCostume) *pPCCostume, PCBoneDef *pBone)
{
	int i,j;

	for(i=eaSize(&pPCCostume->eaRegionCategories)-1; i>=0; --i) {
		PCCategory *pCat = GET_REF(pPCCostume->eaRegionCategories[i]->hCategory);
		if (pCat) {
			for(j=eaSize(&pCat->eaExcludedBones)-1; j>=0; --j) {
				if (pBone == GET_REF(pCat->eaExcludedBones[j]->hBone)) {
					return true;
				}
			}
		}
	}
	return false;
}

bool costumeTailor_CategoryIncludedInSlotType(PCCategory *pCategory, PCSlotType *pSlotType)
{
	int i;

	if (!pSlotType) {
		return true;
	}

	for(i=eaSize(&pSlotType->eaCategories)-1; i>=0; --i) {
		if (pCategory == GET_REF(pSlotType->eaCategories[i]->hCategory)) {
			return true;
		}
	}
	return false;
}

// Fills the earray with the legal set of categories for the costume and provided region
// Applies restrictions based on the costume
void costumeTailor_GetValidCategories(NOCONST(PlayerCostume) *pPCCostume, PCRegion *pRegion, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCSlotType *pSlotType, PCCategory ***peaCategories, U32 uCGVFlags)
{
	PCCategory *pCategory;
	int i;
	PERFINFO_AUTO_START_FUNC();

	eaClear(peaCategories);
	if (!pRegion) {
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	for(i=0; i<eaSize(&pRegion->eaCategories); ++i) {
		pCategory = GET_REF(pRegion->eaCategories[i]->hCategory);
		if (pCategory && costumeTailor_CategoryIncludedInSlotType(pCategory, pSlotType) &&
			(!(uCGVFlags & CGVF_OMIT_EMPTY) || (!pCategory->bHidden && !costumeTailor_CategoryIsExcluded(pPCCostume, pCategory) && costumeTailor_CategoryHasValidGeos(pPCCostume, pRegion, pCategory, pSpecies, eaUnlockedCostumes, eaPowerFXBones, uCGVFlags)))) {
			eaPush(peaCategories, pCategory);
		}
	}
	costumeTailor_SortCategories(*peaCategories, !!(uCGVFlags & CGVF_SORT_DISPLAY));
	PERFINFO_AUTO_STOP_FUNC();
}


// Fills the earray with the legal set of bones for the skeleton defined in the costume
// Applies restrictions based on the costume
void costumeTailor_GetValidBones(NOCONST(PlayerCostume) *pPCCostume, PCSkeletonDef *pSkeleton, PCRegion *pRegion, PCCategory *pCategory, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCBoneDef ***peaBones, U32 uCGVFlags)
{
	PCBoneDef *pDef;
	int i;
	bool bMirrorMode = !!(uCGVFlags & CGVF_MIRROR_MODE);
	bool bBoneGroupMode = !!(uCGVFlags & CGVF_BONE_GROUP_MODE);
	bool bOmitEmpty = !!(uCGVFlags & CGVF_OMIT_EMPTY);
	bool bOmitHasOnlyOne = !!(uCGVFlags & CGVF_OMIT_ONLY_ONE);
	bool countNone = !!(uCGVFlags & CGVF_COUNT_NONE);
	bool bRequirePowerFXBones = !!(uCGVFlags & CGVF_REQUIRE_POWERFX);
	bool bSortDisplay = !!(uCGVFlags & CGVF_SORT_DISPLAY);
	bool bUnlockAll = !!(uCGVFlags & CGVF_UNLOCK_ALL);
	bool bExcludeEmblemBones = !!(uCGVFlags & CGVF_EXCLUDE_GUILD_EMBLEM);
	int allowedMinCount = (bOmitEmpty ? (bOmitHasOnlyOne ? 2 : 1) : 0);

	 PERFINFO_AUTO_START_FUNC();

	eaClear(peaBones);

	if (!pSkeleton) {
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	for(i=0; i<eaSize(&pSkeleton->eaRequiredBoneDefs); ++i) {
		pDef = GET_REF(pSkeleton->eaRequiredBoneDefs[i]->hBone);
		if (pDef &&
			(!GET_REF(pDef->hRegion) || !pRegion || (pRegion == GET_REF(pDef->hRegion))) &&
			costumeTailor_TestRestriction(pDef->eRestriction, true, (PlayerCostume*)pPCCostume) &&
			costumeTailor_BoneIsNotPowerFxLocked(pPCCostume,pDef,eaPowerFXBones,bUnlockAll) &&
			(!bOmitEmpty || (bRequirePowerFXBones && pDef->bPowerFX) || (!costumeTailor_BoneIsExcluded(pPCCostume, pDef) && allowedMinCount <= costumeTailor_BoneHasValidGeo(pPCCostume, pDef, pCategory, pSpecies, eaUnlockedCostumes, bSortDisplay, bUnlockAll, true, countNone))) &&
			(!bExcludeEmblemBones || !pDef->bIsGuildEmblemBone)) {
			eaPushUnique(peaBones, pDef);
		}
	}
	for(i=0; i<eaSize(&pSkeleton->eaOptionalBoneDefs); ++i) {
		pDef = GET_REF(pSkeleton->eaOptionalBoneDefs[i]->hBone);
		if (pDef &&
			(!GET_REF(pDef->hRegion) || !pRegion || (pRegion == GET_REF(pDef->hRegion))) &&
			costumeTailor_TestRestriction(pDef->eRestriction, true, (PlayerCostume*)pPCCostume) &&
			costumeTailor_BoneIsNotPowerFxLocked(pPCCostume,pDef,eaPowerFXBones,bUnlockAll) &&
			(!bOmitEmpty || (bRequirePowerFXBones && pDef->bPowerFX) || (!costumeTailor_BoneIsExcluded(pPCCostume, pDef) && allowedMinCount <= costumeTailor_BoneHasValidGeo(pPCCostume, pDef, pCategory, pSpecies, eaUnlockedCostumes, bSortDisplay, bUnlockAll, true, countNone))) &&
			(!bExcludeEmblemBones || !pDef->bIsGuildEmblemBone)) {
			eaPushUnique(peaBones, pDef);
		}
	}

	if (bMirrorMode) {
		// Remove mirror bones when asked to do the merge
		for(i=eaSize(peaBones)-1; i>=0; --i) {
			pDef = (*peaBones)[i];
			if (GET_REF(pDef->hMirrorBone)) {
				PCLayer *pSelfLayer = GET_REF(pDef->hSelfLayer);
				int j;
				for(j=i-1; j>=0; --j) {
					if ((*peaBones)[j] == GET_REF(pDef->hMirrorBone)) {
						if (pSelfLayer->eLayerType == kPCLayerType_Left) {
							eaRemove(peaBones, j);
							--i;
						} else {
							eaRemove(peaBones, i);
						}
						break;
					}
				}
			}
		}
	}
	if (bBoneGroupMode)
	{
		// Remove all but first bone in the list when asked to do the merge
		for(i=eaSize(peaBones)-1; i>=0; --i) {
			int j, k = -1;

			pDef = (*peaBones)[i];
			for(j=eaSize(&pSkeleton->eaBoneGroups)-1; j>=0; --j)
			{
				if (pSkeleton->eaBoneGroups[j]->eBoneGroupFlags & kPCBoneGroupFlags_MatchGeos)
				{
					for(k=eaSize(&pSkeleton->eaBoneGroups[j]->eaBoneInGroup)-1; k>0; --k)
					{
						if (pDef == GET_REF(pSkeleton->eaBoneGroups[j]->eaBoneInGroup[k]->hBone))
						{
							eaRemove(peaBones, i);
							break;
						}
					}
				}
				if (k > 0) break;
			}
		}
	}

	costumeTailor_SortBones(*peaBones, bSortDisplay);
	PERFINFO_AUTO_STOP_FUNC();
}

bool costumeTailor_GetValidGeosFillParentGeoList(NOCONST(PlayerCostume) * pPCCostume, SpeciesDef * pSpecies, PCBoneDef * pBoneDef, NOCONST(PCPart) **ppParentPart, PCCategory * pCategory, PlayerCostume **eaUnlockedCostumes, PCGeometryDef ***peaParentGeos, bool bSortDisplay, bool bUnlockAll) 
{
	int i,j;
	// For child bone, need to check parent bone
	if (pPCCostume && pBoneDef->bIsChildBone) {
		DictionaryEArrayStruct *pBoneStruct = resDictGetEArrayStruct("CostumeBone");
		PCBoneDef *pParentBone = NULL;

		PERFINFO_AUTO_START("Get Geos From Parent", 1);
		for(i=eaSize(&pBoneStruct->ppReferents)-1; i>=0 && !pParentBone; --i) {
			PCBoneDef *pOtherBone = pBoneStruct->ppReferents[i];
			for(j=eaSize(&pOtherBone->eaChildBones)-1; j>=0; --j) {
				if (GET_REF(pOtherBone->eaChildBones[j]->hChildBone) == pBoneDef) {
					pParentBone = pOtherBone;
					break;
				}
			}
		}
		if (pParentBone) {
			*ppParentPart = costumeTailor_GetPartByBone(pPCCostume, pParentBone, NULL);
			if (*ppParentPart) {
				costumeTailor_GetValidChildGeos(pPCCostume, pCategory, GET_REF((*ppParentPart)->hGeoDef), pBoneDef, pSpecies, eaUnlockedCostumes, peaParentGeos, bSortDisplay, bUnlockAll);
			}
		}
		PERFINFO_AUTO_STOP();

		if (eaSize(peaParentGeos) == 0) {
			PERFINFO_AUTO_STOP_FUNC();
			// No valid parts
			return false;
		}
	}
	return true;
}

void costumeTailor_GetValidGeosFillLegalGeoList(NOCONST(PlayerCostume) * pPCCostume, SpeciesDef* pSpecies, PCGeometryDef*** peaGeos, PCBoneDef* pBoneDef, PCGeometryDef** eaParentGeos, PCCategory* pCategory, PlayerCostume** eaUnlockedCostumes, bool bMirrorMode, bool bBoneGroupMode, bool bUnlockAll) 
{
	DictionaryEArrayStruct *pGeoStruct = resDictGetEArrayStruct("CostumeGeometry");
	int i,j;

	// Check each geometry to see if it is allowed
	PERFINFO_AUTO_START("Find Valid Geos", 1);
	for(i=0; i<eaSize(&pGeoStruct->ppReferents); ++i) {
		PCGeometryDef *pDef = (PCGeometryDef*)pGeoStruct->ppReferents[i];
		if (pDef && (GET_REF(pDef->hBone) == pBoneDef)) {

			// When merging bones, only valid if have a mirror geo
			if (bMirrorMode && !pDef->pcMirrorGeometry) {
				continue; 
			}

			// When merging bones, only valid if have a bone group
			if (bBoneGroupMode)
			{
				NOCONST(PCPart) *pPart = costumeTailor_GetPartByBone(pPCCostume, pBoneDef, NULL);
				if (pPart && pPart->iBoneGroupIndex < 0)
				{
					continue; 
				}
			}

			// Is a child bone, and not legal for current parent
			if (pBoneDef->bIsChildBone && pPCCostume && (eaFind(&eaParentGeos, pDef) < 0)) {
				continue; 
			}

			PERFINFO_AUTO_START("Check Add Unlocked Geo", 1);
			if (pCategory && pPCCostume && (pPCCostume->eCostumeType != kPCCostumeType_Unrestricted)) {
				for(j=eaSize(&pDef->eaCategories)-1; j>=0; --j) {
					if (pDef->eaCategories[j] && GET_REF(pDef->eaCategories[j]->hCategory) == pCategory) {
						if (costumeTailor_TestValidGeoInSpecies(pDef,pSpecies)) {
							PERFINFO_AUTO_START("Test Unlocked Geo", 1);
							if (costumeTestRestrictionEfficiently(pDef->eRestriction, bUnlockAll ? true : costumeTailor_GeometryIsUnlocked(eaUnlockedCostumes, pDef), (PlayerCostume*)pPCCostume)) {
								eaPush(peaGeos, pDef);
							}
							PERFINFO_AUTO_STOP();
							break;
						}
					}
				}
			} else {
				if (costumeTailor_TestValidGeoInSpecies(pDef,pSpecies))
				{
					PERFINFO_AUTO_START("Test Unlocked Geo", 1);
					if (costumeTestRestrictionEfficiently(pDef->eRestriction, bUnlockAll ? true : costumeTailor_GeometryIsUnlocked(eaUnlockedCostumes, pDef), (PlayerCostume*)pPCCostume)) {
						eaPush(peaGeos, pDef);
					}
					PERFINFO_AUTO_STOP();
				}
			}
			PERFINFO_AUTO_STOP();
		}
	}
	PERFINFO_AUTO_STOP();
}

void costumeTailor_GetValidGeosRemoveMirrorGeos(NOCONST(PlayerCostume)* pPCCostume, SpeciesDef* pSpecies, PCGeometryDef*** peaGeos, PCSkeletonDef* pSkeleton, PCBoneDef* pBoneDef, PCCategory* pCategory, PlayerCostume** eaUnlockedCostumes, bool bMirrorMode, bool bSortDisplay, bool bUnlockAll) 
{
	// If in mirror mode, remove all entries that don't appear on mirror piece
	if (bMirrorMode && GET_REF(pBoneDef->hMirrorBone)) {
		PCGeometryDef **eaMirrorGeos = NULL;
		int i,j;
		PERFINFO_AUTO_START("Mirror Update", 1);

		costumeTailor_GetValidGeos(pPCCostume, pSkeleton, GET_REF(pBoneDef->hMirrorBone), pCategory, pSpecies, eaUnlockedCostumes, &eaMirrorGeos, false, false, bSortDisplay, bUnlockAll);
		for(i=eaSize(peaGeos)-1; i>=0; --i) {
			PCGeometryDef *pOrigDef = (*peaGeos)[i];
			for(j=eaSize(&eaMirrorGeos)-1; j>=0; --j) {
				PCGeometryDef *pOtherDef = eaMirrorGeos[j];
				if ((pOrigDef == pOtherDef) ||
					(pOrigDef->pcMirrorGeometry && (stricmp(pOtherDef->pcName, pOrigDef->pcMirrorGeometry) == 0)) ||
					(stricmp(TranslateDisplayMessage(pOrigDef->displayNameMsg), TranslateDisplayMessage(pOtherDef->displayNameMsg)) == 0)) {
						break;
				}
			}
			if (j < 0) {
				eaRemove(peaGeos, i);
			}
		}

		eaDestroy(&eaMirrorGeos);
		PERFINFO_AUTO_STOP();
	}
}

void costumeTailor_GetValidGeosRemoveFromNonmatchingBoneGroups(NOCONST(PlayerCostume)* pPCCostume, SpeciesDef* pSpecies, PCGeometryDef*** peaGeos, PCSkeletonDef* pSkeleton, PCCategory* pCategory, PlayerCostume** eaUnlockedCostumes, bool bBoneGroupMode, bool bUnlockAll, bool bSortDisplay)
{
	int i;

	// If in bone group mode, remove all entries that don't appear on all the bones
	if (bBoneGroupMode)
	{
		int iGeo;
		PCGeometryDef **eaGroupGeos = NULL;
		NOCONST(PCPart) *pGroupPart = NULL;
		PERFINFO_AUTO_START("Bone Group Update", 1);

		for(i=eaSize(peaGeos)-1; i>=0; --i) {
			PCGeometryDef *pOrigDef = (*peaGeos)[i];
			pGroupPart = costumeTailor_GetPartByBone(pPCCostume, GET_REF(pOrigDef->hBone), NULL);
			if (pGroupPart->iBoneGroupIndex >= 0)
			{
				costumeTailor_GetValidGeos(pPCCostume, pSkeleton, GET_REF(pGroupPart->hBoneDef), pCategory, pSpecies, eaUnlockedCostumes, &eaGroupGeos, false, false, bSortDisplay, bUnlockAll);
				iGeo = costumeTailor_GetMatchingGeoIndex(pOrigDef, eaGroupGeos);
				if (iGeo < 0)
					eaRemove(peaGeos, i);
			}
		}

		eaDestroy(&eaGroupGeos);
		PERFINFO_AUTO_STOP();
	}
}

void costumeTailor_GetValidGeosAddNoneGeoIfRequired(NOCONST(PlayerCostume)* pPCCostume, SpeciesDef* pSpecies, PCGeometryDef*** peaGeos, PCBoneDef* pBoneDef, NOCONST(PCPart)* pParentPart, bool bSortDisplay) 
{
	int i;
	if (eaSize(peaGeos)) {
		bool bFound;

		PERFINFO_AUTO_START("Sort Geos", 1);
		costumeTailor_SortGeos(*peaGeos, bSortDisplay);
		PERFINFO_AUTO_STOP();

		// Add a "None" entry if appropriate
		PERFINFO_AUTO_START("Add None Geo", 1);
		bFound = false;
		if (pPCCostume &&
			(pPCCostume->eCostumeType != kPCCostumeType_Unrestricted) &&
			(pPCCostume->eCostumeType != kPCCostumeType_Item) &&
			(pPCCostume->eCostumeType != kPCCostumeType_Overlay)) {
				if (pParentPart && GET_REF(pParentPart->hGeoDef)) {
					PCGeometryDef *pParentGeo = GET_REF(pParentPart->hGeoDef);
					PCGeometryOptions *pParentOptions = pParentGeo->pOptions;
					for(i=eaSize(&pParentOptions->eaChildGeos)-1; i>=0; --i) {
						if ((GET_REF(pParentOptions->eaChildGeos[i]->hChildBone) == pBoneDef) && (pParentOptions->eaChildGeos[i]->bRequiresChild)) {
							bFound = true;
							break;
						}
					}
				}
				if (!bFound)
					bFound = costumeTailor_IsBoneRequired(pPCCostume, pBoneDef, pSpecies);
		}
		if (!bFound) {
			PCGeometryDef *pDef = RefSystem_ReferentFromString(g_hCostumeGeometryDict, "None");
			if (pDef) {
				eaInsert(peaGeos, pDef, 0);
			}
		}
		PERFINFO_AUTO_STOP();
	}
}

// Fills the earray with the legal set of geometries for the given bone
// Applies restrictions based on the costume
void costumeTailor_GetValidGeos(NOCONST(PlayerCostume) *pPCCostume, PCSkeletonDef *pSkeleton, PCBoneDef *pBoneDef, PCCategory *pCategory, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, PCGeometryDef ***peaGeos, bool bMirrorMode, bool bBoneGroupMode, bool bSortDisplay, bool bUnlockAll)
{
	PCGeometryDef **eaParentGeos = NULL;
	NOCONST(PCPart) *pParentPart = NULL;

	PERFINFO_AUTO_START_FUNC();
	
	eaClear(peaGeos);

	if (!pBoneDef) {
		PERFINFO_AUTO_STOP_FUNC();
		return; // Can't have any valid geos if not a valid bone
	}
	
	// Check if this bone is excluded
	if (pPCCostume && costumeTailor_BoneIsExcluded(pPCCostume, pBoneDef)) {
		PERFINFO_AUTO_STOP_FUNC();
		return; // This bone is excluded so no geometries allowed
	}

	if (!costumeTailor_GetValidGeosFillParentGeoList(pPCCostume, pSpecies, pBoneDef, &pParentPart, pCategory, eaUnlockedCostumes, &eaParentGeos, bSortDisplay, bUnlockAll))
	{
		eaDestroy(&eaParentGeos);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	costumeTailor_GetValidGeosFillLegalGeoList(pPCCostume, pSpecies, peaGeos, pBoneDef, eaParentGeos, pCategory, eaUnlockedCostumes, bMirrorMode, bBoneGroupMode, bUnlockAll);
	costumeTailor_GetValidGeosRemoveMirrorGeos(pPCCostume, pSpecies, peaGeos, pSkeleton, pBoneDef, pCategory, eaUnlockedCostumes, bMirrorMode, bSortDisplay, bUnlockAll);
	costumeTailor_GetValidGeosRemoveFromNonmatchingBoneGroups(pPCCostume, pSpecies, peaGeos, pSkeleton, pCategory, eaUnlockedCostumes, bBoneGroupMode, bUnlockAll, bSortDisplay);
	costumeTailor_GetValidGeosAddNoneGeoIfRequired(pPCCostume, pSpecies, peaGeos, pBoneDef, pParentPart, bSortDisplay);

	eaDestroy(&eaParentGeos);
	PERFINFO_AUTO_STOP_FUNC();
}


// Fills the earray with the legal set of geometry layers for the costume and provided geometry
// If the child geometry is NULL, get all valid layers for the geometry
// Applies restrictions based on the costume
void costumeTailor_GetValidLayers(NOCONST(PlayerCostume) *pPCCostume, PCGeometryDef *pGeoDef, PCGeometryDef *pChildGeoDef, PCLayer ***peaLayers, bool bSortDisplay)
{
	PCBoneDef *pBone;
	PCLayer *pLayer;
	int i;

	eaClear(peaLayers);

	if (!pGeoDef) {
		return;
	}
	pBone = GET_REF(pGeoDef->hBone);
	if (!pBone) {
		return;
	}

	if (pGeoDef->pClothData && pGeoDef->pClothData->bIsCloth && pGeoDef->pClothData->bHasClothBack) {
		pLayer = GET_REF(pBone->hMainLayerFront);
		if (!pLayer) {
			pLayer = RefSystem_ReferentFromString(g_hCostumeLayerDict, "Front");
		}
		if (pLayer) {
			eaPush(peaLayers, pLayer);
		}

		pLayer = GET_REF(pBone->hMainLayerBack);
		if (!pLayer) {
			pLayer = RefSystem_ReferentFromString(g_hCostumeLayerDict, "Back");
		}
		if (pLayer) {
			eaPush(peaLayers, pLayer);
		}
	} else {
		pLayer = GET_REF(pBone->hMainLayerFront);
		if (!pLayer) {
			pLayer = RefSystem_ReferentFromString(g_hCostumeLayerDict, "Main");
		}
		if (pLayer) {
			eaPush(peaLayers, pLayer);
		}
	}

	for(i=eaSize(&pBone->eaChildBones)-1; i>=0; --i) {
		PCChildBone *pBoneInfo = pBone->eaChildBones[i];
		NOCONST(PCPart) *pPart;
		PCGeometryDef *pGeo;
		bool bFindPart = true;

		// Only check the one child if it is provided
		if (pChildGeoDef && (stricmp(pChildGeoDef->pcName, "None") != 0)) {
			if (GET_REF(pChildGeoDef->hBone) != GET_REF(pBoneInfo->hChildBone)) {
				continue;
			}
			bFindPart = false;
		}

		if (pBoneInfo) {
			if (bFindPart) {
				pPart = costumeTailor_GetPartByBone(pPCCostume, GET_REF(pBoneInfo->hChildBone), NULL);
				if (pPart) {
					pGeo = GET_REF(pPart->hGeoDef);
				} else {
					pGeo = pChildGeoDef;
				}
			} else {
				pGeo = pChildGeoDef;
			}

			if (pGeo && pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack) {
				pLayer = GET_REF(pBoneInfo->hChildLayerFront);
				if (!pLayer) {
					pLayer = RefSystem_ReferentFromString(g_hCostumeLayerDict, "Child_Front");
				}
				if (pLayer) {
					eaPush(peaLayers, pLayer);
				}

				pLayer = GET_REF(pBoneInfo->hChildLayerBack);
				if (!pLayer) {
					pLayer = RefSystem_ReferentFromString(g_hCostumeLayerDict, "Child_Back");
				}
				if (pLayer) {
					eaPush(peaLayers, pLayer);
				}
			} else {
				pLayer = GET_REF(pBoneInfo->hChildLayerFront);
				if (!pLayer) {
					pLayer = RefSystem_ReferentFromString(g_hCostumeLayerDict, "Child");
				}
				if (pLayer) {
					eaPush(peaLayers, pLayer);
				}
			}
		}
	}
}


// Fills the earray with the legal set of geometries for the child
// Applies restrictions based on the costume
void costumeTailor_GetValidChildGeos(NOCONST(PlayerCostume) *pPCCostume, PCCategory *pCategory, PCGeometryDef *pGeoDef, PCBoneDef *pChildBone, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, PCGeometryDef ***peaGeos, bool bSortDisplay, bool bUnlockAll)
{
	PCGeometryChildDef *pChildDef = NULL;
	int i,j;

	eaClear(peaGeos);

	if (!pGeoDef || !pChildBone) {
		return;
	}

	// Find the child def
	if (pGeoDef->pOptions)
	{
		for(i=eaSize(&pGeoDef->pOptions->eaChildGeos)-1; i>=0; --i) {
			if (GET_REF(pGeoDef->pOptions->eaChildGeos[i]->hChildBone) == pChildBone) {
				pChildDef = pGeoDef->pOptions->eaChildGeos[i];
				break;
			}
		}
	}
	if (!pChildDef) {
		return;
	}

	// See which child geos are legal
	for(i=eaSize(&pChildDef->eaChildGeometries)-1; i>=0; --i) {
		PCGeometryDef *pDef = GET_REF(pChildDef->eaChildGeometries[i]->hGeo);
		if (pDef && costumeTestRestrictionEfficiently(pDef->eRestriction, bUnlockAll ? true : costumeTailor_GeometryIsUnlocked(eaUnlockedCostumes, pDef), (PlayerCostume*)pPCCostume)) {
			if (pCategory && (pPCCostume->eCostumeType != kPCCostumeType_Unrestricted)) {
				for(j=eaSize(&pDef->eaCategories)-1; j>=0; --j) {
					if (GET_REF(pDef->eaCategories[j]->hCategory) == pCategory) {
						if (costumeTailor_TestValidGeoInSpecies(pDef,pSpecies))
						{
							eaPush(peaGeos, pDef);
							break;
						}
					}
				}
			} else {
				if (costumeTailor_TestValidGeoInSpecies(pDef,pSpecies))
				{
					eaPush(peaGeos, pDef);
				}
			}
		}
	}

	if (eaSize(peaGeos)) {	
		costumeTailor_SortGeos(*peaGeos, bSortDisplay);

		// Add a "None" entry if appropriate
		if (!pChildDef->bRequiresChild) {
			PCGeometryDef *pDef = RefSystem_ReferentFromString(g_hCostumeGeometryDict, "None");
			if (pDef) {
				eaInsert(peaGeos, pDef, 0);
			}
		}
	}
}


// Fills the earray with the legal set of materials for the given geometry
// Applies restrictions based on the costume
void costumeTailor_GetValidMaterials(NOCONST(PlayerCostume) *pPCCostume, PCGeometryDef *pGeoDef, SpeciesDef *pSpecies, PCGeometryDef *pMirrorGeo, PCBoneGroup *pBoneGroup, PlayerCostume **eaUnlockedCostumes, PCMaterialDef ***peaMats, bool bMirrorMode, bool bSortDisplay, bool bUnlockAll)
{
	DictionaryEArrayStruct *pGeoAddStruct = resDictGetEArrayStruct("CostumeGeometryAdd");
	int i,j;
	
	eaClear(peaMats);
	
	if (!pGeoDef) {
		return;
	}
	for(i=0; i<eaSize(&pGeoDef->eaAllowedMaterialDefs); ++i) {
		if (pGeoDef->eaAllowedMaterialDefs[i]) {
			PCMaterialDef *pMatDef = RefSystem_ReferentFromString(g_hCostumeMaterialDict, pGeoDef->eaAllowedMaterialDefs[i]);
			if (pMatDef && costumeTestRestrictionEfficiently(pMatDef->eRestriction, bUnlockAll ? true : costumeTailor_MaterialIsUnlocked(eaUnlockedCostumes, pMatDef, pGeoDef), (PlayerCostume*)pPCCostume)) {
				if (costumeTailor_TestValidMatInSpecies(pMatDef, pGeoDef, pSpecies))
				{
					eaPush(peaMats, pMatDef);
				}
			}
		}
	}
	for(i=eaSize(&pGeoAddStruct->ppReferents)-1; i>=0; --i) {
		PCGeometryAdd *pGeoAdd = (PCGeometryAdd*)pGeoAddStruct->ppReferents[i];
		if (pGeoAdd->pcGeoName && (stricmp(pGeoAdd->pcGeoName, pGeoDef->pcName) == 0)) {
			for(j=0; j<eaSize(&pGeoAdd->eaAllowedMaterialDefs); ++j) {
				if (pGeoAdd->eaAllowedMaterialDefs[j]) {
					PCMaterialDef *pMatDef = RefSystem_ReferentFromString(g_hCostumeMaterialDict, pGeoAdd->eaAllowedMaterialDefs[j]);
					if (pMatDef && costumeTestRestrictionEfficiently(pMatDef->eRestriction, bUnlockAll ? true : costumeTailor_MaterialIsUnlocked(eaUnlockedCostumes, pMatDef, pGeoDef), (PlayerCostume*)pPCCostume)) {
						if (costumeTailor_TestValidMatInSpecies(pMatDef, pGeoDef, pSpecies))
						{
							eaPush(peaMats, pMatDef);
						}
					}
				}
			}
		}
	}

	// If in mirror mode, remove all entries that don't appear on mirror piece
	if (bMirrorMode) {
		PCMaterialDef **eaMirrorMats = NULL;

		costumeTailor_GetValidMaterials(pPCCostume, pMirrorGeo, pSpecies, NULL, NULL, eaUnlockedCostumes, &eaMirrorMats, false, bSortDisplay, bUnlockAll);
		for(i=eaSize(peaMats)-1; i>=0; --i) {
			PCMaterialDef *pOrigDef = (*peaMats)[i];
			for(j=eaSize(&eaMirrorMats)-1; j>=0; --j) {
				PCMaterialDef *pOtherDef = eaMirrorMats[j];
				if ((pOrigDef == pOtherDef) || (stricmp(TranslateDisplayMessage(pOrigDef->displayNameMsg), TranslateDisplayMessage(pOtherDef->displayNameMsg)) == 0)) {
					break;
				}
			}
			if (j < 0) {
				eaRemove(peaMats, i);
			}
		}
		
		eaDestroy(&eaMirrorMats);
	}
	// If in bone group mode, remove all entries that don't appear on all the bones
	if (pBoneGroup)
	{
		int iMat = -1;
		PCMaterialDef **eaGroupMats = NULL;
		NOCONST(PCPart) *pGroupPart = NULL;

		for(i=eaSize(peaMats)-1; i>=0; --i) {
			PCMaterialDef *pOrigDef = (*peaMats)[i];
			for(j=eaSize(&pBoneGroup->eaBoneInGroup)-1; j>=0; --j) {
				if (pBoneGroup->eBoneGroupFlags & kPCBoneGroupFlags_MatchGeos)
				{
					PCBoneDef *bone = GET_REF(pBoneGroup->eaBoneInGroup[j]->hBone);
					pGroupPart = costumeTailor_GetPartByBone(pPCCostume, bone, NULL);
					if (pGroupPart)
					{
						costumeTailor_GetValidMaterials(pPCCostume, GET_REF(pGroupPart->hGeoDef), pSpecies, NULL, NULL, eaUnlockedCostumes, &eaGroupMats, false, bSortDisplay, bUnlockAll);
						iMat = costumeTailor_GetMatchingMatIndex(pOrigDef, eaGroupMats);
						if (iMat < 0) eaRemove(peaMats, i);
					}
				}
			}
		}

		eaDestroy(&eaGroupMats);
	}

	costumeTailor_SortMaterials(*peaMats, bSortDisplay);
}


bool costumeTailor_DoesMaterialRequireType(PCGeometryDef *pGeoDef, PCMaterialDef *pMatDef, SpeciesDef *pSpecies, PCTextureType eType)
{
	if ((eType == kPCTextureType_Pattern && pMatDef->bRequiresPattern) ||
		(eType == kPCTextureType_Detail && pMatDef->bRequiresDetail) ||
		(eType == kPCTextureType_Specular && pMatDef->bRequiresSpecular) ||
		(eType == kPCTextureType_Diffuse && pMatDef->bRequiresDiffuse) ||
		(eType == kPCTextureType_Movable && pMatDef->bRequiresMovable))
	{
		return true;
	}

	if (pSpecies)
	{
		int i, j;
		for (i = eaSize(&pSpecies->eaGeometries)-1; i >= 0; --i)
		{
			PCGeometryDef *geo = GET_REF(pSpecies->eaGeometries[i]->hGeo);
			if (geo && geo == pGeoDef)
			{
				for (j = eaSize(&pSpecies->eaGeometries[i]->eaMaterials)-1; j >= 0; --j)
				{
					PCMaterialDef *mat = GET_REF(pSpecies->eaGeometries[i]->eaMaterials[j]->hMaterial);
					if (mat && mat == pMatDef)
					{
						MaterialLimits *ml = pSpecies->eaGeometries[i]->eaMaterials[j];
						if ((eType == kPCTextureType_Pattern && ml->bRequiresPattern) ||
							(eType == kPCTextureType_Detail && ml->bRequiresDetail) ||
							(eType == kPCTextureType_Specular && ml->bRequiresSpecular) ||
							(eType == kPCTextureType_Diffuse && ml->bRequiresDiffuse) ||
							(eType == kPCTextureType_Movable && ml->bRequiresMovable))
						{
							return true;
						}
					}
				}
			}
		}
	}

	return false;
}

// Fills the earray with the legal set of textures for the given material and texture type
// Applies restrictions based on the costume
void costumeTailor_GetValidTextures(NOCONST(PlayerCostume) *pPCCostume, PCMaterialDef *pMatDef, SpeciesDef *pSpecies, PCMaterialDef *pMirrorMat, PCBoneGroup *pBoneGroup, PCGeometryDef *pGeoDef, PCGeometryDef *pMirrorGeo, PlayerCostume **eaUnlockedCostumes, PCTextureType eType, PCTextureDef ***peaTexs, bool bMirrorMode, bool bSortDisplay, bool bUnlockAll)
{
	DictionaryEArrayStruct *pMatAddStruct = resDictGetEArrayStruct("CostumeMaterialAdd");
	int i,j;

	eaClear(peaTexs);

	if (!pMatDef) {
		return;
	}

	for(i=0; i<eaSize(&pMatDef->eaAllowedTextureDefs); ++i) {
		PCTextureDef *pDef = RefSystem_ReferentFromString(g_hCostumeTextureDict, pMatDef->eaAllowedTextureDefs[i]);
		if (pDef && (pDef->eTypeFlags & eType)) {
			if (pDef && costumeTestRestrictionEfficiently(pDef->eRestriction, bUnlockAll ? true : costumeTailor_TextureIsUnlocked(eaUnlockedCostumes, pDef, pMatDef, pGeoDef), (PlayerCostume*)pPCCostume)) {
				if (costumeTailor_TestValidTexInSpecies(pDef, pMatDef, pGeoDef, pSpecies))
				{
					eaPush(peaTexs, pDef);
				}
			}
		}
	}
	for(i=eaSize(&pMatAddStruct->ppReferents)-1; i>=0; --i) {
		PCMaterialAdd *pMatAdd = (PCMaterialAdd*)pMatAddStruct->ppReferents[i];
		if (pMatAdd->pcMatName && (stricmp(pMatAdd->pcMatName, pMatDef->pcName) == 0)) {
			for(j=0; j<eaSize(&pMatAdd->eaAllowedTextureDefs); ++j) {
				PCTextureDef *pDef = RefSystem_ReferentFromString(g_hCostumeTextureDict, pMatAdd->eaAllowedTextureDefs[j]);
				if (pDef && (pDef->eTypeFlags & eType)) {
					if (costumeTestRestrictionEfficiently(pDef->eRestriction, bUnlockAll ? true : costumeTailor_TextureIsUnlocked(eaUnlockedCostumes, pDef, pMatDef, pGeoDef), (PlayerCostume*)pPCCostume)) {
						if (costumeTailor_TestValidTexInSpecies(pDef, pMatDef, pGeoDef, pSpecies))
						{
							eaPush(peaTexs, pDef);
						}
					}
				}
			}
		}
	}

	// If in mirror mode, remove all entries that don't appear on mirror piece
	if (bMirrorMode) {
		PCTextureDef **eaMirrorTexs = NULL;

		costumeTailor_GetValidTextures(pPCCostume, pMirrorMat, pSpecies, NULL, NULL, pMirrorGeo, NULL, eaUnlockedCostumes, eType, &eaMirrorTexs, false, bSortDisplay, bUnlockAll);
		for(i=eaSize(peaTexs)-1; i>=0; --i) {
			PCTextureDef *pOrigDef = (*peaTexs)[i];
			for(j=eaSize(&eaMirrorTexs)-1; j>=0; --j) {
				PCTextureDef *pOtherDef = eaMirrorTexs[j];
				if ((pOrigDef == pOtherDef) || (stricmp(TranslateDisplayMessage(pOrigDef->displayNameMsg), TranslateDisplayMessage(pOtherDef->displayNameMsg)) == 0)) {
					break;
				}
			}
			if (j < 0) {
				eaRemove(peaTexs, i);
			}
		}
		
		eaDestroy(&eaMirrorTexs);
	}
	// If in bone group mode, remove all entries that don't appear on all the bones
	if (pBoneGroup)
	{
		int iTex = -1;
		PCTextureDef **eaGroupTex = NULL;
		NOCONST(PCPart) *pGroupPart = NULL;

		for(i=eaSize(peaTexs)-1; i>=0; --i) {
			PCTextureDef *pOrigDef = (*peaTexs)[i];
			for(j=eaSize(&pBoneGroup->eaBoneInGroup)-1; j>=0; --j) {
				if (pBoneGroup->eBoneGroupFlags & kPCBoneGroupFlags_MatchGeos)
				{
					PCBoneDef *bone = GET_REF(pBoneGroup->eaBoneInGroup[j]->hBone);
					pGroupPart = costumeTailor_GetPartByBone(pPCCostume, bone, NULL);
					if (pGroupPart)
					{
						costumeTailor_GetValidTextures(pPCCostume, GET_REF(pGroupPart->hMatDef), pSpecies, NULL, NULL, GET_REF(pGroupPart->hGeoDef), NULL, eaUnlockedCostumes, eType, &eaGroupTex, false, bSortDisplay, bUnlockAll);
						iTex = costumeTailor_GetMatchingTexIndex(pOrigDef, eaGroupTex);
						if (iTex < 0) eaRemove(peaTexs, i);
					}
				}
			}
		}

		eaDestroy(&eaGroupTex);
	}

	if (eaSize(peaTexs)) {
		costumeTailor_SortTextures(*peaTexs, bSortDisplay);

		if (!costumeTailor_DoesMaterialRequireType(pGeoDef, pMatDef, pSpecies, eType)) {
			PCTextureDef *pDef = RefSystem_ReferentFromString(g_hCostumeTextureDict, "None");
			if (pDef) {
				eaInsert(peaTexs, pDef, 0);
			}
		}
	}
}

void costumeTailor_GetValidStyles(NOCONST(PlayerCostume) *pPCCostume, PCRegion *pRegion, PCCategory *pCategory, PCBoneDef *pBone, PCGeometryDef *pGeoDef, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCStyle ***peaStyles, U32 uCGVFlags)
{
	int i, j, k;
	PCBoneDef **eaBones = NULL;
	PCGeometryDef **eaGeos = NULL;
	PCSkeletonDef *pSkel = pPCCostume ? GET_REF(pPCCostume->hSkeleton) : NULL;

	if (!pSkel || !peaStyles)
		return;

	eaClear(peaStyles);

	if (pGeoDef) {
		// Return the valid styles on the geometry
		for (k=eaSize(&pGeoDef->eaStyles)-1; k>=0; --k) {
			PCStyle *pStyle = RefSystem_ReferentFromString(g_hCostumeStyleDict, pGeoDef->eaStyles[k]);
			if (pStyle) {
				eaPush(peaStyles, pStyle);
			}
		}
		return;
	}

	if (pBone) {
		// Only check the one bone
		eaPush(&eaBones, pBone);
	} else {
		// Get all the bones in the costume (or region if set)
		costumeTailor_GetValidBones(pPCCostume, pSkel, pRegion, pCategory, pSpecies, eaUnlockedCostumes, eaPowerFXBones, &eaBones, uCGVFlags);
	}

	for (i=eaSize(&eaBones)-1; i>=0; --i) {
		eaClearFast(&eaGeos);
		costumeTailor_GetValidGeos(pPCCostume, pSkel, eaBones[i], pCategory, pSpecies, eaUnlockedCostumes, &eaGeos, !!(uCGVFlags & CGVF_MIRROR_MODE), !!(uCGVFlags & CGVF_BONE_GROUP_MODE), false, !!(uCGVFlags & CGVF_UNLOCK_ALL));
		for (j=eaSize(&eaGeos)-1; j>=0; --j) {
			PCGeometryDef *pGeo = eaGeos[j];
			for (k=eaSize(&pGeo->eaStyles)-1; k>=0; --k) {
				PCStyle *pStyle = RefSystem_ReferentFromString(g_hCostumeStyleDict, pGeo->eaStyles[k]);
				if (pStyle && eaFind(peaStyles, pStyle)<0) {
					eaPush(peaStyles, pStyle);
				}
			}
		}
	}

	eaDestroy(&eaGeos);
	eaDestroy(&eaBones);

	costumeTailor_SortStyles(*peaStyles, !!(uCGVFlags & CGVF_SORT_DISPLAY));
}

void costumeTailor_GetValidSlotTypes(NOCONST(PlayerCostume) *pPCCostume, PCSlotDef *pSlotDef, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCSlotType ***peaSlotTypes, U32 uCGVFlags)
{
	PCSkeletonDef *pSkel = pPCCostume ? GET_REF(pPCCostume->hSkeleton) : NULL;
	PCBoneDef **eaBones = NULL;
	PCRegion **eaRegions = NULL;
	PCCategory **eaCategories = NULL;
	NOCONST(PlayerCostume) *pCostumeCopy = NULL;
	PCSlotType *pSlotType;
	S32 i, j, iErrors;

	if (!pSkel || !pSlotDef || !peaSlotTypes)
		return;

	PERFINFO_AUTO_START_FUNC();

	eaClear(peaSlotTypes);

	pCostumeCopy = StructCloneNoConst(parse_PlayerCostume, pPCCostume);

	// Required regions
	costumeTailor_GetValidBones(pPCCostume, pSkel, NULL, NULL, pSpecies, eaUnlockedCostumes, eaPowerFXBones, &eaBones, uCGVFlags);
	for (i=eaSize(&eaBones)-1; i>=0; --i) {
		if (costumeTailor_IsBoneRequired(pPCCostume, eaBones[i], pSpecies)) {
			if (GET_REF(eaBones[i]->hRegion)) {
				eaPushUnique(&eaRegions, GET_REF(eaBones[i]->hRegion));
			}
		}
	}
	eaDestroy(&eaBones);

	// The pSlotType should always be considered valid
	pSlotType = costumeLoad_GetSlotType(pSlotDef->pcSlotType);
	eaPush(peaSlotTypes, pSlotType);

	for (i=eaSize(&pSlotDef->eaOptionalSlotTypes)-1; i>=0; --i) {
		bool bValid = true;
		pSlotType = costumeLoad_GetSlotType(pSlotDef->eaOptionalSlotTypes[i]);
		// Ensure required regions all have a valid category
		for (iErrors=eaSize(&eaRegions)-1; iErrors>=0; --iErrors) {
			bValid = true;
			for (j=eaSize(&eaRegions)-1; j>=0; --j) {
				costumeTailor_GetValidCategories(pCostumeCopy, eaRegions[j], pSpecies, eaUnlockedCostumes, eaPowerFXBones, pSlotType, &eaCategories, uCGVFlags);
				if (eaSize(&eaCategories) <= 0) {
					bValid = false;
				} else {
					costumeTailor_SetRegionCategory(pCostumeCopy, eaRegions[j], eaCategories[0]);
				}
			}
			if (bValid) {
				break;
			}
		}
		if (bValid) {
			eaPushUnique(peaSlotTypes, pSlotType);
		}
	}

	eaDestroy(&eaCategories);
	eaDestroy(&eaRegions);
	StructDestroyNoConstSafe(parse_PlayerCostume, &pCostumeCopy);

	PERFINFO_AUTO_STOP();
}

// Builds a list of all valid PresetValueGroups for a given costume's skeleton and scale group (e.g. upper body, face)
void costumeTailor_GetCostumePresetScales(NOCONST(PlayerCostume) *pPCCostume, PCPresetScaleValueGroup ***peaPresets, const char* pcTag)
{
	PCSkeletonDef *pSkel = GET_REF(pPCCostume->hSkeleton);
	if (pSkel)
	{
		int i;
		for (i = 0; i < eaSize(&pSkel->eaPresets); i++)
		{
			PCPresetScaleValueGroup *pPreset = pSkel->eaPresets[i];
			if(stricmp(pPreset->pcTag, pcTag) == 0)
			{
				eaPush(peaPresets, pPreset);
			}
		}
	}
}

// Get the texwords keys
void costumeTailor_GetValidTexWordsKeysFromTexture(PCTextureDef *pTex, const char ***peaKeys)
{
	int i;

	if (pTex->pcTexWordsKey) {
		eaPushUnique(peaKeys, pTex->pcTexWordsKey);
	}

	for(i=eaSize(&pTex->eaExtraSwaps)-1; i>=0; --i) {
		if (pTex->eaExtraSwaps[i]->pcTexWordsKey) {
			eaPushUnique(peaKeys, pTex->eaExtraSwaps[i]->pcTexWordsKey);
		}
	}
}

// Get the texwords keys
void costumeTailor_GetValidTexWordsKeys(NOCONST(PlayerCostume) *pPCCostume, const char ***peaKeys)
{
	int i;

	for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
		NOCONST(PCPart) *pPart = pPCCostume->eaParts[i];
		PCTextureDef *pTex;

		pTex = GET_REF(pPart->hPatternTexture);
		if (pTex) {
			costumeTailor_GetValidTexWordsKeysFromTexture(pTex, peaKeys);
		}

		pTex = GET_REF(pPart->hDetailTexture);
		if (pTex) {
			costumeTailor_GetValidTexWordsKeysFromTexture(pTex, peaKeys);
		}

		pTex = GET_REF(pPart->hSpecularTexture);
		if (pTex) {
			costumeTailor_GetValidTexWordsKeysFromTexture(pTex, peaKeys);
		}

		pTex = GET_REF(pPart->hDiffuseTexture);
		if (pTex) {
			costumeTailor_GetValidTexWordsKeysFromTexture(pTex, peaKeys);
		}

		if (pPart->pMovableTexture) {
			pTex = GET_REF(pPart->pMovableTexture->hMovableTexture);
			if (pTex) {
				costumeTailor_GetValidTexWordsKeysFromTexture(pTex, peaKeys);
			}
		}
	}
}

// Check if a given geometry has been unlocked
bool costumeTailor_GeometryIsUnlocked(PlayerCostume **eaUnlockedCostumes, PCGeometryDef *pGeoDef)
{
	int i, j;
	
	if (!pGeoDef) {
		return false;
	}

	for(i=eaSize(&eaUnlockedCostumes)-1; i>=0; i--) {
		PCPart **eaParts = (PCPart**)(eaUnlockedCostumes[i]->eaParts);
		for (j=eaSize(&eaParts)-1; j>=0; j--) {
			if (GET_REF(eaParts[j]->hGeoDef) == pGeoDef) {
				return true;
			}
		}
	}
	
	return false;
}

// Check if a given material has been unlocked for this geometry
bool costumeTailor_MaterialIsUnlocked(PlayerCostume **eaUnlockedCostumes, PCMaterialDef *pMatDef, PCGeometryDef *pGeoDef)
{
	int i, j;

	if (!pGeoDef || !pMatDef) {
		return false;
	}
	
	for (i=eaSize(&eaUnlockedCostumes)-1; i>=0; --i) {
		PCPart **eaParts = (PCPart**)(eaUnlockedCostumes[i]->eaParts);
		bool bUnlockAll = SAFE_MEMBER2(eaUnlockedCostumes[i], pArtistData, bUnlockAll) || SAFE_MEMBER2(eaUnlockedCostumes[i], pArtistData, bUnlockAllCMat);
		for (j=eaSize(&eaParts)-1; j>=0; --j) {
			if ((bUnlockAll || GET_REF(eaParts[j]->hGeoDef) == pGeoDef) && GET_REF(eaParts[j]->hMatDef) == pMatDef) {
				return true;
			}
		}
	}
	
	return false;
}

// Check if a given texture has been unlocked uigenfor this geometry and material
bool costumeTailor_TextureIsUnlocked(PlayerCostume **eaUnlockedCostumes, PCTextureDef *pTexDef, PCMaterialDef *pMatDef, PCGeometryDef *pGeoDef)
{
	int i, j;

	if (!pGeoDef || !pMatDef || !pTexDef) {
		return false;
	}
	
	for (i=eaSize(&eaUnlockedCostumes)-1; i>=0; --i) {
		PCPart **eaParts = (PCPart**)(eaUnlockedCostumes[i]->eaParts);
		bool bUnlockAll = SAFE_MEMBER2(eaUnlockedCostumes[i], pArtistData, bUnlockAll) || SAFE_MEMBER2(eaUnlockedCostumes[i], pArtistData, bUnlockAllCTex);
		for (j=eaSize(&eaParts)-1; j>=0; --j) {
			if ((bUnlockAll || (GET_REF(eaParts[j]->hGeoDef) == pGeoDef && GET_REF(eaParts[j]->hMatDef) == pMatDef))
				&& (GET_REF(eaParts[j]->hPatternTexture) == pTexDef 
					|| GET_REF(eaParts[j]->hDetailTexture) == pTexDef 
					|| GET_REF(eaParts[j]->hDiffuseTexture) == pTexDef 
					|| GET_REF(eaParts[j]->hSpecularTexture) == pTexDef 
					|| (eaParts[j]->pMovableTexture && GET_REF(eaParts[j]->pMovableTexture->hMovableTexture) == pTexDef))) 
			{
				return true;
			}
		}
	}
	
	return false;
}

bool costumeTailor_StanceIsUnlocked(PlayerCostume **eaUnlockedCostumes, PCStanceInfo *pStanceDef)
{
	int i;

	if (!pStanceDef) {
		return false;
	}

	for(i=eaSize(&eaUnlockedCostumes)-1; i>=0; i--) {
		if (!eaUnlockedCostumes[i]->pcStance) continue;
		if (!stricmp(eaUnlockedCostumes[i]->pcStance, pStanceDef->pcName))
		{
			return true;
		}
	}

	return false;
}

// Pick a valid stance for the costume
// Leaves the costume alone if the current stance is legal
void costumeTailor_PickValidStance(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll)
{
	int i;
	PCStanceInfo **eaStances = NULL;
	costumeTailor_GetValidStances(pPCCostume, pSpecies, pSlotType, &eaStances, true, eaUnlockedCostumes, bUnlockAll);

	// Pick default if had no stance before
	if (!pPCCostume->pcStance) {
		const PCSlotStanceStruct *pStance = costumeTailor_GetStanceFromSlot(pSlotType, costumeTailor_GetGenderFromSpeciesOrCostume(pSpecies, (PlayerCostume *)pPCCostume));
		
		if(pStance && pStance->pcDefaultStance)
		{
			pPCCostume->pcStance = pStance->pcDefaultStance;
		}
		else if (pSpecies && pSpecies->pcDefaultStance)
		{
			pPCCostume->pcStance = pSpecies->pcDefaultStance;
		}
		else
		{
			PCSkeletonDef *pSkel = GET_REF(pPCCostume->hSkeleton);
			if (pSkel && pSkel->pcDefaultStance) {
				pPCCostume->pcStance = pSkel->pcDefaultStance;
			}
		}
	}

	// See if current choice is legal
	if (pPCCostume->pcStance) {
		for (i = 0; i < eaSize(&eaStances); i++) {
			PCStanceInfo *pStance = eaStances[i];
			if (!stricmp(pStance->pcName, pPCCostume->pcStance)) {
				eaDestroy(&eaStances);
				return; // Current choice is legal
			}
		}
	}

	// Otherwise use the first stance.
	if (eaSize(&eaStances)) {
		pPCCostume->pcStance = eaStances[0]->pcName;
	} else {
		pPCCostume->pcStance = NULL;
	}
	eaDestroy(&eaStances);
}

PCStanceInfo *costumeTailor_GetStance(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType)
{
	PCSkeletonDef *pSkel = GET_REF(pPCCostume->hSkeleton);
	int i;
	const PCSlotStanceStruct *pStance;

	if (!pSkel) {
		return NULL;
	}

	if (!pPCCostume->pcStance) {
		return NULL;
	}

	pStance = costumeTailor_GetStanceFromSlot(pSlotType, costumeTailor_GetGenderFromSpeciesOrCostume(pSpecies, (PlayerCostume *)pPCCostume));
	if(pStance)
	{
		for (i = 0; i < eaSize(&pStance->eaStanceInfo); i++) {
			if (!stricmp(pStance->eaStanceInfo[i]->pcName, pPCCostume->pcStance)) {
				return pStance->eaStanceInfo[i];
			}
		}
	}
	else if (pSpecies && eaSize(&pSpecies->eaStanceInfo))
	{
		for (i = 0; i < eaSize(&pSpecies->eaStanceInfo); i++) {
			if (!stricmp(pSpecies->eaStanceInfo[i]->pcName, pPCCostume->pcStance)) {
				return pSpecies->eaStanceInfo[i];
			}
		}
	}
	else
	{
		for (i = 0; i < eaSize(&pSkel->eaStanceInfo); i++) {
			if (!stricmp(pSkel->eaStanceInfo[i]->pcName, pPCCostume->pcStance)) {
				return pSkel->eaStanceInfo[i];
			}
		}
	}

	// If the costume doesn't have a stance but the skeleton does, give it one.
	if (i > 0) {
		if (pSpecies && eaSize(&pSpecies->eaStanceInfo))
		{
			pPCCostume->pcStance = pSpecies->eaStanceInfo[0]->pcName;
			return pSpecies->eaStanceInfo[0];
		}
		else
		{
			pPCCostume->pcStance = pSkel->eaStanceInfo[0]->pcName;
			return pSkel->eaStanceInfo[0];
		}
	}
	else {
		return NULL;
	}
}


// Pick a valid voice for the costume
// Leaves the costume alone if the current voice is legal
void costumeTailor_PickValidVoice(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, GameAccountDataExtract *pExtract)
{
	int i;
	int iRand;
	PCVoice **eaVoices = NULL;

	costumeTailor_GetValidVoices(pPCCostume, pSpecies, &eaVoices, true, pExtract);
	if (!eaSize(&eaVoices))
	{
		eaDestroy(&eaVoices);
		REMOVE_HANDLE(pPCCostume->hVoice);
		return;
	}

	if (GET_REF(pPCCostume->hVoice))
	{
		// See if current choice is legal
		PCVoice *pVoice = GET_REF(pPCCostume->hVoice);
		for (i = 0; i < eaSize(&eaVoices); i++)
		{
			if (!stricmp(eaVoices[i]->pcName, pVoice->pcName)) {
				eaDestroy(&eaVoices);
				return; // Current choice is legal
			}
		}
	}

	iRand = randomIntRange(0, eaSize(&eaVoices)-1);
	SET_HANDLE_FROM_REFERENT("CostumeVoice", eaVoices[iRand], pPCCostume->hVoice);

	eaDestroy(&eaVoices);
}


// Pick a category for the region
// Leaves the category alone if the current category is legal
void costumeTailor_PickValidCategoryForRegion(NOCONST(PlayerCostume) *pPCCostume, PCRegion *pRegion, PCCategory **eaCategories, bool bIgnoreRegCatNull)
{
	NOCONST(PCRegionCategory) *pRegCat = NULL;
	int i;

	// Get the Region/Category
	for(i=eaSize(&pPCCostume->eaRegionCategories)-1; i>=0; --i) {
		if (GET_REF(pPCCostume->eaRegionCategories[i]->hRegion) == pRegion) {
			pRegCat = pPCCostume->eaRegionCategories[i];
			break;
		}
	}

	// special case when editing current character in tailor where weapons don't have a pRegCat
	if(bIgnoreRegCatNull && !pRegCat)
	{
		return;
	}
	
	if (!pRegCat) {
		if (i < 0)
		{
			Errorf("Region %s not found on costume.  Costume (%s) is invalid and won't function in game play.", pRegion->pcName, pPCCostume->pcName);
		}
		else
		{
			Errorf("No category defined for region %s.  Costume (%s) is invalid and won't function in game play.", pRegion->pcName, pPCCostume->pcName);
		}
		return;
	}

	// See if current choice is legal
	if (pRegCat) {
		for(i=eaSize(&eaCategories)-1; i>=0; --i) {
			if (eaCategories[i] == GET_REF(pRegCat->hCategory)) {
				return; // Current choice is legal
			}
		}
	}

	if (eaSize(&eaCategories) > 0) {
		// Pick first choice in list
		SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, eaCategories[0], pRegCat->hCategory);
	} else {
		REMOVE_HANDLE(pRegCat->hCategory);
	}
}


// Pick a valid category for the part
// Leaves the part alone if the current category is legal
void costumeTailor_PickValidCategory(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, PCCategory **eaCategories)
{
	NOCONST(PCRegionCategory) *pRegCat = NULL;
	PCBoneDef *pBone;
	int i;

	// Get the bone
	pBone = GET_REF(pPart->hBoneDef);
	if (!pBone) {
		return;
	}

	// Get current Region/Category
	for(i=eaSize(&pPCCostume->eaRegionCategories)-1; i>=0; --i) {
		if (GET_REF(pPCCostume->eaRegionCategories[i]->hRegion) == GET_REF(pBone->hRegion)) {
			pRegCat = pPCCostume->eaRegionCategories[i];
			break;
		}
	}

	// Create new entry if none present
	if (!pRegCat) {
		pRegCat = StructCreateNoConst(parse_PCRegionCategory);
		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, GET_REF(pBone->hRegion), pRegCat->hRegion);
		eaPush(&pPCCostume->eaRegionCategories, pRegCat);
	}

	// See if current choice is legal
	if (GET_REF(pRegCat->hCategory)) {
		for(i=eaSize(&eaCategories)-1; i>=0; --i) {
			if (eaCategories[i] == GET_REF(pRegCat->hCategory)) {
				return; // Current choice is legal
			}
		}
	}

	assertmsg(eaSize(&eaCategories) > 0, "No legal categories for region");

	// Pick first choice in list
	SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, eaCategories[0], pRegCat->hCategory);
}

bool costumeTailor_IsBoneRequired(NOCONST(PlayerCostume) *pPCCostume, PCBoneDef *pBone, SpeciesDef *pSpecies)
{
	int i;
	PCSkeletonDef *pSkel;

	// Find if bone is required.  Not required if costume is unrestricted.
	if (pBone &&
		(pPCCostume->eCostumeType != kPCCostumeType_Unrestricted) &&
		(pPCCostume->eCostumeType != kPCCostumeType_Item) &&
		(pPCCostume->eCostumeType != kPCCostumeType_Overlay))
	{
		pSkel = GET_REF(pPCCostume->hSkeleton);
		if (pSkel) {
			for(i=eaSize(&pSkel->eaRequiredBoneDefs)-1; i>=0; --i) {
				if (GET_REF(pSkel->eaRequiredBoneDefs[i]->hBone) == pBone) {
					return true;
				}
			}
		}
		if (pBone) {
			PCCategory *pCat = costumeTailor_GetCategoryForRegion((PlayerCostume*)pPCCostume, GET_REF(pBone->hRegion));
			if (pCat) {
				for(i=eaSize(&pCat->eaRequiredBones)-1; i>=0; --i) {
					if (GET_REF(pCat->eaRequiredBones[i]->hBone) == pBone) {
						return true;
					}
				}
			}
		}
		if (pSpecies)
		{
			for(i=eaSize(&pSpecies->eaBoneData)-1; i>=0; --i) {
				PCBoneDef *bone = GET_REF(pSpecies->eaBoneData[i]->hBone);
				if (!bone) continue;
				if (bone == pBone) {
					if (pSpecies->eaBoneData[i]->bRequires) {
						return true;
					}
					break;
				}
			}
		}
	}

	return false;
}

// Pick a valid geometry for the part
// Leaves the part alone if the current geometry is legal
void costumeTailor_PickValidGeometry(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, PCGeometryDef **eaGeos, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll)
{
	PCGeometryDef *pGeo = NULL;
	int i;

	if (pPart) {
		pGeo = GET_REF(pPart->hGeoDef);
	}
	if (pGeo) {
		for(i=eaSize(&eaGeos)-1; i>=0; --i) {
			if (pGeo == eaGeos[i]) {
				// Already valid, so return
				if (pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && !pPart->pClothLayer) {
					costumeTailor_PartFillClothLayer(pPCCostume, pPart, pSpecies, eaUnlockedCostumes, bUnlockAll);
				}
				return;
			}
		}

		// Not valid so clear old choice
		SET_HANDLE_FROM_STRING(g_hCostumeGeometryDict, "None", pPart->hGeoDef);
	}

	if (eaSize(&eaGeos) && pPart) {
		PCBoneDef *pBone = GET_REF(pPart->hBoneDef);
		bool bRequired = costumeTailor_IsBoneRequired(pPCCostume, pBone, pSpecies) || (!eaSize(&eaGeos)) || stricmp(eaGeos[0]->pcName, "None");

		if (bRequired) {
			bool bFound = false;

			// If required and have a default, use the default
			if (pBone && GET_REF(pBone->hDefaultGeo)) {
				for(i=eaSize(&eaGeos)-1; i>=0; --i) {
					if (eaGeos[i] == GET_REF(pBone->hDefaultGeo)) {
						SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, GET_REF(pBone->hDefaultGeo), pPart->hGeoDef);
						bFound = true;
						break;
					}
				}
			}
			// If no default found, then use first geo in the list
			if (!bFound) {
				SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, eaGeos[0], pPart->hGeoDef);
			}
		}

		if (GET_REF(pPart->hGeoDef) && GET_REF(pPart->hGeoDef)->pClothData && GET_REF(pPart->hGeoDef)->pClothData->bIsCloth && GET_REF(pPart->hGeoDef)->pClothData->bHasClothBack && !pPart->pClothLayer) {
			costumeTailor_PartFillClothLayer(pPCCostume, pPart, pSpecies, eaUnlockedCostumes, bUnlockAll);
		} else if (pPart->pClothLayer) {
			StructDestroyNoConst(parse_PCPart, pPart->pClothLayer);
			pPart->pClothLayer = NULL;
		}
	}
}


// Get a part for a given bone
NOCONST(PCPart) *costumeTailor_GetPartByBone(NOCONST(PlayerCostume) *pPCCostume, PCBoneDef *pBone, PCLayer *pLayer)
{
	int i, j;

	if (!pPCCostume) {
		return NULL;
	}

	for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
		if (pBone == GET_REF(pPCCostume->eaParts[i]->hBoneDef)) {
			if (!pLayer || (pLayer->eLayerArea == kPCLayerArea_Main)) {
				if (!pLayer || (pLayer->eLayerType == kPCLayerType_Front)) {
					return pPCCostume->eaParts[i];
				} else {
					return pPCCostume->eaParts[i]->pClothLayer;
				}
			} else {
				NOCONST(PCPart) **eaChildParts = NULL;
				assert(pBone);

				// Child layer, so get child part
				for(j=eaSize(&pBone->eaChildBones)-1; j>=0; --j) {
					if ((pLayer == GET_REF(pBone->eaChildBones[j]->hChildLayerFront)) || 
						(pLayer == GET_REF(pBone->eaChildBones[j]->hChildLayerBack))) {
						costumeTailor_GetChildParts(pPCCostume, pPCCostume->eaParts[i], pLayer, &eaChildParts);
						break;
					}
				}
				if (eaSize(&eaChildParts) > 0) {
					NOCONST(PCPart) *pChildPart = eaChildParts[0];
					eaDestroy(&eaChildParts);

					if (pLayer->eLayerType == kPCLayerType_Back) {
						return pChildPart->pClothLayer;
					} else {
						return pChildPart;
					}
				}
			}
		}
	}
	return NULL;
}


// Get the child parts of a part
void costumeTailor_GetChildParts(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, PCLayer *pLayer, NOCONST(PCPart) ***peaParts)
{
	PCBoneDef *pBone, *pChildBone;
	int i, j;

	if (!pPart) {
		return;
	}
	pBone = GET_REF(pPart->hBoneDef);
	if (!pBone) {
		return;
	}
	for(i=0; i<eaSize(&pBone->eaChildBones); ++i) {
		pChildBone = GET_REF(pBone->eaChildBones[i]->hChildBone);
		if (!pChildBone) {
			return;
		}
		for(j=eaSize(&pPCCostume->eaParts)-1; j>=0; --j) {
			if (GET_REF(pPCCostume->eaParts[j]->hBoneDef) == pChildBone) {
				if (!pLayer || (pLayer == GET_REF(pBone->eaChildBones[i]->hChildLayerFront)) || (pLayer == GET_REF(pBone->eaChildBones[i]->hChildLayerBack))) {
					eaPush(peaParts, pPCCostume->eaParts[j]);
				}
			}
		}
	}
}


// Pick all valid child geometries for the part
// Picks for all child bones if no bone is provided
// Leaves the part alone if the current geometries are legal
void costumeTailor_PickValidChildGeometry(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, PCBoneDef *pChildBone, SpeciesDef *pSpecies, PCGeometryDef **eaGeos, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll)
{	
	NOCONST(PCPart) **eaChildParts = NULL;
	NOCONST(PCPart) *pChildPart = NULL;
	PCGeometryDef *pParentGeo;
	PCGeometryDef *pGeo = NULL;
	int i,j,k,l;
	bool bHasNoneGeo = eaSize(&eaGeos) && !stricmp(eaGeos[0]->pcName, "None") ? true : false;

	// Find the child parts
	costumeTailor_GetChildParts(pPCCostume, pPart, NULL, &eaChildParts);
	if (!eaSize(&eaChildParts)) {
		return;
	}

	// Iterate the child parts and pick valid values
	for(i=eaSize(&eaChildParts)-1; i>=0; --i) {
		pChildPart = eaChildParts[i];

		if (pChildBone && (pChildBone != GET_REF(pChildPart->hBoneDef))) {
			continue;
		}

		pGeo = NULL;
		//Check if Child is part of a bone group; if so take the geo forced by that bone group
		{
			PCSkeletonDef *skel = GET_REF(pPCCostume->hSkeleton);
			PCBoneDef *temp, *bone = pChildBone;
			NOCONST(PCPart) *part;
			if (skel && bone)
			{
				PCBoneGroup **bg = skel->eaBoneGroups;
				if (bg)
				{
					for(k=eaSize(&bg)-1; k>=0; --k)
					{
						if (!(bg[k]->eBoneGroupFlags & kPCBoneGroupFlags_MatchGeos)) continue;
						for (l = eaSize(&bg[k]->eaBoneInGroup)-1; l >= 0; --l)
						{
							if (bone == GET_REF(bg[k]->eaBoneInGroup[l]->hBone))
							{
								break;
							}
						}
						if (l>=0) break;
					}

					if (k>=0)
					{
						int g;
						PCGeometryDef **eaGeoDefs = NULL;
						PCCategory *pCat;
						//Child bone is in bone group; find bone to copy
						for (l = eaSize(&bg[k]->eaBoneInGroup)-1; l >= 0; --l)
						{
							temp = GET_REF(bg[k]->eaBoneInGroup[l]->hBone);
							if (bone == temp || !temp) continue;
							part = costumeTailor_GetPartByBone(pPCCostume, temp, NULL);
							if (!part) continue;
							pGeo = GET_REF(part->hGeoDef);
							if (!pGeo) continue;
							pCat = costumeTailor_GetCategoryForRegion((PlayerCostume*)pPCCostume, GET_REF(bone->hRegion));
							costumeTailor_GetValidGeos(pPCCostume, GET_REF(pPCCostume->hSkeleton), bone, pCat, pSpecies, eaUnlockedCostumes, &eaGeoDefs, false, false, false, bUnlockAll);
							g = costumeTailor_GetMatchingGeoIndex(pGeo, eaGeoDefs);
							if (g < 0)
							{
								eaDestroy(&eaGeoDefs);
								continue;
							}
							SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, eaGeoDefs[g], pChildPart->hGeoDef);
							eaDestroy(&eaGeoDefs);
							break;
						}
					}
				}
			}
		}

		pGeo = GET_REF(pChildPart->hGeoDef);
		if (pGeo) {
			bool bValid = false;

			for(j=eaSize(&eaGeos)-1; j>=0; --j) {
				if (pGeo == eaGeos[j]) {
					// Already valid
					if (pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && !pChildPart->pClothLayer) {
						costumeTailor_PartFillClothLayer(pPCCostume, pChildPart, pSpecies, eaUnlockedCostumes, bUnlockAll);
					}
					bValid = true;
					break;
				}
			}
			if (bValid) {
				continue;
			}

			// Not valid so clear old choice
			SET_HANDLE_FROM_STRING(g_hCostumeGeometryDict, "None", pChildPart->hGeoDef);
		}
		pGeo = NULL;

		if (eaSize(&eaGeos)) {
			pParentGeo = GET_REF(pPart->hGeoDef);
			if (pParentGeo && pParentGeo->pOptions) {
				for(j=eaSize(&pParentGeo->pOptions->eaChildGeos)-1; j>=0; --j) {
					PCGeometryChildDef *pChildDef = pParentGeo->pOptions->eaChildGeos[j];

					//If required then make sure we have one
					if ((pChildDef->bRequiresChild && !pGeo) || !bHasNoneGeo) {
						bool bFound = false;

						if (GET_REF(pChildDef->hDefaultChildGeo)) {
							for(k=eaSize(&eaGeos)-1; k>=0; --k) {
								if (eaGeos[k] == GET_REF(pChildDef->hDefaultChildGeo)) {
									pGeo = GET_REF(pChildDef->hDefaultChildGeo);
									bFound = true;
									break;
								}
							}
						}
						if (!bFound) {
							pGeo = eaGeos[0];
						}

						assert(pGeo);
						SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, pGeo, pChildPart->hGeoDef);
					}
				}
			}

			if (GET_REF(pChildPart->hGeoDef) && GET_REF(pChildPart->hGeoDef)->pClothData && GET_REF(pChildPart->hGeoDef)->pClothData->bIsCloth && GET_REF(pChildPart->hGeoDef)->pClothData->bHasClothBack) {
				if (!pChildPart->pClothLayer) {
					costumeTailor_PartFillClothLayer(pPCCostume, pChildPart, pSpecies, eaUnlockedCostumes, bUnlockAll);
				}
			} else if (pChildPart->pClothLayer) {
				StructDestroyNoConst(parse_PCPart, pChildPart->pClothLayer);
				pChildPart->pClothLayer = NULL;
			}
		}
	}
	eaDestroy(&eaChildParts);
}


// Pick a valid layer for the part.  Deletes invalid layer.
// Leaves the part alone if the current layer is legal
PCLayer *costumeTailor_PickValidLayer(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, PCLayer *pLayer, PCLayer **eaLayers)
{
	int i;

	// TODO: Delete layers that are not appropriate for the geometry

	if (pLayer) {
		for(i=eaSize(&eaLayers)-1; i>=0; --i) {
			if (pLayer == eaLayers[i]) {
				// Already valid, so return
				return pLayer;
			}
		}
	}

	if (eaSize(&eaLayers)) {
		return eaLayers[0];
	}

	return NULL;
}


// Pick a valid material for the part
// Leaves the part alone if the current material is legal
void costumeTailor_PickValidMaterial(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, NOCONST(PCPart) *pPart, PlayerCostume **eaUnlockedCostumes, PCMaterialDef **eaMats, bool bUnlockAll, bool bApplyMaterialRules)
{
	PCMaterialDef *pMat = NULL;
	int i, j;

	if (pPart) {
		pMat = GET_REF(pPart->hMatDef);
	}
	if (pMat) {
		for(i=eaSize(&eaMats)-1; i>=0; --i) {
			if (pMat == eaMats[i]) {
				if (IS_PLAYER_COSTUME(pPCCostume) && pPart->pCustomColors)
				{
					//If anything in the material's data is invalid fix it
					for(j = 0; j < 4; ++j)
					{
						if(pPart->pCustomColors->glowScale[j] != 0 && !(pMat->pColorOptions && pMat->pColorOptions->bAllowGlow[j]))
						{
							pPart->pCustomColors->glowScale[j] = 0;
						}
						if(pPart->pCustomColors->reflection[j] != 0 && !(pMat->pColorOptions && pMat->pColorOptions->bAllowReflection[j]))
						{
							pPart->pCustomColors->reflection[j] = 0;
						}
						if (pPart->pCustomColors->specularity[j] != 0 && !(pMat->pColorOptions && pMat->pColorOptions->bAllowSpecularity[j]))
						{
							pPart->pCustomColors->specularity[j] = 0;
						}
					}
				}
				if (bApplyMaterialRules) costumeTailor_PartApplyMaterialRules(pPCCostume, pPart, pSpecies, eaUnlockedCostumes, bUnlockAll);
				// Already valid, so return
				return;
			}
		}

		// Not valid so clear old choice
		SET_HANDLE_FROM_STRING(g_hCostumeMaterialDict, "None", pPart->hMatDef);
	}

	if (eaSize(&eaMats) && pPart) {
		PCGeometryDef *pGeo = GET_REF(pPart->hGeoDef);
		bool bFound = false;
		if (pGeo && GET_REF(pGeo->hDefaultMaterial)) {
			for(i=eaSize(&eaMats)-1; i>=0; --i) {
				if (eaMats[i] == GET_REF(pGeo->hDefaultMaterial)) {
					SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, GET_REF(pGeo->hDefaultMaterial), pPart->hMatDef);
					bFound = true;
					break;
				}
			}
		}
		if (!bFound) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, eaMats[0], pPart->hMatDef);
		}
	}

	if (bApplyMaterialRules && pPart && GET_REF(pPart->hMatDef) && GET_REF(pPart->hMatDef)->pcName && stricmp(GET_REF(pPart->hMatDef)->pcName,"None"))
	{
		costumeTailor_PartApplyMaterialRules(pPCCostume, pPart, pSpecies, eaUnlockedCostumes, bUnlockAll);
	}
}


// Pick a valid material for the part
// Leaves the part alone if the current texture is legal
void costumeTailor_PickValidTexture(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, PCTextureType eType, PCTextureDef **eaTexs)
{
	int i;
	PCMaterialDef *pMat = NULL;
	PCTextureDef *pTex = NULL;
	
	if (pPart) {
		pMat = GET_REF(pPart->hMatDef);
	}

	switch(eType) {
		case kPCTextureType_Pattern:
		{
			if (pPart) {
				pTex = GET_REF(pPart->hPatternTexture);
			}
			if (pTex) {
				for(i=eaSize(&eaTexs)-1; i>=0; --i) {
					if (pTex == eaTexs[i]) {
						// Already valid, so return
						return;
					}
				}

				// Not valid so clear old choice
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pPart->hPatternTexture);
				pTex = NULL;
			}
			// Pick default texture if it is legal
			if (!pTex && pMat && GET_REF(pMat->hDefaultPattern)) {
				for(i=eaSize(&eaTexs)-1; i>=0; --i) {
					if (eaTexs[i] == GET_REF(pMat->hDefaultPattern)) {
						SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, GET_REF(pMat->hDefaultPattern), pPart->hPatternTexture);
						return;
					}
				}
			}
			// Pick first texture if required to pick something
			if (!pTex && pMat && costumeTailor_DoesMaterialRequireType(GET_REF(pPart->hGeoDef), pMat, pSpecies, kPCTextureType_Pattern) && eaSize(&eaTexs)) {
				SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, eaTexs[0], pPart->hPatternTexture);
			}
			break;
		}
		case kPCTextureType_Detail:
		{
			if (pPart) {
				pTex = GET_REF(pPart->hDetailTexture);
			}
			if (pTex) {
				for(i=eaSize(&eaTexs)-1; i>=0; --i) {
					if (pTex == eaTexs[i]) {
						// Already valid, so return
						return;
					}
				}

				// Not valid so clear old choice
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pPart->hDetailTexture);
				pTex = NULL;
			}
			// Pick default texture if it is legal
			if (!pTex && pMat && GET_REF(pMat->hDefaultDetail)) {
				for(i=eaSize(&eaTexs)-1; i>=0; --i) {
					if (eaTexs[i] == GET_REF(pMat->hDefaultDetail)) {
						SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, GET_REF(pMat->hDefaultDetail), pPart->hDetailTexture);
						return;
					}
				}
			}
			// Pick first texture if required to pick something
			if (!pTex && pMat && costumeTailor_DoesMaterialRequireType(GET_REF(pPart->hGeoDef), pMat, pSpecies, kPCTextureType_Detail) && eaSize(&eaTexs)) {
				SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, eaTexs[0], pPart->hDetailTexture);
			}
			break;
		}
		case kPCTextureType_Specular:
		{
			if (pPart) {
				pTex = GET_REF(pPart->hSpecularTexture);
			}
			if (pTex) {
				for(i=eaSize(&eaTexs)-1; i>=0; --i) {
					if (pTex == eaTexs[i]) {
						// Already valid, so return
						return;
					}
				}

				// Not valid so clear old choice
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pPart->hSpecularTexture);
				pTex = NULL;
			}
			// Pick default texture if it is legal
			if (!pTex && pMat && GET_REF(pMat->hDefaultSpecular)) {
				for(i=eaSize(&eaTexs)-1; i>=0; --i) {
					if (eaTexs[i] == GET_REF(pMat->hDefaultSpecular)) {
						SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, GET_REF(pMat->hDefaultSpecular), pPart->hSpecularTexture);
						return;
					}
				}
			}
			// Pick first texture if required to pick something
			if (!pTex && pMat && costumeTailor_DoesMaterialRequireType(GET_REF(pPart->hGeoDef), pMat, pSpecies, kPCTextureType_Specular) && eaSize(&eaTexs)) {
				SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, eaTexs[0], pPart->hSpecularTexture);
			}
			break;
		}
		case kPCTextureType_Diffuse:
		{
			if (pPart) {
				pTex = GET_REF(pPart->hDiffuseTexture);
			}
			if (pTex) {
				for(i=eaSize(&eaTexs)-1; i>=0; --i) {
					if (pTex == eaTexs[i]) {
						// Already valid, so return
						return;
					}
				}

				// Not valid so clear old choice
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pPart->hDiffuseTexture);
				pTex = NULL;
			}
			// Pick default texture if it is legal
			if (!pTex && pMat && GET_REF(pMat->hDefaultDiffuse)) {
				for(i=eaSize(&eaTexs)-1; i>=0; --i) {
					if (eaTexs[i] == GET_REF(pMat->hDefaultDiffuse)) {
						SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, GET_REF(pMat->hDefaultDiffuse), pPart->hDiffuseTexture);
						return;
					}
				}
			}
			// Pick first texture if required to pick something
			if (!pTex && pMat && costumeTailor_DoesMaterialRequireType(GET_REF(pPart->hGeoDef), pMat, pSpecies, kPCTextureType_Diffuse) && eaSize(&eaTexs)) {
				SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, eaTexs[0], pPart->hDiffuseTexture);
			}
			break;
		}
		case kPCTextureType_Movable:
			{
				pTex = NULL;
				if (pPart && pPart->pMovableTexture) {
					pTex = GET_REF(pPart->pMovableTexture->hMovableTexture);
				}
				if (pTex && pPart->pMovableTexture) {
					for(i=eaSize(&eaTexs)-1; i>=0; --i) {
						if (pTex == eaTexs[i]) {
							// Already valid, so return
							return;
						}
					}

					// Not valid so clear old choice
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pPart->pMovableTexture->hMovableTexture);
					pTex = NULL;
				}
				// Pick default texture if it is legal
				if (!pTex && pMat && GET_REF(pMat->hDefaultMovable)) {
					for(i=eaSize(&eaTexs)-1; i>=0; --i) {
						if (eaTexs[i] == GET_REF(pMat->hDefaultMovable)) {
							if (!pPart->pMovableTexture) {
								pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
							}
							SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, GET_REF(pMat->hDefaultMovable), pPart->pMovableTexture->hMovableTexture);
							return;
						}
					}
				}
				// Pick first texture if required to pick something
				if (!pTex && pMat && costumeTailor_DoesMaterialRequireType(GET_REF(pPart->hGeoDef), pMat, pSpecies, kPCTextureType_Movable) && eaSize(&eaTexs)) {
					if (!pPart->pMovableTexture) {
						pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
					}
					SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, eaTexs[0], pPart->pMovableTexture->hMovableTexture);
				}
				break;
			}
		default:
			assertmsg(0, "Unexpected value");
	}
}

bool costumeTailor_PartHasBadGuildEmblem(NOCONST(PCPart) *pPart, Guild *pGuild)
{
	PCBoneDef *pBone = pPart ? GET_REF(pPart->hBoneDef) : NULL;
	bool bGuildTexture = false;

	if (pGuild && pBone && pBone->bIsGuildEmblemBone && pGuild->pGuildBadEmblem)
	{
		bGuildTexture = true;
		if (pGuild->pGuildBadEmblem->pcEmblem && *pGuild->pGuildBadEmblem->pcEmblem)
		{
			if (GET_REF(pPart->hPatternTexture) != RefSystem_ReferentFromString(g_hCostumeTextureDict, pGuild->pGuildBadEmblem->pcEmblem))
			{
				bGuildTexture = false;
			}
		}
		else if (REF_STRING_FROM_HANDLE(pPart->hPatternTexture) && (stricmp("None",REF_STRING_FROM_HANDLE(pPart->hPatternTexture)) != 0))
		{
			bGuildTexture = false;
		}
		if (bGuildTexture)
		{
			if (pGuild->pGuildBadEmblem->pcEmblem3 && *pGuild->pGuildBadEmblem->pcEmblem3)
			{
				if (GET_REF(pPart->hDetailTexture) != RefSystem_ReferentFromString(g_hCostumeTextureDict, pGuild->pGuildBadEmblem->pcEmblem3))
				{
					bGuildTexture = false;
				}
			}
			else if (REF_STRING_FROM_HANDLE(pPart->hDetailTexture) && (stricmp("None",REF_STRING_FROM_HANDLE(pPart->hDetailTexture)) != 0))
			{
				bGuildTexture = false;
			}
		}
		if (bGuildTexture)
		{
			if (pGuild->pGuildBadEmblem->pcEmblem2 && *pGuild->pGuildBadEmblem->pcEmblem2)
			{
				if ((!pPart->pMovableTexture) || GET_REF(pPart->pMovableTexture->hMovableTexture) != RefSystem_ReferentFromString(g_hCostumeTextureDict, pGuild->pGuildBadEmblem->pcEmblem2))
				{
					bGuildTexture = false;
				}
			}
			else if (pPart->pMovableTexture && REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture) && (stricmp("None",REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture)) != 0))
			{
				bGuildTexture = false;
			}
		}
		if (bGuildTexture)
		{
			if (pGuild->pGuildBadEmblem->iEmblemColor0 != *((U32*)pPart->color2) ||
				pGuild->pGuildBadEmblem->iEmblemColor1 != *((U32*)pPart->color3) ||
				pGuild->pGuildBadEmblem->iEmblem2Color0 != *((U32*)pPart->color0) ||
				pGuild->pGuildBadEmblem->iEmblem2Color1 != *((U32*)pPart->color1))
			{
				bGuildTexture = false;
			}
		}
		if (bGuildTexture)
		{
			if (pPart->pTextureValues)
			{
				if (pGuild->pGuildBadEmblem->fEmblemRotation != pPart->pTextureValues->fPatternValue)
				{
					bGuildTexture = false;
				}
			}
			else
			{
				if (pGuild->pGuildBadEmblem->fEmblemRotation != 0)
				{
					bGuildTexture = false;
				}
			}
		}
		if (bGuildTexture)
		{
			if (pPart->pMovableTexture)
			{
				if (pGuild->pGuildBadEmblem->fEmblem2Rotation != pPart->pMovableTexture->fMovableRotation ||
					pGuild->pGuildBadEmblem->fEmblem2X != pPart->pMovableTexture->fMovableX ||
					pGuild->pGuildBadEmblem->fEmblem2Y != pPart->pMovableTexture->fMovableY ||
					pGuild->pGuildBadEmblem->fEmblem2ScaleX != pPart->pMovableTexture->fMovableScaleX ||
					pGuild->pGuildBadEmblem->fEmblem2ScaleY != pPart->pMovableTexture->fMovableScaleY)
				{
					bGuildTexture = false;
				}
			}
			else
			{
				if (pGuild->pGuildBadEmblem->fEmblem2Rotation != 0 ||
					pGuild->pGuildBadEmblem->fEmblem2X != 0 ||
					pGuild->pGuildBadEmblem->fEmblem2Y != 0 ||
					pGuild->pGuildBadEmblem->fEmblem2ScaleX != 1 ||
					pGuild->pGuildBadEmblem->fEmblem2ScaleY != 1)
				{
					bGuildTexture = false;
				}
			}
		}
	}

	return bGuildTexture;
}

bool costumeTailor_PartHasGuildEmblem(NOCONST(PCPart) *pPart, Guild *pGuild)
{
	PCBoneDef *pBone = pPart ? GET_REF(pPart->hBoneDef) : NULL;
	bool bGuildTexture = false;

	if (pGuild && pBone && pBone->bIsGuildEmblemBone)
	{
		bGuildTexture = true;
		if (pGuild->pcEmblem && *pGuild->pcEmblem)
		{
			if (GET_REF(pPart->hPatternTexture) != RefSystem_ReferentFromString(g_hCostumeTextureDict, pGuild->pcEmblem))
			{
				bGuildTexture = false;
			}
		}
		else if (REF_STRING_FROM_HANDLE(pPart->hPatternTexture) && (stricmp("None",REF_STRING_FROM_HANDLE(pPart->hPatternTexture)) != 0))
		{
			bGuildTexture = false;
		}
		if (bGuildTexture)
		{
			if (pGuild->pcEmblem3 && *pGuild->pcEmblem3)
			{
				if (GET_REF(pPart->hDetailTexture) != RefSystem_ReferentFromString(g_hCostumeTextureDict, pGuild->pcEmblem3))
				{
					bGuildTexture = false;
				}
			}
			else if (REF_STRING_FROM_HANDLE(pPart->hDetailTexture) && (stricmp("None",REF_STRING_FROM_HANDLE(pPart->hDetailTexture)) != 0))
			{
				bGuildTexture = false;
			}
		}
		if (bGuildTexture)
		{
			if (pGuild->pcEmblem2 && *pGuild->pcEmblem2)
			{
				if ((!pPart->pMovableTexture) || GET_REF(pPart->pMovableTexture->hMovableTexture) != RefSystem_ReferentFromString(g_hCostumeTextureDict, pGuild->pcEmblem2))
				{
					bGuildTexture = false;
				}
			}
			else if (pPart->pMovableTexture && REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture) && (stricmp("None",REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture)) != 0))
			{
				bGuildTexture = false;
			}
		}
	}

	return bGuildTexture;
}

static void costumeTailor_PickValidMaterialValues(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, PCGeometryDef *pGeo, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, bool bSortDisplay, bool bUnlockAll, bool bApplyMaterialRules, bool bInEditor, Guild *pGuild)
{
	PCMaterialDef **eaMats = NULL;
	PCTextureDef **eaPatternTex = NULL;
	PCTextureDef **eaDetailTex = NULL;
	PCTextureDef **eaSpecularTex = NULL;
	PCTextureDef **eaDiffuseTex = NULL;
	PCTextureDef **eaMovableTex = NULL;
	PCTextureType eFlags = 0;
	PCTextureDef *pTemp = NULL;
	PCBoneDef *pBone = GET_REF(pPart->hBoneDef);
	bool bGuildTexture = costumeTailor_PartHasGuildEmblem(pPart, pGuild);
	int i, j;

	// Validate material choice
	costumeTailor_GetValidMaterials(pCostume, pGeo, pSpecies, NULL, NULL, eaUnlockedCostumes, &eaMats, false, bSortDisplay, bUnlockAll);
	costumeTailor_PickValidMaterial(pCostume, pSpecies, pPart, eaUnlockedCostumes, eaMats, bUnlockAll, bApplyMaterialRules);
	eaDestroy(&eaMats);
	
	// Validate texture choice
	costumeTailor_GetValidTextures(pCostume, GET_REF(pPart->hMatDef), pSpecies, NULL, NULL, pGeo, NULL, eaUnlockedCostumes, kPCTextureType_Pattern, &eaPatternTex, false, bSortDisplay, bUnlockAll);
	costumeTailor_GetValidTextures(pCostume, GET_REF(pPart->hMatDef), pSpecies, NULL, NULL, pGeo, NULL, eaUnlockedCostumes, kPCTextureType_Detail, &eaDetailTex,  false, bSortDisplay, bUnlockAll);
	costumeTailor_GetValidTextures(pCostume, GET_REF(pPart->hMatDef), pSpecies, NULL, NULL, pGeo, NULL, eaUnlockedCostumes, kPCTextureType_Specular, &eaSpecularTex,  false, bSortDisplay, bUnlockAll);
	costumeTailor_GetValidTextures(pCostume, GET_REF(pPart->hMatDef), pSpecies, NULL, NULL, pGeo, NULL, eaUnlockedCostumes, kPCTextureType_Diffuse, &eaDiffuseTex,  false, bSortDisplay, bUnlockAll);
	costumeTailor_GetValidTextures(pCostume, GET_REF(pPart->hMatDef), pSpecies, NULL, NULL, pGeo, NULL, eaUnlockedCostumes, kPCTextureType_Movable, &eaMovableTex,  false, bSortDisplay, bUnlockAll);

	if (!bGuildTexture)
	{
		if (pBone && pBone->bIsGuildEmblemBone && (IS_PLAYER_COSTUME(pCostume) || !bInEditor))
		{
			//Strip invalid textures
			for(i=eaSize(&eaPatternTex)-1; i>=0; i--) {
				for (j = eaSize(&g_GuildEmblems.eaEmblems)-1; j >= 0; --j)
				{
					if (!stricmp(eaPatternTex[i]->pcName,REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[j]->hTexture))) {
						eaRemove(&eaPatternTex, i);
					}
				}
			}
		}
		costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Pattern, eaPatternTex);
	}
	eFlags |= costumeTailor_GetTextureFlags(GET_REF(pPart->hPatternTexture));

	if ((eFlags & kPCTextureType_Detail) == 0) {
		if (!bGuildTexture)
		{
			if (pBone && pBone->bIsGuildEmblemBone && (IS_PLAYER_COSTUME(pCostume) || !bInEditor))
			{
				//Strip invalid textures
				for(i=eaSize(&eaDetailTex)-1; i>=0; i--) {
					for (j = eaSize(&g_GuildEmblems.eaEmblems)-1; j >= 0; --j)
					{
						if (!stricmp(eaDetailTex[i]->pcName,REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[j]->hTexture))) {
							eaRemove(&eaDetailTex, i);
						}
					}
				}
			}
			costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Detail, eaDetailTex);
		}
		eFlags |= costumeTailor_GetTextureFlags(GET_REF(pPart->hDetailTexture));
	} else {
		REMOVE_HANDLE(pPart->hDetailTexture);
	}

	if ((eFlags & kPCTextureType_Specular) == 0) {
		costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Specular, eaSpecularTex);
		eFlags |= costumeTailor_GetTextureFlags(GET_REF(pPart->hSpecularTexture));
	} else {
		REMOVE_HANDLE(pPart->hSpecularTexture);
	}

	if ((eFlags & kPCTextureType_Diffuse) == 0) {
		costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Diffuse, eaDiffuseTex);
		eFlags |= costumeTailor_GetTextureFlags(GET_REF(pPart->hDiffuseTexture));
	} else {
		REMOVE_HANDLE(pPart->hDiffuseTexture);
	}

	if ((eFlags & kPCTextureType_Movable) == 0) {
		if (!bGuildTexture)
		{
			if (pBone && pBone->bIsGuildEmblemBone && (IS_PLAYER_COSTUME(pCostume) || !bInEditor))
			{
				//Strip invalid textures
				for(i=eaSize(&eaMovableTex)-1; i>=0; i--) {
					for (j = eaSize(&g_GuildEmblems.eaEmblems)-1; j >= 0; --j)
					{
						if (!stricmp(eaMovableTex[i]->pcName,REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[j]->hTexture))) {
							eaRemove(&eaMovableTex, i);
						}
					}
				}
			}
			costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Movable, eaMovableTex);
		}
	} else {
		REMOVE_HANDLE(pPart->pMovableTexture->hMovableTexture);
	}
	
	eaDestroy(&eaPatternTex);
	eaDestroy(&eaDetailTex);
	eaDestroy(&eaSpecularTex);
	eaDestroy(&eaDiffuseTex);
	eaDestroy(&eaMovableTex);
}

static void costumeTailor_PickValidColors(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, const PCSlotType *pSlotType, Guild *pGuild)
{
	PCBoneDef *pBone = GET_REF(pPart->hBoneDef);

	if (pBone && pBone->bIsGuildEmblemBone)
	{
		if (costumeTailor_PartHasGuildEmblem(pPart, pGuild))
		{
			*((U32*)pPart->color0) = pGuild->iEmblem2Color0;
			*((U32*)pPart->color1) = pGuild->iEmblem2Color1;
			*((U32*)pPart->color2) = pGuild->iEmblemColor0;
			*((U32*)pPart->color3) = pGuild->iEmblemColor1;
			return;
		}
	}

	{
		int flags = costumeTailor_IsColorValidForPart(pCostume, pSpecies, pSlotType, pPart, pPart->color0, pPart->color1, pPart->color2, pPart->color3, true);

		if (!(flags & kPCColorFlags_Color0))
		{
			costumeTailor_FindClosestColor(pPart->color0, costumeTailor_GetColorSetForPart(pCostume, pSpecies, pSlotType, pPart, 0), pPart->color0);
		}
		if (!(flags & kPCColorFlags_Color1))
		{
			costumeTailor_FindClosestColor(pPart->color1, costumeTailor_GetColorSetForPart(pCostume, pSpecies, pSlotType, pPart, 1), pPart->color1);
		}
		if (!(flags & kPCColorFlags_Color2))
		{
			costumeTailor_FindClosestColor(pPart->color2, costumeTailor_GetColorSetForPart(pCostume, pSpecies, pSlotType, pPart, 2), pPart->color2);
		}
		if (!(flags & kPCColorFlags_Color3))
		{
			costumeTailor_FindClosestColor(pPart->color3, costumeTailor_GetColorSetForPart(pCostume, pSpecies, pSlotType, pPart, 3), pPart->color3);
		}
	}
}

void costumeTailor_PickValidSlideValues(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, bool bInEditor, Guild *pGuild)
{
	PCBoneDef *pBone = pGuild ? GET_REF(pPart->hBoneDef) : NULL;

	if (pPart->pTextureValues)
	{
		if (pBone && pBone->bIsGuildEmblemBone && costumeTailor_PartHasGuildEmblem(pPart, pGuild))
		{
			pPart->pTextureValues->fPatternValue = pGuild->fEmblemRotation;
		}

		if (pPart->pTextureValues->fPatternValue < -100.0f) pPart->pTextureValues->fPatternValue = -100.0f;
		else if (pPart->pTextureValues->fPatternValue > 100.0f) pPart->pTextureValues->fPatternValue = 100.0f;

		if (pPart->pTextureValues->fDetailValue < -100.0f) pPart->pTextureValues->fDetailValue = -100.0f;
		else if (pPart->pTextureValues->fDetailValue > 100.0f) pPart->pTextureValues->fDetailValue = 100.0f;

		if (pPart->pTextureValues->fSpecularValue < -100.0f) pPart->pTextureValues->fSpecularValue = -100.0f;
		else if (pPart->pTextureValues->fSpecularValue > 100.0f) pPart->pTextureValues->fSpecularValue = 100.0f;

		if (pPart->pTextureValues->fDiffuseValue < -100.0f) pPart->pTextureValues->fDiffuseValue = -100.0f;
		else if (pPart->pTextureValues->fDiffuseValue > 100.0f) pPart->pTextureValues->fDiffuseValue = 100.0f;
	}

	if (pPart->pMovableTexture)
	{
		if (pBone && pBone->bIsGuildEmblemBone && costumeTailor_PartHasGuildEmblem(pPart, pGuild))
		{
			pPart->pMovableTexture->fMovableRotation = pGuild->fEmblem2Rotation;
			pPart->pMovableTexture->fMovableX = pGuild->fEmblem2X;
			pPart->pMovableTexture->fMovableY = pGuild->fEmblem2Y;
			pPart->pMovableTexture->fMovableScaleX = pGuild->fEmblem2ScaleX;
			pPart->pMovableTexture->fMovableScaleY = pGuild->fEmblem2ScaleY;
		}

		if (pPart->pMovableTexture->fMovableValue < -100.0f) pPart->pMovableTexture->fMovableValue = -100.0f;
		else if (pPart->pMovableTexture->fMovableValue > 100.0f) pPart->pMovableTexture->fMovableValue = 100.0f;

		if (pPart->pMovableTexture->fMovableX < -100.0f) pPart->pMovableTexture->fMovableX = -100.0f;
		else if (pPart->pMovableTexture->fMovableX > 100.0f) pPart->pMovableTexture->fMovableX = 100.0f;

		if (pPart->pMovableTexture->fMovableY < -100.0f) pPart->pMovableTexture->fMovableY = -100.0f;
		else if (pPart->pMovableTexture->fMovableY > 100.0f) pPart->pMovableTexture->fMovableY = 100.0f;

		if (pPart->pMovableTexture->fMovableScaleX < 0.0f) pPart->pMovableTexture->fMovableScaleX = 0.0f;
		else if (pPart->pMovableTexture->fMovableScaleX > 100.0f) pPart->pMovableTexture->fMovableScaleX = 100.0f;

		if (pPart->pMovableTexture->fMovableScaleY < 0.0f) pPart->pMovableTexture->fMovableScaleY = 0.0f;
		else if (pPart->pMovableTexture->fMovableScaleY > 100.0f) pPart->pMovableTexture->fMovableScaleY = 100.0f;

		if (pPart->pMovableTexture->fMovableRotation < 0.0f) pPart->pMovableTexture->fMovableRotation = 0.0f;
		else if (pPart->pMovableTexture->fMovableRotation > 100.0f) pPart->pMovableTexture->fMovableRotation = 100.0f;
	}
}

static void costumeTailor_FixUpGuildEmblemPart(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, PCBoneDef *pBone, Guild *pGuild)
{
	if (pBone && pBone->bIsGuildEmblemBone)
	{
		if (pPart->eColorLink == kPCColorLink_All) pPart->eColorLink = kPCColorLink_MirrorGroup; //Emblem parts should never have color link all

		if (!costumeTailor_PartHasGuildEmblem(pPart, pGuild))
		{
			bool bClear = false;
			PCMaterialDef *pMaterial = GET_REF(pPart->hMatDef);
			PCTextureDef *pTexture = NULL;

			if (!costumeTailor_IsBoneRequired(pCostume, pBone, pSpecies))
			{
				if (pMaterial)
				{
					if (pMaterial->bRequiresPattern)
					{
						pTexture = GET_REF(pPart->hPatternTexture);
						if ((!pTexture) || (!pTexture->pcName) || !stricmp("None",pTexture->pcName)) bClear = true;
					}
					if (pMaterial->bRequiresDetail && !bClear)
					{
						pTexture = GET_REF(pPart->hDetailTexture);
						if ((!pTexture) || (!pTexture->pcName) || !stricmp("None",pTexture->pcName)) bClear = true;
					}
					if (pMaterial->bRequiresMovable && !bClear)
					{
						if (!pPart->pMovableTexture)
						{
							bClear = true;
						}
						else
						{
							pTexture = GET_REF(pPart->pMovableTexture->hMovableTexture);
							if ((!pTexture) || (!pTexture->pcName) || !stricmp("None",pTexture->pcName)) bClear = true;
						}
					}
				}
				else
				{
					bClear = true;
				}
			}

			if (bClear)
			{
				REMOVE_HANDLE(pPart->hGeoDef);
				REMOVE_HANDLE(pPart->hMatDef);
				REMOVE_HANDLE(pPart->hPatternTexture);
				REMOVE_HANDLE(pPart->hDetailTexture);
				REMOVE_HANDLE(pPart->hSpecularTexture);
				REMOVE_HANDLE(pPart->hDiffuseTexture);
				if (pPart->pMovableTexture) REMOVE_HANDLE(pPart->pMovableTexture->hMovableTexture);
			}
			else
			{
				if (pMaterial)
				{
					pTexture = GET_REF(pPart->hPatternTexture);
					if (pTexture && pTexture->pColorOptions)
					{
						if (pTexture->pColorOptions->bHasDefaultColor0) *((U32*)pPart->color0) = *((U32*)pTexture->pColorOptions->uDefaultColor0);
						if (pTexture->pColorOptions->bHasDefaultColor1) *((U32*)pPart->color1) = *((U32*)pTexture->pColorOptions->uDefaultColor1);
						if (pTexture->pColorOptions->bHasDefaultColor2) *((U32*)pPart->color2) = *((U32*)pTexture->pColorOptions->uDefaultColor2);
						if (pTexture->pColorOptions->bHasDefaultColor3) *((U32*)pPart->color3) = *((U32*)pTexture->pColorOptions->uDefaultColor3);
					}
					else
					{
						*((U32*)pPart->color0) = 0;
						*((U32*)pPart->color1) = 0;
						*((U32*)pPart->color2) = 0;
						*((U32*)pPart->color3) = 0;
					}
					if (pTexture && pTexture->pValueOptions)
					{
						if (!pPart->pTextureValues)
						{
							pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
						}
						if (pPart->pTextureValues)
						{
							pPart->pTextureValues->fPatternValue = pTexture->pValueOptions->fValueDefault;
						}
					}
					else if (pPart->pTextureValues)
					{
						pPart->pTextureValues->fPatternValue = 0.0f;
					}
					if (pTexture && pTexture->pMovableOptions)
					{
						if (!pPart->pMovableTexture)
						{
							pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
						}
						if (pPart->pMovableTexture)
						{
							pPart->pMovableTexture->fMovableRotation = pTexture->pMovableOptions->fMovableDefaultRotation;
							pPart->pMovableTexture->fMovableX = pTexture->pMovableOptions->fMovableDefaultX;
							pPart->pMovableTexture->fMovableY = pTexture->pMovableOptions->fMovableDefaultY;
							pPart->pMovableTexture->fMovableScaleX = pTexture->pMovableOptions->fMovableDefaultScaleX;
							pPart->pMovableTexture->fMovableScaleY = pTexture->pMovableOptions->fMovableDefaultScaleY;
						}
					}
					else if (pPart->pMovableTexture)
					{
						pPart->pMovableTexture->fMovableRotation = 0;
						pPart->pMovableTexture->fMovableX = 0;
						pPart->pMovableTexture->fMovableY = 0;
						pPart->pMovableTexture->fMovableScaleX = 1;
						pPart->pMovableTexture->fMovableScaleY = 1;
					}
					if (!pMaterial->bRequiresDetail)
					{
						REMOVE_HANDLE(pPart->hDetailTexture);
					}
					if (!pMaterial->bRequiresSpecular)
					{
						REMOVE_HANDLE(pPart->hSpecularTexture);
					}
					if (!pMaterial->bRequiresDiffuse)
					{
						REMOVE_HANDLE(pPart->hDiffuseTexture);
					}
					if (!pMaterial->bRequiresMovable)
					{
						if (pPart->pMovableTexture) REMOVE_HANDLE(pPart->pMovableTexture->hMovableTexture);
					}
				}
			}
		}
	}
}

void costumeTailor_PickValidPartValues(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, const PCSlotType *pSlotType, PlayerCostume **eaUnlockedCostumes, bool bSortDisplay, bool bUnlockAll, bool bFixColors, bool bInEditor, Guild *pGuild)
{
	NOCONST(PCPart) **eaChildParts = NULL;
	PCBoneDef *pBone;
	PCGeometryDef **eaGeos = NULL;
	PCCategory *pCategory = NULL;
	int i;
	bool bHasEmblem;

	PERFINFO_AUTO_START_FUNC();

	// Get category
	pBone = GET_REF(pPart->hBoneDef);
	if (pBone) {
		pCategory = costumeTailor_GetCategoryForRegion((PlayerCostume*)pCostume, GET_REF(pBone->hRegion));
	}

	bHasEmblem = costumeTailor_PartHasGuildEmblem(pPart, pGuild);

	// Validate geometry choice
	PERFINFO_AUTO_START("Fixup Geometry", 1);
	costumeTailor_GetValidGeos(pCostume, GET_REF(pCostume->hSkeleton), GET_REF(pPart->hBoneDef), pCategory, pSpecies, eaUnlockedCostumes, &eaGeos, false, false, bSortDisplay, bUnlockAll);
	if (bHasEmblem && eaSize(&eaGeos) > 1 && !stricmp(eaGeos[0]->pcName, "None")) eaRemove(&eaGeos, 0);
	costumeTailor_PickValidGeometry(pCostume, pPart, pSpecies, eaGeos, eaUnlockedCostumes, bUnlockAll);
	eaDestroy(&eaGeos);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Fixup Material", 1);
	costumeTailor_PickValidMaterialValues(pCostume, pPart, GET_REF(pPart->hGeoDef), pSpecies, eaUnlockedCostumes, bSortDisplay, bUnlockAll, true, bInEditor, pGuild);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Fixup Texture Values", 1);
	costumeTailor_PickValidSlideValues(pCostume, pPart, pSpecies, bInEditor, pGuild);
	PERFINFO_AUTO_STOP();

	if (bFixColors) {
		PERFINFO_AUTO_START("Fixup Colors", 1);
		costumeTailor_PickValidColors(pCostume, pPart, pSpecies, pSlotType, pGuild);
		PERFINFO_AUTO_STOP();
	}

	if (!bInEditor)
	{
		PERFINFO_AUTO_START("Fixup Guild Emblem", 1);
		costumeTailor_FixUpGuildEmblemPart(pCostume, pPart, pSpecies, pBone, pGuild);
		PERFINFO_AUTO_STOP();
	}

	// Validate cloth layer
	if (pPart->pClothLayer) {
		PCGeometryDef *pGeo = GET_REF(pPart->hGeoDef);
		if (pGeo && ((!pGeo->pClothData) || (!pGeo->pClothData->bHasClothBack) || (!pGeo->pClothData->bIsCloth)))
		{
			StructDestroyNoConstSafe(parse_PCPart,&pPart->pClothLayer);
		}
		else
		{
			PERFINFO_AUTO_START("Fixup Cloth Material", 1);
			costumeTailor_PickValidMaterialValues(pCostume, pPart->pClothLayer, pGeo, pSpecies, eaUnlockedCostumes, bSortDisplay, bUnlockAll, false, bInEditor, pGuild);
			PERFINFO_AUTO_STOP();

			PERFINFO_AUTO_START("Fixup Cloth Texture Values", 1);
			costumeTailor_PickValidSlideValues(pCostume, pPart->pClothLayer, pSpecies, bInEditor, pGuild);
			PERFINFO_AUTO_STOP();

			if (bFixColors) {
				PERFINFO_AUTO_START("Fixup Cloth Colors", 1);
				costumeTailor_PickValidColors(pCostume, pPart->pClothLayer, pSpecies, pSlotType, pGuild);
				PERFINFO_AUTO_STOP();
			}
		}
	}

	// Validate child geometry choice
	PERFINFO_AUTO_START("Fixup Child Bones", 1);
	if (pBone) {
		for(i=eaSize(&pBone->eaChildBones)-1; i>=0; --i) {
			NOCONST(PCPart) *pChildPart = costumeTailor_GetPartByBone(pCostume, GET_REF(pBone->eaChildBones[i]->hChildBone), NULL);
			bHasEmblem = costumeTailor_PartHasGuildEmblem(pChildPart, pGuild);
			PERFINFO_AUTO_START("Fixup Child Geo", 1);
			costumeTailor_GetValidChildGeos(pCostume, pCategory, GET_REF(pPart->hGeoDef), GET_REF(pBone->eaChildBones[i]->hChildBone), pSpecies, eaUnlockedCostumes, &eaGeos, bSortDisplay, bUnlockAll);
			if (bHasEmblem && eaSize(&eaGeos) > 1 && !stricmp(eaGeos[0]->pcName, "None")) eaRemove(&eaGeos, 0);
			costumeTailor_PickValidChildGeometry(pCostume, pPart, GET_REF(pBone->eaChildBones[i]->hChildBone), pSpecies, eaGeos, eaUnlockedCostumes, bUnlockAll);
			eaDestroy(&eaGeos);
			PERFINFO_AUTO_STOP();
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Fixup Child Parts", 1);
	costumeTailor_GetChildParts(pCostume, pPart, NULL, &eaChildParts);
	for(i=eaSize(&eaChildParts)-1; i>=0; --i) {
		NOCONST(PCPart) *pChildPart = eaChildParts[i];

		PERFINFO_AUTO_START("Fixup Material", 1);
		costumeTailor_PickValidMaterialValues(pCostume, pChildPart, GET_REF(pChildPart->hGeoDef), pSpecies, eaUnlockedCostumes, bSortDisplay, bUnlockAll, false, bInEditor, pGuild);
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("Fixup Texture Values", 1);
		costumeTailor_PickValidSlideValues(pCostume, pChildPart, pSpecies, bInEditor, pGuild);
		PERFINFO_AUTO_STOP();

		if (bFixColors) {
			PERFINFO_AUTO_START("Fixup Colors", 1);
			costumeTailor_PickValidColors(pCostume, pChildPart, pSpecies, pSlotType, pGuild);
			PERFINFO_AUTO_STOP();
		}

		if (!bInEditor)
		{
			PERFINFO_AUTO_START("Fixup Guild Emblem", 1);
			costumeTailor_FixUpGuildEmblemPart(pCostume, pChildPart, pSpecies, GET_REF(pChildPart->hBoneDef), pGuild);
			PERFINFO_AUTO_STOP();
		}

		if (pChildPart->pClothLayer) {
			PCGeometryDef *pGeo = GET_REF(pChildPart->hGeoDef);
			if (pGeo && ((!pGeo->pClothData) || (!pGeo->pClothData->bHasClothBack) || (!pGeo->pClothData->bIsCloth)))
			{
				StructDestroyNoConstSafe(parse_PCPart,&pChildPart->pClothLayer);
			}
			else
			{
				PERFINFO_AUTO_START("Fixup Cloth Material", 1);
				costumeTailor_PickValidMaterialValues(pCostume, pChildPart->pClothLayer, pGeo, pSpecies, eaUnlockedCostumes, bSortDisplay, bUnlockAll, false, bInEditor, pGuild);
				PERFINFO_AUTO_STOP();

				PERFINFO_AUTO_START("Fixup Cloth Texture Values", 1);
				costumeTailor_PickValidSlideValues(pCostume, pChildPart->pClothLayer, pSpecies, bInEditor, pGuild);
				PERFINFO_AUTO_STOP();

				if (bFixColors) {
					PERFINFO_AUTO_START("Fixup Cloth Colors", 1);
					costumeTailor_PickValidColors(pCostume, pChildPart->pClothLayer, pSpecies, pSlotType, pGuild);
					PERFINFO_AUTO_STOP();
				}
			}
		}
	}
	eaDestroy(&eaChildParts);
	PERFINFO_AUTO_STOP();

	// Remove bad artist data if costume is a player costume
	PERFINFO_AUTO_START("Fixup Artist Data", 1);
	if (IS_PLAYER_COSTUME(pCostume) && pPart->pArtistData && !bInEditor) {
		StructDestroyNoConst(parse_PCArtistPartData, pPart->pArtistData);
		pPart->pArtistData = NULL;
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP_FUNC();
}

void costumeTailor_PartFillClothLayer(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll)
{
	NOCONST(PCPart) *pNewPart;
	PCMaterialDef **eaMats = NULL;
	PCMaterialDef *pMatDef;

	if (pPart->pClothLayer) {
		return;
	}

	pNewPart = StructCloneNoConst(parse_PCPart, pPart);
	assert(pNewPart);
	if (pNewPart->pArtistData) {
		pNewPart->pArtistData->bNoShadow = false;
	}

	// Pick valid material if required
	if (GET_REF(pPart->hGeoDef)) {
		costumeTailor_GetValidMaterials(pPCCostume, GET_REF(pPart->hGeoDef), pSpecies, NULL, NULL, eaUnlockedCostumes, &eaMats, false, false, bUnlockAll);
		costumeTailor_PickValidMaterial(pPCCostume, pSpecies, pNewPart, eaUnlockedCostumes, eaMats, bUnlockAll, false);
		eaDestroy(&eaMats);
	}

	// Fill in the rest of the part as appropriate
	pMatDef = GET_REF(pNewPart->hMatDef);
	if (pMatDef) {
		costumeTailor_FillMatForPart(pNewPart, pPCCostume, pMatDef, pSpecies, eaUnlockedCostumes, false, false);
	}

	//These must be removed last because functions above rely on them
	REMOVE_HANDLE(pNewPart->hBoneDef);
	REMOVE_HANDLE(pNewPart->hGeoDef);

	pPart->pClothLayer = pNewPart;
}


void costumeTailor_FillMatForPart(NOCONST(PCPart) *pNewPart, NOCONST(PlayerCostume) *pPCCostume, PCMaterialDef *pMatDef, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, bool bSortDisplay, bool bUnlockAll)
{
	PCTextureDef **eaTexs = NULL;

	// Also pick valid textures if required
	costumeTailor_GetValidTextures(pPCCostume, GET_REF(pNewPart->hMatDef), pSpecies, NULL, NULL, GET_REF(pNewPart->hGeoDef), NULL, eaUnlockedCostumes, kPCTextureType_Pattern, &eaTexs, false, bSortDisplay, bUnlockAll);
	costumeTailor_PickValidTexture(pPCCostume, pNewPart, pSpecies, kPCTextureType_Pattern, eaTexs);
	eaDestroy(&eaTexs);
	if (!GET_REF(pNewPart->hPatternTexture)) {
		SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pNewPart->hPatternTexture);
	}
	costumeTailor_GetValidTextures(pPCCostume, GET_REF(pNewPart->hMatDef), pSpecies, NULL, NULL, GET_REF(pNewPart->hGeoDef), NULL, eaUnlockedCostumes, kPCTextureType_Detail, &eaTexs, false, bSortDisplay, bUnlockAll);
	costumeTailor_PickValidTexture(pPCCostume, pNewPart, pSpecies, kPCTextureType_Detail, eaTexs);
	eaDestroy(&eaTexs);
	if (!GET_REF(pNewPart->hDetailTexture)) {
		SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pNewPart->hDetailTexture);
	}
	costumeTailor_GetValidTextures(pPCCostume, GET_REF(pNewPart->hMatDef), pSpecies, NULL, NULL, GET_REF(pNewPart->hGeoDef), NULL, eaUnlockedCostumes, kPCTextureType_Specular, &eaTexs, false, bSortDisplay, bUnlockAll);
	costumeTailor_PickValidTexture(pPCCostume, pNewPart, pSpecies, kPCTextureType_Specular, eaTexs);
	eaDestroy(&eaTexs);
	if (!GET_REF(pNewPart->hSpecularTexture)) {
		SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pNewPart->hSpecularTexture);
	}
	costumeTailor_GetValidTextures(pPCCostume, GET_REF(pNewPart->hMatDef), pSpecies, NULL, NULL, GET_REF(pNewPart->hGeoDef), NULL, eaUnlockedCostumes, kPCTextureType_Diffuse, &eaTexs, false, bSortDisplay, bUnlockAll);
	costumeTailor_PickValidTexture(pPCCostume, pNewPart, pSpecies, kPCTextureType_Diffuse, eaTexs);
	eaDestroy(&eaTexs);
	if (!GET_REF(pNewPart->hDiffuseTexture)) {
		SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pNewPart->hDiffuseTexture);
	}
	costumeTailor_GetValidTextures(pPCCostume, GET_REF(pNewPart->hMatDef), pSpecies, NULL, NULL, GET_REF(pNewPart->hGeoDef), NULL, eaUnlockedCostumes, kPCTextureType_Movable, &eaTexs, false, bSortDisplay, bUnlockAll);
	costumeTailor_PickValidTexture(pPCCostume, pNewPart, pSpecies, kPCTextureType_Movable, eaTexs);
	eaDestroy(&eaTexs);
	if (!pNewPart->pMovableTexture || !GET_REF(pNewPart->pMovableTexture->hMovableTexture)) {
		if (!pNewPart->pMovableTexture) {
			pNewPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
		}
		SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pNewPart->pMovableTexture->hMovableTexture);
	}

}


AUTO_TRANS_HELPER;
void costumeTailor_SetDefaultSkinColor(ATH_ARG NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType)
{
	PCSkeletonDef *pSkel;
	UIColorSet *pColorSet;
	U8 skinPink[4] = { 212, 137, 114, 255 };
	U8 tempColor[4];

	pSkel = GET_REF(pPCCostume->hSkeleton);
	if (!pSkel) {
		return;
	}

	if (pSkel->defaultSkinColor[0] || pSkel->defaultSkinColor[1] || pSkel->defaultSkinColor[2] || pSkel->defaultSkinColor[3]) {
		VEC4_TO_COSTUME_COLOR(pSkel->defaultSkinColor, tempColor);
		
		pColorSet = costumeTailor_GetOverrideSkinColorSet(pSpecies, pSlotType);
		if(pColorSet)
		{
			if ((!eaSize(&pColorSet->eaColors)) || (costumeLoad_ColorInSet(tempColor, pColorSet)))
			{
				costumeTailor_SetSkinColor(pPCCostume, tempColor);
			}
			else
			{
				VEC4_TO_COSTUME_COLOR(pColorSet->eaColors[0]->color, tempColor);
				costumeTailor_SetSkinColor(pPCCostume, tempColor);
			}
		}
		else
		{
			costumeTailor_SetSkinColor(pPCCostume, tempColor);
		}
	} else {
		pColorSet = costumeTailor_GetOverrideSkinColorSet(pSpecies, pSlotType);
		if(!pColorSet)		
		{
			pColorSet = GET_REF(pSkel->hSkinColorSet);
		}
		if (pColorSet && eaSize(&pColorSet->eaColors)) {
			VEC4_TO_COSTUME_COLOR(pColorSet->eaColors[0]->color, tempColor);
			costumeTailor_SetSkinColor(pPCCostume, tempColor);
		} else {
			costumeTailor_SetSkinColor(pPCCostume, skinPink);
		}
	}
}

void costumeTailor_FillAllRegions(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const char **eaPowerFXBones, PCSlotType *pSlotType)
{
	PCRegion **eaRegions = NULL;
	PCCategory **eaCategories = NULL;
	PCCategory *pCategory = NULL;
	int i,j;

	// Fill region categories
	costumeTailor_GetValidRegions(pPCCostume, pSpecies, NULL, NULL, pSlotType, &eaRegions, CGVF_OMIT_EMPTY | CGVF_REQUIRE_POWERFX);
	for(i=eaSize(&eaRegions)-1; i>=0; --i) {
		pCategory = costumeTailor_GetCategoryForRegion((PlayerCostume*)pPCCostume, eaRegions[i]);
		if (!pCategory) {
			// Use auto-picker based on parts
			pCategory = costumeTailor_PickBestCategory((PlayerCostume*)pPCCostume, eaRegions[i], pSpecies, pSlotType, false);
		}
		if (!pCategory) {
			// Should only get here if no parts in this region right now
			costumeTailor_GetValidCategories(pPCCostume, eaRegions[i], pSpecies, NULL, NULL, pSlotType, &eaCategories, CGVF_OMIT_EMPTY | CGVF_REQUIRE_POWERFX);
			if (GET_REF(eaRegions[i]->hDefaultCategory)) {
				// Use default category if it's on the legal list
				for(j=eaSize(&eaCategories)-1; j>=0; --j) {
					if (eaCategories[j] == GET_REF(eaRegions[i]->hDefaultCategory)) {
						pCategory = eaCategories[j];
						break;
					}
				}
			} 
			if (!pCategory) {
				if (eaSize(&eaCategories) > 0) {
					// Use first valid non-empty category if available
					pCategory = eaCategories[0];
				} else {
					// Otherwise just use first category
					pCategory = GET_REF(eaRegions[i]->eaCategories[0]->hCategory);
				}
			}
			costumeTailor_SetRegionCategory(pPCCostume, eaRegions[i], pCategory);
			eaDestroy(&eaCategories);
		}
	}
	eaDestroy(&eaRegions);
}

// Fill a player costume with all legal bones
// Useful for tools
void costumeTailor_FillAllBones(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const char **eaPowerFXBones, PCSlotType *pSlotType, bool bApplyRequired, bool bAddArtistData, bool bSortDisplay)
{
	PCSkeletonDef *pSkel;
	PCBoneDef **eaBones = NULL;
	PCGeometryDef **eaGeos = NULL;
	PCGeometryDef **eaChildGeos = NULL;
	PCMaterialDef **eaMats = NULL;
	NOCONST(PCPart) *pNewPart;
	PCCategory *pCategory = NULL;
	int i,j;
	U8 white[4] = { 255, 255, 255, 255 };
	
	pSkel = GET_REF(pPCCostume->hSkeleton);
	
	if (!pSkel) {
		return;
	}

	costumeTailor_FillAllRegions(pPCCostume, pSpecies, eaPowerFXBones, pSlotType);
	
	// Fill the bones
	costumeTailor_GetValidBones(pPCCostume, GET_REF(pPCCostume->hSkeleton), NULL, NULL, pSpecies, NULL, eaPowerFXBones, &eaBones, CGVF_REQUIRE_POWERFX | (bSortDisplay ? CGVF_SORT_DISPLAY : 0));
	for(i=0; i<eaSize(&eaBones); ++i) {
		pNewPart = NULL;
		for(j=i; j<eaSize(&pPCCostume->eaParts); ++j) {
			if (GET_REF(pPCCostume->eaParts[j]->hBoneDef) == eaBones[i]) {
				if (j > i) {
					eaSwap(&pPCCostume->eaParts, i, j);
				}
				pNewPart = pPCCostume->eaParts[i];
				break;
			}
		}
		if (!pNewPart) {
			// Create the part
			pNewPart = StructCreateNoConst(parse_PCPart);
			SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, eaBones[i], pNewPart->hBoneDef);

			// Set color linking to "All" to copy from any other "All" part
			costumeTailor_SetPartColorLinking(pPCCostume, pNewPart, kPCColorLink_All, pSpecies, pSlotType);

			// If all black, then no other "All" part, so try copying from Mirror
			if (costumeTailor_PartHasNoColor(pNewPart)) {
				costumeTailor_SetPartColorLinking(pPCCostume, pNewPart, kPCColorLink_MirrorGroup, pSpecies, pSlotType);
			}

			// Add the part
			eaInsert(&pPCCostume->eaParts, pNewPart, i);

			// Give it a geometry if one is required
			if (bApplyRequired) {
				PCGeometryDef *pGeoDef;
				PCMaterialDef *pMatDef;

				pCategory = costumeTailor_GetCategoryForRegion((PlayerCostume*)pPCCostume, GET_REF(eaBones[i]->hRegion));
				if (pCategory) {
					costumeTailor_GetValidGeos(pPCCostume, GET_REF(pPCCostume->hSkeleton), eaBones[i], pCategory, pSpecies, NULL,
										&eaGeos, false, false, bSortDisplay, false);
					costumeTailor_PickValidGeometry(pPCCostume, pNewPart, pSpecies, eaGeos, NULL, false);
					eaDestroy(&eaGeos);
				}

				pGeoDef = GET_REF(pNewPart->hGeoDef);
				if (pGeoDef) {
					// Also pick valid material if required
					costumeTailor_SetPartMaterialLinking(pPCCostume, pNewPart, kPCColorLink_All, pSpecies, NULL, false);
					if (!GET_REF(pNewPart->hMatDef))
					{
						costumeTailor_SetPartMaterialLinking(pPCCostume, pNewPart, kPCColorLink_MirrorGroup, pSpecies, NULL, false);
						if (!GET_REF(pNewPart->hMatDef))
						{
							if (!pPCCostume->eDefaultMaterialLinkAll)
							{
								pNewPart->eMaterialLink = kPCColorLink_MirrorGroup;
							}
							else
							{
								pNewPart->eMaterialLink = kPCColorLink_All;
							}
							costumeTailor_GetValidMaterials(pPCCostume, GET_REF(pNewPart->hGeoDef), pSpecies, NULL, NULL, NULL, &eaMats, false, bSortDisplay, false);
							costumeTailor_PickValidMaterial(pPCCostume, pSpecies, pNewPart, NULL, eaMats, false, true);
							eaDestroy(&eaMats);
						}
					}
					else
					{
						if (!pPCCostume->eDefaultMaterialLinkAll)
						{
							pNewPart->eMaterialLink = kPCColorLink_MirrorGroup;
						}
						else
						{
							pNewPart->eMaterialLink = kPCColorLink_All;
						}
					}
				} else {
					SET_HANDLE_FROM_STRING(g_hCostumeGeometryDict, "None", pNewPart->hGeoDef);
				}

				// Apply skin color to part if appropriate
				pMatDef = GET_REF(pNewPart->hMatDef);
				if (pMatDef) {
					costumeTailor_FillMatForPart(pNewPart, pPCCostume, pMatDef, pSpecies, NULL, bSortDisplay, false);
				} else {
					SET_HANDLE_FROM_STRING(g_hCostumeMaterialDict, "None", pNewPart->hMatDef);
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pNewPart->hPatternTexture);
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pNewPart->hDetailTexture);
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pNewPart->hSpecularTexture);
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pNewPart->hDiffuseTexture);
				}

				// Apply child parts as required
				if (pGeoDef && pGeoDef->pOptions) {
					for(j=eaSize(&pGeoDef->pOptions->eaChildGeos)-1; j>=0; --j) {
						if (pGeoDef->pOptions && pGeoDef->pOptions->eaChildGeos[j]->bRequiresChild) {
							costumeTailor_GetValidChildGeos(pPCCostume, pCategory, pGeoDef, GET_REF(pGeoDef->pOptions->eaChildGeos[j]->hChildBone), pSpecies, NULL, &eaChildGeos, bSortDisplay, false);
							costumeTailor_PickValidChildGeometry(pPCCostume, pNewPart, GET_REF(pGeoDef->pOptions->eaChildGeos[j]->hChildBone), pSpecies, eaGeos, NULL, false);
							eaDestroy(&eaChildGeos);
						}
					}
				}
			}

			// If still all black, then set a color
			if (costumeTailor_PartHasNoColor(pNewPart)) {
				PCColorQuad *pQuad = NULL;
				PCColorQuadSet *pQuadSet;

				if (pPCCostume->eDefaultColorLinkAll)
				{
					pNewPart->eColorLink = kPCColorLink_All;
				}
				else
				{
					pNewPart->eColorLink = kPCColorLink_MirrorGroup;
				}

				pQuadSet = costumeTailor_GetColorQuadSetForPart(pPCCostume, pSpecies, pNewPart);
				if (pQuadSet && eaSize(&pQuadSet->eaColorQuads))
				{
					if (GET_REF(pSkel->hColorQuadSet) == pQuadSet)
					{
						pQuad = pSkel->pDefaultBodyColorQuad;
					}
					if (!pQuad)
					{
						pQuad = pQuadSet->eaColorQuads[0];
					}
				}
				if (!pQuad)
				{
					pQuad = &g_StaticQuad;
				}
				COPY_COSTUME_COLOR(pQuad->color0, pNewPart->color0);
				COPY_COSTUME_COLOR(pQuad->color1, pNewPart->color1);
				COPY_COSTUME_COLOR(pQuad->color2, pNewPart->color2);
				COPY_COSTUME_COLOR(pQuad->color3, pNewPart->color3);
			}
			else if (pNewPart->eColorLink != kPCColorLink_MirrorGroup)
			{
				if (pPCCostume->eDefaultColorLinkAll)
				{
					pNewPart->eColorLink = kPCColorLink_All;
				}
				else
				{
					pNewPart->eColorLink = kPCColorLink_MirrorGroup;
				}
			}
		} else {
			if (!GET_REF(pNewPart->hGeoDef)) {
				SET_HANDLE_FROM_STRING(g_hCostumeGeometryDict, "None", pNewPart->hGeoDef);
			}
			if (!GET_REF(pNewPart->hMatDef)) {
				SET_HANDLE_FROM_STRING(g_hCostumeMaterialDict, "None", pNewPart->hMatDef);
			}
			if (!GET_REF(pNewPart->hPatternTexture)) {
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pNewPart->hPatternTexture);
			}
			if (!GET_REF(pNewPart->hDetailTexture)) {
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pNewPart->hDetailTexture);
			}
			if (!GET_REF(pNewPart->hSpecularTexture)) {
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pNewPart->hSpecularTexture);
			}
			if (!GET_REF(pNewPart->hDiffuseTexture)) {
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pNewPart->hDiffuseTexture);
			}
		}

		// Fill in optional substructures so they exist for all parts
		if (!pNewPart->pCustomColors) {
			pNewPart->pCustomColors = StructCreateNoConst(parse_PCCustomColorInfo);
		}
		if (!pNewPart->pMovableTexture) {
			pNewPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
		}
		if (!GET_REF(pNewPart->pMovableTexture->hMovableTexture)) {
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pNewPart->pMovableTexture->hMovableTexture);
		}
		if (bAddArtistData && !pNewPart->pArtistData) {
			pNewPart->pArtistData = StructCreateNoConst(parse_PCArtistPartData);
		}
	}

	if (bAddArtistData) {
		// Fill in optional substructures on main costume
		if (!pPCCostume->pArtistData) {
			pPCCostume->pArtistData = StructCreateNoConst(parse_PCArtistCostumeData);
		}
		if (!pPCCostume->pArtistData->pDismountFX) {
			pPCCostume->pArtistData->pDismountFX = StructCreateNoConst(parse_PCFX);
		}
		if (!pPCCostume->pBodySockInfo) {
			pPCCostume->pBodySockInfo = StructCreateNoConst(parse_PCBodySockInfo);
		}
	}

	eaDestroy(&eaBones);
}


void costumeTailor_StripUnnecessaryFromPart(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, int i, bool bNullGeoAllowed)
{
	PCGeometryDef *pGeo;
	PCMaterialDef *pMat;
	PCTextureDef *pTex;
	int j;
	PCBoneDef *pBone;
	PCRegion *pRegion;
	SpeciesDef *pSpecies;
	
	if(!pPart)
	{
		return;
	}
	
	if(pPCCostume)
	{
		pBone = GET_REF(pPart->hBoneDef);
		pRegion = pBone ? GET_REF(pBone->hRegion) : NULL;
		pSpecies = GET_REF(pPCCostume->hSpecies);

		// some parts such as weapon with no cats need to be removed, but only if they do not use a required bone
		if(!costumeTailor_IsBoneRequired(pPCCostume, pBone, pSpecies) && (!pRegion || !costumeTailor_GetCategoryForRegion((PlayerCostume *)pPCCostume, pRegion) ) )
		{
			StructDestroyNoConst(parse_PCPart, pPart);
			eaRemove(&pPCCostume->eaParts, i);
			return;
		}
	}

	// Remove parts with no geometry defined
	pGeo = GET_REF(pPart->hGeoDef);
	if (pPCCostume && !bNullGeoAllowed && (!pGeo || (stricmp("None",pGeo->pcName) == 0))) {
		StructDestroyNoConst(parse_PCPart, pPart);
		eaRemove(&pPCCostume->eaParts, i);
		return;
	} else {
		pMat = GET_REF(pPart->hMatDef);
		if (pMat && (stricmp("None",pMat->pcName) == 0)) {
			REMOVE_HANDLE(pPart->hMatDef);
		}
		pTex = GET_REF(pPart->hPatternTexture);
		if (pTex && (stricmp("None",pTex->pcName) == 0)) {
			REMOVE_HANDLE(pPart->hPatternTexture);
		}
		pTex = GET_REF(pPart->hDetailTexture);
		if (pTex && (stricmp("None",pTex->pcName) == 0)) {
			REMOVE_HANDLE(pPart->hDetailTexture);
		}
		pTex = GET_REF(pPart->hSpecularTexture);
		if (pTex && (stricmp("None",pTex->pcName) == 0)) {
			REMOVE_HANDLE(pPart->hSpecularTexture);
		}
		pTex = GET_REF(pPart->hDiffuseTexture);
		if (pTex && (stricmp("None",pTex->pcName) == 0)) {
			REMOVE_HANDLE(pPart->hDiffuseTexture);
		}

		if (pPart->pMovableTexture) {
			pTex = GET_REF(pPart->pMovableTexture->hMovableTexture);
			if (pTex && (stricmp("None",pTex->pcName) == 0)) {
				REMOVE_HANDLE(pPart->pMovableTexture->hMovableTexture);
				pTex = NULL;
			}

			// Remove movable texture if no texture defined
			if (!pTex) {
				StructDestroyNoConstSafe(parse_PCMovableTextureInfo, &pPart->pMovableTexture);
			}
		}

		if (pPart->pCustomColors) {
			// Remove defaulted reflection and scalability
			if (!pPart->pCustomColors->bCustomReflection) {
				pPart->pCustomColors->reflection[0] = pPart->pCustomColors->reflection[1] = pPart->pCustomColors->reflection[2] = pPart->pCustomColors->reflection[3] = 0;
			}
			if (!pPart->pCustomColors->bCustomSpecularity) {
				pPart->pCustomColors->specularity[0] = pPart->pCustomColors->specularity[1] = pPart->pCustomColors->specularity[2] = pPart->pCustomColors->specularity[3] = 0;
			}

			// Remove structure entirely if not used
			if (!pPart->pCustomColors->bCustomReflection && 
				!pPart->pCustomColors->bCustomSpecularity && 
				(pPart->pCustomColors->glowScale[0] <= 1) && (pPart->pCustomColors->glowScale[1] <= 1) && (pPart->pCustomColors->glowScale[2] <= 1) && (pPart->pCustomColors->glowScale[3] <= 1)
				) {
				StructDestroyNoConstSafe(parse_PCCustomColorInfo, &pPart->pCustomColors);
			}
		}

		if (pPart->pTextureValues) {
			// Remove structure if not used
			if (!pPart->pTextureValues->fPatternValue &&
				!pPart->pTextureValues->fDetailValue &&
				!pPart->pTextureValues->fDiffuseValue &&
				!pPart->pTextureValues->fSpecularValue
				) {
				StructDestroyNoConstSafe(parse_PCTextureValueInfo, &pPart->pTextureValues);
			}
		}

		if (pPart->pArtistData) {
			// Remove extra textures that are not defined
			for(j=eaSize(&pPart->pArtistData->eaExtraTextures)-1; j>=0; --j) {
				pTex = GET_REF(pPart->pArtistData->eaExtraTextures[j]->hTexture);
				if (!pTex || (stricmp("None",pTex->pcName) == 0)) {
					StructDestroyNoConst(parse_PCTextureRef, pPart->pArtistData->eaExtraTextures[j]);
					eaRemove(&pPart->pArtistData->eaExtraTextures, j);
				}
			}

			// Remove structure if not used
			if (!eaSize(&pPart->pArtistData->eaExtraColors) &&
				!eaSize(&pPart->pArtistData->eaExtraConstants) &&
				!eaSize(&pPart->pArtistData->eaExtraTextures) &&
				!pPart->pArtistData->bNoShadow
				) {
				StructDestroyNoConstSafe(parse_PCArtistPartData, &pPart->pArtistData);
			}
		}
	}

	if (pPart->pClothLayer) {
		costumeTailor_StripUnnecessaryFromPart(NULL, pPart->pClothLayer, -1, true);
	}
}


// Strip out all unnecessary data from a costume
// Good to do this before saving
void costumeTailor_StripUnnecessary(NOCONST(PlayerCostume) *pPCCostume)
{
	int i;

	for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
		NOCONST(PCPart) *pPart = pPCCostume->eaParts[i];
		PCBoneDef *pBoneDef = GET_REF(pPart->hBoneDef);
		
		// Null ("None") geo is allowed on parts that are PowerFX bones with unlinked colors - keeping such
		//  parts around allows the Costume-based Power Art system to use default geometry with custom colors.
		int bNullGeoAllowed = (pBoneDef && pBoneDef->bPowerFX && pPart->eColorLink==kPCColorLink_None);
		
		costumeTailor_StripUnnecessaryFromPart(pPCCostume, pPart, i, bNullGeoAllowed);
	}

	if (pPCCostume->pArtistData) {
		// Remove FX that are not defined
		for(i=eaSize(&pPCCostume->pArtistData->eaFX)-1; i>=0; --i) {
			if (!pPCCostume->pArtistData->eaFX[i]->pcName || !strlen(pPCCostume->pArtistData->eaFX[i]->pcName)) {
				StructDestroyNoConst(parse_PCFX, pPCCostume->pArtistData->eaFX[i]);
				eaRemove(&pPCCostume->pArtistData->eaFX, i);
			}
		}

		// Remove FX Swaps that are not defined
		for(i=eaSize(&pPCCostume->pArtistData->eaFXSwap)-1; i>=0; --i) {
			if (!pPCCostume->pArtistData->eaFXSwap[i]->pcOldName || !strlen(pPCCostume->pArtistData->eaFXSwap[i]->pcOldName) ||
				!pPCCostume->pArtistData->eaFXSwap[i]->pcNewName || !strlen(pPCCostume->pArtistData->eaFXSwap[i]->pcNewName)) {
				StructDestroyNoConst(parse_PCFXSwap, pPCCostume->pArtistData->eaFXSwap[i]);
				eaRemove(&pPCCostume->pArtistData->eaFXSwap, i);
			}
		}

		// Remove constant bits that are not defined
		for(i=eaSize(&pPCCostume->pArtistData->eaConstantBits)-1; i>=0; --i) {
			if (!pPCCostume->pArtistData->eaConstantBits[i]->pcName || !strlen(pPCCostume->pArtistData->eaConstantBits[i]->pcName)) {
				StructDestroyNoConst(parse_PCBitName, pPCCostume->pArtistData->eaConstantBits[i]);
				eaRemove(&pPCCostume->pArtistData->eaConstantBits, i);
			}
		}
		
		//Remove undefined DismountFX
		if (pPCCostume->pArtistData->pDismountFX && (!pPCCostume->pArtistData->pDismountFX->pcName || !strlen(pPCCostume->pArtistData->pDismountFX->pcName))) {
			StructDestroyNoConst(parse_PCFX, pPCCostume->pArtistData->pDismountFX);
			pPCCostume->pArtistData->pDismountFX = NULL;
		}

		// Destroy the artist data if not used on this costume
		if (!pPCCostume->pArtistData->pcCollisionGeo &&
			!eaSize(&pPCCostume->pArtistData->ppCollCapsules) &&
			!pPCCostume->pArtistData->fCollRadius &&
			!eaSize(&pPCCostume->pArtistData->eaFX) &&
			!eaSize(&pPCCostume->pArtistData->eaFXSwap) &&
			!eaSize(&pPCCostume->pArtistData->eaConstantBits) &&
			!pPCCostume->pArtistData->fTransparency &&
			!pPCCostume->pArtistData->bNoCollision &&
			!pPCCostume->pArtistData->bNoBodySock &&
			!pPCCostume->pArtistData->bNoRagdoll &&
			!pPCCostume->pArtistData->bShellColl &&
			!pPCCostume->pArtistData->pDismountFX
			) {
			StructDestroyNoConstSafe(parse_PCArtistCostumeData, &pPCCostume->pArtistData);
		}
	}

	if (pPCCostume->pBodySockInfo) {
		if (((!pPCCostume->pBodySockInfo->pcBodySockGeo) || !*pPCCostume->pBodySockInfo->pcBodySockGeo) &&
			((!pPCCostume->pBodySockInfo->pcBodySockPose) || !*pPCCostume->pBodySockInfo->pcBodySockPose) &&
			pPCCostume->pBodySockInfo->vBodySockMin[0] == 0 && pPCCostume->pBodySockInfo->vBodySockMin[1] == 0 && pPCCostume->pBodySockInfo->vBodySockMin[2] == 0 &&
			pPCCostume->pBodySockInfo->vBodySockMax[0] == 0 && pPCCostume->pBodySockInfo->vBodySockMax[1] == 0 && pPCCostume->pBodySockInfo->vBodySockMax[2] == 0)
		{
			StructDestroyNoConstSafe(parse_PCBodySockInfo, &pPCCostume->pBodySockInfo);
		}
	}

	// Remove scale values that are zero
	for(i=eaSize(&pPCCostume->eaScaleValues)-1; i>=0; --i) {
		if (pPCCostume->eaScaleValues[i]->fValue == 0.0) {
			StructDestroyNoConst(parse_PCScaleValue, pPCCostume->eaScaleValues[i]);
			eaRemove(&pPCCostume->eaScaleValues, i);
		}
	}

	// Remove regions with no category
	for(i=eaSize(&pPCCostume->eaRegionCategories)-1; i>=0; --i) {
		if(!GET_REF(pPCCostume->eaRegionCategories[i]->hCategory)) {
			StructDestroyNoConst(parse_PCRegionCategory, pPCCostume->eaRegionCategories[i]);
			eaRemove(&pPCCostume->eaRegionCategories, i);
		}
	}

}


int costumeTailor_GetMatchingCategoryIndex(PCCategory *pCategory, PCCategory **eaCategory)
{	
	int i;
	const char *pcName1;

	if (!pCategory) return -1;

	// Look for exact match
	for(i=eaSize(&eaCategory)-1; i>=0; --i) {
		if (eaCategory[i] == pCategory) {
			return i;
		}
	}

	pcName1 = TranslateDisplayMessage(pCategory->displayNameMsg);

	// Look for display name match
	for(i=eaSize(&eaCategory)-1; i>=0; --i) {
		const char *pcName2 = TranslateDisplayMessage(eaCategory[i]->displayNameMsg);
		if (pcName1 && pcName2 && (stricmp(pcName1, pcName2) == 0)) {
			return i;
		}
	}

	return -1;
}

int costumeTailor_GetMatchingBoneIndex(PCBoneDef *pBone, PCBoneDef **eaBones)
{	
	int i;
	const char *pcName1;

	if (!pBone) return -1;

	// Look for exact match
	for(i=eaSize(&eaBones)-1; i>=0; --i) {
		if (eaBones[i] == pBone) {
			return i;
		}
	}

	pcName1 = TranslateDisplayMessage(pBone->displayNameMsg);

	// Look for display name match
	for(i=eaSize(&eaBones)-1; i>=0; --i) {
		const char *pcName2 = TranslateDisplayMessage(eaBones[i]->displayNameMsg);
		if (pcName1 && pcName2 && (stricmp(pcName1, pcName2) == 0)) {
			return i;
		}
	}

	return -1;
}

int costumeTailor_GetMatchingGeoIndex(PCGeometryDef *pGeo, PCGeometryDef **eaGeos)
{	
	int i;
	const char *pcName1;
	
	// Look for exact match
	for(i=eaSize(&eaGeos)-1; i>=0; --i) {
		if (eaGeos[i] == pGeo) {
			return i;
		}
	}

	// Look for assigned match
	if (pGeo->pcMirrorGeometry) {
		for(i=eaSize(&eaGeos)-1; i>=0; --i) {
			if (eaGeos[i]->pcName && (stricmp(pGeo->pcMirrorGeometry, eaGeos[i]->pcName) == 0)) {
				return i;
			}
		}
	}

	pcName1 = TranslateDisplayMessage(pGeo->displayNameMsg);

	// Look for display name match
	for(i=eaSize(&eaGeos)-1; i>=0; --i) {
		const char *pcName2 = TranslateDisplayMessage(eaGeos[i]->displayNameMsg);
		if (pcName1 && pcName2 && (stricmp(pcName1, pcName2) == 0)) {
			return i;
		}
	}

	return -1;
}

int costumeTailor_GetMatchingMatIndex(PCMaterialDef *pMat, PCMaterialDef **eaMats)
{	
	int i;
	const char *pcName1;
	
	// Look for exact match
	for(i=eaSize(&eaMats)-1; i>=0; --i) {
		if (eaMats[i] == pMat) {
			return i;
		}
	}

	pcName1 = TranslateDisplayMessage(pMat->displayNameMsg);

	// Look for display name match
	for(i=eaSize(&eaMats)-1; i>=0; --i) {
		const char *pcName2 = TranslateDisplayMessage(eaMats[i]->displayNameMsg);
		if (pcName1 && pcName2 && (stricmp(pcName1, pcName2) == 0)) {
			return i;
		}
	}

	return -1;
}

int costumeTailor_GetMatchingTexIndex(PCTextureDef *pTex, PCTextureDef **eaTexs)
{	
	int i;
	const char *pcName1;
	
	// Look for exact match
	for(i=eaSize(&eaTexs)-1; i>=0; --i) {
		if (eaTexs[i] == pTex) {
			return i;
		}
	}

	pcName1 = TranslateDisplayMessage(pTex->displayNameMsg);

	// Look for display name match
	for(i=eaSize(&eaTexs)-1; i>=0; --i) {
		const char *pcName2 = TranslateDisplayMessage(eaTexs[i]->displayNameMsg);
		if (pcName1 && pcName2 && (stricmp(pcName1, pcName2) == 0)) {
			return i;
		}
	}

	return -1;
}

bool costumeTailor_DoesCostumeHaveUnlockables(PlayerCostume *pBaseCostume)
{
	int i;

	if (!pBaseCostume) return false;

	for (i = eaSize(&pBaseCostume->eaParts)-1; i >= 0; --i)
	{
		PCPart *p = pBaseCostume->eaParts[i];
		if (GET_REF(p->hBoneDef) && !(GET_REF(p->hBoneDef)->eRestriction & kPCRestriction_Player_Initial)) return true;
		if (GET_REF(p->hGeoDef) && !(GET_REF(p->hGeoDef)->eRestriction & kPCRestriction_Player_Initial)) return true;
		if (GET_REF(p->hMatDef) && !(GET_REF(p->hMatDef)->eRestriction & kPCRestriction_Player_Initial)) return true;
		if (GET_REF(p->hPatternTexture) && !(GET_REF(p->hPatternTexture)->eRestriction & kPCRestriction_Player_Initial)) return true;
		if (GET_REF(p->hDiffuseTexture) && !(GET_REF(p->hDiffuseTexture)->eRestriction & kPCRestriction_Player_Initial)) return true;
		if (GET_REF(p->hSpecularTexture) && !(GET_REF(p->hSpecularTexture)->eRestriction & kPCRestriction_Player_Initial)) return true;
		if (GET_REF(p->hDetailTexture) && !(GET_REF(p->hDetailTexture)->eRestriction & kPCRestriction_Player_Initial)) return true;
		if (p->pMovableTexture && GET_REF(p->pMovableTexture->hMovableTexture) && !(GET_REF(p->pMovableTexture->hMovableTexture)->eRestriction & kPCRestriction_Player_Initial)) return true;
	}

	return false;
}

PlayerCostume *costumeTailor_MakeCostumeOverlayBG(PlayerCostume *pBaseCostume, PCSkeletonDef *pSkel, PCBoneGroup *bg, bool bIncludeChildBones, bool bCloneCostume)
{
	NOCONST(PlayerCostume) *pOverlay = NULL;
	int i, j;

	if (!pBaseCostume || !bg) return NULL;

	//Get Skeleton
	if (!pSkel) {
		pSkel = GET_REF(pBaseCostume->hSkeleton);
	}
	if (!pSkel) {
		return NULL;
	}

	if (bCloneCostume)
	{
		pOverlay = StructCloneDeConst(parse_PlayerCostume, pBaseCostume);
		if (!pOverlay) return NULL;
		eaDestroyStructNoConst(&pOverlay->eaParts, parse_PCPart);
		SET_HANDLE_FROM_REFERENT(g_hCostumeSkeletonDict, pSkel, pOverlay->hSkeleton);
	}
	else
	{
		pOverlay = StructCreateNoConst(parse_PlayerCostume);
		if (!pOverlay) return NULL;
		SET_HANDLE_FROM_REFERENT(g_hCostumeSkeletonDict, pSkel, pOverlay->hSkeleton);
		COPY_HANDLE(pOverlay->hSpecies,pBaseCostume->hSpecies);

		for(i=eaSize(&pSkel->eaRegions)-1; i>=0; --i)
		{
			PCCategory *pCat = costumeTailor_GetCategoryForRegion((PlayerCostume*)pBaseCostume,GET_REF(pSkel->eaRegions[i]->hRegion));
			costumeTailor_SetRegionCategory(pOverlay, GET_REF(pSkel->eaRegions[i]->hRegion), pCat);
		}
	}

	// Apply costume parts
	for(i=eaSize(&bg->eaBoneInGroup)-1; i>=0; --i) {
		PCBoneDef *pBone;
		PCPart *pPart;
		NOCONST(PCPart) *pNewPart;

		// Collect Info
		pBone = GET_REF(bg->eaBoneInGroup[i]->hBone);
		if (!pBone) {
			continue;
		}
		pPart = (PCPart *)costumeTailor_GetPartByBone(CONTAINER_NOCONST(PlayerCostume, pBaseCostume), pBone, NULL);

		// Apply part to the costume
		if (pPart)
		{
			pNewPart = StructCloneDeConst(parse_PCPart, pPart);
			eaPush(&pOverlay->eaParts, pNewPart);

			if (bIncludeChildBones)
			{
				for (j = eaSize(&pBone->eaChildBones)-1; j >= 0; --j)
				{
					PCBoneDef *b = GET_REF(pBone->eaChildBones[j]->hChildBone);
					if (!b) continue;
					pPart = (PCPart *)costumeTailor_GetPartByBone(CONTAINER_NOCONST(PlayerCostume, pBaseCostume), b, NULL);
					if (pPart)
					{
						pNewPart = StructCloneDeConst(parse_PCPart, pPart);
						eaPush(&pOverlay->eaParts, pNewPart);
					}
				}
			}
		}
	}

	return (PlayerCostume *)pOverlay;
}

PlayerCostume *costumeTailor_MakeCostumeOverlayEx(PlayerCostume *pBaseCostume, PCSkeletonDef *pSkel, const char *pchBoneGroup, bool bIncludeChildBones, bool bCloneCostume)
{
	int i;
	PCBoneGroup *bg = NULL;

	if (!pBaseCostume || !pchBoneGroup || !*pchBoneGroup) return NULL;

	//Get Skeleton
	if (!pSkel) {
		pSkel = GET_REF(pBaseCostume->hSkeleton);
	}
	if (!pSkel) {
		return NULL;
	}

	for (i = eaSize(&pSkel->eaBoneGroups)-1; i >= 0; --i)
	{
		if (!stricmp(pchBoneGroup, pSkel->eaBoneGroups[i]->pcName))
		{
			bg = pSkel->eaBoneGroups[i];
			break;
		}
	}
	if (!bg) return NULL;

	return costumeTailor_MakeCostumeOverlayBG(pBaseCostume, pSkel, bg, bIncludeChildBones, bCloneCostume);
}

// Simple check to see if there is a Geo that has the same name, appended with _cr_categoryname.
//  Such a Geo is presumed to be legal in the category (no validation), and to be a reasonable
//  straight replacement of the existing Geo (without further checks to texture, material and so on)
// TODO(JW): May need to support stripping down to the base name if the geo name already has _cr_ in it.
// TODO(JW): May need to support selected the base geo if the geo name already has _cr_ in it, which would
//  require validating that the geo to be returned is valid in the category.
SA_RET_OP_VALID static PCGeometryDef *costumeTailor_FindCategoryReplacementGeo(SA_PARAM_NN_VALID PCGeometryDef *pGeo, SA_PARAM_OP_VALID PCCategory *pCat)
{
	PCGeometryDef *pGeoReplace = NULL;
	char *pchGeoReplace = NULL;
	estrStackCreate(&pchGeoReplace);
	estrCopy2(&pchGeoReplace,pGeo->pcName);
	estrAppend2(&pchGeoReplace,"_cr_");
	estrAppend2(&pchGeoReplace,pCat ? pCat->pcName : "None");
	pGeoReplace = RefSystem_ReferentFromString(g_hCostumeGeometryDict,pchGeoReplace);
	estrDestroy(&pchGeoReplace);
	return pGeoReplace;
}


// Overlays the second costume onto the first to the extent that is legal
// Returns true if the base costume is modified in any way
bool costumeTailor_ApplyCostumeOverlayBG(PlayerCostume *pBaseCostume, CostumeDisplayData* pData, PlayerCostume *pCostumeToOverlay, PlayerCostume **eaUnlockedCostumes, PCBoneGroup *bg, PCSlotType *pSlotType, bool bForceCategory, bool bIgnoreSpeciesMatching, bool bIgnoreSkelMatching, bool bUseClosestColors, bool bOverlayCategoryReplace)
{
	PlayerCostume *pCostumeToOverlayReplace = NULL;
	NOCONST(PlayerCostume) *pModifiedCostume = CONTAINER_NOCONST(PlayerCostume, pBaseCostume);
	NOCONST(PlayerCostume) *pSavedCostume;
	PCSkeletonDef *pSkel, *pOverlaySkel;
	bool bModified = false;
	int i,j,k,l,m;
	SpeciesDef *pSpecies = NULL;
	PCBoneDef **eaBones = NULL, **eaBaseBones = NULL;
	NOCONST(PCPart) **eaBaseParts = NULL, **eaOverlayParts = NULL;
	PCGeometryDef *pGeo;
	PCRegion **eaAllowedRegions = NULL;
	PCCategory **eaNewCategories = NULL;
	PCRegion **eaModifiedRegions = NULL;

	if (!pCostumeToOverlay) return false;

	// Can't apply if skeletons don't match
	if ((!bIgnoreSkelMatching) && (GET_REF(pModifiedCostume->hSkeleton) != GET_REF(pCostumeToOverlay->hSkeleton))) {
		return false;
	}

	if (pCostumeToOverlay->pArtistData) {
		// Add all bits from the overlay
		for(i=eaSize(&pCostumeToOverlay->pArtistData->eaConstantBits)-1; i>=0; --i) {
			NOCONST(PCBitName) *pName = StructCloneDeConst(parse_PCBitName, pCostumeToOverlay->pArtistData->eaConstantBits[i]);
			if (!pModifiedCostume->pArtistData) {
				pModifiedCostume->pArtistData = StructCreateNoConst(parse_PCArtistCostumeData);
			}
			if (pModifiedCostume->pArtistData) {
				eaPush(&pModifiedCostume->pArtistData->eaConstantBits, pName);
				bModified = true;
			}
		}

		// Add all FX from the overlay
		for(i=eaSize(&pCostumeToOverlay->pArtistData->eaFX)-1; i>=0; --i) {
			NOCONST(PCFX) *pFX = StructCloneDeConst(parse_PCFX, pCostumeToOverlay->pArtistData->eaFX[i]);
			if (!pModifiedCostume->pArtistData) {
				pModifiedCostume->pArtistData = StructCreateNoConst(parse_PCArtistCostumeData);
			}
			if (pModifiedCostume->pArtistData) {
				eaPush(&pModifiedCostume->pArtistData->eaFX, pFX);
				bModified = true;
			}
		}

		// Add all FX Swaps from the overlay
		for(i=eaSize(&pCostumeToOverlay->pArtistData->eaFXSwap)-1; i>=0; --i) {
			NOCONST(PCFXSwap) *pFXSwap = StructCloneDeConst(parse_PCFXSwap, pCostumeToOverlay->pArtistData->eaFXSwap[i]);
			if (!pModifiedCostume->pArtistData) {
				pModifiedCostume->pArtistData = StructCreateNoConst(parse_PCArtistCostumeData);
			}
			if (pModifiedCostume->pArtistData) {
				eaPush(&pModifiedCostume->pArtistData->eaFXSwap, pFXSwap);
				bModified = true;
			}
		}
	}

	//Get Skeleton
	pSkel = GET_REF(pModifiedCostume->hSkeleton);
	if (!pSkel) {
		return bModified;
	}
	pOverlaySkel = GET_REF(pCostumeToOverlay->hSkeleton);
	if (!pOverlaySkel) {
		return bModified;
	}

	pSpecies = GET_REF(pModifiedCostume->hSpecies);

	if(bOverlayCategoryReplace)
	{
		S32 bReplaced = false;

		// Copy the overlay costume, but throw away the specified region categories
		pCostumeToOverlayReplace = StructClone(parse_PlayerCostume,pCostumeToOverlay);
		eaDestroyStructNoConst(&CONTAINER_NOCONST(PlayerCostume, pCostumeToOverlayReplace)->eaRegionCategories,parse_PCRegionCategory);

		// Go through each part and try to match with the base costume category, attempt to category replace
		//  if they don't match
		for(i=eaSize(&pCostumeToOverlayReplace->eaParts)-1; i>=0; i--)
		{
			PCPart *pOverlayPart = pCostumeToOverlayReplace->eaParts[i];
			PCGeometryDef *pOverlayGeo = pOverlayPart ? GET_REF(pOverlayPart->hGeoDef) : NULL;
			PCBoneDef *pOverlayBoneDef = pOverlayPart ? GET_REF(pOverlayPart->hBoneDef) : NULL;
			PCRegion *pOverlayRegion = pOverlayBoneDef ? GET_REF(pOverlayBoneDef->hRegion) : NULL;
			PCCategory *pModCategory = costumeTailor_GetCategoryForRegion((PlayerCostume*)pModifiedCostume, pOverlayRegion);

			if(pModCategory && pOverlayGeo)
			{
				for(j=eaSize(&pOverlayGeo->eaCategories)-1; j>=0; j--)
				{
					if(GET_REF(pOverlayGeo->eaCategories[j]->hCategory) == pModCategory)
						break;
				}
				if(j < 0)
				{
					// This part doesn't match the existing category in the region it is in, check for
					//  a category replace
					PCGeometryDef *pOverlayGeoReplace = costumeTailor_FindCategoryReplacementGeo(pOverlayGeo,pModCategory);
					if(pOverlayGeoReplace)
					{
						// TODO(JW): Hack: This doesn't do any validation that the replacement geo
						//  allows the pre-existing material and texture.
						SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict,pOverlayGeoReplace,pOverlayPart->hGeoDef);
						bReplaced = true;
					}
				}
			}
		}

		if(!bReplaced)
		{
			StructDestroy(parse_PlayerCostume,pCostumeToOverlayReplace);
			return bModified;
		}

		// Change the costume we're trying to overlay to the one with with some category
		//  replacement changes in it and see if that works
		pCostumeToOverlay = pCostumeToOverlayReplace;
	}



	//Make list of bones to overlay
	for(i = eaSize(&pCostumeToOverlay->eaParts)-1; i>=0; --i)
	{
		eaPush(&eaBones, GET_REF(pCostumeToOverlay->eaParts[i]->hBoneDef));
	}
	for(i = ( bg ? eaSize(&bg->eaBoneInGroup)-1 : eaSize(&pSkel->eaRequiredBoneDefs)-1 ); i>=0; --i)
	{
		PCBoneDef *b = bg ? GET_REF(bg->eaBoneInGroup[i]->hBone) : GET_REF(pSkel->eaRequiredBoneDefs[i]->hBone);
		int iBone = costumeTailor_GetMatchingBoneIndex(b, eaBones);
		if (eaBones && iBone >= 0)
		{
			eaPush(&eaOverlayParts, costumeTailor_GetPartByBone(CONTAINER_NOCONST(PlayerCostume, pCostumeToOverlay), eaBones[iBone], NULL));
			eaPush(&eaBaseBones, b);
			eaPush(&eaBaseParts, costumeTailor_GetPartByBone(CONTAINER_NOCONST(PlayerCostume, pBaseCostume), b, NULL));
		}
		else if (bg && b)
		{
			eaPush(&eaOverlayParts, NULL);
			eaPush(&eaBaseBones, b);
			eaPush(&eaBaseParts, costumeTailor_GetPartByBone(CONTAINER_NOCONST(PlayerCostume, pBaseCostume), b, NULL));
		}
	}
	if (!bg)
	{
		for(i = eaSize(&pSkel->eaOptionalBoneDefs)-1; i>=0; --i)
		{
			PCBoneDef *b = GET_REF(pSkel->eaOptionalBoneDefs[i]->hBone);
			int iBone = costumeTailor_GetMatchingBoneIndex(b, eaBones);
			if (eaBones && iBone >= 0)
			{
				eaPush(&eaOverlayParts, costumeTailor_GetPartByBone(CONTAINER_NOCONST(PlayerCostume, pCostumeToOverlay), eaBones[iBone], NULL));
				eaPush(&eaBaseBones, b);
				eaPush(&eaBaseParts, costumeTailor_GetPartByBone(CONTAINER_NOCONST(PlayerCostume, pBaseCostume), b, NULL));
			}
		}
	}
	if (eaBones) eaDestroy(&eaBones);

	// Check region compatibility
	for(i=eaSize(&pOverlaySkel->eaRegions)-1; i>=0; --i) {
		PCCategory *pModifiedCat = NULL;
		PCCategory *pOverlayCat = NULL;
		PCCategory *pOverlayMatchCat = NULL;
		PCRegion *pModRegion = NULL;
		PCRegion *pOverRegion = GET_REF(pOverlaySkel->eaRegions[i]->hRegion);

		//Find matching region
		if (!pOverRegion) continue;
		for (j = eaSize(&pSkel->eaRegions)-1; j >= 0; --j)
		{
			if (GET_REF(pSkel->eaRegions[j]->hRegion) == pOverRegion)
			{
				pModRegion = GET_REF(pSkel->eaRegions[j]->hRegion);
				break;
			}
		}
		if (!pModRegion)
		{
			const char *pcName1, *pcName2;
			pcName1 = TranslateDisplayMessage(pOverRegion->displayNameMsg);
			for (j = eaSize(&pSkel->eaRegions)-1; j >= 0; --j)
			{
				PCRegion *pTemp = GET_REF(pSkel->eaRegions[j]->hRegion);
				if (!pTemp) continue;
				pcName2 = TranslateDisplayMessage(pTemp->displayNameMsg);
				if (pcName1 && pcName2 && (stricmp(pcName1, pcName2) == 0))
				{
					pModRegion = GET_REF(pSkel->eaRegions[j]->hRegion);
					break;
				}
			}
		}
		if (!pModRegion) continue;

		//Get categories
		pModifiedCat = costumeTailor_GetCategoryForRegion((PlayerCostume*)pModifiedCostume, pModRegion);
		pOverlayCat = costumeTailor_GetCategoryForRegion(pCostumeToOverlay, pOverRegion);

		if (!pModifiedCat) {
			pModifiedCat = costumeTailor_PickBestCategory((PlayerCostume*)pModifiedCostume, pModRegion, pSpecies, pSlotType, false);
		}
		if (!pOverlayCat) {
			pOverlayCat = costumeTailor_PickBestCategory(pCostumeToOverlay, pOverRegion, pSpecies, pSlotType, false);
		}

		if (pSkel != pOverlaySkel && pOverlayCat)
		{
			int iCat;
			PCCategory **eaCat = NULL;
			costumeTailor_GetValidCategories(pModifiedCostume, pModRegion, pSpecies, eaUnlockedCostumes, NULL, pSlotType, &eaCat, 0);
			iCat = costumeTailor_GetMatchingCategoryIndex(pOverlayCat, eaCat);
			if (iCat >= 0)
			{
				pOverlayMatchCat = eaCat[iCat];
			}
			eaDestroy(&eaCat);
		}

		if (pOverlayMatchCat && pOverlayMatchCat == pModifiedCat)
		{
			eaPush(&eaAllowedRegions, pModRegion);
			eaPush(&eaNewCategories, pOverlayMatchCat);
		}
		else if (pModifiedCat && (pModifiedCat != pOverlayCat))
		{
			if (bForceCategory)
			{
				bool bOverwriteCategory = true;

				// Incompatible category
				// Try to see if all parts in region are valid in new category
				for(j=eaSize(&eaBaseParts)-1; j>=0; --j) {
					PCBoneDef *pBaseBone, *pOverlayBone;
					NOCONST(PCPart) *pBasePart, *pOverlayPart;

					// Collect Info
					pBasePart = eaBaseParts[j];
					pBaseBone = eaBaseBones[j];
					if (!pBaseBone) {
						continue;
					}
					pOverlayPart = eaOverlayParts[j];
					if (!pOverlayPart) {
						continue;
					}
					pOverlayBone = GET_REF(pOverlayPart->hBoneDef);
					if (!pOverlayBone) {
						continue;
					}

					if (GET_REF(pBaseBone->hRegion) != pModRegion) {
						continue; // Not in region
					}

					pGeo = GET_REF(pOverlayPart->hGeoDef);
					if (pSkel != pOverlaySkel && pGeo)
					{
						int iGeo;
						PCGeometryDef **eaGeos = NULL;
						costumeTailor_GetValidGeos(pModifiedCostume, pSkel, pBaseBone, pModifiedCat, pSpecies, eaUnlockedCostumes, &eaGeos, false, false, false, false);
						iGeo = costumeTailor_GetMatchingGeoIndex(pGeo, eaGeos);
						if (iGeo >= 0)
						{
							pGeo = eaGeos[iGeo];
						}
						eaDestroy(&eaGeos);
					}
					if (pGeo) {
						for(k=eaSize(&pGeo->eaCategories)-1; k>=0; --k) {
							if (GET_REF(pGeo->eaCategories[k]->hCategory) == pModifiedCat) {
								break;
							}
						}
						if (k < 0) {
							// Part is not legal in the category
							break;
						}
					}
				}

				if (j >= 0)
				{
					// A part is not legal in the category
					for (j = eaSize(&pModRegion->eaCategories)-1; j >= 0; --j)
					{
						if (GET_REF(pModRegion->eaCategories[j]->hCategory) == pOverlayCat)
						{
							if (costumeTailor_CategoryIncludedInSlotType(pOverlayCat, pSlotType))
							{
								break;
							}
						}
					}
					if (j >= 0)
					{
						//Category is allowed
						eaPush(&eaAllowedRegions, pModRegion);
						eaPush(&eaNewCategories, pModifiedCat = pOverlayCat);
					}
					else if (pOverlayMatchCat)
					{
						eaPush(&eaAllowedRegions, pModRegion);
						eaPush(&eaNewCategories, pModifiedCat = pOverlayMatchCat);
					}
				}
				else
				{
					eaPush(&eaAllowedRegions, pModRegion);
					eaPush(&eaNewCategories, pModifiedCat);
				}
			}
			else
			{
				PCCategory **eaModRegionCategoryList = NULL;

				for (j = 0; j < eaSize(&pModRegion->eaCategories); ++j)
				{
					PCCategory *pCat = GET_REF(pModRegion->eaCategories[j]->hCategory);
					if (pCat && !pCat->bHidden && (!pSlotType || costumeTailor_CategoryIncludedInSlotType(pCat, pSlotType)))
					{
						eaPush(&eaModRegionCategoryList, pCat);
					}
				}

				for(j=eaSize(&eaBaseParts)-1; j>=0; --j) {
					PCBoneDef *pBaseBone, *pOverlayBone;
					NOCONST(PCPart) *pBasePart, *pOverlayPart;

					// Collect Info
					pBasePart = eaBaseParts[j];
					pBaseBone = eaBaseBones[j];
					pOverlayPart = eaOverlayParts[j];
					pOverlayBone = pOverlayPart ? GET_REF(pOverlayPart->hBoneDef) : NULL;

					if (!pBaseBone) continue;

					if ((!pBasePart) && (!pOverlayPart)) {
						continue;
					}

					if (GET_REF(pBaseBone->hRegion) != pModRegion) {
						continue; // Not in region
					}

					if (pOverlayPart)
					{
						pGeo = GET_REF(pOverlayPart->hGeoDef);
						if (pSkel != pOverlaySkel && pGeo)
						{
							int iGeo;
							PCGeometryDef **eaGeos = NULL;
							costumeTailor_GetValidGeos(pModifiedCostume, pSkel, pBaseBone, pModifiedCat, pSpecies, eaUnlockedCostumes, &eaGeos, false, false, false, false);
							iGeo = costumeTailor_GetMatchingGeoIndex(pGeo, eaGeos);
							if (iGeo >= 0)
							{
								pGeo = eaGeos[iGeo];
							}
							eaDestroy(&eaGeos);
						}
					}
					else
					{
						pGeo = GET_REF(pBasePart->hGeoDef);
					}

					if (pGeo)
					{
						for (k = eaSize(&eaModRegionCategoryList)-1; k >= 0; --k)
						{
							for(l=eaSize(&pGeo->eaCategories)-1; l>=0; --l) {
								if (GET_REF(pGeo->eaCategories[l]->hCategory) == eaModRegionCategoryList[k]) {
									break;
								}
							}
							if (l < 0) {
								// Part is not legal in the category
								eaRemove(&eaModRegionCategoryList, k);
							}
						}
					}
				}

				// For each existing part in the region that isn't in the
				//  base parts list, any categories in the possible category
				//  list for which the part's geo is not legal are removed
				//  from consideration.  This is to ensure that no existing
				//  parts in the region will be illegal if the category changes.
				for(j = eaSize(&pBaseCostume->eaParts)-1; j>=0; --j)
				{
					PCPart *pPart = pBaseCostume->eaParts[j];
					PCBoneDef *pBaseBone = GET_REF(pPart->hBoneDef);

					if (!pBaseBone)
						continue;
					
					if (GET_REF(pBaseBone->hRegion) != pModRegion)
						continue;

					pGeo = GET_REF(pPart->hGeoDef);
					if (!pGeo)
						continue;

					if (pGeo->pcName && !stricmp(pGeo->pcName, "none"))
						continue;

					k = eaFind(&eaBaseParts,CONTAINER_NOCONST(PCPart, pPart));
					if (k >= 0)
						continue;

					for (k = eaSize(&eaModRegionCategoryList)-1; k >= 0; --k)
					{
						for(l=eaSize(&pGeo->eaCategories)-1; l>=0; --l) {
							if (GET_REF(pGeo->eaCategories[l]->hCategory) == eaModRegionCategoryList[k]) {
								break;
							}
						}
						if (l < 0)
						{
							// Part is not legal in the category

							// Before removing the category from consideration, see
							//  if there's another geo in the dictionary to replace it
							//  which presumably is legal in this category.  If so,
							//  don't remove the category from consideration.
							if(!(gConf.bCostumeCategoryReplace && costumeTailor_FindCategoryReplacementGeo(pGeo,eaModRegionCategoryList[k])))
							{
								eaRemove(&eaModRegionCategoryList, k);
							}
						}
					}

					// Minor optimization - if there are no more possible categories we
					//  can break the loop now, since we know it won't work
					if(!eaSize(&eaModRegionCategoryList))
						break;
				}

				if (eaSize(&eaModRegionCategoryList))
				{
					eaPush(&eaAllowedRegions, pModRegion);

					// Fail safe check if there's something wrong with the overlay's category
					j = eaFind(&eaModRegionCategoryList, pModifiedCat);
					eaPush(&eaNewCategories, pModifiedCat = eaModRegionCategoryList[MAX(j, 0)]);

					eaPushUnique(&eaModifiedRegions, pModRegion);
				}

				eaDestroy(&eaModRegionCategoryList);
			}
		}
		else if (pModRegion)
		{
			eaPush(&eaAllowedRegions, pModRegion);
			eaPush(&eaNewCategories, pModifiedCat);
		}
	}

	pSavedCostume = StructCloneNoConst(parse_PlayerCostume,pModifiedCostume);

	// Apply costume parts
	for(i = eaSize(&eaOverlayParts)-1; i>=0; --i) {
		PCBoneDef *pBaseBone, *pOverlayBone;
		NOCONST(PCPart) *pBasePart, *pOverlayPart;
		NOCONST(PCPart) *pNewPart = NULL;
		PCCategory *pCat = NULL;
		int iCurRegCat = 0;

		// Collect Info
		pBasePart = eaBaseParts[i];
		pBaseBone = eaBaseBones[i];
		if (!pBaseBone) {
			continue;
		}

		for(j = eaSize(&eaAllowedRegions)-1; j>=0; --j)
		{
			if (GET_REF(pBaseBone->hRegion) == eaAllowedRegions[j])
			{
				iCurRegCat = j;
				break;
			}
		}
		if (j < 0) continue; //Region not allowed

		pOverlayPart = eaOverlayParts[i];
		if (!pOverlayPart)
		{
			// Remove any previous part for this bone
			if ((!costumeTailor_IsBoneRequired(pModifiedCostume, pBaseBone, pSpecies)) && pBasePart)
			{
				for(j=eaSize(&pBaseCostume->eaParts)-1; j>=0; --j) {
					if (pBaseCostume->eaParts[j] == (PCPart*)pBasePart) {
						StructDestroy(parse_PCPart, pBaseCostume->eaParts[j]);
						eaRemove(&pModifiedCostume->eaParts, j);

						// Remove any part on child bones of this bone that is not an overlay part
						for (k = eaSize(&pBaseBone->eaChildBones)-1; k >= 0; --k)
						{
							PCBoneDef *cb = GET_REF(pBaseBone->eaChildBones[k]->hChildBone);
							if (!cb) continue;
							for(l=eaSize(&pBaseCostume->eaParts)-1; l>=0; --l)
							{
								PCBoneDef *b = GET_REF(pBaseCostume->eaParts[l]->hBoneDef);
								if (!b || (cb != b)) continue;

								StructDestroy(parse_PCPart, pBaseCostume->eaParts[l]);
								eaRemove(&pModifiedCostume->eaParts, l);
								if (l <= j) {
									--j;
								}
								break;
							}
						}
					}
				}
			}
			continue;
		}
		pOverlayBone = GET_REF(pOverlayPart->hBoneDef);
		if (!pOverlayBone) {
			continue;
		}

		pCat = eaNewCategories[iCurRegCat];
		costumeTailor_SetRegionCategory(pModifiedCostume, eaAllowedRegions[iCurRegCat], pCat);

		// Check if part is legal for the costume
		pGeo = GET_REF(pOverlayPart->hGeoDef);
		if (pGeo && pCat)
		{
			if (pSkel != pOverlaySkel)
			{
				int iGeo;
				PCGeometryDef **eaGeos = NULL;
				costumeTailor_GetValidGeos(pModifiedCostume, pSkel, pBaseBone, pCat, pSpecies, eaUnlockedCostumes, &eaGeos, false, false, false, false);
				iGeo = costumeTailor_GetMatchingGeoIndex(pGeo, eaGeos);
				if (iGeo >= 0)
				{
					pGeo = eaGeos[iGeo];
				}
				eaDestroy(&eaGeos);
			}
			for(j=eaSize(&pGeo->eaCategories)-1; j>=0; --j) {
				if (GET_REF(pGeo->eaCategories[j]->hCategory) == pCat) {
					break;
				}
			}
			if (j < 0) {
				// Part is not legal in the category
				continue;
			}
		}

		if (pOverlayPart)
		{
			pNewPart = StructCloneNoConst(parse_PCPart, pOverlayPart);
			SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, pBaseBone, pNewPart->hBoneDef);
			if (pNewPart && GET_REF(pBaseCostume->hSpecies) && (!bIgnoreSpeciesMatching) && !costumeTailor_BoneIsExcluded(CONTAINER_NOCONST(PlayerCostume, pBaseCostume), pBaseBone))
			{
				bool fail = false;
				int iIndex;
				PCGeometryDef **eaGoes = NULL;
				PCGeometryDef *pGeoTemp = NULL;
				PCMaterialDef *pMatTemp = NULL;

				//Make sure the part is valid for the species
				if (GET_REF(pNewPart->hGeoDef) && stricmp(GET_REF(pNewPart->hGeoDef)->pcName,"None"))
				{
					costumeTailor_GetValidGeos(pModifiedCostume, pSkel, pBaseBone, pCat, pSpecies, eaUnlockedCostumes, &eaGoes, false, false, false, false);
					iIndex = costumeTailor_GetMatchingGeoIndex(GET_REF(pNewPart->hGeoDef), eaGoes);
					if (iIndex >= 0)
					{
						pGeoTemp = eaGoes[iIndex];
						SET_HANDLE_FROM_REFERENT("CostumeGeometry", pGeoTemp, pNewPart->hGeoDef);
					}
					else
					{
						fail = true;
					}
					eaDestroy(&eaGoes);
				}

				if (pGeoTemp && GET_REF(pNewPart->hMatDef) && stricmp(GET_REF(pNewPart->hMatDef)->pcName,"None") && !fail)
				{
					PCMaterialDef **eaMats = NULL;
					costumeTailor_GetValidMaterials(pModifiedCostume, pGeoTemp, pSpecies, NULL, NULL, eaUnlockedCostumes, &eaMats, false, false, false);
					iIndex = costumeTailor_GetMatchingMatIndex(GET_REF(pNewPart->hMatDef), eaMats);
					if (iIndex >= 0)
					{
						pMatTemp = eaMats[iIndex];
						SET_HANDLE_FROM_REFERENT("CostumeMaterial", pMatTemp, pNewPart->hMatDef);
					}
					else
					{
						fail = true;
					}
					eaDestroy(&eaMats);
				}

				if (pGeoTemp && pMatTemp && !fail)
				{
					PCTextureDef **eaTexs = NULL;

					if (GET_REF(pNewPart->hPatternTexture) && stricmp(GET_REF(pNewPart->hPatternTexture)->pcName,"None"))
					{
						costumeTailor_GetValidTextures(pModifiedCostume, pMatTemp, pSpecies, NULL, NULL, pGeoTemp, NULL, eaUnlockedCostumes, kPCTextureType_Pattern, &eaTexs, false, false, false);
						iIndex = costumeTailor_GetMatchingTexIndex(GET_REF(pNewPart->hPatternTexture), eaTexs);
						if (iIndex >= 0)
						{
							SET_HANDLE_FROM_REFERENT("CostumeTexture", eaTexs[iIndex], pNewPart->hPatternTexture);
						}
						else
						{
							fail = true;
						}
						eaDestroy(&eaTexs);
					}

					if (GET_REF(pNewPart->hDetailTexture) && stricmp(GET_REF(pNewPart->hDetailTexture)->pcName,"None") && !fail)
					{
						costumeTailor_GetValidTextures(pModifiedCostume, pMatTemp, pSpecies, NULL, NULL, pGeoTemp, NULL, eaUnlockedCostumes, kPCTextureType_Detail, &eaTexs, false, false, false);
						iIndex = costumeTailor_GetMatchingTexIndex(GET_REF(pNewPart->hDetailTexture), eaTexs);
						if (iIndex >= 0)
						{
							SET_HANDLE_FROM_REFERENT("CostumeTexture", eaTexs[iIndex], pNewPart->hDetailTexture);
						}
						else
						{
							fail = true;
						}
						eaDestroy(&eaTexs);
					}

					if (GET_REF(pNewPart->hSpecularTexture)  && stricmp(GET_REF(pNewPart->hSpecularTexture)->pcName,"None")&& !fail)
					{
						costumeTailor_GetValidTextures(pModifiedCostume, pMatTemp, pSpecies, NULL, NULL, pGeoTemp, NULL, eaUnlockedCostumes, kPCTextureType_Specular, &eaTexs, false, false, false);
						iIndex = costumeTailor_GetMatchingTexIndex(GET_REF(pNewPart->hSpecularTexture), eaTexs);
						if (iIndex >= 0)
						{
							SET_HANDLE_FROM_REFERENT("CostumeTexture", eaTexs[iIndex], pNewPart->hSpecularTexture);
						}
						else
						{
							fail = true;
						}
						eaDestroy(&eaTexs);
					}

					if (GET_REF(pNewPart->hDiffuseTexture) && stricmp(GET_REF(pNewPart->hDiffuseTexture)->pcName,"None") && !fail)
					{
						costumeTailor_GetValidTextures(pModifiedCostume, pMatTemp, pSpecies, NULL, NULL, pGeoTemp, NULL, eaUnlockedCostumes, kPCTextureType_Diffuse, &eaTexs, false, false, false);
						iIndex = costumeTailor_GetMatchingTexIndex(GET_REF(pNewPart->hDiffuseTexture), eaTexs);
						if (iIndex >= 0)
						{
							SET_HANDLE_FROM_REFERENT("CostumeTexture", eaTexs[iIndex], pNewPart->hDiffuseTexture);
						}
						else
						{
							fail = true;
						}
						eaDestroy(&eaTexs);
					}

					if (pNewPart->pMovableTexture && GET_REF(pNewPart->pMovableTexture->hMovableTexture) && stricmp(GET_REF(pNewPart->pMovableTexture->hMovableTexture)->pcName,"None") && !fail)
					{
						costumeTailor_GetValidTextures(pModifiedCostume, pMatTemp, pSpecies, NULL, NULL, pGeoTemp, NULL, eaUnlockedCostumes, kPCTextureType_Movable, &eaTexs, false, false, false);
						iIndex = costumeTailor_GetMatchingTexIndex(GET_REF(pNewPart->pMovableTexture->hMovableTexture), eaTexs);
						if (iIndex >= 0)
						{
							SET_HANDLE_FROM_REFERENT("CostumeTexture", eaTexs[iIndex], pNewPart->pMovableTexture->hMovableTexture);
						}
						else
						{
							fail = true;
						}
						eaDestroy(&eaTexs);
					}
				}

				//Validate Colors
				if (!fail)
				{
					if (bUseClosestColors)
					{
						costumeTailor_FindClosestColor(pNewPart->color0, costumeTailor_GetColorSetForPart(pModifiedCostume, pSpecies, NULL, pNewPart, 0), pNewPart->color0);
						costumeTailor_FindClosestColor(pNewPart->color1, costumeTailor_GetColorSetForPart(pModifiedCostume, pSpecies, NULL, pNewPart, 1), pNewPart->color1);
						costumeTailor_FindClosestColor(pNewPart->color2, costumeTailor_GetColorSetForPart(pModifiedCostume, pSpecies, NULL, pNewPart, 2), pNewPart->color2);
						costumeTailor_FindClosestColor(pNewPart->color3, costumeTailor_GetColorSetForPart(pModifiedCostume, pSpecies, NULL, pNewPart, 3), pNewPart->color3);
					}
					else
					{
						fail = (kPCColorFlags_Color0|kPCColorFlags_Color1|kPCColorFlags_Color2|kPCColorFlags_Color3) != costumeTailor_IsColorValidForPart(pModifiedCostume, pSpecies, NULL, pNewPart, pNewPart->color0, pNewPart->color1, pNewPart->color2, pNewPart->color3, true);
					}
				}

				if (fail)
				{
					//Attempt to replace with valid costume part failed, leave it alone
					StructDestroyNoConst(parse_PCPart, pNewPart);

					//Restore parts in the same region
					for(j=eaSize(&pBaseCostume->eaParts)-1; j>=0; --j)
					{
						PCBoneDef *pB = GET_REF(pBaseCostume->eaParts[j]->hBoneDef);
						if (!pB) continue;
						if (GET_REF(pB->hRegion) != eaAllowedRegions[iCurRegCat]) continue;
						StructDestroy(parse_PCPart, pBaseCostume->eaParts[j]);
						eaRemove(&pModifiedCostume->eaParts, j);
					}

					for (j=eaSize(&pSavedCostume->eaParts)-1; j>=0; --j)
					{
						PCBoneDef *pB = GET_REF(pSavedCostume->eaParts[j]->hBoneDef);
						if (!pB) continue;
						if (GET_REF(pB->hRegion) != eaAllowedRegions[iCurRegCat]) continue;
						pNewPart = StructCloneNoConst(parse_PCPart,pSavedCostume->eaParts[j]);
						if (pNewPart) eaPush(&pModifiedCostume->eaParts, pNewPart);
					}

					costumeTailor_SetRegionCategory(pModifiedCostume, eaAllowedRegions[iCurRegCat], costumeTailor_GetCategoryForRegion((PlayerCostume*)pSavedCostume, eaAllowedRegions[iCurRegCat]));

					eaRemove(&eaAllowedRegions, iCurRegCat);
					eaRemove(&eaNewCategories, iCurRegCat);
					continue;
				}
			}
		}

		// Remove any previous part for this bone
		if ((pNewPart || !costumeTailor_IsBoneRequired(pModifiedCostume, pBaseBone, pSpecies)) && pBasePart)
		{
			for(j=eaSize(&pBaseCostume->eaParts)-1; j>=0; --j) {
				if (pBaseCostume->eaParts[j] == (PCPart*)pBasePart) {
					StructDestroy(parse_PCPart, pBaseCostume->eaParts[j]);
					eaRemove(&pModifiedCostume->eaParts, j);

					// Remove any part on child bones of this bone that is not an overlay part
					for (k = eaSize(&pBaseBone->eaChildBones)-1; k >= 0; --k)
					{
						PCBoneDef *cb = GET_REF(pBaseBone->eaChildBones[k]->hChildBone);
						if (!cb) continue;
						for(l=eaSize(&pBaseCostume->eaParts)-1; l>=0; --l)
						{
							PCBoneDef *b = GET_REF(pBaseCostume->eaParts[l]->hBoneDef);
							if (!b || (cb != b)) continue;
							for (m = eaSize(&eaBaseBones)-1; m >= 0; --m)
							{
								if (eaBaseBones[m] == b) break;
							}
							if (m < 0) {
								StructDestroy(parse_PCPart, pBaseCostume->eaParts[l]);
								eaRemove(&pModifiedCostume->eaParts, l);
								if (l <= j) {
									--j;
								}
							}
							break;
						}
					}
				}
			}
		}

		//Time to apply dye overrides
		if (pData)
		{
			//0,0,0 is a special color value that means "don't dye this"
			if ((int)pData->vDyeColors[0][0] != 0 ||
				(int)pData->vDyeColors[0][1] != 0 ||
				(int)pData->vDyeColors[0][2] != 0)
			{
				pNewPart->color0[0] = pData->vDyeColors[0][0];
				pNewPart->color0[1] = pData->vDyeColors[0][1];
				pNewPart->color0[2] = pData->vDyeColors[0][2];
				pNewPart->color0[3] = 255;
			}
			if ((int)pData->vDyeColors[1][0] != 0 ||
				(int)pData->vDyeColors[1][1] != 0 ||
				(int)pData->vDyeColors[1][2] != 0)
			{
				pNewPart->color1[0] = pData->vDyeColors[1][0];
				pNewPart->color1[1] = pData->vDyeColors[1][1];
				pNewPart->color1[2] = pData->vDyeColors[1][2];
				pNewPart->color1[3] = 255;
			}
			if ((int)pData->vDyeColors[2][0] != 0 ||
				(int)pData->vDyeColors[2][1] != 0 ||
				(int)pData->vDyeColors[2][2] != 0)
			{
				pNewPart->color2[0] = pData->vDyeColors[2][0];
				pNewPart->color2[1] = pData->vDyeColors[2][1];
				pNewPart->color2[2] = pData->vDyeColors[2][2];
				pNewPart->color2[3] = 255;
			}
			if ((int)pData->vDyeColors[3][0] != 0 ||
				(int)pData->vDyeColors[3][1] != 0 ||
				(int)pData->vDyeColors[3][2] != 0)
			{
				pNewPart->color3[0] = pData->vDyeColors[3][0];
				pNewPart->color3[1] = pData->vDyeColors[3][1];
				pNewPart->color3[2] = pData->vDyeColors[3][2];
				pNewPart->color3[3] = 255;
			}
			if (IS_HANDLE_ACTIVE(pData->hDyeMat))
			{
				COPY_HANDLE(pNewPart->hMatDef, pData->hDyeMat);
			}
		}

		// Apply part to the costume
		if (pNewPart)
		{
			eaPush(&pModifiedCostume->eaParts, pNewPart);
			bModified = true;
		}
	}

	// Attempt to do category replacement on any parts that aren't legal due to regions changing categories
	if(gConf.bCostumeCategoryReplace)
	{
		for(i=eaSize(&eaModifiedRegions)-1; i>=0; i--)
		{
			PCRegion *pRegion = eaModifiedRegions[i];
			PCCategory *pCategory = costumeTailor_GetCategoryForRegion((PlayerCostume*)pModifiedCostume,pRegion);

			// For each existing part in the region, if the part's geo is not
			//  legal in the region's category, attempt to find a replacement.
			for(j = eaSize(&pModifiedCostume->eaParts)-1; j>=0; --j)
			{
				PCPart *pPart = ((PlayerCostume*)pModifiedCostume)->eaParts[j];
				PCBoneDef *pBaseBone = GET_REF(pPart->hBoneDef);

				if (!pBaseBone)
					continue;

				if (GET_REF(pBaseBone->hRegion) != pRegion)
					continue;

				pGeo = GET_REF(pPart->hGeoDef);
				if (!pGeo)
					continue;

				if (pGeo->pcName && !stricmp(pGeo->pcName, "none"))
					continue;

				for(k=eaSize(&pGeo->eaCategories)-1; k>=0; --k) {
					if (GET_REF(pGeo->eaCategories[k]->hCategory) == pCategory) {
						break;
					}
				}
				if (k < 0)
				{
					// Part is not legal in the category, check for replacement
					PCGeometryDef *pGeoReplace = costumeTailor_FindCategoryReplacementGeo(pGeo,pCategory);
					if(pGeoReplace)
					{
						// TODO(JW): Hack: This doesn't do any validation that the replacement geo
						//  allows the pre-existing material and texture.
						SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict,pGeoReplace,pPart->hGeoDef);
					}
				}
			}
		}
	}

	if (eaModifiedRegions) eaDestroy(&eaModifiedRegions);
	if (eaAllowedRegions) eaDestroy(&eaAllowedRegions);
	if (eaNewCategories) eaDestroy(&eaNewCategories);
	if (eaBaseParts) eaDestroy(&eaBaseParts);
	if (eaOverlayParts) eaDestroy(&eaOverlayParts);
	if (eaBaseBones) eaDestroy(&eaBaseBones);
	StructDestroyNoConst(parse_PlayerCostume, pSavedCostume);
	StructDestroy(parse_PlayerCostume, pCostumeToOverlayReplace);
	return bModified;
}

bool costumeTailor_ApplyCostumeOverlay(PlayerCostume *pBaseCostume, CostumeDisplayData* pData, PlayerCostume *pCostumeToOverlay, PlayerCostume **eaUnlockedCostumes, const char *pchBoneGroup, PCSlotType *pSlotType, bool bForceCategory, bool bIgnoreSpeciesMatching, bool bIgnoreSkelMatching, bool bUseClosestColors)
{
	int i;
	PCBoneGroup *bg = NULL;
	PCSkeletonDef *pSkel;
	bool bModified;

	if (!pBaseCostume) return false;

	//Get Skeleton
	pSkel = GET_REF(pBaseCostume->hSkeleton);
	if (!pSkel) {
		return false;
	}

	if (pchBoneGroup && *pchBoneGroup)
	{
		for (i = eaSize(&pSkel->eaBoneGroups)-1; i >= 0; --i)
		{
			if (!stricmp(pchBoneGroup, pSkel->eaBoneGroups[i]->pcName))
			{
				bg = pSkel->eaBoneGroups[i];
				break;
			}
		}
		if (!bg) return false;
	}

	bModified = costumeTailor_ApplyCostumeOverlayBG(pBaseCostume, pData, pCostumeToOverlay, eaUnlockedCostumes, bg, pSlotType, bForceCategory, bIgnoreSpeciesMatching, bIgnoreSkelMatching, bUseClosestColors, false);

	if(!bModified && gConf.bCostumeCategoryReplace)
		bModified = costumeTailor_ApplyCostumeOverlayBG(pBaseCostume, pData, pCostumeToOverlay, eaUnlockedCostumes, bg, pSlotType, bForceCategory, bIgnoreSpeciesMatching, bIgnoreSkelMatching, bUseClosestColors, true);

	return bModified;
}

// Helper function for gslEntityGenerateCostume
static int costumeTailor_CompareCostumeDisplayData(const CostumeDisplayData **left, const CostumeDisplayData **right)
{
	return (((*left)->iPriority == (*right)->iPriority) ? 0 : (((*left)->iPriority > (*right)->iPriority) ? 1 : -1));
}

// Helper function for gslEntityGenerateCostume
static F32 costumeTailor_ModifyCostumeValue(F32 fOrig, kCostumeValueMode eMode, F32 fValue, F32 fMinValue, F32 fMaxValue)
{
	switch(eMode) {
		case kCostumeValueMode_Increment_Value: 
			return CLAMP(fOrig + fValue, fMinValue, fMaxValue > 0 ? fMaxValue : 100.0);
		case kCostumeValueMode_Scale_Value:
			return CLAMP(fOrig * fValue, fMinValue, fMaxValue > 0 ? fMaxValue : 100.0);
		case kCostumeValueMode_Set_Value:
			return CLAMP(fValue, fMinValue, fMaxValue > 0 ? fMaxValue : 100.0);
		default:
			assertmsg(0, "Unexpected value");
	}
}

NOCONST(PlayerCostume) *costumeTailor_ApplyOverrideSet(PlayerCostume *pBaseCostume, PCSlotType *pSlotType, CostumeDisplayData **eaData, SpeciesDef *pSpecies)
{
	int i, j;
	int iStance;
	NOCONST(PlayerCostume) *pNewCostume = NULL;

	// Sort it on priority
	eaQSort(eaData, costumeTailor_CompareCostumeDisplayData);

	// Look for the highest "replace always" and clip below that to avoid extra work
	for(i=eaSize(&eaData)-1; i>=0; --i) {
		if (eaData[i]->eMode == kCostumeDisplayMode_Replace_Always) {
			--i;
			while(i >= 0) {
				eaDestroy(&eaData[i]->eaCostumes);
				eaDestroyStruct(&eaData[i]->eaCostumesOwned, parse_PlayerCostume);
				free(eaData[i]);
				eaRemove(&eaData, i);
				--i;
			}
		}
	}

	// Create thew new costume by cloning the base one
	pNewCostume = StructCloneDeConst(parse_PlayerCostume, pBaseCostume);

	if (pNewCostume) {

		for(i=0; i<eaSize(&eaData); ++i) {
			//Additional FX from items
			if (eaSize(&eaData[i]->eaAddedFX) > 0)
			{
				if (!pNewCostume->pArtistData) {
					pNewCostume->pArtistData = StructCreateNoConst(parse_PCArtistCostumeData);
				}
				if (pNewCostume->pArtistData) {
					NOCONST(PCFX)** eaFxNC = (NOCONST(PCFX)**)(eaData[i]->eaAddedFX);
					eaPushEArray(&pNewCostume->pArtistData->eaFX, &eaFxNC);
				}
			}

		// Apply costume display data
			if (eaData[i]->eType == kCostumeDisplayType_Value_Change) {
				// Apply a value change on the costume
				switch(eaData[i]->eValueArea) {
					case kCostumeValueArea_Height:
						pNewCostume->fHeight = costumeTailor_ModifyCostumeValue(pNewCostume->fHeight, eaData[i]->eValueMode, eaData[i]->fValue, eaData[i]->fMinValue, eaData[i]->fMaxValue);
					xcase kCostumeValueArea_Mass:
						if (eafSize(&pNewCostume->eafBodyScales) > 0) 
						{
							pNewCostume->eafBodyScales[0] = costumeTailor_ModifyCostumeValue(pNewCostume->eafBodyScales[0], eaData[i]->eValueMode, eaData[i]->fValue, eaData[i]->fMinValue, eaData[i]->fMaxValue);
						}
					xcase kCostumeValueArea_Transparency:
						if (pNewCostume->pArtistData) {
							pNewCostume->pArtistData->fTransparency = costumeTailor_ModifyCostumeValue(pNewCostume->pArtistData->fTransparency, eaData[i]->eValueMode, eaData[i]->fValue, eaData[i]->fMinValue, eaData[i]->fMaxValue);
						}
					xdefault:
						assertmsg(0, "Unexpected value");
				}

			} else if (eaData[i]->eType != kCostumeDisplayType_Costume) {
				assertmsg(0, "Unexpected value");

			} else if ((eaData[i]->eMode == kCostumeDisplayMode_Replace_Always) && (eaSize(&eaData[i]->eaCostumes) || eaSize(&eaData[i]->eaCostumesOwned))) {
				// A "replace always" mode simply replaces the previous costume
				StructDestroyNoConst(parse_PlayerCostume, pNewCostume);
				if (eaSize(&eaData[i]->eaCostumes))
				{
					pNewCostume = StructCloneDeConst(parse_PlayerCostume, eaData[i]->eaCostumes[0]);
				}
				else
				{
					pNewCostume = StructCloneDeConst(parse_PlayerCostume, eaData[i]->eaCostumesOwned[0]);
				}

			} else if ((eaData[i]->eMode == kCostumeDisplayMode_Replace_Match) && (eaSize(&eaData[i]->eaCostumes) || eaSize(&eaData[i]->eaCostumesOwned))) {
				// A "replace match" only replaces if it finds a matching skeleton
				for(j=0; j<eaSize(&eaData[i]->eaCostumes); ++j) {
					if (GET_REF(eaData[i]->eaCostumes[j]->hSkeleton) == GET_REF(pNewCostume->hSkeleton)) {
						StructDestroyNoConst(parse_PlayerCostume, pNewCostume);
						pNewCostume = StructCloneDeConst(parse_PlayerCostume, eaData[i]->eaCostumes[j]);
						break;
					}
				}
				if (j >= eaSize(&eaData[i]->eaCostumes))
				{
					for(j=0; j<eaSize(&eaData[i]->eaCostumesOwned); ++j) {
						if (GET_REF(eaData[i]->eaCostumesOwned[j]->hSkeleton) == GET_REF(pNewCostume->hSkeleton)) {
							StructDestroyNoConst(parse_PlayerCostume, pNewCostume);
							pNewCostume = StructCloneDeConst(parse_PlayerCostume, eaData[i]->eaCostumesOwned[j]);
							break;
						}
					}
				}
			} else if (eaData[i]->eMode == kCostumeDisplayMode_Overlay ||
					   eaData[i]->eMode == kCostumeDisplayMode_Overlay_Always) {
				//If the costume display mode is set to "Override_Always" then ignore skeleton matching
				bool bIgnoreSkelMatching = (eaData[i]->eMode == kCostumeDisplayMode_Overlay_Always);
				// Overlay looks until it finds a costume that applies changes
				for(j=0; j<eaSize(&eaData[i]->eaCostumes); ++j) {
					SET_HANDLE_FROM_REFERENT("Species",pSpecies,pNewCostume->hSpecies);
					if (costumeTailor_ApplyCostumeOverlay((PlayerCostume*)pNewCostume, eaData[i], eaData[i]->eaCostumes[j], NULL, NULL, pSlotType, false, true, bIgnoreSkelMatching, false)) {
						break;
					}
				}
				if (j >= eaSize(&eaData[i]->eaCostumes))
				{
					for(j=0; j<eaSize(&eaData[i]->eaCostumesOwned); ++j) {
						SET_HANDLE_FROM_REFERENT("Species",pSpecies,pNewCostume->hSpecies);
						if (costumeTailor_ApplyCostumeOverlay((PlayerCostume*)pNewCostume, eaData[i], eaData[i]->eaCostumesOwned[j], NULL, NULL, pSlotType, false, true, bIgnoreSkelMatching, false)) {
							break;
						}
					}
				}
			} else {
				assertmsg(0, "Unexpected value");
			}
			if (!pNewCostume->pArtistData && eaSize(&eaData[i]->eaStances) > 0)
				pNewCostume->pArtistData = StructCreateNoConst(parse_PCArtistCostumeData);

			for (iStance = 0; iStance < eaSize(&eaData[i]->eaStances); iStance++)
			{
				NOCONST(PCBitName)* pBit = StructCreateNoConst(parse_PCBitName);
				pBit->pcName = allocAddString(eaData[i]->eaStances[iStance]);
				eaPush(&pNewCostume->pArtistData->eaConstantBits, pBit);
			}
		}
	}

	return pNewCostume;
}

NOCONST(PlayerCostume) *costumeTailor_ApplyMount(	CostumeDisplayData **eaData,
													F32 *fOutMountScaleOverride)
{
	int i;
	NOCONST(PlayerCostume) *pNewCostume = NULL;

	// Sort it on priority
	eaQSort(eaData, costumeTailor_CompareCostumeDisplayData);

	// Look for the highest "replace always" and clip below that to avoid extra work
	for(i=eaSize(&eaData)-1; i>=0; --i) {
		if (eaData[i]->eMode == kCostumeDisplayMode_Replace_Always) {
			--i;
			while(i >= 0) {
				eaDestroy(&eaData[i]->eaCostumes);
				eaDestroyStruct(&eaData[i]->eaCostumesOwned, parse_PlayerCostume);
				free(eaData[i]);
				eaRemove(&eaData, i);
				--i;
			}
		}
	}

	for(i=0; i<eaSize(&eaData); ++i)
	{
		if ((eaData[i]->eMode == kCostumeDisplayMode_Replace_Always) && (eaSize(&eaData[i]->eaCostumes) || eaSize(&eaData[i]->eaCostumesOwned))) {
			// A "replace always" mode simply replaces the previous costume
			if (eaSize(&eaData[i]->eaCostumes))
			{
				pNewCostume = StructCloneDeConst(parse_PlayerCostume, eaData[i]->eaCostumes[0]);
				*fOutMountScaleOverride = eaData[i]->fMountScaleOverride;
			}
			else
			{
				pNewCostume = StructCloneDeConst(parse_PlayerCostume, eaData[i]->eaCostumesOwned[0]);
				*fOutMountScaleOverride = eaData[i]->fMountScaleOverride;
			}
		} else {
			assertmsg(0, "Unexpected value");
		}
	}

	return pNewCostume;
}

void costumeTailor_ChangeSkeleton(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, PCSlotType *pSlotType, PCSkeletonDef *pSkel)
{
	DictionaryEArrayStruct *pGeoStruct = resDictGetEArrayStruct(g_hCostumeGeometryDict);
	PCGeometryDef *pGeo, *pMatchingGeo;
	PCMaterialDef *pMat, *pMatchingMat;
	PCTextureDef *pTex, *pMatchingTex;
	PCBoneDef *pBone;
	PCRegion **eaRegions = NULL;
	PCCategory **eaCategories = NULL;
	NOCONST(PCPart) *pPart;
	int i,j,k;
	bool bFound;

	if (!pSkel) {
		return;
	}

	// Set the skeleton
	SET_HANDLE_FROM_REFERENT(g_hCostumeSkeletonDict, pSkel, pPCCostume->hSkeleton);

	// Strip the costume
	costumeTailor_StripUnnecessary(pPCCostume);

	// Find conversion for regions
		for (i=eaSize(&pPCCostume->eaRegionCategories)-1; i>=0; --i) {
		StructDestroyNoConst(parse_PCRegionCategory, pPCCostume->eaRegionCategories[i]);
	}
	eaClear(&pPCCostume->eaRegionCategories);

	// Remove non-existing regions and replace ones with a match
	costumeTailor_GetValidRegions(pPCCostume, pSpecies, NULL, NULL, pSlotType, &eaRegions, CGVF_OMIT_EMPTY | CGVF_REQUIRE_POWERFX);
	for(i=eaSize(&pPCCostume->eaRegionCategories)-1; i>=0; --i) {
		NOCONST(PCRegionCategory) *pRegCat = pPCCostume->eaRegionCategories[i];
		PCRegion *pReg = GET_REF(pRegCat->hRegion);
		if (!pReg) {
			// Region does not exist
			eaRemove(&pPCCostume->eaRegionCategories, i);
		} else if (eaFind(&eaRegions, pReg) == -1) {
			// Region is not legal on the new skeleton, so check for match
			const char *pcRegName = TranslateDisplayMessage(pReg->displayNameMsg);
			for(j=eaSize(&eaRegions)-1; j>=0; --j) {
				const char *pcOtherRegName = TranslateDisplayMessage(eaRegions[j]->displayNameMsg);
				if (pcRegName && pcOtherRegName && (stricmp(pcRegName, pcOtherRegName) == 0)) {
					// Found match on display name, so now check category
					PCCategory *pCat = GET_REF(pRegCat->hCategory);
					costumeTailor_GetValidCategories(pPCCostume, eaRegions[j], pSpecies, NULL, NULL, pSlotType, &eaCategories, CGVF_OMIT_EMPTY | CGVF_REQUIRE_POWERFX);
					SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, eaRegions[j], pRegCat->hRegion);
					if (!pCat) {
						// Category does not exist
						eaRemove(&pPCCostume->eaRegionCategories, i);
					} else if (eaFind(&eaCategories, pCat) == -1) {
						// Category is not legal on new region, so check for match
						const char *pcCatName = TranslateDisplayMessage(pCat->displayNameMsg);
						for(k=eaSize(&eaCategories)-1; k>=0; --k) {
							const char *pcOtherCatName = TranslateDisplayMessage(eaCategories[k]->displayNameMsg);
							if (pcCatName && pcOtherCatName && (stricmp(pcCatName, pcOtherCatName) == 0)) {
								SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, eaCategories[k], pRegCat->hCategory);
								break;
							}
						}
						if (k < 0) {
							eaRemove(&pPCCostume->eaRegionCategories, i);
						}
					}
					eaDestroy(&eaCategories);
					break;
				}
			}
			if (j < 0) {
				eaRemove(&pPCCostume->eaRegionCategories, i);
			}
		}
	}

	// Add missing regions and validate existing ones
	for(i=eaSize(&eaRegions)-1; i>=0; --i) {
		if (!costumeTailor_GetCategoryForRegion((PlayerCostume*)pPCCostume, eaRegions[i])) {
			// Region is missing so need to add an entry
			NOCONST(PCRegionCategory) *pRegCat = StructCreateNoConst(parse_PCRegionCategory);
			SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, eaRegions[i], pRegCat->hRegion);
			eaPush(&pPCCostume->eaRegionCategories, pRegCat);
		}

		// Pick a valid category for the entry
		costumeTailor_GetValidCategories(pPCCostume, eaRegions[i], pSpecies, NULL, NULL, pSlotType, &eaCategories, CGVF_OMIT_EMPTY | CGVF_REQUIRE_POWERFX);
		costumeTailor_PickValidCategoryForRegion(pPCCostume, eaRegions[i], eaCategories, false);
		eaDestroy(&eaCategories);
	}
	eaDestroy(&eaRegions);


	// Find conversion for stance
	if (pPCCostume->pcStance) {
		const PCSlotStanceStruct *pStance = costumeTailor_GetStanceFromSlot(pSlotType, costumeTailor_GetGenderFromSpeciesOrCostume(pSpecies, (PlayerCostume *)pPCCostume));
		bFound = false;
		if (pStance)
		{
			for(i=eaSize(&pStance->eaStanceInfo)-1; i>=0; --i) {
				if (stricmp(pStance->eaStanceInfo[i]->pcName, pPCCostume->pcStance) == 0) {
					bFound = true;
					break;
				}
			}
		}
		else if (pSpecies && eaSize(&pSpecies->eaStanceInfo))
		{
			for(i=eaSize(&pSpecies->eaStanceInfo)-1; i>=0; --i) {
				if (stricmp(pSpecies->eaStanceInfo[i]->pcName, pPCCostume->pcStance) == 0) {
					bFound = true;
					break;
				}
			}
		}
		else
		{
			for(i=eaSize(&pSkel->eaStanceInfo)-1; i>=0; --i) {
				if (stricmp(pSkel->eaStanceInfo[i]->pcName, pPCCostume->pcStance) == 0) {
					bFound = true;
					break;
				}
			}
		}
		if (!bFound) {
			pPCCostume->pcStance = NULL;
		}
	}

	// Make sure body scale count is correct
	if (eafSize(&pPCCostume->eafBodyScales) > eaSize(&pSkel->eaBodyScaleInfo)) {
		// Clip off if too big
		eafSetSize(&pPCCostume->eafBodyScales, eaSize(&pSkel->eaBodyScaleInfo));
	} else {
		for(i=eafSize(&pPCCostume->eafBodyScales); i < eaSize(&pSkel->eaBodyScaleInfo); ++i) {
			if (i < eafSize(&pSkel->eafDefaultBodyScales)) {
				eafPush(&pPCCostume->eafBodyScales, pSkel->eafDefaultBodyScales[i]);
			} else {
				eafPush(&pPCCostume->eafBodyScales, 20);
			}
		}
	}

	// Then see if we can find conversions for existing parts
	for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i) {
		pPart = pPCCostume->eaParts[i];
		pBone = GET_REF(pPart->hBoneDef);

		// See if there's a matching bone
		bFound = false;
		if (!pSkel) {
			StructDestroyNoConst(parse_PCPart, pPCCostume->eaParts[i]);
			eaRemove(&pPCCostume->eaParts, i);
			continue;
		}

		// Look for exact match on a bone def
		for(j=eaSize(&pSkel->eaRequiredBoneDefs)-1; j>=0; --j) {
			if (GET_REF(pSkel->eaRequiredBoneDefs[j]->hBone) == pBone) {
				bFound = true;
				break;
			}
		}
		if (!bFound) {
			for(j=eaSize(&pSkel->eaOptionalBoneDefs)-1; j>=0; --j) {
				if (GET_REF(pSkel->eaOptionalBoneDefs[j]->hBone) == pBone) {
					bFound = true;
					break;
				}
			}
		}


		if (!bFound) {
			// Look for display name match on a bone def
			for(j=eaSize(&pSkel->eaRequiredBoneDefs)-1; j>=0; --j) {
				PCBoneDef *pOtherBone = GET_REF(pSkel->eaRequiredBoneDefs[j]->hBone);
				if (pBone && pOtherBone && stricmp(TranslateDisplayMessage(pBone->displayNameMsg), TranslateDisplayMessage(pOtherBone->displayNameMsg)) == 0) {
					pBone = pOtherBone;
					bFound = true;
					break;
				}
			}
			if (!bFound) {
				for(j=eaSize(&pSkel->eaOptionalBoneDefs)-1; j>=0; --j) {
					PCBoneDef *pOtherBone = GET_REF(pSkel->eaOptionalBoneDefs[j]->hBone);
					if (pBone && pOtherBone && stricmp(TranslateDisplayMessage(pBone->displayNameMsg), TranslateDisplayMessage(pOtherBone->displayNameMsg)) == 0) {
						pBone = pOtherBone;
						bFound = true;
						break;
					}
				}
			}

			if (!bFound) {
				// No matching bone, so remove part and continue
				StructDestroyNoConst(parse_PCPart, pPCCostume->eaParts[i]);
				eaRemove(&pPCCostume->eaParts, i);
				continue;
			}
					
			// Found a matching bone on display name, replace bone name on part with close match
			SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, pBone, pPart->hBoneDef);
		}
					
		// Now look for matching geometry
		pGeo = GET_REF(pPart->hGeoDef);
		if (!pGeo) {
			// Geo is bad, so remove part and continue
			StructDestroyNoConst(parse_PCPart, pPCCostume->eaParts[i]);
			eaRemove(&pPCCostume->eaParts, i);
			continue;
		}

		pMatchingGeo = NULL;
		for(j=eaSize(&pGeoStruct->ppReferents)-1; j>=0; --j) {
			PCGeometryDef *pOtherGeo = (PCGeometryDef*)pGeoStruct->ppReferents[j];
			if (GET_REF(pOtherGeo->hBone) == pBone) {
				if (stricmp(pOtherGeo->pcName, pGeo->pcName) == 0) {
					// Perfect match on geo, so we're done
					pMatchingGeo = pGeo;
					break;
				}
				if (stricmp(TranslateDisplayMessage(pOtherGeo->displayNameMsg), TranslateDisplayMessage(pGeo->displayNameMsg)) == 0) {
					pMatchingGeo = pOtherGeo;
				}
			}
		}
		if (pMatchingGeo == pGeo) {
			// Existing geo is good, so we're done
			continue;
		}

		if (!pMatchingGeo) {
			// Can't find a matching geometry, so drop the part
			StructDestroyNoConst(parse_PCPart, pPCCostume->eaParts[i]);
			eaRemove(&pPCCostume->eaParts, i);
			continue;
		}

		// Found matching geo, so replace it
		SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, pMatchingGeo, pPart->hGeoDef);

		// Look for matching material
		pMat = GET_REF(pPart->hMatDef);
		pMatchingMat = NULL;
		if (pMat) {
			for(j=eaSize(&pMatchingGeo->eaAllowedMaterialDefs)-1; j>=0; --j) {
				PCMaterialDef *pOtherMat = RefSystem_ReferentFromString(g_hCostumeMaterialDict, pMatchingGeo->eaAllowedMaterialDefs[j]);
				if (pOtherMat && stricmp(pOtherMat->pcName, pMat->pcName) == 0) {
					// Found matching material, so done
					pMatchingMat = pMat;
					break;
				}
				if (pOtherMat && stricmp(TranslateDisplayMessage(pOtherMat->displayNameMsg), TranslateDisplayMessage(pMat->displayNameMsg)) == 0) {
					pMatchingMat = pOtherMat;
				}
			}
		}
		if (pMatchingMat == pMat) {
			continue;
		}

		if (!pMatchingMat) {
			// No matching material, so go with no material
			SET_HANDLE_FROM_STRING(g_hCostumeMaterialDict, "None", pPart->hMatDef);
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pPart->hPatternTexture);
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pPart->hDetailTexture);
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pPart->hSpecularTexture);
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pPart->hDiffuseTexture);
			if (pPart->pMovableTexture) {
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pPart->pMovableTexture->hMovableTexture);
			}
			continue;
		}

		// Replace material name
		SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, pMatchingMat, pPart->hMatDef);


		// Look for matching PATTERN texture
		pTex = GET_REF(pPart->hPatternTexture);
		pMatchingTex = NULL;
		if (pTex) {
			for(j=eaSize(&pMatchingMat->eaAllowedTextureDefs)-1; j>=0; --j) {
				PCTextureDef *pOtherTex = RefSystem_ReferentFromString(g_hCostumeTextureDict, pMatchingMat->eaAllowedTextureDefs[j]);
				if (pOtherTex && stricmp(pOtherTex->pcName, pTex->pcName) == 0) {
					// Found matching texture, so done
					pMatchingTex = pTex;
					break;
				}
				if (pOtherTex && stricmp(TranslateDisplayMessage(pOtherTex->displayNameMsg), TranslateDisplayMessage(pTex->displayNameMsg)) == 0) {
					pMatchingTex = pOtherTex;
				}
			}
		}
		if (!pMatchingTex) {
			// No matching texture, so go with no texture
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pPart->hPatternTexture);
		} else {
			// Replace texture
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pMatchingTex, pPart->hPatternTexture);
		}

		// Look for matching DETAIL texture
		pTex = GET_REF(pPart->hDetailTexture);
		pMatchingTex = NULL;
		if (pTex) {
			for(j=eaSize(&pMatchingMat->eaAllowedTextureDefs)-1; j>=0; --j) {
				PCTextureDef *pOtherTex = RefSystem_ReferentFromString(g_hCostumeTextureDict, pMatchingMat->eaAllowedTextureDefs[j]);
				if (pOtherTex && stricmp(pOtherTex->pcName, pTex->pcName) == 0) {
					// Found matching texture, so done
					pMatchingTex = pTex;
					break;
				}
				if (pOtherTex && stricmp(TranslateDisplayMessage(pOtherTex->displayNameMsg), TranslateDisplayMessage(pTex->displayNameMsg)) == 0) {
					pMatchingTex = pOtherTex;
				}
			}
		}
		if (!pMatchingTex) {
			// No matching texture, so go with no texture
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pPart->hDetailTexture);
		} else {
			// Replace texture
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pMatchingTex, pPart->hDetailTexture);
		}

		// Look for matching SPECULAR texture
		pTex = GET_REF(pPart->hSpecularTexture);
		pMatchingTex = NULL;
		if (pTex) {
			for(j=eaSize(&pMatchingMat->eaAllowedTextureDefs)-1; j>=0; --j) {
				PCTextureDef *pOtherTex = RefSystem_ReferentFromString(g_hCostumeTextureDict, pMatchingMat->eaAllowedTextureDefs[j]);
				if (pOtherTex && stricmp(pOtherTex->pcName, pTex->pcName) == 0) {
					// Found matching texture, so done
					pMatchingTex = pTex;
					break;
				}
				if (pOtherTex && stricmp(TranslateDisplayMessage(pOtherTex->displayNameMsg), TranslateDisplayMessage(pTex->displayNameMsg)) == 0) {
					pMatchingTex = pOtherTex;
				}
			}
		}
		if (!pMatchingTex) {
			// No matching texture, so go with no texture
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pPart->hSpecularTexture);
		} else {
			// Replace texture
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pMatchingTex, pPart->hSpecularTexture);
		}

		// Look for matching DIFFUSE texture
		pTex = GET_REF(pPart->hDiffuseTexture);
		pMatchingTex = NULL;
		if (pTex) {
			for(j=eaSize(&pMatchingMat->eaAllowedTextureDefs)-1; j>=0; --j) {
				PCTextureDef *pOtherTex = RefSystem_ReferentFromString(g_hCostumeTextureDict, pMatchingMat->eaAllowedTextureDefs[j]);
				if (pOtherTex && stricmp(pOtherTex->pcName, pTex->pcName) == 0) {
					// Found matching texture, so done
					pMatchingTex = pTex;
					break;
				}
				if (pOtherTex && stricmp(TranslateDisplayMessage(pOtherTex->displayNameMsg), TranslateDisplayMessage(pTex->displayNameMsg)) == 0) {
					pMatchingTex = pOtherTex;
				}
			}
		}
		if (!pMatchingTex) {
			// No matching texture, so go with no texture
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pPart->hDiffuseTexture);
		} else {
			// Replace texture
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pMatchingTex, pPart->hDiffuseTexture);
		}

		// Look for matching MOVABLE texture
		if (!pPart->pMovableTexture) {
			pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
		}
		pTex = GET_REF(pPart->pMovableTexture->hMovableTexture);
		pMatchingTex = NULL;
		if (pTex) {
			for(j=eaSize(&pMatchingMat->eaAllowedTextureDefs)-1; j>=0; --j) {
				PCTextureDef *pOtherTex = RefSystem_ReferentFromString(g_hCostumeTextureDict, pMatchingMat->eaAllowedTextureDefs[j]);
				if (pOtherTex && stricmp(pOtherTex->pcName, pTex->pcName) == 0) {
					// Found matching texture, so done
					pMatchingTex = pTex;
					break;
				}
				if (pOtherTex && stricmp(TranslateDisplayMessage(pOtherTex->displayNameMsg), TranslateDisplayMessage(pTex->displayNameMsg)) == 0) {
					pMatchingTex = pOtherTex;
				}
			}
		}
		if (!pMatchingTex) {
			// No matching texture, so go with no texture]
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pPart->pMovableTexture->hMovableTexture);
		} else {
			// Replace texture
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pMatchingTex, pPart->pMovableTexture->hMovableTexture);
		}
	}
}


// --------------------------------------------------------------------------
// Costume manipulation
// --------------------------------------------------------------------------


F32 costumeTailor_GetMatConstantScale(const char *pcName)
{
	if (!pcName) {
		return 100.0;	// 0 - 1
	} else if (strstri(pcName, "tile") || strstri(pcName, "tiling")) {
		return 2.0;		// 0 - 50
	} else if ( strstri(pcName, "shininess") ) {
		return 1.0;		// 0 - 100 but modified to non-linear value during set
	} else if ( strstri(pcName, "oscillate") ) {
		return 5.0;		// 0 - 20
	} else if ( strstri(pcName, "RovingRotateScale") ) {
		return 5.0;		// 0 - 20  (note that third value is later ranged down to 0-1, 4th value hardcoded)
	} else if ( strstri(pcName, "RovingTranslate") ) {
		return 90.0;		// 0 - 1.1  (note that 3rd and 4th values are hardcoded
	} else if (strstri(pcName, "decal")) {
		if (strstri(pcName, "translate")) {
			return (100.0 / 12.0);	// 0 - 12
		} else if (strstri(pcName, "scale")) {
			return (100.0 / 6.0);	// 0 - 6
		} else if (strstri(pcName, "rotate")) {
			return 100.0;			// 0 - 1
		} else {
			return 100.0;			// 0 - 1
		}
	} else if (strstri(pcName, "strength")) {
		return 50.0;	// 0 - 2 here but modified to -1 - 1 during set
	} else {
		return 100.0;	// 0 - 1
	}
}

S32 costumeTailor_GetMatConstantNumValues(const char* pcName)
{
	if (!pcName) {
		return 1;
	}
	else if (strstri(pcName, "weight") || 
			 strstri(pcName, "texrotate") || 
		     strstri(pcName, "tattoo_offset")) {
		return 4;
	}
	else if (strstri(pcName, "oscillate") ||
			 strstri(pcName, "RovingRotateScale")) {
		return 3;
	}
	else if (strstri(pcName, "RovingTranslate")) {
		return 2;
	}
	else if (strstri(pcName, "decal")) {
		if (strstri(pcName, "translate") || strstri(pcName, "scale")) {
			return 2;
		}
		else if (strstri(pcName, "rotate")) {
			return 1;
		}
		else {
			return 1;
		}
	}
	else {
		return 1;
	}
}

const char* costumeTailor_GetMatConstantValueName(const char* pcName, int valIdx)
{
	char buf[260];
	
	if (!pcName || valIdx < 0 || valIdx >= costumeTailor_GetMatConstantNumValues(pcName)) {
		sprintf(buf, "");
	}
	else if (strstri(pcName, "oscillate")) {
		// oscillate constants need specific A/F/P labels
		switch (valIdx) {
			case 0:  sprintf(buf, "%s", "Amplitude");
			xcase 1: sprintf(buf, "%s", "Frequency");
			xcase 2: sprintf(buf, "%s", "Phase");
			xdefault: assertmsg(0, "Invalid mat constant value index");
		}
	}
	else if (strstri(pcName, "RovingRotateScale")) {
		switch (valIdx) {
			case 0:  sprintf(buf, "%s", "Scale X");
			xcase 1: sprintf(buf, "%s", "Scale Y");
			xcase 2: sprintf(buf, "%s", "Rotation");
			xdefault: assertmsg(0, "Invalid mat constant value index");
		}
	}
	else if (strstri(pcName, "RovingTranslate")) {
		switch (valIdx) {
			case 0:  sprintf(buf, "%s", "Translate X");
			xcase 1: sprintf(buf, "%s", "Translate Y");
			xdefault: assertmsg(0, "Invalid mat constant value index");
		}
	}
	else if (strstri(pcName, "decal")) {
		if (strstri(pcName, "translate")) {
			switch (valIdx) {
				case 0:  sprintf(buf, "%s", "Translate X");
				xcase 1: sprintf(buf, "%s", "Translate Y");
				xdefault: assertmsg(0, "Invalid mat constant value index");
			}
		}
		else if (strstri(pcName, "scale")) {
			switch (valIdx) {
				case 0:  sprintf(buf, "%s", "Scale X");
				xcase 1: sprintf(buf, "%s", "Scale Y");
				xdefault: assertmsg(0, "Invalid mat constant value index");
			}
		}
		else if (strstri(pcName, "rotate")) {
			switch (valIdx) {
				case 0:  sprintf(buf, "%s", "Rotation");
				xdefault: assertmsg(0, "Invalid mat constant value index");
			}
		}
		else {
			sprintf(buf, "%d", valIdx);
		}
	}
	else {
		// most values will just want their index value as a label
		sprintf(buf, "%d", valIdx);
	}
	
	return allocAddString(buf);
}



#if defined(GAMESERVER)

//Since the server doesn't have access to gfxMaterialsInitMaterial this function substitutes and only sets what is needed which must then be cleared
static bool costumeTailor_MaterialsInitMaterial(Material *pMaterial)
{
	MaterialRenderInfo *render_info;
	const MaterialData *material_data;
	const ShaderTemplate *shader_template;
	int num_ops;
	int i, j;
	unsigned int pos = 0;

	if (!pMaterial->graphic_props.render_info)
	{
		pMaterial->graphic_props.render_info = calloc(sizeof(*pMaterial->graphic_props.render_info), 1);
	}
	else
	{
		return false;
	}

	material_data = materialGetData(pMaterial);
	shader_template = materialGetTemplateByName(material_data->graphic_props.default_fallback.shader_template_name);
	if (!shader_template) return false;
	if (!shader_template->graph) return false;
	render_info = pMaterial->graphic_props.render_info;
	num_ops = eaSize(&shader_template->graph->operations);
	render_info->rdr_material.const_count = num_ops;

	render_info->constant_names = calloc(render_info->rdr_material.const_count * 4 * sizeof(render_info->constant_names), 1);
	if (!render_info->constant_names) return false;
	for (i=num_ops-1; i>=0; i--) {
		ShaderOperation *op = shader_template->graph->operations[i];
		const ShaderOperationDef *op_def = GET_REF(op->h_op_definition);
		if (!op_def) // BAD!
			continue;
		for (j=eaSize(&op_def->op_inputs)-1; j>=0; j--) {
			const ShaderInput *op_input = op_def->op_inputs[j];
			if (op_input->num_floats) {
				assert(pos < render_info->rdr_material.const_count*4);
				render_info->constant_names[pos++] = allocAddString(op->op_name);
			}
		}
	}

	return true;
}

static void costumeTailor_MaterialsDeinitMaterial(Material *pMaterial)
{
	MaterialRenderInfo *render_info = pMaterial->graphic_props.render_info;
	free((void*)render_info->constant_names);
	free(render_info);
	pMaterial->graphic_props.render_info = NULL;
}
#endif

bool costumeTailor_IsSliderConstValid(const char *pcName, PCMaterialDef *pMat, int index)
{
#if defined(GAMESERVER)
	bool mustDeinit = false;
#endif
	int i;
	Material *pMaterial;

	if (!pcName) return false;
	if (index < 0) return false;
	pMaterial = materialFindNoDefault(pMat->pcMaterial, 0);
	if (pMaterial)
	{
		MaterialRenderInfo *render_info;

		if (!pMaterial->graphic_props.render_info)
		{
#if defined(GAMESERVER)
			mustDeinit = costumeTailor_MaterialsInitMaterial(pMaterial);
			if (!mustDeinit) return false;
#else
#ifdef GAMECLIENT
			gfxMaterialsInitMaterial(pMaterial, true);
#else
			return false;
#endif
#endif
		}
		render_info = pMaterial->graphic_props.render_info;
		assert(render_info);
		for (i = (render_info->rdr_material.const_count * 4) - 1; i >= 0; --i)
		{
			if (render_info->constant_names[i])
			{
				if ((stricmp(render_info->constant_names[i], "Color0") == 0) ||
					(stricmp(render_info->constant_names[i], "Color1") == 0) ||
					(stricmp(render_info->constant_names[i], "Color2") == 0) ||
					(stricmp(render_info->constant_names[i], "Color3") == 0) ||
					(stricmp(render_info->constant_names[i], "MuscleWeight") == 0) ||
					(stricmp(render_info->constant_names[i], "ReflectionWeight") == 0) ||
					(stricmp(render_info->constant_names[i], "SpecularWeight") == 0) ||
					// Artists request that these be hidden
					(stricmp(render_info->constant_names[i], "FresnelTerm_Advanced1") == 0) ||
					(strnicmp(render_info->constant_names[i], "LERP", 4) == 0) ||
					(stricmp(render_info->constant_names[i], "MyOutput") == 0)
					)
				{
					// No constants that have built-in UI
					continue;
				}

				if (strstri(render_info->constant_names[i], "Color") != NULL)
				{
					// No colors
					continue;
				}

				if (!strcmp(pcName,render_info->constant_names[i]))
				{
					if (index >= 0 && index < costumeTailor_GetMatConstantNumValues(pcName))
					{
#if defined(GAMESERVER)
						if (mustDeinit) costumeTailor_MaterialsDeinitMaterial(pMaterial);
#endif
						return true;
					}
				}
			}
		}
	}

#if defined(GAMESERVER)
	if (mustDeinit) costumeTailor_MaterialsDeinitMaterial(pMaterial);
#endif
	return false;
}


bool costumeValidate_PlayerCreatedError(PlayerCostume *pPCCostume, char **ppEStringError, char *pError, PCPart *pPart)
{
	if(ppEStringError)
	{
		if(estrLength(ppEStringError)<1)
		{
			estrPrintf(ppEStringError, "Costume validation for player error:");
			if(pPCCostume->pcName)
			{
				estrConcatf(ppEStringError, " Name:%s", pPCCostume->pcName);
			}
			if(pPCCostume->pcFileName)
			{
				estrConcatf(ppEStringError, ", Filename:%s", pPCCostume->pcFileName);
			}
		}

		if(pPart)
		{
			PCBoneDef *pBone;
			PCGeometryDef *pGeo;
			pBone = GET_REF(pPart->hBoneDef);
			if(pBone)
			{
				estrConcatf(ppEStringError, ", Bone %s", pBone->pcName);
			}
			pGeo = GET_REF(pPart->hGeoDef);
			if(pGeo)
			{
				estrConcatf(ppEStringError, ", Geo %s", pGeo->pcName);
			}
		}

		estrConcatf(ppEStringError, ", %s.\n", pError);
		return true;
	}

	return false;
}

// **********************************************************************************
// *
// * costumeValidate_ValidatePlayerCreatedPart - check to make the costume created by a player is legal
// *
// * KDB 05-06-2009
// *
// **********************************************************************************
bool costumeValidate_ValidatePlayerCreatedPart(PCPart *pPart, PCSkeletonDef *pSkeleton, SpeciesDef *pSpecies, const PCSlotType *pSlotType, Guild *pGuild, bool recursing, PlayerCostume *pPCCostume, char **ppEStringError)
{
	UIColorSet *pColorSetBody[4] = {NULL,NULL,NULL,NULL};
	PCBoneDef *pBone = GET_REF(pPart->hBoneDef);
	PCGeometryDef *pGeo = GET_REF(pPart->hGeoDef);
	PCMaterialDef *pMaterial;
	PCGeometryDef *pGeometry;
	bool bCustomSpec, bCustomRef;
	int i;
	bool bRetVal = true;
	char cError[256];
	char buf[1024];

	if(pPart->pArtistData && pPart->pArtistData->bNoShadow)
	{
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "bNoShadow is true", pPart))
		{
			return false;
		}
		bRetVal = false;
	}

	if(pPart->pArtistData && pPart->pArtistData->eaExtraColors)
	{
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "eaExtraColors is set", pPart))
		{
			return false;
		}
		bRetVal = false;
	}


	if(pPart->pArtistData && pPart->pArtistData->eaExtraConstants)
	{
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "eaExtraConstants is set", pPart))
		{
			return false;
		}
		bRetVal = false;
	}

	if(pPart->pArtistData && pPart->pArtistData->eaExtraTextures)
	{
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "eaExtraTextures is set", pPart))
		{
			return false;
		}
		bRetVal = false;
	}

	if ((pPart->pTextureValues &&
		(pPart->pTextureValues->fDetailValue < -100.0f || pPart->pTextureValues->fDetailValue > 100.0f ||
		pPart->pTextureValues->fSpecularValue < -100.0f || pPart->pTextureValues->fSpecularValue > 100.0f ||
		pPart->pTextureValues->fDiffuseValue < -100.0f || pPart->pTextureValues->fDiffuseValue > 100.0f)) ||
		(pPart->pMovableTexture &&
		(pPart->pMovableTexture->fMovableValue < -100.0f || pPart->pMovableTexture->fMovableValue > 100.0f))
		)
	{
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "Texture defined Material constant value is out of range", pPart))
		{
			return false;
		}
		bRetVal = false;
	}

	if (pGuild && pBone && pBone->bIsGuildEmblemBone && costumeTailor_PartHasGuildEmblem(CONTAINER_NOCONST(PCPart, pPart), pGuild))
	{
#define TESTDELTA 0.00001f
		float rot1 = pGuild->fEmblemRotation;
		float rot2 = pGuild->fEmblem2Rotation;
		float x2 = pGuild->fEmblem2X;
		float y2 = pGuild->fEmblem2Y;
		float sx2 = pGuild->fEmblem2ScaleX;
		float sy2 = pGuild->fEmblem2ScaleY;
		if (rot1 < -100) rot1 = -100;
		if (rot1 > 100) rot1 = 100;
		if (rot2 < 0) rot2 = 0;
		if (rot2 > 100) rot2 = 100;
		if (x2 < -100.0f) x2 = -100.0f;
		if (x2 > 100.0f) x2 = 100.0f;
		if (y2 < -100.0f) y2 = -100.0f;
		if (y2 > 100.0f) y2 = 100.0f;
		if (sx2 < 0.0f) sx2 = 0.0f;
		if (sx2 > 100.0f) sx2 = 100.0f;
		if (sy2 < 0.0f) sy2 = 0.0f;
		if (sy2 > 100.0f) sy2 = 100.0f;
		if ((pPart->pTextureValues && (pPart->pTextureValues->fPatternValue > rot1 + TESTDELTA || pPart->pTextureValues->fPatternValue < rot1 - TESTDELTA)) ||
			((!pPart->pTextureValues) && rot1 != 0.0f))
		{
			if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "Texture defined Material constant value does not match guild for emblem bone", pPart))
			{
				return false;
			}
			bRetVal = false;
		}
		if ((pPart->pMovableTexture &&
			(pPart->pMovableTexture->fMovableX > x2 + TESTDELTA || pPart->pMovableTexture->fMovableX < x2 - TESTDELTA ||
			pPart->pMovableTexture->fMovableY > y2 + TESTDELTA || pPart->pMovableTexture->fMovableY < y2 - TESTDELTA ||
			pPart->pMovableTexture->fMovableScaleX > sx2 + TESTDELTA || pPart->pMovableTexture->fMovableScaleX < sx2 - TESTDELTA ||
			pPart->pMovableTexture->fMovableScaleY > sy2 + TESTDELTA || pPart->pMovableTexture->fMovableScaleY < sy2 - TESTDELTA ||
			pPart->pMovableTexture->fMovableRotation > rot2 + TESTDELTA || pPart->pMovableTexture->fMovableRotation < rot2 - TESTDELTA)) ||
			((!pPart->pMovableTexture) &&
			(x2 != 0.0f ||
			y2 != 0.0f ||
			sx2 != 1.0f ||
			sy2 != 1.0f ||
			rot2 != 0.0f)))
		{
			if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "Movable texture value does not match guild for emblem bone", pPart))
			{
				return false;
			}
			bRetVal = false;
		}
	}
	else
	{
		if (pPart->pTextureValues &&
			(pPart->pTextureValues->fPatternValue < -100.0f || pPart->pTextureValues->fPatternValue > 100.0f))
		{
			if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "Texture defined Material constant value is out of range", pPart))
			{
				return false;
			}
			bRetVal = false;
		}
		if ((pPart->pMovableTexture &&
			(pPart->pMovableTexture->fMovableX < -100.0f || pPart->pMovableTexture->fMovableX > 100.0f ||
			pPart->pMovableTexture->fMovableY < -100.0f || pPart->pMovableTexture->fMovableY > 100.0f ||
			pPart->pMovableTexture->fMovableScaleX < 0.0f || pPart->pMovableTexture->fMovableScaleX > 100.0f ||
			pPart->pMovableTexture->fMovableScaleY < 0.0f || pPart->pMovableTexture->fMovableScaleY > 100.0f ||
			pPart->pMovableTexture->fMovableRotation < 0.0f || pPart->pMovableTexture->fMovableRotation > 100.0f)))
		{
			if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "Movable texture value is out of range", pPart))
			{
				return false;
			}
			bRetVal = false;
		}
	}

	if (!gConf.bAllowPlayerCostumeFX)
	{
		if (pGuild && pBone && pBone->bIsGuildEmblemBone && costumeTailor_PartHasGuildEmblem(CONTAINER_NOCONST(PCPart, pPart), pGuild))
		{
			if (*((U32*)pPart->color0) != pGuild->iEmblem2Color0)
			{
				sprintf(buf, "color0 is not correct color for guild emblem");
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, buf, pPart))
				{
					return false;
				}
				bRetVal = false;
			}
			if (*((U32*)pPart->color1) != pGuild->iEmblem2Color1)
			{
				sprintf(buf, "color1 is not correct color for guild emblem");
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, buf, pPart))
				{
					return false;
				}
				bRetVal = false;
			}
			if (*((U32*)pPart->color2) != pGuild->iEmblemColor0)
			{
				sprintf(buf, "color2 is not correct color for guild emblem");
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, buf, pPart))
				{
					return false;
				}
				bRetVal = false;
			}
			if (*((U32*)pPart->color3) != pGuild->iEmblemColor1)
			{
				sprintf(buf, "color3 is not correct color for guild emblem");
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, buf, pPart))
				{
					return false;
				}
				bRetVal = false;
			}
		}
		else
		{
			U8 color0[4];
			U8 color1[4];
			U8 color2[4];
			U8 color3[4];

			pColorSetBody[0] = costumeTailor_GetColorSetForPart(CONTAINER_NOCONST(PlayerCostume, pPCCostume), pSpecies, pSlotType, CONTAINER_NOCONST(PCPart, pPart), 0);
			pColorSetBody[1] = costumeTailor_GetColorSetForPart(CONTAINER_NOCONST(PlayerCostume, pPCCostume), pSpecies, pSlotType, CONTAINER_NOCONST(PCPart, pPart), 1);
			pColorSetBody[2] = costumeTailor_GetColorSetForPart(CONTAINER_NOCONST(PlayerCostume, pPCCostume), pSpecies, pSlotType, CONTAINER_NOCONST(PCPart, pPart), 2);
			pColorSetBody[3] = costumeTailor_GetColorSetForPart(CONTAINER_NOCONST(PlayerCostume, pPCCostume), pSpecies, pSlotType, CONTAINER_NOCONST(PCPart, pPart), 3);

			COPY_COSTUME_COLOR(pPart->color0, color0);
			COPY_COSTUME_COLOR(pPart->color1, color1);
			COPY_COSTUME_COLOR(pPart->color2, color2);
			COPY_COSTUME_COLOR(pPart->color3, color3);

			if(pColorSetBody[0] && !costumeLoad_ColorInSet(color0, pColorSetBody[0]))
			{
				sprintf(buf, "color0 is not in color set '%s' for this geo", pColorSetBody[0]->pcName);
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, buf, pPart))
				{
					return false;
				}
				bRetVal = false;
			}
			if(pColorSetBody[1] && !costumeLoad_ColorInSet(color1, pColorSetBody[1]))
			{
				sprintf(buf, "color1 is not in color set '%s' for this geo", pColorSetBody[1]->pcName);
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, buf, pPart))
				{
					return false;
				}
				bRetVal = false;
			}
			if(pColorSetBody[2] && !costumeLoad_ColorInSet(color2, pColorSetBody[2]))
			{
				sprintf(buf, "color2 is not in color set '%s' for this geo", pColorSetBody[2]->pcName);
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, buf, pPart))
				{
					return false;
				}
				bRetVal = false;
			}
			if(pColorSetBody[3] && !costumeLoad_ColorInSet(color3, pColorSetBody[3]))
			{
				sprintf(buf, "color3 is not in color set '%s' for this geo", pColorSetBody[3]->pcName);
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, buf, pPart))
				{
					return false;
				}
				bRetVal = false;
			}
		}
	}

	pMaterial = GET_REF(pPart->hMatDef);

	bCustomRef = false;
	bCustomSpec = false;
	if (pPart->pCustomColors) {
		for(i = 0; i < COSTUME_GLOW_ETC_COUNT; ++i)
		{
			if(pPart->pCustomColors->glowScale[i] != 0 && (!pMaterial || !pMaterial->pColorOptions || !pMaterial->pColorOptions->bAllowGlow[i]))
			{
				sprintf(cError,"Glow[%d] is non-zero",i);
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, pPart))
				{
					return false;
				}
				bRetVal = false;
			}
			if(pPart->pCustomColors->reflection[i] != 0)
			{
				if(!pMaterial || !pMaterial->pColorOptions || !pMaterial->pColorOptions->bAllowReflection[i])
				{
					sprintf(cError,"reflection[%d] is non-zero",i);
					if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, pPart))
					{
						return false;
					}
					bRetVal = false;
				}
				else
				{
					bCustomRef = true;
				}
			}
			if(pPart->pCustomColors->specularity[i] != 0)
			{
				if(!pMaterial || !pMaterial->pColorOptions || !pMaterial->pColorOptions->bAllowSpecularity[i])
				{
					sprintf(cError,"specularity[%d] is non-zero",i);
					if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, pPart))
					{
						return false;
					}
					bRetVal = false;
				}
				else
				{
					bCustomSpec = true;
				}
			}
		}

		if(bCustomSpec != pPart->pCustomColors->bCustomSpecularity)
		{
			if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "bCustomSpecularity is incorrect", pPart))
			{
				return false;
			}
			bRetVal = false;
		}

		if(bCustomRef != pPart->pCustomColors->bCustomReflection)
		{
			if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "bCustomReflection is incorrect", pPart))
			{
				return false;
			}
			bRetVal = false;
		}
	}

	pGeometry = GET_REF(pPart->hGeoDef);

	if(pPart->pClothLayer)
	{
		bool bClothOk = true;

		if(!pGeometry || !pGeometry->pClothData || !pGeometry->pClothData->bHasClothBack || !pGeometry->pClothData->bIsCloth)
		{
			if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "Cloth layer present but should not be", pPart))
			{
				return false;
			}
			bRetVal = false;
			bClothOk = false;
		}

		if(recursing)
		{
			// bad data as we are already checking cloth layer
			if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "Recursion in cloth layer check too deep", pPart))
			{
				return false;
			}
			bRetVal = false;
			bClothOk = false;
		}

		if(bClothOk)
		{
			bClothOk = costumeValidate_ValidatePlayerCreatedPart(pPart->pClothLayer, pSkeleton, pSpecies, pSlotType, pGuild, true, pPCCostume, ppEStringError);
			if(!bClothOk)
			{
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "Error in cloth layer", pPart))
				{
					return false;
				}
				bRetVal = false;
			}
		}
	}
	else
	{
		if(pGeometry && pGeometry->pClothData && pGeometry->pClothData->bHasClothBack && pGeometry->pClothData->bIsCloth)
		{
			if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "No cloth layer when there should be", pPart))
			{
				return false;
			}
			bRetVal = false;
		}
	}

	return bRetVal;
}


bool costumeValidate_ValidateBoneScale(PlayerCostume *pPCCostume, PCSkeletonDef *pSkel, SpeciesDef *pSpecies, PCSlotType *pSlotType, PCScaleInfo *pScale, char **ppEStringError)
{
	PCScaleValue *pValue = NULL;
	F32 fMin = 0, fMax = 0;
	int i;
	bool bRetVal = true;
	bool bOverride = true;
	char buf[1024];

	for(i=eaSize(&pPCCostume->eaScaleValues)-1; i>=0; --i) {
		if (stricmp(pPCCostume->eaScaleValues[i]->pcScaleName, pScale->pcName) == 0) {
			pValue = pPCCostume->eaScaleValues[i];
			break;
		}
	}

	if (!costumeTailor_GetOverrideBoneScale(pSkel, pScale, pScale->pcName, pSpecies, pSlotType, &fMin, &fMax)) {
		fMin = pScale->fPlayerMin;
		fMax = pScale->fPlayerMax;
		bOverride = false;
	}

	if ((pValue && (pValue->fValue < fMin || pValue->fValue > fMax)) ||
		(!pValue && (fMin > 0 || fMax < 0))) {
		if (bOverride) {
			sprintf(buf, "Bone scale %s value %g is out of range %g -> %g for the species or slot override", pScale->pcName, pValue ? pValue->fValue : 0.0, fMin, fMax);
			if (!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, buf, NULL)) {
				return false;
			}
		} else {
			sprintf(buf, "Bone scale %s value %g is out of range %g -> %g for the skeleton", pScale->pcName, pValue ? pValue->fValue : 0.0, fMin, fMax);
			if (!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, buf, NULL)) {
				return false;
			}
		}
		bRetVal = false;
	}

	return bRetVal;
}


// **********************************************************************************
// *
// * costumeValidatePlayerCreated - check to make sure the costume created by a player is legal
// *
// * in: PlayerCostume, optional error string
// * out: optional error string
// *
// * KDB 05-06-2009
// *
// **********************************************************************************
bool costumeValidate_ValidatePlayerCreated(PlayerCostume *pPCCostume, SpeciesDef *pSpecies, PCSlotType *pSlotType, Entity *pPlayerEnt, Entity *pEnt, char **ppEStringError, ValidatePlayerCostumeItems *pTestItems, PlayerCostume **eaOverrideUnlockedCostumes, bool bInEditor)
{
	PCSkeletonDef *pSkeleton;
	bool bFound;
	int i,j,sz,k;
	PCScaleInfo *pScale = NULL;
	bool bRetVal = true;
	char cError[256];
	UIColorSet *pColorSetSkin = NULL;
	BoneScaleLimit *pSpeciesBoneScaleLimit = NULL;
	PlayerCostume **eaUnlockedCostumes = NULL;
	PCVoice *voice;
	Guild *pGuild = guild_GetGuild(pPlayerEnt);
	F32 fMin = 0, fMax = 0;
	bool bFoundOverride;
	
	// in artist override section, assume no use for player
	if(pPCCostume->pArtistData && pPCCostume->pArtistData->bNoBodySock) {
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "bNoBodySock is true", NULL)) {
			return false;
		}
		bRetVal = false;
	}

	// in artist override section, assume no use for player
	if(pPCCostume->pArtistData && pPCCostume->pArtistData->bNoCollision) {
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "bNoCollision is true", NULL)) {
			return false;
		}
		bRetVal = false;
	}

	// in artist override section, assume no use for player
	if(pPCCostume->pArtistData && pPCCostume->pArtistData->eaConstantBits) {
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "eaConstantBits is set", NULL)) {
			return false;
		}
		bRetVal = false;
	}

	// in artist override section, assume no use for player
	if(pPCCostume->pArtistData && pPCCostume->pArtistData->eaFX && !gConf.bAllowPlayerCostumeFX) {
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "eaFX is set", NULL)) {
			return false;
		}
		bRetVal = false;
	}
	// in artist override section, assume no use for player
	if(pPCCostume->pArtistData && pPCCostume->pArtistData->eaFXSwap) {
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "eaFXSwap is set", NULL)) {
			return false;
		}
		bRetVal = false;
	}

	// in artist override section, assume no use for player
	if(pPCCostume->pArtistData && pPCCostume->pArtistData->pcCollisionGeo) {
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "pcCollisionGeo is set", NULL)) {
			return false;
		}
		bRetVal = false;
	}

	if(pPCCostume->eCostumeType != kPCCostumeType_Player) {
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "eCostumeType is not player", NULL)) {
			return false;
		}
		bRetVal = false;
	}

	if(pPCCostume->pBodySockInfo) {
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "pBodySockInfo is set", NULL)) {
			return false;
		}
		bRetVal = false;
	}

	// players cant change collision radius
	if(pPCCostume->pArtistData && pPCCostume->pArtistData->fCollRadius != 0.0) {
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "fCollRadius is not zero", NULL)) {
			return false;
		}
		bRetVal = false;
	}

	// what level transparency for players? Not changable by players
	if(pPCCostume->pArtistData && pPCCostume->pArtistData->fTransparency != 0.0) {
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "fTransparency is not zero", NULL)) {
			return false;
		}
		bRetVal = false;
	}

	if (pPCCostume->bPlayerCantChange) {
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "Costume marked as can not change", NULL)) {
			return false;
		}
		bRetVal = false;
	}

	// skeleton check
	pSkeleton = GET_REF(pPCCostume->hSkeleton);
	if(!pSkeleton) {
		(void)costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "No skeleton, can't continue validation!", NULL);
		return false;
	}

	if (pTestItems) {
		for (i = eaSize(&pTestItems->eaBonesUsed)-1; i >= 0; --i) {
			if (!GET_REF(pTestItems->eaBonesUsed[i]->hBone)) {
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "CostumeTestParts refers to Bone that doesn't exist", NULL)) {
					return false;
				}
				bRetVal = false;
			}
		}
	}

	if(pPCCostume->eafBodyScales) {
		bool bCanCheckScales = true;
		sz = eafSize(&pPCCostume->eafBodyScales);
		if( sz > eaSize(&pSkeleton->eaBodyScaleInfo) ||
			sz > eafSize(&pSkeleton->eafPlayerMinBodyScales) ||
			sz > eafSize(&pSkeleton->eafPlayerMaxBodyScales)
			) {
				if (!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "Too many elements in eaBodyScaleInfo or eafPlayerMinBodyScales or eafPlayerMaxBodyScales", NULL)) {
				return false;
			}
			bRetVal = false;
			bCanCheckScales = false;
		}

		if (bCanCheckScales) {
			// check scales
			for(i = 0; i < sz; ++i) {
				if (pTestItems && pTestItems->bTestBodyScales) {
					for (j = eaSize(&pTestItems->eaBodyScalesUsed)-1; j >= 0; --j) {
						if(stricmp(pTestItems->eaBodyScalesUsed[j]->pcName, pSkeleton->eaBodyScaleInfo[i]->pcName) == 0) {
							break;
						}
					}
					if (j < 0) {
						continue;
					}
				}
				bFoundOverride = false;
				if (!pSkeleton->eaBodyScaleInfo[i]) {
					if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "Skeleton references bodyscaleinfo that doesn't exist!", NULL)) {
						return false;
					}
					estrConcatf(ppEStringError, ", Skeleton:%s", pSkeleton->pcName);
					estrConcatf(ppEStringError, ", Bodyscaleinfo number:%i", i);
					bRetVal = false;
				} else {
					if (costumeTailor_GetOverrideBodyScale(pSkeleton, pSkeleton->eaBodyScaleInfo[i]->pcName, pSpecies, pSlotType, &fMin, &fMax)) {
						bFoundOverride = true;
					}
				}

				if (bFoundOverride && (pPCCostume->eafBodyScales[i] < fMin || pPCCostume->eafBodyScales[i] > fMax)) {
					if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "eafBodyScales is out of range for the species", NULL)) {
						return false;
					}
					bRetVal = false;
				} else if (pPCCostume->eafBodyScales[i] < pSkeleton->eafPlayerMinBodyScales[i] || pPCCostume->eafBodyScales[i] > pSkeleton->eafPlayerMaxBodyScales[i]) {
					if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "eafBodyScales is out of range for the skeleton", NULL)) {
						return false;
					}
					bRetVal = false;
				}
			}
		}
	}

	// Detect any scales that should not be present
	if (pPCCostume->eaScaleValues) {
		for(i = 0; i < eaSize(&pPCCostume->eaScaleValues); ++i) {
			if (pTestItems && pTestItems->bTestBoneScales) {
				for (j = eaSize(&pTestItems->eaBoneScalesUsed)-1; j >= 0; --j) {
					if(stricmp(pTestItems->eaBoneScalesUsed[j]->pcName, pPCCostume->eaScaleValues[i]->pcScaleName) == 0) {
						break;
					}
				}
				if (j < 0) {
					continue;
				}
			}
			bFound = false;

			// match for in skeleton?
			for (j=0; (j < eaSize(&pSkeleton->eaScaleInfoGroups) && !bFound); ++j) {
				for (k=0; k < eaSize(&pSkeleton->eaScaleInfoGroups[j]->eaScaleInfo); ++k) {
					if(stricmp(pSkeleton->eaScaleInfoGroups[j]->eaScaleInfo[k]->pcName, pPCCostume->eaScaleValues[i]->pcScaleName) == 0) {
						pScale = pSkeleton->eaScaleInfoGroups[j]->eaScaleInfo[k];
						bFound = true;
						break;
					}
				}
			}
			if (!bFound) {
				for (k=0; k < eaSize(&pSkeleton->eaScaleInfo); ++k) {
					if (stricmp(pSkeleton->eaScaleInfo[k]->pcName, pPCCostume->eaScaleValues[i]->pcScaleName) == 0) {
						pScale = pSkeleton->eaScaleInfo[k];
						bFound = true;
						break;
					}
				}
			}

			if (!bFound) {
				if (!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "eaScaleValues has an entry it can't match in the skeleton", NULL)) {
					return false;
				}
				bRetVal = false;
			}
		}
	}

	// Look for scales that should be present and make sure they are correct
	for (i=0; (i < eaSize(&pSkeleton->eaScaleInfoGroups)); ++i) {
		for (j=0; j < eaSize(&pSkeleton->eaScaleInfoGroups[i]->eaScaleInfo); ++j) {
			pScale = pSkeleton->eaScaleInfoGroups[i]->eaScaleInfo[j];
			if (!costumeValidate_ValidateBoneScale(pPCCostume, pSkeleton, pSpecies, pSlotType, pScale, ppEStringError)) {
				bRetVal = false;
			}
		}
	}
	for (i=0; i < eaSize(&pSkeleton->eaScaleInfo); ++i) {
		pScale = pSkeleton->eaScaleInfo[i];
		if (!costumeValidate_ValidateBoneScale(pPCCostume, pSkeleton, pSpecies, pSlotType, pScale, ppEStringError)) {
			bRetVal = false;
		}
	}

	if ((!pTestItems) || pTestItems->bTestMuscle) {
		F32 fHMin, fHMax;
		fHMin = costumeTailor_GetOverrideMuscleMin(pSpecies, pSlotType);
		fHMax = costumeTailor_GetOverrideMuscleMax(pSpecies, pSlotType);
		if (fHMax) {
			if (pPCCostume->fMuscle < fHMin || pPCCostume->fMuscle > fHMax) {
				if (!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "fMuscle is out of range for the species or slot", NULL)) {
					return false;
				}
				bRetVal = false;
			}
		} else if (pPCCostume->fMuscle < pSkeleton->fPlayerMinMuscle || pPCCostume->fMuscle > pSkeleton->fPlayerMaxMuscle) {
			if (!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "fMuscle is out of range for the skeleton", NULL)) {
				return false;
			}
			bRetVal = false;
		}
	}

	if ((!pTestItems) || pTestItems->bTestStance) {
		if (pPCCostume->pcStance) {
			const PCSlotStanceStruct *pStance = costumeTailor_GetStanceFromSlot(pSlotType, costumeTailor_GetGenderFromSpeciesOrCostume(pSpecies, pPCCostume));
			bFound = false;
			if (pStance) {
				for (i=eaSize(&pStance->eaStanceInfo)-1; i>=0; --i) {
					if (stricmp(pStance->eaStanceInfo[i]->pcName,pPCCostume->pcStance) == 0 && costumeTailor_TestRestriction(pStance->eaStanceInfo[i]->eRestriction, true, pPCCostume)) {
						bFound = true;
						break;
					}
				}
				if (!bFound) {
					sprintf(cError,"Costume stance %s can't be found on slot type or is not allowed for player", pPCCostume->pcStance);
					if (!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, NULL)) {
						return false;
					}
					bRetVal = false;
				}
			} else if (pSpecies && eaSize(&pSpecies->eaStanceInfo))	{
				for(i=eaSize(&pSpecies->eaStanceInfo)-1; i>=0; --i) {
					if (stricmp(pSpecies->eaStanceInfo[i]->pcName,pPCCostume->pcStance) == 0 && costumeTailor_TestRestriction(pSpecies->eaStanceInfo[i]->eRestriction, true, pPCCostume)) {
						bFound = true;
						break;
					}
				}
				if (!bFound) {
					sprintf(cError,"Costume stance %s can't be found on species or is not allowed for player", pPCCostume->pcStance);
					if (!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, NULL)) {
						return false;
					}
					bRetVal = false;
				}
			} else {
				for(i=eaSize(&pSkeleton->eaStanceInfo)-1; i>=0; --i) {
					if (stricmp(pSkeleton->eaStanceInfo[i]->pcName,pPCCostume->pcStance) == 0 && costumeTailor_TestRestriction(pSkeleton->eaStanceInfo[i]->eRestriction, true, pPCCostume)) {
						bFound = true;
						break;
					}
				}
				if (!bFound) {
					sprintf(cError,"Costume stance %s can't be found on skeleton or is not allowed for player", pPCCostume->pcStance);
					if (!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, NULL)) {
						return false;
					}
					bRetVal = false;
				}
			}
		} else {
			// TODO: do players require stance?
		}
	}

	voice = GET_REF(pPCCostume->hVoice);
	if (voice)
	{
		PCVoice *v;
		VoiceRef ***peaVoices = pSpecies ? &(VoiceRef**)pSpecies->eaAllowedVoices : NULL;
		const char *pKeyValue;

		if (pSpecies && (!pSpecies->bAllowAllVoices) && ((!peaVoices) || !eaSize(peaVoices)))
		{
			sprintf(cError,"Costume voice %s is not allowed for this species", REF_STRING_FROM_HANDLE(pPCCostume->hVoice));
			if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, NULL))
			{
				return false;
			}
			bRetVal = false;
		}

		if (peaVoices && eaSize(peaVoices))
		{
			for (i = eaSize(peaVoices)-1; i >= 0; --i)
			{
				v = GET_REF((*peaVoices)[i]->hVoice);
				if (!v) continue;
				if (v == voice) break;
			}
			if (i < 0)
			{
				sprintf(cError,"Costume voice %s is not allowed for this species", REF_STRING_FROM_HANDLE(pPCCostume->hVoice));
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, NULL))
				{
					return false;
				}
				bRetVal = false;
			}
		}
		if (voice->pcUnlockCode && *voice->pcUnlockCode)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
			pKeyValue = gad_GetAttribStringFromExtract(pExtract, voice->pcUnlockCode);
			if((!pKeyValue) || atoi(pKeyValue) <= 0)
			{
				sprintf(cError,"Costume voice %s is not unlocked for this player", REF_STRING_FROM_HANDLE(pPCCostume->hVoice));
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, NULL))
				{
					return false;
				}
				bRetVal = false;
			}
		}
	}

	if ((!pTestItems) || pTestItems->bTestHeight)
	{
		// height check based on skeleton
		F32 hMin, hMax;
		hMin = costumeTailor_GetOverrideHeightMin(pSpecies, pSlotType);
		hMax = costumeTailor_GetOverrideHeightMax(pSpecies, pSlotType);
		if(hMax)
		{
			if(pPCCostume->fHeight < hMin || pPCCostume->fHeight > hMax)
			{
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "fHeight is out of range for the species or slot", NULL))
				{
					return false;
				}
				bRetVal = false;
			}
		}
		else if((pSkeleton->fHeightBase != 0.0) && ((pPCCostume->fHeight < pSkeleton->fPlayerMinHeight) || (pPCCostume->fHeight > pSkeleton->fPlayerMaxHeight)))
		{
			if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "height is out of range for the skeleton", NULL))
			{
				return false;
			}
			bRetVal = false;
		}
	}

	// part check requires skeleton
	if(pPCCostume->eaParts)
	{
		if (eaSize(&eaOverrideUnlockedCostumes))
		{
			eaUnlockedCostumes = eaOverrideUnlockedCostumes;
		}
		else
		{
			GameAccountData *pData = NULL;
			if(pPlayerEnt && pPlayerEnt->pPlayer){
				pData = entity_GetGameAccount(pPlayerEnt);
			}
			costumeEntity_GetUnlockCostumes(pPlayerEnt && pPlayerEnt->pSaved ? pPlayerEnt->pSaved->costumeData.eaUnlockedCostumeRefs : NULL, pData, pPlayerEnt, pEnt, &eaUnlockedCostumes);
		}

		// check parts
		for(i=eaSize(&pPCCostume->eaParts)-1; i>=0; --i)
		{
			PCBoneDef *pBone = GET_REF(pPCCostume->eaParts[i]->hBoneDef);
			PCRegion *pRegion = pBone ? GET_REF(pBone->hRegion) : NULL;

			if (pTestItems && pTestItems->bTestBones)
			{
				for (j = eaSize(&pTestItems->eaBonesUsed)-1; j >= 0; --j)
				{
					if(GET_REF(pTestItems->eaBonesUsed[j]->hBone) == pBone)
					{
						break;
					}
				}
				if (j < 0) continue;
			}

			if (!pRegion || !costumeTailor_GetCategoryForRegion(pPCCostume, pRegion))
			{
				sprintf(cError,"PC part %d failed because it has no category", i);
				if (!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, pPCCostume->eaParts[i]))
				{
					return false;
				}
				bRetVal = false;
			}

			// TODO: is this required and if needed pass new parameter to not write out errors
			if(!costumeLoad_ValidatePCPart(pPCCostume, pPCCostume->eaParts[i], NULL, pSkeleton, kPCPartType_Primary, bInEditor, pGuild, false))
			{
				sprintf(cError,"PC part %d failed validatePCPart",i);
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, pPCCostume->eaParts[i]))
				{
					return false;
				}
				bRetVal = false;
			}

			if(!costumeValidate_ValidatePlayerCreatedPart(pPCCostume->eaParts[i], pSkeleton, pSpecies, pSlotType, pGuild, false, pPCCostume, ppEStringError))
			{
				sprintf(cError,"PC part %d failed costumeValidate_ValidatePlayerCreatedPart",i);
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, pPCCostume->eaParts[i]))
				{
					return false;
				}
				bRetVal = false;
			}

			for(j=i-1; j>=0; --j) {
				PCBoneDef *pOtherBone = GET_REF(pPCCostume->eaParts[j]->hBoneDef);
				if (pBone && pOtherBone && (pBone == pOtherBone)) {
					sprintf(cError,"PC part %d failed because there is another part on the same bone",i);
					if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, pPCCostume->eaParts[i]))
					{
						return false;
					}
					bRetVal = false;
				}
			}
		}

		if(!costumeValidate_AreRestrictionsValid(CONTAINER_NOCONST(PlayerCostume, pPCCostume), eaUnlockedCostumes, !(pEnt || pPlayerEnt), ppEStringError, pTestItems && pTestItems->bTestBones ? &pTestItems->eaBonesUsed : NULL))
		{
			if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "failed costume restriction check", NULL))
			{
				return false;
			}
			bRetVal = false;
		}
		
		// Make sure all required bone defs are in this costume
		for (i = eaSize(&pSkeleton->eaRequiredBoneDefs)-1; i >= 0; i--)
		{
			PCBoneDef *pBone = GET_REF(pSkeleton->eaRequiredBoneDefs[i]->hBone);

			if (pTestItems && pTestItems->bTestBones)
			{
				for (j = eaSize(&pTestItems->eaBonesUsed)-1; j >= 0; --j)
				{
					if(GET_REF(pTestItems->eaBonesUsed[j]->hBone) == pBone)
					{
						break;
					}
				}
				if (j < 0) continue;
			}

			if(pBone)
			{
				bool bFoundBone = false;
				S32 iPartIdx;
				// go through all parts and see if we have a part with this bone def
				for(iPartIdx = eaSize(&pPCCostume->eaParts)-1; iPartIdx >= 0; --iPartIdx)
				{
					PCBoneDef *pCostumeBone;
					pCostumeBone = GET_REF(pPCCostume->eaParts[iPartIdx]->hBoneDef);
					if(pCostumeBone == pBone)
					{
						bFoundBone = true;
						break;
					}
				}
				if(!bFoundBone)
				{
					sprintf(cError,"Required bone %s not found", pBone->pcName);
					if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, NULL))
					{
						return false;
					}
					bRetVal = false;
				}
			}
		}
	}
	else
	{
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "eaParts is null", NULL))
		{
			return false;
		}
		bRetVal = false;
	}

	if(pPCCostume->eaRegionCategories)
	{
		for(i=eaSize(&pPCCostume->eaRegionCategories)-1; i>=0; --i)
		{
			PCRegion *pReg = GET_REF(pPCCostume->eaRegionCategories[i]->hRegion);
			PCCategory *pCat = GET_REF(pPCCostume->eaRegionCategories[i]->hCategory);
			PCCategory **eaCategories = NULL;

			if (!pReg)
			{
				sprintf(cError,"eaRegionCategories[%d] pReg is null",i);
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, NULL))
				{
					return false;
				}
				bRetVal = false;
				break;
			}

			if(!pCat)
			{
				if(pReg)
				{
					sprintf(cError,"eaRegionCategories[%d] pCat for region \"%s\" is null",i,pReg->pcName);
				}
				else
				{
					sprintf(cError,"eaRegionCategories[%d] pCat is null",i);
				}

				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, NULL))
				{
					return false;
				}
				bRetVal = false;
				break;
			}

			costumeTailor_GetValidCategories(CONTAINER_NOCONST(PlayerCostume, pPCCostume), pReg, pSpecies, eaUnlockedCostumes, NULL, pSlotType, &eaCategories, 0);

			if (eaFind(&eaCategories, pCat) == -1)
			{
				sprintf(cError,"eaRegionCategories[%d] is not legal on this costume",i);
				if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, cError, NULL))
				{
					return false;
				}
				bRetVal = false;
			}

			eaDestroy(&eaCategories);
		}
	}
	else
	{
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "eaRegionCategories is null", NULL))
		{
			return false;
		}
		bRetVal = false;
	}

	if(pPCCostume->pArtistData && pPCCostume->pArtistData->ppCollCapsules)
	{
		if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "ppCollCapsules is set", NULL))
		{
			return false;
		}
		bRetVal = false;
	}

	if ((!pTestItems) || pTestItems->bTestSkinColor)
	{
		pColorSetSkin = costumeTailor_GetOverrideSkinColorSet(pSpecies, pSlotType);
		if (!pColorSetSkin) pColorSetSkin = GET_REF(pSkeleton->hSkinColorSet);
		if(!pColorSetSkin)
		{
			if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "No skin color set", NULL))
			{
				return false;
			}
			bRetVal = false;
		}

		if(!costumeLoad_ColorInSet(pPCCostume->skinColor, pColorSetSkin) && !gConf.bAllowPlayerCostumeFX)
		{
			if(!costumeValidate_PlayerCreatedError(pPCCostume, ppEStringError, "Skin color does not match skeleton", NULL))
			{
				return false;
			}
			bRetVal = false;
		}
	}

	// pPCCostume->pcName might be null or the characters name, ignore this field Also used in tailor to rename costume.
	// pcFileName ignore
	// pcScope ignore

	if (eaUnlockedCostumes != eaOverrideUnlockedCostumes)
	{
		eaDestroy(&eaUnlockedCostumes);
	}

	return bRetVal;
}


void costumeTailor_ApplyPresetScaleValueGroup(NOCONST(PlayerCostume) *pCostume, PCPresetScaleValueGroup *pPreset)
{
	PCSkeletonDef *pSkel = GET_REF(pCostume->hSkeleton);
	if (pSkel && pPreset)
	{
		int i, j;
		for (i = 0; i < eaSize(&pPreset->eaScaleValues); ++i)
		{
			PCScaleValue *pScaleValueToApply = pPreset->eaScaleValues[i];
			
			NOCONST(PCScaleValue) *pScaleValue = NULL;

			// Find the named scale if already exists
			for(j=eaSize(&pCostume->eaScaleValues)-1; j>=0; --j) {
				NOCONST(PCScaleValue) *pValue = pCostume->eaScaleValues[j];
				if (stricmp(pValue->pcScaleName,pScaleValueToApply->pcScaleName) == 0) {
					pScaleValue = pValue;
					break;
				}
			}

			// if zero, remove it
			if (!pScaleValueToApply->fValue) {
				if (pScaleValue) {
					StructDestroyNoConst(parse_PCScaleValue, pScaleValue);
					eaRemove(&pCostume->eaScaleValues, j);
				}
				continue;
			}
			// If no entry found, create one
			if (!pScaleValue) {
				pScaleValue = StructCreateNoConst(parse_PCScaleValue);
				pScaleValue->pcScaleName = StructAllocString(pScaleValueToApply->pcScaleName);
				eaPush(&pCostume->eaScaleValues, pScaleValue);
			}
			// Set the value
			pScaleValue->fValue = pScaleValueToApply->fValue;
		}
	}
}

Gender costumeTailor_GetGender(PlayerCostume *pCostume)
{
	if (pCostume) {
		PCSkeletonDef* pSkel = GET_REF(pCostume->hSkeleton);
		if (pCostume->eGender == Gender_Unknown && pSkel) {
			return pSkel->eGender;
		}
		else {
			return pCostume->eGender;
		}
	}
	return Gender_Unknown;
}

static void costumeTailor_FixPartChildGeos(NOCONST(PlayerCostume) *pFixUpCostume, SpeciesDef *pSpecies, NOCONST(PCPart) *pPart, PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, bool bSortDisplay, bool bUnlockAll, bool bInEditor, Guild *pGuild)
{
	int j;
	PCGeometryDef *pGeo = GET_REF(pPart->hGeoDef);
	if (pGeo && pGeo->pOptions)
	{
		for (j = eaSize(&pGeo->pOptions->eaChildGeos)-1; j >= 0; --j)
		{
			PCGeometryChildDef *pChildGeo = pGeo->pOptions->eaChildGeos[j];
			if (pChildGeo->bRequiresChild)
			{
				pPart = StructCreateNoConst(parse_PCPart);
				COPY_HANDLE(pPart->hBoneDef, pChildGeo->hChildBone);
				eaPush(&pFixUpCostume->eaParts, pPart);
				costumeTailor_PickValidPartValues(pFixUpCostume, pPart, pSpecies, pSlotType, eaUnlockedCostumes, bSortDisplay, bUnlockAll, true, bInEditor, pGuild);
				costumeTailor_FixPartChildGeos(pFixUpCostume, pSpecies, pPart, eaUnlockedCostumes, pSlotType, bSortDisplay, bUnlockAll, bInEditor, pGuild);
			}
		}
	}
}

static void costumeTailor_FixUpBoneScale(NOCONST(PlayerCostume) *pFixUpCostume, PCSkeletonDef *pSkel, SpeciesDef *pSpecies, PCSlotType *pSlotType, PCScaleInfo *pInfo)
{
	NOCONST(PCScaleValue) *pValue = NULL;
	F32 fMin, fMax;
	int i;

	for(i=eaSize(&pFixUpCostume->eaScaleValues)-1; i>=0; --i) {
		if (stricmp(pInfo->pcName, pFixUpCostume->eaScaleValues[i]->pcScaleName) == 0) {
			pValue = pFixUpCostume->eaScaleValues[i];
			break;
		}
	}

	// Get the override value, or use the default range
	if(!costumeTailor_GetOverrideBoneScale(pSkel, pInfo, pInfo->pcName, pSpecies, pSlotType, &fMin, &fMax)) {
		fMin = pInfo->fPlayerMin;
		fMax = pInfo->fPlayerMax;
	}

	if (pValue) {
		pValue->fValue = CLAMP(pValue->fValue, fMin, fMax);
	} else if (fMin > 0 || fMax < 0) {
		// If scale not set and zero not legal, add scale
		pValue = StructCreateNoConst(parse_PCScaleValue);
		pValue->pcScaleName = pInfo->pcName;
		pValue->fValue = fMin + ((fMax - fMin) / 2); // Set to halfway
		eaPush(&pFixUpCostume->eaScaleValues, pValue);
	}
}

void costumeTailor_MakeCostumeValid(NOCONST(PlayerCostume) *pFixUpCostume, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, bool bSortDisplay, bool bUnlockAll, bool bInEditor, Guild *pGuild,
	bool bIgnoreRegCatNull, GameAccountDataExtract *pExtract, bool bUGC, const char** eapchPowerFXBones)
{
	PCSkeletonDef *pSkel;
	PCRegion **eaRegions = NULL;
	PCBoneDef **eaBones = NULL;
	UIColorSet *pColorSet = NULL;
	int i,j,k;

	PERFINFO_AUTO_START_FUNC();

	// Fix up skeleton if required
	PERFINFO_AUTO_START("Fixup Skeleton", 1);
	if (pSpecies) {
		if (GET_REF(pSpecies->hSkeleton) && (GET_REF(pSpecies->hSkeleton) != GET_REF(pFixUpCostume->hSkeleton))) {
			costumeTailor_ChangeSkeleton(pFixUpCostume, pSpecies, pSlotType, GET_REF(pSpecies->hSkeleton));
		}
	}
	PERFINFO_AUTO_STOP();
	pSkel = GET_REF(pFixUpCostume->hSkeleton);
	if (!pSkel) {
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	PERFINFO_AUTO_START("Fixup Regions", 1);
	// Make sure all the required bones have their categories set
	costumeTailor_GetValidRegions(pFixUpCostume, pSpecies, eaUnlockedCostumes, eapchPowerFXBones, pSlotType, &eaRegions, CGVF_OMIT_EMPTY | (bUnlockAll ? CGVF_UNLOCK_ALL : 0) | CGVF_REQUIRE_POWERFX);
	for(i=eaSize(&pSkel->eaRequiredBoneDefs)-1; i>=0; --i) {
		PCBoneDef *pBone = GET_REF(pSkel->eaRequiredBoneDefs[i]->hBone);
		if (eaFind(&eaRegions, GET_REF(pBone->hRegion)) < 0) {
			// Something is making the list of regions invalid (most likely a category).
			// Rebuild the entire list of region categories
			eaClearStructNoConst(&pFixUpCostume->eaRegionCategories, parse_PCRegionCategory);
			costumeTailor_GetValidRegions(pFixUpCostume, pSpecies, eaUnlockedCostumes, eapchPowerFXBones, pSlotType, &eaRegions, CGVF_OMIT_EMPTY | (bUnlockAll ? CGVF_UNLOCK_ALL : 0) | CGVF_REQUIRE_POWERFX);
			costumeTailor_FillAllRegions(pFixUpCostume, pSpecies, NULL, pSlotType);
			break;
		}
	}
	// If the list of region categories was not rebuilt, then make all the categories valid now
	if (i<0) {
		PERFINFO_AUTO_START("Fixup Categories", 1);
		// Pick valid categories, this is O(N^2) because worst case only one category
		// might be fixed per pass.
		for(j=eaSize(&eaRegions)-1; eaSize(&eaRegions) && j>=0; --j) {
			for(i=eaSize(&eaRegions)-1; i>=0; --i) {
				PCCategory **eaCats = NULL;
				costumeTailor_GetValidCategories(pFixUpCostume, eaRegions[i], pSpecies, eaUnlockedCostumes, eapchPowerFXBones, pSlotType, &eaCats, CGVF_OMIT_EMPTY | (bUnlockAll ? CGVF_UNLOCK_ALL : 0) | CGVF_REQUIRE_POWERFX);
				if (eaSize(&eaCats)) {
					if (!costumeTailor_GetCategoryForRegion((PlayerCostume *)pFixUpCostume, eaRegions[i])) {
						// missing region category, create a new one
						NOCONST(PCRegionCategory) *pRegCat = StructCreateNoConst(parse_PCRegionCategory);
						eaPush(&pFixUpCostume->eaRegionCategories, pRegCat);
						SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, eaRegions[i], pRegCat->hRegion);
						if (IS_HANDLE_ACTIVE(eaRegions[i]->hDefaultCategory)) {
							COPY_HANDLE(pRegCat->hCategory, eaRegions[i]->hDefaultCategory);
						} else {
							SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, eaCats[0], pRegCat->hCategory);
						}
					}
					costumeTailor_PickValidCategoryForRegion(pFixUpCostume, eaRegions[i], eaCats, bIgnoreRegCatNull);
					eaRemove(&eaRegions, i);
				} else {
					// Clear the region, try again next pass
					costumeTailor_SetRegionCategory(pFixUpCostume, eaRegions[i], NULL);
				}
				eaDestroy(&eaCats);
			}
		}
		if (eaSize(&eaRegions)>0) {
			// Something is preventing the list of regions from becoming valid, rebuild the region categories
			eaClearStructNoConst(&pFixUpCostume->eaRegionCategories, parse_PCRegionCategory);
			costumeTailor_FillAllRegions(pFixUpCostume, pSpecies, NULL, pSlotType);
		}
		PERFINFO_AUTO_STOP();
	}
	// Remove existing regions that aren't in the list of valid regions
	costumeTailor_GetValidRegions(pFixUpCostume, pSpecies, eaUnlockedCostumes, eapchPowerFXBones, pSlotType, &eaRegions, CGVF_OMIT_EMPTY | (bUnlockAll ? CGVF_UNLOCK_ALL : 0) | CGVF_REQUIRE_POWERFX);
	for (i=eaSize(&pFixUpCostume->eaRegionCategories)-1; i>=0; --i) {
		if (GET_REF(pFixUpCostume->eaRegionCategories[i]->hRegion) && eaFind(&eaRegions, GET_REF(pFixUpCostume->eaRegionCategories[i]->hRegion))<0) {
			StructDestroyNoConst(parse_PCRegionCategory, eaRemove(&pFixUpCostume->eaRegionCategories, i));
		}
	}
	eaDestroy(&eaRegions);
	PERFINFO_AUTO_STOP();

	// Pick valid body scales (if player costume)
	PERFINFO_AUTO_START("Fixup Body Scales", 1);
	eafSetSize(&pFixUpCostume->eafBodyScales, eaSize(&pSkel->eaBodyScaleInfo));
	if (IS_PLAYER_COSTUME(pFixUpCostume)) {

		for(i=0; i<eaSize(&pSkel->eaBodyScaleInfo); ++i) {
			PCBodyScaleInfo *pInfo = pSkel->eaBodyScaleInfo[i];
			bool bFound = false;
			F32 fMin, fMax;

			if(costumeTailor_GetOverrideBodyScale(pSkel, pInfo->pcName, pSpecies, pSlotType, &fMin, &fMax))
			{
				pFixUpCostume->eafBodyScales[i] = CLAMP(pFixUpCostume->eafBodyScales[i], fMin, fMax);
				bFound = true;
			}
			if (!bFound) {
				pFixUpCostume->eafBodyScales[i] = CLAMP(pFixUpCostume->eafBodyScales[i], pSkel->eafPlayerMinBodyScales[i], pSkel->eafPlayerMaxBodyScales[i]);
			}
		}
	}
	PERFINFO_AUTO_STOP();

	// Fix up bone scales
	PERFINFO_AUTO_START("Fixup Bone Scales", 1);
	// Remove non-existent bone scales
	for(i=eaSize(&pFixUpCostume->eaScaleValues)-1; i>=0; --i) {
		NOCONST(PCScaleValue) *pValue = pFixUpCostume->eaScaleValues[i];
		PCScaleInfo *pInfo = NULL;
		bool bFound = false;

		for(j=eaSize(&pSkel->eaScaleInfoGroups)-1; j>=0 && !bFound; --j) {
			PCScaleInfoGroup *pGroup = pSkel->eaScaleInfoGroups[j];
			for(k=eaSize(&pGroup->eaScaleInfo)-1; k>=0; --k) {
				pInfo = pGroup->eaScaleInfo[k];
				if (stricmp(pInfo->pcName, pValue->pcScaleName) == 0) {
					bFound = true;
					break;
				}
			}
		}
		for(j=eaSize(&pSkel->eaScaleInfo)-1; j>=0 && !bFound; --j) {
			pInfo = pSkel->eaScaleInfo[j];
			if (stricmp(pInfo->pcName, pValue->pcScaleName) == 0) {
				bFound = true;
				break;
			}
		}

		if (!bFound) {
			// Remove scale if not found
			StructDestroyNoConst(parse_PCScaleValue, pValue);
			eaRemove(&pFixUpCostume->eaScaleValues, i);
		}
	}
	// Add any required bone scales and fix existing ones
	if (IS_PLAYER_COSTUME(pFixUpCostume)) {
		for(i=eaSize(&pSkel->eaScaleInfoGroups)-1; i>=0; --i) {
			PCScaleInfoGroup *pGroup = pSkel->eaScaleInfoGroups[i];
			for(j=eaSize(&pGroup->eaScaleInfo)-1; j>=0; --j) {
				PCScaleInfo *pInfo = pGroup->eaScaleInfo[j];
				costumeTailor_FixUpBoneScale(pFixUpCostume, pSkel, pSpecies, pSlotType, pInfo);
			}
		}
		for(i=eaSize(&pSkel->eaScaleInfo)-1; i>=0; --i) {
			PCScaleInfo *pInfo = pSkel->eaScaleInfo[i];
			costumeTailor_FixUpBoneScale(pFixUpCostume, pSkel, pSpecies, pSlotType, pInfo);
		}
	}
	PERFINFO_AUTO_STOP();

	// Fix muscle & height value
	PERFINFO_AUTO_START("Fixup Muscle & Height", 1);
	if (IS_PLAYER_COSTUME(pFixUpCostume)) {
		F32 fHMin, fHMax;
		fHMin = costumeTailor_GetOverrideHeightMin(pSpecies, pSlotType);
		fHMax = costumeTailor_GetOverrideHeightMax(pSpecies, pSlotType);
		if (fHMax) {
			pFixUpCostume->fHeight = CLAMP(pFixUpCostume->fHeight, fHMin, fHMax);
		} else {
			pFixUpCostume->fHeight = CLAMP(pFixUpCostume->fHeight, pSkel->fPlayerMinHeight, pSkel->fPlayerMaxHeight);
		}
		fHMin = costumeTailor_GetOverrideMuscleMin(pSpecies, pSlotType);
		fHMax = costumeTailor_GetOverrideMuscleMax(pSpecies, pSlotType);
		if(fHMax)
		{
			pFixUpCostume->fMuscle = CLAMP(pFixUpCostume->fMuscle, fHMin, fHMax);
		} else
		{
			pFixUpCostume->fMuscle = CLAMP(pFixUpCostume->fMuscle, pSkel->fPlayerMinMuscle, pSkel->fPlayerMaxMuscle);
		}
	}
	PERFINFO_AUTO_STOP();

	// Pick valid stance
	PERFINFO_AUTO_START("Fixup Stance", 1);
	costumeTailor_PickValidStance(pFixUpCostume, pSpecies, pSlotType, eaUnlockedCostumes, bUnlockAll);
	PERFINFO_AUTO_STOP();

	// Pick valid voice
	PERFINFO_AUTO_START("Fixup Voice", 1);
	costumeTailor_PickValidVoice(pFixUpCostume, pSpecies, pExtract);
	PERFINFO_AUTO_STOP();

	// Pick valid skin color
	PERFINFO_AUTO_START("Fixup Skin Color", 1);
	if (!bUGC) {
		pColorSet = costumeTailor_GetOverrideSkinColorSet(pSpecies, pSlotType);
		if (!pColorSet) {
			pColorSet = GET_REF(pSkel->hSkinColorSet);
		}
		if (pColorSet && !costumeLoad_ColorInSet(pFixUpCostume->skinColor, pColorSet)) {
			costumeTailor_FindClosestColor(pFixUpCostume->skinColor, pColorSet, pFixUpCostume->skinColor);
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Make Sure Required Parts Exist", 1);
	for(i=eaSize(&pSkel->eaRequiredBoneDefs)-1; i>=0; --i) {
		PCBoneDef *pBone = GET_REF(pSkel->eaRequiredBoneDefs[i]->hBone);
		if (pBone)
		{
			NOCONST(PCPart) *pPart = costumeTailor_GetPartByBone(pFixUpCostume, pBone, NULL);
			if (!pPart)
			{
				pPart = StructCreateNoConst(parse_PCPart);
				SET_HANDLE_FROM_REFERENT("CostumeBone", pBone, pPart->hBoneDef);
				eaPush(&pFixUpCostume->eaParts, pPart);
				costumeTailor_PickValidPartValues(pFixUpCostume, pPart, pSpecies, pSlotType, eaUnlockedCostumes, bSortDisplay, bUnlockAll, !bUGC, bInEditor, pGuild);
				costumeTailor_FixPartChildGeos(pFixUpCostume, pSpecies, pPart, eaUnlockedCostumes, pSlotType, bSortDisplay, bUnlockAll, bInEditor, pGuild);
			}
		}
	}
	for(i=eaSize(&pSkel->eaOptionalBoneDefs)-1; i>=0; --i) {
		PCBoneDef *pBone = GET_REF(pSkel->eaOptionalBoneDefs[i]->hBone);
		if (pBone && costumeTailor_IsBoneRequired(pFixUpCostume, pBone, pSpecies))
		{
			NOCONST(PCPart) *pPart = costumeTailor_GetPartByBone(pFixUpCostume, pBone, NULL);
			if (!pPart)
			{
				pPart = StructCreateNoConst(parse_PCPart);
				SET_HANDLE_FROM_REFERENT("CostumeBone", pBone, pPart->hBoneDef);
				eaPush(&pFixUpCostume->eaParts, pPart);
				costumeTailor_PickValidPartValues(pFixUpCostume, pPart, pSpecies, pSlotType, eaUnlockedCostumes, bSortDisplay, bUnlockAll, !bUGC, bInEditor, pGuild);
				costumeTailor_FixPartChildGeos(pFixUpCostume, pSpecies, pPart, eaUnlockedCostumes, pSlotType, bSortDisplay, bUnlockAll, bInEditor, pGuild);
			}
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Remove bones that don't exist", 1);
	// Remove parts that are on bones that don't exist on this skeleton (ignoring all factors)
	costumeTailor_GetValidBones(pFixUpCostume, pSkel, NULL, NULL, NULL, NULL, NULL, &eaBones, CGVF_UNLOCK_ALL);
	for(i=eaSize(&pFixUpCostume->eaParts)-1; i>=0; --i) {
		if (eaFind(&eaBones, GET_REF(pFixUpCostume->eaParts[i]->hBoneDef)) == -1) {
			StructDestroyNoConst(parse_PCPart, pFixUpCostume->eaParts[i]);
			eaRemove(&pFixUpCostume->eaParts, i);
		}
	}
	eaDestroy(&eaBones);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Fixup Parts", 1);
	for(i=eaSize(&pFixUpCostume->eaParts)-1; i>=0; --i) {
		PCBoneDef *pBone = GET_REF(pFixUpCostume->eaParts[i]->hBoneDef);
		// Be sure not to validate child bones because the parent will do that work
		// and the child does not have enough information to perform the task
		if (pBone && !pBone->bIsChildBone) {
			costumeTailor_PickValidPartValues(pFixUpCostume, pFixUpCostume->eaParts[i], pSpecies, pSlotType, eaUnlockedCostumes, bSortDisplay, bUnlockAll, !bUGC, bInEditor, pGuild);
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Fixup Artist Data", 1);
	// Remove bad artist data if costume is a player costume
	if (IS_PLAYER_COSTUME(pFixUpCostume) && pFixUpCostume->pArtistData && !bInEditor) {
		StructDestroyNoConst(parse_PCArtistCostumeData, pFixUpCostume->pArtistData);
		pFixUpCostume->pArtistData = NULL;
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Fixup Body Sock Data", 1);
	// Remove bad artist data if costume is a player costume
	if (IS_PLAYER_COSTUME(pFixUpCostume) && pFixUpCostume->pBodySockInfo && !bInEditor) {
		StructDestroyNoConst(parse_PCBodySockInfo, pFixUpCostume->pBodySockInfo);
		pFixUpCostume->pBodySockInfo = NULL;
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP_FUNC();
}

// Get the override for the body scales, slot is higher priority than species. Add new overrides here.
bool costumeTailor_GetOverrideBodyScale(const PCSkeletonDef *pSkeleton, const char *pcName, const SpeciesDef *pSpecies, const PCSlotType *pSlotType, F32 *fMinOut, F32 *fMaxOut)
{
	F32 fMin = 0.0f, fMax = 0.0f;
	bool bFound = false, bFoundSlot = false, bFoundSpecies = false, bOutOffRange = false;
	S32 i;
	
	if(!pSkeleton || !pcName) {
		return false;
	}
	
	if(pSlotType && pSlotType->bUseCostumeSlotOverride)
	{
		for(i = eaSize(&pSlotType->eaBodyScaleLimits)-1; i >= 0; --i)
		{
			if(stricmp(pcName, pSlotType->eaBodyScaleLimits[i]->pcName) == 0)
			{
				fMin = pSlotType->eaBodyScaleLimits[i]->fMin;
				fMax = pSlotType->eaBodyScaleLimits[i]->fMax;
				bFound = true;
				bFoundSlot = true;
				break;
			}
		}
	}
	
	if(!bFound && pSpecies)
	{
		for (i = eaSize(&pSpecies->eaBodyScaleLimits)-1; i >= 0; --i)
		{
			if(stricmp(pcName, pSpecies->eaBodyScaleLimits[i]->pcName) == 0)
			{
				fMin = pSpecies->eaBodyScaleLimits[i]->fMin;
				fMax = pSpecies->eaBodyScaleLimits[i]->fMax;
				bFound = true;
				bFoundSpecies = true;
				break;
			}
		}
	}

	if(bFound)
	{
		if(fMinOut)
		{
			*fMinOut = fMin;
		}
		if(fMaxOut)
		{
			*fMaxOut = fMax;
		}
	}
	
	return bFound;	
}

// Get the override for the bone scales, slot is higher priority than species. Add new overrides here.
bool costumeTailor_GetOverrideBoneScale(const PCSkeletonDef *pSkeleton, const PCScaleInfo *pScale, const char *pcName, const SpeciesDef *pSpecies, const PCSlotType *pSlotType, F32 *fMinOut, F32 *fMaxOut)
{
	F32 fMin = 0.0f, fMax = 0.0f;
	bool bFound = false, bFoundSlot = false, bFoundSpecies = false, bOutOffRange = false;
	S32 i,j,k;

	if(!pSkeleton || !pcName)
	{
		return false;
	}

	if(pSlotType && pSlotType->bUseCostumeSlotOverride)
	{
		for(i = eaSize(&pSlotType->eaBoneScaleLimits)-1; i >= 0; --i)
		{
			if(stricmp(pcName, pSlotType->eaBoneScaleLimits[i]->pcName) == 0)
			{
				fMin = pSlotType->eaBoneScaleLimits[i]->fMin;
				fMax = pSlotType->eaBoneScaleLimits[i]->fMax;
				bFound = true;
				bFoundSlot = true;
				break;
			}
		}
	}

	if(!bFound && pSpecies)
	{
		for (i = eaSize(&pSpecies->eaBoneScaleLimits)-1; i >= 0; --i)
		{
			if(stricmp(pcName, pSpecies->eaBoneScaleLimits[i]->pcName) == 0)
			{
				fMin = pSpecies->eaBoneScaleLimits[i]->fMin;
				fMax = pSpecies->eaBoneScaleLimits[i]->fMax;
				bFound = true;
				bFoundSpecies = true;
				break;
			}
		}
	}

	if(bFound)
	{
		// use the skeleton scale to make sure the override is in bounds
		if(!pScale)
		{
			// match for in skeleton?
			for (j=0; (j < eaSize(&pSkeleton->eaScaleInfoGroups) && !pScale); ++j)
			{
				for (k=0; k < eaSize(&pSkeleton->eaScaleInfoGroups[j]->eaScaleInfo); ++k)
				{
					if(stricmp(pSkeleton->eaScaleInfoGroups[j]->eaScaleInfo[k]->pcName, pcName) == 0)
					{
						pScale = pSkeleton->eaScaleInfoGroups[j]->eaScaleInfo[k];
						break;
					}
				}
			}
			if (!pScale)
			{
				for (k=0; k < eaSize(&pSkeleton->eaScaleInfo); ++k)
				{
					if(stricmp(pSkeleton->eaScaleInfo[k]->pcName, pcName) == 0)
					{
						pScale = pSkeleton->eaScaleInfo[k];
						break;
					}
				}
			}
		}
		
		if(!pScale)
		{
			return false;
		}

		if(fMin < pScale->fPlayerMin)
		{
			fMin = pScale->fPlayerMin;
			bOutOffRange = true;
		}
		if(fMax > pScale->fPlayerMax)
		{
			fMax = pScale->fPlayerMax;
			bOutOffRange = true;
		}
		
		// Show errors so they can be corrected
		if(bOutOffRange)
		{
			if(bFoundSlot)
			{
				Errorf("OverrideBoneScale: Skeleton: %s, slot type %s is out of range for %s",pSkeleton->pcName, pSlotType->pcName, pcName);
			}
			else if (bFoundSpecies)
			{
				Errorf("OverrideBoneScale: Skeleton: %s, species %s is out of range for %s",pSkeleton->pcName, pSpecies->pcName, pcName);
			}
		}
		
		if(fMinOut)
		{
			*fMinOut = fMin;
		}
		if(fMaxOut)
		{
			*fMaxOut = fMax;
		}
	}

	return bFound;	
}

UIColorSet *costumeTailor_GetOverrideSkinColorSet(const SpeciesDef *pSpecies, const PCSlotType *pSlotType)
{
	UIColorSet *pColor = NULL;
	if(pSlotType && pSlotType->bUseCostumeSlotOverride && pSlotType->fMaxHeight)
	{
		pColor = GET_REF(pSlotType->hSkinColorSet);	
	}
	
	if(pSpecies && !pColor)
	{
		pColor = GET_REF(pSpecies->hSkinColorSet);	
	}
	
	return pColor;
}

F32 costumeTailor_GetOverrideHeightMin(const SpeciesDef *pSpecies, const PCSlotType *pSlotType)
{
	if(pSlotType && pSlotType->bUseCostumeSlotOverride && pSlotType->fMaxHeight)
	{
		return pSlotType->fMinHeight;
	}
	
	if(pSpecies)
	{
		return pSpecies->fMinHeight;
	}
	
	return 0.0f;
}

F32 costumeTailor_GetOverrideHeightMax(const SpeciesDef *pSpecies, const PCSlotType *pSlotType)
{
	if(pSlotType && pSlotType->bUseCostumeSlotOverride && pSlotType->fMaxHeight)
	{
		return pSlotType->fMaxHeight;
	}

	if(pSpecies)
	{
		return pSpecies->fMaxHeight;
	}

	return 0.0f;
}

bool costumeTailor_GetOverrideHeightNoChange(const SpeciesDef *pSpecies, const PCSlotType *pSlotType)
{
	if(pSlotType && pSlotType->bUseCostumeSlotOverride)
	{
		return pSlotType->bNoHeightChange;
	}

	if(pSpecies)
	{
		return pSpecies->bNoHeightChange;
	}
	
	return false;
}

F32 costumeTailor_GetOverrideMuscleMin(const SpeciesDef *pSpecies, const PCSlotType *pSlotType)
{
	if(pSlotType && pSlotType->bUseCostumeSlotOverride && pSlotType->fMaxMuscle)
	{
		return pSlotType->fMinMuscle;
	}

	if(pSpecies)
	{
		return pSpecies->fMinMuscle;
	}

	return 0.0f;
}

F32 costumeTailor_GetOverrideMuscleMax(const SpeciesDef *pSpecies, const PCSlotType *pSlotType)
{
	if(pSlotType && pSlotType->bUseCostumeSlotOverride && pSlotType->fMaxMuscle)
	{
		return pSlotType->fMaxMuscle;
	}

	if(pSpecies)
	{
		return pSpecies->fMaxMuscle;
	}

	return 0.0f;
}

bool costumeTailor_GetOverrideMuscleNoChange(const SpeciesDef *pSpecies, const PCSlotType *pSlotType)
{
	if(pSlotType && pSlotType->bUseCostumeSlotOverride)
	{
		return pSlotType->bNoMuscle;
	}

	if(pSpecies)
	{
		return pSpecies->bNoMuscle;
	}

	return false;
}

Gender costumeTailor_GetGenderFromSpeciesOrCostume(const SpeciesDef *pSpecies, const PlayerCostume *pCostume)
{
	if(pSpecies && pSpecies->eGender != Gender_Unknown)
	{
		return pSpecies->eGender;
	}
	
	if(pCostume)
	{
		return pCostume->eGender;
	}
	
	return Gender_Unknown;
}

// get the stance information from the slot override if it exists.
const PCSlotStanceStruct *costumeTailor_GetStanceFromSlot(const PCSlotType *pSlotType, Gender eGender)
{
	S32 i;
	
	if(pSlotType && pSlotType->bUseCostumeSlotOverride)
	{
		for(i= 0; i < eaSize(&pSlotType->eaSlotStances); ++i)
		{
			if(pSlotType->eaSlotStances[i]->eGender == eGender && eaSize(&pSlotType->eaSlotStances[i]->eaStanceInfo) > 0)
			{
				return pSlotType->eaSlotStances[i];
			}
		}
	}
	
	return NULL;
}


// This function sets the category on the costume and applies rules while setting the value
bool costumeTailor_SetCategory(NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies, PCRegion *pRegion, const char *pchCategory,
								PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCSlotType *pSlotType,
								Guild *pGuild, bool bUnlockAll)
{
	PCCategory *pCategory = RefSystem_ReferentFromString(g_hCostumeCategoryDict, pchCategory);
	int i;

	if (pCategory) {
		costumeTailor_SetRegionCategory(pCostume, pRegion, pCategory);

		// Need to revalidate all category choices
		for(i=eaSize(&pCostume->eaRegionCategories)-1; i>=0; --i) {
			PCRegion *pCatRegion = GET_REF(pCostume->eaRegionCategories[i]->hRegion);
			PCCategory **eaCats = NULL;
			if (pRegion) {
				costumeTailor_GetValidCategories(pCostume, pCatRegion, pSpecies, eaUnlockedCostumes, eaPowerFXBones, pSlotType, &eaCats, CGVF_OMIT_EMPTY | CGVF_SORT_DISPLAY | (bUnlockAll ? CGVF_UNLOCK_ALL : 0));
				costumeTailor_PickValidCategoryForRegion(pCostume, pCatRegion, eaCats, false);
			}
			eaDestroy(&eaCats);
		}

		// Need to revalidate parts in the current region against this choice
		for(i=eaSize(&pCostume->eaParts)-1; i>=0; --i) {
			PCBoneDef *pBone = GET_REF(pCostume->eaParts[i]->hBoneDef);
			if (pBone && !pBone->bIsChildBone && (GET_REF(pBone->hRegion) == pRegion)) {
				costumeTailor_PickValidPartValues(pCostume, pCostume->eaParts[i], pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, pGuild);
			}
		}
		return true;
	}
	return false;
}

bool costumeTailor_SetAllMaterials(NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies, const char *pcMatName,
								   PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, Guild *pGuild, bool bUnlockAll)
{
	PCMaterialDef *pMat = RefSystem_ReferentFromString(g_hCostumeMaterialDict, pcMatName);
	PCMaterialDef **eaMats = NULL;
	S32 i;

	if (!pMat) {
		return false;
	}

	for (i = eaSize(&pCostume->eaParts)-1; i>=0; --i) {
		NOCONST(PCPart) *pPart = pCostume->eaParts[i];
		PCGeometryDef *pGeo = GET_REF(pPart->hGeoDef);
		S32 iMaterial;

		costumeTailor_GetValidMaterials(pCostume, pGeo, pSpecies, NULL, NULL, eaUnlockedCostumes, &eaMats, false, false, bUnlockAll);
		iMaterial = costumeTailor_GetMatchingMatIndex(pMat, eaMats);

		SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, iMaterial >= 0 ? eaMats[iMaterial] : pMat, pPart->hMatDef);
		if (pGeo && pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pPart->pClothLayer) {
			// Always set both layers
			SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, iMaterial >= 0 ? eaMats[iMaterial] : pMat, pPart->pClothLayer->hMatDef);
		}

		costumeTailor_PickValidPartValues(pCostume, pPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, pGuild);
	}

	eaDestroy(&eaMats);

	return eaSize(&pCostume->eaParts) > 0;
}

bool costumeTailor_SetPartGeometry(NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies, PCBoneDef *pBone, const char *pchGeo,
								   PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, Guild *pGuild, bool bUnlockAll, bool bMirrorMode, bool bGroupMode)
{
	PCGeometryDef *pGeo = RefSystem_ReferentFromString(g_hCostumeGeometryDict, pchGeo);
	
	if (!stricmp(pchGeo, "None") && !pGeo) {
		pchGeo = NULL;
	}
	if (pGeo || !pchGeo) {
		PCGeometryDef *pNoneGeo = RefSystem_ReferentFromString(g_hCostumeGeometryDict, "None");
		bool bDefaultColor = false;
		// Need to get real part in case we're on back side of a cloth piece
		NOCONST(PCPart) *pRealPart;
		NOCONST(PCPart) *pEditPart;
		PCColorQuadSet *pColors;
		PCColorQuad *pQuad = NULL;

		if (!pBone) {
			return false;
		}
		if (!stricmp(pBone->pcName,"None")) {
			return false;
		}
		if(!pCostume) {
			return false;
		}

		pRealPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);
		pEditPart = pRealPart;
		assertmsgf(pRealPart, "Part not found in costume (%s) for BoneDef (%s)", pCostume->pcName, pBone->pcName);

		if (bMirrorMode && pRealPart->eEditMode == kPCEditMode_Right) {
			pEditPart = costumeTailor_GetMirrorPart(pCostume, pRealPart);
		}

		bDefaultColor = GET_REF(pEditPart->hGeoDef) == pNoneGeo;
		SET_HANDLE_FROM_STRING(g_hCostumeGeometryDict, pchGeo, pEditPart->hGeoDef);
		costumeTailor_PickValidPartValues(pCostume, pEditPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, pGuild);

		// Get the colors for the part, now that the geo has changed
		pColors = costumeTailor_GetColorQuadSetForPart(pCostume, pSpecies, pRealPart);
		if (pColors && eaSize(&pColors->eaColorQuads) > 0) {
			pQuad = pColors->eaColorQuads[0];
		}

		// Apply default colors after changed
		if (pQuad && bDefaultColor) {
			copyVec4(pQuad->color0, pEditPart->color0);
			copyVec4(pQuad->color1, pEditPart->color1);
			copyVec4(pQuad->color2, pEditPart->color2);
			copyVec4(pQuad->color3, pEditPart->color3);
		}

		// Apply mirror change if appropriate
		if (bMirrorMode && pRealPart->eEditMode == kPCEditMode_Both) {
			NOCONST(PCPart) *pMirrorPart = costumeTailor_GetMirrorPart(pCostume, pRealPart);
			if (pMirrorPart) {
				bDefaultColor = GET_REF(pMirrorPart->hGeoDef) == pNoneGeo;
				costumeTailor_CopyToMirrorPart(pCostume, pRealPart, pMirrorPart, pSpecies, EDIT_FLAG_GEOMETRY, eaUnlockedCostumes, true, bUnlockAll, false);

				// Apply default colors after changed
				if (bDefaultColor && pQuad) {
					copyVec4(pQuad->color0, pMirrorPart->color0);
					copyVec4(pQuad->color1, pMirrorPart->color1);
					copyVec4(pQuad->color2, pMirrorPart->color2);
					copyVec4(pQuad->color3, pMirrorPart->color3);
				}
			}
		}

		// Apply bone group change if appropriate
		if (bGroupMode && pRealPart->iBoneGroupIndex >= 0) {
			NOCONST(PCPart) *pGroupPart = NULL;
			PCSkeletonDef *pSkel = GET_REF(pCostume->hSkeleton);
			int i;

			if (pSkel) {
				for (i = eaSize(&pSkel->eaBoneGroups[pRealPart->iBoneGroupIndex]->eaBoneInGroup)-1; i >= 0; --i) {
					pGroupPart = costumeTailor_GetPartByBone(pCostume, GET_REF(pSkel->eaBoneGroups[pRealPart->iBoneGroupIndex]->eaBoneInGroup[i]->hBone), NULL);
					if (pGroupPart) {
						costumeTailor_CopyToGroupPart(pCostume, pRealPart, pGroupPart, pSpecies, EDIT_FLAG_GEOMETRY, eaUnlockedCostumes, true, bUnlockAll, true, false);
					}
				}
			}
		}
		return true;
	}
	return false;
}


bool costumeTailor_SetPartMaterial(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, PCBoneDef **eaFindBones, const char *pcMatName,
								   PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, Guild *pGuild, bool bUnlockAll, bool bMirrorMode)
{
	PCMaterialDef *pMat = RefSystem_ReferentFromString(g_hCostumeMaterialDict, pcMatName);

	if (!stricmp(pcMatName, "None") && !pMat) {
		pcMatName = NULL;
	}

	if (!pPart) {
		PCSkeletonDef *pSkel = GET_REF(pCostume->hSkeleton);
		int j;

		if (pSkel) {
			for (j = eaSize(&pSkel->eaBoneGroups)-1; j >= 0; --j)
			{
				PCBoneGroup *pBoneGroup = pSkel->eaBoneGroups[j];
				if (eaSize(&pBoneGroup->eaBoneInGroup) && pBoneGroup->pcName && !stricmp(pBoneGroup->pcName,"CommonMaterialsDef"))
				{
					pPart = costumeTailor_GetPartByBone(pCostume, GET_REF(pBoneGroup->eaBoneInGroup[0]->hBone), NULL);
					break;
				}
			}
		}
	}

	if (!pPart) {
		// Find a part with a material with link_all and change that material
		// Use provided bone list (if present), otherwise search all parts
		NOCONST(PCPart) *pTempPart;
		int i;

		if (eaSize(&eaFindBones)) {
			for (i = eaSize(&eaFindBones)-1; i >= 0; --i) {
				pTempPart = costumeTailor_GetPartByBone(pCostume, eaFindBones[i], NULL);
				if (!pTempPart) continue;
				if (pTempPart->eMaterialLink == kPCColorLink_All) {
					pPart = pTempPart;
					break;
				}
			}
		} else {
			for (i = 0; i<eaSize(&pCostume->eaParts); ++i) {
				pTempPart = pCostume->eaParts[i];
				if (pTempPart->eMaterialLink == kPCColorLink_All) {
					pPart = pTempPart;
					break;
				}
			}
		}
	}

	if (pPart && (pMat || !pcMatName)) {
		PCGeometryDef *pGeo = GET_REF(pPart->hGeoDef);
		PCBoneDef *pBone = GET_REF(pPart->hBoneDef);
		NOCONST(PCPart) *pRealPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);

		if (!pBone) {
			return false;
		}
		if (!stricmp(pBone->pcName,"None")) {
			return false;
		}
		assert(pRealPart);

		// Set value accounting for cloth
		if (pGeo && pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer) {
			if (pPart->eEditMode == kPCEditMode_Front || pPart->eEditMode == kPCEditMode_Back) {
				// If only front or back, then apply as per rule
				if (pPart->eEditMode == kPCEditMode_Front) {
					SET_HANDLE_FROM_STRING(g_hCostumeMaterialDict, pcMatName, pRealPart->hMatDef);
				} else {
					SET_HANDLE_FROM_STRING(g_hCostumeMaterialDict, pcMatName, pRealPart->pClothLayer->hMatDef);
				}
			} else {
				// Always set both sides
				SET_HANDLE_FROM_STRING(g_hCostumeMaterialDict, pcMatName, pRealPart->hMatDef);
				SET_HANDLE_FROM_STRING(g_hCostumeMaterialDict, pcMatName, pRealPart->pClothLayer->hMatDef);
			}
			costumeTailor_PickValidPartValues(pCostume, pRealPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, pGuild);
		} else {
			// If not cloth, then simply apply
			SET_HANDLE_FROM_STRING(g_hCostumeMaterialDict, pcMatName, pPart->hMatDef);
			costumeTailor_PickValidPartValues(pCostume, pPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, pGuild);
		}

		// Apply mirror change if appropriate
		if (bMirrorMode && pPart->eEditMode == kPCEditMode_Both) {
			NOCONST(PCPart) *pMirrorPart = costumeTailor_GetMirrorPart(pCostume, pPart);
			if (pMirrorPart) {
				costumeTailor_CopyToMirrorPart(pCostume, pPart, pMirrorPart, pSpecies, EDIT_FLAG_MATERIAL, eaUnlockedCostumes, true, bUnlockAll, false);
			}
		}

		return true;
	}
	return false;
}


bool costumeTailor_SetPartTexturePattern(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, const char *pcPattern,
								   PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, Guild *pGuild, bool bUnlockAll, bool bMirrorMode)
{
	NOCONST(PCPart) *pRealPart;
	PCGeometryDef *pGeo;
	PCTextureDef *pPattern;
	PCBoneDef *pBone = pPart ? GET_REF(pPart->hBoneDef) : NULL;
	int i;

	if (pGuild && pBone && pBone->bIsGuildEmblemBone) {
		if (stricmp(pcPattern, "GuildEmblem") == 0) {
			i = -1;
			if (pGuild->pcEmblem && *pGuild->pcEmblem) {
				for (i = eaSize(&g_GuildEmblems.eaEmblems)-1; i >= 0; --i) {
					if (stricmp(REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[i]->hTexture),pGuild->pcEmblem) == 0) {
						break;
					}
				}
			}
			if ((i >= 0 && !g_GuildEmblems.eaEmblems[i]->bFalse) || ((!pGuild->pcEmblem) || (!*pGuild->pcEmblem))) {
				PCRegion *pRegion = pBone ? GET_REF(pBone->hRegion) : NULL;
				PCCategory *pCategory = pRegion ? costumeTailor_GetCategoryForRegion((PlayerCostume*)pCostume, pRegion) : NULL;
				PCGeometryDef **eaGeometries = NULL, *pGeometry = NULL;
				PCMaterialDef **eaMaterials = NULL, *pMaterial = NULL;

				pRealPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);

				costumeTailor_GetValidGeos(pCostume, GET_REF(pCostume->hSkeleton), pBone, pCategory, pSpecies, NULL, &eaGeometries, false, false, true, true);
				if (eaSize(&eaGeometries)) {
					pGeometry = eaGeometries[0];
					if ((stricmp(pGeometry->pcName, "None") == 0) && eaSize(&eaGeometries) > 1) {
						pGeometry = eaGeometries[1];
					}
					SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, pGeometry, pRealPart->hGeoDef);
				}
				eaDestroy(&eaGeometries);

				costumeTailor_GetValidMaterials(pCostume, pGeometry, pSpecies, NULL, NULL, NULL, &eaMaterials, false, true, true);
				if (eaSize(&eaMaterials)) {
					pMaterial = eaMaterials[0];
					if ((stricmp(pMaterial->pcName, "None") == 0) && eaSize(&eaMaterials) > 1) {
						pMaterial = eaMaterials[1];
					}
					SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, pMaterial, pRealPart->hMatDef);
				}
				eaDestroy(&eaMaterials);

				if (pGuild->pcEmblem && *pGuild->pcEmblem) {
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pGuild->pcEmblem, pRealPart->hPatternTexture);
				} else {
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pRealPart->hPatternTexture);
				}
				if (pGuild->pcEmblem2 && *pGuild->pcEmblem2) {
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pGuild->pcEmblem2, pRealPart->pMovableTexture->hMovableTexture);
				} else {
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pRealPart->pMovableTexture->hMovableTexture);
				}
				if (pGuild->pcEmblem3 && *pGuild->pcEmblem3) {
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pGuild->pcEmblem3, pRealPart->hDetailTexture);
				} else {
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pRealPart->hDetailTexture);
				}

				if (!pRealPart->pTextureValues) {
					pRealPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
				}
				if (pRealPart->pTextureValues) {
					pRealPart->pTextureValues->fPatternValue = pGuild->fEmblemRotation;
				}
				if (!pRealPart->pMovableTexture) {
					pRealPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
				}
				if (pRealPart->pMovableTexture) {
					pRealPart->pMovableTexture->fMovableRotation = pGuild->fEmblem2Rotation;
					pRealPart->pMovableTexture->fMovableX = pGuild->fEmblem2X;
					pRealPart->pMovableTexture->fMovableY = pGuild->fEmblem2Y;
					pRealPart->pMovableTexture->fMovableScaleX = pGuild->fEmblem2ScaleX;
					pRealPart->pMovableTexture->fMovableScaleY = pGuild->fEmblem2ScaleY;
				}
				*((U32*)pRealPart->color0) = pGuild->iEmblem2Color0;
				*((U32*)pRealPart->color1) = pGuild->iEmblem2Color1;
				*((U32*)pRealPart->color2) = pGuild->iEmblemColor0;
				*((U32*)pRealPart->color3) = pGuild->iEmblemColor1;

				return true;
			}

		} else {
			// Get here if on guild bone but not the guild emblem pattern
			// Wipes out the emblem and sets things back to defaults
			pRealPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);
			if (costumeTailor_PartHasGuildEmblem(pRealPart, pGuild)) {
				pPattern = RefSystem_ReferentFromString(g_hCostumeTextureDict, pcPattern);

				if (pPattern && pPattern->pValueOptions) {
					if (!pRealPart->pTextureValues) {
						pRealPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
					}
					if (pRealPart->pTextureValues) {
						pRealPart->pTextureValues->fPatternValue = pPattern->pValueOptions->fValueDefault;
					}
				} else if (pRealPart->pTextureValues) {
					pRealPart->pTextureValues->fPatternValue = 0.0f;
				}

				if (pPattern && pPattern->pMovableOptions) {
					if (!pRealPart->pMovableTexture) {
						pRealPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
					}
					if (pRealPart->pMovableTexture) {
						pRealPart->pMovableTexture->fMovableRotation = pPattern->pMovableOptions->fMovableDefaultRotation;
						pRealPart->pMovableTexture->fMovableX = pPattern->pMovableOptions->fMovableDefaultX;
						pRealPart->pMovableTexture->fMovableY = pPattern->pMovableOptions->fMovableDefaultY;
						pRealPart->pMovableTexture->fMovableScaleX = pPattern->pMovableOptions->fMovableDefaultScaleX;
						pRealPart->pMovableTexture->fMovableScaleY = pPattern->pMovableOptions->fMovableDefaultScaleY;
					}
				} else if (pRealPart->pMovableTexture) {
					pRealPart->pMovableTexture->fMovableRotation = 0;
					pRealPart->pMovableTexture->fMovableX = 0;
					pRealPart->pMovableTexture->fMovableY = 0;
					pRealPart->pMovableTexture->fMovableScaleX = 1;
					pRealPart->pMovableTexture->fMovableScaleY = 1;
				}

				if (pPattern && pPattern->pColorOptions) {
					if (pPattern->pColorOptions->bHasDefaultColor0) {
						*((U32*)pRealPart->color0) = *((U32*)pPattern->pColorOptions->uDefaultColor0);
					}
					if (pPattern->pColorOptions->bHasDefaultColor1) {
						*((U32*)pRealPart->color1) = *((U32*)pPattern->pColorOptions->uDefaultColor1);
					}
					if (pPattern->pColorOptions->bHasDefaultColor2) {
						*((U32*)pRealPart->color2) = *((U32*)pPattern->pColorOptions->uDefaultColor2);
					}
					if (pPattern->pColorOptions->bHasDefaultColor3) {
						*((U32*)pRealPart->color3) = *((U32*)pPattern->pColorOptions->uDefaultColor3);
					}
				} else {
					*((U32*)pRealPart->color0) = 0;
					*((U32*)pRealPart->color1) = 0;
					*((U32*)pRealPart->color2) = 0;
					*((U32*)pRealPart->color3) = 0;
				}

				if (costumeTailor_IsBoneRequired(pCostume, pBone, pSpecies) || stricmp("None", pcPattern)) {
					PCMaterialDef *pMaterial = GET_REF(pRealPart->hMatDef);
					if (pMaterial) {
						if (!pMaterial->bRequiresDetail) {
							SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pRealPart->hDetailTexture);
						}
						if (!pMaterial->bRequiresMovable) {
							SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pRealPart->pMovableTexture->hMovableTexture);
						}
					} else {
						SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pRealPart->hDetailTexture);
						SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pRealPart->pMovableTexture->hMovableTexture);
					}
				} else {
					SET_HANDLE_FROM_STRING(g_hCostumeGeometryDict, "None", pRealPart->hGeoDef);
					SET_HANDLE_FROM_STRING(g_hCostumeMaterialDict, "None", pRealPart->hMatDef);
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pRealPart->hPatternTexture);
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pRealPart->hDetailTexture);
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "None", pRealPart->pMovableTexture->hMovableTexture);

					return true;
				}
			} else {
				if (!GET_REF(pRealPart->hGeoDef)) {
					return false;
				}
			}
		}
	}

	// Get here if the texture is not the guild emblem

	pPattern = RefSystem_ReferentFromString(g_hCostumeTextureDict, pcPattern);
	if (!pPattern && (stricmp(pcPattern, "None") == 0)) {
		pcPattern = NULL;
	}
	if (pPart && (pPattern || !pcPattern)) {
		if (!pBone) {
			return false;
		}
		if (stricmp(pBone->pcName,"None") == 0) {
			return false;
		}

		pRealPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);
		assert(pRealPart);

		pGeo = GET_REF(pRealPart->hGeoDef);
		assert(pGeo);

		// Set value accounting for cloth
		if (pGeo && pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer) {
			if ((pPart->eEditMode == kPCEditMode_Front) ||
				(pPart->eEditMode == kPCEditMode_Back)) {
				// If only front or back, then apply as per rule
				if (pPart->eEditMode == kPCEditMode_Front) {
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcPattern, pPart->hPatternTexture);
				} else {
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcPattern, pRealPart->pClothLayer->hPatternTexture);
				}
			} else {
				// If both, then get real part and apply to both
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcPattern, pRealPart->hPatternTexture);
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcPattern, pRealPart->pClothLayer->hPatternTexture);
			}
			costumeTailor_PickValidPartValues(pCostume, pRealPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, pGuild);
		} else {
			// If not cloth, then simply apply
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcPattern, pPart->hPatternTexture);
			costumeTailor_PickValidPartValues(pCostume, pPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, pGuild);
		}

		// Apply mirror change if appropriate
		if (bMirrorMode && pRealPart->eEditMode == kPCEditMode_Both) {
			NOCONST(PCPart) *pMirrorPart = costumeTailor_GetMirrorPart(pCostume, pRealPart);
			if (pMirrorPart) {
				costumeTailor_CopyToMirrorPart(pCostume, pRealPart, pMirrorPart, pSpecies, EDIT_FLAG_TEXTURE1, eaUnlockedCostumes, true, bUnlockAll, false);
			}
		}

		//// Apply bone group change if appropriate
		//if (bGroupMode && pRealPart->iBoneGroupIndex >= 0) {
		//	NOCONST(PCPart) *pGroupPart = NULL;
		//	PCSkeletonDef *pSkel = GET_REF(pCostume->hSkeleton);
		//	int i;
		//
		//	if (pSkel) {
		//		for (i = eaSize(&pSkel->eaBoneGroups[pRealPart->iBoneGroupIndex]->eaBoneInGroup)-1; i >= 0; --i) {
		//			pGroupPart = costumeTailor_GetPartByBone(pCostume, GET_REF(pSkel->eaBoneGroups[pRealPart->iBoneGroupIndex]->eaBoneInGroup[i]->hBone), NULL);
		//			if (pGroupPart) {
		//				costumeTailor_CopyToGroupPart(pCostume, pRealPart, pGroupPart, pSpecies, EDIT_FLAG_TEXTURE1, eaUnlockedCostumes, true, bUnlockAll, true);
		//			}
		//		}
		//	}
		//}

		return true;
	}
	return false;
}


bool costumeTailor_SetPartTextureDetail(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, const char *pcDetail,
								   PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, Guild *pGuild, bool bUnlockAll, bool bMirrorMode)
{
	NOCONST(PCPart) *pRealPart;
	PCGeometryDef *pGeo;
	PCTextureDef *pDetail;

	pDetail = RefSystem_ReferentFromString(g_hCostumeTextureDict, pcDetail);
	if (!stricmp(pcDetail, "None") && !pDetail) {
		pcDetail = NULL;
	}

	if (pPart && (pDetail || !pcDetail)) {
		if (!GET_REF(pPart->hBoneDef)) {
			return false;
		}
		if (stricmp(GET_REF(pPart->hBoneDef)->pcName,"None") == 0) {
			return false;
		}

		pRealPart = costumeTailor_GetPartByBone(pCostume, GET_REF(pPart->hBoneDef), NULL);
		assert(pRealPart);

		pGeo = GET_REF(pRealPart->hGeoDef);
		assert(pGeo);

		// Set value accounting for cloth
		if (pGeo && pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer) {
			if ((pPart->eEditMode == kPCEditMode_Front) ||
				(pPart->eEditMode == kPCEditMode_Back)) {
				// If only front or back, then apply as per rule
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcDetail, pPart->hDetailTexture);
			} else {
				// If both, then get real part and apply to both
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcDetail, pRealPart->hDetailTexture);
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcDetail, pRealPart->pClothLayer->hDetailTexture);
			}
			costumeTailor_PickValidPartValues(pCostume, pRealPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, pGuild);
		} else {
			// If not cloth, then simply apply
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcDetail, pPart->hDetailTexture);
			costumeTailor_PickValidPartValues(pCostume, pPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, pGuild);
		}

		// Apply mirror change if appropriate
		if (bMirrorMode && pRealPart->eEditMode == kPCEditMode_Both) {
			NOCONST(PCPart) *pMirrorPart = costumeTailor_GetMirrorPart(pCostume, pRealPart);
			if (pMirrorPart) {
				costumeTailor_CopyToMirrorPart(pCostume, pRealPart, pMirrorPart, pSpecies, EDIT_FLAG_TEXTURE2, eaUnlockedCostumes, true, bUnlockAll, false);
			}
		}

		//// Apply bone group change if appropriate
		//if (bGroupMode && pRealPart->iBoneGroupIndex >= 0) {
		//	NOCONST(PCPart) *pGroupPart = NULL;
		//	PCSkeletonDef *pSkel = GET_REF(pCostume->hSkeleton);
		//	int i;
		//
		//	if (pSkel) {
		//		for (i = eaSize(&pSkel->eaBoneGroups[pRealPart->iBoneGroupIndex]->eaBoneInGroup)-1; i >= 0; --i) {
		//			pGroupPart = costumeTailor_GetPartByBone(pCostume, GET_REF(pSkel->eaBoneGroups[pRealPart->iBoneGroupIndex]->eaBoneInGroup[i]->hBone), NULL);
		//			if (pGroupPart) {
		//				costumeTailor_CopyToGroupPart(pCostume, pRealPart, pGroupPart, pSpecies, EDIT_FLAG_TEXTURE2, eaUnlockedCostumes, true, bUnlockAll, true);
		//			}
		//		}
		//	}
		//}

		return true;
	}
	return false;
}


bool costumeTailor_SetPartTextureSpecular(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, const char *pcSpecular,
								   PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, Guild *pGuild, bool bUnlockAll, bool bMirrorMode)
{
	NOCONST(PCPart) *pRealPart;
	PCGeometryDef *pGeo;
	PCTextureDef *pSpecular;

	pSpecular = RefSystem_ReferentFromString(g_hCostumeTextureDict, pcSpecular);
	if (!stricmp(pcSpecular, "None") && !pSpecular) {
		pcSpecular = NULL;
	}

	if (pPart && (pSpecular || !pcSpecular)) {
		if (!GET_REF(pPart->hBoneDef)) {
			return false;
		}
		if (stricmp(GET_REF(pPart->hBoneDef)->pcName,"None") == 0) {
			return false;
		}

		pRealPart = costumeTailor_GetPartByBone(pCostume, GET_REF(pPart->hBoneDef), NULL);
		assert(pRealPart);

		pGeo = GET_REF(pRealPart->hGeoDef);
		assert(pGeo);

		// Set value accounting for cloth
		if (pGeo && pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer) {
			if ((pPart->eEditMode == kPCEditMode_Front) ||
				(pPart->eEditMode == kPCEditMode_Back)) {
				// If only front or back, then apply as per rule
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcSpecular, pPart->hSpecularTexture);
			} else {
				// If both, then get real part and apply to both
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcSpecular, pRealPart->hSpecularTexture);
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcSpecular, pRealPart->pClothLayer->hSpecularTexture);
			}
			costumeTailor_PickValidPartValues(pCostume, pRealPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, pGuild);
		} else {
			// If not cloth, then simply apply
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcSpecular, pPart->hSpecularTexture);
			costumeTailor_PickValidPartValues(pCostume, pPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, pGuild);
		}

		// Apply mirror change if appropriate
		if (bMirrorMode && pRealPart->eEditMode == kPCEditMode_Both) {
			NOCONST(PCPart) *pMirrorPart = costumeTailor_GetMirrorPart(pCostume, pRealPart);
			if (pMirrorPart) {
				costumeTailor_CopyToMirrorPart(pCostume, pRealPart, pMirrorPart, pSpecies, EDIT_FLAG_TEXTURE3, eaUnlockedCostumes, true, bUnlockAll, false);
			}
		}

		//// Apply bone group change if appropriate
		//if (bGroupMode && pRealPart->iBoneGroupIndex >= 0) {
		//	NOCONST(PCPart) *pGroupPart = NULL;
		//	PCSkeletonDef *pSkel = GET_REF(pCostume->hSkeleton);
		//	int i;
		//
		//	if (pSkel) {
		//		for (i = eaSize(&pSkel->eaBoneGroups[pRealPart->iBoneGroupIndex]->eaBoneInGroup)-1; i >= 0; --i) {
		//			pGroupPart = costumeTailor_GetPartByBone(pCostume, GET_REF(pSkel->eaBoneGroups[pRealPart->iBoneGroupIndex]->eaBoneInGroup[i]->hBone), NULL);
		//			if (pGroupPart) {
		//				costumeTailor_CopyToGroupPart(pCostume, pRealPart, pGroupPart, pSpecies, EDIT_FLAG_TEXTURE3, eaUnlockedCostumes, true, bUnlockAll, true);
		//			}
		//		}
		//	}
		//}

		return true;
	}
	return false;
}


bool costumeTailor_SetPartTextureDiffuse(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, const char *pcDiffuse,
								   PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, Guild *pGuild, bool bUnlockAll, bool bMirrorMode)
{
	NOCONST(PCPart) *pRealPart;
	PCGeometryDef *pGeo;
	PCTextureDef *pDiffuse;

	pDiffuse = RefSystem_ReferentFromString(g_hCostumeTextureDict, pcDiffuse);
	if (!stricmp(pcDiffuse, "None") && !pDiffuse) {
		pcDiffuse = NULL;
	}

	if (pPart && (pDiffuse || !pcDiffuse)) {
		if (!GET_REF(pPart->hBoneDef)) {
			return false;
		}
		if (stricmp(GET_REF(pPart->hBoneDef)->pcName,"None") == 0) {
			return false;
		}

		pRealPart = costumeTailor_GetPartByBone(pCostume, GET_REF(pPart->hBoneDef), NULL);
		assert(pRealPart);

		pGeo = GET_REF(pRealPart->hGeoDef);
		assert(pGeo);

		// Set value accounting for cloth
		if (pGeo && pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer) {
			if ((pPart->eEditMode == kPCEditMode_Front) ||
				(pPart->eEditMode == kPCEditMode_Back)) {
				// If only front or back, then apply as per rule
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcDiffuse, pPart->hDiffuseTexture);
			} else {
				// If both, then get real part and apply to both
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcDiffuse, pRealPart->hDiffuseTexture);
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcDiffuse, pRealPart->pClothLayer->hDiffuseTexture);
			}
			costumeTailor_PickValidPartValues(pCostume, pRealPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, pGuild);
		} else {
			// If not cloth, then simply apply
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcDiffuse, pPart->hDiffuseTexture);
			costumeTailor_PickValidPartValues(pCostume, pPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, pGuild);
		}

		// Apply mirror change if appropriate
		if (bMirrorMode && pRealPart->eEditMode == kPCEditMode_Both) {
			NOCONST(PCPart) *pMirrorPart = costumeTailor_GetMirrorPart(pCostume, pRealPart);
			if (pMirrorPart) {
				costumeTailor_CopyToMirrorPart(pCostume, pRealPart, pMirrorPart, pSpecies, EDIT_FLAG_TEXTURE4, eaUnlockedCostumes, true, bUnlockAll, false);
			}
		}

		//// Apply bone group change if appropriate
		//if (bGroupMode && pRealPart->iBoneGroupIndex >= 0) {
		//	NOCONST(PCPart) *pGroupPart = NULL;
		//	PCSkeletonDef *pSkel = GET_REF(pCostume->hSkeleton);
		//	int i;
		//
		//	if (pSkel) {
		//		for (i = eaSize(&pSkel->eaBoneGroups[pRealPart->iBoneGroupIndex]->eaBoneInGroup)-1; i >= 0; --i) {
		//			pGroupPart = costumeTailor_GetPartByBone(pCostume, GET_REF(pSkel->eaBoneGroups[pRealPart->iBoneGroupIndex]->eaBoneInGroup[i]->hBone), NULL);
		//			if (pGroupPart) {
		//				costumeTailor_CopyToGroupPart(pCostume, pRealPart, pGroupPart, pSpecies, EDIT_FLAG_TEXTURE4, eaUnlockedCostumes, true, bUnlockAll, true);
		//			}
		//		}
		//	}
		//}

		return true;
	}
	return false;
}


bool costumeTailor_SetPartTextureMovable(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, const char *pcMovable,
								   PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, Guild *pGuild, bool bUnlockAll, bool bMirrorMode)
{
	NOCONST(PCPart) *pRealPart;
	PCGeometryDef *pGeo;
	PCTextureDef *pMovable;

	pMovable = RefSystem_ReferentFromString(g_hCostumeTextureDict, pcMovable);
	if (!stricmp(pcMovable, "None") && !pMovable) {
		pcMovable = NULL;
	}

	if (pPart && (pMovable || !pcMovable)) {
		if (!GET_REF(pPart->hBoneDef)) {
			return false;
		}
		if (stricmp(GET_REF(pPart->hBoneDef)->pcName,"None") == 0) {
			return false;
		}

		pRealPart = costumeTailor_GetPartByBone(pCostume, GET_REF(pPart->hBoneDef), NULL);
		assert(pRealPart);

		pGeo = GET_REF(pRealPart->hGeoDef);
		assert(pGeo);

		// Set value accounting for cloth
		if (pGeo && pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer) {
			if ((pPart->eEditMode == kPCEditMode_Front) ||
				(pPart->eEditMode == kPCEditMode_Back)) {
				// If only front or back, then apply as per rule
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcMovable, pPart->pMovableTexture->hMovableTexture);
			} else {
				// If both, then get real part and apply to both
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcMovable, pRealPart->pMovableTexture->hMovableTexture);
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcMovable, pRealPart->pClothLayer->pMovableTexture->hMovableTexture);
			}
			costumeTailor_PickValidPartValues(pCostume, pRealPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, pGuild);
		} else {
			// If not cloth, then simply apply
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcMovable, pPart->pMovableTexture->hMovableTexture);
			costumeTailor_PickValidPartValues(pCostume, pPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, pGuild);
		}

		// Apply mirror change if appropriate
		if (bMirrorMode && pRealPart->eEditMode == kPCEditMode_Both) {
			NOCONST(PCPart) *pMirrorPart = costumeTailor_GetMirrorPart(pCostume, pRealPart);
			if (pMirrorPart) {
				costumeTailor_CopyToMirrorPart(pCostume, pRealPart, pMirrorPart, pSpecies, EDIT_FLAG_TEXTURE5, eaUnlockedCostumes, true, bUnlockAll, false);
			}
		}

		//// Apply bone group change if appropriate
		//if (bGroupMode && pRealPart->iBoneGroupIndex >= 0) {
		//	NOCONST(PCPart) *pGroupPart = NULL;
		//	PCSkeletonDef *pSkel = GET_REF(pCostume->hSkeleton);
		//	int i;
		//
		//	if (pSkel) {
		//		for (i = eaSize(&pSkel->eaBoneGroups[pRealPart->iBoneGroupIndex]->eaBoneInGroup)-1; i >= 0; --i) {
		//			pGroupPart = costumeTailor_GetPartByBone(pCostume, GET_REF(pSkel->eaBoneGroups[pRealPart->iBoneGroupIndex]->eaBoneInGroup[i]->hBone), NULL);
		//			if (pGroupPart) {
		//				costumeTailor_CopyToGroupPart(pCostume, pRealPart, pGroupPart, pSpecies, EDIT_FLAG_TEXTURE5, eaUnlockedCostumes, true, bUnlockAll, true);
		//			}
		//		}
		//	}
		//}

		return true;
	}
	return false;
}


void costumeTailor_SetPartDefaultTextureValues(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, PCTextureDef *pTex, PCTextureType eTexType)
{
	if (pTex && pTex->pValueOptions) {
		F32 fMin = pTex->pValueOptions->fValueMin;
		F32 fMax = pTex->pValueOptions->fValueMax;
		costumeTailor_GetTextureValueMinMax((PCPart*)pPart, pTex, pSpecies, &fMin, &fMax);
		if (fMax > fMin && pTex->pValueOptions->fValueDefault >= fMin && pTex->pValueOptions->fValueDefault <= fMax) {
			if (!pPart->pTextureValues) {
				pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
			}
			if (pPart->pTextureValues) {
				if (eTexType & kPCTextureType_Pattern) {
					pPart->pTextureValues->fPatternValue = (((pTex->pValueOptions->fValueDefault - fMin) * 200.0f)/(fMax - fMin)) - 100.0f;
				}
				if (eTexType & kPCTextureType_Detail) {
					pPart->pTextureValues->fDetailValue = (((pTex->pValueOptions->fValueDefault - fMin) * 200.0f)/(fMax - fMin)) - 100.0f;
				}
				if (eTexType & kPCTextureType_Specular) {
					pPart->pTextureValues->fSpecularValue = (((pTex->pValueOptions->fValueDefault - fMin) * 200.0f)/(fMax - fMin)) - 100.0f;
				}
				if (eTexType & kPCTextureType_Diffuse) {
					pPart->pTextureValues->fDiffuseValue = (((pTex->pValueOptions->fValueDefault - fMin) * 200.0f)/(fMax - fMin)) - 100.0f;
				}
			}
			if (pPart->pMovableTexture) {
				if (eTexType & kPCTextureType_Movable) {
					pPart->pMovableTexture->fMovableValue = (((pTex->pValueOptions->fValueDefault - fMin) * 200.0f)/(fMax - fMin)) - 100.0f;
				}
			}
		}
	}

	if (pTex && pTex->pMovableOptions) {
		F32 fMovableMinX, fMovableMaxX, fMovableMinY, fMovableMaxY;
		F32 fMovableMinScaleX, fMovableMaxScaleX, fMovableMinScaleY, fMovableMaxScaleY;
		bool bMovableCanEditPosition, bMovableCanEditRotation, bMovableCanEditScale;

		costumeTailor_GetTextureMovableValues((PCPart*)pPart, pTex, pSpecies,
			&fMovableMinX, &fMovableMaxX, &fMovableMinY, &fMovableMaxY,
			&fMovableMinScaleX, &fMovableMaxScaleX, &fMovableMinScaleY, &fMovableMaxScaleY,
			&bMovableCanEditPosition, &bMovableCanEditRotation, &bMovableCanEditScale);

		if (!pPart->pMovableTexture) {
			pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
		}
		if (pPart->pMovableTexture) {
			if (fMovableMaxX > fMovableMinX && pTex->pMovableOptions->fMovableDefaultX >= fMovableMinX && pTex->pMovableOptions->fMovableDefaultX <= fMovableMaxX) {
				pPart->pMovableTexture->fMovableX = (((pTex->pMovableOptions->fMovableDefaultX - fMovableMinX) * 200.0f)/(fMovableMaxX - fMovableMinX)) - 100.0f;
			}
			if (fMovableMaxY > fMovableMinY && pTex->pMovableOptions->fMovableDefaultY >= fMovableMinY && pTex->pMovableOptions->fMovableDefaultY <= fMovableMaxY) {
				pPart->pMovableTexture->fMovableY = (((pTex->pMovableOptions->fMovableDefaultY - fMovableMinY) * 200.0f)/(fMovableMaxY - fMovableMinY)) - 100.0f;
			}
			if (fMovableMaxScaleX > fMovableMinScaleX && pTex->pMovableOptions->fMovableDefaultScaleX >= fMovableMinScaleX && pTex->pMovableOptions->fMovableDefaultScaleX <= fMovableMaxScaleX) {
				pPart->pMovableTexture->fMovableScaleX = (((pTex->pMovableOptions->fMovableDefaultScaleX - fMovableMinScaleX) * 100.0f)/(fMovableMaxScaleX - fMovableMinScaleX));
			}
			if (fMovableMaxScaleY > fMovableMinScaleY && pTex->pMovableOptions->fMovableDefaultScaleY >= fMovableMinScaleY && pTex->pMovableOptions->fMovableDefaultScaleY <= fMovableMaxScaleY) {
				pPart->pMovableTexture->fMovableScaleY = (((pTex->pMovableOptions->fMovableDefaultScaleY - fMovableMinScaleY) * 100.0f)/(fMovableMaxScaleY - fMovableMinScaleY));
			}
			pPart->pMovableTexture->fMovableRotation = pTex->pMovableOptions->fMovableDefaultRotation;
		}
	}
}


void costumeTailor_SetPartDefaultTextureColors(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, PCTextureDef *pTex)
{
	if (pTex && pTex->pColorOptions) {
		if (pTex->pColorOptions->bHasDefaultColor0) {
			*((U32*)pPart->color0) = *((U32*)pTex->pColorOptions->uDefaultColor0);
		}
		if (pTex->pColorOptions->bHasDefaultColor1) {
			*((U32*)pPart->color1) = *((U32*)pTex->pColorOptions->uDefaultColor1);
		}
		if (pTex->pColorOptions->bHasDefaultColor2) {
			*((U32*)pPart->color2) = *((U32*)pTex->pColorOptions->uDefaultColor2);
		}
		if (pTex->pColorOptions->bHasDefaultColor3) {
			*((U32*)pPart->color3) = *((U32*)pTex->pColorOptions->uDefaultColor3);
		}
	}
}


// Returns true if anything changes
bool costumeTailor_SetBodyScale(NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies, int iIndex, F32 fBodyScale, PCSlotType *pSlotType)
{
	PCSkeletonDef *pSkel = GET_REF(pCostume->hSkeleton);
	if (pSkel) {
		F32 fMinScale = 0;
		F32 fMaxScale = 100;

		if (pSpecies && eaSize(&pSkel->eaBodyScaleInfo) > iIndex) {
			F32 fMin, fMax;
			assert(pSkel->eaBodyScaleInfo);
			if(costumeTailor_GetOverrideBodyScale(pSkel, pSkel->eaBodyScaleInfo[iIndex]->pcName, pSpecies, pSlotType, &fMin, &fMax)) {
				fMinScale = fMin;
				fMaxScale = fMax;
				if (pCostume->eafBodyScales && (eafSize(&pCostume->eafBodyScales) > iIndex)) {
					pCostume->eafBodyScales[iIndex] = CLAMP(fBodyScale + fMinScale, fMinScale, fMaxScale);
					return true;
				}
				return false;
			}
		}

		if (pSkel->eafPlayerMinBodyScales && (eafSize(&pSkel->eafPlayerMinBodyScales) > iIndex)) {
			fMinScale = pSkel->eafPlayerMinBodyScales[iIndex];
		}
		if (pSkel->eafPlayerMaxBodyScales && (eafSize(&pSkel->eafPlayerMaxBodyScales) > iIndex)) {
			fMaxScale = pSkel->eafPlayerMaxBodyScales[iIndex];
		}
		if (pCostume->eafBodyScales && (eafSize(&pCostume->eafBodyScales) > iIndex)) {
			pCostume->eafBodyScales[iIndex] = CLAMP(fBodyScale + fMinScale, fMinScale, fMaxScale);
			return true;
		}
	}
	return false;
}


// Returns true if anything changes
bool costumeTailor_SetBodyScaleByName(NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies, const char *pcScaleName, F32 fBodyScale, PCSlotType *pSlotType)
{
	PCSkeletonDef *pSkel = GET_REF(pCostume->hSkeleton);
	if (pSkel) {
		int iIndex;
		for (iIndex = 0; iIndex < eaSize(&pSkel->eaBodyScaleInfo); iIndex++) {
			if (stricmp(pSkel->eaBodyScaleInfo[iIndex]->pcName, pcScaleName) == 0) {
				return costumeTailor_SetBodyScale(pCostume, pSpecies, iIndex, fBodyScale, pSlotType);
			}
		}
	}
	return false;
}

int costumeTailor_FindBodyScaleInfoIndexByName( PlayerCostume* costume, const char* name )
{
	PCSkeletonDef* pSkel = GET_REF( costume->hSkeleton );
	int i;
	name = allocAddString( name );

	if( !pSkel ) {
		return -1;
	}
	for( i = 0; i < eaSize( &pSkel->eaBodyScaleInfo ); ++i ) {
		if( pSkel->eaBodyScaleInfo[i]->pcName == name ) {
			return i;
		}
	}
	return -1;
}


const PCScaleValue* costumeTailor_FindScaleValueByName( PlayerCostume* costume, const char* name )
{
	int it;
	for( it = eaSize( &costume->eaScaleValues ) - 1; it >= 0; --it ) {
		if( stricmp( costume->eaScaleValues[ it ]->pcScaleName, name ) == 0 ) {
			return costume->eaScaleValues[ it ];
		}
	}

	return NULL;
}

NOCONST(PCScaleValue)* costumeTailor_FindScaleValueByNameNoConst( NOCONST(PlayerCostume)* costume, const char* name )
{
	const PCScaleValue* value = costumeTailor_FindScaleValueByName( CONTAINER_RECONST( PlayerCostume, costume ), name );
	return CONTAINER_NOCONST( PCScaleValue, value );
}

const PCScaleInfoGroup* costumeTailor_FindScaleGroupByName( const PCSkeletonDef* pSkel, const char* name )
{
	int it;
	for( it = 0; it != eaSize( &pSkel->eaScaleInfoGroups ); ++it ) {
		if( stricmp( pSkel->eaScaleInfoGroups[ it ]->pcName, name ) == 0 ) {
			return pSkel->eaScaleInfoGroups[ it ];
		}
	}

	return NULL;
}

const PCScaleInfo* costumeTailor_FindScaleInfoByName( const PCSkeletonDef* pSkel, const char* name )
{
	int groupIt;
	int scaleIt;

	name = allocFindString( name );
	for( groupIt = 0; groupIt != eaSize( &pSkel->eaScaleInfoGroups ); ++groupIt ) {
		PCScaleInfoGroup* group = pSkel->eaScaleInfoGroups[ groupIt ];
		for( scaleIt = 0; scaleIt != eaSize( &group->eaScaleInfo ); ++scaleIt ) {
			PCScaleInfo* scaleInfo = group->eaScaleInfo[ scaleIt ];
			if( name == scaleInfo->pcName ) {
				return scaleInfo;
			}
		}
	}
	for( scaleIt = 0; scaleIt != eaSize( &pSkel->eaScaleInfo ); ++scaleIt ) {
		PCScaleInfo* scaleInfo = pSkel->eaScaleInfo[ scaleIt ];
		if( name == scaleInfo->pcName ) {
			return scaleInfo;
		}
	}

	return NULL;
}
