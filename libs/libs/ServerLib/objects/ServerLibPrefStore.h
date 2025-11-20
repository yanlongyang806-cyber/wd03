#pragma once


#include "GlobalTypeEnum.h"
#include "stdtypes.h"


AUTO_ENUM;
typedef enum PrefType
{
	PREF_STRING,
} PrefType;

AUTO_STRUCT AST_CONTAINER;
typedef struct PrefKV
{
	CONST_STRING_POOLED name;			AST(KEY PERSIST POOL_STRING)
	const PrefType type;				AST(PERSIST)
	CONST_STRING_MODIFIABLE stringVal;	AST(PERSIST)
	int cmdID;							NO_AST
} PrefKV;

AUTO_STRUCT AST_CONTAINER;
typedef struct PrefStore
{
	const U32 id;						AST(KEY PERSIST)
	CONST_EARRAY_OF(PrefKV) ppPrefs;	AST(PERSIST)

	AST_COMMAND("Apply Transaction","ServerMonTransactionOnEntity PrefStore $FIELD(id) $STRING(Transaction String)$CONFIRM(Really apply this transaction?)")
} PrefStore;

typedef void (*PrefStoreCallbackFunc)(bool bSuccess, char *pRetVal, void *pUserData);

void PrefStore_SetString(char *pKey, char *pString, PrefStoreCallbackFunc pCB, void *pUserData);
void PrefStore_GetString(char *pKey, PrefStoreCallbackFunc pCB, void *pUserData);
void PrefStore_AtomicAddAndGet(char *pKey, U32 iAmount, PrefStoreCallbackFunc pCB, void *pUserData);