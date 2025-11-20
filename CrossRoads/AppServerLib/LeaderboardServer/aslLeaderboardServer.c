/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLeaderboardServer.h"

#include "AutoTransDefs.h"
#include "aslLeaderboardDB.h"
#include "GlobalTypes.h" 
#include "mathutil.h"
#include "serverlib.h"
#include "timing.h"

#include "autogen/controller_autogen_remotefuncs.h"
#include "autogen/Leaderboard_h_ast.h"
#include "autogen/aslLeaderboardServer_h_ast.h"
#include "autogen/AppServerLib_autotransactions_autogen_wrappers.h"



AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

Leaderboard **ppLeaderboardAll;

Leaderboard **ppLeaderboardWaiting;
Leaderboard **ppLeaderboardCurrent;
Leaderboard **ppLeaderboardLocked;

LeaderboardData **ppPlayerData;

int ranking_ComparePlayerID(const Ranking** a, const Ranking** b) {
	return (*a)->ePlayerID - (*b)->ePlayerID;
}

int leaderboard_CompareKey(const Leaderboard** a, const Leaderboard** b) {
	return strcmp((*a)->pchKey,(*b)->pchKey);
}

int ranking_CompareScore(const Ranking** a, const Ranking** b) {
	F32 fReturn = (*b)->fScore - (*a)->fScore;

	return fReturn == 0 ? 0 : fReturn > 0 ? 1 : -1;
}

int ranking_CompareAll(const Ranking** a, const Ranking** b) {
	if((*a)->fScore == (*b)->fScore)
	{
		if((*a)->ePlayerID == (*b)->ePlayerID)
			return 0;
		else
			return (*a)->iLastUpdate - (*b)->iLastUpdate;
	}
	else
	{
		return ranking_CompareScore(a,b);
	}
}

Leaderboard *leaderboard_Find(char *pchKey)
{
	int i;
	Leaderboard sFakeLeaderboard = {0};
	Leaderboard *sFakeLeaderboardPointer = &sFakeLeaderboard;

	if(!ppLeaderboardAll)
		return NULL;

	sFakeLeaderboard.pchKey = pchKey;

	i = (int)eaBFind(ppLeaderboardAll,leaderboard_CompareKey,sFakeLeaderboardPointer);

	if(i >= 0 && strcmp(ppLeaderboardAll[i]->pchKey,pchKey) == 0)
		return ppLeaderboardAll[i];

	return NULL;
}

char *leaderboard_CreateName(const char *pDefName, int iInterval)
{
	static char *pchReturn = NULL;

	if(!pchReturn)
		estrCreate(&pchReturn);
	else
		estrClear(&pchReturn);

	estrPrintf(&pchReturn,"%s_%d",pDefName,iInterval);
	
	return pchReturn;
}

void leaderboard_createNew(LeaderboardDef *pDef, int iInterval)
{
	Leaderboard *pNew = NULL;

	if(pDef)
	{
		pNew = StructCreate(parse_Leaderboard);

		pNew->pchKey = StructAllocString(leaderboard_CreateName(pDef->pchLeaderboardKey,iInterval));
		pNew->pchLeaderboard = pDef->pchLeaderboardKey;

		pNew->eStatus = kLeaderboard_Waiting;

		pNew->iDateStart = pDef->iDateStart + (pDef->iDateInterval * iInterval);

		if(pDef->eType != kLeaderboardType_Ongoing)
		{
			pNew->iDateEnd = pDef->iDateStart + (pDef->iDateInterval * (iInterval+1));
			pNew->iInterval = iInterval;
		}

		eaPush(&ppLeaderboardAll,pNew);
		eaPush(&ppLeaderboardWaiting,pNew);
	}
}

void leaderboard_createWaiting()
{
	int i;
	
	for(i=eaSize(&g_sLeaderboardDefs.ppLeaderboards)-1;i>=0;i--)
	{
		int n;
		for(n=eaSize(&ppLeaderboardWaiting)-1;n>=0;n--)
		{
			if(ppLeaderboardWaiting[n]->pchLeaderboard == g_sLeaderboardDefs.ppLeaderboards[i]->pchLeaderboardKey)
				break;
		}

		if(n==-1)
			leaderboard_createNew(g_sLeaderboardDefs.ppLeaderboards[i],0);
	}
}

