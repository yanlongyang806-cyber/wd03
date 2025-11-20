/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "SuggestedContentCommon.h"
#include "AutoGen/SuggestedContentCommon_h_ast.h"
#include "Entity.h"
#include "Player.h"
#include "ActivityCommon.h"
#include "GameStringFormat.h"
#include "gslActivity.h"
#include "gslQueue.h"
#include "StringCache.h"
#include "queue_common.h"
#include "mission_common.h"
#include "progression_common.h"
#include "progression_transact.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_STARTUP(SuggestedContent) ASTRT_DEPS(AS_GameProgression, Activities, Queues);
void gslSuggestedContent_Load(void)
{
	suggestedContent_LoadSuggestedContent();
}

static bool gslSuggestedContent_EligibleForQueue(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_VALID QueueDef *pQueueDef)
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

// Returns the suggested content for the level given
SuggestedContentForLevel * gslSuggestedContent_GetSuggestedContentForLevel(SA_PARAM_NN_VALID SuggestedContentList *pContentList, S32 iLevel)
{
	S32 iNumLevels = eaSize(&pContentList->ppSuggestedContentForLevels);
	if (iNumLevels)
	{
		S32 iFoundIndex;
		if (iLevel <= pContentList->ppSuggestedContentForLevels[0]->iLevel)
		{
			return pContentList->ppSuggestedContentForLevels[0];
		}

		if (iLevel >= pContentList->ppSuggestedContentForLevels[iNumLevels - 1]->iLevel)
		{
			return pContentList->ppSuggestedContentForLevels[iNumLevels - 1];
		}
		iFoundIndex = eaIndexedFindUsingInt(&pContentList->ppSuggestedContentForLevels, iLevel);

		if (iFoundIndex >= 0)
		{
			return pContentList->ppSuggestedContentForLevels[iFoundIndex];
		}
		else
		{
			// This part can be turned into a binary search to find the correct position if this becomes a bottle neck
			S32 i;
			for (i = 0; i < iNumLevels; i++)
			{
				if (pContentList->ppSuggestedContentForLevels[i]->iLevel > iLevel)
				{
					break;
				}
			}
			return pContentList->ppSuggestedContentForLevels[i - 1];
		}
	}
	return NULL;
}


