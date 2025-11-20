/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "PowerTreeTransactions.h"

#include "Entity.h"
#include "AutoGen/Entity_h_ast.h"
#include "entCritter.h"
#include "EntityLib.h"
#include "earray.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "LoggedTransactions.h"
#include "logging.h"
#include "objTransactions.h"
#include "ReferenceSystem.h"
#include "StringFormat.h"
#include "TransactionSystem.h"
#include "inventoryCommon.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"

#include "aiAnimList.h"
#include "AnimList_Common.h"
#include "Character.h"
#include "Character_h_ast.h"
#include "gslLogSettings.h"
#include "gslSavedPet.h"
#include "NotifyCommon.h"
#include "Powers.h"
#include "Powers_h_ast.h"
#include "Player.h"
#include "PowerTree.h"
#include "PowerTree_h_ast.h"
#include "SavedPetCommon.h"
#include "StringCache.h"
#include "CharacterRespecServer.h"
#include "CharacterAttribs.h"
#include "gslEventSend.h"

#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

#include "PowerHelpers.h"
#include "PowerTreeHelpers.h"

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void powertree_PlayAnimListOnGrantPower(CmdContext * pContext, SA_PARAM_NN_STR const char * pchTree, SA_PARAM_NN_STR const char * pchNode)
{
	Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);

	if (pEnt)
	{
		PowerTreeDef *pTreeDef = powertreedef_Find(pchTree);
		PTNodeDef *pNodeDef = powertreenodedef_Find(pchNode);
		if (pTreeDef)
		{
			Message *pGrantMessage = GET_REF(pTreeDef->pGrantMessage.hMessage);
			AIAnimList *pAnimList = GET_REF(pTreeDef->hAnimListToPlayOnGrant);
			if (pAnimList)
			{
				// Play the anim list
				aiAnimListSetOneTick(pEnt, pAnimList);
			}

			if (pGrantMessage)
			{
				char *estrGrantMessage = NULL;
				FormatDisplayMessage(&estrGrantMessage, pTreeDef->pGrantMessage, STRFMT_STRING("Power", TranslateDisplayMessage(pNodeDef->pDisplayMessage)), STRFMT_END);
				ClientCmd_NotifySend(pEnt, 
					kNotifyType_PowerGranted, 
					estrGrantMessage, 
					pchNode, 
					NULL);
				estrDestroy(&estrGrantMessage);
			}
		}
	}
}




// Entry point for all PowerTree additions after Character creation
// NO other entry points for adding Trees, Nodes or Enhancements are legal
// Wraps entity_PowerTreeStepsAddHelper, which does not perform validation, so
//  all validation must be performed before calling this transaction.
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Pchar.Uipowertreemodcount, .Pchar.Pppowertrees, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pPayer, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome trEntity_PowerTreeStepsAdd(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Entity)* pPayer, PowerTreeSteps *pSteps, U32 bFullRespec, const ItemChangeReason *pReason)
{
	if(pSteps->uiPowerTreeModCount != pEnt->pChar->uiPowerTreeModCount)
	{
		TRANSACTION_RETURN_LOG_FAILURE("uiPowerTreeModCount mismatch. Expected %d, found %d", pSteps->uiPowerTreeModCount, pEnt->pChar->uiPowerTreeModCount);
	}

	pSteps->bTransaction = true;
	
	if(!entity_PowerTreeStepsAddHelper(ATR_PASS_ARGS,pPayer,pEnt,pSteps,pReason))
	{
		TRANSACTION_RETURN_LOG_FAILURE("General failure");
	}

	if (bFullRespec)
	{
		character_UpdateFullRespecVersion(pEnt);
	}
	else
	{
		character_UpdatePowerTreeVersion(pEnt);
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Success");
}

// Entry point for all PowerTree respecs
// NO other entry points for removing Trees, Nodes or Enhancements are legal
// Wraps entity_PowerTreeStepsRespecHelper and entity_PowerTreeStepsAddHelper,
//  which do not perform validation, so all validation must be performed before
//  calling this transaction.
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Pchar.Uipowertreemodcount, .Pchar.Uilastfreerespectime, .Pchar.Uilastforcedrespectime, .Pplayer.Icreatedtime, .Pchar.Pppowertrees, .Hallegiance, .Hsuballegiance, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pchar.Pppointspentpowertrees, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pData, ".Eakeys");
enumTransactionOutcome trEntity_PowerTreeStepsRespec(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(GameAccountData)* pData, PowerTreeSteps *pStepsRespec, PowerTreeSteps *pStepsBuy, const ItemChangeReason *pReason, int eRespecType)
{
	if(pStepsRespec->uiPowerTreeModCount != pEnt->pChar->uiPowerTreeModCount)
	{
		TRANSACTION_RETURN_LOG_FAILURE("uiPowerTreeModCount mismatch. Expected %d, found %d", pStepsRespec->uiPowerTreeModCount, pEnt->pChar->uiPowerTreeModCount);
	}

	pStepsRespec->bTransaction = true;
	if(!entity_PowerTreeStepsRespecHelper(ATR_PASS_ARGS,pEnt,pData,pStepsRespec,pReason, eRespecType))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Respec failure");
	}

	if(pStepsBuy)
	{
		pStepsBuy->bTransaction = true;
		if(!entity_PowerTreeStepsAddHelper(ATR_PASS_ARGS,NULL,pEnt,pStepsBuy,pReason))
		{
			TRANSACTION_RETURN_LOG_FAILURE("Buy failure");
		}
	}

	character_UpdatePowerTreeVersion(pEnt);

	TRANSACTION_RETURN_LOG_SUCCESS("Success");
}

static void PowerTreeStepsAdd_CB(TransactionReturnVal *pReturnVal, void *pData)
{
	EntityRef *pRef = (EntityRef*)pData;
	Entity *e = entFromEntityRefAnyPartition(*pRef);

	if(e && e->pChar)
	{
		if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			eventsend_RecordPowerTreeStepsAdded(e);
		}
	}

	SAFE_FREE(pRef);
}


// Adds all appropriate PowerTree AutoBuy Trees and Nodes to the Entity
void entity_PowerTreeAutoBuyEx(int iPartitionIdx, Entity *pEnt, Entity *pPayer, bool bFullRespec)
{
	if(pEnt && pEnt->pChar)
	{
		PowerTreeSteps *pSteps;

		PERFINFO_AUTO_START_FUNC();
		
		pSteps = StructCreate(parse_PowerTreeSteps);
		if(entity_PowerTreeAutoBuySteps(iPartitionIdx,pEnt,pPayer,pSteps))
		{
			EntityRef *pRef;
			TransactionReturnVal *pReturnVal;

			ItemChangeReason reason = {0};

			inv_FillItemChangeReason(&reason, pPayer ? pPayer : pEnt, "Powers:PowerTreeAutoBuy", pEnt->debugName);

			pRef = calloc(1, sizeof(EntityRef));
			pReturnVal = LoggedTransactions_CreateManagedReturnValEnt("PowerTreeStepsAdd", pEnt, PowerTreeStepsAdd_CB, pRef);
			*pRef = entGetRef(pEnt);

			AutoTrans_trEntity_PowerTreeStepsAdd(
				pReturnVal, GetAppGlobalType(),
				entGetType(pEnt), entGetContainerID(pEnt),
				pPayer ? entGetType(pPayer) : 0, pPayer ? entGetContainerID(pPayer) : 0,
				pSteps, bFullRespec, &reason);
		}
		StructDestroy(parse_PowerTreeSteps,pSteps);

		PERFINFO_AUTO_STOP();
	}
}

