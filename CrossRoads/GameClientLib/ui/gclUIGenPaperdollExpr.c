#include "gclUIGenPaperdollExpr.h"
#include "GameClientLib.h"
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
#include "itemart.h"

#include "gclMicroTransactions.h"
#include "gclCostumeUnlockUI.h"
#include "gclCostumeUI.h"
#include "gclCostumeUIState.h"
#include "gclCostumeLineUI.h"
#include "CostumeCommonTailor.h"
#include "GuildUI.h"
#include "UIGen.h"

#include "AutoGen/gclUIGenPaperdoll_h_ast.h"
#include "AutoGen/gclUIGenPaperdollExpr_c_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_STRUCT;
typedef struct PaperdollCostumeGenCache {
	// The last time this cache entry was used
	U32 uLastUpdateTime;

	// The source file
	const char *pchFilename; AST(POOL_STRING)

	// The source costumes
	PlayerCostume *pSource1; AST(UNOWNED)
	PlayerCostume *pSource2; AST(UNOWNED)

	// An arbitrary pointer you may use to key off of to determine when to use a cached costume or not. 
	void* pvKey; NO_AST
	void* pvKey2; NO_AST
	void* pvKey3; NO_AST
	S32 iKeyInt;
	F32 fKeyFloat;

	// The generated costume
	PlayerCostume *pResult;
} PaperdollCostumeGenCache;

#define CacheUpdate(pCache) ((pCache)->uLastUpdateTime = gGCLState.totalElapsedTimeMs)
#define CacheUse(pHeadshotData, pCache) (CacheUpdate(pCache), ((pHeadshotData)->pPlayerCostume = (pCache)->pResult), (pHeadshotData))
#define CacheNewUse(pContext, pCache) (CacheUpdate(pCache), gclPaperdoll_CreateHeadshotData((pContext), NULL, (pCache)->pResult, NULL, NULL, NULL, NULL))

extern RemoteContact** geaCachedRemoteContacts;

static PaperdollCostumeGenCache** s_eaCache;

// This should be run after the UI has run. It will free any costumes
// that were not used the last frame.
void gclPaperdollFlushCostumeGenCache(void)
{
	U32 uNow = gGCLState.totalElapsedTimeMs;
	S32 i;

	for (i = eaSize(&s_eaCache) - 1; i >= 0; --i)
	{
		if (s_eaCache[i]->uLastUpdateTime < uNow)
			StructDestroy(parse_PaperdollCostumeGenCache, eaRemove(&s_eaCache, i));
	}
}

static PlayerCostume **gclPaperdollGetUnlockedCostumes(Entity* pSubEnt)
{
	static PlayerCostume **s_eaUnlockedCostumes;
	static U32 s_iRegenFrame;

	Entity *pPlayer = entActivePlayerPtr();
	GameAccountData *pGAD = entity_GetGameAccount(pPlayer);
	S32 i;

	// This is probably too slow to run every frame.
	if (s_iRegenFrame != gGCLState.totalElapsedTimeMs)
	{
		if (pPlayer && pSubEnt)
			costumeEntity_GetUnlockCostumes(pPlayer->pSaved->costumeData.eaUnlockedCostumeRefs, pGAD, pPlayer, pSubEnt, &s_eaUnlockedCostumes);
		else if (pPlayer)
			costumeEntity_GetUnlockCostumes(pPlayer->pSaved->costumeData.eaUnlockedCostumeRefs, pGAD, pPlayer, pPlayer, &s_eaUnlockedCostumes);
		else
			costumeEntity_GetUnlockCostumes(NULL, pGAD, NULL, NULL, &s_eaUnlockedCostumes);

		if (g_pMTCostumes)
		{
			for (i = eaSize(&g_pMTCostumes->ppCostumes) - 1; i >= 0; i--)
			{
				MicroTransactionCostume *pMTCostume = g_pMTCostumes->ppCostumes[i];
				PlayerCostume *pCostume = GET_REF(g_pMTCostumes->ppCostumes[i]->hCostume);

				if (!pCostume || pMTCostume->bHidden)
				{
					continue;
				}

				eaPushUnique(&s_eaUnlockedCostumes, pCostume);
			}
		}

		s_iRegenFrame = gGCLState.totalElapsedTimeMs;
	}

	return s_eaUnlockedCostumes;
}

bool s_CostumeValidate = true;
AUTO_CMD_INT(s_CostumeValidate, CostumeValidate) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9) ACMD_HIDE;

// Generate a costume from two costumes. A base costume and an overlay costume.
SA_RET_OP_VALID static PaperdollHeadshotData* gclPaperdollGenOverlay(ExprContext* pContext,
																  SA_PARAM_OP_VALID PaperdollHeadshotData* pBase,
																  PlayerCostume* pOverlay,
																  const char* pchBoneGroup,
																  const char* pchSlotType,
																  Guild* pGuild,
																  void* pvKey)
{
	if (pBase && pBase->pPlayerCostume && pOverlay)
	{
		PCSlotType *pSlotType = NULL;
		PaperdollCostumeGenCache* pCache;
		PlayerCostume **eaUnlockedCostumes;

		// Check cache
		FOR_EACH_IN_EARRAY(s_eaCache, PaperdollCostumeGenCache, pCostume)
		{
			if (pCostume->pSource1 == pBase->pPlayerCostume && pCostume->pSource2 == pOverlay && pCostume->pvKey == pvKey)
			{
				return CacheUse(pBase, pCostume);
			}
		}
		FOR_EACH_END;

		if (pchBoneGroup && !*pchBoneGroup)
			pchBoneGroup = NULL;
		if (pchSlotType && *pchSlotType)
			pSlotType = costumeLoad_GetSlotType(pchSlotType);

		// Need to make a new costume
		pCache = StructCreate(parse_PaperdollCostumeGenCache);
		pCache->pSource1 = pBase->pPlayerCostume;
		pCache->pSource2 = pOverlay;
		pCache->pResult = StructClone(parse_PlayerCostume, pBase->pPlayerCostume);
		pCache->pvKey = pvKey;
		eaPush(&s_eaCache, pCache);

		eaUnlockedCostumes = gclPaperdollGetUnlockedCostumes(pBase->pEntity);
		costumeTailor_ApplyCostumeOverlay(pCache->pResult,
										  NULL, 
										  pOverlay,
										  eaUnlockedCostumes,
										  pchBoneGroup,
										  pSlotType,
										  true,
										  false,
										  true,
										  true);
		if (s_CostumeValidate)
		{
			costumeTailor_MakeCostumeValid(CONTAINER_NOCONST(PlayerCostume, pCache->pResult),
										   GET_REF(pCache->pResult->hSpecies),
										   eaUnlockedCostumes,
										   pSlotType,
										   false,
										   false,
										   false,
										   pGuild,
										   false,
										   NULL,
										   false,
										   NULL);
		}

		return CacheUse(pBase, pCache);
	}
	return pBase;
}

// Load a costume from a file.
SA_RET_OP_VALID static PlayerCostume* gclPaperdollLoadCostume(const char* pchFilename)
{
	const char *pchFile = NULL;

	if (pchFilename && *pchFilename)
		pchFile = allocAddString(pchFilename);

	if (pchFile)
	{
		PlayerCostume *pLoaded;
		PaperdollCostumeGenCache* pCache;

		FOR_EACH_IN_EARRAY(s_eaCache, PaperdollCostumeGenCache, pCostume)
		{
			if (pCostume->pchFilename == pchFile)
			{
				CacheUpdate(pCostume);
				return pCostume->pResult;
			}
		}
		FOR_EACH_END;

		// Load the costume
		pLoaded = CostumeOnly_LoadCostume(pchFile);
		if (!pLoaded)
			return NULL;

		// Make a new cache entry
		pCache = StructCreate(parse_PaperdollCostumeGenCache);
		pCache->pchFilename = pchFile;
		pCache->pResult = pLoaded;
		eaPush(&s_eaCache, pCache);

		CacheUpdate(pCache);
		return pCache->pResult;
	}
	return NULL;
}

// This structure maps to the Guild emblem data in the Guild structure.
typedef struct GuildEmblemData
{
	const char *pcEmblem; // Name of a PCTextureDef in CostumeTexture dictionary
	U32 iEmblemColor0;
	U32 iEmblemColor1;
	F32 fEmblemRotation; // [-PI, PI)
	const char *pcEmblem2; // Name of a PCTextureDef in CostumeTexture dictionary
	U32 iEmblem2Color0;
	U32 iEmblem2Color1;
	F32 fEmblem2Rotation; // [-PI, PI)
	F32 fEmblem2X; // -100 to 100
	F32 fEmblem2Y; // -100 to 100
	F32 fEmblem2ScaleX; // 0 to 100
	F32 fEmblem2ScaleY; // 0 to 100
	const char *pcEmblem3; // Name of a PCTextureDef in CostumeTexture dictionary (Detail)
} GuildEmblemData;

