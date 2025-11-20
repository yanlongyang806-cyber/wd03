/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Login2Common.h"
#include "LoginCommon.h"
#include "stdtypes.h"
#include "GlobalTypes.h"
#include "GamePermissionsCommon.h"
#include "GameAccountData/GameAccountData.h"
#include "accountnet.h"

#include "AutoGen/GlobalTypeEnum_h_ast.h"
#include "AutoGen/Login2Common_h_ast.h"
#include "AutoGen/LoginCommon_h_ast.h"
#include "AutoGen/GlobalTypes_h_ast.h"

// The time in seconds that players subject to anti-addiction rules can play before getting booted.
U32 gAddictionMaxPlayTime = 0;
AUTO_CMD_INT(gAddictionMaxPlayTime, AddictionMaxPlayTime) ACMD_AUTO_SETTING(LoginServer, LOGINSERVER, GAMESERVER);

// Extract the a string from the extra header fields.
static const char *
GetExtraStringFromCharacterChoice(Login2CharacterChoice *characterChoice, int extraIndex)
{
    switch (extraIndex)
    {
    case 1:
        return characterChoice->extraData1;
    case 2:
        return characterChoice->extraData2;
    case 3:
        return characterChoice->extraData3;
    case 4:
        return characterChoice->extraData4;
    case 5:
        return characterChoice->extraData5;
    default:
        return NULL;
    }
}

// Extract the allegiance name used for character slot computations from the extra header fields.
const char *
Login2_GetAllegianceFromCharacterChoice(Login2CharacterChoice *characterChoice)
{
    return GetExtraStringFromCharacterChoice(characterChoice, gConf.iCharacterChoiceExtraHeaderForAllegiance);
}

// Extract the character class name used during character selection from the extra header fields.
const char *
Login2_GetClassNameFromCharacterChoice(Login2CharacterChoice *characterChoice)
{
    return GetExtraStringFromCharacterChoice(characterChoice, gConf.iCharacterChoiceExtraHeaderForClass);
}

// Extract the character path name used during character selection from the extra header fields.
const char *
Login2_GetPathNameFromCharacterChoice(Login2CharacterChoice *characterChoice)
{
    return GetExtraStringFromCharacterChoice(characterChoice, gConf.iCharacterChoiceExtraHeaderForPath);
}

// Extract the character species name used during character selection from the extra header fields.
const char *
Login2_GetSpeciesNameFromCharacterChoice(Login2CharacterChoice *characterChoice)
{
    return GetExtraStringFromCharacterChoice(characterChoice, gConf.iCharacterChoiceExtraHeaderForSpecies);
}

#ifndef OBJECTDB
// Get the base number of project slots as granted by gamepermissions.
int
Login2_GetUGCProjectBaseEditSlots(GameAccountData *gameAccountData)
{
    S32 iVal;
    if(gamePermission_Enabled())
    {
        if(!GetGamePermissionValueUncached(gameAccountData, GAME_PERMISSION_UGC_PROJECT_SLOTS, &iVal))
        {
            iVal = 0;
        }
    }
    else
    {
        iVal = 8;
    }
    return iVal;
}

// Get the total number of project slots, which includes slots from gamepermissions, game account data key/value and account server key/value.
int 
Login2_UGCGetProjectMaxSlots(GameAccountData *gameAccountData)
{
    char *extraMapCountValue;
    int extraMapSlots;
    int extraGADSlots;

    if ( gameAccountData == NULL )
    {
        return 0;
    }

    extraMapCountValue = AccountProxyFindValueFromKeyContainer(gameAccountData->eaAccountKeyValues, GetAccountUgcProjectExtraSlotsKey());
    extraMapSlots = (extraMapCountValue ? atoi(extraMapCountValue) : 0);
    extraGADSlots = gad_GetAttribInt(gameAccountData, GetAccountUgcProjectExtraSlotsKey());

    return Login2_GetUGCProjectBaseEditSlots(gameAccountData) + extraMapSlots + extraGADSlots;
}

// Get the total number of series slots, which includes slots from gamepermissions, game account data key/value and account server key/value.
int 
Login2_UGCGetSeriesMaxSlots(GameAccountData *gameAccountData)
{
    char *extraMapCountValue;
    int extraMapSlots;
    int extraGADSlots;

    extraMapCountValue = AccountProxyFindValueFromKeyContainer(gameAccountData->eaAccountKeyValues, GetAccountUgcProjectSeriesExtraSlotsKey());
    extraMapSlots = (extraMapCountValue ? atoi(extraMapCountValue) : 0);
    extraGADSlots = gad_GetAttribInt(gameAccountData, GetAccountUgcProjectSeriesExtraSlotsKey());

    return Login2_GetUGCProjectBaseEditSlots(gameAccountData) + extraMapSlots + extraGADSlots;
}
#endif
#include "AutoGen/Login2Common_h_ast.c"