// Adds one rank of the Enhancement to the Entity's PowerTree Node
void entity_PowerTreeNodeEnhance(int iPartitionIdx, Entity *pEnt, const char *pchTree, const char *pchNode, const char *pchEnhancement)
{
	if(pEnt && pEnt->pChar)
	{
		PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);
		if(entity_PowerTreeNodeEnhanceSteps(iPartitionIdx, pEnt,pchTree,pchNode,pchEnhancement,pSteps))
		{
			EntityRef *pRef;
			TransactionReturnVal *pReturnVal;

			ItemChangeReason reason = {0};

			inv_FillItemChangeReason(&reason, pEnt, "Powers:PowerTreeNodeEnhance", pchNode);

			pRef = calloc(1, sizeof(EntityRef));
			pReturnVal = LoggedTransactions_CreateManagedReturnValEnt("PowerTreeStepsAdd", pEnt, PowerTreeStepsAdd_CB, pRef);
			*pRef = entGetRef(pEnt);

			AutoTrans_trEntity_PowerTreeStepsAdd(
				pReturnVal, GetAppGlobalType(),
				entGetType(pEnt), entGetContainerID(pEnt),
				0, 0,
				pSteps, false, &reason);
		}
		StructDestroy(parse_PowerTreeSteps,pSteps);
	}
}

// Adds one rank to the Entity's PowerTree Node
void entity_PowerTreeNodeInceaseRank(int iPartitionIdx, Entity *pEnt, const char *pchTree, const char *pchNode)
{
	if(pEnt && pEnt->pChar)
	{
		PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);
		if(entity_PowerTreeNodeIncreaseRankSteps(iPartitionIdx,pEnt,pchTree,pchNode,pSteps))
		{
			EntityRef *pRef;
			TransactionReturnVal *pReturnVal;

			ItemChangeReason reason = {0};

			inv_FillItemChangeReason(&reason, pEnt, "Powers:PowerTreeNodeIncreaseRank", pchNode);

			pRef = calloc(1, sizeof(EntityRef));
			pReturnVal = LoggedTransactions_CreateManagedReturnValEnt("PowerTreeStepsAdd", pEnt, PowerTreeStepsAdd_CB, pRef);
			*pRef = entGetRef(pEnt);

			AutoTrans_trEntity_PowerTreeStepsAdd(
				pReturnVal, GetAppGlobalType(),
				entGetType(pEnt), entGetContainerID(pEnt),
				0, 0,
				pSteps, false, &reason);
		}
		StructDestroy(parse_PowerTreeSteps,pSteps);
	}
}

// Adds the PowerTreeSteps to the Entity, optionally passes along the payer and return val
void entity_PowerTreeStepsBuy(int iPartitionIdx, Entity *pEnt, Entity *pPayer, PowerTreeSteps *pStepsRequested, TransactionReturnVal *pReturnVal)
{
	if(pEnt && pEnt->pChar && pStepsRequested)
	{
		PowerTreeSteps *pStepsResult = StructCreate(parse_PowerTreeSteps);
		if(entity_PowerTreeStepsBuySteps(iPartitionIdx,pEnt,pPayer,pStepsRequested,pStepsResult,false))
		{
			ItemChangeReason reason = {0};

			inv_FillItemChangeReason(&reason, pPayer ? pPayer : pEnt, "Powers:PowerTreeStepsBuy", pEnt->debugName);

			if (!pReturnVal)
			{
				EntityRef *pRef = calloc(1, sizeof(EntityRef));
				pReturnVal = LoggedTransactions_CreateManagedReturnValEnt("PowerTreeStepsAdd", pEnt, PowerTreeStepsAdd_CB, pRef);
				*pRef = entGetRef(pEnt);
			}

			AutoTrans_trEntity_PowerTreeStepsAdd(
				pReturnVal,	GetAppGlobalType(),
				entGetType(pEnt), entGetContainerID(pEnt),
				pPayer ? entGetType(pPayer) : 0, pPayer ? entGetContainerID(pPayer) : 0,
				pStepsResult, false, &reason);
		}
		StructDestroy(parse_PowerTreeSteps,pStepsResult);
	}
}

typedef struct RespecCBData
{
	EntityRef erRef;
	bool bForcedRespec;
} RespecCBData;

RespecCBData* Respec_CreateCBData(Entity *pEnt, bool bIsForcedRespec)
{
	RespecCBData *pData = calloc(1, sizeof(RespecCBData));
	pData->bForcedRespec = bIsForcedRespec;
	pData->erRef = entGetRef(pEnt);
	return pData;
}

void Respec_CB(TransactionReturnVal *pReturn, void *pData)
{
	RespecCBData *pRespecData = (RespecCBData*)pData;
	Entity *e = entFromEntityRefAnyPartition(pRespecData->erRef);

	if(e && e->pChar)
	{
		char *pchMesg = NULL;
		if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			int iPartitionIdx = entGetPartitionIdx(e);
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);

			entFormatGameMessageKey(e, &pchMesg, "CharacterRespec_Successful", STRFMT_END);
			notify_NotifySend(e, kNotifyType_RespecSuccess, pchMesg, 
								(pRespecData->bForcedRespec ? "ForcedRespec" : NULL), 
								NULL);

			// Dirty all the Character's innate data
			character_DirtyInnateEquip(e->pChar);
			character_DirtyInnatePowers(e->pChar);
			character_DirtyPowerStats(e->pChar);
			character_DirtyInnateAccrual(e->pChar);

			// Deactivate all Passives (ResetPowersArray will restart them)
			character_DeactivatePassives(iPartitionIdx, e->pChar);

			character_ResetPowersArray(iPartitionIdx, e->pChar, pExtract);

			// reset the free-respec flag
			e->pChar->iFreeRespecAvailable = trhEntity_GetFreeRespecTime(CONTAINER_NOCONST(Entity, e));
			entity_SetDirtyBit(e, parse_Character, e->pChar, false);
		}
		else
		{
			entFormatGameMessageKey(e, &pchMesg, "CharacterRespec_Failed", STRFMT_END);
			notify_NotifySend(e, kNotifyType_RespecFailed, pchMesg, NULL, NULL);
		}
		estrDestroy(&pchMesg);

		// reset the free-respec flag
		e->pChar->iFreeRespecAvailable = trhEntity_GetFreeRespecTime(CONTAINER_NOCONST(Entity, e));
		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	}

	SAFE_FREE(pData);
}

// Respecs the PowerTreeSteps from the Entity, optionally passes along the post-respec buys and return val
void entity_PowerTreeStepsRespec(int iPartitionIdx,
								 Entity *pEnt,
								 PowerTreeSteps *pStepsRequestedRespec,
								 PowerTreeSteps *pStepsRequestedBuy,
								 TransactionReturnVal *pReturnVal,
								 PTRespecGroupType eRespecType,
								 S32 bStepRespec)
{
	if(pEnt && pEnt->pChar && pStepsRequestedRespec)
	{
		PowerTreeSteps *pStepsResultRespec = StructCreate(parse_PowerTreeSteps);
		PowerTreeSteps *pStepsResultBuy = StructCreate(parse_PowerTreeSteps);
		if(entity_PowerTreeStepsRespecSteps(iPartitionIdx,pEnt,NULL,pStepsRequestedRespec,pStepsRequestedBuy,pStepsResultRespec,pStepsResultBuy,false,bStepRespec))
		{
			ItemChangeReason reason = {0};

			if (!pReturnVal)
			{
				RespecCBData* pData = Respec_CreateCBData(pEnt, false);
				pReturnVal = LoggedTransactions_CreateManagedReturnValEnt("PowerTreeRespec", pEnt, Respec_CB, pData);
			}

			inv_FillItemChangeReason(&reason, pEnt, "Powers:PowerTreeStepsRespec", NULL);

			AutoTrans_trEntity_PowerTreeStepsRespec(
				pReturnVal, GetAppGlobalType(),
				entGetType(pEnt), entGetContainerID(pEnt),
				GLOBALTYPE_GAMEACCOUNTDATA, entGetAccountID(pEnt),
				pStepsResultRespec, pStepsResultBuy, &reason, eRespecType);
		}
		StructDestroy(parse_PowerTreeSteps,pStepsResultRespec);
		StructDestroy(parse_PowerTreeSteps,pStepsResultBuy);
	}
}






