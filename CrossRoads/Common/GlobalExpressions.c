/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Expression.h"
#include "error.h"
#include "textparser.h"
#include "GlobalExpressions.h"

#include "GlobalExpressions_h_ast.h"

GlobalExpressions g_GlobalExpressions;

extern ExprContext *g_pItemContext;

AUTO_STARTUP(GlobalExpressions) ASTRT_DEPS(PowerVars);
void LoadGlobalExpressions(void)
{
	loadstart_printf("Loading GlobalExpressions...");
	ParserLoadFiles(NULL, "defs/config/GlobalExpressions.def", "GlobalExpressions.bin", 0, parse_GlobalExpressions, &g_GlobalExpressions);
	exprGenerate(g_GlobalExpressions.pExprItemEPValue, g_pItemContext);
	exprGenerate(g_GlobalExpressions.pExprStoreEPConversion, g_pItemContext);
	loadend_printf("done.");
}

#include "GlobalExpressions_h_ast.c"
