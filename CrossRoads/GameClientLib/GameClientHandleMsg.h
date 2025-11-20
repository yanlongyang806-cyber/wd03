#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GAMECLIENTHANDLEMSG_H_
#define GAMECLIENTHANDLEMSG_H_

#include "GlobalComm.h"

typedef struct Entity Entity;

int GameClientHandlePktMsg(Packet* pak, int cmd, Entity *ent);
int GameClientHandlePktInput(Packet* pak, int cmd);

#endif