AUTO_TRANSACTION ATR_LOCKS(e, ".Pchar.Pppowertrees, .Pchar.Pppointspentpowertrees");
enumTransactionOutcome trCharacter_UpdatePointsSpentPowerTrees(ATR_ARGS, NOCONST(Entity)* e)
{
	character_UpdatePointsSpentPowerTrees(e->pChar, false);
	character_LockAllPowerTrees(e->pChar);
	TRANSACTION_RETURN_LOG_SUCCESS("Updated PointsSpent for PowerTrees");
}

AUTO_TRANSACTION ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Pchar.Pppowertrees, .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome trCharacter_ResetPointsSpentPowerTree(ATR_ARGS, NOCONST(Entity)* pEnt, const char* pchPowerTreeName, ItemChangeReason* pReason)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pChar))
	{
		const char* pchTreeNamePooled = allocFindString(pchPowerTreeName);
		int i, j, k;
		for (i = eaSize(&pEnt->pChar->ppPowerTrees)-1; i >= 0; i--)
		{
			NOCONST(PowerTree)* pTree = pEnt->pChar->ppPowerTrees[i];
			if (pchTreeNamePooled == REF_STRING_FROM_HANDLE(pTree->hDef))
			{
				PowerTreeDef* pTreeDef = GET_REF(pTree->hDef);
				PTTypeDef *pTypeDef = SAFE_GET_REF(pTreeDef, hTreeType);
				if (pTypeDef
					&& pTypeDef->pchSpentPointsNumeric
					&& *pTypeDef->pchSpentPointsNumeric
					&& !pTypeDef->bSpentPointsNonDynamic)
				{
					for (j = eaSize(&pTree->ppNodes)-1; j >= 0; j--)
					{
						NOCONST(PTNode)* pNode = pTree->ppNodes[j];
						PTNodeDef* pNodeDef = GET_REF(pNode->hDef);
						if (pNodeDef)
						{
							for (k = 0; k <= pNode->iRank; k++)
							{
								int iCost = entity_PowerTreeNodeRankCostHelper(pEnt->pChar, pNodeDef, k);
								if (iCost > 0)
								{
									inv_ent_trh_AddNumeric(ATR_PASS_ARGS,pEnt,true,pTypeDef->pchSpentPointsNumeric,-iCost,pReason);
								}
							}
						}
					}
				}
				eaDestroyStructNoConst(&pEnt->pChar->ppPowerTrees[i]->ppNodes, parse_PTNode);
				return TRANSACTION_OUTCOME_SUCCESS;
			}
		}
	}
	return TRANSACTION_OUTCOME_FAILURE;
}


// Performs a respec/buy as necessary to the the ranks as requested, regardless of restrictions
void entity_PowerTreeAddWithoutRules(Entity *pEnt, const char *pchTree, int iRank, int iRankEnh)
{
	PowerTreeSteps *pStepsRespec = StructCreate(parse_PowerTreeSteps);
	PowerTreeSteps *pStepsBuy = StructCreate(parse_PowerTreeSteps);

	int i,j,k;
	PowerTreeStep *pStep;
	PowerTreeDef *pTreeDef = powertreedef_Find(pchTree);
	NOCONST(PowerTree) *pTree;
	ItemChangeReason reason = {0};

	// Make sure we've got the tree
	pTree = entity_FindPowerTreeHelper(CONTAINER_NOCONST(Entity, pEnt), pTreeDef);
	if(!pTree)
	{
		pStep = StructCreate(parse_PowerTreeStep);
		pStep->pchTree = pTreeDef->pchName;
		eaPush(&pStepsBuy->ppSteps,pStep);
	}

	// This is called with a 1-based rank, but Nodes use 0-based
	iRank--;

	// Iterate through each Node and create steps to set it to desired rank
	for(i=eaSize(&pTreeDef->ppGroups)-1; i>=0; i--)
	{
		PTGroupDef *pGroupDef = pTreeDef->ppGroups[i];
		for(j=eaSize(&pGroupDef->ppNodes)-1; j>=0; j--)
		{
			PTNodeDef *pNodeDef = pGroupDef->ppNodes[j];
			NOCONST(PTNode) *pNode;
			int iRankCur, iRankTarget;

			iRankTarget = MIN(iRank,eaSize(&pNodeDef->ppRanks)-1);

			//entity_PowerTreeNodeSetRankHelper(ATR_EMPTY_ARGS, NULL, pEnt, pchTree, pNodeDef->pchNameFull, iRank-1, false);

			pNode = character_FindPowerTreeNodeHelper(CONTAINER_NOCONST(Character, pEnt->pChar), NULL, pNodeDef->pchNameFull);
			iRankCur = pNode ? pNode->iRank : -1;

			if(iRankCur<iRankTarget)
			{
				for(; iRankCur<iRankTarget; iRankCur++)
				{
					pStep = StructCreate(parse_PowerTreeStep);
					pStep->pchTree = pTreeDef->pchName;
					pStep->pchNode = pNodeDef->pchNameFull;
					eaPush(&pStepsBuy->ppSteps,pStep);
				}
			}
			else if(iRankCur>iRankTarget)
			{
				for(; iRankCur>iRankTarget; iRankCur--)
				{
					pStep = StructCreate(parse_PowerTreeStep);
					pStep->pchTree = pTreeDef->pchName;
					pStep->pchNode = pNodeDef->pchNameFull;
					eaPush(&pStepsRespec->ppSteps,pStep);
				}
			}

			for(k=eaSize(&pNodeDef->ppEnhancements)-1; k>=0; k--)
			{
				PowerDef *pdefEnh = GET_REF(pNodeDef->ppEnhancements[k]->hPowerDef);

				if(!pdefEnh)
					continue;

				iRankTarget = MIN(iRankEnh,pNodeDef->ppEnhancements[k]->iLevelMax);

				iRankCur = pNode ? powertreenode_FindEnhancementRankHelper(pNode,pdefEnh) : 0;
				if(iRankCur<iRankTarget)
				{
					for(; iRankCur<iRankTarget; iRankCur++)
					{
						pStep = StructCreate(parse_PowerTreeStep);
						pStep->pchTree = pTreeDef->pchName;
						pStep->pchNode = pNodeDef->pchNameFull;
						pStep->pchEnhancement = pdefEnh->pchName;
						eaPush(&pStepsBuy->ppSteps,pStep);
					}
				}
				else if(iRankCur>iRankTarget)
				{
					for(; iRankCur>iRankTarget; iRankCur--)
					{
						pStep = StructCreate(parse_PowerTreeStep);
						pStep->pchTree = pTreeDef->pchName;
						pStep->pchNode = pNodeDef->pchNameFull;
						pStep->pchEnhancement = pdefEnh->pchName;
						eaPush(&pStepsRespec->ppSteps,pStep);
					}
				}
			}
		}
	}

	pStepsRespec->uiPowerTreeModCount = pEnt->pChar->uiPowerTreeModCount;

	inv_FillItemChangeReason(&reason, pEnt, "Powers:PowerTreeAddWithoutRules", NULL);

	AutoTrans_trEntity_PowerTreeStepsRespec(
		LoggedTransactions_MakeEntReturnVal(__FUNCTION__,pEnt),
		GetAppGlobalType(),
		entGetType(pEnt),
		entGetContainerID(pEnt),
		GLOBALTYPE_GAMEACCOUNTDATA,
		entGetAccountID(pEnt),
		pStepsRespec,
		pStepsBuy,
		&reason,
		kPTRespecGroup_ALL);

	if(!pEnt->pChar->bResetPowersArray)
	{
		pEnt->pChar->bResetPowersArray = true;
		// If the powers array isn't reset, the following powers lists need to be cleared
		//  because they cannot be trusted to have valid powers
		eaClear(&pEnt->pChar->ppPowers);
		eaClear(&pEnt->pChar->ppPowersLimitedUse);
		eaClear(&pEnt->pChar->modArray.ppPowers);
	}

	StructDestroy(parse_PowerTreeSteps,pStepsRespec);
	StructDestroy(parse_PowerTreeSteps,pStepsBuy);
}

