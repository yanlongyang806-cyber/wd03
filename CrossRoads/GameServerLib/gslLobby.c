/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslLobby.h"
#include "LobbyCommon.h"
#include "AutoGen/LobbyCommon_h_ast.h"
#include "Entity.h"
#include "ActivityCommon.h"
#include "gslActivity.h"
#include "gslQueue.h"
#include "StringCache.h"
#include "queue_common.h"
#include "progression_common.h"
#include "progression_transact.h"
#include "mission_common.h"
#include "Reward.h"
#include "WebRequestServer/wrContainerSubs.h"
#include "gslMission.h"
#include "gslMission_transact.h"
#include "gslInteraction.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_STARTUP(GameContentNodes) ASTRT_DEPS(LobbyCommon, AS_GameProgression, Activities, Queues, Missions);
void gameContentNode_Load(void)
{
	gameContentNode_LoadRecommendedContent();
}

static bool gslLobby_EligibleForQueue(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_VALID QueueDef *pQueueDef)
{
	S32 iPlayerLevel = entity_GetSavedExpLevel(pEntity);
	bool bMatchingLevelBandFound = eaSize(&pQueueDef->eaLevelBands) == 0;

	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pQueueDef->eaLevelBands, QueueLevelBand, pLevelBand)
	{
		if ((pLevelBand->iMinLevel == 0 || iPlayerLevel >= pLevelBand->iMinLevel) &&
			(pLevelBand->iMaxLevel == 0 || iPlayerLevel <= pLevelBand->iMaxLevel))
		{
			bMatchingLevelBandFound = true;
			break;
		}
	}
	FOR_EACH_END

	return bMatchingLevelBandFound && gslEntCannotUseQueue(pEntity, pQueueDef, false, false, false) == QueueCannotUseReason_None;
}

// Returns the recommended content for the level given
RecommendedPlayerContentForLevel * gameContentNode_GetRecommendedPlayerContentForLevel(SA_PARAM_NN_VALID RecommendedPlayerContent *pContentList, S32 iLevel)
{
	S32 iNumLevels = eaSize(&pContentList->ppRecommendedPlayerContentForLevels);
	if (iNumLevels)
	{
		S32 iFoundIndex;
		if (iLevel <= pContentList->ppRecommendedPlayerContentForLevels[0]->iLevel)
		{
			return pContentList->ppRecommendedPlayerContentForLevels[0];
		}

		if (iLevel >= pContentList->ppRecommendedPlayerContentForLevels[iNumLevels - 1]->iLevel)
		{
			return pContentList->ppRecommendedPlayerContentForLevels[iNumLevels - 1];
		}
		iFoundIndex = eaIndexedFindUsingInt(&pContentList->ppRecommendedPlayerContentForLevels, iLevel);

		if (iFoundIndex >= 0)
		{
			return pContentList->ppRecommendedPlayerContentForLevels[iFoundIndex];
		}
		else
		{
			// This part can be turned into a binary search to find the correct position if this becomes a bottle neck
			S32 i;
			for (i = 0; i < iNumLevels; i++)
			{
				if (pContentList->ppRecommendedPlayerContentForLevels[i]->iLevel > iLevel)
				{
					break;
				}
			}
			return pContentList->ppRecommendedPlayerContentForLevels[i - 1];
		}
	}
	return NULL;
}

