/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ActivityCommon.h"
#include "chatCommonStructs.h"
#include "Entity.h"
#include "EntityLib.h"
#include "error.h"
#include "file.h"
#include "GameServerLib.h"
#include "gslActivity.h"
#include "logging.h"
#include "MicroTransactions.h"
#include "mission_common.h"
#include "Player.h"
#include "StringCache.h"
#include "utilitiesLib.h"

#include "AutoGen/ActivityCommon_h_ast.h"
#include "AutoGen/AppLocale_h_ast.h"
#include "AutoGen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/Player_h_ast.h"

static BulletinsStruct s_Bulletins = {0};
static EventDef** s_eaEventsWithBulletins = NULL;

static U32* s_puEntIDsRequestingBulletins = NULL;
static bool s_bRequestingBulletinUpdate = false;

static U32 s_uBulletinLoginUpdateTime = 10;
static U32 s_uBulletinPeriodicUpdateTime = 300;
AUTO_CMD_INT(s_uBulletinPeriodicUpdateTime, BulletinPeriodicUpdateTime) ACMD_CATEGORY(debug);

static bool gslBulletins_ValidateDef(BulletinDef* pDef)
{
	bool bRetVal = true;

	if (pDef->pchMicroTransDef)
	{
		if (!RefSystem_ReferentFromString(g_hMicroTransDefDict, pDef->pchMicroTransDef))
		{
			Errorf("BulletinDef: Invalid MicroTransDef specified '%s'", pDef->pchMicroTransDef);
			bRetVal = false;
		}
	}
	if (pDef->pEvent && pDef->pEvent->pchMissionDef)
	{
		if (!missiondef_FindMissionByName(NULL, pDef->pEvent->pchMissionDef))
		{
			Errorf("BulletinDef: Invalid MissionDef specified '%s'", pDef->pEvent->pchMissionDef);
			bRetVal = false;
		}
	}
	return bRetVal;
}

static bool gslBulletins_ValidateCategory(BulletinCategory* pCategory)
{
	bool bRetVal = true;

	if (pCategory->pchMicroTransDef)
	{
		if (!RefSystem_ReferentFromString(g_hMicroTransDefDict, pCategory->pchMicroTransDef))
		{
			Errorf("BulletinCategory '%s': Invalid MicroTransDef specified '%s'", 
				pCategory->pchName, pCategory->pchMicroTransDef);
			bRetVal = false;
		}
	}
	return bRetVal;
}

AUTO_STARTUP(Bulletins) ASTRT_DEPS(MicroTransactions, Missions);
void gslBulletins_Startup(void)
{
	if (isDevelopmentMode() || gbMakeBinsAndExit)
	{
		S32 i;
		ParserLoadFiles(NULL,
						"defs/config/Bulletins.def", 
						"Bulletins.bin", 
						PARSER_OPTIONALFLAG, 
						parse_BulletinsStruct, 
						&s_Bulletins);

		// Do basic validation
		for (i = eaSize(&s_Bulletins.eaDefs)-1; i >= 0; i--)
		{
			gslBulletins_ValidateDef(s_Bulletins.eaDefs[i]);
		}
		for (i = eaSize(&s_Bulletins.eaCategories)-1; i >= 0; i--)
		{
			gslBulletins_ValidateCategory(s_Bulletins.eaCategories[i]);
		}
	}
}

static BulletinMessage* gslBulletins_GetMessageFromArray(BulletinMessage*** peaMessages, Language eLanguage)
{
	S32 i, iDefaultIndex = -1;
	BulletinMessage* pResult;
	for (i = eaSize(peaMessages)-1; i >= 0; i--)
	{
		BulletinMessage* pMessage = (*peaMessages)[i];
		if (pMessage->eLanguage == eLanguage)
		{
			break;
		}
		else if (pMessage->eLanguage == LANGUAGE_ENGLISH)
		{
			iDefaultIndex = i;
		}
	}
	pResult = eaGet(peaMessages, i);
	if (!pResult)
	{
		pResult = eaGet(peaMessages, iDefaultIndex);
		log_printf(LOG_GSL, 
			"Bulletin not found for language (%s), defaulting to English.", 
			StaticDefineIntRevLookup(LanguageEnum, eLanguage));
	}
	return pResult;
}

