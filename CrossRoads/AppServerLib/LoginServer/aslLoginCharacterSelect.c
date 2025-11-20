/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLoginServer.h"
#include "aslLoginCharacterSelect.h"
#include "aslLoginEntityMigrate.h"
#include "aslLoginUGCProject.h"
#include "objTransactions.h"
#include "net/net.h"
#include "netipfilter.h"
#include "LoginCommon.h"
#include "AutoGen/LoginCommon_h_ast.h"
#include "structnet.h"
#include "AutoGen/ServerLib_autogen_remotefuncs.h"
#include "AutoGen/Controller_autogen_remotefuncs.h"
#include "AutoGen/GameServerLib_autogen_remotefuncs.h"
#include "AutoGen/ObjectDB_autogen_remotefuncs.h"
#include "Autogen/AppServerLib_autogen_remotefuncs.h"
#include "logcomm.h"
#include "Entity.h"
#include "EntityLib.h"
#include "EntityBuild.h"
#include "EntityExtern.h"
#include "entCritter.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/EntityBuild_h_ast.h"
#include "file.h"
#include "InstancedStateMachine.h"
#include "Powers.h"
#include "PowerSlots_h_ast.h"
#include "PowerTree.h"
#include "Tray.h"
#include "Character.h"
#include "CostumeCommonEntity.h"
#include "AutoGen/Character_h_ast.h"
#include "AutoGen/StatPoints_h_ast.h"
#include "AutoGen/CostumeCommonEnums_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"
#include "inventoryCommon.h"
#include "AutoGen/PowerTree_h_ast.h"
#include "PowerTreeHelpers.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "CharacterClass.h"
#include "CharacterAttribs.h"
#include "UtilitiesLib.h"
#include "ResourceInfo.h"
#include "StringCache.h"
#include "StringFormat.h"
#include "Expression.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "SavedPetTransactions.h"
#include "StringUtil.h"
#include "serverLib.h"
#include "microtransactions_common.h"
#include "autogen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "autogen/ServerLib_autotransactions_autogen_wrappers.h"
#include "ContinuousBuilderSupport.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "PlayerBooter.h"
#include "timing.h"
#include "aslLoginCharacterSelect_h_ast.h"
#include "sock.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountData_h_ast.h"
#include "GameAccountDataCommon.h"
#include "GamePermissionsCommon.h"
#include "GamePermissionsCommon_h_ast.h"
#include "GamePermissionTransactions.h"
#include "zutils.h"
#include "SavedPetCommon.h"
#include "species_common.h"
#include "virtualShard.h"
#include "alerts.h"
#include "notifycommon.h"
#include "referencesystem.h"
#include "MicroTransactions.h"
#include "TimedCallback.h"
#include "progression_common.h"
#include "AutoGen/progression_common_h_ast.h"
#include "powervars.h"
#include "textparser.h"
#include "textparserJSON.h"
#include "LoggedTransactions.h"
#include "ShardCommon.h"
#include "Login2ServerCommon.h"
#include "aslLogin2_StateMachine.h"
#include "accountnet.h"
#include "AutoGen/accountnet_h_ast.h"
#include "AccountProxyCommon.h"

#include "LoadScreen\LoadScreen_Common.h"
#include "AutoGen/LoadScreen_Common_h_ast.h"

static bool sbEveryoneCanAccessUGCShard = true;
AUTO_CMD_INT(sbEveryoneCanAccessUGCShard, EveryoneCanAccessUGCShard);


static bool sbEveryoneCanAccessAllVShards = false;
AUTO_CMD_INT(sbEveryoneCanAccessAllVShards, EveryoneCanAccessAllVShards);



static ContainerID siVirtualShardForNewChars = 0;
AUTO_CMD_INT(siVirtualShardForNewChars, VirtualShardForNewChars);

static S32 allowDuplicateAccountLogins;
AUTO_CMD_INT(allowDuplicateAccountLogins, allowDuplicateAccountLogins);

//if true, then characters can edit UGC iff they are on the UGC virtual shard
static bool gbUGCAccessDependsOnVirtualShard = true;
AUTO_CMD_INT(gbUGCAccessDependsOnVirtualShard, UGCAccessDependsOnVirtualShard);

AUTO_STARTUP(AnimLists);
void aslLoginServerFakeAnimListStartup(void)
{
	//fake auto startup here since inventory does validation on anim lists
}

//
// Utility functions for checking virtual shards
//
VirtualShard* aslGetVirtualShardByID(U32 iVShardContainerID)
{
	char idString[16];
	sprintf(idString, "%u", iVShardContainerID);
	return RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(GLOBALTYPE_VIRTUALSHARD), idString);
}

bool aslIsVirtualShardEnabled(int clientAccessLevel, VirtualShard *pShard)
{
	if (clientAccessLevel < ACCESS_GM_FULL)
	{
		return (pShard && !pShard->bDisabled);
	}
	return true;
}

bool aslIsVirtualShardEnabledByID(int clientAccessLevel, U32 iVShardContainerID)
{
    // virtual shard is always enabled for players AL 7 or greater
    if (clientAccessLevel < ACCESS_GM_FULL && iVShardContainerID != 0)
	{
		return aslIsVirtualShardEnabled(clientAccessLevel, aslGetVirtualShardByID(iVShardContainerID));
	}
	return true;
}

