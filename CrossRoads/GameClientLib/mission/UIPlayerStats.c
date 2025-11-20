
/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "playerstats_common.h"
#include "Expression.h"
#include "UIGen.h"
#include "referencesystem.h"
#include "earray.h"
#include "Entity.h"
#include "Player.h"

#include "autogen/UIGen_h_ast.h"
#include "autogen/playerstats_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct PlayerStatDefHolder
{
	REF_TO(PlayerStatDef) hStatDef;
}PlayerStatDefHolder;

PlayerStatDefHolder **g_ppPlayerStatHolders = NULL;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayerStatsLoadDefs);
void exprPlayerStatsLoadDefs(SA_PARAM_NN_VALID Entity *pPlayerEnt)
{
	int i;

	if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pStatsInfo)
	{
		for(i=0;i<eaSize(&pPlayerEnt->pPlayer->pStatsInfo->eaPlayerStats);i++)
		{
			int n;
			PlayerStat *pStat = pPlayerEnt->pPlayer->pStatsInfo->eaPlayerStats[i];
			PlayerStatDefHolder *pNewHolder = NULL;

			if(RefSystem_ReferentFromString("PlayerStatDef",pStat->pchStatName))
				continue;

			for(n=eaSize(&g_ppPlayerStatHolders)-1;n>=0;n--)
			{
				if(strcmp(REF_STRING_FROM_HANDLE(g_ppPlayerStatHolders[n]->hStatDef),pStat->pchStatName) == 0)
					break;
			}

			if(n!=-1)
				continue;

			pNewHolder = malloc(sizeof(PlayerStatDefHolder));
			SET_HANDLE_FROM_STRING("PlayerStatDef",pStat->pchStatName,pNewHolder->hStatDef);
			eaPush(&g_ppPlayerStatHolders,pNewHolder);
		}
	}
	
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayerStatsFromCategory);
void exprPlayerStatsFromCategory(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_NN_VALID Entity *pPlayerEnt, S32 eCategory)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	PlayerStat **ppPlayerStats = NULL;

	eaCreate(&ppPlayerStats);
	
	if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pStatsInfo)
	{
		int i;

		for(i=0;i<eaSize(&pPlayerEnt->pPlayer->pStatsInfo->eaPlayerStats);i++)
		{
			PlayerStat *pStat = pPlayerEnt->pPlayer->pStatsInfo->eaPlayerStats[i];
			PlayerStatDef *pDef = RefSystem_ReferentFromString("PlayerStatDef",pStat->pchStatName);

			if(pDef && (eCategory == -1 || pDef->eCategory == eCategory))
			{
				eaPush(&ppPlayerStats,pStat);
			}
		}
		
	}

	ui_GenSetManagedListSafe(pGen,&ppPlayerStats, PlayerStat,false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayerStatGetDisplayName);
const char *exprPlayerStatGetDisplayname(const char *pchPlayerStat)
{
	PlayerStatDef *pDef = RefSystem_ReferentFromString("PlayerStatDef",pchPlayerStat);

	if(pDef)
		return TranslateDisplayMessage(pDef->displayNameMsg);
	else
		return "(Invalid Player Stat)";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayerStatGetDescription);
const char *exprPlayerStatGetDescription(const char *pchPlayerStat)
{
	PlayerStatDef *pDef = RefSystem_ReferentFromString("PlayerStatDef",pchPlayerStat);

	if(pDef)
		return TranslateDisplayMessage(pDef->descriptionMsg);
	else
		return "(Invalid Player Stat)";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayerStatFromName);
SA_RET_OP_VALID PlayerStat *exprPlayerStatFromName(SA_PARAM_NN_VALID Entity *pPlayerEnt, const char *pchPlayerStat)
{
	PlayerStatDef *pDef = RefSystem_ReferentFromString("PlayerStatDef",pchPlayerStat);
	if(pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pStatsInfo)
	{
		int i;

		for(i=0;i<eaSize(&pPlayerEnt->pPlayer->pStatsInfo->eaPlayerStats);i++)
		{
			PlayerStat *pStat = pPlayerEnt->pPlayer->pStatsInfo->eaPlayerStats[i];
			if(RefSystem_ReferentFromString("PlayerStatDef",pStat->pchStatName) == pDef)
				return pStat;
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayerStatValueFromName);
int exprPlayerStatValueFromName(SA_PARAM_NN_VALID Entity *pPlayerEnt, const char *pchPlayerStat)
{
	PlayerStat *pStat = exprPlayerStatFromName(pPlayerEnt,pchPlayerStat);

	if(pStat)
		return pStat->uValue;

	return 0;
}