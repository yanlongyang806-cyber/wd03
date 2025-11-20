/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Leaderboard.h"

#include "Entity.h"
#include "EntitySavedData.h"
#include "error.h"
#include "Expression.h"
#include "GameStringFormat.h"
#include "Player.h"
#include "StringCache.h"
#include "textparser.h"

#include "AutoGen/Leaderboard_h_ast.h"
#include "AutoGen/Entity_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#ifdef GAMESERVER
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#endif

typedef struct LeaderboardCBData
{
	int iPartitionIdx;
	EntityRef iRef;
	LeaderboardDef *pDef;
	F32 fChange;
	bool bLocked;
}LeaderboardCBData;

LeaderboardDefs g_sLeaderboardDefs = {0};

static ExprContext* s_pContext = NULL;
static ExprFuncTable* s_pFuncTable = NULL;

// Determines if the name of a = the name of b
int leaderboard_CompareNames(const LeaderboardDef* a, const LeaderboardDef* b) {
	return (strcmp(a->pchLeaderboardKey, b->pchLeaderboardKey) == 0);
}

LeaderboardDef *leaderboardDef_Find(const char *pchName)
{
	LeaderboardDef sFakeLeaderboard = {0};
	int iPos;

	sFakeLeaderboard.pchLeaderboardKey = pchName;

	iPos = eaFindCmp(&g_sLeaderboardDefs.ppLeaderboards,&sFakeLeaderboard,leaderboard_CompareNames);

	if(iPos>=0)
		return g_sLeaderboardDefs.ppLeaderboards[iPos];

	return NULL;
}

void LeaderboardData_generate(LeaderboardDataDef *pDef)
{
	if(pDef->pValueExpr)
	{
		exprContextSetPointerVar(s_pContext,"Source",NULL,parse_Entity, true, true);

		exprGenerate(pDef->pValueExpr,s_pContext);
	}
}

void leaderboard_generate(LeaderboardDef *pDef)
{
	if(pDef->sEval.pPointsExpr)
	{
		exprContextSetPointerVar(s_pContext,"Source",NULL,parse_Entity, true, true);

		exprGenerate(pDef->sEval.pPointsExpr,s_pContext);
	}
}

void leaderboard_getTimes(LeaderboardDef *pDef)
{
	int itest = 0;
	if(pDef->pchDateStart)
		pDef->iDateStart = timeGetSecondsSince2000FromDateString(pDef->pchDateStart);

	pDef->iDateInterval = (pDef->iIntervalDays * SECONDS_PER_DAY) + (pDef->iIntervalHours * SECONDS_PER_HOUR);
}

void leaderboard_validateDef(LeaderboardDef *pDef)
{ //Don't check this on the app server
#ifndef APPSERVER
	if (!GET_REF(pDef->pDisplayMessage.hMessage)) {
		if (REF_STRING_FROM_HANDLE(pDef->pDisplayMessage.hMessage)) {
			ErrorFilenamef("defs/config/leaderboards.def", "Leaderboard '%s' refers to non-existent message '%s'", pDef->pchLeaderboardKey, REF_STRING_FROM_HANDLE(pDef->pDisplayMessage.hMessage));
		} else {
			ErrorFilenamef("defs/config/leaderboards.def", "Leaderboard '%s' is missing its display name message", pDef->pchLeaderboardKey);
		}
	}
	if (!GET_REF(pDef->pDescriptionMessage.hMessage)) {
		if (REF_STRING_FROM_HANDLE(pDef->pDescriptionMessage.hMessage)) {
			ErrorFilenamef("defs/config/leaderboards.def", "Leaderboard '%s' refers to non-existent message '%s'", pDef->pchLeaderboardKey, REF_STRING_FROM_HANDLE(pDef->pDescriptionMessage.hMessage));
		} else {
			ErrorFilenamef("defs/config/leaderboards.def", "Leaderboard '%s' is missing its description message", pDef->pchLeaderboardKey);
		}
	}
#endif
}

