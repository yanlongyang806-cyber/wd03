/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#include "Character.h"
#include "CostumeCommon.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonGenerate.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "dynAnimInterface.h"
#include "dynCloth.h"
#include "dynFxInterface.h"
#include "dynSkeleton.h"
#include "Entity.h"
#include "EntityAttach.h"
#include "EntityLib.h"
#include "error.h"
#include "Estring.h"
#include "GfxMaterials.h"
#include "GlobalTypes.h"
#include "GraphicsLib.h"
#include "rgb_hsv.h"
#include "species_common.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "timing.h"
#include "wlCostume.h"
#include "../wlModelLoad.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "dynFxInfo.h"
#include "bounds.h"
#include "entCritter.h"
#include "character_target.h"

#if GAMESERVER || GAMECLIENT
	#include "EntityMovementManager.h"
	#include "EntityMovementProjectile.h"
	
#endif

#if GAMECLIENT
	#include "dynFxManager.h"
	#include "EntityClient.h"
	#include "gclEntity.h"
	#include "GraphicsLib.h"
	#include "gclDemo.h"
#endif

#include "AutoGen/wlCostume_h_ast.h"
#include "AutoGen/wlSkelInfo_h_ast.h"
#include "AutoGen/dynFxInfo_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

bool g_bDisableClassStances = 0;

// --------------------------------------------------------------------------
//  Static Data
// --------------------------------------------------------------------------

static const char* pcPooled_Avatar_Cloth;
static const char* pcPooled_Color0;
static const char* pcPooled_Color1;
static const char* pcPooled_Color2;
static const char* pcPooled_Color3;
static const char* pcPooled_MuscleWeight;
static const char* pcPooled_ReflectionWeight;
static const char* pcPooled_SpecularWeight;
static const char* pcPooled_Shininess;
static const char* pcPooled_Overall;
static const char* pcPooled_MountRiderScaleBlend;

AUTO_RUN;
void registerStaticCostumeStrings(void)
{
	pcPooled_Avatar_Cloth = allocAddStaticString("Avatar_Cloth");
	pcPooled_Color0 = allocAddStaticString("Color0");
	pcPooled_Color1 = allocAddStaticString("Color1");
	pcPooled_Color2 = allocAddStaticString("Color2");
	pcPooled_Color3 = allocAddStaticString("Color3");
	pcPooled_MuscleWeight = allocAddStaticString("MuscleWeight");
	pcPooled_ReflectionWeight = allocAddStaticString("ReflectionWeight");
	pcPooled_SpecularWeight = allocAddStaticString("SpecularWeight");
	pcPooled_Shininess = allocAddStaticString("Shininess");
	pcPooled_Overall = allocAddStaticString("Overall");
	pcPooled_MountRiderScaleBlend = allocAddString("MountRiderScaleBlend");
}


// --------------------------------------------------------------------------
//  WL Costume Generation
// --------------------------------------------------------------------------

// Internal helper for creating a WLCostume
// NOTE: you HAVE to allocAddString pcName yourself (or assign from a POOL_STRING)
static void costumeGenerate_AddMatColor(MaterialNamedConstant ***peaMatConstants, const char *pcName, const U8 value[4], U8 uGlowScale, F32 fTransparency)
{
	MaterialNamedConstant *pNamedConst = StructCreate(parse_MaterialNamedConstant);
	pNamedConst->name = pcName;

	pNamedConst->value[0] = value[0]*U8TOF32_COLOR;
	pNamedConst->value[1] = value[1]*U8TOF32_COLOR;
	pNamedConst->value[2] = value[2]*U8TOF32_COLOR;
	pNamedConst->value[3] = value[3]*U8TOF32_COLOR * (100.0-CLAMP(fTransparency,0.0,100.0))/100.0;

	if (uGlowScale > 1) {
		Vec3 hsv;
		// Switch to HSV and multiply value to get glow without shifting hue
		rgbToHsv(pNamedConst->value, hsv);
		hsv[2] *= uGlowScale;
		hsvToRgb(hsv, pNamedConst->value);
	}
	eaPush(peaMatConstants, pNamedConst);
}

// Internal helper for creating a WLCostume
// NOTE: you HAVE to allocAddString pcName yourself (or assign it from a POOL_STRING)
static void costumeGenerate_AddMatConstant4(MaterialNamedConstant ***peaMatConstants, const char *pcName, const U8 value[4])
{
	MaterialNamedConstant *pNamedConst = StructCreate(parse_MaterialNamedConstant);
	pNamedConst->name = pcName;
	pNamedConst->value[0] = value[0]/100.0;
	pNamedConst->value[1] = value[1]/100.0;
	pNamedConst->value[2] = value[2]/100.0;
	pNamedConst->value[3] = value[3]/100.0;
	eaPush(peaMatConstants, pNamedConst);
}


// Internal helper for creating a WLCostume
// NOTE: you HAVE to allocAddString pcName yourself (or assign it from a POOL_STRING)
static void costumeGenerate_AddMatConstant(MaterialNamedConstant ***peaMatConstants, const char *pcName, const Vec4 values)
{
	MaterialNamedConstant *pNamedConst = StructCreate(parse_MaterialNamedConstant);
	F32 fScale = costumeTailor_GetMatConstantScale(pcName);
	S32 iNumValues = costumeTailor_GetMatConstantNumValues(pcName);
	int ii = 0;
	
	// there can't be any more than four values in the mat constant's Vec4
	if (iNumValues > 4) {
		iNumValues = 4;
	}

	// Copy each value of the mat constant; if not all four values are used, then fill the extra
	// values in pNamedConst with copies of the first value of the mat constant.
	for (ii = 0; ii < 4; ii++) {
		int ValIdx = ((ii < iNumValues) ? ii : 0);
		F32 fResult = values[ValIdx]/fScale;

		if (strstri(pcName, "Shininess")) {
			if (fResult <= 0.005) {
				fResult = 0.005;
			} else if (fResult < 35.0) {
				fResult /= 100.0;
			} else if (fResult < 50.0) {
				fResult /= 85.0;
			} else if (fResult < 65.0) {
				fResult /= 65.0;
			} else if (fResult < 80.0) {
				fResult /= 45.0;
			} else if( fResult < 100) {
				fResult /= 15.0;
			} else {
				fResult /= 10.0;
			}
		} else if (strstri(pcName, "Strength")) {
			fResult = fResult - 1.0; // Change range from 0 through 2, to -1 through 1
		} else if (strstri(pcName, "RovingRotateScale")) {
			if (ii == 2) {
				fResult = fResult / 20; // Change range from 0 through 20, to 0 through 1 only for 3rd value
			} else if (ii == 3) {
				fResult = 0; // Hardcoded zero on 4th value
			}
		} else if (strstri(pcName, "RovingTranslate")) {
			if ((ii == 2) || (ii == 3)) {
				fResult = 1; // Hardcoded 1 in 3rd and 4th value
			}
		} else if (strstri(pcName, "RotateOnly")) {
			if ((ii == 0) || (ii == 1)) {
				fResult = 1; // Hardcoded 1 in 1st and 2nd value
			} else if (ii == 2) {
				fResult = values[0]/100.0; // Put actual value into 3rd value
			} else if (ii == 3) {
				fResult = 0; // Hardcoded 0 in 4th value
			}
		}
		pNamedConst->value[ii] = fResult;
	}

	pNamedConst->name = pcName;
	eaPush(peaMatConstants, pNamedConst);
}


// Internal helper for creating a WLCostume
// NOTE: you HAVE to properly pre-scale the values for this
static void costumeGenerate_AddMatConstantPreScaled(MaterialNamedConstant ***peaMatConstants, const char *pcName, const F32 value[4])
{
	MaterialNamedConstant *pNamedConst = StructCreate(parse_MaterialNamedConstant);
	pNamedConst->name = pcName;
	pNamedConst->value[0] = value[0];
	pNamedConst->value[1] = value[1];
	pNamedConst->value[2] = value[2];
	pNamedConst->value[3] = value[3];
	eaPush(peaMatConstants, pNamedConst);
}


static const char *costumeGenerate_GetTexWordsValue(PlayerCostume *pPCCostume, const char *pcKey)
{
	int i;

	for(i=eaSize(&pPCCostume->eaTexWords)-1; i>=0; --i) {
		if (pPCCostume->eaTexWords[i]->pcKey && (stricmp(pcKey, pPCCostume->eaTexWords[i]->pcKey) == 0)) {
			return pPCCostume->eaTexWords[i]->pcText;
		}
	}

	return NULL;
}


// Internal helper for creating a WLCostume
static void costumeGenerate_AddTexture(PlayerCostume *pPCCostume, CostumeTextureSwap ***peaTextureSwaps, PCTextureDef *pTexDef)
{
	CostumeTextureSwap *pSwap;
	char buf[1024];
	int i;

	if (!pTexDef) {
		Alertf("Missing costume texture def in costume '%s'\n", pPCCostume->pcName);
		return;
	}

	pSwap = StructCreate(parse_CostumeTextureSwap);
	pSwap->pcOldTexture = pTexDef->pcOrigTexture;
	eaPush(peaTextureSwaps, pSwap);
	if (pTexDef->pcTexWordsKey) {
		const char *pcKey = costumeGenerate_GetTexWordsValue(pPCCostume, pTexDef->pcTexWordsKey);
		if (pcKey) {
			size_t len;
			sprintf(buf, "\\%s\\", pTexDef->pcNewTexture);
			len = strlen(buf);
			strcat(buf, pcKey);
			string_toupper(buf + len);
			pSwap->pcNewTextureNonPooled = StructAllocString(buf);
		} else {
			pSwap->pcNewTexture = pTexDef->pcNewTexture;
		}
	} else {
		pSwap->pcNewTexture = pTexDef->pcNewTexture;
	}

	for(i=eaSize(&pTexDef->eaExtraSwaps)-1; i>=0; --i) {
		pSwap = StructCreate(parse_CostumeTextureSwap);
		pSwap->pcOldTexture = pTexDef->eaExtraSwaps[i]->pcOrigTexture;
		eaPush(peaTextureSwaps, pSwap);

		if (pTexDef->eaExtraSwaps[i]->pcTexWordsKey) {
			const char *pcKey = costumeGenerate_GetTexWordsValue(pPCCostume, pTexDef->eaExtraSwaps[i]->pcTexWordsKey);
			if (pcKey) {
				size_t len;
				sprintf(buf, "\\%s\\", pTexDef->eaExtraSwaps[i]->pcNewTexture);
				len = strlen(buf);
				strcat(buf, pcKey);
				string_toupper(buf + len);
				pSwap->pcNewTextureNonPooled = StructAllocString(buf);
			} else {
				pSwap->pcNewTexture = pTexDef->eaExtraSwaps[i]->pcNewTexture;
			}
		} else {
			pSwap->pcNewTexture = pTexDef->eaExtraSwaps[i]->pcNewTexture;
		}
	}
}


static void costumeGenerate_AddBits(WLCostume *pCostume, const char *pcBits)
{
	char buf[260];
	char *pcStart, *pcEnd;

	PERFINFO_AUTO_START_FUNC();

	strcpy(buf, pcBits);
	pcStart = buf;
	pcEnd = strchr(pcStart, ' ');
	while(pcEnd) {
		*pcEnd = '\0';
		if (strlen(pcStart)) {
			// Make sure this new bit isn't already in the array
			pcStart = (char*)allocAddString(pcStart);
			eaPushUnique(&pCostume->eaConstantBits, pcStart);
		}
		pcStart = pcEnd + 1;
		pcEnd = strchr(pcStart, ' ');
	}
	if (strlen(pcStart)) {
		// Make sure this new bit isn't already in the array
		pcStart = (char*)allocAddString(pcStart);
		eaPushUnique(&pCostume->eaConstantBits, pcStart);
	}

	PERFINFO_AUTO_STOP();
}


