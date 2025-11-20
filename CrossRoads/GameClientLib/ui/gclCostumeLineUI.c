/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#include "CharacterCreationUI.h"
#include "CostumeCommon.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonRandom.h"
#include "CostumeCommonTailor.h"
#include "GameAccountDataCommon.h"
#include "gclBaseStates.h"
#include "gclCostumeLineUI.h"
#include "gclCostumeUI.h"
#include "gclCostumeUIState.h"
#include "gclCostumeUnlockUI.h"
#include "gclEntity.h"
#include "GlobalStateMachine.h"
#include "Guild.h"
#include "Player.h"
#include "ReferenceSystem.h"
#include "species_common.h"
#include "StringCache.h"
#include "UIGen.h"
#include "GameClientLib.h"

#include "gclCostumeUIState_h_ast.h"
#include "gclCostumeLineUI_h_ast.h"
#include "species_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// --------------------------------------------------------------------------
// Type definitions and prototypes
// --------------------------------------------------------------------------

extern bool g_MirrorSelectMode;
extern bool g_GroupSelectMode;
extern bool g_bOmitHasOnlyOne;
extern bool g_bCountNone;


// --------------------------------------------------------------------------
// Utility Functions
// --------------------------------------------------------------------------


void costumeLineUI_DestroyLine(CostumeEditLine *pLine)
{
	// Destroy the array, but not the values in them
	eaDestroy(&pLine->eaTex);
	eaDestroy(&pLine->eaMat);
	eaDestroy(&pLine->eaGeo);
	eaDestroy(&pLine->eaBone);
	eaDestroy(&pLine->eaCat);
	eaDestroy(&pLine->eaRegion);
	eaDestroy(&pLine->eaOverlays);
	eaDestroy(&pLine->eaValues);

	// Do not destroy these since points into other owned arrays
	pLine->eaGuildOverlays = NULL;

	StructDestroy(parse_CostumeEditLine, pLine);

	// These are cleaned up by StructDestroy
	//   displayNameMsg.hMessage
	//   displayNameMsg2.hMessage
	//   hOwnerMat
	//   hOwnerGeo
	//   hOwnerBone
	//   hOwnerCat
	//   hOwnerRegion
}

void costumeLineUI_DestroyLines(CostumeEditLine ***peaLines)
{
	CostumeEditLine *pLine = NULL;
	while (pLine = eaPop(peaLines))
	{
		costumeLineUI_DestroyLine(pLine);
	}
}


static PCColorFlags costumeLineUI_AddTextureLine(
			CostumeEditLine ***eaCostumeEditLine, int lineNum, NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies,
			PCRegion *pRegion, PCCategory *pCategory, PCBoneDef *pBone, PCGeometryDef *pGeometry, PCMaterialDef *pMaterial, 
			CostumeEditLineType eFindTypes, PlayerCostume **eaUnlockedCostumes,
			bool bUnlockAll, bool bLineListHideMirrorBones,
			int texNum, PCColorFlags *required, bool bBoneLine, bool bGeoLine, bool bMatLine)
{
	PCTextureDef **eaTextures = NULL;
	PCTextureDef *pTexture = NULL;
	NOCONST(PCPart) *pPart = NULL;
	//char text[128];
	CostumeEditLine *el = NULL;
	Entity *pEnt = entActivePlayerPtr();
	Guild *pGuild = guild_GetGuild(pEnt);
	bool found = false, copied = false, pushed = false;
	PCColorFlags eColorChoices = 0;
	int size = 0;
	int i, j, match;

	PERFINFO_AUTO_START_FUNC();

	if (eFindTypes & kCostumeEditLineType_Texture0 && texNum == 0)
	{
		costumeTailor_GetValidTextures(pCostume, pMaterial, pSpecies, NULL, NULL /*pBoneGroup*/, pGeometry, NULL, eaUnlockedCostumes, kPCTextureType_Pattern, &eaTextures, false, true, bUnlockAll);

		if (pBone && pBone->bIsGuildEmblemBone)
		{
			//Add Fleet Emblem to the list
			i = -1;
			match = -1;
			if (pGuild && pGuild->pcEmblem && *pGuild->pcEmblem)
			{
				for (i = eaSize(&g_GuildEmblems.eaEmblems)-1; i >= 0; --i)
				{
					const char *pTempTex = REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[i]->hTexture);
					if (!stricmp(pTempTex,pGuild->pcEmblem))
					{
						match = i;
					}
					for (j = eaSize(&eaTextures)-1; j >= 0; --j)
					{
						if (!eaTextures[j]) continue;
						if (!eaTextures[j]->pcName) continue;
						if (j == 0 && !stricmp(eaTextures[j]->pcName, "None")) continue;
						if (!stricmp(pTempTex,eaTextures[j]->pcName))
						{
							eaRemove(&eaTextures, j);
						}
					}
				}
			}
			else
			{
				for (i = eaSize(&g_GuildEmblems.eaEmblems)-1; i >= 0; --i)
				{
					const char *pTempTex = REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[i]->hTexture);
					for (j = eaSize(&eaTextures)-1; j >= 0; --j)
					{
						if (!eaTextures[j]) continue;
						if (!eaTextures[j]->pcName) continue;
						if (j == 0 && !stricmp(eaTextures[j]->pcName, "None")) continue;
						if (!stricmp(pTempTex,eaTextures[j]->pcName))
						{
							eaRemove(&eaTextures, j);
						}
					}
				}
			}
			if (pGuild && ((match >= 0 && !g_GuildEmblems.eaEmblems[match]->bFalse) || ((!pGuild->pcEmblem) || (!*pGuild->pcEmblem))))
			{
				pTexture = RefSystem_ReferentFromString("CostumeTexture", "GuildEmblem");
				if (pTexture)
				{
					if (eaSize(&eaTextures) > 0)
					{
						if ((!eaTextures[0]) || (!eaTextures[0]->pcName) || !stricmp(eaTextures[0]->pcName, "None"))
						{
							if (eaSize(&eaTextures) > 1)
							{
								eaInsert(&eaTextures, pTexture, 1);
							}
							else
							{
								eaPush(&eaTextures, pTexture);
							}
						}
						else
						{
							eaInsert(&eaTextures, pTexture, 0);
						}
					}
					else
					{
						if (!costumeTailor_DoesMaterialRequireType(pGeometry, pMaterial, pSpecies, kPCTextureType_Pattern))
						{
							PCTextureDef *pDef = RefSystem_ReferentFromString(g_hCostumeTextureDict, "None");
							if (pDef) {
								eaPush(&eaTextures, pDef);
							}
						}
						eaPush(&eaTextures, pTexture);
					}
				}
			}
		}

		if (eaSize(&eaTextures) > 0)
		{
			size = eaSize(&eaTextures);
			//if (eaSize(&eaTextures) > 1)
			{
				el = StructCreate(parse_CostumeEditLine);
				//sprintf(text, "Texture0_%d", lineNum);
				el->iType = kCostumeEditLineType_Texture0;
				pPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);
				if (pPart)
				{
					pTexture = GET_REF(pPart->hPatternTexture);

					if (pBone && pBone->bIsGuildEmblemBone && pGuild)
					{
						if (costumeTailor_PartHasGuildEmblem(pPart, pGuild))
						{
							//Select Guild Emblem as currently selected texture if it looks like that is what is selected
							pTexture = RefSystem_ReferentFromString("CostumeTexture", "GuildEmblem");
						}
					}
				}
				if (pBone && GET_REF(pBone->patternFieldDispName.hMessage))
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->patternFieldDispName.hMessage);
				}
				else if (pMaterial)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pMaterial->displayNameMsg.hMessage);
				}
				else if (pGeometry)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pGeometry->displayNameMsg.hMessage);
				}
				else if (pBone && bLineListHideMirrorBones && GET_REF(pBone->hMirrorBone))
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->mergeNameMsg.hMessage);
				}
				else if (pBone)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->displayNameMsg.hMessage);
				}
				else if (pCategory)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pCategory->displayNameMsg.hMessage);
				}
				found = true;
			}
		}
	}
	else if ((eFindTypes & kCostumeEditLineType_Texture1 && texNum == 1) && ((!pBone) || (!pBone->bIsGuildEmblemBone)))
	{
		costumeTailor_GetValidTextures(pCostume, pMaterial, pSpecies, NULL, NULL /*pBoneGroup*/, pGeometry, NULL, eaUnlockedCostumes, kPCTextureType_Detail, &eaTextures, false, true, bUnlockAll);

		if (eaSize(&eaTextures) > 0)
		{
			size = eaSize(&eaTextures);
			//if (eaSize(&eaTextures) > 1)
			{
				el = StructCreate(parse_CostumeEditLine);
				//sprintf(text, "Texture1_%d", lineNum);
				el->iType = kCostumeEditLineType_Texture1;
				pPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);
				if (pPart)
				{
					pTexture = GET_REF(pPart->hDetailTexture);
				}
				if (pBone && GET_REF(pBone->detailFieldDisplayName.hMessage))
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->detailFieldDisplayName.hMessage);
				}
				else if (pMaterial)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pMaterial->displayNameMsg.hMessage);
				}
				else if (pGeometry)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pGeometry->displayNameMsg.hMessage);
				}
				else if (pBone && bLineListHideMirrorBones && GET_REF(pBone->hMirrorBone))
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->mergeNameMsg.hMessage);
				}
				else if (pBone)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->displayNameMsg.hMessage);
				}
				else if (pCategory)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pCategory->displayNameMsg.hMessage);
				}
				found = true;
			}
		}
	}
	else if ((eFindTypes & kCostumeEditLineType_Texture2 && texNum == 2) && ((!pBone) || (!pBone->bIsGuildEmblemBone)))
	{
		costumeTailor_GetValidTextures(pCostume, pMaterial, pSpecies, NULL, NULL /*pBoneGroup*/, pGeometry, NULL, eaUnlockedCostumes, kPCTextureType_Specular, &eaTextures, false, true, bUnlockAll);

		if (eaSize(&eaTextures) > 0)
		{
			size = eaSize(&eaTextures);
			//if (eaSize(&eaTextures) > 1)
			{
				el = StructCreate(parse_CostumeEditLine);
				//sprintf(text, "Texture2_%d", lineNum);
				el->iType = kCostumeEditLineType_Texture2;
				pPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);
				if (pPart)
				{
					pTexture = GET_REF(pPart->hSpecularTexture);
				}
				if (pBone && GET_REF(pBone->specularFieldDisplayName.hMessage))
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->specularFieldDisplayName.hMessage);
				}
				else if (pMaterial)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pMaterial->displayNameMsg.hMessage);
				}
				else if (pGeometry)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pGeometry->displayNameMsg.hMessage);
				}
				else if (pBone && bLineListHideMirrorBones && GET_REF(pBone->hMirrorBone))
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->mergeNameMsg.hMessage);
				}
				else if (pBone)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->displayNameMsg.hMessage);
				}
				else if (pCategory)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pCategory->displayNameMsg.hMessage);
				}
				found = true;
			}
		}
	}
	else if ((eFindTypes & kCostumeEditLineType_Texture3 && texNum == 3) && ((!pBone) || (!pBone->bIsGuildEmblemBone)))
	{
		costumeTailor_GetValidTextures(pCostume, pMaterial, pSpecies, NULL, NULL /*pBoneGroup*/, pGeometry, NULL, eaUnlockedCostumes, kPCTextureType_Diffuse, &eaTextures, false, true, bUnlockAll);

		if (eaSize(&eaTextures) > 0)
		{
			size = eaSize(&eaTextures);
			//if (eaSize(&eaTextures) > 1)
			{
				el = StructCreate(parse_CostumeEditLine);
				//sprintf(text, "Texture3_%d", lineNum);
				el->iType = kCostumeEditLineType_Texture3;
				pPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);
				if (pPart)
				{
					pTexture = GET_REF(pPart->hDiffuseTexture);
				}
				if (pBone && GET_REF(pBone->diffuseFieldDisplayName.hMessage))
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->diffuseFieldDisplayName.hMessage);
				}
				else if (pMaterial)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pMaterial->displayNameMsg.hMessage);
				}
				else if (pGeometry)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pGeometry->displayNameMsg.hMessage);
				}
				else if (pBone && bLineListHideMirrorBones && GET_REF(pBone->hMirrorBone))
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->mergeNameMsg.hMessage);
				}
				else if (pBone)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->displayNameMsg.hMessage);
				}
				else if (pCategory)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pCategory->displayNameMsg.hMessage);
				}
				found = true;
			}
		}
	}
	else if ((eFindTypes & kCostumeEditLineType_Texture4 && texNum == 4) && ((!pBone) || (!pBone->bIsGuildEmblemBone)))
	{
		costumeTailor_GetValidTextures(pCostume, pMaterial, pSpecies, NULL, NULL /*pBoneGroup*/, pGeometry, NULL, eaUnlockedCostumes, kPCTextureType_Movable, &eaTextures, false, true, bUnlockAll);

		if (eaSize(&eaTextures) > 0)
		{
			size = eaSize(&eaTextures);
			//if (eaSize(&eaTextures) > 1)
			{
				el = StructCreate(parse_CostumeEditLine);
				//sprintf(text, "Texture3_%d", lineNum);
				el->iType = kCostumeEditLineType_Texture4;
				pPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);
				if (pPart && pPart->pMovableTexture)
				{
					pTexture = GET_REF(pPart->pMovableTexture->hMovableTexture);
				}
				if (pBone && GET_REF(pBone->movableFieldDisplayName.hMessage))
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->movableFieldDisplayName.hMessage);
				}
				else if (pMaterial)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pMaterial->displayNameMsg.hMessage);
				}
				else if (pGeometry)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pGeometry->displayNameMsg.hMessage);
				}
				else if (pBone && bLineListHideMirrorBones && GET_REF(pBone->hMirrorBone))
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->mergeNameMsg.hMessage);
				}
				else if (pBone)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->displayNameMsg.hMessage);
				}
				else if (pCategory)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pCategory->displayNameMsg.hMessage);
				}
				found = true;
			}
		}
	}

	if (found)
	{
		if (!pTexture)
		{
			pTexture = eaTextures[0];
			el->pCurrentTex = eaTextures[0];
		}
		else
		{
			el->pCurrentTex = pTexture;
		}
		SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, pMaterial, el->hOwnerMat);
		SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, pGeometry, el->hOwnerGeo);
		SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, pBone, el->hOwnerBone);
		SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, pCategory, el->hOwnerCat);
		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, pRegion, el->hOwnerRegion);
		el->pcName = allocAddString(pTexture->pcName);
		el->eaTex = eaTextures;

		if (pTexture && pTexture->pcName && stricmp(pTexture->pcName, "None") && stricmp(pTexture->pcName, "GuildEmblem"))
		{
			el->bColor0Allowed = (!(pTexture->eColorChoices & kPCColorFlags_Color0) ? 0 : 1);
			el->bColor1Allowed = (!(pTexture->eColorChoices & kPCColorFlags_Color1) ? 0 : 1);
			el->bColor2Allowed = (!(pTexture->eColorChoices & kPCColorFlags_Color2) ? 0 : 1);
			el->bColor3Allowed = (!(pTexture->eColorChoices & kPCColorFlags_Color3) ? 0 : 1);
			eColorChoices |= (el->bColor0Allowed ? kPCColorFlags_Color0 : 0);
			eColorChoices |= (el->bColor1Allowed ? kPCColorFlags_Color1 : 0);
			eColorChoices |= (el->bColor2Allowed ? kPCColorFlags_Color2 : 0);
			eColorChoices |= (el->bColor3Allowed ? kPCColorFlags_Color3 : 0);
		}

		if (pTexture->pValueOptions && pTexture->pValueOptions->pcValueConstant)
		{
			if (costumeTailor_IsSliderConstValid(pTexture->pValueOptions->pcValueConstant, pMaterial, pTexture->pValueOptions->iValConstIndex))
			{
				el->bHasSlider = 1;
				//*required |= eColorChoices;
				//eColorChoices = 0;
				//el->bColor0Allowed = 0;
				//el->bColor1Allowed = 0;
				//el->bColor2Allowed = 0;
				//el->bColor3Allowed = 0;
				el->pcName2 = allocAddString(pTexture->pValueOptions->pcValueConstant);
				el->fScaleMin1 = -100.0f;
				el->fScaleMax1 = 100.0f;
				if (pPart)
				{
					if (texNum == 0) el->fScaleValue1 = pPart->pTextureValues ? pPart->pTextureValues->fPatternValue : 0;
					else if (texNum == 1) el->fScaleValue1 = pPart->pTextureValues ? pPart->pTextureValues->fDetailValue : 0;
					else if (texNum == 2) el->fScaleValue1 = pPart->pTextureValues ? pPart->pTextureValues->fSpecularValue : 0;
					else if (texNum == 3) el->fScaleValue1 = pPart->pTextureValues ? pPart->pTextureValues->fDiffuseValue : 0;
					else if (texNum == 4) el->fScaleValue1 = pPart->pMovableTexture ? pPart->pMovableTexture->fMovableValue : 0;
				}
				el->fScaleValue1 = CLAMP(el->fScaleValue1, el->fScaleMin1, el->fScaleMax1);
			}
		}
		if (size != 1 || el->bColor0Allowed || el->bColor1Allowed || el->bColor2Allowed || el->bColor3Allowed || el->bHasSlider)
		{
			eaPush(eaCostumeEditLine, el);
			pushed = true;
		}

		if (eFindTypes & kCostumeEditLineType_Texture4 && texNum == 4)
		{
			if (pTexture->pMovableOptions && pTexture->pMovableOptions->bMovableCanEditPosition)
			{
				if (!pushed)
				{
					eaPush(eaCostumeEditLine, el);
					pushed = true;
				}
				el = StructCreate(parse_CostumeEditLine);
				el->pcName = allocAddString("PositionX");
				el->pcName2 = allocAddString("PositionY");
				SET_HANDLE_FROM_STRING(gMessageDict,"Costume_Costume.Label.PositionX",el->displayNameMsg.hMessage);
				SET_HANDLE_FROM_STRING(gMessageDict,"Costume_Costume.Label.PositionY",el->displayNameMsg2.hMessage);
				SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, pBone, el->hOwnerBone);
				el->iType = kCostumeEditLineType_TextureScale;
				el->bHasSlider = 1;
				if (pPart) el->fScaleValue1 = pPart->pMovableTexture ? pPart->pMovableTexture->fMovableX : 0;
				el->fScaleMin1 = -100;
				el->fScaleMax1 = 100;
				el->fScaleValue1 = CLAMP(el->fScaleValue1, el->fScaleMin1, el->fScaleMax1);
				if (pPart) el->fScaleValue2 = pPart->pMovableTexture ? pPart->pMovableTexture->fMovableY : 0;
				el->fScaleMin2 = -100;
				el->fScaleMax2 = 100;
				el->fScaleValue2 = CLAMP(el->fScaleValue2, el->fScaleMin2, el->fScaleMax2);
				eaPush(eaCostumeEditLine, el);
			}
			if (pTexture->pMovableOptions && pTexture->pMovableOptions->bMovableCanEditScale)
			{
				if (!pushed)
				{
					eaPush(eaCostumeEditLine, el);
					pushed = true;
				}
				el = StructCreate(parse_CostumeEditLine);
				el->pcName = allocAddString("ScaleX");
				el->pcName2 = allocAddString("ScaleY");
				SET_HANDLE_FROM_STRING(gMessageDict,"Costume_Costume.Label.ScaleX",el->displayNameMsg.hMessage);
				SET_HANDLE_FROM_STRING(gMessageDict,"Costume_Costume.Label.ScaleY",el->displayNameMsg2.hMessage);
				SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, pBone, el->hOwnerBone);
				el->iType = kCostumeEditLineType_TextureScale;
				el->bHasSlider = 1;
				if (pPart) el->fScaleValue1 = pPart->pMovableTexture ? pPart->pMovableTexture->fMovableScaleX : 0;
				el->fScaleMin1 = 0;
				el->fScaleMax1 = 100;
				el->fScaleValue1 = CLAMP(el->fScaleValue1, el->fScaleMin1, el->fScaleMax1);
				if (pPart) el->fScaleValue2 = pPart->pMovableTexture ? pPart->pMovableTexture->fMovableScaleY : 0;
				el->fScaleMin2 = 0;
				el->fScaleMax2 = 100;
				el->fScaleValue2 = CLAMP(el->fScaleValue2, el->fScaleMin2, el->fScaleMax2);
				eaPush(eaCostumeEditLine, el);
			}
			if (pTexture->pMovableOptions && pTexture->pMovableOptions->bMovableCanEditRotation)
			{
				if (!pushed)
				{
					eaPush(eaCostumeEditLine, el);
					pushed = true;
				}
				el = StructCreate(parse_CostumeEditLine);
				el->pcName = allocAddString("Rotation");
				SET_HANDLE_FROM_STRING(gMessageDict,"Costume_Costume.Label.Rotation",el->displayNameMsg.hMessage);
				SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, pBone, el->hOwnerBone);
				el->iType = kCostumeEditLineType_TextureScale;
				el->bHasSlider = 0;
				if (pPart) el->fScaleValue1 = pPart->pMovableTexture ? pPart->pMovableTexture->fMovableRotation : 0;
				el->fScaleMin1 = 0;
				el->fScaleMax1 = 100;
				el->fScaleValue1 = CLAMP(el->fScaleValue1, el->fScaleMin1, el->fScaleMax1);
				eaPush(eaCostumeEditLine, el);
			}
		}

		if (!pushed)
		{
			eaDestroy(&eaTextures);
			el->eaTex = NULL;
			costumeLineUI_DestroyLine(el);
		}
	}
	else if (!copied)
	{
		eaDestroy(&eaTextures);
	}

	PERFINFO_AUTO_STOP_FUNC();
	return eColorChoices;
}


