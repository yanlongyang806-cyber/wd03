/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "aslLogin2_CharacterCreation.h"
#include "aslLogin2_StateMachine.h"
#include "Login2Common.h"
#include "Login2ServerCommon.h"
#include "aslLogin2_ClientComm.h"
#include "aslLoginServer.h"
#include "aslLoginCharacterSelect.h"
#include "LoginCommon.h"
#include "Entity.h"
#include "Player.h"
#include "Character.h"
#include "EntitySavedData.h"
#include "logging.h"
#include "file.h"
#include "ResourceInfo.h"
#include "GameAccountData/GameAccountData.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "referencesystem.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonEntity.h"
#include "GameAccountDataCommon.h"
#include "ContinuousBuilderSupport.h"
#include "PowerTree.h"
#include "CharacterAttribs.h"
#include "StringUtil.h"
#include "SavedPetCommon.h"
#include "PowerTreeHelpers.h"
#include "allegiance.h"
#include "VirtualShard.h"
#include "EString.h"
#include "entCritter.h"
#include "species_common.h"
#include "CostumeCommonTailor.h"
#include "StringCache.h"
#include "EntityBuild.h"
#include "PowerVars.h"
#include "SavedPetTransactions.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/Character_h_ast.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "AutoGen/LoginCommon_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"
#include "AutoGen/StatPoints_h_ast.h"

static ExpressionList s_NewCharacterExpressions;
static ExprContext *s_pNewCharacterContext;

static bool 
IsCharacterAllegianceUnlocked(GameAccountData *gameAccountData, ContainerID virtualShardID, const char *allegianceName)
{
    const char *keyValue;
    bool returnVal = false;
    const char *shortProductName = GetShortProductName();
    static char *s_keyName = NULL;

    // There are no allegiance restrictions on the UGC virtual shard, so return true if the player is creating on a UGC virtual shard.
    if ( virtualShardID )
    {
        VirtualShard *virtualShard;

        virtualShard = aslGetVirtualShardByID(virtualShardID);
        if ( virtualShard && virtualShard->bUGCShard )
        {
            return true;
        }
    }

    // Find a key of the form XX.AllegianceName and return true if it has a value greater than 0.
    estrPrintf(&s_keyName, "%s.%s", shortProductName, allegianceName);

    keyValue = gad_GetAttribString(gameAccountData, s_keyName);
    if( keyValue )
    {
        if ( atoi(keyValue) > 0 )
        {
            return true;
        }
    }

    return false;
}

// Horrible, horrible hack to ensure consistent sorting of node choices during character creation.
//  In theory you'd want to do this in data somehow, or be a bit more clever, but until it actually
//  breaks something it's not worth the headache.
static int 
NodeChoiceSort(const char **a, const char **b)
{
    PTNodeDef *pNodeDefA = a ? powertreenodedef_Find(*a) : NULL;
    PTNodeDef *pNodeDefB = b ? powertreenodedef_Find(*b) : NULL;
    char *pchCostA = pNodeDefA && eaSize(&pNodeDefA->ppRanks) ? pNodeDefA->ppRanks[0]->pchCostTable : NULL;
    char *pchCostB = pNodeDefB && eaSize(&pNodeDefB->ppRanks) ? pNodeDefB->ppRanks[0]->pchCostTable : NULL;
    int iA=0, iB=0;

    // Handle null cases
    if(!pchCostA && !pchCostB)
        return 0;
    if(pchCostA && !pchCostB)
        return -1;
    if(!pchCostA && pchCostB)
        return 1;

    if(!stricmp(pchCostA,"OriginPoints"))
        iA = 1;
    else if(!stricmp(pchCostA,"EndBuildPoints"))
        iA = 2;
    else if(!stricmp(pchCostA,"TreePoints"))
        iA = 3;

    if(!stricmp(pchCostB,"OriginPoints"))
        iB = 1;
    else if(!stricmp(pchCostB,"EndBuildPoints"))
        iB = 2;
    else if(!stricmp(pchCostB,"TreePoints"))
        iB = 3;

    return iA-iB;
}

static void 
BasicCharacterInit(ContainerID accountID, U32 loginCookie, const char *characterName, const char *accountName, const char *displayName, 
    const char *subName, PlayerType playerType, bool initTray, NOCONST(Entity) *ent)
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
    if (initTray)
    {
        ent->pSaved->pTray = StructCreateNoConst(parse_SavedTray);
    }

    strcpy(ent->pSaved->savedName, characterName);
    if (subName && subName[0])
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

    return;
}

void 
aslLogin2_GetCharacterCreationData(GameAccountData *gameAccountData, PlayerType playerType, ContainerID accountID, CharacterCreationDataHolder *holder)
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
        BasicCharacterInit(accountID, 0, "", "", "", "", playerType, false, pEnt);

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

#define NEWCHARFAIL(fmt, ...) { \
    entLog(LOG_LOGIN, (Entity *)playerEnt, "Creating", fmt, __VA_ARGS__); \
    if (g_isContinuousBuilder) { \
    char *pFullString = NULL; \
    logFlush(); \
    estrPrintf(&pFullString, fmt, __VA_ARGS__); \
    assertmsgf(0, "aslInitializeNewCharacter failing: %s", pFullString); \
    estrDestroy(&pFullString); \
    } \
    if (isDevelopmentMode()) { \
    char *pFullString = NULL; \
    estrPrintf(&pFullString, fmt, __VA_ARGS__); \
    Errorf("aslInitializeNewCharacter failing: %s", pFullString); \
    estrDestroy(&pFullString); \
    } \
    }

