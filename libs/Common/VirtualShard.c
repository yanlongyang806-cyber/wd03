#include "VirtualShard.h"
#include "VirtualShard_h_ast.h"
#include "objSchema.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););



AUTO_RUN_LATE;
void virtualShardStartup(void)
{
	objRegisterNativeSchema(GLOBALTYPE_VIRTUALSHARD, parse_VirtualShard, NULL, NULL, NULL, NULL, NULL);
}


#include "VirtualShard_h_ast.c"