// Currently only used for UGC shards, this returns true if the shard is available to the player
bool aslIsVirtualShardAvailable(int clientAccessLevel, bool hasUGCProjectSlots, VirtualShard *pShard)
{
	if (!pShard)
	{
		return false;
	}
	if (pShard->bUGCShard)
	{
		if (clientAccessLevel == 0 && !hasUGCProjectSlots)
		{
			return false;
		}
	}
	return true;
}

bool aslIsVirtualShardAvailableByID(int clientAccessLevel, bool hasUGCProjectSlots, U32 iVShardContainerID)
{
	if (iVShardContainerID)
	{
		return aslIsVirtualShardAvailable(clientAccessLevel, hasUGCProjectSlots, aslGetVirtualShardByID(iVShardContainerID));
	}
	return true;
}

bool aslCanPlayerAccessVirtualShard(CONST_EARRAY_OF(AccountProxyKeyValueInfoContainer) accountKeyValues, U32 iVShardContainerID)
{
	char *pNotAllowed;
	char *pAllowed;
	VirtualShard *pShard;

    if (!accountKeyValues  || isDevelopmentMode())
	{
		return true;
	}

	if (sbEveryoneCanAccessAllVShards)
	{
		return true;
	}

	if (iVShardContainerID == 0)
	{
		pNotAllowed = AccountProxyFindValueFromKeyContainer(accountKeyValues, GetAccountVShardNotAllowedKey(iVShardContainerID));
		if (pNotAllowed)
		{
			ANALYSIS_ASSUME(pNotAllowed);
			if (atoi(pNotAllowed))
			{
				return false;
			}
		}
		return true;
	}

	pShard = aslGetVirtualShardByID(iVShardContainerID);
	if (!pShard)
	{
		return false;
	}

	if (sbEveryoneCanAccessUGCShard && pShard->bUGCShard)
	{
		return true;
	}



    pNotAllowed = AccountProxyFindValueFromKeyContainer(accountKeyValues, GetAccountVShardNotAllowedKey(iVShardContainerID));
	if (pNotAllowed)
	{
		ANALYSIS_ASSUME(pNotAllowed != NULL);
		if (atoi(pNotAllowed))
			return false;
	}

	pAllowed =  AccountProxyFindValueFromKeyContainer(accountKeyValues, GetAccountVShardAllowedKey(iVShardContainerID));

	if (pAllowed)
	{
		ANALYSIS_ASSUME(pAllowed);
		if (atoi(pAllowed))
		{
			return true;
		}
	}

	return false;
}

//
// Functions for character slots.
//
static int NumUnrestrictedCharSlots(GameAccountData *pGameAccount, int clientAccessLevel)
{
    int iNumSlots = 0;
    char *pKeyValue = NULL;
    U32 iDaysSubscribed = 0;
    int iAccountServerSlots = 0;
    int iGameAccountSlots = 0;
	int iCSRGameAccountSlots = 0;
    bool bLifetime = false;

    // Gamepermission slots
    if(gamePermission_Enabled())
    {
        if(!GetGamePermissionValueUncached(pGameAccount, GAME_PERMISSION_CHARACTER_SLOTS, &iNumSlots))
        {
            iNumSlots = 0;
        }
    }
    else
    {
        iNumSlots = gProjectLoginServerConfig.iMaximumNumOfCharacters;
    }

    // bonus for subscribers
    if(pGameAccount)
    {
        iDaysSubscribed = pGameAccount->iDaysSubscribed;
        bLifetime = pGameAccount->bLifetimeSubscription;
    }

    //Sets the amount of bonus slots based on days subscribed
    {
        S32 i;
        U32 iBestBonus = 0;

        for(i = 0; i < eaSize(&gProjectLoginServerConfig.pBonusSubscriberCharacterSlots); ++i)
        {
            SubscriberCharacterSlotBonus *bonusDef = gProjectLoginServerConfig.pBonusSubscriberCharacterSlots[i];

            if( ( ( iDaysSubscribed >= bonusDef->iCharacterSlotBonusDays ) || ( bLifetime && bonusDef->bGrantForLifetime ) ) &&
                bonusDef->iCharacterSlotBonus > iBestBonus)
            {
                iBestBonus = bonusDef->iCharacterSlotBonus;			
            }
        }

        iNumSlots += iBestBonus;
    }

    // Add extra slots for AccessLevel 9
    if ( clientAccessLevel >= 9 )
    {
        iNumSlots += gProjectLoginServerConfig.iBonusCharactersAccessLevel9; //add more characters for access-level 9
    }

    // Add slots from GameAccountData key/value
    iGameAccountSlots = gad_GetAttribInt(pGameAccount, MicroTrans_GetCharSlotsGADKey());

    if( iGameAccountSlots > 0 )
    {
        iNumSlots += iGameAccountSlots;
    }

	// Add CSR granted slots from GameAccountData key/valu
	iCSRGameAccountSlots = gad_GetAttribInt(pGameAccount, MicroTrans_GetCSRCharSlotsGADKey());
	if( iCSRGameAccountSlots > 0 )
	{
		iNumSlots += iCSRGameAccountSlots;
	}

    // Add slots from account server key/value
    pKeyValue = AccountProxyFindValueFromKeyContainer(pGameAccount->eaAccountKeyValues, MicroTrans_GetCharSlotsGADKey());

	if (pKeyValue)
	{
		iAccountServerSlots = atoi(pKeyValue);
	}
	
    if ( iAccountServerSlots > 0 )
    {
        iNumSlots += iAccountServerSlots;
    }

	iAccountServerSlots = 0;
	pKeyValue = AccountProxyFindValueFromKeyContainer(pGameAccount->eaAccountKeyValues,
		AccountProxySubstituteKeyTokens(MicroTrans_GetCharSlotsASKey(), AccountGetShardProxyName(), ShardCommon_GetClusterName(), microtrans_GetShardEnvironmentName()));

	if (pKeyValue)
	{
		iAccountServerSlots = atoi(pKeyValue);
	}

	if ( iAccountServerSlots > 0 )
	{
		iNumSlots += iAccountServerSlots;
	}

    // Builders are weird.  Make sure they have a character slot.
    if ( iNumSlots == 0 && g_isContinuousBuilder )
    {
        iNumSlots = 1;
    }

    return iNumSlots;
}

