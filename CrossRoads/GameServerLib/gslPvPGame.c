/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "gslPvPGame.h"
#include "PvPGameCommon.h"

#include "gslCritter.h"
#include "gslQueue.h"
#include "gslMapState.h"
#include "gslPartition.h"
#include "gslPlayerMatchStats.h"
#include "gslNamedPoint.h"
#include "gslInteractable.h"
#include "gslEntity.h"
#include "gslPowerTransactions.h"
#include "gslEncounter.h"

#include "Entity.h"
#include "EntityLib.h"
#include "Player.h"
#include "PowerActivation.h"
#include "wlEncounter.h"
#include "Reward.h"
#include "rewardCommon.h"
#include "Character.h"
#include "Powers.h"
#include "PowersMovement.h"
#include "cmdServerCombat.h"
#include "mathutil.h"
#include "wlInteraction.h"
#include "gslEventSend.h"
#include "NotifyCommon.h"
#include "GameStringFormat.h"
#include "fileutil.h"
#include "PowerAnimFX.h"
#include "PowersMovement.h"
#include "dynFxManager.h"
#include "Leaderboard.h"
#include "inventoryCommon.h"
#include "StaticWorld/ZoneMap.h"

#include "PvPGameCommon_h_ast.h"
#include "Powers_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
//#include "autogen/GameServerLib_autogen_RemoteFuncs.h"
#include "autogen/AppServerLib_autogen_RemoteFuncs.h"
#include "queue_common_structs.h"
#include "queue_common_structs_h_ast.h"
#include "Leaderboard_h_ast.h"


#include "gslPVPGame_h_ast.h"

#define PVPMaxGameFinishedTime 120.f

#define PVPFlagCaptureDistance 6.0f

#define PVPGamePoints "PVPGame_Points"
#define PVPGamePointsWithMSG "PVPGame_PointsWithMessage"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


int iGameAutoStart = 0;

AUTO_CMD_INT(iGameAutoStart,pvpGameAutoStart) ACMD_CMDLINE;

typedef struct PvPEntityCallbackData {
	int iPartitionIdx;
	EntityRef ref;
} PvPEntityCallbackData;

static PVPGamePointsDef s_PointsStruct = {0};

static PVPCurrentGameDetails **g_eaGameDetailsPartition = NULL;

PVPCurrentGameDetails * gslPvPGameDetailsFromIdx(int iPartitionIdx)
{
	return eaGet(&g_eaGameDetailsPartition,iPartitionIdx);
}

void gslPVPGame_recordEvent(U32 uiPartition, PvPEvent eEvent, Entity *pSource, Entity *pTarget)
{
	static Entity **ppSources = NULL;
	static Entity **ppTargets = NULL;

	eaClear(&ppSources);
	eaClear(&ppTargets);

	if(pSource)
		eaPush(&ppSources,pSource);

	if(pTarget)
		eaPush(&ppTargets,pTarget);

	eventsend_RecordPvPEvent(uiPartition, eEvent,ppSources,ppTargets);
}

void gslPVPGame_awardPoints(Entity *pEntity, int iPoints, const char *pchMessage)
{
	static char *estr;
	MapState *pState;
	MapStateValue* pValue = NULL;

	pState = mapState_FromEnt(pEntity);
	pValue = mapState_FindPlayerValue(pState, pEntity, "Player_Points");

	if(pValue && iPoints)
	{
		MultiValSetInt(&pValue->mvValue, MultiValGetInt(&pValue->mvValue,NULL) + iPoints);

		estrClear(&estr);

		if(pchMessage)
		{
			entFormatGameMessageKey(pEntity, &estr, PVPGamePointsWithMSG, 
				STRFMT_INT("playerPoints", iPoints),
				STRFMT_MESSAGEKEY("message",pchMessage),
				STRFMT_END);
		} else {
			entFormatGameMessageKey(pEntity, &estr, PVPGamePoints, 
				STRFMT_INT("playerPoints", iPoints),
				STRFMT_END);
		}
		

		ClientCmd_NotifySend(pEntity,kNotifyType_PVPPoints,estr,NULL,NULL);
	}
}

static void gslPVPGame_AwardPointsFromDef(Entity *pEntity, const PVPPointDef *pPointDef)
{
	static char *estr;
	MapState *pState;
	MapStateValue* pValue = NULL;
	int iPoints = pPointDef->iPointValue;
	
	pState = mapState_FromEnt(pEntity);
	pValue = mapState_FindPlayerValue(pState, pEntity, "Player_Points");

	if(pValue && iPoints)
	{
		MultiValSetInt(&pValue->mvValue, MultiValGetInt(&pValue->mvValue,NULL) + iPoints);

		estrClear(&estr);

		if(IS_HANDLE_ACTIVE(pPointDef->displayMessage.hMessage))
		{
			entFormatGameMessageKey(pEntity, &estr, PVPGamePointsWithMSG, 
				STRFMT_INT("playerPoints", iPoints),
				STRFMT_MESSAGEREF("message",pPointDef->displayMessage.hMessage),
				STRFMT_END);
		} else {
			entFormatGameMessageKey(pEntity, &estr, PVPGamePoints, 
				STRFMT_INT("playerPoints", iPoints),
				STRFMT_END);
		}

		if (!pPointDef->pchNotificationTag)
		{
			ClientCmd_NotifySend(pEntity,kNotifyType_PVPPoints,estr,NULL,NULL);
		}
		else
		{
			ClientCmd_NotifySendWithTag(pEntity,kNotifyType_PVPPoints,estr,NULL,NULL,pPointDef->pchNotificationTag);
		}
	}
}

void gslPVPGame_PlayerNearDeathEnter(Entity *pKilled, Entity *pKiller)
{
	int iPartitionIdx = entGetPartitionIdx(pKilled);
	PVPCurrentGameDetails * pGameDetails = gslPvPGameDetailsFromIdx(iPartitionIdx);

	if (!pGameDetails)
		return; 
		
	if(pGameDetails && pGameDetails->pNearDeathFunc)
	{
		pGameDetails->pNearDeathFunc(pGameDetails, pKilled, pKiller);
	}
}

void gslPVPGame_PlayerKilled(Entity *pKilled, Entity *pKiller)
{
	int iPartitionIdx = entGetPartitionIdx(pKilled);
	PVPCurrentGameDetails * pGameDetails = gslPvPGameDetailsFromIdx(iPartitionIdx);

	if (!pGameDetails)
		return; 

	// gclPvP_NotifyKilled
	if (pGameDetails->pQueueMatch)
	{
		QueueMatch * pQueueMatch = pGameDetails->pQueueMatch;
		
		FOR_EACH_IN_EARRAY(pQueueMatch->eaGroups, QueueGroup, pGroup)
		{
			FOR_EACH_IN_EARRAY(pGroup->eaMembers, QueueMatchMember, pMember)
			{
				Entity *pPlayer = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pMember->uEntID);

				if (pPlayer)
				{
					ClientCmd_gclPvP_NotifyKilled(pPlayer, pKiller ? entGetRef(pKiller) : 0, entGetRef(pKilled));
				}
			}
			FOR_EACH_END
		}
		FOR_EACH_END
	}

	if(pGameDetails->pRules->bDisableNaturalRespawn && pKilled->pPlayer)
		pKilled->pPlayer->bDisableRespawn = true;

	if(pGameDetails->pKilledFunc)
	{
		pGameDetails->pKilledFunc(pGameDetails,pKilled,pKiller);
	}
}

void gslPVPGame_KillCredit(KillCreditTeam ***eaTeams, Entity *pKilled)
{
	int i,m;
	PVPPointDef *pKillDef = NULL;
	PVPPointDef *pAssistDef = NULL;

	if(entGetType(pKilled) == GLOBALTYPE_ENTITYCRITTER)
	{
		CritterDef *pCritterDef = GET_REF(pKilled->pCritter->critterDef);

		for(i=0;i<eaSize(&s_PointsStruct.CritterKillCredit);i++)
		{
			int t;
			for(t=ea32Size(&s_PointsStruct.CritterKillCredit[i]->critterTags)-1;t>=0;t--)
			{
				if(ea32Find(&pCritterDef->piTags,s_PointsStruct.CritterKillCredit[i]->critterTags[t]) == -1)
					break;
			}

			if(t==-1)
			{
				pKillDef = &s_PointsStruct.CritterKillCredit[i]->killCredit;
				pAssistDef = &s_PointsStruct.CritterKillCredit[i]->assistCredit;

				if(ea32Size(&s_PointsStruct.CritterKillCredit[i]->critterTags) > 0)
					break;
			}
		}
	}

	if(!pKillDef)
	{
		pKillDef = &s_PointsStruct.iKillCredit;
		pAssistDef = &s_PointsStruct.iAssistCredit;
	}

	for(i=0;i<eaSize(eaTeams);i++)
	{
		for(m=0;m<eaSize(&(*eaTeams)[i]->eaMembers);m++)
		{
			KillCreditEntity *pKillCredit = (*eaTeams)[i]->eaMembers[m];
			Entity *pPlayer = entFromEntityRef(entGetPartitionIdx(pKilled), pKillCredit->entRef);

			if(pPlayer)
			{
				if(!gameevent_AreAssistsEnabled() || pKillCredit->bFinalBlow)
					gslPVPGame_AwardPointsFromDef(pPlayer,pKillDef);
				else
					gslPVPGame_AwardPointsFromDef(pPlayer,pAssistDef);
			}
		}
	}
}

void gslPVPGame_setCountdown(PVPCurrentGameDetails *pGameDetails, F32 fTime)
{
	pGameDetails->fTimer = MAX(fTime, 0.0f);
	pGameDetails->bTimerCountDown = true;

	mapState_SetScoreboardTimer(pGameDetails->iPartitionIdx,pmTimestamp(fTime),true);
}

void gslPVPGame_resetTimer(PVPCurrentGameDetails *pGameDetails)
{
	pGameDetails->fTimer = 0.f;
	pGameDetails->bTimerCountDown = false;

	mapState_SetScoreboardTimer(pGameDetails->iPartitionIdx,pmTimestamp(0),false);
	mapState_SetScoreboardOvertime(pGameDetails->iPartitionIdx,false);
}

void pvpGame_InitGroups(PVPCurrentGameDetails *pGameDetails)
{
	int i;
	MatchMapState * pMatchMapState = pGameDetails->pMapState;

	if(pGameDetails && pGameDetails->pRules->publicRules.eGameType)
	{
		int iNumGroups = eaSize(&pGameDetails->pQueueMatch->eaGroups);
		for(i=0;i<iNumGroups;i++)
		{
			PVPGroupGameParams *pNewParams = NULL;
			if(i < eaSize(&pMatchMapState->ppGroupGameParams))
			{
				PVPGroupGameParams *pParams = pMatchMapState->ppGroupGameParams[i];

				if(pParams->eType == pGameDetails->pRules->publicRules.eGameType)
					continue;

				eaRemove(&pMatchMapState->ppGroupGameParams,i);
			}

			pNewParams = StructCreateVoid(pvpGame_GetGroupParseTable(pGameDetails->pRules->publicRules.eGameType));
			pNewParams->eType = pGameDetails->pRules->publicRules.eGameType;

			eaInsert(&pMatchMapState->ppGroupGameParams,pNewParams,i);
		}
	}
}

void gslPVPGame_start(QueuePartitionInfo *pInfo,QueueDef *pDef)
{
	int i;
	char * estr = NULL;
	PVPCurrentGameDetails * pGameDetails;
	QueueMatch * pQueueMatch;

	mapState_SetScoreboardState(pInfo->iPartitionIdx,kScoreboardState_Active);
	gslQueue_MapSetStateFromScoreboardState(pInfo->iPartitionIdx,kScoreboardState_Active);
	
	pGameDetails = gslPvPGameDetailsFromIdx(pInfo->iPartitionIdx);

	if(!pInfo->pMatch)
	{
		gslQueue_SetupDefaultMatch(pInfo);
		pGameDetails->pQueueMatch = pInfo->pMatch;
	}

	pvpGame_InitGroups(pGameDetails);
	pQueueMatch = pGameDetails->pQueueMatch;

	for(i=0;i<eaSize(&pQueueMatch->eaGroups);i++)
	{
		int iMember;
		for(iMember=0;iMember<eaSize(&pQueueMatch->eaGroups[i]->eaMembers);iMember++)
		{
			Entity *pPlayer = entFromContainerID(pInfo->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER,pQueueMatch->eaGroups[i]->eaMembers[iMember]->uEntID);

			if(pPlayer)
				gslPlayerRespawn(pPlayer, true, true);
		}
	}

	estrDestroy(&estr);

	if(pGameDetails && pGameDetails->pGameStartFunc)
	{
		mapState_SetScoreboard(pInfo->iPartitionIdx, pGameDetails->pRules->pchScoreboard);

		pGameDetails->pGameStartFunc(pGameDetails,&pDef->MapRules.QGameRules);
	}
}

