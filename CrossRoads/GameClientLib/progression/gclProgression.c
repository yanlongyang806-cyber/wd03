/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "progression_common.h"
#include "AutoGen/progression_common_h_ast.h"
#include "gclLogin.h"
#include "net.h"
#include "gclSendToServer.h"
#include "gclEntity.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(9) ACMD_NAME(ProgressionCompleteThroughNode) ACMD_HIDE;
void gclProgression_cmd_DebugCompleteThroughNode(ACMD_NAMELIST("GameProgressionNodeDef", REFDICTIONARY) const char *pchNodeName)
{
	if (linkConnected(gpLoginLink))
	{
		Entity *pEnt = entActiveOrSelectedPlayer();

		if (pEnt)
		{
			// We're connected to the login server
			GameProgressionNodeRef *pRef = StructCreate(parse_GameProgressionNodeRef);		
			Packet *pPak = pktCreate(gpLoginLink, TOLOGIN_PROGRESSION_COMPLETE_THROUGH_NODE);

			SET_HANDLE_FROM_STRING(g_hGameProgressionNodeDictionary, pchNodeName, pRef->hDef);

			pktSendStruct(pPak, pRef, parse_GameProgressionNodeRef);

			pktSend(&pPak);
		}
	}
	else if (gclServerIsConnected())
	{
		// We're connected to the game server
		ServerCmd_ProgressionCompleteThroughNode(pchNodeName);
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(9) ACMD_NAME(ProgressionUncompleteThroughNode) ACMD_HIDE;
void gclProgression_cmd_DebugUncompleteThroughNode(ACMD_NAMELIST("GameProgressionNodeDef", REFDICTIONARY) const char *pchNodeName)
{
	if (linkConnected(gpLoginLink))
	{
		Entity *pEnt = entActiveOrSelectedPlayer();

		if (pEnt)
		{
			// We're connected to the login server
			GameProgressionNodeRef *pRef = StructCreate(parse_GameProgressionNodeRef);		
			Packet *pPak = pktCreate(gpLoginLink, TOLOGIN_PROGRESSION_UNCOMPLETE_THROUGH_NODE);

			SET_HANDLE_FROM_STRING(g_hGameProgressionNodeDictionary, pchNodeName, pRef->hDef);

			pktSendStruct(pPak, pRef, parse_GameProgressionNodeRef);

			pktSend(&pPak);
		}
	}
	else if (gclServerIsConnected())
	{
		// We're connected to the game server
		ServerCmd_ProgressionUncompleteThroughNode(pchNodeName);
	}
}