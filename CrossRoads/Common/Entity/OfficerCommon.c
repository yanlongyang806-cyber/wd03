/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "OfficerCommon.h"

#include "AutoTransDefs.h"
#include "allegiance.h"
#include "Character.h"
#include "contact_common.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "error.h"
#include "Expression.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "GamePermissionsCommon.h"
#include "inventoryCommon.h"
#include "microtransactions_common.h"
#include "mission_common.h"
#include "Player.h"
#include "PowerModes.h"
#include "Powers.h"
#include "PowerStoreCommon.h"
#include "PowerTree.h"
#include "PowerTreeHelpers.h"
#include "SavedPetCommon.h"
#include "StringFormat.h"
#include "textparser.h"
#include "tradeCommon.h"

#include "AutoGen/OfficerCommon_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/EntitySavedData_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

OfficerRankStruct g_OfficerRankStruct;


bool Officer_ValidateSkills(PetDef* pDef)
{
	bool bSuccess = true;
	S32 i, iMinPurpose = MAX(kPowerPurpose_Uncategorized+1,0);
	S32* piHasPurposeProp = NULL;
	S32* piHasPurpose = NULL;
	AllegianceDef* pAllegiance = GET_REF(pDef->hAllegiance);

	PERFINFO_AUTO_START_FUNC();

	for (i = eaSize(&pDef->ppEscrowPowers)-1; i >= 0; i--)
	{
		PTNodeDef* pNodeDef = GET_REF(pDef->ppEscrowPowers[i]->hNodeDef);
		PowerDef* pPowerDef = pNodeDef && eaSize(&pNodeDef->ppRanks) ? GET_REF(pNodeDef->ppRanks[0]->hPowerDef) : NULL;

		if (pPowerDef)
		{
			if (pNodeDef->ePurpose != pPowerDef->ePurpose)
			{
				S32 iNodeRank = pNodeDef->ePurpose-iMinPurpose;
				OfficerRankDef* pNodeRank = Officer_GetRankDef(iNodeRank, pAllegiance, NULL);
				const char* pchNodeRank = pNodeRank ? pNodeRank->pchName : "";
				S32 iPowerRank = pPowerDef->ePurpose-iMinPurpose;
				OfficerRankDef* pPowerRank = Officer_GetRankDef(iPowerRank, pAllegiance, NULL);
				const char* pchPowerRank = pPowerRank ? pPowerRank->pchName : "";
				ErrorFilenamef(pDef->pchFilename, "Officer item (%s) has mismatched ranks on NodeDef(%s)[R%d,%s] and PowerDef(%s)[R%d,%s]", 
					pDef->pchPetName, pNodeDef->pchName, iNodeRank, pchNodeRank, pPowerDef->pchName, iPowerRank, pchPowerRank);
				bSuccess = false;
			}
			else if (pPowerDef->ePurpose >= iMinPurpose)
			{
				S32 iConvertedPurpose = pPowerDef->ePurpose-iMinPurpose;
				if (pPowerDef->powerProp.bPropPower)
				{
					if (ea32Get(&piHasPurposeProp, iConvertedPurpose)!=0)
					{
						OfficerRankDef* pPowerRank = Officer_GetRankDef(iConvertedPurpose, pAllegiance, NULL);
						const char* pchPowerRank = pPowerRank ? pPowerRank->pchName : "";
						ErrorFilenamef(pDef->pchFilename, "Officer item (%s) has more than one propagated node (duplicate is %s) for rank [R%d,%s]", 
							pDef->pchPetName, pNodeDef->pchName, iConvertedPurpose, pchPowerRank);
						bSuccess = false;
					}
					else
					{
						ea32Set(&piHasPurposeProp, 1, iConvertedPurpose);
					}
				}
				else 
				{
					if (ea32Get(&piHasPurpose, iConvertedPurpose)!=0)
					{
						OfficerRankDef* pPowerRank = Officer_GetRankDef(iConvertedPurpose, pAllegiance, NULL);
						const char* pchPowerRank = pPowerRank ? pPowerRank->pchName : "";
						ErrorFilenamef(pDef->pchFilename, "Officer item (%s) has more than one non-propagated node (duplicate is %s) for rank [R%d,%s]", 
							pDef->pchPetName, pNodeDef->pchName, iConvertedPurpose, pchPowerRank);
						bSuccess = false;
					}
					else
					{
						ea32Set(&piHasPurpose, 1, iConvertedPurpose);
					}
				}
			}
		}
	}
	for (i = ea32Size(&piHasPurposeProp)-1; i >= 0; i--)
	{
		if (piHasPurposeProp[i]==0)
		{
			OfficerRankDef* pPowerRank = Officer_GetRankDef(i, pAllegiance, NULL);
			const char* pchPowerRank = pPowerRank ? pPowerRank->pchName : "";
			ErrorFilenamef(pDef->pchFilename, "Officer item (%s) doesn't have a propagated node specified for rank [R%d,%s]", 
				pDef->pchPetName, i, pchPowerRank);
			bSuccess = false;
		}
	}
	for (i = ea32Size(&piHasPurpose)-1; i >= 0; i--)
	{
		if (piHasPurpose[i]==0)
		{
			OfficerRankDef* pPowerRank = Officer_GetRankDef(i, pAllegiance, NULL);
			const char* pchPowerRank = pPowerRank ? pPowerRank->pchName : "";
			ErrorFilenamef(pDef->pchFilename, "Officer item (%s) doesn't have a non-propagated node specified for rank [R%d,%s]", 
				pDef->pchPetName, i, pchPowerRank);
			bSuccess = false;
		}
	}
	ea32Destroy(&piHasPurposeProp);
	ea32Destroy(&piHasPurpose);
	PERFINFO_AUTO_STOP();
	return bSuccess;
}