static PCColorFlags costumeLineUI_AddMaterialLine(
			CostumeEditLine ***eaCostumeEditLine, int lineNum, NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies,
			PCRegion *pRegion, PCCategory *pCategory, PCBoneDef *pBone, PCGeometryDef *pGeometry, 
			CostumeEditLineType eFindTypes, PlayerCostume **eaUnlockedCostumes,
			bool bUnlockAll, bool bCombineLines, bool bLineListHideMirrorBones, bool bTextureLinesForCurrentPartValuesOnly,
			PCColorFlags *required, bool bBoneLine, bool bGeoLine)
{
	PCMaterialDef **eaMaterials = NULL;
	PCMaterialDef *pMaterial = NULL;
	NOCONST(PCPart) *pPart = NULL;
	Entity *pEnt = entActivePlayerPtr();
	Guild *pGuild = guild_GetGuild(pEnt);
	PCColorFlags eColorChoices = 0;
	//char text[128];
	CostumeEditLine *el;
	int l, pos;
	//int i;
	bool found, copied = false;
	PERFINFO_AUTO_START_FUNC();

	if ((eFindTypes & kCostumeEditLineType_Material) && ((!pBone) || (!pBone->bIsGuildEmblemBone)))
	{
		found = false;
		costumeTailor_GetValidMaterials(pCostume, pGeometry, pSpecies, NULL, NULL /*pBoneGroup*/, eaUnlockedCostumes, &eaMaterials, false, true, bUnlockAll);

		if (eaSize(&eaMaterials) > 0)
		{
			//if (eaSize(&eaMaterials) > 1)
			{
				el = StructCreate(parse_CostumeEditLine);
				el->iType = kCostumeEditLineType_Material;
				pPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);
				if (pPart)
				{
					pMaterial = GET_REF(pPart->hMatDef);
				}
				if (!pMaterial)
				{
					pMaterial = eaMaterials[0];
					el->pCurrentMat = eaMaterials[0];
				}
				else
				{
					el->pCurrentMat = pMaterial;
				}
				SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, pGeometry, el->hOwnerGeo);
				SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, pBone, el->hOwnerBone);
				SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, pCategory, el->hOwnerCat);
				SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, pRegion, el->hOwnerRegion);
				if (pBone && GET_REF(pBone->materialFieldDispName.hMessage))
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->materialFieldDispName.hMessage);
				}
				else if (pGeometry)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pGeometry->displayNameMsg.hMessage);
				}
				else if (pBone && bLineListHideMirrorBones && GET_REF(pBone->hMirrorBone))
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->mergeNameMsg.hMessage);
				}
				else if (pBone)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pBone->displayNameMsg.hMessage);
				}
				else if (pCategory)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pCategory->displayNameMsg.hMessage);
				}
				el->pcName = allocAddString(pMaterial->pcName);
				el->eaMat = eaMaterials;
				pos = eaSize(eaCostumeEditLine);
				eaPush(eaCostumeEditLine, el);
				found = true;
			}

			if (pMaterial && pMaterial->pcName && stricmp(pMaterial->pcName, "None"))
			{
				eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, lineNum*100, pCostume, pSpecies,
						pRegion, pCategory, pBone, pGeometry, pMaterial, 
						eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
						0, required, bBoneLine, bGeoLine, found);
				eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, lineNum*100, pCostume, pSpecies,
						pRegion, pCategory, pBone, pGeometry, pMaterial, 
						eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
						1, required, bBoneLine, bGeoLine, found);
				eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, lineNum*100, pCostume, pSpecies,
						pRegion, pCategory, pBone, pGeometry, pMaterial, 
						eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
						2, required, bBoneLine, bGeoLine, found);
				eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, lineNum*100, pCostume, pSpecies, 
						pRegion, pCategory, pBone, pGeometry, pMaterial, 
						eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
						3, required, bBoneLine, bGeoLine, found);
				eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, lineNum*100, pCostume, pSpecies,
						pRegion, pCategory, pBone, pGeometry, pMaterial, 
						eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
						4, required, bBoneLine, bGeoLine, found);

				if (found)
				{
					el->bColor0Allowed = ((eColorChoices & kPCColorFlags_Color0) || !((pMaterial->eColorChoices & kPCColorFlags_Color0) || (*required & kPCColorFlags_Color0)) ? 0 : 1);
					el->bColor1Allowed = ((eColorChoices & kPCColorFlags_Color1) || !((pMaterial->eColorChoices & kPCColorFlags_Color1) || (*required & kPCColorFlags_Color1)) ? 0 : 1);
					el->bColor2Allowed = ((eColorChoices & kPCColorFlags_Color2) || !((pMaterial->eColorChoices & kPCColorFlags_Color2) || (*required & kPCColorFlags_Color2)) ? 0 : 1);
					el->bColor3Allowed = ((eColorChoices & kPCColorFlags_Color3) || !((pMaterial->eColorChoices & kPCColorFlags_Color3) || (*required & kPCColorFlags_Color3)) ? 0 : 1);
					*required = 0;
					eColorChoices |= (el->bColor0Allowed ? kPCColorFlags_Color0 : 0);
					eColorChoices |= (el->bColor1Allowed ? kPCColorFlags_Color1 : 0);
					eColorChoices |= (el->bColor2Allowed ? kPCColorFlags_Color2 : 0);
					eColorChoices |= (el->bColor3Allowed ? kPCColorFlags_Color3 : 0);
				}
			}
			else
			{
				el->bColor0Allowed = (!(*required & kPCColorFlags_Color0) ? 0 : 1);
				el->bColor1Allowed = (!(*required & kPCColorFlags_Color1) ? 0 : 1);
				el->bColor2Allowed = (!(*required & kPCColorFlags_Color2) ? 0 : 1);
				el->bColor3Allowed = (!(*required & kPCColorFlags_Color3) ? 0 : 1);
				*required = 0;
				eColorChoices |= (el->bColor0Allowed ? kPCColorFlags_Color0 : 0);
				eColorChoices |= (el->bColor1Allowed ? kPCColorFlags_Color1 : 0);
				eColorChoices |= (el->bColor2Allowed ? kPCColorFlags_Color2 : 0);
				eColorChoices |= (el->bColor3Allowed ? kPCColorFlags_Color3 : 0);
			}

			if (eaSize(&eaMaterials) == 1 && el->bColor0Allowed == 0 && el->bColor1Allowed == 0 && el->bColor2Allowed == 0 && el->bColor3Allowed == 0 && el->bHasSlider == 0)
			{
				el->eaMat = NULL; // Gets destroyed later
				costumeLineUI_DestroyLine(el);
				eaRemove(eaCostumeEditLine, pos);
				found = false;
			}
			else if (bCombineLines && pos + 1 == eaSize(eaCostumeEditLine) - 1)
			{
				// Try to combine if there is only one added option
				int iMergePos = pos + 1;
				CostumeEditLine *elNext = (*eaCostumeEditLine)[iMergePos];
				if (GET_REF(elNext->hOwnerBone) == GET_REF(el->hOwnerBone) && el->iType != elNext->iType && elNext->iMergeType == kCostumeEditLineType_Invalid)
				{
					bool bMerged = false;
					switch (elNext->iType)
					{
					xcase kCostumeEditLineType_Texture4:
					case kCostumeEditLineType_Texture0:
					case kCostumeEditLineType_Texture1:
					case kCostumeEditLineType_Texture2:
					case kCostumeEditLineType_Texture3:
						if (!elNext->bHasSlider && (eaSize(&elNext->eaTex) < 2 || eaSize(&el->eaMat) < 2))
						{
							el->eaTex = elNext->eaTex;
							elNext->eaTex = NULL;
							el->pcName2 = elNext->pcName2;
							elNext->pcName2 = NULL;
							el->iMergeType = elNext->iType;
							COPY_HANDLE(el->displayNameMsg2.hMessage, elNext->displayNameMsg.hMessage);
							el->pCurrentTex = elNext->pCurrentTex;
							el->bColor0Allowed |= elNext->bColor0Allowed;
							el->bColor1Allowed |= elNext->bColor1Allowed;
							el->bColor2Allowed |= elNext->bColor2Allowed;
							el->bColor3Allowed |= elNext->bColor3Allowed;
							bMerged = true;
						}
					}
					if (bMerged)
					{
						costumeLineUI_DestroyLine(elNext);
						eaRemove(eaCostumeEditLine, iMergePos);
					}
				}
			}
		}
		if ((!found) && (!copied)) eaDestroy(&eaMaterials);
	}
	else if ((eFindTypes & (kCostumeEditLineType_Texture0|kCostumeEditLineType_Texture1|kCostumeEditLineType_Texture2|kCostumeEditLineType_Texture3|kCostumeEditLineType_Texture4)))
	{
		eaMaterials = NULL;
		if (bTextureLinesForCurrentPartValuesOnly)
		{
			pPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);
			if (pPart && GET_REF(pPart->hMatDef) && GET_REF(pPart->hGeoDef))
			{
				if ((eFindTypes & kCostumeEditLineType_Texture0))
				{
					eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, lineNum*100, pCostume, pSpecies,
							pRegion, pCategory, pBone, pGeometry, GET_REF(pPart->hMatDef), 
							eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
							0, required, bBoneLine, bGeoLine, false);
				}
				if ((eFindTypes & kCostumeEditLineType_Texture1))
				{
					eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, lineNum*100, pCostume, pSpecies,
							pRegion, pCategory, pBone, pGeometry, GET_REF(pPart->hMatDef), 
							eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
							1, required, bBoneLine, bGeoLine, false);
				}
				if ((eFindTypes & kCostumeEditLineType_Texture2))
				{
					eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, lineNum*100, pCostume, pSpecies,
							pRegion, pCategory, pBone, pGeometry, GET_REF(pPart->hMatDef), 
							eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
							2, required, bBoneLine, bGeoLine, false);
				}
				if ((eFindTypes & kCostumeEditLineType_Texture3))
				{
					eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, lineNum*100, pCostume, pSpecies,
							pRegion, pCategory, pBone, pGeometry, GET_REF(pPart->hMatDef), 
							eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
							3, required, bBoneLine, bGeoLine, false);
				}
				if ((eFindTypes & kCostumeEditLineType_Texture4))
				{
					eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, lineNum*100, pCostume, pSpecies,
							pRegion, pCategory, pBone, pGeometry, GET_REF(pPart->hMatDef), 
							eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
							4, required, bBoneLine, bGeoLine, false);
				}
			}
			else
			{
				costumeTailor_GetValidMaterials(pCostume, pGeometry, pSpecies, NULL, NULL /*pBoneGroup*/, eaUnlockedCostumes, &eaMaterials, false, true, bUnlockAll);
			}
		}
		else
		{
			costumeTailor_GetValidMaterials(pCostume, pGeometry, pSpecies, NULL, NULL /*pBoneGroup*/, eaUnlockedCostumes, &eaMaterials, false, true, bUnlockAll);
		}

		for (l = 0; l < eaSize(&eaMaterials); ++l)
		{
			if ((eFindTypes & kCostumeEditLineType_Texture0))
			{
				eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, l+(lineNum*100), pCostume, pSpecies,
						pRegion, pCategory, pBone, pGeometry, eaMaterials[l], 
						eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
						0, required, bBoneLine, bGeoLine, false);
			}
			if ((eFindTypes & kCostumeEditLineType_Texture1))
			{
				eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, l+(lineNum*100), pCostume, pSpecies,
						pRegion, pCategory, pBone, pGeometry, eaMaterials[l], 
						eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
						1, required, bBoneLine, bGeoLine, false);
			}
			if ((eFindTypes & kCostumeEditLineType_Texture2))
			{
				eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, l+(lineNum*100), pCostume, pSpecies,
						pRegion, pCategory, pBone, pGeometry, eaMaterials[l], 
						eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
						2, required, bBoneLine, bGeoLine, false);
			}
			if ((eFindTypes & kCostumeEditLineType_Texture3))
			{
				eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, l+(lineNum*100), pCostume, pSpecies,
						pRegion, pCategory, pBone, pGeometry, eaMaterials[l], 
						eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
						3, required, bBoneLine, bGeoLine, false);
			}
			if ((eFindTypes & kCostumeEditLineType_Texture4))
			{
				eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, l+(lineNum*100), pCostume, pSpecies,
						pRegion, pCategory, pBone, pGeometry, eaMaterials[l], 
						eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
						4, required, bBoneLine, bGeoLine, false);
			}
		}
		eaDestroy(&eaMaterials);
	}

	PERFINFO_AUTO_STOP_FUNC();
	return eColorChoices;
}


static PCColorFlags costumeLineUI_AddGeometryLine(
			CostumeEditLine ***eaCostumeEditLine, int lineNum, NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies, 
			PCRegion *pRegion, PCCategory *pCategory, PCBoneDef *pBone, 
			CostumeEditLineType eFindTypes, PlayerCostume **eaUnlockedCostumes, 
			bool bLineListHideMirrorBones, bool bUnlockAll, bool bCombineLines,
			bool bTextureLinesForCurrentPartValuesOnly,
			PCColorFlags *required, bool bBoneLine)
{
	PCGeometryDef **eaGeometries = NULL;
	PCMaterialDef **eaMaterials = NULL;
	PCGeometryDef *pGeometry = NULL;
	NOCONST(PCPart) *pPart = NULL;
	Entity *pEnt = entActivePlayerPtr();
	Guild *pGuild = guild_GetGuild(pEnt);
	PCColorFlags eColorChoices = 0;
	//char text[128];
	CostumeEditLine *el;
	int k, l, pos;
	//int i;
	bool found, copied = false;
	PERFINFO_AUTO_START_FUNC();

	if ((eFindTypes & kCostumeEditLineType_Geometry) && ((!pBone) || (!pBone->bIsGuildEmblemBone)))
	{
		found = false;
		costumeTailor_GetValidGeos(pCostume, GET_REF(pCostume->hSkeleton), pBone, pCategory, pSpecies, eaUnlockedCostumes, &eaGeometries, false, false /*g_GroupSelectMode && pPart->iBoneGroupIndex >= 0*/, true, bUnlockAll);

		if (eaSize(&eaGeometries) > 0)
		{
			//if (eaSize(&eaGeometries) > 1)
			{
				el = StructCreate(parse_CostumeEditLine);
				el->iType = kCostumeEditLineType_Geometry;
				pPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);
				if (pPart)
				{
					pGeometry = GET_REF(pPart->hGeoDef);
				}
				if (!pGeometry)
				{
					pGeometry = eaGeometries[0];
					el->pCurrentGeo = eaGeometries[0];
				}
				else
				{
					el->pCurrentGeo = pGeometry;
				}
				if (pBone)
				{
					if (GET_REF(pBone->geometryFieldDispName.hMessage))
					{
						COPY_HANDLE(el->displayNameMsg.hMessage,pBone->geometryFieldDispName.hMessage);
					}
					else if (bLineListHideMirrorBones && GET_REF(pBone->hMirrorBone))
					{
						COPY_HANDLE(el->displayNameMsg.hMessage,pBone->mergeNameMsg.hMessage);
					}
					else
					{
						COPY_HANDLE(el->displayNameMsg.hMessage,pBone->displayNameMsg.hMessage);
					}
				}
				else if (pCategory)
				{
					COPY_HANDLE(el->displayNameMsg.hMessage,pCategory->displayNameMsg.hMessage);
				}
				el->pcName = allocAddString(pGeometry->pcName);
				SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, pBone, el->hOwnerBone);
				SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, pCategory, el->hOwnerCat);
				SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, pRegion, el->hOwnerRegion);
				el->eaGeo = eaGeometries;
				pos = eaSize(eaCostumeEditLine);
				eaPush(eaCostumeEditLine, el);
				found = true;
			}

			if (pGeometry && pGeometry->pcName && stricmp(pGeometry->pcName, "None"))
			{
				eColorChoices |= costumeLineUI_AddMaterialLine(eaCostumeEditLine, lineNum*100, pCostume, pSpecies,
						pRegion, pCategory, pBone, pGeometry, 
						eFindTypes, eaUnlockedCostumes, bUnlockAll, bCombineLines, bLineListHideMirrorBones, bTextureLinesForCurrentPartValuesOnly,
						required, bBoneLine, found);

				if (found)
				{
					el->bColor0Allowed = ((eColorChoices & kPCColorFlags_Color0) || !((pGeometry->eColorChoices & kPCColorFlags_Color0) || (*required & kPCColorFlags_Color0)) ? 0 : 1);
					el->bColor1Allowed = ((eColorChoices & kPCColorFlags_Color1) || !((pGeometry->eColorChoices & kPCColorFlags_Color1) || (*required & kPCColorFlags_Color1)) ? 0 : 1);
					el->bColor2Allowed = ((eColorChoices & kPCColorFlags_Color2) || !((pGeometry->eColorChoices & kPCColorFlags_Color2) || (*required & kPCColorFlags_Color2)) ? 0 : 1);
					el->bColor3Allowed = ((eColorChoices & kPCColorFlags_Color3) || !((pGeometry->eColorChoices & kPCColorFlags_Color3) || (*required & kPCColorFlags_Color3)) ? 0 : 1);
					*required = 0;
					eColorChoices |= (el->bColor0Allowed ? kPCColorFlags_Color0 : 0);
					eColorChoices |= (el->bColor1Allowed ? kPCColorFlags_Color1 : 0);
					eColorChoices |= (el->bColor2Allowed ? kPCColorFlags_Color2 : 0);
					eColorChoices |= (el->bColor3Allowed ? kPCColorFlags_Color3 : 0);
				}
			}
			else
			{
				el->bColor0Allowed = (!(*required & kPCColorFlags_Color0) ? 0 : 1);
				el->bColor1Allowed = (!(*required & kPCColorFlags_Color1) ? 0 : 1);
				el->bColor2Allowed = (!(*required & kPCColorFlags_Color2) ? 0 : 1);
				el->bColor3Allowed = (!(*required & kPCColorFlags_Color3) ? 0 : 1);
				*required = 0;
				eColorChoices |= (el->bColor0Allowed ? kPCColorFlags_Color0 : 0);
				eColorChoices |= (el->bColor1Allowed ? kPCColorFlags_Color1 : 0);
				eColorChoices |= (el->bColor2Allowed ? kPCColorFlags_Color2 : 0);
				eColorChoices |= (el->bColor3Allowed ? kPCColorFlags_Color3 : 0);
			}

			if (eaSize(&eaGeometries) == 1 && el->bColor0Allowed == 0 && el->bColor1Allowed == 0 && el->bColor2Allowed == 0 && el->bColor3Allowed == 0 && el->bHasSlider == 0)
			{
				el->eaGeo = NULL; // Destroyed later
				costumeLineUI_DestroyLine(el);
				eaRemove(eaCostumeEditLine, pos);
				found = false;
			}
			else if (bCombineLines && pos + 1 == eaSize(eaCostumeEditLine) - 1)
			{
				// Try to combine if there is only one added option
				int iMergePos = eaSize(eaCostumeEditLine) - 1;
				CostumeEditLine *elNext = (*eaCostumeEditLine)[iMergePos];
				if (GET_REF(elNext->hOwnerBone) == GET_REF(el->hOwnerBone) && el->iType != elNext->iType && elNext->iMergeType == kCostumeEditLineType_Invalid)
				{
					bool bMerged = false;
					switch (elNext->iType)
					{
					xcase kCostumeEditLineType_Material:
						if (!elNext->bHasSlider && (eaSize(&elNext->eaMat) < 2 || eaSize(&el->eaGeo) < 2))
						{
							el->eaMat = elNext->eaMat;
							elNext->eaMat = NULL;
							el->iMergeType = elNext->iType;
							COPY_HANDLE(el->displayNameMsg2.hMessage, elNext->displayNameMsg.hMessage);
							el->pCurrentMat = elNext->pCurrentMat;
							el->bColor0Allowed |= elNext->bColor0Allowed;
							el->bColor1Allowed |= elNext->bColor1Allowed;
							el->bColor2Allowed |= elNext->bColor2Allowed;
							el->bColor3Allowed |= elNext->bColor3Allowed;
							bMerged = true;
						}
					xcase kCostumeEditLineType_Texture4:
					case kCostumeEditLineType_Texture0:
					case kCostumeEditLineType_Texture1:
					case kCostumeEditLineType_Texture2:
					case kCostumeEditLineType_Texture3:
						if (!elNext->bHasSlider && (eaSize(&elNext->eaTex) < 2 || eaSize(&el->eaMat) < 2))
						{
							el->eaTex = elNext->eaTex;
							elNext->eaTex = NULL;
							el->iMergeType = elNext->iType;
							COPY_HANDLE(el->displayNameMsg2.hMessage, elNext->displayNameMsg.hMessage);
							el->pCurrentTex = elNext->pCurrentTex;
							el->bColor0Allowed |= elNext->bColor0Allowed;
							el->bColor1Allowed |= elNext->bColor1Allowed;
							el->bColor2Allowed |= elNext->bColor2Allowed;
							el->bColor3Allowed |= elNext->bColor3Allowed;
							bMerged = true;
						}
					}
					if (bMerged)
					{
						costumeLineUI_DestroyLine(elNext);
						eaRemove(eaCostumeEditLine, iMergePos);
					}
				}
			}
		}
		if ((!found) && (!copied)) eaDestroy(&eaGeometries);
	}
	else if (eFindTypes & kCostumeEditLineType_Material)
	{
		eaGeometries = NULL;
		costumeTailor_GetValidGeos(pCostume, GET_REF(pCostume->hSkeleton), pBone, pCategory, pSpecies, eaUnlockedCostumes, &eaGeometries, false, false /*g_GroupSelectMode && pPart->iBoneGroupIndex >= 0*/, true, bUnlockAll);
		for (k = 0; k < eaSize(&eaGeometries); ++k)
		{
			eColorChoices |= costumeLineUI_AddMaterialLine(eaCostumeEditLine, k+(lineNum*100), pCostume, pSpecies,
						pRegion, pCategory, pBone, eaGeometries[k], 
						eFindTypes, eaUnlockedCostumes, bUnlockAll, bCombineLines, bLineListHideMirrorBones, bTextureLinesForCurrentPartValuesOnly,
						required, bBoneLine, false);
		}
		eaDestroy(&eaGeometries);
	}
	else if ((eFindTypes & (kCostumeEditLineType_Texture0|kCostumeEditLineType_Texture1|kCostumeEditLineType_Texture2|kCostumeEditLineType_Texture3|kCostumeEditLineType_Texture4)))
	{
		eaGeometries = NULL;
		eaMaterials = NULL;
		if (bTextureLinesForCurrentPartValuesOnly)
		{
			pPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);
			if (pPart && GET_REF(pPart->hMatDef) && GET_REF(pPart->hGeoDef))
			{
				if ((eFindTypes & kCostumeEditLineType_Texture0))
				{
					eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, lineNum*100*100, pCostume, pSpecies,
							pRegion, pCategory, pBone, GET_REF(pPart->hGeoDef), GET_REF(pPart->hMatDef), 
							eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
							0, required, bBoneLine, false, false);
				}
				if ((eFindTypes & kCostumeEditLineType_Texture1))
				{
					eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, lineNum*100*100, pCostume, pSpecies,
							pRegion, pCategory, pBone, GET_REF(pPart->hGeoDef), GET_REF(pPart->hMatDef), 
							eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
							1, required, bBoneLine, false, false);
				}
				if ((eFindTypes & kCostumeEditLineType_Texture2))
				{
					eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, lineNum*100*100, pCostume, pSpecies,
							pRegion, pCategory, pBone, GET_REF(pPart->hGeoDef), GET_REF(pPart->hMatDef), 
							eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
							2, required, bBoneLine, false, false);
				}
				if ((eFindTypes & kCostumeEditLineType_Texture3))
				{
					eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, lineNum*100*100, pCostume, pSpecies,
							pRegion, pCategory, pBone, GET_REF(pPart->hGeoDef), GET_REF(pPart->hMatDef), 
							eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
							3, required, bBoneLine, false, false);
				}
				if ((eFindTypes & kCostumeEditLineType_Texture4)) {
					eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, lineNum*100*100, pCostume, pSpecies,
							pRegion, pCategory, pBone, GET_REF(pPart->hGeoDef), GET_REF(pPart->hMatDef), 
							eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
							4, required, bBoneLine, false, false);
				}
			}
			else
			{
				costumeTailor_GetValidGeos(pCostume, GET_REF(pCostume->hSkeleton), pBone, pCategory, pSpecies, eaUnlockedCostumes, &eaGeometries, false, false /*g_GroupSelectMode && pPart->iBoneGroupIndex >= 0*/, true, bUnlockAll);
			}
		}
		else
		{
			costumeTailor_GetValidGeos(pCostume, GET_REF(pCostume->hSkeleton), pBone, pCategory, pSpecies, eaUnlockedCostumes, &eaGeometries, false, false /*g_GroupSelectMode && pPart->iBoneGroupIndex >= 0*/, true, bUnlockAll);
		}

		for (k = 0; k < eaSize(&eaGeometries); ++k)
		{
			costumeTailor_GetValidMaterials(pCostume, eaGeometries[k], pSpecies, NULL, NULL /*pBoneGroup*/, eaUnlockedCostumes, &eaMaterials, false, true, bUnlockAll);
			for (l = 0; l < eaSize(&eaMaterials); ++l)
			{
				if ((eFindTypes & kCostumeEditLineType_Texture0))
				{
					eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, l+((k+(lineNum*100))*100), pCostume, pSpecies,
							pRegion, pCategory, pBone, eaGeometries[k], eaMaterials[l], 
							eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
							0, required, bBoneLine, false, false);
				}
				if ((eFindTypes & kCostumeEditLineType_Texture1))
				{
					eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, l+((k+(lineNum*100))*100), pCostume, pSpecies,
							pRegion, pCategory, pBone, eaGeometries[k], eaMaterials[l], 
							eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
							1, required, bBoneLine, false, false);
				}
				if ((eFindTypes & kCostumeEditLineType_Texture2))
				{
					eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, l+((k+(lineNum*100))*100), pCostume, pSpecies,
							pRegion, pCategory, pBone, eaGeometries[k], eaMaterials[l], 
							eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
							2, required, bBoneLine, false, false);
				}
				if ((eFindTypes & kCostumeEditLineType_Texture3))
				{
					eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, l+((k+(lineNum*100))*100), pCostume, pSpecies,
							pRegion, pCategory, pBone, eaGeometries[k], eaMaterials[l], 
							eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
							3, required, bBoneLine, false, false);
				}
				if ((eFindTypes & kCostumeEditLineType_Texture4))
				{
					eColorChoices |= costumeLineUI_AddTextureLine(eaCostumeEditLine, l+((k+(lineNum*100))*100), pCostume, pSpecies,
							pRegion, pCategory, pBone, eaGeometries[k], eaMaterials[l], 
							eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
							4, required, bBoneLine, false, false);
				}
			}
		}
		eaDestroy(&eaMaterials);
		eaDestroy(&eaGeometries);
	}

	PERFINFO_AUTO_STOP_FUNC();
	return eColorChoices;
}


