#include "GamePermissionsCommon.h"
#include "GamePermissionsCommon_h_ast.h"

#include "StringUtil.h"
#include "utilitiesLib.h"
#include "timing.h"
#include "error.h"
#include "file.h"
#include "textparser.h"
#include "ShardCommon.h"
#include "Alerts.h"

#include "AutoGen/GamePermissionsCommon_h_ast.h"

// This file handles loading of GamePermissions.
// It is not used by the client.  Clients now get their GamePermissions from the loginserver when they first connect.

static void GamePermissions_DoToFromDate(void)
{
    S32 i, j;

    for(i = 0; i < eaSize(&g_GamePermissions.eaTimedTokenList); ++i)
    {
        for(j = 0; j < eaSize(&g_GamePermissions.eaTimedTokenList[i]->eaFromToDates); ++j)
        {
            if(g_GamePermissions.eaTimedTokenList[i]->eaFromToDates[j]->pcFromDateString && g_GamePermissions.eaTimedTokenList[i]->eaFromToDates[j]->pcFromDateString[0])
            {
                g_GamePermissions.eaTimedTokenList[i]->eaFromToDates[j]->uFromTimeSeconds = timeGetSecondsSince2000FromDateString(g_GamePermissions.eaTimedTokenList[i]->eaFromToDates[j]->pcFromDateString);
            }

            if(g_GamePermissions.eaTimedTokenList[i]->eaFromToDates[j]->pcToDateString && g_GamePermissions.eaTimedTokenList[i]->eaFromToDates[j]->pcToDateString[0])
            {
                g_GamePermissions.eaTimedTokenList[i]->eaFromToDates[j]->uToTimeSeconds = timeGetSecondsSince2000FromDateString(g_GamePermissions.eaTimedTokenList[i]->eaFromToDates[j]->pcToDateString);
            }

            if(g_GamePermissions.eaTimedTokenList[i]->eaFromToDates[j]->uFromTimeSeconds == 0)
            {
                loadupdate_printf("from [%s] is an invalid date.  Didn't get loaded.", g_GamePermissions.eaTimedTokenList[i]->eaFromToDates[j]->pcFromDateString);
            }
            if(g_GamePermissions.eaTimedTokenList[i]->eaFromToDates[j]->uToTimeSeconds == 0)
            {
                loadupdate_printf("to [%s] is an invalid date.  Didn't get loaded.", g_GamePermissions.eaTimedTokenList[i]->eaFromToDates[j]->pcFromDateString);
            }
        }
    }
}

static void GamePermissionValidation(GamePermissionDefs *pGamePermissions)
{
    // Check timed permissions to make sure none of them have the same name
    S32 i, j, iP1, iP2;

    for(i = 0; i < eaSize(&pGamePermissions->eaTimedTokenList); ++i)
    {
        for(iP1 = 0; iP1 < eaSize(&pGamePermissions->eaTimedTokenList[i]->eaPermissions); ++iP1)
        {
            // check all other timed permissions
            for(j = 0; j < eaSize(&pGamePermissions->eaTimedTokenList); ++j)
            {
                for(iP2 = 0; iP2 < eaSize(&pGamePermissions->eaTimedTokenList[j]->eaPermissions); ++iP2)
                {
                    if(i != j || iP1 != iP2)
                    {
                        // do the check
                        if(stricmp(pGamePermissions->eaTimedTokenList[i]->eaPermissions[iP1]->pchName, pGamePermissions->eaTimedTokenList[j]->eaPermissions[iP2]->pchName) == 0)
                        {
                            Errorf("Timed Game permission same name %s at index %d and %d.", pGamePermissions->eaTimedTokenList[i]->eaPermissions[iP1]->pchName, i, j);
                        }
                    }
                }
            }
        }
    }
}

bool CompareBagRestrictionsCB(GamePermissionBagRestriction *restriction1, GamePermissionBagRestriction *restriction2)
{
    return ( restriction1->eBagID == restriction2->eBagID );
}

bool ComparePermissionsCB(GamePermissionDef *permission1, GamePermissionDef *permission2)
{
    if (stricmp_safe(permission1->pchName, permission2->pchName) == 0)
    {
        return true;
    }

    return false;
}

