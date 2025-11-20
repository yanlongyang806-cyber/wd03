/***************************************************************************
*     Copyright (c) 2013, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslPromoGameCurrency.h"
#include "MicroTransactions.h"
#include "accountnet.h"
#include "stdtypes.h"
#include "TransactionOutcomes.h"
#include "Entity.h"
#include "inventoryCommon.h"
#include "AutoTransDefs.h"
#include "AccountProxyCommon.h"
#include "objTransactions.h"
#include "Player.h"
#include "EntityLib.h"
#include "NotifyEnum.h"
#include "GameStringFormat.h"
#include "LoggedTransactions.h"

#include "AutoGen/accountnet_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/gslPromoGameCurrency_c_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_TRANSACTION
ATR_LOCKS(playerEnt, ".Pplayer.Accountid, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Pscpdata.Isummonedscp, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[]")
ATR_LOCKS(srcLock, ".Plock.Result, .Plock.Uaccountid, .Plock.Fdestroytime, .Plock.Etransactiontype, .Plock.Pkey");
enumTransactionOutcome
gslPromoGameCurrency_tr_Claim(ATR_ARGS, NOCONST(Entity) *playerEnt, NOCONST(AccountProxyLockContainer) *srcLock, const char *destinationNumericName, U32 quantity, ItemChangeReason *reason)
{

    if ( ISNULL(playerEnt) )
    {
        TRANSACTION_RETURN_FAILURE("%s: Null Entity", __FUNCTION__);
    }

    if ( ISNULL(srcLock) )
    {
        TRANSACTION_RETURN_FAILURE("%s: Null AccountProxy Lock", __FUNCTION__);
    }

    if ( !APFinalizeKeyValue(srcLock, playerEnt->pPlayer->accountID, microtrans_GetPromoGameCurrencyKey(), APRESULT_COMMIT, TransLogType_Other) )
    {
        TRANSACTION_RETURN_FAILURE("%s: AccountProxy lock key finalize failed", __FUNCTION__);
    }

    if ( !inv_ent_trh_AddNumeric(ATR_PASS_ARGS, playerEnt, false, destinationNumericName, (S32)quantity, reason ) )
    {
        TRANSACTION_RETURN_FAILURE("%s: Failed to claim %u promo currency to numeric %s", __FUNCTION__, quantity, destinationNumericName);
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_STRUCT;
typedef struct ClaimPromoGameCurrencyState
{
    ContainerID playerID;
    U32 quantity;
} ClaimPromoGameCurrencyState;

static void
ClaimPromoCB(TransactionReturnVal *returnVal, ClaimPromoGameCurrencyState *claimState)
{
    Entity *playerEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, claimState->playerID);
    if ( returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS )
    {
        if ( playerEnt )
        {
            ClientCmd_NotifySend(playerEnt, kNotifyType_PromoGameCurrencyClaimFailed, entTranslateMessageKey(playerEnt, "PromoGameCurrency.FailedClaim"), NULL, NULL);
        }
    }
    StructDestroy(parse_ClaimPromoGameCurrencyState, claimState);
}

static void
ClaimPromoLockCB(AccountKeyValueResult result, U32 accountID, SA_PARAM_NN_STR const char *key, ContainerID lockID, SA_PARAM_OP_VALID ClaimPromoGameCurrencyState *claimState)
{
    Entity *playerEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, claimState->playerID);

    if ( result == AKV_SUCCESS )
    {
        ItemChangeReason reason = {0};
        TransactionReturnVal *returnVal;

        inv_FillItemChangeReason(&reason, playerEnt, "PromoGameCurrency:Claim", NULL);
        returnVal = LoggedTransactions_CreateManagedReturnValEnt("PromoGameCurrency:Claim", playerEnt, ClaimPromoCB, claimState);
        AutoTrans_gslPromoGameCurrency_tr_Claim(returnVal, objServerType(), playerEnt->myEntityType, playerEnt->myContainerID, 
            GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, lockID, microtrans_GetPromoGameCurrencyWithdrawNumericName(), claimState->quantity, &reason);
    }
    else
    {
        if ( playerEnt )
        {
            ClientCmd_NotifySend(playerEnt, kNotifyType_PromoGameCurrencyClaimFailed, entTranslateMessageKey(playerEnt, "PromoGameCurrency.FailedClaim"), NULL, NULL);
        }
        StructDestroy(parse_ClaimPromoGameCurrencyState, claimState);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void
gslPromoGameCurrency_Claim(Entity *playerEnt, U32 quantity)
{
    ClaimPromoGameCurrencyState *claimState = StructCreate(parse_ClaimPromoGameCurrencyState);

    if ( playerEnt == NULL || playerEnt->pPlayer == NULL || microtrans_GetPromoGameCurrencyKey() == NULL || microtrans_GetPromoGameCurrencyWithdrawNumericName() == NULL )
    {
        return;
    }

    claimState->playerID = playerEnt->myContainerID;
    claimState->quantity = quantity;

    APChangeKeyValue(playerEnt->pPlayer->accountID, microtrans_GetPromoGameCurrencyKey(), -(S32)quantity, ClaimPromoLockCB, claimState);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void
gslPromoGameCurrency_ClaimDebug(Entity *playerEnt, U32 quantity)
{
    gslPromoGameCurrency_Claim(playerEnt, quantity);
}

#include "AutoGen/gslPromoGameCurrency_c_ast.c"