/* Tailor specific support code: editable pets, editable slots */

#include "gclCostumeUI.h"
#include "gclCostumeUIState.h"
#include "gclCostumeView.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonTailor.h"
#include "CostumeCommonLoad.h"
#include "GameClientLib.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "Character.h"
#include "gclEntity.h"
#include "UIGen.h"
#include "GameAccountDataCommon.h"
#include "CharacterCreationUI.h"
#include "gclBaseStates.h"
#include "GlobalStateMachine.h"
#include "LoginCommon.h"
#include "gclLogin.h"
#include "CharacterSelection.h"
#include "gclCostumeUnlockUI.h"
#include "PowerAnimFX.h"
#include "Guild.h"
#include "Player.h"
#include "StringCache.h"
#include "PowerTree.h"
#include "contact_common.h"
#include "mission_common.h"
#include "gclLogin2.h"
#include "Login2Common.h"

#include "AutoGen/gclCostumeUIState_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

extern Login2CharacterCreationData *g_CharacterCreationData;

#define UI_FIXUP_STORAGE_TYPE(eType) ((eType) < 0 ? kPCCostumeStorageType_Primary : (eType) >= kPCCostumeStorageType_Count ? kPCCostumeStorageType_Primary : (eType))

void CostumeCreator_StoreCostumeLoginHelper(Entity *pPlayerEnt, Entity *pEnt, S32 iIndex, NOCONST(PlayerCostume) *pCostume, PCSlotType *pSlotType, PCSlotDef *pSlotDef, PCSlotSet *pSlotSet, PlayerCostumeSlot *pStoreSlot);

static Entity ***CostumeCreator_GetEntityList(SA_PARAM_OP_VALID Entity *pEnt, PCCostumeStorageType eStorageType)
{
	static struct {
		U32 uCacheTime;
		Entity **eaEnts;
		Entity *pEnt;
	} s_Cache[kPCCostumeStorageType_Count + 1];

	if (eStorageType < 0 || eStorageType >= kPCCostumeStorageType_Count)
	{
		return &s_Cache[kPCCostumeStorageType_Count].eaEnts;
	}

	if (s_Cache[eStorageType].pEnt != pEnt || s_Cache[eStorageType].uCacheTime != gGCLState.totalElapsedTimeMs)
	{
		costumeEntity_GetStoreCostumeEntities(pEnt, eStorageType, &s_Cache[eStorageType].eaEnts);
		s_Cache[eStorageType].pEnt = pEnt;
		s_Cache[eStorageType].uCacheTime = gGCLState.totalElapsedTimeMs;
	}

	return &s_Cache[eStorageType].eaEnts;
}

// StarTrek
// Get the list of skeletons available for the current costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPlayerPetList");
void CostumeCreator_GetPlayerPetList(SA_PARAM_NN_VALID UIGen *pGen)
{
	Entity ***peaEntities;
	COSTUME_UI_TRACE_FUNC();
	peaEntities = CostumeCreator_GetEntityList(entActivePlayerPtr(), kPCCostumeStorageType_Pet);
	ui_GenSetListSafe(pGen, peaEntities, Entity);
}

// Night, Bronze, StarTrek
// Get a pet entity by index
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPetByPetNum");
SA_RET_OP_VALID Entity *CostumeCreator_GetPetByPetNum(int petnum)
{
	Entity ***peaEntities;
	COSTUME_UI_TRACE_FUNC();
	peaEntities = CostumeCreator_GetEntityList(entActivePlayerPtr(), kPCCostumeStorageType_Pet);
	return eaGet(peaEntities, petnum);
}

// StarTrek
// Get the index of a pet entity by container id
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPetNumFromPetID");
int CostumeCreator_GetPetNumFromPetID(SA_PARAM_NN_VALID Entity *pEnt, ContainerID petID)
{
	Entity ***peaEntities;
	S32 i;
	COSTUME_UI_TRACE_FUNC();
	peaEntities = CostumeCreator_GetEntityList(pEnt, kPCCostumeStorageType_Pet);
	for (i = eaSize(peaEntities) - 1; i >= 0; i--) {
		if (entGetContainerID((*peaEntities)[i]) == petID) {
			return i;
		}
	}
	return 0;
}

// Night, Bronze, StarTrek
// Get the name of the pet entity by index
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPetName");
const char *CostumeCreator_GetPetName(int petnum)
{
	Entity *pEnt = CostumeCreator_GetPetByPetNum(petnum);
	COSTUME_UI_TRACE_FUNC();
	return pEnt && pEnt->pSaved ? pEnt->pSaved->savedName : "Unknown";
}

// Night, Bronze, StarTrek
// Get the pet entity's active costume type (Lies! This is actually equivalent to CostumeCreator_GetPetActiveCostumeIndex. The concept of a CostumeType no longer exists.)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPetActiveCostumeType");
int CostumeCreator_GetPetActiveCostumeIndex(int petnum);

// Night, Bronze
// Get the pet entity's active costume index
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPetActiveCostumeIndex");
int CostumeCreator_GetPetActiveCostumeIndex(int petnum)
{
	Entity *pEnt = CostumeCreator_GetPetByPetNum(petnum);
	COSTUME_UI_TRACE_FUNC();
	return SAFE_MEMBER2(pEnt, pSaved, costumeData.iActiveCostume);
}

// Night, Bronze, FightClub
// Get the list of player costumes available
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPlayerCostumeList");
void CostumeCreator_GetPlayerCostumeList(SA_PARAM_NN_VALID UIGen *pGen, /* PCCostumeStorageType */ int eCostumeType, int iPetNum, bool bIncludeEmpty)
{
	static PlayerCostume **s_eaCostumes;
	Entity ***peaEntities;
	Entity *pEnt;

	COSTUME_UI_TRACE_FUNC();

	peaEntities = CostumeCreator_GetEntityList(entActivePlayerPtr(), eCostumeType);
	pEnt = eaGet(peaEntities, iPetNum);

	if (pEnt)
	{
		// Get costume slot earray
		const PlayerCostumeSlot *const *eaSlots = pEnt->pSaved ? pEnt->pSaved->costumeData.eaCostumeSlots : NULL;
		S32 i, iCostumeSlots = eaSize(&eaSlots);

		// Determine how many slots to fill
		if (bIncludeEmpty)
			iCostumeSlots = costumeEntity_trh_GetNumCostumeSlots(CONTAINER_NOCONST(Entity, pEnt), CONTAINER_NOCONST(GameAccountData, entity_GetGameAccount(pEnt)));

		// Fill array
		for (i = 0; i < iCostumeSlots; i++)
		{
			const PlayerCostumeSlot *pSlot = eaGet(&eaSlots, i);
			eaPush(&s_eaCostumes, pSlot ? pSlot->pCostume : NULL);
		}
	}

	ui_GenSetListSafe(pGen, &s_eaCostumes, PlayerCostume);
	eaClear(&s_eaCostumes);
}