static PCBoneDef *costumeLineUI_FindSelectedBone(PCRegion *pRegion, PCCategory *pCategory, CostumeEditLine **eaCostumEditLines)
{
	bool foundRegion = false;
	bool foundCategory = false;
	int i;

	PERFINFO_AUTO_START_FUNC();

	if (!pRegion) {
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}
	for (i = eaSize(&eaCostumEditLines)-1; i >= 0; --i)
	{
		if (!foundRegion)
		{
			if (eaCostumEditLines[i]->iType == kCostumeEditLineType_Region)
			{
				if (!strcmp(eaCostumEditLines[i]->pcName,pRegion->pcName))
				{
					foundRegion = true;
				}
			}
		}
		else if (!foundCategory)
		{
			if (eaCostumEditLines[i]->iType == kCostumeEditLineType_Region) return NULL;
			if (eaCostumEditLines[i]->iType == kCostumeEditLineType_Category)
			{
				if (pCategory && !strcmp(eaCostumEditLines[i]->pcName,pCategory->pcName))
				{
					foundCategory = true;
				}
			}
		}
		else
		{
			if (eaCostumEditLines[i]->iType == kCostumeEditLineType_Region) return NULL;
			if (eaCostumEditLines[i]->iType == kCostumeEditLineType_Category) return NULL;
		}

		if (foundRegion && (foundCategory || !pCategory) && eaCostumEditLines[i]->iType == kCostumeEditLineType_Bone)
		{
			PERFINFO_AUTO_STOP_FUNC();
			return eaCostumEditLines[i]->pCurrentBone;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
	return NULL;
}


static void costumeLineUI_UpdateBodyScales(
						CostumeEditLine ***eaCostumeEditLine, NOCONST(PlayerCostume) *pCostume, 
						SpeciesDef *pSpecies, PCSkeletonDef *pSkeleton, PCSlotType *pSlotType,
						PCBodyScaleInfo **eaBodyScalesInclude, PCBodyScaleInfo **eaBodyScalesExclude)
{
	int i, j, k, index;
	CostumeEditLine *el;
	PCBodyScaleInfo **temp = NULL;
	PERFINFO_AUTO_START_FUNC();

	costumeTailor_GetValidBodyScales(pCostume, pSpecies, &temp, true);
	if (eaSize(&eaBodyScalesInclude))
	{
		for (i = eaSize(&temp) - 1; i >= 0; i--)
		{
			if (eaFind(&eaBodyScalesInclude, temp[i]) < 0)
			{
				eaRemove(&temp, i);
			}
		}
	}
	else if (eaSize(&eaBodyScalesExclude))
	{
		for (i = eaSize(&temp) - 1; i >= 0; i--)
		{
			if (eaFind(&eaBodyScalesExclude, temp[i]) >= 0)
			{
				eaRemove(&temp, i);
			}
		}
	}

	for (i = eaSize(&temp) - 1; i >= 0; i--)
	{
		for (index = eaSize(&pSkeleton->eaBodyScaleInfo)-1; index >= 0; --index)
		{
			if (!strcmp(pSkeleton->eaBodyScaleInfo[index]->pcName,temp[i]->pcName))
			{
				break;
			}
		}

		if (index < 0) continue;

		el = StructCreate(parse_CostumeEditLine);
		el->pcName = allocAddString(temp[i]->pcName);
		COPY_HANDLE(el->displayNameMsg.hMessage,temp[i]->displayNameMsg.hMessage);
		el->iType = kCostumeEditLineType_BodyScale;

		// Since the following code modifies the contents of the array
		// make the CostumeEditLine own the modified array of values.
		eaCopy(&el->eaValues, &temp[i]->eaValues);

		if (eaSize(&el->eaValues))
		{
			for (j = eaSize(&el->eaValues)-1; j >= 0; --j)
			{
				F32 fMin, fMax;
				if(costumeTailor_GetOverrideBodyScale(pSkeleton, temp[i]->pcName, pSpecies, pSlotType, &fMin, &fMax))
				{
					if(el->eaValues[j]->fValue > fMax || el->eaValues[j]->fValue < fMin)
					{
						eaRemove(&el->eaValues, j);
						continue;
					}
				}
				else
				{
					for (k = eaSize(&pSkeleton->eaBodyScaleInfo)-1; k >= 0; --k)
					{
						if (!stricmp(pSkeleton->eaBodyScaleInfo[k]->pcName, temp[i]->pcName))
						{
							break;
						}
					}
					if (k >= 0)
					{
						if (pSkeleton->eafPlayerMinBodyScales && (eafSize(&pSkeleton->eafPlayerMinBodyScales) > k))
						{
							if (el->eaValues[j]->fValue < pSkeleton->eafPlayerMinBodyScales[k])
							{
								eaRemove(&el->eaValues, j);
								continue;
							}
							if (el->eaValues[j]->fValue > pSkeleton->eafPlayerMaxBodyScales[k])
							{
								eaRemove(&el->eaValues, j);
								continue;
							}
						}
					}
				}
			}

			if (eaSize(&el->eaValues) <= 1)
			{
				costumeLineUI_DestroyLine(el);
				continue;
			}

			el->bHasSlider = 0;
			if (pCostume->eafBodyScales && (eafSize(&pCostume->eafBodyScales) > index)) {
				el->fScaleValue1 = pCostume->eafBodyScales[index];
			}
			else
			{
				el->fScaleValue1 = 0;
			}
			for (j = eaSize(&el->eaValues)-1; j >= 0; --j)
			{
				if (el->eaValues[j]->fValue == el->fScaleValue1)
				{
					break;
				}
			}
			if (j < 0)
			{
				el->pCurrentValue = el->eaValues[0];
				el->fScaleValue1 = el->eaValues[0]->fValue;
			}
			else
			{
				el->pCurrentValue = el->eaValues[j];
			}
			el->fScaleMin1 = el->fScaleValue1;
			el->fScaleMax1 = el->fScaleValue1;
		}
		else
		{
			F32 fMin, fMax;
			el->bHasSlider = 1;
			j = CostumeCreator_GetSpeciesBodyScaleIndex(temp[i]->pcName);
			
			if(costumeTailor_GetOverrideBodyScale(pSkeleton, temp[i]->pcName, pSpecies, pSlotType, &fMin, &fMax))
			{
				el->fScaleMin1 = fMin;
				el->fScaleMax1 = fMax;
			}
			else
			{
				if (pSkeleton->eafPlayerMinBodyScales && (eafSize(&pSkeleton->eafPlayerMinBodyScales) > index)) {
					el->fScaleMin1 = pSkeleton->eafPlayerMinBodyScales[index];
				}
				if (pSkeleton->eafPlayerMaxBodyScales && (eafSize(&pSkeleton->eafPlayerMaxBodyScales) > index)) {
					el->fScaleMax1 = pSkeleton->eafPlayerMaxBodyScales[index];
				}
			}
			if (el->fScaleMax1 <= el->fScaleMin1)
			{
				costumeLineUI_DestroyLine(el);
				continue;
			}
			if (pCostume->eafBodyScales && (eafSize(&pCostume->eafBodyScales) > index)) {
				el->fScaleValue1 = CLAMP(pCostume->eafBodyScales[index] + el->fScaleMin1, el->fScaleMin1, el->fScaleMax1);
			}
			else
			{
				el->fScaleValue1 = el->fScaleMin1;
			}
			el->pCurrentValue = NULL;
			eaDestroy(&el->eaValues);
		}

		eaPush(eaCostumeEditLine, el);
	}

	eaDestroy(&temp);
	PERFINFO_AUTO_STOP_FUNC();
}


static void costumeLineUI_UpdateOverlays(CostumeEditLine ***eaCostumeEditLine, const char *pcCostumeSet, SpeciesDef *pSpecies, Entity *pEnt)
{
	CostumeEditLine *el;
	PCCostumeSet *pSet = NULL;
	REF_TO(PCCostumeSet) hSet;

	PERFINFO_AUTO_START_FUNC();

	//Guild Uniforms
	if (pcCostumeSet && *pcCostumeSet && pEnt && pEnt->pPlayer && guild_IsMember(pEnt))
	{
		Player *pPlayer = pEnt->pPlayer;
		PlayerGuild *pGuild = pPlayer ? pPlayer->pGuild : NULL;

		if (pGuild && eaSize(&pGuild->eaGuildCostumes) >= 1 && pGuild->eaGuildCostumes[0] && pGuild->eaGuildCostumes[0]->pCostume && pGuild->eaGuildCostumes[0]->pCostume->pcName)
		{
			el = StructCreate(parse_CostumeEditLine);
			el->iType = kCostumeEditLineType_GuildOverlay;
			el->pCurrentGuildOverlay = pGuild->eaGuildCostumes[0];
			el->pcName = allocAddString(pGuild->eaGuildCostumes[0]->pCostume->pcName);
			el->eaGuildOverlays = pGuild->eaGuildCostumes;
			el->bColor0Allowed = 0;
			el->bColor1Allowed = 0;
			el->bColor2Allowed = 0;
			el->bColor3Allowed = 0;
			eaPush(eaCostumeEditLine, el);
		}
	}

	//Basic Uniforms
	if (pcCostumeSet && *pcCostumeSet)
	{
		SET_HANDLE_FROM_STRING(g_hCostumeSetsDict, pcCostumeSet, hSet);
		pSet = GET_REF(hSet);

		if (pSet)
		{
			CostumeRefForSet **eaCostumes = NULL;

			costumeTailor_GetValidCostumesFromSet(pSet, pSpecies, &eaCostumes, true, true);

			if (eaSize(&eaCostumes) >= 1)
			{
				el = StructCreate(parse_CostumeEditLine);
				el->iType = kCostumeEditLineType_Overlay;
				el->pCurrentOverlay = pSet->eaPlayerCostumes[0];
				COPY_HANDLE(el->displayNameMsg.hMessage,pSet->displayNameMsg.hMessage);
				el->pcName = allocAddString(pSet->pcName);
				el->eaOverlays = eaCostumes;
				el->bColor0Allowed = 0;
				el->bColor1Allowed = 0;
				el->bColor2Allowed = 0;
				el->bColor3Allowed = 0;
				eaPush(eaCostumeEditLine, el);
			}
		}

		REMOVE_HANDLE(hSet);
	}

	PERFINFO_AUTO_STOP_FUNC();
}


void costumeLineUI_UpdateLines(NOCONST(PlayerCostume) *pCostume,
							   CostumeEditLine ***peaCostumeEditLines,
							   SpeciesDef *pSpecies,
							   PCSkeletonDef *pSkeleton,
							   CostumeEditLineType eFindTypes,
							   int iBodyScalesRule,
							   PCRegionRef ***peaFindRegions,
							   CostumeUIScaleGroup **eaFindScaleGroup,
							   PCBodyScaleInfo **eaBodyScalesInclude, 
							   PCBodyScaleInfo **eaBodyScalesExclude,
							   const char **eaIncludeBones,
							   const char **eaExcludeBones,
							   PCSlotType *pSlotType,
							   const char *pcCostumeSet,
							   bool bLineListHideMirrorBones,
							   bool bUnlockAll,
							   bool bMirrorSelectMode,
							   bool bGroupSelectMode,
							   bool bCountNone,
							   bool bOmitHasOnlyOne,
							   bool bCombineLines,
							   bool bTextureLinesForCurrentPartValuesOnly,
							   PlayerCostume **eaUnlockedCostumes,
							   const char **eaPowerFXBones)
{
	PlayerCostume *pConstCostume = (PlayerCostume *)pCostume;
	CostumeEditLine **eaCostumeEditLine = NULL;
	PCRegion *pRegion;
	PCCategory *pCategory = NULL;
	PCBoneDef *pBone = NULL;
	PCCategory **eaCategories = NULL;
	PCBoneDef **eaBones = NULL;
	PCGeometryDef **eaGeometries = NULL;
	PCMaterialDef **eaMaterials = NULL;
	NOCONST(PCPart) *pPart;
	PCColorFlags eColorChoices = 0, required = 0;
	CostumeEditLine *el;
	int i, j, k, l, pos;
	bool found, copied;
	PERFINFO_AUTO_START_FUNC();

	costumeLineUI_UpdateOverlays(&eaCostumeEditLine, pcCostumeSet, pSpecies, CostumeUI_GetSourceEnt());
	if (iBodyScalesRule == kCostumeUIBodyScaleRule_AfterOverlays && (eFindTypes & kCostumeEditLineType_BodyScale)) 
	{
		costumeLineUI_UpdateBodyScales(&eaCostumeEditLine, pCostume, pSpecies, pSkeleton, pSlotType, eaBodyScalesInclude, eaBodyScalesExclude);
	}

	for (i = eaSize(peaFindRegions)-1; i >= 0; --i)
	{
		pRegion = GET_REF((*peaFindRegions)[i]->hRegion);
		if (!pRegion) continue;

		if (eFindTypes & kCostumeEditLineType_Region)
		{
			el = StructCreate(parse_CostumeEditLine);
			el->pcName = allocAddString(pRegion->pcName);
			COPY_HANDLE(el->displayNameMsg.hMessage,pRegion->displayNameMsg.hMessage);
			el->iType = kCostumeEditLineType_Region;
			el->bColor0Allowed = 0;
			el->bColor1Allowed = 0;
			el->bColor2Allowed = 0;
			el->bColor3Allowed = 0;
			eaPush(&eaCostumeEditLine, el);
		}

		if (i == eaSize(peaFindRegions)-1 && iBodyScalesRule == kCostumeUIBodyScaleRule_AfterLastRegionHeader && (eFindTypes & kCostumeEditLineType_BodyScale)) 
		{
			costumeLineUI_UpdateBodyScales(&eaCostumeEditLine, pCostume, pSpecies, pSkeleton, pSlotType, eaBodyScalesInclude, eaBodyScalesExclude);
		}

		if (eFindTypes & kCostumeEditLineType_Category)
		{
			// Populate category list
			found = false;
			copied = false;

			eaCategories = NULL;
			costumeTailor_GetValidCategories(pCostume, pRegion, pSpecies, eaUnlockedCostumes, eaPowerFXBones, pSlotType, &eaCategories, CGVF_OMIT_EMPTY | CGVF_SORT_DISPLAY | (bUnlockAll ? CGVF_UNLOCK_ALL : 0));

			if (eaSize(&eaCategories) > 0)
			{
				for (j = eaSize(&pCostume->eaRegionCategories)-1; j >= 0; --j)
				{
					if (GET_REF(pCostume->eaRegionCategories[j]->hRegion) == pRegion)
					{
						for (k = eaSize(&eaCategories)-1; k >= 0; --k)
						{
							if (eaCategories[k] == GET_REF(pCostume->eaRegionCategories[j]->hCategory))
							{
								pCategory = GET_REF(pCostume->eaRegionCategories[j]->hCategory);
								break;
							}
						}
						break;
					}
				}

				if(j >= 0)
				{
					if (!pCategory)
					{
						pCategory = eaCategories[0];
						SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, eaCategories[0], pCostume->eaRegionCategories[j]->hCategory);
					}
					if (eaSize(&eaCategories) > 1)
					{
						el = StructCreate(parse_CostumeEditLine);
						el->iType = kCostumeEditLineType_Category;
						el->pCurrentCat = pCategory;
						SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, pRegion, el->hOwnerRegion);
						SET_HANDLE_FROM_STRING(gMessageDict,"Costume_Costume.Label.Category",el->displayNameMsg.hMessage);
						el->pcName = allocAddString(pCategory->pcName);
						el->eaCat = eaCategories;
						el->bColor0Allowed = 0;
						el->bColor1Allowed = 0;
						el->bColor2Allowed = 0;
						el->bColor3Allowed = 0;
						eaPush(&eaCostumeEditLine, el);
						found = true;
					}
				}
			}
			if ((!found) && (!copied)) 
			{
				eaDestroy(&eaCategories);
			}
		}

		//costumeTailor_FillAllBones(pCostume, pSpecies, true, false, true);
		if (eFindTypes & kCostumeEditLineType_Bone)
		{
			//Populate bone list
			found = false;
			copied = false;

			eaBones = NULL;
			costumeTailor_GetValidBones(pCostume, GET_REF(pCostume->hSkeleton), pRegion, pCategory, pSpecies, eaUnlockedCostumes, eaPowerFXBones, &eaBones, CGVF_OMIT_EMPTY | (bOmitHasOnlyOne ? CGVF_OMIT_ONLY_ONE : 0) | (bCountNone ? CGVF_COUNT_NONE : 0) | CGVF_SORT_DISPLAY | (bUnlockAll ? CGVF_UNLOCK_ALL : 0));
			CostumeUI_FilterBoneList(&eaBones, eaIncludeBones, eaExcludeBones);

			if (eaSize(&eaBones) > 0)
			{
				//if (eaSize(&eaBones) > 1)
				{
					el = StructCreate(parse_CostumeEditLine);
					el->iType = kCostumeEditLineType_Bone;
					pBone = costumeLineUI_FindSelectedBone(pRegion, pCategory, *peaCostumeEditLines);
					if (!pBone)
					{
						pBone = eaBones[0];
						el->pCurrentBone = eaBones[0];
					}
					else
					{
						el->pCurrentBone = pBone;
					}
					if (pCategory)
					{
						COPY_HANDLE(el->displayNameMsg.hMessage,pCategory->displayNameMsg.hMessage);
					}
					//else
					//{
					//	SET_HANDLE_FROM_STRING(g_hCostumeBoneDict,"Costume_Costume.Label.Bone",el->displayNameMsg.hMessage);
					//}
					SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, pCategory, el->hOwnerCat);
					SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, pRegion, el->hOwnerRegion);
					//sprintf(text, "Bone%d", i);
					el->pcName = allocAddString(pBone->pcName);
					el->eaBone = eaBones;
					pos = eaSize(&eaCostumeEditLine);
					eaPush(&eaCostumeEditLine, el);
					found = true;
				}
				//else
				//{
				//	pBone = eaBones[0];
				//}

				if (pBone && pBone->pcName && stricmp(pBone->pcName, "None"))
				{
					eColorChoices |= costumeLineUI_AddGeometryLine(&eaCostumeEditLine, i*100, pCostume, pSpecies,
						pRegion, pCategory, pBone, eFindTypes, eaUnlockedCostumes, 
						bLineListHideMirrorBones, bUnlockAll, bCombineLines, bTextureLinesForCurrentPartValuesOnly,
						&required, found);

					if (found)
					{
						el->bColor0Allowed = ((eColorChoices & kPCColorFlags_Color0) || !((pBone->eColorChoices & kPCColorFlags_Color0) || (required & kPCColorFlags_Color0)) ? 0 : 1);
						el->bColor1Allowed = ((eColorChoices & kPCColorFlags_Color1) || !((pBone->eColorChoices & kPCColorFlags_Color1) || (required & kPCColorFlags_Color1)) ? 0 : 1);
						el->bColor2Allowed = ((eColorChoices & kPCColorFlags_Color2) || !((pBone->eColorChoices & kPCColorFlags_Color2) || (required & kPCColorFlags_Color2)) ? 0 : 1);
						el->bColor3Allowed = ((eColorChoices & kPCColorFlags_Color3) || !((pBone->eColorChoices & kPCColorFlags_Color3) || (required & kPCColorFlags_Color3)) ? 0 : 1);
						required = 0;
						eColorChoices |= (el->bColor0Allowed ? kPCColorFlags_Color0 : 0);
						eColorChoices |= (el->bColor1Allowed ? kPCColorFlags_Color1 : 0);
						eColorChoices |= (el->bColor2Allowed ? kPCColorFlags_Color2 : 0);
						eColorChoices |= (el->bColor3Allowed ? kPCColorFlags_Color3 : 0);
					}
				}
				else
				{
					el->bColor0Allowed = (!(required & kPCColorFlags_Color0) ? 0 : 1);
					el->bColor1Allowed = (!(required & kPCColorFlags_Color1) ? 0 : 1);
					el->bColor2Allowed = (!(required & kPCColorFlags_Color2) ? 0 : 1);
					el->bColor3Allowed = (!(required & kPCColorFlags_Color3) ? 0 : 1);
					required = 0;
					eColorChoices |= (el->bColor0Allowed ? kPCColorFlags_Color0 : 0);
					eColorChoices |= (el->bColor1Allowed ? kPCColorFlags_Color1 : 0);
					eColorChoices |= (el->bColor2Allowed ? kPCColorFlags_Color2 : 0);
					eColorChoices |= (el->bColor3Allowed ? kPCColorFlags_Color3 : 0);
				}

				if (eaSize(&eaBones) == 1 && el->bColor0Allowed == 0 && el->bColor1Allowed == 0 && el->bColor2Allowed == 0 && el->bColor3Allowed == 0 && el->bHasSlider == 0)
				{
					el->eaBone = NULL; // Destroyed later
					costumeLineUI_DestroyLine(el);
					eaRemove(&eaCostumeEditLine, pos);
				}
			}
			if ((!found) && (!copied)) 
			{
				eaDestroy(&eaBones);
			}
		}
		else if (eFindTypes & kCostumeEditLineType_Geometry)
		{
			//Populate geometry list
			eaBones = NULL;
			costumeTailor_GetValidBones(pCostume, GET_REF(pCostume->hSkeleton), pRegion, pCategory, pSpecies, eaUnlockedCostumes, eaPowerFXBones, &eaBones, (bLineListHideMirrorBones && bMirrorSelectMode ? CGVF_MIRROR_MODE : 0) | (bLineListHideMirrorBones && bGroupSelectMode ? CGVF_BONE_GROUP_MODE : 0) | CGVF_OMIT_EMPTY | (bOmitHasOnlyOne ? CGVF_OMIT_ONLY_ONE : 0) | (bCountNone ? CGVF_COUNT_NONE : 0) | CGVF_SORT_DISPLAY | (bUnlockAll ? CGVF_UNLOCK_ALL : 0));
			CostumeUI_FilterBoneList(&eaBones, eaIncludeBones, eaExcludeBones);

			for (j = 0; j < eaSize(&eaBones); ++j)
			{
				eColorChoices |= costumeLineUI_AddGeometryLine(&eaCostumeEditLine, j+(i*100), pCostume, pSpecies,
						pRegion, pCategory, eaBones[j], eFindTypes, eaUnlockedCostumes, 
						bLineListHideMirrorBones, bUnlockAll, bCombineLines, bTextureLinesForCurrentPartValuesOnly,
						&required, false);
			}
			eaDestroy(&eaBones);
		}
		else if (eFindTypes & kCostumeEditLineType_Material)
		{
			//Populate material list
			eaBones = NULL;
			costumeTailor_GetValidBones(pCostume, GET_REF(pCostume->hSkeleton), pRegion, pCategory, pSpecies, eaUnlockedCostumes, eaPowerFXBones, &eaBones, CGVF_OMIT_EMPTY | (bOmitHasOnlyOne ? CGVF_OMIT_ONLY_ONE : 0) | (bCountNone ? CGVF_COUNT_NONE : 0) | CGVF_SORT_DISPLAY | (bUnlockAll ? CGVF_UNLOCK_ALL : 0));
			CostumeUI_FilterBoneList(&eaBones, eaIncludeBones, eaExcludeBones);

			for (j = 0; j < eaSize(&eaBones); ++j)
			{
				eaGeometries = NULL;
				costumeTailor_GetValidGeos(pCostume, GET_REF(pCostume->hSkeleton), eaBones[j], pCategory, pSpecies, eaUnlockedCostumes, &eaGeometries, false, false /*g_GroupSelectMode && pPart->iBoneGroupIndex >= 0*/, true, bUnlockAll);
				for (k = 0; k < eaSize(&eaGeometries); ++k)
				{
					eColorChoices |= costumeLineUI_AddMaterialLine(&eaCostumeEditLine, k+((j+(i*100))*100), pCostume, pSpecies,
							pRegion, pCategory, eaBones[j], eaGeometries[k], 
							eFindTypes, eaUnlockedCostumes, bUnlockAll, bCombineLines, bLineListHideMirrorBones, bTextureLinesForCurrentPartValuesOnly,
							&required, false, false);
				}
			}
			eaDestroy(&eaGeometries);
			eaDestroy(&eaBones);
		}
		else if ((eFindTypes & (kCostumeEditLineType_Texture0|kCostumeEditLineType_Texture1|kCostumeEditLineType_Texture2|kCostumeEditLineType_Texture3|kCostumeEditLineType_Texture4)))
		{
			//Populate texture list
			eaBones = NULL;
			costumeTailor_GetValidBones(pCostume, GET_REF(pCostume->hSkeleton), pRegion, pCategory, pSpecies, eaUnlockedCostumes, eaPowerFXBones, &eaBones, CGVF_OMIT_EMPTY | (bOmitHasOnlyOne ? CGVF_OMIT_ONLY_ONE : 0) | (bCountNone ? CGVF_COUNT_NONE : 0) | CGVF_SORT_DISPLAY | (bUnlockAll ? CGVF_UNLOCK_ALL : 0));
			CostumeUI_FilterBoneList(&eaBones, eaIncludeBones, eaExcludeBones);

			for (j = 0; j < eaSize(&eaBones); ++j)
			{
				eaGeometries = NULL;
				if (bTextureLinesForCurrentPartValuesOnly)
				{
					pPart = costumeTailor_GetPartByBone(pCostume, eaBones[j], NULL);
					if (pPart && GET_REF(pPart->hMatDef) && GET_REF(pPart->hGeoDef))
					{
						if ((eFindTypes & kCostumeEditLineType_Texture0))
						{
							eColorChoices |= costumeLineUI_AddTextureLine(&eaCostumeEditLine, ((j+i*100)*100)*100, pCostume, pSpecies,
									pRegion, pCategory, eaBones[j], GET_REF(pPart->hGeoDef), GET_REF(pPart->hMatDef), 
									eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
									0, &required, false, false, false);
						}
						if ((eFindTypes & kCostumeEditLineType_Texture1))
						{
							eColorChoices |= costumeLineUI_AddTextureLine(&eaCostumeEditLine, ((j+i*100)*100)*100, pCostume, pSpecies,
									pRegion, pCategory, eaBones[j], GET_REF(pPart->hGeoDef), GET_REF(pPart->hMatDef), 
									eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
									1, &required, false, false, false);
						}
						if ((eFindTypes & kCostumeEditLineType_Texture2))
						{
							eColorChoices |= costumeLineUI_AddTextureLine(&eaCostumeEditLine, ((j+i*100)*100)*100, pCostume, pSpecies,
									pRegion, pCategory, eaBones[j], GET_REF(pPart->hGeoDef), GET_REF(pPart->hMatDef), 
									eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
									2, &required, false, false, false);
						}
						if ((eFindTypes & kCostumeEditLineType_Texture3))
						{
							eColorChoices |= costumeLineUI_AddTextureLine(&eaCostumeEditLine, ((j+i*100)*100)*100, pCostume, pSpecies,
									pRegion, pCategory, eaBones[j], GET_REF(pPart->hGeoDef), GET_REF(pPart->hMatDef), 
									eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
									3, &required, false, false, false);
						}
						if ((eFindTypes & kCostumeEditLineType_Texture4))
						{
							eColorChoices |= costumeLineUI_AddTextureLine(&eaCostumeEditLine, ((j+i*100)*100)*100, pCostume, pSpecies,
									pRegion, pCategory, eaBones[j], GET_REF(pPart->hGeoDef), GET_REF(pPart->hMatDef), 
									eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
									4, &required, false, false, false);
						}
					}
					else
					{
						costumeTailor_GetValidGeos(pCostume, GET_REF(pCostume->hSkeleton), eaBones[j], pCategory, pSpecies, eaUnlockedCostumes, &eaGeometries, false, false /*g_GroupSelectMode && pPart->iBoneGroupIndex >= 0*/, true, bUnlockAll);
					}
				}
				else
				{
					costumeTailor_GetValidGeos(pCostume, GET_REF(pCostume->hSkeleton), eaBones[j], pCategory, pSpecies, eaUnlockedCostumes, &eaGeometries, false, false /*g_GroupSelectMode && pPart->iBoneGroupIndex >= 0*/, true, bUnlockAll);
				}

				for (k = 0; k < eaSize(&eaGeometries); ++k)
				{
					eaMaterials = NULL;
					costumeTailor_GetValidMaterials(pCostume, eaGeometries[k], pSpecies, NULL, NULL /*pBoneGroup*/, eaUnlockedCostumes, &eaMaterials, false, true, bUnlockAll);
					for (l = 0; l < eaSize(&eaMaterials); ++l)
					{
						if ((eFindTypes & kCostumeEditLineType_Texture0))
						{
							eColorChoices |= costumeLineUI_AddTextureLine(&eaCostumeEditLine, l+((k+((j+(i*100))*100))*100), pCostume, pSpecies,
									pRegion, pCategory, eaBones[j], eaGeometries[k], eaMaterials[l], 
									eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
									0, &required, false, false, false);
						}
						if ((eFindTypes & kCostumeEditLineType_Texture1))
						{
							eColorChoices |= costumeLineUI_AddTextureLine(&eaCostumeEditLine, l+((k+((j+(i*100))*100))*100), pCostume, pSpecies,
									pRegion, pCategory, eaBones[j], eaGeometries[k], eaMaterials[l], 
									eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
									1, &required, false, false, false);
						}
						if ((eFindTypes & kCostumeEditLineType_Texture2))
						{
							eColorChoices |= costumeLineUI_AddTextureLine(&eaCostumeEditLine, l+((k+((j+(i*100))*100))*100), pCostume, pSpecies,
									pRegion, pCategory, eaBones[j], eaGeometries[k], eaMaterials[l], 
									eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
									2, &required, false, false, false);
						}
						if ((eFindTypes & kCostumeEditLineType_Texture3))
						{
							eColorChoices |= costumeLineUI_AddTextureLine(&eaCostumeEditLine, l+((k+((j+(i*100))*100))*100), pCostume, pSpecies,
									pRegion, pCategory, eaBones[j], eaGeometries[k], eaMaterials[l], 
									eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
									3, &required, false, false, false);
						}
						if ((eFindTypes & kCostumeEditLineType_Texture4))
						{
							eColorChoices |= costumeLineUI_AddTextureLine(&eaCostumeEditLine, l+((k+((j+(i*100))*100))*100), pCostume, pSpecies,
									pRegion, pCategory, eaBones[j], eaGeometries[k], eaMaterials[l], 
									eFindTypes, eaUnlockedCostumes, bUnlockAll, bLineListHideMirrorBones,
									4, &required, false, false, false);
						}
					}
				}
			}
			eaDestroy(&eaMaterials);
			eaDestroy(&eaGeometries);
			eaDestroy(&eaBones);
		}
	}

	if (iBodyScalesRule == kCostumeUIBodyScaleRule_AfterRegions && (eFindTypes & kCostumeEditLineType_BodyScale)) 
	{
		costumeLineUI_UpdateBodyScales(&eaCostumeEditLine, pCostume, pSpecies, pSkeleton, pSlotType, eaBodyScalesInclude, eaBodyScalesExclude);
	}

	if (eFindTypes & kCostumeEditLineType_Scale)
	{
		for (i = 0; i < eaSize(&eaFindScaleGroup); ++i)
		{
			for (j = eaSize(&pSkeleton->eaScaleInfoGroups)-1; j >= 0; --j)
			{
				if (!strcmp(eaFindScaleGroup[i]->pcName,pSkeleton->eaScaleInfoGroups[j]->pcName))
				{
					int count = 0;
					bool bLeft = true;
					PCScaleInfoGroup *sig = pSkeleton->eaScaleInfoGroups[j];

					el = StructCreate(parse_CostumeEditLine);
					el->pcName = allocAddString(sig->pcName);
					COPY_HANDLE(el->displayNameMsg.hMessage,sig->displayNameMsg.hMessage);
					el->iType = kCostumeEditLineType_Divider;
					el->bColor0Allowed = 0;
					el->bColor1Allowed = 0;
					el->bColor2Allowed = 0;
					el->bColor3Allowed = 0;
					eaPush(&eaCostumeEditLine, el);

					for (k = 0; k < eaSize(&sig->eaScaleInfo); ++k)
					{
						NOCONST(BoneScaleLimit) bsl;
						PCScaleInfo *pScaleInfo = sig->eaScaleInfo[k];
						float fMin, fMax, fDef;

						bsl.fMin = pScaleInfo->fPlayerMin;
						bsl.fMax = pScaleInfo->fPlayerMax;
						if(costumeTailor_GetOverrideBoneScale(pSkeleton, pScaleInfo, pScaleInfo->pcName, pSpecies, pSlotType, &fMin, &fMax))
						{
							bsl.fMin = fMin;
							bsl.fMax = fMax;
						}

						if (bsl.fMax <= bsl.fMin)
						{
							if (k + 1 >= eaSize(&sig->eaScaleInfo) && !bLeft)
							{
								eaPush(&eaCostumeEditLine, el);
								++count;
							}
							continue;
						}

						if (bLeft)
						{
							el = StructCreate(parse_CostumeEditLine);
							el->iType = kCostumeEditLineType_Scale;
							el->bHasSlider = 0;

							el->pcName = allocAddString(pScaleInfo->pcName);
							COPY_HANDLE(el->displayNameMsg.hMessage,pScaleInfo->displayNameMsg.hMessage);
							for (l = eaSize(&pCostume->eaScaleValues)-1; l >= 0; --l)
							{
								if (!strcmp(pCostume->eaScaleValues[l]->pcScaleName, el->pcName))
								{
									el->fScaleValue1 = pCostume->eaScaleValues[l]->fValue;
									break;
								}
							}
							fMin = bsl.fMin;
							fMax = bsl.fMax;
							if (l < 0)
							{
								fDef = 0.0f;
								CostumeCreator_CommonSetBoneScale(pCostume, fMin, fMax, el->pcName, (el->fScaleValue1 = fDef) - fMin);
							}
							el->fScaleMin1 = fMin;
							el->fScaleMax1 = fMax;

							if (k + 1 >= eaSize(&sig->eaScaleInfo))
							{
								eaPush(&eaCostumeEditLine, el);
								++count;
								break;
							}

							bLeft = false;
						}
						else
						{
							el->pcName2 = allocAddString(pScaleInfo->pcName);
							COPY_HANDLE(el->displayNameMsg2.hMessage,pScaleInfo->displayNameMsg.hMessage);
							for (l = eaSize(&pCostume->eaScaleValues)-1; l >= 0; --l)
							{
								if (!strcmp(pCostume->eaScaleValues[l]->pcScaleName, el->pcName2))
								{
									el->fScaleValue2 = pCostume->eaScaleValues[l]->fValue;
									break;
								}
							}
							fMin = bsl.fMin;
							fMax = bsl.fMax;
							if (l < 0)
							{
								fDef = 0.0f;
								CostumeCreator_CommonSetBoneScale(pCostume, fMin, fMax, el->pcName2, (el->fScaleValue2 = fDef) - fMin);
							}
							el->fScaleMin2 = fMin;
							el->fScaleMax2 = fMax;
							el->bHasSlider = 1;

							eaPush(&eaCostumeEditLine, el);
							++count;

							bLeft = true;
						}
					}

					if (!count)
					{
						el = eaPop(&eaCostumeEditLine);
						costumeLineUI_DestroyLine(el);
					}

					break;
				}
			}
		}
	}

	if (iBodyScalesRule == kCostumeUIBodyScaleRule_AfterScaleInfoGroups && (eFindTypes & kCostumeEditLineType_BodyScale)) 
	{
		costumeLineUI_UpdateBodyScales(&eaCostumeEditLine, pCostume, pSpecies, pSkeleton, pSlotType, eaBodyScalesInclude, eaBodyScalesExclude);
	}

	// Clean out old data
	for (i = eaSize(peaCostumeEditLines)-1; i >= 0; --i)
	{
		CostumeEditLine *pLine = (*peaCostumeEditLines)[i];
		costumeLineUI_DestroyLine(pLine);
	}
	eaDestroy(peaCostumeEditLines);

	// Set new lines into the passed in array
	*peaCostumeEditLines = eaCostumeEditLine;

	PERFINFO_AUTO_STOP_FUNC();
}