// Create a guild emblem costume
SA_RET_OP_VALID static PlayerCostume *gclCreateGuildEmblemCostume(ContainerID iGuildID, const char *pchSkeleton, const char *pchEmblemBone, GuildEmblemData *pEmblemData, bool bNoBackground)
{
	static PlayerCostume s_GuildEmblem = {0};
	NOCONST(PCPart) *pEmblemPart = NULL;
	NOCONST(PlayerCostume) *pCostume;
	PaperdollCostumeGenCache* pCache;
	PCSkeletonDef *pSkel;
	PCBoneDef *pEmblemBone = RefSystem_ReferentFromString(g_hCostumeBoneDict, pchEmblemBone);
	char achNameBuffer[256];
	S32 i;

	FOR_EACH_IN_EARRAY(s_eaCache, PaperdollCostumeGenCache, pCachedCostume)
	{
		if (pCachedCostume->pSource1 == &s_GuildEmblem && pCachedCostume->pvKey == (void*)iGuildID)
		{
			CacheUpdate(pCachedCostume);
			return pCachedCostume->pResult;
		}
	}
	FOR_EACH_END;

	pSkel = RefSystem_ReferentFromString(g_hCostumeSkeletonDict, pchSkeleton);
	if (!pSkel)
	{
		return NULL;
	}

	sprintf(achNameBuffer, "GuildEmblem_%u", iGuildID);

	// Initialize basic data
	pCostume = StructCreateNoConst(parse_PlayerCostume);
	pCostume->pcName = allocAddString(achNameBuffer);
	pCostume->pcFileName = allocAddString("gclUIGenPaperdollExpr.c");
	pCostume->eCostumeType = kPCCostumeType_Unrestricted;
	SET_HANDLE_FROM_REFERENT(g_hCostumeSkeletonDict, pSkel, pCostume->hSkeleton);

	// Initialize the costume data
	pCostume->fHeight = pSkel->fDefaultHeight ? pSkel->fDefaultHeight : 6;
	pCostume->fMuscle = pSkel->fDefaultMuscle ? pSkel->fDefaultMuscle : 20;
	for(i = 0; i < eaSize(&pSkel->eaBodyScaleInfo); ++i)
	{
		eafPush(&pCostume->eafBodyScales, i < eafSize(&pSkel->eafDefaultBodyScales) ? pSkel->eafDefaultBodyScales[i] : 20);
	}
	costumeTailor_SetDefaultSkinColor(pCostume, NULL, NULL);
	costumeTailor_FillAllBones(pCostume, NULL, NULL, NULL, true, false, true);
	costumeTailor_MakeCostumeValid(pCostume, NULL, NULL, NULL, true, true, false, NULL, true, NULL, false, NULL);

	// STO fixup hack
	if (stricmp(GetShortProductName(), "ST") == 0)
	{
		PCBoneDef *pEBone = RefSystem_ReferentFromString(g_hCostumeBoneDict, "Emblem");
		PCBoneDef *pBBone = RefSystem_ReferentFromString(g_hCostumeBoneDict, "EmblemBackground");
		NOCONST(PCPart) *pEmblem = NULL, *pBackground = NULL;
		for (i = 0; i < eaSize(&pCostume->eaParts); i++)
		{
			if (pEBone && GET_REF(pCostume->eaParts[i]->hBoneDef) == pEBone)
				pEmblem = pCostume->eaParts[i];
			if (pBBone && GET_REF(pCostume->eaParts[i]->hBoneDef) == pBBone)
				pBackground = pCostume->eaParts[i];
		}
		if (pBackground)
		{
			SET_HANDLE_FROM_STRING(g_hCostumeGeometryDict, "E_Emblembackground_01", pBackground->hGeoDef);
			SET_HANDLE_FROM_STRING(g_hCostumeMaterialDict, "E_Emblembackground_01_Character_Master_Basic_01", pBackground->hMatDef);
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, "Emblembackground_01_C", pBackground->hPatternTexture);
			if (bNoBackground)
			{
				memset(pBackground->color0, 0, sizeof(pBackground->color0));
				memset(pBackground->color1, 0, sizeof(pBackground->color1));
				memset(pBackground->color2, 0, sizeof(pBackground->color2));
				memset(pBackground->color3, 0, sizeof(pBackground->color3));
			}
		}
		if (pEmblem)
		{
			SET_HANDLE_FROM_STRING(g_hCostumeGeometryDict, "E_Emblem_Placeholder_01", pEmblem->hGeoDef);
			SET_HANDLE_FROM_STRING(g_hCostumeMaterialDict, "Emblem_Character_Master_02", pEmblem->hMatDef);
			SET_HANDLE_FROM_STRING(g_hCostumeMaterialDict, "Emblem_Patch_01_N", pEmblem->hDetailTexture);
		}
	}

	// Grab the parts to fill
	for (i = 0; i < eaSize(&pCostume->eaParts); i++)
	{
		if (pEmblemBone && GET_REF(pCostume->eaParts[i]->hBoneDef) == pEmblemBone)
			pEmblemPart = pCostume->eaParts[i];
	}

	// Fill in data
	if (pEmblemPart)
	{
		if (pEmblemData->pcEmblem)
		{
			if (!pEmblemPart->pTextureValues)
				pEmblemPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pEmblemData->pcEmblem, pEmblemPart->hPatternTexture);
			pEmblemPart->pTextureValues->fPatternValue = pEmblemData->fEmblemRotation;
			// Not exactly happy about this, but since the original code does it.
			// I'm going to keep it here for backward compatibility. -JM
			*((U32 *) pEmblemPart->color2) = pEmblemData->iEmblemColor0;
			*((U32 *) pEmblemPart->color3) = pEmblemData->iEmblemColor1;
		}
		if (pEmblemData->pcEmblem2)
		{
			if (!pEmblemPart->pMovableTexture)
				pEmblemPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pEmblemData->pcEmblem2, pEmblemPart->pMovableTexture->hMovableTexture);
			pEmblemPart->pMovableTexture->fMovableRotation = pEmblemData->fEmblem2Rotation;
			pEmblemPart->pMovableTexture->fMovableX = pEmblemData->fEmblem2X;
			pEmblemPart->pMovableTexture->fMovableY = pEmblemData->fEmblem2Y;
			pEmblemPart->pMovableTexture->fMovableScaleX = pEmblemData->fEmblem2ScaleX;
			pEmblemPart->pMovableTexture->fMovableScaleY = pEmblemData->fEmblem2ScaleY;
			// Not exactly happy about this, but since the original code does it.
			// I'm going to keep it here for backward compatibility. -JM
			*((U32 *) pEmblemPart->color0) = pEmblemData->iEmblem2Color0;
			*((U32 *) pEmblemPart->color1) = pEmblemData->iEmblem2Color1;
		}
		if (pEmblemData->pcEmblem3)
		{
			SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pEmblemData->pcEmblem3, pEmblemPart->hDetailTexture);
		}
	}

	// Make a new cache entry
	pCache = StructCreate(parse_PaperdollCostumeGenCache);
	pCache->pSource1 = &s_GuildEmblem;
	pCache->pvKey = (void*)iGuildID;
	pCache->pResult = CONTAINER_RECONST(PlayerCostume, pCostume);
	eaPush(&s_eaCache, pCache);

	CacheUpdate(pCache);
	return pCache->pResult;
}

///////////////////////////////////////////////////////////////////////////////
// Expression functions that deal with the paperdoll state.

// Get the costume used by the headshot.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPaperdollGetCostume");
SA_RET_OP_VALID PlayerCostume* gclExprPaperdollGetCostume(SA_PARAM_NN_VALID UIGen* pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypePaperdoll))
	{
		UIGenPaperdollState* pState = UI_GEN_STATE(pGen, Paperdoll);
		return pState ? pState->pCostume : NULL;
	}
	return NULL;
}

// Deprecated: Use GenPaperdollRotate
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollRotate");
bool gclExprPaperdollRotate(SA_PARAM_NN_VALID UIGen* pGen, F32 fdYaw, F32 fdPitch);

// Rotate the headshot of the current player.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPaperdollRotate");
bool gclExprPaperdollRotate(SA_PARAM_NN_VALID UIGen* pGen, F32 fdYaw, F32 fdPitch)
{
	bool bChanged = false;
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypePaperdoll))
	{
		UIGenPaperdoll* pPaperdoll = UI_GEN_RESULT(pGen, Paperdoll);
		UIGenPaperdollState* pState = UI_GEN_STATE(pGen, Paperdoll);
		if (fdYaw)
		{
			F32 fOrigYaw = pState->fYaw;

			// Add the yaw, but keep the rotation normalized
			pState->fYaw += fdYaw;
			while (pState->fYaw < 0)
				pState->fYaw += 360;
			while (pState->fYaw >= 360)
				pState->fYaw -= 360;

			bChanged |= !nearSameF32(fOrigYaw, pState->fYaw);
		}
		if (fdPitch)
		{
			F32 fOrigPitch = pState->fPitch;

			// Add the pitch, but keep it away from straight up and down
			pState->fPitch += fdPitch;
			pState->fPitch = CLAMP(pState->fPitch, -89.0f, 89.0f);

			bChanged |= !nearSameF32(fOrigPitch, pState->fPitch);
		}
		if (bChanged)
		{
			pState->bRedraw = !pPaperdoll || !pPaperdoll->bUpdateCamera;
		}
	}
	return bChanged;
}

