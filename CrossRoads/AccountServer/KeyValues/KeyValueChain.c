#include "KeyValueChain.h"
#include "objContainer.h"

#include "KeyValueChain_h_ast.h"

// Deprecated interface to old "KeyValueChain" container type
// Preserved to allow loading (and possibly deleting) of leftover chain containers

// Initialize keyvalue chains
void initializeKeyValueChains(void)
{
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNTSERVER_KEYVALUECHAIN, parse_KeyValueChain, NULL, NULL, NULL, NULL, NULL);
}

#include "KeyValueChain_h_ast.c"