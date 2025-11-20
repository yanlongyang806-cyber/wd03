#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef ENTITYLIB_H_
#define ENTITYLIB_H_

#include "Entity.h"

typedef struct Container Container;

extern bool gbAmGameServer;

// Flags for which entity bucket chunks are being sent, keep in sync with ENT_BUCKET_BITS
typedef enum EntityBucketFlags
{
	ENT_BUCKET_SEND = 1 << 0,
	ENT_BUCKET_ENTITYFLAGS = 1 << 1,
	ENT_BUCKET_MODSNET = 1 << 2,
	ENT_BUCKET_ATTRIBS = 1 << 3,
	ENT_BUCKET_ATTRIBS_INNATE = 1 << 4,
	ENT_BUCKET_INVENTORY = 1 << 5,
	ENT_BUCKET_LIMITEDUSE = 1 << 6,
	ENT_BUCKET_CHARGEDATA = 1 << 7,
} EntityBucketFlags;

#define ENT_BUCKET_BITS 8

// Run once
void entityLibStartup(void); 

// Run each frame
void entityLibOncePerFrame(F32 fFrameTime);

// Reset the entity lib state, which is useful when re-entering gameplay
void entityLibResetState(void);

// Gets an entity from a partition, db id, and ent type
Entity *entFromContainerID(int iPartitionIdx, GlobalType type, ContainerID id);
Entity *entFromContainerIDAnyPartition(GlobalType type, ContainerID id);

// Gets an entity from an account id (player only)
Entity* entFromAccountID(ContainerID acctid);

// Gets an fake, subscribed entity from db id and ent type
Entity *entSubscribedCopyFromContainerID(GlobalType type, ContainerID id);

// Are we on the server?
static __forceinline bool entIsServer(void) { return gbAmGameServer; }

// Private entity functions. You should call the server/client versions of these instead!

// Creates a new entity. You do this on the server
Entity *entCreateNew(GlobalType type, char *pComment);

// Creates a new entity, using an EntityRef. You do this on the client
Entity *entCreateNewFromEntityRef(GlobalType type, EntityRef ref, char *pComment);

// Destroys an existing entity
#define entDestroy(ent) entDestroyEx(ent, __FILE__, __LINE__)
int entDestroyEx(Entity *ent, const char* file, int line);

//given an Entity* that has been created outside the entity system, 
//adds it to the entity system as if it had been created internally
void entRegisterExisting(Entity *pEnt);

// Container functions

// Request the container corresponding to an ent
Container *entGetContainer(Entity *e);

// List available for debugging that contains all of the entities currently allocated
// Many of these entities will be invalid at various points.
Entity *gpEntityList[MAX_ENTITIES_PRIVATE];

// Return the primary mission for this (player) entity. This can be NULL. It is either the primary mission for the tream or primary solo mission (mission info)
// Returns a pooled string
const char *entGetPrimaryMission(Entity *pEnt);

// Determines if entity from is on entity to's specified whitelist
bool entIsWhitelisted(Entity* to, Entity* from, S32 eWhitelistFlag);
bool entIsWhitelistedEx(Entity* to, U32 uFromID, U32 uFromAcctID, S32 eWhitelistFlag);
bool entIsWhitelistedWithPreCalculatedFriendStatus(Entity* to, U32 uFromID, U32 uFromAcctID, S32 eWhitelistFlag, bool bAreFriends);

// This function was written specifically for gateway. Because you don't have a direct entity pointer in gateway, you will need
// to use the subscription copy of the entity to send the client command. 
Entity *entForClientCmd(ContainerID id, Entity *pEnt);
#endif