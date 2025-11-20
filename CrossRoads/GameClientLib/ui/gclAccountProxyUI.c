#include "gclAccountProxy.h"
//#include "gclAccountProxy_h_ast.h"

#include "AccountDataCache.h"
#include "accountnet.h"
#include "Entity.h"
#include "estring.h"
#include "GameAccountDataCommon.h"
#include "GlobalTypes.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "microtransactions_common.h"
#include "Player.h"
#include "entCritter.h"
#include "UIGen.h"
#if _XBOX
#include "xbox\XStore.h"
#endif

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

extern StaticDefineInt MicroItemTypeEnum[];

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// Get the integer value of a key/value pair from the account (proxy) server.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AccountGetKeyValueInt);
S32 gclAPExprAccountGetKeyValueInt(SA_PARAM_NN_STR const char *pKey)
{
	S32 iVal;
	if (gclAPGetKeyValueInt(pKey, &iVal))
	{
		return iVal;
	}
	else
	{
		return 0;
	}
}

// Forces the product cache to clear.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenClearProductLists);
void gclAPExprGenClearProductLists()
{
	ADCClearProductCache();
}

AUTO_RUN;
void gclAPInitVars(void)
{
	ui_GenInitStaticDefineVars(MicroItemTypeEnum, "MicroItemType");
}
