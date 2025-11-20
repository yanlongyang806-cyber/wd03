/***************************************************************************
*     Copyright (c) 2005-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "EntDebugMenu.h"
#include "textparser.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_COMMAND ACMD_SERVERCMD;
void RequestDebugMenu(Entity* playerEnt)
{
	if (playerEnt)
	{
		DebugMenuItem* rootItem = StructCreate(parse_DebugMenuItem);
		debugmenu_AddLocalCommands(playerEnt, rootItem);
		ClientCmd_RefreshDebugMenu(playerEnt, rootItem);
		StructDestroy(parse_DebugMenuItem, rootItem);
	}
}