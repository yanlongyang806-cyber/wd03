#include "dynCostume.h"

#include "fileutil.h"
#include "FolderCache.h"
#include "error.h"
#include "StringCache.h"
#include "mathutil.h"

#include "dynFxInfo.h"
#include "wlCostume.h"

#include "dynCostume_h_ast.h"
#include "dynFxInfo_h_ast.h"
#include "AutoGen/wlCostume_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

//////////////////////////////////////////////////////////////////////////////////
//
// DynCostume
//
//////////////////////////////////////////////////////////////////////////////////
DictionaryHandle hDynCostumeInfo;

static S32 siCount = 0;

static const char *spcColor0;
static const char *spcColor1;
static const char *spcColor2;
static const char *spcColor3;

static WLCostume* dynCostumeInfoCreateWLCostume(	const char *pcName,
													const char *pcFilename,
													const char *pcSkelInfo,
													const char *pcSubCostumeAttachmentBone,
													DynCostumeScaleSetting **eaScaleSettings,
													DynCostumePart **eaCostumeParts,
													bool isSubCostume)
{
	WLCostume* pCostume = StructCreate(parse_WLCostume);

	pCostume->pcName = pcName;
	pCostume->pcFileName = pcFilename;
	SET_HANDLE_FROM_STRING("SkelInfo", pcSkelInfo, pCostume->hSkelInfo);

	if (isSubCostume) {
		pCostume->pcSubCostumeAttachmentBone = pcSubCostumeAttachmentBone;
		pCostume->bForceNoLOD = true;
		pCostume->uNoCollision = 1;
	}

	FOR_EACH_IN_EARRAY(eaScaleSettings, DynCostumeScaleSetting, pScaleInfo)
	{
		ScaleValue *pScaleValue = StructCreate(parse_ScaleValue);
		pScaleValue->pcScaleGroup = allocAddString(pScaleInfo->pcName);
		copyVec3(pScaleInfo->vValue, pScaleValue->vScaleInputs);
		scaleVec3(pScaleValue->vScaleInputs, 0.01f, pScaleValue->vScaleInputs);
		eaPush(&pCostume->eaScaleValue, pScaleValue);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(eaCostumeParts, DynCostumePart, pInfoPart)
	{
		WLCostumePart* pNewPart = StructCreate(parse_WLCostumePart);
		pNewPart->pcModel = pInfoPart->pchGeometry;
		if (!pInfoPart->pchBoneName)
		{
			FxFileError(pCostume->pcFileName, "Must specify bone name in costume part");
			wlCostumeFree(pCostume);
			return NULL;
		}
		pNewPart->pchBoneName = pInfoPart->pchBoneName;
		pNewPart->pchMaterial = pInfoPart->pchMaterial;
		pNewPart->uiRequiredLOD = 4;

		FOR_EACH_IN_EARRAY(pInfoPart->eaTextureSwaps, DynCostumePartTextureSwap, pTextureInfo)
		{
			CostumeTextureSwap *pTextureSwap = StructCreate(parse_CostumeTextureSwap);
			pTextureSwap->pcOldTexture = pTextureInfo->pcOldTexture;
			pTextureSwap->pcNewTexture = pTextureInfo->pcNewTexture;
			eaPush(&pNewPart->eaTextureSwaps, pTextureSwap);
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY(pInfoPart->eaColorSwaps, DynCostumePartColorSwap, pColorInfo)
		{
			MaterialNamedConstant *pColorSwap = StructCreate(parse_MaterialNamedConstant);
			pColorSwap->name = pColorInfo->pcName;
			pColorSwap->value[0] = pColorInfo->uiValue[0] * U8TOF32_COLOR;
			pColorSwap->value[1] = pColorInfo->uiValue[1] * U8TOF32_COLOR;
			pColorSwap->value[2] = pColorInfo->uiValue[2] * U8TOF32_COLOR;
			pColorSwap->value[3] = pColorInfo->uiValue[3] * U8TOF32_COLOR;
			eaPush(&pNewPart->eaMatConstant, pColorSwap);
		}
		FOR_EACH_END;

		eaPush(&pCostume->eaCostumeParts, pNewPart);
	}
	FOR_EACH_END;

	return pCostume;
}

static bool dynCostumeVerifyScaleSettings(	const char *pcFilename,
											DynCostumeScaleSetting **eaScaleSettings)
{
	bool bRet = true;

	FOR_EACH_IN_EARRAY(eaScaleSettings, DynCostumeScaleSetting, pScaleInfo)
	{
		if (pScaleInfo->vValue[0] < -100.f || 100.f < pScaleInfo->vValue[0] ||
			pScaleInfo->vValue[1] < -100.f || 100.f < pScaleInfo->vValue[1] ||
			pScaleInfo->vValue[2] < -100.f || 100.f < pScaleInfo->vValue[2] )
		{
			FxFileError(pcFilename, "Scale setting values must be between -100.0 and +100.0");
			bRet = false;
		}
	}
	FOR_EACH_END;

	return bRet;
}

static bool dynCostumeVerifyCostumeParts(	const char *pcFilename,
											DynCostumePart **eaCostumeParts)
{
	bool bRet = true;

	FOR_EACH_IN_EARRAY(eaCostumeParts, DynCostumePart, pInfoPart)
	{
		if (!pInfoPart->pchBoneName) {
			FxFileError(pcFilename, "Must specify bone name in costume part");
			bRet = false;
		}

		FOR_EACH_IN_EARRAY(pInfoPart->eaColorSwaps, DynCostumePartColorSwap, pColorInfo) {
			if (255 < pColorInfo->uiValue[0] ||
				255 < pColorInfo->uiValue[1] ||
				255 < pColorInfo->uiValue[2] )
			{
				FxFileError(pcFilename, "Color swap values must be between 0 and 255");
				bRet = false;
			}
		} FOR_EACH_END;
	}
	FOR_EACH_END;

	return bRet;
}

bool dynCostumeInfoVerify(DynCostumeInfo* pInfo)
{
	bool bRet = true;

	bRet &= dynCostumeVerifyScaleSettings(pInfo->pcFileName, pInfo->eaScaleSettings);
	bRet &= dynCostumeVerifyCostumeParts(pInfo->pcFileName, pInfo->eaCostumeParts);

	FOR_EACH_IN_EARRAY(pInfo->eaSubCostumes, DynSubCostumeInfo, pSubInfo)
	{
		bRet &= dynCostumeVerifyScaleSettings(pInfo->pcFileName, pSubInfo->eaScaleSettings);
		bRet &= dynCostumeVerifyCostumeParts(pInfo->pcFileName, pSubInfo->eaCostumeParts);
	}
	FOR_EACH_END;

	return bRet;
}

bool dynCostumeInfoFixup(DynCostumeInfo* pInfo, WLCostume **pCostOut, bool bAutoCleanup)
{
	WLCostume* pNewCostume, *pOldCostume, **eaSubCostumes = NULL;
	const char *cNameAlloc;
	char cName[256];
	bool bSubCostumeFail = false;
	eaCreate(&eaSubCostumes);

	getFileNameNoExt(cName, pInfo->pcFileName);
	pInfo->pcInfoName = cNameAlloc = allocAddString(cName);

	if (bAutoCleanup)
	{
		char cValue[256];
		_itoa(siCount,cValue,10);
		Strcat(cName,cValue);
		cNameAlloc = allocAddString(cName);
		siCount++;
	}

	FOR_EACH_IN_EARRAY(pInfo->eaSubCostumes, DynSubCostumeInfo, pSubCostumeInfo)
	{
		WLCostume *pNewSubCostume, *pOldSubCostume;
		const char *sNameAlloc;
		char sName[256], sValue[256];

		_itoa(ipSubCostumeInfoIndex,sValue,10);
		Strcpy(sName, cName);
		Strcat(sName, "Sub");
		Strcat(sName,sValue);
		sNameAlloc = allocAddString(sName);

		pOldSubCostume = wlCostumeFromName(sNameAlloc);
		if (pOldSubCostume)
			wlCostumeRemoveByName(sNameAlloc);

		pNewSubCostume = dynCostumeInfoCreateWLCostume(	sNameAlloc,
														pInfo->pcFileName,
														pSubCostumeInfo->pcSkelInfo,
														pSubCostumeInfo->pcAttachmentBone,
														pSubCostumeInfo->eaScaleSettings,
														pSubCostumeInfo->eaCostumeParts,
														true);

		if (!pNewSubCostume) {
			bSubCostumeFail = true;
			break;
		}

		eaPush(&eaSubCostumes, pNewSubCostume);
	}
	FOR_EACH_END;

	if (bSubCostumeFail) {
		eaDestroyEx(&eaSubCostumes, wlCostumeFree);
		*pCostOut = NULL;
		return false;
	}

	pOldCostume = wlCostumeFromName(cNameAlloc);
	if (pOldCostume)
		wlCostumeRemoveByName(cNameAlloc);

	pNewCostume = dynCostumeInfoCreateWLCostume(cNameAlloc,
												pInfo->pcFileName,
												pInfo->pcSkelInfo,
												NULL,
												pInfo->eaScaleSettings,
												pInfo->eaCostumeParts,
												false);

	if (!pNewCostume) {
		eaDestroyEx(&eaSubCostumes, wlCostumeFree);
		*pCostOut = NULL;
		return false;
	}

	if ((GetAppGlobalType() != GLOBALTYPE_GAMESERVER || isDevelopmentMode())
		&& !verifyCostume(pNewCostume, false))
	{
		//memory leak on costume data, but error needs to be fixed anyhow
		eaDestroy(&eaSubCostumes);
		*pCostOut = NULL;
		return false;
	}

	pNewCostume->bNoAutoCleanup = !bAutoCleanup;
	FOR_EACH_IN_EARRAY(eaSubCostumes, WLCostume, pSubCostume) {
		pSubCostume->bNoAutoCleanup = !bAutoCleanup;
		wlCostumePushSubCostume(pSubCostume, pNewCostume);
	} FOR_EACH_END;

	wlCostumeAddToDictionary(pNewCostume, pNewCostume->pcName);

	eaDestroy(&eaSubCostumes);
	*pCostOut = pNewCostume;
	return true;
}

static void dynCostumeInfoReloadCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);


	if(!ParserReloadFileToDictionary(relpath,hDynCostumeInfo))
	{
		FxFileError(relpath, "Error reloading DynCostume file: %s", relpath);
	}
	else
	{
		// nothing to do here
	}
}

