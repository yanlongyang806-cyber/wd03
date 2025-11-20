#include "Character.h"
#include "CharacterCreationUI.h"
#include "CharacterClass.h"
#include "contact_common.h"
#include "CostumeCommon.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonGenerate.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonRandom.h"
#include "CostumeCommonTailor.h"
#include "dynFxInfo.h"
#include "dynFxInterface.h"
#include "dynFx.h"
#include "EditorManager.h"
#include "entCritter.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "GameClientLib.h"
#include "gclBaseStates.h"
#include "gclCamera.h"
#include "gclCommandParse.h"
#include "gclCostumeCameraUI.h"
#include "gclCostumeLineUI.h"
#include "gclCostumeUI.h"
#include "gclCostumeUIState.h"
#include "gclCostumeUtil.h"
#include "gclCostumeView.h"
#include "gclDialogBox.h"
#include "gclEntity.h"
#include "gclLogin.h"
#include "GfxConsole.h"
#include "GfxHeadshot.h"
#include "GlobalStateMachine.h"
#include "GraphicsLib.h"
#include "Guild.h"
#include "inputGamepad.h"
#include "inputMouse.h"
#include "interaction_common.h"
#include "inventoryCommon.h"
#include "LoginCommon.h"
#include "microtransactions_common.h"
#include "mission_common.h"
#include "Player.h"
#include "Powers.h"
#include "PowerAnimFX.h"
#include "PowerTree.h"
#include "ResourceManager.h"
#include "SavedPetCommon.h"
#include "SimpleParser.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "UIGenColorChooser.h"
#include "WeaponStance.h"
#include "pub/gclEntity.h"
#include "MicroTransactions.h"
#include "gclMicroTransactions.h"
#include "MicroTransactionUI.h"
#include "GameStringFormat.h"
#include "GfxTexturesPublic.h"
#include "gclCostumeUnlockUI.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "StaticWorld/WorldCell.h"
#include "fileutil.h"
#include "gclNotify.h"
#include "Login2Common.h"

#include "EntitySavedData_h_ast.h"
#include "gclCostumeUIState_h_ast.h"
#include "gclCostumeUI_c_ast.h"
#include "gclCostumeLineUI_h_ast.h"
#include "entEnums_h_ast.h"
#include "species_common_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "CharacterSelection.h"
#include "Character_h_ast.h"
#include "Entity_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping("gclCostumeUIState.h", BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

extern Login2CharacterCreationData *g_CharacterCreationData;

static bool s_bRegionCategoryBoneFilter = true;

bool g_MirrorSelectMode = true;
bool g_GroupSelectMode = true;

CostumeUITrace g_CostumeUITraceState[MAX_COSTUME_UI_TRACE_STATE];
S32 g_iNextCostumeTraceState;
U32 g_uiLastCostumeRegenTime;
U32 g_uiLastCostumeDictTime;
U32 g_uiLastCostumeLineTime;

static char *s_pchStorePlayerCostumeResult = NULL;
static bool s_bStorePlayerCostumeSuccessful = false;

static bool s_bCreatorActive = false;

static GfxCameraView s_CameraView;

bool g_bHideUnownedCostumes;

static int s_iRandomizerTest = 0;
AUTO_CMD_INT(s_iRandomizerTest, CostumeCreator_TestCostumeRandomize) ACMD_ACCESSLEVEL(9) ACMD_HIDE;

static int s_iDemoFlag = false;
AUTO_CMD_INT(s_iDemoFlag, SetDemoFlag) ACMD_ACCESSLEVEL(7);

AUTO_STRUCT;
typedef struct PetCostumeList {
	PlayerCostume **eaPetCostumes;				AST(UNOWNED)
} PetCostumeList;

AUTO_STRUCT;
typedef struct PetCostumeSlotList {
	UICostumeSlot **eaSlots;					AST(UNOWNED)
} PetCostumeSlotList;

AUTO_STRUCT;
typedef struct StringModelRow {
	char *estrString; AST(NAME(String) ESTRING)
} StringModelRow;

typedef struct ColorPermutationData
{
	U8 color[4];
	int iSlot;
	int iOriginalSlot;
} ColorPermutationData;

CostumeEditState g_CostumeEditState = {0};
bool g_bOmitHasOnlyOne = false;
bool g_bCountNone = false;

static bool s_bAddNoneToBoneList = false;

static char *gErrorText = NULL;

extern ParseTable parse_DynDefineParam[];
#define TYPE_parse_DynDefineParam DynDefineParam
extern DictionaryHandle g_hWeaponStanceDict;

//list of skeletons used by CharacterCreation_BuildPlainCostume
static const char *g_plainSkeletons[] = 
{
	"Male",
	"Human_Male",
	"StarTrekMale",
	"NW_Male",
	"Default",
	"CoreMale",
	"CoreDefault"
};

bool CostumeCreator_CommonSetBoneScale(NOCONST(PlayerCostume) *pCostume, F32 fMin, F32 fMax, const char *pcScaleName, F32 fBoneScale);
void CostumeCreator_SetMaterialLinkAll(void);

PCBoneDef *CostumeUI_FindBone(const char *pcName, PCSkeletonDef *pSkel)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	if (!pSkel)
	{
		if (pcName && !!strchr(pcName, '*'))
		{
			DictionaryEArrayStruct *pBonesDict = resDictGetEArrayStruct(g_hCostumeBoneDict);

			for (i = eaSize(&pBonesDict->ppReferents) - 1; i >= 0; i--)
			{
				PCBoneDef *pBoneDef = pBonesDict->ppReferents[i];
				if (isWildcardMatch(pcName, pBoneDef->pcName, false, true))
				{
					PERFINFO_AUTO_STOP_FUNC();
					return pBoneDef;
				}
			}
		}

		PERFINFO_AUTO_STOP_FUNC();
		return RefSystem_ReferentFromString(g_hCostumeBoneDict, pcName);
	}

	if (!stricmp(pcName, "None"))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return RefSystem_ReferentFromString(g_hCostumeBoneDict, pcName);
	}

	for (i = eaSize(&pSkel->eaRequiredBoneDefs) - 1; i >= 0; i--)
	{
		PCBoneDef *pBoneDef = GET_REF(pSkel->eaRequiredBoneDefs[i]->hBone);
		if (pBoneDef && isWildcardMatch(pcName, pBoneDef->pcName, false, true))
		{
			PERFINFO_AUTO_STOP_FUNC();
			return pBoneDef;
		}
	}
	for (i = eaSize(&pSkel->eaOptionalBoneDefs) - 1; i >= 0; i--)
	{
		PCBoneDef *pBoneDef = GET_REF(pSkel->eaOptionalBoneDefs[i]->hBone);
		if (pBoneDef && isWildcardMatch(pcName, pBoneDef->pcName, false, true))
		{
			PERFINFO_AUTO_STOP_FUNC();
			return pBoneDef;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
	return NULL;
}

void CostumeUI_FilterBoneList(PCBoneDef ***peaBones, const char **eaIncludeBones, const char **eaExcludeBones)
{
	int i, j, k;

	PERFINFO_AUTO_START_FUNC();

	if (eaSize(&eaExcludeBones)) {
		for (i = eaSize(peaBones) - 1; i >= 0; i--) {
			for (j = eaSize(&eaExcludeBones) - 1; j >= 0; j--) {
				if (isWildcardMatch(eaExcludeBones[j], (*peaBones)[i]->pcName, false, true)) {
					for (k = eaSize(&eaIncludeBones) - 1; k >= 0; k--) {
						if (isWildcardMatch(eaIncludeBones[k], (*peaBones)[i]->pcName, false, true)) {
							break;
						}
					}
					if (k < 0) {
						eaRemove(peaBones, i);
					}
					break;
				}
			}
		}
	}
	else if (eaSize(&eaIncludeBones))
	{
		for (i = eaSize(peaBones) - 1; i >= 0; i--) {
			for (j = eaSize(&eaIncludeBones) - 1; j >= 0; j--) {
				if (isWildcardMatch(eaIncludeBones[j], (*peaBones)[i]->pcName, false, true)) {
					break;
				}
			}
			if (j < 0) {
				eaRemove(peaBones, i);
			}
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void CostumeUI_GetAllValidSpecies(PCSkeletonDef **eaSkels, SpeciesDef ***peaSpecies)
{
	SpeciesDef **eaPartial = NULL;
	NOCONST(PlayerCostume) *pCostume;
	int i;

	PERFINFO_AUTO_START_FUNC();

	pCostume = StructCreateNoConst(parse_PlayerCostume);
	pCostume->eCostumeType = kPCCostumeType_Player;

	for (i = 0; i < eaSize(&eaSkels); i++)
	{
		SET_HANDLE_FROM_REFERENT(g_hCostumeSkeletonDict, eaSkels[i], pCostume->hSkeleton);
		costumeTailor_GetValidSpecies(pCostume, &eaPartial, true, true);
		eaPushEArray(peaSpecies, &eaPartial);
	}

	StructDestroyNoConst(parse_PlayerCostume, pCostume);
	eaDestroy(&eaPartial);

	PERFINFO_AUTO_STOP_FUNC();
}

void CostumeUI_ClearSelections(void)
{
	PERFINFO_AUTO_START_FUNC();

	// Clear old selection info after randomizing
	g_CostumeEditState.pPart = NULL;
	REMOVE_HANDLE(g_CostumeEditState.hBone);
	REMOVE_HANDLE(g_CostumeEditState.hRegion);
	REMOVE_HANDLE(g_CostumeEditState.hCategory);
	REMOVE_HANDLE(g_CostumeEditState.hGeometry);
	REMOVE_HANDLE(g_CostumeEditState.hMaterial);
	REMOVE_HANDLE(g_CostumeEditState.hPattern);
	REMOVE_HANDLE(g_CostumeEditState.hDetail);
	REMOVE_HANDLE(g_CostumeEditState.hSpecular);
	REMOVE_HANDLE(g_CostumeEditState.hDiffuse);
	REMOVE_HANDLE(g_CostumeEditState.hMovable);

	PERFINFO_AUTO_STOP_FUNC();
}

void CostumeUI_ValidateAllParts(NOCONST(PlayerCostume) *pCostume, bool bUGC, bool bSafeMode)
{
	PERFINFO_AUTO_START_FUNC();

	if (pCostume->eCostumeType == kPCCostumeType_Player) {
		// Don't force costume to change when in "unrestricted" mode
		Entity *pEnt = entActivePlayerPtr();
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		if (bSafeMode) {
			// This is really slow, and should probably only be used in cases where categories need to fixed.
			costumeTailor_MakeCostumeValid(pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, true, g_CostumeEditState.bUnlockAll, false, guild_GetGuild(pEnt), false, pExtract, bUGC, g_CostumeEditState.eaPowerFXBones);
		} else {
			// This runs faster, but is more prone to breaking.
			enum { ValidateListNormal, ValidateListUnlockAll, ValidateListUnlockCMat, ValidateListUnlockCTex, ValidateList_MAX };
			static PlayerCostume **eaFlatList = NULL;
			eaSetSizeStruct(&eaFlatList, parse_PlayerCostume, ValidateList_MAX);
			CostumeUI_GetValidCostumeUnlocks(
				CONTAINER_NOCONST(PlayerCostume, eaFlatList[ValidateListNormal]), 
				CONTAINER_NOCONST(PlayerCostume, eaFlatList[ValidateListUnlockAll]),
				CONTAINER_NOCONST(PlayerCostume, eaFlatList[ValidateListUnlockCMat]), 
				CONTAINER_NOCONST(PlayerCostume, eaFlatList[ValidateListUnlockCTex]), 
				g_CostumeEditState.pCostume);
			costumeTailor_MakeCostumeValid(pCostume, GET_REF(g_CostumeEditState.hSpecies), eaFlatList, g_CostumeEditState.pSlotType, true, g_CostumeEditState.bUnlockAll, false, guild_GetGuild(pEnt), false, pExtract, bUGC, g_CostumeEditState.eaPowerFXBones);
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Get the costume validation error or an empty string if there is no error
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetValidationError");
const char *CostumeCreator_GetValidationError(void)
{
	COSTUME_UI_TRACE_FUNC();
	if (!gErrorText) return "";
	return gErrorText;
}

// Set Slot Type
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetSlotType");
void CostumeCreator_SetSlotType(const char *pchSlotType)
{
	COSTUME_UI_TRACE_FUNC();
	g_CostumeEditState.pSlotType = costumeLoad_GetSlotType(pchSlotType);
	CostumeUI_RegenCostumeEx(true, true);
}

// Toggle bone selection verification from all bones
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_AllowFullBoneSelection");
void CostumeCreator_AllowFullBoneSelection(bool bFlag)
{
	COSTUME_UI_TRACE_FUNC();
	g_CostumeEditState.bAllowSelectFromAllBones = bFlag;
}

// Reset the bone list filters
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_ResetBoneFilters");
void CostumeCreator_ResetBoneFilters(void)
{
	COSTUME_UI_TRACE_FUNC();
	while (eaSize(&g_CostumeEditState.eaExcludeBones) > 0) {
		char *pchFilter = eaPop(&g_CostumeEditState.eaExcludeBones);
		if (pchFilter) {
			StructFreeString(pchFilter);
		}
	}
	while (eaSize(&g_CostumeEditState.eaIncludeBones) > 0) {
		char *pchFilter = eaPop(&g_CostumeEditState.eaIncludeBones);
		if (pchFilter) {
			StructFreeString(pchFilter);
		}
	}
}

// Exclude a bone from the list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_ExcludeBone");
void CostumeCreator_ExcludeBone(const char *pchFilter)
{
	COSTUME_UI_TRACE_FUNC();
	eaPush(&g_CostumeEditState.eaExcludeBones, StructAllocString(pchFilter));
	g_CostumeEditState.bUpdateLines = true;
	g_CostumeEditState.bUpdateLists = true;
}

// Include a bone that matched an exclude filter, or only allow the bones that match the filter
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_IncludeBone");
void CostumeCreator_IncludeBone(const char *pchFilter)
{
	COSTUME_UI_TRACE_FUNC();
	eaPush(&g_CostumeEditState.eaIncludeBones, StructAllocString(pchFilter));
	g_CostumeEditState.bUpdateLines = true;
	g_CostumeEditState.bUpdateLists = true;
}

// Check to see if the part is unlockable
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_IsUnlockablePart");
int CostumeCreator_IsUnlockablePart(int restriction)
{
	COSTUME_UI_TRACE_FUNC();
	return (restriction & kPCRestriction_Player) && !(restriction & kPCRestriction_Player_Initial) ? 1 : 0;
}

// Clear the region list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_ClearRegionList");
void CostumeCreator_ClearRegionList(void)
{
	COSTUME_UI_TRACE_FUNC();
	eaDestroyStruct(&g_CostumeEditState.eaFindRegions, parse_PCRegionRef);
	g_CostumeEditState.bUpdateLines = true;
}

// Add to region list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_AddRegion");
void CostumeCreator_AddRegion(const char *pchName)
{
	PCRegionRef *pRef = StructCreate(parse_PCRegionRef);
	COSTUME_UI_TRACE_FUNC();

	if (!pRef) return;
	if (!pchName)
	{
		StructDestroy(parse_PCRegionRef,pRef);
		return;
	}
	SET_HANDLE_FROM_STRING(g_hCostumeRegionDict, pchName, pRef->hRegion);
	if (!GET_REF(pRef->hRegion))
	{
		StructDestroy(parse_PCRegionRef,pRef);
		return;
	}
	eaPush(&g_CostumeEditState.eaFindRegions, pRef);
	g_CostumeEditState.bUpdateLines = true;
}

// Clear the scale group list
void CostumeCreator_ClearScaleGroupList(void)
{
	eaDestroyStruct(&g_CostumeEditState.eaFindScaleGroup, parse_CostumeUIScaleGroup);
	g_CostumeEditState.bUpdateLines = true;
}

void CostumeCreator_AddScaleGroup(const char *pchName)
{
	CostumeUIScaleGroup *pRef = StructCreate(parse_CostumeUIScaleGroup);

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

//0 = No show; 1 = Top of list; 2 = Between Pickers and Sliders; 3 = Bottom of List
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetOverlayList");
void CostumeCreator_SetOverlayList(const char *name)
{
	COSTUME_UI_TRACE_FUNC();
	if ((!name) || (!*name)) g_CostumeEditState.pchCostumeSet = NULL;
	g_CostumeEditState.pchCostumeSet = allocAddString(name);
}

static __forceinline bool CostumeUI_ValidateSelectedBone(NOCONST(PlayerCostume) *pCostume)
{
	PCBoneDef **eaValidSkeletonBones = NULL;
	PERFINFO_AUTO_START_FUNC();
	costumeTailor_GetValidBones(pCostume, GET_REF(pCostume->hSkeleton), NULL, NULL, GET_REF(pCostume->hSpecies), NULL, NULL, &eaValidSkeletonBones, CGVF_OMIT_EMPTY | CGVF_UNLOCK_ALL);
	if (eaFind(&eaValidSkeletonBones, g_CostumeEditState.pSelectedBone) < 0) {
		g_CostumeEditState.pSelectedBone = NULL;
	}
	if (g_CostumeEditState.pSelectedBone) {
		// pointer still valid, see if it's a changeable option
		PCRegion *pSelectedBoneRegion = GET_REF(g_CostumeEditState.pSelectedBone->hRegion);
		PCCategory *pSelectedBoneCategory = costumeTailor_GetCategoryForRegion((PlayerCostume *)pCostume, pSelectedBoneRegion);
		costumeTailor_GetValidBones(pCostume, GET_REF(pCostume->hSkeleton), pSelectedBoneRegion, pSelectedBoneCategory, GET_REF(pCostume->hSpecies), NULL, NULL, &eaValidSkeletonBones, CGVF_OMIT_EMPTY | CGVF_UNLOCK_ALL);
		if (eaFind(&eaValidSkeletonBones, g_CostumeEditState.pSelectedBone) < 0) {
			g_CostumeEditState.bValidSelectedBone = false;
		}
	}
	eaDestroy(&eaValidSkeletonBones);
	PERFINFO_AUTO_STOP_FUNC();
	return g_CostumeEditState.pSelectedBone != NULL && g_CostumeEditState.bValidSelectedBone;
}

void CostumeUI_UpdateLists(NOCONST(PlayerCostume) *pCostume, bool bUGC, bool bValidateSafeMode)
{
	PlayerCostume *pConstCostume = (PlayerCostume *)pCostume;
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	NOCONST(PCPart) *pMirrorPart = NULL;
	PCSkeletonDef *pSkel = GET_REF(pCostume->hSkeleton);
	PCBoneDef *pBone = NULL;
	PCBoneDef *pMirrorBone = NULL;
	PCBoneGroup *pBoneGroup = NULL;
	PCGeometryDef *pGeo = NULL;
	PCGeometryDef *pMirrorGeo = NULL;
	PCMaterialDef *pMat = NULL;
	PCMaterialDef *pMirrorMat = NULL;
	PCTextureDef *pTex = NULL;
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	S32 i, j;
	bool bMirrorMode, bIsMirrorGeo, bIsMirrorMat;
	NOCONST(PlayerCostume) *pCostumeCopy = NULL;
	bool bSelectedMirrorBone = false;
	Entity *pEnt = entActivePlayerPtr();
	Guild *pGuild = guild_GetGuild(pEnt);
	GameAccountDataExtract *pExtract;
	static PlayerCostume **eaFlatList = NULL;

	PERFINFO_AUTO_START_FUNC();

	eaClearFast(&eaFlatList);
	eaPush(&eaFlatList, &g_CostumeEditState.FlatUnlockedGeos);

	PERFINFO_AUTO_START("Populate Slot Type List", 1);
	if (g_CostumeEditState.pcSlotSet) {
		// Ensure that we have a valid SlotDef.
		PCSlotSet *pSlotSet = costumeLoad_GetSlotSet(g_CostumeEditState.pcSlotSet);
		if (pSlotSet && g_CostumeEditState.bExtraSlot) {
			g_CostumeEditState.pSlotDef = pSlotSet->pExtraSlotDef;
		} else if (pSlotSet) {
			for (i = eaSize(&pSlotSet->eaSlotDefs) - 1; i >= 0; --i) {
				if (pSlotSet->eaSlotDefs[i]->iSlotID == g_CostumeEditState.iSlotID) {
					g_CostumeEditState.pSlotDef = pSlotSet->eaSlotDefs[i];
					break;
				}
			}
		}
		if (g_CostumeEditState.pSlotDef) {
			costumeTailor_GetValidSlotTypes(pCostume, g_CostumeEditState.pSlotDef, pSpecies, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eaPowerFXBones, &g_CostumeEditState.eaSlotTypes, CGVF_OMIT_EMPTY);
			if (eaFind(&g_CostumeEditState.eaSlotTypes, g_CostumeEditState.pSlotType) < 0) {
				g_CostumeEditState.pSlotType = g_CostumeEditState.pSlotDef->pSlotType;
			}
		}
	} else {
		eaClearFast(&g_CostumeEditState.eaSlotTypes);
	}
	PERFINFO_AUTO_STOP();

	// Set working skeleton from costume
	SET_HANDLE_FROM_REFERENT(g_hCostumeSkeletonDict, GET_REF(pCostume->hSkeleton), g_CostumeEditState.hSkeleton);

	PERFINFO_AUTO_START("Populate Skeleton List", 1);
	// Populate skeleton list
	costumeTailor_GetValidSkeletons(pCostume, pSpecies, &g_CostumeEditState.eaSkeletons, false, true);
	if(!devassertmsg(eaSize(&g_CostumeEditState.eaSkeletons), "No skeletons found"))
		return;
	if (eaFind(&g_CostumeEditState.eaSkeletons, GET_REF(g_CostumeEditState.hSkeleton)) < 0)
	{
		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, g_CostumeEditState.eaSkeletons[0], pCostume->hSkeleton);
		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, g_CostumeEditState.eaSkeletons[0], g_CostumeEditState.hSkeleton);
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Populate Preset List", 1);
	// Populate preset list
	costumeTailor_GetValidPresets(pCostume, pSpecies, &g_CostumeEditState.eaPresets, true, g_CostumeEditState.eaUnlockedCostumes, false);
	if (eaSize(&g_CostumeEditState.eaSlotTypes)) {
		for (i=eaSize(&g_CostumeEditState.eaPresets)-1; i>=0; --i) {
			CostumePreset *pPreset = g_CostumeEditState.eaPresets[i];
			CostumePresetCategory *pCategory = pPreset->bOverrideExcludeValues ? NULL : GET_REF(g_CostumeEditState.eaPresets[i]->hPresetCategory);
			PCSlotType *pPresetSlotType = costumeLoad_GetSlotType(pPreset->pcSlotType);
			bool bExcludeSlotType = pCategory ? pCategory->bExcludeSlotType : pPreset->bExcludeSlotType;
			if (!bExcludeSlotType && pPresetSlotType && eaFind(&g_CostumeEditState.eaSlotTypes, pPresetSlotType) < 0) {
				eaRemove(&g_CostumeEditState.eaPresets, i);
			}
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Populate Stance List", 1);
	// Populate stance list
	costumeTailor_GetValidStances(pCostume, pSpecies, g_CostumeEditState.pSlotType, &g_CostumeEditState.eaStances, true, g_CostumeEditState.eaUnlockedCostumes, false);
	if(eaSize(&g_CostumeEditState.eaStances) > 0) {
		if (!pCostume->pcStance) {
			costumeTailor_PickValidStance(pCostume, pSpecies, g_CostumeEditState.pSlotType, g_CostumeEditState.eaUnlockedCostumes, false);
		}
		g_CostumeEditState.pStance = costumeTailor_GetStance(pCostume, pSpecies, g_CostumeEditState.pSlotType);
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Populate Voice List", 1);
	// Populate voice list
	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	costumeTailor_GetValidVoices(pCostume, pSpecies, &g_CostumeEditState.eaVoices, true, pExtract);
	if(eaSize(&g_CostumeEditState.eaVoices) > 0) {
		if (!GET_REF(pCostume->hVoice)) {
			costumeTailor_PickValidVoice(pCostume, pSpecies, pExtract);
		}
		g_CostumeEditState.pVoice = GET_REF(g_CostumeEditState.pCostume->hVoice);
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Populate Mood List", 1);
	// Populate mood list
	costumeTailor_GetValidMoods(&g_CostumeEditState.eaMoods, true);
	if(eaSize(&g_CostumeEditState.eaMoods) > 0) {
		if (!GET_REF(g_CostumeEditState.hMood) || (eaFind(&g_CostumeEditState.eaMoods, GET_REF(g_CostumeEditState.hMood)) == -1)) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeMoodDict, g_CostumeEditState.eaMoods[0], g_CostumeEditState.hMood);
		}
	} else {
		REMOVE_HANDLE(g_CostumeEditState.hMood);
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Populate Region List", 1);
	// Populate region list
	costumeTailor_GetValidRegions(pCostume, pSpecies, eaFlatList, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, &g_CostumeEditState.eaRegions, CGVF_OMIT_EMPTY | CGVF_SORT_DISPLAY | CGVF_REQUIRE_POWERFX);
	if (eaFind(&g_CostumeEditState.eaRegions, GET_REF(g_CostumeEditState.hRegion)) < 0)
	{
		if (eaSize(&g_CostumeEditState.eaRegions)) {
			SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, g_CostumeEditState.eaRegions[0], g_CostumeEditState.hRegion);
		} else {
			REMOVE_HANDLE(g_CostumeEditState.hRegion);
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Populate Body Scales", 1);
	// Populate body scale list
	costumeTailor_GetValidBodyScales(pCostume, pSpecies, &g_CostumeEditState.eaBodyScales, true);

	// Apply filtering to the lists
	if (eaSize(&g_CostumeEditState.eaBodyScalesInclude))
	{
		for (i = eaSize(&g_CostumeEditState.eaBodyScales) - 1; i >= 0; i--)
		{
			if (eaFind(&g_CostumeEditState.eaBodyScalesInclude, g_CostumeEditState.eaBodyScales[i]) < 0)
			{
				eaRemove(&g_CostumeEditState.eaBodyScales, i);
			}
		}
	}
	else if (eaSize(&g_CostumeEditState.eaBodyScalesExclude))
	{
		for (i = eaSize(&g_CostumeEditState.eaBodyScales) - 1; i >= 0; i--)
		{
			if (eaFind(&g_CostumeEditState.eaBodyScalesExclude, g_CostumeEditState.eaBodyScales[i]) >= 0)
			{
				eaRemove(&g_CostumeEditState.eaBodyScales, i);
			}
		}
	}
	for (i = eaSize(&g_CostumeEditState.eaBodyScales) - 1; i >= 0; i--)
	{
		if (!eaSize(&g_CostumeEditState.eaBodyScales[i]->eaValues))
		{
			F32 fMin, fMax;
			if(costumeTailor_GetOverrideBodyScale(pSkel, g_CostumeEditState.eaBodyScales[i]->pcName, pSpecies, g_CostumeEditState.pSlotType, &fMin, &fMax))
			{
				if (fMax <= fMin)
				{
					eaRemove(&g_CostumeEditState.eaBodyScales, i);
				}
			}
			else
			{
				float mn = 0, mx = 0;
				for (j = eaSize(&pSkel->eaBodyScaleInfo)-1; j >= 0; --j)
				{
					if (!strcmp(pSkel->eaBodyScaleInfo[j]->pcName,g_CostumeEditState.eaBodyScales[i]->pcName))
					{
						break;
					}
				}
				if (j >= 0)
				{
					if (pSkel->eafPlayerMinBodyScales && (eafSize(&pSkel->eafPlayerMinBodyScales) > j)) {
						mn = pSkel->eafPlayerMinBodyScales[j];
					}
					if (pSkel->eafPlayerMaxBodyScales && (eafSize(&pSkel->eafPlayerMaxBodyScales) > j)) {
						mx = pSkel->eafPlayerMaxBodyScales[j];
					}
				}
				if (mx <= mn)
				{
					eaRemove(&g_CostumeEditState.eaBodyScales, i);
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Populate Bone Scale List", 1);
	// Populate bone scale list
	eaClear(&g_CostumeEditState.eaBoneScales);
	if (g_CostumeEditState.pcBoneScaleGroup) {
		for(i=eaSize(&pSkel->eaScaleInfoGroups)-1; i>=0; --i) {
			if (stricmp(g_CostumeEditState.pcBoneScaleGroup, pSkel->eaScaleInfoGroups[i]->pcName) == 0) {
				PCScaleInfoGroup *pGroup = pSkel->eaScaleInfoGroups[i];
				for(j=0; j<eaSize(&pGroup->eaScaleInfo); ++j) {
					PCScaleInfo *pInfo = pGroup->eaScaleInfo[j];
					if (pInfo->eRestriction & (kPCRestriction_Player|kPCRestriction_Player_Initial)) {
						eaPush(&g_CostumeEditState.eaBoneScales, pInfo);
					}
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Populate Category List", 1);
	// Populate category list
	eaClear(&g_CostumeEditState.eaCategories);
	costumeTailor_GetValidCategories(pCostume, GET_REF(g_CostumeEditState.hRegion), pSpecies, eaFlatList, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, &g_CostumeEditState.eaCategories, CGVF_OMIT_EMPTY | CGVF_SORT_DISPLAY | CGVF_REQUIRE_POWERFX);
	{
		PCCategory *pCategory = costumeTailor_GetCategoryForRegion(pConstCostume, GET_REF(g_CostumeEditState.hRegion));
		if (eaFind(&g_CostumeEditState.eaCategories, pCategory) < 0)
		{
			if (eaSize(&g_CostumeEditState.eaCategories)) {
				SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, g_CostumeEditState.eaCategories[0], g_CostumeEditState.hCategory);
				costumeTailor_SetRegionCategory(pCostume, GET_REF(g_CostumeEditState.hRegion), g_CostumeEditState.eaCategories[0]);
			} else {
				REMOVE_HANDLE(g_CostumeEditState.hCategory);
			}
		}
		else
		{
			SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, pCategory, g_CostumeEditState.hCategory);
		}
	}
	PERFINFO_AUTO_STOP();


	PERFINFO_AUTO_START("Populate Bone List", 1);
	// Populate bone list
	eaClear(&g_CostumeEditState.eaBones);
	eaClear(&g_CostumeEditState.eaAllBones);
	costumeTailor_FillAllBones(pCostume, pSpecies, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, true, false, true);
	if (GET_REF(g_CostumeEditState.hRegion) && GET_REF(g_CostumeEditState.hCategory) && s_bRegionCategoryBoneFilter) {
		costumeTailor_GetValidBones(pCostume, GET_REF(pCostume->hSkeleton), GET_REF(g_CostumeEditState.hRegion), GET_REF(g_CostumeEditState.hCategory), pSpecies, eaFlatList, g_CostumeEditState.eaPowerFXBones, &g_CostumeEditState.eaBones, (g_MirrorSelectMode ? CGVF_MIRROR_MODE : 0) | (g_GroupSelectMode ? CGVF_BONE_GROUP_MODE : 0) | CGVF_OMIT_EMPTY | (g_bOmitHasOnlyOne ? CGVF_OMIT_ONLY_ONE : 0) | (g_bCountNone ? CGVF_COUNT_NONE : 0) | CGVF_SORT_DISPLAY | CGVF_REQUIRE_POWERFX | (!pGuild ? CGVF_EXCLUDE_GUILD_EMBLEM : 0));
		costumeTailor_GetValidBones(pCostume, GET_REF(pCostume->hSkeleton), GET_REF(g_CostumeEditState.hRegion), GET_REF(g_CostumeEditState.hCategory), pSpecies, eaFlatList, g_CostumeEditState.eaPowerFXBones, &g_CostumeEditState.eaAllBones, CGVF_OMIT_EMPTY | (g_bOmitHasOnlyOne ? CGVF_OMIT_ONLY_ONE : 0) | (g_bCountNone ? CGVF_COUNT_NONE : 0) | CGVF_SORT_DISPLAY | CGVF_REQUIRE_POWERFX | (!pGuild ? CGVF_EXCLUDE_GUILD_EMBLEM : 0));
	}
	else
	{
		costumeTailor_GetValidBones(pCostume, GET_REF(pCostume->hSkeleton), NULL, NULL, pSpecies, eaFlatList, g_CostumeEditState.eaPowerFXBones, &g_CostumeEditState.eaBones, (g_MirrorSelectMode ? CGVF_MIRROR_MODE : 0) | (g_GroupSelectMode ? CGVF_BONE_GROUP_MODE : 0) | CGVF_OMIT_EMPTY | (g_bOmitHasOnlyOne ? CGVF_OMIT_ONLY_ONE : 0) | (g_bCountNone ? CGVF_COUNT_NONE : 0) | CGVF_SORT_DISPLAY | CGVF_REQUIRE_POWERFX | (!pGuild ? CGVF_EXCLUDE_GUILD_EMBLEM : 0));
		costumeTailor_GetValidBones(pCostume, GET_REF(pCostume->hSkeleton), NULL, NULL, pSpecies, eaFlatList, g_CostumeEditState.eaPowerFXBones, &g_CostumeEditState.eaAllBones, CGVF_OMIT_EMPTY | (g_bOmitHasOnlyOne ? CGVF_OMIT_ONLY_ONE : 0) | (g_bCountNone ? CGVF_COUNT_NONE : 0) | CGVF_SORT_DISPLAY | CGVF_REQUIRE_POWERFX | (!pGuild ? CGVF_EXCLUDE_GUILD_EMBLEM : 0));
	}

	CostumeUI_FilterBoneList(&g_CostumeEditState.eaBones, g_CostumeEditState.eaIncludeBones, g_CostumeEditState.eaExcludeBones);

	if (s_bAddNoneToBoneList)
	{
		if (eaSize(&g_CostumeEditState.eaBones) > 0 && strcmp(g_CostumeEditState.eaBones[0]->pcName,"None"))
		{
			PCBoneDef *pDef = RefSystem_ReferentFromString(g_hCostumeBoneDict, "None");
			if (pDef) {
				eaInsert(&g_CostumeEditState.eaBones, pDef, 0);
			}
		}
		else if (eaSize(&g_CostumeEditState.eaBones) == 0)
		{
			PCBoneDef *pDef = RefSystem_ReferentFromString(g_hCostumeBoneDict, "None");
			if (pDef) {
				eaPush(&g_CostumeEditState.eaBones, pDef);
			}
		}
	}

	for (i = eaSize(&g_CostumeEditState.eaBoneValidValues) - 1; i >= 0; i--) {
		if (!costumeTailor_GetPartByBone(pCostume, GET_REF(g_CostumeEditState.eaBoneValidValues[i]->hBone), NULL)) {
			StructDestroy(parse_CostumeBoneValidValues, eaRemove(&g_CostumeEditState.eaBoneValidValues, i));
		}
	}
	PERFINFO_AUTO_STOP();

	CostumeUI_ValidateAllParts(pCostume, bUGC, bValidateSafeMode);
	eaClearFast(&eaFlatList);
	eaPush(&eaFlatList, &g_CostumeEditState.FlatUnlockedGeos);

	PERFINFO_AUTO_START("Validate Selected Bone", 1);
	// Make sure the selected bone is valid for what we're editing...
	{
		bool bFound = false;
		if (g_CostumeEditState.bAllowSelectFromAllBones) {
			for (i = 0; i < eaSize(&g_CostumeEditState.eaAllBones) && !bFound; i++) {
				bFound = (g_CostumeEditState.eaAllBones[i] == GET_REF(g_CostumeEditState.hBone));
			}
		} else {
			for (i = 0; i < eaSize(&g_CostumeEditState.eaBones) && !bFound; i++) {
				bFound = (g_CostumeEditState.eaBones[i] == GET_REF(g_CostumeEditState.hBone));
			}
		}
		if (g_MirrorSelectMode && GET_REF(g_CostumeEditState.hBone) && GET_REF(GET_REF(g_CostumeEditState.hBone)->hMirrorBone)) {
			pMirrorBone = GET_REF(GET_REF(g_CostumeEditState.hBone)->hMirrorBone);
			if (g_CostumeEditState.bAllowSelectFromAllBones && eaFind(&g_CostumeEditState.eaAllBones, pMirrorBone) < 0) {
				pMirrorBone = NULL;
			} else if (eaFind(&g_CostumeEditState.eaBones, pMirrorBone) < 0) {
				pMirrorBone = NULL;
			}
			if (!bFound && pMirrorBone) {
				for (i = 0; i < eaSize(&pSkel->eaRequiredBoneDefs); i++) {
					if (GET_REF(pSkel->eaRequiredBoneDefs[i]->hBone) == GET_REF(g_CostumeEditState.hBone)) {
						bFound = true;
						break;
					} else if (GET_REF(pSkel->eaRequiredBoneDefs[i]->hBone) == pMirrorBone) {
						// The mirror bone appears in the list before the current selection, normalize to the mirror bone
						bFound = true;
						bSelectedMirrorBone = true;
						SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, pMirrorBone, g_CostumeEditState.hBone);
						break;
					}
				}
			}
			if (!bFound && pMirrorBone) {
				for (i = 0; i < eaSize(&pSkel->eaOptionalBoneDefs); i++) {
					if (GET_REF(pSkel->eaOptionalBoneDefs[i]->hBone) == GET_REF(g_CostumeEditState.hBone)) {
						bFound = true;
						break;
					} else if (GET_REF(pSkel->eaOptionalBoneDefs[i]->hBone) == pMirrorBone) {
						// The mirror bone appears in the list before the current selection, normalize to the mirror bone
						bFound = true;
						bSelectedMirrorBone = true;
						SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, pMirrorBone, g_CostumeEditState.hBone);
						break;
					}
				}
			}
			pMirrorBone = NULL;
		}
		if (!bFound) {
			if (eaSize(&g_CostumeEditState.eaBones)) {
				SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, g_CostumeEditState.eaBones[0], g_CostumeEditState.hBone);
			} else {
				REMOVE_HANDLE(g_CostumeEditState.hBone);
			}
		}
		pPart = NULL;
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Setup Bone & Part", 1);
	// Set up pBone and pMirrorBone
	pBone = GET_REF(g_CostumeEditState.hBone);
	pMirrorBone = g_MirrorSelectMode && pBone ? GET_REF(pBone->hMirrorBone) : NULL;
	if (pMirrorBone) {
		for(i=eaSize(&g_CostumeEditState.eaAllBones)-1; i>=0; --i) {
			if (pMirrorBone == g_CostumeEditState.eaAllBones[i]) {
				break;
			}
		}
		if (i < 0) {
			// Mirror bone exists but isn't currently legal, so clear it
			pMirrorBone = NULL;
		}
	}

	// Make sure the part is for the current bone...
	if (pPart && (GET_REF(pPart->hBoneDef) != pBone) && 
		(!pMirrorBone || (GET_REF(pPart->hBoneDef) != pMirrorBone) || (pPart->eEditMode == kPCEditMode_Right))) {
		pPart = NULL;
	}
	if (!pPart) {
		for (i = 0; i < eaSize(&pCostume->eaParts) && !pPart; i++) {
			if (GET_REF(pCostume->eaParts[i]->hBoneDef) == pBone) {
				pPart = pCostume->eaParts[i];
			}
		}
		if (pMirrorBone && (pPart->eEditMode == kPCEditMode_Right)) {
			// If mirror bone and part is set to Right, then find mirror part for actual data
			for (i = 0; i < eaSize(&pCostume->eaParts); i++) {
				if (GET_REF(pCostume->eaParts[i]->hBoneDef) == pMirrorBone) {
					pPart = pCostume->eaParts[i];
					break;
				}
			}
			pPart->eEditMode = kPCEditMode_Right;
		}
		g_CostumeEditState.pPart = pPart;
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Setup Bone Group Index", 1);
	//Set up iBoneGroupIndex
	{
		int k;
		if (pSkel)
		{
			for (i = 0; i < eaSize(&pCostume->eaParts); i++)
			{
				PCBoneDef *bone = GET_REF(pCostume->eaParts[i]->hBoneDef);
				pCostume->eaParts[i]->iBoneGroupIndex = -1;
				for(j=eaSize(&pSkel->eaBoneGroups)-1; j>=0; --j)
				{
					if (!(pSkel->eaBoneGroups[j]->eBoneGroupFlags & kPCBoneGroupFlags_MatchGeos)) continue;
					for(k=eaSize(&pSkel->eaBoneGroups[j]->eaBoneInGroup)-1; k>=0; --k)
					{
						if (bone == GET_REF(pSkel->eaBoneGroups[j]->eaBoneInGroup[k]->hBone))
						{
							pCostume->eaParts[i]->iBoneGroupIndex = j;
							break;
						}
					}
					if (k >= 0) break;
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Validate Layer Choice", 1);
	// Validate layer choice
	eaClear(&g_CostumeEditState.eaLayers);
	if (pMirrorBone) {
		PCLayer *pSelfLayer = GET_REF(pBone->hSelfLayer);
		PCLayer *pMergeLayer = GET_REF(pBone->hMergeLayer);
		PCLayer *pOtherLayer = GET_REF(pMirrorBone->hSelfLayer);

		if (pMergeLayer && pSelfLayer && pOtherLayer) {
			eaPush(&g_CostumeEditState.eaLayers, pSelfLayer);
			eaPush(&g_CostumeEditState.eaLayers, pMergeLayer);
			eaPush(&g_CostumeEditState.eaLayers, pOtherLayer);

			if (pPart->eEditMode == kPCEditMode_Both) {
				g_CostumeEditState.pCurrentLayer = pMergeLayer;
			} else if (pPart->eEditMode == kPCEditMode_Left) {
				g_CostumeEditState.pCurrentLayer = pSelfLayer;
			} else if (pPart->eEditMode == kPCEditMode_Right) {
				g_CostumeEditState.pCurrentLayer = pOtherLayer;
			}
		} else {
			pPart->eEditMode = kPCEditMode_Both;
			g_CostumeEditState.pCurrentLayer = NULL;
		}
	} else {
		g_CostumeEditState.pCurrentLayer = NULL;
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Get Mirror Part", 1);
	bMirrorMode = g_MirrorSelectMode && pMirrorBone && (g_CostumeEditState.pPart->eEditMode == kPCEditMode_Both);
	if (bMirrorMode) {
		pMirrorPart = costumeTailor_GetMirrorPart(pCostume, g_CostumeEditState.pPart);
		if (!pMirrorPart) {
			bMirrorMode = false;
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Validate Geometry Choice", 1);
	// Validate geometry choice
	eaClear(&g_CostumeEditState.eaGeos);
	if (pPart) {
		costumeTailor_GetValidGeos(pCostume, GET_REF(pCostume->hSkeleton), GET_REF(pPart->hBoneDef), GET_REF(g_CostumeEditState.hCategory), pSpecies, eaFlatList, &g_CostumeEditState.eaGeos, bMirrorMode, false /*g_GroupSelectMode && pPart->iBoneGroupIndex >= 0*/, true, g_CostumeEditState.bUnlockAll);
	}
	if (pCostume->eCostumeType != kPCCostumeType_Unrestricted) {
		if (pMirrorPart) {
			PCGeometryDef **eaTempGeos = NULL;
			// If in mirror mode, need to validate each on separate criteria
			costumeTailor_GetValidGeos(pCostume, GET_REF(pCostume->hSkeleton), GET_REF(pPart->hBoneDef), GET_REF(g_CostumeEditState.hCategory), pSpecies, eaFlatList, &eaTempGeos, false, false, true, g_CostumeEditState.bUnlockAll);
			costumeTailor_PickValidGeometry(pCostume, pPart, pSpecies, eaTempGeos, eaFlatList, g_CostumeEditState.bUnlockAll);
			eaDestroy(&eaTempGeos);
			costumeTailor_GetValidGeos(pCostume, GET_REF(pCostume->hSkeleton), GET_REF(pMirrorPart->hBoneDef), GET_REF(g_CostumeEditState.hCategory), pSpecies, eaFlatList, &eaTempGeos, false, false, true, g_CostumeEditState.bUnlockAll);
			costumeTailor_PickValidGeometry(pCostume, pMirrorPart, pSpecies, eaTempGeos, eaFlatList, g_CostumeEditState.bUnlockAll);
			eaDestroy(&eaTempGeos);
		} else {
			// When not in mirror mode, the main list is the right one for validation
			costumeTailor_PickValidGeometry(pCostume, pPart, pSpecies, g_CostumeEditState.eaGeos, eaFlatList, g_CostumeEditState.bUnlockAll);
		}
	}
	if (pPart) {
		pGeo = GET_REF(pPart->hGeoDef);
	}
	for (i = eaSize(&g_CostumeEditState.eaBoneValidValues) - 1; i >= 0; i--) {
		PCBoneDef *pValidBone = GET_REF(g_CostumeEditState.eaBoneValidValues[i]->hBone);
		NOCONST(PCPart) *pValidPart = costumeTailor_GetPartByBone(pCostume, pValidBone, NULL);
		PCCategory *pValidCategory = pValidBone ? costumeTailor_GetCategoryForRegion(CONTAINER_RECONST(PlayerCostume, pCostume), GET_REF(pValidBone->hRegion)) : NULL;
		if (pValidPart) {
			costumeTailor_GetValidGeos(pCostume, GET_REF(pCostume->hSkeleton), GET_REF(g_CostumeEditState.eaBoneValidValues[i]->hBone), pValidCategory, pSpecies, eaFlatList, &g_CostumeEditState.eaBoneValidValues[i]->eaGeos, false, false /*g_GroupSelectMode && pPart->iBoneGroupIndex >= 0*/, true, g_CostumeEditState.bUnlockAll);
		} else {
			CostumeBoneValidValues_ResetLists(g_CostumeEditState.eaBoneValidValues[i]);
		}
	}
	PERFINFO_AUTO_STOP();

	bIsMirrorGeo = bMirrorMode && pMirrorPart && costumeTailor_IsMirrorGeometry(pGeo,GET_REF(pMirrorPart->hGeoDef));
	if (bMirrorMode && pMirrorPart) {
		pMirrorGeo = GET_REF(pMirrorPart->hGeoDef);
	}

	PERFINFO_AUTO_START("Validate Cloth Layer Choice", 1);
	if (pGeo && pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pPart->pClothLayer && GET_REF(pBone->hMainLayerFront) && GET_REF(pBone->hMainLayerBack)) {
		PCLayer *pBothLayer = GET_REF(pBone->hMainLayerBoth);
		PCLayer *pFrontLayer = GET_REF(pBone->hMainLayerFront);
		PCLayer *pBackLayer = GET_REF(pBone->hMainLayerBack);

		if (pBothLayer && pFrontLayer && pBackLayer) {
			eaPush(&g_CostumeEditState.eaLayers, pBothLayer);
			eaPush(&g_CostumeEditState.eaLayers, pFrontLayer);
			eaPush(&g_CostumeEditState.eaLayers, pBackLayer);

			if (pPart->eEditMode == kPCEditMode_Both) {
				g_CostumeEditState.pCurrentLayer = pBothLayer;
			} else if (pPart->eEditMode == kPCEditMode_Front) {
				g_CostumeEditState.pCurrentLayer = pFrontLayer;
			} else if (pPart->eEditMode == kPCEditMode_Back) {
				g_CostumeEditState.pCurrentLayer = pBackLayer;
				assert(pPart->pClothLayer);
				pPart = pPart->pClothLayer;
				g_CostumeEditState.pPart = pPart;
			}
		} else {
			pPart->eEditMode = kPCEditMode_Both;
			g_CostumeEditState.pCurrentLayer = NULL;
		}
	} else if (pPart && ((pPart->eEditMode == kPCEditMode_Front) || (pPart->eEditMode == kPCEditMode_Back))) {
		pPart->eEditMode = kPCEditMode_Both;
	}

	// Reset mode if no layers
	if (!g_CostumeEditState.pCurrentLayer && pPart) {
		pPart->eEditMode = kPCEditMode_Both;
	}
	PERFINFO_AUTO_STOP();

	pBoneGroup = (g_CostumeEditState.pPart && GET_REF(g_CostumeEditState.hSkeleton) && g_CostumeEditState.pPart->iBoneGroupIndex >= 0 ? GET_REF(g_CostumeEditState.hSkeleton)->eaBoneGroups[g_CostumeEditState.pPart->iBoneGroupIndex] : NULL);

	PERFINFO_AUTO_START("Populate Material List", 1);
	// Populate materials list
	eaClear(&g_CostumeEditState.eaMats);
	if (pGeo && (!bMirrorMode || bIsMirrorGeo)) {
		costumeTailor_GetValidMaterials(pCostume, pGeo, pSpecies, pMirrorGeo, NULL /*pBoneGroup*/, g_CostumeEditState.eaUnlockedCostumes, &g_CostumeEditState.eaMats, bIsMirrorGeo, true, g_CostumeEditState.bUnlockAll);
	}
	for (i = eaSize(&g_CostumeEditState.eaBoneValidValues) - 1; i >= 0; i--) {
		NOCONST(PCPart) *pValidPart = costumeTailor_GetPartByBone(pCostume, GET_REF(g_CostumeEditState.eaBoneValidValues[i]->hBone), NULL);
		if (pValidPart) {
			costumeTailor_GetValidMaterials(pCostume, GET_REF(pValidPart->hGeoDef), pSpecies, NULL, NULL /*pBoneGroup*/, g_CostumeEditState.eaUnlockedCostumes, &g_CostumeEditState.eaBoneValidValues[i]->eaMats, false, true, g_CostumeEditState.bUnlockAll);
		} else {
			CostumeBoneValidValues_ResetLists(g_CostumeEditState.eaBoneValidValues[i]);
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Validate Material Choice", 1);
	// Validate material choice
	if (pCostume->eCostumeType != kPCCostumeType_Unrestricted) {
		if (pMirrorPart) {
			PCMaterialDef **eaTempMats = NULL;
			// If in mirror mode, need to validate each on separate criteria
			costumeTailor_GetValidMaterials(pCostume, pGeo, pSpecies, NULL, NULL, g_CostumeEditState.eaUnlockedCostumes, &eaTempMats, false, true, g_CostumeEditState.bUnlockAll);
			costumeTailor_PickValidMaterial(pCostume, pSpecies, pPart, g_CostumeEditState.eaUnlockedCostumes, eaTempMats, g_CostumeEditState.bUnlockAll, true);
			eaDestroy(&eaTempMats);
			costumeTailor_GetValidMaterials(pCostume, pMirrorGeo, pSpecies, NULL, NULL, g_CostumeEditState.eaUnlockedCostumes, &eaTempMats, false, true, g_CostumeEditState.bUnlockAll);
			costumeTailor_PickValidMaterial(pCostume, pSpecies, pMirrorPart, g_CostumeEditState.eaUnlockedCostumes, eaTempMats, g_CostumeEditState.bUnlockAll, false);
			eaDestroy(&eaTempMats);
		} else {
			costumeTailor_PickValidMaterial(pCostume, pSpecies, pPart, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eaMats, g_CostumeEditState.bUnlockAll, true);
		}
	}
	if (pPart) {
		pMat = GET_REF(pPart->hMatDef);
	}
	PERFINFO_AUTO_STOP();

	bIsMirrorMat = bMirrorMode && bIsMirrorGeo && pMirrorPart && costumeTailor_IsMirrorMaterial(pMat,GET_REF(pMirrorPart->hMatDef));
	if (bMirrorMode && pMirrorPart) {
		pMirrorMat = GET_REF(pMirrorPart->hMatDef);
	}

	PERFINFO_AUTO_START("Populate Texture Lists", 1);
	// Populate texture lists
	eaClear(&g_CostumeEditState.eaPatternTex);
	eaClear(&g_CostumeEditState.eaDetailTex);
	eaClear(&g_CostumeEditState.eaSpecularTex);
	eaClear(&g_CostumeEditState.eaDiffuseTex);
	eaClear(&g_CostumeEditState.eaMovableTex);
	if (pMat && (!bMirrorMode || bIsMirrorMat)) {
		costumeTailor_GetValidTextures(pCostume, pMat, pSpecies, pMirrorMat, NULL /*pBoneGroup*/, pGeo, pMirrorGeo, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Pattern, &g_CostumeEditState.eaPatternTex, bIsMirrorMat, true, g_CostumeEditState.bUnlockAll);
		costumeTailor_GetValidTextures(pCostume, pMat, pSpecies, pMirrorMat, NULL /*pBoneGroup*/, pGeo, pMirrorGeo, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Detail, &g_CostumeEditState.eaDetailTex, bIsMirrorMat, true, g_CostumeEditState.bUnlockAll);
		costumeTailor_GetValidTextures(pCostume, pMat, pSpecies, pMirrorMat, NULL /*pBoneGroup*/, pGeo, pMirrorGeo, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Specular, &g_CostumeEditState.eaSpecularTex, bIsMirrorMat, true, g_CostumeEditState.bUnlockAll);
		costumeTailor_GetValidTextures(pCostume, pMat, pSpecies, pMirrorMat, NULL /*pBoneGroup*/, pGeo, pMirrorGeo, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Diffuse, &g_CostumeEditState.eaDiffuseTex, bIsMirrorMat, true, g_CostumeEditState.bUnlockAll);
		costumeTailor_GetValidTextures(pCostume, pMat, pSpecies, pMirrorMat, NULL /*pBoneGroup*/, pGeo, pMirrorGeo, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Movable, &g_CostumeEditState.eaMovableTex, bIsMirrorMat, true, g_CostumeEditState.bUnlockAll);
	}
	for (i = eaSize(&g_CostumeEditState.eaBoneValidValues) - 1; i >= 0; i--) {
		NOCONST(PCPart) *pValidPart = costumeTailor_GetPartByBone(pCostume, GET_REF(g_CostumeEditState.eaBoneValidValues[i]->hBone), NULL);
		if (pValidPart) {
			costumeTailor_GetValidTextures(pCostume, GET_REF(pValidPart->hMatDef), pSpecies, NULL, NULL /*pBoneGroup*/, GET_REF(pValidPart->hGeoDef), NULL, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Pattern, &g_CostumeEditState.eaPatternTex, false, true, g_CostumeEditState.bUnlockAll);
			costumeTailor_GetValidTextures(pCostume, GET_REF(pValidPart->hMatDef), pSpecies, NULL, NULL /*pBoneGroup*/, GET_REF(pValidPart->hGeoDef), NULL, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Detail, &g_CostumeEditState.eaDetailTex, false, true, g_CostumeEditState.bUnlockAll);
			costumeTailor_GetValidTextures(pCostume, GET_REF(pValidPart->hMatDef), pSpecies, NULL, NULL /*pBoneGroup*/, GET_REF(pValidPart->hGeoDef), NULL, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Specular, &g_CostumeEditState.eaSpecularTex, false, true, g_CostumeEditState.bUnlockAll);
			costumeTailor_GetValidTextures(pCostume, GET_REF(pValidPart->hMatDef), pSpecies, NULL, NULL /*pBoneGroup*/, GET_REF(pValidPart->hGeoDef), NULL, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Diffuse, &g_CostumeEditState.eaDiffuseTex, false, true, g_CostumeEditState.bUnlockAll);
			costumeTailor_GetValidTextures(pCostume, GET_REF(pValidPart->hMatDef), pSpecies, NULL, NULL /*pBoneGroup*/, GET_REF(pValidPart->hGeoDef), NULL, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Movable, &g_CostumeEditState.eaMovableTex, false, true, g_CostumeEditState.bUnlockAll);
		} else {
			CostumeBoneValidValues_ResetLists(g_CostumeEditState.eaBoneValidValues[i]);
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Make Part Valid",1);
	if (pPart) {
		PCTextureDef **eaTempTexs = NULL;
		PCTextureType eFlags = 0;
		PCTextureType eMirrorFlags = 0;
		bool bGuildTexture = costumeTailor_PartHasGuildEmblem(pPart, pGuild);

		if (pCostume->eCostumeType != kPCCostumeType_Unrestricted) {
			if (pMirrorPart) {
				costumeTailor_GetValidTextures(pCostume, pMat, pSpecies, NULL, NULL, pGeo, NULL, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Pattern, &eaTempTexs, false, true, g_CostumeEditState.bUnlockAll);
				costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Pattern, eaTempTexs);
				eaDestroy(&eaTempTexs);
				costumeTailor_GetValidTextures(pCostume, pMirrorMat, pSpecies, NULL, NULL, pMirrorGeo, NULL, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Pattern, &eaTempTexs, false, true, g_CostumeEditState.bUnlockAll);
				costumeTailor_PickValidTexture(pCostume, pMirrorPart, pSpecies, kPCTextureType_Pattern, eaTempTexs);
				eaDestroy(&eaTempTexs);
			} else if (!bGuildTexture) {
				costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Pattern, g_CostumeEditState.eaPatternTex);
			}
		}
		if (pMirrorPart)
			eMirrorFlags |= costumeTailor_GetTextureFlags(GET_REF(pMirrorPart->hPatternTexture));

		eFlags |= costumeTailor_GetTextureFlags(GET_REF(pPart->hPatternTexture));

		if ((eFlags & kPCTextureType_Detail) || (eMirrorFlags & kPCTextureType_Detail))
			eaClear(&g_CostumeEditState.eaDetailTex);

		if (pCostume->eCostumeType != kPCCostumeType_Unrestricted) {
			if (pMirrorPart) {
				if ((eFlags & kPCTextureType_Detail) == 0) {
					costumeTailor_GetValidTextures(pCostume, pMat, pSpecies, NULL, NULL, pGeo, NULL, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Detail, &eaTempTexs, false, true, g_CostumeEditState.bUnlockAll);
				}
				costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Detail, eaTempTexs);
				eaDestroy(&eaTempTexs);
				if ((eMirrorFlags & kPCTextureType_Detail) == 0) {
					costumeTailor_GetValidTextures(pCostume, pMirrorMat, pSpecies, NULL, NULL, pMirrorGeo, NULL, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Detail, &eaTempTexs, false, true, g_CostumeEditState.bUnlockAll);
				}
				costumeTailor_PickValidTexture(pCostume, pMirrorPart, pSpecies, kPCTextureType_Detail, eaTempTexs);
				eaDestroy(&eaTempTexs);
			} else if (!bGuildTexture) {
				if ((eFlags & kPCTextureType_Detail)) {
					costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Detail, eaTempTexs);
					eaDestroy(&eaTempTexs);
				}
				else {
					costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Detail, g_CostumeEditState.eaDetailTex);
				}
			}
		}
		if (pMirrorPart)
			eMirrorFlags |= costumeTailor_GetTextureFlags(GET_REF(pMirrorPart->hDetailTexture));

		eFlags |= costumeTailor_GetTextureFlags(GET_REF(pPart->hDetailTexture));

		if ((eFlags & kPCTextureType_Specular) || (eMirrorFlags & kPCTextureType_Specular))
			eaClear(&g_CostumeEditState.eaSpecularTex);

		if (pCostume->eCostumeType != kPCCostumeType_Unrestricted) {
			if (pMirrorPart) {
				if ((eFlags & kPCTextureType_Specular) == 0) {
					costumeTailor_GetValidTextures(pCostume, pMat, pSpecies, NULL, NULL, pGeo, NULL, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Specular, &eaTempTexs, false, true, g_CostumeEditState.bUnlockAll);
				}
				costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Specular, eaTempTexs);
				eaDestroy(&eaTempTexs);
				if ((eMirrorFlags & kPCTextureType_Specular) == 0) {
					costumeTailor_GetValidTextures(pCostume, pMirrorMat, pSpecies, NULL, NULL, pMirrorGeo, NULL, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Specular, &eaTempTexs, false, true, g_CostumeEditState.bUnlockAll);
				}
				costumeTailor_PickValidTexture(pCostume, pMirrorPart, pSpecies, kPCTextureType_Specular, eaTempTexs);
				eaDestroy(&eaTempTexs);
			} else {
				if ((eFlags & kPCTextureType_Specular) == 0) {
					costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Specular, g_CostumeEditState.eaSpecularTex);
				}
			}
		}
		if (pMirrorPart)
			eMirrorFlags |= costumeTailor_GetTextureFlags(GET_REF(pMirrorPart->hSpecularTexture));

		eFlags |= costumeTailor_GetTextureFlags(GET_REF(pPart->hSpecularTexture));

		if ((eFlags & kPCTextureType_Diffuse) || (eMirrorFlags & kPCTextureType_Diffuse))
			eaClear(&g_CostumeEditState.eaDiffuseTex);

		if (pCostume->eCostumeType != kPCCostumeType_Unrestricted) {
			if (pMirrorPart) {
				if ((eFlags & kPCTextureType_Diffuse) == 0) {
					costumeTailor_GetValidTextures(pCostume, pMat, pSpecies, NULL, NULL, pGeo, NULL, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Diffuse, &eaTempTexs, false, true, g_CostumeEditState.bUnlockAll);
				}
				costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Diffuse, eaTempTexs);
				eaDestroy(&eaTempTexs);
				if ((eMirrorFlags & kPCTextureType_Diffuse) == 0) {
					costumeTailor_GetValidTextures(pCostume, pMirrorMat, pSpecies, NULL, NULL, pMirrorGeo, NULL, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Diffuse, &eaTempTexs, false, true, g_CostumeEditState.bUnlockAll);
				}
				costumeTailor_PickValidTexture(pCostume, pMirrorPart, pSpecies, kPCTextureType_Diffuse, eaTempTexs);
				eaDestroy(&eaTempTexs);
			} else {
				if ((eFlags & kPCTextureType_Diffuse)) {
					costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Diffuse, eaTempTexs);
					eaDestroy(&eaTempTexs);
				}
				else {
					costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Diffuse, g_CostumeEditState.eaDiffuseTex);
				}
			}
		}

		if ((eFlags & kPCTextureType_Movable) || (eMirrorFlags & kPCTextureType_Movable))
			eaClear(&g_CostumeEditState.eaMovableTex);

		if (pCostume->eCostumeType != kPCCostumeType_Unrestricted) {
			if (pMirrorPart) {
				if ((eFlags & kPCTextureType_Movable) == 0) {
					costumeTailor_GetValidTextures(pCostume, pMat, pSpecies, NULL, NULL, pGeo, NULL, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Movable, &eaTempTexs, false, true, g_CostumeEditState.bUnlockAll);
				}
				costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Movable, eaTempTexs);
				eaDestroy(&eaTempTexs);
				if ((eMirrorFlags & kPCTextureType_Movable) == 0) {
					costumeTailor_GetValidTextures(pCostume, pMirrorMat, pSpecies, NULL, NULL, pMirrorGeo, NULL, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Movable, &eaTempTexs, false, true, g_CostumeEditState.bUnlockAll);
				}
				costumeTailor_PickValidTexture(pCostume, pMirrorPart, pSpecies, kPCTextureType_Movable, eaTempTexs);
				eaDestroy(&eaTempTexs);
			} else {
				if (!bGuildTexture) {
					if ((eFlags & kPCTextureType_Movable)) {
						costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Movable, eaTempTexs);
						eaDestroy(&eaTempTexs);
					}
					else {
						costumeTailor_PickValidTexture(pCostume, pPart, pSpecies, kPCTextureType_Movable, g_CostumeEditState.eaMovableTex);
					}
				}
			}
		}

		SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, GET_REF(pPart->hGeoDef), g_CostumeEditState.hGeometry);
		SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, GET_REF(pPart->hMatDef), g_CostumeEditState.hMaterial);
		SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, GET_REF(pPart->hPatternTexture), g_CostumeEditState.hPattern);
		SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, GET_REF(pPart->hDetailTexture), g_CostumeEditState.hDetail);
		SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, GET_REF(pPart->hSpecularTexture), g_CostumeEditState.hSpecular);
		SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, GET_REF(pPart->hDiffuseTexture), g_CostumeEditState.hDiffuse);
		if (pPart->pMovableTexture)
			SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, GET_REF(pPart->pMovableTexture->hMovableTexture), g_CostumeEditState.hMovable);
	} else {
		REMOVE_HANDLE(g_CostumeEditState.hGeometry);
		REMOVE_HANDLE(g_CostumeEditState.hMaterial);
		REMOVE_HANDLE(g_CostumeEditState.hPattern);
		REMOVE_HANDLE(g_CostumeEditState.hDetail);
		REMOVE_HANDLE(g_CostumeEditState.hSpecular);
		REMOVE_HANDLE(g_CostumeEditState.hDiffuse);
		REMOVE_HANDLE(g_CostumeEditState.hMovable);
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Get Valid Styles",1);
	// Get the list of valid styles
	costumeTailor_GetValidStyles(g_CostumeEditState.pCostume, NULL, NULL, NULL, NULL, pSpecies, eaFlatList, g_CostumeEditState.eaPowerFXBones, &g_CostumeEditState.eaStyles, CGVF_OMIT_EMPTY | CGVF_SORT_DISPLAY | (g_CostumeEditState.bUnlockAll ? CGVF_UNLOCK_ALL : 0));
	for (i=eaSize(&g_CostumeEditState.eaRandomStyles)-1; i>=0; --i) {
		for (j=eaSize(&g_CostumeEditState.eaStyles)-1; j>=0; --j) {
			if (g_CostumeEditState.eaStyles[j]->pcName == g_CostumeEditState.eaRandomStyles[i]) {
				break;
			}
		}
		if (j<0) {
			eaRemove(&g_CostumeEditState.eaRandomStyles, i);
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Validate Selected Bone",1);
	// Make sure the selected bone is still valid
	if (g_CostumeEditState.pSelectedBone) {
		CostumeUI_ValidateSelectedBone(pCostume);
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP_FUNC();
}

static NOCONST(PCPart) *CostumeUI_GetSharedColorCostumePart(void)
{
	int i;

	// Check current part
	if (g_CostumeEditState.pPart && g_CostumeEditState.pPart->eColorLink == kPCColorLink_All) {
		return g_CostumeEditState.pPart;
	}

	// exit if there isn't a costume. 
	if(!g_CostumeEditState.pCostume)
	{
		return NULL;
	}

	// Find part that uses the shared color
	for (i = eaSize(&g_CostumeEditState.pCostume->eaParts) - 1; i >= 0; i--) {
		assert(g_CostumeEditState.pCostume->eaParts[i]);
		if (g_CostumeEditState.pCostume->eaParts[i]->eColorLink == kPCColorLink_All) {
			return g_CostumeEditState.pCostume->eaParts[i];
		}
	}

	return NULL;
}

static bool CostumeUI_FindColorDiffInBoneGroup(NOCONST(PCPart) *pPart, int iColor)
{
	PCBoneRef **peaBoneGroup = NULL;
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	NOCONST(PCPart) *pTemp;
	int i;

	if (!pSkel) return false;
	if (!pPart) return false;
	if (pPart->iBoneGroupIndex < 0) return false;
	if (!pSkel->eaBoneGroups) return false;
	peaBoneGroup = pSkel->eaBoneGroups[pPart->iBoneGroupIndex]->eaBoneInGroup;
	if (!peaBoneGroup) return false;

	for (i = eaSize(&peaBoneGroup)-1; i >= 0; --i)
	{
		pTemp = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(peaBoneGroup[i]->hBone), NULL);
		switch(iColor)
		{
		case 0:
		case 120:
			if (!IS_SAME_COSTUME_COLOR(pTemp->color0,pPart->color0))
			{
				return true;
			}
		xcase 1:
		case 121:
			if (!IS_SAME_COSTUME_COLOR(pTemp->color1,pPart->color1))
			{
				return true;
			}
		xcase 2:
		case 122:
			if (!IS_SAME_COSTUME_COLOR(pTemp->color2,pPart->color2))
			{
				return true;
			}
		xcase 3:
			if (!IS_SAME_COSTUME_COLOR(pTemp->color3,pPart->color3))
			{
				return true;
			}
		xcase 123:
			if (!IS_SAME_COSTUME_COLOR(pTemp->color3,pPart->color3))
			{
				return true;
			}
		}
	}

	return false;
}

SA_RET_NN_VALID static UIColor *CostumeUI_GetColor(int iColor)
{
	static U8 diffColor[4] = {0, 0, 0, 255};

	NOCONST(PCPart) *pMirrorPart = NULL;
	NOCONST(PCPart) *pRealPart = NULL;
	PCGeometryDef *pGeo;
	bool bBothMode = false;
	NOCONST(PCPart) *pSharedPart = CostumeUI_GetSharedColorCostumePart();
	U8 tempColor[4];
	TailorWeaponStance *pStance = g_CostumeEditState.pPart ? WeaponStace_GetStanceForBone(GET_REF(g_CostumeEditState.pPart->hBoneDef)) : NULL;

	if (!(kPCEditColor_SharedColor0 <= iColor && iColor <= kPCEditColor_SharedColor3)) {
		if (!GET_REF(g_CostumeEditState.hBone)) {
			return &g_CostumeEditState.color;
		}
		if (!stricmp(GET_REF(g_CostumeEditState.hBone)->pcName,"None")) {
			return &g_CostumeEditState.color;
		}
		pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
		if (!pRealPart || !GET_REF(pRealPart->hGeoDef)) {
			return &g_CostumeEditState.color;
		}
		pGeo = GET_REF(pRealPart->hGeoDef);
		assert(pGeo);

		if (pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer && (pRealPart->eEditMode == kPCEditMode_Both)) {
			bBothMode = true;
		}

		if (g_MirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Both)) {
			pMirrorPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, pRealPart);
		}
	}

	switch(iColor)
	{
	xcase kPCEditColor_Color0:
		if ((pMirrorPart && !IS_SAME_COSTUME_COLOR(g_CostumeEditState.pPart->color0,pMirrorPart->color0)) ||
			(bBothMode && !IS_SAME_COSTUME_COLOR(pRealPart->color0, pRealPart->pClothLayer->color0))) {
			COPY_COSTUME_COLOR(diffColor, g_CostumeEditState.color.color);
		} else if (CostumeUI_FindColorDiffInBoneGroup(g_CostumeEditState.pPart, iColor)) {
			COPY_COSTUME_COLOR(diffColor, g_CostumeEditState.color.color);
		} else {
			COPY_COSTUME_COLOR(g_CostumeEditState.pPart->color0, g_CostumeEditState.color.color);
		}
	xcase kPCEditColor_Color1: 
		if ((pMirrorPart && !IS_SAME_COSTUME_COLOR(g_CostumeEditState.pPart->color1,pMirrorPart->color1)) ||
			(bBothMode && !IS_SAME_COSTUME_COLOR(pRealPart->color1, pRealPart->pClothLayer->color1))) {
			COPY_COSTUME_COLOR(diffColor, g_CostumeEditState.color.color);
		} else if (CostumeUI_FindColorDiffInBoneGroup(g_CostumeEditState.pPart, iColor)) {
			COPY_COSTUME_COLOR(diffColor, g_CostumeEditState.color.color);
		} else {
			COPY_COSTUME_COLOR(g_CostumeEditState.pPart->color1, g_CostumeEditState.color.color);
		}
	xcase kPCEditColor_Color2: 
		if ((pMirrorPart && !IS_SAME_COSTUME_COLOR(g_CostumeEditState.pPart->color2,pMirrorPart->color2)) ||
			(bBothMode && !IS_SAME_COSTUME_COLOR(pRealPart->color2, pRealPart->pClothLayer->color2))) {
			COPY_COSTUME_COLOR(diffColor, g_CostumeEditState.color.color);
		} else if (CostumeUI_FindColorDiffInBoneGroup(g_CostumeEditState.pPart, iColor)) {
			COPY_COSTUME_COLOR(diffColor, g_CostumeEditState.color.color);
		} else {
			COPY_COSTUME_COLOR(g_CostumeEditState.pPart->color2, g_CostumeEditState.color.color);
		}
	xcase kPCEditColor_Color3: 
		{
			PCMaterialDef *pMat = GET_REF(g_CostumeEditState.pPart->hMatDef);
			PCMaterialDef *pMirrorMat = NULL;
			if (pMirrorPart) {
				pMirrorMat = GET_REF(pMirrorPart->hMatDef);
			}
			if (pMat && pMat->bHasSkin && (!pMirrorMat || pMirrorMat->bHasSkin) && (!bBothMode || (GET_REF(pRealPart->hMatDef) == GET_REF(pRealPart->pClothLayer->hMatDef)))) {
				COPY_COSTUME_COLOR(g_CostumeEditState.pCostume->skinColor, g_CostumeEditState.color.color);
			} else {
				if ((pMirrorPart && !IS_SAME_COSTUME_COLOR(g_CostumeEditState.pPart->color3,pMirrorPart->color3)) ||
					(bBothMode && !IS_SAME_COSTUME_COLOR(pRealPart->color3, pRealPart->pClothLayer->color3))) {
					COPY_COSTUME_COLOR(diffColor, g_CostumeEditState.color.color);
				} else if (CostumeUI_FindColorDiffInBoneGroup(g_CostumeEditState.pPart, iColor)) {
					COPY_COSTUME_COLOR(diffColor, g_CostumeEditState.color.color);
				} else {
					COPY_COSTUME_COLOR(g_CostumeEditState.pPart->color3, g_CostumeEditState.color.color);
				}
			}
			break;
		}
		// New color id's
	xcase kPCEditColor_Skin:
		COPY_COSTUME_COLOR(g_CostumeEditState.pCostume->skinColor, g_CostumeEditState.color.color);
		return &g_CostumeEditState.color;
	xcase kPCEditColor_SharedColor0:
		VEC4_TO_COSTUME_COLOR(g_CostumeEditState.sharedColor0.color, tempColor);
		COPY_COSTUME_COLOR(pSharedPart ? pSharedPart->color0 : tempColor, g_CostumeEditState.color.color);
		return &g_CostumeEditState.color;
	xcase kPCEditColor_SharedColor1:
		VEC4_TO_COSTUME_COLOR(g_CostumeEditState.sharedColor1.color, tempColor);
		COPY_COSTUME_COLOR(pSharedPart ? pSharedPart->color1 : tempColor, g_CostumeEditState.color.color);
		return &g_CostumeEditState.color;
	xcase kPCEditColor_SharedColor2:
		VEC4_TO_COSTUME_COLOR(g_CostumeEditState.sharedColor2.color, tempColor);
		COPY_COSTUME_COLOR(pSharedPart ? pSharedPart->color2 : tempColor, g_CostumeEditState.color.color);
		return &g_CostumeEditState.color;
	xcase kPCEditColor_SharedColor3:
		VEC4_TO_COSTUME_COLOR(g_CostumeEditState.sharedColor3.color, tempColor);
		COPY_COSTUME_COLOR(pSharedPart ? pSharedPart->color3 : tempColor, g_CostumeEditState.color.color);
		return &g_CostumeEditState.color;
	xcase kPCEditColor_PerPartColor0:
		if (g_CostumeEditState.pPart->eColorLink != kPCColorLink_None && pStance) {
			VEC3_TO_COSTUME_COLOR(pStance->vColor0, g_CostumeEditState.color.color);
		} else if ((pMirrorPart && !IS_SAME_COSTUME_COLOR(g_CostumeEditState.pPart->color0,pMirrorPart->color0)) ||
			(bBothMode && !IS_SAME_COSTUME_COLOR(pRealPart->color0, pRealPart->pClothLayer->color0))) {
			COPY_COSTUME_COLOR(diffColor, g_CostumeEditState.color.color);
		} else if (CostumeUI_FindColorDiffInBoneGroup(g_CostumeEditState.pPart, iColor)) {
			COPY_COSTUME_COLOR(diffColor, g_CostumeEditState.color.color);
		} else {
			COPY_COSTUME_COLOR(g_CostumeEditState.pPart->color0, g_CostumeEditState.color.color);
		}
		return &g_CostumeEditState.color;
	xcase kPCEditColor_PerPartColor1:
		if (g_CostumeEditState.pPart->eColorLink != kPCColorLink_None && pStance) {
			VEC4_TO_COSTUME_COLOR(pStance->vColor1, g_CostumeEditState.color.color);
		} else if ((pMirrorPart && !IS_SAME_COSTUME_COLOR(g_CostumeEditState.pPart->color1,pMirrorPart->color1)) ||
			(bBothMode && !IS_SAME_COSTUME_COLOR(pRealPart->color1, pRealPart->pClothLayer->color1))) {
			COPY_COSTUME_COLOR(diffColor, g_CostumeEditState.color.color);
		} else if (CostumeUI_FindColorDiffInBoneGroup(g_CostumeEditState.pPart, iColor)) {
			COPY_COSTUME_COLOR(diffColor, g_CostumeEditState.color.color);
		} else {
			COPY_COSTUME_COLOR(g_CostumeEditState.pPart->color1, g_CostumeEditState.color.color);
		}
		return &g_CostumeEditState.color;
	xcase kPCEditColor_PerPartColor2:
		if (g_CostumeEditState.pPart->eColorLink != kPCColorLink_None && pStance) {
			VEC4_TO_COSTUME_COLOR(pStance->vColor2, g_CostumeEditState.color.color);
		} else if ((pMirrorPart && !IS_SAME_COSTUME_COLOR(g_CostumeEditState.pPart->color2,pMirrorPart->color2)) ||
			(bBothMode && !IS_SAME_COSTUME_COLOR(pRealPart->color2, pRealPart->pClothLayer->color2))) {
			COPY_COSTUME_COLOR(diffColor, g_CostumeEditState.color.color);
		} else if (CostumeUI_FindColorDiffInBoneGroup(g_CostumeEditState.pPart, iColor)) {
			COPY_COSTUME_COLOR(diffColor, g_CostumeEditState.color.color);
		} else {
			COPY_COSTUME_COLOR(g_CostumeEditState.pPart->color2, g_CostumeEditState.color.color);
		}
		return &g_CostumeEditState.color;
	xcase kPCEditColor_PerPartColor3:
		if (g_CostumeEditState.pPart->eColorLink != kPCColorLink_None && pStance) {
			VEC4_TO_COSTUME_COLOR(pStance->vColor3, g_CostumeEditState.color.color);
		} else if ((pMirrorPart && !IS_SAME_COSTUME_COLOR(g_CostumeEditState.pPart->color3,pMirrorPart->color3)) ||
			(bBothMode && !IS_SAME_COSTUME_COLOR(pRealPart->color3, pRealPart->pClothLayer->color3))) {
			COPY_COSTUME_COLOR(diffColor, g_CostumeEditState.color.color);
		} else if (CostumeUI_FindColorDiffInBoneGroup(g_CostumeEditState.pPart, iColor)) {
			COPY_COSTUME_COLOR(diffColor, g_CostumeEditState.color.color);
		} else {
			COPY_COSTUME_COLOR(g_CostumeEditState.pPart->color3, g_CostumeEditState.color.color);
		}
		return &g_CostumeEditState.color;
	}
	return &g_CostumeEditState.color;
}

static void CostumeUI_FillPermutationDataFromCostumePart(NOCONST(PCPart) *pPart, ColorPermutationData pPermute[4], int *iColors)
{
	int i;

	if (!iColors || !pPermute || !pPart) {
		return;
	}

	for (i = 0; i < 4; i++) {
		memset(&pPermute[i], 0, sizeof(ColorPermutationData));
	}

	*iColors = 0;
	if (pPart->eColorLink == kPCColorLink_All) {
		if (!(g_CostumeEditState.eSharedColorLocks & kPCControlledRandomLock_AllColor0)) {
			pPermute[*iColors].iSlot = 0;
			COPY_COSTUME_COLOR(pPart->color0, pPermute[*iColors].color);
			(*iColors)++;
		}
		if (!(g_CostumeEditState.eSharedColorLocks & kPCControlledRandomLock_AllColor1)) {
			pPermute[*iColors].iSlot = 1;
			COPY_COSTUME_COLOR(pPart->color1, pPermute[*iColors].color);
			(*iColors)++;
		}
		if (!(g_CostumeEditState.eSharedColorLocks & kPCControlledRandomLock_AllColor2)) {
			pPermute[*iColors].iSlot = 2;
			COPY_COSTUME_COLOR(pPart->color2, pPermute[*iColors].color);
			(*iColors)++;
		}
		if (!(g_CostumeEditState.eSharedColorLocks & kPCControlledRandomLock_AllColor3)) {
			pPermute[*iColors].iSlot = 3;
			COPY_COSTUME_COLOR(pPart->color3, pPermute[*iColors].color);
			(*iColors)++;
		}
	} else {
		if (!(pPart->eControlledRandomLocks & kPCControlledRandomLock_Color0)) {
			pPermute[*iColors].iSlot = 0;
			COPY_COSTUME_COLOR(pPart->color0, pPermute[*iColors].color);
			(*iColors)++;
		}
		if (!(pPart->eControlledRandomLocks & kPCControlledRandomLock_Color1)) {
			pPermute[*iColors].iSlot = 1;
			COPY_COSTUME_COLOR(pPart->color1, pPermute[*iColors].color);
			(*iColors)++;
		}
		if (!(pPart->eControlledRandomLocks & kPCControlledRandomLock_Color2)) {
			pPermute[*iColors].iSlot = 2;
			COPY_COSTUME_COLOR(pPart->color2, pPermute[*iColors].color);
			(*iColors)++;
		}
		if (!(pPart->eControlledRandomLocks & kPCControlledRandomLock_Color3)) {
			pPermute[*iColors].iSlot = 3;
			COPY_COSTUME_COLOR(pPart->color3, pPermute[*iColors].color);
			(*iColors)++;
		}
	}
}

static bool CostumeUI_MatHasGlow(int iColor)
{
	PCMaterialDef *pMaterial = GET_REF(g_CostumeEditState.hMaterial);
	NOCONST(PCPart) *pSharedPart = CostumeUI_GetSharedColorCostumePart();

	if (!pMaterial || !pMaterial->pColorOptions)
		return false;

	switch (iColor) {
		case kPCEditColor_Color0:
		case kPCEditColor_Color1:
		case kPCEditColor_Color2:
			return pMaterial->pColorOptions->bAllowGlow[iColor];
		case kPCEditColor_Color3:
			return pMaterial->bHasSkin ? false : pMaterial->pColorOptions->bAllowGlow[iColor];
		case kPCEditColor_SharedColor0:
		case kPCEditColor_SharedColor1:
		case kPCEditColor_SharedColor2:
		case kPCEditColor_SharedColor3:
			if (pSharedPart) {
				bool bHasGlow = false;
				int i;
				// Find part that uses the shared color
				for (i = eaSize(&g_CostumeEditState.pCostume->eaParts) - 1; i >= 0; i--) {
					assert(g_CostumeEditState.pCostume->eaParts[i]);
					if (g_CostumeEditState.pCostume->eaParts[i]->eColorLink == kPCColorLink_All) {
						pMaterial = GET_REF(g_CostumeEditState.pCostume->eaParts[i]->hMatDef);
						bHasGlow = bHasGlow || (pMaterial && pMaterial->pColorOptions && pMaterial->pColorOptions->bAllowGlow[iColor - kPCEditColor_SharedColor0]);
					}
				}
				return bHasGlow;
			}
			else
				return false;
	case kPCEditColor_PerPartColor0:
	case kPCEditColor_PerPartColor1:
	case kPCEditColor_PerPartColor2:
	case kPCEditColor_PerPartColor3:
			return pMaterial->pColorOptions->bAllowGlow[iColor - kPCEditColor_PerPartColor0];
		default:
			return false;
	}
}

static void CostumeUI_ShuffleColors(ColorPermutationData *pPermutations, int iColors) {
	int i, j, k;
	bool iChanges = 0;

	switch (iColors) {
	case 0:
	case 1:
		// Not enough colors to permute
		return;
	case 2:
		// Just swap the colors
		swap(&pPermutations[0].iSlot, &pPermutations[1].iSlot, sizeof(int));
		return;
	}

	for (i = 0; i < iColors; i++) {
		pPermutations[i].iOriginalSlot = pPermutations[i].iSlot;
	}

	// Complex case
	for (i = 0; i < 1000 && iChanges == 0; i++) {
		for (j = 0; j < iColors; j++) {
			// Find a color
			k = randomMersenneIntRange(g_CostumeEditState.pRandTable, 0, iColors - 1);

			if (k != j) {
				int it;
				it = pPermutations[j].iSlot;
				pPermutations[j].iSlot = pPermutations[k].iSlot;
				pPermutations[k].iSlot = it;
			}
		}

		iChanges = 0;
		for (j = 0; j < iColors; j++) {
			if (pPermutations[j].iSlot != pPermutations[j].iOriginalSlot) {
				iChanges++;
			}
		}
	}
}

static void CostumeUI_SetColorsFromWeaponStance(TailorWeaponStance *pStance, NOCONST(PCPart) *pPart)
{
	if (!pStance || !pPart)
		return;

	VEC3_TO_COSTUME_COLOR(pStance->vColor0, pPart->color0);
	VEC4_TO_COSTUME_COLOR(pStance->vColor1, pPart->color1);
	VEC4_TO_COSTUME_COLOR(pStance->vColor2, pPart->color2);
	VEC4_TO_COSTUME_COLOR(pStance->vColor3, pPart->color3);
}

void CostumeCreator_ApplyWeaponStance()
{
	if (g_CostumeEditState.pPart && g_pCostumeView) {
		SET_HANDLE_FROM_STRING(g_hWeaponStanceDict, REF_STRING_FROM_HANDLE(g_CostumeEditState.pPart->hBoneDef), g_pCostumeView->costume.hWeaponStance);
	}
}

void CostumeUI_costumeView_RegenCostume(CostumeViewGraphics *pGraphics, PlayerCostume *pCostume, const PCSlotType *pSlotType, PCMood *pMood, CharacterClass* pClass, ItemDefRef **eaShowItems)
{
	int i;
	NOCONST(PlayerCostume) *pNewCostume = NULL;
	CostumeDisplayData **eaData = NULL;
	PERFINFO_AUTO_START_FUNC();

	if (!pGraphics) {
		return;
	}

	if (costumeCameraUI_IsShowingCostumeItems())
	{
		GameAccountDataExtract *pExtract = NULL; // No entity during preview
		item_GetItemCostumeDataToShow(PARTITION_CLIENT, CostumeUI_GetSourceEnt(), &eaData, pExtract);

		for (i=eaSize(&eaShowItems)-1; i>=0; --i) {
			ItemDef *pItemDef = GET_REF(eaShowItems[i]->hDef);
			if (pItemDef && eaSize(&pItemDef->ppCostumes) > 0 && pItemDef->eCostumeMode != kCostumeDisplayMode_Unlock) {
				CostumeDisplayData *pCDD = item_GetCostumeDisplayData(PARTITION_CLIENT, CostumeUI_GetSourceEnt(), NULL, pItemDef, 0, NULL, 0);
				if (pCDD) {
					eaPush(&eaData, pCDD);
				}
			}
		}

		if (eaSize(&eaData) > 0)
		{
			pNewCostume = costumeTailor_ApplyOverrideSet(pCostume, (PCSlotType *)pSlotType, eaData, GET_REF(g_CostumeEditState.hSpecies));

			for(i=eaSize(&eaData)-1; i>=0; --i) {
				eaDestroy(&eaData[i]->eaCostumes);
				eaDestroy(&eaData[i]->eaAddedFX);
				eaDestroyStruct(&eaData[i]->eaCostumesOwned, parse_PlayerCostume);
				free(eaData[i]);
			}
			eaDestroy(&eaData);
		}
	}
	CostumeCreator_ApplyWeaponStance();
	if (pNewCostume)
	{
		costumeView_RegenCostumeEx(pGraphics, (PlayerCostume*)pNewCostume, pSlotType, pMood, pClass);
		StructDestroyNoConst(parse_PlayerCostume, pNewCostume);
	}
	else
	{
		costumeView_RegenCostumeEx(pGraphics, pCostume, pSlotType, pMood, pClass);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void CostumeUI_ComputeCostumeChangeCost(NOCONST(PlayerCostume) *pCostumeCopy)
{
	Entity *pSourceEnt = CostumeCreator_GetEditEntity();
	Entity *pPlayerEnt = CostumeCreator_GetEditPlayerEntity();
	CharClassTypes Class = GetCharacterClassEnum( pSourceEnt );

	if (g_CostumeEditState.pStartCostume && g_CostumeEditState.pCostume) {
		GameAccountData *pData = entity_GetGameAccount(pPlayerEnt);
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
		PCCostumeStorageType eStorageType = g_CostumeEditState.eCostumeStorageType;
		bool bHaveFreeChangeToken = false;

		PERFINFO_AUTO_START("Compute Costume Cost", 1);

		if ( eStorageType == kPCCostumeStorageType_SpacePet ) {
			// Determine free token
			bHaveFreeChangeToken = costumeEntity_GetFreeChangeTokens(NULL, pSourceEnt) > 0
				|| costumeEntity_GetFreeChangeTokens(pPlayerEnt, pSourceEnt) > 0
				|| costumeEntity_GetAccountChangeTokens(pSourceEnt, pData) > 0;
		} else {
			// Nemeses need to be treated like pet, they are like a pet in almost all other ways.
			if (eStorageType == kPCCostumeStorageType_Nemesis) {
				eStorageType = kPCCostumeStorageType_Pet;
			}

			// Determine free token
			bHaveFreeChangeToken = costumeEntity_GetFreeChangeTokens(NULL, pSourceEnt) > 0
				|| costumeEntity_GetFreeFlexChangeTokens(pSourceEnt) > 0
				|| costumeEntity_GetFreeFlexChangeTokens(pPlayerEnt) > 0
				|| costumeEntity_GetAccountChangeTokens(pSourceEnt, pData) > 0;
		}

		// No cost if the costume is free, or the player doesn't have a choice and doesn't have a token
		if(pPlayerEnt && !g_CostumeEditState.bCostumeChangeIsFree && (gConf.bTailorPaymentChoice || (!gConf.bTailorPaymentChoice && !bHaveFreeChangeToken)) )
		{
			NOCONST(PlayerCostume) *pStartCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pStartCostume);

			//Do this every regen call because the references may not be in from server yet
			pStartCostume->eCostumeType = kPCCostumeType_Unrestricted;
			CostumeCreator_BeginCostumeEditing(pStartCostume);

			g_CostumeEditState.currentCost = costumeEntity_GetCostToChange(
					pPlayerEnt, eStorageType,
					pStartCostume, pCostumeCopy,
					&g_CostumeEditState.bCostumeChanged
				);

			StructDestroyNoConst(parse_PlayerCostume, pStartCostume);
		} else {
			g_CostumeEditState.currentCost = 0;
		}

		PERFINFO_AUTO_STOP();
	}
}

// Call with bUpdateReferences set to false if you did not do anything
// that requires changing part information (e.g. muscle or bone scale).
void CostumeUI_RegenCostumeEx(bool bUpdateReferences, bool bValidateSafeMode)
{
	Entity *pEnt = entActivePlayerPtr();
	PERFINFO_AUTO_START_FUNC();

	COSTUME_UI_TRACE_FUNC();

	if (!g_pCostumeView || !g_CostumeEditState.pConstCostume) {
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if (bUpdateReferences)
	{
		g_uiLastCostumeRegenTime = gGCLState.totalElapsedTimeMs;

		PERFINFO_AUTO_START("Update References", 1);
		CostumeUI_UpdateLists(g_CostumeEditState.pCostume, false, bValidateSafeMode);
		g_CostumeEditState.bUpdateLists = false;
		costumeLineUI_UpdateLines(g_CostumeEditState.pCostume, &g_CostumeEditState.eaBufferedEditLine,
			GET_REF(g_CostumeEditState.hSpecies), GET_REF(g_CostumeEditState.hSkeleton),
			g_CostumeEditState.eFindTypes, g_CostumeEditState.iBodyScalesRule,
			&g_CostumeEditState.eaFindRegions, g_CostumeEditState.eaFindScaleGroup,
			g_CostumeEditState.eaBodyScalesInclude, g_CostumeEditState.eaBodyScalesExclude,
			g_CostumeEditState.eaIncludeBones, g_CostumeEditState.eaExcludeBones,
			g_CostumeEditState.pSlotType, g_CostumeEditState.pchCostumeSet, g_CostumeEditState.bLineListHideMirrorBones, g_CostumeEditState.bUnlockAll, g_MirrorSelectMode, g_GroupSelectMode, g_bCountNone, g_bOmitHasOnlyOne, g_CostumeEditState.bCombineLines,
			g_CostumeEditState.bTextureLinesForCurrentPartValuesOnly, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eaPowerFXBones);
		costumeLineUI_FillUnlockInfo(g_CostumeEditState.eaBufferedEditLine, g_CostumeEditState.stashGeoUnlockMeta, g_CostumeEditState.stashMatUnlockMeta, g_CostumeEditState.stashTexUnlockMeta);
		g_CostumeEditState.bUpdateLines = false;

		{
			// Make a copy of the costume and clean it up, then validate that.
			NOCONST(PlayerCostume) *pCostumeCopy = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
			costumeTailor_StripUnnecessary(pCostumeCopy);

			CostumeUI_ComputeCostumeChangeCost(pCostumeCopy);

			estrClear(&gErrorText);
			if (!g_CostumeEditState.bUnlockAll)
			{
				PERFINFO_AUTO_START("Validate Player Costume",1);

				// The original costume is unrestricted before "edit costume" is actually clicked,
				// so for validation purposes, we'll set the copy back to player type.
				// The server doesn't do this to the check, so it doesn't enable people to use
				// unrestricted costumes, but it does mean we don't catch bad eCostumeType errors
				// in the client.
				pCostumeCopy->eCostumeType = kPCCostumeType_Player;

				// First check to see if the costume is valid with the unowned parts. If that's
				// the case, then check to see if the costume is valid without unowned parts. The
				// result of the second test is primarily designed to provide a quick test to see
				// if the player is only using parts they own.
				if (costumeValidate_ValidatePlayerCreated((PlayerCostume*)pCostumeCopy, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType, entActivePlayerPtr(), CostumeUI_GetSourceEnt(), &gErrorText, NULL, g_CostumeEditState.eaUnlockedCostumes, false)) {
					g_CostumeEditState.bOwnedCostumeValid = costumeValidate_ValidatePlayerCreated((PlayerCostume*)pCostumeCopy, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType, entActivePlayerPtr(), CostumeUI_GetSourceEnt(), NULL, NULL, NULL, false);
				} else {
					g_CostumeEditState.bOwnedCostumeValid = false;
				}

				if (gErrorText && *gErrorText) {
					// Don't spam this more than once every 10 seconds
					static int iLastTime = 0;
					int iCurrentTime = timeSecondsSince2000();
					if (iCurrentTime > iLastTime + 10) {
						printf("%s\n", gErrorText);
						iLastTime = iCurrentTime;
					}
				}

				PERFINFO_AUTO_STOP();
			}

			// Clean up costume copy.
			StructDestroyNoConst(parse_PlayerCostume, pCostumeCopy);
		}
		PERFINFO_AUTO_STOP();
	}

	CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume ? g_CostumeEditState.pConstHoverCostume : g_CostumeEditState.pConstCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
	PERFINFO_AUTO_STOP_FUNC();
}

NOCONST(PlayerCostume)* CostumeUI_GetCostume(void)
{
	return g_CostumeEditState.pCostume;
}

SpeciesDef* CostumeUI_GetSpecies(void)
{
	return GET_REF(g_CostumeEditState.hSpecies);
}

PCSlotType *CostumeUI_GetSlotType(void)
{
	return g_CostumeEditState.pSlotType;
}

void CostumeUI_SetCostumeEx( NOCONST(PlayerCostume)* pCostume, CharacterClass* pClass, bool bRegen, bool bNoModify )
{
	if (pCostume==NULL)
		return;

	StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pCostume);
	StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pHoverCostume);

	CostumeUI_ClearSelections();

	g_CostumeEditState.pCostume = StructCloneNoConst(parse_PlayerCostume, pCostume);

	if (g_pCostumeView && g_CostumeEditState.pConstCostume)
	{
		// If the costume is just for display, set the costume type to unrestricted
		// to keep it from getting modified due to missing unlock information
		if (bNoModify)
			g_CostumeEditState.pCostume->eCostumeType = kPCCostumeType_Unrestricted;

		if (bRegen)
		{
			CostumeUI_RegenCostumeEx(true, true);
		}
		else
		{
			// Don't call CostumeEdit_RegenCostume since that updates lists and we
			// don't want to update lists.  Only the tailor uses that and many other
			// places render costumes.
			CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), pClass, g_CostumeEditState.eaShowItems);
		}
	}
}

PCMood* CostumeUI_GetMood(void)
{
	return GET_REF(g_CostumeEditState.hMood);
}

void CostumeUI_ClearCostume(void)
{
	int i;

	g_CostumeEditState.pCostume = NULL;
	g_CostumeEditState.pStartCostume = NULL;
	REMOVE_HANDLE(g_CostumeEditState.hMood);;
	for(i=eaSize(&g_CostumeEditState.eaCachedCostumes)-1; i>=0; --i) {
		StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.eaCachedCostumes[i]);
	}
	eaDestroy(&g_CostumeEditState.eaCachedCostumes);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SetPartColor");
void CostumeUI_SetPartColor(int colorIndex, F32 color0, F32 color1, F32 color2, F32 color3, const char *pchBoneGroup)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.pCostume && g_pCostumeView && colorIndex >= 0 && colorIndex <= 3)
	{
		int i, j;
		NOCONST(PlayerCostume) *pTempCostume = g_CostumeEditState.pHoverCostume ? g_CostumeEditState.pHoverCostume : g_CostumeEditState.pCostume;
		PCBoneGroup *bg = NULL;
		PCSkeletonDef *pSkel;

		pSkel = GET_REF(pTempCostume->hSkeleton);
		if (!pSkel) {
			return;
		}

		bg = eaIndexedGetUsingString(&pSkel->eaBoneGroups, pchBoneGroup);
		if (!bg) {
			return;
		}

		for (i = eaSize(&pTempCostume->eaParts)-1; i >= 0; --i)
		{
			for (j = eaSize(&bg->eaBoneInGroup)-1; j >= 0; --j)
			{
				if (REF_COMPARE_HANDLES(bg->eaBoneInGroup[j]->hBone, pTempCostume->eaParts[i]->hBoneDef))
					break;
			}

			if (j >= 0)
			{
				U8 *colors[] = {
					pTempCostume->eaParts[i]->color0,
					pTempCostume->eaParts[i]->color1,
					pTempCostume->eaParts[i]->color2,
					pTempCostume->eaParts[i]->color3,
				};
				colors[colorIndex][0] = color0;
				colors[colorIndex][1] = color1;
				colors[colorIndex][2] = color2;
				colors[colorIndex][3] = color3;
			}
		}

		CostumeUI_costumeView_RegenCostume(g_pCostumeView, CONTAINER_RECONST(PlayerCostume, pTempCostume), g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
	}
}

//////////////////////////////////////////////////////////////////////////

// Unlock all costume pieces
AUTO_COMMAND ACMD_NAME("CostumeCreator.UnlockAll") ACMD_ACCESSLEVEL(9);
void CostumeCreator_UnlockAll(bool bUnlockAll)
{
	Entity *pEnt = entActivePlayerPtr();
	
	if (pEnt && pEnt->pSaved) {
		pEnt->pSaved->costumeData.bUnlockAll = bUnlockAll;
	}
}

// Set the muscle definition of the character being created.
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetMuscle") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetMuscle(F32 fMuscle);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetMuscle");
void CostumeCreator_SetMuscle(F32 fMuscle)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	COSTUME_UI_TRACE_FUNC();
	if (pSkel) {
		F32 fHMin, fHMax;
		fHMin = costumeTailor_GetOverrideMuscleMin(pSpecies, g_CostumeEditState.pSlotType);
		fHMax = costumeTailor_GetOverrideMuscleMax(pSpecies, g_CostumeEditState.pSlotType);
		if (fHMax)
		{
			g_CostumeEditState.pCostume->fMuscle = CLAMP(fMuscle + fHMin, fHMin, fHMax);
		}
		else
		{
			g_CostumeEditState.pCostume->fMuscle = CLAMP(fMuscle + pSkel->fPlayerMinMuscle, pSkel->fPlayerMinMuscle, pSkel->fPlayerMaxMuscle);
		}
		CostumeUI_RegenCostume(false);
	}
}

int CostumeCreator_GetSpeciesBodyScaleIndex(const char *pchName)
{
	int i;
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	if (pSpecies)
	{
		for (i = eaSize(&pSpecies->eaBodyScaleLimits) - 1; i >= 0; --i)
		{
			if (!strcmp(pSpecies->eaBodyScaleLimits[i]->pcName, pchName))
			{
				return i;
			}
		}
	}
	return -1;
}

// Set the body mass of the character being created.
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetBodyScale") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetBodyScale(int index, F32 fBodyScale);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetBodyScale");
void CostumeCreator_SetBodyScale(int index, F32 fBodyScale)
{
	COSTUME_UI_TRACE_FUNC();
	if (costumeTailor_SetBodyScale(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), index, fBodyScale, g_CostumeEditState.pSlotType)) {
		CostumeUI_RegenCostume(false);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetBodyScaleByName);
void CostumeCreator_SetBodyScaleByName(const char *pchName, F32 fBodyScale)
{
	COSTUME_UI_TRACE_FUNC();
	if (costumeTailor_SetBodyScaleByName(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), pchName, fBodyScale, g_CostumeEditState.pSlotType)) {
		CostumeUI_RegenCostume(false);
	}
}

// Set the body mass of the character being created.
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetHoverBodyScale") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetHoverBodyScale(int index, F32 fBodyScale);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverBodyScale");
void CostumeCreator_SetHoverBodyScale(int index, F32 fBodyScale)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	COSTUME_UI_TRACE_FUNC();
	if (index >= 0)
	{
		if (!g_CostumeEditState.pHoverCostume) {
			g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
		}
		assert(g_CostumeEditState.pHoverCostume);

		if (pSkel) {
			F32 fMinScale = 0;
			F32 fMaxScale = 100;

			if (pSpecies && eaSize(&pSkel->eaBodyScaleInfo) > index)
			{
				F32 fMin, fMax;
				if(costumeTailor_GetOverrideBodyScale(pSkel, pSkel->eaBodyScaleInfo[index]->pcName, pSpecies, g_CostumeEditState.pSlotType, &fMin, &fMax))
				{
					fMinScale = fMin;
					fMaxScale = fMax;
					if (g_CostumeEditState.pHoverCostume->eafBodyScales && (eafSize(&g_CostumeEditState.pHoverCostume->eafBodyScales) > index)) {
						g_CostumeEditState.pHoverCostume->eafBodyScales[index] = CLAMP(fBodyScale + fMinScale, fMinScale, fMaxScale);
						CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
					}
					return;
				}
			}
			if (pSkel->eafPlayerMinBodyScales && (eafSize(&pSkel->eafPlayerMinBodyScales) > index)) {
				fMinScale = pSkel->eafPlayerMinBodyScales[index];
			}
			if (pSkel->eafPlayerMaxBodyScales && (eafSize(&pSkel->eafPlayerMaxBodyScales) > index)) {
				fMaxScale = pSkel->eafPlayerMaxBodyScales[index];
			}
			if (g_CostumeEditState.pHoverCostume->eafBodyScales && (eafSize(&g_CostumeEditState.pHoverCostume->eafBodyScales) > index)) {
				g_CostumeEditState.pHoverCostume->eafBodyScales[index] = CLAMP(fBodyScale + fMinScale, fMinScale, fMaxScale);
				CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
			}
		}
	}
	else
	{
		if (g_CostumeEditState.pHoverCostume) {
			StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
			g_CostumeEditState.pHoverCostume = NULL;
			CostumeUI_RegenCostume(true);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetHoverBodyScaleByName);
void CostumeCreator_SetHoverBodyScaleByName(const char *pchName, F32 fBodyScale)
{
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	COSTUME_UI_TRACE_FUNC();
	if (pSkel) {
		int index;
		for (index = 0; index < eaSize(&pSkel->eaBodyScaleInfo); index++)
		{
			if (!stricmp(pSkel->eaBodyScaleInfo[index]->pcName, pchName))
			{
				break;
			}
		}
		CostumeCreator_SetHoverBodyScale(index, fBodyScale);
	}
}

static int CostumeCreator_GetSpeciesBoneScaleIndex(const char *pchName)
{
	int i;
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	if (pSpecies)
	{
		for (i = eaSize(&pSpecies->eaBoneScaleLimits) - 1; i >= 0; --i)
		{
			if (!strcmp(pSpecies->eaBoneScaleLimits[i]->pcName, pchName))
			{
				return i;
			}
		}
	}
	return -1;
}

bool CostumeCreator_CommonSetBoneScale(NOCONST(PlayerCostume) *pCostume, F32 fMin, F32 fMax, const char *pcScaleName, F32 fBoneScale)
{
	int i;
	NOCONST(PCScaleValue) *pScaleValue = NULL;

	// Find the named scale if already exists
	for(i=eaSize(&pCostume->eaScaleValues)-1; i>=0; --i) {
		NOCONST(PCScaleValue) *pValue = pCostume->eaScaleValues[i];
		if (stricmp(pValue->pcScaleName,pcScaleName) == 0) {
			pScaleValue = pValue;
			break;
		}
	}

	fBoneScale += fMin;

	// if zero, remove it
	if (!fBoneScale) {
		if (pScaleValue) {
			StructDestroyNoConst(parse_PCScaleValue, pScaleValue);
			eaRemove(&pCostume->eaScaleValues, i);
		}
		return true;
	}
	// If no entry found, create one
	if (!pScaleValue) {
		pScaleValue = StructCreateNoConst(parse_PCScaleValue);
		pScaleValue->pcScaleName = StructAllocString(pcScaleName);
		eaPush(&pCostume->eaScaleValues, pScaleValue);
	}
	// Set the value
	if (!nearf(pScaleValue->fValue, fBoneScale))
	{
		pScaleValue->fValue = CLAMP(fBoneScale, fMin, fMax);
		return true;
	}
	else
		return false;
}

// Set the bone scale of the character being created.
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetBoneScale") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetBoneScale(int index, F32 fBoneScale);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetBoneScale");
void CostumeCreator_SetBoneScale(int index, F32 fBoneScale)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	const char *pcScaleName;
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	F32 fMin, fMax;

	COSTUME_UI_TRACE_FUNC();

	if (index >= eaSize(&g_CostumeEditState.eaBoneScales) || index < 0) {
		return;
	}

	if(costumeTailor_GetOverrideBoneScale(pSkel, g_CostumeEditState.eaBoneScales[index], g_CostumeEditState.eaBoneScales[index]->pcName, pSpecies, g_CostumeEditState.pSlotType, &fMin, &fMax))
	{
		if (CostumeCreator_CommonSetBoneScale(g_CostumeEditState.pCostume, fMin, fMax, g_CostumeEditState.eaBoneScales[index]->pcName, fBoneScale))
			CostumeUI_RegenCostume(false);
		return;
	}

	pcScaleName = g_CostumeEditState.eaBoneScales[index]->pcName;
	fMin = g_CostumeEditState.eaBoneScales[index]->fPlayerMin;
	fMax = g_CostumeEditState.eaBoneScales[index]->fPlayerMax;

	if (CostumeCreator_CommonSetBoneScale(g_CostumeEditState.pCostume, fMin, fMax, pcScaleName, fBoneScale))
		CostumeUI_RegenCostume(false);
}

// Set the bone scale of the character being created.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetBoneScaleByName");
void CostumeCreator_SetBoneScaleByGroupName(SA_PARAM_NN_VALID PCScaleInfo *pScaleInfo, F32 fBoneScale)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	F32 fMin, fMax;
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);

	COSTUME_UI_TRACE_FUNC();

	if (!pScaleInfo)
		return;

	if(costumeTailor_GetOverrideBoneScale(pSkel, pScaleInfo, pScaleInfo->pcName, pSpecies, g_CostumeEditState.pSlotType, &fMin, &fMax))
	{
		if (CostumeCreator_CommonSetBoneScale(g_CostumeEditState.pCostume, fMin, fMax, pScaleInfo->pcName, fBoneScale))
			CostumeUI_RegenCostume(false);
	}
	else
	{
		if (CostumeCreator_CommonSetBoneScale(g_CostumeEditState.pCostume, pScaleInfo->fPlayerMin, pScaleInfo->fPlayerMax, pScaleInfo->pcName, fBoneScale))
			CostumeUI_RegenCostume(false);
	}
	return;
}

// Set the body mass of the character being created.
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetHeight") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetHeight(F32 fHeight);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHeight");
void CostumeCreator_SetHeight(F32 fHeight)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	F32 fHMin, fHMax;
	fHMin = costumeTailor_GetOverrideHeightMin(pSpecies, g_CostumeEditState.pSlotType);
	fHMax = costumeTailor_GetOverrideHeightMax(pSpecies, g_CostumeEditState.pSlotType);

	COSTUME_UI_TRACE_FUNC();

	if (pSkel) {
		if (fHMax)
		{
			g_CostumeEditState.pCostume->fHeight = CLAMP(fHeight + fHMin, fHMin, fHMax);
		}
		else
		{
			g_CostumeEditState.pCostume->fHeight = CLAMP(fHeight + pSkel->fPlayerMinHeight, pSkel->fPlayerMinHeight, pSkel->fPlayerMaxHeight);
		}
		CostumeUI_RegenCostume(false);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetRawHeight");
F32 CostumeCreator_GetRawHeight(void)
{
	COSTUME_UI_TRACE_FUNC();
	return g_CostumeEditState.pCostume ? g_CostumeEditState.pCostume->fHeight : 0.0f;
}

// DELETEME: This does not work correctly and will likely make the costume invalid.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetRawHeight");
void CostumeCreator_SetRawHeight(F32 fHeight)
{
	COSTUME_UI_TRACE_FUNC();
	g_CostumeEditState.pCostume->fHeight = fHeight;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetHeight");
F32 CostumeCreator_GetHeight(void)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	F32 fHMin, fHMax;
	fHMin = costumeTailor_GetOverrideHeightMin(pSpecies, g_CostumeEditState.pSlotType);
	fHMax = costumeTailor_GetOverrideHeightMax(pSpecies, g_CostumeEditState.pSlotType);
	COSTUME_UI_TRACE_FUNC();
	if( g_CostumeEditState.pCostume )
	{
		if (fHMax)
		{
			return g_CostumeEditState.pCostume->fHeight - fHMin;
		}	
		if (pSkel)
		{
			return g_CostumeEditState.pCostume->fHeight - pSkel->fPlayerMinHeight;
		}
	}
	return 0.0f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetHeightRange");
F32 CostumeCreator_GetHeightRange(void)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	F32 fHMin, fHMax;
	fHMin = costumeTailor_GetOverrideHeightMin(pSpecies, g_CostumeEditState.pSlotType);
	fHMax = costumeTailor_GetOverrideHeightMax(pSpecies, g_CostumeEditState.pSlotType);
	COSTUME_UI_TRACE_FUNC();
	if (fHMax)
	{
		return fHMax - fHMin;
	}
	if (pSkel)
	{
		return pSkel->fPlayerMaxHeight - pSkel->fPlayerMinHeight;
	}
	return 0.0f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMaxHeight");
F32 CostumeCreator_GetMaxHeight(void)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	F32 fHMax;
	fHMax = costumeTailor_GetOverrideHeightMax(pSpecies, g_CostumeEditState.pSlotType);
	COSTUME_UI_TRACE_FUNC();
	if(fHMax)
	{
		return fHMax;
	}
	if (pSkel)
	{
		return pSkel->fPlayerMaxHeight;
	}
	return 0.0f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMinHeight");
F32 CostumeCreator_GetMinHeight(void)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	F32 fHMin, fHMax;
	fHMin = costumeTailor_GetOverrideHeightMin(pSpecies, g_CostumeEditState.pSlotType);
	fHMax = costumeTailor_GetOverrideHeightMax(pSpecies, g_CostumeEditState.pSlotType);
	if (fHMax)
	{
		return fHMin;
	}	
	if (pSkel)
	{
		return pSkel->fPlayerMinHeight;
	}
	return 0.0f;
}

// Generate a random costume with the currently selected skeleton
AUTO_COMMAND ACMD_NAME("CostumeCreator.Randomize") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_Random(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_Randomize");
void CostumeCreator_Random(void)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	Entity *pEnt = entActivePlayerPtr();
	COSTUME_UI_TRACE_FUNC();
	costumeRandom_SetRandomTable(g_CostumeEditState.pRandTable);
	costumeRandom_FillRandom(g_CostumeEditState.pCostume, pSpecies, guild_GetGuild(pEnt), NULL, NULL, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, true, true, true, g_CostumeEditState.bUnlockAll, true, true, true);
	costumeTailor_FillAllBones(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, true, false, true);
	costumeRandom_SetRandomTable(NULL);

	CostumeUI_ClearSelections();
	CostumeUI_RegenCostumeEx(true, true);
}

// Generate a random morphology with the currently selected skeleton
AUTO_COMMAND ACMD_NAME("CostumeCreator.RandomMorphology") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_RandomMorphology(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_RandomMorphology");
void CostumeCreator_RandomMorphology(void)
{
	COSTUME_UI_TRACE_FUNC();
	costumeRandom_RandomMorphology(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType, NULL, true, true, true, g_CostumeEditState.eaUnlockedCostumes, false);
	CostumeUI_RegenCostume(false);
}

// Generate a random morphology with the currently selected skeleton
AUTO_COMMAND ACMD_NAME("CostumeCreator.RandomHeight") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_RandomHeight(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_RandomHeight");
void CostumeCreator_RandomHeight(void)
{
	COSTUME_UI_TRACE_FUNC();
	costumeRandom_RandomHeight(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType);
	CostumeUI_RegenCostume(false);
}

// Generate a random morphology with the currently selected skeleton
AUTO_COMMAND ACMD_NAME("CostumeCreator.RandomMuscle") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_RandomMuscle(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_RandomMuscle");
void CostumeCreator_RandomMuscle(void)
{
	COSTUME_UI_TRACE_FUNC();
	costumeRandom_RandomMuscle(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType);
	CostumeUI_RegenCostume(false);
}

// Generate a random morphology with the currently selected skeleton
AUTO_COMMAND ACMD_NAME("CostumeCreator.RandomBodyScales") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_RandomBodyScales(const char* pchBodyScaleName);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_RandomBodyScales");
void CostumeCreator_RandomBodyScales(const char* pchBodyScaleName)
{
	COSTUME_UI_TRACE_FUNC();
	costumeRandom_RandomBodyScales(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType, pchBodyScaleName);
	CostumeUI_RegenCostume(false);
}

// Generate a random morphology with the currently selected skeleton
AUTO_COMMAND ACMD_NAME("CostumeCreator.RandomBoneScales") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_RandomBoneScales(const char* pchGroupname);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_RandomBoneScales");
void CostumeCreator_RandomBoneScales(const char* pchGroupname)
{
	COSTUME_UI_TRACE_FUNC();
	costumeRandom_RandomBoneScales(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType, pchGroupname);
	CostumeUI_RegenCostume(false);
}

// Generate a random morphology with the currently selected skeleton
AUTO_COMMAND ACMD_NAME("CostumeCreator.RandomStance") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_RandomStance(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_RandomStance");
void CostumeCreator_RandomStance(void)
{
	COSTUME_UI_TRACE_FUNC();
	costumeRandom_RandomStance(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType, true, g_CostumeEditState.eaUnlockedCostumes, false);
	CostumeUI_RegenCostume(false);
}

// Generate a random costume with the currently selected skeleton and morphology
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_MTRandomCostume");
void CostumeCreator_MTRandomCostume(int direction, bool bExcludeMicroTransactionCostumes)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	Entity *pEnt = CostumeUI_GetSourceEnt();
	COSTUME_UI_TRACE_FUNC();
	if (direction == 0) {
		g_CostumeEditState.seedPos = eaiSize(&g_CostumeEditState.eaiSeeds) - 1;
	}
	if (direction >= 0) {
		++g_CostumeEditState.seedPos;
		if (g_CostumeEditState.seedPos >= eaiSize(&g_CostumeEditState.eaiSeeds)) {
			eaiPush(&g_CostumeEditState.eaiSeeds, randomU32());
		}
		if (eaiSize(&g_CostumeEditState.eaiSeeds) > 100) {
			eaiRemove(&g_CostumeEditState.eaiSeeds, 0);
			--g_CostumeEditState.seedPos;
		}
	} else if (g_CostumeEditState.seedPos > 0) {
		--g_CostumeEditState.seedPos;
	} else {
		return;
	}
	assert(g_CostumeEditState.eaiSeeds);
	mersenneTableFree(g_CostumeEditState.pRandTable);
	g_CostumeEditState.pRandTable = mersenneTableCreate(g_CostumeEditState.eaiSeeds[g_CostumeEditState.seedPos]);

	costumeRandom_SetRandomTable(g_CostumeEditState.pRandTable);
	costumeRandom_RandomParts(g_CostumeEditState.pCostume, pSpecies, guild_GetGuild(pEnt), NULL, bExcludeMicroTransactionCostumes ? g_CostumeEditState.eaOwnedUnlockedCostumes : g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, true, true, true, g_CostumeEditState.bUnlockAll, true);
	costumeTailor_FillAllBones(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, true, false, true);
	CostumeCreator_SetMaterialLinkAll();
	costumeRandom_SetRandomTable(NULL);

	// Clear old selection info after randomizing
	g_CostumeEditState.pPart = NULL;
	REMOVE_HANDLE(g_CostumeEditState.hBone);
	REMOVE_HANDLE(g_CostumeEditState.hRegion);
	REMOVE_HANDLE(g_CostumeEditState.hCategory);
	REMOVE_HANDLE(g_CostumeEditState.hGeometry);
	REMOVE_HANDLE(g_CostumeEditState.hMaterial);
	REMOVE_HANDLE(g_CostumeEditState.hPattern);
	REMOVE_HANDLE(g_CostumeEditState.hDetail);
	REMOVE_HANDLE(g_CostumeEditState.hSpecular);
	REMOVE_HANDLE(g_CostumeEditState.hDiffuse);
	REMOVE_HANDLE(g_CostumeEditState.hMovable);

	CostumeUI_RegenCostumeEx(true, true);
}

// Generate a random costume with the currently selected skeleton and morphology
AUTO_COMMAND ACMD_NAME("CostumeCreator.RandomCostume") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_RandomCostume(int direction);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_RandomCostume");
void CostumeCreator_RandomCostume(int direction)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeCreator_MTRandomCostume(direction, false);
}

void CostumeCreator_SetSkeletonPtr(PCSkeletonDef *pSkel)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	if (pSkel && g_CostumeEditState.pCostume && (stricmp(pSkel->pcName, REF_STRING_FROM_HANDLE(g_CostumeEditState.pCostume->hSkeleton)) != 0))
	{
		if (pSpecies && GET_REF(pSpecies->hSkeleton) != pSkel) {
			// Find matching species that supports the new skeleton
			SpeciesDef **eaSpecies = NULL;
			Entity *pPlayer = entActivePlayerPtr();
			if (!pPlayer) {
				pPlayer = CONTAINER_RECONST(Entity, g_pFakePlayer);
			}
			species_GetSpeciesList(pPlayer, pSpecies->pcSpeciesName, pSkel, &eaSpecies);
			if (!eaSize(&eaSpecies)) {
				// no choices
				eaDestroy(&eaSpecies);
				return;
			}
			pSpecies = eaSpecies[0];
			SET_HANDLE_FROM_REFERENT(g_hSpeciesDict, pSpecies, g_CostumeEditState.hSpecies);
			SET_HANDLE_FROM_REFERENT(g_hSpeciesDict, pSpecies, g_CostumeEditState.pCostume->hSpecies);
			eaDestroy(&eaSpecies);
		}
		SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, pSkel, g_CostumeEditState.hSkeleton);
		costumeTailor_ChangeSkeleton(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, pSkel);
		costumeTailor_FillAllBones(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, true, false, true);
		CostumeUI_RegenCostumeEx(true, true);
		CostumeUI_UpdateUnlockedCostumeParts();
	}
}

// Sets the skeleton of the character being created.
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetSkeleton") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetSkeleton(const char *pchSkeleton);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetSkeleton");
void CostumeCreator_SetSkeleton(const char *pchSkeleton)
{
	PCSkeletonDef *pSkel = RefSystem_ReferentFromString(g_hCostumeSkeletonDict, pchSkeleton);
	COSTUME_UI_TRACE_FUNC();
	CostumeCreator_SetSkeletonPtr(pSkel);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_CanSetSkeleton");
bool CostumeCreator_CanSetSkeleton(const char *pchSkeleton)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel = RefSystem_ReferentFromString(g_hCostumeSkeletonDict, pchSkeleton);
	SpeciesDef **eaSpecies = NULL;

	if (!pSpecies || !pSkel) {
		return false;
	}

	if (pSpecies && GET_REF(pSpecies->hSkeleton) == pSkel) {
		return true;
	}

	species_GetSpeciesList(CostumeCreator_GetEditPlayerEntity(), pSpecies->pcSpeciesName, pSkel, &eaSpecies);
	if (!eaSize(&eaSpecies)) {
		// no choices
		eaDestroy(&eaSpecies);
		return false;
	}

	eaDestroy(&eaSpecies);
	return true;
}

// Sets the species of the character being created.
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetSpecies") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetSpecies(const char *pchSpecies);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetSpecies");
void CostumeCreator_SetSpecies(const char *pchSpecies)
{
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	COSTUME_UI_TRACE_FUNC();
	if (pSkel)
	{
		SpeciesDef *pNewSpecies = NULL;

		REMOVE_HANDLE(g_CostumeEditState.hSpecies);

		FOR_EACH_IN_REFDICT(g_hSpeciesDict, SpeciesDef, pSpecies);
			if (GET_REF(pSpecies->hSkeleton) == pSkel)
			{
				if (!pNewSpecies)
					pNewSpecies = pSpecies;

				if (!stricmp(pchSpecies, pSpecies->pcName))
				{
					pNewSpecies = pSpecies;
					break;
				}
			}
		FOR_EACH_END;

		if (pNewSpecies)
		{
			SET_HANDLE_FROM_REFERENT(g_hSpeciesDict, pNewSpecies, g_CostumeEditState.hSpecies);
			if (g_CostumeEditState.pCostume)
			{
				SET_HANDLE_FROM_REFERENT(g_hSpeciesDict, pNewSpecies, g_CostumeEditState.pCostume->hSpecies);
				CostumeUI_RegenCostumeEx(true, true);
				CostumeUI_UpdateUnlockedCostumeParts();
			}
		}
	}
}

// Set the stance of the character being created.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetPreset");
bool CostumeCreator_SetPreset(const char *pchPreset)
{
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	COSTUME_UI_TRACE_FUNC();
	if (pSkel && pSpecies)
	{
		S32 i;
		for (i = 0; i < eaSize(&pSpecies->eaPresets); i++)
		{
			CostumePreset *pPreset = pSpecies->eaPresets[i];
			CostumePresetCategory *pPresetCat = GET_REF(pPreset->hPresetCategory);
			const char *name = pPreset->pcName || !pPresetCat ? pPreset->pcName : pPresetCat->pcName;
			if (!name) continue;
			if (!stricmp(name, pchPreset))
			{
				if (g_CostumeEditState.pCostume) {
					StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
				}
				g_CostumeEditState.pCostume = StructCloneDeConst(parse_PlayerCostume, GET_REF(pPreset->hCostume));
				costumeEntity_ApplyEntityInfoToCostume(CostumeUI_GetSourceEnt(),g_CostumeEditState.pConstCostume);

				// Clear old selection info after selecting new preset
				g_CostumeEditState.pPart = NULL;
				REMOVE_HANDLE(g_CostumeEditState.hBone);
				REMOVE_HANDLE(g_CostumeEditState.hRegion);
				REMOVE_HANDLE(g_CostumeEditState.hCategory);
				REMOVE_HANDLE(g_CostumeEditState.hGeometry);
				REMOVE_HANDLE(g_CostumeEditState.hMaterial);
				REMOVE_HANDLE(g_CostumeEditState.hPattern);
				REMOVE_HANDLE(g_CostumeEditState.hDetail);
				REMOVE_HANDLE(g_CostumeEditState.hSpecular);
				REMOVE_HANDLE(g_CostumeEditState.hDiffuse);
				REMOVE_HANDLE(g_CostumeEditState.hMovable);

				CostumeUI_RegenCostumeEx(true, true);
				return true;
			}
		}
	}
	return false;
}

// Set the stance of the character being created.
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetStance") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetStance(const char *pchStance);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetStance");
bool CostumeCreator_SetStance(const char *pchStance)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	COSTUME_UI_TRACE_FUNC();
	if (pSkel && g_CostumeEditState.pCostume)
	{
		S32 i;
		if (pSpecies && eaSize(&pSpecies->eaStanceInfo))
		{
			for (i = 0; i < eaSize(&pSpecies->eaStanceInfo); i++)
			{
				if (!stricmp(pSpecies->eaStanceInfo[i]->pcName, pchStance))
				{
					g_CostumeEditState.pCostume->pcStance = allocAddString(pchStance);
					CostumeUI_RegenCostume(true);
					return true;
				}
			}
		}
		else
		{
			for (i = 0; i < eaSize(&pSkel->eaStanceInfo); i++)
			{
				if (!stricmp(pSkel->eaStanceInfo[i]->pcName, pchStance))
				{
					g_CostumeEditState.pCostume->pcStance = allocAddString(pchStance);
					CostumeUI_RegenCostume(true);
					return true;
				}
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetPresetIndex");
bool CostumeCreator_SetPresetIndex(int idx)
{
	COSTUME_UI_TRACE_FUNC();
	if (idx < eaSize(&g_CostumeEditState.eaPresets))
	{
		CostumePreset *pPreset = g_CostumeEditState.eaPresets[idx];
		if (g_CostumeEditState.pCostume) {
			StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
		}
		g_CostumeEditState.pCostume = StructCloneDeConst(parse_PlayerCostume, GET_REF(pPreset->hCostume));
		costumeEntity_ApplyEntityInfoToCostume(CostumeUI_GetSourceEnt(),g_CostumeEditState.pConstCostume);

		// Clear old selection info after selecting new preset
		g_CostumeEditState.pPart = NULL;
		REMOVE_HANDLE(g_CostumeEditState.hBone);
		REMOVE_HANDLE(g_CostumeEditState.hRegion);
		REMOVE_HANDLE(g_CostumeEditState.hCategory);
		REMOVE_HANDLE(g_CostumeEditState.hGeometry);
		REMOVE_HANDLE(g_CostumeEditState.hMaterial);
		REMOVE_HANDLE(g_CostumeEditState.hPattern);
		REMOVE_HANDLE(g_CostumeEditState.hDetail);
		REMOVE_HANDLE(g_CostumeEditState.hSpecular);
		REMOVE_HANDLE(g_CostumeEditState.hDiffuse);
		REMOVE_HANDLE(g_CostumeEditState.hMovable);

		CostumeUI_RegenCostume(true);
		return true;
	}
	return false;
}
// Set the stance of the character being created.
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetVoice") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetVoice(const char *pchVoice);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetVoice");
bool CostumeCreator_SetVoice(const char *pchVoice)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	COSTUME_UI_TRACE_FUNC();
	if (pSkel && g_CostumeEditState.pCostume)
	{
		S32 i;
		if (pSpecies && (pSpecies->bAllowAllVoices == false || eaSize(&pSpecies->eaAllowedVoices)))
		{
			for (i = 0; i < eaSize(&pSpecies->eaAllowedVoices); i++)
			{
				PCVoice *v = GET_REF(pSpecies->eaAllowedVoices[i]->hVoice);
				if (!stricmp(v->pcName, pchVoice))
				{
					COPY_HANDLE(g_CostumeEditState.pCostume->hVoice, pSpecies->eaAllowedVoices[i]->hVoice);
					CostumeUI_RegenCostume(true);
					return true;
				}
			}
		}
		else
		{
			SET_HANDLE_FROM_STRING("CostumeVoice", pchVoice, g_CostumeEditState.pCostume->hVoice);
			CostumeUI_RegenCostume(true);
			return true;
		}
	}
	return false;
}

// Set the mood of the character being created.
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetMood") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetMood(const char *pchMood);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetMood");
bool CostumeCreator_SetMood(const char *pchMood)
{
	S32 i;
	COSTUME_UI_TRACE_FUNC();
	for (i = 0; i < eaSize(&g_CostumeEditState.eaMoods); i++)
	{
		if (!stricmp(g_CostumeEditState.eaMoods[i]->pcName, pchMood))
		{
			SET_HANDLE_FROM_REFERENT(g_hCostumeMoodDict, g_CostumeEditState.eaMoods[i], g_CostumeEditState.hMood);
			CostumeUI_RegenCostume(true);
			return true;
		}
	}
	return false;
}

// Set the region being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetRegion") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetRegion(const char *pchRegion);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetRegion");
bool CostumeCreator_SetRegion(const char *pchRegion)
{
	PCRegion *pRegion = RefSystem_ReferentFromString(g_hCostumeRegionDict, pchRegion);
	COSTUME_UI_TRACE_FUNC();
	if (pRegion)
	{
		SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, pRegion, g_CostumeEditState.hRegion);
		CostumeUI_RegenCostumeEx(true, true);
		return true;
	}
	return false;
}


// Set the category being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetCategory") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetCategory(const char *pchCategory);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetCategory");
bool CostumeCreator_SetCategory(const char *pchCategory)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	Entity *pEnt = entActivePlayerPtr();
	COSTUME_UI_TRACE_FUNC();

	if (costumeTailor_SetCategory(g_CostumeEditState.pCostume, pSpecies, GET_REF(g_CostumeEditState.hRegion), pchCategory,
				g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll))
	{
		PCCategory *pCategory = RefSystem_ReferentFromString(g_hCostumeCategoryDict, pchCategory);
		SET_HANDLE_FROM_REFERENT(g_hCostumeCategoryDict, pCategory, g_CostumeEditState.hCategory);
		CostumeUI_RegenCostumeEx(true, true);
		return true;
	}
	return false;
}

// Set the bone being edited, which also sets the active part
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetBone") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetBone(const char *pchBone);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetBone");
bool CostumeCreator_SetBone(const char *pchBone)
{
	PCBoneDef *pBone = CostumeUI_FindBone(pchBone, GET_REF(g_CostumeEditState.hSkeleton));
	PCRegion *pRegion;
	COSTUME_UI_TRACE_FUNC();
	if (pBone)
	{
		SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, pBone, g_CostumeEditState.hBone);

		pRegion = GET_REF(pBone->hRegion);
		if (pRegion && (pRegion != GET_REF(g_CostumeEditState.hRegion)))
		{
			SET_HANDLE_FROM_REFERENT(g_hCostumeRegionDict, pRegion, g_CostumeEditState.hRegion);
		}

		CostumeUI_RegenCostumeEx(true, true);
		return true;
	}
	return false;
}

// Set the layer on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetLayer") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetLayer(const char *pchLayer);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetLayer");
bool CostumeCreator_SetLayer(const char *pchLayer)
{
	NOCONST(PCPart) *pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	PCLayer *pLayer = RefSystem_ReferentFromString(g_hCostumeLayerDict, pchLayer);
	COSTUME_UI_TRACE_FUNC();

	if (!GET_REF(g_CostumeEditState.hBone)) return false;
	if (!stricmp(GET_REF(g_CostumeEditState.hBone)->pcName,"None")) return false;
	assert(pRealPart);

	if (pLayer) {
		switch(pLayer->eLayerType) {
			case kPCLayerType_All:
				pRealPart->eEditMode = kPCEditMode_Both;
			xcase kPCLayerType_Left:
				pRealPart->eEditMode = kPCEditMode_Left;
			xcase kPCLayerType_Right:
				pRealPart->eEditMode = kPCEditMode_Right;
			xcase kPCLayerType_Front:
				pRealPart->eEditMode = kPCEditMode_Front;
			xcase kPCLayerType_Back:
				pRealPart->eEditMode = kPCEditMode_Back;
			xdefault:
				ErrorFilenamef(pLayer->pcFileName, "Unexpected layer type encountered");
		}
		if (GET_REF(pRealPart->hBoneDef) != GET_REF(g_CostumeEditState.hBone)) {
			NOCONST(PCPart) *pMirrorPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, pRealPart);
			if (pMirrorPart) {
				pMirrorPart->eEditMode = pLayer->eLayerType;
			}
		}
		if (pRealPart->pClothLayer) {
			pRealPart->pClothLayer->eEditMode = pRealPart->eEditMode;
		}
		CostumeUI_RegenCostume(true);
		return true;
	}
	return false;
}


// Set the geo on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetGeo") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetGeo(const char *pchGeo);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetGeo");
bool CostumeCreator_SetGeo(const char *pchGeo)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	Entity *pEnt = entActivePlayerPtr();
	Guild *pGuild = guild_GetGuild(pEnt);
	COSTUME_UI_TRACE_FUNC();

	if (costumeTailor_SetPartGeometry(g_CostumeEditState.pCostume, pSpecies, GET_REF(g_CostumeEditState.hBone), pchGeo,
			g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, 
			pGuild, g_CostumeEditState.bUnlockAll, g_MirrorSelectMode, g_GroupSelectMode))
	{
		costumeEntity_ApplyEntityInfoToCostume(CostumeUI_GetSourceEnt(),g_CostumeEditState.pConstCostume);
		CostumeUI_RegenCostume(true);
		return true;
	}
	return false;
}


// Set the material on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetMaterial") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetMaterial(const char *pchMat);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetMaterial");
bool CostumeCreator_SetMaterial(const char *pchMat)
{
	Entity *pEnt = entActivePlayerPtr();
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	COSTUME_UI_TRACE_FUNC();

	if (!pPart) {
		return false;
	}
	if (!GET_REF(pPart->hBoneDef)) {
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	}

	if (costumeTailor_SetPartMaterial(g_CostumeEditState.pCostume, pPart, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.eaBones, pchMat,
			g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, guild_GetGuild(pEnt),
			g_CostumeEditState.bUnlockAll, g_MirrorSelectMode) ) {
		CostumeUI_RegenCostume(true);
		return true;
	} else {
		return false;
	}
}


static void CostumeCreator_SetMatDef(NOCONST(PCPart) *pRealPart, bool setCloth)
{
	int iMat = -1;
	PCGeometryDef *pGeo;
	PCMaterialDef **eaTempMat = NULL;

	pGeo = GET_REF(pRealPart->hGeoDef);
	costumeTailor_GetValidMaterials(g_CostumeEditState.pCostume, pGeo, GET_REF(g_CostumeEditState.hSpecies), NULL, NULL, g_CostumeEditState.eaUnlockedCostumes, &eaTempMat, false, false, g_CostumeEditState.bUnlockAll);

	iMat = costumeTailor_GetMatchingMatIndex(GET_REF(pRealPart->hMatDef), eaTempMat);
	if (iMat >= 0) SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, eaTempMat[iMat], pRealPart->hMatDef);

	if (setCloth)
	{
		iMat = costumeTailor_GetMatchingMatIndex(GET_REF(pRealPart->pClothLayer->hMatDef), eaTempMat);
		if (iMat >= 0) SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, eaTempMat[iMat], pRealPart->pClothLayer->hMatDef);
	}

	eaDestroy(&eaTempMat);
}

// Set all the materials on the skeleton
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetAllMaterials") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetAllMaterials(const char *pchMat);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetAllMaterials");
bool CostumeCreator_SetAllMaterials(const char *pchMat)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	Entity *pEnt = entActivePlayerPtr();
	COSTUME_UI_TRACE_FUNC();

	if (costumeTailor_SetAllMaterials(g_CostumeEditState.pCostume, pSpecies, pchMat, g_CostumeEditState.eaUnlockedCostumes,
		g_CostumeEditState.pSlotType, guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll))
	{
		CostumeUI_RegenCostumeEx(true, true);
		return true;
	}

	return false;
}

// Set all the materials on the skeleton
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetAllMaterialsToBoneGroup") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetAllMaterialsToBoneGroup(const char *pchBoneGroup);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetAllMaterialsToBoneGroup");
void CostumeCreator_SetAllMaterialsToBoneGroup(const char *pchBoneGroup)
{
	PCBoneGroup *pBoneGroup = NULL;
	PCBoneDef *pBone = NULL;
	NOCONST(PCPart) *pPart = NULL;
	int j;
	PCSkeletonDef *skel = GET_REF(g_CostumeEditState.hSkeleton);
	PCMaterialDef *pMat;
	COSTUME_UI_TRACE_FUNC();

	if (!pchBoneGroup) return;
	if (!skel) return;

	for(j=eaSize(&skel->eaBoneGroups)-1; j>=0; --j)
	{
		if (skel->eaBoneGroups[j]->pcName && !stricmp(skel->eaBoneGroups[j]->pcName,pchBoneGroup))
		{
			pBoneGroup = skel->eaBoneGroups[j];
			break;
		}
	}
	if (!pBoneGroup) return;
	if (!eaSize(&pBoneGroup->eaBoneInGroup)) return;
	pBone = GET_REF(pBoneGroup->eaBoneInGroup[0]->hBone);
	if (!pBone) return;
	pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL);
	if (!pPart) return;
	pMat = GET_REF(pPart->hMatDef);
	if (!pMat) return;

	CostumeCreator_SetAllMaterials(pMat->pcName);
}

// Set the pattern on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetPattern") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetPattern(const char *pchPattern);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetPattern");
bool CostumeCreator_SetPattern(const char *pchPattern)
{
	Entity *pEnt = entActivePlayerPtr();
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	COSTUME_UI_TRACE_FUNC();

	if (!pPart) {
		return false;
	}
	if (!GET_REF(pPart->hBoneDef)) {
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	}

	if (costumeTailor_SetPartTexturePattern(g_CostumeEditState.pCostume, pPart, GET_REF(g_CostumeEditState.hSpecies), pchPattern,
					g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll, g_MirrorSelectMode)) {
		CostumeUI_RegenCostume(true);
		return true;
	} else {
		return false;
	}
}

static void CostumeCreator_SetPatternDef(NOCONST(PCPart) *pRealPart, PCTextureDef *pPattern, PCTextureDef *pClothPattern)
{
	int iTex = -1;
	PCGeometryDef *pGeo;
	PCMaterialDef *pMat;
	PCTextureDef **eaTempTex = NULL;

	pGeo = GET_REF(pRealPart->hGeoDef);
	pMat = GET_REF(pRealPart->hMatDef);
	costumeTailor_GetValidTextures(g_CostumeEditState.pCostume, pMat, GET_REF(g_CostumeEditState.hSpecies), NULL, NULL, pGeo, NULL, g_CostumeEditState.eaUnlockedCostumes, kPCTextureType_Pattern, &eaTempTex, false, false, g_CostumeEditState.bUnlockAll);

	iTex = costumeTailor_GetMatchingTexIndex(pPattern, eaTempTex);
	if (iTex >= 0) SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, eaTempTex[iTex], pRealPart->hPatternTexture);

	if (pClothPattern)
	{
		iTex = costumeTailor_GetMatchingTexIndex(pClothPattern, eaTempTex);
		if (iTex >= 0) SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, eaTempTex[iTex], pRealPart->pClothLayer->hPatternTexture);
	}

	eaDestroy(&eaTempTex);
}

// Set the pattern on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetPatternToAll") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetPatternToAll(const char *pchPattern);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetPatternToAll");
bool CostumeCreator_SetPatternToAll(const char *pchPattern)
{
	int i;
	bool found = false;
	NOCONST(PCPart) *pRealPart;
	PCGeometryDef *pGeo;
	PCTextureDef *pPattern;
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	Entity *pEnt = entActivePlayerPtr();
	COSTUME_UI_TRACE_FUNC();

	pPattern = RefSystem_ReferentFromString(g_hCostumeTextureDict, pchPattern);
	if (!stricmp(pchPattern, "None") && !pPattern) {
		pchPattern = NULL;
	}
	if (!GET_REF(g_CostumeEditState.hBone)) return false;
	if (!stricmp(GET_REF(g_CostumeEditState.hBone)->pcName,"None")) return false;

	for(i=eaSize(&g_CostumeEditState.pConstCostume->eaParts)-1; i>=0; --i)
	{
		PCPart *pPart = g_CostumeEditState.pConstCostume->eaParts[i];

		if (pPart && (pPattern || !pchPattern)) {
			pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(pPart->hBoneDef), NULL);
			assert(pRealPart);
			pGeo = GET_REF(pRealPart->hGeoDef);
			assert(pGeo);

			// Set value accounting for cloth
			if (pGeo && pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer) {
				if ((pRealPart->eEditMode == kPCEditMode_Front) ||
					(pRealPart->eEditMode == kPCEditMode_Back)) {
						// If only front or back, then apply as per rule
						CostumeCreator_SetPatternDef(pRealPart, pPattern, NULL);
				} else {
					// If both, then get real part and apply to both
					CostumeCreator_SetPatternDef(pRealPart, pPattern, pPattern);
				}
				costumeTailor_PickValidPartValues(g_CostumeEditState.pCostume, pRealPart, pSpecies, g_CostumeEditState.pSlotType, g_CostumeEditState.eaUnlockedCostumes, true, g_CostumeEditState.bUnlockAll, true, false, guild_GetGuild(pEnt));
			} else {
				// If not cloth, then simply apply
				CostumeCreator_SetPatternDef(pRealPart, pPattern, NULL);
				costumeTailor_PickValidPartValues(g_CostumeEditState.pCostume, pRealPart, pSpecies, g_CostumeEditState.pSlotType, g_CostumeEditState.eaUnlockedCostumes, true, g_CostumeEditState.bUnlockAll, true, false, guild_GetGuild(pEnt));
			}

			found = true;
		}
	}

	if (found) CostumeUI_RegenCostume(true);
	return found;
}

// Set the detail on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetDetail") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetDetail(const char *pchDetail);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetDetail");
bool CostumeCreator_SetDetail(const char *pchDetail)
{
	Entity *pEnt = entActivePlayerPtr();
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	COSTUME_UI_TRACE_FUNC();

	if (!pPart) {
		return false;
	}
	if (!GET_REF(pPart->hBoneDef)) {
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	}

	if (costumeTailor_SetPartTextureDetail(g_CostumeEditState.pCostume, pPart, GET_REF(g_CostumeEditState.hSpecies), pchDetail,
					g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll, g_MirrorSelectMode)) {
		CostumeUI_RegenCostume(true);
		return true;
	} else {
		return false;
	}
}

// Set the specular map on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetSpecular") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetSpecular(const char *pchSpecular);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetSpecular");
bool CostumeCreator_SetSpecular(const char *pchSpecular)
{
	Entity *pEnt = entActivePlayerPtr();
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	COSTUME_UI_TRACE_FUNC();

	if (!pPart) {
		return false;
	}
	if (!GET_REF(pPart->hBoneDef)) {
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	}

	if (costumeTailor_SetPartTextureSpecular(g_CostumeEditState.pCostume, pPart, GET_REF(g_CostumeEditState.hSpecies), pchSpecular,
					g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll, g_MirrorSelectMode)) {
		CostumeUI_RegenCostume(true);
		return true;
	} else {
		return false;
	}
}

// Set the pattern on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetDiffuse") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetDiffuse(const char *pchDiffuse);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetDiffuse");
bool CostumeCreator_SetDiffuse(const char *pchDiffuse)
{
	Entity *pEnt = entActivePlayerPtr();
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	COSTUME_UI_TRACE_FUNC();

	if (!pPart) {
		return false;
	}
	if (!GET_REF(pPart->hBoneDef)) {
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	}

	if (costumeTailor_SetPartTextureDiffuse(g_CostumeEditState.pCostume, pPart, GET_REF(g_CostumeEditState.hSpecies), pchDiffuse,
					g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll, g_MirrorSelectMode)) {
		CostumeUI_RegenCostume(true);
		return true;
	} else {
		return false;
	}
}

// Set the pattern on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetMovable") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetMovable(const char *pchMovable);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetMovable");
bool CostumeCreator_SetMovable(const char *pchMovable)
{
	Entity *pEnt = entActivePlayerPtr();
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	COSTUME_UI_TRACE_FUNC();

	if (!pPart) {
		return false;
	}
	if (!GET_REF(pPart->hBoneDef)) {
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	}

	if (costumeTailor_SetPartTextureMovable(g_CostumeEditState.pCostume, pPart, GET_REF(g_CostumeEditState.hSpecies), pchMovable,
					g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll, g_MirrorSelectMode)) {
		CostumeUI_RegenCostume(true);
		return true;
	} else {
		return false;
	}
}

// Set the hover preset on the costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverPreset");
bool CostumeCreator_SetHoverPreset(const char *pchPreset)
{
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	COSTUME_UI_TRACE_FUNC();
	if (!pchPreset || !*pchPreset)
	{
		if (g_CostumeEditState.pHoverCostume) {
			StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
		}
		g_CostumeEditState.pHoverCostume = NULL;
		CostumeUI_RegenCostume(true);
		return false;
	}
	if (pSkel && pSpecies && g_CostumeEditState.pCostume)
	{
		S32 i;
		for (i = 0; i < eaSize(&pSpecies->eaPresets); i++)
		{
			CostumePreset *pPreset = pSpecies->eaPresets[i];
			CostumePresetCategory *pPresetCat = GET_REF(pPreset->hPresetCategory);
			const char *name = pPreset->pcName || !pPresetCat ? pPreset->pcName : pPresetCat->pcName;
			if (!name) continue;
			if (!stricmp(name, pchPreset))
			{
				if (g_CostumeEditState.pHoverCostume) {
					StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
				}
				g_CostumeEditState.pHoverCostume = StructCloneDeConst(parse_PlayerCostume, GET_REF(pPreset->hCostume));
				costumeEntity_ApplyEntityInfoToCostume(CostumeUI_GetSourceEnt(),g_CostumeEditState.pConstHoverCostume);

				CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
				return true;
			}
		}
	}
	return false;
}

static bool CostumeCreator_ApplyPresetOverlayBones(PCBoneDef *pBone, NOCONST(PlayerCostume) *pSrc, NOCONST(PlayerCostume) *pDst) 
{
	bool bValidate = false;
	if (pBone) {
		NOCONST(PCPart) *pSrcPart = costumeTailor_GetPartByBone(pSrc, pBone, NULL);
		NOCONST(PCPart) *pDstPart = costumeTailor_GetPartByBone(pDst, pBone, NULL);
		if (pSrcPart) {
			PCCategory *pSrcCategory = costumeTailor_GetCategoryForRegion(CONTAINER_RECONST(PlayerCostume, pSrc), GET_REF(pBone->hRegion));
			PCCategory *pDstCategory = costumeTailor_GetCategoryForRegion(CONTAINER_RECONST(PlayerCostume, pDst), GET_REF(pBone->hRegion));
			if (pSrcCategory && pSrcCategory != pDstCategory) {
				PCGeometryDef *pGeo = GET_REF(pSrcPart->hGeoDef);
				if (pGeo) {
					int i;
					for (i = eaSize(&pGeo->eaCategories) - 1; i >= 0; i--) {
						if (GET_REF(pGeo->eaCategories[i]->hCategory) == pDstCategory) {
							break;
						}
					}
					if (i < 0) {
						costumeTailor_SetRegionCategory(pDst, GET_REF(pBone->hRegion), pSrcCategory);
						bValidate = true;
					}
				}
			}

			if (pDstPart) {
				StructCopyAllNoConst(parse_PCPart, pSrcPart, pDstPart);
			} else {
				eaPush(&pDst->eaParts, StructCloneNoConst(parse_PCPart, pSrcPart));
			}
		}
	}
	return bValidate;
}

static bool s_bApplyPresetOverlayDebugOutput = false;
AUTO_CMD_INT(s_bApplyPresetOverlayDebugOutput, CharacterCreator_ApplyPresetOverlayDebug) ACMD_CATEGORY(CharacterCreation) ACMD_ACCESSLEVEL(9);

void CostumeCreator_ApplyPresetOverlay(NOCONST(PlayerCostume) *pDst, CostumePreset *pPreset, bool bForceIgnoreHeight)
{
#define COSTUME_PRESET_VALIDATE_VALUE(value) (pPreset->bOverrideValidateValues || !pPresetCat ? pPreset->validatePlayerValues.value : pPresetCat->validatePlayerValues.value)

	CostumePresetCategory *pPresetCat = GET_REF(pPreset->hPresetCategory);
	NOCONST(PlayerCostume) *pSrc = CONTAINER_NOCONST(PlayerCostume, GET_REF(pPreset->hCostume));
	PCSlotType *pSlotType = pPreset->pcSlotType || !pPresetCat ? costumeLoad_GetSlotType(pPreset->pcSlotType) : costumeLoad_GetSlotType(pPresetCat->pcSlotType);
	PCSkeletonDef *pSrcSkel = SAFE_GET_REF(pSrc, hSkeleton);
	PCSkeletonDef *pDstSkel = SAFE_GET_REF(pDst, hSkeleton);
	S32 i, j;
	bool bValidate = false;

	if (!pSrcSkel || !pDstSkel) {
		return;
	}
	if (s_bApplyPresetOverlayDebugOutput) {
		printf("---------------- CostumeCreator_ApplyPresetOverlay\n");
	}

	if (COSTUME_PRESET_VALIDATE_VALUE(bTestSkinColor)) {
		if (s_bApplyPresetOverlayDebugOutput) {
			printf("Applying Skin Color\n");
		}
		copyVec4(pSrc->skinColor, pDst->skinColor);
	}
	// bForceIgnoreHeight is a hack, we really need to just update the costume presets
	if (!bForceIgnoreHeight && COSTUME_PRESET_VALIDATE_VALUE(bTestHeight)) {
		if (s_bApplyPresetOverlayDebugOutput) {
			printf("Applying Height\n");
		}
		pDst->fHeight = pSrc->fHeight;
	}
	if (COSTUME_PRESET_VALIDATE_VALUE(bTestMuscle)) {
		if (s_bApplyPresetOverlayDebugOutput) {
			printf("Applying Muscle\n");
		}
		pDst->fMuscle = pSrc->fMuscle;
	}
	if (COSTUME_PRESET_VALIDATE_VALUE(bTestStance)) {
		if (s_bApplyPresetOverlayDebugOutput) {
			printf("Applying Stance\n");
		}
		pDst->pcStance = pSrc->pcStance;
	}
	if (COSTUME_PRESET_VALIDATE_VALUE(bTestBones)) {
		PCBoneRef **eaBones = COSTUME_PRESET_VALIDATE_VALUE(eaBonesUsed);
		if (s_bApplyPresetOverlayDebugOutput) {
			printf("Applying Bones\n");
		}
		for (i = eaSize(&eaBones) - 1; i >= 0; i--) {
			bValidate = CostumeCreator_ApplyPresetOverlayBones(GET_REF(eaBones[i]->hBone), pSrc, pDst);
		}

	} else {
		// This is also a bit of a hack. Again, costume preset data should just be fixed. 
		for (i = eaSize(&g_CostumeEditState.eaCostumeEditLine) - 1; i >= 0; i--) {
			switch (g_CostumeEditState.eaCostumeEditLine[i]->iType) {
				xcase kCostumeEditLineType_Bone:
				case kCostumeEditLineType_Geometry:
				case kCostumeEditLineType_Material:
				case kCostumeEditLineType_Texture0:
				case kCostumeEditLineType_Texture1:
				case kCostumeEditLineType_Texture2:
				case kCostumeEditLineType_Texture3:
				case kCostumeEditLineType_Texture4:
				case kCostumeEditLineType_TextureScale:	{
					bValidate = CostumeCreator_ApplyPresetOverlayBones(GET_REF(g_CostumeEditState.eaCostumeEditLine[i]->hOwnerBone), pSrc, pDst);
				}
			}
		}
	}
	if (COSTUME_PRESET_VALIDATE_VALUE(bTestBodyScales)) {
		BodyScaleSP **eaScales = COSTUME_PRESET_VALIDATE_VALUE(eaBodyScalesUsed);
		if (s_bApplyPresetOverlayDebugOutput) {
			printf("Applying Body Scales\n");
		}
		for (i = eaSize(&eaScales) - 1; i >= 0; i--) {
			for (j = eaSize(&pSrcSkel->eaBodyScaleInfo); j >= 0; j--) {
				if (pSrcSkel->eaBodyScaleInfo[j]->pcName == eaScales[i]->pcName) {
					F32 fValue = j < eafSize(&pSrc->eafBodyScales) ? pSrc->eafBodyScales[j] : j < eafSize(&pSrcSkel->eafDefaultBodyScales) ? pSrcSkel->eafDefaultBodyScales[j] : 0;
					costumeTailor_SetBodyScaleByName(pDst, GET_REF(pDst->hSpecies), eaScales[i]->pcName, fValue, pSlotType);
					break;
				}
			}
		}
	}
	if (COSTUME_PRESET_VALIDATE_VALUE(bTestBoneScales)) {
		BoneScaleSP **eaScales = COSTUME_PRESET_VALIDATE_VALUE(eaBoneScalesUsed);
		if (s_bApplyPresetOverlayDebugOutput) {
			printf("Applying Bone Scales\n");
		}
		for (i = eaSize(&eaScales) - 1; i >= 0; i--) {
			F32 fValue = 0;
			PCScaleInfo *pScale = NULL;
			F32 fMin, fMax;

			for (j = eaSize(&pSrc->eaScaleValues) - 1; j >= 0; j--) {
				if (pSrc->eaScaleValues[j]->pcScaleName == eaScales[i]->pcName) {
					fValue = pSrc->eaScaleValues[j]->fValue;
					break;
				}
			}
			for (j = eaSize(&pDstSkel->eaScaleInfo) - 1; j >= 0; j--) {
				if (pDstSkel->eaScaleInfo[j]->pcName == eaScales[i]->pcName) {
					pScale = pDstSkel->eaScaleInfo[j];
					break;
				}
			}
			if (!pScale) {
				continue;
			}

			if(!costumeTailor_GetOverrideBoneScale(pDstSkel, pScale, pScale->pcName, GET_REF(pDst->hSpecies), g_CostumeEditState.pSlotType, &fMin, &fMax)) {
				fMin = pScale->fPlayerMin;
				fMax = pScale->fPlayerMax;
			}
			CostumeCreator_CommonSetBoneScale(pDst, fMin, fMax, pScale->pcName, fValue);
		}
	}

	if (bValidate) {
		Entity *pEnt = entActivePlayerPtr();
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		costumeTailor_MakeCostumeValid(pDst, GET_REF(pDst->hSpecies), g_CostumeEditState.eaUnlockedCostumes, pSlotType, true, false, false, guild_GetGuild(pEnt), false, pExtract, false, g_CostumeEditState.eaPowerFXBones);
	}

#undef COSTUME_PRESET_VALIDATE_VALUE
}

// Set the hover preset as an overlay
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverPresetOverlay");
bool CostumeCreator_SetHoverPresetOverlay(const char *pchPreset, bool bIgnoreHeight)
{
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	COSTUME_UI_TRACE_FUNC();
	if (!pchPreset || !*pchPreset) {
		if (g_CostumeEditState.pHoverCostume) {
			StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
		}
		g_CostumeEditState.pHoverCostume = NULL;
		CostumeUI_RegenCostume(true);
		return false;
	}
	if (pSkel && pSpecies && g_CostumeEditState.pCostume) {
		S32 i;
		for (i = 0; i < eaSize(&pSpecies->eaPresets); i++) {
			CostumePreset *pPreset = pSpecies->eaPresets[i];
			CostumePresetCategory *pPresetCat = GET_REF(pPreset->hPresetCategory);
			const char *name = pPreset->pcName || !pPresetCat ? pPreset->pcName : pPresetCat->pcName;
			if (!name) {
				continue;
			}
			if (!stricmp(name, pchPreset)) {
				PCSlotType *pSlotType = pPreset->pcSlotType || !pPresetCat ? costumeLoad_GetSlotType(pPreset->pcSlotType) : costumeLoad_GetSlotType(pPresetCat->pcSlotType);
				if (g_CostumeEditState.pHoverCostume) {
					StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
				}
				g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
				CostumeCreator_ApplyPresetOverlay(g_CostumeEditState.pHoverCostume, pPreset, bIgnoreHeight);
				costumeEntity_ApplyEntityInfoToCostume(CostumeUI_GetSourceEnt(),g_CostumeEditState.pConstHoverCostume);
				CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
				return true;
			}
		}
	}
	return false;
}

// Set the hover preset as an overlay
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetPresetOverlay");
bool CostumeCreator_SetPresetOverlay(const char *pchPreset, bool bIgnoreHeight)
{
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	COSTUME_UI_TRACE_FUNC();
	if (pSkel && pSpecies && g_CostumeEditState.pCostume) {
		S32 i;
		for (i = 0; i < eaSize(&pSpecies->eaPresets); i++) {
			CostumePreset *pPreset = pSpecies->eaPresets[i];
			CostumePresetCategory *pPresetCat = GET_REF(pPreset->hPresetCategory);
			const char *name = pPreset->pcName || !pPresetCat ? pPreset->pcName : pPresetCat->pcName;
			if (!name) {
				continue;
			}
			if (!stricmp(name, pchPreset)) {
				PCSlotType *pSlotType = pPreset->pcSlotType || !pPresetCat ? costumeLoad_GetSlotType(pPreset->pcSlotType) : costumeLoad_GetSlotType(pPresetCat->pcSlotType);
				if(!pPreset->bExcludeSlotType && (pPreset->pcSlotType || (pPresetCat && pPresetCat->pcSlotType)))
				{
					g_CostumeEditState.pSlotType = pSlotType;
				}
				CostumeCreator_ApplyPresetOverlay(g_CostumeEditState.pCostume, pPreset, bIgnoreHeight);
				CostumeUI_RegenCostume(true);
				return true;
			}
		}
	}
	return false;
}

// Set the hover geo on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetHoverCategory") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetHoverCategory(const char *pchCat);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverCategory");
bool CostumeCreator_SetHoverCategory(const char *pchCategory)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	Entity *pEnt = entActivePlayerPtr();
	bool bReset = !!g_CostumeEditState.pHoverCostume;
	COSTUME_UI_TRACE_FUNC();

	StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pHoverCostume);

	if (pchCategory && *pchCategory && g_CostumeEditState.pCostume) {
		g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);

		if (costumeTailor_SetCategory(g_CostumeEditState.pHoverCostume, pSpecies, GET_REF(g_CostumeEditState.hRegion), pchCategory,
			g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll))
		{
			costumeEntity_ApplyEntityInfoToCostume(CostumeUI_GetSourceEnt(), g_CostumeEditState.pConstHoverCostume);
			CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
			return true;
		}
	}

	if (bReset) {
		CostumeUI_RegenCostumeEx(true, true);
	}

	return false;
}

// Set the hover geo on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetHoverGeo") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetHoverGeo(const char *pchGeo);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverGeo");
bool CostumeCreator_SetHoverGeo(const char *pchGeo)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	Entity *pEnt = entActivePlayerPtr();
	bool bReset = !!g_CostumeEditState.pHoverCostume;
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	COSTUME_UI_TRACE_FUNC();

	StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pHoverCostume);

	if (pPart && !GET_REF(pPart->hBoneDef)) {
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	}

	if (pchGeo && *pchGeo && pPart) {
		g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);

		// Get part from hover costume
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, GET_REF(pPart->hBoneDef), NULL);

		if (costumeTailor_SetPartGeometry(g_CostumeEditState.pHoverCostume, pSpecies, GET_REF(pPart->hBoneDef), pchGeo,
			g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType,
			guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll, g_MirrorSelectMode, g_GroupSelectMode))
		{
			costumeEntity_ApplyEntityInfoToCostume(CostumeUI_GetSourceEnt(), g_CostumeEditState.pConstHoverCostume);
			CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
			return true;
		}
	}

	if (bReset) {
		CostumeUI_RegenCostumeEx(true, true);
	}

	return false;
}

// Set the hover material on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetHoverMaterial") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetHoverMaterial(const char *pchMat);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverMaterial");
bool CostumeCreator_SetHoverMaterial(const char *pchMat)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	Entity *pEnt = entActivePlayerPtr();
	bool bReset = !!g_CostumeEditState.pHoverCostume;
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	COSTUME_UI_TRACE_FUNC();

	StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pHoverCostume);

	if (pPart && !GET_REF(pPart->hBoneDef)) {
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	}

	if (pchMat && *pchMat && pPart) {
		g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);

		// Get part from hover costume
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, GET_REF(pPart->hBoneDef), NULL);

		if (costumeTailor_SetPartMaterial(g_CostumeEditState.pHoverCostume, pPart, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.eaBones, pchMat,
			g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, guild_GetGuild(pEnt),
			g_CostumeEditState.bUnlockAll, g_MirrorSelectMode))
		{
			CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
			return true;
		}
	}

	if (bReset) {
		CostumeUI_RegenCostumeEx(true, true);
	}

	return false;
}

// Set the hover material on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetHoverAllMaterials") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetHoverAllMaterials(const char *pchMat);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverAllMaterials");
bool CostumeCreator_SetHoverAllMaterials(const char *pchMat)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	Entity *pEnt = entActivePlayerPtr();
	bool bReset = !!g_CostumeEditState.pHoverCostume;
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	COSTUME_UI_TRACE_FUNC();

	StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pHoverCostume);

	if (pchMat && *pchMat && g_CostumeEditState.pCostume) {
		g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);

		if (costumeTailor_SetAllMaterials(g_CostumeEditState.pHoverCostume, pSpecies, pchMat, g_CostumeEditState.eaUnlockedCostumes,
			g_CostumeEditState.pSlotType, guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll))
		{
			CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
			return true;
		}
	}

	if (bReset) {
		CostumeUI_RegenCostumeEx(true, true);
	}

	return false;
}

// Set the hover pattern on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetHoverPattern") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetHoverPattern(const char *pchPattern);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverPattern");
bool CostumeCreator_SetHoverPattern(const char *pchPattern)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	Entity *pEnt = entActivePlayerPtr();
	bool bReset = !!g_CostumeEditState.pHoverCostume;
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	COSTUME_UI_TRACE_FUNC();

	StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pHoverCostume);

	if (pPart && !GET_REF(pPart->hBoneDef)) {
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	}

	if (pchPattern && *pchPattern && pPart) {
		g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);

		// Get part from hover costume
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, GET_REF(pPart->hBoneDef), NULL);

		if (costumeTailor_SetPartTexturePattern(g_CostumeEditState.pHoverCostume, pPart, GET_REF(g_CostumeEditState.hSpecies), pchPattern,
						g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll, g_MirrorSelectMode))
		{
			CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
			return true;
		}
	}

	if (bReset) {
		CostumeUI_RegenCostumeEx(true, true);
	}

	return false;
}

// Set the hover detail on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetHoverDetail") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetHoverDetail(const char *pchDetail);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverDetail");
bool CostumeCreator_SetHoverDetail(const char *pchDetail)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	Entity *pEnt = entActivePlayerPtr();
	bool bReset = !!g_CostumeEditState.pHoverCostume;
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	COSTUME_UI_TRACE_FUNC();

	StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pHoverCostume);

	if (pPart && !GET_REF(pPart->hBoneDef)) {
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	}

	if (pchDetail && *pchDetail && pPart) {
		g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);

		// Get part from hover costume
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, GET_REF(pPart->hBoneDef), NULL);

		if (costumeTailor_SetPartTextureDetail(g_CostumeEditState.pHoverCostume, pPart, GET_REF(g_CostumeEditState.hSpecies), pchDetail,
					g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll, g_MirrorSelectMode))
		{
			CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
			return true;
		}
	}

	if (bReset) {
		CostumeUI_RegenCostumeEx(true, true);
	}

	return false;
}

// Set the hover specular map on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetHoverSpecular") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetHoverSpecular(const char *pchSpecular);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverSpecular");
bool CostumeCreator_SetHoverSpecular(const char *pchSpecular)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	Entity *pEnt = entActivePlayerPtr();
	bool bReset = !!g_CostumeEditState.pHoverCostume;
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	COSTUME_UI_TRACE_FUNC();

	StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pHoverCostume);

	if (pPart && !GET_REF(pPart->hBoneDef)) {
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	}

	if (pchSpecular && *pchSpecular && pPart) {
		g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);

		// Get part from hover costume
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, GET_REF(pPart->hBoneDef), NULL);

		if (costumeTailor_SetPartTextureSpecular(g_CostumeEditState.pHoverCostume, pPart, GET_REF(g_CostumeEditState.hSpecies), pchSpecular,
					g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll, g_MirrorSelectMode))
		{
			CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
			return true;
		}
	}

	if (bReset) {
		CostumeUI_RegenCostumeEx(true, true);
	}

	return false;
}

// Set the hover pattern on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetHoverDiffuse") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetHoverDiffuse(const char *pchDiffuse);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverDiffuse");
bool CostumeCreator_SetHoverDiffuse(const char *pchDiffuse)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	Entity *pEnt = entActivePlayerPtr();
	bool bReset = !!g_CostumeEditState.pHoverCostume;
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	COSTUME_UI_TRACE_FUNC();

	StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pHoverCostume);

	if (!g_CostumeEditState.pCostume) {
		return false;
	}

	if (pPart && !GET_REF(pPart->hBoneDef)) {
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	}

	if (pchDiffuse && *pchDiffuse && pPart) {
		g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);

		// Get part from hover costume
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, GET_REF(pPart->hBoneDef), NULL);

		if (costumeTailor_SetPartTextureDiffuse(g_CostumeEditState.pHoverCostume, pPart, GET_REF(g_CostumeEditState.hSpecies), pchDiffuse,
					g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll, g_MirrorSelectMode))
		{
			CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
			return true;
		}
	}

	if (bReset) {
		CostumeUI_RegenCostumeEx(true, true);
	}

	return false;
}

// Set the hover pattern on the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetHoverMovable") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
bool CostumeCreator_SetHoverMovable(const char *pchMovable);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverMovable");
bool CostumeCreator_SetHoverMovable(const char *pchMovable)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	Entity *pEnt = entActivePlayerPtr();
	bool bReset = !!g_CostumeEditState.pHoverCostume;
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	COSTUME_UI_TRACE_FUNC();
	StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pHoverCostume);

	if (!g_CostumeEditState.pCostume) {
		return false;
	}

	if (pPart && !GET_REF(pPart->hBoneDef)) {
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	}

	if (pchMovable && *pchMovable && pPart) {
		g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);

		// Get part from hover costume
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, GET_REF(pPart->hBoneDef), NULL);

		if (costumeTailor_SetPartTextureMovable(g_CostumeEditState.pHoverCostume, pPart, GET_REF(g_CostumeEditState.hSpecies), pchMovable,
					g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, guild_GetGuild(pEnt), g_CostumeEditState.bUnlockAll, g_MirrorSelectMode))
		{
			CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
			return true;
		}
	}

	if (bReset) {
		CostumeUI_RegenCostumeEx(true, true);
	}

	return false;
}

// These three hacky wrappers that automatically fixup the passed in color are because previewing should display
// fixed up colors.

bool CostumeCreator_SetSkinColorFixup(NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, U8 skinColor[4])
{
	UIColorSet *pColorSet = costumeTailor_GetOverrideSkinColorSet(pSpecies, pSlotType);
	U8 correctColor[4];
	if (!pColorSet && pCostume && GET_REF(pCostume->hSkeleton)) {
		PCSkeletonDef *pSkel = GET_REF(pCostume->hSkeleton);
		pColorSet = GET_REF(pSkel->hSkinColorSet);
	}
	costumeTailor_FindClosestColor(skinColor, pColorSet, correctColor);
	return costumeTailor_SetSkinColor(pCostume, correctColor);
}

bool CostumeCreator_SetPartColor(NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, NOCONST(PCPart) *pPart, int iColor, U8 color[4], U8 glowScale)
{
	PCMaterialDef *pMat = SAFE_GET_REF(pPart, hMatDef);
	if ((iColor != kPCEditColor_Color3) || !pMat || !pMat->bHasSkin || gConf.bCostumeColorDoesNotAffectSkin) {
		UIColorSet *pColorSet = costumeTailor_GetColorSetForPart(pCostume, pSpecies, pSlotType, pPart, iColor);
		U8 correctColor[4];
		costumeTailor_FindClosestColor(color, pColorSet, correctColor);
		return costumeTailor_SetPartColor(pCostume, pSpecies, pSlotType, pPart, iColor, correctColor, glowScale);
	} else {
		return CostumeCreator_SetSkinColorFixup(pCostume, pSpecies, pSlotType, color);
	}
}

bool CostumeCreator_SetRealPartColor(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, int iColor, U8 color[4], PCSlotType *pSlotType, bool bMirrorSelectMode)
{
	PCMaterialDef *pMat = SAFE_GET_REF(pPart, hMatDef);
	if ((iColor != kPCEditColor_Color3) || !pMat || !pMat->bHasSkin || gConf.bCostumeColorDoesNotAffectSkin) {
		UIColorSet *pColorSet = costumeTailor_GetColorSetForPart(pCostume, pSpecies, pSlotType, pPart, iColor);
		U8 correctColor[4];
		costumeTailor_FindClosestColor(color, pColorSet, correctColor);
		return costumeTailor_SetRealPartColor(pCostume, pPart, pSpecies, iColor, correctColor, pSlotType, bMirrorSelectMode);
	} else {
		return CostumeCreator_SetSkinColorFixup(pCostume, pSpecies, pSlotType, color);
	}
}

void CostumeCreator_SetColorHelper(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, PCSlotType *pSlotType, int iColor, U8 color[4])
{
	S32 i;
	if (iColor < 4) {
		CostumeCreator_SetRealPartColor(pCostume, pPart, pSpecies, iColor, color, pSlotType, g_MirrorSelectMode);
	} else {
		if (kPCEditColor_SharedColor0 <= iColor && iColor <= kPCEditColor_SharedColor3) {
			// if setting a shared color, find a part that is the shared color and change it there
			for (i = 0; i < eaSize(&pCostume->eaParts); i++) {
				if (pCostume->eaParts[i]->eColorLink == kPCColorLink_All) {
					CostumeCreator_SetPartColor(pCostume, pSpecies, pSlotType, pCostume->eaParts[i], iColor - kPCEditColor_SharedColor0, color, 0);
					break;
				}
			}
		} else if (kPCEditColor_PerPartColor0 <= iColor && iColor <= kPCEditColor_PerPartColor2) {
			if (!GET_REF(pPart->hBoneDef)) {
				CostumeCreator_SetPartColor(pCostume, pSpecies, pSlotType, pPart, iColor - kPCEditColor_PerPartColor0, color, 0);
			} else {
				CostumeCreator_SetRealPartColor(pCostume, pPart, pSpecies, iColor - kPCEditColor_PerPartColor0, color, pSlotType, g_MirrorSelectMode);
			}
		}

		switch (iColor) {
			xcase kPCEditColor_PerPartColor3: {
				// kPCEditColor_PerPartColor3 is always supposed to set the 3rd part color,
				// even if it ends up not being used because it uses the skin color instead.
				// You can thank Dave for this quirk.
				PCBoneDef *pBone = GET_REF(pPart->hBoneDef);
				if (pBone && stricmp(pBone->pcName, "None")) {
					NOCONST(PCPart) *pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL);
					PCGeometryDef *pGeo = g_MirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Right) ? GET_REF(g_CostumeEditState.pPart->hGeoDef) : GET_REF(pRealPart->hGeoDef);
					NOCONST(PCPart) *pMirrorPart;
					if (g_MirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Both)) {
						pMirrorPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, g_CostumeEditState.pPart);
						if (pMirrorPart) {
							CostumeCreator_SetPartColor(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType, pMirrorPart, 3, color, GET_PART_GLOWSCALE(pMirrorPart,3));
						}
					}
					if (pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer && (pRealPart->eEditMode == kPCEditMode_Both)) {
						CostumeCreator_SetPartColor(pCostume, pSpecies, pSlotType, pRealPart, 3, color, GET_PART_GLOWSCALE(pRealPart,3));
						CostumeCreator_SetPartColor(pCostume, pSpecies, pSlotType, pRealPart->pClothLayer, 3, color, GET_PART_GLOWSCALE(pRealPart->pClothLayer,3));
					} else {
						CostumeCreator_SetPartColor(pCostume, pSpecies, pSlotType, pRealPart, 3, color, GET_PART_GLOWSCALE(pRealPart,3));
					}
				} else {
					// This would be the cloth layer part, use the default behavior.
					CostumeCreator_SetPartColor(pCostume, pSpecies, pSlotType, pPart, iColor - kPCEditColor_PerPartColor0, color, g_MirrorSelectMode);
				}
			}
			xcase kPCEditColor_Skin:
				CostumeCreator_SetSkinColorFixup(pCostume, pSpecies, pSlotType, color);
			xcase kPCEditColor_SharedColor0:
				COPY_COSTUME_COLOR(color, g_CostumeEditState.sharedColor0.color);
			xcase kPCEditColor_SharedColor1:
				COPY_COSTUME_COLOR(color, g_CostumeEditState.sharedColor1.color);
			xcase kPCEditColor_SharedColor2:
				COPY_COSTUME_COLOR(color, g_CostumeEditState.sharedColor2.color);
			xcase kPCEditColor_SharedColor3:
				COPY_COSTUME_COLOR(color, g_CostumeEditState.sharedColor3.color);
				break;
		}
	}
}

// Set the color of the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetHoverColor") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
bool CostumeCreator_SetHoverColor(S32 iColor, F32 fR, F32 fG, F32 fB, F32 fA);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverColor");
bool CostumeCreator_SetHoverColor(S32 iColor, F32 fR, F32 fG, F32 fB, F32 fA)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	NOCONST(PCPart) *pPart;
	U8 color[4] = { (U8)fR, (U8)fG, (U8)fB, (U8)fA};
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.pCostume && g_CostumeEditState.pPart && (fR >= 0))
	{
		if (!g_CostumeEditState.pHoverCostume) {
			g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
		}
		assert(g_CostumeEditState.pHoverCostume);

		// Find the part in the hover costume
		if (kPCEditColor_SharedColor0 <= iColor && iColor <= kPCEditColor_SharedColor3) {
			pPart = CostumeUI_GetSharedColorCostumePart();
			if (pPart) {
				pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, GET_REF(pPart->hBoneDef), g_CostumeEditState.pClothLayer);
			}
		} else {
			pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, GET_REF(g_CostumeEditState.hBone), g_CostumeEditState.pClothLayer);
		}

		if (pPart) {
			CostumeCreator_SetColorHelper(g_CostumeEditState.pHoverCostume, pPart, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType, iColor, color);
		}

		CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
		return true;
	} else {
		if (g_CostumeEditState.pHoverCostume) {
			StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
			g_CostumeEditState.pHoverCostume = NULL;
			CostumeUI_RegenCostume(true);
		}
	}
	return false;
}

// Set the selected part's color as a color value
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetBoneHoverColorValue");
bool CostumeCreator_SetBoneHoverColorValue(const char *pchBone, int iColor, F32 fR, F32 fG, F32 fB, F32 fA)
{
	PCBoneDef *pBone;
	U8 color[4] = { (U8)fR, (U8)fG, (U8)fB, (U8)fA };
	NOCONST(PCPart) *pPart;
	COSTUME_UI_TRACE_FUNC();

	pBone = CostumeUI_FindBone(pchBone, GET_REF(g_CostumeEditState.hSkeleton));

	if (g_CostumeEditState.pCostume && pBone && (fR >= 0)) {
		if (!g_CostumeEditState.pHoverCostume) {
			g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
		}
		assert(g_CostumeEditState.pHoverCostume);

		if (iColor == kPCEditColor_Skin)
		{
			if (g_CostumeEditState.pHoverCostume)
			{
				g_CostumeEditState.pHoverCostume->skinColor[0] = (U8)fR;
				g_CostumeEditState.pHoverCostume->skinColor[1] = (U8)fG;
				g_CostumeEditState.pHoverCostume->skinColor[2] = (U8)fB;
				g_CostumeEditState.pHoverCostume->skinColor[3] = (U8)fA;
				CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
				return true;
			}
			return false;
		}

		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, pBone, NULL);

		if (pPart) {
			CostumeCreator_SetColorHelper(g_CostumeEditState.pHoverCostume, pPart, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType, ABS(iColor) % 10, color);
		}

		CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
		return true;
	} else {
		if (g_CostumeEditState.pHoverCostume) {
			StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
			g_CostumeEditState.pHoverCostume = NULL;
			CostumeUI_RegenCostume(true);
		}
	}
	return false;
}

// Set the color of the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetColor") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetColor(S32 iColor, F32 fR, F32 fG, F32 fB, F32 fA);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetColor");
void CostumeCreator_SetColor(S32 iColor, F32 fR, F32 fG, F32 fB, F32 fA)
{
	U8 color[4] = { (U8)fR, (U8)fG, (U8)fB, (U8)fA};

	COSTUME_UI_TRACE_FUNC();
	if (!g_CostumeEditState.pCostume) {
		return;
	}

	CostumeCreator_SetColorHelper(g_CostumeEditState.pCostume, g_CostumeEditState.pPart, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType, iColor, color);
	CostumeUI_RegenCostume(true);
}

// Set the selected part's color as a color value
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetBoneColorValue");
void CostumeCreator_SetBoneColorValue(const char *pchBone, int iColor, F32 fR, F32 fG, F32 fB, F32 fA)
{
	PCBoneDef *pBone;
	U8 color[4] = { (U8)fR, (U8)fG, (U8)fB, (U8)fA };
	NOCONST(PCPart) *pPart;
	COSTUME_UI_TRACE_FUNC();

	if (iColor == kPCEditColor_Skin)
	{
		if (g_CostumeEditState.pCostume)
		{
			g_CostumeEditState.pCostume->skinColor[0] = (U8)fR;
			g_CostumeEditState.pCostume->skinColor[1] = (U8)fG;
			g_CostumeEditState.pCostume->skinColor[2] = (U8)fB;
			g_CostumeEditState.pCostume->skinColor[3] = (U8)fA;
			CostumeUI_RegenCostume(true);
		}
		return;
	}

	pBone = CostumeUI_FindBone(pchBone, GET_REF(g_CostumeEditState.hSkeleton));
	pPart = g_CostumeEditState.pCostume && pBone ? costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL) : NULL;

	if (pPart)
	{
		CostumeCreator_SetColorHelper(g_CostumeEditState.pCostume, pPart, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType, ABS(iColor) % 10, color);
		CostumeUI_RegenCostume(true);
	}
}

// Set the glow of a color
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetHoverGlow") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetHoverGlow(S32 iColor, int ubGlowScale);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetHoverGlow");
void CostumeCreator_SetHoverGlow(S32 iColor, int ubGlowScale)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	NOCONST(PCPart) *pPart;
	U8 tempColor[4];
	COSTUME_UI_TRACE_FUNC();

	if (!CostumeUI_MatHasGlow(iColor)) {
		return;
	}

	if (g_CostumeEditState.pCostume && g_CostumeEditState.pPart && (ubGlowScale >= 0) && (ubGlowScale <= 255))
	{
		if (!g_CostumeEditState.pHoverCostume) {
			g_CostumeEditState.pHoverCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
		}
		assert(g_CostumeEditState.pHoverCostume);

		// Find the part in the hover costume
		if (kPCEditColor_SharedColor0 <= iColor  && iColor <= kPCEditColor_SharedColor3) {
			pPart = CostumeUI_GetSharedColorCostumePart();
			if (pPart) {
				pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, GET_REF(pPart->hBoneDef), g_CostumeEditState.pClothLayer);
			}
		} else {
			pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pHoverCostume, GET_REF(g_CostumeEditState.hBone), g_CostumeEditState.pClothLayer);
		}
		if (pPart) {
			PCGeometryDef *pGeo;
			bool bBothMode = false;
			NOCONST(PCPart) *pMirrorPart = NULL;
			PCMaterialDef *pMirrorMat = NULL;
			PCMaterialDef *pMat = GET_REF(pPart->hMatDef);

			// Found the part, so change that part's color

			if (g_MirrorSelectMode && (pPart->eEditMode == kPCEditMode_Right)) {
				pPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pHoverCostume, pPart);
			}

			pGeo = GET_REF(pPart->hGeoDef);

			if (pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pPart->pClothLayer) {
				NOCONST(PCPart) *pMainRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
				if (pMainRealPart->eEditMode == kPCEditMode_Back) {
					pPart = pPart->pClothLayer;
				} else if (pMainRealPart->eEditMode == kPCEditMode_Both) {
					bBothMode = true;
				}
			}

			// Apply mirror change if appropriate
			if (g_MirrorSelectMode && pPart->eEditMode == kPCEditMode_Both) {
				pMirrorPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pHoverCostume, pPart);
				if (pMirrorPart) {
					pMirrorMat = GET_REF(pMirrorPart->hMatDef);
					if (iColor < 4) {
						if (iColor != 3) {
							VEC4_TO_COSTUME_COLOR(CostumeUI_GetColor(iColor)->color, tempColor);
							costumeTailor_SetPartColor(g_CostumeEditState.pHoverCostume, pSpecies, g_CostumeEditState.pSlotType, pMirrorPart, iColor, tempColor, ubGlowScale);
						} else {
							if (!(pMat && pMat->bHasSkin && (!pMirrorMat || pMirrorMat->bHasSkin))) {
								VEC4_TO_COSTUME_COLOR(CostumeUI_GetColor(iColor)->color, tempColor);
								costumeTailor_SetPartColor(g_CostumeEditState.pHoverCostume, pSpecies, g_CostumeEditState.pSlotType, pMirrorPart, iColor, tempColor, ubGlowScale);
							}
						}
					} else {
						if (kPCEditColor_PerPartColor0 <= iColor && iColor <= kPCEditColor_PerPartColor3) {
							VEC4_TO_COSTUME_COLOR(CostumeUI_GetColor(iColor)->color, tempColor);
							costumeTailor_SetPartColor(g_CostumeEditState.pHoverCostume, pSpecies, g_CostumeEditState.pSlotType, pMirrorPart, iColor - (iColor < kPCEditColor_PerPartColor0 ? kPCEditColor_SharedColor0 : kPCEditColor_PerPartColor0), tempColor, ubGlowScale);
						}
					}
				}
			}

			if (iColor < 4) {
				if (iColor != 3) {
					VEC4_TO_COSTUME_COLOR(CostumeUI_GetColor(iColor)->color, tempColor);
					costumeTailor_SetPartColor(g_CostumeEditState.pHoverCostume, pSpecies, g_CostumeEditState.pSlotType, pPart, iColor, tempColor, ubGlowScale);
					if (bBothMode) {
						VEC4_TO_COSTUME_COLOR(CostumeUI_GetColor(iColor)->color, tempColor);
						costumeTailor_SetPartColor(g_CostumeEditState.pHoverCostume, pSpecies, g_CostumeEditState.pSlotType, pPart->pClothLayer, iColor, tempColor, ubGlowScale);
					}
				} else {
					if (!(pMat && pMat->bHasSkin && (!pMirrorMat || pMirrorMat->bHasSkin))) {
						VEC4_TO_COSTUME_COLOR(CostumeUI_GetColor(iColor)->color, tempColor);
						costumeTailor_SetPartColor(g_CostumeEditState.pHoverCostume, pSpecies, g_CostumeEditState.pSlotType, pPart, iColor, tempColor, ubGlowScale);
						if (bBothMode) {
							costumeTailor_SetPartColor(g_CostumeEditState.pHoverCostume, pSpecies, g_CostumeEditState.pSlotType, pPart->pClothLayer, iColor, tempColor, ubGlowScale);
						}
					}
				}
			} else {
				if (kPCEditColor_PerPartColor0 <= iColor && iColor <= kPCEditColor_PerPartColor3) {
					VEC4_TO_COSTUME_COLOR(CostumeUI_GetColor(iColor)->color, tempColor);
					costumeTailor_SetPartColor(g_CostumeEditState.pHoverCostume, pSpecies, g_CostumeEditState.pSlotType, pPart, iColor - (iColor < kPCEditColor_PerPartColor0 ? kPCEditColor_SharedColor0 : kPCEditColor_PerPartColor0), tempColor, ubGlowScale);
					if (bBothMode) {
						costumeTailor_SetPartColor(g_CostumeEditState.pHoverCostume, pSpecies, g_CostumeEditState.pSlotType, pPart->pClothLayer, iColor - kPCEditColor_PerPartColor0, tempColor, ubGlowScale);
					}
				} else if (kPCEditColor_SharedColor0 <= iColor && iColor <= kPCEditColor_SharedColor3) {
					int i;
					for (i = eaSize(&g_CostumeEditState.pHoverCostume->eaParts) - 1; i >= 0; i--) {
						PCMaterialDef *pPartMat = GET_REF(g_CostumeEditState.pHoverCostume->eaParts[i]->hMatDef);
						if (g_CostumeEditState.pHoverCostume->eaParts[i]->eColorLink == kPCColorLink_All && pPartMat && pPartMat->pColorOptions && pPartMat->pColorOptions->bAllowGlow[iColor - 110]) {
							if (g_CostumeEditState.pHoverCostume->eaParts[i]->pCustomColors) {
								g_CostumeEditState.pHoverCostume->eaParts[i]->pCustomColors->glowScale[iColor - kPCEditColor_SharedColor0] = ubGlowScale;
							}
						}
					}
				}
			}

			CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
		}
		return;
	} else {
		if (g_CostumeEditState.pHoverCostume) {
			StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
			g_CostumeEditState.pHoverCostume = NULL;
			CostumeUI_RegenCostume(true);
		}
	}
	CostumeUI_RegenCostume(true);
}

// Set the color of the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetGlow") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetGlow(S32 iColor, U8 ubGlowScale);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetGlow");
void CostumeCreator_SetGlow(S32 iColor, U8 ubGlowScale)
{
	NOCONST(PCPart) *pRealPart;
	NOCONST(PCPart) *pMirrorPart = NULL;
	PCMaterialDef *pMirrorMat = NULL;
	PCGeometryDef *pGeo;
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCMaterialDef *pMat = SAFE_GET_REF(g_CostumeEditState.pPart, hMatDef);
	bool bBothMode = false;
	U8 tempColor[4];

	COSTUME_UI_TRACE_FUNC();

	if (!g_CostumeEditState.pCostume) {
		return;
	}

	if (!CostumeUI_MatHasGlow(iColor)) {
		return;
	}

	if (!GET_REF(g_CostumeEditState.hBone)) return;
	if (!stricmp(GET_REF(g_CostumeEditState.hBone)->pcName,"None")) return;
	pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	assert(pRealPart);
	if (!pMat) pMat = GET_REF(pRealPart->hMatDef);
	if (g_MirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Right)) {
		pGeo = GET_REF(g_CostumeEditState.pPart->hGeoDef);
	} else {
		pGeo = GET_REF(pRealPart->hGeoDef);
	}
	assert(pGeo);
	if (pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer && (pRealPart->eEditMode == kPCEditMode_Both)) {
		bBothMode = true;
	}

	// Apply mirror change if appropriate
	if (g_MirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Both)) {
		pMirrorPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, g_CostumeEditState.pPart);
		if (pMirrorPart) {
			pMirrorMat = GET_REF(pMirrorPart->hMatDef);
			if (iColor < 4) {
				if (!((iColor == 3) && pMirrorMat && pMat && pMat->bHasSkin && pMirrorMat->bHasSkin)) {
					VEC4_TO_COSTUME_COLOR(CostumeUI_GetColor(iColor)->color, tempColor);
					costumeTailor_SetPartColor(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, pMirrorPart, iColor, tempColor, ubGlowScale);
				}
			} else {
				if (120 <= iColor && iColor <= 123) {
					VEC4_TO_COSTUME_COLOR(CostumeUI_GetColor(iColor)->color, tempColor);
					costumeTailor_SetPartColor(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, pMirrorPart, iColor - 120, tempColor, ubGlowScale);
				}
			}
		}
	}

	if (iColor < 4) {
		if (iColor != 3) {
			VEC4_TO_COSTUME_COLOR(CostumeUI_GetColor(iColor)->color, tempColor);
			if (bBothMode) {
				costumeTailor_SetPartColor(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, pRealPart, iColor, tempColor, ubGlowScale);
				costumeTailor_SetPartColor(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, pRealPart->pClothLayer, iColor, tempColor, ubGlowScale);
			} else {
				costumeTailor_SetPartColor(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, g_CostumeEditState.pPart, iColor, tempColor, ubGlowScale);
			}
		} else {
			if (!(pMat && pMat->bHasSkin && (!pMirrorMat || pMirrorMat->bHasSkin))) {
				VEC4_TO_COSTUME_COLOR(CostumeUI_GetColor(iColor)->color, tempColor);
				if (bBothMode) {
					costumeTailor_SetPartColor(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, pRealPart, iColor, tempColor, ubGlowScale);
					costumeTailor_SetPartColor(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, pRealPart->pClothLayer, iColor, tempColor, ubGlowScale);
				} else {
					costumeTailor_SetPartColor(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, g_CostumeEditState.pPart, iColor, tempColor, ubGlowScale);
				}
			}
		}
	} else {
		NOCONST(PCPart) *pSharedPart = CostumeUI_GetSharedColorCostumePart();

		if (110 <= iColor && iColor <= 113) {
			g_CostumeEditState.sharedGlowScale[iColor - 110] = ubGlowScale;
			if (pSharedPart) {
				int i;
				for (i = eaSize(&g_CostumeEditState.pCostume->eaParts) - 1; i >= 0; i--) {
					PCMaterialDef *pPartMat = GET_REF(g_CostumeEditState.pCostume->eaParts[i]->hMatDef);
					if (g_CostumeEditState.pCostume->eaParts[i]->eColorLink == kPCColorLink_All && pPartMat && pPartMat->pColorOptions && pPartMat->pColorOptions->bAllowGlow[iColor - 110]) {
						if (g_CostumeEditState.pCostume->eaParts[i]->pCustomColors) {
							g_CostumeEditState.pCostume->eaParts[i]->pCustomColors->glowScale[iColor - 110] = ubGlowScale;
						}
					}
				}
			}
		} else if (120 <= iColor && iColor <= 123) {
			VEC4_TO_COSTUME_COLOR(CostumeUI_GetColor(iColor)->color, tempColor);
			if (bBothMode) {
				costumeTailor_SetPartColor(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, pRealPart, iColor - 120, tempColor, ubGlowScale);
				costumeTailor_SetPartColor(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, pRealPart->pClothLayer, iColor - 120, tempColor, ubGlowScale);
			} else {
				costumeTailor_SetPartColor(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, g_CostumeEditState.pPart, iColor - 120, tempColor, ubGlowScale);
			}
		}
	}
	CostumeUI_RegenCostume(true);
}

// Set the skin color of the costume
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetSkinColor") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetSkinColor(int uR, int uG, int uB);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetSkinColor");
void CostumeCreator_SetSkinColor(int uR, int uG, int uB)
{
	U8 color[4] = {uR, uG, uB, 255};
	COSTUME_UI_TRACE_FUNC();
	if (!g_CostumeEditState.pCostume) {
		return;
	}

	CostumeCreator_SetSkinColorFixup(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType, color);
	CostumeUI_RegenCostume(true);
}

// Set the color link type of the active part being edited
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetColorLink");
void CostumeCreator_SetColorLink(int /*PCColorLink*/ eLinkType)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	NOCONST(PCPart) *pRealPart;
	PCGeometryDef *pGeo;
	U8 zeroColor[4] = { 0,0,0,0 };
	TailorWeaponStance *pStance = g_CostumeEditState.pPart && g_CostumeEditState.pPart->eColorLink != kPCColorLink_None && eLinkType == kPCColorLink_None ? RefSystem_ReferentFromString(g_hWeaponStanceDict, REF_STRING_FROM_HANDLE(g_CostumeEditState.pPart->hBoneDef)) : NULL;
	COSTUME_UI_TRACE_FUNC();

	if (!g_CostumeEditState.pCostume) {
		return;
	}

	if (g_CostumeEditState.pPart && g_CostumeEditState.pPart->eColorLink == kPCColorLink_All) {
		COPY_COSTUME_COLOR(g_CostumeEditState.pPart->color0, g_CostumeEditState.sharedColor0.color);
		COPY_COSTUME_COLOR(g_CostumeEditState.pPart->color1, g_CostumeEditState.sharedColor1.color);
		COPY_COSTUME_COLOR(g_CostumeEditState.pPart->color2, g_CostumeEditState.sharedColor2.color);
		COPY_COSTUME_COLOR(g_CostumeEditState.pPart->color3, g_CostumeEditState.sharedColor3.color);
		if (g_CostumeEditState.pPart->pCustomColors) {
			COPY_COSTUME_COLOR(g_CostumeEditState.pPart->pCustomColors->glowScale, g_CostumeEditState.sharedGlowScale);
		} else {
			COPY_COSTUME_COLOR(zeroColor, g_CostumeEditState.sharedGlowScale);
		}
	}

	if (!GET_REF(g_CostumeEditState.hBone)) return;
	if (!stricmp(GET_REF(g_CostumeEditState.hBone)->pcName,"None")) return;
	pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	assert(pRealPart);
	if (g_MirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Right)) {
		pGeo = GET_REF(g_CostumeEditState.pPart->hGeoDef);
	} else {
		pGeo = GET_REF(pRealPart->hGeoDef);
	}
	assert(pGeo);
	if (pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer && (pRealPart->eEditMode == kPCEditMode_Both)) {
		costumeTailor_SetPartColorLinking(g_CostumeEditState.pCostume, pRealPart->pClothLayer, eLinkType, pSpecies, g_CostumeEditState.pSlotType);
		CostumeUI_SetColorsFromWeaponStance(pStance, pRealPart->pClothLayer);
	}
	costumeTailor_SetPartColorLinking(g_CostumeEditState.pCostume, g_CostumeEditState.pPart, eLinkType, pSpecies, g_CostumeEditState.pSlotType);
	CostumeUI_SetColorsFromWeaponStance(pStance, pRealPart);

	// Apply mirror change if appropriate
	if (g_MirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Both)) {
		NOCONST(PCPart) *pMirrorPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, pRealPart);
		if (pMirrorPart) {
			costumeTailor_SetPartColorLinking(g_CostumeEditState.pCostume, pMirrorPart, eLinkType, pSpecies, g_CostumeEditState.pSlotType);
			CostumeUI_SetColorsFromWeaponStance(pStance, pMirrorPart);
		}
	}

	CostumeUI_RegenCostume(true);
}

AUTO_COMMAND ACMD_NAME("CostumeCreator.SetColorLink") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetColorLinkCmd(PCColorLink eLinkType)
{
	CostumeCreator_SetColorLink(eLinkType);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetAllColorLink");
void CostumeCreator_SetAllColorLink(int /*PCColorLink*/ eLinkType)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	NOCONST(PCPart) *pRealPart;
	PCGeometryDef *pGeo;
	PCBoneDef *pBone;
	U8 zeroColor[4] = { 0,0,0,0 };
	int i;
	TailorWeaponStance *pStance;
	COSTUME_UI_TRACE_FUNC();

	if (!g_CostumeEditState.pCostume) {
		return;
	}

	if (g_CostumeEditState.pPart && g_CostumeEditState.pPart->eColorLink == kPCColorLink_All) {
		COPY_COSTUME_COLOR(g_CostumeEditState.pPart->color0, g_CostumeEditState.sharedColor0.color);
		COPY_COSTUME_COLOR(g_CostumeEditState.pPart->color1, g_CostumeEditState.sharedColor1.color);
		COPY_COSTUME_COLOR(g_CostumeEditState.pPart->color2, g_CostumeEditState.sharedColor2.color);
		COPY_COSTUME_COLOR(g_CostumeEditState.pPart->color3, g_CostumeEditState.sharedColor3.color);
		if (g_CostumeEditState.pPart->pCustomColors) {
			COPY_COSTUME_COLOR(g_CostumeEditState.pPart->pCustomColors->glowScale, g_CostumeEditState.sharedGlowScale);
		} else {
			COPY_COSTUME_COLOR(zeroColor, g_CostumeEditState.sharedGlowScale);
		}
	}

	for (i = eaSize(&g_CostumeEditState.pCostume->eaParts)-1; i >=0; --i)
	{
		pBone = GET_REF(g_CostumeEditState.pCostume->eaParts[i]->hBoneDef);
		assert(pBone);

		if (!GET_REF(g_CostumeEditState.hBone)) return;
		if (!stricmp(GET_REF(g_CostumeEditState.hBone)->pcName,"None")) return;
		pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL);
		assert(pRealPart);

		pStance = pRealPart->eColorLink != kPCColorLink_None && eLinkType == kPCColorLink_None ? RefSystem_ReferentFromString(g_hWeaponStanceDict, REF_STRING_FROM_HANDLE(pRealPart->hBoneDef)) : NULL;

		pGeo = GET_REF(pRealPart->hGeoDef);
		assert(pGeo);

		if (pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer) {
			costumeTailor_SetPartColorLinking(g_CostumeEditState.pCostume, pRealPart->pClothLayer, eLinkType, pSpecies, g_CostumeEditState.pSlotType);
			CostumeUI_SetColorsFromWeaponStance(pStance, pRealPart->pClothLayer);
		}
		costumeTailor_SetPartColorLinking(g_CostumeEditState.pCostume, pRealPart, eLinkType, pSpecies, g_CostumeEditState.pSlotType);
		CostumeUI_SetColorsFromWeaponStance(pStance, pRealPart);
	}

	CostumeUI_RegenCostume(true);
}

// Set the color link type of the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetAllColorLink") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetAllColorLinkCmd(PCColorLink eLinkType)
{
	CostumeCreator_SetAllColorLink(eLinkType);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetMaterialLink");
void CostumeCreator_SetMaterialLink(int /*PCColorLink*/ eLinkType)
{
	NOCONST(PCPart) *pRealPart;
	PCGeometryDef *pGeo;
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	COSTUME_UI_TRACE_FUNC();

	if (!g_CostumeEditState.pCostume) {
		return;
	}

	if (!GET_REF(g_CostumeEditState.hBone)) return;
	if (!stricmp(GET_REF(g_CostumeEditState.hBone)->pcName,"None")) return;
	pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	assert(pRealPart);
	if (g_MirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Right)) {
		pGeo = GET_REF(g_CostumeEditState.pPart->hGeoDef);
	} else {
		pGeo = GET_REF(pRealPart->hGeoDef);
	}
	assert(pGeo);
	if (pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer && (pRealPart->eEditMode == kPCEditMode_Both)) {
		costumeTailor_SetPartMaterialLinking(g_CostumeEditState.pCostume, pRealPart->pClothLayer, eLinkType, pSpecies, g_CostumeEditState.eaUnlockedCostumes, false);
	}
	costumeTailor_SetPartMaterialLinking(g_CostumeEditState.pCostume, g_CostumeEditState.pPart, eLinkType, pSpecies, g_CostumeEditState.eaUnlockedCostumes, false);

	// Apply mirror change if appropriate
	if (g_MirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Both)) {
		NOCONST(PCPart) *pMirrorPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, pRealPart);
		if (pMirrorPart) {
			costumeTailor_SetPartMaterialLinking(g_CostumeEditState.pCostume, pMirrorPart, eLinkType, pSpecies, g_CostumeEditState.eaUnlockedCostumes, false);
		}
	}

	CostumeUI_RegenCostume(true);
}

// Set the material link type of the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetMaterialLink") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetMaterialLinkCmd(PCColorLink eLinkType)
{
	CostumeCreator_SetMaterialLink(eLinkType);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetAllMaterialLink");
void CostumeCreator_SetAllMaterialLink(int /*PCColorLink*/ eLinkType)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	NOCONST(PCPart) *pRealPart;
	PCGeometryDef *pGeo;
	PCBoneDef *pBone;
	int i;
	COSTUME_UI_TRACE_FUNC();

	if (!g_CostumeEditState.pCostume) {
		return;
	}

	for (i = eaSize(&g_CostumeEditState.pCostume->eaParts)-1; i >=0; --i)
	{
		pBone = GET_REF(g_CostumeEditState.pCostume->eaParts[i]->hBoneDef);
		assert(pBone);

		if (!GET_REF(g_CostumeEditState.hBone)) return;
		if (!stricmp(GET_REF(g_CostumeEditState.hBone)->pcName,"None")) return;
		pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL);
		assert(pRealPart);

		pGeo = GET_REF(pRealPart->hGeoDef);
		assert(pGeo);

		if (pGeo->pClothData && pGeo->pClothData->bIsCloth) {
			costumeTailor_SetPartMaterialLinking(g_CostumeEditState.pCostume, pRealPart->pClothLayer, eLinkType, pSpecies, g_CostumeEditState.eaUnlockedCostumes, false);
		}
		costumeTailor_SetPartMaterialLinking(g_CostumeEditState.pCostume, pRealPart, eLinkType, pSpecies, g_CostumeEditState.eaUnlockedCostumes, false);
	}

	CostumeUI_RegenCostume(true);
}

// Set the color link type of the active part being edited
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetAllMaterialLink") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetAllMaterialLinkCmd(PCColorLink eLinkType)
{
	CostumeCreator_SetAllMaterialLink(eLinkType);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME("Tailor_EnableTailor") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void CostumeCreator_EnableTailor(Entity *pEnt, bool bEnabled)
{
	COSTUME_UI_TRACE_FUNC();
	g_CostumeEditState.bEnableTailor = bEnabled;
	g_CostumeEditState.bTailorReady = true;
}

//////////////////////////////////////////////////////////////////////////

// Get the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditCostume");
SA_RET_NN_VALID PlayerCostume *CostumeCreator_GetEditCostume(void)
{
	COSTUME_UI_TRACE_FUNC();
	return g_CostumeEditState.pConstCostume;
}

// Get the part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditCostumePart");
SA_RET_NN_VALID PCPart *CostumeCreator_GetEditCostumePart(void)
{
	COSTUME_UI_TRACE_FUNC();
	return g_CostumeEditState.pConstPart;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditSpeciesDispName");
const char *CostumeCreator_GetEditSpeciesDispName(void)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.pConstCostume->hSpecies);
	COSTUME_UI_TRACE_FUNC();
	if (pSpecies)
	{
		return TranslateDisplayMessage(pSpecies->displayNameMsg);
	}
	pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	if (pSpecies)
	{
		return TranslateDisplayMessage(pSpecies->displayNameMsg);
	}
	return "";
}

// Get the current part color
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetUniqueColor");
SA_RET_NN_VALID UIColor *CostumeCreator_GetUniqueColor(int iColor)
{
	COSTUME_UI_TRACE_FUNC();
	return CostumeUI_GetColor(iColor + kPCEditColor_PerPartColor0);
}

// Get the current part color
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetUniqueColorValue");
int CostumeCreator_GetUniqueColorValue(int iColor)
{
	UIColor *pColor = CostumeUI_GetColor(iColor + kPCEditColor_PerPartColor0);
	COSTUME_UI_TRACE_FUNC();
	return ((int)pColor->color[0] << 24) | ((int)pColor->color[1] << 16) | ((int)pColor->color[2] << 8) | ((int)pColor->color[3]);
}

// Get the skin color
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetSkinColor");
SA_RET_NN_VALID UIColor *CostumeCreator_GetSkinColor(void)
{
	COSTUME_UI_TRACE_FUNC();
	return CostumeUI_GetColor(kPCEditColor_Skin);
}

// Get the skin color as avalue
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetSkinColorValue");
int CostumeCreator_GetSkinColorValue(void)
{
	UIColor *pColor = CostumeUI_GetColor(kPCEditColor_Skin);
	COSTUME_UI_TRACE_FUNC();
	return ((int)pColor->color[0] << 24) | ((int)pColor->color[1] << 16) | ((int)pColor->color[2] << 8) | ((int)pColor->color[3]);
}

// Get the shared color
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetSharedColor");
SA_RET_NN_VALID UIColor *CostumeCreator_GetSharedColor(int iColor)
{
	COSTUME_UI_TRACE_FUNC();
	return CostumeUI_GetColor(iColor + kPCEditColor_SharedColor0);
}

// Get the shared color as a value
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetSharedColorValue");
int CostumeCreator_GetSharedColorValue(int iColor)
{
	UIColor *pColor = CostumeUI_GetColor(iColor + kPCEditColor_SharedColor0);
	COSTUME_UI_TRACE_FUNC();
	return ((int)pColor->color[0] << 24) | ((int)pColor->color[1] << 16) | ((int)pColor->color[2] << 8) | ((int)pColor->color[3]);
}

// Get the current part's color as a UIColor
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditColor");
SA_RET_NN_VALID UIColor *CostumeCreator_GetEditColor(int iColor)
{
	COSTUME_UI_TRACE_FUNC();
	return CostumeUI_GetColor(iColor);
}

// Get the current part's color as a color value
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditColorValue");
int CostumeCreator_GetEditColorValue(int iColor)
{
	UIColor *pColor = CostumeUI_GetColor(iColor);
	COSTUME_UI_TRACE_FUNC();
	return ((int)pColor->color[0] << 24) | ((int)pColor->color[1] << 16) | ((int)pColor->color[2] << 8) | ((int)pColor->color[3]);
}

// Get the selected part's color as a color value
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBoneColorValue");
int CostumeCreator_GetBoneColorValue(const char *pchBone, int iColor)
{
	PCBoneDef *pBone = CostumeUI_FindBone(pchBone, GET_REF(g_CostumeEditState.hSkeleton));
	U8 auDefault[4] = { 0, 0, 0, 255 };
	const U8 *puColor = auDefault;

	if (g_CostumeEditState.pCostume && iColor == kPCEditColor_Skin)
	{
		puColor = g_CostumeEditState.pCostume->skinColor;
	}
	else if (g_CostumeEditState.pCostume && pBone)
	{
		NOCONST(PCPart) *pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL);

		switch (ABS(iColor) % 10)
		{
		xcase kPCEditColor_Color0:
			puColor = pPart->color0;
		xcase kPCEditColor_Color1:
			puColor = pPart->color1;
		xcase kPCEditColor_Color2:
			puColor = pPart->color2;
		xcase kPCEditColor_Color3:
			puColor = pPart->color3;
		}
	}

	return ((puColor[0] & 0xff) << 24) | ((puColor[1] & 0xff) << 16) | ((puColor[2] & 0xff) << 8) | (puColor[3] & 0xff);
}

static UIColorSet *CostumeCreator_GetColorSetByNumber(PCBoneDef *pBone, int iColor)
{
	bool bIsSpecificPartColor = iColor > kPCEditColor_Skin;
	int iColorIndex = 0;
	UIColorSet *pColorSet = NULL;
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	NOCONST(PCPart) *pPart = pBone ? costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, g_CostumeEditState.pCurrentLayer) : g_CostumeEditState.pPart;
	PCMaterialDef *pMat = SAFE_GET_REF(pPart, hMatDef);

	switch (iColor)
	{
	case kPCEditColor_Color0:
	case kPCEditColor_SharedColor0:
	case kPCEditColor_PerPartColor0:
		iColorIndex = kPCEditColor_Color0;
		break;
	case kPCEditColor_Color1:
	case kPCEditColor_SharedColor1:
	case kPCEditColor_PerPartColor1:
		iColorIndex = kPCEditColor_Color1;
		break;
	case kPCEditColor_Color2:
	case kPCEditColor_SharedColor2:
	case kPCEditColor_PerPartColor2:
		iColorIndex = kPCEditColor_Color2;
		break;
	case kPCEditColor_Color3:
	case kPCEditColor_SharedColor3:
	case kPCEditColor_PerPartColor3:
		iColorIndex = kPCEditColor_Color3;
		break;
	}

	if (pMat && pMat->bHasSkin && iColorIndex == kPCEditColor_Color3 && !bIsSpecificPartColor || iColor == kPCEditColor_Skin)
	{
		pColorSet = costumeTailor_GetOverrideSkinColorSet(pSpecies, g_CostumeEditState.pSlotType);
		if (pSkel && !pColorSet) {
			pColorSet = GET_REF(pSkel->hSkinColorSet);
		}
	}
	if (!pColorSet)
	{
		pColorSet = costumeTailor_GetColorSetForPart(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, pPart, iColorIndex);
	}
	if (!pColorSet && pSkel)
	{
		pColorSet = GET_REF(pSkel->hBodyColorSet);
	}

	return pColorSet;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditColorNumber");
S32 CostumeCreator_GetEditColorNumber(int iColor)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	PCMaterialDef *pMat = SAFE_GET_REF(g_CostumeEditState.pPart, hMatDef);
	UIColorSet *pColorSet = NULL;
	F32 colorDif0, colorDif1, colorDif2, colorDif3;
	F32 minDist = 0, dist;
	int closest = -1;
	COSTUME_UI_TRACE_FUNC();

	if (pSkel && g_CostumeEditState.pPart) {
		pColorSet = CostumeCreator_GetColorSetByNumber(NULL, iColor);
		if (pColorSet) {
			int i;
			Vec4 temp;

			switch (iColor)
			{
			case kPCEditColor_Skin:
				temp[0] = g_CostumeEditState.pCostume->skinColor[0];
				temp[1] = g_CostumeEditState.pCostume->skinColor[1];
				temp[2] = g_CostumeEditState.pCostume->skinColor[2];
				temp[3] = g_CostumeEditState.pCostume->skinColor[3];
			xcase kPCEditColor_Color0:
			case kPCEditColor_PerPartColor0:
				temp[0] = g_CostumeEditState.pPart->color0[0];
				temp[1] = g_CostumeEditState.pPart->color0[1];
				temp[2] = g_CostumeEditState.pPart->color0[2];
				temp[3] = g_CostumeEditState.pPart->color0[3];
			xcase kPCEditColor_Color1:
			case kPCEditColor_PerPartColor1:
				temp[0] = g_CostumeEditState.pPart->color1[0];
				temp[1] = g_CostumeEditState.pPart->color1[1];
				temp[2] = g_CostumeEditState.pPart->color1[2];
				temp[3] = g_CostumeEditState.pPart->color1[3];
			xcase kPCEditColor_Color2:
			case kPCEditColor_PerPartColor2:
				temp[0] = g_CostumeEditState.pPart->color2[0];
				temp[1] = g_CostumeEditState.pPart->color2[1];
				temp[2] = g_CostumeEditState.pPart->color2[2];
				temp[3] = g_CostumeEditState.pPart->color2[3];
			xcase kPCEditColor_Color3:
				if (pMat && pMat->bHasSkin)
				{
					temp[0] = g_CostumeEditState.pCostume->skinColor[0];
					temp[1] = g_CostumeEditState.pCostume->skinColor[1];
					temp[2] = g_CostumeEditState.pCostume->skinColor[2];
					temp[3] = g_CostumeEditState.pCostume->skinColor[3];
					break;
				}
			//fall through
			case kPCEditColor_PerPartColor3:
				temp[0] = g_CostumeEditState.pPart->color3[0];
				temp[1] = g_CostumeEditState.pPart->color3[1];
				temp[2] = g_CostumeEditState.pPart->color3[2];
				temp[3] = g_CostumeEditState.pPart->color3[3];
			xcase kPCEditColor_SharedColor0:
				temp[0] = g_CostumeEditState.sharedColor0.color[0];
				temp[1] = g_CostumeEditState.sharedColor0.color[1];
				temp[2] = g_CostumeEditState.sharedColor0.color[2];
				temp[3] = g_CostumeEditState.sharedColor0.color[3];
			xcase kPCEditColor_SharedColor1:
				temp[0] = g_CostumeEditState.sharedColor1.color[0];
				temp[1] = g_CostumeEditState.sharedColor1.color[1];
				temp[2] = g_CostumeEditState.sharedColor1.color[2];
				temp[3] = g_CostumeEditState.sharedColor1.color[3];
			xcase kPCEditColor_SharedColor2:
				temp[0] = g_CostumeEditState.sharedColor2.color[0];
				temp[1] = g_CostumeEditState.sharedColor2.color[1];
				temp[2] = g_CostumeEditState.sharedColor2.color[2];
				temp[3] = g_CostumeEditState.sharedColor2.color[3];
			xcase kPCEditColor_SharedColor3:
				temp[0] = g_CostumeEditState.sharedColor3.color[0];
				temp[1] = g_CostumeEditState.sharedColor3.color[1];
				temp[2] = g_CostumeEditState.sharedColor3.color[2];
				temp[3] = g_CostumeEditState.sharedColor3.color[3];
				break;
			default:
				return -1;
			}

			for (i=eaSize(&pColorSet->eaColors)-1; i >= 0; --i)
			{
				colorDif0 = pColorSet->eaColors[i]->color[0] - temp[0];
				colorDif1 = pColorSet->eaColors[i]->color[1] - temp[1];
				colorDif2 = pColorSet->eaColors[i]->color[2] - temp[2];
				colorDif3 = pColorSet->eaColors[i]->color[3] - temp[3];
#define MAX_COLOR_DIF (0.99)
				if(colorDif0 >= -MAX_COLOR_DIF && colorDif0 <= MAX_COLOR_DIF &&
					colorDif1 >= -MAX_COLOR_DIF && colorDif1 <= MAX_COLOR_DIF &&
					colorDif2 >= -MAX_COLOR_DIF && colorDif2 <= MAX_COLOR_DIF &&
					colorDif3 >= -MAX_COLOR_DIF && colorDif3 <= MAX_COLOR_DIF
					)
				{
					dist = colorDif0*colorDif0 + colorDif1*colorDif1 + colorDif2*colorDif2 + colorDif3*colorDif3;
					if (dist == 0) return i;
					if (closest == -1 || dist < minDist)
					{
						minDist = dist;
						closest = i;
					}
				}
			}
			return closest;
		}
	}

	return -1;
}

// Get the player's active costume type
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPlayerActiveCostumeType");
int CostumeCreator_GetPlayerActiveCostumeType(void)
{
	COSTUME_UI_TRACE_FUNC();
	// This function is now obsolete.  Need to remove it when the gens no longer try to use it.
	return 0;
}

// Get the player's active costume index
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPlayerActiveCostumeIndex");
int CostumeCreator_GetPlayerActiveCostumeIndex(void)
{
	Entity *pEnt = entActivePlayerPtr();
	COSTUME_UI_TRACE_FUNC();
	if (pEnt) {
		return pEnt->pSaved->costumeData.iActiveCostume;
	}
	return 0;
}

// Get the costume index that the UI should automatically start editing, rather than letting
//  the player choose.  Returns -1 if there isn't one.  Automatically clears the value when called.
AUTO_EXPR_FUNC(UIGen);
int CostumeCreator_GetAutoEditCostumeIndex(void)
{
	int i = g_CostumeEditState.iAutoEditIndex;
	COSTUME_UI_TRACE_FUNC();
	g_CostumeEditState.iAutoEditIndex = -1;
	return i;
}

// Sets the Client's AutoEditCostumeIndex and shows the Tailor UI (which should then start editing
//  that costume index)
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void Tailor_AutoEditCostumeIndex(int index)
{
	g_CostumeEditState.iAutoEditIndex = index;
	COSTUME_UI_TRACE_FUNC();
	globCmdParse("GenSendMessage Root ShowTailor");
}

// Find out if can go back on random
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_NumBackRandom");
int CostumeCreator_NumBackRandom(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaiSize(&g_CostumeEditState.eaiSeeds);
}

// Get the number of slot types available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetSlotTypeListSize");
S32 CostumeCreator_GetSlotTypeListSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaSlotTypes);
}

// Get the list of slot types available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetSlotTypeList");
void CostumeCreator_GetSlotTypeList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaSlotTypes, parse_PCSlotType);
}

// Get the list of skeletons available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetSkeletonList");
void CostumeCreator_GetSkeletonList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaSkeletons, parse_PCSkeletonDef);
}

// Get the name of a skeleton for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetSkeletonSysName");
const char *CostumeCreator_GetSkeletonSysName(void)
{
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	COSTUME_UI_TRACE_FUNC();

	if (!pSkel) return "";

	return pSkel->pcName;
}

// Find a particular preset
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_FindPreset");
SA_RET_OP_VALID CostumePreset *CostumeCreator_FindPreset(const char *pchGroup, const char *pchSlotType)
{
	S32 i;
	COSTUME_UI_TRACE_FUNC();
	pchSlotType = allocFindString(pchSlotType);
	pchGroup = allocFindString(pchGroup);
	for (i = 0; i < eaSize(&g_CostumeEditState.eaPresets); ++i) {
		CostumePreset *pSourcePreset = g_CostumeEditState.eaPresets[i];
		CostumePresetCategory *pCategory = pSourcePreset->bOverrideExcludeValues ? NULL : GET_REF(pSourcePreset->hPresetCategory);
		bool bExcludeGroup = pCategory ? pCategory->bExcludeGroup : pSourcePreset->bExcludeGroup;
		const char *pcGroup = pCategory ? pCategory->pcGroup : pSourcePreset->pcGroup;
		if (!pchGroup || !((bExcludeGroup && pchGroup == pcGroup) || (!bExcludeGroup && pchGroup != pcGroup))) {
			if (!pchSlotType || pSourcePreset->pcSlotType && !stricmp(pchSlotType, pSourcePreset->pcSlotType)) {
				return pSourcePreset;
			}
		}
	}
	return NULL;
}

// Get the list of presets available for the current costume and species
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPresetListGroup");
void CostumeCreator_GetPresetListGroup(SA_PARAM_NN_VALID UIGen *pGen, const char *pchGroup)
{
	CostumePreset ***peaPresetCopy = ui_GenGetManagedListSafe(pGen, CostumePreset);
	S32 i, iCount = 0;
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	pchGroup = allocFindString(pchGroup);
	for (i = 0; i < eaSize(&g_CostumeEditState.eaPresets); ++i) {
		CostumePreset *pSourcePreset = g_CostumeEditState.eaPresets[i];
		CostumePresetCategory *pCategory = pSourcePreset->bOverrideExcludeValues ? NULL : GET_REF(pSourcePreset->hPresetCategory);
		bool bExcludeGroup = pCategory ? pCategory->bExcludeGroup : pSourcePreset->bExcludeGroup;
		bool bExcludeSlotType = pCategory ? pCategory->bExcludeSlotType : pSourcePreset->bExcludeSlotType;
		const char *pcGroup = pCategory ? pCategory->pcGroup : pSourcePreset->pcGroup;
		if (!pchGroup || !((bExcludeGroup && !bExcludeSlotType && pchGroup == pcGroup) || (!bExcludeGroup && pchGroup != pcGroup))) {
			CostumePreset *pPreset = eaGetStruct(peaPresetCopy, parse_CostumePreset, iCount++);
			StructCopyAll(parse_CostumePreset, pSourcePreset, pPreset);
			// Grab values from the category and update the cached copy
			if (pCategory) {
				pPreset->bExcludeGroup = pCategory->bExcludeGroup;
				pPreset->bExcludeSlotType = pCategory->bExcludeSlotType;
				pPreset->pcGroup = pCategory->pcGroup;
				pPreset->pcSlotType = pCategory->pcSlotType;
			}
		}
	}
	eaSetSizeStruct(peaPresetCopy, parse_CostumePreset, iCount++);
	ui_GenSetManagedListSafe(pGen, peaPresetCopy, CostumePreset, true);
}

// Get the list of presets available for the current costume and species
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPresetList");
void CostumeCreator_GetPresetList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeCreator_GetPresetListGroup(pGen, NULL);
}

// Get the list of presets available for the current costume and species
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPresetListSize");
int CostumeCreator_GetPresetListSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaPresets);
}

// Get the list of stances available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetStanceList");
void CostumeCreator_GetStanceList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaStances, parse_PCStanceInfo);
}

// Get the size of the list of stances available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetStanceListSize");
int CostumeCreator_GetStanceListSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaStances);
}

// Get the current stance being edited
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditStance");
SA_RET_OP_VALID PCStanceInfo *CostumeCreator_GetEditStance(void)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel;
	int i;
	COSTUME_UI_TRACE_FUNC();
	
	pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	if (pSkel && g_CostumeEditState.pCostume) {
		if (pSpecies && eaSize(&pSpecies->eaStanceInfo))
		{
			if (g_CostumeEditState.pCostume->pcStance) {
				for(i=eaSize(&pSpecies->eaStanceInfo)-1; i>=0; --i) {
					if (stricmp(pSpecies->eaStanceInfo[i]->pcName, g_CostumeEditState.pCostume->pcStance) == 0) {
						return pSpecies->eaStanceInfo[i];
					}
				}
			} else if (pSpecies->pcDefaultStance) {
				for(i=eaSize(&pSpecies->eaStanceInfo)-1; i>=0; --i) {
					if (stricmp(pSpecies->eaStanceInfo[i]->pcName, pSpecies->pcDefaultStance) == 0) {
						return pSpecies->eaStanceInfo[i];
					}
				}
			}
		}
		else
		{
			if (g_CostumeEditState.pCostume->pcStance) {
				for(i=eaSize(&pSkel->eaStanceInfo)-1; i>=0; --i) {
					if (stricmp(pSkel->eaStanceInfo[i]->pcName, g_CostumeEditState.pCostume->pcStance) == 0) {
						return pSkel->eaStanceInfo[i];
					}
				}
			} else
			{
				if (pSpecies && pSpecies->pcDefaultStance) {
					for(i=eaSize(&pSkel->eaStanceInfo)-1; i>=0; --i) {
						if (stricmp(pSkel->eaStanceInfo[i]->pcName, pSpecies->pcDefaultStance) == 0) {
							return pSkel->eaStanceInfo[i];
						}
					}
				} else if (pSkel->pcDefaultStance) {
					for(i=eaSize(&pSkel->eaStanceInfo)-1; i>=0; --i) {
						if (stricmp(pSkel->eaStanceInfo[i]->pcName, pSkel->pcDefaultStance) == 0) {
							return pSkel->eaStanceInfo[i];
						}
					}
				}
			}
		}
	}
	return NULL;
}

// Get the list of stances available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetVoiceList");
void CostumeCreator_GetVoiceList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaVoices, parse_PCVoice);
}

// Get the size of the list of voices available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetVoiceListSize");
int CostumeCreator_GetVoiceListSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaVoices);
}

// Get the current stance being edited
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditVoice");
SA_RET_OP_VALID PCVoice *CostumeCreator_GetEditVoice(void)
{
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	COSTUME_UI_TRACE_FUNC();
	if (pSkel && g_CostumeEditState.pCostume) {
		return GET_REF(g_CostumeEditState.pCostume->hVoice);
	}
	return NULL;
}

// Get the list of moods available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMoodList");
void CostumeCreator_GetMoodList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaMoods, parse_PCMood);
}

// Get the size of the list of moods available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMoodListSize");
int CostumeCreator_GetMoodListSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaMoods);
}

// Get the current mood being edited
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditMood");
SA_RET_OP_VALID PCMood *CostumeCreator_GetEditMood(void)
{
	COSTUME_UI_TRACE_FUNC();
	return GET_REF(g_CostumeEditState.hMood);
}

// Get the list of body scales available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBodyScaleList");
void CostumeCreator_GetBodyScaleList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaBodyScales, parse_PCBodyScaleInfo);
}

// Get a specific body scale value list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_GetBodyScaleValues);
void CostumeCreator_GetBodyScaleValues(SA_PARAM_NN_VALID UIGen *pGen, const char *pchScaleName)
{
	S32 i;
	PCBodyScaleInfo *pInfo = NULL;
	PCBodyScaleValue ***peaValues = NULL;
	PCSkeletonDef *pSkeleton = GET_REF(g_CostumeEditState.hSkeleton);
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSlotType *pSlotType = g_CostumeEditState.pSlotType;

	COSTUME_UI_TRACE_FUNC();
	pchScaleName = pchScaleName && *pchScaleName ? allocFindString(pchScaleName) : NULL;

	if (!pchScaleName || !pSkeleton) {
		ui_GenSetList(pGen, NULL, parse_PCBodyScaleValue);
		return;
	}

	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}

	for (i = eaSize(&g_CostumeEditState.eaBodyScales) - 1; i >= 0; i--) {
		if (g_CostumeEditState.eaBodyScales[i]->pcName == pchScaleName) {
			pInfo = g_CostumeEditState.eaBodyScales[i];
			break;
		}
	}

	if (pInfo) {
		F32 fMin = 1, fMax = -1;
		S32 iIndex = -1;

		for (i = eaSize(&pSkeleton->eaBodyScaleInfo)-1; i >= 0; --i) {
			if (pSkeleton->eaBodyScaleInfo[i]->pcName == pchScaleName) {
				iIndex = i;
				break;
			}
		}

		if(!costumeTailor_GetOverrideBodyScale(pSkeleton, pInfo->pcName, pSpecies, pSlotType, &fMin, &fMax))
		{
			if (iIndex >= 0 && iIndex < eaSize(&pSkeleton->eaBodyScaleInfo))
			{
				fMin = pSkeleton->eafPlayerMinBodyScales[i];
				fMax = pSkeleton->eafPlayerMaxBodyScales[i];
			}
		}

		peaValues = ui_GenGetManagedListSafe(pGen, PCBodyScaleValue);
		eaCopy(peaValues, &pInfo->eaValues);

		for (i = eaSize(peaValues)-1; i >= 0; --i)
		{
			if((*peaValues)[i]->fValue > fMax || (*peaValues)[i]->fValue < fMin)
			{
				eaRemove(peaValues, i);
				continue;
			}
		}

		ui_GenSetManagedListSafe(pGen, peaValues, PCBodyScaleValue, false);
	} else {
		ui_GenSetList(pGen, NULL, parse_PCBodyScaleValue);
	}
}

// Get the size of a specific body scale value list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_GetBodyScaleValuesSize);
S32 CostumeCreator_GetBodyScaleValuesSize(const char *pchScaleName)
{
	S32 i;
	PCBodyScaleInfo *pInfo = NULL;
	PCSkeletonDef *pSkeleton = GET_REF(g_CostumeEditState.hSkeleton);
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSlotType *pSlotType = g_CostumeEditState.pSlotType;
	COSTUME_UI_TRACE_FUNC();

	COSTUME_UI_TRACE_FUNC();
	pchScaleName = pchScaleName && *pchScaleName ? allocFindString(pchScaleName) : NULL;

	if (!pchScaleName) {
		return 0;
	}

	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		// Note this must be done, unlike the others since this function accesses
		// the contents of the body scales.
		CostumeUI_RegenCostume(true);
	}

	for (i = eaSize(&g_CostumeEditState.eaBodyScales) - 1; i >= 0; i--) {
		if (g_CostumeEditState.eaBodyScales[i]->pcName == pchScaleName) {
			return eaSize(&g_CostumeEditState.eaBodyScales[i]->eaValues);
		}
	}

	for (i = eaSize(&g_CostumeEditState.eaBodyScales) - 1; i >= 0; i--) {
		if (g_CostumeEditState.eaBodyScales[i]->pcName == pchScaleName) {
			pInfo = g_CostumeEditState.eaBodyScales[i];
			break;
		}
	}

	if (pInfo) {
		S32 iCount = 0;
		F32 fMin = 1, fMax = -1;
		S32 iIndex = -1;

		for (i = eaSize(&pSkeleton->eaBodyScaleInfo)-1; i >= 0; --i) {
			if (pSkeleton->eaBodyScaleInfo[i]->pcName == pchScaleName) {
				iIndex = i;
				break;
			}
		}

		if(!costumeTailor_GetOverrideBodyScale(pSkeleton, pInfo->pcName, pSpecies, pSlotType, &fMin, &fMax))
		{
			if (iIndex >= 0 && iIndex < eaSize(&pSkeleton->eaBodyScaleInfo))
			{
				fMin = pSkeleton->eafPlayerMinBodyScales[i];
				fMax = pSkeleton->eafPlayerMaxBodyScales[i];
			}
		}

		for (i = eaSize(&pInfo->eaValues)-1; i >= 0; --i)
		{
			if(!(pInfo->eaValues[i]->fValue > fMax || pInfo->eaValues[i]->fValue < fMin))
			{
				iCount++;
			}
		}

		return iCount;
	}

	return 0;
}

// Get a specific list of body scale sliders
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_FilterBodyScaleList);
void CostumeCreator_FilterBodyScaleList(SA_PARAM_OP_VALID UIGen *pGen, const char *pchIncludeList, const char *pchExcludeList)
{
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	bool bIncludeAll = !pchIncludeList || !*pchIncludeList;
	bool bExcludeAll = !pchExcludeList || !*pchExcludeList;
	char *pchTempList = NULL;
	char *pchContext = NULL;
	char *pchToken = NULL;
	int i;
	COSTUME_UI_TRACE_FUNC();

	// No scales?
	if (!pSkel || !eaSize(&pSkel->eaBodyScaleInfo))
	{
		return;
	}

	// Clear the existing lists
	eaClear(&g_CostumeEditState.eaBodyScalesInclude);
	eaClear(&g_CostumeEditState.eaBodyScalesExclude);

	// Build the include list
	strdup_alloca(pchTempList, pchIncludeList);
	if ((pchToken = strtok_r(pchTempList, " ,\r\n\t", &pchContext)))
	{
		do
		{
			for (i = eaSize(&pSkel->eaBodyScaleInfo) - 1; i >= 0; i--)
			{
				if (!stricmp(pSkel->eaBodyScaleInfo[i]->pcName, pchToken))
				{
					eaPushUnique(&g_CostumeEditState.eaBodyScalesInclude, pSkel->eaBodyScaleInfo[i]);
					break;
				}
			}
		}
		while ((pchToken = strtok_r(NULL, " ,\r\n\t", &pchContext)));
	}

	// Build the exclude list
	strdup_alloca(pchTempList, pchExcludeList);
	if ((pchToken = strtok_r(pchTempList, " ,\r\n\t", &pchContext)))
	{
		do
		{
			for (i = eaSize(&pSkel->eaBodyScaleInfo) - 1; i >= 0; i--)
			{
				if (!stricmp(pSkel->eaBodyScaleInfo[i]->pcName, pchToken))
				{
					eaPushUnique(&g_CostumeEditState.eaBodyScalesExclude, pSkel->eaBodyScaleInfo[i]);
					break;
				}
			}
		}
		while ((pchToken = strtok_r(NULL, " ,\r\n\t", &pchContext)));
	}

	// Update the lists
	CostumeUI_RegenCostume(true);
}

// Get the size of the list of body scales available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBodyScaleListSize");
int CostumeCreator_GetBodyScaleListSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaBodyScales);
}

// Get the current body scale info being edited
// The "hackyEnum" is 0=current costume value, 1=skeleton player min, 2=skeleton player max
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditBodyScale");
SA_RET_NN_VALID F32 CostumeCreator_GetEditBodyScale(int hackyEnum, int index)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	F32 fMin, fMax;
	COSTUME_UI_TRACE_FUNC();
	switch(hackyEnum)
	{
	case 0: // Current costume
		if (g_CostumeEditState.pCostume && (0 <= index) && (eafSize(&g_CostumeEditState.pCostume->eafBodyScales) > index)) {
			return g_CostumeEditState.pCostume->eafBodyScales[index];
		}
		else if(pSkel && (0 <= index) && (eaSize(&pSkel->eaBodyScaleInfo) > index) && costumeTailor_GetOverrideBodyScale(pSkel, pSkel->eaBodyScaleInfo[index]->pcName, pSpecies, g_CostumeEditState.pSlotType, &fMin, &fMax))
		{
			// get the mid range of the override
			return (fMin + fMax) / 2.0f;
		}
		else if (pSkel && pSkel->eafDefaultBodyScales && (eafSize(&pSkel->eafDefaultBodyScales) > index))
		{
			return pSkel->eafDefaultBodyScales[index];
		} 
		else if (pSkel && pSkel->eafPlayerMinBodyScales && (eafSize(&pSkel->eafPlayerMinBodyScales) > index))
		{
			return pSkel->eafPlayerMinBodyScales[index];
		}
		else
		{
			return 0;
		}
		break;
	case 1: // Player min
		if (pSkel && pSkel->eafPlayerMinBodyScales && (0 <= index) && (eafSize(&pSkel->eafPlayerMinBodyScales) > index)) {
			if (pSpecies && eaSize(&pSkel->eaBodyScaleInfo) > index)
			{
				if(costumeTailor_GetOverrideBodyScale(pSkel, pSkel->eaBodyScaleInfo[index]->pcName, pSpecies, g_CostumeEditState.pSlotType, &fMin, &fMax))
				{
					return fMin;
				}
			}
			return pSkel->eafPlayerMinBodyScales[index];
		} else {
			return 0;
		}
		break;
	case 2: // Player max
		if (pSkel && pSkel->eafPlayerMaxBodyScales && (0 <= index) && (eafSize(&pSkel->eafPlayerMaxBodyScales) > index)) {
			if (pSpecies && eaSize(&pSkel->eaBodyScaleInfo) > index)
			{
				if(costumeTailor_GetOverrideBodyScale(pSkel, pSkel->eaBodyScaleInfo[index]->pcName, pSpecies, g_CostumeEditState.pSlotType, &fMin, &fMax))
				{
					return fMax;
				}
			}
			return pSkel->eafPlayerMaxBodyScales[index];
		} else {
			return 0;
		}
		break;
	default:
		// Unsupported value
		return 0;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_GetEditBodyScaleByName);
F32 CostumeCreator_GetEditBodyScaleByName(int hackyEnum, const char *pchName)
{
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	int index;
	COSTUME_UI_TRACE_FUNC();

	if (!pSkel)
	{
		return 0;
	}

	for (index = 0; index < eaSize(&pSkel->eaBodyScaleInfo); index++)
	{
		if (!stricmp(pSkel->eaBodyScaleInfo[index]->pcName, pchName))
		{
			break;
		}
	}

	return CostumeCreator_GetEditBodyScale(hackyEnum, index);
}

// Set the bone scale group of the character being created.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetBoneScaleGroup");
void CostumeCreator_SetBoneScaleGroup(const char *pcGroupName)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.pcBoneScaleGroup) {
		free(g_CostumeEditState.pcBoneScaleGroup);
	}
	if (!pcGroupName || !pcGroupName[0]) {
		g_CostumeEditState.pcBoneScaleGroup = NULL;
	} else {
		g_CostumeEditState.pcBoneScaleGroup = strdup(pcGroupName);
	}

	CostumeUI_RegenCostume(true);
}

// Get the list of bone scales available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBoneScaleListByName");
void CostumeCreator_GetBoneScaleListByName(SA_PARAM_NN_VALID UIGen *pGen, const char *pcGroupName)
{
	PCScaleInfo ***peaScaleInfo = ui_GenGetManagedListSafe(pGen, PCScaleInfo);
	S32 i, j;
	PCSkeletonDef *pSkel = SAFE_GET_REF(g_CostumeEditState.pCostume, hSkeleton);
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	COSTUME_UI_TRACE_FUNC();

	eaClear(peaScaleInfo);
	if (pcGroupName && pcGroupName[0] && pSkel) {
		for(i=eaSize(&pSkel->eaScaleInfoGroups)-1; i>=0; --i) {
			if (stricmp(pcGroupName, pSkel->eaScaleInfoGroups[i]->pcName) == 0) {
				PCScaleInfoGroup *pGroup = pSkel->eaScaleInfoGroups[i];
				for(j=0; j<eaSize(&pGroup->eaScaleInfo); ++j) {
					PCScaleInfo *pInfo = pGroup->eaScaleInfo[j];
					if (pInfo->eRestriction & (kPCRestriction_Player|kPCRestriction_Player_Initial))
					{
						F32 fMin, fMax;
						if(costumeTailor_GetOverrideBoneScale(pSkel, pInfo, pSkel->eaScaleInfoGroups[i]->pcName, pSpecies, g_CostumeEditState.pSlotType, &fMin, &fMax))
						{
							pInfo->fPlayerMin = fMin;
							pInfo->fPlayerMax = fMax;
						}
						
						eaPush(peaScaleInfo, pInfo);
					}
				}
			}
		}
	}
	ui_GenSetManagedListSafe(pGen, peaScaleInfo, PCScaleInfo, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBoneScaleList");
void CostumeCreator_GetBoneScaleList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaBoneScales, parse_PCScaleInfo);
}

// Get the size of the list of bone scales available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBoneScaleListSize");
int CostumeCreator_GetBoneScaleListSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaBoneScales);
}

// Get the current bone scale info being edited
// The "hackyEnum" is 0=current costume value, 1=skeleton player min, 2=skeleton player max
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditBoneScale");
SA_RET_NN_VALID F32 CostumeCreator_GetEditBoneScale(int hackyEnum, int index)
{
	int i;
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	const char *pcScaleName;
	F32 fMin, fMax;
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	COSTUME_UI_TRACE_FUNC();

	if (index >= eaSize(&g_CostumeEditState.eaBoneScales)) {
		return 0;
	}
	assert(g_CostumeEditState.eaBoneScales);
	pcScaleName = g_CostumeEditState.eaBoneScales[index]->pcName;

	switch(hackyEnum)
	{
	case 0: // Current costume
		for(i=eaSize(&g_CostumeEditState.pCostume->eaScaleValues)-1; i>=0; --i) {
			if (stricmp(pcScaleName, g_CostumeEditState.pCostume->eaScaleValues[i]->pcScaleName) == 0) {
				return g_CostumeEditState.pCostume->eaScaleValues[i]->fValue;
			}
		}
		return 0;
	case 1: // Player min
		for(i=eaSize(&g_CostumeEditState.eaBoneScales)-1; i>=0; --i) {
			if (stricmp(pcScaleName, g_CostumeEditState.eaBoneScales[i]->pcName) == 0) {
				if(costumeTailor_GetOverrideBoneScale(pSkel, g_CostumeEditState.eaBoneScales[i], pcScaleName, pSpecies, g_CostumeEditState.pSlotType, &fMin, &fMax))
				{
					return fMin;				
				}
				return g_CostumeEditState.eaBoneScales[i]->fPlayerMin;
			}
		}
		return 0;
	case 2: // Player max
		for(i=eaSize(&g_CostumeEditState.eaBoneScales)-1; i>=0; --i) {
			if (stricmp(pcScaleName, g_CostumeEditState.eaBoneScales[i]->pcName) == 0) {
				if(costumeTailor_GetOverrideBoneScale(pSkel, g_CostumeEditState.eaBoneScales[i], pcScaleName, pSpecies, g_CostumeEditState.pSlotType, &fMin, &fMax))
				{
					return fMax;
				}
				return g_CostumeEditState.eaBoneScales[i]->fPlayerMax;
			}
		}
		return 0;
	default:
		// Unsupported value
		return 0;
	}
}

// Get the current bone scale info being edited
// The "hackyEnum" is 0=current costume value, 1=skeleton player min, 2=skeleton player max
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditBoneScaleByName");
SA_RET_NN_VALID F32 CostumeCreator_GetEditBoneScaleByData(int hackyEnum, SA_PARAM_OP_VALID PCScaleInfo *pScaleInfo)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	int i;
	F32 fMin, fMax;
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	COSTUME_UI_TRACE_FUNC();

	if (pScaleInfo)
	{
		switch(hackyEnum)
		{
		case 0: // Current costume
			for(i=eaSize(&g_CostumeEditState.pCostume->eaScaleValues)-1; i>=0; --i) {
				if (stricmp(pScaleInfo->pcName, g_CostumeEditState.pCostume->eaScaleValues[i]->pcScaleName) == 0) {
					return g_CostumeEditState.pCostume->eaScaleValues[i]->fValue;
				}
			}
			return 0;
		case 1: // Player min
			if(costumeTailor_GetOverrideBoneScale(pSkel, pScaleInfo, pScaleInfo->pcName, pSpecies, g_CostumeEditState.pSlotType, &fMin, &fMax))
			{
				return fMin;
			}
			return pScaleInfo->fPlayerMin;
		case 2: // Player max
			if(costumeTailor_GetOverrideBoneScale(pSkel, pScaleInfo, pScaleInfo->pcName, pSpecies, g_CostumeEditState.pSlotType, &fMin, &fMax))
			{
				return fMax;
			}
			return pScaleInfo->fPlayerMax;
		default:
			// Unsupported value, fall through
			;
		}
	}
	return 0;
}

// Get the list of regions available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetRegionList");
void CostumeCreator_GetRegionList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaRegions, parse_PCRegion);
}

// Get the current region being edited
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditRegion");
SA_RET_NN_VALID PCRegion *CostumeCreator_GetEditRegion(void)
{
	PCRegion *pRegion = GET_REF(g_CostumeEditState.hRegion);
	COSTUME_UI_TRACE_FUNC();
	if (!pRegion && g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	if (!pRegion && eaSize(&g_CostumeEditState.eaRegions)) {
		CostumeCreator_SetRegion(g_CostumeEditState.eaRegions[0]->pcName);
		pRegion = GET_REF(g_CostumeEditState.hRegion);
	}
	assert(pRegion && pRegion->pcName);
	return pRegion;
}

// Get the name of the current region being edited
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditRegionName");
const char *CostumeCreator_GetEditRegionName(void)
{
	PCRegion *pRegion = CostumeCreator_GetEditRegion();
	COSTUME_UI_TRACE_FUNC();
	return TranslateDisplayMessage(pRegion->displayNameMsg);
}

// Get the list of categories available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetCategoryList");
void CostumeCreator_GetCategoryList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaCategories, parse_PCCategory);
}

// Get the current category being edited
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditCategory");
SA_RET_NN_VALID PCCategory *CostumeCreator_GetEditCategory(void)
{
	PCCategory* category = GET_REF(g_CostumeEditState.hCategory);
	COSTUME_UI_TRACE_FUNC();
	assert(category);
	return category;
}

// Update the list of PowerFXBones in the costume edit state based on Character creation data
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_UpdatePowerFXBonesFromCharacterCreation");
void CostumeCreator_UpdatePowerFXBonesFromCharacterCreation(void)
{
	// TODO: this should no longer be required
	int i,j;
	eaClear(&g_CostumeEditState.eaPowerFXBones);
	COSTUME_UI_TRACE_FUNC();
	for(i=eaSize(&g_CharacterCreationData->powerNodes)-1; i>=0; i--)
	{
		PTNodeDef *pNodeDef = powertreenodedef_Find(g_CharacterCreationData->powerNodes[i]);
		if(pNodeDef && eaSize(&pNodeDef->ppRanks))
		{
			PTNodeRankDef *pRankDef = pNodeDef->ppRanks[0];
			if(pRankDef)
			{
				PowerDef *pdef = GET_REF(pRankDef->hPowerDef);
				PowerAnimFX *pafx = SAFE_GET_REF(pdef, hFX);
				if(pafx && pafx->cpchPCBoneName)
				{
					eaPushUnique(&g_CostumeEditState.eaPowerFXBones,pafx->cpchPCBoneName);
				}
				else if (pdef && pdef->eType == kPowerType_Combo)
				{
					// Is it possible to have recursive combo powers?
					for (j=eaSize(&pdef->ppCombos)-1; j>=0; j--)
					{
						PowerDef *pComboDef = GET_REF(pdef->ppCombos[j]->hPower);
						pafx = SAFE_GET_REF(pComboDef, hFX);
						if (pafx && pafx->cpchPCBoneName)
						{
							eaPushUnique(&g_CostumeEditState.eaPowerFXBones,pafx->cpchPCBoneName);
						}
					}
				}
			}
		}
	}
}

// Get the current bone being edited
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetEditBone");
SA_RET_NN_VALID PCBoneDef *CostumeCreator_GetEditBone(void)
{
	PCBoneDef* bone = GET_REF(g_CostumeEditState.hBone);
	COSTUME_UI_TRACE_FUNC();
	assert(bone);
	return bone;
}

// Get the list of bones available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_BoneListAddExtra");
void CostumeCreator_BoneListAddExtra(int addExtra)
{
	COSTUME_UI_TRACE_FUNC();
	s_bAddNoneToBoneList = addExtra ? true : false;
}

// Get the list of bones available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBoneList");
void CostumeCreator_GetBoneList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaBones, parse_PCBoneDef);
}

// Get the name of a bone for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBoneName");
const char *CostumeCreator_GetBoneName(SA_PARAM_OP_VALID PCBoneDef *pBone)
{
	COSTUME_UI_TRACE_FUNC();
	if (!pBone) {
		return "Unnamed";
	}
	if (g_MirrorSelectMode) {
		PCBoneDef *pMirrorBone = GET_REF(pBone->hMirrorBone);
		if (pMirrorBone && (eaFind(&g_CostumeEditState.eaAllBones, pMirrorBone) >= 0)) {
			return TranslateDisplayMessage(pBone->mergeNameMsg);
		}
	}
	if (g_GroupSelectMode)
	{
		NOCONST(PCPart) *pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL);
		if (pRealPart && pRealPart->iBoneGroupIndex >= 0) {
			NOCONST(PCPart) *pGroupPart = NULL;
			PCSkeletonDef *skel = GET_REF(g_CostumeEditState.hSkeleton);

			if (skel)
			{
				PCBoneGroup **bg = skel->eaBoneGroups;
				if (bg)
				{
					return TranslateDisplayMessage(bg[pRealPart->iBoneGroupIndex]->displayNameMsg);
				}
			}
		}
	}

	return TranslateDisplayMessage(pBone->displayNameMsg);
}

// Get the name of a bone for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPartBoneName");
const char *CostumeCreator_GetPartBoneName(SA_PARAM_OP_VALID PCPart *pPart)
{
	COSTUME_UI_TRACE_FUNC();
	if (!pPart) {
		return "Unnamed";
	}
	return CostumeCreator_GetBoneName(GET_REF(pPart->hBoneDef));
}

// Get the name of a bone for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetRefBoneName");
const char *CostumeCreator_GetRefBoneName(const char *pcBone)
{
	COSTUME_UI_TRACE_FUNC();
	if (!pcBone || !*pcBone) {
		return "Unnamed";
	}
	return CostumeCreator_GetBoneName(CostumeUI_FindBone(pcBone, GET_REF(g_CostumeEditState.hSkeleton)));
}

// Get the list of layers available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetLayerList");
void CostumeCreator_GetLayerList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaLayers, parse_PCLayer);
}

// Get the size of the list of layers available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetLayerListSize");
int CostumeCreator_GetLayerListSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaLayers);
}

// Get the current layer
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetCurrentLayer");
SA_RET_OP_VALID PCLayer *CostumeCreator_GetCurrentLayer(void)
{
	COSTUME_UI_TRACE_FUNC();
	return g_CostumeEditState.pCurrentLayer;
}

// Get the list of geos available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetGeoList");
void CostumeCreator_GetGeoList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaGeos, parse_PCGeometryDef);
}

// Get the size of the list of geos available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetGeoListSize");
int CostumeCreator_GetGeoListSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaGeos);
}

// Get the list of geos available for the specified bone
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBoneGeoList");
void CostumeCreator_GetBoneGeoList(SA_PARAM_NN_VALID UIGen *pGen, const char *pchBone)
{
	PCBoneDef *pBone = CostumeUI_FindBone(pchBone, GET_REF(g_CostumeEditState.hSkeleton));
	CostumeBoneValidValues *pValidValues = pBone ? eaIndexedGetUsingString(&g_CostumeEditState.eaBoneValidValues, pBone->pcName) : NULL;

	COSTUME_UI_TRACE_FUNC();

	if (!pValidValues && pBone) {
		pValidValues = StructCreate(parse_CostumeBoneValidValues);
		SET_HANDLE_FROM_REFERENT(g_hCostumeBoneDict, pBone, pValidValues->hBone);
		eaIndexedEnable(&g_CostumeEditState.eaBoneValidValues, parse_CostumeBoneValidValues);
		eaIndexedAdd(&g_CostumeEditState.eaBoneValidValues, pValidValues);
		g_CostumeEditState.bUpdateLists = true;
	}

	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
		pValidValues = eaIndexedGetUsingString(&g_CostumeEditState.eaBoneValidValues, pBone->pcName);
	}

	ui_GenSetList(pGen, pValidValues ? &pValidValues->eaGeos : NULL, parse_PCGeometryDef);
}

// Get the size of the list of geos available for the specified bone
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBoneGeoListSize");
int CostumeCreator_GetBoneGeoListSize(const char *pchBone)
{
	PCBoneDef *pBone = CostumeUI_FindBone(pchBone, GET_REF(g_CostumeEditState.hSkeleton));
	CostumeBoneValidValues *pValidValues = pBone ? eaIndexedGetUsingString(&g_CostumeEditState.eaBoneValidValues, pBone->pcName) : NULL;

	COSTUME_UI_TRACE_FUNC();

	return pValidValues ? eaSize(&pValidValues->eaGeos) : 0;
}

// Get the list of geos available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetGeoDisplayName");
const char *CostumeCreator_GetGeoDisplayName(const char *pchGeo)
{
	static char *s_pch;
	PCGeometryDef *pGeo = RefSystem_ReferentFromString(g_hCostumeGeometryDict, pchGeo);
	COSTUME_UI_TRACE_FUNC();
	if (pGeo) {
		if (pGeo->eRestriction & kPCRestriction_Player_Initial || stricmp("None", pchGeo) == 0) {
			return TranslateDisplayMessage(pGeo->displayNameMsg);
		} else {
			estrPrintf(&s_pch, "{%s}", TranslateDisplayMessage(pGeo->displayNameMsg));
			return s_pch;
		}
	} else {
		return "";
	}
}

const char *CostumeCreator_GetGeoSysNameInternal(PCBoneDef *pBone);

// Get the name of a geometry for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetGeoSysName");
const char *CostumeCreator_GetGeoSysName(void)
{
	COSTUME_UI_TRACE_FUNC();
	return CostumeCreator_GetGeoSysNameInternal(GET_REF(g_CostumeEditState.hBone));
}

// Get the name of a specific bone's geometry for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBoneGeoSysName");
const char *CostumeCreator_GetBoneGeoSysName(const char *pcBone)
{
	PCBoneDef *pBone;
	COSTUME_UI_TRACE_FUNC();	
	pBone = pcBone && *pcBone ? CostumeUI_FindBone(pcBone, GET_REF(g_CostumeEditState.hSkeleton)) : NULL;
	return CostumeCreator_GetGeoSysNameInternal(pBone);
}

const char *CostumeCreator_GetGeoSysNameInternal(PCBoneDef *pBone)
{
	NOCONST(PCPart) *pRealPart;
	PCGeometryDef *pGeo; 

	if (!pBone || !g_CostumeEditState.pCostume) return "";
	if (!stricmp(pBone->pcName,"None")) return "";
	pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL);
	if (g_MirrorSelectMode && pRealPart && (pRealPart->eEditMode == kPCEditMode_Right)) {
		pRealPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, pRealPart);
	}
	if (!pRealPart) {
		return "";
	}

	pGeo = GET_REF(pRealPart->hGeoDef);
	assert(pGeo);

	return pGeo->pcName;
}

const char *CostumeCreator_GetGeoNameInternal(PCBoneDef *pBone);

// Get the name of a geometry for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetGeoName");
const char *CostumeCreator_GetGeoName(void)
{
	COSTUME_UI_TRACE_FUNC();
	return CostumeCreator_GetGeoNameInternal(GET_REF(g_CostumeEditState.hBone));
}

// Get the name of a geometry for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBoneGeoName");
const char *CostumeCreator_GetBoneGeoName(const char *pcBone)
{
	PCBoneDef *pBone = pcBone && *pcBone ? CostumeUI_FindBone(pcBone, GET_REF(g_CostumeEditState.hSkeleton)) : NULL;
	COSTUME_UI_TRACE_FUNC();
	return CostumeCreator_GetGeoNameInternal(pBone);
}

const char *CostumeCreator_GetGeoNameInternal(PCBoneDef *pBone)
{
	NOCONST(PCPart) *pRealPart;
	NOCONST(PCPart) *pPart;
	PCGeometryDef *pGeo;

	if(!g_CostumeEditState.pCostume)
	{
		Errorf("Error CostumeCreator_GetGeoNameInternal, no costume in edit state.");
		return "";
	}
	if (!pBone) return "";
	if (!stricmp(pBone->pcName,"None")) return "";
	pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL);
	assert(pRealPart);
	if (g_MirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Right)) {
		pPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, pRealPart);
		pGeo = GET_REF(pPart->hGeoDef);
	} else {
		pGeo = GET_REF(pRealPart->hGeoDef);
	}
	assert(pGeo);

	if (pBone->bPowerFX) {
		TailorWeaponStance *pStance = RefSystem_ReferentFromString("TailorWeaponStance", pBone->pcName);
		if (pStance && pStance->pDefaultName && !stricmp(pGeo->pcName, "None")) {
			return TranslateDisplayMessage(*pStance->pDefaultName);
		}
	}

	if (g_MirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Both) && eaFind(&g_CostumeEditState.eaAllBones, GET_REF(pBone->hMirrorBone)) >= 0) {
		NOCONST(PCPart) *pMirrorPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, pRealPart);
		if (pMirrorPart) {
			PCGeometryDef *pMirrorGeo = GET_REF(pMirrorPart->hGeoDef);
			if ((pMirrorGeo && ((pGeo == pMirrorGeo) || (stricmp(TranslateDisplayMessage(pGeo->displayNameMsg), TranslateDisplayMessage(pMirrorGeo->displayNameMsg)) == 0))) ||
				(!pMirrorGeo && (stricmp(pGeo->pcName,"None") == 0))){
				return TranslateDisplayMessage(pGeo->displayNameMsg);
			} else {
				return TranslateMessageKey("CostumeCreator.DifferentValues");
			}
		}
	}

	return TranslateDisplayMessage(pGeo->displayNameMsg);
}

// Get the list of materials available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMaterialList");
void CostumeCreator_GetMaterialList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaMats, parse_PCMaterialDef);
}

// Get the list of materials available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMaterialListFromBoneGroup");
void CostumeCreator_GetMaterialListFromBoneGroup(SA_PARAM_NN_VALID UIGen *pGen, const char *pchGroup)
{
	COSTUME_UI_TRACE_FUNC();
	if (!pchGroup) return;
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}

	{
		PCBoneGroup *pBoneGroup = NULL;
		PCGeometryDef *pGeo = NULL;
		PCBoneDef *pBone = NULL;
		NOCONST(PCPart) *pPart = NULL;
		int j;
		PCSkeletonDef *skel = GET_REF(g_CostumeEditState.hSkeleton);

		if (!skel) goto skip;
		for(j=eaSize(&skel->eaBoneGroups)-1; j>=0; --j)
		{
			if (skel->eaBoneGroups[j]->pcName && !stricmp(skel->eaBoneGroups[j]->pcName,pchGroup))
			{
				pBoneGroup = skel->eaBoneGroups[j];
				break;
			}
		}

		if (!pBoneGroup) goto skip;
		if (!eaSize(&pBoneGroup->eaBoneInGroup)) goto skip;
		pBone = GET_REF(pBoneGroup->eaBoneInGroup[0]->hBone);
		if (!pBone) goto skip;
		pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL);
		if (!pPart) goto skip;
		pGeo = GET_REF(pPart->hGeoDef);
		if (!pGeo) goto skip;

		eaClear(&g_CostumeEditState.eaMats);
		costumeTailor_GetValidMaterials(g_CostumeEditState.pCostume, pGeo, GET_REF(g_CostumeEditState.hSpecies), NULL, NULL, g_CostumeEditState.eaUnlockedCostumes, &g_CostumeEditState.eaMats, false, true, g_CostumeEditState.bUnlockAll);
		costumeTailor_PickValidMaterial(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), pPart, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eaMats, g_CostumeEditState.bUnlockAll, false);
	}

skip:
	ui_GenSetList(pGen, &g_CostumeEditState.eaMats, parse_PCMaterialDef);
}

// Get the list of materials available for the current costume part
AUTO_COMMAND ACMD_NAME("CostumeCreator_SetMaterialLinkAll") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_SetMaterialLinkAll(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetMaterialLinkAll");
void CostumeCreator_SetMaterialLinkAll(void)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}

	{
		PCBoneGroup *pBoneGroup = NULL;
		PCGeometryDef *pGeo = NULL;
		PCBoneDef *pBone = NULL;
		NOCONST(PCPart) *pPart = NULL;
		int j;
		PCSkeletonDef *skel = GET_REF(g_CostumeEditState.hSkeleton);

		if (!skel) return;
		for(j=eaSize(&skel->eaBoneGroups)-1; j>=0; --j)
		{
			if ((skel->eaBoneGroups[j]->eBoneGroupFlags & kPCBoneGroupFlags_LinkMaterials))
			{
				pBoneGroup = skel->eaBoneGroups[j];
				break;
			}
		}

		if (pBoneGroup)
		{
			for(j=eaSize(&pBoneGroup->eaBoneInGroup)-1; j>=0; --j)
			{
				pPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(pBoneGroup->eaBoneInGroup[j]->hBone), NULL);
				if (!pPart) continue;
				costumeTailor_SetPartMaterialLinking(g_CostumeEditState.pCostume, pPart, kPCColorLink_All, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.bUnlockAll);
			}
		}
	}

	CostumeUI_RegenCostume(true);
}

// Get the size of the list of materials available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMaterialListSize");
int CostumeCreator_GetMaterialListSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaMats);
}

const char *CostumeCreator_GetMatSysNameInternal(SA_PARAM_OP_VALID PCBoneDef *pBone);

// Get the name of a material for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMatSysName");
const char *CostumeCreator_GetMatSysName(void)
{
	COSTUME_UI_TRACE_FUNC();
	return CostumeCreator_GetMatSysNameInternal(GET_REF(g_CostumeEditState.hBone));
}

// Get the name of a material for a specific bone
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBoneMatSysName");
const char *CostumeCreator_GetBoneMatSysName(const char *pcBoneName)
{
	PCBoneDef *pBone = pcBoneName && *pcBoneName ? CostumeUI_FindBone(pcBoneName, GET_REF(g_CostumeEditState.hSkeleton)) : NULL;
	COSTUME_UI_TRACE_FUNC();
	return CostumeCreator_GetMatSysNameInternal(pBone);
}

const char *CostumeCreator_GetMatSysNameInternal(SA_PARAM_OP_VALID PCBoneDef *pBone)
{
	NOCONST(PCPart) *pRealPart;
	PCMaterialDef *pMat;

	if (!pBone || !stricmp(pBone->pcName,"None"))
	{
		PCSkeletonDef *skel = GET_REF(g_CostumeEditState.hSkeleton);
		int j;

		if (skel)
		{
			PCBoneGroup **bg = skel->eaBoneGroups;
			if (bg)
			{
				for (j = eaSize(&bg)-1; j >= 0; --j)
				{
					if (eaSize(&bg[j]->eaBoneInGroup) && bg[j]->pcName && !stricmp(bg[j]->pcName,"CommonMaterialsDef"))
					{
						pBone = GET_REF(bg[j]->eaBoneInGroup[0]->hBone);
						break;
					}
				}
			}
		}
	}

	if (!pBone) return "";
	if (!stricmp(pBone->pcName,"None")) return "";
	pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL);
	if (g_MirrorSelectMode && pRealPart && (pRealPart->eEditMode == kPCEditMode_Right)) {
		pRealPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, pRealPart);
	}
	if (pRealPart && GET_REF(pRealPart->hGeoDef) && GET_REF(pRealPart->hGeoDef)->pClothData && GET_REF(pRealPart->hGeoDef)->pClothData->bIsCloth && GET_REF(pRealPart->hGeoDef)->pClothData->bHasClothBack && pRealPart->pClothLayer) {
		if (pRealPart->eEditMode == kPCEditMode_Back && pRealPart->pClothLayer) {
			pRealPart = pRealPart->pClothLayer;
		}
	}
	assert(pRealPart);
	pMat = GET_REF(pRealPart->hMatDef);
	assert(pMat);

	return pMat->pcName;
}

const char *CostumeCreator_GetMatNameInternal(SA_PARAM_OP_VALID PCBoneDef *pBone);

// Get the name of a material for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMatName");
const char *CostumeCreator_GetMatName(void)
{
	COSTUME_UI_TRACE_FUNC();
	return CostumeCreator_GetMatNameInternal(GET_REF(g_CostumeEditState.hBone));
}

// Get the name of a material for a specific bone
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBoneMatName");
const char *CostumeCreator_GetBoneMatName(const char *pcBoneName)
{
	PCBoneDef *pBone = pcBoneName && *pcBoneName ? CostumeUI_FindBone(pcBoneName, GET_REF(g_CostumeEditState.hSkeleton)) : NULL;
	COSTUME_UI_TRACE_FUNC();
	return CostumeCreator_GetMatNameInternal(pBone);
}

const char *CostumeCreator_GetMatNameInternal(SA_PARAM_OP_VALID PCBoneDef *pBone)
{
	NOCONST(PCPart) *pRealPart;
	PCMaterialDef *pMat;
	PCGeometryDef *pGeo;
	bool bHaveMirrorBone;

	if (!pBone || !stricmp(pBone->pcName,"None"))
	{
		PCSkeletonDef *skel = GET_REF(g_CostumeEditState.hSkeleton);
		int j;

		if (skel)
		{
			PCBoneGroup **bg = skel->eaBoneGroups;
			if (bg)
			{
				for (j = eaSize(&bg)-1; j >= 0; --j)
				{
					if (eaSize(&bg[j]->eaBoneInGroup) && bg[j]->pcName && !stricmp(bg[j]->pcName,"CommonMaterialsDef"))
					{
						pBone = GET_REF(bg[j]->eaBoneInGroup[0]->hBone);
						break;
					}
				}
			}
		}
	}

	if (!pBone) return "";
	if (!stricmp(pBone->pcName,"None")) return "";
	pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL);
	if (g_MirrorSelectMode && pRealPart && (pRealPart->eEditMode == kPCEditMode_Right)) {
		pRealPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, pRealPart);
	}
	assert(pRealPart);
	pGeo = GET_REF(pRealPart->hGeoDef);
	assert(pGeo);

	bHaveMirrorBone = eaFind(&g_CostumeEditState.eaAllBones, GET_REF(pBone->hMirrorBone)) >= 0;

	if (pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer && (pRealPart->eEditMode == kPCEditMode_Both) && bHaveMirrorBone) {
		if (GET_REF(pRealPart->hMatDef) != GET_REF(pRealPart->pClothLayer->hMatDef)) {
			return TranslateMessageKey("CostumeCreator.DifferentValues");
		}
	}
	if (pRealPart && pGeo && pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer) {
		if (pRealPart->eEditMode == kPCEditMode_Back && pRealPart->pClothLayer) {
			pRealPart = pRealPart->pClothLayer;
		}
	}
	pMat = GET_REF(pRealPart->hMatDef);
	assert(pMat);

	if (g_MirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Both) && bHaveMirrorBone) {
		NOCONST(PCPart) *pMirrorPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, pRealPart);
		if (pMirrorPart) {
			PCMaterialDef *pMirrorMat = GET_REF(pMirrorPart->hMatDef);
			if ((pMirrorMat && ((pMat == pMirrorMat) || (stricmp(TranslateDisplayMessage(pMat->displayNameMsg), TranslateDisplayMessage(pMirrorMat->displayNameMsg)) == 0))) ||
				(!pMirrorMat && (stricmp(pMat->pcName,"None") == 0))){
				return TranslateDisplayMessage(pMat->displayNameMsg);
			} else {
				return TranslateMessageKey("CostumeCreator.DifferentValues");
			}
		}
	}

	return TranslateDisplayMessage(pMat->displayNameMsg);
}

// Get the list of patterns available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPatternList");
void CostumeCreator_GetPatternList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaPatternTex, parse_PCTextureDef);
}


// Get the size of the list of patterns  available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPatternListSize");
int CostumeCreator_GetPatternListSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaPatternTex);
}

// Get the list of details available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetDetailList");
void CostumeCreator_GetDetailList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaDetailTex, parse_PCTextureDef);
}


// Get the size of the list of patterns  available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetDetailListSize");
int CostumeCreator_GetDetailListSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaDetailTex);
}

// Get the list of specular maps available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetSpecularList");
void CostumeCreator_GetSpecularList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaSpecularTex, parse_PCTextureDef);
}


// Get the size of the list of specular maps available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetSpecularListSize");
int CostumeCreator_GetSpecularListSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaSpecularTex);
}

// Get the list of diffuse maps available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetDiffuseList");
void CostumeCreator_GetDiffuseList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaDiffuseTex, parse_PCTextureDef);
}


// Get the size of the list of diffuse maps available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetDiffuseListSize");
int CostumeCreator_GetDiffuseListSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaDiffuseTex);
}

// Get the list of movable maps available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMovableList");
void CostumeCreator_GetMovableList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaMovableTex, parse_PCTextureDef);
}


// Get the size of the list of diffuse maps available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMovableListSize");
int CostumeCreator_GetMovableListSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaMovableTex);
}

const char *CostumeCreator_GetTexSysNameInternal(PCBoneDef *pBone, int id);

// Get the name of a texture for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetTexSysName");
const char *CostumeCreator_GetTexSysName(int id)
{
	COSTUME_UI_TRACE_FUNC();
	return CostumeCreator_GetTexSysNameInternal(GET_REF(g_CostumeEditState.hBone), id);
}

// Get the name of a texture for a specific bone in the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBoneTexSysName");
const char *CostumeCreator_GetBoneTexSysName(const char *pcBone, int id)
{
	COSTUME_UI_TRACE_FUNC();
	if (!pcBone || !*pcBone) return "";
	return CostumeCreator_GetTexSysNameInternal(CostumeUI_FindBone(pcBone, GET_REF(g_CostumeEditState.hSkeleton)), id);
}

const char *CostumeCreator_GetTexSysNameInternal(PCBoneDef *pBone, int id)
{
	NOCONST(PCPart) *pRealPart;
	PCTextureDef *pTex = NULL;

	if (!pBone) return "";
	if (!stricmp(pBone->pcName,"None")) return "";
	pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL);
	if (g_MirrorSelectMode && pRealPart && (pRealPart->eEditMode == kPCEditMode_Right)) {
		pRealPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, pRealPart);
	}
	if (pRealPart && GET_REF(pRealPart->hGeoDef) && GET_REF(pRealPart->hGeoDef)->pClothData && GET_REF(pRealPart->hGeoDef)->pClothData->bIsCloth && GET_REF(pRealPart->hGeoDef)->pClothData->bHasClothBack && pRealPart->pClothLayer) {
		if (pRealPart->eEditMode == kPCEditMode_Back && pRealPart->pClothLayer) {
			pRealPart = pRealPart->pClothLayer;
		}
	}
	assert(pRealPart);

	switch(id) {
		xcase 1: pTex = GET_REF(pRealPart->hPatternTexture);
		xcase 2: pTex = GET_REF(pRealPart->hDetailTexture);
		xcase 3: pTex = GET_REF(pRealPart->hSpecularTexture);
		xcase 4: pTex = GET_REF(pRealPart->hDiffuseTexture);
		xcase 5: pTex = GET_REF(pRealPart->pMovableTexture->hMovableTexture);
	}

	if (!pTex)
		return "";
	return pTex->pcName;
}

const char *CostumeCreator_GetTexNameInternal(PCBoneDef *pBone, int id);

// Get the name of a texture for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetTexName");
const char *CostumeCreator_GetTexName(int id)
{
	COSTUME_UI_TRACE_FUNC();
	return CostumeCreator_GetTexNameInternal(GET_REF(g_CostumeEditState.hBone), id);
}

// Get the name of a texture for a specific bone in the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBoneTexName");
const char *CostumeCreator_GetBoneTexName(const char *pcBoneName, int id)
{
	COSTUME_UI_TRACE_FUNC();
	if (!pcBoneName || !*pcBoneName) return "";
	return CostumeCreator_GetTexNameInternal(CostumeUI_FindBone(pcBoneName, GET_REF(g_CostumeEditState.hSkeleton)), id);
}

const char *CostumeCreator_GetTexNameInternal(PCBoneDef *pBone, int id)
{
	NOCONST(PCPart) *pRealPart;
	NOCONST(PCPart) *pPart;
	PCGeometryDef *pGeo;
	PCTextureDef *pTex = NULL;
	bool bHaveMirrorBone;

	pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL);
	if (!pRealPart)
		return "...";
	pPart = pRealPart;
	if (g_MirrorSelectMode && pRealPart && (pRealPart->eEditMode == kPCEditMode_Right)) {
		pPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, pRealPart);
	}
	pGeo = GET_REF(pRealPart->hGeoDef);
	if (!pGeo)
		return "...";
	if (pRealPart && pGeo && pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer) {
		if (pRealPart->eEditMode == kPCEditMode_Back && pRealPart->pClothLayer) {
			pPart = pRealPart->pClothLayer;
		}
	}

	bHaveMirrorBone = eaFind(&g_CostumeEditState.eaAllBones, GET_REF(pBone->hMirrorBone)) >= 0;

	if (pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer && (pRealPart->eEditMode == kPCEditMode_Both) && bHaveMirrorBone) {
		switch(id) {
			xcase 1: 
				if (GET_REF(pRealPart->hPatternTexture) != GET_REF(pRealPart->pClothLayer->hPatternTexture)) {
					return TranslateMessageKey("CostumeCreator.DifferentValues");
				}
			xcase 2: 
				if (GET_REF(pRealPart->hDetailTexture) != GET_REF(pRealPart->pClothLayer->hDetailTexture)) {
					return TranslateMessageKey("CostumeCreator.DifferentValues");
				}
			xcase 3: 
				if (GET_REF(pRealPart->hSpecularTexture) != GET_REF(pRealPart->pClothLayer->hSpecularTexture)) {
					return TranslateMessageKey("CostumeCreator.DifferentValues");
				}
			xcase 4: 
				if (GET_REF(pRealPart->hDiffuseTexture) != GET_REF(pRealPart->pClothLayer->hDiffuseTexture)) {
					return TranslateMessageKey("CostumeCreator.DifferentValues");
				}
			xcase 5: 
				if (GET_REF(pRealPart->pMovableTexture->hMovableTexture) != GET_REF(pRealPart->pClothLayer->pMovableTexture->hMovableTexture)) {
					return TranslateMessageKey("CostumeCreator.DifferentValues");
				}
		}
	}

	switch(id) {
		xcase 1: pTex = GET_REF(pPart->hPatternTexture);
		xcase 2: pTex = GET_REF(pPart->hDetailTexture);
		xcase 3: pTex = GET_REF(pPart->hSpecularTexture);
		xcase 4: pTex = GET_REF(pPart->hDiffuseTexture);
		xcase 5: pTex = GET_REF(pPart->pMovableTexture->hMovableTexture);
	}
	if (!pTex) {
		return NULL;
	}

	if (g_MirrorSelectMode && (pPart->eEditMode == kPCEditMode_Both) && bHaveMirrorBone) {
		NOCONST(PCPart) *pMirrorPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, pPart);
		if (pMirrorPart) {
			PCTextureDef *pMirrorTex = NULL;
			switch(id) {
				xcase 1: pMirrorTex = GET_REF(pMirrorPart->hPatternTexture);
				xcase 2: pMirrorTex = GET_REF(pMirrorPart->hDetailTexture);
				xcase 3: pMirrorTex = GET_REF(pMirrorPart->hSpecularTexture);
				xcase 4: pMirrorTex = GET_REF(pMirrorPart->hDiffuseTexture);
				xcase 5: pMirrorTex = GET_REF(pMirrorPart->pMovableTexture->hMovableTexture);
			}
			if ((pMirrorTex && ((pTex == pMirrorTex) || (stricmp(TranslateDisplayMessage(pTex->displayNameMsg), TranslateDisplayMessage(pMirrorTex->displayNameMsg)) == 0))) ||
				(!pMirrorTex && (stricmp(pTex->pcName,"None") == 0))){
				return TranslateDisplayMessage(pTex->displayNameMsg);
			} else {
				return TranslateMessageKey("CostumeCreator.DifferentValues");
			}
		}
	}

	return TranslateDisplayMessage(pTex->displayNameMsg);
}

// Get the list of styles
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetStyleList");
void CostumeCreator_GetStyleList(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.bUpdateLists && g_CostumeEditState.pCostume) {
		CostumeUI_RegenCostume(true);
	}
	ui_GenSetList(pGen, &g_CostumeEditState.eaStyles, parse_PCStyle);
}

// Get the size of the list of styles
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetStyleListSize");
int CostumeCreator_GetStyleListSize(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaStyles);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetColorModel");
void CostumeCreator_GetColorModel(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, int iColor)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	UIColorSet *pColorSet = NULL;
	COSTUME_UI_TRACE_FUNC();
	if (pSkel && g_CostumeEditState.pPart) {
		pColorSet = CostumeCreator_GetColorSetByNumber(NULL, iColor);
		if (pColorSet) {
			int **peaiColorList = ui_GenGetColorList(pGen);
			int i;
			eaiClear(peaiColorList);
			for (i = 0; i < eaSize(&pColorSet->eaColors); i++)
			{
				UIColor *pColor = pColorSet->eaColors[i];
				U32 uiColor = (((U32)pColor->color[0]) << 24) | (((U32)pColor->color[1]) << 16) | (((U32)pColor->color[2]) << 8) | ((U32)pColor->color[3]);
				eaiPush(peaiColorList, uiColor);
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBoneColorModel");
void CostumeCreator_GetBoneColorModel(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchBone, int iColor)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	PCBoneDef *pBone = CostumeUI_FindBone(pchBone, pSkel);
	UIColorSet *pColorSet = NULL;
	COSTUME_UI_TRACE_FUNC();
	if (pSkel && g_CostumeEditState.pPart) {
		pColorSet = CostumeCreator_GetColorSetByNumber(pBone, iColor);
		if (pColorSet) {
			int **peaiColorList = ui_GenGetColorList(pGen);
			int i;
			eaiClear(peaiColorList);
			for (i = 0; i < eaSize(&pColorSet->eaColors); i++)
			{
				UIColor *pColor = pColorSet->eaColors[i];
				U32 uiColor = (((U32)pColor->color[0]) << 24) | (((U32)pColor->color[1]) << 16) | (((U32)pColor->color[2]) << 8) | ((U32)pColor->color[3]);
				eaiPush(peaiColorList, uiColor);
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetColorModelName");
const char *CostumeCreator_GetColorModelName(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, int iColor)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	UIColorSet *pColorSet = NULL;
	COSTUME_UI_TRACE_FUNC();
	if (pSkel && g_CostumeEditState.pPart) {
		pColorSet = CostumeCreator_GetColorSetByNumber(NULL, iColor);
		if (pColorSet) {
			return pColorSet->pcName;
		}
	}
	return NULL;
}

// Find out if the current part has skin color
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_HasSkinColor");
int CostumeCreator_HasSkinColor(void)
{
	PCSkeletonDef *pSkel = SAFE_GET_REF(g_CostumeEditState.pCostume, hSkeleton);
	COSTUME_UI_TRACE_FUNC();
	if (pSkel && g_CostumeEditState.pPart) {
		NOCONST(PCPart) *pMirrorPart = NULL;
		PCMaterialDef *pMat = GET_REF(g_CostumeEditState.pPart->hMatDef);
		PCMaterialDef *pMirrorMat = NULL;

		// Check mirror part if necessary
		if (g_MirrorSelectMode && (g_CostumeEditState.pPart->eEditMode == kPCEditMode_Both)) {
			pMirrorPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, g_CostumeEditState.pPart);
			if (pMirrorPart) {
				pMirrorMat = GET_REF(pMirrorPart->hMatDef);
			}
		}

		// Check bone group parts if necessary
		if (g_GroupSelectMode && g_CostumeEditState.pPart->iBoneGroupIndex >= 0) {
			NOCONST(PCPart) *pGroupPart = NULL;
			PCSkeletonDef *skel = GET_REF(g_CostumeEditState.hSkeleton);
			int i;

			if (skel)
			{
				PCBoneGroup **bg = skel->eaBoneGroups;
				if (bg)
				{
					for (i = eaSize(&bg[g_CostumeEditState.pPart->iBoneGroupIndex]->eaBoneInGroup)-1; i >= 0; --i)
					{
						pGroupPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(bg[g_CostumeEditState.pPart->iBoneGroupIndex]->eaBoneInGroup[i]->hBone), NULL);
						if (pGroupPart && GET_REF(pGroupPart->hMatDef) && GET_REF(pGroupPart->hMatDef)->bHasSkin) {
							return 1;
						}
					}
				}
			}
		}

		if (pMat && pMat->bHasSkin && (!pMirrorMat || pMirrorMat->bHasSkin)) {
			return 1;
		}
	}
	return 0;
}


// Get the list of colors available for the current costume part
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetColorLinkType");
S32 CostumeCreator_GetColorLinkType(void)
{
	NOCONST(PCPart) *pRealPart;
	PCGeometryDef *pGeo;
	COSTUME_UI_TRACE_FUNC();

	pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	if (!pRealPart)
		return kPCColorLink_Different;
	pGeo = GET_REF(pRealPart->hGeoDef);
	if (!pGeo)
		return kPCColorLink_Different;
	if (pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer && (pRealPart->eEditMode == kPCEditMode_Both)) {
		if (pRealPart->eColorLink != pRealPart->pClothLayer->eColorLink) {
			return kPCColorLink_Different;
		}
	}

	// Check mirror part if necessary
	if (g_MirrorSelectMode && (g_CostumeEditState.pPart->eEditMode == kPCEditMode_Both)) {
		NOCONST(PCPart) *pMirrorPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, g_CostumeEditState.pPart);
		if (pMirrorPart && (pMirrorPart->eColorLink != g_CostumeEditState.pPart->eColorLink)) {
			return kPCColorLink_Different;
		}
	}
	return g_CostumeEditState.pPart->eColorLink;
}

// Get the cost of the current costume change
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetCost");
S32 CostumeCreator_GetCost(void)
{
	COSTUME_UI_TRACE_FUNC();
	return g_CostumeEditState.currentCost;
}

// Check if a geometry is player initial
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GeoIsPlayerInitial");
bool CostumeCreator_GeoIsPlayerInitial(const char *pchGeo)
{
	PCGeometryDef *pGeoDef = RefSystem_ReferentFromString(g_hCostumeGeometryDict, pchGeo);
	COSTUME_UI_TRACE_FUNC();
	if (pGeoDef) {
		return (pGeoDef->eRestriction & kPCRestriction_Player_Initial) != 0;
	} else {
		return false;
	}
}

// Check if a material is player initial
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_MatIsPlayerInitial");
bool CostumeCreator_MatIsPlayerInitial(const char *pchMat)
{
	PCMaterialDef *pMatDef = RefSystem_ReferentFromString(g_hCostumeMaterialDict, pchMat);
	COSTUME_UI_TRACE_FUNC();
	if (pMatDef) {
		return (pMatDef->eRestriction & kPCRestriction_Player_Initial) != 0;
	} else {
		return false;
	}
}

// Check if a texture is player initial
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_TexIsPlayerInitial");
bool CostumeCreator_TexIsPlayerInitial(const char *pchTex)
{
	PCTextureDef *pTexDef = RefSystem_ReferentFromString(g_hCostumeTextureDict, pchTex);
	COSTUME_UI_TRACE_FUNC();
	if (pTexDef) {
		return (pTexDef->eRestriction & kPCRestriction_Player_Initial) != 0;
	} else {
		return false;
	}
}

// Set the name of the edit costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetName");
void CostumeCreator_SetName(const char *pcName)
{
	char *estrWorking = NULL;
	COSTUME_UI_TRACE_FUNC();
	estrStackCreate(&estrWorking);
	estrAppend2(&estrWorking, pcName);
	estrTrimLeadingAndTrailingWhitespace(&estrWorking);
	if (g_CostumeEditState.pCostume->pcName) {
		if (estrWorking && stricmp(g_CostumeEditState.pCostume->pcName, estrWorking) == 0) {
			estrDestroy(&estrWorking);
			return;
		}
	}
	g_CostumeEditState.pCostume->pcName = allocAddString(estrWorking);
	estrDestroy(&estrWorking);
	{
		NOCONST(PlayerCostume) *pCostumeCopy = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
		costumeTailor_StripUnnecessary(pCostumeCopy);
		CostumeUI_ComputeCostumeChangeCost(pCostumeCopy);
		StructDestroyNoConst(parse_PlayerCostume, pCostumeCopy);
	}
}

// Is the name valid?
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_IsNameValid");
bool CostumeCreator_IsNameValid(void)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.pCostume->pcName) 
	{
		int strerr;
		Entity *pEnt = entActivePlayerPtr();
		if (!pEnt)
		{
			pEnt = (Entity *)g_pFakePlayer;
		}
		strerr = StringIsInvalidCharacterName( g_CostumeEditState.pCostume->pcName, entGetAccessLevel(pEnt) );

		if ( strerr > 0 )
		{
			char* pcError = NULL;
			estrCreate( &pcError );
			
			StringCreateNameError( &pcError, strerr );
			notify_NotifySend(NULL, kNotifyType_NameInvalid, pcError, NULL, NULL);
			estrDestroy(&pcError);

			return false;
		}
	}

	return true;
}

// Gets the players's name or Unknown if not possible.
// DEPRECATE: Use EntGetName(Player) instead.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPlayerName");
const char *CostumeCreator_GetPlayerName(void)
{
	Entity *pEnt = entActivePlayerPtr();
	COSTUME_UI_TRACE_FUNC();
	if (pEnt && pEnt->pSaved) {
		return pEnt->pSaved->savedName;
	}
	return "Unknown";
}

// Tells whether or not the tailor is initialized
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_TailorReady");
bool CostumeCreator_TailorReady(void)
{
	static bool s_bErrored = false;
	static U32 s_uLastChangeTime;
	static int s_uLastMissingCos;
	static int s_uLastMissingGeo;
	static int s_uLastMissingMat;
	static int s_uLastMissingTex;
	static const char **s_eaMissingCos;
	static const char **s_eaMissingGeo;
	static const char **s_eaMissingMat;
	static const char **s_eaMissingTex;
	int iThere_CC = 0, iTotal_CC = 0;
	int i, j;
	PlayerCostume *pUnlockedCostume;
	COSTUME_UI_TRACE_FUNC();

	eaClearFast(&s_eaMissingCos);
	eaClearFast(&s_eaMissingGeo);
	eaClearFast(&s_eaMissingMat);
	eaClearFast(&s_eaMissingTex);

	// The tailor is not ready as long as the UnlockedCostumeRefs are still being downloaded.
	for (i = eaSize(&g_CostumeEditState.eaOwnedUnlockedCostumeRefs) - 1; i >= 0; i--) {
		iTotal_CC++;
		if (GET_REF(g_CostumeEditState.eaOwnedUnlockedCostumeRefs[i]->hCostume)) {
			iThere_CC++;
			pUnlockedCostume = GET_REF(g_CostumeEditState.eaOwnedUnlockedCostumeRefs[i]->hCostume);
			for (j = eaSize(&pUnlockedCostume->eaParts) - 1; j >= 0; j--) {
				if (IS_HANDLE_ACTIVE(pUnlockedCostume->eaParts[j]->hGeoDef)) {
					iTotal_CC++;
					if (GET_REF(pUnlockedCostume->eaParts[j]->hGeoDef)) {
						iThere_CC++;
					} else {
						eaPush(&s_eaMissingGeo, REF_STRING_FROM_HANDLE(pUnlockedCostume->eaParts[j]->hGeoDef));
					}
				}
				if (IS_HANDLE_ACTIVE(pUnlockedCostume->eaParts[j]->hMatDef)) {
					iTotal_CC++;
					if (GET_REF(pUnlockedCostume->eaParts[j]->hMatDef)) {
						iThere_CC++;
					} else {
						eaPush(&s_eaMissingMat, REF_STRING_FROM_HANDLE(pUnlockedCostume->eaParts[j]->hMatDef));
					}
				}
				if (IS_HANDLE_ACTIVE(pUnlockedCostume->eaParts[j]->hPatternTexture)) {
					iTotal_CC++;
					if (GET_REF(pUnlockedCostume->eaParts[j]->hPatternTexture)) {
						iThere_CC++;
					} else {
						eaPush(&s_eaMissingTex, REF_STRING_FROM_HANDLE(pUnlockedCostume->eaParts[j]->hPatternTexture));
					}
				}
				if (IS_HANDLE_ACTIVE(pUnlockedCostume->eaParts[j]->hDetailTexture)) {
					iTotal_CC++;
					if (GET_REF(pUnlockedCostume->eaParts[j]->hDetailTexture)) {
						iThere_CC++;
					} else {
						eaPush(&s_eaMissingTex, REF_STRING_FROM_HANDLE(pUnlockedCostume->eaParts[j]->hDetailTexture));
					}
				}
				if (IS_HANDLE_ACTIVE(pUnlockedCostume->eaParts[j]->hDiffuseTexture)) {
					iTotal_CC++;
					if (GET_REF(pUnlockedCostume->eaParts[j]->hDiffuseTexture)) {
						iThere_CC++;
					} else {
						eaPush(&s_eaMissingTex, REF_STRING_FROM_HANDLE(pUnlockedCostume->eaParts[j]->hDiffuseTexture));
					}
				}
				if (IS_HANDLE_ACTIVE(pUnlockedCostume->eaParts[j]->hSpecularTexture)) {
					iTotal_CC++;
					if (GET_REF(pUnlockedCostume->eaParts[j]->hSpecularTexture)) {
						iThere_CC++;
					} else {
						eaPush(&s_eaMissingTex, REF_STRING_FROM_HANDLE(pUnlockedCostume->eaParts[j]->hSpecularTexture));
					}
				}
				if (pUnlockedCostume->eaParts[j]->pMovableTexture) {
					if (IS_HANDLE_ACTIVE(pUnlockedCostume->eaParts[j]->pMovableTexture->hMovableTexture)) {
						iTotal_CC++;
						if (GET_REF(pUnlockedCostume->eaParts[j]->pMovableTexture->hMovableTexture)) {
							iThere_CC++;
						} else {
							eaPush(&s_eaMissingTex, REF_STRING_FROM_HANDLE(pUnlockedCostume->eaParts[j]->pMovableTexture->hMovableTexture));
						}
					}
				}
			}
		} else {
			eaPush(&s_eaMissingCos, REF_STRING_FROM_HANDLE(g_CostumeEditState.eaOwnedUnlockedCostumeRefs[i]->hCostume));
		}
	}

	if (eaSize(&s_eaMissingCos) != s_uLastMissingCos || eaSize(&s_eaMissingGeo) != s_uLastMissingGeo || eaSize(&s_eaMissingMat) != s_uLastMissingMat || eaSize(&s_eaMissingTex) != s_uLastMissingTex) {
		s_uLastChangeTime = gGCLState.totalElapsedTimeMs;
		s_bErrored = false;
	} else if (eaSize(&s_eaMissingCos) >= 0 && eaSize(&s_eaMissingGeo) >= 0 && eaSize(&s_eaMissingMat) >= 0 && eaSize(&s_eaMissingTex) >= 0) {
		if (s_uLastChangeTime + 10000 < gGCLState.totalElapsedTimeMs && !s_bErrored) {
			s_bErrored = true;

			if (isDevelopmentMode()) {
				for (i = 0; i < eaSize(&s_eaMissingCos); i++) {
					Errorf("Timeout waiting for server to send PlayerCostume '%s'", s_eaMissingCos[i]);
				}
				for (i = 0; i < eaSize(&s_eaMissingGeo); i++) {
					Errorf("Timeout waiting for server to send CostumeGeometry '%s'", s_eaMissingGeo[i]);
				}
				for (i = 0; i < eaSize(&s_eaMissingMat); i++) {
					Errorf("Timeout waiting for server to send CostumeMaterial '%s'", s_eaMissingMat[i]);
				}
				for (i = 0; i < eaSize(&s_eaMissingTex); i++) {
					Errorf("Timeout waiting for server to send CostumeTexture '%s'", s_eaMissingTex[i]);
				}
			}
		}
	}

	s_uLastMissingCos = eaSize(&s_eaMissingCos);
	s_uLastMissingGeo = eaSize(&s_eaMissingGeo);
	s_uLastMissingMat = eaSize(&s_eaMissingMat);
	s_uLastMissingTex = eaSize(&s_eaMissingTex);

	return g_CostumeEditState.bTailorReady && (
		s_bErrored || (
			s_uLastMissingCos == 0 &&
			s_uLastMissingGeo == 0 &&
			s_uLastMissingMat == 0 &&
			s_uLastMissingTex == 0
		)
	);
}

// Tells whether or not the player can edit their costume in the current location
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_CanEditCostume");
bool CostumeCreator_CanEditCostume(void)
{
	Entity *pEnt = entActivePlayerPtr();
	COSTUME_UI_TRACE_FUNC();
	if (pEnt && interaction_IsPlayerNearContact(pEnt, ContactFlag_Tailor))
	{
		return true;
	}
	return g_CostumeEditState.bEnableTailor;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_DoesGeoIncludeCategoryNamePart");
bool CostumeCreator_DoesGeoIncludeCategoryNamePart(ExprContext *pContext, const char *pchGeometryName, const char *pchCategoryNamePart)
{
	PCGeometryDef *pGeometry = pchGeometryName && *pchGeometryName ? RefSystem_ReferentFromString(g_hCostumeGeometryDict, pchGeometryName) : NULL;
	int i;
	COSTUME_UI_TRACE_FUNC();

	if (!pGeometry || !pchCategoryNamePart)
	{
		return false;
	}

	for (i = eaSize(&pGeometry->eaCategories) - 1; i >= 0; i--)
	{
		PCCategory *pCategory = GET_REF(pGeometry->eaCategories[i]->hCategory);
		if (strstri(pCategory->pcName, pchCategoryNamePart))
			return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_IsGeoCategorySelected");
bool CostumeCreator_IsGeoCategorySelected(ExprContext *pContext, const char *pchBoneName, const char *pchGeometryName)
{
	PCBoneDef *pBone = pchBoneName && *pchBoneName ? CostumeUI_FindBone(pchBoneName, GET_REF(g_CostumeEditState.hSkeleton)) : NULL;
	PCGeometryDef *pGeometry = pchGeometryName && *pchGeometryName ? RefSystem_ReferentFromString(g_hCostumeGeometryDict, pchGeometryName) : NULL;
	PCCategory *pCategory = NULL;
	int i;
	COSTUME_UI_TRACE_FUNC();

	if (!g_CostumeEditState.pCostume || !pGeometry || !pBone)
	{
		return false;
	}

	pCategory = costumeTailor_GetCategoryForRegion(g_CostumeEditState.pConstCostume, GET_REF(pBone->hRegion));

	if (pCategory)
	{
		for (i = eaSize(&pGeometry->eaCategories) - 1; i >= 0; i--)
		{
			if (pCategory == GET_REF(pGeometry->eaCategories[i]->hCategory))
			{
				return true;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetGeoCategorySelect");
const char *CostumeCreator_GetGeoCategorySelect(ExprContext *pContext, const char *pchBoneName, const char *pchGeometryName)
{
	PCBoneDef *pBone = pchBoneName && *pchBoneName ? CostumeUI_FindBone(pchBoneName, GET_REF(g_CostumeEditState.hSkeleton)) : NULL;
	PCGeometryDef *pGeometry = pchGeometryName && *pchGeometryName ? RefSystem_ReferentFromString(g_hCostumeGeometryDict, pchGeometryName) : NULL;
	PCRegion *pRegion = SAFE_GET_REF(pBone, hRegion);
	PCCategory *pCategory = NULL;
	COSTUME_UI_TRACE_FUNC();

	if (!pRegion || !g_CostumeEditState.pCostume || !pGeometry || !pBone)
	{
		return "";
	}

	pCategory = eaSize(&pGeometry->eaCategories) >= 0 ? GET_REF(pGeometry->eaCategories[0]->hCategory) : NULL;
	return pCategory ? pCategory->pcName : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_ClearHoverCostume");
void CostumeCreator_ClearHoverCostume(ExprContext *pContext)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_CostumeEditState.pHoverCostume) {
		StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pHoverCostume);
		g_CostumeEditState.pHoverCostume = NULL;
		costumeEntity_ApplyEntityInfoToCostume(CostumeUI_GetSourceEnt(),g_CostumeEditState.pConstHoverCostume);
	}
	CostumeUI_RegenCostume(true);
}

//////////////////////////////////////////////////////////////////////////

static void CharacterCreation_FixCostumeName(NOCONST(PlayerCostume) *pCostume)
{
	if (!pCostume->pcName) {
		pCostume->pcName = allocAddString(CostumeCreator_GetPlayerName());
	} else if (pCostume->pcName && !pCostume->pcName[0]) {
		pCostume->pcName = allocAddString(CostumeCreator_GetPlayerName());
	}
	else if ( pCostume->pcName[strlen(pCostume->pcName)-1] == ' ' )
	{
		U32 iTrailCount = 0;
		U32 iSize = (U32)strlen(pCostume->pcName);
		const char* pCursor = pCostume->pcName + iSize - 1;
		char* pchBuff = _alloca(iSize+1);
		
		strcpy_s(pchBuff, iSize+1, pCostume->pcName);
		
		//remove trailing whitespace
		while(isspace((unsigned char)*pCursor)){
			--pCursor;
			++iTrailCount;
		}

		iSize -= iTrailCount;

		pchBuff[iSize]='\0';

		pCostume->pcName = allocAddString(pchBuff);
	}
}

NOCONST(PlayerCostume) *CharacterCreation_MakePlainCostumeFromSkeleton(PCSkeletonDef *pSkel, SpeciesDef *pSpecies)
{
	NOCONST(PlayerCostume) *pCostume;
	int i;

	pCostume = StructCreateNoConst(parse_PlayerCostume);
	pCostume->eCostumeType = kPCCostumeType_Player;

	SET_HANDLE_FROM_REFERENT(g_hCostumeSkeletonDict, pSkel, pCostume->hSkeleton);
	SET_HANDLE_FROM_REFERENT(g_hSpeciesDict, pSpecies, pCostume->hSpecies);

	if (pSkel && pSkel->fDefaultHeight) {
		pCostume->fHeight = pSkel->fDefaultHeight;
	} else {
		pCostume->fHeight = 6;
	}
	if (pSkel && pSkel->fDefaultMuscle) {
		pCostume->fMuscle = pSkel->fDefaultMuscle;
	} else {
		pCostume->fMuscle = 20;
	}
	if (pSkel) {
		for(i=0; i<eaSize(&pSkel->eaBodyScaleInfo); ++i) {
			if (i < eafSize(&pSkel->eafDefaultBodyScales)) {
				eafPush(&pCostume->eafBodyScales, pSkel->eafDefaultBodyScales[i]);
			} else {
				eafPush(&pCostume->eafBodyScales, 20);
			}
		} 
	}
	costumeTailor_SetDefaultSkinColor(pCostume, pSpecies, g_CostumeEditState.pSlotType);
	costumeTailor_FillAllBones(pCostume, pSpecies, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, true, false, true);

	return pCostume;
}

PCSkeletonDef *CharacterCreation_GetPlainSkeleton(void)
{
	PCSkeletonDef *pCSkel = NULL;
	U32 i;

	for (i = 0; i < ARRAY_SIZE(g_plainSkeletons); i++) {
		if (pCSkel =  RefSystem_ReferentFromString(g_hCostumeSkeletonDict, g_plainSkeletons[i]))
			return pCSkel;
	}

	assert(pCSkel != NULL);
	return pCSkel;
}

// Both a command and an expr func
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CharacterCreation.BuildPlainCostume") ACMD_HIDE;
void CharacterCreation_BuildPlainCostume(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation.BuildPlainCostume");
void CharacterCreation_BuildPlainCostume(void)
{
	PCSkeletonDef *pSkel = CharacterCreation_GetPlainSkeleton();
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	COSTUME_UI_TRACE_FUNC();

	// reset the unlocked costume pointers in case the refs now point to different values (or are gone).
	CostumeUI_SetUnlockedCostumes(true, true, NULL, NULL);

	if (g_CostumeEditState.pCostume) {
		StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
	}
	g_CostumeEditState.pCostume = CharacterCreation_MakePlainCostumeFromSkeleton(pSkel, pSpecies);
	REMOVE_HANDLE(g_CostumeEditState.hMood);

	CostumeUI_RegenCostumeEx(true, true);
}

// Reset the costume to the default for the current skeleton
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CharacterCreation.BuildCurrentPlainCostume") ACMD_HIDE;
void CharacterCreation_BuildCurrentPlainCostume(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_BuildCurrentPlainCostume");
void CharacterCreation_BuildCurrentPlainCostume(void)
{
	PCSkeletonDef *pSkel;
	COSTUME_UI_TRACE_FUNC();


	pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	if (!pSkel)
	{
		return;
	}

	if (g_CostumeEditState.pCostume) {
		StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
	}
	g_CostumeEditState.pCostume = CharacterCreation_MakePlainCostumeFromSkeleton(pSkel, GET_REF(g_CostumeEditState.hSpecies));
	REMOVE_HANDLE(g_CostumeEditState.hMood);

	CostumeUI_ClearSelections();
	CostumeUI_RegenCostumeEx(true, true);
}

// Make a set of random costumes
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CharacterCreation.BuildRandomCostumes") ACMD_HIDE;
void CharacterCreation_BuildRandomCostumes(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_BuildRandomCostumes");
void CharacterCreation_BuildRandomCostumes(void)
{
	COSTUME_UI_TRACE_FUNC();
	if (eaSize(&g_CostumeEditState.eaCachedCostumes) == 0) {
		Entity *pEnt = entActivePlayerPtr();
		NOCONST(PlayerCostume) *pCostume;
		PCSkeletonDef **eaSkels = NULL;
		SpeciesDef **eaSpecies = NULL;
		int i;

		// Get list of skeletons
		pCostume = StructCreateNoConst(parse_PlayerCostume);
		pCostume->eCostumeType = kPCCostumeType_Player;
		costumeTailor_GetValidSkeletons(pCostume, NULL, &eaSkels, true, true);
		StructDestroyNoConst(parse_PlayerCostume, pCostume);

		CostumeUI_GetAllValidSpecies(eaSkels, &eaSpecies);

		costumeRandom_SetRandomTable(g_CostumeEditState.pRandTable);

		// Make random costume for each
		for (i=0; i<eaSize(&eaSpecies); ++i) {
			pCostume = StructCreateNoConst(parse_PlayerCostume);
			pCostume->eCostumeType = kPCCostumeType_Player;

			SET_HANDLE_FROM_REFERENT(g_hSpeciesDict, eaSpecies[i], pCostume->hSpecies);
			COPY_HANDLE(pCostume->hSkeleton, eaSpecies[i]->hSkeleton);
			costumeRandom_FillRandom(pCostume, eaSpecies[i], guild_GetGuild(pEnt), NULL, NULL, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, true, true, true, g_CostumeEditState.bUnlockAll, true, true, true);
			costumeTailor_FillAllBones(pCostume, eaSpecies[i], g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, true, false, true);

			eaPush(&g_CostumeEditState.eaCachedCostumes, pCostume);
		}
		for(i=0; i<eaSize(&eaSkels); ++i) {
			pCostume = StructCreateNoConst(parse_PlayerCostume);
			pCostume->eCostumeType = kPCCostumeType_Player;
			
			SET_HANDLE_FROM_REFERENT(g_hCostumeSkeletonDict, eaSkels[i], pCostume->hSkeleton);
			costumeRandom_FillRandom(pCostume, NULL, NULL, NULL, NULL, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, true, true, true, g_CostumeEditState.bUnlockAll, true, true, true);
			costumeTailor_FillAllBones(pCostume, NULL, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, true, false, true);
			
			eaPush(&g_CostumeEditState.eaCachedCostumes, pCostume);
		}

		costumeRandom_SetRandomTable(NULL);

		eaDestroy(&eaSkels);
		eaDestroy(&eaSpecies);

		// Set the first one as the current one
		if (eaSize(&g_CostumeEditState.eaCachedCostumes) > 0) {
			StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pCostume);
			for (i=eaSize(&g_CostumeEditState.eaCachedCostumes)-1; i>=0; --i) {
				if (GET_REF(g_CostumeEditState.hSpecies) == GET_REF(g_CostumeEditState.eaCachedCostumes[i]->hSpecies) &&
					GET_REF(g_CostumeEditState.hSkeleton) == GET_REF(g_CostumeEditState.eaCachedCostumes[i]->hSkeleton)) {
					StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pCostume);
					g_CostumeEditState.pCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.eaCachedCostumes[i]);
				}
			}
			if (!g_CostumeEditState.pCostume) {
				g_CostumeEditState.pCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.eaCachedCostumes[0]);
			}
			REMOVE_HANDLE(g_CostumeEditState.hMood);
			CostumeUI_ClearSelections();
			CostumeUI_RegenCostumeEx(true, true);
		} else {
			CharacterCreation_BuildPlainCostume();
		}
	}
}

void CostumeUI_BuildPlainCostumes()
{
	if (eaSize(&g_CostumeEditState.eaCachedCostumes) == 0) {
		NOCONST(PlayerCostume) *pCostume;
		PCSkeletonDef **eaSkels = NULL;
		SpeciesDef **eaSpecies = NULL;
		int i;

		// Get list of skeletons
		pCostume = StructCreateNoConst(parse_PlayerCostume);
		pCostume->eCostumeType = kPCCostumeType_Player;
		costumeTailor_GetValidSkeletons(pCostume, NULL, &eaSkels, true, true);
		StructDestroyNoConst(parse_PlayerCostume, pCostume);

		CostumeUI_GetAllValidSpecies(eaSkels, &eaSpecies);

		// Make plain costume for each
		if (eaSize(&eaSpecies)) {
			for (i=0; i<eaSize(&eaSpecies); ++i) {
				if (GET_REF(eaSpecies[i]->hSkeleton)) {
					eaPush(&g_CostumeEditState.eaCachedCostumes, CharacterCreation_MakePlainCostumeFromSkeleton(GET_REF(eaSpecies[i]->hSkeleton), eaSpecies[i]));
				}
			}
		}
		for(i=0; i<eaSize(&eaSkels); ++i) {
			eaPush(&g_CostumeEditState.eaCachedCostumes, CharacterCreation_MakePlainCostumeFromSkeleton(eaSkels[i], NULL));
		}

		eaDestroy(&eaSkels);
		eaDestroy(&eaSpecies);

		// Set the first one as the current one
		if (eaSize(&g_CostumeEditState.eaCachedCostumes) > 0) {
			StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pCostume);
			for (i=eaSize(&g_CostumeEditState.eaCachedCostumes)-1; i>=0; --i) {
				if (GET_REF(g_CostumeEditState.hSpecies) == GET_REF(g_CostumeEditState.eaCachedCostumes[i]->hSpecies) &&
					GET_REF(g_CostumeEditState.hSkeleton) == GET_REF(g_CostumeEditState.eaCachedCostumes[i]->hSkeleton)) {
						StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pCostume);
						g_CostumeEditState.pCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.eaCachedCostumes[i]);
				}
			}
			if (!g_CostumeEditState.pCostume) {
				g_CostumeEditState.pCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.eaCachedCostumes[0]);
			}
			REMOVE_HANDLE(g_CostumeEditState.hMood);
			CostumeUI_ClearSelections();
			CostumeUI_RegenCostumeEx(true, true);
		} else {
			CharacterCreation_BuildPlainCostume();
		}
	}
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CharacterCreation.BuildPlainCostumes") ACMD_HIDE;
void CharacterCreation_BuildPlainCostumesCmd(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_BuildPlainCostumes");
void CharacterCreation_BuildPlainCostumesCmd(void)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeUI_BuildPlainCostumes();
}

// Choose the saved costume that matches the skeleton
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CharacterCreation.CacheCostume") ACMD_HIDE;
void CharacterCreation_CacheCostume(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_CacheCostume");
void CharacterCreation_CacheCostume(void)
{
	NOCONST(PlayerCostume) *pCostumeCopy;
	int i;
	COSTUME_UI_TRACE_FUNC();

	if (!g_CostumeEditState.pCostume) {
		return;
	}

	for(i=eaSize(&g_CostumeEditState.eaCachedCostumes)-1; i>=0; --i) {
		if (GET_REF(g_CostumeEditState.eaCachedCostumes[i]->hSkeleton) == GET_REF(g_CostumeEditState.pCostume->hSkeleton) &&
			GET_REF(g_CostumeEditState.eaCachedCostumes[i]->hSpecies) == GET_REF(g_CostumeEditState.pCostume->hSpecies)) {
			pCostumeCopy = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
			assert(pCostumeCopy);
			StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.eaCachedCostumes[i]);
			g_CostumeEditState.eaCachedCostumes[i] = pCostumeCopy;
		}
	}
}

// Choose the saved costume that matches the skeleton
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CharacterCreation.ChooseCachedCostume") ACMD_HIDE;
void CharacterCreation_ChooseCachedCostume(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_ChooseCachedCostume");
void CharacterCreation_ChooseCachedCostume(void)
{
	NOCONST(PlayerCostume) *pCostumeCopy;
	int i;
	COSTUME_UI_TRACE_FUNC();

	if (g_CostumeEditState.pCostume) {
		for(i=eaSize(&g_CostumeEditState.eaCachedCostumes)-1; i>=0; --i) {
			if (GET_REF(g_CostumeEditState.eaCachedCostumes[i]->hSkeleton) == GET_REF(g_CostumeEditState.pCostume->hSkeleton) &&
				GET_REF(g_CostumeEditState.eaCachedCostumes[i]->hSpecies) == GET_REF(g_CostumeEditState.pCostume->hSpecies)) {
				pCostumeCopy = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.eaCachedCostumes[i]);
				assert(pCostumeCopy);
				if (g_CostumeEditState.pCostume) {
					StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
				}
				g_CostumeEditState.pCostume = pCostumeCopy;
				REMOVE_HANDLE(g_CostumeEditState.hMood);
				CostumeUI_ClearSelections();
				CostumeUI_RegenCostumeEx(true, true);
				CostumeUI_UpdateUnlockedCostumeParts();
				return;
			}
		}
	}

	// No match, so build a plain costume
	CharacterCreation_BuildPlainCostume();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_ChooseCachedCostumeFromSelections");
void CharacterCreation_ChooseCachedCostumeFromSelections(void)
{
	NOCONST(PlayerCostume) *pCostumeCopy;
	int i;
	COSTUME_UI_TRACE_FUNC();

	for(i=eaSize(&g_CostumeEditState.eaCachedCostumes)-1; i>=0; --i) {
		if (GET_REF(g_CostumeEditState.eaCachedCostumes[i]->hSkeleton) == GET_REF(g_CostumeEditState.hSkeleton) &&
			GET_REF(g_CostumeEditState.eaCachedCostumes[i]->hSpecies) == GET_REF(g_CostumeEditState.hSpecies)) {
				pCostumeCopy = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.eaCachedCostumes[i]);
				assert(pCostumeCopy);
				if (g_CostumeEditState.pCostume) {
					StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
				}
				g_CostumeEditState.pCostume = pCostumeCopy;
				REMOVE_HANDLE(g_CostumeEditState.hMood);
				CostumeUI_ClearSelections();
				CostumeUI_RegenCostumeEx(true, true);
				CostumeUI_UpdateUnlockedCostumeParts();
				return;
		}
	}

	// No match, so build a plain costume
	CharacterCreation_BuildPlainCostume();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_InitDefaultCostumes");
void CharacterCreation_InitDefaultCostumes(const char *pchCostumeNames)
{
	char *pchContext = NULL;
	char *pchToken;
	COSTUME_UI_TRACE_FUNC();

	strdup_alloca(pchContext, pchCostumeNames);

	while ((pchToken = strtok_r(NULL, " ,\r\n\t", &pchContext)) != NULL)
	{
		bool bFound = false;
		int i;
		for (i = eaSize(&g_CostumeEditState.eaDefaultCostumes) - 1; i >= 0; i--) {
			const char *pchCostumeName = REF_STRING_FROM_HANDLE(g_CostumeEditState.eaDefaultCostumes[i]->hCostume);
			if (pchCostumeName && !stricmp(pchCostumeName, pchToken)) {
				bFound = true;
				break;
			}
		}

		if (!bFound) {
			CostumeCreatorCostumeRef *pCostumeRef = StructCreate(parse_CostumeCreatorCostumeRef);
			SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, pchToken, pCostumeRef->hCostume);
			eaPush(&g_CostumeEditState.eaDefaultCostumes, pCostumeRef);
		}
	}
}

static NOCONST(PlayerCostume) *CharacterCreation_GetDefaultCostume(PCSkeletonDef *pSkeleton, SpeciesDef *pSpecies, PCSlotType *pSlotType, bool bFillInSpecies)
{
	int i;
	PlayerCostume *pDefaultCostume = NULL;
	PlayerCostume *pDefaultCostumeTemplate = NULL;
	NOCONST(PlayerCostume) *pCostumeCopy;

	// Check default costume list
	for (i=eaSize(&g_CostumeEditState.eaDefaultCostumes)-1; i>=0; i--) {
		pDefaultCostume = GET_REF(g_CostumeEditState.eaDefaultCostumes[i]->hCostume);
		if (pDefaultCostume && pSkeleton == GET_REF(pDefaultCostume->hSkeleton) && pSpecies == GET_REF(pDefaultCostume->hSpecies)) {
			break;
		}
		pDefaultCostume = NULL;
	}

	// Check species preset list
	if (!pDefaultCostume && pSpecies && eaSize(&pSpecies->eaPresets)) {
		for (i=0; i<eaSize(&pSpecies->eaPresets); i++) {
			if (!pSlotType || pSlotType && pSpecies->eaPresets[i]->pcSlotType && *pSpecies->eaPresets[i]->pcSlotType && !stricmp(pSpecies->eaPresets[i]->pcSlotType, pSlotType->pcName)) {
				pDefaultCostume = GET_REF(pSpecies->eaPresets[i]->hCostume);
				break;
			}
		}
	}

	// Check default costume list for one without any species defined then choose it
	if (bFillInSpecies && pSpecies) {
		for (i=eaSize(&g_CostumeEditState.eaDefaultCostumes)-1; i>=0; i--) {
			pDefaultCostumeTemplate = GET_REF(g_CostumeEditState.eaDefaultCostumes[i]->hCostume);
			if (pDefaultCostumeTemplate && pSkeleton == GET_REF(pDefaultCostumeTemplate->hSkeleton) && NULL == GET_REF(pDefaultCostumeTemplate->hSpecies)) {
				break;
			}
			pDefaultCostumeTemplate = NULL;
		}
	}

	if (pDefaultCostume) {
		pCostumeCopy = StructCloneDeConst(parse_PlayerCostume, pDefaultCostume);
	} else if (pDefaultCostumeTemplate) {
		// Start with the template, then set it's species
		Entity *pEnt = entActivePlayerPtr();
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		pCostumeCopy = StructCloneDeConst(parse_PlayerCostume, pDefaultCostumeTemplate);
		SET_HANDLE_FROM_REFERENT(g_hSpeciesDict, pSpecies, pCostumeCopy->hSpecies);
		costumeTailor_MakeCostumeValid(pCostumeCopy, pSpecies, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, true, g_CostumeEditState.bUnlockAll, true, guild_GetGuild(pEnt), false, pExtract, false, g_CostumeEditState.eaPowerFXBones);
	} else {
		// Generate costume
		pCostumeCopy = CharacterCreation_MakePlainCostumeFromSkeleton(pSkeleton, pSpecies);
	}

	// Fix costume data
	pCostumeCopy->pcFileName = NULL;
	pCostumeCopy->pcName = NULL;
	pCostumeCopy->pcScope = NULL;
	pCostumeCopy->eCostumeType = kPCCostumeType_Player;

	// Fix costume species
	if (IS_HANDLE_ACTIVE(pCostumeCopy->hSpecies) && !pSpecies) {
		REMOVE_HANDLE(pCostumeCopy->hSpecies);
	} else if (GET_REF(pCostumeCopy->hSpecies) != pSpecies) {
		SET_HANDLE_FROM_REFERENT(g_hSpeciesDict, pSpecies, pCostumeCopy->hSpecies);
	}

	return pCostumeCopy;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_AreAllDefaultCostumesLoaded");
bool CharacterCreation_AreAllDefaultCostumesLoaded()
{
	int j;
	COSTUME_UI_TRACE_FUNC();

	for (j=eaSize(&g_CostumeEditState.eaDefaultCostumes)-1; j>=0; j--) {
		if (!GET_REF(g_CostumeEditState.eaDefaultCostumes[j]->hCostume)) {
			return false;
		}
	}

	return eaSize(&g_CostumeEditState.eaDefaultCostumes) > 0 ? true : false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_AssignDefaultSpeciesCostumes");
bool CharacterCreation_AssignDefaultSpeciesCostumes(bool bFillInSpecies)
{
	COSTUME_UI_TRACE_FUNC();
	if (eaSize(&g_CostumeEditState.eaCachedCostumes) == 0) {
		NOCONST(PlayerCostume) *pCostume;
		PCSkeletonDef **eaSkels = NULL;
		SpeciesDef **eaSpecies = NULL;
		int j;

		// Are all the costumes loaded?
		for (j=eaSize(&g_CostumeEditState.eaDefaultCostumes)-1; j>=0; j--) {
			if (!GET_REF(g_CostumeEditState.eaDefaultCostumes[j]->hCostume)) {
				return false;
			}
		}

		// Get list of skeletons
		pCostume = StructCreateNoConst(parse_PlayerCostume);
		pCostume->eCostumeType = kPCCostumeType_Player;
		costumeTailor_GetValidSkeletons(pCostume, NULL, &eaSkels, true, true);
		StructDestroyNoConst(parse_PlayerCostume, pCostume);
		pCostume = NULL;

		CostumeUI_GetAllValidSpecies(eaSkels, &eaSpecies);

		// Make plain costume for each
		for (j=0; j<eaSize(&eaSpecies); ++j) {
			eaPush(&g_CostumeEditState.eaCachedCostumes, CharacterCreation_GetDefaultCostume(GET_REF(eaSpecies[j]->hSkeleton), eaSpecies[j], g_CostumeEditState.pSlotType, bFillInSpecies));
		}
		for (j=0; j<eaSize(&eaSkels); ++j) {
			eaPush(&g_CostumeEditState.eaCachedCostumes, CharacterCreation_GetDefaultCostume(eaSkels[j], NULL, g_CostumeEditState.pSlotType, bFillInSpecies));
		}

		eaDestroy(&eaSkels);
		eaDestroy(&eaSpecies);

		// Set the first one as the current one
		if (eaSize(&g_CostumeEditState.eaCachedCostumes) > 0) {
			StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pCostume);
			for (j=eaSize(&g_CostumeEditState.eaCachedCostumes)-1; j>=0; --j) {
				if (GET_REF(g_CostumeEditState.hSpecies) == GET_REF(g_CostumeEditState.eaCachedCostumes[j]->hSpecies) &&
					GET_REF(g_CostumeEditState.hSkeleton) == GET_REF(g_CostumeEditState.eaCachedCostumes[j]->hSkeleton)) {
						StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pCostume);
						g_CostumeEditState.pCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.eaCachedCostumes[j]);
				}
			}
			if (!g_CostumeEditState.pCostume) {
				g_CostumeEditState.pCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.eaCachedCostumes[0]);
			}
			REMOVE_HANDLE(g_CostumeEditState.hMood);
			CostumeUI_ClearSelections();
			CostumeUI_RegenCostumeEx(true, true);
		} else {
			CharacterCreation_BuildPlainCostume();
		}
	}

	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_AssignDefaultCostumes");
bool CharacterCreation_AssignDefaultCostumes(void)
{
	COSTUME_UI_TRACE_FUNC();
	return CharacterCreation_AssignDefaultSpeciesCostumes(false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_AssignCurrentSkelDefaultSpeciesCostume");
bool CharacterCreation_AssignCurrentSkelDefaultSpeciesCostume(bool bFillInSpecies)
{
	PCSkeletonDef *pSkel = g_CostumeEditState.pCostume ? GET_REF(g_CostumeEditState.pCostume->hSkeleton) : GET_REF(g_CostumeEditState.hSkeleton);
	SpeciesDef *pSpecies = g_CostumeEditState.pCostume ? GET_REF(g_CostumeEditState.pCostume->hSpecies) : GET_REF(g_CostumeEditState.hSpecies);
	int i;
	COSTUME_UI_TRACE_FUNC();

	// Are all the costumes loaded?
	for (i=eaSize(&g_CostumeEditState.eaDefaultCostumes)-1; i>=0; i--) {
		if (!GET_REF(g_CostumeEditState.eaDefaultCostumes[i]->hCostume)) {
			return false;
		}
	}

	g_CostumeEditState.pCostume = CharacterCreation_GetDefaultCostume(pSkel, pSpecies, g_CostumeEditState.pSlotType, bFillInSpecies);
	REMOVE_HANDLE(g_CostumeEditState.hMood);
	CostumeUI_ClearSelections();
	CostumeUI_RegenCostumeEx(true, true);
	CostumeUI_UpdateUnlockedCostumeParts();

	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_AssignCurrentSkelDefaultCostume");
bool CharacterCreation_AssignCurrentSkelDefaultCostume(void)
{
	COSTUME_UI_TRACE_FUNC();
	return CharacterCreation_AssignCurrentSkelDefaultSpeciesCostume(false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_DeinitDefaultCostumes");
void CharacterCreation_DeinitDefaultCostumes(void)
{
	COSTUME_UI_TRACE_FUNC();
	while (eaSize(&g_CostumeEditState.eaDefaultCostumes)) {
		CostumeCreatorCostumeRef *pDefaultCostume = eaPop(&g_CostumeEditState.eaDefaultCostumes);
		REMOVE_HANDLE(pDefaultCostume->hCostume);
		if (GET_REF(pDefaultCostume->hWLCostume)) {
			WLCostume *pWLCostume = GET_REF(pDefaultCostume->hWLCostume);
			RefSystem_RemoveReferent(pWLCostume, true);
			wlCostumeFree(pWLCostume);

			REMOVE_HANDLE(pDefaultCostume->hWLCostume);
		}
		StructDestroy(parse_CostumeCreatorCostumeRef, pDefaultCostume);
	}

	eaDestroy(&g_CostumeEditState.eaDefaultCostumes);
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CharacterCreation_OmitHasOnlyOne") ACMD_HIDE;
void CharacterCreation_OmitHasOnlyOne(int omitHasOnlyOne);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_OmitHasOnlyOne");
void CharacterCreation_OmitHasOnlyOne(int omitHasOnlyOne)
{
	COSTUME_UI_TRACE_FUNC();
	g_bOmitHasOnlyOne = omitHasOnlyOne;
	g_bCountNone = omitHasOnlyOne; //This may need to go into and independent function depending on what are future needs are
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeUI_GetSlotType");
const char *CostumeUI_GetPlayerActiveCostumeSlotType(void)
{
	Entity *pEnt = entActivePlayerPtr();
	PCSlotType *st = pEnt ? costumeEntity_GetSlotType(pEnt, pEnt->pSaved->costumeData.iActiveCostume, false, NULL) : NULL;
	COSTUME_UI_TRACE_FUNC();
	return st ? st->pcName : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ShipEditor_GetName");
const char *ShipEditor_GetName(void)
{
	COSTUME_UI_TRACE_FUNC();
	return g_CostumeEditState.pcNemesisName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_CopyGuildEmblem");
void CharacterCreation_CopyGuildEmblem(Entity *pEnt)
{
	Guild *pGuild = pEnt ? guild_GetGuild(pEnt) : NULL;
	NOCONST(PlayerCostume) *pCostume = NULL;
	NOCONST(PCPart) *pPart = NULL;
	bool bChanged = false;
	int i;
	COSTUME_UI_TRACE_FUNC();

	if (pGuild)
	{
		//Build the costume
		pCostume = StructCreateNoConst(parse_PlayerCostume);
		SET_HANDLE_FROM_STRING("CostumeSkeleton", "Emblem", pCostume->hSkeleton);
		pCostume->pcName = allocAddString("GuildEmblemHeadShot");
		pCostume->eCostumeType = kPCCostumeType_Unrestricted;
		pPart = StructCreateNoConst(parse_PCPart);
		eaPush(&pCostume->eaParts, pPart);
		SET_HANDLE_FROM_STRING("CostumeBone", "EmblemBackground", pPart->hBoneDef);
		SET_HANDLE_FROM_STRING("CostumeGeometry", "E_Emblembackground_01", pPart->hGeoDef);
		SET_HANDLE_FROM_STRING("CostumeMaterial", "E_Emblembackground_01_Character_Master_Basic_01", pPart->hMatDef);
		SET_HANDLE_FROM_STRING("CostumeTexture", "Emblembackground_01_C", pPart->hPatternTexture);
		*((U32*)pPart->color0) = 0xFFFFFFFF;
		*((U32*)pPart->color1) = 0xFFFFFFFF;
		*((U32*)pPart->color2) = 0xFFFFFFFF;
		*((U32*)pPart->color3) = 0xFFFFFFFF;
		pPart = StructCreateNoConst(parse_PCPart);
		eaPush(&pCostume->eaParts, pPart);
		SET_HANDLE_FROM_STRING("CostumeBone", "Emblem", pPart->hBoneDef);
		SET_HANDLE_FROM_STRING("CostumeGeometry", "E_Emblem_Placeholder_01", pPart->hGeoDef);
		SET_HANDLE_FROM_STRING("CostumeMaterial", "Emblem_Character_Master_02", pPart->hMatDef);
		if (pGuild->pcEmblem && *pGuild->pcEmblem)
		{
			for (i = 0; i < eaSize(&g_GuildEmblems.eaEmblems); ++i)
			{
				if (!stricmp(REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[i]->hTexture),pGuild->pcEmblem))
				{
					if (!g_GuildEmblems.eaEmblems[i]->bFalse)
					{
						pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
						SET_HANDLE_FROM_STRING("CostumeTexture", pGuild->pcEmblem, pPart->hPatternTexture);
						pPart->pTextureValues->fPatternValue = pGuild->fEmblemRotation;
						*((U32*)pPart->color2) = pGuild->iEmblemColor0;
						*((U32*)pPart->color3) = pGuild->iEmblemColor1;
					}
					else
					{
						pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
						pPart->pTextureValues->fPatternValue = -100;
						*((U32*)pPart->color2) = 0xFF010101;
						*((U32*)pPart->color3) = 0xFEFEFEFF;
					}
					break;
				}
			}
		}
		else
		{
			pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
			pPart->pTextureValues->fPatternValue = -100;
			*((U32*)pPart->color2) = 0xFF010101;
			*((U32*)pPart->color3) = 0xFEFEFEFF;
		}
		if (pGuild->pcEmblem2 && *pGuild->pcEmblem2)
		{
			pPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
			SET_HANDLE_FROM_STRING("CostumeTexture", pGuild->pcEmblem2, pPart->pMovableTexture->hMovableTexture);
			pPart->pMovableTexture->fMovableRotation = pGuild->fEmblem2Rotation;
			pPart->pMovableTexture->fMovableX = pGuild->fEmblem2X;
			pPart->pMovableTexture->fMovableY = pGuild->fEmblem2Y;
			pPart->pMovableTexture->fMovableScaleX = pGuild->fEmblem2ScaleX;
			pPart->pMovableTexture->fMovableScaleY = pGuild->fEmblem2ScaleY;
			*((U32*)pPart->color0) = pGuild->iEmblem2Color0;
			*((U32*)pPart->color1) = pGuild->iEmblem2Color1;
		}
		else
		{
			*((U32*)pPart->color2) = 0xFF010101;
			*((U32*)pPart->color3) = 0xFEFEFEFF;
		}
		if (pGuild->pcEmblem3 && *pGuild->pcEmblem3)
		{
			SET_HANDLE_FROM_STRING("CostumeTexture", pGuild->pcEmblem3, pPart->hDetailTexture);
		}
		else
		{
			SET_HANDLE_FROM_STRING("CostumeTexture", "Emblem_Patch_01_N", pPart->hDetailTexture);
		}

		//
		REMOVE_HANDLE(g_CostumeEditState.hSpecies);
		g_CostumeEditState.pSlotType = NULL;
		eaClear(&g_CostumeEditState.eaUnlockedCostumes);
		eaClear(&g_CostumeEditState.eaOwnedUnlockedCostumes);
		eaClear(&g_CostumeEditState.eaOwnedUnlockedCostumeRefs);

		CostumeUI_ClearSelections();

		if (pCostume) {

			if (!g_CostumeEditState.pStartCostume || (StructCompare(parse_PlayerCostume, pCostume, g_CostumeEditState.pStartCostume, 0, 0, 0)) || (StructCompare(parse_PlayerCostume, pCostume, g_CostumeEditState.pCostume, 0, 0, 0) != 0)) {
				NOCONST(PlayerCostume) *pCostumeCopy = NULL;
				pCostumeCopy = StructCloneNoConst(parse_PlayerCostume, pCostume);
				assert(pCostumeCopy);
				if (g_CostumeEditState.pCostume) {
					StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
				}
				// We set the costume to Unrestricted when copying it in so it won't be mutated for
				// display.  Prior to any attempt to edit the costume, however, be sure to set the
				// costume type back to Player type or the editor may do bad things.
				pCostumeCopy->eCostumeType = kPCCostumeType_Unrestricted;
				g_CostumeEditState.pCostume = pCostumeCopy;
				if (!g_CostumeEditState.pCostume->pcName && pEnt && pEnt->pSaved && pEnt->pSaved->savedName){
					g_CostumeEditState.pCostume->pcName = allocAddString(pEnt->pSaved->savedName);
				}
				CharacterCreation_FixCostumeName(g_CostumeEditState.pCostume);

				pCostumeCopy = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
				assert(pCostumeCopy);
				if (g_CostumeEditState.pStartCostume) {
					StructDestroyNoConst(parse_PlayerCostume, g_CostumeEditState.pStartCostume);
				}
				g_CostumeEditState.pStartCostume = pCostumeCopy;

				bChanged = true;
			}
		}

		eaClear(&g_CostumeEditState.eaPowerFXBones);

		g_CostumeEditState.eCostumeStorageType = 0;
		g_CostumeEditState.uCostumeEntContainerID = 0;
		g_CostumeEditState.iCostumeIndex = 0;

		REMOVE_HANDLE(g_CostumeEditState.hMood);

		if (bChanged) {
			CostumeUI_RegenCostumeEx(true, true);
		}

		g_CostumeEditState.bUnlockAll = true;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SetGuildEmblem");
void CharacterCreation_SetGuildEmblem(Entity *pEnt)
{
	int i;
	NOCONST(PCPart) *pPart = NULL;
	PCBoneDef *pBone = RefSystem_ReferentFromString("CostumeBone", "Emblem");
	COSTUME_UI_TRACE_FUNC();

	if (g_CostumeEditState.pCostume && pBone)
	{
		for (i = eaSize(&g_CostumeEditState.pCostume->eaParts)-1; i >= 0; --i)
		{
			if (GET_REF(g_CostumeEditState.pCostume->eaParts[i]->hBoneDef) == pBone)
			{
				pPart = g_CostumeEditState.pCostume->eaParts[i];
				break;
			}
		}
	}

	if (pPart)
	{
		Guild *pGuild = pEnt ? guild_GetGuild(pEnt) : NULL;
		if (pGuild)
		{
			const char *pcEmblem = REF_STRING_FROM_HANDLE(pPart->hPatternTexture);
			if (pcEmblem && *pcEmblem && stricmp(pcEmblem, "None"))
			{
				ServerCmd_Guild_SetAdvancedEmblem(pcEmblem, *(U32*)pPart->color2, *(U32*)pPart->color3, pPart->pTextureValues ? pPart->pTextureValues->fPatternValue : -100, true);
			}
			else
			{
				if (gConf.bAllowGuildstoHaveNoEmblems)
				{
					ServerCmd_Guild_SetAdvancedEmblem(NULL, *(U32*)pPart->color2, *(U32*)pPart->color3, pPart->pTextureValues ? pPart->pTextureValues->fPatternValue : -100, true);
				}
				else
				{
					for (i = 0; i < eaSize(&g_GuildEmblems.eaEmblems); ++i)
					{
						if (g_GuildEmblems.eaEmblems[i]->bFalse)
						{
							pcEmblem = REF_STRING_FROM_HANDLE(g_GuildEmblems.eaEmblems[i]->hTexture);
							break;
						}
					}
					if (pcEmblem)
					{
						ServerCmd_Guild_SetAdvancedEmblem(pcEmblem, *(U32*)pPart->color2, *(U32*)pPart->color3, pPart->pTextureValues ? pPart->pTextureValues->fPatternValue : -100, true);
					}
				}
			}
			pcEmblem = NULL;
			if (pPart->pMovableTexture)
			{
				pcEmblem = REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture);
			}
			if (pcEmblem && *pcEmblem && stricmp(pcEmblem, "None"))
			{
				ServerCmd_Guild_SetAdvancedEmblem2(pcEmblem, *(U32*)pPart->color0, *(U32*)pPart->color1, pPart->pMovableTexture->fMovableRotation,
													pPart->pMovableTexture->fMovableX, pPart->pMovableTexture->fMovableY, pPart->pMovableTexture->fMovableScaleX, pPart->pMovableTexture->fMovableScaleY);
			}
			else
			{
				ServerCmd_Guild_SetAdvancedEmblem2(NULL, *(U32*)pPart->color0, *(U32*)pPart->color1, 0, 0, 0, 1, 1);
			}
			pcEmblem = REF_STRING_FROM_HANDLE(pPart->hDetailTexture);
			if (pcEmblem && *pcEmblem && stricmp(pcEmblem, "None"))
			{
				ServerCmd_Guild_SetAdvancedEmblem3(pcEmblem, true);
			}
			else
			{
				ServerCmd_Guild_SetAdvancedEmblem3(NULL, true);
			}
		}
	}
}

// Delete a saved costume
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(9) ACMD_NAME("CostumeCreator_DeleteCostume") ACMD_HIDE;
void CostumeCreator_DeleteCostumeCmd(PCCostumeStorageType eCostumeType, int iIndex)
{
	COSTUME_UI_TRACE_FUNC();
	if (iIndex != 0)
		ServerCmd_DeletePlayerCostume(eCostumeType, iIndex);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_GetSaveCostumeResult);
const char *CostumeCreator_GetSaveCostumeResult(ExprContext *pContext)
{
	const char *pchResult = s_pchStorePlayerCostumeResult ? TranslateMessageKey(s_pchStorePlayerCostumeResult) : "(null)";
	COSTUME_UI_TRACE_FUNC();

	if (!pchResult)
	{
		if (s_pchStorePlayerCostumeResult)
		{
			char *pchTemp = NULL;
			estrStackCreate(&pchTemp);
			estrConcatf(&pchTemp, "[UNTRANSLATED: %s]", s_pchStorePlayerCostumeResult);
			pchResult = exprContextAllocString(pContext, pchTemp);
			estrDestroy(&pchTemp);
		}
		else
		{
			pchResult = "[null]";
		}
	}

	return pchResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_CostumeSaved);
bool CostumeCreator_CostumeSaved(void)
{
	COSTUME_UI_TRACE_FUNC();
	return s_bStorePlayerCostumeSuccessful;
}

AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0);
void CostumeCreator_SetStorePlayerCostumeResult(bool bSuccessful, const char *pcResultKey)
{
	if (!bSuccessful)
		gclNotifyReceive(bSuccessful ? kNotifyType_Default : kNotifyType_Failed, TranslateMessageKeyDefault(pcResultKey, pcResultKey), NULL, NULL);

	s_bStorePlayerCostumeSuccessful = bSuccessful;
	if (s_pchStorePlayerCostumeResult)
		free(s_pchStorePlayerCostumeResult);
	s_pchStorePlayerCostumeResult = pcResultKey ? strdup(pcResultKey) : NULL;
}

// Rename a costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_RenameCostume");
void CostumeCreator_RenameCostume(/*PCCostumeStorageType*/ int eCostumeType, int iPetNum, int iCostumeIndex, const char *pcNewName)
{
	CostumeCreator_RenameCostumePet(eCostumeType, iPetNum, iCostumeIndex, kPCPay_Default, pcNewName);
}

// Rename a costume
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CostumeCreator_RenameCostume") ACMD_HIDE;
void CostumeCreator_RenameCostumeCmd(PCCostumeStorageType eCostumeType, int iPetNum, int iCostumeIndex, const char *pcNewName)
{
	CostumeCreator_RenameCostumePet(eCostumeType, iPetNum, iCostumeIndex, kPCPay_Default, pcNewName);
}

// Both a command and an expr func
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CostumeCreation.SelectBone") ACMD_HIDE;
void CharacterCreation_SelectBone(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreation_SelectBone");
void CharacterCreation_SelectBone(void) 
{
	COSTUME_UI_TRACE_FUNC();
	if (g_pCostumeView) {
		PCBoneDef *pBone = costumeView_GetSelectedBone(g_pCostumeView, g_CostumeEditState.pCostume);
		if (pBone) {
			CostumeCreator_SetBone(pBone->pcName);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreation_FindMousedBone");
SA_RET_OP_VALID PCBoneDef *CostumeCreation_FindMousedBone(void)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_pCostumeView) {
		PCBoneDef *pNewBone = costumeView_GetSelectedBone(g_pCostumeView, g_CostumeEditState.pCostume);
		if (pNewBone != g_CostumeEditState.pSelectedBone) {
			g_CostumeEditState.pSelectedBone = pNewBone;
			g_CostumeEditState.bValidSelectedBone = true;
			CostumeUI_ValidateSelectedBone(g_CostumeEditState.pCostume);
		}
	} else {
		g_CostumeEditState.pSelectedBone = NULL;
	}
	return g_CostumeEditState.bValidSelectedBone ? g_CostumeEditState.pSelectedBone : NULL;
}

void CostumeCreation_GetBoneScreenLocation(const char* pchBoneName, F32* xOut, F32* yOut)
{
	if (g_pCostumeView && pchBoneName && xOut && yOut) {

		char* context = NULL;
		char* pchString = strdup(pchBoneName);
		char* pTok = strtok_s(pchString, " ", &context);
		while(pTok)
		{
			SpeciesDef *pSpecies = SAFE_GET_REF(g_CostumeEditState.pCostume, hSpecies);
			Vec3 vWorldPos;
			Vec2 vScreenPos;

			if (!g_CostumeEditState.pCostume) {
				return;
			}

			if (g_pCostumeView->costume.pSkel) {
		//		costumeTailor_GetValidBones(pCostume, GET_REF(pCostume->hSkeleton), NULL, NULL, pSpecies, NULL, NULL, &pGraphics->eaTempBones, CGVF_OMIT_EMPTY | CGVF_UNLOCK_ALL);
				dynSkeletonGetBoneWorldPosByName(g_pCostumeView->costume.pSkel->pRoot, pTok, vWorldPos);
				gfxWorldToScreenSpaceVector(&s_CameraView, vWorldPos, vScreenPos, true);
	//				pResult = costumeView_GetCostumeBoneForSkelBone(pGraphics, pBone->pcTag);
	//			}
	//			eaDestroy(&pGraphics->eaTempBones);
				*xOut = vScreenPos[0];
				*yOut = vScreenPos[1];

			}
			pTok = strtok_s(NULL, " ", &context);
		}
		free(pchString);
	}
}


// Both a command and an expr func
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CostumeCreation.SetSky") ACMD_HIDE;
void CharacterCreation_SetSky(const char *pcNewSkyName);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreation_SetSky");
void CharacterCreation_SetSky(const char *pcNewSkyName) 
{
	static char pcSkyNameStorage[128];
	COSTUME_UI_TRACE_FUNC();
	if (!g_pCostumeView) return;
	if (pcNewSkyName && *pcNewSkyName && *pcNewSkyName != '0')
	{
		strncpy(pcSkyNameStorage, pcNewSkyName, 128);
		pcSkyNameStorage[127] = '\0';
		g_pCostumeView->bOverrideSky = true;
		g_pCostumeView->pcSkyOverride = pcSkyNameStorage;
	}
	else
	{
		g_pCostumeView->bOverrideSky = false;
		g_pCostumeView->pcSkyOverride = NULL;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_UpdateCostumeSpecies");
void CharacterCreation_UpdateCostumeSpecies(bool bBuildCostume)
{
	COSTUME_UI_TRACE_FUNC();
	// Set the species from character creation
	if (g_pFakePlayer && g_pFakePlayer->pChar && GET_REF(g_pFakePlayer->pChar->hSpecies)) {
		SpeciesDef *pSpecies = GET_REF(g_pFakePlayer->pChar->hSpecies);
		COPY_HANDLE(g_CostumeEditState.hSpecies, g_pFakePlayer->pChar->hSpecies);
		COPY_HANDLE(g_CostumeEditState.hSkeleton, pSpecies->hSkeleton);
		CostumeUI_SetUnlockedCostumes(true, true, NULL, NULL);
		if (pSpecies) {
			if (bBuildCostume || !g_CostumeEditState.pCostume || GET_REF(g_CostumeEditState.hSkeleton) != GET_REF(g_CostumeEditState.pCostume->hSkeleton)) {
				CharacterCreation_ChooseCachedCostumeFromSelections();
			} else {
				const char *pchCostumeSpecies = REF_STRING_FROM_HANDLE(g_CostumeEditState.pCostume->hSpecies);
				if (!pchCostumeSpecies || stricmp(pSpecies->pcName, pchCostumeSpecies) != 0) {
					COPY_HANDLE(g_CostumeEditState.pCostume->hSpecies, g_CostumeEditState.hSpecies);
				}
			}
		}
	}
}

AUTO_EXPR_FUNC_STATIC_CHECK;
void CostumeUI_SetFOVHackDeprecate(ExprContext *pContext, float fFOV, float fXDistanceMultiplier, float fYDistanceMultiplier)
{
	ErrorFilenamef(exprContextGetBlameFile(pContext), "Is using deprecated expression function CostumeUI_SetFOVHack. It's a terrible hack that doesn't really work, and shouldn't be used.");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeUI_SetFOVHack");
void CostumeUI_SetFOVHack(ExprContext *pContext, float fFOV, float fXDistanceMultiplier, float fYDistanceMultiplier)
{
	// TODO: remove me
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeUI_EnableCostumeUpdate");
void CostumeUI_EnableCostumeUpdate(int enable)
{
	// TODO: remove me
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeUI_BeforeTick");
void CostumeUI_OncePerFrame(void)
{
	COSTUME_UI_TRACE_FUNC();
	// Deprecated
}

void CostumeUI_UpdateWorldRegion(bool bForceOff)
{
	if (!bForceOff && GSM_IsStateActive(GCL_LOGIN)) {
		GfxCameraController *pCam = gclLoginGetCameraController();
		costumeCameraUI_SetFxRegion(worldGetWorldRegionByPos(pCam->camfocus));
	} else {
		costumeCameraUI_SetFxRegion(NULL);
	}
}

void Costume_OncePerFrame(void)
{
	static int tickCount = 0;
	static bool s_bLocalCopyOfHideUnownedCostumes;
	bool bHideUnownedCostumesToggled = (s_bLocalCopyOfHideUnownedCostumes != g_bHideUnownedCostumes);
	COSTUME_UI_TRACE_FUNC();

	CostumeUI_SetUnlockedCostumes(g_CostumeEditState.bUpdateUnlockedRefs || bHideUnownedCostumesToggled, true, NULL, NULL);
	g_CostumeEditState.bUpdateUnlockedRefs = false;

	if (g_pCostumeView) {
		costumeCameraUI_CameraOncePerFrame(g_pCostumeView, &s_CameraView, s_bCreatorActive);
	}

	if (g_CostumeEditState.pCostume) {
		if (g_CostumeEditState.bUpdateLists || g_CostumeEditState.bUpdateLines || bHideUnownedCostumesToggled) {
			CostumeUI_RegenCostume(true);
		}
	}

	// Swap to the buffered lines.
	if (eaSize(&g_CostumeEditState.eaBufferedEditLine)) {
		costumeLineUI_DestroyLines(&g_CostumeEditState.eaCostumeEditLine);
		eaCopy(&g_CostumeEditState.eaCostumeEditLine, &g_CostumeEditState.eaBufferedEditLine);
		eaClear(&g_CostumeEditState.eaBufferedEditLine);
	}
	s_bLocalCopyOfHideUnownedCostumes = g_bHideUnownedCostumes;
}

static void Costume_DictionaryUpdate(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	COSTUME_UI_TRACE_FUNC();
	if (eType != RESEVENT_NO_REFERENCES)
	{
		g_CostumeEditState.bUpdateLists = true;
		g_CostumeEditState.bUpdateLines = true;
		if (g_CostumeEditState.bUnlockMetaIncomplete && !stricmp(pDictName, "PlayerCostume"))
			g_CostumeEditState.bUpdateUnlockedRefs = true;
	}
}

static void MTCostume_Update(void *pUserData)
{
	COSTUME_UI_TRACE_FUNC();
	g_CostumeEditState.bUpdateLists = true;
	g_CostumeEditState.bUpdateLines = true;
	g_CostumeEditState.bUpdateUnlockedRefs = true;
}

void CostumeUI_DataChanged(void)
{
	if (g_pCostumeView)
	{
		COSTUME_UI_TRACE_FUNC();
		g_CostumeEditState.bUpdateLists = true;
		g_CostumeEditState.bUpdateLines = true;
		g_CostumeEditState.bUpdateUnlockedRefs = true;
	}
}

bool CostumeUI_IsCreatorActive(void)
{
	return s_bCreatorActive;
}

static void Costume_Enter(void)
{
	COSTUME_UI_TRACE_FUNC();
	if (!g_pCostumeView)
	{
		bool bBuildCostume = false;

		ui_GenSetPointerVar("CostumeState", &g_CostumeEditState, parse_CostumeEditState);
		costumeCameraUI_CreateWorld(&s_CameraView);
		CostumeUI_UpdateWorldRegion(false);
		s_bCreatorActive = true;

		g_CostumeEditState.eCostumeStorageType = 0;
		g_CostumeEditState.uCostumeEntContainerID = 0;
		g_CostumeEditState.iCostumeIndex = 0;

		// Be very careful to clear this when leaving or else bad things happen on the client
		worldCellSetNoCloseCellsDueToTempCameraPosition(true);

		assert(g_pCostumeView);
		if (!g_CostumeEditState.pCostume) {
			CharacterCreation_BuildPlainCostume();
			bBuildCostume = true;
		} else {
			// Need to render the costume here
			// Don't call CostumeEdit_RegenCostume since that updates lists and we
			// don't want to update lists.  Only the tailor uses that and many other
			// places render costumes.
			CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), GET_REF(g_CostumeEditState.hClass), g_CostumeEditState.eaShowItems);
		}
		assert(g_CostumeEditState.pCostume);
		gclRegisterGhostDrawFunc(costumeCameraUI_DrawGhosts);

		// Create random table
		eaiClear(&g_CostumeEditState.eaiSeeds);
		g_CostumeEditState.seedPos = -1;
		g_CostumeEditState.pRandTable = mersenneTableCreate(randomU32());

		if (GSM_IsStateActive(GCL_LOGIN_NEW_CHARACTER_CREATION))
		{
			g_CostumeEditState.pSlotType = costumeEntity_GetSlotType(CONTAINER_RECONST(Entity, g_pFakePlayer), 0, false, NULL);

			CharacterCreation_UpdateCostumeSpecies(bBuildCostume);
		}

		CostumeUI_SetUnlockedCostumes(true, true, NULL, NULL);
	} else {
		// Reset random table
		eaiClear(&g_CostumeEditState.eaiSeeds);
		g_CostumeEditState.seedPos = -1;
		seedMersenneTable(g_CostumeEditState.pRandTable, randomU32());
	}

	resDictRegisterEventCallback(g_hPlayerCostumeDict, Costume_DictionaryUpdate, NULL);
	resDictRegisterEventCallback(g_hCostumeSkeletonDict, Costume_DictionaryUpdate, NULL);
	resDictRegisterEventCallback(g_hCostumeRegionDict, Costume_DictionaryUpdate, NULL);
	resDictRegisterEventCallback(g_hCostumeCategoryDict, Costume_DictionaryUpdate, NULL);
	resDictRegisterEventCallback(g_hCostumeBoneDict, Costume_DictionaryUpdate, NULL);
	resDictRegisterEventCallback(g_hCostumeGeometryDict, Costume_DictionaryUpdate, NULL);
	resDictRegisterEventCallback(g_hCostumeMaterialDict, Costume_DictionaryUpdate, NULL);
	resDictRegisterEventCallback(g_hCostumeTextureDict, Costume_DictionaryUpdate, NULL);
	resDictRegisterEventCallback(g_hCostumeMoodDict, Costume_DictionaryUpdate, NULL);
	resDictRegisterEventCallback(g_hCostumeVoiceDict, Costume_DictionaryUpdate, NULL);
	// Most/all of the following may not actually be necessary, but I'm adding them just
	// in case the tailor ever depends on them. It'll save future crashes. -JM
	resDictRegisterEventCallback(g_hCostumeColorsDict, Costume_DictionaryUpdate, NULL);
	resDictRegisterEventCallback(g_hCostumeColorQuadsDict, Costume_DictionaryUpdate, NULL);
	resDictRegisterEventCallback(g_hCostumeStyleDict, Costume_DictionaryUpdate, NULL);
	resDictRegisterEventCallback(g_hCostumeLayerDict, Costume_DictionaryUpdate, NULL);
	resDictRegisterEventCallback(g_hCostumeMaterialAddDict, Costume_DictionaryUpdate, NULL);
	resDictRegisterEventCallback(g_hCostumeGeometryAddDict, Costume_DictionaryUpdate, NULL);
	resDictRegisterEventCallback(g_hCostumeSetsDict, Costume_DictionaryUpdate, NULL);
	resDictRegisterEventCallback(g_hCostumeGroupsDict, Costume_DictionaryUpdate, NULL);
	gclMicroTrans_AddCostumeListChangedHandler(MTCostume_Update, NULL);

	// Ask server to enable/disable based on proximity to tailor
	g_CostumeEditState.bTailorReady = false;
	ServerCmd_Tailor_CheckIfTailor();
}

static void Costume_Leave(void)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_pCostumeView)
	{
		CostumeEditList_ClearCostumeSourceList(NULL, false);

		// Clear Power FXBones
		eaClear(&g_CostumeEditState.eaPowerFXBones);

		CostumeCreator_ResetBoneFilters();

		// Clear the unlock metadata
		CostumeUI_ClearUnlockMetaData();
		stashTableDestroySafe(&g_CostumeEditState.stashGeoUnlockMeta);
		stashTableDestroySafe(&g_CostumeEditState.stashMatUnlockMeta);
		stashTableDestroySafe(&g_CostumeEditState.stashTexUnlockMeta);

		eaDestroy(&g_CostumeEditState.eaShowItems);

		eaClear(&g_CostumeEditState.eaBodyScalesExclude);
		eaClear(&g_CostumeEditState.eaBodyScalesInclude);

		gclRemoveGhostDrawFunc(costumeCameraUI_DrawGhosts);
		costumeCameraUI_ClearBackgroundCostumes();
		costumeView_FreeGraphics(g_pCostumeView);
		g_pCostumeView = NULL;
		if (!GSM_IsStateActive(GCL_LOGIN))
		{
			gfxSetActiveCameraController(&gGCLState.pPrimaryDevice->gamecamera, true);
			gclSetGameCameraActive();
		}
		gGCLState.pPrimaryDevice->activecamera->useHorizontalFOV = false;
		costumeLineUI_DestroyLines(&g_CostumeEditState.eaCostumeEditLine);
		costumeLineUI_DestroyLines(&g_CostumeEditState.eaBufferedEditLine);

		if(g_CostumeEditState.eaFXArray) {
			eaDestroyStruct(&g_CostumeEditState.eaFXArray, parse_PCFXTemp);
			g_CostumeEditState.eaFXArray = NULL;
		}

		StructDeInit(parse_CostumeEditState, &g_CostumeEditState);
		gGCLState.bHideWorld = false;

		// Be very careful to clear this when leaving or else bad things happen on the client
		worldCellSetNoCloseCellsDueToTempCameraPosition(false);
	}

	if(g_CostumeEditState.eaFXArray) {
		eaDestroyStruct(&g_CostumeEditState.eaFXArray, parse_PCFXTemp);
		g_CostumeEditState.eaFXArray = NULL;
	}
	
	s_bCreatorActive = false;

	resDictRemoveEventCallback(g_hPlayerCostumeDict, Costume_DictionaryUpdate);
	resDictRemoveEventCallback(g_hCostumeSkeletonDict, Costume_DictionaryUpdate);
	resDictRemoveEventCallback(g_hCostumeRegionDict, Costume_DictionaryUpdate);
	resDictRemoveEventCallback(g_hCostumeCategoryDict, Costume_DictionaryUpdate);
	resDictRemoveEventCallback(g_hCostumeBoneDict, Costume_DictionaryUpdate);
	resDictRemoveEventCallback(g_hCostumeGeometryDict, Costume_DictionaryUpdate);
	resDictRemoveEventCallback(g_hCostumeMaterialDict, Costume_DictionaryUpdate);
	resDictRemoveEventCallback(g_hCostumeTextureDict, Costume_DictionaryUpdate);
	gclMicroTrans_RemoveCostumeListChangedHandler(MTCostume_Update);
}

// Both a command and an expr func
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CharacterCreation.InitCostume") ACMD_HIDE;
void CharacterCreation_InitCostume(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_InitCostume");
void CharacterCreation_InitCostume(void)
{
	Entity *pEnt = entActivePlayerPtr();
	COSTUME_UI_TRACE_FUNC();
	if (pEnt)
	{
		if ( guild_WithGuild(pEnt) )
		{
			ServerCmd_Guild_RequestUniforms();
		}
	}
	Costume_Enter();
}

// Both a command and an expr func
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CharacterCreation.ResetCostume") ACMD_HIDE;
void CharacterCreation_ResetCostume(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_ResetCostume");
void CharacterCreation_ResetCostume(void)
{
	COSTUME_UI_TRACE_FUNC();
	Costume_Leave();
}

// Both a command and an expr func
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CharacterCreation.ClearCostume") ACMD_HIDE;
void CharacterCreation_ClearCostume(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_ClearCostume");
void CharacterCreation_ClearCostume(void)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeUI_ClearCostume();
}

void CharacterCreation_SetCostumePtr(PlayerCostume* pCostume)
{
    SpeciesDef *speciesDef = GET_REF(g_pFakePlayer->pChar->hSpecies);
	CharacterCreation_ResetCostume();
	CostumeUI_SetCostume(CONTAINER_NOCONST(PlayerCostume, pCostume));
	SET_HANDLE_FROM_REFERENT(g_hSpeciesDict, speciesDef, g_CostumeEditState.hSpecies);
	Costume_Enter();
}

// Both a command and an expr func
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CharacterCreation.SetCostume") ACMD_HIDE;
void CharacterCreation_SetCostume( const char* pchCostumeDef );

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SetCostume");
void CharacterCreation_SetCostume(const char* pchCostumeDef)
{
	PlayerCostume* pCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchCostumeDef);
	COSTUME_UI_TRACE_FUNC();
	CharacterCreation_SetCostumePtr(pCostume);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SetCostumeFromDefaults");
bool CharacterCreation_SetCostumeFromDefaults()
{
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	int i;
	COSTUME_UI_TRACE_FUNC();
	for (i = 0; i < eaSize(&g_CostumeEditState.eaDefaultCostumes); i++)
	{
		PlayerCostume *pCostume = GET_REF(g_CostumeEditState.eaDefaultCostumes[i]->hCostume);
		if (pCostume && pSkel && pSkel == GET_REF(pCostume->hSkeleton))
		{
			StructDestroySafe(parse_PlayerCostume, &g_CostumeEditState.pConstCostume);
			g_CostumeEditState.pConstCostume = StructClone(parse_PlayerCostume, pCostume);
			g_CostumeEditState.pCostume->eCostumeType = kPCCostumeType_Player;
			CostumeUI_RegenCostume(true);
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SetCostumeFromPath");
void CharacterCreation_SetCostumeFromPath(CharacterPath *pPath)
{
	COSTUME_UI_TRACE_FUNC();
	if (pPath)
	{
		PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
		int i;
		for (i = 0; i < eaSize(&pPath->eaCostumeRefs); i++)
		{
			PlayerCostume *pCostume = GET_REF(pPath->eaCostumeRefs[i]->hCostume);
			if (pCostume && pSkel && pSkel == GET_REF(pCostume->hSkeleton))
			{
				StructDestroySafe(parse_PlayerCostume, &g_CostumeEditState.pConstCostume);
				g_CostumeEditState.pConstCostume = StructClone(parse_PlayerCostume, pCostume);
				g_CostumeEditState.pCostume->eCostumeType = kPCCostumeType_Player;
				CostumeUI_RegenCostume(true);
				break;
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SetCostumeFromPathName");
void CharacterCreation_SetCostumeFromPathName(const char *pchPathName)
{
	CharacterPath *pPath = RefSystem_ReferentFromString(g_hCharacterPathDict, pchPathName);
	COSTUME_UI_TRACE_FUNC();
	CharacterCreation_SetCostumeFromPath(pPath);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SetCostumeFromRandomPath");
void CharacterCreation_SetCostumeFromRandomPath(bool bRestrictLockedPaths)
{
	static CharacterPath **eaPaths = NULL;
	Entity *pEnt = (Entity*)g_pFakePlayer;
	COSTUME_UI_TRACE_FUNC();

	if(GSM_IsStateActiveOrPending(GCL_LOGIN_USER_CHOOSING_EXISTING))
		pEnt = (Entity*)gclLoginGetChosenEntity();

	if(pEnt)
	{
		CharacterPath *pPath;
		RefDictIterator iter;
		eaClearFast(&eaPaths);
		RefSystem_InitRefDictIterator("CharacterPath", &iter);
		while (pPath = RefSystem_GetNextReferentFromIterator(&iter))
		{
			if (!bRestrictLockedPaths || Entity_EvalCharacterPathRequiresExpr(pEnt, pPath))
				eaPush(&eaPaths, pPath);
		}
		CharacterCreation_SetCostumeFromPath(eaGet(&eaPaths, randomIntRange(0, eaSize(&eaPaths)-1)));
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_ResetCostumeSkinColorFromDefaultCostume");
void CostumeCreator_ResetCostumeSkinColorFromDefaultCostume( const char* pchCostumeDef )
{
	COSTUME_UI_TRACE_FUNC();
	CostumeUI_ResetCostumeSkinColor(g_CostumeEditState.pCostume, RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchCostumeDef));
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_ResetCostumeSkinColorFromCostume");
void CostumeCreator_ResetCostumeSkinColorFromCostume( int /*PCCostumeStorageType*/ eCostumeType, int iPetNum, int iCostumeIndex )
{
	PlayerCostumeSlot *pCostumeSlot;
	COSTUME_UI_TRACE_FUNC();

	if (CostumeCreator_GetStoreCostumeSlotFromPet(eCostumeType, iPetNum, iCostumeIndex, NULL, &pCostumeSlot))
	{
		CostumeUI_ResetCostumeSkinColor(g_CostumeEditState.pCostume, SAFE_MEMBER(pCostumeSlot, pCostume));
	}
}

// Generate a random morphology with the currently selected skeleton
AUTO_COMMAND ACMD_NAME("CostumeCreator.ResetHeight") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_ResetHeight(const char* pchCostumeDef);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_ResetHeight");
void CostumeCreator_ResetHeight(const char* pchCostumeDef)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeUI_ResetCostumeHeight(g_CostumeEditState.pCostume, RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchCostumeDef));
}

// Generate a random morphology with the currently selected skeleton
AUTO_COMMAND ACMD_NAME("CostumeCreator.ResetMuscle") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_ResetMuscle(const char* pchCostumeDef);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_ResetMuscle");
void CostumeCreator_ResetMuscle(const char* pchCostumeDef)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeUI_ResetCostumeMuscle(g_CostumeEditState.pCostume, RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchCostumeDef));
}

// Generate a random morphology with the currently selected skeleton
AUTO_COMMAND ACMD_NAME("CostumeCreator.ResetBodyScales") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_ResetBodyScales(const char* pchBodyScaleName, const char* pchCostumeDef);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_ResetBodyScales");
void CostumeCreator_ResetBodyScales(const char* pchBodyScaleName, const char* pchCostumeDef)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeUI_ResetCostumeBodyScales(pchBodyScaleName, g_CostumeEditState.pCostume, RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchCostumeDef));
}

// Generate a random morphology with the currently selected skeleton
AUTO_COMMAND ACMD_NAME("CostumeCreator.ResetStance") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void CostumeCreator_ResetStance(const char* pchCostumeDef);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_ResetStance");
void CostumeCreator_ResetStance(const char* pchCostumeDef)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeUI_ResetCostumeStance(g_CostumeEditState.pCostume, RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchCostumeDef));
}

static bool s_bCostumeStance = false;

static void CharacterCreation_UpdateCostumeStanceBit(void)
{
	char *pchBits = NULL;
	char *pchNewBits = NULL;
	char **ppchSetBits = NULL;
	char *pchToken, *pchContext;
	bool bSet = false;
	S32 i;

	if (!g_pCostumeView) {
		return;
	}

	if (g_pCostumeView->costume.pcBits) {
		estrCopy2(&pchBits, g_pCostumeView->costume.pcBits);
	}

	if (pchBits && (pchToken = strtok_r(pchBits, " \r\n\t", &pchContext))) {
		do {
			if (!stricmp(pchToken, "COSTUME") && s_bCostumeStance) {
				eaPush(&ppchSetBits, pchToken);
				bSet = true;
			} else if (stricmp(pchToken, "COSTUME")) {
				eaPush(&ppchSetBits, pchToken);
			}
		} while (pchToken = strtok_r(NULL, " \r\n\t", &pchContext));
	}

	if (s_bCostumeStance && !bSet) {
		eaPush(&ppchSetBits, "COSTUME");
	}

	for (i = 0; i < eaSize(&ppchSetBits); i++) {
		estrAppend2(&pchNewBits, ppchSetBits[i]);
		if (i < eaSize(&ppchSetBits) - 1) {
			estrConcatChar(&pchNewBits, ' ');
		}
	}

	g_pCostumeView->costume.pcBits = pchNewBits && *pchNewBits ? allocAddString(pchNewBits) : NULL;
	estrDestroy(&pchNewBits);
	estrDestroy(&pchBits);
}

// Both a command and an expr func
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CharacterCreation.ForceCostumeStance") ACMD_HIDE;
void CharacterCreation_ForceCostumeStance(int flag);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_ForceCostumeStance");
void CharacterCreation_ForceCostumeStance(int flag)
{
	COSTUME_UI_TRACE_FUNC();
	if (!g_pCostumeView) {
		return;
	}

	s_bCostumeStance = !!flag;
	CharacterCreation_UpdateCostumeStanceBit();
	CostumeUI_RegenCostumeEx(false, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_ForceCostumeStanceBits");
void CharacterCreation_ForceCostumeStanceBits(const char *bits)
{
	static char sBits[256];
	COSTUME_UI_TRACE_FUNC();
	if (!g_pCostumeView) {
		return;
	}
	if ((!bits) || !*bits)
	{
		g_pCostumeView->costume.pcBits = NULL; // Causes normal stance to show
	}
	else
	{
		*sBits = '\0';
		strncat(sBits, bits, 255);
		sBits[255] = '\0';
		g_pCostumeView->costume.pcBits = sBits; // Forces a special animation when in the tailor
	}
	CharacterCreation_UpdateCostumeStanceBit();
	CostumeUI_RegenCostumeEx(false, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_ForceAnimation");
void CharacterCreation_ForceAnimation(const char *pcBits, const char *pcAnimStanceWords, const char *pcAnimKeyword, const char *pcAnimMove)
{
	COSTUME_UI_TRACE_FUNC();
	g_pCostumeView->costume.pcBits = strlen(pcBits) ? allocAddString(pcBits) : NULL;
	g_pCostumeView->costume.pcAnimStanceWords = strlen(pcAnimStanceWords) ? allocAddString(pcAnimStanceWords) : NULL;
	g_pCostumeView->costume.pcAnimKeyword = strlen(pcAnimKeyword) ? allocAddString(pcAnimKeyword) : NULL;
	g_pCostumeView->costume.pcAnimMove = strlen(pcAnimMove) ? allocAddString(pcAnimMove) : NULL;
	g_pCostumeView->costume.bNeedsResetToDefault = true; //might want to make this part of the expression in the future
	CharacterCreation_UpdateCostumeStanceBit();
	CostumeUI_RegenCostumeEx(false, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_IsCostumeStance");
bool CharacterCreation_IsCostumeStance()
{
	COSTUME_UI_TRACE_FUNC();
	return s_bCostumeStance;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetUnlocks);
void CostumeCreator_SetUnlocks(SA_PARAM_OP_VALID Entity *pEnt)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeUI_SetUnlockedCostumes(true, true, pEnt, pEnt);
}

void CosutmeUI_GameAccountDictChanged(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	COSTUME_UI_TRACE_FUNC();
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {
		// If game account data is modified or added, update unlock info
		// This pre-caches and also keeps client up to date with changes to account data
		CostumeCreator_SetUnlocks(entActivePlayerPtr());
		g_CostumeEditState.bUpdateLists = true;
	}
}

AUTO_RUN;
void CostumeCreation_AutoRegister(void)
{
	ui_GenInitPointerVar("CostumeState", parse_CostumeEditState);
	ui_GenInitStaticDefineVars(PCColorLinkEnum, "CostumeColorLink_");
	ui_GenInitStaticDefineVars(PCControlledRandomLockEnum, "CostumeLock_");
	ui_GenInitStaticDefineVars(CostumeLockCheckStateEnum, "CostumeLockState_");
	ui_GenInitStaticDefineVars(CostumeEditLineTypeEnum, "CostumeEditLineType_");
	ui_GenInitStaticDefineVars(PCEditColorEnum, "PCEditColor_");
	ui_GenInitStaticDefineVars(PCColorFlagsEnum, "PCColorFlag_");
	ui_GenInitStaticDefineVars(PCPaymentMethodEnum, "PCPay_");
	ui_GenInitStaticDefineVars(PCCostumeStorageTypeEnum, "CostumeType_");

	// Hacky id's used by GetTexName & GetTexSysName
	ui_GenInitIntVar("CostumeTexture_Pattern", 1);
	ui_GenInitIntVar("CostumeTexture_Detail", 2);
	ui_GenInitIntVar("CostumeTexture_Specular", 3);
	ui_GenInitIntVar("CostumeTexture_Diffuse", 4);
	ui_GenInitIntVar("CostumeTexture_Movable", 5);

	// Hacky id's used by CostumeCreator_GetEditBodyScale, CostumeCreator_GetEditBoneScale & CostumeCreator_GetEditBoneScaleByData
	ui_GenInitIntVar("CostumeScale_Current", 0);
	ui_GenInitIntVar("CostumeScale_Min", 1);
	ui_GenInitIntVar("CostumeScale_Max", 2);
}

// Generate a random costume that obeys the currently set locks, optionally will not include microtransacted costumes
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_MTControlledRandomCostume");
void CostumeCreator_MTControlledRandomCostume(bool bExcludeMicroTransactionCostumes)
{
	Entity *pEnt = entActivePlayerPtr();
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCStyle **eaRandomStyles = NULL;
	int i, j;
	COSTUME_UI_TRACE_FUNC();
	costumeRandom_SetRandomTable(g_CostumeEditState.pRandTable);

	if (eaSize(&g_CostumeEditState.eaStyles) && eaSize(&g_CostumeEditState.eaRandomStyles)) {
		eaCreate(&eaRandomStyles);
		eaIndexedEnable(&eaRandomStyles, parse_PCStyle);
		for (i = eaSize(&g_CostumeEditState.eaRandomStyles) - 1; i >= 0; i--) {
			for (j = eaSize(&g_CostumeEditState.eaStyles) - 1; j >= 0; j--) {
				if (g_CostumeEditState.eaStyles[j]->pcName == g_CostumeEditState.eaRandomStyles[i]) {
					eaPush(&eaRandomStyles, g_CostumeEditState.eaStyles[j]);
					break;
				}
			}
		}
		if (!eaSize(&eaRandomStyles)) {
			eaDestroy(&eaRandomStyles);
		}
	}

	costumeRandom_ControlledRandomParts(g_CostumeEditState.pCostume, pSpecies, guild_GetGuild(pEnt), bExcludeMicroTransactionCostumes ? g_CostumeEditState.eaOwnedUnlockedCostumes : g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, g_CostumeEditState.eaLockedRegions, g_CostumeEditState.eaPowerFXBones, eaRandomStyles, true, true, true, g_CostumeEditState.bUnlockAll);
	costumeRandom_ControlledRandomColors(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, bExcludeMicroTransactionCostumes ? g_CostumeEditState.eaOwnedUnlockedCostumes : g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eSharedColorLocks, true, true, true, g_CostumeEditState.bUnlockAll);
	costumeTailor_FillAllBones(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, true, false, true);

	if (s_iRandomizerTest > 0)
	{
		char *estrResult = NULL;
		estrStackCreate(&estrResult);
		for (i = 0; i < s_iRandomizerTest; i++)
		{
			NOCONST(PlayerCostume) *pSrcCostume;
			NOCONST(PlayerCostume) *validate;
			bool bValid;
			U32 uRandomSeed = randomU32();

			// Track initial configuration
			seedMersenneTable(g_CostumeEditState.pRandTable, uRandomSeed);
			pSrcCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);

			costumeRandom_ControlledRandomParts(g_CostumeEditState.pCostume, pSpecies, guild_GetGuild(pEnt), bExcludeMicroTransactionCostumes ? g_CostumeEditState.eaOwnedUnlockedCostumes : g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, g_CostumeEditState.eaLockedRegions, g_CostumeEditState.eaPowerFXBones, eaRandomStyles, true, true, true, g_CostumeEditState.bUnlockAll);
			costumeRandom_ControlledRandomColors(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, bExcludeMicroTransactionCostumes ? g_CostumeEditState.eaOwnedUnlockedCostumes : g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eSharedColorLocks, true, true, true, g_CostumeEditState.bUnlockAll);
			costumeTailor_FillAllBones(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, true, false, true);

			validate = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
			costumeTailor_StripUnnecessary(validate);
			bValid = costumeValidate_ValidatePlayerCreated((PlayerCostume *)validate, pSpecies, g_CostumeEditState.pSlotType, entActivePlayerPtr(), CostumeUI_GetSourceEnt(), &estrResult, NULL, bExcludeMicroTransactionCostumes ? g_CostumeEditState.eaOwnedUnlockedCostumes : g_CostumeEditState.eaUnlockedCostumes, false);
			StructDestroyNoConst(parse_PlayerCostume, validate);

			if (!bValid) {
				printf("Validation failure: %s\n", estrResult);

				// Re-run the randomization pass with initial configuration
				seedMersenneTable(g_CostumeEditState.pRandTable, uRandomSeed);
				costumeRandom_ControlledRandomParts(pSrcCostume, pSpecies, guild_GetGuild(pEnt), bExcludeMicroTransactionCostumes ? g_CostumeEditState.eaOwnedUnlockedCostumes : g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, g_CostumeEditState.eaLockedRegions, g_CostumeEditState.eaPowerFXBones, eaRandomStyles, true, true, true, g_CostumeEditState.bUnlockAll);
				costumeRandom_ControlledRandomColors(pSrcCostume, pSpecies, g_CostumeEditState.pSlotType, bExcludeMicroTransactionCostumes ? g_CostumeEditState.eaOwnedUnlockedCostumes : g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eSharedColorLocks, true, true, true, g_CostumeEditState.bUnlockAll);
				costumeTailor_FillAllBones(pSrcCostume, pSpecies, g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, true, false, true);

				StructDestroyNoConst(parse_PlayerCostume, pSrcCostume);
				break;
			}

			StructDestroyNoConst(parse_PlayerCostume, pSrcCostume);
		}
		printf("Validated %d costumes\n", i);
		estrDestroy(&estrResult);
	}

	eaDestroy(&eaRandomStyles);

	costumeRandom_SetRandomTable(NULL);

	CostumeUI_ClearSelections();
	CostumeUI_RegenCostumeEx(true, true);
}

// Generate a random costume that obeys the currently set locks
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_ControlledRandomCostume");
void CostumeCreator_ControlledRandomCostume(void)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeCreator_MTControlledRandomCostume(false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_AddRandomStyle");
bool CostumeCreator_AddRandomStyle(const char *pchStyle)
{
	int i;
	COSTUME_UI_TRACE_FUNC();
	for (i = eaSize(&g_CostumeEditState.eaRandomStyles) - 1; i >= 0; i--) {
		if (!stricmp(g_CostumeEditState.eaRandomStyles[i], pchStyle)) {
			return false;
		}
	}
	for (i = eaSize(&g_CostumeEditState.eaStyles) - 1; i >= 0; i--) {
		if (!stricmp(g_CostumeEditState.eaStyles[i]->pcName, pchStyle)) {
			eaPush(&g_CostumeEditState.eaRandomStyles, g_CostumeEditState.eaStyles[i]->pcName);
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_RemoveRandomStyle");
bool CostumeCreator_RemoveRandomStyle(const char *pchStyle)
{
	int i;
	COSTUME_UI_TRACE_FUNC();
	for (i = eaSize(&g_CostumeEditState.eaRandomStyles) - 1; i >= 0; i--) {
		if (!stricmp(g_CostumeEditState.eaRandomStyles[i], pchStyle)) {
			eaRemove(&g_CostumeEditState.eaRandomStyles, i);
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_IsStyleEnabled");
bool CostumeCreator_IsStyleEnabled(const char *pchStyle)
{
	int i;
	COSTUME_UI_TRACE_FUNC();
	for (i = eaSize(&g_CostumeEditState.eaRandomStyles) - 1; i >= 0; i--) {
		if (!stricmp(g_CostumeEditState.eaRandomStyles[i], pchStyle)) {
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_ClearRandomStyles");
void CostumeCreator_ClearRandomStyles(void)
{
	COSTUME_UI_TRACE_FUNC();
	eaClear(&g_CostumeEditState.eaRandomStyles);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_GetEnabledStyles);
void CostumeCreator_GetEnabledStyles(SA_PARAM_NN_VALID UIGen *pGen)
{
	PCStyle ***peaStyles = ui_GenGetManagedListSafe(pGen, PCStyle);
	S32 i, iUsed = 0;
	COSTUME_UI_TRACE_FUNC();
	for (i = 0; i < eaSize(&g_CostumeEditState.eaRandomStyles); i++) {
		PCStyle *pStyle = RefSystem_ReferentFromString(g_hCostumeStyleDict, g_CostumeEditState.eaRandomStyles[i]);
		if (pStyle) {
			while (eaSize(peaStyles) <= iUsed)
				eaPush(peaStyles, NULL);
			(*peaStyles)[iUsed++] = pStyle;
		}
	}
	eaSetSize(peaStyles, iUsed);
	ui_GenSetManagedListSafe(pGen, peaStyles, PCStyle, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_GetEnabledStylesSize);
S32 CostumeCreator_GetEnabledStylesSize(void)
{
	COSTUME_UI_TRACE_FUNC();
	return eaSize(&g_CostumeEditState.eaRandomStyles);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_LockBoneToggle");
void CostumeCreator_LockBoneToggle(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID PCBoneDef *pBone)
{
	bool bToggle = false;
	PCBoneDef *pMirrorBone = SAFE_GET_REF(pBone, hMirrorBone);
	NOCONST(PCPart) *pPart = pBone && g_CostumeEditState.pCostume ? costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL) : NULL;
	NOCONST(PCPart) *pMirrorPart = pMirrorBone && g_CostumeEditState.pCostume ? costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pMirrorBone, NULL) : NULL;
	PCControlledRandomLock eThisLocks = pPart ? pPart->eControlledRandomLocks : 0;
	PCControlledRandomLock eMirrorLocks = pMirrorPart ? pMirrorPart->eControlledRandomLocks : 0;
	COSTUME_UI_TRACE_FUNC();

	bToggle = !((!pMirrorPart || eMirrorLocks == kPCControlledRandomLock_AllStyle) && eThisLocks == kPCControlledRandomLock_AllStyle);

	if (!pPart) {
		return;
	}

	pPart->eControlledRandomLocks = bToggle ? kPCControlledRandomLock_AllStyle : 0;
	if (bToggle) {
		pPart->eColorLink = kPCColorLink_None;
	}
	if (pMirrorPart) {
		pMirrorPart->eControlledRandomLocks = bToggle ? kPCControlledRandomLock_AllStyle : 0;
		if (bToggle) {
			pMirrorPart->eColorLink = kPCColorLink_None;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_LockBoneCheck");
int CostumeCreator_LockBoneCheck(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID PCBoneDef *pBone)
{
	PCBoneDef *pMirrorBone = SAFE_GET_REF(pBone, hMirrorBone);
	NOCONST(PCPart) *pPart = pBone && g_CostumeEditState.pCostume ? costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL) : NULL;
	NOCONST(PCPart) *pMirrorPart = pMirrorBone && g_CostumeEditState.pCostume ? costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pMirrorBone, NULL) : NULL;
	PCControlledRandomLock eThisLocks = pPart ? pPart->eControlledRandomLocks : 0;
	PCControlledRandomLock eMirrorLocks = pMirrorPart ? pMirrorPart->eControlledRandomLocks : 0;
	COSTUME_UI_TRACE_FUNC();

	if (eMirrorLocks == 0 && eThisLocks == 0) {
		return CostumeLockCheckState_Unchecked;
	} else if ((!pMirrorPart || eMirrorLocks == kPCControlledRandomLock_AllStyle) && eThisLocks == kPCControlledRandomLock_AllStyle) {
		return CostumeLockCheckState_Checked;
	} else {
		return CostumeLockCheckState_Partial;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_LockStyleToggle");
void CostumeCreator_LockStyleToggle(SA_PARAM_NN_VALID UIGen *pGen, int eLock)
{
	bool bToggle = false;
	PCBoneDef *pBone = NULL;
	PCBoneDef *pMirrorBone = NULL;
	NOCONST(PCPart) *pMirrorPart = NULL;
	PCControlledRandomLock eMirrorLocks = 0;
	PCControlledRandomLock eThisLocks = g_CostumeEditState.pPart->eControlledRandomLocks & eLock;
	bool bBoth = g_CostumeEditState.pPart->eEditMode == kPCEditMode_Both;
	COSTUME_UI_TRACE_FUNC();

	if (g_MirrorSelectMode && bBoth) {
		pBone = GET_REF(g_CostumeEditState.hBone);
		pMirrorBone = SAFE_GET_REF(pBone, hMirrorBone);
		pMirrorPart = pMirrorBone && g_CostumeEditState.pCostume ? costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pMirrorBone, NULL) : NULL;
		eMirrorLocks = (pMirrorPart ? pMirrorPart->eControlledRandomLocks : 0) & eLock;
		if ((!pMirrorPart || eMirrorLocks == eLock) && eThisLocks == eLock) {
			bToggle = false;
		} else {
			bToggle = true;
		}
	} else {
		bToggle = eThisLocks != eLock;
	}

	g_CostumeEditState.pPart->eControlledRandomLocks = !bToggle ?
		(g_CostumeEditState.pPart->eControlledRandomLocks & ~eLock) :
		(g_CostumeEditState.pPart->eControlledRandomLocks | eLock);
	if (pMirrorPart) {
		pMirrorPart->eControlledRandomLocks = !bToggle ?
			(pMirrorPart->eControlledRandomLocks & ~eLock) :
			(pMirrorPart->eControlledRandomLocks | eLock);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_LockStyleCheck");
int CostumeCreator_LockStyleCheck(SA_PARAM_NN_VALID UIGen *pGen, int eLock)
{
	PCBoneDef *pBone = NULL;
	PCBoneDef *pMirrorBone = NULL;
	NOCONST(PCPart) *pMirrorPart = NULL;
	PCControlledRandomLock eMirrorLocks = 0;
	PCControlledRandomLock eThisLocks = g_CostumeEditState.pPart ? (g_CostumeEditState.pPart->eControlledRandomLocks & eLock) : 0;
	bool bBoth = g_CostumeEditState.pPart ? g_CostumeEditState.pPart->eEditMode == kPCEditMode_Both : false;
	COSTUME_UI_TRACE_FUNC();

	if (g_MirrorSelectMode && bBoth) {
		pBone = GET_REF(g_CostumeEditState.hBone);
		pMirrorBone = SAFE_GET_REF(pBone, hMirrorBone);
		pMirrorPart = pMirrorBone && g_CostumeEditState.pCostume ? costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pMirrorBone, NULL) : NULL;
		eMirrorLocks = (pMirrorPart ? pMirrorPart->eControlledRandomLocks : 0) & eLock;
		if (eMirrorLocks == 0 && eThisLocks == 0) {
			return CostumeLockCheckState_Unchecked;
		} else if ((!pMirrorPart || eMirrorLocks == eLock) && eThisLocks == eLock) {
			return CostumeLockCheckState_Checked;
		} else {
			return CostumeLockCheckState_Partial;
		}
	} else {
		return eThisLocks == 0 ? CostumeLockCheckState_Unchecked :
				eThisLocks == eLock ? CostumeLockCheckState_Checked :
				CostumeLockCheckState_Partial;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_LockSharedColorToggle");
void CostumeCreator_LockSharedColorToggle(SA_PARAM_NN_VALID UIGen *pGen, int eLock)
{
	COSTUME_UI_TRACE_FUNC();
	g_CostumeEditState.eSharedColorLocks = (g_CostumeEditState.eSharedColorLocks & eLock) == eLock
		? (g_CostumeEditState.eSharedColorLocks & ~eLock)
		: (g_CostumeEditState.eSharedColorLocks | eLock);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_LockSharedColorCheck");
int CostumeCreator_LockSharedColorCheck(SA_PARAM_NN_VALID UIGen *pGen, int eLock)
{
	COSTUME_UI_TRACE_FUNC();
	return (g_CostumeEditState.eSharedColorLocks & eLock) == eLock ? CostumeLockCheckState_Checked :
		(g_CostumeEditState.eSharedColorLocks & eLock) ? CostumeLockCheckState_Partial : CostumeLockCheckState_Unchecked;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_LockCategoryToggle");
void CostumeCreator_LockCategoryToggle(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID PCRegion *pRegion)
{
	int j;
	COSTUME_UI_TRACE_FUNC();

	for (j = eaSize(&g_CostumeEditState.eaLockedRegions) - 1; j >= 0; j--)
	{
		if (g_CostumeEditState.eaLockedRegions[j] == pRegion)
		{
			eaRemove(&g_CostumeEditState.eaLockedRegions, j);
			break;
		}
	}

	if (j < 0 && pRegion)
	{
		eaPush(&g_CostumeEditState.eaLockedRegions, pRegion);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_LockCategoryCheck");
int CostumeCreator_LockCategoryCheck(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID PCRegion *pRegion)
{
	int j;
	COSTUME_UI_TRACE_FUNC();

	for (j = eaSize(&g_CostumeEditState.eaLockedRegions) - 1; j >= 0; j--)
	{
		if (g_CostumeEditState.eaLockedRegions[j] == pRegion)
		{
			return CostumeLockCheckState_Checked;
		}
	}

	return CostumeLockCheckState_Unchecked;
}

static bool CostumeCreator_IsPartCommon(NOCONST(PCPart) *pPart, U8 color0[4], U8 color1[4], U8 color2[4], U8 color3[4], U8 glowScale[4])
{
	NOCONST(PCPart) *pSharedColorPart = CostumeUI_GetSharedColorCostumePart();

	// Check for a unique color mismatch
	if (pSharedColorPart) {
		U8 zeroColor[4] = { 0,0,0,0 };
		U8 *partGlowScale = zeroColor;
		if (pSharedColorPart->pCustomColors) {
			partGlowScale = pSharedColorPart->pCustomColors->glowScale;
		}
		if (!IS_SAME_COSTUME_COLOR(pSharedColorPart->color0, color0) ||
			!IS_SAME_COSTUME_COLOR(pSharedColorPart->color1, color1) ||
			!IS_SAME_COSTUME_COLOR(pSharedColorPart->color2, color2) ||
			!IS_SAME_COSTUME_COLOR(pSharedColorPart->color3, color3) ||
			!IS_SAME_COSTUME_COLOR(partGlowScale, glowScale)) {
			return false;
		}
	} else {
		if (!IS_SAME_COSTUME_COLOR(g_CostumeEditState.sharedColor0.color, color0) ||
			!IS_SAME_COSTUME_COLOR(g_CostumeEditState.sharedColor1.color, color1) ||
			!IS_SAME_COSTUME_COLOR(g_CostumeEditState.sharedColor2.color, color2) ||
			!IS_SAME_COSTUME_COLOR(g_CostumeEditState.sharedColor3.color, color3) ||
			!IS_SAME_COSTUME_COLOR(g_CostumeEditState.sharedGlowScale, glowScale)) {
			return false;
		}
	}

	// Check for a lock on a color
	if ((g_CostumeEditState.pPart->eControlledRandomLocks & kPCControlledRandomLock_Colors)) {
		return false;
	}

	// Part should share colors
	return true;
}

// Update the color link status for both parts independently.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetPartColorLinkIfCommon");
void CostumeCreator_SetPartColorLinkIfCommon(SA_PARAM_NN_VALID UIGen *pGen, int iColorID, bool bCheckColor, F32 fR, F32 fG, F32 fB, bool bCheckGlow, U8 ubGlowScale)
{
	PCBoneDef *pBone = NULL;
	PCBoneDef *pMirrorBone = NULL;
	NOCONST(PCPart) *pMirrorPart = NULL;
	NOCONST(PCPart) *pRealPart = NULL;
	bool bBoth = g_CostumeEditState.pPart->eEditMode == kPCEditMode_Both;
	bool bOnDifferentColors = false;
	bool bOnLockSet = false;
	bool bSetLinked;
	U8 newColor[4] = { (U8)fR, (U8)fG, (U8)fB, 255 };
	U8 glowScale[4] = { 0,0,0,0 };
	TailorWeaponStance *pStance = RefSystem_ReferentFromString(g_hWeaponStanceDict, REF_STRING_FROM_HANDLE(g_CostumeEditState.pPart->hBoneDef));
	COSTUME_UI_TRACE_FUNC();

	if (iColorID < 0) {
		return;
	}

	if (g_CostumeEditState.pPart->pCustomColors) {
		COPY_COSTUME_COLOR(g_CostumeEditState.pPart->pCustomColors->glowScale, glowScale);
	}
	if (120 <= iColorID && iColorID <= 123 && bCheckGlow) {
		glowScale[iColorID - 120] = ubGlowScale;
	}
	// Check for a unique color mismatch
	bSetLinked = CostumeCreator_IsPartCommon(g_CostumeEditState.pPart,
												(iColorID == 120 && bCheckColor) ? newColor : g_CostumeEditState.pPart->color0,
												(iColorID == 121 && bCheckColor) ? newColor : g_CostumeEditState.pPart->color1,
												(iColorID == 122 && bCheckColor) ? newColor : g_CostumeEditState.pPart->color2,
												(iColorID == 123 && bCheckColor) ? newColor : g_CostumeEditState.pPart->color3,
												glowScale);

	// Set/unset shared?
	if (!bSetLinked && g_CostumeEditState.pPart->eColorLink == kPCColorLink_All) {
		g_CostumeEditState.pPart->eColorLink = kPCColorLink_None;
		CostumeUI_SetColorsFromWeaponStance(pStance, g_CostumeEditState.pPart);
	} else if (bSetLinked && g_CostumeEditState.pPart->eColorLink == kPCColorLink_None) {
		g_CostumeEditState.pPart->eColorLink = kPCColorLink_All;
	}

	// Handle mirror
	if (g_MirrorSelectMode && bBoth) {
		pBone = GET_REF(g_CostumeEditState.hBone);
		pMirrorBone = SAFE_GET_REF(pBone, hMirrorBone);
		pMirrorPart = pMirrorBone && g_CostumeEditState.pCostume ? costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pMirrorBone, NULL) : NULL;
		if (pMirrorPart) {
			if (pMirrorPart->pCustomColors) {
				COPY_COSTUME_COLOR(pMirrorPart->pCustomColors->glowScale, glowScale);
			}
			if (120 <= iColorID && iColorID <= 123 && bCheckGlow) {
				glowScale[iColorID - 120] = ubGlowScale;
			}
			// Update mirror part share state
			// this is why it can't be done in a gen expression,
			// since the gens can't individually edit mirrored bones :(
			bSetLinked = CostumeCreator_IsPartCommon(pMirrorPart,
														(iColorID == 120 && bCheckColor) ? newColor : pMirrorPart->color0,
														(iColorID == 121 && bCheckColor) ? newColor : pMirrorPart->color1,
														(iColorID == 122 && bCheckColor) ? newColor : pMirrorPart->color2,
														(iColorID == 123 && bCheckColor) ? newColor : pMirrorPart->color3,
														glowScale);
			if (!bSetLinked && pMirrorPart->eColorLink == kPCColorLink_All) {
				pMirrorPart->eColorLink = kPCColorLink_None;
				CostumeUI_SetColorsFromWeaponStance(pStance, pMirrorPart);
			} else if (bSetLinked && pMirrorPart->eColorLink == kPCColorLink_None) {
				pMirrorPart->eColorLink = kPCColorLink_All;
			}
		}
	}

	// Handle bone groups
	if (g_GroupSelectMode)
	{
		pBone = GET_REF(g_CostumeEditState.hBone);
		if (pBone)
		{
			pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL);
			if (pRealPart && pRealPart->iBoneGroupIndex >= 0) {
				NOCONST(PCPart) *pGroupPart = NULL;
				PCSkeletonDef *skel = GET_REF(g_CostumeEditState.hSkeleton);
				int i;

				if (skel)
				{
					PCBoneGroup **bg = skel->eaBoneGroups;
					if (bg)
					{
						for (i = eaSize(&bg[pRealPart->iBoneGroupIndex]->eaBoneInGroup)-1; i >= 0; --i)
						{
							pGroupPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(bg[pRealPart->iBoneGroupIndex]->eaBoneInGroup[i]->hBone), NULL);
							if (pGroupPart) {
								if (pGroupPart->pCustomColors) {
									COPY_COSTUME_COLOR(pGroupPart->pCustomColors->glowScale, glowScale);
								}
								if (120 <= iColorID && iColorID <= 123 && bCheckGlow) {
									glowScale[iColorID - 120] = ubGlowScale;
								}
								// Update bone group part share state
								// this is why it can't be done in a gen expression,
								// since the gens can't individually edit grouped bones :(
								bSetLinked = CostumeCreator_IsPartCommon(pGroupPart,
									(iColorID == 120 && bCheckColor) ? newColor : pGroupPart->color0,
									(iColorID == 121 && bCheckColor) ? newColor : pGroupPart->color1,
									(iColorID == 122 && bCheckColor) ? newColor : pGroupPart->color2,
									(iColorID == 123 && bCheckColor) ? newColor : pGroupPart->color3,
									glowScale);
								if (!bSetLinked && pGroupPart->eColorLink == kPCColorLink_All) {
									pGroupPart->eColorLink = kPCColorLink_None;
									CostumeUI_SetColorsFromWeaponStance(pStance, pGroupPart);
								} else if (bSetLinked && pGroupPart->eColorLink == kPCColorLink_None) {
									pGroupPart->eColorLink = kPCColorLink_All;
								}
							}
						}
					}
				}
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_UniqueColorsRandomize");
void CostumeCreator_UniqueColorsRandomize(SA_PARAM_NN_VALID ExprContext *pContext)
{
	PCBoneDef *pBone = GET_REF(g_CostumeEditState.hBone);
	PCBoneDef *pMirrorBone = NULL;
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	NOCONST(PCPart) *pMirrorPart = NULL;
	bool bBoth = g_CostumeEditState.pPart->eEditMode == kPCEditMode_Both;
	COSTUME_UI_TRACE_FUNC();

	if (!pBone) {
		return;
	}

	if (g_MirrorSelectMode && bBoth) {
		pMirrorBone = SAFE_GET_REF(pBone, hMirrorBone);
		pMirrorPart = pMirrorBone && g_CostumeEditState.pCostume ? costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pMirrorBone, NULL) : NULL;
	}

	// Check color links
	if (pPart->eColorLink != kPCColorLink_None && (!pMirrorPart || pMirrorPart->eColorLink != kPCColorLink_None)) {
		return;
	}

	costumeRandom_ControlledRandomBoneColors(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType, g_CostumeEditState.eaUnlockedCostumes, pBone, !!pMirrorPart, g_GroupSelectMode && pPart->iBoneGroupIndex >= 0, true, g_CostumeEditState.bUnlockAll);

	CostumeUI_RegenCostume(true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_UniqueColorsShuffle");
void CostumeCreator_UniqueColorsShuffle(SA_PARAM_NN_VALID ExprContext *pContext)
{
	ColorPermutationData permutationData[4];
	U8 colors[4][4];
	int iColors = 0;
	int i;
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	PCBoneDef *pBone = GET_REF(g_CostumeEditState.hBone);
	PCBoneDef *pMirrorBone = NULL;
	NOCONST(PCPart) *pPart = g_CostumeEditState.pPart;
	NOCONST(PCPart) *pMirrorPart = NULL;
	bool bBoth = g_CostumeEditState.pPart->eEditMode == kPCEditMode_Both;
	U8 noglow[4] = {0, 0, 0, 0};
	COSTUME_UI_TRACE_FUNC();

	if (!pBone) {
		return;
	}

	if (g_MirrorSelectMode && bBoth) {
		pMirrorBone = SAFE_GET_REF(pBone, hMirrorBone);
		pMirrorPart = pMirrorBone && g_CostumeEditState.pCostume ? costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pMirrorBone, NULL) : NULL;
	}

	// Check color links
	if (pPart->eColorLink == kPCColorLink_All && (!pMirrorPart || pMirrorPart->eColorLink == kPCColorLink_All)) {
		return;
	}

	CostumeUI_FillPermutationDataFromCostumePart(pPart, permutationData, &iColors);
	CostumeUI_ShuffleColors(permutationData, iColors);

	COPY_COSTUME_COLOR(pPart->color0, colors[0]);
	COPY_COSTUME_COLOR(pPart->color1, colors[1]);
	COPY_COSTUME_COLOR(pPart->color2, colors[2]);
	COPY_COSTUME_COLOR(pPart->color3, colors[3]);

	for (i = 0; i < iColors; i++) {
		switch (permutationData[i].iSlot) {
		xcase 0: COPY_COSTUME_COLOR(permutationData[i].color, colors[0]);
		xcase 1: COPY_COSTUME_COLOR(permutationData[i].color, colors[1]);
		xcase 2: COPY_COSTUME_COLOR(permutationData[i].color, colors[2]);
		xcase 3: COPY_COSTUME_COLOR(permutationData[i].color, colors[3]);
		}
	}

	costumeTailor_SetPartColors(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, pPart, colors[0], colors[1], colors[2], colors[3], noglow);
	if (pMirrorPart) {
		costumeTailor_SetPartColors(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, pMirrorPart, colors[0], colors[1], colors[2], colors[3], noglow);
	}
	if (g_GroupSelectMode && pPart->iBoneGroupIndex >= 0) {
		NOCONST(PCPart) *pGroupPart = NULL;
		PCSkeletonDef *skel = GET_REF(g_CostumeEditState.hSkeleton);

		if (skel)
		{
			PCBoneGroup **bg = skel->eaBoneGroups;
			if (bg && bg[pPart->iBoneGroupIndex])
			{
				if (eaSize(&bg[pPart->iBoneGroupIndex]->eaBoneInGroup) >= 1)
				{
					for (i = eaSize(&bg[pPart->iBoneGroupIndex]->eaBoneInGroup)-1; i >= 0; --i)
					{
						pGroupPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(bg[pPart->iBoneGroupIndex]->eaBoneInGroup[i]->hBone), NULL);
						if (pGroupPart) {
							costumeTailor_SetPartColors(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, pGroupPart, colors[0], colors[1], colors[2], colors[3], noglow);
						}
					}
				}
			}
		}
	}

	CostumeUI_RegenCostume(true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SharedColorsRandomize");
void CostumeCreator_SharedColorsRandomize(SA_PARAM_NN_VALID ExprContext *pContext)
{
	COSTUME_UI_TRACE_FUNC();

	// Just randomize the colors
	costumeRandom_ControlledRandomColors(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType, g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.eSharedColorLocks, true, true, true, g_CostumeEditState.bUnlockAll);

	CostumeUI_RegenCostume(true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SharedColorsShuffle");
void CostumeCreator_SharedColorsShuffle(SA_PARAM_NN_VALID ExprContext *pContext)
{
	ColorPermutationData permutationData[4];
	int iColors = 0;
	int i;
	NOCONST(PCPart) *pSharedPart = CostumeUI_GetSharedColorCostumePart();
	COSTUME_UI_TRACE_FUNC();

	// Copy colors to randomize
	if (pSharedPart) {
		CostumeUI_FillPermutationDataFromCostumePart(pSharedPart, permutationData, &iColors);
		COPY_COSTUME_COLOR(pSharedPart->color0, g_CostumeEditState.sharedColor0.color);
		COPY_COSTUME_COLOR(pSharedPart->color1, g_CostumeEditState.sharedColor1.color);
		COPY_COSTUME_COLOR(pSharedPart->color2, g_CostumeEditState.sharedColor2.color);
		COPY_COSTUME_COLOR(pSharedPart->color3, g_CostumeEditState.sharedColor3.color);
	}
	else
	{
		memset(permutationData, 0, sizeof(permutationData));
		if (!(g_CostumeEditState.eSharedColorLocks & kPCControlledRandomLock_AllColor0)) {
			permutationData[iColors].iSlot = 0;
			COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor0.color, permutationData[iColors].color);
			iColors++;
		}
		if (!(g_CostumeEditState.eSharedColorLocks & kPCControlledRandomLock_AllColor1)) {
			permutationData[iColors].iSlot = 1;
			COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor1.color, permutationData[iColors].color);
			iColors++;
		}
		if (!(g_CostumeEditState.eSharedColorLocks & kPCControlledRandomLock_AllColor2)) {
			permutationData[iColors].iSlot = 2;
			COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor2.color, permutationData[iColors].color);
			iColors++;
		}
		if (!(g_CostumeEditState.eSharedColorLocks & kPCControlledRandomLock_AllColor3)) {
			permutationData[iColors].iSlot = 3;
			COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor3.color, permutationData[iColors].color);
			iColors++;
		}
	}

	CostumeUI_ShuffleColors(permutationData, iColors);

	for (i = 0; i < iColors; i++) {
		switch (permutationData[i].iSlot) {
		xcase 0: COPY_COSTUME_COLOR(permutationData[i].color, g_CostumeEditState.sharedColor0.color);
		xcase 1: COPY_COSTUME_COLOR(permutationData[i].color, g_CostumeEditState.sharedColor1.color);
		xcase 2: COPY_COSTUME_COLOR(permutationData[i].color, g_CostumeEditState.sharedColor2.color);
		xcase 3: COPY_COSTUME_COLOR(permutationData[i].color, g_CostumeEditState.sharedColor3.color);
		}
	}

	if (pSharedPart) {
		for (i = 0; i < eaSize(&g_CostumeEditState.pCostume->eaParts); i++) {
			NOCONST(PCPart) *pPart = g_CostumeEditState.pCostume->eaParts[i];
			if (pPart->eColorLink == kPCColorLink_All) {
				COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor0.color, pPart->color0);
				COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor1.color, pPart->color1);
				COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor2.color, pPart->color2);
				COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor3.color, pPart->color3);
			}
		}
	}

	CostumeUI_RegenCostume(true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_UniqueColorsReset");
void CostumeCreator_UniqueColorsReset(SA_PARAM_NN_VALID ExprContext *pContext)
{
	int i;
	PCBoneDef *pBone = NULL;
	PCBoneDef *pMirrorBone = NULL;
	NOCONST(PCPart) *pMirrorPart = NULL;
	bool bBoth = g_CostumeEditState.pPart->eEditMode == kPCEditMode_Both;
	NOCONST(PCPart) *pSharedColorPart = CostumeUI_GetSharedColorCostumePart();
	COSTUME_UI_TRACE_FUNC();

	if (g_MirrorSelectMode && bBoth) {
		pBone = GET_REF(g_CostumeEditState.hBone);
		pMirrorBone = SAFE_GET_REF(pBone, hMirrorBone);
		pMirrorPart = pMirrorBone && g_CostumeEditState.pCostume ? costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pMirrorBone, NULL) : NULL;
	}

	// Unset the locks
	g_CostumeEditState.pPart->eControlledRandomLocks = (g_CostumeEditState.pPart->eControlledRandomLocks & ~kPCControlledRandomLock_Colors);
	if (pMirrorPart) {
		pMirrorPart->eControlledRandomLocks = (g_CostumeEditState.pPart->eControlledRandomLocks & ~kPCControlledRandomLock_Colors);
	}

	// Set the color link
	g_CostumeEditState.pPart->eColorLink = kPCColorLink_All;
	if (pMirrorPart) {
		pMirrorPart->eColorLink = kPCColorLink_All;
	}

	// Reset the colors
	if (pSharedColorPart) {
		COPY_COSTUME_COLOR(pSharedColorPart->color0, g_CostumeEditState.pPart->color0);
		COPY_COSTUME_COLOR(pSharedColorPart->color1, g_CostumeEditState.pPart->color1);
		COPY_COSTUME_COLOR(pSharedColorPart->color2, g_CostumeEditState.pPart->color2);
		COPY_COSTUME_COLOR(pSharedColorPart->color3, g_CostumeEditState.pPart->color3);
		if (pSharedColorPart->pCustomColors) {
			if (!g_CostumeEditState.pPart->pCustomColors) {
				g_CostumeEditState.pPart->pCustomColors = StructCreateNoConst(parse_PCCustomColorInfo);
			}
			if (g_CostumeEditState.pPart->pCustomColors) {
				COPY_COSTUME_COLOR(pSharedColorPart->pCustomColors->glowScale, g_CostumeEditState.pPart->pCustomColors->glowScale);
			}
		}
		if (pMirrorPart) {
			COPY_COSTUME_COLOR(pSharedColorPart->color0, pMirrorPart->color0);
			COPY_COSTUME_COLOR(pSharedColorPart->color1, pMirrorPart->color1);
			COPY_COSTUME_COLOR(pSharedColorPart->color2, pMirrorPart->color2);
			COPY_COSTUME_COLOR(pSharedColorPart->color3, pMirrorPart->color3);
			if (pSharedColorPart->pCustomColors) {
				if (!pMirrorPart->pCustomColors) {
					pMirrorPart->pCustomColors = StructCreateNoConst(parse_PCCustomColorInfo);
				}
				if (pMirrorPart->pCustomColors) {
					COPY_COSTUME_COLOR(pSharedColorPart->pCustomColors->glowScale, pMirrorPart->pCustomColors->glowScale);
				}
			}
		}
		if (g_GroupSelectMode && g_CostumeEditState.pPart->iBoneGroupIndex >= 0) {
			NOCONST(PCPart) *pGroupPart = NULL;
			PCSkeletonDef *skel = GET_REF(g_CostumeEditState.hSkeleton);

			if (skel)
			{
				PCBoneGroup **bg = skel->eaBoneGroups;
				if (bg)
				{
					for (i = eaSize(&bg[g_CostumeEditState.pPart->iBoneGroupIndex]->eaBoneInGroup)-1; i >= 0; --i)
					{
						pGroupPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(bg[g_CostumeEditState.pPart->iBoneGroupIndex]->eaBoneInGroup[i]->hBone), NULL);
						if (pGroupPart) {
							COPY_COSTUME_COLOR(pSharedColorPart->color0, pGroupPart->color0);
							COPY_COSTUME_COLOR(pSharedColorPart->color1, pGroupPart->color1);
							COPY_COSTUME_COLOR(pSharedColorPart->color2, pGroupPart->color2);
							COPY_COSTUME_COLOR(pSharedColorPart->color3, pGroupPart->color3);
							if (pSharedColorPart->pCustomColors) {
								if (!pGroupPart->pCustomColors) {
									pGroupPart->pCustomColors = StructCreateNoConst(parse_PCCustomColorInfo);
								}
								if (pGroupPart->pCustomColors) {
									COPY_COSTUME_COLOR(pSharedColorPart->pCustomColors->glowScale, pGroupPart->pCustomColors->glowScale);
								}
							} else if (pGroupPart->pCustomColors) {
								U8 zeroColor[4] = { 0,0,0,0 };
								COPY_COSTUME_COLOR(zeroColor, pGroupPart->pCustomColors->glowScale);
							}
						}
					}
				}
			}
		}
	} else {
		COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor0.color, g_CostumeEditState.pPart->color0);
		COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor1.color, g_CostumeEditState.pPart->color1);
		COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor2.color, g_CostumeEditState.pPart->color2);
		COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor3.color, g_CostumeEditState.pPart->color3);
		if (!g_CostumeEditState.pPart->pCustomColors) {
			g_CostumeEditState.pPart->pCustomColors = StructCreateNoConst(parse_PCCustomColorInfo);
		}
		if (g_CostumeEditState.pPart->pCustomColors) {
			COPY_COSTUME_COLOR(g_CostumeEditState.sharedGlowScale, g_CostumeEditState.pPart->pCustomColors->glowScale);
		}
		if (pMirrorPart) {
			COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor0.color, pMirrorPart->color0);
			COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor1.color, pMirrorPart->color1);
			COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor2.color, pMirrorPart->color2);
			COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor3.color, pMirrorPart->color3);
			if (!pMirrorPart->pCustomColors) {
				pMirrorPart->pCustomColors = StructCreateNoConst(parse_PCCustomColorInfo);
			}
			if (pMirrorPart->pCustomColors) {
				COPY_COSTUME_COLOR(g_CostumeEditState.sharedGlowScale, pMirrorPart->pCustomColors->glowScale);
			}
		}
		if (g_GroupSelectMode && g_CostumeEditState.pPart->iBoneGroupIndex >= 0) {
			NOCONST(PCPart) *pGroupPart = NULL;
			PCSkeletonDef *skel = GET_REF(g_CostumeEditState.hSkeleton);

			if (skel)
			{
				PCBoneGroup **bg = skel->eaBoneGroups;
				if (bg)
				{
					for (i = eaSize(&bg[g_CostumeEditState.pPart->iBoneGroupIndex]->eaBoneInGroup)-1; i >= 0; --i)
					{
						pGroupPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(bg[g_CostumeEditState.pPart->iBoneGroupIndex]->eaBoneInGroup[i]->hBone), NULL);
						if (pGroupPart) {
							COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor0.color, pGroupPart->color0);
							COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor1.color, pGroupPart->color1);
							COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor2.color, pGroupPart->color2);
							COPY_COSTUME_COLOR(g_CostumeEditState.sharedColor3.color, pGroupPart->color3);
							if (!pGroupPart->pCustomColors) {
								pGroupPart->pCustomColors = StructCreateNoConst(parse_PCCustomColorInfo);
							}
							if (pGroupPart->pCustomColors) {
								COPY_COSTUME_COLOR(g_CostumeEditState.sharedGlowScale, pGroupPart->pCustomColors->glowScale);
							}
						}
					}
				}
			}
		}
	}

	CostumeUI_RegenCostume(true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SharedColorsReset");
void CostumeCreator_SharedColorsReset(SA_PARAM_NN_VALID ExprContext *pContext)
{
	SpeciesDef *pSpecies = GET_REF(g_CostumeEditState.hSpecies);
	NOCONST(PCPart) *pSharedColorPart = CostumeUI_GetSharedColorCostumePart();
	PCColorQuad *pQuad = NULL;
	PCSkeletonDef *pSkel;
	static U8 noglow[4] = {0, 0, 0, 0};
	static PCColorQuad s_StaticQuad = { {10, 40, 90, 255}, {10, 10, 10, 255}, {240, 240, 240, 255}, {220, 0, 0, 255}, 0 };
	COSTUME_UI_TRACE_FUNC();

	// Get the skeleton
	pSkel = GET_REF(g_CostumeEditState.hSkeleton);

	// Unset the locks
	g_CostumeEditState.eSharedColorLocks = (g_CostumeEditState.pPart->eControlledRandomLocks & ~kPCControlledRandomLock_AllColors);

	// Get default colors
	if (pSkel) {
		pQuad = pSkel->pDefaultBodyColorQuad;
		if (!pQuad) {
			PCColorQuadSet *pQuadSet = GET_REF(pSkel->hColorQuadSet);
			if (!pQuadSet) {
				pQuadSet = RefSystem_ReferentFromString(g_hCostumeColorQuadsDict, "Core_Body");
			}
			if (pQuadSet && eaSize(&pQuadSet->eaColorQuads)) {
				pQuad = pQuadSet->eaColorQuads[0];
			}
		}
	}
	if (!pQuad) {
		pQuad = &s_StaticQuad;
	}

	// Set shared colors
	COPY_COSTUME_COLOR(pQuad->color0, g_CostumeEditState.sharedColor0.color);
	COPY_COSTUME_COLOR(pQuad->color1, g_CostumeEditState.sharedColor1.color);
	COPY_COSTUME_COLOR(pQuad->color2, g_CostumeEditState.sharedColor2.color);
	COPY_COSTUME_COLOR(pQuad->color3, g_CostumeEditState.sharedColor3.color);
	COPY_COSTUME_COLOR(noglow, g_CostumeEditState.sharedGlowScale);
	if (pSharedColorPart) {
		costumeTailor_SetPartColors(g_CostumeEditState.pCostume, pSpecies, g_CostumeEditState.pSlotType, pSharedColorPart, pQuad->color0, pQuad->color1, pQuad->color2, pQuad->color3, noglow);
	}

	CostumeUI_RegenCostume(true);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_CostumeHistoryPushCurrent");
void CostumeCreator_SaveCurrentCostume(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	gclCostumeEditListExpr_PushCostume(COSTUME_LIST_HISTORY, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_CostumeHistorySize");
int CostumeCreator_CostumeHistorySize(SA_PARAM_NN_VALID UIGen *pGen)
{
	COSTUME_UI_TRACE_FUNC();
	return gclCostumeEditListExpr_ListSize(COSTUME_LIST_HISTORY);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_CostumeHistoryClearAfter");
void CostumeCreator_CostumeHistoryClearAfter(SA_PARAM_NN_VALID UIGen *pGen, int iAfter)
{
	COSTUME_UI_TRACE_FUNC();
	gclCostumeEditListExpr_ClearAfter(COSTUME_LIST_HISTORY, iAfter);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_CostumeHistorySelect");
void CostumeCreator_CostumeHistorySelect(SA_PARAM_NN_VALID UIGen *pGen, int iHistory)
{
	COSTUME_UI_TRACE_FUNC();
	gclCostumeEditListExpr_SelectCostume(COSTUME_LIST_HISTORY, iHistory);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_CostumeHistoryLimit");
void CostumeCreator_CostumeHistoryLimit(SA_PARAM_NN_VALID UIGen *pGen, int iLimit)
{
	CostumeSourceList *pList;
	COSTUME_UI_TRACE_FUNC();
	pList = CostumeEditList_GetSourceList(COSTUME_LIST_HISTORY, true);
	pList->iMaxSize = MAX(0, iLimit);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_MaterialHasGlow");
bool CostumeCreator_MaterialHasGlow(SA_PARAM_NN_VALID ExprContext *pContext, int iColor)
{
	COSTUME_UI_TRACE_FUNC();
	return CostumeUI_MatHasGlow(iColor);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetGlow");
int CostumeCreator_GetGlow(SA_PARAM_NN_VALID ExprContext *pContext, int iColor)
{
	NOCONST(PCPart) *pMirrorPart = NULL;
	NOCONST(PCPart) *pRealPart = NULL;
	PCGeometryDef *pGeo;
	bool bBothMode = false;
	NOCONST(PCPart) *pSharedPart = CostumeUI_GetSharedColorCostumePart();
	PCMaterialDef *pMaterial = GET_REF(g_CostumeEditState.hMaterial);
	COSTUME_UI_TRACE_FUNC();

	if (!GET_REF(g_CostumeEditState.hBone)) return 0;
	if (!stricmp(GET_REF(g_CostumeEditState.hBone)->pcName,"None")) return 0;
	pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hBone), NULL);
	assert(pRealPart);
	pGeo = GET_REF(pRealPart->hGeoDef);
	assert(pGeo);
	if (pGeo->pClothData && pGeo->pClothData->bIsCloth && pGeo->pClothData->bHasClothBack && pRealPart->pClothLayer && (pRealPart->eEditMode == kPCEditMode_Both)) {
		bBothMode = true;
	}

	if (g_MirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Both)) {
		pMirrorPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, pRealPart);
	}

	if (CostumeUI_MatHasGlow(iColor)) {
		switch (iColor) {
			case 110:
			case 111:
			case 112:
			case 113:
				return (pSharedPart ? GET_PART_GLOWSCALE(pSharedPart,iColor - 110) : g_CostumeEditState.sharedGlowScale[iColor - 110]) & 0xff;
			case 0:
			case 1:
			case 2:
			case 3:
			case 120:
			case 121:
			case 122:
			case 123:
				if (iColor == 3 && pMaterial && pMaterial->bHasSkin) {
					return 0;
				}
				if (g_CostumeEditState.pPart) {
					if (pMirrorPart) {
						U8 mainGlow = GET_PART_GLOWSCALE(pRealPart, iColor < 4 ? iColor : iColor - 120);
						U8 mirrorGlow = GET_PART_GLOWSCALE(pMirrorPart, iColor < 4 ? iColor : iColor - 120);
						if (mainGlow != mirrorGlow) {
							return 0;
						}
						return mainGlow;
					} else if (bBothMode) {
						U8 mainGlow = GET_PART_GLOWSCALE(pRealPart, iColor < 4 ? iColor : iColor - 120);
						U8 clothGlow = GET_PART_GLOWSCALE(pRealPart->pClothLayer, iColor < 4 ? iColor : iColor - 120);
						if (mainGlow != clothGlow) {
							return 0;
						}
						return mainGlow;
					} else {
						return (GET_PART_GLOWSCALE(pRealPart,iColor < 4 ? iColor : iColor - 120)) & 0xff;
					}
				}
				return 0;
		}
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_CreateListModelFromString");
void CostumeCreator_CreateListModelFromString(SA_PARAM_NN_VALID ExprContext *pContext, const char *pchModel)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	StringModelRow ***peaStringModelRow = pGen ? ui_GenGetManagedListSafe(pGen, StringModelRow) : NULL;
	char *pchModelStack = NULL;
	char *pchContext = NULL;
	char *pchToken = NULL;
	int iUsed = 0;
	COSTUME_UI_TRACE_FUNC();

	if (!peaStringModelRow)
	{
		return;
	}

	strdup_alloca(pchModelStack, pchModel);

	while ((pchToken = strtok_r(!pchContext ? pchModelStack : NULL, " ,\r\n\t%", &pchContext)))
	{
		StringModelRow *pRow;

		while (iUsed >= eaSize(peaStringModelRow))
		{
			StringModelRow *pNewRow = StructCreate(parse_StringModelRow);
			estrCreate(&pNewRow->estrString);
			eaPush(peaStringModelRow, pNewRow);
		}

		pRow = (*peaStringModelRow)[iUsed++];
		estrClear(&pRow->estrString);
		estrAppend2(&pRow->estrString, pchToken);
	}

	while (iUsed < eaSize(peaStringModelRow))
	{
		StringModelRow *pExtraRow = eaPop(peaStringModelRow);
		estrDestroy(&pExtraRow->estrString);
		StructDestroy(parse_StringModelRow, pExtraRow);
	}

	ui_GenSetManagedListSafe(pGen, peaStringModelRow, StringModelRow, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_CheckName");
const char *CostumeCreator_CheckName(ExprContext *pContext, const char *pchName)
{
	int strerr;
	char *estrWorking = NULL;
	Entity *pEnt = entActivePlayerPtr();
	COSTUME_UI_TRACE_FUNC();

	if (!pchName)
		return "(null)";

	estrStackCreate(&estrWorking);
	estrAppend2(&estrWorking, pchName);
	estrTrimLeadingAndTrailingWhitespace(&estrWorking);

	if (!pEnt)
	{
		pEnt = (Entity *)g_pFakePlayer;
	}

	strerr = StringIsInvalidCharacterName( estrWorking, entGetAccessLevel(pEnt) );
	
	if(strerr == 0)
	{
		// check for badname, if so make sure name is different than old name
        const Login2CharacterChoice *pCharacter = GetRenamingCharacter();
		if(pCharacter && pCharacter->hasBadName && pCharacter->oldBadName)
		{
			if(stricmp(pchName, pCharacter->oldBadName) == 0)
			{
				strerr = STRINGERR_RESTRICTED;
			}
		}
	}
	
	if (strerr > 0 )
	{
		char *estrError = NULL;
		char *result = NULL;

		estrStackCreate(&estrError);
		StringCreateNameError(&estrError, strerr);

		result = exprContextAllocScratchMemory(pContext, strlen(estrError) + 1);
		memcpy(result, estrError, strlen(estrError) + 1);
		estrDestroy(&estrError);
		estrDestroy(&estrWorking);
		return FIRST_IF_SET(result, "(null)");
	}
	
	estrDestroy(&estrWorking);
	return "No Error"; // This message 
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_CheckDescription");
const char *CostumeCreator_CheckDescription(ExprContext *pContext, const char *pchDescription)
{
	int strerr;
	COSTUME_UI_TRACE_FUNC();

	if (!pchDescription)
		return "(null)";

	strerr = StringIsInvalidDescription( pchDescription );
	if (strerr > 0 )
	{
		char *estrError = NULL;
		char *result = NULL;

		estrStackCreate(&estrError);
		StringCreateDescriptionError(&estrError, strerr);

		result = exprContextAllocScratchMemory(pContext, strlen(estrError) + 1);
		memcpy(result, estrError, strlen(estrError) + 1);
		estrDestroy(&estrError);
		return FIRST_IF_SET(result, "(null)");
	}

	return "No Error"; // This message 
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetDefaultHeight");
float CostumeCreator_GetDefaultHeight(void)
{
	PCSkeletonDef *pSkel = GET_REF(g_CostumeEditState.hSkeleton);
	COSTUME_UI_TRACE_FUNC();
	if (pSkel) {
		return pSkel->fDefaultHeight;
	}
	return g_CostumeEditState.pCostume ? g_CostumeEditState.pCostume->fHeight : 6;
}

// This saves the player's current costume to a text file
// This is not part of the tailor UI.  It's an access level command for use by artists.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME("SaveCostumeToFile");
char *CostumeSystem_saveCostumeToFile(void)
{
	Entity *pEnt;
	PlayerCostume *pCostume = NULL;
	PlayerCostumeHolder sHolder;

	pEnt = entActivePlayerPtr();
	if (pEnt) {
		pCostume = costumeEntity_GetActiveSavedCostume(pEnt);
	}

	if (pCostume) {
		sHolder.pCostume = StructClone(parse_PlayerCostume, pCostume);
		if (pEnt && pEnt->pChar && sHolder.pCostume) {
			COPY_HANDLE(sHolder.pCostume->hSpecies, pEnt->pChar->hSpecies);
		}
		ParserWriteTextFile("C:\\SavedCostume.costume", parse_PlayerCostumeHolder, &sHolder, 0, 0);
		StructDestroy(parse_PlayerCostume, sHolder.pCostume);
		return "Costume written to 'C:\\SavedCostume.costume'";
	} else {
		return "No costume to save";
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
char *CostumeCreator_SaveCostumeToFile(ACMD_SENTENCE pchPath)
{
	static char achLog[500];
	PlayerCostumeHolder sHolder;

	if (g_CostumeEditState.pConstCostume)
	{
		char *pchBasePath = NULL;
		char *pchName = NULL;
		char *pchIter;
		char *pchFullPath;
		char achFileName[CRYPTIC_MAX_PATH];

		sHolder.pCostume = StructClone(parse_PlayerCostume, g_CostumeEditState.pConstCostume);
		if (!devassert(sHolder.pCostume)) {
			return "Costume copy failed";
		}
		costumeTailor_StripUnnecessary(CONTAINER_NOCONST(PlayerCostume, sHolder.pCostume));

		if (strEndsWith(pchPath, ".costume"))
		{
			// Extract the name of the costume
			char *pchClipFore = strrchr(pchPath, '/');
			char *pchClipBack = strrchr(pchPath, '\\');
			pchClipFore = MAX(pchClipFore, pchClipBack);

			if (!pchClipFore)
				// Use an empty path
				pchClipFore = pchPath;
			else
				// Exclude the slash
				pchClipFore++;

			// Extract just the name portion
			strdup_alloca(pchName, pchClipFore);
			*strrchr(pchName, '.') = '\0';

			// Save the folder path
			strdup_alloca(pchBasePath, pchPath);
			pchBasePath[pchClipFore - pchPath] = '\0';

			// Add the folder separator
			if (!strEndsWith(pchBasePath, "/") && !strEndsWith(pchBasePath, "\\"))
				strcat_s(pchBasePath, strlen(pchPath), "/");
		}
		else
		{
			U32 pathLen = (U32)strlen(pchPath) + 1;
			// Use the costume's name
			if (sHolder.pCostume->pcName && *sHolder.pCostume->pcName) {
				strdup_alloca(pchName, sHolder.pCostume->pcName);
			} else {
				strdup_alloca(pchName, "New_Costume");
			}

			// Copy the base path
			pchBasePath = alloca(pathLen + 1);
			strcpy_s(pchBasePath, pathLen + 1, pchPath);

			// Add the folder separator
			if (!strEndsWith(pchBasePath, "/") && !strEndsWith(pchBasePath, "\\"))
				strcat_s(pchBasePath, pathLen + 1, "/");
		}

		// Normalize the costume name
		for (pchIter = pchName; *pchIter; pchIter++) {
			if (!strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_", *pchIter)) {
				*pchIter = '_';
			}
		}
		while (*pchName == '_') {
			pchName++;
		}
		for (pchIter = pchName; *pchIter && *(pchIter+1); pchIter++) {
			if (*pchIter == '_' && *(pchIter+1) == '_') {
				U32 nameLen = (U32)strlen(pchName);
				memmove_s(pchIter, nameLen, pchIter+1, nameLen);
			}
		}

		// Set the costume name
		*(const char **)&sHolder.pCostume->pcName = allocAddString(pchName);

		{
			U32 fullLen = (U32)strlen(pchBasePath) + (U32)strlen(pchName) + 8;
			pchFullPath = alloca(fullLen + 1);
			strcpy_s(pchFullPath, fullLen+1, pchBasePath);
			strcat_s(pchFullPath, fullLen+1, pchName);
			strcat_s(pchFullPath, fullLen+1, ".costume");
		}

		// Write the file
		if (fileLocateWrite(pchFullPath, achFileName)) {
			COPY_HANDLE(sHolder.pCostume->hSpecies, g_CostumeEditState.hSpecies);
			ParserWriteTextFile(pchFullPath, parse_PlayerCostumeHolder, &sHolder, 0, 0);
			strcpy(achLog, "Costume written to: ");
		} else {
			strcpy(achLog, "Error locating file: ");
		}
		strcat(achLog, pchFullPath);

		StructDestroy(parse_PlayerCostume, sHolder.pCostume);
	} else {
		strcpy(achLog, "No costume exists");
	}

	return achLog;
}

// Get a list of all PCPresetScaleValueGroups for a given ScaleInfoGroup
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPresetScales");
void CostumeCreator_GetPresetScales(SA_PARAM_NN_VALID UIGen *pGen, const char *pchGroupname)
{
	PCPresetScaleValueGroup ***pPresets = ui_GenGetManagedListSafe(pGen, PCPresetScaleValueGroup);
	COSTUME_UI_TRACE_FUNC();
	eaClear(pPresets);
	costumeTailor_GetCostumePresetScales(g_CostumeEditState.pCostume, pPresets, pchGroupname);
	ui_GenSetManagedListSafe(pGen, pPresets, PCPresetScaleValueGroup, 0);
}

// Get a list of all PCPresetScaleValueGroups for a given ScaleInfoGroup
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_ApplyPresetScales");
void CostumeCreator_ApplyPresetScales(SA_PARAM_OP_VALID PCPresetScaleValueGroup *pPreset)
{
	NOCONST(PlayerCostume) *pPCCostume = g_CostumeEditState.pCostume;
	COSTUME_UI_TRACE_FUNC();
	if (pPreset)
	{
		CostumeCreator_SetStance(pPreset->pcStance);
		CostumeCreator_SetMood(pPreset->pcMood);
		costumeTailor_ApplyPresetScaleValueGroup(pPCCostume, pPreset);
	}
	CostumeUI_RegenCostume(true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetDemoFlag");
int GetDemoFlag(void)
{
	COSTUME_UI_TRACE_FUNC();
	return s_iDemoFlag;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SetDemoFlag");
void SetDemoFlag(int iFlag)
{
	COSTUME_UI_TRACE_FUNC();
	s_iDemoFlag = iFlag;
}

int CostumeCreator_GetGeoColorFlagsInternal(PCBoneDef *pBone);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetGeoColorFlags");
int CostumeCreator_GetGeoColorFlags(void)
{
	COSTUME_UI_TRACE_FUNC();
	return CostumeCreator_GetGeoColorFlagsInternal(GET_REF(g_CostumeEditState.hBone));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetBoneGeoColorFlags");
int CostumeCreator_GetBoneGeoColorFlags(const char *pcBone)
{
	PCBoneDef *pBone;
	COSTUME_UI_TRACE_FUNC();
	pBone = pcBone && *pcBone ? CostumeUI_FindBone(pcBone, GET_REF(g_CostumeEditState.hSkeleton)) : NULL;
	return CostumeCreator_GetGeoColorFlagsInternal(pBone);
}

int CostumeCreator_GetGeoColorFlagsInternal(PCBoneDef *pBone)
{
	NOCONST(PCPart) *pRealPart;
	NOCONST(PCPart) *pPart;
	PCGeometryDef *pGeo;

	if (!pBone) return 0;
	if (!stricmp(pBone->pcName,"None")) return 0;
	pRealPart = costumeTailor_GetPartByBone(g_CostumeEditState.pCostume, pBone, NULL);
	assert(pRealPart);
	if (g_MirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Right)) {
		pPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, pRealPart);
		pGeo = GET_REF(pPart->hGeoDef);
	} else {
		pGeo = GET_REF(pRealPart->hGeoDef);
	}

	if (g_MirrorSelectMode && (pRealPart->eEditMode == kPCEditMode_Both)) {
		NOCONST(PCPart) *pMirrorPart = costumeTailor_GetMirrorPart(g_CostumeEditState.pCostume, pRealPart);
		if (pMirrorPart) {
			PCGeometryDef *pMirrorGeo = GET_REF(pMirrorPart->hGeoDef);
			return (pGeo ? pGeo->eColorChoices : 0) & (pMirrorGeo ? pMirrorGeo->eColorChoices : 0);
		}
	}

	return pGeo ? pGeo->eColorChoices : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetGeoUnlock");
SA_RET_OP_VALID UnlockMetaData *CostumeCreator_GetGeoUnlock(SA_PARAM_NN_VALID UIGen *pGen, const char *pcGeometryName)
{
	UnlockMetaData *pUnlockData = NULL;
	COSTUME_UI_TRACE_FUNC();
	stashFindPointer(g_CostumeEditState.stashGeoUnlockMeta, pcGeometryName, &pUnlockData);
	ui_GenSetPointer(pGen, pUnlockData, parse_UnlockMetaData);
	return pUnlockData;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetMatUnlock");
SA_RET_OP_VALID UnlockMetaData *CostumeCreator_GetMatUnlock(SA_PARAM_NN_VALID UIGen *pGen, const char *pcMaterialName)
{
	UnlockMetaData *pUnlockData = NULL;
	COSTUME_UI_TRACE_FUNC();
	stashFindPointer(g_CostumeEditState.stashMatUnlockMeta, pcMaterialName, &pUnlockData);
	ui_GenSetPointer(pGen, pUnlockData, parse_UnlockMetaData);
	return pUnlockData;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetTexUnlock");
SA_RET_OP_VALID UnlockMetaData *CostumeCreator_GetTexUnlock(SA_PARAM_NN_VALID UIGen *pGen, const char *pcTextureName)
{
	UnlockMetaData *pUnlockData = NULL;
	COSTUME_UI_TRACE_FUNC();
	stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, pcTextureName, &pUnlockData);
	ui_GenSetPointer(pGen, pUnlockData, parse_UnlockMetaData);
	return pUnlockData;
}

S64 CostumeCreator_GenerateProductList(SA_PARAM_OP_VALID UIGen *pGen, bool bUnownedOnly, MicroTransactionProduct ***peaProducts)
{
	static U32 *s_eaiProducts = NULL;
	static MicroTransactionProduct **s_eaUsedProducts = NULL;
	S64 iPrice = 0;
	int i;

	if (!peaProducts) {
		peaProducts = &s_eaUsedProducts;
	}

	eaiClearFast(&s_eaiProducts);
	eaClearFast(peaProducts);
	if (g_CostumeEditState.pCostume && g_pMTList)
	{
		for (i = eaSize(&g_CostumeEditState.pCostume->eaParts) - 1; i >= 0; --i)
		{
			NOCONST(PCPart) *pPart = g_CostumeEditState.pCostume->eaParts[i];
			PCGeometryDef *pGeo = GET_REF(pPart->hGeoDef);
			PCMaterialDef *pMat = GET_REF(pPart->hMatDef);
			PCTextureDef *pPattern = GET_REF(pPart->hPatternTexture);
			PCTextureDef *pDetail = GET_REF(pPart->hDetailTexture);
			PCTextureDef *pSpecular = GET_REF(pPart->hSpecularTexture);
			PCTextureDef *pDiffuse = GET_REF(pPart->hDiffuseTexture);
			PCTextureDef *pMovable = SAFE_GET_REF(pPart->pMovableTexture, hMovableTexture);
			UnlockMetaData *pUnlockData = NULL;

			if (pGeo && stashFindPointer(g_CostumeEditState.stashGeoUnlockMeta, pGeo->pcName, &pUnlockData) && pUnlockData->uMicroTransactionID) {
				if ((!pUnlockData->bOwned || !bUnownedOnly) && pUnlockData->uMicroTransactionID) {
					eaiPushUnique(&s_eaiProducts, pUnlockData->uMicroTransactionID);
				}
			}

			if (pMat && stashFindPointer(g_CostumeEditState.stashMatUnlockMeta, pMat->pcName, &pUnlockData) && pUnlockData->uMicroTransactionID) {
				if ((!pUnlockData->bOwned || !bUnownedOnly) && pUnlockData->uMicroTransactionID) {
					eaiPushUnique(&s_eaiProducts, pUnlockData->uMicroTransactionID);
				}
			}

			if (pPattern && stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, pPattern->pcName, &pUnlockData) && pUnlockData->uMicroTransactionID) {
				if ((!pUnlockData->bOwned || !bUnownedOnly) && pUnlockData->uMicroTransactionID) {
					eaiPushUnique(&s_eaiProducts, pUnlockData->uMicroTransactionID);
				}
			}

			if (pDetail && stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, pDetail->pcName, &pUnlockData) && pUnlockData->uMicroTransactionID) {
				if ((!pUnlockData->bOwned || !bUnownedOnly) && pUnlockData->uMicroTransactionID) {
					eaiPushUnique(&s_eaiProducts, pUnlockData->uMicroTransactionID);
				}
			}

			if (pSpecular && stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, pSpecular->pcName, &pUnlockData) && pUnlockData->uMicroTransactionID) {
				if ((!pUnlockData->bOwned || !bUnownedOnly) && pUnlockData->uMicroTransactionID) {
					eaiPushUnique(&s_eaiProducts, pUnlockData->uMicroTransactionID);
				}
			}

			if (pDiffuse && stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, pDiffuse->pcName, &pUnlockData) && pUnlockData->uMicroTransactionID) {
				if ((!pUnlockData->bOwned || !bUnownedOnly) && pUnlockData->uMicroTransactionID) {
					eaiPushUnique(&s_eaiProducts, pUnlockData->uMicroTransactionID);
				}
			}

			if (pMovable && stashFindPointer(g_CostumeEditState.stashTexUnlockMeta, pMovable->pcName, &pUnlockData) && pUnlockData->uMicroTransactionID) {
				if ((!pUnlockData->bOwned || !bUnownedOnly) && pUnlockData->uMicroTransactionID) {
					eaiPushUnique(&s_eaiProducts, pUnlockData->uMicroTransactionID);
				}
			}
		}

		for (i = eaiSize(&s_eaiProducts) - 1; i >= 0; i--) {
			MicroTransactionProduct *pProduct = eaIndexedGetUsingInt(&g_pMTList->ppProducts, s_eaiProducts[i]);
			if (devassertmsg(pProduct, "Expected MicroTransaction product to exist")) {
				iPrice += microtrans_GetPrice(pProduct->pProduct);
				eaPush(peaProducts, pProduct);
			}
		}
	}

	if (pGen) {
		gclMicroTrans_GetProductList(pGen, entActivePlayerPtr(), *peaProducts);
	}

	return iPrice;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetProductList");
void CostumeCreator_GetProductList(SA_PARAM_NN_VALID UIGen *pGen, bool bUnownedOnly)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeCreator_GenerateProductList(pGen, bUnownedOnly, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetProductListSize");
S32 CostumeCreator_GetProductListSize(SA_PARAM_NN_VALID UIGen *pGen, bool bUnownedOnly)
{
	static MicroTransactionProduct **s_eaProducts;
	COSTUME_UI_TRACE_FUNC();
	CostumeCreator_GenerateProductList(NULL, bUnownedOnly, &s_eaProducts);
	return eaSize(&s_eaProducts);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetProductCost");
S64 CostumeCreator_GetProductCost(bool bUnownedOnly)
{
	COSTUME_UI_TRACE_FUNC();
	return CostumeCreator_GenerateProductList(NULL, true, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_PurchaseCostumes");
void CostumeCreator_PurchaseCostumes(SA_PARAM_OP_STR const char *pchCurrency)
{
	MicroTransactionProduct **eaProducts = NULL;
	COSTUME_UI_TRACE_FUNC();
	CostumeCreator_GenerateProductList(NULL, true, &eaProducts);
	gclMicroTrans_PurchaseMicroTransactionList(entActivePlayerPtr(), eaProducts, pchCurrency);
	eaDestroy(&eaProducts);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetRegionCategoryBoneFilter");
void CostumeCreator_SetRegionCategoryBoneFilter(bool bFlag)
{
	COSTUME_UI_TRACE_FUNC();
	if (bFlag != s_bRegionCategoryBoneFilter)
	{
		s_bRegionCategoryBoneFilter = bFlag;
		CostumeUI_RegenCostumeEx(true, true);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_FlashFX");
void CostumeCreator_FlashFX(const char* pchFXName)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_pCostumeView && g_pCostumeView->costume.pSkel && pchFXName && pchFXName[0])
	{
		DynAddFxParams params = {0};
		params.pSourceRoot = g_pCostumeView->costume.pSkel->pRoot;
		params.eSource = eDynFxSource_Costume;

		if (dynFxInfoSelfTerminates(pchFXName))
		{
			dynAddFx(g_pCostumeView->costume.pFxManager, pchFXName, &params);
		}
		else
		{
			Errorf("CostumeCreator_FlashFX: Tried to flash FX '%s' that is not self-terminating", pchFXName);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_StartFX");
void CostumeCreator_StartFX(const char* pchFXName)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_pCostumeView && g_pCostumeView->costume.pSkel && pchFXName && pchFXName[0] && g_pCostumeView->costume.pFxManager)
	{
		DynParamBlock *params = NULL;
		dynFxManAddMaintainedFX(g_pCostumeView->costume.pFxManager, pchFXName, params, 0.0f, 0, eDynFxSource_UI);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_StopFX");
void CostumeCreator_StopFX(const char* pchFXName)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_pCostumeView && pchFXName && pchFXName[0] && g_pCostumeView->costume.pFxManager)
	{
		dynFxManRemoveMaintainedFX(g_pCostumeView->costume.pFxManager, pchFXName, true);
	}
}

// Returns true if the costumeview is playing a given FX
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(CostumeCreator_IsPlayingFx);
bool exprCostumeCreator_IsPlayingFx(const char *pchFx)
{
	// This is probably the wrong way to do this.
	if( g_pCostumeView )
	{
		DynFxManager *pFxManager = g_pCostumeView->costume.pFxManager;
		pchFx = pchFx && *pchFx ? allocFindString(pchFx) : pchFx;
		if (pFxManager && pchFx && *pchFx)
		{
			S32 i;
			for (i = 0; i < eaSize(&pFxManager->eaDynFx); i++)
			{
				DynFx *pDynFx = pFxManager->eaDynFx[i];
				if (pDynFx && REF_STRING_FROM_HANDLE(pDynFx->hInfo) == pchFx)
				{
					return true;
				}
			}
		}
	}
	return false;
}

PlayerCostume *CostumeCreator_GetCostume(void)
{
	return g_CostumeEditState.pConstHoverCostume ? g_CostumeEditState.pConstHoverCostume : g_CostumeEditState.pConstCostume;
}

PCMood *CostumeCreator_GetMood(void)
{
	return GET_REF(g_CostumeEditState.hMood);
}

void CostumeUI_AddInventoryItems(ItemDefRef **eaShowItems)
{
	if (eaSize(&eaShowItems) > 0) {
		eaCopyStructs(&eaShowItems, &g_CostumeEditState.eaShowItems, parse_ItemDefRef);
	} else {
		eaDestroyStruct(&g_CostumeEditState.eaShowItems, parse_ItemDefRef);
	}
	CostumeUI_RegenCostume(false);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(Entity_GetCostumeSetIndexToShow);
U8 exprEntGetCostumeSetIndexToShow(SA_PARAM_OP_VALID Entity *pEntity)
{
	return SAFE_MEMBER2(pEntity,pSaved,iCostumeSetIndexToShow);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(Entity_SetCostumeSetIndexToShow);
void exprEntSetCostumeSetIndexToShow(SA_PARAM_NN_VALID Entity *pEntity, U8 iCostumeSetToShow)
{
#ifdef GAMECLIENT
	ServerCmd_gslEntity_SetCostumeSetIndexToShow(iCostumeSetToShow);
#endif
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_ProjectPointToScreenX);
float CostumeCreator_ProjectPointToScreenX(float fX, float fY, float fZ)
{
	Vec3 vWorldPos = {fX, fY, fZ};
	Vec2 vScreenPos = {0, 0};
	COSTUME_UI_TRACE_FUNC();
	gfxWorldToScreenSpaceVector(&s_CameraView, vWorldPos, vScreenPos, true);
	return vScreenPos[0];
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_ProjectPointToScreenY);
float CostumeCreator_ProjectPointToScreenY(float fX, float fY, float fZ)
{
	Vec3 vWorldPos = {fX, fY, fZ};
	Vec2 vScreenPos = {0, 0};
	COSTUME_UI_TRACE_FUNC();
	gfxWorldToScreenSpaceVector(&s_CameraView, vWorldPos, vScreenPos, true);
	return vScreenPos[1];
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetUseHorizontalFOV);
void CostumeCreator_SetUseHorizontalFOV(bool bUseHorizontalFOV)
{
	gfxGetActiveCameraController()->useHorizontalFOV = bUseHorizontalFOV;
}


void CostumeUI_ResetCostumeSkinColor(NOCONST(PlayerCostume) *pTarget, PlayerCostume *pCostume)
{
	if (pTarget && pCostume)
	{
		pTarget->skinColor[0] = pCostume->skinColor[0];
		pTarget->skinColor[1] = pCostume->skinColor[1];
		pTarget->skinColor[2] = pCostume->skinColor[2];
		pTarget->skinColor[3] = pCostume->skinColor[3];
		CostumeUI_RegenCostume(true);
	}
}

void CostumeUI_ResetCostumeHeight(NOCONST(PlayerCostume) *pTarget, PlayerCostume *pCostume)
{
	if (pTarget && pCostume)
	{
		pTarget->fHeight = pCostume->fHeight;
		CostumeUI_RegenCostume(true);
	}
}

void CostumeUI_ResetCostumeMuscle(NOCONST(PlayerCostume) *pTarget, PlayerCostume *pCostume)
{
	if (pTarget && pCostume)
	{
		pTarget->fMuscle = pCostume->fMuscle;
		CostumeUI_RegenCostume(true);
	}
}

void CostumeUI_ResetCostumeStance(NOCONST(PlayerCostume) *pTarget, PlayerCostume *pCostume)
{
	if (pTarget && pCostume)
	{
		pTarget->pcStance = pCostume->pcStance;
		CostumeUI_RegenCostume(true);
	}
}

void CostumeUI_ResetCostumeBodyScales(const char* pchBodyScaleName, NOCONST(PlayerCostume) *pTarget, PlayerCostume *pCostume)
{
	if (pTarget && pCostume)
	{
		PCSkeletonDef *pSkelSrc = GET_REF(pCostume->hSkeleton);
		PCSkeletonDef *pSketTarget = GET_REF(pTarget->hSkeleton);
		bool bAll = stricmp(pchBodyScaleName, "all") == 0;
		int i, j;

		if (pSkelSrc && pSketTarget)
		{
			for(j=0; j<eaSize(&pSkelSrc->eaBodyScaleInfo); ++j)
			{
				if (bAll || isWildcardMatch(pchBodyScaleName, pSkelSrc->eaBodyScaleInfo[j]->pcName, false, true))
				{
					for(i=0; i<eaSize(&pSketTarget->eaBodyScaleInfo); ++i)
					{
						if (stricmp(pSkelSrc->eaBodyScaleInfo[j]->pcName, pSketTarget->eaBodyScaleInfo[i]->pcName) == 0)
						{
							pTarget->eafBodyScales[i] = pCostume->eafBodyScales[j];
							break;
						}
					}
				}
			}

			CostumeUI_RegenCostume(true);
		}
	}
}

void CostumeUI_ResetCostumeBoneScales(const char* pchBoneScaleName, NOCONST(PlayerCostume) *pTarget, PlayerCostume *pCostume)
{
	PCSkeletonDef *pTargetSkel = SAFE_GET_REF(pTarget, hSkeleton);
	if (pTarget && pTargetSkel && pCostume)
	{
		bool bAll = stricmp(pchBoneScaleName, "all") == 0;
		int i, j;

		for(i=eaSize(&pTarget->eaScaleValues)-1; i>=0; --i)
		{
			if (bAll || isWildcardMatch(pchBoneScaleName, pTarget->eaScaleValues[i]->pcScaleName, false, true))
			{
				for(j=eaSize(&pCostume->eaScaleValues)-1; j>=0; --j)
				{
					if (stricmp(pCostume->eaScaleValues[j]->pcScaleName, pTarget->eaScaleValues[i]->pcScaleName) == 0)
						break;
				}
				if (j < 0)
					StructDestroyNoConst(parse_PCScaleValue, eaRemove(&pTarget->eaScaleValues, i));
			}
		}

		for(j=eaSize(&pCostume->eaScaleValues)-1; j>=0; --j)
		{
			if (bAll || isWildcardMatch(pchBoneScaleName, pCostume->eaScaleValues[j]->pcScaleName, false, true))
			{
				for(i=eaSize(&pTarget->eaScaleValues)-1; i>=0; --i)
				{
					if (stricmp(pCostume->eaScaleValues[j]->pcScaleName, pTarget->eaScaleValues[i]->pcScaleName) == 0)
					{
						pTarget->eaScaleValues[i]->fValue = pCostume->eaScaleValues[j]->fValue;
						break;
					}
				}
				if (i < 0)
				{
					for(i=eaSize(&pTargetSkel->eaScaleInfo)-1; i>=0; --i)
					{
						if (stricmp(pCostume->eaScaleValues[j]->pcScaleName, pTargetSkel->eaScaleInfo[i]->pcName) == 0)
							break;
					}
					if (i >= 0)
						eaPush(&pTarget->eaScaleValues, StructCloneDeConst(parse_PCScaleValue, pCostume->eaScaleValues[j]));
				}
			}
		}

		CostumeUI_RegenCostume(true);
	}
}

void CostumeUI_ResetCostumePart(const char* pchBoneName, NOCONST(PlayerCostume) *pTarget, PlayerCostume *pCostume)
{
	PCSkeletonDef *pTargetSkel = SAFE_GET_REF(pTarget, hSkeleton);
	if (pTarget && pTargetSkel && pCostume)
	{
		bool bAll = stricmp(pchBoneName, "all") == 0;
		int i, j;

		for(i=eaSize(&pTarget->eaParts)-1; i>=0; --i)
		{
			PCBoneDef *pTargetBone = GET_REF(pTarget->eaParts[i]->hBoneDef);
			if (bAll || pTargetBone && isWildcardMatch(pchBoneName, pTargetBone->pcName, false, true))
			{
				for(j=eaSize(&pCostume->eaParts)-1; j>=0; --j)
				{
					PCBoneDef *pCostumeBone = GET_REF(pCostume->eaParts[j]->hBoneDef);
					if (pCostumeBone && stricmp(pCostumeBone->pcName, pTargetBone->pcName) == 0)
						break;
				}
				if (j < 0)
					StructDestroyNoConst(parse_PCPart, eaRemove(&pTarget->eaParts, i));
			}
		}

		for(j=eaSize(&pCostume->eaParts)-1; j>=0; --j)
		{
			PCBoneDef *pCostumeBone = GET_REF(pCostume->eaParts[j]->hBoneDef);
			if (bAll || pCostumeBone && isWildcardMatch(pchBoneName, pCostumeBone->pcName, false, true))
			{
				for(i=eaSize(&pTarget->eaParts)-1; i>=0; --i)
				{
					PCBoneDef *pTargetBone = GET_REF(pTarget->eaParts[i]->hBoneDef);
					if (pTargetBone && stricmp(pCostumeBone->pcName, pTargetBone->pcName) == 0)
					{
						StructCopyAllDeConst(parse_PCPart, pCostume->eaParts[j], pTarget->eaParts[i]);
						break;
					}
				}
				if (i < 0)
				{
					for(i=eaSize(&pTargetSkel->eaRequiredBoneDefs)-1; i>=0; --i)
					{
						PCBoneDef *pTargetBone = GET_REF(pTargetSkel->eaRequiredBoneDefs[i]->hBone);
						if (stricmp(pCostumeBone->pcName, pTargetBone->pcName) == 0)
							break;
					}
					if (i < 0)
					{
						for(i=eaSize(&pTargetSkel->eaOptionalBoneDefs)-1; i>=0; --i)
						{
							PCBoneDef *pTargetBone = GET_REF(pTargetSkel->eaOptionalBoneDefs[i]->hBone);
							if (stricmp(pCostumeBone->pcName, pTargetBone->pcName) == 0)
								break;
						}
					}
					if (i >= 0)
						eaPush(&pTarget->eaParts, StructCloneDeConst(parse_PCPart, pCostume->eaParts[j]));
				}
			}
		}

		CostumeUI_RegenCostume(true);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_IsCostumeFullyLoaded);
bool CostumeCreator_IsCostumeFullyLoaded()
{
	if( g_CostumeEditState.pConstCostume )
	{
		const char *pCostumeName = costumeGenerate_CreateWLCostumeName(g_CostumeEditState.pConstCostume, "CostumeEditor.", 0, 0, 0);
		WLCostume *pWLCostume = wlCostumeFromName(pCostumeName);
		if( pWLCostume )
		{
			return pWLCostume->bComplete;
		}
	}
	return false;
}

AUTO_FIXUPFUNC;
TextParserResult CostumeCreator_FixupCostumeBoneValidValues(CostumeBoneValidValues *pValidValues, enumTextParserFixupType eType, void *pExtraData)
{
	if (eType == FIXUPTYPE_DESTRUCTOR)
	{
		// Cleanup the geos array
		CostumeBoneValidValues_ResetLists(pValidValues);
	}

	return PARSERESULT_SUCCESS;
}

#include "gclCostumeUIState_h_ast.c"
#include "gclCostumeUI_c_ast.c"
