/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLogin2_GetCharacterChoices.h"
#include "aslLogin2_Booting.h"
#include "aslLogin2_GetCharacterDetail.h"
#include "Login2Common.h"
#include "earray.h"
#include "textparser.h"
#include "Entity.h"
#include "Login2CharacterDetail.h"

#include "AutoGen/Login2Common_h_ast.h"

static void
DebugGetCharacterChoicesCB(Login2CharacterChoices *characterChoices, void *userData)
{
    int i;
    if ( characterChoices )
    {
        printf("Got character choices for accountID %u\n", characterChoices->accountID);
        if ( eaSize(&characterChoices->missingShardNames) > 0 )
        {
            printf("Shards not responding:\n");
            for ( i = eaSize(&characterChoices->missingShardNames) - 1; i >= 0; i-- )
            {
                printf("  %s\n", characterChoices->missingShardNames[i]);
            }
        }
        if ( eaSize(&characterChoices->characterChoices) > 0 )
        {
            printf("Characters:\n");
            for ( i = eaSize(&characterChoices->characterChoices) - 1; i >= 0; i-- )
            {
                Login2CharacterChoice *characterChoice = characterChoices->characterChoices[i];

                printf("  %u@%u %s@%s, %s\n", characterChoice->containerID, characterChoice->accountID, characterChoice->savedName, characterChoice->pubAccountName, characterChoice->shardName);
                printf("    AccountName: %s\n", characterChoice->privAccountName);
                printf("    Virtual Shard: %d\n", characterChoice->virtualShardID);
                printf("    Level: %d\n", characterChoice->level);
                printf("    Fixup Version: %d\n", characterChoice->fixupVersion);
                printf("    Creation Time: %d\n", characterChoice->createdTime);
                printf("    Last Played Time: %d\n", characterChoice->lastPlayedTime);
                if ( characterChoice->isDeleted )
                {
                    printf("    Deleted\n");
                }
                if ( characterChoice->isOffline )
                {
                    printf("    Offline\n");
                }
            }
        }
        else
        {
            printf("No Characters Found.\n");
        }

        StructDestroy(parse_Login2CharacterChoices, characterChoices);
    }
    else
    {
        printf("Got NULL character choices.\n");
    }

}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void
aslLogin2_Debug_GetCharacterChoices(U32 accountID)
{
    aslLogin2_GetCharacterChoices(accountID, DebugGetCharacterChoicesCB, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void
aslLogin2_Debug_BootAccountFromAllLoginServers(U32 accountID, U32 excludeServerID)
{
    aslLogin2_BootAccountFromAllLoginServers(accountID, excludeServerID);
}

static void
DebugBootPlayerCB(bool success, void *userData)
{
    if ( success )
    {
        printf("Player boot succeeded.\n");
    }
    else
    {
        printf("Player boot failed.\n");
    }
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void
aslLogin2_Debug_BootPlayer(U32 playerID, const char *shardName, ACMD_SENTENCE message)
{
    aslLogin2_BootPlayer(playerID, shardName, message, DebugBootPlayerCB, NULL);
}

static void
DebugGetCharacterDetailCB(Login2CharacterDetail *characterDetail, void *userData)
{
    if ( characterDetail && characterDetail->playerEnt )
    {
        printf("Received character details for player %s\n", characterDetail->playerEnt->debugName);
    }
    else
    {
        printf("Did not receive character details\n");
    }
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void
aslLogin2_Debug_GetCharacterDetail(U32 accountID, U32 playerID, const char *shardName, bool returnActivePuppets)
{
    aslLogin2_GetCharacterDetail(accountID, playerID, shardName, returnActivePuppets, DebugGetCharacterDetailCB, NULL);
}