void gslPVPGame_init(QueuePartitionInfo *pInfo,PVPCurrentGameDetails *pGameDetails)
{
	QueueDef *pDef = queue_DefFromName(SAFE_MEMBER(pInfo->pGameInfo, pchQueueDef));
	int iJoinTimeLimit = queue_GetJoinTimeLimit(pDef);	// -1 if no limit

	mapState_SetScoreboardState(pInfo->iPartitionIdx,kScoreboardState_Init);

	mapState_SetScoreboard(pInfo->iPartitionIdx, pGameDetails->pRules->pchScoreboard);

	if(iJoinTimeLimit >= 0 && !iGameAutoStart)
	{
		gslPVPGame_setCountdown(pGameDetails,iJoinTimeLimit);
	}
	else
	{
		gslPVPGame_start(pInfo,pDef);
	}
}

int gslPVPGame_CanArmamentSwap(int iPartitionIdx)
{
	MapState *pMapState = mapState_FromPartitionIdx(iPartitionIdx);
	if (pMapState)
	{
		if (mapState_GetScoreboardState(pMapState) == kScoreboardState_Active)
		{
			PVPCurrentGameDetails * pGameDetails = gslPvPGameDetailsFromIdx(iPartitionIdx);
			if (pGameDetails)
			{
				return pGameDetails->fTimer <= 1.f;
			}
		}
	}

	return false;
}

// Make a list of queue rewards for this victory level
static void gslPVPGame_GetQueueRewards(QueueDef *pQueueDef, EQueueRewardTableCondition eType, QueueRewardTable ***eaRewards)
{
	S32 i;

	for(i = 0; i < eaSize(&pQueueDef->eaRewardTables); ++i)
	{
		if(pQueueDef->eaRewardTables[i]->eRewardCondition == eType)
		{
			eaPush(eaRewards, pQueueDef->eaRewardTables[i]);
		}
	}
}

static void gslPVPGame_ProcessQueueRewardTableForEnts(	QueuePartitionInfo *pInfo, MapState *pMapState, 
														QueueDef *pQueueDef, Entity **ppEnts, QueueRewardTable *pRewardInfo)
{
	RewardTable *pRewardTable = NULL;

	if (eaSize(&ppEnts) == 0)
		return;

	pRewardTable = GET_REF(pRewardInfo->hRewardTable);
	
	if(pRewardTable)
	{
		ItemChangeReason reason = {0};

		FOR_EACH_IN_EARRAY_FORWARDS(ppEnts, Entity, pEnt)
		{
			int iLevel = entity_GetSavedExpLevel(pEnt);
			F32 fScalefactor = 1.f;

			if(pRewardInfo->bScaleReward)
			{
				MultiVal *mFactor = mapState_GetPlayerValue(pMapState, pEnt, "player_points");
				if(mFactor)
					fScalefactor = MultiValGetFloat(mFactor,NULL);
			}

			inv_FillItemChangeReason(&reason, pEnt, "PvP:Reward", pQueueDef->pchName);

			reward_PowerExec(pEnt, pRewardTable, iLevel, fScalefactor, true, &reason);
		}
		FOR_EACH_END
	}

	if(pRewardInfo->pchEvent)
	{
		eventsend_RecordPoke(pInfo->iPartitionIdx, NULL, ppEnts, pRewardInfo->pchEvent);
	}
}


void gslPVPGame_endWithWinner(int iPartitionIdx, int iWinningGroup, int iHighScore, bool bAllowTies)
{
	int i,m;
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);
	MapState *pMapState = mapState_FromPartitionIdx(pInfo->iPartitionIdx);
	QueueDef *pQueueDef = queue_DefFromName(SAFE_MEMBER(pInfo->pGameInfo, pchQueueDef));
	PVPCurrentGameDetails *pGameDetails = gslPvPGameDetailsFromIdx(iPartitionIdx);
	MatchMapState * pMatchMapState = pGameDetails->pMapState;
	QueueMatch * pQueueMatch = pGameDetails->pQueueMatch;
	const char *pchWinningFaction = NULL;
	ItemChangeReason reason = {0};

	mapState_SetScoreboardTotalMatchTime(pGameDetails->iPartitionIdx, pmTimestamp(0) - pMatchMapState->uCounterTime);
	mapState_SetScoreboardState(pGameDetails->iPartitionIdx,kScoreboardState_Final);
	mapState_SetScoreboardOvertime(pGameDetails->iPartitionIdx,false);

	if(eaSize(&pGameDetails->pRules->ppchRankingLeaderboards))
	{
		LeaderboardTeam *pWinners = StructCreate(parse_LeaderboardTeam);
		LeaderboardTeam *pLosers = StructCreate(parse_LeaderboardTeam);
		Entity **ppEnts = NULL;

		for(i=0;i<eaSize(&pQueueMatch->eaGroups);i++)
		{
			for(m=0;m<eaSize(&pQueueMatch->eaGroups[i]->eaMembers);m++)
			{
				Entity *pEnt = entFromContainerID(pGameDetails->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER,pQueueMatch->eaGroups[i]->eaMembers[m]->uEntID);

				if(i==iWinningGroup)
				{
					ea32Push(&pWinners->piTeam,pQueueMatch->eaGroups[i]->eaMembers[m]->uEntID);
				}
				else
				{
					ea32Push(&pLosers->piTeam,pQueueMatch->eaGroups[i]->eaMembers[m]->uEntID);
				}

				if(pEnt)
					eaPush(&ppEnts,pEnt);
			}
		}

		if(ea32Size(&pWinners->piTeam) > 0 && ea32Size(&pLosers->piTeam) > 0)
		{
			for(i=0;i<eaSize(&pGameDetails->pRules->ppchRankingLeaderboards);i++)
			{
				RemoteCommand_leaderboard_UpdatePlayerRatings(GLOBALTYPE_LEADERBOARDSERVER,0,pGameDetails->pRules->ppchRankingLeaderboards[i],pWinners,pLosers,false,NULL,NULL,NULL);
			}
		}

		for(i=0;i<eaSize(&ppEnts);i++)
		{
			entity_updateLeaderboardData(ppEnts[i]);
		}

		eaDestroy(&ppEnts);
	}

	for(i=0;i<eaSize(&pQueueMatch->eaGroups);i++)
	{
		QueueRewardTable **eaRewards = NULL;
		Entity **ppEnts = NULL;
		int j;

		if(i == iWinningGroup)
		{
			pchWinningFaction = REF_STRING_FROM_HANDLE(pQueueDef->eaGroupDefs[i]->hFaction);

			gslPVPGame_GetQueueRewards(pQueueDef, kQueueRewardTableCondition_Win, &eaRewards);
		}
		else if(bAllowTies && pMatchMapState->ppGroupGameParams[i]->iScore == iHighScore)
		{
			gslPVPGame_GetQueueRewards(pQueueDef, kQueueRewardTableCondition_Draw, &eaRewards);
			if(eaSize(&eaRewards) < 1)
			{
				gslPVPGame_GetQueueRewards(pQueueDef, kQueueRewardTableCondition_Win, &eaRewards);
			}
		}
		else
		{
			gslPVPGame_GetQueueRewards(pQueueDef, kQueueRewardTableCondition_Loss, &eaRewards);
		}
		
		for (m=0;m<eaSize(&pQueueMatch->eaGroups[i]->eaMembers);m++)
		{
			Entity *pPlayer = entFromContainerID(pInfo->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER,pQueueMatch->eaGroups[i]->eaMembers[m]->uEntID);

			if(pPlayer)
			{
				eaPush(&ppEnts,pPlayer);
			}
		}

		for(j = 0; j < eaSize(&eaRewards); j++)
		{
			gslPVPGame_ProcessQueueRewardTableForEnts(pInfo, pMapState, pQueueDef, ppEnts, eaRewards[j]);
		}
		
		eaDestroy(&ppEnts);
	}


	// grant the reward tables that require a per entity expression check via pExprRewardCondition
	FOR_EACH_IN_EARRAY(pQueueDef->eaRewardTables, QueueRewardTable, pRewardTable)
	{
		if (pRewardTable->eRewardCondition == kQueueRewardTableCondition_UseExpression && 
			pRewardTable->pExprRewardCondition)
		{
			Entity **ppEnts = NULL;

			// go through all the entities and see if this applies to them, then grant all the found entities the reward table

			FOR_EACH_IN_EARRAY_FORWARDS(pQueueMatch->eaGroups, QueueGroup, pGroup)
			{
				FOR_EACH_IN_EARRAY_FORWARDS(pGroup->eaMembers, QueueMatchMember, pMember)
				{
					Entity *pEnt = entFromContainerID(pInfo->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pMember->uEntID);
					if (pEnt)
					{
						ExprContext *pQueueContext = queue_GetContext(pEnt);
						if (pQueueContext)
						{
							MultiVal mVal;
							exprEvaluate(pRewardTable->pExprRewardCondition, pQueueContext, &mVal);
							if (MultiValToBool(&mVal))
							{
								eaPush(&ppEnts, pEnt);
							}
						}
					}
				}
				FOR_EACH_END
			}
			FOR_EACH_END
						
			gslPVPGame_ProcessQueueRewardTableForEnts(pInfo, pMapState, pQueueDef, ppEnts, pRewardTable);

			eaDestroy(&ppEnts);
		}
	}
	FOR_EACH_END

	gslQueue_MapSetStateFromScoreboardStateEx(pInfo->iPartitionIdx,kScoreboardState_Final,pchWinningFaction);

	gslPVPGame_setCountdown(pGameDetails,PVPMaxGameFinishedTime);

	playermatchstats_SendMatchStatsToPlayers(pInfo->iPartitionIdx);
}

void gslPVPGame_end(PVPCurrentGameDetails * pGameDetails)
{
	MapState *pMapState = mapState_FromPartitionIdx(pGameDetails->iPartitionIdx);
	int iWinningGroup = -1;
	int iHighScore = 0;

	//hand out rewards
	if(pGameDetails->pGroupWinningFunc)
		iWinningGroup = pGameDetails->pGroupWinningFunc(pGameDetails,&iHighScore);

	gslPVPGame_endWithWinner(pGameDetails->iPartitionIdx, iWinningGroup, iHighScore, true);
}

void gslPVPGame_countdownFinished(PVPCurrentGameDetails * pGameDetails)
{
	ScoreboardState eCurrentState = mapState_GetScoreboardState(mapState_FromPartitionIdx(pGameDetails->iPartitionIdx));
	gslPVPGame_resetTimer(pGameDetails);

	switch(eCurrentState)
	{
		case kScoreboardState_Init:
			{
			// meh....
			QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(pGameDetails->iPartitionIdx);
			QueueDef *pDef = queue_DefFromName(SAFE_MEMBER(pInfo->pGameInfo, pchQueueDef));
			gslPVPGame_start(pInfo,pDef);
			// Set game active
			}
			break;
		case kScoreboardState_Active:
			if(pGameDetails->pGameTimeComplete)
				pGameDetails->pGameTimeComplete(pGameDetails);
			else
				gslPVPGame_end(pGameDetails);

			break;
		case kScoreboardState_Intermission:
			if(pGameDetails->pIntermissionComplete)
				pGameDetails->pIntermissionComplete(pGameDetails);
		case kScoreboardState_Final:
			//Kick everyone

			break;
	}
}

int gslPVPGame_GetWinningTeam(PVPCurrentGameDetails * pGameDetails, int *iHighScoreOut)
{
	int iCurrentWinner = 1;
	int iHighScore = 0;
	int i;

	MatchMapState * pMatchMapState = pGameDetails->pMapState;

	for(i=0;i<eaSize(&pMatchMapState->ppGroupGameParams);i++)
	{
		if(pMatchMapState->ppGroupGameParams[i]->iScore > iHighScore)
		{
			iCurrentWinner = i;
			iHighScore = pMatchMapState->ppGroupGameParams[i]->iScore;
		}
		else if(pMatchMapState->ppGroupGameParams[i]->iScore == iHighScore)
			iCurrentWinner = -1;
	}

	if(iHighScoreOut)
		(*iHighScoreOut) = iHighScore;

	return iCurrentWinner;
}

static bool gslPVPGame_IsDeadOrNearDeath(Entity *pPlayer)
{
	return (!entIsAlive(pPlayer) || (pPlayer->pChar && pPlayer->pChar->pNearDeath));
}

/////////////////////////////////////////////////////////////////////////////////
// Domination (DOM)
/////////////////////////////////////////////////////////////////////////////////