void leaderboard_ProcessWaiting()
{
	int i;
	int iCurrentTime = timeSecondsSince2000();

	for(i=eaSize(&ppLeaderboardWaiting)-1;i>=0;i--)
	{
		Leaderboard *pLeaderboard = ppLeaderboardWaiting[i];

		if(pLeaderboard->iDateStart <= iCurrentTime)
		{
			LeaderboardDef *pDef = leaderboardDef_Find(pLeaderboard->pchLeaderboard);
			//Make this leader board current
			eaRemoveFast(&ppLeaderboardWaiting,i);
			
			pLeaderboard->eStatus = kLeaderboard_Current;
			eaPush(&ppLeaderboardCurrent,pLeaderboard);

			if(pDef && pDef->eType == kLeaderboardType_Interval)
				leaderboard_createNew(pDef,pLeaderboard->iInterval+1);
		}
	}
}

void leaderboard_ProcessCurrent()
{
	int i;

	int iCurrentTime = timeSecondsSince2000();

	for(i=eaSize(&ppLeaderboardCurrent)-1;i>=0;i--)
	{
		Leaderboard *pLeaderboard = ppLeaderboardCurrent[i];
		LeaderboardDef *pDef = leaderboardDef_Find(pLeaderboard->pchLeaderboard);

		if(pDef && pDef->eType != kLeaderboardType_Ongoing && pLeaderboard->iDateEnd <= iCurrentTime)
		{
			//Make this leader board locked
			eaRemoveFast(&ppLeaderboardCurrent,i);

			pLeaderboard->eStatus = kLeaderboard_Locked;
			eaPush(&ppLeaderboardLocked,pLeaderboard);
		}
	}
}

void leaderboard_ProcessLocked()
{
	int i;

	for(i=0;i<eaSize(&ppLeaderboardLocked);i++)
	{
		if(timeSecondsSince2000() - ppLeaderboardLocked[i]->iDateEnd >= 600)
		{
			//Final save
			leaderboardDBAddToHog(ppLeaderboardLocked[i]);
			eaRemove(&ppLeaderboardLocked,i);
		}
	}
}

Ranking *leaderboard_FindRankingByPlayerID(Leaderboard *pLeaderboard, ContainerID uiPlayerID)
{
	Ranking sFakeRanking = {0};
	Ranking *sFakeRankingPointer = &sFakeRanking;
	int iPos = -1;

	if(!pLeaderboard)
		return NULL;

	sFakeRanking.ePlayerID = uiPlayerID;
	iPos = (int)eaBFind(pLeaderboard->ppPlayerSorted,ranking_ComparePlayerID,sFakeRankingPointer);

	if(iPos != -1 && iPos < eaSize(&pLeaderboard->ppPlayerSorted))
	{
		return pLeaderboard->ppPlayerSorted[iPos];
	}

	return NULL;
}

int leaderboard_FindPlayersCurrentRanking(Leaderboard *pLeaderboard, Ranking *pPlayer)
{
	int iRank = (int)eaBFind(pLeaderboard->ppRankings,ranking_CompareAll,pPlayer);

	if(iRank == -1)
		return eaSize(&pLeaderboard->ppRankings);

	return iRank;
}

Leaderboard *leaderboard_FindByDef(Leaderboard **ppLeaderboards,const char *pchDefKey)
{
	int i;

	for(i=0;i<eaSize(&ppLeaderboards);i++)
	{
		if(strcmp(ppLeaderboards[i]->pchLeaderboard,pchDefKey) == 0)
			return ppLeaderboards[i];
	}

	return NULL;
}

int leaderboard_FindRankingForScore(Leaderboard *pLeaderboard, F32 fScore, ContainerID ePlayerID)
{
	int high = eaSize(&pLeaderboard->ppRankings);
	int low = 0;
	bool bFound = false;

	if(!pLeaderboard->ppRankings || high == 0)
		return 0;

	while (high > low)
	{
		int mid = (high + low) / 2;
		F32 comp = pLeaderboard->ppRankings[mid]->fScore - fScore;
		
		if (comp == 0)
		{	
			if(pLeaderboard->ppRankings[mid]->ePlayerID == ePlayerID)
				low = high = mid;
			else
				comp = 1;
		}
		
		if (comp < 0)
			high = mid;
		else if (mid == low)
			low = high;
		else
			low = mid;
	}
	return low;
}

