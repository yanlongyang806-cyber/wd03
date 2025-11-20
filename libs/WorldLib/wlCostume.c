
#include "wlCostume.h"

// UtilsLib
#include "StringCache.h"
#include "timing.h"

#include "wlModelLoad.h"
#include "WorldGrid.h"
#include "dynDraw.h"
#include "dynFxInfo.h"
#include "dynSeqData.h"
#include "wlState.h"
#include "dynAnimChart.h" //for stances

#include "AutoGen/wlCostume_h_ast.h"
#include "AutoGen/wlSkelInfo_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_CMD_INT(dynDebugState.bDrawBodysockAtlas, danimDrawBodysockAtlas) ACMD_CATEGORY(dynAnimation);

DictionaryHandle hWLCostumeDict;

const char* pcDefaultSkeleton = "Basic_Anim_Object";

// special pre-pooled strings
static const char* pcPooled_BodySockGeometry;
//static const char* pcPooled_BodySockModel;
static const char* pcPooled_DefaultColor;
static const char* pcPooled_DefaultSkinColor;

// pre-pooled strings
static const char* pcPooled_BodySock;
static const char* pcPooled_BodySockPose;
static const char* pcPooled_Base;
static const char* pcPooled_Hips;
static const char* pcPooled_Head;
static const char* pcPooled_Hair;
static const char* pcPooled_Chest;
static const char* pcPooled_HandL;
static const char* pcPooled_HandR;
static const char* pcPooled_FeetL;
static const char* pcPooled_FeetR;

AUTO_RUN;
void registerStaticWLCostumeStrings(void)
{
	pcPooled_BodySockGeometry = allocAddStaticString("Character_Library/M_Hips_Bodysock");
	//pcPooled_BodySockModel = allocAddStaticString("GEO_Hips_BodySock");
	//pcPooled_BodySockGeometry = allocAddStaticString("character_library/M_Hips_UberBodySock");
	//pcPooled_BodySockModel = allocAddStaticString("GEO_Hips_UberBodySock");
	//pcPooled_BodySockPose = allocAddStaticString("CoreDefault/Modes/Misc/BodySock_Pose");
	pcPooled_DefaultColor = allocAddStaticString("Color0");
	pcPooled_DefaultSkinColor = allocAddStaticString("Color3");

	pcPooled_BodySock = allocAddStaticString("BodySock");
	pcPooled_Hips = allocAddStaticString("Hips");
	pcPooled_Base = allocAddStaticString("Base");
	pcPooled_Head = allocAddStaticString("Head");
	pcPooled_Hair = allocAddStaticString("Hair");
	pcPooled_Chest = allocAddStaticString("Chest");
	pcPooled_HandL = allocAddStaticString("HandL");
	pcPooled_HandR = allocAddStaticString("HandR");
	pcPooled_FeetL = allocAddStaticString("FeetL");
	pcPooled_FeetR = allocAddStaticString("FeetR");
}

static bool verifyCostumeGeometryExists(WLCostumePart* pPart, const char* pcModelName, const WLCostume* pCostume)
{
	ModelHeaderSet *pSet;
	ModelHeader *pModel;
	const char* pcGeometry = pPart->pchGeometry;
	if (!pcGeometry)
		return true; // null geometry is allowed for now

	pSet = modelHeaderSetFind(pcGeometry);
	if (!pSet && (!pcModelName || !strStartsWith(pcGeometry, "object_library")))
	{
		CharacterFileErrorAndInvalidate(pCostume->pcFileName, "In Costume %s, Part %s, can not find geometry %s", pCostume->pcName, pPart->pchBoneName, pcGeometry);
		return false;
	}

	if (pcModelName)
	{
		pModel = wlModelHeaderFromNameEx(pcGeometry, pcModelName);
		if (!pModel)
		{
			CharacterFileErrorAndInvalidate(pCostume->pcFileName, "In Costume %s, Part %s, can not find model %s in geometry %s!", pCostume->pcName, pPart->pchBoneName, pcModelName, pcGeometry);
			return false;
		}
	}

	return true;
}

