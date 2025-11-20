#include "textparser.h"
#include "StructPack.h"
#include "UIGen.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static PackedStructStream s_Stream;

AUTO_RUN;
void ui_GenPackInit(void)
{
	PackedStructStreamInit(&s_Stream, STRUCT_PACK_BITPACK);
}

bool ui_GenPack(UIGenInternal *pInt, U32 *puiPacked)
{
	// We can't pack things with inline children since they're shallow copied.
	if (pInt && eaSize(&pInt->eaInlineChildren) == 0)
	{
		ParseTable *pTable = ui_GenInternalGetType(pInt);
		*puiPacked = StructPack(pTable, pInt, &s_Stream);
		return true;
	}
	else
		return false;
}

UIGenInternal *ui_GenUnpack(UIGenInternal *pInt, U32 *puiPacked)
{
	if (!*puiPacked || pInt)
		return pInt;
	return NULL;
	
}