void leaderboard_AddNewPlayerRank(Leaderboard *pLeaderboard, ContainerID uiPlayerID, LeaderboardRank sRank)
{
	Ranking *pNew = StructCreate(parse_Ranking);
	int iRanking = 0;

	pNew->ePlayerID = uiPlayerID;
	pNew->sRank.fDeviation = sRank.fDeviation;
	pNew->sRank.fMean = sRank.fMean;
	pNew->fScore = sRank.fMean - (sRank.fDeviation * 3);
	pNew->iLastUpdate = pLeaderboard->iUpdates++;

	iRanking = leaderboard_FindRankingForScore(pLeaderboard,pNew->fScore,uiPlayerID);

	eaInsert(&pLeaderboard->ppRankings,pNew,iRanking);
	eaPush(&pLeaderboard->ppPlayerSorted,pNew);
}

void leaderboard_AddNewPlayer(Leaderboard *pLeaderboard, ContainerID uiPlayerID, F32 fScore)
{
	Ranking *pNew = StructCreate(parse_Ranking);
	int iRanking = leaderboard_FindRankingForScore(pLeaderboard,fScore,uiPlayerID);

	if(!(iRanking == 0 || pLeaderboard->ppRankings[iRanking-1]->fScore >= fScore))
	{
		int i=0;
	}

	pNew->ePlayerID = uiPlayerID;
	pNew->fScore = fScore;
	pNew->iLastUpdate = pLeaderboard->iUpdates++;

	eaInsert(&pLeaderboard->ppRankings,pNew,iRanking);
	eaPush(&pLeaderboard->ppPlayerSorted,pNew);
}

void leaderboard_AddPoints(Leaderboard *pLeaderboard, ContainerID uiPlayerID, F32 fScore)
{
	if(pLeaderboard)
	{
		Ranking *pPlayerRanking = leaderboard_FindRankingByPlayerID(pLeaderboard,uiPlayerID);
		if(pPlayerRanking)
		{
			int iCurRank = leaderboard_FindPlayersCurrentRanking(pLeaderboard,pPlayerRanking);
			int iNextRank = 0;

			fScore += pPlayerRanking->fScore;
			iNextRank = leaderboard_FindRankingForScore(pLeaderboard,fScore,pPlayerRanking->ePlayerID);

			if(!(iNextRank == 0 || pLeaderboard->ppRankings[iNextRank-1]->fScore >= fScore))
			{
				int i=0;
			}

			pPlayerRanking->fScore = fScore;

			if(iCurRank!=iNextRank)
			{
				pPlayerRanking->iLastUpdate = pLeaderboard->iUpdates++;
				eaMove(&pLeaderboard->ppRankings,iCurRank,iNextRank);
			}
		}
		else
		{
			leaderboard_AddNewPlayer(pLeaderboard,uiPlayerID, fScore);
		}
	}
}

void leaderboard_SetRank(Leaderboard *pLeaderboard, ContainerID uiPlayerID, LeaderboardRank sRank)
{
	if(pLeaderboard)
	{
		Ranking *pPlayerRanking = leaderboard_FindRankingByPlayerID(pLeaderboard,uiPlayerID);
		if(pPlayerRanking)
		{
			int iCurRank = leaderboard_FindPlayersCurrentRanking(pLeaderboard,pPlayerRanking);
			int iNextRank = 0;

			pPlayerRanking->sRank.fMean = sRank.fMean;
			pPlayerRanking->sRank.fDeviation = sRank.fDeviation;
			pPlayerRanking->fScore = sRank.fMean - (sRank.fDeviation * 3);

			iNextRank = leaderboard_FindRankingForScore(pLeaderboard,pPlayerRanking->fScore,pPlayerRanking->ePlayerID);

			if(iCurRank!=iNextRank)
			{
				pPlayerRanking->iLastUpdate = pLeaderboard->iUpdates++;
				eaMove(&pLeaderboard->ppRankings,iCurRank,iNextRank);
			}
		}
		else
		{
			leaderboard_AddNewPlayerRank(pLeaderboard,uiPlayerID,sRank);
		}
	}
}

