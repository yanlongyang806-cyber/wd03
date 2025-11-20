/***************************************************************************
*     Copyright (c) 2003-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "StringUtil.h"
#include "TextFilter.h"
#include "ui/ChatBubbles.h"

#include "GameClientLib.h"
#include "gclEntity.h"
#include "chatCommonStructs.h"
#include "ChatData.h"
#include "chat/gclChatLog.h"
#include "gclChatConfig.h"
#include "Entity_h_ast.h"
#include "gclUIGen.h"
#include "GraphicsLib.h"
#include "CharacterStatus.h"

#include "soundLib.h"
#include "Autogen/ChatData_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// checks the message text for tags and performs operation if found
// Current tags supported:
// <sound> - play a sound from the entity
//		Example: <sound path/to/sound/event>This is the message text
//
// For performance reasons, the assumption is that a tag will exist at the beginning of the message (and there will only be one)
// if it finds the tag, it attempts to play the sound and removes the tag from the input string by moving the pointer after the tag
// 
void ChatBubble_PerformMessageTagsForEntity(const char **ppchMessage, EntityRef hEntity)
{
	const char *pchMessage = *ppchMessage;

	// check for embedded tag(s)  (whitespace should be stripped by now)
	if( pchMessage && pchMessage[0] == '<')
	{
		static const char *pchSoundTagOpen = "<sound";
		static const char *pchSoundTagClose = ">";
		size_t iSoundTagLen = strlen(pchSoundTagOpen);

		if(strlen(pchMessage) > iSoundTagLen)
		{
			// check for sound tag
			if(!strnicmp(pchMessage, pchSoundTagOpen, iSoundTagLen))
			{
				const char *pchTagStart; 
				const char *pchTagEnd;

				pchTagStart = pchMessage + iSoundTagLen;
				
				while(*pchTagStart && *pchTagStart == ' ') pchTagStart++; // skip any leading spaces

				pchTagEnd = strstri(pchTagStart, pchSoundTagClose);
				if(pchTagEnd)
				{
					int iLength;

					// trim trailing spaces
					const char *pchEndTrimmed = pchTagEnd-1; // start one char before end
					while(pchEndTrimmed > pchTagStart && *pchEndTrimmed && *pchEndTrimmed == ' ') pchEndTrimmed--; 
					pchEndTrimmed++;

					iLength = pchEndTrimmed - pchTagStart; 
					if(iLength > 0)
					{
						char *pchSoundEvent = malloc(iLength+1);
						memcpy(pchSoundEvent, pchTagStart, sizeof(char) * iLength);
						pchSoundEvent[iLength] = '\0';

						sndPlayFromEntity(pchSoundEvent, hEntity, NULL, false);
						free(pchSoundEvent);
					}

					// now strip off the tag (even if the event does not exist)
					pchTagEnd += strlen(pchSoundTagClose);

					// strip off any leading whitespace after the <sound *> and before the real message
					while(isspace((unsigned char)*pchTagEnd))
						++pchTagEnd;

					*ppchMessage = pchTagEnd;
				}
			}
		}
	}
}

void ChatBubble_Say(EntityRef hEntity, const char *pchMessage, F32 fDuration, const char *pchBubble)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	ChatBubble ***peaBubbleStack;
	const S32 ciQueueLimit = 5;
	Entity *pEnt = entFromEntityRefAnyPartition(hEntity);
	if (!pEnt)
		return;

	if (!pEnt->pEntUI)
		pEnt->pEntUI = StructCreate(parse_EntityUI);

	peaBubbleStack = &pEnt->pEntUI->eaBubbles;



	if (eaSize(peaBubbleStack) < ciQueueLimit)
	{
		ChatBubble *pNewBubble;
		
		ChatBubble_PerformMessageTagsForEntity(&pchMessage, hEntity);

		if( !*pchMessage )
			return; // After stripping off the "<sound *>" there's nothing left, so don't show the bubble.

		pNewBubble = StructCreate(parse_ChatBubble);
		pNewBubble->uiStartTimeMs = gGCLState.totalElapsedTimeMs;
		pNewBubble->uiEndTimeMs = pNewBubble->uiStartTimeMs + (fDuration * 1000);
		pNewBubble->pchMessage = StructAllocString(pchMessage);

		// Censor profanity from "msg" text if the player wants profanity filtering, 
		// or by default if pConfig is somehow null
		if ( (!pConfig || pConfig->bProfanityFilter) && (pEnt->erOwner || pEnt->pPlayer) )
		{
			ReplaceAnyWordProfane( pNewBubble->pchMessage );
		}	

		eaPush(peaBubbleStack, pNewBubble);

		if (!pchBubble)
			pchBubble = "Default";

		SET_HANDLE_FROM_STRING("ChatBubbleDef", pchBubble, pNewBubble->hDef);
		if (!GET_REF(pNewBubble->hDef))
		{
			ErrorDetailsf("Entity %s", pEnt->debugName);
			Errorf("Tried to create a chat bubble with unknown bubble style %s", pchBubble);
		}
	}
}

bool ChatBubble_DrawFor(Entity *pEnt, F32 fZ)
{
	ChatBubble ***peaBubbleStack = (pEnt && pEnt->pEntUI) ? &pEnt->pEntUI->eaBubbles : NULL;
	GfxCameraController *pCamera = gfxGetActiveCameraController();
	bool bVisible;
	Vec2 v2EntPos;
	Vec3 v3EntPos;
	Vec3 v3CamPos;
	F32 fY;
	S32 i;
	bool bFirst = true;
	F32 fScreenDist;
	F32 fWorldDistSq;
	F32 yOffSet = gProjectGameClientConfig.fChatNameOffsetY;
	Entity *pPlayerEnt = entActivePlayerPtr();

	if (!peaBubbleStack || eaSize(peaBubbleStack) <= 0 || (*peaBubbleStack)[0]->uiEndTimeMs <= gGCLState.totalElapsedTimeMs)
		return false;

	if(pEnt == entActivePlayerPtr() || gGCLState.bCutsceneActive || entCheckFlag(pEnt, ENTITYFLAG_UNTARGETABLE) > 0 ||
		(pEnt && !exprTestOverheadFlag(pEnt, true, "Name")))
	{
		// keep the bubble close to the target as there is no overhead stuff in these cases
		yOffSet = gProjectGameClientConfig.fChatNormalOffsetY;
	}

	fScreenDist = entGetWindowScreenPosAndDist(pEnt, v2EntPos, yOffSet);
	gfxGetActiveCameraPos(v3CamPos);
	entGetPos(pEnt, v3EntPos);
	fWorldDistSq = distance3Squared(v3EntPos, v3CamPos);

	// if an entity is being sent but is far away from the camera, don't display it.
	// this happens a lot during cutscenes where ents near the player and near the
	// camera are both sent.
	if (fWorldDistSq >= SQR(ENTUI_MAX_FEET_FROM_CAMERA))
		return false;

	// Cutscenes only show messages for entities in the camera view
	if (gGCLState.bCutsceneActive)
	{
		int frustWidth, frustHeight;
		gfxGetActiveSurfaceSize(&frustWidth, &frustHeight);

		// If the entity's 2D position is outside the camera view, don't display its chat bubble
		if (fScreenDist < ENTUI_MIN_FEET_FROM_CAMERA)
			return false;
		if (v2EntPos[0] < 0 || v2EntPos[1] < 0 || v2EntPos[0] > frustWidth || v2EntPos[1] > frustHeight)
			return false;
	}

	bVisible = entIsVisible(pEnt);

	// If the entity isn't visible and is above/below it's probably behind a ceiling/floor.
	// But if we're in a cutscene, it's probably just behind a wall and we want to see it anyway.
	if (!bVisible && !gGCLState.bCutsceneActive && fabs(pCamera->camcenter[1] - v3EntPos[1]) > CHAT_BUBBLE_FLOOR_DISTANCE)
		return false;

	fY = floorf(v2EntPos[1]);

	if (fY < 0)
		return false;
	for (i = min(eaSize(peaBubbleStack), CHAT_BUBBLE_MAX_PER_ENT) - 1; i >= 0; i--)
	{
		ChatBubble *pBubble = (*peaBubbleStack)[i];
		ChatBubbleDef *pDef = pBubble ? GET_REF(pBubble->hDef) : NULL;
		F32 fCreatedAgo = (gGCLState.totalElapsedTimeMs - pBubble->uiStartTimeMs) / 1000.0;
		F32 fRemaining = (pBubble->uiEndTimeMs - gGCLState.totalElapsedTimeMs) / 1000.0;
		if (pDef && pBubble && pBubble->pchMessage && *pBubble->pchMessage)
		{
			fY = gclChatBubbleDefDraw(pDef, pBubble->pchMessage, -1, floorf(v2EntPos[0]), fY, fZ, 1.f, fCreatedAgo, fRemaining, bVisible, bFirst, fScreenDist);
			bFirst = false;
		}
		fZ += 0.0001;
	}
	return true;
}

void ChatBubbleStack_Process(Entity *pEnt, F32 fElapsed)
{
	ChatBubble ***peaBubbleStack = (pEnt && pEnt->pEntUI) ? &pEnt->pEntUI->eaBubbles : NULL;
	S32 j;

	if (!peaBubbleStack)
		return;

    for (j = eaSize(peaBubbleStack) - 1; j >= 0; j--)
	{
		ChatBubble *pBubble = (*peaBubbleStack)[j];
		if (pBubble->uiEndTimeMs <= gGCLState.totalElapsedTimeMs)
		{
			eaRemove(peaBubbleStack, j);
			StructDestroy(parse_ChatBubble, pBubble);
		}
	}
}

// Create a chat bubble over the player's head to test.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug) ACMD_HIDE;
void PlayerSay(ACMD_SENTENCE pchMessage)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt)
		ChatBubble_Say(entGetRef(pEnt), pchMessage, 3.f, NULL);
}

// Have an entity "fake" chat (chat bubble and log message).
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void EntSayMsgWithBubble(ChatUserInfo *pUserInfo, F32 fDuration, const char *pchBubble, ACMD_SENTENCE pchText)
{
	if (!gclContactDialogCameraActive())
	{
		ChatData *pData = StructCreate(parse_ChatData);
		ChatBubbleData *pBubbleData = StructCreate(parse_ChatBubbleData);

		pData->pBubbleData = pBubbleData;
		pBubbleData->fDuration = fDuration;
		pBubbleData->pchBubbleStyle = StructAllocString(pchBubble);

		if (pUserInfo) {
			ChatLog_AddEntityMessage(pUserInfo, kChatLogEntryType_NPC, pchText, pData);
		}

		StructDestroy(parse_ChatData, pData); // Includes ChatBubbleData
	}
}

// Have an entity "fake" chat (chat bubble and log message), specify a duration
// but use the default chat bubble.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void EntSayMsg(ChatUserInfo *pUserInfo, F32 fDuration, ACMD_SENTENCE pchMessage)
{
	if (!gclContactDialogCameraActive())
	{
		EntSayMsgWithBubble(pUserInfo, fDuration, "Default", pchMessage);
	}
}

// Have an entity "fake" chat (chat bubble and log message), automatically figure out a duration
// and use the default chat bubble.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void MissionCritterSpeak(ChatUserInfo *pUserInfo, ACMD_SENTENCE pchMessage)
{
	if (!gclContactDialogCameraActive())
	{
		const F32 cfMaxDisplayTime = 15.f;
		const F32 cfMinDisplayTime = 8.f;
		const S32 ciMinTextLength = 20;
		const S32 ciMaxTextLength = 100;
		S32 iLength = UTF8GetLength(pchMessage);
		F32 fDuration;

		iLength = CLAMP(iLength, ciMinTextLength, ciMaxTextLength) - ciMinTextLength;
		fDuration = cfMinDisplayTime + (cfMaxDisplayTime - cfMinDisplayTime) * iLength / (ciMaxTextLength - ciMinTextLength);
		if (iLength && cfMaxDisplayTime)
			EntSayMsg(pUserInfo, fDuration, pchMessage);
	}
}
