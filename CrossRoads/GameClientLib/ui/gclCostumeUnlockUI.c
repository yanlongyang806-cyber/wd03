#include "gclCostumeUnlockUI.h"
#include "gclCostumeUIState.h"
#include "gclCostumeUI.h"
#include "earray.h"
#include "gclEntity.h"
#include "GameAccountDataCommon.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonTailor.h"
#include "CostumeCommonLoad.h"
#include "MicroTransactions.h"
#include "gclMicroTransactions.h"
#include "MicroTransactionUI.h"
#include "GameStringFormat.h"
#include "UIGen.h"
#include "Expression.h"
#include "Guild.h"
#include "Player.h"
#include "StringCache.h"
#include "GameClientLib.h"

#include "EntitySavedData_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define IS_UNLOCK_ONLY(eRestriction)		(((eRestriction) & (kPCRestriction_Player)) && !((eRestriction) & (kPCRestriction_Player_Initial)))

static const char *s_pchUnlockedMaterialMessage;
static const char *s_pchUnlockedPatternMessage;
static const char *s_pchUnlockedDetailMessage;
static const char *s_pchUnlockedSpecularMessage;
static const char *s_pchUnlockedDiffuseMessage;
static const char *s_pchUnlockedMovableMessage;

static void CostumeUI_AddUnlockedPart(NOCONST(PlayerCostume) *pValidateCostume, NOCONST(PlayerCostume) *pValidateCostumeUnlockAll, NOCONST(PlayerCostume) *pValidateCostumeUnlockCMat, NOCONST(PlayerCostume) *pValidateCostumeUnlockCTex, UnlockMetaData *pUnlock) {
	int i, j;
	bool bAdd;

	if (!pValidateCostume || !pUnlock)
		return;

	for (i = 0; i < eaSize(&pUnlock->eaCostumes); i++) {
		PlayerCostume *pCostume = GET_REF(pUnlock->eaCostumes[i]->hCostume);
		bAdd = false;
		for (j = 0; j < eaSize(&pCostume->eaParts); j++) {
			NOCONST(PCPart) *pPart = CONTAINER_NOCONST(PCPart, pCostume->eaParts[j]);

			// Is the part already on the costume?
			if (eaFind(&pValidateCostume->eaParts, pPart) >= 0) {
				continue;
			}

			// Check to see if this part unlocks pUnlock
			if (IS_HANDLE_ACTIVE(pUnlock->hGeometry)) {
				if (!REF_COMPARE_HANDLES(pPart->hGeoDef, pUnlock->hGeometry)) {
					continue;
				}
			} else if (IS_HANDLE_ACTIVE(pUnlock->hMaterial)) {
				if (!REF_COMPARE_HANDLES(pPart->hMatDef, pUnlock->hMaterial)) {
					continue;
				}
			} else if (IS_HANDLE_ACTIVE(pUnlock->hTexture)) {
				if (!REF_COMPARE_HANDLES(pPart->hPatternTexture, pUnlock->hTexture) &&
					!REF_COMPARE_HANDLES(pPart->hDetailTexture, pUnlock->hTexture) &&
					!REF_COMPARE_HANDLES(pPart->hSpecularTexture, pUnlock->hTexture) &&
					!REF_COMPARE_HANDLES(pPart->hDiffuseTexture, pUnlock->hTexture) &&
					(!pPart->pMovableTexture || !REF_COMPARE_HANDLES(pPart->pMovableTexture->hMovableTexture, pUnlock->hTexture))) {
					continue;
				}
			} else {
				continue;
			}

			// It does, so this costume has possible unlocks
			bAdd = true;
			break;
		}
		if (bAdd) {
			// Need to add the entire costume
			if (SAFE_MEMBER2(pCostume, pArtistData, bUnlockAll)) {
				for (j = 0; j < eaSize(&pCostume->eaParts); j++) {
					eaPushUnique(&pValidateCostumeUnlockAll->eaParts, CONTAINER_NOCONST(PCPart, pCostume->eaParts[j]));
				}
			} else if (SAFE_MEMBER2(pCostume, pArtistData, bUnlockAllCMat)) {
				for (j = 0; j < eaSize(&pCostume->eaParts); j++) {
					eaPushUnique(&pValidateCostumeUnlockCMat->eaParts, CONTAINER_NOCONST(PCPart, pCostume->eaParts[j]));
				}
			} else if (SAFE_MEMBER2(pCostume, pArtistData, bUnlockAllCTex)) {
				for (j = 0; j < eaSize(&pCostume->eaParts); j++) {
					eaPushUnique(&pValidateCostumeUnlockCTex->eaParts, CONTAINER_NOCONST(PCPart, pCostume->eaParts[j]));
				}
			} else {
				for (j = 0; j < eaSize(&pCostume->eaParts); j++) {
					eaPushUnique(&pValidateCostume->eaParts, CONTAINER_NOCONST(PCPart, pCostume->eaParts[j]));
				}
			}
		}
	}
}

void CostumeUI_GetValidCostumeUnlocks(NOCONST(PlayerCostume) *pValidateCostume, NOCONST(PlayerCostume) *pValidateCostumeUnlockAll, NOCONST(PlayerCostume) *pValidateCostumeUnlockCMat, NOCONST(PlayerCostume) *pValidateCostumeUnlockCTex, NOCONST(PlayerCostume) *pCostume)
{
	int i;

	if (!pValidateCostume
		|| !pValidateCostumeUnlockAll
		|| !pValidateCostumeUnlockCMat
		|| !pValidateCostumeUnlockCTex)
		return;

	eaClearFast(&pValidateCostume->eaParts);
	eaClearFast(&pValidateCostumeUnlockAll->eaParts);
	eaClearFast(&pValidateCostumeUnlockCMat->eaParts);
	eaClearFast(&pValidateCostumeUnlockCTex->eaParts);


#define COSTUME_SET_UNLOCK_FLAGS(pCostume, all, cmat, ctex) \
	do { \
		if (!(pCostume)->pArtistData) \
			(pCostume)->pArtistData = StructCreateNoConst(parse_PCArtistCostumeData); \
		(pCostume)->pArtistData->bUnlockAll = (all); \
		(pCostume)->pArtistData->bUnlockAllCMat = (cmat); \
		(pCostume)->pArtistData->bUnlockAllCTex = (ctex); \
	} while(0)

	COSTUME_SET_UNLOCK_FLAGS(pValidateCostume, false, false, false);
	COSTUME_SET_UNLOCK_FLAGS(pValidateCostumeUnlockAll, true, false, false);
	COSTUME_SET_UNLOCK_FLAGS(pValidateCostumeUnlockCMat, false, true, false);
	COSTUME_SET_UNLOCK_FLAGS(pValidateCostumeUnlockCTex, false, false, true);

#undef COSTUME_SET_UNLOCK_FLAGS

	if (!pCostume)
		return;
	
	for (i = 0; i < eaSize(&pCostume->eaParts); i++) {
		NOCONST(PCPart) *pPart = pCostume->eaParts[i];
		PCGeometryDef *pGeo = GET_REF(pPart->hGeoDef);
		PCMaterialDef *pMat = GET_REF(pPart->hMatDef);
		PCTextureDef *pPattern = GET_REF(pPart->hPatternTexture);
		PCTextureDef *pDetail = GET_REF(pPart->hDetailTexture);
		PCTextureDef *pSpecular = GET_REF(pPart->hSpecularTexture);
		PCTextureDef *pDiffuse = GET_REF(pPart->hDiffuseTexture);
		PCTextureDef *pMovable = pPart->pMovableTexture ? GET_REF(pPart->pMovableTexture->hMovableTexture) : NULL;
		UnlockMetaData *pUnlock;

		if (pMovable && IS_UNLOCK_ONLY(pMovable->eRestriction)) {
			if (stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, pMovable->pcName, &pUnlock)) {
				CostumeUI_AddUnlockedPart(pValidateCostume, pValidateCostumeUnlockAll, pValidateCostumeUnlockCMat, pValidateCostumeUnlockCTex, pUnlock);
			}
		}
		if (pDiffuse && IS_UNLOCK_ONLY(pDiffuse->eRestriction)) {
			if (stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, pDiffuse->pcName, &pUnlock)) {
				CostumeUI_AddUnlockedPart(pValidateCostume, pValidateCostumeUnlockAll, pValidateCostumeUnlockCMat, pValidateCostumeUnlockCTex, pUnlock);
			}
		}
		if (pSpecular && IS_UNLOCK_ONLY(pSpecular->eRestriction)) {
			if (stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, pSpecular->pcName, &pUnlock)) {
				CostumeUI_AddUnlockedPart(pValidateCostume, pValidateCostumeUnlockAll, pValidateCostumeUnlockCMat, pValidateCostumeUnlockCTex, pUnlock);
			}
		}
		if (pDetail && IS_UNLOCK_ONLY(pDetail->eRestriction)) {
			if (stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, pDetail->pcName, &pUnlock)) {
				CostumeUI_AddUnlockedPart(pValidateCostume, pValidateCostumeUnlockAll, pValidateCostumeUnlockCMat, pValidateCostumeUnlockCTex, pUnlock);
			}
		}
		if (pPattern && IS_UNLOCK_ONLY(pPattern->eRestriction)) {
			if (stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, pPattern->pcName, &pUnlock)) {
				CostumeUI_AddUnlockedPart(pValidateCostume, pValidateCostumeUnlockAll, pValidateCostumeUnlockCMat, pValidateCostumeUnlockCTex, pUnlock);
			}
		}
		if (pMat && IS_UNLOCK_ONLY(pMat->eRestriction)) {
			if (stashFindPointer(g_CostumeEditState.stashMatUnlockMeta, pMat->pcName, &pUnlock)) {
				CostumeUI_AddUnlockedPart(pValidateCostume, pValidateCostumeUnlockAll, pValidateCostumeUnlockCMat, pValidateCostumeUnlockCTex, pUnlock);
			}
		}
		if (pGeo && IS_UNLOCK_ONLY(pGeo->eRestriction)) {
			if (stashFindPointer(g_CostumeEditState.stashGeoUnlockMeta, pGeo->pcName, &pUnlock)) {
				CostumeUI_AddUnlockedPart(pValidateCostume, pValidateCostumeUnlockAll, pValidateCostumeUnlockCMat, pValidateCostumeUnlockCTex, pUnlock);
			}
		}
	}
}

