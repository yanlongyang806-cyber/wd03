/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct Entity Entity;
typedef enum GlobalType GlobalType;
typedef U32 ContainerID;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(Guild) NOCONST(Guild);
typedef enum enumTransactionOutcome enumTransactionOutcome;

void gslActivity_AddLevelUpEntry(Entity *pEnt, U32 newLevel);
void gslActivity_AddPetAddEntry(Entity *playerEnt, Entity *petEnt);
void gslActivity_AddPetDismissEntry(Entity *playerEnt, Entity *petEnt);
void gslActivity_AddPetRenameEntry(Entity *playerEnt, Entity *petEnt, const char *oldName);
void gslActivity_AddPetPromoteEntry(Entity *playerEnt, Entity *petEnt);
void gslActivity_AddPetTrainEntry(Entity *playerEnt, Entity *petEnt, const char *newSkillNode);
void gslActivity_AddPetTradeEntry(Entity *srcEnt, Entity *destEnt, Entity *petEnt);

// These are here so that the Game Action transactions can call the activity
//  log transactions as helpers.
enumTransactionOutcome ActivityLog_tr_AddEntityLogEntry(ATR_ARGS, NOCONST(Entity) *pEnt, int entryType, const char *argString, U32 time, float playedTime);
enumTransactionOutcome ActivityLog_tr_AddGuildLogEntry(ATR_ARGS, NOCONST(Guild) *pGuildContainer, int entryType, const char *argString, U32 time, U32 subjectID);