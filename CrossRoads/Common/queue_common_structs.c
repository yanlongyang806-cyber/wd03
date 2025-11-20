#include "queue_common_structs.h"
#include "queue_common_structs_h_ast.h"
#include "queue_common_structs_h_ast.c"

//#include "PvPGameCommon_h_ast.h"

DefineContext* g_pQueueCategories = NULL;
DefineContext* g_pQueueRewards = NULL;
DefineContext* g_pQueueDifficulty = NULL;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););