static void
AddVirtualShardCharacterSlots(AvailableCharSlots *availableCharSlots, GameAccountData *pGameAccount)
{
    VirtualShard *pVirtualShard;
    RefDictIterator iterator;

    if ( pGameAccount == NULL || availableCharSlots == NULL )
    {
        return;
    }

    RefSystem_InitRefDictIterator(GlobalTypeToCopyDictionaryName(GLOBALTYPE_VIRTUALSHARD), &iterator);

    while ((pVirtualShard = RefSystem_GetNextReferentFromIterator(&iterator)))
    {
        if (aslCanPlayerAccessVirtualShard(pGameAccount->eaAccountKeyValues, pVirtualShard->id))
        {
            int slots = 0;
            int iGameAccountSlots = gad_GetAttribInt(pGameAccount, MicroTrans_GetVirtualShardCharSlotsGADKey(pVirtualShard->id));
            char *pKeyValue = AccountProxyFindValueFromKeyContainer(pGameAccount->eaAccountKeyValues, MicroTrans_GetVirtualShardCharSlotsGADKey(pVirtualShard->id));

			if (pKeyValue)
			{
				ANALYSIS_ASSUME(pKeyValue != NULL);
				if (atoi(pKeyValue) > 0)
				{
					slots = atoi(pKeyValue);
				}
			}

            if(iGameAccountSlots > 0)
            {
                slots += iGameAccountSlots;
            }

            if(pVirtualShard->bUGCShard)
            {
                if ( gamePermission_Enabled() )
                {
                    // Get the number of free UGC character slots from game permissions
                    S32 value;
                    if( GetGamePermissionValueUncached(pGameAccount, GAME_PERMISSION_UGC_FREE_CHAR_SLOTS, &value) )
                    {
                        slots += value;
                    }
                }
                else
                {
                    // UGC virtual shards get one free character slot if we are not using game permissions
                    slots++;
                }
            }

            if ( slots > 0 )
            {
                // add the virtual shard restricted slots
                CharSlots_AddSlots(availableCharSlots, slots, CharSlotRestrictFlag_VirtualShard, pVirtualShard->id, NULL);
            }
        }
    }

    return;
}

static void
AddAllegianceCharacterSlots(AvailableCharSlots *availableCharSlots, GameAccountData *pGameAccount)
{
    int i;
    static const char *allegianceCharSlotTypeName = NULL;
    static char *allegianceName = NULL;
    static char *tokenType = NULL;
    S32 value;

    if ( pGameAccount == NULL || availableCharSlots == NULL )
    {
        return;
    }

    if ( allegianceCharSlotTypeName == NULL )
    {
         allegianceCharSlotTypeName = StaticDefineIntRevLookup(GameTokenTypeEnum, kGameToken_AllegianceCharSlots);
    }

    if ( allegianceCharSlotTypeName == NULL )
    {
        return;
    }

    for ( i = 0; i < eaSize(&pGameAccount->eaTokens); i++ )
    {
        // extract type, name and value from the token
        if ( gamePermissions_GetNameKeyValue(pGameAccount->eaTokens[i], &tokenType, &allegianceName, &value) )
        {
            // match only AllegianceCharSlots tokens
            if ( value > 0 && stricmp_safe(allegianceCharSlotTypeName, tokenType) == 0 )
            {
                AllegianceDef *allegianceDef = allegiance_FindByName(allegianceName);

                if ( allegianceDef )
                {
                    // allegiance is valid, so add the slots
                    CharSlots_AddSlots(availableCharSlots, value, CharSlotRestrictFlag_Allegiance, 0, allegianceDef->pcName);
                }
                else
                {
                    // the allegiance specified in the token could not be found, so generate an error
                    Errorf("%s: invalid allegiance for allegiance restricted character slot gamepermission token: %s", __FUNCTION__, pGameAccount->eaTokens[i]->pchKey);
                }
            }
        }
    }
}

