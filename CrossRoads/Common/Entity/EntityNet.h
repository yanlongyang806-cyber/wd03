#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "Entity.h"

typedef struct Packet Packet;
typedef struct Entity Entity;

//obsolete ----------------
void entSendFull(Packet *pak, Entity *ent);
void entReceiveFull(Packet *pak, Entity *ent);

void entSendRef(Packet *pak, EntityRef ref);
EntityRef entReceiveRef(Packet *pak);

void entSendType(Packet *pak, GlobalType type);
GlobalType entReceiveType(Packet *pak);
// obsolete ----------------

//we send a certain number of bits of the reference ID (the unique ID that goes along with the slot number) whenever we send a dif.
#define NUM_BITS_OF_REF_ID_TO_SEND (12)

//void entReceivePosition(Packet *pak, Entity *pEnt);
//void entSendPosition(Packet *pak, Entity *pEnt);