static bool extractMatConstantColor(MaterialNamedConstant*** peaMatConstant, ColorBGRA* pResult, const char* pcMNC) // pcMNC must be a pooled string
{
	FOR_EACH_IN_EARRAY((*peaMatConstant), MaterialNamedConstant, pMNC)
	{
		if (pMNC->name == pcMNC) // pooled strings
		{
			vec4ToColorBGRA(pResult, pMNC->value);
			return true;
		}
	}
	FOR_EACH_END;
	return false;
}

bool verifyCostume(SA_PARAM_NN_VALID WLCostume* pCostume, bool bRescaleConstants)
{
	U32 uiNumParts = eaUSize(&pCostume->eaCostumeParts);
	const SkelInfo* pSkelInfo = GET_REF(pCostume->hSkelInfo);
	const SkelScaleInfo* pScaleInfo = pSkelInfo?GET_REF(pSkelInfo->hScaleInfo):NULL;
	const char* pcBadBit;
	
	// Mega hack
	if (IsLoginServer() || IsAuctionServer())
	{
		return true;
	}

	PERFINFO_AUTO_START_FUNC();

	if (eaSize(&pCostume->eaConstantBits) > 0)
	{
		if (!gConf.bNewAnimationSystem)
		{
			pCostume->constantBits.ppcBits = pCostume->eaConstantBits;
			if (!dynBitFieldStaticSetFromStrings(&pCostume->constantBits, &pcBadBit))
			{
				CharacterFileErrorAndInvalidate(pCostume->pcFileName, "Invalid RequiresBit %s", pcBadBit);
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
		else
		{
			pCostume->constantBits.ppcBits = pCostume->eaConstantBits;
			pCostume->constantBits.uiNumBits = eaSize(&pCostume->constantBits.ppcBits);
			FOR_EACH_IN_EARRAY(pCostume->eaConstantBits, const char, pcStanceWord)
			{
				int i;

				if (!pcStanceWord || strlen(pcStanceWord) == 0)
				{
					CharacterFileErrorAndInvalidate(pCostume->pcFileName, "Costume %s: Found empty stance word!", pCostume->pcName);
					PERFINFO_AUTO_STOP();
					return false;
				}
				if (!dynAnimStanceValid(pcStanceWord))
				{
					CharacterFileErrorAndInvalidate(pCostume->pcFileName, "Costume %s: Found invalid stance word '%s', fix it or add it to dyn/AnimStance!", pCostume->pcName, pcStanceWord);
					PERFINFO_AUTO_STOP();
					return false;
				}

				// Verify no dups
				for (i = 0; i < ipcStanceWordIndex; i++)
				{
					if (pcStanceWord == pCostume->eaConstantBits[i])
					{
						CharacterFileErrorAndInvalidate(pCostume->pcFileName, "Costume %s: Found duplicate stance word %s!", pCostume->pcName, pcStanceWord);
						PERFINFO_AUTO_STOP();
						return false;
					}
				}
			}
			FOR_EACH_END;
		}
	}

	if (!pSkelInfo)
	{
		CharacterFileErrorAndInvalidate(pCostume->pcFileName, "Costume %s: Unable to find skelinfo %s", pCostume->pcName, REF_STRING_FROM_HANDLE(pCostume->hSkelInfo));
		PERFINFO_AUTO_STOP();
		return false;
	}
	else if (wlIsClient() && !pCostume->bForceNoLOD)
	{
		if (!pCostume->pBodySockInfo && pSkelInfo->bodySockInfo.pcBodySockGeo)
		{
			pCostume->pBodySockInfo = StructAlloc(parse_BodySockInfo);
			StructCopy(parse_BodySockInfo, &pSkelInfo->bodySockInfo, pCostume->pBodySockInfo, 0, 0, 0);
		}

		if (pCostume->pBodySockInfo || pSkelInfo->bLOD)
		{
			WLCostumePart* pLODPart = StructAlloc(parse_WLCostumePart);
			pLODPart->pchGeometry = pCostume->pBodySockInfo?pCostume->pBodySockInfo->pcBodySockGeo:NULL; // pooled string
			pCostume->pcBodysockPose = pCostume->pBodySockInfo?pCostume->pBodySockInfo->pcBodySockPose:NULL;
			pLODPart->pchMaterial = pcPooled_BodySock; // pooled string
			pLODPart->pchBoneName = pcPooled_Base; // pooled string
			pLODPart->bLOD = true;
			pLODPart->uiRequiredLOD = MAX_WORLD_REGION_LOD_LEVELS-1;
			pCostume->bHasLOD = true;
			eaPush(&pCostume->eaCostumeParts, pLODPart);
		}

	}

	if ( uiNumParts > 0 )
	{
		U32 uiPartIndex;
		// no longer indexed, if this needs sorting it will need to be done manually
		// eaSortUsingKey(&pCostume->eaCostumeParts, parse_CostumePart);
		for (uiPartIndex=0; uiPartIndex<uiNumParts; ++uiPartIndex)
		{
			WLCostumePart* pPart = pCostume->eaCostumeParts[uiPartIndex];
			/*
			if (pPart->pcModel && pPart->pcModel == pcPooled_BodySockModel && !pPart->bLOD && pCostume->pcName != pcPooled_BodySock) // pooled strings
			{
				Errorf("Should never find this model without LOD set!");
			}
			*/
			if (!verifyCostumeGeometryExists(pPart, pPart->pcModel, pCostume))
			{
				PERFINFO_AUTO_STOP();
				return false;
			}
			if (bRescaleConstants)
			{
				FOR_EACH_IN_EARRAY(pPart->eaMatConstant, MaterialNamedConstant, pMNC)
					scaleVec4(pMNC->value, U8TOF32_COLOR, pMNC->value);
				FOR_EACH_END;
			}
		}
	}

	// Verify dynfx
	if (!(wlGetLoadFlags() & WL_NO_LOAD_DYNFX)) {

		FOR_EACH_IN_EARRAY(pCostume->eaFXSwap, CostumeFXSwap, pFXSwap)
		{
			const DynFxInfo* pOldInfo = GET_REF(pFXSwap->hOldFx);
			const DynFxInfo* pNewInfo = GET_REF(pFXSwap->hNewFx);
			if (!pOldInfo)
			{
				CharacterFileErrorAndInvalidate(pCostume->pcFileName, "Unknown FX %s in costume %s", REF_STRING_FROM_HANDLE(pFXSwap->hOldFx), pCostume->pcName);
				PERFINFO_AUTO_STOP();
				return false;
			}
			if (!pNewInfo)
			{
				CharacterFileErrorAndInvalidate(pCostume->pcFileName, "Unknown FX %s in costume %s", REF_STRING_FROM_HANDLE(pFXSwap->hNewFx), pCostume->pcName);
				PERFINFO_AUTO_STOP();
				return false;
			}

			//commented this out because of the textparser threadsafe changeover, and Sam says this is irrelevant now
			/*
			if (*g_pparselist)
			{
				FileListInsert(g_pparselist, pOldInfo->pcFileName, 0);
				FileListInsert(g_pparselist, pNewInfo->pcFileName, 0);
			}
			*/
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY(pCostume->eaFX, CostumeFX, pFx)
			const DynFxInfo* pFxInfo = GET_REF(pFx->hFx);
			if (!pFxInfo)
			{
				CharacterFileErrorAndInvalidate(pCostume->pcFileName, "Unknown FX %s in costume %s", REF_STRING_FROM_HANDLE(pFx->hFx), pCostume->pcName);
				PERFINFO_AUTO_STOP();
				return false;
			}
		FOR_EACH_END;

	}

	// Verify scale values
	FOR_EACH_IN_EARRAY(pCostume->eaScaleValue, ScaleValue, pScaleValue)
	{
		if (
			pScaleValue->vScaleInputs[0] < -1.0f || pScaleValue->vScaleInputs[0] > 1.0f
			|| pScaleValue->vScaleInputs[1] < -1.0f || pScaleValue->vScaleInputs[1] > 1.0f
			|| pScaleValue->vScaleInputs[2] < -1.0f || pScaleValue->vScaleInputs[2] > 1.0f
			)

		{
			CharacterFileErrorAndInvalidate(pCostume->pcFileName, "Costume %s: Invalid ScaleValue %s: Values must be from -1 to 1!", pCostume->pcName, pScaleValue->pcScaleGroup);
			PERFINFO_AUTO_STOP();
			return false;
		}
		if (!pScaleInfo)
		{
			CharacterFileError(pCostume->pcFileName, "Costume %s: Must have valid ScaleInfo on Skeleton %s [file %s] for Scale to do anything!", pCostume->pcName, pSkelInfo->pcSkelInfoName, pSkelInfo->pcFileName);
		}
		else
		{
			bool bFound = false;
			FOR_EACH_IN_EARRAY(pScaleInfo->eaScaleGroup, SkelScaleGroup, pScaleGroup)
				if (pScaleValue->pcScaleGroup == pScaleGroup->pcGroupName) // pooled strings
					bFound = true;
			FOR_EACH_END
			if (!bFound)
				CharacterFileError(pCostume->pcFileName, "Costume %s: Unable to find scale group %s in scaleinfo %s!", pCostume->pcName, pScaleValue->pcScaleGroup, pScaleInfo->pcScaleInfoName);
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pCostume->eaScaleAnimInterp, WLScaleAnimInterp, pScaleAnimInterp)
	{
		bool bMatchFound = false;
		if (pScaleAnimInterp->fValue < -1.0f || pScaleAnimInterp->fValue > 1.0f)
		{
			CharacterFileError(pCostume->pcFileName, "Costume %s: Invalid BodyScale %s value %.2f, must be between -1 and 1. Clamping.", pCostume->pcName, pScaleAnimInterp->pcName, pScaleAnimInterp->fValue);
			pScaleAnimInterp->fValue = CLAMP(pScaleAnimInterp->fValue, -1.0f, 1.0f);
		}
		/*
		if (!pScaleInfo)
		{
			CharacterFileError(pCostume->pcFileName, "Costume %s: Skeleton must have valid ScaleInfo for Scale to do anything!", pCostume->pcName);
		}
		else
		{
			FOR_EACH_IN_EARRAY(pScaleInfo->eaScaleAnimTrack, SkelScaleAnimTrack, pScaleAnimTrack)
			{
				if (pScaleAnimTrack->pcName == pScaleAnimInterp->pcName)
				{
					bMatchFound = true;
					break;
				}
			}
			FOR_EACH_END;
			if (!bMatchFound)
			{
				CharacterFileError(pCostume->pcFileName, "Costume %s: Unable to find ScaleAnimTrack %s in %s", pCostume->pcName, pScaleAnimInterp->pcName, pScaleInfo->pcFileName);
			}
		}
		*/
	}
	FOR_EACH_END;

	PERFINFO_AUTO_STOP();
	return true;
}

void wlCostumeFree(WLCostume* pCostume)
{
	if (pCostume->pBodysockTexture)
		wl_state.gfx_bodysock_texture_release_callback(pCostume->pBodysockTexture, pCostume->iBodysockSectionIndex);
	FOR_EACH_IN_EARRAY(pCostume->eaSubCostumes, WLSubCostume, pSubCostume)
	{

	}
	FOR_EACH_END;
	SAFE_FREE(pCostume->constantBits.pBits);
	StructDestroy(parse_WLCostume, pCostume);
}


static void wlCostumeDictChangeCallback(enumResourceEventType eType, const char *pDictName, const char *pRefData, WLCostume *pCostume, void *pUserData)
{
	// Delete costume when it no longer has any references
	if (pCostume && (eType == RESEVENT_NO_REFERENCES) && !pCostume->bNoAutoCleanup) {
		RefSystem_RemoveReferent(pCostume, true);
		wlCostumeFree(pCostume);
	}
}


AUTO_RUN;
void registerCostumeDictionary(void)
{
	hWLCostumeDict = RefSystem_RegisterSelfDefiningDictionary("Costume", false, parse_WLCostume, true, true, NULL);
	resDictRegisterEventCallback(hWLCostumeDict, wlCostumeDictChangeCallback, NULL);		
	resDictSetSendNoReferencesCallback(hWLCostumeDict, true);
}


WLCostume* wlCostumeFromName(const char* pcName)
{
	// Assumes it's a world interaction key first, and a real costume name second
	WLCostume *pCostume = worldInteractionGetWLCostume(pcName);
	if(!pCostume) pCostume = RefSystem_ReferentFromString(hWLCostumeDict, pcName);
	return pCostume;
}



const SkelInfo* wlCostumeGetSkeletonInfo(const WLCostume* pCostume)
{
	return GET_REF(pCostume->hSkelInfo);
}

const DynBaseSkeleton* wlCostumeGetBaseSkeleton(const WLCostume* pCostume)
{
	const SkelInfo *skelInfo = GET_REF(pCostume->hSkelInfo);
	if (!skelInfo)
	{
		return NULL;
	}
	return GET_REF(skelInfo->hBaseSkeleton);
}


const char* wlCostumeSwapFX(const WLCostume* pCostume, const char* pcOldInfo)
{
	FOR_EACH_IN_EARRAY(pCostume->eaFXSwap, CostumeFXSwap, pFXSwap)
		if (stricmp(pcOldInfo, REF_STRING_FROM_HANDLE(pFXSwap->hOldFx))==0 && GET_REF(pFXSwap->hNewFx))
			return REF_STRING_FROM_HANDLE(pFXSwap->hNewFx);
	FOR_EACH_END;
	return pcOldInfo;
}

void wlCostumeAddToDictionary(WLCostume* pCostume, const char* pcNewName)
{
	Referent ref;

	PERFINFO_AUTO_START_FUNC();

	ref =  RefSystem_ReferentFromString(hWLCostumeDict, pcNewName);
	if (ref) 
	{
	    RefSystem_RemoveReferent(ref,false);
	}
	RefSystem_AddReferent(hWLCostumeDict, pcNewName, pCostume);

	// Destroy the old costume 
	if (ref)
	{
		wlCostumeFree(ref);
	}



	PERFINFO_AUTO_STOP();
}

bool wlCostumeRemoveByName(const char* pcCostume)
{
	WLCostume* pCostume = RefSystem_ReferentFromString(hWLCostumeDict, pcCostume);
	if (!pCostume)
	{
		Errorf("Unable to find costume %s", pcCostume);
		return false;
	}

	RefSystem_RemoveReferent(pCostume, false);
	return true;
}

bool wlCostumeGenerateBoneScaleTable(const WLCostume* pCostume, StashTable* stBoneScaleTable)
{
	// Look through skel_info
	const SkelInfo* pSkelInfo = GET_REF(pCostume->hSkelInfo);
	*stBoneScaleTable = stashTableCreateWithStringKeys(64, StashDefault);
	FOR_EACH_IN_EARRAY(pCostume->eaScaleValue, ScaleValue, pScaleValue)
		wlSkelInfoGetBoneScales(pSkelInfo, pScaleValue, eaSize(&pCostume->eaScaleAnimInterp)>0?pCostume->eaScaleAnimInterp[0]->fValue:0.0f, *stBoneScaleTable);
	FOR_EACH_END;
	return true;
}

bool wlCostumeDoesAnyOverrideValue(ShaderTemplate *shader_template, const char *op_name)
{
	FOR_EACH_IN_REFDICT(hWLCostumeDict, const WLCostume, pCostume)
	{
		FOR_EACH_IN_EARRAY(pCostume->eaCostumeParts, WLCostumePart, part)
		{
			const MaterialData *material_data;
			bool anyTemplateIsSame = false;
			
			if (!part->pchMaterial)
				continue;
			material_data = materialFindData(part->pchMaterial);
			if (!material_data)
				continue;
			if (!materialDataHasShaderTemplate(material_data, shader_template->template_name))
				continue;
			FOR_EACH_IN_EARRAY(part->eaMatConstant, MaterialNamedConstant, named_constant)
			{
				if (named_constant->name == op_name)
					return true;
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
	return false;
}

void wlCostumeApplyOverridePart(WLCostume* pCostume, WLCostumePart* pPart)
{
	int iIndex = eaSize(&pCostume->eaCostumeParts)-1;


	FOR_EACH_IN_EARRAY(pCostume->eaCostumeParts, WLCostumePart, pCurrentPart)
		if (pCurrentPart->pchBoneName == pPart->pchBoneName)
		{
			// We found a match, replace this part
			eaRemove(&pCostume->eaCostumeParts, iIndex);
			break;
		}
		--iIndex;
	FOR_EACH_END

	// Make a copy
	{
		WLCostumePart* pCopy = StructAlloc(parse_WLCostumePart);
		StructCopyAll(parse_WLCostumePart, pPart, pCopy);
		// no longer indexed, if this needs sorting it will need to be done manually
		eaPush(&pCostume->eaCostumeParts, pCopy);
	}
}

const char* wlCostumeGetThrowableGeometryHack(const WLCostume* pCostume)
{
	FOR_EACH_IN_EARRAY(pCostume->eaCostumeParts, WLCostumePart, pPart)
		if (pPart->pchGeometry)
			return pPart->pchGeometry;
	FOR_EACH_END;
	return NULL;
}

void wlCostumePushSubCostume( WLCostume* pSubCostume, WLCostume* pCostume ) 
{
	WLSubCostume* pNewSubCostume = StructCreate(parse_WLSubCostume);
	wlCostumeAddToDictionary(pSubCostume, pSubCostume->pcName);
	SET_HANDLE_FROM_REFERENT("Costume", pSubCostume, pNewSubCostume->hSubCostume);
	pNewSubCostume->pcAttachmentBone = pSubCostume->pcSubCostumeAttachmentBone;
	eaPush(&pCostume->eaSubCostumes, pNewSubCostume);
}

void wlCostumeAddRiderToMount(WLCostume *pRiderCostume, WLCostume *pMountCostume)
{
	WLSubCostume *pRiderAsSubCostume = StructCreate(parse_WLSubCostume);
	wlCostumeAddToDictionary(pRiderCostume, pRiderCostume->pcName);
	SET_HANDLE_FROM_REFERENT("Costume", pRiderCostume, pRiderAsSubCostume->hSubCostume);
	pRiderAsSubCostume->pcAttachmentBone = allocFindString("Mount_Rider");
	eaPush(&pMountCostume->eaSubCostumes, pRiderAsSubCostume);
	pMountCostume->bMount = true;
	pRiderCostume->bRider = true;
	FOR_EACH_IN_EARRAY(pRiderCostume->eaSubCostumes, WLSubCostume, pRiderSubCustume) {
		WLCostume *pModRiderSub;
		if (pModRiderSub = GET_REF(pRiderSubCustume->hSubCostume)) {
			pModRiderSub->bRiderChild = true;
		}
	} FOR_EACH_END;
}

U32 wlCostumeNumCostumes(void)
{
	return RefSystem_GetDictionaryNumberOfReferents(hWLCostumeDict);
}

#include "AutoGen/wlCostume_h_ast.c"