// Transaction for the respec-like process of replacing one Node with another, with a special cost
AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Psaved.Ppuppetmaster.Curtempid, .Pchar.Uipowertreemodcount, .Pchar.Uilastfreerespectime, .Pchar.Uilastforcedrespectime, .Pplayer.Icreatedtime, .Pchar.Pppowertrees, .Pplayer.Pugckillcreditlimit, .Psaved.Ppallowedcritterpets, .Pchar.Pppointspentpowertrees, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pPayer, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome trEntity_PowerTreeNodeReplaceEscrow(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Entity)* pPayer,
														   PowerTreeSteps *pStepsRespec, PowerTreeSteps *pStepsBuy,
														   S32 iCost, const char* pchCostNumeric, const ItemChangeReason *pReason)
{
	S32 iOwnerCurrency = pchCostNumeric ? inv_trh_GetNumericValue(ATR_PASS_ARGS, pPayer, pchCostNumeric) : 0;
	if (iCost > 0 && (iOwnerCurrency < iCost || !inv_ent_trh_SetNumeric(ATR_EMPTY_ARGS, pPayer, false, pchCostNumeric, iOwnerCurrency - iCost, pReason)))
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED Unable to replace power in escrow for ent: %s, couldn't set cost numeric %s/%d.", pEnt->debugName, pchCostNumeric, iCost);
	}

	return trEntity_PowerTreeStepsRespec(ATR_PASS_ARGS,pEnt,NULL,pStepsRespec,pStepsBuy,pReason, kPTRespecGroup_ALL);
}

AUTO_TRANS_HELPER;
bool trhCharacter_CompleteTraining(ATR_ARGS, ATH_ARG NOCONST(Entity)* pBuyer, ATH_ARG NOCONST(Entity)* pEnt,
								   U64 uiRemoveItemID, PowerTreeSteps *pStepsRespec, PowerTreeSteps *pStepsBuy, 
								   const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	// Try to do the necessary powers operations
	if(!trEntity_PowerTreeStepsRespec(ATR_PASS_ARGS,pEnt,NULL,pStepsRespec,pStepsBuy,pReason, kPTRespecGroup_ALL))
	{
		TRANSACTION_APPEND_LOG_FAILURE("FAILED Unable to complete training: %s, power tree respec failed.", pEnt->debugName);
		return false;
	}
	// Attempt to remove the item providing training
	if (uiRemoveItemID)
	{
		bool bSuccess = true;
		NOCONST(Item)* pItem = NULL;
		BagIterator* pIter = inv_trh_FindItemByID(ATR_PASS_ARGS, pBuyer, uiRemoveItemID);
		if (pIter)
			pItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, bagiterator_GetCurrentBag(pIter), pIter->i_cur);
		if (NONNULL(pItem))
			pItem->flags &= ~kItemFlag_TrainingFromItem;
		if (ISNULL(pItem) || !invbag_RemoveItem(ATR_PASS_ARGS, pBuyer, false, bagiterator_GetCurrentBagID(pIter), pIter->i_cur, 1, pReason, pExtract))
		{
			TRANSACTION_APPEND_LOG_FAILURE("FAILED Unable to complete training: %s, couldn't remove item (ID %"FORM_LL"d).", pEnt->debugName, uiRemoveItemID);
			bSuccess = false;
		}
		bagiterator_Destroy(pIter);
		return bSuccess;
	}
	return true;
}

