#include "ValueLockList.h"
#include "ValueLockList_c_ast.h"
#include "objContainer.h"
#include "objTransactions.h"

#include "AutoGen\GlobalTypes_h_ast.h"

// TODO:
// DEPRECATE THE EVER-LIVING CRAP OUT OF THIS FILE

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountValueLocked
{
	const U32 accountID;			AST(PERSIST)
	CONST_STRING_MODIFIABLE key;	AST(ESTRING PERSIST)
	CONST_STRING_MODIFIABLE proxy;	AST(ESTRING PERSIST)
	const int proxyHas;				AST(PERSIST)
} AccountValueLocked;

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountValueLockedList
{
	const U32 id;								AST(KEY PERSIST)
	CONST_EARRAY_OF(AccountValueLocked) list;	AST(PERSIST)

	AST_COMMAND("Apply Transaction","ServerMonTransactionOnEntity AccountServerLocks $FIELD(ID) $STRING(Transaction String)")
} AccountValueLockedList;

void initializeLockedList(void)
{
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNTSERVER_LOCKS, parse_AccountValueLockedList, NULL, NULL, NULL, NULL, NULL);
}

void deleteLockedList(void)
{
	ContainerRef **ppRefs = NULL;

	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_LOCKS, pContainer);
	{
		ContainerRef *pRef = StructCreate(parse_ContainerRef);
		pRef->containerType = GLOBALTYPE_ACCOUNTSERVER_LOCKS;
		pRef->containerID = pContainer->containerID;
		eaPush(&ppRefs, pRef);
	}
	CONTAINER_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(ppRefs, iRef, iNumRefs);
	{
		objRequestContainerDestroyLocal(NULL, ppRefs[iRef]->containerType, ppRefs[iRef]->containerID);
	}
	EARRAY_FOREACH_END;

	eaDestroyStruct(&ppRefs, parse_ContainerRef);
}

#include "ValueLockList_c_ast.c"