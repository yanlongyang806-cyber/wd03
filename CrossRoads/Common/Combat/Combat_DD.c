/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Combat_DD.h"

#include "Character.h"

#include "Entity.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "Powers.h"


#ifdef GAMESERVER
#include "LoggedTransactions.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "itemTransaction.h"
#include "inventoryTransactions.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Returns the DD AbilityMod for a given value
S32 combat_DDAbilityMod(F32 fValue)
{
	return floorf((fValue - 10.0f) / 2.0f);
}