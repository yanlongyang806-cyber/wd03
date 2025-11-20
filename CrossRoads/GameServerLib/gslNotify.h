/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "NotifyCommon.h"

typedef struct Entity Entity;
typedef struct MissionDef MissionDef;

// Wrapper to send a few types of notifications to the player
void notify_SendMissionNotification(Entity *pEnt, Entity *pFormatEnt, MissionDef *pDef, const char *pcMessageKey, NotifyType eNotifyType);