__forceinline static void CostumeUI_UpdateMetaDataSources(SA_PARAM_NN_VALID UnlockMetaData *pUnlockData, bool bOwned, SA_PARAM_NN_VALID PlayerCostume *pCostume)
{
	PlayerCostumeRef *pCostumeRef;
	int k, j;
	PERFINFO_AUTO_START_FUNC();

	for (k = eaSize(&pUnlockData->eaCostumes) - 1; k >= 0; k--) {
		if (GET_REF(pUnlockData->eaCostumes[k]->hCostume) == pCostume) {
			break;
		}
	}

	if (k < 0) {
		pCostumeRef = StructCreate(parse_PlayerCostumeRef);
		SET_HANDLE_FROM_REFERENT(g_hPlayerCostumeDict, pCostume, pCostumeRef->hCostume);
		pUnlockData->bOwned = pUnlockData->bOwned || bOwned;
		eaPush(&pUnlockData->eaCostumes, pCostumeRef);
	}

	if (g_pMTCostumes) {
		MicroTransactionCostume *pMTCostume = eaIndexedGetUsingString(&g_pMTCostumes->ppCostumes, pCostume->pcName);
		if (pMTCostume && !pMTCostume->bHidden) {
			MicroTransactionUIProduct *pUIProduct;
			U32 uOwnedID = -1;

			// New behavior, track all the products
			for (k = eaSize(&pMTCostume->eaSources) - 1; k >= 0; k--)
			{
				if (pMTCostume->eaSources[k]->bHidden)
					continue;

				// Don't try to add duplicates
				for (j = eaSize(&pUnlockData->eaFullProductList) - 1; j >= 0; j--) {
					if (pUnlockData->eaFullProductList[j]->uID == pMTCostume->eaSources[k]->uID) {
						break;
					}
				}
				if (j >= 0) {
					continue;
				}

				pUIProduct = gclMicroTrans_MakeUIProduct(pMTCostume->eaSources[k]->uID);
				if (pUIProduct)
				{
					eaPush(&pUnlockData->eaFullProductList, pUIProduct);

					// For UI code simplification, track min/max prices outside of the array
					MIN1(pUnlockData->iMinimumProductPrice, pUIProduct->iPrice);
					MAX1(pUnlockData->iMaximumProductPrice, pUIProduct->iPrice);

					// Check to see if this is an owned ID
					if (pMTCostume->eaSources[k]->bOwned)
						uOwnedID = pMTCostume->eaSources[k]->uID;
				}
			}

			if (devassertmsg(eaSize(&pUnlockData->eaFullProductList) > 0, "No valid products found")) {
				// Old behavior, track a single random product
				// In this case, it'll be the first one it encounters or the first one it encounters that's "owned".
				if (!pUnlockData->pProduct
					|| (!pUnlockData->pProduct->bCannotPurchaseAgain && uOwnedID != -1)) {
						StructDestroySafe(parse_MicroTransactionUIProduct, &pUnlockData->pProduct);
						pUnlockData->uMicroTransactionID = uOwnedID != -1 ? uOwnedID : pUnlockData->eaFullProductList[0]->uID;
						pUnlockData->pProduct = gclMicroTrans_MakeUIProduct(pUnlockData->uMicroTransactionID);
				}
			}

			pUnlockData->bOwned = pUnlockData->bOwned || pMTCostume->bOwned;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void CostumeUI_AddUnlockMetaData(SA_PARAM_NN_VALID PlayerCostume *pCostume, bool bOwned)
{
	int j, k;
	PERFINFO_AUTO_START_FUNC();

	if (!g_CostumeEditState.stashGeoUnlockMeta) {
		g_CostumeEditState.stashGeoUnlockMeta = stashTableCreateWithStringKeys(2000, StashCaseInsensitive);
	}
	if (!g_CostumeEditState.stashMatUnlockMeta) {
		g_CostumeEditState.stashMatUnlockMeta = stashTableCreateWithStringKeys(2000, StashCaseInsensitive);
	}
	if (!g_CostumeEditState.stashTexUnlockMeta) {
		g_CostumeEditState.stashTexUnlockMeta = stashTableCreateWithStringKeys(2000, StashCaseInsensitive);
	}

	for (j = eaSize(&pCostume->eaParts) - 1; j >= 0; j--)
	{
		PCPart *pPart = pCostume->eaParts[j];
		PCGeometryDef *pGeo = GET_REF(pPart->hGeoDef);
		PCMaterialDef *pMat = GET_REF(pPart->hMatDef);
		PCTextureDef *pTexs[5] = {
			GET_REF(pPart->hPatternTexture),
			GET_REF(pPart->hSpecularTexture),
			GET_REF(pPart->hDetailTexture),
			GET_REF(pPart->hDiffuseTexture),
			pPart->pMovableTexture ? GET_REF(pPart->pMovableTexture->hMovableTexture) : NULL
		};
		UnlockMetaData *pUnlockData;

		if (pGeo && IS_UNLOCK_ONLY(pGeo->eRestriction)) {
			if (!stashFindPointer(g_CostumeEditState.stashGeoUnlockMeta, pGeo->pcName, &pUnlockData)) {
				pUnlockData = StructCreate(parse_UnlockMetaData);
				pUnlockData->iMinimumProductPrice = LLONG_MAX;
				SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, pGeo, pUnlockData->hGeometry);
				stashAddPointer(g_CostumeEditState.stashGeoUnlockMeta, pGeo->pcName, pUnlockData, false);
			}
			CostumeUI_UpdateMetaDataSources(pUnlockData, bOwned, pCostume);
		}

		if (pMat && IS_UNLOCK_ONLY(pMat->eRestriction)) {
			if (!stashFindPointer(g_CostumeEditState.stashMatUnlockMeta, pMat->pcName, &pUnlockData)) {
				pUnlockData = StructCreate(parse_UnlockMetaData);
				pUnlockData->iMinimumProductPrice = LLONG_MAX;
				SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, pMat, pUnlockData->hMaterial);
				stashAddPointer(g_CostumeEditState.stashMatUnlockMeta, pMat->pcName, pUnlockData, false);
			}
			CostumeUI_UpdateMetaDataSources(pUnlockData, bOwned, pCostume);
		}

		for (k = 0; k < ARRAY_SIZE_CHECKED(pTexs); k++) {
			if (pTexs[k] && IS_UNLOCK_ONLY(pTexs[k]->eRestriction)) {
				if (!stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, pTexs[k]->pcName, &pUnlockData)) {
					pUnlockData = StructCreate(parse_UnlockMetaData);
					pUnlockData->iMinimumProductPrice = LLONG_MAX;
					SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pTexs[k], pUnlockData->hTexture);
					stashAddPointer(g_CostumeEditState.stashTexUnlockMeta, pTexs[k]->pcName, pUnlockData, false);
				}
				CostumeUI_UpdateMetaDataSources(pUnlockData, bOwned, pCostume);
			}
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void DestroyUnlockMetaData(UnlockMetaData *pUnlockData)
{
	StructDestroy(parse_UnlockMetaData, pUnlockData);
}

static void RestoreSelected(UnlockMetaData *pUnlockSelection, UnlockMetaData *pMTOption) {
	int j;
	if (pUnlockSelection->uMicroTransactionID != pMTOption->uMicroTransactionID) {
		// Make sure the old uID is valid.
		for (j = eaSize(&pUnlockSelection->eaFullProductList) - 1; j >= 0; j--) {
			if (pUnlockSelection->eaFullProductList[j]->uID == pMTOption->uMicroTransactionID) {
				// Restore selected transaction
				StructDestroySafe(parse_MicroTransactionUIProduct, &pUnlockSelection->pProduct);
				pUnlockSelection->uMicroTransactionID = pMTOption->uMicroTransactionID;
				pUnlockSelection->pProduct = StructClone(parse_MicroTransactionUIProduct, pUnlockSelection->eaFullProductList[j]);
				break;
			}
		}
	}
}

void CostumeUI_SetUnlockedCostumes(bool bRefreshRefs, bool bRefreshList, Entity *pEnt, Entity *pSubEnt)
{
	GameAccountData *pAccountData = NULL;
	PlayerCostume *pCostume;
	int i, j;
	UnlockMetaData **eaMTOptions = NULL;
	UnlockMetaData *pUnlockSelection = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (bRefreshRefs) {
		PERFINFO_AUTO_START("Refresh Refs", 1);
		pEnt = !pEnt ? CostumeCreator_GetEditPlayerEntity() : pEnt;
		pSubEnt = !pSubEnt ? CostumeCreator_GetEditEntity() : pSubEnt;
		g_CostumeEditState.bUnlockMetaIncomplete = false;

		PERFINFO_AUTO_START("Save Selected Products", 1);
		// Save the selected microtransaction product. Reusing the UnlockMetaData
		// for temporary storage, just because I can, not for any technical reasons.
		FOR_EACH_IN_STASHTABLE(g_CostumeEditState.stashGeoUnlockMeta, UnlockMetaData, pUnlockInfo);
			if (pUnlockInfo->uMicroTransactionID) {
				pUnlockSelection = StructCreate(parse_UnlockMetaData);
				COPY_HANDLE(pUnlockSelection->hGeometry, pUnlockInfo->hGeometry);
				pUnlockSelection->uMicroTransactionID = pUnlockInfo->uMicroTransactionID;
				eaPush(&eaMTOptions, pUnlockSelection);
			}
		FOR_EACH_END;
		FOR_EACH_IN_STASHTABLE(g_CostumeEditState.stashMatUnlockMeta, UnlockMetaData, pUnlockInfo);
			if (pUnlockInfo->uMicroTransactionID) {
				pUnlockSelection = StructCreate(parse_UnlockMetaData);
				COPY_HANDLE(pUnlockSelection->hMaterial, pUnlockInfo->hMaterial);
				pUnlockSelection->uMicroTransactionID = pUnlockInfo->uMicroTransactionID;
				eaPush(&eaMTOptions, pUnlockSelection);
			}
		FOR_EACH_END;
		FOR_EACH_IN_STASHTABLE(g_CostumeEditState.stashTexUnlockMeta, UnlockMetaData, pUnlockInfo);
			if (pUnlockInfo->uMicroTransactionID) {
				pUnlockSelection = StructCreate(parse_UnlockMetaData);
				COPY_HANDLE(pUnlockSelection->hTexture, pUnlockInfo->hTexture);
				pUnlockSelection->uMicroTransactionID = pUnlockInfo->uMicroTransactionID;
				eaPush(&eaMTOptions, pUnlockSelection);
			}
		FOR_EACH_END;
		PERFINFO_AUTO_STOP();

		// Clear the unlock metadata
		PERFINFO_AUTO_START("Clear UnlockMetaData", 1);
		if (g_CostumeEditState.stashGeoUnlockMeta) {
			stashTableClearEx(g_CostumeEditState.stashGeoUnlockMeta, NULL, DestroyUnlockMetaData);
		}
		if (g_CostumeEditState.stashMatUnlockMeta) {
			stashTableClearEx(g_CostumeEditState.stashMatUnlockMeta, NULL, DestroyUnlockMetaData);
		}
		if (g_CostumeEditState.stashTexUnlockMeta) {
			stashTableClearEx(g_CostumeEditState.stashTexUnlockMeta, NULL, DestroyUnlockMetaData);
		}
		PERFINFO_AUTO_STOP();

		// Get the base list of unlocked costumes
		PERFINFO_AUTO_START("Get Owned List", 1);
		pAccountData = entity_GetGameAccount(pEnt);
		eaClearStruct(&g_CostumeEditState.eaOwnedUnlockedCostumeRefs, parse_PlayerCostumeRef);
		costumeEntity_GetUnlockCostumesRef(SAFE_MEMBER2(pEnt, pSaved, costumeData.eaUnlockedCostumeRefs), pAccountData, pEnt, pSubEnt, &g_CostumeEditState.eaOwnedUnlockedCostumeRefs);
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("Building UnlockedMetaData", 1);
		// Add owned metadata
		for (i = eaSize(&g_CostumeEditState.eaOwnedUnlockedCostumeRefs) - 1; i >= 0; i--) {
			pCostume = GET_REF(g_CostumeEditState.eaOwnedUnlockedCostumeRefs[i]->hCostume);
			if (pCostume) {
				CostumeUI_AddUnlockMetaData(pCostume, true);
			} else {
				g_CostumeEditState.bUnlockMetaIncomplete = true;
			}
		}
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("Build Usable List", 1);
		// Copy into the universal list
		eaClearStruct(&g_CostumeEditState.eaUnlockedCostumeRefs, parse_PlayerCostumeRef);
		eaCopyStructs(&g_CostumeEditState.eaOwnedUnlockedCostumeRefs, &g_CostumeEditState.eaUnlockedCostumeRefs, parse_PlayerCostumeRef);

		// Add MicroTransaction unlocks
		if (g_pMTCostumes) {
			for (i = eaSize(&g_pMTCostumes->ppCostumes) - 1; i >= 0; i--) {
				MicroTransactionCostume *pMTCostume = g_pMTCostumes->ppCostumes[i];
				if (pMTCostume->bHidden) {
					continue;
				}
				for (j = eaSize(&g_CostumeEditState.eaUnlockedCostumeRefs) - 1; j >= 0; j--) {
					if (REF_COMPARE_HANDLES(g_CostumeEditState.eaUnlockedCostumeRefs[j]->hCostume, pMTCostume->hCostume)) {
						break;
					}
				}
				if (j < 0) {
					PlayerCostumeRef *pRef = StructCreate(parse_PlayerCostumeRef);
					COPY_HANDLE(pRef->hCostume, pMTCostume->hCostume);
					eaPush(&g_CostumeEditState.eaUnlockedCostumeRefs, pRef);
				}
			}
		}
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("Building UnlockedMetaData", 1);
		// Add unowned metadata
		if (g_CostumeEditState.eaUnlockedCostumeRefs) {
			for (i = eaSize(&g_CostumeEditState.eaUnlockedCostumeRefs) - 1; i >= eaSize(&g_CostumeEditState.eaOwnedUnlockedCostumeRefs); i--) {
				pCostume = GET_REF(g_CostumeEditState.eaUnlockedCostumeRefs[i]->hCostume);
				if (pCostume) {
					CostumeUI_AddUnlockMetaData(pCostume, false);
				} else {
					g_CostumeEditState.bUnlockMetaIncomplete = true;
				}
			}
		}
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("Building Flat Unlocks", 1);
		{
			StashTableIterator iterGeo;
			StashElement elemGeo;
			NOCONST(PlayerCostume) *pFlatUnlockedGeos = CONTAINER_NOCONST(PlayerCostume, &g_CostumeEditState.FlatUnlockedGeos);
			NOCONST(PCPart) *pPart;

			bool bHaveGeo = true;

			S32 iFlatUnlocksGeo = 0;

			stashGetIterator(g_CostumeEditState.stashGeoUnlockMeta, &iterGeo);

			while (bHaveGeo) {
				bHaveGeo = stashGetNextElement(&iterGeo, &elemGeo);
				if (bHaveGeo) {
					UnlockMetaData *pUnlock = stashElementGetPointer(elemGeo);
					if (pUnlock && (!g_bHideUnownedCostumes || pUnlock->bOwned)) {
						pPart = eaGetStructNoConst(&pFlatUnlockedGeos->eaParts, parse_PCPart, iFlatUnlocksGeo++);
						COPY_HANDLE(pPart->hGeoDef, pUnlock->hGeometry);
					}
				}
			}

			eaSetSizeStructNoConst(&pFlatUnlockedGeos->eaParts, parse_PCPart, iFlatUnlocksGeo);
		}
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("Restore Selected Products", 1);
		// Go through the existing options and apply them to the UnlockMetaData.
		for (i = eaSize(&eaMTOptions) - 1; i >= 0; i--) {
			if (IS_HANDLE_ACTIVE(eaMTOptions[i]->hGeometry)) {
				if (stashFindPointer(g_CostumeEditState.stashGeoUnlockMeta, REF_STRING_FROM_HANDLE(eaMTOptions[i]->hGeometry), &pUnlockSelection)) {
					RestoreSelected(pUnlockSelection, eaMTOptions[i]);
				}
			} else if (IS_HANDLE_ACTIVE(eaMTOptions[i]->hMaterial)) {
				if (stashFindPointer(g_CostumeEditState.stashMatUnlockMeta, REF_STRING_FROM_HANDLE(eaMTOptions[i]->hMaterial), &pUnlockSelection)) {
					RestoreSelected(pUnlockSelection, eaMTOptions[i]);
				}
			} else if (IS_HANDLE_ACTIVE(eaMTOptions[i]->hTexture)) {
				if (stashFindPointer(g_CostumeEditState.stashGeoUnlockMeta, REF_STRING_FROM_HANDLE(eaMTOptions[i]->hGeometry), &pUnlockSelection)) {
					RestoreSelected(pUnlockSelection, eaMTOptions[i]);
				}
			}
		}
		eaDestroyStruct(&eaMTOptions, parse_UnlockMetaData);
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_STOP();
	}

	if (bRefreshList) {
		PlayerCostumeRef *** peaDisplayedUnlockedCostumeRefs =
			g_bHideUnownedCostumes ? &g_CostumeEditState.eaOwnedUnlockedCostumeRefs : &g_CostumeEditState.eaUnlockedCostumeRefs;

		eaSetSize(&g_CostumeEditState.eaUnlockedCostumes, eaSize(peaDisplayedUnlockedCostumeRefs));
		eaSetSize(&g_CostumeEditState.eaOwnedUnlockedCostumes, eaSize(&g_CostumeEditState.eaOwnedUnlockedCostumeRefs));

		PERFINFO_AUTO_START("Refresh Owned Refs", 1);
		// Add the owned refs
		for (i = eaSize(&g_CostumeEditState.eaOwnedUnlockedCostumeRefs) - 1; i >= 0; i--) {
			pCostume = GET_REF(g_CostumeEditState.eaOwnedUnlockedCostumeRefs[i]->hCostume);
			if (pCostume) {
				g_CostumeEditState.eaOwnedUnlockedCostumes[i] = pCostume;
			} else {
				eaRemoveFast(&g_CostumeEditState.eaOwnedUnlockedCostumes, i);
			}
		}
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("Refresh All Refs", 1);
		// Add all the refs
		for (i = eaSize(peaDisplayedUnlockedCostumeRefs) - 1; i >= 0; i--) {
			pCostume = GET_REF((*peaDisplayedUnlockedCostumeRefs)[i]->hCostume);
			if (pCostume) {
				g_CostumeEditState.eaUnlockedCostumes[i] = pCostume;
			} else {
				eaRemoveFast(&g_CostumeEditState.eaUnlockedCostumes, i);
			}
		}
		PERFINFO_AUTO_STOP();
	}

	if (bRefreshRefs && g_CostumeEditState.pCostume) {
		// Update the list of unlocked costume parts
		CostumeUI_UpdateUnlockedCostumeParts();
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static __forceinline int OrderInfo(float fOrder1, float fOrder2, const char *pcName1, const char *pcName2)
{
	int order;

	if (fOrder1 < fOrder2)
		return -1;
	if (fOrder2 > fOrder1)
		return 1;

	order = stricmp(pcName1 ? pcName1 : "", pcName2 ? pcName2 : "");
	if (order < 0)
		return -1;
	if (order > 0)
		return 1;
	return 0;
}

static int OrderUnlockedPart(UnlockedCostumePart *pUnlocked,
							 PCBoneDef *pBone,
							 PCGeometryDef *pGeo,
							 PCMaterialDef *pMat,
							 PCTextureDef *pPattern,
							 PCTextureDef *pDetail,
							 PCTextureDef *pSpecular,
							 PCTextureDef *pDiffuse,
							 PCTextureDef *pMovable)
{
	int order = 0;

	if (pBone != GET_REF(pUnlocked->hBone)) {
		PCBoneDef *pOtherBone = GET_REF(pUnlocked->hBone);
		if (pBone && pOtherBone) {
			order = OrderInfo(pBone->fOrder, pOtherBone->fOrder, TranslateDisplayMessage(pBone->displayNameMsg), TranslateDisplayMessage(pOtherBone->displayNameMsg));
		}
	}
	if (order != 0) {
		return order;
	}

	if (pGeo != GET_REF(pUnlocked->hUnlockedGeometry))
	{
		PCGeometryDef *pOtherGeo = GET_REF(pUnlocked->hUnlockedGeometry);
		if (pGeo && pOtherGeo) {
			order = OrderInfo(pGeo->fOrder, pOtherGeo->fOrder, TranslateDisplayMessage(pGeo->displayNameMsg), TranslateDisplayMessage(pOtherGeo->displayNameMsg));
		}
	}
	if (order != 0) {
		return order;
	}

	if (pMat != GET_REF(pUnlocked->hUnlockedMaterial))
	{
		PCMaterialDef *pOtherMat = GET_REF(pUnlocked->hUnlockedMaterial);
		if (pMat && pOtherMat) {
			order = OrderInfo(pMat->fOrder, pOtherMat->fOrder, TranslateDisplayMessage(pMat->displayNameMsg), TranslateDisplayMessage(pOtherMat->displayNameMsg));
		}
	}
	if (order != 0) {
		return order;
	}
	if (pPattern != GET_REF(pUnlocked->hUnlockedPatternTexture))
	{
		PCTextureDef *pOtherPattern = GET_REF(pUnlocked->hUnlockedPatternTexture);
		if (pPattern && pOtherPattern) {
			order = OrderInfo(pPattern->fOrder, pOtherPattern->fOrder, TranslateDisplayMessage(pPattern->displayNameMsg), TranslateDisplayMessage(pOtherPattern->displayNameMsg));
		}
	}
	if (order != 0) {
		return order;
	}

	if (pDetail != GET_REF(pUnlocked->hUnlockedDetailTexture))
	{
		PCTextureDef *pOtherDetail = GET_REF(pUnlocked->hUnlockedDetailTexture);
		if (pDetail && pOtherDetail) {
			order = OrderInfo(pDetail->fOrder, pOtherDetail->fOrder, TranslateDisplayMessage(pDetail->displayNameMsg), TranslateDisplayMessage(pOtherDetail->displayNameMsg));
		}
	}
	if (order != 0) {
		return order;
	}

	if (pSpecular != GET_REF(pUnlocked->hUnlockedSpecularTexture))
	{
		PCTextureDef *pOtherSpecular = GET_REF(pUnlocked->hUnlockedSpecularTexture);
		if (pSpecular && pOtherSpecular) {
			order = OrderInfo(pSpecular->fOrder, pOtherSpecular->fOrder, TranslateDisplayMessage(pSpecular->displayNameMsg), TranslateDisplayMessage(pOtherSpecular->displayNameMsg));
		}
	}
	if (order != 0) {
		return order;
	}

	if (pDiffuse != GET_REF(pUnlocked->hUnlockedDiffuseTexture))
	{
		PCTextureDef *pOtherDiffuse = GET_REF(pUnlocked->hUnlockedDiffuseTexture);
		if (pDiffuse && pOtherDiffuse) {
			order = OrderInfo(pDiffuse->fOrder, pOtherDiffuse->fOrder, TranslateDisplayMessage(pDiffuse->displayNameMsg), TranslateDisplayMessage(pOtherDiffuse->displayNameMsg));
		}
	}
	if (order != 0) {
		return order;
	}

	if (pMovable != GET_REF(pUnlocked->hUnlockedMovableTexture))
	{
		PCTextureDef *pOtherMovable = GET_REF(pUnlocked->hUnlockedMovableTexture);
		if (pMovable && pOtherMovable) {
			order = OrderInfo(pMovable->fOrder, pOtherMovable->fOrder, TranslateDisplayMessage(pMovable->displayNameMsg), TranslateDisplayMessage(pOtherMovable->displayNameMsg));
		}
	}

	return order;
}

static __forceinline const char *FormatUnlockedPartLocation(const char *pchBoneName, SA_PARAM_NN_VALID UnlockedCostumePart *pUnlocked)
{
	PCGeometryDef *pGeometry = GET_REF(pUnlocked->hUnlockedGeometry);
	PCMaterialDef *pMaterial = GET_REF(pUnlocked->hUnlockedMaterial);
	PCTextureDef *pPattern = GET_REF(pUnlocked->hUnlockedPatternTexture);
	PCTextureDef *pDetail = GET_REF(pUnlocked->hUnlockedDetailTexture);
	PCTextureDef *pSpecular = GET_REF(pUnlocked->hUnlockedSpecularTexture);
	PCTextureDef *pDiffuse = GET_REF(pUnlocked->hUnlockedDiffuseTexture);
	PCTextureDef *pMovable = GET_REF(pUnlocked->hUnlockedMovableTexture);

	if (pUnlocked->bMatUnlock) {
		if (s_pchUnlockedMaterialMessage && *s_pchUnlockedMaterialMessage) {
			FormatGameMessageKey(&pUnlocked->estrLocation, s_pchUnlockedMaterialMessage,
				STRFMT_STRING("Bone", pchBoneName),
				STRFMT_DISPLAYMESSAGE("Geometry", pGeometry->displayNameMsg),
				STRFMT_DISPLAYMESSAGE("Material", pMaterial->displayNameMsg),
				STRFMT_END);
			pchBoneName = NULL;
		}
	} else if (pUnlocked->bTexPatternUnlock) {
		if (s_pchUnlockedPatternMessage && *s_pchUnlockedPatternMessage) {
			FormatGameMessageKey(&pUnlocked->estrLocation, s_pchUnlockedPatternMessage,
				STRFMT_STRING("Bone", pchBoneName),
				STRFMT_DISPLAYMESSAGE("Geometry", pGeometry->displayNameMsg),
				STRFMT_DISPLAYMESSAGE("Material", pMaterial->displayNameMsg),
				STRFMT_DISPLAYMESSAGE("Pattern", pPattern->displayNameMsg),
				STRFMT_END);
			pchBoneName = NULL;
		}
	} else if (pUnlocked->bTexDetailUnlock) {
		if (s_pchUnlockedDetailMessage && *s_pchUnlockedDetailMessage) {
			FormatGameMessageKey(&pUnlocked->estrLocation, s_pchUnlockedDetailMessage,
				STRFMT_STRING("Bone", pchBoneName),
				STRFMT_DISPLAYMESSAGE("Geometry", pGeometry->displayNameMsg),
				STRFMT_DISPLAYMESSAGE("Material", pMaterial->displayNameMsg),
				STRFMT_DISPLAYMESSAGE("Detail", pDetail->displayNameMsg),
				STRFMT_END);
			pchBoneName = NULL;
		}
	} else if (pUnlocked->bTexSpecularUnlock) {
		if (s_pchUnlockedSpecularMessage && *s_pchUnlockedSpecularMessage) {
			FormatGameMessageKey(&pUnlocked->estrLocation, s_pchUnlockedSpecularMessage,
				STRFMT_STRING("Bone", pchBoneName),
				STRFMT_DISPLAYMESSAGE("Geometry", pGeometry->displayNameMsg),
				STRFMT_DISPLAYMESSAGE("Material", pMaterial->displayNameMsg),
				STRFMT_DISPLAYMESSAGE("Specular", pSpecular->displayNameMsg),
				STRFMT_END);
			pchBoneName = NULL;
		}
	} else if (pUnlocked->bTexDiffuseUnlock) {
		if (s_pchUnlockedDiffuseMessage && *s_pchUnlockedDiffuseMessage) {
			FormatGameMessageKey(&pUnlocked->estrLocation, s_pchUnlockedDiffuseMessage,
				STRFMT_STRING("Bone", pchBoneName),
				STRFMT_DISPLAYMESSAGE("Geometry", pGeometry->displayNameMsg),
				STRFMT_DISPLAYMESSAGE("Material", pMaterial->displayNameMsg),
				STRFMT_DISPLAYMESSAGE("Diffuse", pDiffuse->displayNameMsg),
				STRFMT_END);
			pchBoneName = NULL;
		}
	} else if (pUnlocked->bTexMovableUnlock) {
		if (s_pchUnlockedMovableMessage && *s_pchUnlockedMovableMessage) {
			FormatGameMessageKey(&pUnlocked->estrLocation, s_pchUnlockedMovableMessage,
				STRFMT_STRING("Bone", pchBoneName),
				STRFMT_DISPLAYMESSAGE("Geometry", pGeometry->displayNameMsg),
				STRFMT_DISPLAYMESSAGE("Material", pMaterial->displayNameMsg),
				STRFMT_DISPLAYMESSAGE("Movable", pMovable->displayNameMsg),
				STRFMT_END);
			pchBoneName = NULL;
		}
	}
	return pchBoneName;
}

S32 CostumeUI_AddUnlockedCostumeParts(UnlockedCostumePart ***peaUnlocked, S32 iUsed, PlayerCostume *pPCCostume, PCCategory **eaAllValidCategories, PCBoneDef **eaValidBones, const char **eaFilter, PCBoneDef **eaSkeletonBones, bool bSoftCostumeRef)
{
	static PCPart **s_eaIgnoredParts = NULL;
	int j, k;

	eaClear(&s_eaIgnoredParts);

	for (j = 0; j < eaSize(&pPCCostume->eaParts); j++)
	{
		PCPart *pPart = pPCCostume->eaParts[j];
		PCBoneDef *pBone = GET_REF(pPart->hBoneDef);
		PCRegion *pRegion = SAFE_GET_REF(pBone, hRegion);
		PCGeometryDef *pGeometry = GET_REF(pPart->hGeoDef);
		PCMaterialDef *pMaterial = GET_REF(pPart->hMatDef);
		PCTextureDef *pPattern = GET_REF(pPart->hPatternTexture);
		PCTextureDef *pDetail = GET_REF(pPart->hDetailTexture);
		PCTextureDef *pSpecular = GET_REF(pPart->hSpecularTexture);
		PCTextureDef *pDiffuse = GET_REF(pPart->hDiffuseTexture);
		PCTextureDef *pMovable = pPart->pMovableTexture ? GET_REF(pPart->pMovableTexture->hMovableTexture) : NULL;
		UnlockedCostumePart *pUnlocked;
		const char *pchName = NULL;
		const char *pchInternalName = NULL;
		const char *pchBoneName;

		if (!pRegion)
		{
			continue;
		}

		if (eaFind(&s_eaIgnoredParts, pPart) >= 0)
		{
			// flagged as a mirror part
			continue;
		}

		if (eaSkeletonBones && eaFind(&eaSkeletonBones, pBone) < 0)
		{
			// Not a part of this skeleton
			continue;
		}		

		if (!pGeometry || !(pGeometry->eRestriction & (kPCRestriction_Player | kPCRestriction_Player_Initial)) ||
			pMaterial && !(pMaterial->eRestriction & (kPCRestriction_Player | kPCRestriction_Player_Initial)) || !pMaterial && IS_HANDLE_ACTIVE(pPart->hMatDef) ||
			pPattern && !(pPattern->eRestriction & (kPCRestriction_Player | kPCRestriction_Player_Initial)) || !pPattern && IS_HANDLE_ACTIVE(pPart->hPatternTexture) ||
			pDetail && !(pDetail->eRestriction & (kPCRestriction_Player | kPCRestriction_Player_Initial)) || !pDetail && IS_HANDLE_ACTIVE(pPart->hDetailTexture) ||
			pSpecular && !(pSpecular->eRestriction & (kPCRestriction_Player | kPCRestriction_Player_Initial)) || !pSpecular && IS_HANDLE_ACTIVE(pPart->hSpecularTexture) ||
			pDiffuse && !(pDiffuse->eRestriction & (kPCRestriction_Player | kPCRestriction_Player_Initial)) || !pDiffuse && IS_HANDLE_ACTIVE(pPart->hDiffuseTexture) ||
			pMovable && !(pMovable->eRestriction & (kPCRestriction_Player | kPCRestriction_Player_Initial)) || !pMovable && pPart->pMovableTexture && IS_HANDLE_ACTIVE(pPart->pMovableTexture->hMovableTexture))
		{
			// if something is invalid, don't add it
			continue;
		}

		pUnlocked = eaGetStruct(peaUnlocked, parse_UnlockedCostumePart, iUsed);

		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, pRegion, pUnlocked->hRegion);
		SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, pBone, pUnlocked->hBone);

		pUnlocked->bGeoUnlock = pGeometry != NULL && IS_UNLOCK_ONLY(pGeometry->eRestriction);
		pUnlocked->bMatUnlock = pMaterial != NULL && IS_UNLOCK_ONLY(pMaterial->eRestriction);
		pUnlocked->bTexPatternUnlock = pPattern != NULL && IS_UNLOCK_ONLY(pPattern->eRestriction);
		pUnlocked->bTexDetailUnlock = pDetail != NULL && IS_UNLOCK_ONLY(pDetail->eRestriction);
		pUnlocked->bTexSpecularUnlock = pSpecular != NULL && IS_UNLOCK_ONLY(pSpecular->eRestriction);
		pUnlocked->bTexDiffuseUnlock = pDiffuse != NULL && IS_UNLOCK_ONLY(pDiffuse->eRestriction);
		pUnlocked->bTexMovableUnlock = pMovable != NULL && IS_UNLOCK_ONLY(pMovable->eRestriction);

		if (!pUnlocked->bGeoUnlock && !pUnlocked->bMatUnlock &&
			!pUnlocked->bTexPatternUnlock && !pUnlocked->bTexDetailUnlock &&
			!pUnlocked->bTexSpecularUnlock && !pUnlocked->bTexDiffuseUnlock && !pUnlocked->bTexMovableUnlock)
		{
			continue;
		}

		// Get part name
		if (pUnlocked->bGeoUnlock) {
			pchName = TranslateDisplayMessage(pGeometry->displayNameMsg);
			pchInternalName = pGeometry->pcName;
		} else if (pUnlocked->bMatUnlock) {
			pchName = TranslateDisplayMessage(pMaterial->displayNameMsg);
			pchInternalName = pMaterial->pcName;
		} else if (pUnlocked->bTexPatternUnlock) {
			pchName = TranslateDisplayMessage(pPattern->displayNameMsg);
			pchInternalName = pPattern->pcName;
		} else if (pUnlocked->bTexDetailUnlock) {
			pchName = TranslateDisplayMessage(pDetail->displayNameMsg);
			pchInternalName = pDetail->pcName;
		} else if (pUnlocked->bTexSpecularUnlock) {
			pchName = TranslateDisplayMessage(pSpecular->displayNameMsg);
			pchInternalName = pSpecular->pcName;
		} else if (pUnlocked->bTexDiffuseUnlock) {
			pchName = TranslateDisplayMessage(pDiffuse->displayNameMsg);
			pchInternalName = pDiffuse->pcName;
		} else if (pUnlocked->bTexMovableUnlock) {
			pchName = TranslateDisplayMessage(pMovable->displayNameMsg);
			pchInternalName = pMovable->pcName;
		}

		// Check for mirror part
		pUnlocked->bBoth = false;
		if (GET_REF(pBone->hMirrorBone))
		{
			const char *pchMirrorName = NULL;
			PCPart *pMirrorPart = NULL;

			for (k = j + 1; k < eaSize(&pPCCostume->eaParts); k++)
			{
				if (GET_REF(pPCCostume->eaParts[k]->hBoneDef) == GET_REF(pBone->hMirrorBone))
				{
					pMirrorPart = pPCCostume->eaParts[k];

					if (pMirrorPart && pUnlocked->bGeoUnlock)
					{
						PCGeometryDef *pMirrorGeo = GET_REF(pMirrorPart->hGeoDef);
						if (pMirrorGeo && pMirrorGeo != pGeometry) {
							pchMirrorName = TranslateDisplayMessage(pMirrorGeo->displayNameMsg);
						} else if (!pMirrorGeo) {
							pMirrorPart = NULL;
						}
					}
					else if (pMirrorPart && pUnlocked->bMatUnlock)
					{
						PCMaterialDef *pMirrorMat = GET_REF(pMirrorPart->hMatDef);
						if (pMirrorMat && pMirrorMat != pMaterial) {
							pchMirrorName = TranslateDisplayMessage(pMirrorMat->displayNameMsg);
						} else if (!pMirrorMat) {
							pMirrorPart = NULL;
						}
					}
					else if (pMirrorPart && pUnlocked->bTexPatternUnlock)
					{
						PCTextureDef *pMirrorPattern = GET_REF(pMirrorPart->hPatternTexture);
						if (pMirrorPattern && pMirrorPattern != pPattern) {
							pchMirrorName = TranslateDisplayMessage(pMirrorPattern->displayNameMsg);
						} else if (!pMirrorPattern) {
							pMirrorPart = NULL;
						}
					}
					else if (pMirrorPart && pUnlocked->bTexDetailUnlock)
					{
						PCTextureDef *pMirrorDetail = GET_REF(pMirrorPart->hDetailTexture);
						if (pMirrorDetail && pMirrorDetail != pDetail) {
							pchMirrorName = TranslateDisplayMessage(pMirrorDetail->displayNameMsg);
						} else if (!pMirrorDetail) {
							pMirrorPart = NULL;
						}
					}
					else if (pMirrorPart && pUnlocked->bTexSpecularUnlock)
					{
						PCTextureDef *pMirrorSpecular = GET_REF(pMirrorPart->hSpecularTexture);
						if (pMirrorSpecular && pMirrorSpecular != pSpecular) {
							pchMirrorName = TranslateDisplayMessage(pMirrorSpecular->displayNameMsg);
						} else if (!pMirrorSpecular) {
							pMirrorPart = NULL;
						}
					}
					else if (pMirrorPart && pUnlocked->bTexDiffuseUnlock)
					{
						PCTextureDef *pMirrorDiffuse = GET_REF(pMirrorPart->hDiffuseTexture);
						if (pMirrorDiffuse && pMirrorDiffuse != pDiffuse) {
							pchMirrorName = TranslateDisplayMessage(pMirrorDiffuse->displayNameMsg);
						} else if (!pMirrorDiffuse) {
							pMirrorPart = NULL;
						}
					}
					else if (pMirrorPart && pUnlocked->bTexMovableUnlock)
					{
						PCTextureDef *pMirrorMovable = GET_REF(pMirrorPart->pMovableTexture->hMovableTexture);
						if (pMirrorMovable && pMirrorMovable != pMovable) {
							pchMirrorName = TranslateDisplayMessage(pMirrorMovable->displayNameMsg);
						} else if (!pMirrorMovable) {
							pMirrorPart = NULL;
						}
					}

					if (pchName && pchMirrorName && stricmp(pchName, pchMirrorName) != 0) {
						pMirrorPart = NULL;
					}

					if (pMirrorPart) {
						eaPush(&s_eaIgnoredParts, pMirrorPart);
						pUnlocked->bBoth = true;
					}
				}
			}
		}

		// Already in list
		for (k = iUsed - 1; k >= 0; k--) {
			UnlockedCostumePart *pOtherUnlocked = (*peaUnlocked)[k];
			bool bMatched = false;
			if (GET_REF(pPCCostume->hSkeleton) != GET_REF(pOtherUnlocked->hSkeleton)) {
				// ignore parts from other skeletons
			} else if (pOtherUnlocked->bGeoUnlock && pUnlocked->bGeoUnlock && GET_REF(pOtherUnlocked->hUnlockedGeometry) == pGeometry) {
				bMatched = true;
			} else if (pOtherUnlocked->bMatUnlock && pUnlocked->bMatUnlock && GET_REF(pOtherUnlocked->hUnlockedGeometry) == pGeometry && GET_REF(pOtherUnlocked->hUnlockedMaterial) == pMaterial) {
				bMatched = true;
			} else if (pOtherUnlocked->bTexPatternUnlock && pUnlocked->bTexPatternUnlock && GET_REF(pOtherUnlocked->hUnlockedGeometry) == pGeometry && GET_REF(pOtherUnlocked->hUnlockedPatternTexture) == pPattern) {
				bMatched = true;
			} else if (pOtherUnlocked->bTexDetailUnlock && pUnlocked->bTexDetailUnlock && GET_REF(pOtherUnlocked->hUnlockedGeometry) == pGeometry && GET_REF(pOtherUnlocked->hUnlockedDetailTexture) == pDetail) {
				bMatched = true;
			} else if (pOtherUnlocked->bTexSpecularUnlock && pUnlocked->bTexSpecularUnlock && GET_REF(pOtherUnlocked->hUnlockedGeometry) == pGeometry && GET_REF(pOtherUnlocked->hUnlockedSpecularTexture) == pSpecular) {
				bMatched = true;
			} else if (pOtherUnlocked->bTexDiffuseUnlock && pUnlocked->bTexDiffuseUnlock && GET_REF(pOtherUnlocked->hUnlockedGeometry) == pGeometry && GET_REF(pOtherUnlocked->hUnlockedDiffuseTexture) == pDiffuse) {
				bMatched = true;
			} else if (pOtherUnlocked->bTexMovableUnlock && pUnlocked->bTexMovableUnlock && GET_REF(pOtherUnlocked->hUnlockedGeometry) == pGeometry && GET_REF(pOtherUnlocked->hUnlockedMovableTexture) == pMovable) {
				bMatched = true;
			}

			if (bMatched) {
				if (OrderUnlockedPart(pOtherUnlocked, pBone, pGeometry, pMaterial, pPattern, pDetail, pSpecular, pDiffuse, pMovable) < 0) {
					PCBoneDef *pOtherUnlockedBone = GET_REF(pOtherUnlocked->hBone);
					PCGeometryDef *pOtherUnlockedGeo = GET_REF(pOtherUnlocked->hUnlockedGeometry);

					if (pOtherUnlocked->bBoth) {
						pchBoneName = TranslateDisplayMessage(pOtherUnlockedBone->mergeNameMsg);
					} else {
						pchBoneName = TranslateDisplayMessage(pOtherUnlockedBone->displayNameMsg);
					}

					// Do not want to update the Geo, since the geo defines categories
					if (pOtherUnlockedGeo && pMaterial && eaFind(&pOtherUnlockedGeo->eaAllowedMaterialDefs, pMaterial->pcName) >= 0) {
						SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, pMaterial, pOtherUnlocked->hUnlockedMaterial);
					}
					// Textures?

					if (bSoftCostumeRef) {
						SET_HANDLE_FROM_REFERENT(g_hPlayerCostumeDict, pPCCostume, pUnlocked->hSoftCostume);
					} else if (IS_HANDLE_ACTIVE(pUnlocked->hSoftCostume)) {
						REMOVE_HANDLE(pUnlocked->hSoftCostume);
					}

					// Reformat the unlocked part
					estrClear(&pOtherUnlocked->estrLocation);
					FormatUnlockedPartLocation(pchBoneName, pOtherUnlocked);
				}
				break;
			}
		}
		if (k >= 0) {
			continue;
		}

		pUnlocked->bUsable = eaFind(&eaValidBones, pBone) >= 0;

		COPY_HANDLE(pUnlocked->hSkeleton, pPCCostume->hSkeleton);
		SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, pGeometry, pUnlocked->hUnlockedGeometry);
		SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, pMaterial, pUnlocked->hUnlockedMaterial);

		if (pUnlocked->bTexPatternUnlock) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pPattern, pUnlocked->hUnlockedPatternTexture);
		} else {
			REMOVE_HANDLE(pUnlocked->hUnlockedPatternTexture);
		}

		if (pUnlocked->bTexDetailUnlock) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pDetail, pUnlocked->hUnlockedDetailTexture);
		} else {
			REMOVE_HANDLE(pUnlocked->hUnlockedDetailTexture);
		}

		if (pUnlocked->bTexSpecularUnlock) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pSpecular, pUnlocked->hUnlockedSpecularTexture);
		} else {
			REMOVE_HANDLE(pUnlocked->hUnlockedSpecularTexture);
		}

		if (pUnlocked->bTexDiffuseUnlock) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pDiffuse, pUnlocked->hUnlockedDiffuseTexture);
		} else {
			REMOVE_HANDLE(pUnlocked->hUnlockedDiffuseTexture);
		}

		if (pUnlocked->bTexMovableUnlock) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pMovable, pUnlocked->hUnlockedMovableTexture);
		} else {
			REMOVE_HANDLE(pUnlocked->hUnlockedMovableTexture);
		}

		if (!pUnlocked->pchName || stricmp(pchName, pUnlocked->pchName))
		{
			if (pUnlocked->pchName)
				StructFreeString(pUnlocked->pchName);
			pUnlocked->pchName = StructAllocString(pchName);
		}

		if (pUnlocked->bBoth) {
			pchBoneName = TranslateDisplayMessage(pBone->mergeNameMsg);
		} else {
			pchBoneName = TranslateDisplayMessage(pBone->displayNameMsg);
		}

		estrClear(&pUnlocked->estrLocation);

		if (!pUnlocked->bGeoUnlock) {
			pchBoneName = FormatUnlockedPartLocation(pchBoneName, pUnlocked);
		}

		// Unformatted
		if (pchBoneName) {
			estrCopy2(&pUnlocked->estrLocation, pchBoneName);
		}

		// Shut up /ANALYZE
		assert(pGeometry);

		// Find including categories
		{
			int iIncludedCategories = 0;
			int iUnavailableCategories = 0;
			int iExcludedCategories = 0;
			for (k = eaSize(&pGeometry->eaCategories) - 1; k >= 0; k--) {
				if (eaFind(&eaAllValidCategories, GET_REF(pGeometry->eaCategories[k]->hCategory)) >= 0) {
					PCCategoryRef *pIncludedCategory = eaGetStruct(&pUnlocked->eaIncludedCategories, parse_PCCategoryRef, iIncludedCategories++);
					COPY_HANDLE(pIncludedCategory->hCategory, pGeometry->eaCategories[k]->hCategory);
				} else {
					PCCategoryRef *pUnavailableCategory = eaGetStruct(&pUnlocked->eaUnavailableCategories, parse_PCCategoryRef, iUnavailableCategories++);
					COPY_HANDLE(pUnavailableCategory->hCategory, pGeometry->eaCategories[k]->hCategory);
				}
			}
			eaSetSizeStruct(&pUnlocked->eaIncludedCategories, parse_PCCategoryRef, iIncludedCategories);
			eaSetSizeStruct(&pUnlocked->eaUnavailableCategories, parse_PCCategoryRef, iUnavailableCategories);
			pUnlocked->bUsable = pUnlocked->bUsable && eaSize(&pUnlocked->eaIncludedCategories) > 0;

			if (g_CostumeEditState.pCostume) {
				for (k = eaSize(&g_CostumeEditState.pCostume->eaRegionCategories) - 1; k >= 0; k--) {
					PCRegion *pCategoryRegion = GET_REF(g_CostumeEditState.pCostume->eaRegionCategories[k]->hRegion);
					PCCategory *pCategory = GET_REF(g_CostumeEditState.pCostume->eaRegionCategories[k]->hCategory);

					if (!pCategoryRegion || !pCategory) {
						continue;
					}

					if (pCategoryRegion != pRegion) {
						bool bExcluded = false;
						int l, m;
						for (l = eaSize(&pCategory->eaExcludedBones) - 1; l >= 0; l--) {
							if (GET_REF(pCategory->eaExcludedBones[l]->hBone) == pBone) {
								bExcluded = true;
							}
						}
						if (!bExcluded) {
							bExcluded = true;
							for (l = eaSize(&pGeometry->eaCategories) - 1; l >= 0; l--) {
								bool bCategoryExcluded = false;
								for (m = eaSize(&pCategory->eaExcludedCategories) - 1; m >= 0; m--) {
									if (GET_REF(pGeometry->eaCategories[l]->hCategory) == GET_REF(pCategory->eaExcludedCategories[m]->hCategory)) {
										bCategoryExcluded = true;
										break;
									}
								}
								if (!bCategoryExcluded) {
									bExcluded = false;
									break;
								}
							}
						}
						if (bExcluded) {
							PCCategoryRef *pRef = eaGetStruct(&pUnlocked->eaExcludedCategories, parse_PCCategoryRef, iExcludedCategories++);
							SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, pCategory, pRef->hCategory);
						}
					}
				}
				eaSetSizeStruct(&pUnlocked->eaExcludedCategories, parse_PCCategoryRef, iExcludedCategories);
			}
		}

		costumeTailor_SortCategoryRefs(pUnlocked->eaIncludedCategories, true);
		costumeTailor_SortCategoryRefs(pUnlocked->eaExcludedCategories, true);
		costumeTailor_SortCategoryRefs(pUnlocked->eaUnavailableCategories, true);

		// Fill in ownership information
		pUnlocked->pUnlockData = NULL;
		if (pUnlocked->bGeoUnlock) {
			stashFindPointer(g_CostumeEditState.stashGeoUnlockMeta, pchInternalName, &pUnlocked->pUnlockData);
		} else if (pUnlocked->bMatUnlock) {
			stashFindPointer(g_CostumeEditState.stashMatUnlockMeta, pchInternalName, &pUnlocked->pUnlockData);
		} else if (pUnlocked->bTexPatternUnlock || pUnlocked->bTexSpecularUnlock || pUnlocked->bTexDetailUnlock || pUnlocked->bTexDiffuseUnlock || pUnlocked->bTexMovableUnlock) {
			stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, pchInternalName, &pUnlocked->pUnlockData);
		}
		if (bSoftCostumeRef) {
			SET_HANDLE_FROM_REFERENT(g_hPlayerCostumeDict, pPCCostume, pUnlocked->hSoftCostume);
		} else if (IS_HANDLE_ACTIVE(pUnlocked->hSoftCostume)) {
			REMOVE_HANDLE(pUnlocked->hSoftCostume);
		}

		if (pUnlocked->pUnlockData) {
			// Arbitrary sort order for unlock information
			//   1) In-game earned costumes
			//   2) Purchased Micro Transaction costumes
			//   3) Unpurchased Micro Transaction costumes
			if (pUnlocked->pUnlockData->bOwned && !pUnlocked->pUnlockData->uMicroTransactionID) {
				pUnlocked->pchUnlockOrder = "1";
			} else if (pUnlocked->pUnlockData->bOwned) {
				pUnlocked->pchUnlockOrder = "2";
			} else {
				pUnlocked->pchUnlockOrder = "3";
			}
		} else {
			pUnlocked->pchUnlockOrder = "0";
		}

		if (eaSize(&eaFilter)) {
			for (k = eaSize(&eaFilter) - 1; k >= 0; k--) {
				if (!strstri(pUnlocked->pchName, eaFilter[k]) && !strstri(pUnlocked->estrLocation, eaFilter[k])) {
					break;
				}
			}
			if (k < 0) {
				eaPush(&g_CostumeEditState.eaFilteredUnlockedCostumeParts, pUnlocked);
			}
		}

		iUsed++;
	}

	return iUsed;
}

