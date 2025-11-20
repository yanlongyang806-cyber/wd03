/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AutoTransDefs.h"
#include "Entity.h"
#include "Player.h"
#include "autogen/GameServerLib_autotransactions_autogen_wrappers.h"

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
// This command sets the XNADDR for the player
void xBox_setPlayerXnAddr(Entity *pEnt, CrypticXnAddr *pXnAddr, U64 xuid)
{
	AutoTrans_gslXBox_trSetPlayerXnAddr(NULL, GLOBALTYPE_GAMESERVER, entGetType(pEnt), entGetContainerID(pEnt), pXnAddr, xuid);
}