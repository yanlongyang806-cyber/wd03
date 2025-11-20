
#include "StringCache.h"
#include "GlobalStateMachine.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonRandom.h"
#include "CostumeCommonTailor.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "Expression.h"

#include "inputKeyBind.h"

#include "UIGen.h"

#include "Entity.h"
#include "entCritter.h"
#include "Character.h"
#include "CharacterClass.h"
#include "CharacterSelection.h"
#include "CombatEnums.h"
#include "gclCostumeUI.h"
#include "GameStringFormat.h"
#include "gclHUDOptions.h"
#include "Player.h"
#include "PowerGrid.h"
#include "PowerTree.h"
#include "PowerTreeHelpers.h"
#include "SavedPetCommon.h"
#include "inventoryCommon.h"
#include "FCInventoryUI.h"

#include "species_common.h"
#include "species_common_h_ast.h"

#include "Entity_h_ast.h"
#include "Character_h_ast.h"
#include "StatPoints_h_ast.h"
#include "PowerTree_h_ast.h"
#include "Player_h_ast.h"
#include "AutoGen/LoginCommon_h_ast.h"
#include "AutoGen/CharacterClass_h_ast.h"
#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/CombatEnums_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/CharacterCreationUI_h_ast.h"
#include "AutoGen/allegiance_h_ast.h"

#include "GameClientLib.h"
#include "gclBaseStates.h"
#include "gclLogin.h"
#include "LoginCommon.h"

#include "gclQuickPlay.h"
#include "gclDialogBox.h"

#include "SimpleParser.h"
#include "StringUtil.h"
#include "gclCommandParse.h"

#include "CharacterCreationUI.h"
#include "ResourceInfo.h"

#include "gclBaseStates.h"

#include "CostumeCommonEntity.h"

#include "Powers.h"
#include "AbilityScores_DD.h"

#include "MicroTransactions.h"
#include "gclMicroTransactions.h"
#include "MicroTransactionUI.h"
#include "GameAccountDataCommon.h"
#include "gclAccountProxy.h"
#include "GamePermissionsCommon.h"
#include "Login2Common.h"
#include "AutoGen/Login2Common_h_ast.h"
#include "gclCostumeUIState.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

Login2CharacterCreationData *g_CharacterCreationData = NULL;

const char *g_pcAllegianceName = NULL;

CmdList g_CharacterCreationCmds;

extern char gQuickLoginRequestedCharName[];

bool gbRandomizeCostume = false;
AUTO_CMD_INT(gbRandomizeCostume, RandomizeCostume) ACMD_COMMANDLINE;

S32 g_bCharacterPathAllowDevRestricted = false;
AUTO_CMD_INT(g_bCharacterPathAllowDevRestricted, CharacterPathAllowDevRestricted) ACMD_COMMANDLINE;

char g_charCreationShardName[128] = "";
AUTO_CMD_STRING(g_charCreationShardName, CharCreationShardName) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

extern int giTestClientPort;

static KeyBindProfile s_Profile = {"Character Creation Commands", __FILE__, NULL, true, true, NULL, &g_CharacterCreationCmds};

NOCONST(Entity) *g_pFakePlayer = NULL;
NOCONST(Entity) *g_eaFakeOwnedEnts = NULL;

static AllegianceList s_CharacterCreationAllegiances;

static CharacterCreationAllegianceUI **s_eaUIAllegiances;
static U32 s_iUIAllegiancesShard;
static StashTable s_stSpeciesProducts;

void CharacterCreation_FillUIAllegiances(U32 iVirtualShardID);
void CharacterCreation_SavePlayerSpecies(SA_PARAM_OP_VALID SpeciesDef* pSpecies);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SetShardName");
void CharacterCreation_SetShardName(const char *shardName)
{
    strcpy(g_charCreationShardName, shardName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SetAllegiance");
void CharacterCreation_SetAllegiance(const char *faction)
{
	if (!faction || !*faction)
	{
		if (g_pFakePlayer) REMOVE_HANDLE(g_pFakePlayer->hAllegiance);
		g_pcAllegianceName = NULL;
		return;
	}

	g_pcAllegianceName = allocAddString(faction);
	if (g_pFakePlayer) SET_HANDLE_FROM_STRING("Allegiance",g_pcAllegianceName,g_pFakePlayer->hAllegiance);
}

// Same as CharacterCreation_SetAllegiance, except it copies the allegiance from another entity.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SetAllegianceFromPlayer");
void CharacterCreation_SetAllegianceFromPlayer(SA_PARAM_NN_VALID Entity *pEntity)
{
	const char *faction = REF_STRING_FROM_HANDLE(pEntity->hAllegiance);
	CharacterCreation_SetAllegiance(faction);
}

static void HandleQuickLoginCreate(void)
{
	PCSlotSet *pSlotSet = costumeEntity_GetSlotSet(CONTAINER_RECONST(Entity, g_pFakePlayer), false);

    StructDestroy(parse_Login2CharacterCreationData, g_CharacterCreationData);
    g_CharacterCreationData = StructCreate(parse_Login2CharacterCreationData);

	gclQuickPlay_FillDefaultCharacter(g_CharacterCreationData);

	if(gbRandomizeCostume)
	{
		MersenneTable *pTable = mersenneTableCreate(giTestClientPort + randomU32());
		int i;

		for (i = 0; pSlotSet && i < eaSize(&pSlotSet->eaSlotDefs); i++)
		{
			if (pSlotSet->eaSlotDefs[i]->eCreateCharacter == kPCCharacterCreateSlot_Required)
			{
				PossibleCharacterCostume *pCharCostume = StructCreate(parse_PossibleCharacterCostume);
				PCSkeletonDef **ppSkels = NULL;

				eaPush(&g_CharacterCreationData->costumes, pCharCostume);

				pCharCostume->iSlotID = pSlotSet->eaSlotDefs[i]->iSlotID;

				pCharCostume->pCostume = StructCreateNoConst(parse_PlayerCostume);
				pCharCostume->pCostume->eCostumeType = kPCCostumeType_Player;
				costumeTailor_GetValidSkeletons(pCharCostume->pCostume, NULL, &ppSkels, false, true);
				SET_HANDLE_FROM_REFERENT(g_hCostumeSkeletonDict, ppSkels[0], pCharCostume->pCostume->hSkeleton);
				eaDestroy(&ppSkels);

				costumeRandom_SetRandomTable(pTable);
				costumeRandom_FillRandom(pCharCostume->pCostume, NULL/*Species*/, NULL, NULL, NULL, NULL, NULL, true, true, true, false, true, true, true);
				costumeTailor_FillAllBones(pCharCostume->pCostume, NULL, NULL, NULL, true, false, true);
				costumeRandom_SetRandomTable(NULL);
			}
		}

		if (!eaSize(&g_CharacterCreationData->costumes))
		{
			PossibleCharacterCostume *pCharCostume = StructCreate(parse_PossibleCharacterCostume);
			PCSkeletonDef **ppSkels = NULL;

			eaPush(&g_CharacterCreationData->costumes, pCharCostume);

			pCharCostume->pCostume = StructCreateNoConst(parse_PlayerCostume);
			pCharCostume->pCostume->eCostumeType = kPCCostumeType_Player;
			costumeTailor_GetValidSkeletons(pCharCostume->pCostume, NULL, &ppSkels, false, true);
			SET_HANDLE_FROM_REFERENT(g_hCostumeSkeletonDict, ppSkels[0], pCharCostume->pCostume->hSkeleton);
			eaDestroy(&ppSkels);

			costumeRandom_SetRandomTable(pTable);
			costumeRandom_FillRandom(pCharCostume->pCostume, NULL/*Species*/, NULL, NULL, NULL, NULL, NULL, true, true, true, false, true, true, true);
			costumeTailor_FillAllBones(pCharCostume->pCostume, NULL, NULL, NULL, true, false, true);
			costumeRandom_SetRandomTable(NULL);
		}

		mersenneTableFree(pTable);
	}

	if (gQuickLoginRequestedCharName[0])
    {
		g_CharacterCreationData->name = StructAllocString(gQuickLoginRequestedCharName);
    }

	if (g_iQuickLogin > 1)
    {
        char buf[32];
		strcatf(buf, " %d", g_iQuickLogin);
        g_CharacterCreationData->name = StructAllocString(buf);
    }

    gclLoginCreateCharacter(g_CharacterCreationData);
}

static void CharacterCreation_CreateUI(void)
{
}

static void CharacterCreation_Enter(void)
{
	PlayerCostumeRef **ppCostumes = NULL;

	keybind_PushProfile(&s_Profile);
	keybind_PushProfileName("CostumeCreationUI");

    StructDestroy(parse_Login2CharacterCreationData, g_CharacterCreationData);
    g_CharacterCreationData = StructCreate(parse_Login2CharacterCreationData);

    g_CharacterCreationData->shardName = allocAddString(g_charCreationShardName);

	s_stSpeciesProducts = stashTableCreateAddress(16);

	if( g_pFakePlayer )
	{
		StructDestroyNoConstSafe(parse_Entity, &g_pFakePlayer);
		g_pFakePlayer = NULL;
	}

	if (!g_pFakePlayer)
	{
		PCSlotSet *pSlotSet = NULL;
		NOCONST(InventoryBagLite) *pLiteBag;

		g_pFakePlayer = StructCreateWithComment(parse_Entity, "FakePlayer created in CharacterCreation_Enter");
		g_pFakePlayer->myEntityType = GLOBALTYPE_ENTITYPLAYER;
		g_pFakePlayer->pChar = StructCreateNoConst(parse_Character);
		g_pFakePlayer->pChar->iLevelExp = 1;
		g_pFakePlayer->pChar->iLevelCombat = g_pFakePlayer->pChar->iLevelExp;
		g_pFakePlayer->pPlayer = StructCreateNoConst(parse_Player);
		g_pFakePlayer->pPlayer->accessLevel = g_pFakePlayer->pPlayer->accountAccessLevel = LoginGetAccessLevel();
		g_pFakePlayer->pPlayer->playerType = kPlayerType_None;
		strcpy(g_pFakePlayer->pPlayer->publicAccountName, gGCLState.displayName);
		g_pFakePlayer->pSaved = StructCreateNoConst(parse_SavedEntityData);
		if (g_pcAllegianceName) SET_HANDLE_FROM_STRING("Allegiance",g_pcAllegianceName,g_pFakePlayer->hAllegiance);
		g_pFakePlayer->pSaved->uFixupVersion = CURRENT_ENTITY_FIXUP_VERSION;
		g_pFakePlayer->pSaved->uGameSpecificFixupVersion = gameSpecificFixup_Version();
		g_pFakePlayer->pInventoryV2 = StructCreateNoConst(parse_Inventory);
		eaIndexedEnableNoConst(&g_pFakePlayer->pInventoryV2->ppLiteBags, parse_InventoryBagLite);

		// Make it possible for the UI to muck around with numerics
		pLiteBag = StructCreateNoConst(parse_InventoryBagLite);
		pLiteBag->BagID = InvBagIDs_Numeric;
		eaIndexedAdd(&g_pFakePlayer->pInventoryV2->ppLiteBags, pLiteBag);

		// For UI set this
		if(g_bCharIsSuperPremium)
		{
			g_pFakePlayer->pPlayer->playerType = kPlayerType_SuperPremium;
		}

		// Initialize the costume slots
		pSlotSet = costumeEntity_GetSlotSet((Entity*)g_pFakePlayer, false);
		costumeEntity_trh_FixupCostumeSlots(ATR_EMPTY_ARGS, NULL, g_pFakePlayer, pSlotSet ? pSlotSet->pcName : NULL);
	}

	CharacterCreation_CreateUI();

	if (g_iQuickLogin)
		HandleQuickLoginCreate();

	// Make it easy to debug character creation state in the UI
	ui_GenSetPointerVar("CharacterCreationData", g_CharacterCreationData, parse_Login2CharacterCreationData);
}

static void CharacterCreation_DestroyUI(void)
{
	keybind_PopProfileName("CostumeCreationUI");
	keybind_PopProfile(&s_Profile);
}

static void CharacterCreation_ClearSpeciesProduct(MicroTransactionProduct **eaAllProducts)
{
	eaDestroy(&eaAllProducts);
}

static void CharacterCreation_OncePerFrame(void)
{
	ui_GenSetPointerVar("Player", g_pFakePlayer, parse_Entity);
	ui_GenSetPointerVar("CharacterCreationData", g_CharacterCreationData, parse_Login2CharacterCreationData);

	stashTableClearEx(s_stSpeciesProducts, NULL, CharacterCreation_ClearSpeciesProduct);

	if(g_pFakePlayer && g_pFakePlayer->pChar)
	{
		g_pFakePlayer->pChar->pEntParent = (Entity*)g_pFakePlayer;
	}

	if (g_pFakePlayer && GET_REF(g_pFakePlayer->hAllegiance))
	{
		AllegianceDef *pAllegiance = GET_REF(g_pFakePlayer->hAllegiance);
		COPY_HANDLE(g_pFakePlayer->hFaction, pAllegiance->hFaction);
	}

	if (s_eaUIAllegiances)
	{
		CharacterCreation_FillUIAllegiances(s_iUIAllegiancesShard);
	}
}

static void CharacterCreation_Leave(void)
{
	ui_GenSetPointerVar("Player", NULL, parse_Entity);
	ui_GenSetPointerVar("CharacterCreationData", NULL, parse_Login2CharacterCreationData);
	StructDestroyNoConstSafe(parse_Entity, &g_pFakePlayer);
	CharacterCreation_DestroyUI();
	eaDestroyStruct(&s_eaUIAllegiances, parse_CharacterCreationAllegianceUI);
}

static bool CharacterCreation_CreateCharacter(bool bFinish, const char *pchName, const char *pchSubName, const char *pchDescription, bool bGoToLobby)
{
	PCMood *pMood = CostumeUI_GetMood();
	char *pchMesg = NULL;
	U32 uOldId;
	int i;

    devassertmsg(!bGoToLobby, "Go to lobby flag passed to CharacterCreation_CreateCharacter");

	if (pchName) 
    {
		// Check for duplicate
		for (i = 0; i < eaSize(&g_characterSelectionData->characterChoices->characterChoices); i++) 
        {
			if (stricmp(pchName, g_characterSelectionData->characterChoices->characterChoices[i]->savedName) == 0) 
            {
				FormatGameMessageKey(&pchMesg, "NameFormat_DuplicateName", STRFMT_END);
				notify_NotifySend(NULL, kNotifyType_NameInvalid, pchMesg, NULL, NULL);
				estrDestroy(&pchMesg);
				return false;
			}
		}

		if (CharacterCreation_IsNameValidWithErrorMessage(pchName, kNotifyType_NameInvalid)) 
        {
            g_CharacterCreationData->name = StructAllocString(pchName);
		} 
        else 
        {
			return false;
		}
	}
    else if ( g_pFakePlayer )
    {
        g_CharacterCreationData->name = StructAllocString(g_pFakePlayer->pSaved->savedName);
    }

	if (pchSubName) 
    {
        g_CharacterCreationData->subName = StructAllocString(pchSubName);
	}
    else if ( g_pFakePlayer )
    {
        g_CharacterCreationData->subName = StructAllocString(g_pFakePlayer->pSaved->savedSubName);
    }

	if (pchDescription) 
    {
		if (CharacterCreation_IsDescriptionValidWithErrorMessage(pchDescription)) 
        {
            g_CharacterCreationData->description = StructAllocString(pchDescription);
		} 
        else 
        {
			return false;
		}
	}
    else if ( g_pFakePlayer )
    {
        g_CharacterCreationData->description = StructAllocString(g_pFakePlayer->pSaved->savedDescription);
    }

	//trim the trailing whitespace
    if ( g_CharacterCreationData->name )
    {
	    removeTrailingWhiteSpaces( g_CharacterCreationData->name );
    }
    if ( g_CharacterCreationData->subName )
    {
	    removeTrailingWhiteSpaces( g_CharacterCreationData->subName );
    }

	// Make sure current edited costume is saved
	uOldId = g_CostumeEditState.uCostumeEntContainerID;
	g_CostumeEditState.uCostumeEntContainerID = 0;
	CostumeCreator_SaveCostumeDefault(kPCPay_Default);
	g_CostumeEditState.uCostumeEntContainerID = uOldId;

	// Copy costumes from g_pFakePlayer to g_pCharCreateChoice
	g_CharacterCreationData->speciesName = NULL;
	eaClearStruct(&g_CharacterCreationData->costumes, parse_PossibleCharacterCostume);
	for (i = 0; i < eaSize(&g_pFakePlayer->pSaved->costumeData.eaCostumeSlots); i++)
	{
		NOCONST(PlayerCostumeSlot) *pCostumeSlot = g_pFakePlayer->pSaved->costumeData.eaCostumeSlots[i];
		if (pCostumeSlot->pCostume)
		{
			PossibleCharacterCostume *pCharCostume = StructCreate(parse_PossibleCharacterCostume);
			PCSlotType *pSlotType = costumeEntity_GetSlotType(CONTAINER_RECONST(Entity, g_pFakePlayer), i, false, NULL);
			pCharCostume->pCostume = StructCloneNoConst(parse_PlayerCostume, pCostumeSlot->pCostume);
			pCharCostume->iSlotID = pCostumeSlot->iSlotID;
			pCharCostume->pcSlotType = pSlotType && pSlotType->pcName != pCostumeSlot->pcSlotType ? pCostumeSlot->pcSlotType : NULL;
			eaPush(&g_CharacterCreationData->costumes, pCharCostume);

			if ( g_CharacterCreationData->speciesName == NULL )
			{
				g_CharacterCreationData->speciesName = allocAddString(REF_STRING_FROM_HANDLE(pCharCostume->pCostume->hSpecies));
			}
		}
	}

	// I'm told that we can't trust the g_CostumeEditState.hSpecies in STO for alien species.
	//g_CharacterCreationData->speciesName = allocAddString(REF_STRING_FROM_HANDLE(g_CostumeEditState.hSpecies));

	g_CharacterCreationData->moodName = pMood ? allocAddString(pMood->pcName) : NULL;

	// TODO: Copy costumes from pets on g_pFakePlayer to g_CharacterCreationData->petInfo
	for (i = 0; i < eaSize(&g_CharacterCreationData->petInfo); i++)
	{
		if (g_CharacterCreationData->petInfo[i]->pCostume)
			costumeTailor_StripUnnecessary(g_CharacterCreationData->petInfo[i]->pCostume);
	}

	g_CharacterCreationData->skipTutorial = g_characterSelectionData->hasCompletedTutorial && gGCLState.bSkipTutorial;
    g_CharacterCreationData->virtualShardID = g_pFakePlayer->pPlayer->iVirtualShardID;
    g_CharacterCreationData->allegianceName = allocAddString(REF_STRING_FROM_HANDLE(g_pFakePlayer->hAllegiance));

	return bFinish && gclLoginCreateCharacter(g_CharacterCreationData);
}

static bool CharacterCreation_IsSpeciesInAllegiance(SpeciesDef *species, const char *pcAllegianceName)
{
	int i;
	AllegianceDef *Allegiance = NULL;

	if (!pcAllegianceName) {
		return true;
	}
	if (!species) {
		return false;
	}

	Allegiance = resGetObject("Allegiance", pcAllegianceName);
	if (!Allegiance) {
		for (i = eaSize(&s_CharacterCreationAllegiances.refArray)-1; i >= 0; --i) {
			AllegianceDef *f = GET_REF(s_CharacterCreationAllegiances.refArray[i]->hDef);
			if (f && !stricmp(f->pcName, pcAllegianceName)) {
				break;
			}
		}

		if (i < 0) {
			//Allegiance not available on client; Make reference to it to get it
			AllegianceRef *h = StructCreate(parse_AllegianceRef);
			if (h) {
				SET_HANDLE_FROM_STRING("Allegiance", pcAllegianceName, h->hDef);
				eaPush(&s_CharacterCreationAllegiances.refArray, h);
			}
		}

		return false;
	}

	if (eaSize(&Allegiance->eaStartSpecies) == 0) {
		return true;
	}

	for (i = eaSize(&Allegiance->eaStartSpecies)-1; i >= 0; --i) {
		if (GET_REF(Allegiance->eaStartSpecies[i]->hSpecies) == species) {
			return true;
		}
	}

	return false;
}

static bool CanPlayerUseSpecies(SpeciesDef *pSpecies, bool bUGC, bool bUnlockAll)
{
	int i;

	if (!pSpecies) {
		return false;
	}

	// Too soon
	if( (pSpecies->uUnlockTimestamp != 0) && (pSpecies->uUnlockTimestamp > timeServerSecondsSince2000()) )
		return false;

	// Player_Initial is irrelevant if the UnlockCode is defined.
	if ((pSpecies->eRestriction & kPCRestriction_Player_Initial) && (!pSpecies->pcUnlockCode || !*pSpecies->pcUnlockCode)) {
		return true;
	}

	if (bUGC && (pSpecies->eRestriction & kPCRestriction_UGC_Initial)) {
		return true;
	}

	if ((pSpecies->eRestriction & kPCRestriction_Player)) {
		if (bUnlockAll) {
			return true;
		}

		if (g_characterSelectionData) {
			for (i = eaSize(&g_characterSelectionData->unlockedSpecies) - 1; i >= 0; i--) {
				if (GET_REF(g_characterSelectionData->unlockedSpecies[i]->hSpecies) == pSpecies) {
					return true;
				}
			}
		}
	}

	return false;
}

static bool GetAllSpeciesProducts(MicroTransactionProduct ***peaAllProducts, SpeciesDef *pSpecies)
{
	MicroTransactionProduct **eaAllProducts = NULL;
	bool bUnlockable = false;
	S32 j, k;

	if (peaAllProducts) {
		eaClear(peaAllProducts);
	}

	// Scan through the micro-transactions to see if it's in there
	if (!pSpecies || !g_pMTList || !pSpecies->pcUnlockCode || !pSpecies->pcUnlockCode[0] || !s_stSpeciesProducts) {
		return bUnlockable;
	}

	if (stashAddressFindPointer(s_stSpeciesProducts, pSpecies, (void **)&eaAllProducts)) {
		bUnlockable = eaSize(&eaAllProducts) > 0;
		if (peaAllProducts) {
			eaCopy(peaAllProducts, &eaAllProducts);
		}
		return bUnlockable;
	}

	for (j = eaSize(&g_pMTList->ppProducts) - 1; j >= 0; j--) {
		MicroTransactionProduct *pProduct = g_pMTList->ppProducts[j];
		MicroTransactionDef *pProductDef = GET_REF(pProduct->hDef);
		if (pProductDef && microtrans_GetPrice(pProduct->pProduct) >= 0) {
			for (k = eaSize(&pProductDef->eaParts) - 1; k >= 0; k--) {
				MicroTransactionPart *pProductPart = pProductDef->eaParts[k];

				// If the part is a species unlock
				if (pProductPart->ePartType == kMicroPart_Species) {
					SpeciesDef *pUnlockedSpecies = GET_REF(pProductPart->hSpeciesDef);
					if (pUnlockedSpecies == pSpecies || (pUnlockedSpecies->pcUnlockCode && pSpecies->pcUnlockCode && !stricmp(pUnlockedSpecies->pcUnlockCode, pSpecies->pcUnlockCode))) {
						if (!gclMicroTrans_IsProductHidden(pProduct)) {
							bUnlockable = true;
							eaPush(&eaAllProducts, pProduct);
						}
					}
				}
			}
		}
	}

	if (peaAllProducts) {
		eaCopy(peaAllProducts, &eaAllProducts);
	}

	stashAddressAddPointer(s_stSpeciesProducts, pSpecies, eaAllProducts, false);
	return bUnlockable;
}

void CharacterCreation_FillSpeciesListEx(SpeciesDef ***peaSpeciesList,
										 const char *pcAllegianceName,
										 U32 uFlags)
{
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct("SpeciesDef");
	S32 i, j;
	CharClassTypes eSpaceType = StaticDefineIntGetInt(CharClassTypesEnum, "Space");

	eaClear(peaSpeciesList);

	for (i = 0; i < eaSize(&pArray->ppReferents); i++) {
		SpeciesDef *pSpecies = (SpeciesDef*)pArray->ppReferents[i];

		if (stricmp(pSpecies->pcName, "None") == 0) {
			continue;
		}

		if (((uFlags & kFillSpeciesListFlag_Space) && (CharClassTypes)pSpecies->eType != eSpaceType)
			|| (!(uFlags & kFillSpeciesListFlag_Space) && (CharClassTypes)pSpecies->eType == eSpaceType)) {
			continue;
		}

		if (!CanPlayerUseSpecies(pSpecies, !!(uFlags & kFillSpeciesListFlag_UGC), false)) {
			if (!CanPlayerUseSpecies(pSpecies, !!(uFlags & kFillSpeciesListFlag_UGC), true)) {
				continue;
			}

			if (!(uFlags & kFillSpeciesListFlag_MicroTransacted)) {
				continue;
			}

			// Scan through the micro-transactions to see if it's in there
			if (!GetAllSpeciesProducts(NULL, pSpecies)) {
				continue;
			}
		}

		if (!(uFlags & kFillSpeciesListFlag_Space) && !CharacterCreation_IsSpeciesInAllegiance(pSpecies, pcAllegianceName)) {
			continue;
		}

		if( (uFlags & kFillSpeciesListFlag_OnlyIncludeGenderFemale) && pSpecies->eGender != Gender_Female )
			continue;
		if( (uFlags & kFillSpeciesListFlag_OnlyIncludeGenderMale)   && pSpecies->eGender != Gender_Male )
			continue;

		if( (uFlags & kFillSpeciesListFlag_HideBetaSpecies) && pSpecies->bHideInBeta )
			continue;

		if (uFlags & kFillSpeciesListFlag_NoDuplicates) {
			bool bFound = false;
			for (j = 0; j < eaSize(peaSpeciesList); j++) {
				if (stricmp(pSpecies->pcSpeciesName, (*peaSpeciesList)[j]->pcSpeciesName) == 0) {
					bFound = true;
					break;
				}
			}
			if (bFound) {
				continue;
			}
		}

		eaPush(peaSpeciesList, pSpecies);
	}

	costumeTailor_SortSpecies(*peaSpeciesList, true);
}

void CharacterCreation_FillGenderListEx(SpeciesDef ***peaGenderList, SpeciesDef *pSpecies, U32 uFlags)
{
	DictionaryEArrayStruct* pArray = resDictGetEArrayStruct("SpeciesDef");
	S32 i;

	eaClear(peaGenderList);

	if (pSpecies) {
		for (i = 0; i < eaSize(&pArray->ppReferents); i++) {
			SpeciesDef *pOtherSpecies = (SpeciesDef*)pArray->ppReferents[i];

			if (!CanPlayerUseSpecies(pOtherSpecies, !!(uFlags & kFillSpeciesListFlag_UGC), false)) {
				if (!CanPlayerUseSpecies(pOtherSpecies, !!(uFlags & kFillSpeciesListFlag_UGC), true)) {
					continue;
				}

				if (!(uFlags & kFillSpeciesListFlag_MicroTransacted)) {
					continue;
				}

				// Scan through the micro-transactions to see if it's in there
				if (!GetAllSpeciesProducts(NULL, pOtherSpecies)) {
					continue;
				}
			}

			if (stricmp(pSpecies->pcSpeciesName, pOtherSpecies->pcSpeciesName) == 0) {
				eaPush(peaGenderList, pArray->ppReferents[i]);
			}
		}
	}
}

PlayerCostume* CharacterCreation_GetDefaultCostumeForSpecies(SpeciesDef* pSpecies, bool bIsPet)
{
	U32 iPreset;
	CostumePreset* pPreset;

	if (!g_pFakePlayer) {
		iPreset = 0;
	} else if (bIsPet && eaSize(&g_CharacterCreationData->petInfo) > 0) {
		iPreset = g_CharacterCreationData->petInfo[0]->iPreset;
	} else {
		iPreset = g_CharacterCreationData->costumePreset;
	}

	if (pPreset = eaGet(&pSpecies->eaPresets, iPreset)) {
		return GET_REF(pPreset->hCostume);
	}
	return NULL;
}

//////////////////////////////////////////////////////////////////////////
// Commands and expression functions for Gens during character creation.

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetTreeDefsWithTypeAndAllegiance");
void CharacterCreation_GenGetTreeDefsWithTypeAndAllegiance(ExprContext *pContext, const char *pchTreeType, const char *pchTreeAllegiance)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct("PowerTreeDef");
	int i;
	//CharClassTypes eType = StaticDefineIntGetInt(CharClassTypesEnum,pchTreeType);
	static PowerTreeDef **s_eaTrees;

	eaClear(&s_eaTrees);

	for(i=0;i<eaSize(&pStruct->ppReferents);i++)
	{
		PowerTreeDef* pTreeDef = (PowerTreeDef*)pStruct->ppReferents[i];
		PTTypeDef *pTreeTypeDef = GET_REF(pTreeDef->hTreeType);
		CharacterClass *pClass = GET_REF(pTreeDef->hClass);
		AllegianceDef *pAllegiance = RefSystem_ReferentFromString("Allegiance", pchTreeAllegiance);
		int j = -1;

		if (pTreeDef->pExprRequires && exprIsZero(pTreeDef->pExprRequires))
		{
			continue;
		}

		if (pAllegiance && pClass)
		{
			for (j = eaSize(&pAllegiance->eaClassesAllowed)-1; j >= 0; --j)
			{
				if (pClass == GET_REF(pAllegiance->eaClassesAllowed[j]->hClass))
				{
					break;
				}
			}
		}
		if (j >= 0 || !eaSize(&pAllegiance->eaClassesAllowed))
		{
			if(pTreeTypeDef && stricmp(pchTreeType,pTreeTypeDef->pchTreeType)==0) //if(eType == pTreeDef->eType)
				eaPush(&s_eaTrees,pTreeDef);
		}
	}

	if(pGen)
	{
		ui_GenSetList(pGen, &s_eaTrees, parse_PowerTreeDef);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetTreeDefsWithType);
void CharacterCreation_GenGetTreeDefsWithType(ExprContext *pContext, const char *pchTreeType)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct("PowerTreeDef");
	int i;
	//CharClassTypes eType = StaticDefineIntGetInt(CharClassTypesEnum,pchTreeType);
	static PowerTreeDef **s_eaTrees;

	eaClear(&s_eaTrees);

	for(i=0;i<eaSize(&pStruct->ppReferents);i++)
	{
		PowerTreeDef* pTreeDef = (PowerTreeDef*)pStruct->ppReferents[i];
		PTTypeDef *pTreeTypeDef = GET_REF(pTreeDef->hTreeType);

		if (pTreeDef->pExprRequires && exprIsZero(pTreeDef->pExprRequires))
		{
			continue;
		}

		if(pTreeTypeDef && stricmp(pchTreeType,pTreeTypeDef->pchTreeType)==0) //if(eType == pTreeDef->eType)
			eaPush(&s_eaTrees,pTreeDef);
	}

	if(pGen)
	{
		ui_GenSetList(pGen, &s_eaTrees, parse_PowerTreeDef);
	}
}

// Do not use, use CharacterCreation_GetPrimaryPowerTree instead.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenEntGetPrimaryPowerTree);
const char *CharacterCreation_GetPrimaryPowerTree(SA_PARAM_OP_VALID Entity *pEntity);

// Get the primary power tree of the entity set by CharacterCreation_SetPrimaryPowerTree,
// CharacterCreation_SetPrimaryPowerTreeAndClass, or CharacterCreation_SetPrimaryPowerTreeAndClassByName
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_GetPrimaryPowerTree);
const char *CharacterCreation_GetPrimaryPowerTree(SA_PARAM_OP_VALID Entity *pEntity)
{
	return g_CharacterCreationData->powerTreeName ? g_CharacterCreationData->powerTreeName : "";
}