AUTO_TRANS_HELPER;
bool trhCharacter_StartTraining(ATR_ARGS, ATH_ARG NOCONST(Entity)* pBuyer, ATH_ARG NOCONST(Entity)* pEnt, 
								const char* pchCostNumeric, S32 iCost, U64 uiItemID,
								NON_CONTAINER CharacterTraining* pTraining, PropPowerSaveList* pSavePowerList,
								PowerTreeSteps *pStepsRespec, PowerTreeSteps *pStepsBuy,
								const ItemChangeReason *pReason,
								GameAccountDataExtract *pBuyerExtract)
{
	S32 i, iOwnerCurrency;
	NOCONST(CharacterTraining) *pNewTraining;
	NOCONST(Item)* pItem = NULL;

	if (pEnt->myContainerID == GLOBALTYPE_ENTITYSAVEDPET &&	
		eaUSize(&pEnt->pChar->ppTraining) >= g_PetRestrictions.uiMaxSimultaneousTraining)
	{
		TRANSACTION_APPEND_LOG_FAILURE("FAILED Unable to start training: %s, already training max number of nodes.",pEnt->debugName);
		return false;
	}
	for (i = eaSize(&pEnt->pChar->ppTraining)-1; i >= 0; i--)
	{
		if (REF_COMPARE_HANDLES(pTraining->hNewNodeDef, pEnt->pChar->ppTraining[i]->hNewNodeDef))
		{
			TRANSACTION_APPEND_LOG_FAILURE("FAILED Unable to start training: %s, already training node %s.",pEnt->debugName, REF_STRING_FROM_HANDLE(pTraining->hNewNodeDef));
			return false;
		}
	}

	iOwnerCurrency = inv_trh_GetNumericValue(ATR_PASS_ARGS, pBuyer, pchCostNumeric);
	if (iCost > 0 && (iOwnerCurrency < iCost || !inv_ent_trh_SetNumeric(ATR_PASS_ARGS, pBuyer, false, pchCostNumeric, iOwnerCurrency - iCost, pReason)))
	{
		TRANSACTION_APPEND_LOG_FAILURE("FAILED Unable to start training: %s, couldn't set cost numeric.", pEnt->debugName);
		return false;
	}

	if (uiItemID)
	{
		BagIterator* pIter = inv_trh_FindItemByID(ATR_PASS_ARGS, pBuyer, uiItemID);
		if (pIter)
			pItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, bagiterator_GetCurrentBag(pIter), pIter->i_cur);
		bagiterator_Destroy(pIter);
		if (!item_trh_CanRemoveItem(pItem))
		{
			TRANSACTION_APPEND_LOG_FAILURE("FAILED Unable to start training: %s, cannot remove item (ID %"FORM_LL"d).", pEnt->debugName, uiItemID);
			return false;
		}
	}
	if (pTraining->uiStartTime < pTraining->uiCompleteTime)
	{
		if (NONNULL(pItem) && pTraining->uiItemID)
		{
			//TODO(MK): Move to a hidden bag
			pItem->flags |= kItemFlag_TrainingFromItem;
		}
		pNewTraining = StructCloneDeConst(parse_CharacterTraining, pTraining);

		//keep the list sorted by completion time
		for (i = 0; i < eaSize(&pEnt->pChar->ppTraining); i++)
		{
			if (pTraining->uiCompleteTime < pEnt->pChar->ppTraining[i]->uiCompleteTime)
			{
				eaInsert(&pEnt->pChar->ppTraining,pNewTraining,i);
				break;
			}
		}
		if (i == eaSize(&pEnt->pChar->ppTraining))
		{
			eaPush(&pEnt->pChar->ppTraining,pNewTraining);
		}

		//Sets the pet as offline and turns off TEAMREQUEST and ALWAYSPROP (buyer is the owner)
		if (pEnt->myEntityType == GLOBALTYPE_ENTITYSAVEDPET)
		{
			if (trChangeSavedPetState(ATR_PASS_ARGS,pBuyer,pEnt,OWNEDSTATE_OFFLINE,0,0,-1,-1,pSavePowerList) == TRANSACTION_OUTCOME_FAILURE)
			{
				TRANSACTION_APPEND_LOG_FAILURE("FAILED %s couldn't set pet state", pEnt->debugName);
				return false;
			}
		}
	}
	else // If the training should complete immediately then do the complete operations now
	{
		if (!trhCharacter_CompleteTraining(ATR_PASS_ARGS,pBuyer,pEnt,pTraining->uiItemID,pStepsRespec,pStepsBuy,pReason,pBuyerExtract))
		{
			TRANSACTION_APPEND_LOG_FAILURE("FAILED %s couldn't complete training for %s", pEnt->debugName, REF_STRING_FROM_HANDLE(pTraining->hNewNodeDef));
			return false;
		}
	}
	TRANSACTION_APPEND_LOG_SUCCESS("SUCCESS %s started training %s", pEnt->debugName, REF_STRING_FROM_HANDLE(pTraining->hNewNodeDef));
	return true;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Pugckillcreditlimit, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .pInventoryV2.Ppinventorybags, .Psaved.Ppownedcontainers, .Psaved.Ppalwayspropslots, .Psaved.Ppuppetmaster.Pppuppets, .Pchar.Pppointspentpowertrees, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Pchar.Pptraining, .Psaved.Ipetid, .Pchar.Hclass, .Pchar.Pppowerspersonal, .Pchar.Pppowersclass, .Pchar.Pppowersspecies, .Pchar.Pppowertrees, .Pchar.Pppowerspersonal[AO], .Pchar.Pppowersclass[AO], .Pchar.Pppowersspecies[AO], .Pchar.Uipowertreemodcount, .Pchar.Uilastfreerespectime, .Pchar.Uilastforcedrespectime, .Pplayer.Icreatedtime, .Pchar.Uipoweridmax, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, pInventoryV2.ppLiteBags");
enumTransactionOutcome trCharacter_StartTraining(ATR_ARGS, NOCONST(Entity)* pEnt, 
												 const char* pchCostNumeric, S32 iCost, U64 uiItemID,
												 NON_CONTAINER CharacterTraining* pTraining, PropPowerSaveList* pSavePowerList,
												 PowerTreeSteps *pStepsRespec, PowerTreeSteps *pStepsBuy,
												 const ItemChangeReason *pReason, GameAccountDataExtract *pBuyerExtract)
{
	if (!trhCharacter_StartTraining(ATR_PASS_ARGS,pEnt,pEnt,pchCostNumeric,iCost,uiItemID,
									pTraining,pSavePowerList,pStepsRespec,pStepsBuy,pReason,pBuyerExtract))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(pBuyer, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Pugckillcreditlimit, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .pInventoryV2.Ppinventorybags, .Psaved.Ppownedcontainers, .Psaved.Ppalwayspropslots, .Psaved.Ppuppetmaster.Pppuppets, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, pInventoryV2.ppLiteBags")
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Psaved.Ppuppetmaster.Curtempid, .Pchar.Pptraining, .Psaved.Ppallowedcritterpets, .Pchar.Pppointspentpowertrees, .Psaved.Ipetid, .Pchar.Hclass, .Pchar.Pppowerspersonal, .Pchar.Pppowersclass, .Pchar.Pppowersspecies, .Pchar.Pppowertrees, .Pchar.Pppowerspersonal[AO], .Pchar.Pppowersclass[AO], .Pchar.Pppowersspecies[AO], .Pchar.Uipowertreemodcount, .Pchar.Uilastfreerespectime, .Pchar.Uilastforcedrespectime, .Pplayer.Icreatedtime, .Pplayer.Pugckillcreditlimit, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Uipoweridmax, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome trCharacter_StartTrainingForEnt(ATR_ARGS, NOCONST(Entity)* pBuyer, NOCONST(Entity)* pEnt, 
													  const char* pchCostNumeric, S32 iCost, U64 uiItemID,
													  NON_CONTAINER CharacterTraining* pTraining, PropPowerSaveList* pSavePowerList,
													  PowerTreeSteps *pStepsRespec, PowerTreeSteps *pStepsBuy,
													  const ItemChangeReason *pReason, GameAccountDataExtract *pBuyerExtract)
{
	if (!trhCharacter_StartTraining(ATR_PASS_ARGS,pBuyer,pEnt,pchCostNumeric,iCost,uiItemID,
									pTraining,pSavePowerList,pStepsRespec,pStepsBuy,pReason,pBuyerExtract))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pBuyer, ".pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid")
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Pchar.Pptraining, .Psaved.Ppuppetmaster.Curtempid, .Pchar.Uipowertreemodcount, .Pchar.Uilastfreerespectime, .Pchar.Uilastforcedrespectime, .Pplayer.Icreatedtime, .Pchar.Pppowertrees, .Hallegiance, .Hsuballegiance, .Psaved.Ppallowedcritterpets, .Pchar.Pppointspentpowertrees, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome trCharacter_CompleteTraining(ATR_ARGS, NOCONST(Entity)* pBuyer, NOCONST(Entity)* pEnt, 
													const char* pchNewNodeDef, S32 eType,
													PowerTreeSteps *pStepsRespec, PowerTreeSteps *pStepsBuy,
													const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	S32 i;
	for (i = eaSize(&pEnt->pChar->ppTraining)-1; i >= 0; i--)
	{
		NOCONST(CharacterTraining)* pTraining = pEnt->pChar->ppTraining[i];
		if (stricmp(pchNewNodeDef,REF_STRING_FROM_HANDLE(pTraining->hNewNodeDef))==0)
		{
			if (trhCharacter_CompleteTraining(ATR_PASS_ARGS, pBuyer, pEnt, pTraining->uiItemID, pStepsRespec, pStepsBuy, pReason, pExtract))
			{
				//List is manually sorted, so don't remove fast
				StructDestroyNoConst(parse_CharacterTraining, eaRemove(&pEnt->pChar->ppTraining,i));
				TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS %s completed training %s", pEnt->debugName, pchNewNodeDef);
			}
			break;
		}
	}

	TRANSACTION_RETURN_LOG_FAILURE("FAILED Unable to complete training: %s, couldn't find node %s.", pEnt->debugName, pchNewNodeDef);
}

AUTO_TRANSACTION
ATR_LOCKS(pBuyer, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pEnt, ".Pchar.Pptraining");
enumTransactionOutcome trCharacter_CancelTraining(ATR_ARGS, NOCONST(Entity)* pBuyer, NOCONST(Entity)* pEnt, 
												  const char* pchNewNodeDef, const ItemChangeReason *pReason)
{
	S32 i;
	for (i = eaSize(&pEnt->pChar->ppTraining)-1; i >= 0; i--)
	{
		NOCONST(CharacterTraining)* pTraining = pEnt->pChar->ppTraining[i];
		if (stricmp(pchNewNodeDef,REF_STRING_FROM_HANDLE(pTraining->hNewNodeDef))==0)
		{
			NOCONST(Item)* pItem = NULL;
			BagIterator* pIter = inv_trh_FindItemByID(ATR_PASS_ARGS, pBuyer, pTraining->uiItemID);
			S32 iOwnerCurrency = inv_trh_GetNumericValue(ATR_PASS_ARGS, pBuyer, pTraining->pchRefundNumeric);
			
			// Try to give the buyer a refund
			if (pTraining->iRefundAmount > 0 && !inv_ent_trh_SetNumeric(ATR_PASS_ARGS, pBuyer, true, pTraining->pchRefundNumeric, iOwnerCurrency + pTraining->iRefundAmount, pReason))
			{
				bagiterator_Destroy(pIter);
				TRANSACTION_RETURN_LOG_FAILURE("FAILED Unable to cancel training: %s, couldn't set cost numeric.", pEnt->debugName);
			}
			if (pIter)
				pItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, bagiterator_GetCurrentBag(pIter), pIter->i_cur);
			if (NONNULL(pItem))
			{
				pItem->flags &= ~(kItemFlag_TrainingFromItem);
			}
			bagiterator_Destroy(pIter);
			//List is manually sorted, so don't remove fast
			StructDestroyNoConst(parse_CharacterTraining, eaRemove(&pEnt->pChar->ppTraining,i));
			break;
		}
	}
	if (i < 0)
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED Unable to cancel training: %s, couldn't find node %s.", pEnt->debugName, pchNewNodeDef);
	}
	TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS %s cancelled training %s", pEnt->debugName, pchNewNodeDef);
}

