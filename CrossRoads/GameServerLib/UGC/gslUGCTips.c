/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslUGCTips.h"
#include "UGCTipsCommon.h"
#include "Entity.h"
#include "GameAccountDataCommon.h"
#include "Player.h"
#include "AutoTransDefs.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "EntityLib.h"
#include "StringUtil.h"
#include "NotifyEnum.h"
#include "StringFormat.h"
#include "GameStringFormat.h"
#include "LoggedTransactions.h"
#include "UGCProjectCommon.h"
#include "UGCAchievements.h"
#include "mission_common.h"
#include "AccountProxyCommon.h"

#include "GameAccountData/GameAccountData.h"

#include "AutoGen/gslUGCTips_c_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/GameAccountData_h_ast.h"
#include "AutoGen/UGCTipsCommon_h_ast.h"
#include "AutoGen/UGCAchievements_h_ast.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/UGCProjectCommon_h_ast.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Give Tip

static void ugc_trh_AchievementEventForTipping(ATR_ARGS, ContainerID uTipperAccountID, ContainerID uProjectID, ContainerID uAuthorAccountID, int iTipAmount)
{
	UGCAchievementEvent *event = StructCreate(parse_UGCAchievementEvent);
	event->uUGCAuthorID = uAuthorAccountID;
	event->uUGCProjectID = uProjectID;
	event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
	event->ugcAchievementServerEvent->ugcProjectTippedEvent = StructCreate(parse_UGCProjectTippedEvent);
	event->ugcAchievementServerEvent->ugcProjectTippedEvent->uTipAmount = (U32)iTipAmount;
	QueueRemoteCommand_gslUGC_ugcAchievementEvent_Send(ATR_RESULT_SUCCESS, GetAppGlobalType(), GetAppGlobalID(), event, __FUNCTION__);
	StructDestroy(parse_UGCAchievementEvent, event);

	event = StructCreate(parse_UGCAchievementEvent);
	event->uUGCAuthorID = uTipperAccountID;
	event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
	event->ugcAchievementServerEvent->ugcTippedProjectEvent = StructCreate(parse_UGCTippedProjectEvent);
	event->ugcAchievementServerEvent->ugcTippedProjectEvent->uTipAmount = (U32)iTipAmount;
	QueueRemoteCommand_gslUGC_ugcAchievementEvent_Send(ATR_RESULT_SUCCESS, GetAppGlobalType(), GetAppGlobalID(), event, __FUNCTION__);
	StructDestroy(parse_UGCAchievementEvent, event);
}

AUTO_STRUCT;
typedef struct UGCGiveTipData
{
	ContainerID uTipperEntityID;
	ContainerID uTipperAccountID;

	ContainerID uProjectID;

	ContainerID uAuthorAccountID;

	char* pchProjName;
	char* pchAuthName;

	int iAmount;

	// Only used if gConf.bDontAllowGADModification (multi-shard environment)
	const char *key;				AST(UNOWNED)
} UGCGiveTipData;

////////////////////////////////////////

static void GivetipNotifyError(Entity *pEntity, const char* projectName, U32 uProjectID)
{
    char *notifyStr = NULL;
	entFormatGameMessageKey(pEntity, &notifyStr, "FoundryTips.GivetipError",
			STRFMT_STRING("ProjectName", projectName),
			STRFMT_INT("ProjectID", uProjectID),
			STRFMT_END);
    ClientCmd_NotifySend(pEntity, kNotifyType_FoundryTipsFailure, notifyStr, NULL, NULL);
    estrDestroy(&notifyStr);
}

static void GivetipNotifySuccess(Entity *pEntity, const char* projectName, U32 uProjectID, U32 tipAmount)
{
    char *notifyStr = NULL;
	entFormatGameMessageKey(pEntity, &notifyStr, "FoundryTips.GivetipFeedback",
			STRFMT_STRING("ProjectName", projectName),
			STRFMT_INT("ProjectID", uProjectID),
			STRFMT_INT("Quantity", tipAmount),
			STRFMT_END);
	
    ClientCmd_NotifySend(pEntity, kNotifyType_FoundryTipsSuccess, notifyStr, NULL, NULL);
    estrDestroy(&notifyStr);
}

