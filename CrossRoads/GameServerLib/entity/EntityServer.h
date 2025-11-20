/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef ENTITYSERVER_H_
#define ENTITYSERVER_H_

typedef struct Entity Entity;

// Implement general entity code specific to the gameserver and a certain product

void entConSetHealth(Entity *e, F32 fValue);

#endif