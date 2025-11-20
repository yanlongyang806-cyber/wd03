/***************************************************************************
*     Copyright (c) 2005-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GameServerLib.h"
#include "EntityIterator.h"
#include "Entity.h"
#include "gslExtern.h"
#include "gslEntity.h"
#include "StringUtil.h"
#include "gslTransactions.h"
#include "LoggedTransactions.h"
#include "Player.h"
#include "inventoryCommon.h"
#include "EntityLib.h"
#include "GameAccountDataCommon.h"
#include "gslSharedBank.h"
#include "AutoTransDefs.h"
#include "inventoryTransactions.h"
#include "SharedBankCommon.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/ObjectDB_autogen_remotefuncs.h"
#include "AutoGen/Player_h_ast.h"


AUTO_TRANSACTION
	ATR_LOCKS(pSharedBank, ".Psaved.Pscpdata, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Pinventoryv2, .Hallegiance, .Hsuballegiance, .Psaved.Conowner.Containertype,\
						   .Itemidmax, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid,\
						   .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets,\
						   .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Uiindexbuild, .Pchar.Ilevelexp, .Pplayer.Pugckillcreditlimit, .Pplayer.Playertype");
enumTransactionOutcome SharedBank_tr_SetDefaultData(ATR_ARGS, NOCONST(Entity) *pSharedBank, const ItemChangeReason *pReason)
{
	if(pSharedBank->myEntityType == GLOBALTYPE_ENTITYSHAREDBANK)
	{
		// already set up
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	pSharedBank->myEntityType = GLOBALTYPE_ENTITYSHAREDBANK;

	// set up inventory
	// Use SharedBank template
	inv_ent_trh_InitAndFixupInventory(ATR_PASS_ARGS, pSharedBank, (DefaultInventory*)RefSystem_ReferentFromString(g_hDefaultInventoryDict,"SharedBank"),true, false, pReason);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(pSharedBankEnt, ".pInventoryV2.Ppinventorybags[], .Pplayer.Playertype, pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome SharedBank_tr_FixupSharedBank(ATR_ARGS, NOCONST(Entity) *pSharedBankEnt, GameAccountDataExtract *pExtract, S32 iNewBankSize)
{
	S32 iBankSize;
	NOCONST(InventoryBag) *pBag;

	if (ISNULL(pSharedBankEnt) 
		|| ISNULL(pSharedBankEnt->pInventoryV2) 
		|| !(pBag = inv_trh_GetBag(ATR_PASS_ARGS, pSharedBankEnt, 2 /* Literal InvBagIDs_Inventory */, pExtract)))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	
	iBankSize = invbag_trh_maxslots(pSharedBankEnt, pBag);
	if (iNewBankSize > iBankSize)
	{
        inv_bag_trh_SetMaxSlots(ATR_PASS_ARGS, pSharedBankEnt, pBag, iNewBankSize);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void SharedBank_FixupCB(TransactionReturnVal* pReturn, void* pData)
{

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, (uintptr_t)pData);
		if(pEnt)
		{
			Errorf("Failure to fixu-p shared bank with ent ID %d", pEnt->myContainerID);
		}
		else
		{
			Errorf("Failure to fix-up shared bank with no ent ID");
		}
	}
}

// Set the ref to the container, do a shared bank create
// This is called in by gslPeriodicUpdate (once per 5 seconds)
void SharedBankFixupCheck(Entity *pEnt)
{
	PERFINFO_AUTO_START_FUNC();

	if(pEnt && pEnt->pPlayer)
	{
		Entity *pSharedBankEnt = GET_REF(pEnt->pPlayer->hSharedBank);
		if(pSharedBankEnt)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			if(pExtract)
			{
				if(SharedBank_SharedBankNeedsFixup(pEnt, pSharedBankEnt, pExtract))
				{
					TransactionReturnVal* pReturn = objCreateManagedReturnVal(SharedBank_FixupCB, (void *)(intptr_t)entGetContainerID(pEnt));
					S32 iNewBankSize = SharedBank_GetNumSlots(pEnt, pExtract, true);
					AutoTrans_SharedBank_tr_FixupSharedBank(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYSHAREDBANK, pEnt->pPlayer->accountID, pExtract, iNewBankSize);
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

static void CreateSharedBank_CB(TransactionReturnVal *returnVal, SharedBankCBData *cbData)
{
	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		{
			free(cbData);
			return;
		}
	case TRANSACTION_OUTCOME_SUCCESS:
		{
			NOCONST(Entity) *pEntNoconst = NULL;
			Entity *pEnt = entFromContainerIDAnyPartition(cbData->ownerType, cbData->ownerID);
			ItemChangeReason reason = {0};

			if(pEnt)
			{
				char idBuf[128];
				// subscribe the bank
				pEntNoconst = CONTAINER_NOCONST(Entity, pEnt);
				SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSHAREDBANK), ContainerIDToString(pEnt->pPlayer->accountID, idBuf), pEntNoconst->pPlayer->hSharedBank);
				entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);

				AutoTrans_inv_tr_FixupItemIDs(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYSHAREDBANK, cbData->accountID);

				inv_FillItemChangeReason(&reason, pEnt, "SharedBank", "SetDefault");
			}

			AutoTrans_SharedBank_tr_SetDefaultData(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYSHAREDBANK, cbData->accountID, &reason);

			if (cbData->pFunc)
				cbData->pFunc(returnVal, cbData->pUserData);

			free(cbData);

			return;
		}
	}
}

