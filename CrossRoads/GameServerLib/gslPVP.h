/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

typedef struct AttribMod AttribMod;
typedef struct AttribModDef AttribModDef;
typedef struct Entity Entity;
typedef struct PVPFlagParams PVPFlagParams;

void gslPVPTick(void);
void gslPVPCleanup(Entity *e);  // Cleans up duels, infections, etc.

void gslPVPDuelRequest(Entity *e1, Entity *e2);
void gslPVPDuelAccept(Entity *e1);
void gslPVPDuelDecline(Entity *e);
void gslPVPTeamDuelAccept(Entity *pEnt);
void gslPVPTeamDuelDecline(Entity *pEnt);
void gslPVPTeamDuelSurrender(Entity *pEnt);

void gslPVPJoinGroupEnt(Entity *e, Entity *source, PVPFlagParams *params);

void gslPVPInfectEnt(Entity *e, F32 radius, U32 allowHeal, U32 allowCombatOut);
void gslPVPInfectEnd(Entity *e);

void gslPVPModNotify(Entity *source, Entity *target, AttribMod* mod, AttribModDef* moddef);
void gslPVPInfect(Entity *source, Entity *target, int enemy);

//Team Duels
void gslPVPTeamDuelRequest(Entity *e1, Entity *e2);