GameEncounter *gslPVPGame_DOM_GetPointEncByName(PVPCurrentGameDetails * pGameDetails, int iPointNumber, int iGroupNumber)
{
	GameEncounter *pPointEnc = NULL;
	char *pchPointName = NULL;

	estrCreate(&pchPointName);

	estrPrintf(&pchPointName,"%s%d_%d",pGameDetails->pRules->pchCapturePointName,iPointNumber,iGroupNumber+1);

	pPointEnc = encounter_GetByName(pchPointName,NULL);

	if(!pPointEnc)
	{
		estrPrintf(&pchPointName,"%s%d",pGameDetails->pRules->pchCapturePointName,iPointNumber);

		pPointEnc = encounter_GetByName(pchPointName,NULL);
	}

	estrDestroy(&pchPointName);

	return pPointEnc;
}

GameInteractable *gslPVPGame_DOM_GetNamedPointByname(PVPCurrentGameDetails * pGameDetails, int iPointNumber)
{
	GameInteractable *pPoint = NULL;
	char *pchPointName = NULL;

	estrCreate(&pchPointName);

	estrPrintf(&pchPointName,"%s%d",pGameDetails->pRules->pchCapturePointName,iPointNumber);

	pPoint = interactable_GetByName(pchPointName,NULL);

	estrDestroy(&pchPointName);

	return pPoint;
}

void gslPVPGame_DOM_PointFXCapture(PVPCurrentGameDetails * pGameDetails, DOMControlPoint *pPoint, GameEncounter *pPointEnc, bool bRemoveOnly)
{
	Entity **ppEnts = NULL;
	int i;

	encounter_GetEntities(pGameDetails->iPartitionIdx,pPointEnc,&ppEnts,false,false);

	for(i=0;i<eaSize(&ppEnts);i++)
	{
		DynParamBlock *pParamBlock;
		EntityRef er = entGetRef(ppEnts[i]);
		U32 uiTime = pmTimestamp(0);
		int x;

		for(x=0;x<eaSize(&pGameDetails->pRules->ppchCapturePointFX);x++)
		{
			pmFxStop(ppEnts[i]->pChar->pPowersMovement,kPVPAnimID_DOM_Cap_FX,0,kPowerAnimFXType_PVP,
				er,
				er,
				uiTime,
				pGameDetails->pRules->ppchCapturePointFX[x]);
		}

		if(!bRemoveOnly)
		{
			pParamBlock = dynParamBlockCreate();

			pmFxReplaceOrStart(ppEnts[i]->pChar->pPowersMovement,
				kPVPAnimID_DOM_Cap_FX,0,kPowerAnimFXType_PVP,
				er,
				er,
				uiTime,
				pGameDetails->pRules->ppchCapturePointFX,
				pParamBlock,
				0.0f,
				false,
				NULL,
				NULL,
				false);
		}
	}
}

void gslPVPGame_DOM_PointFXCheck(PVPCurrentGameDetails * pGameDetails, DOMControlPoint *pPoint, GameEncounter *pPointEnc, F32 fPreLife, bool bWasContested)
{
	int iFXSize = eaSize(&pGameDetails->pRules->ppchPointStatusFX) - 1;
	int iPreFX = fPreLife * iFXSize;
	int iFX = pPoint->fLife * iFXSize; 

	if(!pPointEnc)
		pPointEnc = gslPVPGame_DOM_GetPointEncByName(pGameDetails,pPoint->iPointNumber,pPoint->iOwningGroup);

	if(iFXSize > -1)
	{
		if(pPoint->eStatus == kDOMPointStatus_Unowned)
		{
			iPreFX = (iPreFX - iFXSize) * -1;
			iFX = (iFX - iFXSize) * -1;
		}

		if(iPreFX != iFX || iFX == 0)
		{
			Entity **ppEnts = NULL;
			int i;

			if(pPointEnc)
				encounter_GetEntities(pGameDetails->iPartitionIdx,pPointEnc,&ppEnts,false,false);

			for(i=0;i<eaSize(&ppEnts);i++)
			{
				DynParamBlock *pParamBlock;
				EntityRef er = entGetRef(ppEnts[i]);
				U32 uiTime = pmTimestamp(0);
				int x;
				char **ppchFX = NULL;

				for(x=0;x<eaSize(&pGameDetails->pRules->ppchPointStatusFX);x++)
				{
					pmFxStop(ppEnts[i]->pChar->pPowersMovement,kPVPAnimID_DOM_Step_FX,0,kPowerAnimFXType_PVP,
						er,
						er,
						uiTime,
						pGameDetails->pRules->ppchPointStatusFX[x]);
				}

				pParamBlock = dynParamBlockCreate();
				iFX = pPoint->fLife * iFXSize;
			
				eaCreate(&ppchFX);
				eaPush(&ppchFX,pGameDetails->pRules->ppchPointStatusFX[iFX]);

				pmFxReplaceOrStart(ppEnts[i]->pChar->pPowersMovement,
					kPVPAnimID_DOM_Step_FX,0,kPowerAnimFXType_PVP,
					er,
					er,
					uiTime,
					ppchFX,
					pParamBlock,
					0.0f,
					false,
					NULL,
					NULL,
					false);

				eaDestroy(&ppchFX);
			}
		}
	}

	if(bWasContested && pPoint->eStatus != kDOMPointStatus_Contested)
	{
		Entity **ppEnts = NULL;
		int i;

		encounter_GetEntities(pGameDetails->iPartitionIdx,pPointEnc,&ppEnts,false,false);

		for(i=0;i<eaSize(&ppEnts);i++)
		{
			EntityRef er = entGetRef(ppEnts[i]);
			U32 uiTime = pmTimestamp(0);
			int x;

			if(pPoint->eStatus == kDOMPointStatus_Controled)
			{
				gslPVPGame_DOM_PointFXCapture(pGameDetails,pPoint,pPointEnc,false);
			}

			for(x=0;x<eaSize(&pGameDetails->pRules->ppchContestedPointFX);x++)
			{
				pmFxStop(ppEnts[i]->pChar->pPowersMovement,kPVPAnimID_DOM_Contest_FX,0,kPowerAnimFXType_PVP,
					er,
					er,
					uiTime,
					pGameDetails->pRules->ppchContestedPointFX[x]);
			}
		}
	}
	else if(!bWasContested && pPoint->eStatus == kDOMPointStatus_Contested)
	{
		Entity **ppEnts = NULL;
		int i;

		if(pPointEnc)
			encounter_GetEntities(pGameDetails->iPartitionIdx,pPointEnc,&ppEnts,false,false);

		for(i=0;i<eaSize(&ppEnts);i++)
		{
			DynParamBlock *pParamBlock;
			EntityRef er = entGetRef(ppEnts[i]);
			U32 uiTime = pmTimestamp(0);
			char **ppchFX = NULL;

			pParamBlock = dynParamBlockCreate();
			iFX = pPoint->fLife * iFXSize;

			eaCreate(&ppchFX);
			eaPush(&ppchFX,pGameDetails->pRules->ppchContestedPointFX[iFX]);

			pmFxReplaceOrStart(ppEnts[i]->pChar->pPowersMovement,
				kPVPAnimID_DOM_Contest_FX,0,kPowerAnimFXType_PVP,
				er,
				er,
				uiTime,
				ppchFX,
				pParamBlock,
				0.0f,
				false,
				NULL,
				NULL,
				false);

			eaDestroy(&ppchFX);
		}

		gslPVPGame_DOM_PointFXCapture(pGameDetails,pPoint,pPointEnc,true);
	}
}

void gslPVPGame_DOM_CapturePoint(PVPCurrentGameDetails * pGameDetails, MapState *pMapState, DOMControlPoint *pPoint, int iNewOwner)
{
	int i;
	Entity **ppEnts = NULL;
	char *pchPontName = NULL;
	GameEncounter *pPointEnc = NULL;
	GameEncounterPartitionState *pState = NULL;
	GameInteractable *pNamedPoint = NULL;
	MatchMapState * pMatchMapState = pGameDetails->pMapState;
	QueueMatch * pQueueMatch = pGameDetails->pQueueMatch;

	for(i=0;i<eaSize(&pMatchMapState->ppGroupGameParams);i++)
	{
		DOMGroupParams *pGroupParams = (DOMGroupParams *)pMatchMapState->ppGroupGameParams[i];
		if(i!=iNewOwner && iNewOwner != -1)
			eaFindAndRemove(&pGroupParams->ppOwnedPoints,pPoint);
		else
			eaPushUnique(&pGroupParams->ppOwnedPoints,pPoint);
	}

	pPointEnc = gslPVPGame_DOM_GetPointEncByName(pGameDetails,pPoint->iPointNumber,pPoint->iOwningGroup);
	pNamedPoint = gslPVPGame_DOM_GetNamedPointByname(pGameDetails,pPoint->iPointNumber);

	if(pPointEnc)
	{
		pState = encounter_GetPartitionState(pGameDetails->iPartitionIdx, pPointEnc);
		encounter_Reset(pPointEnc,pState);
	}
	

	pPoint->iOwningGroup = iNewOwner;
	pPoint->eStatus = iNewOwner >= 0 ? kDOMPointStatus_Controled : kDOMPointStatus_Unowned;
	pPoint->iAttackingGroup = -1;
	pPoint->fLife = 1.0f;
	pPoint->fTick = 0;

	pPointEnc = gslPVPGame_DOM_GetPointEncByName(pGameDetails,pPoint->iPointNumber,pPoint->iOwningGroup);

	if(pPointEnc)
	{
		pState = encounter_GetPartitionState(pGameDetails->iPartitionIdx, pPointEnc);

		if (pState->eState != EncounterState_Spawned) {
			encounter_SpawnEncounter(pPointEnc,pState);
		}
		if(iNewOwner != -1)
		{
			encounter_GetEntities(pGameDetails->iPartitionIdx,pPointEnc,&ppEnts,false,false);

			for(i=0;i<eaSize(&ppEnts);i++)
			{
				gslEntity_SetFactionOverrideByHandle(ppEnts[i], kFactionOverrideType_DEFAULT, REF_HANDLEPTR(pQueueMatch->eaGroups[iNewOwner]->pGroupDef->hFaction));
				

				gslPVPGame_DOM_PointFXCheck(pGameDetails,pPoint,pPointEnc,-1.f,false);
			}

			estrDestroy(&pchPontName);
		}
	}

	if(pNamedPoint)
	{
		interactable_SetVisibleChild(pGameDetails->iPartitionIdx,pNamedPoint,pPoint->iOwningGroup + 1,true);
	}
}

void gsl_PVPGame_DOM_GameStart(PVPCurrentGameDetails *pGameDetails, PVPGameRules *pGameRules)
{
	//Find all points and set to unowned
	int i = 0;
	DOMControlPoint *pCurrentPoint = NULL;
	MapState *pMapState = mapState_FromPartitionIdx(pGameDetails->iPartitionIdx);
	MatchMapState * pMatchMapState = pGameDetails->pMapState;

	for(i=0;i<eaSize(&pMatchMapState->ppGroupGameParams);i++)
	{
		DOMGroupParams *pParams = (DOMGroupParams*)pMatchMapState->ppGroupGameParams[i];
		eaClear(&pParams->ppOwnedPoints);
	}

	i = 0;

	do
	{
		GameEncounter *pEncounter = NULL;
		GameInteractable *pNamedPoint = NULL;

		pCurrentPoint = NULL;

		i++;

		pEncounter = gslPVPGame_DOM_GetPointEncByName(pGameDetails,i,-1);
		pNamedPoint = gslPVPGame_DOM_GetNamedPointByname(pGameDetails,i);

		if(pEncounter)
		{
			pCurrentPoint = StructCreate(parse_DOMControlPoint);
			encounter_GetPosition(pEncounter,pCurrentPoint->vLocation);
			pCurrentPoint->iPointNumber = i;
			eaPush(&pMatchMapState->ppGameSpecific,pCurrentPoint);
			gslPVPGame_DOM_CapturePoint(pGameDetails,pMapState,pCurrentPoint,-1);
		}

		if(!pEncounter && pNamedPoint)
		{
			pCurrentPoint = StructCreate(parse_DOMControlPoint);
			interactable_GetPosition(pNamedPoint,pCurrentPoint->vLocation);
			pCurrentPoint->iPointNumber = i;
			eaPush(&pMatchMapState->ppGameSpecific,pCurrentPoint);
			gslPVPGame_DOM_CapturePoint(pGameDetails,pMapState,pCurrentPoint,-1);
		}
	} while(pCurrentPoint != NULL);
}

