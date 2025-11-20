/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "progressionui_eval.h"
#include "progression_common.h"
#include "progression_transact.h"
#include "gclEntity.h"
#include "Team.h"
#include "LobbyCommon.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_EXPR_FUNC(UIGen) ACMD_ACCESSLEVEL(0) ACMD_NAME("ProgressionSetProgression");
void exprProgressionSetProgression(SA_PARAM_OP_STR const char *pchNodeName, bool bWarpToSpawnPoint)
{
	GameProgressionNodeDef *pNode = pchNodeName && pchNodeName[0] ? progression_NodeDefFromName(pchNodeName) : NULL;
	if (pNode && g_GameProgressionConfig.bAllowReplay)
	{
		ServerCmd_SetProgressionByGameProgressionNode(pNode->pchName, bWarpToSpawnPoint);
	}	
}

AUTO_EXPR_FUNC(UIGen) ACMD_ACCESSLEVEL(0) ACMD_NAME("ProgressionSetTeamProgression");
void exprProgressionSetTeamProgression(SA_PARAM_OP_STR const char *pchNodeName, bool bWarpToSpawnPoint)
{
	GameProgressionNodeDef *pNode = pchNodeName && pchNodeName[0] ? (GameProgressionNodeDef *)RefSystem_ReferentFromString(g_hGameProgressionNodeDictionary, pchNodeName) : NULL;
	if (pNode && g_GameProgressionConfig.bAllowReplay && g_GameProgressionConfig.bEnableTeamProgressionTracking)
	{
		ServerCmd_SetTeamProgressionByGameProgressionNode(pNode->pchName, bWarpToSpawnPoint);
	}	
}

bool bAutoShowTeamProgressionSetter = true;
AUTO_CMD_INT(bAutoShowTeamProgressionSetter, AutoShowTeamProgressionSetter) ACMD_COMMANDLINE;

AUTO_EXPR_FUNC(UIGen) ACMD_ACCESSLEVEL(0) ACMD_NAME("ProgressionShouldShowTeamProgressSetter");
bool exprProgressionShouldShowTeamProgressSetter(void)
{
	Entity *pEnt = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pEnt);	 

	if (bAutoShowTeamProgressionSetter && pTeam && team_IsTeamLeader(pEnt))
	{
		// Get the progression information
		TeamProgressionData* pTeamData = progression_GetCurrentTeamProgress(pTeam);

		if (!pTeamData || !IS_HANDLE_ACTIVE(pTeamData->hNode))
		{
			return true;
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ProgressIsCurrentTeamProgress);
bool exprProgressIsCurrentTeamProgress(SA_PARAM_OP_VALID GameContentNode *pNode)
{
	GameProgressionNodeDef *pGameProgressionNode;	
	Entity *pEnt = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pEnt);

	if (pTeam && (pGameProgressionNode = GET_REF(pNode->contentRef.hNode)))
	{
		TeamProgressionData* pData = progression_GetCurrentTeamProgress(pTeam);
		return pData && GET_REF(pData->hNode) == pGameProgressionNode;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ProgressGetCurrentTeamProgress);
SA_RET_OP_VALID GameProgressionNodeDef * exprProgressGetCurrentTeamProgress(void)
{
	Entity *pEnt = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pEnt);

	if (pTeam)
	{
		TeamProgressionData* pData = progression_GetCurrentTeamProgress(pTeam);

		if (pData)
		{
			return GET_REF(pData->hNode);
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ProgressGetNodeSummary");
SA_RET_OP_STR const char * exprProgressGetNodeSummary(SA_PARAM_OP_VALID GameProgressionNodeDef *pNode)
{
	const char *pchReturn = "";

	if (pNode)
	{
		pchReturn = TranslateMessageRefDefault(pNode->msgSummary.hMessage, pNode->pchName);
	}

	if (pchReturn == NULL)
	{
		pchReturn = "";
	}

	return pchReturn;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ProgressGetNodeDisplayName");
SA_RET_OP_STR const char * exprProgressGetNodeDisplayName(SA_PARAM_OP_VALID GameProgressionNodeDef *pNode)
{
	const char *pchReturn = "";

	if (pNode)
	{
		pchReturn = TranslateMessageRefDefault(pNode->msgDisplayName.hMessage, pNode->pchName);
	}

	if (pchReturn == NULL)
	{
		pchReturn = "";
	}

	return pchReturn;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ProgressGetNodeArtFileName");
SA_RET_OP_STR const char * exprProgressGetNodeArtFileName(SA_PARAM_OP_VALID GameProgressionNodeDef *pNode)
{
	const char *pchReturn = "";

	if (pNode)
	{
		pchReturn = pNode->pchArtFileName;
	}

	if (pchReturn == NULL)
	{
		pchReturn = "";
	}

	return pchReturn;
}

AUTO_EXPR_FUNC(UIGen) ACMD_ACCESSLEVEL(0) ACMD_NAME("ProgressionGetProgressionNodeByName");
SA_RET_OP_VALID GameProgressionNodeDef * exprProgressionGetProgressionNodeByName(SA_PARAM_OP_STR const char *pchNodeName)
{
	return progression_NodeDefFromName(pchNodeName);
}