bool CompareTimedTokenListCB(GamePermissionTimed *permissionTimed1, GamePermissionTimed *permissionTimed2)
{
    int i;

    if ( ( permissionTimed1->iHours != permissionTimed2->iHours ) || 
        ( permissionTimed1->iDaysSubscribed != permissionTimed2->iDaysSubscribed ) || 
        ( permissionTimed1->iStartSeconds != permissionTimed2->iStartSeconds ) || 
        ( eaSize(&permissionTimed1->eaFromToDates) != eaSize(&permissionTimed2->eaFromToDates) ) )
    {
        return false;
    }

    for( i = eaSize(&permissionTimed1->eaFromToDates) - 1; i >= 0; i-- )
    {
        if ( ( permissionTimed1->eaFromToDates[i]->uFromTimeSeconds != permissionTimed2->eaFromToDates[i]->uFromTimeSeconds ) ||
            ( permissionTimed1->eaFromToDates[i]->uToTimeSeconds != permissionTimed2->eaFromToDates[i]->uToTimeSeconds ) )
        {
            return false;
        }
    }

    return true;
}

bool GamePermissions_LoadFromText(char **ppErrorString, char **ppCommentString)
{
    char fileName[CRYPTIC_MAX_PATH];
    char *pClusterName;
    char located[CRYPTIC_MAX_PATH];
    char *bRet;
    EARRAY_OF(GamePermissionDef) eaTmpPermissions = NULL;
    EARRAY_OF(GamePermissionBagRestriction) eaTmpBagRestrictions = NULL;
    bool devPermissionsWereRead = false;

    if ( isDevelopmentMode() )
    {
        // Allow for dev mode specific permissions that get loaded instead of the standard permissions.
        sprintf(fileName, "server/GamePermissions_dev.txt");
        bRet = fileLocateRead(fileName, located);

        devPermissionsWereRead = ParserReadTextFile(fileName, parse_GamePermissionDefs, &g_GamePermissions, 0);

        if ( devPermissionsWereRead )
        {
            estrConcatf(ppCommentString, "Loading from %s\n", located);

            // Move contents of indexed arrays into un-indexed temp arrays.
            while(eaSize(&g_GamePermissions.eaPermissions))
            {
                eaPush(&eaTmpPermissions, eaPop(&g_GamePermissions.eaPermissions));
            }
            while(eaSize(&g_GamePermissions.eaInvBagRestrictions))
            {
                eaPush(&eaTmpBagRestrictions, eaPop(&g_GamePermissions.eaInvBagRestrictions));
            }
        }
    }

    if ( !devPermissionsWereRead )
    {
        sprintf(fileName, "server/GamePermissions.txt");
        bRet = fileLocateRead(fileName, located);
        estrConcatf(ppCommentString, "Loading from %s\n", located);
        if (!ParserReadTextFile(fileName, parse_GamePermissionDefs, &g_GamePermissions, 0))
        {
            char *pTempString = NULL;
            ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
            ParserReadTextFile(fileName, parse_GamePermissionDefs, &g_GamePermissions, 0);
            ErrorfPopCallback();
            estrPrintf(ppErrorString, "Text Parser error while reading %s: %s", fileName, pTempString);

            estrDestroy(&pTempString);
            return false;
        }

        // Move contents of indexed arrays into un-indexed temp arrays.
        while(eaSize(&g_GamePermissions.eaPermissions))
        {
            eaPush(&eaTmpPermissions, eaPop(&g_GamePermissions.eaPermissions));
        }
        while(eaSize(&g_GamePermissions.eaInvBagRestrictions))
        {
            eaPush(&eaTmpBagRestrictions, eaPop(&g_GamePermissions.eaInvBagRestrictions));
        }
    }

	pClusterName = ShardCommon_GetClusterName();
    if (pClusterName && pClusterName[0])
    {

        sprintf(fileName, "server/GamePermissions_%s.txt", pClusterName);
        bRet = fileLocateRead(fileName, located);
        if (fileExists(fileName))
        {
            estrConcatf(ppCommentString, "Loading from %s\n", located);
            if (!ParserReadTextFile(fileName, parse_GamePermissionDefs, &g_GamePermissions, 0))
            {
                char *pTempString = NULL;
                ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
                ParserReadTextFile(fileName, parse_GamePermissionDefs, &g_GamePermissions, 0);
                ErrorfPopCallback();
                estrPrintf(ppErrorString, "Text Parser error while reading %s: %s", fileName, pTempString);

                estrDestroy(&pTempString);
                return false;
            }
        }
        else
        {
            estrConcatf(ppCommentString, "NOT loading from %s, it does not exist\n", fileName);
        }
    }

    // Move contents of indexed arrays into un-indexed temp arrays.
    while(eaSize(&g_GamePermissions.eaPermissions))
    {
        eaPush(&eaTmpPermissions, eaPop(&g_GamePermissions.eaPermissions));
    }
    while(eaSize(&g_GamePermissions.eaInvBagRestrictions))
    {
        eaPush(&eaTmpBagRestrictions, eaPop(&g_GamePermissions.eaInvBagRestrictions));
    }

    sprintf(fileName, "server/GamePermissions_%s.txt", GetShardNameFromShardInfoString());
    bRet = fileLocateRead(fileName, located);
    if (fileExists(fileName))
    {
        estrConcatf(ppCommentString, "Loading from %s\n", located);
        if (!ParserReadTextFile(fileName, parse_GamePermissionDefs, &g_GamePermissions, 0))
        {
            char *pTempString = NULL;
            ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
            ParserReadTextFile(fileName, parse_GamePermissionDefs, &g_GamePermissions, 0);
            ErrorfPopCallback();
            estrPrintf(ppErrorString, "Text Parser error while reading %s: %s", fileName, pTempString);

            estrDestroy(&pTempString);
            return false;
        }
    }
    else
    {
        estrConcatf(ppCommentString, "NOT loading from %s, it does not exist\n", fileName);
    }

    // Move contents of indexed arrays into un-indexed temp arrays.
    while(eaSize(&g_GamePermissions.eaPermissions))
    {
        eaPush(&eaTmpPermissions, eaPop(&g_GamePermissions.eaPermissions));
    }
    while(eaSize(&g_GamePermissions.eaInvBagRestrictions))
    {
        eaPush(&eaTmpBagRestrictions, eaPop(&g_GamePermissions.eaInvBagRestrictions));
    }

    if (pClusterName && pClusterName[0])
    {

        sprintf(fileName, "server/GamePermissions_ClusterLocal.txt");
        bRet = fileLocateRead(fileName, located);
        if (fileExists(fileName))
        {
            estrConcatf(ppCommentString, "Loading from %s\n", located);
            if (!ParserReadTextFile(fileName, parse_GamePermissionDefs, &g_GamePermissions, 0))
            {
                char *pTempString = NULL;
                ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
                ParserReadTextFile(fileName, parse_GamePermissionDefs, &g_GamePermissions, 0);
                ErrorfPopCallback();
                estrPrintf(ppErrorString, "Text Parser error while reading %s: %s", fileName, pTempString);

                estrDestroy(&pTempString);
                return false;
            }
        }
        else
        {
            estrConcatf(ppCommentString, "NOT loading from %s, it does not exist\n", fileName);
        }

    }

    // Move contents of indexed arrays into un-indexed temp arrays.
    while(eaSize(&g_GamePermissions.eaPermissions))
    {
        eaPush(&eaTmpPermissions, eaPop(&g_GamePermissions.eaPermissions));
    }
    while(eaSize(&g_GamePermissions.eaInvBagRestrictions))
    {
        eaPush(&eaTmpBagRestrictions, eaPop(&g_GamePermissions.eaInvBagRestrictions));
    }

    sprintf(fileName, "server/GamePermissions_local.txt");
    bRet = fileLocateRead(fileName, located);
    if (fileExists(fileName))
    {
        estrConcatf(ppCommentString, "Loading from %s\n", located);
        if (!ParserReadTextFile(fileName, parse_GamePermissionDefs, &g_GamePermissions, 0))
        {
            char *pTempString = NULL;
            ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
            ParserReadTextFile(fileName, parse_GamePermissionDefs, &g_GamePermissions, 0);
            ErrorfPopCallback();
            estrPrintf(ppErrorString, "Text Parser error while reading %s: %s", fileName, pTempString);

            estrDestroy(&pTempString);
            return false;
        }
    }
    else
    {
        estrConcatf(ppCommentString, "NOT loading from %s, it does not exist\n", fileName);
    }

    // Move contents of indexed arrays into un-indexed temp arrays.
    while(eaSize(&g_GamePermissions.eaPermissions))
    {
        eaPush(&eaTmpPermissions, eaPop(&g_GamePermissions.eaPermissions));
    }
    while(eaSize(&g_GamePermissions.eaInvBagRestrictions))
    {
        eaPush(&eaTmpBagRestrictions, eaPop(&g_GamePermissions.eaInvBagRestrictions));
    }

    GamePermissions_DoToFromDate();

    eaRemoveDuplicates(&eaTmpBagRestrictions, CompareBagRestrictionsCB, parse_GamePermissionBagRestriction);
    eaRemoveDuplicates(&eaTmpPermissions, ComparePermissionsCB, parse_GamePermissionDef);
    eaRemoveDuplicates(&g_GamePermissions.eaTimedTokenList, CompareTimedTokenListCB, parse_GamePermissionTimed);

    // Move the contents of the temporary arrays back into the real arrays after they have had duplicates removed.
    while(eaSize(&eaTmpPermissions))
    {
        eaPush(&g_GamePermissions.eaPermissions, eaPop(&eaTmpPermissions));
    }
    while(eaSize(&eaTmpBagRestrictions))
    {
        eaPush(&g_GamePermissions.eaInvBagRestrictions, eaPop(&eaTmpBagRestrictions));
    }

    estrCopy(&g_GamePermissions.pLoadingComment, ppCommentString);

    return true;
}