void gslPVPGame_DOM_HidePoints(PVPCurrentGameDetails *pGameDetails)
{
	if(pGameDetails)
	{
		int i=0;
		GameEncounter *pEncounter = NULL;
		bool bFound = false;
		
		do
		{
			int iGroupNumber = -1;
			bFound = false;

			do 
			{
				pEncounter = gslPVPGame_DOM_GetPointEncByName(pGameDetails,i,iGroupNumber);
				if(pEncounter)
				{
					GameEncounterPartitionState *pState = encounter_GetPartitionState(pGameDetails->iPartitionIdx, pEncounter);
					encounter_Reset(pEncounter,pState);
					bFound = true;
				}
				iGroupNumber++;

			} while (pEncounter != NULL);

			i++;
		}while(bFound == true);
	}
}

void gslPVPGame_DOM_PlayerKilled(PVPCurrentGameDetails *pGameDetails, Entity *pKilled, Entity *pKiller)
{
	MapState *pMapState = mapState_FromPartitionIdx(pGameDetails->iPartitionIdx);
	MatchMapState * pMatchMapState = pGameDetails->pMapState;
	QueueMatch * pQueueMatch = pGameDetails->pQueueMatch;
	int i;

	if(pKiller && pKilled)
	{
		Vec3 vKilledLocation;
		Vec3 vKillerLocation;
		F32 fCreditDistSq = (pGameDetails->pRules->fCaptureDistance * 2) * (pGameDetails->pRules->fCaptureDistance * 2);

		entGetCombatPosDir(pKilled,NULL,vKilledLocation,NULL);
		entGetCombatPosDir(pKilled,NULL,vKillerLocation,NULL);

		for(i=0;i<eaSize(&pMatchMapState->ppGameSpecific);i++)
		{
			DOMControlPoint *pPoint = (DOMControlPoint*)pMatchMapState->ppGameSpecific[i];

			if(distance3Squared(vKilledLocation,pPoint->vLocation) <= fCreditDistSq
				|| distance3Squared(vKillerLocation,pPoint->vLocation) <= fCreditDistSq)
			{
				if(pPoint->iOwningGroup >= 0 && queue_Match_FindMemberInGroup(pQueueMatch->eaGroups[pPoint->iOwningGroup],entGetContainerID(pKiller))!=-1)
				{
					gslPVPGame_AwardPointsFromDef(pKiller,&s_PointsStruct.iDOM_DefendPoint);
					gslPVPGame_recordEvent(pGameDetails->iPartitionIdx,PvPEvent_DOM_DefendPoint,pKiller,pKilled);
				}
				else
				{
					gslPVPGame_AwardPointsFromDef(pKiller,&s_PointsStruct.iDOM_AttackPoint);
					gslPVPGame_recordEvent(pGameDetails->iPartitionIdx,PvPEvent_DOM_AttackPoint,pKiller,pKilled);
				}
			}
		}
	}
}

void gslPVPGame_DOM_ProcessPoints(PVPCurrentGameDetails * pGameDetails, MapState *pMapState, F32 fTime)
{
	int i;
	int iGroupSize = 0;
	int *piCloseEnts = NULL;
	MatchMapState * pMatchMapState = pGameDetails->pMapState;
	QueueMatch * pQueueMatch = pGameDetails->pQueueMatch;

	iGroupSize = eaSize(&pMatchMapState->ppGroupGameParams);

	for(i=0;i<eaSize(&pMatchMapState->ppGameSpecific);i++)
	{
		DOMControlPoint *pPoint = (DOMControlPoint*)pMatchMapState->ppGameSpecific[i];
		int g,m;
		bool bEntsFound = false;
		F32 fPreLife = pPoint->fLife;
		Entity **ppCloseEnts = NULL;
		bool bWasContested = pPoint->eStatus == kDOMPointStatus_Contested ? true : false;
		
		ea32Clear(&piCloseEnts);
		ea32SetSize(&piCloseEnts,iGroupSize);
		ea32Clear(&pPoint->iAttackingEnts);

		pPoint->iAttackingGroup = -1;

		for(g=0;g<eaSize(&pQueueMatch->eaGroups);g++)
		{
			for(m=0;m<eaSize(&pQueueMatch->eaGroups[g]->eaMembers);m++)
			{
				Entity *pEnt = entFromContainerID(pGameDetails->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER,pQueueMatch->eaGroups[g]->eaMembers[m]->uEntID);

				if(pEnt)
				{
					Vec3 vPlayerLocation;

					if(gslPVPGame_IsDeadOrNearDeath(pEnt))
						continue;

					entGetCombatPosDir(pEnt,NULL,vPlayerLocation,NULL);

					if(distance3Squared(vPlayerLocation,pPoint->vLocation) <= pGameDetails->pRules->fCaptureDistance * pGameDetails->pRules->fCaptureDistance)
					{
						piCloseEnts[g]++;
						bEntsFound = true;

						eaPush(&ppCloseEnts,pEnt);

						if(g!=pPoint->iOwningGroup)
							ea32Push(&pPoint->iAttackingEnts,pQueueMatch->eaGroups[g]->eaMembers[m]->uEntID);
					}
				}
			}
		}

		if(bEntsFound)
		{
			int iHighValue = 0;
			for(g=0;g<ea32Size(&piCloseEnts);g++)
			{
				if(g == pPoint->iOwningGroup)
					continue;

				if(iHighValue < piCloseEnts[g])
				{
					pPoint->iAttackingGroup = g;
					iHighValue = piCloseEnts[g];
				}
				else if(iHighValue == piCloseEnts[g] && iHighValue > 0)
				{
					pPoint->iAttackingGroup = pPoint->iOwningGroup;
				}
			}

			if(pPoint->iAttackingGroup >= 0 
				&& (pPoint->iOwningGroup == -1 
				|| piCloseEnts[pPoint->iOwningGroup] < iHighValue))
			{
				pPoint->eStatus = kDOMPointStatus_Contested;
			}
			else if(pPoint->iAttackingGroup >= 0 && pPoint->iOwningGroup > -1 && piCloseEnts[pPoint->iOwningGroup] >= iHighValue)
			{
				pPoint->eStatus = kDOMPointStatus_Contested;
				pPoint->iAttackingGroup = pPoint->iOwningGroup;
			}
			else if(pPoint->iOwningGroup >= 0)
			{
				pPoint->eStatus = kDOMPointStatus_Controled;
			}
			else
			{
				pPoint->eStatus = kDOMPointStatus_Unowned;
			}
		}
		else
		{
			pPoint->iAttackingGroup = -1;
			if(pPoint->iOwningGroup != -1)
				pPoint->eStatus = kDOMPointStatus_Controled;
			else
				pPoint->eStatus = kDOMPointStatus_Unowned;
		}

		if(pPoint->iAttackingGroup >= 0 && pPoint->iAttackingGroup != pPoint->iOwningGroup)
		{
			pPoint->fLife -=  fTime /pGameDetails->pRules->fCaptureTime;

			if(piCloseEnts[pPoint->iAttackingGroup] > 1)
			{
				pPoint->fLife -= (fTime * (piCloseEnts[pPoint->iAttackingGroup] - 1) * pGameDetails->pRules->fFriendlyBonus) / pGameDetails->pRules->fCaptureTime;
			}
		}
		else if(pPoint->eStatus != kDOMPointStatus_Contested && pPoint->fLife < 1.0f)
		{
			pPoint->fLife += fTime / pGameDetails->pRules->fRecycleTime;
			pPoint->fLife = min(pPoint->fLife,1.0f);
		}

		if(pPoint->fLife <= 0.f)
		{
			gslPVPGame_DOM_CapturePoint(pGameDetails,pMapState,pPoint,pPoint->iAttackingGroup);

			for(m=0;m<eaSize(&ppCloseEnts);m++)
			{
				if(queue_Match_FindMemberInGroup(pQueueMatch->eaGroups[pPoint->iOwningGroup],entGetContainerID(ppCloseEnts[m]))!=-1)
				{
					gslPVPGame_AwardPointsFromDef(ppCloseEnts[m],&s_PointsStruct.iDOM_CapturePoint);
					gslPVPGame_recordEvent(pGameDetails->iPartitionIdx,PvPEvent_DOM_CapturePoint,ppCloseEnts[m],NULL);
				}
			}
		}

		gslPVPGame_DOM_PointFXCheck(pGameDetails,pPoint,NULL,fPreLife,bWasContested);
	}

	if(piCloseEnts)
		ea32Destroy(&piCloseEnts);
}

void gslPVPGame_DOM_Tick(PVPCurrentGameDetails * pGameDetails, F32 fTime)
{
	MapState *pMapState = mapState_FromPartitionIdx(pGameDetails->iPartitionIdx);
	int i;
	bool bPointsScored = false;
	MatchMapState * pMatchMapState = pGameDetails->pMapState;

	if(mapState_GetScoreboardState(pMapState) != kScoreboardState_Active)
		return;

	gslPVPGame_DOM_ProcessPoints(pGameDetails,pMapState,fTime);

	for(i=0;i<eaSize(&pMatchMapState->ppGroupGameParams);i++)
	{
		DOMGroupParams *pParams = (DOMGroupParams*)pMatchMapState->ppGroupGameParams[i];
		int p;

		for(p=0;p<eaSize(&pParams->ppOwnedPoints);p++)
		{
			if(pParams->ppOwnedPoints[p]->eStatus == kDOMPointStatus_Controled)
			{
				pParams->ppOwnedPoints[p]->fTick += fTime;

				if(pParams->ppOwnedPoints[p]->fTick >= pGameDetails->pRules->fPointTime)
				{
					pParams->params.iScore += 1;
					bPointsScored = true;
					pParams->ppOwnedPoints[p]->fTick -= pGameDetails->pRules->fPointTime;
				} 
			}
		}
	}

	if(bPointsScored)
	{
		int iHighScore;
		int iWinningGroup = gslPVPGame_GetWinningTeam(pGameDetails,&iHighScore);

		if(iHighScore >= pGameDetails->pRules->publicRules.iPointMax)
			gslPVPGame_end(pGameDetails);
	}
}

/////////////////////////////////////////////////////////////////////////////////
// Capture the Flag (CTF)
/////////////////////////////////////////////////////////////////////////////////

int gslPVPGame_CTF_GetWinningTeam(PVPCurrentGameDetails * pGameDetails, int *iHighScoreOut)
{
	// return -1 if there is a tie
	int iCurrentWinner = -1;
	int iHighScore = 0;
	int i;
	MatchMapState * pMatchMapState = pGameDetails->pMapState;

	for(i=0;i<eaSize(&pMatchMapState->ppGroupGameParams);i++)
	{
		CTFGroupParams *pGroupParams = (CTFGroupParams*)pMatchMapState->ppGroupGameParams[i];

		if(pGroupParams->params.iScore > iHighScore)
		{
			iCurrentWinner = i;
			iHighScore = pGroupParams->params.iScore;
		}
		else if(pGroupParams->params.iScore == iHighScore)
		{
			iCurrentWinner = -1;
		}
	}

	if(iHighScoreOut)
		(*iHighScoreOut) = iHighScore;

	return iCurrentWinner;
}

static int AddFlagPowerStack_CB(Power *ppow, PvPEntityCallbackData *pData)
{
	int bSuccess = false;
	PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;

	if(ppow && pData && pdef)
	{
		Entity *pEnt = pData->ref ? entFromEntityRef(pData->iPartitionIdx, pData->ref) : NULL;
		if(pEnt && pEnt->pChar)
		{
			Character *pchar = pEnt->pChar;

			if(!eaSize(&pchar->ppPowersTemporary) || !character_FindPowerByID(pchar,ppow->uiID))
			{
				eaIndexedEnable(&pchar->ppPowersTemporary,parse_Power);
				eaPush(&pchar->ppPowersTemporary, ppow);			

				bSuccess = true;
				pchar->bResetPowersArray = true;
			}
		}
	}

	if(pData)
		free(pData);

	return(bSuccess);
}

static int AddFlagPower_CB(Power *ppow, PvPEntityCallbackData *pData)
{
	int bSuccess = false;
	PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;

	if(ppow && pData && pdef)
	{
		Entity *pEnt = pData->ref ? entFromEntityRef(pData->iPartitionIdx, pData->ref) : NULL;
		if(pEnt && pEnt->pChar)
		{
			Character *pchar = pEnt->pChar;

			if(!eaSize(&pchar->ppPowersTemporary) || 
				(!character_FindPowerByDef(pchar, pdef) && !character_FindPowerByDefTemporary(pchar,pdef)) )
			{
				eaIndexedEnable(&pchar->ppPowersTemporary,parse_Power);
				eaPush(&pchar->ppPowersTemporary, ppow);			

				bSuccess = true;
				pchar->bResetPowersArray = true;
			}
		}
	}

	if(pData)
		free(pData);

	return(bSuccess);
}