static bool CharacterCreation_EntSetPrimaryTree(SA_PARAM_NN_VALID Entity *pEntity,
												SA_PARAM_NN_VALID PowerTreeDef *pTreeDef,
												bool bSetClassFromTree)
{
	if ( pEntity->pChar )
	{
		NOCONST(PowerTree) *pTree = CONTAINER_NOCONST(PowerTree, eaGet(&pEntity->pChar->ppPowerTrees, 0));
		if (!pTree || GET_REF(pTree->hDef) != pTreeDef || (bSetClassFromTree && !REF_COMPARE_HANDLES(pEntity->pChar->hClass, pTreeDef->hClass)) )
		{
			pTree = powertree_Create(pTreeDef);
			eaClearStructNoConst(&CONTAINER_NOCONST(Entity, pEntity)->pChar->ppPowerTrees, parse_PowerTree);
			if (pTree)
			{
				eaPush(&CONTAINER_NOCONST(Entity, pEntity)->pChar->ppPowerTrees, pTree);
				g_CharacterCreationData->powerTreeName = REF_STRING_FROM_HANDLE(pTree->hDef);

				if ( bSetClassFromTree )
				{
					const char* pchClass = REF_STRING_FROM_HANDLE( pTreeDef->hClass );
					g_CharacterCreationData->className = allocAddString(pchClass);
					COPY_HANDLE( g_pFakePlayer->pChar->hClass, pTreeDef->hClass );
				}
			}
		}
		return !!pTree;
	}

	return false;
}

// Do not use, use CharacterCreation_SetPrimaryPowerTreeAndClassByName instead.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenEntSetPrimaryPowerTreeAndClassByName);
bool CharacterCreation_SetPrimaryPowerTreeAndClassByName(	ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity,
	ACMD_EXPR_DICT(PowerTreeDef) SA_PARAM_NN_STR const char *pchTreeDef);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_SetPrimaryPowerTreeAndClassByName);
bool CharacterCreation_SetPrimaryPowerTreeAndClassByName(	ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity,
	ACMD_EXPR_DICT(PowerTreeDef) SA_PARAM_NN_STR const char *pchTreeDef)
{
	PowerTreeDef *pTreeDef = (PowerTreeDef *) RefSystem_ReferentFromString("PowerTreeDef", pchTreeDef);
	return !pTreeDef || !pEntity ? false : CharacterCreation_EntSetPrimaryTree(pEntity, pTreeDef, true);
}

// Do not use, use PowerTreeGetDisplayNameByName instead.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetPowerTreeDisplayNameByName);
const char *gclExprPowerTreeGetDisplayNameByName(ExprContext *pContext, ACMD_EXPR_DICT(PowerTreeDef) SA_PARAM_NN_STR const char *pchTreeDef);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PowerTreeGetDisplayNameByName);
const char *gclExprPowerTreeGetDisplayNameByName(ExprContext *pContext, ACMD_EXPR_DICT(PowerTreeDef) SA_PARAM_NN_STR const char *pchTreeDef)
{
	// TODO: This general function does not belong here.
	PowerTreeDef *pTreeDef = (PowerTreeDef *) RefSystem_ReferentFromString("PowerTreeDef", pchTreeDef);
	return pTreeDef ? TranslateDisplayMessage(pTreeDef->pDisplayMessage) : "";
}

// Do not use, use PowerTreeGetDescriptionByName instead.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetPowerTreeDescriptionByName);
const char *gclExprPowerTreeGetDescriptionByName(ExprContext *pContext, ACMD_EXPR_DICT(PowerTreeDef) SA_PARAM_NN_STR const char *pchTreeDef);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PowerTreeGetDescriptionByName);
const char *gclExprPowerTreeGetDescriptionByName(ExprContext *pContext, ACMD_EXPR_DICT(PowerTreeDef) SA_PARAM_NN_STR const char *pchTreeDef)
{
	PowerTreeDef *pTreeDef = (PowerTreeDef *) RefSystem_ReferentFromString("PowerTreeDef", pchTreeDef);
	return pTreeDef ? TranslateDisplayMessage(pTreeDef->pDescriptionMessage) : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AllegianceGetDisplayNameByName);
const char *gclExprAllegianceGetDisplayNameByName(ExprContext *pContext, SA_PARAM_NN_STR const char *pchAllegianceDef)
{
	AllegianceDef *pAllegianceDef = allegiance_FindByName(pchAllegianceDef);
	return pAllegianceDef ? TranslateDisplayMessage(pAllegianceDef->displayNameMsg) : "";
}

// Set this entity's name.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenEntSetName);
void CharacterCreation_GenEntSetName(ExprContext *pContext, SA_PARAM_NN_VALID Entity *pEntity, const char *pchName)
{
	if (pEntity && pEntity->pPlayer && pEntity->pSaved)
	{
		strcpy_s((char *)pEntity->pSaved->savedName, ARRAY_SIZE_CHECKED(pEntity->pSaved->savedName), pchName);
		entity_SetDirtyBit(pEntity, parse_SavedEntityData, pEntity->pSaved, false);
	}
}

// Set this entity's description.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenEntSetDescription);
void CharacterCreation_GenEntSetDescription(ExprContext *pContext, SA_PARAM_NN_VALID Entity *pEntity, const char *pchDescription)
{
	if (pEntity && pEntity->pPlayer && pEntity->pSaved && strlen(pchDescription) <= MAX_DESCRIPTION_LEN)
	{
		if(pEntity->pSaved->savedDescription)
			StructFreeString(DECONST(char*, pEntity->pSaved->savedDescription));
		DECONST(char*, pEntity->pSaved->savedDescription) = StructAllocString(pchDescription);
	}
}

// Get this entity's description
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_EntGetDescription);
const char *CharacterCreation_exprEntGetDescription(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
    return( (pEntity && pEntity->pSaved) ? (pEntity->pSaved->savedDescription) : (NULL));
}

