/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GSLEXTERN_H_
#define GSLEXTERN_H_

// This file gives function definitions for all the functions that must
// be defined in a solution for GameServerLib to link properly

#include "EntityExtern.h"

typedef struct FrameLockedTimer FrameLockedTimer;
typedef enum LogoffType LogoffType;

// Does game-specific initialization
void gslExternInit(void);

// Loads any game-specific systems after the map has been loaded
void gslExternLoadPostMapLoad(void);

// Functions called when a player logs in, out, or is saved
void gslExternPlayerLogin(Entity *ent);
void gslExternPlayerLogout(Entity *ent, LogoffType eLogoffType);
void gslExternPlayerSave(Entity *ent, bool bRunTransact);

bool gslExternPlayerLoadLooseUI(Entity* ent);

// Called once per frame
void gslExternOncePerFrame(FrameLockedTimer* flt);

// Replace variables in commands based on game-specific code
char *gslExternExpandCommandVar(const char *variable,int *cmdlen,Entity *client);

// Initialize the entity on the client and server
void gslExternInitializeEntity(Entity * ent);
void gslExternCleanupEntity(int iPartitionIdx, Entity * ent);

// Return true if the given ent is detectable by the clientLink
bool gslExternEntityDetectable(ClientLink *pLink, Entity *ent);

#endif