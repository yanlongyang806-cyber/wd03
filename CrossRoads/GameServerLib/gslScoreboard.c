/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "entCritter.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "gslMapState.h"
#include "gslScoreboard.h"
#include "mapstate_common.h"
#include "StringCache.h"
#include "CharacterClass.h"

#include "AutoGen/gslScoreboard_h_ast.h"
#include "AutoGen/gslScoreboard_h_ast.c"
#include "AutoGen/pvp_common_h_ast.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

static ScoreboardPartitionInfo** s_eaScoreboardPartitionInfo = NULL;

ScoreboardPartitionInfo* gslScoreboard_PartitionInfoFromIdx(int iPartitionIdx)
{
	return eaIndexedGetUsingInt(&s_eaScoreboardPartitionInfo, iPartitionIdx);
}

static void gslScoreboard_CreateAllPlayerScoreData(ScoreboardPartitionInfo* pInfo)
{
	EntityIterator* iter = entGetIteratorSingleType(pInfo->iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
	Entity* pEnt;
			
	while ((pEnt = EntityIteratorGetNext(iter)))
	{
		gslScoreboard_CreatePlayerScoreData(pEnt);
	}
	EntityIteratorRelease(iter);
}

ScoreboardPartitionInfo* gslScoreboard_CreateInfo(int iPartitionIdx)
{
	ScoreboardPartitionInfo* pInfo = gslScoreboard_PartitionInfoFromIdx(iPartitionIdx);
	if (!pInfo)
	{
		pInfo = StructCreate(parse_ScoreboardPartitionInfo);
		pInfo->iPartitionIdx = iPartitionIdx;
		if (!s_eaScoreboardPartitionInfo)
			eaIndexedEnable(&s_eaScoreboardPartitionInfo, parse_ScoreboardPartitionInfo);
		eaPush(&s_eaScoreboardPartitionInfo, pInfo);

		// Initialize all player score data
		gslScoreboard_CreateAllPlayerScoreData(pInfo);
	}
	return pInfo;
}

void gslScoreboard_SetDefaultFaction(int iPartitionIdx, CritterFaction* pFaction)
{
	ScoreboardPartitionInfo* pInfo = gslScoreboard_PartitionInfoFromIdx(iPartitionIdx);
	if (pInfo)
	{
		SET_HANDLE_FROM_REFERENT("CritterFaction", pFaction, pInfo->hDefaultFaction);
	}
}

void gslScoreboard_CreateGroup( int iPartitionIdx, CritterFaction* pFaction, Message* pDisplayMessage, const char *pchGroupTexture )
{
	ScoreboardPartitionInfo* pInfo = gslScoreboard_CreateInfo(iPartitionIdx);
	int i;

	if (!pFaction && !pDisplayMessage)
	{
		Errorf("gslScoreboard_CreateGroup must be called with either a valid faction or display message");
		return;
	}

	for (i = eaSize(&pInfo->Scores.eaGroupList)-1; i >= 0; i--)
	{
		ScoreboardGroup* pGroup = pInfo->Scores.eaGroupList[i];
		
		if (pFaction && pFaction->pchName == pGroup->pchFactionName)
		{
			break;
		}
		if (pDisplayMessage && pDisplayMessage == GET_REF(pGroup->hDisplayMessage))
		{
			break;
		}
	}

	if (i < 0)
	{
		ScoreboardGroup* pScoreboardGroup = StructCreate(parse_ScoreboardGroup);
		SET_HANDLE_FROM_REFERENT("Message", pDisplayMessage, pScoreboardGroup->hDisplayMessage);
		pScoreboardGroup->pchFactionName = pFaction ? allocAddString(pFaction->pchName) : NULL;
		if (pchGroupTexture)
		{
			pScoreboardGroup->pchGroupTexture = pchGroupTexture;
		}
		eaPush(&pInfo->Scores.eaGroupList, pScoreboardGroup);
	}
}

// Create player data only if the scoreboard partition information exists
void gslScoreboard_CreatePlayerScoreData(Entity* pEnt)
{
	ScoreboardPartitionInfo* pInfo = gslScoreboard_PartitionInfoFromIdx(entGetPartitionIdx(pEnt));
	if (pInfo)
	{
		ScoreboardEntity* pScore = eaIndexedGetUsingInt(&pInfo->Scores.eaScoresList, pEnt->myContainerID);
		if (!pScore)
		{
			CharacterPath *pCharPath = entity_GetPrimaryCharacterPath(pEnt);
			pScore = StructCreate(parse_ScoreboardEntity);
			pScore->pchName = StructAllocString(entGetLocalName(pEnt));
			pScore->pchAccountName = StructAllocString(entGetAccountOrLocalName(pEnt));
			pScore->iEntID = pEnt->myContainerID;
			pScore->pchCharacterPathName = pCharPath ? StructAllocString(pCharPath->pchName) : StructAllocString("");
			eaPush(&pInfo->Scores.eaScoresList, pScore);
		}
	}
}

void gslScoreboard_DestroyPlayerScoreData(Entity* pEnt)
{
	ScoreboardPartitionInfo* pInfo = gslScoreboard_PartitionInfoFromIdx(entGetPartitionIdx(pEnt));
	if (pInfo && pInfo->bRemoveInactivePlayerScores)
	{
		int i = eaIndexedFindUsingInt(&pInfo->Scores.eaScoresList, pEnt->myContainerID);
		if (i >= 0)
		{
			StructDestroy(parse_ScoreboardEntity, eaRemove(&pInfo->Scores.eaScoresList, i));
		}
	}
}

static void gslScoreboard_SendScoresToClients(ScoreboardPartitionInfo* pInfo)
{
	EntityIterator* iter = entGetIteratorSingleType(pInfo->iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	Entity* pEnt;
			
	while ((pEnt = EntityIteratorGetNext(iter)))
	{
		if(!pEnt || !pEnt->pPlayer || !pEnt->pChar)
			continue;

		ClientCmd_PVP_UpdateScores(pEnt, &pInfo->Scores);
	}
	EntityIteratorRelease(iter);
}

static void gslScoreboard_SetScoresInactive(ScoreboardPartitionInfo* pScoreboardInfo)
{
	if (pScoreboardInfo)
	{
		int i;
		for (i = eaSize(&pScoreboardInfo->Scores.eaScoresList)-1; i >= 0; i--)
		{
			ScoreboardEntity* pScore = pScoreboardInfo->Scores.eaScoresList[i];
			Entity* pEnt = NULL;
			
			if (pScore->entRef)
			{
				pEnt = entFromEntityRef(pScoreboardInfo->iPartitionIdx, pScore->entRef);
			}
			if (!pEnt || pEnt->myContainerID != pScore->iEntID)
			{
				pScore->entRef = 0;
			}
			pScore->bActive = false;
		}
	}
}

static void gslScoreboard_UpdateScores(ScoreboardPartitionInfo* pInfo, MapState *pState)
{
	ScoreboardEntityList *pScores = &pInfo->Scores;
	
	if (pState && pState->pPlayerValueData)
	{
		int i, n = eaSize(&pState->pPlayerValueData->eaPlayerValues);
		
		PERFINFO_AUTO_START_FUNC();

		// Make all scores inactive
		gslScoreboard_SetScoresInactive(pInfo);

		for (i = 0; i < n; i++)
		{
			PlayerMapValues* pPlayerValues = pState->pPlayerValueData->eaPlayerValues[i];
			Entity* pEnt = entFromContainerID(pInfo->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pPlayerValues->iEntID);
			if(pEnt)
			{
				MapStateValue* pValue;
				CritterFaction *pDefaultFaction;
				CritterFaction *pPlayerFaction;
				ScoreboardEntity *pScore;
				
				pScore = eaIndexedGetUsingInt(&pScores->eaScoresList, pEnt->myContainerID);
				if (!pScore)
				{
					continue;
				}

				pScore->entRef = pEnt->myRef;
				pDefaultFaction = GET_REF(pInfo->hDefaultFaction);
				pPlayerFaction = entGetFaction(pEnt);

				if (pPlayerFaction)
				{
					//If a default faction is set, and the player isn't the default faction,
					//then assume that it is the battle faction and set that as their faction for scoring
					if (!pDefaultFaction || pPlayerFaction != pDefaultFaction)
					{
						pScore->pchFactionName = allocAddString(pPlayerFaction->pchName);
					}
				}
				else if (!pDefaultFaction)
				{
					pScore->pchFactionName = allocAddString("");
				}

				pScore->iPlayerKills = pScore->iDeathsToPlayers = pScore->iPoints = 0;
				pScore->iTotalKills = pScore->iDeathsTotal = 0;
				pScore->iBossKills = pScore->iDeathsToBosses = 0;
				pScore->iPlayerHealing = 0;
				pScore->iPlayerDamage = 0;
				pScore->iPlayerAssaultTeams = 0;
				
				pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Player_Kills");
				if(pValue)
					pScore->iPlayerKills = MultiValGetInt(&pValue->mvValue, NULL);

				pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Player_Assists");
				if(pValue)
					pScore->iPlayerAssists = MultiValGetInt(&pValue->mvValue,NULL);

				pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Player_Deaths");
				if(pValue)
					pScore->iDeathsToPlayers = MultiValGetInt(&pValue->mvValue, NULL);

				pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Player_Time");
				if(pValue)
					pScore->iPlayerTime = MultiValGetInt(&pValue->mvValue, NULL);

				pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Player_Healing");
				if(pValue)
					pScore->iPlayerHealing = MultiValGetInt(&pValue->mvValue, NULL);

				pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Player_Damage");
				if(pValue)
					pScore->iPlayerDamage = MultiValGetInt(&pValue->mvValue, NULL);

				pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Boss_Kills");
				if(pValue)
					pScore->iBossKills = MultiValGetInt(&pValue->mvValue, NULL);

				pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Boss_Deaths");
				if(pValue)
					pScore->iDeathsToBosses = MultiValGetInt(&pValue->mvValue, NULL);

				pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Total_Kills");
				if(pValue)
					pScore->iTotalKills = MultiValGetInt(&pValue->mvValue, NULL);

				pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Total_Deaths");
				if(pValue)
					pScore->iDeathsTotal = MultiValGetInt(&pValue->mvValue, NULL);

				pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Player_Assault_Teams");
				if(pValue)
					pScore->iPlayerAssaultTeams = MultiValGetInt(&pValue->mvValue, NULL);

				pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Player_Points");
				if(pValue)
					pScore->iPoints = MultiValGetInt(&pValue->mvValue, NULL);
				else
					pScore->iPoints = pScore->iPlayerKills;

				// Set the score as active
				pScore->bActive = true;
			}
		}

		//Send the scores to players on the map
		gslScoreboard_SendScoresToClients(pInfo);

		PERFINFO_AUTO_STOP();
	}
}

void gslScoreboard_Reset(int iPartitionIdx)
{
	ScoreboardPartitionInfo* pInfo = gslScoreboard_PartitionInfoFromIdx(iPartitionIdx);
	MapState* pMapState = mapState_FromPartitionIdx(iPartitionIdx);

	if (pInfo && pMapState->pPlayerValueData)
	{
		int i, n = eaSize(&pMapState->pPlayerValueData->eaPlayerValues);

		for (i = 0; i < n; i++)
		{
			PlayerMapValues* pPlayerValues = pMapState->pPlayerValueData->eaPlayerValues[i];
			MapStateValue* pValue;

			if(eaIndexedFindUsingInt(&pInfo->Scores.eaScoresList, pPlayerValues->iEntID) < 0)
			{
				continue;
			}
			pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Player_Kills");
			if (pValue)
				MultiValSetInt(&pValue->mvValue, 0);
			pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Player_Assists");
			if (pValue)
				MultiValSetInt(&pValue->mvValue, 0);
			pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Player_Deaths");
			if (pValue)
				MultiValSetInt(&pValue->mvValue, 0);
			pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Player_Time");
			if (pValue)
				MultiValSetInt(&pValue->mvValue, 0);
			pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Player_Healing");
			if (pValue)
				MultiValSetInt(&pValue->mvValue, 0);
			pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Player_Damage");
			if (pValue)
				MultiValSetInt(&pValue->mvValue, 0);
			pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Boss_Kills");
			if (pValue)
				MultiValSetInt(&pValue->mvValue, 0);
			pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Boss_Deaths");
			if (pValue)
				MultiValSetInt(&pValue->mvValue, 0);
			pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Total_Kills");
			if (pValue)
				MultiValSetInt(&pValue->mvValue, 0);
			pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Total_Deaths");
			if (pValue)
				MultiValSetInt(&pValue->mvValue, 0);
			pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Player_Assault_Teams");
			if (pValue)
				MultiValSetInt(&pValue->mvValue, 0);
			pValue = mapState_FindMapValueInArray(&pPlayerValues->eaValues, "Player_Points");
			if (pValue)
				MultiValSetInt(&pValue->mvValue, 0);
		}
	}
	if (pInfo)
	{
		gslScoreboard_UpdateScores(pInfo, pMapState);
	}
}

void gslScoreboard_PartitionUnload(int iPartitionIdx)
{
	int i = eaIndexedFindUsingInt(&s_eaScoreboardPartitionInfo, iPartitionIdx);
	if (i >= 0)
	{
		ScoreboardPartitionInfo *pInfo = eaRemove(&s_eaScoreboardPartitionInfo, i);
		StructDestroy(parse_ScoreboardPartitionInfo, pInfo);
	}
}

void gslScoreboard_MapUnload(void)
{
	// Destroy all partitions
	int i;
	for (i = eaSize(&s_eaScoreboardPartitionInfo)-1; i >= 0; i--) 
	{
		StructDestroy(parse_ScoreboardPartitionInfo, eaRemove(&s_eaScoreboardPartitionInfo, i));
	}
}

void gslScoreboard_Tick(F32 fElapsedTime)
{
	static F32 s_fTickTime = 0;

	s_fTickTime += fElapsedTime;

	if(s_fTickTime > 1.0f)
	{	
		int i;
		for (i = 0; i < eaSize(&s_eaScoreboardPartitionInfo); i++)
		{
			ScoreboardPartitionInfo* pInfo = s_eaScoreboardPartitionInfo[i];
			MapState* pMapState = mapState_FromPartitionIdx(pInfo->iPartitionIdx);

			gslScoreboard_UpdateScores(pInfo, pMapState);
		}
		s_fTickTime = 0.0f;
	}
}