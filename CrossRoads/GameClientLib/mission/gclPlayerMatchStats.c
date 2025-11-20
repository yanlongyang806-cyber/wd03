#include "Entity.h"
#include "entCritter.h"
#include "playerMatchStats_common.h"
#include "playerMatchStats_common_h_ast.h"
#include "playerstats_common.h"
#include "StringCache.h"
#include "UIGen.h"
#include "missionui_eval.h"
#include "mission_common.h"
#include "gclEntity.h"
#include "gclMapState.h"
#include "mapstate_common.h"
#include "gclPlayerMatchStats_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// ------------------------------------------------------------------------------------------

AUTO_STRUCT;
typedef struct UIGenPlayerMatchStats
{
	U32			keyRank;
	const char	*pchIconName;			AST(POOL_STRING)
	const char	*pchEntityName;			AST(POOL_STRING)
	const char  *pchDisplayName;		AST(POOL_STRING)
	U32			uValue;					AST(NAME(Value))
	
	char		*pchTooltipMessage;		AST(ESTRING)

	PlayerStatCategory eCategory;		NO_AST
} UIGenPlayerMatchStats;

AUTO_STRUCT;
typedef struct UIGenTeamMatchStats
{
	const char *pchStatName;		AST(POOL_STRING KEY)
	F32			friendlyValue;
	F32			enemyValue;
	F32			valuesSum;
} UIGenTeamMatchStats;

typedef struct ClientPlayerMatchStats
{
	UIGenPlayerMatchStats **eaUIPlayerMatchStats;
	UIGenTeamMatchStats **eaUITeamMatchStats;
	UIGenTeamMatchStats **eaUITeamTimedBasedMatchStats;

	PerMatchPlayerStatList *pPlayersStatsList;
	
	S32		iVerCount;
} ClientPlayerMatchStats;


static ClientPlayerMatchStats s_matchStats = {0};


int cmpUIGenPlayerMatchStatsByRank(const UIGenPlayerMatchStats** lhs, const UIGenPlayerMatchStats** rhs)
{
	return (S32)(*rhs)->keyRank - (S32)(*lhs)->keyRank;
}

int cmpUIGenPlayerMatchStatsByValue(const UIGenPlayerMatchStats** lhs, const UIGenPlayerMatchStats** rhs)
{
	return (S32)(*rhs)->uValue - (S32)(*lhs)->uValue;
}

// ------------------------------------------------------------------------------------------
static void createRankSortedPlayerStats(PerMatchPlayerStatList *pPlayersStatsList, UIGenPlayerMatchStats ***pppUIPlayerMatchStats)
{
	eaClearEx(pppUIPlayerMatchStats, NULL);

	//
	FOR_EACH_IN_EARRAY(pPlayersStatsList->eaPlayerMatchStats, PlayerMatchInfo, pmi)
	{
		FOR_EACH_IN_EARRAY(pmi->eaPlayerStats, PlayerMatchStat, pstat)
		{
			if (pstat->pchStatName && pstat->uValue)
			{
				PlayerStatDef *pDef = RefSystem_ReferentFromString(g_PlayerStatDictionary,pstat->pchStatName);
				if (pDef && pDef->iRank)
				{
					UIGenPlayerMatchStats *pPMS = StructAlloc(parse_UIGenPlayerMatchStats);
					pPMS->keyRank = pDef->iRank * pstat->uValue;
					pPMS->pchIconName = pDef->pchIconName;
					pPMS->pchEntityName = pmi->pchPlayerName ? allocAddString(pmi->pchPlayerName) : "";
										
					pPMS->pchDisplayName = TranslateDisplayMessage(pDef->displayNameMsg);
					
					pPMS->uValue = pstat->uValue;

					eaPush(pppUIPlayerMatchStats, pPMS);
				}
			}
		}
		FOR_EACH_END
	}
	FOR_EACH_END
		
	eaQSort(*pppUIPlayerMatchStats, cmpUIGenPlayerMatchStatsByRank);
}