void CostumeUI_UpdateUnlockedCostumeParts(void)
{
	PlayerCostume **eaFlatList = NULL;
	PCCategory **eaValidCategories = NULL;
	PCCategory **eaAllValidCategories = NULL;
	PCBoneDef **eaValidBones = NULL;
	PCBoneDef **eaSkeletonBones = NULL;
	PCRegion **eaRegions = NULL;
	UnlockedCostumePart ***peaUnlocked = &g_CostumeEditState.eaUnlockedCostumeParts;
	PCSkeletonDef *pCostumeEditStateSkel = GET_REF(g_CostumeEditState.hSkeleton);
	const char **eaFilter = NULL;
	int iUsed = 0;
	int i;
	PERFINFO_AUTO_START_FUNC();

	eaPush(&eaFlatList, &g_CostumeEditState.FlatUnlockedGeos);
	costumeTailor_GetValidBones(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.pCostume->hSkeleton), NULL, NULL, GET_REF(g_CostumeEditState.pCostume->hSpecies), eaFlatList, g_CostumeEditState.eaPowerFXBones, &eaValidBones, CGVF_OMIT_EMPTY);
	eaClearFast(&g_CostumeEditState.eaFilteredUnlockedCostumeParts);

	costumeTailor_GetValidRegions(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), eaFlatList, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, &eaRegions, 0);
	for (i = 0; i < eaSize(&eaRegions); i++) {
		costumeTailor_GetValidCategories(g_CostumeEditState.pCostume, eaRegions[i], GET_REF(g_CostumeEditState.hSpecies), eaFlatList, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, &eaValidCategories, CGVF_OMIT_EMPTY);
		eaPushEArray(&eaAllValidCategories, &eaValidCategories);
	}

	if (g_CostumeEditState.pchUnlockedCostumeFilter) {
		char *pchTmp = NULL, *pchContext = NULL, *pchToken;
		strdup_alloca(pchTmp, g_CostumeEditState.pchUnlockedCostumeFilter);
		while ((pchToken = strtok_r(pchContext ? NULL : pchTmp, " \r\n\t,", &pchContext)) != NULL) {
			eaPush(&eaFilter, pchToken);
		}
	}

	if (pCostumeEditStateSkel)
	{
		for (i = 0; i < eaSize(&pCostumeEditStateSkel->eaOptionalBoneDefs); i++)
		{
			PCBoneDef *pBoneDef = SAFE_GET_REF(pCostumeEditStateSkel->eaOptionalBoneDefs[i], hBone);
			if (pBoneDef)
				eaPush(&eaSkeletonBones, pBoneDef);
		}
		for (i = 0; i < eaSize(&pCostumeEditStateSkel->eaRequiredBoneDefs); i++)
		{
			PCBoneDef *pBoneDef = SAFE_GET_REF(pCostumeEditStateSkel->eaRequiredBoneDefs[i], hBone);
			if (pBoneDef)
				eaPush(&eaSkeletonBones, pBoneDef);
		}
	}

	for (i = 0; i < eaSize(&g_CostumeEditState.eaUnlockedCostumes); i++)
	{
		PlayerCostume *pPCCostume = g_CostumeEditState.eaUnlockedCostumes[i];
		iUsed = CostumeUI_AddUnlockedCostumeParts(peaUnlocked, iUsed, pPCCostume, eaAllValidCategories, eaValidBones, eaFilter, eaSkeletonBones, false);
	}

	eaSetSizeStruct(peaUnlocked, parse_UnlockedCostumePart, iUsed);

	eaDestroy(&eaFilter);

	eaDestroy(&eaRegions);
	eaDestroy(&eaValidBones);
	eaDestroy(&eaValidCategories);
	eaDestroy(&eaAllValidCategories);
	eaDestroy(&eaFlatList);
	
	PERFINFO_AUTO_STOP_FUNC();
}

