/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "allegiance.h"
#include "AutoTransDefs.h"
#include "Character.h"
#include "Entity.h"
#include "EntityLib.h"
#include "GameAccountDataCommon.h"
#include "gslEntity.h"
#include "LoggedTransactions.h"
#include "inventoryCommon.h"
#include "objTransactions.h"
#include "OfficerCommon.h"
#include "Powers.h"
#include "PowerTree.h"
#include "PowerTreeHelpers.h"
#include "PowerTreeTransactions.h"
#include "gslActivityLog.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/OfficerCommon_h_ast.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

AUTO_TRANSACTION
ATR_LOCKS(pOwner, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pOfficer, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Pplayer.Pugckillcreditlimit, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome trEntity_PromoteOfficer(ATR_ARGS, NOCONST(Entity)* pOwner, NOCONST(Entity)* pOfficer, OfficerRankDef* pDef, S32 iPromotionRank, const ItemChangeReason *pReason)
{
	S32 iOwnerCurrency;
	S32 iNextRank = inv_trh_GetNumericValue( ATR_PASS_ARGS, pOfficer, "StarfleetRank" ) + 1;

	if ( iNextRank != iPromotionRank )
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED Unable to promote officer: %s, invalid promotion rank.",pOfficer->debugName);
	}

	iOwnerCurrency = inv_trh_GetNumericValue( ATR_PASS_ARGS, pOwner, pDef->pchCostNumeric );

	if ( iOwnerCurrency < pDef->iCost || !inv_ent_trh_SetNumeric( ATR_EMPTY_ARGS, pOwner, true, pDef->pchCostNumeric, iOwnerCurrency - pDef->iCost, pReason ) )
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED Unable to promote officer: %s, couldn't set cost numeric.",pOfficer->debugName);
	}

	if ( !inv_ent_trh_SetNumeric( ATR_EMPTY_ARGS, pOfficer, true, "StarfleetRank", iPromotionRank, pReason ) )
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED Unable to promote officer: %s, couldn't set rank numeric.",pOfficer->debugName);
	}

	TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS Promoted officer: %s",pOfficer->debugName);
}

typedef struct PromoteOfficerCBData
{
	EntityRef playerRef;
	ContainerID officerID;
} PromoteOfficerCBData;