// ------------------------------------------------------------------------------------------
static void createTeamBasedMatchStats(PerMatchPlayerStatList *pPlayersStatsList, UIGenTeamMatchStats ***peaUITeamMatchStats)
{
	Entity* pEnt = entActivePlayerPtr();
	ContainerID erActivePlayerID = 0;
	CritterFaction *pFaction = NULL;
	const char *pchFactionName = NULL;

	if (pEnt)
	{
		erActivePlayerID = entGetContainerID(pEnt);
		pFaction = entGetFaction(pEnt);
		pchFactionName = SAFE_MEMBER(pFaction, pchName);
	}
	
	eaIndexedEnable(peaUITeamMatchStats, parse_UIGenTeamMatchStats);
	
	FOR_EACH_IN_EARRAY(pPlayersStatsList->eaPlayerMatchStats, PlayerMatchInfo, pPMI)
	{
		bool bFriendly = true;
		if ((!pchFactionName && pPMI->iContainerID != erActivePlayerID) ||
			!pPMI->pchFactionName || stricmp(pchFactionName, pPMI->pchFactionName))
		{
			bFriendly = false;
		}

		FOR_EACH_IN_EARRAY(pPMI->eaPlayerStats, PlayerMatchStat, pStat)
		{
			if (pStat->pchStatName)
			{
				UIGenTeamMatchStats *pTeamStat = eaIndexedGetUsingString(peaUITeamMatchStats, pStat->pchStatName);
				if (!pTeamStat)
				{
					pTeamStat = StructCreate(parse_UIGenTeamMatchStats);
					pTeamStat->pchStatName = pStat->pchStatName;
					eaIndexedAdd(peaUITeamMatchStats, pTeamStat);
				}

				if (bFriendly)
					pTeamStat->friendlyValue += (F32)pStat->uValue;
				else
					pTeamStat->enemyValue += (F32)pStat->uValue;

				pTeamStat->valuesSum += (F32)pStat->uValue;
			}
		}
		FOR_EACH_END
	}
	FOR_EACH_END
}


// ------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void playermatchstats_RecieveMatchStats(PerMatchPlayerStatList *pPlayerMatchStatList)
{
	s_matchStats.iVerCount ++;

	if (s_matchStats.pPlayersStatsList)
	{
		// destroy our old version
		StructDestroy(parse_PerMatchPlayerStatList, s_matchStats.pPlayersStatsList);
		s_matchStats.pPlayersStatsList = NULL;

		eaDestroyStruct(&s_matchStats.eaUIPlayerMatchStats, parse_UIGenPlayerMatchStats);
		eaDestroyStruct(&s_matchStats.eaUITeamMatchStats, parse_UIGenTeamMatchStats);
		eaDestroyStruct(&s_matchStats.eaUITeamTimedBasedMatchStats, parse_UIGenTeamMatchStats);
	}

	s_matchStats.pPlayersStatsList = StructClone(parse_PerMatchPlayerStatList, pPlayerMatchStatList);
	
	if (s_matchStats.pPlayersStatsList)
	{
		createRankSortedPlayerStats(s_matchStats.pPlayersStatsList, &s_matchStats.eaUIPlayerMatchStats);
		createTeamBasedMatchStats(s_matchStats.pPlayersStatsList, &s_matchStats.eaUITeamMatchStats);
	}

}

// ------------------------------------------------------------------------------------------
void playermatchstats_GetValueForPlayer(Entity *pEnt, const char *pchStatsName)
{
	if (s_matchStats.pPlayersStatsList && pEnt)
	{
		PlayerMatchInfo* pPlayerMatchInfo = playermatchstats_FindPlayerByEntRef(s_matchStats.pPlayersStatsList, entGetRef(pEnt));
		if (pPlayerMatchInfo)
		{
			playermatchstats_GetValue(pPlayerMatchInfo, pchStatsName);
		}
	}
}


// ------------------------------------------------------------------------------------------

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetRankedMatchStats");
void exprGetRankedMatchStats(	SA_PARAM_NN_VALID UIGen *pGen,
								S32 maxStats)
{
	static UIGenPlayerMatchStats **s_ppPlayerMatchStats = NULL;

	if(eaSize(&s_matchStats.eaUIPlayerMatchStats))
	{
		S32 i;
		eaClear(&s_ppPlayerMatchStats);
		for(i = 0; i < maxStats && i < eaSize(&s_matchStats.eaUIPlayerMatchStats); i++)
		{
			UIGenPlayerMatchStats *pms = s_matchStats.eaUIPlayerMatchStats[i];
			
			eaPush(&s_ppPlayerMatchStats, pms);
		}
	}
	else
	{
		eaClear(&s_ppPlayerMatchStats);
	}
	ui_GenSetManagedListSafe(pGen, &s_ppPlayerMatchStats, UIGenPlayerMatchStats, false);
}


