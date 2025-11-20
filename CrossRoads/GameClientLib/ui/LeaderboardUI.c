/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Expression.h"
#include "Leaderboard.h"
#include "UIGen.h"

#include "AutoGen/Leaderboard_h_ast.h"
#include "autogen/UIGen_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/GameServerLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

typedef struct CurrentLeaderboard
{
	LeaderboardDef *pDef;
	LeaderboardPage *pPage;
}CurrentLeaderboard;

CurrentLeaderboard g_sCurrentLeaderboard = {0};

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetLeaderboards");
void GenExprGetAllLeaderboards(SA_PARAM_NN_VALID ExprContext *pContext)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	ui_GenSetManagedListSafe(pGen,&g_sLeaderboardDefs.ppLeaderboards, LeaderboardDef,false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("LeaderboardCount");
int GenExprGetLeaderboardCount(SA_PARAM_NN_VALID ExprContext *pContext)
{
	return eaSize(&g_sLeaderboardDefs.ppLeaderboards);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetLeaderboardName");
const char * GenExprGetLeaderboardName(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_OP_VALID LeaderboardDef *pDef)
{
	if(pDef)
		return TranslateDisplayMessage(pDef->pDisplayMessage);

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetLeaderboardDescription");
const char *GenExprGetLeaderboardDescription(SA_PARAM_NN_VALID ExprContext *pContext, LeaderboardDef *pDef)
{
	if(pDef)
		return TranslateDisplayMessage(pDef->pDescriptionMessage);

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetCurrentLeaderboard");
SA_RET_OP_VALID LeaderboardDef *GenExprGetCurrentLeaderboard(SA_PARAM_NN_VALID ExprContext *pContext)
{
	return g_sCurrentLeaderboard.pDef;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SetLeaderboard");
void GenExprSetLeaderboard(SA_PARAM_NN_VALID ExprContext *pContext, LeaderboardDef *pDef)
{
	LeaderboardPageRequest sRequest = {0};

	g_sCurrentLeaderboard.pDef = pDef;

	sRequest.pchLeaderboardKey = pDef->pchLeaderboardKey;
	sRequest.iInterval = -1;
	sRequest.iPageSearch = 0;
	sRequest.ePlayerSearch = 0;
	sRequest.iRankingsPerPage = 10;

	ServerCmd_LeaderboardPageRequest(&sRequest);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetLeaderboardPageRankings");
void GenExprGetLeaderboardPageRankings(SA_PARAM_NN_VALID ExprContext *pContext)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	if(g_sCurrentLeaderboard.pPage)
		ui_GenSetManagedListSafe(pGen,&g_sCurrentLeaderboard.pPage->ppEntries, LeaderboardPageEntry,false);
	else
		ui_GenSetManagedListSafe(pGen,NULL, LeaderboardPageEntry,false);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void setLeaderboardPage(LeaderboardPage *pPage)
{
	if(g_sCurrentLeaderboard.pPage)
		StructDestroy(parse_LeaderboardPage,g_sCurrentLeaderboard.pPage);

	g_sCurrentLeaderboard.pPage = StructClone(parse_LeaderboardPage,pPage);
}