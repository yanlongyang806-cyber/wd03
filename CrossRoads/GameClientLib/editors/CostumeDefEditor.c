//
// CostumeDefEditor.c
//

#ifndef NO_EDITORS

#include "CostumeCommonLoad.h"
#include "CostumeEditor.h"
#include "CostumeDefEditor.h"
#include "cmdparse.h"
#include "dynFxInfo.h"
#include "EditorPrefs.h"
#include "FileUtil.h"
#include "FolderCache.h"
#include "GfxMaterials.h"
#include "Message.h"
#include "SimpleParser.h"
#include "Strings_opt.h"
#include "StringCache.h"
#include "UIGimmeButton.h"
#include "wlModel.h"

#include "AutoGen/CostumeDefEditor_h_ast.h"
#include "AutoGen/CostumeCommonEnums_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


// Forward declarations
void costumeDefEdit_DefSetBone(CostumeEditDefDoc *pDefDoc, PCBoneDef *pBone);
void costumeDefEdit_DefSetGeo(CostumeEditDefDoc *pDefDoc, PCGeometryDef *pGeo);
void costumeDefEdit_DefSetMat(CostumeEditDefDoc *pDefDoc, PCMaterialDef *pMat);
void costumeDefEdit_DefSetTex(CostumeEditDefDoc *pDefDoc, PCTextureDef *pTex);
static void costumeDefEdit_DefTexNamesRefresh(CostumeEditDefDoc *pDefDoc);
static void costumeDefEdit_DefGeoChildNamesRefresh(CostumeEditDefDoc* pDefDoc);
static void costumeDefEdit_BuildMatConstantGroups(CostumeEditDefDoc* pDefDoc);
static void costumeDefEdit_PromptForGeoAddSave(CostumeEditDefDoc *pDefDoc, PCGeometryAdd **eaAdds);
static void costumeDefEdit_PromptForMatAddSave(CostumeEditDefDoc *pDefDoc, PCMaterialAdd **eaAdds);
static void costumeDefEdit_SaveSubDoc(CostumeEditDefDoc *pDefDoc);

static void costumeDefEdit_UICancelSaveFromPrompt(UIButton *pButton, CostumeEditDefDoc *pDefDoc);
static void costumeDefEdit_UIDefExtraTextureChanged(MEField *pField, bool bFinished, CostumeExtraTextureGroup *pGroup);
static void costumeDefEdit_UIDefFieldChanged(MEField *pField, bool bFinished, CostumeEditDefDoc *pDefDoc);
static void costumeDefEdit_UIColorSetFieldChanged(MEField *pField, bool bFinished, CostumeEditDefDoc *pDefDoc);
static void costumeDefEdit_UIDefTextureChanged(MEField *pField, bool bFinished, CostumeEditDefDoc *pDefDoc);
static void costumeDefEdit_UIExtraTexAdd(UIButton *pButton, CostumeEditDefDoc *pDefDoc);
static void costumeDefEdit_UIExtraTexRemove(UIButton *pButton, CostumeExtraTextureGroup *pTexGroup);
static void costumeDefEdit_UIGeoChildAdd(UIButton* pButton, CostumeGeoChildBoneGroup* pBoneGroup);
static void costumeDefEdit_UIGeoChildRemove(UIButton* pButton, CostumeGeoChildGeoGroup* pChildGroup);
static void costumeDefEdit_UIGeoFXAdd(UIButton* pButton, CostumeEditDefDoc* pDefDoc);
static void costumeDefEdit_UIGeoFXRemove(UIButton* pButton, CostumeDefFxGroup* pGroup);
static void costumeDefEdit_UIGeoFXSwapAdd(UIButton* pButton, CostumeEditDefDoc* pDefDoc);
static void costumeDefEdit_UIGeoFXSwapRemove(UIButton* pButton, CostumeDefFxSwapGroup* pGroup);
static void costumeDefEdit_UIGeoSaveButton(UIButton *pButton, CostumeAddSaveData *pData);
static void costumeDefEdit_UIGeoSaveCancel(UIButton *pButton, CostumeEditDefDoc *pDefDoc);
static void costumeDefEdit_UIMatConstantColorGroupChanged(UIColorButton *pButton, bool bFinished, CostumeMatConstantGroup *pGroup);
static void costumeDefEdit_UIMatConstantValueGroupChanged(UISliderTextEntry *pSlider, bool bFinished, CostumeMatConstantGroup *pGroup);
static void costumeDefEdit_UIMatConstantGroupToggled(UICheckButton *pButton, CostumeMatConstantGroup *pGroup);
static void costumeDefEdit_UIMatFXSwapAdd(UIButton* pButton, CostumeEditDefDoc* pDefDoc);
static void costumeDefEdit_UIMatFXSwapRemove(UIButton* pButton, CostumeDefFxSwapGroup* pGroup);
static void costumeDefEdit_UIMatSaveButton(UIButton *pButton, CostumeAddSaveData *pData);
static void costumeDefEdit_UIMatSaveCancel(UIButton *pButton, CostumeEditDefDoc *pDefDoc);
static void costumeDefEdit_UIMenuSelectApplyGeo(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc);
static void costumeDefEdit_UIMenuSelectApplyMat(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc);
static void costumeDefEdit_UIMenuSelectApplyTex(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc);
static void costumeDefEdit_UIMenuSelectRemoveMat(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc);
static void costumeDefEdit_UIMenuSelectRemoveTex(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc);
static void costumeDefEdit_DictChanged(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData);

static int costumeDefEdit_CompareMatConstantGroups(const CostumeMatConstantGroup **left, const CostumeMatConstantGroup **right);
static int costumeDefEdit_CompareGeoChildGroups(const CostumeGeoChildGeoGroup** left, const CostumeGeoChildGeoGroup** right);

EMTaskStatus costumeDefEdit_SaveGeoDef(CostumeEditDoc *pDoc, CostumeEditDefDoc *pDefDoc);

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

// Clipboard
static PCGeometryDef *gGeoDefClipboard = NULL;
static PCMaterialDef *gMatDefClipboard = NULL;
static PCTextureDef *gTexDefClipboard = NULL;

// Cache of file names for choosers
static const char **g_eaGeoFileNames = NULL;
static const char **g_eaMatFileNames = NULL;
static const char **g_eaTexFileNames = NULL;

// Empty defs for comparison use
static PCGeometryDef *gNoGeoDef = NULL;
static PCMaterialDef *gNoMatDef = NULL;
static PCTextureDef *gNoTexDef = NULL;

extern bool gUseDispNames;
extern CostumeEditDefDoc *gDefDoc;

static UIWindow *pGlobalWindow = NULL;

static UISkin *gDefExpanderSkin = NULL;

static bool gMovConstExpVisible = true;

//---------------------------------------------------------------------------------------------------
// Data Manipulation
//---------------------------------------------------------------------------------------------------


static void costumeDefEdit_AddMaterialsToGeo(CostumeEditDefDoc *pDefDoc, CostumeSaveType eForceSaveType, char *pcForceName)
{
	DictionaryEArrayStruct *pGeoAddStruct = resDictGetEArrayStruct(g_hCostumeGeometryAddDict);
	PCGeometryDef *pGeo = NULL;
	PCGeometryAdd **eaAdds = NULL;
	PCMaterialDef **eaMats = NULL;
	EMFile *pEMFile = NULL;
	ResourceInfo *pResInfo = NULL;
	int i,j;

	if (!pDefDoc->pcGeoForMatsToAdd) {
		// No parent to add to
		// TODO: Provide notice dialog
		return;
	}

	// Remove duplicate materials
	if (pDefDoc->pcGeoForMatsToAdd) {
		pGeo = RefSystem_ReferentFromString(g_hCostumeGeometryDict, pDefDoc->pcGeoForMatsToAdd);
	}
	if (pGeo) {
		costumeTailor_GetValidMaterials(NULL, pGeo, NULL/*species*/, NULL, NULL, NULL, &eaMats, false, false, true);
		for(i=eaSize(&pDefDoc->eaMatsToAdd)-1; i>=0; --i) {
			for(j=eaSize(&eaMats)-1; j>=0; --j) {
				if (stricmp(eaMats[j]->pcName, pDefDoc->eaMatsToAdd[i]) == 0) {
					free(pDefDoc->eaMatsToAdd[i]);
					eaRemove(&pDefDoc->eaMatsToAdd, i);
					break;
				}
			}
		}
		eaDestroy(&eaMats);
	}
	if (!eaSize(&pDefDoc->eaMatsToAdd)) {
		// Nothing to add or remove, so done
		return;
	}

	// Get the geometry
	pGeo = RefSystem_ReferentFromString(g_hCostumeGeometryDict, pDefDoc->pcGeoForMatsToAdd);
	if (pGeo) {
		pResInfo = resGetInfo(g_hCostumeGeometryDict, pGeo->pcName);
	}

	// If parent is not new and is checked out, add and save it
	if (pGeo && ((!pResInfo || resIsWritable(pResInfo->resourceDict, pResInfo->resourceName)) || (eForceSaveType == SaveType_GeoDef))) {
		// Add to geo and save it
		pDefDoc->saveRequested[SaveType_GeoDef] = true;
		pDefDoc->bGeoDefSaved = false;
		if (pDefDoc->pGeoDefToSave) {
			StructDestroy(parse_PCGeometryDef, pDefDoc->pGeoDefToSave);
		}
		pDefDoc->pGeoDefToSave = StructClone(parse_PCGeometryDef, pGeo);
		langMakeEditorCopy(parse_PCGeometryDef, pDefDoc->pGeoDefToSave, false);
		pDefDoc->pGeoDefToSaveOrig = StructClone(parse_PCGeometryDef, pDefDoc->pGeoDefToSave);
		assert(pDefDoc->pGeoDefToSave);

		// Duplicates were removed earlier so add them all
		for(i=0; i<eaSize(&pDefDoc->eaMatsToAdd); ++i) {
			const char *pcName = allocAddString(pDefDoc->eaMatsToAdd[i]);
			eaPush(&pDefDoc->pGeoDefToSave->eaAllowedMaterialDefs, pcName);
		}

		// Now save the geo def... should work unless validation fails
		costumeDefEdit_SaveSubDoc(pDefDoc);

		// TODO: If parent is also edited, then add to it as well
		return;
	}

	// Find existing AddGeos for the parent geo
	for(i=eaSize(&pGeoAddStruct->ppReferents)-1; i>=0; --i) {
		PCGeometryAdd *pAdd = (PCGeometryAdd*)pGeoAddStruct->ppReferents[i];
		if (pAdd->pcGeoName && (stricmp(pAdd->pcGeoName, pDefDoc->pcGeoForMatsToAdd) == 0)) {
			// Track the add
			eaPush(&eaAdds, pAdd);
		}
	}

	//// Find if one of the existing addgeos is editable
	//for(i=eaSize(&eaAdds)-1; i>=0; --i) {
	//	pResInfo = resGetInfo(g_hCostumeGeometryAddDict, eaAdds[i]->pcName);
	//	if ((!pResInfo || pResInfo->bWritable) || ((eForceSaveType == SaveType_GeoAdd) && (stricmp(pcForceName, eaAdds[i]->pcName) == 0))) {
	//		// Add to the geoadd and save it
	//		pDefDoc->eSaveType = SaveType_GeoAdd;
	//		if (pDefDoc->pGeoAddToSave) {
	//			StructDestroy(parse_PCGeometryAdd, pDefDoc->pGeoAddToSave);
	//		}
	//		pDefDoc->pGeoAddToSave = StructClone(parse_PCGeometryAdd, eaAdds[i]);
	//		assert(pDefDoc->pGeoAddToSave);
	//		for(i=0; i<eaSize(&pDefDoc->eaMatsToAdd); ++i) {
	//			PCMaterialNameRef *pRef = StructCreate(parse_PCMaterialNameRef);
	//			pRef->pcName = allocAddString(pDefDoc->eaMatsToAdd[i]);
	//			eaPush(&pDefDoc->pGeoAddToSave->eaAllowedMaterialDefs, pRef);
	//		}
	//		eaDestroy(&eaAdds);

	//		costumeDefEdit_SaveSubDoc(pDefDoc);
	//		return;
	//	}
	//}

	// If no existing is editable, prompt
	costumeDefEdit_PromptForGeoAddSave(pDefDoc, eaAdds);
	eaDestroy(&eaAdds);
}


static void costumeDefEdit_AddTexturesToMaterial(CostumeEditDefDoc *pDefDoc, CostumeSaveType eForceSaveType, char *pcForceName)
{
	DictionaryEArrayStruct *pMatAddStruct = resDictGetEArrayStruct(g_hCostumeMaterialAddDict);
	PCMaterialDef *pMat = NULL;
	PCMaterialAdd **eaAdds = NULL;
	PCTextureDef **eaTexs = NULL;
	EMFile *pEMFile = NULL;
	ResourceInfo *pResInfo = NULL;
	int i,j;

	if (!pDefDoc->pcMatForTexsToAdd) {
		// No parent to add to
		// TODO: Provide notice dialog
		return;
	}

	// Remove duplicate textures
	if (pDefDoc->pcMatForTexsToAdd) {
		pMat = RefSystem_ReferentFromString(g_hCostumeMaterialDict, pDefDoc->pcMatForTexsToAdd);
	}
	if (pMat) {
		costumeTailor_GetValidTextures(NULL, pMat, NULL/*species*/, NULL, NULL, NULL, NULL, NULL,
							kPCTextureType_Pattern | kPCTextureType_Detail | kPCTextureType_Specular | kPCTextureType_Diffuse | kPCTextureType_Movable | kPCTextureType_Other, 
							&eaTexs, false, false, true);
		for(i=eaSize(&pDefDoc->eaTexsToAdd)-1; i>=0; --i) {
			for(j=eaSize(&eaTexs)-1; j>=0; --j) {
				if (stricmp(eaTexs[j]->pcName, pDefDoc->eaTexsToAdd[i]) == 0) {
					free(pDefDoc->eaTexsToAdd[i]);
					eaRemove(&pDefDoc->eaTexsToAdd, i);
					break;
				}
			}
		}
		eaDestroy(&eaTexs);
	}
	if (!eaSize(&pDefDoc->eaTexsToAdd)) {
		// Nothing to add, so done
		return;
	}

	// Get the material
	pMat = RefSystem_ReferentFromString(g_hCostumeMaterialDict, pDefDoc->pcMatForTexsToAdd);
	if (pMat) {
		pResInfo = resGetInfo(g_hCostumeMaterialDict, pMat->pcName);
	}

	// If parent is not new and is checked out, add and save it
	if (pMat && ((!pResInfo || resIsWritable(pResInfo->resourceDict, pResInfo->resourceName)) || (eForceSaveType == SaveType_GeoDef))) {
		// Add to material and save it
		pDefDoc->saveRequested[SaveType_MatDef] = true;
		pDefDoc->bMatDefSaved = false;
		if (pDefDoc->pMatDefToSave) {
			StructDestroy(parse_PCMaterialDef, pDefDoc->pMatDefToSave);
		}
		pDefDoc->pMatDefToSave = StructClone(parse_PCMaterialDef, pMat);
		langMakeEditorCopy(parse_PCMaterialDef, pDefDoc->pMatDefToSave, false);
		pDefDoc->pMatDefToSaveOrig = StructClone(parse_PCMaterialDef, pDefDoc->pMatDefToSave);
		assert(pDefDoc->pMatDefToSave);

		// Duplicates were removed earlier so add them all
		for(i=0; i<eaSize(&pDefDoc->eaTexsToAdd); ++i) {
			const char *pcName = allocAddString(pDefDoc->eaTexsToAdd[i]);
			eaPush(&pDefDoc->pMatDefToSave->eaAllowedTextureDefs, pcName);
		}

		// Now save the mat def... should work unless validation fails
		costumeDefEdit_SaveSubDoc(pDefDoc);

		// TODO: If parent is also edited, then add to it as well
		return;
	}

	// Find existing AddMats for the parent mat
	for(i=eaSize(&pMatAddStruct->ppReferents)-1; i>=0; --i) {
		PCMaterialAdd *pAdd = (PCMaterialAdd*)pMatAddStruct->ppReferents[i];
		if ((pAdd->pcMatName && stricmp(pAdd->pcMatName, pDefDoc->pcMatForTexsToAdd) == 0) ||
			(!pAdd->pcMatName && stricmp(pAdd->pcName, pDefDoc->pcMatForTexsToAdd) == 0)) {
			// Track the add
			eaPush(&eaAdds, pAdd);
		}
	}

	//// Find if one of the existing addmats is editable
	//for(i=eaSize(&eaAdds)-1; i>=0; --i) {
	//	pResInfo = resGetInfo(g_hCostumeMaterialAddDict, eaAdds[i]->pcName);
	//	if ((!pResInfo || pResInfo->bWritable) || ((eForceSaveType == SaveType_GeoAdd) && (stricmp(pcForceName, eaAdds[i]->pcName) == 0))){
	//		// Add to the matadd and save it
	//		pDefDoc->eSaveType = SaveType_MatAdd;
	//		if (pDefDoc->pMatAddToSave) {
	//			StructDestroy(parse_PCMaterialAdd, pDefDoc->pMatAddToSave);
	//		}
	//		pDefDoc->pMatAddToSave = StructClone(parse_PCMaterialAdd, eaAdds[i]);
	//		assert(pDefDoc->pMatAddToSave);
	//		for(i=0; i<eaSize(&pDefDoc->eaTexsToAdd); ++i) {
	//			PCTextureNameRef *pRef = StructCreate(parse_PCTextureNameRef);
	//			pRef->pcName = allocAddString(pDefDoc->eaTexsToAdd[i]);
	//			eaPush(&pDefDoc->pMatAddToSave->eaAllowedTextureDefs, pRef);
	//		}
	//		eaDestroy(&eaAdds);

	//		costumeDefEdit_SaveSubDoc(pDefDoc);
	//		return;
	//	}
	//}

	// If no existing is editable, prompt
	costumeDefEdit_PromptForMatAddSave(pDefDoc, eaAdds);
	eaDestroy(&eaAdds);
}


static void costumeDefEdit_CleanupPromptAddData(CostumeEditDefDoc *pDefDoc)
{
	int i;

	for(i=eaSize(&pDefDoc->eaPromptAddData)-1; i>=0; --i) {
		SAFE_FREE(pDefDoc->eaPromptAddData[i]->pcName);
		SAFE_FREE(pDefDoc->eaPromptAddData[i]->pcFileName);
		free(pDefDoc->eaPromptAddData[i]);
	}
	eaDestroy(&pDefDoc->eaPromptAddData);
}


static int costumeDefEdit_CompareMatConstantGroups(const CostumeMatConstantGroup **left, const CostumeMatConstantGroup **right)
{
	return stricmp((*left)->pcName, (*right)->pcName);
}


static int costumeDefEdit_CompareGeoChildGroups(const CostumeGeoChildGeoGroup** left, const CostumeGeoChildGeoGroup** right)
{
	return stricmp((*left)->pcGeoName, (*right)->pcGeoName);
}


static const char *costumeDefEdit_CreateUniqueName(const char *pcDictName, const char *pcBaseName)
{
	char buf[260];
	int count = 1;

	strcpy(buf, pcBaseName);
	while(RefSystem_ReferentFromString(pcDictName, buf)) {
		sprintf(buf, "%s_%d", pcBaseName, count);
		++count;
	}
	return allocAddString(buf);
}


void costumeDefEdit_DefRefresh(CostumeEditDefDoc *pDefDoc)
{
	if (pDefDoc->bGeoDefSaved ||
		(pDefDoc->pOrigGeoDef && !StructCompare(parse_PCGeometryDef, pDefDoc->pOrigGeoDef, pDefDoc->pCurrentGeoDef, 0, 0, 0))) {
		// Geo is not edited, so refresh it
			PCGeometryDef *pGeoDef = pDefDoc->pCurrentGeoDef ? RefSystem_ReferentFromString(g_hCostumeGeometryDict, pDefDoc->pCurrentGeoDef->pcName) : NULL;
		if (pGeoDef) {
			costumeDefEdit_DefSetGeo(pDefDoc, pGeoDef);
			//pDefDoc->bGeoDefSaved = false;
		} else if (pDefDoc->bGeoDefSaved) {
			costumeDefEdit_DefSetGeo(pDefDoc, gNoGeoDef);
		}
	}
	if (pDefDoc->bMatDefSaved ||
		(pDefDoc->pOrigMatDef && !StructCompare(parse_PCMaterialDef, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, 0, 0, 0))) {
		// Mat is not edited, so refresh it
		PCMaterialDef *pMatDef = pDefDoc->pCurrentMatDef ? RefSystem_ReferentFromString(g_hCostumeMaterialDict, pDefDoc->pCurrentMatDef->pcName) : NULL;
		if (pMatDef) {
			costumeDefEdit_DefSetMat(pDefDoc, pMatDef);
			//pDefDoc->bMatDefSaved = false;
		} else if (pDefDoc->bMatDefSaved) {
			costumeDefEdit_DefSetMat(pDefDoc, gNoMatDef);
		}
	}
	if (pDefDoc->bTexDefSaved ||
		(pDefDoc->pOrigTexDef && !StructCompare(parse_PCTextureDef, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, 0, 0, 0))) {
		// Tex is not edited, so refresh it
		PCTextureDef *pTexDef = pDefDoc->pCurrentTexDef ? RefSystem_ReferentFromString(g_hCostumeTextureDict, pDefDoc->pCurrentTexDef->pcName) : NULL;
		if (pTexDef) {
			costumeDefEdit_DefSetTex(pDefDoc, pTexDef);
			//pDefDoc->bTexDefSaved = false;
		} else if (pDefDoc->bTexDefSaved) {
			costumeDefEdit_DefSetTex(pDefDoc, gNoTexDef);
		}
	}
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


void costumeDefEdit_DefMakeScope(CostumeEditDefDoc *pDefDoc, char *pcType, char **ppcScope)
{
	PCSkeletonDef *pSkel = NULL;
	char skelScope[260];
	if (pDefDoc->pcSkeleton) {
		pSkel = RefSystem_ReferentFromString(g_hCostumeSkeletonDict, pDefDoc->pcSkeleton);
		assert(pSkel);
		if (strnicmp(pSkel->pcFileName, "defs/costumes/definitions/", 26) == 0) {
			char *ptr;
			strcpy(skelScope, pSkel->pcFileName + 26);
			ptr = strrchr(skelScope, '/');
			if (ptr)
				*ptr = '\0';
			if (strlen(skelScope) < 2) {
				strcpy(skelScope, pSkel->pcName);
			}
		} else {
			strcpy(skelScope, pSkel->pcName);
		}
	} else {
		strcpy(skelScope, "Shared");
	}
	estrPrintf(ppcScope, "%s/%s", skelScope, pcType);
}


void costumeDefEdit_DefSetBone(CostumeEditDefDoc *pDefDoc, PCBoneDef *pBone)
{
	pDefDoc->pCurrentBoneDef = pBone;
	//if (pDefDoc->pCurrentBoneDef) {
	//	StructDestroy(parse_PCBoneDef, pDefDoc->pCurrentBoneDef);
	//}
	//if (pBone) {
	//	pDefDoc->pCurrentBoneDef = StructClone(parse_PCBoneDef, pBone);
	//	assert(pDefDoc->pCurrentBoneDef);
	//	langMakeEditorCopy(parse_PCBoneDef, pDefDoc->pCurrentBoneDef, false);
	//}
	//else {
	//	PCSkeletonDef *pSkel = NULL;
	//	char *estrScope = NULL;
	//	costumeDefEdit_DefMakeScope(pDefDoc, "Geometries", &estrScope);
	//	pDefDoc->pCurrentGeoDef = StructCreate(parse_PCGeometryDef);
	//	pDefDoc->pCurrentGeoDef->pcName = costumeDefEdit_CreateUniqueName("CostumeGeometry", "NewGeometry");
	//	pDefDoc->pCurrentGeoDef->pcScope = allocAddString(estrScope);
	//	resFixFilename(g_hCostumeGeometryDict, pDefDoc->pCurrentGeoDef->pcName, pDefDoc->pCurrentGeoDef);
	//	if (pDefDoc->pcSkeleton) {
	//		pSkel = RefSystem_ReferentFromString(g_hCostumeSkeletonDict, pDefDoc->pcSkeleton);
	//		assert(pSkel);
	//		pDefDoc->pCurrentGeoDef->eRestriction = pSkel->eRestriction;
	//	}
	//	if (pDefDoc->pCurrentBoneDef) {
	//		SET_HANDLE_FROM_STRING(g_hCostumeBoneDict, pDefDoc->pCurrentBoneDef->pcName, pDefDoc->pCurrentGeoDef->hBone);
	//	}
	//	for(i=eaSize(&pDefDoc->eaCategories)-1; i>=0; --i) {
	//		PCCategoryRef *pCatRef = StructCreate(parse_PCCategoryRef);
	//		SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, pDefDoc->eaCategories[i], pCatRef->hCategory);
	//		eaPush(&pDefDoc->pCurrentGeoDef->eaCategories, pCatRef);
	//	}
	//	estrDestroy(&estrScope);
	//}

	//if (pDefDoc->pCurrentBoneDef && !pDefDoc->pCurrentBoneDef->displayNameMsg.pEditorCopy) {
	//	char buf[1024];
	//	sprintf(buf, "Costume.Bone.%s", pDefDoc->pCurrentBoneDef->pcName);
	//	pDefDoc->pCurrentBoneDef->displayNameMsg.pEditorCopy = langCreateMessage(MKP_COSTUME_BONE, buf, "Costume bone name", "Costume/Bone", pDefDoc->pCurrentBoneDef->pcName);
	//}

	//pDefDoc->pcLastGeoChangeName = NULL;
}


void costumeDefEdit_DefSetGeo(CostumeEditDefDoc *pDefDoc, PCGeometryDef *pGeo)
{
	int i;

	if (pDefDoc->pCurrentGeoDef) {
		devassert(pGeo != pDefDoc->pCurrentGeoDef);
		StructDestroy(parse_PCGeometryDef, pDefDoc->pCurrentGeoDef);
	}
	if (pDefDoc->pOrigGeoDef) {
		devassert(pGeo != pDefDoc->pOrigGeoDef);
		StructDestroy(parse_PCGeometryDef, pDefDoc->pOrigGeoDef);
		pDefDoc->pOrigGeoDef = NULL;
	}
	if (pGeo) {
		pDefDoc->pCurrentGeoDef = StructClone(parse_PCGeometryDef, pGeo);
		assert(pDefDoc->pCurrentGeoDef);
		langMakeEditorCopy(parse_PCGeometryDef, pDefDoc->pCurrentGeoDef, false);
		if (pGeo->pcName) { // No name means it's not editable
			pDefDoc->pOrigGeoDef = StructClone(parse_PCGeometryDef, pDefDoc->pCurrentGeoDef);
		}
	} else {
		PCSkeletonDef *pSkel = NULL;
		char *estrScope = NULL;
		costumeDefEdit_DefMakeScope(pDefDoc, "Geometries", &estrScope);
		pDefDoc->pCurrentGeoDef = StructCreate(parse_PCGeometryDef);
		pDefDoc->pCurrentGeoDef->pcName = costumeDefEdit_CreateUniqueName("CostumeGeometry", "NewGeometry");
		pDefDoc->pCurrentGeoDef->pcScope = allocAddString(estrScope);
		resFixFilename(g_hCostumeGeometryDict, pDefDoc->pCurrentGeoDef->pcName, pDefDoc->pCurrentGeoDef);
		if (pDefDoc->pcSkeleton) {
			pSkel = RefSystem_ReferentFromString(g_hCostumeSkeletonDict, pDefDoc->pcSkeleton);
			assert(pSkel);
			//pDefDoc->pCurrentGeoDef->eRestriction = pSkel->eRestriction;
			pDefDoc->pCurrentGeoDef->eRestriction = kPCRestriction_NPC;
		}
		if (pDefDoc->pCurrentBoneDef) {
			SET_HANDLE_FROM_STRING(g_hCostumeBoneDict, pDefDoc->pCurrentBoneDef->pcName, pDefDoc->pCurrentGeoDef->hBone);
		}
		for(i=eaSize(&pDefDoc->eaCategories)-1; i>=0; --i) {
			PCCategoryRef *pCatRef = StructCreate(parse_PCCategoryRef);
			SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, pDefDoc->eaCategories[i], pCatRef->hCategory);
			eaPush(&pDefDoc->pCurrentGeoDef->eaCategories, pCatRef);
		}
		estrDestroy(&estrScope);
	}

	if (!pDefDoc->pCurrentGeoDef->pOptions)
		pDefDoc->pCurrentGeoDef->pOptions = StructCreate(parse_PCGeometryOptions);
	if (!pDefDoc->pCurrentGeoDef->pClothData)
		pDefDoc->pCurrentGeoDef->pClothData= StructCreate(parse_PCGeometryClothData);

	if (!pDefDoc->pCurrentGeoDef->displayNameMsg.pEditorCopy) {
		char buf[1024];
		sprintf(buf, "Costume.Geometry.%s", pDefDoc->pCurrentGeoDef->pcName);
		pDefDoc->pCurrentGeoDef->displayNameMsg.pEditorCopy = langCreateMessageWithTerseKey(MKP_COSTUME_GEO, buf, "Costume geometry name", "Costume/Geometry", pDefDoc->pCurrentGeoDef->pcName);
	}

	pDefDoc->pcLastGeoChangeName = NULL;
	
	costumeDefEdit_DefGeoChildNamesRefresh(pDefDoc);
}


void costumeDefEdit_DefSetMat(CostumeEditDefDoc *pDefDoc, PCMaterialDef *pMat)
{
	if (pDefDoc->pCurrentMatDef) {
		StructDestroy(parse_PCMaterialDef, pDefDoc->pCurrentMatDef);
	}
	if (pDefDoc->pOrigMatDef) {
		StructDestroy(parse_PCMaterialDef, pDefDoc->pOrigMatDef);
		pDefDoc->pOrigMatDef = NULL;
	}
	if (pMat) {
		pDefDoc->pCurrentMatDef = StructClone(parse_PCMaterialDef, pMat);
		assert(pDefDoc->pCurrentMatDef);
		langMakeEditorCopy(parse_PCMaterialDef, pDefDoc->pCurrentMatDef, false);
		if (pMat->pcName) { // No name means its not editable
			pDefDoc->pOrigMatDef = StructClone(parse_PCMaterialDef, pDefDoc->pCurrentMatDef);
		}
	} else {
		char *estrScope = NULL;
		pDefDoc->pCurrentMatDef = StructCreate(parse_PCMaterialDef);
		pDefDoc->pCurrentMatDef->pcName = costumeDefEdit_CreateUniqueName("CostumeMaterial", "NewMaterial");
		costumeDefEdit_DefMakeScope(pDefDoc, "Materials", &estrScope);
		pDefDoc->pCurrentMatDef->pcScope = allocAddString(estrScope);
		resFixFilename(g_hCostumeMaterialDict, pDefDoc->pCurrentMatDef->pcName, pDefDoc->pCurrentMatDef);
		if (pDefDoc->pCurrentGeoDef) {
			pDefDoc->pCurrentMatDef->eRestriction = pDefDoc->pCurrentGeoDef->eRestriction;
		}
		estrDestroy(&estrScope);
	}

	pDefDoc->pcLastMatChangeName = NULL;

	assert(pDefDoc->pCurrentMatDef);

	if (!pDefDoc->pCurrentMatDef->displayNameMsg.pEditorCopy) {
		char buf[1024];
		sprintf(buf, "Costume.Material.%s", pDefDoc->pCurrentMatDef->pcName);
		pDefDoc->pCurrentMatDef->displayNameMsg.pEditorCopy = langCreateMessageWithTerseKey(MKP_COSTUME_MAT, buf, "Costume material name", "Costume/Material", pDefDoc->pCurrentMatDef->pcName);
	}

	if (!pDefDoc->pCurrentMatDef->pColorOptions)
		pDefDoc->pCurrentMatDef->pColorOptions = StructCreate(parse_PCMaterialColorOptions);
	if (!pDefDoc->pCurrentMatDef->pOptions)
		pDefDoc->pCurrentMatDef->pOptions = StructCreate(parse_PCMaterialOptions);

	// if pOrigMatDef then make sure that the color and options are not null
	if(pDefDoc->pOrigMatDef)
	{
		if(!pDefDoc->pOrigMatDef->pColorOptions)
		{
			pDefDoc->pOrigMatDef->pColorOptions = StructCreate(parse_PCMaterialColorOptions);
		}
		if (!pDefDoc->pOrigMatDef->pOptions)
		{
			pDefDoc->pOrigMatDef->pOptions = StructCreate(parse_PCMaterialOptions);
		}
	}

	// Set up material options
	pDefDoc->origDefaults.bAllowGlow0 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowGlow[0];
	pDefDoc->origDefaults.bAllowGlow1 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowGlow[1];
	pDefDoc->origDefaults.bAllowGlow2 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowGlow[2];
	pDefDoc->origDefaults.bAllowGlow3 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowGlow[3];
	pDefDoc->currentDefaults.bAllowGlow0 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowGlow[0];
	pDefDoc->currentDefaults.bAllowGlow1 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowGlow[1];
	pDefDoc->currentDefaults.bAllowGlow2 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowGlow[2];
	pDefDoc->currentDefaults.bAllowGlow3 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowGlow[3];

	pDefDoc->origDefaults.bAllowReflect0 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowReflection[0];
	pDefDoc->origDefaults.bAllowReflect1 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowReflection[1];
	pDefDoc->origDefaults.bAllowReflect2 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowReflection[2];
	pDefDoc->origDefaults.bAllowReflect3 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowReflection[3];
	pDefDoc->currentDefaults.bAllowReflect0 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowReflection[0];
	pDefDoc->currentDefaults.bAllowReflect1 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowReflection[1];
	pDefDoc->currentDefaults.bAllowReflect2 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowReflection[2];
	pDefDoc->currentDefaults.bAllowReflect3 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowReflection[3];
	pDefDoc->origDefaults.fDefaultReflect0 = pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[0];
	pDefDoc->origDefaults.fDefaultReflect1 = pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[1];
	pDefDoc->origDefaults.fDefaultReflect2 = pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[2];
	pDefDoc->origDefaults.fDefaultReflect3 = pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[3];
	pDefDoc->currentDefaults.fDefaultReflect0 = pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[0];
	pDefDoc->currentDefaults.fDefaultReflect1 = pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[1];
	pDefDoc->currentDefaults.fDefaultReflect2 = pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[2];
	pDefDoc->currentDefaults.fDefaultReflect3 = pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[3];

	pDefDoc->origDefaults.bAllowSpecular0 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowSpecularity[0];
	pDefDoc->origDefaults.bAllowSpecular1 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowSpecularity[1];
	pDefDoc->origDefaults.bAllowSpecular2 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowSpecularity[2];
	pDefDoc->origDefaults.bAllowSpecular3 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowSpecularity[3];
	pDefDoc->currentDefaults.bAllowSpecular0 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowSpecularity[0];
	pDefDoc->currentDefaults.bAllowSpecular1 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowSpecularity[1];
	pDefDoc->currentDefaults.bAllowSpecular2 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowSpecularity[2];
	pDefDoc->currentDefaults.bAllowSpecular3 = pDefDoc->pCurrentMatDef->pColorOptions->bAllowSpecularity[3];
	pDefDoc->origDefaults.fDefaultSpecular0 = pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[0];
	pDefDoc->origDefaults.fDefaultSpecular1 = pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[1];
	pDefDoc->origDefaults.fDefaultSpecular2 = pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[2];
	pDefDoc->origDefaults.fDefaultSpecular3 = pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[3];
	pDefDoc->currentDefaults.fDefaultSpecular0 = pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[0];
	pDefDoc->currentDefaults.fDefaultSpecular1 = pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[1];
	pDefDoc->currentDefaults.fDefaultSpecular2 = pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[2];
	pDefDoc->currentDefaults.fDefaultSpecular3 = pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[3];

	pDefDoc->origDefaults.bCustomMuscle0 = pDefDoc->pCurrentMatDef->pColorOptions->bSuppressMuscle[0];
	pDefDoc->origDefaults.bCustomMuscle1 = pDefDoc->pCurrentMatDef->pColorOptions->bSuppressMuscle[1];
	pDefDoc->origDefaults.bCustomMuscle2 = pDefDoc->pCurrentMatDef->pColorOptions->bSuppressMuscle[2];
	pDefDoc->origDefaults.bCustomMuscle3 = pDefDoc->pCurrentMatDef->pColorOptions->bSuppressMuscle[3];
	pDefDoc->currentDefaults.bCustomMuscle0 = pDefDoc->pCurrentMatDef->pColorOptions->bSuppressMuscle[0];
	pDefDoc->currentDefaults.bCustomMuscle1 = pDefDoc->pCurrentMatDef->pColorOptions->bSuppressMuscle[1];
	pDefDoc->currentDefaults.bCustomMuscle2 = pDefDoc->pCurrentMatDef->pColorOptions->bSuppressMuscle[2];
	pDefDoc->currentDefaults.bCustomMuscle3 = pDefDoc->pCurrentMatDef->pColorOptions->bSuppressMuscle[3];

	costumeDefEdit_DefTexNamesRefresh(pDefDoc);
	costumeDefEdit_BuildMatConstantGroups(pDefDoc);
}


void costumeDefEdit_DefSetTex(CostumeEditDefDoc *pDefDoc, PCTextureDef *pTex)
{
	if (pDefDoc->pCurrentTexDef) {
		StructDestroy(parse_PCTextureDef, pDefDoc->pCurrentTexDef);
	}
	if (pDefDoc->pOrigTexDef) {
		StructDestroy(parse_PCTextureDef, pDefDoc->pOrigTexDef);
		pDefDoc->pOrigTexDef = NULL;
	}
	if (pTex) {
		pDefDoc->pCurrentTexDef = StructClone(parse_PCTextureDef, pTex);
		assert(pDefDoc->pCurrentTexDef);
		langMakeEditorCopy(parse_PCTextureDef, pDefDoc->pCurrentTexDef, false);
		if (pTex->pcName) { // No name means it's not editable
			pDefDoc->pOrigTexDef = StructClone(parse_PCTextureDef, pDefDoc->pCurrentTexDef);
		}
	} else {
		char *estrScope = NULL;
		pDefDoc->pCurrentTexDef = StructCreate(parse_PCTextureDef);
		pDefDoc->pCurrentTexDef->pcName = costumeDefEdit_CreateUniqueName("CostumeTexture", "NewTexture");
		costumeDefEdit_DefMakeScope(pDefDoc, "Textures", &estrScope);
		pDefDoc->pCurrentTexDef->pcScope = allocAddString(estrScope);
		resFixFilename(g_hCostumeTextureDict, pDefDoc->pCurrentTexDef->pcName, pDefDoc->pCurrentTexDef);
		pDefDoc->pCurrentTexDef->eTypeFlags = kPCTextureType_Pattern;
		if (pDefDoc->pCurrentMatDef) {
			pDefDoc->pCurrentTexDef->eRestriction = pDefDoc->pCurrentMatDef->eRestriction;
		}
		estrDestroy(&estrScope);
	}

	if (!pDefDoc->pCurrentTexDef->pMovableOptions)
		pDefDoc->pCurrentTexDef->pMovableOptions = StructCreate(parse_PCTextureMovableOptions);
	if (!pDefDoc->pCurrentTexDef->pValueOptions)
		pDefDoc->pCurrentTexDef->pValueOptions = StructCreate(parse_PCTextureValueOptions);

	if (!pDefDoc->pCurrentTexDef->displayNameMsg.pEditorCopy) {
		char buf[1024];
		sprintf(buf, "Costume.Texture.%s", pDefDoc->pCurrentTexDef->pcName);
		pDefDoc->pCurrentTexDef->displayNameMsg.pEditorCopy = langCreateMessageWithTerseKey(MKP_COSTUME_TEX, buf, "Costume texture name", "Costume/Texture", pDefDoc->pCurrentTexDef->pcName);
	}

	pDefDoc->pcLastTexChangeName = NULL;
}


static void costumeDefEdit_DefTexNamesRefresh(CostumeEditDefDoc *pDefDoc)
{
	int i;
	Material *pMaterial;
	StashTable pStash;
	StashElement pElement;
	StashTableIterator pIter;

	// Remove old values
	for(i=eaSize(&pDefDoc->eaOldTexNames)-1; i>=0; --i) {
		free(pDefDoc->eaOldTexNames[i]);
	}
	eaClear(&pDefDoc->eaOldTexNames);

	// Get the material
	if (pDefDoc->pOrigMatDef) {
		pMaterial = materialFindNoDefault(pDefDoc->pOrigMatDef->pcMaterial, 0);
		if (pMaterial) {
			// Get the texture names off the material
			pStash = stashTableCreateWithStringKeys(10, StashDefault);
			materialGetTextureNames(pMaterial, pStash, NULL);
			stashGetIterator(pStash, &pIter);
			while(stashGetNextElement(&pIter, &pElement)) {
				eaPush(&pDefDoc->eaOldTexNames, strdup(stashElementGetStringKey(pElement)));
			}
			stashTableDestroy(pStash);
		}
	}
}


// Determine the names of legal child geometries for the current geometry, based on the properties
// of the current geometry's bone def.
static void costumeDefEdit_DefGeoChildNamesRefresh(CostumeEditDefDoc* pDefDoc)
{
	PCSkeletonDef* pSkelDef = NULL;
	PCGeometryDef* pGeoDef = pDefDoc->pCurrentGeoDef;
	PCBoneDef* pBoneDef = (pGeoDef ? GET_REF(pGeoDef->hBone) : NULL);
	int i, j;
	
	if (!pBoneDef || !pGeoDef) {
		return;
	}

	// Get the def structure for the current skeleton
	if (pDefDoc->pcSkeleton) {
		pSkelDef = RefSystem_ReferentFromString(g_hCostumeSkeletonDict, pDefDoc->pcSkeleton);
	}

	for(i=0; i<eaSize(&pBoneDef->eaChildBones); ++i) {
		CostumeGeoChildBoneGroup *pBoneGroup;
		PCGeometryDef** eaChildGeos = NULL;
		PCBoneDef* pChildBoneDef = NULL;

		// Get the bone group
		if (i >= eaSize(&pDefDoc->eaGeoChildBoneGroups)) {
			pBoneGroup = calloc(1, sizeof(CostumeGeoChildBoneGroup));
			pBoneGroup->pDefDoc = pDefDoc;
			pBoneGroup->index = i;
			eaPush(&pDefDoc->eaGeoChildBoneGroups, pBoneGroup);
		} else {
			pBoneGroup = pDefDoc->eaGeoChildBoneGroups[i];
		}

		// Remove old values
		eaDestroyEx(&pBoneGroup->eaGeoChildNames, NULL);

		pChildBoneDef = GET_REF(pBoneDef->eaChildBones[i]->hChildBone);

		// Ensure that sufficient data is available to proceed
		if (!pSkelDef || !pChildBoneDef) {
			continue;
		}

		// Find all the valid child geometries for this geometry, based on the bone defs
		costumeTailor_GetValidGeos(NULL, pSkelDef, pChildBoneDef, NULL, NULL/*species*/, NULL, &eaChildGeos, false, false, gUseDispNames, true);

		for (j = 0; j < eaSize(&eaChildGeos); j++) {
			eaPush(&pBoneGroup->eaGeoChildNames, strdup(eaChildGeos[j]->pcName));
		}
		
		eaDestroy(&eaChildGeos);
	}
}


//
// This procedure updates the list of controls in the extra textures list UI
//
void costumeDefEdit_UpdateExtraTextures(CostumeEditDefDoc *pDefDoc)
{
	UIExpander *pExpander = pDefDoc->pExtraTexturesExpander;
	CostumeExtraTextureGroup *pTexGroup;
	int i,numTex;
	int y=0;

	// Free memory for excess groups
	for(i=eaSize(&pDefDoc->eaTexGroups)-1; i>=eaSize(&pDefDoc->pCurrentTexDef->eaExtraSwaps); --i) {
		MEFieldSafeDestroy(&pDefDoc->eaTexGroups[i]->pOldTexField);
		MEFieldSafeDestroy(&pDefDoc->eaTexGroups[i]->pNewTexField);
		MEFieldSafeDestroy(&pDefDoc->eaTexGroups[i]->pTypeField);
		MEFieldSafeDestroy(&pDefDoc->eaTexGroups[i]->pTexWordsKeyField);
		MEFieldSafeDestroy(&pDefDoc->eaTexGroups[i]->pTexWordsCapsField);
		ui_WidgetQueueFreeAndNull(&pDefDoc->eaTexGroups[i]->pRemoveButton);
		ui_WidgetQueueFreeAndNull(&pDefDoc->eaTexGroups[i]->pSprite);
		ui_WidgetQueueFreeAndNull(&pDefDoc->eaTexGroups[i]->pOldLabel);
		ui_WidgetQueueFreeAndNull(&pDefDoc->eaTexGroups[i]->pNewLabel);
		ui_WidgetQueueFreeAndNull(&pDefDoc->eaTexGroups[i]->pTypeLabel);
		ui_WidgetQueueFreeAndNull(&pDefDoc->eaTexGroups[i]->pTexWordsKeyLabel);
		ui_WidgetQueueFreeAndNull(&pDefDoc->eaTexGroups[i]->pTexWordsCapsLabel);
		ui_WidgetQueueFreeAndNull(&pDefDoc->eaTexGroups[i]->pSeparator);
		free(pDefDoc->eaTexGroups[i]);
		eaRemove(&pDefDoc->eaTexGroups,i);
	}

	// Add or change entry controls
	numTex = eaSize(&pDefDoc->pCurrentTexDef->eaExtraSwaps);
	for(i=0; i<numTex; ++i) {
		PCExtraTexture *pOldTex = NULL;

		if (eaSize(&pDefDoc->eaTexGroups) <= i) {
			// Allocate new group
			pTexGroup = calloc(1,sizeof(CostumeExtraTextureGroup));
			pTexGroup->pDefDoc = pDefDoc;
			pTexGroup->index = i;
			eaPush(&pDefDoc->eaTexGroups, pTexGroup);
		} else {
			pTexGroup = pDefDoc->eaTexGroups[i];
		}

		if ((pDefDoc->pOrigTexDef) && (i < eaSize(&pDefDoc->pOrigTexDef->eaExtraSwaps))) {
			pOldTex = pDefDoc->pOrigTexDef->eaExtraSwaps[i];
		}

		if (!pTexGroup->pOldLabel) {
			pTexGroup->pOldLabel = ui_LabelCreate("Old Texture", 15, y);
			ui_ExpanderAddChild(pExpander, pTexGroup->pOldLabel);
		}

		if (!pTexGroup->pOldTexField) {
			pTexGroup->pOldTexField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pOldTex, pDefDoc->pCurrentTexDef->eaExtraSwaps[i], parse_PCExtraTexture, "OrigTexture", NULL, &pDefDoc->eaOldTexNames, NULL);
			MEFieldAddToParent(pTexGroup->pOldTexField, UI_WIDGET(pExpander), 115, y);
			MEFieldSetChangeCallback(pTexGroup->pOldTexField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
		} else {
			pTexGroup->pOldTexField->pOld = pOldTex;
			pTexGroup->pOldTexField->pNew = pDefDoc->pCurrentTexDef->eaExtraSwaps[i];
		}
		ui_WidgetSetPosition(pTexGroup->pOldTexField->pUIWidget, 115, y);
		ui_WidgetSetWidthEx(pTexGroup->pOldTexField->pUIWidget, 1, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pTexGroup->pOldTexField->pUIWidget, 0, 21, 0, 0);
		MEFieldRefreshFromData(pTexGroup->pOldTexField);

		y += 28;

		if (!pTexGroup->pNewLabel) {
			pTexGroup->pNewLabel = ui_LabelCreate("New Texture", 15, y);
			ui_ExpanderAddChild(pExpander, pTexGroup->pNewLabel);
		}

		if (!pTexGroup->pNewTexField) {
			pTexGroup->pNewTexField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOldTex, pDefDoc->pCurrentTexDef->eaExtraSwaps[i], parse_PCExtraTexture, "NewTexture", NULL, &g_eaTexFileNames, NULL);
			MEFieldAddToParent(pTexGroup->pNewTexField, UI_WIDGET(pExpander), 115, y);
			MEFieldSetChangeCallback(pTexGroup->pNewTexField, costumeDefEdit_UIDefExtraTextureChanged, pTexGroup);
		} else {
			pTexGroup->pNewTexField->pOld = pOldTex;
			pTexGroup->pNewTexField->pNew = pDefDoc->pCurrentTexDef->eaExtraSwaps[i];
		}
		ui_WidgetSetPosition(pTexGroup->pNewTexField->pUIWidget, 115, y);
		ui_WidgetSetWidthEx(pTexGroup->pNewTexField->pUIWidget, 1, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pTexGroup->pNewTexField->pUIWidget, 0, 21, 0, 0);
		MEFieldRefreshFromData(pTexGroup->pNewTexField);

		y += 28;

		if (!pTexGroup->pTypeLabel) {
			pTexGroup->pTypeLabel = ui_LabelCreate("Type", 15, y);
			ui_ExpanderAddChild(pExpander, pTexGroup->pTypeLabel);
		}

		if (!pTexGroup->pTypeField) {
			pTexGroup->pTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOldTex, pDefDoc->pCurrentTexDef->eaExtraSwaps[i], parse_PCExtraTexture, "TypeFlags", PCTextureTypeEnum);
			MEFieldAddToParent(pTexGroup->pTypeField, UI_WIDGET(pExpander), 115, y);
			MEFieldSetChangeCallback(pTexGroup->pTypeField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
		} else {
			pTexGroup->pTypeField->pOld = pOldTex;
			pTexGroup->pTypeField->pNew = pDefDoc->pCurrentTexDef->eaExtraSwaps[i];
		}
		ui_WidgetSetPosition(pTexGroup->pTypeField->pUIWidget, 115, y);
		ui_WidgetSetWidth(pTexGroup->pTypeField->pUIWidget, 120);
		MEFieldRefreshFromData(pTexGroup->pTypeField);

		// Preview Sprite
		if (!pTexGroup->pSprite) {
			pTexGroup->pSprite = ui_SpriteCreate(245, y, 50, 50, "");
			pTexGroup->pSprite->bDrawBorder = true;
			ui_ExpanderAddChild(pExpander, pTexGroup->pSprite);
		}
		ui_SpriteSetTexture(pTexGroup->pSprite, pDefDoc->pCurrentTexDef->eaExtraSwaps[i]->pcNewTexture ? pDefDoc->pCurrentTexDef->eaExtraSwaps[i]->pcNewTexture : "");

		y += 28;

		if (!pTexGroup->pTexWordsKeyLabel) {
			pTexGroup->pTexWordsKeyLabel = ui_LabelCreate("TexWords Key", 15, y);
			ui_ExpanderAddChild(pExpander, pTexGroup->pTexWordsKeyLabel);
		}

		if (!pTexGroup->pTexWordsKeyField) {
			pTexGroup->pTexWordsKeyField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldTex, pDefDoc->pCurrentTexDef->eaExtraSwaps[i], parse_PCExtraTexture, "TexWordsKey");
			MEFieldAddToParent(pTexGroup->pTexWordsKeyField, UI_WIDGET(pExpander), 115, y);
			MEFieldSetChangeCallback(pTexGroup->pTexWordsKeyField, costumeDefEdit_UIDefExtraTextureChanged, pTexGroup);
		} else {
			pTexGroup->pTexWordsKeyField->pOld = pOldTex;
			pTexGroup->pTexWordsKeyField->pNew = pDefDoc->pCurrentTexDef->eaExtraSwaps[i];
		}
		ui_WidgetSetPosition(pTexGroup->pTexWordsKeyField->pUIWidget, 115, y);
		ui_WidgetSetWidthEx(pTexGroup->pTexWordsKeyField->pUIWidget, 1, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pTexGroup->pTexWordsKeyField->pUIWidget, 0, 21, 0, 0);
		MEFieldRefreshFromData(pTexGroup->pTexWordsKeyField);

		y += 28;

		if (!pTexGroup->pTexWordsCapsLabel) {
			pTexGroup->pTexWordsCapsLabel = ui_LabelCreate("TexWords Caps", 15, y);
			ui_ExpanderAddChild(pExpander, pTexGroup->pTexWordsCapsLabel);
		}

		if (!pTexGroup->pTexWordsCapsField) {
			pTexGroup->pTexWordsCapsField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pOldTex, pDefDoc->pCurrentTexDef->eaExtraSwaps[i], parse_PCExtraTexture, "TexWordsCaps");
			MEFieldAddToParent(pTexGroup->pTexWordsCapsField, UI_WIDGET(pExpander), 115, y);
			MEFieldSetChangeCallback(pTexGroup->pTexWordsCapsField, costumeDefEdit_UIDefExtraTextureChanged, pTexGroup);
		} else {
			pTexGroup->pTexWordsCapsField->pOld = pOldTex;
			pTexGroup->pTexWordsCapsField->pNew = pDefDoc->pCurrentTexDef->eaExtraSwaps[i];
		}
		ui_WidgetSetPosition(pTexGroup->pTexWordsCapsField->pUIWidget, 115, y);
		ui_WidgetSetWidthEx(pTexGroup->pTexWordsCapsField->pUIWidget, 1, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pTexGroup->pTexWordsCapsField->pUIWidget, 0, 21, 0, 0);
		MEFieldRefreshFromData(pTexGroup->pTexWordsCapsField);

		y += 28;
		if (!pTexGroup->pRemoveButton) {
			pTexGroup->pRemoveButton = ui_ButtonCreate("Remove", 15, y, costumeDefEdit_UIExtraTexRemove, pTexGroup);
			ui_WidgetSetWidth(UI_WIDGET(pTexGroup->pRemoveButton), 70);
			ui_ExpanderAddChild(pExpander, pTexGroup->pRemoveButton);
		}
		ui_WidgetSetPosition(UI_WIDGET(pTexGroup->pRemoveButton), 15, y);

		y += 28;

		if (!pTexGroup->pSeparator) {
			pTexGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
			ui_ExpanderAddChild(pExpander, pTexGroup->pSeparator);
		}
		ui_WidgetSetPosition(UI_WIDGET(pTexGroup->pSeparator), 15, y);
		y += 9;
	}

	// If no entries, then a default string
	if (numTex == 0) {
		if (!pDefDoc->pExtraTexturesLabel) {
			pDefDoc->pExtraTexturesLabel = ui_LabelCreate("No extra textures are currently defined",15,y);
			ui_ExpanderAddChild(pExpander, pDefDoc->pExtraTexturesLabel);
		}
		ui_WidgetSetPosition(UI_WIDGET(pDefDoc->pExtraTexturesLabel), 15, y);
		y += 20;
	} else {
		ui_WidgetQueueFreeAndNull(&pDefDoc->pExtraTexturesLabel);
	}

	// Button to add entries
	if (!pDefDoc->pExtraTexturesAddButton) {
		pDefDoc->pExtraTexturesAddButton = ui_ButtonCreate("Add Texture...", 15, y, costumeDefEdit_UIExtraTexAdd, pDefDoc );
		pDefDoc->pExtraTexturesAddButton->widget.width = 120;
		ui_ExpanderAddChild(pExpander, pDefDoc->pExtraTexturesAddButton);
	}
	ui_WidgetSetPosition(UI_WIDGET(pDefDoc->pExtraTexturesAddButton), 15, y);
	y += 28;

	ui_ExpanderSetHeight(pExpander, y);
}


// Build the list of material constants included in the current material
void costumeDefEdit_BuildMatConstantGroups(CostumeEditDefDoc* pDefDoc)
{
	PCMaterialDef* pMatDef = pDefDoc->pCurrentMatDef;
	Material* pMaterial = NULL;
	const char** eaConstants = NULL;
	int i = 0, j = 0;
	bool bFound = false;

	// Find all the named constants in this material
	if (pMatDef) {
		pMaterial = materialFindNoDefault(pMatDef->pcMaterial, 0);
		if (pMaterial) {
			MaterialRenderInfo *render_info;

			if (!pMaterial->graphic_props.render_info) {
				gfxMaterialsInitMaterial(pMaterial, true);
			}
			render_info = pMaterial->graphic_props.render_info;
			assert(render_info);
			for (i = (render_info->rdr_material.const_count * 4) - 1; i >= 0; --i) {
				if (render_info->constant_names[i]) {
					eaPushUnique(&eaConstants, render_info->constant_names[i]);
				}
			}
		}
	}

	// Remove constants that have built-in UI
	for (i = eaSize(&eaConstants) - 1; i >= 0; --i) {
		if ((stricmp(eaConstants[i], "Color0") == 0) ||
			(stricmp(eaConstants[i], "Color1") == 0) ||
			(stricmp(eaConstants[i], "Color2") == 0) ||
			(stricmp(eaConstants[i], "Color3") == 0) ||
			(stricmp(eaConstants[i], "MuscleWeight") == 0) ||
			(stricmp(eaConstants[i], "ReflectionWeight") == 0) ||
			(stricmp(eaConstants[i], "SpecularWeight") == 0) ||
			// Artists request that these be hidden
			(stricmp(eaConstants[i], "FresnelTerm_Advanced1") == 0) ||
			(strnicmp(eaConstants[i], "LERP", 4) == 0) ||
			(stricmp(eaConstants[i], "MyOutput") == 0)
			)
		{
			eaRemove(&eaConstants, i);
		}
	}

	// Remove color groups that no longer need to be here
	for (i = eaSize(&pDefDoc->eaMatConstantGroups) - 1; i >= 0; --i) {
		CostumeMatConstantGroup* pGroup = pDefDoc->eaMatConstantGroups[i];
		bFound = false;
		for (j = eaSize(&eaConstants) - 1; j >= 0; --j) {
			if (stricmp(pDefDoc->eaMatConstantGroups[i]->pcName, eaConstants[j]) == 0) {
				bFound = true;
				break;
			}
		}
		if (!bFound) {
			ui_WidgetQueueFreeAndNull(&pDefDoc->eaMatConstantGroups[i]->pCheck);
			ui_WidgetQueueFreeAndNull(&pDefDoc->eaMatConstantGroups[i]->pLabel);
			ui_WidgetQueueFreeAndNull(&pDefDoc->eaMatConstantGroups[i]->pColorButton);
			ui_WidgetQueueFreeAndNull(&pDefDoc->eaMatConstantGroups[i]->pSlider1);
			ui_WidgetQueueFreeAndNull(&pDefDoc->eaMatConstantGroups[i]->pSlider2);
			ui_WidgetQueueFreeAndNull(&pDefDoc->eaMatConstantGroups[i]->pSlider3);
			ui_WidgetQueueFreeAndNull(&pDefDoc->eaMatConstantGroups[i]->pSlider4);
			ui_WidgetQueueFreeAndNull(&pDefDoc->eaMatConstantGroups[i]->pSubLabel1);
			ui_WidgetQueueFreeAndNull(&pDefDoc->eaMatConstantGroups[i]->pSubLabel2);
			ui_WidgetQueueFreeAndNull(&pDefDoc->eaMatConstantGroups[i]->pSubLabel3);
			ui_WidgetQueueFreeAndNull(&pDefDoc->eaMatConstantGroups[i]->pSubLabel4);
			free(pDefDoc->eaMatConstantGroups[i]);
			eaRemove(&pDefDoc->eaMatConstantGroups, i);
		}
	}

	// Add in missing constant groups
	for (i = eaSize(&eaConstants) - 1; i >= 0; --i) {
		bFound = false;
		for (j = eaSize(&pDefDoc->eaMatConstantGroups) - 1; j >= 0; --j) {
			if (stricmp(pDefDoc->eaMatConstantGroups[j]->pcName, eaConstants[i]) == 0) {
				bFound = true;
				break;
			}
		}
		if (!bFound) {
			CostumeMatConstantGroup *pGroup = calloc(1, sizeof(CostumeMatConstantGroup));
			pGroup->pcName = eaConstants[i];
			pGroup->pDoc = pDefDoc;
			pGroup->bIsColor = (strstri(pGroup->pcName, "Color") != NULL);
			eaPush(&pDefDoc->eaMatConstantGroups, pGroup);
		}
	}

	// Set current status on color groups
	for (i = eaSize(&pDefDoc->eaMatConstantGroups) - 1; i >= 0; --i) {
		CostumeMatConstantGroup *pGroup = pDefDoc->eaMatConstantGroups[i];

		if (pMatDef && pMatDef->pOptions) {
			Vec4 value;

			// See if the value has a current value
			bFound = false;
			if (pGroup->bIsColor) {
				for (j = eaSize(&pMatDef->pOptions->eaExtraColors) - 1; j >= 0; --j) {
					if (stricmp(pMatDef->pOptions->eaExtraColors[j]->pcName, pGroup->pcName) == 0) {
						bFound = true;
						break;
					}
				}
				if (bFound) {
					copyVec4(pMatDef->pOptions->eaExtraColors[j]->color, pGroup->currentValue);
					pGroup->bIsSet = true;
				}
				else if (pMaterial && materialGetNamedConstantValue(pMaterial, pGroup->pcName, value)) {
					scaleVec4(value, 255.0f, pGroup->currentValue);
					pGroup->bIsSet = false;
				}
				else {
					setVec4same(pGroup->currentValue, 255.0);
					pGroup->bIsSet = false;
				}
			}
			else {
				for (j = eaSize(&pMatDef->pOptions->eaExtraConstants) - 1; j >= 0; --j) {
					if (stricmp(pMatDef->pOptions->eaExtraConstants[j]->pcName, pGroup->pcName) == 0) {
						bFound = true;
						break;
					}
				}
				if (bFound) {
					copyVec4(pMatDef->pOptions->eaExtraConstants[j]->values, pGroup->currentValue);
					pGroup->bIsSet = true;
				}
				else if (pMaterial && materialGetNamedConstantValue(pMaterial, pGroup->pcName, value)) {
					scaleVec4(value, costumeTailor_GetMatConstantScale(pGroup->pcName), pGroup->currentValue);
					pGroup->bIsSet = false;
				}
				else {
					zeroVec4(pGroup->currentValue);
					pGroup->bIsSet = false;
				}
			}
		}
	}

	// sort the finalized list of material constants
	eaQSort(pDefDoc->eaMatConstantGroups, costumeDefEdit_CompareMatConstantGroups);

	// cleanup
	eaDestroy(&eaConstants);
}


// Update the list of controls in the material constants list UI
void costumeDefEdit_UpdateMaterialConstants(CostumeEditDefDoc* pDefDoc)
{
	CostumeMatConstantGroup* pGroup;
	int i, j;
	int y = 0, x = 0;
	UIExpander* pExpander = pDefDoc->pMatConstantExpander;
	PCMaterialDef* pMatDef = pDefDoc->pCurrentMatDef;

	// ensure that the proper material constant groups exist
	costumeDefEdit_BuildMatConstantGroups(pDefDoc);

	// Build the UI

	// Add in checkbox labels first to determine dynamic width
	for (i = 0; i < eaSize(&pDefDoc->eaMatConstantGroups); ++i) {
		pGroup = pDefDoc->eaMatConstantGroups[i];

		// Put in checkbox
		if (!pGroup->pCheck) {
			pGroup->pCheck = ui_CheckButtonCreate(15, y, pGroup->pcName, false);
			ui_CheckButtonSetToggledCallback(pGroup->pCheck, costumeDefEdit_UIMatConstantGroupToggled, pGroup);
		}
		if (!pGroup->pCheck->widget.group) {
			ui_ExpanderAddChild(pExpander, pGroup->pCheck);
		}
		ui_CheckButtonSetState(pGroup->pCheck, pGroup->bIsSet);
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pCheck), 15, y);
		ui_SetActive(UI_WIDGET(pGroup->pCheck), true);

		if (pGroup->pCheck->widget.width > x) {
			x = pGroup->pCheck->widget.width;
		}
		y += 28;

		// add appropriate sub-labels if multiple values are used
		if (!pGroup->bIsColor && costumeTailor_GetMatConstantNumValues(pGroup->pcName) > 1) {
			// list the sliders which might be used
			UILabel** labels[4] = { &pGroup->pSubLabel1,
									&pGroup->pSubLabel2,
									&pGroup->pSubLabel3,
									&pGroup->pSubLabel4 };

			for (j = 0; j < costumeTailor_GetMatConstantNumValues(pGroup->pcName); j++) {
				// create a new sub-label for the slider and add it to the panel
				if (!(*labels[j])) {
					(*labels[j]) = ui_LabelCreate(costumeTailor_GetMatConstantValueName(pGroup->pcName, j), 45, y);
				}
				if (!(*labels[j])->widget.group) {
					ui_ExpanderAddChild(pExpander, *labels[j]);
				}
				ui_WidgetSetPosition(UI_WIDGET(*labels[j]), 45, y);
				ui_SetActive(UI_WIDGET(*labels[j]), pGroup->bIsSet);
				y += 28;
			}
		}
		else {
			// remove unneeded labels from the group
			if (pGroup->pSubLabel1 && pGroup->pSubLabel1->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pSubLabel1));
			}
			if (pGroup->pSubLabel2 && pGroup->pSubLabel2->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pSubLabel2));
			}
			if (pGroup->pSubLabel3 && pGroup->pSubLabel3->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pSubLabel3));
			}
			if (pGroup->pSubLabel4 && pGroup->pSubLabel4->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pSubLabel4));
			}
		}
	}

	x += 20;
	y = 0;

	for (i = 0; i < eaSize(&pDefDoc->eaMatConstantGroups); ++i) {
		pGroup = pDefDoc->eaMatConstantGroups[i];

		if (pGroup->bIsColor) {
			Vec4 value;
			// If color, show color button and hide the label
			if (!pGroup->pColorButton) {
				pGroup->pColorButton = ui_ColorButtonCreate(x, y, pGroup->currentValue);
				pGroup->pColorButton->liveUpdate = true;
				//JE: Needed for tattoos: pGroup->pColorButton->min = -1;
				//JE: Needed for tattoos: pGroup->pColorButton->max = 10;
				ui_ColorButtonSetChangedCallback(pGroup->pColorButton, costumeDefEdit_UIMatConstantColorGroupChanged, pGroup);
				ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pColorButton), 1, UIUnitPercentage);
				ui_WidgetSetPaddingEx(UI_WIDGET(pGroup->pColorButton), 0, 21, 0, 0);
			}
			if (!pGroup->pColorButton->widget.group) {
				ui_ExpanderAddChild(pExpander, pGroup->pColorButton);
			}
			ui_WidgetSetPosition(UI_WIDGET(pGroup->pColorButton), x, y);
			scaleVec4(pGroup->currentValue, U8TOF32_COLOR, value);
			ui_ColorButtonSetColor(pGroup->pColorButton, value);
			ui_SetActive(UI_WIDGET(pGroup->pColorButton), pGroup->bIsSet);

			// remove conflicting widgets
			if (pGroup->pLabel && pGroup->pLabel->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pLabel));
			}

			y += 28;
		}
		else {
			// If not a color, show a slider and hide the label for each value; between one and four
			char buf[260];
			int numValues = CLAMP(costumeTailor_GetMatConstantNumValues(pGroup->pcName), 1, 4);
			int ii = 0;

			// list the sliders which might be used
			UISliderTextEntry** sliders[4] = { &pGroup->pSlider1,
											   &pGroup->pSlider2,
											   &pGroup->pSlider3,
											   &pGroup->pSlider4 };

			// if more than one value is used, leave a blank line above
			if (numValues > 1) {
				y += 28;
			}

			// iterate through as many sliders as there are values being used
			for (ii = 0; ii < numValues; ii++) {
				F64 fStep = 0.0f;

				if (!(*sliders[ii])) {
					(*sliders[ii]) = ui_SliderTextEntryCreateWithNoSnap("0", 0, 100, x, y, 120);
					ui_WidgetSetWidthEx(UI_WIDGET(*sliders[ii]), 1, UIUnitPercentage);
					ui_WidgetSetPaddingEx(UI_WIDGET(*sliders[ii]), 0, 21, 0, 0);
					ui_SliderTextEntrySetRange(*sliders[ii], 0, 100, 0.5);
					ui_SliderTextEntrySetPolicy(*sliders[ii], UISliderContinuous);
					ui_SliderTextEntrySetChangedCallback(*sliders[ii], costumeDefEdit_UIMatConstantValueGroupChanged, pGroup);
				}
				if (!(*sliders[ii])->widget.group) {
					ui_ExpanderAddChild(pExpander, *sliders[ii]);
				}
				ui_WidgetSetPosition(UI_WIDGET(*sliders[ii]), x, y);
				ui_SetActive(UI_WIDGET(*sliders[ii]), pGroup->bIsSet);
				sprintf(buf, "%g", pGroup->currentValue[ii]);

				// temporarily disable step when updating
				if ((*sliders[ii])->pSlider) {
					fStep = (*sliders[ii])->pSlider->step;
					(*sliders[ii])->pSlider->step = 0.0f;
				}
				ui_SliderTextEntrySetTextAndCallback(*sliders[ii], buf);
				if ((*sliders[ii])->pSlider) {
					(*sliders[ii])->pSlider->step = fStep;
				}

				y += 28;
			}

			// remove conflicting widgets
			if (pGroup->pLabel && pGroup->pLabel->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pLabel));
			}
		}
	}

	// if no entries, then a default string
	if (eaSize(&pDefDoc->eaMatConstantGroups) == 0) {
		if (!pDefDoc->pMatConstantLabel) {
			pDefDoc->pMatConstantLabel = ui_LabelCreate("No material constants are currently defined",15,y);
			ui_ExpanderAddChild(pExpander, pDefDoc->pMatConstantLabel);
		}
		ui_WidgetSetPosition(UI_WIDGET(pDefDoc->pMatConstantLabel), 15, y);
		y += 20;
	} else {
		ui_WidgetQueueFreeAndNull(&pDefDoc->pMatConstantLabel);
	}

	ui_ExpanderSetHeight(pExpander, y);
}


// Update the list of controls in the material FX swap UI
void costumeDefEdit_UpdateMaterialFxSwaps(CostumeEditDefDoc* pDefDoc)
{
	UIExpander* pExpander = pDefDoc->pMatFxSwapExpander;
	CostumeDefFxSwapGroup* pFxSwapGroup = NULL;
	int i = 0, numFxSwap = 0;
	int y = 0;

	// Free memory for excess FX groups
	for (i = eaSize(&pDefDoc->eaMatFxSwapGroups)-1
		; i >= (pDefDoc->pCurrentMatDef->pOptions ? eaSize(&pDefDoc->pCurrentMatDef->pOptions->eaFXSwap) : 0)
		; --i) {
		MEFieldSafeDestroy(&pDefDoc->eaMatFxSwapGroups[i]->pFxOldField);
		MEFieldSafeDestroy(&pDefDoc->eaMatFxSwapGroups[i]->pFxNewField);
		ui_WidgetQueueFreeAndNull(&pDefDoc->eaMatFxSwapGroups[i]->pRemoveButton);
		ui_WidgetQueueFreeAndNull(&pDefDoc->eaMatFxSwapGroups[i]->pSeparator);
		free(pDefDoc->eaMatFxSwapGroups[i]);
		eaRemove(&pDefDoc->eaMatFxSwapGroups, i);
	}

	// Add or change FX Swap entry controls
	numFxSwap = pDefDoc->pCurrentMatDef->pOptions ? eaSize(&pDefDoc->pCurrentMatDef->pOptions->eaFXSwap) : 0;
	for (i = 0; i < numFxSwap; i++) {
		NOCONST(PCFXSwap) *pOldFxSwap = NULL;

		if (eaSize(&pDefDoc->eaMatFxSwapGroups) <= i) {
			// Allocate new group
			pFxSwapGroup = calloc(1, sizeof(CostumeDefFxSwapGroup));
			pFxSwapGroup->pDefDoc = pDefDoc;
			eaPush(&pDefDoc->eaMatFxSwapGroups, pFxSwapGroup);
		}
		else {
			pFxSwapGroup = pDefDoc->eaMatFxSwapGroups[i];
		}

		if ((pDefDoc->pOrigMatDef && pDefDoc->pOrigMatDef->pOptions) && (i < eaSize(&pDefDoc->pOrigMatDef->pOptions->eaFXSwap))) {
			pOldFxSwap = CONTAINER_NOCONST(PCFXSwap, pDefDoc->pOrigMatDef->pOptions->eaFXSwap[i]);
		}

		if (!pFxSwapGroup->pFxOldField) {
			pFxSwapGroup->pFxOldField = MEFieldCreateSimpleDictionary(kMEFieldType_TextEntry, pOldFxSwap, pDefDoc->pCurrentMatDef->pOptions->eaFXSwap[i], parse_PCFXSwap, "pcOldName", "DynFxInfo", parse_DynFxInfo, "InternalName");
			MEFieldAddToParent(pFxSwapGroup->pFxOldField, UI_WIDGET(pExpander), 15, 0);
			MEFieldSetChangeCallback(pFxSwapGroup->pFxOldField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
		}
		else {
			pFxSwapGroup->pFxOldField->pOld = pOldFxSwap;
			pFxSwapGroup->pFxOldField->pNew = pDefDoc->pCurrentMatDef->pOptions->eaFXSwap[i];
		}
		ui_WidgetSetPosition(pFxSwapGroup->pFxOldField->pUIWidget, 15, y);
		ui_WidgetSetWidthEx(pFxSwapGroup->pFxOldField->pUIWidget, 1, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pFxSwapGroup->pFxOldField->pUIWidget, 0, 80, 0, 0);
		MEFieldRefreshFromData(pFxSwapGroup->pFxOldField);

		if (!pFxSwapGroup->pRemoveButton) {
			pFxSwapGroup->pRemoveButton = ui_ButtonCreate("Remove", 0, y+14, costumeDefEdit_UIMatFXSwapRemove, pFxSwapGroup);
			ui_WidgetSetWidth(UI_WIDGET(pFxSwapGroup->pRemoveButton), 70);
			ui_ExpanderAddChild(pExpander, pFxSwapGroup->pRemoveButton);
		}
		ui_WidgetSetPositionEx(UI_WIDGET(pFxSwapGroup->pRemoveButton), 0, y+14, 0, 0, UITopRight);

		y += 28;

		if (!pFxSwapGroup->pFxNewField) {
			pFxSwapGroup->pFxNewField = MEFieldCreateSimpleDictionary(kMEFieldType_TextEntry, pOldFxSwap, pDefDoc->pCurrentMatDef->pOptions->eaFXSwap[i], parse_PCFXSwap, "pcNewName", "DynFxInfo", parse_DynFxInfo, "InternalName");
			MEFieldAddToParent(pFxSwapGroup->pFxNewField, UI_WIDGET(pExpander), 15, 0);
			MEFieldSetChangeCallback(pFxSwapGroup->pFxNewField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
		}
		else {
			pFxSwapGroup->pFxNewField->pOld = pOldFxSwap;
			pFxSwapGroup->pFxNewField->pNew = pDefDoc->pCurrentMatDef->pOptions->eaFXSwap[i];
		}
		ui_WidgetSetPosition(pFxSwapGroup->pFxNewField->pUIWidget, 15, y);
		ui_WidgetSetWidthEx(pFxSwapGroup->pFxNewField->pUIWidget, 1, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pFxSwapGroup->pFxNewField->pUIWidget, 0, 80, 0, 0);
		MEFieldRefreshFromData(pFxSwapGroup->pFxNewField);

		y += 28;

		if (!pFxSwapGroup->pSeparator) {
			pFxSwapGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
			ui_ExpanderAddChild(pExpander, pFxSwapGroup->pSeparator);
		}
		ui_WidgetSetPosition(UI_WIDGET(pFxSwapGroup->pSeparator), 15, y);
		y += 9;
	}

	// If no FX, then a default string
	if (numFxSwap == 0) {
		if (!pDefDoc->pMatFxSwapEmptyLabel) {
			pDefDoc->pMatFxSwapEmptyLabel = ui_LabelCreate("No FX Swaps are currently defined", 15, y);
			ui_ExpanderAddChild(pExpander, pDefDoc->pMatFxSwapEmptyLabel);
		}
		ui_WidgetSetPosition(UI_WIDGET(pDefDoc->pMatFxSwapEmptyLabel), 15, y);
		y += 20;
	} else {
		ui_WidgetQueueFreeAndNull(&pDefDoc->pMatFxSwapEmptyLabel);
	}

	// Button to add FX
	if (!pDefDoc->pMatFxAddButton) {
		pDefDoc->pMatFxAddButton = ui_ButtonCreate("Add FX Swap...", 15, y, costumeDefEdit_UIMatFXSwapAdd, pDefDoc);
		pDefDoc->pMatFxAddButton->widget.width = 100;
		ui_ExpanderAddChild(pExpander, pDefDoc->pMatFxAddButton);
	}
	ui_WidgetSetPosition(UI_WIDGET(pDefDoc->pMatFxAddButton), 15, y);
	y += 28;

	ui_ExpanderSetHeight(pExpander, y);
}


// Update the list of controls in the geometry advanced options UI
void costumeDefEdit_UpdateGeoOptionsOptions(CostumeEditDefDoc* pDefDoc)
{
	UIExpander* pExpander = pDefDoc->pGeoOptionsExpander;
	MEField* pField = NULL;
	int x = 20, y = pExpander->openedHeight;

	if (!pDefDoc->pGeoOptionsColor0Label) {
		pDefDoc->pGeoOptionsColor0Label = ui_LabelCreate("Color Set 0", x, y);
		ui_ExpanderAddChild(pExpander, pDefDoc->pGeoOptionsColor0Label);
		pField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pOptions), SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pOptions), parse_PCGeometryOptions, "BodyColorSet0", "CostumeColors", parse_UIColorSet, "Name");
		MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
		MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
		ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
		pDefDoc->pDefColorSet0Field = pField;
		y+=28;
	}
	else
	{
		pDefDoc->pDefColorSet0Field->pOld = SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pOptions);
		pDefDoc->pDefColorSet0Field->pNew = SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pOptions);
		ui_WidgetSetPosition(pDefDoc->pDefColorSet0Field->pUIWidget, x+100, y - (28*5));
	}
	ui_WidgetSetWidthEx(pDefDoc->pDefColorSet0Field->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pDefDoc->pDefColorSet0Field->pUIWidget, 0, 20, 0, 0);
	MEFieldRefreshFromData(pDefDoc->pDefColorSet0Field);

	if (!pDefDoc->pGeoOptionsColor1Label) {
		pDefDoc->pGeoOptionsColor1Label = ui_LabelCreate("Color Set 1", x, y);
		ui_ExpanderAddChild(pExpander, pDefDoc->pGeoOptionsColor1Label);
		pField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pOptions), SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pOptions), parse_PCGeometryOptions, "BodyColorSet1", "CostumeColors", parse_UIColorSet, "Name");
		MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
		MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
		ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
		pDefDoc->pDefColorSet1Field = pField;
		y+=28;
	}
	else
	{
		pDefDoc->pDefColorSet1Field->pOld = SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pOptions);
		pDefDoc->pDefColorSet1Field->pNew = SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pOptions);
		ui_WidgetSetPosition(pDefDoc->pDefColorSet1Field->pUIWidget, x+100, y - (28*4));
	}
	ui_WidgetSetWidthEx(pDefDoc->pDefColorSet1Field->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pDefDoc->pDefColorSet1Field->pUIWidget, 0, 20, 0, 0);
	MEFieldRefreshFromData(pDefDoc->pDefColorSet1Field);

	if (!pDefDoc->pGeoOptionsColor2Label) {
		pDefDoc->pGeoOptionsColor2Label = ui_LabelCreate("Color Set 2", x, y);
		ui_ExpanderAddChild(pExpander, pDefDoc->pGeoOptionsColor2Label);
		pField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pOptions), SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pOptions), parse_PCGeometryOptions, "BodyColorSet2", "CostumeColors", parse_UIColorSet, "Name");
		MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
		MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
		ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
		pDefDoc->pDefColorSet2Field = pField;
		y+=28;
	}
	else
	{
		pDefDoc->pDefColorSet2Field->pOld = SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pOptions);
		pDefDoc->pDefColorSet2Field->pNew = SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pOptions);
		ui_WidgetSetPosition(pDefDoc->pDefColorSet2Field->pUIWidget, x+100, y - (28*3));
	}
	ui_WidgetSetWidthEx(pDefDoc->pDefColorSet2Field->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pDefDoc->pDefColorSet2Field->pUIWidget, 0, 20, 0, 0);
	MEFieldRefreshFromData(pDefDoc->pDefColorSet2Field);

	if (!pDefDoc->pGeoOptionsColor3Label) {
		pDefDoc->pGeoOptionsColor3Label = ui_LabelCreate("Color Set 3", x, y);
		ui_ExpanderAddChild(pExpander, pDefDoc->pGeoOptionsColor3Label);
		pField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pOptions), SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pOptions), parse_PCGeometryOptions, "BodyColorSet3", "CostumeColors", parse_UIColorSet, "Name");
		MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
		MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
		ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
		pDefDoc->pDefColorSet3Field = pField;
		y+=28;
	}
	else
	{
		pDefDoc->pDefColorSet3Field->pOld = SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pOptions);
		pDefDoc->pDefColorSet3Field->pNew = SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pOptions);
		ui_WidgetSetPosition(pDefDoc->pDefColorSet3Field->pUIWidget, x+100, y - (28*2));
	}
	ui_WidgetSetWidthEx(pDefDoc->pDefColorSet3Field->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pDefDoc->pDefColorSet3Field->pUIWidget, 0, 20, 0, 0);
	MEFieldRefreshFromData(pDefDoc->pDefColorSet3Field);

	if (!pDefDoc->pGeoOptionsColorQuadLabel) {
		pDefDoc->pGeoOptionsColorQuadLabel = ui_LabelCreate("Color Quad", x, y);
		ui_ExpanderAddChild(pExpander, pDefDoc->pGeoOptionsColorQuadLabel);
		pField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pOptions), SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pOptions), parse_PCGeometryOptions, "ColorQuadSet", "CostumeColorQuads", parse_PCColorQuadSet, "Name");
		MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
		MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
		ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
		pDefDoc->pDefColorQuadField = pField;
		y+=28;
	}
	else
	{
		pDefDoc->pDefColorQuadField->pOld = SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pOptions);
		pDefDoc->pDefColorQuadField->pNew = SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pOptions);
		ui_WidgetSetPosition(pDefDoc->pDefColorQuadField->pUIWidget, x+100, y - (28*1));
	}
	ui_WidgetSetWidthEx(pDefDoc->pDefColorQuadField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pDefDoc->pDefColorQuadField->pUIWidget, 0, 20, 0, 0);
	MEFieldRefreshFromData(pDefDoc->pDefColorQuadField);

	ui_ExpanderSetHeight(pExpander, y);
}

// Update the list of controls in the geometry advanced options UI
void costumeDefEdit_UpdateGeoAdvancedOptions(CostumeEditDefDoc* pDefDoc)
{
	PCBoneDef* pBoneDef = (pDefDoc->pCurrentGeoDef ? GET_REF(pDefDoc->pCurrentGeoDef->hBone) : NULL);
	UIExpander* pExpander = pDefDoc->pGeoAdvancedExpander;
	UILabel* pLabel = NULL;
	MEField* pField = NULL;
	int x = 20, y = 0;
	int i = 0, j;
	int iNumChild = 0;

	// Animated costume part label
	if (!pDefDoc->pGeoAdvAnimatedLabel) {
		pDefDoc->pGeoAdvAnimatedLabel = ui_LabelCreate("Animated Costume Parts", x, y);
		ui_ExpanderAddChild(pExpander, pDefDoc->pGeoAdvAnimatedLabel);
	}
	y+=28;

	// Sub Skeleton Label
	if (!pDefDoc->pGeoSubSkeletonLabel) {
		pDefDoc->pGeoSubSkeletonLabel = ui_LabelCreate("Sub Skeleton", x, y);
	}
	if (!pDefDoc->pGeoSubSkeletonLabel->widget.group) {
		ui_ExpanderAddChild(pExpander, pDefDoc->pGeoSubSkeletonLabel);
	}

	if (pDefDoc->pCurrentGeoDef)
	{
		if (!pDefDoc->pCurrentGeoDef->pOptions)
			pDefDoc->pCurrentGeoDef->pOptions = StructCreate(parse_PCGeometryOptions);
		if (!pDefDoc->pCurrentGeoDef->pClothData)
			pDefDoc->pCurrentGeoDef->pClothData = StructCreate(parse_PCGeometryClothData);
	}
	if (!pDefDoc->pDefGeoSubSkeletonField) {
		pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pOptions), SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pOptions), parse_PCGeometryOptions, "SubSkeleton");
		MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
		MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
		pDefDoc->pDefGeoSubSkeletonField = pField;
	}
	else {
		pDefDoc->pDefGeoSubSkeletonField->pOld = SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pOptions);
		pDefDoc->pDefGeoSubSkeletonField->pNew = SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pOptions);
	}
	ui_WidgetSetPosition(pDefDoc->pDefGeoSubSkeletonField->pUIWidget, x+100, y);
	ui_WidgetSetWidthEx(pDefDoc->pDefGeoSubSkeletonField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pDefDoc->pDefGeoSubSkeletonField->pUIWidget, 0, 20, 0, 0);
	MEFieldRefreshFromData(pDefDoc->pDefGeoSubSkeletonField);
	y+=28;

	// Sub Bone
	if (!pDefDoc->pGeoSubBoneLabel) {
		pDefDoc->pGeoSubBoneLabel = ui_LabelCreate("Sub Bone", x, y);
	}
	if (!pDefDoc->pGeoSubBoneLabel->widget.group) {
		ui_ExpanderAddChild(pExpander, pDefDoc->pGeoSubBoneLabel);
	}
	
	if (!pDefDoc->pDefGeoSubBoneField) {
		pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pOptions), SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pOptions), parse_PCGeometryOptions, "SubBone");
		MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
		MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
		pDefDoc->pDefGeoSubBoneField = pField;
	}
	else {
		pDefDoc->pDefGeoSubBoneField->pOld = SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pOptions);
		pDefDoc->pDefGeoSubBoneField->pNew = SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pOptions);
	}
	ui_WidgetSetPosition(pDefDoc->pDefGeoSubBoneField->pUIWidget, x+100, y);
	ui_WidgetSetWidthEx(pDefDoc->pDefGeoSubBoneField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pDefDoc->pDefGeoSubBoneField->pUIWidget, 0, 20, 0, 0);
	MEFieldRefreshFromData(pDefDoc->pDefGeoSubBoneField);
	y+=28;

	// if this geometry's bone is a child, then this geometry must be a child too
	if (pBoneDef)
	{
		if (pBoneDef->bIsChildBone)
		pDefDoc->pCurrentGeoDef->pOptions->bIsChild = true;

		// leave a small gap between this group and the previous one
		y+=14;

		// Cloth geometry label
		if (!pDefDoc->pGeoAdvClothGroupLabel) {
			pDefDoc->pGeoAdvClothGroupLabel = ui_LabelCreate("Cloth Geometry", x, y);
		}
		if (!pDefDoc->pGeoAdvClothGroupLabel->widget.group) {
			ui_ExpanderAddChild(pExpander, pDefDoc->pGeoAdvClothGroupLabel);
		}
		y+=28;

		// Cloth geometry choice control
		if (!pDefDoc->pGeoIsClothLabel) {
			pDefDoc->pGeoIsClothLabel = ui_LabelCreate("Is Cloth", x, y);
		}
		if (!pDefDoc->pGeoIsClothLabel->widget.group) {
			ui_ExpanderAddChild(pExpander, pDefDoc->pGeoIsClothLabel);
		}
		if (!pDefDoc->pDefGeoIsClothField) {
			pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pClothData), pDefDoc->pCurrentGeoDef->pClothData, parse_PCGeometryClothData, "IsCloth");
			MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
			MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
			pDefDoc->pDefGeoIsClothField = pField;
		}
		else {
			pDefDoc->pDefGeoIsClothField->pOld = SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pClothData);
			pDefDoc->pDefGeoIsClothField->pNew = SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pClothData);
		}
		ui_WidgetSetPosition(pDefDoc->pDefGeoIsClothField->pUIWidget, x+100, y);
		ui_WidgetSetWidth(pDefDoc->pDefGeoIsClothField->pUIWidget, 120);
		MEFieldRefreshFromData(pDefDoc->pDefGeoIsClothField);
		y += 28;
		
		// Cloth geometry back choice control
		if (!pDefDoc->pGeoClothBackLabel) {
			pDefDoc->pGeoClothBackLabel = ui_LabelCreate("Has Back Side", x, y);
		}
		if (!pDefDoc->pGeoClothBackLabel->widget.group) {
			ui_ExpanderAddChild(pExpander, pDefDoc->pGeoClothBackLabel);
		}
		if (!pDefDoc->pDefGeoClothBackField) {
			pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pClothData), pDefDoc->pCurrentGeoDef->pClothData, parse_PCGeometryClothData, "HasClothBack");
			MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
			MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
			pDefDoc->pDefGeoClothBackField = pField;
		}
		else {
			pDefDoc->pDefGeoClothBackField->pOld = SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pClothData);
			pDefDoc->pDefGeoClothBackField->pNew = SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pClothData);
		}
		ui_WidgetSetPosition(pDefDoc->pDefGeoClothBackField->pUIWidget, x+100, y);
		ui_WidgetSetWidth(pDefDoc->pDefGeoClothBackField->pUIWidget, 120);
		MEFieldRefreshFromData(pDefDoc->pDefGeoClothBackField);
		y += 28;

		// Cloth info field
		if (!pDefDoc->pGeoClothInfoLabel) {
			pDefDoc->pGeoClothInfoLabel = ui_LabelCreate("Cloth Info", x, y);
		}
		if (!pDefDoc->pGeoClothInfoLabel->widget.group) {
			ui_ExpanderAddChild(pExpander, pDefDoc->pGeoClothInfoLabel);
		}
		if (!pDefDoc->pDefGeoClothInfoField) {
			pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pClothData), pDefDoc->pCurrentGeoDef->pClothData, parse_PCGeometryClothData, "ClothInfo");
			MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
			MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
			pDefDoc->pDefGeoClothInfoField = pField;
		}
		else {
			pDefDoc->pDefGeoClothInfoField->pOld = SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pClothData);
			pDefDoc->pDefGeoClothInfoField->pNew = SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pClothData);
		}
		ui_WidgetSetPosition(pDefDoc->pDefGeoClothInfoField->pUIWidget, x+100, y);
		ui_WidgetSetWidthEx(pDefDoc->pDefGeoClothInfoField->pUIWidget, 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pDefDoc->pDefGeoClothInfoField->pUIWidget, 0, 20, 0, 0);
		MEFieldRefreshFromData(pDefDoc->pDefGeoClothInfoField);
		y += 28;

		// Cloth collision info field
		if (!pDefDoc->pGeoClothCollisionLabel) {
			pDefDoc->pGeoClothCollisionLabel = ui_LabelCreate("Cloth Collision", x, y);
		}
		if (!pDefDoc->pGeoClothCollisionLabel->widget.group) {
			ui_ExpanderAddChild(pExpander, pDefDoc->pGeoClothCollisionLabel);
		}
		if (!pDefDoc->pDefGeoClothCollisionField) {
			pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pClothData), pDefDoc->pCurrentGeoDef->pClothData, parse_PCGeometryClothData, "ClothCollision");
			MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
			MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
			pDefDoc->pDefGeoClothCollisionField = pField;
		}
		else {
			pDefDoc->pDefGeoClothCollisionField->pOld = SAFE_MEMBER2(pDefDoc, pOrigGeoDef, pClothData);
			pDefDoc->pDefGeoClothCollisionField->pNew = SAFE_MEMBER2(pDefDoc, pCurrentGeoDef, pClothData);
		}
		ui_WidgetSetPosition(pDefDoc->pDefGeoClothCollisionField->pUIWidget, x+100, y);
		ui_WidgetSetWidthEx(pDefDoc->pDefGeoClothCollisionField->pUIWidget, 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pDefDoc->pDefGeoClothCollisionField->pUIWidget, 0, 20, 0, 0);
		MEFieldRefreshFromData(pDefDoc->pDefGeoClothCollisionField);
		y += 28;
	}

	if (pBoneDef && eaSize(&pBoneDef->eaChildBones)) {
		// if this geometry's bone has a child bone, then show controls to specify a list of child geometries for
		// this geometry

		// leave a small gap between this group and the previous one
		y+=14;

		for(i=0; i<eaSize(&pBoneDef->eaChildBones); ++i) {
			PCChildBone *pBoneInfo = pBoneDef->eaChildBones[i];
			PCGeometryChildDef *pGeoInfo = NULL;
			PCGeometryChildDef *pOrigGeoInfo = NULL;
			CostumeGeoChildBoneGroup *pBoneGroup;
			char buffer[260] = { 0 };

			// Find the child geo info
			for(j=eaSize(&pDefDoc->pCurrentGeoDef->pOptions->eaChildGeos)-1; j>=0; --j) {
				if (GET_REF(pBoneInfo->hChildBone) == GET_REF(pDefDoc->pCurrentGeoDef->pOptions->eaChildGeos[j]->hChildBone)) {
					pGeoInfo = pDefDoc->pCurrentGeoDef->pOptions->eaChildGeos[j];
					break;
				}
			}
			if (!pGeoInfo) {
				pGeoInfo = StructCreate(parse_PCGeometryChildDef);
				SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, GET_REF(pBoneInfo->hChildBone), pGeoInfo->hChildBone);
				eaPush(&pDefDoc->pCurrentGeoDef->pOptions->eaChildGeos, pGeoInfo);
			}
			if (pDefDoc->pOrigGeoDef && pDefDoc->pOrigGeoDef->pOptions) {
				for(j=eaSize(&pDefDoc->pOrigGeoDef->pOptions->eaChildGeos)-1; j>=0; --j) {
					if (GET_REF(pBoneInfo->hChildBone) == GET_REF(pDefDoc->pOrigGeoDef->pOptions->eaChildGeos[j]->hChildBone)) {
						pOrigGeoInfo = pDefDoc->pOrigGeoDef->pOptions->eaChildGeos[j];
						break;
					}
				}
			}

			// Get the proper control group
			if (i >= eaSize(&pDefDoc->eaGeoChildBoneGroups)) {
				pBoneGroup = calloc(1,sizeof(CostumeGeoChildBoneGroup));
				pBoneGroup->pDefDoc = pDefDoc;
				pBoneGroup->index = i;
				eaPush(&pDefDoc->eaGeoChildBoneGroups, pBoneGroup);
			} else {
				pBoneGroup = pDefDoc->eaGeoChildBoneGroups[i];
			}

			if (!pBoneGroup->pGeoTitleLabel) {
				if (GET_REF(pBoneInfo->hChildBone)) {
					if (gUseDispNames && GET_REF(GET_REF(pBoneInfo->hChildBone)->displayNameMsg.hMessage)) {
						sprintf(buffer, "Child Bone Setup: %s", GET_REF(GET_REF(pBoneInfo->hChildBone)->displayNameMsg.hMessage)->pcDefaultString);
					} else {
						sprintf(buffer, "Child Bone Setup: %s", GET_REF(pBoneInfo->hChildBone)->pcName);
					}
				} else {
					sprintf(buffer, "Child Bone Setup: Unnamed");
				}
				pBoneGroup->pGeoTitleLabel = ui_LabelCreate(buffer, x, y);
			}
			if (!pBoneGroup->pGeoTitleLabel->widget.group) {
				ui_ExpanderAddChild(pExpander, pBoneGroup->pGeoTitleLabel);
			}
			ui_WidgetSetPosition(UI_WIDGET(pBoneGroup->pGeoTitleLabel), x, y);
			y += 28;

			// Child required boolean choice control
			if (!pBoneGroup->pGeoChildRequiredLabel) {
				pBoneGroup->pGeoChildRequiredLabel = ui_LabelCreate("Required", x, y);
			}
			if (!pBoneGroup->pGeoChildRequiredLabel->widget.group) {
				ui_ExpanderAddChild(pExpander, pBoneGroup->pGeoChildRequiredLabel);
			}
			if (!pBoneGroup->pDefGeoChildRequiredField) {
				pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pOrigGeoInfo, pGeoInfo, parse_PCGeometryChildDef, "RequiresChildGeometry");
				MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
				MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
				pBoneGroup->pDefGeoChildRequiredField = pField;
			}
			else {
				pBoneGroup->pDefGeoChildRequiredField->pOld = pOrigGeoInfo;
				pBoneGroup->pDefGeoChildRequiredField->pNew = pGeoInfo;
			}
			ui_WidgetSetPosition(pBoneGroup->pDefGeoChildRequiredField->pUIWidget, x+100, y);
			ui_WidgetSetWidth(pBoneGroup->pDefGeoChildRequiredField->pUIWidget, 120);
			MEFieldRefreshFromData(pBoneGroup->pDefGeoChildRequiredField);
			y += 28;

			// Default geo control; show only if at least one child has been specified
			if (eaSize(&pGeoInfo->eaChildGeometries) > 0) {
				// if the child geo is not set, then set it to "None" for display purposes
				if (!GET_REF(pGeoInfo->hDefaultChildGeo)) {
					SET_HANDLE_FROM_STRING(g_hCostumeGeometryDict, "None", pGeoInfo->hDefaultChildGeo);
				}
				if (pOrigGeoInfo && !GET_REF(pOrigGeoInfo->hDefaultChildGeo)) {
					SET_HANDLE_FROM_STRING(g_hCostumeGeometryDict, "None", pOrigGeoInfo->hDefaultChildGeo);
				}

				// Default geometry control
				if (!pBoneGroup->pGeoChildDefaultLabel) {
					pBoneGroup->pGeoChildDefaultLabel = ui_LabelCreate("Default", x, y);
				}
				if (!pBoneGroup->pGeoChildDefaultLabel->widget.group) {
					ui_ExpanderAddChild(pExpander, pBoneGroup->pGeoChildDefaultLabel);
				}
				if (!pBoneGroup->pDefGeoChildDefaultField) {
					pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pOrigGeoInfo, pGeoInfo, parse_PCGeometryChildDef, "DefaultChildGeometry", NULL, &pBoneGroup->eaGeoChildNames, NULL);
					MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
					MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
					pBoneGroup->pDefGeoChildDefaultField = pField;
				} else {
					pBoneGroup->pDefGeoChildDefaultField->pOld = pOrigGeoInfo;
					pBoneGroup->pDefGeoChildDefaultField->pNew = pGeoInfo;
				}
				ui_WidgetSetPosition(pBoneGroup->pDefGeoChildDefaultField->pUIWidget, x+100, y);
				ui_WidgetSetWidthEx(pBoneGroup->pDefGeoChildDefaultField->pUIWidget, 1, UIUnitPercentage);
				ui_WidgetSetPaddingEx(pBoneGroup->pDefGeoChildDefaultField->pUIWidget, 0, 21, 0, 0);
				MEFieldRefreshFromData(pBoneGroup->pDefGeoChildDefaultField);
				y += 28;
			}
			else {
				if (pBoneGroup->pGeoChildDefaultLabel && pBoneGroup->pGeoChildDefaultLabel->widget.group) {
					ui_WidgetRemoveFromGroup(UI_WIDGET(pBoneGroup->pGeoChildDefaultLabel));
				}
				if (pBoneGroup->pDefGeoChildDefaultField) {
					MEFieldSafeDestroy(&pBoneGroup->pDefGeoChildDefaultField);
					REMOVE_HANDLE(pGeoInfo->hDefaultChildGeo);	// clear the MEField's data as well
				}
			}

			// Free memory for excess groups
			for (j = eaSize(&pBoneGroup->eaChildGroups)-1; j >= eaSize(&pGeoInfo->eaChildGeometries); --j) {
				MEFieldSafeDestroy(&pBoneGroup->eaChildGroups[j]->pGeoField);
				ui_WidgetQueueFreeAndNull(&pBoneGroup->eaChildGroups[j]->pIndexLabel);
				ui_WidgetQueueFreeAndNull(&pBoneGroup->eaChildGroups[j]->pRemoveButton);
				free(pBoneGroup->eaChildGroups[j]);
				eaRemove(&pBoneGroup->eaChildGroups, j);
			}

			// Add the entry controls
			for(j=0; j<eaSize(&pGeoInfo->eaChildGeometries); ++j) {
				CostumeGeoChildGeoGroup* pChildGroup = NULL;
				PCGeometryRef* pChildRef = NULL;
				PCGeometryRef* pOrigChildRef = NULL;

				// Get the proper child group
				if (j >= eaSize(&pBoneGroup->eaChildGroups)) {
					pChildGroup = calloc(1,sizeof(CostumeGeoChildGeoGroup));
					pChildGroup->pDefDoc = pDefDoc;
					pChildGroup->index = i; // Yes, this should be "i" and not "j" as we want the bone index
					eaPush(&pBoneGroup->eaChildGroups, pChildGroup);
				} else {
					pChildGroup = pBoneGroup->eaChildGroups[j];
				}

				// Get pointers set up
				pChildRef = pGeoInfo->eaChildGeometries[j];
				if (pOrigGeoInfo && pOrigGeoInfo->eaChildGeometries && j < eaSize(&pOrigGeoInfo->eaChildGeometries)) {
					pOrigChildRef = pOrigGeoInfo->eaChildGeometries[j];
				}

				// create a new label widget if necessary
				if (!pChildGroup->pIndexLabel) {
					sprintf(buffer, "%d", j+1);
					pChildGroup->pIndexLabel = ui_LabelCreate(buffer, x, y);
				}
				if (!pChildGroup->pIndexLabel->widget.group) {
					ui_ExpanderAddChild(pExpander, pChildGroup->pIndexLabel);
				}

				// create a new remove button if necessary
				if (!pChildGroup->pRemoveButton) {
					pChildGroup->pRemoveButton = ui_ButtonCreate("Remove", x+20, y, costumeDefEdit_UIGeoChildRemove, pChildGroup);
					ui_WidgetSetWidth(UI_WIDGET(pChildGroup->pRemoveButton), 70);
					ui_ExpanderAddChild(pExpander, pChildGroup->pRemoveButton);
				}
				ui_WidgetSetPosition(UI_WIDGET(pChildGroup->pRemoveButton), x+20, y);

				// create or update the combo box
				if (!pChildGroup->pGeoField) {
					pChildGroup->pGeoField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pOrigChildRef, pChildRef, parse_PCGeometryRef, "hGeo", NULL, &pBoneGroup->eaGeoChildNames, NULL);
					MEFieldAddToParent(pChildGroup->pGeoField, UI_WIDGET(pExpander), x+100, y);
					MEFieldSetChangeCallback(pChildGroup->pGeoField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
				}
				else {
					pChildGroup->pGeoField->pOld = pOrigChildRef;
					pChildGroup->pGeoField->pNew = pChildRef;
				}
				ui_WidgetSetPosition(pChildGroup->pGeoField->pUIWidget, x+100, y);
				ui_WidgetSetWidthEx(pChildGroup->pGeoField->pUIWidget, 1, UIUnitPercentage);
				ui_WidgetSetPaddingEx(pChildGroup->pGeoField->pUIWidget, 0, 21, 0, 0);
				MEFieldRefreshFromData(pChildGroup->pGeoField);

				// add space for the row
				y += 28;

			}

			// if no child geometries are present, then show a label saying so
			if (eaSize(&pBoneGroup->eaChildGroups) == 0) {
				if (!pBoneGroup->pGeoChildEmptyLabel) {
					pBoneGroup->pGeoChildEmptyLabel = ui_LabelCreate("No child geometries specified", x, y);
				}
				if (!pBoneGroup->pGeoChildEmptyLabel->widget.group) {
					ui_ExpanderAddChild(pExpander, pBoneGroup->pGeoChildEmptyLabel);
				}
				ui_WidgetSetPosition(UI_WIDGET(pBoneGroup->pGeoChildEmptyLabel), x, y);
				y += 28;
			}
			else {
				ui_WidgetQueueFreeAndNull(&pBoneGroup->pGeoChildEmptyLabel);
			}

			// add a button to include more child geometries
			if (!pBoneGroup->pGeoChildAddButton) {
				pBoneGroup->pGeoChildAddButton = ui_ButtonCreate("Add Child Geometry...", x, y, costumeDefEdit_UIGeoChildAdd, pBoneGroup);
				ui_WidgetSetWidth(UI_WIDGET(pBoneGroup->pGeoChildAddButton), 200);
				ui_ExpanderAddChild(pExpander, pBoneGroup->pGeoChildAddButton);
			}
			ui_WidgetSetPosition(UI_WIDGET(pBoneGroup->pGeoChildAddButton), x, y);
			y += 28;

		}
	}
	
	// Free memory for excess groups
	if (pDefDoc->pCurrentGeoDef) {
		iNumChild = eaSize(&pDefDoc->pCurrentGeoDef->pOptions->eaChildGeos);
	}
	for (i = eaSize(&pDefDoc->eaGeoChildBoneGroups)-1; i >= iNumChild; --i) {
		CostumeGeoChildBoneGroup *pBoneGroup = pDefDoc->eaGeoChildBoneGroups[i];

		MEFieldSafeDestroy(&pBoneGroup->pDefGeoChildRequiredField);
		MEFieldSafeDestroy(&pBoneGroup->pDefGeoChildDefaultField);
		ui_WidgetQueueFreeAndNull(&pBoneGroup->pGeoTitleLabel);
		ui_WidgetQueueFreeAndNull(&pBoneGroup->pGeoChildRequiredLabel);
		ui_WidgetQueueFreeAndNull(&pBoneGroup->pGeoChildDefaultLabel);
		ui_WidgetQueueFreeAndNull(&pBoneGroup->pGeoChildEmptyLabel);
		ui_WidgetQueueFreeAndNull(&pBoneGroup->pGeoChildAddButton);
		for(j=eaSize(&pBoneGroup->eaChildGroups)-1; j>=0; --j) {
			MEFieldSafeDestroy(&pBoneGroup->eaChildGroups[j]->pGeoField);
			ui_WidgetQueueFreeAndNull(&pBoneGroup->eaChildGroups[j]->pIndexLabel);
			ui_WidgetQueueFreeAndNull(&pBoneGroup->eaChildGroups[j]->pRemoveButton);
			free(pBoneGroup->eaChildGroups[j]);
			eaRemove(&pBoneGroup->eaChildGroups, j);
		}
		eaDestroy(&pBoneGroup->eaChildGroups);

		eaDestroyEx(&pBoneGroup->eaGeoChildNames, NULL);

		free(pDefDoc->eaGeoChildBoneGroups[i]);
		eaRemove(&pDefDoc->eaGeoChildBoneGroups, i);
	}

	ui_ExpanderSetHeight(pExpander, y);
}

void costumeDefEdit_UpdateGeoFx(CostumeEditDefDoc* pDefDoc)
{
	UIExpander* pExpander = pDefDoc->pGeoFxExpander;
	CostumeDefFxGroup* pFxGroup = NULL;
	int i = 0, numFx = 0;
	int y = 0;

	// Free memory for excess FX groups
	for (i = eaSize(&pDefDoc->eaGeoFxGroups)-1
		; i >= (pDefDoc->pCurrentGeoDef->pOptions ? eaSize(&pDefDoc->pCurrentGeoDef->pOptions->eaFX) : 0)
		; --i) {
		MEFieldSafeDestroy(&pDefDoc->eaGeoFxGroups[i]->pFxField);
		MEFieldSafeDestroy(&pDefDoc->eaGeoFxGroups[i]->pHueField);
		ui_WidgetQueueFreeAndNull(&pDefDoc->eaGeoFxGroups[i]->pRemoveButton);
		ui_WidgetQueueFreeAndNull(&pDefDoc->eaGeoFxGroups[i]->pSeparator);
		free(pDefDoc->eaGeoFxGroups[i]);
		eaRemove(&pDefDoc->eaGeoFxGroups, i);
	}

	// Add or change FX Add entry controls
	numFx = pDefDoc->pCurrentGeoDef->pOptions ? eaSize(&pDefDoc->pCurrentGeoDef->pOptions->eaFX) : 0;
	for (i = 0; i < numFx; i++) {
		NOCONST(PCFX) *pOldFx = NULL;

		if (eaSize(&pDefDoc->eaGeoFxGroups) <= i) {
			// Allocate new group
			pFxGroup = calloc(1, sizeof(CostumeDefFxGroup));
			pFxGroup->pDefDoc = pDefDoc;
			eaPush(&pDefDoc->eaGeoFxGroups, pFxGroup);
		}
		else {
			pFxGroup = pDefDoc->eaGeoFxGroups[i];
		}

		if ((pDefDoc->pOrigGeoDef && pDefDoc->pOrigGeoDef->pOptions) && (i < eaSize(&pDefDoc->pOrigGeoDef->pOptions->eaFX))) {
			pOldFx = CONTAINER_NOCONST(PCFX, pDefDoc->pOrigGeoDef->pOptions->eaFX[i]);
		}

		if (!pFxGroup->pFxField) {
			pFxGroup->pFxField = MEFieldCreateSimpleDictionary(kMEFieldType_TextEntry, pOldFx, pDefDoc->pCurrentGeoDef->pOptions->eaFX[i], parse_PCFX, "pcName", "DynFxInfo", parse_DynFxInfo, "InternalName");
			MEFieldAddToParent(pFxGroup->pFxField, UI_WIDGET(pExpander), 15, 0);
			MEFieldSetChangeCallback(pFxGroup->pFxField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
		}
		else {
			pFxGroup->pFxField->pOld = pOldFx;
			pFxGroup->pFxField->pNew = pDefDoc->pCurrentGeoDef->pOptions->eaFX[i];
		}
		ui_WidgetSetPosition(pFxGroup->pFxField->pUIWidget, 15, y);
		ui_WidgetSetWidthEx(pFxGroup->pFxField->pUIWidget, 1, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pFxGroup->pFxField->pUIWidget, 0, 80, 0, 0);
		MEFieldRefreshFromData(pFxGroup->pFxField);

		if (!pFxGroup->pRemoveButton) {
			pFxGroup->pRemoveButton = ui_ButtonCreate("Remove", 0, y+14, costumeDefEdit_UIGeoFXRemove, pFxGroup);
			ui_WidgetSetWidth(UI_WIDGET(pFxGroup->pRemoveButton), 70);
			ui_ExpanderAddChild(pExpander, pFxGroup->pRemoveButton);
		}
		ui_WidgetSetPositionEx(UI_WIDGET(pFxGroup->pRemoveButton), 0, y+14, 0, 0, UITopRight);

		y += 28;

		if (!pFxGroup->pHueField) {
			pFxGroup->pHueField = MEFieldCreateSimple(kMEFieldType_SliderText, pOldFx, pDefDoc->pCurrentGeoDef->pOptions->eaFX[i], parse_PCFX, "fHue");
			MEFieldAddToParent(pFxGroup->pHueField, UI_WIDGET(pExpander), 15, 0);
			MEFieldSetChangeCallback(pFxGroup->pHueField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
			ui_SliderTextEntrySetRange(pFxGroup->pHueField->pUISliderText, 0, 360, 1);
		} else {
			pFxGroup->pHueField->pOld = pOldFx;
			pFxGroup->pHueField->pNew = pDefDoc->pCurrentGeoDef->pOptions->eaFX[i];
		}
		ui_WidgetSetPositionEx(pFxGroup->pHueField->pUIWidget, 80, y, 0, 0, UITopRight);
		ui_WidgetSetWidth(pFxGroup->pHueField->pUIWidget, 120);
		MEFieldRefreshFromData(pFxGroup->pHueField); // Need to refresh after setting range

		y += 28;


		if (!pFxGroup->pSeparator) {
			pFxGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
			ui_ExpanderAddChild(pExpander, pFxGroup->pSeparator);
		}
		ui_WidgetSetPosition(UI_WIDGET(pFxGroup->pSeparator), 15, y);
		y += 9;
	}

	// If no FX, then a default string
	if (numFx == 0) {
		if (!pDefDoc->pGeoFxEmptyLabel) {
			pDefDoc->pGeoFxEmptyLabel = ui_LabelCreate("No FX s are currently defined", 15, y);
			ui_ExpanderAddChild(pExpander, pDefDoc->pGeoFxEmptyLabel);
		}
		ui_WidgetSetPosition(UI_WIDGET(pDefDoc->pGeoFxEmptyLabel), 15, y);
		y += 20;
	} else  {
		ui_WidgetQueueFreeAndNull(&pDefDoc->pGeoFxEmptyLabel);
	}

	// Button to add FX
	if (!pDefDoc->pGeoFxAddButton) {
		pDefDoc->pGeoFxAddButton = ui_ButtonCreate("Add FX ...", 15, y, costumeDefEdit_UIGeoFXAdd, pDefDoc);
		pDefDoc->pGeoFxAddButton->widget.width = 100;
		ui_ExpanderAddChild(pExpander, pDefDoc->pGeoFxAddButton);
	}
	ui_WidgetSetPosition(UI_WIDGET(pDefDoc->pGeoFxAddButton), 15, y);
	y += 28;

	ui_ExpanderSetHeight(pExpander, y);
}

// Update the list of controls in the geometry FX swap UI
void costumeDefEdit_UpdateGeoFxSwaps(CostumeEditDefDoc* pDefDoc)
{
	UIExpander* pExpander = pDefDoc->pGeoFxSwapExpander;
	CostumeDefFxSwapGroup* pFxSwapGroup = NULL;
	int i = 0, numFxSwap = 0;
	int y = 0;

	// Free memory for excess FX groups
	for (i = eaSize(&pDefDoc->eaGeoFxSwapGroups)-1
		; i >= (pDefDoc->pCurrentGeoDef->pOptions ? eaSize(&pDefDoc->pCurrentGeoDef->pOptions->eaFXSwap) : 0)
		; --i) {
		MEFieldSafeDestroy(&pDefDoc->eaGeoFxSwapGroups[i]->pFxOldField);
		MEFieldSafeDestroy(&pDefDoc->eaGeoFxSwapGroups[i]->pFxNewField);
		ui_WidgetQueueFreeAndNull(&pDefDoc->eaGeoFxSwapGroups[i]->pRemoveButton);
		ui_WidgetQueueFreeAndNull(&pDefDoc->eaGeoFxSwapGroups[i]->pSeparator);
		free(pDefDoc->eaGeoFxSwapGroups[i]);
		eaRemove(&pDefDoc->eaGeoFxSwapGroups, i);
	}

	// Add or change FX Swap entry controls
	numFxSwap = pDefDoc->pCurrentGeoDef->pOptions ? eaSize(&pDefDoc->pCurrentGeoDef->pOptions->eaFXSwap) : 0;
	for (i = 0; i < numFxSwap; i++) {
		NOCONST(PCFXSwap) *pOldFxSwap = NULL;

		if (eaSize(&pDefDoc->eaGeoFxSwapGroups) <= i) {
			// Allocate new group
			pFxSwapGroup = calloc(1, sizeof(CostumeDefFxSwapGroup));
			pFxSwapGroup->pDefDoc = pDefDoc;
			eaPush(&pDefDoc->eaGeoFxSwapGroups, pFxSwapGroup);
		}
		else {
			pFxSwapGroup = pDefDoc->eaGeoFxSwapGroups[i];
		}

		if ((pDefDoc->pOrigGeoDef && pDefDoc->pOrigGeoDef->pOptions) && (i < eaSize(&pDefDoc->pOrigGeoDef->pOptions->eaFXSwap))) {
			pOldFxSwap = CONTAINER_NOCONST(PCFXSwap, pDefDoc->pOrigGeoDef->pOptions->eaFXSwap[i]);
		}

		if (!pFxSwapGroup->pFxOldField) {
			pFxSwapGroup->pFxOldField = MEFieldCreateSimpleDictionary(kMEFieldType_TextEntry, pOldFxSwap, pDefDoc->pCurrentGeoDef->pOptions->eaFXSwap[i], parse_PCFXSwap, "pcOldName", "DynFxInfo", parse_DynFxInfo, "InternalName");
			MEFieldAddToParent(pFxSwapGroup->pFxOldField, UI_WIDGET(pExpander), 15, 0);
			MEFieldSetChangeCallback(pFxSwapGroup->pFxOldField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
		}
		else {
			pFxSwapGroup->pFxOldField->pOld = pOldFxSwap;
			pFxSwapGroup->pFxOldField->pNew = pDefDoc->pCurrentGeoDef->pOptions->eaFXSwap[i];
		}
		ui_WidgetSetPosition(pFxSwapGroup->pFxOldField->pUIWidget, 15, y);
		ui_WidgetSetWidthEx(pFxSwapGroup->pFxOldField->pUIWidget, 1, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pFxSwapGroup->pFxOldField->pUIWidget, 0, 80, 0, 0);
		MEFieldRefreshFromData(pFxSwapGroup->pFxOldField);

		if (!pFxSwapGroup->pRemoveButton) {
			pFxSwapGroup->pRemoveButton = ui_ButtonCreate("Remove", 0, y+14, costumeDefEdit_UIGeoFXSwapRemove, pFxSwapGroup);
			ui_WidgetSetWidth(UI_WIDGET(pFxSwapGroup->pRemoveButton), 70);
			ui_ExpanderAddChild(pExpander, pFxSwapGroup->pRemoveButton);
		}
		ui_WidgetSetPositionEx(UI_WIDGET(pFxSwapGroup->pRemoveButton), 0, y+14, 0, 0, UITopRight);

		y += 28;

		if (!pFxSwapGroup->pFxNewField) {
			pFxSwapGroup->pFxNewField = MEFieldCreateSimpleDictionary(kMEFieldType_TextEntry, pOldFxSwap, pDefDoc->pCurrentGeoDef->pOptions->eaFXSwap[i], parse_PCFXSwap, "pcNewName", "DynFxInfo", parse_DynFxInfo, "InternalName");
			MEFieldAddToParent(pFxSwapGroup->pFxNewField, UI_WIDGET(pExpander), 15, 0);
			MEFieldSetChangeCallback(pFxSwapGroup->pFxNewField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
		}
		else {
			pFxSwapGroup->pFxNewField->pOld = pOldFxSwap;
			pFxSwapGroup->pFxNewField->pNew = pDefDoc->pCurrentGeoDef->pOptions->eaFXSwap[i];
		}
		ui_WidgetSetPosition(pFxSwapGroup->pFxNewField->pUIWidget, 15, y);
		ui_WidgetSetWidthEx(pFxSwapGroup->pFxNewField->pUIWidget, 1, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pFxSwapGroup->pFxNewField->pUIWidget, 0, 80, 0, 0);
		MEFieldRefreshFromData(pFxSwapGroup->pFxNewField);

		y += 28;

		if (!pFxSwapGroup->pSeparator) {
			pFxSwapGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
			ui_ExpanderAddChild(pExpander, pFxSwapGroup->pSeparator);
		}
		ui_WidgetSetPosition(UI_WIDGET(pFxSwapGroup->pSeparator), 15, y);
		y += 9;
	}

	// If no FX, then a default string
	if (numFxSwap == 0) {
		if (!pDefDoc->pGeoFxSwapEmptyLabel) {
			pDefDoc->pGeoFxSwapEmptyLabel = ui_LabelCreate("No FX Swaps are currently defined", 15, y);
			ui_ExpanderAddChild(pExpander, pDefDoc->pGeoFxSwapEmptyLabel);
		}
		ui_WidgetSetPosition(UI_WIDGET(pDefDoc->pGeoFxSwapEmptyLabel), 15, y);
		y += 20;
	} else  {
		ui_WidgetQueueFreeAndNull(&pDefDoc->pGeoFxSwapEmptyLabel);
	}

	// Button to add FX
	if (!pDefDoc->pGeoFxSwapAddButton) {
		pDefDoc->pGeoFxSwapAddButton = ui_ButtonCreate("Add FX Swap...", 15, y, costumeDefEdit_UIGeoFXSwapAdd, pDefDoc);
		pDefDoc->pGeoFxSwapAddButton->widget.width = 100;
		ui_ExpanderAddChild(pExpander, pDefDoc->pGeoFxSwapAddButton);
	}
	ui_WidgetSetPosition(UI_WIDGET(pDefDoc->pGeoFxSwapAddButton), 15, y);
	y += 28;

	ui_ExpanderSetHeight(pExpander, y);
}


void costumeDefEdit_DefUpdateLists(CostumeEditDefDoc *pDefDoc)
{
	int i, j;
	bool bFound;
	Material *pMaterial = NULL;
	PCSkeletonDef *pSkel = NULL;
	PCBoneDef *pBone = NULL;
	char text[256];

	pDefDoc->bIgnoreDefFieldChanges = true;

	

	// Get the bones list
	if (pDefDoc->pcSkeleton) {
		pSkel = RefSystem_ReferentFromString(g_hCostumeSkeletonDict, pDefDoc->pcSkeleton);
	}
	costumeTailor_GetValidBones(NULL, pSkel, NULL, NULL, NULL/*species*/, NULL, NULL, &pDefDoc->eaDefBones, (gUseDispNames ? CGVF_SORT_DISPLAY : 0) | CGVF_UNLOCK_ALL);

	// Validate pCurrentBoneDef
	bFound = (eaFind(&pDefDoc->eaDefBones, pDefDoc->pCurrentBoneDef) != -1);
	if (!bFound) {
		if (eaSize(&pDefDoc->eaDefBones)) {
			pDefDoc->pCurrentBoneDef = pDefDoc->eaDefBones[0];
		} else {
			pDefDoc->pCurrentBoneDef = NULL;
		}
	}

	// Update bone combo
	if (pDefDoc->pCurrentBoneDef) {
		ui_ComboBoxSetSelectedObject(pDefDoc->pDefBoneCombo, pDefDoc->pCurrentBoneDef);
	}

	// Reset text labels
	if (pDefDoc->pCurrentBoneDef && GET_REF(pDefDoc->pCurrentBoneDef->geometryFieldDispName.hMessage))
	{
		ui_LabelSetText(pDefDoc->pGeoGeometryTextLabel, TranslateDisplayMessage(pDefDoc->pCurrentBoneDef->geometryFieldDispName));
		ui_LabelSetText(pDefDoc->pMatGeometryTextLabel, TranslateDisplayMessage(pDefDoc->pCurrentBoneDef->geometryFieldDispName));
	}
	else
	{
		ui_LabelSetText(pDefDoc->pGeoGeometryTextLabel, "Geometry");
		ui_LabelSetText(pDefDoc->pMatGeometryTextLabel, "Geometry");
	}
	if (pDefDoc->pCurrentBoneDef && GET_REF(pDefDoc->pCurrentBoneDef->materialFieldDispName.hMessage))
	{
		ui_LabelSetText(pDefDoc->pMatMaterialTextLabel, TranslateDisplayMessage(pDefDoc->pCurrentBoneDef->materialFieldDispName));
		ui_LabelSetText(pDefDoc->pTexMaterialTextLabel, TranslateDisplayMessage(pDefDoc->pCurrentBoneDef->materialFieldDispName));
	}
	else
	{
		ui_LabelSetText(pDefDoc->pMatMaterialTextLabel, "Material");
		ui_LabelSetText(pDefDoc->pTexMaterialTextLabel, "Material");
	}
	if (pDefDoc->pCurrentBoneDef && GET_REF(pDefDoc->pCurrentBoneDef->patternFieldDispName.hMessage))
	{
		sprintf(text, "Default %s", TranslateDisplayMessage(pDefDoc->pCurrentBoneDef->patternFieldDispName));
		ui_LabelSetText(pDefDoc->pMatDefPatternTextLabel, text);
		sprintf(text, "Requires %s", TranslateDisplayMessage(pDefDoc->pCurrentBoneDef->patternFieldDispName));
		ui_LabelSetText(pDefDoc->pMatReqPatternTextLabel, text);
	}
	else
	{
		ui_LabelSetText(pDefDoc->pMatDefPatternTextLabel, "Default Pattern");
		ui_LabelSetText(pDefDoc->pMatReqPatternTextLabel, "Requires Pattern");
	}
	if (pDefDoc->pCurrentBoneDef && GET_REF(pDefDoc->pCurrentBoneDef->detailFieldDisplayName.hMessage))
	{
		sprintf(text, "Default %s", TranslateDisplayMessage(pDefDoc->pCurrentBoneDef->detailFieldDisplayName));
		ui_LabelSetText(pDefDoc->pMatDefDetailTextLabel, text);
		sprintf(text, "Requires %s", TranslateDisplayMessage(pDefDoc->pCurrentBoneDef->detailFieldDisplayName));
		ui_LabelSetText(pDefDoc->pMatReqDetailTextLabel, text);
	}
	else
	{
		ui_LabelSetText(pDefDoc->pMatDefDetailTextLabel, "Default Detail");
		ui_LabelSetText(pDefDoc->pMatReqDetailTextLabel, "Requires Detail");
	}
	if (pDefDoc->pCurrentBoneDef && GET_REF(pDefDoc->pCurrentBoneDef->specularFieldDisplayName.hMessage))
	{
		sprintf(text, "Default %s", TranslateDisplayMessage(pDefDoc->pCurrentBoneDef->specularFieldDisplayName));
		ui_LabelSetText(pDefDoc->pMatDefSpecularTextLabel, text);
		sprintf(text, "Requires %s", TranslateDisplayMessage(pDefDoc->pCurrentBoneDef->specularFieldDisplayName));
		ui_LabelSetText(pDefDoc->pMatReqSpecularTextLabel, text);
	}
	else
	{
		ui_LabelSetText(pDefDoc->pMatDefSpecularTextLabel, "Default Specular");
		ui_LabelSetText(pDefDoc->pMatReqSpecularTextLabel, "Requires Specular");
	}
	if (pDefDoc->pCurrentBoneDef && GET_REF(pDefDoc->pCurrentBoneDef->diffuseFieldDisplayName.hMessage))
	{
		sprintf(text, "Default %s", TranslateDisplayMessage(pDefDoc->pCurrentBoneDef->diffuseFieldDisplayName));
		ui_LabelSetText(pDefDoc->pMatDefDiffuseTextLabel, text);
		sprintf(text, "Requires %s", TranslateDisplayMessage(pDefDoc->pCurrentBoneDef->diffuseFieldDisplayName));
		ui_LabelSetText(pDefDoc->pMatReqDiffuseTextLabel, text);
	}
	else
	{
		ui_LabelSetText(pDefDoc->pMatDefDiffuseTextLabel, "Default Diffuse");
		ui_LabelSetText(pDefDoc->pMatReqDiffuseTextLabel, "Requires Diffuse");
	}
	if (pDefDoc->pCurrentBoneDef && GET_REF(pDefDoc->pCurrentBoneDef->movableFieldDisplayName.hMessage))
	{
		sprintf(text, "Default %s", TranslateDisplayMessage(pDefDoc->pCurrentBoneDef->movableFieldDisplayName));
		ui_LabelSetText(pDefDoc->pMatDefMovableTextLabel, text);
		sprintf(text, "Requires %s", TranslateDisplayMessage(pDefDoc->pCurrentBoneDef->movableFieldDisplayName));
		ui_LabelSetText(pDefDoc->pMatReqMovableTextLabel, text);
	}
	else
	{
		ui_LabelSetText(pDefDoc->pMatDefMovableTextLabel, "Default Movable");
		ui_LabelSetText(pDefDoc->pMatReqMovableTextLabel, "Requires Movable");
	}

	// Refresh eaDefGeos based on pCurrentBoneDef
	if (pDefDoc->pCurrentBoneDef) {
		if (pDefDoc->pCurrentBoneDef->pcName) {
			pBone = RefSystem_ReferentFromString(g_hCostumeBoneDict, pDefDoc->pCurrentBoneDef->pcName);
		}
		costumeTailor_GetValidGeos(NULL, pSkel, pBone, NULL, NULL/*species*/, NULL, &pDefDoc->eaDefGeos, false, false, gUseDispNames, true);
		if (eaSize(&pDefDoc->eaDefGeos) && stricmp(pDefDoc->eaDefGeos[0]->pcName, "None") == 0) {
			eaRemove(&pDefDoc->eaDefGeos, 0);
		}
		if (GET_REF(pDefDoc->pCurrentBoneDef->hMirrorBone)) {
			PCBoneDef *pMirrorBone = GET_REF(pDefDoc->pCurrentBoneDef->hMirrorBone);
			costumeTailor_GetValidGeos(NULL, pSkel, pMirrorBone, NULL, NULL/*species*/, NULL, &pDefDoc->eaDefMirrorGeos, false, false, gUseDispNames, true);
		} else {
			eaClear(&pDefDoc->eaDefMirrorGeos);
		}
	} else {
		eaClear(&pDefDoc->eaDefGeos);
		eaClear(&pDefDoc->eaDefMirrorGeos);
	}
	// Validate pCurrentGeoDef
	bFound = false;
	if (!pDefDoc->pOrigGeoDef && pDefDoc->pCurrentGeoDef && pDefDoc->pCurrentGeoDef->pcName) {
		bFound = true;
	}
	if (!bFound && pDefDoc->pCurrentGeoDef && pDefDoc->pCurrentGeoDef->pcName) {
		for(i=eaSize(&pDefDoc->eaDefGeos)-1; i>=0; --i) {
			if (stricmp(pDefDoc->eaDefGeos[i]->pcName, pDefDoc->pCurrentGeoDef->pcName) == 0) {
				bFound = true;
				break;
			}
		}
	}
	if (!bFound) {
		if (eaSize(&pDefDoc->eaDefGeos)) {
			costumeDefEdit_DefSetGeo(pDefDoc, pDefDoc->eaDefGeos[0]);
		} else {
			costumeDefEdit_DefSetGeo(pDefDoc, gNoGeoDef);
		}
	}
	assert(pDefDoc->pCurrentGeoDef);

	// Update selection
	bFound = false;
	if (pDefDoc->pCurrentGeoDef && pDefDoc->pCurrentGeoDef->pcName) {
		for(i=eaSize(&pDefDoc->eaDefGeos)-1; i>=0; --i) {
			if (stricmp(pDefDoc->eaDefGeos[i]->pcName, pDefDoc->pCurrentGeoDef->pcName) == 0) {
				ui_ComboBoxSetSelected(pDefDoc->pDefGeoCombo, i);
				ui_ListSetSelectedRow(pDefDoc->pDefGeoList, i);
				ui_ListScrollToSelection(pDefDoc->pDefGeoList);
				bFound = true;
				break;
			}
		}
	}
	if (!bFound) {
		ui_ComboBoxSetSelected(pDefDoc->pDefGeoCombo, -1);
		ui_ListSetSelectedRow(pDefDoc->pDefGeoList, -1);
	}

	// Refresh categories list
	if (pDefDoc->pCurrentGeoDef && pDefDoc->pCurrentGeoDef->pcName) {
		costumeTailor_GetValidCategories(NULL, GET_REF(pDefDoc->pCurrentBoneDef->hRegion), NULL/*species*/, NULL, NULL, NULL, &pDefDoc->eaCategories, (gUseDispNames ? CGVF_SORT_DISPLAY : 0) | CGVF_UNLOCK_ALL);
	}

	// Refresh eaDefMats based on pCurrentGeoDef
	if (pDefDoc->pCurrentGeoDef && pDefDoc->pCurrentGeoDef->pcName) {
		PCGeometryDef *pGeo = RefSystem_ReferentFromString(g_hCostumeGeometryDict, pDefDoc->pCurrentGeoDef->pcName);
		costumeTailor_GetValidMaterials(NULL, pGeo, NULL/*species*/, NULL, NULL, NULL, &pDefDoc->eaDefMats, false, gUseDispNames, true);
		if (eaSize(&pDefDoc->eaDefMats) && stricmp(pDefDoc->eaDefMats[0]->pcName, "None") == 0) {
			eaRemove(&pDefDoc->eaDefMats, 0);
		}
	} else {
		eaClear(&pDefDoc->eaDefMats);
	}
	// Validate pCurrentMatDef
	bFound = false;
	if (!pDefDoc->pOrigMatDef && pDefDoc->pCurrentMatDef && pDefDoc->pCurrentMatDef->pcName) {
		bFound = true;
	}
	if (!bFound && pDefDoc->pCurrentMatDef && pDefDoc->pCurrentMatDef->pcName) {
		for(i=eaSize(&pDefDoc->eaDefMats)-1; i>=0; --i) {
			if (stricmp(pDefDoc->eaDefMats[i]->pcName, pDefDoc->pCurrentMatDef->pcName) == 0) {
				bFound = true;
				break;
			}
		}
	}
	if (!bFound) {
		if (eaSize(&pDefDoc->eaDefMats)) {
			costumeDefEdit_DefSetMat(pDefDoc, pDefDoc->eaDefMats[0]);
		} else {
			costumeDefEdit_DefSetMat(pDefDoc, gNoMatDef);
		}
	}
	// Update selection
	bFound = false;
	if (pDefDoc->pCurrentMatDef && pDefDoc->pCurrentMatDef->pcName) {
		for(i=eaSize(&pDefDoc->eaDefMats)-1; i>=0; --i) {
			if (stricmp(pDefDoc->eaDefMats[i]->pcName, pDefDoc->pCurrentMatDef->pcName) == 0) {
				ui_ComboBoxSetSelected(pDefDoc->pDefMatCombo, i);
				ui_ListSetSelectedRow(pDefDoc->pDefMatList, i);
				ui_ListScrollToSelection(pDefDoc->pDefMatList);
				bFound = true;
				break;
			}
		}
		if (pDefDoc->pCurrentMatDef->pcMaterial) {
			pMaterial = materialFindNoDefault(pDefDoc->pCurrentMatDef->pcMaterial, 0);
		}
	}
	if (!bFound) {
		ui_ComboBoxSetSelected(pDefDoc->pDefMatCombo, -1);
		ui_ListSetSelectedRow(pDefDoc->pDefMatList, -1);
	}

	// Refresh eaDefTexs based on pCurrentMatDef
	if (pDefDoc->pCurrentMatDef && pDefDoc->pCurrentMatDef->pcName) {
		PCMaterialDef *pMat = RefSystem_ReferentFromString(g_hCostumeMaterialDict, pDefDoc->pCurrentMatDef->pcName);
		costumeTailor_GetValidTextures(NULL, pMat, NULL/*species*/, NULL, NULL, NULL, NULL, NULL,
			kPCTextureType_Pattern | kPCTextureType_Detail | kPCTextureType_Specular | kPCTextureType_Diffuse | kPCTextureType_Movable | kPCTextureType_Other,
			&pDefDoc->eaDefTexs, false, gUseDispNames, true);
		if (eaSize(&pDefDoc->eaDefTexs) && stricmp(pDefDoc->eaDefTexs[0]->pcName, "None") == 0) {
			eaRemove(&pDefDoc->eaDefTexs, 0);
		}
	} else {
		eaClear(&pDefDoc->eaDefTexs);
	}
	// Validate pCurrentTexDef
	bFound = false;
	if (pDefDoc->pCurrentTexDef && pDefDoc->pCurrentTexDef->pcName)
	{
		if (!pDefDoc->pOrigTexDef) {
			bFound = true;
		}
		if (!bFound) {
			for(i=eaSize(&pDefDoc->eaDefTexs)-1; i>=0; --i) {
				if (stricmp(pDefDoc->eaDefTexs[i]->pcName, pDefDoc->pCurrentTexDef->pcName) == 0) {
					bFound = true;
					break;
				}
			}
		}
	}
	if (!bFound) {
		if (eaSize(&pDefDoc->eaDefTexs)) {
			costumeDefEdit_DefSetTex(pDefDoc, pDefDoc->eaDefTexs[0]);
		} else {
			costumeDefEdit_DefSetTex(pDefDoc, gNoTexDef);
		}
	}

	// Update selection
	bFound = false;
	if (pDefDoc->pCurrentTexDef && pDefDoc->pCurrentTexDef->pcName) {
		for(i=eaSize(&pDefDoc->eaDefTexs)-1; i>=0; --i) {
			if (stricmp(pDefDoc->eaDefTexs[i]->pcName, pDefDoc->pCurrentTexDef->pcName) == 0) {
				ui_ListSetSelectedRow(pDefDoc->pDefTexList, i);
				ui_ListScrollToSelection(pDefDoc->pDefTexList);
				bFound = true;
				break;
			}
		}
	}
	if (!bFound) {
		ui_ListSetSelectedRow(pDefDoc->pDefTexList, -1);
	}

	// Refresh specific def texs based on pCurrentMatDef
	if (pDefDoc->pCurrentMatDef && pDefDoc->pCurrentMatDef->pcName) {
		costumeTailor_GetValidTextures(NULL, pDefDoc->pCurrentMatDef, NULL/*species*/, NULL, NULL, NULL, NULL, NULL, kPCTextureType_Pattern, &pDefDoc->eaDefPatternTex, false, gUseDispNames, true);
		costumeTailor_GetValidTextures(NULL, pDefDoc->pCurrentMatDef, NULL/*species*/, NULL, NULL, NULL, NULL, NULL, kPCTextureType_Detail, &pDefDoc->eaDefDetailTex, false, gUseDispNames, true);
		costumeTailor_GetValidTextures(NULL, pDefDoc->pCurrentMatDef, NULL/*species*/, NULL, NULL, NULL, NULL, NULL, kPCTextureType_Specular, &pDefDoc->eaDefSpecularTex, false, gUseDispNames, true);
		costumeTailor_GetValidTextures(NULL, pDefDoc->pCurrentMatDef, NULL/*species*/, NULL, NULL, NULL, NULL, NULL, kPCTextureType_Diffuse, &pDefDoc->eaDefDiffuseTex, false, gUseDispNames, true);
		costumeTailor_GetValidTextures(NULL, pDefDoc->pCurrentMatDef, NULL/*species*/, NULL, NULL, NULL, NULL, NULL, kPCTextureType_Movable, &pDefDoc->eaDefMovableTex, false, gUseDispNames, true);

		if (REF_STRING_FROM_HANDLE(pDefDoc->pCurrentMatDef->hDefaultPattern)) {
			for(i=eaSize(&pDefDoc->eaDefPatternTex)-1; i>=0; --i) {
				if (GET_REF(pDefDoc->pCurrentMatDef->hDefaultPattern) == pDefDoc->eaDefPatternTex[i]) {
					break;
				}
			}
			if (i < 0) {
				REMOVE_HANDLE(pDefDoc->pCurrentMatDef->hDefaultPattern);
			}
		}
		if (REF_STRING_FROM_HANDLE(pDefDoc->pCurrentMatDef->hDefaultDetail)) {
			for(i=eaSize(&pDefDoc->eaDefDetailTex)-1; i>=0; --i) {
				if (GET_REF(pDefDoc->pCurrentMatDef->hDefaultDetail) == pDefDoc->eaDefDetailTex[i]) {
					break;
				}
			}
			if (i < 0) {
				REMOVE_HANDLE(pDefDoc->pCurrentMatDef->hDefaultDetail);
			}
		}
		if (REF_STRING_FROM_HANDLE(pDefDoc->pCurrentMatDef->hDefaultSpecular)) {
			for(i=eaSize(&pDefDoc->eaDefSpecularTex)-1; i>=0; --i) {
				if (GET_REF(pDefDoc->pCurrentMatDef->hDefaultSpecular) == pDefDoc->eaDefSpecularTex[i]) {
					break;
				}
			}
			if (i < 0) {
				REMOVE_HANDLE(pDefDoc->pCurrentMatDef->hDefaultSpecular);
			}
		}
		if (REF_STRING_FROM_HANDLE(pDefDoc->pCurrentMatDef->hDefaultDiffuse)) {
			for(i=eaSize(&pDefDoc->eaDefDiffuseTex)-1; i>=0; --i) {
				if (GET_REF(pDefDoc->pCurrentMatDef->hDefaultDiffuse) == pDefDoc->eaDefDiffuseTex[i]) {
					break;
				}
			}
			if (i < 0) {
				REMOVE_HANDLE(pDefDoc->pCurrentMatDef->hDefaultDiffuse);
			}
		}
		if (REF_STRING_FROM_HANDLE(pDefDoc->pCurrentMatDef->hDefaultMovable)) {
			for(i=eaSize(&pDefDoc->eaDefMovableTex)-1; i>=0; --i) {
				if (GET_REF(pDefDoc->pCurrentMatDef->hDefaultMovable) == pDefDoc->eaDefMovableTex[i]) {
					break;
				}
			}
			if (i < 0) {
				REMOVE_HANDLE(pDefDoc->pCurrentMatDef->hDefaultMovable);
			}
		}
	} else {
		eaClear(&pDefDoc->eaDefPatternTex);
		eaClear(&pDefDoc->eaDefDetailTex);
		eaClear(&pDefDoc->eaDefSpecularTex);
		eaClear(&pDefDoc->eaDefDiffuseTex);
		eaClear(&pDefDoc->eaDefMovableTex);
	}

	// Update Geo fields
	pDefDoc->pDefGeoNameField->pOld = pDefDoc->pOrigGeoDef;
	pDefDoc->pDefGeoNameField->pNew = pDefDoc->pCurrentGeoDef;
	MEFieldRefreshFromData(pDefDoc->pDefGeoNameField);

	pDefDoc->pDefGeoDispNameField->pOld = pDefDoc->pOrigGeoDef ? &pDefDoc->pOrigGeoDef->displayNameMsg : NULL;
	pDefDoc->pDefGeoDispNameField->pNew = &pDefDoc->pCurrentGeoDef->displayNameMsg;
	MEFieldRefreshFromData(pDefDoc->pDefGeoDispNameField);

	pDefDoc->pDefGeoScopeField->pOld = pDefDoc->pOrigGeoDef;
	pDefDoc->pDefGeoScopeField->pNew = pDefDoc->pCurrentGeoDef;
	MEFieldRefreshFromData(pDefDoc->pDefGeoScopeField);
	
	if (pDefDoc->pCurrentGeoDef) {
		ui_LabelSetText(pDefDoc->pDefGeoFileNameLabel, pDefDoc->pCurrentGeoDef->pcFileName);
	} else {
		ui_LabelSetText(pDefDoc->pDefGeoFileNameLabel, "");
	}

	// Model and geometry interact, so update both for old/new before refreshing either
	pDefDoc->pDefGeoModelField->pOld = pDefDoc->pOrigGeoDef;
	pDefDoc->pDefGeoModelField->pNew = pDefDoc->pCurrentGeoDef;
	pDefDoc->pDefGeoGeometryField->pOld = pDefDoc->pOrigGeoDef;
	pDefDoc->pDefGeoGeometryField->pNew = pDefDoc->pCurrentGeoDef;
	MEFieldRefreshFromData(pDefDoc->pDefGeoGeometryField);
	MEFieldRefreshFromData(pDefDoc->pDefGeoModelField);

	pDefDoc->pDefGeoMirrorField->pOld = pDefDoc->pOrigGeoDef;
	pDefDoc->pDefGeoMirrorField->pNew = pDefDoc->pCurrentGeoDef;
	MEFieldRefreshFromData(pDefDoc->pDefGeoMirrorField);

	pDefDoc->pDefGeoMatField->pOld = pDefDoc->pOrigGeoDef;
	pDefDoc->pDefGeoMatField->pNew = pDefDoc->pCurrentGeoDef;
	MEFieldRefreshFromData(pDefDoc->pDefGeoMatField);

	pDefDoc->pDefGeoRandomWeightField->pOld = pDefDoc->pOrigGeoDef;
	pDefDoc->pDefGeoRandomWeightField->pNew = pDefDoc->pCurrentGeoDef;
	MEFieldRefreshFromData(pDefDoc->pDefGeoRandomWeightField);

	pDefDoc->pDefGeoOrderField->pOld = pDefDoc->pOrigGeoDef;
	pDefDoc->pDefGeoOrderField->pNew = pDefDoc->pCurrentGeoDef;
	MEFieldRefreshFromData(pDefDoc->pDefGeoOrderField);

	pDefDoc->pDefGeoStyleField->pOld = pDefDoc->pOrigGeoDef;
	pDefDoc->pDefGeoStyleField->pNew = pDefDoc->pCurrentGeoDef;
	MEFieldRefreshFromData(pDefDoc->pDefGeoStyleField);

	pDefDoc->pDefGeoCostumeGroupsField->pOld = pDefDoc->pOrigGeoDef;
	pDefDoc->pDefGeoCostumeGroupsField->pNew = pDefDoc->pCurrentGeoDef;
	MEFieldRefreshFromData(pDefDoc->pDefGeoCostumeGroupsField);

	pDefDoc->pDefGeoColorRestrictionField->pOld = pDefDoc->pOrigGeoDef;
	pDefDoc->pDefGeoColorRestrictionField->pNew = pDefDoc->pCurrentGeoDef;
	MEFieldRefreshFromData(pDefDoc->pDefGeoColorRestrictionField);

	pDefDoc->pDefGeoLODField->pOld = pDefDoc->pOrigGeoDef;
	pDefDoc->pDefGeoLODField->pNew = pDefDoc->pCurrentGeoDef;
	MEFieldRefreshFromData(pDefDoc->pDefGeoLODField);

	pDefDoc->pDefGeoRestrictionField->pOld = pDefDoc->pOrigGeoDef;
	pDefDoc->pDefGeoRestrictionField->pNew = pDefDoc->pCurrentGeoDef;
	MEFieldRefreshFromData(pDefDoc->pDefGeoRestrictionField);
	
	if (eaSize(&pDefDoc->eaDefMirrorGeos)) {
		ui_SetActive(pDefDoc->pDefGeoMirrorField->pUIWidget, true);
	} else {
		ui_SetActive(pDefDoc->pDefGeoMirrorField->pUIWidget, false);
	}

	if (eaSize(&pDefDoc->eaDefMats)) {
		ui_SetActive(pDefDoc->pDefGeoMatField->pUIWidget, true);
	} else {
		ui_SetActive(pDefDoc->pDefGeoMatField->pUIWidget, false);
	}

	if (pDefDoc->pDefCatList) {
		int *eaiRows = NULL;

		// Set category selection list
		for(i=eaSize(&pDefDoc->pCurrentGeoDef->eaCategories)-1; i>=0; --i) {
			for(j=eaSize(&pDefDoc->eaCategories)-1; j>=0; --j) {
				if (GET_REF(pDefDoc->pCurrentGeoDef->eaCategories[i]->hCategory) == pDefDoc->eaCategories[j]) {
					eaiPush(&eaiRows, j);
				}
			}
		}
		ui_ListSetSelectedRows(pDefDoc->pDefCatList, &eaiRows);
		eaiDestroy(&eaiRows);
	}
	
	// Update dynamic part of geometry UI
	costumeDefEdit_UpdateGeoAdvancedOptions(pDefDoc);
	costumeDefEdit_UpdateGeoOptionsOptions(pDefDoc);
	costumeDefEdit_UpdateGeoFx(pDefDoc);
	costumeDefEdit_UpdateGeoFxSwaps(pDefDoc);

	// Update Mat fields
	pDefDoc->pDefMatNameField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatNameField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatNameField);

	pDefDoc->pDefMatDispNameField->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->pOrigMatDef->displayNameMsg : NULL;
	pDefDoc->pDefMatDispNameField->pNew = &pDefDoc->pCurrentMatDef->displayNameMsg;
	MEFieldRefreshFromData(pDefDoc->pDefMatDispNameField);

	pDefDoc->pDefMatScopeField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatScopeField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatScopeField);

	if (pDefDoc->pCurrentMatDef) {
		ui_LabelSetText(pDefDoc->pDefMatFileNameLabel, pDefDoc->pCurrentMatDef->pcFileName);
	} else {
		ui_LabelSetText(pDefDoc->pDefMatFileNameLabel, "");
	}

	pDefDoc->pDefMatMaterialField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatMaterialField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatMaterialField);

	pDefDoc->pDefMatSkinField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatSkinField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatSkinField);

	pDefDoc->pDefMatPatternField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatPatternField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatPatternField);

	pDefDoc->pDefMatDetailField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatDetailField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatDetailField);

	pDefDoc->pDefMatSpecularField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatSpecularField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatSpecularField);

	pDefDoc->pDefMatDiffuseField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatDiffuseField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatDiffuseField);

	pDefDoc->pDefMatMovableField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatMovableField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatMovableField);

	pDefDoc->pDefMatReqPatternField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatReqPatternField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatReqPatternField);

	pDefDoc->pDefMatReqDetailField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatReqDetailField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatReqDetailField);

	pDefDoc->pDefMatReqSpecularField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatReqSpecularField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatReqSpecularField);

	pDefDoc->pDefMatReqDiffuseField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatReqDiffuseField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatReqDiffuseField);

	pDefDoc->pDefMatReqMovableField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatReqMovableField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatReqMovableField);

	pDefDoc->pDefMatRandomWeightField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatRandomWeightField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatRandomWeightField);

	pDefDoc->pDefMatOrderField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatOrderField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatOrderField);

	pDefDoc->pDefMatColorRestrictionField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatColorRestrictionField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatColorRestrictionField);

	pDefDoc->pDefMatRestrictionField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatRestrictionField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatRestrictionField);

	pDefDoc->pDefMatCostumeGroupsField->pOld = pDefDoc->pOrigMatDef;
	pDefDoc->pDefMatCostumeGroupsField->pNew = pDefDoc->pCurrentMatDef;
	MEFieldRefreshFromData(pDefDoc->pDefMatCostumeGroupsField);

	if (pDefDoc->pCurrentMatDef && pDefDoc->pCurrentMatDef->pcName && !pDefDoc->pCurrentMatDef->pColorOptions->bCustomReflection) {
		Vec4 value;
		if (pMaterial && materialGetNamedConstantValue(pMaterial, "ReflectionWeight", value)) {
			pDefDoc->currentDefaults.fDefaultReflect0 = (value[0] + 1.0) * 50.0;
			pDefDoc->currentDefaults.fDefaultReflect1 = (value[1] + 1.0) * 50.0;
			pDefDoc->currentDefaults.fDefaultReflect2 = (value[2] + 1.0) * 50.0;
			pDefDoc->currentDefaults.fDefaultReflect3 = (value[3] + 1.0) * 50.0;
		} else {
			pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[0] = 0;
			pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[1] = 0;
			pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[2] = 0;
			pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[3] = 0;
			pDefDoc->currentDefaults.fDefaultReflect0 = 0;
			pDefDoc->currentDefaults.fDefaultReflect1 = 0;
			pDefDoc->currentDefaults.fDefaultReflect2 = 0;
			pDefDoc->currentDefaults.fDefaultReflect3 = 0;
		}
	}

	if (pDefDoc->pCurrentMatDef && pDefDoc->pCurrentMatDef->pcName && !pDefDoc->pCurrentMatDef->pColorOptions->bCustomSpecularity) {
		Vec4 value;
		if (pMaterial && materialGetNamedConstantValue(pMaterial, "SpecularWeight", value)) {
			pDefDoc->currentDefaults.fDefaultSpecular0 = (value[0] + 1.0) * 50.0;
			pDefDoc->currentDefaults.fDefaultSpecular1 = (value[1] + 1.0) * 50.0;
			pDefDoc->currentDefaults.fDefaultSpecular2 = (value[2] + 1.0) * 50.0;
			pDefDoc->currentDefaults.fDefaultSpecular3 = (value[3] + 1.0) * 50.0;
		} else {
			pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[0] = 0;
			pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[1] = 0;
			pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[2] = 0;
			pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[3] = 0;
			pDefDoc->currentDefaults.fDefaultSpecular0 = 0;
			pDefDoc->currentDefaults.fDefaultSpecular1 = 0;
			pDefDoc->currentDefaults.fDefaultSpecular2 = 0;
			pDefDoc->currentDefaults.fDefaultSpecular3 = 0;
		}
	}

	pDefDoc->pAllowGlow0Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pAllowGlow1Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pAllowGlow2Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pAllowGlow3Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	MEFieldRefreshFromData(pDefDoc->pAllowGlow0Field);
	MEFieldRefreshFromData(pDefDoc->pAllowGlow1Field);
	MEFieldRefreshFromData(pDefDoc->pAllowGlow2Field);
	MEFieldRefreshFromData(pDefDoc->pAllowGlow3Field);

	pDefDoc->pCustomizeReflectField->pOld = SAFE_MEMBER(pDefDoc->pOrigMatDef, pColorOptions);
	pDefDoc->pCustomizeReflectField->pNew = pDefDoc->pCurrentMatDef->pColorOptions;
	MEFieldRefreshFromData(pDefDoc->pCustomizeReflectField);

	pDefDoc->pAllowReflect0Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pAllowReflect1Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pAllowReflect2Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pAllowReflect3Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	MEFieldRefreshFromData(pDefDoc->pAllowReflect0Field);
	MEFieldRefreshFromData(pDefDoc->pAllowReflect1Field);
	MEFieldRefreshFromData(pDefDoc->pAllowReflect2Field);
	MEFieldRefreshFromData(pDefDoc->pAllowReflect3Field);
	pDefDoc->pDefaultReflect0Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pDefaultReflect1Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pDefaultReflect2Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pDefaultReflect3Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	MEFieldRefreshFromData(pDefDoc->pDefaultReflect0Field);
	MEFieldRefreshFromData(pDefDoc->pDefaultReflect1Field);
	MEFieldRefreshFromData(pDefDoc->pDefaultReflect2Field);
	MEFieldRefreshFromData(pDefDoc->pDefaultReflect3Field);

	pDefDoc->pCustomizeSpecularField->pOld = SAFE_MEMBER(pDefDoc->pOrigMatDef, pColorOptions);
	pDefDoc->pCustomizeSpecularField->pNew = pDefDoc->pCurrentMatDef->pColorOptions;
	MEFieldRefreshFromData(pDefDoc->pCustomizeSpecularField);

	pDefDoc->pAllowSpecular0Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pAllowSpecular1Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pAllowSpecular2Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pAllowSpecular3Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	MEFieldRefreshFromData(pDefDoc->pAllowSpecular0Field);
	MEFieldRefreshFromData(pDefDoc->pAllowSpecular1Field);
	MEFieldRefreshFromData(pDefDoc->pAllowSpecular2Field);
	MEFieldRefreshFromData(pDefDoc->pAllowSpecular3Field);
	pDefDoc->pDefaultSpecular0Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pDefaultSpecular1Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pDefaultSpecular2Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pDefaultSpecular3Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	MEFieldRefreshFromData(pDefDoc->pDefaultSpecular0Field);
	MEFieldRefreshFromData(pDefDoc->pDefaultSpecular1Field);
	MEFieldRefreshFromData(pDefDoc->pDefaultSpecular2Field);
	MEFieldRefreshFromData(pDefDoc->pDefaultSpecular3Field);

	pDefDoc->pCustomMuscle0Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pCustomMuscle1Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pCustomMuscle2Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	pDefDoc->pCustomMuscle3Field->pOld = pDefDoc->pOrigMatDef ? &pDefDoc->origDefaults : NULL;
	MEFieldRefreshFromData(pDefDoc->pCustomMuscle0Field);
	MEFieldRefreshFromData(pDefDoc->pCustomMuscle1Field);
	MEFieldRefreshFromData(pDefDoc->pCustomMuscle2Field);
	MEFieldRefreshFromData(pDefDoc->pCustomMuscle3Field);

	if (pMaterial && materialHasNamedConstant(pMaterial, "ReflectionWeight")) {
		// Do nothing if UI is already set up
		if (!pDefDoc->pAllowReflect0Field->pUIWidget->group) {
			// Put in reflection controls
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pCustomizeReflectField->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pAllowReflect0Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pAllowReflect1Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pAllowReflect2Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pAllowReflect3Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pDefaultReflect0Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pDefaultReflect1Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pDefaultReflect2Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pDefaultReflect3Field->pUIWidget);

			// Remove label
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pReflectExpander), UI_WIDGET(pDefDoc->pReflectLabel));
		}
	} else {
		if (pDefDoc->pAllowReflect0Field->pUIWidget->group) {
			// Remove reflection controls
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pCustomizeReflectField->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pAllowReflect0Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pAllowReflect1Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pAllowReflect2Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pAllowReflect3Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pDefaultReflect0Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pDefaultReflect1Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pDefaultReflect2Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pReflectExpander), pDefDoc->pDefaultReflect3Field->pUIWidget);

			// Add label
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pReflectExpander), UI_WIDGET(pDefDoc->pReflectLabel));
		}
	}

	if (pMaterial && materialHasNamedConstant(pMaterial, "SpecularWeight")) {
		// Do nothing if UI is already set up
		if (!pDefDoc->pAllowSpecular0Field->pUIWidget->group) {
			// Put in reflection controls
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pCustomizeSpecularField->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pAllowSpecular0Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pAllowSpecular1Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pAllowSpecular2Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pAllowSpecular3Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pDefaultSpecular0Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pDefaultSpecular1Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pDefaultSpecular2Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pDefaultSpecular3Field->pUIWidget);

			// Remove label
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pSpecularExpander), UI_WIDGET(pDefDoc->pSpecularLabel));
		}
	} else {
		if (pDefDoc->pAllowSpecular0Field->pUIWidget->group) {
			// Remove reflection controls
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pCustomizeSpecularField->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pAllowSpecular0Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pAllowSpecular1Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pAllowSpecular2Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pAllowSpecular3Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pDefaultSpecular0Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pDefaultSpecular1Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pDefaultSpecular2Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(pDefDoc->pSpecularExpander), pDefDoc->pDefaultSpecular3Field->pUIWidget);

			// Add label
			ui_WidgetAddChild(UI_WIDGET(pDefDoc->pSpecularExpander), UI_WIDGET(pDefDoc->pSpecularLabel));
		}
	}

	if (eaSize(&pDefDoc->eaDefPatternTex)) {
		ui_SetActive(pDefDoc->pDefMatPatternField->pUIWidget, true);
	} else {
		ui_SetActive(pDefDoc->pDefMatPatternField->pUIWidget, false);
	}
	if (eaSize(&pDefDoc->eaDefDetailTex)) {
		ui_SetActive(pDefDoc->pDefMatDetailField->pUIWidget, true);
	} else {
		ui_SetActive(pDefDoc->pDefMatDetailField->pUIWidget, false);
	}
	if (eaSize(&pDefDoc->eaDefSpecularTex)) {
		ui_SetActive(pDefDoc->pDefMatSpecularField->pUIWidget, true);
	} else {
		ui_SetActive(pDefDoc->pDefMatSpecularField->pUIWidget, false);
	}
	if (eaSize(&pDefDoc->eaDefDiffuseTex)) {
		ui_SetActive(pDefDoc->pDefMatDiffuseField->pUIWidget, true);
	} else {
		ui_SetActive(pDefDoc->pDefMatDiffuseField->pUIWidget, false);
	}
	if (eaSize(&pDefDoc->eaDefMovableTex)) {
		ui_SetActive(pDefDoc->pDefMatMovableField->pUIWidget, true);
	} else {
		ui_SetActive(pDefDoc->pDefMatMovableField->pUIWidget, false);
	}
	
	// Update dynamic part of material UI
	costumeDefEdit_UpdateMaterialConstants(pDefDoc);
	costumeDefEdit_UpdateMaterialFxSwaps(pDefDoc);

	// Update Tex fields
	pDefDoc->pDefTexNameField->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexNameField->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexNameField);

	pDefDoc->pDefTexDispNameField->pOld = pDefDoc->pOrigTexDef ? &pDefDoc->pOrigTexDef->displayNameMsg : NULL;
	pDefDoc->pDefTexDispNameField->pNew = &pDefDoc->pCurrentTexDef->displayNameMsg;
	MEFieldRefreshFromData(pDefDoc->pDefTexDispNameField);

	pDefDoc->pDefTexScopeField->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexScopeField->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexScopeField);

	if (pDefDoc->pCurrentTexDef) {
		ui_LabelSetText(pDefDoc->pDefTexFileNameLabel, pDefDoc->pCurrentTexDef->pcFileName);
	} else {
		ui_LabelSetText(pDefDoc->pDefTexFileNameLabel, "");
	}

	pDefDoc->pDefTexOldField->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexOldField->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexOldField);

	pDefDoc->pDefTexNewField->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexNewField->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexNewField);

	pDefDoc->pDefTexSkinField->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexSkinField->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexSkinField);

	pDefDoc->pDefTexMovMinXField->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pMovableOptions);
	pDefDoc->pDefTexMovMinXField->pNew = pDefDoc->pCurrentTexDef->pMovableOptions;
	MEFieldRefreshFromData(pDefDoc->pDefTexMovMinXField);

	pDefDoc->pDefTexMovMaxXField->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pMovableOptions);
	pDefDoc->pDefTexMovMaxXField->pNew = pDefDoc->pCurrentTexDef->pMovableOptions;
	MEFieldRefreshFromData(pDefDoc->pDefTexMovMaxXField);

	pDefDoc->pDefTexMovDefaultXField->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pMovableOptions);
	pDefDoc->pDefTexMovDefaultXField->pNew = pDefDoc->pCurrentTexDef->pMovableOptions;
	MEFieldRefreshFromData(pDefDoc->pDefTexMovDefaultXField);

	pDefDoc->pDefTexMovMinYField->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pMovableOptions);
	pDefDoc->pDefTexMovMinYField->pNew = pDefDoc->pCurrentTexDef->pMovableOptions;
	MEFieldRefreshFromData(pDefDoc->pDefTexMovMinYField);

	pDefDoc->pDefTexMovMaxYField->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pMovableOptions);
	pDefDoc->pDefTexMovMaxYField->pNew = pDefDoc->pCurrentTexDef->pMovableOptions;
	MEFieldRefreshFromData(pDefDoc->pDefTexMovMaxYField);

	pDefDoc->pDefTexMovDefaultYField->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pMovableOptions);
	pDefDoc->pDefTexMovDefaultYField->pNew = pDefDoc->pCurrentTexDef->pMovableOptions;
	MEFieldRefreshFromData(pDefDoc->pDefTexMovDefaultYField);

	pDefDoc->pDefTexMovMinScaleXField->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pMovableOptions);
	pDefDoc->pDefTexMovMinScaleXField->pNew = pDefDoc->pCurrentTexDef->pMovableOptions;
	MEFieldRefreshFromData(pDefDoc->pDefTexMovMinScaleXField);

	pDefDoc->pDefTexMovMaxScaleXField->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pMovableOptions);
	pDefDoc->pDefTexMovMaxScaleXField->pNew = pDefDoc->pCurrentTexDef->pMovableOptions;
	MEFieldRefreshFromData(pDefDoc->pDefTexMovMaxScaleXField);

	pDefDoc->pDefTexMovDefaultScaleXField->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pMovableOptions);
	pDefDoc->pDefTexMovDefaultScaleXField->pNew = pDefDoc->pCurrentTexDef->pMovableOptions;
	MEFieldRefreshFromData(pDefDoc->pDefTexMovDefaultScaleXField);

	pDefDoc->pDefTexMovMinScaleYField->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pMovableOptions);
	pDefDoc->pDefTexMovMinScaleYField->pNew = pDefDoc->pCurrentTexDef->pMovableOptions;
	MEFieldRefreshFromData(pDefDoc->pDefTexMovMinScaleYField);

	pDefDoc->pDefTexMovMaxScaleYField->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pMovableOptions);
	pDefDoc->pDefTexMovMaxScaleYField->pNew = pDefDoc->pCurrentTexDef->pMovableOptions;
	MEFieldRefreshFromData(pDefDoc->pDefTexMovMaxScaleYField);

	pDefDoc->pDefTexMovDefaultScaleYField->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pMovableOptions);
	pDefDoc->pDefTexMovDefaultScaleYField->pNew = pDefDoc->pCurrentTexDef->pMovableOptions;
	MEFieldRefreshFromData(pDefDoc->pDefTexMovDefaultScaleYField);

	pDefDoc->pDefTexMovDefaultRotField->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pMovableOptions);
	pDefDoc->pDefTexMovDefaultRotField->pNew = pDefDoc->pCurrentTexDef->pMovableOptions;
	MEFieldRefreshFromData(pDefDoc->pDefTexMovDefaultRotField);

	pDefDoc->pDefTexMovCanEditPosField->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pMovableOptions);
	pDefDoc->pDefTexMovCanEditPosField->pNew = pDefDoc->pCurrentTexDef->pMovableOptions;
	MEFieldRefreshFromData(pDefDoc->pDefTexMovCanEditPosField);

	pDefDoc->pDefTexMovCanEditRotField->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pMovableOptions);
	pDefDoc->pDefTexMovCanEditRotField->pNew = pDefDoc->pCurrentTexDef->pMovableOptions;
	MEFieldRefreshFromData(pDefDoc->pDefTexMovCanEditRotField);

	pDefDoc->pDefTexMovCanEditScaleField->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pMovableOptions);
	pDefDoc->pDefTexMovCanEditScaleField->pNew = pDefDoc->pCurrentTexDef->pMovableOptions;
	MEFieldRefreshFromData(pDefDoc->pDefTexMovCanEditScaleField);

	pDefDoc->pDefTexWordsKeyField->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexWordsKeyField->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexWordsKeyField);

	pDefDoc->pDefTexWordsCapsField->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexWordsCapsField->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexWordsCapsField);

	pDefDoc->pDefTexTypeField->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexTypeField->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexTypeField);

	pDefDoc->pDefMaterialConstantName->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pValueOptions);
	pDefDoc->pDefMaterialConstantName->pNew = pDefDoc->pCurrentTexDef->pValueOptions;
	MEFieldRefreshFromData(pDefDoc->pDefMaterialConstantName);

	pDefDoc->pDefMaterialConstantIndex->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pValueOptions);
	pDefDoc->pDefMaterialConstantIndex->pNew = pDefDoc->pCurrentTexDef->pValueOptions;
	MEFieldRefreshFromData(pDefDoc->pDefMaterialConstantIndex);

	pDefDoc->pDefMaterialConstantDefault->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pValueOptions);
	pDefDoc->pDefMaterialConstantDefault->pNew = pDefDoc->pCurrentTexDef->pValueOptions;
	MEFieldRefreshFromData(pDefDoc->pDefMaterialConstantDefault);

	pDefDoc->pDefMaterialConstantMin->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pValueOptions);
	pDefDoc->pDefMaterialConstantMin->pNew = pDefDoc->pCurrentTexDef->pValueOptions;
	MEFieldRefreshFromData(pDefDoc->pDefMaterialConstantMin);

	pDefDoc->pDefMaterialConstantMax->pOld = SAFE_MEMBER(pDefDoc->pOrigTexDef, pValueOptions);
	pDefDoc->pDefMaterialConstantMax->pNew = pDefDoc->pCurrentTexDef->pValueOptions;
	MEFieldRefreshFromData(pDefDoc->pDefMaterialConstantMax);

	pDefDoc->pDefTexRandomWeightField->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexRandomWeightField->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexRandomWeightField);

	pDefDoc->pDefTexOrderField->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexOrderField->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexOrderField);

	pDefDoc->pDefTexColorRestrictionField->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexColorRestrictionField->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexColorRestrictionField);

	pDefDoc->pDefTexColorSwap0Field->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexColorSwap0Field->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexColorSwap0Field);

	pDefDoc->pDefTexColorSwap1Field->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexColorSwap1Field->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexColorSwap1Field);

	pDefDoc->pDefTexColorSwap2Field->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexColorSwap2Field->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexColorSwap2Field);

	pDefDoc->pDefTexColorSwap3Field->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexColorSwap3Field->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexColorSwap3Field);

	pDefDoc->pDefTexRestrictionField->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexRestrictionField->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexRestrictionField);

	pDefDoc->pDefTexCostumeGroupsField->pOld = pDefDoc->pOrigTexDef;
	pDefDoc->pDefTexCostumeGroupsField->pNew = pDefDoc->pCurrentTexDef;
	MEFieldRefreshFromData(pDefDoc->pDefTexCostumeGroupsField);

	ui_SpriteSetTexture(pDefDoc->pDefTexSprite, pDefDoc->pCurrentTexDef->pcNewTexture ? pDefDoc->pCurrentTexDef->pcNewTexture : "");

	// Update dynamic part of the texture UI
	costumeDefEdit_UpdateExtraTextures(pDefDoc);

	// Also update def button states
	costumeDefEdit_DefUpdateState(pDefDoc);

	pDefDoc->bIgnoreDefFieldChanges = false;
}


void costumeDefEdit_DefUpdateState(CostumeEditDefDoc *pDefDoc)
{
	// Update names for gimme icons
	ui_GimmeButtonSetName(pDefDoc->pGeoGimmeButton, pDefDoc->pCurrentGeoDef ? pDefDoc->pCurrentGeoDef->pcName : NULL);
	ui_GimmeButtonSetReferent(pDefDoc->pGeoGimmeButton, pDefDoc->pCurrentGeoDef);
	ui_GimmeButtonSetName(pDefDoc->pMatGimmeButton, pDefDoc->pCurrentMatDef ? pDefDoc->pCurrentMatDef->pcName : NULL);
	ui_GimmeButtonSetReferent(pDefDoc->pMatGimmeButton, pDefDoc->pCurrentMatDef);
	ui_GimmeButtonSetName(pDefDoc->pTexGimmeButton, pDefDoc->pCurrentTexDef ? pDefDoc->pCurrentTexDef->pcName : NULL);
	ui_GimmeButtonSetReferent(pDefDoc->pTexGimmeButton, pDefDoc->pCurrentTexDef);

	assert(pDefDoc->pCurrentGeoDef);

	// Update Geo button states
	if (!pDefDoc->pOrigGeoDef && !pDefDoc->pCurrentGeoDef->pcName && !pDefDoc->pCurrentGeoDef->pcFileName) {
		// Not editing state
		ui_ButtonSetText(pDefDoc->pGeoRevertButton, "Revert");
		ui_SetActive(UI_WIDGET(pDefDoc->pGeoNewButton), true);
		ui_SetActive(UI_WIDGET(pDefDoc->pGeoRevertButton), false);
		ui_SetActive(UI_WIDGET(pDefDoc->pGeoSaveButton), false);
		ui_SetActive(UI_WIDGET(pDefDoc->pDefCatList), false);

		ui_SetActive(pDefDoc->pDefGeoNameField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefGeoDispNameField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefGeoScopeField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefGeoGeometryField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefGeoModelField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefGeoMirrorField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefGeoMatField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefGeoRandomWeightField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefGeoOrderField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefGeoStyleField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefGeoCostumeGroupsField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefGeoColorRestrictionField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefGeoLODField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefGeoRestrictionField->pUIWidget, false);
	} else {
		ui_SetActive(UI_WIDGET(pDefDoc->pDefCatList), true);
		if (!pDefDoc->pOrigGeoDef) {
			// New state
			ui_ButtonSetText(pDefDoc->pGeoRevertButton, "Discard");
			ui_SetActive(UI_WIDGET(pDefDoc->pGeoNewButton), false);
			ui_SetActive(UI_WIDGET(pDefDoc->pGeoRevertButton), true);
			ui_SetActive(UI_WIDGET(pDefDoc->pGeoSaveButton), true);
		} else {
			// Editing state
			bool bDirty = StructCompare(parse_PCGeometryDef, pDefDoc->pOrigGeoDef, pDefDoc->pCurrentGeoDef, 0, 0, 0);
			ui_ButtonSetText(pDefDoc->pGeoRevertButton, "Revert");
			ui_SetActive(UI_WIDGET(pDefDoc->pGeoNewButton), true);
			ui_SetActive(UI_WIDGET(pDefDoc->pGeoRevertButton), bDirty);
			ui_SetActive(UI_WIDGET(pDefDoc->pGeoSaveButton), bDirty);
		}

		ui_SetActive(pDefDoc->pDefGeoNameField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefGeoDispNameField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefGeoScopeField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefGeoGeometryField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefGeoModelField->pUIWidget, (eaSize(&pDefDoc->eaModelNames) > 0));
		ui_SetActive(pDefDoc->pDefGeoMirrorField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefGeoMatField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefGeoRandomWeightField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefGeoOrderField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefGeoStyleField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefGeoCostumeGroupsField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefGeoColorRestrictionField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefGeoLODField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefGeoRestrictionField->pUIWidget, true);
		
		if (pDefDoc->pDefGeoClothBackField) {
			ui_SetActive(pDefDoc->pDefGeoClothBackField->pUIWidget, pDefDoc->pCurrentGeoDef->pClothData ? pDefDoc->pCurrentGeoDef->pClothData->bIsCloth : false);
		}
		if (pDefDoc->pDefGeoClothInfoField) {
			ui_SetActive(pDefDoc->pDefGeoClothInfoField->pUIWidget, pDefDoc->pCurrentGeoDef->pClothData ? pDefDoc->pCurrentGeoDef->pClothData->bIsCloth : false);
		}
		if (pDefDoc->pDefGeoClothCollisionField) {
			ui_SetActive(pDefDoc->pDefGeoClothCollisionField->pUIWidget, pDefDoc->pCurrentGeoDef->pClothData ? pDefDoc->pCurrentGeoDef->pClothData->bIsCloth : false);
		}
	}

	assert(pDefDoc->pCurrentMatDef);

	// Update Mat button states
	if (!pDefDoc->pOrigMatDef && !pDefDoc->pCurrentMatDef->pcName && !pDefDoc->pCurrentMatDef->pcFileName) {
		// Not editing state
		ui_ButtonSetText(pDefDoc->pMatRevertButton, "Revert");
		ui_SetActive(UI_WIDGET(pDefDoc->pMatNewButton), true);
		ui_SetActive(UI_WIDGET(pDefDoc->pMatRevertButton), false);
		ui_SetActive(UI_WIDGET(pDefDoc->pMatSaveButton), false);
	
		ui_SetActive(pDefDoc->pDefMatNameField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMatDispNameField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMatScopeField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMatMaterialField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMatSkinField->pUIWidget, false);

		ui_SetActive(pDefDoc->pAllowGlow0Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pAllowGlow1Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pAllowGlow2Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pAllowGlow3Field->pUIWidget, false);

		ui_SetActive(pDefDoc->pAllowReflect0Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pAllowReflect1Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pAllowReflect2Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pAllowReflect3Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefaultReflect0Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefaultReflect1Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefaultReflect2Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefaultReflect3Field->pUIWidget, false);

		ui_SetActive(pDefDoc->pAllowSpecular0Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pAllowSpecular1Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pAllowSpecular2Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pAllowSpecular3Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefaultSpecular0Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefaultSpecular1Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefaultSpecular2Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefaultSpecular3Field->pUIWidget, false);

		ui_SetActive(pDefDoc->pCustomMuscle0Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pCustomMuscle1Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pCustomMuscle2Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pCustomMuscle3Field->pUIWidget, false);

		ui_SetActive(pDefDoc->pDefMatRandomWeightField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMatOrderField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMatColorRestrictionField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMatRestrictionField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMatPatternField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMatDetailField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMatSpecularField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMatDiffuseField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMatMovableField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMatReqPatternField->pUIWidget, false);
		
		ui_SetActive(pDefDoc->pDefMatCostumeGroupsField->pUIWidget, false);
		
	} else {
		if (!pDefDoc->pOrigMatDef) {
			// New State
			ui_ButtonSetText(pDefDoc->pMatRevertButton, "Discard");
			ui_SetActive(UI_WIDGET(pDefDoc->pMatNewButton), false);
			ui_SetActive(UI_WIDGET(pDefDoc->pMatRevertButton), true);
			ui_SetActive(UI_WIDGET(pDefDoc->pMatSaveButton), true && (pDefDoc->pOrigGeoDef != NULL));
		} else {
			// Editing state
			bool bDirty = StructCompare(parse_PCMaterialDef, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, 0, 0, 0);
			ui_ButtonSetText(pDefDoc->pMatRevertButton, "Revert");
			ui_SetActive(UI_WIDGET(pDefDoc->pMatNewButton), true);
			ui_SetActive(UI_WIDGET(pDefDoc->pMatRevertButton), bDirty);
			ui_SetActive(UI_WIDGET(pDefDoc->pMatSaveButton), bDirty && (pDefDoc->pOrigGeoDef != NULL));
		}
	
		ui_SetActive(pDefDoc->pDefMatNameField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMatDispNameField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMatScopeField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMatMaterialField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMatSkinField->pUIWidget, true);

		ui_SetActive(pDefDoc->pAllowGlow0Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pAllowGlow1Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pAllowGlow2Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pAllowGlow3Field->pUIWidget, true);

		ui_SetActive(pDefDoc->pAllowReflect0Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pAllowReflect1Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pAllowReflect2Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pAllowReflect3Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefaultReflect0Field->pUIWidget, pDefDoc->pCurrentMatDef->pColorOptions && pDefDoc->pCurrentMatDef->pColorOptions->bCustomReflection);
		ui_SetActive(pDefDoc->pDefaultReflect1Field->pUIWidget, pDefDoc->pCurrentMatDef->pColorOptions && pDefDoc->pCurrentMatDef->pColorOptions->bCustomReflection);
		ui_SetActive(pDefDoc->pDefaultReflect2Field->pUIWidget, pDefDoc->pCurrentMatDef->pColorOptions && pDefDoc->pCurrentMatDef->pColorOptions->bCustomReflection);
		ui_SetActive(pDefDoc->pDefaultReflect3Field->pUIWidget, pDefDoc->pCurrentMatDef->pColorOptions && pDefDoc->pCurrentMatDef->pColorOptions->bCustomReflection);

		ui_SetActive(pDefDoc->pAllowSpecular0Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pAllowSpecular1Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pAllowSpecular2Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pAllowSpecular3Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefaultSpecular0Field->pUIWidget, pDefDoc->pCurrentMatDef->pColorOptions && pDefDoc->pCurrentMatDef->pColorOptions->bCustomSpecularity);
		ui_SetActive(pDefDoc->pDefaultSpecular1Field->pUIWidget, pDefDoc->pCurrentMatDef->pColorOptions && pDefDoc->pCurrentMatDef->pColorOptions->bCustomSpecularity);
		ui_SetActive(pDefDoc->pDefaultSpecular2Field->pUIWidget, pDefDoc->pCurrentMatDef->pColorOptions && pDefDoc->pCurrentMatDef->pColorOptions->bCustomSpecularity);
		ui_SetActive(pDefDoc->pDefaultSpecular3Field->pUIWidget, pDefDoc->pCurrentMatDef->pColorOptions && pDefDoc->pCurrentMatDef->pColorOptions->bCustomSpecularity);

		ui_SetActive(pDefDoc->pCustomMuscle0Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pCustomMuscle1Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pCustomMuscle2Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pCustomMuscle3Field->pUIWidget, true);

		ui_SetActive(pDefDoc->pDefMatRandomWeightField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMatOrderField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMatColorRestrictionField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMatRestrictionField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMatPatternField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMatDetailField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMatSpecularField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMatDiffuseField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMatMovableField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMatReqPatternField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMatCostumeGroupsField->pUIWidget, true);
	}

	assert(pDefDoc->pCurrentTexDef);

	// Update Tex button states
	if (!pDefDoc->pOrigTexDef && !pDefDoc->pCurrentTexDef->pcName && !pDefDoc->pCurrentTexDef->pcFileName) {
		// Not editing state
		ui_ButtonSetText(pDefDoc->pTexRevertButton, "Revert");
		ui_SetActive(UI_WIDGET(pDefDoc->pTexNewButton), true);
		ui_SetActive(UI_WIDGET(pDefDoc->pTexRevertButton), false);
		ui_SetActive(UI_WIDGET(pDefDoc->pTexSaveButton), false);

		ui_SetActive(pDefDoc->pDefTexNameField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexDispNameField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexScopeField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexOldField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexNewField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexSkinField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexTypeField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexMovMinXField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexMovMaxXField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexMovDefaultXField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexMovMinYField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexMovMaxYField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexMovDefaultYField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexMovMinScaleXField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexMovMaxScaleXField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexMovDefaultScaleXField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexMovMinScaleYField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexMovMaxScaleYField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexMovDefaultScaleYField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexMovDefaultRotField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexMovCanEditPosField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexMovCanEditRotField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexMovCanEditScaleField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexWordsKeyField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexWordsCapsField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMaterialConstantName->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMaterialConstantIndex->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMaterialConstantDefault->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMaterialConstantMin->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefMaterialConstantMax->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexRandomWeightField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexOrderField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexColorRestrictionField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexColorSwap0Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexColorSwap1Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexColorSwap2Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexColorSwap3Field->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexRestrictionField->pUIWidget, false);
		ui_SetActive(pDefDoc->pDefTexCostumeGroupsField->pUIWidget, false);
	} else {
		if (!pDefDoc->pOrigTexDef) {
			// New state
			ui_ButtonSetText(pDefDoc->pTexRevertButton, "Discard");
			ui_SetActive(UI_WIDGET(pDefDoc->pTexNewButton), false);
			ui_SetActive(UI_WIDGET(pDefDoc->pTexRevertButton), true);
			ui_SetActive(UI_WIDGET(pDefDoc->pTexSaveButton), (pDefDoc->pOrigMatDef != NULL));
			ui_SetActive(pDefDoc->pDefTexNameField->pUIWidget, true);
		} else {
			// Editing state
			bool bDirty = StructCompare(parse_PCTextureDef, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, 0, 0, 0);
			ui_ButtonSetText(pDefDoc->pTexRevertButton, "Revert");
			ui_SetActive(UI_WIDGET(pDefDoc->pTexNewButton), true);
			ui_SetActive(UI_WIDGET(pDefDoc->pTexRevertButton), bDirty);
			ui_SetActive(UI_WIDGET(pDefDoc->pTexSaveButton), bDirty && (pDefDoc->pOrigMatDef != NULL));
			ui_SetActive(pDefDoc->pDefTexNameField->pUIWidget, true);
		}

		ui_SetActive(pDefDoc->pDefTexDispNameField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexScopeField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexOldField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexNewField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexSkinField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexTypeField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexMovMinXField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexMovMaxXField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexMovDefaultXField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexMovMinYField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexMovMaxYField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexMovDefaultYField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexMovMinScaleXField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexMovMaxScaleXField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexMovDefaultScaleXField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexMovMinScaleYField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexMovMaxScaleYField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexMovDefaultScaleYField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexMovDefaultRotField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexMovCanEditPosField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexMovCanEditRotField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexMovCanEditScaleField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexWordsKeyField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexWordsCapsField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMaterialConstantName->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMaterialConstantIndex->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMaterialConstantDefault->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMaterialConstantMin->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefMaterialConstantMax->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexRandomWeightField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexOrderField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexColorRestrictionField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexColorSwap0Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexColorSwap1Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexColorSwap2Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexColorSwap3Field->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexRestrictionField->pUIWidget, true);
		ui_SetActive(pDefDoc->pDefTexCostumeGroupsField->pUIWidget, true);
	}

	if (pDefDoc->pCurrentTexDef->eTypeFlags == kPCTextureType_Movable)
	{
		if (!gMovConstExpVisible)
		{
			ui_ExpanderGroupInsertExpander(pDefDoc->pTextureExpanderGroup, pDefDoc->pMovableConstantsExpander, 1);
			gMovConstExpVisible = true;
		}
	}
	else
	{
		if (gMovConstExpVisible)
		{
			ui_ExpanderGroupRemoveExpander(pDefDoc->pTextureExpanderGroup, pDefDoc->pMovableConstantsExpander);
			gMovConstExpVisible = false;
		}
	}
}


void costumeDefEdit_ExpandChanged(UIExpander *pExpander, char *pcExpanderName)
{
	EditorPrefStoreInt(COSTUME_EDITOR, "Expander", pcExpanderName, ui_ExpanderIsOpened(pExpander));
}


static void costumeDefEdit_GetValidModels(const char *pcGeoFile, const char ***peaModelNames)
{
	ModelHeaderSet *pSet;

	// Load the header for the geometry (faster than full load)
	eaClear(peaModelNames);
	pSet = modelHeaderSetFind(pcGeoFile);
	if (pSet) {
		FOR_EACH_IN_EARRAY(pSet->model_headers, ModelHeader, model_header)
		{
			// This used to only be grabbing models with an lod_info?!
			eaPush(peaModelNames, model_header->modelname);
		}
		FOR_EACH_END;
	}
}


static FileScanAction costumeDefEdit_GeoFileScanAction(char *dir, struct _finddata32_t *data, void *pUserData)
{
	static char *ext = ".ModelHeader";
	static int ext_len = 12; // strlen(ext);
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

	STR_COMBINE_SSS(filename, dir, "/", data->name);

	// Clip off the "ModelHeader" ending
	filename[strlen(filename)-ext_len] = '\0';

	// Store the geometry file name
	eaPush(&g_eaGeoFileNames,allocAddFilename(filename));

	return FSA_EXPLORE_DIRECTORY;
}


static void costumeDefEdit_GeoFileChangeCallback(const char *relpath, int when)
{
	CostumeEditDoc **eaDocs = NULL;

	// When a ModelHeader file changes, reload the list
	eaClear(&g_eaGeoFileNames);
	fileScanAllDataDirs("character_library", costumeDefEdit_GeoFileScanAction, NULL);

	// Have to regenerate the model pull-down list
	if (gDefDoc && gDefDoc->pCurrentGeoDef && gDefDoc->pCurrentGeoDef->pcGeometry) {
		costumeDefEdit_GetValidModels(gDefDoc->pCurrentGeoDef->pcGeometry, &gDefDoc->eaModelNames);
		costumeDefEdit_DefUpdateLists(gDefDoc);
	}
}


static void costumeDefEdit_HidePopup(CostumeEditDefDoc *pDefDoc)
{
	if (pDefDoc->pPopupWindow) {
		ui_WindowHide(pDefDoc->pPopupWindow);
		ui_WidgetQueueFreeAndNull(&pDefDoc->pPopupWindow);
	}
}


static void costumeDefEdit_MatFileScanCallback(void *userData, const char *material_name)
{
	eaPush(&g_eaMatFileNames, allocAddString(material_name));
}


static void costumeDefEdit_MatFileChangeCallback(const char *relpath, int when)
{
	// When a material file changes, reload the list
	eaClear(&g_eaMatFileNames);
	materialForEachMaterialName(costumeDefEdit_MatFileScanCallback, NULL);
}


static void costumeDefEdit_PromptForGeoAddSave(CostumeEditDefDoc *pDefDoc, PCGeometryAdd **eaAdds)
{
	UILabel *pLabel;
	UIButton *pButton;
	UITextEntry *pText;
	UIFileNameEntry *pFile;
	CostumeAddSaveData *pData;
	PCGeometryDef *pGeo;
	int i, y;

	pDefDoc->pPopupWindow = ui_WindowCreate("Choose Where to Save Added Materials",200,300,600,70);
	pDefDoc->pPopupWindow->widget.scale = emGetEditorScale(pDefDoc->pEditor);

	y = 0;

	pLabel = ui_LabelCreate("You want to save here if possible.", 0, y);
	ui_WindowAddChild(pDefDoc->pPopupWindow, pLabel);
	y+=28;

	pGeo = RefSystem_ReferentFromString(g_hCostumeGeometryDict, pDefDoc->pcGeoForMatsToAdd);
	if (pGeo) {
		pLabel = ui_LabelCreate("Geometry File", 0, y);
		ui_WindowAddChild(pDefDoc->pPopupWindow, pLabel);
		pText = ui_TextEntryCreate(pGeo->pcFileName, 100, y);
		ui_WidgetSetWidthEx(UI_WIDGET(pText), 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(UI_WIDGET(pText), 0, 100, 0, 0);
		ui_SetActive(UI_WIDGET(pText), false);
		ui_WindowAddChild(pDefDoc->pPopupWindow, pText);
		pData = (CostumeAddSaveData*)calloc(1, sizeof(CostumeAddSaveData));
		pData->pDefDoc = pDefDoc;
		pData->pcName = strdup(pGeo->pcName);
		pData->pcFileName = strdup(pGeo->pcFileName);
		pData->eSaveType = SaveType_GeoDef;
		pButton = ui_ButtonCreate("Save Here", 0, y, costumeDefEdit_UIGeoSaveButton, pData);
		ui_WidgetSetWidth(UI_WIDGET(pButton), 90);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, 0, 0, UITopRight);
		ui_WindowAddChild(pDefDoc->pPopupWindow, pButton);
		y+=28;
	}

	pLabel = ui_LabelCreate("If saving to the geometry file fails, save in one of these.", 0, y);
	ui_WindowAddChild(pDefDoc->pPopupWindow, pLabel);
	y+=28;

	for(i=0; i<eaSize(&eaAdds); ++i) {
		pLabel = ui_LabelCreate("Add File", 0, y);
		ui_WindowAddChild(pDefDoc->pPopupWindow, pLabel);
		pText = ui_TextEntryCreate(eaAdds[i]->pcFileName, 100, y);
		ui_WidgetSetWidthEx(UI_WIDGET(pText), 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(UI_WIDGET(pText), 0, 100, 0, 0);
		ui_SetActive(UI_WIDGET(pText), false);
		ui_WindowAddChild(pDefDoc->pPopupWindow, pText);
		pData = (CostumeAddSaveData*)calloc(1, sizeof(CostumeAddSaveData));
		pData->pDefDoc = pDefDoc;
		pData->pcName = strdup(eaAdds[i]->pcName);
		pData->pcFileName = strdup(eaAdds[i]->pcFileName);
		pData->eSaveType = SaveType_GeoAdd;
		pButton = ui_ButtonCreate("Save Here", 0, y, costumeDefEdit_UIGeoSaveButton, pData);
		ui_WidgetSetWidth(UI_WIDGET(pButton), 90);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, 0, 0, UITopRight);
		ui_WindowAddChild(pDefDoc->pPopupWindow, pButton);
		y+= 28;
	}

	pLabel = ui_LabelCreate("Other Add File", 0, y);
	ui_WindowAddChild(pDefDoc->pPopupWindow, pLabel);
	pFile = ui_FileNameEntryCreate(EditorPrefGetString(COSTUME_EDITOR, "FilePref", "GeoAdd", ""), NULL, "defs", "defs/costumes/Definitions", ".addgeo", UIBrowseNewOrExisting);
	ui_WidgetSetPosition(UI_WIDGET(pFile), 100, y);
	ui_WidgetSetWidthEx(UI_WIDGET(pFile), 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pFile), 0, 100, 0, 0);
	ui_WindowAddChild(pDefDoc->pPopupWindow, pFile);
	pDefDoc->pAddSaveFileEntry = pFile;
	pData = (CostumeAddSaveData*)calloc(1, sizeof(CostumeAddSaveData));
	pData->pDefDoc = pDefDoc;
	pData->pcName = NULL;
	pData->pcFileName = NULL;
	pData->eSaveType = SaveType_GeoAdd;
	pButton = ui_ButtonCreate("Save Here", 0, y, costumeDefEdit_UIGeoSaveButton, pData);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 90);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, 0, 0, UITopRight);
	ui_WindowAddChild(pDefDoc->pPopupWindow, pButton);
	y+=28;

	// Create the buttons
	pButton = ui_ButtonCreate("Cancel Save of Adds",0,y,costumeDefEdit_UIGeoSaveCancel,pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 140);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), -60, y, 0.5, 0, UITopLeft);
	ui_WindowAddChild(pDefDoc->pPopupWindow,pButton);
	y+=28;

	ui_WidgetSetHeight(UI_WIDGET(pDefDoc->pPopupWindow), y);

	// Show the window
	ui_WindowSetClosable(pDefDoc->pPopupWindow,0);
	ui_WindowPresent(pDefDoc->pPopupWindow);
}


static void costumeDefEdit_PromptForMatAddSave(CostumeEditDefDoc *pDefDoc, PCMaterialAdd **eaAdds)
{
	UILabel *pLabel;
	UIButton *pButton;
	UITextEntry *pText;
	UIFileNameEntry *pFile;
	CostumeAddSaveData *pData;
	PCMaterialDef *pMat;
	int i, y;

	pDefDoc->pPopupWindow = ui_WindowCreate("Choose Where to Save Added Textures",200,300,600,70);
	pDefDoc->pPopupWindow->widget.scale = emGetEditorScale(pDefDoc->pEditor);

	y = 0;

	pLabel = ui_LabelCreate("You want to save here if possible.", 0, y);
	ui_WindowAddChild(pDefDoc->pPopupWindow, pLabel);
	y+=28;

	pMat = RefSystem_ReferentFromString(g_hCostumeMaterialDict, pDefDoc->pcMatForTexsToAdd);
	if (pMat) {
		pLabel = ui_LabelCreate("Material File", 0, y);
		ui_WindowAddChild(pDefDoc->pPopupWindow, pLabel);
		pText = ui_TextEntryCreate(pMat->pcFileName, 100, y);
		ui_WidgetSetWidthEx(UI_WIDGET(pText), 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(UI_WIDGET(pText), 0, 100, 0, 0);
		ui_SetActive(UI_WIDGET(pText), false);
		ui_WindowAddChild(pDefDoc->pPopupWindow, pText);
		pData = (CostumeAddSaveData*)calloc(1, sizeof(CostumeAddSaveData));
		pData->pDefDoc = pDefDoc;
		pData->pcName = strdup(pMat->pcName);
		pData->pcFileName = strdup(pMat->pcFileName);
		pData->eSaveType = SaveType_MatDef;
		pButton = ui_ButtonCreate("Save Here", 0, y, costumeDefEdit_UIMatSaveButton, pData);
		ui_WidgetSetWidth(UI_WIDGET(pButton), 90);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, 0, 0, UITopRight);
		ui_WindowAddChild(pDefDoc->pPopupWindow, pButton);
		y+=28;
	}

	pLabel = ui_LabelCreate("If saving to the material file fails, save in one of these.", 0, y);
	ui_WindowAddChild(pDefDoc->pPopupWindow, pLabel);
	y+=28;

	for(i=0; i<eaSize(&eaAdds); ++i) {
		pLabel = ui_LabelCreate("Add File", 0, y);
		ui_WindowAddChild(pDefDoc->pPopupWindow, pLabel);
		pText = ui_TextEntryCreate(eaAdds[i]->pcFileName, 100, y);
		ui_WidgetSetWidthEx(UI_WIDGET(pText), 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(UI_WIDGET(pText), 0, 100, 0, 0);
		ui_SetActive(UI_WIDGET(pText), false);
		ui_WindowAddChild(pDefDoc->pPopupWindow, pText);
		pData = (CostumeAddSaveData*)calloc(1, sizeof(CostumeAddSaveData));
		pData->pDefDoc = pDefDoc;
		pData->pcName = strdup(eaAdds[i]->pcName);
		pData->pcFileName = strdup(eaAdds[i]->pcFileName);
		pData->eSaveType = SaveType_MatAdd;
		pButton = ui_ButtonCreate("Save Here", 0, y, costumeDefEdit_UIMatSaveButton, pData);
		ui_WidgetSetWidth(UI_WIDGET(pButton), 90);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, 0, 0, UITopRight);
		ui_WindowAddChild(pDefDoc->pPopupWindow, pButton);
		y+= 28;
	}

	pLabel = ui_LabelCreate("Other Add File", 0, y);
	ui_WindowAddChild(pDefDoc->pPopupWindow, pLabel);
	pFile = ui_FileNameEntryCreate(EditorPrefGetString(COSTUME_EDITOR, "FilePref", "MatAdd", ""), NULL, "defs", "defs/costumes/Definitions", ".addmat", UIBrowseNewOrExisting);
	ui_WidgetSetPosition(UI_WIDGET(pFile), 100, y);
	ui_WidgetSetWidthEx(UI_WIDGET(pFile), 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pFile), 0, 100, 0, 0);
	ui_WindowAddChild(pDefDoc->pPopupWindow, pFile);
	pDefDoc->pAddSaveFileEntry = pFile;
	pData = (CostumeAddSaveData*)calloc(1, sizeof(CostumeAddSaveData));
	pData->pDefDoc = pDefDoc;
	pData->pcName = NULL;
	pData->pcFileName = NULL;
	pData->eSaveType = SaveType_MatAdd;
	pButton = ui_ButtonCreate("Save Here", 0, y, costumeDefEdit_UIMatSaveButton, pData);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 90);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, 0, 0, UITopRight);
	ui_WindowAddChild(pDefDoc->pPopupWindow, pButton);
	y+=28;

	// Create the buttons
	pButton = ui_ButtonCreate("Cancel Save of Adds",0,y,costumeDefEdit_UIMatSaveCancel,pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 140);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), -60, y, 0.5, 0, UITopLeft);
	ui_WindowAddChild(pDefDoc->pPopupWindow,pButton);
	y+=28;

	ui_WidgetSetHeight(UI_WIDGET(pDefDoc->pPopupWindow), y);

	// Show the window
	ui_WindowSetClosable(pDefDoc->pPopupWindow,0);
	ui_WindowPresent(pDefDoc->pPopupWindow);
}


void costumeDefEdit_SavePrefs(CostumeEditDefDoc *pDefDoc)
{
	// No prefs to store right now
}


void costumeDefEdit_SelectDefBone(CostumeEditDefDoc *pDefDoc, PCBoneDef *pBoneDef)
{
	// Choose a bone
	if (!pBoneDef) {
		pBoneDef = pDefDoc->eaDefBones[0];
	}
	pDefDoc->pCurrentBoneDef = pBoneDef;

	// Update lists
	costumeDefEdit_DefUpdateLists(pDefDoc);

	// Save prefs when change bone
	costumeDefEdit_SavePrefs(pDefDoc);
}


void costumeDefEdit_SetSkeleton(CostumeEditDefDoc *pDefDoc, PCSkeletonDef *pSkeleton)
{
	if (!pSkeleton) {
		return;
	}

	// Set the skeleton name
	pDefDoc->pcSkeleton = strdup(pSkeleton->pcName);
}


static FileScanAction costumeDefEdit_TexFileScanAction(char *dir, struct _finddata32_t *data, void *pUserData)
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


static void costumeDefEdit_TexFileChangeCallback(const char *relpath, int when)
{
	// When a wtex file changes, reload the list
	eaClear(&g_eaTexFileNames);
	fileScanAllDataDirs("texture_library/costumes", costumeDefEdit_TexFileScanAction, NULL);
	fileScanAllDataDirs("texture_library/character_library", costumeDefEdit_TexFileScanAction, NULL);
	fileScanAllDataDirs("texture_library/core_costumes", costumeDefEdit_TexFileScanAction, NULL);
}


void costumeDefEdit_UpdateDisplayNameStatus(CostumeEditDefDoc *pDefDoc)
{
	pDefDoc->pDefBoneCombo->drawData = gUseDispNames ? "DisplayNameMsg" : "Name";
	pDefDoc->pDefBoneCombo->pTextData = gUseDispNames ? "DisplayNameMsg" : "Name";
	pDefDoc->pDefBoneCombo->bUseMessage = gUseDispNames;
	if (pDefDoc->pDefBoneCombo->pPopupList) {
		ui_ListColumnSetType(pDefDoc->pDefBoneCombo->pPopupList->eaColumns[0], gUseDispNames ? UIListPTMessage : UIListPTName, (intptr_t)(gUseDispNames ? "DisplayNameMsg" : "Name"), NULL);
	}
	pDefDoc->pDefGeoCombo->drawData = gUseDispNames ? "DisplayNameMsg" : "Name";
	pDefDoc->pDefGeoCombo->pTextData = gUseDispNames ? "DisplayNameMsg" : "Name";
	pDefDoc->pDefGeoCombo->bUseMessage = gUseDispNames;
	if (pDefDoc->pDefGeoCombo->pPopupList) {
		ui_ListColumnSetType(pDefDoc->pDefGeoCombo->pPopupList->eaColumns[0], gUseDispNames ? UIListPTMessage : UIListPTName, (intptr_t)(gUseDispNames ? "DisplayNameMsg" : "Name"), NULL);
	}
	pDefDoc->pDefMatCombo->drawData = gUseDispNames ? "DisplayNameMsg" : "Name";
	pDefDoc->pDefMatCombo->pTextData = gUseDispNames ? "DisplayNameMsg" : "Name";
	pDefDoc->pDefMatCombo->bUseMessage = gUseDispNames;
	if (pDefDoc->pDefMatCombo->pPopupList) {
		ui_ListColumnSetType(pDefDoc->pDefMatCombo->pPopupList->eaColumns[0], gUseDispNames ? UIListPTMessage : UIListPTName, (intptr_t)(gUseDispNames ? "DisplayNameMsg" : "Name"), NULL);
	}

	// Apply to non-field lists
	ui_ListColumnSetType(pDefDoc->pDefGeoList->eaColumns[0], gUseDispNames ? UIListPTMessage : UIListPTName, (intptr_t)(gUseDispNames ? "DisplayNameMsg" : "Name"), NULL);
	ui_ListColumnSetType(pDefDoc->pDefMatList->eaColumns[0], gUseDispNames ? UIListPTMessage : UIListPTName, (intptr_t)(gUseDispNames ? "DisplayNameMsg" : "Name"), NULL);
	ui_ListColumnSetType(pDefDoc->pDefTexList->eaColumns[0], gUseDispNames ? UIListPTMessage : UIListPTName, (intptr_t)(gUseDispNames ? "DisplayNameMsg" : "Name"), NULL);
	ui_ListColumnSetType(pDefDoc->pDefCatList->eaColumns[0], gUseDispNames ? UIListPTMessage : UIListPTName, (intptr_t)(gUseDispNames ? "DisplayNameMsg" : "Name"), NULL);

	// Apply to relevant fields
	MEFieldSetDictField(pDefDoc->pDefGeoMirrorField, "Name", gUseDispNames ? "DisplayNameMsg" : "Name", gUseDispNames);
	MEFieldSetDictField(pDefDoc->pDefGeoMatField, "Name", gUseDispNames ? "DisplayNameMsg" : "Name", gUseDispNames);
}


//---------------------------------------------------------------------------------------------------
// UI Callbacks
//---------------------------------------------------------------------------------------------------


static void costumeDefEdit_UIAddMatsButton(UIWidget *pWidget, CostumeEditDefDoc *pDefDoc)
{
	PCMaterialDef ***peaDefs;
	int i;

	// Get the selection
	eaDestroy(&pDefDoc->eaMatsToAdd);
	pDefDoc->pcGeoForMatsToAdd = strdup(pDefDoc->pCurrentGeoDef->pcName);
	peaDefs = (PCMaterialDef***)ui_FilteredListGetSelectedObjects(pDefDoc->pAddList);
	for(i=0; i<eaSize(peaDefs); ++i) {
		eaPush(&pDefDoc->eaMatsToAdd, strdup((*peaDefs)[i]->pcName));
	}

	EditorPrefStoreWindowPosition(COSTUME_EDITOR, "WindowPosition", "AddMats", pDefDoc->pPopupWindow);

	// Hide the popup
	costumeDefEdit_HidePopup(pDefDoc);

	// Apply the new mats
	costumeDefEdit_AddMaterialsToGeo(pDefDoc, 0, NULL);
}


static void costumeDefEdit_UIAddTexsButton(UIWidget *pWidget, CostumeEditDefDoc *pDefDoc)
{
	PCTextureDef ***peaDefs;
	int i;

	// Get the selection
	eaDestroy(&pDefDoc->eaTexsToAdd);
	pDefDoc->pcMatForTexsToAdd = strdup(pDefDoc->pCurrentMatDef->pcName);
	peaDefs = (PCTextureDef***)ui_FilteredListGetSelectedObjects(pDefDoc->pAddList);
	for(i=0; i<eaSize(peaDefs); ++i) {
		eaPush(&pDefDoc->eaTexsToAdd, strdup((*peaDefs)[i]->pcName));
	}

	EditorPrefStoreWindowPosition(COSTUME_EDITOR, "WindowPosition", "AddTexs", pDefDoc->pPopupWindow);

	// Hide the popup
	costumeDefEdit_HidePopup(pDefDoc);

	// Apply the new textures
	costumeDefEdit_AddTexturesToMaterial(pDefDoc, 0, NULL);
}


static void costumeDefEdit_UICancelCreateDupsButton(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	EditorPrefStoreWindowPosition(COSTUME_EDITOR, "WindowPosition", "CreateDups", pDefDoc->pPopupWindow);

	// Hide the popup
	costumeDefEdit_HidePopup(pDefDoc);
}

static void costumeDefEdit_UICancelAddMatsButton(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	EditorPrefStoreWindowPosition(COSTUME_EDITOR, "WindowPosition", "AddMats", pDefDoc->pPopupWindow);

	// Hide the popup
	costumeDefEdit_HidePopup(pDefDoc);
}


static void costumeDefEdit_UICancelAddTexsButton(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	EditorPrefStoreWindowPosition(COSTUME_EDITOR, "WindowPosition", "AddTexs", pDefDoc->pPopupWindow);

	// Hide the popup
	costumeDefEdit_HidePopup(pDefDoc);
}


static void costumeDefEdit_UIContextGeo(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, CostumeEditDefDoc *pDefDoc)
{
	if (!pDefDoc->pGeoContextMenu) {
		pDefDoc->pGeoContextMenu = ui_MenuCreate(NULL);
		ui_MenuAppendItems(pDefDoc->pGeoContextMenu,
			ui_MenuItemCreate("Apply to Costume",UIMenuCallback, costumeDefEdit_UIMenuSelectApplyGeo, pDefDoc, NULL),
			NULL);
	}

	pDefDoc->iGeoContextRow = iRow;
	pDefDoc->pGeoContextMenu->widget.scale = emGetEditorScale(pDefDoc->pEditor);
	ui_MenuPopupAtCursor(pDefDoc->pGeoContextMenu);
}


static void costumeDefEdit_UIContextMat(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, CostumeEditDefDoc *pDefDoc)
{
	if (!pDefDoc->pMatContextMenu) {
		pDefDoc->pMatContextMenu = ui_MenuCreate(NULL);
		ui_MenuAppendItems(pDefDoc->pMatContextMenu,
			ui_MenuItemCreate("Apply to Costume",UIMenuCallback, costumeDefEdit_UIMenuSelectApplyMat, pDefDoc, NULL),
			ui_MenuItemCreate("Remove Material",UIMenuCallback, costumeDefEdit_UIMenuSelectRemoveMat, pDefDoc, NULL),
			NULL);
	}

	pDefDoc->pMatContextMenu->items[0]->active = (pDefDoc->pOrigGeoDef != NULL);
	pDefDoc->pMatContextMenu->items[1]->active = (pDefDoc->pOrigGeoDef != NULL);

	pDefDoc->iMatContextRow = iRow;
	pDefDoc->pMatContextMenu->widget.scale = emGetEditorScale(pDefDoc->pEditor);
	ui_MenuPopupAtCursor(pDefDoc->pMatContextMenu);
}


static void costumeDefEdit_UIContextTex(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, CostumeEditDefDoc *pDefDoc)
{
	if (!pDefDoc->pTexContextMenu) {
		pDefDoc->pTexContextMenu = ui_MenuCreate(NULL);
		ui_MenuAppendItems(pDefDoc->pTexContextMenu,
			ui_MenuItemCreate("Apply to Costume",UIMenuCallback, costumeDefEdit_UIMenuSelectApplyTex, pDefDoc, NULL),
			ui_MenuItemCreate("Remove Texture",UIMenuCallback, costumeDefEdit_UIMenuSelectRemoveTex, pDefDoc, NULL),
			NULL);
	}

	pDefDoc->pTexContextMenu->items[0]->active = (pDefDoc->pOrigTexDef != NULL);
	pDefDoc->pTexContextMenu->items[1]->active = (pDefDoc->pOrigTexDef != NULL);

	pDefDoc->iTexContextRow = iRow;
	pDefDoc->pTexContextMenu->widget.scale = emGetEditorScale(pDefDoc->pEditor);
	ui_MenuPopupAtCursor(pDefDoc->pTexContextMenu);
}


static void costumeDefEdit_UIDefExtraTextureChanged(MEField *pField, bool bFinished, CostumeExtraTextureGroup *pGroup)
{
	if (!pGroup->pDefDoc->pOrigTexDef && pGroup->pTypeField && eaSize(&pGroup->pDefDoc->pCurrentTexDef->eaExtraSwaps) > pGroup->index) {
		PCExtraTexture *pExtra = pGroup->pDefDoc->pCurrentTexDef->eaExtraSwaps[pGroup->index];
		if (pExtra->pcNewTexture) {
			int len = (int)strlen(pExtra->pcNewTexture);

			if ((len > 3) && (stricmp(pExtra->pcNewTexture + len - 3, "_MM") == 0)) {
				pExtra->eTypeFlags = kPCTextureType_Pattern;
			} else if ((len > 2) && (stricmp(pExtra->pcNewTexture + len - 2, "_N") == 0)) {
				pExtra->eTypeFlags = kPCTextureType_Detail;
			} else if ((len > 2) && (stricmp(pExtra->pcNewTexture + len - 2, "_D") == 0)) {
				pExtra->eTypeFlags = kPCTextureType_Diffuse;
			} else if ((len > 2) && (stricmp(pExtra->pcNewTexture + len - 2, "_S") == 0)) {
				pExtra->eTypeFlags = kPCTextureType_Specular;
			} else if ((len > 3) && (stricmp(pExtra->pcNewTexture + len - 3, "_R") == 0)) {
				pExtra->eTypeFlags = kPCTextureType_Movable;
			}
			MEFieldRefreshFromData(pGroup->pTypeField);
		}
	}
}


static void costumeDefEdit_UIDefFieldChanged(MEField *pField, bool bFinished, CostumeEditDefDoc *pDefDoc)
{
	costumeDefEdit_DefUpdateState(pDefDoc);
}

static void costumeDefEdit_FixColorSwap(PCTextureDef *texture, int colorIndex)
{
	int i;
	switch (colorIndex)
	{
	case 0:
		texture->uColorSwap0 = 0;
		for (i = 0; i < 3; ++i)
		{
			if (texture->uColorSwap1 == texture->uColorSwap0) ++texture->uColorSwap0;
			if (texture->uColorSwap2 == texture->uColorSwap0) ++texture->uColorSwap0;
			if (texture->uColorSwap3 == texture->uColorSwap0) ++texture->uColorSwap0;
		}
		break;
	case 1:
		texture->uColorSwap1 = 0;
		for (i = 0; i < 3; ++i)
		{
			if (texture->uColorSwap0 == texture->uColorSwap1) ++texture->uColorSwap1;
			if (texture->uColorSwap2 == texture->uColorSwap1) ++texture->uColorSwap1;
			if (texture->uColorSwap3 == texture->uColorSwap1) ++texture->uColorSwap1;
		}
		break;
	case 2:
		texture->uColorSwap2 = 0;
		for (i = 0; i < 3; ++i)
		{
			if (texture->uColorSwap0 == texture->uColorSwap2) ++texture->uColorSwap2;
			if (texture->uColorSwap1 == texture->uColorSwap2) ++texture->uColorSwap2;
			if (texture->uColorSwap3 == texture->uColorSwap2) ++texture->uColorSwap2;
		}
		break;
	case 3:
		texture->uColorSwap3 = 0;
		for (i = 0; i < 3; ++i)
		{
			if (texture->uColorSwap0 == texture->uColorSwap3) ++texture->uColorSwap3;
			if (texture->uColorSwap1 == texture->uColorSwap3) ++texture->uColorSwap3;
			if (texture->uColorSwap2 == texture->uColorSwap3) ++texture->uColorSwap3;
		}
		break;
	}
}

static void costumeDefEdit_UIDefColorSwapChanged(MEField *pField, bool bFinished, CostumeEditDefDoc *pDefDoc)
{
	PCTextureDef *texture = pDefDoc->pCurrentTexDef;

	if (pField == pDefDoc->pDefTexColorSwap0Field)
	{
		if (texture->uColorSwap0 < 0 || texture->uColorSwap0 > 3)
		{
			//Fix it
			costumeDefEdit_FixColorSwap(texture, 0);
			MEFieldRefreshFromData(pField);
			costumeDefEdit_DefUpdateState(pDefDoc);
		}
		//else
		//{
		//	//Swap it
		//	if (texture->uColorSwap1 == texture->uColorSwap0)
		//	{
		//		costumeDefEdit_FixColorSwap(texture, 1);
		//		MEFieldRefreshFromData(pDefDoc->pDefTexColorSwap1Field);
		//		costumeDefEdit_DefUpdateState(pDefDoc);
		//	}
		//	else if (texture->uColorSwap2 == texture->uColorSwap0)
		//	{
		//		costumeDefEdit_FixColorSwap(texture, 2);
		//		MEFieldRefreshFromData(pDefDoc->pDefTexColorSwap2Field);
		//		costumeDefEdit_DefUpdateState(pDefDoc);
		//	}
		//	else if (texture->uColorSwap3 == texture->uColorSwap0)
		//	{
		//		costumeDefEdit_FixColorSwap(texture, 3);
		//		MEFieldRefreshFromData(pDefDoc->pDefTexColorSwap3Field);
		//		costumeDefEdit_DefUpdateState(pDefDoc);
		//	}
		//}
	}

	if (pField == pDefDoc->pDefTexColorSwap1Field)
	{
		if (texture->uColorSwap1 < 0 || texture->uColorSwap1 > 3)
		{
			//Fix it
			costumeDefEdit_FixColorSwap(texture, 1);
			MEFieldRefreshFromData(pField);
			costumeDefEdit_DefUpdateState(pDefDoc);
		}
		//else
		//{
		//	//Swap it
		//	if (texture->uColorSwap0 == texture->uColorSwap1)
		//	{
		//		costumeDefEdit_FixColorSwap(texture, 0);
		//		MEFieldRefreshFromData(pDefDoc->pDefTexColorSwap0Field);
		//		costumeDefEdit_DefUpdateState(pDefDoc);
		//	}
		//	else if (texture->uColorSwap2 == texture->uColorSwap1)
		//	{
		//		costumeDefEdit_FixColorSwap(texture, 2);
		//		MEFieldRefreshFromData(pDefDoc->pDefTexColorSwap2Field);
		//		costumeDefEdit_DefUpdateState(pDefDoc);
		//	}
		//	else if (texture->uColorSwap3 == texture->uColorSwap1)
		//	{
		//		costumeDefEdit_FixColorSwap(texture, 3);
		//		MEFieldRefreshFromData(pDefDoc->pDefTexColorSwap3Field);
		//		costumeDefEdit_DefUpdateState(pDefDoc);
		//	}
		//}
	}

	if (pField == pDefDoc->pDefTexColorSwap2Field)
	{
		if (texture->uColorSwap2 < 0 || texture->uColorSwap2 > 3)
		{
			//Fix it
			costumeDefEdit_FixColorSwap(texture, 2);
			MEFieldRefreshFromData(pField);
			costumeDefEdit_DefUpdateState(pDefDoc);
		}
		//else
		//{
		//	//Swap it
		//	if (texture->uColorSwap1 == texture->uColorSwap2)
		//	{
		//		costumeDefEdit_FixColorSwap(texture, 1);
		//		MEFieldRefreshFromData(pDefDoc->pDefTexColorSwap1Field);
		//		costumeDefEdit_DefUpdateState(pDefDoc);
		//	}
		//	else if (texture->uColorSwap0 == texture->uColorSwap2)
		//	{
		//		costumeDefEdit_FixColorSwap(texture, 0);
		//		MEFieldRefreshFromData(pDefDoc->pDefTexColorSwap0Field);
		//		costumeDefEdit_DefUpdateState(pDefDoc);
		//	}
		//	else if (texture->uColorSwap3 == texture->uColorSwap2)
		//	{
		//		costumeDefEdit_FixColorSwap(texture, 3);
		//		MEFieldRefreshFromData(pDefDoc->pDefTexColorSwap3Field);
		//		costumeDefEdit_DefUpdateState(pDefDoc);
		//	}
		//}
	}

	if (pField == pDefDoc->pDefTexColorSwap3Field)
	{
		if (texture->uColorSwap3 < 0 || texture->uColorSwap3 > 3)
		{
			//Fix it
			costumeDefEdit_FixColorSwap(texture, 3);
			MEFieldRefreshFromData(pField);
			costumeDefEdit_DefUpdateState(pDefDoc);
		}
		//else
		//{
		//	//Swap it
		//	if (texture->uColorSwap1 == texture->uColorSwap3)
		//	{
		//		costumeDefEdit_FixColorSwap(texture, 1);
		//		MEFieldRefreshFromData(pDefDoc->pDefTexColorSwap1Field);
		//		costumeDefEdit_DefUpdateState(pDefDoc);
		//	}
		//	else if (texture->uColorSwap2 == texture->uColorSwap3)
		//	{
		//		costumeDefEdit_FixColorSwap(texture, 2);
		//		MEFieldRefreshFromData(pDefDoc->pDefTexColorSwap2Field);
		//		costumeDefEdit_DefUpdateState(pDefDoc);
		//	}
		//	else if (texture->uColorSwap0 == texture->uColorSwap3)
		//	{
		//		costumeDefEdit_FixColorSwap(texture, 0);
		//		MEFieldRefreshFromData(pDefDoc->pDefTexColorSwap0Field);
		//		costumeDefEdit_DefUpdateState(pDefDoc);
		//	}
		//}
	}
}


static void costumeDefEdit_UIDefGeoNameScopeChanged(MEField *pField, bool bFinished, CostumeEditDefDoc *pDefDoc)
{
	if (pDefDoc->pCurrentGeoDef && !pDefDoc->bIgnoreDefFieldChanges) {
		resFixFilename(g_hCostumeGeometryDict, pDefDoc->pCurrentGeoDef->pcName, pDefDoc->pCurrentGeoDef);
		ui_LabelSetText(pDefDoc->pDefGeoFileNameLabel, pDefDoc->pCurrentGeoDef->pcFileName);
	}
	costumeDefEdit_UIDefFieldChanged(pField, bFinished, pDefDoc);
}


static void costumeDefEdit_UIDefMatNameScopeChanged(MEField *pField, bool bFinished, CostumeEditDefDoc *pDefDoc)
{
	if (pDefDoc->pCurrentMatDef && !pDefDoc->bIgnoreDefFieldChanges) {
		resFixFilename(g_hCostumeMaterialDict, pDefDoc->pCurrentMatDef->pcName, pDefDoc->pCurrentMatDef);
		ui_LabelSetText(pDefDoc->pDefMatFileNameLabel, pDefDoc->pCurrentMatDef->pcFileName);
	}
	costumeDefEdit_UIDefFieldChanged(pField, bFinished, pDefDoc);
}


static void costumeDefEdit_UIDefTexNameScopeChanged(MEField *pField, bool bFinished, CostumeEditDefDoc *pDefDoc)
{
	if (pDefDoc->pCurrentTexDef && !pDefDoc->bIgnoreDefFieldChanges) {
		resFixFilename(g_hCostumeTextureDict, pDefDoc->pCurrentTexDef->pcName, pDefDoc->pCurrentTexDef);
		ui_LabelSetText(pDefDoc->pDefTexFileNameLabel, pDefDoc->pCurrentTexDef->pcFileName);
	}
	costumeDefEdit_UIDefFieldChanged(pField, bFinished, pDefDoc);
}


static void costumeDefEdit_UIDefGeoAddMat(UIButton *pWidget, CostumeEditDefDoc *pDefDoc)
{
	DictionaryEArrayStruct *pDefStruct = resDictGetEArrayStruct(g_hCostumeMaterialDict);
	UIWindow *pWin;
	UIButton *pButton;
	UIFilteredList *pList;

	pWin = ui_WindowCreate("Add Materials to Geometry", 300, 400, 300, 400);
	pWin->widget.scale = emGetEditorScale(pDefDoc->pEditor);
	EditorPrefGetWindowPosition(COSTUME_EDITOR, "WindowPosition", "AddMats", pWin);

	pList = ui_FilteredListCreateParseName("Materials", parse_PCMaterialDef, &pDefStruct->ppReferents, "Name", 20);
	ui_FilteredListSetMultiselect(pList, true);
	ui_FilteredListSetActivatedCallback(pList, costumeDefEdit_UIAddMatsButton, pDefDoc);
	ui_ListAppendColumn(pList->pList, ui_ListColumnCreateParseMessage("Display Name", "DisplayNameMsg", NULL));
	ui_WidgetSetDimensionsEx(UI_WIDGET(pList), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pList), 0, 0, 0, 28);
	ui_WindowAddChild(pWin, pList);
	pDefDoc->pAddList = pList;

	pButton = ui_ButtonCreate("Add", 90, 0, costumeDefEdit_UIAddMatsButton, pDefDoc);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 90, 0, 0, 0, UIBottomRight);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 80);
	ui_WindowAddChild(pWin, pButton);

	pButton = ui_ButtonCreate("Cancel", 0, 0, costumeDefEdit_UICancelAddMatsButton, pDefDoc);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 0, 0, 0, UIBottomRight);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 80);
	ui_WindowAddChild(pWin, pButton);

	pDefDoc->pPopupWindow = pWin;
	ui_WindowSetModal(pWin, true);
	ui_WindowSetClosable(pWin, false);
	ui_WindowPresent(pWin);
}


static void costumeDefEdit_UIDefGeometryChanged(MEField *pField, bool bFinished, CostumeEditDefDoc *pDefDoc)
{
	// When the geometry changes, reload the possible model list
	costumeDefEdit_GetValidModels(ui_TextEntryGetText(pField->pUIText), &pDefDoc->eaModelNames);

	// If new, change the names to match
	if (!pDefDoc->bIgnoreDefFieldChanges && !pDefDoc->pOrigGeoDef && pDefDoc->pCurrentGeoDef && pDefDoc->pCurrentGeoDef->pcGeometry) {
		// Strip off directory path for the name
		const char *pcName = getFileNameConst(pDefDoc->pCurrentGeoDef->pcGeometry);
		if (!pDefDoc->pcLastGeoChangeName || (stricmp(pDefDoc->pcLastGeoChangeName, pcName) != 0)) {
			StructFreeString((char*)pDefDoc->pCurrentGeoDef->displayNameMsg.pEditorCopy->pcDefaultString);
			pDefDoc->pCurrentGeoDef->pcName = allocAddString(pcName);
			pDefDoc->pCurrentGeoDef->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString(pcName);

			// Save name change event
			SAFE_FREE(pDefDoc->pcLastGeoChangeName);
			pDefDoc->pcLastGeoChangeName = strdup(pcName);
		}
	}

	// Go update lists and such
	costumeDefEdit_UIDefFieldChanged(pField, bFinished, pDefDoc);

	// And finally ensure data gets refreshed
	if (!pDefDoc->bIgnoreDefFieldChanges) {
		MEFieldRefreshFromData(pDefDoc->pDefGeoNameField);
		MEFieldRefreshFromData(pDefDoc->pDefGeoDispNameField);
		MEFieldRefreshFromData(pDefDoc->pDefGeoModelField);

		// If model invalid, clear selection
		if (pDefDoc->pCurrentGeoDef->pcModel) {
			int i;

			for(i=eaSize(&pDefDoc->eaModelNames)-1; i>=0; --i) {
				if (stricmp(pDefDoc->eaModelNames[i], pDefDoc->pCurrentGeoDef->pcModel) == 0) {
					break;
				}
			}
			if (i < 0) {
				// Clear selection since current model is not in the list
				ui_ComboBoxSetSelected(pDefDoc->pDefGeoModelField->pUICombo, -1);
			}
		} else {
			// Clear selection since no model
			ui_ComboBoxSetSelected(pDefDoc->pDefGeoModelField->pUICombo, -1);
		}

		// If no model, select one
		if ((ui_ComboBoxGetSelected(pDefDoc->pDefGeoModelField->pUICombo) < 0) && eaSize(&pDefDoc->eaModelNames)) {
			ui_ComboBoxSetSelectedAndCallback(pDefDoc->pDefGeoModelField->pUICombo, 0);
		}
	}
}


static void costumeDefEdit_UIDefGeoNew(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	costumeDefEdit_DefSetGeo(pDefDoc, NULL);
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UIDefGeoRevert(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	if (!pDefDoc->pOrigGeoDef) {
		costumeDefEdit_DefSetGeo(pDefDoc, gNoGeoDef);
	} else {
		PCGeometryDef *pGeo = RefSystem_ReferentFromString(g_hCostumeGeometryDict, pDefDoc->pOrigGeoDef->pcName);
		costumeDefEdit_DefSetGeo(pDefDoc, pGeo);
	}
	costumeDefEdit_DefUpdateLists(pDefDoc);
}

static void costumeDefEdit_UIDismissErrorWindow(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	EditorPrefStoreWindowPosition(COSTUME_EDITOR, "Window Position", "Not Checked Out", pGlobalWindow);

	// Free the window
	ui_WindowHide(pGlobalWindow);
	ui_WidgetQueueFreeAndNull(&pGlobalWindow);
}

static void costumeDefEdit_NotCheckedOutError(CostumeEditDefDoc *pDefDoc)
{
	UIButton *pButton;
	int y = 0;
	int width = 250;
	int x = 0;

	pGlobalWindow = ui_WindowCreate("You can't save this. It is not checked out.", 200, 200, 300, 60);

	EditorPrefGetWindowPosition(COSTUME_EDITOR, "Window Position", "Not Checked Out", pGlobalWindow);

	pButton = ui_ButtonCreate("Cancel", 0, 0, costumeDefEdit_UIDismissErrorWindow, pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.5, 0, UITopLeft);
	ui_WindowAddChild(pGlobalWindow, pButton);

	y += 28;

	pGlobalWindow->widget.width = width;
	pGlobalWindow->widget.height = y;

	ui_WindowSetClosable(pGlobalWindow, false);
	ui_WindowSetModal(pGlobalWindow, true);

	ui_WindowPresent(pGlobalWindow);
}

static void costumeDefEdit_UIDefGeoSave(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	if (pDefDoc->pOrigGeoDef && !resIsWritable(g_hCostumeGeometryDict, pDefDoc->pOrigGeoDef->pcName))
	{
		costumeDefEdit_NotCheckedOutError(pDefDoc);
		return;
	}

	pDefDoc->saveRequested[SaveType_GeoDef] = true;
	pDefDoc->bGeoDefSaved = false;
	if (pDefDoc->pGeoDefToSave) {
		StructDestroy(parse_PCGeometryDef, pDefDoc->pGeoDefToSave);
	}
	pDefDoc->pGeoDefToSave = StructClone(parse_PCGeometryDef, pDefDoc->pCurrentGeoDef);
	if (pDefDoc->pOrigGeoDef) {
		pDefDoc->pGeoDefToSaveOrig = StructClone(parse_PCGeometryDef, pDefDoc->pOrigGeoDef);
	} else {
		pDefDoc->pGeoDefToSaveOrig = NULL;
	}

	costumeDefEdit_SaveSubDoc(pDefDoc);
}


static void costumeDefEdit_UIDefMatAddTex(UIButton *pWidget, CostumeEditDefDoc *pDefDoc)
{
	DictionaryEArrayStruct *pDefStruct = resDictGetEArrayStruct(g_hCostumeTextureDict);
	UIWindow *pWin;
	UIButton *pButton;
	UIFilteredList *pList;

	pWin = ui_WindowCreate("Add Textures to Material", 300, 400, 300, 400);
	pWin->widget.scale = emGetEditorScale(pDefDoc->pEditor);
	EditorPrefGetWindowPosition(COSTUME_EDITOR, "WindowPosition", "AddTexs", pWin);

	pList = ui_FilteredListCreateParseName("Textures", parse_PCTextureDef, &pDefStruct->ppReferents, "Name", 20);
	ui_FilteredListSetMultiselect(pList, true);
	ui_FilteredListSetActivatedCallback(pList, costumeDefEdit_UIAddTexsButton, pDefDoc);
	ui_ListAppendColumn(pList->pList, ui_ListColumnCreateParseMessage("Display Name", "DisplayNameMsg", NULL));
	ui_WidgetSetDimensionsEx(UI_WIDGET(pList), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pList), 0, 0, 0, 28);
	ui_WindowAddChild(pWin, pList);
	pDefDoc->pAddList = pList;

	pButton = ui_ButtonCreate("Add", 90, 0, costumeDefEdit_UIAddTexsButton, pDefDoc);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 90, 0, 0, 0, UIBottomRight);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 80);
	ui_WindowAddChild(pWin, pButton);

	pButton = ui_ButtonCreate("Cancel", 0, 0, costumeDefEdit_UICancelAddTexsButton, pDefDoc);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 0, 0, 0, UIBottomRight);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 80);
	ui_WindowAddChild(pWin, pButton);

	pDefDoc->pPopupWindow = pWin;
	ui_WindowSetModal(pWin, true);
	ui_WindowSetClosable(pWin, false);
	ui_WindowPresent(pWin);
}


static void costumeDefEdit_UIDefMatDataChanged(MEField *pField, bool bFinished, CostumeEditDefDoc *pDefDoc)
{
	pDefDoc->pCurrentMatDef->pColorOptions->bAllowGlow[0] = pDefDoc->currentDefaults.bAllowGlow0;
	pDefDoc->pCurrentMatDef->pColorOptions->bAllowGlow[1] = pDefDoc->currentDefaults.bAllowGlow1;
	pDefDoc->pCurrentMatDef->pColorOptions->bAllowGlow[2] = pDefDoc->currentDefaults.bAllowGlow2;
	pDefDoc->pCurrentMatDef->pColorOptions->bAllowGlow[3] = pDefDoc->currentDefaults.bAllowGlow3;

	pDefDoc->pCurrentMatDef->pColorOptions->bAllowReflection[0] = pDefDoc->currentDefaults.bAllowReflect0;
	pDefDoc->pCurrentMatDef->pColorOptions->bAllowReflection[1] = pDefDoc->currentDefaults.bAllowReflect1;
	pDefDoc->pCurrentMatDef->pColorOptions->bAllowReflection[2] = pDefDoc->currentDefaults.bAllowReflect2;
	pDefDoc->pCurrentMatDef->pColorOptions->bAllowReflection[3] = pDefDoc->currentDefaults.bAllowReflect3;
	if (pDefDoc->pCurrentMatDef->pColorOptions->bCustomReflection) {
		pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[0] = pDefDoc->currentDefaults.fDefaultReflect0;
		pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[1] = pDefDoc->currentDefaults.fDefaultReflect1;
		pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[2] = pDefDoc->currentDefaults.fDefaultReflect2;
		pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[3] = pDefDoc->currentDefaults.fDefaultReflect3;
	} else {
		pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[0] = 0;
		pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[1] = 0;
		pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[2] = 0;
		pDefDoc->pCurrentMatDef->pColorOptions->defaultReflection[3] = 0;
	}

	pDefDoc->pCurrentMatDef->pColorOptions->bAllowSpecularity[0] = pDefDoc->currentDefaults.bAllowSpecular0;
	pDefDoc->pCurrentMatDef->pColorOptions->bAllowSpecularity[1] = pDefDoc->currentDefaults.bAllowSpecular1;
	pDefDoc->pCurrentMatDef->pColorOptions->bAllowSpecularity[2] = pDefDoc->currentDefaults.bAllowSpecular2;
	pDefDoc->pCurrentMatDef->pColorOptions->bAllowSpecularity[3] = pDefDoc->currentDefaults.bAllowSpecular3;
	if (pDefDoc->pCurrentMatDef->pColorOptions->bCustomSpecularity) {
		pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[0] = pDefDoc->currentDefaults.fDefaultSpecular0;
		pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[1] = pDefDoc->currentDefaults.fDefaultSpecular1;
		pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[2] = pDefDoc->currentDefaults.fDefaultSpecular2;
		pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[3] = pDefDoc->currentDefaults.fDefaultSpecular3;
	} else {
		pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[0] = 0;
		pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[1] = 0;
		pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[2] = 0;
		pDefDoc->pCurrentMatDef->pColorOptions->defaultSpecularity[3] = 0;
	}

	pDefDoc->pCurrentMatDef->pColorOptions->bSuppressMuscle[0] = pDefDoc->currentDefaults.bCustomMuscle0;
	pDefDoc->pCurrentMatDef->pColorOptions->bSuppressMuscle[1] = pDefDoc->currentDefaults.bCustomMuscle1;
	pDefDoc->pCurrentMatDef->pColorOptions->bSuppressMuscle[2] = pDefDoc->currentDefaults.bCustomMuscle2;
	pDefDoc->pCurrentMatDef->pColorOptions->bSuppressMuscle[3] = pDefDoc->currentDefaults.bCustomMuscle3;

	costumeDefEdit_UIDefFieldChanged(pField, bFinished, pDefDoc);
}


static void costumeDefEdit_UIDefMaterialChanged(MEField *pField, bool bFinished, CostumeEditDefDoc *pDefDoc)
{
	// If new, change the names to match
	if (!pDefDoc->pOrigMatDef && pDefDoc->pCurrentMatDef->pcMaterial && pDefDoc->pCurrentGeoDef->pcName) {
		char buf[1024];
		sprintf(buf, "%s_%s", pDefDoc->pCurrentGeoDef->pcName, pDefDoc->pCurrentMatDef->pcMaterial);
		if (!pDefDoc->pcLastMatChangeName || (stricmp(pDefDoc->pcLastMatChangeName, buf) != 0)) {
			StructFreeString((char*)pDefDoc->pCurrentMatDef->displayNameMsg.pEditorCopy->pcDefaultString);
			pDefDoc->pCurrentMatDef->pcName = allocAddString(buf);
			pDefDoc->pCurrentMatDef->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString(pDefDoc->pCurrentMatDef->pcMaterial);

			// Save name change info
			SAFE_FREE(pDefDoc->pcLastMatChangeName);
			pDefDoc->pcLastMatChangeName = strdup(buf);
		}
	}

	// Go update lists and such
	costumeDefEdit_UIDefFieldChanged(pField, bFinished, pDefDoc);

	// And finally ensure data gets refreshed
	if (!pDefDoc->bIgnoreDefFieldChanges) {
		MEFieldRefreshFromData(pDefDoc->pDefMatNameField);
		MEFieldRefreshFromData(pDefDoc->pDefMatDispNameField);
	}

	// Update material constants list
	costumeDefEdit_UpdateMaterialConstants(pDefDoc);
}


static void costumeDefEdit_UIDefMatNew(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	costumeDefEdit_DefSetMat(pDefDoc, NULL);
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UIDefMatRevert(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	if (!pDefDoc->pOrigMatDef) {
		costumeDefEdit_DefSetMat(pDefDoc, gNoMatDef);
	} else {
		PCMaterialDef *pMat = RefSystem_ReferentFromString(g_hCostumeMaterialDict, pDefDoc->pOrigMatDef->pcName);
		costumeDefEdit_DefSetMat(pDefDoc, pMat);
	}
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UIDefMatSave(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	if (pDefDoc->pOrigMatDef && !resIsWritable(g_hCostumeMaterialDict, pDefDoc->pOrigMatDef->pcName))
	{
		costumeDefEdit_NotCheckedOutError(pDefDoc);
		return;
	}

	pDefDoc->saveRequested[SaveType_MatDef] = true;
	pDefDoc->bMatDefSaved = false;
	if (pDefDoc->pMatDefToSave) {
		StructDestroy(parse_PCMaterialDef, pDefDoc->pMatDefToSave);
	}
	pDefDoc->pMatDefToSave = StructClone(parse_PCMaterialDef, pDefDoc->pCurrentMatDef);
	if (pDefDoc->pOrigMatDef) {
		pDefDoc->pMatDefToSaveOrig = StructClone(parse_PCMaterialDef, pDefDoc->pOrigMatDef);
	} else {
		pDefDoc->pMatDefToSaveOrig = NULL;
	}
	// Clear tex add/remove data
	eaDestroy(&pDefDoc->eaTexsToAdd);

	costumeDefEdit_SaveSubDoc(pDefDoc);
}


static void costumeDefEdit_UIDefTexNew(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	costumeDefEdit_DefSetTex(pDefDoc, NULL);
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UIDefTexRevert(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	if (!pDefDoc->pOrigTexDef) {
		costumeDefEdit_DefSetTex(pDefDoc, gNoTexDef);
	} else {
		PCTextureDef *pTex = RefSystem_ReferentFromString(g_hCostumeTextureDict, pDefDoc->pOrigTexDef->pcName);
		costumeDefEdit_DefSetTex(pDefDoc, pTex);
	}
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UIDefTexSave(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	if (pDefDoc->pOrigTexDef && !resIsWritable(g_hCostumeTextureDict, pDefDoc->pOrigTexDef->pcName))
	{
		costumeDefEdit_NotCheckedOutError(pDefDoc);
		return;
	}

	pDefDoc->saveRequested[SaveType_TexDef] = true;
	pDefDoc->bTexDefSaved = false;
	if (pDefDoc->pTexDefToSave) {
		StructDestroy(parse_PCTextureDef, pDefDoc->pTexDefToSave);
	}
	pDefDoc->pTexDefToSave = StructClone(parse_PCTextureDef, pDefDoc->pCurrentTexDef);
	if (pDefDoc->pOrigTexDef) {
		pDefDoc->pTexDefToSaveOrig = StructClone(parse_PCTextureDef, pDefDoc->pOrigTexDef);
	} else {
		pDefDoc->pTexDefToSaveOrig = NULL;
	}

	costumeDefEdit_SaveSubDoc(pDefDoc);
}


static void costumeDefEdit_UIDefTextureChanged(MEField *pField, bool bFinished, CostumeEditDefDoc *pDefDoc)
{
	int i;

	// If new, change the names to match
	if (!pDefDoc->pOrigTexDef && pDefDoc->pCurrentTexDef->pcNewTexture) {
		int len = (int)strlen(pDefDoc->pCurrentTexDef->pcNewTexture);

		if (!pDefDoc->pcLastTexChangeName || (stricmp(pDefDoc->pcLastTexChangeName, pDefDoc->pCurrentTexDef->pcNewTexture) != 0)) {
			StructFreeString(pDefDoc->pCurrentTexDef->displayNameMsg.pEditorCopy->pcDefaultString);
			pDefDoc->pCurrentTexDef->pcName = allocAddString(pDefDoc->pCurrentTexDef->pcNewTexture);
			pDefDoc->pCurrentTexDef->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString(pDefDoc->pCurrentTexDef->pcNewTexture);

			SAFE_FREE(pDefDoc->pcLastTexChangeName);
			pDefDoc->pcLastTexChangeName = strdup(pDefDoc->pCurrentTexDef->pcNewTexture);
		}

		if ((len > 3) && (stricmp(pDefDoc->pCurrentTexDef->pcNewTexture + len - 3, "_MM") == 0)) {
			pDefDoc->pCurrentTexDef->eTypeFlags = kPCTextureType_Pattern;
		} else if ((len > 2) && (stricmp(pDefDoc->pCurrentTexDef->pcNewTexture + len - 2, "_N") == 0)) {
			pDefDoc->pCurrentTexDef->eTypeFlags = kPCTextureType_Detail;
		} else if ((len > 2) && (stricmp(pDefDoc->pCurrentTexDef->pcNewTexture + len - 2, "_D") == 0)) {
			pDefDoc->pCurrentTexDef->eTypeFlags = kPCTextureType_Diffuse;
		} else if ((len > 2) && (stricmp(pDefDoc->pCurrentTexDef->pcNewTexture + len - 2, "_S") == 0)) {
			pDefDoc->pCurrentTexDef->eTypeFlags = kPCTextureType_Specular;
		} else if ((len > 3) && (stricmp(pDefDoc->pCurrentTexDef->pcNewTexture + len - 3, "_R") == 0)) {
			pDefDoc->pCurrentTexDef->eTypeFlags = kPCTextureType_Movable;
		}
	}

	// Go update lists and such
	costumeDefEdit_UIDefFieldChanged(pField, bFinished, pDefDoc);

	// And finally ensure data gets refreshed
	if (!pDefDoc->bIgnoreDefFieldChanges) {
		MEFieldRefreshFromData(pDefDoc->pDefTexNameField);
		MEFieldRefreshFromData(pDefDoc->pDefTexDispNameField);
		MEFieldRefreshFromData(pDefDoc->pDefTexTypeField);
		ui_SpriteSetTexture(pDefDoc->pDefTexSprite, pDefDoc->pCurrentTexDef->pcNewTexture ? pDefDoc->pCurrentTexDef->pcNewTexture : "");

		for(i=eaSize(&pDefDoc->eaTexGroups)-1; i>=0; --i) {
			ui_SpriteSetTexture(pDefDoc->eaTexGroups[i]->pSprite, pDefDoc->pCurrentTexDef->eaExtraSwaps[i]->pcNewTexture ? pDefDoc->pCurrentTexDef->eaExtraSwaps[i]->pcNewTexture : "");
		}
	}
}


static void costumeDefEdit_UIExtraTexAdd(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	PCExtraTexture *pExtraTex;

	// Add an empty structure to the def
	pExtraTex = StructCreate(parse_PCExtraTexture);
	eaPush(&pDefDoc->pCurrentTexDef->eaExtraSwaps, pExtraTex);

	// Update the display
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UIExtraTexRemove(UIButton *pButton, CostumeExtraTextureGroup *pTexGroup)
{
	int i;

	for(i=eaSize(&pTexGroup->pDefDoc->pCurrentTexDef->eaExtraSwaps)-1; i>=0; --i) {
		if (pTexGroup->pDefDoc->pCurrentTexDef->eaExtraSwaps[i] == pTexGroup->pOldTexField->pNew) {
			eaRemove(&pTexGroup->pDefDoc->pCurrentTexDef->eaExtraSwaps, i);
			costumeDefEdit_DefUpdateLists(pTexGroup->pDefDoc);
			return;
		}
	}
	assertmsg(0, "Missing field");
}


static void costumeDefEdit_UIGeoChildAdd(UIButton* pButton, CostumeGeoChildBoneGroup* pBoneGroup)
{
	int j;
	PCBoneDef *pBone = GET_REF(pBoneGroup->pDefDoc->pCurrentGeoDef->hBone);
	PCChildBone *pChildBone = pBone && eaSize(&pBone->eaChildBones) ? pBone->eaChildBones[pBoneGroup->index] : NULL;
	PCGeometryChildDef *pGeoInfo = NULL;
	PCGeometryRef* pGeoChild;

	if (pChildBone)
	{
		for(j=eaSize(&pBoneGroup->pDefDoc->pCurrentGeoDef->pOptions->eaChildGeos)-1; j>=0; --j) {
			if (GET_REF(pChildBone->hChildBone) == GET_REF(pBoneGroup->pDefDoc->pCurrentGeoDef->pOptions->eaChildGeos[j]->hChildBone)) {
				pGeoInfo = pBoneGroup->pDefDoc->pCurrentGeoDef->pOptions->eaChildGeos[j];
				break;
			}
		}
	}

	if (pGeoInfo)
	{
		// Add an empty structure to the def
		pGeoChild = StructCreate(parse_PCGeometryRef);
		SET_HANDLE_FROM_STRING(g_hCostumeGeometryDict, "None", pGeoChild->hGeo);

		eaPush(&pGeoInfo->eaChildGeometries, pGeoChild);

		// Update the display
		costumeDefEdit_DefUpdateLists(pBoneGroup->pDefDoc);
	}
}


static void costumeDefEdit_UIGeoChildRemove(UIButton* pButton, CostumeGeoChildGeoGroup* pChildGroup)
{
	int i = 0, j;
	PCBoneDef *pBone = GET_REF(pChildGroup->pDefDoc->pCurrentGeoDef->hBone);
	PCChildBone *pChildBone = pBone && eaSize(&pBone->eaChildBones) ? pBone->eaChildBones[pChildGroup->index] : NULL;
	PCGeometryChildDef *pGeoInfo = NULL;

	if (pChildBone)
	{
		for(j=eaSize(&pChildGroup->pDefDoc->pCurrentGeoDef->pOptions->eaChildGeos)-1; j>=0; --j) {
			if (GET_REF(pChildBone->hChildBone) == GET_REF(pChildGroup->pDefDoc->pCurrentGeoDef->pOptions->eaChildGeos[j]->hChildBone)) {
				pGeoInfo = pChildGroup->pDefDoc->pCurrentGeoDef->pOptions->eaChildGeos[j];
				break;
			}
		}
	}

	if (pGeoInfo)
	{
		for (i = eaSize(&pGeoInfo->eaChildGeometries) - 1; i >= 0; --i) {
			if (pGeoInfo->eaChildGeometries[i] == pChildGroup->pGeoField->pNew) {
				eaRemove(&pGeoInfo->eaChildGeometries, i);
				costumeDefEdit_DefUpdateLists(pChildGroup->pDefDoc);
				return;
			}
		}
	}
	assertmsg(0, "Missing field");
}

static void costumeDefEdit_UIGeoFXAdd(UIButton* pButton, CostumeEditDefDoc* pDefDoc)
{
	// Add an empty FX structure to the costume
	PCFX* pFx = StructCreate(parse_PCFX);
	eaPush(&pDefDoc->pCurrentGeoDef->pOptions->eaFX, pFx);

	// Update the display
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UIGeoFXRemove(UIButton* pButton, CostumeDefFxGroup* pGroup)
{
	int i = 0;

	for (i = eaSize(&pGroup->pDefDoc->pCurrentGeoDef->pOptions->eaFX)-1; i >= 0; --i) {
		if (pGroup->pDefDoc->pCurrentGeoDef->pOptions->eaFX[i] == pGroup->pFxField->pNew) {
			eaRemove(&pGroup->pDefDoc->pCurrentGeoDef->pOptions->eaFX, i);
			costumeDefEdit_DefUpdateLists(pGroup->pDefDoc);
			return;
		}
	}
	assertmsg(0, "Missing field");
}

static void costumeDefEdit_UIGeoFXSwapAdd(UIButton* pButton, CostumeEditDefDoc* pDefDoc)
{
	// Add an empty FX structure to the costume
	PCFXSwap* pFxSwap = StructCreate(parse_PCFXSwap);
	eaPush(&pDefDoc->pCurrentGeoDef->pOptions->eaFXSwap, pFxSwap);

	// Update the display
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UIGeoFXSwapRemove(UIButton* pButton, CostumeDefFxSwapGroup* pGroup)
{
	int i = 0;

	for (i = eaSize(&pGroup->pDefDoc->pCurrentGeoDef->pOptions->eaFXSwap)-1; i >= 0; --i) {
		if (pGroup->pDefDoc->pCurrentGeoDef->pOptions->eaFXSwap[i] == pGroup->pFxOldField->pNew) {
			eaRemove(&pGroup->pDefDoc->pCurrentGeoDef->pOptions->eaFXSwap, i);
			costumeDefEdit_DefUpdateLists(pGroup->pDefDoc);
			return;
		}
	}
	assertmsg(0, "Missing field");
}


static void costumeDefEdit_UIGeoSaveButton(UIButton *pButton, CostumeAddSaveData *pData)
{
	CostumeEditDefDoc *pDefDoc = pData->pDefDoc;
	EMFile *pEMFile = NULL;
	int i;

	// Before cleaning up popup, capture file name entry if we need it
	if (!pData->pcName) {
		// Wants to save using new file
		pData->pcFileName = strdup(ui_FileNameEntryGetFileName(pDefDoc->pAddSaveFileEntry));

		// Save the name used
		EditorPrefStoreString(COSTUME_EDITOR, "FilePref", "GeoAdd", pData->pcFileName);
	}

	// Hide the popup
	costumeDefEdit_HidePopup(pDefDoc);

	if (pData->pcName) {
		// Force save on the chosen selection
		costumeDefEdit_AddMaterialsToGeo(pDefDoc, pData->eSaveType, pData->pcName);
	} else {
		// This is a new selection that needs to be created
		pDefDoc->saveRequested[SaveType_GeoAdd] = true;
		if (pDefDoc->pGeoAddToSave) {
			StructDestroy(parse_PCGeometryAdd, pDefDoc->pGeoAddToSave);
		}
		pDefDoc->pGeoAddToSave = StructCreate(parse_PCGeometryAdd);
		pDefDoc->pGeoAddToSave->pcName = costumeDefEdit_CreateUniqueName("CostumeGeometryAdd", pDefDoc->pcGeoForMatsToAdd);
		pDefDoc->pGeoAddToSave->pcGeoName = StructAllocString(pDefDoc->pcGeoForMatsToAdd);
		pDefDoc->pGeoAddToSave->pcFileName = allocAddString(pData->pcFileName);
		for(i=0; i<eaSize(&pDefDoc->eaMatsToAdd); ++i) {
			const char *pcName = allocAddString(pDefDoc->eaMatsToAdd[i]);
			eaPush(&pDefDoc->pGeoAddToSave->eaAllowedMaterialDefs, pcName);
		}

		// Save it
		costumeDefEdit_SaveSubDoc(pDefDoc);
	}

	costumeDefEdit_CleanupPromptAddData(pDefDoc);
}


static void costumeDefEdit_UIGeoSaveCancel(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	// Hide the popup
	costumeDefEdit_HidePopup(pDefDoc);

	// Clean up save state data
	costumeDefEdit_CleanupPromptAddData(pDefDoc);
}


static void costumeDefEdit_UIMatConstantColorGroupChanged(UIColorButton *pButton, bool bFinished, CostumeMatConstantGroup *pGroup)
{
	Vec4 value;

	ui_ColorButtonGetColor(pButton, value);
	scaleVec4(value, 255.0f, pGroup->currentValue);

	// Also set it into the extra color if available
	if (pGroup->bIsSet) {
		int i = 0;

		for(i=eaSize(&pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraColors)-1; i>=0; --i) {
			if (stricmp(pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraColors[i]->pcName, pGroup->pcName) == 0) {
				scaleVec4(value, 255.0f, pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraColors[i]->color);
				break;
			}
		}
	}

	// Update the display
	costumeDefEdit_DefUpdateState(pGroup->pDoc);
}


static void costumeDefEdit_UIMatConstantValueGroupChanged(UISliderTextEntry *pSlider, bool bFinished, CostumeMatConstantGroup *pGroup)
{
	F32 fValue;
	S32 sliderIdx = 0;
	S32 numValues = costumeTailor_GetMatConstantNumValues(pGroup->pcName);

	// determine which of this group's sliders was changed
	if (pSlider == pGroup->pSlider1) {
		sliderIdx = 0;
	}
	else if (pSlider == pGroup->pSlider2) {
		sliderIdx = 1;
	}
	else if (pSlider == pGroup->pSlider3) {
		sliderIdx = 2;
	}
	else if (pSlider == pGroup->pSlider4) {
		sliderIdx = 3;
	}
	else {
		// it's not one of this group's sliders after all
		return;
	}

	fValue = atof(ui_SliderTextEntryGetText(pSlider));
	if (fValue != pGroup->currentValue[sliderIdx]) {
		int idx;
		bool last = (sliderIdx == (numValues - 1));	// true if this is the highest used slider index

		pGroup->currentValue[sliderIdx] = fValue;
		if (last) {
			// populate unused values with copies of the last valid value
			for (idx = (sliderIdx+1); idx < 4; idx++) {
				pGroup->currentValue[idx] = fValue;
			}
		}

		// Also set it into the extra constant if available
		if (pGroup->bIsSet) {
			int i;
			bool bFound = false;

			for(i=eaSize(&pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraConstants)-1; i>=0; --i) {
				if (stricmp(pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraConstants[i]->pcName, pGroup->pcName) == 0) {
					pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraConstants[i]->values[sliderIdx] = fValue;
					if (last) {
						// populate unused values with copies of the last valid value
						for (idx = (sliderIdx+1); idx < 4; idx++) {
							pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraConstants[i]->values[idx] = fValue;
						}
					}
					bFound = true;
					break;
				}
			}
			if (!bFound) {
				Alertf("Value group does not have the constant in place.");
			}
		}
	}

	// Update the display
	costumeDefEdit_DefUpdateState(pGroup->pDoc);
}


static void costumeDefEdit_UIMatConstantGroupToggled(UICheckButton *pButton, CostumeMatConstantGroup *pGroup)
{
	if (ui_CheckButtonGetState(pButton)) {
		if (pGroup->bIsColor) {
			PCMaterialDefColor *pMColor;

			// Need to add the color group to the current part
			pMColor = StructCreate(parse_PCMaterialDefColor);
			pMColor->pcName = allocAddString(pGroup->pcName);
			copyVec4(pGroup->currentValue, pMColor->color);
			eaPush(&pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraColors, pMColor);
		}
		else {
			PCMaterialDefConstant *pMConstant;

			// Need to add the color group to the current part
			pMConstant = StructCreate(parse_PCMaterialDefConstant);
			pMConstant->pcName = allocAddString(pGroup->pcName);
			copyVec4(pGroup->currentValue, pMConstant->values);
			eaPush(&pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraConstants, pMConstant);
		}
	}
	else {
		int i = 0;

		if (pGroup->bIsColor) {
			// Need to remove the color entry
			for (i = eaSize(&pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraColors) - 1; i >= 0; --i) {
				if (stricmp(pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraColors[i]->pcName, pGroup->pcName) == 0) {
					StructDestroy(parse_PCMaterialDefColor, pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraColors[i]);
					eaRemove(&pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraColors, i);
					break;
				}
			}
		}
		else {
			// Need to remove the constant entry
			for (i = eaSize(&pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraConstants) - 1; i >= 0; --i) {
				if (stricmp(pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraConstants[i]->pcName, pGroup->pcName) == 0) {
					StructDestroy(parse_PCMaterialDefConstant, pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraConstants[i]);
					eaRemove(&pGroup->pDoc->pCurrentMatDef->pOptions->eaExtraConstants, i);
					break;
				}
			}
		}
	}

	// Update the display, including activation state of toggled list controls
	costumeDefEdit_DefUpdateLists(pGroup->pDoc);
}


static void costumeDefEdit_UIMatFXSwapAdd(UIButton* pButton, CostumeEditDefDoc* pDefDoc)
{
	// Add an empty FX structure to the costume
	PCFXSwap* pFxSwap = StructCreate(parse_PCFXSwap);
	eaPush(&pDefDoc->pCurrentMatDef->pOptions->eaFXSwap, pFxSwap);

	// Update the display
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UIMatFXSwapRemove(UIButton* pButton, CostumeDefFxSwapGroup* pGroup)
{
	int i = 0;

	for (i = eaSize(&pGroup->pDefDoc->pCurrentMatDef->pOptions->eaFXSwap)-1; i >= 0; --i) {
		if (pGroup->pDefDoc->pCurrentMatDef->pOptions->eaFXSwap[i] == pGroup->pFxOldField->pNew) {
			eaRemove(&pGroup->pDefDoc->pCurrentMatDef->pOptions->eaFXSwap, i);
			costumeDefEdit_DefUpdateLists(pGroup->pDefDoc);
			return;
		}
	}
	assertmsg(0, "Missing field");
}


static void costumeDefEdit_UIMatSaveButton(UIButton *pButton, CostumeAddSaveData *pData)
{
	CostumeEditDefDoc *pDefDoc = pData->pDefDoc;
	int i;

	// Before cleaning up popup, capture file name entry if we need it
	if (!pData->pcName) {
		// Wants to save using new file
		pData->pcFileName = strdup(ui_FileNameEntryGetFileName(pDefDoc->pAddSaveFileEntry));

		// Save the name used
		EditorPrefStoreString(COSTUME_EDITOR, "FilePref", "MatAdd", pData->pcFileName);
	}

	// Hide the popup
	costumeDefEdit_HidePopup(pDefDoc);

	if (pData->pcName) {
		// Force save on the chosen selection
		costumeDefEdit_AddTexturesToMaterial(pDefDoc, pData->eSaveType, pData->pcName);
	} else {
		// This is a new selection that needs to be created
		pDefDoc->saveRequested[SaveType_MatAdd] = true;
		if (pDefDoc->pMatAddToSave) {
			StructDestroy(parse_PCMaterialAdd, pDefDoc->pMatAddToSave);
		}
		pDefDoc->pMatAddToSave = StructCreate(parse_PCMaterialAdd);
		pDefDoc->pMatAddToSave->pcName = costumeDefEdit_CreateUniqueName("CostumeMaterialAdd", pDefDoc->pcMatForTexsToAdd);
		pDefDoc->pMatAddToSave->pcMatName = StructAllocString(pDefDoc->pcMatForTexsToAdd);
		pDefDoc->pMatAddToSave->pcFileName = allocAddString(pData->pcFileName);
		for(i=0; i<eaSize(&pDefDoc->eaTexsToAdd); ++i) {
			const char *pcName = allocAddString(pDefDoc->eaTexsToAdd[i]);
			eaPush(&pDefDoc->pMatAddToSave->eaAllowedTextureDefs, pcName);
		}

		// Save it
		costumeDefEdit_SaveSubDoc(pDefDoc);
	}

	costumeDefEdit_CleanupPromptAddData(pDefDoc);
}


static void costumeDefEdit_UIMatSaveCancel(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	// Hide the popup
	costumeDefEdit_HidePopup(pDefDoc);
}


static void costumeDefEdit_UIMenuSelectApplyGeo(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc)
{
	PCGeometryDef *pGeoDef;
	CostumeEditDoc *pDoc;
	
	// Get the selected geometry
	if ((pDefDoc->iGeoContextRow < 0) || (pDefDoc->iGeoContextRow >= eaSize(&pDefDoc->eaDefGeos))) {
		return; // Bad selection
	}
	pGeoDef = pDefDoc->eaDefGeos[pDefDoc->iGeoContextRow];

	pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();
	costumeEdit_SetPartGeometry(pDoc, pDefDoc->pCurrentBoneDef->pcName, pGeoDef->pcName);
}


static void costumeDefEdit_UIMenuSelectApplyMat(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc)
{
	PCGeometryDef *pGeoDef;
	PCMaterialDef *pMatDef;
	CostumeEditDoc *pDoc;
	
	// Get the selected geometry and material
	if ((pDefDoc->iMatContextRow < 0) || (pDefDoc->iMatContextRow >= eaSize(&pDefDoc->eaDefMats))) {
		return; // Bad selection
	}
	pMatDef = pDefDoc->eaDefMats[pDefDoc->iMatContextRow];
	if (!pDefDoc->pOrigGeoDef) {
		return; // Bad selection
	}
	pGeoDef = pDefDoc->pOrigGeoDef;

	pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();
	costumeEdit_SetPartMaterial(pDoc, pDefDoc->pCurrentBoneDef->pcName, pGeoDef->pcName, pMatDef->pcName);
}


static void costumeDefEdit_UIMenuSelectApplyTex(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc)
{
	PCGeometryDef *pGeoDef;
	PCMaterialDef *pMatDef;
	PCTextureDef *pTexDef;
	CostumeEditDoc *pDoc;
	
	// Get the selected geometry, material, and texture
	if ((pDefDoc->iTexContextRow < 0) || (pDefDoc->iTexContextRow >= eaSize(&pDefDoc->eaDefTexs))) {
		return; // Bad selection
	}
	pTexDef = pDefDoc->eaDefTexs[pDefDoc->iTexContextRow];
	if (!pDefDoc->pOrigMatDef) {
		return; // Bad selection
	}
	pMatDef = pDefDoc->pOrigMatDef;
	if (!pDefDoc->pOrigGeoDef) {
		return; // Bad selection
	}
	pGeoDef = pDefDoc->pOrigGeoDef;

	pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();
	costumeEdit_SetPartTexture(pDoc, pDefDoc->pCurrentBoneDef->pcName, pGeoDef->pcName, pMatDef->pcName, pTexDef->pcName, pTexDef->eTypeFlags);
}


static void costumeDefEdit_UIMenuSelectDefGeoClone(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc)
{
	if (!pDefDoc->pOrigGeoDef) {
		return; // Bad State
	}

	// Set the name to something unique
	pDefDoc->pCurrentGeoDef->pcName = costumeDefEdit_CreateUniqueName("CostumeGeometry", pDefDoc->pOrigGeoDef->pcName);

	// Clear the original
	if (pDefDoc->pOrigGeoDef) {
		StructDestroy(parse_PCGeometryDef, pDefDoc->pOrigGeoDef);
		pDefDoc->pOrigGeoDef = NULL;
	}

	// Update the display
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UIMenuSelectDefGeoCopy(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc)
{
	if (gGeoDefClipboard) {
		StructDestroy(parse_PCGeometryDef, gGeoDefClipboard);
	}
	gGeoDefClipboard = StructClone(parse_PCGeometryDef, pDefDoc->pCurrentGeoDef);
}


static void costumeDefEdit_UIMenuSelectDefGeoPaste(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc)
{
	const char *pcName = allocAddString(pDefDoc->pCurrentGeoDef->pcName);
	const char *pcFileName = allocAddString(pDefDoc->pCurrentGeoDef->pcFileName);

	// Paste from clipboard
	StructCopyAll(parse_PCGeometryDef, gGeoDefClipboard, pDefDoc->pCurrentGeoDef);

	// Restore name and file
	pDefDoc->pCurrentGeoDef->pcName = pcName;
	pDefDoc->pCurrentGeoDef->pcFileName = pcFileName;

	// Update the display
	costumeDefEdit_DefUpdateLists(pDefDoc);
}

static PCBoneDef** s_eaValidBones = NULL;
static PCGeometryDef** s_eaGeosToSave = NULL;

static bool costumDefEdit_CheckForMorePendingGeoSaves(CostumeEditDefDoc *pDefDoc)
{
	if (eaSize(&s_eaGeosToSave) > 0)
	{
		PCGeometryDef* pGeo = s_eaGeosToSave[0];
		PCGeometryDef* pGeoOrig = RefSystem_ReferentFromString(g_hCostumeGeometryDict, pGeo->pcName);

		pDefDoc->pGeoDefToSave = pGeo;
		pDefDoc->pGeoDefToSaveOrig = pGeoOrig;

		if (pDefDoc->pGeoDefToSaveOrig && !resIsWritable(g_hCostumeGeometryDict, pDefDoc->pGeoDefToSave->pcName))
		{
			costumeDefEdit_NotCheckedOutError(pDefDoc);
			StructDestroy(parse_PCGeometryDef, s_eaGeosToSave[0]);
			eaRemoveFast(&s_eaGeosToSave, 0);
			costumDefEdit_CheckForMorePendingGeoSaves(pDefDoc);
			return (eaSize(&s_eaGeosToSave) > 0);
		}

		pDefDoc->saveRequested[SaveType_GeoDef] = true;
		pDefDoc->bGeoDefSaved = false;
		costumeDefEdit_SaveSubDoc(pDefDoc);
		eaRemoveFast(&s_eaGeosToSave, 0);
		return (eaSize(&s_eaGeosToSave) >= 0);
	}
	else
		return false;
}

static void costumeDefEdit_CheckoutGeoDef(const char* pcName)
{
	ResourceInfo *pInfo = resGetInfo("PCGeometryDef", pcName);
	if (pInfo) 
	{
		resCallback_HandleSimpleEdit *pCB;
		if (pCB = resGetSimpleEditCB("PCGeometryDef", kResEditType_CheckOut))
		{
			pCB(kResEditType_CheckOut, pInfo, NULL);
		}
		else
		{
			resRequestLockResource("PCGeometryDef", pcName, NULL);
		}
	}
}

static void costumeDefEdit_CheckoutAllGeoDefDupes(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc)
{
	PCBoneDef* pBoneDef = NULL;
	PCBoneDef* pCurBoneDef = SAFE_GET_REF2(pDefDoc, pCurrentGeoDef, hBone);
	char pcBaseNameBuf[260];
	const char *boneRefString;
	RefDictIterator iter;
	char* pcEndOfBaseName = NULL;
	int i, j, k;
	char buf[260];

	if (!pDefDoc->pOrigGeoDef) {
		return; // Bad State
	}

	eaClear(&s_eaValidBones);
	RefSystem_InitRefDictIterator(g_hCostumeBoneDict, &iter);
	while (pBoneDef = (PCBoneDef*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		if (pBoneDef == pCurBoneDef)
			continue;

		for(j=eaSize(&pDefDoc->pCurrentGeoDef->eaCategories)-1; j>=0; --j) {
			PCCategory *pCat = GET_REF(pDefDoc->pCurrentGeoDef->eaCategories[j]->hCategory);
			PCRegion *pRegion = GET_REF(pBoneDef->hRegion);
			if (pRegion) {
				bool bFound = false;
				for(k=eaSize(&pRegion->eaCategories)-1; k>=0; --k) {
					if (GET_REF(pRegion->eaCategories[k]->hCategory) == pCat) {
						bFound = true;
						break;
					}
				}
				if (bFound) {
					eaPush(&s_eaValidBones, pBoneDef);
				}
			}
		}
	}

	strcpy_s(pcBaseNameBuf, 260, pDefDoc->pCurrentGeoDef->pcName);
	boneRefString = REF_STRING_FROM_HANDLE(pDefDoc->pCurrentGeoDef->hBone);
	if (boneRefString)
	{
		ANALYSIS_ASSUME(boneRefString);
		pcEndOfBaseName = strstri(pcBaseNameBuf, boneRefString);
	}

	if (!pcEndOfBaseName)
		return;

	if (pcEndOfBaseName)
		*(pcEndOfBaseName-1) = '\0';

	costumeDefEdit_CheckoutGeoDef(pDefDoc->pCurrentGeoDef->pcName);

	eaClear(&s_eaGeosToSave);
	for (i = 0; i < eaSize(&s_eaValidBones); i++)
	{
		if (s_eaValidBones[i] != GET_REF(pDefDoc->pCurrentGeoDef->hBone))
		{
			sprintf(buf, "%s_%s", pcBaseNameBuf, s_eaValidBones[i]->pcName);
			costumeDefEdit_CheckoutGeoDef(buf);
		}
	}
}

static void costumeDefEdit_UICreateDupsOnBones(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc)
{
	const int * const *peaiRows;
	char pcBaseNameBuf[260];
	const char *boneRefString;
	char* pcEndOfBaseName = NULL;
	int i;
	char buf[260];

	if (!pDefDoc->pOrigGeoDef) {
		return; // Bad State
	}

	strcpy_s(pcBaseNameBuf, 260, pDefDoc->pCurrentGeoDef->pcName);
	boneRefString = REF_STRING_FROM_HANDLE(pDefDoc->pCurrentGeoDef->hBone);
	if (boneRefString)
	{
		ANALYSIS_ASSUME(boneRefString);
		pcEndOfBaseName = strstri(pcBaseNameBuf, boneRefString);
	}
	
	//append the bone name if it isn't there already
	//If the original name didn't contain the bone name, we'll rename it so it conforms.

	if (pDefDoc->pGeoDefToSave)
		StructDestroy(parse_PCGeometryDef, pDefDoc->pGeoDefToSave);

	pDefDoc->pGeoDefToSave = StructClone(parse_PCGeometryDef, pDefDoc->pCurrentGeoDef);
	pDefDoc->pGeoDefToSaveOrig = StructClone(parse_PCGeometryDef, pDefDoc->pOrigGeoDef);

	if (!pcEndOfBaseName)
	{
		pDefDoc->bSaveRename = true;
		sprintf(buf, "%s_%s", pcBaseNameBuf, REF_STRING_FROM_HANDLE(pDefDoc->pGeoDefToSave->hBone));
		pDefDoc->pGeoDefToSave->pcName = allocAddString(buf);

	}

	if (pcEndOfBaseName)
		*(pcEndOfBaseName-1) = '\0';

	if (pDefDoc->pGeoDefToSaveOrig && !pDefDoc->bSaveRename && !resIsWritable(g_hCostumeGeometryDict, pDefDoc->pGeoDefToSave->pcName))
	{
		costumeDefEdit_NotCheckedOutError(pDefDoc);
		pDefDoc->bSaveRename = false;
		return;
	}
	pDefDoc->saveRequested[SaveType_GeoDef] = true;
	pDefDoc->bGeoDefSaved = false;
	costumeDefEdit_SaveSubDoc(pDefDoc);

	eaClear(&s_eaGeosToSave);
	peaiRows = ui_ListGetSelectedRows(pDefDoc->pDupForBonesList);
	for (i = 0; i < eaiSize(peaiRows); i++)
	{
		if (s_eaValidBones[(*peaiRows)[i]] != GET_REF(pDefDoc->pCurrentGeoDef->hBone))
		{
			PCGeometryDef* pDup = StructClone(parse_PCGeometryDef, pDefDoc->pCurrentGeoDef);
			SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, s_eaValidBones[(*peaiRows)[i]], pDup->hBone);
			sprintf(buf, "%s_%s", pcBaseNameBuf, s_eaValidBones[(*peaiRows)[i]]->pcName);
			pDup->pcName = allocAddString(buf);
			eaPush(&s_eaGeosToSave, pDup);
		}
	}
	EditorPrefStoreWindowPosition(COSTUME_EDITOR, "WindowPosition", "CreateDups", pDefDoc->pPopupWindow);

	// Hide the popup
	costumeDefEdit_HidePopup(pDefDoc);

	// Update the display
	costumeDefEdit_DefUpdateLists(pDefDoc);
}

static void costumeDefEdit_UIMenuSelectDefDupForOtherBones(UIButton *pWidget, CostumeEditDefDoc *pDefDoc)
{
	UIWindow *pWin;
	UIButton *pButton;
	UIList *pList;
	RefDictIterator iter;
	PCBoneDef* pBoneDef = NULL;
	PCBoneDef* pCurBoneDef = SAFE_GET_REF2(pDefDoc, pCurrentGeoDef, hBone);
	UILabel *pLabel;
	char buf[1024];
	int j, k;

	if (!pDefDoc || !pDefDoc->pCurrentGeoDef)
		return;

	eaClear(&s_eaValidBones);
	RefSystem_InitRefDictIterator(g_hCostumeBoneDict, &iter);
	while (pBoneDef = (PCBoneDef*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		if (pBoneDef == pCurBoneDef)
			continue;

		for(j=eaSize(&pDefDoc->pCurrentGeoDef->eaCategories)-1; j>=0; --j) {
			PCCategory *pCat = GET_REF(pDefDoc->pCurrentGeoDef->eaCategories[j]->hCategory);
			PCRegion *pRegion = GET_REF(pBoneDef->hRegion);
			if (pRegion) {
				bool bFound = false;
				for(k=eaSize(&pRegion->eaCategories)-1; k>=0; --k) {
					if (GET_REF(pRegion->eaCategories[k]->hCategory) == pCat) {
						bFound = true;
						break;
					}
				}
				if (bFound) {
					eaPush(&s_eaValidBones, pBoneDef);
				}
			}
		}
	}

	pWin = ui_WindowCreate("Create Geo Duplicates For Bones", 300, 400, 300, 350);
	pWin->widget.scale = emGetEditorScale(pDefDoc->pEditor);
	EditorPrefGetWindowPosition(COSTUME_EDITOR, "WindowPosition", "CreateDups", pWin);

	sprintf(buf, "Select desired bones to duplicate this geo def.");
	pLabel = ui_LabelCreate(buf, 0, 0);
	ui_WindowAddChild(pWin, pLabel);

	pList = ui_ListCreate(parse_PCBoneDef, &s_eaValidBones, 20);
	ui_ListAppendColumn(pList, ui_ListColumnCreateParseMessage("Bones", "displayNameMsg", NULL));
	ui_ListSetMultiselect(pList, true);
	pList->fHeaderHeight = 0;
	pList->bToggleSelect = true;
	pList->bDrawSelection = false;
	pList->eaColumns[0]->fWidth = 50;
	pList->eaColumns[0]->bShowCheckBox = true;
	ui_WidgetSetDimensionsEx(UI_WIDGET(pList), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pList), 0, 0, 28, 28);
	ui_WindowAddChild(pWin, pList);
	pDefDoc->pDupForBonesList = pList;

	pButton = ui_ButtonCreate("Create", 90, 0, costumeDefEdit_UICreateDupsOnBones, pDefDoc);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 90, 0, 0, 0, UIBottomRight);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 80);
	ui_WindowAddChild(pWin, pButton);

	pButton = ui_ButtonCreate("Cancel", 0, 0, costumeDefEdit_UICancelCreateDupsButton, pDefDoc);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 0, 0, 0, UIBottomRight);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 80);
	ui_WindowAddChild(pWin, pButton);

	pDefDoc->pPopupWindow = pWin;
	ui_WindowSetModal(pWin, true);
	ui_WindowSetClosable(pWin, false);
	ui_WindowPresent(pWin);
}


static void costumeDefEdit_UIMenuSelectDefMatClone(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc)
{
	if (!pDefDoc->pOrigMatDef) {
		return; // Bad State
	}

	// Set the name to something unique
	pDefDoc->pCurrentMatDef->pcName = costumeDefEdit_CreateUniqueName("CostumeMaterial", pDefDoc->pOrigMatDef->pcName);

	// Clear the original
	if (pDefDoc->pOrigMatDef) {
		StructDestroy(parse_PCMaterialDef, pDefDoc->pOrigMatDef);
		pDefDoc->pOrigMatDef = NULL;
	}

	// Update the display
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UIMenuSelectDefMatCopy(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc)
{
	if (gMatDefClipboard) {
		StructDestroy(parse_PCMaterialDef, gMatDefClipboard);
	}
	gMatDefClipboard = StructClone(parse_PCMaterialDef, pDefDoc->pCurrentMatDef);
}


static void costumeDefEdit_UIMenuSelectDefMatPaste(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc)
{
	const char *pcName = allocAddString(pDefDoc->pCurrentMatDef->pcName);
	const char *pcFileName = allocAddString(pDefDoc->pCurrentMatDef->pcFileName);

	// Paste from clipboard
	StructCopyAll(parse_PCMaterialDef, gMatDefClipboard, pDefDoc->pCurrentMatDef);

	// Restore name and file
	pDefDoc->pCurrentMatDef->pcName = pcName;
	pDefDoc->pCurrentMatDef->pcFileName = pcFileName;

	// Update the display
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UIMenuSelectDefTexClone(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc)
{
	if (!pDefDoc->pOrigTexDef) {
		return; // Bad State
	}

	// Set the name to something unique
	pDefDoc->pCurrentTexDef->pcName = costumeDefEdit_CreateUniqueName("CostumeTexture", pDefDoc->pOrigTexDef->pcName);

	// Clear the original
	if (pDefDoc->pOrigTexDef) {
		StructDestroy(parse_PCTextureDef, pDefDoc->pOrigTexDef);
		pDefDoc->pOrigTexDef = NULL;
	}

	// Update the display
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UIMenuSelectDefTexCopy(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc)
{
	if (gTexDefClipboard) {
		StructDestroy(parse_PCTextureDef, gTexDefClipboard);
	}
	gTexDefClipboard = StructClone(parse_PCTextureDef, pDefDoc->pCurrentTexDef);
}


static void costumeDefEdit_UIMenuSelectDefTexPaste(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc)
{
	const char *pcName = allocAddString(pDefDoc->pCurrentTexDef->pcName);
	const char *pcFileName = allocAddString(pDefDoc->pCurrentTexDef->pcFileName);

	// Paste from clipboard
	StructCopyAll(parse_PCTextureDef, gTexDefClipboard, pDefDoc->pCurrentTexDef);

	// Restore name and file
	pDefDoc->pCurrentTexDef->pcName = pcName;
	pDefDoc->pCurrentTexDef->pcFileName = pcFileName;

	// Update the display
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UIMenuSelectRemoveMat(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc)
{
	DictionaryEArrayStruct *pGeoAddStruct = resDictGetEArrayStruct(g_hCostumeGeometryAddDict);
	PCMaterialDef *pMatDef;
	int i,j;

	// Get the material
	if ((pDefDoc->iMatContextRow < 0) || (pDefDoc->iMatContextRow >= eaSize(&pDefDoc->eaDefMats))) {
		return; // Bad state
	}
	pMatDef = pDefDoc->eaDefMats[pDefDoc->iMatContextRow];
	if (!pDefDoc->pOrigGeoDef) {
		return; // Bad state
	}

	// See if it is on the geo
	for(i=eaSize(&pDefDoc->pOrigGeoDef->eaAllowedMaterialDefs)-1; i>=0; --i) {
		if (stricmp(pDefDoc->pOrigGeoDef->eaAllowedMaterialDefs[i], pMatDef->pcName) == 0) {
			// Found on the geometry, so set up for save
			pDefDoc->saveRequested[SaveType_GeoDef] = true;
			pDefDoc->bGeoDefSaved = false;
			pDefDoc->bSaveOverwrite = true;
			pDefDoc->bSaveRename = false;
			if (pDefDoc->pGeoDefToSave) {
				StructDestroy(parse_PCGeometryDef, pDefDoc->pGeoDefToSave);
			}
			pDefDoc->pGeoDefToSave = StructClone(parse_PCGeometryDef, pDefDoc->pOrigGeoDef);
			langMakeEditorCopy(parse_PCGeometryDef, pDefDoc->pGeoDefToSave, false);
			pDefDoc->pGeoDefToSaveOrig = StructClone(parse_PCGeometryDef, pDefDoc->pGeoDefToSave);
			assert(pDefDoc->pGeoDefToSave);
			eaRemove(&pDefDoc->pGeoDefToSave->eaAllowedMaterialDefs, i);

			// Actually save
			costumeDefEdit_SaveSubDoc(pDefDoc);
			return;
		}
	}

	// See if it is on a GeoAdd
	for(i=eaSize(&pGeoAddStruct->ppReferents)-1; i>=0; --i) {
		PCGeometryAdd *pAdd = pGeoAddStruct->ppReferents[i];
		if ((pAdd->pcGeoName && stricmp(pAdd->pcGeoName, pDefDoc->pOrigGeoDef->pcName) == 0) ||
			(!pAdd->pcGeoName && stricmp(pAdd->pcName, pDefDoc->pOrigGeoDef->pcName) == 0)) {
			for(j=eaSize(&pAdd->eaAllowedMaterialDefs)-1; j>=0; --j) {
				if (stricmp(pAdd->eaAllowedMaterialDefs[j], pMatDef->pcName) == 0) {
					// Found on a geometry add, so set up for save
					pDefDoc->saveRequested[SaveType_GeoAdd] = true;
					pDefDoc->bSaveOverwrite = true;
					pDefDoc->bSaveRename = false;
					if (pDefDoc->pGeoAddToSave) {
						StructDestroy(parse_PCGeometryAdd, pDefDoc->pGeoAddToSave);
					}
					pDefDoc->pGeoAddToSave = StructClone(parse_PCGeometryAdd, pAdd);
					assert(pDefDoc->pGeoAddToSave);
					eaRemove(&pDefDoc->pGeoAddToSave->eaAllowedMaterialDefs, j);

					// Actually save
					costumeDefEdit_SaveSubDoc(pDefDoc);
					return;
				}
			}
		}
	}
}


static void costumeDefEdit_UIMenuSelectRemoveTex(UIMenuItem *pItem, CostumeEditDefDoc *pDefDoc)
{
	DictionaryEArrayStruct *pMatAddStruct = resDictGetEArrayStruct(g_hCostumeMaterialAddDict);
	PCTextureDef *pTexDef;
	int i,j;

	// Get the texture
	if ((pDefDoc->iTexContextRow < 0) || (pDefDoc->iTexContextRow >= eaSize(&pDefDoc->eaDefTexs))) {
		return; // Bad state
	}
	pTexDef = pDefDoc->eaDefTexs[pDefDoc->iTexContextRow];
	if (!pDefDoc->pOrigMatDef) {
		return; // Bad state
	}

	// See if it is on the material
	for(i=eaSize(&pDefDoc->pOrigMatDef->eaAllowedTextureDefs)-1; i>=0; --i) {
		if (stricmp(pDefDoc->pOrigMatDef->eaAllowedTextureDefs[i], pTexDef->pcName) == 0) {
			// Found on the geometry, so set up for save
			pDefDoc->saveRequested[SaveType_MatDef] = true;
			pDefDoc->bMatDefSaved = false;
			pDefDoc->bSaveOverwrite = true;
			pDefDoc->bSaveRename = false;
			if (pDefDoc->pMatDefToSave) {
				StructDestroy(parse_PCMaterialDef, pDefDoc->pMatDefToSave);
			}
			pDefDoc->pMatDefToSave = StructClone(parse_PCMaterialDef, pDefDoc->pOrigMatDef);
			langMakeEditorCopy(parse_PCMaterialDef, pDefDoc->pMatDefToSave, false);
			pDefDoc->pMatDefToSaveOrig = StructClone(parse_PCMaterialDef, pDefDoc->pMatDefToSave);
			assert(pDefDoc->pMatDefToSave);
			eaDestroy(&pDefDoc->eaTexsToAdd);
			eaRemove(&pDefDoc->pMatDefToSave->eaAllowedTextureDefs, i);

			// Actually save
			costumeDefEdit_SaveSubDoc(pDefDoc);
			return;
		}
	}

	// See if it is on a MatAdd
	for(i=eaSize(&pMatAddStruct->ppReferents)-1; i>=0; --i) {
		PCMaterialAdd *pAdd = pMatAddStruct->ppReferents[i];
		if ((pAdd->pcMatName && stricmp(pAdd->pcMatName, pDefDoc->pOrigMatDef->pcName) == 0) ||
			(!pAdd->pcMatName && stricmp(pAdd->pcName, pDefDoc->pOrigMatDef->pcName) == 0)) {
			for(j=eaSize(&pAdd->eaAllowedTextureDefs)-1; j>=0; --j) {
				if (stricmp(pAdd->eaAllowedTextureDefs[j], pTexDef->pcName) == 0) {
					// Found on a geometry add, so set up for save
					pDefDoc->saveRequested[SaveType_MatAdd] = true;
					pDefDoc->bSaveOverwrite = true;
					pDefDoc->bSaveRename = false;
					if (pDefDoc->pMatAddToSave) {
						StructDestroy(parse_PCMaterialAdd, pDefDoc->pMatAddToSave);
					}
					pDefDoc->pMatAddToSave = StructClone(parse_PCMaterialAdd, pAdd);
					assert(pDefDoc->pMatAddToSave);
					eaRemove(&pDefDoc->pMatAddToSave->eaAllowedTextureDefs, j);

					// Actually save
					costumeDefEdit_SaveSubDoc(pDefDoc);
					return;
				}
			}
		}
	}
}


static void costumeDefEdit_UIMenuPreopenGeoOption(UIMenu *pMenu, CostumeEditDefDoc *pDefDoc)
{
	pMenu->items[0]->active = (pDefDoc->pOrigGeoDef != NULL);
	pMenu->items[2]->active = (gGeoDefClipboard != NULL);
}


static void costumeDefEdit_UIMenuPreopenMatOption(UIMenu *pMenu, CostumeEditDefDoc *pDefDoc)
{
	pMenu->items[0]->active = (pDefDoc->pOrigMatDef != NULL);
	pMenu->items[2]->active = (gMatDefClipboard != NULL);
}


static void costumeDefEdit_UIMenuPreopenTexOption(UIMenu *pMenu, CostumeEditDefDoc *pDefDoc)
{
	pMenu->items[0]->active = (pDefDoc->pOrigTexDef != NULL);
	pMenu->items[2]->active = (gTexDefClipboard != NULL);
}


static void costumeDefEdit_UIOpenMaterials(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	pDefDoc->pMatEditWin->show = true;
	ui_WindowPresent(pDefDoc->pMatEditWin);
}


static void costumeDefEdit_UIOpenTextures(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	pDefDoc->pTexEditWin->show = true;
	ui_WindowPresent(pDefDoc->pTexEditWin);
}


static void costumeDefEdit_UISelectCategoryList(UIList *pList, CostumeEditDefDoc *pDefDoc)
{
	const int * const *peaiRows;
	int i, j;
	bool bFound;

	peaiRows = ui_ListGetSelectedRows(pList);
	
	// Add categories that were missing
	for(i=eaiSize(peaiRows)-1; i>=0; --i) {
		bFound = false;
		for(j=eaSize(&pDefDoc->pCurrentGeoDef->eaCategories)-1; j>=0; --j) {
			if (pDefDoc->eaCategories[(*peaiRows)[i]] == GET_REF(pDefDoc->pCurrentGeoDef->eaCategories[j]->hCategory)) {
				bFound = true;
				break;
			}
		}
		if (!bFound) {
			PCCategoryRef *pCatRef = StructCreate(parse_PCCategoryRef);
			SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, pDefDoc->eaCategories[(*peaiRows)[i]], pCatRef->hCategory);
			eaPush(&pDefDoc->pCurrentGeoDef->eaCategories, pCatRef);
		}
	}
	// Remove categories that are no longer selected
	for(i=eaSize(&pDefDoc->pCurrentGeoDef->eaCategories)-1; i>=0; --i) {
		for(j=eaiSize(peaiRows)-1; j>=0; --j) {
			if (pDefDoc->eaCategories[(*peaiRows)[j]] == GET_REF(pDefDoc->pCurrentGeoDef->eaCategories[i]->hCategory)) {
				break;
			}
		}
		if (j < 0) {
			// Not found so remove it
			eaRemove(&pDefDoc->pCurrentGeoDef->eaCategories, i);
		}
	}

	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UISelectDefBoneCombo(UIComboBox *pCombo, CostumeEditDefDoc *pDefDoc)
{
	pDefDoc->pCurrentBoneDef = (PCBoneDef*)ui_ComboBoxGetSelectedObject(pCombo);
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UISelectDefGeoCombo(UIComboBox *pCombo, CostumeEditDefDoc *pDefDoc)
{
	PCGeometryDef *pGeo = (PCGeometryDef*)ui_ComboBoxGetSelectedObject(pCombo);
	costumeDefEdit_DefSetGeo(pDefDoc, pGeo);
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UISelectDefGeoList(UIList *pList, CostumeEditDefDoc *pDefDoc)
{
	PCGeometryDef *pGeo = (PCGeometryDef*)ui_ListGetSelectedObject(pList);
	costumeDefEdit_DefSetGeo(pDefDoc, pGeo);
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UISelectDefMatCombo(UIComboBox *pCombo, CostumeEditDefDoc *pDefDoc)
{
	PCMaterialDef *pMat = (PCMaterialDef*)ui_ComboBoxGetSelectedObject(pCombo);
	costumeDefEdit_DefSetMat(pDefDoc, pMat);
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UISelectDefMatList(UIList *pList, CostumeEditDefDoc *pDefDoc)
{
	PCMaterialDef *pMat = (PCMaterialDef*)ui_ListGetSelectedObject(pList);
	costumeDefEdit_DefSetMat(pDefDoc, pMat);
	costumeDefEdit_DefUpdateLists(pDefDoc);
}


static void costumeDefEdit_UISelectDefTexList(UIList *pList, CostumeEditDefDoc *pDefDoc)
{
	PCTextureDef *pTex = (PCTextureDef*)ui_ListGetSelectedObject(pList);
	costumeDefEdit_DefSetTex(pDefDoc, pTex);
	costumeDefEdit_DefUpdateLists(pDefDoc);
}

//---------------------------------------------------------------------------------------------------
// Save Logic
//---------------------------------------------------------------------------------------------------


static void costumeDefEdit_SaveSubDoc(CostumeEditDefDoc *pDefDoc)
{
	EMEditorDoc *pDoc = emGetActiveEditorDoc();
	if (pDoc) {
		eaClear(&pDoc->sub_docs);
		pDefDoc->emSubDoc.saved = false;
		eaPush(&pDoc->sub_docs, (EMEditorSubDoc*)pDefDoc);
		emSaveSubDoc(pDoc, (EMEditorSubDoc*)pDefDoc);
	}
}


static void costumeDefEdit_EndSave(CostumeEditDefDoc *pDefDoc)
{
	int i;
	EMEditorDoc *pDoc = emGetActiveEditorDoc();
	if (pDoc) {
		eaClear(&pDoc->sub_docs);
	}
	pDefDoc->emSubDoc.saved = true;
	pDefDoc->bSaveOverwrite = false;
	pDefDoc->bSaveRename = false;
	pDefDoc->bSaveAsNew = false;
	for ( i=0; i < SaveType_NumSaveTypes; i++ )
		pDefDoc->saveRequested[i] = false;

}


static void costumeDefEdit_UIDismissWindow(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	EditorPrefStoreWindowPosition(COSTUME_EDITOR, "Window Position", "Save Confirm", pGlobalWindow);

	// Free the window
	ui_WindowHide(pGlobalWindow);
	ui_WidgetQueueFreeAndNull(&pGlobalWindow);

	// End save
	costumeDefEdit_EndSave(pDefDoc);
}


static void costumeDefEdit_UISaveOverwrite(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	costumeDefEdit_UIDismissWindow(pButton, pDefDoc);

	pDefDoc->bSaveOverwrite = true;
	pDefDoc->bSaveAsNew = true;
	pDefDoc->saveRequested[pDefDoc->eSavingPromptWindowType] = true;

	// Restart save
	costumeDefEdit_SaveSubDoc(pDefDoc);
}


static void costumeDefEdit_UISaveRename(UIButton *pButton, CostumeEditDefDoc *pDefDoc)
{
	costumeDefEdit_UIDismissWindow(pButton, pDefDoc);

	pDefDoc->bSaveRename = true;
	pDefDoc->bSaveAsNew = false;
	pDefDoc->saveRequested[pDefDoc->eSavingPromptWindowType] = true;

	// Restart save
	costumeDefEdit_SaveSubDoc(pDefDoc);
}


static void costumeDefEdit_PromptForSave(CostumeEditDoc *pDoc, CostumeEditDefDoc *pDefDoc, const char *pcName, bool bNameCollision, bool bNameChanged, CostumeSaveType eSaveType)
{
	UILabel *pLabel;
	UIButton *pButton;
	char buf[1024];
	int y = 0;
	int width = 0;
	int x = 0;
	bool bAllowRename = (eSaveType == SaveType_GeoDef);

	pDefDoc->eSavingPromptWindowType = eSaveType;

	pGlobalWindow = ui_WindowCreate("Confirm Save?", 200, 200, 300, 60);

	EditorPrefGetWindowPosition(COSTUME_EDITOR, "Window Position", "Save Confirm", pGlobalWindow);

	if (bNameChanged && bAllowRename) {
		sprintf(buf, "The definition name was changed to a new name.  Did you want to rename or save as new?");
		pLabel = ui_LabelCreate(buf, 0, y);
		ui_WindowAddChild(pGlobalWindow, pLabel);
		width = MAX(width, pLabel->widget.width + 20);
		y += 28;
	} else if (bNameChanged) {
		sprintf(buf, "The definition name was changed to a new name.  Did you want to save as new?");
		pLabel = ui_LabelCreate(buf, 0, y);
		ui_WindowAddChild(pGlobalWindow, pLabel);
		width = MAX(width, pLabel->widget.width + 20);
		y += 28;
	}

	if (bNameCollision) {
		sprintf(buf, "The definition name '%s' is already in use.  Did you want to overwrite it?", pcName);
		pLabel = ui_LabelCreate(buf, 0, y);
		ui_WindowAddChild(pGlobalWindow, pLabel);
		width = MAX(width, pLabel->widget.width + 20);
		y += 28;
	}
	
	if (bAllowRename) {
		pLabel = ui_LabelCreate("NOTE: Renaming does not fix up all references so real errors may occur", 0, y);
		ui_WindowAddChild(pGlobalWindow, pLabel);
		width = MAX(width, pLabel->widget.width + 20);
		y += 28;
	}

	if (bNameChanged) {
		if (bNameCollision) {
			pButton = ui_ButtonCreate("Save As New AND Overwrite", 0, 28, costumeDefEdit_UISaveOverwrite, pDefDoc);
			ui_WidgetSetWidth(UI_WIDGET(pButton), 200);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), bAllowRename ? -260 : -155, y, 0.5, 0, UITopLeft);
			ui_WindowAddChild(pGlobalWindow, pButton);

			if (bAllowRename) {
				pButton = ui_ButtonCreate("Rename AND Overwrite", 0, 28, costumeDefEdit_UISaveRename, pDefDoc);
				ui_WidgetSetWidth(UI_WIDGET(pButton), 200);
				ui_WidgetSetPositionEx(UI_WIDGET(pButton), -50, y, 0.5, 0, UITopLeft);
				ui_WindowAddChild(pGlobalWindow, pButton);
			}

			x = bAllowRename ? 160 : 55;
			width = MAX(width, 540);
		} else {
			pButton = ui_ButtonCreate("Save As New", 0, 0, costumeDefEdit_UISaveOverwrite, pDefDoc);
			ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), bAllowRename ? -160 : -105, y, 0.5, 0, UITopLeft);
			ui_WindowAddChild(pGlobalWindow, pButton);

			if (bAllowRename) {
				pButton = ui_ButtonCreate("Rename", 0, 0, costumeDefEdit_UISaveRename, pDefDoc);
				ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
				ui_WidgetSetPositionEx(UI_WIDGET(pButton), -50, y, 0.5, 0, UITopLeft);
				ui_WindowAddChild(pGlobalWindow, pButton);
			}

			x = bAllowRename ? 60 : 5;
			width = MAX(width, 340);
		}
	} else {
		pButton = ui_ButtonCreate("Overwrite", 0, 0, costumeDefEdit_UISaveOverwrite, pDefDoc);
		ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), -105, y, 0.5, 0, UITopLeft);
		ui_WindowAddChild(pGlobalWindow, pButton);

		x = 5;
		width = MAX(width, 230);
	}

	pButton = ui_ButtonCreate("Cancel", 0, 0, costumeDefEdit_UIDismissWindow, pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.5, 0, UITopLeft);
	ui_WindowAddChild(pGlobalWindow, pButton);

	y += 28;

	pGlobalWindow->widget.width = width;
	pGlobalWindow->widget.height = y;

	ui_WindowSetClosable(pGlobalWindow, false);
	ui_WindowSetModal(pGlobalWindow, true);

	ui_WindowPresent(pGlobalWindow);
}


bool costumeDefEdit_SaveDefContinue(EMEditor *pEditor, const char *pcName, CostumeEditDefDoc *pDefDoc, EMResourceState eState, void *pData, bool bSuccess)
{
	if (bSuccess && (eState == EMRES_STATE_SAVE_SUCCEEDED)) {

		if(pDefDoc->saveRequested[SaveType_GeoDef]) {
			PCGeometryDef *pGeo;

			pDefDoc->bGeoDefSaved = true;
			pGeo = RefSystem_ReferentFromString(g_hCostumeGeometryDict, pcName);
			if (pGeo) {
				costumeDefEdit_DefSetGeo(pDefDoc, pGeo);
			}
		}

		if(pDefDoc->saveRequested[SaveType_MatDef]) {
			PCMaterialDef *pMat;

			pDefDoc->bMatDefSaved = true;
			pMat = RefSystem_ReferentFromString(g_hCostumeMaterialDict, pcName);
			if (pMat) {
				costumeDefEdit_DefSetMat(pDefDoc, pMat);
			}
			// Now add to parent as allowed
			if (pDefDoc->pOrigGeoDef) {
				pDefDoc->pcGeoForMatsToAdd = strdup(pDefDoc->pOrigGeoDef->pcName);
				eaDestroy(&pDefDoc->eaMatsToAdd);
				eaPush(&pDefDoc->eaMatsToAdd, strdup(pDefDoc->pCurrentMatDef->pcName));
				costumeDefEdit_AddMaterialsToGeo(pDefDoc, 0, NULL);
			}
		}

		if(pDefDoc->saveRequested[SaveType_TexDef]) {
			PCTextureDef *pTex;

			pDefDoc->bTexDefSaved = true;
			pTex = RefSystem_ReferentFromString(g_hCostumeTextureDict, pcName);
			if (pTex) {
				costumeDefEdit_DefSetTex(pDefDoc, pTex);
			}

			// Now add to parent as allowed
			if (pDefDoc->pOrigMatDef) {
				pDefDoc->pcMatForTexsToAdd = strdup(pDefDoc->pOrigMatDef->pcName);
				eaDestroy(&pDefDoc->eaTexsToAdd);
				eaPush(&pDefDoc->eaTexsToAdd, strdup(pDefDoc->pCurrentTexDef->pcName));
				costumeDefEdit_AddTexturesToMaterial(pDefDoc, 0, NULL);
			}
		}
	}
	return true;
}


static bool costumeDefEdit_DeleteDefContinue(EMEditor *pEditor, const char *pcName, DeleteInfoStruct *pDelInfo, EMResourceState eState, void *unused, bool bSuccess)
{
	if (bSuccess && (eState == EMRES_STATE_LOCK_SUCCEEDED)) {
		// Since we got the lock, continue by doing the delete save
		emSetResourceStateWithData(pEditor, pcName, EMRES_STATE_DELETING, pDelInfo);
		resRequestSaveResource(pDelInfo->hDict, pDelInfo->pcName, NULL);
	}

	return true;
}


static void costumeDefEdit_DeleteDefStart(EMEditor *pEditor, const char *pcName, void *pDef, 
										  ParseTable *pParseTable, DictionaryHandle hDict)
{
	DeleteInfoStruct *pDelInfo = calloc(1, sizeof(DeleteInfoStruct));
	pDelInfo->pDef = StructCloneVoid(pParseTable, pDef);
	pDelInfo->hDict = hDict;
	pDelInfo->pcName = strdup(pcName);

	resSetDictionaryEditMode(hDict, true);
	resSetDictionaryEditMode(gMessageDict, true);
	resRequestLockResource(hDict, pcName, pDef);

	// Go into lock state if we don't already have the lock
	if (!resGetLockOwner(hDict, pcName)) {
		emSetResourceStateWithData(pEditor, pcName, EMRES_STATE_LOCKING_FOR_DELETE, pDelInfo);
		//printf("Locking %s\n", pcName); // DEBUG
		return;
	}

	// Otherwise continue the delete
	costumeDefEdit_DeleteDefContinue(pEditor, pcName, pDelInfo, EMRES_STATE_LOCK_SUCCEEDED, NULL, true);
}


static EMTaskStatus costumeDefEdit_GenericSave(CostumeEditDoc *pDoc, CostumeEditDefDoc *pDefDoc,
										void *pNewDef, void **ppOrigDef, 
										const char *pcNewName, const char *pcOrigName,
										const char *pcNewFileName, const char *pcOrigFileName,
										ParseTable *pParseTable, DictionaryHandle hDict, bool *pbSaved, CostumeSaveType eSaveType)
{
	// Make sure the name is valid
	if (!resIsValidName(pcNewName)) {
		ui_DialogPopup("Failed to Save", "The definition name is not legal");
		return EM_TASK_FAILED;
	}

	// Prompt if name already in use (new costume or save as new)
	if (!pDefDoc->bSaveOverwrite && (!ppOrigDef || !(*ppOrigDef) || pDefDoc->bSaveAsNew) && RefSystem_ReferentFromString(hDict, pcNewName)) {
		costumeDefEdit_PromptForSave(pDoc, pDefDoc, pcNewName, true, false, eSaveType);
		return EM_TASK_FAILED;
	} else if (!pDefDoc->bSaveRename && !pDefDoc->bSaveOverwrite &&
			   ppOrigDef && (*ppOrigDef) && (stricmp(pcOrigName,pcNewName) != 0)) {
		// Name changed and may have collision
		costumeDefEdit_PromptForSave(pDoc, pDefDoc, pcNewName, (RefSystem_ReferentFromString(hDict, pcNewName) != NULL), true, eSaveType);
		return EM_TASK_FAILED;
	}

	resSetDictionaryEditMode(hDict, true);
	resSetDictionaryEditMode(gMessageDict, true);
	resRequestLockResource(hDict, pcNewName, pNewDef);

	// LOCKING START

	// Check the lock
	if (!resGetLockOwner(hDict, pcNewName)) {
		// Don't have lock, so ask server to lock and go into locking state
		emSetResourceState(pDoc->emDoc.editor, pcNewName, EMRES_STATE_LOCKING_FOR_SAVE);
		return EM_TASK_INPROGRESS;
	}
	// Get here if have the main lock... now check for rename lock
	if (!pDefDoc->bSaveAsNew && ppOrigDef && *ppOrigDef &&
		(stricmp(pcOrigName,pcNewName) != 0)) {
			costumeDefEdit_DeleteDefStart(pDoc->emDoc.editor, pcOrigName, ppOrigDef ? *ppOrigDef : NULL, pParseTable, hDict);
	}

	// LOCKING END

	// SAVE START

	if (pDefDoc->bSaveAsNew && ppOrigDef) {
		// When save as new, remove original so revert acts on new
		StructDestroyVoid(pParseTable, *ppOrigDef);
		*ppOrigDef = NULL;
	}

	// Send save to server
	emSetResourceStateWithData(pDoc->emDoc.editor, pcNewName, EMRES_STATE_SAVING, pDefDoc);
	resRequestSaveResource(hDict, pcNewName, pNewDef);
	return EM_TASK_INPROGRESS;
}


EMTaskStatus costumeDefEdit_SaveGeoDef(CostumeEditDoc *pDoc, CostumeEditDefDoc *pDefDoc)
{
	char buf[1024];
	const char *pcTerseKey;
	int i = 0, j;
	const char** eaChildNames = NULL;
	PCGeometryRef* pGeoRef = NULL;
	PCGeometryDef* pGeoDef = NULL;
	PCGeometryDef* pDefaultGeoDef;
	// VALIDATION LOGIC START

	// Fix display name
	sprintf(buf, "Costume.Geometry.%s", pDefDoc->pGeoDefToSave->pcName);
	pcTerseKey = msgCreateUniqueKey(MKP_COSTUME_GEO, buf, pDefDoc->pGeoDefToSave->displayNameMsg.pEditorCopy->pcMessageKey);
	if (stricmp(pcTerseKey, pDefDoc->pGeoDefToSave->displayNameMsg.pEditorCopy->pcMessageKey) != 0) {
		pDefDoc->pGeoDefToSave->displayNameMsg.pEditorCopy->pcMessageKey = pcTerseKey;
	}

	sprintf(buf, "Costume geometry name for %s", pDefDoc->pGeoDefToSave->pcName);
	if (stricmp(buf, pDefDoc->pGeoDefToSave->displayNameMsg.pEditorCopy->pcDescription)) {
		StructFreeStringSafe(&pDefDoc->pGeoDefToSave->displayNameMsg.pEditorCopy->pcDescription);
		pDefDoc->pGeoDefToSave->displayNameMsg.pEditorCopy->pcDescription = StructAllocString(buf);
	}
	
	if (pDefDoc->pGeoDefToSave->pOptions)
	{
		// If the default child geo is set to "None", then remove it
		for(i=eaSize(&pDefDoc->pGeoDefToSave->pOptions->eaChildGeos)-1; i>=0; --i) {
			pDefaultGeoDef = GET_REF(pDefDoc->pGeoDefToSave->pOptions->eaChildGeos[i]->hDefaultChildGeo);
			if (pDefaultGeoDef && stricmp("None", pDefaultGeoDef->pcName) == 0) {
				REMOVE_HANDLE(pDefDoc->pGeoDefToSave->pOptions->eaChildGeos[i]->hDefaultChildGeo);
			}
		}

		for(i=eaSize(&pDefDoc->pGeoDefToSave->pOptions->eaChildGeos)-1; i>=0; --i) {
			PCGeometryChildDef *pChildInfo = pDefDoc->pGeoDefToSave->pOptions->eaChildGeos[i];

			// If the geometry required a child geometry, then at least one must be specified
			if (pChildInfo->bRequiresChild && eaSize(&pChildInfo->eaChildGeometries) == 0) {
				ui_DialogPopup("Failed to Save", "This definition requires that at least one child geometry be specified for it.");
				return EM_TASK_FAILED;
			}
			// If nothing interesting specified the remove child struct
			if (!eaSize(&pChildInfo->eaChildGeometries) && !pChildInfo->bRequiresChild && !GET_REF(pChildInfo->hDefaultChildGeo)) {
				StructDestroy(parse_PCGeometryChildDef, pChildInfo);
				eaRemove(&pDefDoc->pGeoDefToSave->pOptions->eaChildGeos, i);
			}
		}

		// Compile a list of all unique child geometries specified for this geometry def
		for(i=eaSize(&pDefDoc->pGeoDefToSave->pOptions->eaChildGeos)-1; i>=0; --i) {
			for (j = 0; j < eaSize(&pDefDoc->pGeoDefToSave->pOptions->eaChildGeos[i]->eaChildGeometries); j++) {
				pGeoRef = pDefDoc->pGeoDefToSave->pOptions->eaChildGeos[i]->eaChildGeometries[j];
				pGeoDef = (pGeoRef ? GET_REF(pGeoRef->hGeo) : NULL);
				if (pGeoDef && stricmp(pGeoDef->pcName, "None") != 0) {
					eaPushUnique(&eaChildNames, pGeoDef->pcName);	// filter out duplicates
				}
				else {
					ui_DialogPopup("Failed to Save", "Empty or 'none' child geometry definitions are not permitted.");
					return EM_TASK_FAILED;
				}
			}

			// Prohibit duplicate child geometries
			if (eaSize(&pDefDoc->pGeoDefToSave->pOptions->eaChildGeos[i]->eaChildGeometries) != eaSize(&eaChildNames)) {
				ui_DialogPopup("Failed to Save", "Duplicate child geometry definitions are not permitted.");
				return EM_TASK_FAILED;
			}

			// The default child geometry, if one is specified, must be one of the child geo defs given
			pDefaultGeoDef = GET_REF(pDefDoc->pGeoDefToSave->pOptions->eaChildGeos[i]->hDefaultChildGeo);
			if (pDefaultGeoDef && (eaFindString(&eaChildNames, pDefaultGeoDef->pcName) == -1)) {
				ui_DialogPopup("Failed to Save", "The default child geometry is not among the child geometries specified for this definition.");
				return EM_TASK_FAILED;
			}

			// cleanup
			eaDestroy(&eaChildNames);
		}
	}

	// Chop off unused parts of the structure.

	if (pDefDoc->pGeoDefToSave && pDefDoc->pGeoDefToSave->pClothData && !pDefDoc->pGeoDefToSave->pClothData->bIsCloth) {
		StructDestroySafe(parse_PCGeometryClothData, &pDefDoc->pGeoDefToSave->pClothData);
	}

	if (pDefDoc->pGeoDefToSave
		&& pDefDoc->pGeoDefToSave->pOptions
		&& !(pDefDoc->pGeoDefToSave->pOptions->bIsChild
		|| eaSize(&pDefDoc->pGeoDefToSave->pOptions->eaChildGeos)
		|| eaSize(&pDefDoc->pGeoDefToSave->pOptions->eaFX)
		|| eaSize(&pDefDoc->pGeoDefToSave->pOptions->eaFXSwap)
		|| GET_REF(pDefDoc->pGeoDefToSave->pOptions->hDamageFxInfo)
		|| SAFE_DEREF(pDefDoc->pGeoDefToSave->pOptions->pcSubBone)
		|| SAFE_DEREF(pDefDoc->pGeoDefToSave->pOptions->pcSubSkeleton)
		|| GET_REF(pDefDoc->pGeoDefToSave->pOptions->hBodyColorSet0)
		|| GET_REF(pDefDoc->pGeoDefToSave->pOptions->hBodyColorSet1)
		|| GET_REF(pDefDoc->pGeoDefToSave->pOptions->hBodyColorSet2)
		|| GET_REF(pDefDoc->pGeoDefToSave->pOptions->hBodyColorSet3))) {
			StructDestroySafe(parse_PCGeometryOptions, &pDefDoc->pGeoDefToSave->pOptions);
	}

	// VALIDATION LOGIC END

	// Save off file name as a pref if this is a new def
	if (!pDefDoc->pOrigGeoDef) {
		EditorPrefStoreString(COSTUME_EDITOR, "FilePref", "GeoDef", pDefDoc->pCurrentGeoDef->pcFileName);
	}

	return costumeDefEdit_GenericSave(pDoc, pDefDoc, 
									pDefDoc->pGeoDefToSave, &pDefDoc->pGeoDefToSaveOrig,
									pDefDoc->pGeoDefToSave->pcName, pDefDoc->pGeoDefToSaveOrig ? pDefDoc->pGeoDefToSaveOrig->pcName : NULL,
									pDefDoc->pGeoDefToSave->pcFileName, pDefDoc->pGeoDefToSaveOrig ? pDefDoc->pGeoDefToSaveOrig->pcFileName : NULL,
									parse_PCGeometryDef, g_hCostumeGeometryDict, &pDefDoc->bGeoDefSaved, SaveType_GeoDef);
}


EMTaskStatus costumeDefEdit_SaveMatDef(CostumeEditDoc *pDoc, CostumeEditDefDoc *pDefDoc)
{
	EMTaskStatus status;
	char buf[1024];
	const char *pcTerseKey;

	// VALIDATION LOGIC START

	// Clean out any "None" values on defaults (if any)
	if (REF_STRING_FROM_HANDLE(pDefDoc->pMatDefToSave->hDefaultPattern) && stricmp("None", REF_STRING_FROM_HANDLE(pDefDoc->pMatDefToSave->hDefaultPattern)) == 0) {
		REMOVE_HANDLE(pDefDoc->pMatDefToSave->hDefaultPattern);
	}
	if (REF_STRING_FROM_HANDLE(pDefDoc->pMatDefToSave->hDefaultDetail) && stricmp("None", REF_STRING_FROM_HANDLE(pDefDoc->pMatDefToSave->hDefaultDetail)) == 0) {
		REMOVE_HANDLE(pDefDoc->pMatDefToSave->hDefaultDetail);
	}
	if (REF_STRING_FROM_HANDLE(pDefDoc->pMatDefToSave->hDefaultSpecular) && stricmp("None", REF_STRING_FROM_HANDLE(pDefDoc->pMatDefToSave->hDefaultSpecular)) == 0) {
		REMOVE_HANDLE(pDefDoc->pMatDefToSave->hDefaultSpecular);
	}
	if (REF_STRING_FROM_HANDLE(pDefDoc->pMatDefToSave->hDefaultDiffuse) && stricmp("None", REF_STRING_FROM_HANDLE(pDefDoc->pMatDefToSave->hDefaultDiffuse)) == 0) {
		REMOVE_HANDLE(pDefDoc->pMatDefToSave->hDefaultDiffuse);
	}
	if (REF_STRING_FROM_HANDLE(pDefDoc->pMatDefToSave->hDefaultMovable) && stricmp("None", REF_STRING_FROM_HANDLE(pDefDoc->pMatDefToSave->hDefaultMovable)) == 0) {
		REMOVE_HANDLE(pDefDoc->pMatDefToSave->hDefaultMovable);
	}

	// Fix display name
	sprintf(buf, "Costume.Material.%s", pDefDoc->pMatDefToSave->pcName);
	pcTerseKey = msgCreateUniqueKey(MKP_COSTUME_MAT, buf, pDefDoc->pMatDefToSave->displayNameMsg.pEditorCopy->pcMessageKey);
	if (stricmp(pcTerseKey, pDefDoc->pMatDefToSave->displayNameMsg.pEditorCopy->pcMessageKey) != 0) {
		pDefDoc->pMatDefToSave->displayNameMsg.pEditorCopy->pcMessageKey = pcTerseKey;
	}

	sprintf(buf, "Costume material name for %s", pDefDoc->pMatDefToSave->pcName);
	if (stricmp(buf, pDefDoc->pMatDefToSave->displayNameMsg.pEditorCopy->pcDescription)) {
		StructFreeStringSafe(&pDefDoc->pMatDefToSave->displayNameMsg.pEditorCopy->pcDescription);
		pDefDoc->pMatDefToSave->displayNameMsg.pEditorCopy->pcDescription = StructAllocString(buf);
	}

	// Chop off unused parts of the structure.

	if (pDefDoc->pMatDefToSave
		&& pDefDoc->pMatDefToSave->pOptions
		&& StructIsZero(pDefDoc->pMatDefToSave->pOptions)) {
			StructDestroySafe(parse_PCMaterialOptions, &pDefDoc->pMatDefToSave->pOptions);
	}
	if (pDefDoc->pMatDefToSave
		&& pDefDoc->pMatDefToSave->pColorOptions
		&& StructIsZero(pDefDoc->pMatDefToSave->pColorOptions)) {
			StructDestroySafe(parse_PCMaterialColorOptions, &pDefDoc->pMatDefToSave->pColorOptions);
	}

	// VALIDATION LOGIC END

	// Save off file name as a pref if this is a new def
	if (!pDefDoc->pOrigMatDef) {
		EditorPrefStoreString(COSTUME_EDITOR, "FilePref", "MatDef", pDefDoc->pCurrentMatDef->pcFileName);
	}

	status = costumeDefEdit_GenericSave(pDoc, pDefDoc, 
									pDefDoc->pMatDefToSave, &pDefDoc->pMatDefToSaveOrig,
									pDefDoc->pMatDefToSave->pcName, pDefDoc->pMatDefToSaveOrig ? pDefDoc->pMatDefToSaveOrig->pcName : NULL,
									pDefDoc->pMatDefToSave->pcFileName, pDefDoc->pMatDefToSaveOrig ? pDefDoc->pMatDefToSaveOrig->pcFileName : NULL,
									parse_PCMaterialDef, g_hCostumeMaterialDict, &pDefDoc->bMatDefSaved, SaveType_MatDef);

	// now that we have saved put the 'empty' parts back in as they are assumed to always be nonnull									
	if(pDefDoc->pMatDefToSave && !pDefDoc->pMatDefToSave->pOptions)
	{
		pDefDoc->pMatDefToSave->pOptions = StructCreate(parse_PCMaterialOptions);
	}
	if(pDefDoc->pMatDefToSave && !pDefDoc->pMatDefToSave->pColorOptions)
	{
		pDefDoc->pMatDefToSave->pColorOptions = StructCreate(parse_PCMaterialColorOptions);
	}

	if (status == EM_TASK_SUCCEEDED) {
		// Now add to parent as allowed (won't happen if gConf.bServerSaving)
		if (pDefDoc->pOrigGeoDef) {
			pDefDoc->pcGeoForMatsToAdd = strdup(pDefDoc->pOrigGeoDef->pcName);
			eaDestroy(&pDefDoc->eaMatsToAdd);
			eaPush(&pDefDoc->eaMatsToAdd, strdup(pDefDoc->pCurrentMatDef->pcName));
			costumeDefEdit_AddMaterialsToGeo(pDefDoc, 0, NULL);
		}
	}

	return status;
}

EMTaskStatus costumeDefEdit_SaveTexDef(CostumeEditDoc *pDoc, CostumeEditDefDoc *pDefDoc)
{
	EMTaskStatus status;
	PCTextureType flags;
	char buf[1024];
	const char *pcTerseKey;
	int i;

	// VALIDATION LOGIC START

	// Fail out if flags are not set or not compatible
	flags = pDefDoc->pTexDefToSave->eTypeFlags;
	if (!flags) {
		ui_DialogPopup("Failed to Save", "The definition must have a type");
		return EM_TASK_FAILED;
	}
	for(i=eaSize(&pDefDoc->pTexDefToSave->eaExtraSwaps)-1; i>=0; --i) {
		if (pDefDoc->pTexDefToSave->eaExtraSwaps[i]->eTypeFlags < pDefDoc->pTexDefToSave->eTypeFlags) {
			ui_DialogPopup("Failed to Save", "An extra texture swap uses a type that is of higher priority than the main swap.  The priority order is: Pattern, Detail, Specular, Diffuse, Other.");
			return EM_TASK_FAILED;
		}
		if (flags & pDefDoc->pTexDefToSave->eaExtraSwaps[i]->eTypeFlags) {
			ui_DialogPopup("Failed to Save", "An extra texture swap re-uses a type.  All swaps on a single definition must have unique types.");
			return EM_TASK_FAILED;
		}
		// Don't count "Other" flags on the extra swaps for purposes of priority and duplication
		if (pDefDoc->pTexDefToSave->eaExtraSwaps[i]->eTypeFlags != kPCTextureType_Other) {
			flags |= pDefDoc->pTexDefToSave->eaExtraSwaps[i]->eTypeFlags;
		}
	}

	// Clean out incomplete extra texture defs
	for(i=eaSize(&pDefDoc->pTexDefToSave->eaExtraSwaps)-1; i>=0; --i) {
		if (!pDefDoc->pTexDefToSave->eaExtraSwaps[i]->pcOrigTexture || !strlen(pDefDoc->pTexDefToSave->eaExtraSwaps[i]->pcOrigTexture) ||
			!pDefDoc->pTexDefToSave->eaExtraSwaps[i]->pcNewTexture || !strlen(pDefDoc->pTexDefToSave->eaExtraSwaps[i]->pcNewTexture) ||
			(pDefDoc->pTexDefToSave->eaExtraSwaps[i]->eTypeFlags == 0)) {
			StructDestroy(parse_PCExtraTexture, pDefDoc->pTexDefToSave->eaExtraSwaps[i]);
			eaRemove(&pDefDoc->pTexDefToSave->eaExtraSwaps, i);
		}
	}

	// Fix display name
	sprintf(buf, "Costume.Texture.%s", pDefDoc->pTexDefToSave->pcName);
	pcTerseKey = msgCreateUniqueKey(MKP_COSTUME_TEX, buf, pDefDoc->pTexDefToSave->displayNameMsg.pEditorCopy->pcMessageKey);
	if (stricmp(pcTerseKey, pDefDoc->pTexDefToSave->displayNameMsg.pEditorCopy->pcMessageKey) != 0) {
		pDefDoc->pTexDefToSave->displayNameMsg.pEditorCopy->pcMessageKey = pcTerseKey;
	}

	sprintf(buf, "Costume texture name for %s", pDefDoc->pTexDefToSave->pcName);
	if (stricmp(buf, pDefDoc->pTexDefToSave->displayNameMsg.pEditorCopy->pcDescription)) {
		StructFreeStringSafe(&pDefDoc->pTexDefToSave->displayNameMsg.pEditorCopy->pcDescription);
		pDefDoc->pTexDefToSave->displayNameMsg.pEditorCopy->pcDescription = StructAllocString(buf);
	}

	// Chop off unused parts of the structure

	if (pDefDoc->pTexDefToSave && pDefDoc->pTexDefToSave->eTypeFlags != kPCTextureType_Movable) {
		StructDestroySafe(parse_PCTextureMovableOptions, &pDefDoc->pTexDefToSave->pMovableOptions);
	}
	if (pDefDoc->pTexDefToSave
		&& pDefDoc->pTexDefToSave->pValueOptions
		&& !SAFE_DEREF(pDefDoc->pTexDefToSave->pValueOptions->pcValueConstant)) {
			StructDestroySafe(parse_PCTextureValueOptions, &pDefDoc->pTexDefToSave->pValueOptions);
	}

	// VALIDATION LOGIC END

	// Save off file name as a pref if this is a new def
	if (!pDefDoc->pOrigTexDef) {
		EditorPrefStoreString(COSTUME_EDITOR, "FilePref", "TexDef", pDefDoc->pCurrentTexDef->pcFileName);
	}

	status = costumeDefEdit_GenericSave(pDoc, pDefDoc, 
									pDefDoc->pTexDefToSave, &pDefDoc->pTexDefToSaveOrig,
									pDefDoc->pTexDefToSave->pcName, pDefDoc->pTexDefToSaveOrig ? pDefDoc->pTexDefToSaveOrig->pcName : NULL,
									pDefDoc->pTexDefToSave->pcFileName, pDefDoc->pTexDefToSaveOrig ? pDefDoc->pTexDefToSaveOrig->pcFileName : NULL,
									parse_PCTextureDef, g_hCostumeTextureDict, &pDefDoc->bTexDefSaved, SaveType_TexDef);

	if (status == EM_TASK_SUCCEEDED) {
		// Now add to parent as allowed
		if (pDefDoc->pOrigMatDef) {
			pDefDoc->pcMatForTexsToAdd = strdup(pDefDoc->pOrigMatDef->pcName);
			eaDestroy(&pDefDoc->eaTexsToAdd);
			eaPush(&pDefDoc->eaTexsToAdd, strdup(pDefDoc->pCurrentTexDef->pcName));
			costumeDefEdit_AddTexturesToMaterial(pDefDoc, 0, NULL);
		}
	}
	return status;
}

EMTaskStatus costumeDefEdit_SaveGeoAdd(CostumeEditDoc *pDoc, CostumeEditDefDoc *pDefDoc)
{
	// VALIDATION LOGIC START

	// TODO: Perform pre-save validation

	// VALIDATION LOGIC END

	return costumeDefEdit_GenericSave(pDoc, pDefDoc, 
									pDefDoc->pGeoAddToSave, NULL,
									pDefDoc->pGeoAddToSave->pcName, NULL,
									pDefDoc->pGeoAddToSave->pcFileName, NULL,
									parse_PCGeometryAdd, g_hCostumeGeometryAddDict, NULL, SaveType_GeoAdd);
}

EMTaskStatus costumeDefEdit_SaveMatAdd(CostumeEditDoc *pDoc, CostumeEditDefDoc *pDefDoc)
{
	// VALIDATION LOGIC START

	// TODO: Perform pre-save validation

	// VALIDATION LOGIC END

	return costumeDefEdit_GenericSave(pDoc, pDefDoc, 
									pDefDoc->pMatAddToSave, NULL,
									pDefDoc->pMatAddToSave->pcName, NULL,
									pDefDoc->pMatAddToSave->pcFileName, NULL,
									parse_PCMaterialAdd, g_hCostumeMaterialAddDict, NULL, SaveType_MatAdd);
}

typedef EMTaskStatus (*costumeDefEdit_SaveFunction)(CostumeEditDoc *pDoc, CostumeEditDefDoc *pDefDoc);
void costumeDefEdit_SaveDefGeneric(CostumeEditDoc *pDoc, CostumeEditDefDoc *pDefDoc, const char *pcName, 
								   bool *pbSaving, costumeDefEdit_SaveFunction pSaveFunc,
								   bool *pbSomeFailed, bool *pbSomeInProgress, bool *pbSomeSucceeded)
{
	EMTaskStatus status = EM_TASK_FAILED;

	if(!(*pbSaving))
		return;

	if (!emHandleSaveResourceState(pDoc->emDoc.editor, pcName, &status)) {
		// Save state not auto-handled so continue here
		status = pSaveFunc(pDoc, pDefDoc);
	}

	switch(status)
	{
	case EM_TASK_FAILED:
		*pbSomeFailed = true;
		break;
	case EM_TASK_INPROGRESS:
		*pbSomeInProgress = true;
		break;
	case EM_TASK_SUCCEEDED:
		*pbSomeSucceeded = true;
		break;
	}
	
	if(status == EM_TASK_SUCCEEDED)
		(*pbSaving) = false;
}

// This is called by editor manager during save
EMTaskStatus costumeDefEdit_SaveDef(CostumeEditDoc *pDoc, CostumeEditDefDoc *pDefDoc)
{
	bool bSomeFailed = false;
	bool bSomeInProgress = false;
	bool bSomeSucceeded = false;

	// Deal with state changes
	costumeDefEdit_SaveDefGeneric(pDoc, pDefDoc, 
		SAFE_MEMBER(pDefDoc->pGeoDefToSave, pcName), &pDefDoc->saveRequested[SaveType_GeoDef], costumeDefEdit_SaveGeoDef, 
		&bSomeFailed, &bSomeInProgress, &bSomeSucceeded);
	costumeDefEdit_SaveDefGeneric(pDoc, pDefDoc, 
		SAFE_MEMBER(pDefDoc->pMatDefToSave, pcName), &pDefDoc->saveRequested[SaveType_MatDef], costumeDefEdit_SaveMatDef, 
		&bSomeFailed, &bSomeInProgress, &bSomeSucceeded);
	costumeDefEdit_SaveDefGeneric(pDoc, pDefDoc, 
		SAFE_MEMBER(pDefDoc->pTexDefToSave, pcName), &pDefDoc->saveRequested[SaveType_TexDef], costumeDefEdit_SaveTexDef, 
		&bSomeFailed, &bSomeInProgress, &bSomeSucceeded);
	costumeDefEdit_SaveDefGeneric(pDoc, pDefDoc, 
		SAFE_MEMBER(pDefDoc->pGeoAddToSave, pcName), &pDefDoc->saveRequested[SaveType_GeoAdd], costumeDefEdit_SaveGeoAdd, 
		&bSomeFailed, &bSomeInProgress, &bSomeSucceeded);
	costumeDefEdit_SaveDefGeneric(pDoc, pDefDoc, 
		SAFE_MEMBER(pDefDoc->pMatAddToSave, pcName), &pDefDoc->saveRequested[SaveType_MatAdd], costumeDefEdit_SaveMatAdd, 
		&bSomeFailed, &bSomeInProgress, &bSomeSucceeded);

	if (bSomeFailed || !bSomeInProgress) {
		// Clear save state because we're done
		costumeDefEdit_EndSave(pDefDoc);
		if (costumDefEdit_CheckForMorePendingGeoSaves(pDefDoc))
			bSomeInProgress = true;
		costumeDefEdit_SavePrefs(pDefDoc);
	}
	if(bSomeFailed)
		return EM_TASK_FAILED;
	if(bSomeInProgress)
		return EM_TASK_INPROGRESS;
	if(bSomeSucceeded)
		return EM_TASK_SUCCEEDED;
	return EM_TASK_FAILED;
}




//---------------------------------------------------------------------------------------------------
// UI Initialization
//---------------------------------------------------------------------------------------------------


static UIWindow *costumeDefEdit_InitGeoEditWin(CostumeEditDefDoc *pDefDoc)
{
	UIWindow *pWin;
	UILabel *pLabel;
	UIList *pList;
	UIButton *pButton;
	UIMenuButton *pMenuButton;
	UIComboBox *pCombo;
	UIExpanderGroup *pExpanderGroup;
	UIExpander *pExpander;
	UIGimmeButton *pGimmeButton;
	F32 x, y;
	MEField *pField;

	pWin = ui_WindowCreate("Geometry Definition", 700, 50, 500, 180);

	x=0;
	y=0;

	// Bone
	pLabel = ui_LabelCreate("Bone", x, y);
	ui_WindowAddChild(pWin, pLabel);
	pCombo = ui_ComboBoxCreate(x+40, y, 150, parse_PCBoneDef, &pDefDoc->eaDefBones, "Name");
	ui_ComboBoxSetSelectedCallback(pCombo, costumeDefEdit_UISelectDefBoneCombo, pDefDoc);
	ui_WidgetSetName(UI_WIDGET(pCombo), "CostumeDefEdit = Bone");
	ui_WidgetSetWidthEx(UI_WIDGET(pCombo), 0.30, UIUnitPercentage);
	ui_WindowAddChild(pWin, pCombo);
	pDefDoc->pDefBoneCombo = pCombo;
	y+=28;

	// Geometry List
	pList = ui_ListCreate(parse_PCGeometryDef, &pDefDoc->eaDefGeos, 20);
	pList->bUseBackgroundColor = true;
	pList->backgroundColor = ColorWhite;
	ui_ListAppendColumn(pList, ui_ListColumnCreateParseName("Geometries", "Name", NULL));
	ui_ListSetSelectedCallback(pList, costumeDefEdit_UISelectDefGeoList, pDefDoc);
	ui_ListSetCellContextCallback(pList, costumeDefEdit_UIContextGeo, pDefDoc);
	ui_WidgetSetPosition(UI_WIDGET(pList), x, y);
	ui_WidgetSetWidth(UI_WIDGET(pList), 200);
	ui_WidgetSetHeightEx(UI_WIDGET(pList), 1, UIUnitPercentage);
	ui_WidgetSetWidthEx(UI_WIDGET(pList), 0.30, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pList), 0, 0, 0, 28);
	ui_WindowAddChild(pWin, pList);
	pDefDoc->pDefGeoList = pList;

	// Material button
	pButton = ui_ButtonCreate("Materials", 0, 0, costumeDefEdit_UIOpenMaterials, pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 70);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 0, 0, 0, UIBottomLeft);
	ui_WindowAddChild(pWin, pButton);

	// Set up expander group
	pExpanderGroup = ui_ExpanderGroupCreate();
	ui_WindowAddChild(pWin, pExpanderGroup);
	ui_WidgetSetPositionEx(UI_WIDGET(pExpanderGroup), 0, 0, 0.3, 0, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pExpanderGroup), 0.7, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pExpanderGroup), 0, 0, 0, 28);

	// Gimme Button
	pGimmeButton = ui_GimmeButtonCreate(0, 0, "CostumeGeometry", pDefDoc->pCurrentGeoDef->pcName, pDefDoc->pCurrentGeoDef);
	ui_MenuAppendItem(pGimmeButton->pMenu,
		ui_MenuItemCreate("Checkout All Dupes", UIMenuCallback, costumeDefEdit_CheckoutAllGeoDefDupes, pDefDoc, NULL));
	ui_WidgetSetPositionEx(UI_WIDGET(pGimmeButton), 265, 3, 0, 0, UIBottomRight);
	ui_WindowAddChild(pWin, pGimmeButton);
	pDefDoc->pGeoGimmeButton = pGimmeButton;

	// Menu Button
	pMenuButton = ui_MenuButtonCreate(240, 3);
	ui_MenuButtonAppendItems(pMenuButton,
		ui_MenuItemCreate("Clone",UIMenuCallback, costumeDefEdit_UIMenuSelectDefGeoClone, pDefDoc, NULL),
		ui_MenuItemCreate("Copy",UIMenuCallback, costumeDefEdit_UIMenuSelectDefGeoCopy, pDefDoc, NULL),
		ui_MenuItemCreate("Paste",UIMenuCallback, costumeDefEdit_UIMenuSelectDefGeoPaste, pDefDoc, NULL),
		ui_MenuItemCreate("Dup For Other Bones",UIMenuCallback, costumeDefEdit_UIMenuSelectDefDupForOtherBones, pDefDoc, NULL),
		NULL);
	ui_MenuButtonSetPreopenCallback(pMenuButton, costumeDefEdit_UIMenuPreopenGeoOption, pDefDoc);
	ui_WidgetSetPositionEx(UI_WIDGET(pMenuButton), 240, 3, 0, 0, UIBottomRight);
	ui_WindowAddChild(pWin, pMenuButton);

	// New Button
	pButton = ui_ButtonCreate("New", 0, 0, costumeDefEdit_UIDefGeoNew, pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 70);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 160, 0, 0, 0, UIBottomRight);
	ui_WindowAddChild(pWin, pButton);
	pDefDoc->pGeoNewButton = pButton;

	// Revert Button
	pButton = ui_ButtonCreate("Revert", 0, 0, costumeDefEdit_UIDefGeoRevert, pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 70);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 80, 0, 0, 0, UIBottomRight);
	ui_WindowAddChild(pWin, pButton);
	pDefDoc->pGeoRevertButton = pButton;

	// Save Button
	pButton = ui_ButtonCreate("Save", 0, 0, costumeDefEdit_UIDefGeoSave, pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 70);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 0, 0, 0, UIBottomRight);
	ui_WindowAddChild(pWin, pButton);
	pDefDoc->pGeoSaveButton = pButton;

	pExpander = ui_ExpanderCreate("Definition", 112);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Geo Definition", 1));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Geo Definition");
	y = 0;
	x = 15;

	// Name
	pLabel = ui_LabelCreate("Name", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigGeoDef, pDefDoc->pCurrentGeoDef, parse_PCGeometryDef, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefGeoNameScopeChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefGeoNameField = pField;
	y+=28;

	// Display Name
	pLabel = ui_LabelCreate("Display Name", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_Message, pDefDoc->pOrigGeoDef ? &pDefDoc->pOrigGeoDef->displayNameMsg : NULL, &pDefDoc->pCurrentGeoDef->displayNameMsg, parse_DisplayMessage, "EditorCopy");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefGeoDispNameField = pField;
	y+=28;

	// Scope
	pLabel = ui_LabelCreate("Scope", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigGeoDef, pDefDoc->pCurrentGeoDef, parse_PCGeometryDef, "Scope");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefGeoNameScopeChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefGeoScopeField = pField;
	y+=28;

	// File Name
	pLabel = ui_LabelCreate("Def File", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pLabel = ui_LabelCreate(pDefDoc->pCurrentGeoDef ? pDefDoc->pCurrentGeoDef->pcFileName : NULL, x+100, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	ui_WidgetSetWidthEx(UI_WIDGET(pLabel), 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pLabel), 0, 20, 0, 0);
	pDefDoc->pDefGeoFileNameLabel = pLabel;

	y+=28;

	// Geometry
	pDefDoc->pGeoGeometryTextLabel = ui_LabelCreate("Geometry", x, y);
	ui_ExpanderAddChild(pExpander, pDefDoc->pGeoGeometryTextLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDefDoc->pOrigGeoDef, pDefDoc->pCurrentGeoDef, parse_PCGeometryDef, "Geometry", NULL, &g_eaGeoFileNames, NULL);
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefGeometryChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefGeoGeometryField = pField;
	y+=28;

	// Model
	pDefDoc->pGeoModelTextLabel = ui_LabelCreate("Model", x, y);
	ui_ExpanderAddChild(pExpander, pDefDoc->pGeoModelTextLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDefDoc->pOrigGeoDef, pDefDoc->pCurrentGeoDef, parse_PCGeometryDef, "Model", NULL, &pDefDoc->eaModelNames, NULL);
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefGeoModelField = pField;
	y+=28;

	ui_ExpanderSetHeight(pExpander, y);

	pExpander = ui_ExpanderCreate("Categories", 112);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Geo Categories", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Geo Categories");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;

	// Categories
	pLabel = ui_LabelCreate("Categories", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pList = ui_ListCreate(parse_PCCategory, &pDefDoc->eaCategories, 20);
	ui_ListAppendColumn(pList, ui_ListColumnCreateParseName("Categories", "Name", NULL));
	ui_ListSetSelectedCallback(pList, costumeDefEdit_UISelectCategoryList, pDefDoc);
	ui_ListSetMultiselect(pList, true);
	pList->fHeaderHeight = 0;
	pList->bToggleSelect = true;
	pList->bDrawSelection = false;
	pList->eaColumns[0]->fWidth = 50;
	pList->eaColumns[0]->bShowCheckBox = true;
	ui_WidgetSetPosition(UI_WIDGET(pList), x+100, y);
	ui_WidgetSetHeight(UI_WIDGET(pList), 140);
	ui_WidgetSetWidthEx(UI_WIDGET(pList), 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pList), 0, 21, 0, 0);
	ui_ExpanderAddChild(pExpander, pList);
	pDefDoc->pDefCatList = pList;
	y+=140;

	ui_ExpanderSetHeight(pExpander, y);

	// FX Add Panel
	pExpander = ui_ExpanderCreate("Geometry FX Add", 28);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Geo FX Add", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Geo FX Add");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;
	
	pDefDoc->pGeoFxExpander = pExpander;
	ui_ExpanderSetHeight(pExpander, y);
	
	// FX Swap Panel
	pExpander = ui_ExpanderCreate("Geometry FX Swap", 28);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Geo FX Swap", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Geo FX Swap");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;
	
	pDefDoc->pGeoFxSwapExpander = pExpander;
	ui_ExpanderSetHeight(pExpander, y);


	// Advanced Panel
	pExpander = ui_ExpanderCreate("Advanced", 28);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Geo Advanced", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Geo Advanced");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;
	
	// save the geometry advanced options expander so that it can be added to later
	pDefDoc->pGeoAdvancedExpander = pExpander;
	
	ui_ExpanderSetHeight(pExpander, y);		// set the final opended height for the expander

	pExpander = ui_ExpanderCreate("Options", 112);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Geo Options", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Geo Options");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;

	// Mirror Geometry
	pLabel = ui_LabelCreate("Mirror Geometry", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDefDoc->pOrigGeoDef, pDefDoc->pCurrentGeoDef, parse_PCGeometryDef, "MirrorGeometry", parse_PCGeometryDef, &pDefDoc->eaDefMirrorGeos, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefGeoMirrorField = pField;
	y+=28;

	// Default Material
	pLabel = ui_LabelCreate("Default Material", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDefDoc->pOrigGeoDef, pDefDoc->pCurrentGeoDef, parse_PCGeometryDef, "DefaultMaterial", parse_PCMaterialDef, &pDefDoc->eaDefMats, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefGeoMatField = pField;
	y+=28;

	// Random Weight
	pLabel = ui_LabelCreate("Random Weight", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigGeoDef, pDefDoc->pCurrentGeoDef, parse_PCGeometryDef, "RandomWeight");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefGeoRandomWeightField = pField;
	y+=28;

	// Order
	pLabel = ui_LabelCreate("Order", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigGeoDef, pDefDoc->pCurrentGeoDef, parse_PCGeometryDef, "Order");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefGeoOrderField = pField;
	y+=28;

	// Style
	pLabel = ui_LabelCreate("Style", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pDefDoc->pOrigGeoDef, pDefDoc->pCurrentGeoDef, parse_PCGeometryDef, "Style", "CostumeStyle", "ResourceName");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefGeoStyleField = pField;
	y+=28;

	// Costume Group
	pLabel = ui_LabelCreate("Costume Group", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pDefDoc->pOrigGeoDef, pDefDoc->pCurrentGeoDef, parse_PCGeometryDef, "CostumeGroups", "CostumeGroupDict", "ResourceName");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefGeoCostumeGroupsField = pField;
	y+=28;

	// Level of Detail
	pLabel = ui_LabelCreate("LOD", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDefDoc->pOrigGeoDef, pDefDoc->pCurrentGeoDef, parse_PCGeometryDef, "LOD", CostumeLODLevelEnum);
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefGeoLODField = pField;
	y+=28;

	// Color Restriction
	pLabel = ui_LabelCreate("Color Choices", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimpleEnum(kMEFieldType_FlagCombo, pDefDoc->pOrigGeoDef, pDefDoc->pCurrentGeoDef, parse_PCGeometryDef, "ColorChoices", PCColorFlagsEnum);
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefGeoColorRestrictionField = pField;
	y+=28;

	pDefDoc->pGeoOptionsExpander = pExpander;

	ui_ExpanderSetHeight(pExpander, y);

	pExpander = ui_ExpanderCreate("Restrictions", 28);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Geo Restriction", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Geo Restriction");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;

	// Restriction
	pLabel = ui_LabelCreate("Restriction", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimpleEnum(kMEFieldType_FlagCombo, pDefDoc->pOrigGeoDef, pDefDoc->pCurrentGeoDef, parse_PCGeometryDef, "RestrictedTo", PCRestrictionEnum);
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefGeoRestrictionField = pField;
	y+=28;

	ui_ExpanderSetHeight(pExpander, y);

	pDefDoc->pGeoEditWin = pWin;

	pWin->show = false; // Default to editor manager not showing this window

	return pWin;
}


static UIWindow *costumeDefEdit_InitMatEditWin(CostumeEditDefDoc *pDefDoc)
{
	UIWindow *pWin;
	UILabel *pLabel;
	UIList *pList;
	UIButton *pButton;
	UIMenuButton *pMenuButton;
	UIComboBox *pCombo;
	UIExpanderGroup *pExpanderGroup;
	UIExpander *pExpander;
	UIGimmeButton *pGimmeButton;
	F32 x, y;
	MEField *pField;
	char text[256];

	pWin = ui_WindowCreate("Material Definition", 700, 300, 500, 180);

	x=0;
	y=0;

	// Geometry
	pDefDoc->pMatGeometryTextLabel = ui_LabelCreate("Geometry", x, y);
	ui_WindowAddChild(pWin, pDefDoc->pMatGeometryTextLabel);
	pCombo = ui_ComboBoxCreate(x+70, y, 130, parse_PCGeometryDef, &pDefDoc->eaDefGeos, "Name");
	ui_ComboBoxSetSelectedCallback(pCombo, costumeDefEdit_UISelectDefGeoCombo, pDefDoc);
	ui_WidgetSetName(UI_WIDGET(pCombo), "CostumeDefEdit = Geometry");
	ui_WidgetSetWidthEx(UI_WIDGET(pCombo), 0.30, UIUnitPercentage);
	ui_WindowAddChild(pWin, pCombo);
	pDefDoc->pDefGeoCombo = pCombo;
	y+=28;

	// Material List
	pList = ui_ListCreate(parse_PCMaterialDef, &pDefDoc->eaDefMats, 20);
	pList->bUseBackgroundColor = true;
	pList->backgroundColor = ColorWhite;
	ui_ListAppendColumn(pList, ui_ListColumnCreateParseName("Materials", "Name", NULL));
	ui_ListSetSelectedCallback(pList, costumeDefEdit_UISelectDefMatList, pDefDoc);
	ui_ListSetCellContextCallback(pList, costumeDefEdit_UIContextMat, pDefDoc);
	ui_WidgetSetPosition(UI_WIDGET(pList), x, y);
	ui_WidgetSetWidth(UI_WIDGET(pList), 200);
	ui_WidgetSetHeightEx(UI_WIDGET(pList), 1, UIUnitPercentage);
	ui_WidgetSetWidthEx(UI_WIDGET(pList), 0.30, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pList), 0, 0, 0, 28);
	ui_WindowAddChild(pWin, pList);
	pDefDoc->pDefMatList = pList;

	// Add Button
	pButton = ui_ButtonCreate("Add ...", 0, 28, costumeDefEdit_UIDefGeoAddMat, pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 70);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 0, 0, 0, UIBottomLeft);
	ui_WindowAddChild(pWin, pButton);

	// Remove button
	pButton = ui_ButtonCreate("Textures", 0, 28, costumeDefEdit_UIOpenTextures, pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 70);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 80, 0, 0, 0, UIBottomLeft);
	ui_WindowAddChild(pWin, pButton);

	// Set up expander group
	pExpanderGroup = ui_ExpanderGroupCreate();
	ui_WindowAddChild(pWin, pExpanderGroup);
	ui_WidgetSetPositionEx(UI_WIDGET(pExpanderGroup), 0, 0, 0.3, 0, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pExpanderGroup), 0.7, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pExpanderGroup), 0, 0, 0, 28);

	// Gimme Button
	pGimmeButton = ui_GimmeButtonCreate(0, 0, "CostumeMaterial", pDefDoc->pCurrentMatDef->pcName, pDefDoc->pCurrentMatDef);
	ui_WidgetSetPositionEx(UI_WIDGET(pGimmeButton), 265, 3, 0, 0, UIBottomRight);
	ui_WindowAddChild(pWin, pGimmeButton);
	pDefDoc->pMatGimmeButton = pGimmeButton;

	// Menu Button
	pMenuButton = ui_MenuButtonCreate(240, 3);
	ui_MenuButtonAppendItems(pMenuButton,
		ui_MenuItemCreate("Clone",UIMenuCallback, costumeDefEdit_UIMenuSelectDefMatClone, pDefDoc, NULL),
		ui_MenuItemCreate("Copy",UIMenuCallback, costumeDefEdit_UIMenuSelectDefMatCopy, pDefDoc, NULL),
		ui_MenuItemCreate("Paste",UIMenuCallback, costumeDefEdit_UIMenuSelectDefMatPaste, pDefDoc, NULL),
		NULL);
	ui_MenuButtonSetPreopenCallback(pMenuButton, costumeDefEdit_UIMenuPreopenMatOption, pDefDoc);
	ui_WidgetSetPositionEx(UI_WIDGET(pMenuButton), 240, 3, 0, 0, UIBottomRight);
	ui_WindowAddChild(pWin, pMenuButton);

	// New Button
	pButton = ui_ButtonCreate("New", 0, 0, costumeDefEdit_UIDefMatNew, pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 70);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 160, 0, 0, 0, UIBottomRight);
	ui_WindowAddChild(pWin, pButton);
	pDefDoc->pMatNewButton = pButton;

	// Revert Button
	pButton = ui_ButtonCreate("Revert", 0, 0, costumeDefEdit_UIDefMatRevert, pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 70);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 80, 0, 0, 0, UIBottomRight);
	ui_WindowAddChild(pWin, pButton);
	pDefDoc->pMatRevertButton = pButton;

	// Save Button
	pButton = ui_ButtonCreate("Save", 0, 0, costumeDefEdit_UIDefMatSave, pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 70);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 0, 0, 0, UIBottomRight);
	ui_WindowAddChild(pWin, pButton);
	pDefDoc->pMatSaveButton = pButton;

	pExpander = ui_ExpanderCreate("Definition", 112);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Mat Definition", 1));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Mat Definition");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	ui_ExpanderSetOpened(pExpander, true);
	y = 0;
	x = 15;

	// Name
	pLabel = ui_LabelCreate("Name", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatNameScopeChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMatNameField = pField;
	y+=28;

	// Display Name
	pLabel = ui_LabelCreate("Display Name", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_Message, pDefDoc->pOrigMatDef ? &pDefDoc->pOrigMatDef->displayNameMsg : NULL, &pDefDoc->pCurrentMatDef->displayNameMsg, parse_DisplayMessage, "EditorCopy");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMatDispNameField = pField;
	y+=28;

	// Scope
	pLabel = ui_LabelCreate("Scope", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "Scope");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatNameScopeChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMatScopeField = pField;
	y+=28;

	// File Name
	pLabel = ui_LabelCreate("Def File", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pLabel = ui_LabelCreate(pDefDoc->pCurrentMatDef ? pDefDoc->pCurrentMatDef->pcFileName : "", x+100, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	ui_WidgetSetWidthEx(UI_WIDGET(pLabel), 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pLabel), 0, 20, 0, 0);
	pDefDoc->pDefMatFileNameLabel = pLabel;

	y+=28;
	
	// Costume Group
	pLabel = ui_LabelCreate("Costume Group", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "CostumeGroups", "CostumeGroupDict", "ResourceName");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMatCostumeGroupsField = pField;
	y+=28;

	// Material
	pDefDoc->pMatMaterialTextLabel = ui_LabelCreate("Material", x, y);
	ui_ExpanderAddChild(pExpander, pDefDoc->pMatMaterialTextLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "Material", NULL, &g_eaMatFileNames, NULL);
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMaterialChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMatMaterialField = pField;
	y+=28;

	// Has Skin
	pLabel = ui_LabelCreate("Has Skin", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "HasSkin");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	pDefDoc->pDefMatSkinField = pField;
	y+=28;

	ui_ExpanderSetHeight(pExpander, y);

	pExpander = ui_ExpanderCreate("Default Textures", 140);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Mat Default Textures", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Mat Default Textures");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;

	// Default Pattern
	sprintf(text, "Default %s", "Pattern");
	pDefDoc->pMatDefPatternTextLabel = ui_LabelCreate(text, x, y);
	ui_ExpanderAddChild(pExpander, pDefDoc->pMatDefPatternTextLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "DefaultPattern", parse_PCTextureDef, &pDefDoc->eaDefPatternTex, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMatPatternField = pField;
	y+=28;

	// Default Detail
	sprintf(text, "Default %s", "Detail");
	pDefDoc->pMatDefDetailTextLabel = ui_LabelCreate(text, x, y);
	ui_ExpanderAddChild(pExpander, pDefDoc->pMatDefDetailTextLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "DefaultDetail", parse_PCTextureDef, &pDefDoc->eaDefDetailTex, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMatDetailField = pField;
	y+=28;

	// Default Specular
	sprintf(text, "Default %s", "Specular");
	pDefDoc->pMatDefSpecularTextLabel = ui_LabelCreate(text, x, y);
	ui_ExpanderAddChild(pExpander, pDefDoc->pMatDefSpecularTextLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "DefaultSpecular", parse_PCTextureDef, &pDefDoc->eaDefSpecularTex, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMatSpecularField = pField;
	y+=28;

	// Default Diffuse
	sprintf(text, "Default %s", "Diffuse");
	pDefDoc->pMatDefDiffuseTextLabel = ui_LabelCreate(text, x, y);
	ui_ExpanderAddChild(pExpander, pDefDoc->pMatDefDiffuseTextLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "DefaultDiffuse", parse_PCTextureDef, &pDefDoc->eaDefDiffuseTex, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMatDiffuseField = pField;
	y+=28;

	// Default Movable
	sprintf(text, "Default %s", "Movable");
	pDefDoc->pMatDefMovableTextLabel = ui_LabelCreate(text, x, y);
	ui_ExpanderAddChild(pExpander, pDefDoc->pMatDefMovableTextLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "DefaultMovable", parse_PCTextureDef, &pDefDoc->eaDefMovableTex, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMatMovableField = pField;
	y+=28;

	// Requires Pattern
	sprintf(text, "Requires %s", "Pattern");
	pDefDoc->pMatReqPatternTextLabel = ui_LabelCreate(text, x, y);
	ui_ExpanderAddChild(pExpander, pDefDoc->pMatReqPatternTextLabel);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "RequiresPattern");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	pDefDoc->pDefMatReqPatternField = pField;
	y+=28;

	// Requires Detail
	sprintf(text, "Requires %s", "Detail");
	pDefDoc->pMatReqDetailTextLabel = ui_LabelCreate(text, x, y);
	ui_ExpanderAddChild(pExpander, pDefDoc->pMatReqDetailTextLabel);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "RequiresDetail");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	pDefDoc->pDefMatReqDetailField = pField;
	y+=28;

	// Requires Specular
	sprintf(text, "Requires %s", "Specular");
	pDefDoc->pMatReqSpecularTextLabel = ui_LabelCreate(text, x, y);
	ui_ExpanderAddChild(pExpander, pDefDoc->pMatReqSpecularTextLabel);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "RequiresSpecular");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	pDefDoc->pDefMatReqSpecularField = pField;
	y+=28;

	// Requires Diffuse
	sprintf(text, "Requires %s", "Diffuse");
	pDefDoc->pMatReqDiffuseTextLabel = ui_LabelCreate(text, x, y);
	ui_ExpanderAddChild(pExpander, pDefDoc->pMatReqDiffuseTextLabel);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "RequiresDiffuse");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	pDefDoc->pDefMatReqDiffuseField = pField;
	y+=28;

	// Requires Diffuse
	sprintf(text, "Requires %s", "Movable");
	pDefDoc->pMatReqMovableTextLabel = ui_LabelCreate(text, x, y);
	ui_ExpanderAddChild(pExpander, pDefDoc->pMatReqMovableTextLabel);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "RequiresMovable");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	pDefDoc->pDefMatReqMovableField = pField;
	y+=28;

	ui_ExpanderSetHeight(pExpander, y);

	// Reflection
	pExpander = ui_ExpanderCreate("Reflection Options", 112);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Reflection Options", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Reflection Options");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;

	pLabel = ui_LabelCreate("User Editable", x+60, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pLabel = ui_LabelCreate("Default Value", x+150, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	y+=25;

	pField = MEFieldCreateSimple(kMEFieldType_Check, SAFE_MEMBER(pDefDoc->pOrigMatDef, pColorOptions), pDefDoc->pCurrentMatDef->pColorOptions, parse_PCMaterialColorOptions, "CustomReflection");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+150, y);
	ui_CheckButtonSetText(pField->pUICheck, "Customize");
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	pDefDoc->pCustomizeReflectField = pField;

	y+=28;

	pDefDoc->pReflectLabel = ui_LabelCreate("Not Available", x+60, y);

	pLabel = ui_LabelCreate("Color 0", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "allowReflect3");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+60, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDefDoc->pAllowReflect3Field = pField;

	pField = MEFieldCreateSimple(kMEFieldType_SliderText, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "defaultReflect3");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+150, y);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	pDefDoc->pDefaultReflect3Field = pField;

	y+=28;

	pLabel = ui_LabelCreate("Color 1", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "allowReflect0");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+60, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDefDoc->pAllowReflect0Field = pField;

	pField = MEFieldCreateSimple(kMEFieldType_SliderText, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "defaultReflect0");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+150, y);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	pDefDoc->pDefaultReflect0Field = pField;

	y+=28;

	pLabel = ui_LabelCreate("Color 2", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "allowReflect1");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+60, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDefDoc->pAllowReflect1Field = pField;

	pField = MEFieldCreateSimple(kMEFieldType_SliderText, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "defaultReflect1");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+150, y);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	pDefDoc->pDefaultReflect1Field = pField;

	y+=28;

	pLabel = ui_LabelCreate("Color 3", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "allowReflect2");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+60, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDefDoc->pAllowReflect2Field = pField;

	pField = MEFieldCreateSimple(kMEFieldType_SliderText, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "defaultReflect2");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+150, y);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	pDefDoc->pDefaultReflect2Field = pField;

	y+=28;

	pDefDoc->pReflectExpander = pExpander;

	ui_ExpanderSetHeight(pExpander, y);

	// Specularity
	pExpander = ui_ExpanderCreate("Specular Options", 112);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Specular Options", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Specular Options");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;

	pLabel = ui_LabelCreate("User Editable", x+60, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pLabel = ui_LabelCreate("Default Value", x+150, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	y+=25;

	pField = MEFieldCreateSimple(kMEFieldType_Check, SAFE_MEMBER(pDefDoc->pOrigMatDef, pColorOptions), pDefDoc->pCurrentMatDef->pColorOptions, parse_PCMaterialColorOptions, "CustomSpecularity");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+150, y);
	ui_CheckButtonSetText(pField->pUICheck, "Customize");
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	pDefDoc->pCustomizeSpecularField = pField;

	y+=28;

	pDefDoc->pSpecularLabel = ui_LabelCreate("Not Available", x+60, y);

	pLabel = ui_LabelCreate("Color 0", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "allowSpecular3");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+60, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDefDoc->pAllowSpecular3Field = pField;

	pField = MEFieldCreateSimple(kMEFieldType_SliderText, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "defaultSpecular3");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+150, y);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	pDefDoc->pDefaultSpecular3Field = pField;

	y+=28;

	pLabel = ui_LabelCreate("Color 1", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "allowSpecular0");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+60, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDefDoc->pAllowSpecular0Field = pField;

	pField = MEFieldCreateSimple(kMEFieldType_SliderText, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "defaultSpecular0");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+150, y);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	pDefDoc->pDefaultSpecular0Field = pField;

	y+=28;

	pLabel = ui_LabelCreate("Color 2", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "allowSpecular1");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+60, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDefDoc->pAllowSpecular1Field = pField;

	pField = MEFieldCreateSimple(kMEFieldType_SliderText, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "defaultSpecular1");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+150, y);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	pDefDoc->pDefaultSpecular1Field = pField;

	y+=28;

	pLabel = ui_LabelCreate("Color 3", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "allowSpecular2");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+60, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDefDoc->pAllowSpecular2Field = pField;

	pField = MEFieldCreateSimple(kMEFieldType_SliderText, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "defaultSpecular2");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+150, y);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	pDefDoc->pDefaultSpecular2Field = pField;

	y+=28;

	pDefDoc->pSpecularExpander = pExpander;
	ui_ExpanderSetHeight(pExpander, y);

	// Glow
	pExpander = ui_ExpanderCreate("Glow Options", 112);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Glow Options", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Glow Options");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;

	pLabel = ui_LabelCreate("User Editable", x+60, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	y+=25;

	pLabel = ui_LabelCreate("Color 0", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "allowGlow0");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+60, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDefDoc->pAllowGlow0Field = pField;

	y+=28;

	pLabel = ui_LabelCreate("Color 1", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "allowGlow1");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+60, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDefDoc->pAllowGlow1Field = pField;

	y+=28;

	pLabel = ui_LabelCreate("Color 2", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "allowGlow2");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+60, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDefDoc->pAllowGlow2Field = pField;

	y+=28;

	pLabel = ui_LabelCreate("Color 3", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "allowGlow3");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+60, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDefDoc->pAllowGlow3Field = pField;

	y+=28;

	ui_ExpanderSetHeight(pExpander, y);

	// Muscle
	pExpander = ui_ExpanderCreate("Muscle Options", 112);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Muscle Options", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Muscle Options");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;

	pLabel = ui_LabelCreate("Suppress Muscle", x+60, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	y+=25;

	pLabel = ui_LabelCreate("Color 0", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "customMuscle0");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+60, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDefDoc->pCustomMuscle0Field = pField;

	y+=28;

	pLabel = ui_LabelCreate("Color 1", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "customMuscle1");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+60, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDefDoc->pCustomMuscle1Field = pField;

	y+=28;

	pLabel = ui_LabelCreate("Color 2", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "customMuscle2");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+60, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDefDoc->pCustomMuscle2Field = pField;

	y+=28;

	pLabel = ui_LabelCreate("Color 3", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);

	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, &pDefDoc->origDefaults, &pDefDoc->currentDefaults, parse_CostumeEditDefaultsStruct, "customMuscle3");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+60, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefMatDataChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDefDoc->pCustomMuscle3Field = pField;

	y+=28;

	ui_ExpanderSetHeight(pExpander, y);
	
	pExpander = ui_ExpanderCreate("Material Constants", 28);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Material Constants", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Material Constants");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;
	
	// save the material constant expander so that it can be added to later
	pDefDoc->pMatConstantExpander = pExpander;
	
	ui_ExpanderSetHeight(pExpander, y);

	// FX Swap Panel
	pExpander = ui_ExpanderCreate("Material FX Swap", 28);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Mat FX Swap", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Mat FX Swap");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;

	pDefDoc->pMatFxSwapExpander = pExpander;
	ui_ExpanderSetHeight(pExpander, y);

	pExpander = ui_ExpanderCreate("Other Options", 28);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Material Options", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Material Options");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;

	// Random Weight
	pLabel = ui_LabelCreate("Random Weight", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "RandomWeight");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMatRandomWeightField = pField;
	y+=28;

	// Order
	pLabel = ui_LabelCreate("Order", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "Order");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMatOrderField = pField;
	y+=28;

	// Color Restriction
	pLabel = ui_LabelCreate("Color Choices", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimpleEnum(kMEFieldType_FlagCombo, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "ColorChoices", PCColorFlagsEnum);
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMatColorRestrictionField = pField;
	y+=28;

	ui_ExpanderSetHeight(pExpander, y);

	pExpander = ui_ExpanderCreate("Restrictions", 28);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Mat Restriction", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Mat Restriction");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;

	// Restriction
	pLabel = ui_LabelCreate("Restriction", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimpleEnum(kMEFieldType_FlagCombo, pDefDoc->pOrigMatDef, pDefDoc->pCurrentMatDef, parse_PCMaterialDef, "RestrictedTo", PCRestrictionEnum);
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMatRestrictionField = pField;
	y+=28;

	ui_ExpanderSetHeight(pExpander, y);

	pDefDoc->pMatEditWin = pWin;

	pWin->show = false; // Default to editor manager not showing this window

	return pWin;
}


static UIWindow *costumeDefEdit_InitTexEditWin(CostumeEditDefDoc *pDefDoc)
{
	UIWindow *pWin;
	UILabel *pLabel;
	UIList *pList;
	UIButton *pButton;
	UIMenuButton *pMenuButton;
	UIComboBox *pCombo;
	UIExpanderGroup *pExpanderGroup;
	UIExpander *pExpander;
	UIGimmeButton *pGimmeButton;
	UISprite *pSprite;
	F32 x, y;
	MEField *pField;

	pWin = ui_WindowCreate("Texture Definition", 700, 550, 500, 180);

	x=0;
	y=0;

	// Material
	pDefDoc->pTexMaterialTextLabel = ui_LabelCreate("Material", x, y);
	ui_WindowAddChild(pWin, pDefDoc->pTexMaterialTextLabel);
	pCombo = ui_ComboBoxCreate(x+60, y, 130, parse_PCMaterialDef, &pDefDoc->eaDefMats, "Name");
	ui_ComboBoxSetSelectedCallback(pCombo, costumeDefEdit_UISelectDefMatCombo, pDefDoc);
	ui_WidgetSetName(UI_WIDGET(pCombo), "CostumeDefEdit = Material");
	ui_WidgetSetWidthEx(UI_WIDGET(pCombo), 0.30, UIUnitPercentage);
	ui_WindowAddChild(pWin, pCombo);
	pDefDoc->pDefMatCombo = pCombo;
	y+=28;

	// Texture List
	pList = ui_ListCreate(parse_PCTextureDef, &pDefDoc->eaDefTexs, 20);
	pList->bUseBackgroundColor = true;
	pList->backgroundColor = ColorWhite;
	ui_ListAppendColumn(pList, ui_ListColumnCreateParseName("Textures", "Name", NULL));
	ui_ListSetSelectedCallback(pList, costumeDefEdit_UISelectDefTexList, pDefDoc);
	ui_ListSetCellContextCallback(pList, costumeDefEdit_UIContextTex, pDefDoc);
	ui_WidgetSetPosition(UI_WIDGET(pList), x, y);
	ui_WidgetSetWidth(UI_WIDGET(pList), 200);
	ui_WidgetSetHeightEx(UI_WIDGET(pList), 1, UIUnitPercentage);
	ui_WidgetSetWidthEx(UI_WIDGET(pList), 0.30, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pList), 0, 0, 0, 28);
	ui_WindowAddChild(pWin, pList);
	pDefDoc->pDefTexList = pList;

	// Add Button
	pButton = ui_ButtonCreate("Add ...", 0, 28, costumeDefEdit_UIDefMatAddTex, pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 70);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 0, 0, 0, UIBottomLeft);
	ui_WindowAddChild(pWin, pButton);

	// Set up expander group
	pExpanderGroup = ui_ExpanderGroupCreate();
	ui_WindowAddChild(pWin, pExpanderGroup);
	ui_WidgetSetPositionEx(UI_WIDGET(pExpanderGroup), 0, 0, 0.3, 0, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pExpanderGroup), 0.7, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pExpanderGroup), 0, 0, 0, 28);

	pDefDoc->pTextureExpanderGroup = pExpanderGroup;

	// Gimme Button
	pGimmeButton = ui_GimmeButtonCreate(0, 0, "CostumeTexture", pDefDoc->pCurrentTexDef->pcName, pDefDoc->pCurrentTexDef);
	ui_WidgetSetPositionEx(UI_WIDGET(pGimmeButton), 265, 3, 0, 0, UIBottomRight);
	ui_WindowAddChild(pWin, pGimmeButton);
	pDefDoc->pTexGimmeButton = pGimmeButton;

	// Menu Button
	pMenuButton = ui_MenuButtonCreate(240, 3);
	ui_MenuButtonAppendItems(pMenuButton,
		ui_MenuItemCreate("Clone",UIMenuCallback, costumeDefEdit_UIMenuSelectDefTexClone, pDefDoc, NULL),
		ui_MenuItemCreate("Copy",UIMenuCallback, costumeDefEdit_UIMenuSelectDefTexCopy, pDefDoc, NULL),
		ui_MenuItemCreate("Paste",UIMenuCallback, costumeDefEdit_UIMenuSelectDefTexPaste, pDefDoc, NULL),
		NULL);
	ui_MenuButtonSetPreopenCallback(pMenuButton, costumeDefEdit_UIMenuPreopenTexOption, pDefDoc);
	ui_WidgetSetPositionEx(UI_WIDGET(pMenuButton), 240, 3, 0, 0, UIBottomRight);
	ui_WindowAddChild(pWin, pMenuButton);

	// New Button
	pButton = ui_ButtonCreate("New", 0, 0, costumeDefEdit_UIDefTexNew, pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 70);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 160, 0, 0, 0, UIBottomRight);
	ui_WindowAddChild(pWin, pButton);
	pDefDoc->pTexNewButton = pButton;

	// Revert Button
	pButton = ui_ButtonCreate("Revert", 0, 0, costumeDefEdit_UIDefTexRevert, pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 70);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 80, 0, 0, 0, UIBottomRight);
	ui_WindowAddChild(pWin, pButton);
	pDefDoc->pTexRevertButton = pButton;

	// Save Button
	pButton = ui_ButtonCreate("Save", 0, 0, costumeDefEdit_UIDefTexSave, pDefDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 70);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 0, 0, 0, UIBottomRight);
	ui_WindowAddChild(pWin, pButton);
	pDefDoc->pTexSaveButton = pButton;

	pExpander = ui_ExpanderCreate("Definition", 112);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Tex Definition", 1));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Tex Definition");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	ui_ExpanderSetOpened(pExpander, true);
	y = 0;
	x = 15;

	// Name
	pLabel = ui_LabelCreate("Name", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefTexNameScopeChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexNameField = pField;
	y+=28;

	// Display Name
	pLabel = ui_LabelCreate("Display Name", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_Message, pDefDoc->pOrigTexDef ? &pDefDoc->pOrigTexDef->displayNameMsg : NULL, &pDefDoc->pCurrentTexDef->displayNameMsg, parse_DisplayMessage, "EditorCopy");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexDispNameField = pField;
	y+=28;

	// Scope
	pLabel = ui_LabelCreate("Scope", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "Scope");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefTexNameScopeChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexScopeField = pField;
	y+=28;

	// File Name
	pLabel = ui_LabelCreate("Def File", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pLabel = ui_LabelCreate(pDefDoc->pCurrentTexDef ? pDefDoc->pCurrentTexDef->pcFileName : "", x+100, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	ui_WidgetSetWidthEx(UI_WIDGET(pLabel), 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pLabel), 0, 20, 0, 0);
	pDefDoc->pDefTexFileNameLabel = pLabel;

	y+=28;
	
	// Costume Group
	pLabel = ui_LabelCreate("Costume Group", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "CostumeGroups", "CostumeGroupDict", "ResourceName");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexCostumeGroupsField = pField;
	y+=28;


	// Old Texture
	pLabel = ui_LabelCreate("Old Texture", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "OrigTexture", NULL, &pDefDoc->eaOldTexNames, NULL);
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexOldField = pField;
	y+=28;

	// New Texture
	pLabel = ui_LabelCreate("New Texture", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "NewTexture", NULL, &g_eaTexFileNames, NULL);
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefTextureChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexNewField = pField;
	y+=28;

	// Texture Type
	pLabel = ui_LabelCreate("Type", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "TypeFlags", PCTextureTypeEnum);
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	pDefDoc->pDefTexTypeField = pField;

	// Preview Sprite
	pSprite = ui_SpriteCreate(x+230, y, 50, 50, "");
	pSprite->bDrawBorder = true;
	pDefDoc->pDefTexSprite = pSprite;
	ui_ExpanderAddChild(pExpander, pSprite);
	y+=28;

	// Has Skin
	pLabel = ui_LabelCreate("Has Skin", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "HasSkin");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	pDefDoc->pDefTexSkinField = pField;
	y+=28;

	// Tex Words Key
	pLabel = ui_LabelCreate("TexWords Key", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "TexWordsKey");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	pDefDoc->pDefTexWordsKeyField = pField;
	y+=28;

	// Has Skin
	pLabel = ui_LabelCreate("TexWords Caps", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "TexWordsCaps");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	pDefDoc->pDefTexWordsCapsField = pField;
	y+=28;

	ui_ExpanderSetHeight(pExpander, y);

	pExpander = ui_ExpanderCreate("Movable Texture Constants", 28);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Movable Texture Constants", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Movable Texture Constants");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;

	pDefDoc->pMovableConstantsExpander = pExpander;
	gMovConstExpVisible = true;

	pLabel = ui_LabelCreate("Movable Min X", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pMovableOptions), pDefDoc->pCurrentTexDef->pMovableOptions, parse_PCTextureMovableOptions, "MovableMinX");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+175, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexMovMinXField = pField;
	y+=28;

	pLabel = ui_LabelCreate("Movable Max X", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pMovableOptions), pDefDoc->pCurrentTexDef->pMovableOptions, parse_PCTextureMovableOptions, "MovableMaxX");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+175, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexMovMaxXField = pField;
	y+=28;

	pLabel = ui_LabelCreate("Movable Default X", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pMovableOptions), pDefDoc->pCurrentTexDef->pMovableOptions, parse_PCTextureMovableOptions, "MovableDefaultX");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+175, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexMovDefaultXField = pField;
	y+=28;

	pLabel = ui_LabelCreate("Movable Min Y", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pMovableOptions), pDefDoc->pCurrentTexDef->pMovableOptions, parse_PCTextureMovableOptions, "MovableMinY");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+175, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexMovMinYField = pField;
	y+=28;

	pLabel = ui_LabelCreate("Movable Max Y", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pMovableOptions), pDefDoc->pCurrentTexDef->pMovableOptions, parse_PCTextureMovableOptions, "MovableMaxY");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+175, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexMovMaxYField = pField;
	y+=28;

	pLabel = ui_LabelCreate("Movable Default Y", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pMovableOptions), pDefDoc->pCurrentTexDef->pMovableOptions, parse_PCTextureMovableOptions, "MovableDefaultY");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+175, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexMovDefaultYField = pField;
	y+=28;

	pLabel = ui_LabelCreate("Movable Min Scale X", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pMovableOptions), pDefDoc->pCurrentTexDef->pMovableOptions, parse_PCTextureMovableOptions, "MovableMinScaleX");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+175, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexMovMinScaleXField = pField;
	y+=28;

	pLabel = ui_LabelCreate("Movable Max Scale X", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pMovableOptions), pDefDoc->pCurrentTexDef->pMovableOptions, parse_PCTextureMovableOptions, "MovableMaxScaleX");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+175, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexMovMaxScaleXField = pField;
	y+=28;

	pLabel = ui_LabelCreate("Movable Default Scale X", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pMovableOptions), pDefDoc->pCurrentTexDef->pMovableOptions, parse_PCTextureMovableOptions, "MovableDefaultScaleX");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+175, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexMovDefaultScaleXField = pField;
	y+=28;

	pLabel = ui_LabelCreate("Movable Min Scale Y", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pMovableOptions), pDefDoc->pCurrentTexDef->pMovableOptions, parse_PCTextureMovableOptions, "MovableMinScaleY");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+175, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexMovMinScaleYField = pField;
	y+=28;

	pLabel = ui_LabelCreate("Movable Max Scale Y", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pMovableOptions), pDefDoc->pCurrentTexDef->pMovableOptions, parse_PCTextureMovableOptions, "MovableMaxScaleY");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+175, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexMovMaxScaleYField = pField;
	y+=28;

	pLabel = ui_LabelCreate("Movable Default Scale Y", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pMovableOptions), pDefDoc->pCurrentTexDef->pMovableOptions, parse_PCTextureMovableOptions, "MovableDefaultScaleY");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+175, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexMovDefaultScaleYField = pField;
	y+=28;

	pLabel = ui_LabelCreate("Movable Default Rotation", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pMovableOptions), pDefDoc->pCurrentTexDef->pMovableOptions, parse_PCTextureMovableOptions, "MovableDefaultRotation");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+175, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexMovDefaultRotField = pField;
	y+=28;

	pLabel = ui_LabelCreate("Movable Can Edit Pos", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pMovableOptions), pDefDoc->pCurrentTexDef->pMovableOptions, parse_PCTextureMovableOptions, "MovableCanEditPosition");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+175, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	pDefDoc->pDefTexMovCanEditPosField = pField;
	y+=28;

	pLabel = ui_LabelCreate("Movable Can Edit Rot", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pMovableOptions), pDefDoc->pCurrentTexDef->pMovableOptions, parse_PCTextureMovableOptions, "MovableCanEditRotation");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+175, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	pDefDoc->pDefTexMovCanEditRotField = pField;
	y+=28;

	pLabel = ui_LabelCreate("Movable Can Edit Scale", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pMovableOptions), pDefDoc->pCurrentTexDef->pMovableOptions, parse_PCTextureMovableOptions, "MovableCanEditScale");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+175, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 120);
	pDefDoc->pDefTexMovCanEditScaleField = pField;
	y+=28;

	ui_ExpanderSetHeight(pExpander, y);

	pExpander = ui_ExpanderCreate("Extra Texture Swaps", 28);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Extra Textures", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Extra Textures");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;

	pDefDoc->pExtraTexturesExpander = pExpander;

	ui_ExpanderSetHeight(pExpander, y);

	pExpander = ui_ExpanderCreate("Material Constant", 28);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Texture Material Constant", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Texture Material Constant");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;

	pLabel = ui_LabelCreate("Constant Name", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pValueOptions), pDefDoc->pCurrentTexDef->pValueOptions, parse_PCTextureValueOptions, "ValueConstant");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMaterialConstantName = pField;
	y+=28;

	pLabel = ui_LabelCreate("Constant Index", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pValueOptions), pDefDoc->pCurrentTexDef->pValueOptions, parse_PCTextureValueOptions, "ConstValueIndex");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMaterialConstantIndex = pField;
	y+=28;

	pLabel = ui_LabelCreate("Constant Default", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pValueOptions), pDefDoc->pCurrentTexDef->pValueOptions, parse_PCTextureValueOptions, "ConstValueDefault");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMaterialConstantDefault = pField;
	y+=28;

	pLabel = ui_LabelCreate("Constant Min", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pValueOptions), pDefDoc->pCurrentTexDef->pValueOptions, parse_PCTextureValueOptions, "ConstValueMin");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMaterialConstantMin = pField;
	y+=28;

	pLabel = ui_LabelCreate("Constant Max", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER2(pDefDoc, pOrigTexDef, pValueOptions), pDefDoc->pCurrentTexDef->pValueOptions, parse_PCTextureValueOptions, "ConstValueMax");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefMaterialConstantMax = pField;
	y+=28;

	ui_ExpanderSetHeight(pExpander, y);

	pExpander = ui_ExpanderCreate("Other Options", 28);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Texture Options", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Texture Options");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;

	// Random Weight
	pLabel = ui_LabelCreate("Random Weight", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "RandomWeight");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexRandomWeightField = pField;
	y+=28;

	// Order
	pLabel = ui_LabelCreate("Order", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "Order");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexOrderField = pField;
	y+=28;

	// Color Restriction
	pLabel = ui_LabelCreate("Color Choices", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimpleEnum(kMEFieldType_FlagCombo, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "ColorChoices", PCColorFlagsEnum);
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexColorRestrictionField = pField;
	y+=28;

	// Color Swap 0
	pLabel = ui_LabelCreate("Color Swap 0", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "ColorSwap0");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefColorSwapChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexColorSwap0Field = pField;
	y+=28;

	// Color Swap 1
	pLabel = ui_LabelCreate("Color Swap 1", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "ColorSwap1");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefColorSwapChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexColorSwap1Field = pField;
	y+=28;

	// Color Swap 2
	pLabel = ui_LabelCreate("Color Swap 2", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "ColorSwap2");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefColorSwapChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexColorSwap2Field = pField;
	y+=28;

	// Color Swap 3
	pLabel = ui_LabelCreate("Color Swap 3", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "ColorSwap3");
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefColorSwapChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexColorSwap3Field = pField;
	y+=28;

	ui_ExpanderSetHeight(pExpander, y);

	pExpander = ui_ExpanderCreate("Restrictions", 28);
	ui_WidgetSkin(UI_WIDGET(pExpander), gDefExpanderSkin);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(COSTUME_EDITOR, "Expander", "Tex Restriction", 0));
	ui_ExpanderSetExpandCallback(pExpander, costumeDefEdit_ExpandChanged, "Tex Restriction");
	ui_ExpanderGroupAddExpander(pExpanderGroup, pExpander);
	y = 0;

	// Restriction
	pLabel = ui_LabelCreate("Restriction", x, y);
	ui_ExpanderAddChild(pExpander, pLabel);
	pField = MEFieldCreateSimpleEnum(kMEFieldType_FlagCombo, pDefDoc->pOrigTexDef, pDefDoc->pCurrentTexDef, parse_PCTextureDef, "RestrictedTo", PCRestrictionEnum);
	MEFieldAddToParent(pField, UI_WIDGET(pExpander), x+100, y);
	MEFieldSetChangeCallback(pField, costumeDefEdit_UIDefFieldChanged, pDefDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 20, 0, 0);
	pDefDoc->pDefTexRestrictionField = pField;
	y+=28;

	ui_ExpanderSetHeight(pExpander, y);

	pDefDoc->pTexEditWin = pWin;

	pWin->show = false; // Default to editor manager not showing this window

	return pWin;
}


void costumeDefEdit_InitDisplay(CostumeEditDefDoc *pDefDoc)
{
	if (!pDefDoc->pGeoEditWin) {
		// Set up default def edit values
		costumeDefEdit_DefSetGeo(pDefDoc, gNoGeoDef);
		costumeDefEdit_DefSetMat(pDefDoc, gNoMatDef);
		costumeDefEdit_DefSetTex(pDefDoc, gNoTexDef);

		// Create the windows
		pDefDoc->pGeoEditWin = costumeDefEdit_InitGeoEditWin(pDefDoc);
		pDefDoc->pMatEditWin = costumeDefEdit_InitMatEditWin(pDefDoc);
		pDefDoc->pTexEditWin = costumeDefEdit_InitTexEditWin(pDefDoc);
	}
}

void costumeDefEdit_InitData(EMEditor *pEditor)
{
	if (!gNoGeoDef) {
		gNoGeoDef = StructCreate(parse_PCGeometryDef);
		gNoMatDef = StructCreate(parse_PCMaterialDef);
		gNoTexDef = StructCreate(parse_PCTextureDef);

		resDictRegisterEventCallback(g_hCostumeTextureDict, costumeDefEdit_DictChanged, NULL);
		resDictRegisterEventCallback(g_hCostumeBoneDict, costumeDefEdit_DictChanged, NULL);
		resDictRegisterEventCallback(g_hCostumeGeometryDict, costumeDefEdit_DictChanged, NULL);
		resDictRegisterEventCallback(g_hCostumeMaterialDict, costumeDefEdit_DictChanged, NULL);

		// Manage the geo file list
		costumeDefEdit_GeoFileChangeCallback(NULL,0);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "character_library/*.ModelHeader", costumeDefEdit_GeoFileChangeCallback);

		// Manage the material file list
		costumeDefEdit_MatFileChangeCallback(NULL,0);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "materials/*.Material", costumeDefEdit_MatFileChangeCallback);

		// Manage the tex file list
		costumeDefEdit_TexFileChangeCallback(NULL,0);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "texture_library/costumes/*.wtex", costumeDefEdit_TexFileChangeCallback);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "texture_library/character_library/*.wtex", costumeDefEdit_TexFileChangeCallback);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "texture_library/core_costumes/*.wtex", costumeDefEdit_TexFileChangeCallback);

		gDefExpanderSkin = ui_SkinCreate(NULL);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "Default_Bold", gDefExpanderSkin->hNormal);

		// Dictionary change listeners
		emAddDictionaryStateChangeHandler(pEditor, "CostumeGeometry", NULL, NULL, costumeDefEdit_SaveDefContinue, costumeDefEdit_DeleteDefContinue, NULL); 
		emAddDictionaryStateChangeHandler(pEditor, "CostumeMaterial", NULL, NULL, costumeDefEdit_SaveDefContinue, costumeDefEdit_DeleteDefContinue, NULL); 
		emAddDictionaryStateChangeHandler(pEditor, "CostumeTexture", NULL, NULL, costumeDefEdit_SaveDefContinue, costumeDefEdit_DeleteDefContinue, NULL); 
		emAddDictionaryStateChangeHandler(pEditor, "CostumeGeometryAdd", NULL, NULL, costumeDefEdit_SaveDefContinue, costumeDefEdit_DeleteDefContinue, NULL); 
		emAddDictionaryStateChangeHandler(pEditor, "CostumeMaterialAdd", NULL, NULL, costumeDefEdit_SaveDefContinue, costumeDefEdit_DeleteDefContinue, NULL); 
	}
}

void costumeDefEdit_RemoveCurrentPartRefs()
{
	gDefDoc->pCurrentBoneDef = NULL;
	gDefDoc->pCurrentGeoDef = NULL;
	gDefDoc->pCurrentMatDef = NULL;
	gDefDoc->pCurrentTexDef = NULL;
	gDefDoc->pOrigGeoDef = NULL;
	gDefDoc->pOrigMatDef = NULL;
	gDefDoc->pOrigTexDef = NULL;
}

static void costumeDefEdit_DictChanged(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	if (!pRefData) {
		return;
	}

 	if (eType == RESEVENT_RESOURCE_REMOVED)
	{
		// Clear current selections as they may not exist anymore when the dictionary changes.
		// on second thought, that was a terrible idea, shame on me.
		
		if (gDefDoc->pCurrentBoneDef && gDefDoc->pCurrentBoneDef->pcName && strcmp(pRefData, gDefDoc->pCurrentBoneDef->pcName) == 0)
			gDefDoc->pCurrentBoneDef = NULL;
		if (gDefDoc->pCurrentGeoDef && gDefDoc->pCurrentGeoDef->pcName && strcmp(pRefData, gDefDoc->pCurrentGeoDef->pcName) == 0)
			gDefDoc->pCurrentGeoDef = NULL;
		if (gDefDoc->pCurrentMatDef && gDefDoc->pCurrentMatDef->pcName && strcmp(pRefData, gDefDoc->pCurrentMatDef->pcName) == 0)
			gDefDoc->pCurrentMatDef = NULL;
		if (gDefDoc->pCurrentTexDef && gDefDoc->pCurrentTexDef->pcName && strcmp(pRefData, gDefDoc->pCurrentTexDef->pcName) == 0)
			gDefDoc->pCurrentTexDef = NULL;
	}
}

//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------


#endif

#include "CostumeDefEditor.h"
#include "AutoGen/CostumeDefEditor_h_ast.c"