static void
PromoteOfficer_CB(TransactionReturnVal *pReturn, PromoteOfficerCBData *cbData)
{
	Entity *playerEnt;
	Entity *officerEnt;

	// don't log on failure
	if ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
	{
		// get owner entity
		playerEnt = entFromEntityRefAnyPartition(cbData->playerRef);

		if ( playerEnt != NULL )
		{
			int iPartitionIdx = entGetPartitionIdx(playerEnt);

			officerEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYSAVEDPET, cbData->officerID);

			if (!officerEnt)
				officerEnt = entity_GetSubEntity(iPartitionIdx, playerEnt, GLOBALTYPE_ENTITYSAVEDPET, cbData->officerID);

			if ( officerEnt != NULL )
			{
				gslActivity_AddPetPromoteEntry(playerEnt, officerEnt);
				
				// Promotion was success, try to AutoBuy new stuff
				entity_PowerTreeAutoBuy(iPartitionIdx, officerEnt, playerEnt);
			}
		}
	}

	free(cbData);
	return;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslPromoteOfficer( Entity* pPlayerEnt, S32 iOwnerType, U32 iOwnerID, U32 uiOfficerID )
{
	int iPartitionIdx = pPlayerEnt ? entGetPartitionIdx(pPlayerEnt) : PARTITION_UNINITIALIZED;
	Entity* pEnt = entFromContainerID( iPartitionIdx, iOwnerType, iOwnerID );
	Entity* pOfficer;
	AllegianceDef* pAllegiance = pPlayerEnt ? GET_REF(pPlayerEnt->hAllegiance) : NULL;
	AllegianceDef* pSubAllegiance = pPlayerEnt ? GET_REF(pPlayerEnt->hSubAllegiance) : NULL;
	AllegianceDef* pPreferredAllegiance = pPlayerEnt ? allegiance_GetOfficerPreference(pAllegiance, pSubAllegiance) : NULL;
	OfficerRankDef* pDef = NULL;
	PromoteOfficerCBData *cbData;
	ItemChangeReason reason = {0};
	S32 iNextRank;

	if (!Officer_CanPromote( pEnt, uiOfficerID, pPreferredAllegiance ))
	{
		return;
	}
	if (!(pOfficer = entity_GetSubEntity(iPartitionIdx,pEnt,GLOBALTYPE_ENTITYSAVEDPET,uiOfficerID)))
	{
		return;
	}
	iNextRank = inv_GetNumericItemValue(pOfficer,"StarfleetRank") + 1;

	if (!(pDef = Officer_GetRankDef(iNextRank, pAllegiance, pSubAllegiance)))
	{
		return;
	}

	cbData = (PromoteOfficerCBData *)malloc(sizeof(PromoteOfficerCBData));
	cbData->playerRef = entGetRef(pEnt);
	cbData->officerID = entGetContainerID(pOfficer);

	inv_FillItemChangeReason(&reason, pEnt, "PromoteOfficer", pOfficer->debugName);

	AutoTrans_trEntity_PromoteOfficer(	LoggedTransactions_CreateManagedReturnVal("PromoteOfficer",PromoteOfficer_CB,cbData), 
										GLOBALTYPE_GAMESERVER, 
										entGetType(pEnt), entGetContainerID(pEnt),
										entGetType(pOfficer), entGetContainerID(pOfficer),
										pDef, iNextRank, &reason);

}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslTrainOfficer( Entity* pPlayerEnt, S32 iTrainerType, U32 iTrainerID, U32 uiOfficerID, const char* pchOldNode, const char* pchNewNode )
{
	int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	Entity* pEnt = entFromContainerID( iPartitionIdx, iTrainerType, iTrainerID );
	Entity* pOfficer;
	OfficerRankDef* pDef = NULL;
	AllegianceDef* pAllegiance = pPlayerEnt ? GET_REF(pPlayerEnt->hAllegiance) : NULL;
	AllegianceDef* pSubAllegiance = pPlayerEnt ? GET_REF(pPlayerEnt->hSubAllegiance) : NULL;
	AllegianceDef* pPreferredAllegiance = pPlayerEnt ? allegiance_GetOfficerPreference(pAllegiance, pSubAllegiance) : NULL;
	U32 uiStartTime, uiEndTime;
	PTNodeDef* pNewNodeDef = powertreenodedef_Find(pchNewNode);
	PowerDef* pNewPowerDef = pNewNodeDef && eaSize(&pNewNodeDef->ppRanks)>0 ? GET_REF(pNewNodeDef->ppRanks[0]->hPowerDef) : NULL;
	PowerPurpose ePurpose = kPowerPurpose_Uncategorized;

	if ( pEnt==NULL )
		pEnt = entity_GetSubEntity(iPartitionIdx,pPlayerEnt,iTrainerType,iTrainerID);

	if(pNewPowerDef)
	{
		ePurpose = pNewPowerDef->ePurpose;
	}
	else if(pNewNodeDef)
	{
		ePurpose = pNewNodeDef->ePurpose;
	}

	if (	pEnt==NULL || pNewNodeDef==NULL
		|| !Officer_CanTrain( iPartitionIdx, pPlayerEnt, pEnt, uiOfficerID, pchOldNode, pchNewNode, true, true, pPreferredAllegiance ) 
		|| !(pOfficer = entity_GetSubEntity(iPartitionIdx,pPlayerEnt,GLOBALTYPE_ENTITYSAVEDPET,uiOfficerID))
		|| !(pDef = Officer_GetRankDef(ePurpose-1, pAllegiance, pSubAllegiance)))
		return;

	uiStartTime = timeSecondsSince2000();
	uiEndTime = uiStartTime + pDef->uiTrainingTime;

	character_StartTraining(iPartitionIdx,pPlayerEnt, pOfficer,
							pchOldNode, pchNewNode, 0, pDef->pchTrainingNumeric, pDef->iTrainingCost, 
							pDef->fTrainingRefundPercent, uiStartTime, uiEndTime, 0, false, false,
							CharacterTrainingType_ReplaceEscrow, NULL, NULL);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslCancelTrainingOfficer( Entity* pPlayerEnt, U32 uiOfficerID, const char* pchNewNode )
{
	S32 i;
	int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	Entity* pOfficer = entity_GetSubEntity(iPartitionIdx,pPlayerEnt,GLOBALTYPE_ENTITYSAVEDPET,uiOfficerID);
	CharacterTraining* pTraining = NULL;
	
	if ( pOfficer==NULL || pOfficer->pChar==NULL )
		return;

	for ( i = eaSize(&pOfficer->pChar->ppTraining)-1; i >= 0; i-- )
	{
		if ( stricmp(pchNewNode,REF_STRING_FROM_HANDLE(pOfficer->pChar->ppTraining[i]->hNewNodeDef))==0 )
		{
			pTraining = pOfficer->pChar->ppTraining[i];
			break;
		}
	}
	if ( pTraining==NULL )
		return;

	character_CancelTraining( pPlayerEnt, pOfficer, pTraining );
}