AvailableCharSlots *
BuildAvailableCharacterSlots(GameAccountData *pGameAccount, int clientAccessLevel)
{
    AvailableCharSlots *availableCharSlots = StructCreate(parse_AvailableCharSlots);

    // compute the number of unrestricted slots
    availableCharSlots->numUnrestrictedSlots = NumUnrestrictedCharSlots(pGameAccount, clientAccessLevel);
    availableCharSlots->numTotalSlots = availableCharSlots->numUnrestrictedSlots;

    // add virtual shard restricted slots
    AddVirtualShardCharacterSlots(availableCharSlots, pGameAccount);

    // add allegiance restricted slots
    AddAllegianceCharacterSlots(availableCharSlots, pGameAccount);

    return availableCharSlots;
}

void
RemoveUsedCharacterSlots(PossibleCharacterChoices *possibleCharacterChoices)
{
    int i;

    for ( i = 0; i < eaSize(&possibleCharacterChoices->ppChoices); i++ )
    {
        PossibleCharacterChoice *possibleCharacterChoice = possibleCharacterChoices->ppChoices[i];

        if ( !CharSlots_MatchSlot(possibleCharacterChoices->pAvailableCharSlots, possibleCharacterChoice->iVirtualShardID, possibleCharacterChoice->pcAllegianceName, true) )
        {
            ErrorDetailsf("%s: characterID=%u, virtualShardID=%d, allegiance=%s", possibleCharacterChoice->name, possibleCharacterChoice->iID, possibleCharacterChoice->iVirtualShardID, possibleCharacterChoice->pcAllegianceName);
            Errorf("%s: Could not find character slot for existing character", __FUNCTION__);
        }
    }
}

// Handles project-specific part of Character PlayerType conversion, should return
//  false if something went wrong.
S32 DEFAULT_LATELINK_login_ConvertCharacterPlayerType(Entity *pEnt, int accountPlayerType, TransactionReturnCallback cbFunc, void *cbData)
{
	// Not valid by default
	return false;
}

