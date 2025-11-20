#pragma once

typedef struct TestScript TestScript;
typedef struct TestServerGlobal TestServerGlobal;

#define TESTSERVER_CONSOLE_LINESIZE 80
#define TESTSERVER_CONSOLE_MAXLINES 300

AUTO_STRUCT;
typedef struct TestServerWebMain
{
	char					*title; AST(ESTRING)
} TestServerWebMain;

AUTO_STRUCT;
typedef struct TestServerConsolePage
{
	char					*title; AST(ESTRING)
} TestServerConsolePage;

AUTO_STRUCT;
typedef struct TestServerConsoleLine
{
	int						color;
	S64						iTimestamp;
	char					*pcLine; AST(ESTRING)
} TestServerConsoleLine;

AUTO_STRUCT;
typedef struct TestServerConsoleLines
{
	S64						iTimestamp;
	TestServerConsoleLine	**ppLines;
} TestServerConsoleLines;

AUTO_STRUCT;
typedef struct TestServerStatus
{
	char					*title; AST(ESTRING)
	EARRAY_OF(TestScript)	scripts;
	char					*pcLastScript; AST(ESTRING)
} TestServerStatus;

AUTO_STRUCT;
typedef struct TestServerView
{
	char							*title; AST(ESTRING)
	const char						*pcScope; AST(POOL_STRING)
	const char						*pcName; AST(POOL_STRING)

	bool							bExpanded;
	bool							bAllowModify;
	
	EARRAY_OF(TestServerGlobal)		ppItems;
} TestServerView;

AUTO_STRUCT;
typedef struct TestServerViewListItem
{
	char	*name; AST(ESTRING)
	char	*date; AST(ESTRING)
	char	*fn; AST(ESTRING)
} TestServerViewListItem;

AUTO_STRUCT;
typedef struct TestServerViewList
{
	char								*title; AST(ESTRING)
	EARRAY_OF(TestServerViewListItem)	ppItems;
} TestServerViewList;

AUTO_STRUCT;
typedef struct TestServerScriptListItem
{
	char	*name; AST(ESTRING)
	char	*fn; AST(ESTRING)
	bool	dir;
	int		depth;
} TestServerScriptListItem;

AUTO_STRUCT;
typedef struct TestServerScriptList
{
	char								*title; AST(ESTRING)
	EARRAY_OF(TestServerScriptListItem)	ppItems;
} TestServerScriptList;

AUTO_STRUCT;
typedef struct TestServerLuaWebInfo
{
	char	*title; AST(ESTRING)
	char	*script; AST(ESTRING)
} TestServerLuaWebInfo;

void TestServer_StartWebInterface(void);
void TestServer_ConsolePrintf(const char *pcFormat, ...);
void TestServer_ConsolePrintfColor(int color, const char *pcFormat, ...);
char *TestServer_ViewToHtml(TestServerView *pView);