static void gslBulletins_AddEventBulletinToList(BulletinDef*** peaDefs, 
												S32* piCount, 
												EventDef* pEventDef,
												U32 uStartTime,
												U32 uEndTime,
												Language eLanguage)
{
	const char* pchTitle;
	const char* pchBody;
	BulletinDef* pData;
	BulletinMessage* pMessage;

	// Create the bulletin
	pData = eaGetStruct(peaDefs, parse_BulletinDef, (*piCount)++);
	pData->uActivateTime = uStartTime - pEventDef->pBulletin->uPrecedeEventTime;
	pData->uIgnoreTime = uEndTime;

	// Create the bulletin message
	pMessage = eaGetStruct(&pData->eaMessages, parse_BulletinMessage, 0);
	pMessage->eLanguage = eLanguage;
	pchTitle = langTranslateDisplayMessage(eLanguage, pEventDef->pBulletin->msgTitle);
	pchBody = langTranslateDisplayMessage(eLanguage, pEventDef->pBulletin->msgMessageBody);
	StructCopyString(&pMessage->pchTranslatedTitle, pchTitle);
	StructCopyString(&pMessage->pchTranslatedString, pchBody);
				
	// Create the bulletin event
	if (!pData->pEvent)
		pData->pEvent = StructCreate(parse_BulletinEvent);
	pData->pEvent->uEventTime = uStartTime;
	pData->pEvent->pchTexture = allocAddString(pEventDef->pchIcon);
				
	// Create the bulletin event message
	pMessage = eaGetStruct(&pData->pEvent->eaMessages, parse_BulletinMessage, 0);
	pMessage->eLanguage = eLanguage;
	pchTitle = langTranslateDisplayMessage(eLanguage, pEventDef->msgDisplayName);
	pchBody = langTranslateDisplayMessage(eLanguage, pEventDef->msgDisplayLongDesc);
	StructCopyString(&pMessage->pchTranslatedTitle, pchTitle);
	StructCopyString(&pMessage->pchTranslatedString, pchBody);
}

static bool gslBulletins_AddToList(BulletinDef*** peaDefs, S32* piCount, BulletinDef* pDef, Language eLanguage)
{
	BulletinMessage* pMessage = gslBulletins_GetMessageFromArray(&pDef->eaMessages, eLanguage);
	if (pMessage)
	{
		if (peaDefs)
		{
			BulletinDef* pData;
			BulletinMessage* pEventMessage;
			BulletinMessage* pNewMessage;
			pData = eaGetStruct(peaDefs, parse_BulletinDef, (*piCount)++);
			pNewMessage = eaGetStruct(&pData->eaMessages, parse_BulletinMessage, 0);
			pData->uActivateTime = pDef->uActivateTime;
			StructCopyString(&pData->pchMicroTransDef, pDef->pchMicroTransDef);
			StructCopyString(&pData->pchLink, pDef->pchLink);
			StructCopyString(&pData->pchCategory, pDef->pchCategory);
			StructCopyAll(parse_BulletinMessage, pMessage, pNewMessage);
			StructDestroySafe(parse_BulletinEvent, &pData->pEvent);
			if (pDef->pEvent)
			{
				pEventMessage = gslBulletins_GetMessageFromArray(&pDef->pEvent->eaMessages, eLanguage);
				if (pEventMessage)
				{
					pData->pEvent = StructCreate(parse_BulletinEvent);
					pNewMessage = eaGetStruct(&pData->pEvent->eaMessages, parse_BulletinMessage, 0);
					pData->pEvent->uEventTime = pDef->pEvent->uEventTime;
					pData->pEvent->pchTexture = allocAddString(pDef->pEvent->pchTexture);
					StructCopyAll(parse_BulletinMessage, pEventMessage, pNewMessage);
				}
			}
		}
		return true;
	}
	return false;
}

