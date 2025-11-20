/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "error.h"
#include "fileutil.h"
#include "foldercache.h"

#include "AlgoItemCommon.h"
#include "AlgoItemCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


CommonAlgoTables g_CommonAlgoTables;

static void CommonAlgoTables_ReloadCallback( const char *relpath, int when)
{
	loadstart_printf("Reloading CommonAlgoTables...");

	StructDeInit(parse_CommonAlgoTables, &g_CommonAlgoTables);
	StructInit(parse_CommonAlgoTables, &g_CommonAlgoTables);

	ParserLoadFiles( NULL, "defs/rewards/algotables_common.data", "algotables_common.bin", 0, parse_CommonAlgoTables, &g_CommonAlgoTables);	

	loadend_printf(" done.");
}


AUTO_STARTUP(AlgoTablesCommon);
void CommonAlgoTables_Load(void)
{
	loadstart_printf("Loading CommonAlgoTables...");

	StructInit(parse_CommonAlgoTables, &g_CommonAlgoTables);

	ParserLoadFiles( NULL, "defs/rewards/algotables_common.data", "algotables_common.bin", 0, parse_CommonAlgoTables, &g_CommonAlgoTables);	

	loadend_printf(" done." );

	if (isDevelopmentMode())
	{
		// Have reload take effect immediately
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/rewards/algotables_common.data", CommonAlgoTables_ReloadCallback);
	}
}



#include "AlgoItemCommon_h_ast.c"