static void gslLobby_ChooseBestRecommendedContentFromEventList(SA_PARAM_NN_VALID Entity *pEntity,
	SA_PARAM_NN_STR const char *pchListName,
	SA_PARAM_NN_VALID const RecommendedPlayerContentForLevel * pLevelContent,
	SA_PARAM_NN_VALID PlayerSpecificRecommendedContent ***pppPlayerSpecificRecommendedContentOut)
{
	EventDef *pEarliestEventDef = NULL;
	U32 uEarliestEventStartDate = UINT_MAX;
	U32 uEventClockTime = gslActivity_GetEventClockSecondsSince2000();
	S32 i;
	
	for (i = 0; i < eaSize(&pLevelContent->ppContent); i++)
	{
		GameContentNodeRef *pContentRef = pLevelContent->ppContent[i];
		EventDef *pEventDef;

		devassert(pContentRef->eType == GameContentNodeType_Event);

		pEventDef = EventDef_Find(pContentRef->pchEventName);

		if (pEventDef)
		{
			QueueDef *pQueueDef = GET_REF(pEventDef->hLinkedQueue);

			if (pQueueDef==NULL || gslLobby_EligibleForQueue(pEntity, pQueueDef))
			{
				// Eligible for the queue. Check the times.

				U32 uNextOrCurrentStart = 0;
				U32 uEventEndDate = 0;
				U32 uEventLastStart;
				U32 uEventEndOfLastStart;
				U32 uEventNextStart;

				// This only pays attention to the actual def and does not take into consideration the iRunMode of the EventDef:
				ShardEventTiming_GetUsefulTimes(&(pEventDef->ShardTimingDef), uEventClockTime, &uEventLastStart, &uEventEndOfLastStart, &uEventNextStart);

				// We either want the start time of the currently running event, or the start time of the next time it will run.
				if (uEventClockTime < uEventEndOfLastStart)
				{
					uNextOrCurrentStart = uEventLastStart;
				}
				else
				{
					uNextOrCurrentStart = uEventNextStart;
				}

				if (uNextOrCurrentStart < uEarliestEventStartDate)
				{
					EventWarpDef *pWarpDef = EventDef_GetWarpForAllegiance(pEventDef, SAFE_GET_REF(pEntity, hAllegiance));
					if (pWarpDef == NULL || Event_CanPlayerUseWarp(pWarpDef, pEntity, false))
					{
						pEarliestEventDef = pEventDef;
						uEarliestEventStartDate = uNextOrCurrentStart;

					}
				}
			}
		}
	}

	if (pEarliestEventDef)
	{
		PlayerSpecificRecommendedContent *pPlayerSpecificRecommendedContent = StructCreate(parse_PlayerSpecificRecommendedContent);
		GameContentNode *pContentNode = &pPlayerSpecificRecommendedContent->contentNode;
		EventWarpDef *pWarpDef = EventDef_GetWarpForAllegiance(pEarliestEventDef, SAFE_GET_REF(pEntity, hAllegiance));

		// Set the content list name
		pPlayerSpecificRecommendedContent->pchRecommendedContentListName = allocAddString(pchListName);

		// Add to the output list
		eaPush(pppPlayerSpecificRecommendedContentOut, pPlayerSpecificRecommendedContent);
		
		// Set the content node reference
		pContentNode->contentRef.eType = GameContentNodeType_Event;
		pContentNode->contentRef.pchEventName = allocAddString(pEarliestEventDef->pchEventName);

		// Set the time sensitive info
		pContentNode->pTimeSensitiveContentInfo = StructCreate(parse_TimeSensitiveGameContentNodeInfo);
		{
			U32 uEventLastStart;
			U32 uEventEndOfLastStart;
			U32 uEventNextStart;

			// -Get a start time/end time off the start date we found. This guarantees we get the correct end date even if we are looking at a start date in the future.
			// -This only pays attention to the actual def and does not take into consideration the iRunMode of the EventDef:
			ShardEventTiming_GetUsefulTimes(&(pEarliestEventDef->ShardTimingDef), uEarliestEventStartDate, &uEventLastStart, &uEventEndOfLastStart, &uEventNextStart);

			pContentNode->pTimeSensitiveContentInfo->uStartDate = uEventLastStart;
			pContentNode->pTimeSensitiveContentInfo->uEndDate = uEventEndOfLastStart;
			pContentNode->pTimeSensitiveContentInfo->bActive = gslEvent_IsActive(pEarliestEventDef) && 
																	(uEventClockTime >= uEventLastStart && uEventClockTime < uEventEndOfLastStart);
		}
		COPY_HANDLE(pContentNode->pTimeSensitiveContentInfo->hQueue, pEarliestEventDef->hLinkedQueue);
		if (pWarpDef)
		{
			pContentNode->pTimeSensitiveContentInfo->pchSpawnMap = allocAddString(pWarpDef->pchSpawnMap);
		}

		// Set the metadata for the UI
		pContentNode->strDisplayName = StructAllocString(TranslateDisplayMessage(pEarliestEventDef->msgDisplayName));
		pContentNode->strSummary = StructAllocString(TranslateDisplayMessage(pEarliestEventDef->msgDisplayLongDesc));
		pContentNode->pchArtFileName = allocAddString(pEarliestEventDef->pchBackground);
	}
}