// This function uses globals and UIGens
void costumeLineUI_GetSubLineListInternal(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID CostumeEditLine *el, CostumeEditLineType iType, bool bExpandProducts)
{
	CostumeSubListRow ***peaSubList = NULL;
	CostumeSubListRow *pRow, *pRowCopy;
	int i, j, k;
	PERFINFO_AUTO_START_FUNC();

	if (!el || iType == kCostumeEditLineType_Invalid || iType == kCostumeEditLineType_TextureScale || iType == kCostumeEditLineType_BodyScale && el->bHasSlider)
	{
		ui_GenSetList(pGen, NULL, parse_CostumeSubListRow);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if (g_CostumeEditState.bUpdateLines || g_CostumeEditState.bUpdateLists)
	{
		ui_GenSetList(pGen, NULL, parse_CostumeSubListRow);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	peaSubList = ui_GenGetManagedListSafe(pGen, CostumeSubListRow);

	switch (iType)
	{
	case kCostumeEditLineType_Category:
		j = 0;
		for (i = 0; i < eaSize(&el->eaCat); i++)
		{
			pRow = eaGetStruct(peaSubList, parse_CostumeSubListRow, j++);
			memset(pRow, 0, sizeof(CostumeSubListRow));
			pRow->pCat = el->eaCat[i];
			pRow->iType = el->iType;
			pRow->pLine = el;
			pRow->bPlayerInitial = true;
			if (devassert(el->eaCat[i]))
			{
				pRow->pcName = el->eaCat[i]->pcName;
				pRow->pcDisplayName = TranslateDisplayMessage(el->eaCat[i]->displayNameMsg);
			}
			else
			{
				pRow->pcName = "";
				pRow->pcDisplayName = "";
			}
			pRow->pUnlockInfo = NULL;
		}
		eaSetSizeStruct(peaSubList, parse_CostumeSubListRow, j);
	xcase kCostumeEditLineType_Bone:
		j = 0;
		for (i = 0; i < eaSize(&el->eaBone); i++)
		{
			pRow = eaGetStruct(peaSubList, parse_CostumeSubListRow, j++);
			memset(pRow, 0, sizeof(CostumeSubListRow));
			pRow->pBone = el->eaBone[i];
			pRow->iType = el->iType;
			pRow->pLine = el;
			pRow->bPlayerInitial = true;
			if (devassert(el->eaBone[i]))
			{
				pRow->pcName = el->eaBone[i]->pcName;
				pRow->pcDisplayName = TranslateDisplayMessage(el->eaBone[i]->displayNameMsg);
			}
			else
			{
				pRow->pcName = "";
				pRow->pcDisplayName = "";
			}
			pRow->pUnlockInfo = NULL;
		}
		eaSetSizeStruct(peaSubList, parse_CostumeSubListRow, j);
	xcase kCostumeEditLineType_Geometry:
		j = 0;
		for (i = 0; i < eaSize(&el->eaGeo); i++)
		{
			pRow = eaGetStruct(peaSubList, parse_CostumeSubListRow, j++);
			memset(pRow, 0, sizeof(CostumeSubListRow));
			pRow->pGeo = el->eaGeo[i];
			pRow->iType = el->iType;
			pRow->pLine = el;
			if (devassert(el->eaGeo[i]))
			{
				pRow->bPlayerInitial = !!(el->eaGeo[i]->eRestriction & kPCRestriction_Player_Initial);
				pRow->pcName = el->eaGeo[i]->pcName;
				pRow->pcDisplayName = TranslateDisplayMessage(el->eaGeo[i]->displayNameMsg);
				stashFindPointer(g_CostumeEditState.stashGeoUnlockMeta, pRow->pcName, &pRow->pUnlockInfo);
			}
			else
			{
				pRow->bPlayerInitial = true;
				pRow->pcName = "";
				pRow->pcDisplayName = "";
				pRow->pUnlockInfo = NULL;
			}

			if (pRow->pUnlockInfo && eaSize(&pRow->pUnlockInfo->eaFullProductList) > 0) {
				if (bExpandProducts) {
					// Make a row for each product
					pRow->pProduct = pRow->pUnlockInfo->eaFullProductList[0];
					for (k = 1; k < eaSize(&pRow->pUnlockInfo->eaFullProductList); k++)
					{
						pRowCopy = eaGetStruct(peaSubList, parse_CostumeSubListRow, j++);
						memcpy(pRowCopy, pRow, sizeof(CostumeSubListRow));
						pRowCopy->pProduct = pRow->pUnlockInfo->eaFullProductList[k];
					}
				} else {
					// Use the default product
					pRow->pProduct = pRow->pUnlockInfo->pProduct;
				}
			}
		}
		eaSetSizeStruct(peaSubList, parse_CostumeSubListRow, j);
	xcase kCostumeEditLineType_Material:
		j = 0;
		for (i = 0; i < eaSize(&el->eaMat); i++)
		{
			pRow = eaGetStruct(peaSubList, parse_CostumeSubListRow, j++);
			memset(pRow, 0, sizeof(CostumeSubListRow));
			pRow->pMat = el->eaMat[i];
			pRow->iType = el->iType;
			pRow->pLine = el;
			if (devassert(el->eaMat[i]))
			{
				pRow->bPlayerInitial = !!(el->eaMat[i]->eRestriction & kPCRestriction_Player_Initial);
				pRow->pcName = el->eaMat[i]->pcName;
				pRow->pcDisplayName = TranslateDisplayMessage(el->eaMat[i]->displayNameMsg);
				stashFindPointer(g_CostumeEditState.stashMatUnlockMeta, pRow->pcName, &pRow->pUnlockInfo);
			}
			else
			{
				pRow->bPlayerInitial = true;
				pRow->pcName = "";
				pRow->pcDisplayName = "";
				pRow->pUnlockInfo = NULL;
			}

			if (pRow->pUnlockInfo && eaSize(&pRow->pUnlockInfo->eaFullProductList) > 0) {
				if (bExpandProducts) {
					// Make a row for each product
					pRow->pProduct = pRow->pUnlockInfo->eaFullProductList[0];
					for (k = 1; k < eaSize(&pRow->pUnlockInfo->eaFullProductList); k++)
					{
						pRowCopy = eaGetStruct(peaSubList, parse_CostumeSubListRow, j++);
						memcpy(pRowCopy, pRow, sizeof(CostumeSubListRow));
						pRowCopy->pProduct = pRow->pUnlockInfo->eaFullProductList[k];
					}
				} else {
					// Use the default product
					pRow->pProduct = pRow->pUnlockInfo->pProduct;
				}
			}
		}
		eaSetSizeStruct(peaSubList, parse_CostumeSubListRow, j);
	xcase kCostumeEditLineType_Texture0:
	case kCostumeEditLineType_Texture1:
	case kCostumeEditLineType_Texture2:
	case kCostumeEditLineType_Texture3:
	case kCostumeEditLineType_Texture4:
		j = 0;
		for (i = 0; i < eaSize(&el->eaTex); i++)
		{
			pRow = eaGetStruct(peaSubList, parse_CostumeSubListRow, j++);
			memset(pRow, 0, sizeof(CostumeSubListRow));
			pRow->pTex = el->eaTex[i];
			pRow->iType = el->iType;
			pRow->pLine = el;
			if (devassert(el->eaTex[i]))
			{
				pRow->bPlayerInitial = !!(el->eaTex[i]->eRestriction & kPCRestriction_Player_Initial);
				pRow->pcName = el->eaTex[i]->pcName;
				pRow->pcDisplayName = TranslateDisplayMessage(el->eaTex[i]->displayNameMsg);
				stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, pRow->pcName, &pRow->pUnlockInfo);
			}
			else
			{
				pRow->bPlayerInitial = true;
				pRow->pcName = "";
				pRow->pcDisplayName = "";
				pRow->pUnlockInfo = NULL;
			}

			if (pRow->pUnlockInfo && eaSize(&pRow->pUnlockInfo->eaFullProductList) > 0) {
				if (bExpandProducts) {
					// Make a row for each product
					pRow->pProduct = pRow->pUnlockInfo->eaFullProductList[0];
					for (k = 1; k < eaSize(&pRow->pUnlockInfo->eaFullProductList); k++)
					{
						pRowCopy = eaGetStruct(peaSubList, parse_CostumeSubListRow, j++);
						memcpy(pRowCopy, pRow, sizeof(CostumeSubListRow));
						pRowCopy->pProduct = pRow->pUnlockInfo->eaFullProductList[k];
					}
				} else {
					// Use the default product
					pRow->pProduct = pRow->pUnlockInfo->pProduct;
				}
			}
		}
		eaSetSizeStruct(peaSubList, parse_CostumeSubListRow, j);
	xcase kCostumeEditLineType_BodyScale:
		j = 0;
		for (i = 0; i < eaSize(&el->eaValues); i++)
		{
			pRow = eaGetStruct(peaSubList, parse_CostumeSubListRow, j++);
			memset(pRow, 0, sizeof(CostumeSubListRow));
			pRow->pValue = el->eaValues[i];
			pRow->iType = el->iType;
			pRow->pLine = el;
			pRow->bPlayerInitial = true;
			if (devassert(el->eaValues[i]))
			{
				pRow->pcName = el->eaValues[i]->pcName;
				pRow->pcDisplayName = TranslateDisplayMessage(el->eaValues[i]->displayNameMsg);
			}
			else
			{
				pRow->pcName = "";
				pRow->pcDisplayName = "";
			}
			pRow->pUnlockInfo = NULL;
		}
		eaSetSizeStruct(peaSubList, parse_CostumeSubListRow, j);
	xcase kCostumeEditLineType_Overlay:
		j = 0;
		for (i = 0; i < eaSize(&el->eaOverlays); i++)
		{
			pRow = eaGetStruct(peaSubList, parse_CostumeSubListRow, j++);
			memset(pRow, 0, sizeof(CostumeSubListRow));
			pRow->pOverlay = el->eaOverlays[i];
			pRow->iType = el->iType;
			pRow->pLine = el;
			pRow->bPlayerInitial = true;
			if (devassert(el->eaOverlays[i]))
			{
				pRow->pcName = el->eaOverlays[i]->pcName;
				pRow->pcDisplayName = TranslateDisplayMessage(el->eaOverlays[i]->displayNameMsg);
			}
			else
			{
				pRow->pcName = "";
				pRow->pcDisplayName = "";
			}
			pRow->pUnlockInfo = NULL;
		}
		eaSetSizeStruct(peaSubList, parse_CostumeSubListRow, j);
	xcase kCostumeEditLineType_GuildOverlay:
		j = 0;
		for (i = 0; i < eaSize(&el->eaGuildOverlays); i++)
		{
			pRow = eaGetStruct(peaSubList, parse_CostumeSubListRow, j++);
			memset(pRow, 0, sizeof(CostumeSubListRow));
			pRow->pGuildOverlay = el->eaGuildOverlays[i];
			pRow->iType = el->iType;
			pRow->pLine = el;
			pRow->bPlayerInitial = true;
			if (devassert(el->eaGuildOverlays[i]))
			{
				pRow->pcName = el->eaGuildOverlays[i]->pCostume->pcName;
				pRow->pcDisplayName = el->eaGuildOverlays[i]->pCostume->pcName;
			}
			else
			{
				pRow->pcName = "";
				pRow->pcDisplayName = "";
			}
			pRow->pUnlockInfo = NULL;
		}
		eaSetSizeStruct(peaSubList, parse_CostumeSubListRow, j);
	}

	ui_GenSetManagedListSafe(pGen, peaSubList, CostumeSubListRow, true);
	PERFINFO_AUTO_STOP_FUNC();
}


int costumeLineUI_GetCostumeEditSubLineListSizeInternal(SA_PARAM_OP_VALID CostumeEditLine *el, CostumeEditLineType iType)
{
	if (!el) return 0;

	switch (iType)
	{
	case kCostumeEditLineType_Category:
		return eaSize(&el->eaCat);
	xcase kCostumeEditLineType_Bone:
		return eaSize(&el->eaBone);
	xcase kCostumeEditLineType_Geometry:
		return eaSize(&el->eaGeo);
	xcase kCostumeEditLineType_Material:
		return eaSize(&el->eaMat);
	xcase kCostumeEditLineType_Texture0:
	case kCostumeEditLineType_Texture1:
	case kCostumeEditLineType_Texture2:
	case kCostumeEditLineType_Texture3:
	case kCostumeEditLineType_Texture4:
		return eaSize(&el->eaTex);
	xcase kCostumeEditLineType_TextureScale:
		return 0;
	case kCostumeEditLineType_BodyScale:
		if (el->bHasSlider) return 0;
		return eaSize(&el->eaValues);
	xcase kCostumeEditLineType_Overlay:
		return eaSize(&el->eaOverlays);
	xcase kCostumeEditLineType_GuildOverlay:
		return eaSize(&el->eaGuildOverlays);
	}
	return 0;
}


const char *costumeLineUI_GetCostumeEditSubLineSelDisplayNameInternal(SA_PARAM_OP_VALID CostumeEditLine *el, CostumeEditLineType iType)
{
	PCCategory *cat;
	PCBoneDef *bone;
	PCGeometryDef *geo;
	PCMaterialDef *mat;
	PCTextureDef *tex;
	PCBodyScaleValue *val;
	CostumeRefForSet *overlay;
	PlayerCostumeHolder *guildoverlay;

	if (!el) return NULL;

	switch (iType)
	{
	case kCostumeEditLineType_Category:
		cat = el->pCurrentCat;
		if (!cat) return NULL;
		return TranslateDisplayMessage(cat->displayNameMsg);
	xcase kCostumeEditLineType_Bone:
		bone = el->pCurrentBone;
		if (!bone) return NULL;
		return TranslateDisplayMessage(bone->displayNameMsg);
	xcase kCostumeEditLineType_Geometry:
		geo = el->pCurrentGeo;
		if (!geo) return NULL;
		return TranslateDisplayMessage(geo->displayNameMsg);
	xcase kCostumeEditLineType_Material:
		mat = el->pCurrentMat;
		if (!mat) return NULL;
		return TranslateDisplayMessage(mat->displayNameMsg);
	xcase kCostumeEditLineType_Texture0:
	case kCostumeEditLineType_Texture1:
	case kCostumeEditLineType_Texture2:
	case kCostumeEditLineType_Texture3:
	case kCostumeEditLineType_Texture4:
		tex = el->pCurrentTex;
		if (!tex) return NULL;
		return TranslateDisplayMessage(tex->displayNameMsg);
	xcase kCostumeEditLineType_TextureScale:
		return NULL;
	case kCostumeEditLineType_BodyScale:
		if (el->bHasSlider) return NULL;
		val = el->pCurrentValue;
		if (!val) return NULL;
		return TranslateDisplayMessage(val->displayNameMsg);
	xcase kCostumeEditLineType_Overlay:
		overlay = el->pCurrentOverlay;
		if (!overlay) return NULL;
		return TranslateDisplayMessage(overlay->displayNameMsg);
	xcase kCostumeEditLineType_GuildOverlay:
		guildoverlay = el->pCurrentGuildOverlay;
		if (!guildoverlay) return NULL;
		if (!guildoverlay->pCostume) return NULL;
		return guildoverlay->pCostume->pcName;
	}
	return NULL;
}


bool costumeLineUI_IsMergeSubLineSelPlayerInitialInternal(SA_PARAM_OP_VALID CostumeEditLine *el, CostumeEditLineType iType)
{
	if (!el) return true;

	switch (iType)
	{
	case kCostumeEditLineType_Category:
		return true;
	case kCostumeEditLineType_Bone:
		return el->pCurrentBone && !!(el->pCurrentBone->eRestriction & kPCRestriction_Player_Initial);
	case kCostumeEditLineType_Geometry:
		return el->pCurrentGeo && !!(el->pCurrentGeo->eRestriction & kPCRestriction_Player_Initial);
	case kCostumeEditLineType_Material:
		return el->pCurrentMat && !!(el->pCurrentMat->eRestriction & kPCRestriction_Player_Initial);
	case kCostumeEditLineType_Texture0:
	case kCostumeEditLineType_Texture1:
	case kCostumeEditLineType_Texture2:
	case kCostumeEditLineType_Texture3:
	case kCostumeEditLineType_Texture4:
		return el->pCurrentTex && !!(el->pCurrentTex->eRestriction & kPCRestriction_Player_Initial);
	case kCostumeEditLineType_TextureScale:
		return true;
	case kCostumeEditLineType_BodyScale:
		return true;
		xcase kCostumeEditLineType_GuildOverlay:
		return true;
	}

	return true;
}


const char *costumeLineUI_GetSubLineSelSysNameInternal(SA_PARAM_OP_VALID CostumeEditLine *el, CostumeEditLineType iType)
{
	if (!el) return NULL;

	switch (iType)
	{
	case kCostumeEditLineType_Category:
		if (!el->pCurrentCat) return NULL;
		return el->pCurrentCat->pcName;
	xcase kCostumeEditLineType_Bone:
		if (!el->pCurrentBone) return NULL;
		return el->pCurrentBone->pcName;
	xcase kCostumeEditLineType_Geometry:
		if (!el->pCurrentGeo) return NULL;
		return el->pCurrentGeo->pcName;
	xcase kCostumeEditLineType_Material:
		if (!el->pCurrentMat) return NULL;
		return el->pCurrentMat->pcName;
	xcase kCostumeEditLineType_Texture0:
	case kCostumeEditLineType_Texture1:
	case kCostumeEditLineType_Texture2:
	case kCostumeEditLineType_Texture3:
	case kCostumeEditLineType_Texture4:
		if (!el->pCurrentTex) return NULL;
		return el->pCurrentTex->pcName;
	xcase kCostumeEditLineType_TextureScale:
		return NULL;
	case kCostumeEditLineType_BodyScale:
		if (el->bHasSlider) return NULL;
		if (!el->pCurrentValue) return NULL;
		return el->pCurrentValue->pcName;
	xcase kCostumeEditLineType_Overlay:
		//if (!el->pCurrentOverlay) return NULL;
		//return el->pCurrentOverlay->pcName;
		return "";
	xcase kCostumeEditLineType_GuildOverlay:
		//if (!el->pCurrentGuildOverlay) return NULL;
		//return el->pCurrentGuildOverlay->pcName;
		return "";
	}
	return NULL;
}


void costumeLineUI_SetCostumeEditLinesGeneral( 
						PlayerCostume *pCostume, NOCONST(PlayerCostume) *pTarget, CostumeEditLine **eaCostumeEditLine,
						PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCSlotType *pSlotType, 
						Guild *pGuild, bool bUnlockAll)
{
	PCSkeletonDef *pSkel = pTarget ? GET_REF(pTarget->hSkeleton) : NULL;
	SpeciesDef *pSpecies = pTarget ? GET_REF(pTarget->hSpecies) : NULL;
	PCBoneDef **eaSetBones = NULL;
	PCRegion **eaSetCategories = NULL;
	const char **eaSetScales = NULL;
	const char **eaSetBodyScales = NULL;
	int i, j;
	bool bRefillBones = false;
	F32 fMin, fMax, fDef;

	if (!pCostume || !pTarget) return;
	if (GET_REF(pCostume->hSkeleton) != GET_REF(pTarget->hSkeleton)) return;
	if (!pSpecies) pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	if (GET_REF(pCostume->hSpecies) && GET_REF(pCostume->hSpecies) != pSpecies) return;

	// Find everything to copy over
	for (i = eaSize(&eaCostumeEditLine) - 1; i >= 0; i--) {
		switch (eaCostumeEditLine[i]->iType)
		{
		xcase kCostumeEditLineType_Region:
		case kCostumeEditLineType_Category:
			// Copy the category for the region from the source
			if (GET_REF(eaCostumeEditLine[i]->hOwnerRegion)) {
				if (eaFind(&eaSetCategories, GET_REF(eaCostumeEditLine[i]->hOwnerRegion)) < 0) {
					eaPush(&eaSetCategories, GET_REF(eaCostumeEditLine[i]->hOwnerRegion));
				}
			}
		xcase kCostumeEditLineType_Bone:
		case kCostumeEditLineType_Geometry:
		case kCostumeEditLineType_Material:
		case kCostumeEditLineType_Texture0:
		case kCostumeEditLineType_Texture1:
		case kCostumeEditLineType_Texture2:
		case kCostumeEditLineType_Texture3:
		case kCostumeEditLineType_Texture4:
		case kCostumeEditLineType_TextureScale:
			// Copy entire part from source
			if (GET_REF(eaCostumeEditLine[i]->hOwnerBone)) {
				if (eaFind(&eaSetBones, GET_REF(eaCostumeEditLine[i]->hOwnerBone)) < 0) {
					eaPush(&eaSetBones, GET_REF(eaCostumeEditLine[i]->hOwnerBone));
				}
			}
		xcase kCostumeEditLineType_Scale:
			if (eaFind(&eaSetScales, eaCostumeEditLine[i]->pcName) < 0) {
				eaPush(&eaSetScales, eaCostumeEditLine[i]->pcName);
			}
			if (eaCostumeEditLine[i]->bHasSlider) {
				if (eaFind(&eaSetScales, eaCostumeEditLine[i]->pcName2) < 0) {
					eaPush(&eaSetScales, eaCostumeEditLine[i]->pcName2);
				}
			}
		xcase kCostumeEditLineType_BodyScale:
			if (eaFind(&eaSetBodyScales, eaCostumeEditLine[i]->pcName) < 0) {
				eaPush(&eaSetBodyScales, eaCostumeEditLine[i]->pcName);
			}
		}
	}

	for (i = eaSize(&eaSetScales) - 1; i >= 0; i--) {
		PCScaleValue* pSrcValue = NULL;
		NOCONST(PCScaleValue)* pDstValue = NULL;
		for (j = eaSize(&pCostume->eaScaleValues) - 1; j >= 0; j--) {
			if (!stricmp(eaSetScales[i], pCostume->eaScaleValues[j]->pcScaleName)) {
				pSrcValue = pCostume->eaScaleValues[j];
				break;
			}
		}
		for (j = eaSize(&pTarget->eaScaleValues) - 1; j >= 0; j--) {
			if (!stricmp(eaSetScales[i], pTarget->eaScaleValues[j]->pcScaleName)) {
				pDstValue = pTarget->eaScaleValues[j];
				break;
			}
		}
		if (!pSrcValue && !pDstValue) {
			continue;
		} else if (pSrcValue && pDstValue) {
			pDstValue->fValue = pSrcValue->fValue;
		} else if (!pDstValue) {
			pDstValue = StructCloneDeConst(parse_PCScaleValue, pSrcValue);
			eaPush(&pTarget->eaScaleValues, pDstValue);
		} else {
			eaRemove(&pTarget->eaScaleValues, j);
			StructDestroyNoConst(parse_PCScaleValue, pDstValue);
		}
	}

	for (i = eaSize(&eaSetBodyScales) - 1; i >= 0; i--) {
		for (j = 0; j < eaSize(&pSkel->eaBodyScaleInfo); j++) {
			if (!stricmp(eaSetBodyScales[i], pSkel->eaBodyScaleInfo[j]->pcName)) {
				break;
			}
		}
		fMin = j < eafSize(&pSkel->eafPlayerMinBodyScales) ? pSkel->eafPlayerMinBodyScales[j] : 0;
		fMax = j < eafSize(&pSkel->eafPlayerMaxBodyScales) ? pSkel->eafPlayerMaxBodyScales[j] : 100;
		fDef = j < eafSize(&pSkel->eafDefaultBodyScales) ? pSkel->eafDefaultBodyScales[j] : 50;
		if (j >= eafSize(&pCostume->eafBodyScales) && j >= eafSize(&pTarget->eafBodyScales)) {
			continue;
		} else if (j < eafSize(&pCostume->eafBodyScales) && j < eafSize(&pTarget->eafBodyScales)) {
			pTarget->eafBodyScales[j] = CLAMP(pCostume->eafBodyScales[j], fMin, fMax);
		} else if (j < eafSize(&pTarget->eafBodyScales)) {
			pTarget->eafBodyScales[j] = fDef;
		} else if (j < eafSize(&pCostume->eafBodyScales)) {
			while (eafSize(&pTarget->eafBodyScales) <= j) {
				int iSet = eafSize(&pTarget->eafBodyScales);
				eafPush(&pTarget->eafBodyScales, iSet < eafSize(&pSkel->eafDefaultBodyScales) ? pSkel->eafDefaultBodyScales[iSet] : 50);
			}
			pTarget->eafBodyScales[j] = CLAMP(pCostume->eafBodyScales[j], fMin, fMax);
		}
	}

	for (i = eaSize(&eaSetCategories) - 1; i >= 0; i--) {
		PCCategory *pCat = costumeTailor_GetCategoryForRegion(pCostume, eaSetCategories[i]);
		if (pCat) {
			PCCategory **eaValidCat = NULL;
			costumeTailor_SetRegionCategory(pTarget, eaSetCategories[i], pCat);

			// Validate, though it should be valid
			costumeTailor_GetValidCategories(pTarget, eaSetCategories[i], pSpecies, eaUnlockedCostumes, eaPowerFXBones, pSlotType, &eaValidCat, CGVF_OMIT_EMPTY | CGVF_SORT_DISPLAY | (bUnlockAll ? CGVF_UNLOCK_ALL : 0));
			costumeTailor_PickValidCategoryForRegion(pTarget, eaSetCategories[i], eaValidCat, false);
			eaDestroy(&eaValidCat);
		}
	}

	for (i = eaSize(&eaSetBones) - 1; i >= 0; i--) {
		PCPart *pSrcPart = NULL;
		NOCONST(PCPart) *pDstPart = NULL;
		for (j = eaSize(&pCostume->eaParts) - 1; j >= 0; j--) {
			if (GET_REF(pCostume->eaParts[j]->hBoneDef) == eaSetBones[i]) {
				pSrcPart = pCostume->eaParts[j];
				break;
			}
		}
		for (j = eaSize(&pTarget->eaParts) - 1; j >= 0; j--) {
			if (GET_REF(pTarget->eaParts[j]->hBoneDef) == eaSetBones[i]) {
				pDstPart = pTarget->eaParts[j];
				break;
			}
		}
		if (!pSrcPart && !pDstPart) {
			continue;
		} else if (pSrcPart && pDstPart) {
			StructCopyAllDeConst(parse_PCPart, pSrcPart, pDstPart);
		} else if (pSrcPart) {
			pDstPart = StructCloneDeConst(parse_PCPart, pSrcPart);
			eaPush(&pTarget->eaParts, pDstPart);
		} else if (pDstPart) {
			eaRemove(&pTarget->eaParts, j);
			StructDestroyNoConst(parse_PCPart, pDstPart);
			bRefillBones = true;
			continue;
		}

		costumeTailor_PickValidPartValues(pTarget, pDstPart, pSpecies, pSlotType, eaUnlockedCostumes, true, false, true, false, pGuild);
	}

	if (bRefillBones) {
		costumeTailor_FillAllBones(pTarget, pSpecies, eaPowerFXBones, pSlotType, true, false, true);
	}

	CostumeUI_ValidateAllParts(pTarget, false, true);

	eaDestroy(&eaSetBodyScales);
	eaDestroy(&eaSetScales);
	eaDestroy(&eaSetCategories);
	eaDestroy(&eaSetBones);
}


bool costumeLineUI_SetLineItemInternal(NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies, 
										const char *pchSysName, SA_PARAM_OP_VALID CostumeEditLine *el, CostumeEditLineType iType,
										PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCSlotType *pSlotType, 
										Guild *pGuild, GameAccountDataExtract *pExtract, bool bUnlockAll, bool bMirrorMode, bool bGroupMode)
{
	int i;
	PCBodyScaleValue **bsv;
	CostumeRefForSet **crfs;
	PlayerCostumeHolder **pch;
	NOCONST(PCPart) *pPart;

	if (!el) {
		return false;
	}

	switch (iType)
	{
	case kCostumeEditLineType_Category:
		return costumeTailor_SetCategory(pCostume, pSpecies, GET_REF(el->hOwnerRegion), pchSysName, 
									eaUnlockedCostumes, eaPowerFXBones, pSlotType, pGuild, bUnlockAll);

	case kCostumeEditLineType_Bone:
		// This code path ignores most of the input values and uses globals
		// sdangelo: I think this is a dead code path and can be removed, but I'm not sure
		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, GET_REF(el->hOwnerRegion), g_CostumeEditState.hRegion);
		SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, GET_REF(el->hOwnerCat), g_CostumeEditState.hCategory);
		return CostumeCreator_SetBone(pchSysName);

	case kCostumeEditLineType_Geometry:
		return costumeTailor_SetPartGeometry(pCostume, pSpecies, GET_REF(el->hOwnerBone), pchSysName,
			eaUnlockedCostumes, pSlotType, pGuild, bUnlockAll, bMirrorMode, bGroupMode);

	case kCostumeEditLineType_Material:
		pPart = costumeTailor_GetPartByBone(pCostume, GET_REF(el->hOwnerBone), NULL);
		return costumeTailor_SetPartMaterial(pCostume, pPart, pSpecies, NULL, pchSysName,
											 eaUnlockedCostumes, pSlotType, pGuild, bUnlockAll, bMirrorMode);

	case kCostumeEditLineType_Texture0:
		pPart = costumeTailor_GetPartByBone(pCostume, GET_REF(el->hOwnerBone), NULL);
		return costumeTailor_SetPartTexturePattern(pCostume, pPart, pSpecies, pchSysName,
											 eaUnlockedCostumes, pSlotType, pGuild, bUnlockAll, bMirrorMode);

	case kCostumeEditLineType_Texture1:
		pPart = costumeTailor_GetPartByBone(pCostume, GET_REF(el->hOwnerBone), NULL);
		return costumeTailor_SetPartTextureDetail(pCostume, pPart, pSpecies, pchSysName,
											 eaUnlockedCostumes, pSlotType, pGuild, bUnlockAll, bMirrorMode);

	case kCostumeEditLineType_Texture2:
		pPart = costumeTailor_GetPartByBone(pCostume, GET_REF(el->hOwnerBone), NULL);
		return costumeTailor_SetPartTextureSpecular(pCostume, pPart, pSpecies, pchSysName,
											 eaUnlockedCostumes, pSlotType, pGuild, bUnlockAll, bMirrorMode);

	case kCostumeEditLineType_Texture3:
		pPart = costumeTailor_GetPartByBone(pCostume, GET_REF(el->hOwnerBone), NULL);
		return costumeTailor_SetPartTextureDiffuse(pCostume, pPart, pSpecies, pchSysName,
											 eaUnlockedCostumes, pSlotType, pGuild, bUnlockAll, bMirrorMode);

	case kCostumeEditLineType_Texture4:
		pPart = costumeTailor_GetPartByBone(pCostume, GET_REF(el->hOwnerBone), NULL);
		return costumeTailor_SetPartTextureMovable(pCostume, pPart, pSpecies, pchSysName,
											 eaUnlockedCostumes, pSlotType, pGuild, bUnlockAll, bMirrorMode);

	case kCostumeEditLineType_TextureScale:
		return false;

	case kCostumeEditLineType_BodyScale:
		if (el->bHasSlider) {
			return false;
		}
		bsv = el->eaValues;
		for (i = eaSize(&bsv)-1; i >= 0; --i) {
			if (strcmp(bsv[i]->pcName,pchSysName) == 0) {
				costumeTailor_SetBodyScaleByName(pCostume, pSpecies, el->pcName, bsv[i]->fValue, pSlotType);
			}
		}
		return true;

	case kCostumeEditLineType_Overlay:
		crfs = el->eaOverlays;
		for (i = eaSize(&crfs)-1; i >= 0; --i) {
			if (!strcmp(crfs[i]->pcName,pchSysName)) {
				break;
			}
		}
		if (i >= 0) {
			SET_HANDLE_FROM_REFERENT("SpeciesDef", pSpecies, pCostume->hSpecies);
			if (costumeTailor_ApplyCostumeOverlay((PlayerCostume*)pCostume, NULL, GET_REF(crfs[i]->hPlayerCostume), eaUnlockedCostumes, "Uniforms", pSlotType, true, false, true, true)) {
				costumeTailor_MakeCostumeValid(pCostume, pSpecies, eaUnlockedCostumes, pSlotType, false, bUnlockAll, false, pGuild, false, pExtract, false, NULL);
				return true;
			}
		}
		return false;

	case kCostumeEditLineType_GuildOverlay:
		pch = el->eaGuildOverlays;
		for (i = eaSize(&pch)-1; i >= 0; --i) {
			if (!pch[i]->pCostume) {
				continue;
			}
			if (strcmp(pch[i]->pCostume->pcName,pchSysName) == 0) {
				break;
			}
		}
		if (i >= 0) {
			SET_HANDLE_FROM_REFERENT("SpeciesDef", pSpecies, pCostume->hSpecies);
			if (costumeTailor_ApplyCostumeOverlay((PlayerCostume*)pCostume, NULL, pch[i]->pCostume, eaUnlockedCostumes, "Uniforms", pSlotType, true, false, true, true)) {
				costumeTailor_MakeCostumeValid(pCostume, pSpecies, eaUnlockedCostumes, pSlotType, false, bUnlockAll, false, pGuild, false, pExtract, false, NULL);
				return true;
			}
		}
		return false;
	}

	return false;
}