static void UGCTipsGiveTip_CB(TransactionReturnVal *returnVal, UGCGiveTipData *pUGCGiveTipData)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pUGCGiveTipData->uTipperEntityID);
	if(pEntity)
	{
		if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
			GivetipNotifySuccess(pEntity, pUGCGiveTipData->pchProjName, pUGCGiveTipData->uProjectID, pUGCGiveTipData->iAmount);
		else
			GivetipNotifyError(pEntity, pUGCGiveTipData->pchProjName, pUGCGiveTipData->uProjectID);
	}

	StructDestroy(parse_UGCGiveTipData, pUGCGiveTipData);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pTipperGameAccountData, ".Iaccountid, .Eatiprecords");
bool gslUGCTips_trh_RecordTip(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pTipperGameAccountData, U32 uAuthorAccountID)
{
	U32 curTime = timeSecondsSince2000();
	U32 startOfCurrentInterval;
	int iMaxTipsPerPeriod=gUGCTipsConfig.allowedTipsPerTimeInterval;
	int i;
	NOCONST(FoundryTipRecord) *pNewRecord = NULL;

    // compute the start time of the current time interval
    startOfCurrentInterval = ( curTime / gUGCTipsConfig.timeIntervalSeconds ) * gUGCTipsConfig.timeIntervalSeconds;

	// Clean out records from before this interval and check to see if we've already tipped this author
	for(i = eaSize(&(pTipperGameAccountData->eaTipRecords))-1; i>=0; i--)
	{
		if (pTipperGameAccountData->eaTipRecords[i]->uTimeOfTip < startOfCurrentInterval)
		{
			StructDestroyNoConst(parse_FoundryTipRecord, eaRemove(&(pTipperGameAccountData->eaTipRecords),i));
		}
		else
		{
			if (pTipperGameAccountData->eaTipRecords[i]->uTipAuthorAccountID == uAuthorAccountID)
			{
				// Already tipped this one
				TRANSACTION_APPEND_LOG_FAILURE("Account[%d] has already tipped account[%d].", pTipperGameAccountData->iAccountID, uAuthorAccountID);
				return(false);
			}
		}
	}

	if (eaSize(&(pTipperGameAccountData->eaTipRecords)) >= iMaxTipsPerPeriod)
	{
		// Too many tips
		TRANSACTION_APPEND_LOG_FAILURE("Account[%d] has already tipped %d times in this tipping period.", pTipperGameAccountData->iAccountID, iMaxTipsPerPeriod);
		return(false);
	}
	
	pNewRecord = StructCreateNoConst(parse_FoundryTipRecord);
	pNewRecord->uTipAuthorAccountID = uAuthorAccountID;
	pNewRecord->uTimeOfTip = curTime;

	eaPush(&(pTipperGameAccountData->eaTipRecords), pNewRecord);

	return(true);
}