void RestoreSharedBank_CB(TransactionReturnVal *returnVal, SharedBankCBData *cbData)
{
	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		{
			free(cbData);
			return;
		}
	case TRANSACTION_OUTCOME_SUCCESS:
		{
			bool containerExists;
			if(RemoteCommandCheck_DBCheckAccountWideContainerExistsWithRestore(returnVal, &containerExists) == TRANSACTION_OUTCOME_SUCCESS)
			{
				if(containerExists)
				{
					CreateSharedBank_CB(returnVal, cbData);
					return;
				}
				else if (cbData->accountID > 0)
				{
					TransactionRequest *request = objCreateTransactionRequest();

					objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
						"VerifyContainer containerIDVar %s %d",
						GlobalTypeToName(GLOBALTYPE_ENTITYSHAREDBANK),
						cbData->accountID);
					objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
						objCreateManagedReturnVal(CreateSharedBank_CB, cbData), "EnsureGameContainerExists", request);
					objDestroyTransactionRequest(request);
				}
			}
		}
	}
}


// Make sure container exists, restoring if necessary. If not create at account id.
bool gslSharedBankLoadOrCreate(Entity *pEntity)
{
	GameAccountData *pGameAccount = entity_GetGameAccount(pEntity);
	SharedBankCBData *cbData = NULL;

	// not a player character
	if(!pEntity->pPlayer)
	{
		return false;
	}

	cbData = calloc(sizeof(SharedBankCBData), 1);
	cbData->ownerType = entGetType(pEntity);
	cbData->ownerID = entGetContainerID(pEntity);
	cbData->accountID = pEntity->pPlayer->accountID;
	cbData->pFunc = NULL;
	cbData->pUserData = NULL;

	RemoteCommand_DBCheckAccountWideContainerExistsWithRestore(objCreateManagedReturnVal(RestoreSharedBank_CB, cbData), GLOBALTYPE_OBJECTDB, 0, pEntity->pPlayer->accountID, GLOBALTYPE_ENTITYSHAREDBANK);

	return true;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pSharedBankEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome SharedBank_tr_DoNumericTransfer(ATR_ARGS, NOCONST(Entity) *pEnt, NOCONST(Entity) *pSharedBankEnt, S32 iToBank, const char *pcNumeric, const ItemChangeReason *pReason)
{
	if(ISNULL(pEnt))
	{
		TRANSACTION_RETURN_LOG_FAILURE( "Shared Bank Numeric: No player ent");
	}

	if(ISNULL(pSharedBankEnt))
	{
		TRANSACTION_RETURN_LOG_FAILURE( "Shared Bank Numeric: No shared bank");
	}

	if(!inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, pEnt, true, pcNumeric, -iToBank, NumericOp_Add, pReason))
	{
		TRANSACTION_RETURN_LOG_FAILURE( "Shared Bank Numeric: Failed to add to ent");
	}

	if(!inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, pSharedBankEnt, true, pcNumeric, iToBank, NumericOp_Add, pReason))
	{
		TRANSACTION_RETURN_LOG_FAILURE( "Shared Bank Numeric: Failed to add to bank");
	}

	TRANSACTION_RETURN_LOG_SUCCESS( "Shared Bank numeric transfer succeeded ");
}

// transfer a numeric into or from the sharedbank, + is a transfer to the bank
void gslSharedBank_TransferNumeric(Entity *pEnt, S32 iToBank, const char *pchNumeric)
{
	SharedBankError error = SharedBank_ValidateNumericTransfer(pEnt, iToBank, pchNumeric);
	if(error == SharedBankError_None)
	{
		Entity *pSharedBank = GET_REF(pEnt->pPlayer->hSharedBank);
		ItemChangeReason reason = {0};

		inv_FillItemChangeReason(&reason, pEnt, "SharedBank", "TransferNumeric");

		// do the transaction
		AutoTrans_SharedBank_tr_DoNumericTransfer(NULL, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, pEnt->myContainerID, pSharedBank->myEntityType, pSharedBank->myContainerID, iToBank, pchNumeric, &reason);
	}
	else
	{
		// error 
	}
}