//////////////////////////////////////////////////////////////////////////
// Choose starting powers
void ptui_AutoBuy(NOCONST(Entity) *pEnt)
{
	S32 i, c, j;

	for (i = eaSize(&pEnt->pChar->ppPowerTrees) - 1; i >= 0; i--)
	{
		PowerTree *pTree = (PowerTree*)pEnt->pChar->ppPowerTrees[i];
		PowerTreeDef *pTreeDef = GET_REF(pEnt->pChar->ppPowerTrees[i]->hDef);

		if (!pTreeDef)
			continue;

		for (j = eaSize(&pTreeDef->ppGroups) - 1; j >= 0; j--)
		{
			PTGroupDef *pGroupDef = pTreeDef->ppGroups[j];
			if (!pGroupDef)
				continue;
			for (c = eaSize(&pGroupDef->ppNodes) - 1; c >= 0; c--)
			{
				PTNodeDef *pNodeDef = pGroupDef->ppNodes[c];
				if (pNodeDef && pNodeDef->eFlag & kNodeFlag_AutoBuy)
				{
					S32 iRank = character_GetNextRank((Character*)pEnt->pChar, pNodeDef);
					while (iRank < eaSize(&pNodeDef->ppRanks) && pNodeDef->ppRanks[iRank]->iCost == 0 && character_CanBuyPowerTreeNode(PARTITION_CLIENT, (Character*)pEnt->pChar, pGroupDef, pNodeDef, iRank))
					{
						entity_PowerTreeNodeIncreaseRankHelper(PARTITION_CLIENT, pEnt, NULL, pTreeDef->pchName, pNodeDef->pchNameFull,false,false,false,NULL);
						iRank++;
					}
				}
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SetNumeric");
bool exprCharacterCreation_SetNumeric(const char *pchNumeric, S32 iValue)
{
	ItemDef *pItemDef = gclInvGetItemDef(pchNumeric);
	if (g_pFakePlayer && pItemDef)
	{
		if (inv_ent_trh_SetNumeric(ATR_EMPTY_ARGS, g_pFakePlayer, true, pchNumeric, iValue, NULL))
		{
			ptui_AutoBuy(g_pFakePlayer);
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_CanBuyPowerTree");
S32 exprCharacterCreation_CanBuyPowerTree(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEnt, const char *pchTree)
{
	S32 iResult = false;
	if(pEnt)
	{
		PowerTreeDef *pDef = powertreedef_Find(pchTree);
		if(pDef)
		{
			iResult = entity_CanBuyPowerTreeHelper(PARTITION_CLIENT,CONTAINER_NOCONST(Entity, pEnt), pDef, false);
		}
	}
	return iResult;
}

// Buy a power tree node, during character creation.
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_LIST(g_CharacterCreationCmds) ACMD_NAME("Buy_PowerTreeNode");
void CharacterCreation_BuyPowerTreeNode(const char *pchTree, const char *pchNodeFull)
{
	char *pchParsed = NULL;
	char *pchTreeName;
	char *pchGroupName = NULL;
	char *pchNodeName = NULL;
	estrStackCreate(&pchParsed);
	estrCopy2(&pchParsed, pchNodeFull);
	pchTreeName = pchParsed;
	pchGroupName = strchr(pchParsed, '.');
	if (pchGroupName)
	{
		*pchGroupName = '\0';
		pchNodeName = strchr(++pchGroupName, '.');
	}
	if (pchNodeName)
	{
		*pchNodeName = '\0';
		pchNodeName++;
	}

	if (pchNodeName)
	{
		PTNodeDef *pNode;
		PTGroupDef *pGroup;
		char achGroupFullName[1000];

		sprintf(achGroupFullName, "%s.%s", pchTreeName, pchGroupName);
		pGroup = powertreegroupdef_Find(achGroupFullName);
		pNode = powertreenodedef_Find(pchNodeFull);
		if (pNode && pGroup)
		{
			if (entity_PowerTreeNodeIncreaseRankHelper(PARTITION_CLIENT, g_pFakePlayer, NULL, pchTree, pchNodeFull,false,false,false,NULL))
			{
				eaPush(&g_CharacterCreationData->powerNodes, StructAllocString(pchNodeFull));
			}
		}
	}
	estrDestroy(&pchParsed);
}

// Auto buy recommended powers from a cost table
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_AutoBuyRecommendedPowersByCostTable");
void CharacterCreation_AutoBuyRecommendedPowersByCostTable(ExprContext *pContext, SA_PARAM_NN_STR const char *pchCostTable)
{
	PTNodeDef *pNextSuggestedNode = NULL;
	static char * s_estrTree = NULL;

	devassert(pchCostTable);

	if (pchCostTable == NULL || pchCostTable[0] == '\0')
		return;

	// Buy all powers from this cost table until there is nothing left to purchase
	while(pNextSuggestedNode = CharacterPath_GetNextSuggestedNodeFromCostTable(PARTITION_CLIENT, (Entity *)g_pFakePlayer, pchCostTable,false))
	{
		// Get the power tree name from the node
		estrClear(&s_estrTree);
		powertree_TreeNameFromNodeDef(pNextSuggestedNode, &s_estrTree);

		if (entity_PowerTreeNodeIncreaseRankHelper(PARTITION_CLIENT, g_pFakePlayer, NULL, s_estrTree, pNextSuggestedNode->pchNameFull, false, false, false, NULL))
		{
			eaPush(&g_CharacterCreationData->powerNodes, StructAllocString(pNextSuggestedNode->pchNameFull));
		}
		else
		{
			// This should never happen
			break;
		}
	}
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_LIST(g_CharacterCreationCmds) ACMD_NAME("Undo_Buy_PowerTreeNode");
void CharacterCreation_UndoBuyPowerTreeNode(void)
{
	if (eaSize(&g_CharacterCreationData->powerNodes))
	{
		char *pchNode = eaPop(&g_CharacterCreationData->powerNodes);
		S32 i, j;
		for (i = 0; i < eaSize(&g_pFakePlayer->pChar->ppPowerTrees); i++)
		{
			PowerTree *pTree = (PowerTree *)g_pFakePlayer->pChar->ppPowerTrees[i];
			for (j = 0; j < eaSize(&pTree->ppNodes); j++)
			{
				NOCONST(PTNode) *pNode = CONTAINER_NOCONST(PTNode, pTree->ppNodes[j]);
				if (IS_HANDLE_ACTIVE(pNode->hDef) && !stricmp(pchNode, REF_STRING_FROM_HANDLE(pNode->hDef)))
				{
					if (pNode->iRank > 0)
					{
						(int)(pNode->iRank)--;
					}
					else
					{
						eaRemove((void ***)&pTree->ppNodes, j);
						StructDestroyNoConst(parse_PTNode, pNode);
					}
					break;
				}
			}
		}
		free(pchNode);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenIsPowerTreeNodeBought");
bool CharacterCreation_IsPowerTreeNodeBought(const char *pchNodeFull)
{
	int i;
	if (eaSize(&g_CharacterCreationData->powerNodes))
	{
		for(i = 0; i < eaSize(&g_CharacterCreationData->powerNodes); i++) {
			if(stricmp(g_CharacterCreationData->powerNodes[i], pchNodeFull) == 0) {
				return true;
			}
		}
	}
	return false;
}


AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_LIST(g_CharacterCreationCmds) ACMD_NAME("Undo_Buy_PowerTreeNodeByName");
void CharacterCreation_UndoBuyPowerTreeNodeByName(const char *pchNodeFull)
{
	if (eaSize(&g_CharacterCreationData->powerNodes))
	{
		S32 i, j, k;

		//Find the power node and remove it
		for(k = 0; k < eaSize(&g_CharacterCreationData->powerNodes) && stricmp(g_CharacterCreationData->powerNodes[k], pchNodeFull) != 0; k++);
		if(k < eaSize(&g_CharacterCreationData->powerNodes)) {
			eaRemove(&g_CharacterCreationData->powerNodes, k);
		} else {
			return;
		}

		//Decrement the rank of the power on its power tree
		for (i = 0; i < eaSize(&g_pFakePlayer->pChar->ppPowerTrees); i++)
		{
			PowerTree *pTree = (PowerTree *)g_pFakePlayer->pChar->ppPowerTrees[i];
			for (j = 0; j < eaSize(&pTree->ppNodes); j++)
			{
				NOCONST(PTNode) *pNode = CONTAINER_NOCONST(PTNode, pTree->ppNodes[j]);
				if (IS_HANDLE_ACTIVE(pNode->hDef) && !stricmp(pchNodeFull, REF_STRING_FROM_HANDLE(pNode->hDef)))
				{
					if (pNode->iRank > 0)
					{
						(int)(pNode->iRank)--;
					}
					else
					{
						eaRemove((void ***)&pTree->ppNodes, j);
						StructDestroyNoConst(parse_PTNode, pNode);
					}
					break;
				}
			}
		}
	}
}

// Clear all power-buying choices made so far.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_ClearPowers");
void CharacterCreation_ClearPowers(SA_PARAM_NN_VALID Entity *pEnt)
{
	while (eaSize(&g_CharacterCreationData->powerNodes))
	{
		char *pchNode = eaPop(&g_CharacterCreationData->powerNodes);
		S32 i, j;
		for (i = 0; i < eaSize(&pEnt->pChar->ppPowerTrees); i++)
		{
			PowerTree *pTree = (PowerTree *)pEnt->pChar->ppPowerTrees[i];
			for (j = 0; j < eaSize(&pTree->ppNodes); j++)
			{
				NOCONST(PTNode) *pNode = CONTAINER_NOCONST(PTNode, pTree->ppNodes[j]);
				if (IS_HANDLE_ACTIVE(pNode->hDef) && !stricmp(pchNode, REF_STRING_FROM_HANDLE(pNode->hDef)))
				{
					if (pNode->iRank > 0)
					{
						(int)(pNode->iRank)--;
					}
					else
					{
						eaRemove((void ***)&pTree->ppNodes, j);
						StructDestroyNoConst(parse_PTNode, pNode);
					}
					break;
				}
			}
		}
		free(pchNode);
	}
	eaDestroyStruct(&pEnt->pChar->ppPowerTrees, parse_PowerTree);
}

// Buy a power in a specific node list index.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_BuyPowerIndex);
void CharacterCreation_BuyPowerIndex(SA_PARAM_NN_VALID Entity *pEnt, const char *pchNode, S32 i)
{
	while (eaSize(&g_CharacterCreationData->powerNodes) <= i)
	{
		eaPush(&g_CharacterCreationData->powerNodes, NULL);
	}
	assert(g_CharacterCreationData->powerNodes);
	SAFE_FREE(g_CharacterCreationData->powerNodes[i]);
	g_CharacterCreationData->powerNodes[i] = StructAllocString(pchNode);

}

// Buy all powers in the given group on this tree...
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_BuyGroupFromTree);
void CharacterCreation_BuyGroupFromTree(ExprContext *pContext, SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_VALID PowerTreeDef *pTreeDef, const char *pchGroup)
{
	bool bSuccess = true;
	S32 i;
	for (i = 0; i < eaSize(&pTreeDef->ppGroups); i++)
	{
		PTGroupDef *pGroup = pTreeDef->ppGroups[i];
		if (!stricmp(pGroup->pchGroup, pchGroup))
		{
			S32 j;
			for (j = 0; j < eaSize(&pGroup->ppNodes); j++)
			{
				PTNodeDef *pNodeDef = pGroup->ppNodes[j];
				CharacterCreation_BuyPowerTreeNode(pTreeDef->pchName, pNodeDef->pchNameFull);
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_BuyRandomNodesInPowerTree);
S32 CharacterCreation_ExprBuyRandomNodesInPowerTree(const char* pchTreeName)
{
	NOCONST(Entity)* pEnt = g_pFakePlayer;
	PowerTreeDef* pTreeDef = powertreedef_Find(pchTreeName);
	S32 i, iBuyCount = 0;
	if (pEnt && pTreeDef)
	{
		PTNodeDef** ppAvailableNodes = NULL;
		PowerTree* pTree = (PowerTree*)entity_FindPowerTreeHelper(pEnt, pTreeDef);

		for (i = eaSize(&pTreeDef->ppGroups)-1; i >= 0; i--)
		{
			PTGroupDef *pGroupDef = pTreeDef->ppGroups[i];

			while (gclGetMaxBuyablePowerTreeNodesInGroup((Entity*)pEnt,
													     pTree,
													     pGroupDef,
													     true,
													     &ppAvailableNodes) > 0)
			{
				S32 iRandomIdx = randomIntRange(0, eaSize(&ppAvailableNodes)-1);
				PTNodeDef* pBuyNode = eaRemove(&ppAvailableNodes, iRandomIdx);
				if (pBuyNode)
				{
					if (entity_PowerTreeNodeIncreaseRankHelper(PARTITION_CLIENT,
															   pEnt,
															   NULL,
															   pTreeDef->pchName,
															   pBuyNode->pchNameFull,
															   false,
															   false,
															   false,
															   NULL))
					{
						eaPush(&g_CharacterCreationData->powerNodes, StructAllocString(pBuyNode->pchNameFull));
						iBuyCount++;
					}
					else
					{
						break; //Shouldn't get here
					}
				}
				eaClear(&ppAvailableNodes);
			}
		}
		// Cleanup
		eaDestroy(&ppAvailableNodes);
	}
	return iBuyCount;
}

void CharacterCreation_InitPowers(const char *pchTreeName)
{
	PTNodeDef **ppNodes = NULL;

	entity_PowerTreeAddHelper(g_pFakePlayer,powertreedef_Find(pchTreeName));
	ptui_AutoBuy(g_pFakePlayer);

	PowerTree_GetAllAvailableNodes(PARTITION_CLIENT,(Character*)g_pFakePlayer->pChar,&ppNodes);

	if(eaSize(&ppNodes) == 1)
	{
		if(entity_PowerTreeNodeIncreaseRankHelper(PARTITION_CLIENT,g_pFakePlayer,NULL,pchTreeName,ppNodes[0]->pchNameFull,false,false,false,NULL))
		{
			if (eaFindString(&g_CharacterCreationData->powerNodes, ppNodes[0]->pchNameFull) < 0)
			{
				eaPush(&g_CharacterCreationData->powerNodes, StructAllocString(ppNodes[0]->pchNameFull));
			}
		}
	}

	eaDestroy(&ppNodes);
}

//Removes all powers from a given power tree
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_RemoveAllPowersInPowerTree");
void CharacterCreation_RemovePowers(const char *pchTreeName)
{
	PowerTreeDef *pTreeDef = powertreedef_Find(pchTreeName);
	S32 i, j;

	if (!pTreeDef)
		return;

	if (g_pFakePlayer)
	{
		for(i = eaSize(&g_pFakePlayer->pChar->ppPowerTrees)-1; i >= 0; i--)
		{
			NOCONST(PowerTree) *pTree = g_pFakePlayer->pChar->ppPowerTrees[i];
			if(GET_REF(pTree->hDef) == pTreeDef)
			{
				StructDestroyNoConst(parse_PowerTree, eaRemove(&g_pFakePlayer->pChar->ppPowerTrees, i));
			}
		}
	}

	if (g_pFakePlayer)
	{
		for (i = 0; i < eaSize(&pTreeDef->ppGroups); i++)
		{
			for (j = 0; j < eaSize(&pTreeDef->ppGroups[i]->ppNodes); j++)
			{
				PTNodeDef *pNode = pTreeDef->ppGroups[i]->ppNodes[j];
				int idx = eaFindString(&g_CharacterCreationData->powerNodes, pNode->pchNameFull);
				if (idx >= 0)
				{
					StructFreeString(eaRemove(&g_CharacterCreationData->powerNodes, idx));
				}
			}
		}
	}
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_LIST(g_CharacterCreationCmds) ACMD_NAME("CharacterCreation_InitPowers");
void CharacterCreationCmd_InitPowers(const char *pchTreeName)
{
	PTNodeDef **ppNodes = NULL;

	eaDestroyStructNoConst(&g_pFakePlayer->pChar->ppPowerTrees, parse_PowerTree);
	eaDestroyEx(&g_CharacterCreationData->powerNodes, NULL);

	CharacterCreation_InitPowers(pchTreeName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenEntGetOwnedPowerTrees");
void ui_GenExprEntGetOwnedPowerTrees(ExprContext *pContext, SA_PARAM_NN_VALID Entity *pEntity)
{
	Character *pChar = pEntity->pChar;
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	static PowerTreeDef **s_eaTrees;

	eaClear(&s_eaTrees);

	if (!pGen)
		return;
	if (pChar)
	{
		S32 i;
		for (i = 0; i < eaSize(&pChar->ppPowerTrees); i++)
		{
			PowerTree *pTree = eaGet(&pChar->ppPowerTrees, i);
			PowerTreeDef *pDef = pTree ? GET_REF(pTree->hDef) : NULL;
			if (pDef)
				eaPush(&s_eaTrees, pDef);
		}
	}
	ui_GenSetList(pGen, &s_eaTrees, parse_PowerTreeDef);
}

// Get a list of power trees this entity can buy (which depends on class, level, etc).
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenEntGetBuyablePowerTrees");
void ui_GenExprGetBuyablePowerTrees(ExprContext *pContext, SA_PARAM_NN_VALID Entity *pEntity)
{
	Character *pChar = pEntity->pChar;
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct("PowerTreeDef");
	static PowerTreeDef **s_eaTrees;

	eaClear(&s_eaTrees);

	if (pChar)
	{
		S32 i;
		for (i = 0; i < eaSize(&pStruct->ppReferents); i++)
		{
			PowerTreeDef *pDef = eaGet(&pStruct->ppReferents, i);
			if (pDef && character_CanBuyPowerTree(PARTITION_CLIENT, pChar, pDef))
				eaPush(&s_eaTrees, pDef);
		}
	}
	if (pGen)
		ui_GenSetList(pGen, &s_eaTrees, parse_PowerTreeDef);
}


// Set the Class of the character you are creating.
AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_ACCESSLEVEL(0) ACMD_LIST(g_CharacterCreationCmds) ACMD_NAME("CharacterCreation.SetOrigin") ACMD_NAME("CharacterCreation.SetClass");
void CharacterCreation_SetClass(const char *pchClass)
{
    CharacterClass *pClass;
	g_CharacterCreationData->className = allocAddString(pchClass);
	SET_HANDLE_FROM_STRING("CharacterClass", pchClass, g_pFakePlayer->pChar->hClass);
    pClass = GET_REF(g_pFakePlayer->pChar->hClass);
	if (pClass) {
		
		SpeciesDef *pSpecies = GET_REF(g_pFakePlayer->pChar->hSpecies);
		int i;
		if (eaSize(&pClass->eaPermittedSpecies) > 0) {
			for (i = 0; i < eaSize(&pClass->eaPermittedSpecies); i++) {
				SpeciesDefRef *pSpeciesRef = pClass->eaPermittedSpecies[i];
				if (GET_REF(pSpeciesRef->hSpecies) == pSpecies) {
					break;
				}
			}
			if (i >= eaSize(&pClass->eaPermittedSpecies)) {
				CharacterCreation_SavePlayerSpecies(GET_REF(pClass->eaPermittedSpecies[0]->hSpecies));
			}
		}
	}
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_SetClass);
void exprCharacterCreation_SetClass(const char *pchClass)
{
	CharacterCreation_SetClass(pchClass);
}


// Set the gender of the character you are creating.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_SetSkeleton);
bool CharacterCreationExpr_SetSkeleton(const char *pchSkeleton)
{
	if (g_pFakePlayer) {
		SpeciesDef *pSpecies = GET_REF(g_pFakePlayer->pChar->hSpecies);
		PCSkeletonDef *pSkel = RefSystem_ReferentFromString(g_hCostumeSkeletonDict, pchSkeleton);
		if (pSkel && pSpecies) {
			if (GET_REF(pSpecies->hSkeleton) != pSkel) {
				SpeciesDef **eaSpecies = NULL;
				species_GetSpeciesList(CONTAINER_RECONST(Entity, g_pFakePlayer), pSpecies->pcSpeciesName, pSkel, &eaSpecies);
				if (eaSize(&eaSpecies) > 0) {
					// Select the first species
					CharacterCreation_SavePlayerSpecies(eaSpecies[0]);
					eaDestroy(&eaSpecies);
					return true;
				}
				eaDestroy(&eaSpecies);
			} else {
				return true;
			}
		}
	}
	return false;
}

NOCONST(AssignedStats) * CharacterCreation_DD_GetNewAssignedStats(AttribType eAttribType, F32 fAbilityScore)
{
	NOCONST(AssignedStats) *pNewAssignedStats = NULL;
	S32 iCost = g_DDAbilityStatPointCostLookupTable[(S32)fAbilityScore - DD_MIN_ABILITY_SCORE];
	if (iCost == 0)
		return pNewAssignedStats;
	pNewAssignedStats = StructCreateNoConst(parse_AssignedStats);
	pNewAssignedStats->eType = eAttribType;
	pNewAssignedStats->iPoints = (S32)fAbilityScore - DD_MIN_ABILITY_SCORE;
	pNewAssignedStats->iPointPenalty = iCost - pNewAssignedStats->iPoints;

	return pNewAssignedStats;
}

// Resets the stat points to their defaults
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_ResetStatPoints");
void CharacterCreation_ResetStatPoints()
{
	S32 i;
	AttribType eAttribType[6];
	CharacterPath *pCharacterPath = GET_REF(g_pFakePlayer->pChar->hPath);

	// Remove all assigned stat points
	if (g_CharacterCreationData->assignedStats)
	{
		eaClearStructNoConst(&g_CharacterCreationData->assignedStats, parse_AssignedStats);
	}

	if (pCharacterPath == NULL || pCharacterPath->ppAssignedStats == NULL) // Defaults could not be loaded
	{
		// Add +2 to all stats as defaults (10 for all)
		eAttribType[0] = StaticDefineIntGetInt(AttribTypeEnum, "STR");
		eAttribType[1] = StaticDefineIntGetInt(AttribTypeEnum, "CON");
		eAttribType[2] = StaticDefineIntGetInt(AttribTypeEnum, "DEX");
		eAttribType[3] = StaticDefineIntGetInt(AttribTypeEnum, "INT");
		eAttribType[4] = StaticDefineIntGetInt(AttribTypeEnum, "WIS");
		eAttribType[5] = StaticDefineIntGetInt(AttribTypeEnum, "CHA");

		// Add all stat points
		for (i = 0; i < 6; i++)
		{
			eaPush(&g_CharacterCreationData->assignedStats, CharacterCreation_DD_GetNewAssignedStats(eAttribType[i], 10.0f));
		}
	}
	else // Use the defaults
	{
		// Add all non 0 assigned stats
		for (i = 0; i < eaSize(&pCharacterPath->ppAssignedStats); i++)
		{
			if (pCharacterPath->ppAssignedStats[i]->iPoints != 0)
			{
				// Add the assigned stat to the array after calculating the penalty
				eaPush(&g_CharacterCreationData->assignedStats,
					CharacterCreation_DD_GetNewAssignedStats(pCharacterPath->ppAssignedStats[i]->eType,
					DDGetBaseAbilityScore(StaticDefineIntRevLookup(AttribTypeEnum, pCharacterPath->ppAssignedStats[i]->eType)) + pCharacterPath->ppAssignedStats[i]->iPoints));
			}
		}
	}
}

F32 CharacterCreation_GetBaseAttribValue(const char *pszAttribName)
{
	// The attrib value does not exist. Push it in the array with its default value and return it
	if (gConf.eCCGetBaseAttribValues == CCGETBASEATTRIBVALUES_RETURN_DD_BASE)
	{
		return DDGetBaseAbilityScore(pszAttribName);
	}
	else if (gConf.eCCGetBaseAttribValues == CCGETBASEATTRIBVALUES_RETURN_CLASS_VALUE)
	{
		CharacterClass* pClass = GET_REF(g_pFakePlayer->pChar->hClass);
		if (pClass)
		{
			CharacterClassAttrib* pAttr = class_GetAttrib(pClass, StaticDefineInt_FastStringToInt(AttribTypeEnum, pszAttribName, 0));
			return pAttr && pAttr->pfBasic ? pAttr->pfBasic[0] : 0;
		}
	}
	return 0.0f;
}

F32 CharacterCreation_ApplyStatPointsToBase(const char *pszAttribName, NOCONST(AssignedStats) *pAssignedStats)
{
	if (gConf.eCCGetBaseAttribValues == CCGETBASEATTRIBVALUES_RETURN_DD_BASE)
		return DDApplyStatPointsToBaseAbilityScore(pszAttribName, pAssignedStats);
	else
		return pAssignedStats->iPoints + CharacterCreation_GetBaseAttribValue(pszAttribName);
}


// Gets the current initial attrib value
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_GetAttribValue");
F32 CharacterCreation_GetAttribValue(const char *pszAttribName)
{
	S32 i;
	AttribType eAttribType = StaticDefineIntGetInt(AttribTypeEnum, pszAttribName);

	if (eAttribType < 0)
		return 0.0f;

	// Check if the array is initialized already
	if (g_CharacterCreationData->assignedStats)
	{
		// Look for the key
		for (i = 0; i < eaSize(&g_CharacterCreationData->assignedStats); i++)
		{
			if (g_CharacterCreationData->assignedStats[i]->eType == eAttribType)
			{
				return CharacterCreation_ApplyStatPointsToBase(pszAttribName, g_CharacterCreationData->assignedStats[i]);
			}
		}
	}

	// Return the base value
	return CharacterCreation_GetBaseAttribValue(pszAttribName);
}

// Gets the current initial attrib value (effective)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_DD_GetEffectiveAttribValue");
F32 CharacterCreation_DD_GetEffectiveAttribValue(const char *pszAttribName)
{
	return CharacterCreation_GetAttribValue(pszAttribName);
}

// Returns the ability modifier
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_DD_GetAbilityMod");
S32 CharacterCreation_DD_GetAbilityMod(const char *pszAttribName)
{
	return floorf((F32)(CharacterCreation_GetAttribValue(pszAttribName) - 10) / 2.0f);
}

// Returns the AC
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_DD_GetAC");
S32 CharacterCreation_DD_GetAC()
{
	return 10 + max(CharacterCreation_DD_GetAbilityMod("INT"), CharacterCreation_DD_GetAbilityMod("DEX"));
}

// Returns the fortitude
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_DD_GetFortitude");
S32 CharacterCreation_DD_GetFortitude()
{
	return 10 + max(CharacterCreation_DD_GetAbilityMod("STR"), CharacterCreation_DD_GetAbilityMod("CON"));
}

// Returns the reflex
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_DD_GetReflex");
S32 CharacterCreation_DD_GetReflex()
{
	return 10 + max(CharacterCreation_DD_GetAbilityMod("INT"), CharacterCreation_DD_GetAbilityMod("DEX"));
}

// Returns the willpower
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_DD_GetWillpower");
S32 CharacterCreation_DD_GetWillpower()
{
	return 10 + max(CharacterCreation_DD_GetAbilityMod("WIS"), CharacterCreation_DD_GetAbilityMod("CHA"));
}

// Returns the points left to distribute to ability scores
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_GetPointsLeft");
S32 CharacterCreation_GetPointsLeft()
{
	if (gConf.eCCGetPointsLeft == CCGETPOINTSLEFT_RETURN_USE_DD_POINT_SYSTEM)
	{
		return DDGetPointsLeftForAbilityScores(g_CharacterCreationData->assignedStats);
	}
	return 0;
}

// Returns the points left to distribute to ability scores
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_GetPointCostToIncrease");
S32 CharacterCreation_GetPointCostToIncrease(F32 val)
{
	int iValue = (int)(CLAMP(val, 8, 17));
	return g_DDAbilityStatPointCostLookupTable[iValue+1 - DD_MIN_ABILITY_SCORE] - g_DDAbilityStatPointCostLookupTable[iValue - DD_MIN_ABILITY_SCORE];

}

bool CharacterCreation_DD_IsValidAttribChange(NOCONST(AssignedStats) **eaAssignedStats, const char * pszAttribName, F32 fDelta)
{
	S32 iCurrentValue = CharacterCreation_GetAttribValue(pszAttribName);
	S32 iNewValue = iCurrentValue + (S32)fDelta;
	S32 iPointsLeft = DDGetPointsLeftForAbilityScores(eaAssignedStats);
	S32 iPointNeededForChange;
	S32 iAttribCountLessThan10 = 0;
	AttribType eAttribType = StaticDefineIntGetInt(AttribTypeEnum, pszAttribName);

	// Validation
	if (eAttribType < 0 || iPointsLeft == -1)
		return false;

	// Only single point changes are allowed
	if (fDelta != -1.0f && fDelta != 1.0f)
	{
		return false;
	}

	// We cannot go beyond 18 and below 8
	if ((iNewValue < 8) || (iNewValue > 18))
		return false;

	// See if the attrib set is valid
	if (!DDIsAbilityScoreSetValid(eaAssignedStats, false))
	{
		return false;
	}

	// See if we can really go below 10
	if (iNewValue < 10)
	{
		S32 i;
		// Any stat that does not exist in the array is less than 10
		iAttribCountLessThan10 += (6 - eaSize(&eaAssignedStats));

		if (iAttribCountLessThan10 > 1)
			return false;

		for (i = 0; i < eaSize(&eaAssignedStats); i++)
		{
			if (eaAssignedStats[i]->eType == eAttribType || DD_MIN_ABILITY_SCORE + eaAssignedStats[i]->iPoints < 10.0f)
			{
				iAttribCountLessThan10++;
			}
			if (iAttribCountLessThan10 > 1)
				return false;
		}
	}

	// See how many points we will need to do the change
	iPointNeededForChange = g_DDAbilityStatPointCostLookupTable[iNewValue - DD_MIN_ABILITY_SCORE] - g_DDAbilityStatPointCostLookupTable[iCurrentValue - DD_MIN_ABILITY_SCORE];

	return (iPointsLeft - iPointNeededForChange) >= 0;
}

// Applies the delta to the existing stat point. This function does not do validation. It's done prior to calling this function.
void CharacterCreation_DD_ApplyAttribDelta(NOCONST(AssignedStats) **eaAssignedStats, const char * pszAttribName, F32 fDelta)
{
	NOCONST(AssignedStats) *pCurrentAssignedStats = NULL;
	S32 i;
	AttribType eAttribType = StaticDefineIntGetInt(AttribTypeEnum, pszAttribName);

	if (eAttribType < 0)
		return;

	if (fDelta != 1.0f && fDelta != -1.0f)
		return;

	if (eaAssignedStats)
	{
		for (i = eaSize(&eaAssignedStats) - 1; i >= 0; i--)
		{
			if (eaAssignedStats[i]->eType == eAttribType)
			{
				// We found the existing value
				if (eaAssignedStats[i]->iPoints + (S32)fDelta == 0) // Remove the current value because the delta makes the attribute 8 which requires no stat points
				{
					eaRemove(&eaAssignedStats, i);
					return;
				}
				else // Update the values
				{
					eaAssignedStats[i]->iPoints += fDelta;
					eaAssignedStats[i]->iPointPenalty = g_DDAbilityStatPointCostLookupTable[eaAssignedStats[i]->iPoints] - eaAssignedStats[i]->iPoints;
					return;
				}
			}
		}
	}
	// Since an existing stat does not exist we only need to take care of +1 case
	if (fDelta == 1.0f)
	{
		eaPush(&eaAssignedStats,
			CharacterCreation_DD_GetNewAssignedStats(eAttribType, DDGetBaseAbilityScore(pszAttribName) + fDelta)
		);
	}
}

// Validates the attrib change
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_IsValidAttribChange");
bool CharacterCreation_IsValidAttribChange(const char *pszAttribName, F32 fDelta)
{
	if (gConf.eCCValidateAttribChanges == CCVALIDATEATTRIBCHANGES_USE_DD_RULES)
	{
		return CharacterCreation_DD_IsValidAttribChange(g_CharacterCreationData->assignedStats, pszAttribName, fDelta);
	}
	return false;
}

// Increments/Decrements the attrib value if allowed
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_ModifyAttribValue");
void CharacterCreation_ModifyAttribValue(const char *pszAttribName, F32 fDelta)
{
	if (CharacterCreation_IsValidAttribChange(pszAttribName, fDelta))
	{
		if (gConf.eCCGetBaseAttribValues == CCGETBASEATTRIBVALUES_RETURN_DD_BASE)
		{
			CharacterCreation_DD_ApplyAttribDelta(g_CharacterCreationData->assignedStats, pszAttribName, fDelta);
		}
		else
		{
			AttribType eAttribType = StaticDefineIntGetInt(AttribTypeEnum, pszAttribName);
			NOCONST(AssignedStats) *pAssignedStats = NULL;
			S32 i;

			if (eAttribType < 0)
				return;

			// Try to find an existing one
			if (g_CharacterCreationData->assignedStats)
			{
				for (i = 0; i < eaSize(&g_CharacterCreationData->assignedStats); i++)
				{
					if (g_CharacterCreationData->assignedStats[i]->eType == eAttribType)
					{
						g_CharacterCreationData->assignedStats[i]->iPoints += fDelta;
						return;
					}
				}
			}

			// Create a new AssignedStats and add it to the array
			pAssignedStats = StructCreateNoConst(parse_AssignedStats);
			pAssignedStats->eType = eAttribType;
			pAssignedStats->iPoints = fDelta;
			pAssignedStats->iPointPenalty = 0;
			eaPush(&g_CharacterCreationData->assignedStats, pAssignedStats);
		}
	}
}

// Set the power tree of the character you are creating.
AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_ACCESSLEVEL(0) ACMD_LIST(g_CharacterCreationCmds) ACMD_NAME("CharacterCreation.SetPowerTree");
void CharacterCreation_SetPowerTree(const char *pchPowerTree)
{
	g_CharacterCreationData->powerTreeName = allocAddString(pchPowerTree);
}

AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_ACCESSLEVEL(0) ACMD_LIST(g_CharacterCreationCmds) ACMD_NAME("CharacterCreation.InitPuppet");
void CharacterCreataion_InitPuppets(void)
{
	S32 i;

	eaClearStruct(&g_CharacterCreationData->puppetInfo, parse_LoginPuppetInfo);

	// Fill in the list of puppet requests with defaults
	if (g_PetRestrictions.iRequiredPuppetRequestCount)
	{
		for (i = 0; i < eaSize(&g_PetRestrictions.eaAllowedPuppetRequests); i++)
		{
			PuppetRequestChoice* pChoice = g_PetRestrictions.eaAllowedPuppetRequests[i];

			if (!pChoice->pcAllegiance ||
				stricmp(g_pcAllegianceName, pChoice->pcAllegiance)==0)
			{
				LoginPuppetInfo* pInfo = StructCreate(parse_LoginPuppetInfo);
				pInfo->pchType = StructAllocString(pChoice->pcCritterDef);
				pInfo->pchName = NULL;
				eaPush(&g_CharacterCreationData->puppetInfo, pInfo);

				if (eaSize(&g_CharacterCreationData->puppetInfo) >= g_PetRestrictions.iRequiredPuppetRequestCount)
				{
					break;
				}
			}
		}
	}
}

AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_ACCESSLEVEL(0) ACMD_LIST(g_CharacterCreationCmds) ACMD_NAME("CharacterCreation.InitPet");
void CharacterCreataion_InitPets(void)
{
	S32 i;

	eaClearStruct(&g_CharacterCreationData->petInfo, parse_LoginPetInfo);

	// Fill in the list of pet requests with defaults
	if (g_PetRestrictions.iRequiredPetRequestCount)
	{
		for (i = 0; i < eaSize(&g_PetRestrictions.eaAllowedPetRequests); i++)
		{
			PetRequestChoice* pChoice = g_PetRestrictions.eaAllowedPetRequests[i];

			if (!pChoice->pcAllegiance ||
				stricmp(g_pcAllegianceName, pChoice->pcAllegiance)==0)
			{
				LoginPetInfo* pInfo = StructCreate(parse_LoginPetInfo);
				pInfo->pchType = StructAllocString(pChoice->pcPetDef);
				pInfo->pchName = NULL;
				eaPush(&g_CharacterCreationData->petInfo, pInfo);

				if (eaSize(&g_CharacterCreationData->petInfo) >= g_PetRestrictions.iRequiredPetRequestCount)
				{
					break;
				}
			}
		}
	}
}

// Add a puppet to the character you are creating.
AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_ACCESSLEVEL(0) ACMD_LIST(g_CharacterCreationCmds) ACMD_NAME("CharacterCreation.AddPuppet");
void CharacterCreation_AddPuppet(const char *pchCritterDef)
{
	S32 i;
	// Check for duplicates
	for (i = eaSize(&g_CharacterCreationData->puppetInfo)-1; i >= 0; i--)
	{
		LoginPuppetInfo* pInfo = g_CharacterCreationData->puppetInfo[i];
		if (stricmp(pInfo->pchType, pchCritterDef)==0)
		{
			break;
		}
	}
	if (i < 0)
	{
		LoginPuppetInfo* pInfo = StructCreate(parse_LoginPuppetInfo);
		pInfo->pchType = StructAllocString(pchCritterDef);
		pInfo->pchName = NULL;
		eaPush(&g_CharacterCreationData->puppetInfo, pInfo);
	}
}

// set name and costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SetNameAndCostume");
void CharacterCreationCmd_SetNameAndCostume(void)
{
	CharacterCreation_CreateCharacter(false, NULL, NULL, NULL, false);
}

// Create the character (and then pick map)
AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME("CharacterCreation.FinishAll");
void CharacterCreationCmd_FinishAll(void)
{
	CharacterCreation_CreateCharacter(true, NULL, NULL, NULL, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_BeginPlaying");
void CharacterCreationExpr_BeginPlaying(void)
{
	CharacterCreation_CreateCharacter(true, NULL, NULL, NULL, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_AcceptCharacter");
void CharacterCreationExpr_AcceptCharacter()
{
	CharacterCreation_CreateCharacter(true, NULL, NULL, NULL, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_AcceptCharacterAndGoToLobby");
void CharacterCreationExpr_AcceptCharacterAndGoToLobby()
{
	CharacterCreation_CreateCharacter(true, NULL, NULL, NULL, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SaveNameForPet");
bool CharacterCreation_SaveNameForPet( U32 iPetIndex, SA_PARAM_NN_STR const char* pchName )
{
	if ( iPetIndex >= eaUSize( &g_CharacterCreationData->petInfo ) )
		return false;

	if ( CharacterCreation_IsNameValidWithErrorMessage( pchName, kNotifyType_NameInvalid ) )
	{
		g_CharacterCreationData->petInfo[iPetIndex]->pchName = StructAllocString( pchName );

		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SaveTypeForPet");
bool CharacterCreation_SaveTypeForPet( U32 iPetIndex, SA_PARAM_NN_STR const char* pchType )
{
	if ( iPetIndex >= eaUSize( &g_CharacterCreationData->petInfo ) )
		return false;

	g_CharacterCreationData->petInfo[iPetIndex]->pchType = StructAllocString( pchType );

	return true;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SaveSpeciesForPet");
bool CharacterCreation_SaveSpeciesForPet( U32 iPetIndex, SA_PARAM_OP_VALID SpeciesDef* pSpecies )
{
	if ( iPetIndex >= eaUSize( &g_CharacterCreationData->petInfo ) )
		return false;

	g_CharacterCreationData->petInfo[iPetIndex]->pSpecies = pSpecies;

	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SaveCostumePresetForPet");
bool CharacterCreation_SaveCostumePresetForPet( U32 iPetIndex, SA_PARAM_NN_VALID UIGen* pParentGen, SA_PARAM_OP_VALID CostumePreset* pPreset )
{
	SpeciesDef* pSpecies = ui_GenGetPointer( pParentGen, parse_SpeciesDef, NULL );

	if ( pPreset==NULL || iPetIndex >= eaUSize( &g_CharacterCreationData->petInfo ) )
		return false;

	if ( pSpecies )
	{
		S32 i;
		for ( i = 0; i < eaSize( &pSpecies->eaPresets ); i++ )
		{
			if ( pPreset == pSpecies->eaPresets[i] )
			{
				g_CharacterCreationData->petInfo[iPetIndex]->iPreset = i;
				return true;
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SaveCostumePresetForPetDefault");
void CharacterCreation_SaveCostumePresetForPetDefault( U32 iPetIndex )
{
	g_CharacterCreationData->petInfo[iPetIndex]->iPreset = 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SaveNameForPuppet");
bool CharacterCreation_SaveNameForPuppet( U32 iPupIndex, SA_PARAM_NN_STR const char* pchName )
{
	if ( iPupIndex >= eaUSize( &g_CharacterCreationData->puppetInfo ) )
		return false;

	if ( CharacterCreation_IsNameValidWithErrorMessage( pchName, kNotifyType_NameInvalid ) )
	{
		g_CharacterCreationData->puppetInfo[iPupIndex]->pchName = StructAllocString( pchName );

		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_GetShipNamePrefix");
const char *CharacterCreation_GetShipNamePrefix(U32 iPupIndex, SA_PARAM_NN_STR const char* pchName)
{
	if (!pchName)
		pchName = "";

	if (iPupIndex < eaUSize(&g_CharacterCreationData->puppetInfo))
	{
		AllegianceDef *pDef = g_pFakePlayer ? GET_REF(g_pFakePlayer->hAllegiance) : NULL;
		AllegianceDef *pSubDef = g_pFakePlayer ? GET_REF(g_pFakePlayer->hSubAllegiance) : NULL;
		const char *pchPrefix = NULL;
		const char *pchExpectedPrefix = NULL;

		// Check to see if a prefix was specified in the name
		pchPrefix = allegiance_GetNamePrefix(pDef, pSubDef, NULL, LoginGetAccountData(), pchName, &pchExpectedPrefix);
		if (pchExpectedPrefix && pchPrefix && stricmp(pchPrefix, pchExpectedPrefix))
			return pchExpectedPrefix;

		return FIRST_IF_SET(pchPrefix, pchExpectedPrefix);
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_GetPuppetName");
const char *CharacterCreation_GetPuppetName(U32 iPupIndex)
{
	if (iPupIndex < eaUSize(&g_CharacterCreationData->puppetInfo))
		return g_CharacterCreationData->puppetInfo[iPupIndex]->pchName;
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_ValidateShipName");
bool CharacterCreation_ValidateShipName(U32 iPupIndex, SA_PARAM_NN_STR const char* pchName)
{
	if (iPupIndex < eaUSize(&g_CharacterCreationData->puppetInfo) && pchName)
	{
		static char* s_estrBuffer = NULL;
		static char* s_estrNameBuffer = NULL;
		AllegianceDef *pDef = g_pFakePlayer ? GET_REF(g_pFakePlayer->hAllegiance) : NULL;
		AllegianceDef *pSubDef = g_pFakePlayer ? GET_REF(g_pFakePlayer->hSubAllegiance) : NULL;
		const char *pchPrefix = NULL;
		const char *pchExpectedPrefix = NULL;

		// Check to see if a prefix was specified in the name
		pchPrefix = allegiance_GetNamePrefix(pDef, pSubDef, NULL, LoginGetAccountData(), pchName, &pchExpectedPrefix);
		if (pchExpectedPrefix && pchPrefix && stricmp(pchPrefix, pchExpectedPrefix))
		{
			char *estrError = NULL;
			if (!stricmp(pchExpectedPrefix, "U.S.S. "))
				FormatMessageKey(&estrError, "NameFormat_ShipName_NotBeginUSS", STRFMT_END);
			else if (!stricmp(pchExpectedPrefix, "I.K.S. "))
				FormatMessageKey(&estrError, "NameFormat_ShipName_NotBeginIKS", STRFMT_END);
			else
				FormatMessageKey(&estrError, "NameFormat_ShipName_NotBeginPrefix", STRFMT_STRING("Prefix", pchExpectedPrefix), STRFMT_END);
			notify_NotifySend(NULL, kNotifyType_ShipNameInvalid, estrError, NULL, NULL);
			estrDestroy(&estrError);
			return false;
		}

		// Strip the prefix if one was found
		if (pchPrefix)
			pchName += strlen(pchPrefix);
		else
			pchPrefix = pchExpectedPrefix;

		// Validate name without prefix
		if (!CharacterCreation_IsNameValidWithErrorMessage(pchName, kNotifyType_ShipNameInvalid))
			return false;

		// Add prefix to name
		estrClear(&s_estrBuffer);
		if (pchPrefix)
			estrAppend2(&s_estrBuffer, pchPrefix);
		estrAppend2(&s_estrBuffer, pchName);

		// Validate name with prefix
		if (!CharacterCreation_IsNameValidWithErrorMessage(s_estrBuffer, kNotifyType_ShipNameInvalid))
			return false;

		StructCopyString(&g_CharacterCreationData->puppetInfo[iPupIndex]->pchName, s_estrBuffer);
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_AcceptName");
bool CharacterCreationExpr_AcceptName(const char *pchName)
{
	if(g_pFakePlayer && g_pFakePlayer->pSaved)
	{
		return CharacterCreation_CreateCharacter(true, entGetPersistedName((Entity *)g_pFakePlayer), NULL, g_pFakePlayer->pSaved->savedDescription, false);
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SavePlayerCostume");
void CharacterCreation_SavePlayerCostume(void)
{
	CostumeCreator_SaveCostumeDefault(kPCPay_Default);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SavePlayerSpeciesByName");
void CharacterCreation_SavePlayerSpeciesByName(SA_PARAM_OP_VALID const char* pcSpeciesName)
{
	if(pcSpeciesName && g_pFakePlayer && g_pFakePlayer->pChar) {
		SET_HANDLE_FROM_STRING("Species", pcSpeciesName, g_pFakePlayer->pChar->hSpecies);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SavePlayerSpecies");
void CharacterCreation_SavePlayerSpecies(SA_PARAM_OP_VALID SpeciesDef* pSpecies)
{
	if(pSpecies && g_pFakePlayer && g_pFakePlayer->pChar) {
		SET_HANDLE_FROM_STRING("Species", pSpecies->pcName, g_pFakePlayer->pChar->hSpecies);
	}
}

static void SpeciesGetMicroTransactionUIProducts(MicroTransactionUIProduct ***peaUIProducts, SA_PARAM_OP_VALID SpeciesDef *pSpecies)
{
	static MicroTransactionProduct **s_eaProducts;
	S32 i, j;

	// TODO: put this somewhere that makes sense, currently it depends on the functions at the beginning of the file
	if (GetAllSpeciesProducts(&s_eaProducts, pSpecies)) {
		for (i = 0; i < eaSize(&s_eaProducts); i++) {
			MicroTransactionProduct *pProduct = s_eaProducts[i];
			MicroTransactionUIProduct *pUIProduct = eaGet(peaUIProducts, i);
			if (!pUIProduct || pUIProduct->uID != pProduct->uID) {
				pUIProduct = NULL;
				for (j = i + 1; j < eaSize(peaUIProducts); j++) {
					if ((*peaUIProducts)[j]->uID == pProduct->uID) {
						pUIProduct = eaRemove(peaUIProducts, j);
						break;
					}
				}

				if (!pUIProduct) {
					pUIProduct = gclMicroTrans_MakeUIProduct(pProduct->uID);
				}

				if (pUIProduct) {
					eaInsert(peaUIProducts, pUIProduct, i);
				}
			}
		}
	}

	eaSetSizeStruct(peaUIProducts, parse_MicroTransactionUIProduct, eaSize(&s_eaProducts));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetSpeciesUnlockProductID);
S32 exprGetSpeciesUnlockProductID(SA_PARAM_OP_VALID SpeciesDef *pSpecies)
{
	static MicroTransactionUIProduct **s_eaProducts;
	S32 i;
	U32 uID = 0;

	// TODO: see SpeciesGetMicroTransactionUIProducts
	SpeciesGetMicroTransactionUIProducts(&s_eaProducts, pSpecies);
	for (i = 0; i < eaSize(&s_eaProducts); i++) {
		if (!uID || (s_eaProducts[i]->bCannotPurchaseAgain || s_eaProducts[i]->bUniqueInInv)) {
			uID = s_eaProducts[i]->uID;
		}
	}

	return uID;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntGetSpeciesUnlockProductID);
S32 exprEntGetSpeciesUnlockProductID(SA_PARAM_OP_VALID Entity *pEnt)
{
	SpeciesDef *pSpecies = NULL;
	if (pEnt && pEnt->pChar) {
		// NOT GENERAL!
		pSpecies = GET_REF(pEnt->pChar->hSpecies);
	}
	return exprGetSpeciesUnlockProductID(pSpecies);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetSpeciesUnlockProductList);
void exprGetSpeciesUnlockProductList(SA_PRE_NN_VALID SA_POST_P_FREE UIGen *pGen, SA_PARAM_OP_VALID SpeciesDef *pSpecies)
{
	// TODO: see SpeciesGetMicroTransactionUIProducts
	MicroTransactionUIProduct ***peaProducts = ui_GenGetManagedListSafe(pGen, MicroTransactionUIProduct);
	SpeciesGetMicroTransactionUIProducts(peaProducts, pSpecies);
	ui_GenSetManagedListSafe(pGen, peaProducts, MicroTransactionUIProduct, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntGetSpeciesUnlockProductList);
void exprEntGetSpeciesUnlockProductList(SA_PRE_NN_VALID SA_POST_P_FREE UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	SpeciesDef *pSpecies = NULL;
	if (pEnt && pEnt->pChar) {
		// NOT GENERAL!
		pSpecies = GET_REF(pEnt->pChar->hSpecies);
	}
	exprGetSpeciesUnlockProductList(pGen, pSpecies);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetSpeciesUnlockProductListSize);
S32 exprGetSpeciesUnlockProductListSize(SA_PARAM_OP_VALID SpeciesDef *pSpecies)
{
	// TODO: see SpeciesGetMicroTransactionUIProducts
	static MicroTransactionUIProduct **s_eaProducts;
	SpeciesGetMicroTransactionUIProducts(&s_eaProducts, pSpecies);
	return eaSize(&s_eaProducts);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntGetSpeciesUnlockProductListSize);
S32 exprEntGetSpeciesUnlockProductListSize(SA_PARAM_OP_VALID Entity *pEnt)
{
	SpeciesDef *pSpecies = NULL;
	if (pEnt && pEnt->pChar) {
		// NOT GENERAL!
		pSpecies = GET_REF(pEnt->pChar->hSpecies);
	}
	return exprGetSpeciesUnlockProductListSize(pSpecies);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CanUseSpeciesDef);
bool exprCanUseSpeciesDef(SA_PARAM_OP_VALID SpeciesDef *pSpecies)
{
	return pSpecies && CanPlayerUseSpecies(pSpecies, false, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CanUseSpeciesByName);
bool exprCanUseSpeciesByName(SA_PARAM_NN_STR const char *pchSpeciesName )
{
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct("SpeciesDef");
	S32 i;

	if (pchSpeciesName && pchSpeciesName[0])
	{
		for (i = 0; i < eaSize(&pArray->ppReferents); i++)
		{
			SpeciesDef *pSpecies = (SpeciesDef*)pArray->ppReferents[i];

			if (pSpecies && stricmp(pSpecies->pcSpeciesName, pchSpeciesName) == 0)
			{
				return CanPlayerUseSpecies(pSpecies, false, false);
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SetRandomPlayerSpeciesAndCostume");
void CharacterCreation_SetRandomPlayerSpeciesAndCostume(bool bExcludeCustom)
{
	static SpeciesDef **s_eaSpecies = NULL;
	SpeciesDef *pSelectedSpecies = NULL;
	S32 iSize;

	// We used to use bExcludeCustom to fill the speciesList. This functionality no longer exists.
	CharacterCreation_FillSpeciesListEx(&s_eaSpecies, g_pcAllegianceName, 0);

	iSize = eaSize(&s_eaSpecies);
	if (iSize == 1)
	{
		pSelectedSpecies = s_eaSpecies[0];
	}
	else if (iSize > 1)
	{
		S32 iRandomIdx = randomIntRange(0, iSize-1);
		pSelectedSpecies = s_eaSpecies[iRandomIdx];
	}
	if (pSelectedSpecies)
	{
		PlayerCostume* pCostume = CharacterCreation_GetDefaultCostumeForSpecies(pSelectedSpecies, false);
		CharacterCreation_SavePlayerSpecies(pSelectedSpecies);
		CostumeCreator_SetSkeletonPtr(GET_REF(pSelectedSpecies->hSkeleton));
		CharacterCreation_SetCostumePtr(pCostume);
	}
	eaClearFast(&s_eaSpecies);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_GetCurPlayerSpecies");
const char *CharacterCreation_GetCurPlayerSpecies(void)
{
	if (g_pFakePlayer)
	{
        SpeciesDef *speciesDef = GET_REF(g_pFakePlayer->pChar->hSpecies);
        if ( speciesDef )
        {
		    return speciesDef->pcName;
        }
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_GetCurPlayerSpeciesNameOrder");
const char *CharacterCreation_GetCurPlayerSpeciesNameOrder(void)
{
    if (g_pFakePlayer)
    {
        SpeciesDef *speciesDef = GET_REF(g_pFakePlayer->pChar->hSpecies);
        if ( speciesDef )
        {
            if (speciesDef->eNameOrder == kNameOrder_FML)
            {
                return "FML";
            }
            else
            {
                return "LFM";
            }
        }
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SavePlayerClass");
void CharacterCreation_SavePlayerClass(SA_PARAM_OP_VALID const char *pchPowerTree, const char *pchClass)
{
	g_CharacterCreationData->powerTreeName = allocAddString(pchPowerTree);
	g_CharacterCreationData->className = allocAddString(pchClass);
	SET_HANDLE_FROM_STRING("CharacterClass", pchClass, g_pFakePlayer->pChar->hClass);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SavePlayerCostumePreset");
bool CharacterCreation_SavePlayerCostumePreset(SA_PARAM_NN_VALID UIGen* pParentGen, CostumePreset* pPreset)
{
	SpeciesDef* pSpecies = ui_GenGetPointer( pParentGen, parse_SpeciesDef, NULL );

	if ( pSpecies )
	{
		S32 i;
		for ( i = 0; i < eaSize( &pSpecies->eaPresets ); i++ )
		{
			if ( pPreset == pSpecies->eaPresets[i] )
			{
				g_CharacterCreationData->costumePreset = i;
				return true;
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SaveRandomPlayerCostumePreset");
bool CharacterCreation_SaveRandomPlayerCostumePreset(SA_PARAM_NN_VALID UIGen* pParentGen)
{
	SpeciesDef* pSpecies = ui_GenGetPointer( pParentGen, parse_SpeciesDef, NULL );

	if ( pSpecies )
	{
		if (eaSize( &pSpecies->eaPresets ))
		{
			g_CharacterCreationData->costumePreset = randomIntRange( 0, eaSize( &pSpecies->eaPresets )-1 );
			return true;
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SavePlayerCostumePresetDefault");
void CharacterCreation_SavePlayerCostumePresetDefault(void)
{
	g_CharacterCreationData->costumePreset = 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_SavePetCostume");
void CharacterCreation_SavePetCostume( U32 iPetIndex )
{
	if ( iPetIndex >= eaUSize( &g_CharacterCreationData->petInfo ) )
		return;

	g_CharacterCreationData->petInfo[iPetIndex]->pCostume = StructCloneNoConst(parse_PlayerCostume, CostumeUI_GetCostume());
}

U32 CharacterCreation_RandomizeExclusive( SpeciesDef** eaSpecies, PetDef* pPetDef, U32* eaExclude )
{
	if ( eaSpecies && pPetDef )
	{
		S32 iMax = eaSize(&eaSpecies)-1;
		S32 iStart = -1;
		S32 iRand = randomIntRange( 0, iMax );

		while ( (CharClassTypes)eaSpecies[iRand]->eType == StaticDefineIntGetInt(CharClassTypesEnum, "Space") || ea32Find( &eaExclude, iRand ) >= 0 || !stricmp(eaSpecies[iRand]->pcName,"None")) //eaSpecies[iRand]->eClassType != ePetClass
		{
			if ( iStart < 0 )
				iStart = iRand;

			if ( ++iRand > iMax ) iRand = 0;

			if ( iRand == iStart )
				break;
		}

		return iRand;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_RandomizeUnusedPetCostumes");
void CharacterCreation_RandomizeUnusedPetCostumes()
{
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct("SpeciesDef");

	U32 i, c;
	U32 iPetArraySize = eaSize(&g_CharacterCreationData->petInfo);
	U32 iSpeciesArraySize = eaSize( &pArray->ppReferents );

	U32* eaUsed = NULL;
    SpeciesDef *speciesDef = NULL;
    if ( g_pFakePlayer )
    {
        speciesDef = GET_REF(g_pFakePlayer->pChar->hSpecies);
    }

	ea32Create( &eaUsed );

	for ( c = 0; c < iSpeciesArraySize; c++ )
	{
		if ( speciesDef == (SpeciesDef*)(pArray->ppReferents[c]) )
		{
			ea32Push( &eaUsed, c );
			break;
		}
	}

	for ( i = 0; i < iPetArraySize; i++ )
	{
		NOCONST(PlayerCostume)* pCostume = g_CharacterCreationData->petInfo[i]->pCostume;

		if ( pCostume==NULL )
		{
			PetDef *pPetDef = RefSystem_ReferentFromString("PetDef",g_CharacterCreationData->petInfo[i]->pchType);
			U32 iRand = CharacterCreation_RandomizeExclusive((SpeciesDef**)pArray->ppReferents,pPetDef,eaUsed);
            S32 iPresetMax;
            CostumePreset* pPreset;
            PlayerCostume* pPresetCostume;

            speciesDef = (SpeciesDef*)(pArray->ppReferents[iRand]);
            iPresetMax = (speciesDef && eaSize( &speciesDef->eaPresets ) > 0) ? eaSize(&speciesDef->eaPresets)-1 : 0;

			pPreset = (speciesDef && speciesDef->eaPresets) ?
				(speciesDef->eaPresets[randomIntRange( 0, iPresetMax )]) : NULL;

			pPresetCostume = pPreset ? GET_REF(pPreset->hCostume ) : NULL;

			g_CharacterCreationData->petInfo[i]->pCostume =
				pPresetCostume ? StructCloneDeConst( parse_PlayerCostume, pPresetCostume ) : NULL;

			ea32Push( &eaUsed, iRand );
		}
		else
		{
			if ( !g_CharacterCreationData->petInfo[i]->bIsCustomSpecies )
			{
				for ( c = 0; c < iSpeciesArraySize; c++ )
				{
					if ( g_CharacterCreationData->petInfo[i]->pSpecies == (SpeciesDef*)(pArray->ppReferents[c]) )
					{
						ea32Push( &eaUsed, c );
						break;
					}
				}
			}
		}
	}

	ea32Destroy( &eaUsed );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_LoadPlayerCostume");
void CharacterCreation_LoadPlayerCostume(void)
{
	CostumeCreator_CopyCostumeFromPet(kPCCostumeStorageType_Primary, 0, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_LoadPetCostume");
void CharacterCreation_LoadPetCostume( U32 iPetIndex )
{
	// TODO: This requires adding fake pets to the fake entity.
	CostumeCreator_CopyCostumeFromPet(kPCCostumeStorageType_Pet, iPetIndex, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetAppearancePresetListFromPlayer");
void CharacterCreation_GenGetAppearancePresetListFromPlayer( SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEnt, const char *pcGroup, const char *pcSlotType )
{
	SpeciesDef* pSpecies = NULL;
	S32 i;
	static CostumePreset** s_eaPresets = NULL;

	if(pEnt && pEnt->pChar) {
		pSpecies = GET_REF(pEnt->pChar->hSpecies);
	} else {
		return;
	}

	eaClear(&s_eaPresets);

	if (pSpecies)
	{
		S32 iSize = eaSize(&pSpecies->eaPresets);

		if (!s_eaPresets)
		{
			eaCreate(&s_eaPresets);
			eaIndexedEnable(&s_eaPresets, parse_CostumePreset);
		}

		for (i = 0; i < iSize; i++)
		{
			CostumePreset *pPreset = pSpecies->eaPresets[i];
			CostumePresetCategory *pPresetCat = GET_REF(pPreset->hPresetCategory), tempPresetCat;

			tempPresetCat.bExcludeGroup = pPreset->bOverrideExcludeValues || !pPresetCat ? pPreset->bExcludeGroup : pPresetCat->bExcludeGroup;
			tempPresetCat.bExcludeSlotType = pPreset->bOverrideExcludeValues || !pPresetCat ? pPreset->bExcludeSlotType : pPresetCat->bExcludeSlotType;
			tempPresetCat.pcGroup = (pPreset->pcGroup && *pPreset->pcGroup) || !pPresetCat ? pPreset->pcGroup : pPresetCat->pcGroup;
			tempPresetCat.pcSlotType = (pPreset->pcSlotType && *pPreset->pcSlotType) || !pPresetCat ? pPreset->pcSlotType : pPresetCat->pcSlotType;
			pPresetCat = &tempPresetCat;

			if (pcGroup)
			{
				if (pPresetCat->bExcludeGroup && pPresetCat->pcGroup && *pPresetCat->pcGroup && !stricmp(pcGroup,pPresetCat->pcGroup))
				{
					if (pPresetCat->bExcludeSlotType)
					{
						if (pPresetCat->pcSlotType && *pPresetCat->pcSlotType && !stricmp(pcSlotType,pPresetCat->pcSlotType))
						{
							continue;
						}
					}
					else
					{
						continue;
					}
				}
				else if ((!pPresetCat->bExcludeGroup) && pPresetCat->pcGroup && *pPresetCat->pcGroup && stricmp(pcGroup,pPresetCat->pcGroup))
				{
					continue;
				}
			}
			if (pcSlotType)
			{
				if (pPresetCat->bExcludeSlotType && pPresetCat->pcSlotType && *pPresetCat->pcSlotType && !stricmp(pcSlotType,pPresetCat->pcSlotType))
				{
					if (pPresetCat->bExcludeGroup)
					{
						if (pPresetCat->pcGroup && *pPresetCat->pcGroup && !stricmp(pcGroup,pPresetCat->pcGroup))
						{
							continue;
						}
					}
					else
					{
						continue;
					}
				}
				else if ((!pPresetCat->bExcludeSlotType) && pPresetCat->pcSlotType && *pPresetCat->pcSlotType && stricmp(pcSlotType,pPresetCat->pcSlotType))
				{
					continue;
				}
			}
			eaPush(&s_eaPresets, pPreset);
		}
	}

	ui_GenSetManagedListSafe(pGen, &s_eaPresets, CostumePreset, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetAppearancePresetListSizeFromPlayer");
S32 CharacterCreation_GenGetAppearancePresetListSizeFromPlayer( SA_PARAM_NN_VALID Entity* pEnt )
{
	SpeciesDef* pSpecies = NULL;

	if(pEnt && pEnt->pChar) {
		pSpecies = GET_REF(pEnt->pChar->hSpecies);
	}

	if (pSpecies) {
		return eaSize(&pSpecies->eaPresets);
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetAppearancePresetFromPlayer");
const char* CharacterCreation_GenGetAppearancePresetFromPlayer( SA_PARAM_NN_VALID Entity* pEnt, S32 iIndex )
{
	SpeciesDef* pSpecies = NULL;

	if(pEnt && pEnt->pChar) {
		pSpecies = GET_REF(pEnt->pChar->hSpecies);
	}

	if (pSpecies && 0 <= iIndex && iIndex < eaSize(&pSpecies->eaPresets)) {
		if (IS_HANDLE_ACTIVE(pSpecies->eaPresets[iIndex]->hCostume)) {
			return REF_STRING_FROM_HANDLE(pSpecies->eaPresets[iIndex]->hCostume);
		}
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetDefaultCostumeDefFromPtr");
const char* CharacterCreation_GenExprGetDefaultCostumeFromSpecies(SA_PARAM_NN_VALID UIGen* pGen,
																  SA_PARAM_NN_VALID SpeciesDef* pSpecies,
																  bool bIsPet)
{
	PlayerCostume* pCostume = CharacterCreation_GetDefaultCostumeForSpecies(pSpecies, bIsPet);
	if (pCostume)
	{
		return pCostume->pcName;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetDefaultCostumeDef");
const char* CharacterCreation_GenGetDefaultCostumeDef(SA_PARAM_NN_VALID UIGen* pGen, bool bIsPet)
{
	SpeciesDef* pSpecies = ui_GenGetPointer(pGen, parse_SpeciesDef, NULL);

	if (pSpecies)
	{
		PlayerCostume* pCostume = CharacterCreation_GetDefaultCostumeForSpecies(pSpecies, bIsPet);
		if (pCostume)
		{
			return pCostume->pcName;
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenHasMultipleGenderOptions");
bool CharacterCreation_GenHasMultipleGenderOptions( SA_PARAM_NN_VALID UIGen* pGen )
{
	S32 i, iCount = 0;
	DictionaryEArrayStruct* pArray = resDictGetEArrayStruct("SpeciesDef");
	SpeciesDef* pSpecies = ui_GenGetPointer( pGen, parse_SpeciesDef, NULL );

	if (pSpecies)
	{
		for (i = 0; i < eaSize(&pArray->ppReferents); i++)
		{
			if ( stricmp( ((SpeciesDef*)pArray->ppReferents[i])->pcSpeciesName, pSpecies->pcSpeciesName ) == 0 )
			{
				iCount++;
			}

			if ( iCount > 1 )
				return true;
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetNextGenderInList");
bool CharacterCreation_GenGetNextGenderInList( SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID UIGen *pGenSave )
{
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct("SpeciesDef");
	S32 i;
	SpeciesDef* pSpecies = ui_GenGetPointer( pGen, parse_SpeciesDef, NULL );
	SpeciesDef* pFound = NULL;

	if (pSpecies)
	{
		S32 iSize = eaSize(&pArray->ppReferents);
		S32 iFirst = 0;

		for ( i = 0; i < iSize; i++ )
		{
			SpeciesDef* pCurDefi = pArray->ppReferents[i];

			if ( pSpecies == pCurDefi )
			{
				S32 j;
				for ( j = i + 1; j != i; j++ )
				{
					SpeciesDef* pCurDefj = pArray->ppReferents[j=j>=iSize?0:j];

					if ( stricmp( pSpecies->pcSpeciesName, pCurDefj->pcSpeciesName ) == 0 )
					{
						if ( pSpecies != pCurDefj )
						{
							pFound = pCurDefj;
							break;
						}
					}
				}
				break;
			}
		}
	}

	if ( pFound )
	{
		ui_GenSetPointer( pGenSave, pFound, parse_SpeciesDef );
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPrevGenderInList");
bool CharacterCreation_GenGetPrevGenderInList( SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID UIGen *pGenSave )
{
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct("SpeciesDef");
	S32 i;
	SpeciesDef* pSpecies = ui_GenGetPointer( pGen, parse_SpeciesDef, NULL );
	SpeciesDef* pFound = NULL;

	if (pSpecies)
	{
		S32 iSize = eaSize(&pArray->ppReferents);
		S32 iFirst = 0;

		for ( i = iSize - 1; i >= 0; i-- )
		{
			SpeciesDef* pCurDefi = pArray->ppReferents[i];

			if ( pSpecies == pCurDefi )
			{
				S32 j;
				for ( j = i - 1; j != i; j-- )
				{
					SpeciesDef* pCurDefj = pArray->ppReferents[j=j<0?iSize-1:j];

					if ( stricmp( pSpecies->pcSpeciesName, pCurDefj->pcSpeciesName ) == 0 )
					{
						if ( pSpecies != pCurDefj )
						{
							pFound = pCurDefj;
							break;
						}
					}
				}

				break;
			}
		}
	}

	if ( pFound )
	{
		ui_GenSetPointer( pGenSave, pFound, parse_SpeciesDef );
		return true;
	}

	return false;
}

// Deprecated: DO NOT USE, NO REPLACEMENT
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetGenderList");
void CharacterCreation_GenGetGenderList( SA_PARAM_NN_VALID UIGen* pGen )
{
	SpeciesDef* pSpecies = ui_GenGetPointer( pGen->pParent, parse_SpeciesDef, NULL );
	static SpeciesDef** s_eaGenders = NULL;

	CharacterCreation_FillGenderListEx(&s_eaGenders, pSpecies, kFillSpeciesListFlag_MicroTransacted);
	ui_GenSetListSafe(pGen, &s_eaGenders, SpeciesDef);
	eaClearFast(&s_eaGenders);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetGenderListFromPlayer");
void CharacterCreation_GenGetGenderListFromPlayer( SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEnt)
{
	SpeciesDef* pSpecies = NULL;
	static SpeciesDef** s_eaGenders = NULL;

	if(pEnt && pEnt->pChar) {
		pSpecies = GET_REF(pEnt->pChar->hSpecies);
	}

	CharacterCreation_FillGenderListEx(&s_eaGenders, pSpecies, kFillSpeciesListFlag_MicroTransacted);
	ui_GenSetListSafe(pGen, &s_eaGenders, SpeciesDef);
	eaClearFast(&s_eaGenders);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSpeciesGetGenderList");
void CharacterCreation_GenSpeciesGetGenderList(SA_PARAM_NN_VALID UIGen* pGen, const char *pchSpecies)
{
	SpeciesDef* pSpecies = RefSystem_ReferentFromString(g_hSpeciesDict, pchSpecies);
	static SpeciesDef** s_eaGenders = NULL;

	CharacterCreation_FillGenderListEx(&s_eaGenders, pSpecies, kFillSpeciesListFlag_MicroTransacted);
	ui_GenSetListSafe(pGen, &s_eaGenders, SpeciesDef);
	eaClearFast(&s_eaGenders);
}

// Deprecated: DO NOT USE, NO REPLACEMENT
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetDefaultSpeciesData");
void CharacterCreation_GenSetDefaultSpeciesData( SA_PARAM_NN_VALID UIGen* pGen, bool bIsPet )
{
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct("SpeciesDef");
    SpeciesDef *speciesDef = NULL;

    if ( g_pFakePlayer )
    {
        speciesDef = GET_REF(g_pFakePlayer->pChar->hSpecies);
    }

	if (	(bIsPet && g_CharacterCreationData->petInfo[0]->pSpecies==NULL)
		||	(!bIsPet && speciesDef==NULL) )
	{
		if ( eaSize(&pArray->ppReferents ) )
			ui_GenSetPointer( pGen, pArray->ppReferents[0], parse_SpeciesDef );
	}
	else
	{
		if ( bIsPet )
		{
			if ( eaSize(&g_CharacterCreationData->petInfo) )
				ui_GenSetPointer( pGen, g_CharacterCreationData->petInfo[0]->pSpecies, parse_SpeciesDef );
		}
		else
		{
			ui_GenSetPointer( pGen, speciesDef, parse_SpeciesDef );
		}
	}
}

// Deprecated: DO NOT USE, NO REPLACEMENT
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetDefaultGenderData");
void CharacterCreation_GenSetDefaultGenderData(	SA_PARAM_NN_VALID UIGen* pGen,
												SA_PARAM_NN_VALID UIGen* pParentGen )
{
	S32 i;
	DictionaryEArrayStruct* pArray = resDictGetEArrayStruct("SpeciesDef");

	SpeciesDef* pSpeciesParent = ui_GenGetPointer( pParentGen, parse_SpeciesDef, NULL );
	SpeciesDef* pSpeciesChild = ui_GenGetPointer( pGen, parse_SpeciesDef, NULL );

	if (pSpeciesParent)
	{
		if ( pSpeciesChild )
		{
			for (i = 0; i < eaSize(&pArray->ppReferents); i++)
			{
				if ( stricmp( ((SpeciesDef*)pArray->ppReferents[i])->pcSpeciesName, pSpeciesParent->pcSpeciesName ) == 0 )
				{
					if ( ((SpeciesDef*)pArray->ppReferents[i])->eGender == pSpeciesChild->eGender )
					{
						ui_GenSetPointer( pGen, pArray->ppReferents[i], parse_SpeciesDef );
						return;
					}
				}
			}
		}

		ui_GenSetPointer( pGen, pSpeciesParent, parse_SpeciesDef );
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetDefaultClassData");
void CharacterCreation_GenSetDefaultClassData( SA_PARAM_NN_VALID UIGen* pGen, char *pchClassName)
{
	if (g_CharacterCreationData->powerTreeName==NULL)
	{
		PowerTreeDef *pTreeDef = powertreedef_Find(pchClassName);
		if (pTreeDef)
			ui_GenSetPointer( pGen, pTreeDef, parse_PowerTreeDef );
	}
	else
	{
		ui_GenSetPointer( pGen, RefSystem_ReferentFromString("PowerTreeDef", g_CharacterCreationData->powerTreeName), parse_PowerTreeDef);
	}

}

// Deprecated: DO NOT USE, NO REPLACEMENT
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetDefaultAppearanceData");
void CharacterCreation_GenSetDefaultAppearanceData(	SA_PARAM_NN_VALID UIGen* pGen,
													SA_PARAM_NN_VALID UIGen* pParentGen )
{
	SpeciesDef* pSpecies = ui_GenGetPointer( pParentGen, parse_SpeciesDef, NULL );

	if (pSpecies)
	{
		ui_GenSetPointer( pGen, pSpecies, parse_SpeciesDef );
	}
}

// Deprecated: DO NOT USE, NO REPLACEMENT
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetDefaultGenderDisplayName");
const char* CharacterCreation_GenGetDefaultGenderDisplayName( SA_PARAM_NN_VALID UIGen* pGen )
{
	SpeciesDef* pSpecies = ui_GenGetPointer( pGen, parse_SpeciesDef, NULL );

	if (pSpecies)
	{
		return TranslateDisplayMessage( pSpecies->genderNameMsg );
	}

	return "";
}

// Deprecated: DO NOT USE, NO REPLACEMENT
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetDefaultClassDisplayName");
const char* CharacterCreation_GenGetDefaultClassDisplayName( SA_PARAM_NN_VALID UIGen* pGen )
{
	PowerTreeDef* pPowerTree = ui_GenGetPointer( pGen, parse_PowerTreeDef, NULL );

	if (pPowerTree)
	{
		return (pPowerTree->pchName);
	}

	return "";
}

// Deprecated: DO NOT USE, NO REPLACEMENT
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetDefaultSkeletonDef");
const char* CharacterCreation_GenGetDefaultSkeletonDef( SA_PARAM_NN_VALID UIGen* pGen )
{
	SpeciesDef* pSpecies = ui_GenGetPointer( pGen, parse_SpeciesDef, NULL );

	if (pSpecies)
	{
		return REF_STRING_FROM_HANDLE(pSpecies->hSkeleton);
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetSpeciesByName");
bool CharacterCreation_GenGetSpeciesByName(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_STR const char *pchSpeciesName)
{
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct("SpeciesDef");
	S32 i;

	if (pGen && pchSpeciesName && pchSpeciesName[0])
	{
		for (i = 0; i < eaSize(&pArray->ppReferents); i++)
		{
			SpeciesDef *pSpecies = (SpeciesDef*)pArray->ppReferents[i];

			if (pSpecies && stricmp(pSpecies->pcSpeciesName, pchSpeciesName) == 0)
			{
				ui_GenSetPointer(pGen, pSpecies, parse_SpeciesDef);
				return true;
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetSpeciesByNameAndGender");
bool CharacterCreation_GenGetSpeciesByNameAndGender(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_STR const char *pchSpeciesName, int iGender)
{
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct("SpeciesDef");
	S32 i;

	if (pGen && pchSpeciesName && pchSpeciesName[0])
	{
		for (i = 0; i < eaSize(&pArray->ppReferents); i++)
		{
			SpeciesDef *pSpecies = (SpeciesDef*)pArray->ppReferents[i];

			if (pSpecies && stricmp(pSpecies->pcSpeciesName, pchSpeciesName) == 0 && pSpecies->eGender == iGender )
			{
				ui_GenSetPointer(pGen, pSpecies, parse_SpeciesDef);
				return true;
			}
		}
	}

	return false;
}


// Deprecated: DO NOT USE, NO REPLACEMENT
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetNextSpeciesInList");
bool CharacterCreation_GenGetNextSpeciesInList( SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID UIGen *pGenSave )
{
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct("SpeciesDef");
	S32 i;
	SpeciesDef* pSpecies = ui_GenGetPointer( pGen, parse_SpeciesDef, NULL );
	SpeciesDef* pFound = NULL;

	if (pSpecies)
	{
		S32 iSize = eaSize(&pArray->ppReferents);

		for (i = 0; i < iSize; i++)
		{
			if ( pSpecies == pArray->ppReferents[i] )
			{
				S32 j;

				for ( j = i + 1; j != i; j++ )
				{
					SpeciesDef* pCurDef = pArray->ppReferents[j=(j>=iSize)?0:j];

					if ( stricmp( pSpecies->pcSpeciesName, pCurDef->pcSpeciesName ) != 0 )
					{
						if ( pSpecies->eGender == pCurDef->eGender )
						{
							pFound = pCurDef;
							break;
						}
					}
				}

				break;
			}
		}
	}

	if ( pFound )
	{
		ui_GenSetPointer( pGenSave, pFound, parse_SpeciesDef );
		return true;
	}

	return false;
}

// Deprecated: DO NOT USE, NO REPLACEMENT
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPrevSpeciesInList");
bool CharacterCreation_GenGetPrevSpeciesInList( SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID UIGen *pGenSave )
{
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct("SpeciesDef");
	S32 i;
	SpeciesDef* pSpecies = ui_GenGetPointer( pGen, parse_SpeciesDef, NULL );
	SpeciesDef* pFound = NULL;

	if (pSpecies)
	{
		S32 iSize = eaSize(&pArray->ppReferents);

		for (i = 0; i < iSize; i++)
		{
			if ( pSpecies == pArray->ppReferents[i] )
			{
				S32 j;

				for ( j = i - 1; j != i; j-- )
				{
					SpeciesDef* pCurDef = pArray->ppReferents[j=(j<0)?iSize-1:j];

					if ( stricmp( pSpecies->pcSpeciesName, pCurDef->pcSpeciesName ) != 0 )
					{
						if ( pSpecies->eGender == pCurDef->eGender )
						{
							pFound = pCurDef;
							break;
						}
					}
				}

				break;
			}
		}
	}

	if ( pFound )
	{
		ui_GenSetPointer( pGenSave, pFound, parse_SpeciesDef );
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetSpeciesListGender");
void CharacterCreation_GenGetSpeciesListGender( SA_PARAM_NN_VALID UIGen *pGen, int iGender )
{
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct("SpeciesDef");
	S32 i, j;
	static SpeciesDef **s_eaSpecies = NULL;

	if (iGender != Gender_Female && iGender != Gender_Male && iGender != Gender_Neuter)
	{
		return;
	}

	eaClear(&s_eaSpecies);

	//if (pEntity)
	{
		if (!s_eaSpecies)
		{
			eaCreate(&s_eaSpecies);
			eaIndexedEnable(&s_eaSpecies, parse_SpeciesDef);
		}

		for (i = 0; i < eaSize(&pArray->ppReferents); i++)
		{
			bool bFound = false;
			SpeciesDef *pSpecies = (SpeciesDef*)pArray->ppReferents[i];

			for ( j = 0; j < eaSize(&s_eaSpecies); j++ )
			{
				if ( stricmp( pSpecies->pcSpeciesName, s_eaSpecies[j]->pcSpeciesName ) == 0 && pSpecies->eGender == iGender)
				{
					bFound = true;
					break;
				}
			}
			if (bFound == false)
			{
				if ((CharClassTypes)pSpecies->eType != StaticDefineIntGetInt(CharClassTypesEnum, "Space") && stricmp(pSpecies->pcName,"None"))
				{
					if (CharacterCreation_IsSpeciesInAllegiance(pSpecies, g_pcAllegianceName) && pSpecies->eGender == iGender)// && CanPlayerUseSpecies(pSpecies))
					{
						eaPush(&s_eaSpecies, pArray->ppReferents[i]);
					}
				}
			}
			else
			{
				if (pSpecies->eGender != iGender || !CharacterCreation_IsSpeciesInAllegiance(pSpecies, g_pcAllegianceName))
				{
					eaRemove(&s_eaSpecies, i);
				}
			}
		}
	}

	eaIndexedDisable(&s_eaSpecies);
	costumeTailor_SortSpecies(s_eaSpecies, true);
	ui_GenSetManagedListSafe(pGen, &s_eaSpecies, SpeciesDef, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetDefaultCharacterPath");
void CharacterCreation_GenSetDefaultCharacterPath(SA_PARAM_NN_VALID UIGen* pGen)
{
	CharacterPath *pFirstAvailableCharacterPath = NULL;
	static CharacterPath **s_eaCharacterPaths = NULL;

	if (g_CharacterCreationData->characterPathName && g_CharacterCreationData->characterPathName[0])
	{
		pFirstAvailableCharacterPath = GET_REF(g_pFakePlayer->pChar->hPath);
	}
	else
	{
		// Clear the list
		eaClear(&s_eaCharacterPaths);

		// Get the list of character paths
		CharacterPath_GetCharacterPaths(&s_eaCharacterPaths, g_CharacterCreationData->powerTreeName, true, false);

		if (eaSize(&s_eaCharacterPaths) > 0)
		{
			pFirstAvailableCharacterPath = s_eaCharacterPaths[0];
		}
	}

	// Set the default character path
	ui_GenSetPointer(pGen, pFirstAvailableCharacterPath, parse_CharacterPath);
}

//Saves the character's PRIMARY character path.
AUTO_EXPR_FUNC(UIGen);
void CharacterCreation_SaveCharacterPath(SA_PARAM_OP_STR const char *pchCharacterPathName)
{
	CharacterPath *pPath = RefSystem_ReferentFromString(g_hCharacterPathDict, pchCharacterPathName);
	if (pchCharacterPathName && pchCharacterPathName[0]
		&& pPath
		&& g_pFakePlayer
		&& g_pFakePlayer->pChar)
	{
		g_CharacterCreationData->characterPathName = allocAddString(pchCharacterPathName);
		if (IS_HANDLE_ACTIVE(pPath->hPowerTree))
			g_CharacterCreationData->powerTreeName = allocAddString(REF_STRING_FROM_HANDLE(pPath->hPowerTree));

		SET_HANDLE_FROM_STRING(g_hCharacterPathDict, pchCharacterPathName, g_pFakePlayer->pChar->hPath);
		if(eaSize(&pPath->eaRequiredClasses))
		{
			COPY_HANDLE( g_pFakePlayer->pChar->hClass, pPath->eaRequiredClasses[0]->hClass);
			g_CharacterCreationData->className = allocAddString(REF_STRING_FROM_HANDLE(pPath->eaRequiredClasses[0]->hClass));
		}
	}
}

MicroPurchaseErrorType CharacterCreation_CharacterPathGetMicroTransactionDef(CharacterPath * pPath, MicroTransactionDef **ppOutDef, char **pestrError) 
{
	MicroTransactionDef *pBestDef = NULL;
	MicroPurchaseErrorType resultBest = kMicroPurchaseErrorType_None;
	S64 iBestPrice = LLONG_MAX;
	MicroTransactionProduct **eaProducts = NULL;

	if (pPath)
	{
		gclMicroTrans_FindProductsForPermissionExpr(&eaProducts, pPath->pExprRequires);

		FOR_EACH_IN_EARRAY_FORWARDS(eaProducts, MicroTransactionProduct, pProduct)
		{
			MicroTransactionDef *pDef = GET_REF(pProduct->hDef);
			if(pDef)
			{
				bool bMeetsRequirements = gclMicroTrans_PrerequisitesMet(pProduct);
				MicroPurchaseErrorType result =  microtrans_GetCanPurchaseError(0, NULL, entity_GetGameAccount(NULL), pDef, locGetLanguage(getCurrentLocale()), pestrError);
				S64 iPrice = microtrans_GetPrice(pProduct->pProduct);
				if(bMeetsRequirements && result == kMicroPurchaseErrorType_None && iPrice >= 0 && iPrice < iBestPrice)
				{
					pBestDef = pDef;
					resultBest = result;
					iBestPrice = iPrice;
				}
			}
		} FOR_EACH_END;

		eaDestroy(&eaProducts);
	}

	if (ppOutDef)
		*ppOutDef = pBestDef;

	return resultBest;
}

void CharacterCreation_FillCharacterPathNode(Entity *pEnt, CharacterPath *pPath, CharacterPathNode *pOutNode) 
{
	MicroTransactionDef *pBestDef = NULL;
	MicroPurchaseErrorType resultBest = CharacterCreation_CharacterPathGetMicroTransactionDef(pPath, &pBestDef, &pOutNode->estrError);
	
	pOutNode->pPath = pPath;
	pOutNode->bCanPurchase = (pBestDef != NULL && resultBest == kMicroPurchaseErrorType_None);
	pOutNode->bCanUse = pEnt ? Entity_EvalCharacterPathRequiresExpr(pEnt, pPath) : false;
	pOutNode->pchProductName = pBestDef ? pBestDef->pchName : "";

}

AUTO_EXPR_FUNC(UIGen);
const char* CharacterCreation_GetCharacterPathProductName(SA_PARAM_OP_VALID CharacterPath *pPath)
{
	MicroTransactionDef *pBestDef = NULL;
	CharacterCreation_CharacterPathGetMicroTransactionDef(pPath, &pBestDef, NULL);
	return SAFE_MEMBER(pBestDef, pchName);
}

AUTO_EXPR_FUNC(UIGen);
const char* CharacterCreation_GetCharacterPathProductNameByName(const char* pPathName)
{
	CharacterPath *pPath = RefSystem_ReferentFromString(g_hCharacterPathDict, pPathName);
	return CharacterCreation_GetCharacterPathProductName(pPath);
}

//Clears the character's PRIMARY character path.
AUTO_EXPR_FUNC(UIGen);
void CharacterCreation_ClearCharacterPath()
{
	if (g_pFakePlayer
		&& g_pFakePlayer->pChar)
	{
		g_CharacterCreationData->characterPathName = NULL;
		REMOVE_HANDLE(g_pFakePlayer->pChar->hPath);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_GetCharacterPathName");
const char* CharacterCreation_GetCharacterPathName()
{
	if (g_CharacterCreationData->characterPathName)
	{
		CharacterPath* pPath = RefSystem_ReferentFromString(g_hCharacterPathDict, g_CharacterCreationData->characterPathName);
		if (pPath)
		{
			return TranslateDisplayMessage(pPath->pDisplayName);
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetCharacterPathList");
void CharacterCreation_GenGetCharacterPathList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity )
{
	static CharacterPath **s_eaCharacterPaths = NULL;

	// Clear the list
	eaClear(&s_eaCharacterPaths);

	// Get the list of character paths
	Entity_GetCharacterPaths(pEntity, &s_eaCharacterPaths, g_CharacterCreationData->powerTreeName);

	// Set the gen list
	ui_GenSetManagedListSafe(pGen, &s_eaCharacterPaths, CharacterPath, false);
}

static CharacterPath **s_eaFreeCharacterPaths;

// set the list of free (not owned) character paths
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetFreeCharacterPathList");
void CharacterCreation_GetFreeCharacterPathList(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetManagedListSafe(pGen, &s_eaFreeCharacterPaths, CharacterPath, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("IsCharacterPathFreeNotOwned");
bool CharacterCreation_IsCharacterPathFreeNotOwned(SA_PARAM_OP_VALID CharacterPath *pCharacterPath)
{
	GameAccountData *pData = entity_GetGameAccount(NULL);
	return pData && pCharacterPath && CharacterPath_FreeNotOwned(pData, pCharacterPath);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("IsCharacterPathFreeNotOwnedByName");
bool CharacterCreation_IsCharacterPathFreeNotOwnedByName(const char *pcCharacterPathName)
{
	CharacterPath *pCharacterPath = RefSystem_ReferentFromString(g_hCharacterPathDict, pcCharacterPathName);
	return CharacterCreation_IsCharacterPathFreeNotOwned(pCharacterPath);
}

#define CHARACTER_CREATION_FREE_UPDATE_SECONDS 5

void CharacterCreation_ManageFreeCharacterPathList(void)
{
	static U32 uLastCheckTime = 0;
	U32 uCurTime = timeSecondsSince2000();

	if(uCurTime >= uLastCheckTime)
	{
		GameAccountData *pData = entity_GetGameAccount(NULL);
		uLastCheckTime = uCurTime + CHARACTER_CREATION_FREE_UPDATE_SECONDS;
		if(pData)
		{
			S32 i;
			CharacterPath **eaCharacterPaths = NULL;

			// Get the list of character paths, rebuild list entirely as there could be changes to the character paths due to editing
			CharacterPath_GetCharacterPaths(&eaCharacterPaths, NULL, true, false);

			// go through list of character paths, add ones that are free to list
			eaClear(&s_eaFreeCharacterPaths);
			for(i = 0; i < eaSize(&eaCharacterPaths); ++i)
			{
				if(CharacterPath_FreeNotOwned(pData, eaCharacterPaths[i]))
				{
					eaPush(&s_eaFreeCharacterPaths, eaCharacterPaths[i]);
				}
			}
			eaDestroy(&eaCharacterPaths);
		}
	}
}

static SpeciesDef **s_eaSpecies = NULL;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetSpeciesList");
void CharacterCreation_GenGetSpeciesList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity)
{
	CharacterCreation_FillSpeciesListEx(&s_eaSpecies, g_pcAllegianceName, kFillSpeciesListFlag_NoDuplicates | kFillSpeciesListFlag_MicroTransacted);
	ui_GenSetListSafe(pGen, &s_eaSpecies, SpeciesDef);
	eaClearFast(&s_eaSpecies);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetSpeciesListWithDummy");
void CharacterCreation_GenGetSpeciesListWithDummy(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity)
{
	CharacterCreation_FillSpeciesListEx(&s_eaSpecies, g_pcAllegianceName, kFillSpeciesListFlag_NoDuplicates | kFillSpeciesListFlag_MicroTransacted);

	// Append a NULL SpeciesDef, so we can show the "Coming Soon" species. The Gen has to wrap all GenIstanceData uses with a NULL-check.
	eaPush(&s_eaSpecies, NULL); 

	ui_GenSetListSafe(pGen, &s_eaSpecies, SpeciesDef);
	eaClearFast(&s_eaSpecies);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSpeciesGetList");
void CharacterCreation_GenSpeciesGetList(SA_PARAM_NN_VALID UIGen *pGen, U32 uFlags)
{
	CharacterCreation_FillSpeciesListEx(&s_eaSpecies, g_pcAllegianceName, uFlags);
	ui_GenSetListSafe(pGen, &s_eaSpecies, SpeciesDef);
	eaClearFast(&s_eaSpecies);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSpeciesGetListWithDummy");
void CharacterCreation_GenSpeciesGetListWithDummy(SA_PARAM_NN_VALID UIGen *pGen, U32 uFlags)
{
	CharacterCreation_FillSpeciesListEx(&s_eaSpecies, g_pcAllegianceName, uFlags);

	// Append a NULL SpeciesDef, so we can show the "Coming Soon" species. The Gen has to wrap all GenIstanceData uses with a NULL-check.
	eaPush(&s_eaSpecies, NULL); 

	ui_GenSetListSafe(pGen, &s_eaSpecies, SpeciesDef);
	eaClearFast(&s_eaSpecies);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSpeciesGroupGetListWithDummy");
void CharacterCreation_GenSpeciesGroupGetListWithDummy(SA_PARAM_NN_VALID UIGen *pGen, U32 uFlags)
{
	SpeciesDef **eaSpecies = NULL;
	SpeciesGroupUI ** eaSpeciesGroups = NULL;
	int iGroup, iSpecies;
	SpeciesGroupUI *pSpeciesGroup;
	// Get the list of species
	CharacterCreation_FillSpeciesListEx(&eaSpecies, g_pcAllegianceName, uFlags);

	// Group them by species group
	eaClear(&eaSpeciesGroups);
	// For each Species
	for( iSpecies = 0; iSpecies < eaSize(&eaSpecies); ++iSpecies )
	{
		SpeciesDef *pSpeciesDef = eaSpecies[iSpecies];
		pSpeciesGroup = NULL;

		// Find it's SpeciesGroup in the GroupList, or add a new group.
		for( iGroup = 0; iGroup < eaSize(&eaSpeciesGroups); ++iGroup )
		{
			if( eaSpeciesGroups[iGroup]->pcSpeciesGroup == pSpeciesDef->pcSpeciesGroup )
			{
				pSpeciesGroup = eaSpeciesGroups[iGroup];
				break;
			}
		}
		// It wasn't found, so make a new group
		if( pSpeciesGroup == NULL )
		{
			pSpeciesGroup = StructCreate(parse_SpeciesGroupUI);
			pSpeciesGroup->pcSpeciesGroup = pSpeciesDef->pcSpeciesGroup;
			iGroup = eaPush(&eaSpeciesGroups, pSpeciesGroup);
			pSpeciesGroup = eaSpeciesGroups[iGroup];
		}

		// Add this Species to the SpeciesGroup's SpeciesList.
		eaPush(&pSpeciesGroup->eaSpeciesList, pSpeciesDef);
	}
	
	// Append a NULL SpeciesGroup, so we can show the "Coming Soon" species. The Gen has to wrap all GenIstanceData uses with a NULL-check.
	pSpeciesGroup = StructCreate(parse_SpeciesGroupUI);
	pSpeciesGroup->pcSpeciesGroup = allocAddString("");
	eaPush(&eaSpeciesGroups, pSpeciesGroup);

	ui_GenSetListSafe(pGen, &eaSpeciesGroups, SpeciesGroupUI);
	eaClearFast(&eaSpecies);
	eaClearFast(&eaSpeciesGroups);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SpeciesGroupUI_GetSpeciesListSize);
U32 exprSpeciesGroupUI_GetSpeciesListSize(SA_PARAM_NN_VALID SpeciesGroupUI *pSpeciesGroupUI)
{
	return pSpeciesGroupUI ? eaSize(&pSpeciesGroupUI->eaSpeciesList) : 0;
}

Entity* CharacterCreation_InitFakePlayerWithPowers(Entity *pOwner, Entity *pEnt)
{
	if ( pOwner==NULL || pOwner->pChar==NULL || pEnt==NULL || pEnt->pChar==NULL )
		return NULL;

	StructDestroyNoConstSafe( parse_Entity, &g_pFakePlayer );

	g_bCartRespecRequest = false;

	g_pFakePlayer = CONTAINER_NOCONST(Entity, entity_CreateOwnerCopy( pOwner, pEnt, false, true, true, true, true ));

	return (Entity*)g_pFakePlayer;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenInitFakePlayerWithPowers");
bool gclGenExprInitFakePlayerWithPowers(SA_PARAM_OP_VALID Entity *pOwner, SA_PARAM_OP_VALID Entity *pEnt)
{
	return CharacterCreation_InitFakePlayerWithPowers(pOwner, pEnt) != NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenDeInitFakePlayer");
void gclGenExprDeInitFakePlayer(void)
{
	StructDestroyNoConstSafe(parse_Entity, &g_pFakePlayer);

	g_pFakePlayer = NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenUpdateFakePlayerNumeric");
bool gclGenExprUpdateFakePlayerNumeric(SA_PARAM_OP_VALID Entity *pEnt, char* pchNumeric)
{
	bool bResult;
	S32 iEntNumericValue = inv_trh_GetNumericValue(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), pchNumeric);
	bResult = inv_ent_trh_SetNumeric(ATR_EMPTY_ARGS, g_pFakePlayer, true, pchNumeric, iEntNumericValue, NULL);
	return bResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetFakePlayer");
SA_RET_OP_VALID Entity* gclGenExprGetFakePlayer(void)
{
	return (Entity*)g_pFakePlayer;
}

//Removes any lingering power trees of the specified type from the player's PT list, then adds valid ones to the player.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenAddPowerTreesByType");
void CharacterCreation_gclGenExprAddPowerTreesByType(SA_PARAM_NN_VALID Entity* pEnt, ACMD_EXPR_DICT(PowerTreeTypeDef) const char *pchPowerTreeType)
{
	PTTypeDef *pType = RefSystem_ReferentFromString("PowerTreeTypeDef", pchPowerTreeType);
	RefDictIterator iter;
	PowerTreeDef *pTree = NULL;
	int i;

	//Remove current powers of that type from the tree list
	for(i = eaSize(&g_pFakePlayer->pChar->ppPowerTrees)-1; i >= 0; i--) {
		PowerTreeDef *pTreeDef = GET_REF(g_pFakePlayer->pChar->ppPowerTrees[i]->hDef);
		if(pTreeDef && GET_REF(pTreeDef->hTreeType) == pType) {
			CharacterCreation_RemovePowers(pTreeDef->pchName);
		}
	}


	//Add new ones
	RefSystem_InitRefDictIterator("PowerTreeDef", &iter);
	while (pTree = RefSystem_GetNextReferentFromIterator(&iter))
	{
		if(GET_REF(pTree->hTreeType) == pType) {
			CharacterClass *pClass = GET_REF(pTree->hClass);
			if ((!pClass || pClass->bPlayerClass) && (character_CanBuyPowerTree(PARTITION_CLIENT, pEnt->pChar, pTree))) {
				//character_AddPowerTree(g_pFakePlayer->pChar, pTree->pchName);
				//ptui_AutoBuy(g_pFakePlayer);
				CharacterCreation_InitPowers(pTree->pchName);
			}
		}
	}
}

// Buy a power tree node, during character creation.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenCanPurchaseGroup");
bool CharacterCreation_CanPurchaseGroup(SA_PARAM_NN_VALID Entity *pEnt, const char* pchGroupName)
{
	bool bResult = false;
	if (pchGroupName)
	{
		PTGroupDef *pGroup;
		pGroup = powertreegroupdef_Find(pchGroupName);
		if (pGroup && pEnt->pChar)
		{
			bResult = character_CanBuyPowerTreeGroup(PARTITION_CLIENT, pEnt->pChar, pGroup);
		}
	}
	return bResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_IsCharTypeUnlocked");
bool CharacterCreation_IsCharTypeUnlocked(const char *unlockable)
{
	if (unlockable)
	{
		if (StaticDefineIntGetInt(UnlockedAllegianceFlagsEnum, unlockable) & g_characterSelectionData->unlockAllegianceFlags)
		{
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_IsCharTypeUnlockedForVirtualShard");
bool CharacterCreation_IsCharTypeUnlockedForVirtualShard(const char *unlockable, ContainerID iVirtualShard)
{
	if (unlockable)
	{
		int iAllegianceFlag = StaticDefineIntGetInt(UnlockedAllegianceFlagsEnum, unlockable);
		bool bUGC = CharacterSelection_IsVirtualShardUGCShard(iVirtualShard);
		if(bUGC && iAllegianceFlag > -1)
		{
			return true;
		}
        else if (iAllegianceFlag & g_characterSelectionData->unlockAllegianceFlags)
		{
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_IsCharCreateTypeUnlocked");
bool CharacterCreation_IsCharCreateTypeUnlocked(const char *unlockable)
{
	if (unlockable)
	{
		if (StaticDefineIntGetInt(UnlockedCreateFlagsEnum, unlockable) & g_characterSelectionData->unlockCreateFlags)
		{
			return true;
		}
	}
	return false;
}

// expression to allow checking for number of powers in power tree with this category and purpose for use on a new character being created
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_GetNumPowersWithCatAndPurpose");
S32 CharacterCreation_GetNumPowersWithCatAndPurpose(char *pcCategory, char *pcPurpose)
{
	if(g_pFakePlayer)
	{
		return PowersUI_GetNumPowersWithCatAndPurposeInternal((Entity *)g_pFakePlayer, pcCategory, pcPurpose);
	}

	return 0;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenEntOwnsPowerTree");
bool CharacterCreation_EntOwnsPowerTree(SA_PARAM_NN_VALID Entity* pEnt, const char *pchTreeName)
{
	Character *pSrc = pEnt ? pEnt->pChar : NULL;
	int i;

	if(!pSrc)
	return 0;

	for(i=0;i<eaSize(&pSrc->ppPowerTrees);i++)
	{
		PowerTreeDef *pTreeDef = GET_REF(pSrc->ppPowerTrees[i]->hDef);
		if(pTreeDef && pTreeDef->pchName && !strcmp(pTreeDef->pchName,pchTreeName))
		{
			return 1;
		}
	}

	return 0;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenEntOwnsNodesInPowerTree");
bool CharacterCreation_EntOwnsNodesInPowerTree(SA_PARAM_NN_VALID Entity* pEnt, const char *pchTreeName)
{
	Character *pSrc = pEnt ? pEnt->pChar : NULL;
	int i;

	if(!pSrc)
		return 0;

	for(i=0;i<eaSize(&pSrc->ppPowerTrees);i++)
	{
		PowerTreeDef *pTreeDef = GET_REF(pSrc->ppPowerTrees[i]->hDef);
		if(pTreeDef && pTreeDef->pchName && !strcmp(pTreeDef->pchName,pchTreeName))
		{
			if (eaSize(&pSrc->ppPowerTrees[i]->ppNodes))
				return 1;
		}
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenEntOwnsTempPowerWithPrefix");
bool CharacterCreation_EntOwnsTempPowerWithPrefix(SA_PARAM_NN_VALID Entity* pEnt, const char *pchPrefix)
{
	Character *pChar = pEnt ? pEnt->pChar : NULL;

	if (pChar)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pChar->ppPowersTemporary, Power, pPower)
		{
			PowerDef *pPowerDef = pPower ? GET_REF(pPower->hDef) : NULL;
			if (pPowerDef && strStartsWith(pPowerDef->pchName, pchPrefix))
				return true;
		}
		FOR_EACH_END
		return false;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_GetMatchingCharacterPathsFromStringWithDummy);
ExprFuncReturnVal CharacterCreation_GetMatchingCharacterPathsFromStringWithDummy(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char* pchPaths, bool bShowUnpurchased, bool bAddDummyAtEnd)
{
	CharacterPathNode ***peaNodes = ui_GenGetManagedListSafe(pGen, CharacterPathNode);
	int iCount = 0;
	Entity *pEnt = (Entity*)g_pFakePlayer;

	if(GSM_IsStateActiveOrPending(GCL_LOGIN_USER_CHOOSING_EXISTING))
		pEnt = (Entity*)gclLoginGetChosenEntity();

	if(pEnt)
	{
		CharacterPath *pPath;
		RefDictIterator iter;

		RefSystem_InitRefDictIterator("CharacterPath", &iter);
		while (pPath = RefSystem_GetNextReferentFromIterator(&iter))
		{
			CharacterPathNode *pNode;

			// If this path can't be used hide (don't add to list) it if flagged to do so
			if ((pchPaths && !strstri(pchPaths, pPath->pchName))
				|| ((pPath->bHideIfCantUse || !bShowUnpurchased) && !Entity_EvalCharacterPathRequiresExpr(pEnt, pPath)))
			{
				continue;
			}

			//Since this is character creation, only allow Primary paths.
			if (pPath->eType >= 0)
			{
				continue;
			}

			pNode = eaGetStruct(peaNodes, parse_CharacterPathNode, iCount++);

			if (pPath->bPlayerPathDevRestricted && !g_bCharacterPathAllowDevRestricted)
			{	// if the CharacterPath is restricted to devs 
				// only allow it if the ent has access level and CharacterPathAllowDevRestricted is set
				pNode->bCanPurchase = false;
				pNode->bCanUse = false;
				pNode->pchProductName = "";
				estrClear(&pNode->estrError);
			}
			else if (pPath->pExprRequires)
			{
				CharacterCreation_FillCharacterPathNode(pEnt, pPath, pNode);
			}
			else
			{
				pNode->bCanPurchase = false;
				pNode->bCanUse = true;
				pNode->pchProductName = "";
				estrClear(&pNode->estrError);
			}
			pNode->pPath = pPath;
		}
	}

	if( bAddDummyAtEnd )
	{
		CharacterPathNode *pNode = eaGetStruct(peaNodes, parse_CharacterPathNode, iCount++);
		pNode->bCanPurchase = false;
		pNode->bCanUse = false;
		pNode->pchProductName = "";
		estrClear(&pNode->estrError);
		pNode->pPath = NULL;
	}

	eaSetSizeStruct(peaNodes, parse_CharacterPathNode, iCount);

	ui_GenSetListSafe(pGen, peaNodes, CharacterPathNode);
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_GetMatchingCharacterPathsFromString);
ExprFuncReturnVal CharacterCreation_GetMatchingCharacterPathsFromString(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char* pchPaths, bool bShowUnpurchased)
{
	return CharacterCreation_GetMatchingCharacterPathsFromStringWithDummy(pContext, pGen, pchPaths, bShowUnpurchased, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_GetCharacterPathListWithDummy);
ExprFuncReturnVal CharacterCreation_GetCharacterPathListWithDummy(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, bool bAddDummyAtEnd)
{
	return CharacterCreation_GetMatchingCharacterPathsFromStringWithDummy(pContext, pGen, NULL, true, bAddDummyAtEnd);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_GetCharacterPathList);
ExprFuncReturnVal CharacterCreation_GetCharacterPathList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	return CharacterCreation_GetMatchingCharacterPathsFromString(pContext, pGen, NULL, true);
}

// This is so dumb, but I don't have a better solution at the moment
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_IsPowerDefLoaded);
bool CharacterCreation_IsPowerDefLoaded(ExprContext *pContext, const char *pchPowerDef)
{
	PowerDef *pDef = RefSystem_ReferentFromString(g_hPowerDefDict, pchPowerDef);
	return !!pDef;
}

//////////////////////////////////////////////////////////////////////////
// Character Creation: Name/Description Commands

static const char *CharacterCreation_GetNameError(const char *pcMessageKey, int stringError, const char *pcName)
{
	static char *s_estrError = NULL;

	estrClear(&s_estrError);

	if (pcMessageKey && *pcMessageKey)
	{
		FormatGameMessageKey((unsigned char **) &s_estrError, pcMessageKey,
			STRFMT_STRING("Name", NULL_TO_EMPTY(pcName)),
			STRFMT_END);
	}
	else
	{
		StringCreateNameErrorMinMax(&s_estrError, stringError, gConf.iMinNameLength, gConf.iMaxNameLength);
	}

	return s_estrError;
}

static const char *CharacterCreation_CheckNameError(const char *pcName, int iMinLength, int iMaxLength, bool bCharacterName)
{
	int err;
	int i;
	const Login2CharacterChoice *characterChoice, *renameChoice;

	// Use standard bounds
	if (iMinLength < 0)
		iMinLength = gConf.iMinNameLength;
	if (iMaxLength < 0)
		iMaxLength = gConf.iMaxNameLength;

	// Using: StringIsInvalidCharacterName(pcName, LoginAccessLevel()) except customized to allow alternate lengths.
	if ((err = StringIsInvalidNameGeneric(pcName, "-._ '", iMinLength, iMaxLength, false, true, true, true, true, LoginGetAccessLevel())) != STRINGERR_NONE)
		return CharacterCreation_GetNameError(NULL, err, pcName);

	// Additional checks if the name represents a character name.
	if (bCharacterName)
	{
        renameChoice = GetRenamingCharacter();

		// Duplication error
		for (i = eaSize(&g_characterSelectionData->characterChoices->characterChoices)-1; i >= 0; --i)
		{
			characterChoice = g_characterSelectionData->characterChoices->characterChoices[i];
			if (!stricmp(pcName, characterChoice->savedName))
			{
				if (characterChoice != renameChoice)
					return CharacterCreation_GetNameError("NameFormat_DuplicateName", STRINGERR_NONE, pcName);
				else
					return CharacterCreation_GetNameError(NULL, STRINGERR_RESTRICTED, pcName);
			}
		}
	}

	return NULL;
}

bool CharacterCreation_NotifyNameError(NotifyType eType, const char *pcName, int iMinLength, int iMaxLength, bool bCharacterName)
{
	const char *pcError = CharacterCreation_CheckNameError(pcName, iMinLength, iMaxLength, bCharacterName);

	if (pcError)
	{
		notify_NotifySend(NULL, eType, pcError, NULL, NULL);
		return true;
	}

	return false;
}

static const char *CharacterCreation_CheckDescriptionError(const char *pcDescription)
{
	static char *s_estrError = NULL;
	int err;

	estrClear(&s_estrError);

	// String errors
	if ((err = StringIsInvalidDescription(pcDescription)) != STRINGERR_NONE) {
		StringCreateDescriptionError(&s_estrError, err);
		return s_estrError;
	}

	return NULL;
}

bool CharacterCreation_NotifyDescriptionError(NotifyType eType, const char *pcDescription)
{
	const char *pcError = CharacterCreation_CheckDescriptionError(pcDescription);

	if (pcError)
	{
		notify_NotifySend(NULL, eType, pcError, NULL, NULL);
		return true;
	}

	return false;
}

// Returns an empty string if there is no error, otherwise the error message.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_CheckValidCharacterName);
const char *CharacterCreationExpr_CheckValidCharacterName(const char *pchName)
{
	const char *pcError = CharacterCreation_CheckNameError(pchName, -1, -1, true);
	return NULL_TO_EMPTY(pcError);
}

//naming this SetNameOnly because currently SetName sets a lot more than it says.
//hopefully this will help distinguish these two functions
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_SetNameOnly);
bool CharacterCreationExpr_SetNameOnly(SA_PARAM_NN_STR const char* pchName)
{
	if (!g_pFakePlayer || !g_pFakePlayer->pSaved)
		return false;

	if (CharacterCreation_NotifyNameError(kNotifyType_NameInvalid, pchName, -1, -1, true))
		return false;

	strcpy(g_pFakePlayer->pSaved->savedName, pchName);

	return true;
}

// Returns an empty string if there is no error, otherwise the error message.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_CheckValidCharacterDescription);
const char *CharacterCreation_CheckValidCharacterDescription(const char *pchDescription)
{
	const char *pcError = CharacterCreation_CheckDescriptionError(pchDescription);
	return NULL_TO_EMPTY(pcError);
}

// Set the character's description.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_SetDescription);
bool CharacterCreationExpr_SetDescription(SA_PARAM_NN_STR const char* pchDescription)
{
	if (!g_pFakePlayer)
		return false;

	if (CharacterCreation_NotifyDescriptionError(kNotifyType_DescriptionInvalid, pchDescription))
		return false;

	StructFreeStringSafe(&g_pFakePlayer->pSaved->savedDescription);
	g_pFakePlayer->pSaved->savedDescription = StructAllocString(pchDescription);

	return true;
}

// Set the character's formal name.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_SetFormalName);
const char *CharacterCreationExpr_SetFormalName( SA_PARAM_OP_STR const char* pchFirstName, SA_PARAM_OP_STR const char* pchMiddleName, SA_PARAM_OP_STR const char* pchLastName, int bLFM, SA_PARAM_OP_STR const char* pchFirstNameGen, SA_PARAM_OP_STR const char* pchMiddleNameGen, SA_PARAM_OP_STR const char* pchLastNameGen )
{
	char achFirst[64], achMiddle[64], achLast[64];
	char achFullName[256];
    char subName[MAX_NAME_LEN];
	if (!g_pFakePlayer)
		return false;

	strcpy(achFirst, NULL_TO_EMPTY(pchFirstName));
	strcpy(achMiddle, NULL_TO_EMPTY(pchMiddleName));
	strcpy(achLast, NULL_TO_EMPTY(pchLastName));

	removeTrailingWhiteSpaces(achFirst);
	removeTrailingWhiteSpaces(achMiddle);
	removeTrailingWhiteSpaces(achLast);

	if (CharacterCreation_NotifyNameError(kNotifyType_FirstNameInvalid, achFirst, 0, 20, false))
		return pchFirstNameGen;
	if (CharacterCreation_NotifyNameError(kNotifyType_MiddleNameInvalid, achMiddle, 0, 20, false))
		return pchMiddleNameGen;
	if (CharacterCreation_NotifyNameError(kNotifyType_LastNameInvalid, achLast, 0, 20, false))
		return pchLastNameGen;

	// Last name should always be defined
	if (!achLast[0] && achMiddle[0])
	{
		strcpy(achLast, achMiddle);
		memset(achMiddle, 0, sizeof(achMiddle));
	}
	else if (!achLast[0] && achFirst[0])
	{
		strcpy(achLast, achFirst);
		memset(achFirst, 0, sizeof(achFirst));
	}
	// First name should be defined before middle
	else if (!achFirst[0] && achMiddle[0])
	{
		strcpy(achFirst, achMiddle);
		memset(achMiddle, 0, sizeof(achMiddle));
	}

	achFullName[0] = '\0';

	// Make sure when the formal name is displayed it won't show naughty text.
	if (bLFM)
	{
		strcat(achFullName, achLast);
		strcat(achFullName, " ");
	}

	strcat(achFullName, achFirst);

	if (achMiddle && achMiddle[0])
	{
		strcat(achFullName, " ");
		strcat(achFullName, achMiddle);
	}

	if (!bLFM)
	{
		strcat(achFullName, " ");
		strcat(achFullName, achLast);
	}

	removeTrailingWhiteSpaces(achFullName);

	if (CharacterCreation_NotifyNameError(kNotifyType_FormalNameInvalid, achFullName, 0, 256, false))
		return "All";

	subName[0] = '\0';

	// Copy the name
	if (achLast[0] || achFirst[0] || achMiddle[0])
	{
		// Set the string
		if (bLFM)
		{
			strcat(subName, "LFM:");
			strcat(subName, achLast);
			strcat(subName, ":");
		}
		else
		{
			strcat(subName, "FML:");
		}

		strcat(subName, achFirst);
		strcat(subName, ":");
		strcat(subName, achMiddle);

		if (!bLFM)
		{
			strcat(subName, ":");
			strcat(subName, achLast);
		}
	}
	else
	{
		// Setting this to NONE doesn't appear to be necessary, internally it's stored as
		// a NULL pointer. Except from character creation, in which case it's stored as NONE.
		// But later renames will save the pointer as NULL. So now I'm making this not set
		// NONE. There also doesn't appear to be any dependencies on the value being NONE in
		// the source code, except during validation.
		//strcpy(subName, "NONE");
	}

	// Set the name on the fake player
	StructFreeStringSafe(&g_pFakePlayer->pSaved->savedSubName);
	if (subName[0])
		g_pFakePlayer->pSaved->savedSubName = StructAllocString(subName);

	return "";
}

//////////////////////////////////////////////////////////////////////////
// Character Creation: Costume Commands

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_CountRequiredCostumesSlots);
int CharacterCreation_CountRequiredCostumesSlots(void)
{
	int i, count = 0;

	if (g_pFakePlayer)
	{
		PCSlotSet *pSlotSet = costumeEntity_GetSlotSet(CONTAINER_RECONST(Entity, g_pFakePlayer), false);
		for (i = 0; i < eaSize(&pSlotSet->eaSlotDefs); i++)
		{
			if (pSlotSet->eaSlotDefs[i]->eCreateCharacter == kPCCharacterCreateSlot_Required)
			{
				count++;
			}
		}
	}

	// There's always at least 1 costume slot required
	return MAX(count, 1);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_CountFilledCostumesSlots);
int CharacterCreation_CountFilledCostumesSlots(void)
{
	int i, count = 0;

	if (g_pFakePlayer)
	{
		for (i = 0; i < eaSize(&g_pFakePlayer->pSaved->costumeData.eaCostumeSlots); i++)
		{
			if (g_pFakePlayer->pSaved->costumeData.eaCostumeSlots[i]->pCostume)
			{
				count++;
			}
		}
	}

	return count;
}

// Sets the virtual shard ID for the character being created
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_SetVirtualShardID);
bool exprCharacterCreation_SetVirtualShardID(ContainerID iVirtualShardID)
{
	if(g_characterSelectionData && g_characterSelectionData->virtualShardInfos && g_pFakePlayer)
	{
		int i;
		// Check to make sure the player can create a character on this shard
		for (i = eaSize(&g_characterSelectionData->virtualShardInfos)-1; i >= 0; i--)
		{
			if(g_characterSelectionData->virtualShardInfos[i]->iContainerID == iVirtualShardID)
				break;
		}
		if(i >= 0)
		{
			// Set the shard ID
			g_pFakePlayer->pPlayer->iVirtualShardID = iVirtualShardID;
			return true;
		}
	}
	return false;
}

// Returns a random allegiance
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_RandomAllegiance);
const char *exprCharacterCreation_RandomAllegiance(ContainerID iVirtualShardID)
{
	DictionaryEArrayStruct *pAllegiances = resDictGetEArrayStruct("AllegianceDef");
	AllegianceDef **eaValidAllegiances = NULL;
	AllegianceDef *pRandomAllegiance;
	S32 i;

	for (i = eaSize(&pAllegiances->ppReferents) - 1; i >= 0; i--)
	{
		AllegianceDef *pAllegiance = pAllegiances->ppReferents[i];
		if (CharSlots_MatchSlot(g_characterSelectionData->availableCharSlots, iVirtualShardID, pAllegiance->pcName, false))
			eaPush(&eaValidAllegiances, pAllegiance);
	}

	i = floor(eaSize(&eaValidAllegiances) * randomPositiveF32());
	pRandomAllegiance = eaGet(&eaValidAllegiances, i);

	eaDestroy(&eaValidAllegiances);
	return SAFE_MEMBER(pRandomAllegiance, pcName);
}

// Returns the name of a random species in a given allegiance
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_RandomSpecies);
const char *exprCharacterCreation_RandomSpecies(const char *pchAllegiance)
{
	SpeciesDef **eaSpecies = NULL;
	SpeciesDef *pRandomSpecies;
	S32 i;

	CharacterCreation_FillSpeciesListEx(&eaSpecies, pchAllegiance, kFillSpeciesListFlag_MicroTransacted);

	i = floor(eaSize(&eaSpecies) * randomPositiveF32());
	pRandomSpecies = eaGet(&eaSpecies, i);

	eaDestroy(&eaSpecies);
	return SAFE_MEMBER(pRandomSpecies, pcName);
}

void CharacterCreation_UIAllegiance_FillSpeciesDef(CharacterCreationAllegianceUI *pAllegianceUI, CharacterCreationSpeciesUI *pSpeciesUI, SpeciesDef *pSpeciesDef)
{
	if (pSpeciesUI)
	{
		if (pSpeciesDef)
			StructCopyAll(parse_DisplayMessage, &pSpeciesDef->displayNameMsg, &pSpeciesUI->DisplayNameMsg);
		else
			StructReset(parse_DisplayMessage, &pSpeciesDef->displayNameMsg);

		pSpeciesUI->pchTextureName = SAFE_MEMBER(pSpeciesDef, pcTextureName);
		pSpeciesUI->pSpeciesDef = pSpeciesDef;
		pSpeciesUI->bCreatable = SAFE_MEMBER(pAllegianceUI, bCreatable);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_FillUIAllegiances");
void CharacterCreation_FillUIAllegiances(U32 iVirtualShardID)
{
	static SpeciesDef **s_eaRelatedSpecies;
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct("Allegiance");
	S32 i, j, k, l, n = pArray ? eaSize(&pArray->ppReferents) : 0;
	S32 iSelectedAllegiance = -1;
	bool bCanCreate = false;

	s_iUIAllegiancesShard = iVirtualShardID;

	for (i = 0; i < n; i++)
	{
		AllegianceDef *pAllegianceDef = pArray->ppReferents[i];
		CharacterCreationAllegianceUI *pAllegianceUI = NULL;
		S32 iCurrentSpeciesGroupIndex = -1;

		// Find existing UI allegiance
		for (j = i; j < eaSize(&s_eaUIAllegiances); j++)
		{
			if (s_eaUIAllegiances[j]->pchAllegiance == allocFindString(pAllegianceDef->pcName))
			{
				if (j != i)
					eaMove(&s_eaUIAllegiances, i, j);
				pAllegianceUI = s_eaUIAllegiances[i];
				break;
			}
		}

		if (!pAllegianceUI)
		{
			pAllegianceUI = StructCreate(parse_CharacterCreationAllegianceUI);
			pAllegianceUI->pchAllegiance = allocAddString(pAllegianceDef->pcName);
			eaInsert(&s_eaUIAllegiances, pAllegianceUI, i);
		}

		StructCopyAll(parse_DisplayMessage, &pAllegianceDef->displayNameMsg, &pAllegianceUI->DisplayNameMsg);
		pAllegianceUI->pchTextureName = pAllegianceDef->pchIcon;
		pAllegianceUI->pAllegianceDef = pAllegianceDef;

		// Set the creatable flag
		pAllegianceUI->bCreatable = CharSlots_MatchSlot(g_characterSelectionData->availableCharSlots, iVirtualShardID, pAllegianceDef->pcName, false);
		if (pAllegianceDef->bNeedsUnlock && !CharacterSelection_IsVirtualShardUGCShard(iVirtualShardID))
		{
			char *estr = NULL;
			S32 iAllegiance;
			estrStackCreate(&estr);
			estrPrintf(&estr, "%s.%s", GetShortProductName(), pAllegianceDef->pcName);
			iAllegiance = StaticDefineIntGetIntDefault(UnlockedAllegianceFlagsEnum, estr, 0);
			if (iAllegiance == 0 || !g_characterSelectionData || (g_characterSelectionData->unlockAllegianceFlags & iAllegiance) == 0)
				pAllegianceUI->bCreatable = false;
			estrDestroy(&estr);
		}
		if (pAllegianceUI->bCreatable)
			bCanCreate = true;

		// Set the selected flag based on whether or not the fake player is the current allegiance
		pAllegianceUI->bSelected = g_pFakePlayer && GET_REF(g_pFakePlayer->hAllegiance) == pAllegianceDef;
		if (pAllegianceUI->bSelected)
			iSelectedAllegiance = i;

		// Update the list of species
		CharacterCreation_FillSpeciesListEx(&s_eaSpecies, pAllegianceDef->pcName, kFillSpeciesListFlag_NoDuplicates | kFillSpeciesListFlag_MicroTransacted);
		for (k = 0; k < eaSize(&s_eaSpecies); k++)
		{
			SpeciesDef *pSpeciesDef = s_eaSpecies[k];
			CharacterCreationSpeciesUI *pSpeciesUI = NULL;
			S32 iCurrentIndex;

			// Find existing UI species
			for (j = k; j < eaSize(&pAllegianceUI->eaSpecies); j++)
			{
				if (pAllegianceUI->eaSpecies[j]->pchSpeciesName == allocFindString(s_eaSpecies[k]->pcSpeciesName))
				{
					if (j != k)
						eaMove(&pAllegianceUI->eaSpecies, k, j);
					pSpeciesUI = pAllegianceUI->eaSpecies[k];
					break;
				}
			}

			if (!pSpeciesUI)
			{
				pSpeciesUI = StructCreate(parse_CharacterCreationSpeciesUI);
				pSpeciesUI->pchSpeciesName = allocAddString(s_eaSpecies[k]->pcSpeciesName);
				pSpeciesUI->uCostumeSeed = randomU32();
				eaInsert(&pAllegianceUI->eaSpecies, pSpeciesUI, k);
			}

			// Update list of related species
			CharacterCreation_FillGenderListEx(&s_eaRelatedSpecies, pSpeciesDef, kFillSpeciesListFlag_MicroTransacted);
			pSpeciesUI->iSpeciesCount = eaSize(&s_eaRelatedSpecies);
			eaSetSize(&pSpeciesUI->eapchRelatedSpecies, pSpeciesUI->iSpeciesCount);
			for (l = 0; l < pSpeciesUI->iSpeciesCount; l++)
				pSpeciesUI->eapchRelatedSpecies[l] = s_eaRelatedSpecies[l]->pcName;

			iCurrentIndex = eaFind(&pSpeciesUI->eapchRelatedSpecies, pSpeciesUI->pchCurrentSpecies);
			if (iCurrentIndex < 0)
				iCurrentIndex = floor(pSpeciesUI->iSpeciesCount * randomPositiveF32());

			// Set the current species
			if (iCurrentIndex < eaSize(&pSpeciesUI->eapchRelatedSpecies))
			{
				pSpeciesUI->pchCurrentSpecies = pSpeciesUI->eapchRelatedSpecies[iCurrentIndex];
				pSpeciesDef = s_eaRelatedSpecies[iCurrentIndex];
				CharacterCreation_UIAllegiance_FillSpeciesDef(pAllegianceUI, pSpeciesUI, pSpeciesDef);
			}
			else
			{
				pSpeciesUI->pchCurrentSpecies = NULL;
				pSpeciesDef = NULL;
				CharacterCreation_UIAllegiance_FillSpeciesDef(pAllegianceUI, pSpeciesUI, pSpeciesDef);
			}

			eaClear(&s_eaRelatedSpecies);

			// Set the current species index
			pSpeciesUI->bSelected = pAllegianceUI->pchCurrentSpecies == pSpeciesUI->pchSpeciesName;
			if (pSpeciesUI->bSelected)
				iCurrentSpeciesGroupIndex = k;
		}
		eaSetSizeStruct(&pAllegianceUI->eaSpecies, parse_CharacterCreationSpeciesUI, eaSize(&s_eaSpecies));
		eaClear(&s_eaSpecies);

		if (iCurrentSpeciesGroupIndex < 0 || iCurrentSpeciesGroupIndex >= eaSize(&pAllegianceUI->eaSpecies))
			iCurrentSpeciesGroupIndex = floor(eaSize(&pAllegianceUI->eaSpecies) * randomPositiveF32());
		if (iCurrentSpeciesGroupIndex < eaSize(&pAllegianceUI->eaSpecies))
			pAllegianceUI->pchCurrentSpecies = pAllegianceUI->eaSpecies[iCurrentSpeciesGroupIndex]->pchSpeciesName;
	}

	eaSetSizeStruct(&s_eaUIAllegiances, parse_CharacterCreationAllegianceUI, n);

	// Randomly select an allegiance if none are already selected
	while (bCanCreate && (iSelectedAllegiance < 0 || iSelectedAllegiance >= eaSize(&s_eaUIAllegiances) || !s_eaUIAllegiances[iSelectedAllegiance]->bCreatable))
		iSelectedAllegiance = floor(eaSize(&s_eaUIAllegiances) * randomPositiveF32());
	if (iSelectedAllegiance < eaSize(&s_eaUIAllegiances) && iSelectedAllegiance >= 0)
		s_eaUIAllegiances[iSelectedAllegiance]->bSelected = true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_UIAllegiances_GetAllegianceList");
void CharacterCreation_UIAllegiances_GetAllegianceList(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &s_eaUIAllegiances, parse_CharacterCreationAllegianceUI);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_UIAllegiances_GetSpeciesList");
void CharacterCreation_UIAllegiances_GetSpeciesList(SA_PARAM_NN_VALID UIGen *pGen, const char *pchAllegiance)
{
	CharacterCreationAllegianceUI *pAllegianceUI = NULL;
	S32 i;

	pchAllegiance = allocFindString(EMPTY_TO_NULL(pchAllegiance));
	for (i = 0; i < eaSize(&s_eaUIAllegiances); i++)
	{
		if (pchAllegiance && s_eaUIAllegiances[i]->pchAllegiance == pchAllegiance || !pchAllegiance && s_eaUIAllegiances[i]->bSelected)
		{
			pAllegianceUI = s_eaUIAllegiances[i];
			break;
		}
	}

	ui_GenSetList(pGen, pAllegianceUI ? &pAllegianceUI->eaSpecies : NULL, parse_CharacterCreationSpeciesUI);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_UIAllegiances_GetRelatedSpeciesList");
void CharacterCreation_UIAllegiances_GetRelatedSpeciesList(SA_PARAM_NN_VALID UIGen *pGen, const char *pchAllegiance, const char *pchSpecies)
{
	CharacterCreationAllegianceUI *pAllegianceUI = NULL;
	CharacterCreationSpeciesUI *pSpeciesUI = NULL;
	S32 i;

	pchAllegiance = allocFindString(EMPTY_TO_NULL(pchAllegiance));
	pchSpecies = allocFindString(EMPTY_TO_NULL(pchSpecies));

	for (i = 0; i < eaSize(&s_eaUIAllegiances); i++)
	{
		if (pchAllegiance && s_eaUIAllegiances[i]->pchAllegiance == pchAllegiance || !pchAllegiance && s_eaUIAllegiances[i]->bSelected)
		{
			pAllegianceUI = s_eaUIAllegiances[i];
			break;
		}
	}

	if (pAllegianceUI)
	{
		if (!pchSpecies)
			pchSpecies = pAllegianceUI->pchCurrentSpecies;

		for (i = 0; i < eaSize(&pAllegianceUI->eaSpecies); i++)
		{
			if (pAllegianceUI->eaSpecies[i]->pchSpeciesName == pchSpecies)
			{
				pSpeciesUI = pAllegianceUI->eaSpecies[i];
				break;
			}
		}
	}

	if (pSpeciesUI)
	{
		for (i = 0; i < eaSize(&pSpeciesUI->eapchRelatedSpecies); i++)
		{
			SpeciesDef *pSpeciesDef = RefSystem_ReferentFromString("SpeciesDef", pSpeciesUI->eapchRelatedSpecies[i]);
			if (pSpeciesDef)
				eaPush(&s_eaSpecies, pSpeciesDef);
		}
	}

	ui_GenSetList(pGen, &s_eaSpecies, parse_SpeciesDef);
	eaClear(&s_eaSpecies);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_UIAllegiances_SetAllegiance");
void CharacterCreation_UIAllegiances_SetAllegiance(const char *pchAllegiance)
{
	CharacterCreationAllegianceUI *pAllegianceUI = NULL;
	CharacterCreationSpeciesUI *pSpeciesUI = NULL;
	S32 i;

	pchAllegiance = allocFindString(EMPTY_TO_NULL(pchAllegiance));
	for (i = 0; i < eaSize(&s_eaUIAllegiances); i++)
	{
		if (pchAllegiance && s_eaUIAllegiances[i]->pchAllegiance == pchAllegiance || !pchAllegiance && s_eaUIAllegiances[i]->bSelected)
		{
			pAllegianceUI = s_eaUIAllegiances[i];
			break;
		}
	}

	if (!pAllegianceUI)
		return;

	CharacterCreation_SetAllegiance(pAllegianceUI->pchAllegiance);

	for (i = 0; i < eaSize(&pAllegianceUI->eaSpecies); i++)
	{
		if (pAllegianceUI->eaSpecies[i]->pchSpeciesName == pAllegianceUI->pchCurrentSpecies)
		{
			pSpeciesUI = pAllegianceUI->eaSpecies[i];
			break;
		}
	}

	if (!pSpeciesUI)
		return;

	CharacterCreation_SavePlayerSpeciesByName(pSpeciesUI->pchCurrentSpecies);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_UIAllegiances_SetSpecies");
void CharacterCreation_UIAllegiances_SetSpecies(const char *pchAllegiance, const char *pchSpecies)
{
	CharacterCreationAllegianceUI *pAllegianceUI = NULL;
	CharacterCreationSpeciesUI *pSpeciesUI = NULL;
	S32 i;

	pchAllegiance = allocFindString(EMPTY_TO_NULL(pchAllegiance));
	pchSpecies = allocFindString(EMPTY_TO_NULL(pchSpecies));
	if (!pchSpecies)
		return;

	for (i = 0; i < eaSize(&s_eaUIAllegiances); i++)
	{
		if (pchAllegiance && s_eaUIAllegiances[i]->pchAllegiance == pchAllegiance || !pchAllegiance && s_eaUIAllegiances[i]->bSelected)
		{
			pAllegianceUI = s_eaUIAllegiances[i];
			break;
		}
	}

	if (!pAllegianceUI)
		return;

	for (i = 0; i < eaSize(&pAllegianceUI->eaSpecies); i++)
	{
		if (pAllegianceUI->eaSpecies[i]->pchSpeciesName == pchSpecies)
		{
			pSpeciesUI = pAllegianceUI->eaSpecies[i];
			break;
		}
	}

	if (!pSpeciesUI)
		return;

	pAllegianceUI->pchCurrentSpecies = pSpeciesUI->pchSpeciesName;
	if (pAllegianceUI->bSelected)
		CharacterCreation_SavePlayerSpeciesByName(pSpeciesUI->pchCurrentSpecies);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterCreation_UIAllegiances_SetSpeciesDef");
void CharacterCreation_UIAllegiances_SetSpeciesDef(const char *pchAllegiance, const char *pchSpecies, const char *pchSpeciesDef)
{
	CharacterCreationAllegianceUI *pAllegianceUI = NULL;
	CharacterCreationSpeciesUI *pSpeciesUI = NULL;
	S32 i;

	pchAllegiance = allocFindString(EMPTY_TO_NULL(pchAllegiance));
	pchSpecies = allocFindString(EMPTY_TO_NULL(pchSpecies));
	pchSpeciesDef = allocFindString(EMPTY_TO_NULL(pchSpeciesDef));
	if (!pchSpeciesDef)
		return;

	for (i = 0; i < eaSize(&s_eaUIAllegiances); i++)
	{
		if (pchAllegiance && s_eaUIAllegiances[i]->pchAllegiance == pchAllegiance || !pchAllegiance && s_eaUIAllegiances[i]->bSelected)
		{
			pAllegianceUI = s_eaUIAllegiances[i];
			break;
		}
	}

	if (!pAllegianceUI)
		return;

	if (!pchSpecies)
		pchSpecies = pAllegianceUI->pchCurrentSpecies;

	for (i = 0; i < eaSize(&pAllegianceUI->eaSpecies); i++)
	{
		if (pAllegianceUI->eaSpecies[i]->pchSpeciesName == pchSpecies)
		{
			pSpeciesUI = pAllegianceUI->eaSpecies[i];
			break;
		}
	}

	if (!pSpeciesUI)
		return;

	if (eaFind(&pSpeciesUI->eapchRelatedSpecies, pchSpeciesDef) < 0)
		return;

	pSpeciesUI->pchCurrentSpecies = pchSpeciesDef;
	CharacterCreation_UIAllegiance_FillSpeciesDef(pAllegianceUI, pSpeciesUI, RefSystem_ReferentFromString("SpeciesDef", pchSpeciesDef));
	if (pSpeciesUI->bSelected)
		CharacterCreation_SavePlayerSpeciesByName(pSpeciesUI->pchCurrentSpecies);
}

//////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_NAME("CharacterCreation.ChooseExisting") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void CharacterCreation_Return(void)
{
	if (GSM_IsStateActive(GCL_LOGIN_NEW_CHARACTER_CREATION))
		GSM_SwitchToState_Complex(GCL_LOGIN_NEW_CHARACTER_CREATION "/../" GCL_LOGIN_USER_CHOOSING_EXISTING);
}

//////////////////////////////////////////////////////////////////////////
// Finally, connect all the callbacks...

AUTO_RUN;
void CharacterCreation_AutoRegister(void)
{
	GSM_AddGlobalStateCallbacksApp(GCL_LOGIN_NEW_CHARACTER_CREATION, CharacterCreation_Enter, CharacterCreation_OncePerFrame, NULL, CharacterCreation_Leave);
	ui_GenInitStaticDefineVars(FillSpeciesListFlagEnum, "SpeciesGetList");
	ui_GenInitPointerVar("CharacterCreationData", parse_Login2CharacterCreationData);
}

#include "CharacterCreationUI_h_ast.c"
