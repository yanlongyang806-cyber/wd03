#include "gclUIGenPaperdoll.h"
#include "gclEntity.h"
#include "EntitySavedData.h"
#include "GameAccountDataCommon.h"
#include "GfxTexturesPublic.h"
#include "GraphicsLib.h"
#include "utilitiesLib.h"
#include "EntityBuild.h"
#include "error.h"
#include "Expression.h"
#include "GfxHeadshot.h"
#include "GfxTexAtlas.h"
#include "dynBitField.h"
#include "dynSequencer.h"
#include "dynSkeleton.h"
#include "Color.h"
#include "ResourceManager.h"
#include "WLCostume.h"
#include "Character.h"
#include "contact_common.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonGenerate.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "mission_common.h"
#include "NotifyCommon.h"
#include "gclNotify.h"
#include "Player.h"
#include "SavedPetCommon.h"
#include "StringCache.h"
#include "LoginCommon.h"
#include "StringUtil.h"
#include "MicroTransactionUI.h"
#include "Guild.h"
#include "species_common.h"
#include "gclCostumeOnly.h"
#include "EntityLib.h"
#include "AnimList_Common.h"

#include "gclMicroTransactions.h"
#include "gclCostumeUnlockUI.h"
#include "gclCostumeUI.h"
#include "CostumeCommonTailor.h"
#include "GameClientLib.h"
#include "MemoryPool.h"

#include "GfxClipper.h"
#include "GfxSprite.h"
//#include "GfxTextures.h"
#include "MultiVal.h"

#include "itemArt.h"

#include "UIGen_h_ast.h"
#include "UICore_h_ast.h"
#include "AutoGen/gclUIGenPaperdoll_h_ast.h"
#include "AutoGen/gclUIGenPaperdoll_c_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/dynFxInfo_h_ast.h"

#define PAPERDOLL_DEFAULT_ANIMBITS "HEADSHOT IDLE NOLOD"
#define PAPERDOLL_DEFAULT_FRAME "Status"
#define PAPERDOLL_WLCOSTUME_PREFIX "UIGenPaperdoll|"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

MP_DEFINE(UIGenPaperdoll);
MP_DEFINE(UIGenPaperdollState);

AUTO_STRUCT;
typedef struct PaperdollEntityRef
{
	GlobalType eType;
	ContainerID uID;
	S32 iRefCount; AST(NAME(refCount))
	U32 uChangedTime;
} PaperdollEntityRef;

AUTO_STRUCT;
typedef struct PaperdollCostumeRef
{
	REF_TO(WLCostume) hCostume;
	S32 iRefCount; AST(NAME(refCount))
	U32 uChangedTime;
} PaperdollCostumeRef;

// Reference counted entity data used by the paperdoll gens
// to update headshots when the underlying dictionary referents change
static PaperdollEntityRef** s_eaPaperdollEntities = NULL;

static const char *s_pchNone = NULL;
static UIGen **s_ppAnimatedHeadshots; // All UIGenPaperdolls with Animated 1
static UIGen **s_ppFocusedGenHeadshots; // All UIGenPaperdolls with Animated 1 and is in the focus path
static UIGen **s_ppHeadshotFocusHeadshots; // All UIGenPaperdolls with Animated 1 and has the HeadshotFocus flag set on the result

static S32 gclPaperdoll_FindEntityRef(ContainerRef* pRef)
{
	S32 i;
	for (i = eaSize(&s_eaPaperdollEntities)-1; i >= 0; i--)
	{
		PaperdollEntityRef* pEntRef = s_eaPaperdollEntities[i];
		if (pEntRef->eType == pRef->containerType && pEntRef->uID == pRef->containerID)
		{
			return i;
		}
	}
	return -1;
}

static bool gclPaperdoll_RemoveEntityRef(ContainerRef* pRef, bool bRemoveAllRefs)
{
	S32 i = gclPaperdoll_FindEntityRef(pRef);
	PaperdollEntityRef* pEntRef = eaGet(&s_eaPaperdollEntities, i);
	if (pEntRef)
	{
		if (bRemoveAllRefs || --pEntRef->iRefCount <= 0)
		{
			StructDestroy(parse_PaperdollEntityRef, eaRemove(&s_eaPaperdollEntities, i));
		}
		return true;
	}
	return false;
}

static bool gclPaperdoll_AddEntityRef(Entity* pEntity)
{
	ContainerRef Ref = {0};
	Ref.containerType = pEntity ? entGetType(pEntity) : 0;
	Ref.containerID = pEntity ? entGetContainerID(pEntity) : 0;
	if (pEntity)
	{
		S32 i = gclPaperdoll_FindEntityRef(&Ref);
		PaperdollEntityRef* pEntRef = eaGet(&s_eaPaperdollEntities, i);
		
		if (pEntRef)
		{
			pEntRef->iRefCount++;
		}
		else
		{
			pEntRef = StructCreate(parse_PaperdollEntityRef);
			pEntRef->eType = Ref.containerType;
			pEntRef->uID = Ref.containerID;
			pEntRef->uChangedTime = g_ui_State.totalTimeInMs;
			pEntRef->iRefCount = 1;
			eaPush(&s_eaPaperdollEntities, pEntRef);
			return true;
		}
	}
	return false;
}

// Returns true if the entity has changed in the dictionary
static bool gclPaperdoll_UpdateCostumeDataChangedTime(UIGenPaperdollState* pState)
{
	if (pState->EntityContainerRef.containerType && pState->EntityContainerRef.containerID)
	{
		S32 i = gclPaperdoll_FindEntityRef(&pState->EntityContainerRef);
		if (i >= 0)
		{
			PaperdollEntityRef* pEntRef = s_eaPaperdollEntities[i];
			if (pEntRef->uChangedTime > pState->uLastUpdateTime || pState->uDisplayDataTime > pState->uLastUpdateTime)
			{
				pState->uLastUpdateTime = MAX(pState->uDisplayDataTime, pEntRef->uChangedTime);
				return true;
			}
		}
	}
	return false;
}