// This function uses globals!
bool costumeLineUI_SetHoverLineMergeItemInternal(const char *pchSysName, SA_PARAM_OP_VALID CostumeEditLine *el, CostumeEditLineType iType)
{
	int i;
	PCBodyScaleValue **bsv;
	CostumeRefForSet **crfs;
	PlayerCostumeHolder **pch;
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	Entity *pEnt = entActivePlayerPtr();

	if (!el) return false;

	switch (iType)
	{
	case kCostumeEditLineType_Category:
		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, GET_REF(el->hOwnerRegion), g_CostumeEditState.hRegion);
		return CostumeCreator_SetHoverCategory(pchSysName);
		return false;
	case kCostumeEditLineType_Bone:
		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, GET_REF(el->hOwnerRegion), g_CostumeEditState.hRegion);
		SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, GET_REF(el->hOwnerCat), g_CostumeEditState.hCategory);
		return false;
	case kCostumeEditLineType_Geometry:
		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, GET_REF(el->hOwnerRegion), g_CostumeEditState.hRegion);
		SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, GET_REF(el->hOwnerCat), g_CostumeEditState.hCategory);
		SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, GET_REF(el->hOwnerBone), g_CostumeEditState.hBone);
		g_CostumeEditState.pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
		return CostumeCreator_SetHoverGeo(pchSysName);
	case kCostumeEditLineType_Material:
		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, GET_REF(el->hOwnerRegion), g_CostumeEditState.hRegion);
		SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, GET_REF(el->hOwnerCat), g_CostumeEditState.hCategory);
		SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, GET_REF(el->hOwnerBone), g_CostumeEditState.hBone);
		g_CostumeEditState.pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
		SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, GET_REF(el->hOwnerGeo), g_CostumeEditState.hGeometry);
		return CostumeCreator_SetHoverMaterial(pchSysName);
	case kCostumeEditLineType_Texture0:
		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, GET_REF(el->hOwnerRegion), g_CostumeEditState.hRegion);
		SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, GET_REF(el->hOwnerCat), g_CostumeEditState.hCategory);
		SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, GET_REF(el->hOwnerBone), g_CostumeEditState.hBone);
		g_CostumeEditState.pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
		SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, GET_REF(el->hOwnerGeo), g_CostumeEditState.hGeometry);
		SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, GET_REF(el->hOwnerMat), g_CostumeEditState.hMaterial);
		if (!g_CostumeEditState.pHoverCostume) {
			g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
		}
		assert(g_CostumeEditState.pHoverCostume);
		{
			NOCONST(PCPart) *pTemp = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, GET_REF(g_CostumeEditState.hBone), NULL);
			PCTextureDef *pTex = RefSystem_ReferentFromString(g_hCostumeTextureDict, pchSysName);
			if (!pTemp) return false;
			if (pTex && pTex->pValueOptions)
			{
				F32 fMin = pTex->pValueOptions->fValueMin;
				F32 fMax = pTex->pValueOptions->fValueMax;
				costumeTailor_GetTextureValueMinMax((PCPart*)pTemp, pTex, pSpecies, &fMin, &fMax);
				if (fMax > fMin && pTex->pValueOptions->fValueDefault >= fMin && pTex->pValueOptions->fValueDefault <= fMax)
				{
					if (!pTemp->pTextureValues) {
						pTemp->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
					}
					if (pTemp->pTextureValues) {
						pTemp->pTextureValues->fPatternValue = (((pTex->pValueOptions->fValueDefault - fMin) * 200.0f)/(fMax - fMin)) - 100.0f;
					}
				}
			}
			if (pTex && pTex->pColorOptions)
			{
				if (pTex->pColorOptions->bHasDefaultColor0) *((U32*)g_CostumeEditState.pPart->color0) = *((U32*)pTex->pColorOptions->uDefaultColor0);
				if (pTex->pColorOptions->bHasDefaultColor1) *((U32*)g_CostumeEditState.pPart->color1) = *((U32*)pTex->pColorOptions->uDefaultColor1);
				if (pTex->pColorOptions->bHasDefaultColor2) *((U32*)g_CostumeEditState.pPart->color2) = *((U32*)pTex->pColorOptions->uDefaultColor2);
				if (pTex->pColorOptions->bHasDefaultColor3) *((U32*)g_CostumeEditState.pPart->color3) = *((U32*)pTex->pColorOptions->uDefaultColor3);
			}
		}
		return CostumeCreator_SetHoverPattern(pchSysName);
	case kCostumeEditLineType_Texture1:
		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, GET_REF(el->hOwnerRegion), g_CostumeEditState.hRegion);
		SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, GET_REF(el->hOwnerCat), g_CostumeEditState.hCategory);
		SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, GET_REF(el->hOwnerBone), g_CostumeEditState.hBone);
		g_CostumeEditState.pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
		SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, GET_REF(el->hOwnerGeo), g_CostumeEditState.hGeometry);
		SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, GET_REF(el->hOwnerMat), g_CostumeEditState.hMaterial);
		if (!g_CostumeEditState.pHoverCostume) {
			g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
		}
		assert(g_CostumeEditState.pHoverCostume);
		{
			NOCONST(PCPart) *pTemp = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, GET_REF(g_CostumeEditState.hBone), NULL);
			PCTextureDef *pTex = RefSystem_ReferentFromString(g_hCostumeTextureDict, pchSysName);
			if (!pTemp) return false;
			if (pTex && pTex->pValueOptions)
			{
				F32 fMin = pTex->pValueOptions->fValueMin;
				F32 fMax = pTex->pValueOptions->fValueMax;
				costumeTailor_GetTextureValueMinMax((PCPart*)pTemp, pTex, pSpecies, &fMin, &fMax);
				if (fMax > fMin && pTex->pValueOptions->fValueDefault >= fMin && pTex->pValueOptions->fValueDefault <= fMax)
				{
					if (!pTemp->pTextureValues) {
						pTemp->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
					}
					if (pTemp->pTextureValues) {
						pTemp->pTextureValues->fDetailValue = (((pTex->pValueOptions->fValueDefault - fMin) * 200.0f)/(fMax - fMin)) - 100.0f;
					}
				}
			}
			if (pTex && pTex->pColorOptions)
			{
				if (pTex->pColorOptions->bHasDefaultColor0) *((U32*)g_CostumeEditState.pPart->color0) = *((U32*)pTex->pColorOptions->uDefaultColor0);
				if (pTex->pColorOptions->bHasDefaultColor1) *((U32*)g_CostumeEditState.pPart->color1) = *((U32*)pTex->pColorOptions->uDefaultColor1);
				if (pTex->pColorOptions->bHasDefaultColor2) *((U32*)g_CostumeEditState.pPart->color2) = *((U32*)pTex->pColorOptions->uDefaultColor2);
				if (pTex->pColorOptions->bHasDefaultColor3) *((U32*)g_CostumeEditState.pPart->color3) = *((U32*)pTex->pColorOptions->uDefaultColor3);
			}
		}
		return CostumeCreator_SetHoverDetail(pchSysName);
	case kCostumeEditLineType_Texture2:
		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, GET_REF(el->hOwnerRegion), g_CostumeEditState.hRegion);
		SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, GET_REF(el->hOwnerCat), g_CostumeEditState.hCategory);
		SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, GET_REF(el->hOwnerBone), g_CostumeEditState.hBone);
		g_CostumeEditState.pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
		SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, GET_REF(el->hOwnerGeo), g_CostumeEditState.hGeometry);
		SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, GET_REF(el->hOwnerMat), g_CostumeEditState.hMaterial);
		if (!g_CostumeEditState.pHoverCostume) {
			g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
		}
		assert(g_CostumeEditState.pHoverCostume);
		{
			NOCONST(PCPart) *pTemp = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, GET_REF(g_CostumeEditState.hBone), NULL);
			PCTextureDef *pTex = RefSystem_ReferentFromString(g_hCostumeTextureDict, pchSysName);
			if (!pTemp) return false;
			if (pTex && pTex->pValueOptions)
			{
				F32 fMin = pTex->pValueOptions->fValueMin;
				F32 fMax = pTex->pValueOptions->fValueMax;
				costumeTailor_GetTextureValueMinMax((PCPart*)pTemp, pTex, pSpecies, &fMin, &fMax);
				if (fMax > fMin && pTex->pValueOptions->fValueDefault >= fMin && pTex->pValueOptions->fValueDefault <= fMax)
				{
					if (!pTemp->pTextureValues) {
						pTemp->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
					}
					if (pTemp->pTextureValues) {
						pTemp->pTextureValues->fSpecularValue = (((pTex->pValueOptions->fValueDefault - fMin) * 200.0f)/(fMax - fMin)) - 100.0f;
					}
				}
			}
			if (pTex && pTex->pColorOptions)
			{
				if (pTex->pColorOptions->bHasDefaultColor0) *((U32*)g_CostumeEditState.pPart->color0) = *((U32*)pTex->pColorOptions->uDefaultColor0);
				if (pTex->pColorOptions->bHasDefaultColor1) *((U32*)g_CostumeEditState.pPart->color1) = *((U32*)pTex->pColorOptions->uDefaultColor1);
				if (pTex->pColorOptions->bHasDefaultColor2) *((U32*)g_CostumeEditState.pPart->color2) = *((U32*)pTex->pColorOptions->uDefaultColor2);
				if (pTex->pColorOptions->bHasDefaultColor3) *((U32*)g_CostumeEditState.pPart->color3) = *((U32*)pTex->pColorOptions->uDefaultColor3);
			}
		}
		return CostumeCreator_SetHoverSpecular(pchSysName);
	case kCostumeEditLineType_Texture3:
		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, GET_REF(el->hOwnerRegion), g_CostumeEditState.hRegion);
		SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, GET_REF(el->hOwnerCat), g_CostumeEditState.hCategory);
		SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, GET_REF(el->hOwnerBone), g_CostumeEditState.hBone);
		g_CostumeEditState.pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
		SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, GET_REF(el->hOwnerGeo), g_CostumeEditState.hGeometry);
		SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, GET_REF(el->hOwnerMat), g_CostumeEditState.hMaterial);
		if (!g_CostumeEditState.pHoverCostume) {
			g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
		}
		assert(g_CostumeEditState.pHoverCostume);
		{
			NOCONST(PCPart) *pTemp = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, GET_REF(g_CostumeEditState.hBone), NULL);
			PCTextureDef *pTex = RefSystem_ReferentFromString(g_hCostumeTextureDict, pchSysName);
			if (!pTemp) return false;
			if (pTex && pTex->pValueOptions)
			{
				F32 fMin = pTex->pValueOptions->fValueMin;
				F32 fMax = pTex->pValueOptions->fValueMax;
				costumeTailor_GetTextureValueMinMax((PCPart*)pTemp, pTex, pSpecies, &fMin, &fMax);
				if (fMax > fMin && pTex->pValueOptions->fValueDefault >= fMin && pTex->pValueOptions->fValueDefault <= fMax)
				{
					if (!pTemp->pTextureValues) {
						pTemp->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
					}
					if (pTemp->pTextureValues) {
						pTemp->pTextureValues->fDiffuseValue = (((pTex->pValueOptions->fValueDefault - fMin) * 200.0f)/(fMax - fMin)) - 100.0f;
					}
				}
			}
			if (pTex && pTex->pColorOptions)
			{
				if (pTex->pColorOptions->bHasDefaultColor0) *((U32*)g_CostumeEditState.pPart->color0) = *((U32*)pTex->pColorOptions->uDefaultColor0);
				if (pTex->pColorOptions->bHasDefaultColor1) *((U32*)g_CostumeEditState.pPart->color1) = *((U32*)pTex->pColorOptions->uDefaultColor1);
				if (pTex->pColorOptions->bHasDefaultColor2) *((U32*)g_CostumeEditState.pPart->color2) = *((U32*)pTex->pColorOptions->uDefaultColor2);
				if (pTex->pColorOptions->bHasDefaultColor3) *((U32*)g_CostumeEditState.pPart->color3) = *((U32*)pTex->pColorOptions->uDefaultColor3);
			}
		}
		return CostumeCreator_SetHoverDiffuse(pchSysName);
	case kCostumeEditLineType_Texture4:
		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, GET_REF(el->hOwnerRegion), g_CostumeEditState.hRegion);
		SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, GET_REF(el->hOwnerCat), g_CostumeEditState.hCategory);
		SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, GET_REF(el->hOwnerBone), g_CostumeEditState.hBone);
		g_CostumeEditState.pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
		SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, GET_REF(el->hOwnerGeo), g_CostumeEditState.hGeometry);
		SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, GET_REF(el->hOwnerMat), g_CostumeEditState.hMaterial);
		if (!g_CostumeEditState.pHoverCostume) {
			g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
		}
		assert(g_CostumeEditState.pHoverCostume);
		{
			NOCONST(PCPart) *pTemp = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, GET_REF(g_CostumeEditState.hBone), NULL);
			PCTextureDef *pTex = RefSystem_ReferentFromString(g_hCostumeTextureDict, pchSysName);
			if (!pTemp) return false;
			if (pTex && pTex->pValueOptions)
			{
				F32 fMin = pTex->pValueOptions->fValueMin;
				F32 fMax = pTex->pValueOptions->fValueMax;
				costumeTailor_GetTextureValueMinMax((PCPart*)pTemp, pTex, pSpecies, &fMin, &fMax);
				if (fMax > fMin && pTex->pValueOptions->fValueDefault >= fMin && pTex->pValueOptions->fValueDefault <= fMax)
				{
					if (!pTemp->pMovableTexture) {
						pTemp->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
					}
					if (pTemp->pMovableTexture) {
						pTemp->pMovableTexture->fMovableValue = (((pTex->pValueOptions->fValueDefault - fMin) * 200.0f)/(fMax - fMin)) - 100.0f;
					}
				}
			}
			if (pTex && pTex->pColorOptions)
			{
				if (pTex->pColorOptions->bHasDefaultColor0) *((U32*)g_CostumeEditState.pPart->color0) = *((U32*)pTex->pColorOptions->uDefaultColor0);
				if (pTex->pColorOptions->bHasDefaultColor1) *((U32*)g_CostumeEditState.pPart->color1) = *((U32*)pTex->pColorOptions->uDefaultColor1);
				if (pTex->pColorOptions->bHasDefaultColor2) *((U32*)g_CostumeEditState.pPart->color2) = *((U32*)pTex->pColorOptions->uDefaultColor2);
				if (pTex->pColorOptions->bHasDefaultColor3) *((U32*)g_CostumeEditState.pPart->color3) = *((U32*)pTex->pColorOptions->uDefaultColor3);
			}
			if (pTex && pTex->pMovableOptions)
			{
				F32 fMovableMinX, fMovableMaxX, fMovableMinY, fMovableMaxY;
				F32 fMovableMinScaleX, fMovableMaxScaleX, fMovableMinScaleY, fMovableMaxScaleY;
				bool bMovableCanEditPosition, bMovableCanEditRotation, bMovableCanEditScale;
				costumeTailor_GetTextureMovableValues((PCPart*)pTemp, pTex, pSpecies,
					&fMovableMinX, &fMovableMaxX, &fMovableMinY, &fMovableMaxY,
					&fMovableMinScaleX, &fMovableMaxScaleX, &fMovableMinScaleY, &fMovableMaxScaleY,
					&bMovableCanEditPosition, &bMovableCanEditRotation, &bMovableCanEditScale);

				if (!pTemp->pMovableTexture) {
					pTemp->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
				}
				if (pTemp->pMovableTexture) {
					if (fMovableMaxX > fMovableMinX && pTex->pMovableOptions->fMovableDefaultX >= fMovableMinX && pTex->pMovableOptions->fMovableDefaultX <= fMovableMaxX)
					{
						pTemp->pMovableTexture->fMovableX = (((pTex->pMovableOptions->fMovableDefaultX - fMovableMinX) * 200.0f)/(fMovableMaxX - fMovableMinX)) - 100.0f;
					}
					if (fMovableMaxY > fMovableMinY && pTex->pMovableOptions->fMovableDefaultY >= fMovableMinY && pTex->pMovableOptions->fMovableDefaultY <= fMovableMaxY)
					{
						pTemp->pMovableTexture->fMovableY = (((pTex->pMovableOptions->fMovableDefaultY - fMovableMinY) * 200.0f)/(fMovableMaxY - fMovableMinY)) - 100.0f;
					}
					if (fMovableMaxScaleX > fMovableMinScaleX && pTex->pMovableOptions->fMovableDefaultScaleX >= fMovableMinScaleX && pTex->pMovableOptions->fMovableDefaultScaleX <= fMovableMaxScaleX)
					{
						pTemp->pMovableTexture->fMovableScaleX = (((pTex->pMovableOptions->fMovableDefaultScaleX - fMovableMinScaleX) * 100.0f)/(fMovableMaxScaleX - fMovableMinScaleX));
					}
					if (fMovableMaxScaleY > fMovableMinScaleY && pTex->pMovableOptions->fMovableDefaultScaleY >= fMovableMinScaleY && pTex->pMovableOptions->fMovableDefaultScaleY <= fMovableMaxScaleY)
					{
						pTemp->pMovableTexture->fMovableScaleY = (((pTex->pMovableOptions->fMovableDefaultScaleY - fMovableMinScaleY) * 100.0f)/(fMovableMaxScaleY - fMovableMinScaleY));
					}
					pTemp->pMovableTexture->fMovableRotation = pTex->pMovableOptions->fMovableDefaultRotation;
				}
			}
		}
		return CostumeCreator_SetHoverMovable(pchSysName);
	case kCostumeEditLineType_TextureScale:
		return false;
	case kCostumeEditLineType_BodyScale:
		if (el->bHasSlider) return false;
		bsv = (PCBodyScaleValue **)el->eaValues;
		for (i = eaSize(&bsv)-1; i >= 0; --i)
		{
			if (!strcmp(bsv[i]->pcName,pchSysName))
			{
				CostumeCreator_SetHoverBodyScaleByName(el->pcName, bsv[i]->fValue);
				return true;
			}
		}
		CostumeCreator_SetHoverBodyScale(-1, 0);
		return true;
	case kCostumeEditLineType_Overlay:
		if ((!pchSysName) || (!*pchSysName))
		{
			if (g_CostumeEditState.pHoverCostume) {
				StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
				g_CostumeEditState.pHoverCostume = NULL;
				CostumeUI_RegenCostume(true);
			}
		}
		else
		{
			//Must start from original costume when useing an overlay
			StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
			g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);

			crfs = el->eaOverlays;
			for (i = eaSize(&crfs)-1; i >= 0; --i)
			{
				if (!strcmp(crfs[i]->pcName,pchSysName))
				{
					break;
				}
			}
			if (i >= 0)
			{
				COPY_HANDLE(g_CostumeEditState.pConstHoverCostume->hSpecies, g_CostumeEditState.hSpecies);
				if (costumeTailor_ApplyCostumeOverlay(g_CostumeEditState.pConstHoverCostume, NULL, GET_REF(crfs[i]->hPlayerCostume), g_CostumeEditState.eaUnlockedCostumes, "Uniforms", g_CostumeEditState.pSlotType, true, false, true, true))
				{
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
					costumeTailor_MakeCostumeValid(g_CostumeEditState.pHoverCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, false, g_CostumeEditState.bUnlockAll, false, guild_GetGuild(pEnt), false, pExtract, false, NULL);
					CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), NULL, g_CostumeEditState.eaShowItems);
				}
			}
		}
		return true;
	case kCostumeEditLineType_GuildOverlay:
		if ((!pchSysName) || (!*pchSysName))
		{
			if (g_CostumeEditState.pHoverCostume) {
				StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
				g_CostumeEditState.pHoverCostume = NULL;
				CostumeUI_RegenCostume(true);
			}
		}
		else
		{
			//Must start from original costume when useing an overlay
			StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
			g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);

			pch = el->eaGuildOverlays;
			for (i = eaSize(&pch)-1; i >= 0; --i)
			{
				if (!pch[i]->pCostume) continue;
				if (!strcmp(pch[i]->pCostume->pcName,pchSysName))
				{
					break;
				}
			}
			if (i >= 0)
			{
				COPY_HANDLE(g_CostumeEditState.pConstHoverCostume->hSpecies, g_CostumeEditState.hSpecies);
				if (costumeTailor_ApplyCostumeOverlay(g_CostumeEditState.pConstHoverCostume, NULL, pch[i]->pCostume, g_CostumeEditState.eaUnlockedCostumes, "Uniforms", g_CostumeEditState.pSlotType, true, false, true, true))
				{
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
					costumeTailor_MakeCostumeValid(g_CostumeEditState.pHoverCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, false, g_CostumeEditState.bUnlockAll, false, guild_GetGuild(pEnt), false, pExtract, false, g_CostumeEditState.eaPowerFXBones);
					CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), NULL, g_CostumeEditState.eaShowItems);
				}
			}
		}
		return true;
	}

	return false;
}