static void costumeGenerate_MovableTexConst(WLCostumePart *pPart, SpeciesDef *pSpecies, PCPart *pPCPart, PCMaterialDef *pMatDef)
{
#ifdef GAMECLIENT
	PCTextureDef *pTex;

	if (!pPCPart->pMovableTexture || !pMatDef) {
		return;
	}
	pTex = GET_REF(pPCPart->pMovableTexture->hMovableTexture);
	if (pTex && (stricmp("None",pTex->pcName) != 0))
	{
		Material* pMaterial = materialFindNoDefault(pMatDef->pcMaterial, 0);
		MaterialRenderInfo *render_info = NULL;
		int iTranslateIndex = -1;
		int iRotateIndex = -1;
		F32 fMovableMinX, fMovableMaxX, fMovableMinY, fMovableMaxY;
		F32 fMovableMinScaleX, fMovableMaxScaleX, fMovableMinScaleY, fMovableMaxScaleY;
		bool bMovableCanEditPosition, bMovableCanEditRotation, bMovableCanEditScale;

		costumeTailor_GetTextureMovableValues(pPCPart, pTex, pSpecies,
										&fMovableMinX, &fMovableMaxX, &fMovableMinY, &fMovableMaxY,
										&fMovableMinScaleX, &fMovableMaxScaleX, &fMovableMinScaleY, &fMovableMaxScaleY,
										&bMovableCanEditPosition, &bMovableCanEditRotation, &bMovableCanEditScale);

		if (pMaterial)
		{
			int i = -1;
			if (!pMaterial->graphic_props.render_info) {
				gfxMaterialsInitMaterial(pMaterial, true);
			}
			render_info = pMaterial->graphic_props.render_info;
			assert(render_info);
			for (i = (render_info->rdr_material.const_count * 4) - 1; i >= 0; --i) {
				if (render_info->constant_names[i]) {
					if (strstri(render_info->constant_names[i],"Rovingtranslate"))
					{
						iTranslateIndex = i;
					}
					else if (strstri(render_info->constant_names[i],"Rovingrotatescale"))
					{
						iRotateIndex = i;
					}
				}
			}
		}
		if (iTranslateIndex >= 0)
		{
			int j;

			ANALYSIS_ASSUME(render_info);
			for (j = 0; j < eaSize(&pPart->eaMatConstant); j++)
			{
				if (stricmp(pPart->eaMatConstant[j]->name,render_info->constant_names[iTranslateIndex]) == 0)
				{
					break;
				}
			}
			if (j >= eaSize(&pPart->eaMatConstant))
			{
				Vec4 values = {0,0,1,1};
				values[0] = fMovableMinX + (((pPCPart->pMovableTexture->fMovableX + 100.0f)*(fMovableMaxX-fMovableMinX))/200.0f);
				values[0] = CLAMP(values[0], fMovableMinX, fMovableMaxX);
				values[1] = fMovableMinY + (((pPCPart->pMovableTexture->fMovableY + 100.0f)*(fMovableMaxY-fMovableMinY))/200.0f);
				values[1] = CLAMP(values[1], fMovableMinY, fMovableMaxY);
				costumeGenerate_AddMatConstantPreScaled(&pPart->eaMatConstant, render_info->constant_names[iTranslateIndex], values);
			}
			else
			{
				pPart->eaMatConstant[j]->value[0] = fMovableMinX + (((pPCPart->pMovableTexture->fMovableX + 100.0f)*(fMovableMaxX-fMovableMinX))/200.0f);
				pPart->eaMatConstant[j]->value[0] = CLAMP(pPart->eaMatConstant[j]->value[0], fMovableMinX, fMovableMaxX);
				pPart->eaMatConstant[j]->value[1] = fMovableMinY + (((pPCPart->pMovableTexture->fMovableY + 100.0f)*(fMovableMaxY-fMovableMinY))/200.0f);
				pPart->eaMatConstant[j]->value[1] = CLAMP(pPart->eaMatConstant[j]->value[1], fMovableMinY, fMovableMaxY);
			}
		}

		if (iRotateIndex >= 0)
		{
			int j;

			for (j = 0; j < eaSize(&pPart->eaMatConstant); j++)
			{
				if (stricmp(pPart->eaMatConstant[j]->name,render_info->constant_names[iRotateIndex]) == 0)
				{
					break;
				}
			}
			if (j >= eaSize(&pPart->eaMatConstant))
			{
				Vec4 values = {1,1,0,0};
				values[0] = fMovableMinScaleX + (((pPCPart->pMovableTexture->fMovableScaleX)*(fMovableMaxScaleX-fMovableMinScaleX))/100.0f);
				values[0] = 20.0f - (CLAMP(values[0], fMovableMinScaleX, fMovableMaxScaleX));
				if (values[0] < 0) values[0] = 0;
				values[1] = fMovableMinScaleY + (((pPCPart->pMovableTexture->fMovableScaleY)*(fMovableMaxScaleY-fMovableMinScaleY))/100.0f);
				values[1] = 20.0f - (CLAMP(values[1], fMovableMinScaleY, fMovableMaxScaleY));
				if (values[1] < 0) values[1] = 0;
				values[2] = pPCPart->pMovableTexture->fMovableRotation/100.0f;
				if (values[2] < 0) values[2] = 1.0f + values[2];
				values[2] = CLAMP(values[2], 0, 1);
				costumeGenerate_AddMatConstantPreScaled(&pPart->eaMatConstant, render_info->constant_names[iRotateIndex], values);
			}
			else
			{
				pPart->eaMatConstant[j]->value[0] = fMovableMinScaleX + (((pPCPart->pMovableTexture->fMovableScaleX)*(fMovableMaxScaleX-fMovableMinScaleX))/100.0f);
				pPart->eaMatConstant[j]->value[0] = 20.0f - (CLAMP(pPart->eaMatConstant[j]->value[0], fMovableMinScaleX, fMovableMaxScaleX));
				if (pPart->eaMatConstant[j]->value[0] < 0) pPart->eaMatConstant[j]->value[0] = 0;
				pPart->eaMatConstant[j]->value[1] = fMovableMinScaleY + (((pPCPart->pMovableTexture->fMovableScaleY)*(fMovableMaxScaleY-fMovableMinScaleY))/100.0f);
				pPart->eaMatConstant[j]->value[1] = 20.0f - (CLAMP(pPart->eaMatConstant[j]->value[1], fMovableMinScaleY, fMovableMaxScaleY));
				if (pPart->eaMatConstant[j]->value[1] < 0) pPart->eaMatConstant[j]->value[1] = 0;
				pPart->eaMatConstant[j]->value[2] = pPCPart->pMovableTexture->fMovableRotation/100.0f;
				if (pPart->eaMatConstant[j]->value[2] < 0) pPart->eaMatConstant[j]->value[2] = 1.0f + pPart->eaMatConstant[j]->value[2];
				pPart->eaMatConstant[j]->value[2] = CLAMP(pPart->eaMatConstant[j]->value[2], 0, 1);
			}
		}
	}
#endif
}

static void costumeGenerate_SetSliderConst(WLCostumePart *pPart, SpeciesDef *pSpecies, PCPart *pPCPart, PCMaterialDef *pMatDef)
{
	int i;
	PCTextureDef *apTextures[] = {
		GET_REF(pPCPart->hPatternTexture),
		GET_REF(pPCPart->hDetailTexture),
		GET_REF(pPCPart->hSpecularTexture),
		GET_REF(pPCPart->hDiffuseTexture),
		pPCPart->pMovableTexture ? GET_REF(pPCPart->pMovableTexture->hMovableTexture) : NULL,
	};
	F32 afTextureValues[] = {
		SAFE_MEMBER2(pPCPart, pTextureValues, fPatternValue),
		SAFE_MEMBER2(pPCPart, pTextureValues, fDetailValue),
		SAFE_MEMBER2(pPCPart, pTextureValues, fSpecularValue),
		SAFE_MEMBER2(pPCPart, pTextureValues, fDiffuseValue),
		SAFE_MEMBER2(pPCPart, pMovableTexture, fMovableValue),
	};

	assertmsg(ARRAY_SIZE_CHECKED(apTextures) == ARRAY_SIZE_CHECKED(afTextureValues),
		"There must be as many texture values as textures.");

	for (i = 0; i < ARRAY_SIZE_CHECKED(apTextures); i++)
	{
		PCTextureDef *pTex = apTextures[i];
		F32 fTextureValue = afTextureValues[i];

		if (pTex
			&& pTex->pValueOptions
			&& pTex->pValueOptions->pcValueConstant
			&& *pTex->pValueOptions->pcValueConstant)
		{
			PCTextureValueOptions *pValue = pTex->pValueOptions;
			F32 fMin = pValue->fValueMin;
			F32 fMax = pValue->fValueMax;
			int j;
			costumeTailor_GetTextureValueMinMax(pPCPart, pTex, pSpecies, &fMin, &fMax);
			for (j = 0; j < eaSize(&pPart->eaMatConstant); j++)
			{
				if (!stricmp(pPart->eaMatConstant[j]->name,pValue->pcValueConstant))
				{
					break;
				}
			}
			if (j >= eaSize(&pPart->eaMatConstant))
			{
				if (pMatDef && costumeTailor_IsSliderConstValid(pValue->pcValueConstant,pMatDef,pValue->iValConstIndex))
				{
					Vec4 values = {0,0,0,0};
					if ((!strstri(pValue->pcValueConstant,"scale")))
					{
						values[0] = values[1] = values[2] = 1;
					}
					values[pValue->iValConstIndex] = fMin + (((fTextureValue + 100.0f)*(fMax-fMin))/200.0f);
					values[pValue->iValConstIndex] = CLAMP(values[pValue->iValConstIndex], fMin, fMax);
					costumeGenerate_AddMatConstant(&pPart->eaMatConstant, pValue->pcValueConstant, values);
				}
			}
			else
			{
				pPart->eaMatConstant[i]->value[pValue->iValConstIndex] = fMin + (((fTextureValue + 100.0f)*(fMax-fMin))/200.0f);
				pPart->eaMatConstant[i]->value[pValue->iValConstIndex] = CLAMP(pPart->eaMatConstant[i]->value[pValue->iValConstIndex], fMin, fMax);
			}
		}
	}
}

static bool costumeGenerate_SwapColors(PCPart *pPCPart, PCTextureDef *pTexDef, const U8 **color0, const U8 **color1, const U8 **color2, const U8 **color3)
{
	if (pTexDef && (pTexDef->uColorSwap0 != 0 || pTexDef->uColorSwap1 != 1 || pTexDef->uColorSwap2 != 2 || pTexDef->uColorSwap3 != 3))
	{
		switch (pTexDef->uColorSwap0)
		{
			xcase 0: *color0 = pPCPart->color0;
			xcase 1: *color0 = pPCPart->color1;
			xcase 2: *color0 = pPCPart->color2;
			xcase 3: *color0 = pPCPart->color3;
		}
		switch (pTexDef->uColorSwap1)
		{
			xcase 0: *color1 = pPCPart->color0;
			xcase 1: *color1 = pPCPart->color1;
			xcase 2: *color1 = pPCPart->color2;
			xcase 3: *color1 = pPCPart->color3;
		}
		switch (pTexDef->uColorSwap2)
		{
			xcase 0: *color2 = pPCPart->color0;
			xcase 1: *color2 = pPCPart->color1;
			xcase 2: *color2 = pPCPart->color2;
			xcase 3: *color2 = pPCPart->color3;
		}
		switch (pTexDef->uColorSwap3)
		{
			xcase 0: *color3 = pPCPart->color0;
			xcase 1: *color3 = pPCPart->color1;
			xcase 2: *color3 = pPCPart->color2;
			xcase 3: *color3 = pPCPart->color3;
		}
		return true;
	}
	return false;
}