// Generic paperdoll camera function used by animated headshots.
// Exits early if the paperdoll doesn't want camera updates.
static void gclPaperdoll_UpdateCamera(UIGen* pGen, DynSkeleton *pSkel, GroupDef* ignored, Vec3 vCamPos, Vec3 vCamDir)
{
	Vec3 vCam = {0};
	UIGenPaperdoll* pPaperdoll = UI_GEN_RESULT(pGen, Paperdoll);
	UIGenPaperdollState* pState = UI_GEN_STATE(pGen, Paperdoll);
	HeadshotStyleDef* pStyle = GET_REF(pState->hHeadshotStyle);
	Vec3 v3AutoCenter;
	F32 fRadius = pState->fRadius;
	F32 fHeight = pState->fCachedEntityHeight;
	S32 i;

	if (!pState->bUpdateCamera)
	{
		return;
	}

	if (!pPaperdoll->bUpdateCamera && pStyle && pStyle->pContactCameraParams)
	{
		// Update using the headshot style
		setVec3(vCamPos, pStyle->pContactCameraParams->fRight, pStyle->pContactCameraParams->fAbove, pStyle->pContactCameraParams->fForward);

		// This cannot support camera roll...
		vCamDir[0] = sin(RAD(pStyle->pContactCameraParams->fYaw)) * cos(RAD(pStyle->pContactCameraParams->fPitch));
		vCamDir[1] = sin(RAD(pStyle->pContactCameraParams->fPitch));
		vCamDir[2] = cos(RAD(pStyle->pContactCameraParams->fYaw)) * cos(RAD(pStyle->pContactCameraParams->fPitch));
		return;
	}

	// If the Paperdoll is sizing based off of the skeleton, do
	// the calculations here. Since at this point, the skeleton has
	// been updated and has valid visibility extents.
	if (pState->bUseSkeletonRadius)
	{
		F32 fFOVy = contact_HeadshotStyleDefGetFOV(pStyle, pPaperdoll->fFOVy);
		Vec3 v3ExtentMin;
		Vec3 v3ExtentMax;
		Vec3 v3ExtentDif;

		if (fFOVy <= 0)
			fFOVy = DEFAULT_FOV;

		// Start with the visibility extents
		if (!pState->bExtentsInitialized || !pPaperdoll->bUpdateExtentsOnce)
		{
			dynSkeletonGetVisibilityExtents(pSkel, v3ExtentMin, v3ExtentMax);
			pState->bExtentsInitialized = true;
			for (i = 0; i < 3; i++)
			{
				v3ExtentMin[i] = MIN(pState->v3ExtentsMin[i], v3ExtentMin[i]);
				v3ExtentMax[i] = MAX(pState->v3ExtentsMax[i], v3ExtentMax[i]);
			}
		}
		else
		{
			copyVec3(pState->v3ExtentsMin, v3ExtentMin);
			copyVec3(pState->v3ExtentsMax, v3ExtentMax);
		}

		// Calculate the radius and height to use
		subVec3(v3ExtentMax, v3ExtentMin, v3ExtentDif);
		fRadius = lengthVec3(v3ExtentDif);
		if( fRadius == 0 ) {
			// If the costume being captured is incomplete, then it is expected that the skeleton has equal min and max.
			//Also if the costume has no geo, the radius will be zero.
			//Just set it to some arbitrary value to prevent a divide by zero
			fRadius = 3;
		}
		fRadius = tanf(RAD(fFOVy / 2)) * 2.0f / fRadius + 1.1f * fRadius;
		fHeight = vecY(v3ExtentDif);

		// If centering based off the skeleton, calculate the center here
		if (pPaperdoll->bAutoCenter)
		{
			scaleVec3(v3ExtentDif, .5f, v3ExtentDif);
			subVec3(v3ExtentMax, v3ExtentDif, v3AutoCenter);
		}

		for (i = 0; i < 3; i++)
		{
			pState->v3ExtentsMin[i] = v3ExtentMin[i];
			pState->v3ExtentsMax[i] = v3ExtentMax[i];
		}
	}

	// Sadly not enough information to have a perfect "examine cam"
	vCam[0] = sin(RAD(pState->fYaw)) * cos(RAD(pState->fPitch));
	vCam[1] = sin(RAD(pState->fPitch));
	vCam[2] = cos(RAD(pState->fYaw)) * cos(RAD(pState->fPitch));

	vCamPos[0] = pState->fZoom * fRadius * vCam[0];
	vCamPos[1] = pState->fZoom * fRadius * vCam[1] + pState->fZoomHeight * fHeight;
	vCamPos[2] = pState->fZoom * fRadius * vCam[2];

	// If AutoCenter is set, then use the center of the skeleton bounding box
	if (pState->bUseSkeletonRadius && pPaperdoll->bAutoCenter)
	{
		addVec3(vCamPos, v3AutoCenter, vCamPos);
	}

	vCamDir[0] = -vCam[0];
	vCamDir[1] = -vCam[1];
	vCamDir[2] = -vCam[2];

	assert(FINITEVEC3(vCamPos));

	copyVec3(vCamPos, pState->v3LastCamPos);
	copyVec3(vCamDir, pState->v3LastCamDir);
}

// Called whenever a referent in the GLOBALTYPE_ENTITYSAVEDPET copy dictionary changes.
// This is required for changes to pet costumes.
// If the resource has been modified, then a new headshot is generated.
static void gclPaperdoll_EntityCopyDictChanged(enumResourceEventType eType, 
											   const char *pDictName, 
											   const char *pRefData,
											   Entity *pEnt, 
											   void *pUserData)
{
	ContainerRef Ref = {0};
	if (pEnt)
	{
		Ref.containerType = entGetType(pEnt);
		Ref.containerID = entGetContainerID(pEnt);
	}
	switch (eType) 
	{
		xcase RESEVENT_RESOURCE_MODIFIED: 
		{
			S32 i = gclPaperdoll_FindEntityRef(&Ref);
			if (i >= 0)
			{
				s_eaPaperdollEntities[i]->uChangedTime = g_ui_State.totalTimeInMs;
			}
		}
		xcase RESEVENT_RESOURCE_REMOVED:
		{
			gclPaperdoll_RemoveEntityRef(&Ref, true);
		}
	}
}

static void gclPaperdoll_Init(void)
{
	static bool s_bPaperdollInit = false;
	if (!s_bPaperdollInit)
	{
		s_bPaperdollInit = true;
		resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET), gclPaperdoll_EntityCopyDictChanged, NULL);
	}
}

// Cleans up Entity-specific information on the paperdoll state
static void gclPaperdoll_CleanupEntityData(UIGenPaperdollState* pState)
{
	gclPaperdoll_RemoveEntityRef(&pState->EntityContainerRef, false);
	pState->EntityContainerRef.containerType = 0;
	pState->EntityContainerRef.containerID = 0;
	REMOVE_HANDLE(pState->hEntity);
}

static void gclPaperdoll_ResetCostumeState(UIGenPaperdollState* pState)
{
	pState->bRedraw = true;
	pState->bExtentsInitialized = false;
	zeroVec3(pState->v3ExtentsMax);
	zeroVec3(pState->v3ExtentsMin);
}