void gslPVPGame_UpdateFlagEncounter(PVPCurrentGameDetails * pGameDetails, int iGroupIdx, GameEncounter *pOptionalEncounter, bool showFlag)
{
	GameEncounter *pFlag = pOptionalEncounter;

	if(!pFlag)
	{
		char *estrFlagName = NULL;

		estrCreate(&estrFlagName);
		estrPrintf(&estrFlagName,"%s%d",pGameDetails->pRules->pchFlagName,iGroupIdx+1);

		pFlag = encounter_GetByName(estrFlagName,NULL);

		estrDestroy(&estrFlagName);
	}

	if(pFlag)
	{
		GameEncounterPartitionState *pState = encounter_GetPartitionState(pGameDetails->iPartitionIdx, pFlag);
		if(showFlag)
		{
			Entity **ppEnts = NULL;
			int i;
			
			if (pState->eState != EncounterState_Spawned) {
				encounter_SpawnEncounter(pFlag, pState);
			}
			encounter_GetEntities(pGameDetails->iPartitionIdx,pFlag,&ppEnts,false,false);
			for(i=0;i<eaSize(&ppEnts);i++)
			{
				entSetCodeFlagBits(ppEnts[i],ENTITYFLAG_DONOTFADE);
			}
		}
		else
		{
			encounter_Reset(pFlag, pState);
		}
	}
}

void gslPVPGame_updateFlag(PVPCurrentGameDetails * pGameDetails, int iGroupIdx, GameInteractable *pOptionalFlag, bool showFlag)
{
	GameInteractable *pFlag = pOptionalFlag;

	if(!pFlag)
	{
		char *estrFlagName = NULL;

		estrCreate(&estrFlagName);
		estrPrintf(&estrFlagName,"%s%d",pGameDetails->pRules->pchFlagName,iGroupIdx+1);

		pFlag = interactable_GetByName(estrFlagName,NULL);
		
		estrDestroy(&estrFlagName);
	}

	if(pFlag)
	{
		int iVisibleChild = -1;
		interactable_GetVisibleChild(pGameDetails->iPartitionIdx,NULL,pFlag->pcName,&iVisibleChild,NULL);

		if(iVisibleChild != -1)
		{
			interactable_SetVisibleChild(pGameDetails->iPartitionIdx,pFlag,showFlag ? 0 : 1,true);
		} else {
			interactable_SetHideState(pGameDetails->iPartitionIdx,pFlag,showFlag ? false : true, 0, true);
		}
	} 
	else
	{
		gslPVPGame_UpdateFlagEncounter(pGameDetails,iGroupIdx,NULL,showFlag);
	}
}

void gslPVPGame_updateFlagCarrier(PVPCurrentGameDetails * pGameDetails, Entity *pEntity, bool bHasFlag)
{
	int i;

	if(IS_HANDLE_ACTIVE(pGameDetails->pRules->hFlagCarrierClass))
	{
		if(bHasFlag)
		{
			COPY_HANDLE(pEntity->pChar->hClassTemporary,pGameDetails->pRules->hFlagCarrierClass);
		}
		else
		{
			REMOVE_HANDLE(pEntity->pChar->hClassTemporary);
		}

		character_SetClassCallback(pEntity, NULL);
	}

	for(i=0;i<eaSize(&pGameDetails->pRules->ppchFlagPowers);i++)
	{
		PowerDef *pPowerDef = powerdef_Find(pGameDetails->pRules->ppchFlagPowers[i]);

		if(!pPowerDef)
			continue;

		if(bHasFlag)
		{
			PvPEntityCallbackData *pData = calloc(1,sizeof(PvPEntityCallbackData));
			pData->iPartitionIdx = entGetPartitionIdx(pEntity);
			pData->ref = entGetRef(pEntity);
			character_AddPowerTemporary(pEntity->pChar,pPowerDef,AddFlagPower_CB,pData);
		}
		else
		{
			Power *ppow = character_FindPowerByDefTemporary(pEntity->pChar, pPowerDef);
			if(ppow)
				character_RemovePowerTemporary(pEntity->pChar, ppow->uiID);
		}
	}

	for(i=0;i<eaSize(&pGameDetails->pRules->eaFlagStackPowers);i++)
	{
		PowerDef *pPowerDef = powerdef_Find(pGameDetails->pRules->eaFlagStackPowers[i]->pchPower);

		if(!pPowerDef)
			continue;

		if(bHasFlag)
		{
			F32 iPowers = (pGameDetails->fTimer - pGameDetails->pRules->eaFlagStackPowers[i]->fStartTime) / pGameDetails->pRules->eaFlagStackPowers[i]->fIntervalTime;
			F32 j;

			for(j=0;j<iPowers;j+=1)
			{
				PvPEntityCallbackData *pData = calloc(1,sizeof(PvPEntityCallbackData));
				pData->iPartitionIdx = entGetPartitionIdx(pEntity);
				pData->ref = entGetRef(pEntity);
				character_AddPowerTemporary(pEntity->pChar,pPowerDef,AddFlagPowerStack_CB,pData);
			}
		}else{
			Power *ppow = character_FindPowerByDefTemporary(pEntity->pChar, pPowerDef);
			while(ppow)
			{
				character_RemovePowerTemporary(pEntity->pChar, ppow->uiID);
				ppow = character_FindPowerByDefTemporary(pEntity->pChar, pPowerDef);
			}
		}
	}

	if(bHasFlag && eaSize(&pGameDetails->pRules->ppchRechargePowerCategories))
	{
		for(i=0;i<eaSize(&pGameDetails->pRules->ppchRechargePowerCategories);i++)
		{
			int iCategory = StaticDefineIntGetInt(PowerCategoriesEnum,pGameDetails->pRules->ppchRechargePowerCategories[i]);
			PowerCategory *pCat = iCategory != -1 ? g_PowerCategories.ppCategories[iCategory] : NULL;

			if(pCat && pCat->fTimeCooldown)
				character_CategorySetCooldown(pGameDetails->iPartitionIdx,pEntity->pChar,iCategory,pCat->fTimeCooldown);
		}
	}

	character_ResetPowersArray(entGetPartitionIdx(pEntity), pEntity->pChar, NULL);
	entity_SetDirtyBit(pEntity,parse_Character,pEntity->pChar,false);
}

void gslPVPGame_CTF_ResetFlag(PVPCurrentGameDetails * pGameDetails, int iGroupIdx)
{
	MatchMapState * pMatchMapState = pGameDetails->pMapState;
	CTFGroupParams *pGroupParams = (CTFGroupParams*)pMatchMapState->ppGroupGameParams[iGroupIdx];
	char *estrFlagName = NULL;
	GameNamedPoint *pFlagPoint;
	GameInteractable *pFlagInteractable;
	GameEncounter *pFlagEncounter;

	estrCreate(&estrFlagName);
	estrPrintf(&estrFlagName,"%s%d",pGameDetails->pRules->pchFlagName,iGroupIdx+1);

	if(pGroupParams->eFlagStatus != kCTFFlagStatus_InPlace)
	{
		Entity *pHoldingEntity = entFromEntityRef(pGameDetails->iPartitionIdx, pGroupParams->eFlagHolder);

		if(pHoldingEntity)
		{
			//Holding entity is a critter, destroy it
			if(pGroupParams->eFlagStatus == kCTFFlagStatus_Dropped)
			{
				gslQueueEntityDestroy(pHoldingEntity);
			} else {
				gslPVPGame_updateFlagCarrier(pGameDetails,pHoldingEntity,false);
			}
		}
		
	}

	pGroupParams->eFlagStatus = kCTFFlagStatus_InPlace;
	pFlagPoint = namedpoint_GetByName(estrFlagName,NULL);

	if(pFlagPoint)
	{
		copyVec3(pFlagPoint->pWorldPoint->point_pos,pGroupParams->vecFlagLocation);
		copyVec3(pFlagPoint->pWorldPoint->point_pos,pGroupParams->vecCapLocation);
	}
	else
	{
		pFlagInteractable = interactable_GetByName(estrFlagName,NULL);

		if (pFlagInteractable)
		{
			interactable_GetPosition(pFlagInteractable,pGroupParams->vecFlagLocation);
			interactable_GetPosition(pFlagInteractable,pGroupParams->vecCapLocation);
			gslPVPGame_updateFlag(pGameDetails,iGroupIdx,pFlagInteractable,true);
		}
		else
		{
			pFlagEncounter = encounter_GetByName(estrFlagName,NULL);

			if(pFlagEncounter)
			{
				encounter_GetPosition(pFlagEncounter,pGroupParams->vecFlagLocation);
				encounter_GetPosition(pFlagEncounter,pGroupParams->vecCapLocation);
				gslPVPGame_UpdateFlagEncounter(pGameDetails,iGroupIdx,pFlagEncounter,true);
			}
		}
	}

	pGroupParams->fTimeDropped = 0.f;
	pGroupParams->eFlagHolder = 0;

	estrDestroy(&estrFlagName);
}

void gslPVPGame_CTF_GameStart(PVPCurrentGameDetails * pGameDetails, PVPGameRules *pGameRules)
{
	int i;
	MatchMapState * pMatchMapState = pGameDetails->pMapState;

	if(pGameRules->fMaxGameTime)
		gslPVPGame_setCountdown(pGameDetails,pGameRules->fMaxGameTime);

	for(i=0;i<eaSize(&pMatchMapState->ppGroupGameParams);i++)
	{
		gslPVPGame_CTF_ResetFlag(pGameDetails,i);
	}
}

bool gslPVPGame_CTF_CanPickupFlag(PVPCurrentGameDetails * pGameDetails, int iGroupIdx, Entity *pPlayer)
{
	int iGroup;
	MapState *pMapState = mapState_FromPartitionIdx(pGameDetails->iPartitionIdx);
	MatchMapState * pMatchMapState = pGameDetails->pMapState;
	QueueMatch * pQueueMatch = pGameDetails->pQueueMatch;

	if(gslPVPGame_IsDeadOrNearDeath(pPlayer))
		return false;

	// Players can always pick up their own flags if alive
	if(queue_Match_FindMemberInGroup(pQueueMatch->eaGroups[iGroupIdx],entGetContainerID(pPlayer))!=-1)
		return true;

	//Make sure that the player isn't carrying any other teams flags
	for(iGroup=0;iGroup<eaSize(&pMatchMapState->ppGroupGameParams);iGroup++)
	{
		CTFGroupParams *pGroupParams = (CTFGroupParams*)pMatchMapState->ppGroupGameParams[iGroup];

		if(pGroupParams->eFlagStatus == kCTFFlagStatus_PickedUp && pGroupParams->eFlagHolder == entGetRef(pPlayer))
			return false;
	}

	return true;
}

