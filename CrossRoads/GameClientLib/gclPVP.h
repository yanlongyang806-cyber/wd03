/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

typedef struct Entity Entity;

void gclPVPDuelRequestAck(Entity *other);

void gclPVP_EntityUpdate(Entity *pEnt);

//Monitors the PvP match state and sends appropriate notifications
void gclPVP_Tick();

//Returns the ID of the winning group for the current PvP match
S32 gclPVP_GetWinningGroupID();