AUTO_FIXUPFUNC;
TextParserResult fixupDynCostumeInfo(DynCostumeInfo* pCostumeInfo, enumTextParserFixupType eType, void *pExtraData)
{
	WLCostume *pCostumeOut = NULL;

	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
			if (!dynCostumeInfoVerify(pCostumeInfo) || !dynCostumeInfoFixup(pCostumeInfo, &pCostumeOut, false))
				return PARSERESULT_INVALID; // remove this from the costume list
		xcase FIXUPTYPE_POST_BIN_READ:
			if (!dynCostumeInfoFixup(pCostumeInfo, &pCostumeOut, false))
				return PARSERESULT_INVALID; // remove this from the costume list
	}

	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void registerCostumeInfoDict(void)
{
	hDynCostumeInfo = RefSystem_RegisterSelfDefiningDictionary("DynCostume", false, parse_DynCostumeInfo, true, false, NULL);
}

void dynCostumeInfoLoadAll(void)
{
	loadstart_printf("Loading DynCostumeInfos...");

	// optional for outsource build
	ParserLoadFilesToDictionary("dyn/costume", ".dcost", "DynCostume.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, hDynCostumeInfo);

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/costume/*.dcost", dynCostumeInfoReloadCallback);
	}

	loadend_printf("done (%d DynCostumes)", RefSystem_GetDictionaryNumberOfReferents(hDynCostumeInfo) );
}



AUTO_RUN;
void dynCostumeInitStrings(void)
{
	spcColor0 = allocAddString("Color0");
	spcColor1 = allocAddString("Color1");
	spcColor2 = allocAddString("Color2");
	spcColor3 = allocAddString("Color3");
}

#define CONVERT_COLOR(y,x)	if (y##->bSetColor##x) {										\
								if (y##->vColor##x##[3] < 0.f)								\
									y##->vColor##x##[3] = 255.f;							\
								scaleVec4(y##->vColor##x, U8TOF32_COLOR, y##->vColor##x);	\
								CLAMPVEC4(y##->vColor##x, 0.f, 1.f);						\
							} 0

#define TRY_REPLACING_TEXTURE(y,x)	if (y##->bSetTexture##x##Old &&								\
										y##->bSetTexture##x##New &&								\
										pTextureSwap->pcOldTexture == y##->pcTexture##x##Old)	\
									{															\
										pTextureSwap->pcNewTexture = y##->pcTexture##x##New;	\
										bReplacedTexture##x = true;								\
									} 0

#define TRY_REPLACING_COLOR(y,x)	if (y##->bSetColor##x &&						\
										pMaterial->name == spcColor##x)				\
									{												\
										copyVec4(y##->vColor##x, pMaterial->value);	\
										bReplacedColor##x = true;					\
									} 0

#define TRY_ADDING_TEXTURE(y,x)	if (y##->bSetTexture##x##Old &&													\
									y##->bSetTexture##x##New &&													\
									!bReplacedTexture##x)														\
								{																				\
									CostumeTextureSwap *pTextureSwap = StructCreate(parse_CostumeTextureSwap);	\
									pTextureSwap->pcOldTexture = y##->pcTexture##x##Old;						\
									pTextureSwap->pcNewTexture = y##->pcTexture##x##New;						\
									eaPush(&pCostumePart->eaTextureSwaps, pTextureSwap);						\
								} 0

#define TRY_ADDING_COLOR(y,x)	if (y##->bSetColor##x &&															\
									!bReplacedColor##x)																\
								{																					\
									MaterialNamedConstant *pColorSwap = StructCreate(parse_MaterialNamedConstant);	\
									pColorSwap->name = spcColor##x;													\
									copyVec4(y##->vColor##x, pColorSwap->value);									\
									eaPush(&pCostumePart->eaMatConstant, pColorSwap);								\
								} 0

void dynCostumeApplyParams(	WLCostume *pCostume,
							DynCostumePart **eaCostumePartsInfo,
							DynCostumeMaterialParameters *pMaterials,
							DynCostumeMaterialParameters *pAltMaterials,
							U32 bHasAltParams )
{
	CONVERT_COLOR(pMaterials,0);
	CONVERT_COLOR(pMaterials,1);
	CONVERT_COLOR(pMaterials,2);
	CONVERT_COLOR(pMaterials,3);

	if (bHasAltParams)
	{
		CONVERT_COLOR(pAltMaterials,0);
		CONVERT_COLOR(pAltMaterials,1);
		CONVERT_COLOR(pAltMaterials,2);
		CONVERT_COLOR(pAltMaterials,3);
	}

	FOR_EACH_IN_EARRAY(pCostume->eaCostumeParts, WLCostumePart, pCostumePart)
	{
		DynCostumeMaterialParameters *pUseMaterials = eaCostumePartsInfo[ipCostumePartIndex]->bUseAltParams ? pAltMaterials : pMaterials;

		bool bReplacedTexture1 = false;
		bool bReplacedTexture2 = false;
		bool bReplacedTexture3 = false;
		bool bReplacedTexture4 = false;

		bool bReplacedColor0 = false;
		bool bReplacedColor1 = false;
		bool bReplacedColor2 = false;
		bool bReplacedColor3 = false;

		if (pUseMaterials->pcMaterial) {
			pCostumePart->pchMaterial = pUseMaterials->pcMaterial;
		}

		FOR_EACH_IN_EARRAY(pCostumePart->eaTextureSwaps, CostumeTextureSwap, pTextureSwap)
		{
			TRY_REPLACING_TEXTURE(pUseMaterials,1);
			TRY_REPLACING_TEXTURE(pUseMaterials,2);
			TRY_REPLACING_TEXTURE(pUseMaterials,3);
			TRY_REPLACING_TEXTURE(pUseMaterials,4);
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY(pCostumePart->eaMatConstant, MaterialNamedConstant, pMaterial)
		{
			TRY_REPLACING_COLOR(pUseMaterials,0);
			TRY_REPLACING_COLOR(pUseMaterials,1);
			TRY_REPLACING_COLOR(pUseMaterials,2);
			TRY_REPLACING_COLOR(pUseMaterials,3);
		}
		FOR_EACH_END;
	
		TRY_ADDING_TEXTURE(pUseMaterials,1);
		TRY_ADDING_TEXTURE(pUseMaterials,2);
		TRY_ADDING_TEXTURE(pUseMaterials,3);
		TRY_ADDING_TEXTURE(pUseMaterials,4);

		TRY_ADDING_COLOR(pUseMaterials,0);
		TRY_ADDING_COLOR(pUseMaterials,1);
		TRY_ADDING_COLOR(pUseMaterials,2);
		TRY_ADDING_COLOR(pUseMaterials,3);
	}
	FOR_EACH_END;
}

#undef TRY_ADDING_COLOR
#undef TRY_ADDING_TEXTURE
#undef TRY_REPLACING_COLOR
#undef TRY_REPLACING_TEXTURE
#undef CONVERT_COLOR

static void dynCostumeInitParameters(DynCostumeMaterialParameters *pMaterialParams, U32 bFull)
{
	if (bFull) {
		pMaterialParams->pcMaterial = NULL;
		pMaterialParams->pcTexture1New = pMaterialParams->pcTexture1Old = NULL;
		pMaterialParams->pcTexture1New = pMaterialParams->pcTexture1Old = NULL;
		pMaterialParams->pcTexture1New = pMaterialParams->pcTexture1Old = NULL;
		pMaterialParams->pcTexture1New = pMaterialParams->pcTexture1Old = NULL;
		setVec4same(pMaterialParams->vColor0, -1.f);
		setVec4same(pMaterialParams->vColor1, -1.f);
		setVec4same(pMaterialParams->vColor2, -1.f);
		setVec4same(pMaterialParams->vColor3, -1.f);
	}

	pMaterialParams->bSetMaterial = 0;
	pMaterialParams->bSetTexture1New = pMaterialParams->bSetTexture1Old = 0;
	pMaterialParams->bSetTexture2New = pMaterialParams->bSetTexture2Old = 0;
	pMaterialParams->bSetTexture3New = pMaterialParams->bSetTexture3Old = 0;
	pMaterialParams->bSetTexture4New = pMaterialParams->bSetTexture4Old = 0;
	pMaterialParams->bSetColor0 = 0;
	pMaterialParams->bSetColor1 = 0;
	pMaterialParams->bSetColor2 = 0;
	pMaterialParams->bSetColor3 = 0;
}

WLCostume *dynCostumeFetchOrCreateFromFxCostume(const char *pcCostumeName, DynFxCostume *pFxCostumeInfo, DynParamBlock *pFxParamBlock, DynParamBlock *pFallbackParamBlock)
{
	DynCostumeInfo *pDynCostumeInfo = RefSystem_ReferentFromString(hDynCostumeInfo, pFxCostumeInfo->pcCostume);
	U32 uiNumParams = eaSize(&pFxCostumeInfo->eaParams);
	WLCostume *pCostume = wlCostumeFromName(pcCostumeName); //by default grab the base costume, name passed in could be from pFxCostumeInfo or pFxParamBlock
	bool bHasAltParams = false;

	if (!pDynCostumeInfo) {
		return pCostume;
	}

	FOR_EACH_IN_EARRAY(pDynCostumeInfo->eaCostumeParts, DynCostumePart, pChkPart)
	{
		bHasAltParams |= pChkPart->bUseAltParams;
	}
	FOR_EACH_END;

	//try to apply any parameters
	if (uiNumParams > 0 &&
		(	pFxParamBlock ||
			pFallbackParamBlock))
	{
		DynCostumeMaterialParameters materials;
		DynCostumeMaterialParameters altMaterials;

		dynCostumeInitParameters(&materials, 1);
		dynCostumeInitParameters(&altMaterials, bHasAltParams);

		if (pFxParamBlock)
		{
			materials.bSetMaterial		= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_MATERIAL_INDEX,    pFxParamBlock, (void*)&materials.pcMaterial,    parse_DynFxCostume);
			materials.bSetTexture1New	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_TEXTURE1NEW_INDEX, pFxParamBlock, (void*)&materials.pcTexture1New, parse_DynFxCostume);
			materials.bSetTexture1Old	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_TEXTURE1OLD_INDEX, pFxParamBlock, (void*)&materials.pcTexture1Old, parse_DynFxCostume);
			materials.bSetTexture2New	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_TEXTURE2NEW_INDEX, pFxParamBlock, (void*)&materials.pcTexture2New, parse_DynFxCostume);
			materials.bSetTexture2Old	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_TEXTURE2OLD_INDEX, pFxParamBlock, (void*)&materials.pcTexture2Old, parse_DynFxCostume);
			materials.bSetTexture3New	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_TEXTURE3NEW_INDEX, pFxParamBlock, (void*)&materials.pcTexture3New, parse_DynFxCostume);
			materials.bSetTexture3Old	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_TEXTURE3OLD_INDEX, pFxParamBlock, (void*)&materials.pcTexture3Old, parse_DynFxCostume);
			materials.bSetTexture4New	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_TEXTURE4NEW_INDEX, pFxParamBlock, (void*)&materials.pcTexture4New, parse_DynFxCostume);
			materials.bSetTexture4Old	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_TEXTURE4OLD_INDEX, pFxParamBlock, (void*)&materials.pcTexture4Old, parse_DynFxCostume);
			materials.bSetColor0		= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_COLOR_INDEX,       pFxParamBlock, (void*)&materials.vColor0,       parse_DynFxCostume);
			materials.bSetColor1		= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_COLOR1_INDEX,      pFxParamBlock, (void*)&materials.vColor1,       parse_DynFxCostume);
			materials.bSetColor2		= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_COLOR2_INDEX,      pFxParamBlock, (void*)&materials.vColor2,       parse_DynFxCostume);
			materials.bSetColor3		= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_COLOR3_INDEX,      pFxParamBlock, (void*)&materials.vColor3,       parse_DynFxCostume);

			if (bHasAltParams)
			{
				altMaterials.bSetMaterial		= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTMATERIAL_INDEX,    pFxParamBlock, (void*)&altMaterials.pcMaterial,    parse_DynFxCostume);
				altMaterials.bSetTexture1New	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTTEXTURE1NEW_INDEX, pFxParamBlock, (void*)&altMaterials.pcTexture1New, parse_DynFxCostume);
				altMaterials.bSetTexture1Old	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTTEXTURE1OLD_INDEX, pFxParamBlock, (void*)&altMaterials.pcTexture1Old, parse_DynFxCostume);
				altMaterials.bSetTexture2New	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTTEXTURE2NEW_INDEX, pFxParamBlock, (void*)&altMaterials.pcTexture2New, parse_DynFxCostume);
				altMaterials.bSetTexture2Old	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTTEXTURE2OLD_INDEX, pFxParamBlock, (void*)&altMaterials.pcTexture2Old, parse_DynFxCostume);
				altMaterials.bSetTexture3New	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTTEXTURE3NEW_INDEX, pFxParamBlock, (void*)&altMaterials.pcTexture3New, parse_DynFxCostume);
				altMaterials.bSetTexture3Old	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTTEXTURE3OLD_INDEX, pFxParamBlock, (void*)&altMaterials.pcTexture3Old, parse_DynFxCostume);
				altMaterials.bSetTexture4New	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTTEXTURE4NEW_INDEX, pFxParamBlock, (void*)&altMaterials.pcTexture4New, parse_DynFxCostume);
				altMaterials.bSetTexture4Old	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTTEXTURE4OLD_INDEX, pFxParamBlock, (void*)&altMaterials.pcTexture4Old, parse_DynFxCostume);
				altMaterials.bSetColor0			= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTCOLOR_INDEX,       pFxParamBlock, (void*)&altMaterials.vColor0,       parse_DynFxCostume);
				altMaterials.bSetColor1			= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTCOLOR1_INDEX,      pFxParamBlock, (void*)&altMaterials.vColor1,       parse_DynFxCostume);
				altMaterials.bSetColor2			= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTCOLOR2_INDEX,      pFxParamBlock, (void*)&altMaterials.vColor2,       parse_DynFxCostume);
				altMaterials.bSetColor3			= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTCOLOR3_INDEX,      pFxParamBlock, (void*)&altMaterials.vColor3,       parse_DynFxCostume);
			}
		}
		
		if (pFallbackParamBlock)
		{
			if (!materials.bSetMaterial)	materials.bSetMaterial    = dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_MATERIAL_INDEX,    pFallbackParamBlock, (void*)&materials.pcMaterial,    parse_DynFxCostume);
			if (!materials.bSetTexture1New) materials.bSetTexture1New = dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_TEXTURE1NEW_INDEX, pFallbackParamBlock, (void*)&materials.pcTexture1New, parse_DynFxCostume);
			if (!materials.bSetTexture1Old) materials.bSetTexture1Old = dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_TEXTURE1OLD_INDEX, pFallbackParamBlock, (void*)&materials.pcTexture1Old, parse_DynFxCostume);
			if (!materials.bSetTexture2New) materials.bSetTexture2New = dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_TEXTURE2NEW_INDEX, pFallbackParamBlock, (void*)&materials.pcTexture2New, parse_DynFxCostume);
			if (!materials.bSetTexture2Old) materials.bSetTexture2Old = dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_TEXTURE2OLD_INDEX, pFallbackParamBlock, (void*)&materials.pcTexture2Old, parse_DynFxCostume);
			if (!materials.bSetTexture3New) materials.bSetTexture3New = dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_TEXTURE3NEW_INDEX, pFallbackParamBlock, (void*)&materials.pcTexture3New, parse_DynFxCostume);
			if (!materials.bSetTexture3Old) materials.bSetTexture3Old = dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_TEXTURE3OLD_INDEX, pFallbackParamBlock, (void*)&materials.pcTexture3Old, parse_DynFxCostume);
			if (!materials.bSetTexture4New) materials.bSetTexture4New = dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_TEXTURE4NEW_INDEX, pFallbackParamBlock, (void*)&materials.pcTexture4New, parse_DynFxCostume);
			if (!materials.bSetTexture4Old)	materials.bSetTexture4Old = dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_TEXTURE4OLD_INDEX, pFallbackParamBlock, (void*)&materials.pcTexture4Old, parse_DynFxCostume);
			if (!materials.bSetColor0)      materials.bSetColor0      = dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_COLOR_INDEX,       pFallbackParamBlock, (void*)&materials.vColor0,       parse_DynFxCostume);
			if (!materials.bSetColor1)      materials.bSetColor1      = dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_COLOR1_INDEX,      pFallbackParamBlock, (void*)&materials.vColor1,       parse_DynFxCostume);
			if (!materials.bSetColor2)      materials.bSetColor2      = dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_COLOR2_INDEX,      pFallbackParamBlock, (void*)&materials.vColor2,       parse_DynFxCostume);
			if (!materials.bSetColor3)      materials.bSetColor3      = dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_COLOR3_INDEX,      pFallbackParamBlock, (void*)&materials.vColor3,       parse_DynFxCostume);

			if (bHasAltParams)
			{
				if (!altMaterials.bSetMaterial)		altMaterials.bSetMaterial		= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTMATERIAL_INDEX,    pFxParamBlock, (void*)&altMaterials.pcMaterial,    parse_DynFxCostume);
				if (!altMaterials.bSetTexture1New)	altMaterials.bSetTexture1New	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTTEXTURE1NEW_INDEX, pFxParamBlock, (void*)&altMaterials.pcTexture1New, parse_DynFxCostume);
				if (!altMaterials.bSetTexture1Old)	altMaterials.bSetTexture1Old	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTTEXTURE1OLD_INDEX, pFxParamBlock, (void*)&altMaterials.pcTexture1Old, parse_DynFxCostume);
				if (!altMaterials.bSetTexture2New)	altMaterials.bSetTexture2New	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTTEXTURE2NEW_INDEX, pFxParamBlock, (void*)&altMaterials.pcTexture2New, parse_DynFxCostume);
				if (!altMaterials.bSetTexture2Old)	altMaterials.bSetTexture2Old	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTTEXTURE2OLD_INDEX, pFxParamBlock, (void*)&altMaterials.pcTexture2Old, parse_DynFxCostume);
				if (!altMaterials.bSetTexture3New)	altMaterials.bSetTexture3New	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTTEXTURE3NEW_INDEX, pFxParamBlock, (void*)&altMaterials.pcTexture3New, parse_DynFxCostume);
				if (!altMaterials.bSetTexture3Old)	altMaterials.bSetTexture3Old	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTTEXTURE3OLD_INDEX, pFxParamBlock, (void*)&altMaterials.pcTexture3Old, parse_DynFxCostume);
				if (!altMaterials.bSetTexture4New)	altMaterials.bSetTexture4New	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTTEXTURE4NEW_INDEX, pFxParamBlock, (void*)&altMaterials.pcTexture4New, parse_DynFxCostume);
				if (!altMaterials.bSetTexture4Old)	altMaterials.bSetTexture4Old	= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTTEXTURE4OLD_INDEX, pFxParamBlock, (void*)&altMaterials.pcTexture4Old, parse_DynFxCostume);
				if (!altMaterials.bSetColor0)		altMaterials.bSetColor0			= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTCOLOR_INDEX,       pFxParamBlock, (void*)&altMaterials.vColor0,       parse_DynFxCostume);
				if (!altMaterials.bSetColor1)		altMaterials.bSetColor1			= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTCOLOR1_INDEX,      pFxParamBlock, (void*)&altMaterials.vColor1,       parse_DynFxCostume);
				if (!altMaterials.bSetColor2)		altMaterials.bSetColor2			= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTCOLOR2_INDEX,      pFxParamBlock, (void*)&altMaterials.vColor2,       parse_DynFxCostume);
				if (!altMaterials.bSetColor3)		altMaterials.bSetColor3			= dynFxApplyCopyParamsGeneral(uiNumParams, pFxCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_ALTCOLOR3_INDEX,      pFxParamBlock, (void*)&altMaterials.vColor3,       parse_DynFxCostume);
			}
		}

		if (materials.bSetMaterial ||
			materials.bSetTexture1New || materials.bSetTexture1Old ||
			materials.bSetTexture2New || materials.bSetTexture2Old ||
			materials.bSetTexture3New || materials.bSetTexture3Old ||
			materials.bSetTexture4New || materials.bSetTexture4Old ||
			materials.bSetColor0 ||
			materials.bSetColor2 ||
			materials.bSetColor2 ||
			materials.bSetColor3 ||
			(	bHasAltParams &&
				(	altMaterials.bSetMaterial ||
					altMaterials.bSetTexture1New || altMaterials.bSetTexture1Old ||
					altMaterials.bSetTexture2New || altMaterials.bSetTexture2Old ||
					altMaterials.bSetTexture3New || altMaterials.bSetTexture3Old ||
					altMaterials.bSetTexture4New || altMaterials.bSetTexture4Old ||
					altMaterials.bSetColor0 ||
					altMaterials.bSetColor1 ||
					altMaterials.bSetColor2 ||
					altMaterials.bSetColor3
			)))
		{
			//found parameters, attempt to rebuild the costume under a new name w/ auto-cleanup enabled
			if (dynCostumeInfoFixup(pDynCostumeInfo, &pCostume, true))
			{
				FOR_EACH_IN_EARRAY(pCostume->eaSubCostumes, WLSubCostume, pSubCostume)
				{
					WLCostume *pDictSubCostume = GET_REF(pSubCostume->hSubCostume);
					if (pDictSubCostume) {
						assert(ipSubCostumeIndex < eaSize(&pDynCostumeInfo->eaSubCostumes));
						assert(eaSize(&pDictSubCostume->eaCostumeParts) == eaSize(&pDynCostumeInfo->eaSubCostumes[ipSubCostumeIndex]->eaCostumeParts));
						dynCostumeApplyParams(	pDictSubCostume,
												pDynCostumeInfo->eaSubCostumes[ipSubCostumeIndex]->eaCostumeParts,
												&materials,
												&altMaterials,
												bHasAltParams);
					}
				}
				FOR_EACH_END;

				dynCostumeApplyParams(	pCostume,
										pDynCostumeInfo->eaCostumeParts,
										&materials,
										&altMaterials,
										bHasAltParams );
			}
		}
	}

	return pCostume;
}

#include "dynCostume_h_ast.c"