OfficerRankDef* Officer_GetRankDefUsingNumeric(S32 iRank, AllegianceDef *pAllegiance, AllegianceDef *pSubAllegiance, const char* pchNumeric)
{
	int i, count = 0;
	AllegianceDef *pPreferredAllegiance = allegiance_GetOfficerPreference(pAllegiance, pSubAllegiance);
	iRank = MIN(iRank, (eaSize( &g_OfficerRankStruct.eaRanks )-1) );
	if ( iRank >= 0 && EMPTY_TO_NULL(pchNumeric))
	{
		for (i = 0; i < eaSize( &g_OfficerRankStruct.eaRanks ); ++i)
		{
			const char* pchRankNumeric = EMPTY_TO_NULL(g_OfficerRankStruct.eaRanks[i]->pchRankNumeric) ? g_OfficerRankStruct.eaRanks[i]->pchRankNumeric : DEFAULT_OFFICER_RANK_NUMERIC;

			if (GET_REF(g_OfficerRankStruct.eaRanks[i]->hAllegiance) == pPreferredAllegiance &&
				stricmp(pchNumeric, pchRankNumeric) == 0) 
			{
				++count;
			}

			if (count > iRank)
			{
				return g_OfficerRankStruct.eaRanks[i];
			}
		}
	}

	pPreferredAllegiance = allegiance_GetOfficerNonPreference(pAllegiance, pSubAllegiance);
	if ( pPreferredAllegiance )
	{
		return Officer_GetRankDefUsingNumeric(iRank, pPreferredAllegiance, NULL, pchNumeric);
	}

	return NULL;
}

OfficerRankDef* Officer_GetRankDefFromNodeUsingNumeric(PTNodeDef* pNodeDef, AllegianceDef* pAllegiance, AllegianceDef* pSubAllegiance, const char* pchNumeric)
{
	if ( pNodeDef && pAllegiance )
	{
		PowerPurpose ePurpose = kPowerPurpose_Uncategorized;
		PowerDef* pPowerDef = eaSize(&pNodeDef->ppRanks)>0 ? GET_REF(pNodeDef->ppRanks[0]->hPowerDef) : NULL;
		
		if(pPowerDef)
		{
			ePurpose = pPowerDef->ePurpose;
		}
		else
		{
			ePurpose = pNodeDef->ePurpose;
		}

		return Officer_GetRankDefUsingNumeric(ePurpose-1,pAllegiance,pSubAllegiance,pchNumeric);
	}
	return 0;
}

S32 Officer_GetRankCountUsingNumeric(AllegianceDef *pAllegiance, const char* pchNumeric)
{
	int i, count = 0;
	if (!g_OfficerRankStruct.eaRanks || !pchNumeric) return 0;
	for (i = eaSize( &g_OfficerRankStruct.eaRanks )-1; i >= 0 ; --i)
	{
		const char* pchRankNumeric = EMPTY_TO_NULL(g_OfficerRankStruct.eaRanks[i]->pchRankNumeric) ? g_OfficerRankStruct.eaRanks[i]->pchRankNumeric : DEFAULT_OFFICER_RANK_NUMERIC;
		if (GET_REF(g_OfficerRankStruct.eaRanks[i]->hAllegiance) == pAllegiance && (stricmp(pchRankNumeric, pchNumeric) == 0)) ++count;
	}
	return count;
}