typedef struct CharacterTrainingCBData
{
	GlobalType			uiTraineeType;
	ContainerID			uiTraineeID;
	GlobalType			uiOwnerType;
	ContainerID			uiOwnerID;
	U32					uiStartTime;
	U32					uiEndTime;
	REF_TO(PTNodeDef)	hNewNodeDef;
	UserDataCallback	pCallback;
	void*				pCallbackData;
} CharacterTrainingCBData;

static void TrainingNotifySuccess(Entity* pOwner, Entity* pTrainee, PTNodeDef* pNodeDef, 
								  const char* pchKey, NotifyType eType)
{
	if (pOwner && pTrainee && pNodeDef)
	{
		const char* pchName = entGetLangName(pTrainee, entGetLanguage(pOwner));
		const char* pchNode = pNodeDef ? entTranslateMessage(pOwner, GET_REF(pNodeDef->pDisplayMessage.hMessage)) : NULL;
		char * estrMsg = NULL;
		estrStackCreate(&estrMsg);
		FormatMessageKey(&estrMsg, pchKey, STRFMT_STRING("name",pchName), STRFMT_STRING("node",pchNode), STRFMT_END);
		notify_NotifySend(pOwner, eType, estrMsg, NULL, NULL);
		estrDestroy(&estrMsg);
	}
}

static void TrainingLogFailure(TransactionReturnVal* returnVal, CharacterTrainingCBData* cbData, const char* pchInfoString)
{
	int i;
	for (i = 0; i < returnVal->iNumBaseTransactions; i++)
	{			
		if (returnVal->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE)
		{
			if (gbEnableGamePlayDataLogging)
			{
				objLog(LOG_CONTAINER, cbData->uiTraineeType, cbData->uiTraineeID, 0, NULL, NULL, NULL, NULL, pchInfoString, 
					"Failed because: %s", returnVal->pBaseReturnVals[i].returnString);
			}
			break;
		}
	}
}

static void StartTraining_CB(TransactionReturnVal* returnVal, CharacterTrainingCBData* cbData)
{
	Entity* pOwner = entFromContainerIDAnyPartition(cbData->uiOwnerType,cbData->uiOwnerID);
	Entity* pTrainee;
	
	if (pOwner) {
		pTrainee = entity_GetSubEntity(entGetPartitionIdx(pOwner),pOwner,cbData->uiTraineeType,cbData->uiTraineeID);

		switch(returnVal->eOutcome)
		{
			xcase TRANSACTION_OUTCOME_SUCCESS:
			{
				PTNodeDef* pNodeDef = GET_REF(cbData->hNewNodeDef);
				const char* pchMsgKey = "Training_Started";
				if (cbData->uiEndTime <= cbData->uiStartTime)
					pchMsgKey = "Training_Complete";
				TrainingNotifySuccess(pOwner, pTrainee, pNodeDef, pchMsgKey, kNotifyType_TrainingStarted);
				if (cbData->pCallback)
					cbData->pCallback(cbData->pCallbackData);
			}
			xcase TRANSACTION_OUTCOME_FAILURE:
			{
				TrainingLogFailure(returnVal, cbData, "StartTraining");
			}	
		}
	}
	REMOVE_HANDLE(cbData->hNewNodeDef);
	free(cbData);
}

static void CompleteTraining_CB(TransactionReturnVal* returnVal, CharacterTrainingCBData* cbData)
{
	Entity* pOwner = entFromContainerIDAnyPartition(cbData->uiOwnerType,cbData->uiOwnerID);
	Entity* pTrainee;
	
	if (pOwner) {
		pTrainee = entity_GetSubEntity(entGetPartitionIdx(pOwner),pOwner,cbData->uiTraineeType,cbData->uiTraineeID);

		switch(returnVal->eOutcome)
		{
			xcase TRANSACTION_OUTCOME_SUCCESS:
			{
				PTNodeDef* pNodeDef = GET_REF(cbData->hNewNodeDef);
				TrainingNotifySuccess(pOwner, pTrainee, pNodeDef, "Training_Complete", kNotifyType_TrainingComplete);
			}
			xcase TRANSACTION_OUTCOME_FAILURE:
			{
				TrainingLogFailure(returnVal, cbData, "CompleteTraining");

				if (pOwner && pOwner->pChar && pTrainee && GET_REF(cbData->hNewNodeDef))
				{
					S32 i;
					CharacterTraining* pTraining = NULL;
					for (i = eaSize(&pOwner->pChar->ppTraining)-1; i >= 0; i--)
					{
						pTraining = pOwner->pChar->ppTraining[i];
						if (GET_REF(pTraining->hNewNodeDef) == GET_REF(cbData->hNewNodeDef))
						{
							break;
						}
					}
					if (i >= 0)
					{
						character_CancelTraining(pOwner, pTrainee, pTraining);
					}
				}
			}	
		}

		if(pTrainee->pChar && !pTrainee->pChar->bResetPowersArray)
		{
			pTrainee->pChar->bResetPowersArray = true;
			// If the powers array isn't reset, the following powers lists need to be cleared
			//  because they cannot be trusted to have valid powers
			eaClear(&pTrainee->pChar->ppPowers);
			eaClear(&pTrainee->pChar->ppPowersLimitedUse);
			eaClear(&pTrainee->pChar->modArray.ppPowers);
		}
	}
	REMOVE_HANDLE(cbData->hNewNodeDef);
	free(cbData);
}