// Night, Bronze, FightClub
// Get the size of the list of player costumes available
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPlayerCostumeListSize");
int CostumeCreator_GetPlayerCostumeListSize(/* PCCostumeStorageType */ int eCostumeType, int iPetNum, bool bIncludeEmpty)
{
	static PlayerCostume **s_eaCostumes;
	Entity ***peaEntities;
	Entity *pEnt;

	COSTUME_UI_TRACE_FUNC();

	peaEntities = CostumeCreator_GetEntityList(entActivePlayerPtr(), eCostumeType);
	pEnt = eaGet(peaEntities, iPetNum);

	if (pEnt)
	{
		// Get costume slot earray
		const PlayerCostumeSlot *const *eaSlots = pEnt->pSaved ? pEnt->pSaved->costumeData.eaCostumeSlots : NULL;

		// Return slot count
		if (bIncludeEmpty)
			return costumeEntity_trh_GetNumCostumeSlots(CONTAINER_NOCONST(Entity, pEnt), CONTAINER_NOCONST(GameAccountData, entity_GetGameAccount(pEnt)));
		else
			return eaSize(&eaSlots);
	}

	return 0;
}

// StarTrek
// Get the size of the list of player costumes available
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeUI_CanPlayerChangeCostume");
bool CostumeCreator_CanPlayerChangeCostume(SA_PARAM_OP_VALID Entity *pEnt)
{
	NOCONST(PlayerCostume) *pCostume = NULL;
	int iCostumeIndex = 0;
	COSTUME_UI_TRACE_FUNC();
	if (pEnt && pEnt->pSaved && iCostumeIndex >= 0 && iCostumeIndex < eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots))
	{
		pCostume = CONTAINER_NOCONST(PlayerCostume, pEnt->pSaved->costumeData.eaCostumeSlots[iCostumeIndex]->pCostume);
	}
	return SAFE_MEMBER(pCostume, bPlayerCantChange);
}

STATIC_ASSERT_MESSAGE(kPCCostumeStorageType_Count < 16, "CostumeCreator_GetCostumeSlots assumes that the number of costume storage types is less than < 16 to work it's bit hack magic");
static S32 CostumeCreator_GetCostumeSlots(UICostumeSlot ***peaSlots, PCCostumeStorageType eStorageType, S32 iPetNum, bool bHideInactive)
{
	static Entity **s_eaFillEntities;
	Entity ***peaEntities;
	Entity *pEnt, *pPlayerEnt = entActivePlayerPtr();
	S32 iEnt, iTotalCount = 0, iSlotCount = 0;
	bool bEntityHeaders = false;

	// Determine entities to get the slots for
	if (eStorageType < 65536)
	{
		// A simple costume type
		peaEntities = CostumeCreator_GetEntityList(pPlayerEnt, eStorageType);
		pEnt = eaGet(peaEntities, iPetNum);

		if (pEnt)
		{
			eaPush(&s_eaFillEntities, pEnt);
		}
		else if (iPetNum < 0)
		{
			eaCopy(&s_eaFillEntities, peaEntities);
			bEntityHeaders = true;
		}
	}
	else
	{
		S32 iType;

		// Handle all the storage type flags
		for (iType = 0; iType < kPCCostumeStorageType_Count; iType++)
		{
			if (iType == kPCCostumeStorageType_Secondary)
				continue;

			if ((eStorageType & (1 << iType)) == 0)
			{
				if (iType != kPCCostumeStorageType_Primary || (eStorageType & (1 << kPCCostumeStorageType_Secondary)) == 0)
					continue;
			}

			peaEntities = CostumeCreator_GetEntityList(pPlayerEnt, iType);
			eaPushEArray(&s_eaFillEntities, peaEntities);
		}

		pEnt = eaGet(&s_eaFillEntities, iPetNum);
		if (pEnt)
		{
			eaSet(&s_eaFillEntities, pEnt, 0);
			eaSetSize(&s_eaFillEntities, 1);
		}
		else if (iPetNum < 0)
		{
			bEntityHeaders = true;
		}
		else
		{
			eaClear(&s_eaFillEntities);
		}
	}

	// Add all costume slots from the entity list
	for (iEnt = 0; iEnt < eaSize(&s_eaFillEntities); iEnt++)
	{
		const PlayerCostumeSlot *const *eaSlots;
		PCSlotDef *pExtraSlotDef;
		S32 iExtraID, iSlotIndex, iCostumeSlots;

		// Get parameters from the entity
		pEnt = s_eaFillEntities[iEnt];
		eaSlots = pEnt->pSaved ? pEnt->pSaved->costumeData.eaCostumeSlots : NULL;
		pExtraSlotDef = costumeEntity_GetExtraSlotDef(pEnt);
		iExtraID = SAFE_MEMBER(pExtraSlotDef, iSlotID);
		iCostumeSlots = eaSize(&eaSlots);

		// Determine how many slots to fill
		if (!bHideInactive)
			iCostumeSlots = costumeEntity_trh_GetNumCostumeSlots(CONTAINER_NOCONST(Entity, pEnt), CONTAINER_NOCONST(GameAccountData, entity_GetGameAccount(pEnt)));

		// Add header entry
		if (bEntityHeaders && peaSlots)
		{
			UICostumeSlot *pUISlot = eaGetStruct(peaSlots, parse_UICostumeSlot, iCostumeSlots++);

			// Set slot entity
			pUISlot->pEntity = pEnt;

			// Set slot storage information
			pUISlot->eStorageType = eStorageType;
			pUISlot->uContainerID = entGetContainerID(pEnt);
			pUISlot->iIndex = -1;

			// Fill in slot info
			pUISlot->bIsHeader = true;
			pUISlot->iSlotID = 0;
			pUISlot->pCostume = NULL;
			pUISlot->pSlotDef = NULL;
			pUISlot->pSlotType = NULL;
			pUISlot->bIsUnlocked = true;
		}

		// Add entity slots
		for (iSlotIndex = 0; iSlotIndex < iCostumeSlots; iSlotIndex++)
		{
			const PlayerCostumeSlot *pSlot = eaGet(&eaSlots, iSlotIndex);
			PCSlotDef *pSlotDef = !pSlot || iExtraID == pSlot->iSlotID ? pExtraSlotDef : costumeEntity_GetSlotDef(pEnt, pSlot->iSlotID);
			bool bActive = (!bHideInactive || pSlot && pSlot->pCostume) && !costumeEntity_IsSlotHidden(pPlayerEnt, pEnt, pSlotDef);
			UICostumeSlot *pUISlot = peaSlots && bActive ? eaGetStruct(peaSlots, parse_UICostumeSlot, iSlotCount++) : NULL;

			if (bActive)
				iTotalCount++;

			if (pUISlot)
			{
				// Determine slot type
				const char *pcSlotType = pSlot && pSlot->pcSlotType && *pSlot->pcSlotType ? pSlot->pcSlotType : NULL;
				if (!pcSlotType && pSlotDef)
					pcSlotType = pSlotDef->pcSlotType;

				// Set slot entity
				pUISlot->pEntity = pEnt;

				// Set slot storage information
				pUISlot->eStorageType = eStorageType;
				pUISlot->uContainerID = entGetContainerID(pEnt);
				pUISlot->iIndex = iSlotIndex;

				// Fill in slot info
				pUISlot->bIsHeader = false;
				pUISlot->iSlotID = pSlot ? pSlot->iSlotID : iExtraID;
				pUISlot->pCostume = pSlot ? pSlot->pCostume : NULL;
				pUISlot->pSlotDef = pSlotDef;
				pUISlot->pSlotType = costumeLoad_GetSlotType(pcSlotType);
				pUISlot->bIsUnlocked = costumeEntity_IsSlotUnlocked(pPlayerEnt, pEnt, pSlotDef) && (!pUISlot->pCostume || !pUISlot->pCostume->bPlayerCantChange);
			}
		}
	}

	eaClear(&s_eaFillEntities);

	if (peaSlots)
		eaSetSizeStruct(peaSlots, parse_UICostumeSlot, iSlotCount);
	return iTotalCount;
}