// DEPRECATED; Use GenPaperdollRotate
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollRotateSimple");
bool gclExprPaperdollRotateSimple(ExprContext* pContext, F32 fdYaw, F32 fdPitch)
{
	// DEPRECATED: Used in FightClub/data/ui/gens/Windows/CostumePreview.uigen
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	if (pGen)
	{
		return gclExprPaperdollRotate(pGen, fdYaw, fdPitch);
	}
	return false;
}

// Deprecated: Use GenPaperdollSetYaw
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollSetYaw");
bool gclExprPaperdollSetYaw(SA_PARAM_NN_VALID UIGen* pGen, F32 fYaw);

// Sets the rotation of the headshot of the current player.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPaperdollSetYaw");
bool gclExprPaperdollSetYaw(SA_PARAM_NN_VALID UIGen* pGen, F32 fYaw)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypePaperdoll))
	{
		UIGenPaperdoll* pPaperdoll = UI_GEN_RESULT(pGen, Paperdoll);
		UIGenPaperdollState* pState = UI_GEN_STATE(pGen, Paperdoll);

		// Normalize the yaw
		while (fYaw < 0)
			fYaw += 360;
		while (fYaw >= 360)
			fYaw -= 360;

		if (!nearSameF32(fYaw, pState->fYaw))
		{
			pState->fYaw = fYaw;
			pState->bRedraw = !pPaperdoll || !pPaperdoll->bUpdateCamera;
			return true;
		}
	}
	return false;
}

// DEPRECATED; Use PaperdollSetYaw
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollSetYawSimple");
bool gclExprPaperdollSetYawSimple(ExprContext* pContext, F32 fYaw)
{
	// DEPRECATED: Used in FightClub/data/ui/gens/Windows/CostumePreview.uigen
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	if (pGen)
	{
		return gclExprPaperdollSetYaw(pGen, fYaw);
	}
	return false;
}

// Deprecated: Use GenPaperdollSetPitch
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollSetPitch");
bool gclExprPaperdollSetPitch(SA_PARAM_NN_VALID UIGen* pGen, F32 fPitch);

// Sets the pitch of the headshot of the current player.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPaperdollSetPitch");
bool gclExprPaperdollSetPitch(SA_PARAM_NN_VALID UIGen* pGen, F32 fPitch)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypePaperdoll))
	{
		UIGenPaperdoll* pPaperdoll = UI_GEN_RESULT(pGen, Paperdoll);
		UIGenPaperdollState* pState = UI_GEN_STATE(pGen, Paperdoll);

		// Clamp the pitch
		fPitch = CLAMP(fPitch, -89.0f, 89.0f);

		if (!nearSameF32(fPitch, pState->fPitch))
		{
			pState->fPitch = fPitch;
			pState->bRedraw = !pPaperdoll || !pPaperdoll->bUpdateCamera;
			return true;
		}
	}
	return false;
}

// DEPRECATED; Use PaperdollSetPitch
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollSetPitchSimple");
bool gclExprPaperdollSetPitchSimple(ExprContext* pContext, F32 fPitch)
{
	// DEPRECATED: Used in FightClub/data/ui/gens/Windows/CostumePreview.uigen
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	if (pGen)
	{
		return gclExprPaperdollSetPitch(pGen, fPitch);
	}
	return false;
}

// Deprecated: Use GenPaperdollZoom
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollZoom");
bool gclExprPaperdollZoom(SA_PARAM_NN_VALID UIGen* pGen, F32 fZoom);

// Zoom the headshot of the current player. Accepts values from 1/4x standard to 4x standard inclusive.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPaperdollZoom");
bool gclExprPaperdollZoom(SA_PARAM_NN_VALID UIGen* pGen, F32 fZoom)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypePaperdoll))
	{
		UIGenPaperdoll* pPaperdoll = UI_GEN_RESULT(pGen, Paperdoll);
		UIGenPaperdollState* pState = UI_GEN_STATE(pGen, Paperdoll);

		fZoom = CLAMP(fZoom, 0.25f, 4.0f);
		if (!nearSameF32(fZoom, pState->fZoom))
		{
			pState->fZoom = fZoom;
			pState->bRedraw = !pPaperdoll || !pPaperdoll->bUpdateCamera;
			return true;
		}
	}

	return false;
}

// DEPRECATED; Use PaperdollZoom
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollZoomSimple");
bool gclExprPaperdollZoomSimple(ExprContext* pContext, F32 fZoom)
{
	// DEPRECATED: Used in FightClub/data/ui/gens/Windows/CostumePreview.uigen
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	if (pGen)
	{
		return gclExprPaperdollZoom(pGen, fZoom);
	}
	return false;
}

// Deprecated: GenPaperdollZoomHeight
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollZoomHeight");
bool gclExprPaperdollZoomHeight(SA_PARAM_NN_VALID UIGen* pGen, F32 fZoomHeight);

// Change the zoom height of the headshot of the current player. Accepts a value from 0 to 1, treated as a percentage of the character's height.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPaperdollZoomHeight");
bool gclExprPaperdollZoomHeight(SA_PARAM_NN_VALID UIGen* pGen, F32 fZoomHeight)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypePaperdoll))
	{
		UIGenPaperdollState* pState = UI_GEN_STATE(pGen, Paperdoll);

		fZoomHeight = CLAMP(fZoomHeight, 0.0f, 1.0f);
		if (!nearSameF32(fZoomHeight, pState->fZoomHeight))
		{
			pState->fZoomHeight = fZoomHeight;
			return true;
		}
	}
	return false;
}

// Change the size of the headshot. Accepts 128, 256, 512, and 1024 for the texture sizes.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollHeadshotSizeSimple");
bool gclExprPaperdollHeadshotSizeSimple(U32 uSize)
{
	// DEPRECATED: Used in FightClub/data/ui/gens/Windows/CostumePreview.uigen
	return false;
}

// Change the size of the headshot. Accepts 128, 256, 512, and 1024 for the texture sizes.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollBackgroundSimple");
bool gclExprPaperdollBackgroundSimple(const char* pchBackground)
{
	// DEPRECATED: Used in FightClub/data/ui/gens/Windows/CostumePreview.uigen
	return false;
}

// Sets which costume to display based on the index in the player's saved costumedata array. 
// Negative index indicates the current costume. 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollCostumeIndexSimple");
bool gclExprPaperdollCostumeIndexSimple(S32 iCostumeIndex)
{
	// DEPRECATED: Used in FightClub/data/ui/gens/HUD/PlayerStatus_Costume.uigen
	return false;
}

// Get the headshot of the current player using the default parameters
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollHeadshotSimple");
SA_RET_OP_VALID BasicTexture *gclExprPaperdollHeadshotSimple(void)
{
	// DEPRECATED: Used in FightClub/data/ui/gens/Windows/CostumePreview.uigen
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// Expression functions that set the costume data for the Paperdoll.

// Gets the Entity's PlayerCostume stored on the costume slot at the specified index
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetHeadshotFromCostumeSlot");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetHeadshotFromCostumeSlot(ExprContext* pContext,
																			   SA_PARAM_OP_VALID Entity* pEntity, 
																			   S32 iSlotIndex)
{
	if (pEntity && pEntity->pSaved)
	{
		PlayerCostumeSlot* pSlot = eaGet(&pEntity->pSaved->costumeData.eaCostumeSlots, iSlotIndex);
		if (pSlot)
		{
			return gclPaperdoll_CreateHeadshotData(pContext, NULL, pSlot->pCostume, NULL, NULL, NULL, NULL);
		}
	}
	return NULL;
}

// Gets the appropriate costume stored on the Entity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetHeadshotFromEnt");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetHeadshotFromEnt(ExprContext* pContext,
																	   SA_PARAM_OP_VALID Entity* pEntity)
{
	if (pEntity)
	{
		return gclPaperdoll_CreateHeadshotData(pContext, NULL, NULL, pEntity, NULL,  NULL, NULL);
	}
	return NULL;
}

// Gets the appropriate costume stored on the Entity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPaperdollSetDressingRoomItem");
void gclExprGenPaperdollSetDressingRoomItem(ExprContext* pContext, SA_PARAM_NN_VALID UIGen *pGen,
	SA_PARAM_OP_VALID Item* pItem)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypePaperdoll))
	{
		UIGenPaperdollState *pState = UI_GEN_STATE(pGen, Paperdoll);
		if (pState->pDisplayData)
		{
			eaDestroy(&pState->pDisplayData->eaCostumes);
			eaDestroyStruct(&pState->pDisplayData->eaCostumesOwned, parse_PlayerCostume);
			free(pState->pDisplayData);
		}
		pState->pDisplayData = !pItem ? NULL : item_GetCostumeDisplayData(PARTITION_CLIENT, entActivePlayerPtr(), pItem, GET_REF(pItem->hItem), 0, NULL, 0);
		pState->uDisplayDataTime = g_ui_State.totalTimeInMs;
	}
}

