/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GSLENTITYNET_H_
#define GSLENTITYNET_H_

typedef struct Packet Packet;
typedef struct ClientLink ClientLink;
typedef struct Entity Entity;
typedef struct SendEntityUpdateThreadData SendEntityUpdateThreadData;
typedef struct ImbeddedList ImbeddedList;

typedef enum EntityDeleteFlag {
	ENTITY_DELETE_NOFADE = 1 << 0,
} EntityDeleteFlag;

// Handles receiving and sending of entity updates, on the server

void gslEntityUpdateThreadDataCreate(SendEntityUpdateThreadData** tdOut);
void gslEntityUpdateThreadDataDestroy(SendEntityUpdateThreadData** tdInOut);

void gslEntityAddDeleteFlags(Entity* e, U8 flags);
void gslSendEntityUpdate(ClientLink *link, Packet *pak, SendEntityUpdateThreadData*const td);
void gslEntityUpdateBegin(void);
void gslEntityUpdateEnd(void);

S32 gslEntityUpdateEndInThread(	void* unused,
								S32 entityIndex,
								SendEntityUpdateThreadData** tds);

void gslPopulateNearbyPlayerNotifyEnts(	SendEntityUpdateThreadData* td,
										ImbeddedList *pList);

#endif