void CostumeUI_ClearUnlockMetaData(void)
{
	if (g_CostumeEditState.stashGeoUnlockMeta) {
		stashTableClearEx(g_CostumeEditState.stashGeoUnlockMeta, NULL, DestroyUnlockMetaData);
	}
	if (g_CostumeEditState.stashMatUnlockMeta) {
		stashTableClearEx(g_CostumeEditState.stashMatUnlockMeta, NULL, DestroyUnlockMetaData);
	}
	if (g_CostumeEditState.stashTexUnlockMeta) {
		stashTableClearEx(g_CostumeEditState.stashTexUnlockMeta, NULL, DestroyUnlockMetaData);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetHideUnownedParts);
void CostumeCreator_SetHideUnownedParts(ExprContext *pContext, bool bHide)
{
	COSTUME_UI_TRACE_FUNC();
	g_bHideUnownedCostumes = bHide;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_GetHideUnownedParts);
bool CostumeCreator_GetHideUnownedParts(ExprContext *pContext)
{
	COSTUME_UI_TRACE_FUNC();
	return g_bHideUnownedCostumes;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_GetUnlockedCostumePartsSize);
int CostumeCreator_GetUnlockedCostumePartsSize(ExprContext *pContext)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaUnlockedCostumeParts);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_GetUnlockedCostumeParts);
void CostumeCreator_GetUnlockedCostumeParts(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	ui_GenSetList(pGen, &g_CostumeEditState.eaUnlockedCostumeParts, parse_UnlockedCostumePart);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_GetFilteredUnlockedCostumePartsSize);
int CostumeCreator_GetFilteredUnlockedCostumePartsSize(ExprContext *pContext, const char *pchFilter)
{
	COSTUME_UI_TRACE_FUNC();
	if (!pchFilter || !*pchFilter) {
		return eaSize(&g_CostumeEditState.eaUnlockedCostumeParts);
	}
	if (!g_CostumeEditState.pchUnlockedCostumeFilter || stricmp(g_CostumeEditState.pchUnlockedCostumeFilter, pchFilter)) {
		StructFreeStringSafe(&g_CostumeEditState.pchUnlockedCostumeFilter);
		g_CostumeEditState.pchUnlockedCostumeFilter = StructAllocString(pchFilter);
		CostumeUI_UpdateUnlockedCostumeParts();
	}
	return eaSize(&g_CostumeEditState.eaFilteredUnlockedCostumeParts);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_GetFilteredUnlockedCostumeParts);