// This function uses globals!
static bool costumeLineUI_SetLine(SA_PARAM_OP_VALID CostumeEditLine *el, NOCONST(PlayerCostume) *pCostume)
{
	PERFINFO_AUTO_START_FUNC();

	if (!el) {
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, GET_REF(el->hOwnerRegion), g_CostumeEditState.hRegion);
	SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, GET_REF(el->hOwnerCat), g_CostumeEditState.hCategory);
	SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, GET_REF(el->hOwnerBone), g_CostumeEditState.hBone);
	g_CostumeEditState.pPart = costumeTailor_GetPartByBone(pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, GET_REF(el->hOwnerGeo), g_CostumeEditState.hGeometry);
	SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, GET_REF(el->hOwnerMat), g_CostumeEditState.hMaterial);

	PERFINFO_AUTO_STOP_FUNC();
	return true;
}


bool costumeLineUI_SetLineScaleInternal(NOCONST(PlayerCostume) *pCostume, SA_PARAM_OP_VALID CostumeEditLine *el, int scaleNum, F32 fValue)
{
	NOCONST(PCPart) *pPart;
	int l;

	if (!el) return false;
	if (el->iType != kCostumeEditLineType_Scale && el->iType != kCostumeEditLineType_BodyScale && el->iType != kCostumeEditLineType_TextureScale &&
		(el->bHasSlider == 0 || (el->iType != kCostumeEditLineType_Texture0 && el->iType != kCostumeEditLineType_Texture1 && el->iType != kCostumeEditLineType_Texture2 && el->iType != kCostumeEditLineType_Texture3 && el->iType != kCostumeEditLineType_Texture4)))
		return 0;
	if (el->iType == kCostumeEditLineType_BodyScale && !el->bHasSlider) return false;

	pPart = costumeTailor_GetPartByBone(pCostume, GET_REF(el->hOwnerBone), NULL);

	if (el->iType == kCostumeEditLineType_BodyScale)
	{
		costumeTailor_SetBodyScaleByName(pCostume, GET_REF(pCostume->hSpecies), el->pcName, fValue, NULL);
		return true;
	}
	else if (el->iType == kCostumeEditLineType_Scale)
	{
		for (l = eaSize(&pCostume->eaScaleValues)-1; l >= 0; --l)
		{
			if (scaleNum)
			{
				if (!stricmp(pCostume->eaScaleValues[l]->pcScaleName, el->pcName2))
				{
					break;
				}
			}
			else
			{
				if (!stricmp(pCostume->eaScaleValues[l]->pcScaleName, el->pcName))
				{
					break;
				}
			}
		}

		if (l >= 0)
		{
			if (scaleNum)
			{
				el->fScaleValue2 = pCostume->eaScaleValues[l]->fValue = fValue + el->fScaleMin2;
			}
			else
			{
				el->fScaleValue1 = pCostume->eaScaleValues[l]->fValue = fValue + el->fScaleMin1;
			}
		}
		else
		{
			if (scaleNum)
			{
				CostumeCreator_CommonSetBoneScale(pCostume, el->fScaleMin2, el->fScaleMax2, el->pcName2, fValue);
				el->fScaleValue2 = fValue + el->fScaleMin2;
			}
			else
			{
				CostumeCreator_CommonSetBoneScale(pCostume, el->fScaleMin1, el->fScaleMax1, el->pcName, fValue);
				el->fScaleValue1 = fValue + el->fScaleMin1;
			}
		}
	}
	else if (el->iType == kCostumeEditLineType_Texture0)
	{
		if (pPart)
		{
			el->fScaleValue1 = fValue + el->fScaleMin1;
			if (!pPart->pTextureValues) {
				pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
			}
			pPart->pTextureValues->fPatternValue = fValue + el->fScaleMin1;
		}
	}
	else if (el->iType == kCostumeEditLineType_Texture1)
	{
		if (pPart)
		{
			el->fScaleValue1 = fValue + el->fScaleMin1;
			if (!pPart->pTextureValues) {
				pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
			}
			pPart->pTextureValues->fDetailValue = fValue + el->fScaleMin1;
		}
	}
	else if (el->iType == kCostumeEditLineType_Texture2)
	{
		if (pPart)
		{
			el->fScaleValue1 = fValue + el->fScaleMin1;
			if (!pPart->pTextureValues) {
				pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
			}
			pPart->pTextureValues->fSpecularValue = fValue + el->fScaleMin1;
		}
	}
	else if (el->iType == kCostumeEditLineType_Texture3)
	{
		if (pPart)
		{
			el->fScaleValue1 = fValue + el->fScaleMin1;
			if (!pPart->pTextureValues) {
				pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
			}
			pPart->pTextureValues->fDiffuseValue = fValue + el->fScaleMin1;
		}
	}
	else if (el->iType == kCostumeEditLineType_Texture4)
	{
		if (pPart)
		{
			el->fScaleValue1 = fValue + el->fScaleMin1;
			if (!pPart->pMovableTexture) {
				pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
			}
			pPart->pMovableTexture->fMovableValue = fValue + el->fScaleMin1;
		}
	}
	else if (el->iType == kCostumeEditLineType_TextureScale)
	{
		if (pPart)
		{
			if (scaleNum)
			{
				if (!stricmp("PositionY", el->pcName2))
				{
					el->fScaleValue2 = fValue + el->fScaleMin2;
					if (!pPart->pMovableTexture) {
						pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
					}
					pPart->pMovableTexture->fMovableY = fValue + el->fScaleMin2;
				}
				else if (!stricmp("ScaleY", el->pcName2))
				{
					el->fScaleValue2 = fValue + el->fScaleMin2;
					if (!pPart->pMovableTexture) {
						pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
					}
					pPart->pMovableTexture->fMovableScaleY = fValue + el->fScaleMin2;
				}
			}
			else
			{
				if (!stricmp("PositionX", el->pcName))
				{
					el->fScaleValue1 = fValue + el->fScaleMin1;
					if (!pPart->pMovableTexture) {
						pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
					}
					pPart->pMovableTexture->fMovableX = fValue + el->fScaleMin1;
				}
				else if (!stricmp("ScaleX", el->pcName))
				{
					el->fScaleValue1 = fValue + el->fScaleMin1;
					if (!pPart->pMovableTexture) {
						pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
					}
					pPart->pMovableTexture->fMovableScaleX = fValue + el->fScaleMin1;
				}
				else if (!stricmp("Rotation", el->pcName))
				{
					el->fScaleValue1 = fValue + el->fScaleMin1;
					if (!pPart->pMovableTexture) {
						pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
					}
					pPart->pMovableTexture->fMovableRotation = fValue + el->fScaleMin1;
				}
			}
		}
	}

	return true;
}

