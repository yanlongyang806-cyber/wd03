#include "gclEntity.h"
#include "mapstate_common.h"
#include "gclMapState.h"
#include "Message.h"
#include "Player.h"
#include "PlayerDifficultyCommon.h"
#include "Team.h"
#include "UIGen.h"
#include "WorldGrid.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_InstanceDifficultyApplied);
bool pd_exprInstanceDifficultyApplied(void)
{
	return pd_MapDifficultyApplied();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_GetInstanceDifficultyDescStr);
SA_RET_OP_VALID const char *pd_exprGetInstanceDifficultyDescStr(void)
{
	Message* pDescMsg = NULL;
	Entity* pPlayerEnt = entActivePlayerPtr();
	WorldRegionType eRegion = pPlayerEnt ? entGetWorldRegionTypeOfEnt(pPlayerEnt) : WRT_None;

	if (!pd_MapDifficultyApplied())
		return NULL;

	pDescMsg = pd_GetDifficultyDescMsg(mapState_GetDifficulty(mapStateClient_Get()), zmapInfoGetPublicName(NULL), eRegion);

	if(pDescMsg)
	{
		return TranslateMessagePtr(pDescMsg);
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_GetInstanceDifficultyStr);
SA_RET_OP_VALID const char *pd_exprGetInstanceDifficultyStr(void)
{
	Message* pDescMsg = NULL;
	Entity* pPlayerEnt = entActivePlayerPtr();
	WorldRegionType eRegion = pPlayerEnt ? entGetWorldRegionTypeOfEnt(pPlayerEnt) : WRT_None;

	if (!pd_MapDifficultyApplied())
		return NULL;

	pDescMsg = pd_GetDifficultyNameMsg(mapState_GetDifficulty(mapStateClient_Get()), zmapInfoGetPublicName(NULL), eRegion);

	if(pDescMsg)
	{
		return TranslateMessagePtr(pDescMsg);
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_GetInstanceDifficultyRow);
int pd_exprGetInstanceDifficultyRow(void)
{
	PlayerDifficulty *pDifficulty = NULL;

	if (!pd_MapDifficultyApplied())
		return -1;

	pDifficulty = pd_GetDifficulty(mapState_GetDifficulty(mapStateClient_Get()));
	if (!pDifficulty)
		return -1;

	return eaFind(&g_PlayerDifficultySet.peaPlayerDifficulties, pDifficulty);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_GetTeamDifficultyDescStr);
SA_RET_OP_VALID const char *pd_exprGetTeamDifficultyDescStr(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pPlayer);
	PlayerDifficulty *pDifficulty = pTeam ? pd_GetDifficulty(pTeam->iDifficulty) : NULL;

	if (!pDifficulty)
		return NULL;

	return TranslateMessageRef(pDifficulty->hDescription);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_GetTeamDifficultyStr);
SA_RET_OP_VALID const char *pd_exprGetTeamDifficultyStr(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pPlayer);
	PlayerDifficulty *pDifficulty = pTeam ? pd_GetDifficulty(pTeam->iDifficulty) : NULL;

	if (!pDifficulty)
		return NULL;

	return TranslateMessageRef(pDifficulty->hName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_GetTeamDifficultyRow);
int pd_exprGetTeamDifficultyRow(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pPlayer);
	PlayerDifficulty *pDifficulty = pTeam ? pd_GetDifficulty(pTeam->iDifficulty) : NULL;

	if (!pDifficulty)
		return -1;

	return eaFind(&g_PlayerDifficultySet.peaPlayerDifficulties, pDifficulty);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_GetPlayerDifficultyDescStr);
SA_RET_OP_VALID const char *pd_exprGetPlayerDifficultyDescStr(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	PlayerDifficulty *pDifficulty = (pPlayer && pPlayer->pPlayer) ? pd_GetDifficulty(pPlayer->pPlayer->iDifficulty) : NULL;

	if (!pDifficulty)
		return NULL;

	return TranslateMessageRef(pDifficulty->hDescription);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_GetPlayerDifficultyStr);
SA_RET_OP_VALID const char *pd_exprGetPlayerDifficultyStr(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	PlayerDifficulty *pDifficulty = (pPlayer && pPlayer->pPlayer) ? pd_GetDifficulty(pPlayer->pPlayer->iDifficulty) : NULL;

	if (!pDifficulty)
		return NULL;

	return TranslateMessageRef(pDifficulty->hName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_GetPlayerDifficultyRow);
int pd_exprGetPlayerDifficultyRow(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	PlayerDifficulty *pDifficulty = (pPlayer && pPlayer->pPlayer) ? pd_GetDifficulty(pPlayer->pPlayer->iDifficulty) : NULL;

	if (!pDifficulty)
		return -1;

	return eaFind(&g_PlayerDifficultySet.peaPlayerDifficulties, pDifficulty);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_GetEntityDifficultyRow);
int pd_exprGetEntityDifficultyRow(SA_PARAM_OP_VALID Entity* pEnt)
{
	PlayerDifficulty *pDifficulty = NULL;
	if(pEnt && pEnt->pPlayer) {
		pDifficulty = pd_GetDifficulty(pEnt->pPlayer->iDifficulty);
	}

	if (!pDifficulty)
		return -1;

	return eaFind(&g_PlayerDifficultySet.peaPlayerDifficulties, pDifficulty);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_GetEntityDifficultyDescStr);
SA_RET_OP_VALID const char *pd_exprGetEntityDifficultyDescStr(SA_PARAM_OP_VALID Entity* pEnt)
{
	PlayerDifficulty *pDifficulty = NULL;
	if(pEnt && pEnt->pPlayer) {
		pDifficulty = pd_GetDifficulty(pEnt->pPlayer->iDifficulty);
	}

	if (!pDifficulty)
		return NULL;

	return TranslateMessageRef(pDifficulty->hDescription);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_GetEntityDifficultyStr);
SA_RET_OP_VALID const char *pd_exprGetEntityDifficultyStr(SA_PARAM_OP_VALID Entity* pEnt)
{
	PlayerDifficulty *pDifficulty = NULL;
	if(pEnt && pEnt->pPlayer) {
		pDifficulty = pd_GetDifficulty(pEnt->pPlayer->iDifficulty);
	}

	if (!pDifficulty)
		return NULL;

	return TranslateMessageRef(pDifficulty->hName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_GetDifficultyStr);
SA_RET_OP_VALID const char *pd_exprGetDifficultyStr(int iDifficultyIdx)
{
	PlayerDifficulty *pDifficulty = pd_GetDifficulty(iDifficultyIdx);

	if (!pDifficulty)
		return NULL;

	return TranslateMessageRef(pDifficulty->hName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_GetDifficultyDescStr);
SA_RET_OP_VALID const char *pd_exprGetDifficultyDescStr(int iDifficultyIdx)
{
	PlayerDifficulty *pDifficulty = pd_GetDifficulty(iDifficultyIdx);

	if (!pDifficulty)
		return NULL;

	return TranslateMessageRef(pDifficulty->hDescription);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_GetDifficultyList);
void pd_exprGetDifficultyList(SA_PARAM_NN_VALID UIGen *gen)
{
	ui_GenSetList(gen, &g_PlayerDifficultySet.peaPlayerDifficulties, parse_PlayerDifficulty);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pd_ShowDifficultyUI);
bool pd_exprShowDifficultyUI(void)
{
	return eaSize(&g_PlayerDifficultySet.peaPlayerDifficulties) > 1;
}
