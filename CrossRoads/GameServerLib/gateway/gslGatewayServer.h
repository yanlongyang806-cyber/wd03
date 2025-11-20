/***************************************************************************
 *     Copyright (c) 2012-, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 *
 ***************************************************************************/
#ifndef GSLGATEWAYSERVER_H__
#define GSLGATEWAYSERVER_H__
#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"
#include "ResourceManager.h"

typedef struct Entity Entity;
typedef struct GatewaySession GatewaySession;
typedef struct UrlArgumentList UrlArgumentList;
typedef struct ParseTable ParseTable;
typedef struct Cluster_Overview Cluster_Overview;
typedef struct GameAccountData GameAccountData;

void gslGatewayServer_Init(void);
void gslGatewayServer_OncePerFrame(void);
void gslGatewayServer_ContainerSubscriptionUpdate(enumResourceEventType eType, GlobalType type, ContainerID id);
void gslGatewayServer_GameAccountDataSubscribed(GameAccountData *pGameAccountdData);

void gslGatewayServer_PeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);

void gslGatewayServer_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct);

void gslGatewayServer_ShardClusterOverviewChanged(Cluster_Overview *pOverview);

int gslGatewayServer_GetSessionCount(void);

void gslGatewayServer_Log(int ecat, char* format, ...);


#endif /* #ifndef GSLGATEWAYSERVER_H__ */

/* End of File */