static void costumeGenerate_SetWLClothPart(WLCostumePart *pPart, PlayerCostume *pPCCostume, SpeciesDef *pSpecies, PCPart *pPCPart, bool *pbComplete)
{
	PCMaterialDef *pMatDef;
	PCMaterialColorOptions *pColorOptions;
	PCTextureDef *pTexDef;
	MaterialNamedConstant *pNamedConst;
	const U8 *color0, *color1, *color2, *color3;
	bool bSwapedColors = false;
	int i;

	PERFINFO_AUTO_START_FUNC();

	if (!pPCPart) {
		if (gConf.bCostumeClothBackSideDefaultsToAvatarCloth) {
			// This is a backwards compatibility feature for Champions.
			// If there is no back data present, then this is used to make the back black
			U8 blackColor[4] = { 0, 0, 0, 255 };
			pPart->pSecondMaterialInfo = StructCreate(parse_WLCostumeMaterialInfo);
			pPart->pSecondMaterialInfo->pchMaterial = pcPooled_Avatar_Cloth;
			costumeGenerate_AddMatColor(&pPart->pSecondMaterialInfo->eaMatConstant, pcPooled_Color0, blackColor, 0, 0);
			costumeGenerate_AddMatColor(&pPart->pSecondMaterialInfo->eaMatConstant, pcPooled_Color1, blackColor, 0, 0);
			costumeGenerate_AddMatColor(&pPart->pSecondMaterialInfo->eaMatConstant, pcPooled_Color2, blackColor, 0, 0);
			costumeGenerate_AddMatColor(&pPart->pSecondMaterialInfo->eaMatConstant, pcPooled_Color3, blackColor, 0, 0);
		}
		PERFINFO_AUTO_STOP();
		return;
	}

	color0 = pPCPart->color0;
	color1 = pPCPart->color1;
	color2 = pPCPart->color2;
	color3 = pPCPart->color3;

	pPart->pSecondMaterialInfo = StructCreate(parse_WLCostumeMaterialInfo);

	pMatDef = GET_REF(pPCPart->hMatDef);
	if (!pMatDef && REF_STRING_FROM_HANDLE(pPCPart->hMatDef)) {
		*pbComplete = false;
	}
	if (pMatDef && (stricmp("None", pMatDef->pcName) == 0)) {
		pMatDef = NULL;
	}
	if (pMatDef) {
		pPart->pSecondMaterialInfo->pchMaterial = pMatDef->pcMaterial;
	}

	pColorOptions = pMatDef ? pMatDef->pColorOptions : NULL;

	// Apply the muscle
	pNamedConst = StructCreate(parse_MaterialNamedConstant);
	pNamedConst->name = pcPooled_MuscleWeight;
	pNamedConst->value[0] = (pPCCostume->fMuscle/100) * (pColorOptions && pColorOptions->bSuppressMuscle[0] ? 0 : 1);
	pNamedConst->value[1] = (pPCCostume->fMuscle/100) * (pColorOptions && pColorOptions->bSuppressMuscle[1] ? 0 : 1);
	pNamedConst->value[2] = (pPCCostume->fMuscle/100) * (pColorOptions && pColorOptions->bSuppressMuscle[2] ? 0 : 1);
	pNamedConst->value[3] = (pPCCostume->fMuscle/100) * (pColorOptions && pColorOptions->bSuppressMuscle[3] ? 0 : 1);
	eaPush(&pPart->pSecondMaterialInfo->eaMatConstant, pNamedConst);

	// Apply reflection weight & specular weight only if customize is true
	if (pPCPart->pCustomColors && pPCPart->pCustomColors->bCustomReflection) {
		costumeGenerate_AddMatConstant4(&pPart->pSecondMaterialInfo->eaMatConstant, pcPooled_ReflectionWeight, pPCPart->pCustomColors->reflection);
	} else if (pColorOptions && pColorOptions->bCustomReflection) {
		costumeGenerate_AddMatConstant4(&pPart->pSecondMaterialInfo->eaMatConstant, pcPooled_ReflectionWeight, pColorOptions->defaultReflection);
	}
	if (pPCPart->pCustomColors && pPCPart->pCustomColors->bCustomSpecularity) {
		costumeGenerate_AddMatConstant4(&pPart->pSecondMaterialInfo->eaMatConstant, pcPooled_SpecularWeight, pPCPart->pCustomColors->specularity);
	} else if (pColorOptions && pColorOptions->bCustomSpecularity) {
		costumeGenerate_AddMatConstant4(&pPart->pSecondMaterialInfo->eaMatConstant, pcPooled_SpecularWeight, pColorOptions->defaultSpecularity);
	}

	// copy in texture swaps
	pTexDef = GET_REF(pPCPart->hPatternTexture);
	if (!pTexDef && REF_STRING_FROM_HANDLE(pPCPart->hPatternTexture)) {
		*pbComplete = false;
	}
	if (pTexDef && (stricmp("None", pTexDef->pcName) != 0)) {
		costumeGenerate_AddTexture(pPCCostume, &pPart->pSecondMaterialInfo->eaTextureSwaps, pTexDef);
	}
	if (!bSwapedColors)
	{
		bSwapedColors = costumeGenerate_SwapColors(pPCPart, pTexDef, &color0, &color1, &color2, &color3);
	}
	pTexDef = GET_REF(pPCPart->hDetailTexture);
	if (!pTexDef && REF_STRING_FROM_HANDLE(pPCPart->hDetailTexture)) {
		*pbComplete = false;
	}
	if (pTexDef && (stricmp("None", pTexDef->pcName) != 0)) {
		costumeGenerate_AddTexture(pPCCostume, &pPart->pSecondMaterialInfo->eaTextureSwaps, pTexDef);
	}
	if (!bSwapedColors)
	{
		bSwapedColors = costumeGenerate_SwapColors(pPCPart, pTexDef, &color0, &color1, &color2, &color3);
	}
	pTexDef = GET_REF(pPCPart->hSpecularTexture);
	if (!pTexDef && REF_STRING_FROM_HANDLE(pPCPart->hSpecularTexture)) {
		*pbComplete = false;
	}
	if (pTexDef && (stricmp("None", pTexDef->pcName) != 0)) {
		costumeGenerate_AddTexture(pPCCostume, &pPart->pSecondMaterialInfo->eaTextureSwaps, pTexDef);
	}
	if (!bSwapedColors)
	{
		bSwapedColors = costumeGenerate_SwapColors(pPCPart, pTexDef, &color0, &color1, &color2, &color3);
	}
	pTexDef = GET_REF(pPCPart->hDiffuseTexture);
	if (!pTexDef && REF_STRING_FROM_HANDLE(pPCPart->hDiffuseTexture)) {
		*pbComplete = false;
	}
	if (pTexDef && (stricmp("None", pTexDef->pcName) != 0)) {
		costumeGenerate_AddTexture(pPCCostume, &pPart->pSecondMaterialInfo->eaTextureSwaps, pTexDef);
	}
	if (!bSwapedColors)
	{
		bSwapedColors = costumeGenerate_SwapColors(pPCPart, pTexDef, &color0, &color1, &color2, &color3);
	}
	if (pPCPart->pMovableTexture) {
		pTexDef = GET_REF(pPCPart->pMovableTexture->hMovableTexture);
		if (!pTexDef && REF_STRING_FROM_HANDLE(pPCPart->pMovableTexture->hMovableTexture)) {
			*pbComplete = false;
		}
		if (pTexDef && (stricmp("None", pTexDef->pcName) != 0)) {
			costumeGenerate_AddTexture(pPCCostume, &pPart->pSecondMaterialInfo->eaTextureSwaps, pTexDef);
		}
		if (!bSwapedColors)
		{
			bSwapedColors = costumeGenerate_SwapColors(pPCPart, pTexDef, &color0, &color1, &color2, &color3);
		}
	}

	// Set the colors
	costumeGenerate_AddMatColor(&pPart->pSecondMaterialInfo->eaMatConstant, pcPooled_Color0, color0, GET_PART_GLOWSCALE(pPCPart, 0), GET_COSTUME_TRANSPARENCY(pPCCostume));
	costumeGenerate_AddMatColor(&pPart->pSecondMaterialInfo->eaMatConstant, pcPooled_Color1, color1, GET_PART_GLOWSCALE(pPCPart, 1), GET_COSTUME_TRANSPARENCY(pPCCostume));
	costumeGenerate_AddMatColor(&pPart->pSecondMaterialInfo->eaMatConstant, pcPooled_Color2, color2, GET_PART_GLOWSCALE(pPCPart, 2), GET_COSTUME_TRANSPARENCY(pPCCostume));
	costumeGenerate_AddMatColor(&pPart->pSecondMaterialInfo->eaMatConstant, pcPooled_Color3, color3, GET_PART_GLOWSCALE(pPCPart, 3), GET_COSTUME_TRANSPARENCY(pPCCostume));

	if (pPCPart->pArtistData) {
		for(i=0; i<eaSize(&pPCPart->pArtistData->eaExtraTextures); ++i) {
			pTexDef = GET_REF(pPCPart->pArtistData->eaExtraTextures[i]->hTexture);
			if (!pTexDef && REF_STRING_FROM_HANDLE(pPCPart->pArtistData->eaExtraTextures[i]->hTexture)) {
				*pbComplete = false;
			}
			if (pTexDef && (stricmp("None", pTexDef->pcName) != 0)) {
				costumeGenerate_AddTexture(pPCCostume, &pPart->pSecondMaterialInfo->eaTextureSwaps, pTexDef);
			}
		}

		// Add extra colors & constants
		for(i=0; i<eaSize(&pPCPart->pArtistData->eaExtraColors); ++i) {
			costumeGenerate_AddMatColor(&pPart->pSecondMaterialInfo->eaMatConstant, pPCPart->pArtistData->eaExtraColors[i]->pcName, pPCPart->pArtistData->eaExtraColors[i]->color, 1, 0);
		}
		for(i=0; i<eaSize(&pPCPart->pArtistData->eaExtraConstants); ++i) {
			costumeGenerate_AddMatConstant(&pPart->pSecondMaterialInfo->eaMatConstant, pPCPart->pArtistData->eaExtraConstants[i]->pcName, pPCPart->pArtistData->eaExtraConstants[i]->values);
		}
	}

	if (!eaSize(&pPart->pSecondMaterialInfo->eaTextureSwaps))
	{
		//Safety: We are not allowed to have no texture swaps
		StructDestroySafe(parse_WLCostumeMaterialInfo, &pPart->pSecondMaterialInfo);
	}

	if (pPart->pSecondMaterialInfo)
	{
		costumeGenerate_SetSliderConst(pPart, pSpecies, pPCPart, pMatDef);
		costumeGenerate_MovableTexConst(pPart, pSpecies, pPCPart, pMatDef);
	}

	PERFINFO_AUTO_STOP();
}

