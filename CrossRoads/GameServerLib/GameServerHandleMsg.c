/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GameServerHandleMsg.h"
#include "error.h"

int GameServerHandlePktMsg(Packet* pak, int cmd, Entity *ent)
{
	return 1;
}

int GameServerHandlePktInput(Packet* pak, int cmd)
{
	Errorf("Invalid Server msg received: %d", cmd);
	return 0;
}