static int *s_GamePermissionLevels = NULL;

int *
GamePermissions_GetLevelList(void)
{
    return s_GamePermissionLevels;
}

// After loading game permissions, create an array of all levels that may grant permissions.
static void
GamePermissions_FillLevelList(GamePermissionDefs *gamePermissionDefs)
{
    int i;
    int j;
    int k;
    GamePermissionDef *gamePermissionDef;
    ea32ClearFast(&s_GamePermissionLevels);

    for ( i = eaSize(&gamePermissionDefs->eaPermissions) - 1; i >= 0; i-- )
    {
        gamePermissionDef = gamePermissionDefs->eaPermissions[i];

        for ( j = eaSize(&gamePermissionDef->eaLevelRestrictedTokens) - 1; j >= 0; j-- )
        {
            LevelRestrictedTokens *levelRestrictedTokens = gamePermissionDef->eaLevelRestrictedTokens[j];
            ea32PushUnique(&s_GamePermissionLevels, levelRestrictedTokens->uMinAccountLevel);
        }
    }

    for ( i = eaSize(&gamePermissionDefs->eaTimedTokenList) - 1; i >= 0; i-- )
    {
        GamePermissionTimed *gamePermissionTimed = gamePermissionDefs->eaTimedTokenList[i];
        for ( j = eaSize(&gamePermissionTimed->eaPermissions) - 1; j >= 0; j-- )
        {
            gamePermissionDef = gamePermissionTimed->eaPermissions[j];
            for ( k = eaSize(&gamePermissionDef->eaLevelRestrictedTokens) - 1; k >= 0; k-- )
            {
                LevelRestrictedTokens *levelRestrictedTokens = gamePermissionDef->eaLevelRestrictedTokens[k];
                ea32PushUnique(&s_GamePermissionLevels, levelRestrictedTokens->uMinAccountLevel);
            }
        }
    }
}

AUTO_STARTUP(GamePermissions);
void GamePermissions_Load(void)
{
    char *errorString = NULL;
    char *commentString = NULL;

    loadstart_printf("Loading Game Permissions... ");

    if ( !GamePermissions_LoadFromText(&errorString, &commentString) )
    {
        TriggerAlert("ERROR_LOADING_GAMEPERMISSIONS", 
            STACK_SPRINTF("GamePermissions were not loaded due to an error.  %s", NULL_TO_EMPTY(errorString)), 
            ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);
    }
    GamePermissions_DoToFromDate();

    GamePermissions_SetBaseAndPremium();

    GamePermissionValidation(&g_GamePermissions);

    GamePermissions_FillLevelList(&g_GamePermissions);

    if ( errorString && errorString[0] )
    {

    }

    loadend_printf(" done (%d Permissions).", eaSize(&g_GamePermissions.eaPermissions));
}