// Gets the appropriate costume stored on the Entity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPaperdollSetDyedDressingRoomItem");
void gclExprGenPaperdollSetDyedDressingRoomItem(ExprContext* pContext, SA_PARAM_NN_VALID UIGen *pGen,
	SA_PARAM_OP_VALID Item* pItem, SA_PARAM_OP_VALID Item* pDye, int iChannel)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypePaperdoll))
	{
		UIGenPaperdollState *pState = UI_GEN_STATE(pGen, Paperdoll);
		if (pState->pDisplayData)
		{
			eaDestroy(&pState->pDisplayData->eaCostumes);
			eaDestroyStruct(&pState->pDisplayData->eaCostumesOwned, parse_PlayerCostume);
			free(pState->pDisplayData);
		}
		pState->pDisplayData = !pItem ? NULL : item_GetCostumeDisplayData(PARTITION_CLIENT, entActivePlayerPtr(), pItem, GET_REF(pItem->hItem), 0, pDye, iChannel);
		if (pState->pDisplayData)
			pState->pDisplayData->iPriority = INT_MAX;
		pState->uDisplayDataTime = g_ui_State.totalTimeInMs;
	}
}

// Gets the appropriate costume stored on the Entity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPaperdollClearDressingRoomItem");
void gclExprGenPaperdollClearDressingRoomItem(ExprContext* pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypePaperdoll))
	{
		UIGenPaperdollState *pState = UI_GEN_STATE(pGen, Paperdoll);
		if (pState->pDisplayData)
		{
			eaDestroy(&pState->pDisplayData->eaCostumes);
			eaDestroyStruct(&pState->pDisplayData->eaCostumesOwned, parse_PlayerCostume);
			free(pState->pDisplayData);
		}
		pState->pDisplayData = NULL;
		pState->uDisplayDataTime = g_ui_State.totalTimeInMs;
	}
}

// Gets the appropriate costume stored on the PossibleCharacterChoice
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetHeadshotFromPossibleCharacterChoice");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetHeadshotFromPossibleCharacterChoice(ExprContext* pContext, 
	SA_PARAM_OP_VALID PossibleCharacterChoice* pChoice, S32 iIndex)
{
	if (pChoice && iIndex >= 0 && iIndex < eaSize(&pChoice->eaCostumes) && pChoice->eaCostumes[iIndex]->pConstCostume)
	{		
		return gclPaperdoll_CreateHeadshotData(pContext, NULL, pChoice->eaCostumes[iIndex]->pConstCostume, NULL, NULL,  NULL, NULL);
	}
	return NULL;
}

// Gets the appropriate costume stored on the Entity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetHeadshotFromPlayerCostume");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetHeadshotFromPlayerCostume(ExprContext* pContext,
																				 SA_PARAM_OP_VALID PlayerCostume* pCostume)
{
	if (pCostume)
	{
		return gclPaperdoll_CreateHeadshotData(pContext, NULL, pCostume, NULL, NULL,  NULL, NULL);
	}
	return NULL;
}

// Gets the appropriate costume stored on the Entity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetHeadshotFromItem");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetHeadshotFromItem(ExprContext* pContext,
	SA_PARAM_OP_VALID Item* pItem, const char* pchInvisCostumeName, bool bOverrideHeadshotStyle)
{
	ItemDef* pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	if (pItem && pItem->pSpecialProps && pItem->pSpecialProps->pTransmutationProps)
		pItemDef = GET_REF(pItem->pSpecialProps->pTransmutationProps->hTransmutatedItemDef);

	if (pItem && pItemDef)
	{
		int iCostume = 0;
		Entity* pEnt = entActivePlayerPtr();
		const char* pchHeadshotStyle = bOverrideHeadshotStyle ? item_GetHeadshotStyleDef(pItem) : NULL;
		for(iCostume=eaSize(&pItemDef->ppCostumes)-1; iCostume>=0; iCostume--)
		{
			S32 bValid = true;

			if (pItemDef->ppCostumes[iCostume]->pExprRequires)
			{
				MultiVal answer = {0};
				itemeval_Eval(PARTITION_CLIENT, pItemDef->ppCostumes[iCostume]->pExprRequires, pItemDef,  NULL, pItem, pEnt, pItemDef->iLevel, pItemDef->Quality, 0, pItemDef->pchFileName, -1, &answer);
				bValid = itemeval_GetIntResult(&answer,pItemDef->pchFileName,pItemDef->ppCostumes[iCostume]->pExprRequires);
			}

			//Even if we failed the requires expression, if this is the last possible costume we'll have to show it.
			if(bValid || iCostume == 0)
			{
				PlayerCostume* pCostume = GET_REF(pItemDef->ppCostumes[iCostume]->hCostumeRef);
				SpeciesDef* pSpecies = pCostume ? GET_REF(pCostume->hSpecies) : NULL;
 				PlayerCostume* pEntCostume = pEnt ? pEnt->costumeRef.pEffectiveCostume : NULL;
				if (!pEntCostume) 
					pEntCostume = pEnt->costumeRef.pStoredCostume;
				if (pCostume && pEntCostume && 
					(!gConf.bCheckOverlayCostumeSpecies || !pSpecies || !pEnt || !pEnt->pChar || (pSpecies == GET_REF(pEnt->pChar->hSpecies))) &&
					(REF_COMPARE_HANDLES(pCostume->hSkeleton, pEntCostume->hSkeleton) || pItemDef->eCostumeMode == kCostumeDisplayMode_Overlay_Always))
				{
					return gclPaperdoll_CreateHeadshotData(pContext, NULL, pCostume, NULL, NULL, pchHeadshotStyle, NULL);
				}
			}
		}
	}
	else
	{
		return gclPaperdoll_CreateHeadshotData(pContext, NULL, NULL, NULL, NULL, NULL, NULL);
	}
	return NULL;
}

// Gets a headshot from a PlayerCostume name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PaperdollGetHeadshotFromPlayerCostumeName);
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetHeadshotFromPlayerCostumeName(ExprContext* pContext,
																					 const char* pchPlayerCostume)
{
	if (pchPlayerCostume && pchPlayerCostume[0])
	{
		PlayerCostume* pPlayerCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchPlayerCostume);
		if (!pPlayerCostume)
		{
			ErrorFilenamef(exprContextGetBlameFile(pContext), 
						   "PaperdollGetHeadshotFromPlayerCostumeName: Couldn't find PlayerCostume '%s'", 
						   pchPlayerCostume);
		}
		return gclPaperdoll_CreateHeadshotData(pContext, NULL, pPlayerCostume, NULL, NULL, NULL, NULL);
	}
	return NULL;
}