WLCostumePart *costumeGenerate_SetWLPart(WLCostume *pCostume, PlayerCostume *pPCCostume, SpeciesDef *pSpecies, PCPart *pPCPart, bool *pbComplete)
{
	MaterialNamedConstant *pNamedConst;
	PCBoneDef *pBoneDef = NULL;
	PCGeometryDef *pGeoDef = NULL;
	PCMaterialDef *pMatDef = NULL;
	PCMaterialColorOptions *pColorOptions = NULL;
	PCMaterialOptions *pOptions = NULL;
	PCTextureDef *pTexDef = NULL;
	WLCostumePart *pPart = NULL;
	const U8 *color0, *color1, *color2, *color3;
	bool bSwapedColors = false;
	int i = 0, j = 0;
	bool found = false;

	PERFINFO_AUTO_START_FUNC();

	pBoneDef = GET_REF(pPCPart->hBoneDef);
	if (!pBoneDef && REF_STRING_FROM_HANDLE(pPCPart->hBoneDef)) {
		*pbComplete = false;
	}
	pGeoDef = GET_REF(pPCPart->hGeoDef);
	if (!pGeoDef && REF_STRING_FROM_HANDLE(pPCPart->hGeoDef)) {
		*pbComplete = false;
	}
	if (pGeoDef && (stricmp("None", pGeoDef->pcName) == 0)) {
		pGeoDef = NULL;
	}
	pMatDef = GET_REF(pPCPart->hMatDef);
	if (!pMatDef && REF_STRING_FROM_HANDLE(pPCPart->hMatDef)) {
		*pbComplete = false;
	}
	if (pMatDef && (stricmp("None", pMatDef->pcName) == 0)) {
		pMatDef = NULL;
	}

	if (!pBoneDef || !pGeoDef || !pGeoDef->pcGeometry) {
		// Skip the part if it is not defined
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	// Copy in values
	pPart = StructCreate(parse_WLCostumePart);
	pPart->pchBoneName = allocAddString(pBoneDef->pcBoneName);
	pPart->pchGeometry = pGeoDef->pcGeometry;
	if (pGeoDef->eLOD != kCostumeLODLevel_Default)
	{
		pPart->uiRequiredLOD = (U32)pGeoDef->eLOD;
	}
	else
	{
		pPart->uiRequiredLOD = (U32)pBoneDef->eLOD;
	}
	pPart->pcOrigAttachmentBone = pBoneDef->pcClickBoneName?pBoneDef->pcClickBoneName:pBoneDef->pcBoneName;
	if (pMatDef) {
		pPart->pchMaterial = pMatDef->pcMaterial;
	}
	if (pGeoDef->pcModel) {
		pPart->pcModel = pGeoDef->pcModel;
	}

	pColorOptions = pMatDef ? pMatDef->pColorOptions : NULL;
	pOptions = pMatDef ? pMatDef->pOptions : NULL;

	color0 = pPCPart->color0;
	color1 = pPCPart->color1;
	color2 = pPCPart->color2;
	color3 = pPCPart->color3;

	// Apply the muscle
	pNamedConst = StructCreate(parse_MaterialNamedConstant);
	pNamedConst->name = pcPooled_MuscleWeight;
	pNamedConst->value[0] = (pPCCostume->fMuscle/100) * (pColorOptions && pColorOptions->bSuppressMuscle[0] ? 0 : 1);
	pNamedConst->value[1] = (pPCCostume->fMuscle/100) * (pColorOptions && pColorOptions->bSuppressMuscle[1] ? 0 : 1);
	pNamedConst->value[2] = (pPCCostume->fMuscle/100) * (pColorOptions && pColorOptions->bSuppressMuscle[2] ? 0 : 1);
	pNamedConst->value[3] = (pPCCostume->fMuscle/100) * (pColorOptions && pColorOptions->bSuppressMuscle[3] ? 0 : 1);
	eaPush(&pPart->eaMatConstant, pNamedConst);

	// Apply reflection weight & specular weight only if customize is true
	if (pPCPart->pCustomColors && pPCPart->pCustomColors->bCustomReflection) {
		costumeGenerate_AddMatConstant4(&pPart->eaMatConstant, pcPooled_ReflectionWeight, pPCPart->pCustomColors->reflection);
	} else if (pColorOptions && pColorOptions->bCustomReflection) {
		costumeGenerate_AddMatConstant4(&pPart->eaMatConstant, pcPooled_ReflectionWeight, pColorOptions->defaultReflection);
	}
	if (pPCPart->pCustomColors && pPCPart->pCustomColors->bCustomSpecularity) {
		costumeGenerate_AddMatConstant4(&pPart->eaMatConstant, pcPooled_SpecularWeight, pPCPart->pCustomColors->specularity);
	} else if (pColorOptions && pColorOptions->bCustomSpecularity) {
		costumeGenerate_AddMatConstant4(&pPart->eaMatConstant, pcPooled_SpecularWeight, pColorOptions->defaultSpecularity);
	}

	// copy in texture swaps
	pTexDef = GET_REF(pPCPart->hPatternTexture);
	if (!pTexDef && REF_STRING_FROM_HANDLE(pPCPart->hPatternTexture)) {
		*pbComplete = false;
	}
	if (pTexDef && (stricmp("None", pTexDef->pcName) != 0)) {
		costumeGenerate_AddTexture(pPCCostume, &pPart->eaTextureSwaps, pTexDef);
	}
	if (!bSwapedColors)
	{
		bSwapedColors = costumeGenerate_SwapColors(pPCPart, pTexDef, &color0, &color1, &color2, &color3);
	}
	pTexDef = GET_REF(pPCPart->hDetailTexture);
	if (!pTexDef && REF_STRING_FROM_HANDLE(pPCPart->hDetailTexture)) {
		*pbComplete = false;
	}
	if (pTexDef && (stricmp("None", pTexDef->pcName) != 0)) {
		costumeGenerate_AddTexture(pPCCostume, &pPart->eaTextureSwaps, pTexDef);
	}
	if (!bSwapedColors)
	{
		bSwapedColors = costumeGenerate_SwapColors(pPCPart, pTexDef, &color0, &color1, &color2, &color3);
	}
	pTexDef = GET_REF(pPCPart->hSpecularTexture);
	if (!pTexDef && REF_STRING_FROM_HANDLE(pPCPart->hSpecularTexture)) {
		*pbComplete = false;
	}
	if (pTexDef && (stricmp("None", pTexDef->pcName) != 0)) {
		costumeGenerate_AddTexture(pPCCostume, &pPart->eaTextureSwaps, pTexDef);
	}
	if (!bSwapedColors)
	{
		bSwapedColors = costumeGenerate_SwapColors(pPCPart, pTexDef, &color0, &color1, &color2, &color3);
	}
	pTexDef = GET_REF(pPCPart->hDiffuseTexture);
	if (!pTexDef && REF_STRING_FROM_HANDLE(pPCPart->hDiffuseTexture)) {
		*pbComplete = false;
	}
	if (pTexDef && (stricmp("None", pTexDef->pcName) != 0)) {
		costumeGenerate_AddTexture(pPCCostume, &pPart->eaTextureSwaps, pTexDef);
	}
	if (!bSwapedColors)
	{
		bSwapedColors = costumeGenerate_SwapColors(pPCPart, pTexDef, &color0, &color1, &color2, &color3);
	}
	if (pPCPart->pMovableTexture) {
		pTexDef = GET_REF(pPCPart->pMovableTexture->hMovableTexture);
		if (!pTexDef && REF_STRING_FROM_HANDLE(pPCPart->pMovableTexture->hMovableTexture)) {
			*pbComplete = false;
		}
		if (pTexDef && (stricmp("None", pTexDef->pcName) != 0)) {
			costumeGenerate_AddTexture(pPCCostume, &pPart->eaTextureSwaps, pTexDef);
		}
		if (!bSwapedColors)
		{
			bSwapedColors = costumeGenerate_SwapColors(pPCPart, pTexDef, &color0, &color1, &color2, &color3);
		}
	}

	// Set the colors
	costumeGenerate_AddMatColor(&pPart->eaMatConstant, pcPooled_Color0, color0, GET_PART_GLOWSCALE(pPCPart, 0), GET_COSTUME_TRANSPARENCY(pPCCostume));
	costumeGenerate_AddMatColor(&pPart->eaMatConstant, pcPooled_Color1, color1, GET_PART_GLOWSCALE(pPCPart, 1), GET_COSTUME_TRANSPARENCY(pPCCostume));
	costumeGenerate_AddMatColor(&pPart->eaMatConstant, pcPooled_Color2, color2, GET_PART_GLOWSCALE(pPCPart, 2), GET_COSTUME_TRANSPARENCY(pPCCostume));
	if (pMatDef && pMatDef->bHasSkin && (
		pPCCostume->skinColor[0] || pPCCostume->skinColor[1] || pPCCostume->skinColor[2] || pPCCostume->skinColor[3])) {
			// Use costume skin color if it is set and the material wants a skin color
			costumeGenerate_AddMatColor(&pPart->eaMatConstant, pcPooled_Color3, pPCCostume->skinColor, 0, GET_COSTUME_TRANSPARENCY(pPCCostume));
	} else {
		costumeGenerate_AddMatColor(&pPart->eaMatConstant, pcPooled_Color3, color3, GET_PART_GLOWSCALE(pPCPart, 3), GET_COSTUME_TRANSPARENCY(pPCCostume));
	}

	if (pPCPart->pArtistData) {
		for(i=0; i<eaSize(&pPCPart->pArtistData->eaExtraTextures); ++i) {
			pTexDef = GET_REF(pPCPart->pArtistData->eaExtraTextures[i]->hTexture);
			if (!pTexDef && REF_STRING_FROM_HANDLE(pPCPart->pArtistData->eaExtraTextures[i]->hTexture)) {
				*pbComplete = false;
			}
			if (pTexDef && (stricmp("None", pTexDef->pcName) != 0)) {
				costumeGenerate_AddTexture(pPCCostume, &pPart->eaTextureSwaps, pTexDef);
			}
		}
	}

	// Add extra colors
	if (pPCPart->pArtistData) {
		for(i=0; i<eaSize(&pPCPart->pArtistData->eaExtraColors); ++i) {
			costumeGenerate_AddMatColor(&pPart->eaMatConstant, pPCPart->pArtistData->eaExtraColors[i]->pcName, pPCPart->pArtistData->eaExtraColors[i]->color, 1, 0);
		}
	}
	if (pOptions) {
		// Add in fallback color values from the part's material, but only if not overridden by the part itself
		for (i = 0; i < eaSize(&pOptions->eaExtraColors); i++) {
			found = false;
			if (pPCPart->pArtistData) {
				for (j = 0; j < eaSize(&pPCPart->pArtistData->eaExtraColors); j++) {
					if (stricmp(pOptions->eaExtraColors[i]->pcName, pPCPart->pArtistData->eaExtraColors[j]->pcName) == 0) {
						found = true;
						break;
					}
				}
			}
			if (!found) {
				U8 color[4];
				VEC4_TO_COSTUME_COLOR(pOptions->eaExtraColors[i]->color, color);
				costumeGenerate_AddMatColor(&pPart->eaMatConstant, pOptions->eaExtraColors[i]->pcName, color, 1, 0);
			}
		}
	}
	
	// Add extra constants
	if (pPCPart->pArtistData) {
		for(i=0; i<eaSize(&pPCPart->pArtistData->eaExtraConstants); ++i) {
			costumeGenerate_AddMatConstant(&pPart->eaMatConstant, pPCPart->pArtistData->eaExtraConstants[i]->pcName, pPCPart->pArtistData->eaExtraConstants[i]->values);
		}
	}
	if (pOptions) {
		// Add in fallback constant values from the part's material, but only if not overridden by the part itself
		for (i = 0; i < eaSize(&pOptions->eaExtraConstants); i++) {
			found = false;
			if (pPCPart->pArtistData) {
				for (j = 0; j < eaSize(&pPCPart->pArtistData->eaExtraConstants); j++) {
					if (stricmp(pOptions->eaExtraConstants[i]->pcName, pPCPart->pArtistData->eaExtraConstants[j]->pcName) == 0) {
						found = true;
						break;
					}
				}
			}
			if (!found) {
				costumeGenerate_AddMatConstant(&pPart->eaMatConstant, pOptions->eaExtraConstants[i]->pcName, pOptions->eaExtraConstants[i]->values);
			}
		}
	}

	costumeGenerate_SetSliderConst(pPart, pSpecies, pPCPart, pMatDef);
	costumeGenerate_MovableTexConst(pPart, pSpecies, pPCPart, pMatDef);

	// Put in "no-shadow" flag
	if (pPCPart->pArtistData && pPCPart->pArtistData->bNoShadow) {
		pPart->bNoShadow = true;
	}

	if (pBoneDef)
		pPart->bRaycastable = pBoneDef->bRaycastable;

	// Apply cloth
	if (pGeoDef && pGeoDef->pClothData && pGeoDef->pClothData->bIsCloth) {
		pPart->pcClothInfo = pGeoDef->pClothData->pcClothInfo;
		pPart->pcClothColInfo = pGeoDef->pClothData->pcClothColInfo;
	}

	if (pGeoDef)
	{
		PCSkeletonDef* pSkeletonDef = GET_REF(pPCCostume->hSkeleton);
		if (pSkeletonDef)
		{
			FOR_EACH_IN_EARRAY(pSkeletonDef->eaStumps, PCStump, pStump)
			{
				PCGeometryDef* pStumpGeo = GET_REF(pStump->hGeoDef);
				if (pStumpGeo)
				{
					PCBoneDef* pStumpBoneDef = GET_REF(pStumpGeo->hBone);
					if (pStumpBoneDef && pStumpBoneDef == GET_REF(pGeoDef->hBone))
					{
						// Bones match, this must be a valid stump
						pPart->pcStumpGeo = pStumpGeo->pcGeometry;
						pPart->pcStumpModel = pStumpGeo->pcModel;
					}
				}
			}
			FOR_EACH_END;
		}
	}

	// See if this is part of an optional piece so that FX can exclude
	// material swaps or adds on it.
	pPart->bOptionalPart = false;
	if(pBoneDef) {
		PCSkeletonDef *pSkeleton = GET_REF(pPCCostume->hSkeleton);
		if(pSkeleton) {
			for(i = 0; i < eaSize(&pSkeleton->eaOptionalBoneDefs); i++) {
				PCBoneDef *pCheckBoneDef = GET_REF(pSkeleton->eaOptionalBoneDefs[i]->hBone);
				if(pCheckBoneDef) {
					if(pCheckBoneDef->pcName == pBoneDef->pcName) {
						pPart->bOptionalPart = true;
						break;
					}
				}
			}
		}
	}

	// Put the part on the costume
	eaPush(&pCostume->eaCostumeParts, pPart);

	PERFINFO_AUTO_STOP();

	return pPart;
}


const char *costumeGenerate_CreateWLCostumeName(PlayerCostume *pPCCostume, const char *pcNamePrefix, GlobalType type, ContainerID id, U32 uiSubCostumeIndex)
{
	char buf[260];
	if (!pcNamePrefix) {
		pcNamePrefix = "";
	}
	if (id) {
		if (uiSubCostumeIndex > 0)
			sprintf(buf, "%s%d_%d_Sub%d", pcNamePrefix, type, id, uiSubCostumeIndex);
		else
			sprintf(buf, "%s%d_%d", pcNamePrefix, type, id);
	} else {
		if (uiSubCostumeIndex > 0)
			sprintf(buf, "%s%s_Sub%d", pcNamePrefix, pPCCostume->pcName, uiSubCostumeIndex);
		else
			sprintf(buf, "%s%s", pcNamePrefix, pPCCostume->pcName);
	}
	return allocAddString(buf);
}

void costumeGenerate_AddFx(WLCostume* pCostume, const char* pcName, F32 fHue, DynParamBlock *pParams)
{
	if (pcName && strlen(pcName)) {
		CostumeFX *pFX = StructCreate(parse_CostumeFX);
		SET_HANDLE_FROM_STRING("DynFXInfo", pcName, pFX->hFx);
		pFX->fHue = fHue;
		pFX->pParams = StructClone(parse_DynParamBlock, pParams);
		eaPush(&pCostume->eaFX, pFX);
	}
}



// Create a world layer costume from a player costume
// Returns the main costume, and if an optional pointer to an earray of costumes is passed in, will also return any sub costumes (animated costume parts)
WLCostume* costumeGenerate_CreateWLCostumeEx(PlayerCostume *pPCCostume, SpeciesDef *pSpecies, CharacterClass* pClass, const char** eaExtraStances, const PCSlotType *pSlotType,
	PCMood *pMood, PCFXTemp ***eaAdditionalFX, char *pcNamePrefix, GlobalType type, ContainerID id, bool bIsPlayer, bool bGenerateSkeletonFX, WLCostume*** peaSubCostumes)
{
	WLCostume* pCostume;
	PCSkeletonDef *pSkelDef;
	PCGeometryDef *pCollGeoDef;
	int i, j, k, n, groupIdx, valIdx;
	F32 globalScale = 1.0;
	bool bComplete = true;

	if (!pPCCostume)
		return NULL;

	pSkelDef = GET_REF(pPCCostume->hSkeleton);
	if (!pSkelDef) {
		return NULL;
	}

	PERFINFO_AUTO_START_FUNC();

	// Copy in basic values
	pCostume = StructCreate(parse_WLCostume);
	pCostume->pcName = costumeGenerate_CreateWLCostumeName(pPCCostume, pcNamePrefix, type, id, 0);

	// Apply body sock option
	pCostume->bForceNoLOD = SAFE_MEMBER2(pPCCostume, pArtistData, bNoBodySock);

	// Used to determine a mounts global scale at runtime based on both costumes, 1.0 = 100% mount, 0.0 = 100% rider, -1.0 = OFF (uses a hard coded value when mounting happens)
	if (pSkelDef->fMountRiderScaleBlend >= 0.f){
		pCostume->fMountRiderScaleBlend = CLAMP(pSkelDef->fMountRiderScaleBlend,0.f,1.f);
	} else if (TokenIsSpecifiedByName(parse_CostumeConfig, &g_CostumeConfig, pcPooled_MountRiderScaleBlend) && g_CostumeConfig.fMountRiderScaleBlend >= 0.f) {
		pCostume->fMountRiderScaleBlend = CLAMP(g_CostumeConfig.fMountRiderScaleBlend,0.f,1.f);
	} else {
		pCostume->fMountRiderScaleBlend = -1.f;
	}

	// Provides a description of collidable objects on the costume's skeleton
	// to apply to the cloth of any attached riders
	pCostume->pcMountClothCollisionInfo = pSkelDef->pcMountClothCollisionInfo;

	//set mount status
	pCostume->bMount = false;
	pCostume->bRider = false;
	pCostume->bRiderChild = false;

	//set terrain tilt vars
	pCostume->bTerrainTiltApply			= pSkelDef->bTerrainTiltApply;
	pCostume->bTerrainTiltModifyRoot	= pSkelDef->bTerrainTiltModifyRoot;
	pCostume->fTerrainTiltBaseLength	= pSkelDef->fTerrainTiltBaseLength;
	pCostume->fTerrainTiltStrength		= pSkelDef->fTerrainTiltStrength;
	pCostume->fTerrainTiltMaxBlendAngle = RAD(pSkelDef->fTerrainTiltMaxBlendAngle);

	SET_HANDLE_FROM_STRING("SkelInfo", pSkelDef->pcSkeleton, pCostume->hSkelInfo);
	pCostume->uNoCollision = SAFE_MEMBER2(pPCCostume, pArtistData, bNoCollision);
	for(i=0; i<eaSize(&pSkelDef->eaBodyScaleInfo); ++i) {
		F32 fValue;
		if (i < eafSize(&pPCCostume->eafBodyScales)) {
			fValue = CLAMP((pPCCostume->eafBodyScales[i] - 50) / 50, -1.0, 1.0);
		} else if (i < eafSize(&pSkelDef->eafDefaultBodyScales)) {
			fValue = CLAMP((pSkelDef->eafDefaultBodyScales[i] - 50) / 50, -1.0, 1.0);
		} else {
			fValue = 0;
		}
		{
			WLScaleAnimInterp* pNewScaleAnimInterp = StructCreate(parse_WLScaleAnimInterp);
			pNewScaleAnimInterp->pcName = pSkelDef->eaBodyScaleInfo[i]->pcName;
			pNewScaleAnimInterp->fValue = fValue;
			eaPush(&pCostume->eaScaleAnimInterp, pNewScaleAnimInterp);
		}

		fValue = (fValue + 1.0) * 50;
		for (j = eaSize(&pSkelDef->eaBodyScaleInfo[i]->eaAnimBitRange)-1; j >= 0; --j)
		{
			PCAnimBitRange *a = pSkelDef->eaBodyScaleInfo[i]->eaAnimBitRange[j];
			if (a->fMin <= fValue && a->fMax >= fValue)
			{
				costumeGenerate_AddBits(pCostume, a->pcBit);
			}
		}
	}
	pCostume->bAutoGlueUp = pSkelDef->bAutoGlueUp;
	if (pSkelDef->pcShieldGeometry)
	{
		pCostume->pcShieldGeometry = pSkelDef->pcShieldGeometry;
		pCostume->pcShieldAttachBone = pSkelDef->pcShieldAttachBone;
		copyVec3(pSkelDef->vShieldScale, pCostume->vShieldScale);
	}
	if (pPCCostume->pArtistData && pPCCostume->pArtistData->pcCollisionGeo) {
		pCollGeoDef = RefSystem_ReferentFromString(g_hCostumeGeometryDict, pPCCostume->pArtistData->pcCollisionGeo);
		if (pCollGeoDef && pCollGeoDef->pcModel) {
			pCostume->pcCollGeo = pCollGeoDef->pcModel; // pooled strings
		}
	}

	// Overall scale
	if ((pSkelDef->fHeightMin > 0.0) && (pSkelDef->fHeightBase > pSkelDef->fHeightMin) && (pSkelDef->fHeightMax > pSkelDef->fHeightBase)) {
		ScaleValue *pScale = StructCreate(parse_ScaleValue);
		F32 scale;
		if (pPCCostume->fHeight >= pSkelDef->fHeightBase)
		{
			scale = ( pPCCostume->fHeight - pSkelDef->fHeightBase ) / (pSkelDef->fHeightMax - pSkelDef->fHeightBase);
		}
		else 
		{
			scale = ( ( pPCCostume->fHeight - pSkelDef->fHeightMin ) / (pSkelDef->fHeightBase - pSkelDef->fHeightMin) ) - 1.0f;
		}
		scale = CLAMP(scale, -1.0, 1.0);
		pScale->pcScaleGroup = pcPooled_Overall;
		pScale->vScaleInputs[0] = pScale->vScaleInputs[1] =pScale->vScaleInputs[2] = scale;
		eaPush(&pCostume->eaScaleValue, pScale);
		globalScale = pPCCostume->fHeight / pSkelDef->fHeightBase;

		for (i = eaSize(&pSkelDef->eaAnimBitRange)-1; i >= 0; --i)
		{
			PCAnimBitRange *a = pSkelDef->eaAnimBitRange[i];
			if (a->fMin <= pPCCostume->fHeight && a->fMax >= pPCCostume->fHeight)
			{
				costumeGenerate_AddBits(pCostume, a->pcBit);
			}
		}
	}

	// Apply scale values
	for(i=0; i<eaSize(&pPCCostume->eaScaleValues); ++i) {
		// Ignore zero scale values
		if (pPCCostume->eaScaleValues[i]->fValue == 0) {
			continue;
		}
		// Look for proper scale info on the skeleton; groups first, then standalone scale info
		for (groupIdx = 0; groupIdx < eaSize(&pSkelDef->eaScaleInfoGroups); groupIdx++) {
			PCScaleInfoGroup* pGroup = pSkelDef->eaScaleInfoGroups[groupIdx];
			for(j=0; j < eaSize(&pGroup->eaScaleInfo); j++) {
				// apply the scale value if its name matches the given scale info, but only if it's not for an animated sub-skeleton
				if (stricmp(pGroup->eaScaleInfo[j]->pcName, pPCCostume->eaScaleValues[i]->pcScaleName) == 0 && estrLength(&pGroup->eaScaleInfo[j]->pcSubSkeleton) == 0) {
					// Apply each scale from the scale info
					for(k=eaSize(&pGroup->eaScaleInfo[j]->eaScaleEntries)-1; k>=0; --k) {
						// See if it's a modification of an existing scale
						bool bFound = false;
						for(n=eaSize(&pCostume->eaScaleValue)-1; n>=0; --n) {
							if (stricmp(pGroup->eaScaleInfo[j]->eaScaleEntries[k]->pcName, pCostume->eaScaleValue[n]->pcScaleGroup) == 0) {
								bFound = true;
								pCostume->eaScaleValue[n]->vScaleInputs[pGroup->eaScaleInfo[j]->eaScaleEntries[k]->iIndex] = pPCCostume->eaScaleValues[i]->fValue / 100.0;
								break;
							}
						}
						if (!bFound) {
							ScaleValue *pScale = StructCreate(parse_ScaleValue);
							pScale->pcScaleGroup = allocAddString(pGroup->eaScaleInfo[j]->eaScaleEntries[k]->pcName);
							pScale->vScaleInputs[pGroup->eaScaleInfo[j]->eaScaleEntries[k]->iIndex] = pPCCostume->eaScaleValues[i]->fValue / 100.0;
							eaPush(&pCostume->eaScaleValue, pScale);
						}
					}
				}
			}
		}
		for(j=0; j < eaSize(&pSkelDef->eaScaleInfo); j++) {
			// apply the scale value if its name matches the given scale info, but only if it's not for an animated sub-skeleton
			if (stricmp(pSkelDef->eaScaleInfo[j]->pcName, pPCCostume->eaScaleValues[i]->pcScaleName) == 0 && estrLength(&pSkelDef->eaScaleInfo[j]->pcSubSkeleton) == 0) {
				// Apply each scale from the scale info
				for(k=eaSize(&pSkelDef->eaScaleInfo[j]->eaScaleEntries)-1; k>=0; --k) {
					// See if it's a modification of an existing scale
					bool bFound = false;
					for(n=eaSize(&pCostume->eaScaleValue)-1; n>=0; --n) {
						if (stricmp(pSkelDef->eaScaleInfo[j]->eaScaleEntries[k]->pcName, pCostume->eaScaleValue[n]->pcScaleGroup) == 0) {
							bFound = true;
							pCostume->eaScaleValue[n]->vScaleInputs[pSkelDef->eaScaleInfo[j]->eaScaleEntries[k]->iIndex] = pPCCostume->eaScaleValues[i]->fValue / 100.0;
							break;
						}
					}
					if (!bFound) {
						ScaleValue *pScale = StructCreate(parse_ScaleValue);
						pScale->pcScaleGroup = allocAddString(pSkelDef->eaScaleInfo[j]->eaScaleEntries[k]->pcName);
						pScale->vScaleInputs[pSkelDef->eaScaleInfo[j]->eaScaleEntries[k]->iIndex] = pPCCostume->eaScaleValues[i]->fValue / 100.0;
						eaPush(&pCostume->eaScaleValue, pScale);
					}
				}
			}
		}
	}

	// Apply FX from costume and skeleton
	if (pPCCostume->pArtistData) {
		FOR_EACH_IN_EARRAY(pPCCostume->pArtistData->eaFX, PCFX, pPCFX)
			DynParamBlock *block = pPCFX->pcParams ? StructCreateFromString(parse_DynParamBlock, pPCFX->pcParams) : NULL;

			costumeGenerate_AddFx(pCostume, pPCFX->pcName, pPCFX->fHue, block);

			StructDestroySafe(parse_DynParamBlock, &block);
		FOR_EACH_END;
	}

	if (bGenerateSkeletonFX)
	{
		FOR_EACH_IN_EARRAY(pSkelDef->eaFX, PCFX, pPCFX)
			costumeGenerate_AddFx(pCostume, pPCFX->pcName, pPCFX->fHue, NULL);
		FOR_EACH_END;
	}

	if (eaAdditionalFX)
	{
		FOR_EACH_IN_EARRAY((*eaAdditionalFX), PCFXTemp, pPCFX)
			costumeGenerate_AddFx(pCostume, pPCFX->pcName, pPCFX->fHue, pPCFX->pParams);
		FOR_EACH_END;
	}

	
	// Apply FX swaps from costume and the costume parts' material/geometry
	if (pSpecies)
	{
		for(i=0; i<eaSize(&pSpecies->eaFXSwap); ++i) {
			if (pSpecies->eaFXSwap[i]->pcOldName && strlen(pSpecies->eaFXSwap[i]->pcOldName) &&
				pSpecies->eaFXSwap[i]->pcNewName && strlen(pSpecies->eaFXSwap[i]->pcNewName)) {
					CostumeFXSwap *pSwap = StructCreate(parse_CostumeFXSwap);
					SET_HANDLE_FROM_STRING("DynFXInfo", pSpecies->eaFXSwap[i]->pcOldName, pSwap->hOldFx);
					SET_HANDLE_FROM_STRING("DynFXInfo", pSpecies->eaFXSwap[i]->pcNewName, pSwap->hNewFx);
					eaPush(&pCostume->eaFXSwap, pSwap);
			}
		}
	}
	if (pPCCostume->pArtistData) {
		for(i=0; i<eaSize(&pPCCostume->pArtistData->eaFXSwap); ++i) {
			if (pPCCostume->pArtistData->eaFXSwap[i]->pcOldName && strlen(pPCCostume->pArtistData->eaFXSwap[i]->pcOldName) &&
				pPCCostume->pArtistData->eaFXSwap[i]->pcNewName && strlen(pPCCostume->pArtistData->eaFXSwap[i]->pcNewName)) {
				CostumeFXSwap *pSwap = StructCreate(parse_CostumeFXSwap);
				SET_HANDLE_FROM_STRING("DynFXInfo", pPCCostume->pArtistData->eaFXSwap[i]->pcOldName, pSwap->hOldFx);
				SET_HANDLE_FROM_STRING("DynFXInfo", pPCCostume->pArtistData->eaFXSwap[i]->pcNewName, pSwap->hNewFx);
				eaPush(&pCostume->eaFXSwap, pSwap);
			}
		}
	}
	for (i = 0; i < eaSize(&pPCCostume->eaParts); i++) {
		PCMaterialDef* pMatDef = GET_REF(pPCCostume->eaParts[i]->hMatDef);
		PCGeometryDef* pGeoDef = GET_REF(pPCCostume->eaParts[i]->hGeoDef);
		
		// if the material has swaps defined, use those; otherwise, use the ones in the geometry
		if (pMatDef && pMatDef->pOptions && eaSize(&pMatDef->pOptions->eaFXSwap) > 0) {
			for (j = 0; j < eaSize(&pMatDef->pOptions->eaFXSwap); j++) {
				if ( pMatDef->pOptions->eaFXSwap[j]->pcOldName && strlen(pMatDef->pOptions->eaFXSwap[j]->pcOldName) &&
					 pMatDef->pOptions->eaFXSwap[j]->pcNewName && strlen(pMatDef->pOptions->eaFXSwap[j]->pcNewName) )
				{
					CostumeFXSwap *pSwap = StructCreate(parse_CostumeFXSwap);
					SET_HANDLE_FROM_STRING("DynFXInfo", pMatDef->pOptions->eaFXSwap[j]->pcOldName, pSwap->hOldFx);
					SET_HANDLE_FROM_STRING("DynFXInfo", pMatDef->pOptions->eaFXSwap[j]->pcNewName, pSwap->hNewFx);
					eaPush(&pCostume->eaFXSwap, pSwap);
				}
			}
		}
		else if (pGeoDef && pGeoDef->pOptions) {
			for (j = 0; j < eaSize(&pGeoDef->pOptions->eaFXSwap); j++) {
				if ( pGeoDef->pOptions->eaFXSwap[j]->pcOldName && strlen(pGeoDef->pOptions->eaFXSwap[j]->pcOldName) &&
					 pGeoDef->pOptions->eaFXSwap[j]->pcNewName && strlen(pGeoDef->pOptions->eaFXSwap[j]->pcNewName) )
				{
					CostumeFXSwap *pSwap = StructCreate(parse_CostumeFXSwap);
					SET_HANDLE_FROM_STRING("DynFXInfo", pGeoDef->pOptions->eaFXSwap[j]->pcOldName, pSwap->hOldFx);
					SET_HANDLE_FROM_STRING("DynFXInfo", pGeoDef->pOptions->eaFXSwap[j]->pcNewName, pSwap->hNewFx);
					eaPush(&pCostume->eaFXSwap, pSwap);
				}
			}
		}

		if (pGeoDef && pGeoDef->pOptions)
		{
			FOR_EACH_IN_EARRAY(pGeoDef->pOptions->eaFX, PCFX, pPCFX)
				costumeGenerate_AddFx(pCostume, pPCFX->pcName, pPCFX->fHue, NULL);
			FOR_EACH_END;
		}
	}

	// Apply Stance
	if (pPCCostume->pcStance || pSkelDef->pcDefaultStance) {
		const PCSlotStanceStruct *pSlotStance = costumeTailor_GetStanceFromSlot(pSlotType, costumeTailor_GetGenderFromSpeciesOrCostume(pSpecies, pPCCostume));
		const char *pcStance = pPCCostume->pcStance;
		if (!pcStance) {
			if(pSlotStance && pSlotStance->pcDefaultStance)
			{
				pcStance = pSlotStance->pcDefaultStance;
			}
			else if (pSpecies && pSpecies->pcDefaultStance)
			{
				pcStance = pSpecies->pcDefaultStance;
			}
			else
			{
				pcStance = pSkelDef->pcDefaultStance;
			}
		}
		if (pSlotStance)
		{
			for(i=eaSize(&pSlotStance->eaStanceInfo)-1; i>=0; --i) {
				if (stricmp(pSlotStance->eaStanceInfo[i]->pcName, pcStance) == 0) {
					if (pSlotStance->eaStanceInfo[i]->pcBits && strlen(pSlotStance->eaStanceInfo[i]->pcBits)) {
						costumeGenerate_AddBits(pCostume, pSlotStance->eaStanceInfo[i]->pcBits);
					}
					break;
				}
			}
		}
		else if (pSpecies && eaSize(&pSpecies->eaStanceInfo))
		{
			for(i=eaSize(&pSpecies->eaStanceInfo)-1; i>=0; --i) {
				if (stricmp(pSpecies->eaStanceInfo[i]->pcName, pcStance) == 0) {
					if (pSpecies->eaStanceInfo[i]->pcBits && strlen(pSpecies->eaStanceInfo[i]->pcBits)) {
						costumeGenerate_AddBits(pCostume, pSpecies->eaStanceInfo[i]->pcBits);
					}
					break;
				}
			}
		}
		else
		{
			for(i=eaSize(&pSkelDef->eaStanceInfo)-1; i>=0; --i) {
				if (stricmp(pSkelDef->eaStanceInfo[i]->pcName, pcStance) == 0) {
					if (pSkelDef->eaStanceInfo[i]->pcBits && strlen(pSkelDef->eaStanceInfo[i]->pcBits)) {
						costumeGenerate_AddBits(pCostume, pSkelDef->eaStanceInfo[i]->pcBits);
					}
					break;
				}
			}
		}
	}

	// Apply species animation bits
	if (pSpecies && pSpecies->pcCostumeBits)
	{
		costumeGenerate_AddBits(pCostume, pSpecies->pcCostumeBits);
	}

	// Apply class animation bits
	if (pClass && pClass->pchStanceWords && pClass->pchStanceWords[0] && !g_bDisableClassStances)
	{
		costumeGenerate_AddBits(pCostume, pClass->pchStanceWords);
	}

	// Apply extra animation bits
	for (i = 0; i < eaSize(&eaExtraStances); i++)
	{
		costumeGenerate_AddBits(pCostume, eaExtraStances[i]);
	}

	// Apply mood
	if (pMood && pMood->pcBits) {
		costumeGenerate_AddBits(pCostume, pMood->pcBits);
	}

	// Apply constant bits
	if (pPCCostume->pArtistData) {
		for(i=0; i<eaSize(&pPCCostume->pArtistData->eaConstantBits); ++i) {
			if (pPCCostume->pArtistData->eaConstantBits[i] && pPCCostume->pArtistData->eaConstantBits[i]->pcName && strlen(pPCCostume->pArtistData->eaConstantBits[i]->pcName)) {
				costumeGenerate_AddBits(pCostume, pPCCostume->pArtistData->eaConstantBits[i]->pcName);
			}
		}
	}
	if (bIsPlayer) {
		costumeGenerate_AddBits(pCostume, "PLAYER");
	}

	// Create the costume parts
	for(i=0; i<eaSize(&pPCCostume->eaParts); ++i) {
		WLCostumePart *pPart;
		PCPart* pPCPart = pPCCostume->eaParts[i];
		PCGeometryDef* pPCGeoDef = GET_REF(pPCPart->hGeoDef);
		PCBoneDef* pPCBoneDef = pPCGeoDef ? GET_REF(pPCGeoDef->hBone) : NULL;

		if(pPCBoneDef && pPCBoneDef->bPowerFX)
			continue;

		if (pPCGeoDef && pPCGeoDef->pOptions && pPCGeoDef->pOptions->pcSubSkeleton)
		{
			// If they passed in a pointer to a sub costume array, we'll load the part into a new costume and push it on the array
			if (peaSubCostumes)
			{
				WLCostume* pSubCostume = StructCreate(parse_WLCostume);
				eaPush(peaSubCostumes, pSubCostume);
				pSubCostume->pcName = costumeGenerate_CreateWLCostumeName(pPCCostume, pcNamePrefix, type, id, eaSize(peaSubCostumes));

				// Apply body sock option
				pSubCostume->bForceNoLOD = true;

				SET_HANDLE_FROM_STRING("SkelInfo", pPCGeoDef->pOptions->pcSubSkeleton, pSubCostume->hSkelInfo);
				pSubCostume->uNoCollision = 1;
				pPart = costumeGenerate_SetWLPart(pSubCostume, pPCCostume, pSpecies, pPCCostume->eaParts[i], &bComplete);
				
				//If this part was moved to a different bone because of an itemcategory, we need to reflect that here.
				if (pPCPart->pchOrigBone == pPCGeoDef->pOptions->pcSubBone)
				{
					PCBoneDef* pBone = GET_REF(pPCPart->hBoneDef);
					pSubCostume->pcSubCostumeAttachmentBone = pBone->pcBoneName;
				}
#ifdef GAMECLIENT
				//In demoplayback, we can't know the above because we're missing pchOrigBone.
				// Detect instead obviously mismatched bone/geo pairs.
				else if (demo_playingBack() && !REF_COMPARE_HANDLES(pPCPart->hBoneDef, pPCGeoDef->hBone))
				{
					PCBoneDef* pBone = GET_REF(pPCPart->hBoneDef);
					pSubCostume->pcSubCostumeAttachmentBone = pBone->pcBoneName;
				}
#endif
				else
					pSubCostume->pcSubCostumeAttachmentBone = pPCGeoDef->pOptions->pcSubBone;
				
				// apply bone scaling to the sub-skeleton
				for(valIdx=0; valIdx<eaSize(&pPCCostume->eaScaleValues); ++valIdx) {
					// Look for proper scale info on the skeleton; groups first, then standalone scale info
					for (groupIdx = 0; groupIdx < eaSize(&pSkelDef->eaScaleInfoGroups); groupIdx++) {
						PCScaleInfoGroup* pGroup = pSkelDef->eaScaleInfoGroups[groupIdx];
						for(j=0; j < eaSize(&pGroup->eaScaleInfo); j++) {
							// apply the scale value if its name matches the given scale info, but only if it's for this sub-skeleton
							if ( stricmp(pGroup->eaScaleInfo[j]->pcName, pPCCostume->eaScaleValues[valIdx]->pcScaleName) == 0 && 
								 stricmp(pGroup->eaScaleInfo[j]->pcSubSkeleton, pPCGeoDef->pOptions->pcSubSkeleton) == 0) 
							{
								// Apply each scale from the scale info
								for(k=eaSize(&pGroup->eaScaleInfo[j]->eaScaleEntries)-1; k>=0; --k) {
									// See if it's a modification of an existing scale
									bool bFound = false;
									for(n=eaSize(&pSubCostume->eaScaleValue)-1; n>=0; --n) {
										if (stricmp(pGroup->eaScaleInfo[j]->eaScaleEntries[k]->pcName, pSubCostume->eaScaleValue[n]->pcScaleGroup) == 0) {
											bFound = true;
											pSubCostume->eaScaleValue[n]->vScaleInputs[pGroup->eaScaleInfo[j]->eaScaleEntries[k]->iIndex] = pPCCostume->eaScaleValues[valIdx]->fValue / 100.0;
											break;
										}
									}
									if (!bFound) {
										ScaleValue *pScale = StructCreate(parse_ScaleValue);
										pScale->pcScaleGroup = allocAddString(pGroup->eaScaleInfo[j]->eaScaleEntries[k]->pcName);
										pScale->vScaleInputs[pGroup->eaScaleInfo[j]->eaScaleEntries[k]->iIndex] = pPCCostume->eaScaleValues[valIdx]->fValue / 100.0;
										eaPush(&pSubCostume->eaScaleValue, pScale);
									}
								}
							}
						}
					}
					for(j=0; j < eaSize(&pSkelDef->eaScaleInfo); j++) {
						// apply the scale value if its name matches the given scale info, but only if it's for this sub-skeleton
						if ( stricmp(pSkelDef->eaScaleInfo[j]->pcName, pPCCostume->eaScaleValues[valIdx]->pcScaleName) == 0 && 
							 stricmp(pSkelDef->eaScaleInfo[j]->pcSubSkeleton, pPCGeoDef->pOptions->pcSubSkeleton) == 0) 
						{
							// Apply each scale from the scale info
							for(k=eaSize(&pSkelDef->eaScaleInfo[j]->eaScaleEntries)-1; k>=0; --k) {
								// See if it's a modification of an existing scale
								bool bFound = false;
								for(n=eaSize(&pSubCostume->eaScaleValue)-1; n>=0; --n) {
									if (stricmp(pSkelDef->eaScaleInfo[j]->eaScaleEntries[k]->pcName, pSubCostume->eaScaleValue[n]->pcScaleGroup) == 0) {
										bFound = true;
										pSubCostume->eaScaleValue[n]->vScaleInputs[pSkelDef->eaScaleInfo[j]->eaScaleEntries[k]->iIndex] = pPCCostume->eaScaleValues[valIdx]->fValue / 100.0;
										break;
									}
								}
								if (!bFound) {
									ScaleValue *pScale = StructCreate(parse_ScaleValue);
									pScale->pcScaleGroup = allocAddString(pSkelDef->eaScaleInfo[j]->eaScaleEntries[k]->pcName);
									pScale->vScaleInputs[pSkelDef->eaScaleInfo[j]->eaScaleEntries[k]->iIndex] = pPCCostume->eaScaleValues[valIdx]->fValue / 100.0;
									eaPush(&pSubCostume->eaScaleValue, pScale);
								}
							}
						}
					}
				}
			}
			else // otherwise, just ignore the part and move on to the next
				pPart = NULL;
		}
		else
		{
			// Create the primary part
			pPart = costumeGenerate_SetWLPart(pCostume, pPCCostume, pSpecies, pPCCostume->eaParts[i], &bComplete);
		}

		// Deal with cloth child part
		if (pPart) {

			PCGeometryDef *pGeoDef = GET_REF(pPCCostume->eaParts[i]->hGeoDef);
			if (pPCCostume->eaParts[i]->pClothLayer && pGeoDef && pGeoDef->pClothData && pGeoDef->pClothData->bHasClothBack) {
				costumeGenerate_SetWLClothPart(pPart, pPCCostume, pSpecies, pPCCostume->eaParts[i]->pClothLayer, &bComplete);
			} else {
				if (pGeoDef && pGeoDef->pClothData && pGeoDef->pClothData->bIsCloth) {
					costumeGenerate_SetWLClothPart(pPart, pPCCostume, pSpecies, NULL, &bComplete);
				}
			}
		}
	}

	//Apply Body Sock
	if (pPCCostume->pBodySockInfo)
	{
		if (!pCostume->pBodySockInfo) pCostume->pBodySockInfo = StructCreate(parse_BodySockInfo);
		if (pCostume->pBodySockInfo)
		{
			pCostume->pBodySockInfo->pcBodySockGeo = pPCCostume->pBodySockInfo->pcBodySockGeo;
			pCostume->pBodySockInfo->pcBodySockPose = pPCCostume->pBodySockInfo->pcBodySockPose;
			pCostume->pBodySockInfo->vBodySockMax[0] = pPCCostume->pBodySockInfo->vBodySockMax[0];
			pCostume->pBodySockInfo->vBodySockMax[1] = pPCCostume->pBodySockInfo->vBodySockMax[1];
			pCostume->pBodySockInfo->vBodySockMax[2] = pPCCostume->pBodySockInfo->vBodySockMax[2];
			pCostume->pBodySockInfo->vBodySockMin[0] = pPCCostume->pBodySockInfo->vBodySockMin[0];
			pCostume->pBodySockInfo->vBodySockMin[1] = pPCCostume->pBodySockInfo->vBodySockMin[1];
			pCostume->pBodySockInfo->vBodySockMin[2] = pPCCostume->pBodySockInfo->vBodySockMin[2];
		}
	}

	// Verify the costume was created properly
	if (
		(	GetAppGlobalType() != GLOBALTYPE_GAMESERVER
		|| isDevelopmentMode()
		)
		&& !verifyCostume((WLCostume*)pCostume,false)
		)
		Errorf("Costume failed to verify, possibly due to player costume '%s' [file := '%s'], skeleton '%s' [file := '%s'], species '%s' [file := '%s'], or mood '%s' [file := '%s']",
			pPCCostume ? pPCCostume->pcName : "N/A", pPCCostume ? pPCCostume->pcFileName : "N/A",
			pSkelDef   ? pSkelDef->pcName   : "N/A", pSkelDef   ? pSkelDef->pcFileName   : "N/A",
			pSpecies   ? pSpecies->pcName   : "N/A", pSpecies   ? pSpecies->pcFileName   : "N/A",
			pMood      ? pMood->pcName      : "N/A", pMood      ? pMood->pcFilename      : "N/A"
			);

	pCostume->bComplete = bComplete;

	PERFINFO_AUTO_STOP();
	
	return pCostume;
}


// --------------------------------------------------------------------------
//  WL Costume Regeneration and Fixup
// --------------------------------------------------------------------------

static void costumeGenerate_CalculateCostumeExtents(Entity* e, WLCostume* wlc)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if (!e || !wlc)
		return;

	zeroVec3(wlc->vExtentsMin);
	zeroVec3(wlc->vExtentsMax);

	// Calculate extents
	{
		const Capsule*const* capsules = NULL;

		mmGetCapsules(e->mm.movement, &capsules);

		EARRAY_CONST_FOREACH_BEGIN(capsules, i, isize);
		{
			const Capsule*	c = capsules[i];
			Vec3			vEnd;

			if (c->iType != 0)
				continue;

			scaleAddVec3(c->vDir, c->fLength, c->vStart, vEnd);

			FOR_BEGIN(j, 3);
			{
				MAX1(wlc->vExtentsMax[j], c->vStart[j] + c->fRadius);
				MIN1(wlc->vExtentsMin[j], c->vStart[j] - c->fRadius);

				MAX1(wlc->vExtentsMax[j], vEnd[j] + c->fRadius);
				MIN1(wlc->vExtentsMin[j], vEnd[j] - c->fRadius);
			}
			FOR_END;
		}
		EARRAY_FOREACH_END;
	}
#endif
}