// StarTrek
// Get the list of player costumes available
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPlayerCostumeSlotList");
void CostumeCreator_GetPlayerCostumeSlotList(SA_PARAM_NN_VALID UIGen *pGen, /* PCCostumeStorageType */ int eCostumeType, int iPetNum)
{
	UICostumeSlot ***peaCostumeSlots = ui_GenGetManagedListSafe(pGen, UICostumeSlot);
	COSTUME_UI_TRACE_FUNC();
	CostumeCreator_GetCostumeSlots(peaCostumeSlots, eCostumeType, iPetNum, false);
	ui_GenSetManagedListSafe(pGen, peaCostumeSlots, UICostumeSlot, true);
}

// StarTrek
// Get the list of player costumes available
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPlayerActiveCostumeSlotList");
void CostumeCreator_GetPlayerActiveCostumeSlotList(SA_PARAM_NN_VALID UIGen *pGen, /* PCCostumeStorageType */ int eCostumeType, int iPetNum)
{
	UICostumeSlot ***peaCostumeSlots = ui_GenGetManagedListSafe(pGen, UICostumeSlot);
	COSTUME_UI_TRACE_FUNC();
	CostumeCreator_GetCostumeSlots(peaCostumeSlots, eCostumeType, iPetNum, true);
	ui_GenSetManagedListSafe(pGen, peaCostumeSlots, UICostumeSlot, true);
}

// StarTrek
// Get the size of the list of player costumes available
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPlayerCostumeSlotListSize");
int CostumeCreator_GetPlayerCostumeSlotListSize(/* PCCostumeStorageType */ int eCostumeType, int iPetNum)
{
	COSTUME_UI_TRACE_FUNC();
	return CostumeCreator_GetCostumeSlots(NULL, eCostumeType, iPetNum, false);
}

// StarTrek
// Get the size of the list of player costumes available
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_GetPlayerActiveCostumeSlotListSize");
int CostumeCreator_GetPlayerActiveCostumeSlotListSize(/* PCCostumeStorageType */ int eCostumeType, int iPetNum)
{
	COSTUME_UI_TRACE_FUNC();
	return CostumeCreator_GetCostumeSlots(NULL, eCostumeType, iPetNum, true);
}

// Start editing a costume
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CostumeCreator_StartEditCostume") ACMD_HIDE;
void CostumeCreator_StartEditCostumeExpr(void);

// Night, Bronze, FightClub, StarTrek
// TODO: delete me this should no longer be necessary.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_StartEditCostume");
void CostumeCreator_StartEditCostumeExpr(void)
{
	// TODO: Do nothing. Make edit costume automatically managed
	CostumeCreator_StartEditCostume();
}

// If we are on the ground get the spaceship
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CharacterCreation.STOCopyCostume") ACMD_HIDE;
void CharacterCreation_STOCopyCostume(bool bGenerateSkeletonFX);

// StarTrek
// If we are on the ground get the spaceship
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_STOCopyCostume");
void CharacterCreation_STOCopyCostume(bool bGenerateSkeletonFX)
{
	PuppetEntity *pPuppet = NULL;
	Entity *pEnt = entActivePlayerPtr();
	U32 uShipContainerID = 0;
	CharClassTypes eSpaceClass = StaticDefineIntGetInt(CharClassTypesEnum, "Space");
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);

	COSTUME_UI_TRACE_FUNC();

	if (g_pCostumeView)
		g_pCostumeView->costume.bIgnoreSkelFX = !bGenerateSkeletonFX;

	// First check the contact for the puppet to edit
	if (pEnt && pEnt->pSaved && pEnt->pSaved->pPuppetMaster && pDialog)
	{
		S32 i;
		for (i = eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets) - 1; i >= 0; i--)
		{
			if (pEnt->pSaved->pPuppetMaster->ppPuppets[i]->curID == pDialog->uiShipTailorEntityID)
			{
				pPuppet = pEnt->pSaved->pPuppetMaster->ppPuppets[i];
				break;
			}
		}
	}

	// Try picking the active preferred puppet
	if (pEnt && pEnt->pSaved && pEnt->pSaved->pPuppetMaster && !pPuppet)
		pPuppet = entity_GetPuppetByTypeEx(pEnt, eSpaceClass, GET_REF(pEnt->pSaved->pPuppetMaster->hPreferredCategorySet), true);

	// Try picking any active puppet
	if (!pPuppet)
		pPuppet = entity_GetPuppetByTypeEx(pEnt, eSpaceClass, NULL, true);

	// Try picking any preferred puppet
	if (pEnt && pEnt->pSaved && pEnt->pSaved->pPuppetMaster && !pPuppet)
		pPuppet = entity_GetPuppetByTypeEx(pEnt, eSpaceClass, GET_REF(pEnt->pSaved->pPuppetMaster->hPreferredCategorySet), false);

	// Try picking any puppet
	if (!pPuppet)
		pPuppet = entity_GetPuppetByTypeEx(pEnt, eSpaceClass, NULL, false);

	if (pPuppet)
	{
		Entity *pPuppetEnt = GET_REF(pPuppet->hEntityRef);
		if (pPuppetEnt)
		{
			uShipContainerID = entGetContainerID(pPuppetEnt);
			StructCopyString(&g_CostumeEditState.pcNemesisName, entGetLocalName(pPuppetEnt));
		}
	}

	CostumeCreator_CopyCostumeFromContainer(kPCCostumeStorageType_SpacePet, uShipContainerID, 0);
}