void leaderboard_autoSave(void)
{
	int i;
	S64 iMSeconds = timeMsecsSince2000();

	printf("Leaderboard Autosave...\n");
	for(i=0;i<eaSize(&ppLeaderboardCurrent);i++)
	{
		leaderboardDBAddToHog(ppLeaderboardCurrent[i]);
	}

	for(i=0;i<eaSize(&ppLeaderboardWaiting);i++)
	{
		leaderboardDBAddToHog(ppLeaderboardWaiting[i]);
	}

	printf("Done <%"FORM_LL"d>\n",timeMsecsSince2000() - iMSeconds);
}

/*
#include "rand.h"
#include "StringCache.h"
void leaderboard_AddScoreToCurrent(const char *pchLeaderboard, ContainerID ePlayerID, F32 fPoints);
void leaderboard_SetEntityData(LeaderboardData *pData);

void leaderboard_test()
{
	static int iCount = 0;
	int i;
	char *pchData = NULL;

	if(iCount >= 1000000)
		return;

	printf("Leaderboard Test: Adding Players %d to %d\n",iCount,iCount+9999);

	estrCreate(&pchData);

	for(i=0;i<10000;i++)
	{
		LeaderboardData *pData = StructCreate(parse_LeaderboardData);
		LeaderboardDataEntry *pEntry = NULL;

		pData->ePlayerID = iCount;
		
		leaderboard_AddScoreToCurrent("TimePlayed",iCount,randomPositiveF32() * 1000);
		
		estrPrintf(&pchData, "Player %d", iCount);

		pEntry = StructCreate(parse_LeaderboardDataEntry);
		pEntry->pchKey = allocAddString("Name");
		pEntry->pchValue = StructAllocString(pchData);
		
		eaPush(&pData->ppEntry,pEntry);

		pEntry = StructCreate(parse_LeaderboardDataEntry);
		pEntry->pchKey = allocAddString("Class");
		pEntry->pchValue = StructAllocString("Test Entry");
		
		eaPush(&pData->ppEntry,pEntry);

		estrPrintf(&pchData, "%d", randomIntRange(1,50));

		pEntry = StructCreate(parse_LeaderboardDataEntry);
		pEntry->pchKey = allocAddString("Level");
		pEntry->pchValue = StructAllocString(pchData);

		eaPush(&pData->ppEntry,pEntry);

		leaderboard_SetEntityData(pData);

		StructDestroy(parse_LeaderboardData,pData);

		iCount++;
	}

	estrDestroy(&pchData);
}
*/

void LeaderboardInit(void)
{
	eaIndexedEnable(&ppPlayerData,parse_LeaderboardData);
}

int LeaderboardLibOncePerFrame(F32 fElapsed)
{
	static U32 iLastAutoSave = 0;
	static bool bOnce = false;

	if(!bOnce) {
		RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
		ATR_DoLateInitialization();

		bOnce = true;

		leaderboard_createWaiting();
	}

	//leaderboard_test();

	if (timeSecondsSince2000() - iLastAutoSave >= LEADERBOARD_AUTOSAVE_INTERVAL)
	{
		leaderboard_autoSave();
		iLastAutoSave = timeSecondsSince2000();
	}

	leaderboard_ProcessLocked();
	leaderboard_ProcessCurrent();
	leaderboard_ProcessWaiting();
	return 1;
}

F32 CumlativeTo(F32 x)
{
	int i;
	F32 t;
	F32 ty;
	F32 d = 0;
	F32 dd = 0;
	F32 z;
	F32 coefficients[] = {
		-1.3026537197817094, 6.4196979235649026e-1,
		1.9476473204185836e-2, -9.561514786808631e-3, -9.46595344482036e-4,
		3.66839497852761e-4, 4.2523324806907e-5, -2.0278578112534e-5,
		-1.624290004647e-6, 1.303655835580e-6, 1.5626441722e-8, -8.5238095915e-8,
		6.529054439e-9, 5.059343495e-9, -9.91364156e-10, -2.27365122e-10,
		9.6467911e-11, 2.394038e-12, -6.886027e-12, 8.94487e-13, 3.13092e-13,
		-1.12708e-13, 3.81e-16, 7.106e-15, -1.523e-15, -9.4e-17, 1.21e-16, -2.8e-17
	};

	x = x* (-0.707106781186547524400844362104);
	z = x < 0 ? x * -1 : x;

	t = 2.0/(2.0 + z);
	ty = 4*t - 2;
	
	for(i=27;i>0;i--)
	{
		F32 tmp = d;
		d = ty*d - dd + coefficients[i];
		dd = tmp;
	}

	z = t*exp(-z*z+0.5*(coefficients[0] + ty*d) - dd);
	x = x >= 0.0 ? z : (2.0 - z);

	return x*0.5;
}

