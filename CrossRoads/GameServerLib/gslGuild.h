/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#pragma once
GCC_SYSTEM

typedef enum enumTransactionOutcome enumTransactionOutcome;
typedef struct NOCONST(Guild) NOCONST(Guild);
typedef struct Entity Entity;

enumTransactionOutcome gslGuild_tr_UpdateGuildStat(ATR_ARGS, NOCONST(Guild) *pGuildContainer, const char *pchStatName, S32 eOperation, S32 iValue);
enumTransactionOutcome gslGuild_tr_SetGuildTheme(ATR_ARGS, NOCONST(Guild) *pGuildContainer, const char *pchThemeName);

void gslGuild_AutoJoinGuild(Entity *pEntity);