bool 
aslLogin2_InitializeNewCharacter(Login2State *loginState, NOCONST(Entity) *playerEnt)
{
    PERFINFO_AUTO_START_FUNC();

    if ( loginState == NULL || loginState->characterCreationData == NULL )
    {
        PERFINFO_AUTO_STOP_FUNC();
        return false;
    }
    else
    {
        PowerTreeDef *powerTreeDef;
        NOCONST(PowerTree)* powerTree;
        S32 i;
        char *pKeyValue;
        char *pEStringError = NULL;
        PCSlotSet *costumeSlotSet = NULL;
        SpeciesDef *speciesDef = NULL;
        CritterFaction *critterFaction = NULL;
        AllegianceDef *allegianceDef = NULL;
        CharacterClass *characterClass = NULL;
        S32 iRequiredSlots = 0;
        PCSkeletonDef *skeletonDef = NULL;
        GameAccountDataExtract *pExtract;
        Login2CharacterCreationData *characterCreationData;
        GameAccountData *gameAccountData;
        static char *s_looseUIString = NULL;

        characterCreationData = loginState->characterCreationData;

        BasicCharacterInit(loginState->accountID, Login2_ShortenToken(loginState->loginCookie), 
            characterCreationData->name, loginState->accountName, loginState->accountDisplayName, 
            characterCreationData->subName, loginState->playerType, eaSize(&characterCreationData->puppetInfo) == 0, playerEnt);

        powerTreeDef = characterCreationData->powerTreeName ? powertreedef_Find(characterCreationData->powerTreeName) : NULL;
        powerTree = powertree_Create(powerTreeDef);
        
        gameAccountData = GET_REF(loginState->hGameAccountData);
        pExtract = entity_CreateLocalGameAccountDataExtract(gameAccountData);

        if ( !aslCanPlayerAccessVirtualShard(gameAccountData->eaAccountKeyValues, characterCreationData->virtualShardID) )
        {
            NEWCHARFAIL("Disallowed virtual shard %u", characterCreationData->virtualShardID);
            StructDestroyNoConst(parse_PowerTree, powerTree);
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            PERFINFO_AUTO_STOP_FUNC();
            return false;
        }

        if ( !aslIsVirtualShardEnabledByID(loginState->clientAccessLevel, characterCreationData->virtualShardID) )
        {
            NEWCHARFAIL("Disabled virtual shard %u", characterCreationData->virtualShardID);
            StructDestroyNoConst(parse_PowerTree, powerTree);
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            PERFINFO_AUTO_STOP_FUNC();
            return false;
        }

        if ( !aslIsVirtualShardAvailableByID(loginState->clientAccessLevel, loginState->playerHasUGCProjectSlots, characterCreationData->virtualShardID) )
        {
            NEWCHARFAIL("Locked virtual shard %u", characterCreationData->virtualShardID);
            StructDestroyNoConst(parse_PowerTree, powerTree);
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            PERFINFO_AUTO_STOP_FUNC();
            return false;
        }

        // entity_IsUGCCharacter would be better but ent isn't set to virtual shard yet. If virtual shards are not all UGC this function will require a fix
        if ( loginState->UGCCharacterPlayOnly && !characterCreationData->virtualShardID )
        {
            NEWCHARFAIL("Only UGC characters can be created at this time.");
            StructDestroyNoConst(parse_PowerTree, powerTree);
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            PERFINFO_AUTO_STOP_FUNC();
            return false;
        }

        if ( characterCreationData->powerTreeName && !(powerTree && powerTreeDef) )
        {
            NEWCHARFAIL("Specified an invalid power tree %s", characterCreationData->powerTreeName);
            StructDestroyNoConst(parse_PowerTree, powerTree);
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            PERFINFO_AUTO_STOP_FUNC();
            return false;
        }

        if ( !gameSpecific_PreInitNewCharacter(playerEnt, characterCreationData, pExtract) )
        {
            NEWCHARFAIL("Game-specific new character initialization failed.");
            StructDestroyNoConst(parse_PowerTree, powerTree);
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            return false;
        }

        // If we can't find these references, either we're forgetting to
        // load something, or someone is injecting malicious data into
        // the character creation process. Either way, we probably want it
        // to be logged.

        // Initialize account-related values
        playerEnt->pPlayer->accountID = loginState->accountID;
        playerEnt->pPlayer->accountAccessLevel = loginState->clientAccessLevel;
        if( isProductionMode() )
        {
            playerEnt->pPlayer->accessLevel = 0;
        }
        else
        {
            playerEnt->pPlayer->accessLevel = loginState->clientAccessLevel;
        }

        //set the time of creation to be now
        playerEnt->pPlayer->iCreatedTime = timeSecondsSince2000();

        //set the virtual shard id
        playerEnt->pPlayer->iVirtualShardID = characterCreationData->virtualShardID;

        // Set the fixup version
        playerEnt->pSaved->uFixupVersion = CURRENT_ENTITY_FIXUP_VERSION;
        playerEnt->pSaved->uGameSpecificFixupVersion = gameSpecificFixup_Version();
        playerEnt->pSaved->uGameSpecificPreLoginFixupVersion = gameSpecificPreLoginFixup_Version();

        if ( loginState->accountDisplayName )
        {
            strcpy(playerEnt->pPlayer->publicAccountName, loginState->accountDisplayName); //  update display name
        }

        //Update the description
        if( characterCreationData->description )
        {
            if( playerEnt->pSaved->savedDescription )
            {
                StructFreeString(playerEnt->pSaved->savedDescription);
            }
            playerEnt->pSaved->savedDescription = StructAllocString(characterCreationData->description);
        }

        SET_HANDLE_FROM_STRING("CharacterClass", characterCreationData->className ? characterCreationData->className : "Default", playerEnt->pChar->hClass);
        if ( !GET_REF(playerEnt->pChar->hClass) )
        {
            NEWCHARFAIL( "Has no class");
            StructDestroyNoConst(parse_PowerTree, powerTree);
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            PERFINFO_AUTO_STOP_FUNC();
            return false;
        }

        characterClass = GET_REF(playerEnt->pChar->hClass);

        // Set the path of the character if available
        if ( characterCreationData->characterPathName && characterCreationData->characterPathName[0] )
        {
            CharacterPath * characterPath = NULL;
            //Set the character's primary path.
            SET_HANDLE_FROM_STRING(g_hCharacterPathDict, characterCreationData->characterPathName, playerEnt->pChar->hPath);

            characterPath = entity_trh_GetPrimaryCharacterPath(playerEnt);
            if( !characterPath )
            {
                NEWCHARFAIL( "Invalid no charpath selected");
                StructDestroyNoConst(parse_PowerTree, powerTree);
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                PERFINFO_AUTO_STOP_FUNC();
                return false;
            }
            else if( !Entity_EvalCharacterPathRequiresExpr((Entity *)playerEnt, characterPath) )
            {
                NEWCHARFAIL( "Character cannot use that char path: %s", characterPath->pchName);
                StructDestroyNoConst(parse_PowerTree, powerTree);
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                PERFINFO_AUTO_STOP_FUNC();
                return false;
            }
        }

        //If this is not the default class and the entity cannot be that class...
        if( stricmp(characterClass->pchName, "Default") && !entity_PlayerCanBecomeClass((Entity *)playerEnt, characterClass) )
        {
            NEWCHARFAIL( "Cannot be that class! %s", characterClass->pchName ? characterClass->pchName : "(unnamed)");
            StructDestroyNoConst(parse_PowerTree, powerTree);
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            PERFINFO_AUTO_STOP_FUNC();
            return false;
        }

        character_FillSavedAttributesFromClass(playerEnt->pChar,0);

        if ( powerTreeDef && GET_REF(powerTreeDef->hClass) )
        {
            if ( GET_REF(powerTreeDef->hClass) != GET_REF(playerEnt->pChar->hClass) )
            {
                NEWCHARFAIL( "Powertree class doesn't match passed in class");
                StructDestroyNoConst(parse_PowerTree, powerTree);
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                PERFINFO_AUTO_STOP_FUNC();
                return false;
            }
        }

        if ( StringIsValidCommonName( playerEnt->pSaved->savedName, playerEnt->pPlayer->accessLevel ) == false )
        {
            NEWCHARFAIL( "Invalid Character Name=%s", playerEnt->pSaved->savedName);
            StructDestroyNoConst(parse_PowerTree, powerTree);
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            PERFINFO_AUTO_STOP_FUNC();
            return false;
        }

        if ( playerEnt->pSaved->savedSubName )
        {
            if ( savedpet_ValidateFormalName(CONTAINER_RECONST(Entity, playerEnt), playerEnt->pSaved->savedSubName, &pEStringError) == false)
            {
                NEWCHARFAIL( "Invalid Character Formal Name=%s", playerEnt->pSaved->savedSubName);
                StructDestroyNoConst(parse_PowerTree, powerTree);
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                estrDestroy(&pEStringError);
                PERFINFO_AUTO_STOP_FUNC();
                return false;
            }
            estrDestroy(&pEStringError);
        }

        if ( StringIsInvalidDescription( playerEnt->pSaved->savedDescription ) )
        {
            NEWCHARFAIL( "Invalid Description=%s", playerEnt->pSaved->savedDescription);
            StructDestroyNoConst(parse_PowerTree, powerTree);
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            PERFINFO_AUTO_STOP_FUNC();
            return false;
        }

        SET_HANDLE_FROM_STRING("CostumeMood", characterCreationData->moodName ? characterCreationData->moodName : "Normal", playerEnt->costumeRef.hMood);

        // XP at level 1 is always 0		
        if ( powerTree && powerTreeDef && !entity_CanBuyPowerTreeHelper(PARTITION_IN_TRANSACTION, playerEnt, powerTreeDef, false) )
        {
            NEWCHARFAIL( "Cannot buy tree=%s", powerTreeDef->pchName);
            StructDestroyNoConst(parse_PowerTree, powerTree);
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            PERFINFO_AUTO_STOP_FUNC();
            return false;
        }

        // Mike says this is the right way to purchase a power tree inside a transaction. -- jfw
        if( powerTree )
        {
            eaIndexedEnableNoConst(&playerEnt->pChar->ppPowerTrees, parse_PowerTree);
            eaPush(&playerEnt->pChar->ppPowerTrees, powerTree);
        }

        if ( characterCreationData->allegianceName && characterCreationData->allegianceName[0] )
        {
            SET_HANDLE_FROM_STRING("Allegiance", characterCreationData->allegianceName, playerEnt->hAllegiance);
            allegianceDef = GET_REF(playerEnt->hAllegiance);
            if ( allegianceDef )
            {
                if ( GET_REF(allegianceDef->hFaction) )
                {
                    COPY_HANDLE(playerEnt->hFaction, allegianceDef->hFaction);
                }
            }
            if ( (!allegianceDef) || ( !GET_REF(playerEnt->hFaction) ) )
            {
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                NEWCHARFAIL("Invalid allegiance or faction");
                PERFINFO_AUTO_STOP_FUNC();
                return false;
            }
            else if ( allegianceDef->bNeedsUnlock && !IsCharacterAllegianceUnlocked(gameAccountData, characterCreationData->virtualShardID, allegianceDef->pcName) )
            {
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                NEWCHARFAIL("Allegiance requires unlock");
                PERFINFO_AUTO_STOP_FUNC();
                return false;
            }
        }
        else
        {
            COPY_HANDLE(playerEnt->hFaction,FactionDefaults->hDefaultPlayerFaction);
            COPY_HANDLE(playerEnt->hAllegiance,gAllegianceDefaults->hDefaultPlayerAllegiance);
        }

        critterFaction = GET_REF(playerEnt->hFaction);
        if ( gConf.bPlayerRequiredToHaveAFaction && !critterFaction )
        {
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            NEWCHARFAIL("No Faction was found");
            PERFINFO_AUTO_STOP_FUNC();
            return false;
        }

        allegianceDef = GET_REF(playerEnt->hAllegiance);
        if ( gConf.bPlayerRequiredToHaveAllegiance && !allegianceDef )
        {
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            NEWCHARFAIL("No Allegiance was found");
            PERFINFO_AUTO_STOP_FUNC();
            return false;
        }

        if ( characterCreationData->speciesName )
        {
            SET_HANDLE_FROM_STRING("Species", characterCreationData->speciesName, playerEnt->pChar->hSpecies);
        }

        speciesDef = GET_REF(playerEnt->pChar->hSpecies);
        if ( gConf.bPlayerRequiredToHaveASpecies && !speciesDef )
        {
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            NEWCHARFAIL("No Species was found");
            PERFINFO_AUTO_STOP_FUNC();
            return false;
        }

        //Validate species belongs in Allegiance
        if ( allegianceDef && speciesDef && eaSize(&allegianceDef->eaStartSpecies) )
        {
            for ( i = eaSize(&allegianceDef->eaStartSpecies)-1; i >= 0; --i )
            {
                if ( GET_REF(allegianceDef->eaStartSpecies[i]->hSpecies) == speciesDef )
                {
                    if ( speciesDef->pcUnlockCode && *speciesDef->pcUnlockCode )
                    {
                        pKeyValue = (char *)gad_GetAttribString(gameAccountData, speciesDef->pcUnlockCode);
                        if(pKeyValue && atoi(pKeyValue) > 0)
                        {
                            break;
                        }
                    }
                    else
                    {
                        break;
                    }
                }
            }
            if (i < 0)
            {
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                NEWCHARFAIL("Species is not allowed with this Allegiance or is not unlocked");
                PERFINFO_AUTO_STOP_FUNC();
                return false;
            }
        }
        else
        {
            // Validate species is unlocked
            if ( speciesDef && speciesDef->pcUnlockCode && *speciesDef->pcUnlockCode )
            {
                pKeyValue = (char *)gad_GetAttribString(gameAccountData, speciesDef->pcUnlockCode);
                if( !pKeyValue || atoi(pKeyValue) <= 0 )
                {
                    entity_DestroyLocalGameAccountDataExtract(&pExtract);
                    NEWCHARFAIL("Species is not unlocked");
                    PERFINFO_AUTO_STOP_FUNC();
                    return false;
                }
            }
        }

        //Validate class belongs in Allegiance
        if ( allegianceDef && characterClass && eaSize(&allegianceDef->eaClassesAllowed) )
        {
            for ( i = eaSize(&allegianceDef->eaClassesAllowed)-1; i >= 0; --i )
            {
                if ( GET_REF(allegianceDef->eaClassesAllowed[i]->hClass) == characterClass )
                {
                    break;
                }
            }
            if (i < 0)
            {
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                NEWCHARFAIL("Class is not allowed with this Allegiance");
                PERFINFO_AUTO_STOP_FUNC();
                return false;
            }
        }

        // Validate species is permitted by the class
        if ( characterClass && speciesDef && eaSize(&characterClass->eaPermittedSpecies) )
        {
            for ( i = eaSize(&characterClass->eaPermittedSpecies)-1; i >= 0; --i )
            {
                if ( GET_REF(characterClass->eaPermittedSpecies[i]->hSpecies) == speciesDef )
                {
                    break;
                }
            }
            if ( i < 0 )
            {
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                NEWCHARFAIL("Species is not allowed with this Class");
                PERFINFO_AUTO_STOP_FUNC();
                return false;
            }
        }

        // Initialize the skeleton to the species' skeleton
        if ( speciesDef )
        {
            skeletonDef = GET_REF(speciesDef->hSkeleton);
        }

        // Initialize the costume slots
        costumeSlotSet = costumeEntity_GetSlotSet((Entity*)playerEnt, false);
        costumeEntity_trh_FixupCostumeSlots(ATR_EMPTY_ARGS, NULL, playerEnt, costumeSlotSet ? costumeSlotSet->pcName : NULL);

        if ( eaSize(&characterCreationData->costumes) == 0 )
        {
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            NEWCHARFAIL("Has no costumes");
            PERFINFO_AUTO_STOP_FUNC();
            return false;
        }

        // Count the required slots, and initialize the active costume slot
        for ( i = 0; costumeSlotSet && i < eaSize(&costumeSlotSet->eaSlotDefs); i++ )
        {
            if ( costumeSlotSet->eaSlotDefs[i]->eCreateCharacter == kPCCharacterCreateSlot_Required )
            {
                iRequiredSlots++;
            }
        }

        // Go through the list slots to fill in at character creation
        for ( i = 0; i < eaSize(&characterCreationData->costumes); i++ )
        {
            PossibleCharacterCostume *pCharCostume = characterCreationData->costumes[i];
            PlayerCostume *pCostume = pCharCostume->pConstCostume;
            PCSlotDef *pSlotDef = NULL;
            PCSlotType *pSlotType = NULL;
            NOCONST(PlayerCostume) *pCostumeCopy = NULL;
            NOCONST(PlayerCostumeSlot) *pSlot = NULL;
            char *error = NULL;
            int iSlot = 0;
            int iSlotID = 0;
            bool bOk;

            // Find the slot that matches
            for ( ; costumeSlotSet && iSlot < eaSize(&costumeSlotSet->eaSlotDefs); iSlot++ )
            {
                if ( costumeSlotSet->eaSlotDefs[iSlot]->iSlotID == pCharCostume->iSlotID )
                {
                    pSlotDef = costumeSlotSet->eaSlotDefs[iSlot];
                    pSlotType = costumeLoad_GetSlotType(pSlotDef->pcSlotType);
                    iSlotID = pSlotDef->iSlotID;
                    break;
                }
            }
            if ( pCharCostume->iSlotID != 0 && !pSlotDef )
            {
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                NEWCHARFAIL("Bad costume slot ID: %d", pCharCostume->iSlotID);
                PERFINFO_AUTO_STOP_FUNC();
                return false;
            }

            if ( !pSlotDef || pSlotDef->eCreateCharacter == kPCCharacterCreateSlot_Default )
            {
                // Only the first slot may be created if there are no required slots
                if ( iRequiredSlots != 0 )
                {
                    entity_DestroyLocalGameAccountDataExtract(&pExtract);
                    NEWCHARFAIL("Costume slot %d cannot be filled at creation", iSlot);
                    PERFINFO_AUTO_STOP_FUNC();
                    return false;
                }
                else
                {
                    // If there are no required slots, force the first slot and
                    // fall through to the validation that checks to see if the
                    // costume was already filled in.
                    iSlot = 0;
                    iSlotID = 0;
                }
            }
            else if ( pSlotDef->eCreateCharacter != kPCCharacterCreateSlot_Required )
            {
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                NEWCHARFAIL("Costume slot %d cannot be filled at creation", iSlot);
                PERFINFO_AUTO_STOP_FUNC();
                return false;
            }

            if ( !pSlotDef )
            {
                // Default to the first slot if it can't find one
                iSlot = 0;
                pSlotType = costumeEntity_GetSlotType((Entity*)playerEnt, iSlot, false, &iSlotID);
            }
            else
            {
                pSlotType = costumeLoad_GetSlotType(pSlotDef->pcSlotType);
            }

            if ( pCharCostume->pcSlotType )
            {
                PCSlotType *pCostumeSlotType = costumeLoad_GetSlotType(pCharCostume->pcSlotType);
                if ( *pCharCostume->pcSlotType && !pCostumeSlotType )
                {
                    entity_DestroyLocalGameAccountDataExtract(&pExtract);
                    NEWCHARFAIL("Costume has invalid type '%s'", pCharCostume->pcSlotType);
                    PERFINFO_AUTO_STOP_FUNC();
                    return false;
                }
                else if ( pCostumeSlotType && pSlotDef )
                {
                    if ( pSlotDef->pcSlotType && stricmp(pSlotDef->pcSlotType, pCostumeSlotType->pcName) != 0 && eaFind(&pSlotDef->eaOptionalSlotTypes, pCostumeSlotType->pcName) < 0 )
                    {
                        entity_DestroyLocalGameAccountDataExtract(&pExtract);
                        NEWCHARFAIL("Costume cannot use type '%s' in slot %d", pCharCostume->pcSlotType, iSlot);
                        PERFINFO_AUTO_STOP_FUNC();
                        return false;
                    }
                }
                else if ( pCostumeSlotType && pSlotType )
                {
                    if ( pSlotType != pCostumeSlotType )
                    {
                        entity_DestroyLocalGameAccountDataExtract(&pExtract);
                        NEWCHARFAIL("Costume cannot use type '%s' in slot %d", pCharCostume->pcSlotType, iSlot);
                        PERFINFO_AUTO_STOP_FUNC();
                        return false;
                    }
                }
                pSlotType = pCostumeSlotType;
            }

            if ( iSlot < eaSize(&playerEnt->pSaved->costumeData.eaCostumeSlots) )
            {
                // Make sure it's not already filled
                if ( playerEnt->pSaved->costumeData.eaCostumeSlots[iSlot]->pCostume )
                {
                    entity_DestroyLocalGameAccountDataExtract(&pExtract);
                    NEWCHARFAIL("Costume slot %d already filled", iSlot);
                    PERFINFO_AUTO_STOP_FUNC();
                    return false;
                }
            }

            // Get the costume
            if ( !pCostume && pCharCostume->pcCostume )
            {
                pCostume = costumeEntity_CostumeFromName(pCharCostume->pcCostume);
            }
            if ( !pCostume )
            {
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                NEWCHARFAIL("Has undefined costume");
                PERFINFO_AUTO_STOP_FUNC();
                return false;
            }

            pCostumeCopy = StructCloneDeConst(parse_PlayerCostume, pCostume);
            // Validate the costume
            bOk = costumeValidate_ValidatePlayerCreated((PlayerCostume*)pCostumeCopy, speciesDef, pSlotType, (Entity*)playerEnt, (Entity*)playerEnt, &error, NULL, NULL, false);

            //attempt to make the costume valid if it failed and then check again
            if( !bOk )
            {
                PlayerCostume **eaUnlockedCostumes = NULL;

                costumeEntity_GetUnlockCostumes(NULL, gameAccountData, (Entity*)playerEnt, (Entity*)playerEnt, &eaUnlockedCostumes);
                costumeTailor_MakeCostumeValid(pCostumeCopy, speciesDef, NULL, pSlotType, false, false, false, NULL, true, pExtract, true, NULL);
                eaDestroy(&eaUnlockedCostumes);

                bOk = costumeValidate_ValidatePlayerCreated((PlayerCostume*)pCostumeCopy, speciesDef, pSlotType, (Entity*)playerEnt, (Entity*)playerEnt, &error, NULL, NULL, false);

                if( !bOk )
                {
                    entity_DestroyLocalGameAccountDataExtract(&pExtract);
                    NEWCHARFAIL("Costume was invalid: %s", error);
                    PERFINFO_AUTO_STOP_FUNC();
                    StructDestroyNoConst(parse_PlayerCostume, pCostumeCopy);
                    return false;
                }
            }


            if ( !skeletonDef )
            {
                skeletonDef = GET_REF(pCostume->hSkeleton);
            }
            if ( GET_REF(pCostume->hSkeleton) != skeletonDef )
            {
                // NB: costumeValidatePlayerCreated requires the presence of a Skeleton
                PCSkeletonDef *otherSkeletonDef = GET_REF(pCostume->hSkeleton);
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                NEWCHARFAIL("Mismatched skeleton: %s != %s", skeletonDef->pcName, otherSkeletonDef->pcName);
                PERFINFO_AUTO_STOP_FUNC();
                StructDestroyNoConst(parse_PlayerCostume, pCostumeCopy);
                return false;
            }

            // Ensure player costume name matches the player name
            pCostumeCopy->pcName = allocAddString(playerEnt->pSaved->savedName);

            // Ensure stored player costumes do not have a file
            pCostumeCopy->pcFileName = NULL;

            // Store the costume
            while ( iSlot >= eaSize(&playerEnt->pSaved->costumeData.eaCostumeSlots) )
            {
                int iNewSlot = eaSize(&playerEnt->pSaved->costumeData.eaCostumeSlots);
                PCSlotDef *pNewDef = costumeSlotSet ? costumeSlotSet->eaSlotDefs[iNewSlot] : NULL;
                pSlot = StructCreateNoConst(parse_PlayerCostumeSlot);
                if ( pNewDef )
                {
                    pSlot->iSlotID = pNewDef->iSlotID;
                    pSlot->pcSlotType = pNewDef->pcSlotType;
                }
                eaPush(&playerEnt->pSaved->costumeData.eaCostumeSlots, pSlot);
            }

            if ( !pSlot )
            {
                pSlot = playerEnt->pSaved->costumeData.eaCostumeSlots[iSlot];
            }

            pSlot->pCostume = pCostumeCopy;
            if ( pSlotType )
            {
                pSlot->pcSlotType = pSlotType->pcName;
            }
        }

        // Set the active costume
        if ( !costumeSlotSet || !iRequiredSlots )
        {
            playerEnt->pSaved->costumeData.iActiveCostume = 0;
        }
        else
        {
            // Set to the first required costume
            for ( i = 0; i < eaSize(&costumeSlotSet->eaSlotDefs); i++ )
            {
                if (costumeSlotSet->eaSlotDefs[i]->eCreateCharacter == kPCCharacterCreateSlot_Required)
                {
                    playerEnt->pSaved->costumeData.iActiveCostume = i;
                    break;
                }
            }

            // Make sure all the required costumes are defined
            for ( i = 0; i < eaSize(&costumeSlotSet->eaSlotDefs); i++ )
            {
                if ( costumeSlotSet->eaSlotDefs[i]->eCreateCharacter == kPCCharacterCreateSlot_Required &&
                    (i >= eaSize(&playerEnt->pSaved->costumeData.eaCostumeSlots) ||
                    !playerEnt->pSaved->costumeData.eaCostumeSlots[i]->pCostume) )
                {
                    entity_DestroyLocalGameAccountDataExtract(&pExtract);
                    NEWCHARFAIL("Missing costume for slot ID %d", costumeSlotSet->eaSlotDefs[i]->iSlotID);
                    PERFINFO_AUTO_STOP_FUNC();
                    return false;
                }
            }
        }

        costumeEntity_ResetStoredCostume((Entity*) playerEnt);

        // Add GM flag to characters in GM accounts
        pKeyValue = AccountProxyFindValueFromKeyContainer(gameAccountData->eaAccountKeyValues, GetAccountGMKey());
        if( pKeyValue )
        {
            if( atoi(pKeyValue) > 0 )
            {
                playerEnt->pPlayer->bIsGM = true;
            }
        }

        playerEnt->eGender = costumeEntity_GetEffectiveCostumeGender((Entity *)playerEnt);

        //initialize Player inventory bags here
        inv_ent_trh_VerifyInventoryData(ATR_EMPTY_ARGS, playerEnt, true, true, NULL);

        inv_ent_trh_AddInventoryItems(ATR_EMPTY_ARGS, playerEnt, NULL, pExtract);

        // hardcoded build creation, happens after class and inventory init
        //  also creates PowerSlots (not usually necessary, but just to be totally safe since they're SOMETIMES_TRANSACT)
        if( entity_BuildCanCreate(playerEnt) )
        {
            if ( !player_BuildCreate(playerEnt) )
            {
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                NEWCHARFAIL( "Failed to create the player build");
                PERFINFO_AUTO_STOP_FUNC();
                return false;
            }
        }

        // Autobuy PowerTrees first - use of internal helper here is legal since this isn't really a transaction.
        //  This won't update the uiPowerTreeModCount, but that's not really an issue.  If
        //  it is, this can just be changed to do something more similar to entity_PowerTreeAutoBuy.
        // If you copy-paste this I will hunt you down and kill you.
        if( !(entity_PowerTreeAutoBuyHelper(PARTITION_IN_TRANSACTION, playerEnt, NULL, NULL) >= 0) )
        {
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            NEWCHARFAIL( "Auto-buy failed");
            PERFINFO_AUTO_STOP_FUNC();
            return false;
        }

        // Hard-coded champs sort, only happens if any of the relevant trees exist.
        if ( powertable_Find("OriginPoints") || powertable_Find("EndBuildPoints") || powertable_Find("TreePoints") )
        {
            eaQSort(characterCreationData->powerNodes, NodeChoiceSort);
        }

        if ( eaSize(&characterCreationData->powerNodes) > 0 )
        {
            for ( i = 0; i < eaSize(&characterCreationData->powerNodes); i++ )
            {
                const char *powerNodeName = eaGet(&characterCreationData->powerNodes, i);
                if ( powerNodeName && *powerNodeName )
                {
                    PTNodeDef *powerNodeDef = powertreenodedef_Find(powerNodeName);
                    powerTreeDef = powertree_TreeDefFromNodeDef(powerNodeDef);
                    if ( !(powerTreeDef && powerNodeDef) )
                    {
                        entity_DestroyLocalGameAccountDataExtract(&pExtract);
                        NEWCHARFAIL( "Failed to find tree/node, node=%s", powerNodeName);
                        PERFINFO_AUTO_STOP_FUNC();
                        return false;
                    }
                    else
                    {
                        // Buy PowerTree Nodes - use of internal helper here is legal since this isn't really a transaction.
                        //  This won't update the uiPowerTreeModCount, but that's not really an issue.  If
                        //  it is, this can just be changed to do something more similar to entity_PowerTreeNodeIncreaseRank.
                        // If you copy-paste this I will hunt you down and kill you.
                        NOCONST(PTNode) *powerNode = entity_PowerTreeNodeIncreaseRankHelper(PARTITION_IN_TRANSACTION, playerEnt, NULL, powerTreeDef->pchName, powerNodeName, false, false, false, NULL);
                        if ( !powerNode )
                        {
                            entity_DestroyLocalGameAccountDataExtract(&pExtract);
                            NEWCHARFAIL( "Failed to buy node, tree=%s, node=%s", powerTreeDef->pchName, powerNodeName);
                            PERFINFO_AUTO_STOP_FUNC();
                            return false;
                        }
                    }
                }
            }
        }
        //Did we get a blank list of powers? If so, projects that require you to follow
        //the classpath choices will just give all the required powers here.
        else if ( gConf.bCharacterPathMustBeFollowed && characterCreationData->characterPathName )
        {
            int j, k;
            CharacterPath* characterPath = entity_trh_GetPrimaryCharacterPath(playerEnt);
            for ( i = 0; i < eaSize(&characterPath->eaSuggestedPurchases); i++ )
            {
                PowerTable* powerTable = powertable_Find(characterPath->eaSuggestedPurchases[i]->pchPowerTable);
                CharacterPathChoice** characterPathChoices = characterPath->eaSuggestedPurchases[i]->eaChoices;
                //Buy all the powers that are suggested and afforadable at level 1.
                for ( j = 0; j < powerTable->pfValues[0]; j++ )
                {
                    for ( k = 0; k < eaSize(&characterPathChoices[j]->eaSuggestedNodes); k++ )
                    {
                        PTNodeDef* powerNodeDef = GET_REF(characterPathChoices[j]->eaSuggestedNodes[k]->hNodeDef);
                        powerTreeDef = powertree_TreeDefFromNodeDef(powerNodeDef);
                        if ( !(powerTreeDef && powerNodeDef) )
                        {
                            entity_DestroyLocalGameAccountDataExtract(&pExtract);
                            NEWCHARFAIL( "Failed to find tree/node, node=%s", powerNodeDef->pchNameFull);
                            PERFINFO_AUTO_STOP_FUNC();
                            return false;
                        }
                        else
                        {
                            // Buy PowerTree Nodes - use of internal helper here is legal since this isn't really a transaction.
                            //  This won't update the uiPowerTreeModCount, but that's not really an issue.  If
                            //  it is, this can just be changed to do something more similar to entity_PowerTreeNodeIncreaseRank.
                            // If you copy-paste this I will hunt you down and kill you.
                            NOCONST(PTNode) *powerNode = entity_PowerTreeNodeIncreaseRankHelper(PARTITION_IN_TRANSACTION, playerEnt, NULL, powerTreeDef->pchName, powerNodeDef->pchNameFull, false, false, false, NULL);
                            if ( !powerNode )
                            {
                                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                                NEWCHARFAIL( "Failed to buy node, tree=%s, node=%s", powerTreeDef->pchName, powerNodeDef->pchNameFull);
                                PERFINFO_AUTO_STOP_FUNC();
                                return false;
                            }
                        }
                    }
                }
            }
        }

        for ( i=0; i < eaSize(&characterCreationData->puppetInfo); i++ )
        { 
            LoginPuppetInfo* puppetInfo = characterCreationData->puppetInfo[i];

            if( i == 0 )
            {
                Entity_MakePuppetMaster(playerEnt);
            }

            if ( !Entity_AddPuppetCreateRequest(playerEnt,puppetInfo) )
            {
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                NEWCHARFAIL("Puppet Add Failed (CritterDef=%s)", puppetInfo->pchType);
                PERFINFO_AUTO_STOP_FUNC();
                return false;
            }
        }

        for ( i = 0; i < eaSize(&characterCreationData->petInfo); i++ )
        {
            LoginPetInfo* petInfo = characterCreationData->petInfo[i];

            if ( !Entity_AddPetCreateRequest(playerEnt, petInfo) )
            {
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                NEWCHARFAIL("Pet Add Failed (PetDef=%s)", petInfo->pchType);
                PERFINFO_AUTO_STOP_FUNC();
                return false;
            }
        }

        for( i = 0; i < eaSize(&g_ppAutoGrantPets); i++ )
        {
            if( entity_CanUsePetDef((Entity*)playerEnt, g_ppAutoGrantPets[i], NULL) )
            {
                if( g_ppAutoGrantPets[i]->bCritterPet )
                {
					ItemChangeReason reason = {0};
					inv_FillItemChangeReason(&reason, (Entity*)playerEnt, "Login:AutoGrantPetOnCharCreate", NULL);

                    trAddAllowedCritterPet(ATR_EMPTY_ARGS, playerEnt, g_ppAutoGrantPets[i], 0, &reason, pExtract);
                }
                else
                {
                    LoginPetInfo petInfo = {0};

                    petInfo.pchType = g_ppAutoGrantPets[i]->pchPetName;
                    petInfo.pPetDef = g_ppAutoGrantPets[i];

                    Entity_AddPetCreateRequest(playerEnt, &petInfo);
                }
            }
        }

        // Copy all stat points to the entity
        if ( characterCreationData->assignedStats )
        {
            eaCopyStructsNoConst(&characterCreationData->assignedStats, &playerEnt->pChar->ppAssignedStats, parse_AssignedStats);
        }

        exprContextSetPointerVar(s_pNewCharacterContext, "Entity", playerEnt, parse_Entity, true, true);
        for ( i = 0; i < eaSize(&s_NewCharacterExpressions.expressions); i++ )
        {
            Expression *pExpr = s_NewCharacterExpressions.expressions[i];
            MultiVal mv;
            exprEvaluate(pExpr, s_pNewCharacterContext, &mv);
            if ( !mv.intval || (MultiValIsString(&mv) && *mv.str) )
            { 
                entity_DestroyLocalGameAccountDataExtract(&pExtract);
                NEWCHARFAIL(
                    "new character validation expression failed: expression=%s result=%s/%"FORM_LL"d, ",
                    exprGetCompleteString(pExpr), MultiValGetString(&mv, NULL), MultiValGetInt(&mv, NULL));
                PERFINFO_AUTO_STOP_FUNC();
                return false;
            }
        }

        //Don't check for this on the continuous builder, it may not choose powers when logging in
        if( !g_isContinuousBuilder )
        {
            // Check for correct number of starting powers. This is optional by game.
            for( i = 0; i < eaSize(&g_RequiredPowersAtCreation.eaRequiredPowers); ++i )
            {
                S32 numPowers = PowersUI_GetNumPowersWithCatAndPurposeInternal((Entity *)playerEnt, g_RequiredPowersAtCreation.eaRequiredPowers[i]->pcRequiredPowerCategory, g_RequiredPowersAtCreation.eaRequiredPowers[i]->pcRequiredPowerPurpose);
                if( numPowers != g_RequiredPowersAtCreation.eaRequiredPowers[i]->iNumberOfRequiredPower )
                {
                    entity_DestroyLocalGameAccountDataExtract(&pExtract);
                    NEWCHARFAIL( "Failed due to incorrect number of powers. Category = %s, Purpose = %s, should be %d but was %d",
                        g_RequiredPowersAtCreation.eaRequiredPowers[i]->pcRequiredPowerCategory,
                        g_RequiredPowersAtCreation.eaRequiredPowers[i]->pcRequiredPowerPurpose,
                        g_RequiredPowersAtCreation.eaRequiredPowers[i]->iNumberOfRequiredPower,
                        numPowers
                        );
                    PERFINFO_AUTO_STOP_FUNC();
                    return false;
                }
            }
        }


        // Lock in their initial points spent
        character_UpdatePointsSpentPowerTrees(playerEnt->pChar, false);

        if (!gameSpecific_PostInitNewCharacter(playerEnt, characterCreationData, pExtract))
        {
            entity_DestroyLocalGameAccountDataExtract(&pExtract);
            NEWCHARFAIL(
                "Game-specific post-initialization failed.");
            PERFINFO_AUTO_STOP_FUNC();
            return false;
        }

        // Clear the item IDs, since the entity doesn't have a valid container ID yet
        inv_ClearItemIDsNoConst(playerEnt);

        entity_DestroyLocalGameAccountDataExtract(&pExtract);

        // Initialize the loose UI string.
        estrClear(&s_looseUIString);
        ParserWriteText(&s_looseUIString, parse_PlayerLooseUI, playerEnt->pPlayer->pUI->pLooseUI, 0, 0, 0);
        estrClear(&playerEnt->pPlayer->pUI->pchLooseUI);
        estrBase64Encode(&playerEnt->pPlayer->pUI->pchLooseUI, s_looseUIString, estrLength(&s_looseUIString));

        PERFINFO_AUTO_STOP_FUNC();
        return true;
    }
}

