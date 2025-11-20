/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef TEAMSERVER_H
#define TEAMSERVER_H

#include "GlobalTypeEnum.h"

typedef enum enumTransactionOutcome enumTransactionOutcome;

typedef struct NOCONST(Team) NOCONST(Team);
typedef struct NOCONST(Entity) NOCONST(Entity);

typedef struct TransactionCommand TransactionCommand;
typedef struct TransactionReturnVal TransactionReturnVal;
typedef struct Team Team;

typedef U32 ContainerID;
typedef int SlowRemoteCommandID;

typedef void (*SlowRemoteCommandReturnFunction)(SlowRemoteCommandID iCmdID, char* retVal);

int TeamServerInit(void);

#endif
