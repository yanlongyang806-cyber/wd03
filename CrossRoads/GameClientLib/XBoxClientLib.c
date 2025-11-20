/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#if _XBOX

#include "XBoxClientLib.h"

#include <Xtl.h>
#pragma comment (lib, "Xnet.lib")
#pragma comment (lib, "XOnline.lib")

#include "gclEntity.h"
#include "XBoxStructs.h"
#include "XBoxStructs_h_ast.h"
#include "AutoTransDefs.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

bool gcl_xBoxSetPlayerXnAddr(void)
{
	// XUID
	XUID xuid = 0;

	// Player XNADDR
	XNADDR xnAddr;

	// Internal representation of XNADDR
	CrypticXnAddr crypticXnAddr;

	// Get the active player
	Entity *pActivePlayer = entActivePlayerPtr();

	if (pActivePlayer == NULL)
	{
		return false;
	}

	// Get the XUID
	if (XUserGetXUID(0, &xuid) != ERROR_SUCCESS)
	{
		return false;
	}

	// See if we can retrieve the XNADDR
	if (XNetGetTitleXnAddr(&xnAddr) == XNET_GET_XNADDR_PENDING)
	{
		return false;
	}

	// Do the conversion for XNADDR
	xBoxStructConvertToCrypticXnAddr(&xnAddr, CONTAINER_NOCONST(CrypticXnAddr, &crypticXnAddr));

	// Call the game server command to update the XNADDR for the player
	ServerCmd_xBox_setPlayerXnAddr(&crypticXnAddr, (U64)xuid);

	return true;
}

#endif