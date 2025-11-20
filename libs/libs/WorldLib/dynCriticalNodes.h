#pragma once
GCC_SYSTEM

#include "dynNode.h"

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynCriticalNodeList
{
	const char *pcName;				AST(KEY POOL_STRING)
	const char *pcFileName;			AST(CURRENTFILE)
	const char **eaCriticalNode;	AST(POOL_STRING)
}
DynCriticalNodeList;
extern ParseTable parse_DynCriticalNodeList[];
#define TYPE_parse_DynCriticalNodeList DynCriticalNodeList

void dynCriticalNodeListLoadAll(void);