// Copy a costume from the current player
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CharacterCreation.CopyPlayerCostume") ACMD_HIDE;
void CharacterCreation_CopyPlayerCostume(/* PCCostumeStorageType */ int eCostumeType, int iPetNum, int iCostumeIndex);

// Night, Bronze, FightClub, StarTrek
// Copy a costume from the current player
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_CopyPlayerCostume");
void CharacterCreation_CopyPlayerCostume(/* PCCostumeStorageType */ int eCostumeType, int iPetNum, int iCostumeIndex)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeCreator_CopyCostumeFromPet(UI_FIXUP_STORAGE_TYPE(eCostumeType), iPetNum, iCostumeIndex);
}

// Save the costume to a given pet
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SaveCostume");
void CostumeCreator_SaveCostume_Expr(/* PCCostumeStorageType */ int eCostumeType, int iPetNum, int iCostumeIndex)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeCreator_StoreCostumeToPet(UI_FIXUP_STORAGE_TYPE(eCostumeType), iPetNum, iCostumeIndex, kPCPay_Default);
}

// Save the costume to a given pet with a given payment method
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SaveCostume_WithPayment");
void CostumeCreator_SaveCostume_WithPayment_Expr(/* PCCostumeStorageType */ int eCostumeType, int iPetNum, int iCostumeIndex, const char *pchPayMethod)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeCreator_StoreCostumeToPet(UI_FIXUP_STORAGE_TYPE(eCostumeType), iPetNum, iCostumeIndex, StaticDefineIntGetInt(PCPaymentMethodEnum, pchPayMethod));
}

