/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GlobalStateMachine.h"
#include "Expression.h"
#include "EString.h"
#include "objPath.h"
#include "itemCommon.h"
#include "inventoryCommon.h"

#include "GraphicsLib.h"
#include "CostumeCommonGenerate.h"
#include "CostumeCommonTailor.h"

#include "UIGenSprite.h"

#include "gclBaseStates.h"
#include "LoginCommon.h"
#include "gclLogin.h"
#include "species_common.h"
#include "WorldGrid.h"
#include "CharacterClass.h"
#include "SavedPetCommon.h"
#include "OfficerCommon.h"

#include "CharacterSelection.h"
#include "EntityPlayerTypeConversion.h"
#include "gclCostumeUI.h"
#include "GameAccountData\GameAccountData.h"
#include "GameStringFormat.h"
#include "NotifyCommon.h"
#include "microtransactions_common.h"
#include "StringUtil.h"
#include "Prefs.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "Character.h"

#include "LoginCommon_h_ast.h"

#include "GameClientLib.h"
#include "globalTypes.h"
#include "globalComm.h"
#include "ContinuousBuilderSupport.h"
#include "Login2Common.h"
#include "Login2CharacterDetail.h"
#include "gclLogin2.h"

// we're not editing, but the code that looks for the source entity for items assumes that a field in the costume edit
// struct is populated, which is why I'm including this.  Feel free to clean it up.  [RMARR - 4/29/11]
#include "gclCostumeUIState.h"
#include "gclCostumeCameraUI.h"
#include "GameAccountDataCommon.h"

#include "Autogen/CharacterSelection_h_ast.h"
#include "Autogen/Login2Common_h_ast.h"

#include "itemArt.h"



AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

ContainerID g_CharacterSelectionPlayerId = 0;
static S32 s_RenamingIndex = -1;
#define INVALID_RENAMINGID 0xFFFFFFFF
static ContainerID s_RenamingId = INVALID_RENAMINGID;
static PlayerTypeConversion *s_pConversion = NULL;

// the following can be used to prevent demo messages from popping up
static S32 gbFullAccessAccount;
AUTO_CMD_INT(gbFullAccessAccount, FullAccessAccount) ACMD_CMDLINE;

static void LoginUserChoosingCharacter_Enter(void)
{
	if (!GSM_AreAnyStateChangesRequested())
	{
		GSM_SwitchToState_Complex(GCL_LOGIN_USER_CHOOSING_CHARACTER "/" GCL_LOGIN_USER_CHOOSING_EXISTING);
	}
}

static void LoginUserChoosingCharacter_OncePerFrame(void)
{
	Entity *playerEnt = gclLogin2_CharacterDetailCache_GetEntity(g_CharacterSelectionPlayerId);
	ui_GenSetPointerVar("Player", playerEnt, parse_Entity);
}

static void LoginUserChoosingCharacter_Exit(void)
{
	ui_GenSetPointerVar("Player", NULL, parse_Entity);
}

AUTO_RUN;
void CharacterSelection_AutoRegister(void)
{
	GSM_AddGlobalStateCallbacksApp(GCL_LOGIN_USER_CHOOSING_CHARACTER, LoginUserChoosingCharacter_Enter, LoginUserChoosingCharacter_OncePerFrame, NULL, LoginUserChoosingCharacter_Exit);
}

extern EARRAY_OF(Login2CharacterChoice) g_SortedCharacterChoices;

// Fill in the list of possible character choices.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetCharacterChoices");
void CharacterSelection_GetCharacterChoices(SA_PARAM_NN_VALID UIGen *pGen)
{
    ui_GenSetList(pGen, &g_SortedCharacterChoices, parse_Login2CharacterChoice);
}

// Get the Index of the most-recently played character IN THE CHARACTER SELECTION GEN'S LIST
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GenGetMostRecentCharacterChoiceIndex");
int CharacterSelection_GenGetMostRecentCharacterChoiceIndex(SA_PARAM_OP_VALID UIGen *pGen)
{
	CharacterSelectionSlot ***peaSlots = ui_GenGetManagedListSafe(pGen, CharacterSelectionSlot);
	int i;
	unsigned int iBestTime = 0;
	int iBestIndex = 0;
	for (i = 0; i < eaSize(peaSlots); i++)
	{
		if ((*peaSlots)[i] && (*peaSlots)[i]->pChoice && (*peaSlots)[i]->pChoice->lastPlayedTime > iBestTime)
		{
			iBestTime = (*peaSlots)[i]->pChoice->lastPlayedTime;
			iBestIndex = i;
		}
	}
	ui_GenSetListSafe(pGen, peaSlots, CharacterSelectionSlot);
	return iBestIndex;
}

// Get the ID of the most-recently played character ON THE GIVEN SHARD
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetMostRecentCharacterChoiceIDOnShard");
int CharacterSelection_GetMostRecentCharacterChoiceIDOnShard(ContainerID iVirtualShardID)
{
	int i;
	unsigned int iBestTime = 0;
	ContainerID iBestID = 0;
	for (i = 0; i < eaSize(&g_characterSelectionData->characterChoices->characterChoices); i++)
	{
		Login2CharacterChoice *pChoice = g_characterSelectionData->characterChoices->characterChoices[i];
        if (pChoice->virtualShardID == iVirtualShardID && pChoice->lastPlayedTime > iBestTime)
		{
			iBestTime = pChoice->lastPlayedTime;
			iBestID = pChoice->containerID;
		}
	}
	return iBestID;
}

// For logging in a character when you don't have the container ID
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetCharacterChoiceIDByIndexOnVirtualShard");
int CharacterSelection_GetCharacterChoiceIDByIndexOnVirtualShard(U32 iIndex, U32 iVirtualShardID)
{
	int i;
	ContainerID iBestID = 0;
	for (i = 0; i < eaSize(&g_characterSelectionData->characterChoices->characterChoices); i++)
	{
		Login2CharacterChoice *pChoice = g_characterSelectionData->characterChoices->characterChoices[i];
		if (pChoice->virtualShardID == iVirtualShardID)
		{
			if( iIndex-- == 0 )
			{
				iBestID = pChoice->containerID;
				break;
			}
		}
	}
	return iBestID;
}