static void gclPaperdoll_HeadshotRelease(UIGenPaperdoll *pPaperdoll, UIGenPaperdollState* pState)
{
	// Make current texture the stale texture if possible
	if (pPaperdoll->bRenderStaleHeadshots && !pPaperdoll->bAnimated
		&& !pState->bCreated && pState->pTexture && gfxHeadshotIsFinishedCostume(pState->pTexture))
	{
		if (pState->pStaleTexture)
			gfxHeadshotRelease(pState->pStaleTexture);
		pState->pStaleTexture = pState->pTexture;
		pState->pTexture = NULL;
	}

	// Cleanup the texture
	if (pState->pTexture)
	{
		gfxHeadshotRelease(pState->pTexture);
		pState->pTexture = NULL;
	}
}

// Reset all costume data on the paperdoll state, which includes the Entity, WLCostume, and PlayerCostume
static void gclPaperdoll_ResetCostumeData(UIGenPaperdollState* pState)
{
	gclPaperdoll_CleanupEntityData(pState);
	REMOVE_HANDLE(pState->hCostume);
	pState->pCostume = NULL;
	pState->uLastUpdateTime = 0;
	gclPaperdoll_ResetCostumeState(pState);
}

// Update the WLCostume and clear the Entity and PlayerCostume information on the paperdoll state
static void gclPaperdoll_UpdateWLCostume(UIGenPaperdollState* pState, WLCostume* pWLCostume)
{
	if (!pWLCostume)
	{
		gclPaperdoll_ResetCostumeData(pState);
	}
	else
	{
		if (stricmp(REF_STRING_FROM_HANDLE(pState->hCostume), pWLCostume->pcName) != 0)
		{
			SET_HANDLE_FROM_REFERENT("Costume", pWLCostume, pState->hCostume);
			gclPaperdoll_ResetCostumeState(pState);
		}
		gclPaperdoll_CleanupEntityData(pState);
		pState->pCostume = NULL;
	}
}

static WLCostume *gclPaperdoll_CreateWLCostume(UIGenPaperdollState *pState, PlayerCostume *pCostume, SpeciesDef *pSpecies)
{
	WLCostume** eaSubCostumes = NULL;
	WLCostume *pWLCostume;

	if (!pState->pchWLCostumeName)
	{
		static U32 s_uNames;
		pState->uWLCostumeId = ++s_uNames;
		pState->pchWLCostumeName = costumeGenerate_CreateWLCostumeName(NULL, PAPERDOLL_WLCOSTUME_PREFIX, GLOBALTYPE_NONE, pState->uWLCostumeId, 0);
	}

	if (!pSpecies)
		pSpecies = GET_REF(pCostume->hSpecies);

	// Create the world layer costume
	pWLCostume = costumeGenerate_CreateWLCostume(pCostume, pSpecies, NULL, NULL, NULL, NULL, NULL, PAPERDOLL_WLCOSTUME_PREFIX, GLOBALTYPE_NONE, pState->uWLCostumeId, false, &eaSubCostumes);
	if (pWLCostume)
	{
		FOR_EACH_IN_EARRAY(eaSubCostumes, WLCostume, pSubCostume)
			wlCostumePushSubCostume(pSubCostume, pWLCostume);
		FOR_EACH_END;
		wlCostumeAddToDictionary(pWLCostume, pState->pchWLCostumeName);
		SET_HANDLE_FROM_STRING("Costume", pState->pchWLCostumeName, pState->hCostume);
	}
	else
	{
		REMOVE_HANDLE(pState->hCostume);
	}
	eaDestroy(&eaSubCostumes);

	return pWLCostume;
}

// Update the PlayerCostume pointer and create a new WLCostume which will be added to the costume dictionary
static void gclPaperdoll_UpdatePlayerCostume(UIGenPaperdollState* pState, PlayerCostume* pPlayerCostume, ItemArt* pArt, const char*** pppchAddedFX)
{
	if (!pPlayerCostume)
	{
		gclPaperdoll_ResetCostumeData(pState);
	}
	else
	{
		WLCostume *pWLCostume = GET_REF(pState->hCostume);
		bool bRefreshForFX = false;
		int i;

		//check if FX differ:
		if(eaSize(&pState->ppchAddedFX) != eaSize(pppchAddedFX))
		{
			bRefreshForFX = true;
		}
		else
		{
			for(i=0; i<eaSize(pppchAddedFX); i++)
			{
				if(eaFind(pppchAddedFX, pState->ppchAddedFX[i]) == -1)
					// an effect in one isn't in the other.
					bRefreshForFX = true;
			}
		}

		
		if (pState->pCostume != pPlayerCostume || pState->pItemArt != pArt || !pWLCostume || !pWLCostume->bComplete || pState->uDisplayDataTime > pState->uLastUpdateTime || bRefreshForFX)
		{
			NOCONST(PlayerCostume)* pNewCostume = NULL;
			CostumeDisplayData** eaData = NULL;
			pState->uLastUpdateTime = pState->uDisplayDataTime;

			if (pState->pDisplayData)
			{
				eaPush(&eaData, pState->pDisplayData);
				pNewCostume = costumeTailor_ApplyOverrideSet(pPlayerCostume, NULL, eaData, NULL);
				eaDestroy(&eaData);
			}
			if (pNewCostume)
				gclPaperdoll_CreateWLCostume(pState, (PlayerCostume*)pNewCostume, NULL);
			else
				gclPaperdoll_CreateWLCostume(pState, pPlayerCostume, NULL);
			pState->pCostume = pPlayerCostume;
			gclPaperdoll_CleanupEntityData(pState);
			if (pState->eaExtraFX && eaSize(&pState->eaExtraFX) > 0)
			{
				eaClearStruct(&pState->eaExtraFX, parse_PCFXTemp);
			}
			pState->pItemArt = pArt;

			if (pState->ppchAddedFX)
			{
				eaDestroy(&pState->ppchAddedFX);
				pState->ppchAddedFX = NULL;
			}

			eaCopy(&pState->ppchAddedFX, pppchAddedFX);
			gclPaperdoll_ResetCostumeState(pState);
			return;
		}
		gclPaperdoll_CleanupEntityData(pState);
	}
}

