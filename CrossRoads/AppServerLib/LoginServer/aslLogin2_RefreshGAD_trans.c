/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLogin2_RefreshGAD.h"
#include "stdtypes.h"
#include "TransactionOutcomes.h"
#include "GameAccountData/GameAccountData.h"
#include "ResourceInfo.h"
#include "earray.h"
#include "species_common.h"
#include "GamePermissionsCommon.h"
#include "GamePermissionTransactions.h"
#include "timing.h"

#include "AutoGen/GameAccountData_h_ast.h"
#include "AutoGen/accountnet_h_ast.h"

extern ParseTable parse_AccountProxyKeyValueInfoContainer[];

// Build the species unlock list once and cache it.
static SpeciesUnlockList *
GetSpeciesUnlockList()
{
    static SpeciesUnlockList *s_SpeciesList = NULL;
    if ( s_SpeciesList == NULL )
    {
        DictionaryEArrayStruct *deas = resDictGetEArrayStruct("Species");
        int i;

        s_SpeciesList = StructCreate(parse_SpeciesUnlockList);

        for(i = eaSize(&deas->ppReferents)-1; i >= 0; --i)
        {
            SpeciesDef *species = (SpeciesDef*)deas->ppReferents[i];
            SpeciesUnlock *pSpeciesUnlock = NULL;
            if (!species->pcUnlockCode || !(*species->pcUnlockCode) )
                continue;

            pSpeciesUnlock = StructCreate(parse_SpeciesUnlock);
            pSpeciesUnlock->pchSpeciesName = species->pcName;
            pSpeciesUnlock->pchSpeciesUnlockCode = StructAllocString(species->pcUnlockCode);
            eaPush(&s_SpeciesList->eaSpeciesUnlocks, pSpeciesUnlock);
        }
    }

    return s_SpeciesList;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccountData, ".Blifetimesubscription, .Bpress, .Blinkedaccount, .Bshadowaccount, .Idayssubscribed, .Ufirstplayedtime, .Utotalplayedtime_Accountserver, .Ulastrefreshtime, .Eatokens, .Eaaccountkeyvalues, .Iversion, .Eacostumekeys, .Eakeys, .Eapermissions, .Umaxcharacterlevelcached, .Bbilled, .Cachedgamepermissionsfromaccountpermissions");
enumTransactionOutcome 
aslLogin2_tr_RefreshGAD(ATR_ARGS, NOCONST(GameAccountData) *pAccountData, ParsedAVPList *pAttribList, NON_CONTAINER GamePermissionDefs *pTempPermissions,
    S32 bLifetime, S32 bPress, AccountProxyAccountDataResponse *accountServerData, PooledStringArrayStruct *gamePermissionsFromAccountPermissions, U32 overrideLevel)
{
    U32 daysSubscribed;
    U32 currentTime = timeSecondsSince2000();
    int index;

    pAccountData->uLastRefreshTime = currentTime;

    // Update account unlocks.
    if(pAttribList)
    {
        SpeciesUnlockList *pSpeciesList = GetSpeciesUnlockList();
        EARRAY_CONST_FOREACH_BEGIN(pAttribList->eaPairs, i, s);
        ParsedAVP *pPair = eaGet(&pAttribList->eaPairs, i);
        if(pPair && pPair->pchValue && atoi(pPair->pchValue) > 0)
        {
            switch(pPair->eType)
            {
            default:
                break;
            case kMicroItemType_PlayerCostume:
                {
                    slGAD_trh_UnlockCostumeRef_Force(ATR_PASS_ARGS, pAccountData, pPair->pchAttribute);
                    break;
                }
            case kMicroItemType_Costume:
                {
                    slGAD_trh_UnlockCostumeItem_Force(ATR_PASS_ARGS, pAccountData, pPair->pchAttribute);
                    break;
                }
            case kMicroItemType_Species:
                {
                    SpeciesUnlock *pSpecies = eaIndexedGetUsingString(&pSpeciesList->eaSpeciesUnlocks,pPair->pchItemIdent);
                    if(pSpecies)
                        slGAD_trh_UnlockSpecies_Force(ATR_PASS_ARGS, pAccountData, pSpecies, pSpecies->pchSpeciesUnlockCode);
                    break;
                }
            case kMicroItemType_AttribValue:
                {
                    char *pchAttrib = estrStackCreateFromStr(pPair->pchItemIdent);
                    estrReplaceOccurrences(&pchAttrib, " ", ".");
                    slGAD_trh_UnlockAttribValue_Force(ATR_PASS_ARGS, pAccountData, pchAttrib);
                    estrDestroy(&pchAttrib);
                    break;
                }
            }
        }
        EARRAY_FOREACH_END;
    }

    // Update account flags.
    pAccountData->bLifetimeSubscription = bLifetime;
    pAccountData->bPress = bPress;
    pAccountData->bLinkedAccount = accountServerData->bLinkedAccount;
    pAccountData->bShadowAccount = accountServerData->bShadowAccount;

    // Update the cached max level.  Use the maximum of account server level, game account data level and override level.
    pAccountData->uMaxCharacterLevelCached = MAX(MAX(overrideLevel, accountServerData->uHighestLevel), pAccountData->uMaxCharacterLevelCached);
    pAccountData->bBilled = accountServerData->bBilled;

    // Update the cache of extra game permissions that come from account permissions
    eaClear(&pAccountData->cachedGamePermissionsFromAccountPermissions);
    for ( index = eaSize(&gamePermissionsFromAccountPermissions->eaStrings) - 1; index >= 0; index-- )
    {
        eaPush(&pAccountData->cachedGamePermissionsFromAccountPermissions, (char *)gamePermissionsFromAccountPermissions->eaStrings[index]);
    }

    // Days Subscribed only gets higher.
    daysSubscribed = (accountServerData->uSubscribedSeconds + (SECONDS_PER_DAY-1)) / SECONDS_PER_DAY;
    if( daysSubscribed > pAccountData->iDaysSubscribed )
    {
        pAccountData->iDaysSubscribed = daysSubscribed;
    }

    // Update first played time.  Will only get lower.
    if (!pAccountData->uFirstPlayedTime || accountServerData->uFirstPlayedSS2000 < pAccountData->uFirstPlayedTime)
    {
        pAccountData->uFirstPlayedTime = accountServerData->uFirstPlayedSS2000;
    }

    // Update total played time.  Will only get higher.
    if (!pAccountData->uTotalPlayedTime_AccountServer || accountServerData->uTotalPlayedSS2000 > pAccountData->uTotalPlayedTime_AccountServer)
    {
        pAccountData->uTotalPlayedTime_AccountServer = accountServerData->uTotalPlayedSS2000;
    }

    // Clear out previous game permission tokens.
    eaClearStructNoConst(&pAccountData->eaTokens, parse_GameToken);

    // Create the new tokens based on the given permissions.
    if(!GamePermissions_trh_CreateTokens_Force(pAccountData, pTempPermissions))
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Clear out the previous copy of account server key/values.
    eaClearStructNoConst(&pAccountData->eaAccountKeyValues, parse_AccountProxyKeyValueInfoContainer);

    // Copy the key/values into the GameAccountData container.
    // Need to use the no type check version because there are container and non-container versions of the key/value struct.

    if ( accountServerData && accountServerData->pKeyValues )
    {
        eaCopyStructsVoidNoTypeCheck(&accountServerData->pKeyValues->ppList, &pAccountData->eaAccountKeyValues, parse_AccountProxyKeyValueInfoContainer);
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}