S32 DEFAULT_LATELINK_login_updateCharactersMapType(LoginLink *loginLink, Entity *pEnt)
{
	return false;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Playertype");
enumTransactionOutcome asl_trh_ValidPlayerTypeConversion(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, PlayerType newPlayerType)
{

	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
		TRANSACTION_RETURN_LOG_FAILURE("Null ent or player");

	if(newPlayerType == kPlayerType_None)
		TRANSACTION_RETURN_LOG_FAILURE("New PlayerType is None");

	if(newPlayerType == pEnt->pPlayer->playerType)
		TRANSACTION_RETURN_LOG_FAILURE("New PlayerType same as old PlayerType");

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Default callbacks

// Modifies the login cookie on the character
enumTransactionOutcome SetLoginCookieCB(ATR_ARGS, NOCONST(Entity) *newPlayer, NOCONST(Entity) *backupPlayer, GlobalType locationType, ContainerID locationID)
{
	Login2State *loginState;

    if ( newPlayer->pPlayer == NULL )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    loginState = aslLogin2_GetActiveLoginStateByAccountID(newPlayer->pPlayer->accountID);
	objSetDebugName(newPlayer->debugName, MAX_NAME_LEN,
		newPlayer->myEntityType,
		newPlayer->myContainerID, newPlayer->pPlayer->accountID,
		newPlayer->pSaved ? newPlayer->pSaved->savedName : NULL, newPlayer->pPlayer->publicAccountName);

	if (!loginState)
	{
		// This means we're a new character
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	newPlayer->pPlayer->loginCookie = Login2_ShortenToken(loginState->loginCookie);

	return TRANSACTION_OUTCOME_SUCCESS;
}

static bool basicInit(ContainerID accountID, U32 loginCookie, const char *characterName, const char *accountName, const char *displayName, const char *subName, PlayerType playerType, PossibleCharacterChoice *chosenCharacter, NOCONST(Entity) *ent)
{
	ent->myEntityType = GLOBALTYPE_ENTITYPLAYER;

	objSetDebugName(ent->debugName, MAX_NAME_LEN,
		ent->myEntityType,
		0, accountID,
		characterName, displayName);

	copyVec3(zerovec3, ent->pos_use_accessor);
	copyQuat(unitquat, ent->rot_use_accessor);
	ent->pPlayer = StructCreateNoConst(parse_Player);

	//Setup the character and the return pointer.  I'm surprised this hadn't crashed before
	ent->pChar = StructCreateNoConst(parse_Character);
	ent->pChar->pEntParent = (Entity*)ent;

	ent->pSaved = StructCreateNoConst(parse_SavedEntityData);
	ent->pTeam = StructCreateNoConst(parse_PlayerTeam);
	if (gConf.bDefaultToOpenTeaming)
	{
		ent->pTeam->eMode = TeamMode_Open;
	}
	if (chosenCharacter && eaSize(&chosenCharacter->eaPuppetInfo)==0)
	{
		ent->pSaved->pTray = StructCreateNoConst(parse_SavedTray);
	}

	strcpy(ent->pSaved->savedName, characterName);
	if (*subName)
	{
		ent->pSaved->savedSubName = StructAllocString(subName);
	}
	ent->pPlayer->playerFlags = PLAYERFLAG_NEW_CHARACTER;

	if (isDevelopmentMode() || gLoginServerState.bIsTestingMode)
	{
		ent->pPlayer->accessLevel = ACCESS_DEBUG;
	}
	else
	{
		ent->pPlayer->accessLevel = ACCESS_USER;
	}
	ent->pPlayer->loginCookie = loginCookie;
	strcpy(ent->pPlayer->privateAccountName, accountName);

	ent->pPlayer->playerType = playerType;
	ent->pPlayer->accountID = accountID;

	return 1;
}



int DEFAULT_LATELINK_player_BuildCreate(NOCONST(Entity) *ent)
{
	char* pBuildName = NULL;
	NOCONST(EntityBuild) *pBuild = CONTAINER_NOCONST(EntityBuild, StructAlloc(parse_EntityBuild));

	COPY_HANDLE(pBuild->hClass,ent->pChar->hClass);
	inv_ent_trh_BuildFill(ATR_EMPTY_ARGS,ent,false,pBuild);
	ent->pChar->pSlots = StructCreateNoConst(parse_CharacterPowerSlots);
	eaPush(&ent->pChar->pSlots->ppSets,StructCreateNoConst(parse_PowerSlotSet));

	eaPush(&ent->pSaved->ppBuilds,pBuild);

	// Give the build a default name
	estrStackCreate(&pBuildName); 
	FormatMessageKey(&pBuildName, "DefaultBuild", STRFMT_INT("Index", 1), STRFMT_END);
	strncpy(pBuild->achName,pBuildName,MAX_NAME_LEN_ENTITYBUILD-1);
	estrDestroy(&pBuildName);
	return true;
}


bool DEFAULT_LATELINK_gameSpecific_PreInitNewCharacter(NOCONST(Entity) *ent, Login2CharacterCreationData* pChoice, GameAccountDataExtract* pExtract)
{
	return true;
}
bool DEFAULT_LATELINK_gameSpecific_PostInitNewCharacter(NOCONST(Entity) *ent, Login2CharacterCreationData* pChoice, GameAccountDataExtract* pExtract)
{
	return true;
}

void aslGetCharacterCreationData(GameAccountData *gameAccountData, PlayerType playerType, ContainerID accountID, CharacterCreationDataHolder *holder)
{	
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct("PowerTreeDef");
	S32 i;

	for (i = 0; i < eaSize(&pArray->ppReferents); i++)
	{
		CharacterCreationRef *newRef = StructCreate(parse_CharacterCreationRef);
		SET_HANDLE_FROM_REFERENT("PowerTreeDef", pArray->ppReferents[i], newRef->hPowerTreeDef);
		eaPush(&holder->ppRefs, newRef);
	}

	//TODO(BH): Should this follow the conventions in characterClass.c?
	pArray = resDictGetEArrayStruct("CharacterClass");
	for (i = 0; i < eaSize(&pArray->ppReferents); i++)
	{
		CharacterClass *pClass = pArray->ppReferents[i];
		if (pClass->bPlayerClass)
		{
			CharacterCreationRef *newRef = StructCreate(parse_CharacterCreationRef);
			SET_HANDLE_FROM_REFERENT("CharacterClass", pClass, newRef->hClass);
			eaPush(&holder->ppRefs, newRef);
		}
	}

	pArray = resDictGetEArrayStruct("PetDef");
	for (i=0; i<eaSize(&pArray->ppReferents); i++)
	{
		PetDef *pPetDef = pArray->ppReferents[i];
		if(pPetDef)
		{
			CharacterCreationRef *newRef = StructCreate(parse_CharacterCreationRef);
			SET_HANDLE_FROM_REFERENT("PetDef", pPetDef, newRef->hPet);
			eaPush(&holder->ppRefs, newRef);
		}
	}

	pArray = resDictGetEArrayStruct("Species");
	for (i=0; i<eaSize(&pArray->ppReferents); i++)
	{
		SpeciesDef *pSpeciesDef = pArray->ppReferents[i];
		if(pSpeciesDef)
		{
			CharacterCreationRef *newRef = StructCreate(parse_CharacterCreationRef);
			SET_HANDLE_FROM_REFERENT("Species", pSpeciesDef, newRef->hPet);
			eaPush(&holder->ppRefs, newRef);
		}
	}

	if(gameAccountData)
	{
		//Unlock costumes that are for this game, of the costume type and have an item found with costumes on it
		for(i=0; i<eaSize(&gameAccountData->eaKeys);i++)
		{
			AttribValuePair *pPair = eaGet(&gameAccountData->eaKeys, i);
			if(pPair->pchValue && atoi(pPair->pchValue) > 0)
			{
				char *pchItem = estrStackCreateFromStr(pPair->pchAttribute);
				char *pchGameTitle = NULL;
				MicroItemType eType = kMicroItemType_None;
				char *pchItemName = NULL;

				if(!MicroTrans_TokenizeItemID(pchItem, &pchGameTitle, &eType, &pchItemName))
				{
					estrDestroy(&pchItem);
					continue;
				}

				if(	pchGameTitle &&
					eType == kMicroItemType_Costume &&
					pchItemName &&
					!stricmp(GetShortProductName(), pchGameTitle) )
				{
					ItemDef *pItemDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict, pchItemName);
					if(pItemDef)
					{
						S32 iIdx;
						for(iIdx = eaSize(&pItemDef->ppCostumes)-1; iIdx >= 0; iIdx--)
						{
							const char *pchCostumeRefString = REF_STRING_FROM_HANDLE(pItemDef->ppCostumes[iIdx]->hCostumeRef);

							if(pchCostumeRefString && pchCostumeRefString[0])
							{
								CharacterCreationRef *newRef = StructCreate(parse_CharacterCreationRef);
								COPY_HANDLE(newRef->hCostume, pItemDef->ppCostumes[iIdx]->hCostumeRef);
								eaPush(&holder->ppRefs, newRef);
							}
						}
					}
				}
				estrDestroy(&pchItem);
			}
		}

		//Unlock the costume keys
		for(i=0; i<eaSize(&gameAccountData->eaCostumeKeys);i++)
		{
			AttribValuePair *pPair = eaGet(&gameAccountData->eaCostumeKeys, i);
			if(pPair->pchValue)
			{
				char *pchItem = estrStackCreateFromStr(pPair->pchAttribute);
				char *pchGameTitle = NULL;
				MicroItemType eType = kMicroItemType_None;
				char *pchCostumeName = NULL;

				if(!MicroTrans_TokenizeItemID(pchItem, &pchGameTitle, &eType, &pchCostumeName))
				{
					estrDestroy(&pchItem);
					continue;
				}

				if(	pchGameTitle &&
					eType == kMicroItemType_PlayerCostume &&
					pchCostumeName &&
					!stricmp(GetShortProductName(), pchGameTitle) )
				{
					PlayerCostume *pCostume = (PlayerCostume*)RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchCostumeName);
					if(pCostume)
					{
						CharacterCreationRef *newRef = StructCreate(parse_CharacterCreationRef);
						SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, pCostume->pcName, newRef->hCostume);
						eaPush(&holder->ppRefs, newRef);
					}
				}
				estrDestroy(&pchItem);
			}
		}
	}

	pArray = resDictGetEArrayStruct("CostumeSet");

	//check to see if they've unlocked any costume sets (account-wide)
	if(accountID && eaSize(&pArray->ppReferents))
	{
		//Create a dummy entity initialized with the basics
		NOCONST(Entity) *pEnt = StructCreateWithComment(parse_Entity, "DummyCharacter for CostumeSets on LoginServer.");
		Entity *pConstEnt = (Entity*)pEnt;
		basicInit(accountID, 0, "", "", "", "", playerType, NULL, pEnt);
		
		for (i=0; i<eaSize(&pArray->ppReferents); i++)
		{
			PCCostumeSet *pSetDef = pArray->ppReferents[i];
			if(pSetDef && pSetDef->pExprUnlock && costumeEntity_EvaluateExpr(pConstEnt, pConstEnt, pSetDef->pExprUnlock))
			{
				int iCostumeIdx;
				for(iCostumeIdx=eaSize(&pSetDef->eaPlayerCostumes)-1; iCostumeIdx >= 0; iCostumeIdx--)
				{
					CharacterCreationRef *newRef = StructCreate(parse_CharacterCreationRef);
					COPY_HANDLE( newRef->hCostume, pSetDef->eaPlayerCostumes[iCostumeIdx]->hPlayerCostume );
					eaPush(&holder->ppRefs, newRef);
				}
			}
		}

		StructDestroyNoConst(parse_Entity, pEnt);
	}

	return;
}

