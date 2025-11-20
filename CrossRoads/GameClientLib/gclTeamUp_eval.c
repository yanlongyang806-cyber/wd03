/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#include "TeamUpCommon.h"
#include "Entity.h"
#include "player.h"
#include "gclEntity.h"
#include "Message.h"
#include "GameStringFormat.h"
#include "Character.h"
#include "UIGen.h"
#include "TeamUI.h"

#include "AutoGen/TeamUpCommon_h_ast.h"

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(TeamUp_HasTeamUpRequest);
U32 TeamUpExpr_HasTeamUpRequest()
{
	Entity *pPlayer = entActivePlayerPtr();

	if(pPlayer && pPlayer->pTeamUpRequest)
		return 1;

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(TeamUp_PlayerHasStatus);
U32 TeamUpExpr_PlayerHasStatus(const char *pchStatus)
{
	TeamUpState eState = StaticDefineIntGetInt(TeamUpStateEnum,pchStatus);
	Entity *pPlayer = entActivePlayerPtr();

	if(pPlayer && pPlayer->pTeamUpRequest && eState != -1)
	{
		return pPlayer->pTeamUpRequest->eState == eState;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(TeamUp_GetDisplayName);
const char *TeamUpExpr_GetDisplayName()
{
	Entity *pPlayer = entActivePlayerPtr();

	if(pPlayer && pPlayer->pTeamUpRequest && IS_HANDLE_ACTIVE(pPlayer->pTeamUpRequest->msgDisplayMessage.hMessage))
	{
		return entTranslateDisplayMessage(pPlayer,pPlayer->pTeamUpRequest->msgDisplayMessage);
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(TeamUp_GetCurrentGroupIdx);
int TeamUpExpr_GetCurrentGroupIdx()
{
	Entity *pPlayer = entActivePlayerPtr();

	if(pPlayer && pPlayer->pTeamUpRequest)
		return pPlayer->pTeamUpRequest->iGroupIndex;

	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(TeamUp_GetMemberList);
void TeamUpExpr_GetMemberList(UIGen *pGen, int iGroupIdx, int bIncludePets)
{
	Entity *pPlayer = entActivePlayerPtr();

	TeamUp_GetMembers(pGen,pPlayer,iGroupIdx,bIncludePets,true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(TeamUp_GetGroupList);
void TeamUpExpr_GetGroupList(UIGen *pGen, int bIncludeMyGroup, int bInlcudeEmptyGroup)
{
	Entity *pPlayer = entActivePlayerPtr();

	TeamUp_GetGroups(pGen, pPlayer, bIncludeMyGroup, bInlcudeEmptyGroup);
}