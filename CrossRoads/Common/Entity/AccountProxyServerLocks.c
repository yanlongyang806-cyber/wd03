#include "ResourceManager.h"
#include "net/accountnet.h"
#include "accountnet_h_ast.h"

// Register the container type.
AUTO_RUN_LATE;
int RegisterContainerType(void)
{
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, parse_AccountProxyLockContainer, NULL, NULL, NULL, NULL, NULL);
	return 1;
}