static void costumeGenerate_CreateMovementBodyAndDoOtherStuff(	Entity* e,
																WLCostume* wlc,
																PlayerCostume* pc)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	bool bCollisionGeo = false;
	
	
	if (entGetType(e) == GLOBALTYPE_ENTITYPROJECTILE)
		return; // projectile entities don't get their collision from the costume

	PERFINFO_AUTO_START_FUNC();
	
	mmResourceDestroyFG(e->mm.movement,
						&e->mm.movementBodyHandle);

	mmNoCollHandleDestroyFG(&e->mm.mnchCostume);

	if(wlc){
		if(wlc->uNoCollision){
			mmNoCollHandleCreateFG(e->mm.movement, &e->mm.mnchCostume, __FILE__, __LINE__);
		}

		if(	wlc->pcCollGeo &&
			wlc->uNoCollision)
		{
			ErrorFilenameGroupf(wlc->pcFileName,
								"Design",
								3,
								"Nonsense data.  Cannot specify collision geometry as well"
								" as no collision flag.  Disabling collgeo.\n");
								
			wlc->pcCollGeo = NULL;
			wlc->bCollision = false;
		}

		if(	wlc->pcCollGeo &&
			!wlc->pCollGeo)
		{
			wlc->pCollGeo = groupModelFind(wlc->pcCollGeo, 0);
			if (wlc->pCollGeo)
			{
				wlc->pCollGeo = modelGetCollModel(wlc->pCollGeo);
			}
			
			if(!wlc->pCollGeo){
				ErrorFilenameGroupf(wlc->pcFileName,
									"Design",
									3,
									"Invalid collision geometry %s for entity %s\n",
									wlc->pcCollGeo,
									ENTDEBUGNAME(e));
									
				wlc->pcCollGeo = NULL;
				wlc->bCollision = false;
			}
		}

		if(wlc->pCollGeo){
			wlc->bCollision = true;
		}

		if(wlc->bCollision){
			MovementBodyDesc* bd;
			
			mmBodyDescCreate(&bd);

			// Fill in collision bounds.

			if(wlc->pCollGeo){
				MovementGeometryDesc	geoDesc = {0};
				MovementGeometry*		geo = NULL;

				geoDesc.geoType = MM_GEO_GROUP_MODEL;
				geoDesc.model.modelName = wlc->pcCollGeo;

				mmGeometryCreate(&geo, &geoDesc);

				mmBodyDescAddGeometry(bd, geo, zerovec3, zerovec3);

				// Single geometry, at unitmat.
				
				copyVec3(wlc->pCollGeo->min, wlc->vCollBoundsMin);
				copyVec3(wlc->pCollGeo->max, wlc->vCollBoundsMax);
			}else{
				// Multiple geometries.
				
				Vec3 boundsMin = {8e16, 8e16, 8e16};
				Vec3 boundsMax = {-8e16, -8e16, -8e16};

				EARRAY_CONST_FOREACH_BEGIN(wlc->eaCostumeParts, i, isize);
				{
					WLCostumePart* cp = wlc->eaCostumeParts[i];
					
					if(	cp->bCollisionOnly &&
						cp->pchGeometry &&
						cp->pcModel)
					{
						if(!cp->pCachedModel){
							cp->pCachedModel = modelFindEx(	cp->pchGeometry,
															cp->pcModel, 
															true,
															WL_FOR_ENTITY);
							if (cp->pCachedModel)
							{
								cp->pCachedModel = modelGetCollModel(cp->pCachedModel);
							}
						}
						
						if(cp->pCachedModel){
							Mat3 transform;
							Vec3 scale;
							Vec3 partMin;
							Vec3 partMax;
							
							mulBoundsAA(cp->pCachedModel->min,
										cp->pCachedModel->max,
										cp->mTransform,
										partMin,
										partMax);
										
							copyMat3(cp->mTransform, transform);
							extractScale(transform, scale);
							
							vec3RunningMin(partMin, boundsMin);
							vec3RunningMax(partMax, boundsMax);
							
							{
								MovementGeometryDesc	geoDesc = {0};
								MovementGeometry*		geo = NULL;
								Vec3					pyr;

								geoDesc.geoType = MM_GEO_WL_MODEL;
								geoDesc.model.fileName = cp->pchGeometry;
								geoDesc.model.modelName = cp->pcModel;
								copyVec3(scale, geoDesc.model.scale);

								mmGeometryCreate(&geo, &geoDesc);

								getMat3YPR(cp->mTransform, pyr);

								mmBodyDescAddGeometry(bd, geo, cp->mTransform[3], pyr);
							}
						}
					}
				}
				EARRAY_FOREACH_END;

				if(	boundsMin[0] > boundsMax[0] ||
					boundsMin[1] > boundsMax[1] ||
					boundsMin[2] > boundsMax[2])
				{
					setVec3same(wlc->vCollBoundsMin, 0);
					setVec3same(wlc->vCollBoundsMax, 0);
					wlc->bCollision = false;
				}else{
					copyVec3(boundsMin, wlc->vCollBoundsMin);
					copyVec3(boundsMax, wlc->vCollBoundsMax);
				}
			}

			{
				MovementBody* b;

				mmBodyCreate(&b, &bd);
			
				mmrBodyCreateFG(e->mm.movement,
								&e->mm.movementBodyHandle,
								b,
								SAFE_MEMBER2(pc, pArtistData, bShellColl),
								SAFE_MEMBER2(pc, pArtistData, bHasOneWayCollision));
			}
		}

		if(wlc->bCollision){
			bCollisionGeo = true;
		}
	}

	if(	pc &&
		!bCollisionGeo)
	{
		PCSkeletonDef*	sd = GET_REF(pc->hSkeleton);
		F32				globalScale = 1.f;

		if(pc->pArtistData && pc->pArtistData->bNoCollision){
			mmNoCollHandleCreateFG(e->mm.movement, &e->mm.mnchCostume, __FILE__, __LINE__);
		}

		if(sd){
			MovementBodyDesc*		bd;
			MovementBody*			b;
			const Capsule*const*	capsulesToCopy = NULL;

			// Overall scale.
			
			if(	sd->fHeightMin > 0.0f &&
				sd->fHeightBase > sd->fHeightMin &&
				sd->fHeightMax > sd->fHeightBase)
			{
				globalScale = pc->fHeight / sd->fHeightBase;
			}

			if(pc->pArtistData && eaSize(&pc->pArtistData->ppCollCapsules)){
				capsulesToCopy = (Capsule **)pc->pArtistData->ppCollCapsules;
			}
			else if(eaSize(&sd->ppCollCapsules)){
				capsulesToCopy = sd->ppCollCapsules;
			}

			mmBodyDescCreate(&bd);

			if(SAFE_MEMBER(pc->pArtistData, pcCollisionGeo)){
				PCGeometryDef *gd = RefSystem_ReferentFromString("CostumeGeometry", pc->pArtistData->pcCollisionGeo);

				if(gd){
					Model* model = modelFindEx(	gd->pcGeometry,
												gd->pcModel,
												true,
												WL_FOR_ENTITY);

					if(model){
						MovementGeometryDesc	geoDesc = {0};
						MovementGeometry*		geo = NULL;

						geoDesc.geoType = MM_GEO_WL_MODEL;
						geoDesc.model.fileName = gd->pcGeometry;
						geoDesc.model.modelName = gd->pcModel;
						copyVec3(unitvec3, geoDesc.model.scale);

						mmGeometryCreate(&geo, &geoDesc);

						mmBodyDescAddGeometry(bd, geo, zerovec3, zerovec3);

						if(wlc){
							copyVec3(model->min, wlc->vCollBoundsMin);
							copyVec3(model->max, wlc->vCollBoundsMax);
						}
					}
				}				
			}

			if(capsulesToCopy){
				EARRAY_CONST_FOREACH_BEGIN(capsulesToCopy, i, isize);
				{
					Capsule c = *capsulesToCopy[i];

					// Apply scale if capsules are from sd.
					
					if(capsulesToCopy == sd->ppCollCapsules){
						c.fLength *= globalScale;
						c.fRadius *= globalScale;
						scaleVec3(c.vStart, globalScale, c.vStart);
					}

					normalVec3(c.vDir);

					mmBodyDescAddCapsule(bd, &c);
				}
				EARRAY_FOREACH_END;
			}else{
				Capsule c = {0};
				
				c.vDir[1] = 1.0f;
				c.vStart[1] = 1.5f * globalScale;
				c.fLength = 3.0f * globalScale;
				c.fRadius = 1.5f * globalScale;

				mmBodyDescAddCapsule(bd, &c);
			}
			
			mmBodyCreate(&b, &bd);
						
			mmrBodyCreateFG(e->mm.movement,
							&e->mm.movementBodyHandle,
							b,
							SAFE_MEMBER2(pc, pArtistData, bShellColl),
							SAFE_MEMBER2(pc, pArtistData, bHasOneWayCollision));

			// Override collRadius if specified
			
			//if(pc->fCollRadius){
			//	MAX1(mm->collRadius, pc->fCollRadius);
			//}
			//else if(sd->fCollRadius){
			//	MAX1(mm->collRadius, sd->fCollRadius * globalScale);
			//}

			if(wlc){
				costumeGenerate_CalculateCostumeExtents(e, wlc);
			}
		}
	}

	PERFINFO_AUTO_STOP();
