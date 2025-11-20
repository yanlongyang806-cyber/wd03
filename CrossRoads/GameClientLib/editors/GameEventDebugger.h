/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#pragma once

GCC_SYSTEM

typedef struct GameEvent GameEvent;
typedef struct GameEventParticipants GameEventParticipants;

#ifndef NO_EDITORS

#endif // #endif NO_EDITORS

void gameeventdebug_SendEvent(GameEvent *pGameEvent, GameEventParticipants *pGameEventParticipants);