static void CancelTraining_CB(TransactionReturnVal* returnVal, CharacterTrainingCBData* cbData)
{
	switch(returnVal->eOutcome)
	{
		xcase TRANSACTION_OUTCOME_SUCCESS:
	{
		Entity* pOwner = entFromContainerIDAnyPartition(cbData->uiOwnerType,cbData->uiOwnerID);
		Entity* pTrainee;
		
		if (pOwner) {
			PTNodeDef* pNodeDef = GET_REF(cbData->hNewNodeDef);
			pTrainee = entity_GetSubEntity(entGetPartitionIdx(pOwner),pOwner,cbData->uiTraineeType,cbData->uiTraineeID);
			TrainingNotifySuccess(pOwner, pTrainee, pNodeDef, "Training_Canceled", kNotifyType_TrainingCanceled);
		}
	}
	xcase TRANSACTION_OUTCOME_FAILURE:
	{
		TrainingLogFailure(returnVal,cbData,"CancelTraining");
	}
	}
	REMOVE_HANDLE(cbData->hNewNodeDef);
	free(cbData);
}

static bool character_CanTrain(int iPartitionIdx,Entity* pOwner, Entity* pEnt, CharacterTraining* pTraining, 
							   PowerTreeSteps* pStepsRespec, PowerTreeSteps* pStepsBuy)
{
	bool bSuccess = false;

	switch (pTraining->eType)
	{
		xcase CharacterTrainingType_Give:
		{
			PTNodeDef* pNewNodeDef = GET_REF(pTraining->hNewNodeDef);
			const char* pchNewNode = NULL;
			NOCONST(PTNode)* pNode = NULL;
			const char* pchTree = NULL;
			S32 iNodeRank = -1;
			
			if (pNewNodeDef)
			{
				NOCONST(PowerTree)* pTree = NULL;
				pchNewNode = pNewNodeDef->pchNameFull;
				pNode = character_FindPowerTreeNodeHelper(CONTAINER_NOCONST(Entity, pEnt)->pChar, &pTree, pchNewNode);
				if (pNode)
				{
					pchTree = REF_STRING_FROM_HANDLE(pTree->hDef);
					iNodeRank = pNode->iRank;
				}
				else
				{
					PowerTreeDef* pTreeDef = powertree_TreeDefFromNodeDef(pNewNodeDef);
					if (pTreeDef)
					{
						pchTree = pTreeDef->pchName;
					}
				}
			}
			if (pchTree && pTraining->iNewNodeRank > iNodeRank)
			{
				PowerTreeSteps* pCheckStepsRespec = StructCreate(parse_PowerTreeSteps);
				PowerTreeSteps* pCheckStepsBuy = StructCreate(parse_PowerTreeSteps);
				S32 iNewRank = pTraining->iNewNodeRank; 

				// Create a step to buy the new node
				PowerTreeStep* pStep = StructCreate(parse_PowerTreeStep);
				pStep->pchTree = pchTree;
				pStep->pchNode = pchNewNode;
				pStep->iRank = iNewRank;
				eaPush(&pCheckStepsBuy->ppSteps,pStep);

				pCheckStepsBuy->bIsTraining = true;

				if (entity_PowerTreeStepsRespecSteps(iPartitionIdx,pEnt,pOwner,pCheckStepsRespec,pCheckStepsBuy,pStepsRespec,pStepsBuy,false,false))
				{
					bSuccess = true;
				}
				StructDestroy(parse_PowerTreeSteps, pCheckStepsRespec);
				StructDestroy(parse_PowerTreeSteps, pCheckStepsBuy);
			}
		}
		xcase CharacterTrainingType_Replace:
		{
			NOCONST(PowerTree)* pTree = NULL;
			const char* pchOldNode = REF_STRING_FROM_HANDLE(pTraining->hOldNodeDef);
			const char* pchNewNode = REF_STRING_FROM_HANDLE(pTraining->hNewNodeDef);
			NOCONST(PTNode)* pOldNode = character_FindPowerTreeNodeHelper(CONTAINER_NOCONST(Character, pEnt->pChar), &pTree, pchOldNode);
			NOCONST(PTNode)* pNewNode = character_FindPowerTreeNodeHelper(CONTAINER_NOCONST(Character, pEnt->pChar), NULL, pchNewNode);

			if (pTree && pOldNode && !pNewNode)
			{
				PowerTreeSteps* pCheckStepsRespec = StructCreate(parse_PowerTreeSteps);
				PowerTreeSteps* pCheckStepsBuy = StructCreate(parse_PowerTreeSteps);
				PowerTreeStep* pStep;
				const char* pchTree = REF_STRING_FROM_HANDLE(pTree->hDef);
				int iOldRank = pOldNode->iRank;
				S32 iNewRank = pTraining->iNewNodeRank;
				
				// Respec the old node
				for (; iOldRank >= 0; iOldRank--)
				{
					pStep = StructCreate(parse_PowerTreeStep);
					pStep->pchTree = pchTree;
					pStep->pchNode = pchOldNode;
					eaPush(&pCheckStepsRespec->ppSteps,pStep);
				}

				// Create a step to buy the new node
				pStep = StructCreate(parse_PowerTreeStep);
				pStep->pchTree = pchTree;
				pStep->pchNode = pchNewNode;
				pStep->iRank = iNewRank;
				eaPush(&pCheckStepsBuy->ppSteps,pStep);

				pCheckStepsBuy->bIsTraining = true;

				if (entity_PowerTreeStepsRespecSteps(iPartitionIdx,pEnt,pOwner,pCheckStepsRespec,pCheckStepsBuy,pStepsRespec,pStepsBuy,false,false))
				{
					bSuccess = true;
				}
				StructDestroy(parse_PowerTreeSteps, pCheckStepsRespec);
				StructDestroy(parse_PowerTreeSteps, pCheckStepsBuy);
			}
		}
		xcase CharacterTrainingType_ReplaceEscrow:
		{
			const char* pchOldNode = REF_STRING_FROM_HANDLE(pTraining->hOldNodeDef);
			NOCONST(PowerTree)* pTree = NULL;
			character_FindPowerTreeNodeHelper(CONTAINER_NOCONST(Character, pEnt->pChar), &pTree, pchOldNode);

			if (pTree)
			{
				const char* pchNewNode = REF_STRING_FROM_HANDLE(pTraining->hNewNodeDef);
				const char* pchTree = REF_STRING_FROM_HANDLE(pTree->hDef);
				if (entity_PowerTreeNodeReplaceEscrowSteps(iPartitionIdx,pEnt,pOwner,pchTree,pchOldNode,pchTree,pchNewNode,pStepsRespec,pStepsBuy))
				{
					bSuccess = true;
				}
			}
		}
	}
	return bSuccess;
}