void leaderboard_generateAll(void)
{
	int i;

	if(!s_pContext)
	{
		s_pContext = exprContextCreate();
		s_pFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_pFuncTable, "entityutil");
		exprContextAddFuncsToTableByTag(s_pFuncTable, "PTECharacter");
		exprContextSetFuncTable(s_pContext, s_pFuncTable);
		exprContextSetAllowRuntimePartition(s_pContext);
	}

	for(i=0;i<eaSize(&g_sLeaderboardDefs.ppDataDefs);i++)
	{
		LeaderboardData_generate(g_sLeaderboardDefs.ppDataDefs[i]);
	}

	for(i=0;i<eaSize(&g_sLeaderboardDefs.ppLeaderboards);i++)
	{
		LeaderboardDef *pDef = g_sLeaderboardDefs.ppLeaderboards[i];
		leaderboard_validateDef(pDef);
		leaderboard_getTimes(pDef);
#ifdef GAMESERVER
		leaderboard_generate(pDef);
#endif
	}
}

AUTO_STARTUP(Leaderboard);
void leaderboard_load(void)
{
	loadstart_printf("Loading Leader boards... ");

	ParserLoadFiles(NULL, "defs/config/leaderboards.def", "leaderboards.bin", PARSER_OPTIONALFLAG, parse_LeaderboardDefs, &g_sLeaderboardDefs);

	leaderboard_generateAll();

	loadend_printf(" done (%d Leader boards).", eaSize(&g_sLeaderboardDefs.ppLeaderboards));
}

LeaderboardStats *entity_newLeaderboardStats(Entity *pEntity, LeaderboardDef *pDef)
{
	if(pEntity && pEntity->pSaved && pDef)
	{
		LeaderboardStats *pNew = StructCreate(parse_LeaderboardStats);

		pNew->pchLeaderboard = pDef->pchLeaderboardKey;
		pNew->iLastTimestamp = timeSecondsSince2000();
		pNew->fPoints = 0.f;

		if(pDef->sEval.eRankingType != kLeaderboardRanking_Accumulate)
		{
			pNew->sRank.fDeviation = pDef->sEval.sDefaultRank.fDeviation;
			pNew->sRank.fMean = pDef->sEval.sDefaultRank.fMean;
		}

		eaPush(&pEntity->pSaved->ppLeaderboardStats,pNew);

		return pNew;
	}

	return NULL;
}

LeaderboardStats *entity_getLeaderboardStatsFromDef(Entity *pEntity, LeaderboardDef *pDef)
{
	int i;

	if(!pEntity || !pEntity->pSaved)
		return NULL;

	for(i=0;i<eaSize(&pEntity->pSaved->ppLeaderboardStats);i++)
	{
		if(pEntity->pSaved->ppLeaderboardStats[i]->pchLeaderboard == pDef->pchLeaderboardKey)
			return pEntity->pSaved->ppLeaderboardStats[i];
	}

	return NULL;
}

int leaderboard_GetIntervalFromTime(LeaderboardDef *pDef, U32 uTime)
{
	if(uTime < pDef->iDateStart)
		return -1;

	if(pDef->eType == kLeaderboardType_Ongoing || pDef->iDateInterval == 0)
		return 0;

	return (int)(pDef->iDateStart - uTime) / pDef->iDateInterval;
}