void CostumeCreator_GetFilteredUnlockedCostumeParts(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchFilter)
{
	UnlockedCostumePart ***peaList = NULL;
	COSTUME_UI_TRACE_FUNC();
	if (!pchFilter || !*pchFilter) {
		ui_GenSetList(pGen, &g_CostumeEditState.eaUnlockedCostumeParts, parse_UnlockedCostumePart);
		return;
	}
	if (!g_CostumeEditState.pchUnlockedCostumeFilter || stricmp(g_CostumeEditState.pchUnlockedCostumeFilter, pchFilter)) {
		StructFreeStringSafe(&g_CostumeEditState.pchUnlockedCostumeFilter);
		g_CostumeEditState.pchUnlockedCostumeFilter = StructAllocString(pchFilter);
		CostumeUI_UpdateUnlockedCostumeParts();
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaFilteredUnlockedCostumeParts, parse_UnlockedCostumePart);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(UnlockedCostumePart_GetRegion);
const char *CostumeCreator_UnlockedCostumePart_GetRegion(ExprContext *pContext, SA_PARAM_NN_VALID UnlockedCostumePart *pUnlocked)
{
	PCBoneDef *pBone = GET_REF(pUnlocked->hBone);
	PCRegion *pRegion = SAFE_GET_REF(pBone, hRegion);
	COSTUME_UI_TRACE_FUNC();
	return !pRegion ? "" : pRegion->pcName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(UnlockedCostumePart_GetBoneName);
const char *CostumeCreator_UnlockedCostumePart_GetBoneName(ExprContext *pContext, SA_PARAM_NN_VALID UnlockedCostumePart *pUnlocked)
{
	PCBoneDef *pBone = GET_REF(pUnlocked->hBone);
	COSTUME_UI_TRACE_FUNC();
	return !pBone ? "" : pUnlocked->bBoth ? TranslateDisplayMessage(pBone->mergeNameMsg) : TranslateDisplayMessage(pBone->displayNameMsg);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(UnlockedCostumePart_GetRegionName);
const char *CostumeCreator_UnlockedCostumePart_GetRegionName(ExprContext *pContext, SA_PARAM_NN_VALID UnlockedCostumePart *pUnlocked)
{
	PCRegion *pRegion = GET_REF(pUnlocked->hRegion);
	COSTUME_UI_TRACE_FUNC();
	return !pRegion ? "" : TranslateDisplayMessage(pRegion->displayNameMsg);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(UnlockedCostumePart_GetUnlockedGeometryName);
const char *CostumeCreator_UnlockedCostumePart_GetUnlockedGeometryName(ExprContext *pContext, SA_PARAM_NN_VALID UnlockedCostumePart *pUnlocked)
{
	PCGeometryDef *pGeometry = GET_REF(pUnlocked->hUnlockedGeometry);
	COSTUME_UI_TRACE_FUNC();
	return !pGeometry ? "" : TranslateDisplayMessage(pGeometry->displayNameMsg);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(UnlockedCostumePart_GetUnlockedMaterialName);
const char *CostumeCreator_UnlockedCostumePart_GetUnlockedMaterialName(ExprContext *pContext, SA_PARAM_NN_VALID UnlockedCostumePart *pUnlocked)
{
	PCMaterialDef *pMaterial = GET_REF(pUnlocked->hUnlockedMaterial);
	COSTUME_UI_TRACE_FUNC();
	return !pMaterial ? "" : TranslateDisplayMessage(pMaterial->displayNameMsg);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(UnlockedCostumePart_GetUnlockedTextureName);
const char *CostumeCreator_UnlockedCostumePart_GetUnlockedTextureName(ExprContext *pContext, SA_PARAM_NN_VALID UnlockedCostumePart *pUnlocked, int iTexture)
{
	PCTextureDef *pTexture;
	COSTUME_UI_TRACE_FUNC();
	switch (iTexture)
	{
		xcase 0:
		pTexture = GET_REF(pUnlocked->hUnlockedPatternTexture);
		xcase 1:
		pTexture = GET_REF(pUnlocked->hUnlockedDetailTexture);
		xcase 2:
		pTexture = GET_REF(pUnlocked->hUnlockedSpecularTexture);
		xcase 3:
		pTexture = GET_REF(pUnlocked->hUnlockedDiffuseTexture);
		xcase 4:
		pTexture = GET_REF(pUnlocked->hUnlockedMovableTexture);
		default:
		return "";
	}
	return !pTexture ? "" : TranslateDisplayMessage(pTexture->displayNameMsg);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(UnlockedCostumePart_FormatCategories2);
const char *CostumeCreator_UnlockedCostumePart_FormatCategories2(ExprContext *pContext, SA_PARAM_NN_VALID UnlockedCostumePart *pUnlocked, ACMD_EXPR_DICT(Message) const char *pchIncludedFormatKey, ACMD_EXPR_DICT(Message) const char *pchExcludedFormatKey, ACMD_EXPR_DICT(Message) const char *pchUnavailableFormatKey, const char *pchDefault)
{
	bool bDisplayUnavailable = pchUnavailableFormatKey && *pchUnavailableFormatKey && eaSize(&pUnlocked->eaUnavailableCategories) > 0;
	char *estrResult = NULL;
	const char *pchResult;
	int i;
	PCCategory *pCategory;
	COSTUME_UI_TRACE_FUNC();

	estrStackCreate(&estrResult);

	if (eaSize(&pUnlocked->eaExcludedCategories) > 0) {
		for (i = 0; i < eaSize(&pUnlocked->eaExcludedCategories); i++) {
			pCategory = GET_REF(pUnlocked->eaExcludedCategories[i]->hCategory);
			if (pCategory) {
				FormatGameMessageKey(&estrResult, pchExcludedFormatKey,
					STRFMT_DISPLAYMESSAGE("Category", pCategory->displayNameMsg),
					STRFMT_END);
			}
		}
	} else if (eaSize(&pUnlocked->eaIncludedCategories) > 0 || bDisplayUnavailable) {
		for (i = 0; i < eaSize(&pUnlocked->eaIncludedCategories); i++) {
			pCategory = GET_REF(pUnlocked->eaIncludedCategories[i]->hCategory);
			if (pCategory) {
				FormatGameMessageKey(&estrResult, pchIncludedFormatKey,
					STRFMT_DISPLAYMESSAGE("Category", pCategory->displayNameMsg),
					STRFMT_END);
			}
		}
		if (bDisplayUnavailable) {
			for (i = 0; i < eaSize(&pUnlocked->eaUnavailableCategories); i++) {
				pCategory = GET_REF(pUnlocked->eaUnavailableCategories[i]->hCategory);
				if (pCategory) {
					FormatGameMessageKey(&estrResult, pchUnavailableFormatKey,
						STRFMT_DISPLAYMESSAGE("Category", pCategory->displayNameMsg),
						STRFMT_END);
				}
			}
		}
	} else {
		estrAppend2(&estrResult, pchDefault);
	}

	pchResult = exprContextAllocString(pContext, estrResult);
	estrDestroy(&estrResult);

	return pchResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(UnlockedCostumePart_FormatCategories);
const char *CostumeCreator_UnlockedCostumePart_FormatCategories(ExprContext *pContext, SA_PARAM_NN_VALID UnlockedCostumePart *pUnlocked, ACMD_EXPR_DICT(Message) const char *pchIncludedFormatKey, ACMD_EXPR_DICT(Message) const char *pchExcludedFormatKey, const char *pchDefault)
{
	COSTUME_UI_TRACE_FUNC();
	return CostumeCreator_UnlockedCostumePart_FormatCategories2(pContext, pUnlocked, pchIncludedFormatKey, pchExcludedFormatKey, "", pchDefault);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(UnlockedCostumePart_SetLocationFormat2);
void CostumeCreator_UnlockedCostumePart_SetLocationFormat2(ExprContext *pContext, const char *pchMaterial, const char *pchPattern, const char *pchDetail, const char *pchSpecular, const char *pchDiffuse, const char *pchMovable)
{
	COSTUME_UI_TRACE_FUNC();
	s_pchUnlockedMaterialMessage = pchMaterial && *pchMaterial ? allocFindString(pchMaterial) : NULL;
	s_pchUnlockedPatternMessage = pchPattern && *pchPattern ? allocFindString(pchPattern) : NULL;
	s_pchUnlockedDetailMessage = pchDetail && *pchDetail ? allocFindString(pchDetail) : NULL;
	s_pchUnlockedSpecularMessage = pchSpecular && *pchSpecular ? allocFindString(pchSpecular) : NULL;
	s_pchUnlockedDiffuseMessage = pchDiffuse && *pchDiffuse ? allocFindString(pchDiffuse) : NULL;
	s_pchUnlockedMovableMessage = pchMovable && *pchMovable ? allocFindString(pchMovable) : NULL;

	// Rebuild the unlocked costume parts
	if (g_pCostumeView) {
		CostumeUI_UpdateUnlockedCostumeParts();
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(UnlockedCostumePart_SetLocationFormat);
void CostumeCreator_UnlockedCostumePart_SetLocationFormat(ExprContext *pContext, const char *pchMaterial, const char *pchPattern, const char *pchDetail, const char *pchSpecular, const char *pchDiffuse)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeCreator_UnlockedCostumePart_SetLocationFormat2(pContext, pchMaterial, pchPattern, pchDetail, pchSpecular, pchDiffuse, NULL);
}

PCCategory *PickBestCategoryForGeo(SA_PARAM_NN_VALID NOCONST(PlayerCostume) *pCostume, SA_PARAM_NN_VALID PCGeometryDef *pGeometry, SA_PARAM_NN_VALID PCRegion *pRegion, SA_PARAM_NN_VALID PCCategory *pCurrent, SA_PARAM_NN_VALID PCCategory **eaValidCategories)
{
	PCCategory *pCat, *pBestCategory = NULL;
	int i, j, k;
	PCGeometryDef **eaGeos = NULL;
	S32 iBestCount = 10000, iCurrent;
	S32 iAddCount = 10000, iAddCurrent;
	PCCategory **eaCats = NULL;
	const char *pcNone = allocFindString("None");

	for (i=eaSize(&pGeometry->eaCategories)-1; i>=0; --i) {
		if (GET_REF(pGeometry->eaCategories[i]->hCategory) == pCurrent) {
			// Current category matches, it is the optimal category
			return pCurrent;
		}
	}

	// Get the geo's categories, sorted by display name
	for (i=eaSize(&pGeometry->eaCategories)-1; i>=0; --i) {
		pCat = GET_REF(pGeometry->eaCategories[i]->hCategory);
		if (pCat) {
			eaPush(&eaCats, pCat);
		}
	}
	costumeTailor_SortCategories(eaCats, true);

	// Figure out which category will break the fewest geos
	for(i=eaSize(&pCostume->eaParts)-1; i>=0; --i) {
		PCBoneDef *pPartBone = GET_REF(pCostume->eaParts[i]->hBoneDef);
		if (pPartBone && GET_REF(pPartBone->hRegion) == pRegion && GET_REF(pCostume->eaParts[i]->hGeoDef) && REF_STRING_FROM_HANDLE(pCostume->eaParts[i]->hGeoDef) != pcNone && pPartBone != GET_REF(pGeometry->hBone)) {
			eaPush(&eaGeos, GET_REF(pCostume->eaParts[i]->hGeoDef));
		}
	}

	// Maximize the used geos, Minimize the added geos
	for (i=eaSize(&eaCats)-1; i>=0; --i) {
		pCat = eaCats[i];

		iCurrent = 0;
		for (j=eaSize(&eaGeos)-1; j>=0; j--) {
			for (k=eaSize(&eaGeos[j]->eaCategories)-1; k>=0; k--) {
				if (GET_REF(eaGeos[j]->eaCategories[k]->hCategory) == pCat) {
					break;
				}
			}
			if (k < 0) {
				iCurrent++;
			}
		}

		iAddCurrent = 0;
		for (j=eaSize(&pCat->eaRequiredBones)-1; j>=0; j--) {
			for (k=eaSize(&pCurrent->eaRequiredBones)-1; k>=0; k--) {
				if (GET_REF(pCat->eaRequiredBones[j]->hBone) == GET_REF(pCurrent->eaRequiredBones[k]->hBone)) {
					break;
				}
			}
			if (k < 0 && GET_REF(pGeometry->hBone) != GET_REF(pCat->eaRequiredBones[j]->hBone)) {
				// The new category has a required bone that
				// isn't required by the old category, and the
				// new geo doesn't go on that bone.
				iAddCurrent++;
			}
		}

		if (iCurrent * iCurrent + iAddCurrent * iAddCurrent <= iBestCount * iBestCount + iAddCount * iAddCount) {
			pBestCategory = pCat;
			iBestCount = iCurrent;
			iAddCount = iAddCurrent;
		}
	}
	eaDestroy(&eaGeos);

	if (pBestCategory) {
		// There was a category that won't change all the geos
		eaDestroy(&eaCats);
		return pBestCategory;
	}

	// Default to the first category that appears in the valid category list
	// i.e. if eaCats is sorted by display order, then it'll pick the first
	// category that would be displayed.
	for (j=0; j<eaSize(&eaValidCategories); j++) {
		pCat = eaGet(&eaValidCategories, j);
		if (pCat && eaFind(&eaCats, pCat) >= 0) {
			eaDestroy(&eaCats);
			return pCat;
		}
	}

	// Geo can't be used
	eaDestroy(&eaCats);
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_GetApplyCostumeUnlockWarning);
const char *CostumeCreator_GetApplyCostumeUnlockWarning(ExprContext *pContext, SA_PARAM_NN_VALID UnlockedCostumePart *pUnlock)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCBoneDef *pBone = GET_REF(pUnlock->hBone);
	PCGeometryDef *pGeometry = GET_REF(pUnlock->hUnlockedGeometry);
	PCRegion *pRegion = GET_REF(pUnlock->hRegion);
	NOCONST(PCPart) *pPart;
	PCCategory *pCategory;
	PCCategory **eaCats = NULL;
	PCCategory *pBestCategory = NULL;
	char *estrWarning = NULL;
	const char *pchWarningResult = "";
	COSTUME_UI_TRACE_FUNC();

	if (!pBone || !pGeometry || !pRegion || !g_CostumeEditState.pCostume) {
		return "";
	}

	pCategory = costumeTailor_GetCategoryForRegion(g_CostumeEditState.pConstCostume, pRegion);
	if (!pCategory) {
		return "";
	}

	pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL);
	if (!pPart) {
		return false;
	}

	// Get a list of all the categories that are available for the region
	costumeTailor_GetValidCategories(g_CostumeEditState.pCostume, pRegion, pSpecies, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, &eaCats, CGVF_OMIT_EMPTY | CGVF_SORT_DISPLAY | (g_CostumeEditState.bUnlockAll ? CGVF_UNLOCK_ALL : 0));
	pBestCategory = PickBestCategoryForGeo(g_CostumeEditState.pCostume, pGeometry, pRegion, pCategory, eaCats);
	eaDestroy(&eaCats);

	if (!pBestCategory || pBestCategory == pCategory) {
		// All categories the unlock is in are disallowed
		// Or it has no categories
		return "";
	}

	estrStackCreate(&estrWarning);
	FormatGameMessageKey(&estrWarning, "CostumeUI_Unlock_CategoryChangeWarning",
		STRFMT_DISPLAYMESSAGE("Bone", pBone->displayNameMsg),
		STRFMT_STRING("Part", pUnlock->pchName),
		STRFMT_DISPLAYMESSAGE("Region", pRegion->displayNameMsg),
		STRFMT_DISPLAYMESSAGE("OldCategory", pCategory->displayNameMsg),
		STRFMT_DISPLAYMESSAGE("NewCategory", pBestCategory->displayNameMsg),
		STRFMT_END);
	pchWarningResult = exprContextAllocString(pContext, estrWarning);
	estrDestroy(&estrWarning);

	return pchWarningResult;
}

bool CostumeCreator_ApplyCostumeUnlockToCostume(SA_PARAM_OP_VALID NOCONST(PlayerCostume) *pCostume, SA_PARAM_NN_VALID UnlockedCostumePart *pUnlock, PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCSlotType *pSlotType, bool bUnlockAll)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCBoneDef *pBone = GET_REF(pUnlock->hBone);
	PCGeometryDef *pGeometry = GET_REF(pUnlock->hUnlockedGeometry);
	PCRegion *pRegion = GET_REF(pUnlock->hRegion);
	PCCategory *pCategory;
	int i, j;
	PCCategory *pBestCategory = NULL;
	NOCONST(PCPart) *pPart, *pMirrorPart = NULL;
	PCCategory **eaCats = NULL;
	Entity *pEnt = entActivePlayerPtr();
	PCBoneDef *pParentBone = NULL;
	NOCONST(PCPart) *pParentPart = NULL;
	PCSkeletonDef *pSkel = GET_REF(pUnlock->hSkeleton);
	bool bSetMirrorPart = pUnlock->bBoth;

	if (!pBone || !pGeometry || !pRegion || !pCostume || !pSkel) {
		return false;
	}

	pCategory = costumeTailor_GetCategoryForRegion(CONTAINER_RECONST(PlayerCostume, pCostume), pRegion);
	if (!pCategory) {
		return false;
	}

	pPart = costumeTailor_GetPartByBone(pCostume, pBone, NULL);
	if (!pPart) {
		return false;
	}

	// Pick the best valid category
	costumeTailor_GetValidCategories(pCostume, pRegion, pSpecies, eaUnlockedCostumes, eaPowerFXBones, pSlotType, &eaCats, CGVF_OMIT_EMPTY | CGVF_SORT_DISPLAY | (bUnlockAll ? CGVF_UNLOCK_ALL : 0));
	pBestCategory = PickBestCategoryForGeo(pCostume, pGeometry, pRegion, pCategory, eaCats);
	eaDestroy(&eaCats);

	if (!pBestCategory) {
		// All categories the unlock is in are disallowed
		// Or it has no categories
		return false;
	}

	if (pCategory != pBestCategory) {
		// Update the category
		costumeTailor_SetRegionCategory(pCostume, pRegion, pBestCategory);
		if (GET_REF(g_CostumeEditState.hRegion) == pRegion) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, pBestCategory, g_CostumeEditState.hCategory);
		}

		// Need to revalidate parts in the current region against this choice
		for(i=eaSize(&pCostume->eaParts)-1; i>=0; --i) {
			PCBoneDef *pPartBone = GET_REF(pCostume->eaParts[i]->hBoneDef);
			if (pPartBone && !pPartBone->bIsChildBone && pPartBone != pBone && GET_REF(pPartBone->hRegion) == pRegion) {
				costumeTailor_PickValidPartValues(pCostume, pCostume->eaParts[i], pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, guild_GetGuild(pEnt));
			}
		}
	}

	if (pBone->bIsChildBone) {
		PCGeometryDef **eaChildGeos = NULL;
		PCGeometryDef **eaParentGeos = NULL;
		PCGeometryDef *pBestParentGeo = NULL;
		PCGeometryDef *pUnlockedGeo = GET_REF(pUnlock->hUnlockedGeometry);
		PCGeometryDef *pParentGeo;

		for(i=eaSize(&pCostume->eaParts)-1; i>=0; --i) {
			PCBoneDef *pPartBone = GET_REF(pCostume->eaParts[i]->hBoneDef);
			if (pPartBone && !pPartBone->bIsChildBone && GET_REF(pPartBone->hRegion) == pRegion) {
				for (j=eaSize(&pPartBone->eaChildBones)-1; j>=0; --j) {
					if (GET_REF(pPartBone->eaChildBones[j]->hChildBone) == pBone) {
						pParentBone = pPartBone;
						pParentPart = pCostume->eaParts[i];
						break;
					}
				}
			}
			if (pParentBone) {
				break;
			}
		}

		if (!pParentPart || !GET_REF(pParentPart->hGeoDef) || !pUnlockedGeo) {
			return false;
		}

		costumeTailor_GetValidGeos(pCostume, pSkel, pParentBone, pBestCategory, pSpecies, eaUnlockedCostumes, &eaParentGeos, false, false, true, bUnlockAll);

		for (i=eaSize(&eaParentGeos)-1; i>=0; --i) {
			pParentGeo = eaParentGeos[i];
			eaClearFast(&eaChildGeos);
			costumeTailor_GetValidChildGeos(pCostume, pBestCategory, pParentGeo, pBone, pSpecies, eaUnlockedCostumes, &eaChildGeos, true, bUnlockAll);
			if (eaFind(&eaChildGeos, GET_REF(pUnlock->hUnlockedGeometry)) >= 0) {
				// Find first valid geo or the same geo
				pBestParentGeo = pParentGeo;
				if (pParentGeo == GET_REF(pParentPart->hGeoDef)) {
					// Geo currently in use
					break;
				}
			}
		}

		if (pBestParentGeo) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, pBestParentGeo, pParentPart->hGeoDef);
			costumeTailor_PickValidPartValues(pCostume, pParentPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, guild_GetGuild(pEnt));
		} else {
			return false;
		}

		// Check the mirror
		if (bSetMirrorPart && GET_REF(pParentBone->hMirrorBone)) {
			pBestParentGeo = NULL;
			pParentBone = GET_REF(pParentBone->hMirrorBone);
			pParentPart = costumeTailor_GetMirrorPart(pCostume, pParentPart);

			if (pParentPart && GET_REF(pParentPart->hGeoDef)) {
				eaClearFast(&eaParentGeos);
				costumeTailor_GetValidGeos(pCostume, pSkel, pParentBone, pBestCategory, pSpecies, eaUnlockedCostumes, &eaParentGeos, false, false, true, bUnlockAll);

				for (i=eaSize(&eaParentGeos)-1; i>=0; --i) {
					pParentGeo = eaParentGeos[i];
					eaClearFast(&eaChildGeos);
					costumeTailor_GetValidChildGeos(pCostume, pBestCategory, pParentGeo, pBone, pSpecies, eaUnlockedCostumes, &eaChildGeos, true, true);
					for (j=eaSize(&eaChildGeos)-1; j>=0; j--) {
						if ((pUnlockedGeo == eaChildGeos[i]) || (pUnlockedGeo->pcMirrorGeometry && (stricmp(pUnlockedGeo->pcMirrorGeometry, eaChildGeos[i]->pcName) == 0))) {
							// Found perfect match so done
							break;
						} else if (stricmp(TranslateDisplayMessage(pUnlockedGeo->displayNameMsg), TranslateDisplayMessage(eaChildGeos[i]->displayNameMsg)) == 0) {
							// Candidate but continue looking for perfect one
							break;
						}
					}
					if (j >= 0) {
						// Find first valid geo or matching
						pBestParentGeo = pParentGeo;
						if (pParentGeo == GET_REF(pParentPart->hGeoDef)) {
							// Geo currently in use
							break;
						}
					}
				}

				if (pBestParentGeo) {
					SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, pBestParentGeo, pParentPart->hGeoDef);
					costumeTailor_PickValidPartValues(pCostume, pParentPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, guild_GetGuild(pEnt));
				} else {
					bSetMirrorPart = false;
				}
			} else {
				bSetMirrorPart = false;
			}
		}
	}

	// Set new part values
	COPY_HANDLE(pPart->hGeoDef, pUnlock->hUnlockedGeometry);
	COPY_HANDLE(pPart->hMatDef, pUnlock->hUnlockedMaterial);
	COPY_HANDLE(pPart->hPatternTexture, pUnlock->hUnlockedPatternTexture);
	COPY_HANDLE(pPart->hDetailTexture, pUnlock->hUnlockedDetailTexture);
	COPY_HANDLE(pPart->hSpecularTexture, pUnlock->hUnlockedSpecularTexture);
	COPY_HANDLE(pPart->hDiffuseTexture, pUnlock->hUnlockedDiffuseTexture);
	if (IS_HANDLE_ACTIVE(pUnlock->hUnlockedMovableTexture)) {
		if (!pPart->pMovableTexture) {
			pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
		}
		COPY_HANDLE(pPart->pMovableTexture->hMovableTexture, pUnlock->hUnlockedMovableTexture);
	} else if (pPart->pMovableTexture) {
		REMOVE_HANDLE(pPart->pMovableTexture->hMovableTexture);
	}

	if (bSetMirrorPart) {
		pMirrorPart = costumeTailor_GetMirrorPart(pCostume, pPart);
		costumeTailor_CopyToMirrorPart(pCostume, pPart, pMirrorPart, pSpecies,
			(pUnlock->bGeoUnlock ? EDIT_FLAG_GEOMETRY : 0) |
			(pUnlock->bMatUnlock ? EDIT_FLAG_GEOMETRY | EDIT_FLAG_MATERIAL : 0) |
			(pUnlock->bTexPatternUnlock ? EDIT_FLAG_GEOMETRY | EDIT_FLAG_MATERIAL | EDIT_FLAG_TEXTURE1 : 0) |
			(pUnlock->bTexDetailUnlock ? EDIT_FLAG_GEOMETRY | EDIT_FLAG_MATERIAL | EDIT_FLAG_TEXTURE2 : 0) |
			(pUnlock->bTexSpecularUnlock ? EDIT_FLAG_GEOMETRY | EDIT_FLAG_MATERIAL | EDIT_FLAG_TEXTURE3 : 0) |
			(pUnlock->bTexDiffuseUnlock ? EDIT_FLAG_GEOMETRY | EDIT_FLAG_MATERIAL | EDIT_FLAG_TEXTURE4 : 0) |
			(pUnlock->bTexMovableUnlock ? EDIT_FLAG_GEOMETRY | EDIT_FLAG_MATERIAL | EDIT_FLAG_TEXTURE5 : 0),
			eaUnlockedCostumes, false, bUnlockAll, true);
	}

	// Make part valid
	costumeTailor_PickValidPartValues(pCostume, pPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, guild_GetGuild(pEnt));
	if (pMirrorPart)
		costumeTailor_PickValidPartValues(pCostume, pMirrorPart, pSpecies, pSlotType, eaUnlockedCostumes, true, bUnlockAll, true, false, guild_GetGuild(pEnt));

	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetCostumeUnlock);
bool CostumeCreator_SetCostumeUnlock(ExprContext *pContext, SA_PARAM_NN_VALID UnlockedCostumePart *pUnlock)
{
	COSTUME_UI_TRACE_FUNC();
	if (CostumeCreator_ApplyCostumeUnlockToCostume(g_CostumeEditState.pCostume, pUnlock, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, false)) {
		CostumeUI_RegenCostume(true);
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetHoverCostumeUnlock);
bool CostumeCreator_SetHoverCostumeUnlock(ExprContext *pContext, SA_PARAM_NN_VALID UnlockedCostumePart *pUnlock)
{
	COSTUME_UI_TRACE_FUNC();
	if (!g_CostumeEditState.pHoverCostume) {
		g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
	} else {
		StructCopyAllNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume, g_CostumeEditState.pHoverCostume);
	}
	if (CostumeCreator_ApplyCostumeUnlockToCostume(g_CostumeEditState.pHoverCostume, pUnlock, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, false)) {
		CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), NULL, g_CostumeEditState.eaShowItems);
		return true;
	}
	return false;
}