static void gslSuggestedContent_ChooseBestContent(
	SA_PARAM_NN_VALID Entity *pEntity,
	SA_PARAM_NN_STR const char *pchListName,
	SA_PARAM_NN_VALID const SuggestedContentForLevel * pLevelContent,
	SA_PARAM_NN_VALID SuggestedContentInfo ***peaContentInfo)
{
	EventDef *pBestEventDef = NULL;
	QueueDef *pBestQueueDef = NULL;
	SuggestedContentType eBestType = SuggestedContentType_None;
	U32 uBestEventStartDate = UINT_MAX;
	U32 uQueryTime = gslActivity_GetEventClockSecondsSince2000();
	S32 i;
	

	// Loop through the content trying to determine what is best for us.
	// We want the thing that is earliest, or any queue that has no event limiter
	for (i = 0; i < eaSize(&pLevelContent->ppContent); i++)
	{
		SuggestedContentNode *pContentNode = pLevelContent->ppContent[i];
		QueueDef *pQueueDef=NULL;
		EventDef *pEventDef=NULL;

		if (pContentNode->eType == SuggestedContentType_Event)
		{
			pEventDef = EventDef_Find(pContentNode->pchContentEventName);
			if (pEventDef)
			{
				pQueueDef = GET_REF(pEventDef->hLinkedQueue);
				// May return NULL if there is no queue
			}
		}
		else if (pContentNode->eType == SuggestedContentType_Queue)
		{
			pQueueDef = GET_REF(pContentNode->hContentQueue);
			if (pQueueDef!=NULL)
			{
				pEventDef = EventDef_Find(pQueueDef->Requirements.pchRequiredEvent);
			}
		}

		if (pEventDef==NULL)
		{
			if (pQueueDef!=NULL)
			{
				// Eventless queue. Best thing automatically!
				uBestEventStartDate=0;
				pBestEventDef=NULL;
				pBestQueueDef=pQueueDef;
				eBestType = pContentNode->eType;
				break;
			}

			// Neither queue nor event. What is it? 
			continue;
		}

		// Okay. We have an eventdef for sure
		{
			U32 uNextOrCurrentStart = 0;
			U32 uEventLastStart;
			U32 uEventEndOfLastStart;
			U32 uEventNextStart;

			// This only pays attention to the actual def and does not take into consideration the iRunMode of the EventDef:
			ShardEventTiming_GetUsefulTimes(&(pEventDef->ShardTimingDef), uQueryTime, &uEventLastStart, &uEventEndOfLastStart, &uEventNextStart);

			// We either want the start time of the currently running event, or the start time of the next time it will run.
			if (uQueryTime < uEventEndOfLastStart)
			{
				uNextOrCurrentStart = uEventLastStart;
			}
			else
			{
				uNextOrCurrentStart = uEventNextStart;
			}

			if (uNextOrCurrentStart < uBestEventStartDate)
			{
				pBestEventDef = pEventDef;
				uBestEventStartDate = uNextOrCurrentStart;
				pBestQueueDef=pQueueDef;
				eBestType = pContentNode->eType;
			}
		}
	}

	//////////////////////////////////////

	// We are done looking through everything. Set up return data if we found something
	
	if (pBestEventDef!=NULL || pBestQueueDef!=NULL)
	{
		// We have something
		SuggestedContentInfo *pSuggestedContentInfo = StructCreate(parse_SuggestedContentInfo);

		// Add to the output list
		eaPush(peaContentInfo, pSuggestedContentInfo);

		// Fill out the contents

		pSuggestedContentInfo->eType = eBestType;
		pSuggestedContentInfo->strListName = allocAddString(pchListName);

		// QUEUE

		if (pBestQueueDef!=NULL)
		{
			// Generic Queue data
			SET_HANDLE_FROM_REFERENT(g_hQueueDefDict, pBestQueueDef, pSuggestedContentInfo->hQueue);
			pSuggestedContentInfo->eCannotUseQueueReason=gslEntCannotUseQueue(pEntity, pBestQueueDef, false, false, false);

			// Set up the cannot use display message
			if (pSuggestedContentInfo->eCannotUseQueueReason!=QueueCannotUseReason_None)
			{
				char *estrBuffer = NULL;
				estrStackCreate(&estrBuffer);
				entFormatGameMessageKey(pEntity, &estrBuffer, gslGetCannotUseQueueReasonMsgKey(pSuggestedContentInfo->eCannotUseQueueReason),
					STRFMT_DISPLAYMESSAGE("Queue", pBestQueueDef->displayNameMesg),
					STRFMT_ENTITY_KEY("Subject", pEntity),
					STRFMT_STRING("Event", ""),		// Not sure how this should be supported. We may have an event to use. It may be the wrong one.
					STRFMT_TARGET(pEntity),
					STRFMT_END);

				pSuggestedContentInfo->CannotUseDisplayMessage = StructAllocString(estrBuffer);
				estrDestroy(&estrBuffer);
			}
			else
			{
				pSuggestedContentInfo->CannotUseDisplayMessage = allocAddString("");
			}

			// Set the metadata for the UI if we are queue type
			if (eBestType == SuggestedContentType_Queue)
			{
				pSuggestedContentInfo->strDisplayName = StructAllocString(TranslateDisplayMessage(pBestQueueDef->displayNameMesg));
				pSuggestedContentInfo->strSummary = StructAllocString(TranslateDisplayMessage(pBestQueueDef->descriptionMesg));
				pSuggestedContentInfo->pchArtFileName = allocAddString(pBestQueueDef->pchIcon);
			}
		}
		else
		{
			pSuggestedContentInfo->eCannotUseQueueReason=QueueCannotUseReason_None;
			pSuggestedContentInfo->CannotUseDisplayMessage = StructAllocString("");
		}

		// EVENT

		if (pBestEventDef!=NULL)
		{
			EventWarpDef *pWarpDef = EventDef_GetWarpForAllegiance(pBestEventDef, SAFE_GET_REF(pEntity, hAllegiance));

			// Generic Event data
			pSuggestedContentInfo->pchEventName = allocAddString(pBestEventDef->pchEventName);
			pSuggestedContentInfo->bEventIsValidForPlayer = (pWarpDef == NULL || Event_CanPlayerUseWarp(pWarpDef, pEntity, false));
			
			// Time Info
			{
				U32 uEventLastStart;
				U32 uEventEndOfLastStart;
				U32 uEventNextStart;

				// -Get a start time/end time off the start date we found. This guarantees we get the correct end date even if we are looking at a start date in the future.
				// -This only pays attention to the actual def and does not take into consideration the iRunMode of the EventDef:
				ShardEventTiming_GetUsefulTimes(&(pBestEventDef->ShardTimingDef), uBestEventStartDate, &uEventLastStart, &uEventEndOfLastStart, &uEventNextStart);

				pSuggestedContentInfo->uStartDate = uEventLastStart;
				pSuggestedContentInfo->uEndDate = uEventEndOfLastStart;
				pSuggestedContentInfo->bEventActive = gslEvent_IsActive(pBestEventDef) && 
																	(uQueryTime >= uEventLastStart && uQueryTime < uEventEndOfLastStart);
			}

			// Set the metadata for the UI if we are event type
			if (eBestType == SuggestedContentType_Event)
			{
				pSuggestedContentInfo->strDisplayName = StructAllocString(TranslateDisplayMessage(pBestEventDef->msgDisplayName));
				pSuggestedContentInfo->strSummary = StructAllocString(TranslateDisplayMessage(pBestEventDef->msgDisplayLongDesc));
				pSuggestedContentInfo->pchArtFileName = allocAddString(pBestEventDef->pchBackground);
			}
		}
		else
		{
			pSuggestedContentInfo->uEndDate = 0xffffffff;
			pSuggestedContentInfo->bEventIsValidForPlayer = true;
		}
	}
}