// Get the effective costume on the Entity, or the active saved costume if that isn't available
static PlayerCostume* gclPaperdoll_GetPlayerCostumeFromEntity(UIGenPaperdollState* pState, Entity* pEntity, bool* pbCreatedNewCostume)
{
	PlayerCostume* pPlayerCostume = costumeEntity_GetEffectiveCostume(pEntity);  // Live entities
	if (!pPlayerCostume || pState->pDisplayData)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		NOCONST(PlayerCostume)* pNewCostume = NULL;
		CostumeDisplayData** eaData = NULL;
		PCSlotType* pSlotType = NULL;
		pPlayerCostume = costumeEntity_GetActiveSavedCostume(pEntity); // Pets and other non-live entities
		pSlotType = costumeEntity_GetActiveSavedSlotType(pEntity);
		if (!pPlayerCostume || !pPlayerCostume->bPlayerCantChange)
		{
			item_GetItemCostumeDataToShow(PARTITION_CLIENT, pEntity, &eaData, pExtract);
		}
		if (pState->pDisplayData)
			eaPush(&eaData, pState->pDisplayData);
		if (eaSize(&eaData)) 
		{
			S32 i;
			SpeciesDef* pSpecies = pEntity->pChar ? GET_REF(pEntity->pChar->hSpecies) : NULL;
			pNewCostume = costumeTailor_ApplyOverrideSet(pPlayerCostume, pSlotType, eaData, pSpecies);

			for (i = eaSize(&eaData)-1; i >= 0; --i)
			{
				if (eaData[i] != pState->pDisplayData )
				{
					eaDestroy(&eaData[i]->eaCostumes);
					eaDestroy(&eaData[i]->eaAddedFX);
					eaDestroyStruct(&eaData[i]->eaCostumesOwned, parse_PlayerCostume);
					free(eaData[i]);
				}
			}
			eaDestroy(&eaData);
		}
		if (pNewCostume)
		{
			pPlayerCostume = (PlayerCostume*)pNewCostume;
			(*pbCreatedNewCostume) = true;
		}
	}
	return pPlayerCostume;
}

// Updates Entity-specific costume data on the paperdoll state
static void gclPaperdoll_UpdateEntity(UIGenPaperdollState* pState, Entity* pEntity)
{
	if (!pEntity)
	{
		gclPaperdoll_ResetCostumeData(pState);
	}
	else if (pState->EntityContainerRef.containerType != entGetType(pEntity) ||
		pState->EntityContainerRef.containerID != entGetContainerID(pEntity) ||
		gclPaperdoll_UpdateCostumeDataChangedTime(pState))
	{
		WLCostume* pWLCostume;

		REMOVE_HANDLE(pState->hCostume);

		if (GET_REF(pEntity->hWLCostume) && !pState->pDisplayData)
		{
			pWLCostume = GET_REF(pEntity->hWLCostume);
			SET_HANDLE_FROM_REFERENT("Costume", pWLCostume, pState->hCostume);
		}
		else
		{
			bool bCleanupCostume = false;
			PlayerCostume* pPlayerCostume = gclPaperdoll_GetPlayerCostumeFromEntity(pState, pEntity, &bCleanupCostume);
			pWLCostume = pPlayerCostume ? gclPaperdoll_CreateWLCostume(pState, pPlayerCostume, SAFE_GET_REF2(pEntity, pChar, hSpecies)) : NULL;
			if (bCleanupCostume)
				StructDestroy(parse_PlayerCostume, pPlayerCostume);
		}

		if (pWLCostume)
		{
			if (pState->EntityContainerRef.containerType != entGetType(pEntity) ||
				pState->EntityContainerRef.containerID != entGetContainerID(pEntity))
			{
				gclPaperdoll_RemoveEntityRef(&pState->EntityContainerRef, false);
				gclPaperdoll_AddEntityRef(pEntity);

				pState->EntityContainerRef.containerType = entGetType(pEntity);
				pState->EntityContainerRef.containerID = entGetContainerID(pEntity);
			}

			SET_HANDLE_FROM_REFERENT(GlobalTypeToCopyDictionaryName(entGetType(pEntity)), pEntity, pState->hEntity);
			pState->pCostume = NULL;

			gclPaperdoll_ResetCostumeState(pState);
		}
		else
		{
			gclPaperdoll_ResetCostumeData(pState);
		}
	}
}

static Color gclPaperDoll_HeadshotStyleGetBackgroundColor(UIGenPaperdoll* pPaperdoll,
														  UIGenPaperdollState* pState, 
														  HeadshotStyleDef* pStyle)
{
	if (pState->pBackground)
	{
		if (pStyle)
		{
			return colorFromRGBA(pStyle->uiBackgroundColor);
		}
		return colorFromRGBA(ui_StyleColorPaletteIndex(pPaperdoll->uBackgroundColor));
	}
	return ColorTransparent;
}

static void gclPaperdoll_HeadshotStyleSetAnimBits(UIGenPaperdollState* pState, 
												  HeadshotStyleDef* pStyle, 
												  const char* pchFallbackAnimBits,
												  AIAnimList* pAnimList)
{
	static char* s_estrAnimListBits;
	const char* pchAnimBits;

	if (pAnimList && eaSize(&pAnimList->bits) > 0)
	{
		S32 i;
		for (i = 0; i < eaSize(&pAnimList->bits); i++)
		{
			if (pAnimList->bits[i] && *pAnimList->bits[i])
			{
				if (s_estrAnimListBits && *s_estrAnimListBits)
					estrConcatChar(&s_estrAnimListBits, ' ');
				estrAppend2(&s_estrAnimListBits, pAnimList->bits[i]);
			}
		}
		pchAnimBits = s_estrAnimListBits;
	}
	else if (SAFE_MEMBER(pStyle, pchAnimBits) || pchFallbackAnimBits)
	{
		pchAnimBits = pStyle && pStyle->pchAnimBits ? pStyle->pchAnimBits : pchFallbackAnimBits;
	}
	else
	{
		pchAnimBits = PAPERDOLL_DEFAULT_ANIMBITS;
	}

	if (!pState->pchLastBits || stricmp(pState->pchLastBits, pchAnimBits) != 0)
	{
		dynBitFieldGroupClearAll(&pState->BitFieldGroup);
		dynBitFieldGroupAddBits(&pState->BitFieldGroup, (char*)pchAnimBits, true);
		if (pState->pchLastBits)
			StructFreeString(pState->pchLastBits);
		pState->pchLastBits = StructAllocString(pchAnimBits);
		pState->bRedraw = true;
	}

	if (s_estrAnimListBits && *s_estrAnimListBits)
		estrClear(&s_estrAnimListBits);
}

static void gclPaperdoll_HeadshotStyleSetAnimKeyword(	UIGenPaperdoll		*pDoll,
														UIGenPaperdollState	*pState,
														HeadshotStyleDef	*pStyle,
														AIAnimList			*pAnimList)
{
	const char *pcAnimKeyword;

	if (pAnimList && pAnimList->animKeyword)
		pcAnimKeyword = pAnimList->animKeyword;
	else if (SAFE_MEMBER(pStyle, pcAnimKeyword))
		pcAnimKeyword = pStyle->pcAnimKeyword;
	else
		pcAnimKeyword = pDoll->pcAnimKeyword;

	if (pState->pcAnimKeyword != pcAnimKeyword)
	{
		pState->pcAnimKeyword = pcAnimKeyword;
		gclPaperdoll_ResetCostumeState(pState);
	}
}