#endif
}

static void costumeGenerate_PrepareDynFxRefSaveData(DynDrawSkeleton *pDrawSkeleton,
													DynFxRef ***peaSaveData)
{
	eaPushEArray(peaSaveData, &pDrawSkeleton->eaDynFxRefs);
	eaClear(&pDrawSkeleton->eaDynFxRefs);
}

static void costumeGenerate_RestoreDynFxRefSaveData(DynDrawSkeleton *pDrawSkeleton,
													DynFxRef ***peaSaveData)												
{
	FOR_EACH_IN_EARRAY(*peaSaveData, DynFxRef, pFxRef) {
		//make a hard copy, destroy original needs to be done elsewhere
		dynDrawSkeletonHardCopyDynFxRef(pDrawSkeleton, pFxRef);
	} FOR_EACH_END;
	
	FOR_EACH_IN_EARRAY(pDrawSkeleton->eaSubDrawSkeletons, DynDrawSkeleton, pChildSkeleton) {
		costumeGenerate_RestoreDynFxRefSaveData(pChildSkeleton, peaSaveData);
	} FOR_EACH_END;
}

static void costumeGenerate_PrepareDynDrawSkeletonSaveData(	DynDrawSkeleton *pDrawSkeleton,
															DynSkeleton *pSkeleton,
															DynDrawSkeletonSaveData ***peaSaveData)
{
	//make sure the skeleton still exist with new costume
	bool foundIt = false;
	if (pDrawSkeleton->pSkeleton == pSkeleton) {
		foundIt = true;
	} else {
		FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, curSkel)
		{
			if (pDrawSkeleton->pSkeleton == curSkel) {
				foundIt = true;
				break;
			}
		}
		FOR_EACH_END;
	}

	if (foundIt && pDrawSkeleton->pSkeleton->bSavedOnCostumeChange)
	{
		//save the current skeleton's data
		DynDrawSkeletonSaveData *pSaveStruct = StructCreate(parse_DynDrawSkeletonSaveData);
		
		pSaveStruct->pSkeleton = pDrawSkeleton->pSkeleton;

		eaPushEArray(&pSaveStruct->eaSeveredBones, &pDrawSkeleton->eaSeveredBones);
		eaClear(&pDrawSkeleton->eaSeveredBones);

		eaPushEArray(&pSaveStruct->eaDynFxRefs, &pDrawSkeleton->eaDynFxRefs);
		eaClear(&pDrawSkeleton->eaDynFxRefs);
		
		eaPush(peaSaveData, pSaveStruct);
	}

	//process the children
	FOR_EACH_IN_EARRAY(pDrawSkeleton->eaSubDrawSkeletons, DynDrawSkeleton, pChildDrawSkeleton)
	{
		costumeGenerate_PrepareDynDrawSkeletonSaveData(pChildDrawSkeleton, pSkeleton, peaSaveData);
	}
	FOR_EACH_END;
}

