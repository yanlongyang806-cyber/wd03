/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef TESTTRANSHARNESS_H_
#define TESTTRANSHARNESS_H_

#include "LocalTransactionManager.h"

#define OBJECTS_PER_SERVER 10000


typedef struct
{
	int iTime;
	int iMoney;
} SimpleObjectPersistData;

typedef struct
{
	TransactionID iLockingID;
	int iLockExtraCount;
	LTMSlowTransactionID iSlowTransID; // if 0, no slow transaction going on


	SimpleObjectPersistData *pBackupData;
	SimpleObjectPersistData data;
	int iSlowTransactionCount;

} SimpleObject;


typedef struct SimpleServer
{
	int iServerID;
	SimpleObject objects[OBJECTS_PER_SERVER];
	struct LocalTransactionManager *pLocalTransactionManager;
} SimpleServer;


void UpdateSimpleServer(SimpleServer *pServer);
void InitSimpleServer(SimpleServer *pServer);


#endif