static const char* gclPaperdoll_HeadshotStyleGetFrame(UIGenPaperdoll *pPaperdoll, HeadshotStyleDef* pStyle)
{
	if (pStyle && pStyle->pchFrame)
	{
		return pStyle->pchFrame;
	}
	if (pPaperdoll && pPaperdoll->pchFrame)
	{
		// HACK: The Guild Uniform previewer uses a NULL frame, so this needs to
		// be able to provide "NULL".
		return pPaperdoll->pchFrame == s_pchNone ? NULL : pPaperdoll->pchFrame;
	}
	return PAPERDOLL_DEFAULT_FRAME;
}

static void gclui_GenPaperdollUpdate(UIGen* pGen)
{
	UIGenPaperdoll* pPaperdoll = UI_GEN_RESULT(pGen, Paperdoll);
	UIGenPaperdollState* pState = UI_GEN_STATE(pGen, Paperdoll);
	const char* pchHeadshotStyle = pPaperdoll->pchHeadshotStyle;
	const char *pchBackground = pPaperdoll->pchBackgroundTexture;
	HeadshotStyleDef* pStyle = GET_REF(pState->hHeadshotStyle);
	const char* pcAnimKeyword = pPaperdoll->pcAnimKeyword;
	AIAnimList* pAnimList = NULL;
	bool bFocusedGen = ui_GenInState(pGen, kUIGenStateFocused) || ui_GenInState(pGen, kUIGenStateFocusedAncestor) || pGen->pParent && ui_GenInState(pGen->pParent, kUIGenStateFocusedChild);

	gclPaperdoll_Init();

	// If there currently is no animated costume, pick a number to determine who gets
	// to animate next.
	if (pPaperdoll->bAnimated)
		eaPushUnique(&s_ppAnimatedHeadshots, pGen);
	else
		eaFindAndRemove(&s_ppAnimatedHeadshots, pGen);
	if (pPaperdoll->bAnimated && bFocusedGen)
		eaPushUnique(&s_ppFocusedGenHeadshots, pGen);
	else
		eaFindAndRemove(&s_ppFocusedGenHeadshots, pGen);
	if (pPaperdoll->bAnimated && pPaperdoll->bHeadshotFocus)
		eaPushUnique(&s_ppHeadshotFocusHeadshots, pGen);
	else
		eaFindAndRemove(&s_ppHeadshotFocusHeadshots, pGen);

	// Evaluate the headshot expression, which should return a PaperdollHeadshotData pointer
	if (pPaperdoll->pHeadshotExpr)
	{
		MultiVal mv = {0};
		PaperdollHeadshotData* pData;
		S32 iValidate;

		pState->iUsedHeadshotData = 0;
		ui_GenEvaluate(pGen, pPaperdoll->pHeadshotExpr, &mv);
		pData = MultiValGetPointer(&mv, NULL);
		iValidate = eaFind(&pState->eaHeadshotData, pData);

		if (pData && iValidate >= 0 && iValidate < pState->iUsedHeadshotData)
		{
			if (pData->pWLCostume)
			{
				gclPaperdoll_UpdateWLCostume(pState, pData->pWLCostume);
			}
			else if (pData->pPlayerCostume)
			{
				gclPaperdoll_UpdatePlayerCostume(pState, pData->pPlayerCostume, pData->pItemArt, &pData->ppchAddedFX);
			}
			else if (pData->pEntity)
			{
				gclPaperdoll_UpdateEntity(pState, pData->pEntity);
			}
			else
			{
				gclPaperdoll_ResetCostumeData(pState);
			}

			if (pData->pAnimList)
			{
				pAnimList = pData->pAnimList;
			}

			// If a headshot style is specified, override the one on the paperdoll
			if (pData->pchHeadshotStyle)
			{
				pchHeadshotStyle = allocAddString(pData->pchHeadshotStyle);
			}
		} 
		else
		{
			if (pData)
				ErrorFilenamef(pGen->pchFilename, "%s: HeadshotExpr returned an invalid PaperdollHeadshotData", pGen->pchName);

			gclPaperdoll_ResetCostumeData(pState);
		}
	}
	else
	{
		gclPaperdoll_ResetCostumeData(pState);
	}

	if (REF_STRING_FROM_HANDLE(pState->hHeadshotStyle) != pchHeadshotStyle)
	{
		pStyle = pchHeadshotStyle ? RefSystem_ReferentFromString("HeadshotStyleDef", pchHeadshotStyle) : NULL;
		if (pStyle)
			SET_HANDLE_FROM_REFERENT("HeadshotStyleDef", pStyle, pState->hHeadshotStyle);
		else
			REMOVE_HANDLE(pState->hHeadshotStyle);
	}

	if (pStyle && pStyle->pchBackground)
		pchBackground = pStyle->pchBackground;
	if (pchBackground && !*pchBackground)
		pchBackground = NULL;
	pState->bUpdateCamera = pPaperdoll->bUpdateCamera || pStyle && pStyle->pContactCameraParams;

	// Apply updates every frame.
	if (!gConf.bNewAnimationSystem)
		gclPaperdoll_HeadshotStyleSetAnimBits(pState, pStyle, pPaperdoll->pchAnimBits, pAnimList);
	else
		gclPaperdoll_HeadshotStyleSetAnimKeyword(pPaperdoll, pState, pStyle, pAnimList);

	UI_GEN_LOAD_TEXTURE(pPaperdoll->pchMaskTexture, pState->pMask);
	if (stricmp_safe(pState->pchLastBackground, pchBackground) != 0)
	{
		pState->pBackground = pchBackground ? texLoadBasic(pchBackground, TEX_LOAD_IN_BACKGROUND, WL_FOR_UI) : NULL;
		if (pState->pchLastBackground)
			StructFreeString(pState->pchLastBackground);
		pState->pchLastBackground = StructAllocString(pchBackground);
		pState->bRedraw = true;
	}
}

static void gclui_GenPaperdoll_CreateFXFromStyle(UIGenPaperdollState* pState, HeadshotStyleDef* pStyle)
{
	int i;
	for (i = eaSize(&pStyle->eaHeadshotFX)-1; i >= 0; i--)
	{
		HeadshotStyleFX* pStyleFX = pStyle->eaHeadshotFX[i];
		PCFXTemp *pFX = StructCreate(parse_PCFXTemp);
		pFX->pcName = pStyleFX->pchFXName;
		eaPush(&pState->eaExtraFX, pFX);
	}
}

