#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef COMBATDEBUGVIEWER_H__
#define COMBATDEBUGVIEWER_H__

typedef struct CombatDebug	CombatDebug;
typedef struct Packet Packet;

// Global
extern CombatDebug *g_pCombatDebugData;

// Copies combat debug information, which does some special processing
void combatdebug_Copy(CombatDebug *pSrc, CombatDebug *pDest, F32 fRate);

void combatdebug_HandlePacket(Packet * pPacket);

void combatDebugView(int bShow);

#endif