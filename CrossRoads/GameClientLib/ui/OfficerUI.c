/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "species_common.h"
#include "allegiance.h"
#include "Character.h"
#include "entCritter.h"
#include "Entity.h"
#include "EString.h"
#include "ExpressionPrivate.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "gclEntity.h"
#include "gclUIGen.h"
#include "Guild.h"
#include "inventoryCommon.h"
#include "OfficerCommon.h"
#include "Player.h"
#include "PowerGrid.h"
#include "Powers.h"
#include "PowerTree.h"
#include "structDefines.h"
#include "Team.h"
#include "GameClientLib.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/OfficerUI_c_ast.h"
#include "AutoGen/PowerGrid_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetPromoteOfficerError");
const char* OfficerExpr_GetPromoteOfficerError(	ExprContext *pContext,
												SA_PARAM_OP_VALID Entity* pEnt,
												SA_PARAM_OP_VALID Entity* pOfficer)
{
	OfficerActionReturnValue eReturn;
	char* pcMessageKey = NULL;
	char* pcMessageKeyScratch;

	if ( pEnt==NULL || pOfficer==NULL )
		return "";

	if ( eReturn = Officer_GetPromoteReturnValue(pEnt,entGetContainerID(pOfficer),allegiance_GetOfficerPreference(GET_REF(pEnt->hAllegiance),GET_REF(pEnt->hSubAllegiance))) )
	{
		U32 uiSize;
		estrCreate( &pcMessageKey );
		Officer_GetActionMessageKeyFromReturnValue( &pcMessageKey, eReturn, "OfficerPromotionMessage" );
		uiSize = estrLength(&pcMessageKey)+1;
		pcMessageKeyScratch = exprContextAllocScratchMemory( pContext, uiSize );
		strcpy_s( pcMessageKeyScratch, uiSize, pcMessageKey );
		estrDestroy( &pcMessageKey );
		return pcMessageKeyScratch;
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PromoteOfficer");
bool OfficerExpr_PromoteOfficer(SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_OP_VALID Entity* pOfficer, bool bTestOnly)
{
	if ( pEnt==NULL || pOfficer==NULL )
		return false;

	if ( Officer_CanPromote(pEnt,entGetContainerID(pOfficer),allegiance_GetOfficerPreference(GET_REF(pEnt->hAllegiance),GET_REF(pEnt->hSubAllegiance))) )
	{
		if ( !bTestOnly )
		{
			ServerCmd_gslPromoteOfficer( entGetType(pEnt), entGetContainerID(pEnt), entGetContainerID(pOfficer) );
		}
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerTrainingGetSkillToReplace");
const char* gclOfficerExpr_TrainingGetSkillToReplace( SA_PARAM_OP_VALID Entity* pOfficer, const char* pchTrainNode )
{
	PTNodeDef* pTrainNodeDef = powertreenodedef_Find( pchTrainNode );

	if ( pOfficer && pOfficer->pChar && pTrainNodeDef )
	{
		PowerDef* pTrainPowerDef = eaSize(&pTrainNodeDef->ppRanks)>0 ? GET_REF(pTrainNodeDef->ppRanks[0]->hPowerDef) : NULL;
		PowerPurpose eTrainPurpose;
		bool bProp = powertree_NodeHasPropagationPowers( pTrainNodeDef );
		S32 i, j;

		if(pTrainPowerDef)
		{
			eTrainPurpose = pTrainPowerDef->ePurpose;
		}
		else
		{
			eTrainPurpose = pTrainNodeDef->ePurpose;
		}

		for ( i = eaSize(&pOfficer->pChar->ppPowerTrees)-1; i >= 0; i-- )
		{
			PowerTree* pTree = pOfficer->pChar->ppPowerTrees[i];

			for ( j = eaSize(&pTree->ppNodes)-1; j >= 0; j-- )
			{
				PTNode* pNode = pTree->ppNodes[j];
				PTNodeDef* pNodeDef = GET_REF(pNode->hDef);

				if ( pNodeDef )
				{
					PowerDef* pPowerDef = eaSize(&pNodeDef->ppRanks)>0 ? GET_REF(pNodeDef->ppRanks[0]->hPowerDef) : NULL;
					PowerPurpose ePurpose = kPowerPurpose_Uncategorized;
					if(pPowerDef)
					{
						ePurpose = pPowerDef->ePurpose;
					}
					else if(pNodeDef)
					{
						ePurpose = pNodeDef->ePurpose;
					}
					if ( ePurpose != eTrainPurpose )
						continue;

					if ( bProp == powertree_NodeHasPropagationPowers( pNodeDef ) )
					{
						return pNodeDef->pchNameFull;
					}
				}
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetTrainOfficerError");
const char* OfficerExpr_GetTrainOfficerError(	ExprContext *pContext,
												SA_PARAM_OP_VALID Entity* pTrainer, SA_PARAM_OP_VALID Entity* pOfficer,
												const char* pchTrainNode, bool bCheckCost)
{
	OfficerActionReturnValue eReturn;
	char* pcMessageKey = NULL;
	char* pcMessageKeyScratch;
	const char* pchOldNode;

	Entity* pPlayerEnt = entActivePlayerPtr();

	if ( pPlayerEnt==NULL || pTrainer==NULL || pOfficer==NULL )
		return "";

	pchOldNode = gclOfficerExpr_TrainingGetSkillToReplace(pOfficer, pchTrainNode);

	if ( pchOldNode==NULL )
		return "";

	if ( eReturn = Officer_GetTrainReturnValue(PARTITION_CLIENT,pPlayerEnt,pTrainer,entGetContainerID(pOfficer),pchOldNode,pchTrainNode,bCheckCost,false,
												allegiance_GetOfficerPreference(GET_REF(pPlayerEnt->hAllegiance),GET_REF(pPlayerEnt->hSubAllegiance))) )
	{
		U32 uiSize;
		estrCreate( &pcMessageKey );
		Officer_GetActionMessageKeyFromReturnValue( &pcMessageKey, eReturn, "OfficerTrainingMessage" );
		uiSize = estrLength(&pcMessageKey)+1;
		pcMessageKeyScratch = exprContextAllocScratchMemory( pContext, uiSize );
		strcpy_s( pcMessageKeyScratch, uiSize, pcMessageKey );
		estrDestroy( &pcMessageKey );
		return pcMessageKeyScratch;
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CanTrainOfficer");
bool OfficerExpr_CanTrainOfficer(	SA_PARAM_OP_VALID Entity* pTrainer, SA_PARAM_OP_VALID Entity* pOfficer,
									const char* pchTrainNode, bool bCheckCost )
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	const char* pchOldNode;

	if ( pPlayerEnt==NULL || pTrainer==NULL || pOfficer==NULL )
		return false;

	pchOldNode = gclOfficerExpr_TrainingGetSkillToReplace(pOfficer, pchTrainNode);

	if ( pchOldNode==NULL )
		return false;

	return Officer_CanTrain(PARTITION_CLIENT, pPlayerEnt, pTrainer, entGetContainerID(pOfficer), pchOldNode, pchTrainNode, bCheckCost, false,
							allegiance_GetOfficerPreference(GET_REF(pPlayerEnt->hAllegiance), GET_REF(pPlayerEnt->hSubAllegiance)));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TrainOfficer");
bool OfficerExpr_TrainOfficer(SA_PARAM_OP_VALID Entity* pTrainer, SA_PARAM_OP_VALID Entity* pOfficer, const char* pchTrainNode, bool bCheckCost)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	const char* pchOldNode;

	if ( pPlayerEnt==NULL || pTrainer==NULL || pOfficer==NULL )
		return false;

	pchOldNode = gclOfficerExpr_TrainingGetSkillToReplace(pOfficer, pchTrainNode);

	if ( pchOldNode==NULL )
		return false;

	if ( Officer_CanTrain(PARTITION_CLIENT, pPlayerEnt, pTrainer, entGetContainerID(pOfficer), pchOldNode, pchTrainNode, bCheckCost, false,
							allegiance_GetOfficerPreference(GET_REF(pPlayerEnt->hAllegiance), GET_REF(pPlayerEnt->hSubAllegiance))) )
	{
		ServerCmd_gslTrainOfficer(entGetType(pTrainer),entGetContainerID(pTrainer),entGetContainerID(pOfficer),pchOldNode,pchTrainNode);
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerCancelTraining");
bool OfficerExpr_OfficerCancelTraining(	SA_PARAM_OP_VALID Entity* pOfficer )
{
	Entity* pEnt = entActivePlayerPtr();

	if ( pEnt && pOfficer && pOfficer->pChar && eaSize(&pOfficer->pChar->ppTraining) > 0 )
	{
		ServerCmd_gslCancelTrainingOfficer(	entGetContainerID(pOfficer),
											REF_STRING_FROM_HANDLE(pOfficer->pChar->ppTraining[0]->hNewNodeDef) );
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
////
//// Rank Display Name: Officer(Get)?RankDisplayName
////	* UsingNumeric
////	* <Default>
////	* ShortUsingNumeric
////	* Short
////

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerGetRankDisplayNameUsingNumeric");
const char* OfficerExpr_GetRankDisplayNameUsingNumeric(const char* pchAllegiance, S32 iRank, const char* pchNumeric)
{
	AllegianceDef* pAllegiance = allegiance_FindByName(pchAllegiance);
	OfficerRankDef* pOfficerRankDef = pAllegiance ? Officer_GetRankDefUsingNumeric(iRank, pAllegiance, NULL, pchNumeric) : NULL;

	if ( pOfficerRankDef && pOfficerRankDef->pDisplayMessage )
	{
		return TranslateMessageRef(pOfficerRankDef->pDisplayMessage->hMessage);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerGetRankDisplayName");
const char* OfficerExpr_GetRankDisplayName(const char* pchAllegiance, S32 iRank)
{
	return OfficerExpr_GetRankDisplayNameUsingNumeric(pchAllegiance, iRank, DEFAULT_OFFICER_RANK_NUMERIC);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerGetRankDisplayNameShortUsingNumeric");
const char* OfficerExpr_GetRankDisplayNameShortUsingNumeric(const char* pchAllegiance, S32 iRank, const char* pchNumeric)
{
	AllegianceDef* pAllegiance = allegiance_FindByName(pchAllegiance);
	OfficerRankDef* pOfficerRankDef = pAllegiance ? Officer_GetRankDefUsingNumeric(iRank, pAllegiance, NULL, pchNumeric) : NULL;

	if ( pOfficerRankDef && pOfficerRankDef->pDisplayMsgShort )
	{
		return TranslateMessageRef(pOfficerRankDef->pDisplayMsgShort->hMessage);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerGetRankDisplayNameShort");
const char* OfficerExpr_GetRankDisplayNameShort(const char* pchAllegiance, S32 iRank)
{
	return OfficerExpr_GetRankDisplayNameShortUsingNumeric(pchAllegiance, iRank, DEFAULT_OFFICER_RANK_NUMERIC);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetOfficerRankDisplayNameUsingNumeric");
const char* OfficerExpr_GetOfficerRankDisplayNameUsingNumeric(SA_PARAM_OP_VALID Entity* pOfficer,S32 iModifier, const char* pchNumeric)
{
	if ( pOfficer )
	{
		S32 iRank = Officer_GetRankUsingNumeric(pOfficer, pchNumeric);
		AllegianceDef *pAllegianceDef = GET_REF(pOfficer->hAllegiance);
		AllegianceDef *pSubAllegianceDef = GET_REF(pOfficer->hSubAllegiance);
		OfficerRankDef* pOfficerRankDef = Officer_GetRankDefUsingNumeric(iRank+iModifier,pAllegianceDef,pSubAllegianceDef,pchNumeric);

		if ( pOfficerRankDef && pOfficerRankDef->pDisplayMessage )
			return TranslateMessageRef(pOfficerRankDef->pDisplayMessage->hMessage);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetOfficerRankDisplayName");
const char* OfficerExpr_GetOfficerRankDisplayName(SA_PARAM_OP_VALID Entity* pOfficer,S32 iModifier)
{
	return OfficerExpr_GetOfficerRankDisplayNameUsingNumeric(pOfficer,iModifier, DEFAULT_OFFICER_RANK_NUMERIC);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetOfficerRankDisplayNameShortUsingNumeric");
const char* OfficerExpr_GetOfficerRankDisplayNameShortUsingNumeric(SA_PARAM_OP_VALID Entity* pOfficer,S32 iModifier, const char* pchNumeric)
{
	if ( pOfficer )
	{
		S32 iRank = Officer_GetRankUsingNumeric(pOfficer, pchNumeric);
		AllegianceDef *pAllegianceDef = GET_REF(pOfficer->hAllegiance);
		AllegianceDef *pSubAllegianceDef = GET_REF(pOfficer->hSubAllegiance);
		OfficerRankDef* pOfficerRankDef = Officer_GetRankDefUsingNumeric(iRank+iModifier,pAllegianceDef,pSubAllegianceDef,pchNumeric);

		if ( pOfficerRankDef && pOfficerRankDef->pDisplayMsgShort )
			return TranslateMessageRef(pOfficerRankDef->pDisplayMsgShort->hMessage);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetOfficerRankDisplayNameShort");
const char* OfficerExpr_GetOfficerRankDisplayNameShort(SA_PARAM_OP_VALID Entity* pOfficer,S32 iModifier)
{
	return OfficerExpr_GetOfficerRankDisplayNameShortUsingNumeric(pOfficer, iModifier, DEFAULT_OFFICER_RANK_NUMERIC);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
////
//// Conversions between Level <==> Rank <==> Grade
////

static AllegianceDef* gclOfficerGetAllegianceFromString(const char* pchAllegiance)
{
	Entity* pEnt = entActivePlayerPtr();
	AllegianceDef* pAllegiance = NULL;

	if (pchAllegiance && pchAllegiance[0])
	{
		pAllegiance = allegiance_FindByName(pchAllegiance);
	}
	else if (pEnt)
	{
		pAllegiance = GET_REF(pEnt->hAllegiance);
	}
	return pAllegiance;
}

static S32 gclOfficerGetGradeFromLevelAndRank(const char* pchAllegiance, S32 iLevel, S32* piRank) 
{
	AllegianceDef* pAllegiance = gclOfficerGetAllegianceFromString(pchAllegiance);
	S32 iGrade = -1;

	if (!pAllegiance)
	{
		*piRank = -1;
		return -1;
	}

	if ((*piRank) >= 0)
	{
		iGrade = Officer_GetGradeFromLevelAndRank(pAllegiance, iLevel, (*piRank));
	}
	else
	{
		Officer_GetRankAndGradeFromLevel(iLevel, pAllegiance, piRank, &iGrade);
	}
	return iGrade;
}

// Calculate the Grade from iLevel and iRank.  If iRank < 0 then it will be completely derived from the level.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerGetGradeFromLevelAndRank");
S32 OfficerExpr_OfficerGetGradeFromLevelAndRank(const char* pchAllegiance, S32 iLevel, S32 iRank)
{
	return gclOfficerGetGradeFromLevelAndRank(pchAllegiance, iLevel, &iRank);
}

// Calculate the Rank from iLevel.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerGetRankFromLevel");
S32 OfficerExpr_OfficerGetRankFromLevel(const char* pchAllegiance, S32 iLevel)
{
	S32 iRank = -1;
	gclOfficerGetGradeFromLevelAndRank(pchAllegiance, iLevel, &iRank);
	return iRank;
}

// Calculate the Level from iRank and iGrade.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerGetLevelFromRankAndGrade");
S32 OfficerExpr_OfficerGetLevelFromRankAndGrade(const char* pchAllegiance, S32 iRank, S32 iGrade)
{
	AllegianceDef* pAllegiance = gclOfficerGetAllegianceFromString(pchAllegiance);
	if (pAllegiance)
	{
		return Officer_GetLevelFromRankAndGrade(pAllegiance, iRank, iGrade);
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
////
//// Rank & Grade Display:
////	* OfficerGetRankAndGradeString
////	* <Default>
////	* ShortUsingNumeric
////	* Short
////

static const char* gclGetRankAndGradeAsString(	ExprContext *pContext,
												S32 iLevel, S32 iRank, S32 iGrade,
												AllegianceDef* pAllegiance,
												const char* pchRankAndGradeMsgKey,
												const char* pchGradeMsgKey,
												bool bShortRankName)
{
	OfficerRankDef* pOfficerRankDef = Officer_GetRankDef(iRank,pAllegiance,NULL);
	if (pOfficerRankDef)
	{
		const char* result = NULL;
		char* estrResult = NULL;
		const char* pchRankName = NULL;
		if (bShortRankName && pOfficerRankDef->pDisplayMsgShort)
		{
			pchRankName = TranslateMessageRef(pOfficerRankDef->pDisplayMsgShort->hMessage);
		}
		else if (!bShortRankName && pOfficerRankDef->pDisplayMessage)
		{
			pchRankName = TranslateMessageRef(pOfficerRankDef->pDisplayMessage->hMessage);
		}
		if (pchRankName)
		{
			estrStackCreate(&estrResult);
			FormatGameMessageKey(&estrResult,
				iLevel > 0 ? pchRankAndGradeMsgKey : pchGradeMsgKey,
				STRFMT_STRING("Rank", pchRankName),
				STRFMT_INT("Grade", iGrade),
				STRFMT_INT("Level", iLevel),
				STRFMT_INT("ZeroLevel", MAX(iLevel - 1, 0)),
				STRFMT_END);
			if (estrResult && *estrResult)
			{
				result = exprContextAllocString(pContext, estrResult);
			}
			estrDestroy(&estrResult);
			return NULL_TO_EMPTY(result);
		}
	}
	return "";
}

// Get the rank and grade from an entity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerGetRankAndGradeString");
const char* OfficerExpr_GetRankAndGrade(ExprContext *pContext,
										SA_PARAM_OP_VALID Entity* pOfficer,
										const char* pchRankAndGradeMsgKey,
										const char* pchGradeMsgKey,
										bool bShortRankName,
										bool bUseCombatLevel)
{
	if (pOfficer)
	{
		S32 iRank, iLevel;
		S32 iOverrideLevel = bUseCombatLevel ? entity_GetCombatLevel(pOfficer) : 0;
		S32 iGrade = Officer_GetGrade(pOfficer, iOverrideLevel, &iLevel, &iRank);
		AllegianceDef* pAllegiance = allegiance_GetOfficerPreference(GET_REF(pOfficer->hAllegiance), GET_REF(pOfficer->hSubAllegiance));

		if (pAllegiance && iLevel >= 0)
		{
			return gclGetRankAndGradeAsString(pContext, iLevel, iRank, iGrade, pAllegiance, 
											  pchRankAndGradeMsgKey, pchGradeMsgKey, 
											  bShortRankName);
		}
	}
	return "";
}

// If iRank < 0 then it will automatically be calculated from iLevel
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerGetRankAndGradeStringFromLevelAndRank");
const char* OfficerExpr_OfficerGetRankAndGradeStringFromLevelAndRank(ExprContext *pContext,
																	 S32 iLevel, S32 iRank,
																	 const char* pchAllegiance,
																	 const char* pchRankAndGradeMsgKey,
																	 const char* pchGradeMsgKey,
																	 bool bShortRankName)
{

	S32 iGrade = gclOfficerGetGradeFromLevelAndRank(pchAllegiance, iLevel, &iRank);
	if (iRank >= 0 && iGrade >= 0)
	{
		return gclGetRankAndGradeAsString(pContext, iLevel, iRank, iGrade,
										  gclOfficerGetAllegianceFromString(pchAllegiance), 
										  pchRankAndGradeMsgKey, pchGradeMsgKey, 
										  bShortRankName);
	}
	return "";
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
////
//// More rank/grade functions
////

// Get the number of grades for the rank
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerRankGetGradeCount");
S32 OfficerExpr_OfficerRankGetGradeCount(const char* pchAllegiance, S32 iRank)
{
	AllegianceDef* pAllegiance = allegiance_FindByName(pchAllegiance);
	OfficerRankDef* pRankDef = Officer_GetRankDefUsingNumeric(iRank, pAllegiance, NULL, DEFAULT_OFFICER_RANK_NUMERIC);
	if (pRankDef)
	{
		return pRankDef->iGradeCount;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerGetRank");
S32 OfficerExpr_OfficerGetRank(Entity* pOfficer)
{
	Entity* pPetEnt;
	Entity* pEnt = entActivePlayerPtr();
	if (!pOfficer)
	{
		return 0;
	}
	if (entIsPlayer(pOfficer))
	{
		if (pOfficer == pEnt)
		{
			return Officer_GetRank(pEnt);
		}
		else if (team_OnSameTeam(pEnt, pOfficer))
		{
			Team* pTeam = team_GetTeam(pEnt);
			TeamMember* pMember = team_FindMember(pTeam, pOfficer);
			if (pMember)
			{
				return pMember->iOfficerRank;
			}
		}
		else if (guild_InSameGuild(pEnt, pOfficer))
		{
			GuildMember* pMember = guild_FindMember(pOfficer);
			if (pMember)
			{
				return pMember->iOfficerRank;
			}
		}
		//guess at the rank - TODO(MK): add an officer rank field on the player and use that instead
		if (SAFE_MEMBER2(pOfficer, pChar, iLevelCombat))
		{
			S32 iRank = -1;
			S32 iCombatLevel = pOfficer->pChar->iLevelCombat;
			AllegianceDef* pAllegiance = allegiance_GetOfficerPreference(GET_REF(pOfficer->hAllegiance), GET_REF(pOfficer->hSubAllegiance));
			Officer_GetRankAndGradeFromLevel(iCombatLevel,pAllegiance,&iRank,NULL);
			return iRank;
		}
	}
	else if (entGetType(pOfficer) == GLOBALTYPE_ENTITYSAVEDPET
		&& (pPetEnt = entity_GetSubEntity(PARTITION_CLIENT, pEnt,entGetType(pOfficer),entGetContainerID(pOfficer))))
	{
		return Officer_GetRank(pPetEnt);
	}
	else if (pOfficer->pCritter)
	{
		return critterRankGetOrder(pOfficer->pCritter->pcRank);
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerRequiredPointsForRankUsingNumeric");
S32 OfficerExpr_OfficerRequiredPointsForRankUsingNumeric( SA_PARAM_OP_VALID Entity* pOfficer, S32 iRank, const char* pchNumeric )
{
	return Officer_GetRequiredPointsForRankUsingNumeric( entActivePlayerPtr(), pOfficer, iRank, pchNumeric, false );
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerRequiredPointsForRank");
S32 OfficerExpr_OfficerRequiredPointsForRank( SA_PARAM_OP_VALID Entity* pOfficer, S32 iRank )
{
	return Officer_GetRequiredPointsForRank( entActivePlayerPtr(), pOfficer, iRank, false );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerRequiredPointsForRankScaled");
S32 OfficerExpr_OfficerRequiredPointsForRankScaled( SA_PARAM_OP_VALID Entity* pOfficer, S32 iRank )
{
	return Officer_GetRequiredPointsForRank(entActivePlayerPtr(), pOfficer, iRank, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerRequiredPointsToPromote");
S32 OfficerExpr_OfficerRequiredPointsToPromote(SA_PARAM_OP_VALID Entity* pOfficer)
{
	if ( pOfficer )
	{
		S32 iRank = Officer_GetRank(pOfficer);

		if ( iRank >= 0 )
		{
			return Officer_GetRequiredPointsForRank( entActivePlayerPtr(), pOfficer, iRank+1, false);
		}
	}
	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerRequiredPointsToPromoteScaled");
S32 OfficerExpr_OfficerRequiredPointsToPromoteScaled(SA_PARAM_OP_VALID Entity* pOfficer)
{
	if ( pOfficer )
	{
		S32 iRank = Officer_GetRank(pOfficer);

		if ( iRank >= 0 )
		{
			return Officer_GetRequiredPointsForRank(entActivePlayerPtr(), pOfficer, iRank+1, true);
		}
	}
	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerGetPromotionCostNumeric");
const char* OfficerExpr_OfficerGetPromotionCostNumeric(SA_PARAM_OP_VALID Entity* pOfficer)
{
	if ( pOfficer )
	{
		Entity* pEnt = entActivePlayerPtr();
		S32 iRank = Officer_GetRank(pOfficer);
		OfficerRankDef* pOfficerRankDef = Officer_GetRankDef(iRank+1,pEnt?GET_REF(pEnt->hAllegiance):NULL,pEnt?GET_REF(pEnt->hSubAllegiance):NULL);

		if ( pOfficerRankDef )
			return pOfficerRankDef->pchCostNumeric;
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerGetPromotionCost");
S32 OfficerExpr_OfficerGetPromotionCost(SA_PARAM_OP_VALID Entity* pOfficer, bool bScale)
{
	if (pOfficer)
	{
		Entity* pEnt = entActivePlayerPtr();
		S32 iRank = Officer_GetRank(pOfficer);
		OfficerRankDef* pOfficerRankDef = Officer_GetRankDef(iRank+1,pEnt?GET_REF(pEnt->hAllegiance):NULL,pEnt?GET_REF(pEnt->hSubAllegiance):NULL);

		if (pOfficerRankDef)
		{
			F32 fScale = 1.0f;
			if (bScale)
			{
				ItemDef* pItemDef = item_DefFromName(pOfficerRankDef->pchCostNumeric);
				if (pItemDef)
				{
					fScale = pItemDef->fScaleUI;
				}
			}
			return pOfficerRankDef->iCost * fScale;
		}
	}
	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerGetTrainingCostNumeric");
const char* OfficerExpr_OfficerGetTrainingCostNumeric(SA_PARAM_OP_VALID Entity* pOfficer)
{
	if ( pOfficer )
	{
		Entity* pEnt = entActivePlayerPtr();
		S32 iRank = Officer_GetRank(pOfficer);
		OfficerRankDef* pOfficerRankDef = Officer_GetRankDef(iRank,pEnt?GET_REF(pEnt->hAllegiance):NULL,pEnt?GET_REF(pEnt->hSubAllegiance):NULL);

		if ( pOfficerRankDef )
			return pOfficerRankDef->pchTrainingNumeric;
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerGetTrainingCost");
S32 OfficerExpr_OfficerGetTrainingCost(SA_PARAM_OP_VALID Entity* pOfficer)
{
	if ( pOfficer )
	{
		Entity* pEnt = entActivePlayerPtr();
		S32 iRank = Officer_GetRank(pOfficer);
		OfficerRankDef* pOfficerRankDef = Officer_GetRankDef(iRank,pEnt?GET_REF(pEnt->hAllegiance):NULL,pEnt?GET_REF(pEnt->hSubAllegiance):NULL);

		if ( pOfficerRankDef )
			return pOfficerRankDef->iTrainingCost;
	}
	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerGetMaxAllowedPets");
S32 OfficerExpr_GetRequiredCostToTrainOfficer(SA_PARAM_OP_VALID Entity* pOfficer)
{
	S32 iResult = 0;
	Entity* pEnt = entActivePlayerPtr();
	if (pOfficer) {
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		iResult = Officer_GetMaxAllowedPets( pOfficer,pEnt?GET_REF(pEnt->hAllegiance):NULL, pExtract );
	}
	return iResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerGetRankCount");
S32 OfficerExpr_GetRankCount(SA_PARAM_OP_VALID Entity* pOfficer)
{
	AllegianceDef* pAllegianceDef = pOfficer ? allegiance_GetOfficerPreference(GET_REF(pOfficer->hAllegiance), GET_REF(pOfficer->hSubAllegiance)) : NULL;
	return pAllegianceDef ? Officer_GetRankCount(pAllegianceDef) : 0;
}

AUTO_STRUCT;
typedef struct OfficerSkillCategory
{
	DisplayMessage	DisplayMsg;		AST(NAME(DisplayString) STRUCT(parse_DisplayMessage))
	const char**	ppchNodes;		AST(NAME(Node) POOL_STRING)
} OfficerSkillCategory;

AUTO_STRUCT;
typedef struct OfficerSkillAllegiance
{
	REF_TO(AllegianceDef)	hAllegiance;	AST(NAME(Allegiance) REFDICT(Allegiance) STRUCTPARAM)
	OfficerSkillCategory**	eaCategories;	AST(NAME(Category))
	const char*				pchFilename;	AST(CURRENTFILE)
} OfficerSkillAllegiance;

AUTO_STRUCT;
typedef struct OfficerSkillCategoryList
{
	OfficerSkillAllegiance**	eaAllegiances;	AST(NAME(Allegiance))
	const char*					pchFilename;	AST(CURRENTFILE)
} OfficerSkillCategoryList;

AUTO_STRUCT;
typedef struct OfficerSkillCategoryUI
{
	PTNodeUICategory eCategory; AST(KEY)
	const char*	pchDisplayName;
	PowerListNode** eaNodeList;
	S32 iListSize;
	S32 iRowCount;
	S32 iColumnCount;
	bool bAnyAvailable;
} OfficerSkillCategoryUI;

AUTO_STRUCT;
typedef struct OfficerSkillData
{
	OfficerSkillCategoryUI** eaCategoryList;
	char* pchTrees;
	U32 iLastUseTime;
	bool bReset; AST(NAME(reset))
	bool bAnyAvailable;
} OfficerSkillData;

static OfficerSkillData** s_ppOfficerSkillData = NULL;

static OfficerSkillData* gclOfficer_GetTreeSkillData(const char *pchTrees)
{
	S32 i, iBest = -1;

	if (!pchTrees)
		pchTrees = "";

	for (i = eaSize(&s_ppOfficerSkillData) - 1; i >= 0; --i)
	{
		// Find unused one
		if (s_ppOfficerSkillData[i]->iLastUseTime < gGCLState.totalElapsedTimeMs)
		{
			if (iBest < 0 || s_ppOfficerSkillData[i]->iLastUseTime < s_ppOfficerSkillData[iBest]->iLastUseTime)
				iBest = i;
		}
		if (!stricmp(pchTrees, s_ppOfficerSkillData[i]->pchTrees))
			break;
	}

	if (i < 0)
	{
		if (iBest < 0)
		{
			OfficerSkillData *pNewSkillData = StructCreate(parse_OfficerSkillData);
			eaIndexedEnable(&pNewSkillData->eaCategoryList, parse_OfficerSkillCategoryUI);
			i = eaSize(&s_ppOfficerSkillData);
			eaPush(&s_ppOfficerSkillData, pNewSkillData);
		}
		else
		{
			i = iBest;
		}
		if (s_ppOfficerSkillData[i]->pchTrees)
			StructFreeString(s_ppOfficerSkillData[i]->pchTrees);
		s_ppOfficerSkillData[i]->pchTrees = StructAllocString(pchTrees);
		s_ppOfficerSkillData[i]->bReset = true;
	}

	s_ppOfficerSkillData[i]->iLastUseTime = gGCLState.totalElapsedTimeMs;
	return s_ppOfficerSkillData[i];
}

static bool gclAddNodeToOfficerSkillList( PowerNodeFilterCallbackData* pData, OfficerSkillCategoryUI* pCategory )
{
	PowerListNode *pListNode = eaGetStruct(&pCategory->eaNodeList, parse_PowerListNode, pCategory->iListSize++);
	FillPowerListNodeFromFilterData( pData, pListNode );
	return true;
}

static void gclOfficer_UpdateSkillCategoryList( OfficerSkillData* pOfficerSkillData,
												SA_PARAM_OP_VALID Entity* pOfficer,
												SA_PARAM_OP_VALID Entity* pFakeEnt,
												const char* pchAttribsAffectingPowerDef,
												S32 iFilterMask, const char* pchTextFilter,
												S32 iRankStart, S32 iRankCount,
												const char* pchPowerTrees)
{
	static OfficerSkillCategoryUI** s_eaCategories = NULL;
	static PowerListNode EmptyListNode = {0};
	S32 i, j, k;
	Entity* pEnt = entActivePlayerPtr();
	AllegianceDef* pAllegiance = pEnt ? allegiance_GetOfficerPreference(GET_REF(pEnt->hAllegiance), GET_REF(pEnt->hSubAllegiance)) : NULL;
	PowerDef* pAttribsAffectingPowerDef = NULL;
	S32 iPowerTreeBufferLength = pchPowerTrees && *pchPowerTrees ? strlen(pchPowerTrees) + 1 : 0;
	char* pchPowerTreeBuffer = iPowerTreeBufferLength ? (char*) alloca(iPowerTreeBufferLength) : NULL;

	if ( !devassertmsg(pOfficerSkillData, "Caller needs to create OfficerSkillData") )
	{
		return;
	}

	if ( pAllegiance==NULL )
	{
		StructReset(parse_OfficerSkillData, pOfficerSkillData);
		return;
	}

	iRankCount = iRankCount >= 0 ? iRankCount : Officer_GetRankCount( pAllegiance );

	if ( !EmptyListNode.bIsEmpty )
	{
		EmptyListNode.bIsEmpty = true;
	}

	if ( pEnt==NULL || pFakeEnt==NULL || pFakeEnt->pChar==NULL )
	{
		StructReset(parse_OfficerSkillData, pOfficerSkillData);
		return;
	}

	if (pchAttribsAffectingPowerDef && pchAttribsAffectingPowerDef[0])
	{
		pAttribsAffectingPowerDef = RefSystem_ReferentFromString("PowerDef", pchAttribsAffectingPowerDef);
	}

	for ( i = eaSize(&s_eaCategories)-1; i >= 0; i-- )
	{
		s_eaCategories[i]->iListSize = 0;
	}

	if ( eaIndexedGetTable(&s_eaCategories)==NULL )
	{
		eaIndexedEnable(&s_eaCategories, parse_OfficerSkillCategoryUI);
	}

	for ( i = eaSize(&pFakeEnt->pChar->ppPowerTrees)-1; i >= 0; i-- )
	{
		PowerTreeDef* pTreeDef = GET_REF(pFakeEnt->pChar->ppPowerTrees[i]->hDef);

		if ( pTreeDef )
		{
			if ( pchPowerTreeBuffer )
			{
				char* pchToken;
				char* pchContext;
				bool bMatch = false;

				strcpy_s(pchPowerTreeBuffer, iPowerTreeBufferLength, pchPowerTrees);

				for (pchToken = strtok_r(pchPowerTreeBuffer, " \r\n\t,%|", &pchContext);
					 !bMatch && pchToken != NULL;
					 pchToken = strtok_r(NULL, " \r\n\t,%|", &pchContext))
				{
					bool bNot = false;

					if ( pchToken[0] == '\0' )
						continue;

					if ( pchToken[0] == '!' )
					{
						bNot = true;
						pchToken++;
					}

					if ( pchToken[0] == '=' )
						bMatch = stricmp(pTreeDef->pchName, pchToken+1) == 0;
					else
						bMatch = strstri(pTreeDef->pchName, pchToken) != NULL;

					if ( bNot && bMatch )
					{
						bMatch = false;
						break;
					}
				}

				if ( !bMatch )
					continue;
			}

			for ( j = eaSize(&pTreeDef->ppGroups)-1; j >= 0; j-- )
			{
				PTGroupDef* pGroupDef = pTreeDef->ppGroups[j];
				for ( k = eaSize(&pGroupDef->ppNodes)-1; k >= 0; k-- )
				{
					PTNodeDef* pNodeDef = pGroupDef->ppNodes[k];
					OfficerSkillCategoryUI* pCategory;

					if ( pNodeDef->eUICategory == kPTNodeUICategory_None )
						continue;

					pCategory = eaIndexedGetUsingInt(&s_eaCategories, pNodeDef->eUICategory);

					if ( pCategory==NULL )
					{
						pCategory = StructCreate( parse_OfficerSkillCategoryUI );
						pCategory->eCategory = pNodeDef->eUICategory;
						eaPush(&s_eaCategories, pCategory);
					}

					gclPowerNodePassesFilter(pOfficer, pFakeEnt, pGroupDef, pNodeDef,
											 pAttribsAffectingPowerDef,
											 iFilterMask, pchTextFilter,
											 gclAddNodeToOfficerSkillList, pCategory);
				}
			}
		}
	}

	for ( i = eaSize(&s_eaCategories)-1; i >= 0; i-- )
	{
		OfficerSkillCategoryUI* pCategory = s_eaCategories[i];

		if ( pCategory->iListSize == 0 )
		{
			S32 iRemoved = eaIndexedFindUsingInt(&pOfficerSkillData->eaCategoryList, pCategory->eCategory);
			OfficerSkillCategoryUI* pRemoved = eaGet(&pOfficerSkillData->eaCategoryList, iRemoved);
			if ( pRemoved )
			{
				for ( j = eaSize(&pRemoved->eaNodeList)-1; j >= 0; j-- )
				{
					pRemoved->eaNodeList[j] = NULL;
				}
				StructDestroy( parse_OfficerSkillCategoryUI, pRemoved );
				eaRemove(&pOfficerSkillData->eaCategoryList, iRemoved);
			}
		}

		eaSetSizeStruct(&pCategory->eaNodeList, parse_PowerListNode, pCategory->iListSize);
		eaQSort(pCategory->eaNodeList, SortPowerListNodeByPurpose);
	}

	for ( i = 0; i < eaSize(&s_eaCategories); i++ )
	{
		S32 iMaxRankSkillCount = 0;
		S32 iCurrentRankSkillMax = 0;
		S32 iRankIndex = 0;
		S32 iCurrentRank = -1;
		OfficerSkillCategoryUI* pCategory;

		if ( s_eaCategories[i]->iListSize == 0 )
		{
			continue;
		}

		pCategory = eaIndexedGetUsingInt(&pOfficerSkillData->eaCategoryList, s_eaCategories[i]->eCategory);
		if ( pCategory==NULL )
		{
			pCategory = StructCreate( parse_OfficerSkillCategoryUI );
			pCategory->eCategory = s_eaCategories[i]->eCategory;
			pCategory->pchDisplayName = StructAllocString(StaticDefineGetTranslatedMessage(PTNodeUICategoryEnum, pCategory->eCategory));
			eaPush(&pOfficerSkillData->eaCategoryList,pCategory);
		}

		//Find max rank count
		for ( j = 0; j < eaSize(&s_eaCategories[i]->eaNodeList); j++ )
		{
			PowerListNode* pListNode = s_eaCategories[i]->eaNodeList[j];
			PTNodeDef* pNodeDef = GET_REF(pListNode->hNodeDef);
			PowerDef* pPowerDef = GET_REF(pListNode->hPowerDef);
			PowerPurpose ePurpose = kPowerPurpose_Uncategorized;
			S32 iRank;

			if ( pPowerDef )
			{
				ePurpose = pPowerDef->ePurpose;
			}
			else if ( pNodeDef )
			{
				ePurpose = pNodeDef->ePurpose;
			}

			iRank = ePurpose-1;

			if ( iRank==-1 || iRank < iCurrentRank )
				continue;

			if ( iRank != iCurrentRank )
			{
				if ( iMaxRankSkillCount < iCurrentRankSkillMax )
					iMaxRankSkillCount = iCurrentRankSkillMax;
				iCurrentRankSkillMax = 0;
				iCurrentRank = iRank;
			}
			iCurrentRankSkillMax++;
		}
		if ( iMaxRankSkillCount < iCurrentRankSkillMax )
			iMaxRankSkillCount = iCurrentRankSkillMax;

		pCategory->iColumnCount = iRankCount;
		pCategory->iRowCount = iMaxRankSkillCount;

		for ( j = eaSize(&pCategory->eaNodeList)-1; j >= 0; j-- )
		{
			pCategory->eaNodeList[j] = NULL;
		}
		eaSetSize(&pCategory->eaNodeList, pCategory->iRowCount * pCategory->iColumnCount);
		pCategory->iListSize = eaSize(&pCategory->eaNodeList);
		for ( j = eaSize(&pCategory->eaNodeList)-1; j >= 0; j-- )
		{
			pCategory->eaNodeList[j] = &EmptyListNode;
		}

		for ( j = 0; j < eaSize(&s_eaCategories[i]->eaNodeList); j++ )
		{
			PowerListNode* pListNode = s_eaCategories[i]->eaNodeList[j];
			PTNodeDef* pListNodeDef = GET_REF(pListNode->hNodeDef);
			PowerDef* pListPowerDef = GET_REF(pListNode->hPowerDef);
			PowerPurpose ePurpose = kPowerPurpose_Uncategorized;
			S32 iRank, iListNodeIndex;

			if ( pListPowerDef )
			{
				ePurpose = pListPowerDef->ePurpose;
			}
			else if ( pListNodeDef )
			{
				ePurpose = pListNodeDef->ePurpose;
			}

			iRank = ePurpose-1;

			if ( iRank < iRankStart || iRank >= iRankStart + iRankCount )
				continue;

			if ( iCurrentRank != iRank )
			{
				iCurrentRank = iRank;
				iRankIndex = 0;
			}

			iListNodeIndex = (iCurrentRank-iRankStart) + iRankIndex * iRankCount;

			if ( eaGet(&pCategory->eaNodeList, iListNodeIndex) == &EmptyListNode )
			{
				eaSet(&pCategory->eaNodeList, StructClone(parse_PowerListNode, pListNode), iListNodeIndex);
			}

			iRankIndex++;
		}
	}

	for ( i = 0; i < eaSize(&s_eaCategories); i++ )
	{
		OfficerSkillCategoryUI* pCategory = s_eaCategories[i];
		for ( j = 0; j < eaSize(&pCategory->eaNodeList); j++ )
		{
			if ( pCategory->eaNodeList[j] == &EmptyListNode )
			{
				pCategory->eaNodeList[j] = NULL;
			}
		}
	}

	pOfficerSkillData->bAnyAvailable = false;
	for ( i = 0; i < eaSize(&pOfficerSkillData->eaCategoryList); i++ )
	{
		OfficerSkillCategoryUI* pCategory = pOfficerSkillData->eaCategoryList[i];
		pCategory->bAnyAvailable = false;
		for ( j = 0; j < eaSize(&pCategory->eaNodeList); j++ )
		{
			if ( pCategory->eaNodeList[j] == &EmptyListNode )
			{
				pCategory->eaNodeList[j] = StructClone(parse_PowerListNode, &EmptyListNode);
			}
			else if ( !pCategory->bAnyAvailable )
			{
				pCategory->bAnyAvailable = pCategory->eaNodeList[j]->bIsAvailable;
			}
		}
		if ( !pOfficerSkillData->bAnyAvailable )
		{
			pOfficerSkillData->bAnyAvailable = pCategory->bAnyAvailable;
		}
	}
	pOfficerSkillData->bReset = false;
}

static void gclOfficer_UpdateSkillCategoryListPointers(OfficerSkillData* pOfficerSkillData,
													   SA_PARAM_OP_VALID Entity* pOfficer, SA_PARAM_OP_VALID Entity* pFakeEnt)
{
	int i, j;
	for (i = eaSize(&pOfficerSkillData->eaCategoryList)-1; i >= 0; i--)
	{
		OfficerSkillCategoryUI* pCategory = pOfficerSkillData->eaCategoryList[i];
		for (j = eaSize(&pCategory->eaNodeList)-1; j >= 0; j--)
		{
			PowerListNode* pListNode = pCategory->eaNodeList[j];
			PowerTreeDef* pTreeDef = GET_REF(pListNode->hTreeDef);
			PTGroupDef* pGroupDef = GET_REF(pListNode->hGroupDef);
			PTNodeDef* pNodeDef = GET_REF(pListNode->hNodeDef);

			FillPowerListNodeForEnt(pOfficer, pFakeEnt, pListNode, NULL, pTreeDef, NULL, pGroupDef, NULL, pNodeDef);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenOfficerGetSkillsForCategory");
void OfficerExpr_GenOfficerGetSkillsForCategory(SA_PARAM_NN_VALID UIGen* pGen, S32 iCategory)
{
	OfficerSkillData* pOfficerSkillData = gclOfficer_GetTreeSkillData(NULL);
	OfficerSkillCategoryUI* pCategory = eaIndexedGetUsingInt(&pOfficerSkillData->eaCategoryList, iCategory);

	if ( pCategory==NULL )
	{
		ui_GenSetManagedListSafe(pGen, NULL, PowerListNode, false);
		return;
	}

	ui_GenSetManagedListSafe(pGen, &pCategory->eaNodeList, PowerListNode, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenOfficerGetSkillsFromCategory");
void OfficerExpr_GenOfficerGetSkillsFromCategory(SA_PARAM_NN_VALID UIGen* pGen, OfficerSkillCategoryUI* pCategory)
{
	if ( pCategory==NULL )
	{
		ui_GenSetManagedListSafe(pGen, NULL, PowerListNode, false);
		return;
	}

	ui_GenSetManagedListSafe(pGen, &pCategory->eaNodeList, PowerListNode, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenOfficerGetSkillCategories");
void OfficerExpr_GenOfficerGetSkillCategories(SA_PARAM_NN_VALID UIGen* pGen,
											  SA_PARAM_OP_VALID Entity* pOfficer, SA_PARAM_OP_VALID Entity* pFakeEnt,
											  const char* pchAttribsAffectingPowerDef,
											  S32 iFilterMask, const char* pchTextFilter,
											  S32 iRankStart, S32 iRankCount,
											  bool bRegenList)
{
	OfficerSkillData* pOfficerSkillData = gclOfficer_GetTreeSkillData(NULL);
	if (bRegenList || pOfficerSkillData->bReset)
	{
		gclOfficer_UpdateSkillCategoryList(pOfficerSkillData,
										   pOfficer, pFakeEnt, pchAttribsAffectingPowerDef,
										   iFilterMask, pchTextFilter, iRankStart, iRankCount,
										   NULL);
	}
	else // Update unowned pointers
	{
		gclOfficer_UpdateSkillCategoryListPointers(pOfficerSkillData, pOfficer, pFakeEnt);
	}
	ui_GenSetManagedListSafe(pGen, &pOfficerSkillData->eaCategoryList, OfficerSkillCategoryUI, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenOfficerGetSkillTreeCategories");
void OfficerExpr_GenOfficerGetSkillTreeCategories(SA_PARAM_NN_VALID UIGen* pGen, 
											  SA_PARAM_OP_VALID Entity* pOfficer, SA_PARAM_OP_VALID Entity* pFakeEnt, 
											  const char* pchAttribsAffectingPowerDef,
											  S32 iFilterMask, const char* pchTextFilter,
											  S32 iRankStart, S32 iRankCount,
											  bool bRegenList,
											  const char* pchPowerTrees)
{
	OfficerSkillData* pOfficerSkillData = gclOfficer_GetTreeSkillData(pchPowerTrees);
	if (bRegenList || pOfficerSkillData->bReset)
	{
		gclOfficer_UpdateSkillCategoryList(pOfficerSkillData,
										   pOfficer, pFakeEnt, pchAttribsAffectingPowerDef,
										   iFilterMask, pchTextFilter, iRankStart, iRankCount,
										   pchPowerTrees);
	}
	else // Update unowned pointers
	{
		gclOfficer_UpdateSkillCategoryListPointers(pOfficerSkillData, pOfficer, pFakeEnt);
	}
	ui_GenSetManagedListSafe(pGen, &pOfficerSkillData->eaCategoryList, OfficerSkillCategoryUI, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OfficerSkillTreeIsAnyAvailable");
bool OfficerExpr_OfficerSkillTreeIsAnyAvailable(const char* pchPowerTrees)
{
	OfficerSkillData* pOfficerSkillData = gclOfficer_GetTreeSkillData(pchPowerTrees);
	return pOfficerSkillData->bAnyAvailable;
}

AUTO_STRUCT;
typedef struct OfficerRankData
{
	const char*		pchDisplayName;	AST(UNOWNED)
	OfficerRankDef* pRankDef;		AST(UNOWNED)
} OfficerRankData;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenOfficerGetRankList");
void OfficerExpr_GenOfficerGetRankList(	SA_PARAM_NN_VALID UIGen* pGen,
										SA_PARAM_OP_VALID Entity* pOfficer,
										S32 iRankStart, S32 iRankCount,
										bool bRegenList)
{
	OfficerRankData*** peaData = ui_GenGetManagedListSafe(pGen, OfficerRankData);
	Entity* pEnt = entActivePlayerPtr();
	AllegianceDef* pAllegiance = pOfficer ? GET_REF(pOfficer->hAllegiance) : NULL;
	AllegianceDef* pSubAllegiance = pOfficer ? GET_REF(pOfficer->hSubAllegiance) : NULL;
	S32 i;

	if ( pEnt != NULL && pAllegiance != NULL )
	{
		iRankCount = iRankCount >= 0 ? iRankCount : Officer_GetRankCount( pAllegiance );
		eaSetSizeStruct(peaData, parse_OfficerRankData, iRankCount);

		for ( i = 0; i < iRankCount; i++ )
		{
			OfficerRankDef* pRankDef = Officer_GetRankDef( iRankStart + i, pAllegiance, pSubAllegiance );
			OfficerRankData* pData = eaGetStruct( peaData, parse_OfficerRankData, i );
			if ( pRankDef )
			{
				pData->pchDisplayName = entTranslateDisplayMessage( pEnt, *pRankDef->pDisplayMessage );
				pData->pRankDef = pRankDef;
			}
			else
			{
				pData->pchDisplayName = NULL;
				pData->pRankDef = NULL;
			}
		}
	}
	else
	{
		eaSetSizeStruct(peaData, parse_OfficerRankData, 0);
	}

	ui_GenSetManagedListSafe(pGen, peaData, OfficerRankData, true);
}

#include "AutoGen/OfficerUI_c_ast.c"

