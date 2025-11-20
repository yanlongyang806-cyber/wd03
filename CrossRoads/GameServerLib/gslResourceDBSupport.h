#pragma once

#include "stdtypes.h"

typedef struct Entity Entity;
typedef struct TransactionReturnVal TransactionReturnVal;

typedef void (*TransactionReturnCallback)(TransactionReturnVal *returnVal, UserData data);

void MissionAdd_ResourceDBDeferred(Entity *pEnt, const char *pMissionName, TransactionReturnCallback cb, UserData data);
void MissionInit_ResourceDBDeferred(Entity *pEnt, const char* pMissionName);