// Games have the opportunity to do extra initialization on GameAccountData that is being created
GameAccountData *DEFAULT_LATELINK_gameSpecific_GameAccountDataCreateInit(void)
{
    return NULL;
}

// LOGIN2TODO - move these to login2 if still needed
AUTO_RUN;
void RegisterCharacterLoginStates(void)
{
	objRegisterSpecialTransactionCallback(GLOBALTYPE_ENTITYPLAYER, TRANSACTION_RECEIVE_CONTAINER_FROM, SetLoginCookieCB);
}

// LOGIN2TODO - port rename code to login2
typedef struct SetNameCBStruct
{
	int loginCookie;
	bool bBadName;
} SetNameCBStruct;

// Name set callback
void aslSetName_CB(TransactionReturnVal *pReturn, void *pData)
{
	SetNameCBStruct *pCBStruct = (SetNameCBStruct*)pData;
	LoginLink *loginLink = aslFindLoginLinkForCookie(pCBStruct->loginCookie);

	if (!loginLink || loginLink->bFailedLogin)
	{
		log_printf(LOG_LOGIN, "Command %s returned for invalid login id %d", __FUNCTION__, pCBStruct->loginCookie);
		SAFE_FREE(pCBStruct);
		return;	
	}

	if (!ISM_IsStateActive(LOGIN_STATE_MACHINE, loginLink, LOGINSTATE_SELECTING_CHARACTER))
	{
		// It's okay if we get this out of turn
		SAFE_FREE(pCBStruct);
		return;
	}

	switch(pReturn->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		{
			aslNotifyLogin(loginLink, langTranslateMessageKey(loginLink->clientLangID, "LoginServer_RenameFailed"), kNotifyType_Failed, NULL);
			
			SAFE_FREE(pCBStruct);
			return;
		}
	case TRANSACTION_OUTCOME_SUCCESS:
		{
            //aslLoginRequestCharacterChoices(loginLink);
			break;
		}
	}
	SAFE_FREE(pCBStruct);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Bbadname, .Psaved.Savedname, .Psaved.Esoldname")
ATR_LOCKS(pPuppetEnt, ".Psaved.Savedname");
bool asl_trh_aslSetName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(Entity)* pPuppetEnt,
						const char *pcNewName, bool bBadName)
{
	if( NONNULL(pEnt) &&
		NONNULL(pEnt->pSaved) &&
		strcmp(pEnt->pSaved->savedName, pcNewName) &&
		(!bBadName || pEnt->pSaved->bBadName) )
	{
		strcpy(pEnt->pSaved->savedName, pcNewName);
		if (NONNULL(pPuppetEnt) && NONNULL(pPuppetEnt->pSaved))
		{
			strcpy(pPuppetEnt->pSaved->savedName, pcNewName);
		}
		pEnt->pSaved->bBadName = false;
		estrDestroy(&pEnt->pSaved->esOldName);

		return(true);
	}
	else
	{
		return(false);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Accountid, .Psaved.Bbadname, .Psaved.Savedname, .Psaved.Esoldname")
ATR_LOCKS(pPuppetEnt, ".Psaved.Savedname")
ATR_LOCKS(pData, ".Eakeys")
ATR_LOCKS(pLockContainer, ".Plock.Result, .Plock.Uaccountid, .Plock.Fdestroytime, .Plock.Etransactiontype, .Plock.Pkey");
enumTransactionOutcome asl_tr_SetNameWithCost(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Entity)* pPuppetEnt,
											  NOCONST(GameAccountData) *pData, NOCONST(AccountProxyLockContainer) *pLockContainer, const char *pKey, const char *pcNewName)
{
	if(NONNULL(pEnt) && NONNULL(pcNewName) && NONNULL(pEnt->pSaved))
	{
        if(gConf.bDontAllowGADModification)
        {
            if(ISNULL(pLockContainer) || !APFinalizeKeyValue(pLockContainer, pEnt->pPlayer->accountID, pKey, APRESULT_COMMIT, TransLogType_Other))
            {
                TRANSACTION_RETURN_LOG_FAILURE("FAILED to pay account key-value respec token %s", pKey);
            }
        }
        else
        {
		    if(!slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pData, MicroTrans_GetRenameTokensGADKey(), -1, 0, 100000))
		    {
			    TRANSACTION_RETURN_LOG_FAILURE(
				    "Name Change Failed: Could not pay with rename token");
		    }
        }

		if(!asl_trh_aslSetName(ATR_PASS_ARGS, pEnt, pPuppetEnt, pcNewName, false))
		{
			TRANSACTION_RETURN_LOG_FAILURE(
				"Name Change Failed: Could not commit change");
		}

		TRANSACTION_RETURN_LOG_SUCCESS(
			"Name Change Success");
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Name Change Failed: Null ent, name or saved data");
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt,".Psaved.Bbadname, .Psaved.Savedname, .Psaved.Esoldname")
ATR_LOCKS(pPuppetEnt,".Psaved.Savedname");
enumTransactionOutcome asl_tr_ResetBadName(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Entity)* pPuppetEnt,
										   const char *pcNewName)
{
	if(NONNULL(pEnt) && NONNULL(pcNewName) && NONNULL(pEnt->pSaved))
	{
		if(!asl_trh_aslSetName(ATR_PASS_ARGS, pEnt, pPuppetEnt, pcNewName, true))
		{
			TRANSACTION_RETURN_LOG_FAILURE(
				"Name Change Failed: Could not commit change");
		}

		TRANSACTION_RETURN_LOG_SUCCESS(
			"Name Change Success");
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Name Change Failed: Null ent, name or saved data");
	}
}

