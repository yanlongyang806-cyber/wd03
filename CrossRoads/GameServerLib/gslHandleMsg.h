/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GSLHANDLEMSG_H_
#define GSLHANDLEMSG_H_

#include "GlobalComm.h"

typedef struct ClientLink ClientLink;

typedef enum PlayerLoginWait
{
	kLoginSuccess_None			=	0,
	kLoginSuccess_Fixup			=	1,
	kLoginSuccess_PuppetSwap	=	2,
	kLoginSuccess_GameAccount	=	4,
	kLoginSuccess_InvBagFixup	=	8,
} PlayerLoginWait;

void HandlePlayerLogin_EarlyEntityTasks(Entity *pEnt);
void HandlePlayerLogin_EntityTasks(Entity *pEntity);
void HandleReadyForGeneralUpdates(ClientLink *clientLink);
void HandleDoneLoading_Entity(Entity *pEnt);

void HandlePlayerLogin_Success(Entity *pEntity, PlayerLoginWait eType);

// Handle top-level packets
void gslHandleInput(Packet *pak, int cmd, NetLink *link, ClientLink *cLink);

// Handle per-entity packets
int gslHandleMsg(Packet* pak, int cmd, Entity *ent);

// This is the per-project version, and must be implemented by each game
int GameServerHandlePktMsg(Packet* pak, int cmd, Entity *ent);

// This is the per-project version, and must be implemented by each game
int GameServerHandlePktInput(Packet* pak, int cmd);

LATELINK;
void SendClientGameSpecificLoginInfo(Entity *pEntity);

#endif