static void Officer_ValidateDefs(void)
{
	int i;

	for(i=eaSize(&g_OfficerRankStruct.eaRanks)-1; i>=0; --i) {
		OfficerRankDef *pDef = g_OfficerRankStruct.eaRanks[i];

		if (!pDef->pDisplayMessage) {
			ErrorFilenamef("defs/config/OfficerRanks.def", "Officer rank def '%s' is missing its display name", pDef->pchName);
		} else if (!GET_REF(pDef->pDisplayMessage->hMessage)) {
			if (REF_STRING_FROM_HANDLE(pDef->pDisplayMessage->hMessage)) {
				ErrorFilenamef("defs/config/OfficerRanks.def", "Officer rank def '%s' references non-existent message '%s'", pDef->pchName, REF_STRING_FROM_HANDLE(pDef->pDisplayMessage->hMessage));
			} else {
				ErrorFilenamef("defs/config/OfficerRanks.def", "Officer rank def '%s' is missing its display name", pDef->pchName);
			}
		} 

		if (!pDef->pDisplayMsgShort) {
			ErrorFilenamef("defs/config/OfficerRanks.def", "Officer rank def '%s' is missing its short display name", pDef->pchName);
		} else if (!GET_REF(pDef->pDisplayMsgShort->hMessage)) {
			if (REF_STRING_FROM_HANDLE(pDef->pDisplayMsgShort->hMessage)) {
				ErrorFilenamef("defs/config/OfficerRanks.def", "Officer rank def '%s' references non-existent message '%s'", pDef->pchName, REF_STRING_FROM_HANDLE(pDef->pDisplayMsgShort->hMessage));
			} else {
				ErrorFilenamef("defs/config/OfficerRanks.def", "Officer rank def '%s' is missing its short display name", pDef->pchName);
			}
		} 
	}
}

static void Officer_ReloadOfficerRankDefs_CB(const char *relpath, int when)
{
	loadstart_printf("Reloading OfficerRankDefs...");

	StructReset(parse_OfficerRankStruct,&g_OfficerRankStruct);

	ParserLoadFiles(NULL, "defs/config/OfficerRanks.def", "OfficerRanks.bin", PARSER_OPTIONALFLAG, parse_OfficerRankStruct, &g_OfficerRankStruct);	

	loadend_printf(" done.");
}

static void Officer_LoadRankDefs(void)
{
	loadstart_printf("Loading OfficerRankDefs...");

	StructInit(parse_OfficerRankStruct, &g_OfficerRankStruct);

	ParserLoadFiles(NULL, "defs/config/OfficerRanks.def", "OfficerRanks.bin", PARSER_OPTIONALFLAG, parse_OfficerRankStruct, &g_OfficerRankStruct);

	if (isDevelopmentMode())
	{
		Officer_ValidateDefs();

		// Have reload take effect immediately
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/OfficerRanks.def", Officer_ReloadOfficerRankDefs_CB);
	}

	loadend_printf(" done (%d OfficerRankDefs).", eaSize(&g_OfficerRankStruct.eaRanks));
}

AUTO_STARTUP(Officers) ASTRT_DEPS(PowerTrees, AS_Messages);
void Officer_Load(void)
{
	Officer_LoadRankDefs();
}

void Officer_GetActionMessageKeyFromReturnValue( char** ppchMsg, OfficerActionReturnValue eReturnValue, const char* pchType )
{
	const char* pchReturn = StaticDefineIntRevLookup(OfficerActionReturnValueEnum, eReturnValue);
	char pchKey[512];
	estrClear(ppchMsg);
	
	if ( pchReturn && pchReturn[0] )
	{
		sprintf( pchKey, "%s_%s", pchType, pchReturn );
		FormatMessageKey(ppchMsg, pchKey, STRFMT_END);
	}
}

