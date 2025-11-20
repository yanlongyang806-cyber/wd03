/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "entCritter.h"
#include "estring.h"
#include "Expression.h"
#include "GameStringFormat.h"
#include "MapDescription.h"
#include "mapstate_common.h"
#include "Player.h"
#include "pvp_common.h"
#include "StringCache.h"
#include "UIGen.h"
#include "WorldGrid.h"
#include "gclMapState.h"
#include "PowersMovement.h"
#include "PvPGameCommon.h"
#include "gclMapState.h"
#include "gclPVP.h"
#include "inventoryCommon.h"

#include "gclUIGen.h"
#include "gclEntity.h"
#include "GfxCamera.h"
#include "GraphicsLib.h"

#include "EntityLib.h"
#include "CostumeCommonEntity.h"

#include "pvp_common_h_ast.h"
#include "mapstate_common_h_ast.h"
#include "PvPGameCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("ScoreboardEntityList", BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("ScoreboardEntity", BUDGET_UISystem););

char g_pchDebugScoreboard[500] = {0};
AUTO_CMD_SENTENCE(g_pchDebugScoreboard, ScoreboardSetName) ACMD_ACCESSLEVEL(9) ACMD_HIDE;

int g_eScoreboardState = kScoreboardState_Init;
AUTO_CMD_INT(g_eScoreboardState, ScoreboardSetState) ACMD_ACCESSLEVEL(9) ACMD_HIDE;

static ScoreboardEntityList *s_pScoresList = NULL;

void gclScoreboard_ClearScores(void)
{
	StructDestroySafe(parse_ScoreboardEntityList, &s_pScoresList);
}

static void Scoreboard_GetScores(ScoreboardEntity ***peaScores, SA_PARAM_OP_STR const char *pchTeamName)
{
	if(s_pScoresList)
	{
		int i, n = eaSize(&s_pScoresList->eaScoresList);

		for(i = n-1; i >= 0; i--)
		{
			ScoreboardEntity *pScore = eaGet(&s_pScoresList->eaScoresList, i);
			if(pScore && (!pchTeamName || stricmp(pScore->pchFactionName, pchTeamName) == 0))
			{
				eaPush(peaScores, StructClone(parse_ScoreboardEntity, pScore));
			}
		}
	}
}

