/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GameClientHandleMsg.h"
#include "error.h"

int GameClientHandlePktMsg(Packet* pak, int cmd, Entity *ent)
{
	return 1;
}

int GameClientHandlePktInput(Packet* pak, int cmd)
{
	switch (cmd)
	{
	xdefault:
		Errorf("Invalid Server msg received: %d", cmd);
		return 0;
	}
	return 1;
}