static void gclui_GenPaperdollCapture(UIGen *pGen, UIGenPaperdoll* pPaperdoll, UIGenPaperdollState* pState, bool bAnimate)
{
	char pchTextureName[MAX_PATH];
	WLCostume* pCostume = GET_REF(pState->hCostume);
	HeadshotStyleDef* pStyle = GET_REF(pState->hHeadshotStyle);
	Color BackgroundColor = gclPaperDoll_HeadshotStyleGetBackgroundColor(pPaperdoll, pState, pStyle);
	F32 fFOVy = contact_HeadshotStyleDefGetFOV(pStyle, pPaperdoll->fFOVy);
	const char* pchSky = pStyle ? pStyle->pchSky : pPaperdoll->pchSky;
	const char* pchFrame = gclPaperdoll_HeadshotStyleGetFrame(pPaperdoll, pStyle);
	U32 uWidth = pPaperdoll->uRenderWidth;
	U32 uHeight = pPaperdoll->uRenderHeight;
	Entity* pEntity = entFromContainerIDAnyPartition(pState->EntityContainerRef.containerType, pState->EntityContainerRef.containerID);
	int i = 0;

	if (!uWidth)
		uWidth = (U32)CBoxWidth(&pGen->ScreenBox);
	if (!uHeight)
		uHeight = (U32)CBoxHeight(&pGen->ScreenBox);

	if (pCostume)
	{
		sprintf(pchTextureName, "Headshot_%s_%s", pGen->pchName, pCostume->pcName);
	}

	if (!devassertmsg(!pState->pTexture, "Should only be capturing the costume when there is no texture"))
	{
		gclPaperdoll_HeadshotRelease(pPaperdoll, pState);
	}

	eaClearStruct(&pState->eaExtraFX, parse_PCFXTemp);

	if (pState->pItemArt)
	{
		eaPush(&pState->eaExtraFX, gclItemArt_GetItemPreviewFX(pState->pItemArt));
	}

	for (i = 0; i < eaSize(&pState->ppchAddedFX); i++)
	{
		PCFXTemp* pFX = StructCreate(parse_PCFXTemp);
		pFX->pcName = allocAddString(pState->ppchAddedFX[i]);
		eaPush(&pState->eaExtraFX, pFX);
	}

	if(pState && pEntity)
	{
		CharacterClass *pClass = pEntity && pEntity->pChar ? GET_REF(pEntity->pChar->hClass) : NULL;
		gclEntity_UpdateItemArtAnimFX(pEntity, pClass, &pState->eaExtraFX, true);
	}
	if (bAnimate && pStyle && eaSize(&pStyle->eaHeadshotFX))
	{
		gclui_GenPaperdoll_CreateFXFromStyle(pState, pStyle);
	}

	if (bAnimate) 
	{
		// Create an animated headshot texture
		pState->pTexture = gfxHeadshotCaptureAnimatedCostumeScene(pchTextureName,
																  uWidth,
																  uHeight,
																  pCostume,
																  pState->pBackground,
																  pState->bUpdateCamera ? NULL : pchFrame,
																  BackgroundColor,
																  !!pState->bUpdateCamera,
																  &pState->BitFieldGroup,
																  pState->pcAnimKeyword,
																  pStyle ? pStyle->pchStance : NULL,
																  pState->bUpdateCamera ? gclPaperdoll_UpdateCamera : NULL,
																  pGen,
																  fFOVy,
																  pchSky,
																  &pState->eaExtraFX);
		pState->bRedraw = false;
		pState->bAnimating = true;
		pState->bWasAnimating = false;
		pState->bCreated = true;
	}
	else if (pCostume->bComplete && (pState->bWasAnimating || pState->bUpdateCamera))
	{
		// Create a static headshot of the animated headshot
		pState->pTexture = gfxHeadshotCaptureCostumeScene(pchTextureName,
														  uWidth,
														  uHeight,
														  pCostume,
														  pState->pBackground,
														  pState->bUpdateCamera ? NULL : pchFrame,
														  BackgroundColor,
														  !!pState->bUpdateCamera,
														  &pState->BitFieldGroup,
														  pState->pcAnimKeyword,
														  pStyle ? pStyle->pchStance : NULL,
														  pState->v3LastCamPos,
														  pState->v3LastCamDir,
														  -1,
														  fFOVy,
														  pchSky,
														  &pState->eaExtraFX);
		pState->bRedraw = false;
		pState->bCreated = true;
	}
	else if (pCostume->bComplete) 
	{
		// Create a static headshot texture
		pState->pTexture = gfxHeadshotCaptureCostume(pchTextureName,
													 uWidth,
													 uHeight,
													 pCostume,
													 pState->pBackground,
													 pchFrame,
													 BackgroundColor,
													 false,
													 &pState->BitFieldGroup,
													 pState->pcAnimKeyword,
													 pStyle ? pStyle->pchStance : NULL,
													 fFOVy,
													 pchSky,
													 &pState->eaExtraFX,
													 NULL,
													 NULL);
		pState->bRedraw = false;
		pState->bCreated = true;
	}
	else
	{
		pState->pTexture = NULL;
		pState->bCreated = false;
	}

	if (pState->pTexture)
	{
		gfxHeadshotFlagForUI(pState->pTexture);
		if (pPaperdoll->bLowerRenderPriority)
			gfxHeadshotLowerPriority(pState->pTexture);
		else
			gfxHeadshotRaisePriority(pState->pTexture);
	}
}