void gslLeaderboard_ServerDown(LeaderboardCBData *pData, void *pData2)
{
	Entity *pEnt = pData ? entFromEntityRef(pData->iPartitionIdx, pData->iRef) : NULL;

	if (pEnt) {
		/*
#ifdef GAMESERVER
		char *pcErrorMsg = NULL;
		entFormatGameMessageKey(pEnt, &pcErrorMsg, "LeaderboardServer_Offline", STRFMT_END);
		ClientCmd_NotifySend(pEnt, kNotifyType_TeamError, pcErrorMsg, NULL, NULL);
		estrDestroy(&pcErrorMsg);
#endif
		*/

		if(!pData->bLocked)
		{
			//Revert score change if the data was not being sent to a locked leaderboard
			int i;

			for(i=0;i<eaSize(&pEnt->pSaved->ppLeaderboardStats);i++)
			{
				if(pEnt->pSaved->ppLeaderboardStats[i]->pchLeaderboard == pData->pDef->pchLeaderboardKey)
				{
					pEnt->pSaved->ppLeaderboardStats[i]->fPoints -= pData->fChange;
				}
			}
		}
	}

	SAFE_FREE(pData);
}

LeaderboardCBData *gslLeaderboard_NewCBData(Entity *pEntity, LeaderboardDef *pDef, F32 fChange, bool bLocked)
{
	LeaderboardCBData *pData = malloc(sizeof(LeaderboardCBData));
	
	pData->iPartitionIdx = entGetPartitionIdx(pEntity);
	pData->iRef = pEntity->myRef;
	pData->pDef = pDef;
	pData->fChange = fChange;
	pData->bLocked = bLocked;

	return pData;
}

void entity_SetNewLeaderboardPointValue(Entity *pEntity, LeaderboardStats *pStats, F32 fPointValue, bool bNotify)
{
	if(pEntity && pStats)
	{
		F32 iChange = fPointValue - pStats->fPoints;

		pStats->fPoints = fPointValue;
		
		if(iChange && bNotify)
		{
			LeaderboardDef *pDef = leaderboardDef_Find(pStats->pchLeaderboard);
#ifdef GAMESERVER
			if(leaderboard_GetIntervalFromTime(pDef,pStats->iLastTimestamp) < leaderboard_GetIntervalFromTime(pDef,timeSecondsSince2000()))
			{
				RemoteCommand_leaderboard_AddScoreToLocked(GLOBALTYPE_LEADERBOARDSERVER,0,pDef->pchLeaderboardKey,pEntity->myContainerID,iChange,gslLeaderboard_ServerDown,gslLeaderboard_NewCBData(pEntity,pDef,iChange,true),NULL);
			}
			else
			{
				RemoteCommand_leaderboard_AddScoreToCurrent(GLOBALTYPE_LEADERBOARDSERVER,0,pDef->pchLeaderboardKey,pEntity->myContainerID,iChange,gslLeaderboard_ServerDown,gslLeaderboard_NewCBData(pEntity,pDef,iChange,false),NULL);
			}
#endif
		}

		//update timestamps
		pStats->iLastTimestamp = timeSecondsSince2000();
	}
}

F32 leaderboard_getPoints(LeaderboardDef *pDef, Entity *pEntity)
{
	MultiVal sResult;
	bool bResult;

	exprContextSetPointerVar(s_pContext,"Source",pEntity,parse_Entity, false, true);
	exprContextSetPartition(s_pContext, entGetPartitionIdx(pEntity));
	exprEvaluate(pDef->sEval.pPointsExpr,s_pContext,&sResult);

	return MultiValGetFloat(&sResult,&bResult);
}

const char *leaderboardData_getData(LeaderboardDataDef *pDef, Entity *pEntity)
{
	MultiVal sResult;
	static char *estrReturn = NULL;

	if(!estrReturn)
		estrCreate(&estrReturn);
	else
		estrClear(&estrReturn);

	exprContextSetPointerVar(s_pContext,"Source",pEntity,parse_Entity, false, true);
	exprContextSetPartition(s_pContext, entGetPartitionIdx(pEntity));
	exprEvaluate(pDef->pValueExpr,s_pContext,&sResult);

	MultiValToEString(&sResult,&estrReturn);

	return estrReturn;
}

