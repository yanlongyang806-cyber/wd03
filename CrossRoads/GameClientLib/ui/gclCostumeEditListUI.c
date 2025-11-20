#include "gclCostumeUIState.h"

#include "StringUtil.h"
#include "StringCache.h"
#include "UIGen.h"
#include "GfxTexturesPublic.h"
#include "GfxHeadshot.h"
#include "GraphicsLib.h"
#include "CostumeCommonGenerate.h"
#include "wlCostume.h"
#include "dynSequencer.h"
#include "gclCostumeUIState.h"
#include "GameClientLib.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "gclCostumeUI.h"
#include "gclUIGenPaperdoll.h"
#include "gclCostumeView.h"
#include "gclMicroTransactions.h"
#include "MicroTransactionUI.h"
#include "fileutil.h"
#include "gclCostumeUnlockUI.h"

#include "AutoGen/gclCostumeUIState_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define DEFAULT_LIST "Default"

static CostumeSourceList **s_eaCostumeSources;

//////////////////////////////////////////////////////////////////////////
// Utility functions

static void CostumeEditList_UpdateCostumeSource(SA_PARAM_NN_VALID CostumeSourceList *pList, SA_PARAM_NN_VALID CostumeSource *pSource, bool bDeep)
{
	// Enforce costume list limit
	if (pList->iMaxSize > 0)
	{
		while (eaSize(&pList->eaCostumes) >= pList->iMaxSize)
		{
			StructDestroy(parse_CostumeSource, eaRemove(&pList->eaCostumes, 0));
		}
	}
}

CostumeSourceList *CostumeEditList_GetSourceList(const char *pcName, bool bCreate)
{
	CostumeSourceList *pList;
	if (!pcName || !*pcName)
		pcName = DEFAULT_LIST;
	pList = eaIndexedGetUsingString(&s_eaCostumeSources, pcName);
	if (pList || !bCreate)
		return pList;

	if (!s_eaCostumeSources)
	{
		eaCreate(&s_eaCostumeSources);
		eaIndexedEnable(&s_eaCostumeSources, parse_CostumeSourceList);
	}

	pList = StructCreate(parse_CostumeSourceList);
	pList->pcName = allocAddString(pcName);
	eaPush(&s_eaCostumeSources, pList);
	return pList;
}

CostumeSource *CostumeEditList_GetCostume(const char *pcSourceList, const char *pcCostumeName)
{
	CostumeSourceList *pList = CostumeEditList_GetSourceList(pcSourceList, false);
	int i;
	if (!pList)
		return NULL;
	for (i = 0; i < eaSize(&pList->eaCostumes); i++)
	{
		if (!stricmp_safe(pList->eaCostumes[i]->pcTagName, pcCostumeName))
		{
			return pList->eaCostumes[i];
		}
	}
	return NULL;
}

CostumeSource *CostumeEditList_GetCostumeByIndex(const char *pcSourceList, S32 iIndex)
{
	CostumeSourceList *pList = CostumeEditList_GetSourceList(pcSourceList, false);
	if (!pList || iIndex < 0 || iIndex >= eaSize(&pList->eaCostumes))
		return NULL;
	return pList->eaCostumes[iIndex];
}

CostumeSource *CostumeEditList_GetCostumeBySpeciesAndSkeleton(const char *pcSourceList, SpeciesDef *pSpecies, PCSkeletonDef *pSkeleton)
{
	CostumeSourceList *pList = CostumeEditList_GetSourceList(pcSourceList, false);
	int i;
	if (!pList)
		return NULL;
	for (i = 0; i < eaSize(&pList->eaCostumes); i++)
	{
		PlayerCostume *pCostume = pList->eaCostumes[i]->pCostume;
		if (!pCostume)
			pCostume = GET_REF(pList->eaCostumes[i]->hPlayerCostume);

		if (pCostume && GET_REF(pCostume->hSpecies) == pSpecies && GET_REF(pCostume->hSkeleton) == pSkeleton)
		{
			return pList->eaCostumes[i];
		}
	}
	return NULL;
}

CostumeSource *CostumeEditList_AddCostume(const char *pcSourceList, PlayerCostume *pCostume)
{
	CostumeSourceList *pList = CostumeEditList_GetSourceList(pcSourceList, true);
	CostumeSource *pSource = StructCreate(parse_CostumeSource);
	pSource->pCostume = StructClone(parse_PlayerCostume, pCostume);
	eaPush(&pList->eaCostumes, pSource);
	CostumeEditList_UpdateCostumeSource(pList, pSource, true);
	return pSource;
}

CostumeSource *CostumeEditList_AddCostumeRef(const char *pcSourceList, const char *pcCostumeName)
{
	CostumeSourceList *pList = CostumeEditList_GetSourceList(pcSourceList, true);
	CostumeSource *pSource = StructCreate(parse_CostumeSource);
	SET_HANDLE_FROM_STRING("PlayerCostume", pcCostumeName, pSource->hPlayerCostume);
	eaPush(&pList->eaCostumes, pSource);
	CostumeEditList_UpdateCostumeSource(pList, pSource, true);
	return pSource;
}

CostumeSource *CostumeEditList_AddNamedCostume(const char *pcSourceList, const char *pcTagName, PlayerCostume *pCostume)
{
	CostumeSourceList *pList = CostumeEditList_GetSourceList(pcSourceList, true);
	CostumeSource *pSource = NULL;
	int i;

	// Find existing costume
	for (i = 0; i < eaSize(&pList->eaCostumes); i++)
	{
		if (!stricmp_safe(pcTagName, pList->eaCostumes[i]->pcTagName))
		{
			pSource = pList->eaCostumes[i];
			break;
		}
	}

	// Make new costume
	if (!pSource)
	{
		pSource = StructCreate(parse_CostumeSource);
		pSource->pcTagName = StructAllocString(pcTagName);
		eaPush(&pList->eaCostumes, pSource);
	}

	// Release old costume
	StructDestroySafe(parse_PlayerCostume, &pSource->pCostume);
	REMOVE_HANDLE(pSource->hPlayerCostume);

	// Set new costume
	pSource->pCostume = StructClone(parse_PlayerCostume, pCostume);
	CostumeEditList_UpdateCostumeSource(pList, pSource, true);
	return pSource;
}

CostumeSource *CostumeEditList_AddNamedCostumeRef(const char *pcSourceList, const char *pcTagName, const char *pcCostumeName)
{
	CostumeSourceList *pList = CostumeEditList_GetSourceList(pcSourceList, true);
	CostumeSource *pSource = NULL;
	int i;

	// Find existing costume
	for (i = 0; i < eaSize(&pList->eaCostumes); i++)
	{
		if (!stricmp_safe(pcTagName, pList->eaCostumes[i]->pcTagName))
		{
			pSource = pList->eaCostumes[i];
			break;
		}
	}

	// Make new costume
	if (!pSource)
	{
		pSource = StructCreate(parse_CostumeSource);
		pSource->pcTagName = StructAllocString(pcTagName);
		eaPush(&pList->eaCostumes, pSource);
	}

	// Release old costume
	StructDestroySafe(parse_PlayerCostume, &pSource->pCostume);
	REMOVE_HANDLE(pSource->hPlayerCostume);

	// Set new costume
	SET_HANDLE_FROM_STRING("PlayerCostume", pcCostumeName, pSource->hPlayerCostume);
	CostumeEditList_UpdateCostumeSource(pList, pSource, true);
	return pSource;
}

