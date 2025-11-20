/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "Expression.h"
#include "referencesystem.h"
#include "EntityIterator.h"
#include "Player.h"
#include "gslCritter.h"
#include "gslQueue.h"
#include "gslMapState.h"
#include "gslPartition.h"
#include "gslPlayerMatchStats.h"
#include "EntityLib.h"
#include "Player.h"
#include "PowerActivation.h"
#include "cmdServerCombat.h"
#include "NotifyCommon.h"
#include "GameStringFormat.h"
#include "Leaderboard.h"

#include "PvPGameCommon_h_ast.h"
#include "Powers_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "autogen/AppServerLib_autogen_RemoteFuncs.h"
#include "queue_common_structs.h"
#include "queue_common_structs_h_ast.h"
#include "Leaderboard_h_ast.h"

#include "gslPvPGame.h"

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PvPAwardPlayerPoints);
ExprFuncReturnVal exprFuncPvPAwardPlayerPoints(ACMD_EXPR_ENTARRAY_IN entsIn, int iPoints, const char *pchMessage, ACMD_EXPR_ERRSTRING errString)
{
	int i, n;
	ExprFuncReturnVal retVal = ExprFuncReturnFinished;
	Message *pMessage = RefSystem_ReferentFromString(gMessageDict, pchMessage);

	if (pchMessage && pchMessage[0] && !pMessage) {
		estrPrintf(errString, "No message entry found for %s",pchMessage);
		return ExprFuncReturnError;
	}

	n = eaSize(entsIn);
	for(i=0; i<n; i++) {
		Entity* pEntity = (*entsIn)[i];

		if (!iPoints) {
			estrPrintf(errString, "Awarding 0 Points to player %s", pEntity->debugName);
			retVal = ExprFuncReturnError;
		} else {
			gslPVPGame_awardPoints(pEntity,iPoints,pchMessage);
		}
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(allowPlayerRespawns) ACMD_CATEGORY(PVP);
void exprPVPGame_allowPlayerRespawns(ACMD_EXPR_PARTITION iPartitionIdx, bool bForceRespawn)
{
	EntityIterator *pIter = entGetIteratorSingleType(iPartitionIdx, ENTITYFLAG_DEAD, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	Entity *pEnt;

	while(pEnt = EntityIteratorGetNext(pIter))
	{
		pEnt->pPlayer->bDisableRespawn = false;

		if(bForceRespawn)
			gslPlayerRespawn(pEnt,true,false);
	}

	EntityIteratorRelease(pIter);
}

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(PlayersAliveInGroup) ACMD_CATEGORY(PVP);
int exprPVPGame_playersAliveInGroup(ACMD_EXPR_PARTITION partitionIdx, int iGroupNumber)
{
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(partitionIdx);
	int iEntsAlive = 0;

	if(iGroupNumber < eaSize(&pInfo->pMatch->eaGroups) && iGroupNumber >= 0)
	{
		int i;


		for(i=0;i<eaSize(&pInfo->pMatch->eaGroups[iGroupNumber]->eaMembers);i++)
		{
			Entity *pEnt = entFromContainerID(pInfo->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER,pInfo->pMatch->eaGroups[iGroupNumber]->eaMembers[i]->uEntID);

			if(pEnt && entIsAlive(pEnt))
				iEntsAlive++;
		}
	}

	return iEntsAlive;
}

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(AddPointsToPlayer) ACMD_CATEGORY(PVP);
void exprPVPGame_addPointsToPlayer(SA_PARAM_NN_VALID Entity *pPlayer, int iPoints, const char *pchMessageKey)
{
	gslPVPGame_awardPoints(pPlayer,iPoints,pchMessageKey);
}

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(AddPointsToTeam) ACMD_CATEGORY(PVP);
void exprPVPGame_addPointsToTeam(ACMD_EXPR_PARTITION partitionIdx, int iTeamNumber, int iPoints, const char *pchMessageKey)
{
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(partitionIdx);

	if(pInfo && pInfo->pMatch && iTeamNumber > 0 && iTeamNumber <= eaSize(&pInfo->pMatch->eaGroups))
	{
		int i;

		for(i=0;i<eaSize(&pInfo->pMatch->eaGroups[iTeamNumber-1]->eaMembers);i++)
		{
			Entity *pMember = entFromContainerID(pInfo->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER,pInfo->pMatch->eaGroups[iTeamNumber-1]->eaMembers[i]->uEntID);

			if(pMember)
				gslPVPGame_awardPoints(pMember,iPoints,pchMessageKey);
		}
	}
}

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(AddScoreToTeam) ACMD_CATEGORY(PVP);
void exprPVPGame_AddScoreToTeam(ACMD_EXPR_PARTITION partitionIdx, int iTeamNumber, int iPoints)
{
	PVPCurrentGameDetails * pGameDetails = gslPvPGameDetailsFromIdx(partitionIdx);
	MatchMapState * pMatchMapState = pGameDetails->pMapState;

	if(pMatchMapState && iTeamNumber > 0 && iTeamNumber <= eaSize(&pMatchMapState->ppGroupGameParams))
	{
		pMatchMapState->ppGroupGameParams[iTeamNumber-1]->iScore += iPoints;

		if(pMatchMapState->ppGroupGameParams[iTeamNumber-1]->iScore > pGameDetails->pRules->publicRules.iPointMax)
			gslPVPGame_end(pGameDetails);
	}
}

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(GetTeamScore) ACMD_CATEGORY(PVP);
int exprPVPGame_GetTeamScore(ACMD_EXPR_PARTITION partitionIdx, int iTeamNumber)
{
	PVPCurrentGameDetails * pGameDetails = gslPvPGameDetailsFromIdx(partitionIdx);
	if (pGameDetails)
	{
		MatchMapState * pMatchMapState = pGameDetails->pMapState;

		if(pMatchMapState && iTeamNumber > 0 && iTeamNumber <= eaSize(&pMatchMapState->ppGroupGameParams))
		{
			return pMatchMapState->ppGroupGameParams[iTeamNumber-1]->iScore;
		}
	}

	return 0;
}

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(EndGame) ACMD_CATEGORY(PVP);
void exprPVPGame_EndGame(ACMD_EXPR_PARTITION partitionIdx)
{
	PVPCurrentGameDetails * pGameDetails = gslPvPGameDetailsFromIdx(partitionIdx);

	gslPVPGame_end(pGameDetails);
}

static void pvpGame_GetAllPlayerEnts(Entity ***eaEntsOut, int iPartitionIdx)
{
	Entity *pCurrEnt;
	EntityIterator *pIter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	while ((pCurrEnt = EntityIteratorGetNext(pIter))) 
	{
		eaPush(eaEntsOut, pCurrEnt);
	}
	EntityIteratorRelease(pIter);
}

static void pvpGame_SendNotificationToEnts(Entity **eaEnts, NotifyType notifyType, const char* pcMsgKey, Entity *pTarget)
{
	S32 i;
	static char *s_estrTextLanguages[LANGUAGE_MAX] = {NULL};
	static char *s_estrFormatted = NULL;
	Message *pMessage = RefSystem_ReferentFromString(gMessageDict, pcMsgKey);

	if (!pMessage || !eaSize(&eaEnts))
		return;
	for(i=0; i<LANGUAGE_MAX; i++) 
	{
		estrClear(&s_estrTextLanguages[i]);
	}

	// Say the message to all nearby players in their language
	FOR_EACH_IN_EARRAY(eaEnts, Entity, pEnt)
	{
		Language iLangID;
		if (!entIsPlayer(pEnt)) 
			continue;

		iLangID = entGetLanguage(pEnt);
		if (iLangID < 0 || iLangID >= LANGUAGE_MAX)
			continue;

		// Only translate once per language
		if (!s_estrTextLanguages[iLangID] || !s_estrTextLanguages[iLangID][0]) 
		{
			const char *pcTranslated = langTranslateMessage(iLangID, pMessage);
			estrCopy2(&s_estrTextLanguages[iLangID], pcTranslated);
		}

		if (s_estrTextLanguages[iLangID] && s_estrTextLanguages[iLangID][0]) 
		{
			Entity *pMapOwner;
			estrClear(&s_estrFormatted);

			// Format text
			pMapOwner = partition_GetPlayerMapOwner(entGetPartitionIdx(pEnt));

			langFormatGameString(iLangID, &s_estrFormatted, s_estrTextLanguages[iLangID], 
								STRFMT_ENTITY_KEY("Entity", pEnt),
								STRFMT_ENTITY_KEY("Target", pTarget),
								STRFMT_END);
			
			ClientCmd_NotifySend(pEnt, notifyType, s_estrFormatted, NULL, NULL);
		}
	}	
	FOR_EACH_END
}

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(NotifyPlayerOfKillingSpree) ACMD_CATEGORY(PVP);
void exprPVPGame_NotifyPlayerOfKillingSpree(ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_DICT(message) const char *pcMsgKey, const char *pcImg)
{
	Entity *pKillingSpreeEnt = eaGet(peaEnts, 0);
	if (pKillingSpreeEnt && entIsPlayer(pKillingSpreeEnt) && pcMsgKey)
	{
		Entity **eaEntsToSentTo = NULL;

		pvpGame_GetAllPlayerEnts(&eaEntsToSentTo, entGetPartitionIdx(pKillingSpreeEnt));
		pvpGame_SendNotificationToEnts(eaEntsToSentTo, kNotifyType_Default, pcMsgKey, pKillingSpreeEnt);
		eaDestroy(&eaEntsToSentTo);

		ClientCmd_NotifySend(pKillingSpreeEnt, kNotifyType_PVPKillingSpree, " ", NULL, pcImg);

	}
}

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(PvPGameStarted) ACMD_CATEGORY(PVP);
bool exprPVPGame_GameStarted(ACMD_EXPR_PARTITION partitionIdx)
{
	return mapState_GetScoreboardState(mapState_FromPartitionIdx(partitionIdx)) == kScoreboardState_Active;
}


static U64 compactContainerIDValue(ContainerID entID, S32 iValue)
{
	return (((U64)iValue) << 32) | (U64)entID;
}

static void deconstructContainerIDValue(U64 val, ContainerID *pRefOut, S32 *piValueOut)
{
	*pRefOut = (ContainerID)val;
	*piValueOut = (S32)(val >> 32);
}

static int containerIDValueSortComparator(const U64 *val1, const U64 *val2)
{
	S32 iValue1, iValue2;
	ContainerID iEnt1, iEnt2;

	deconstructContainerIDValue(*val1, &iEnt1, &iValue1);
	deconstructContainerIDValue(*val2, &iEnt2, &iValue2);

	if (iValue1 > iValue2)
	{
		return -1;
	}
	else if (iValue2 > iValue1)
	{
		return 1;
	}

	return iEnt1 - iEnt2;
}

static void cachePlayerValueRanks(MapState *pMapState, U64 **peaCachedEntityIDValues, const char *pchValue)
{
	ea64Clear(peaCachedEntityIDValues);

	FOR_EACH_IN_EARRAY(pMapState->pPlayerValueData->eaPlayerValues, PlayerMapValues, pPlayerMapValue) 
	{
		MapStateValue* pValue = mapState_FindMapValueInArray(&pPlayerMapValue->eaValues, pchValue);

		if (pValue)
		{
			bool bTest = false;
			S32 iValue = MultiValGetInt(&pValue->mvValue, &bTest);
			if (bTest)
			{
				U64 stuffedEntIDValue = compactContainerIDValue(pPlayerMapValue->iEntID, iValue);
				ea64Push(peaCachedEntityIDValues, stuffedEntIDValue);
			}
		}
	}
	FOR_EACH_END

	ea64QSort(*peaCachedEntityIDValues, containerIDValueSortComparator);
}

#define PLAYER_RANK_ERROR 1000000

// returns the rank of the player based on the given value. The Rank starts at 1
// sorts from highest to lowest. 
// returns 100000 (arbitrary high number) if there was an error
AUTO_EXPR_FUNC(player) ACMD_NAME(PvPGameGetPlayerRankByPlayerValue) ACMD_CATEGORY(PVP);
S32 exprPVPGame_GetPlayerRankByPlayerValue(	ACMD_EXPR_PARTITION partitionIdx, 
											SA_PARAM_NN_VALID Entity *pEnt, 
											const char *pchValue)
{
	static U64 *s_eaCachedEntityIDValues = NULL;
	static U32 s_iCachedTime = 0;
	static const char *s_pchCachedValue = NULL;
	static int s_iCachedPartition = 0;
	ContainerID entID;
	S32 i;

	MapState *pMapState = mapState_FromPartitionIdx(partitionIdx);
	if (!pMapState)
		return PLAYER_RANK_ERROR;

	pchValue = allocFindString(pchValue);
	if (!pchValue)
		return PLAYER_RANK_ERROR;

	if (s_iCachedTime != pMapState->uServerTimeSecondsSince2000 ||
		s_pchCachedValue != pchValue ||
		s_iCachedPartition != partitionIdx)
	{
		cachePlayerValueRanks(pMapState, &s_eaCachedEntityIDValues, pchValue);
		s_iCachedTime = pMapState->uServerTimeSecondsSince2000;
		s_pchCachedValue = pchValue;
		s_iCachedPartition = partitionIdx;
	}
	
	entID = entGetContainerID(pEnt);
	
	for (i = 0; i < ea64Size(&s_eaCachedEntityIDValues); ++i)
	{
		U64 val = s_eaCachedEntityIDValues[i];

		// entity ID should be the first 32 bits, so just cast and compare
		if ((ContainerID)val == entID)
		{
			return i + 1;
		}
	}

	return PLAYER_RANK_ERROR;
}