void costumeLineUI_FillUnlockInfo(CostumeEditLine **eaCostumeEditLines, StashTable stashGeoUnlockMeta, StashTable stashMatUnlockMeta, StashTable stashTexUnlockMeta)
{
	S32 i;
	for (i = 0; i < eaSize(&eaCostumeEditLines); i++)
	{
		CostumeEditLine *pEditLine = eaCostumeEditLines[i];
		switch (pEditLine->iType)
		{
		xcase kCostumeEditLineType_Geometry:
			if (!stashFindPointer(stashGeoUnlockMeta, pEditLine->pcName, &pEditLine->pUnlockInfo)) {
				pEditLine->pUnlockInfo = NULL;
			}
		xcase kCostumeEditLineType_Material:
			if (!stashFindPointer(stashMatUnlockMeta, pEditLine->pcName, &pEditLine->pUnlockInfo)) {
				pEditLine->pUnlockInfo = NULL;
			}
		xcase kCostumeEditLineType_Texture0:
		case kCostumeEditLineType_Texture1:
		case kCostumeEditLineType_Texture2:
		case kCostumeEditLineType_Texture3:
		case kCostumeEditLineType_Texture4:
			if (!stashFindPointer(stashTexUnlockMeta, pEditLine->pcName, &pEditLine->pUnlockInfo)) {
				pEditLine->pUnlockInfo = NULL;
			}
			break;
		}
	}
}


// --------------------------------------------------------------------------
// Expression Functions
// --------------------------------------------------------------------------


// Sets allowed types
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetAllowedTypes");
void costumeLineUI_SetAllowedLineTypes(int types)
{
	COSTUME_UI_TRACE_FUNC();
	g_CostumeEditState.eFindTypes = (CostumeEditLineType)types;
	g_CostumeEditState.bUpdateLines = true;
}


//0 = No show; 1 = Top of list; 2 = Between Pickers and Sliders; 3 = Bottom of List
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetBodyScalesRule");
void costumeLineUI_SetBodyScalesRule(int rule)
{
	COSTUME_UI_TRACE_FUNC();
	g_CostumeEditState.iBodyScalesRule = rule;
	g_CostumeEditState.bUpdateLines = true;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditColorNumberFromLine");
S32 costumeLineUI_GetEditColorNumberFromLine(int iColor, SA_PARAM_OP_VALID CostumeEditLine *el)
{
	COSTUME_UI_TRACE_FUNC();
	if (!costumeLineUI_SetLine(el, g_CostumeEditState.pCostume)) return 0;
	return CostumeCreator_GetEditColorNumber(iColor);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetColorModelFromLine");
void costumeLineUI_GetColorModelFromLine(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, int iColor, SA_PARAM_OP_VALID CostumeEditLine *el)
{
	COSTUME_UI_TRACE_FUNC();
	if (!costumeLineUI_SetLine(el, g_CostumeEditState.pCostume)) return;
	CostumeCreator_GetColorModel(pContext, pGen, iColor);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetColorModelNameFromLine");
const char *costumeLineUI_GetColorModelNameFromLine(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, int iColor, SA_PARAM_OP_VALID CostumeEditLine *el)
{
	COSTUME_UI_TRACE_FUNC();
	if (!costumeLineUI_SetLine(el, g_CostumeEditState.pCostume)) return NULL;
	return CostumeCreator_GetColorModelName(pContext, pGen, iColor);
}


// Get line's color as a UIColor
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditColorFromLine");
SA_RET_NN_VALID UIColor *costumeLineUI_GetEditColorFromLine(int iColor, SA_PARAM_OP_VALID CostumeEditLine* el)
{
	static UIColor temp = {{0, 0, 0, 255}, 0};
	COSTUME_UI_TRACE_FUNC();
	if (!costumeLineUI_SetLine(el, g_CostumeEditState.pCostume)) return &temp;
	return CostumeCreator_GetEditColor(iColor);
}


// Get line's color value
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditColorValueFromLine");
int costumeLineUI_GetEditColorValueFromLine(int iColor, SA_PARAM_OP_VALID CostumeEditLine* el)
{
	COSTUME_UI_TRACE_FUNC();
	if (!costumeLineUI_SetLine(el, g_CostumeEditState.pCostume)) return 0x000000FF;
	return CostumeCreator_GetEditColorValue(iColor);
}


// Get the list of lines
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetLineList");
void costumeLineUI_GetLineList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLines && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaCostumeEditLine, parse_CostumeEditLine);
}

// Get the list of lines, filtered to a specific set of owner bone names.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetLineListFilteredToBones");
void costumeLineUI_GetLineListFilteredToBones(SA_PARAM_NN_VALID UIGen *pGen, const char* pchBones)
{
	static CostumeEditLine** eaLines = NULL;
//	static int iLastLineUpdate = 0;
	int i, j;
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLines && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
//	if (iLastLineUpdate < g_CostumeEditState.iEditLineUpdateIndex)
	{
		char* context = NULL;
		char* pchString = strdup(pchBones);
		char* pTok = strtok_s(pchString, " ", &context);
		PCBoneDef** eaBones = NULL;
//		iLastLineUpdate = g_CostumeEditState.iEditLineUpdateIndex;
		while(pTok)
		{
			//eaPush(&eaBones, RefSystem_ReferentFromString(g_hCostumeBoneDict, pTok));
			PCBoneDef *pBone = CostumeUI_FindBone(pTok, GET_REF(g_CostumeEditState.hSkeleton));
			if (pBone)
				eaPush(&eaBones, pBone);
			pTok = strtok_s(NULL, " ", &context);
		}

		eaClear(&eaLines);
		for (i = 0; i < eaSize(&g_CostumeEditState.eaCostumeEditLine); i++)
		{
 			PCBoneDef* pBone = g_CostumeEditState.eaCostumeEditLine[i] ? GET_REF(g_CostumeEditState.eaCostumeEditLine[i]->hOwnerBone) : NULL;
			
			if (eaBones &&
				(g_CostumeEditState.eaCostumeEditLine[i]->iType != kCostumeEditLineType_Scale) &&
				(g_CostumeEditState.eaCostumeEditLine[i]->iType != kCostumeEditLineType_BodyScale) &&
				(g_CostumeEditState.eaCostumeEditLine[i]->iType != kCostumeEditLineType_Divider) &&
				(g_CostumeEditState.eaCostumeEditLine[i]->iType != kCostumeEditLineType_Region))
			{
				if (pBone)
				{
					for (j = 0; j < eaSize(&eaBones); j++)
					{
						if (eaBones[j] == pBone)
						{
							eaPush(&eaLines, g_CostumeEditState.eaCostumeEditLine[i]);
							break;
						}
					}
				}
			}
			else if ( (pBone && CostumeUI_FindBone(pBone->pcName, GET_REF(g_CostumeEditState.hSkeleton)) == pBone) || (!pBone) )
				eaPush(&eaLines, g_CostumeEditState.eaCostumeEditLine[i]);
		}
		eaDestroy(&eaBones);
		free(pchString);
	}
	ui_GenSetList(pGen, &eaLines, parse_CostumeEditLine);
}

// Get the number lines in the line list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetLineListSize");
int costumeLineUI_GetLineListSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaCostumeEditLine);
}


// Randomize a section of lines
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_RandomizeAreaFromLineList");
void costumeLineUI_RandomizeAreaFromLineList(SA_PARAM_OP_VALID CostumeEditLine *el, int bRandomizeSkin)
{
	COSTUME_UI_TRACE_FUNC();
	if (!el) return;

	if (el->iType == kCostumeEditLineType_Region)
	{
		PCRegion *pRegion = RefSystem_ReferentFromString(g_hCostumeRegionDict, el->pcName);

		if (pRegion)
		{
			Entity *pEnt = CostumeUI_GetSourceEnt();
			if (gConf.bTailorLinesDefaultToNoColorLinkAll)
			{
				g_CostumeEditState.pCostume->eDefaultColorLinkAll = 0;
			}
			costumeRandom_RandomParts(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), guild_GetGuild(pEnt), pRegion, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, true, true, false, false, bRandomizeSkin);
			CostumeUI_RegenCostume(true);
		}
	}
	else if (el->iType == kCostumeEditLineType_Divider)
	{
		costumeRandom_RandomMorphology(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType, el->pcName, false, true, false, g_CostumeEditState.eaUnlockedCostumes, false);
		CostumeUI_RegenCostume(true);
	}
}


// Randomize all the lines
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_RandomizeAllLineListAreas");
void costumeLineUI_RandomizeAllLineListAreas(bool bRandomizeSkin)
{
	int i;
	PCRegion *pRegion;

	COSTUME_UI_TRACE_FUNC();
	for (i = eaSize(&g_CostumeEditState.eaCostumeEditLine) - 1; i >= 0; i--)
	{
		switch (g_CostumeEditState.eaCostumeEditLine[i]->iType)
		{
		case kCostumeEditLineType_Region:
			pRegion = RefSystem_ReferentFromString(g_hCostumeRegionDict, g_CostumeEditState.eaCostumeEditLine[i]->pcName);
			if (pRegion)
			{
				Entity *pEnt = CostumeUI_GetSourceEnt();
				if (gConf.bTailorLinesDefaultToNoColorLinkAll)
				{
					g_CostumeEditState.pCostume->eDefaultColorLinkAll = 0;
				}
				costumeRandom_RandomParts(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), guild_GetGuild(pEnt), pRegion, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, true, true, false, false, bRandomizeSkin);
			}
			break;

		case kCostumeEditLineType_Divider:
			costumeRandom_RandomMorphology(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType, g_CostumeEditState.eaCostumeEditLine[i]->pcName, false, true, false, g_CostumeEditState.eaUnlockedCostumes, false);
			break;
		}
	}

	CostumeUI_RegenCostumeEx(true, true);
}


// Get a specific line
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetLineByBoneName");
SA_RET_OP_VALID CostumeEditLine *costumeLineUI_GetLineByBoneName(SA_PARAM_NN_VALID UIGen *pGen, const char *pchBone, /*CostumeEditLineType*/ int eType)
{
	// Cache the last requested bone, maybe a slight speed up when requested multiple times
	static int iLastIndex = -1;
	PCBoneDef *pBone = pchBone && *pchBone ? CostumeUI_FindBone(pchBone, GET_REF(g_CostumeEditState.hSkeleton)) : NULL;
	CostumeEditLine *el = NULL;
	int i;

	COSTUME_UI_TRACE_FUNC();
	if (!pBone || eType == 0)
	{
		ui_GenSetPointer(pGen, NULL, parse_CostumeEditLine);
		return NULL;
	}

	if (g_CostumeEditState.bUpdateLines && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}

	// Check the cached index
	i = iLastIndex;
	if (i != -1 && i < eaSize(&g_CostumeEditState.eaCostumeEditLine)) {
		if (GET_REF(g_CostumeEditState.eaCostumeEditLine[i]->hOwnerBone) == pBone && !!(g_CostumeEditState.eaCostumeEditLine[i]->iType & eType)) {
			el = g_CostumeEditState.eaCostumeEditLine[i];
		}
	}

	if (!el) {
		for (i = 0; i < eaSize(&g_CostumeEditState.eaCostumeEditLine); i++) {
			if (GET_REF(g_CostumeEditState.eaCostumeEditLine[i]->hOwnerBone) == pBone && !!(g_CostumeEditState.eaCostumeEditLine[i]->iType & eType)) {
				el = g_CostumeEditState.eaCostumeEditLine[i];
				iLastIndex = i;
				break;
			}
		}
	}

	if (!el) {
		ui_GenSetPointer(pGen, NULL, parse_CostumeEditLine);
	} else {
		ui_GenSetPointer(pGen, g_CostumeEditState.eaCostumeEditLine[i], parse_CostumeEditLine);
	}

	return el;
}