void CostumeEditList_ClearCostumeSourceList(const char *pcSourceList, bool bForce)
{
	int i;
	if (pcSourceList)
	{
		CostumeSourceList *pList = CostumeEditList_GetSourceList(pcSourceList, false);
		if (pList)
		{
			eaFindAndRemove(&s_eaCostumeSources, pList);
			StructDestroy(parse_CostumeSourceList, pList);
		}
	}
	else
	{
		for (i = eaSize(&s_eaCostumeSources) - 1; i >= 0; i--)
		{
			CostumeSourceList *pList = s_eaCostumeSources[i];
			if (bForce || !(pList->eFlags & kCostumeSourceFlag_PersistOnCostumeExit))
			{
				eaRemove(&s_eaCostumeSources, i);
				StructDestroy(parse_CostumeSourceList, pList);
			}
		}
	}
}

static void CostumeEditList_SanitizeCostume(NOCONST(PlayerCostume) *pCostume)
{
	pCostume->pcName = NULL;
	pCostume->pcFileName = NULL;
	StructDestroyNoConstSafe(parse_PCArtistCostumeData, &pCostume->pArtistData);
	pCostume->eCostumeType = kPCCostumeType_Player;
}

//////////////////////////////////////////////////////////////////////////
// Expressions that take the CostumeEditList key string.

// Add the premade costume to the costume list.
// If the Tag given is empty, it will add the costume to the end of the list.
// If the Tag is specified it will either add the costume to the end of the list, or replace the costume with the specified tag.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_LoadCostume);
void gclCostumeEditListExpr_LoadCostume(const char *pcList, const char *pcTag, const char *pcCostume)
{
	COSTUME_UI_TRACE_FUNC();
	if (pcTag && *pcTag)
	{
		CostumeEditList_AddNamedCostumeRef(pcList, pcTag, pcCostume);
	}
	else
	{
		CostumeEditList_AddCostumeRef(pcList, pcCostume);
	}
}

// Add the costume creator's current costume to the costume list.
// If the Tag given is empty, it will add the costume to the end of the list. Useful for creating an undo stack.
// If the Tag is specified it will either add the costume to the end of the list, or replace the costume with the specified tag.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_PushCostume);
bool gclCostumeEditListExpr_PushCostume(const char *pcList, const char *pcTag)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.pConstCostume)
	{
		if (pcTag && *pcTag)
		{
			CostumeEditList_AddNamedCostume(pcList, pcTag, g_CostumeEditState.pConstCostume);
		}
		else
		{
			CostumeEditList_AddCostume(pcList, g_CostumeEditState.pConstCostume);
		}
		return true;
	}
	return false;
}

// Replace the costume creator's current costume with the costume at the specified position in the costume list.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_SelectCostume);
bool gclCostumeEditListExpr_SelectCostume(const char *pcList, int iIndex)
{
	CostumeSource *pSource = CostumeEditList_GetCostumeByIndex(pcList, iIndex);
	NOCONST(PlayerCostume) *pCostume = NULL;
	COSTUME_UI_TRACE_FUNC();

	if (pSource)
	{
		pCostume = CONTAINER_NOCONST(PlayerCostume, (pSource->pCostume) ? pSource->pCostume : GET_REF(pSource->hPlayerCostume));
		pCostume = StructCloneNoConst(parse_PlayerCostume, pCostume);
		if (!pSource->pCostume)
			CostumeEditList_SanitizeCostume(pCostume);
	}

	if (pCostume)
	{
		StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pCostume);

		g_CostumeEditState.pCostume = pCostume;
		REMOVE_HANDLE(g_CostumeEditState.hMood);
		CostumeUI_ClearSelections();
		CostumeUI_RegenCostume(true);

		return true;
	}

	return false;
}

// Replace the costume creator's current costume with the named costume in the costume list.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_SelectNamedCostume);
bool gclCostumeEditListExpr_SelectNamedCostume(const char *pcList, const char *pcTag)
{
	CostumeSource *pSource = CostumeEditList_GetCostume(pcList, pcTag);
	NOCONST(PlayerCostume) *pCostume = NULL;
	COSTUME_UI_TRACE_FUNC();

	if (pSource)
	{
		pCostume = CONTAINER_NOCONST(PlayerCostume, (pSource->pCostume) ? pSource->pCostume : GET_REF(pSource->hPlayerCostume));
		pCostume = StructCloneNoConst(parse_PlayerCostume, pCostume);
		if (!pSource->pCostume)
			CostumeEditList_SanitizeCostume(pCostume);
	}

	if (pCostume)
	{
		StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pCostume);

		g_CostumeEditState.pCostume = pCostume;
		REMOVE_HANDLE(g_CostumeEditState.hMood);
		CostumeUI_ClearSelections();
		CostumeUI_RegenCostume(true);

		return true;
	}

	return false;
}

// Overwrite the costume in the list that has the same skeleton & species of the costume creator's current costume.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_CacheCostume);
bool gclCostumeEditListExpr_CacheCostume(const char *pcList)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.pCostume)
	{
		CostumeSource *pSource = CostumeEditList_GetCostumeBySpeciesAndSkeleton(pcList, GET_REF(g_CostumeEditState.pCostume->hSpecies), GET_REF(g_CostumeEditState.pCostume->hSkeleton));
		if (pSource)
		{
			CostumeSourceList *pList = CostumeEditList_GetSourceList(pcList, false);
			if (pList)
			{
				StructDestroySafe(parse_PlayerCostume, &pSource->pCostume);
				REMOVE_HANDLE(pSource->hPlayerCostume);

				pSource->pCostume = StructCloneReConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
				CostumeEditList_UpdateCostumeSource(pList, pSource, true);
			}

			return true;
		}
	}

	return false;
}

// Choose a costume from the list of costumes that also has the same skeleton & species as the skeleton & species of the costume creator's current costume.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_ChooseCostume);
void gclCostumeEditListExpr_ChooseCostume(const char *pcList)
{
	CostumeSource *pSource = NULL;
	NOCONST(PlayerCostume) *pCostume = NULL;
	COSTUME_UI_TRACE_FUNC();

	if (g_CostumeEditState.pCostume)
	{
		pSource = CostumeEditList_GetCostumeBySpeciesAndSkeleton(pcList, GET_REF(g_CostumeEditState.pCostume->hSpecies), GET_REF(g_CostumeEditState.pCostume->hSkeleton));
	}
	else
	{
		// Make a costume, any costume
		CharacterCreation_BuildPlainCostume();
		if (!devassert(g_CostumeEditState.pCostume))
		{
			// probably out of memory || in some other very bad state
			return;
		}
	}

	if (pSource)
	{
		pCostume = CONTAINER_NOCONST(PlayerCostume, (pSource->pCostume) ? pSource->pCostume : GET_REF(pSource->hPlayerCostume));
		pCostume = StructCloneNoConst(parse_PlayerCostume, pCostume);
		if (!pSource->pCostume)
			CostumeEditList_SanitizeCostume(pCostume);
	}

	if (!pCostume)
	{
		pCostume = CharacterCreation_MakePlainCostumeFromSkeleton(GET_REF(g_CostumeEditState.pCostume->hSkeleton), GET_REF(g_CostumeEditState.pCostume->hSpecies));
	}

	StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);

	g_CostumeEditState.pCostume = pCostume;
	REMOVE_HANDLE(g_CostumeEditState.hMood);
	CostumeUI_ClearSelections();
	CostumeUI_RegenCostume(true);
}