static void costumeGenerate_RestoreDynDrawSkeletonSaveData(	DynDrawSkeleton *pDrawSkeleton,
															DynDrawSkeletonSaveData ***peaSaveData)
{
	//find and restore the skeleton's data
	FOR_EACH_IN_EARRAY(*peaSaveData, DynDrawSkeletonSaveData, curSaveData)
	{
		if (curSaveData->pSkeleton == pDrawSkeleton->pSkeleton)
		{
			assert(curSaveData->pSkeleton->bSavedOnCostumeChange);
			curSaveData->pSkeleton->bSavedOnCostumeChange = 0;
			curSaveData->pSkeleton = NULL;

			eaPushEArray(&pDrawSkeleton->eaSeveredBones, &curSaveData->eaSeveredBones);
			eaClear(&curSaveData->eaSeveredBones);

			eaPushEArray(&pDrawSkeleton->eaDynFxRefs, &curSaveData->eaDynFxRefs);
			eaClear(&curSaveData->eaDynFxRefs);

			break;
		}
	}
	FOR_EACH_END;

	//process the children
	FOR_EACH_IN_EARRAY(pDrawSkeleton->eaSubDrawSkeletons, DynDrawSkeleton, pChildDrawSkeleton)
	{
		costumeGenerate_RestoreDynDrawSkeletonSaveData(pChildDrawSkeleton, peaSaveData);
	}
	FOR_EACH_END;
}

static void costume_ConvertPCFXNoPersistTemp(PCFXNoPersist ***peaNoPersist, PCFXTemp ***peaTemp){
	eaClear(peaTemp);
	FOR_EACH_IN_EARRAY_FORWARDS(*peaNoPersist, PCFXNoPersist, pFX)
	{
		PCFXTemp *pTemp = StructAlloc(parse_PCFXTemp);
		pTemp->pcName = allocAddString(pFX->pcName);
		pTemp->fHue = pFX->fHue;
		pTemp->pParams = NULL;
		eaPush(peaTemp, pTemp);
	}
	FOR_EACH_END
}