static void aslSetName(PossibleCharacterChoice* pChoice, const char* pchName, LoginLink *loginLink, bool bBadName)
{
	TransactionReturnVal* returnVal;

	// final fail safe	
	if(StringIsInvalidCharacterName(pchName, loginLink->clientAccessLevel) == STRINGERR_NONE)
	{
		PossibleCharacterChoice* pPuppetChoice = NULL;
		const char* pchPuppetClassType = gConf.pchRenamePlayerPuppetClassType;
		SetNameCBStruct *pCBStruct = calloc(1, sizeof(SetNameCBStruct));
		returnVal = objCreateManagedReturnVal(aslSetName_CB, pCBStruct);

		pCBStruct->bBadName = bBadName;
		pCBStruct->loginCookie = loginLink->loginCookie;

		if (pchPuppetClassType && pchPuppetClassType[0])
		{
			CharClassTypes eClass = StaticDefineIntGetInt(CharClassTypesEnum, pchPuppetClassType);
			S32 i;
			for (i = eaSize(&pChoice->eaPuppets)-1; i >= 0; i--)
			{
				if (pChoice->eaPuppets[i]->iPetType == (U32)eClass)
				{
					pPuppetChoice = pChoice->eaPuppets[i];
					break;
				}
			}
		}
		if(bBadName)
		{
			AutoTrans_asl_tr_ResetBadName(returnVal, GetAppGlobalType(), 
				pChoice->iType, pChoice->iID, 
				SAFE_MEMBER(pPuppetChoice, iType), SAFE_MEMBER(pPuppetChoice, iID), 
				pchName);
		}
		else
		{
			AutoTrans_asl_tr_SetNameWithCost(returnVal, GetAppGlobalType(), 
				pChoice->iType, pChoice->iID, 
				SAFE_MEMBER(pPuppetChoice, iType), SAFE_MEMBER(pPuppetChoice, iID), 
				GLOBALTYPE_GAMEACCOUNTDATA, loginLink->accountID, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, 0, NULL, pchName);
		}
	}
	else
	{
		aslNotifyLogin(loginLink, "LoginServer_Rename_NameInvalid",kNotifyType_Failed, NULL);
	}
}