// Choose a costume from the list of costumes that also has the same skeleton & species as the costume creator's currently selected skeleton & species.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_ChooseCostumeFromSelections);
void gclCostumeEditListExpr_ChooseCostumeFromSelections(const char *pcList)
{
	CostumeSource *pSource = CostumeEditList_GetCostumeBySpeciesAndSkeleton(pcList, GET_REF(g_CostumeEditState.hSpecies), GET_REF(g_CostumeEditState.hSkeleton));
	NOCONST(PlayerCostume) *pCostume = NULL;
	COSTUME_UI_TRACE_FUNC();

	if (pSource)
	{
		pCostume = CONTAINER_NOCONST(PlayerCostume, (pSource->pCostume) ? pSource->pCostume : GET_REF(pSource->hPlayerCostume));
		pCostume = StructCloneNoConst(parse_PlayerCostume, pCostume);
		if (!pSource->pCostume)
			CostumeEditList_SanitizeCostume(pCostume);
	}

	if (!pCostume)
	{
		pCostume = CharacterCreation_MakePlainCostumeFromSkeleton(GET_REF(g_CostumeEditState.hSkeleton), GET_REF(g_CostumeEditState.hSpecies));
	}

	StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);

	g_CostumeEditState.pCostume = pCostume;
	REMOVE_HANDLE(g_CostumeEditState.hMood);
	CostumeUI_ClearSelections();
	CostumeUI_RegenCostume(true);
}

// Request some premade costumes from the server.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_LoadCostumeList);
void gclCostumeEditListExpr_LoadCostumeList(const char *pcList, const char *pchCostumeNames)
{
	char *pchLocal = NULL;
	char *pchContext = NULL;
	char *pchToken;
	COSTUME_UI_TRACE_FUNC();

	strdup_alloca(pchLocal, pchCostumeNames);

	while ((pchToken = strtok_r(pchContext ? NULL : pchLocal, " ,\r\n\t", &pchContext)) != NULL)
	{
		CostumeEditList_AddCostumeRef(pcList, pchToken);
	}
}

// Add a randomized costume to the list of costumes
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_AddRandomSpeciesPreset);
bool gclCostumeEditListExpr_AddRandomPreset(const char *pcList, U32 uSeed, const char *pchSpecies, const char *pchSlotType, const char *pchPresetGroups, U32 uFlags)
{
	SpeciesDef *pSpecies = RefSystem_ReferentFromString("SpeciesDef", pchSpecies);
	PCSlotType *pSlotType = costumeLoad_GetSlotType(pchSlotType);
	NOCONST(PlayerCostume) *pRandomCostume = NULL;
	CostumeSourceList *pSource = CostumeEditList_GetSourceList(pcList, true);
	bool bEnforceUnique = !!(uFlags & 1);
	bool bNoRepeats = !!(uFlags & 2);
	CostumePreset **eaPresets = NULL;
	const char **eaUsedPresets = NULL;
	const char **eapchPreviouslyUsedOptions = NULL;
	const char **eapchGroups = NULL;
	const char **eapchRequiredGroups = NULL;
	char *pchGroup, *pchContext;
	S32 i, j, iTries = 0;
	bool bCreated = false;

	if (!pSpecies)
		return false;

	costumeTailor_GetValidPresets(g_CostumeEditState.pCostume, pSpecies, &eaPresets, false, g_CostumeEditState.eaUnlockedCostumes, false);

	// Filter out presets by slot type
	if (pchSlotType && *pchSlotType)
	{
		for (i = eaSize(&eaPresets) - 1; i >= 0; i--)
		{
			CostumePreset *pPreset = eaPresets[i];
			CostumePresetCategory *pCategory = pPreset->bOverrideExcludeValues ? NULL : GET_REF(pPreset->hPresetCategory);
			PCSlotType *pPresetSlotType = costumeLoad_GetSlotType(pPreset->pcSlotType);
			bool bExcludeSlotType = pCategory ? pCategory->bExcludeSlotType : pPreset->bExcludeSlotType;
			if (bExcludeSlotType && pPresetSlotType && pSlotType == pPresetSlotType)
			{
				eaRemove(&g_CostumeEditState.eaPresets, i);
			}
		}
	}

	strdup_alloca(pchGroup, pchPresetGroups);
	if (pchGroup = strtok_r(pchGroup, " \r\n\t,%", &pchContext))
	{
		do
		{
			bool bRequired = pchGroup[0] == '+';
			const char *pcGroup = allocFindString(bRequired ? pchGroup + 1 : pchGroup);
			if (pcGroup)
			{
				eaPushUnique(&eapchGroups, pcGroup);
				if (bRequired)
					eaPushUnique(&eapchRequiredGroups, pcGroup);
			}
		} while (pchGroup = strtok_r(NULL, " \r\n\t,%", &pchContext));
	}

	// Create set of previously used options
	for (i = eaSize(&pSource->eaCostumes) - 1; i >= 0; i--)
		eaPushEArray(&eapchPreviouslyUsedOptions, &pSource->eaCostumes[i]->eapchCostumeInputs);

	do
	{
		CostumePreset **eaGroupPresets = NULL;

		eaClearFast(&eaUsedPresets);

		// Generate a random costume
		for (i = 0; i < eaSize(&eapchGroups); i++)
		{
			S32 iRandomPreset, iRejected = 0;

			// Build pool of costume presets in the group
			eaClearFast(&eaGroupPresets);
			for (j = 0; j < eaSize(&eaPresets); j++)
			{
				CostumePreset *pSourcePreset = eaPresets[j];
				CostumePresetCategory *pCategory = pSourcePreset->bOverrideExcludeValues ? NULL : GET_REF(pSourcePreset->hPresetCategory);
				bool bExcludeGroup = pCategory ? pCategory->bExcludeGroup : pSourcePreset->bExcludeGroup;
				bool bExcludeSlotType = pCategory ? pCategory->bExcludeSlotType : pSourcePreset->bExcludeSlotType;
				const char *pcGroup = pCategory ? pCategory->pcGroup : pSourcePreset->pcGroup;
				if (!((bExcludeGroup && !bExcludeSlotType && eapchGroups[i] == pcGroup)
					|| (!bExcludeGroup && eapchGroups[i] != pcGroup))
					)
				{
					if (bNoRepeats)
					{
						char achName[256];
						sprintf(achName, "%s_%s", eapchGroups[i], pSourcePreset->pcName);
						if (eaFindString(&eapchPreviouslyUsedOptions, achName) >= 0)
						{
							iRejected++;
							continue;
						}
					}
					eaPush(&eaGroupPresets, pSourcePreset);
				}
			}

			if (!eaSize(&eaGroupPresets))
			{
				if (eaFind(&eapchRequiredGroups, eapchGroups[i]) >= 0)
				{
					StructDestroyNoConstSafe(parse_PlayerCostume, &pRandomCostume);
					break;
				}

				continue;
			}

			// Initialize random costume
			if (!pRandomCostume)
			{
				char achName[256];
				pRandomCostume = StructCreateNoConst(parse_PlayerCostume);
				sprintf(achName, "Costume_%s_%u", pSpecies->pcName, uSeed);
				pRandomCostume->pcName = allocAddString(achName);
				COPY_HANDLE(pRandomCostume->hSkeleton, pSpecies->hSkeleton);
				SET_HANDLE_FROM_REFERENT("SpeciesDef", pSpecies, pRandomCostume->hSpecies);
				costumeTailor_FillAllBones(pRandomCostume, pSpecies, g_CostumeEditState.eaPowerFXBones, pSlotType, true, false, true);
			}

			// Pick a random preset
			iRandomPreset = floor(eaSize(&eaGroupPresets) * randomPositiveF32Seeded(&uSeed, RandType_LCG));

			// Apply preset to costume
			CostumeCreator_ApplyPresetOverlay(pRandomCostume, eaGroupPresets[iRandomPreset], false);

			{
				char achName[256];
				sprintf(achName, "%s_%s", eapchGroups[i], eaGroupPresets[iRandomPreset]->pcName);
				eaPush(&eaUsedPresets, allocAddString(achName));
			}
		}

		eaDestroy(&eaGroupPresets);

		// Add the new costume to the list
		if (pRandomCostume && bEnforceUnique)
		{
			for (i = eaSize(&pSource->eaCostumes) - 1; i >= 0; i--)
			{
				PlayerCostume *pCostume = pSource->eaCostumes[i]->pCostume;
				if (!pCostume)
					continue;

				if (bEnforceUnique && StructCompare(parse_PlayerCostume, pCostume, pRandomCostume, 0, 0, 0) == 0)
				{
					StructDestroyNoConstSafe(parse_PlayerCostume, &pRandomCostume);
					break;
				}
			}
		}
	} while (!pRandomCostume && (bEnforceUnique || bNoRepeats) && iTries++ < 10);

	if (pRandomCostume)
	{
		CostumeSource *pSourceCostume = CostumeEditList_AddCostume(pcList, CONTAINER_RECONST(PlayerCostume, pRandomCostume));
		eaPushEArray(&pSourceCostume->eapchCostumeInputs, &eaUsedPresets);
		StructDestroyNoConst(parse_PlayerCostume, pRandomCostume);
		pRandomCostume = NULL;
		bCreated = true;
	}

	eaDestroy(&eapchRequiredGroups);
	eaDestroy(&eapchGroups);
	eaDestroy(&eapchPreviouslyUsedOptions);
	eaDestroy(&eaUsedPresets);
	eaDestroy(&eaPresets);
	return bCreated;
}

