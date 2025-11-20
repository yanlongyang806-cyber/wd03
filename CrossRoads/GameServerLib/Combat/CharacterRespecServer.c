/***************************************************************************
*     Copyright (c) 2005-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CharacterRespecServer.h"

#include "AutoTransDefs.h"
#include "Character.h"
#include "Character_h_ast.h"
#include "CharacterAttribs.h"
#include "contact_common.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "file.h"
#include "fileutil.h"
#include "foldercache.h"
#include "GameStringFormat.h"
#include "inventoryCommon.h"
#include "microtransactions_common.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "Powers.h"
#include "PowerTree.h"
#include "PowerTree_h_ast.h"
#include "PowerTreeHelpers.h"
#include "PowerTreeHelpers_h_ast.h"
#include "PowerTreeTransactions.h"
#include "CharacterClass.h"
#include "Character_mods.h"
#include "PowerSlots.h"
#include "PowersMovement.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "gslEventSend.h"
#include "accountnet_h_ast.h"
#include "AccountProxyCommon.h"

//For game account changes
#include "GameAccountData\GameAccountData.h"
#include "GameAccountData_h_ast.h"
#include "GameAccountDataCommon.h"
#include "cmdServerCharacter.h"

#include "gslBugReport.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "objTransactions.h"
#include "LoggedTransactions.h"
#include "TransactionSystem.h"
#include "ReferenceSystem_Internal.h"
#include "autogen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

extern DictionaryHandle g_hCharacterPathDict;
//Respec stat points -------------------------------
static S32 CharacterRespecStatPoints(SA_PARAM_NN_VALID Character *pchar, AttribType eAttrib)
{
	StatPointPoolDef *pDef = StatPointPool_DefFromAttrib(eAttrib);

	if (pDef)
	{
		int iPoints = character_StatPointsSpentPerAttrib(pchar,eAttrib);
		Entity *pEnt = pchar->pEntParent;

		if(iPoints > 0)
		{
			if(!character_IsValidStatPoint(eAttrib, pDef->pchName))
			{
				DBGSTATS_printf("Cannot modify stat points for %s.",StaticDefineIntRevLookup(AttribTypeEnum,eAttrib));
				return false;
			}

			AutoTrans_trCharacter_ModifyStatPoints(
				LoggedTransactions_MakeEntReturnVal("RespecStatPoints", pEnt),
				GetAppGlobalType(),
				entGetType(pEnt),
				entGetContainerID(pEnt),
				pDef->pchName,
				eAttrib,
				iPoints*-1);

			return true;
		}
	}


	return false;
}

static void CharacterRespecStatPointsAll(SA_PARAM_NN_VALID Character *pchar)
{
	int i;

	for(i=eaSize(&pchar->ppAssignedStats)-1;i>=0;i--)
	{
		CharacterRespecStatPoints(pchar,pchar->ppAssignedStats[i]->eType);
	}
}


//Respec Power Trees --------------------------------

static void CharacterGetFreeRespecSteps(int iPartitionIdx, Character *pchar, PowerTreeSteps *pStepsReusltRespec, PowerTreeSteps *pStepsResultBuy, PTRespecGroupType eRespecType)
{
	PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);
	character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, pchar),pSteps, false, eRespecType);
	pSteps->bUpdatePointsSpent = true;
	pSteps->uiPowerTreeModCount = pchar->uiPowerTreeModCount;
	entity_PowerTreeStepsRespecSteps(iPartitionIdx,pchar->pEntParent,NULL,pSteps,NULL,pStepsReusltRespec,pStepsResultBuy,false,false);
	StructDestroy(parse_PowerTreeSteps,pSteps);
}

static void CharacterRespecPowerTrees(int iPartitionIdx, Character *pchar)
{
	PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);
	character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, pchar),pSteps, false, kPTRespecGroup_ALL);
	pSteps->bUpdatePointsSpent = true;
	pSteps->uiPowerTreeModCount = pchar->uiPowerTreeModCount;
	entity_PowerTreeStepsRespec(iPartitionIdx,pchar->pEntParent,pSteps,NULL,NULL,kPTRespecGroup_ALL,false);
	StructDestroy(parse_PowerTreeSteps,pSteps);
}



// Performs a full respec on the Character
void character_RespecFull(int iPartitionIdx, Character *pchar)
{
	if(!gConf.bDisableFullRespec) {
		CharacterRespecStatPointsAll(pchar);
		CharacterRespecPowerTrees(iPartitionIdx,pchar);
		//Let the above command handle respeccing trees.
		//Just remove the char paths and ignore the trees they granted.
		if (eaSize(&pchar->ppSecondaryPaths) > 0)
			Character_RemoveAllSecondaryCharacterPaths(pchar->pEntParent, false);
	}
}

// Respecs your advantages (all of them)
AUTO_COMMAND ACMD_NAME("RespecAdvantages") ACMD_CATEGORY(Powers, csr) ACMD_ACCESSLEVEL(4);
void RespecAdvantages(Entity *e)
{
	if(e->pChar)
	{
		PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);

		character_GetPowerTreeAdvantages(CONTAINER_NOCONST(Character, e->pChar),pSteps);
		pSteps->bUpdatePointsSpent = true;
		entity_PowerTreeStepsRespec(entGetPartitionIdx(e),e,pSteps,NULL,NULL,kPTRespecGroup_ALL,false);

		StructDestroy(parse_PowerTreeSteps,pSteps);
	}
}


// Respecs your PowerTrees back the given number of steps, optionally use the cost
AUTO_COMMAND ACMD_CATEGORY(Powers);
void RespecPowerTreesInternal(Entity *e, int steps, int usecost)
{
	if(e->pChar && steps>0)
	{
		int s;
		PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);
		character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, e->pChar),pSteps, true, kPTRespecGroup_ALL);
		s = eaSize(&pSteps->ppSteps);
		MIN1(steps,s);
		
		if(steps>0)
		{

			if(usecost)
				character_PowerTreeStepsCostRespec(entGetPartitionIdx(e),e->pChar,pSteps,steps);

			// Cut the steps down to just the number requested
			while(s > steps)
			{
				StructDestroy(parse_PowerTreeStep,pSteps->ppSteps[s-1]);
				eaRemove(&pSteps->ppSteps,s-1);
				s--;
			}

			entity_PowerTreeStepsRespec(entGetPartitionIdx(e),e,pSteps,NULL,NULL,kPTRespecGroup_ALL, true);

		}

		StructDestroy(parse_PowerTreeSteps,pSteps);
	}
}

// Sends a request to the server to send the client their PowerTreeSteps for a respec
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void RespecPowerTreesRequestSteps(Entity *e)
{
	if(e->pChar)
	{
		PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);
		character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, e->pChar),pSteps, false, kPTRespecGroup_ALL);
		//ClientCmd_RespecPowerTreesReceiveSteps(e,pSteps); // This has been deleted to save space, hopefully temporary
		StructDestroy(parse_PowerTreeSteps,pSteps);
	}
}

// Respecs your PowerTrees to nothing if they are currently invalid for some reason
AUTO_COMMAND ACMD_CATEGORY(Powers) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void RespecPowerTreesInvalid(Entity *e)
{
	if(e->pChar)
	{
		if(!entity_PowerTreesValidate(entGetPartitionIdx(e),e,NULL,NULL))
		{
			CharacterRespecPowerTrees(entGetPartitionIdx(e),e->pChar);
		}
	}
}

// Respecs a number of steps of your PowerTrees
AUTO_COMMAND ACMD_CATEGORY(Powers) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void RespecPowerTrees(Entity *e, int steps)
{
	RespecPowerTreesInternal(e,steps,true);
}

// Respecs as many steps as possible that are free
AUTO_COMMAND ACMD_CATEGORY(Powers) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void RespecPowerTreesGroupNoCost(Entity *e, PTRespecGroupType eRespecType)
{
	if(e->pChar && PowerTree_CanRespecGroupWithNumeric(eRespecType))
	{
		S32 i, s, iCount = 0, iRemoved = 0;
		PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);
		PowerTreeSteps *pStepsFinal = StructCreate(parse_PowerTreeSteps);
		bool bRequireFull = false;

		// Get everything
		character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, e->pChar),pSteps, false, eRespecType);
		character_PowerTreeStepsCostRespec(entGetPartitionIdx(e),e->pChar,pSteps,0);
		s = eaSize(&pSteps->ppSteps);

		// Put all no cost items into the list
		for(i = 0 ; i < s; ++i)
		{
			if(pSteps->ppSteps[i]->pchNode || (pSteps->ppSteps[i]->pchTree && !pSteps->ppSteps[i]->bStepIsLocked))
			{
				++iCount;
				if(pSteps->ppSteps[i]->iCostRespec == 0)
				{
					PowerTreeDef *pPowerTreeDef = RefSystem_ReferentFromString(g_hPowerTreeDefDict, pSteps->ppSteps[i]->pchTree);

					// only remove nodes
					PowerTreeStep *pFreeStep = StructClone(parse_PowerTreeStep, pSteps->ppSteps[i]);

					eaPush(&pStepsFinal->ppSteps, pFreeStep);

					if(pPowerTreeDef)
					{
						PTTypeDef *pTypeDef = GET_REF(pPowerTreeDef->hTreeType);
						if(pTypeDef && pTypeDef->bHasAutoBuyTrees)
						{
							// There are autobuy trees that use this type as a required buy. These need to be eliminated
							bRequireFull = true;
						}
					}
				}
			}
		}

		if(eaSize(&pStepsFinal->ppSteps) > 0)
		{
			bool bDoStepType = true;

			// Needs step type condition to prevent auto buy trees from being destroyed, they will have been passed in if they need removal
			// It will do a full type respec if all nodes are removed
			if(bRequireFull || eaSize(&pStepsFinal->ppSteps) == iCount)
			{
				bDoStepType = false;
			}

			entity_PowerTreeStepsRespec(entGetPartitionIdx(e),e,pStepsFinal,NULL,NULL,eRespecType, bDoStepType);
		}

		StructDestroy(parse_PowerTreeSteps,pSteps);
		StructDestroy(parse_PowerTreeSteps,pStepsFinal);
	}
}

// Respecs your PowerTrees and StatPoints to nothing
AUTO_COMMAND ACMD_NAME("RespecFull", "Character_RespecFull") ACMD_CATEGORY(Powers, csr) ACMD_ACCESSLEVEL(4);
void RespecFull(Entity *e)
{
	if(e->pChar)
	{
		character_RespecFull(entGetPartitionIdx(e), e->pChar);
	}
}

void character_RespecAdvantages(SA_PARAM_NN_VALID Character *pchar)
{
	RespecAdvantages(pchar->pEntParent);
}

AUTO_TRANSACTION
	ATR_LOCKS(e, ".Pchar.Ppsecondarypaths, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .pInventoryV2.ppInventoryBags[], .Pchar.Uilastfreerespectime, .Pchar.Uilastforcedrespectime, .Pplayer.Icreatedtime, .Pchar.Pppowertrees, .Pchar.Uipowertreemodcount, .Pplayer.Pugckillcreditlimit, .Psaved.Ppallowedcritterpets, .Pchar.Pppointspentpowertrees, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome trEntity_UseFreeRespec(	ATR_ARGS, NOCONST(Entity) *e, 
												PowerTreeSteps *pStepsRespec, 
												PowerTreeSteps *pStepsBuy, 
												const ItemChangeReason *pReason,
												S32 bAsFreeRespec)
{
	enumTransactionOutcome eOutcome = TRANSACTION_OUTCOME_SUCCESS;

	if( ISNULL(e) ||
		ISNULL(e->pPlayer) ||
		ISNULL(e->pChar))
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED due to null player, character, or entity");
	}

	if (bAsFreeRespec)
	{
		if(timeServerSecondsSince2000() < trhEntity_GetFreeRespecTime(e))
		{
			TRANSACTION_RETURN_LOG_FAILURE("Could not use free respec, player not allowed.");
		}
	}
	else
	{
		if(timeServerSecondsSince2000() < trhEntity_GetForcedRespecTime(e))
		{
			TRANSACTION_RETURN_LOG_FAILURE("Could not use forced respec, player not allowed.");
		}
	}
	

	//Do the respec
	if(trEntity_PowerTreeStepsRespec(ATR_PASS_ARGS,e,NULL,pStepsRespec,pStepsBuy,pReason, kPTRespecGroup_ALL) != TRANSACTION_OUTCOME_SUCCESS)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Free Respec failure")
	}

	//Doing a full respec also removes secondary paths. This only affects NW currently.
	character_trh_RemoveAllSecondaryCharacterPaths(ATR_PASS_ARGS, e, false);

	if (bAsFreeRespec)
	{
		e->pChar->uiLastFreeRespecTime = timeServerSecondsSince2000();
		TRANSACTION_RETURN_LOG_SUCCESS("Free Respec successful");
	}
	else
	{
		e->pChar->uiLastForcedRespecTime = timeServerSecondsSince2000();
		TRANSACTION_RETURN_LOG_SUCCESS("Forced Respec successful");
	}

}


AUTO_TRANSACTION
ATR_LOCKS(e, ".Pchar.Ppsecondarypaths, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .pInventoryV2.ppInventoryBags[], .Pchar.Uipowertreemodcount, .Pchar.Uilastfreerespectime, .Pchar.Uilastforcedrespectime, .Pplayer.Icreatedtime, .Pchar.Pppowertrees, .Pplayer.Pugckillcreditlimit, .Psaved.Ppallowedcritterpets, .Pchar.Pppointspentpowertrees, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, pInventoryV2.ppLiteBags[], .Pplayer.Accountid")
ATR_LOCKS(pData, ".Iversion, .Eakeys[]")
ATR_LOCKS(pLockContainer, ".Plock.Uaccountid, .Plock.Pkey, .Plock.Result, .Plock.Fdestroytime, .Plock.Etransactiontype");
enumTransactionOutcome trEntity_UseGameAccountRespec(ATR_ARGS, NOCONST(Entity) *e, NOCONST(GameAccountData) *pData, NOCONST(AccountProxyLockContainer) *pLockContainer, const char *pKey, PowerTreeSteps *pStepsRespec, PowerTreeSteps *pStepsBuy, const ItemChangeReason *pReason)
{
	enumTransactionOutcome eOutcome = TRANSACTION_OUTCOME_SUCCESS;

	if( ISNULL(e) ||
		ISNULL(e->pPlayer) ||
		ISNULL(e->pChar) ||
		ISNULL(pData))
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED due to null player, character, entity or game account");
	}

	// If GAD modification disallowed, this game must use AS key-values for respecs, so expect the lock to be there, then consume it
	if(gConf.bDontAllowGADModification)
	{
		if(ISNULL(pLockContainer) || !APFinalizeKeyValue(pLockContainer, e->pPlayer->accountID, pKey, APRESULT_COMMIT, TransLogType_Other))
		{
			TRANSACTION_RETURN_LOG_FAILURE("FAILED to pay account key-value respec token %s", pKey);
		}
	}
	else
	{
		// Otherwise we're using GAD like normal
		if(!slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pData, pKey, -1, 0, 100000))
		{
			TRANSACTION_RETURN_LOG_FAILURE("FAILED to pay game account respec token %s", pKey);
		}
	}

	//Do the respec
	if(trEntity_PowerTreeStepsRespec(ATR_PASS_ARGS,e,NULL,pStepsRespec,pStepsBuy,pReason,kPTRespecGroup_ALL) != TRANSACTION_OUTCOME_SUCCESS)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Game Account Respec Failure");
	}

	//Doing a full respec also removes secondary paths. This only affects NW currently.
	character_trh_RemoveAllSecondaryCharacterPaths(ATR_PASS_ARGS, e, false);

	pData->iVersion++;

	TRANSACTION_RETURN_LOG_SUCCESS("Game Account Respec successful");
}

AUTO_TRANSACTION
ATR_LOCKS(e, ".Pchar.Ppsecondarypaths, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Pinventoryv2.Ppinventorybags[], .Hallegiance, .Hsuballegiance, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pchar.Pppointspentpowertrees, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Pchar.Uipowertreemodcount, .Pchar.Uilastfreerespectime, .Pchar.Uilastforcedrespectime, .Pplayer.Icreatedtime, .Pchar.Pppowertrees, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome trEntity_UseRespecItem(ATR_ARGS, NOCONST(Entity) *e, PowerTreeSteps *pStepsRespec, PowerTreeSteps *pStepsBuy, const ItemChangeReason *pReason, int eRespecType)
{
	enumTransactionOutcome eOutcome = TRANSACTION_OUTCOME_SUCCESS;

	// Variables for doing the numeric item/bag lookup.
	const char *pchCostNumeric = PowerTree_GetRespecTokensPurchaseItem(eRespecType);
	ItemDef *pItemDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict,pchCostNumeric);

	if( ISNULL(e) ||
		ISNULL(e->pPlayer) ||
		ISNULL(e->pChar))
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED due to null player, character, or entity");
	}

	if(!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, e, false, pchCostNumeric, -1, pReason))
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED to pay respec token %s", pchCostNumeric);
	}

	//Do the respec
	if(trEntity_PowerTreeStepsRespec(ATR_PASS_ARGS,e,NULL,pStepsRespec,pStepsBuy,pReason, eRespecType) != TRANSACTION_OUTCOME_SUCCESS)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Item Respec Failure");
	}

	//Doing a full respec also removes secondary paths. This only affects NW currently.
	if (eRespecType == kPTRespecGroup_ALL)
		character_trh_RemoveAllSecondaryCharacterPaths(ATR_PASS_ARGS, e, false);

	TRANSACTION_RETURN_LOG_SUCCESS("Item Respec successful");
}

static void character_UseRespecHelper(Character *pChar, bool bAsFreeRespec)
{
	int iPartitionIdx = entGetPartitionIdx(pChar->pEntParent);
	PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);
	character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, pChar), pSteps, false, kPTRespecGroup_ALL);
	character_PowerTreeStepsCostRespec(iPartitionIdx, pChar, pSteps, 0);

	if(!eaSize(&pSteps->ppSteps))
	{
		char *pchMesg = NULL;
		entFormatGameMessageKey(pChar->pEntParent, &pchMesg, "CharacterRespec_Failed_NoPowers", STRFMT_END);
		notify_NotifySend(pChar->pEntParent, kNotifyType_RespecFailed, pchMesg, NULL, NULL);
		estrDestroy(&pchMesg);
	}
	else
	{
		S32 iTotalCost = GetPowerTreeSteps_TotalCost(pSteps);

		//If this has a cost associated with it...
		// If we have any secondary paths, a full respec is valid and will remove them.
		if(iTotalCost > 0 || eaSize(&pChar->ppSecondaryPaths) > 0)
		{
			RespecCBData* pData = Respec_CreateCBData(pChar->pEntParent, !bAsFreeRespec);

			TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnValEnt((bAsFreeRespec ? "FreeRespec" : "ForceRespec"), 
																						 pChar->pEntParent, Respec_CB, pData);
			PowerTreeSteps *pStepsRespec = StructCreate(parse_PowerTreeSteps);
			PowerTreeSteps *pStepsBuy = StructCreate(parse_PowerTreeSteps);
			ItemChangeReason reason = {0};

			CharacterGetFreeRespecSteps(iPartitionIdx, pChar, pStepsRespec, pStepsBuy, kPTRespecGroup_ALL);
			
			inv_FillItemChangeReason(&reason, pChar->pEntParent, "Powers:Respec", 
									(bAsFreeRespec ? "UseFreeRespec" : "UseForcedRespec"));

			AutoTrans_trEntity_UseFreeRespec(pReturn, GLOBALTYPE_GAMESERVER, 
					GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pChar->pEntParent), 
					pStepsRespec, pStepsBuy, &reason, bAsFreeRespec);
			
			StructDestroy(parse_PowerTreeSteps, pStepsRespec);
			StructDestroy(parse_PowerTreeSteps, pStepsBuy);
		}
		else
		{
			char *pchMesg = NULL;
			entFormatGameMessageKey(pChar->pEntParent, &pchMesg, "CharacterRespec_Failed_NoCost", STRFMT_END);
			notify_NotifySend(pChar->pEntParent, kNotifyType_RespecFailed, pchMesg, NULL, NULL);
			estrDestroy(&pchMesg);
		}
	}
	StructDestroy(parse_PowerTreeSteps, pSteps);
}


static bool character_HasRespecContact(Entity *pEnt)
{
	if (gConf.bAllowRespecAwayFromContact)
		return true;

	return contact_IsNearRespec(pEnt);
}

// Respecs your PowerTrees and StatPoints to nothing
AUTO_COMMAND ACMD_NAME("Character_UseFreeRespec") ACMD_CATEGORY(Powers) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void UseFreeRespec(Entity *e)
{
	if(e && 
		e->pChar && 
		entIsPlayer(e) && 
		timeServerSecondsSince2000() >= trhEntity_GetFreeRespecTime(CONTAINER_NOCONST(Entity, e)) && 
		character_HasRespecContact(e))
	{
		character_UseRespecHelper(e->pChar, true);
	}
}

// initiates a forced respec if the character has one to do
void character_CheckAndPerformForceRespec(Character *pChar)
{
	if (pChar && entIsPlayer(pChar->pEntParent) && 
		timeServerSecondsSince2000() >= trhEntity_GetForcedRespecTime(CONTAINER_NOCONST(Entity, pChar->pEntParent)))
	{
		character_UseRespecHelper(pChar, false);
	}
}

typedef struct GameAccountRespecInfo
{
	EntityRef *pRef;
	PowerTreeSteps *pStepsRespec;
	PowerTreeSteps *pStepsBuy;
	ItemChangeReason reason;
} GameAccountRespecInfo;

static void UseGameAccountRespec_CB(AccountKeyValueResult result, U32 accountID, SA_PARAM_NN_STR const char *key, ContainerID containerID, SA_PARAM_NN_VALID GameAccountRespecInfo *pRespecInfo)
{
	Entity *pEnt = entFromEntityRefAnyPartition(*pRespecInfo->pRef);

	if (pEnt && result == AKV_SUCCESS)
	{
		RespecCBData* pData = Respec_CreateCBData(pEnt, false);
		TransactionReturnVal *pReturnVal = LoggedTransactions_CreateManagedReturnValEnt("GameAccountRespec", pEnt, Respec_CB, pData);
		AutoTrans_trEntity_UseGameAccountRespec(pReturnVal, GLOBALTYPE_GAMESERVER,
			GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt),
			GLOBALTYPE_GAMEACCOUNTDATA, accountID,
			GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID,
			key, pRespecInfo->pStepsRespec, pRespecInfo->pStepsBuy, &pRespecInfo->reason);
	}
	else
	{
		if (pEnt)
		{
			char *pchMesg = NULL;
			entFormatGameMessageKey(pEnt, &pchMesg, "CharacterRespec_Failed", STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_RespecFailed, pchMesg, NULL, NULL);
			estrDestroy(&pchMesg);
		}

	}
	
	free(pRespecInfo->pRef);

	StructDestroy(parse_PowerTreeSteps, pRespecInfo->pStepsRespec);
	StructDestroy(parse_PowerTreeSteps, pRespecInfo->pStepsBuy);
	freeStruct(pRespecInfo);
}

// Respecs your PowerTrees and StatPoints to nothing using a game account key
AUTO_COMMAND ACMD_NAME("Character_UseGameAccountRespec") ACMD_CATEGORY(Powers) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void UseGameAccountRespec(Entity *e)
{
	if(e && 
		e->pChar && 
		e->pPlayer &&
		e->pPlayer->accountID &&
		entIsPlayer(e) && 
		((!gConf.bDontAllowGADModification && gad_GetAttribInt(GET_REF(e->pPlayer->pPlayerAccountData->hData), MicroTrans_GetRespecTokensGADKey())) ||
		 (gConf.bDontAllowGADModification && gad_GetAccountValueInt(GET_REF(e->pPlayer->pPlayerAccountData->hData), MicroTrans_GetRespecTokensASKey()))) && 
		character_HasRespecContact(e))
	{
		PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);
		character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, e->pChar), pSteps, false, kPTRespecGroup_ALL);
		character_PowerTreeStepsCostRespec(entGetPartitionIdx(e), e->pChar, pSteps, 0);

		if(!eaSize(&pSteps->ppSteps))
		{
			char *pchMesg = NULL;
			entFormatGameMessageKey(e, &pchMesg, "CharacterRespec_Failed_NoPowers", STRFMT_END);
			notify_NotifySend(e, kNotifyType_RespecFailed, pchMesg, NULL, NULL);
			estrDestroy(&pchMesg);
		}
		else
		{
			S32 iTotalCost = GetPowerTreeSteps_TotalCost(pSteps);

			//If this has a cost associated with it...
			if(iTotalCost > 0)
			{
				GameAccountRespecInfo *pRespecInfo = callocStruct(GameAccountRespecInfo);
				pRespecInfo->pRef = calloc(1, sizeof(EntityRef));
				pRespecInfo->pStepsRespec = StructCreate(parse_PowerTreeSteps);
				pRespecInfo->pStepsBuy = StructCreate(parse_PowerTreeSteps);

				CharacterGetFreeRespecSteps(entGetPartitionIdx(e), e->pChar, pRespecInfo->pStepsRespec, pRespecInfo->pStepsBuy, kPTRespecGroup_ALL);
				*pRespecInfo->pRef = entGetRef(e);

				inv_FillItemChangeReason(&pRespecInfo->reason, e, "Powers:Respec", "UseGameAccountRespec");

				// If GAD modification is disallowed, we have to lock the AS key-value first
				if (gConf.bDontAllowGADModification)
				{
					APChangeKeyValue(e->pPlayer->accountID, MicroTrans_GetRespecTokensASKey(), -1, UseGameAccountRespec_CB, pRespecInfo);
				}
				else
				{
					UseGameAccountRespec_CB(AKV_SUCCESS, e->pPlayer->accountID, MicroTrans_GetRespecTokensGADKey(), 0, pRespecInfo);
				}
			}
			else
			{
				char *pchMesg = NULL;
				entFormatGameMessageKey(e, &pchMesg, "CharacterRespec_Failed_NoCost", STRFMT_END);
				notify_NotifySend(e, kNotifyType_RespecFailed, pchMesg, NULL, NULL);
				estrDestroy(&pchMesg);
			}
		}
		StructDestroy(parse_PowerTreeSteps, pSteps);
	}
}

// Respecs your PowerTrees and StatPoints to nothing using a token item
AUTO_COMMAND ACMD_NAME("Character_UseItemRespecEx") ACMD_CATEGORY(Powers) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void UseItemRespecEx(Entity *e, PTRespecGroupType eRespecType)
{
	if(e && 
		e->pChar && 
		entIsPlayer(e) && 
		inv_GetNumericItemValue(e, PowerTree_GetRespecTokensPurchaseItem(eRespecType)) > 0 &&
		character_HasRespecContact(e))
	{
		PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);

		character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, e->pChar),pSteps, false, eRespecType);
		character_PowerTreeStepsCostRespec(entGetPartitionIdx(e), e->pChar, pSteps, 0);

		if(!eaSize(&pSteps->ppSteps))
		{
			char *pchMesg = NULL;
			entFormatGameMessageKey(e, &pchMesg, "CharacterRespec_Failed_NoPowers", STRFMT_END);
			notify_NotifySend(e, kNotifyType_RespecFailed, pchMesg, NULL, NULL);
			estrDestroy(&pchMesg);
		}
		else
		{
			S32 iTotalCost = GetPowerTreeSteps_TotalCost(pSteps);

			//If this has a cost associated with it...
			// If we have any secondary paths, a full respec is valid and will remove them.
			if(iTotalCost > 0 || eaSize(&e->pChar->ppSecondaryPaths) > 0)
			{
				RespecCBData* pData = Respec_CreateCBData(e, false);
				TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemRespec", e, Respec_CB, pData);
				PowerTreeSteps *pStepsRespec = StructCreate(parse_PowerTreeSteps);
				PowerTreeSteps *pStepsBuy = StructCreate(parse_PowerTreeSteps);
				ItemChangeReason reason = {0};

				CharacterGetFreeRespecSteps(entGetPartitionIdx(e),e->pChar,pStepsRespec,pStepsBuy, eRespecType);

				inv_FillItemChangeReason(&reason, e, "Powers:Respec", "UseRespecItem");

				AutoTrans_trEntity_UseRespecItem(pReturn, GLOBALTYPE_GAMESERVER, 
						GLOBALTYPE_ENTITYPLAYER, entGetContainerID(e), 
						pStepsRespec, pStepsBuy, &reason, eRespecType);

				StructDestroy(parse_PowerTreeSteps, pStepsRespec);
				StructDestroy(parse_PowerTreeSteps, pStepsBuy);
			}
			else
			{
				char *pchMesg = NULL;
				entFormatGameMessageKey(e, &pchMesg, "CharacterRespec_Failed_NoCost", STRFMT_END);
				notify_NotifySend(e, kNotifyType_RespecFailed, pchMesg, NULL, NULL);
				estrDestroy(&pchMesg);
			}
		}

		StructDestroy(parse_PowerTreeSteps, pSteps);
	}
}

// Respecs your PowerTrees and StatPoints to nothing using a token item
AUTO_COMMAND ACMD_NAME("Character_UseItemRespec") ACMD_CATEGORY(Powers) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void UseItemRespec(Entity *e)
{
	UseItemRespecEx(e, kPTRespecGroup_ALL);
}

AUTO_TRANSACTION
	ATR_LOCKS(e, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Hallegiance, .Hsuballegiance, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pchar.Pppointspentpowertrees, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Pchar.Uipowertreemodcount, .Pchar.Uilastfreerespectime, .Pchar.Uilastforcedrespectime, .Pplayer.Icreatedtime, .Pchar.Pppowertrees, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome trEntity_UseRespecNumeric(ATR_ARGS, NOCONST(Entity) *e, PowerTreeSteps *pStepsRespec, PowerTreeSteps *pStepsBuy, const ItemChangeReason *pReason, int eRespecType)
{
	enumTransactionOutcome eOutcome = TRANSACTION_OUTCOME_SUCCESS;
	S32 iCost;

	// Variables for doing the numeric item/bag lookup.
	ItemDef *pItemDef = GET_REF(g_PowerTreeRespecConfig.hNumeric);

	if( ISNULL(e) ||
		ISNULL(e->pPlayer) ||
		ISNULL(e->pChar))
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED due to null player, character, or entity");
	}

	if(!pItemDef)
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED due to invalid numeric.");
	}

	iCost = PowerTree_GetNumericRespecCost(e, eRespecType);

	if(!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, e, false, pItemDef->pchName, -iCost, pReason))
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED to pay respec cost %s of %d", pItemDef->pchName, iCost);
	}

	//Do the respec
	if(trEntity_PowerTreeStepsRespec(ATR_PASS_ARGS,e,NULL,pStepsRespec,pStepsBuy,pReason, eRespecType) != TRANSACTION_OUTCOME_SUCCESS)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Item Respec Failure");
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Item Respec successful");
}

// Respecs your PowerTrees and StatPoints to nothing using a token item
AUTO_COMMAND ACMD_NAME("Character_UseNumericRespec") ACMD_CATEGORY(Powers) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void UseNumericRespec(Entity *e, PTRespecGroupType eRespecType)
{
	if(e && 
		e->pChar && 
		entIsPlayer(e) && 
		PowerTree_CanRespecGroupWithNumeric(eRespecType) &&
		character_HasRespecContact(e))
	{
		PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);

		character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, e->pChar),pSteps, false, eRespecType);
		character_PowerTreeStepsCostRespec(entGetPartitionIdx(e), e->pChar, pSteps, 0);

		if(!eaSize(&pSteps->ppSteps))
		{
			char *pchMesg = NULL;
			entFormatGameMessageKey(e, &pchMesg, "CharacterRespec_Failed_NoPowers", STRFMT_END);
			notify_NotifySend(e, kNotifyType_RespecFailed, pchMesg, NULL, NULL);
			estrDestroy(&pchMesg);
		}
		else
		{
			S32 iTotalCost = PowerTree_GetNumericRespecCost(CONTAINER_NOCONST(Entity,e), eRespecType);

			//If this has a cost associated with it...
			if(iTotalCost > 0)
			{
				RespecCBData* pData = Respec_CreateCBData(e, false);
				TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnValEnt("NumericRespec", e, Respec_CB, pData);
				PowerTreeSteps *pStepsRespec = StructCreate(parse_PowerTreeSteps);
				PowerTreeSteps *pStepsBuy = StructCreate(parse_PowerTreeSteps);
				ItemChangeReason reason = {0};

				CharacterGetFreeRespecSteps(entGetPartitionIdx(e),e->pChar,pStepsRespec,pStepsBuy, eRespecType);
				pStepsRespec->bUpdatePointsSpent = true;
				pStepsRespec->bOnlyLowerPoints = true;

				inv_FillItemChangeReason(&reason, e, "Powers:Respec", "UseNumeric");

				AutoTrans_trEntity_UseRespecNumeric(pReturn, GLOBALTYPE_GAMESERVER, 
					GLOBALTYPE_ENTITYPLAYER, entGetContainerID(e), 
					pStepsRespec, pStepsBuy, &reason, eRespecType);

				StructDestroy(parse_PowerTreeSteps, pStepsRespec);
				StructDestroy(parse_PowerTreeSteps, pStepsBuy);
			}
			else
			{
				char *pchMesg = NULL;
				entFormatGameMessageKey(e, &pchMesg, "CharacterRespec_Failed_NoCost", STRFMT_END);
				notify_NotifySend(e, kNotifyType_RespecFailed, pchMesg, NULL, NULL);
				estrDestroy(&pchMesg);
			}
		}

		StructDestroy(parse_PowerTreeSteps, pSteps);
	}
}

AUTO_TRANSACTION ATR_LOCKS(e, ".Pchar.hClass, .Pchar.hPath, .pchar.Pppowertrees, .pchar.Pppointspentpowertrees");
enumTransactionOutcome trCharacter_AssignArchetype(ATR_ARGS, NOCONST(Entity)* e, const char* pchArchetype)
{
	CharacterPath* pPath;
	int i;
	PowerTreeSteps* pSteps = StructCreate(parse_PowerTreeSteps);
	if (!RefSystem_IsReferentStringValid(g_hCharacterPathDict, pchArchetype))
		TRANSACTION_RETURN_LOG_FAILURE("Failed to assign invalid archetype %s.", pchArchetype);

	//Set the primary character path.
	SET_HANDLE_FROM_STRING(g_hCharacterPathDict, pchArchetype, e->pChar->hPath);
	pPath = GET_REF(e->pChar->hPath);
	if (eaSize(&pPath->eaRequiredClasses) > 0)
		COPY_HANDLE(e->pChar->hClass, pPath->eaRequiredClasses[0]->hClass);
	for (i = eaSize(&e->pChar->ppPowerTrees)-1; i >= 0; i--)
	{
		PowerTreeDef* pDef = GET_REF(e->pChar->ppPowerTrees[i]->hDef);
		if ((IS_HANDLE_ACTIVE(pDef->hClass) && !REF_COMPARE_HANDLES(pDef->hClass, e->pChar->hClass)) || pDef->bAutoBuy)
		{
			entity_PowerTreeRemoveHelper(e, pDef);
		}
		else
		{
			//if we're not unbuying the tree, wipe all the nodes
			eaDestroyStructNoConst(&e->pChar->ppPowerTrees[i]->ppNodes,parse_PTNode);
		}
	}
	character_UpdatePointsSpentPowerTrees(e->pChar, false);

	entity_PowerTreeAddHelper(e, GET_REF(pPath->hPowerTree));
	StructDestroy(parse_PowerTreeSteps,pSteps);
	TRANSACTION_RETURN_LOG_SUCCESS("Set entity's archetype to %s.", pchArchetype);
}

void PlayerRespecAsArchetypeAutobuyCallback(TransactionReturnVal* returnVal, EntityRef *pEntRef)
{
	Entity* pEnt = entFromEntityRefAnyPartition(*pEntRef);

	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		character_DirtyInnatePowers(pEnt->pChar);
		character_DirtyPowerStats(pEnt->pChar);
		character_AccrueMods(entGetPartitionIdx(pEnt),pEnt->pChar,0.0f,pExtract);
		character_DirtyInnateAccrual(pEnt->pChar);
		character_PowerSlotsValidate(pEnt->pChar);
		character_DirtyAttribs(pEnt->pChar);
		character_UpdateMovement(pEnt->pChar, NULL);
		character_DirtyPowerTrees(pEnt->pChar);
		eaDestroy(&pEnt->pSaved->pTray->ppTrayElems);
		pEnt->pSaved->pTray->ppTrayElems = NULL;
		BuyCharacterPowersUsingPath(pEnt);
		entity_SetDirtyBit(pEnt, parse_Character, pEnt->pChar, 1);
		entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, 1);

		eventsend_RecordPowerTreeStepsAdded(pEnt);
		
		CharacterRespecPowerTrees(entGetPartitionIdx(pEnt), pEnt->pChar);
	}
	free(pEntRef);
}
void PlayerRespecAsArchetypeCallback(TransactionReturnVal* returnVal, EntityRef *pEntRef)
{
	Entity* pEnt = entFromEntityRefAnyPartition(*pEntRef);

	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		PowerTreeSteps* pSteps = StructCreate(parse_PowerTreeSteps);;
		if(entity_PowerTreeAutoBuySteps(entGetPartitionIdx(pEnt), pEnt, pEnt, pSteps))
		{
			ItemChangeReason reason = {0};
						
			inv_FillItemChangeReason(&reason, pEnt, "Powers:Respec", "RespecAsArchetype");

			AutoTrans_trEntity_PowerTreeStepsAdd(
				LoggedTransactions_CreateManagedReturnValEnt("AssignArchetypeAutobuyStep", pEnt, PlayerRespecAsArchetypeAutobuyCallback, pEntRef), GetAppGlobalType(),
				entGetType(pEnt), entGetContainerID(pEnt),
				0, 0,
				pSteps, false, &reason);
		}
		StructDestroy(parse_PowerTreeSteps,pSteps);
	}
}

void CharacterPathAddCB(TransactionReturnVal* returnVal, EntityRef *pEntRef)
{
	if (pEntRef && returnVal && returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity* pEnt = entFromEntityRefAnyPartition(*pEntRef);
		S32 iPartitionIdx = entGetPartitionIdx(pEnt);
		entity_PowerTreeAutoBuy(iPartitionIdx,pEnt,pEnt);
	}
}

AUTO_COMMAND ACMD_NAME("Character_ChooseSecondaryCharacterPath") ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void Character_ChooseSecondaryCharacterPath(Entity *pEnt, const char* pchPathName)
{
	if (pEnt && pchPathName)
	{
		EntityRef *pEntRef = malloc(sizeof(EntityRef));
		*pEntRef = entGetRef(pEnt);

		AutoTrans_character_tr_AddSecondaryCharacterPath(LoggedTransactions_CreateManagedReturnValEnt("character_tr_AddSecondaryCharacterPath", pEnt, CharacterPathAddCB, pEntRef),  
			GetAppGlobalType(), 
			pEnt->myEntityType, 
			pEnt->myContainerID, 
			pchPathName);
	}
}

AUTO_TRANSACTION ATR_LOCKS(e, ".Pchar.Ilevelexp, .Pchar.hClass, .Pchar.ppSecondaryPaths, .pchar.Pppowertrees");
enumTransactionOutcome character_tr_AddSecondaryCharacterPath(ATR_ARGS, NOCONST(Entity)* e, const char* pchPathName)
{
	NOCONST(AdditionalCharacterPath)* pAddedPath = StructCreateNoConst(parse_AdditionalCharacterPath);
	CharacterPath* pPathToAdd = RefSystem_ReferentFromString(g_hCharacterPathDict, pchPathName);

	if (!pPathToAdd)
		TRANSACTION_RETURN_LOG_FAILURE("Failed to assign invalid character path %s.", pchPathName);
	
	if (character_trh_CanPickSecondaryPath(e->pChar, pPathToAdd, true))
	{

		SET_HANDLE_FROM_STRING(g_hCharacterPathDict, pchPathName, pAddedPath->hPath);
		eaPush(&e->pChar->ppSecondaryPaths, pAddedPath);

		entity_PowerTreeAddHelper(e, GET_REF(pPathToAdd->hPowerTree));

		TRANSACTION_RETURN_LOG_SUCCESS("Added secondary character path %s.", pchPathName);
	}

	TRANSACTION_RETURN_LOG_FAILURE("Failed to assign character path %s; character did not qualify.", pchPathName);
}

AUTO_TRANSACTION ATR_LOCKS(e, ".Pchar.ppSecondaryPaths, .pchar.Pppowertrees");
enumTransactionOutcome character_tr_RemoveSecondaryCharacterPath(ATR_ARGS, NOCONST(Entity)* e, const char* pchPathName)
{
	CharacterPath* pPathToRemove = RefSystem_ReferentFromString(g_hCharacterPathDict, pchPathName);
	int i;

	if (!pPathToRemove)
		TRANSACTION_RETURN_LOG_FAILURE("Failed to remove invalid character path %s.", pchPathName);

	for (i = eaSize(&e->pChar->ppSecondaryPaths)-1; i >= 0; i--)
	{
		if (pPathToRemove == GET_REF(e->pChar->ppSecondaryPaths[i]->hPath))
		{
			PowerTreeDef* pDef = GET_REF(pPathToRemove->hPowerTree);

			StructDestroyNoConst(parse_AdditionalCharacterPath, e->pChar->ppSecondaryPaths[i]);
			eaRemove(&e->pChar->ppSecondaryPaths, i);

			entity_PowerTreeRemoveHelper(e, pDef);

			break;
		}
	}


	TRANSACTION_RETURN_LOG_SUCCESS("Removed secondary character path %s.", pchPathName);
}

AUTO_COMMAND ACMD_NAME("Character_RemoveAllSecondaryCharacterPaths") ACMD_SERVERCMD ACMD_PRIVATE;
void Character_RemoveAllSecondaryCharacterPaths(Entity *pEnt, bool bRemoveTrees)
{
	if (pEnt)
	{
		AutoTrans_character_tr_RemoveAllSecondaryCharacterPaths(LoggedTransactions_CreateManagedReturnValEnt("Character_RemoveAllSecondaryCharacterPaths", pEnt, NULL, NULL),  
			GetAppGlobalType(), 
			pEnt->myEntityType, 
			pEnt->myContainerID,
			bRemoveTrees);
	}
}

AUTO_TRANS_HELPER;
enumTransactionOutcome character_trh_RemoveAllSecondaryCharacterPaths(ATR_ARGS, ATH_ARG NOCONST(Entity)* e, S32 bRemoveTrees)
{
	int i;

	for (i = eaSize(&e->pChar->ppSecondaryPaths)-1; i >= 0; i--)
	{
		CharacterPath* pPathDef = GET_REF(e->pChar->ppSecondaryPaths[i]->hPath);
		PowerTreeDef* pTreeDef = GET_REF(pPathDef->hPowerTree);

		StructDestroyNoConst(parse_AdditionalCharacterPath, e->pChar->ppSecondaryPaths[i]);
		eaRemove(&e->pChar->ppSecondaryPaths, i);

		if (bRemoveTrees)
			entity_PowerTreeRemoveHelper(e, pTreeDef);
	}


	TRANSACTION_RETURN_LOG_SUCCESS("Removed all secondary character paths.");
}
AUTO_TRANSACTION ATR_LOCKS(e, ".Pchar.ppSecondaryPaths, .pchar.Pppowertrees");
enumTransactionOutcome character_tr_RemoveAllSecondaryCharacterPaths(ATR_ARGS, NOCONST(Entity)* e, S32 bRemoveTrees)
{
	return character_trh_RemoveAllSecondaryCharacterPaths(ATR_PASS_ARGS, e, bRemoveTrees);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void PlayerRespecAsArchetype(Entity *pEnt, ACMD_NAMELIST("CharacterPath", REFDICTIONARY) const char* pchArchetype)
{
#ifdef GAMESERVER
	if (!RefSystem_IsReferentStringValid(g_hCharacterPathDict, pchArchetype))
	{
		Errorf("Tried to assign invalid archetype %s.", pchArchetype);
		return;
	}
	if(pEnt && pEnt->pChar)
	{
		EntityRef *pEntRef = malloc(sizeof(EntityRef));
		TransactionReturnVal* returnVal;
		*pEntRef = entGetRef(pEnt);

		returnVal = LoggedTransactions_CreateManagedReturnValEnt("AssignArchetype", pEnt, PlayerRespecAsArchetypeCallback, pEntRef);
		AutoTrans_trCharacter_AssignArchetype(returnVal, GLOBALTYPE_GAMESERVER, 
			GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), pchArchetype);
	
	}
#endif
}