/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CmdParse.h"
#include "Entity.h"
#include "EntityLib.h"
#include "GameStringFormat.h"
#include "gslNotify.h"
#include "gslMission.h"
#include "ItemCommon.h"
#include "Message.h"
#include "mission_common.h"
#include "NotifyCommon.h"
#include "Player.h"

#include "autogen/GameClientLib_AutoGen_ClientCmdWrappers.h"
#include "autogen/mission_common_h_ast.h"


// ----------------------------------------------------------------------------------
// Notify Remote Commands
// ----------------------------------------------------------------------------------


AUTO_COMMAND_REMOTE;
void notify_RemoteSendNotification(const char *pcMessageKey, NotifyType eType, CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		Message *pMessage = RefSystem_ReferentFromString(gMessageDict, pcMessageKey);
		if (pEnt && pMessage) {
			ClientCmd_NotifySend(pEnt, eType, entTranslateMessage(pEnt, pMessage), NULL, NULL);
		}
	}
}

AUTO_COMMAND_REMOTE;
void notify_RemoteSendTutorialNotification(const char *pcMessageKey, TutorialScreenRegion eScreenRegion, const char *pchTexture, NotifyType eType, CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		Message *pMessage = RefSystem_ReferentFromString(gMessageDict, pcMessageKey);
		if (pEnt && pMessage) {
			ClientCmd_NotifySend(pEnt, eType, entTranslateMessage(pEnt, pMessage), StaticDefineIntRevLookup(TutorialScreenRegionEnum, eScreenRegion), pchTexture);
		}
	}
}


// This sends an error message to the client as float text
AUTO_COMMAND_REMOTE;
void notify_RemoteSendItemNotification(const char *pcMessageKey, const char *pcItemName, NotifyType eType, CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, pcItemName);

		if (pEnt && pcMessageKey) {
			char *estrBuffer = NULL;
			estrStackCreate(&estrBuffer);

			entFormatGameMessageKey(pEnt, &estrBuffer, pcMessageKey,
				STRFMT_ENTITY_KEY("Entity", pEnt), STRFMT_ITEMDEF(pItemDef), STRFMT_END);

			if (estrBuffer && estrBuffer[0]) {
				ClientCmd_NotifySend(pEnt, eType, estrBuffer, pItemDef->pchName, pItemDef->pchIconName);
			}

			estrDestroy(&estrBuffer);
		}
	}
}

AUTO_COMMAND_REMOTE;
void notify_SetUGCMissionInfoReviewData(const char *pcMissionName, U32 uLastMissionRatingRequestID, bool bLastMissionPlayingAsBetaReviewer, CmdContext *pContext)
{
	Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
	MissionDef *pMissionDef = RefSystem_ReferentFromString(g_MissionDictionary, pcMissionName);
	MissionInfo *pInfo = NULL;

	// If this could result in a review (or report), keep track of it on the player's mission info so we can verify that a review is allowable and know if it is a beta review.
	if(pEnt && pMissionDef && (pInfo = mission_GetInfoFromPlayer(pEnt)))
	{
		pInfo->uLastMissionRatingRequestID = uLastMissionRatingRequestID;
		pInfo->bLastMissionPlayingAsBetaReviewer = bLastMissionPlayingAsBetaReviewer;
		mission_FlagInfoAsDirty(pInfo);
	}
}

AUTO_COMMAND_REMOTE;
void notify_RemoteSendMissionNotification(const char *pcMessageKey, const char *pcMissionName, NotifyType eType, CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER)
	{
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		MissionDef *pMissionDef = RefSystem_ReferentFromString(g_MissionDictionary, pcMissionName);
		if (pEnt && pMissionDef) {
			notify_SendMissionNotification(pEnt, NULL, pMissionDef, pcMessageKey, eType);
		}
	}
}