void gslPVPGame_CTF_PickupFlag(PVPCurrentGameDetails * pGameDetails, int iGroupIdx, Entity *pPlayer)
{
	MapState *pMapState = mapState_FromPartitionIdx(pGameDetails->iPartitionIdx);
	MatchMapState * pMatchMapState = pGameDetails->pMapState;
	QueueMatch * pQueueMatch = pGameDetails->pQueueMatch;
	CTFGroupParams *pGroupParams = (CTFGroupParams*)pMatchMapState->ppGroupGameParams[iGroupIdx];

	// Trying to pick up your own flag, so reset it instead
	if(queue_Match_FindMemberInGroup(pQueueMatch->eaGroups[iGroupIdx],entGetContainerID(pPlayer))>-1)
	{
		if(pGroupParams->eFlagStatus == kCTFFlagStatus_Dropped)
		{
			Entity *pEntFlag = entFromEntityRef(pGameDetails->iPartitionIdx, pGroupParams->eFlagHolder);
			if(pEntFlag->pChar->erRingoutCredit)
			{
				Entity *pThrower = entFromEntityRef(pGameDetails->iPartitionIdx, pEntFlag->pChar->erRingoutCredit);
				gslPVPGame_AwardPointsFromDef(pPlayer,&s_PointsStruct.iCTF_Interception);
				gslPVPGame_recordEvent(pGameDetails->iPartitionIdx,PvPEvent_CTF_FlagPass,pThrower,pPlayer);
			}
			else
			{
				gslPVPGame_AwardPointsFromDef(pPlayer,&s_PointsStruct.iCTF_ReturnCredit);
			}
		}
		
		gslPVPGame_recordEvent(pGameDetails->iPartitionIdx,PvPEvent_CTF_FlagReturned,pPlayer,NULL);
		gslPVPGame_CTF_ResetFlag(pGameDetails,iGroupIdx);
		return;
	}

	if(pGroupParams->eFlagStatus == kCTFFlagStatus_Dropped)
	{
		Entity *pEntFlag = entFromEntityRef(pGameDetails->iPartitionIdx, pGroupParams->eFlagHolder);

		if(pEntFlag->pChar->erRingoutCredit) // Flag was thrown
		{
			Entity *pThrower = entFromEntityRef(pGameDetails->iPartitionIdx, pEntFlag->pChar->erRingoutCredit);

			if(pThrower && pPlayer != pThrower) // Check for self pass
			{
				F32 fDistance = entGetDistance(pPlayer,NULL,pThrower,NULL,NULL);

				if(fDistance < 12.f)
				{
					gslPVPGame_AwardPointsFromDef(pPlayer,&s_PointsStruct.iCTF_ShortCatchCredit);
					gslPVPGame_AwardPointsFromDef(pThrower,&s_PointsStruct.iCTF_ShortPassCredit);
				}
				else if(fDistance < 80.f)
				{
					gslPVPGame_AwardPointsFromDef(pPlayer,&s_PointsStruct.iCTF_MediumCatchCredit);
					gslPVPGame_AwardPointsFromDef(pThrower,&s_PointsStruct.iCTF_MediumPassCredit);
				}
				else
				{
					gslPVPGame_AwardPointsFromDef(pPlayer,&s_PointsStruct.iCTF_MediumCatchCredit);
					gslPVPGame_AwardPointsFromDef(pThrower,&s_PointsStruct.iCTF_MediumPassCredit);
				}

				gslPVPGame_recordEvent(pGameDetails->iPartitionIdx,PvPEvent_CTF_FlagPass,pThrower,pPlayer);
			}
		}
		gslQueueEntityDestroy(pEntFlag);
	}
	else if(pGroupParams->eFlagStatus == kCTFFlagStatus_InPlace)
	{
		gslPVPGame_AwardPointsFromDef(pPlayer,&s_PointsStruct.iCTF_PickupCredit);
	}


	pGroupParams->eFlagStatus = kCTFFlagStatus_PickedUp;
	pGroupParams->eFlagHolder = entGetRef(pPlayer);
	pGroupParams->fTimeDropped = 0.f;

	gslPVPGame_recordEvent(pGameDetails->iPartitionIdx,PvPEvent_CTF_FlagPickedUp,pPlayer,NULL);

	gslPVPGame_updateFlagCarrier(pGameDetails,pPlayer,true);
	gslPVPGame_updateFlag(pGameDetails,iGroupIdx,NULL,false);
}

void gslPVPGame_CTF_CaptureFlag(PVPCurrentGameDetails * pGameDetails, int iGroupIdxPoint, int iGroupIdxFlag)
{
	MapState *pMapState = mapState_FromPartitionIdx(pGameDetails->iPartitionIdx);
	MatchMapState * pMatchMapState = pGameDetails->pMapState;
	CTFGroupParams *pGroupPointParams = (CTFGroupParams*)pMatchMapState->ppGroupGameParams[iGroupIdxPoint];
	CTFGroupParams *pGroupFlagParams = (CTFGroupParams*)pMatchMapState->ppGroupGameParams[iGroupIdxFlag];
	Entity *pPlayer = entFromEntityRef(pGameDetails->iPartitionIdx, pGroupFlagParams->eFlagHolder);

	pGroupPointParams->params.iScore++;
	gslPVPGame_CTF_ResetFlag(pGameDetails,iGroupIdxFlag);

	if(pPlayer)
	{
		gslPVPGame_AwardPointsFromDef(pPlayer,&s_PointsStruct.iCTF_CaptureCredit);
		gslPVPGame_recordEvent(pGameDetails->iPartitionIdx,PvPEvent_CTF_FlagCaptured,pPlayer,NULL);
	}

	if(pGameDetails->bInOvertime)
	{
		int iWinningTeam = gslPVPGame_CTF_GetWinningTeam(pGameDetails,NULL);

		if (iWinningTeam==iGroupIdxPoint)
			gslPVPGame_end(pGameDetails);
	}
}

Entity *gslPVPGame_CTF_CreateFlagEntity(PVPCurrentGameDetails * pGameDetails, Entity *pPlayer)
{
	CritterDef *pCritterDef = NULL;

	pCritterDef = critter_DefGetByName(pGameDetails->pRules->pchFlagCritter);
	if (pCritterDef)
	{
		Entity *pEntFlag = NULL;
		CritterCreateParams sCritterParams = {0};

		sCritterParams.pOverrideDef = NULL;
		sCritterParams.enttype = GLOBALTYPE_ENTITYCRITTER;
		sCritterParams.iPartitionIdx = pGameDetails->iPartitionIdx;
		sCritterParams.fsmOverride = NULL;
		sCritterParams.iLevel = 0;
		sCritterParams.iTeamSize = 0;
		sCritterParams.pcSubRank = 0;
		sCritterParams.pFaction = entGetFaction(pPlayer);
		sCritterParams.pDisplayNameMsg = 0;
		sCritterParams.pDisplaySubNameMsg = 0;
		sCritterParams.pchSpawnAnim = 0;
		sCritterParams.fSpawnTime = 0;
		sCritterParams.aiTeam = 0;
		sCritterParams.pCreatorNode = 0;

		pEntFlag = critter_CreateByDef(pCritterDef, &sCritterParams, NULL, true);

		if (pEntFlag)
		{
			entSetCodeFlagBits(pEntFlag, ENTITYFLAG_DONOTFADE);
			return pEntFlag;
		}
	}

	return NULL;
}

void gslPVPGame_ThrowFlag(PVPCurrentGameDetails * pGameDetails, Entity *pPlayer, Vec3 vecTarget)
{
	MatchMapState * pMatchMapState = pGameDetails->pMapState;

	MapState *pMapState = mapState_FromPartitionIdx(pGameDetails->iPartitionIdx);
	CTFGroupParams *pGroupParams = NULL;
	Entity *pEntFlag = NULL;
	int iGroupIdx = -1;
	Vec3 vPlayerPos;

	for(iGroupIdx=eaSize(&pMatchMapState->ppGroupGameParams)-1;iGroupIdx>=0;iGroupIdx--)
	{
		pGroupParams = (CTFGroupParams*)pMatchMapState->ppGroupGameParams[iGroupIdx];

		if(pGroupParams->eFlagHolder == entGetRef(pPlayer))
		{
			if(pGroupParams->eFlagStatus == kCTFFlagStatus_PickedUp)
				break;
		}
	}

	if(iGroupIdx == -1 || pGameDetails->pRules->fMaxDropTime == 0)
		return;

	entGetCombatPosDir(pPlayer,NULL,vPlayerPos,NULL);

	if(distance3Squared(vecTarget,vPlayerPos) > 22500) //greater than 50 feet
	{
		subVec3(vecTarget,vPlayerPos,vecTarget);
		normalVec3(vecTarget);
		scaleAddVec3(vecTarget,150.f,vPlayerPos,vecTarget);
	}

	pGroupParams = eaGetVoid(&pMatchMapState->ppGroupGameParams,iGroupIdx);

	pEntFlag = gslPVPGame_CTF_CreateFlagEntity(pGameDetails,pPlayer);

	if(pEntFlag)
	{
		pGroupParams->eFlagStatus = kCTFFlagStatus_Dropped;
		pGroupParams->eFlagHolder = entGetRef(pEntFlag);

		entGetCombatPosDir(pPlayer,NULL,pGroupParams->vecFlagLocation,NULL);

		entSetPos(pEntFlag,pGroupParams->vecFlagLocation,true,"PVP Flag Creation");

		pmKnockToStart(pEntFlag,vecTarget,pmTimestamp(0), false, true, 1.f, true);

		pEntFlag->pChar->erRingoutCredit = entGetRef(pPlayer);

		gslPVPGame_updateFlagCarrier(pGameDetails,pPlayer,false);
		gslPVPGame_updateFlag(pGameDetails,iGroupIdx,NULL,false);
	}
	else
	{
		gslPVPGame_CTF_ResetFlag(pGameDetails,iGroupIdx);
	}
}

void gslPVPGame_CTF_DropFlag(PVPCurrentGameDetails * pGameDetails, int iGroupIdx, Entity *pPlayer)
{
	MapState *pMapState = mapState_FromPartitionIdx(pGameDetails->iPartitionIdx);
	MatchMapState * pMatchMapState = pGameDetails->pMapState;
	CTFGroupParams *pGroupParams = eaGetVoid(&pMatchMapState->ppGroupGameParams,iGroupIdx);
	Entity *pEntFlag = NULL;

	if(pGameDetails->pRules->fMaxDropTime == 0)
		gslPVPGame_CTF_ResetFlag(pGameDetails,iGroupIdx);

	
	pEntFlag = gslPVPGame_CTF_CreateFlagEntity(pGameDetails,pPlayer);

	if(pEntFlag)
	{
		pGroupParams->eFlagStatus = kCTFFlagStatus_Dropped;
		pGroupParams->eFlagHolder = entGetRef(pEntFlag);

		entGetCombatPosDir(pPlayer,NULL,pGroupParams->vecFlagLocation,NULL);

		entSetPos(pEntFlag,pGroupParams->vecFlagLocation,true,"PVP Flag Creation");
		LaunchLoot(pEntFlag);

		gslPVPGame_updateFlagCarrier(pGameDetails,pPlayer,false);
		gslPVPGame_updateFlag(pGameDetails,iGroupIdx,NULL,false);
	}
	else
	{
		gslPVPGame_CTF_ResetFlag(pGameDetails,iGroupIdx);
	}
}

void gslPVPGame_DropFlag(PVPCurrentGameDetails * pGameDetails, Entity *pPlayer)
{
	MapState *pMapState = mapState_FromPartitionIdx(pGameDetails->iPartitionIdx);
	CTFGroupParams *pGroupParams = NULL;
	MatchMapState * pMatchMapState = pGameDetails->pMapState;
	int iGroupIdx = -1;

	for(iGroupIdx=eaSize(&pMatchMapState->ppGroupGameParams)-1;iGroupIdx>=0;iGroupIdx--)
	{
		pGroupParams = (CTFGroupParams*)pMatchMapState->ppGroupGameParams[iGroupIdx];

		if(pGroupParams->eFlagHolder == entGetRef(pPlayer))
		{
			if(pGroupParams->eFlagStatus == kCTFFlagStatus_PickedUp)
				break;
		}
	}

	if(iGroupIdx == -1 || pGameDetails->pRules->fMaxDropTime == 0)
		return;

	gslPVPGame_CTF_DropFlag(pGameDetails,iGroupIdx,pPlayer);
}

// checks if the 'killed' entity is holding the flag and drop the flag if so
static void gslPVPGame_CTF_CheckPlayerDropFlag(PVPCurrentGameDetails * pGameDetails, Entity *pKilled, Entity *pKiller)
{
	// Find if this player is caring any flag
	int iGroup;
	MapState *pMapState = mapState_FromPartitionIdx(pGameDetails->iPartitionIdx);
	MatchMapState * pMatchMapState = pGameDetails->pMapState;

	for(iGroup=0; iGroup < eaSize(&pMatchMapState->ppGroupGameParams); iGroup++)
	{
		CTFGroupParams *pGroupParams = (CTFGroupParams*)pMatchMapState->ppGroupGameParams[iGroup];

		if(pGroupParams->eFlagHolder == entGetRef(pKilled))
		{
			if(pGroupParams->eFlagStatus == kCTFFlagStatus_PickedUp)
				gslPVPGame_CTF_DropFlag(pGameDetails,iGroup, pKilled);
			else if(pGroupParams->eFlagStatus == kCTFFlagStatus_Dropped) 
				gslPVPGame_CTF_ResetFlag(pGameDetails, iGroup); //If the flag is dropped, then the current ent ref is the flag, and it dieing means to reset the flag

			gslPVPGame_recordEvent(pGameDetails->iPartitionIdx, PvPEvent_CTF_FlagDrop, pKiller, pKilled);
		}
	}
}

// 
void gslPVPGame_CTF_PlayerNearDeathEnter(PVPCurrentGameDetails *pGameDetails, Entity *pKilled, Entity *pKiller)
{
	gslPVPGame_CTF_CheckPlayerDropFlag(pGameDetails, pKilled, pKiller);
}

void gslPVPGame_CTF_PlayerKilled(PVPCurrentGameDetails *pGameDetails, Entity *pKilled, Entity *pKiller)
{
	gslPVPGame_CTF_CheckPlayerDropFlag(pGameDetails, pKilled, pKiller);
}

