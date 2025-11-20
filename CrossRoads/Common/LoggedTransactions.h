/***************************************************************************
 *     Copyright (c) 2008, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/

#pragma once

#include "objTransactions.h"

// Creates a managed ReturnVal which automatically logs the success and
//   failure strings from the transaction function. If pchPrefix is not
//   NULL, then the given prefix is prepended to the logged message.
// If pent is provided, then the type and id are ignored and the message
//   is logged with entLog.
// If debugName and gameSpecificLogInfo are provided, then we mimic entLog without requiring the entity.  Useful when running on an app server.
// Otherwise, the message is logged with ObjLog and the type and id provided.serverlib.c
// After logging, the user function is called (if not NULL) with the
//   given user data pointer.
TransactionReturnVal* LoggedTransactions_CreateManagedReturnValEx(const char *pchPrefix, Entity *pent, GlobalType type, ContainerID id, const char *debugName, const char *gameSpecificLogInfo,
    TransactionReturnCallback func, void *pvUserData);

// Special case forms of the above function.
TransactionReturnVal* LoggedTransactions_CreateManagedReturnVal(const char *pchPrefix, TransactionReturnCallback func, void *pvUserData);
TransactionReturnVal* LoggedTransactions_CreateManagedReturnValEnt(const char *pchPrefix, Entity *pent, TransactionReturnCallback func, void *pvUserData);
TransactionReturnVal* LoggedTransactions_CreateManagedReturnValEntInfo(const char *pchPrefix, GlobalType entType, ContainerID entID, const char *debugName, const char *gameSpecificLogInfo, TransactionReturnCallback func, void *pvUserData);
TransactionReturnVal* LoggedTransactions_CreateManagedReturnValObj(const char *pchPrefix, GlobalType type, ContainerID id, TransactionReturnCallback func, void *pvUserData);

// And even more simple versions for when you don't need a callback.
#define LoggedTransactions_MakeEntReturnVal(prefix, pent)     LoggedTransactions_CreateManagedReturnValEnt(prefix, pent, NULL, NULL)
#define LoggedTransactions_MakeObjReturnVal(prefix, type, id) LoggedTransactions_CreateManagedReturnValEnt(prefix, NULL, type, id)
#define LoggedTransactions_MakeReturnVal(prefix)              LoggedTransactions_CreateManagedReturnValEnt(prefix, NULL, NULL, NULL)

/* End of File */
