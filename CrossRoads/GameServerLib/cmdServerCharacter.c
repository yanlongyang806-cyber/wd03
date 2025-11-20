/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "cmdServerCharacter.h"
#include "aiLib.h"
#include "Character.h"
#include "Character_h_ast.h"
#include "StatPoints_h_ast.h"
#include "CharacterAttribs.h"
#include "CharacterAttribs_h_ast.h"
#include "CharacterClass.h"
#include "contact_common.h"
#include "CostumeCommonLoad.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "file.h"
#include "fileutil.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "gslEntity.h"
#include "LoggedTransactions.h"
#include "gslPowerTransactions.h"
#include "gslSendToClient.h"
#include "pub/gslTransactions.h"
#include "itemCommon.h"
#include "itemTransaction.h"
#include "mission_common.h"
#include "objContainer.h"
#include "objContainerIO.h"
#include "objtransactions.h"
#include "Player.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "PowerHelpers.h"
#include "PowerSlots.h"
#include "PowerTree.h"
#include "PowerTree_h_ast.h"
#include "PowerTreeHelpers.h"
#include "PowerTreeTransactions.h"
#include "powervars.h"
#include "rand.h"
#include "resourceinfo.h"
#include "rewardCommon.h"
#include "SavedPetTransactions.h"
#include "species_common.h"
#include "StringCache.h"
#include "TimedCallback.h"
#include "WorldGrid.h"
#include "AutoGen/cmdServerCharacter_h_ast.h"
#include "AutoGen/cmdServerCharacter_h_ast.c"
#include "AutoGen/ObjectDB_autogen_remotefuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "autogen/GameServerLib_autotransactions_autogen_wrappers.h"

typedef enum eType;

AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pchar.Hspecies, .Pchar.Ppassignedstats, .Pchar.Hclass, .Pchar.Ilevelexp");
bool trhCharacter_SetStatPoints(ATH_ARG NOCONST(Entity)* ent, SA_PARAM_NN_STR const char *pchStatPointPoolName, int eType, int iPoints)
{
	int i;
	NOCONST(AssignedStats) *pStat=NULL;

	int iPointsToSpend = entity_GetAssignedStatUnspent(ent, pchStatPointPoolName);

	if(!character_IsValidStatPoint(eType, pchStatPointPoolName))
	{
		return false;
	}

	for(i=0;i<eaSize(&ent->pChar->ppAssignedStats);i++)
	{
		if(ent->pChar->ppAssignedStats[i]->eType == eType)
		{
			pStat = ent->pChar->ppAssignedStats[i];
			break;
		}
	}

	if(!pStat)
	{
		pStat = CONTAINER_NOCONST(AssignedStats, StructAlloc(parse_AssignedStats));
		pStat->eType = eType;
		pStat->iPoints = 0;
		eaPush(&ent->pChar->ppAssignedStats, pStat);
	}
	iPointsToSpend += pStat->iPoints;
	pStat->iPoints = 0;

	iPoints = min(iPoints, iPointsToSpend);

	pStat->iPoints = iPoints;

	if(pStat->iPoints < 0)
	{
		iPoints -= pStat->iPoints;
		pStat->iPoints = 0;
	}

	return true;
}

AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pchar.Ppsavedattribstats, .Pchar.Ppassignedstats");
enumTransactionOutcome trCharacter_SaveStatsPresetFromAssigned(	ATR_ARGS,NOCONST(Entity)* ent, const char* pchPresetName)
{
	S32 i, j;
	NOCONST(SavedAttribStats)* pDst = NULL;
	
	for ( i = 0; i < eaSize(&ent->pChar->ppSavedAttribStats); i++ )
	{
		if ( stricmp(ent->pChar->ppSavedAttribStats[i]->pchPresetName,pchPresetName)==0 )
		{
			pDst = ent->pChar->ppSavedAttribStats[i];
			break;
		}
	}

	if ( ISNULL(pDst) )
	{
		pDst = StructCreateNoConst( parse_SavedAttribStats );
		pDst->pchPresetName = allocAddString( pchPresetName );
		eaPush( &ent->pChar->ppSavedAttribStats, pDst );
	}

	for ( i = 0; i < eaSize(&ent->pChar->ppAssignedStats); i++ )
	{
		for ( j = 0; j < eaSize(&pDst->ppAssignedStats); j++ )
		{
			if ( ent->pChar->ppAssignedStats[i]->eType == pDst->ppAssignedStats[j]->eType )
			{
				pDst->ppAssignedStats[j]->iPoints = ent->pChar->ppAssignedStats[i]->iPoints;
				break;
			}
		}

		if ( j == eaSize(&pDst->ppAssignedStats) )
		{
			eaPush( &pDst->ppAssignedStats, StructCloneNoConst( parse_AssignedStats, ent->pChar->ppAssignedStats[i] ) );
		}
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Successfully set stat points under preset %s.", pchPresetName);
}

AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pchar.Hspecies, .Pchar.Ppsavedattribstats, .Pchar.Pchcurrentattribstatspreset, .Pchar.Ppassignedstats, .Pchar.Hclass, .Pchar.Ilevelexp");
enumTransactionOutcome trCharacter_SetStatsPresetFromSaved(ATR_ARGS,NOCONST(Entity)* ent, AttribStatsPresetDef* pDef, S32 bReset)
{
	S32 i, j;

	if(ISNULL(ent) || ISNULL(ent->pChar))
		TRANSACTION_RETURN_LOG_FAILURE("FAILED: Could not set stat points");


	for(i=0;i<eaSize(&ent->pChar->ppAssignedStats);i++)
	{
		ent->pChar->ppAssignedStats[i]->iPoints = 0;
		ent->pChar->ppAssignedStats[i]->iPointPenalty = 0;
	}

	for ( i = eaSize(&ent->pChar->ppSavedAttribStats) - 1; i >= 0; i-- )
	{
		if ( stricmp(ent->pChar->ppSavedAttribStats[i]->pchPresetName,pDef->pchName) == 0 )
		{
			if ( bReset )
			{
				StructDestroyNoConst( parse_SavedAttribStats, eaRemoveFast(&ent->pChar->ppSavedAttribStats, i) );
			}
			break;
		}
	}

	if ( !bReset && i >= 0 )
	{
		for ( j = eaSize(&ent->pChar->ppSavedAttribStats[i]->ppAssignedStats) - 1; j >= 0; j-- )
		{
			NOCONST(AssignedStats)* pStats = ent->pChar->ppSavedAttribStats[i]->ppAssignedStats[j];
			StatPointPoolDef *pStatPointPoolDef = StatPointPool_DefFromAttrib(pStats->eType);
			if (pStatPointPoolDef == NULL || !trhCharacter_SetStatPoints(ent, pStatPointPoolDef->pchName, pStats->eType, pStats->iPoints))
				break;
		}
	}
	else
	{
		for ( j = eaSize(&pDef->ppAttribStats) - 1; j >= 0; j-- )
		{
			StatPointPoolDef *pStatPointPoolDef = StatPointPool_DefFromAttrib(pDef->ppAttribStats[j]->eType);
			if (pStatPointPoolDef == NULL || !trhCharacter_SetStatPoints(ent, pStatPointPoolDef->pchName, pDef->ppAttribStats[j]->eType, pDef->ppAttribStats[j]->iPoints))
				break;
		}
	}

	if ( j < 0 )
	{
		//make sure the current preset is set to the passed preset
		ent->pChar->pchCurrentAttribStatsPreset = allocAddString(pDef->pchName);
		TRANSACTION_RETURN_LOG_SUCCESS("Successfully set stats under preset %s.", pDef->pchName);
	}

	TRANSACTION_RETURN_LOG_FAILURE("FAILED: Could not set stat points");
}

AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pchar.Hspecies, .Pchar.Ppassignedstats, .Pchar.Hclass, .Pchar.Ilevelexp");
enumTransactionOutcome trCharacter_SetStatPoints(ATR_ARGS,NOCONST(Entity)* ent, SA_PARAM_NN_STR const char *pchStatPointPoolName, int eType, int iPoints)
{
	if ( !trhCharacter_SetStatPoints( ent, pchStatPointPoolName, eType, iPoints ) )
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED: Cannot set stat points to: %s", StaticDefineIntRevLookup(AttribTypeEnum,eType));
	}
	TRANSACTION_RETURN_LOG_SUCCESS("Stats points %s modified by %d", StaticDefineIntRevLookup(AttribTypeEnum, eType), iPoints);
}

AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pchar.Hspecies, .Pchar.Ppassignedstats, .Pchar.Hclass, .Pchar.Ilevelexp");
bool trhCharacter_ModifyStatPoint(ATH_ARG NOCONST(Entity)* ent, SA_PARAM_NN_STR const char *pchStatPointPoolName, S32 eType, S32 iPoints, S32 *piNewValue)
{
	S32 i;
	NOCONST(AssignedStats) *pStat=NULL;

	S32 iPointsToSpend = entity_GetAssignedStatUnspent(ent, pchStatPointPoolName);

	if(!character_IsValidStatPoint(eType, pchStatPointPoolName))
	{
		return false;
	}

	for(i=0;i<eaSize(&ent->pChar->ppAssignedStats);i++)
	{
		if(ent->pChar->ppAssignedStats[i]->eType == eType)
		{
			pStat = ent->pChar->ppAssignedStats[i];
			break;
		}
	}

	if(!pStat)
	{
		pStat = CONTAINER_NOCONST(AssignedStats, StructAlloc(parse_AssignedStats));
		pStat->eType = eType;
		pStat->iPoints = 0;
		eaPush(&ent->pChar->ppAssignedStats, pStat);
	}

	iPoints = min(iPoints, iPointsToSpend);

	pStat->iPoints += iPoints;

	if(pStat->iPoints < 0)
	{
		iPoints -= pStat->iPoints;
		pStat->iPoints = 0;
	}

	if (piNewValue)
	{
		*piNewValue = pStat->iPoints;
	}

	return true;
}

AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pchar.Hspecies, .Pchar.Ppassignedstats, .Pchar.Hclass, .Pchar.Ilevelexp");
enumTransactionOutcome trCharacter_ModifyStatPoints(ATR_ARGS,NOCONST(Entity)* ent, SA_PARAM_NN_STR const char *pchStatPointPoolName, int eType, int iPoints)
{
	S32 iNewValue = 0;
	if (trhCharacter_ModifyStatPoint(ent, pchStatPointPoolName, eType, iPoints, &iNewValue))
	{
		TRANSACTION_RETURN_LOG_SUCCESS("%s modified by %d. Now %d.", StaticDefineIntRevLookup(AttribTypeEnum, eType), iPoints, iNewValue);
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED: Cannot add stat points to: %s", StaticDefineIntRevLookup(AttribTypeEnum, eType));
	}
}

AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pchar.Hspecies, .Pchar.Ppassignedstats, .Pchar.Hclass, .Pchar.Ilevelexp");
enumTransactionOutcome trCharacter_ModifyStatPointsInCart(ATR_ARGS,NOCONST(Entity)* ent, StatPointCart *pCart)
{
	if (pCart && eaSize(&pCart->eaItems) > 0)
	{
		S32 iNewPoints = 0;

		// Make a first pass to see if all items in the cart are valid
		FOR_EACH_IN_EARRAY_FORWARDS(pCart->eaItems, StatPointCartItem, pCartItem)
		{
			if (pCartItem == NULL)
			{
				TRANSACTION_RETURN_LOG_FAILURE("FAILED: Stat point cart passed to the transaction contains an empty cart item.");
			}
			if (pCartItem->iPoints <= 0)
			{
				TRANSACTION_RETURN_LOG_FAILURE("FAILED: Stat point cart passed to the transaction contains a cart item with non-positive points spent. Attrib name: %s, Points Spent: %d",
					StaticDefineIntRevLookup(AttribTypeEnum, pCartItem->eType), pCartItem->iPoints);
			}
		}
		FOR_EACH_END

		// Second pass to issue the stat point updates
		FOR_EACH_IN_EARRAY_FORWARDS(pCart->eaItems, StatPointCartItem, pCartItem)
		{
			StatPointPoolDef *pDef = StatPointPool_DefFromAttrib(pCartItem->eType);
			if (pDef)
			{
				if (!trhCharacter_ModifyStatPoint(ent, pDef->pchName, pCartItem->eType, pCartItem->iPoints, &iNewPoints))
				{
					TRANSACTION_RETURN_LOG_FAILURE("FAILED: Cannot add stat points to: %s", StaticDefineIntRevLookup(AttribTypeEnum, pCartItem->eType));
				}
			}
			else
			{
				TRANSACTION_RETURN_LOG_FAILURE("FAILED: Attrib '%s' in the stat point cart does not exist in any pool.", StaticDefineIntRevLookup(AttribTypeEnum, pCartItem->eType));
			}
		}
		FOR_EACH_END

		TRANSACTION_RETURN_LOG_SUCCESS("All attribs in the stat point cart are updated successfully.");
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED: Empty stat point cart is passed to the transaction.");
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
bool character_AddStatPointsEx(Entity *e, SA_PARAM_NN_STR const char *pchStatPointPoolName, ACMD_NAMELIST(AttribTypeEnum,STATICDEFINE) const char *pchAttrib, int iPoints)
{
	AttribType eAttrib = StaticDefineIntGetInt(AttribTypeEnum,pchAttrib);

	if(!character_IsValidStatPoint(eAttrib, pchStatPointPoolName))
	{
		DBGSTATS_printf("Cannot add stat points for %s",StaticDefineIntRevLookup(AttribTypeEnum,eAttrib));
		return 0;
	}

	if(iPoints > 0)
	{

		AutoTrans_trCharacter_ModifyStatPoints(
			LoggedTransactions_MakeEntReturnVal("AddStatPoints", e), GetAppGlobalType(),
			entGetType(e), entGetContainerID(e),
			pchStatPointPoolName, eAttrib, iPoints);

		return 1;
	}

	return 0;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
bool character_AddStatPoints(Entity *e, ACMD_NAMELIST(AttribTypeEnum,STATICDEFINE) const char *pchAttrib, int iPoints)
{
	return character_AddStatPointsEx(e, STAT_POINT_POOL_DEFAULT, pchAttrib, iPoints);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD;
bool character_ModifyStatPointsEx(Entity *e, SA_PARAM_NN_STR ACMD_NAMELIST("StatPointPool", REFDICTIONARY) const char *pchStatPointPoolName, ACMD_NAMELIST(AttribTypeEnum,STATICDEFINE) const char *pchAttrib, int iPoints)
{
	AttribType eAttrib = StaticDefineIntGetInt(AttribTypeEnum,pchAttrib);
	return character_ModifyStatPointsByEnum(e, pchStatPointPoolName, eAttrib, iPoints);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD;
bool character_ModifyStatPoints(Entity *e,ACMD_NAMELIST(AttribTypeEnum,STATICDEFINE) const char *pchAttrib, int iPoints)
{
	return character_ModifyStatPointsEx(e, STAT_POINT_POOL_DEFAULT, pchAttrib, iPoints);
}

static void character_ModifyStatPointsInCartCB(TransactionReturnVal *returnVal, StatPointCartUpdateCBData *pData)
{
	if (pData)
	{
		Entity* pEnt = entFromEntityRefAnyPartition(pData->erEnt);

		if (pEnt)
		{
			// Let the client know about the result of the transaction
			ClientCmd_StatPointPoolUI_ModifyStatPointsInCartCB(pEnt, returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS);
		}

		StructDestroy(parse_StatPointCartUpdateCBData, pData);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void character_ModifyStatPointsInCart(Entity *e, StatPointCart *pCart)
{
	if (e && pCart)
	{
		StatPointCartUpdateCBData *pCBData = StructCreate(parse_StatPointCartUpdateCBData);

		TransactionReturnVal *pTranReturnVal = LoggedTransactions_CreateManagedReturnValEnt("ModifyStatPointsInCart", e, character_ModifyStatPointsInCartCB, pCBData);

		pCBData->erEnt = entGetRef(e);

		AutoTrans_trCharacter_ModifyStatPointsInCart(
			pTranReturnVal, GetAppGlobalType(),
			entGetType(e), entGetContainerID(e),
			pCart);
	}
}

bool character_SetStatPointsByEnum(Entity *e, SA_PARAM_NN_STR ACMD_NAMELIST("StatPointPool", REFDICTIONARY) const char *pchStatPointPoolName, AttribType eAttrib, int iPoints)
{
	if(!character_IsValidStatPoint(eAttrib, pchStatPointPoolName))
	{
		DBGSTATS_printf("Cannot modify stat points for %s.",StaticDefineIntRevLookup(AttribTypeEnum,eAttrib));
		return 0;
	}

	AutoTrans_trCharacter_SetStatPoints(
		LoggedTransactions_MakeEntReturnVal("ModifyStatPoints", e), GetAppGlobalType(),
		entGetType(e), entGetContainerID(e),
		pchStatPointPoolName, eAttrib, iPoints);
	return 1;
}

bool character_ModifyStatPointsByEnum(Entity *e, SA_PARAM_NN_STR ACMD_NAMELIST("StatPointPool", REFDICTIONARY) const char *pchStatPointPoolName, AttribType eAttrib, int iPoints)
{
	if(!character_IsValidStatPoint(eAttrib, pchStatPointPoolName))
	{
		DBGSTATS_printf("Cannot modify stat points for %s.",StaticDefineIntRevLookup(AttribTypeEnum,eAttrib));
		return 0;
	}

	if(e->myEntityType == GLOBALTYPE_ENTITYPLAYER
		|| e->myEntityType == GLOBALTYPE_ENTITYSAVEDPET)
	{
		AutoTrans_trCharacter_ModifyStatPoints(
			LoggedTransactions_MakeEntReturnVal("ModifyStatPoints", e), GetAppGlobalType(),
			entGetType(e), entGetContainerID(e),
			pchStatPointPoolName, eAttrib, iPoints);
	}
	else if(e->myEntityType == GLOBALTYPE_ENTITYCRITTER)
	{
		NOCONST(Entity) *eNoConst = CONTAINER_NOCONST(Entity, e);

		trCharacter_ModifyStatPoints(ATR_EMPTY_ARGS, eNoConst, pchStatPointPoolName, eAttrib, iPoints);
	}

	return 1;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(StatsPreset_Save) ACMD_SERVERCMD ACMD_HIDE;
void character_SaveStatAssignments(Entity *e, const char* pchPresetName)
{
	AttribStatsPresetDef* pDef = attribstatspreset_GetDefByName( pchPresetName );
	
	if ( e && e->pChar && pDef )
	{
		S32 i, j;
		for ( i = eaSize(&e->pChar->ppAssignedStats) - 1; i >= 0; i-- )
		{
			bool bFoundDiff = false;
			for ( j = eaSize(&pDef->ppAttribStats) - 1; j >= 0; j-- )
			{
				if ( e->pChar->ppAssignedStats[i]->eType == pDef->ppAttribStats[j]->eType )
				{
					if ( e->pChar->ppAssignedStats[i]->iPoints != pDef->ppAttribStats[j]->iPoints )
					{
						bFoundDiff = true;
					}
					break;
				}
			}
			if ( bFoundDiff )
				break;
		}
		if ( i < 0 )
			return;

		AutoTrans_trCharacter_SaveStatsPresetFromAssigned(	LoggedTransactions_MakeEntReturnVal("SaveStatsPresetFromAssigned", e),
															GetAppGlobalType(),
															entGetType(e),
															entGetContainerID(e),
															pchPresetName );
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(StatsPreset_Reset) ACMD_SERVERCMD ACMD_HIDE;
void character_ResetSavedStatsPreset(Entity *e, const char* pchPresetName)
{
	AttribStatsPresetDef* pDef = attribstatspreset_GetDefByName( pchPresetName );
	
	if( e && e->pChar && pDef )
	{
		AutoTrans_trCharacter_SetStatsPresetFromSaved(	LoggedTransactions_MakeEntReturnVal("SetStatsPresetFromSaved", e), GetAppGlobalType(),
														entGetType(e), entGetContainerID(e),
														pDef, true );
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(StatsPreset_Load) ACMD_SERVERCMD ACMD_HIDE;
void character_LoadSavedStatsPreset(Entity *e, const char* pchPresetName)
{
	AttribStatsPresetDef* pDef = attribstatspreset_GetDefByName( pchPresetName );
	
	if( e && e->pChar && pDef )
	{
		AutoTrans_trCharacter_SetStatsPresetFromSaved(	LoggedTransactions_MakeEntReturnVal("SetStatsPresetFromSaved", e), GetAppGlobalType(),
														entGetType(e), entGetContainerID(e),
														pDef, false);
	}
}

// Sets Species
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pchar.Hspecies");
enumTransactionOutcome trCharacterSpeciesSet(ATR_ARGS, NOCONST(Entity) *pEnt, const char *pchSpecies)
{
	// Set to either the provided value or none
	if(pchSpecies && *pchSpecies)
		SET_HANDLE_FROM_STRING(g_hSpeciesDict, pchSpecies, pEnt->pChar->hSpecies);
	else
		REMOVE_HANDLE(pEnt->pChar->hSpecies);

	TRANSACTION_RETURN_LOG_SUCCESS("Species %s", REF_STRING_FROM_HANDLE(pEnt->pChar->hSpecies));
}

// Sets your Species
AUTO_COMMAND;
void SpeciesSet(Entity *pEnt, ACMD_NAMELIST("Species", REFDICTIONARY) const char *pchSpecies)
{
	if(pEnt && pEnt->pChar)
	{
		TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnVal("SpeciesSet", NULL, NULL);
		AutoTrans_trCharacterSpeciesSet(pReturn, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, entGetContainerID(pEnt), pchSpecies);
	}
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(SpeciesSet);
void exprFuncSpeciesSet(ExprContext* pContext, ACMD_EXPR_RES_DICT(SpeciesDef) const char* pchSpecies)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	SpeciesDef *pSpeciesDef = RefSystem_ReferentFromString(g_hSpeciesDict, pchSpecies);
	if (pEnt && pEnt->pChar && pSpeciesDef)
	{
		if (costumeLoad_ValidatePlayerCostume(pEnt->costumeRef.pStoredCostume, pSpeciesDef, false, false, false))
		{
			TransactionReturnVal *pReturn;
			ANALYSIS_ASSUME(pEnt);
			pReturn = LoggedTransactions_CreateManagedReturnVal("exprFuncSetSpecies", NULL, NULL);
			AutoTrans_trCharacterSpeciesSet(pReturn, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, entGetContainerID(pEnt), pchSpecies);
		}
	}
}

//ExpLove 

int ExpLoveTransPerFrame = 128;
bool AllowExpLove = false;
AUTO_CMD_INT(AllowExpLove,AllowExpLove) ACMD_COMMANDLINE;
AUTO_CMD_INT(ExpLoveTransPerFrame,ExpLoveTransPerFrame) ACMD_COMMANDLINE;

typedef struct ExpLoveBaton
{
	InvBagIDs BagID;
	int Exp;
	ContainerList *list;
	GlobalType server;
	ContainerIterator iter;
} ExpLoveBaton;

void gslExpLoveContinue_CB(TimedCallback *callback, F32 timeSinceLastCallback, ExpLoveBaton *baton);

//return true if finished
bool gslExpLoveDispatch(ExpLoveBaton *baton)
{
	ItemChangeReason reason = {0};
	int count = 0;
	if (!baton->list)
	{
		Container *container = NULL;
		while (container = objGetNextContainerFromIteratorEx(&baton->iter, false, true))
		{
			inv_FillItemChangeReason(&reason, NULL, "Internal", "gslExpLove");

			AutoTrans_inv_tr_ApplyNumeric(NULL,
				GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, container->containerID,
				gConf.pcLevelingNumericItem, baton->Exp, 0, &reason);

			if (++count >= ExpLoveTransPerFrame)
			{	//if there's a lot of containers, process 0.25k then wait a frame before continuing.
				//There really should be a more automated way of doing this...
				TimedCallback_Run(gslExpLoveContinue_CB, baton, 0.0f);
				return false;
			}
		}
		TimedCallback_Run(gslExpLoveContinue_CB, baton, 0.0f);
		objInitContainerIteratorFromTypeEx(GLOBALTYPE_ENTITYPLAYER, &baton->iter, false, true);
		return false;
	}
	else
	{
		while (eaiSize(&baton->list->eaiContainers) > 0) 
		{
			U32 conid = eaiPop(&baton->list->eaiContainers);

			if (baton->server == GetAppGlobalType())
			{
				inv_FillItemChangeReason(&reason, NULL, "Internal", "gslExpLove");

				AutoTrans_inv_tr_ApplyNumeric(NULL,
					GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, conid,
					gConf.pcLevelingNumericItem, baton->Exp, 0, &reason);
			}
			else if (baton->server == GLOBALTYPE_OBJECTDB)
			{
				objRequestTransactionSimplef(NULL,
					GLOBALTYPE_ENTITYPLAYER, conid, "ExpLove", 
					"set .pInventoryV2.Pplitebags[Numeric].Ppindexedliteslots[Xp].count += %d", baton->Exp);
			}
			if (++count >= ExpLoveTransPerFrame)
			{	//if there's a lot of containers, process 0.25k then wait a frame before continuing.
				//There really should be a more automated way of doing this...
				TimedCallback_Run(gslExpLoveContinue_CB, baton, 0.0f);
				break;
			}
		}
		return !(eaiSize(&baton->list->eaiContainers));
	}
}
void gslExpLoveContinue_CB(TimedCallback *callback, F32 timeSinceLastCallback, ExpLoveBaton *baton)
{
	if (gslExpLoveDispatch(baton))
	{	//if we finished dispatching, clean up the return struct.
		if (baton->list)
			StructDestroy(parse_ContainerList, baton->list);
		free(baton);
	}
}
void gslExpLove_CB(TransactionReturnVal *pReturn, void *pData)
{
	ExpLoveBaton *baton = pData;
	if (RemoteCommandCheck_dbAcquireContainers(pReturn, &baton->list) == TRANSACTION_OUTCOME_SUCCESS) {
		if (gslExpLoveDispatch(baton))
		{	//if we finished dispatching, clean up the return struct.
			if (baton->list)
				StructDestroy(parse_ContainerList, baton->list);
			free(baton);
		}
	}
}

//ExpLove <iExpvalue>: Gives everyone exp using horribly inefficient auto_transactions on the GameServer
AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void ExpLove(Entity *pClientEntity, int iValue)
{
	ItemDef *pItemDef;
	ExpLoveBaton *baton;
	GameAccountDataExtract *pExtract = NULL; // Not necessary to be a real player for this

	if (!pClientEntity->myEntityType == GLOBALTYPE_ENTITYPLAYER)
		return;

	if (!isDevelopmentMode() && !AllowExpLove)
	{
		ClientCmd_GameDialogGenericMessage(pClientEntity, "No Love", "This command is too dangerous for a production shard.");
		return;
	}

	baton = (ExpLoveBaton*)calloc(1, sizeof(ExpLoveBaton));

	pItemDef = item_DefFromName(gConf.pcLevelingNumericItem);
	baton->BagID = GetBestBagForItemDef(pClientEntity, pItemDef, -1, false, NULL);
	baton->Exp = iValue;
	baton->server = GetAppGlobalType();

	RemoteCommand_dbAcquireContainers(
		objCreateManagedReturnVal(gslExpLove_CB, baton),
		GLOBALTYPE_OBJECTDB, 0,
		objServerType(), objServerID(), GLOBALTYPE_ENTITYPLAYER);
}


//ExpLove <iExpvalue>: Gives everyone exp using stupid transactions on the ObjectDB
AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void ExpLove2(Entity *pClientEntity, int iValue)
{
	ItemDef *pItemDef;
	ExpLoveBaton *baton;
	GameAccountDataExtract *pExtract = NULL; // Not necessary to be a real value for this

	if (!pClientEntity->myEntityType == GLOBALTYPE_ENTITYPLAYER)
		return;

	if (!isDevelopmentMode() && !AllowExpLove)
	{
		ClientCmd_GameDialogGenericMessage(pClientEntity, "No Love", "This command is too dangerous for a production shard.");
		return;
	}

	baton = (ExpLoveBaton*)calloc(1, sizeof(ExpLoveBaton));

	pItemDef = item_DefFromName(gConf.pcLevelingNumericItem);
	baton->BagID = GetBestBagForItemDef(pClientEntity, pItemDef, -1, false, pExtract);
	baton->Exp = iValue;
	baton->server = GLOBALTYPE_OBJECTDB;

	RemoteCommand_dbAcquireContainers(
		objCreateManagedReturnVal(gslExpLove_CB, baton),
		GLOBALTYPE_OBJECTDB, 0,
		objServerType(), objServerID(), GLOBALTYPE_ENTITYPLAYER);
}

//ExpLove <iExpvalue>: Gives XP to all player characters on this gameserver (logged in or not).
AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void ExpLove3(Entity *pClientEntity, int iValue)
{
	ContainerStore *store = objFindContainerStoreFromType(GLOBALTYPE_ENTITYPLAYER);
	ItemDef *pItemDef;
	ExpLoveBaton *baton;
	GameAccountDataExtract *pExtract = NULL; // Not necessary to be a real value for this

	if (!pClientEntity->myEntityType == GLOBALTYPE_ENTITYPLAYER)
		return;

	if (!isDevelopmentMode() && !AllowExpLove)
	{
		ClientCmd_GameDialogGenericMessage(pClientEntity, "No Love", "This command is too dangerous for a production shard.");
		return;
	}

	baton = (ExpLoveBaton*)calloc(1, sizeof(ExpLoveBaton));

	pItemDef = item_DefFromName(gConf.pcLevelingNumericItem);
	baton->BagID = GetBestBagForItemDef(pClientEntity, pItemDef, -1, false, pExtract);
	baton->Exp = iValue;
	baton->server = GLOBALTYPE_OBJECTDB;
	baton->list = NULL;

	objInitContainerIteratorFromTypeEx(GLOBALTYPE_ENTITYPLAYER, &baton->iter, false, true);
	
	TimedCallback_Run(gslExpLoveContinue_CB, baton, 0.0f);
}

//ContainerRaider

typedef struct ContainerRaidBaton
{
	int requests;
	int successes;
	int failures;
	ContainerList *pList;
	int raidsPerFrame;
	int count;
	char *perc_id;
	F32 scaleback;
	bool stillgoing;
	TimedCallbackFunc completeCB;
	TimedCallbackFunc containerCB;
	void *userData;
} ContainerRaidBaton;

static char *running_perc_id = NULL;

int giContainerRaidsPerFrame = 80;
AUTO_CMD_INT(giContainerRaidsPerFrame, ContainerRaidsPerFrame);

//clean up the baton and inform the client.
void gslReturnContainerRaid(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData)
{
	EntityRef iRef = (EntityRef)((intptr_t)userData);
	gslSendPrintf(entFromEntityRefAnyPartition(iRef), "ContainerRaider success.");
}

void gslReturnPercolate(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData)
{
	EntityRef iRef = (EntityRef)((intptr_t)userData);
	gslSendPrintf(entFromEntityRefAnyPartition(iRef), "Percolate finished.");
}

void gslReturnRaidedContainerCB(TimedCallback *pCallback, F32 timeSinceLastCallback, BatonHolder *holder)
{
	Container *con = objGetContainer(holder->type, holder->id);
	if (holder->type == GLOBALTYPE_ENTITYPLAYER)
		gslLogOutEntity(con->containerData, 0, 0);
	else
		objRequestContainerMove(NULL, holder->type, holder->id, objServerType(), objServerID(), GLOBALTYPE_OBJECTDB, 0);

	free(holder);
}

void gslContainerRaided_CB(TransactionReturnVal *pReturn, BatonHolder *holder)
{
	ContainerRaidBaton *pBaton = holder->pBaton;
	holder->pBaton = NULL;
	if (pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		printf("Failed to fetch container: %s.\n", pReturn->pBaseReturnVals[0].returnString);
		pBaton->failures++;
		free(holder);
	}
	else
	{
		pBaton->successes++;
		if (pBaton->perc_id)
		{	//Send it back
			TimedCallback_Run(pBaton->containerCB, holder, 0.0f);
		}
	}

	if (!pBaton->stillgoing && pBaton->requests == (pBaton->successes + pBaton->failures) && !eaiSize(&pBaton->pList->eaiContainers))
	{
		EntityRef iRef = (EntityRef)((intptr_t)pBaton->userData);
		gslSendPrintf(entFromEntityRefAnyPartition(iRef), "%u containers acquired of %u requested.", pBaton->successes, pBaton->requests);

		if (pBaton->perc_id)
		{
			if(pBaton->pList->storeindex >= 0)
			{
				gslSendPrintf(entFromEntityRefAnyPartition(iRef), "PercID: %s stopped at storeindex: %u", NULL_TO_EMPTY(running_perc_id), pBaton->pList->storeindex);
				printf("PercID: %s stopped at storeindex: %u, %u completed.", NULL_TO_EMPTY(running_perc_id), pBaton->pList->storeindex, pBaton->successes);
			}
			else
			{
				gslSendPrintf(entFromEntityRefAnyPartition(iRef), "PercID: %s completed all containers.", NULL_TO_EMPTY(running_perc_id));
				printf("PercID: %s completed.", NULL_TO_EMPTY(running_perc_id));
				if (running_perc_id)
				{
					ANALYSIS_ASSUME(running_perc_id);
					free(running_perc_id);
					running_perc_id = NULL;
				}
			}
		}

		TimedCallback_Run(pBaton->completeCB, pBaton->userData, 0.0f);
		StructDestroy(parse_ContainerList, pBaton->pList);
		free(pBaton);
	}
}

void gsl_RaidContainersCB(TransactionReturnVal *pReturn, ContainerRaidBaton *pBaton);
void gsl_PercContainersCB(TransactionReturnVal *pReturn, ContainerRaidBaton *pBaton);

//return true if finished
void gslRaidContainersDispatch(TimedCallback *pCallback, F32 timeSinceLastCallback, ContainerRaidBaton *pBaton)
{
	int count = 0;

	while (pBaton->pList && eaiSize(&pBaton->pList->eaiContainers) > 0) 
	{
		BatonHolder *holder = (BatonHolder*)calloc(1, sizeof(BatonHolder));
		holder->id = eaiPop(&pBaton->pList->eaiContainers);
		holder->type = pBaton->pList->type;
		holder->pBaton = pBaton;
		pBaton->requests++;
		objRequestContainerMove(objCreateManagedReturnVal(gslContainerRaided_CB, holder), holder->type, holder->id, GLOBALTYPE_OBJECTDB, 0, objServerType(), objServerID());

		if (pBaton->perc_id && pBaton->perc_id != running_perc_id)
		{
			eaiClear(&pBaton->pList->eaiContainers);
			pBaton->pList->storeindex = -1;
			break;
		}

		if (++count >= (pBaton->raidsPerFrame ? pBaton->raidsPerFrame : giContainerRaidsPerFrame) && eaiSize(&pBaton->pList->eaiContainers) > 0)
		{	//There really should be a more automated way of doing this...
			F32 nextraid = 0.0f;
			if (pBaton->perc_id)
			{
				ContainerStore *store = objFindContainerStoreFromType(pBaton->pList->type);
				if (store)
					nextraid = store->ownedContainers * pBaton->scaleback;
			}

			TimedCallback_Run(gslRaidContainersDispatch, pBaton, nextraid);
			break;
		}
	}
		
	if (count)
	{
		verbose_printf("Acquiring ownership of %d %s containers from the ObjectDB\n", count, GlobalTypeToName(pBaton->pList->type));
		printf("%u containers percolated.           \r", pBaton->requests);
	}

	if (pBaton->pList && pBaton->perc_id && pBaton->pList->storeindex >= 0 && eaiSize(&pBaton->pList->eaiContainers) == 0)
	{	//continue percolating
		RemoteCommand_dbPercContainers(objCreateManagedReturnVal(gsl_PercContainersCB, pBaton), GLOBALTYPE_OBJECTDB, 0,
			pBaton->pList->type, pBaton->pList->storeindex, pBaton->count);
		StructDestroy(parse_ContainerList, pBaton->pList);
		pBaton->pList = NULL;
	}
}

void gsl_RaidContainersCB(TransactionReturnVal *pReturn, ContainerRaidBaton *pBaton)
{
	if (RemoteCommandCheck_dbRaidContainers(pReturn, &pBaton->pList) == TRANSACTION_OUTCOME_SUCCESS)
	{
		TimedCallback_Run(gslRaidContainersDispatch, pBaton, 0.0f);
	}
	else
	{
		StructDestroy(parse_ContainerList, pBaton->pList);
		free(pBaton);
	}
}

void gsl_PercContainersCB(TransactionReturnVal *pReturn, ContainerRaidBaton *pBaton)
{
	if (RemoteCommandCheck_dbPercContainers(pReturn, &pBaton->pList) == TRANSACTION_OUTCOME_SUCCESS)
	{
		if(pBaton->pList->storeindex >= 0)
		{
			pBaton->stillgoing = true;
		}
		else
		{
			pBaton->stillgoing = false;
		}

		TimedCallback_Run(gslRaidContainersDispatch, pBaton, 0.0f);
	}
	else
	{
		StructDestroy(parse_ContainerList, pBaton->pList);
		free(pBaton);
	}
}

void gsl_GenericContainerRaider(GlobalType eType, int div, int mod, TimedCallbackFunc completeCB, void *userData)
{
	ContainerRaidBaton *pBaton;

	pBaton = (ContainerRaidBaton *)calloc(1, sizeof(ContainerRaidBaton));
	pBaton->completeCB = completeCB;
	pBaton->userData = userData;

	RemoteCommand_dbRaidContainers(objCreateManagedReturnVal(gsl_RaidContainersCB, pBaton), GLOBALTYPE_OBJECTDB, 0, div, mod, eType);
}

void gsl_GenericPercoRaider(GlobalType eType, char *perc_id, int count, int index, int raidsPerFrame, F32 scaleback, TimedCallbackFunc completeCB, TimedCallbackFunc containerCB, void *userData)
{
	ContainerRaidBaton *pBaton;

	pBaton = (ContainerRaidBaton *)calloc(1, sizeof(ContainerRaidBaton));
	pBaton->completeCB = completeCB;
	pBaton->containerCB = containerCB;
	pBaton->userData = userData;
	pBaton->scaleback = scaleback;
	
	running_perc_id = strdup(perc_id);
	pBaton->perc_id = running_perc_id;

	pBaton->count = count;
	pBaton->raidsPerFrame = raidsPerFrame;

	RemoteCommand_dbPercContainers(objCreateManagedReturnVal(gsl_PercContainersCB, pBaton), GLOBALTYPE_OBJECTDB, 0, eType, index, count);
}

//ContainerRaider acquires all player containers whose IDs modulo div are equal to mod.
//This command is used to pull a subset of all the player containers for testing.
AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void ContainerRaider(Entity *pClientEntity, int div, int mod)
{
	if (!isDevelopmentMode() && !AllowExpLove)
	{
		ClientCmd_GameDialogGenericMessage(pClientEntity, "No Love", "This command is too dangerous for a production shard.");
		return;
	}

	if (mod >= div || mod < 0)
	{
		Errorf("%d is not within the range of modulo %d.", mod, div);
		return;
	}

	gsl_GenericContainerRaider(GLOBALTYPE_ENTITYPLAYER, div, mod, gslReturnContainerRaid, pClientEntity ? (void *)((intptr_t)entGetRef(pClientEntity)) : NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void GenericContainerRaider(Entity *pClientEntity, char *pType, int div, int mod)
{
	GlobalType eType;

	if (!isDevelopmentMode() && !AllowExpLove)
	{
		ClientCmd_GameDialogGenericMessage(pClientEntity, "No Love", "This command is too dangerous for a production shard.");
		return;
	}

	if (mod >= div || mod < 0)
	{
		Errorf("%d is not within the range of modulo %d.", mod, div);
		return;
	}

	eType = NameToGlobalType(pType);

	if (eType < 0 || eType >= GLOBALTYPE_MAXTYPES)
	{
		Errorf("Invalid global type %s requested.", pType);
	}

	gsl_GenericContainerRaider(eType, div, mod, gslReturnContainerRaid, pClientEntity ? (void *)((intptr_t)entGetRef(pClientEntity)) : NULL);
}

//Force container moves to the gameserver based on a perc id. This command will start or continue an existing percolation.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(ObjectDB);
void StartPercolator(Entity *pClientEntity, char *perc_id, char *pType, int storeoffset)
{
	GlobalType eType;

	if (running_perc_id)
	{
		ClientCmd_GameDialogGenericMessage(pClientEntity, "Can't Percolate", STACK_SPRINTF("Percolator \"%s\" is already running.", running_perc_id));
		return;
	}

	eType = NameToGlobalType(pType);

	if (eType < 0 || eType >= GLOBALTYPE_MAXTYPES)
	{
		ClientCmd_GameDialogGenericMessage(pClientEntity, "Can't Percolate", STACK_SPRINTF("Invalid global type %s requested.", pType));
		return;
	}

	gsl_GenericPercoRaider(eType, perc_id, 500, 0, 5, 0.003f, gslReturnPercolate, gslReturnRaidedContainerCB, pClientEntity ? (void *)((intptr_t)entGetRef(pClientEntity)) : NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(ObjectDB);
void StopPercolator(Entity *pClientEntity)
{
	if (running_perc_id)
	{
		ANALYSIS_ASSUME(running_perc_id);

		ClientCmd_GameDialogGenericMessage(pClientEntity, "Stopping Percolator", STACK_SPRINTF("Stopping percolator \"%s\".", running_perc_id));

		free(running_perc_id);
		running_perc_id = NULL;
		return;
	}
	else
	{
		ClientCmd_GameDialogGenericMessage(pClientEntity, "No Percolator", "No Percolator running.");
		return;
	}
}

static bool sbOldReplayMode = false;
AUTO_CMD_INT(sbOldReplayMode, OldReplayLogsMode) ACMD_COMMANDLINE;

void ReplayLogCB(const char *command, U64 sequence, U32 timestamp)
{
	U32 uType;
	U32 uID;
	const char *pStr = NULL;

	if (!sbOldReplayMode)
	{
		RemoteCommand_RemoteHandleDatabaseUpdateString(GLOBALTYPE_OBJECTDB, 0, command, sequence, timestamp);
		return;
	}

	if (!command || !command[0] || !strStartsWith(command, "dbUpdateContainer "))
		return;

	pStr = strchr_fast(command, ' ');
	pStr++;
	uType = atoi(pStr);

	pStr = strchr_fast(pStr, ' ');
	pStr++;
	uID = atoi(pStr);

	pStr = strchr_fast(pStr, ' ');
	pStr++;

	if (sbOldReplayMode == 2)
	{
		RemoteCommand_RemoteUpdateContainer(NULL, GLOBALTYPE_OBJECTDB, 0, uType, uID, pStr);
	}
	else
	{
		TransactionRequest *request = objCreateTransactionRequest();

		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, "VerifyAndSetContainer containerIDVar %s %u \"%s\"", GlobalTypeToName(uType), uID, pStr);
		objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC, NULL, "LogReplayTrans", request);
		objDestroyTransactionRequest(request);
	}
}

FileScanAction FindReplayLogsCallback(char* dir, struct _finddata32_t* data, void *pUserData)
{
	char ***eaFiles = (char***)pUserData;
	char fullPath[CRYPTIC_MAX_PATH] = "";

	if (strEndsWith(data->name, ".log") || strEndsWith(data->name, ".lcg"))
	{
		sprintf(fullPath, "%s\\%s", dir, data->name);
		eaPush(eaFiles, strdup(fullPath));
	}

	return FSA_NO_EXPLORE_DIRECTORY;
}

char **FindReplayLogs(const char *pDir)
{
	char **eaFiles = NULL;
	fileScanAllDataDirs(pDir, FindReplayLogsCallback, &eaFiles);
	return eaFiles;
}

// Replay logs to the ObjectDB!
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(OBjectDB);
void ReplayLogs(const char *pDir)
{
	char **eaFiles = FindReplayLogs(pDir);
	objSetCommandReplayCallback(ReplayLogCB);

	EARRAY_FOREACH_BEGIN(eaFiles, i);
	{
		objReplayLog(eaFiles[i]);
	}
	EARRAY_FOREACH_END;

	eaDestroyEx(&eaFiles, NULL);
}

//GiveExp <iExpValue>: Gives the character the givin amout of exp, will automaticly level up
AUTO_COMMAND ACMD_ACCESSLEVEL(5);
void GiveExp(Entity *pClientEntity,int iValue)
{
	if (pClientEntity)
	{
		ItemChangeReason reason = {0};
		inv_FillItemChangeReason(&reason, pClientEntity, "Internal:GiveExp", NULL);
		itemtransaction_AddNumeric(pClientEntity, gConf.pcLevelingNumericItem, iValue, &reason, NULL, NULL);
	}
}

//Generic command-line to give numeric rewards
AUTO_COMMAND ACMD_ACCESSLEVEL(5);
void GiveNumeric(Entity *pClientEntity, const char* numericType, int iValue)
{
	if (pClientEntity && numericType)
	{
		ItemChangeReason reason = {0};
		inv_FillItemChangeReason(&reason, pClientEntity, "Internal:GiveNumeric", NULL);
		itemtransaction_AddNumeric(pClientEntity, numericType, iValue, &reason, NULL, NULL);
	}
}

// Sets yourself to the specified level
AUTO_COMMAND ACMD_NAME(setexplevel, setlevel) ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(csr, debug);
void SetExpLevel(Entity *pClientEntity, int iLevelValue)
{
	if ( pClientEntity )
	{
		F32 iNumericValue = NUMERIC_AT_LEVEL(iLevelValue);
		ItemChangeReason reason = {0};

		inv_FillItemChangeReason(&reason, pClientEntity, "Internal:SetExpLevel", NULL);

		itemtransaction_SetNumeric(pClientEntity, gConf.pcLevelingNumericItem, iNumericValue, &reason, NULL, NULL);
	}
}

void BuyCharacterPowersUsingPath(Entity* pEntity)
{
	Entity *pFakeEntity = NULL;
	CharacterPath** eaPaths = NULL;

	// Clone the entity
	pFakeEntity = entity_CreateOwnerCopy(pEntity, pEntity, false, true, true, true, false);

	// Get the character paths
	eaStackCreate(&eaPaths, eaSize(&pFakeEntity->pChar->ppSecondaryPaths) + 1);
	entity_GetChosenCharacterPaths(pFakeEntity, &eaPaths);

	if (eaSize(&eaPaths) > 0)
	{
		// All the power purchase transactions
		PowerTreeSteps *pPowerTreeSteps = StructCreate(parse_PowerTreeSteps);

		S32 i, iPath;
		const char *pchCostTable;
		PTNodeDef *pNextSuggestedNodeDef;
		NOCONST(PTNode) *pOwnedNode = NULL;
		NOCONST(PowerTree) *pOwnedPowerTree = NULL;
		int iPartitionIdx = entGetPartitionIdx(pEntity);

		for (iPath = 0; iPath < eaSize(&eaPaths); iPath++)
		{
			// Iterate through all power tables
			for (i = 0; i < eaSize(&eaPaths[iPath]->eaSuggestedPurchases); i++)
			{
				// Get the power table name
				pchCostTable = eaPaths[iPath]->eaSuggestedPurchases[i]->pchPowerTable;

				// Buy all powers from this cost table until there is nothing left to purchase
				while(pNextSuggestedNodeDef = CharacterPath_GetNextSuggestedNodeFromCostTable(iPartitionIdx, pFakeEntity, pchCostTable, false))
				{
					// Increment the rank for this node

					// This could probably be greatly simplified by using the new PowerTreeSteps code, 
					//  but I'm not going to fiddle with it for now.

					PowerTreeDef *pTreeDef = powertree_TreeDefFromNodeDef(pNextSuggestedNodeDef);
					pOwnedNode = entity_PowerTreeNodeIncreaseRankHelper(iPartitionIdx, CONTAINER_NOCONST(Entity, pFakeEntity), NULL, pTreeDef->pchName, pNextSuggestedNodeDef->pchNameFull, false, false, false, NULL);

					if (pOwnedNode)
					{
						PowerTreeStep *pPowerTreeStep = StructCreate(parse_PowerTreeStep);
						pPowerTreeStep->pchTree = pTreeDef->pchName;
						pPowerTreeStep->pchNode = pNextSuggestedNodeDef->pchNameFull;
						pPowerTreeStep->pchEnhancement = NULL;
						pPowerTreeStep->iRank = pOwnedNode->iRank;

						// Add to the list of power tree steps
						eaPush(&pPowerTreeSteps->ppSteps, pPowerTreeStep);
					}
					else
					{
						// This should never happen
						break;
					}
				}
			}
		}

		pPowerTreeSteps->uiPowerTreeModCount = pFakeEntity->pChar->uiPowerTreeModCount;

		// Is there anything to purchase
		if (eaSize(&pPowerTreeSteps->ppSteps) > 0)
		{
			entity_PowerTreeStepsBuy(iPartitionIdx,pEntity,NULL,pPowerTreeSteps,NULL);
		}

		// Clean up
		StructDestroy(parse_PowerTreeSteps, pPowerTreeSteps);
	}

	// Clean up
	StructDestroy(parse_Entity, pFakeEntity);
}

// Called when the set level transaction is completed
static void SetExpLevelUsingCharacterPathCallback(TransactionReturnVal* returnVal, EntityRef *pEntRef)
{
	Entity *pEntity = entFromEntityRefAnyPartition(*pEntRef);

	// Make sure the entity is still accesible and the transaction was successful
	if (pEntity &&
		pEntity->pChar &&
		returnVal && returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		BuyCharacterPowersUsingPath(pEntity);
	}

	// Clean up
	free(pEntRef);
}

// Sets yourself to the specified level and buys all powers suggested by the character path
AUTO_COMMAND ACMD_NAME(SetExpLevelUsingCharacterPath, SetExpLevelCharPath) ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(csr, debug);
void SetExpLevelUsingCharacterPath(Entity *pClientEntity, int iLevelValue)
{
	if ( pClientEntity )
	{
		EntityRef *pEntRef = malloc(sizeof(EntityRef));
		F32 iNumericValue = NUMERIC_AT_LEVEL(iLevelValue);
		ItemChangeReason reason = {0};
		
		// Get the entity ref
		*pEntRef = entGetRef(pClientEntity);
		inv_FillItemChangeReason(&reason, pClientEntity, "Internal:SetExpLevelUsingCharacterPath", NULL);

		itemtransaction_SetNumeric(pClientEntity, gConf.pcLevelingNumericItem, iNumericValue, &reason, SetExpLevelUsingCharacterPathCallback, pEntRef);
	}
}

// Transaction for PowersResetIDs command.
AUTO_TRANSACTION
ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[], .Pchar.Uipoweridmax, .Pchar.Pppowerspersonal, .Pchar.Pppowersclass, .Pchar.Pppowersspecies, .Pchar.Pppowertrees, .pInventoryV2.Ppinventorybags, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppallowedcritterpets, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]")
ATR_LOCKS(ppOwned, "pInventoryV2.ppLiteBags[], .Pchar.Uipoweridmax, .Pchar.Pppowerspersonal, .Pchar.Pppowersclass, .Pchar.Pppowersspecies, .Pchar.Pppowertrees, .pInventoryV2.Ppinventorybags, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppallowedcritterpets, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome trEntity_PowersResetIDs(ATR_ARGS,
											   NOCONST(Entity)* pEnt,
											   CONST_EARRAY_OF(NOCONST(Entity)) ppOwned,
											   GameAccountDataExtract *pExtract)
{
	int i;
	entity_ResetPowerIDsAllHelper(ATR_PASS_ARGS, pEnt, pExtract);
	for(i=eaSize(&ppOwned)-1; i>=0; i--)
	{
		entity_ResetPowerIDsAllHelper(ATR_PASS_ARGS, ppOwned[i], pExtract);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Completely resets all PowerIDs on the Entity and all of its OwnedContainers.  Useful if an Entity gets
//  its PowerIDs into a bad state.  Generally should not be run on an online Entity.
AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(csr, debug, powers);
void PowersResetIDs(Entity *pClientEntity)
{
	if(pClientEntity)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pClientEntity);
		U32 *pcidOwnedContainers = entity_GetOwnedContainerIDsArray(pClientEntity);
		AutoTrans_trEntity_PowersResetIDs(LoggedTransactions_MakeReturnVal("PowersResetIDs"), GLOBALTYPE_GAMESERVER, 
			pClientEntity->myEntityType, pClientEntity->myContainerID, 
			GLOBALTYPE_ENTITYSAVEDPET, &pcidOwnedContainers,
			pExtract);
		ea32Destroy(&pcidOwnedContainers);
	}
}

//Buy_PowerTree <PowerTree>: If allowed, adds the specific power tree to the character though the transaction server
AUTO_COMMAND;
void Buy_PowerTree(Entity *pClientEntity, ACMD_NAMELIST("PowerTreeDef", REFDICTIONARY) const char *pchTree)
{
	if(pClientEntity && pClientEntity->pChar)
	{
		PowerTreeDef *pTreeDef;
		pTreeDef = powertreedef_Find(pchTree);

		if(pTreeDef && character_CanBuyPowerTree(entGetPartitionIdx(pClientEntity),pClientEntity->pChar,pTreeDef))
		{
			objRequestTransactionSimplef(NULL,entGetType(pClientEntity), entGetContainerID(pClientEntity),
				"BuyPowerTree", "create pChar.ppPowerTrees[%s]",pchTree);
		}
		else if(pTreeDef)
		{
			DBGPOWERTREE_printf("Failed to purchase %s Power Tree",pchTree);
		}
		else
		{
			DBGPOWERTREE_printf("%s Power Tree does not exist",pchTree);
		}
	}
}

//Access level 9 command to add a power tree to a player with out having to follow the rules of purchasing it
AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void Add_PowerTree(Entity *pClientEntity, ACMD_NAMELIST("PowerTreeDef", REFDICTIONARY) const char *pchTree)
{
	if(pClientEntity && pClientEntity->pChar)
	{
		PowerTreeDef *pTreeDef;

		pTreeDef = powertreedef_Find(pchTree);

		if(pTreeDef)
		{
			objRequestTransactionSimplef(NULL,entGetType(pClientEntity), entGetContainerID(pClientEntity),
				"AddPowerTree", "create pChar.ppPowerTrees[%s]",pchTree);
		}
		else
		{
			DBGPOWERTREE_printf("%s Power Tree does not exist",pchTree);
		}
	}
}

// Returns true is the player is interacting with a powers trainer (if the game requires trainers)
// or if they are level nine. 
static bool CharacterCanContactTrainer(Player* pPlayer)
{
	ContactDialog *pContactDialog = SAFE_MEMBER2(pPlayer, pInteractInfo, pContactDialog);
	if (pPlayer && pPlayer->accessLevel >= ACCESS_GM)
	{
		return true;
	}
	else if (pContactDialog)
	{
		return gConf.bRequirePowerTrainer ? pContactDialog->state == ContactDialogState_PowersTrainer : true;	
	}
	return gConf.bPlayerCanTrainPowersAnywhere;
	/*
		Okay, so, you may be wondering why there are TWO gConf options that control when the player can train powers.
		Well, bRequirePowerTrainer is set to true for champs and false for STO, and I couldn't get a clear answer from anybody
		about the ramifications of allowing STO characters to train powers without a contact dialogue open... so I added the second
		flag just to be extra careful that I didn't royally screw STO. If you have any question, just ask. - cmiller
	*/
}

//Buy_PowerTreeNode <PowerTree> <Node>: Purchases the Node in the PowerTree
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD;
void Buy_PowerTreeNode(Entity *e, const char *pchTree, const char *pchNode)
{
	if(e && e->pChar)
	{
		if (CharacterCanContactTrainer(e->pPlayer))
		{
			entity_PowerTreeNodeInceaseRank(entGetPartitionIdx(e), e, pchTree, pchNode);

			// check for locking in of purchase
			// due to order of purchase might require a call back or some such
			if(gPowerConfig.bLockPowersIfNotinPowerhouse)
			{
				const char *pcCurrentMap = zmapInfoGetPublicName(NULL);
				ZoneMapInfo* pCurrZoneMap = worldGetZoneMapByPublicName(pcCurrentMap);

				// lock powers as player is not in power house
				if(pCurrZoneMap && !zmapInfoConfirmPurchasesOnExit(pCurrZoneMap))
				{
					// lock in player powers, used by UI when not in power house
					AutoTrans_trCharacter_UpdatePointsSpentPowerTrees(LoggedTransactions_MakeEntReturnVal("UpdatePointsSpentPowerTrees", e), GetAppGlobalType(), entGetType(e), entGetContainerID(e));
				}
			}

		}
	}
}

//Enhance_PowerTreeNode <PowerTree> <Node> <Enhancement> <1/0: Increment/Decrement>
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE;
void Enhance_PowerTreeNode(Entity *e, const char *pchTree, const char *pchNode, const char *pchEnhancement, int bAdd)
{
	if(e && e->pChar)
	{
		// Right now only adds are supported through this function, removes are handled through respec
		if(CharacterCanContactTrainer(e->pPlayer) && bAdd)
		{
			entity_PowerTreeNodeEnhance(entGetPartitionIdx(e),e,pchTree,pchNode,pchEnhancement);
		}
	}
}

//Power_Slot <SlotIndex> <PowerID>: Puts a Power into a PowerSlot; a PowerID of 0 means empty.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void Power_Slot(Entity *e, int iSlot, U32 uiIDPower)
{
	if(e && e->pChar && character_CanModifyPowerTray(e->pChar, -1, iSlot, true))
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		character_PowerTraySlotSet(entGetPartitionIdx(e), e->pChar, -1, iSlot, uiIDPower, true, pExtract, false, true);
	}
}

//Power_Slot <SlotIndexA> <SlotIndexB>: Swaps the Powers in the PowerSlots
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void Power_SlotSwap(Entity *e, int iSlotA, int iSlotB)
{
	if(e && e->pChar) //swaps aren't blocked by powermodes/cooldowns.
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		character_PowerTraySlotSwap(entGetPartitionIdx(e),e->pChar,-1,iSlotA,-1,iSlotB,pExtract);
	}
}

//PowerTray_Slot <TrayIndex> <SlotIndex> <PowerID>: Puts a Power into a PowerSlot; a PowerID of 0 means empty.
// NOTE: Tray index of -1 means current
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void PowerTray_Slot(Entity *e, int iTray, int iSlot, U32 uiIDPower, bool bUnslot)
{
	if(e && e->pChar && character_CanModifyPowerTray(e->pChar, iTray, iSlot, true))
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		character_PowerTraySlotSet(entGetPartitionIdx(e),e->pChar,iTray,iSlot,uiIDPower, bUnslot, pExtract, false, true);
	}
}

//PowerTray_Slot <TrayIndex> <SlotIndex> <PowerNodeFullName>: Puts a Power into a PowerSlot.
// NOTE: Tray index of -1 means current
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void PowerTray_SlotNode(Entity *e, int iTray, int iSlot, const char* pchNodeFullName, bool bUnslot)
{
	PTNodeDef *pNodeDef;
	PTGroupDef *pGroupDef;
	char* pchParsed = NULL;
	char* pchTreeName = NULL;
	char* pchGroupName = NULL;

	if (e && e->pChar && !character_CanModifyPowerTray(e->pChar, iTray, iSlot, true))
	{
		return;
	}

	estrStackCreate(&pchParsed);
	estrCopy2(&pchParsed, pchNodeFullName);
	pchTreeName = pchParsed;
	pchGroupName = strchr(pchParsed, '.');
	if (pchGroupName)
	{
		*pchGroupName = '\0';
	}
	pNodeDef = powertreenodedef_Find(pchNodeFullName);
	pGroupDef = powertree_GroupDefFromNodeDef(pNodeDef);

	if(e && e->pChar && pNodeDef && pGroupDef)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		character_PowerTraySlotSetNode(entGetPartitionIdx(e),e->pChar,iTray,iSlot,pNodeDef,pGroupDef,bUnslot,pExtract,false);
	}
	estrDestroy(&pchParsed);
}

//PowerTray_SlotSwap <TrayIndexA> <SlotIndexA> <TrayIndexB> <SlotIndexB>: Swaps the Powers in the PowerSlots across trays.
// NOTE: Tray index of -1 means current
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void PowerTray_SlotSwap(Entity *e, int iTrayA, int iSlotA, int iTrayB, int iSlotB)
{
	//prevent swap if they are on different trays and powermodes/cooldowns block either, but allow it if they are on the same tray.
	if(e && e->pChar 
	&& (iTrayA == iTrayB || (character_CanModifyPowerTray(e->pChar, iTrayA, iSlotA, true) && character_CanModifyPowerTray(e->pChar, iTrayB, iSlotB, true))))
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		character_PowerTraySlotSwap(entGetPartitionIdx(e),e->pChar,iTrayA,iSlotA,iTrayB,iSlotB,pExtract);
	}
}

//PowerSlots_ForceSetCurrent <Index>: Switches the currently active set of PowerSlots
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Powers) ACMD_SERVERCMD;
void PowerSlots_ForceSetCurrent(Entity *e, U32 uiIndex)
{
	if(e && e->pChar && character_CanModifyPowerTray(e->pChar, -1, -1, true))
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		character_PowerSlotSetCurrent(e->pChar,uiIndex);
		character_RefreshPassives(entGetPartitionIdx(e),e->pChar,pExtract);
	}
}

// Hack to set your player kind temporarily
// TODO(JW): Hack: Remove this and do real PvP targeting
// Disabled; doesn't drop player from current team correctly
AUTO_COMMAND ACMD_NAME("Player.SetFactionHack") ACMD_ACCESSLEVEL(7);
void PlayerSetFactionHack(Entity *pClientEntity, ACMD_NAMELIST("CritterFaction", REFDICTIONARY) char *pchFaction)
{
	if(pClientEntity && pClientEntity->pPlayer)
	{
		if (RefSystem_ReferentFromString("CritterFaction", pchFaction))
		{
			gslEntity_SetFactionOverrideByName(pClientEntity, kFactionOverrideType_DEFAULT, pchFaction);
		}
	}
}
AUTO_COMMAND ACMD_NAME("Player.ResetFaction") ACMD_ACCESSLEVEL(7);
void PlayerResetFaction(Entity *pClientEntity)
{
	if(pClientEntity && pClientEntity->pPlayer)
	{
		if(IS_HANDLE_ACTIVE(pClientEntity->hFactionOverride))
		{
			gslEntity_ClearFaction(pClientEntity, kFactionOverrideType_DEFAULT);
		}
	}
}

// Resets your movement state
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(csr,debug) ACMD_HIDE;
void MovementReset(CmdContext *pContext, Entity *pClientEntity)
{
#define MOVEMENTRESETTIMER 300
	if(pClientEntity && pClientEntity->pChar && pClientEntity->pPlayer)
	{
		U32 uiTimeNow = timeSecondsSince2000();
		if(pContext->access_level > 0 || pClientEntity->pPlayer->uiTimeMovementReset + MOVEMENTRESETTIMER < uiTimeNow)
		{
			if(pContext->access_level==0)
				pClientEntity->pPlayer->uiTimeMovementReset = uiTimeNow;
			character_MovementReset(pClientEntity->pChar);
		}
		else
		{
			// Inform the client that they're not allowed to use this yet
			entFormatGameMessageKey(pClientEntity, pContext->output_msg, "Cmd_MovementReset", STRFMT_INT("Time",pClientEntity->pPlayer->uiTimeMovementReset+MOVEMENTRESETTIMER-uiTimeNow), STRFMT_END);
		}
	}
}



// Manual attempt to cancel all current activations
AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void PowersCancelAllActivations(Entity *e)
{
	if(e && e->pChar)
	{
		character_ActAllCancel(entGetPartitionIdx(e),e->pChar,false);
	}
}




bool OVERRIDE_LATELINK_im_EntityCleanupCheck(Entity *pEnt)
{
	Character *pChar = pEnt->pChar;

	if(pChar->pattrBasic->fHitPoints < pChar->pattrBasic->fHitPointsMax)
		return true;

	return false;
}

bool OVERRIDE_LATELINK_im_EntityInCombatCheck(Entity *pEnt)
{
	Character *pChar = pEnt->pChar;

	if(pChar->uiTimeCombatExit)
		return true;

	return false;
}

//-----------------Auto-Level Functions-------------------//

static int SortAutoLevelNodes(const AutoLevelNodeData** ppA, const AutoLevelNodeData** ppB)
{
	const AutoLevelNodeData* pA = (*ppA);
	const AutoLevelNodeData* pB = (*ppB);
	return pA->iRandomIndex - pB->iRandomIndex;
}

static void AutoLevel_BuildRandomNodeSteps(int iPartitionIdx, Entity* pEntity, PowerTreeSteps* pPowerTreeSteps)
{
	if (pEntity && pEntity->pChar && pPowerTreeSteps)
	{
		int i, j, k;
		Entity* pFakeEnt = entity_CreateOwnerCopy(pEntity, pEntity, true, true, true, false, false);
		AutoLevelNodeData** ppData = NULL;

		for (i = eaSize(&pFakeEnt->pChar->ppPowerTrees)-1; i >= 0; i--)
		{
			PowerTree* pTree = pFakeEnt->pChar->ppPowerTrees[i];
			PowerTreeDef* pTreeDef = GET_REF(pTree->hDef);
			if (pTreeDef)
			{
				for (j = eaSize(&pTreeDef->ppGroups)-1; j >= 0; j--)
				{
					PTGroupDef* pGroupDef = pTreeDef->ppGroups[j];
					for (k = eaSize(&pGroupDef->ppNodes)-1; k >= 0; k--)
					{
						PTNodeDef* pNodeDef = pGroupDef->ppNodes[k];
						PTNode* pNode = (PTNode*)entity_FindPowerTreeNodeHelper(CONTAINER_NOCONST(Entity, pFakeEnt), pNodeDef);
						int iRank = (pNode && !pNode->bEscrow) ? pNode->iRank : -1;
						if (iRank+1 < eaSize(&pNodeDef->ppRanks))
						{
							AutoLevelNodeData* pData = StructCreate(parse_AutoLevelNodeData);
							pData->pNodeDef = pNodeDef;
							pData->pTreeDef = pTreeDef;
							pData->iRank = (pNode && !pNode->bEscrow) ? pNode->iRank : -1;
							pData->iRandomIndex = randomInt();
							eaPush(&ppData, pData);
						}
					}
				}
			}
		}
		// Randomize the order of nodes in the list
		eaQSort(ppData, SortAutoLevelNodes);

		for (i = eaSize(&ppData)-1; i >= 0; i--)
		{
			bool bAdded = false;
			AutoLevelNodeData* pData = ppData[i];
			PTNodeDef* pNodeDef = pData->pNodeDef;
			PowerTreeDef* pTreeDef = pData->pTreeDef;
			
			if (!pNodeDef || !pTreeDef)
			{
				continue;
			}
			
			// This could probably be greatly simplified by using the new PowerTreeSteps code, 
			//  but I'm not going to fiddle with it for now.
			
			while (pData->iRank+1 < eaSize(&pNodeDef->ppRanks))
			{
				if (!entity_PowerTreeNodeIncreaseRankHelper(iPartitionIdx,
															CONTAINER_NOCONST(Entity, pFakeEnt),
															NULL,
															pTreeDef->pchName,
															pNodeDef->pchNameFull,
															false,
															false,
															false,
															NULL))
				{
					break;
				}
				bAdded = true;
				pData->iRank++;
			}
			if (bAdded)
			{
				PowerTreeStep *pPowerTreeStep = StructCreate(parse_PowerTreeStep);
				pPowerTreeStep->pchTree = pTreeDef->pchName;
				pPowerTreeStep->pchNode = pNodeDef->pchNameFull;
				pPowerTreeStep->pchEnhancement = NULL;
				pPowerTreeStep->iRank = pData->iRank;

				// Add to the list of power tree steps
				eaPush(&pPowerTreeSteps->ppSteps, pPowerTreeStep);
				
				// Remove the node from the list if it is now max rank
				if (pData->iRank+1 >= eaSize(&pNodeDef->ppRanks))
				{
					eaRemove(&ppData, i);
				}
				// Start at the back of the list to consider nodes that were previously unable to be purchased
				i = eaSize(&ppData)-1;
			}
		}

		pPowerTreeSteps->uiPowerTreeModCount = pEntity->pChar->uiPowerTreeModCount;
		StructDestroy(parse_Entity, pFakeEnt);
		eaDestroyStruct(&ppData, parse_AutoLevelNodeData);
	}
}

static void AutoLevelCallback(TransactionReturnVal* returnVal, EntityRef* per)
{
	Entity *e = entFromEntityRefAnyPartition(*per);
	if(e)
	{
		PowerTreeSteps* pSteps = StructCreate(parse_PowerTreeSteps);

		AutoLevel_BuildRandomNodeSteps(entGetPartitionIdx(e), e, pSteps);
		entity_PowerTreeStepsBuy(entGetPartitionIdx(e),e,NULL,pSteps,NULL);

		StructDestroy(parse_PowerTreeSteps,pSteps);
	}
	free(per);
}

//iValue is the rel 1 level
AUTO_COMMAND;
void AutoLevel(Entity *pClientEntity, int iLevelValue)
{
	if ( pClientEntity )
	{
		F32 iNumericValue = NUMERIC_AT_LEVEL(iLevelValue);
		EntityRef *per = calloc(1,sizeof(EntityRef));
		ItemChangeReason reason = {0};

		*per = entGetRef(pClientEntity);
		inv_FillItemChangeReason(&reason, pClientEntity, "Internal:AutoLevel", NULL);

		itemtransaction_SetNumeric(pClientEntity, gConf.pcLevelingNumericItem, iNumericValue, &reason, AutoLevelCallback, per);
	}
}

static void ResetSpentPointsInTree_CB(TransactionReturnVal* pReturn, void* pData)
{
	Entity* pEnt = entFromEntityRefAnyPartition((intptr_t)pData);
	if (pEnt && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		entity_PowerTreeAutoBuyEx(entGetPartitionIdx(pEnt), pEnt, NULL, false);
	}
}

AUTO_COMMAND;
void ResetSpentPointsInTree(Entity *pEnt, const char* pchPowerTreeName)
{
	if (pEnt->pChar && character_FindTreeByDefName(pEnt->pChar, pchPowerTreeName))
	{
		ItemChangeReason reason = {0};
		TransactionReturnVal* pReturn;
		pReturn = LoggedTransactions_CreateManagedReturnValEnt("ResetSpentPointsInTree", pEnt, ResetSpentPointsInTree_CB, (void*)((intptr_t)(entGetRef(pEnt))));

		inv_FillItemChangeReason(&reason, pEnt, "Internal:ResetSpentPointsInTree", NULL);
		AutoTrans_trCharacter_ResetPointsSpentPowerTree(pReturn, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pchPowerTreeName, &reason);
	}
}

AUTO_COMMAND;
void ResetSpentPointsInTrees(Entity *pEnt)
{
	if (pEnt->pChar)
	{
		TransactionReturnVal* pReturn;
		ItemChangeReason reason = {0};
		int i;

		inv_FillItemChangeReason(&reason, pEnt, "Internal:ResetSpentPointsInTrees", NULL);
		
		for (i = eaSize(&pEnt->pChar->ppPowerTrees)-1; i >= 0; i--)
		{
			PowerTree* pTree = pEnt->pChar->ppPowerTrees[i];
			const char* pchPowerTreeName = REF_STRING_FROM_HANDLE(pTree->hDef);
			pReturn = LoggedTransactions_CreateManagedReturnValEnt("ResetSpentPointsInTrees", pEnt, ResetSpentPointsInTree_CB, (void*)((intptr_t)(entGetRef(pEnt))));
			AutoTrans_trCharacter_ResetPointsSpentPowerTree(pReturn, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pchPowerTreeName, &reason);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void MoveStatPointsAroundAdd(SA_PARAM_NN_VALID Entity *pEnt, int LockWeaponPower, int LockEnginePower, int LockShieldPower, int LockAuxiliaryPower, int LockStrength, int LockAgility, int interval, int newvalue, int max, int min, const char* TargetKey)
{
	bool locks[6], modified[6];
	S32 Stats[] = {offsetof(CharacterAttribs,fDataDefined01), offsetof(CharacterAttribs,fDataDefined02), offsetof(CharacterAttribs,fDataDefined03), offsetof(CharacterAttribs,fDataDefined04), kAttribType_StatStrength, kAttribType_StatAgility};
	F32 StatValues[6];
	F32 StatOriginalValues[6];
	S32 index = StaticDefineIntGetInt(AttribTypeEnum, TargetKey);
	S32 HighestStatIndex = -1;
	F32	HighestStatValue = -1.0f;
	int i, j, iUnlockedCount = 0, iModifiedCount = 0;
	bool done = false;
	if (index < 0)
		return;
	locks[0] = !!LockWeaponPower;
	locks[1] = !!LockShieldPower;
	locks[2] = !!LockEnginePower;
	locks[3] = !!LockAuxiliaryPower;
	locks[4] = !!LockStrength;
	locks[5] = !!LockAgility;

	for (i = 0; i < 6; ++i)
	{
		if (Stats[i] == index)
		{
			index = i;
			break;
		}
	}

	if(locks[index])
		return;

	//getting the to calculate what to change things, because it's going to be a lot faster to only have to do 1 set of transactions, as opposed to recursing
	for (i = 0; i < 6; ++i)
	{
		//StatValues[i] = StatOriginalValues[i] = *F32PTR_OF_ATTRIB(pEnt->pChar->pattrBasic,Stats[i]);
		
		StatValues[i] = StatOriginalValues[i] = 0;
		if (!locks[i]) iUnlockedCount++;
		modified[i] = false;

		for(j=0;j<eaSize(&pEnt->pChar->ppAssignedStats);j++)
		{
			if(pEnt->pChar->ppAssignedStats[j]->eType == Stats[i])
			{
				StatValues[i] = StatOriginalValues[i] = pEnt->pChar->ppAssignedStats[j]->iPoints;
				break;
			}
		}
	}

	--iUnlockedCount;
	if (iUnlockedCount <= 0)
		return;

	if (newvalue - interval < StatOriginalValues[index])
		return;

	while (!done)
	{
		// have to reset the index and max
		HighestStatIndex = -1;
		HighestStatValue = -1.0f;
		for (i = 0; i < 6; ++i)
		{
			if (!locks[i])
			{
				if ( (i != index) && (HighestStatValue < StatValues[i] || HighestStatIndex == -1) && (!modified[i]) )
				{
					HighestStatIndex = i;
					HighestStatValue = StatValues[i];
				}
			}
		}
		if ((HighestStatIndex != -1) && (HighestStatValue - interval >= min) && (StatValues[index] + interval <= max))
		{
			StatValues[index] += interval;
			StatValues[HighestStatIndex] -= interval;
			modified[HighestStatIndex] = true;
			if (++iModifiedCount >= iUnlockedCount)
			{
				for (j = 0; j < 6; ++j) modified[j] = false;
				iModifiedCount = 0;
			}
		}
		else
		{
			done = true;
		}
		if (newvalue <= StatValues[index])
		{
			done = true;
		}
	}

	//do the subtractions first
	for (i = 0; i < 6; ++i)
	{
		if (!locks[i])
		{
			if (StatOriginalValues[i] > StatValues[i])
			{
				character_SetStatPointsByEnum(pEnt, STAT_POINT_POOL_DEFAULT, Stats[i], StatValues[i]);
			}
		}
	}
	//then do the additions
	for (i = 0; i < 6; ++i)
	{
		if (!locks[i])
		{
			if (StatOriginalValues[i] < StatValues[i])
			{
				character_SetStatPointsByEnum(pEnt, STAT_POINT_POOL_DEFAULT, Stats[i], StatValues[i]);
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void MoveStatPointsAroundSubtract(SA_PARAM_NN_VALID Entity *pEnt, int LockWeaponPower, int LockEnginePower, int LockShieldPower, int LockAuxiliaryPower, int LockStrength, int LockAgility, int interval, int newvalue, int max, int min, const char* TargetKey)
{
	bool locks[6], modified[6];
	S32 Stats[] = {offsetof(CharacterAttribs,fDataDefined01), offsetof(CharacterAttribs,fDataDefined02), offsetof(CharacterAttribs,fDataDefined03), offsetof(CharacterAttribs,fDataDefined04), kAttribType_StatStrength, kAttribType_StatAgility};
	F32 StatValues[6];
	F32 StatOriginalValues[6];
	S32 index = StaticDefineIntGetInt(AttribTypeEnum, TargetKey);
	S32 LowestStatIndex = -1;
	F32	LowestStatValue = 9999999999.0f; // arbitrary very large number
	int i, j, iUnlockedCount = 0, iModifiedCount = 0;
	bool done = false;
	if (index < 0)
		return;
	locks[0] = !!LockWeaponPower;
	locks[1] = !!LockShieldPower;
	locks[2] = !!LockEnginePower;
	locks[3] = !!LockAuxiliaryPower;
	locks[4] = !!LockStrength;
	locks[5] = !!LockAgility;

	for (i = 0; i < 6; ++i)
	{
		if (Stats[i] == index)
		{
			index = i;
			break;
		}
	}

	if(locks[index])
		return;

	//getting the to calculate what to change things, because it's going to be a lot faster to only have to do 1 set of transactions, as opposed to recursing
	for (i = 0; i < 6; ++i)
	{
		//StatValues[i] = StatOriginalValues[i] = *F32PTR_OF_ATTRIB(pEnt->pChar->pattrBasic,Stats[i]);

		StatValues[i] = StatOriginalValues[i] = 0;
		if (!locks[i]) iUnlockedCount++;
		modified[i] = false;

		for(j=0;j<eaSize(&pEnt->pChar->ppAssignedStats);j++)
		{
			if(pEnt->pChar->ppAssignedStats[j]->eType == Stats[i])
			{
				StatValues[i] = StatOriginalValues[i] = pEnt->pChar->ppAssignedStats[j]->iPoints;
				break;
			}
		}
	}

	--iUnlockedCount;
	if (iUnlockedCount <= 0)
		return;

	if (newvalue + interval > StatOriginalValues[index])
		return;

	while (!done)
	{
		// have to reset the index and max
		LowestStatIndex = -1;
		LowestStatValue = 9999999999.0f;
		for (i = 0; i < 6; ++i)
		{
			if (!locks[i])
			{
				if ( (i != index) && (LowestStatValue > StatValues[i] || LowestStatIndex == -1) && (!modified[i]) )
				{
					LowestStatIndex = i;
					LowestStatValue = StatValues[i];
				}
			}
		}
		if ((LowestStatIndex != -1) && (LowestStatValue + interval <= max) && (StatValues[index] - interval >= min))
		{
			StatValues[index] -= interval;
			StatValues[LowestStatIndex] += interval;
			newvalue += interval;
			modified[LowestStatIndex] = true;
			if (++iModifiedCount >= iUnlockedCount)
			{
				for (j = 0; j < 6; ++j) modified[j] = false;
				iModifiedCount = 0;
			}
		}
		else
		{
			done = true;
		}
		if (newvalue + interval > StatValues[index])
		{
			done = true;
		}
	}

	//do the subtractions first
	for (i = 0; i < 6; ++i)
	{
		if (!locks[i])
		{
			if (StatOriginalValues[i] > StatValues[i])
			{
				character_SetStatPointsByEnum(pEnt, STAT_POINT_POOL_DEFAULT, Stats[i], StatValues[i]);
			}
		}
	}
	//then do the additions
	for (i = 0; i < 6; ++i)
	{
		if (!locks[i])
		{
			if (StatOriginalValues[i] < StatValues[i])
			{
				character_SetStatPointsByEnum(pEnt, STAT_POINT_POOL_DEFAULT, Stats[i], StatValues[i]);
			}
		}
	}
}