static void gslLobby_ChooseBestRecommendedContentFromQueueList(SA_PARAM_NN_STR const char *pchListName,
	SA_PARAM_NN_VALID Entity *pEntity,
	SA_PARAM_NN_VALID const RecommendedPlayerContentForLevel * pLevelContent,
	SA_PARAM_NN_VALID PlayerSpecificRecommendedContent ***pppPlayerSpecificRecommendedContentOut)
{
	S32 i;

	for (i = 0; i < eaSize(&pLevelContent->ppContent); i++)
	{
		GameContentNodeRef *pContentRef = pLevelContent->ppContent[i];
		QueueDef *pQueueDef;

		devassert(pContentRef->eType == GameContentNodeType_Queue);

		pQueueDef = GET_REF(pContentRef->hQueue);

		if (pQueueDef && gslLobby_EligibleForQueue(pEntity, pQueueDef))
		{
			PlayerSpecificRecommendedContent *pPlayerSpecificRecommendedContent = StructCreate(parse_PlayerSpecificRecommendedContent);
			GameContentNode *pContentNode = &pPlayerSpecificRecommendedContent->contentNode;

			// Set the content list name
			pPlayerSpecificRecommendedContent->pchRecommendedContentListName = allocAddString(pchListName);

			// Add to the output list
			eaPush(pppPlayerSpecificRecommendedContentOut, pPlayerSpecificRecommendedContent);

			// Set the content node reference
			pContentNode->contentRef.eType = GameContentNodeType_Queue;
			SET_HANDLE_FROM_REFERENT(g_hQueueDefDict, pQueueDef, pContentNode->contentRef.hQueue);

			// Set the metadata for the UI
			pContentNode->strDisplayName = StructAllocString(TranslateDisplayMessage(pQueueDef->displayNameMesg));
			pContentNode->strSummary = StructAllocString(TranslateDisplayMessage(pQueueDef->descriptionMesg));
			pContentNode->pchArtFileName = allocAddString(pQueueDef->pchIcon);

			return;
		}
	}
}

