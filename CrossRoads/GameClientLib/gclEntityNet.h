#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GCLENTITYNET_H_
#define GCLENTITYNET_H_

// Handles receiving and sending of entity updates, on the client

typedef struct Packet Packet;

S32 gclNumEntities(void);
bool gclHandleEntityUpdate(Packet *pak);
void gclDeleteAllClientOnlyEntities(void);
void gclEntityDeleteForDemo(S32 entRef, bool noFade);

#endif