// Save the costume to a given pet with a given payment method
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CostumeCreatorCmd_SaveCostume") ACMD_HIDE;
void CostumeCreator_SaveCostume_Cmd(PCCostumeStorageType eCostumeType, int iPetNum, int iCostumeIndex, PCPaymentMethod ePayMethod)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeCreator_StoreCostumeToPet(UI_FIXUP_STORAGE_TYPE(eCostumeType), iPetNum, iCostumeIndex, ePayMethod);
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CostumeCreator_SaveCurrentCostume") ACMD_HIDE;
void CostumeCreator_SaveCurrentCostume_Cmd(PCPaymentMethod ePayMethod)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeCreator_StoreCostumeToContainer(g_CostumeEditState.eCostumeStorageType, g_CostumeEditState.uCostumeEntContainerID, g_CostumeEditState.iCostumeIndex, ePayMethod);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SaveCurrentCostume");
void CostumeCreator_SaveCurrentCostume_Expr(/* PCPaymentMethod */ int ePayMethod)
{
	COSTUME_UI_TRACE_FUNC();
	CostumeCreator_StoreCostumeToContainer(g_CostumeEditState.eCostumeStorageType, g_CostumeEditState.uCostumeEntContainerID, g_CostumeEditState.iCostumeIndex, ePayMethod);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_SetInitialCostume");
void CostumeCreator_SetInitialCostume(const char *pcCostumeName)
{
	PlayerCostume *pCostume = RefSystem_ReferentFromString("PlayerCostume", pcCostumeName);
	if (pCostume)
	{
		if (IS_HANDLE_ACTIVE(pCostume->hSkeleton))
		{
			// Use new skeleton
			COPY_HANDLE(g_CostumeEditState.hSkeleton, pCostume->hSkeleton);
		}

		if (IS_HANDLE_ACTIVE(pCostume->hSpecies))
		{
			// Use new species
			COPY_HANDLE(g_CostumeEditState.hSpecies, pCostume->hSpecies)
		}
		else
		{
			// Don't use species
			REMOVE_HANDLE(g_CostumeEditState.hSpecies);
		}

		if (CostumeCreator_SetStartCostume(StructCloneDeConst(parse_PlayerCostume, pCostume)))
			CostumeUI_RegenCostumeEx(true, true);
	}
}

//
//
//////////////////////////////////////////////////////////////////////////////////
// Helper API
//

Entity *CostumeCreator_GetPlayerEntity(U32 uContainerID)
{
	if (GSM_IsStateActive(GCL_LOGIN))
	{
		Entity *pSelectionEntity;

		if (uContainerID != 0)
		{
            return gclLogin2_CharacterDetailCache_GetEntity(uContainerID);
		}

		if (uContainerID == 0 && g_pFakePlayer)
			return CONTAINER_RECONST(Entity, g_pFakePlayer);

		pSelectionEntity = (uContainerID == 0) ? gclLogin2_CharacterDetailCache_GetEntity(g_CharacterSelectionPlayerId) : NULL;
		if (pSelectionEntity)
			return pSelectionEntity;
	}

	return entActivePlayerPtr();
}

Entity *CostumeCreator_GetStoreCostumeEntityFromContainer(PCCostumeStorageType eStorageType, U32 uContainerID, S32 iIndex)
{
	Entity *pEnt = CostumeCreator_GetPlayerEntity(uContainerID);
	return costumeEntity_GetStoreCostumeEntity(pEnt, eStorageType, uContainerID);
}

bool CostumeCreator_GetStoreCostumeSlotFromContainer(PCCostumeStorageType eStorageType, U32 uContainerID, S32 iIndex, PCSlotDef **ppSlotDef, PlayerCostumeSlot **ppCostumeSlot)
{
	Entity *pSubEnt, *pEnt = CostumeCreator_GetPlayerEntity(uContainerID);

	pSubEnt = costumeEntity_GetStoreCostumeEntity(pEnt, eStorageType, uContainerID);
	if (pSubEnt)
		return costumeEntity_GetStoreCostumeSlot(pEnt, pSubEnt, iIndex, ppSlotDef, ppCostumeSlot);

	if (ppSlotDef)
		*ppSlotDef = NULL;
	if (ppCostumeSlot)
		*ppCostumeSlot = NULL;

	return false;
}

Entity *CostumeCreator_GetStoreCostumeEntityFromPet(PCCostumeStorageType eStorageType, int iPetNum, S32 iIndex)
{
	Entity ***peaEntities, *pEnt = CostumeCreator_GetPlayerEntity(0);
	peaEntities = CostumeCreator_GetEntityList(pEnt, eStorageType);
	return eaGet(peaEntities, iPetNum);
}

bool CostumeCreator_GetStoreCostumeSlotFromPet(PCCostumeStorageType eStorageType, int iPetNum, S32 iIndex, PCSlotDef **ppSlotDef, PlayerCostumeSlot **ppCostumeSlot)
{
	Entity *pSubEnt, ***peaEntities, *pEnt = CostumeCreator_GetPlayerEntity(0);

	peaEntities = CostumeCreator_GetEntityList(pEnt, eStorageType);
	pSubEnt = eaGet(peaEntities, iPetNum);
	if (pSubEnt)
		return costumeEntity_GetStoreCostumeSlot(pEnt, pSubEnt, iIndex, ppSlotDef, ppCostumeSlot);

	if (ppSlotDef)
		*ppSlotDef = NULL;
	if (ppCostumeSlot)
		*ppCostumeSlot = NULL;

	return false;
}

Entity *CostumeCreator_GetEditPlayerEntity(void)
{
	Entity *pEnt = CostumeCreator_GetEditEntity();
	return CostumeCreator_GetPlayerEntity(pEnt && pEnt->pSaved && pEnt->pSaved->conOwner.containerID ? pEnt->pSaved->conOwner.containerID : pEnt ? entGetContainerID(pEnt) : 0);
}

Entity *CostumeCreator_GetEditEntity(void)
{
	return CostumeCreator_GetStoreCostumeEntityFromContainer(
		g_CostumeEditState.eCostumeStorageType,
		g_CostumeEditState.uCostumeEntContainerID,
		g_CostumeEditState.iCostumeIndex
	);
}

static bool CostumeCreator_EntIsPet(Entity *pPlayerEnt, Entity *pEnt)
{
	if (pPlayerEnt == pEnt)
		return false;

	if (pPlayerEnt && pPlayerEnt->pSaved && pPlayerEnt->pSaved->pPuppetMaster)
	{
		S32 i;
		for (i = 0; i < eaSize(&pPlayerEnt->pSaved->pPuppetMaster->ppPuppets); i++)
		{
			if (pPlayerEnt->pSaved->pPuppetMaster->ppPuppets[i]->curType == entGetType(pEnt)
				&& pPlayerEnt->pSaved->pPuppetMaster->ppPuppets[i]->curID == entGetContainerID(pEnt))
			{
				return false;
			}
		}
	}

	return true;
}

bool CostumeCreator_SetStartCostume(NOCONST(PlayerCostume) *pCostume)
{
	if (!g_CostumeEditState.pStartCostume && !g_CostumeEditState.pCostume
		|| g_CostumeEditState.pStartCostume && StructCompare(parse_PlayerCostume, g_CostumeEditState.pStartCostume, pCostume, 0, 0, 0)
		|| g_CostumeEditState.pCostume && (g_CostumeEditState.pCostume->eCostumeType != kPCCostumeType_Unrestricted && StructCompare(parse_PlayerCostume, g_CostumeEditState.pCostume, pCostume, 0, 0, 0))
		)
	{
		StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pStartCostume);
		StructDestroyNoConstSafe(parse_PlayerCostume, &g_CostumeEditState.pCostume);
		g_CostumeEditState.pStartCostume = pCostume;
		g_CostumeEditState.pCostume = StructCloneNoConst(parse_PlayerCostume, pCostume);
		g_CostumeEditState.pCostume->eCostumeType = kPCCostumeType_Unrestricted;
		g_CostumeEditState.pCostume->pcFileName = NULL;
		COPY_HANDLE(g_CostumeEditState.pCostume->hSpecies, g_CostumeEditState.hSpecies);
		COPY_HANDLE(g_CostumeEditState.pCostume->hSkeleton, g_CostumeEditState.hSkeleton);
		return true;
	}
	else
	{
		StructDestroyNoConst(parse_PlayerCostume, pCostume);
	}

	return false;
}