static void Scoreboard_GetDomPoints(DOMControlPoint ***peaPoints, F32 width, F32 height)
{
	DOMControlPoint ***pppPoints = NULL;
	int i;
	S32 iScreenWidth, iScreenHeight;
	CBox ScreenBox = {0, 0, 0, 0};
	Entity *pPlayer = entActivePlayerPtr();
	Vec3 vPlayerPos;
	F32 fDistance; 
	bool bOnScreen;

	if(!pPlayer)
		return;

	pppPoints = mapState_GetGameSpecificStructs(mapStateClient_Get());

	if(pppPoints)
	{
		GfxCameraView *pView = gfxGetActiveCameraView();

		gfxGetActiveSurfaceSize(&iScreenWidth, &iScreenHeight);
		ScreenBox.hx = iScreenWidth;
		ScreenBox.hy = iScreenHeight;

		entGetPos(pPlayer,vPlayerPos);

		for(i=0;i<eaSize(pppPoints);i++)
		{
			int iIndex = (*pppPoints)[i]->iPointNumber-1;
			CBox pBox;

			if(!eaGet(peaPoints,iIndex))
			{
				if(iIndex >= eaSize(peaPoints))
					eaSetSize(peaPoints,iIndex+1);

				fDistance = distance3Squared(vPlayerPos,(*pppPoints)[i]->vLocation);
				bOnScreen = ProjectCBoxOnScreen((*pppPoints)[i]->vLocation, pView, &pBox, &ScreenBox, width, height);

				(*pppPoints)[i]->fX = pBox.left;
				(*pppPoints)[i]->fY = pBox.top;

				(*peaPoints)[iIndex] = StructClone(parse_DOMControlPoint,(*pppPoints)[i]);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////
// UI Expressions
////////////////////////////////////////////////////////////////////////////////////////////////

// Get the name of the current scoreboard.  Usually set by the server.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetCurrentScoreboard);
const char* exprGetCurrentScoreboard(void)
{
	if(g_pchDebugScoreboard[0] != '\0' && stricmp(g_pchDebugScoreboard, "none") != 0)
	{
		return((const char*)g_pchDebugScoreboard);
	}
	else
	{
		return mapState_GetScoreboard(mapStateClient_Get());
	}
}

// returns true/false if the scoreboard string is not null, expression refused to perform NULL conditionals on a char*
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(HasScoreboard);
int exprHasScoreboard(void)
{
	return exprGetCurrentScoreboard() != NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetScoreboardState);
const char* exprGetScoreboardState(void)
{
	if(g_pchDebugScoreboard[0] != '\0' && stricmp(g_pchDebugScoreboard, "none") != 0)
	{
		return StaticDefineIntRevLookup(ScoreboardStateEnum, g_eScoreboardState);
	}
	else
	{
		return StaticDefineIntRevLookup(ScoreboardStateEnum, mapState_GetScoreboardState(mapStateClient_Get()));
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsScoreboardInState);
ExprFuncReturnVal exprIsScoreboardInState(ACMD_EXPR_INT_OUT piOut, ACMD_EXPR_ENUM(ScoreboardState) const char *pchState)
{
	const char *pchScoreboardName = exprGetCurrentScoreboard();

	if(piOut)
		*piOut = false;

	if(pchScoreboardName && *pchScoreboardName)
	{
		int eState = StaticDefineIntGetInt(ScoreboardStateEnum,pchState);
		if(g_pchDebugScoreboard[0] != '\0' && stricmp(g_pchDebugScoreboard, "none") != 0)
		{
			if(eState == g_eScoreboardState && piOut)
				*piOut = true;
		}
		else
		{
			if(eState == mapState_GetScoreboardState(mapStateClient_Get()) && piOut)
				*piOut = true;
		}
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetScoreboardTimer);
int exprGetScoreboardTimer(void)
{
	U32 uTime = mapState_GetScoreboardTimer(mapStateClient_Get());
	bool bCountdown = mapState_IsScoreboardInCountdown(mapStateClient_Get());
	U32 uCurrentTime = pmTimestamp(0);

	if(bCountdown)
	{
		return uTime - uCurrentTime;
	}
	else
	{
		return uCurrentTime - uTime;
	}
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetScoreboardTotalMatchTime);
int exprGetScoreboardTotalMatchTime(void)
{
	return mapState_GetScoreboardTotalMatchTimeInSeconds(mapStateClient_Get());
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsScoreboardInOvertime);
int exprIsScoreboardInOvertime(void)
{
	return mapState_IsScoreboardInOvertime(mapStateClient_Get());
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsScoreboardOfGametype);
ExprFuncReturnVal exprIsScoreboardOfGametype(ACMD_EXPR_INT_OUT piOut, ACMD_EXPR_ENUM(PVPGameType) const char *pchGameType)
{
	MapState * pMapState;
	if(piOut)
		*piOut = false;

	pMapState = mapStateClient_Get();

	if(pMapState)
	{
		if(StaticDefineIntGetInt(PVPGameTypeEnum,pchGameType) == pMapState->matchState.pvpRules.eGameType)
			*piOut = true;
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetScoreboardGroupFlagStatus);
const char *exprGetScoreboardGroupFlagStatus(int iGroupIdx)
{
	PVPGroupGameParams ***pppGameParams = NULL;

	pppGameParams = mapState_GetScoreboardGroupDefs(mapStateClient_Get());

	if(pppGameParams && eaSize(pppGameParams) > iGroupIdx)
	{
		CTFGroupParams *pGroupParams = (CTFGroupParams*)(*pppGameParams)[iGroupIdx];

		return StaticDefineIntRevLookup(CTFFlagStatusEnum,pGroupParams->eFlagStatus);
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_IsDomPointStatus);
bool exprIsDomPointStatus(SA_PARAM_OP_VALID DOMControlPoint *pPoint, ACMD_EXPR_ENUM(DOMPointStatus) const char *pchStatus)
{
	if(pPoint)
	{
		return pPoint->eStatus == StaticDefineIntGetInt(DOMPointStatusEnum,pchStatus);
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetScoreboardGroupScore);
int exprGetScoreboardGroupScore(int iGroupIdx)
{
	PVPGroupGameParams ***pppGameParams = NULL;

	pppGameParams = mapState_GetScoreboardGroupDefs(mapStateClient_Get());

	if(pppGameParams && eaSize(pppGameParams) > iGroupIdx)
	{
		return (*pppGameParams)[iGroupIdx]->iScore;
	}

	return 0;
}

int SortByPoints(const ScoreboardEntity **a, const ScoreboardEntity **b)
{
	if((*a)->iPoints > (*b)->iPoints)
		return -1;
	if((*a)->iPoints < (*b)->iPoints)
		return 1;
	return stricmp((*a)->pchName, (*b)->pchName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ReceivedInitialScoreboardUpdate);
bool exprReceivedInitialScoreboardUpdate(void)
{
	return s_pScoresList != NULL;
}

// Get a list of entities (players) that have entries in the map state
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetTopScores);
void exprScoreboard_GetTopEntities(SA_PARAM_NN_VALID UIGen *pGen, S32 iNumberOfScores)
{
	if(s_pScoresList)
	{
		ScoreboardEntity** eaPlayerList = NULL;
		Scoreboard_GetScores(&eaPlayerList, NULL);
		eaQSort(eaPlayerList, SortByPoints);
		while(eaSize(&eaPlayerList) > iNumberOfScores)
		{
			StructDestroy(parse_ScoreboardEntity, eaPop(&eaPlayerList));
		}
		ui_GenSetManagedListSafe(pGen, &eaPlayerList, ScoreboardEntity,true);
		eaDestroy(&eaPlayerList);
	}
}

// Get a list of entities (players) that have entries in the map state
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetScores);
void exprScoreboard_GetEntities(SA_PARAM_NN_VALID UIGen *pGen)
{
	if(s_pScoresList)
	{
		ScoreboardEntity** eaPlayerList = NULL;
		Scoreboard_GetScores(&eaPlayerList, NULL);
		ui_GenSetManagedListSafe(pGen, &eaPlayerList, ScoreboardEntity,true);
		eaDestroy(&eaPlayerList);
	}
	else
	{
		ui_GenSetManagedListSafe(pGen, NULL, ScoreboardEntity, true);
	}
}

// Get a list of entities (players) that have entries in the map state
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetScoresMinimum);
void exprScoreboard_GetEntitiesMinNumber(SA_PARAM_NN_VALID UIGen *pGen, S32 iMinScores)
{
	ScoreboardEntity** eaPlayerList = NULL;

	if(s_pScoresList)
	{
		Scoreboard_GetScores(&eaPlayerList, NULL);
	}

	if (iMinScores > 0 && eaSize(&eaPlayerList) < iMinScores)
	{
		eaSetSizeStruct(&eaPlayerList, parse_ScoreboardEntity, iMinScores);
	}

	ui_GenSetManagedListSafe(pGen, &eaPlayerList, ScoreboardEntity,true);
	eaDestroy(&eaPlayerList);
}

// Get a list of entities (players) that have entries in the map state and are on the given team
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetTeamScore);
void exprScoreboard_GetTeamScores(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_STR const char *pchTeamName)
{
	if(s_pScoresList)
	{
		ScoreboardEntity** eaPlayerList = NULL;
		Scoreboard_GetScores(&eaPlayerList, pchTeamName);
		ui_GenSetManagedListSafe(pGen, &eaPlayerList, ScoreboardEntity,true);
		eaDestroy(&eaPlayerList);
	}
}

// Get a list of entities (players) by team, sorted by score 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetTopScoresByTeam);
void exprScoreboard_GetTopScoresByTeam(SA_PARAM_NN_VALID UIGen *pGen, const char *pchTeam, S32 iNumberOfScores)
{
	if(s_pScoresList)
	{
		ScoreboardEntity** eaPlayerList = NULL;
		Scoreboard_GetScores(&eaPlayerList, pchTeam);
		eaQSort(eaPlayerList, SortByPoints);
		while(eaSize(&eaPlayerList) > iNumberOfScores)
		{
			StructDestroy(parse_ScoreboardEntity, eaPop(&eaPlayerList));
		}
		ui_GenSetManagedListSafe(pGen, &eaPlayerList, ScoreboardEntity,true);
		eaDestroy(&eaPlayerList);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scoreboard_IsEntAttackingPoint);
U32 scoreboard_IsEntAttackingPoint(Entity *pEntity, DOMControlPoint *pPoint)
{
	if(pEntity && pPoint)
	{
		if(ea32Find(&pPoint->iAttackingEnts,pEntity->myContainerID)>=0)
			return 1;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetDomPoints);
void exprGetScoreboardDomPoints(SA_PARAM_NN_VALID UIGen *pGen, F32 width, F32 height)
{
	if(zmapInfoGetMapType(NULL) == ZMTYPE_PVP)
	{
		DOMControlPoint ** eaPoints = NULL;
		eaCreate(&eaPoints);
		Scoreboard_GetDomPoints(&eaPoints,width,height);
		ui_GenSetManagedListSafe(pGen,&eaPoints, DOMControlPoint, true);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetTeamDisplayName);
const char* exprScoreboard_GetTeamDisplayName(int iTeamIdx)
{
	if(s_pScoresList && s_pScoresList->eaGroupList)
	{
		ScoreboardGroup* pGroup = eaGet(&s_pScoresList->eaGroupList, iTeamIdx);
		return pGroup ? TranslateMessageRef(pGroup->hDisplayMessage) : "";
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetGroupTexture);
const char* exprScoreboard_GetGroupTexture(int iTeamIdx)
{
	if(s_pScoresList && s_pScoresList->eaGroupList)
	{
		ScoreboardGroup* pGroup = eaGet(&s_pScoresList->eaGroupList, iTeamIdx);
		return NULL_TO_EMPTY(pGroup->pchGroupTexture);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_TeamSetGroupID);
void exprScoreboard_TeamSetGroupID(const char* pchTeamName, int iTeamIdx)
{
	// Deprecated
}

S32 gclPVP_EntGetGroupID(Entity *pEnt)
{
	if (pEnt && s_pScoresList)
	{
		S32 i;
		const char* pchFaction = REF_STRING_FROM_HANDLE(pEnt->hFactionOverride);
		for (i = eaSize(&s_pScoresList->eaGroupList)-1; i >= 0; i--)
		{
			ScoreboardGroup* pGroup = s_pScoresList->eaGroupList[i];
			if (stricmp(pchFaction, pGroup->pchFactionName)==0)
			{
				return i;
			}
		}
	}
	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_EntGetGroupID);
S32 exprScoreboard_EntGetGroupID(SA_PARAM_OP_VALID Entity* pEnt)
{
	return gclPVP_EntGetGroupID(pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_ScoreboardEntGetGroupID);
S32 exprScoreboard_ScoreboardEntGetGroupID(SA_PARAM_OP_VALID ScoreboardEntity* pScoreboardEnt)
{
	if (pScoreboardEnt && s_pScoresList)
	{
		S32 i;
		const char* pchFaction = pScoreboardEnt->pchFactionName;
		for (i = eaSize(&s_pScoresList->eaGroupList)-1; i >= 0; i--)
		{
			ScoreboardGroup* pGroup = s_pScoresList->eaGroupList[i];
			if (stricmp(pchFaction, pGroup->pchFactionName)==0)
			{
				return i;
			}
		}
	}
	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_LeaderScore);
int exprScoreboard_GetLeaderScore(ExprContext *pContext)
{
	static int iLeaderScore = 0;
	ScoreboardEntity** eaPlayerList = NULL;
	Scoreboard_GetScores(&eaPlayerList, NULL);
	
	if(eaSize(&eaPlayerList))
	{
		eaQSort(eaPlayerList, SortByPoints);
		iLeaderScore = eaPlayerList[0]->iPoints;
	}

	eaDestroyStruct(&eaPlayerList, parse_ScoreboardEntity);

	return(iLeaderScore);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetMyScore);
const char* exprScoreboard_GetMyScore(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayerEnt, const char *pchMessageKey)
{
	static ScoreboardEntity *s_pScore = NULL;
	static char *estrBuffer = NULL;
	MapState *pState;

	const char *pchScoreboardName = mapState_GetScoreboard(mapStateClient_Get());

	if(!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	pState = mapStateClient_Get();

	if(pState && pPlayerEnt)
	{
		MultiVal *val = NULL;
		if(!s_pScore)
		{
			s_pScore = StructCreate(parse_ScoreboardEntity);
		}

		s_pScore->iPlayerKills = s_pScore->iDeathsToPlayers = s_pScore->iPoints = 0;
		s_pScore->iPlayerAssists = 0;
		s_pScore->iTotalKills = s_pScore->iDeathsTotal = 0;
		s_pScore->iBossKills = s_pScore->iDeathsToBosses = 0;
		s_pScore->iPlayerHealing = 0;
		s_pScore->iPlayerDamage = 0;
		s_pScore->iPlayerAssaultTeams = 0;

		val = mapState_GetPlayerValue(pState, pPlayerEnt, "Player_Kills");
		if(val)
			s_pScore->iPlayerKills = MultiValGetInt(val, NULL);

		val = mapState_GetPlayerValue(pState, pPlayerEnt, "Player_Assists");
		if(val)
			s_pScore->iPlayerAssists = MultiValGetInt(val,NULL);
	
		val = mapState_GetPlayerValue(pState, pPlayerEnt, "Player_Deaths");
		if(val)
			s_pScore->iDeathsToPlayers = MultiValGetInt(val, NULL);

		val = mapState_GetPlayerValue(pState, pPlayerEnt, "Player_Time");
		if(val)
			s_pScore->iPlayerTime = MultiValGetInt(val, NULL);

		val = mapState_GetPlayerValue(pState, pPlayerEnt, "Player_Healing");
		if(val)
			s_pScore->iPlayerHealing = MultiValGetInt(val, NULL);

		val = mapState_GetPlayerValue(pState, pPlayerEnt, "Player_Damage");
		if(val)
			s_pScore->iPlayerDamage = MultiValGetInt(val, NULL);

		//Stronghold specific data not to be updated with standard scoreboards
		if(stricmp(pchScoreboardName,"PvP_Stronghold") == 0)
		{
			val = mapState_GetPlayerValue(pState, pPlayerEnt, "Boss_Kills");
			if(val)
				s_pScore->iBossKills = MultiValGetInt(val, NULL);

			val = mapState_GetPlayerValue(pState, pPlayerEnt, "Boss_Deaths");
			if(val)
				s_pScore->iDeathsToBosses = MultiValGetInt(val, NULL);
			
			val = mapState_GetPlayerValue(pState, pPlayerEnt, "Total_Kills");
			if(val)
				s_pScore->iTotalKills = MultiValGetInt(val, NULL);

			val = mapState_GetPlayerValue(pState, pPlayerEnt, "Total_Deaths");
			if(val)
				s_pScore->iDeathsTotal = MultiValGetInt(val, NULL);
		}

		//Assault specific data not to be updated with standard scoreboards
		if (stricmp(pchScoreboardName,"PvP_Assault") == 0)
		{
			val = mapState_GetPlayerValue(pState, pPlayerEnt, "Player_Assault_Teams");
			if(val)
				s_pScore->iPlayerAssaultTeams = MultiValGetInt(val, NULL);
		}
				
		val = mapState_GetPlayerValue(pState, pPlayerEnt, "Player_Points");
		if(val)
			s_pScore->iPoints = MultiValGetInt(val, NULL);
		else
			s_pScore->iPoints = s_pScore->iPlayerKills;

		entFormatGameMessageKey(pPlayerEnt, &estrBuffer, pchMessageKey,
				STRFMT_INT("PlayerKills", s_pScore->iPlayerKills),
				STRFMT_INT("PlayerAssists", s_pScore->iPlayerAssists),
				STRFMT_INT("PlayerPoints", s_pScore->iPoints),
				STRFMT_INT("PlayerTime", s_pScore->iPlayerTime),
				STRFMT_INT("PlayerHealing", s_pScore->iPlayerHealing),
				STRFMT_INT("PlayerDamage", s_pScore->iPlayerDamage),
				STRFMT_INT("DeathsToPlayers", s_pScore->iDeathsToPlayers),
				STRFMT_INT("TotalKills", s_pScore->iTotalKills),
				STRFMT_INT("DeathsTotal", s_pScore->iDeathsTotal),
				STRFMT_INT("NPCKills", MAX(0, s_pScore->iTotalKills - s_pScore->iPlayerKills - s_pScore->iBossKills)),
				STRFMT_INT("DeathsToNPCs", MAX(0, s_pScore->iDeathsTotal - s_pScore->iDeathsToPlayers - s_pScore->iDeathsToBosses)),
				STRFMT_INT("BossKills", s_pScore->iBossKills),
				STRFMT_INT("DeathsToBosses", s_pScore->iDeathsToBosses),
				STRFMT_INT("PlayerAssaultTeams", s_pScore->iPlayerAssaultTeams),
				STRFMT_END);
	}
	else
	{
		//TranslateMessageKey(
	}
	
	return(estrBuffer);
}

//Returns the time remaining (or -1 for error)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetTimeRemaining);
int exprScoreboard_GetTimeRemaining(void)
{
	MapState *state = mapStateClient_Get();
	MultiVal *mvStartTime = mapState_GetValue(state,"GameStartTime");
	MultiVal *mvDuration = mapState_GetValue(state, "GameDuration");
	
	if(mvStartTime && mvDuration)
	{
		int iTimeRemaining = 0;
		int iNow = timeServerSecondsSince2000();

		ANALYSIS_ASSUME(mvDuration);
		ANALYSIS_ASSUME(mvStartTime);

		iTimeRemaining = MultiValGetInt(mvStartTime,NULL) + MultiValGetInt(mvDuration,NULL) - iNow;
		MAX1(iTimeRemaining,0);
		return iTimeRemaining;
	}
	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetEntityFromScoreboardEntity);
SA_RET_OP_VALID Entity *exprScoreboard_GetEntityFromScoreboardEntity(SA_PARAM_OP_VALID ScoreboardEntity *pScoreboardEntity)
{
	if (pScoreboardEntity)
	{
		return entFromEntityRefAnyPartition(pScoreboardEntity->entRef);
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetWinnerGroupID);
int exprScoreboard_GetWinnerGroupID(void)
{
	return gclPVP_GetWinningGroupID();
}

int SortByMetric(const ScoreboardMetricEntry **a, const ScoreboardMetricEntry **b)
{
	if((*a)->iMetricValue > (*b)->iMetricValue)
		return -1;
	if((*a)->iMetricValue < (*b)->iMetricValue)
		return 1;
	if ((*a)->iEntID != 0 && (*b)->iEntID == 0)
		return -1;
	if ((*a)->iEntID == 0 && (*b)->iEntID != 0)
		return 1;
	return stricmp((*a)->pchName, (*b)->pchName);
}

static ScoreboardMetricEntry *findScoreboardMetricEntryByID(ScoreboardMetricEntry ***peaEntries, ContainerID id)
{
	int i;

	if(!peaEntries)
		return NULL;

	for (i = 0; i < eaSize(peaEntries); ++i)
	{
		ScoreboardMetricEntry *pCurEntry = (*peaEntries)[i];

		if(pCurEntry && pCurEntry->iEntID == id)
			return pCurEntry;
	}

	return NULL;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetScoresForMetricIndex);
void exprScoreboard_GetScoresForMetricIndex(SA_PARAM_NN_VALID UIGen *pGen, S32 index)
{
	ScoreboardMetricEntry ***peaMetricScores = ui_GenGetManagedListSafe(pGen, ScoreboardMetricEntry);
	static ScoreboardEntity **eaScores = NULL;
	MapState *pState = NULL;
	Entity *pPlayerEnt = entActivePlayerPtr();
	int i;

	eaClearStruct(&eaScores, parse_ScoreboardEntity);

	if(pPlayerEnt)
	{
		pState = mapState_FromPartitionIdx(entGetPartitionIdx(pPlayerEnt));

		if(pState)
		{
			Scoreboard_GetScores(&eaScores, NULL);

			for (i = 0; i < eaSize(&eaScores); ++i)
			{
				ScoreboardEntity *pScore = eaScores[i];

				if(pScore && pState && pState->pPlayerValueData)
				{
					ScoreboardMetricEntry *pMetricScore = findScoreboardMetricEntryByID(peaMetricScores, pScore->iEntID);
					PlayerMapValues *pPlayerValues = eaIndexedGetUsingInt(&pState->pPlayerValueData->eaPlayerValues, pScore->iEntID);
					Entity *pEntity = entFromContainerID(entGetPartitionIdx(pPlayerEnt), GLOBALTYPE_ENTITYPLAYER, pScore->iEntID);

					if (!pMetricScore)
					{
						int j;

						for (j = 0; j < eaSize(peaMetricScores); ++j)
						{
							ScoreboardMetricEntry *pCurEntry = (*peaMetricScores)[j];
							if ( (pCurEntry && pCurEntry->iEntID == 0) || !pCurEntry )
							{
								if (pCurEntry)
								{
									StructDestroySafe(parse_ScoreboardMetricEntry, &pCurEntry);
								}

								eaRemove(peaMetricScores, j);
								break;
							}
						}

						pMetricScore = StructCreate(parse_ScoreboardMetricEntry);
						eaInsert(peaMetricScores, pMetricScore, j);
					}

					if(pMetricScore->iEntID != pScore->iEntID)
					{
						pMetricScore->iEntID = pScore->iEntID;
						strcpy(pMetricScore->pchName, pScore->pchName);
						strcpy(pMetricScore->pchAccountName, pScore->pchAccountName);
					}

					if(index >= 0 && index < eaSize(&pPlayerValues->eaValues))
					{
						MapStateValue *pValue = pPlayerValues->eaValues[index];
						if(pValue)
							pMetricScore->iMetricValue = MultiValGetInt(&pValue->mvValue, NULL);
					}
				}
			}
		}
	}

	eaQSort(*peaMetricScores, SortByMetric);

	for (i = 0; i < eaSize(peaMetricScores); ++i)
	{
		ScoreboardMetricEntry *pEntry = (*peaMetricScores)[i];

		if (pEntry)
		{
			pEntry->iRank = i + 1;
		}
	}

	eaSetSizeStruct(peaMetricScores, parse_ScoreboardMetricEntry, 5);

	ui_GenSetManagedListSafe(pGen, peaMetricScores, ScoreboardMetricEntry, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetMetricNameByIndex);
const char *exprScoreboard_GetMetricNameByIndex(S32 index)
{
	Entity *pPlayerEnt = entActivePlayerPtr();
	MapState *pState = NULL;

	if(pPlayerEnt)
	{
		pState = mapState_FromPartitionIdx(entGetPartitionIdx(pPlayerEnt));

		if(pState && pState->pPlayerValueData)
		{
			PlayerMapValues *pPlayerValues = eaIndexedGetUsingInt(&pState->pPlayerValueData->eaPlayerValues, entGetContainerID(pPlayerEnt));

			if(index >= 0 && index < eaSize(&pPlayerValues->eaValues))
			{
				MapStateValue *pValue = pPlayerValues->eaValues[index];
				if(pValue)
					return pValue->pcName;
			}
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetTotalMetrics);
int exprScoreboard_GetTotalMetrics(void)
{
	Entity *pPlayerEnt = entActivePlayerPtr();
	MapState *pState = NULL;

	if(pPlayerEnt)
	{
		pState = mapState_FromPartitionIdx(entGetPartitionIdx(pPlayerEnt));

		if(pState && pState->pPlayerValueData && pState->pPlayerValueData->eaPlayerValues)
		{
			PlayerMapValues *pPlayerValues = eaIndexedGetUsingInt(&pState->pPlayerValueData->eaPlayerValues, entGetContainerID(pPlayerEnt));

			if(pPlayerValues && pPlayerValues->eaValues)
				return eaSize(&pPlayerValues->eaValues);
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetCostumeForMetricEntry);
SA_RET_OP_VALID PlayerCostume *exprScoreboard_GetCostumeForMetricEntry(ScoreboardMetricEntry *pEntry)
{
	Entity *pPlayerEnt = entActivePlayerPtr();
	if(pPlayerEnt && pEntry->iEntID)
	{
		Entity *pEnt = entFromContainerID(entGetPartitionIdx(pPlayerEnt), GLOBALTYPE_ENTITYPLAYER, pEntry->iEntID);

		if (pEnt)
		{
			return costumeEntity_GetEffectiveCostume(pEnt);
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetEntForMetricEntry);
SA_RET_OP_VALID Entity *exprScoreboard_GetEntForMetricEntry(ScoreboardMetricEntry *pEntry)
{
	Entity *pPlayerEnt = entActivePlayerPtr();
	if(pPlayerEnt && pEntry->iEntID)
	{
		static Entity **eaTeamEnts = NULL;
		static TeamMember **eaTeamMembers = NULL;
		Entity *pEnt = NULL;
		int i;
		int iCount = 0;
		
		eaClear(&eaTeamEnts);
		eaClear(&eaTeamMembers);

		iCount = team_GetTeamListSelfFirst(pPlayerEnt, &eaTeamEnts, &eaTeamMembers, true, false);

		for (i = 0; i < iCount; ++i)
		{
			pEnt = eaTeamEnts[i];

			if (pEnt && pEnt->myContainerID == pEntry->iEntID)
			{
				return pEnt;
			}
		}


		pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pEntry->iEntID);

		return pEnt;
	}

	return NULL;
}

NOCONST(InventoryBag) *g_pRewardBag = NULL;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_GetRewardList);
void exprScoreboard_GetRewardList(SA_PARAM_NN_VALID UIGen *pGen)
{
	if(g_pRewardBag && eaSize(&g_pRewardBag->ppIndexedInventorySlots))
		ui_GenSetManagedListSafe(pGen, (InventorySlot***)&g_pRewardBag->ppIndexedInventorySlots, InventorySlot, false);
	else
		ui_GenSetManagedListSafe(pGen, NULL, InventorySlot, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PvPGetPointMax) ACMD_CATEGORY(PVP);
int exprPVPGame_GetPointMax()
{
	MapState *pState = mapStateClient_Get();

	if(pState)
	{
		return pState->matchState.pvpRules.iPointMax;
	}
	else
	{
		return 100;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PVPGetGroupSize);
int exprPVPGame_GetGroupSize(int iGroup)
{
	MapState *pState = mapStateClient_Get();
	if(pState)
	{
		if (iGroup < eaSize(&pState->matchState.ppGroupGameParams))
		{
			return pState->matchState.ppGroupGameParams[iGroup]->iNumMembers;
		}
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////
// Commands
////////////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_NAME("PVP_UpdateScores") ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0) ACMD_IFDEF(GAMESERVER);
void cmd_UpdateScores(ScoreboardEntityList *pScoresList)
{
	if (!s_pScoresList)
	{
		s_pScoresList = StructClone(parse_ScoreboardEntityList, pScoresList);
	}
	else
	{
		eaCopyStructs(&pScoresList->eaScoresList, &s_pScoresList->eaScoresList, parse_ScoreboardEntity);
		eaCopyStructs(&pScoresList->eaGroupList, &s_pScoresList->eaGroupList, parse_ScoreboardGroup);
	}
}

AUTO_COMMAND ACMD_NAME("PVP_SetReward") ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0) ACMD_IFDEF(GAMESERVER);
void cmd_setReward(InventoryBag *pBag)
{
	if(g_pRewardBag)
		StructDestroyNoConst(parse_InventoryBag,g_pRewardBag);

	if(pBag)
		g_pRewardBag = StructCloneDeConst(parse_InventoryBag,pBag);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Scoreboard_ClearReward);
void exprScoreboard_ClearReward()
{
	if(g_pRewardBag)
		StructDestroyNoConst(parse_InventoryBag,g_pRewardBag);
}

// END COMMANDS