void costumeGenerate_FixEntityCostume(Entity* e)
{
	F32 fMountScaleOverride;
	PlayerCostume*	pc = costumeEntity_GetEffectiveCostume(e);
	PlayerCostume*	pcMount = costumeEntity_GetMountCostume(e, &fMountScaleOverride);
	WLCostume*		wlc = NULL;
	WLCostume*		wlcMount = NULL;
	WLCostume*		wlOld = NULL;
	bool			player_costume = !!e->pPlayer;
	bool bKeepOldSkeleton = false;
	bool bWasMounted = false;
	bool bDismount = false;
	DynDrawSkeletonSaveData **eaDynDrawSkelSaveData = NULL;
	DynFxRef **eaDynFxRefSaveData = NULL;
	DynSkeleton **eaDynSkelSave = NULL;
	PerfInfoGuard*	piGuard;
	DynFxCreateParams** eaOldAutoRetryFx = NULL;
	CharacterClass* pClass = SAFE_GET_REF2(e, pChar, hClass);

	#ifdef GAMECLIENT
		if(gbNoGraphics){
			return;
		}
	#endif

	if (e->bFakeEntity) {
		return;
	}

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);

	wlOld = GET_REF(e->hWLCostume);

	if (wlOld && wlOld->bMount)
		bWasMounted = true;

	wlOld = NULL;

	// Note that costume memory is freed automatically when last reference is removed
	REMOVE_HANDLE(e->hWLCostume);

	if (e->costumeRef.pcDestructibleObjectCostume)
	{
		wlc = worldInteractionGetWLCostume(e->costumeRef.pcDestructibleObjectCostume);
	}
	else if (!IsServer() && pc)
	{
		WLCostume** eaSubCostumes = NULL;
		WLCostume** eaMountSubCostumes = NULL;
		PCFXTemp**  eaTempFX = NULL;

		eaCreate(&eaTempFX);

		costume_ConvertPCFXNoPersistTemp(&e->costumeRef.eaAdditionalFX, &eaTempFX);
		
		wlc = costumeGenerate_CreateWLCostume(	pc,
												e->pChar ? 
													GET_REF(e->pChar->hSpecies) :
												NULL,
												e->pChar ? 
													GET_REF(e->pChar->hClass) :
												NULL,
												NULL,
												costumeEntity_GetActiveSavedSlotType(e),
												GET_REF(e->costumeRef.hMood),
												&eaTempFX,
												"Entity.",
												e->myEntityType,
												e->myContainerID,// Use container ID
												player_costume,
												&eaSubCostumes);
		if (!pcMount)
			e->costumeRef.bPredictDismount = false;

		if (wlc && pcMount && !e->costumeRef.bPredictDismount) {
			wlcMount = costumeGenerate_CreateWLCostume(	pcMount,
														NULL,
														NULL,
														NULL,
														costumeEntity_GetActiveSavedSlotType(e),
														GET_REF(e->costumeRef.hMood),
														&eaTempFX,
														"EntityMount.",
														e->myEntityType,
														e->myContainerID,// Use container ID
														player_costume,
														&eaMountSubCostumes);

			wlcMount->fMountScaleOverride = fabsf(fMountScaleOverride);
		}
		else if (bWasMounted)
			bDismount = true;

		eaDestroy(&eaTempFX);

		if (wlc)
		{
			
			// First add sub costumes to dictionary and add references to the main costume
			FOR_EACH_IN_EARRAY(eaSubCostumes, WLCostume, pSubCostume)
				wlCostumePushSubCostume(pSubCostume, wlc);
			FOR_EACH_END;

			if (wlcMount)
			{
				FOR_EACH_IN_EARRAY(eaMountSubCostumes, WLCostume, pMountSubCostume)
					wlCostumePushSubCostume(pMountSubCostume, wlcMount);
				FOR_EACH_END;
				wlCostumeAddRiderToMount(wlc, wlcMount);
				wlCostumeAddToDictionary(wlcMount, wlcMount->pcName);
			}
			else
			{
				wlCostumeAddToDictionary(wlc, wlc->pcName);
			}
		}
	}


	if (wlcMount) {
		SET_HANDLE_FROM_REFERENT("Costume", wlcMount, e->hWLCostume);
	} else if (wlc) {
		SET_HANDLE_FROM_REFERENT("Costume", wlc, e->hWLCostume);
	}

	#ifdef GAMECLIENT
	// see if we are not changing the base skeleton, and if so save the sequencers so 
	// the current animation does not go away
	// JE: Not sure why the old animation system was only doing this for players, but the new system
	//   breaks down quite a bit if we don't do this for everything (keyword doesn't stick around like
	//   bits used to, so no new animation is triggered)
	// JE: Don't do this if gCostumeAssetsModified, we need to recreate (at least) DynBouncerUpdaters
	eaCreate(&eaDynSkelSave);
	if (e->dyn.guidSkeleton		&&
		wlc						&&
		!wlcMount				&&
		!gCostumeAssetsModified	&&
		(	e->pPlayer || 
			gConf.bNewAnimationSystem ||
			(pc && pc->bCostumeChangeAlwaysSaveSequencersIfSkelsMatch)))
	{
		DynSkeleton *pOldSkeleton = dynSkeletonFromGuid(e->dyn.guidSkeleton);
		if (pOldSkeleton)
		{
			const DynBaseSkeleton *pOldBaseSkeleton = GET_REF(pOldSkeleton->hBaseSkeleton);
			if(pOldBaseSkeleton)
			{
				const DynBaseSkeleton* pNewBaseSkeleton = wlCostumeGetBaseSkeleton(wlc);
				if (pNewBaseSkeleton == pOldBaseSkeleton)
				{
					pOldSkeleton->bSavedOnCostumeChange = 1;
					bKeepOldSkeleton = true;
					
					//pre-treat the old sub-skeleton parts
					if (eaSize(&pOldSkeleton->eaDependentSkeletons))
					{
						DynSkeleton  **keepDepSkels = NULL; //list of dependent skeletons that will carry over to the new costume
						WLSubCostume **keepSubCosts = NULL; //list of costume parts that have a carried over skeleton
						WLSubCostume **tackSubCosts = NULL; //list of costume parts that do _not_ have a carried over skeleton

						eaCreate(&keepDepSkels);
						eaCreate(&keepSubCosts);
						eaCreate(&tackSubCosts);

						//split & reorder subparts into the correct list, the order of carried over skeletons and costume parts should match in the keep list
						FOR_EACH_IN_EARRAY_FORWARDS(wlc->eaSubCostumes, WLSubCostume, curSubCostumeRef)
						{
							bool foundOne = false;
							WLCostume *curSubCost;
							if (curSubCost = GET_REF(curSubCostumeRef->hSubCostume))
							{
								SkelInfo *curSubCostumeSkelInfo = GET_REF(curSubCost->hSkelInfo);
								if (curSubCostumeSkelInfo)
								{
									FOR_EACH_IN_EARRAY(pOldSkeleton->eaDependentSkeletons, DynSkeleton, curDepSkel)
									{
										if (GET_REF(curSubCostumeSkelInfo->hBaseSkeleton) == GET_REF(curDepSkel->hBaseSkeleton))
										{
											curDepSkel->bSavedOnCostumeChange = 1;
											foundOne = true;
											eaPush(&keepDepSkels, curDepSkel);
											eaPush(&keepSubCosts, curSubCostumeRef);
											eaRemove(&pOldSkeleton->eaDependentSkeletons, icurDepSkelIndex);
											break;
										}
									}
									FOR_EACH_END;
								}
							}
							if (!foundOne) {
								eaPush(&tackSubCosts, curSubCostumeRef);
							}
						}
						FOR_EACH_END;

						//rebuild original sub-costume order
						eaClear(&wlc->eaSubCostumes); //should be relinked in the other two earrays
						eaPushEArray(&wlc->eaSubCostumes, &keepSubCosts);
						eaPushEArray(&wlc->eaSubCostumes, &tackSubCosts);
						eaClear(&keepSubCosts); //moved back to original earray
						eaClear(&tackSubCosts); //moved back to original earray

						//remove extra dependent skeleton parts
						FOR_EACH_IN_EARRAY(pOldSkeleton->eaDependentSkeletons, DynSkeleton, pDelSkel)
						{
							if (pDelSkel->bOwnedByParent) {
								dynSkeletonFreeDependence(pDelSkel);
								dynDependentSkeletonFree(pDelSkel);
							} else {
								eaPush(&eaDynSkelSave, pDelSkel);
							}
						}
						FOR_EACH_END;
						eaClear(&pOldSkeleton->eaDependentSkeletons);

						//rebuild original dependent skeleton parts
						eaPushEArray(&pOldSkeleton->eaDependentSkeletons, &keepDepSkels);
						eaClear(&keepDepSkels);

						eaDestroy(&keepDepSkels);
						eaDestroy(&keepSubCosts);
						eaDestroy(&tackSubCosts);
					}
				}
			}
		}
	}
	#endif

	PERFINFO_AUTO_START("Destroy Skeletons", 1);

	#ifdef GAMECLIENT
	eaCreate(&eaDynFxRefSaveData);
	eaCreate(&eaDynDrawSkelSaveData);
	if (e->dyn.guidDrawSkeleton)
	{
		DynSkeleton     *pCheckSkel = dynSkeletonFromGuid(e->dyn.guidSkeleton);
		DynDrawSkeleton *pDrawSkel  = dynDrawSkeletonFromGuid(e->dyn.guidDrawSkeleton);
		if (pDrawSkel){
			if (wlcMount && !pCheckSkel->bMount ||
				!wlcMount && pCheckSkel->bMount) {
				costumeGenerate_PrepareDynFxRefSaveData(pDrawSkel, &eaDynFxRefSaveData);
			} else {
				costumeGenerate_PrepareDynDrawSkeletonSaveData(pDrawSkel, pCheckSkel, &eaDynDrawSkelSaveData);
			}
			dynDrawSkeletonFree(pDrawSkel);
		}
		e->dyn.guidDrawSkeleton = 0;
	}
	if (!bKeepOldSkeleton && e->dyn.guidSkeleton)
	{
		dtSkeletonDestroy(e->dyn.guidSkeleton);
		e->dyn.guidSkeleton = 0;
	}
	#endif
	
	#ifdef GAMECLIENT
	if (!bKeepOldSkeleton && e->dyn.guidFxMan)
	{
		DynFxManager* pMan = dynFxManFromGuid(e->dyn.guidFxMan);

		mmLog(e->mm.movement, NULL, "Destroying fx manager.");

		eaOldAutoRetryFx = pMan->eaAutoRetryFX;
		pMan->eaAutoRetryFX = NULL;

		dtFxManDestroy(e->dyn.guidFxMan);
		e->dyn.guidFxMan = 0;
	}
	#endif
	PERFINFO_AUTO_STOP();

	if(entIsServer()){
		costumeGenerate_CreateMovementBodyAndDoOtherStuff(e, wlcMount ? wlcMount : wlc, pc);
	}else{
		costumeGenerate_CalculateCostumeExtents(e, wlcMount ? wlcMount : wlc);
	}

	if (!wlc)
	{
		#ifdef GAMECLIENT
		eaDestroy(&eaDynSkelSave);
		eaDestroyEx(&eaDynFxRefSaveData, dynDrawFxRefSaveDataDestroy);
		eaDestroyEx(&eaDynDrawSkelSaveData, dynDrawSkeletonSaveDataDestroy);
		#endif
		PERFINFO_AUTO_STOP_GUARD(&piGuard);
		return;
	}

	// Don't create the fx manager until the costume exists
	// Initialization shared for client/server
	#ifdef GAMECLIENT
	{
		DynDrawSkeleton* pDrawSkel = NULL;
		bool bUpdateDrawInfo = false;

		if(!e->dyn.guidFxMan){

			e->dyn.guidFxMan = dtFxManCreate(entIsLocalPlayer(e) ? eFxManagerType_Player : eFxManagerType_Entity, e->dyn.guidRoot, NULL, entIsLocalPlayer(e), false);

			{
				int i;
				DynFxManager* pFxMan = dynFxManFromGuid(e->dyn.guidFxMan);
				Entity *ePlayer = entActivePlayerPtr();

				if (pFxMan)
				{
					if(ePlayer && e)
					{
						EntityRelation relation = entity_GetRelationEx(entGetPartitionIdx(ePlayer), ePlayer, e, false);
						if(relation == kEntityRelation_Foe){
							pFxMan->bEnemyFaction = true;
						} else {
							pFxMan->bLocalPlayerBased = (ePlayer == e || (e->erCreator == entGetRef(ePlayer)));
						}
					}
					
					for (i = 0; i < eaSize(&eaOldAutoRetryFx); i++)
					{
						eaOldAutoRetryFx[i]->guidFxMan = pFxMan->guid;
					}

					pFxMan->eaAutoRetryFX = eaOldAutoRetryFx;

					pFxMan->bWaitingForSkelUpdate = true;
				}
				else
				{
					for (i = 0; i < eaSize(&eaOldAutoRetryFx); i++)
					{
						dynParamBlockFree(eaOldAutoRetryFx[i]->pParamBlock);
						free(eaOldAutoRetryFx[i]);
					}
					eaDestroy(&eaOldAutoRetryFx);
				}
			}


			mmLog(	e->mm.movement,
					NULL,
					"Created fx manager %d.",
					e->dyn.guidFxMan);
		}

		dtFxManSetCostumeFXHue(e->dyn.guidFxMan,e->fHue);
		
		if (e->pPlayer ||
			(	bKeepOldSkeleton &&
				(	gConf.bNewAnimationSystem ||
					(pc && pc->bCostumeChangeAlwaysSaveSequencersIfSkelsMatch))))
		{
			entClientCreateSkeletonEx(e, bKeepOldSkeleton);
		}

		if (e->pCritter && e->pCritter->pcLootGlowFX)
		{
			dtFxManAddMaintainedFx(e->dyn.guidFxMan, e->pCritter->pcLootGlowFX, NULL, 0, 0, eDynFxSource_HardCoded);
		}

		if (eaSize(&eaDynSkelSave))
		{
			DynSkeleton *pDynSkel = dynSkeletonFromGuid(e->dyn.guidSkeleton);
			eaPushEArray(&pDynSkel->eaDependentSkeletons, &eaDynSkelSave);
			eaClear(&eaDynSkelSave);
		}
		eaDestroy(&eaDynSkelSave);

		if (pDrawSkel = dynDrawSkeletonFromGuid(e->dyn.guidDrawSkeleton))
		{
			if (eaSize(&eaDynDrawSkelSaveData))
			{
				costumeGenerate_RestoreDynDrawSkeletonSaveData(pDrawSkel, &eaDynDrawSkelSaveData);
				bUpdateDrawInfo = true;
			}

			if (eaSize(&eaDynFxRefSaveData))
			{
				costumeGenerate_RestoreDynFxRefSaveData(pDrawSkel, &eaDynFxRefSaveData);
				bUpdateDrawInfo = true;
			}
		}
		eaDestroyEx(&eaDynDrawSkelSaveData, dynDrawSkeletonSaveDataDestroy);
		eaDestroyEx(&eaDynFxRefSaveData, dynDrawFxRefSaveDataDestroy);

		if (bUpdateDrawInfo) {
			dynDrawSkeletonUpdateDrawInfo(pDrawSkel);
		}

		if (bDismount && pcMount && pcMount->pArtistData && pcMount->pArtistData->pDismountFX)
		{
			dtAddFxAutoRetry(e->dyn.guidFxMan, pcMount->pArtistData->pDismountFX->pcName, NULL, 0, 0, 0, 0, eDynFxSource_Costume, NULL, NULL);
		}

		mmResourcesCheckForInvalidStateFG(e->mm.movement);
	}
	#else
	{
		e->dyn.guidSkeleton = 0;
	}
	#endif

	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}