F32 GaussianDistributionAt(F32 x, F32 mean, F32 standardDeviation)
{
	F32 multiplier = 1.0/(standardDeviation*sqrt(2*PI));
	F32 expPart = exp((-1.0*pow(x - mean, 2.0))/(2*(standardDeviation*standardDeviation)));
	F32 result = multiplier*expPart;

	return result;
}

F32 leaderboard_VExceedsMargin(F32 teamPerformanceDifference, F32 drawMargin)
{
	F32 denominator;

	teamPerformanceDifference = teamPerformanceDifference;
	drawMargin = drawMargin;

	denominator = CumlativeTo(teamPerformanceDifference - drawMargin);

	return GaussianDistributionAt(teamPerformanceDifference - drawMargin,0,1)/denominator;
}

F32 leaderboard_WExceedsMargin(F32 teamPerformanceDifference, F32 drawMargin, F32 c)
{
	F32 denominator;
	F32 fWin;

	teamPerformanceDifference = teamPerformanceDifference/c;
	drawMargin = drawMargin/c;
	denominator = CumlativeTo(teamPerformanceDifference - drawMargin);

	fWin = leaderboard_VExceedsMargin(teamPerformanceDifference,drawMargin);
	return fWin*(fWin + teamPerformanceDifference - drawMargin);
}