void gslPVPGame_CTF_ProcessFlags(PVPCurrentGameDetails *pGameDetails, F32 fTime)
{
	int iGroup,iCur;
	MapState *pMapState = mapState_FromPartitionIdx(pGameDetails->iPartitionIdx);
	MatchMapState * pMatchMapState = pGameDetails->pMapState;
	QueueMatch * pQueueMatch = pGameDetails->pQueueMatch;

	for(iCur=0;iCur<eaSize(&pMatchMapState->ppGroupGameParams);iCur++)
	{
		CTFGroupParams *pGroupParams = (CTFGroupParams*)pMatchMapState->ppGroupGameParams[iCur];
		Entity *pClosestValidPlayer = NULL;
		F32 fClosestDistance = PVPFlagCaptureDistance * PVPFlagCaptureDistance;

		switch (pGroupParams->eFlagStatus)
		{
			case kCTFFlagStatus_InPlace:
			case kCTFFlagStatus_Dropped:
				// Find if any other players are within the location of the flag
				if(pGroupParams->eFlagStatus == kCTFFlagStatus_Dropped)
				{
					//Update the location of the flag with the location of the dropped entity
					Entity *pFlagEntity = entFromEntityRef(pGameDetails->iPartitionIdx, pGroupParams->eFlagHolder);
					if (pFlagEntity)
					{
						entGetCombatPosDir(pFlagEntity,NULL,pGroupParams->vecFlagLocation,NULL);
					}

					// Do not let anyone pick up the flag for half a second. This allows it to spring from the
					// player being killed before being picked up by an entity in melee distance
					if(pGroupParams->fTimeDropped < 0.5f)
					{
						pGroupParams->fTimeDropped += fTime;
						continue;
					}
				}

				for(iGroup=0;iGroup<eaSize(&pQueueMatch->eaGroups);iGroup++)
				{
					int iMember;

					if(iGroup==iCur && pGroupParams->eFlagStatus == kCTFFlagStatus_InPlace)
						continue;

					for(iMember=0;iMember<eaSize(&pQueueMatch->eaGroups[iGroup]->eaMembers);iMember++)
					{
						Entity *pPlayer = entFromContainerID(pGameDetails->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER,pQueueMatch->eaGroups[iGroup]->eaMembers[iMember]->uEntID);

						if(pPlayer)
						{
							Vec3 vEntPos;
							F32 fDist = 0.f;

							entGetCombatPosDir(pPlayer,NULL,vEntPos,NULL);
							fDist = distance3Squared(vEntPos,pGroupParams->vecFlagLocation);

							if( fDist < fClosestDistance
								&& gslPVPGame_CTF_CanPickupFlag(pGameDetails,iCur,pPlayer))
							{
								pClosestValidPlayer = pPlayer;
								fClosestDistance = fDist;
							}
						}
					}
				}

				if(pClosestValidPlayer)
				{
					gslPVPGame_CTF_PickupFlag(pGameDetails,iCur,pClosestValidPlayer);
				}
				else if(pGroupParams->eFlagStatus == kCTFFlagStatus_Dropped)
				{
					pGroupParams->fTimeDropped += fTime;
					if(pGroupParams->fTimeDropped >= pGameDetails->pRules->fMaxDropTime)
					{
						gslPVPGame_CTF_ResetFlag(pGameDetails,iCur);
					}
				}

			xcase kCTFFlagStatus_PickedUp:
				{
					// Find if the carrying player has returned it to their base
					Entity *pFlagCarrier = entFromEntityRef(pGameDetails->iPartitionIdx, pGroupParams->eFlagHolder);
					CTFGroupParams *pCarrierGroupParams = NULL;

					if(!pFlagCarrier) //Flag carrier is gone? Reset the flag
					{
						gslPVPGame_CTF_ResetFlag(pGameDetails,iCur);
					}
					else
					{
						for(iGroup=eaSize(&pQueueMatch->eaGroups)-1;iGroup>=0;iGroup--)
						{
							if(queue_Match_FindMemberInGroup(pQueueMatch->eaGroups[iGroup],entGetContainerID(pFlagCarrier)) != -1)
								break;
						}

						if(iGroup == -1 || iGroup == iCur)
						{
							gslPVPGame_CTF_ResetFlag(pGameDetails,iCur);
							continue;
						}

						pCarrierGroupParams = (CTFGroupParams*)pMatchMapState->ppGroupGameParams[iGroup];
						if(!pGameDetails->pRules->bRequireOwnFlagToScore || pCarrierGroupParams->eFlagStatus == kCTFFlagStatus_InPlace)
						{
							Vec3 vEntPos;

							entGetCombatPosDir(pFlagCarrier,NULL,vEntPos,NULL);

							if(distance3Squared(pCarrierGroupParams->vecCapLocation,vEntPos) < PVPFlagCaptureDistance * PVPFlagCaptureDistance)
							{
								gslPVPGame_CTF_CaptureFlag(pGameDetails,iGroup,iCur);
							}
						}
					}
				}
		}
	}
}

void gslPVPGame_CTF_GameTimeFinished(PVPCurrentGameDetails *pGameDetails)
{
	//Find a winner
	int iHighScore = 0.f;
	int iWinner = gslPVPGame_CTF_GetWinningTeam(pGameDetails,&iHighScore);

	if(iWinner > -1)
	{
		gslPVPGame_end(pGameDetails);
	} 
	else if(pGameDetails->bInOvertime || pGameDetails->pRules->fMaxOvertime == 0.f)
	{
		gslPVPGame_end(pGameDetails);
	}
	else
	{
		pGameDetails->bInOvertime = true;
		gslPVPGame_setCountdown(pGameDetails,pGameDetails->pRules->fMaxOvertime);
		mapState_SetScoreboardOvertime(pGameDetails->iPartitionIdx,true);
	}
}

void gslPVPGame_CTF_Tick(PVPCurrentGameDetails *pGameDetails, F32 fTime)
{
	int iHighScore = 0;

	if(mapState_GetScoreboardState(mapState_FromPartitionIdx(pGameDetails->iPartitionIdx)) != kScoreboardState_Active)
		return;

	gslPVPGame_CTF_ProcessFlags(pGameDetails,fTime);

	//Look for a team that has won
	if(gslPVPGame_CTF_GetWinningTeam(pGameDetails,&iHighScore) != -1 && iHighScore >= pGameDetails->pRules->publicRules.iPointMax)
	{
		gslPVPGame_end(pGameDetails);
	}
}

//////////////////////////////////////////////////////////////////////////
// Deathmatch (DM)

void gslPVPGame_DM_GameStart(PVPCurrentGameDetails *pGameDetails, PVPGameRules *pGameRules)
{

}

void gslPVPGame_DM_PlayerKilled(PVPCurrentGameDetails *pGameDetails, Entity *pKilled, Entity *pKiller)
{
	Entity *pMember = NULL;
	int iPointValue = 0;
	int iHighScore = 0;
	QueueMatch * pQueueMatch = pGameDetails->pQueueMatch;
	MatchMapState * pMatchMapState = pGameDetails->pMapState;
	int i;
	
	if(pKiller)
	{
		iPointValue = 1;
		pMember = pKiller;
	}else if(pGameDetails->pRules->bSuicidePenality){
		iPointValue = -1;
		pMember = pKilled;
	}

	if(!pMember || !pMember->pPlayer)
		return;

	for(i=0;i<eaSize(&pQueueMatch->eaGroups);i++)
	{
		int m;

		for(m=0;m<eaSize(&pQueueMatch->eaGroups[i]->eaMembers);m++)
		{
			if(entGetContainerID(pMember) == pQueueMatch->eaGroups[i]->eaMembers[m]->uEntID)
			{
				PVPGroupGameParams *pGroupParams = pMatchMapState->ppGroupGameParams[i];

				pGroupParams->iScore += iPointValue;
			}
		}
	}

	//Look for a team that has won
	if(iPointValue > 0 && gslPVPGame_GetWinningTeam(pGameDetails,&iHighScore) != -1 && iHighScore >= pGameDetails->pRules->publicRules.iPointMax)
	{
		gslPVPGame_end(pGameDetails);
	}
}

//////////////////////////////////////////////////////////////////////////
// Last man standing (LMS)

void gslPVPGame_LMS_RoundStart(PVPCurrentGameDetails *pGameDetails)
{
		QueueMatch * pQueueMatch = pGameDetails->pQueueMatch;
	MatchMapState * pMatchMapState = pGameDetails->pMapState;
	int i;

	//Respawn everyone

	for(i=0;i<eaSize(&pQueueMatch->eaGroups);i++)
	{
		int m;
		LMSGroupParams *pGroupParams = (LMSGroupParams*)pMatchMapState->ppGroupGameParams[i];

		for(m=0;m<eaSize(&pQueueMatch->eaGroups[i]->eaMembers);m++)
		{
			Entity *pPlayer = entFromContainerID(pGameDetails->iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pQueueMatch->eaGroups[i]->eaMembers[m]->uEntID);

			if(pPlayer)
			{
				gslPlayerRespawn(pPlayer,true,true);
				pGroupParams->iTeammatesRemaining++;
			}
		}
	}
}

void gslPVPGame_LMS_GameStart(PVPCurrentGameDetails *pGameDetails, PVPGameRules *pGameRules)
{
	//Begin first round
	gslPVPGame_LMS_RoundStart(pGameDetails);
}

void gslPVPGame_LMS_PlayerKilled(PVPCurrentGameDetails *pGameDetails, Entity *pKilled, Entity *pKiller)
{
	QueueMatch * pQueueMatch = pGameDetails->pQueueMatch;
	int i;

	for(i=0;i<eaSize(&pQueueMatch->eaGroups);i++)
	{
		int m;

		for(m=0;m<eaSize(&pQueueMatch->eaGroups[i]->eaMembers);m++)
		{
			if(pQueueMatch->eaGroups[i]->eaMembers[m]->uEntID != entGetContainerID(pKilled))
				continue;

			if(pKilled->pPlayer)
				pKilled->pPlayer->bDisableRespawn = true;
		}
		
	}
}

void gslPVPGame_LMS_RoundComplete(PVPCurrentGameDetails *pGameDetails)
{
	MatchMapState * pMatchMapState = pGameDetails->pMapState;
	int i;
	int iHighScore = 0;

	for(i=0;i<eaSize(&pMatchMapState->ppGroupGameParams);i++)
	{
		LMSGroupParams *pGroupScore = (LMSGroupParams*)pMatchMapState->ppGroupGameParams[i];

		if(pGroupScore->iTeammatesRemaining > 0)
			pGroupScore->params.iScore++;
	}

	//Look for a team that has won
	if(gslPVPGame_GetWinningTeam(pGameDetails,&iHighScore) != -1 && iHighScore >= pGameDetails->pRules->publicRules.iPointMax)
	{
		gslPVPGame_end(pGameDetails);
		return;
	}

	if(pGameDetails->pRules->fIntervalTime)
	{
		mapState_SetScoreboardState(pGameDetails->iPartitionIdx,kScoreboardState_Intermission);
		gslPVPGame_setCountdown(pGameDetails,pGameDetails->pRules->fIntervalTime);
	}else{
		gslPVPGame_LMS_RoundStart(pGameDetails);
	}
}

void gslPVPGame_LMS_Tick(PVPCurrentGameDetails *pGameDetails, F32 fTime)
{
	//Find all players still alive
	int i;
	QueueMatch * pQueueMatch = pGameDetails->pQueueMatch;
	MapState *pMapState = mapState_FromPartitionIdx(pGameDetails->iPartitionIdx);
	MatchMapState * pMatchMapState = pGameDetails->pMapState;
	bool bEndRound = false;

	if(mapState_GetScoreboardState(pMapState) == kScoreboardState_Intermission)
	{
		bool bAllEntsAlive = true;

		for(i=0;i<eaSize(&pQueueMatch->eaGroups);i++)
		{
			int m;

			for(m=0;m<eaSize(&pQueueMatch->eaGroups[i]->eaMembers);m++)
			{
				Entity *pEnt = entFromContainerID(pGameDetails->iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pQueueMatch->eaGroups[i]->eaMembers[m]->uEntID);

				if(pEnt && !entIsAlive(pEnt))
				{
					bAllEntsAlive = false;
					break;
				}
			}

			if(bAllEntsAlive == false)
				break;
		}

		if(bAllEntsAlive)
			mapState_SetScoreboardState(pGameDetails->iPartitionIdx,kScoreboardState_Active);
	}

	if(mapState_GetScoreboardState(pMapState) != kScoreboardState_Active)
		return;

	for(i=0;i<eaSize(&pQueueMatch->eaGroups);i++)
	{
		int m;

		LMSGroupParams *pGroupScore = (LMSGroupParams*)pMatchMapState->ppGroupGameParams[i];

		pGroupScore->iTeammatesRemaining = 0;

		for(m=0;m<eaSize(&pQueueMatch->eaGroups[i]->eaMembers);m++)
		{
			Entity *pEnt = entFromContainerID(pGameDetails->iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pQueueMatch->eaGroups[i]->eaMembers[m]->uEntID);

			if(pEnt)
			{
				ANALYSIS_ASSUME(pEnt != NULL);
				if (entIsAlive(pEnt))
				{
					pGroupScore->iTeammatesRemaining++;
				}
			}
		}

		if(pGroupScore->iTeammatesRemaining == 0)
			bEndRound = true;
	}

	if(bEndRound)
		gslPVPGame_LMS_RoundComplete(pGameDetails);
}