void character_StartTraining(int iPartitionIdx, Entity* pOwner, Entity* pEnt, 
							 const char* pchOldNode, const char* pchNewNode, S32 iNewNodeRank,
							 const char* pchTrainingNumeric, S32 iTrainingCost,
							 F32 fRefundPercent, U32 uiStartTime, U32 uiEndTime, U64 uiItemID, bool bRemoveItem, bool bFromStore,
							 S32 eType, UserDataCallback pCallback, void* pCallbackData)
{
	PowerTreeSteps *pStepsRespec = StructCreate(parse_PowerTreeSteps);
	PowerTreeSteps *pStepsBuy = StructCreate(parse_PowerTreeSteps);
	NOCONST(CharacterTraining)* pTraining = StructCreateNoConst(parse_CharacterTraining);
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pOwner);
	
	SET_HANDLE_FROM_STRING(g_hPowerTreeNodeDefDict, pchOldNode, pTraining->hOldNodeDef);
	SET_HANDLE_FROM_STRING(g_hPowerTreeNodeDefDict, pchNewNode, pTraining->hNewNodeDef);
	pTraining->eType = eType;
	pTraining->iNewNodeRank = iNewNodeRank;
	pTraining->uiBuyerType = entGetType(pOwner);
	pTraining->uiBuyerID = entGetContainerID(pOwner);
	pTraining->iRefundAmount = iTrainingCost * fRefundPercent;
	pTraining->pchRefundNumeric = allocAddString(pchTrainingNumeric);
	pTraining->uiStartTime = uiStartTime;
	pTraining->uiCompleteTime = uiEndTime;
	pTraining->uiItemID = bRemoveItem ? uiItemID : 0;

	pStepsBuy->bIsTraining = true;

	// Validate the training
	if (character_CanTrain(iPartitionIdx, pOwner, pEnt, (CharacterTraining*)pTraining, pStepsRespec, pStepsBuy))
	{
		PropPowerSaveList SaveList = {0};
		CharacterTrainingCBData* pData = calloc(sizeof(CharacterTrainingCBData), 1);
		ItemChangeReason reason = {0};

		pData->uiTraineeType = entGetType(pEnt);
		pData->uiTraineeID = entGetContainerID(pEnt);
		pData->uiOwnerType = entGetType(pOwner);
		pData->uiOwnerID = entGetContainerID(pOwner);
		pData->uiStartTime = uiStartTime;
		pData->uiEndTime = uiEndTime;
		SET_HANDLE_FROM_STRING(g_hPowerTreeNodeDefDict, pchNewNode, pData->hNewNodeDef);
		pData->pCallback = pCallback;
		pData->pCallbackData = pCallbackData;

		if (uiStartTime < uiEndTime && entGetType(pEnt) == GLOBALTYPE_ENTITYSAVEDPET)
		{
			ent_PetGetPropPowersToSave(pOwner, pEnt, NULL, NULL, &SaveList.eaData);
		}

		if (bFromStore)
		{
			inv_FillItemChangeReasonStore(&reason, pOwner ? pOwner : pEnt, "Pets:StartTraining", pEnt->debugName);
		}
		else
		{
			inv_FillItemChangeReason(&reason, pOwner ? pOwner : pEnt, "Pets:StartTraining", pEnt->debugName);
		}

		if (!pOwner || pEnt == pOwner)
		{
			AutoTrans_trCharacter_StartTraining(LoggedTransactions_CreateManagedReturnValEnt("StartTrainingOfficer",pEnt,StartTraining_CB,pData), 
				GLOBALTYPE_GAMESERVER, 
				entGetType(pEnt), entGetContainerID(pEnt),
				pchTrainingNumeric, iTrainingCost, uiItemID, (CharacterTraining*)pTraining, &SaveList, 
				pStepsRespec, pStepsBuy, &reason, pExtract);
		}
		else
		{
			AutoTrans_trCharacter_StartTrainingForEnt(LoggedTransactions_CreateManagedReturnValEnt("StartTrainingOfficer",pOwner,StartTraining_CB,pData), 
				GLOBALTYPE_GAMESERVER, 
				entGetType(pOwner), entGetContainerID(pOwner),
				entGetType(pEnt), entGetContainerID(pEnt),
				pchTrainingNumeric, iTrainingCost, uiItemID, (CharacterTraining*)pTraining, &SaveList, 
				pStepsRespec, pStepsBuy, &reason, pExtract);
		}
		StructDeInit(parse_PropPowerSaveList, &SaveList);
	}
	//Clean up
	StructDestroyNoConst(parse_CharacterTraining, pTraining);
	StructDestroy(parse_PowerTreeSteps, pStepsRespec);
	StructDestroy(parse_PowerTreeSteps, pStepsBuy);
}



// Wrapper for trCharacter_CompleteTraining. Calls trCharacter_CancelTraining if "complete" transaction fails.
void character_CompleteTraining(int iPartitionIdx, Entity* pOwner, Entity* pEnt, CharacterTraining* pTraining, GameAccountDataExtract *pExtract)
{
	PowerTreeSteps *pStepsRespec = StructCreate(parse_PowerTreeSteps);
	PowerTreeSteps *pStepsBuy = StructCreate(parse_PowerTreeSteps);

	if (character_CanTrain(iPartitionIdx, pOwner, pEnt, pTraining, pStepsRespec, pStepsBuy))
	{
		CharacterTrainingCBData* pData = calloc(sizeof(CharacterTrainingCBData), 1);
		ItemChangeReason reason = {0};

		pData->uiTraineeType = entGetType(pEnt);
		pData->uiTraineeID = entGetContainerID(pEnt);
		pData->uiOwnerType = entGetType(pOwner);
		pData->uiOwnerID = entGetContainerID(pOwner);
		pData->uiStartTime = pTraining->uiStartTime;
		pData->uiEndTime = pTraining->uiCompleteTime;
		COPY_HANDLE(pData->hNewNodeDef,pTraining->hNewNodeDef);
		pTraining->bCompletionPending = true;

		inv_FillItemChangeReason(&reason, pOwner, "Pets:CompleteTraining", pEnt->debugName);

		AutoTrans_trCharacter_CompleteTraining(LoggedTransactions_CreateManagedReturnVal("CompleteTraining",CompleteTraining_CB,pData),
			GetAppGlobalType(),
			entGetType(pOwner), entGetContainerID(pOwner),
			entGetType(pEnt), entGetContainerID(pEnt),
			REF_STRING_FROM_HANDLE(pTraining->hNewNodeDef), pTraining->eType,
			pStepsRespec, pStepsBuy, &reason, pExtract);
	}
	//Clean up
	StructDestroy(parse_PowerTreeSteps,pStepsRespec);
	StructDestroy(parse_PowerTreeSteps,pStepsBuy);
}

// Wrapper for trCharacter_CancelTraining.
void character_CancelTraining(Entity* pOwner, Entity* pEnt, CharacterTraining* pTraining)
{
	CharacterTrainingCBData* pData = calloc(sizeof(CharacterTrainingCBData), 1);
	ItemChangeReason reason = {0};

	pData->uiTraineeType = entGetType(pEnt);
	pData->uiTraineeID = entGetContainerID(pEnt);
	pData->uiOwnerType = entGetType(pOwner);
	pData->uiOwnerID = entGetContainerID(pOwner);
	pData->uiStartTime = pTraining->uiStartTime;
	pData->uiEndTime = pTraining->uiCompleteTime;
	COPY_HANDLE(pData->hNewNodeDef,pTraining->hNewNodeDef);

	inv_FillItemChangeReason(&reason, pOwner, "Pets:CancelTraining", pEnt->debugName);

	AutoTrans_trCharacter_CancelTraining(LoggedTransactions_CreateManagedReturnVal("CancelTraining",CancelTraining_CB,pData), GetAppGlobalType(),
		pTraining->uiBuyerType, pTraining->uiBuyerID,
		entGetType(pEnt), entGetContainerID(pEnt),
		REF_STRING_FROM_HANDLE(pTraining->hNewNodeDef), &reason);

	pTraining->bCompletionPending = true;
}