static void gslBulletins_SendToClient(SA_PARAM_NN_VALID Entity* pEnt, bool bLogin)
{
	static BulletinsStruct BulletinsToSend = {0};
	Player* pPlayer = pEnt->pPlayer;
	S32 i, j, iCount = 0, iCatCount = 0;
	U32 uCurrentTime = gslActivity_GetEventClockSecondsSince2000();
	U32 uFinalBulletinTime = 0;
	U32 uBulletinTime = 0;
	Language eLanguage;
	char** ppchBulletinEventCategories = NULL;

	if (!pPlayer)
	{
		return;
	}
	if (bLogin)
	{
		uBulletinTime = pPlayer->uRecentBulletinTime;
	}
	eLanguage = entGetLanguage(pEnt);

	for (i = eaSize(&s_Bulletins.eaDefs)-1; i >= 0; i--)
	{
		BulletinDef* pDef = s_Bulletins.eaDefs[i];
		U32 uActivateOrEventTime = pDef->uActivateTime;

		if (pDef->uIgnoreTime && uCurrentTime >= pDef->uIgnoreTime)
		{
			continue;
		}
		if (pDef->pEvent && pDef->pEvent->uEventTime <= uCurrentTime)
		{
			uActivateOrEventTime = pDef->pEvent->uEventTime;
		}
		if (uActivateOrEventTime <= uCurrentTime && 
			uActivateOrEventTime > uBulletinTime)
		{
			if (uFinalBulletinTime < uActivateOrEventTime)
			{
				uFinalBulletinTime = uActivateOrEventTime;
			}

			if (pDef->pEvent && 
				eaFindString(&ppchBulletinEventCategories, pDef->pchCategory) < 0 &&
				gslBulletins_AddToList(NULL, NULL, pDef, eLanguage))
			{
				// Whenever bulletin events are shown, show all events of the same category
				for (j = eaSize(&s_Bulletins.eaDefs)-1; j >= i+1; j--)
				{
					BulletinDef* pCheckDef = s_Bulletins.eaDefs[j];
					if (pCheckDef->pEvent && pCheckDef->uActivateTime <= uCurrentTime &&
						stricmp(pCheckDef->pchCategory, pDef->pchCategory)==0)
					{
						gslBulletins_AddToList(&BulletinsToSend.eaDefs, &iCount, pCheckDef, eLanguage);
					}
				}
				eaPush(&ppchBulletinEventCategories, pDef->pchCategory);
			}
			gslBulletins_AddToList(&BulletinsToSend.eaDefs, &iCount, pDef, eLanguage);
		}
		else
		{
			break;
		}
	}
	// Add event bulletins to the list of bulletins to send
	for (i = eaSize(&s_eaEventsWithBulletins)-1; i >= 0; i--)
	{
		EventDef* pEventDef = s_eaEventsWithBulletins[i];
		if (pEventDef->pBulletin)
		{
			U32 uPrecedeEventTime = pEventDef->pBulletin->uPrecedeEventTime;
			
			U32 uStartTime = 0;
			U32 uEndTime = 0;
			U32 uActivateOrEventTime = 0xffffffff; /// Meaning we don't want to make a bulletin out of it.

			U32 uLastStart = 0;
			U32 uEndOfLastStart = 0;
			U32 uNextStart;			

			// We want a bulletin only for events that have not finished yet and which have started or will start withing PrecedeEventTime seconds

			// This only pays attention to the actual def and does not take into consideration the iRunMode of the EventDef.
			ShardEventTiming_GetUsefulTimes(&(pEventDef->ShardTimingDef), uCurrentTime, &uLastStart, &uEndOfLastStart, &uNextStart);

			if (uEndOfLastStart <= uCurrentTime)
			{
				// We haven't happened or the most recent occurrence is finished. Use NextStart as our base.
				//   Unless NextStart is 0xffffffff which indicates there will never again be another start
				//   (and uActivateOrEventTime will remaing greater than the current time and we won't bulletin)
				if (uNextStart!=0xffffffff)
				{
					ShardEventTiming_GetUsefulTimes(&(pEventDef->ShardTimingDef), uNextStart, &uLastStart, &uEndOfLastStart, &uNextStart);
					uStartTime = uLastStart;
					uEndTime = uEndOfLastStart; // (will be 0xffffffff if it never ends after that)
					uActivateOrEventTime = uStartTime - uPrecedeEventTime;
				}
			}
			else
			{
				// We have happened at least once. LastStart should be accurate
				// We can use last start and end of last start
				uStartTime = uLastStart;
				uEndTime = uEndOfLastStart; // (will be 0xffffffff if we never end)
				uActivateOrEventTime = uStartTime;
			}

			// If the report time is later than the last update and is now or in the past make a bulletin
			if (uActivateOrEventTime > uBulletinTime && uActivateOrEventTime <= uCurrentTime)
			{
				// clean up endTime for the bulletin system.
				if (uEndTime==0xffffffff)
				{
					uEndTime = 0;
				}
				
				if (uFinalBulletinTime < uActivateOrEventTime)
				{
					uFinalBulletinTime = uActivateOrEventTime;
				}
				gslBulletins_AddEventBulletinToList(&BulletinsToSend.eaDefs, &iCount, pEventDef, uStartTime, uEndTime, eLanguage);
			}
		}
	}

	eaSetSizeStruct(&BulletinsToSend.eaDefs, parse_BulletinDef, iCount);

	// Add all of the bulletin categories to the data to send to the client
	for (i = 0; i < eaSize(&s_Bulletins.eaCategories); i++)
	{
		BulletinCategory* pCategory = s_Bulletins.eaCategories[i];
		BulletinMessage* pMessage = gslBulletins_GetMessageFromArray(&pCategory->eaMessages, eLanguage);
			
		if (pMessage)
		{
			BulletinCategory* pData;
			BulletinMessage* pNewMessage;
			pData = eaGetStruct(&BulletinsToSend.eaCategories, parse_BulletinCategory, iCatCount++);
			pNewMessage = eaGetStruct(&pData->eaMessages, parse_BulletinMessage, 0);
			StructCopyString(&pData->pchMicroTransDef, pCategory->pchMicroTransDef);
			StructCopyString(&pData->pchName, pCategory->pchName);
			pData->pchTexture = allocAddString(pCategory->pchTexture);
			StructCopyAll(parse_BulletinMessage, pMessage, pNewMessage);
		}
	}
	eaSetSizeStruct(&BulletinsToSend.eaCategories, parse_BulletinCategory, iCatCount);
	ClientCmd_gclReceiveBulletins(pEnt, &BulletinsToSend, bLogin);

	if (bLogin && uFinalBulletinTime > pPlayer->uRecentBulletinTime)
	{
		pPlayer->uRecentBulletinTime = uFinalBulletinTime;
		entity_SetDirtyBit(pEnt, parse_Player, pPlayer, false);
	}
	eaDestroy(&ppchBulletinEventCategories);
}