static void gslLobby_ChooseBestRecommendedContentFromStory(SA_PARAM_NN_STR const char *pchStoryName,
	SA_PARAM_NN_VALID Entity *pEntity,
	SA_PARAM_NN_VALID PlayerSpecificRecommendedContent ***pppPlayerSpecificRecommendedContentOut)
{
	// Get the story root node
	GameProgressionNodeDef *pRootNodeDef = progression_NodeDefFromName(pchStoryName);

	if (pRootNodeDef)
	{
		GameProgressionNodeDef *pCurrentNodeDef;
		S32 iPlayerLevel = entity_GetSavedExpLevel(pEntity);

		devassert(pRootNodeDef->eFunctionalType == GameProgressionNodeFunctionalType_StoryRoot);

		pCurrentNodeDef = progression_FindLeftMostLeaf(pRootNodeDef);

		while (pCurrentNodeDef)
		{
			if (progression_ProgressionNodeUnlocked(pEntity, pCurrentNodeDef) &&
				!progression_ProgressionNodeCompleted(pEntity, pCurrentNodeDef) &&
				pCurrentNodeDef->pMissionGroupInfo &&
				(pCurrentNodeDef->pMissionGroupInfo->iRequiredPlayerLevel == 0 || pCurrentNodeDef->pMissionGroupInfo->iRequiredPlayerLevel == iPlayerLevel))
			{
				// Unlocked and not completed by the player. We also match the exact level if there is a level requirement.
				PlayerSpecificRecommendedContent *pPlayerSpecificRecommendedContent = StructCreate(parse_PlayerSpecificRecommendedContent);
				GameContentNode *pContentNode = &pPlayerSpecificRecommendedContent->contentNode;

				// Set the content list name
				pPlayerSpecificRecommendedContent->pchRecommendedContentListName = allocAddString(pchStoryName);

				// Add to the output list
				eaPush(pppPlayerSpecificRecommendedContentOut, pPlayerSpecificRecommendedContent);

				// Fill the meta-data
				gameContentNode_FillFromGameProgressionNode(pContentNode, pCurrentNodeDef);

				break;
			}
			// Go to the next node in the story
			pCurrentNodeDef = GET_REF(pCurrentNodeDef->hNextSibling);
		}
	}		
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE; 
void gslLobby_GetHomePageContent(Entity *pEntity)
{
	HomePageContentInfo *pHomePageInfo = StructCreate(parse_HomePageContentInfo);
	S32 i;
	S32 iPlayerLevel = entity_GetSavedExpLevel(pEntity);

	for (i = 0; i < eaSize(&g_LobbyConfig.ppchHomePageRecommendedContentLists); i++)
	{
		const char *pchContentListName = g_LobbyConfig.ppchHomePageRecommendedContentLists[i];

		// Get the content list
		RecommendedPlayerContent *pContentList = gameContentNode_RecommendedPlayerContentFromName(pchContentListName);

		if (pContentList)
		{
			// Get the content for the player level
			RecommendedPlayerContentForLevel *pLevelContent = gameContentNode_GetRecommendedPlayerContentForLevel(pContentList, iPlayerLevel);

			if (pLevelContent)
			{
				if (eaSize(&pLevelContent->ppContent) > 0)
				{
					if (pLevelContent->ppContent[0]->eType == GameContentNodeType_Event)
					{
						// Handle an event list
						gslLobby_ChooseBestRecommendedContentFromEventList(pEntity, pchContentListName, pLevelContent, &pHomePageInfo->ppPlayerSpecificRecommendedContent);
					}
					else if (pLevelContent->ppContent[0]->eType == GameContentNodeType_Queue)
					{
						// Handle a queue list
						gslLobby_ChooseBestRecommendedContentFromQueueList(pchContentListName, pEntity, pLevelContent, &pHomePageInfo->ppPlayerSpecificRecommendedContent);
					}
				}				
			}		
		}
	}

	if (g_LobbyConfig.pchStoryNameForRecommendedProgressionNodes &&
		g_LobbyConfig.pchStoryNameForRecommendedProgressionNodes[0])
	{
		gslLobby_ChooseBestRecommendedContentFromStory(g_LobbyConfig.pchStoryNameForRecommendedProgressionNodes, 
			pEntity, 
			&pHomePageInfo->ppPlayerSpecificRecommendedContent);
	}

	// Send the results to the client
	ClientCmd_gclLobbyReceiveHomePageContentInfo(pEntity, pHomePageInfo);

	StructDestroy(parse_HomePageContentInfo, pHomePageInfo);
}

GameContentNodeRewardResult * gslLobby_GetMissionRewards(Entity *pEnt, GameContentNodeRef *pNodeRef)
{
	GameContentNodeRewardResult *pResult = StructCreate(parse_GameContentNodeRewardResult);

	// Copy the game content node reference
	StructCopyAll(parse_GameContentNodeRef, pNodeRef, &pResult->nodeRef);

	if (IS_HANDLE_ACTIVE(pNodeRef->hNode))
	{
		// Progression node rewards
		GameProgressionNodeDef *pProgressionNodeDef = GET_REF(pNodeRef->hNode);

		if (pProgressionNodeDef && 
			pProgressionNodeDef->pMissionGroupInfo &&
			pProgressionNodeDef->pMissionGroupInfo->bShowRewardsInUI)
		{
			// Iterate through all required missions
			FOR_EACH_IN_CONST_EARRAY_FORWARDS(pProgressionNodeDef->pMissionGroupInfo->eaMissions, GameProgressionMission, pProgressionMission)
			{
				if (!pProgressionMission->bOptional)
				{
					MissionDef *pMissionDef = missiondef_FindMissionByName(NULL, pProgressionMission->pchMissionName);

					if (pMissionDef)
					{
						U32 seed;
						MissionCreditType eAllowedCreditType;
						MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pEnt);
						Mission *pMission = pMissionInfo ? mission_GetMissionFromDef(pMissionInfo, pMissionDef) : NULL;
						RecruitType eRecruitType= kRecruitType_None;
						RewardContextData rewardContextData = {0};

						GameContentNodeMissionReward *pMissionReward = StructCreate(parse_GameContentNodeMissionReward);

						// Set the mission handle
						SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pMissionReward->hMissionDef);						

						// Push it to the list of rewards
						eaPush(&pResult->eaMissionRewards, pMissionReward);

						if (entGetPartitionIdx(pEnt) == PARTITION_UNINITIALIZED)
						{
							pEnt->iPartitionIdx_UseAccessor = WEBREQUEST_PARTITION_INDEX;
						}						

						// Can this mission be offered to the player at all
						if (!missiondef_CanBeOfferedAtAll(pEnt, pMissionDef, NULL, NULL, &eAllowedCreditType))
						{
							eAllowedCreditType = MissionCreditType_Ineligible;
						}

						// Set the credit type
						pMissionReward->eCreditType = eAllowedCreditType;

						rewardContextData.pEnt = pEnt;

						eRecruitType = GetRecruitTypes(pEnt);

						// Get the seed for the reward
						seed = mission_GetRewardSeed(pEnt, pMission, pMissionDef);

						// Generate the mission rewards

						reward_GenerateMissionActionRewards(WEBREQUEST_PARTITION_INDEX, pEnt, pMissionDef, MissionState_TurnedIn, &pMissionReward->eaRewardBags, &seed,
							eAllowedCreditType, 0, /*time_level=*/0, 0, 0, eRecruitType, /*bUGCProject=*/false, false, false, false, false, /*bGenerateChestRewards=*/true, &rewardContextData, 0, /* RewardGatedDataInOut *pGatedData */ NULL);

						if (eAllowedCreditType == MissionCreditType_Primary)
						{
							// Display the rewards which come from the interactable overrides if the credit type is primary
							FOR_EACH_IN_CONST_EARRAY_FORWARDS(pMissionDef->ppInteractableOverrides, InteractableOverride, pOverride)
							{
								if (pOverride->bTreatAsMissionReward)
								{
									interaction_FillLootBag(pEnt, &pMissionReward->eaRewardBags, pOverride->pPropertyEntry);
								}
							}
							FOR_EACH_END
						}
					}
				}
			}
			FOR_EACH_END
		}
	}

	return pResult;
}
