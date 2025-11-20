/***************************************************************************
*     Copyright (c) 2005-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "SharedBankCommon.h"
#include "AutoTransDefs.h"
#include "GamePermissionsCommon.h"
#include "GameAccountData\GameAccountData.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "error.h"
#include "Expression.h"
#include "entity.h"
#include "Player.h"
#include "AutoTransDefs.h"
#include "GameAccountDataCommon.h"
#include "inventoryCommon.h"

#include "AutoGen/SharedBankCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#ifndef APPSERVER

// Note the following generates warnings on reload but appears to do it correctly. 
// As reload is only done in dev environment for testing this is ok
static void SharedBank_ReloadCB(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Loading SharedBank Config... ");

	fileWaitForExclusiveAccess(pchRelPath);

	ParserReloadFile(pchRelPath, parse_SharedBankConfig, &g_SharedBankConfig, NULL, PARSER_OPTIONALFLAG);	

	loadend_printf(" done (%d Permissions).", eaSize(&g_SharedBankConfig.eaKeyPermissions));
}

#endif	// #ifndef APPSERVER

// Load the shared bank config.
SharedBankConfig g_SharedBankConfig;
AUTO_STARTUP(SharedBank);
void SharedBank_Load(void)
{
	loadstart_printf("Loading SharedBank Config... ");

	ParserLoadFiles(NULL, "defs/config/SharedBankConfig.def", "SharedBankConfig.bin", PARSER_OPTIONALFLAG, parse_SharedBankConfig, &g_SharedBankConfig);

	loadend_printf(" done (%d Permissions).", eaSize(&g_SharedBankConfig.eaKeyPermissions));

#ifndef APPSERVER
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(
			FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE,
			"defs/config/SharedBankConfig.def",
			SharedBank_ReloadCB);
	}
#endif	// #ifndef APPSERVER

}

bool SharedBank_CanAccess(Entity* pEnt, GameAccountDataExtract *pExtract)
{
	if(pEnt && pExtract)
	{
		S32 i;

		// can not access shared bank if a ugc character
		if(entity_IsUGCCharacter(pEnt))
		{
			return false;
		}

		if(GamePermission_ExtractHasToken(pExtract, GAME_PERMISSION_SHARED_BANK, false))
		{
			return true;
		}

		for(i = 0; i < eaSize(&g_SharedBankConfig.eaKeyPermissions); ++i)
		{
			if(gad_GetAttribIntFromExtract(pExtract, g_SharedBankConfig.eaKeyPermissions[i]->pchKeyPermission))
			{
				return true;
			}
		}

	}

	return false;
}

U32 SharedBank_GetNumSlots(Entity *pEnt, GameAccountDataExtract *pExtract, bool bCountGamePermission)
{
	if(pEnt && pExtract)
	{
		bool bFound;
		S32 iSlotsFromGamePermission = GamePermissions_trh_GetCachedMaxNumericEx(CONTAINER_NOCONST(Entity, pEnt), GAME_PERMISSION_SHARED_BANK_SLOTS, false, &bFound);
		S32 iBankSize;

		if(!bFound || !bCountGamePermission)
		{
			iSlotsFromGamePermission = 0;
		}

		iBankSize = gad_GetAttribIntFromExtract(pExtract, MicroTrans_GetSharedBankSlotGADKey()) +
			gad_GetAccountValueIntFromExtract(pExtract, MicroTrans_GetSharedBankSlotASKey()) +
			iSlotsFromGamePermission;

		if(iBankSize < 0)
		{
			iBankSize = 0;
		}

		return (U32)iBankSize;

	}

	return 0;
}

//
// ***************************************************** Expressions below this line ******************************************************
//

// Returns true if this entity can access the sharedbank
AUTO_EXPR_FUNC(UIGen, Entity) ACMD_NAME(SharedBankIsAvailable);
S32 SharedBank_IsAvailable(SA_PARAM_OP_VALID Entity *pEnt)
{
	if(pEnt)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		if(pExtract)
		{
			return SharedBank_CanAccess(pEnt, pExtract);
		}
	}

	return false;
}

// Returns true if this entity can has more slots available
AUTO_EXPR_FUNC(UIGen, Entity) ACMD_NAME(SharedBankCanBuySlots);
bool SharedBank_CanBuySlots(SA_PARAM_OP_VALID Entity *pEnt)
{
	if(pEnt)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		if(pExtract)
		{
			if(g_SharedBankConfig.bCanAlwaysBuy || SharedBank_CanAccess(pEnt, pExtract))
			{
				if(SharedBank_GetNumSlots(pEnt, pExtract, false) < g_SharedBankConfig.uMaximumSlots)
				{
					return true;
				}
			}
		}
	}

	return false;
}


// Returns true if this entity can has more slots available
AUTO_EXPR_FUNC(UIGen, Player, Mission) ACMD_NAME(SharedBankPlayerCanBuySlots);
bool SharedBank_PlayerCanBuySlots(ExprContext* context)
{
	Entity* pEntity = exprContextGetVarPointerUnsafePooled(context, "Player");
	if(pEntity)
	{
		return SharedBank_CanBuySlots(pEntity);
	}

	return false;
}

bool SharedBank_SharedBankNeedsFixup(Entity *pEnt, Entity *pSharedBankEnt, GameAccountDataExtract *pExtract)
{
	if(pEnt && pEnt->pPlayer)
	{
		//Check the shared bank bag
		InventoryBag *pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pSharedBankEnt), InvBagIDs_Inventory, pExtract);
		S32 iCurBankSize = invbag_maxslots(pSharedBankEnt, pBag);
		S32 iBankSize = SharedBank_GetNumSlots(pEnt, pExtract, true);

		if (iBankSize > iCurBankSize)
		{
			return true;
		}
	}
	return false;
}

// Get shared bank numeric (if allowed).
SharedBankNumeric *SharedBank_GetNumeric(Entity *pPlayerEnt, Entity *pSharedBankEnt, const char *pcNumeric)
{
	if(pPlayerEnt && pPlayerEnt->pPlayer && pSharedBankEnt)
	{
		S32 i;
		bool bFound = false;
		for(i = 0; i < eaSize(&g_SharedBankConfig.eaSharedBankNumerics); ++i)
		{
			if(stricmp(pcNumeric, g_SharedBankConfig.eaSharedBankNumerics[i]->pcNumeric) == 0)
			{
				return g_SharedBankConfig.eaSharedBankNumerics[i];
			}
		}
	}

	return NULL;
}

// return the maximum this character can deposit into the shared bank
S32 SharedBank_GetMaximumDeposit(Entity *pPlayerEnt, const char *pcNumeric)
{
	if(pPlayerEnt && pPlayerEnt->pPlayer)
	{
		Entity *pSharedBank = GET_REF(pPlayerEnt->pPlayer->hSharedBank);
		if(pSharedBank)
		{
			SharedBankNumeric *pSharedBankNumeric = SharedBank_GetNumeric(pPlayerEnt, pSharedBank, pcNumeric);
			if(pSharedBankNumeric)
			{
				S32 iBank = inv_GetNumericItemValue(pSharedBank, pcNumeric);
				S32 iBankLimit = pSharedBankNumeric->iMaximumValue;
				S32 iCashEnt = inv_GetNumericItemValue(pPlayerEnt, pcNumeric);
				S32 iCanDeposit = iBankLimit - iBank;

				if(iCanDeposit > iCashEnt)
				{
					iCanDeposit = iCashEnt;
				}

				return iCanDeposit;
			}
		}
	}

	return 0;
}

// check to see if this amount can be transfered to / from the shared bank + iToBank is to bank
SharedBankError SharedBank_ValidateNumericTransfer(Entity *pEnt, S32 iToBank, const char *pcNumeric)
{
	SharedBankError error = SharedBankError_None;
	if(pEnt && pEnt->pPlayer)
	{
		Entity *pSharedBank = GET_REF(pEnt->pPlayer->hSharedBank);
		if(pSharedBank)
		{
			SharedBankNumeric *pSharedBankNumeric = SharedBank_GetNumeric(pEnt, pSharedBank, pcNumeric);
			if(pSharedBankNumeric)
			{
				// numeric is allowed
				S32 iFinalEnt, iFinalBank;
				S32 iPermissionLimit = GamePermissions_trh_GetCachedMaxNumeric(CONTAINER_NOCONST(Entity, pEnt), pcNumeric, true);
				S32 iBankLimit = pSharedBankNumeric->iMaximumValue;

				iFinalEnt = inv_GetNumericItemValue(pEnt, pcNumeric) - iToBank;
				iFinalBank = inv_GetNumericItemValue(pSharedBank, pcNumeric) + iToBank;

				if(iToBank < 0 && iFinalEnt > iPermissionLimit)
				{
					error = SharedBankError_Ent_Full;
				}
				else if(iToBank > 0 && iFinalBank > iBankLimit)
				{
					error = SharedBankError_Bank_Full;
				}
				else if(iFinalBank < 0)
				{
					error = SharedBankError_Bank_Empty;
				}
				else if(iFinalEnt < 0)
				{
					error = SharedBankError_Ent_Empty;
				}
			}
			else
			{
				// could not find numeric
				error = SharedBankError_Numeric_Error;
			}
		}
		else
		{
			// no shared bank
			error = SharedBankError_No_Bank;
		}
	}
	else
	{
		error = SharedBankError_No_Ent;
	}

	return error;
}

// Shared bank, return true if the amount and type to be transfered is legal
AUTO_EXPR_FUNC(UIGen, Entity) ACMD_NAME(SharedBankIsLegalNumericTransfer);
S32 SharedBank_IsLegalNumericTransfer(SA_PARAM_OP_VALID Entity *pEnt, S32 iToBank, const char *pcNumeric)
{
	if(pEnt && pEnt->pPlayer)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		if(pExtract)
		{
			if(SharedBank_CanAccess(pEnt, pExtract))
			{
				if(SharedBank_ValidateNumericTransfer(pEnt, iToBank, pcNumeric) == SharedBankError_None)
				{
					return true;
				}
			}
		}
	}

	return false;
}

// Shared bank, return the maxmimum that this player can deposit
// lower of amount left based on permission or numeric left on player
AUTO_EXPR_FUNC(UIGen, Entity) ACMD_NAME(SharedBankGetMaximumDeposit);
S32 SharedBank_ExprGetMaximumDeposit(SA_PARAM_OP_VALID Entity *pEnt, const char *pcNumeric)
{
	S32 iDeposit = SharedBank_GetMaximumDeposit(pEnt, pcNumeric);

	return iDeposit;
}

#include "AutoGen/SharedBankCommon_h_ast.c"