// ------------------------------------------------------------------------------------------

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MatchStatsGetCompletedMissions");
void exprMatchStatsGetCompletedMissions(SA_PARAM_NN_VALID UIGen *pGen, Entity *pEnt, S32 maxMissions, const char *pchExcludeTags)
{
	PlayerMatchInfo* pPlayerMatchInfo;

	if (!pEnt || !s_matchStats.pPlayersStatsList)
		return;

	pPlayerMatchInfo = playermatchstats_FindPlayerByEntRef(s_matchStats.pPlayersStatsList, entGetRef(pEnt));
	if (!pPlayerMatchInfo)
		return;

	exprFillMissionListFromRefs(pGen, pEnt, pPlayerMatchInfo->eaPlayerMissionsCompleted, maxMissions, pchExcludeTags);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MatchStatsGetTeamStats");
SA_RET_NN_VALID UIGenTeamMatchStats* exprMatchStatsGetTeamStats(const char *pchStatName)
{
	static UIGenTeamMatchStats s_dummyMatchStat = {0};
	s_dummyMatchStat.valuesSum = 1.f;
		
	if (s_matchStats.eaUITeamMatchStats)
	{
		UIGenTeamMatchStats *pStat = eaIndexedGetUsingString(&s_matchStats.eaUITeamMatchStats, pchStatName);
		if (pStat)
			return pStat;
	}
	return &s_dummyMatchStat;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MatchStatsGetTeamStatsSetGenData");
SA_RET_NN_VALID UIGenTeamMatchStats* exprMatchStatsGetTeamStatsSetGenData(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID const char *pchStatName)
{
	UIGenTeamMatchStats *pStat = exprMatchStatsGetTeamStats(pchStatName);
	ui_GenSetPointer(pGen, pStat, parse_UIGenTeamMatchStats);
	return pStat;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MatchStatsGetTeamStatsByMatchLength");
SA_RET_NN_VALID UIGenTeamMatchStats* exprMatchStatsGetTeamStatsByMatchLength(const char *pchStatName)
{
	static UIGenTeamMatchStats s_dummyMatchStat = {0};
	s_dummyMatchStat.valuesSum = 1.f;
		
	if (!s_matchStats.eaUITeamTimedBasedMatchStats)
	{
		eaIndexedEnable(&s_matchStats.eaUITeamTimedBasedMatchStats, parse_UIGenTeamMatchStats);
	}


	{
		// see if we've already computed this stat
		UIGenTeamMatchStats *pTimeBasedStat = eaIndexedGetUsingString(&s_matchStats.eaUITeamTimedBasedMatchStats, pchStatName);
		if (pTimeBasedStat)
		{
			return pTimeBasedStat;
		}
		else
		{
			UIGenTeamMatchStats *pStat = eaIndexedGetUsingString(&s_matchStats.eaUITeamMatchStats, pchStatName);
			MapState *pMapState = mapStateClient_Get();
			F32 matchLength = (F32)mapState_GetScoreboardTotalMatchTimeInSeconds(pMapState);
			bool bPerMinute = true; 
			if (bPerMinute)	// per-minute- until we want otherwise
				matchLength /= 60.f;
			
			if (matchLength > 0.f && pStat)
			{	// make this stat relative to the match time
				pTimeBasedStat = StructCreate(parse_UIGenTeamMatchStats);
				
				pTimeBasedStat->enemyValue = ceil(pStat->enemyValue / matchLength);
				pTimeBasedStat->friendlyValue = ceil(pStat->friendlyValue / matchLength);

				pTimeBasedStat->valuesSum = pTimeBasedStat->enemyValue + pTimeBasedStat->friendlyValue;
				if (!pTimeBasedStat->valuesSum)
					pTimeBasedStat->valuesSum = 1.f;

				pTimeBasedStat->pchStatName = pStat->pchStatName;
				eaIndexedAdd(&s_matchStats.eaUITeamTimedBasedMatchStats, pTimeBasedStat);
				return pTimeBasedStat;
			}
		}
	}
	return &s_dummyMatchStat;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MatchStatsGetTeamStatsByMatchLengthSetGenData");
SA_RET_NN_VALID UIGenTeamMatchStats* exprMatchStatsGetTeamStatsByMatchLengthSetGenData(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID const char *pchStatName)
{
	UIGenTeamMatchStats *pStat = exprMatchStatsGetTeamStatsByMatchLength(pchStatName);
	ui_GenSetPointer(pGen, pStat, parse_UIGenTeamMatchStats);
	return pStat;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MatchStatsGetSumOfTagged");
S32 exprMatchStatsGetSumOfTagged(SA_PARAM_OP_VALID EntityRef erEntity, const char *pchTag)
{
	S32 iTag = pchTag ? StaticDefineIntGetInt(PlayerStatTagEnum, pchTag) : -1;
	S32 iValue = 0;
	if (s_matchStats.pPlayersStatsList)
	{
		PlayerMatchInfo *pPMI = playermatchstats_FindPlayerByEntRef(s_matchStats.pPlayersStatsList, erEntity);
		if (pPMI)
		{
			FOR_EACH_IN_EARRAY(pPMI->eaPlayerStats, PlayerMatchStat, pMatchStat)
			{
				bool bUseStat = false;

				if (iTag != -1)
				{
					PlayerStatDef *pDef = RefSystem_ReferentFromString(g_PlayerStatDictionary,pMatchStat->pchStatName);
					if (eaiFind(&pDef->piTags, iTag) >= 0)
					{
						bUseStat = true;
					}
				}
				else
				{
					bUseStat = true;
				}

				if (bUseStat)
				{
					iValue += (S32)pMatchStat->uValue;
				}
			}
			FOR_EACH_END
		}
	}

	return iValue;
}

// ------------------------------------------------------------------------------------------
typedef struct MatchStatsCombinedQuery
{
	EntityRef	erEnt;
	S32			maxStats;
	S32			statsVer;
} MatchStatsCombinedQuery;

static void matchStats_AddDescription(UIGenPlayerMatchStats *pMatchStats, PlayerStatDef *pDef)
{
	const char *pchDescriptionMessage = TranslateDisplayMessage(pDef->descriptionMsg);
	U32 uiDescLen;
	if (!pchDescriptionMessage)
		return;

	uiDescLen = (U32)strlen(pchDescriptionMessage);
	if (!uiDescLen)
		return;

	if (!pMatchStats->pchTooltipMessage)
	{
		pMatchStats->pchTooltipMessage = estrCreateFromStr(pchDescriptionMessage);
	}
	else
	{	
		const char* pchSeparator = TranslateMessageKey("PlayerMatchStats.Separator");
		U32 uiLenSep;

		if (!pchSeparator)
			pchSeparator = "<BR><BR>";

		uiLenSep = pchSeparator ? (U32)strlen(pchSeparator) : 0;
		
		if (uiLenSep)
			estrConcatString(&pMatchStats->pchTooltipMessage, pchSeparator, uiLenSep);

		estrConcatString(&pMatchStats->pchTooltipMessage, pchDescriptionMessage, uiDescLen);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MatchStatsGetCombined");
void exprMatchStatsGetCombined(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, S32 maxStats, const char *pchTag)
{
	static UIGenPlayerMatchStats **s_ppPlayerMatchStats = NULL;
	static MatchStatsCombinedQuery s_cachedQuery = {0};

	S32 iTag = pchTag ? StaticDefineIntGetInt(PlayerStatTagEnum, pchTag) : -1;

	if (pEnt && s_matchStats.pPlayersStatsList)
	{
		PlayerMatchInfo* pPlayerMatchInfo = playermatchstats_FindPlayerByEntRef(s_matchStats.pPlayersStatsList, entGetRef(pEnt));
		S32 numValidStats = 0;
		MatchStatsCombinedQuery newQuery = {0};

		newQuery.erEnt = entGetRef(pEnt);
		newQuery.maxStats = maxStats;
		newQuery.statsVer = s_matchStats.iVerCount;

		if (!memcmp(&newQuery, &s_cachedQuery, sizeof(MatchStatsCombinedQuery)))
		{
			ui_GenSetManagedListSafe(pGen, &s_ppPlayerMatchStats, UIGenPlayerMatchStats, false);
			return; // same query, don't do anything
		}
					 
		eaDestroyStruct(&s_ppPlayerMatchStats, parse_UIGenPlayerMatchStats);

		// go through all the stats, 
		// pool all the stats that have the matched tag into respective PlayerStatCategory
		FOR_EACH_IN_EARRAY(pPlayerMatchInfo->eaPlayerStats, PlayerMatchStat, pStat)
		{
			PlayerStatDef *pDef = RefSystem_ReferentFromString(g_PlayerStatDictionary,pStat->pchStatName);
			PlayerStatCategoryDef *pCategory;
			
			if (!pDef)
				continue;

			pCategory = eaGet(&g_PlayerStatCategories.eaPlayerStatCategories, pDef->eCategory - kPlayerStatCategory_FIRST_DATA_DEFINED);
			if (!pCategory || !pCategory->pchIconName)
				continue;

			if (iTag == -1 || eaiFind(&pDef->piTags, iTag) >= 0)
			{
				UIGenPlayerMatchStats *pFound = NULL;
								
				FOR_EACH_IN_EARRAY(s_ppPlayerMatchStats, UIGenPlayerMatchStats, pUIGenStats)
				{
					if (pUIGenStats->eCategory == pDef->eCategory)
					{
						pFound = pUIGenStats;
						break;
					}
				}
				FOR_EACH_END

				if (pFound)
				{
					pFound->uValue += pStat->uValue;
		
					matchStats_AddDescription(pFound, pDef); 
				}
				else
				{
					UIGenPlayerMatchStats *pNew = StructAlloc(parse_UIGenPlayerMatchStats);

					pNew->keyRank = 0;
					pNew->pchIconName = pCategory->pchIconName;
					pNew->pchEntityName = pPlayerMatchInfo->pchPlayerName ? allocAddString(pPlayerMatchInfo->pchPlayerName) : "";
					pNew->pchDisplayName = TranslateDisplayMessage(pDef->displayNameMsg);
					pNew->eCategory = pDef->eCategory;

					pNew->uValue = pStat->uValue;

					matchStats_AddDescription(pNew, pDef); 
										
					numValidStats ++;

					eaPush(&s_ppPlayerMatchStats, pNew);
				}
			}
		}
		FOR_EACH_END

		// sort the list and clip off the anything over the maxStats
		eaQSort(s_ppPlayerMatchStats, cmpUIGenPlayerMatchStatsByValue);
		
		while (eaSize(&s_ppPlayerMatchStats) > maxStats)
			StructDestroy(parse_UIGenPlayerMatchStats, eaPop(&s_ppPlayerMatchStats));

		// cache this query, for the next time the function is called we don't redo this work
		memcpy(&s_cachedQuery, &newQuery, sizeof(MatchStatsCombinedQuery));
	}
	else
	{
		eaDestroyStruct(&s_ppPlayerMatchStats, parse_UIGenPlayerMatchStats);
	}
	
	ui_GenSetManagedListSafe(pGen, &s_ppPlayerMatchStats, UIGenPlayerMatchStats, false);
}

AUTO_COMMAND ACMD_NAME("PlayerMatchStatsDebugDump");
void playermatchstats_DumpMatchStats()
{
	if (!s_matchStats.pPlayersStatsList)
	{
		printf("PlayerMatchStats: No stats received.\n");
		return;
	}

	printf("PlayerMatchStats: Dumping player information\n\n");

	FOR_EACH_IN_EARRAY(s_matchStats.pPlayersStatsList->eaPlayerMatchStats, PlayerMatchInfo, pPMI)
	{
		Entity *e = entFromEntityRefAnyPartition(pPMI->erEntity);
		printf("Player: %s\n", pPMI->pchPlayerName);
		printf("\tStats\n");
		FOR_EACH_IN_EARRAY(pPMI->eaPlayerStats, PlayerMatchStat, pStat)
		{
			printf("\t\t%s : %d\n", pStat->pchStatName, pStat->uValue);
		}
		FOR_EACH_END

		printf("\tCompleted Missions:\n");

		FOR_EACH_IN_EARRAY(pPMI->eaPlayerMissionsCompleted, MissionDefRef, pDefRef)
		{
			MissionDef *pDef = GET_REF(pDefRef->hMission);
			if (pDef)
			{
				printf("\t\t%s\n", pDef->name);
			}
			else
			{
				printf("\t\tCould not find mission def from given handle.\n");
			}
		}
		FOR_EACH_END
	}
	FOR_EACH_END
	
}

#include "gclPlayerMatchStats_c_ast.c"