// Get the number of MicroTransaction products will provide access to a specific UnlockMetaData
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeUnlockInfo_GetProductListSize);
S32 CostumeUnlockInfoExpr_GetProductListSize(SA_PARAM_OP_VALID UnlockMetaData *pUnlockInfo)
{
	COSTUME_UI_TRACE_FUNC();
	if (pUnlockInfo) {
		return eaSize(&pUnlockInfo->eaFullProductList);
	}
	return 0;
}

// Get the list of MicroTransaction products that provide access to a specific UnlockMetaData
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeUnlockInfo_GetProductList);
S32 CostumeUnlockInfoExpr_GetProductList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID UnlockMetaData *pUnlockInfo)
{
	COSTUME_UI_TRACE_FUNC();
	if (pUnlockInfo) {
		ui_GenSetList(pGen, &pUnlockInfo->eaFullProductList, parse_MicroTransactionUIProduct);
		return eaSize(&pUnlockInfo->eaFullProductList);
	}
	return 0;
}

// Set the "default" MicroTransaction product for the UnlockMetaData
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeUnlockInfo_SetDefaultProduct);
bool CostumeUnlockInfoExpr_SetDefaultProduct(SA_PARAM_OP_VALID UnlockMetaData *pUnlockInfo, U32 uMicroTransactionID)
{
	S32 i;
	COSTUME_UI_TRACE_FUNC();
	if (pUnlockInfo) {
		for (i=eaSize(&pUnlockInfo->eaFullProductList)-1; i>=0; i--) {
			if (pUnlockInfo->eaFullProductList[i]->uID == uMicroTransactionID) {
				// Clear the old purchase product
				StructDestroySafe(parse_MicroTransactionUIProduct, &pUnlockInfo->pProduct);

				// Set the new purchase product
				pUnlockInfo->uMicroTransactionID = uMicroTransactionID;
				pUnlockInfo->pProduct = gclMicroTrans_MakeUIProduct(pUnlockInfo->uMicroTransactionID);
				return true;
			}
		}
	}
	return false;
}