S32 Officer_GetPromoteReturnValue(SA_PARAM_NN_VALID Entity* pEnt, U32 uiOfficerID, AllegianceDef *pAllegiance)
{
	S32 iOwnerRank, iRank, iPointsSpent;
	OfficerRankDef* pOfficerRankDef;
	PetRelationship* pPet;
	Entity* pOfficer;

	if ( pEnt->pChar==NULL || pEnt->pSaved==NULL )
		return OfficerActionReturnValue_InvalidOfficer;

	if ( character_HasMode(pEnt->pChar, kPowerMode_Combat) )
	{
		return OfficerActionReturnValue_InCombat;
	}

	if ( !(pPet = SavedPet_GetPetFromContainerID( pEnt, uiOfficerID, true ) ) )
		return OfficerActionReturnValue_InvalidOfficer;

	if ( !(pOfficer = GET_REF(pPet->hPetRef)) || pEnt == pOfficer || !pOfficer->pChar )
		return OfficerActionReturnValue_InvalidOfficer;

	if ( character_HasMode(pOfficer->pChar, kPowerMode_Combat) )
		return OfficerActionReturnValue_InCombat;
	
	if(trade_IsPetBeingTraded(pOfficer, pEnt))
		return OfficerActionReturnValue_BeingTraded;

	iOwnerRank = Officer_GetRank(pEnt);
	iRank = Officer_GetRank(pOfficer);
	pOfficerRankDef = Officer_GetRankDef(iRank+1,pAllegiance,NULL);
	
	if ( pOfficerRankDef==NULL || pOfficerRankDef->bPlayerOnly )
		return OfficerActionReturnValue_InvalidOfficer;

	iPointsSpent = inv_GetNumericItemValue(pOfficer,pOfficerRankDef->pchPointsSpentNumeric);

	if ( pOfficerRankDef->pchCostNumeric && pOfficerRankDef->pchCostNumeric[0] )
	{
		S32 iOwnerCurrency = inv_GetNumericItemValue(pEnt,pOfficerRankDef->pchCostNumeric);

		if ( pOfficerRankDef->iCost > iOwnerCurrency )
		{
			return OfficerActionReturnValue_CannotPayCost;
		}
	}
	
	if ( iOwnerRank < iRank + 2 )
		return OfficerActionReturnValue_InvalidRank;

	if ( pOfficerRankDef->iRequiredPointsSpent > iPointsSpent )
		return OfficerActionReturnValue_NotEnoughPoints;

	return OfficerActionReturnValue_Success;
}

bool Officer_CanPromote(SA_PARAM_NN_VALID Entity* pEnt, U32 uiOfficerID, AllegianceDef *pAllegiance)
{
	return !Officer_GetPromoteReturnValue( pEnt, uiOfficerID, pAllegiance );
}