void entity_updateLeaderboardData(Entity *pEntity)
{
	LeaderboardData *pNewData = StructCreate(parse_LeaderboardData);
	int i;

	pNewData->ePlayerID = pEntity->myContainerID;

	for(i=0;i<eaSize(&g_sLeaderboardDefs.ppDataDefs);i++)
	{
		LeaderboardDataEntry *pEntry = StructCreate(parse_LeaderboardDataEntry);

		pEntry->pchKey = g_sLeaderboardDefs.ppDataDefs[i]->pchKey;
		pEntry->pchValue = StructAllocString(leaderboardData_getData(g_sLeaderboardDefs.ppDataDefs[i],pEntity));

		eaPush(&pNewData->ppEntry,pEntry);
	}
#ifdef GAMESERVER
	RemoteCommand_leaderboard_SetEntityData(GLOBALTYPE_LEADERBOARDSERVER,0,(LeaderboardData *)pNewData);
#endif

	StructDestroy(parse_LeaderboardData,pNewData);
}

void entity_updateLeaderboardStats(Entity *pEntity, LeaderboardDef *pLeaderboardDef)
{
	//Find current score
	if(pLeaderboardDef && pLeaderboardDef->sEval.pPointsExpr)
	{
		int fPoints;
		LeaderboardStats *pStats = entity_getLeaderboardStatsFromDef(pEntity,pLeaderboardDef);

		fPoints = leaderboard_getPoints(pLeaderboardDef,pEntity);

		if(!pStats && fPoints)
		{
			pStats = entity_newLeaderboardStats(pEntity,pLeaderboardDef);
		}

		entity_SetNewLeaderboardPointValue(pEntity,pStats,fPoints,true);
	}
}

void entity_updateLeaderboardStatsAll(Entity *pEntity)
{
	int i;

	if (entGetVirtualShardID(pEntity) != 0)
	{
		return;
	}

	for(i=0;i<eaSize(&g_sLeaderboardDefs.ppLeaderboards);i++)
	{
		entity_updateLeaderboardStats(pEntity,g_sLeaderboardDefs.ppLeaderboards[i]);
	}

	entity_updateLeaderboardData(pEntity);
}

void entity_validateLeaderboardStats(Entity *pEntity)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	for(i=0;i<eaSize(&g_sLeaderboardDefs.ppLeaderboards);i++)
	{
		LeaderboardDef *pDef = g_sLeaderboardDefs.ppLeaderboards[i];
		LeaderboardStats *pStats = entity_getLeaderboardStatsFromDef(pEntity,pDef);

		if(!pStats)
		{
			F32 fPoints;

			pStats = entity_newLeaderboardStats(pEntity,pDef);
			fPoints = leaderboard_getPoints(pDef,pEntity);

			entity_SetNewLeaderboardPointValue(pEntity,pStats,fPoints,false);
		}
	}

	PERFINFO_AUTO_STOP();
}

void entity_updateLockedLeaderboardStats(Entity *pEntity)
{
	int i;

	if (entGetVirtualShardID(pEntity) != 0)
	{
		return;
	}

	for(i=0;i<eaSize(&g_sLeaderboardDefs.ppLeaderboards);i++)
	{
		int j;

		for(j=0;j<eaSize(&pEntity->pSaved->ppLeaderboardStats);j++)
		{
			if(pEntity->pSaved->ppLeaderboardStats[j]->pchLeaderboard == g_sLeaderboardDefs.ppLeaderboards[i]->pchLeaderboardKey)
			{
				if(leaderboard_GetIntervalFromTime(g_sLeaderboardDefs.ppLeaderboards[i],pEntity->pSaved->ppLeaderboardStats[j]->iLastTimestamp) < leaderboard_GetIntervalFromTime(g_sLeaderboardDefs.ppLeaderboards[i],timeSecondsSince2000()))
				{
					entity_updateLeaderboardStats(pEntity,g_sLeaderboardDefs.ppLeaderboards[i]);
				}

				break;
			}
		}
	}
}


#include "AutoGen/Leaderboard_h_ast.c"