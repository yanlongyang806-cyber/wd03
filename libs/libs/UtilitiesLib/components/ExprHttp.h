#pragma once

typedef struct StashTableImp*			StashTable;



AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Notes, ReturnType, Tags, Args, Location, Comment");
typedef struct ExpressionFuncForServerMonitor
{
	char *pName; AST(POOL_STRING)
	char *pNotes; AST(ESTRING FORMATSTRING(HTML_NOTENAME=1))
	char *pReturnType; AST(ESTRING)
	char *pTags; AST(ESTRING)
	char **ppFuncTables; AST(POOL_STRING)
	char **ppArgs; AST(ESTRING)
	char *pLocation; AST(ESTRING) //blank on anything other that GS or GC. On client/server will be "cli", "ser" or "cli+ser" (or "ser+cli")
	char *pComment; AST(POOL_STRING)

	char *pSourceFile; AST(POOL_STRING)
	int iLineNum;
} ExpressionFuncForServerMonitor;

AUTO_STRUCT;
typedef struct ExpressionFuncForServerMonitorList
{
	//we send all the names of exprContextFuncTables, so that the receiving end can create 
	//an empty func table for each, so that they can be servermonitored
	char **ppFuncTableNames; AST(POOL_STRING)
	ExpressionFuncForServerMonitor **ppFuncs;
} ExpressionFuncForServerMonitorList;

extern ParseTable parse_ExpressionFuncForServerMonitorList[];
#define TYPE_parse_ExpressionFuncForServerMonitorList ExpressionFuncForServerMonitorList


void AddListOfExpressionFuncsFromOtherLocation(ExpressionFuncForServerMonitorList *pList);


void BeginExpressionServerMonitoring(void);

LATELINK;
void GetLocationNameForExpressionFuncServerMonitoring(char **ppOutString);

//the client calls this to get the expression funcs from the server
LATELINK;
void RequestAdditionalExprFuncs(void);

extern StashTable sAllExpressionsByName;