// Get the number of costumes in the list.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_ListSize);
S32 gclCostumeEditListExpr_ListSize(const char *pcList)
{
	CostumeSourceList *pList = CostumeEditList_GetSourceList(pcList, false);
	COSTUME_UI_TRACE_FUNC();
	return pList ? eaSize(&pList->eaCostumes) : 0;
}

// Clear the list of costumes after the specified index. Useful for clearing part of an undo stack.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_ClearAfter);
bool gclCostumeEditListExpr_ClearAfter(const char *pcList, S32 iIndex)
{
	CostumeSourceList *pList = CostumeEditList_GetSourceList(pcList, false);
	COSTUME_UI_TRACE_FUNC();
	if (pList)
	{
		if (iIndex < 0)
		{
			CostumeEditList_ClearCostumeSourceList(pList->pcName, true);
			return true;
		}
		else
		{
			while (eaSize(&pList->eaCostumes) > iIndex + 1)
			{
				StructDestroy(parse_CostumeSource, eaPop(&pList->eaCostumes));
			}
			return true;
		}
	}
	return false;
}

// Preview the costume at the specified index without destroying the currently edited costume.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_PreviewCostume);
bool gclCostumeEditListExpr_PreviewCostume(const char *pcList, S32 iIndex)
{
	CostumeSource *pSource = CostumeEditList_GetCostumeByIndex(pcList, iIndex);
	PlayerCostume *pCostume = NULL;
	COSTUME_UI_TRACE_FUNC();

	if (pSource)
	{
		pCostume = pSource->pCostume ? pSource->pCostume : GET_REF(pSource->hPlayerCostume);
	}

	if (pCostume)
	{
		CostumeUI_costumeView_RegenCostume(g_pCostumeView, pCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), NULL, g_CostumeEditState.eaShowItems);
		return true;
	}

	return false;
}

// Preview the named costume without destroying the currently edited costume.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_PreviewNamedCostume);
bool gclCostumeEditListExpr_PreviewNamedCostume(const char *pcList, const char *pcTag)
{
	CostumeSource *pSource = CostumeEditList_GetCostume(pcList, pcTag);
	PlayerCostume *pCostume = NULL;
	COSTUME_UI_TRACE_FUNC();

	if (pSource)
	{
		pCostume = pSource->pCostume ? pSource->pCostume : GET_REF(pSource->hPlayerCostume);
	}

	if (pCostume)
	{
		CostumeUI_costumeView_RegenCostume(g_pCostumeView, pCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), NULL, g_CostumeEditState.eaShowItems);
		return true;
	}

	return false;
}

// Get the costume for a paperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_GetPaperdollCostume);
SA_RET_OP_VALID PaperdollHeadshotData *gclCostumeEditListExpr_GetPaperdollCostume(ExprContext *pContext, const char *pcList, S32 iIndex)
{
	CostumeSource *pSource = CostumeEditList_GetCostumeByIndex(pcList, iIndex);
	PlayerCostume *pCostume = NULL;
	COSTUME_UI_TRACE_FUNC();

	if (pSource)
	{
		pCostume = pSource->pCostume ? pSource->pCostume : GET_REF(pSource->hPlayerCostume);
	}

	return gclPaperdoll_CreateHeadshotData(pContext, NULL, pCostume, NULL, NULL, NULL, NULL);
}

// Get the named costume for a paperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_GetPaperdollNamedCostume);
SA_RET_OP_VALID PaperdollHeadshotData *gclCostumeEditListExpr_GetPaperdollNamedCostume(ExprContext *pContext, const char *pcList, const char *pcTag)
{
	CostumeSource *pSource = CostumeEditList_GetCostume(pcList, pcTag);
	PlayerCostume *pCostume = NULL;
	COSTUME_UI_TRACE_FUNC();

	if (pSource)
	{
		pCostume = pSource->pCostume ? pSource->pCostume : GET_REF(pSource->hPlayerCostume);
	}

	return gclPaperdoll_CreateHeadshotData(pContext, NULL, pCostume, NULL, NULL, NULL, NULL);
}

