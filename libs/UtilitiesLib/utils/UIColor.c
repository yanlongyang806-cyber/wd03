/***************************************************************************



***************************************************************************/

#include "UIColor.h"
#include "ResourceManager.h"
#include "MemoryPool.h"

#include "AutoGen/UIColor_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
MP_DEFINE_MEMBER(UIColor) = 0;

AUTO_RUN;
int RegisterColorDicts(void)
{
	return 1;
}

#include "AutoGen/UIColor_h_ast.c"