//TODO(MM): work in the draw sceneario 
void leaderboard_UpdatePlayerRatingsEx(Leaderboard *pLeaderboard, LeaderboardTeam *pWinningTeam, LeaderboardTeam *pLosingTeam, bool bDraw)
{
	F32 fWinMean,fLoseMean,fMeanDelta,fDeviation,fTauSquared,fV,fW;
	int i,iTotalPlayers = 0;
	LeaderboardDef *pDef = leaderboardDef_Find(pLeaderboard->pchLeaderboard);
	LeaderboardRank sDefaultRank;
	Ranking **ppWinningTeam = NULL;
	Ranking **ppLosingTeam = NULL;

	fTauSquared = pDef->sEval.fRankingDynamicsFactor * pDef->sEval.fRankingDynamicsFactor;

	sDefaultRank.fDeviation = pDef->sEval.sDefaultRank.fDeviation;
	sDefaultRank.fMean = pDef->sEval.sDefaultRank.fMean;

	fWinMean = 0;
	fDeviation=0;

	eaIndexedEnable(&ppWinningTeam,parse_Ranking);
	eaIndexedEnable(&ppLosingTeam,parse_Ranking);
	for(i=0;i<ea32Size(&pWinningTeam->piTeam);i++)
	{
		Ranking *pPlayerRanking = leaderboard_FindRankingByPlayerID(pLeaderboard,pWinningTeam->piTeam[i]);

		if(pPlayerRanking)
		{
			fWinMean += pPlayerRanking->sRank.fMean;
			fDeviation += pPlayerRanking->sRank.fDeviation * pPlayerRanking->sRank.fDeviation;
			eaPush(&ppWinningTeam,pPlayerRanking);
		}
		else
		{
			fWinMean += sDefaultRank.fMean;
			fDeviation += sDefaultRank.fDeviation * sDefaultRank.fDeviation;
		}

		iTotalPlayers++;
	}

	fLoseMean = 0;
	for(i=0;i<ea32Size(&pLosingTeam->piTeam);i++)
	{
		Ranking *pPlayerRanking = leaderboard_FindRankingByPlayerID(pLeaderboard,pWinningTeam->piTeam[i]);

		if(pPlayerRanking)
		{
			fLoseMean += pPlayerRanking->sRank.fMean;
			fDeviation += pPlayerRanking->sRank.fDeviation * pPlayerRanking->sRank.fDeviation;
			eaPush(&ppLosingTeam,pPlayerRanking);
		}
		else
		{
			fLoseMean += sDefaultRank.fMean;
			fDeviation += sDefaultRank.fDeviation * sDefaultRank.fDeviation;
		}

		iTotalPlayers++;
	}

	fDeviation += iTotalPlayers * (pDef->sEval.fRankingBeta * pDef->sEval.fRankingBeta);
	fDeviation = sqrt(fDeviation);

	//square default rank
	sDefaultRank.fDeviation = sDefaultRank.fDeviation * sDefaultRank.fDeviation;
	sDefaultRank.fMean = sDefaultRank.fMean * sDefaultRank.fMean;

	fMeanDelta = fWinMean - fLoseMean;

	fV = leaderboard_VExceedsMargin(fMeanDelta/fDeviation,0.f);
	fW = leaderboard_WExceedsMargin(fMeanDelta,0.f,fDeviation);

	for(i=0;i<ea32Size(&pWinningTeam->piTeam);i++)
	{
		int iRanking = eaIndexedFindUsingInt(&ppWinningTeam,pWinningTeam->piTeam[i]);
		LeaderboardRank sRank;
		F32 fMeanMultiplier, stdDevMultiplier, fPlayerMeanDelta, fNewMean, fNewDev;

		if(iRanking > -1)
		{
			sRank.fDeviation = ppWinningTeam[iRanking]->sRank.fDeviation;
			sRank.fMean = ppWinningTeam[iRanking]->sRank.fMean;
		}
		else
		{
			sRank.fDeviation = pDef->sEval.sDefaultRank.fDeviation;
			sRank.fMean = pDef->sEval.sDefaultRank.fMean;
		}

		fMeanMultiplier = (sRank.fDeviation * sRank.fDeviation + fTauSquared) / fDeviation;
		stdDevMultiplier = (sRank.fDeviation * sRank.fDeviation + fTauSquared) / (fDeviation * fDeviation);

		fPlayerMeanDelta = (fMeanMultiplier*fV);
		fNewMean = sRank.fMean + fPlayerMeanDelta;
		fNewDev = sqrt((sRank.fDeviation * sRank.fDeviation + fTauSquared)*(1-fW*stdDevMultiplier));

		sRank.fMean = fNewMean;
		sRank.fDeviation = fNewDev;

		leaderboard_SetRank(pLeaderboard,pWinningTeam->piTeam[i],sRank);
	}

	for(i=0;i<ea32Size(&pLosingTeam->piTeam);i++)
	{
		int iRanking = eaIndexedFindUsingInt(&ppLosingTeam,pWinningTeam->piTeam[i]);
		LeaderboardRank sRank;
		F32 fMeanMultiplier, stdDevMultiplier, fPlayerMeanDelta, fNewMean, fNewDev;

		if(iRanking > -1)
		{
			sRank.fDeviation = ppWinningTeam[iRanking]->sRank.fDeviation;
			sRank.fMean = ppWinningTeam[iRanking]->sRank.fMean;
		}
		else
		{
			sRank.fDeviation = pDef->sEval.sDefaultRank.fDeviation;
			sRank.fMean = pDef->sEval.sDefaultRank.fMean;
		}

		fMeanMultiplier = (sRank.fDeviation * sRank.fDeviation + fTauSquared) / fDeviation;
		stdDevMultiplier = (sRank.fDeviation * sRank.fDeviation + fTauSquared) / (fDeviation * fDeviation);

		fPlayerMeanDelta = (-1*fMeanMultiplier*fV);
		fNewMean = sRank.fMean + fPlayerMeanDelta;
		fNewDev = sqrt((sRank.fDeviation * sRank.fDeviation + fTauSquared)*(1-fW*stdDevMultiplier));

		sRank.fMean = fNewMean;
		sRank.fDeviation = fNewDev;

		leaderboard_SetRank(pLeaderboard,pLosingTeam->piTeam[i],sRank);
	}
}

AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK ACMD_IFDEF(GAMESERVER);
void leaderboard_UpdatePlayerRatings(const char *pchLeaderboard, LeaderboardTeam *pWinningTeam, LeaderboardTeam *pLosingTeam, bool bDraw)
{
	int i;

	for(i=0;i<eaSize(&ppLeaderboardCurrent);i++)
	{
		if(stricmp(ppLeaderboardCurrent[i]->pchLeaderboard,pchLeaderboard)==0)
		{
			leaderboard_UpdatePlayerRatingsEx(ppLeaderboardCurrent[i],pWinningTeam,pLosingTeam,bDraw);
			return;
		}
	}
}

// Only add the data to the current leader board
AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK ACMD_IFDEF(GAMESERVER);
void leaderboard_AddScoreToCurrent(const char *pchLeaderboard, ContainerID ePlayerID, F32 fPoints)
{
	int i;

	for(i=0;i<eaSize(&ppLeaderboardCurrent);i++)
	{
		if(stricmp(ppLeaderboardCurrent[i]->pchLeaderboard,pchLeaderboard)==0)
		{
			leaderboard_AddPoints(ppLeaderboardCurrent[i],ePlayerID,fPoints);
			return;
		}
	}
}

// The server is sending last minute leader board data, add to the locked leader board if there is still time
// other wise, add to a current leader board
AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK ACMD_IFDEF(GAMESERVER);
void leaderboard_AddScoreToLocked(const char *pchLeaderboard, ContainerID ePlayerID, F32 fPoints)
{
	int i;

	for(i=0;i<eaSize(&ppLeaderboardLocked);i++)
	{
		if(strcmp(ppLeaderboardLocked[i]->pchLeaderboard,pchLeaderboard)==0)
		{
			leaderboard_AddPoints(ppLeaderboardLocked[i],ePlayerID,fPoints);
			return;
		}
	}

	//Locked leader board doesn't exist anymore, add score to current leader board
	leaderboard_AddScoreToCurrent(pchLeaderboard,ePlayerID,fPoints);
}

AUTO_COMMAND_REMOTE;
F32 leaderboard_RequestRankForQueue(const char *pchLeaderboard, ContainerID ePlayerID)
{
	int i;

	for(i=0;i<eaSize(&ppLeaderboardCurrent);i++)
	{
		if(stricmp(ppLeaderboardCurrent[i]->pchLeaderboard,pchLeaderboard)==0)
		{
			Ranking *pPlayerRanking = leaderboard_FindRankingByPlayerID(ppLeaderboardCurrent[i],ePlayerID);

			if(pPlayerRanking)
			{
				return pPlayerRanking->sRank.fMean;
			}
			else
			{
				LeaderboardDef *pDef = leaderboardDef_Find(pchLeaderboard);

				if(pDef)
					return pDef->sEval.sDefaultRank.fMean;
			}
		}
	}
	
	return 0.0f;
}

int leaderboardData_compare(const LeaderboardData **a, const LeaderboardData **b)
{
	return (*a)->ePlayerID - (*b)->ePlayerID;
}

