#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"

//a "CachedGlobalObjectList" is an object which is created whenever someone does a filtered servermonitor query. It stores the
//result of that query as a pre-computed list, and automatically attaches various options. These lists live for a while, and
//then go away when they collectively take up more than a certain amount of RAM

AUTO_STRUCT;
typedef struct CachedGlobalObjectListLink
{
	char *pObjName; AST(ESTRING, FORMATSTRING(HTML_SKIP=1))
//	char *pLink; AST(ESTRING, FORMATSTRING(HTML=1))
} CachedGlobalObjectListLink;

AUTO_STRUCT;
typedef struct CachedGlobalObjectList
{
	char name[12]; AST(KEY)//the name is just the ID sprintfed into an int
	int iID; 
	GlobalType eContainerType;
	char *pApplyAtransaction; AST(ESTRING, FORMATSTRING(command=1, HTML_COMMAND_IN_NEW_WINDOW=1))
	char *pDescriptiveName; AST(ESTRING, FORMATSTRING(HTML_NO_HEADER=1))
	CachedGlobalObjectListLink **ppLinks; AST(FORMATSTRING(HTML_SKIP=1))
	int iNumObjects;
	int iMySizeBytes;
} CachedGlobalObjectList;

CachedGlobalObjectList *GetCachedGlobalObjectListFromID(int iID);

//returns the ID of the newly created list
int CreateCachedGlobalObjectList(char *pDescriptiveName, char *pDictionaryName, char ***pppObjectNames);