void aslLoginHandleChangeCharacterName(Packet *pak, LoginLink *loginLink)
{
	PossibleCharacterChoice *pChoice;
	bool bBadName = false;

	if (!ISM_IsStateActive(LOGIN_STATE_MACHINE, loginLink, LOGINSTATE_SELECTING_CHARACTER))
	{
		aslFailLogin(loginLink, langTranslateMessageKey(loginLink->clientLangID, "LoginServer_RenameWrongState"));
		return;
	}

	pChoice = StructAlloc(parse_PossibleCharacterChoice);

	if (!ParserRecv(parse_PossibleCharacterChoice, pak, pChoice, aslLoginServerClientsAreUntrustworthy() ? RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE : 0))
	{
		aslFailLogin(loginLink, langTranslateMessageKey(loginLink->clientLangID, "LoginServer_PacketCorruption"));
		return;
	}

	bBadName = pktGetBool(pak);

	if (pChoice->iID == 0 )
	{
		aslNotifyLogin(loginLink, langTranslateMessageKey(loginLink->clientLangID,"LoginServer_InvalidRename"),kNotifyType_Failed, NULL);
	}
	else
	{
		S32 iCharIdx;
		PossibleCharacterChoice *pFoundChoice = NULL;
		PossibleCharacterChoice *pSameName = NULL;
		for(iCharIdx = eaSize(&loginLink->pAllCharacterChoices->ppChoices)-1; iCharIdx >= 0; iCharIdx--)
		{
			if(loginLink->pAllCharacterChoices->ppChoices[iCharIdx])
			{
				if(loginLink->pAllCharacterChoices->ppChoices[iCharIdx]->iID == pChoice->iID
					&& loginLink->pAllCharacterChoices->ppChoices[iCharIdx]->iType == pChoice->iType)
				{
					pFoundChoice = loginLink->pAllCharacterChoices->ppChoices[iCharIdx];
				}

				if(stricmp(loginLink->pAllCharacterChoices->ppChoices[iCharIdx]->name, pChoice->name) == 0)
				{
					pSameName = loginLink->pAllCharacterChoices->ppChoices[iCharIdx];
				}
			}
		}
		if (pSameName && pFoundChoice == pSameName)
		{
			if(strcmp(pSameName->name, pChoice->name) != 0)
			{
				pSameName = NULL;
			}
		}
		if(!pFoundChoice )
		{
			if(!g_isContinuousBuilder && loginLink->accountID)
			{
				aslNotifyLogin(loginLink, langTranslateMessageKey(loginLink->clientLangID,"LoginServer_InvalidRename_DoNotOwn"),kNotifyType_Failed, NULL);
			}
			else
			{
				aslSetName(pChoice, pChoice->name, loginLink, bBadName);
			}
		}
		else if(pFoundChoice->bBadName && !bBadName)
		{
			aslNotifyLogin(loginLink, langTranslateMessageKey(loginLink->clientLangID,"LoginServer_InvalidRename"),kNotifyType_Failed, NULL);
		}
		else if(!pFoundChoice->bBadName && bBadName)
		{
			aslNotifyLogin(loginLink, langTranslateMessageKey(loginLink->clientLangID,"LoginServer_InvalidRename"),kNotifyType_Failed, NULL);
		}
		else if(pSameName)
		{
			char *pchMesg = NULL;
			langFormatMessageKey(loginLink->clientLangID, &pchMesg, "LoginServer_InvalidRename_DuplicateName",
				STRFMT_STRING("Name", pChoice->name),
				STRFMT_END);
			aslNotifyLogin(loginLink, pchMesg,kNotifyType_Failed, NULL);
		}
		else
		{
			aslSetName(pFoundChoice, pChoice->name, loginLink, bBadName);
		}
	}

	StructDestroy(parse_PossibleCharacterChoice, pChoice);
}

#include "aslLoginCharacterSelect_h_ast.c"
