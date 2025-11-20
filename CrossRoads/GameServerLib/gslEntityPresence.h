#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"

typedef struct GameEncounter GameEncounter;

typedef struct EntityPresenceCheckInfo EntityPresenceCheckInfo;

void gslEntityPresence_OnEncounterActivate(int iPartitionIdx, GameEncounter * pEncounter);

void gslEntityPresenceTick(void);

void gslRequestEntityPresenceUpdate();

void gslEntityPresenceRelease(Player * pPlayer);