static void gclui_GenPaperdollLayoutLate(UIGen* pGen)
{
	UIGenPaperdoll* pPaperdoll = UI_GEN_RESULT(pGen, Paperdoll);
	UIGenPaperdollState* pState = UI_GEN_STATE(pGen, Paperdoll);
	Entity* pEntity = GET_REF(pState->hEntity) ? GET_REF(pState->hEntity) : entFromContainerIDAnyPartition(pState->EntityContainerRef.containerType, pState->EntityContainerRef.containerID);
	WLCostume* pCostume = GET_REF(pState->hCostume);
	HeadshotStyleDef* pStyle = GET_REF(pState->hHeadshotStyle);
	F32 fFOVy = contact_HeadshotStyleDefGetFOV(pStyle, pPaperdoll->fFOVy);
	char pchTextureName[MAX_PATH];
	bool bUpdateRadius = false;

	if (pCostume)
	{
		sprintf(pchTextureName, "Headshot_%s_%s", pGen->pchName, pCostume->pcName);
	}

	// Check to see if the texture needs to be regenerated
	if (pState->pTexture && (pState->bRedraw ||
		 texHeight(pState->pTexture) != pPaperdoll->uRenderHeight ||
		 texWidth(pState->pTexture) != pPaperdoll->uRenderWidth ||
		 stricmp(texFindName(pState->pTexture), pchTextureName)
		))
	{
		gclPaperdoll_HeadshotRelease(pPaperdoll, pState);
		pState->bAnimating = false;
		pState->bWasAnimating = false;
		pState->bCreated = false;

		if (pState->bUpdateCamera)
		{
			bUpdateRadius = true;
		}
	}
	else if (pEntity && pState->bUpdateCamera && !pPaperdoll->bAutoCenter &&
			!nearSameF32(pState->fCachedEntityHeight, entGetHeight(pEntity)))
	{
		bUpdateRadius = true;
	}
	else if (pState->bUpdateCamera)
	{
		pState->bUseSkeletonRadius = true;
	}

	if (bUpdateRadius)
	{
		// NB: If the Paperdoll is set to auto-center, it will ignore
		// the entity and base the center off skeleton calculations.
		if (pEntity && !pPaperdoll->bAutoCenter)
		{
			// Get the height of the entity
			F32 fHeight = entGetHeight(pEntity);

			// Set the radius to fit the character inside the the texture
			pState->fCachedEntityHeight = fHeight;
			pState->fRadius = tanf(RAD(fFOVy / 2)) * 2.0f / fHeight + 1.1f * fHeight;
			pState->bUseSkeletonRadius = false;
		}
		else
		{
			// Use the skeleton to determine the radius.
			pState->bUseSkeletonRadius = true;
		}
	}
}

static void gclui_GenPaperdollDrawEarly(UIGen *pGen)
{
	UIGenPaperdoll* pPaperdoll = UI_GEN_RESULT(pGen, Paperdoll);
	UIGenPaperdollState* pState = UI_GEN_STATE(pGen, Paperdoll);
	WLCostume* pCostume = GET_REF(pState->hCostume);
	HeadshotStyleDef* pStyle = GET_REF(pState->hHeadshotStyle);
	bool bBackgroundOnly = SAFE_MEMBER(pStyle, bUseBackgroundOnly);
	CBox* pBox = &pGen->ScreenBox;
	bool bHeadshotReady;

	if (pCostume && !bBackgroundOnly)
	{
		bool bAnimate = false;

		if (pPaperdoll->bAnimated)
		{
			if (eaSize(&s_ppHeadshotFocusHeadshots) > 0)
				bAnimate = eaGetLast(&s_ppHeadshotFocusHeadshots) == pGen;
			else if (eaSize(&s_ppFocusedGenHeadshots) > 0)
				bAnimate = eaGetLast(&s_ppFocusedGenHeadshots) == pGen;
			else if (eaSize(&s_ppAnimatedHeadshots) > 0)
				bAnimate = eaGetLast(&s_ppAnimatedHeadshots) == pGen;
		}

		if ((pState->bAnimating && !bAnimate)
			|| (pState->bAnimating && pState->pTexture && !gfxHeadshotIsAnimatedCostume(pState->pTexture)))
		{
			// Animation was stolen from this gen (or some other system)
			if (pState->pTexture && gfxHeadshotIsAnimatedCostume(pState->pTexture))
				gclPaperdoll_HeadshotRelease(pPaperdoll, pState);
			pState->bAnimating = false;
			pState->bWasAnimating = true;
			bAnimate = false;
		}
		else if (!pState->bAnimating && bAnimate && !gfxHeadshotIsAnimatedCostume(NULL))
		{
			if (pState->pTexture)
				gclPaperdoll_HeadshotRelease(pPaperdoll, pState);
			pState->bAnimating = true;
			pState->bWasAnimating = false;
		}

		if (!pState->pTexture)
			gclui_GenPaperdollCapture(pGen, pPaperdoll, pState, bAnimate);
	}
	else
	{
		gclPaperdoll_HeadshotRelease(pPaperdoll, pState);
		pState->bAnimating = false;
		pState->bWasAnimating = false;
		pState->bCreated = false;
	}

	bHeadshotReady = !pState->bCreated && pState->pTexture && gfxHeadshotIsFinishedCostume(pState->pTexture);

	// Clean up stale texture now that the new texture is ready
	if (bHeadshotReady && pState->pTexture && pState->pStaleTexture)
	{
		gfxHeadshotRelease(pState->pStaleTexture);
		pState->pStaleTexture = NULL;
	}

	// Do not render the headshot texture until it's ready.
	if ((pState->pTexture && bHeadshotReady) || pState->pStaleTexture || pState->pBackground)
	{
		BasicTexture* pTexture = pState->pTexture && bHeadshotReady ? pState->pTexture : pState->pStaleTexture ? pState->pStaleTexture : pState->pBackground;
		U32 uColor;
		F32 fWidth = (F32)texWidth(pTexture);
		F32 fHeight = (F32)texHeight(pTexture);
		F32 pfTexU[4] = {0, 1, 0, 1};
		F32 pfTexV[4] = {0, 1, 0, 1};
		F32 fScaleX = CBoxWidth(pBox) / fWidth;
		F32 fScaleY = CBoxHeight(pBox) / fHeight;
		F32 fCenterX, fCenterY;

		switch (pPaperdoll->eMode)
		{
		xcase kUIGenHeadshotMode_Scaled:
			fScaleX = fScaleY = min(fScaleX, fScaleY);
			CBoxGetCenter(pBox, &fCenterX, &fCenterY);
			BuildCBox(pBox, fCenterX - fWidth * fScaleX / 2, fCenterY - fHeight * fScaleY / 2, fWidth * fScaleX, fHeight * fScaleY);
		xcase kUIGenHeadshotMode_Filled:
			fScaleX = fScaleY = max(fScaleX, fScaleY);
			CBoxGetCenter(pBox, &fCenterX, &fCenterY);
			BuildCBox(pBox, fCenterX - fWidth * fScaleX / 2, fCenterY - fHeight * fScaleY / 2, fWidth * fScaleX, fHeight * fScaleY);
		}

		{
			F32 fHeadshotPixels = fWidth/fHeight * (pBox->hy - pBox->ly);
			F32 fBoxPixels = (pBox->hx - pBox->lx);
			F32 fClipU = (fHeadshotPixels - fBoxPixels) / fHeadshotPixels / 2.0f;

			pfTexU[0] = fClipU;
			pfTexU[1] = 1.0f - fClipU;
		}

		if (pState->pTexture && bHeadshotReady)
		{
			uColor = ColorRGBAMultiplyAlpha(0xFFFFFFFF, pGen->chAlpha);
		}
		else
		{
			uColor = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(pPaperdoll->uBackgroundColor), pGen->chAlpha);
		}

		display_sprite_effect_ex(
			NULL, pTexture, pState->pMask, NULL, pBox->lx, pBox->ly, UI_GET_Z(),
			fScaleX, fScaleY, uColor, uColor, uColor, uColor,
			pfTexU[0], pfTexV[0], pfTexU[1], pfTexV[1],
			pfTexU[2], pfTexV[2], pfTexU[3], pfTexV[3], 
			RAD(pPaperdoll->fRotation), false, clipperGetCurrent(), 
			RdrSpriteEffect_None, 1.0f, false);
	}

	// HACK: Setting these states here, because this is the "earliest" they can be
	// set and have impact when the result is being created (doing it in the Update
	// is too late, because the result has already been built).
	ui_GenStates(pGen,
		kUIGenStatePaperdollNotReady, !bHeadshotReady,
		kUIGenStatePaperdollReady, bHeadshotReady,
		kUIGenStateNone);
	pState->bCreated = false;
}