// This is the first function that causes a PVPCurrentGameDetails to be created
void gslPVPGame_CreateGameData(QueuePartitionInfo *pInfo, PVPGameType eGameType)
{
	QueueDef *pDef = queue_DefFromName(SAFE_MEMBER(pInfo->pGameInfo, pchQueueDef));
	PVPCurrentGameDetails * pGameDetails = gslPvPGameDetailsFromIdx(pInfo->iPartitionIdx);

	MapState *pMapState = mapState_FromPartitionIdx(pInfo->iPartitionIdx);

	if (pDef == NULL)
		return;

	if(pGameDetails)
		StructDestroy(parse_PVPCurrentGameDetails, pGameDetails);

	pGameDetails = StructCreate(parse_PVPCurrentGameDetails);
	pGameDetails->pMapState = &pMapState->matchState;
	// grab a pointer to the game rules
	pGameDetails->pRules = &pDef->MapRules.QGameRules;
	// copy out the part of the rules that lives in the mapstate.  This will need to be a
	// struct copy if the struct becomes more interesting
	pGameDetails->pMapState->pvpRules = pGameDetails->pRules->publicRules;
	pGameDetails->pQueueMatch = pInfo->pMatch;
	pGameDetails->iPartitionIdx = pInfo->iPartitionIdx;

	eaSet(&g_eaGameDetailsPartition, pGameDetails, pInfo->iPartitionIdx);

	//pGameDetails->eCurrentGameType = eGameType;

	if(eGameType == kPVPGameType_None)
		return; 

	gslPVPGame_DOM_HidePoints(pGameDetails);

	switch	(eGameType)
	{
	case kPVPGameType_CaptureTheFlag:
		pGameDetails->pTickFunc = gslPVPGame_CTF_Tick;
		pGameDetails->pNearDeathFunc = gslPVPGame_CTF_PlayerNearDeathEnter;
		pGameDetails->pKilledFunc = gslPVPGame_CTF_PlayerKilled;
		pGameDetails->pGameStartFunc = gslPVPGame_CTF_GameStart;
		pGameDetails->pGameTimeComplete = gslPVPGame_CTF_GameTimeFinished;
		pGameDetails->pGroupWinningFunc = gslPVPGame_GetWinningTeam;
		break;
	case kPVPGameType_Domination:
		pGameDetails->pTickFunc = gslPVPGame_DOM_Tick;
		pGameDetails->pKilledFunc = gslPVPGame_DOM_PlayerKilled;
		pGameDetails->pGameStartFunc = gsl_PVPGame_DOM_GameStart;
		pGameDetails->pGroupWinningFunc = gslPVPGame_GetWinningTeam;
		break;
	case kPVPGameType_Deathmatch:
		pGameDetails->pTickFunc = NULL;
		pGameDetails->pKilledFunc = gslPVPGame_DM_PlayerKilled;
		pGameDetails->pGameStartFunc = gslPVPGame_DM_GameStart;
		pGameDetails->pGroupWinningFunc = gslPVPGame_GetWinningTeam;
		break;
	case kPVPGameType_LastManStanding:
		pGameDetails->pTickFunc = gslPVPGame_LMS_Tick;
		pGameDetails->pKilledFunc = gslPVPGame_LMS_PlayerKilled;
		pGameDetails->pGameStartFunc = gslPVPGame_LMS_GameStart;
		pGameDetails->pGameTimeComplete = gslPVPGame_LMS_RoundComplete;
		pGameDetails->pGroupWinningFunc = gslPVPGame_GetWinningTeam;
		pGameDetails->pIntermissionComplete = gslPVPGame_LMS_RoundStart;
	}

	gslPVPGame_init(pInfo,pGameDetails);
}

//**********************
//The general tick function for the PVP system. Called every frame on the server
void gslPVPGame_update(PVPCurrentGameDetails *pGameDetails, F32 fTime)
{
	bool bTickTimer = true;
	QueueMatch * pQueueMatch = pGameDetails->pQueueMatch;

	if(pGameDetails->pTickFunc)
		pGameDetails->pTickFunc(pGameDetails,fTime);

	if (pQueueMatch)
	{
		// update the map state relating to the QueueMatch
		int i;
		for(i=0;i<eaSize(&pGameDetails->pMapState->ppGroupGameParams);i++)
		{
			pGameDetails->pMapState->ppGroupGameParams[i]->iNumMembers = eaSize(&pQueueMatch->eaGroups[i]->eaMembers);
		}
	}

	if(pGameDetails->bTimerCountDown)
	{
		ScoreboardState eCurrentState = mapState_GetScoreboardState(mapState_FromPartitionIdx(pGameDetails->iPartitionIdx));

		if(pGameDetails->fTimer <= 0)
		{
			gslPVPGame_countdownFinished(pGameDetails);
		}
		else if(eCurrentState == kScoreboardState_Init && pGameDetails->bTimerCountDown && pGameDetails->fTimer > g_QueueConfig.uMinGameInitTime)
		{
			int iGroup,iMember;
			bool bMissingMember = false;
			
			if(!pQueueMatch)
				return;

			bTickTimer = false;

			//Check to see if all players have logged in

			for(iGroup=0;iGroup<eaSize(&pQueueMatch->eaGroups);iGroup++)
			{
				for(iMember=0;iMember<eaSize(&pQueueMatch->eaGroups[iGroup]->eaMembers);iMember++)
				{
					Entity *pPlayerEntity = entFromContainerID(pGameDetails->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER,pQueueMatch->eaGroups[iGroup]->eaMembers[iMember]->uEntID);

					if(!pPlayerEntity || entCheckFlag(pPlayerEntity,ENTITYFLAG_PLAYER_DISCONNECTED | ENTITYFLAG_PLAYER_LOGGING_IN | ENTITYFLAG_IGNORE))
					{
						// This entity is missing or hasn't loaded yet
						bMissingMember = true;
					}
					else
					{
						bTickTimer = true;
					}

					if(bMissingMember && bTickTimer)
						break;
				}

				if(bMissingMember && bTickTimer)
					break;
			}

			if(bMissingMember==false)
			{
				gslPVPGame_setCountdown(pGameDetails,g_QueueConfig.uMinGameInitTime);
			}
		}
	}

	if(bTickTimer)
		pGameDetails->fTimer += pGameDetails->bTimerCountDown ? fTime * -1 : fTime;
}

void gslPVPGame_TickGames(F32 fTime)
{
	ZoneMapType eMapType = zmapInfoGetMapType(NULL);

	if(eMapType == ZMTYPE_PVP)
	{
		int i;
		for(i=0;i<eaSize(&g_eaGameDetailsPartition);i++)
		{
			if (g_eaGameDetailsPartition[i])
				gslPVPGame_update(g_eaGameDetailsPartition[i],fTime);
		}
	}
}

void gslPVPGame_PartitionLoad(int iPartitionIdx)
{
}

void gslPVPGame_PartitionUnload(int iPartitionIdx)
{
	if ((iPartitionIdx < eaSize(&g_eaGameDetailsPartition)) && g_eaGameDetailsPartition[iPartitionIdx])
		StructDestroy(parse_PVPCurrentGameDetails, g_eaGameDetailsPartition[iPartitionIdx]);
	eaSet(&g_eaGameDetailsPartition,NULL,iPartitionIdx);
}

void gslPVPGame_MapUnload(void)
{
	// Destroy all partitions
	int i;
	for(i=eaSize(&g_eaGameDetailsPartition)-1; i>=0; --i){
		StructDestroy(parse_PVPCurrentGameDetails, g_eaGameDetailsPartition[i]);
	}

	eaClear(&g_eaGameDetailsPartition);
}

void gslPVPGame_MapLoad(void)
{
	// apparently, we are not guaranteed to get an Unload between loads
	gslPVPGame_MapUnload();
}

///////////////////////////////////////////////////////////////////////////////
// AUTO_STARTUP

// Reload PVPGamePoints top level callback
static void PVPGameReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading %s...","CombatConfig");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);
	StructInit(parse_PVPGamePointsDef, &s_PointsStruct);
	ParserLoadFiles(NULL,"defs/config/PVPPoints.def","PVPPoints.bin",PARSER_OPTIONALFLAG,parse_PVPGamePointsDef,&s_PointsStruct);
	loadend_printf(" done.");
}

AUTO_STARTUP(PVPGame) ASTRT_DEPS(Critters);
void PVPGameLoad(void)
{
	loadstart_printf("Loading %s...","PVPGame");

	//Fill-in the default values
	StructInit(parse_PVPGamePointsDef, &s_PointsStruct);

	ParserLoadFiles(NULL,"defs/config/PVPPoints.def","PVPPoints.bin",PARSER_OPTIONALFLAG,parse_PVPGamePointsDef,&s_PointsStruct);

	// Reload callbacks
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/PVPPoints.def", PVPGameReload);
	}

	loadend_printf(" done.");
}

///////////////////////////////
// Commands

/*
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gslPVPGame_SpecialAction(Entity *pPlayer, Vec3 vTarget)
{
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(entGetPartitionIdx(pPlayer));

	if(pInfo && pGameDetails)
	{
		switch(pGameDetails->eCurrentGameType)
		{
		case kPVPGameType_CaptureTheFlag:
			gslPVPgame_ThrowFlag(pInfo,pPlayer,vTarget);
		}
	}
}
*/

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void ReturnFromPVPMatch(Entity* e)
{
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(entGetPartitionIdx(e));
	MapState *pMapState = mapState_FromPartitionIdx(entGetPartitionIdx(e));
	ScoreboardState eScoreboardState = mapState_GetScoreboardState(pMapState);
	
	if (!e)
		return;

	//If you are dead, and can't respawn, leave
	if(entIsAlive(e) == false && e->pPlayer && e->pPlayer->bDisableRespawn)
	{
		bool bDoLeaveTeam=true;
//  WOLF[1Feb13] I hope this is the right thing. Runs the new abandon code which removes players from the map-bound team.			
		gslQueue_HandleAbandonMap(e,entGetContainerID(e),bDoLeaveTeam);
//		if (LeaveMapEx(e, NULL))
//		{
//			gslQueue_HandleLeaveMap(e);
//		}
	}

	//If the game is over, leave
	if(eScoreboardState == kScoreboardState_Final)
	{
		// Things are tricky here. Calling 	gslQueue_HandleAbandonMap(e,entGetContainerID(e),bDoLeaveTeam);
		//  with bDoLeaveTeam false is the disband the team option.
		// gslQueue_HandleAbandonMap with bDoLeaveTeam true doesn't work. I think because the team is still owned by the map
		//  and the team gets removed through another channel (don't have time to debug that right now)
		// Let's just leave the map and behave like PvE doors. The "Return to Instance" button will be up, but we can just
		//  disable that on PvP maps from the uigen 
		
//		bool bDoLeaveTeam=false;
//		gslQueue_HandleAbandonMap(e,entGetContainerID(e),bDoLeaveTeam);
		if (LeaveMapEx(e, NULL))
		{
			gslQueue_HandleLeaveMap(e);
		}
	}
}

// Debug commands
AUTO_COMMAND;
void pvpGame_setScore(Entity *pPlayer, int iGroup, int iScore)
{
	if(pPlayer)
	{
		PVPCurrentGameDetails * pGameDetails = gslPvPGameDetailsFromIdx(entGetPartitionIdx(pPlayer));
		MatchMapState * pMatchMapState = pGameDetails->pMapState;

		if(pMatchMapState && iGroup < eaSize(&pMatchMapState->ppGroupGameParams))
			pMatchMapState->ppGroupGameParams[iGroup]->iScore = iScore;

		if(iScore >= pGameDetails->pRules->publicRules.iPointMax)
			gslPVPGame_end(pGameDetails);
	}
}

AUTO_COMMAND;
void pvpGame_End(Entity *pPlayer)
{
	if(pPlayer)
	{
		PVPCurrentGameDetails * pGameDetails = gslPvPGameDetailsFromIdx(entGetPartitionIdx(pPlayer));

		if(pGameDetails)
			gslPVPGame_end(pGameDetails);
	}
}

#include "gslPVPGame_h_ast.c"