// Get the costume for a paperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_AddBackgroundCostume);
void gclCostumeEditListExpr_AddBackgroundCostume(const char *pcList, S32 iIndex, const char *pchCostumeName)
{
	CostumeSource *pSource = CostumeEditList_GetCostumeByIndex(pcList, iIndex);
	PlayerCostume *pCostume = NULL;
	COSTUME_UI_TRACE_FUNC();

	if (pSource)
	{
		pCostume = pSource->pCostume ? pSource->pCostume : GET_REF(pSource->hPlayerCostume);
	}

	if (pCostume)
	{
		CostumeViewCostume *pViewCostume = NULL;
		S32 i;

		pchCostumeName = allocAddString(pchCostumeName);
		for (i = eaSize(&g_pCostumeView->eaExtraCostumes) - 1; i >= 0; i--)
		{
			if (g_pCostumeView->eaExtraCostumes[i]->pcName == pchCostumeName)
			{
				pViewCostume = g_pCostumeView->eaExtraCostumes[i];
				break;
			}
		}

		if (!pViewCostume)
		{
			pViewCostume = costumeView_CreateViewCostume(g_pCostumeView);
			pViewCostume->pcName = pchCostumeName;
		}

		costumeView_RegenViewCostume(pViewCostume, pCostume, NULL, NULL, NULL);
	}
}

// Get the named costume for a paperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_AddBackgroundNamedCostume);
void gclCostumeEditListExpr_AddBackgroundNamedCostume(const char *pcList, const char *pcTag, const char *pchCostumeName)
{
	CostumeSource *pSource = CostumeEditList_GetCostume(pcList, pcTag);
	PlayerCostume *pCostume = NULL;
	COSTUME_UI_TRACE_FUNC();

	if (pSource)
	{
		pCostume = pSource->pCostume ? pSource->pCostume : GET_REF(pSource->hPlayerCostume);
	}

	if (pCostume)
	{
		CostumeViewCostume *pViewCostume = NULL;
		S32 i;

		pchCostumeName = allocAddString(pchCostumeName);
		for (i = eaSize(&g_pCostumeView->eaExtraCostumes) - 1; i >= 0; i--)
		{
			if (g_pCostumeView->eaExtraCostumes[i]->pcName == pchCostumeName)
			{
				pViewCostume = g_pCostumeView->eaExtraCostumes[i];
				break;
			}
		}

		if (!pViewCostume)
		{
			pViewCostume = costumeView_CreateViewCostume(g_pCostumeView);
			pViewCostume->pcName = pchCostumeName;
		}

		costumeView_RegenViewCostume(pViewCostume, pCostume, NULL, NULL, NULL);
	}
}

void gclCostumeEditList_SetCostumeColors(NOCONST(PlayerCostume) *pCostume, const char *pcBoneGroup, S32 iIndex, F32 fR, F32 fG, F32 fB)
{
	if (pCostume && iIndex >= 0)
	{
		PCSkeletonDef *pSkeleton = GET_REF(pCostume->hSkeleton);
		PCBoneGroup *pBoneGroup = pSkeleton ? eaIndexedGetUsingString(&pSkeleton->eaBoneGroups, pcBoneGroup) : NULL;
		PCBoneDef **eaBones = NULL;
		S32 i;

		if (pBoneGroup)
		{
			for (i = eaSize(&pBoneGroup->eaBoneInGroup) - 1; i >= 0; i--)
			{
				PCBoneDef *pBone = GET_REF(pBoneGroup->eaBoneInGroup[i]->hBone);
				if (pBone)
					eaPush(&eaBones, pBone);
			}
		}
		else if (pSkeleton)
		{
			for (i = eaSize(&pSkeleton->eaRequiredBoneDefs) - 1; i >= 0; i--)
			{
				PCBoneDef *pBone = GET_REF(pSkeleton->eaRequiredBoneDefs[i]->hBone);
				if (pBone)
					eaPush(&eaBones, pBone);
			}

			for (i = eaSize(&pSkeleton->eaOptionalBoneDefs) - 1; i >= 0; i--)
			{
				PCBoneDef *pBone = GET_REF(pSkeleton->eaOptionalBoneDefs[i]->hBone);
				if (pBone)
					eaPush(&eaBones, pBone);
			}
		}

		for (i = eaSize(&eaBones) - 1; i >= 0; i--)
		{
			NOCONST(PCPart) *pPart = costumeTailor_GetPartByBone(pCostume, eaBones[i], NULL);
			if (pPart)
			{
				switch (iIndex)
				{
				case kPCEditColor_Color0:
				case kPCEditColor_PerPartColor0:
					pPart->color0[0] = fR;
					pPart->color0[1] = fG;
					pPart->color0[2] = fB;
					break;
				case kPCEditColor_Color1:
				case kPCEditColor_PerPartColor1:
					pPart->color1[0] = fR;
					pPart->color1[1] = fG;
					pPart->color1[2] = fB;
					break;
				case kPCEditColor_Color2:
				case kPCEditColor_PerPartColor2:
					pPart->color2[0] = fR;
					pPart->color2[1] = fG;
					pPart->color2[2] = fB;
					break;
				case kPCEditColor_Color3:
				case kPCEditColor_PerPartColor3:
					pPart->color3[0] = fR;
					pPart->color3[1] = fG;
					pPart->color3[2] = fB;
					break;
				}
			}
		}

		eaDestroy(&eaBones);
	}
}

// Get the costume for a paperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_SetCostumeColors);
void gclCostumeEditListExpr_SetCostumeColors(const char *pcList, S32 iIndex, const char *pcBoneGroup, S32 iColorIndex, F32 fR, F32 fG, F32 fB)
{
	CostumeSource *pSource = CostumeEditList_GetCostumeByIndex(pcList, iIndex);
	PlayerCostume *pCostume = NULL;
	COSTUME_UI_TRACE_FUNC();

	if (pSource)
	{
		pCostume = pSource->pCostume ? pSource->pCostume : NULL;
	}

	gclCostumeEditList_SetCostumeColors(CONTAINER_NOCONST(PlayerCostume, pCostume), pcBoneGroup, iColorIndex, fR, fG, fB);
}

// Get the named costume for a paperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_SetNamedCostumeColors);
void gclCostumeEditListExpr_SetNamedCostumeColors(const char *pcList, const char *pcTag, const char *pcBoneGroup, S32 iColorIndex, F32 fR, F32 fG, F32 fB)
{
	CostumeSource *pSource = CostumeEditList_GetCostume(pcList, pcTag);
	PlayerCostume *pCostume = NULL;
	COSTUME_UI_TRACE_FUNC();

	if (pSource)
	{
		pCostume = pSource->pCostume ? pSource->pCostume : NULL;
	}

	gclCostumeEditList_SetCostumeColors(CONTAINER_NOCONST(PlayerCostume, pCostume), pcBoneGroup, iColorIndex, fR, fG, fB);
}