// Get the sublist for a line (e.g. the set of materials that can be used)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetSubLineList");
void costumeLineUI_GetSubLineList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID CostumeEditLine *el)
{
	COSTUME_UI_TRACE_FUNC();
	if (el)
	{
		costumeLineUI_GetSubLineListInternal(pGen, el, el->iType, false);
	}
	else
	{
		ui_GenSetList(pGen, NULL, parse_CostumeSubListRow);
	}
}

// Get the sublist for a line (e.g. the set of materials that can be used)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetSubLineListEx");
void costumeLineUI_GetSubLineListEx(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID CostumeEditLine *el, U32 uDisplayFlags)
{
	COSTUME_UI_TRACE_FUNC();
	if (el)
	{
		costumeLineUI_GetSubLineListInternal(pGen, el, el->iType, !!(uDisplayFlags & 1));
	}
	else
	{
		ui_GenSetList(pGen, NULL, parse_CostumeSubListRow);
	}
}


// Get the sublist size of a specific line (e.g. the number of materials available for selection)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetSubLineListSize");
int costumeLineLUI_GetSubLineListSize(SA_PARAM_OP_VALID CostumeEditLine *el)
{
	COSTUME_UI_TRACE_FUNC();
	if (!el || g_CostumeEditState.bUpdateLines || g_CostumeEditState.bUpdateLists) return 0;
	return costumeLineUI_GetCostumeEditSubLineListSizeInternal(el, el->iType);
}


// Get the merged sublist for a line (e.g. the set of materials that can be used), only if merging has been enabled
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMergeSubLineList");
void costumeLineUI_GetMergeSubLineList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID CostumeEditLine *el)
{
	COSTUME_UI_TRACE_FUNC();
	if (el)
	{
		costumeLineUI_GetSubLineListInternal(pGen, el, el->iMergeType, false);
	}
	else
	{
		ui_GenSetList(pGen, NULL, parse_CostumeSubListRow);
	}
}

// Get the merged sublist for a line (e.g. the set of materials that can be used), only if merging has been enabled
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMergeSubLineListEx");
void costumeLineUI_GetMergeSubLineListEx(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID CostumeEditLine *el, U32 uDisplayFlags)
{
	COSTUME_UI_TRACE_FUNC();
	if (el)
	{
		costumeLineUI_GetSubLineListInternal(pGen, el, el->iMergeType, !!(uDisplayFlags & 1));
	}
	else
	{
		ui_GenSetList(pGen, NULL, parse_CostumeSubListRow);
	}
}


// Get the sublist size of a merged line (e.g. the number of materials available for selection), merging has to be enabled for this to work
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMergeSubLineListSize");
int costumeLineUI_GetMergeSubLineListSize(SA_PARAM_OP_VALID CostumeEditLine *el)
{
	COSTUME_UI_TRACE_FUNC();
	if (!el || g_CostumeEditState.bUpdateLines || g_CostumeEditState.bUpdateLists) return 0;
	return costumeLineUI_GetCostumeEditSubLineListSizeInternal(el, el->iMergeType);
}


// Get the display name of the line's currently selected item
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetSubLineSelDisplayName");
const char *costumeLineUI_GetSubLineSelDisplayName(SA_PARAM_OP_VALID CostumeEditLine *el)
{
	COSTUME_UI_TRACE_FUNC();
	if (!el || g_CostumeEditState.bUpdateLines || g_CostumeEditState.bUpdateLists) return NULL;
	return costumeLineUI_GetCostumeEditSubLineSelDisplayNameInternal(el, el->iType);
}


// Get the display name of the line's currently selected merged item
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMergeSubLineSelDisplayName");
const char *costumeLineUI_GetMergeSubLineSelDisplayName(SA_PARAM_OP_VALID CostumeEditLine *el)
{
	COSTUME_UI_TRACE_FUNC();
	if (!el || g_CostumeEditState.bUpdateLines || g_CostumeEditState.bUpdateLists) return NULL;
	return costumeLineUI_GetCostumeEditSubLineSelDisplayNameInternal(el, el->iMergeType);
}


// Test the line's currently selected item to see if it's an Initial costume option
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_IsSubLineSelPlayerInitial");
bool costumeLineUI_IsSubLineSelPlayerInitial(SA_PARAM_OP_VALID CostumeEditLine *el)
{
	COSTUME_UI_TRACE_FUNC();
	if (!el || g_CostumeEditState.bUpdateLines || g_CostumeEditState.bUpdateLists) return true;
	return costumeLineUI_IsMergeSubLineSelPlayerInitialInternal(el, el->iType);
}


// Test the line's currently selected merged item to see if it's an Initial costume option
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_IsMergeSubLineSelPlayerInitial");
bool costumeLineUI_IsMergeSubLineSelPlayerInitial(SA_PARAM_OP_VALID CostumeEditLine *el)
{
	COSTUME_UI_TRACE_FUNC();
	if (!el || g_CostumeEditState.bUpdateLines || g_CostumeEditState.bUpdateLists) return true;
	return costumeLineUI_IsMergeSubLineSelPlayerInitialInternal(el, el->iMergeType);
}


// Get the internal name of the line's currently selected item
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetSubLineSelSysName");
const char *costumeLineUI_GetSubLineSelSysName(SA_PARAM_OP_VALID CostumeEditLine *el)
{
	COSTUME_UI_TRACE_FUNC();
	if (!el || g_CostumeEditState.bUpdateLines || g_CostumeEditState.bUpdateLists) return NULL;
	return costumeLineUI_GetSubLineSelSysNameInternal(el, el->iType);
}


// Get the internal name of the line's currently selected merge item
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMergeSubLineSelSysName");
const char *costumeLineUI_GetMergeSubLineSelSysName(SA_PARAM_OP_VALID CostumeEditLine *el)
{
	COSTUME_UI_TRACE_FUNC();
	if (!el || g_CostumeEditState.bUpdateLines || g_CostumeEditState.bUpdateLists) return NULL;
	return costumeLineUI_GetSubLineSelSysNameInternal(el, el->iMergeType);
}


// Set the line item being edited
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetLineScale");
bool costumeLineUI_SetLineScale(SA_PARAM_OP_VALID CostumeEditLine *el, int scaleNum, F32 fValue)
{
	COSTUME_UI_TRACE_FUNC();
	costumeLineUI_SetLineScaleInternal(g_CostumeEditState.pCostume, el, scaleNum, fValue);
	CostumeUI_RegenCostume(true);
	return true;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetCostumeEditLinesFromCostumeForRegion");
void costumeLineUI_SetCostumeEditLinesFromCostumeForRegion( SA_PARAM_OP_VALID CostumeEditLine *el, int /*PCCostumeStorageType*/ eCostumeType, int iPetNum, int iCostumeIndex )
{
	static CostumeEditLine **s_eaModify = NULL;
	NOCONST(PlayerCostume) *pTarget = g_CostumeEditState.pCostume;
	PlayerCostume *pCostume;
	int i;
	PlayerCostumeSlot *pCostumeSlot;

	COSTUME_UI_TRACE_FUNC();

	if (!CostumeCreator_GetStoreCostumeSlotFromPet(eCostumeType, iPetNum, iCostumeIndex, NULL, &pCostumeSlot))
	{
		return;
	}

	pCostume = pCostumeSlot ? pCostumeSlot->pCostume : NULL;

	if (pCostume && pTarget)
	{
		Entity *pPlayer = CostumeCreator_GetEditPlayerEntity();

		if (!el)
		{
			if (g_CostumeEditState.bUpdateLines && g_CostumeEditState.pCostume)
			{
				CostumeUI_RegenCostume(true);
			}
		}

		if (el && el->iType != kCostumeEditLineType_Region && el->iType != kCostumeEditLineType_Divider)
		{
			return;
		}

		for (i = 0; i < eaSize(&g_CostumeEditState.eaCostumeEditLine); i++)
		{
			if (g_CostumeEditState.eaCostumeEditLine[i] == el)
			{
				i++;
				break;
			}
		}

		eaClear(&s_eaModify);
		for (; i < eaSize(&g_CostumeEditState.eaCostumeEditLine); i++)
		{
			if (g_CostumeEditState.eaCostumeEditLine[i]->iType == kCostumeEditLineType_Region ||
				g_CostumeEditState.eaCostumeEditLine[i]->iType == kCostumeEditLineType_Divider)
			{
				break;
			}
			eaPush(&s_eaModify, g_CostumeEditState.eaCostumeEditLine[i]);
		}

		costumeLineUI_SetCostumeEditLinesGeneral(pCostume, pTarget, !el ? g_CostumeEditState.eaCostumeEditLine : s_eaModify,
					g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType,
					guild_GetGuild(pPlayer), g_CostumeEditState.bUnlockAll);
		CostumeUI_RegenCostume(true);
	}
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetCostumeEditLinesFromCostume");
void costumeLineUI_SetCostumeEditLinesFromCostume( int /*PCCostumeStorageType*/ eCostumeType, int iPetNum, int iCostumeIndex )
{
	COSTUME_UI_TRACE_FUNC();
	costumeLineUI_SetCostumeEditLinesFromCostumeForRegion(NULL, eCostumeType, iPetNum, iCostumeIndex);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetCostumeEditLinesForRegion");
void costumeLineUI_SetCostumeEditLinesForRegion( SA_PARAM_OP_VALID CostumeEditLine *el, const char* pchCostumeDef )
{
	PlayerCostume *pCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchCostumeDef);
	NOCONST(PlayerCostume) *pTarget = g_CostumeEditState.pCostume;
	Entity *pEnt = entActivePlayerPtr();
	static CostumeEditLine **s_eaModify = NULL;
	int i;

	COSTUME_UI_TRACE_FUNC();
	if (!el)
	{
		if (g_CostumeEditState.bUpdateLines && g_CostumeEditState.pCostume) {
			CostumeUI_RegenCostume(true);
		}
	}

	if (el && el->iType != kCostumeEditLineType_Region && el->iType != kCostumeEditLineType_Divider)
	{
		return;
	}

	for (i = 0; i < eaSize(&g_CostumeEditState.eaCostumeEditLine); i++)
	{
		if (g_CostumeEditState.eaCostumeEditLine[i] == el)
		{
			i++;
			break;
		}
	}

	eaClear(&s_eaModify);
	for (; i < eaSize(&g_CostumeEditState.eaCostumeEditLine); i++)
	{
		if (g_CostumeEditState.eaCostumeEditLine[i]->iType == kCostumeEditLineType_Region ||
			g_CostumeEditState.eaCostumeEditLine[i]->iType == kCostumeEditLineType_Divider)
		{
			break;
		}
		eaPush(&s_eaModify, g_CostumeEditState.eaCostumeEditLine[i]);
	}

	costumeLineUI_SetCostumeEditLinesGeneral(pCostume, pTarget, !el ? g_CostumeEditState.eaCostumeEditLine : s_eaModify,
					g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType,
					guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll);
	CostumeUI_RegenCostume(true);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetCostumeEditLines");
void costumeLineUI_SetCostumeEditLines( const char* pchCostumeDef )
{
	COSTUME_UI_TRACE_FUNC();
	costumeLineUI_SetCostumeEditLinesForRegion(NULL, pchCostumeDef);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetCostumeEditLinesAndSkinColor");
void costumeLineUI_SetCostumeEditLinesAndSkinColor( const char* pchCostumeDef )
{
	COSTUME_UI_TRACE_FUNC();
	costumeLineUI_SetCostumeEditLinesForRegion(NULL, pchCostumeDef);
	if (g_CostumeEditState.pCostume)
	{
		PlayerCostume *pCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchCostumeDef);
		if (pCostume)
		{
			g_CostumeEditState.pCostume->skinColor[0] = pCostume->skinColor[0];
			g_CostumeEditState.pCostume->skinColor[1] = pCostume->skinColor[1];
			g_CostumeEditState.pCostume->skinColor[2] = pCostume->skinColor[2];
			g_CostumeEditState.pCostume->skinColor[3] = pCostume->skinColor[3];
			CostumeUI_RegenCostume(false);
		}
	}
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_HoverCostumeEditLinesForRegion");
void costumeLineUI_HoverCostumeEditLinesForRegion( SA_PARAM_OP_VALID CostumeEditLine *el, const char* pchCostumeDef )
{
	PlayerCostume *pCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchCostumeDef);
	NOCONST(PlayerCostume) *pTarget = g_CostumeEditState.pHoverCostume;
	Entity *pEnt = entActivePlayerPtr();
	static CostumeEditLine **s_eaModify = NULL;
	int i;

	COSTUME_UI_TRACE_FUNC();
	if (!pCostume)
	{
		if (g_CostumeEditState.pHoverCostume) {
			StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
			g_CostumeEditState.pHoverCostume = NULL;
			CostumeUI_RegenCostume(true);
		}
		return;
	}

	if (!pTarget) {
		pTarget = g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
	}
	assert(g_CostumeEditState.pHoverCostume);

	if (!el)
	{
		if (g_CostumeEditState.bUpdateLines && g_CostumeEditState.pCostume) {
			if (g_CostumeEditState.pHoverCostume) {
				StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
				g_CostumeEditState.pHoverCostume = NULL;
			}
			CostumeUI_RegenCostume(true);
			if (!g_CostumeEditState.pHoverCostume) {
				g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
			}
			assert(g_CostumeEditState.pHoverCostume);
		}
	}

	if (el && el->iType != kCostumeEditLineType_Region && el->iType != kCostumeEditLineType_Divider)
	{
		return;
	}

	if (el)
	{
		for (i = 0; i < eaSize(&g_CostumeEditState.eaCostumeEditLine); i++)
		{
			if (g_CostumeEditState.eaCostumeEditLine[i] == el)
			{
				i++;
				break;
			}
		}
	}
	else
	{
		i = eaSize(&g_CostumeEditState.eaCostumeEditLine);
	}

	eaClear(&s_eaModify);
	for (; i < eaSize(&g_CostumeEditState.eaCostumeEditLine); i++)
	{
		if (g_CostumeEditState.eaCostumeEditLine[i]->iType == kCostumeEditLineType_Region ||
			g_CostumeEditState.eaCostumeEditLine[i]->iType == kCostumeEditLineType_Divider)
		{
			break;
		}
		eaPush(&s_eaModify, g_CostumeEditState.eaCostumeEditLine[i]);
	}

	costumeLineUI_SetCostumeEditLinesGeneral(pCostume, pTarget, !el ? g_CostumeEditState.eaCostumeEditLine : s_eaModify,
					g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType,
					guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll);
	CostumeUI_costumeView_RegenCostume(g_pCostumeView, (PlayerCostume*)pTarget, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), NULL, g_CostumeEditState.eaShowItems);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_HoverCostumeEditLinesAndSkinColor");
void costumeLineUI_HoverCostumeEditLinesAndSkinColor( const char* pchCostumeDef )
{
	COSTUME_UI_TRACE_FUNC();
	costumeLineUI_HoverCostumeEditLinesForRegion(NULL, pchCostumeDef);
	if (g_CostumeEditState.pHoverCostume)
	{
		PlayerCostume *pCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchCostumeDef);
		if (pCostume)
		{
			g_CostumeEditState.pHoverCostume->skinColor[0] = pCostume->skinColor[0];
			g_CostumeEditState.pHoverCostume->skinColor[1] = pCostume->skinColor[1];
			g_CostumeEditState.pHoverCostume->skinColor[2] = pCostume->skinColor[2];
			g_CostumeEditState.pHoverCostume->skinColor[3] = pCostume->skinColor[3];
			CostumeUI_ValidateAllParts(g_CostumeEditState.pHoverCostume, false, false);
			CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), NULL, g_CostumeEditState.eaShowItems);
		}
	}
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_HoverCostumeEditLines");
void costumeLineUI_HoverCostumeEditLines( const char* pchCostumeDef )
{
	COSTUME_UI_TRACE_FUNC();
	costumeLineUI_HoverCostumeEditLinesForRegion(NULL, pchCostumeDef);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_LineListHideMirrorBones");
void costumeLineUI_LineListHideMirrorBones(bool bHide)
{
	COSTUME_UI_TRACE_FUNC();
	g_CostumeEditState.bLineListHideMirrorBones = bHide;
	g_CostumeEditState.bUpdateLines = true;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_ShowTextureLinesForCurrentPartValuesOnly");
void costumeLineUI_ShowTextureLinesForCurrentPartValuesOnly(bool bFlag)
{
	COSTUME_UI_TRACE_FUNC();
	g_CostumeEditState.bTextureLinesForCurrentPartValuesOnly = bFlag;
	g_CostumeEditState.bUpdateLines = true;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_CombineLines");
void costumeLineUI_FullExplore(bool bFlag)
{
	COSTUME_UI_TRACE_FUNC();
	g_CostumeEditState.bCombineLines = true;
	g_CostumeEditState.bUpdateLines = true;
}


// Set the color of the active part being edited
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetColorAtLine");
void costumeLineUI_SetColorAtLine(S32 iColor, SA_PARAM_OP_VALID CostumeEditLine *el, F32 fR, F32 fG, F32 fB, F32 fA)
{
	COSTUME_UI_TRACE_FUNC();
	if (!costumeLineUI_SetLine(el, g_CostumeEditState.pCostume)) return;
	CostumeCreator_SetColor(iColor, fR, fG, fB, fA);
}


// Set the color of the active part being edited
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverColorAtLine");
void costumeLineUI_SetHoverColorAtLine(S32 iColor, SA_PARAM_OP_VALID CostumeEditLine *el, F32 fR, F32 fG, F32 fB, F32 fA)
{
	COSTUME_UI_TRACE_FUNC();
	if (!costumeLineUI_SetLine(el, g_CostumeEditState.pCostume)) return;
	CostumeCreator_SetHoverColor(iColor, fR, fG, fB, fA);
}


// Set the line item being edited
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetLineItem");
bool costumeLineUI_SetLineItem(const char *pchSysName, SA_PARAM_OP_VALID CostumeEditLine *el)
{
	Entity *pEnt = entActivePlayerPtr();
	GameAccountDataExtract *pExtract;
	COSTUME_UI_TRACE_FUNC();
	if (!el) {
		return false;
	}
	if ((el->iType == kCostumeEditLineType_Overlay) || (el->iType == kCostumeEditLineType_GuildOverlay)) {
		if (g_CostumeEditState.pHoverCostume) {
			StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
			g_CostumeEditState.pHoverCostume = NULL;
		}
	}
	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	if (costumeLineUI_SetLineItemInternal(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies),
			pchSysName, el, el->iType, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eaPowerFXBones,
			g_CostumeEditState.pSlotType, guild_GetGuild(pEnt), pExtract, 
			g_CostumeEditState.bUnlockAll, g_MirrorSelectMode, g_GroupSelectMode)) {
		CostumeUI_RegenCostume(true);
		return true;
	} else {
		return false;
	}
}


// Set the line's merged item being edited
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetLineMergeItem");
bool costumeLineUI_SetLineMergeItem(const char *pchSysName, SA_PARAM_OP_VALID CostumeEditLine *el)
{
	GameAccountDataExtract *pExtract;
	Entity *pEnt = entActivePlayerPtr();
	COSTUME_UI_TRACE_FUNC();
	if (!el) {
		return false;
	}
	if ((el->iType == kCostumeEditLineType_Overlay) || (el->iType == kCostumeEditLineType_GuildOverlay)) {
		if (g_CostumeEditState.pHoverCostume) {
			StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
			g_CostumeEditState.pHoverCostume = NULL;
		}
	}
	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	if (costumeLineUI_SetLineItemInternal(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies),
				pchSysName, el, el->iMergeType, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eaPowerFXBones,
				g_CostumeEditState.pSlotType, guild_GetGuild(pEnt), pExtract, 
				g_CostumeEditState.bUnlockAll, g_MirrorSelectMode, g_GroupSelectMode)) {
		CostumeUI_RegenCostume(true);
		return true;
	} else {
		return false;
	}
}


// Set the line item being edited
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverLineItem");
bool costumeLineUI_SetHoverLineItem(const char *pchSysName, SA_PARAM_OP_VALID CostumeEditLine *el)
{
	COSTUME_UI_TRACE_FUNC();
	if (!el) return false;
	return costumeLineUI_SetHoverLineMergeItemInternal(pchSysName, el, el->iType);
}


// Set the line's merge item being edited
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverLineMergeItem");
bool costumeLineUI_SetHoverLineMergeItem(const char *pchSysName, SA_PARAM_OP_VALID CostumeEditLine *el)
{
	COSTUME_UI_TRACE_FUNC();
	if (!el) return false;
	return costumeLineUI_SetHoverLineMergeItemInternal(pchSysName, el, el->iMergeType);
}


// Clear the scale group list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_ClearScaleGroupList");
void costumeLineUI_ClearScaleGroupList(void)
{
	COSTUME_UI_TRACE_FUNC();
	eaDestroyStruct(&g_CostumeEditState.eaFindScaleGroup, parse_CostumeUIScaleGroup);
	g_CostumeEditState.bUpdateLines = true;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_AddScaleGroup");
void costumeLineUI_AddScaleGroup(const char *pchName)
{
	CostumeUIScaleGroup *pRef = StructCreate(parse_CostumeUIScaleGroup);

	COSTUME_UI_TRACE_FUNC();
	if (!pRef) return;
	if (!pchName)
	{
		StructDestroy(parse_CostumeUIScaleGroup,pRef);
		return;
	}
	pRef->pcName = allocAddString(pchName);
	eaPush(&g_CostumeEditState.eaFindScaleGroup, pRef);
	g_CostumeEditState.bUpdateLines = true;
}


// --------------------------------------------------------------------------
// AST File
// --------------------------------------------------------------------------

#include "gclCostumeLineUI_h_ast.c"