static void gslSuggestedContent_ChooseBestSuggestedStoryContent(
	SA_PARAM_NN_VALID Entity *pEntity,
	SA_PARAM_NN_STR const char *pchListName,
	SA_PARAM_NN_VALID const SuggestedContentForLevel * pLevelContent,
	SA_PARAM_NN_VALID SuggestedContentInfo ***peaContentInfo)
{

	int i;
	S32 iPlayerLevel = entity_GetSavedExpLevel(pEntity);
	
	for (i = 0; i < eaSize(&pLevelContent->ppContent); i++)
	{
		SuggestedContentNode *pContentNode = pLevelContent->ppContent[i];

		// Ignore non-story stuff
		if (pContentNode->eType == SuggestedContentType_StoryNode)
		{
			GameProgressionNodeDef *pRootNodeDef = progression_NodeDefFromName(pContentNode->pchContentStoryName);

			if (pRootNodeDef)
			{
				GameProgressionNodeDef *pCurrentNodeDef;

				pCurrentNodeDef = pRootNodeDef;

				if (pCurrentNodeDef->pMissionGroupInfo!=NULL)
				{
					for (i = 0; i < eaSize(&pCurrentNodeDef->pMissionGroupInfo->eaMissions); i++)
					{
						GameProgressionMission *pProgMission = pCurrentNodeDef->pMissionGroupInfo->eaMissions[i];
						CompletedMission *pCompletedMission = eaIndexedGetUsingString(&pEntity->pPlayer->missionInfo->completedMissions, pProgMission->pchMissionName);

						if (pCompletedMission==NULL)
						{
							// Got an uncompleted one
							MissionDef *pMissionDef = missiondef_DefFromRefString(pProgMission->pchMissionName);
							
							// We have something
							SuggestedContentInfo *pSuggestedContentInfo = StructCreate(parse_SuggestedContentInfo);

							// Add to the output list
							eaPush(peaContentInfo, pSuggestedContentInfo);

							// Fill out the contents

							pSuggestedContentInfo->eType = SuggestedContentType_StoryNode;
							pSuggestedContentInfo->strListName = allocAddString(pchListName);
							pSuggestedContentInfo->pchStoryName = allocAddString(pContentNode->pchContentStoryName);

							if (pMissionDef!=NULL)
							{
								pSuggestedContentInfo->strDisplayName = StructAllocString(TranslateDisplayMessage(pMissionDef->displayNameMsg));
							}
						
							pSuggestedContentInfo->strSummary = StructAllocString(TranslateDisplayMessage(pProgMission->msgDescription));
							pSuggestedContentInfo->pchArtFileName = pProgMission->pchImage;
							return;
						}

					}
				}
			}
		}
	}		
}
			


AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE; 
void gslSuggestedContent_GetContent(Entity *pEntity, const char* pchContentListName)
{
	SuggestedContentInfo **eaContentInfo = NULL; // An eArray of content. Passed to the client, then destroyed
	S32 iLevel = entity_GetSavedExpLevel(pEntity);

	// Get the content list
	SuggestedContentList *pContentList = suggestedContent_SuggestedContentListFromName(pchContentListName);
	
	if (pContentList)
	{
		// Get the content for the  level
		SuggestedContentForLevel *pLevelContent = gslSuggestedContent_GetSuggestedContentForLevel(pContentList, iLevel);

		if (pLevelContent)
		{
			if (eaSize(&pLevelContent->ppContent) > 0)
			{
				// Don't like this. But it works for now. Assume we can have mixed queues and events
				// But stories are their own thing.

				if (pLevelContent->ppContent[0]->eType==SuggestedContentType_StoryNode)
				{
					gslSuggestedContent_ChooseBestSuggestedStoryContent(pEntity, pchContentListName, pLevelContent, &eaContentInfo);
				}
				else
				{
					gslSuggestedContent_ChooseBestContent(pEntity, pchContentListName, pLevelContent, &eaContentInfo);
				}
			}		
		}
	}

	// Send the first result to the client (if there is one)
	//    Not really happy about this. The old system put things into a wrapper function. I don't want to have to create one right now.
	//    In addition, it's odd that we attempt to build an array of content per list and yet are only interested in a single one.
	//    Perhaps the choose should only ever return a single, in which case the eaContentInfo array can become a single pointer.
	if (eaSize(&eaContentInfo) > 0)
	{
		ClientCmd_gclSuggestedContentReceiveInfo(pEntity, eaContentInfo[0]);
	}
//	ClientCmd_gclSuggestedContentReceiveInfo(pEntity, &eaContentInfo);

	eaDestroy(&eaContentInfo);
}