// Clear the memory used by the costume list, equivalent to: CostumeCreatorList_ClearAfter(List, -1)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_Clear);
void gclCostumeEditListExpr_Clear(const char *pcList)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeEditList_ClearCostumeSourceList(pcList, true);
}

// Make the costume list persist after CharacterCreation_ResetCostume() has been called.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_SetPersist);
void gclCostumeEditListExpr_SetPersist(const char *pcList, bool bPersist)
{
	CostumeSourceList *pList = CostumeEditList_GetSourceList(pcList, bPersist);
	COSTUME_UI_TRACE_FUNC();
	if (pList)
	{
		pList->eFlags &= ~kCostumeSourceFlag_PersistOnCostumeExit;
		if (bPersist)
			pList->eFlags |= kCostumeSourceFlag_PersistOnCostumeExit;
	}
}

// Add a list of costumes from microtransactions
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_AddCostumesFromMicroTrans);
void gclCostumeEditListExpr_AddCostumesFromMicroTrans(const char *pcList, const char *pcSkeleton, char *pcCategories)
{
	char **eapchCategories = NULL;
	char *pchToken, *pchContext;
	S32 i, j, k;

	COSTUME_UI_TRACE_FUNC();

	eaStackCreate(&eapchCategories,
			strchrCount(pcCategories, ' ')
			+ strchrCount(pcCategories, '\r')
			+ strchrCount(pcCategories, '\n')
			+ strchrCount(pcCategories, '\t')
			+ strchrCount(pcCategories, '%')
			+ strchrCount(pcCategories, '|')
			+ 1
	);
	strdup_alloca(pcCategories, pcCategories);

	if (strcspn(pcSkeleton, "?*") != strlen(pcSkeleton))
		pcSkeleton = "*";

	if (pchToken = strtok_r(pcCategories, " \r\n\t%|", &pchContext))
	{
		do
		{
			if (*pchToken)
				eaPush(&eapchCategories, pchToken);
		} while (pchToken = strtok_r(NULL, " \r\n\t%|", &pchContext));
	}

	if (g_pMTCostumes)
	{
		for (i = 0; i < eaSize(&g_pMTCostumes->ppCostumes); i++)
		{
			MicroTransactionCostume *pMTCostume = g_pMTCostumes->ppCostumes[i];
			PlayerCostume *pCostume = GET_REF(pMTCostume->hCostume);

			// Ignore unowned hidden costumes
			if (!pMTCostume->bOwned && pMTCostume->bHidden)
				continue;

			// Match skeleton
			if (stricmp(pcSkeleton, "*") && (!pCostume || !isWildcardMatch(pcSkeleton, REF_STRING_FROM_HANDLE(pCostume->hSkeleton), false, true)))
				continue;

			if (!CostumeEditList_GetCostume(pcList, REF_STRING_FROM_HANDLE(pMTCostume->hCostume)))
			{
				MicroTransactionCostumeSource *pMTSource = NULL;

				for (j = 0; j < eaSize(&pMTCostume->eaSources); j++)
				{
					const char ***peaHaystack = &pMTCostume->eaSources[j]->pProduct->ppchCategories;
					bool bValid = false;

					// Match against the categories
					for (k = eaSize(&eapchCategories) - 1; k >= 0; k--)
					{
						if (eapchCategories[k][0] == '+')
						{
							if (eaFindString(peaHaystack, eapchCategories[k] + 1) < 0)
							{
								bValid = false;
								break;
							}
						}
						else if (eapchCategories[k][0] == '-')
						{
							if (eaFindString(peaHaystack, eapchCategories[k] + 1) >= 0)
							{
								bValid = false;
								break;
							}
						}
						else
						{
							if (eaFindString(peaHaystack, eapchCategories[k]) >= 0)
								bValid = true;
						}
					}

					// Valid source
					if (bValid)
					{
						pMTSource = pMTCostume->eaSources[j];
						break;
					}
				}

				if (pMTSource)
				{
					CostumeSource *pSource = CostumeEditList_AddNamedCostumeRef(pcList, REF_STRING_FROM_HANDLE(pMTCostume->hCostume), REF_STRING_FROM_HANDLE(pMTCostume->hCostume));
					if (!eaIndexedGetUsingInt(&pMTCostume->eaSources, pSource->uiProductID))
						pSource->uiProductID = pMTSource->uID;
				}
			}
		}
	}

	eaDestroy(&eapchCategories);

	// TODO: sort costumes by display name
}