static void gclui_GenPaperdollHide(UIGen* pGen)
{
	UIGenPaperdollState* pState = UI_GEN_STATE(pGen, Paperdoll);

	REMOVE_HANDLE(pState->hCostume);

	eaFindAndRemove(&s_ppAnimatedHeadshots, pGen);
	eaFindAndRemove(&s_ppFocusedGenHeadshots, pGen);
	eaFindAndRemove(&s_ppHeadshotFocusHeadshots, pGen);

	if (pState->pStaleTexture)
	{
		gfxHeadshotRelease(pState->pStaleTexture);
		pState->pStaleTexture = NULL;
	}

	if (pState->pTexture)
	{
		gfxHeadshotRelease(pState->pTexture);
		pState->pTexture = NULL;
	}

	if (pState->pDisplayData)
	{
		eaDestroy(&pState->pDisplayData->eaCostumes);
		eaDestroyStruct(&pState->pDisplayData->eaCostumesOwned, parse_PlayerCostume);
		free(pState->pDisplayData);
		pState->pDisplayData = NULL;
	}

	pState->bAnimating = false;
	pState->bWasAnimating = false;
	pState->bCreated = false;

	eaDestroyStruct(&pState->eaHeadshotData, parse_PaperdollHeadshotData);

	gclPaperdoll_ResetCostumeData(pState);
}

PaperdollHeadshotData* gclPaperdoll_CreateHeadshotData(ExprContext* pContext,
													   WLCostume* pWLCostume,
													   PlayerCostume* pPlayerCostume,
													   Entity* pEntity,
													   ItemArt* pArt,
													   const char* pchHeadshotStyle,
													   const char*** ppchAddedFX)
{
	static PaperdollHeadshotData *s_pPaperdollData = NULL;
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	UIGenPaperdollState *pState;
	PaperdollHeadshotData* pData;

	if (!UI_GEN_IS_TYPE(pGen, kUIGenTypePaperdoll))
		return NULL;

	pState = UI_GEN_STATE(pGen, Paperdoll);
	if (!pState)
		return NULL;

	pData = eaGetStruct(&pState->eaHeadshotData, parse_PaperdollHeadshotData, pState->iUsedHeadshotData++);
	pData->pWLCostume = pWLCostume;
	pData->pPlayerCostume = pPlayerCostume;
	pData->pEntity = pEntity;
	pData->pchHeadshotStyle = pchHeadshotStyle;
	pData->pItemArt = pArt;
	if (ppchAddedFX)
		eaCopy(&pData->ppchAddedFX, ppchAddedFX);
	return pData;
}
 
static bool gclPaperdoll_IsValidRenderSize(U32 uWidth, U32 uHeight)
{
	// TODO: probably should make a common function for use with gfxHeadshotValidateSizeForRuntime

	if( uWidth == 128 && uHeight == 128 ) {
		return true;
	}
	if( uWidth == 256 && uHeight == 256 ) {
		return true;
	}
	if( uWidth == 512 && uHeight == 512 ) {
		return true;
	}
	if( uWidth == 256 && uHeight == 512 ) {
		return true;
	}
	if( uWidth == 1024 && uHeight == 1024 ) {
		return true;
	}
	if( uWidth == 300 && uHeight == 400 ) {
		return true;
	}

	return false;
}

static bool gclui_GenPaperdollValidate(UIGen *pGen, UIGenInternal *pInt, const char *pchDescriptor)
{
	UIGenPaperdoll *pPaperdoll = (UIGenPaperdoll *)pInt;

	if (!gclPaperdoll_IsValidRenderSize(pPaperdoll->uRenderWidth, pPaperdoll->uRenderHeight))
	{
		InvalidDataErrorFilenamefInternal(__FILE__, __LINE__, pGen->pchFilename, "%s: %dx%d is not a valid UIGenPaperdoll render size. Valid sizes are 128x128, 256x512, 512x512, and 1024x1024.", pGen->pchName, pPaperdoll->uRenderWidth, pPaperdoll->uRenderHeight);
		return false;
	}
	return true;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_HIDE ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclPaperDoll_resetAnimated(Entity *pPaperDollEnt)
{
	if(pPaperDollEnt)
	{
		S32 i;
		for (i = eaSize(&s_eaPaperdollEntities) - 1; i >= 0; i--)
		{
			if (s_eaPaperdollEntities[i]->eType == entGetType(pPaperDollEnt)
				&& s_eaPaperdollEntities[i]->uID == entGetContainerID(pPaperDollEnt))
			{
				s_eaPaperdollEntities[i]->uChangedTime = g_ui_State.totalTimeInMs;
			}
		}
	}
}

AUTO_RUN;
void gclui_GenRegisterPaperdoll(void)
{
	s_pchNone = allocAddStaticString("None");

	MP_CREATE(UIGenPaperdoll, 32);
	MP_CREATE(UIGenPaperdollState, 32);

	ui_GenRegisterType(kUIGenTypePaperdoll,
		gclui_GenPaperdollValidate,
		UI_GEN_NO_POINTERUPDATE,
		gclui_GenPaperdollUpdate,
		UI_GEN_NO_LAYOUTEARLY,
		gclui_GenPaperdollLayoutLate,
		UI_GEN_NO_TICKEARLY,
		UI_GEN_NO_TICKLATE,
		gclui_GenPaperdollDrawEarly,
		UI_GEN_NO_FITCONTENTSSIZE,
		UI_GEN_NO_FITPARENTSIZE,
		gclui_GenPaperdollHide,
		UI_GEN_NO_INPUT,
		UI_GEN_NO_UPDATECONTEXT, 
		UI_GEN_NO_QUEUERESET);
}

#include "AutoGen/gclUIGenPaperdoll_h_ast.c"
#include "AutoGen/gclUIGenPaperdoll_c_ast.c"
