/***************************************************************************
*     Copyright (c) 2013, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslGamePermissions.h"
#include "Entity.h"
#include "Player.h"
#include "GameAccountData/GameAccountData.h"
#include "GamePermissionsCommon.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"

void
gslGamePermissions_EntityLevelUp(Entity *playerEnt, int oldLevel, int newLevel)
{
    INT_EARRAY levelTable = GamePermissions_GetLevelList();
    int i;
    bool needUpdate = false;
    GameAccountData *gameAccountData;

    for ( i = ea32Size(&levelTable) - 1; i >= 0; i-- )
    {
        if ( levelTable[i] > oldLevel && levelTable[i] <= newLevel )
        {
            // This level up may give additional gamepermission tokens, so we need to refresh game account data.
            needUpdate = true;
            break;
        }
    }

    if ( needUpdate && playerEnt && playerEnt->pPlayer && playerEnt->pPlayer->pPlayerAccountData )
    {
        gameAccountData = GET_REF(playerEnt->pPlayer->pPlayerAccountData->hData);
        if ( gameAccountData )
        {
            PooledStringArrayStruct extraPermissions = {0};
            extraPermissions.eaStrings = (char **)gameAccountData->cachedGamePermissionsFromAccountPermissions;

            // Call the login server to refresh game account data.
            RemoteCommand_aslLogin2_RefreshGADCmd(NULL, GLOBALTYPE_LOGINSERVER, SPECIAL_CONTAINERID_RANDOM, 
                playerEnt->pPlayer->pPlayerAccountData->iAccountID, playerEnt->pPlayer->playerType, 
                gameAccountData->bLifetimeSubscription, gameAccountData->bPress, newLevel, &extraPermissions);
        }
    }
}