// Add a list of costumes from microtransactions
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_AddCostumeTailorPresets);
void gclCostumeEditListExpr_AddCostumeTailorPresets(const char *pcList, const char *pcSkeleton)
{
	static MicroTransactionUIProduct **s_eaProducts;
	static U32 *s_eaiProductIDs;

	if (strcspn(pcSkeleton, "?*") != strlen(pcSkeleton))
		pcSkeleton = "*";

	FOR_EACH_IN_REFDICT("PCCostumeSet", PCCostumeSet, pSet);
	{
		S32 i;

		if ((pSet->eCostumeSetFlags & kPCCostumeSetFlags_TailorPresets) == 0)
			continue;

		for (i = 0; i < eaSize(&pSet->eaPlayerCostumes); i++)
		{
			const char *pchCostumeName = REF_STRING_FROM_HANDLE(pSet->eaPlayerCostumes[i]->hPlayerCostume);
			PlayerCostume *pCostume = GET_REF(pSet->eaPlayerCostumes[i]->hPlayerCostume);

			if (!pCostume)
				continue;

			if (strcmp(pcSkeleton, "*") && !isWildcardMatch(pcSkeleton, REF_STRING_FROM_HANDLE(pCostume->hSkeleton), false, true))
				continue;

			if (!CostumeEditList_GetCostume(pcList, pchCostumeName))
			{
				CostumeSource *pSource = CostumeEditList_AddNamedCostumeRef(pcList, pchCostumeName, pchCostumeName);
				S32 iPart, iProduct, iProductID;

				SET_HANDLE_FROM_REFERENT("PCCostumeSet", pSet, pSource->hCostumeSetRef);

				for (iPart = 0; iPart < eaSize(&pCostume->eaParts); iPart++)
				{
					PCPart *pPart = pCostume->eaParts[iPart];
					UnlockMetaData *pUnlock = NULL;
					if (IS_HANDLE_ACTIVE(pPart->hGeoDef) && stashFindPointer(g_CostumeEditState.stashGeoUnlockMeta, REF_STRING_FROM_HANDLE(pPart->hGeoDef), &pUnlock))
					{
						eaPushEArray(&s_eaProducts, &pUnlock->eaFullProductList);
					}
					if (IS_HANDLE_ACTIVE(pPart->hMatDef) && stashFindPointer(g_CostumeEditState.stashMatUnlockMeta, REF_STRING_FROM_HANDLE(pPart->hMatDef), &pUnlock))
					{
						eaPushEArray(&s_eaProducts, &pUnlock->eaFullProductList);
					}
					if (IS_HANDLE_ACTIVE(pPart->hPatternTexture) && stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, REF_STRING_FROM_HANDLE(pPart->hPatternTexture), &pUnlock))
					{
						eaPushEArray(&s_eaProducts, &pUnlock->eaFullProductList);
					}
					if (IS_HANDLE_ACTIVE(pPart->hDetailTexture) && stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, REF_STRING_FROM_HANDLE(pPart->hDetailTexture), &pUnlock))
					{
						eaPushEArray(&s_eaProducts, &pUnlock->eaFullProductList);
					}
					if (IS_HANDLE_ACTIVE(pPart->hSpecularTexture) && stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, REF_STRING_FROM_HANDLE(pPart->hSpecularTexture), &pUnlock))
					{
						eaPushEArray(&s_eaProducts, &pUnlock->eaFullProductList);
					}
					if (IS_HANDLE_ACTIVE(pPart->hDiffuseTexture) && stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, REF_STRING_FROM_HANDLE(pPart->hDiffuseTexture), &pUnlock))
					{
						eaPushEArray(&s_eaProducts, &pUnlock->eaFullProductList);
					}
					if (pPart->pMovableTexture && IS_HANDLE_ACTIVE(pPart->pMovableTexture->hMovableTexture) && stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture), &pUnlock))
					{
						eaPushEArray(&s_eaProducts, &pUnlock->eaFullProductList);
					}
				}

				for (iProduct = 0; iProduct < eaSize(&s_eaProducts); iProduct++)
					ea32PushUnique(&s_eaiProductIDs, s_eaProducts[iProduct]->uID);

				for (iPart = 0; iPart < eaSize(&pCostume->eaParts); iPart++)
				{
					PCPart *pPart = pCostume->eaParts[iPart];
					UnlockMetaData *pUnlock = NULL;
					if (IS_HANDLE_ACTIVE(pPart->hGeoDef) && stashFindPointer(g_CostumeEditState.stashGeoUnlockMeta, REF_STRING_FROM_HANDLE(pPart->hGeoDef), &pUnlock))
					{
						for (iProductID = ea32Size(&s_eaiProductIDs) - 1; iProductID >= 0; iProductID--)
						{
							for (iProduct = eaSize(&pUnlock->eaFullProductList) - 1; iProduct >= 0; iProduct--)
								if (pUnlock->eaFullProductList[iProduct]->uID == s_eaiProductIDs[iProductID])
									break;

							if (iProduct < 0)
								ea32Remove(&s_eaiProductIDs, iProductID);
						}
					}
					if (IS_HANDLE_ACTIVE(pPart->hMatDef) && stashFindPointer(g_CostumeEditState.stashMatUnlockMeta, REF_STRING_FROM_HANDLE(pPart->hMatDef), &pUnlock))
					{
						for (iProductID = ea32Size(&s_eaiProductIDs) - 1; iProductID >= 0; iProductID--)
						{
							for (iProduct = eaSize(&pUnlock->eaFullProductList) - 1; iProduct >= 0; iProduct--)
								if (pUnlock->eaFullProductList[iProduct]->uID == s_eaiProductIDs[iProductID])
									break;

							if (iProduct < 0)
								ea32Remove(&s_eaiProductIDs, iProductID);
						}
					}
					if (IS_HANDLE_ACTIVE(pPart->hPatternTexture) && stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, REF_STRING_FROM_HANDLE(pPart->hPatternTexture), &pUnlock))
					{
						for (iProductID = ea32Size(&s_eaiProductIDs) - 1; iProductID >= 0; iProductID--)
						{
							for (iProduct = eaSize(&pUnlock->eaFullProductList) - 1; iProduct >= 0; iProduct--)
								if (pUnlock->eaFullProductList[iProduct]->uID == s_eaiProductIDs[iProductID])
									break;

							if (iProduct < 0)
								ea32Remove(&s_eaiProductIDs, iProductID);
						}
					}
					if (IS_HANDLE_ACTIVE(pPart->hDetailTexture) && stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, REF_STRING_FROM_HANDLE(pPart->hDetailTexture), &pUnlock))
					{
						for (iProductID = ea32Size(&s_eaiProductIDs) - 1; iProductID >= 0; iProductID--)
						{
							for (iProduct = eaSize(&pUnlock->eaFullProductList) - 1; iProduct >= 0; iProduct--)
								if (pUnlock->eaFullProductList[iProduct]->uID == s_eaiProductIDs[iProductID])
									break;

							if (iProduct < 0)
								ea32Remove(&s_eaiProductIDs, iProductID);
						}
					}
					if (IS_HANDLE_ACTIVE(pPart->hSpecularTexture) && stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, REF_STRING_FROM_HANDLE(pPart->hSpecularTexture), &pUnlock))
					{
						for (iProductID = ea32Size(&s_eaiProductIDs) - 1; iProductID >= 0; iProductID--)
						{
							for (iProduct = eaSize(&pUnlock->eaFullProductList) - 1; iProduct >= 0; iProduct--)
								if (pUnlock->eaFullProductList[iProduct]->uID == s_eaiProductIDs[iProductID])
									break;

							if (iProduct < 0)
								ea32Remove(&s_eaiProductIDs, iProductID);
						}
					}
					if (IS_HANDLE_ACTIVE(pPart->hDiffuseTexture) && stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, REF_STRING_FROM_HANDLE(pPart->hDiffuseTexture), &pUnlock))
					{
						for (iProductID = ea32Size(&s_eaiProductIDs) - 1; iProductID >= 0; iProductID--)
						{
							for (iProduct = eaSize(&pUnlock->eaFullProductList) - 1; iProduct >= 0; iProduct--)
								if (pUnlock->eaFullProductList[iProduct]->uID == s_eaiProductIDs[iProductID])
									break;

							if (iProduct < 0)
								ea32Remove(&s_eaiProductIDs, iProductID);
						}
					}
					if (pPart->pMovableTexture && IS_HANDLE_ACTIVE(pPart->pMovableTexture->hMovableTexture) && stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture), &pUnlock))
					{
						for (iProductID = ea32Size(&s_eaiProductIDs) - 1; iProductID >= 0; iProductID--)
						{
							for (iProduct = eaSize(&pUnlock->eaFullProductList) - 1; iProduct >= 0; iProduct--)
								if (pUnlock->eaFullProductList[iProduct]->uID == s_eaiProductIDs[iProductID])
									break;

							if (iProduct < 0)
								ea32Remove(&s_eaiProductIDs, iProductID);
						}
					}
				}

				// TODO: do better resolution for ea32Size(&s_eaiProductIDs) > 0
				if (ea32Size(&s_eaiProductIDs) > 0)
					pSource->uiProductID = s_eaiProductIDs[0];
				else
					pSource->uiProductID = 0;

				eaClear(&s_eaProducts);
				eaiClear(&s_eaiProductIDs);
			}
		}
	}
	FOR_EACH_END;

	// TODO: sort costumes by display name
}