static bool CharacterSelection_PlayerHasAccessToVirtualShard(ContainerID iVirtualShardID)
{
	int i;

    if ( g_characterSelectionData )
    {
        for ( i = eaSize(&g_characterSelectionData->virtualShardInfos) - 1; i >= 0; i-- )
        {
            PossibleVirtualShard *virtualShardInfo = g_characterSelectionData->virtualShardInfos[i];
            if ( virtualShardInfo->iContainerID == iVirtualShardID )
            {
                return true;
            }
        }
    }

    return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetVirtualShardList");
void CharacterSelection_GetVirtualShardList(SA_PARAM_NN_VALID UIGen *pGen)
{
    if(g_characterSelectionData)
	{
		ui_GenSetList(pGen, &g_characterSelectionData->virtualShardInfos, parse_PossibleVirtualShard);
	}
	else
	{
		ui_GenSetList(pGen, NULL, parse_PossibleVirtualShard);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetVirtualShardListExcludingSpecifiedShard");
void CharacterSelection_GetVirtualShardListExcludingSpecifiedShard(SA_PARAM_NN_VALID UIGen *pGen, ContainerID iVirtualShardID)
{
	static PossibleVirtualShard** s_eaShards = NULL;
	if(!s_eaShards)
		eaCreate(&s_eaShards);
	else
		eaClear(&s_eaShards);

    if(g_characterSelectionData)
	{
		int i;

		for (i=0; i < eaSize(&g_characterSelectionData->virtualShardInfos); i++)
		{
			if(g_characterSelectionData->virtualShardInfos[i]->iContainerID != iVirtualShardID)
			{
				eaPush(&s_eaShards, g_characterSelectionData->virtualShardInfos[i]);
			}
		}
	}

	ui_GenSetManagedListSafe(pGen, &s_eaShards, PossibleVirtualShard, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetVirtualCount");
int CharacterSelection_GetVirtualCount(void)
{
	if(g_characterSelectionData)
	{
		return eaSize(&g_characterSelectionData->virtualShardInfos);
	}

	return 0;
}


// Fill in the list of possible character choice.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetCharacterChoicesForVirtualShard");
void CharacterSelection_GetCharacterChoicesForVirtualShard(SA_PARAM_NN_VALID UIGen *pGen, ContainerID iVirtualShardID)
{
	static Login2CharacterChoice** s_eaChoices = NULL;
	if(!s_eaChoices)
		eaCreate(&s_eaChoices);
	else
		eaClear(&s_eaChoices);

	if(g_characterSelectionData && g_SortedCharacterChoices && CharacterSelection_PlayerHasAccessToVirtualShard(iVirtualShardID))
	{
		int i;

		for (i=0; i < eaSize(&g_SortedCharacterChoices); i++)
		{
            Login2CharacterChoice *characterChoice = g_SortedCharacterChoices[i];
			if(characterChoice->virtualShardID == iVirtualShardID)
			{
				eaPush(&s_eaChoices, characterChoice);
			}
		}
	}
	ui_GenSetManagedListSafe(pGen, &s_eaChoices, Login2CharacterChoice, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_IsVirtualShardDisabled");
bool CharacterSelection_IsVirtualShardDisabled(ContainerID iVirtualShardID)
{
	if(g_characterSelectionData)
	{
		int i;

		for (i=0; i < eaSize(&g_characterSelectionData->virtualShardInfos); i++)
		{
            PossibleVirtualShard *virtualShardInfo = g_characterSelectionData->virtualShardInfos[i];
			if(virtualShardInfo->iContainerID == iVirtualShardID)
			{
                return virtualShardInfo->bDisabled && LoginGetAccessLevel() < ACCESS_GM_FULL;
			}
		}
	}
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_IsVirtualShardUGCShard");
bool CharacterSelection_IsVirtualShardUGCShard(ContainerID iVirtualShardID)
{
	if(g_characterSelectionData)
	{
		int i;

        for (i=0; i < eaSize(&g_characterSelectionData->virtualShardInfos); i++)
        {
            PossibleVirtualShard *virtualShardInfo = g_characterSelectionData->virtualShardInfos[i];
			if(virtualShardInfo->iContainerID == iVirtualShardID)
			{
				return virtualShardInfo->bUGCShard;
			}
		}
	}
	return false;
}

// Sets the shard which will be initially selected upon logging in
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_SetInitialVirtualShard");
void CharacterSelection_SetPreferedVirtualShard(ContainerID iVirtualShardID)
{
	GamePrefStoreInt("InitialVirtualShard", iVirtualShardID);
}

// Gets the shard which will be initially selected upon logging in
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetInitialVirtualShard");
ContainerID CharacterSelection_GetInitialVirtualShard()
{
	ContainerID iVirtualShard = GamePrefGetInt("InitialVirtualShard", 0);

	if(!CharacterSelection_PlayerHasAccessToVirtualShard(iVirtualShard))
		iVirtualShard = 0;

	return iVirtualShard;
}

// Get the count of possible character choice across all virtual shards.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetCharacterChoiceCount");
int CharacterSelection_GetCharacterChoiceCount(void)
{
	return eaSize(&g_characterSelectionData->characterChoices->characterChoices);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetCharacterChoiceCountForVirtualShard");
int CharacterSelection_GetCharacterChoiceCountForVirtualShard(U32 iVirtualShard)
{
	int i;
	int iCount = 0;
	for(i=0; i<eaSize(&g_characterSelectionData->characterChoices->characterChoices); ++i )
	{
		if( g_characterSelectionData->characterChoices->characterChoices[i]->virtualShardID == iVirtualShard )
			++iCount;
	}
	return iCount;
}

// Find the starship puppet (if any) for the character choice
Entity *GetStarshipFromChoice(Login2CharacterChoice *pChoice)
{
    return gclLogin2_CharacterDetailCache_GetPuppet(pChoice->containerID, 1);
}

// Find the ground puppet (if any) for the character choice
Entity *GetGroundFromChoice(Login2CharacterChoice *pChoice)
{
    return gclLogin2_CharacterDetailCache_GetPuppet(pChoice->containerID, 2);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetChoiceLocation");
const char *CharacterSelection_GetChoiceLocation(Login2CharacterChoice *pChoice)
{
	if (pChoice) 
    {
        Entity *playerEnt = gclLogin2_CharacterDetailCache_GetEntity(pChoice->containerID);
        if ( playerEnt )
        {
            SavedMapDescription *mapDescription = entity_GetLastMap(playerEnt);
            if ( mapDescription ) 
            {
		        ZoneMapInfo *pZone = RefSystem_ReferentFromString("ZoneMap", mapDescription->mapDescription);
		        if (pZone) 
                {
			        return TranslateMessagePtr(zmapInfoGetDisplayNameMessagePtr(pZone));
                }
            }
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetChoiceLevel");
int CharacterSelection_GetChoiceLevel(Login2CharacterChoice *pChoice)
{
	if (pChoice) 
    {
		return pChoice->level;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetChoiceRank");
const char *CharacterSelection_GetChoiceRank(Login2CharacterChoice *pChoice)
{
	if (pChoice) 
    {
        Entity *playerEnt = gclLogin2_CharacterDetailCache_GetEntity(pChoice->containerID);
        if ( playerEnt )
        {
            int playerRank = inv_GetNumericItemValue(playerEnt, "Starfleetrank");
            AllegianceDef *allegianceDef =  GET_REF(playerEnt->hAllegiance);
            AllegianceDef *subAllegianceDef = GET_REF(playerEnt->hSubAllegiance);
            if ( allegianceDef )
            {
			    OfficerRankDef* rankDef = Officer_GetRankDef(playerRank, allegianceDef, subAllegianceDef);
				if (rankDef && rankDef->pDisplayMessage) {
					return TranslateMessagePtr(GET_REF(rankDef->pDisplayMessage->hMessage));
				}
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetChoiceRankInt");
int CharacterSelection_GetChoiceRankInt(Login2CharacterChoice *pChoice)
{
    if (pChoice) 
    {
        Entity *playerEnt = gclLogin2_CharacterDetailCache_GetEntity(pChoice->containerID);
        if ( playerEnt )
        {
            return(inv_GetNumericItemValue(playerEnt, "Starfleetrank"));
        }
    }
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetChoiceGrade");
int CharacterSelection_GetChoiceGrade(Login2CharacterChoice *pChoice)
{
	if (pChoice) 
    {
		int playerRank;

        playerRank = CharacterSelection_GetChoiceRankInt(pChoice);

		if (playerRank >= 0)  
        {
			AllegianceDef *pAllegiance = RefSystem_ReferentFromString("Allegiance", Login2_GetAllegianceFromCharacterChoice(pChoice));
			return Officer_GetGradeFromLevelAndRank(pAllegiance, pChoice->level, playerRank);
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetChoiceClass");
const char *CharacterSelection_GetChoiceClass(Login2CharacterChoice *pChoice)
{
	if (pChoice) {
		CharacterClass *pClass = RefSystem_ReferentFromString("CharacterClass", Login2_GetClassNameFromCharacterChoice(pChoice));
		if (pClass && GET_REF(pClass->msgDisplayName.hMessage)) {
			return TranslateDisplayMessage(pClass->msgDisplayName);
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetChoicePath");
const char *CharacterSelection_GetChoicePath(Login2CharacterChoice *pChoice)
{
	if (pChoice)
	{

		CharacterPath *pCharacterPath = RefSystem_ReferentFromString("CharacterPath", Login2_GetPathNameFromCharacterChoice(pChoice));
		if (pCharacterPath)
		{
			return TranslateDisplayMessage(pCharacterPath->pDisplayName);
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetChoiceGroundClass");
const char *CharacterSelection_GetChoiceGroundClass(Login2CharacterChoice *pChoice)
{
	Entity *groundPuppet = GetGroundFromChoice(pChoice);

	if (groundPuppet && groundPuppet->pChar)
    {
		CharacterClass *pClass = GET_REF(groundPuppet->pChar->hClass);
		if (pClass && GET_REF(pClass->msgDisplayName.hMessage)) 
        {
			return TranslateDisplayMessage(pClass->msgDisplayName);
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetChoiceGroundClassUntranslated");
const char *CharacterSelection_GetChoiceGroundClassUntranslated(Login2CharacterChoice *pChoice)
{
    Entity *groundPuppet = GetGroundFromChoice(pChoice);

    if (groundPuppet && groundPuppet->pChar)
    {
		CharacterClass *pClass = GET_REF(groundPuppet->pChar->hClass);
		if (pClass && GET_REF(pClass->msgDisplayName.hMessage)) {
			return pClass->pchName;
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetChoiceStarshipClass");
const char *CharacterSelection_GetChoiceStarshipClass(Login2CharacterChoice *pChoice)
{
    Entity *shipPuppet = GetStarshipFromChoice(pChoice);

    if (shipPuppet && shipPuppet->pChar)
    {
		CharacterClass *pClass = GET_REF(shipPuppet->pChar->hClass);
		if (pClass && GET_REF(pClass->msgDisplayName.hMessage)) {
			return TranslateDisplayMessage(pClass->msgDisplayName);
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetChoiceAllegianceUntranslated");
const char *CharacterSelection_GetChoiceAllegianceUntranslated(Login2CharacterChoice *pChoice)
{
	if (pChoice) 
    {
		return Login2_GetAllegianceFromCharacterChoice(pChoice);
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetChoiceSpecies");
const char *CharacterSelection_GetChoiceSpecies(Login2CharacterChoice *pChoice)
{
	if (pChoice) 
    {
		SpeciesDef *pSpecies = RefSystem_ReferentFromString("SpeciesDef", Login2_GetSpeciesNameFromCharacterChoice(pChoice));
		if (pSpecies && GET_REF(pSpecies->displayNameMsg.hMessage)) 
        {
			return TranslateDisplayMessage(pSpecies->displayNameMsg);
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetChoiceGroundSpecies");
const char *CharacterSelection_GetChoiceGroundSpecies(Login2CharacterChoice *pChoice)
{
	Entity *groundPuppet = GetGroundFromChoice(pChoice);

	if (groundPuppet) 
    {
		SpeciesDef *pSpecies = GET_REF(groundPuppet->pChar->hSpecies);
		if (pSpecies && GET_REF(pSpecies->displayNameMsg.hMessage)) 
        {
			return TranslateDisplayMessage(pSpecies->displayNameMsg);
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetChoiceGender");
const char *CharacterSelection_GetChoiceGender(Login2CharacterChoice *pChoice)
{
    if (pChoice) 
    {
        SpeciesDef *pSpecies = RefSystem_ReferentFromString("SpeciesDef", Login2_GetSpeciesNameFromCharacterChoice(pChoice));
        if (pSpecies) 
        {
            return TranslateDisplayMessage(pSpecies->genderNameMsg);
        }
    }
    return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterSelection_GetChoiceGroundGender);
const char *CharacterSelection_GetChoiceGroundGender(Login2CharacterChoice *pChoice)
{
    Entity *groundPuppet = GetGroundFromChoice(pChoice);

    if (groundPuppet) 
    {
        SpeciesDef *pSpecies = GET_REF(groundPuppet->pChar->hSpecies);
        if (pSpecies) 
        {
            return TranslateDisplayMessage(pSpecies->genderNameMsg);
        }
    }
    return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterSelection_GetChoiceFormalName);
const char *CharacterSelection_GetChoiceFormalName(Login2CharacterChoice *pChoice)
{
	if (pChoice) 
    {
        Entity *playerEnt = gclLogin2_CharacterDetailCache_GetEntity(pChoice->containerID);
        if ( playerEnt && playerEnt->pSaved )
        {
		    return FormalName_GetFullNameFromSubName(playerEnt->pSaved->savedSubName);
        }
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterSelection_GetChoiceStarshipName);
const char *CharacterSelection_GetChoiceStarshipName(Login2CharacterChoice *pChoice)
{
	Entity *shipEnt = GetStarshipFromChoice(pChoice);

	if (shipEnt && shipEnt->pSaved) 
    {
		return shipEnt->pSaved->savedName;
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterSelection_GetChoiceStarshipRegistry);
const char *CharacterSelection_GetChoiceStarshipRegistry(Login2CharacterChoice *pChoice)
{
	Entity *shipEnt = GetStarshipFromChoice(pChoice);

	if (shipEnt && shipEnt->pSaved && !stricmp("Allegiance_Starfleet", Login2_GetAllegianceFromCharacterChoice(pChoice))) 
    {
		return shipEnt->pSaved->savedSubName;
	}
	return NULL;
}

// Choose a character
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Login_ChooseCharacterByID);
void LoginExpr_ChooseCharacterByID(ContainerID id)
{
    Login2CharacterChoice *characterChoice = CharacterSelection_GetChoiceByID(id);
    if ( characterChoice )
    {
	    gclLoginChooseCharacter(characterChoice);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Login_CharacterChoiceByID);
SA_RET_OP_VALID Login2CharacterChoice * CharacterSelection_GetChoiceByID(ContainerID iID)
{
	if (g_characterSelectionData && g_characterSelectionData->characterChoices)
	{
		return(eaIndexedGetUsingInt(&g_characterSelectionData->characterChoices->characterChoices, iID));
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterSelection_GetCharacterAllegiance);
const char *CharacterSelection_GetCharacterAllegiance(ContainerID id)
{
    Login2CharacterChoice *characterChoice = CharacterSelection_GetChoiceByID(id);
    if ( characterChoice )
    {
        return Login2_GetAllegianceFromCharacterChoice(characterChoice);
    }
	return "";
}

// DEPRECATED: use UIGenPaperdoll
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void CharacterSelection_FreeHeadshots(void);

// DEPRECATED: use UIGenPaperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterSelection_FreeHeadshots);
void CharacterSelection_FreeHeadshots(void)
{
}

// DEPRECATED: use UIGenPaperdoll
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void CharacterSelection_SetCharacterPortraitBackground(const char *texture)
{
}

// DEPRECATED: use UIGenPaperdoll
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void CharacterSelection_BeforeTick(void);

// DEPRECATED: use UIGenPaperdoll
AUTO_EXPR_FUNC(UIGen);
void CharacterSelection_BeforeTick(void)
{
}

// DEPRECATED: use UIGenPaperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_OverrideCharacterPortraitTexture");
void CharacterSelectionExpr_OverrideCharacterPortraitTexture(ACMD_NAMELIST("UIGen", REFDICTIONARY) const char *pchGen, int selection)
{
}

static NOCONST(PlayerCostume) *
GetActiveCostume(Entity *pEnt)
{
    PlayerCostume *playerCostume;
    if ( pEnt && pEnt->pSaved )
    {
        int activeCostume = pEnt->pSaved->costumeData.iActiveCostume;

        // Be sure to bounds check the active costume index.
        if ( ( activeCostume >= 0 ) && ( activeCostume < eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots) ) )
        {
            playerCostume = pEnt->pSaved->costumeData.eaCostumeSlots[activeCostume]->pCostume;

            return CONTAINER_NOCONST(PlayerCostume, playerCostume);
        }

    }
    return NULL;
}

static ContainerID s_lastOverrideCharacterID = 0;

// Forward reference
static void OverrideCostumeInternal(Login2CharacterChoice *characterChoice, CharClassTypes prefPetType);

// This callback is called when the character detail eventually arrives.
static void
CharacterDetailArrived_CB(ContainerID playerID, GCLLogin2FetchResult result, void *userData)
{
    // Only update the costume display if this detail matches the last requested character and the fetch succeeded.
    if ( result == FetchResult_Succeeded && playerID == s_lastOverrideCharacterID )
    {
        CharClassTypes prefPetType = (CharClassTypes)((intptr_t)userData);
        Login2CharacterChoice *characterChoice = CharacterSelection_GetChoiceByID(playerID);

        OverrideCostumeInternal(characterChoice, prefPetType);
    }
}

static void
OverrideCostumeInternal(Login2CharacterChoice *characterChoice, CharClassTypes prefPetType)
{
    static bool itemCostumeDataLoaded = false;
    static NOCONST(PlayerCostume) *pLastCostume = NULL;
    NOCONST(PlayerCostume) *pCostume = NULL;

    if ( characterChoice )
    {
        // Get character details.
        Entity *playerEnt = gclLogin2_CharacterDetailCache_GetEntity(characterChoice->containerID);

        s_lastOverrideCharacterID = characterChoice->containerID;

        if ( playerEnt && playerEnt->pChar)
        {
            CharacterClass *displayedClass = NULL;
            displayedClass = GET_REF(playerEnt->pChar->hClass);

            if ( displayedClass )
            {
                if ( displayedClass->eType != prefPetType )
                {
                    // Players class type does not match the one requested.  Find a puppet that does match.
                    Entity *puppetEnt = gclLogin2_CharacterDetailCache_GetPuppet(characterChoice->containerID, prefPetType);
                    if ( puppetEnt )
                    {
                        pCostume = GetActiveCostume(puppetEnt);
                        if ( puppetEnt->pChar)
                        {
                            displayedClass = GET_REF(puppetEnt->pChar->hClass);
                        }
                    }
                    else
                    {

                    }

                }

                // If we didn't get the costume from a puppet, get it from the player.
                if ( pCostume == NULL )
                {
                    pCostume = GetActiveCostume(playerEnt);
                }

                if ( pCostume && displayedClass )
                {

                    if ( pLastCostume != pCostume )
                    {
                        // Costume changed.
                        g_CostumeEditState.eCostumeStorageType = kPCCostumeStorageType_Primary;
                        g_CostumeEditState.uCostumeEntContainerID = characterChoice->containerID;
                        g_CostumeEditState.iCostumeIndex = 0;

                        pLastCostume = pCostume;

                        REF_HANDLE_SET_FROM_STRING("CharacterClass", displayedClass->pchName, g_CostumeEditState.hClass);
                        itemCostumeDataLoaded = item_GetItemCostumeDataToShow(PARTITION_CLIENT, playerEnt, NULL, NULL);

						COPY_HANDLE(g_CostumeEditState.hSpecies, playerEnt->pChar->hSpecies);

                        if ( costumeCameraUI_IsShowingCostumeItems() && !itemCostumeDataLoaded )
                        {
                            // If we're supposed to show the items on the entity, clear the costume so players don't see an incomplete version of the costume
                            CostumeUI_ClearCostume();
                        }
                        else
                        {
                            // We have everything we need, so go ahead and display the costume.
                            devassert(playerEnt);  // This should never fail.  Here to satisfy static analysis.
                            gclEntity_UpdateItemArtAnimFX(playerEnt, displayedClass, &g_CostumeEditState.eaFXArray, false);
                            CostumeUI_SetCostumeEx(pCostume, displayedClass, false, true);
                        }
                    } 
                    else if ( costumeCameraUI_IsShowingCostumeItems() && !itemCostumeDataLoaded )
                    {
                        // We have been waiting for items and other dependent data to load.
                        itemCostumeDataLoaded = item_GetItemCostumeDataToShow(PARTITION_CLIENT, playerEnt, NULL, NULL);

                        if ( itemCostumeDataLoaded )
                        {
                            // We have everything we need, so go ahead and display the costume.
                            devassert(playerEnt);  // This should never fail.  Here to satisfy static analysis.
                            gclEntity_UpdateItemArtAnimFX(playerEnt, displayedClass, &g_CostumeEditState.eaFXArray, false);
                            CostumeUI_SetCostumeEx(pCostume, displayedClass, false, true);
                        }
                    }
                }
            }
        }
        else
        {
            // Entity not found in cache, so fetch it.
            gclLogin2_CharacterDetailCache_Fetch(characterChoice->containerID, CharacterDetailArrived_CB, (void *)((intptr_t)prefPetType));
        }
    }
    else
    {
        s_lastOverrideCharacterID = 0;
    }

    if ( pCostume == NULL )
    {
        // No costume was found, so clear it.
        pLastCostume = NULL;
        CostumeUI_ClearCostume();
    }
}
// Returns true if a costume is set and properly loaded
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_OverrideCharacterCostume");
void CharacterSelectionExpr_OverrideCharacterCostume(ContainerID characterID, U32 prefPetType)
{
    Login2CharacterChoice *characterChoice = NULL;

    characterChoice = CharacterSelection_GetChoiceByID(characterID);

    OverrideCostumeInternal(characterChoice, prefPetType);
}

// DEPRECATED: use UIGenPaperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_InitializePortraits2");
void CharacterSelection_SetDefaultPortraitInfo(ExprContext *pContext, const char *texture)
{
}


// DEPRECATED: use UIGenPaperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_InitializePortraits");
void CharacterSelection_SetDefaultPortraitInfoOld(ExprContext *pContext, float fWidth, float fHeight, const char *texture)
{
}

// Compute the arctangent of the given value.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_ComputeFanRotation");
float CharacterSelectionExpr_ComputeFanRotation(float fValue)
{
	return atanf(fValue);
}

// DEPRECATED: use UIGenPaperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetCharacterPortraitTexture");
SA_RET_OP_VALID BasicTexture *CharacterSelectionExpr_GetCharacterPortraitTexture(SA_PARAM_NN_VALID Login2CharacterChoice *pChoice, const char *pchBackground, F32 fWidth, F32 fHeight)
{
	return NULL;
}

// DEPRECATED: use UIGenPaperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_RemoveCharacterPortraitTexture");
SA_RET_OP_VALID BasicTexture *CharacterSelectionExpr_RemoveCharacterPortraitTexture(SA_PARAM_NN_VALID Login2CharacterChoice *pChoice, int bDestroy)
{
	return NULL;
}

// Return the number of unrestricted slots available
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_TotalSlots");
int CharacterSelection_TotalSlots(ExprContext *pContext)
{
	if ( g_characterSelectionData && g_characterSelectionData->availableCharSlots )
	{
		return g_characterSelectionData->availableCharSlots->numTotalSlots;
	}
	return 0;
}

// Return the number of unrestricted slots available
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_SlotsAvailableUnrestricted");
int CharacterSelection_SlotsAvailableUnrestricted(ExprContext *pContext)
{
	if ( g_characterSelectionData && g_characterSelectionData->availableCharSlots )
	{
		return g_characterSelectionData->availableCharSlots->numUnrestrictedSlots;
	}
	return 0;
}

// Return the number of restricted slots available
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_SlotsAvailableRestrictedByShard");
int CharacterSelection_SlotsAvailableRestrictedByShard(ExprContext *pContext, ContainerID iVirtualShardID)
{
	int numSlots = 0;
	if ( g_characterSelectionData && g_characterSelectionData->availableCharSlots )
	{
		AvailableCharSlots *availableCharSlots = g_characterSelectionData->availableCharSlots;
		int i;

		for ( i = 0; i < eaSize(&availableCharSlots->eaSlotRestrictions); i++ )
		{
			CharSlotRestriction *slotRestriction = availableCharSlots->eaSlotRestrictions[i];

			// only add restricted slots that will work with the specified virtual shard
			if ( ( ( slotRestriction->flags & CharSlotRestrictFlag_VirtualShard ) == 0 ) || slotRestriction->virtualShardID == iVirtualShardID )
			{
				numSlots += slotRestriction->numSlots;
			}
		}
	}

	return numSlots;
}

// Return a string describing the available character slots
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_SlotsAvailableString");
const char *CharacterSelection_SlotsAvailableString(ExprContext *pContext, ContainerID iVirtualShardID)
{
    static char *s_tmpStr = NULL;
    static char *s_resultStr = NULL;
    int i;
    bool lineBreak = false;
    bool foundSlots = false;

    estrClear(&s_resultStr);
    if ( g_characterSelectionData && g_characterSelectionData->availableCharSlots )
    {
        AvailableCharSlots *availableCharSlots = g_characterSelectionData->availableCharSlots;

        if ( availableCharSlots->numUnrestrictedSlots )
        {
            estrClear(&s_tmpStr);
            FormatGameMessageKey(&s_tmpStr, "CharSlotsAvailable.Unrestricted", STRFMT_INT("NumSlots", availableCharSlots->numUnrestrictedSlots), STRFMT_END);
            estrAppend(&s_resultStr, &s_tmpStr);
            lineBreak = true;
            foundSlots = true;
        }

        for ( i = 0; i < eaSize(&availableCharSlots->eaSlotRestrictions); i++ )
        {
            CharSlotRestriction *slotRestriction = availableCharSlots->eaSlotRestrictions[i];
            static char *s_msgKey = NULL;

            // only add restricted slots that will work with the specified virtual shard
            if ( ( ( slotRestriction->flags & CharSlotRestrictFlag_VirtualShard ) == 0 ) || slotRestriction->virtualShardID == iVirtualShardID )
            {

                if ( slotRestriction->flags & CharSlotRestrictFlag_Allegiance )
                {
                    if ( slotRestriction->flags & CharSlotRestrictFlag_VirtualShard )
                    {
                        estrPrintf(&s_msgKey, "CharSlotsAvailable.%s.%d", slotRestriction->allegianceName, slotRestriction->virtualShardID);
                    }
                    else
                    {
                        estrPrintf(&s_msgKey, "CharSlotsAvailable.%s.Any", slotRestriction->allegianceName);
                    }
                }
                else
                {
                    if ( slotRestriction->flags & CharSlotRestrictFlag_VirtualShard )
                    {
                        estrPrintf(&s_msgKey, "CharSlotsAvailable.Any.%d", slotRestriction->virtualShardID);
                    }
                    else
                    {
                        estrPrintf(&s_msgKey, "CharSlotsAvailable.Error");
                    }
                }

                estrClear(&s_tmpStr);
                FormatGameMessageKey(&s_tmpStr, s_msgKey, STRFMT_INT("NumSlots", slotRestriction->numSlots), STRFMT_INT("LineBreak", lineBreak), STRFMT_END);
                estrAppend(&s_resultStr, &s_tmpStr);
                lineBreak = true;
                foundSlots = true;
            }
        }
    }

    if ( !foundSlots )
    {
        FormatGameMessageKey(&s_resultStr, "CharSlotsAvailable.None", STRFMT_END);
    }

    return s_resultStr ? exprContextAllocString(pContext, s_resultStr) : "";
}

// Return true if there is a character slot available for the virtual shard and allegiance combination
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_SlotAvailable");
bool CharacterSelection_SlotAvailable(ContainerID iVirtualShardID, const char *allegianceName)
{
    return CharSlots_MatchSlot(g_characterSelectionData->availableCharSlots, iVirtualShardID, allegianceName, false);
}

static void gclCharacterSelection_CreateNewCharacter(void)
{
	if (GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_EXISTING))
	{
		GSM_SwitchToState_Complex(GCL_LOGIN_USER_CHOOSING_EXISTING "/../" GCL_LOGIN_NEW_CHARACTER_WAITING_FOR_DATA);
	}
}

// Create a new character during the login process.
AUTO_COMMAND ACMD_NAME("Login_NewCharacterForVirtualShardAndAllegiance") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void CharacterSelection_NewCharacterForVirtualShardAndAllegiance(ContainerID iVirtualShardID, const char *allegianceName)
{
    if(CharSlots_MatchSlot(g_characterSelectionData->availableCharSlots, iVirtualShardID, allegianceName, false))
    {
        gclCharacterSelection_CreateNewCharacter();
    }
}

// Set the skip tutorial flag on the client
AUTO_COMMAND ACMD_NAME("SetNewCharacterSkipTutorial") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void CharacterSelection_SetNewCharacterSkipTutorial(bool bSkipTutorial)
{
	gGCLState.bSkipTutorial = bSkipTutorial;
}

// 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Login_UGCCharactersOnly);
bool CharacterSelectionExpr_UGCCharactersOnly(void)
{
	return g_characterSelectionData->UGCLoginOnly;
}

// Delete a character, specified by container ID.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Login_DeleteCharacterID);
void CharacterSelectionExpr_DeleteCharacterID(ContainerID iContainerID)
{
	if (GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_EXISTING))
	{
        Login2CharacterChoice *characterChoice = CharacterSelection_GetChoiceByID(iContainerID);
        if ( characterChoice )
        {
			gclLoginRequestCharacterDelete(characterChoice);
		}
	}
}

// Resets the PlayerType conversion data
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Login_ResetConvertCharacterPlayerType);
void CharacterSelectionExpr_ResetConvertCharacterPlayerType(void)
{
	if(!s_pConversion)
		s_pConversion = StructCreate(parse_PlayerTypeConversion);
	else
		StructReset(parse_PlayerTypeConversion,s_pConversion);
}

// Sets the target PlayerType for PlayerType conversion
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Login_SetConvertCharacterPlayerType);
void CharacterSelectionExpr_SetConvertCharacterPlayerType(ACMD_EXPR_ENUM(PlayerType) const char *pchType)
{
	if(GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_EXISTING))
	{
		PlayerType eType = StaticDefineIntGetInt(PlayerTypeEnum,pchType);
		if(eType > kPlayerType_None)
		{
			if(!s_pConversion)
				s_pConversion = StructCreate(parse_PlayerTypeConversion);
			s_pConversion->iPlayerTypeNew = eType;
		}
	}
}

// Sets the target CharacterPath for PlayerType conversion
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Login_SetConvertCharacterPlayerTypePath);
void CharacterSelectionExpr_SetConvertCharacterPlayerTypePath(const char *pchPath)
{
	if(GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_EXISTING))
	{
		if(!s_pConversion)
			s_pConversion = StructCreate(parse_PlayerTypeConversion);
		StructFreeStringSafe(&s_pConversion->pchCharacterPath);
		s_pConversion->pchCharacterPath = StructAllocString(pchPath);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Login_SetConvertCharacterToFreeform);
void CharacterSelectionExpr_SetConvertCharacterToFreeform(int bFreeform)
{
	if(!s_pConversion)
		s_pConversion = StructCreate(parse_PlayerTypeConversion);
	s_pConversion->bConvertToFreeform = bFreeform;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Login_SetConvertCharacterSideConvert);
void CharacterSelectionExpr_SetConvertCharacterSideConvert(int bSideConvert)
{
	if(!s_pConversion)
		s_pConversion = StructCreate(parse_PlayerTypeConversion);
	s_pConversion->bSideConvert = bSideConvert;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Login_SetConvertCharacterPaymentGAD);
void CharacterSelectionExpr_SetConvertCharacterPaymentGAD(int bPayWithGAD)
{
	if(!s_pConversion)
		s_pConversion = StructCreate(parse_PlayerTypeConversion);

	s_pConversion->bPayWithGADToken = bPayWithGAD;
}

//Returns the number of rename tokens this character has
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Login_RenameTokensAvailable");
S32 CharacterSelectionExpr_RenameTokensAvailable()
{
	if(GSM_IsStateActive(GCL_LOGIN))
    {
        GameAccountData *gameAccountData = GET_REF(g_characterSelectionData->hGameAccountData);
        if (gConf.bDontAllowGADModification)
            return gad_GetAccountValueInt(gameAccountData, MicroTrans_GetRenameTokensASKey());
        else
            return gad_GetAttribInt(gameAccountData, MicroTrans_GetRenameTokensGADKey());
    }
	return 0;
}

//Returns the number of rename tokens this character has
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Login_RetrainTokensAvailable");
S32 CharacterSelectionExpr_RetrainTokensAvailable()
{
	if(GSM_IsStateActive(GCL_LOGIN))
    {
        GameAccountData *gameAccountData = GET_REF(g_characterSelectionData->hGameAccountData);
		return( gad_GetAttribInt(gameAccountData, MicroTrans_GetRetrainTokensGADKey()) );
    }

	return 0;
}

// change characters name, specified by index.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Login_ChangeNameByContainerID");
void CharacterSelectionExpr_ChangeNameByContainerID(ContainerID iContainerID, bool bBadName, const char *pcNewName)
{
	S32 i;
    Login2CharacterChoice *characterChoice = NULL;
    Login2CharacterChoice *tmpChoice;
    bool conflictFound = false;
    char *pchMesg = NULL;

    if (GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_EXISTING))
    {
        for ( i = eaSize(&g_characterSelectionData->characterChoices->characterChoices) - 1; i >= 0; i-- )
        {
            tmpChoice = g_characterSelectionData->characterChoices->characterChoices[i];
            if ( tmpChoice->containerID == iContainerID )
            {
                // This is the choice we want to change the name of.
                characterChoice = tmpChoice;
            }
            else if ( stricmp(tmpChoice->savedName, pcNewName) == 0 )
            {
                // The new name conflicts with one of our other character names.
                conflictFound = true;
            }
        }

        if ( characterChoice && !conflictFound )
        {
            int iError = StringIsInvalidCharacterName(pcNewName, LoginGetAccessLevel());

            if(iError == STRINGERR_NONE)
            {
                if ( characterChoice->hasBadName == bBadName )
                {
                    gclLoginRequestChangeName(characterChoice->containerID, pcNewName, bBadName);
                }
            }
            else
            {
                StringCreateNameError(&pchMesg, iError);
                notify_NotifySend(NULL, kNotifyType_NameInvalid, pchMesg, NULL, NULL);
                estrDestroy(&pchMesg);
            }
        }
        else
        {
            FormatGameMessageKey(&pchMesg, "NameFormat_DuplicateName", STRFMT_END);
            notify_NotifySend(NULL, kNotifyType_NameInvalid, pchMesg, NULL, NULL);
            estrDestroy(&pchMesg);
        }
    }
}

const Login2CharacterChoice * GetRenamingCharacter(void)
{
	Login2CharacterChoice *pChoice = NULL;
	if (GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_EXISTING))
	{
		if (s_RenamingIndex >= 0 && s_RenamingIndex < eaSize(&g_characterSelectionData->characterChoices->characterChoices))
		{
			pChoice = g_characterSelectionData->characterChoices->characterChoices[s_RenamingIndex];
		}
		if(s_RenamingId != INVALID_RENAMINGID)
		{
			int i;
			for(i=eaSize(&g_characterSelectionData->characterChoices->characterChoices)-1; i>=0; i--)
			{
				if(g_characterSelectionData->characterChoices->characterChoices[i]->containerID == s_RenamingId)
				{
					pChoice = g_characterSelectionData->characterChoices->characterChoices[i];
					break;
				}
			}
		}
	}

	return pChoice;
}

// Mark the character ID the renaming process is going to use.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Login_SetRenamingId");
void exprRenamingId(ContainerID id)
{
	//Set the index to -1, we're going to use the ID if it's found in the choice list
	s_RenamingIndex = -1;
	s_RenamingId = INVALID_RENAMINGID;

	if( GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_EXISTING) &&
		g_characterSelectionData)
	{
		int i;
		for(i=eaSize(&g_characterSelectionData->characterChoices->characterChoices)-1; i>=0; i--)
		{
			if(g_characterSelectionData->characterChoices->characterChoices[i]->containerID == id)
			{
				//It was found in the list, use the ID
				s_RenamingId = id;
				break;
			}
		}
	}
}

// Return whether or not the given possible character choice has a bad name.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Login_IsBadNameById");
bool exprIsBadNameById(ContainerID id)
{
	if (GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_EXISTING))
	{
		int i;
		Login2CharacterChoice *pChoice = NULL;
		for(i=eaSize(&g_characterSelectionData->characterChoices->characterChoices)-1; i>=0; i--)
		{
			pChoice = g_characterSelectionData->characterChoices->characterChoices[i];
			if(pChoice->containerID == id)
			{
                return pChoice->hasBadName;
			}
		}
	}

	return false;
}


// Return a string displaying information about the current invalid chat handle.
// FIXME(tchao): Comment what this actually does.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("exprGetInvalidDisplayName");
const char *exprGetInvalidDisplayName(void)
{
	const char *name = gclLogin_GetInvalidDisplayName();
	return (name ? name : "");
}

// Return if a chat handle is valid for the current account.
AUTO_EXPR_FUNC(UIGen);
bool exprIsDisplayNameValid(const char *displayName)
{
	return StringIsValidDisplayName(displayName, LoginGetAccessLevel());
}

// Set a new chat handle.
AUTO_EXPR_FUNC(UIGen);
bool exprSetDisplayName(const char *displayName)
{
	gclLogin_ChangeDisplayName(displayName);
	return true;
}

// DEPRECATED: Use UIGenPaperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_SetTransparentBackground");
void CharacterSelectionExpr_SetTransparentBackground(bool bTransparent)
{
}

// Returns true if safe login is recommended for this character (the character failed to complete the last map transfer)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Login_IsSafeLoginRecommended");
bool LoginExpr_IsSafeLoginRecommended(ContainerID containerID)
{
    Login2CharacterChoice *characterChoice = CharacterSelection_GetChoiceByID(containerID);

    if ( characterChoice )
	{
		return characterChoice->isSaveLoginRecommended;
	}
	return false;
}

// Cached character choices for UI
static Login2CharacterChoice** s_eaChoicesCached = NULL;
static S32 s_iCurrentCachedCharacterChoiceIndex = 0;
static ContainerID s_iLastCachedVirtualShardID = 0;

// Builds the cached list of character choices
AUTO_EXPR_FUNC(UIGen);
void CharacterSelection_CacheCharacterChoicesForVirtualShard(ContainerID iVirtualShardID)
{
	s_iLastCachedVirtualShardID = iVirtualShardID;
	s_iCurrentCachedCharacterChoiceIndex = 0;

	if(s_eaChoicesCached == NULL)
	{
		eaCreate(&s_eaChoicesCached);
	}
	else
	{
		eaClear(&s_eaChoicesCached);
	}

	if(g_characterSelectionData && CharacterSelection_PlayerHasAccessToVirtualShard(iVirtualShardID))
	{
		int i;

		for (i=0; i < eaSize(&g_characterSelectionData->characterChoices->characterChoices); i++)
		{
            Login2CharacterChoice *characterChoice = g_characterSelectionData->characterChoices->characterChoices[i];
            if(characterChoice->virtualShardID == iVirtualShardID)
			{
                eaPush(&s_eaChoicesCached, characterChoice);
			}
		}
	}
}

// Returns the number of cached character choices
AUTO_EXPR_FUNC(UIGen);
S32 CharacterSelection_GetCountCachedCharacterChoicesForVirtualShard(ContainerID iVirtualShardID)
{
	if (s_iLastCachedVirtualShardID != iVirtualShardID)
	{
		CharacterSelection_CacheCharacterChoicesForVirtualShard(iVirtualShardID);
	}

	return eaSize(&s_eaChoicesCached);
}

// Returns the number of cached character choices
AUTO_EXPR_FUNC(UIGen);
S32 CharacterSelection_GetCurrentCachedCharacterChoiceIndexForVirtualShard(ContainerID iVirtualShardID)
{
	if (s_iLastCachedVirtualShardID != iVirtualShardID)
	{
		CharacterSelection_CacheCharacterChoicesForVirtualShard(iVirtualShardID);
	}

	return s_iCurrentCachedCharacterChoiceIndex;
}

// Indicates if the current character choice index is pointing to the last element
AUTO_EXPR_FUNC(UIGen);
bool CharacterSelection_PreviousCachedCharacterChoicesForVirtualShardExists(ContainerID iVirtualShardID)
{
	if (s_iLastCachedVirtualShardID != iVirtualShardID)
	{
		CharacterSelection_CacheCharacterChoicesForVirtualShard(iVirtualShardID);
	}

	return s_iCurrentCachedCharacterChoiceIndex - 1 >= 0;
}

// Decrements the character choice index to get the previous character choice
AUTO_EXPR_FUNC(UIGen);
void CharacterSelection_SetToCachedCharacterChoiceForVirtualShardByID(ContainerID iVirtualShardID, ContainerID iEntID)
{
	S32 i = 0;
	if (s_iLastCachedVirtualShardID != iVirtualShardID)
	{
		CharacterSelection_CacheCharacterChoicesForVirtualShard(iVirtualShardID);
	}

	for (; i < eaSize(&s_eaChoicesCached); i++)
	{
		if (s_eaChoicesCached[i]->virtualShardID == iEntID)
		{
			s_iCurrentCachedCharacterChoiceIndex = i;
		}
	}
}

// Decrements the character choice index to get the previous character choice
AUTO_EXPR_FUNC(UIGen);
void CharacterSelection_SetToPreviousCachedCharacterChoiceForVirtualShard(ContainerID iVirtualShardID)
{
	if (s_iLastCachedVirtualShardID != iVirtualShardID)
	{
		CharacterSelection_CacheCharacterChoicesForVirtualShard(iVirtualShardID);
	}

	if (s_iCurrentCachedCharacterChoiceIndex - 1 >= 0)
	{
		--s_iCurrentCachedCharacterChoiceIndex;
	}
}

// Indicates if the current character choice index is pointing to the last element
AUTO_EXPR_FUNC(UIGen);
bool CharacterSelection_NextCachedCharacterChoicesForVirtualShardExists(ContainerID iVirtualShardID)
{
	if (s_iLastCachedVirtualShardID != iVirtualShardID)
	{
		CharacterSelection_CacheCharacterChoicesForVirtualShard(iVirtualShardID);
	}
	return (s_iCurrentCachedCharacterChoiceIndex + 1) < eaSize(&s_eaChoicesCached);
}

// Increments the character choice index to get the next character choice
AUTO_EXPR_FUNC(UIGen);
void CharacterSelection_SetToNextCachedCharacterChoiceForVirtualShard(ContainerID iVirtualShardID)
{
	if (s_iLastCachedVirtualShardID != iVirtualShardID)
	{
		CharacterSelection_CacheCharacterChoicesForVirtualShard(iVirtualShardID);
	}

	if (s_iCurrentCachedCharacterChoiceIndex + 1 < eaSize(&s_eaChoicesCached))
	{
		++s_iCurrentCachedCharacterChoiceIndex;
	}
}

// Set GenData to an item.
AUTO_EXPR_FUNC(UIGen);
void CharacterSelection_GetCurrentCachedCharacterChoiceForVirtualShard(SA_PARAM_NN_VALID UIGen *pGen, ContainerID iVirtualShardID)
{
	if (s_iLastCachedVirtualShardID != iVirtualShardID)
	{
		CharacterSelection_CacheCharacterChoicesForVirtualShard(iVirtualShardID);
	}

	if (s_iCurrentCachedCharacterChoiceIndex >= 0 && s_iCurrentCachedCharacterChoiceIndex < eaSize(&s_eaChoicesCached))
	{
		ui_GenSetPointer(pGen, s_eaChoicesCached[s_iCurrentCachedCharacterChoiceIndex], parse_Login2CharacterChoice);
	}
	else
	{
		ui_GenSetPointer(pGen, NULL, parse_Login2CharacterChoice);
	}
}

static StashTable s_stSelectPlayerObservers;

static void gclCharacterSelectionCB_SelectPlayerById(ContainerID playerID, GCLLogin2FetchResult result, void *userData)
{
	Entity *playerEnt = gclLogin2_CharacterDetailCache_GetEntity(playerID);

	if (result != FetchResult_Succeeded)
		return;

	// Update selected Player pointer information

	g_CharacterSelectionPlayerId = playerID;

	if (playerEnt)
		ui_GenSetPointerVar("Player", playerEnt, parse_Entity);

	if (s_stSelectPlayerObservers)
	{
		FOR_EACH_IN_STASHTABLE(s_stSelectPlayerObservers, void, Iter);
		{
			const char *pchGenName = stashElementGetKey(eIterElem);
			const char *pchMessage = stashElementGetPointer(eIterElem);
			UIGen *pGen = RefSystem_ReferentFromString("UIGen", pchGenName);
			UIGenAction *pMessage = pGen ? ui_GenFindMessage(pGen, pchMessage) : NULL;

			// Queue the message for running the next time it's safe
			if (pMessage)
				eaPushUnique(&pGen->eaTransitions, pMessage);
		}
		FOR_EACH_END;
	}
}

// Add an observer for whenever the active player entity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_AddSelectedPlayerObserver");
ExprFuncReturnVal gclCharacterSelectionExpr_AddSelectedPlayerObserver(SA_PARAM_NN_VALID UIGen *pGen, const char *pchMessage, ACMD_EXPR_ERRSTRING err)
{
	if (!ui_GenInDictionary(pGen))
	{
		estrPrintf(err, "UIGen '%s' is not in Dictionary (CharacterSelection_AddSelectedPlayerObserver can't be used with InlineChildren or Templates)", pGen->pchName);
		return ExprFuncReturnError;
	}

	if (!s_stSelectPlayerObservers)
		s_stSelectPlayerObservers = stashTableCreateAddress(16);

	stashAddressAddPointer(s_stSelectPlayerObservers, pGen->pchName, (char *)allocAddString(pchMessage), true);
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_RemoveSelectedPlayerObserver");
void gclCharacterSelectionExpr_RemoveSelectedPlayerObserver(SA_PARAM_NN_VALID UIGen *pGen)
{
	if (s_stSelectPlayerObservers)
	{
		char *pchIgnored;
		stashAddressRemovePointer(s_stSelectPlayerObservers, pGen->pchName, &pchIgnored);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_SelectPlayerById");
void gclCharacterSelectionExpr_SelectPlayerById(U32 playerID)
{
	Entity *playerEnt = gclLogin2_CharacterDetailCache_GetEntity(playerID);
	if (playerEnt)
		gclCharacterSelectionCB_SelectPlayerById(playerID, FetchResult_Succeeded, NULL);
	else
		gclLogin2_CharacterDetailCache_Fetch(playerID, gclCharacterSelectionCB_SelectPlayerById, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_SelectPlayerByCharacterChoice");
void gclCharacterSelectionExpr_SelectPlayerByCharacterChoice(Login2CharacterChoice *characterChoice)
{
	gclCharacterSelectionExpr_SelectPlayerById(SAFE_MEMBER(characterChoice, containerID));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetPuppetNameByClassType");
const char *CharacterSelection_GetPuppetNameByClassType(U32 uContainerID, const char *pchClassType)
{
	Entity *playerEnt = gclLogin2_CharacterDetailCache_GetEntity(uContainerID);
	Entity *subEnt = gclLogin2_CharacterDetailCache_GetPuppet(uContainerID, StaticDefineIntGetInt(CharClassTypesEnum, pchClassType));

	if (subEnt && subEnt->pSaved)
		return subEnt->pSaved->savedName;
	if (playerEnt && playerEnt->pSaved)
		return playerEnt->pSaved->savedName;
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetPuppetClassNameByClassType");
const char *CharacterSelection_GetPuppetClassByClassType(U32 uContainerID, const char *pchClassType)
{
	Entity *playerEnt = gclLogin2_CharacterDetailCache_GetEntity(uContainerID);
	Entity *subEnt = gclLogin2_CharacterDetailCache_GetPuppet(uContainerID, StaticDefineIntGetInt(CharClassTypesEnum, pchClassType));
	CharacterClass *charClass = NULL;

	if (subEnt && subEnt->pChar) 
		charClass = character_GetClassCurrent(subEnt->pChar);
	if (!charClass && playerEnt && playerEnt->pChar)
		charClass = character_GetClassCurrent(playerEnt->pChar);
	return charClass ? TranslateDisplayMessage(charClass->msgDisplayName) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterSelection_GetSelectionDataVersion");
U32 CharacterSelection_GetSelectionDataVersion(void)
{
	return g_characterSelectionDataVersionNumber;
}

#include "Autogen/CharacterSelection_h_ast.c"