void CostumeCreator_CopyCostumeFromEnt(PCCostumeStorageType eStorageType, Entity *pEnt, S32 iIndex)
{
	Entity *pPlayerEnt = CostumeCreator_GetPlayerEntity(pEnt && pEnt->pSaved && pEnt->pSaved->conOwner.containerID ? pEnt->pSaved->conOwner.containerID : pEnt ? entGetContainerID(pEnt) : 0);
	PlayerCostumeSlot *pCostumeSlot;
	PCSlotDef *pSlotDef;
	PCSlotType *pSlotType = NULL;
	PCSlotSet *pSlotSet;
	GameAccountData *pGameAccount;
	GameAccountDataExtract *pExtract;
	NOCONST(PlayerCostume) *pCostume;
	bool bChanged = false;

	if (!costumeEntity_GetStoreCostumeSlot(pPlayerEnt, pEnt, iIndex, &pSlotDef, &pCostumeSlot))
		return;

	if (pCostumeSlot && EMPTY_TO_NULL(pCostumeSlot->pcSlotType))
		pSlotType = costumeLoad_GetSlotType(pCostumeSlot->pcSlotType);
	if (!pSlotType && pSlotDef && EMPTY_TO_NULL(pSlotDef->pcSlotType))
		pSlotType = costumeLoad_GetSlotType(pSlotDef->pcSlotType);
	pSlotSet = costumeEntity_GetSlotSet(pEnt, CostumeCreator_EntIsPet(pPlayerEnt, pEnt));

	pGameAccount = entity_GetGameAccount(pPlayerEnt);
	pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
	pCostume = pCostumeSlot && pCostumeSlot->pCostume ? StructCloneDeConst(parse_PlayerCostume, pCostumeSlot->pCostume) : NULL;

	// Set the species
	if (pEnt && pEnt->pChar && IS_HANDLE_ACTIVE(pEnt->pChar->hSpecies))
	{
		COPY_HANDLE(g_CostumeEditState.hSpecies, pEnt->pChar->hSpecies);
	}
	else if (pCostume && IS_HANDLE_ACTIVE(pCostume->hSpecies))
	{
		COPY_HANDLE(g_CostumeEditState.hSpecies, pCostume->hSpecies);
	}
	else
	{
		REMOVE_HANDLE(g_CostumeEditState.hSpecies);
	}

	// Set the skeleton
	if (pCostume && IS_HANDLE_ACTIVE(pCostume->hSkeleton))
	{
		COPY_HANDLE(g_CostumeEditState.hSkeleton, pCostume->hSkeleton);
	}
	else
	{
		NOCONST(PlayerCostume) Dummy = {0};
		PCSkeletonDef **eaSkels = NULL;
		Dummy.eCostumeType = kPCCostumeType_Player;
		costumeTailor_GetValidSkeletons(&Dummy, GET_REF(g_CostumeEditState.hSpecies), &eaSkels, false, true);
		if (eaSize(&eaSkels) > 0)
			SET_HANDLE_FROM_REFERENT(g_hCostumeSkeletonDict, eaSkels[0], g_CostumeEditState.hSkeleton);
		else
			REMOVE_HANDLE(g_CostumeEditState.hSkeleton);
		eaDestroy(&eaSkels);
	}

	// Set slot stuff
	g_CostumeEditState.pSlotType = pSlotType;
	g_CostumeEditState.pSlotDef = pSlotDef;
	g_CostumeEditState.pcSlotSet = SAFE_MEMBER(pSlotSet, pcName);
	g_CostumeEditState.iSlotID = SAFE_MEMBER(pSlotDef, iSlotID);
	g_CostumeEditState.bExtraSlot = SAFE_MEMBER(pSlotSet, pExtraSlotDef) == pSlotDef;

	// Prepare for editing
	g_CostumeEditState.bUnlockAll = SAFE_MEMBER2(pEnt, pSaved, costumeData.bUnlockAll) || SAFE_MEMBER2(pPlayerEnt, pSaved, costumeData.bUnlockAll);
	CostumeUI_SetUnlockedCostumes(true, true, pPlayerEnt, pEnt);
	CostumeUI_ClearSelections();

	if (pEnt)
	{
		entity_FindPowerFXBones(pEnt, &g_CostumeEditState.eaPowerFXBones);
	}
    else if (GSM_IsStateActive(GCL_LOGIN_NEW_CHARACTER_CREATION) && g_CharacterCreationData)
	{
		S32 i, j;

		// Get PowerFX bones from character creation
		eaClear(&g_CostumeEditState.eaPowerFXBones);
		for(i = eaSize(&g_CharacterCreationData->powerNodes) - 1; i >= 0; --i)
		{
			PTNodeDef *pNodeDef = powertreenodedef_Find(g_CharacterCreationData->powerNodes[i]);
			if(pNodeDef && eaSize(&pNodeDef->ppRanks))
			{
				PTNodeRankDef *pRankDef = pNodeDef->ppRanks[0];
				if(pRankDef)
				{
					PowerDef *pdef = GET_REF(pRankDef->hPowerDef);
					PowerAnimFX *pafx = pdef ? GET_REF(pdef->hFX) : NULL;

					if(pafx && pafx->cpchPCBoneName)
						eaPushUnique(&g_CostumeEditState.eaPowerFXBones, pafx->cpchPCBoneName);

					if (pdef && pdef->eType == kPowerType_Combo)
					{
						// Is it possible to have recursive combo powers?
						for (j = eaSize(&pdef->ppCombos) - 1; j >= 0; --j)
						{
							PowerDef *pComboDef = GET_REF(pdef->ppCombos[j]->hPower);
							pafx = pComboDef ? GET_REF(pComboDef->hFX) : NULL;
							if (pafx && pafx->cpchPCBoneName)
								eaPushUnique(&g_CostumeEditState.eaPowerFXBones, pafx->cpchPCBoneName);
						}
					}
				}
			}
		}
	}
	else
	{
		eaClear(&g_CostumeEditState.eaPowerFXBones);
	}

	// Initialize costume
	if (!pCostume)
	{
		PlayerCostume *pActive = costumeEntity_GetActiveSavedCostume(pEnt);
		if (pActive)
		{
			pCostume = StructCloneDeConst(parse_PlayerCostume, pActive);
			costumeTailor_MakeCostumeValid(pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, true, g_CostumeEditState.bUnlockAll, true, guild_GetGuild(pPlayerEnt), false, pExtract, false, g_CostumeEditState.eaPowerFXBones);
		}
	}
	if (!pCostume)
	{
		S32 i;
		// TODO: make this aware of slot types and prefer a costume from a similar slot type
		// instead of pulling the first available costume.
		for (i = 0; i < eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots); i++)
		{
			if (pEnt->pSaved->costumeData.eaCostumeSlots[i]->pCostume)
			{
				pCostume = StructCloneDeConst(parse_PlayerCostume, pEnt->pSaved->costumeData.eaCostumeSlots[i]->pCostume);
				costumeTailor_MakeCostumeValid(pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, true, g_CostumeEditState.bUnlockAll, true, guild_GetGuild(pPlayerEnt), false, pExtract, false, g_CostumeEditState.eaPowerFXBones);
				break;
			}
		}
	}
	if (!pCostume)
	{
		pCostume = StructCreateNoConst(parse_PlayerCostume);
		pCostume->eCostumeType = kPCCostumeType_Player;
		COPY_HANDLE(pCostume->hSpecies, g_CostumeEditState.hSpecies);
		COPY_HANDLE(pCostume->hSkeleton, g_CostumeEditState.hSkeleton);
		costumeTailor_SetDefaultSkinColor(pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.pSlotType);
		costumeTailor_FillAllBones(pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, true, false, true);
		costumeTailor_MakeCostumeValid(pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, true, g_CostumeEditState.bUnlockAll, true, guild_GetGuild(pPlayerEnt), false, pExtract, false, g_CostumeEditState.eaPowerFXBones);
	}
	if (!pCostume)
	{
		// No costume to copy or create
		return;
	}

	bChanged = CostumeCreator_SetStartCostume(pCostume) || bChanged;

	if (!REF_COMPARE_HANDLES(pEnt->costumeRef.hMood, g_CostumeEditState.hMood))
	{
		COPY_HANDLE(g_CostumeEditState.hMood, pEnt->costumeRef.hMood);
		bChanged = true;
	}

	// Save information about the copied costume
	g_CostumeEditState.eCostumeStorageType = eStorageType;
	g_CostumeEditState.uCostumeEntContainerID = pEnt ? entGetContainerID(pEnt) : 0;
	g_CostumeEditState.iCostumeIndex = iIndex;

	// Check to see if the costume is invalid
	g_CostumeEditState.bCostumeChangeIsFree = pEnt && costumeEntity_TestCostumeForFreeChange(pPlayerEnt, pEnt, iIndex);

	// Refresh costume view
	if (bChanged)
		CostumeUI_RegenCostumeEx(true, true);
}