// Gets a headshot from a WLCostume name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetHeadshotFromCostumeName");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetHeadshotFromCostumeName(ExprContext* pContext,
																			   const char* pchCostumeName)
{
	if (pchCostumeName && pchCostumeName[0])
	{
		WLCostume* pWLCostume = RefSystem_ReferentFromString("Costume", pchCostumeName);
		if (!pWLCostume)
		{
			ErrorFilenamef(exprContextGetBlameFile(pContext), 
						   "PaperdollGetHeadshotFromCostumeName: Couldn't find WLCostume '%s'", 
						   pchCostumeName);
		}
		return gclPaperdoll_CreateHeadshotData(pContext, pWLCostume, NULL, NULL, NULL, NULL, NULL);
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetHeadshotFromBuild");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetHeadshotFromBuild(ExprContext* pContext,
																		 SA_PARAM_OP_VALID Entity* pEnt,
																		 U32 uBuildIndex)
{
	EntityBuild *pBuild = pEnt ? entity_BuildGet(pEnt, uBuildIndex) : NULL;
	if (pBuild && pEnt->pSaved)
	{
		PlayerCostume *pPlayerCostume = NULL;
		
		// Get the player costume. 
		switch(pBuild->chCostumeType) 
		{
			xcase -1:
			{
				pPlayerCostume = costumeEntity_GetEffectiveCostume(pEnt);
			}
			xcase kPCCostumeStorageType_Primary:
			acase kPCCostumeStorageType_Secondary:
			{
				PlayerCostumeSlot* pSlot = eaGet(&pEnt->pSaved->costumeData.eaCostumeSlots, pBuild->chCostume);
				if (pSlot)
				{
					pPlayerCostume = pSlot->pCostume;
				}
			}
		}
		if (pPlayerCostume)
		{
			return gclPaperdoll_CreateHeadshotData(pContext, NULL, pPlayerCostume, NULL, NULL, NULL, NULL);
		}
	}
	return NULL;
}

static PaperdollHeadshotData* gclPaperdoll_GetHeadshotFromContactData(ExprContext* pContext,
																	  PlayerCostume* pHeadshotCostumeOverride,
																	  PlayerCostume* pHeadshotCostume,
																	  ContainerID uHeadshotPetID,
																	  Entity* pHeadshotEnt,
																	  const char* pchHeadshotStyle,
																	  AIAnimList* pAnimList)
{
	Entity* pEntity = entActivePlayerPtr();
	PlayerCostume* pPlayerCostume = NULL;
	WLCostume* pWLCostume = NULL;
	Entity* pCostumeEnt = NULL;
	PaperdollHeadshotData* pHeadshotData;

	if (pHeadshotCostumeOverride || pHeadshotCostume)
	{
		pPlayerCostume = FIRST_IF_SET(pHeadshotCostumeOverride, pHeadshotCostume);
	}
	else if (uHeadshotPetID)
	{
		PetRelationship* pPet = SavedPet_GetPetFromContainerID(pEntity, uHeadshotPetID, true);
		if (pPet)
		{
			pCostumeEnt = GET_REF(pPet->hPetRef);
		}
	}
	else if (pHeadshotEnt)
	{
		pWLCostume = GET_REF(pHeadshotEnt->hWLCostume);
	}

	pHeadshotData = gclPaperdoll_CreateHeadshotData(pContext, pWLCostume, pPlayerCostume, pCostumeEnt, NULL, pchHeadshotStyle, NULL);
	pHeadshotData->pAnimList = pAnimList;
	return pHeadshotData;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetHeadshotFromContactDialog");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetHeadshotFromContactDialog(ExprContext* pContext)
{
	Entity* pEntity = entActivePlayerPtr();
	ContactDialog* pContactDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);
	if (pContactDialog)
	{
		return gclPaperdoll_GetHeadshotFromContactData(pContext,
													   pContactDialog->pHeadshotOverride,
													   GET_REF(pContactDialog->hHeadshotOverride),
													   pContactDialog->iHeadshotOverridePetID,
													   entFromEntityRefAnyPartition(pContactDialog->headshotEnt),
													   pContactDialog->pchHeadshotStyleDef,
													   GET_REF(pContactDialog->hAnimListToPlayForActiveEntity));
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetHeadshotFromContactLog");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetHeadshotFromContactLog(ExprContext* pContext, SA_PARAM_OP_VALID ContactLogEntry *pLogEntry)
{
	if (pLogEntry)
	{
		return gclPaperdoll_GetHeadshotFromContactData(pContext,
													   pLogEntry->pHeadshotCostume,
													   GET_REF(pLogEntry->hHeadshotCostumeRef),
													   pLogEntry->iHeadshotPetID,
													   0,
													   pLogEntry->pchHeadshotStyleDef,
													   NULL);
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetHeadshotFromContactLogList");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetHeadshotFromContactLogList(ExprContext* pContext, SA_PARAM_OP_VALID UIGen *pGen, S32 iModelIndex)
{
	ParseTable *pTable;
	ContactLogEntry ***peaLogEntries = (ContactLogEntry ***)ui_GenGetList(pGen, NULL, &pTable);

	if (pTable == parse_ContactLogEntry)
		return gclExprPaperdollGetHeadshotFromContactLog(pContext, eaGet(peaLogEntries, iModelIndex));
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetHeadshotFromRemoteContact");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetHeadshotFromRemoteContact(ExprContext* pContext,
																				 const char* pchContact)
{
	RemoteContact* pContact = eaIndexedGetUsingString(&geaCachedRemoteContacts, pchContact);
	ContactHeadshotData* pHeadshot = SAFE_MEMBER(pContact, pHeadshot);
	
	if (pHeadshot)
	{
		return gclPaperdoll_GetHeadshotFromContactData(pContext,
													   pHeadshot->pCostume,
													   GET_REF(pHeadshot->hCostume),
													   pHeadshot->iPetID,
													   NULL,
													   pHeadshot->pchHeadshotStyleDef,
													   NULL);
	}
	else if (pContact && !pContact->bHeadshotRequested)
	{
		pContact->bHeadshotRequested = true;
		ServerCmd_RemoteContactOption_RequestHeadshot(pContact->pchContactDef);
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetHeadshotFromNotifyItem");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetHeadshotFromNotifyItem(ExprContext* pContext,
																			  SA_PARAM_OP_VALID NotifyQueueItem* pNotifyItem)
{
	if (pNotifyItem && pNotifyItem->pHeadshotData)
	{
		if (pNotifyItem->pHeadshotData->iPetID)
		{
			Entity *pEnt = entActivePlayerPtr();
			Entity *pPetEnt = NULL;

			if (pEnt)
			{
				PetRelationship *pPetRelationship = SavedPet_GetPetFromContainerID(pEnt, pNotifyItem->pHeadshotData->iPetID, true);
				if(pPetRelationship)
				{
					pPetEnt = GET_REF(pPetRelationship->hPetRef);
				}
			}

			if (pPetEnt)
			{
				return gclPaperdoll_CreateHeadshotData(pContext,
														NULL,
														NULL,
														pPetEnt,
														NULL,
														pNotifyItem->pHeadshotData->pchHeadshotStyleDef, 
														NULL);
			}
		}
		else if (pNotifyItem->pHeadshotData->pCostume)
		{
			return gclPaperdoll_CreateHeadshotData(pContext,
													NULL,
													pNotifyItem->pHeadshotData->pCostume,
													NULL,
													NULL,
													pNotifyItem->pHeadshotData->pchHeadshotStyleDef, 
													NULL);
		}
		else
		{
			return gclPaperdoll_CreateHeadshotData(pContext,
													NULL,
													GET_REF(pNotifyItem->pHeadshotData->hCostume),
													NULL,
													NULL,
													pNotifyItem->pHeadshotData->pchHeadshotStyleDef, 
													NULL);
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetHeadshotFromGuildUniform");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetHeadshotFromGuildUniform(ExprContext* pContext, S32 iUniform)
{
	Entity *pPlayer = entActivePlayerPtr();
	Guild *pGuild = guild_GetGuild(pPlayer);
	if (pGuild)
	{
		GuildCostume *pCostume = eaGet(&pGuild->eaUniforms, iUniform);
		if (pCostume && pCostume->pCostume)
		{
			return gclPaperdoll_CreateHeadshotData(pContext, NULL, pCostume->pCostume, NULL, NULL, NULL, NULL);
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetHeadshotFromSpecies");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetHeadshotFromSpecies(ExprContext* pContext, const char* pchSpecies)
{
	SpeciesDef *pSpecies = RefSystem_ReferentFromString(g_hSpeciesDict, pchSpecies);
	if (pSpecies && eaSize(&pSpecies->eaPresets) && GET_REF(pSpecies->eaPresets[0]->hCostume))
	{
		return gclPaperdoll_CreateHeadshotData(pContext, NULL, GET_REF(pSpecies->eaPresets[0]->hCostume), NULL, NULL, NULL, NULL);
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetHeadshotFromFile");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetHeadshotFromFile(ExprContext* pContext, const char* pchFilename)
{
	return gclPaperdoll_CreateHeadshotData(pContext, NULL, gclPaperdollLoadCostume(pchFilename), NULL, NULL, NULL, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetEmblemFromGuild");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetEmblemFromGuild(ExprContext* pContext, SA_PARAM_OP_VALID Entity *pEnt, const char *pchSkeleton, const char *pchEmblemBone)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	if (pGuild)
	{
		ContainerID iGuildID = pGuild->iContainerID;
		GuildEmblemData EmblemData = {0};
		EmblemData.pcEmblem = pGuild->pcEmblem;
		EmblemData.iEmblemColor0 = pGuild->iEmblemColor0;
		EmblemData.iEmblemColor1 = pGuild->iEmblemColor1;
		EmblemData.fEmblemRotation = pGuild->fEmblemRotation;
		EmblemData.pcEmblem2 = pGuild->pcEmblem2;
		EmblemData.iEmblem2Color0 = pGuild->iEmblem2Color0;
		EmblemData.iEmblem2Color1 = pGuild->iEmblem2Color1;
		EmblemData.fEmblem2Rotation = pGuild->fEmblem2Rotation;
		EmblemData.fEmblem2X = pGuild->fEmblem2X;
		EmblemData.fEmblem2Y = pGuild->fEmblem2Y;
		EmblemData.fEmblem2ScaleX = pGuild->fEmblem2ScaleX;
		EmblemData.fEmblem2ScaleY = pGuild->fEmblem2ScaleY;
		EmblemData.pcEmblem3 = pGuild->pcEmblem3;
		return gclPaperdoll_CreateHeadshotData(pContext, NULL, gclCreateGuildEmblemCostume(iGuildID, pchSkeleton, pchEmblemBone, &EmblemData, false), NULL, NULL, NULL, NULL);
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetEmblemFromGuildWithoutBackground");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetEmblemFromGuildWithoutBackground(ExprContext* pContext, SA_PARAM_OP_VALID Entity *pEnt, const char *pchSkeleton, const char *pchEmblemBone)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	if (pGuild)
	{
		ContainerID iGuildID = pGuild->iContainerID;
		GuildEmblemData EmblemData = {0};
		EmblemData.pcEmblem = pGuild->pcEmblem;
		EmblemData.iEmblemColor0 = pGuild->iEmblemColor0;
		EmblemData.iEmblemColor1 = pGuild->iEmblemColor1;
		EmblemData.fEmblemRotation = pGuild->fEmblemRotation;
		EmblemData.pcEmblem2 = pGuild->pcEmblem2;
		EmblemData.iEmblem2Color0 = pGuild->iEmblem2Color0;
		EmblemData.iEmblem2Color1 = pGuild->iEmblem2Color1;
		EmblemData.fEmblem2Rotation = pGuild->fEmblem2Rotation;
		EmblemData.fEmblem2X = pGuild->fEmblem2X;
		EmblemData.fEmblem2Y = pGuild->fEmblem2Y;
		EmblemData.fEmblem2ScaleX = pGuild->fEmblem2ScaleX;
		EmblemData.fEmblem2ScaleY = pGuild->fEmblem2ScaleY;
		EmblemData.pcEmblem3 = pGuild->pcEmblem3;
		return gclPaperdoll_CreateHeadshotData(pContext, NULL, gclCreateGuildEmblemCostume(iGuildID, pchSkeleton, pchEmblemBone, &EmblemData, true), NULL, NULL, NULL, NULL);
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollGetEmblemFromGuildRecruitData");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollGetEmblemFromGuildRecruitData(ExprContext* pContext, SA_PARAM_OP_VALID GuildRecruitData *pRecruitData, const char *pchSkeleton, const char *pchEmblemBone)
{
	if (pRecruitData)
	{
		ContainerID iGuildID = pRecruitData->iContainerID;
		GuildEmblemData EmblemData = {0};
		EmblemData.pcEmblem = pRecruitData->pcEmblem;
		EmblemData.iEmblemColor0 = pRecruitData->iEmblemColor0;
		EmblemData.iEmblemColor1 = pRecruitData->iEmblemColor1;
		EmblemData.fEmblemRotation = pRecruitData->fEmblemRotation;
		EmblemData.pcEmblem2 = pRecruitData->pcEmblem2;
		EmblemData.iEmblem2Color0 = pRecruitData->iEmblem2Color0;
		EmblemData.iEmblem2Color1 = pRecruitData->iEmblem2Color1;
		EmblemData.fEmblem2Rotation = pRecruitData->fEmblem2Rotation;
		EmblemData.fEmblem2X = pRecruitData->fEmblem2X;
		EmblemData.fEmblem2Y = pRecruitData->fEmblem2Y;
		EmblemData.fEmblem2ScaleX = pRecruitData->fEmblem2ScaleX;
		EmblemData.fEmblem2ScaleY = pRecruitData->fEmblem2ScaleY;
		EmblemData.pcEmblem3 = pRecruitData->pcEmblem3;
		return gclPaperdoll_CreateHeadshotData(pContext, NULL, gclCreateGuildEmblemCostume(iGuildID, pchSkeleton, pchEmblemBone, &EmblemData, false), NULL, NULL, NULL, NULL);
	}
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// Expression functions that have to create new costumes.

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollOverlayCostumeFromGuildUniform");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollOverlayCostumeFromGuildUniform(ExprContext* pContext,
																				   SA_PARAM_OP_VALID PaperdollHeadshotData* pBase,
																				   S32 iUniform,
																				   const char* pchBoneGroup,
																				   const char* pchSlotType)
{
	Entity *pPlayer = entActivePlayerPtr();
	Guild *pGuild = guild_GetGuild(pPlayer);
	if (pGuild && pBase)
	{
		GuildCostume *pCostume = eaGet(&pGuild->eaUniforms, iUniform);
		if (pCostume && pCostume->pCostume)
		{
			return gclPaperdollGenOverlay(pContext, pBase, pCostume->pCostume, pchBoneGroup, pchSlotType, pGuild, NULL);
		}
	}
	return pBase;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollOverlayCostumeFromEntity");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollOverlayCostumeFromEntity(ExprContext* pContext,
																			 SA_PARAM_OP_VALID PaperdollHeadshotData* pBase,
																			 SA_PARAM_OP_VALID Entity* pEnt,
																			 S32 iSlot,
																			 const char* pchBoneGroup,
																			 const char* pchSlotType)
{
	Entity *pPlayer = entActivePlayerPtr();
	Guild *pGuild = guild_GetGuild(pPlayer);
	if (pGuild && pBase && pPlayer->pSaved)
	{
		PlayerCostumeSlot *pSlot = eaGet(&pPlayer->pSaved->costumeData.eaCostumeSlots, iSlot);
		if (pSlot && pSlot->pCostume)
		{
			return gclPaperdollGenOverlay(pContext, pBase, pSlot->pCostume, pchBoneGroup, pchSlotType, pGuild, NULL);
		}
	}
	return pBase;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollOverlayCostumeFromFile");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollOverlayCostumeFromFile(ExprContext* pContext,
																		   SA_PARAM_OP_VALID PaperdollHeadshotData* pBase,
																		   const char* pchFilename,
																		   const char* pchBoneGroup,
																		   const char* pchSlotType)
{
	Entity *pPlayer = entActivePlayerPtr();
	Guild *pGuild = guild_GetGuild(pPlayer);
	PlayerCostume *pOverlay = gclPaperdollLoadCostume(pchFilename);
	if (pOverlay)
	{
		return gclPaperdollGenOverlay(pContext, pBase, pOverlay, pchBoneGroup, pchSlotType, pGuild, NULL);
	}
	return pBase;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollOverlayCostumeFromUnlockedCostumePart");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollOverlayCostumeFromUnlockedCostumePart(ExprContext* pContext,
																						  SA_PARAM_OP_VALID PaperdollHeadshotData* pBase,
																						  SA_PARAM_OP_VALID UnlockedCostumePart* pUnlockedPart,
																						  const char* pchBoneGroup,
																						  const char* pchSlotType,
																						  int uiColor0,
																						  int uiColor1,
																						  int uiColor2,
																						  int uiColor3)
{
	static void *vpPrevUnlockedPart;
	static NOCONST(PlayerCostume) Costume;
	PCPart *pPCBasePart = NULL; // Used for deciding the color of the new part to display. 
	PCPart *pPCAllColorPart = NULL; // If there is no analogous part, then use the first one that has an 'All' linked color
	Entity *pPlayer = entActivePlayerPtr();
	Guild *pGuild = guild_GetGuild(pPlayer);
	NOCONST(PCPart) **eaTempParts = NULL;
	NOCONST(PCPart) **eaSrcParts = NULL;
	NOCONST(PlayerCostume) *pSrcCostume = pUnlockedPart ? (NOCONST(PlayerCostume) *) GET_REF(pUnlockedPart->hSoftCostume) : NULL;
	S32 i;
	
	vpPrevUnlockedPart = pUnlockedPart;
	
	if (!pBase || !pBase->pPlayerCostume || !pSrcCostume)
		return pBase;

	eaStackCreate(&eaTempParts, 1);

	for(i=eaSize(&pSrcCostume->eaParts)-1; i>=0; --i) 
	{
		NOCONST(PCPart) *pPCPart = pSrcCostume->eaParts[i];
		if (REF_HANDLE_COMPARE(pUnlockedPart->hBone, pPCPart->hBoneDef)
			&& REF_HANDLE_COMPARE(pUnlockedPart->hUnlockedGeometry, pPCPart->hGeoDef)) 
		{
			if (REF_HANDLE_COMPARE(pUnlockedPart->hUnlockedMaterial, pPCPart->hMatDef))
			{
				bool bMatched = !(pUnlockedPart->bTexPatternUnlock || pUnlockedPart->bTexDetailUnlock || pUnlockedPart->bTexSpecularUnlock || pUnlockedPart->bTexDiffuseUnlock);
				if (!bMatched && pUnlockedPart->bTexPatternUnlock)
					bMatched = REF_HANDLE_COMPARE(pUnlockedPart->hUnlockedPatternTexture, pPCPart->hPatternTexture);
				if (!bMatched && pUnlockedPart->bTexDetailUnlock)
					bMatched = REF_HANDLE_COMPARE(pUnlockedPart->hUnlockedDetailTexture, pPCPart->hDetailTexture);
				if (!bMatched && pUnlockedPart->bTexSpecularUnlock)
					bMatched = REF_HANDLE_COMPARE(pUnlockedPart->hUnlockedSpecularTexture, pPCPart->hSpecularTexture);
				if (!bMatched && pUnlockedPart->bTexDiffuseUnlock)
					bMatched = REF_HANDLE_COMPARE(pUnlockedPart->hUnlockedDiffuseTexture, pPCPart->hDiffuseTexture);

				if (bMatched)
				{
					PCBoneDef *pBone = GET_REF(pPCPart->hBoneDef);
					eaPushUnique(&eaTempParts, pPCPart);
					break;
				}
			}
		}
	}

	eaSrcParts = pSrcCostume->eaParts;
	pSrcCostume->eaParts = eaTempParts;

	StructCopyAllNoConst(parse_PlayerCostume, pSrcCostume, &Costume);

	pSrcCostume->eaParts = eaSrcParts;
	eaSrcParts = NULL;

	// Determine color
	if (SAFE_MEMBER(pBase, pPlayerCostume) && eaSize(&eaTempParts))
	{
		for (i = 0; i < eaSize(&pBase->pPlayerCostume->eaParts); i++)
		{
			PCPart *pPCPart = pBase->pPlayerCostume->eaParts[i];
			if (!pPCAllColorPart && pPCPart->eColorLink == kPCColorLink_All)
				pPCAllColorPart = pPCPart;
			if (REF_HANDLE_COMPARE(eaTempParts[0]->hBoneDef, pPCPart->hBoneDef))
			{
				pPCBasePart = pPCPart;
				break;
			}
		}
	}

	if (!pPCBasePart)
		pPCBasePart = pPCAllColorPart;

	if (pPCBasePart)
	{
		memcpy(Costume.eaParts[0]->color0, pPCBasePart->color0, 4);
		memcpy(Costume.eaParts[0]->color1, pPCBasePart->color1, 4);
		memcpy(Costume.eaParts[0]->color2, pPCBasePart->color2, 4);
		memcpy(Costume.eaParts[0]->color3, pPCBasePart->color3, 4);
	}
	else
	{
		setU8FromRGBA(Costume.eaParts[0]->color0, uiColor0);
		setU8FromRGBA(Costume.eaParts[0]->color1, uiColor1);
		setU8FromRGBA(Costume.eaParts[0]->color2, uiColor2);
		setU8FromRGBA(Costume.eaParts[0]->color3, uiColor3);
	}

	return gclPaperdollGenOverlay(pContext, pBase, CONTAINER_RECONST(PlayerCostume, &Costume), pchBoneGroup, pchSlotType, pGuild, pUnlockedPart);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollOverlayCostumeFromMicroTrans");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollOverlayCostumeFromMicroTrans(ExprContext* pContext,
																				 SA_PARAM_OP_VALID PaperdollHeadshotData* pBase,
																				 U32 uMicroTransID,
																				 const char* pchBoneGroup,
																				 const char* pchSlotType)
{
	static MicroTransactionCostume **s_eaCostumes = NULL;
	Entity *pPlayer = entActivePlayerPtr();
	Guild *pGuild = guild_GetGuild(pPlayer);
	S32 i, j;

	eaClearFast(&s_eaCostumes);

	if (!pBase || !pBase->pPlayerCostume)
		return pBase;

	for (i = eaSize(&g_pMTCostumes->ppCostumes) - 1; i >= 0; i--)
	{
		MicroTransactionCostume *pMTCostume = g_pMTCostumes->ppCostumes[i];
		for (j = eaSize(&pMTCostume->eaSources) - 1; j >= 0; j--)
		{
			MicroTransactionCostumeSource* pSource = pMTCostume->eaSources[j];
			if (pSource->uID == uMicroTransID)
			{
				eaPush(&s_eaCostumes, pMTCostume);
				break;
			}
		}
	}

	if (!eaSize(&s_eaCostumes))
		return pBase;

	// FIXME(jm): Apply all costumes.
	for (i = 0; i < eaSize(&s_eaCostumes); i++)
	{
		PlayerCostume *pCostume = GET_REF(s_eaCostumes[i]->hCostume);
		if (pCostume && REF_COMPARE_HANDLES(pBase->pPlayerCostume->hSkeleton, pCostume->hSkeleton))
		{
			return gclPaperdollGenOverlay(pContext, pBase, pCostume, pchBoneGroup, pchSlotType, pGuild, NULL);
		}
	}

	return pBase;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollOverlayCostumeFromMicroTransByName");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollOverlayCostumeFromMicroTransByName(ExprContext* pContext,
																					   SA_PARAM_OP_VALID PaperdollHeadshotData* pBase,
																					   const char* pchName,
																					   const char* pchBoneGroup,
																					   const char* pchSlotType)
{
	return gclExprPaperdollOverlayCostumeFromMicroTrans(pContext, pBase, gclMicroTrans_expr_GetID(pchName), pchBoneGroup, pchSlotType);
}

static PlayerCostume *gclPaperdollGetItemDefOverlay(SA_PARAM_NN_VALID ItemDef *pItemDef,
													const char *pchCostume,
													SA_PARAM_NN_VALID PCSkeletonDef *pSkeleton,
													SA_PARAM_OP_VALID SpeciesDef *pSpecies)
{
	PlayerCostume *pOverlayCostume = NULL;
	S32 i;

	if (pchCostume && *pchCostume)
	{
		for (i = 0; i < eaSize(&pItemDef->ppCostumes); i++)
		{
			const char *pchCostumeName = REF_STRING_FROM_HANDLE(pItemDef->ppCostumes[i]->hCostumeRef);
			if (pchCostumeName && !stricmp(pchCostumeName, pchCostume))
			{
				pOverlayCostume = GET_REF(pItemDef->ppCostumes[i]->hCostumeRef);
				break;
			}
		}
	}

	// Treat NULL overlay species as wildcard
	if (!pOverlayCostume
		|| GET_REF(pOverlayCostume->hSkeleton) != pSkeleton
		|| (IS_HANDLE_ACTIVE(pOverlayCostume->hSpecies) && pSpecies && GET_REF(pOverlayCostume->hSpecies) != pSpecies))
	{
		for (i = 0; i < eaSize(&pItemDef->ppCostumes); i++)
		{
			PlayerCostume *pCostume = GET_REF(pItemDef->ppCostumes[i]->hCostumeRef);
			if (pCostume && GET_REF(pCostume->hSkeleton) == pSkeleton
				&& (!IS_HANDLE_ACTIVE(pCostume->hSpecies) || !pSpecies || GET_REF(pCostume->hSpecies) != pSpecies))
			{
				if (!pOverlayCostume || pchCostume && *pchCostume && strstri(pCostume->pcName, pchCostume))
					pOverlayCostume = pCostume;
			}
		}
	}

	return pOverlayCostume;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollOverlayCostumeFromItemDef");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollOverlayCostumeFromItemDef(ExprContext* pContext,
																			  SA_PARAM_OP_VALID PaperdollHeadshotData* pBase,
																			  SA_PARAM_OP_VALID ItemDef* pItemDef,
																			  const char* pchCostume,
																			  const char* pchBoneGroup,
																			  const char* pchSlotType)
{
	Entity *pPlayer = entActivePlayerPtr();
	Guild *pGuild = guild_GetGuild(pPlayer);
	PlayerCostume *pBaseCostume = NULL;
	PCMood *pMood = NULL;
	PlayerCostume *pOverlayCostume = NULL;
	PCSkeletonDef *pSkeleton;
	SpeciesDef *pSpecies;

	if (!pItemDef || !pBase || !pBase->pPlayerCostume
		|| !eaSize(&pItemDef->ppCostumes))
	{
		return pBase;
	}

	pSkeleton = GET_REF(pBase->pPlayerCostume->hSkeleton);
	pSpecies = GET_REF(pBase->pPlayerCostume->hSpecies);
	if (!pSkeleton || (!pSpecies && IS_HANDLE_ACTIVE(pBase->pPlayerCostume->hSpecies)))
		return pBase;

	pOverlayCostume = gclPaperdollGetItemDefOverlay(pItemDef, pchCostume, pSkeleton, pSpecies);
	if (!pOverlayCostume)
		// Waiting for a reference
		return pBase;

	return gclPaperdollGenOverlay(pContext, pBase, pOverlayCostume, pchBoneGroup, pchSlotType, pGuild, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollOverlayCostumeFromItem");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollOverlayCostumeFromItem(ExprContext* pContext,
																		   SA_PARAM_OP_VALID PaperdollHeadshotData* pBase,
																		   SA_PARAM_OP_VALID Item* pItem,
																		   const char* pchCostume,
																		   const char* pchBoneGroup,
																		   const char* pchSlotType)
{
	return gclExprPaperdollOverlayCostumeFromItemDef(pContext,
													 pBase,
													 pItem ? GET_REF(pItem->hItem) : NULL,
													 pchCostume,
													 pchBoneGroup,
													 pchSlotType);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollForceGuildUniform");
SA_RET_OP_VALID PaperdollHeadshotData* gclExprPaperdollForceGuildUniform(ExprContext* pContext,
																	  SA_PARAM_OP_VALID PaperdollHeadshotData* pBase,
																	  const char* pchSlotType)
{
	if (pBase && pBase->pPlayerCostume)
	{
		Entity *pPlayer = entActivePlayerPtr();
		Guild *pGuild = guild_GetGuild(pPlayer);
		PCSlotType *pSlotType = NULL;
		PaperdollCostumeGenCache* pCache;

		// Check cache
		FOR_EACH_IN_EARRAY(s_eaCache, PaperdollCostumeGenCache, pCostume)
		{
			if (pCostume->pSource1 == pBase->pPlayerCostume && pCostume->pSource2 == NULL)
			{
				return CacheUse(pBase, pCostume);
			}
		}
		FOR_EACH_END;

		if (pchSlotType && *pchSlotType)
			pSlotType = costumeLoad_GetSlotType(pchSlotType);

		// Need to make a new costume
		pCache = StructCreate(parse_PaperdollCostumeGenCache);
		pCache->pSource1 = pBase->pPlayerCostume;
		pCache->pResult = StructClone(parse_PlayerCostume, pBase->pPlayerCostume);
		eaPush(&s_eaCache, pCache);

		costumeTailor_MakeCostumeValid(CONTAINER_NOCONST(PlayerCostume, pCache->pResult),
									   GET_REF(pCache->pResult->hSpecies),
									   NULL,
									   pSlotType,
									   false,
									   true,
									   false,
									   pGuild,
									   false,
									   NULL,
									   false,
									   NULL);

		return CacheUse(pBase, pCache);
	}
	return pBase;
}

static S32 s_iTailorCRC;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollTailorCostumeRefresh");
void gclPaperdollTailorCostumeRefresh(ExprContext* pContext)
{
	s_iTailorCRC++;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollTailorCostume");
SA_RET_OP_VALID PaperdollHeadshotData* gclPaperdollTailorCostume(ExprContext* pContext, bool bHover)
{
	PlayerCostume *pTailorCostume = bHover ? g_CostumeEditState.pConstHoverCostume  : g_CostumeEditState.pConstCostume;
	if (pTailorCostume)
	{
		PaperdollCostumeGenCache* pCache;
		S32 iCRC = s_iTailorCRC;

		// Check cache
		FOR_EACH_IN_EARRAY(s_eaCache, PaperdollCostumeGenCache, pCostume)
		{
			if (pCostume->pSource1 == pTailorCostume && pCostume->iKeyInt == iCRC)
			{
				return CacheNewUse(pContext, pCostume);
			}
		}
		FOR_EACH_END;

		// Need to make a new costume
		pCache = StructCreate(parse_PaperdollCostumeGenCache);
		pCache->pSource1 = pTailorCostume;
		pCache->iKeyInt = iCRC;
		pCache->pResult = StructClone(parse_PlayerCostume, pTailorCostume);
		eaPush(&s_eaCache, pCache);

		return CacheNewUse(pContext, pCache);
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollOverlayFromCostumeOptionOrBoneScale");
SA_RET_OP_VALID PaperdollHeadshotData* gclPaperdollOverlayFromCostumeOptionOrBoneScale(ExprContext* pContext,
																 SA_PARAM_OP_VALID PaperdollHeadshotData* pBase,
																 SA_PARAM_OP_VALID CostumeSubListRow *pCostumeSubListRow,
																 const char* pcScaleName, F32 fBodyScale)
{
	char *pSelf;
	pcScaleName = allocAddString(pcScaleName);
	*(void (**)(void))&pSelf = (void (*)(void))gclPaperdollOverlayFromCostumeOptionOrBoneScale;
	if (pBase && (pCostumeSubListRow || pcScaleName))
	{
		PaperdollCostumeGenCache* pCache;
		void *pvKey = pCostumeSubListRow ? pCostumeSubListRow->pcName : (void *)(pSelf + 1);
		void *pvKey2 = pCostumeSubListRow && pCostumeSubListRow->pLine ? REF_STRING_FROM_HANDLE(pCostumeSubListRow->pLine->displayNameMsg.hMessage) : (void *)(pSelf + 2);
		void *pvKey3 = pcScaleName ? (void *)pcScaleName : (void *)(pSelf + 3);

		// Check cache
		FOR_EACH_IN_EARRAY(s_eaCache, PaperdollCostumeGenCache, pCostume)
		{
			if (pCostume->pSource1 == pBase->pPlayerCostume
				&& pCostume->pvKey == pvKey
				&& pCostume->pvKey2 == pvKey2
				&& pCostume->pvKey3 == pvKey3
				&& pCostume->fKeyFloat == fBodyScale)
			{
				return CacheUse(pBase, pCostume);
			}
		}
		FOR_EACH_END;

		// Need to make a new costume
		pCache = StructCreate(parse_PaperdollCostumeGenCache);
		pCache->pSource1 = pBase->pPlayerCostume;
		pCache->pvKey = pvKey;
		pCache->pvKey2 = pvKey2;
		pCache->pvKey3 = pvKey3;
		pCache->fKeyFloat = fBodyScale;
		pCache->pResult = StructClone(parse_PlayerCostume, pBase->pPlayerCostume);
		eaPush(&s_eaCache, pCache);

		if( pcScaleName  )
		{
			costumeTailor_SetBodyScaleByName(CONTAINER_NOCONST(PlayerCostume, pCache->pResult), GET_REF(g_CostumeEditState.hSpecies), pcScaleName, fBodyScale, g_CostumeEditState.pSlotType);
			// TODO Something isn't actually applying these scales separately to each headshot. It looks like they're all getting the same set of scales.
		}

		if( pCostumeSubListRow )
		{
			costumeLineUI_SetLineItemInternal(CONTAINER_NOCONST(PlayerCostume, pCache->pResult), GET_REF(g_CostumeEditState.hSpecies), pCostumeSubListRow->pcName, pCostumeSubListRow->pLine, pCostumeSubListRow->iType, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, NULL, NULL, g_CostumeEditState.bUnlockAll, false, false);
		}

		return CacheUse(pBase, pCache);
	}
	return pBase;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollOverlayFromCostumeOption");
SA_RET_OP_VALID PaperdollHeadshotData* gclPaperdollOverlayFromCostumeOption(ExprContext* pContext,
																 SA_PARAM_OP_VALID PaperdollHeadshotData* pBase,
																 SA_PARAM_OP_VALID CostumeSubListRow *pCostumeSubListRow)
{
	return gclPaperdollOverlayFromCostumeOptionOrBoneScale(pContext, pBase, pCostumeSubListRow, NULL, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollOverlayFromBoneScale");
SA_RET_OP_VALID PaperdollHeadshotData* gclPaperdollOverlayFromBoneScale(ExprContext* pContext,
																 SA_PARAM_OP_VALID PaperdollHeadshotData* pBase,
																 const char* pcScaleName, F32 fBodyScale)
{
	return gclPaperdollOverlayFromCostumeOptionOrBoneScale(pContext, pBase, NULL, pcScaleName, fBodyScale);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollOverlayCostumePreset");
SA_RET_OP_VALID PaperdollHeadshotData* gclPaperdollOverlayCostumePreset(ExprContext* pContext,
																		SA_PARAM_OP_VALID PaperdollHeadshotData* pBase,
																		SA_PARAM_OP_VALID CostumePreset* pPreset,
																		bool bIgnoreHeight)
{
	if (pBase && pPreset)
	{
		PaperdollCostumeGenCache* pCache;

		// Check cache
		FOR_EACH_IN_EARRAY(s_eaCache, PaperdollCostumeGenCache, pCostume)
		{
			if (pCostume->pSource1 == pBase->pPlayerCostume
				&& pCostume->pvKey == pPreset
				&& pCostume->iKeyInt == bIgnoreHeight)
			{
				return CacheUse(pBase, pCostume);
			}
		}
		FOR_EACH_END;

		// Need to make a new costume
		pCache = StructCreate(parse_PaperdollCostumeGenCache);
		pCache->pSource1 = pBase->pPlayerCostume;
		pCache->pvKey = pPreset;
		pCache->iKeyInt = bIgnoreHeight;
		pCache->pResult = StructClone(parse_PlayerCostume, pBase->pPlayerCostume);
		eaPush(&s_eaCache, pCache);

		costumeTailor_FillAllBones(CONTAINER_NOCONST(PlayerCostume, pCache->pResult), GET_REF(pCache->pResult->hSpecies), NULL, NULL, true, false, true);
		CostumeCreator_ApplyPresetOverlay(CONTAINER_NOCONST(PlayerCostume, pCache->pResult), pPreset, bIgnoreHeight);
		return CacheUse(pBase, pCache);
	}
	return pBase;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PaperdollOverlayGeometryDef");
SA_RET_OP_VALID PaperdollHeadshotData* gclPaperdollOverlayGeometryDef(ExprContext* pContext,
																	  SA_PARAM_OP_VALID PaperdollHeadshotData* pBase,
																	  const char* pcBoneDef,
																	  const char* pcGeometryDef)
{
	PCBoneDef *pBoneDef = RefSystem_ReferentFromString(g_hCostumeBoneDict, pcBoneDef);
	PCGeometryDef *pGeometryDef = RefSystem_ReferentFromString(g_hCostumeGeometryDict, pcGeometryDef);
	if (pBase && pBoneDef && pGeometryDef && GET_REF(pGeometryDef->hBone) == pBoneDef)
	{
		PaperdollCostumeGenCache* pCache;
		NOCONST(PCPart) *pPart;

		// Check cache
		FOR_EACH_IN_EARRAY(s_eaCache, PaperdollCostumeGenCache, pCostume)
		{
			if (pCostume->pSource1 == pBase->pPlayerCostume
				&& pCostume->pvKey == pBoneDef
				&& pCostume->pvKey2 == pGeometryDef)
			{
				return CacheUse(pBase, pCostume);
			}
		}
		FOR_EACH_END;

		// Need to make a new costume
		pCache = StructCreate(parse_PaperdollCostumeGenCache);
		pCache->pSource1 = pBase->pPlayerCostume;
		pCache->pvKey = pBoneDef;
		pCache->pvKey2 = pGeometryDef;
		pCache->pResult = StructClone(parse_PlayerCostume, pBase->pPlayerCostume);
		eaPush(&s_eaCache, pCache);

		costumeTailor_FillAllBones(CONTAINER_NOCONST(PlayerCostume, pCache->pResult), GET_REF(pCache->pResult->hSpecies), NULL, NULL, true, false, true);
		pPart = costumeTailor_GetPartByBone(CONTAINER_NOCONST(PlayerCostume, pCache->pResult), pBoneDef, NULL);
		if (pPart)
		{
			SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, pGeometryDef, pPart->hGeoDef);
			costumeTailor_PickValidPartValues(CONTAINER_NOCONST(PlayerCostume, pCache->pResult),
											pPart,
											GET_REF(pCache->pResult->hSpecies),
											NULL,
											NULL,
											true,
											true, // no unlock checking
											true,
											false,
											NULL);
		}

		return CacheUse(pBase, pCache);
	}
	return pBase;
}

#include "AutoGen/gclUIGenPaperdollExpr_c_ast.c"