static S32 CostumeUI_MTGetUnlockedCostumeParts(SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID MicroTransactionProduct *pProduct)
{
	S32 iParts = 0;
	UnlockedCostumePart ***peaUnlocked = pGen ? ui_GenGetManagedListSafe(pGen, UnlockedCostumePart) : NULL;

	if (g_pMTCostumes && pProduct) {
		S32 iCostume;
		for (iCostume = 0; iCostume < eaSize(&g_pMTCostumes->ppCostumes); iCostume++) {
			MicroTransactionCostume *pCostume = g_pMTCostumes->ppCostumes[iCostume];
			if (eaIndexedFindUsingInt(&pCostume->eaSources, pProduct->uID) < 0) {
				continue;
			}

			if (GET_REF(pCostume->hCostume)) {
				iParts = CostumeUI_AddUnlockedCostumeParts(peaUnlocked, iParts, GET_REF(pCostume->hCostume), NULL, NULL, NULL, NULL, true);
			}
		}
	}

	if (peaUnlocked) {
		eaSetSizeStruct(peaUnlocked, parse_UnlockedCostumePart, iParts);
	}
	if (pGen)
		ui_GenSetManagedListSafe(pGen, peaUnlocked, UnlockedCostumePart, true);

	return iParts;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetMicroTransUnlockedCostumeParts);
void MicroTrans_GetUnlockedCostumeParts(SA_PARAM_NN_VALID UIGen *pGen, U32 uMicroTransactionID)
{
	MicroTransactionProduct *pProduct = NULL;
	COSTUME_UI_TRACE_FUNC();

	if (g_pMTList) {
		pProduct = eaIndexedGetUsingInt(&g_pMTList->ppProducts, uMicroTransactionID);
	}

	CostumeUI_MTGetUnlockedCostumeParts(pGen, pProduct);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetMicroTransUnlockedCostumePartsByName);
void MicroTrans_GetUnlockedCostumePartsByName(SA_PARAM_NN_VALID UIGen *pGen, const char *pchName)
{
	MicroTransactionProduct *pProduct = NULL;
	COSTUME_UI_TRACE_FUNC();

	if (g_pMTList) {
		S32 i;
		pchName = pchName && *pchName ? allocFindString(pchName) : NULL;
		for (i = 0; pchName && i < eaSize(&g_pMTList->ppProducts); i++) {
			if (REF_STRING_FROM_HANDLE(g_pMTList->ppProducts[i]->hDef) == pchName) {
				pProduct = g_pMTList->ppProducts[i];
				break;
			}
		}
	}

	CostumeUI_MTGetUnlockedCostumeParts(pGen, pProduct);
}

#include "AutoGen/gclCostumeUnlockUI_h_ast.c"
