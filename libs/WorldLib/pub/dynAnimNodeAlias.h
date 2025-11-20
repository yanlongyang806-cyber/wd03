#pragma once
GCC_SYSTEM

#include "referencesystem.h"

#include "dynNode.h"

extern DictionaryHandle hDynAnimNodeAliasListDict;

AUTO_STRUCT AST_ENDTOK("\n");
typedef struct DynAnimNodeAlias
{
	const char *pcAlias;	AST(STRUCTPARAM POOL_STRING KEY) //node name we'd like to redefine
	const char *pcTarget;	AST(STRUCTPARAM POOL_STRING)	 //node name we'll redefine it to
}
DynAnimNodeAlias;
extern ParseTable parse_DynAnimNodeAlias[];
#define TYPE_parse_DynAnimNodeAlias DynAnimNodeAlias

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynAnimNodeAliasList
{
	const char *pcName;		AST(STRUCTPARAM POOL_STRING KEY)
	const char *pcFileName;	AST(CURRENTFILE)
	const char *pcComments;	AST(SERVER_ONLY)
	const char *pcScope;	AST(SERVER_ONLY POOL_STRING NO_TEXT_SAVE)
	DynAnimNodeAlias **eaFxAlias;	AST(NAME(FxNodeAlias))
	const char *pcFxDefaultNode;
	StashTable stFxAlias;		NO_AST
}
DynAnimNodeAliasList;
extern ParseTable parse_DynAnimNodeAliasList[];
#define TYPE_parse_DynAnimNodeAliasList DynAnimNodeAliasList

void dynAnimNodeAliasLoadAll(void);
const char *dynAnimNodeFxAlias(const DynAnimNodeAliasList *pList, const char *pcName);
const char *dynAnimNodeFxDefaultAlias(const DynAnimNodeAliasList *pList);