void CostumeCreator_CopyCostumeFromContainer(PCCostumeStorageType eStorageType, ContainerID uContainerID, S32 iIndex)
{
	Entity *pEnt = CostumeCreator_GetStoreCostumeEntityFromContainer(eStorageType, uContainerID, iIndex);
	CostumeCreator_CopyCostumeFromEnt(eStorageType, pEnt, iIndex);
}

void CostumeCreator_CopyCostumeFromPet(PCCostumeStorageType eStorageType, int iPetNum, S32 iIndex)
{
	Entity *pEnt = CostumeCreator_GetStoreCostumeEntityFromPet(eStorageType, iPetNum, iIndex);
	CostumeCreator_CopyCostumeFromEnt(eStorageType, pEnt, iIndex);
}

void CostumeCreator_StoreCostumeToEnt(PCCostumeStorageType eStorageType, Entity *pEnt, S32 iIndex, PCPaymentMethod ePayMethod)
{
	Entity *pPlayerEnt = CostumeCreator_GetPlayerEntity(pEnt && pEnt->pSaved && pEnt->pSaved->conOwner.containerID ? pEnt->pSaved->conOwner.containerID : pEnt ? entGetContainerID(pEnt) : 0);
	PlayerCostumeSlot *pCostumeSlot;
	PCSlotDef *pSlotDef;
	PCSlotType *pSlotType = NULL;
	PCSlotSet *pSlotSet;

	if (!g_CostumeEditState.pCostume)
		return;

	if (!costumeEntity_GetStoreCostumeSlot(pPlayerEnt, pEnt, iIndex, &pSlotDef, &pCostumeSlot))
		return;

	pSlotDef = g_CostumeEditState.pSlotDef;
	pSlotType = g_CostumeEditState.pSlotType;
	pSlotSet = costumeEntity_GetSlotSet(pEnt, CostumeCreator_EntIsPet(pPlayerEnt, pEnt));

	// Handle saving costumes to the fake player
	if (CONTAINER_NOCONST(Entity, pPlayerEnt) == g_pFakePlayer)
	{
		// NB: pEnt is modified, so pEnt must not be a real entity!
		CostumeCreator_StoreCostumeLoginHelper(pPlayerEnt, pEnt, iIndex, g_CostumeEditState.pCostume, pSlotType, pSlotDef, pSlotSet, pCostumeSlot);
		return;
	}

	// Save costume on server
	if (pEnt)
	{
		ServerCmd_StorePlayerCostume(eStorageType, entGetContainerID(pEnt), iIndex, g_CostumeEditState.pConstCostume, SAFE_MEMBER(pSlotType, pcName), ePayMethod);

		if (GET_REF(g_CostumeEditState.hMood))
		{
			PCMood *pMood = GET_REF(g_CostumeEditState.hMood);
			ServerCmd_ChangeMood(pMood->pcName);
		}
	}
}

void CostumeCreator_StoreCostumeToContainer(PCCostumeStorageType eStorageType, ContainerID uContainerID, S32 iIndex, PCPaymentMethod ePayMethod)
{
	Entity *pEnt = CostumeCreator_GetStoreCostumeEntityFromContainer(eStorageType, uContainerID, iIndex);
	CostumeCreator_StoreCostumeToEnt(eStorageType, pEnt, iIndex, ePayMethod);
}

void CostumeCreator_StoreCostumeToPet(PCCostumeStorageType eStorageType, int iPetNum, S32 iIndex, PCPaymentMethod ePayMethod)
{
	Entity *pEnt = CostumeCreator_GetStoreCostumeEntityFromPet(eStorageType, iPetNum, iIndex);
	CostumeCreator_StoreCostumeToEnt(eStorageType, pEnt, iIndex, ePayMethod);
}

void CostumeCreator_SaveCostumeDefault(PCPaymentMethod ePayMethod)
{
	CostumeCreator_StoreCostumeToContainer(g_CostumeEditState.eCostumeStorageType, g_CostumeEditState.uCostumeEntContainerID, g_CostumeEditState.iCostumeIndex, ePayMethod);
}

void CostumeCreator_RenameCostumeEnt(PCCostumeStorageType eStorageType, Entity *pEnt, S32 iIndex, PCPaymentMethod ePayMethod, const char *pchName)
{
	Entity *pPlayerEnt = CostumeCreator_GetPlayerEntity(pEnt && pEnt->pSaved && pEnt->pSaved->conOwner.containerID ? pEnt->pSaved->conOwner.containerID : pEnt ? entGetContainerID(pEnt) : 0);
	PlayerCostumeSlot *pCostumeSlot;
	PCSlotDef *pSlotDef;
	PCSlotType *pSlotType = NULL;

	if (!costumeEntity_GetStoreCostumeSlot(pPlayerEnt, pEnt, iIndex, &pSlotDef, &pCostumeSlot))
		return;

	if (!pCostumeSlot || !pCostumeSlot->pCostume || !stricmp(pCostumeSlot->pCostume->pcName, pchName))
		return;

	if (pCostumeSlot && EMPTY_TO_NULL(pCostumeSlot->pcSlotType))
		pSlotType = costumeLoad_GetSlotType(pCostumeSlot->pcSlotType);
	if (!pSlotType && pSlotDef && EMPTY_TO_NULL(pSlotDef->pcSlotType))
		pSlotType = costumeLoad_GetSlotType(pSlotDef->pcSlotType);

	// Handle saving costumes to the fake player
	if (CONTAINER_NOCONST(Entity, pPlayerEnt) == g_pFakePlayer)
	{
		NOCONST(PlayerCostumeSlot) *pNoConstSlot = CONTAINER_NOCONST(PlayerCostumeSlot, pCostumeSlot);
		pNoConstSlot->pCostume->pcName = allocAddString(pchName);
	}
	else if (pEnt)
	{
		// This will fail if the current costume is invalid for the slot
		NOCONST(PlayerCostume) *pCostume = StructCloneDeConst(parse_PlayerCostume, pCostumeSlot->pCostume);
		pCostume->pcName = allocAddString(pchName);
		if (pEnt && pEnt->pChar && !REF_COMPARE_HANDLES(pCostume->hSpecies, pEnt->pChar->hSpecies))
			COPY_HANDLE(pCostume->hSpecies, pEnt->pChar->hSpecies);
		ServerCmd_StorePlayerCostume(eStorageType, entGetContainerID(pEnt), iIndex, CONTAINER_RECONST(PlayerCostume, pCostume), SAFE_MEMBER(pSlotType, pcName), ePayMethod);
		StructDestroyNoConst(parse_PlayerCostume, pCostume);
	}
}

