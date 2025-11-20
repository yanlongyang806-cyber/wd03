#include "SteamCommon.h"
#include "GlobalTypes.h"

#include "AutoGen/SteamCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Returns 0 if the current game doesn't have a Steam App ID
U32 ccSteamAppID(void)
{
	static U32 app_id = -1;
	if(app_id == -1)
	{
		if(stricmp(GetShortProductName(),"ST")==0)
			app_id = 9900;
		else if(stricmp(GetShortProductName(),"FC")==0)
			app_id = 9880;
		else
			app_id = 0;
	}
	return app_id;
}


#include "AutoGen/SteamCommon_h_ast.c"