void gslBulletins_Update(Entity* pEnt, bool bForceUpdate)
{
	if (gConf.bEnableBulletins)
	{
		static U32 s_uLastUpdateTime = 0;
		U32 uCurrentTime = timeSecondsSince2000();
		bool bShouldUpdate = false;

		if (bForceUpdate || !s_Bulletins.uVersion)
		{
			bShouldUpdate = true;
		}
		else if (pEnt)
		{
			if (s_uLastUpdateTime + s_uBulletinLoginUpdateTime <= uCurrentTime)
			{
				bShouldUpdate = true;
			}
		}
		else if (s_uLastUpdateTime + s_uBulletinPeriodicUpdateTime <= uCurrentTime)
		{
			bShouldUpdate = true;
		}
		if (bShouldUpdate)
		{
			U32 uVersion = s_Bulletins.uVersion;
			U32 uServerID = gGSLState.gameServerDescription.baseMapDescription.containerID;
			RemoteCommand_aslUpdateBulletins(GLOBALTYPE_CHATSERVER, 0, uServerID, uVersion);
			s_bRequestingBulletinUpdate = true;
			s_uLastUpdateTime = uCurrentTime;

			if (pEnt)
			{
				eaiPushUnique(&s_puEntIDsRequestingBulletins, entGetContainerID(pEnt));
			}
		}
		else if (pEnt)
		{
			if (s_bRequestingBulletinUpdate)
			{
				eaiPushUnique(&s_puEntIDsRequestingBulletins, entGetContainerID(pEnt));
			}
			else
			{
				gslBulletins_SendToClient(pEnt, true);
			}
		}
	}
}

void gslBulletins_AddEventBulletin(EventDef* pEventDef)
{
	if (pEventDef && pEventDef->pBulletin)
	{
		if (!s_eaEventsWithBulletins)
			eaIndexedEnable(&s_eaEventsWithBulletins, parse_EventDef);

		eaPush(&s_eaEventsWithBulletins, pEventDef);
	}
}

AUTO_COMMAND_REMOTE;
void gslBulletins_ReceiveList(BulletinsStruct* pBulletins)
{
	int i;
	
	// Update bulletins
	if (pBulletins)
	{
		StructCopyAll(parse_BulletinsStruct, pBulletins, &s_Bulletins);
	}
	
	// Send updates to any players that made a request
	for (i = eaiSize(&s_puEntIDsRequestingBulletins)-1; i >= 0; i--)
	{
		ContainerID uEntID = s_puEntIDsRequestingBulletins[i];
		Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uEntID);
		if (pEnt)
		{
			gslBulletins_SendToClient(pEnt, true);
		}
	}
	eaiDestroy(&s_puEntIDsRequestingBulletins);
	s_bRequestingBulletinUpdate = false;
}

AUTO_COMMAND ACMD_NAME(BulletinsForceUpdate) ACMD_ACCESSLEVEL(9);
void gslBulletins_ForceUpdate(Entity* pEnt)
{
	gslBulletins_Update(pEnt, true);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslBulletins_RequestAll(Entity* pEnt)
{
	if (pEnt && gConf.bEnableBulletins)
	{
		gslBulletins_SendToClient(pEnt, false);
	}
}