S32 Officer_GetRequiredPointsForRankUsingNumeric(SA_PARAM_OP_VALID Entity* pPlayerEnt, SA_PARAM_OP_VALID Entity* pOfficer, S32 iRank, const char* pchNumeric, bool bScale)
{
	if (pOfficer)
	{
		AllegianceDef* pAllegianceDef = pPlayerEnt ? GET_REF(pPlayerEnt->hAllegiance) : GET_REF(pOfficer->hAllegiance);
		AllegianceDef* pSubAllegianceDef = pPlayerEnt ? GET_REF(pPlayerEnt->hSubAllegiance) : GET_REF(pOfficer->hSubAllegiance);
		OfficerRankDef* pOfficerRankDef = Officer_GetRankDefUsingNumeric(iRank, pAllegianceDef, pSubAllegianceDef, pchNumeric);

		if ( pOfficerRankDef )
		{
			F32 fScale = 1.0f;
			if (bScale)
			{
				ItemDef* pItemDef;
				if (entGetType(pOfficer) == GLOBALTYPE_ENTITYPLAYER)
				{
					pItemDef = item_DefFromName(pOfficerRankDef->pchPointsSpentPlayerNumeric);
				}
				else
				{
					pItemDef = item_DefFromName(pOfficerRankDef->pchPointsSpentNumeric);
				}
				if (pItemDef)
				{
					fScale = pItemDef->fScaleUI;
				}
			}
			if (entGetType(pOfficer) == GLOBALTYPE_ENTITYPLAYER)
			{
				return pOfficerRankDef->iRequiredPointsSpentPlayer * fScale;
			}
			return pOfficerRankDef->iRequiredPointsSpent * fScale;
		}
	}
	return -1;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME("OfficerRequiredPointsForPlayerRankUsingNumeric");
S32 OfficerExpr_RequiredPointsForPlayerRankUsingNumeric( ExprContext* pContext, S32 iRank, const char* pchNumeric )
{
	Entity* pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);

	return Officer_GetRequiredPointsForRankUsingNumeric( pPlayerEnt, pPlayerEnt, iRank, pchNumeric, false );
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME("OfficerRequiredPointsForPlayerRank");
S32 OfficerExpr_RequiredPointsForPlayerRank( ExprContext* pContext, S32 iRank )
{
	return OfficerExpr_RequiredPointsForPlayerRankUsingNumeric(pContext, iRank, DEFAULT_OFFICER_RANK_NUMERIC);
}

//TODO(MK): could make pchAllegiance into a comma delimited list if necessary later on
AUTO_EXPR_FUNC(player, entity) ACMD_NAME("OfficerIsOfRankAndAllegianceUsingNumeric");
bool OfficerExpr_IsOfRankAndAllegianceUsingNumeric( SA_PARAM_OP_VALID Entity* pOfficer, S32 iRank, const char* pchAllegiance, const char* pchNumeric )
{
	if ( pOfficer==NULL )
		return false;

	if ( pchAllegiance && pchAllegiance[0] )
	{
		AllegianceDef* pDef = GET_REF(pOfficer->hAllegiance);

		if ( !pDef || stricmp(pDef->pcName, pchAllegiance)!=0 )
		{
			AllegianceDef* pSubDef = GET_REF(pOfficer->hAllegiance);
			if ( !pSubDef || stricmp(pSubDef->pcName, pchAllegiance)!=0 )
			{
				return false;
			}
		}
	}
	return ( Officer_GetRankUsingNumeric(pOfficer, pchNumeric) >= iRank );
}

AUTO_EXPR_FUNC(player, entity) ACMD_NAME("OfficerIsOfRankAndAllegiance");
bool OfficerExpr_IsOfRankAndAllegiance( SA_PARAM_OP_VALID Entity* pOfficer, S32 iRank, const char* pchAllegiance )
{
	return OfficerExpr_IsOfRankAndAllegianceUsingNumeric( pOfficer, iRank, pchAllegiance, DEFAULT_OFFICER_RANK_NUMERIC );
}

S32 Officer_GetTrainReturnValue(int iPartitionIdx, SA_PARAM_NN_VALID Entity* pBuyer, SA_PARAM_NN_VALID Entity* pTrainer, U32 uiOfficerID, 
								const char* pchOldNode, const char* pchNewNode, bool bCheckCost, bool bCheckNode, AllegianceDef *pAllegiance)
{
	S32 i, j;
	OfficerRankDef* pOfficerRankDef;
	PetRelationship* pPet;
	Entity* pOfficer;
	PowerTree *pOldTree = NULL;
	PowerTreeDef *pOldTreeDef, *pNewTreeDef;
	PTNode* pOldNode;
	PTNodeDef *pOldNodeDef, *pNewNodeDef;
	PowerDef *pOldPowerDef, *pNewPowerDef;
	PowerPurpose eOldPurpose = kPowerPurpose_Uncategorized;
	PowerPurpose eNewPurpose = kPowerPurpose_Uncategorized;
	const char* pchTree;

	if ( stricmp(pchOldNode,pchNewNode)==0 )
		return OfficerActionReturnValue_SameNode;

	if ( !(pPet = SavedPet_GetPetFromContainerID( pBuyer, uiOfficerID, true ) ) )
		return OfficerActionReturnValue_InvalidOfficer;

	if ( !(pOfficer = GET_REF(pPet->hPetRef)) || pBuyer == pOfficer )
		return OfficerActionReturnValue_InvalidOfficer;

	if ( pBuyer->pChar==NULL || pTrainer->pChar==NULL || pOfficer->pChar==NULL )
		return OfficerActionReturnValue_InvalidOfficer;

	if (character_HasMode(pBuyer->pChar, kPowerMode_Combat) || character_HasMode(pOfficer->pChar, kPowerMode_Combat))
	{
		return OfficerActionReturnValue_InCombat;
	}

	pOldNode = powertree_FindNode( pOfficer->pChar, &pOldTree, pchOldNode );
	pOldNodeDef = pOldNode ? GET_REF(pOldNode->hDef) : NULL;
	pNewNodeDef = powertreenodedef_Find(pchNewNode);
	pOldTreeDef = pOldTree ? GET_REF(pOldTree->hDef) : NULL;
	pNewTreeDef = pNewNodeDef ? powertree_TreeDefFromNodeDef(pNewNodeDef) : NULL;

	if ( !pOldNodeDef || !pNewNodeDef )
		return OfficerActionReturnValue_InvalidAction;

	if ( !pOldTreeDef || !pNewTreeDef || pOldTreeDef != pNewTreeDef )
		return OfficerActionReturnValue_InvalidAction;

	if ( bCheckNode && !powertree_CharacterHasTrainerUnlockNode(pTrainer->pChar,pNewNodeDef) )
	{
		return OfficerActionReturnValue_InvalidNode;
	}

	pchTree = REF_STRING_FROM_HANDLE(pOldTree->hDef);
	pOldPowerDef = GET_REF(pOldNodeDef->ppRanks[0]->hPowerDef);
	pNewPowerDef = GET_REF(pNewNodeDef->ppRanks[0]->hPowerDef);

	if(pOldPowerDef)
	{
		eOldPurpose = pOldPowerDef->ePurpose;
	}
	else if(pOldNodeDef)
	{
		eOldPurpose = pOldNodeDef->ePurpose;
	}
	if(pNewPowerDef)
	{
		eNewPurpose = pNewPowerDef->ePurpose;
	}
	else if(pNewNodeDef)
	{
		eNewPurpose = pNewNodeDef->ePurpose;
	}

	if ( eOldPurpose != eNewPurpose )
		return OfficerActionReturnValue_MismatchedNodePurposes;

	pOfficerRankDef = Officer_GetRankDef(eOldPurpose-1,pAllegiance,NULL);

	if ( pOfficerRankDef==NULL )
		return OfficerActionReturnValue_InvalidNode;

	if (bCheckCost && pOfficerRankDef->pchTrainingNumeric && pOfficerRankDef->pchTrainingNumeric[0])
	{
		S32 iOwnerCurrency = inv_GetNumericItemValue(pBuyer,pOfficerRankDef->pchTrainingNumeric);
		InteractInfo* pInfo = SAFE_MEMBER2(pBuyer, pPlayer, pInteractInfo);
		if (pInfo && pInfo->pContactDialog)
		{
			bool bValid = false;
			ContactDialog* pContactDialog = pInfo->pContactDialog;
			for (i = eaSize(&pContactDialog->eaStorePowers)-1; i >= 0; i--)
			{
				PowerStorePowerInfo* pPowerInfo = pContactDialog->eaStorePowers[i];
				if (	GET_REF(pPowerInfo->hNode) == pNewNodeDef
					&&	GET_REF(pPowerInfo->hTree) == pNewTreeDef)
				{
					for (j = eaSize(&pPowerInfo->eaCostInfo)-1; j >= 0; j--)
					{
						PowerStoreCostInfo* pCostInfo = pPowerInfo->eaCostInfo[j];
						const char* pchCostNumeric = REF_STRING_FROM_HANDLE(pCostInfo->hCurrency);
						if (stricmp(pchCostNumeric,pOfficerRankDef->pchTrainingNumeric)==0)
						{
							if (pCostInfo->iCount > 0 && pCostInfo->iCount > iOwnerCurrency)
							{
								return OfficerActionReturnValue_CannotPayCost;
							}
							bValid = true;
						}
					}
				}
			}
			if (!bValid)
			{
				return OfficerActionReturnValue_InvalidNode;
			}
		}
		else if (pOfficerRankDef->iTrainingCost > 0 && pOfficerRankDef->iTrainingCost > iOwnerCurrency)
		{
			return OfficerActionReturnValue_CannotPayCost;
		}
	}

	if ( eaUSize(&pOfficer->pChar->ppTraining) >= g_PetRestrictions.uiMaxSimultaneousTraining )
		return OfficerActionReturnValue_ExceededMaxActions;

	for ( i = eaSize(&pOfficer->pChar->ppTraining)-1; i >= 0; i-- )
	{
		if ( stricmp(pchNewNode,REF_STRING_FROM_HANDLE(pOfficer->pChar->ppTraining[i]->hNewNodeDef))==0 )
			break;
	}
	if ( i >= 0 )
		return OfficerActionReturnValue_ActionAlreadyQueued;

	for ( i = ea32Size(&pBuyer->pSaved->ppAwayTeamPetID)-1; i >= 0; i-- )
	{
		if ( uiOfficerID == (U32)pBuyer->pSaved->ppAwayTeamPetID[i] )
		{
			return OfficerActionReturnValue_OnAwayTeam;
		}
	}

	if ( !entity_CanReplacePowerTreeNodeInEscrow(	iPartitionIdx, CONTAINER_NOCONST(Entity, pTrainer), CONTAINER_NOCONST(Entity, pOfficer), 
													pchTree, pchOldNode, pchTree, pchNewNode, true))
	{
		return OfficerActionReturnValue_InvalidAction;
	}

	if(trade_IsPetBeingTraded(pOfficer, pBuyer))
		return OfficerActionReturnValue_BeingTraded;

	return OfficerActionReturnValue_Success;
}

bool Officer_CanTrain(int iPartitionIdx, SA_PARAM_NN_VALID Entity* pBuyer, SA_PARAM_NN_VALID Entity* pTrainer, U32 uiOfficerID, const char* pchOldNode, const char* pchNewNode, bool bCheckCost, bool bCheckNode, AllegianceDef *pAllegiance)
{
	return !Officer_GetTrainReturnValue( iPartitionIdx, pBuyer, pTrainer, uiOfficerID, pchOldNode, pchNewNode, bCheckCost, bCheckNode, pAllegiance );
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEntity, ".Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]");
S32 trhOfficer_GetExtraPuppets(ATH_ARG NOCONST(Entity) *pEntity, GameAccountDataExtract *pExtract)
{
	S32 iRank = trhOfficer_GetRankUsingNumeric(pEntity, DEFAULT_OFFICER_RANK_NUMERIC);
	OfficerRankDef* pOfficerRankDef = Officer_GetRankDef(iRank, GET_REF(pEntity->hAllegiance), GET_REF(pEntity->hSubAllegiance));
	S32 iExtra = pOfficerRankDef ? pOfficerRankDef->iExtraAllowedPuppets : 0, iTemp;

	if (pExtract)
	{
		iTemp = gad_GetAttribIntFromExtract(pExtract, "ST.ExtraPuppets");
		if (iTemp > 0)
		{
			iExtra += iTemp;
		}
	}

	iTemp = inv_trh_GetNumericValue(ATR_EMPTY_ARGS,pEntity,"ExtraPuppets");
	if (iTemp > 0)
	{
		iExtra += iTemp;
	}

    iTemp = inv_trh_GetNumericValue(ATR_EMPTY_ARGS,pEntity,"ExtraPuppets2");
    if (iTemp > 0)
    {
        iExtra += iTemp;
    }

	return iExtra;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEntity, ".Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]");
S32 trhOfficer_GetMaxAllowedPets(ATH_ARG NOCONST(Entity)* pEntity, AllegianceDef *pAllegiance, GameAccountDataExtract *pExtract)
{
	S32 iRank = trhOfficer_GetRankUsingNumeric(pEntity, DEFAULT_OFFICER_RANK_NUMERIC);
	OfficerRankDef* pOfficerRankDef = Officer_GetRankDef(iRank,pAllegiance,NULL);

	if(pOfficerRankDef)
	{
		S32 iMaximumSlots = pOfficerRankDef->iBaseAllowedPets;
		S32 iExtraSlots = inv_trh_GetNumericValue(ATR_EMPTY_ARGS,pEntity,"ExtraOfficerSlots");
		
		if(iExtraSlots > 0)
			iMaximumSlots += iExtraSlots;

		if(NONNULL(pEntity->pPlayer))
		{
			if(pExtract)
			{
				iMaximumSlots += gad_GetAttribIntFromExtract(pExtract, MicroTrans_GetOfficerSlotsGADKey());
			}
		}

		if(!pExtract || GamePermission_ExtractHasToken(pExtract, GAME_PERMISSION_PET_EXTRASLOTS, false))
		{
			iMaximumSlots += pOfficerRankDef->iExtraAllowedPets;
		}

		return iMaximumSlots;
	}

	return  -1;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEntity, ".Hallegiance, .Hsuballegiance, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets, pInventoryV2.ppLiteBags[]");
bool trhOfficer_CanAddOfficer(ATH_ARG NOCONST(Entity) *pEntity, AllegianceDef *pPetAllegiance, GameAccountDataExtract *pExtract)
{
	AllegianceDef *pEntityAllegiance = GET_REF(pEntity->hAllegiance);
	AllegianceDef *pEntitySubAllegiance = GET_REF(pEntity->hSubAllegiance);
	S32 iMaxOfficers = trhOfficer_GetMaxAllowedPets(pEntity,allegiance_GetOfficerPreference(pEntityAllegiance, pEntitySubAllegiance),pExtract);
	S32 iOfficers = trhEntity_CountPets(pEntity,true,false,false);
	
	if(iMaxOfficers > -1 && iMaxOfficers <= iOfficers)
		return false;

	if(pPetAllegiance && pEntityAllegiance && pEntityAllegiance != pPetAllegiance)
	{
		if (!pEntitySubAllegiance || pEntitySubAllegiance != pPetAllegiance)
		{
			return false;
		}
	}

	return true;
}

S32 Officer_GetGrade(Entity* pOfficer, S32 iOverrideLevel, S32* piLevel, S32* piRank)
{
	S32 iRank = Officer_GetRank(pOfficer);
	S32 iLevel = 0;
	AllegianceDef* pAllegiance;

	if (iOverrideLevel > 0)
	{
		iLevel = iOverrideLevel;
	}
	else if (entGetType(pOfficer) == GLOBALTYPE_ENTITYPLAYER)
	{
		iLevel = entity_GetSavedExpLevel(pOfficer);
	}

	if (piLevel)
	{
		(*piLevel) = iLevel;
	}
	if (piRank)
	{
		(*piRank) = iRank;
	}
	if (iLevel==0)
	{
		return -1;
	}
	pAllegiance = allegiance_GetOfficerPreference(GET_REF(pOfficer->hAllegiance), GET_REF(pOfficer->hSubAllegiance));
	return Officer_GetGradeFromLevelAndRank(pAllegiance, iLevel, iRank);
}

bool Officer_GetRankAndGradeFromLevel(S32 iLevel, AllegianceDef* pAllegiance, S32* piRank, S32* piGrade)
{
	if (pAllegiance && iLevel >= 0)
	{
		S32 iRank;
		for (iRank = Officer_GetRankCount(pAllegiance)-1; iRank >= 0; iRank--)
		{
			S32 iGrade = Officer_GetGradeFromLevelAndRank(pAllegiance, iLevel, iRank);
			if (iGrade > 0)
			{
				if (piRank)
				{
					(*piRank) = iRank;
				}
				if (piGrade)
				{
					(*piGrade) = iGrade;
				}
				return true;
			}
		}
	}
	return false;
}

S32 Officer_GetGradeFromLevelAndRank(AllegianceDef* pAllegiance, S32 iLevel, S32 iRank)
{
	S32 i;
	S32 iGradeCount = 0;
	S32 iRankCount = 0;
	for (i = 0; i < eaSize(&g_OfficerRankStruct.eaRanks); i++)
	{
		OfficerRankDef* pRankDef = g_OfficerRankStruct.eaRanks[i];
		AllegianceDef* pRankAllegiance = GET_REF(pRankDef->hAllegiance);

		if (pRankAllegiance != pAllegiance)
		{
			continue;
		}
		if (++iRankCount > iRank)
		{
			break;
		}
		iGradeCount += pRankDef->iGradeCount;
	}
	return MAX(iLevel, 1) - iGradeCount;
}

S32 Officer_GetLevelFromRankAndGrade(AllegianceDef* pAllegiance, S32 iRank, S32 iGrade)
{
	S32 i;
	S32 iLevel = 0;
	S32 iRankCount = 0;
	for (i = 0; i < eaSize(&g_OfficerRankStruct.eaRanks); i++)
	{
		OfficerRankDef* pRankDef = g_OfficerRankStruct.eaRanks[i];
		AllegianceDef* pRankAllegiance = GET_REF(pRankDef->hAllegiance);
		
		if (pRankAllegiance != pAllegiance)
		{
			continue;
		}
		if (iRankCount == iRank)
		{
			break;
		}
		iLevel += pRankDef->iGradeCount;
		iRankCount++;
	}
	return iLevel + iGrade;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pOfficer, ".Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]");
S32 trhOfficer_GetRankUsingNumeric(ATH_ARG NOCONST(Entity)* pOfficer, const char* pchNumeric)
{
	if (ISNULL(pOfficer) || Officer_GetRankCountUsingNumeric(allegiance_GetOfficerPreference(GET_REF(pOfficer->hAllegiance), GET_REF(pOfficer->hSubAllegiance)), pchNumeric)==0)
	{
		return 0;
	}
	return inv_trh_GetNumericValue(ATR_EMPTY_ARGS, pOfficer, pchNumeric);
}

#include "AutoGen/OfficerCommon_h_ast.c"
