/***************************************************************************
*     Copyright (c) 2005-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EmoteCommon.h"
#include "UIGen.h"

#include "gclEntity.h"

#include "Autogen/EmoteCommon_h_ast.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("EmoteCommon", BUDGET_GameSystems););

static Emote **g_eaEmotes = NULL;

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void emote_ReceiveEmotes(EmoteList *pList)
{
	eaCopyStructs(&pList->eaEmotes, &g_eaEmotes, parse_Emote);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(emote_ResetEmoteList);
void emote_ExprResetEmoteList(void)
{
	eaClearStruct(&g_eaEmotes, parse_Emote);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(emote_GetEmoteList);
void emote_ExprGetEmoteList(void)
{
	ServerCmd_emote_GetValidEmotes();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(emote_GetEmoteListAll);
void emote_ExprGetEmoteListAll(void)
{
	ServerCmd_emote_GetAllEmotes();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(emote_GetEmotes);
void emote_ExprGetEmotes(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &g_eaEmotes, parse_Emote);
}

#include "EmoteCommon_h_ast.c"
