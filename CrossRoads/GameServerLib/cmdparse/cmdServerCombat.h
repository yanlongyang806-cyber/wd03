/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef CMDSERVERCOMBAT_H__
#define CMDSERVERCOMBAT_H__

typedef struct Cmd Cmd;
typedef struct CmdContext CmdContext;
typedef struct Entity Entity;

void cmdServerCombatExec(Cmd *cmd, CmdContext *cmd_context);
extern Cmd server_combat_cmds[];

void Refill_HP_POW(Entity *e);

bool gslPlayerRespawn(Entity* pPlayerEnt, bool bForceRespawn, bool bForceIfNotDead);

void Add_Power(Entity *clientEntity, ACMD_NAMELIST("PowerDef", REFDICTIONARY) char *pchName);

#endif