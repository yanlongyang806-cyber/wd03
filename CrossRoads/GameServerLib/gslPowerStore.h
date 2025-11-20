/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct Entity Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct PowerStoreDef PowerStoreDef;
typedef struct PowerStorePowerInfo PowerStorePowerInfo;
typedef struct ItemDef ItemDef;

void powerstore_GetStorePowerInfo(Entity *pPlayerEnt, PowerStoreDef *pPowerStoreDef, PowerStorePowerInfo ***peaPowerInfo);

void powerstore_GetTrainerPowerInfoFromEntity(Entity *pEntSrc, Entity *pTrainer, PowerStorePowerInfo ***peaPowerInfo);

void powerstore_GetStorePowerInfoFromItem(Entity *pEntSrc, S32 iBagID, S32 iSlot, ItemDef* pItemDef, PowerStorePowerInfo ***peaPowerInfo, GameAccountDataExtract *pExtract);

void powerstore_RefreshStorePowerInfo(Entity *pPlayerEnt, PowerStorePowerInfo ***peaPowerInfo);