AUTO_STARTUP(NewCharacterValidation);
void aslLoginServerLoadNewCharacterValidation(void)
{
    ExprFuncTable* stFuncTable = exprContextCreateFunctionTable();
    S32 i;
    loadstart_printf("Loading new character validation expressions... ");
    s_pNewCharacterContext = exprContextCreate();
    exprContextSetFuncTable(s_pNewCharacterContext, stFuncTable);
    exprContextAddFuncsToTableByTag(stFuncTable, "util");
    exprContextAddFuncsToTableByTag(stFuncTable, "entityutil");
    exprContextSetPointerVar(s_pNewCharacterContext, "Entity", NULL, parse_Entity, true, true);
    ParserLoadFiles(NULL, "server/NewCharacterValidation.exprs", "NewCharacterValidation.bin", 0, parse_ExpressionList, &s_NewCharacterExpressions);
    for (i = eaSize(&s_NewCharacterExpressions.expressions) - 1; i >= 0; i--)
    {
        Expression *pExpr = s_NewCharacterExpressions.expressions[i];
        if (!exprGenerate(pExpr, s_pNewCharacterContext))
        {
            ErrorFilenamef(s_NewCharacterExpressions.filename, "Invalid new character validation expression, removing.");
            eaRemove(&s_NewCharacterExpressions.expressions, i);
        }
    }
    loadend_printf("Done. (%d)", eaSize(&s_NewCharacterExpressions.expressions));
}