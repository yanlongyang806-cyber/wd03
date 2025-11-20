/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GCLHANDLEMSG_H_
#define GCLHANDLEMSG_H_

typedef struct Entity Entity;

#include "GlobalComm.h"

// Handle top-level packets during gameplay (as opposed to during login)
void gclHandlePacketFromGameServer(Packet *pak, int cmd, NetLink *link, void *user_data);

// Handle per-entity messages
int gclHandleMsg(Packet* pak, int cmd, Entity *ent);
int gclHandleMsgFromReplay(char* msg, U32 iFlags, int cmd, Entity* ent);

// This is the per-project version, and must be implemented by each game
int GameClientHandlePktMsg(Packet* pak, int cmd, Entity *ent);

// This is the per-project version
int GameClientHandlePktInput(Packet* pak, int cmd);

#endif
