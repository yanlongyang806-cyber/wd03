/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslInterior.h"
#include "Entity.h"
#include "ExpressionMinimal.h"
#include "gslMechanics.h"
#include "WorldGrid.h"
#include "MapDescription.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslInterior_SetInterior) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Interior);
void 
gslInterior_SetInteriorCmd(Entity *pEnt, ContainerID petContainerID, const char *interiorDefName)
{
	gslInterior_SetInterior(pEnt, petContainerID, interiorDefName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslInterior_SetInteriorAndOption) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Interior);
void 
gslInterior_SetInteriorAndOptionCmd(Entity *pEnt, ContainerID petContainerID, const char *interiorDefName, const char *optionName, const char *choiceName)
{
	gslInterior_SetInteriorAndOption(pEnt, petContainerID, interiorDefName, optionName, choiceName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslInterior_SetInteriorAndSetting) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Interior);
void 
gslInterior_SetInteriorAndSettingCmd(Entity *pEnt, ContainerID petContainerID, const char *interiorDefName, const char *settingName)
{
	gslInterior_SetInteriorAndSetting(pEnt, petContainerID, interiorDefName, settingName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslInterior_MoveToActiveInterior) ACMD_SERVERCMD ACMD_CATEGORY(Interior) ACMD_PRIVATE;
void 
gslInterior_MoveToActiveInteriorCmd(Entity *pEnt, const char* pchSet)
{
	gslInterior_MoveToActiveInterior(pEnt, pchSet);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslInterior_IsCurrentMapPlayerCurrentInterior) ACMD_SERVERCMD ACMD_CATEGORY(Interior) ACMD_PRIVATE;
void
gslInterior_IsCurrentMapPlayerCurrentInteriorCmd(Entity *pEnt)
{
	 ClientCmd_gclInterior_SetCurrentMapPlayersInterior(pEnt, gslInterior_IsCurrentMapPlayerCurrentInterior(pEnt));
}

AUTO_COMMAND_REMOTE ACMD_NAME(gslInterior_InviteeInvite) ACMD_IFDEF(GAMESERVER);
void
gslInterior_InviteeInviteCmd(ContainerID inviteeID, InteriorInvite *inviteIn, U32 inviterAccountID)
{
	gslInterior_InviteeInvite(inviteeID, inviteIn, inviterAccountID);
}

// Invite another player to your hideout/bridge
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Interior) ACMD_NAME(gslInterior_Invite,InteriorInvite,HideoutInvite) ACMD_HIDE ACMD_PRODUCTS(StarTrek, FightClub);
void gslInterior_InviteCmd(SA_PARAM_NN_VALID Entity *pEnt, ACMD_SENTENCE pcOtherPlayer)
{
	gslInterior_InviteByName(pEnt, pcOtherPlayer);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslInterior_AcceptInvite) ACMD_SERVERCMD ACMD_CATEGORY(Interior) ACMD_HIDE ACMD_PRODUCTS(StarTrek, FightClub);
void
gslInterior_AcceptInviteCmd(Entity *pEnt)
{
	gslInterior_AcceptInvite(pEnt);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslInterior_DeclineInvite) ACMD_SERVERCMD ACMD_CATEGORY(Interior) ACMD_HIDE ACMD_PRODUCTS(StarTrek, FightClub);
void
gslInterior_DeclineInviteCmd(Entity *pEnt)
{
	gslInterior_DeclineInvite(pEnt);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslInterior_ExpelGuest) ACMD_SERVERCMD ACMD_CATEGORY(Interior) ACMD_PRIVATE;
void
gslInterior_ExpelGuestCmd(Entity *pEnt, EntityRef guestRef)
{
	gslInterior_ExpelGuest(pEnt, guestRef);
}

//
// Expression that designers can use to determine the name of the map that the current map owner
//  came from.  Used to determine which texture to use on the view screen in starship bridge interiors.
//
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(MapOwnerReturnMap);
const char *
exprFuncMapOwnerReturnMap(ACMD_EXPR_PARTITION iPartitionIdx)
{
    return gslInterior_GetMapOwnerReturnMap(iPartitionIdx);
}

AUTO_COMMAND ACMD_NAME(gslInterior_ClearData);
void gslInterior_ClearDataCmd(Entity *pEnt)
{
	gslInterior_ClearData(pEnt,0);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(gslInterior_UseFreePurchase) ACMD_PRIVATE;
void gslInterior_UseFreePurchaseCmd(Entity *pEnt, const char *pchSetting)
{
	gslInterior_UseFreePurchase(pEnt, pchSetting);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(gslInterior_Randomize, Hideout_Randomize);
void gslInteriorCmd_Randomize(Entity *pEnt)
{
	gslInterior_Randomize(pEnt);
}

