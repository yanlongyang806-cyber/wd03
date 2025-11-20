/***************************************************************************
*     Copyright (c) 2005-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Character.h"
#include "Character_h_ast.h"
#include "CharacterClass.h"
#include "chatCommonStructs.h"
#include "Entity.h"
#include "entCritter.h"
#include "EntityLib.h"
#include "EntityIterator.h"
#include "EntitySavedData.h"
#include "Error.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GameAccountDataCommon.h"
#include "GameServerLib.h"
#include "GameStringFormat.h"
#include "gslActivity.h"
#include "gslEventSend.h"
#include "gslEntity.h"
#include "gslChat.h"
#include "gslMapVariable.h"
#include "gslMechanics.h"
#include "gslMission.h"
#include "gslOpenMission.h"
#include "gslPartition.h"
#include "gslQueue.h"
#include "gslScoreboard.h"
#include "gslTeam.h"
#include "gslWorldVariable.h"
#include "Guild.h"
#include "itemCommon.h"
#include "rewardCommon.h"
#include "MapDescription.h"
#include "mapstate_common.h"
#include "mission_common.h"
#include "NotifyCommon.h"
#include "objTransactions.h"
#include "Player.h"
#include "pvp_common.h"
#include "queue_common.h"
#include "queue_common_h_ast.h"
#include "queue_common_structs.h"
#include "Team.h"
#include "ServerLib.h"
#include "StringCache.h"
#include "WorldGrid.h"
#include "gslMapState.h"
#include "gslPartition.h"
#include "PowerAnimFX.h"
#include "PowersMovement.h"
#include "dynFxManager.h"
#include "PvPGameCommon.h"
#include "gslPVPGame.h"
#include "gslTeamUp.h"
#include "TeamUpCommon.h"

#include "gslQueue_h_ast.h"
#include "pvp_common_h_ast.h"
#include "queue_common_h_ast.h"
#include "queue_common_structs_h_ast.h"
#include "entity_h_ast.h"
#include "Player_h_ast.h"

#include "autogen/AppServerLib_autogen_remotefuncs.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

#define QUEUE_PLAYER_LEFT_GAME_MESG "QueueServer_PlayerLeftGame"
#define QUEUE_PLAYER_JOINED_GAME_MESG "QueueServer_PlayerJoinedGame"
#define QUEUE_PLAYER_KILLED_MESG "QueueServer_PlayerKilled"
#define QUEUE_PLAYER_KILLED_NOTARGET_MESG "QueueServer_PlayerKilledNoTarget"
#define QUEUE_REQUEST_TIME 45	
#define QUEUE_ALLOW_CONCEDE_START_TIME 120
#define QUEUE_ALLOW_LEAVE_START_TIME 120
#define QUEUE_INITIAL_AUTO_CONCEDE_TIME 20
#define QUEUE_WAIT_FOR_MATCH_INFO_TIMEOUT 30
#define QUEUE_MEMBER_FORCE_LEAVE_TIMEOUT 5

//A bunch of random static or global variables
QueueList *g_pQueueList;

static QueuePartitionInfo **g_ppQueuePartitionInfos = NULL;
static QueuePartitionMatchUpdate **s_ppMatchUpdates = NULL;
static QueuePendingChatMessage** s_eaPendingChatMessages = NULL;

//Turns on debug queues in production mode
int g_iShowDebugQueues = 0;
int g_iDebugQueueServer = 1;

QueueList *g_pQueueList = NULL;
U32 s_uiQueueListNextRequestTime = 0;
U32 s_uiQueueListClearTimer = 0;
U32 s_uiQueueNewDataFromQueueServer = 0;	// Set by the gslQueue_GetQueueList when it gets new data. 

LocalTeam* gslQueue_FindLocalTeamByID(U32 uTeamID)
{
	S32 i, j;
	for (i = eaSize(&g_ppQueuePartitionInfos)-1; i >= 0; i--)
	{
		QueuePartitionInfo* pInfo = g_ppQueuePartitionInfos[i];
		for (j = eaSize(&pInfo->ppLocalTeams)-1; j >= 0; j--)
		{
			LocalTeam* pLocalTeam = pInfo->ppLocalTeams[j];
			if (pLocalTeam->iTeamID == uTeamID)
			{
				return pLocalTeam;
			}
		}
	}
	return NULL;
}

void gslQueue_LocalTeamUpdate(QueuePartitionInfo* pInfo)
{
	QueueDef* pDef;

	if (!pInfo->bAutoTeamLocalTeamInitialized && (pDef = gslQueue_GetQueueDef(pInfo->iPartitionIdx)))
	{
		pInfo->bAutoTeamsEnabled = pDef->MapRules.bEnableAutoTeam;
		pInfo->bMergeGroups = pDef->Settings.bSplitTeams;
		pInfo->iMaxLocalTeamsPerGroup = pDef->MapRules.iMaxLocalTeamsPerGroup;
		pInfo->bAutoTeamMembersCanBeOnOtherMap = (pDef->Settings.bStayInQueueOnMapLeave || g_QueueConfig.bStayInQueueOnMapLeave);
		pInfo->bAutoTeamLocalTeamInitialized = true;
	}
		
	//If it's enabled, then auto team!
	if (pInfo->bAutoTeamsEnabled)
	{
		gslTeam_AutoTeamUpdate(pInfo);
	}
	else
	{
		// AutoTeam could be turned off when the match completes.
		gslTeam_CleanupLocalTeams(pInfo); 
	}
}


// Find a player's private queue
PlayerQueueInstance* gslQueue_FindPrivateInstance(Entity *pEnt, PlayerQueue** ppQueue)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pPlayerQueueInfo)
	{
		S32 i, j, k;
		for (i = eaSize(&pEnt->pPlayer->pPlayerQueueInfo->eaQueues)-1; i >= 0; i--)
		{
			PlayerQueue* pQueue = pEnt->pPlayer->pPlayerQueueInfo->eaQueues[i];
			
			if (pQueue->eCannotUseReason != QueueCannotUseReason_None)
			{
				continue;
			}
			for (j = eaSize(&pQueue->eaInstances)-1; j >= 0; j--)
			{
				PlayerQueueInstance* pInstance = pQueue->eaInstances[j];
				if (pInstance->pParams && pInstance->pParams->uiOwnerID > 0)
				{
					for (k = eaSize(&pInstance->eaMembers)-1; k >= 0; k--)
					{
						if (pInstance->eaMembers[k]->uiID == entGetContainerID(pEnt))
						{
							if (ppQueue)
							{
								(*ppQueue) = pQueue;
							}
							return pInstance;
						}
					}
				}
			}
		}
	}
	return NULL;
}

QueuePartitionInfo *gslQueuePartitionInfoFromIdx(int iPartitionIdx)
{
	int i;

	i = eaIndexedFindUsingInt(&g_ppQueuePartitionInfos,iPartitionIdx);

	if(i>-1)
		return g_ppQueuePartitionInfos[i];

	return NULL;
}

void gslQueuePartition_ForEachPartition(UIQueueInfoFunction function)
{
	int i;

	for(i=0;i<eaSize(&g_ppQueuePartitionInfos);i++)
	{
		function(g_ppQueuePartitionInfos[i]);
	}
}

void gslQueue_AddPendingMatchUpdate(U32 uPartitionID, QueueMatch* pMatch)
{
	QueuePartitionMatchUpdate* pMatchUpdate = StructCreate(parse_QueuePartitionMatchUpdate);
	pMatchUpdate->uPartitionID = uPartitionID;
	pMatchUpdate->pMatch = StructClone(parse_QueueMatch, pMatch);
	eaPush(&s_ppMatchUpdates, pMatchUpdate);
}

static void gslQueue_ProcessPendingMatchUpdates(int iPartitionIdx)
{
	U32 uPartitionID = partition_IDFromIdx(iPartitionIdx);
	S32 i;
	for (i = 0; i < eaSize(&s_ppMatchUpdates); i++)
	{
		QueuePartitionMatchUpdate* pMatchUpdate = s_ppMatchUpdates[i];
		if (pMatchUpdate->uPartitionID == uPartitionID)
		{
			gslQueue_UpdateQueueMatch(uPartitionID, pMatchUpdate->pMatch);
			StructDestroy(parse_QueuePartitionMatchUpdate, eaRemove(&s_ppMatchUpdates, i));
		}
	}
}

QueueDef *gslQueue_GetQueueDef(int iPartitionIdx)
{
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);

	if(pInfo && pInfo->pGameInfo && pInfo->pGameInfo->pchQueueDef)
		return queue_DefFromName(pInfo->pGameInfo->pchQueueDef);
	else
		return NULL;
}

bool gslQueue_IgnorePVPMods(int iPartitionIdx)
{
	QueueDef *pDef = gslQueue_GetQueueDef(iPartitionIdx);
	if(pDef && pDef->MapSettings.eMapType == ZMTYPE_PVP)
	{
		return pDef->MapRules.bIgnoresPVPAttribMods;
	}

	return false;
}

bool gslQueue_IsQueueMap(void)
{
	ZoneMapType eMapType = zmapInfoGetMapType(NULL);

	return queue_IsQueueMap(eMapType);
}

// Returns whether or not an invalid queue should still be sent to the client
static bool gslQueue_ShouldShowInvalidQueue(QueueCannotUseReason eReason)
{
	switch (eReason)
	{
		case QueueCannotUseReason_Cooldown:
		case QueueCannotUseReason_ClassRequirements:
		case QueueCannotUseReason_RequiresUnteamed:
		case QueueCannotUseReason_GearRating:
		{
			return true;
		}
	}
	return false;
}


#define QUEUE_CNU_TOO_HIGH_LEVEL_MESG "QueueServer_CannotUseQueue_TooHighLevel"
#define QUEUE_CNU_TOO_LOW_LEVEL_MESG "QueueServer_CannotUseQueue_TooLowLevel"
#define QUEUE_CNU_SIDEKICKING_MESG "QueueServer_CannotUseQueue_Sidekicking"
#define QUEUE_CNU_INVALID_AFFILIATION_MESG "QueueServer_CannotUseQueue_AffiliationInvalid"
#define QUEUE_CNU_AFFILIATION_REQ_MESG "QueueServer_CannotUseQueue_AffiliationNeeded"
#define QUEUE_CNU_GROUP_REQS_MESG "QueueServer_CannotUseQueue_GroupRequirements"
#define QUEUE_CNU_REQ_MESG "QueueServer_CannotUseQueue_QueueRequirement"
#define QUEUE_CNU_MISSION_MESG "QueueServer_CannotUseQueue_MissionRequirement"
#define QUEUE_CNU_OTHER_MSG "QueueServer_CannotUseQueue"
#define QUEUE_CNU_PLAYER_PENALIZED_MESG "QueueServer_PlayerPenalized"
#define QUEUE_CNU_PLAYER_PREFS_MESG "QueueServer_PlayerNoPrefs"
#define QUEUE_CNU_COOLDOWN_MESG "QueueServer_QueueInCooldown"
#define QUEUE_CNU_REQUIRES_CLASS "QueueServer_CannotUseQueue_ClassRequirements"
#define QUEUE_CNU_REQUIRES_GUILD "QueueServer_CannotUseQueue_RequiresGuild"
#define QUEUE_CNU_REQUIRES_UNTEAMED "QueueServer_CannotUseQueue_RequiresUnteamed"
#define QUEUE_CNU_GEAR_RATING "QueueServer_CannotUseQueue_GearRating"
#define QUEUE_CNU_TEAM_AFFILIATION_MISMATCH "QueueServer_CannotTeamForQueue_AffiliationMismatch"

// Shared team/single
#define QUEUE_CNU_ACTIVITY_NOT_ACTIVE "QueueServer_ActivityNotActive"
#define QUEUE_CNU_EVENT_NOT_ACTIVE "QueueServer_EventNotActive"
#define QUEUE_CNU_TEAM_ACTIVITY_NOT_ACTIVE "QueueServer_ActivityNotActive"
#define QUEUE_CNU_TEAM_EVENT_NOT_ACTIVE "QueueServer_EventNotActive"

#define QUEUE_CNU_TEAM_TOO_HIGH_LEVEL_MESG "QueueServer_CannotUseQueue_Team_TooHighLevel"
#define QUEUE_CNU_TEAM_TOO_LOW_LEVEL_MESG "QueueServer_CannotUseQueue_Team_TooLowLevel"
#define QUEUE_CNU_TEAM_SIDEKICKING_MESG "QueueServer_CannotUseQueue_Team_Sidekicking"
#define QUEUE_CNU_TEAM_INVALID_AFFILIATION_MESG "QueueServer_CannotUseQueue_Team_AffiliationInvalid"
#define QUEUE_CNU_TEAM_AFFILIATION_REQ_MESG "QueueServer_CannotUseQueue_Team_AffiliationNeeded"
#define QUEUE_CNU_TEAM_GROUP_REQS_MESG "QueueServer_CannotUseQueue_Team_GroupRequirements"
#define QUEUE_CNU_TEAM_REQ_MESG "QueueServer_CannotUseQueue_Team_QueueRequirement"
#define QUEUE_CNU_TEAM_MISSION_MESG "QueueServer_CannotUseQueue_Team_MissionRequirement"
#define QUEUE_CNU_TEAM_OTHER_MSG "QueueServer_CannotUseQueue_Team"
#define QUEUE_CNU_TEAM_PLAYER_PENALIZED_MESG "QueueServer_PlayerPenalized"
#define QUEUE_CNU_TEAM_PLAYER_PREFS_MESG "QueueServer_PlayerNoPrefs"
#define QUEUE_CNU_TEAM_COOLDOWN_MESG "QueueServer_QueueInCooldown"
#define QUEUE_CNU_TEAM_REQUIRES_CLASS "QueueServer_CannotUseQueue_Team_ClassRequirements"
#define QUEUE_CNU_TEAM_REQUIRES_GUILD "QueueServer_CannotUseQueue_Team_RequiresGuild"
#define QUEUE_CNU_TEAM_REQUIRES_UNTEAMED "QueueServer_CannotUseQueue_Team_RequiresUnteamed"
#define QUEUE_CNU_TEAM_GEAR_RATING "QueueServer_CannotUseQueue_Team_GearRating"
#define QUEUE_CNU_TEAM_TEAM_AFFILIATION_MISMATCH "QueueServer_CannotUseQueue_Team_AffiliationMismatch"


const char* gslGetCannotUseQueueReasonMsgKey( QueueCannotUseReason eReason)
{
	switch (eReason)
	{
		case QueueCannotUseReason_LevelTooLow: return QUEUE_CNU_TOO_LOW_LEVEL_MESG; break;
		case QueueCannotUseReason_LevelTooHigh: return QUEUE_CNU_TOO_HIGH_LEVEL_MESG; break;
		case QueueCannotUseReason_SideKicking: return QUEUE_CNU_SIDEKICKING_MESG; break;
		case QueueCannotUseReason_InvalidAffiliation: return QUEUE_CNU_INVALID_AFFILIATION_MESG; break;
		case QueueCannotUseReason_AffiliationRequired: return QUEUE_CNU_AFFILIATION_REQ_MESG; break;
		case QueueCannotUseReason_GroupRequirements: return QUEUE_CNU_GROUP_REQS_MESG; break;
		case QueueCannotUseReason_Requirement: return QUEUE_CNU_REQ_MESG; break;
		case QueueCannotUseReason_MissionRequirement: return QUEUE_CNU_MISSION_MESG; break;
		case QueueCannotUseReason_LeaverPenalty: return QUEUE_CNU_PLAYER_PENALIZED_MESG; break;
		case QueueCannotUseReason_MemberPrefs: return QUEUE_CNU_PLAYER_PREFS_MESG; break;
		case QueueCannotUseReason_Cooldown: return QUEUE_CNU_COOLDOWN_MESG; break;
		case QueueCannotUseReason_ActivityRequirement: return QUEUE_CNU_ACTIVITY_NOT_ACTIVE; break;
		case QueueCannotUseReason_EventRequirement: return QUEUE_CNU_EVENT_NOT_ACTIVE; break;
		case QueueCannotUseReason_ClassRequirements: return QUEUE_CNU_REQUIRES_CLASS; break;
		case QueueCannotUseReason_RequiresGuild: return QUEUE_CNU_REQUIRES_GUILD; break;
		case QueueCannotUseReason_RequiresUnteamed: return QUEUE_CNU_REQUIRES_UNTEAMED; break;
		case QueueCannotUseReason_GearRating: return QUEUE_CNU_GEAR_RATING; break;
		case QueueCannotUseReason_MixedTeamAffiliation: return QUEUE_CNU_TEAM_AFFILIATION_MISMATCH; break;
		case QueueCannotUseReason_Other: return QUEUE_CNU_OTHER_MSG; break;
	}
	return NULL;
}

const char* gslGetTeamCannotUseQueueReasonMsgKey( QueueCannotUseReason eReason)
{
	switch (eReason)
	{
		case QueueCannotUseReason_LevelTooLow: return QUEUE_CNU_TEAM_TOO_LOW_LEVEL_MESG; break;
		case QueueCannotUseReason_LevelTooHigh: return QUEUE_CNU_TEAM_TOO_HIGH_LEVEL_MESG; break;
		case QueueCannotUseReason_SideKicking: return QUEUE_CNU_TEAM_SIDEKICKING_MESG; break;
		case QueueCannotUseReason_InvalidAffiliation: return QUEUE_CNU_TEAM_INVALID_AFFILIATION_MESG; break;
		case QueueCannotUseReason_AffiliationRequired: return QUEUE_CNU_TEAM_AFFILIATION_REQ_MESG; break;
		case QueueCannotUseReason_GroupRequirements: return QUEUE_CNU_TEAM_GROUP_REQS_MESG; break;
		case QueueCannotUseReason_Requirement: return QUEUE_CNU_TEAM_REQ_MESG; break;
		case QueueCannotUseReason_MissionRequirement: return QUEUE_CNU_TEAM_MISSION_MESG; break;
		case QueueCannotUseReason_LeaverPenalty: return QUEUE_CNU_TEAM_PLAYER_PENALIZED_MESG; break;
		case QueueCannotUseReason_MemberPrefs: return QUEUE_CNU_TEAM_PLAYER_PREFS_MESG; break;
		case QueueCannotUseReason_Cooldown: return QUEUE_CNU_TEAM_COOLDOWN_MESG; break;
		case QueueCannotUseReason_ActivityRequirement: return QUEUE_CNU_TEAM_ACTIVITY_NOT_ACTIVE; break;
		case QueueCannotUseReason_EventRequirement: return QUEUE_CNU_TEAM_EVENT_NOT_ACTIVE; break;
		case QueueCannotUseReason_ClassRequirements: return QUEUE_CNU_TEAM_REQUIRES_CLASS; break;
		case QueueCannotUseReason_RequiresGuild: return QUEUE_CNU_TEAM_REQUIRES_GUILD; break;
		case QueueCannotUseReason_GearRating: return QUEUE_CNU_TEAM_GEAR_RATING; break;
		case QueueCannotUseReason_RequiresUnteamed: return QUEUE_CNU_TEAM_REQUIRES_UNTEAMED; break;
		case QueueCannotUseReason_MixedTeamAffiliation: return QUEUE_CNU_TEAM_TEAM_AFFILIATION_MISMATCH; break;
		case QueueCannotUseReason_Other: return QUEUE_CNU_TEAM_OTHER_MSG; break;
	}
	return NULL;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////
//  Random Queues

static QueueDefRef **ppRandomActiveQueues = NULL;
static bool bRandomQueuesUpdated = false;

AUTO_COMMAND_REMOTE;
void gslQueue_UpdateRandomActiveQueues(const char *pchNewRandomQueues)
{
	char **ppchDefNames = NULL;

	if(ppRandomActiveQueues)
		eaDestroyStruct(&ppRandomActiveQueues,parse_QueueDefRef);

	DivideString(pchNewRandomQueues, ",", &ppchDefNames,
		DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE|DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS|
		DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE|DIVIDESTRING_POSTPROCESS_ESTRINGS);

	EARRAY_CONST_FOREACH_BEGIN(ppchDefNames, iCurElement, iNumElements);
	{
		QueueDefRef *pNewRef = StructCreate(parse_QueueDefRef);

		SET_HANDLE_FROM_STRING(g_hQueueDefDict,ppchDefNames[iCurElement],pNewRef->hDef);

		eaPush(&ppRandomActiveQueues,pNewRef);
	}
	EARRAY_FOREACH_END;

	bRandomQueuesUpdated = true;
}

bool gslQueue_IsRandomQueueActive(QueueDef *pDef)
{
	QueueCategoryData *pData = queue_GetCategoryData(pDef->eCategory);

	if(pData && pData->uRandomActiveCount > 0)
	{
		int i;

		for(i=0;i<eaSize(&ppRandomActiveQueues);i++)
		{
			if(GET_REF(ppRandomActiveQueues[i]->hDef) == pDef)
				return true;
		}

		return false;
	}

	return true;
}

static QueueCannotUseReason gslEntCannotUseQueue_ActiveCheck(Entity *pEnt, QueueDef *pDef)
{
	if(!gslQueue_IsRandomQueueActive(pDef))
	{
		//Check to see if the player is already in the map
		return QueueCannotUseReason_RandomClosed;
	}

	return QueueCannotUseReason_None;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////


static QueueCannotUseReason gslEntCannotUseQueue_CheckMission(Entity *pEnt, QueueDef *pDef, MissionDef *pMissionDef)
{
	if(pEnt && pEnt->pPlayer)
	{
		if(pDef->Requirements.eMissionReqFlags & kQueueMissionReq_Complete)
		{
			if(mission_GetCompletedMissionByDef(pEnt->pPlayer->missionInfo, pMissionDef) != NULL)
				return QueueCannotUseReason_None;
		}
		if(pDef->Requirements.eMissionReqFlags & kQueueMissionReq_Available)
		{
			if(mission_FindMissionFromDef(pEnt->pPlayer->missionInfo, pMissionDef) != NULL)
			{
				return QueueCannotUseReason_None;
			}
		}
		if(pDef->Requirements.eMissionReqFlags & kQueueMissionReq_CanAccept)
		{
			if (missiondef_CanBeOfferedAsPrimary(pEnt, pMissionDef, NULL, NULL))
			{
				return QueueCannotUseReason_None;
			}
		}
	}
	return QueueCannotUseReason_MissionRequirement;
}


static QueueCannotUseReason gslEntCannotUseQueue_CheckDebug(QueueDef* pDef, QueueCannotUseReason eReason)
{
	//check debug requirements
	if (eReason == QueueCannotUseReason_None && pDef && pDef->Settings.bDebug && (isProductionMode() && !g_iShowDebugQueues))
	{
		return QueueCannotUseReason_Other;
	}
	return eReason;
}

static bool gslEntCannotUseQueue_CheckCooldown(Entity* pEnt, QueueDef* pDef)
{
	if (pEnt->pPlayer && pDef->pchCooldownDef)
	{
		QueueCooldownDef* pCooldownDef = queue_CooldownDefFromName(pDef->pchCooldownDef);
		if (pCooldownDef)
		{
			PlayerQueueCooldown* pPlayerCooldown = eaIndexedGetUsingString(&pEnt->pPlayer->eaQueueCooldowns, pCooldownDef->pchName);
			if (pPlayerCooldown)
			{
				U32 uCurrentTime = timeSecondsSince2000();
				if (uCurrentTime < pPlayerCooldown->uStartTime + pCooldownDef->uCooldownTime)
				{
					return true;
				}
			}
		}
	}
	return false;
}

static bool gslEntCannotUseQueueInstance_CheckGuild(Entity* pEnt, QueueInstanceParams* pParams, QueueDef* pDef)
{
	if (pParams->uiGuildID)
	{
		ContainerID uGuildID = guild_IsMember(pEnt) ? guild_GetGuildID(pEnt) : 0;

		if (pParams->uiGuildID != uGuildID)
		{
			return true;
		}
	} 
	else if (!pParams->uiOwnerID && pDef->Requirements.bRequireAnyGuild)
	{
		if (!guild_IsMember(pEnt))
		{
			return true;
		}
	}
	return false;
}

static bool gslEntCannotUseQueue_CheckClass(Entity* pEnt, QueueDef* pDef)
{
	if (pEnt->pChar)
	{
		CharacterClass* pClass = GET_REF(pEnt->pChar->hClass);
		CharacterClass** ppClasses = NULL;
		int i;

		if (eaSize(&pDef->Requirements.ppClassesRequired) || eaiSize(&pDef->Requirements.piClassCategoriesRequired))
		{
			if (pClass)
			{
				eaPush(&ppClasses, pClass);
			}
			if (pEnt->pSaved && pEnt->pSaved->pPuppetMaster)
			{
				for (i = eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
				{
					PuppetEntity* pPuppet = pEnt->pSaved->pPuppetMaster->ppPuppets[i];

					if (pPuppet->eState == PUPPETSTATE_ACTIVE)
					{
						Entity* pPuppetEnt = GET_REF(pPuppet->hEntityRef);
						if (pPuppetEnt && pPuppetEnt->pChar)
						{
							pClass = GET_REF(pPuppetEnt->pChar->hClass);
							if (pClass)
							{
								eaPushUnique(&ppClasses, pClass);
							}
						}
					}
				}
			}
		}
		if (eaSize(&pDef->Requirements.ppClassesRequired))
		{
			for (i = eaSize(&pDef->Requirements.ppClassesRequired)-1; i >= 0; i--)
			{
				pClass = GET_REF(pDef->Requirements.ppClassesRequired[i]->hClass);

				if (eaFind(&ppClasses, pClass) >= 0)
				{
					break;
				}
			}
			if (i < 0)
			{
				eaDestroy(&ppClasses);
				return true;
			}
		}
		if (eaiSize(&pDef->Requirements.piClassCategoriesRequired))
		{
			for (i = eaSize(&ppClasses)-1; i >= 0; i--)
			{
				pClass = ppClasses[i];
				if (eaiFind(&pDef->Requirements.piClassCategoriesRequired, pClass->eCategory) >= 0)
				{
					break;
				}
			}
			if (i < 0)
			{
				eaDestroy(&ppClasses);
				return true;
			}
		}
		eaDestroy(&ppClasses);
	}
	return false;
}

static bool gslQueue_CheckActivityRequirements(Entity* pEnt, QueueDef* pDef)
{
	if (pDef->Requirements.pchRequiredActivity && pDef->Requirements.pchRequiredActivity[0])
	{
		if (!gslActivity_IsActive(pDef->Requirements.pchRequiredActivity))
		{
			return false;
		}
	}
	
	return true;
}

static bool gslQueue_CheckEventRequirements(Entity* pEnt, QueueDef* pDef)
{
	EventDef *pEventDef = EventDef_Find(pDef->Requirements.pchRequiredEvent);
	if (pEventDef)
	{	
		if (!gslEvent_IsActive(pEventDef))
		{
			return false;
		}
	}
	return true;
}

static bool gslQueue_CheckGuild(Entity* pEnt, QueueDef* pDef)
{
	if (pDef->Requirements.bRequireSameGuild)
	{
		if (!guild_IsMember(pEnt))
		{
			return false;
		}
	}
	return true;
}

static bool gslEntCannotUseQueue_CheckTeaming(Entity* pEnt, QueueDef* pDef)
{
	if (pDef->Requirements.bUnteamedQueuingOnly && team_IsMember(pEnt))
	{
		return(true);
	}
	return(false);
}

static bool gslEntCannotUseQueue_CheckGearRating(Entity* pEnt, QueueDef* pDef)
{
	if (pDef->Requirements.uRequiredGearRating > 0)
	{
		if (pEnt!=NULL && g_QueueConfig.pGearRatingCalcExpr!=NULL)
		{
			MultiVal mVal;
			ExprContext *pContext = queue_GetContext(pEnt);
			U32 uGearRating;
			exprEvaluate(g_QueueConfig.pGearRatingCalcExpr, pContext, &mVal);
			uGearRating = MultiValGetInt(&mVal,NULL);
			if (uGearRating < pDef->Requirements.uRequiredGearRating)
			{
				// We CANNOT use it. Return true
				return(true);
			}
		}
	}
	// We CAN use it. Return false
	return(false);
}


//Checks to see if the given entity can use the given queue def, returns a message key to display if they cannot
QueueCannotUseReason gslEntCannotUseQueue(Entity* pEnt, QueueDef* pDef, S32 bPreValidated, S32 bIgnoreLevelRestrictions, S32 bInActiveQueue)
{
	QueueCannotUseReason eReason = QueueCannotUseReason_None;

	PERFINFO_AUTO_START_FUNC();
	
	if(pEnt && pEnt->pChar && pDef)
	{
		MissionDef *pMissionDef = GET_REF(pDef->Requirements.hMissionRequired);
		S32 iEntLevel = entity_GetSavedExpLevel(pEnt);
		const char* pchAffiliation = queue_EntGetQueueAffiliation(pEnt);

		eReason = queue_EntCannotUseQueue(pEnt, iEntLevel, pchAffiliation, pDef, -1, bIgnoreLevelRestrictions, false);

		if(eReason == QueueCannotUseReason_None &&
			pMissionDef &&
			(!bPreValidated || pDef->Requirements.bMissionReqNoAccess))
		{
			eReason = gslEntCannotUseQueue_CheckMission(pEnt, pDef, pMissionDef);
		}

		if(eReason == QueueCannotUseReason_None && !bInActiveQueue)
		{
			eReason = gslEntCannotUseQueue_ActiveCheck(pEnt, pDef);
		}

		if (eReason == QueueCannotUseReason_None &&
			!gslQueue_CheckActivityRequirements(pEnt, pDef))
		{
			eReason = QueueCannotUseReason_ActivityRequirement;
		}

		if (eReason == QueueCannotUseReason_None &&
			!gslQueue_CheckEventRequirements(pEnt, pDef))
		{
			eReason = QueueCannotUseReason_EventRequirement;
		}

		if (eReason == QueueCannotUseReason_None &&
			!gslQueue_CheckGuild(pEnt, pDef))
		{
			eReason = QueueCannotUseReason_RequiresGuild;
		}

		// Gear rating, Teaming, Class and Cooldown requirements must be after all other reasons.
		// This is because queues with this failure reason are still sent to clients,
		// but queues with any of the above reasons should not be sent.
		if (eReason == QueueCannotUseReason_None &&
			gslEntCannotUseQueue_CheckClass(pEnt, pDef))
		{
			eReason = QueueCannotUseReason_ClassRequirements;
		}

		if (eReason == QueueCannotUseReason_None &&
			gslEntCannotUseQueue_CheckCooldown(pEnt, pDef))
		{
			eReason = QueueCannotUseReason_Cooldown;
		}

		if (eReason == QueueCannotUseReason_None &&
			gslEntCannotUseQueue_CheckTeaming(pEnt, pDef))
		{
			eReason = QueueCannotUseReason_RequiresUnteamed;
		}

		if (eReason == QueueCannotUseReason_None &&
			gslEntCannotUseQueue_CheckGearRating(pEnt, pDef))
		{
			eReason = QueueCannotUseReason_GearRating;
		}
	}

	PERFINFO_AUTO_STOP();
	
	return gslEntCannotUseQueue_CheckDebug(pDef, eReason);
}

const char *gslEntCannotUseQueueMsgKey(Entity* pEnt, QueueDef* pDef, S32 bPreValidated, S32 bIgnoreLevelRestrictions)
{
	QueueCannotUseReason eReason = gslEntCannotUseQueue(pEnt, pDef, bPreValidated, bIgnoreLevelRestrictions, false);
	return gslGetCannotUseQueueReasonMsgKey(eReason);
}

//Checks to see if the given entity can use the given queue instance, returns a message key to display if they cannot
QueueCannotUseReason gslEntCannotUseQueueInstance(Entity* pEnt, 
														 QueueInstanceParams* pParams, 
														 QueueDef* pDef, 
														 S32 bPreValidated,
														 S32 bIgnoreLevelRestrictions,
														 S32 bCheckMissionRequirements,
														 S32 bCheckCooldown)
{
	QueueCannotUseReason eReason = QueueCannotUseReason_None;
	if(pEnt && pEnt->pChar && pDef && pParams)
	{
		MissionDef *pMissionDef = GET_REF(pDef->Requirements.hMissionRequired);
		S32 iEntLevel = entity_GetSavedExpLevel(pEnt);
		const char* pchAffiliation = queue_EntGetQueueAffiliation(pEnt);
		
		eReason = queue_EntCannotUseQueueInstance(pEnt, 
												  iEntLevel, 
												  pchAffiliation, 
												  pParams, 
												  pDef,  
												  bIgnoreLevelRestrictions, false);
		
		if(eReason == QueueCannotUseReason_None &&
			bCheckMissionRequirements &&
			pMissionDef &&
			(!bPreValidated || pDef->Requirements.bMissionReqNoAccess))
		{
			eReason = gslEntCannotUseQueue_CheckMission(pEnt, pDef, pMissionDef);
		}
		if (eReason == QueueCannotUseReason_None &&
			bCheckCooldown &&
			gslEntCannotUseQueue_CheckCooldown(pEnt, pDef))
		{
			eReason = QueueCannotUseReason_Cooldown;
		}
		if (eReason == QueueCannotUseReason_None &&
			gslEntCannotUseQueueInstance_CheckGuild(pEnt, pParams, pDef))
		{
			eReason = QueueCannotUseReason_RequiresGuild;
		}
	}
	return gslEntCannotUseQueue_CheckDebug(pDef, eReason);
}

const char *gslEntCannotUseQueuePrefs(Entity *pEnt, QueueMemberPrefs *pPrefs, QueueDef *pDef)
{
	if((pPrefs == NULL) != (pDef->MapRules.pMatchMakingRules == NULL))
	{
		return gslGetCannotUseQueueReasonMsgKey(QueueCannotUseReason_MemberPrefs);
	}

	return NULL;
}

const char *gslEntCannotUseQueueInstanceMsgKey(Entity* pEnt, 
											   QueueInstanceParams* pParams, 
											   QueueDef* pDef, 
											   S32 bPreValidated,
											   S32 bIgnoreLevelRestrictions)
{
	QueueCannotUseReason eReason = gslEntCannotUseQueueInstance(pEnt,
																pParams,
																pDef,
																bPreValidated,
																bIgnoreLevelRestrictions,
																true,
																true);
	return gslGetCannotUseQueueReasonMsgKey(eReason);
}

// Checks the reason as to why the queue cannot be used and sends a notification to the entity
bool gslQueue_NotifyPlayerCannotUseReason(Entity* pEnt, QueueDef* pDef, QueueCannotUseReason eReason)
{
	if (eReason == QueueCannotUseReason_None)
		eReason = gslEntCannotUseQueue(pEnt, pDef, false, false, false);

	if (eReason != QueueCannotUseReason_None)
	{
		const char *pchMessage = gslGetCannotUseQueueReasonMsgKey(eReason);
		if (pchMessage)
		{
			gslQueue_SendWarning(pEnt->myContainerID, 0, pDef->pchName, pchMessage);
			return true;
		}
	}

	return false;
}

void gslQueue_PlayerAddQueue(Entity *pEnt, QueueDef *pDef)
{
	if(pEnt && pEnt->pPlayer)
	{
		PlayerQueue *pQueue;
		S64 iMapKey = 0;
		U32 uInstanceID = 0;

		if(!pEnt->pPlayer->pPlayerQueueInfo)
			pEnt->pPlayer->pPlayerQueueInfo = StructCreate(parse_PlayerQueueInfo);

		gslQueue_RequestPlayerQueuesProcess(pEnt);
		
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);

		//Make them a new queue
		pQueue = StructCreate(parse_PlayerQueue);
		pQueue->pchQueueName = allocAddString(pDef->pchName);
		SET_HANDLE_FROM_STRING(g_hQueueDefDict, pDef->pchName, pQueue->hDef);
		
		eaIndexedEnable(&pEnt->pPlayer->pPlayerQueueInfo->eaQueues, parse_PlayerQueue);
		if(!eaPush(&pEnt->pPlayer->pPlayerQueueInfo->eaQueues, pQueue))
		{
			StructDestroy(parse_PlayerQueue, pQueue);
		}

		// We used to distinguish Champions from STO/Neverwinter here.
		//   Champions had functionality where queue doors might not exist in
		//   a players Queue UI until they clicked on the door. But would not add the
		//   player to the queue automatically. STO and Neverwinter use the "AutoJoinQueueDoors" functionality.
		
		if (pDef->MapSettings.eMapType == ZMTYPE_QUEUED_PVE)
		{
			// For PVE maps, attempt to find a map that has a guildmate, teammate or friend.
			// If we actually get an instance, we will immediately be invited to it.
			//
			iMapKey = gslQueue_FindBestMapToJoin(pEnt, pDef->pchName, &uInstanceID);
		}

		if (g_QueueConfig.bEnableStrictTeamRules && team_IsMember(pEnt))
		{
			gslQueue_TeamJoin(pEnt, pDef->pchName, uInstanceID, iMapKey, false);
		}
		else
		{
			gslQueue_Join(pEnt, pDef->pchName, NULL, 0, uInstanceID, iMapKey, false, !g_QueueConfig.bAutoJoinQueueRequiresPlayerAccept, NULL);
		}
	}
}

bool gslQueue_EntIDIsInMatchGroup(QueuePartitionInfo *pInfo, int iEntID)
{
	if(pInfo->pMatch)
	{
		S32 iGroupIndex;
		S32 bFound = false;
		for(iGroupIndex = 0; iGroupIndex < eaSize(&pInfo->pMatch->eaGroups); iGroupIndex++)
		{
			QueueGroup *pGroup = eaGet(&pInfo->pMatch->eaGroups, iGroupIndex);

			if (pGroup)
			{
				if (queue_Match_FindMemberInGroup(pGroup, iEntID) >= 0)
				{
					return(true);
				}
			}
		}
		return(false);
	}
	// Special case. We are considered OKAY matchgroup wise if there is no match
	return(true);
}

void gslQueue_SetupDefaultMatch(QueuePartitionInfo *pInfo)
{
	QueueMatch *pMatch = NULL;
	QueueDef *pDef = queue_DefFromName(SAFE_MEMBER(pInfo->pGameInfo, pchQueueDef));

	if(pDef && !pInfo->pMatch)
	{
		ANALYSIS_ASSUME(pDef != NULL);
		pMatch = StructCreate(parse_QueueMatch);
		pInfo->pMatch = pMatch;

		queue_InitMatchGroups(pDef,pMatch);

		qDebugPrintf("Default Queue Setup (%s)\n",pInfo->pGameInfo->pchQueueDef);
	}
	
}

static void gslQueue_setupTrackedEvents(QueuePartitionInfo *pInfo)
{
	int i;
	QueueDef *pDef = queue_DefFromName(SAFE_MEMBER(pInfo->pGameInfo,pchQueueDef));

	if(pDef)
	{
		for(i=0;i<eaSize(&pDef->ppTrackedEvents);i++)
		{
			QueueTrackedEvent *pTrackedEvent = pDef->ppTrackedEvents[i];
			GameEvent* pEvent = NULL;
			bool result;

			if (!pTrackedEvent->pchMapValue) {
				Errorf("Missing value name for tracked event %d",i);
				continue;
			}

			if(pTrackedEvent->pchEventString)
			{
				pEvent = gameevent_EventFromString(pTrackedEvent->pchEventString);

				if (!pEvent) {
					Errorf("Couldn't create event from string %s", pTrackedEvent->pchEventString);
					continue;
				}
			}

			result = mapState_AddPrototypePlayerValue(pInfo->iPartitionIdx, pTrackedEvent->pchMapValue, 0, pEvent);

			// Free the allocated event
			if (pEvent) {
				StructDestroy(parse_GameEvent, pEvent);
			}
		}
	}
}


// pQueueGameInfo is what came from the QueueServer if it was what created this map.
//   If this isn't a queue-started map, or the queue server is down, or something, pQueueGameInfo will
//   be NULL
// We used to use a MapVar to transfer the data. We do not anymore
static void gslQueue_InitGameData(QueuePartitionInfo *pInfo, QueueGameInfo* pQueueGameInfo)
{
	if (!pInfo->pGameInfo)
	{
		QueueDef* pDef = NULL;
		bool bNonQueueStartedMap = false;

		pInfo->pGameInfo = StructCreate(parse_QueueGameInfo);

		if (pQueueGameInfo!=NULL)
		{
			// Source -> Dest
			StructCopyAll(parse_QueueGameInfo, pQueueGameInfo, pInfo->pGameInfo);
			pDef = queue_DefFromName(pInfo->pGameInfo->pchQueueDef);
		}

		if(!pDef)
		{
			// Try to get the queue info directly off the map as backup default data
			int iPvPGameType = StaticDefineIntGetInt(PVPGameTypeEnum,zmapInfoGetDefaultPVPGameType(NULL));
			if(iPvPGameType != -1)
			{
				pInfo->pGameInfo->ePvPGameType = iPvPGameType;
			}
			pInfo->pGameInfo->pchQueueDef = allocAddString(zmapInfoGetDefaultQueueDef(NULL));
			pDef = queue_DefFromName(pInfo->pGameInfo->pchQueueDef);
			bNonQueueStartedMap = true;
			pInfo->bAllowNonGroupedEntities=true;		// Manually started map (map move or such) do not boot ungrouped entities
		}
		
		if (!pDef)
		{
			// Error if a QueueDef wasn't found
			
			GlobalType eOwnerType = partition_OwnerTypeFromIdx(pInfo->iPartitionIdx);

			if (eOwnerType == GLOBALTYPE_QUEUESERVER)
			{
				ErrorDetailsf("QueueDef %s", pInfo->pGameInfo->pchQueueDef);
				Errorf("gslQueue_InitGameData: Couldn't find a valid QueueDef!");
			}
			
			if(zmapInfoGetMapType(NULL) == ZMTYPE_QUEUED_PVE)
			{
				Errorf("No QueueDef (either zonemap variable Queuedef or a default queue in zonemap) for qpve map %s. This will cause many errors including all AL 0 players to be booted from the map.", zmapInfoGetCurrentName(NULL));
			}
		}
		else
		{
			//Set the bolster level
			if (pDef->MapSettings.bBolsterToMapLevel)
			{
				QueueLevelBand* pLevelBand = eaGet(&pDef->eaLevelBands, pInfo->pGameInfo->iLevelBandIndex);
				S32 iMapLevel = mechanics_GetMapLevel(pInfo->iPartitionIdx);
				S32 iBolsterLevel = iMapLevel;
				
				if (pLevelBand)
				{
					if (pLevelBand->iMinLevel)
						MAX1(iBolsterLevel, pLevelBand->iMinLevel);
					if (pLevelBand->iMaxLevel)
						MIN1(iBolsterLevel, pLevelBand->iMaxLevel);
				}
				pInfo->pGameInfo->iBolsterLevel = iBolsterLevel;
			}

			// If a map difficulty is specified on the QueueDef, cache it
			if (pDef->MapSettings.iMapDifficulty)
			{
				pInfo->iQueueMapDifficulty = pDef->MapSettings.iMapDifficulty;
			}

			gslQueue_setupTrackedEvents(pInfo);
			mapState_SetBolsterLevel(pInfo->iPartitionIdx, pInfo->pGameInfo->eBolsterType, pInfo->pGameInfo->iBolsterLevel);

			// For a PvP map or for TeamUps, if we are not coming in from a queueServer map, set up a default empty match. 
			if (pInfo->pGameInfo->ePvPGameType!=kPVPGameType_None || pDef->MapRules.bEnableTeamUp)
			{
				if (bNonQueueStartedMap)
				{
					// We need a default match for a PvP map because devs sometimes start up a map and then manually create groups through CMD PlaceInGroup (gslQueue_cmd.c)
					// Teamups need one because we directly set info on them
					gslQueue_SetupDefaultMatch(pInfo);
				}

				if (pInfo->pGameInfo->ePvPGameType!=kPVPGameType_None)
				{
					gslPVPGame_CreateGameData(pInfo, pInfo->pGameInfo->ePvPGameType);
				}

				if(pDef->MapRules.bEnableTeamUp)
				{
					int i;
					char *estrName = NULL;
		
					estrCreate(&estrName);
		
					for(i=0;i<eaSize(&pDef->eaGroupDefs);i++)
					{
						TeamUpInstance *pInstance = NULL;
		
						estrPrintf(&estrName,"%s_%d",pDef->pchName,i);
		
						pInstance = gslTeamUp_CreateNewInstance(pInfo->iPartitionIdx,estrName,&pDef->eaGroupDefs[i]->DisplayName,kTeamUpFlags_Locked);
						pInfo->pMatch->eaGroups[i]->uiTeamUpID = pInstance->iTeamKey;
		
						estrClear(&estrName);
					}
		
					estrDestroy(&estrName);
				}
			}
		}
		gslQueue_LocalTeamUpdate(pInfo);
	}
}


////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
//
// Rather than rely on Map Variable passing, let's request from the QueueServer the info that
//   used to be passed through "QueueDef" game variables. If we are a non-queue started map
//   we will not find an instance on the aslQueueServer side and we can resolve accordingly.

typedef struct QueueGameInfoCBStruct
{
	int iPartitionID;
	
} QueueGameInfoCBStruct;


// Receive QueueInstanceInfo from the server.
void gslQueue_RequestQueueGameInfo_CB(TransactionReturnVal *pReturn, QueueGameInfoCBStruct *pData)
{
	QueueGameInfo* pQueueGameInfo=NULL;
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(partition_IdxFromID(pData->iPartitionID));
	enumTransactionOutcome eOutcome = RemoteCommandCheck_aslQueue_RequestQueueGameInfo(pReturn, &pQueueGameInfo);
	
	if (pInfo!=NULL)
	{
		if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			// if pQueueGameInfo is not NULL we were started by the Queue server.
			// If it is NULL we were not queue-server-started (or at least we could not find a matching map/instance on the QueueServer
			gslQueue_InitGameData(pInfo, pQueueGameInfo);
		}
		else
		{
			// We're not queue-started
			gslQueue_InitGameData(pInfo, NULL);
		}
		pInfo->bQueueServerInstanceInfoReceived=true;
	}

	if (pQueueGameInfo!=NULL)
	{
		StructDestroy( parse_QueueGameInfo, pQueueGameInfo);
	}
	SAFE_FREE(pData); // Our callback data
}

static void gslQueue_RequestServerInstanceInfo(QueuePartitionInfo* pInfo)
{
	U32 uMapID = gGSLState.gameServerDescription.baseMapDescription.containerID;
	U32 uPartitionID = partition_IDFromIdx(pInfo->iPartitionIdx);
	S64 iMapKey = queue_GetMapKey(uMapID, uPartitionID);

	QueueGameInfoCBStruct* pData = NULL;
	TransactionReturnVal* pReturn;
				
	pData = malloc(sizeof(QueueGameInfoCBStruct));
	pData->iPartitionID = partition_IDFromIdx(pInfo->iPartitionIdx);

	pReturn = objCreateManagedReturnVal(gslQueue_RequestQueueGameInfo_CB, pData);
	RemoteCommand_aslQueue_RequestQueueGameInfo(pReturn, GLOBALTYPE_QUEUESERVER, 0, iMapKey);
	
	pInfo->bQueueServerInstanceInfoRequested=true;
}

////////////////////////////////////////////////////////////////////////////////////////



void gslQueue_ClearQueueList(void)
{
	PERFINFO_AUTO_START_FUNC();
	
	//If it's not null
	if(g_pQueueList)
	{
		int iQueueIdx, iNumQueues = eaSize(&g_pQueueList->eaQueueRefs);
		for(iQueueIdx = 0; iQueueIdx < iNumQueues; iQueueIdx++)
		{
			//Remove all the subscription handles
			REMOVE_HANDLE(g_pQueueList->eaQueueRefs[iQueueIdx]->hDef);
		}

		//Destroy the list and null it out
		StructDestroySafe(parse_QueueList, &g_pQueueList);
		
		//Reset the requested flag just in-case
		s_uiQueueListNextRequestTime = 0;
	}
	
	PERFINFO_AUTO_STOP();
}

#define QUEUE_REQUEST_LIST_TIMEOUT 60
static void gslQueue_FillQueueList(U32 uiCurrentTime)
{
	U32 uiServerID = gGSLState.gameServerDescription.baseMapDescription.containerID;
	int i;

	for(i = 0; i < partition_GetCurNumPartitionsCeiling(); ++i) 
	{		
		if (!partition_ExistsByIdx(i)) {
			continue;
		}

		if (mapState_ArePVPQueuesDisabled(mapState_FromPartitionIdx(i)))
			return;
		
		//Set the "I'm requesting them" flag
		s_uiQueueListNextRequestTime = uiCurrentTime + QUEUE_REQUEST_LIST_TIMEOUT;
		
		//Request the queues
		RemoteCommand_aslQueue_GetQueueList(GLOBALTYPE_QUEUESERVER, 0, uiServerID);

		return;
	}
}

static bool gslQueue_FindFriendByID(ChatState* pChatState, ContainerID uEntID)
{
	if (pChatState)
	{
		S32 i;
		for (i = eaSize(&pChatState->eaFriends)-1; i >= 0; i--)
		{
			ChatPlayerStruct* pPlayer = pChatState->eaFriends[i];
			if (pPlayer->pPlayerInfo.onlineCharacterID == uEntID)
			{
				return true;
			}
		}
	}
	return false;
}

// Finds a map in the specified queue with a guildmate, teammate, or friend - or returns 0 on failure
// Returns the map key, which is a combination of the map ID and the partition ID
// Called specifically when spawnpoint_MovePlayerToMapAndSpawnEx is called with a queueName
//   This is essentially just using doors as an interface to the queue system rather than the usual UI. 
// Also from gslQueue_TeamJoinBestMap and gslQueue_JoinBestMap. Both AccessLevel 0 commands
S64 gslQueue_FindBestMapToJoin(Entity* pEnt, const char* pchQueueName, U32* puInstanceID)
{
	S32 iQueueIdx, iInstanceIdx, iMemberIdx;
	Guild* pGuild = guild_IsMember(pEnt) ? guild_GetGuild(pEnt) : NULL;
	Team* pTeam = team_GetTeam(pEnt);
	ChatState* pChatState = SAFE_MEMBER3(pEnt, pPlayer, pUI, pChatState);
	U32 uGuildSize = pGuild ? eaSize(&pGuild->eaMembers) : 0;
	U32 uTeamSize = team_NumPresentMembers(pTeam);
	U32 uFriendsListSize = pChatState ? eaSize(&pChatState->eaFriends) : 0;
	U32 uFriendInstanceID = 0;
	S64 iFriendMapKey = 0;

	if (!g_pQueueList || (uGuildSize <= 1 && uTeamSize <= 1 && uFriendsListSize==0))
	{
		return 0;
	}
	for (iQueueIdx = eaSize(&g_pQueueList->eaQueueRefs)-1; iQueueIdx >= 0; iQueueIdx--)
	{
		QueueRef* pRef = g_pQueueList->eaQueueRefs[iQueueIdx];
		QueueInfo* pQueue = GET_REF(pRef->hDef);
		QueueDef* pDef = pQueue ? GET_REF(pQueue->hDef) : NULL;
		
		if (pDef && stricmp(pDef->pchName, pchQueueName)==0)
		{
			for (iInstanceIdx = eaSize(&pQueue->eaInstances)-1; iInstanceIdx >= 0; iInstanceIdx--)
			{
				QueueInstance* pInstance = pQueue->eaInstances[iInstanceIdx];
				if (!(*puInstanceID) || pInstance->uiID == (*puInstanceID))
				{
					if (gslEntCannotUseQueueInstanceMsgKey(pEnt, pInstance->pParams, pDef, false, false))
					{
						continue;
					}
					for (iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
					{
						QueueMatch QMatch = {0};
						QueueMember* pMember = pInstance->eaUnorderedMembers[iMemberIdx];
						QueueMap* pMap = eaIndexedGetUsingInt(&pInstance->eaMaps,pMember->iMapKey);
						U32 uMatchSize;

						// Don't bother if the map has passed its JoinTimeLimit
						if (!pMap || !queue_MapAcceptingNewMembers(pMap, pDef))
							continue;

						queue_GroupCache(pInstance, pDef, pMap, &QMatch);
						uMatchSize = QMatch.iMatchSize;
						StructDeInit(parse_QueueMatch, &QMatch);

						if (uMatchSize >= queue_QueueGetMaxPlayers(pDef, pInstance->bOvertime))
							continue;

						if (guild_FindMemberInGuildEntID(pMember->iEntID, pGuild) || 
							team_FindMemberID(pTeam, pMember->iEntID))
						{
							(*puInstanceID) = pInstance->uiID;
							return pMember->iMapKey;
						}
						else if (!iFriendMapKey && gslQueue_FindFriendByID(pChatState, pMember->iEntID))
						{
							iFriendMapKey = pMember->iMapKey;
							uFriendInstanceID = pInstance->uiID;
						}
					}
				}
			}
			break;
		}
	}
	if (iFriendMapKey)
	{
		(*puInstanceID) = uFriendInstanceID;
		return iFriendMapKey;
	}
	return 0;
}

static PlayerQueue* gslQueue_AddPlayerQueue(Entity* pEnt, QueueInfo *pQueueInfo)
{
	PlayerQueue *pQueue = StructCreate(parse_PlayerQueue);
	pQueue->pchQueueName = allocAddString(pQueueInfo->pchName);
	COPY_HANDLE(pQueue->hDef, pQueueInfo->hDef);
	eaIndexedEnable(&pEnt->pPlayer->pPlayerQueueInfo->eaQueues, parse_PlayerQueue);
	devassert(eaPush(&pEnt->pPlayer->pPlayerQueueInfo->eaQueues, pQueue));
	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	return pQueue;
}

static void gslQueue_AddPlayerQueueMember(PlayerQueueInstance* pPlayerQInstance, 
										  QueueMember* pCurrMember,
										  ContainerID iIgnoreEntID, 
										  bool bDontSetEntRef)
{
	S32 iMyMemberIdx;
	PlayerQueueMember* pPlayerQMember = NULL;
	for (iMyMemberIdx = eaSize(&pPlayerQInstance->eaMembers)-1; iMyMemberIdx >= 0; iMyMemberIdx--)
	{
		if (pPlayerQInstance->eaMembers[iMyMemberIdx]->uiID == pCurrMember->iEntID)
		{
			pPlayerQMember = pPlayerQInstance->eaMembers[iMyMemberIdx];
			break;
		}
	}
	if (pPlayerQMember==NULL)
	{
		pPlayerQMember = StructCreate(parse_PlayerQueueMember);
		pPlayerQMember->uiID = pCurrMember->iEntID;
		if (!bDontSetEntRef && pPlayerQMember->uiID != iIgnoreEntID)
		{
			char idBuf[128];
			SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER),
				ContainerIDToString(pPlayerQMember->uiID, idBuf), pPlayerQMember->hEntity);
		}
		eaPush(&pPlayerQInstance->eaMembers, pPlayerQMember);
	}
	pPlayerQMember->uiTeamID = pCurrMember->iTeamID;
	pPlayerQMember->iMapKey = pCurrMember->iMapKey;
	pPlayerQMember->iJoinMapKey = pCurrMember->iJoinMapKey;
	pPlayerQMember->iGroupIndex = pCurrMember->iGroupIndex;
	pPlayerQMember->eState = pCurrMember->eState;
	pPlayerQMember->iLevel = pCurrMember->iLevel;
	pPlayerQMember->iRank = pCurrMember->iRank;
	
	pPlayerQMember->bDirty = true;
}

static void gslQueue_UpdatePlayerQueueMembers(PlayerQueueInstance* pPlayerQInstance, 
											  QueueInstance* pInstance,
											  QueueMember* pMember)
{
	S32 iMemberIdx;
	bool bOwnedQueue = (pPlayerQInstance->pParams->uiOwnerID > 0);

	for (iMemberIdx = eaSize(&pPlayerQInstance->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
	{
		pPlayerQInstance->eaMembers[iMemberIdx]->bDirty = false;
	}

	// If it is not an owned queue, only send the members that are part of the same map
	if (bOwnedQueue || (pPlayerQInstance->iOfferedMapKey && g_QueueConfig.bAlwaysSendPlayerMemberInfo))
	{
		if (pMember->eState == PlayerQueueState_InQueue ||	
			pMember->eState == PlayerQueueState_Offered ||	
			pMember->eState == PlayerQueueState_Accepted ||
			pMember->eState == PlayerQueueState_Countdown)
		{
			for (iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
			{
				QueueMember* pCurrMember = pInstance->eaUnorderedMembers[iMemberIdx];

				if (bOwnedQueue || (pPlayerQInstance->iOfferedMapKey == pCurrMember->iMapKey))
				{
					gslQueue_AddPlayerQueueMember(pPlayerQInstance, pCurrMember, pMember->iEntID, !bOwnedQueue);
				}
			}
		}
		else if (pMember->eState == PlayerQueueState_Invited)
		{
			for (iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
			{
				QueueMember* pCurrMember = pInstance->eaUnorderedMembers[iMemberIdx];
				if (pCurrMember->iEntID == SAFE_MEMBER2(pInstance, pParams, uiOwnerID))
				{
					gslQueue_AddPlayerQueueMember(pPlayerQInstance, pCurrMember, pMember->iEntID, !bOwnedQueue);
					break;
				}
			}
		}
	}
	
	for (iMemberIdx = eaSize(&pPlayerQInstance->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
	{
		if (!pPlayerQInstance->eaMembers[iMemberIdx]->bDirty)
		{
			StructDestroy(parse_PlayerQueueMember, eaRemove(&pPlayerQInstance->eaMembers, iMemberIdx));
		}
	}
}


static void gslQueue_UpdatePlayerQueueInstance(Entity* pEnt, PlayerQueue* pQueue, QueueInfo* pQueueInfo, QueueInstance* pInstance, QueueMember* pMember)
{
	QueueDef* pDef = GET_REF(pQueueInfo->hDef);
	U32 iCurrentTime = timeSecondsSince2000();
	PlayerQueueInstance* pPlayerQInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, pInstance->uiID);
	S32 i, iMapIdx, iMemberIdx;

	if (pDef==NULL)
	{
		return;
	}

	if (pPlayerQInstance==NULL)
	{
		pPlayerQInstance = StructCreate(parse_PlayerQueueInstance);
		pPlayerQInstance->uiID = pInstance->uiID;

		// Don't push unless we just created, otherwise we would have found our PlayerQueueInstance when we looked for it at init
		eaIndexedEnable(&pQueue->eaInstances, parse_PlayerQueueInstance);
		eaPush(&pQueue->eaInstances, pPlayerQInstance);
	}
	
	if (pPlayerQInstance->pParams==NULL)
	{
		pPlayerQInstance->pParams = StructCreate(parse_QueueInstanceParams);
	}
	//Copy params and game settings
	StructCopyAll(parse_QueueInstanceParams,pInstance->pParams,pPlayerQInstance->pParams);
	if (SAFE_MEMBER2(pInstance, pParams, uiOwnerID) > 0)
		{
		eaCopyStructs(&pInstance->eaSettings, &pPlayerQInstance->eaSettings, parse_QueueGameSetting);
	}
	
	pPlayerQInstance->uAverageWaitTime = pInstance->iAverageWaitTime;
	pPlayerQInstance->bOvertime = pInstance->bOvertime;
	pPlayerQInstance->bHasPassword = (pInstance->pParams && pInstance->pParams->pchPassword);
	pPlayerQInstance->uiOrigOwnerID = queue_GetOriginalOwnerID(pInstance);
	
	if (pMember)
	{
		pPlayerQInstance->bIgnoreLevelRestrictions = !!(pMember->eJoinFlags & kQueueJoinFlags_IgnoreLevelRestrictions);
		pPlayerQInstance->bNewMapLoading = pInstance->bNewMap;
		pPlayerQInstance->eQueueState = pMember->eState;
		
		// Update the time remaining in the player's current state
		switch (pPlayerQInstance->eQueueState)
		{
			xcase PlayerQueueState_Offered:
			{
				U32 uiTimeout = g_QueueConfig.uCheckOffersResponseTimeout + pMember->iStateEnteredTime;
				
				pPlayerQInstance->uTimelimit = g_QueueConfig.uCheckOffersResponseTimeout;

				if(g_QueueConfig.uAutoAcceptTime)
				{
					uiTimeout = g_QueueConfig.uAutoAcceptTime + pMember->iStateEnteredTime;
					pPlayerQInstance->uTimelimit = g_QueueConfig.uAutoAcceptTime;
				}

				
				if (uiTimeout > iCurrentTime)
				{
					pPlayerQInstance->uSecondsRemaining = uiTimeout - iCurrentTime;
				}
				else
				{
					pPlayerQInstance->uSecondsRemaining = 0;
				}
			}
			xcase PlayerQueueState_Accepted:
			{
				pPlayerQInstance->uSecondsRemaining = 0;
				if (queue_InstanceShouldCheckOffers(pDef, pPlayerQInstance->pParams))
				{
					for (i = eaSize(&pInstance->eaMaps)-1; i >= 0; i--)
					{
						QueueMap* pMap = pInstance->eaMaps[i];
						if (pMap->iMapKey == pMember->iMapKey)
						{
							if (pMap->eMapState == kQueueMapState_LaunchPending)
							{
								U32 uiTimeout = g_QueueConfig.uMemberResponseTimeout + pMap->iStateEnteredTime;
								pPlayerQInstance->uTimelimit = g_QueueConfig.uMemberResponseTimeout;
								if (uiTimeout > iCurrentTime)
								{
									pPlayerQInstance->uSecondsRemaining = uiTimeout - iCurrentTime;
								}
								break;
							}
						}
					}
				}
			}
			xcase PlayerQueueState_Countdown:
			{
				U32 uiTimeout = g_QueueConfig.uMapLaunchCountdown + pMember->iStateEnteredTime;
				
				pPlayerQInstance->uTimelimit = g_QueueConfig.uMapLaunchCountdown;

				if (uiTimeout > iCurrentTime)
				{
					pPlayerQInstance->uSecondsRemaining = uiTimeout - iCurrentTime;
				}
				else
				{
					pPlayerQInstance->uSecondsRemaining = 0;
				}
			}
			xcase PlayerQueueState_Delaying:
			{
				U32 uiTimeout = g_QueueConfig.uMemberDelayTimeout + pMember->iStateEnteredTime;
				pPlayerQInstance->uTimelimit = g_QueueConfig.uMemberDelayTimeout;
				if (uiTimeout > iCurrentTime)
				{
					pPlayerQInstance->uSecondsRemaining = uiTimeout - iCurrentTime;
				}
				else
				{
					pPlayerQInstance->uSecondsRemaining = 0;
				}
			}
			xcase PlayerQueueState_Invited:
			{
				U32 uiTimeout = g_QueueConfig.uMemberInviteTimeout + pMember->iStateEnteredTime;
				pPlayerQInstance->uTimelimit = g_QueueConfig.uMemberInviteTimeout;
				if (uiTimeout > iCurrentTime)
				{
					pPlayerQInstance->uSecondsRemaining = uiTimeout - iCurrentTime;
				}
				else
				{
					pPlayerQInstance->uSecondsRemaining = 0;
				}
			}
			xdefault:
			{
				pPlayerQInstance->uTimelimit = 1;
				pPlayerQInstance->uSecondsRemaining = 0;
			}
		}

		//Set their invite id/offered map/groups
		pPlayerQInstance->iGroupIndex = pMember->iGroupIndex;

		if (eaIndexedGetUsingInt(&pInstance->eaMaps, pMember->iMapKey))
		{
			pPlayerQInstance->iOfferedMapKey = pMember->iMapKey;
		}
		else
		{
			pPlayerQInstance->iOfferedMapKey = 0;
		}

		gslQueue_UpdatePlayerQueueMembers(pPlayerQInstance, pInstance, pMember);
	}
	else
	{
		pPlayerQInstance->bIgnoreLevelRestrictions = false;
		pPlayerQInstance->bNewMapLoading = false;
		pPlayerQInstance->eQueueState = PlayerQueueState_None;
		pPlayerQInstance->uSecondsRemaining = 0;
		pPlayerQInstance->iGroupIndex = -1;
		pPlayerQInstance->iOfferedMapKey = 0;
	}

	//Update map information
	if (g_QueueConfig.bProvideIndividualMapInfo)
	{
		for (i = ea32Size(&pPlayerQInstance->piGroupPlayerCounts)-1; i >= 0; i--)
		{
			pPlayerQInstance->piGroupPlayerCounts[i] = 0;
		}

		//Update waiting player counts
		for (iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
		{
			QueueMember* pCurrMember = pInstance->eaUnorderedMembers[iMemberIdx];
			if (pCurrMember->eState == PlayerQueueState_InQueue ||
				pCurrMember->eState == PlayerQueueState_Offered || 
				pCurrMember->eState == PlayerQueueState_Accepted)
			{
				// WOLF[14Feb13]. I'm a little suspicious of this counting code where we suddenly do an affiliation check. If the affiliation/requirement check
				//   becomes more complex this will return erroneous info. But NW is not going to use the player counts anyway? Maybe nobody uses these counts?
				for (i = ea32Size(&pPlayerQInstance->piGroupPlayerCounts)-1; i >= 0; i--)
				{
					QueueGroupDef* pGroupDef = eaGet(&pDef->eaGroupDefs, i);
					if (pGroupDef && (!pGroupDef->pchAffiliation || pGroupDef->pchAffiliation == pCurrMember->pchAffiliation))
					{
						pPlayerQInstance->piGroupPlayerCounts[i]++;
						break;
					}
				}
				if (i < 0)
				{
					for (i = 0; i < eaSize(&pDef->eaGroupDefs); i++)
					{
						if (!pDef->eaGroupDefs[i]->pchAffiliation || pDef->eaGroupDefs[i]->pchAffiliation == pCurrMember->pchAffiliation)
						{
							ea32Set(&pPlayerQInstance->piGroupPlayerCounts, 1, i);
						}
					}
				}
			}
		}

		//Update group counts and map information
		if (pMember ||
			pPlayerQInstance->pParams->uiOwnerID == 0 || 
			pPlayerQInstance->pParams->pchPrivateName)
		{
			for (iMapIdx = eaSize(&pPlayerQInstance->eaPlayerQueueMaps)-1; iMapIdx >= 0; iMapIdx--)
			{
				pPlayerQInstance->eaPlayerQueueMaps[iMapIdx]->bDirty = false;
			}
			for (iMapIdx = 0; iMapIdx < eaSize(&pInstance->eaMaps); iMapIdx++)
			{
				QueueMap* pMap = pInstance->eaMaps[iMapIdx];
				PlayerQueueMap* pPlayerQMap;

				if (!pMap || !queue_MapAcceptingNewMembers(pMap, pDef))
				{
					continue;
				}

				pPlayerQMap = eaIndexedGetUsingInt(&pPlayerQInstance->eaPlayerQueueMaps, pMap->iMapKey);

				if (pPlayerQMap==NULL)
				{
					pPlayerQMap = StructCreate(parse_PlayerQueueMap);
					pPlayerQMap->iKey = pMap->iMapKey;
				}
				pPlayerQMap->eMapState = pMap->eMapState;
				pPlayerQMap->bDirty = true;
				pPlayerQMap->uMapLaunchTime = pMap->iMapLaunchTime;

				ea32Clear(&pPlayerQMap->piGroupPlayerCounts);

				for (iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
				{
					QueueMember* pCurrMember = pInstance->eaUnorderedMembers[iMemberIdx];
					if (pCurrMember->eState == PlayerQueueState_InMap && pCurrMember->iMapKey == pMap->iMapKey)
					{
						U32 uiCount = ea32Get(&pPlayerQMap->piGroupPlayerCounts, pCurrMember->iGroupIndex);
						ea32Set(&pPlayerQMap->piGroupPlayerCounts, uiCount+1, pCurrMember->iGroupIndex);
					}
				}
				eaIndexedEnable(&pPlayerQInstance->eaPlayerQueueMaps, parse_PlayerQueueMap);
				eaPush(&pPlayerQInstance->eaPlayerQueueMaps, pPlayerQMap);
			}
			for (iMapIdx = eaSize(&pPlayerQInstance->eaPlayerQueueMaps)-1; iMapIdx >= 0; iMapIdx--)
			{
				if (!pPlayerQInstance->eaPlayerQueueMaps[iMapIdx]->bDirty)
				{
					StructDestroy(parse_PlayerQueueMap, eaRemove(&pPlayerQInstance->eaPlayerQueueMaps, iMapIdx));
				}
			}
		}
	}

	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
}

static bool gslQueue_IsValidPlayerInstance(Entity* pEnt, QueueInstance* pInstance)
{
	// WOLF[22Mar13] For some reason we used to only check validity on non-private maps. Let's always check
	//   so we don't keep instances hanging around. see comment near gsl_UpdatePlayerQueueInstance in ProcessPlayerQueues as well.
	if (pInstance->pParams->pchPrivateName)
	{
		return true;
	}
	else
	{
		// Are they in the list of unordered members
		QueueMember* pMember = queue_FindPlayerInInstance(pInstance, entGetContainerID(pEnt));

		if (pMember)
		{
			return true;
		}
	}
	return false;
}

static void gslQueue_CheckPlayerForceLeaveInstance(Entity* pEnt, QueueDef* pDef, PlayerQueue* pQueue, PlayerQueueInstance* pPlayerQInstance)
{
	if (g_pQueueList && pEnt && pDef && pPlayerQInstance)
	{
		// Don't leave if we are already on map (or about to be on map). These are the same checks as in aslQueue_LeaveForced, which is what used to keep this working
		
		// Perhaps we could just use a pEnt->pPlayer->pPlayerQueueInfo check instead. That indicates the player is associated with a map.
		//  That would not deal with Accepted state though. Let's keep it as it has always been for now.
		// Amendment: Do not ForceLeave if the state is Limbo. We can get in that state by Server instability, etc. and we don't want to kick over-level people.
		PlayerQueueState eQueueState = pPlayerQInstance->eQueueState;
		if (eQueueState == PlayerQueueState_InQueue ||
				 eQueueState == PlayerQueueState_Invited ||
				 eQueueState == PlayerQueueState_Offered ||
				 eQueueState == PlayerQueueState_Countdown ||
				 eQueueState == PlayerQueueState_Delaying)
		{
			U32 uCurrentTime = timeSecondsSince2000();
			U32 uiInstanceID = pPlayerQInstance->uiID;
			S32 iQueueIdx;
			for (iQueueIdx = eaSize(&g_pQueueList->eaQueueRefs)-1; iQueueIdx >= 0; iQueueIdx--)
			{
				QueueInfo* pQueueInfo = GET_REF(g_pQueueList->eaQueueRefs[iQueueIdx]->hDef);
				if (pQueueInfo && GET_REF(pQueueInfo->hDef) == pDef)
				{
					QueueInstance* pInstance = eaIndexedGetUsingInt(&pQueueInfo->eaInstances, uiInstanceID);
					if (pInstance)
					{
						S32 iIdx = eaIndexedFindUsingInt(&pInstance->eaForceLeaves, pEnt->myContainerID);
						if (iIdx < 0)
						{
							QueueMemberForceLeave* pForceLeave = StructCreate(parse_QueueMemberForceLeave);
							pForceLeave->uEntID = pEnt->myContainerID;
							pForceLeave->uTimeoutTime = uCurrentTime + QUEUE_MEMBER_FORCE_LEAVE_TIMEOUT;
							eaPush(&pInstance->eaForceLeaves, pForceLeave);
							RemoteCommand_aslQueue_LeaveForced(GLOBALTYPE_QUEUESERVER, 
															   0, 
															   pEnt->myContainerID,
															   pDef->pchName,
															   pPlayerQInstance->uiID);
						}
					}
				}
			}

			eaFindAndRemove(&pQueue->eaInstances,pPlayerQInstance);
			StructDestroy(parse_PlayerQueueInstance, pPlayerQInstance);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);		// A little unsafe but we must have a pPlayer if we got the pMyQueue off of it
		}
	}
}

static void gslQueue_CleanupPlayerQueueInstance(Entity* pEnt, PlayerQueue* pMyQueue, S32 iMyInstIdx, QueueDef* pDef)
{
	PlayerQueueInstance* pMyInstance = pMyQueue->eaInstances[iMyInstIdx];
	bool bIgnoreLevelRestrictions = pMyInstance->bIgnoreLevelRestrictions;
	QueueCannotUseReason eReason = gslEntCannotUseQueueInstance(pEnt,
																pMyInstance->pParams,
																pDef,
																false,
																bIgnoreLevelRestrictions,
																false,
																false);
	if(eReason != QueueCannotUseReason_None)
	{
		// We may need to leave the QueueInstance if we have not yet gotten to the map.
		gslQueue_CheckPlayerForceLeaveInstance(pEnt, pDef, pMyQueue, pMyInstance);
	}
	else
	{
		S32 iQIdx, iNumQueues = g_pQueueList ? eaSize(&g_pQueueList->eaQueueRefs) : 0;

		for (iQIdx = 0; iQIdx < iNumQueues; iQIdx++)
		{
			QueueInfo *pQueueInfo = GET_REF(g_pQueueList->eaQueueRefs[iQIdx]->hDef);

			if (pQueueInfo && pQueueInfo->pchName == pMyQueue->pchQueueName)
			{
				QueueInstance* pInstance = eaIndexedGetUsingInt(&pQueueInfo->eaInstances, pMyInstance->uiID);

				if (!pInstance || !pInstance->pParams || !gslQueue_IsValidPlayerInstance(pEnt, pInstance))
				{
					StructDestroy(parse_PlayerQueueInstance, eaRemove(&pMyQueue->eaInstances,iMyInstIdx));
					entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);		// A little unsafe but we must have a pPlayer if we got the pMyQueue off of it
				}
				break;
			}
		}
	}
}

// Clean up instance info for any queues the player becomes ineligible for. Only run this on
//   queues the player has instance info for. We deal with wholesale queue addition/removal in ProcessPlayerQueues
static void gslQueue_CleanupPlayerQueueInstances(Entity* pEnt, PlayerQueue* pQueue)
{
	QueueDef *pDef = pQueue ? GET_REF(pQueue->hDef) : NULL;
	S32 iMyInstIdx;
	QueueCannotUseReason eReason = QueueCannotUseReason_None;

	bool bIgnoreLevelRestrictions = false;	// MattK wrote some amount of code in SVN 132873 with this being effectively true.
											//  The check-in comment says nothing about level restrictions and things showing up
											//  in queue lists (which is what this controls). When true, entries in an available
											//  queue list on the client will not update if a player out levels the queue. This is
											//  sad. For purposes of NNO-15050 effectively reverting that change.
											// Update WOLF[29Mar13] I now believe this was to keep people from being kicked when they
											//  outleveled the queue while playing. Now we handle that explicitly as a state check in CheckPlayerForceLeave

	eReason = gslEntCannotUseQueue(pEnt, pDef, false, bIgnoreLevelRestrictions, false);

	if (!pDef || (eReason != QueueCannotUseReason_None && eReason != QueueCannotUseReason_RandomClosed))
	{
		// Don't force leave a random queue if you are in the map or accepting an invite (RandomClosed?)
			
		for(iMyInstIdx = eaSize(&pQueue->eaInstances)-1; iMyInstIdx >= 0; iMyInstIdx--)
		{
			// This will destroy the instance. It used to not. It seems bettter this way  since we are being removed from it due to inability to queue.
			
			gslQueue_CheckPlayerForceLeaveInstance(pEnt, pDef, pQueue, pQueue->eaInstances[iMyInstIdx]);
		}
	}
	else
	{
		for(iMyInstIdx = eaSize(&pQueue->eaInstances)-1; iMyInstIdx >= 0; iMyInstIdx--)
		{
			gslQueue_CleanupPlayerQueueInstance(pEnt, pQueue, iMyInstIdx, pDef);
		}
	}
}

static bool gslQueue_ForceLeavePending(Entity* pEnt, QueueInstance* pInstance)
{
	QueueMemberForceLeave* pForceLeave = eaIndexedGetUsingInt(&pInstance->eaForceLeaves, entGetContainerID(pEnt));
	
	if (pForceLeave && pForceLeave->uTimeoutTime > timeSecondsSince2000())
	{
		return true;
	}
	return false;
}

static void gslQueue_CleanupPendingForceLeaves(QueueInstance* pInstance)
{
	U32 uCurrentTime = timeSecondsSince2000();
	S32 iIdx;
	for (iIdx = eaSize(&pInstance->eaForceLeaves)-1; iIdx >= 0; iIdx--)
	{
		QueueMemberForceLeave* pForceLeave = pInstance->eaForceLeaves[iIdx];
		if (pForceLeave->uTimeoutTime <= uCurrentTime)
		{
			StructDestroy(parse_QueueMemberForceLeave, eaRemove(&pInstance->eaForceLeaves, iIdx));
		}
	}
}

static bool gslQueue_IsValidInstantiation(U32 uEntID, QueueInstantiationInfo* pQueueInstantiationInfo)
{
	QueueRef* pRef = eaIndexedGetUsingString(&g_pQueueList->eaQueueRefs, pQueueInstantiationInfo->pchQueueDef);
	QueueInfo *pQueueInfo = SAFE_GET_REF(pRef, hDef);
	if (pQueueInfo)
	{
		QueueInstance* pInstance = eaIndexedGetUsingInt(&pQueueInfo->eaInstances, pQueueInstantiationInfo->uInstanceID);
		if (pInstance)
		{
			QueueMember* pMember = eaIndexedGetUsingInt(&pInstance->eaUnorderedMembers, uEntID);

			if (pMember!=NULL)
			{			
				if (pQueueInstantiationInfo->iMapKey==0 || pMember->iMapKey == pQueueInstantiationInfo->iMapKey &&
					(pMember->eState == PlayerQueueState_InMap ||
					 pMember->eState == PlayerQueueState_Limbo))
				{
					return true;
				}
			}
		}
	}
	return false;
}

static void gslQueue_ProcessAttemptQueue(Entity *pEntity, PlayerQueueInfo *pPlayerQueueInfo)
{
	if (pEntity && 
		pPlayerQueueInfo && 
		pPlayerQueueInfo->pszAttemptQueueName &&
		eaSize(&pPlayerQueueInfo->eaQueues) > 0)
	{
		if (g_QueueConfig.bEnableStrictTeamRules)
		{
			if (team_IsMember(pEntity))
			{
				gslQueue_TeamJoin(pEntity, pPlayerQueueInfo->pszAttemptQueueName, 0, 0, true);
			}
			else
			{
				gslQueue_JoinCmd(pEntity, pPlayerQueueInfo->pszAttemptQueueName);
			}
		}
		else
		{
			gslQueue_JoinCmd(pEntity, pPlayerQueueInfo->pszAttemptQueueName);
		}

		pPlayerQueueInfo->pszAttemptQueueName = NULL;
	}
	
}

static void gslQueue_CleanupPlayerQueues(void)
{
	EntityIterator* pIter;
	Entity* pEnt;
	int iMyQIdx;

	PERFINFO_AUTO_START_FUNC();
	
	pIter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);

	while ((pEnt = EntityIteratorGetNext(pIter)))
	{
		PlayerQueueInfo *pMyQueues = NULL;
			
		if (!pEnt->pPlayer || !pEnt->pPlayer->pPlayerQueueInfo)
			continue;

		pMyQueues = pEnt->pPlayer->pPlayerQueueInfo;

		for (iMyQIdx = eaSize(&pMyQueues->eaQueues) - 1; iMyQIdx >=0; iMyQIdx--)
		{
			PlayerQueue* pQueue = pMyQueues->eaQueues[iMyQIdx];
			if (pQueue==NULL)
			{
				eaRemove( &(pMyQueues->eaQueues), iMyQIdx);
			}
			else if (eaSize(&pQueue->eaInstances) > 0)
			{
				gslQueue_CleanupPlayerQueueInstances(pEnt, pQueue);
			}
		}
	}
	EntityIteratorRelease(pIter);

	PERFINFO_AUTO_STOP();
}

// Can be called either explicitly when we EntAbandonThisMapAndQueue from the GameServer the Entity is on,
//   Or we wait until we register an invalid queue association in gslQueue_ProcessPlayerQueues
static void gslQueue_PlayerRemoveQueueInstantiation(Entity* pEntity)
{
	// Remove the EntityPlayer-side info about being attached to a queue map
	if (pEntity!=NULL && pEntity->pPlayer!=NULL && pEntity->pPlayer->pPlayerQueueInfo!=NULL)
	{
		StructDestroySafe(parse_QueueInstantiationInfo, &pEntity->pPlayer->pPlayerQueueInfo->pQueueInstantiationInfo);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}


void gslQueue_RequestPlayerQueuesProcess(Entity *pEnt)
{
	// We're requesting that the player's queue info be updated. Use this for periodic tick based updating.
	
	if (pEnt!=NULL && pEnt->pPlayer!=NULL)
	{
		if (!pEnt->pPlayer->pPlayerQueueInfo)
		{
			pEnt->pPlayer->pPlayerQueueInfo = StructCreate(parse_PlayerQueueInfo);
		}

		// Only allow 5 seconds updates
		if (timeSecondsSince2000() > pEnt->pPlayer->pPlayerQueueInfo->iLastDataProcessTime + 5)
		{
			pEnt->pPlayer->pPlayerQueueInfo->bDataUpdateRequested = true;
		}
	}
}

void gslQueue_ForcePlayerQueuesProcess(Entity *pEnt)
{
	// We're forcing that the player's queue info be updated. Use this for event-based updating
	
	if (pEnt!=NULL && pEnt->pPlayer!=NULL)
	{
		if (!pEnt->pPlayer->pPlayerQueueInfo)
		{
			pEnt->pPlayer->pPlayerQueueInfo = StructCreate(parse_PlayerQueueInfo);
		}

		// No timing restriction
		pEnt->pPlayer->pPlayerQueueInfo->bDataUpdateRequested = true;
	}
}


static void gslQueue_ProcessPlayerQueues(void)
{
	Entity* currEnt;
	U32 iCurrentTime = timeSecondsSince2000();
	bool bUpdateQueues = false;

	static ContainerID *piUnimportantEnts = NULL;
	static QueueFailRequirementsData** s_eaFailsAllReqs = NULL;

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("Existing Queues", 1);

	//Handle existing player queues
	{
		EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
		while ((currEnt = EntityIteratorGetNext(iter)))
		{
			PlayerQueueInfo *pMyQueues = NULL;

			if(!currEnt->pPlayer || !currEnt->pPlayer->pPlayerQueueInfo)
			{
				if(g_QueueConfig.bKeepQueueInfo && currEnt->pPlayer)
				{
					// Make sure the player has queue information
					gslQueueRefresh(currEnt);
				}
				continue;
			}

			// Always attempt to cleanup invalid instantiations
			
			PERFINFO_AUTO_START("Check Queue Instantiation", 1);

			pMyQueues = currEnt->pPlayer->pPlayerQueueInfo;
			if (pMyQueues->pQueueInstantiationInfo!=NULL)
			{
				if (g_pQueueList)
				{
					if (gslQueue_IsValidInstantiation(currEnt->myContainerID, pMyQueues->pQueueInstantiationInfo))
					{
						bUpdateQueues = true;
					} else {
						// We are associated with an invalid map
						gslQueue_PlayerRemoveQueueInstantiation(currEnt);
					}
				}
				else
				{
					bUpdateQueues = true;
				}
			}

			PERFINFO_AUTO_STOP(); // Check Queue Instatiation

			// See if we have new data that we can apply to a recent request. (s_uiQueueNewDataFromQueueServer cleared at the end of this function)
			if (s_uiQueueNewDataFromQueueServer)
			{
				if (iCurrentTime < pMyQueues->iLastDataProcessTime + QUEUE_REQUEST_TIME)
				{
					// There was a recent request. Update now since we might have been waiting on the data we just got.
					gslQueue_ForcePlayerQueuesProcess(currEnt);
				}
			}

			PERFINFO_AUTO_START("Update Client Requests", 1);

			//Update if the client requests, or the random queues have changed
			if (pMyQueues->bDataUpdateRequested || bRandomQueuesUpdated == true)
			{
				S32 i, j, iQIdx, iNumQueues = g_pQueueList ? eaSize(&g_pQueueList->eaQueueRefs) : 0;

				pMyQueues->bDataUpdateRequested=false;
				pMyQueues->iLastDataProcessTime = timeSecondsSince2000();

				for (i = eaSize(&s_eaFailsAllReqs)-1; i >= 0; i--)
				{
					s_eaFailsAllReqs[i]->bFailsAllReqs = true;
					s_eaFailsAllReqs[i]->bFailsAnyReqs = false;
				}

				PERFINFO_AUTO_START("Queue Loop", 1);

				for (iQIdx = 0; iQIdx < iNumQueues; iQIdx++)
				{
					QueueCannotUseReason eReason;
					QueueInfo *pQueueInfo = GET_REF(g_pQueueList->eaQueueRefs[iQIdx]->hDef);
					QueueDef *pDef = pQueueInfo ? GET_REF(pQueueInfo->hDef) : NULL;
					PlayerQueue* pPlayerQueue = pQueueInfo ? eaIndexedGetUsingString(&pMyQueues->eaQueues,pQueueInfo->pchName) : NULL;
					S32 iInstanceIdx, iInstanceCount = pQueueInfo ? eaSize(&pQueueInfo->eaInstances) : 0;
					QueueFailRequirementsData* pReqData = NULL;

					if (pQueueInfo==NULL || pDef==NULL || !pDef->Settings.bPublic)
						continue;

					for (i = eaSize(&s_eaFailsAllReqs)-1; i >= 0; i--)
					{
						if (s_eaFailsAllReqs[i]->eMapType == pDef->MapSettings.eMapType)
						{
							pReqData = s_eaFailsAllReqs[i];
							break;
						}
					}
					if (i < 0)
					{
						pReqData = StructCreate(parse_QueueFailRequirementsData);
						pReqData->eMapType = pDef->MapSettings.eMapType;
						eaPush(&s_eaFailsAllReqs, pReqData);
					}

					if (eReason = gslEntCannotUseQueue(currEnt,pDef,false,false,false))
					{
						if (pReqData->bFailsAllReqs && eReason != QueueCannotUseReason_InvalidAffiliation) // Affiliation check: SVN90445 [STO-16679]. I do not understand why
						{
							if (!pReqData->bFailsAnyReqs)
							{
								pReqData->eReason = eReason;
								pReqData->bFailsAnyReqs = true;
							}
							else if (pReqData->eReason != eReason)
							{
								pReqData->eReason = QueueCannotUseReason_Other;
							}
						}
						if (!gslQueue_ShouldShowInvalidQueue(eReason))
						{
							// It's invalid, and we don't want it in our list at all. Destroy it if it's there
							if (pPlayerQueue!=NULL)
							{
								eaFindAndRemove(&pMyQueues->eaQueues,pPlayerQueue);
								StructDestroy(parse_PlayerQueue, pPlayerQueue);
							}
							continue;
						}
					}
					
					if (eReason == QueueCannotUseReason_None)
					{
						pReqData->bFailsAllReqs = false;
					}

					if (pPlayerQueue==NULL)
					{
						pPlayerQueue = gslQueue_AddPlayerQueue(currEnt, pQueueInfo);
					}
					pPlayerQueue->eCannotUseReason = eReason;


					/////////////////////////////////////////////////////////////////////////////////
					//
					//  Run through all instances on the player and update them as if they were not
					//  members of the queue. This will update any player instance info when we are dropped
					//  from a queue. Since the other gslQueue_UpdatePlayerQueueInstance will no longer run.
				
					PERFINFO_AUTO_START("Queue Instance Check", 1);
					
					for (iInstanceIdx = 0; iInstanceIdx < iInstanceCount; iInstanceIdx++)
					{
						QueueInstance* pInstance = pQueueInfo->eaInstances[iInstanceIdx];

						if (!pInstance->pParams || gslQueue_ForceLeavePending(currEnt, pInstance))
							continue;

						//  This Updates the instance information as if the player were not a member of the queue.
						//    which sets the state to None. Later when we process all queue members we set the correct state.
						//  This is really only a need for queues that we are NOT a member of, but where we are holding instance
						//    information. It is not completely clear when this SHOULD be necessary, but there have been issues
						//    with leaving queues or queues becoming invalid and the NW UI not updating properly.
						//    (Team leave where the non-leader doesn't drop, being in a GearRating queue and unequipping something)
						//  I'm not sure what is special about Unnamed Private queues that makes them want to skip this
						//  Add the IsValid check because we don't want to keep adding instances and then turning around and deleting them
						//    in cleanup.
						//  We are much more aggressive with deleting instances the player cannot use, so we may be calling this too much.
						if ((!pInstance->pParams->uiOwnerID || pInstance->pParams->pchPrivateName) &&
							gslQueue_IsValidPlayerInstance(currEnt, pInstance) &&
							!gslEntCannotUseQueueInstance(currEnt,pInstance->pParams,pDef,false,false,false,false))
						{
							gslQueue_UpdatePlayerQueueInstance(currEnt, pPlayerQueue, pQueueInfo, pInstance, NULL);
						}
					}
					PERFINFO_AUTO_STOP(); // Instance Check
				}

				PERFINFO_AUTO_STOP(); // Queue Loop

				//////////////////////////////////////////////////////////////////////////
				//
				//  Star Trek needs to know if a player failed all queues for a particular
				//  reason. Sadness.
				
				for (i = eaSize(&s_eaFailsAllReqs)-1; i >= 0; i--)
				{
					QueueFailRequirementsData* pReqData = s_eaFailsAllReqs[i];
					QueueFailRequirementsData* pMyReqData = NULL;

					for (j = eaSize(&pMyQueues->eaFailsAllReqs)-1; j >= 0; j--)
					{
						if (pMyQueues->eaFailsAllReqs[j]->eMapType == pReqData->eMapType)
						{
							pMyReqData = pMyQueues->eaFailsAllReqs[j];
							break;
						}
					}
					if (j < 0)
					{
						pMyReqData = StructCreate(parse_QueueFailRequirementsData);
						pMyReqData->eMapType = pReqData->eMapType;
						eaPush(&pMyQueues->eaFailsAllReqs, pMyReqData);
					}

					if (pReqData->bFailsAllReqs && pReqData->bFailsAnyReqs)
					{
						pMyReqData->eReason = pReqData->eReason;
					}
					else
					{
						pMyReqData->eReason = QueueCannotUseReason_None;
					}
				}
				
				//////////////////////////////////////////////////////////////////////////

				PERFINFO_AUTO_START("Process Attempt", 1);

				//Make sure that the queues state update if the player is requesting them
				bUpdateQueues = true;

				gslQueue_ProcessAttemptQueue(currEnt, pMyQueues);

				PERFINFO_AUTO_STOP(); // Process Attempt
			}
			else if (!pMyQueues->uLeaverPenaltyDuration && !pMyQueues->pPenaltyData && !pMyQueues->pQueueInstantiationInfo)
			{
				if(!g_QueueConfig.bKeepQueueInfo)
				{
					if (iCurrentTime > pMyQueues->iLastDataProcessTime + QUEUE_REQUEST_TIME)
					{
						eaiPush(&piUnimportantEnts, currEnt->myContainerID);
					}
				}
				continue;
			}

			PERFINFO_AUTO_STOP(); // Update Client Requests

			entity_SetDirtyBit(currEnt, parse_Player, currEnt->pPlayer, false);
		}
		EntityIteratorRelease(iter);
	}

	PERFINFO_AUTO_STOP(); // Existing Queues

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	
	PERFINFO_AUTO_START("Existing Handles", 1);

	//Existing handles for queue server info
	if (g_pQueueList)
	{
		S32 iQueueRefIdx, iNumQueueRefs = eaSize(&g_pQueueList->eaQueueRefs);
		for (iQueueRefIdx = 0; iQueueRefIdx < iNumQueueRefs; iQueueRefIdx++)
		{
			QueueInfo *pQueueInfo = GET_REF(g_pQueueList->eaQueueRefs[iQueueRefIdx]->hDef);
			
			if (pQueueInfo && pQueueInfo->iContainerID)
			{
				S32 iInstanceIdx, iMemIdx;

				for (iInstanceIdx = eaSize(&pQueueInfo->eaInstances)-1; iInstanceIdx >= 0; iInstanceIdx--)
				{
					QueueInstance* pInstance = pQueueInfo->eaInstances[iInstanceIdx];

					gslQueue_CleanupPendingForceLeaves(pInstance);

					for (iMemIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemIdx >= 0; iMemIdx--)
					{
						QueueMember* pMember = pInstance->eaUnorderedMembers[iMemIdx];
						Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pMember->iEntID);
						
						if (pEnt && pEnt->pPlayer && !gslQueue_ForceLeavePending(pEnt, pInstance))
						{
							PlayerQueue* pPlayerQueue;

							if(!pEnt->pPlayer->pPlayerQueueInfo)
								pEnt->pPlayer->pPlayerQueueInfo = StructCreate(parse_PlayerQueueInfo);

							entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);

							pPlayerQueue = eaIndexedGetUsingString(&pEnt->pPlayer->pPlayerQueueInfo->eaQueues, pQueueInfo->pchName);
	
							//If this player doesn't have this queue yet, add it
							if (!pPlayerQueue)
							{
								pPlayerQueue = gslQueue_AddPlayerQueue(pEnt, pQueueInfo);
							}

							gslQueue_UpdatePlayerQueueInstance(pEnt, pPlayerQueue, pQueueInfo, pInstance, pMember);
							
							if(pInstance->eaUnorderedMembers[iMemIdx]->eState != PlayerQueueState_None)
							{
								eaiFindAndRemove(&piUnimportantEnts, pEnt->myContainerID);
								bUpdateQueues = true;
							}
						}
					}
				}
			}
		}
	}

	PERFINFO_AUTO_STOP(); // Existing Handles

	///////////////////////////////////////////////////////////////////////////

	// Cleanup invalid player queues. 
	// This must be done after all other queue updates because it relies on correct queue state information
	//	  Really this only looks up and deals with instance information. 
	gslQueue_CleanupPlayerQueues();

	///////////////////////////////////////////////////////////////////////////

	// Hefty subscription management stuff

	PERFINFO_AUTO_START("Manage Queue List", 1);

	if(bUpdateQueues)
	{
		if(!g_pQueueList && s_uiQueueListNextRequestTime <= iCurrentTime)
		{
			gslQueue_FillQueueList(iCurrentTime);
		}

		s_uiQueueListClearTimer = iCurrentTime + QUEUE_REQUEST_TIME;
	}
	else
	{
		//   Never clear if we are always Keep
		if (!g_QueueConfig.bKeepQueueInfo)
		{
			if (iCurrentTime > s_uiQueueListClearTimer)
			{
				//No longer needs the queue info, remove all the handles. This is 'expensive'
				gslQueue_ClearQueueList();
			}
		}
	}

	PERFINFO_AUTO_STOP(); // Manage Queue List


	////////////////////////////////////////////////////////////////////////////////////
	//
	//  Go through the unimportant entities (those that no longer desire queue info)
	//   and remove the data from them
	
	if (!g_QueueConfig.bKeepQueueInfo)
	{
		// Only do this if we're not Keeping the queue info

		if(eaiSize(&piUnimportantEnts))
		{
			S32 iEntIdx;
	
			PERFINFO_AUTO_START("Unimportant Ents", 1);
	
			for(iEntIdx = eaiSize(&piUnimportantEnts)-1;iEntIdx >= 0; iEntIdx--)
			{
				Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, piUnimportantEnts[iEntIdx]);		
				if(pEnt && pEnt->pPlayer &&  pEnt->pPlayer->pPlayerQueueInfo)
				{
					StructDestroySafe(parse_PlayerQueueInfo, &pEnt->pPlayer->pPlayerQueueInfo);
					entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
				}
			}
	
			eaiClearFast(&piUnimportantEnts);
	
			PERFINFO_AUTO_STOP(); // Unimportant Ents
		}
	}

	s_uiQueueNewDataFromQueueServer = 0;	// We dealt with this. We can clear it now.
	
	PERFINFO_AUTO_STOP();
}

// TRUE if this map was launched from a queue, and we need initialization data from the queue server
bool gslQueue_WaitingForQueueData(int iPartitionIdx)
{
	QueuePartitionInfo *pInfo;
	if (pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx)) 
	{
		U32 uCurrentTime = timeSecondsSince2000();

		if (!pInfo->pGameInfo)
		{
			if (!pInfo->uWaitingForMatchInfoStartTime)
			{
				pInfo->uWaitingForMatchInfoStartTime = uCurrentTime;
				return true;
			} 
			else
			{
				U32 uStartTime = pInfo->uWaitingForMatchInfoStartTime;
				if (uCurrentTime - uStartTime < QUEUE_WAIT_FOR_MATCH_INFO_TIMEOUT)
				{
					return true;
				}
			}
		}
	}
	return false;
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Map leave/abandon stuff

bool gslQueue_IsLeaverPenaltyEnabled(int iPartitionIdx)
{
	if (gslQueue_IsQueueMap())
	{
		QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);
		if (pInfo && pInfo->pGameInfo && pInfo->pGameInfo->uLeaverPenaltyDuration > 0)
		{
			return true;
		}
	}
	return false;
}

// We are leaving a map, and associated queue. This must be run on the game server associated with the queue.
//  This is caused by an externally generated map move, or possibly
//  by the 'ripcord' being pulled either on this map, or when we are on an external map.
//  This is the end result of gslQueue_HandleLeaveMap or gslQueue_HandleAbandonMap
// Note it doesnot actually require the Entity that the uEntID refers to as it may be run on
//   a different server than the one that has the entity.

void gslQueue_EntAbandonThisMapAndQueue(ContainerID uEntID, ContainerID uMapID, U32 uPartitionID, bool bDoMapLeaveIfOnMap)
{
	bool bGameIsFinished;
	bool bLeaverPenaltyEnabled;
	S64 iMapKey;
	const char* pchQueueDefName;
	QueuePartitionInfo *pInfo;
	int iPartitionIdx = partition_IdxFromID(uPartitionID);
	Entity* pLeaveEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, uEntID);	// May be NULL if we're concerned with an entity not on this partition
	

	pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);
	// pInfo may be NULL if we had a catastrophe and the partition disappeared at some point. The ObjectDB versions of the teams and queues
	//  may still think we are on a queue map in a queue team.
	if (pInfo!=NULL)
	{
		pchQueueDefName = SAFE_MEMBER(pInfo->pGameInfo, pchQueueDef);
	
		bGameIsFinished = (mapState_GetScoreboardState(mapState_FromPartitionIdx(iPartitionIdx)) == kScoreboardState_Final);
		bLeaverPenaltyEnabled = gslQueue_IsLeaverPenaltyEnabled(iPartitionIdx);
		iMapKey = queue_GetMapKey(uMapID, uPartitionID);
		RemoteCommand_aslQueue_HandleLeaveMap(GLOBALTYPE_QUEUESERVER, 0, pchQueueDefName, iMapKey, uEntID, bGameIsFinished, bLeaverPenaltyEnabled);
	}

	if (pLeaveEnt!=NULL)
	{
		// Get rid of our instantiation info
		gslQueue_PlayerRemoveQueueInstantiation(pLeaveEnt);
		if (bDoMapLeaveIfOnMap)
		{
			// Return to our last static map
			LeaveMap(pLeaveEnt);		
		}
	}
	else
	{
		// We may be offline and associated with this map. We'll need to rely on
		//   mechanics_LogoutTimersProcess(Entity* pEnt) to catch the person when they log back in and to port them off the map.
	}
}

// We are leaving a map, presumably the queue map. This is caused by an externally generated map move as opposed to ripcording.
//   Make sure we pay attention to the bStayInQueueOnMapLeave flags
bool gslQueue_HandleLeaveMap(Entity* pEnt)
{
	const char* pchQueueDefName;
	QueueDef* pDef;
	QueuePartitionInfo *pInfo;

	if (!pEnt || !pEnt->pPlayer)
	{
		return false;
	}
	if (!gslQueue_IsQueueMap())
	{
		return false;
	}
	pInfo = gslQueuePartitionInfoFromIdx(entGetPartitionIdx(pEnt));
	pchQueueDefName = SAFE_MEMBER(pInfo->pGameInfo, pchQueueDef);
	pDef = queue_DefFromName(pchQueueDefName);

	if (g_QueueConfig.bStayInQueueOnMapLeave ||	(pDef && pDef->Settings.bStayInQueueOnMapLeave))
	{
		return false;
	}
	
	gslQueue_EntAbandonThisMapAndQueue(entGetContainerID(pEnt),
									gGSLState.gameServerDescription.baseMapDescription.containerID,
									partition_IDFromIdx(pInfo->iPartitionIdx),
									false);
								// We are assumably leaving the map by some other means, so don't try leaving from within the abandon.
	
	return true;
}



// The 'ripcord' was pulled. If we are not on the queue map, just stop our association with the (any) queue and map.
//  Otherwise do a HandleLeaveMap but do NOT pay attention to bStayInQueueOnMapLeave as we really really want to leave.
//  See also gslQueue_HandleLeaveMap.
// This handles both kick and leave. For leave Instigtor and AbandoningEntID will be the same ent.
//  For kick they will be different.
// We need to worry about the instigator and abandoning ent being on the same OR different servers. And those being the queue map,
//  or not.
// Caution should be used with bLeaveTeam being false as this could result in being stuck on a team that is auto-teamed and tied
//  to a map. The option mainly exists for TeamRelease where the calling code also makes sure that the team is dissociated from GameServer ownership.
bool gslQueue_HandleAbandonMap(Entity* pInstigatorEnt, ContainerID uAbandoningEntID, bool bLeaveTeam)
{
	if (!g_QueueConfig.bAllowQueueAbandonment)
	{
		return(false);
	}
	
	if (!pInstigatorEnt || !pInstigatorEnt->pPlayer)
	{
		return false;
	}

	// If we're not on the map that we're queue-associated with, we need to remote command to the other server to do the kick.
	if (!gslQueue_IsQueueMap())
	{
		PlayerQueueInfo* pInfo = SAFE_MEMBER2(pInstigatorEnt, pPlayer, pPlayerQueueInfo);
	
		if (pInfo!=NULL && pInfo->pQueueInstantiationInfo!=NULL)
		{
			U32 uMapID = queue_GetMapIDFromMapKey(pInfo->pQueueInstantiationInfo->iMapKey);
			U32 uPartitionID = queue_GetPartitionIDFromMapKey(pInfo->pQueueInstantiationInfo->iMapKey);
			U32 uTeamID = team_GetTeamID(pInstigatorEnt);

			// Force a team leave at this pointif requested. We want this in abandon and not in HandleLeaveMap. Star Trek Team leaving is handled
			//   via the queue maps owening the team and gslTeam_EntityUpdate checking to see if the ent map matches the team map.
			// We will NOT be leaving the team if this is called from a TeamRelease generally

			if (bLeaveTeam && uTeamID!=0)
			{
				RemoteCommand_aslTeam_Leave(GLOBALTYPE_TEAMSERVER, 0, uTeamID, entGetContainerID(pInstigatorEnt), uAbandoningEntID, false, NULL, NULL, NULL);
			}
			RemoteCommand_gslQueue_DoEntAbandonThisMapAndQueue(GLOBALTYPE_GAMESERVER, uMapID, uAbandoningEntID, uMapID, uPartitionID);
			// EntAbandonThisMapAndQueue will cause the abandoning ent to leave the map if it is on it. Could happen if Kicking.
		}
	}
	else
	{
		// Okay. We're on the correct map already. Just abandon directly/
		//  This really should only happen if we can stay in the queue when we leave the map

		// Force a team leave.
		U32 uTeamID = team_GetTeamID(pInstigatorEnt);
		bool bDoMapLeaveIfOnMap=true; // Always do this
		
		if (bLeaveTeam && uTeamID!=0)
		{
			RemoteCommand_aslTeam_Leave(GLOBALTYPE_TEAMSERVER, 0, uTeamID, entGetContainerID(pInstigatorEnt), uAbandoningEntID, false, NULL, NULL, NULL);
		}
		gslQueue_EntAbandonThisMapAndQueue(uAbandoningEntID, gGSLState.gameServerDescription.baseMapDescription.containerID,
										   partition_IDFromIdx(entGetPartitionIdx(pInstigatorEnt)), bDoMapLeaveIfOnMap);
		// EntAbandonThisMapAndQueue will cause the abandoning ent to leave the map if it is on it. Could happen if Leaving.
	}
	return true;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Sends a system message to all players
static void gslQueue_SendMessageToAllPlayers(int iPartitionIdx,
											 const char* pchMessageKey, 
											 GlobalType eSubjectType,
											 ContainerID uSubjectID, 
											 GlobalType eTargetType,
											 ContainerID uTargetID,
											 bool bExcludeSubject)
{
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);
	const char* pchQueueDefName = SAFE_MEMBER2(pInfo, pGameInfo, pchQueueDef);

	if (pInfo && pInfo->pMatch)
	{
		S32 iGroupIdx, iMemberIdx;
		for (iGroupIdx = eaSize(&pInfo->pMatch->eaGroups)-1; iGroupIdx >= 0; iGroupIdx--)
		{
			QueueGroup* pGroup = pInfo->pMatch->eaGroups[iGroupIdx];
			for (iMemberIdx = eaSize(&pGroup->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
			{
				QueueMatchMember* pMember = pGroup->eaMembers[iMemberIdx];
				
				if (bExcludeSubject && pMember->uEntID == uSubjectID)
				{
					continue;
				}
				gslQueue_ResultMessageEx(pMember->uEntID, 
										 eSubjectType,
										 uSubjectID, 
										 eTargetType,
										 uTargetID, 
										 pchQueueDefName, 
										 pchMessageKey, 
										 false);
			}
		}
	}
}

static void gslQueue_AddPendingChatMessage(SA_PARAM_NN_VALID Entity* pEnt, 
										   const char* pchChannel, 
										   const char* pchMessageKey,
										   U32 uSubjectID)
{
	QueuePendingChatMessage* pPending = StructCreate(parse_QueuePendingChatMessage);
	pPending->uEntID = entGetContainerID(pEnt);
	pPending->uSubjectID = uSubjectID;
	pPending->pchChannel = StructAllocString(pchChannel);
	pPending->pchMessageKey = allocAddString(pchMessageKey);
	eaPush(&s_eaPendingChatMessages, pPending);
}

// Find a player's name from ID for a private queue
static const char* gslQueue_GetMemberNameFromID(SA_PARAM_NN_VALID Entity* pEnt, ContainerID uSubjectID)
{
	PlayerQueueInstance* pInstance;
	if (entGetContainerID(pEnt) == uSubjectID)
	{
		return entGetLocalName(pEnt);
	}
	if (pInstance = gslQueue_FindPrivateInstance(pEnt, NULL))
	{
		S32 iMemberIdx;
		for (iMemberIdx = eaSize(&pInstance->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
		{
			PlayerQueueMember* pMember = pInstance->eaMembers[iMemberIdx];
			if (pMember->uiID == uSubjectID)
			{
				Entity* pMemberEnt = GET_REF(pMember->hEntity);
				return pMemberEnt ? entGetLocalName(pMemberEnt) : NULL;
			}
		}
	}
	return NULL;
}

bool gslQueue_SendSystemMessageToChannel(SA_PARAM_NN_VALID Entity* pEnt, 
										 const char* pchChannel, 
										 const char* pchMessageKey,
										 U32 uSubjectID,
										 bool bAddToPendingOnFail,
										 bool bAlwaysSucceed)
{
	const char* pchSubjectName = NULL;
	Language eLang = entGetLanguage(pEnt); 
	char* estrMessage = NULL;
	bool bSuccess = true;
	
	if (uSubjectID)
	{
		pchSubjectName = gslQueue_GetMemberNameFromID(pEnt, uSubjectID);
	}
	if (!pchSubjectName || !pchSubjectName[0])
	{
		if (bAddToPendingOnFail)
		{
			gslQueue_AddPendingChatMessage(pEnt, pchChannel, pchMessageKey, uSubjectID);
		}
		else
		{
			pchSubjectName = "<InvalidEntityName>";
		}
		bSuccess = bAlwaysSucceed;
	}
	if (bAlwaysSucceed || bSuccess)
	{
		estrStackCreate(&estrMessage);
		langFormatGameMessageKey(eLang, 
								 &estrMessage, 
								 pchMessageKey, 
								 STRFMT_ENTITY(pEnt), 
								 STRFMT_STRING("Subject", NULL_TO_EMPTY(pchSubjectName)),
								 STRFMT_END);
		ServerChat_SendChannelSystemMessage(entGetContainerID(pEnt), kChatLogEntryType_Channel, pchChannel, estrMessage);
		estrDestroy(&estrMessage);
	}
	return bSuccess;
}

bool gslQueue_HandlePlayerLogin(Entity* pEnt)
{
	if (!gslQueue_IsQueueMap())
	{
		return false;
	}
	if (pEnt)
	{
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);

		if (g_QueueConfig.bEnableInMapPlayerChatUpdates)
		{
			ContainerID uEntID = entGetContainerID(pEnt);
			gslQueue_SendMessageToAllPlayers(iPartitionIdx, QUEUE_PLAYER_JOINED_GAME_MESG, GLOBALTYPE_ENTITYPLAYER, uEntID, 0, 0, true);
		}
	}
	return true;
}

bool gslQueue_HandlePlayerLogout(Entity* pEnt)
{
	queue_LeaveAllQueues(pEnt);

	if (!gslQueue_IsQueueMap())
	{
		return false;
	}

	if (pEnt && g_QueueConfig.bEnableInMapPlayerChatUpdates)
	{
		ContainerID uEntID = entGetContainerID(pEnt);
		gslQueue_SendMessageToAllPlayers(entGetPartitionIdx(pEnt), QUEUE_PLAYER_LEFT_GAME_MESG, GLOBALTYPE_ENTITYPLAYER, uEntID, 0, 0, true);
	}
	return true;
}

void gslQueue_HandlePlayerNearDeathEnter(Entity* pEnt, Entity* pKiller)
{
	if (zmapInfoGetMapType(NULL) != ZMTYPE_PVP)
	{
		return;
	}

	gslPVPGame_PlayerNearDeathEnter(pEnt, pKiller);
}

void gslQueue_HandlePlayerKilled(Entity* pEnt, Entity* pKiller)
{
	if (zmapInfoGetMapType(NULL) != ZMTYPE_PVP)
	{
		return;
	}

	// Handle game-specific actions
	gslPVPGame_PlayerKilled(pEnt, pKiller);

	// Chat updates
	if (g_QueueConfig.bEnableInMapPlayerChatUpdates && pEnt && entIsPlayer(pEnt))
	{
		ContainerID uEntID = entGetContainerID(pEnt);
		GlobalType eKillerType = 0;
		ContainerID uKillerID = 0;
		const char* pchMsgKey = QUEUE_PLAYER_KILLED_NOTARGET_MESG;
		
		if (pKiller)
		{
			eKillerType = entGetType(pKiller);
			uKillerID = entGetContainerID(pKiller);
			pchMsgKey = QUEUE_PLAYER_KILLED_MESG;
		}
		gslQueue_SendMessageToAllPlayers(entGetPartitionIdx(pEnt), 
										 pchMsgKey,
										 GLOBALTYPE_ENTITYPLAYER,
										 uEntID, 
										 eKillerType,
										 uKillerID,
										 false);
	}
}


void glsQueue_DoLogoutKickForNonMembership(QueuePartitionInfo *pInfo, U32 uEntID)
{
	Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uEntID);
	
	int iKickTime = 5;

	U32 uMapID = gGSLState.gameServerDescription.baseMapDescription.containerID;
	U32 uPartitionID = partition_IDFromIdx(pInfo->iPartitionIdx);
	S64 iMapKey = queue_GetMapKey(uMapID, uPartitionID);
	const char* pchQueueDefName = SAFE_MEMBER2(pInfo, pGameInfo, pchQueueDef);
	
	RemoteCommand_aslQueue_LogMapKickedMember(GLOBALTYPE_QUEUESERVER, 0, pchQueueDefName, uEntID, iMapKey);
	qDebugPrintf("Entity %d is being kicked from this map\n", uEntID);

	// Warn. Used to happen at 5 seconds. Now happens when the event is noticed
	{
		char *estrWarning = NULL;
		
		estrStackCreate(&estrWarning);
		entFormatGameMessageKey(pEnt, &estrWarning, "QueueServer_KickWarning",
							STRFMT_INT("TimeRemaining", iKickTime),
							STRFMT_END);

		// This should not be PvPWarning. Either we need a PvE channel and need to split depending on what type of map, or we need a Queue channel
		notify_NotifySend(pEnt, kNotifyType_PvPWarning, estrWarning, NULL, NULL);
		estrDestroy(&estrWarning);
	}

	// Deal with pendings.
	{
		EntLocalTeamPending* pPending = eaIndexedGetUsingInt(&pInfo->ppEntsLocalTeamPending, uEntID);

		if (pPending)
		{
			gslTeam_RemoveMemberFromPendingList(pInfo, uEntID);
			RemoteCommand_aslQueue_HandlePendingLocalTeamTimeout(GLOBALTYPE_QUEUESERVER, 0, pchQueueDefName, iMapKey, uEntID);

		}
	}
}
 

static ScoreboardPartitionInfo* gslQueue_InitializeScores(QueuePartitionInfo *pInfo, ScoreboardPartitionInfo* pScoreboardInfo)
{
	const char* pchQueueDefName = SAFE_MEMBER(pInfo->pGameInfo, pchQueueDef);
	QueueDef* pDef = pchQueueDefName ? queue_DefFromName(pchQueueDefName) : NULL;
	
	PERFINFO_AUTO_START_FUNC();

	if (!pScoreboardInfo)
	{
		CritterFaction* pNeutralFaction = pDef ? GET_REF(pDef->MapRules.hNeutralFaction) : NULL;
		
		pScoreboardInfo = gslScoreboard_CreateInfo(pInfo->iPartitionIdx);
		gslScoreboard_SetDefaultFaction(pInfo->iPartitionIdx, pNeutralFaction);
		pScoreboardInfo->bRemoveInactivePlayerScores = !!g_QueueConfig.bScoreboardRemovesInactivePlayerScores;
	}

	if (!eaSize(&pScoreboardInfo->Scores.eaGroupList))
	{
		int iGroupIdx, iGroupCount = pDef ? eaSize(&pDef->eaGroupDefs) : 0;

		for (iGroupIdx = 0; iGroupIdx < iGroupCount; iGroupIdx++)
		{
			QueueGroupDef* pGroupDef = pDef->eaGroupDefs[iGroupIdx];
			CritterFaction* pFaction = GET_REF(pGroupDef->hFaction);
			Message* pMessage = GET_REF(pGroupDef->DisplayName.hMessage);
			gslScoreboard_CreateGroup(pInfo->iPartitionIdx, pFaction, pMessage, pDef->pchIcon);
		}
	}

	PERFINFO_AUTO_STOP();

	return pScoreboardInfo;
}

static void pvp_CleanupEntGroupProperties(Entity* pEnt, QueueGroupDef *pGroupDef)
{
	U32 uiTime = pmTimestamp(0);
	EntityRef er = entGetRef(pEnt);

	if(IS_HANDLE_ACTIVE(pGroupDef->hFaction) && GET_REF(pGroupDef->hFaction) == GET_REF(pEnt->hFactionOverride))
	{
		gslEntity_ClearFaction(pEnt, kFactionOverrideType_DEFAULT);
	}

	if(pGroupDef->ppchTeamFXName)
	{
		int i;

		for(i=0;i<eaSize(&pGroupDef->ppchTeamFXName);i++)
		{
			pmFxStop(pEnt->pChar->pPowersMovement,kPVPAnimID_TeamFX,0,kPowerAnimFXType_PVP,
				er,
				er,
				uiTime,
				pGroupDef->ppchTeamFXName[i]);
		}
	}
}

bool gslQueue_RemoveFromGroup(Entity *pEnt,QueueMatch *pMatch)
{
	int i;

	for(i=0;i<eaSize(&pMatch->eaGroups);i++)
	{
		if(queue_Match_FindMemberInGroup(pMatch->eaGroups[i],entGetContainerID(pEnt)) >= 0)
		{
			queue_Match_RemoveMemberFromGroup(pMatch,i,entGetContainerID(pEnt));
			pvp_CleanupEntGroupProperties(pEnt,pMatch->eaGroups[i]->pGroupDef);
		}
	}

	return true;
}

static void gslQueue_UpdateEntGroupProperties(Entity* pEnt, QueuePartitionInfo *pInfo)
{
	const char* pchQueueDefName = SAFE_MEMBER(pInfo->pGameInfo, pchQueueDef);
	QueueDef* pDef = pchQueueDefName ? queue_DefFromName(pchQueueDefName) : NULL;
	if (pDef && pInfo->pMatch)
	{
		//TODO(MK): decouple the scoreboard state from the gameplay state
		if (mapState_GetScoreboardState(mapState_FromPartitionIdx(pInfo->iPartitionIdx)) != kScoreboardState_Active
			&& IS_HANDLE_ACTIVE(pDef->MapRules.hNeutralFaction))
		{
			gslEntity_SetFactionOverrideByHandle(pEnt, kFactionOverrideType_DEFAULT, REF_HANDLEPTR(pDef->MapRules.hNeutralFaction));
		}
		else
		{
			U32 uCurrentTime = timeSecondsSince2000();
			S32 i;
			for (i = eaSize(&pInfo->pMatch->eaGroups)-1; i >= 0; i--)
			{
				QueueGroup* pGroup = pInfo->pMatch->eaGroups[i];

				if (queue_Match_FindMemberInGroup(pGroup, entGetContainerID(pEnt)) >= 0)
				{
					if (pGroup->pGroupDef)
					{
						if (IS_HANDLE_ACTIVE(pGroup->pGroupDef->hFaction))
						{
							gslEntity_SetFactionOverrideByHandle(pEnt, kFactionOverrideType_DEFAULT, REF_HANDLEPTR(pGroup->pGroupDef->hFaction));
						}

						if (pGroup->pGroupDef->ppchTeamFXName)
						{
							DynParamBlock *pParamBlock;
							EntityRef er = entGetRef(pEnt);
							U32 uiTime = pmTimestamp(0);

							pParamBlock = dynParamBlockCreate();

							pmFxReplaceOrStart(pEnt->pChar->pPowersMovement,
								kPVPAnimID_TeamFX,0,kPowerAnimFXType_PVP,
								er,
								er,
								uiTime,
								pGroup->pGroupDef->ppchTeamFXName,
								pParamBlock,
								0.0f,
								false,
								NULL,
								NULL,
								false);
						}

						if(pDef->MapRules.bEnableTeamUp)
						{
							if(!pEnt->pTeamUpRequest || pEnt->pTeamUpRequest->eState != kTeamUpState_Member)
							{
								TeamUpInstance *pInstance = gslTeamUpInstance_FromKey(pInfo->iPartitionIdx,pGroup->uiTeamUpID);
								gslTeamUp_EntityJoinTeamWithoutRequest(pInfo->iPartitionIdx, pEnt, pInstance);
							}
						}
					}
					pGroup->iGroupSize++;
					pInfo->pMatch->iMatchSize++;
					break;
				}
			}
		}
	}
}

static void gslQueue_ClearMatchGroupSizes(QueueMatch *pMatch)
{
	if (pMatch)
	{
		S32 iGroupIdx;
		for (iGroupIdx = eaSize(&pMatch->eaGroups)-1; iGroupIdx >= 0; iGroupIdx--)
		{
			pMatch->eaGroups[iGroupIdx]->iGroupSize = 0;
		}
	}
}
	
S32 gslQueue_GetPlayerGroupIndex(Entity* pEnt)
{
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(entGetPartitionIdx(pEnt));

	if (pInfo->pMatch)
	{
		S32 iGroupIdx, iMemberIdx;
		for (iGroupIdx = eaSize(&pInfo->pMatch->eaGroups)-1; iGroupIdx >= 0; iGroupIdx--)
		{
			QueueGroup* pGroup = pInfo->pMatch->eaGroups[iGroupIdx];
			for (iMemberIdx = eaSize(&pGroup->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
			{
				if (entGetContainerID(pEnt) == pGroup->eaMembers[iMemberIdx]->uEntID)
				{
					return iGroupIdx;
				}
			}
		}
	}
	return -1;
}

static void gslQueue_SendVoteWarning(int iPartitionIdx,
									 SA_PARAM_NN_VALID QueueGroupDef* pGroupDef, 
									 SA_PARAM_OP_VALID Entity* pEntSubject,
									 SA_PARAM_OP_VALID Entity* pEntTarget,
									 const char* pchKey,
									 U32 uTimeRemaining)
{
	Entity* pEnt;
	EntityIterator* pIter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
	char* estrWarning = NULL;
	
	estrStackCreate(&estrWarning);
	
	while (pEnt = EntityIteratorGetNext(pIter))
	{
		estrClear(&estrWarning);
		entFormatGameMessageKey(pEnt, &estrWarning, pchKey,
			STRFMT_INT("TimeRemaining", uTimeRemaining),
			STRFMT_STRING("PlayerName", pEntSubject ? entGetLocalName(pEntSubject) : ""),
			STRFMT_STRING("TargetPlayerName", pEntTarget ? entGetLocalName(pEntTarget) : ""),
			STRFMT_STRING("GroupName", entTranslateDisplayMessage(pEnt, pGroupDef->DisplayName)),
			STRFMT_END);
		notify_NotifySend(pEnt, kNotifyType_PvPWarning, estrWarning, NULL, NULL);
	}

	EntityIteratorRelease(pIter);
	estrDestroy(&estrWarning);
}

static void gslQueue_UpdateClientVoteData(int iPartitionIdx,
										  SA_PARAM_NN_VALID QueueGroup* pGroup,
										  QueueVoteType eVoteType,
										  ContainerID uEntID,
										  ContainerID uTargetEntID,
										  S32 iVoteCount)
{
	Entity* pEnt;
	EntityIterator* pIter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
	while (pEnt = EntityIteratorGetNext(pIter))
	{
		if (pGroup->iGroupIndex == gslQueue_GetPlayerGroupIndex(pEnt))
		{
			bool bVoted = uEntID == entGetContainerID(pEnt);
			ClientCmd_QueueUI_ReceiveVoteData(pEnt, uEntID, uTargetEntID, eVoteType, iVoteCount, pGroup->iGroupSize, bVoted);
		}
	}
	EntityIteratorRelease(pIter);
}

bool gslQueue_ConcedeVote(Entity* pEnt)
{
	S32 iGroupIdx, iConcedeIdx;
	U32 uCurrentTime = timeSecondsSince2000();
	QueueGroupConcedeData* pData = NULL;
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(entGetPartitionIdx(pEnt));
	const char* pchQueueDefName = SAFE_MEMBER2(pInfo, pGameInfo, pchQueueDef);
	QueueDef* pQueueDef = pchQueueDefName ? queue_DefFromName(pchQueueDefName) : NULL;

	if (!pEnt || !pEnt->pPlayer || !pQueueDef || !pQueueDef->MapRules.bAllowConcede)
	{
		return false;
	}
	if (!pInfo->pMatch || mapState_GetScoreboardState(mapState_FromEnt(pEnt)) != kScoreboardState_Active)
	{
		return false;
	}
	if (uCurrentTime < pInfo->uQueueHadAnyPlayersTime + g_QueueConfig.uAllowConcedeStartTime)
	{
		return false;
	}
	iGroupIdx = gslQueue_GetPlayerGroupIndex(pEnt);
	if (iGroupIdx < 0)
	{
		return false;
	}
	for (iConcedeIdx = eaSize(&pInfo->eaConcedeData)-1; iConcedeIdx >= 0; iConcedeIdx--)
	{
		pData = pInfo->eaConcedeData[iConcedeIdx];
		if (pData->iGroupIndex == pInfo->pMatch->eaGroups[iGroupIdx]->iGroupIndex)
		{
			break;
		}
	}
	if (iConcedeIdx < 0)
	{
		pData = StructCreate(parse_QueueGroupConcedeData);
		pData->iGroupIndex = pInfo->pMatch->eaGroups[iGroupIdx]->iGroupIndex;
		eaPush(&pInfo->eaConcedeData, pData);
	}
	if (pData->bConceded || pData->uNextAllowedConcedeTime > uCurrentTime)
	{
		return false;
	}
	if (!ea32Size(&pData->puiEntIDs))
	{
		pData->uTimer = uCurrentTime + g_QueueConfig.uManualConcedeTime;
		gslQueue_SendVoteWarning(pInfo->iPartitionIdx,
								 pInfo->pMatch->eaGroups[iGroupIdx]->pGroupDef, 
								 pEnt, NULL,
								 "QueueServer_ConcedeVote",
								 g_QueueConfig.uManualConcedeTime);
	}
	if (ea32Find(&pData->puiEntIDs, entGetContainerID(pEnt)) < 0)
	{
		ea32Push(&pData->puiEntIDs, entGetContainerID(pEnt));
		gslQueue_UpdateClientVoteData(pInfo->iPartitionIdx,
									  pInfo->pMatch->eaGroups[iGroupIdx],
									  kQueueVoteType_Concede,
									  entGetContainerID(pEnt),
									  0,
									  ea32Size(&pData->puiEntIDs));
	}
	return true;
}

bool gslQueue_HasGroupConceded(QueuePartitionInfo *pInfo, S32 iGroupIndex)
{
	if (pInfo->pMatch)
	{
		S32 iConcedeIdx;
		for (iConcedeIdx = eaSize(&pInfo->eaConcedeData)-1; iConcedeIdx >= 0; iConcedeIdx--)
		{
			QueueGroupConcedeData* pData = pInfo->eaConcedeData[iConcedeIdx];
			if (pData->iGroupIndex == iGroupIndex)
			{
				return pData->bConceded;
			}
		}
	}
	return false;
}

void gslQueue_CheckGroupsConceded(int iPartitionIdx, const char* pchFaction, bool bFriendlyFactions, int* piResult)
{
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);

	if (pInfo && pInfo->pMatch && mapState_GetScoreboardState(mapState_FromPartitionIdx(iPartitionIdx)) == kScoreboardState_Active)
	{
		S32 iGroupIdx;
		(*piResult) = true;
		for (iGroupIdx = eaSize(&pInfo->pMatch->eaGroups)-1; iGroupIdx >= 0; iGroupIdx--)
		{
			QueueGroup* pGroup = pInfo->pMatch->eaGroups[iGroupIdx];
			const char* pchGroupFaction = REF_STRING_FROM_HANDLE(pGroup->pGroupDef->hFaction);
			if ((bFriendlyFactions && stricmp(pchGroupFaction, pchFaction) == 0) ||
				(!bFriendlyFactions && stricmp(pchGroupFaction, pchFaction) != 0))
			{
				if (!gslQueue_HasGroupConceded(pInfo, pGroup->iGroupIndex))
				{
					(*piResult) = false;
					break;
				}
			}
		}
	}
	else
	{
		(*piResult) = false;
	}
}

static bool gslQueue_AllowAutoConcede(QueuePartitionInfo* pInfo, U32 uCurrentTime)
{
	if (pInfo->uQueueHadAnyPlayersTime && pInfo->pMatch && eaSize(&pInfo->pMatch->eaGroups) > 1 &&
		uCurrentTime >= pInfo->uQueueHadAnyPlayersTime + g_QueueConfig.uAllowConcedeStartTime)
	{
		return true;
	}
	return false;
}

static void gslQueue_HandleConcedeSuccess(QueuePartitionInfo *pInfo, QueueGroup* pConcedeGroup)
{
	QueueMatch* pMatch = pInfo->pMatch;
	MapState *pMapState = mapState_FromPartitionIdx(pInfo->iPartitionIdx);

	/// Send a notification to players that the concede succeeded
	gslQueue_SendVoteWarning(pInfo->iPartitionIdx,
							 pConcedeGroup->pGroupDef, 
							 NULL, NULL,
							 "QueueServer_ConcedeSucceeded",
							 0);

	// If there is a current game running, end it
	if (pMapState && pMapState->matchState.pvpRules.eGameType != kPVPGameType_None)
	{
		int iGroupIdx;
		for (iGroupIdx = eaSize(&pMatch->eaGroups)-1; iGroupIdx >= 0; iGroupIdx--)
		{
			if (pConcedeGroup != pMatch->eaGroups[iGroupIdx])
			{
				gslPVPGame_endWithWinner(pInfo->iPartitionIdx, iGroupIdx, 0, false);
				break;
			}
		}
	}
}

static void gslQueue_UpdateConcedeVotes(QueuePartitionInfo *pInfo, QueueGroup* pGroup, U32 uCurrentTime, bool bAllowAutoConcede)
{
	S32 iConcedeIdx;
	if (pGroup->iGroupSize == 0)
	{
		if (!bAllowAutoConcede)
		{
			return;
		}
		// If there are no members in this group, then try to automatically concede
		for (iConcedeIdx = eaSize(&pInfo->eaConcedeData)-1; iConcedeIdx >= 0; iConcedeIdx--)
		{
			QueueGroupConcedeData* pData = pInfo->eaConcedeData[iConcedeIdx];
			if (pData->iGroupIndex == pGroup->iGroupIndex)
			{
				if (pData->bConceded || pData->uNextAllowedConcedeTime > uCurrentTime)
				{
					break;
				}
				if (pData->bAutoConcede && pData->uTimer)
				{
					if (uCurrentTime >= pData->uTimer)
					{
						pData->bConceded = true;
						gslQueue_HandleConcedeSuccess(pInfo, pGroup);
					}
				}
				else
				{
					pData->uTimer = uCurrentTime;
					pData->bAutoConcede = true;
				}
				break;
			}
		}
		if (iConcedeIdx < 0)
		{
			QueueGroupConcedeData* pData = StructCreate(parse_QueueGroupConcedeData);
			pData->iGroupIndex = pGroup->iGroupIndex;
			pData->uTimer = uCurrentTime + g_QueueConfig.uAutoConcedeTime;
			pData->bAutoConcede = true;
			eaPush(&pInfo->eaConcedeData, pData);
					
			gslQueue_SendVoteWarning(pInfo->iPartitionIdx,
									 pGroup->pGroupDef, 
									 NULL, NULL,
									 "QueueServer_AutoConcedeWarning",
									 g_QueueConfig.uAutoConcedeTime);
		}
	}
	else
	{
		S32 iRequiredVoteCount = ceilf(pGroup->iGroupSize*g_QueueConfig.fConcedeVoteRatio);
		for (iConcedeIdx = eaSize(&pInfo->eaConcedeData)-1; iConcedeIdx >= 0; iConcedeIdx--)
		{
			QueueGroupConcedeData* pData = pInfo->eaConcedeData[iConcedeIdx];
			if (pData->iGroupIndex == pGroup->iGroupIndex)
			{
				if (pData->bConceded || pData->uTimer == 0)
				{
					break;
				}
				if (pData->bAutoConcede || uCurrentTime >= pData->uTimer)
				{
					pData->uNextAllowedConcedeTime = uCurrentTime + g_QueueConfig.uConcedeRetryTime;
					pData->uTimer = 0;
					pData->bAutoConcede = false;
					ea32Destroy(&pData->puiEntIDs);
					gslQueue_SendVoteWarning(pInfo->iPartitionIdx,
											 pGroup->pGroupDef, 
											 NULL, NULL,
											 "QueueServer_ConcedeFailed",
											 g_QueueConfig.uConcedeRetryTime);
					gslQueue_UpdateClientVoteData(pInfo->iPartitionIdx, pGroup, 0, 0, 0, 0);
				}
				else if (ea32Size(&pData->puiEntIDs) >= iRequiredVoteCount)
				{
					pData->bConceded = true;
					gslQueue_HandleConcedeSuccess(pInfo, pGroup);
				}
				break;
			}
		}
	}
}

static void gslQueue_UpdateKickVotes(QueuePartitionInfo *pInfo, QueueGroup* pGroup, U32 uCurrentTime)
{
	S32 iVoteKickIdx;
	if (pGroup->iGroupSize)
	{
		S32 iRequiredVoteCount = ceilf((pGroup->iGroupSize-1)*g_QueueConfig.fKickVoteRatio);
		for (iVoteKickIdx = eaSize(&pInfo->eaVoteKickData)-1; iVoteKickIdx >= 0; iVoteKickIdx--)
		{
			QueueGroupKickData* pData = pInfo->eaVoteKickData[iVoteKickIdx];
			if (pData->iGroupIndex == pGroup->iGroupIndex)
			{
				bool bTimeoutExceeded = pData->uTimeout && uCurrentTime >= pData->uTimeout;
				bool bVoteCountSatisfied = !bTimeoutExceeded && ea32Size(&pData->puiEntIDs) >= iRequiredVoteCount;

				if (bTimeoutExceeded || bVoteCountSatisfied)
				{
					U32 uVoteKickPlayerID = pData->uVoteKickPlayerID;
					Entity* pEntKick = entFromContainerID(pInfo->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, uVoteKickPlayerID);
					const char* pchMsgKey;

					pData->uNextAllowedVoteKickTime = uCurrentTime + g_QueueConfig.uVoteKickRetryTime;
					pData->uTimeout = 0;
					pData->uVoteKickPlayerID = 0;
					ea32Destroy(&pData->puiEntIDs);

					if (bTimeoutExceeded)
					{
						pchMsgKey = "QueueServer_VoteKickFailed";
					}
					else
					{
						pchMsgKey = "QueueServer_VoteKickSucceeded";
					}

					// Notify clients that the kick has succeeded or failed
					gslQueue_SendVoteWarning(pInfo->iPartitionIdx,
											 pGroup->pGroupDef, 
											 NULL,
											 pEntKick,
											 pchMsgKey,
											 g_QueueConfig.uVoteKickRetryTime);
					gslQueue_UpdateClientVoteData(pInfo->iPartitionIdx, pGroup, 0, 0, 0, 0);

					if (bVoteCountSatisfied)
					{
						const char* pchQueueDefName;
						U32 uMapID;
						U32 uPartitionID;
						S64 iMapKey;

						// If the entity to kick is currently on the map, force them to leave
						if (pEntKick)
						{
							LeaveMapEx(pEntKick, NULL);
						}

						// Kick the entity from the queue
						pchQueueDefName = SAFE_MEMBER(pInfo->pGameInfo, pchQueueDef);
						uMapID = gGSLState.gameServerDescription.baseMapDescription.containerID;
						uPartitionID = partition_IDFromIdx(pInfo->iPartitionIdx);
						iMapKey = queue_GetMapKey(uMapID, uPartitionID);
						RemoteCommand_aslQueue_Kick(GLOBALTYPE_QUEUESERVER, 0, 0, pchQueueDefName, 0, iMapKey, uVoteKickPlayerID);
					}
				}
				break;
			}
		}
	}
}

// checks for a specific partition
static void gslQueue_CheckGroups(QueuePartitionInfo *pInfo, MapState *state)
{
	S32 iGroupIdx;
	U32 uCurrentTime = timeSecondsSince2000();
	bool bAllowAutoConcede = gslQueue_AllowAutoConcede(pInfo, uCurrentTime);
	QueueDef* pQueueDef = gslQueue_GetQueueDef(pInfo->iPartitionIdx);
	QueueMatch *pMatch = pInfo->pMatch;
	
	if (!pQueueDef)
	{
		return;
	}
	if (pMatch && mapState_GetScoreboardState(state) == kScoreboardState_Active)
	{
		for (iGroupIdx = eaSize(&pMatch->eaGroups)-1; iGroupIdx >= 0; iGroupIdx--)
		{
			QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];

			// Reset the leaver penalty due to timing, people, etc.
			// See if we have a leaver penalty (pulled from the partition which is set up based on the map being created and
			//  yanking things from a MapVariable called "QueueDef". If we do, 
			if (pInfo->pGameInfo && pInfo->pGameInfo->uLeaverPenaltyDuration &&
				pInfo->uQueueHadAnyPlayersTime && uCurrentTime >= pInfo->uQueueHadAnyPlayersTime + QUEUE_ALLOW_LEAVE_START_TIME &&
				eaSize(&pGroup->eaMembers) < pQueueDef->Settings.iLeaverPenaltyMinGroupMemberCount)
			{
				pInfo->pGameInfo->uLeaverPenaltyDuration = 0;
			}
			if (pQueueDef->MapRules.bAllowConcede)
			{
				gslQueue_UpdateConcedeVotes(pInfo, pGroup, uCurrentTime, bAllowAutoConcede);
			}
			if (pQueueDef->MapRules.bAllowVoteKick)
			{
				gslQueue_UpdateKickVotes(pInfo, pGroup, uCurrentTime);
			}
			if (pGroup->iGroupSize && !pInfo->uQueueHadAnyPlayersTime)
			{
				pInfo->uQueueHadAnyPlayersTime = timeSecondsSince2000();
			}
		}
	}
	else // If the mapstate isn't active or there is no match data, destroy all concede/kick attempts
	{
		S32 iConcedeIdx, iVoteKickIdx;
		for (iConcedeIdx = eaSize(&pInfo->eaConcedeData)-1; iConcedeIdx >= 0; iConcedeIdx--)
		{
			QueueGroupConcedeData* pData = pInfo->eaConcedeData[iConcedeIdx];
			iGroupIdx = queue_Match_FindGroupByIndex(pMatch, pData->iGroupIndex);
			if (pMatch && iGroupIdx >= 0)
			{
				gslQueue_UpdateClientVoteData(pInfo->iPartitionIdx, pMatch->eaGroups[iGroupIdx], 0, 0, 0, 0);
			}
			StructDestroy(parse_QueueGroupConcedeData, eaRemove(&pInfo->eaConcedeData, iConcedeIdx));
		}
		for (iVoteKickIdx = eaSize(&pInfo->eaVoteKickData)-1; iVoteKickIdx >= 0; iVoteKickIdx--)
		{
			QueueGroupKickData* pData = pInfo->eaVoteKickData[iVoteKickIdx];
			iGroupIdx = queue_Match_FindGroupByIndex(pMatch, pData->iGroupIndex);
			if (pMatch && iGroupIdx >= 0)
			{
				gslQueue_UpdateClientVoteData(pInfo->iPartitionIdx, pMatch->eaGroups[iGroupIdx], 0, 0, 0, 0);
			}
			StructDestroy(parse_QueueGroupKickData, eaRemove(&pInfo->eaVoteKickData, iVoteKickIdx));
		}
	}
}

static void gslQueue_UpdatePlayerQueueCooldown(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID QueueCooldownDef* pCooldownDef, bool bUpdateExisting)
{
	if (pEnt->pPlayer)
	{
		PlayerQueueCooldown* pPlayerCooldown = eaIndexedGetUsingString(&pEnt->pPlayer->eaQueueCooldowns, pCooldownDef->pchName);
		if (!pPlayerCooldown)
		{
			eaIndexedEnable(&pEnt->pPlayer->eaQueueCooldowns, parse_PlayerQueueCooldown);
			pPlayerCooldown = StructCreate(parse_PlayerQueueCooldown);
			pPlayerCooldown->pchCooldownDef = allocAddString(pCooldownDef->pchName);
			pPlayerCooldown->uStartTime = timeSecondsSince2000();
			eaPush(&pEnt->pPlayer->eaQueueCooldowns, pPlayerCooldown);
		}
		else if (bUpdateExisting)
		{
			pPlayerCooldown->uStartTime = timeSecondsSince2000();
		}
	}
}

static bool gslQueue_UpdatePlayerQueueLeaverData(SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_OP_VALID QueuePartitionInfo* pInfo)
{
	bool bDirty = false;
	if (pEnt && pEnt->pPlayer)
	{
		if (pInfo && pInfo->pGameInfo && pInfo->pGameInfo->uLeaverPenaltyDuration)
		{
			U32 uLeaverPenaltyDuration = pInfo->pGameInfo->uLeaverPenaltyDuration;
			if (!pEnt->pPlayer->pPlayerQueueInfo)
			{
				pEnt->pPlayer->pPlayerQueueInfo = StructCreate(parse_PlayerQueueInfo);
				bDirty = true;
			}
			if (pEnt->pPlayer->pPlayerQueueInfo->uLeaverPenaltyDuration != uLeaverPenaltyDuration)
			{
				pEnt->pPlayer->pPlayerQueueInfo->uLeaverPenaltyDuration = uLeaverPenaltyDuration;
				bDirty = true;
			}
		}
		else if (pEnt->pPlayer->pPlayerQueueInfo && pEnt->pPlayer->pPlayerQueueInfo->uLeaverPenaltyDuration)
		{
			pEnt->pPlayer->pPlayerQueueInfo->uLeaverPenaltyDuration = 0;
			bDirty = true;
		}
		if (bDirty)
		{
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}	
	}
	return bDirty;
}

bool gslQueue_VoteKick(Entity* pEnt, U32 uPlayerID)
{
	S32 iGroupIdx, iVoteKickIdx;
	U32 uCurrentTime = timeSecondsSince2000();
	QueueGroupKickData* pData = NULL;
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(entGetPartitionIdx(pEnt));
	const char* pchQueueDefName = SAFE_MEMBER2(pInfo, pGameInfo, pchQueueDef);
	QueueDef* pQueueDef = pchQueueDefName ? queue_DefFromName(pchQueueDefName) : NULL;

	if (!pEnt || !pEnt->pPlayer || !pQueueDef || !pQueueDef->MapRules.bAllowVoteKick)
	{
		return false;
	}
	if (!uPlayerID || uPlayerID == entGetContainerID(pEnt))
	{
		return false;
	}
	if (!pInfo->pMatch || mapState_GetScoreboardState(mapState_FromEnt(pEnt)) != kScoreboardState_Active)
	{
		return false;
	}
	if (uCurrentTime < pInfo->uQueueHadAnyPlayersTime + g_QueueConfig.uAllowVoteKickStartTime)
	{
		return false;
	}
	iGroupIdx = gslQueue_GetPlayerGroupIndex(pEnt);
	if (iGroupIdx < 0)
	{
		return false;
	}
	if (pInfo->pMatch->eaGroups[iGroupIdx]->iGroupSize <= 2)
	{
		return false;
	}
	for (iVoteKickIdx = eaSize(&pInfo->eaVoteKickData)-1; iVoteKickIdx >= 0; iVoteKickIdx--)
	{
		pData = pInfo->eaVoteKickData[iVoteKickIdx];
		if (pData->iGroupIndex == pInfo->pMatch->eaGroups[iGroupIdx]->iGroupIndex)
		{
			break;
		}
	}
	if (iVoteKickIdx < 0)
	{
		pData = StructCreate(parse_QueueGroupKickData);
		pData->iGroupIndex = pInfo->pMatch->eaGroups[iGroupIdx]->iGroupIndex;
		eaPush(&pInfo->eaVoteKickData, pData);
	}
	if (pData->uNextAllowedVoteKickTime > uCurrentTime)
	{
		return false;
	}
	if (!pData->uTimeout && !pData->uVoteKickPlayerID)
	{
		Entity* pEntKick = entFromContainerID(pInfo->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, uPlayerID);
		if (pEntKick)
		{
			pData->uTimeout = uCurrentTime + g_QueueConfig.uVoteKickTimeout;
			pData->uVoteKickPlayerID = uPlayerID;

			gslQueue_SendVoteWarning(pInfo->iPartitionIdx,
									 pInfo->pMatch->eaGroups[iGroupIdx]->pGroupDef, 
									 pEnt,
									 pEntKick,
									 "QueueServer_VoteKick",
									 g_QueueConfig.uVoteKickTimeout);
		}
		else
		{
			return false;
		}
	}
	else if (uPlayerID != pData->uVoteKickPlayerID)
	{
		return false;
	}
	if (ea32Find(&pData->puiEntIDs, entGetContainerID(pEnt)) < 0)
	{
		ea32Push(&pData->puiEntIDs, entGetContainerID(pEnt));
		gslQueue_UpdateClientVoteData(pInfo->iPartitionIdx,
									  pInfo->pMatch->eaGroups[iGroupIdx],
									  kQueueVoteType_VoteKick,
									  entGetContainerID(pEnt),
									  uPlayerID,
									  ea32Size(&pData->puiEntIDs));
	}
	return true;
}

static void gslQueue_SetPlayerScoreActive(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID QueuePartitionInfo *pInfo)
{
	ScoreboardPartitionInfo* pScoreboardInfo = gslScoreboard_PartitionInfoFromIdx(pInfo->iPartitionIdx);
	if (pScoreboardInfo)
	{
		ScoreboardEntity *pScore = eaIndexedGetUsingInt(&pScoreboardInfo->Scores.eaScoresList, pEnt->myContainerID);
		if (pScore)
		{
			pScore->bActive = true;
		}
	}
}

static void gslQueue_UpdatePlayer(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID QueuePartitionInfo *pInfo)
{
	if (mapState_GetScoreboardState(mapState_FromEnt(pEnt)) == kScoreboardState_Final)
	{
		const char* pchQueueDefName = SAFE_MEMBER2(pInfo, pGameInfo, pchQueueDef);
		QueueDef* pDef = pchQueueDefName ? queue_DefFromName(pchQueueDefName) : NULL;
		QueueCooldownDef* pCooldownDef = pDef ? queue_CooldownDefFromName(pDef->pchCooldownDef) : NULL;

		if (pCooldownDef)
		{
			// Make sure that the player gets his cooldown updated at the end of the match
			gslQueue_UpdatePlayerQueueCooldown(pEnt, pCooldownDef, false);
		}
		if (g_QueueConfig.bEnableMissionReturnAtEnd)
		{
			U32 uLogoutTime = g_QueueConfig.uMissionReturnLogoutTime;

			// If there is an override kick time on the QueueDef, use that
			if (pDef && pDef->Settings.uMissionReturnLogoutTime)
			{
				uLogoutTime = pDef->Settings.uMissionReturnLogoutTime;
			}
			mechanics_LogoutTimerStartEx(pEnt, LogoutTimerType_MissionReturn, uLogoutTime);
		}
	}
	if(entCheckFlag(pEnt, ENTITYFLAG_IGNORE) == 0)
	{
		// Don't add players to the local teams until they are on map (this prevents players leaving the map from being added again).
		gslQueue_UpdateEntGroupProperties(pEnt,pInfo);
	}
	gslQueue_SetPlayerScoreActive(pEnt, pInfo);
	gslQueue_UpdatePlayerQueueLeaverData(pEnt, pInfo);
}

const char *pvp_GetPlayerSpawnPoint(Entity *pEnt)
{
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(entGetPartitionIdx(pEnt));

	if(pInfo && pInfo->pMatch)
	{
		int i;

		for(i=0;i<eaSize(&pInfo->pMatch->eaGroups);i++)
		{
			if(queue_Match_FindMemberInGroup(pInfo->pMatch->eaGroups[i],pEnt->myContainerID) != -1)
			{
				QueueGroupDef *pGroupDef = pInfo->pMatch->eaGroups[i]->pGroupDef;

				return pGroupDef->pchSpawnTargetName;
			}
		}
	}

	return NULL;
}

static void gslQueue_UpdateAllEnts(void)
{
	EntityIterator* iter;
	Entity* currEnt;
	int i;

	// Clear group sizes
	for(i=0;i<eaSize(&g_ppQueuePartitionInfos);i++)
	{
		QueuePartitionInfo *pInfo = g_ppQueuePartitionInfos[i];
		gslQueue_ClearMatchGroupSizes(pInfo->pMatch);
	}

	//Don't ignore ignored entities for the purpose of this loop
	// need to send this info to the queue server for state validation
	iter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	while ((currEnt = EntityIteratorGetNext(iter)))
	{
		QueuePartitionInfo *info = gslQueuePartitionInfoFromIdx(entGetPartitionIdx(currEnt));

		if(!info || !currEnt->pPlayer || !currEnt->pChar)
			continue;

		if(isProductionMode() && currEnt && currEnt->pPlayer && currEnt->pPlayer->bIsGM)
			continue;

		ANALYSIS_ASSUME(currEnt != NULL && info != NULL);
		gslQueue_UpdatePlayer(currEnt, info);
	}
	EntityIteratorRelease(iter);
}

static void gslQueue_UpdateMapLeaderboard(QueuePartitionInfo *info, ScoreboardPartitionInfo* pScoreboardInfo)
{
	S32 iIdx;
	U32 iCurrentTime = timeSecondsSince2000();

	if(!info || !info->pGameInfo || !pScoreboardInfo)
		return;

	info->leaderboard_counter = 0;
	if(!info->pLeaderboard)
	{
		U32 uPartitionID = partition_IDFromIdx(info->iPartitionIdx);
		info->pLeaderboard = StructCreate(parse_MapLeaderboard);
		info->pLeaderboard->iMapKey = queue_GetMapKey(gServerLibState.containerID, uPartitionID);
	}

	for(iIdx = eaSize(&pScoreboardInfo->Scores.eaScoresList)-1; iIdx >= 0; iIdx--)
	{
		ScoreboardEntity *pScore = eaGet(&pScoreboardInfo->Scores.eaScoresList, iIdx);
		if(pScore)
		{
			PlayerLeaderboard *pPlayerLB = eaIndexedGetUsingInt(&info->pLeaderboard->eaEntities, pScore->iEntID);
			if(!pPlayerLB)
			{
				pPlayerLB = StructCreate(parse_PlayerLeaderboard);
				pPlayerLB->iEntID = pScore->iEntID;
				eaPush(&info->pLeaderboard->eaEntities, pPlayerLB);
			}

			pPlayerLB->bOnMap = !!pScore->bActive;
			if(!!pScore->bActive)
				pPlayerLB->iLastTimeOnMap = iCurrentTime;
		}
	}

	info->iLastScoreSize = eaSize(&pScoreboardInfo->Scores.eaScoresList);

	//Push the scores to the queue server
	RemoteCommand_aslQueue_UpdateMapLeaderboard(GLOBALTYPE_QUEUESERVER, 0, info->pGameInfo->pchQueueDef, info->pLeaderboard);
}


static void pvp_UpdatePartition(QueuePartitionInfo *info, MapState *state, F32 fTick)
{	
	ScoreboardPartitionInfo* pScoreboardInfo = gslScoreboard_PartitionInfoFromIdx(info->iPartitionIdx);

	if(!info)
		return;
	
	PERFINFO_AUTO_START_FUNC();

	pScoreboardInfo = gslQueue_InitializeScores(info, pScoreboardInfo);
	
	gslQueue_CheckGroups(info, state);

	//Simple leaderboard/state info pushed to the queue server.  I don't really like it but it'll have to do for now
	if (pScoreboardInfo)
	{
		if(info->pGameInfo && info->pGameInfo->pchQueueDef &&
		   mapState_GetScoreboardState(state) < kScoreboardState_Final &&
		   (eaSize(&pScoreboardInfo->Scores.eaScoresList) != info->iLastScoreSize ||
			info->leaderboard_counter++ > 15))
		{
			gslQueue_UpdateMapLeaderboard(info, pScoreboardInfo);
		}
	}

	PERFINFO_AUTO_STOP();
}

// These functions ping the Queue Server periodically and are essentially what holds the map
//   open. If this stops sending info, the map will be put into a Limbo state.
static void qpve_UpdatePartition(QueuePartitionInfo *info, MapState *state, F32 fTick)
{	
	ScoreboardPartitionInfo* pScoreboardInfo = gslScoreboard_PartitionInfoFromIdx(info->iPartitionIdx);

	if(!info)
		return;

	PERFINFO_AUTO_START_FUNC();

	pScoreboardInfo = gslQueue_InitializeScores(info, pScoreboardInfo);
	
	gslQueue_CheckGroups(info, state);

	if (pScoreboardInfo)
	{
		if(info->pGameInfo && info->pGameInfo->pchQueueDef &&
			(eaSize(&pScoreboardInfo->Scores.eaScoresList) != info->iLastScoreSize ||
			info->leaderboard_counter++ > 15))
		{
			gslQueue_UpdateMapLeaderboard(info, pScoreboardInfo);
		}
	}

	PERFINFO_AUTO_STOP();
}

void queue_PartitionLoad(int iPartitionIdx)
{
	PERFINFO_AUTO_START_FUNC();

	if (gslQueue_IsQueueMap())
	{
		QueuePartitionInfo *pInfo = StructCreate(parse_QueuePartitionInfo);
		pInfo->iPartitionIdx = iPartitionIdx;
		if (!queue_AddPartitionInfo(pInfo))
		{
			StructDestroySafe(parse_QueuePartitionInfo, &pInfo);
		}
		gslQueue_ProcessPendingMatchUpdates(iPartitionIdx);
	}

	PERFINFO_AUTO_STOP();
}

void queue_PartitionUnload(int iPartitionIdx)
{
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);

	if (pInfo)
	{
		queue_DestroyPartitionInfo(pInfo);
	}
}

void queue_MapLoad(void)
{
	// On map load with full init, destroy and re-create all active partitions
	queue_MapUnload();
	partition_ExecuteOnEachPartition(queue_PartitionLoad);
}

void queue_MapUnload(void)
{
	// Destroy all partitions
	int i;
	for(i=eaSize(&g_ppQueuePartitionInfos)-1; i>=0; --i){
		queue_PartitionUnload(g_ppQueuePartitionInfos[i]->iPartitionIdx);
	}
}

bool queue_AddPartitionInfo(QueuePartitionInfo* pInfo)
{
	if (pInfo && !gslQueuePartitionInfoFromIdx(pInfo->iPartitionIdx))
	{
		if (!g_ppQueuePartitionInfos)
		{
			eaIndexedEnable(&g_ppQueuePartitionInfos,parse_QueuePartitionInfo);
		}
		eaIndexedAdd(&g_ppQueuePartitionInfos, pInfo);
		return true;
	}
	return false;
}

bool queue_DestroyPartitionInfo(QueuePartitionInfo* pInfo)
{
	if (pInfo && eaFindAndRemove(&g_ppQueuePartitionInfos, pInfo) >= 0)
	{
		StructDestroy(parse_QueuePartitionInfo, pInfo);
		return true;
	}
	return false;
}


static void gslQueue_UpdatePendingChatMessages(F32 fTime)
{
	const F32 fTimeout = 5.0f;
	S32 i;
	for (i = eaSize(&s_eaPendingChatMessages)-1; i >= 0; i--)
	{
		QueuePendingChatMessage* pPending = s_eaPendingChatMessages[i];
		Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pPending->uEntID);
		bool bForceUpdate = false;
		bool bSuccess = true;

		pPending->fTimePending += fTime;

		if (pEnt)
		{
			bSuccess = gslQueue_SendSystemMessageToChannel(pEnt, 
														   pPending->pchChannel,
														   pPending->pchMessageKey,
														   pPending->uSubjectID,
														   false,
														   pPending->fTimePending > fTimeout);
		}
		if (!pEnt || bSuccess)
		{
			StructDestroy(parse_QueuePendingChatMessage, eaRemove(&s_eaPendingChatMessages, i));
		}
	}
}



#define QUEUE_LOCAL_TEAM_TICK_RATE 3.0f

// This runs off a different tick than the queueTick.
//   For some reason bhanka claims it has to run after the gslPVPTick in GameServerMain.c.
//   I am skeptical of this, but will leave it for now.
void gslQueue_LocalTeamTick(F32 fElapsed)
{
	if(gslQueue_IsQueueMap())
	{
		static F32 s_fTimer = 0.f;
		S32 i;

		s_fTimer += fElapsed;
		if(s_fTimer > QUEUE_LOCAL_TEAM_TICK_RATE)
		{
			s_fTimer -= QUEUE_LOCAL_TEAM_TICK_RATE;

			for(i=0;i<eaSize(&g_ppQueuePartitionInfos);i++)
			{
				QueuePartitionInfo *pInfo = g_ppQueuePartitionInfos[i];
	
				gslQueue_LocalTeamUpdate(pInfo);
			}
		}
	}
}

void queue_TickQueues(F32 fTime)
{
	static F32 fTotalTime = 0;
	int i;
	ZoneMapType eMapType = zmapInfoGetMapType(NULL);

	fTotalTime += fTime;

	if (fTotalTime > 1.0f)
	{
		bRandomQueuesUpdated = false;
		
		if(eMapType == ZMTYPE_PVP || eMapType == ZMTYPE_QUEUED_PVE)
		{
			gslQueue_UpdateAllEnts();
			
			for(i=0;i<eaSize(&g_ppQueuePartitionInfos);i++)
			{
				QueuePartitionInfo *pInfo = g_ppQueuePartitionInfos[i];

				if (!pInfo->bQueueServerInstanceInfoRequested)
				{
					gslQueue_RequestServerInstanceInfo(pInfo);
					pInfo->bQueueServerInstanceInfoRequested=true;
				}
				else if (pInfo->bQueueServerInstanceInfoReceived)
				{
					if(eMapType == ZMTYPE_QUEUED_PVE)
					{
						qpve_UpdatePartition(pInfo,mapState_FromPartitionIdx(pInfo->iPartitionIdx),fTotalTime);
					}
					else
					{
						pvp_UpdatePartition(pInfo,mapState_FromPartitionIdx(pInfo->iPartitionIdx),fTotalTime);
					}
				}
			}
		}

		// PVE used to do this before the UpdatePartition loop. PvP always had it after.
		gslQueue_ProcessPlayerQueues();

		fTotalTime = 0.0f;
	}

	if (eaSize(&s_eaPendingChatMessages))
	{
		gslQueue_UpdatePendingChatMessages(fTime);
	}
}

bool gslQueue_CanSidekick(void)
{
	if (zmapInfoGetMapType(NULL) == ZMTYPE_PVP)
		return false;
	else
		return true;
}

// Remove any cooldown timers on the player that have expired whenever the player logs into a map
static bool gslQueue_RemoveExpiredPlayerCooldowns(Entity* pEnt)
{
	U32 uCurrentTime = timeSecondsSince2000();
	bool bDirty = false;
	int i;
	for (i = eaSize(&pEnt->pPlayer->eaQueueCooldowns)-1; i >= 0; i--)
	{
		PlayerQueueCooldown* pPlayerCooldown = pEnt->pPlayer->eaQueueCooldowns[i];
		QueueCooldownDef* pCooldownDef = queue_CooldownDefFromName(pPlayerCooldown->pchCooldownDef);
		if (!pCooldownDef || uCurrentTime >= pPlayerCooldown->uStartTime + pCooldownDef->uCooldownTime)
		{
			StructDestroy(parse_PlayerQueueCooldown, eaRemove(&pEnt->pPlayer->eaQueueCooldowns, i));
			bDirty = true;
		}
	}
	return bDirty;
}

void gslQueue_ent_CheckQueueState(Entity *pEnt)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);
//	ZoneMapType eMapType;

	PERFINFO_AUTO_START_FUNC();

	gslQueue_UpdatePlayerQueueLeaverData(pEnt, pInfo);

	if (gslQueue_RemoveExpiredPlayerCooldowns(pEnt))
	{
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}

//	eMapType = zmapInfoGetMapType(NULL);
//	if (!gslQueue_IsQueueMap() || eaiFind(&g_QueueConfig.peAllowQueuingOnQueueMaps, eMapType) >= 0)
	// Always do this because we want the info to be sent to the GameServer/Client when a member first moves to the instance map.
	{
		RemoteCommand_aslQueue_CheckQueueState(GLOBALTYPE_QUEUESERVER, 0, entGetContainerID(pEnt));
	}

	PERFINFO_AUTO_STOP();
}

static void gslQueue_ReportMatchResultForPlayer(Entity* pEnt, const char* pchWinnerFaction)
{
	S32 i;
	QueuePartitionInfo *pQueueInfo = gslQueuePartitionInfoFromIdx(entGetPartitionIdx(pEnt));

	for (i = eaSize(&pQueueInfo->pMatch->eaGroups)-1; i >= 0; i--)
	{
		QueueGroup* pGroup = pQueueInfo->pMatch->eaGroups[i];
		if (!pGroup->pGroupDef)
		{
			continue;
		}
		if (queue_Match_FindMemberInGroup(pGroup, entGetContainerID(pEnt)) >= 0)
		{
			MissionInfo* pInfo = mission_GetInfoFromPlayer(pEnt);
			OpenMission* pOpenMission = pInfo ? openmission_GetFromName(entGetPartitionIdx(pEnt),pInfo->pchCurrentOpenMission) : NULL;
			MissionDef* pMissionDef = pOpenMission ?  mission_GetDef(pOpenMission->pMission) : NULL;
			const char* pchMissionCategory = pMissionDef ? REF_STRING_FROM_HANDLE(pMissionDef->hCategory) : NULL;
			
			if (stricmp(REF_STRING_FROM_HANDLE(pGroup->pGroupDef->hFaction), pchWinnerFaction)==0)
			{
				eventsend_RecordPvPQueueMatchResult(pEnt, kPvPQueueMatchResult_Win, pchMissionCategory);
			}
			else
			{
				eventsend_RecordPvPQueueMatchResult(pEnt, kPvPQueueMatchResult_Loss, pchMissionCategory);
			}
			break;
		}
	}
}

static void gslQueue_ReportMatchResult(QueuePartitionInfo* pInfo, const char* pchWinnerFaction)
{
	if (pInfo->pMatch && pchWinnerFaction && pchWinnerFaction[0])
	{
		Entity* pEnt;
		EntityIterator* iter = entGetIteratorSingleType(pInfo->iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
		while ((pEnt = EntityIteratorGetNext(iter)))
		{
			if (pEnt->pPlayer && pEnt->pChar)
			{
				gslQueue_ReportMatchResultForPlayer(pEnt, pchWinnerFaction);
			}
		}
		EntityIteratorRelease(iter);
	}
}

static int SortByMetric(const ScoreboardMetricEntry **a, const ScoreboardMetricEntry **b)
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

static int getTotalMetrics(Entity *pPlayerEnt)
{
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

static void gslQueue_SendScoreboardEvents(QueuePartitionInfo* pInfo)
{
	MapState *pState = mapState_FromPartitionIdx(pInfo->iPartitionIdx);
	EntityIterator* iter = NULL;
	Entity* pEnt = NULL;
	ScoreboardMetricEntry **eaMetricEntries = NULL;
	const char *pchMetricName = NULL;
	int i = 0;
	int j;
	int total = 0;
	bool bMetricsLeft = true;

	while (bMetricsLeft)
	{
		eaClearStruct(&eaMetricEntries, parse_ScoreboardMetricEntry);
		iter = entGetIteratorSingleType(pInfo->iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);

		while ((pEnt = EntityIteratorGetNext(iter)))
		{
			if (pEnt)
			{
				total = getTotalMetrics(pEnt);

				if ( i >= total)
				{
					bMetricsLeft = false;
					break;
				}
			}
			if (pEnt->pPlayer && pEnt->pChar)
			{
				ScoreboardMetricEntry *pMetricScore = StructCreate(parse_ScoreboardMetricEntry);
				PlayerMapValues *pPlayerValues = eaIndexedGetUsingInt(&pState->pPlayerValueData->eaPlayerValues, entGetContainerID(pEnt));
				MapStateValue *pValue = pPlayerValues->eaValues[i];

				pMetricScore->iEntID = entGetContainerID(pEnt);

				if(pValue)
				{
					pMetricScore->iMetricValue = MultiValGetInt(&pValue->mvValue, NULL);
					pchMetricName = pValue->pcName;
				}

				eaPush(&eaMetricEntries, pMetricScore);
			}
		}
		EntityIteratorRelease(iter);

		if (i < total)
		{
			eaQSort(eaMetricEntries, SortByMetric);

			for (j = 0; j < eaSize(&eaMetricEntries); ++j)
			{
				ScoreboardMetricEntry *pEntry = (eaMetricEntries)[j];

				if (pEntry)
				{
					if (j > 0)
					{
						if (eaMetricEntries[j - 1])
						{
							if (pEntry->iMetricValue == eaMetricEntries[j - 1]->iMetricValue)
							{
								pEntry->iRank = eaMetricEntries[j - 1]->iRank;
							}
							else
							{
								pEntry->iRank = j + 1;
							}
						}
						else
						{
							pEntry->iRank = j + 1;
						}
					}
					else
					{
						pEntry->iRank = j + 1;
					}
					eventsend_RecordScoreboardMetricFinish(entFromContainerID(pInfo->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pEntry->iEntID), pchMetricName, pEntry->iRank);
				}
			}
		}
		else
		{
			bMetricsLeft = false;
		}

		i++;
	}

	eaDestroyStruct(&eaMetricEntries, parse_ScoreboardMetricEntry);
}

static void gslQueue_ApplyCooldowns(QueuePartitionInfo *pInfo)
{
	const char* pchQueueDefName = SAFE_MEMBER(pInfo->pGameInfo, pchQueueDef);
	QueueDef* pDef = queue_DefFromName(pchQueueDefName);
	QueueCooldownDef* pCooldownDef = pDef ? queue_CooldownDefFromName(pDef->pchCooldownDef) : NULL;
	if (pCooldownDef)
	{
		Entity* pEnt;
		EntityIterator* pIter = entGetIteratorSingleType(pInfo->iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
		while ((pEnt = EntityIteratorGetNext(pIter)))
		{
			gslQueue_UpdatePlayerQueueCooldown(pEnt, pCooldownDef, true);
		}
		EntityIteratorRelease(pIter);
	}
}

void gslQueue_MapSetStateFromScoreboardStateEx(int iPartitionIdx, ScoreboardState eScoreboardState, const char* pchWinnerFaction)
{
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);
	const char* pchQueueDefName = SAFE_MEMBER2(pInfo, pGameInfo, pchQueueDef);

	// NOTE: ScoreboardState doesn't necessarily have anything to do with scoreboards. It's the MapState and is poorly named.
	//  In any case, we care about the MapState becoming Active or Final. So the name doesn't really matter. Just don't be
	//  too confused.

	if(pchQueueDefName && gslQueue_IsQueueMap())
	{
		if (eScoreboardState==kScoreboardState_Active)
		{
			S64 iMapKey = queue_GetMapKey(gServerLibState.containerID, partition_IDFromIdx(iPartitionIdx));

			// Request the QueueServer take the map active if it has launched
			RemoteCommand_aslQueue_MapRequestActive(GLOBALTYPE_QUEUESERVER, 0, pchQueueDefName, iMapKey);
		}
		else if (eScoreboardState==kScoreboardState_Final)
		{
			S64 iMapKey = queue_GetMapKey(gServerLibState.containerID, partition_IDFromIdx(iPartitionIdx));

			//Inform the queue server that we are finished
			RemoteCommand_aslQueue_MapFinish(GLOBALTYPE_QUEUESERVER, 0, pchQueueDefName, iMapKey);

			if (g_QueueConfig.bDisbandAutoTeamsWhenMatchEnds)
			{	//Turns off local teams when the map is finished so the team containers get destroyed
				pInfo->bAutoTeamsEnabled = false;
			}

			// Report the winner and loser of the match
			gslQueue_ReportMatchResult(pInfo, pchWinnerFaction);

			gslQueue_SendScoreboardEvents(pInfo);

			// If applicable, put the queue into cooldown for each player on the map
			gslQueue_ApplyCooldowns(pInfo);

			// Remove the leaver penalty duration
			if (pInfo->pGameInfo)
			{
				pInfo->pGameInfo->uLeaverPenaltyDuration = 0;
			}
		}
	}
}

// Prevent the QueueServer from doing further matchmaking on this map.
// Some sort of work-around implemented for UI on STO for people getting stuck to private queues.
// Could possibly be refactored into Abandon tech.
void gslQueue_StopTrackingMapOnQueueServer(int iPartitionIdx)
{
	QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);
	const char* pchQueueDefName = SAFE_MEMBER2(pInfo, pGameInfo, pchQueueDef);

	if (pchQueueDefName && gslQueue_IsQueueMap())
	{
		S64 iMapKey = queue_GetMapKey(gServerLibState.containerID, partition_IDFromIdx(iPartitionIdx));

		// Set the map state as finished. May not actually stop tracking if g_QueueConfig.bMaintainQueueTrackingOnMapFinish is on
		RemoteCommand_aslQueue_MapFinish(GLOBALTYPE_QUEUESERVER, 0, pchQueueDefName, iMapKey);
	}
}

// Special handling for map transfer failures
void gslQueue_HandleMapTransferFailure(ContainerID uPlayerID)
{
	RemoteCommand_aslQueue_ForceLeaveActiveInstance(GLOBALTYPE_QUEUESERVER, 0, uPlayerID);
}

static void QueueDefsReloadFix(void)
{
	EntityIterator *piter = entGetIteratorSingleTypeAllPartitions(0,0, GLOBALTYPE_ENTITYPLAYER);
	Entity *pEnt = NULL;

	while(pEnt = EntityIteratorGetNext(piter))
	{
		if(pEnt->pPlayer && pEnt->pPlayer->pPlayerQueueInfo)
		{
			eaDestroyStruct(&pEnt->pPlayer->pPlayerQueueInfo->eaQueues, parse_PlayerQueue);
			
			gslQueue_ForcePlayerQueuesProcess(pEnt);
		}
	}

	EntityIteratorRelease(piter);
}

// Reload QueueDefs top level callback
static void QueueDefsReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading Queues...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	QueueDefsReloadFix();

	ParserReloadFileToDictionaryWithFlags(pchRelPath,g_hQueueDefDict,PARSER_OPTIONALFLAG);

	//Post?

	loadend_printf(" done (%d queues)", RefSystem_GetDictionaryNumberOfReferents(g_hQueueDefDict));
}

static void QueueDefReloadCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, void *pResource, void *pUserData)
{
	QueueDef *pdef = pResource;
	switch(eType)
	{
	case RESEVENT_RESOURCE_PRE_MODIFIED:
	case RESEVENT_RESOURCE_REMOVED:
	case RESEVENT_RESOURCE_ADDED:
	case RESEVENT_RESOURCE_MODIFIED:
		QueueDefsReloadFix();
		break;
	}
}

AUTO_STARTUP(Queues) ASTRT_DEPS(AS_Messages, Missions, QueueCategories, QueueCooldowns, CharacterClasses, WorldLibZone);
void ASTRT_Queues_Load(void)
{
	Queues_Load(QueueDefReloadCB, QueueDefsReload);
	Queues_LoadConfig();
}

AUTO_RUN_LATE;
int gslQueue_RegisterCopyDict(void)
{
	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_QUEUEINFO), false, parse_QueueInfo, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_QUEUEINFO), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	return(1);
}


#include "gslQueue_h_ast.c"