// this transaction will only work in a single shard environment
AUTO_TRANSACTION
ATR_LOCKS(pTipperGameAccountData, ".Iaccountid, .Eatiprecords")
ATR_LOCKS(pAuthorGameAccountData, ".Iaccountid, .Ifoundrytipbalance")
ATR_LOCKS(pProject, ".Ugclifetimetips")
ATR_LOCKS(pTipperEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome trGiveTipFoundryProject(ATR_ARGS, NOCONST(Entity) *pTipperEnt, NOCONST(UGCProject) *pProject, ContainerID uProjectID,
												NOCONST(GameAccountData) *pTipperGameAccountData,										   
												NOCONST(GameAccountData) *pAuthorGameAccountData,										   
												int iTipAmount, const ItemChangeReason *pReason)
{
	// this transaction will only work in a single shard environment

    ItemDef *tipsItemDef;
	U32 uAuthorAccountID;
	U32 uTipperAccountID;

    if ( ISNULL(pTipperEnt) || ISNULL(pTipperEnt->pPlayer) || ISNULL(pTipperGameAccountData) || ISNULL(pAuthorGameAccountData)) 
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    tipsItemDef = GET_REF(gUGCTipsConfig.hTipsNumeric);

	if ( ISNULL(tipsItemDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }
	
	uAuthorAccountID = pAuthorGameAccountData->iAccountID;
	uTipperAccountID = pTipperGameAccountData->iAccountID;

	if (!uAuthorAccountID)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Invalid author account ID");
	}

	if (!uTipperAccountID)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Invalid tipper account ID");
	}

	if (uTipperAccountID == uAuthorAccountID)
	{
		TRANSACTION_RETURN_LOG_FAILURE("You can't give a tip for one of your own missions");
	}

	// Check if tip amount more than tipper has
    if (iTipAmount > inv_trh_GetNumericValue(ATR_PASS_ARGS, pTipperEnt, tipsItemDef->pchName))
	{
		TRANSACTION_RETURN_LOG_FAILURE("You can't tip more than you have.");
	}
	
    // save quantity tipped to the result string, so that it can be passed back to the transaction callback
    // NOTE - this must happen before adjusting the numerics, because that will append logging info to the success string
    estrPrintf(ATR_RESULT_SUCCESS, "%d\n", iTipAmount);

	//	remove amount from tipper player
    if ( !inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pTipperEnt, true, tipsItemDef->pchName, -iTipAmount, pReason) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

	//  add amount to author account
	if ( GameAccount_tr_AddToUGCTips(ATR_RECURSE, pAuthorGameAccountData, iTipAmount) == TRANSACTION_OUTCOME_FAILURE )
	{
        return TRANSACTION_OUTCOME_FAILURE;
	}

	// Check Already tipped this author
	// Check Tipped too many times
	// Log time, author, amount to tipper account tip list
	// Trim log to current day
	if (!gslUGCTips_trh_RecordTip(ATR_PASS_ARGS, pTipperGameAccountData, uAuthorAccountID))
	{
        return TRANSACTION_OUTCOME_FAILURE;
	}

	// add amount to UGC project record
	pProject->ugcLifetimeTips += iTipAmount;

	ugc_trh_AchievementEventForTipping(ATR_PASS_ARGS, uTipperAccountID, uProjectID, uAuthorAccountID, iTipAmount);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gslUGC_IncrementProjectLifetimeTips(ContainerID uProjectID, U32 uTipAmount)
{
	RemoteCommand_Intershard_aslUGCDataManager_IncrementProjectLifetimeTips(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, uProjectID, uTipAmount);
}

// this transaction will work in a multi-shard environment
AUTO_TRANSACTION
ATR_LOCKS(pTipperEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Pinventoryv2.Ppinventorybags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pTipperGameAccountData, ".Iaccountid, .Eatiprecords")
ATR_LOCKS(pAccountProxyLockContainer, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype");
enumTransactionOutcome trGiveTipFoundryProject2(ATR_ARGS, NOCONST(Entity) *pTipperEnt, NOCONST(GameAccountData) *pTipperGameAccountData, NOCONST(AccountProxyLockContainer) *pAccountProxyLockContainer,
	ContainerID uTipperAccountID, ContainerID uAuthorAccountID, int iTipAmount, ContainerID uProjectID, const char *key, const ItemChangeReason *pReason)
{
	// this transaction will work in a multi-shard environment

	ItemDef *tipsItemDef;

	if(ISNULL(pTipperEnt) || ISNULL(pTipperEnt->pPlayer))
		return TRANSACTION_OUTCOME_FAILURE;

	tipsItemDef = GET_REF(gUGCTipsConfig.hTipsNumeric);

	if(ISNULL(tipsItemDef))
		return TRANSACTION_OUTCOME_FAILURE;

	if(!uAuthorAccountID)
		TRANSACTION_RETURN_LOG_FAILURE("Invalid author account ID");

	if(!uTipperAccountID)
		TRANSACTION_RETURN_LOG_FAILURE("Invalid tipper account ID");

	if(uTipperAccountID == uAuthorAccountID)
		TRANSACTION_RETURN_LOG_FAILURE("You can't give a tip for one of your own missions");

	// Check if tip amount more than tipper has
	if(iTipAmount > inv_trh_GetNumericValue(ATR_PASS_ARGS, pTipperEnt, tipsItemDef->pchName))
		TRANSACTION_RETURN_LOG_FAILURE("You can't tip more than you have.");

	// save quantity tipped to the result string, so that it can be passed back to the transaction callback
	// NOTE - this must happen before adjusting the numerics, because that will append logging info to the success string
	estrPrintf(ATR_RESULT_SUCCESS, "%d\n", iTipAmount);

	//remove amount from tipper player
	if(!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pTipperEnt, true, tipsItemDef->pchName, -iTipAmount, pReason))
		return TRANSACTION_OUTCOME_FAILURE;

	// finalize the addition of the tip to the author's Account
	if(!APFinalizeKeyValue(pAccountProxyLockContainer, uAuthorAccountID, key, APRESULT_COMMIT, TransLogType_FoundryTips))
		return TRANSACTION_OUTCOME_FAILURE;

	// Check Already tipped this author
	// Check Tipped too many times
	// Log time, author, amount to tipper account tip list
	// Trim log to current day
	if(!gslUGCTips_trh_RecordTip(ATR_PASS_ARGS, pTipperGameAccountData, uAuthorAccountID))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	QueueRemoteCommand_gslUGC_IncrementProjectLifetimeTips(ATR_RESULT_SUCCESS, GetAppGlobalType(), GetAppGlobalID(), uProjectID, (U32)iTipAmount);

	ugc_trh_AchievementEventForTipping(ATR_PASS_ARGS, uTipperAccountID, uProjectID, uAuthorAccountID, iTipAmount);

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void FoundryTip_APChangeKeyValue_CB(AccountKeyValueResult result, U32 accountID, SA_PARAM_NN_STR const char *key, ContainerID containerID, SA_PARAM_OP_VALID UGCGiveTipData *pUGCGiveTipData)
{
	Entity *pTipperEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pUGCGiveTipData->uTipperEntityID);

	// This is OK! We've determined that it's okay for tip records to continue to be stored on the GAD. Thus, we should /not/ alter how the tip grant/withdraw transactions act upon the tip records.
	GameAccountData *pTipperGameAccountData = entity_GetGameAccount(pTipperEntity);

	devassert(accountID == pUGCGiveTipData->uAuthorAccountID);
	devassert(pUGCGiveTipData->key == key || 0 == stricmp(pUGCGiveTipData->key, key));

	if(pTipperEntity)
	{
		if(result == AKV_SUCCESS)
		{
			TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnValEnt("UGCTipsGiveTip", pTipperEntity, UGCTipsGiveTip_CB, pUGCGiveTipData);
			ItemChangeReason reason = {0};
			inv_FillItemChangeReason(&reason, pTipperEntity, "UGCTipsGiveTip", pUGCGiveTipData->pchAuthName);

			// Now that we have the lock, deduct from the tipper's inventory and finalize the value change for the author.
			AutoTrans_trGiveTipFoundryProject2(pReturn, GetAppGlobalType(),
				GLOBALTYPE_ENTITYPLAYER, pUGCGiveTipData->uTipperEntityID,
				GLOBALTYPE_GAMEACCOUNTDATA, pTipperGameAccountData->iAccountID,
				GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID,
				pUGCGiveTipData->uTipperAccountID,
				pUGCGiveTipData->uAuthorAccountID,
				pUGCGiveTipData->iAmount,
				pUGCGiveTipData->uProjectID,
				key,
				&reason);

			return; // early out because we do not want to destroy pUGCGiveTipData until UGCTipsGiveTip_CB
		}
		else
			GivetipNotifyError(pTipperEntity, pUGCGiveTipData->pchProjName, pUGCGiveTipData->uProjectID);
	}
	else
	{
		// rollback the lock because the tipper disconnected. this should be a rare case.
		if(result == AKV_SUCCESS)
			AutoTrans_AccountProxy_tr_RollbackLock(NULL, GLOBALTYPE_ACCOUNTPROXYSERVER, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID, accountID, key);
	}

	StructDestroy(parse_UGCGiveTipData, pUGCGiveTipData);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gslUGCTips_GiveTip(Entity *pTipperEntity, UGCProject *pProject, int iTipAmount, int iTipIndex)
{
	MissionInfo *pInfo = NULL;
	int iProjectID = 0;
	GameAccountData* pTipperGameAccountData = NULL;

	if(!UGCTipsEnabled())
	{
		GivetipNotifyError(pTipperEntity, "<Disabled>", 0);
		return;
	}

	if(pTipperEntity == NULL || pProject == NULL)
	{
		GivetipNotifyError(pTipperEntity, "<Empty>", 0);
		return;
	}

	iProjectID = pProject->id;

	pInfo = mission_GetInfoFromPlayer(pTipperEntity);
	pTipperGameAccountData = gConf.bDontAllowGADModification ? NULL : entity_GetGameAccount(pTipperEntity);
	if(pInfo == NULL || (pTipperGameAccountData == NULL && !gConf.bDontAllowGADModification))
	{
		GivetipNotifyError(pTipperEntity, pProject->pPublishedVersionName, iProjectID);
		return;
	}

	// We only allow tipping if the uProjectID matches the last one that a rating request came through for.
	//  Reviewing has additional cases where it is allowed
	if(pInfo->uLastMissionRatingRequestID == (U32)iProjectID)
	{
		UGCGiveTipData *pUGCGiveTipData = NULL;

		// Check that the amount matches the tip index. The index is off by one since the list contains an empty "no tip" entry
		if(iTipIndex > ea32Size(&(gUGCTipsConfig.pTipAmounts)) || iTipAmount != gUGCTipsConfig.pTipAmounts[iTipIndex - 1])
		{
			GivetipNotifyError(pTipperEntity, pProject->pPublishedVersionName, iProjectID);
			return;
		}

		pUGCGiveTipData = StructCreate(parse_UGCGiveTipData);
		pUGCGiveTipData->uTipperEntityID = entGetContainerID(pTipperEntity);
		pUGCGiveTipData->uTipperAccountID = entGetAccountID(pTipperEntity);
		pUGCGiveTipData->uAuthorAccountID = pProject->iOwnerAccountID;
		pUGCGiveTipData->uProjectID = iProjectID;
		pUGCGiveTipData->pchAuthName = StructAllocString(pProject->pOwnerAccountName);
		pUGCGiveTipData->pchProjName = StructAllocString(pProject->pPublishedVersionName);
		pUGCGiveTipData->iAmount = iTipAmount;

		if(!gConf.bDontAllowGADModification)
		{
			TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnValEnt("UGCTipsGiveTip", pTipperEntity, UGCTipsGiveTip_CB, pUGCGiveTipData);
			ItemChangeReason reason = {0};

			inv_FillItemChangeReason(&reason, pTipperEntity, "UGCTipsGiveTip", pUGCGiveTipData->pchAuthName);

			// this transaction will only work in a single shard environment
			AutoTrans_trGiveTipFoundryProject(pReturn, GetAppGlobalType(),
				pTipperEntity->myEntityType, pTipperEntity->myContainerID,
				GLOBALTYPE_UGCPROJECT, iProjectID, iProjectID,
				GLOBALTYPE_GAMEACCOUNTDATA, pTipperGameAccountData->iAccountID,
				GLOBALTYPE_GAMEACCOUNTDATA, pProject->iOwnerAccountID,
				iTipAmount, &reason);
		}
		else
		{
			// multi-shard path
			pUGCGiveTipData->key = microtrans_GetShardFoundryTipBucketKey();

			APChangeKeyValue(pProject->iOwnerAccountID, pUGCGiveTipData->key, iTipAmount, FoundryTip_APChangeKeyValue_CB, pUGCGiveTipData);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Withdraw Tips

AUTO_STRUCT;
typedef struct UGCWithdrawTipsData
{
	ContainerID uAuthorEntityID;
	ContainerID uAuthorAccountID;

	int iWithdrawAmount;

	// Only used if gConf.bDontAllowGADModification (multi-shard environment)
	const char *key;				AST(UNOWNED)
} UGCWithdrawTipsData;

static void WithdrawNotifyError(Entity *pEnt)
{
    char *notifyStr = NULL;
    langFormatGameMessageKey(entGetLanguage(pEnt), &notifyStr, "FoundryTips.InternalError", STRFMT_END);
    ClientCmd_NotifySend(pEnt, kNotifyType_FoundryTipsFailure, notifyStr, NULL, NULL);
    estrDestroy(&notifyStr);
}

static void WithdrawNotifySuccess(Entity *pEnt, U32 quantity)
{
    char *notifyStr = NULL;
    langFormatGameMessageKey(entGetLanguage(pEnt), &notifyStr, "FoundryTips.Success", STRFMT_INT("Quantity", quantity), STRFMT_END);
    ClientCmd_NotifySend(pEnt, kNotifyType_FoundryTipsSuccess, notifyStr, NULL, NULL);
    estrDestroy(&notifyStr);
}

void UGCTipsWithdraw_CB(TransactionReturnVal *pReturn, UGCWithdrawTipsData *pUGCWithdrawTipsData)
{
    Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pUGCWithdrawTipsData->uAuthorEntityID);
    if ( pEnt )
    {
        if ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
        {
            U32 amountWitdrawn = 0;

            if ( !StringToUint(objAutoTransactionGetResult(pReturn), &amountWitdrawn) )
			{
				amountWitdrawn = 0;
			}

			WithdrawNotifySuccess(pEnt, amountWitdrawn);
		}
		else
		{
			WithdrawNotifyError(pEnt);
		}
	}

	StructDestroy(parse_UGCWithdrawTipsData, pUGCWithdrawTipsData);
}

// this transaction will only work in a single shard environment
AUTO_TRANSACTION
ATR_LOCKS(pGameAccountData, ".Ifoundrytipbalance")
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome trUGCTipsWithdraw(ATR_ARGS, NOCONST(Entity) *pEnt, NOCONST(GameAccountData) *pGameAccountData, int iWithdrawAmount, const ItemChangeReason *pReason)
{
	// this transaction will only work in a single shard environment
	ItemDef *tipsItemDef;

	if ( ISNULL(pEnt) || ISNULL(pEnt->pPlayer)) 
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	tipsItemDef = GET_REF(gUGCTipsConfig.hTipsNumeric);

	if ( ISNULL(tipsItemDef) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Last chance amount check
	if (iWithdrawAmount > pGameAccountData->iFoundryTipBalance)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	
	// save quantity converted to the result string, so that it can be passed back to the transaction callback
	// NOTE - this must happen before adjusting the numerics, because that will append logging info to the success string
	estrPrintf(ATR_RESULT_SUCCESS, "%u\n", iWithdrawAmount);

	// transfer the numeric
	if ( !inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, true, tipsItemDef->pchName, iWithdrawAmount, pReason) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if ( GameAccount_tr_AddToUGCTips(ATR_RECURSE, pGameAccountData, -iWithdrawAmount) == TRANSACTION_OUTCOME_FAILURE )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

// this transaction will work in a multi-shard environment
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Pinventoryv2.Ppinventorybags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pAccountProxyLockContainer, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype");
enumTransactionOutcome trUGCTipsWithdraw2(ATR_ARGS, NOCONST(Entity) *pEnt, NOCONST(AccountProxyLockContainer) *pAccountProxyLockContainer, ContainerID uAuthorAccountID, const char *key,
		int iWithdrawAmount, const ItemChangeReason *pReason)
{
	// this transaction will work in a multi-shard environment
	ItemDef *tipsItemDef = NULL;

	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
		return TRANSACTION_OUTCOME_FAILURE;

	tipsItemDef = GET_REF(gUGCTipsConfig.hTipsNumeric);

	if(ISNULL(tipsItemDef))
		return TRANSACTION_OUTCOME_FAILURE;

	// save quantity converted to the result string, so that it can be passed back to the transaction callback
	// NOTE - this must happen before adjusting the numerics, because that will append logging info to the success string
	estrPrintf(ATR_RESULT_SUCCESS, "%u\n", iWithdrawAmount);

	// transfer the numeric
	if(!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, true, tipsItemDef->pchName, iWithdrawAmount, pReason))
		return TRANSACTION_OUTCOME_FAILURE;

	// finalize the removal of the tips from the author's Account
	if(!APFinalizeKeyValue(pAccountProxyLockContainer, uAuthorAccountID, key, APRESULT_COMMIT, TransLogType_FoundryTips))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void FoundryWithdrawTips_APChangeKeyValue_CB(AccountKeyValueResult result, U32 accountID, SA_PARAM_NN_STR const char *key, ContainerID containerID, UGCWithdrawTipsData *pUGCWithdrawTipsData)
{
	Entity *pAuthorEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pUGCWithdrawTipsData->uAuthorEntityID);

	devassert(accountID == pUGCWithdrawTipsData->uAuthorAccountID);

	if(pAuthorEntity)
	{
		if(result == AKV_SUCCESS)
		{
			ItemChangeReason reason = {0};
			TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnValEnt("UGCTipsWithdraw", pAuthorEntity, UGCTipsWithdraw_CB, pUGCWithdrawTipsData);

			inv_FillItemChangeReason(&reason, pAuthorEntity, "UGCTipsWithdraw", NULL);

			AutoTrans_trUGCTipsWithdraw2(pReturn, GetAppGlobalType(),
				GLOBALTYPE_ENTITYPLAYER, pUGCWithdrawTipsData->uAuthorEntityID,
				GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID,
				pUGCWithdrawTipsData->uAuthorAccountID,
				key,
				pUGCWithdrawTipsData->iWithdrawAmount,
				&reason);

			return; // early out because we do not want to destroy pUGCWithdrawTipsData until UGCTipsWithdraw_CB
		}
		else
			WithdrawNotifyError(pAuthorEntity);
	}
	else
	{
		// rollback the lock because the author disconnected. this should be a rare case.
		if(result == AKV_SUCCESS)
			AutoTrans_AccountProxy_tr_RollbackLock(NULL, GLOBALTYPE_ACCOUNTPROXYSERVER, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID, accountID, key);
	}

	StructDestroy(parse_UGCWithdrawTipsData, pUGCWithdrawTipsData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD;
void gslUGCTipsWithdraw(Entity *pEnt, int iWithdrawAmount)
{
	GameAccountData* pGameAccountData = gConf.bDontAllowGADModification ? NULL : entity_GetGameAccount(pEnt);
	UGCWithdrawTipsData *pUGCWithdrawTipsData = NULL;

	if(pEnt == NULL || pEnt->pPlayer == NULL || (pGameAccountData == NULL && !gConf.bDontAllowGADModification) || !UGCTipsEnabled())
	{
		WithdrawNotifyError(pEnt);
		return;
	}

	if(!gConf.bDontAllowGADModification && iWithdrawAmount > pGameAccountData->iFoundryTipBalance)
	{
		WithdrawNotifyError(pEnt);
		return;
	}

	pUGCWithdrawTipsData = StructCreate(parse_UGCWithdrawTipsData);
	pUGCWithdrawTipsData->uAuthorEntityID = entGetContainerID(pEnt);
	pUGCWithdrawTipsData->uAuthorAccountID = entGetAccountID(pEnt);
	pUGCWithdrawTipsData->iWithdrawAmount = iWithdrawAmount;

	if(!gConf.bDontAllowGADModification)
	{
		ItemChangeReason reason = {0};
		TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnValEnt("UGCTipsWithdraw", pEnt, UGCTipsWithdraw_CB, pUGCWithdrawTipsData);

		inv_FillItemChangeReason(&reason, pEnt, "UGCTipsWithdraw", NULL);

		AutoTrans_trUGCTipsWithdraw(pReturn, GetAppGlobalType(),
									pEnt->myEntityType, entGetContainerID(pEnt),
									GLOBALTYPE_GAMEACCOUNTDATA, pGameAccountData->iAccountID,
									iWithdrawAmount, &reason);
	}
	else
	{
		// multi-shard path
		pUGCWithdrawTipsData->key = microtrans_GetShardFoundryTipBucketKey();

		APChangeKeyValue(entGetAccountID(pEnt), microtrans_GetShardFoundryTipBucketKey(), -iWithdrawAmount, FoundryWithdrawTips_APChangeKeyValue_CB, pUGCWithdrawTipsData);
	}
}

#include "AutoGen/gslUGCTips_c_ast.c"