LeaderboardData *leaderboard_FindPlayerData(ContainerID ePlayerID)
{

	LeaderboardData sFakeData = {0};
	LeaderboardData *sFakePointer = &sFakeData;
	int iLoc = -1;

	if(!ppPlayerData || eaSize(&ppPlayerData) == 0)
		return NULL;

	sFakeData.ePlayerID = ePlayerID;
	
	iLoc = (int)eaBFind(ppPlayerData,leaderboardData_compare,sFakePointer);

	if(iLoc != -1 && iLoc < eaSize(&ppPlayerData) && ppPlayerData[iLoc]->ePlayerID == ePlayerID)
		return ppPlayerData[iLoc];

	return NULL;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void leaderboard_SetEntityData(LeaderboardData *pData)
{
	if(pData)
	{
		LeaderboardData *pPlayerData = leaderboard_FindPlayerData(pData->ePlayerID);

		if(pPlayerData)
		{
			char *pchDiffString = NULL;

			estrCreate(&pchDiffString);

			StructWriteTextDiff(&pchDiffString,parse_LeaderboardDataEntry,pData,pPlayerData,NULL,0,0,0);

			if(estrLength(&pchDiffString))
			{
				eaClearStruct(&pPlayerData->ppEntry,parse_LeaderboardDataEntry);
				eaCopyStructs(&pData->ppEntry,&pPlayerData->ppEntry,parse_LeaderboardDataEntry);

				leaderboardDataDBAddToHog(pData);
			}
		}
		else
		{
			eaIndexedAdd(&ppPlayerData,StructClone(parse_LeaderboardData,pData));

			leaderboardDataDBAddToHog(pData);
		}
		
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
LeaderboardPage *leaderboard_GetLeaderboardPage(LeaderboardPageRequest *pRequest)
{
	LeaderboardPage *pLeaderboardPage = NULL;
	Leaderboard *pLeaderboard = NULL;
	LeaderboardDef *pDef = NULL;
	U32 i;
	int iCurRank;

	pLeaderboardPage = StructCreate(parse_LeaderboardPage);

	pLeaderboardPage->pchLeaderboardKey = pRequest->pchLeaderboardKey;
	pLeaderboardPage->iInterval = pRequest->iInterval;

	if(pRequest->iInterval == -1)
	{
		pDef = leaderboardDef_Find(pRequest->pchLeaderboardKey);
		pLeaderboard = leaderboard_FindByDef(ppLeaderboardCurrent,pRequest->pchLeaderboardKey);
	}
	else
	{
		char schKey[255];

		sprintf(schKey,"%s_%d",pRequest->pchLeaderboardKey,pRequest->iInterval);

		pLeaderboard = leaderboard_Find(leaderboard_CreateName(pRequest->pchLeaderboardKey,pRequest->iInterval));
	}

	if(!pLeaderboard || !pLeaderboard->ppRankings)
		return NULL;

	if(pRequest->ePlayerSearch)
	{
		Ranking *pPlayersRanking = leaderboard_FindRankingByPlayerID(pLeaderboard,pRequest->ePlayerSearch);
		iCurRank = leaderboard_FindPlayersCurrentRanking(pLeaderboard,pPlayersRanking);

		iCurRank -= iCurRank % pRequest->iRankingsPerPage;
	}
	else
	{
		iCurRank = pRequest->iPageSearch * pRequest->iRankingsPerPage;
	}

	for(i=0;iCurRank<eaSize(&pLeaderboard->ppRankings)&&i<=pRequest->iRankingsPerPage;i++&&iCurRank++)
	{
		LeaderboardPageEntry *pEntry = StructCreate(parse_LeaderboardPageEntry);
		LeaderboardData *pPlayerData = leaderboard_FindPlayerData(pLeaderboard->ppRankings[iCurRank]->ePlayerID);

		pEntry->ePlayerID = pLeaderboard->ppRankings[iCurRank]->ePlayerID;
		pEntry->fScore = pLeaderboard->ppRankings[iCurRank]->fScore;
		pEntry->iRank = iCurRank+1;

		if(pPlayerData && eaSize(&pPlayerData->ppEntry) > 0)
			eaCopyStructs(&pPlayerData->ppEntry,&pEntry->ppPlayerData,parse_LeaderboardDataEntry);

		eaPush(&pLeaderboardPage->ppEntries,pEntry);
	}

	return pLeaderboardPage;
}

void leaderboard_AddTolist(Leaderboard *pLeaderboard)
{
	eaPush(&ppLeaderboardAll,pLeaderboard);

	if(pLeaderboard->eStatus == kLeaderboard_Current)
		eaPush(&ppLeaderboardCurrent,pLeaderboard);

	if(pLeaderboard->eStatus == kLeaderboard_Waiting)
		eaPush(&ppLeaderboardWaiting,pLeaderboard);
}

void leaderboard_SortRankings(Leaderboard *pLeaderboard)
{
	eaCopy(&pLeaderboard->ppRankings,&pLeaderboard->ppPlayerSorted);

	eaQSort(pLeaderboard->ppRankings,ranking_CompareAll);
}

void leaderboarddata_postLoad(LeaderboardData *pData)
{
	eaIndexedAdd(&ppPlayerData,pData);
}

void leaderboard_postLoad(Leaderboard *pLeaderboard)
{
	leaderboard_SortRankings(pLeaderboard);

	leaderboard_AddTolist(pLeaderboard);
}

#include "autogen/aslLeaderboardServer_h_ast.c"