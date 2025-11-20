
#include "Entity.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "CharacterRespecServer.h"
#include "earray.h"
#include "estring.h"
#include "GameAccountDataCommon.h"
#include "Player.h"
#include "Powers.h"
#include "Powers_h_ast.h"
#include "PowerTree.h"
#include "PowerTree_h_ast.h"
#include "PowerTreeHelpers.h"
#include "PowerTreeTransactions.h"
#include "PowerModes.h"
#include "inventoryCommon.h"
#include "tradeCommon.h"
#include "gslEventSend.h"

#include "objTransactions.h"
#include "LoggedTransactions.h"
#include "TransactionSystem.h"
#include "WorldGrid.h"

#include "autogen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/gslPowerCart_c_ast.h"
#include "Player_h_ast.h"
#include "Character_h_ast.h"

AUTO_STRUCT;
typedef struct PowerCartCBData
{
	EntityRef erEnt;
} PowerCartCBData;

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslLoadPowerCartList(Entity* pPlayerEnt)
{
	if ( pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pUI->pLooseUI->ppSavedCartPowers)
	{
		int i;
		SavedCartPowerList* pList = StructCreate( parse_SavedCartPowerList );

		for ( i = 0; i < eaSize( &pPlayerEnt->pPlayer->pUI->pLooseUI->ppSavedCartPowers ); i++ )
		{
			SavedCartPower* pNode = StructClone( parse_SavedCartPower, pPlayerEnt->pPlayer->pUI->pLooseUI->ppSavedCartPowers[i] );

			eaPush( &pList->ppNodes, pNode );
		}

		ClientCmd_gclLoadPowerCartList( pPlayerEnt, pList );

		StructDestroy( parse_SavedCartPowerList, pList );
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslSavePowerCartList(Entity* pPlayerEnt, SavedCartPowerList* pList)
{
	if ( pPlayerEnt == NULL || pPlayerEnt->pPlayer == NULL ) 
		return;

	//clear the old data even if the list is NULL ( because we want to support saving a blank list )
	eaClearStruct( &pPlayerEnt->pPlayer->pUI->pLooseUI->ppSavedCartPowers, parse_SavedCartPower );

	if ( pList && pList->ppNodes )
	{
		int i;
		for ( i = 0; i < eaSize( &pList->ppNodes ); i++ )
		{
			SavedCartPower *pSavedPower = StructClone( parse_SavedCartPower, pList->ppNodes[i] );

			eaPush( &pPlayerEnt->pPlayer->pUI->pLooseUI->ppSavedCartPowers, pSavedPower );
		}
	}
	entity_SetDirtyBit(pPlayerEnt, parse_PlayerUI, pPlayerEnt->pPlayer->pUI, true);
	entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
}

void trPowerCart_BuyCartItems_PostTransaction(TransactionReturnVal* returnVal, PowerCartCBData* pData)
{
	if ( pData )
	{
		Entity* pPlayerEnt = entFromEntityRefAnyPartition( pData->erEnt );
		if ( pPlayerEnt )
		{
			if ( returnVal->eOutcome == TRANSACTION_OUTCOME_FAILURE )
			{
				S32 i;
				for (i = 0; i < returnVal->iNumBaseTransactions; i++)
				{
					if ( returnVal->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE )
					{
						Alertf("Could not buy power cart item. Reason: %s", returnVal->pBaseReturnVals[i].returnString);
					}
				}
				ClientCmd_gclNotifyPowerCartPurchaseFailure( pPlayerEnt );
			}
			else if ( pPlayerEnt->pChar )
			{
				int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);

				// Dirty all the Character's innate data
				character_DirtyInnateEquip(pPlayerEnt->pChar);
				character_DirtyInnatePowers(pPlayerEnt->pChar);
				character_DirtyPowerStats(pPlayerEnt->pChar);
				character_DirtyInnateAccrual(pPlayerEnt->pChar);

				// Deactivate all Passives (ResetPowersArray will restart them)
				character_DeactivatePassives(iPartitionIdx, pPlayerEnt->pChar);

				character_ResetPowersArray(iPartitionIdx, pPlayerEnt->pChar, pExtract);

				// reset the free-respec flag
				pPlayerEnt->pChar->iFreeRespecAvailable = trhEntity_GetFreeRespecTime(CONTAINER_NOCONST(Entity, pPlayerEnt));
				entity_SetDirtyBit(pPlayerEnt, parse_Character, pPlayerEnt->pChar, false);

				// check for locking in of purchase
				if(gPowerConfig.bLockPowersIfNotinPowerhouse)
				{
					const char *pcCurrentMap = zmapInfoGetPublicName(NULL);
					ZoneMapInfo* pCurrZoneMap = worldGetZoneMapByPublicName(pcCurrentMap);

					// lock powers as player is not in power house
					if(pCurrZoneMap && !zmapInfoConfirmPurchasesOnExit(pCurrZoneMap))
					{
						// lock in player powers, used by UI when not in power house
						AutoTrans_trCharacter_UpdatePointsSpentPowerTrees(LoggedTransactions_MakeEntReturnVal("UpdatePointsSpentPowerTrees", pPlayerEnt), GetAppGlobalType(), entGetType(pPlayerEnt), entGetContainerID(pPlayerEnt));
					}
				}

				eventsend_RecordPowerTreeStepsAdded(pPlayerEnt);
			}
		}
		StructDestroy( parse_PowerCartCBData, pData );
	}
}



bool gslPowerCart_GetBuySteps(SA_PARAM_NN_VALID Entity* pPlayerEnt, SA_PARAM_NN_VALID Entity* pEnt, PowerTreeSteps *pSteps)
{
	bool bSuccess = true;
	
	S32 i, iSize = eaSize( &pPlayerEnt->pPlayer->pUI->pLooseUI->ppSavedCartPowers );
	S32 iValidCount = 0;

	//buy the items
	for ( i = 0; i < iSize; i++ )
	{
		int j;
		SavedCartPower* pCartPower = pPlayerEnt->pPlayer->pUI->pLooseUI->ppSavedCartPowers[i];
		
		PowerTreeStep* pStep;
		
		PTNodeDef* pNodeDef = GET_REF( pCartPower->hNodeDef );

		PowerTreeDef *pTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);

		NOCONST(PTNode)* pNode = entity_FindPowerTreeNodeHelper( CONTAINER_NOCONST(Entity, pEnt), pNodeDef );

		if ( pNodeDef==NULL || pTreeDef==NULL || (pNode && !pNode->bEscrow && pCartPower->iRank <= pNode->iRank))
		{
			bSuccess = false;
			break;
		}

		// This stupid bit of code skips any duplicates, since the UI seems to enjoy adding them
		for(j=eaSize(&pSteps->ppSteps)-1; j>=0; j--)
		{
			PowerTreeStep *pStepPrior = pSteps->ppSteps[j];
			if(pStepPrior->pchTree==pTreeDef->pchName
				&& pStepPrior->pchNode==pNodeDef->pchNameFull
				&& pStepPrior->iRank==pCartPower->iRank)
			{
				break;
			}
		}
		if(j>=0)
			continue;

		iValidCount++;

		pStep = StructCreate( parse_PowerTreeStep );

		pStep->pchTree = pTreeDef->pchName;
		pStep->pchNode = pNodeDef->pchNameFull;
		pStep->pchEnhancement = NULL;
		pStep->iRank = pCartPower->iRank;

		eaPush( &pSteps->ppSteps, pStep );
	}

	if ( iValidCount == 0 )
	{
		bSuccess = false;
	}

	pSteps->uiPowerTreeModCount = pEnt->pChar->uiPowerTreeModCount;

	return bSuccess;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslBuyPowerCartItems(Entity* pPlayerEnt)
{
	if (	pPlayerEnt
		&&	pPlayerEnt->pChar
		&&	pPlayerEnt->pPlayer
		&&	!character_HasMode(pPlayerEnt->pChar,kPowerMode_Combat))
	{
		PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);
		
		//get the power steps from the cart data
		bool bSuccess = gslPowerCart_GetBuySteps( pPlayerEnt, pPlayerEnt, pSteps );

		//run the transaction if the buy steps are valid
		if ( bSuccess )
		{
			PowerCartCBData* pData = StructCreate( parse_PowerCartCBData );
			pData->erEnt = entGetRef(pPlayerEnt);
			entity_PowerTreeStepsBuy(entGetPartitionIdx(pPlayerEnt),pPlayerEnt,NULL,pSteps,LoggedTransactions_CreateManagedReturnValEnt("PowerCartPurchase",pPlayerEnt,trPowerCart_BuyCartItems_PostTransaction,pData));
		}
		else
		{
			ClientCmd_gclNotifyPowerCartPurchaseFailure( pPlayerEnt );
		}

		//clear the power cart
		eaClearStruct( &pPlayerEnt->pPlayer->pUI->pLooseUI->ppSavedCartPowers, parse_SavedCartPower );
		entity_SetDirtyBit(pPlayerEnt, parse_PlayerUI, pPlayerEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);

		//destroy the steps
		StructDestroy(parse_PowerTreeSteps,pSteps);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslBuyPowerCartItemsForPet(Entity* pPlayerEnt, U32 iContainerID)
{
	Entity* pEnt = entity_GetSubEntity( entGetPartitionIdx(pPlayerEnt), pPlayerEnt, GLOBALTYPE_ENTITYSAVEDPET, iContainerID );
	
	if (	pEnt 
		&&	pPlayerEnt->pPlayer
		&&	pPlayerEnt->pChar
		&&	!character_HasMode(pPlayerEnt->pChar,kPowerMode_Combat))
	{
		PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);

		//get the power steps from the cart data
		bool bSuccess = gslPowerCart_GetBuySteps( pPlayerEnt, pEnt, pSteps );

		//If the pet is being traded, it is not allowed to buy power cart items
		if(trade_IsPetBeingTraded(pEnt, pPlayerEnt))
		{
			bSuccess = false;
		}
		
		//run the transaction if the buy steps are valid
		if ( bSuccess )
		{
			PowerCartCBData* pData = StructCreate( parse_PowerCartCBData );
			pData->erEnt = entGetRef(pPlayerEnt);
			entity_PowerTreeStepsBuy(entGetPartitionIdx(pPlayerEnt),pEnt,pPlayerEnt,pSteps,LoggedTransactions_CreateManagedReturnValEnt("PowerCartPurchase",pPlayerEnt,trPowerCart_BuyCartItems_PostTransaction,pData));
		}
		else
		{
			ClientCmd_gclNotifyPowerCartPurchaseFailure( pPlayerEnt );
		}

		//clear the power cart
		eaClearStruct( &pPlayerEnt->pPlayer->pUI->pLooseUI->ppSavedCartPowers, parse_SavedCartPower );
		entity_SetDirtyBit(pPlayerEnt, parse_PlayerUI, pPlayerEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);

		//destroy the steps
		StructDestroy(parse_PowerTreeSteps,pSteps);
	}
	else if (pPlayerEnt)
	{
		ClientCmd_gclNotifyPowerCartPurchaseFailure( pPlayerEnt );
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslBuyPowerCartItemsWithRespec(Entity *pPlayerEnt)
{
	if (!pPlayerEnt || !pPlayerEnt->pChar || !pPlayerEnt->pPlayer)
	{
		return;
	}
	if (!entity_PowerCartIsRespecRequired(pPlayerEnt))
	{
		ClientCmd_gclNotifyPowerCartPurchaseFailure(pPlayerEnt);
		return;
	}
	if (!character_HasMode(pPlayerEnt->pChar,kPowerMode_Combat))
	{
		PowerTreeSteps *pStepsBuy = StructCreate(parse_PowerTreeSteps);
		PowerTreeSteps *pStepsRespec = StructCreate(parse_PowerTreeSteps);
		Entity *pFakeEntity = StructCloneWithComment(parse_Entity,pPlayerEnt,"Temp ent in gslBuyPowerCareItemsWithRespec");
		bool bSuccess = false;

		// This is painful because the gslPowerCart_GetBuySteps() needs the Entity to be already-respec'd.
		//  Otherwise you'd just pass the stuff you want to respec and the stuff you want to buy to the
		//  respec function.
		if(pFakeEntity)
		{
			int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

			character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, pPlayerEnt->pChar),pStepsRespec, false, kPTRespecGroup_ALL);
			character_PowerTreeStepsCostRespec(iPartitionIdx, pPlayerEnt->pChar,pStepsRespec,0);
			bSuccess = entity_PowerTreeStepsRespecSteps(iPartitionIdx,pFakeEntity,NULL,pStepsRespec,NULL,NULL,NULL,true,false);

			if(bSuccess && eaSize(&pPlayerEnt->pPlayer->pUI->pLooseUI->ppSavedCartPowers))
				bSuccess = gslPowerCart_GetBuySteps(pFakeEntity,pFakeEntity,pStepsBuy);

			pStepsRespec->iRespecSkillpointSpentMin = character_RespecPointsSpentRequirement(iPartitionIdx, pPlayerEnt->pChar);

			//run the transaction if the character can respec, and the buy steps are valid
			if(bSuccess)
			{
				PowerCartCBData *pData = StructCreate(parse_PowerCartCBData);
				pData->erEnt = entGetRef(pPlayerEnt);

				entity_PowerTreeStepsRespec(iPartitionIdx,pPlayerEnt,pStepsRespec,pStepsBuy,LoggedTransactions_CreateManagedReturnValEnt("PowerCartPurchase",pPlayerEnt,trPowerCart_BuyCartItems_PostTransaction,pData), kPTRespecGroup_ALL,false);
			}
		}

		if(!bSuccess)
		{
			ClientCmd_gclNotifyPowerCartPurchaseFailure(pPlayerEnt);
		}

		//clear the power cart
		eaClearStruct( &pPlayerEnt->pPlayer->pUI->pLooseUI->ppSavedCartPowers, parse_SavedCartPower );
		entity_SetDirtyBit(pPlayerEnt, parse_PlayerUI, pPlayerEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);

		//destroy the steps
		StructDestroy(parse_PowerTreeSteps,pStepsBuy);
		StructDestroy(parse_PowerTreeSteps,pStepsRespec);
		StructDestroy(parse_Entity,pFakeEntity);
	}
}

#include "AutoGen/gslPowerCart_c_ast.c"