void CostumeCreator_RenameCostumeContainer(PCCostumeStorageType eStorageType, ContainerID uContainerID, S32 iIndex, PCPaymentMethod ePayMethod, const char *pchName)
{
	Entity *pEnt = CostumeCreator_GetStoreCostumeEntityFromContainer(eStorageType, uContainerID, iIndex);
	CostumeCreator_RenameCostumeEnt(eStorageType, pEnt, iIndex, ePayMethod, pchName);
}

void CostumeCreator_RenameCostumePet(PCCostumeStorageType eStorageType, int iPetNum, S32 iIndex, PCPaymentMethod ePayMethod, const char *pchName)
{
	Entity *pEnt = CostumeCreator_GetStoreCostumeEntityFromPet(eStorageType, iPetNum, iIndex);
	CostumeCreator_RenameCostumeEnt(eStorageType, pEnt, iIndex, ePayMethod, pchName);
}

void CostumeCreator_StoreCostumeLoginHelper(Entity *pPlayerEnt, Entity *pEnt, S32 iIndex, NOCONST(PlayerCostume) *pCostume, PCSlotType *pSlotType, PCSlotDef *pSlotDef, PCSlotSet *pSlotSet, PlayerCostumeSlot *pStoreSlot)
{
	NOCONST(GameAccountData) *pData = CONTAINER_NOCONST(GameAccountData, entity_GetGameAccount(pEnt));
	NOCONST(Entity) *pEntity = CONTAINER_NOCONST(Entity, pEnt);
	NOCONST(PlayerCostumeSlot) *pCostumeSlot = CONTAINER_NOCONST(PlayerCostumeSlot, pStoreSlot);

	// NOTE:
	//    Pseudo implementation of StoreCostume, probably should use actual code,
	//    but it doesn't need all the additional validation.

	if (!pCostume)
		return;

	if (iIndex >= costumeEntity_trh_GetNumCostumeSlots(pEntity, pData))
		return;

	// If it's not a player costume, make it player valid costume
	CostumeCreator_BeginCostumeEditing(pCostume);

	// Get the slot
	if (!pStoreSlot)
	{
		// Ensure there is enough slots
		while (eaSize(&pEntity->pSaved->costumeData.eaCostumeSlots) <= iIndex)
		{
			NOCONST(PlayerCostumeSlot) *pSlot = StructCreateNoConst(parse_PlayerCostumeSlot);
			if (pSlotSet && pSlotSet->pExtraSlotDef)
			{
				pSlot->iSlotID = pSlotSet->pExtraSlotDef->iSlotID;
				pSlot->pcSlotType = pSlotSet->pExtraSlotDef->pcSlotType;
			}
			eaPush(&pEntity->pSaved->costumeData.eaCostumeSlots, pSlot);
		}

		pCostumeSlot = eaGet(&pEntity->pSaved->costumeData.eaCostumeSlots, iIndex);
	}

	// Fill in the slot with the costume information
	if (pCostumeSlot)
	{
		StructDestroyNoConstSafe(parse_PlayerCostume, &pCostumeSlot->pCostume);
		pCostumeSlot->pCostume = StructCloneNoConst(parse_PlayerCostume, g_CostumeEditState.pCostume);
		pCostumeSlot->pcSlotType = SAFE_MEMBER(pSlotType, pcName);
		costumeTailor_StripUnnecessary(pCostumeSlot->pCostume);
	}
}

bool CostumeCreator_BeginCostumeEditing(NOCONST(PlayerCostume) *pCostume)
{
	if (pCostume && pCostume->eCostumeType == kPCCostumeType_Player)
		return true;

	// This command should be called by the UIGen prior to starting editing to force the costume
	// into a legal state.
	if (pCostume && pCostume->eCostumeType != kPCCostumeType_Player)
	{
		Entity *pEnt = CostumeCreator_GetEditPlayerEntity();
		GameAccountDataExtract *pCleanupExtract = NULL;
		GameAccountDataExtract *pExtract;

		// Get the game account data extract
		if (GSM_IsStateActive(GCL_LOGIN))
			pExtract = pCleanupExtract = GAD_CreateExtract(entity_GetGameAccount(pEnt));
		else
			pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

		// Enforce player rules if try to edit it
		pCostume->eCostumeType = kPCCostumeType_Player;

		// Fill bones, then make valid, then fill a second time to get everything right
		costumeTailor_FillAllBones(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, true, false, true);
		costumeTailor_MakeCostumeValid(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.eaUnlockedCostumes, g_CostumeEditState.pSlotType, true, g_CostumeEditState.bUnlockAll, false, guild_GetGuild(pEnt), false, pExtract, false, g_CostumeEditState.eaPowerFXBones);
		costumeTailor_FillAllBones(g_CostumeEditState.pCostume, GET_REF(g_CostumeEditState.hSpecies), g_CostumeEditState.eaPowerFXBones, g_CostumeEditState.pSlotType, true, false, true);

		// Cleanup a created extract
		if (pCleanupExtract)
			StructDestroy(parse_GameAccountDataExtract, pCleanupExtract);
		return true;
	}

	return false;
}

void CostumeCreator_StartEditCostume(void)
{
	COSTUME_UI_TRACE_FUNC();
	if (CostumeCreator_BeginCostumeEditing(g_CostumeEditState.pCostume))
	{
		// Reset the costume state
		CostumeUI_RegenCostumeEx(true, true);
	}
}