static int SortByDisplayName(const CostumeSource **ppLeft, const CostumeSource **ppRight)
{
	return stricmp_safe((*ppLeft)->pchDisplayName, (*ppRight)->pchDisplayName);
}

// Get the list of costumes from the costume list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_GenGetCostumes);
void gclCostumeEditListExpr_GenGetCostumes(SA_PARAM_NN_VALID UIGen *pGen, const char *pcList, const char *pcSkeleton)
{
	CostumeSourceList *pList = CostumeEditList_GetSourceList(pcList, false);
	static CostumeSource **s_eaCostumes;
	S32 i, j;

	if (strcspn(pcSkeleton, "?*") != strlen(pcSkeleton))
		pcSkeleton = "*";

	if (pList)
	{
		for (i = 0; i < eaSize(&pList->eaCostumes); i++)
		{
			CostumeSource *pSource = pList->eaCostumes[i];
			PlayerCostume *pCostume = pSource->pCostume ? pSource->pCostume : GET_REF(pSource->hPlayerCostume);
			PCSkeletonDef *pSkeleton = pCostume ? GET_REF(pCostume->hSkeleton) : NULL;

			if (stricmp(pcSkeleton, "*") && (!pSkeleton || !isWildcardMatch(pcSkeleton, pSkeleton->pcName, false, true)))
				continue;

			if (pSource->uiProductID)
				gclMicroTrans_UpdateUIProduct(pSource->uiProductID, &pSource->pProduct);

			if (IS_HANDLE_ACTIVE(pSource->hCostumeSetRef))
				pSource->pCostumeSet = GET_REF(pSource->hCostumeSetRef);

			pSource->pchDisplayName = NULL;
			pSource->pchDescription = NULL;
			pSource->pchIcon = NULL;

			if (pSource->pProduct)
			{
				pSource->pchDisplayName = pSource->pProduct->pName;
				pSource->pchDescription = pSource->pProduct->pDescription;
				if (eaSize(&pSource->pProduct->ppIcons) > 0)
					pSource->pchIcon = pSource->pProduct->ppIcons[0];
				else
					pSource->pchIcon = pSource->pProduct->pchIcon;
			}

			if (pSource->pCostumeSet)
			{
				CostumeRefForSet *pRef = NULL;
				for (j = 0; j < eaSize(&pSource->pCostumeSet->eaPlayerCostumes); j++)
				{
					if (GET_REF(pSource->pCostumeSet->eaPlayerCostumes[j]->hPlayerCostume) == pCostume)
					{
						pRef = pSource->pCostumeSet->eaPlayerCostumes[j];
						break;
					}
				}
				if (!pSource->pchDisplayName)
					pSource->pchDisplayName = pRef ? TranslateDisplayMessage(pRef->displayNameMsg) : TranslateDisplayMessage(pSource->pCostumeSet->displayNameMsg);
				if (!pSource->pchDescription)
					pSource->pchDescription = pRef ? TranslateDisplayMessage(pRef->descriptionMsg) : NULL;
				if (!pSource->pchIcon)
					pSource->pchIcon = pRef ? pRef->pcImage : NULL;
			}

			eaPush(&s_eaCostumes, pSource);
		}

		eaQSort(s_eaCostumes, SortByDisplayName);
		ui_GenSetListSafe(pGen, &s_eaCostumes, CostumeSource);
		eaClear(&s_eaCostumes);
	}
	else
		ui_GenSetListSafe(pGen, NULL, CostumeSource);
}

// Apply a costume from the list as an overlay to the editor costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreatorList_ApplyBoneGroupOverlay);
void gclCostumeEditListExpr_ApplyBoneGroupOverlay(const char *pcList, const char *pcName, const char *pcBoneGroup, bool bHover)
{
	CostumeSource *pSource = CostumeEditList_GetCostume(pcList, pcName);

	if (pSource)
	{
		NOCONST(PlayerCostume) *pCostume = g_CostumeEditState.pCostume;
		PlayerCostume *pSourceCostume = pSource->pCostume ? pSource->pCostume : GET_REF(pSource->hPlayerCostume);
		PCSkeletonDef *pSkeleton = pCostume ? GET_REF(pCostume->hSkeleton) : pSourceCostume ? GET_REF(pSourceCostume->hSkeleton) : NULL;
		PCBoneGroup *pBoneGroup = pSkeleton ? eaIndexedGetUsingString(&pSkeleton->eaBoneGroups, pcBoneGroup) : NULL;

		if (bHover)
		{
			StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pHoverCostume);
			g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, pCostume);
			pCostume = g_CostumeEditState.pHoverCostume;
		}

		if (pCostume && pSourceCostume)
		{
			S32 i, j;

			// Remove existing bones in the bone group
			if (pBoneGroup)
			{
				for (i = eaSize(&pBoneGroup->eaBoneInGroup) - 1; i >= 0; i--)
				{
					NOCONST(PCPart) *pPart = costumeTailor_GetPartByBone(pCostume, GET_REF(pBoneGroup->eaBoneInGroup[i]->hBone), NULL);
					if (pPart)
					{
						eaFindAndRemove(&pCostume->eaParts, pPart);
						StructDestroyNoConst(parse_PCPart, pPart);
					}
				}
			}

			// Swap categories to new categories
			for (i = eaSize(&pSourceCostume->eaParts) - 1; i >= 0; i--)
			{
				PCBoneDef *pBone = GET_REF(pSourceCostume->eaParts[i]->hBoneDef);
				PCGeometryDef *pGeoDef = GET_REF(pSourceCostume->eaParts[i]->hGeoDef);
				PCRegion *pRegion = pBone ? GET_REF(pBone->hRegion) : NULL;
				if (pRegion && pGeoDef && stricmp(pGeoDef->pcName, "None"))
				{
					PCCategory *pExistingCategory = costumeTailor_GetCategoryForRegion(CONTAINER_RECONST(PlayerCostume, pCostume), pRegion);
					PCCategory *pNewCategory = costumeTailor_GetCategoryForRegion(pSourceCostume, pRegion);
					if (pExistingCategory != pNewCategory)
					{
						for (j = eaSize(&pGeoDef->eaCategories) - 1; j >= 0; j--)
						{
							if (GET_REF(pGeoDef->eaCategories[j]->hCategory) == pExistingCategory)
							{
								pNewCategory = pExistingCategory;
								break;
							}
						}
					}
					if (pExistingCategory != pNewCategory)
						costumeTailor_SetRegionCategory(pCostume, pRegion, pNewCategory);
				}
			}

			if (costumeTailor_ApplyCostumeOverlayBG(CONTAINER_RECONST(PlayerCostume, pCostume),
				NULL, pSourceCostume, g_CostumeEditState.eaUnlockedCostumes,
				pBoneGroup, g_CostumeEditState.pSlotType,
				true, true, false, true, false))
			{
				if (bHover)
					CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
				else
					CostumeUI_RegenCostume(true);
			}
		}
	}
	else if (bHover)
	{
		StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pHoverCostume);
		CostumeUI_RegenCostume(true);
	}
}
