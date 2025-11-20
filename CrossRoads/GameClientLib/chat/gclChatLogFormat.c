/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gclChatLog.h"
#include "gclChat.h"
#include "gclChatConfig.h"
#include "gclClientChat.h"
#include "gclEntity.h"

#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "chatData.h"

#include "earray.h"
#include "EString.h"
#include "file.h"
#include "GameStringFormat.h"
#include "StringUtil.h"
#include "TextFilter.h"
#include "sysutil.h"
#include "MemoryPool.h"

#include "GameClientLib.h"

#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/chatData_h_ast.h"
#include "AutoGen/gclChatlog_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static bool shouldApplyProfanityFilter( ChatLogEntryType iChatMessageType )
{
	switch ( iChatMessageType )
	{
	case kChatLogEntryType_CombatSelf:
	case kChatLogEntryType_CombatTeam:
	case kChatLogEntryType_CombatOther:
	case kChatLogEntryType_Error:
	case kChatLogEntryType_Friend:
	case kChatLogEntryType_Inventory:
	case kChatLogEntryType_Mission:
	case kChatLogEntryType_Minigame:
	case kChatLogEntryType_Reward:
	case kChatLogEntryType_RewardMinor:
	case kChatLogEntryType_Spy:
	case kChatLogEntryType_System:
	case kChatLogEntryType_NPC:
		return false;
	}

	return true;
}

static void formatUser(char **estrFormattedUser, ChatUserInfo *pUser, ChatLogEntryType eType, const ChatLogEntry *pEntry, SA_PARAM_NN_VALID ChatLink **ppLink) {
	const char *pchUser = pUser->pchName && pUser->pchName[0] ? pUser->pchName : "";
	const char *pchHandle = pUser->pchHandle && pUser->pchHandle[0] ? pUser->pchHandle  : "";
	const char *pchAt = pchHandle[0] ? "@" : "";
	const char *pchGM = pUser->bIsDev ? "<Dev> " : pUser->bIsGM ? "<GM> " : "";

	StructDestroySafe(parse_ChatLink, ppLink);

	if (*pchUser && eType == kChatLogEntryType_System) {
		FormatMessageKey(estrFormattedUser, "ChatEntryLine_SystemUserFormat", 
			STRFMT_STRING("user", pchUser), 
			STRFMT_END);		
	} else 	if (*pchUser || *pchHandle) {
		static char *pchUserWithGM = NULL;

		estrClear(&pchUserWithGM);
		estrAppend2(&pchUserWithGM, pchGM);
		estrAppend2(&pchUserWithGM, pchUser);

		FormatMessageKey(estrFormattedUser, "ChatEntryLine_UserFormat", 
			STRFMT_STRING("user", pchUserWithGM), 
			STRFMT_STRING("at", pchAt), 
			STRFMT_STRING("handle", pchHandle), 
			STRFMT_END);

		if (*pchHandle) {
			static char achNameAndHandle[512];
			sprintf(achNameAndHandle, "%s@%s", pchUser, pchHandle);
			*ppLink = ChatData_CreatePlayerLink(achNameAndHandle, *estrFormattedUser, pUser->bIsGM, pUser->bIsDev, SAFE_MEMBER2(pEntry, pMsg, pchText) );
		}
	}
}

__forceinline static bool formatCapture(char **pestrDest, S32 *piPos, const char *pchText, S32 iLength, ChatLogSpanType eType, ChatLogFormatSpan ***peaSpans, ChatLink *pLink)
{
	if (pchText && iLength > 0)
	{
		while (iLength > 0)
		{
			S32 iPos = *piPos;
			S16 iSpanLen = MIN(iLength, 32767);

			ChatLogFormatSpan *pSpan = StructCreate(parse_ChatLogFormatSpan);
			if (pSpan)
			{
				pSpan->pLink = pLink;
				pSpan->iStart = iPos;
				pSpan->iLength = iSpanLen;
				pSpan->eType = eType;
				estrConcat(pestrDest, pchText, iSpanLen);
				eaPush(peaSpans, pSpan);
				*piPos += iSpanLen;
			}

			iLength -= iSpanLen;
		}
		return true;
	}
	return false;
}

static int sortLinkInfos(const ChatLinkInfo **ppA, const ChatLinkInfo **ppB)
{
	const ChatLinkInfo *pA = *ppA;
	const ChatLinkInfo *pB = *ppB;

	if (!pA || !pB)
		return !pA - !pB;

	if (pA->iStart != pB->iStart)
		return pA->iStart - pB->iStart;
	return pA->iLength - pB->iLength;
}

static void formatMessageContent(char **pestrDest, ChatLogFormatSpan ***peaSpans, S32 *piPos,
	const char *pchMessage, S32 iMessageLen, ChatData *pData)
{
	static ChatLinkInfo **eaInfo;
	S32 iStart, iLink, iLen;

	if (pData)
	{
		eaCopy(&eaInfo, &pData->eaLinkInfos);
		if (eaSize(&eaInfo) > 1)
			eaQSort(eaInfo, sortLinkInfos);
	}
	else
		eaClear(&eaInfo);

	iStart = iLink = 0;

	while (iLink < eaSize(&eaInfo) && iMessageLen > 0)
	{
		// Append plain text
		iLen = eaInfo[iLink]->iStart - iStart;
		MIN1(iLen, iMessageLen);
		formatCapture(pestrDest, piPos, pchMessage + iStart, iLen, kChatLogSpanType_Default, peaSpans, NULL);
		iMessageLen -= iLen;
		iStart += iLen;

		// Append link text
		if (iMessageLen > 0)
		{
			iLen = eaInfo[iLink]->iLength;
			MIN1(iLen, iMessageLen);

			if (iLink < eaSize(&eaInfo) - 1)
			{
				MIN1(iLen, eaInfo[iLink + 1]->iStart - eaInfo[iLink]->iStart);
			}

			formatCapture(pestrDest, piPos, pchMessage + iStart, iLen, kChatLogSpanType_Default, peaSpans, eaInfo[iLink]->pLink);
			iMessageLen -= iLen;
			iStart += iLen;
		}

		iLink++;
	}

	// Append trailing plain text
	if (iMessageLen > 0)
		formatCapture(pestrDest, piPos, pchMessage + iStart, iMessageLen, kChatLogSpanType_Default, peaSpans, NULL);
}

// Dumb strfmt like formatter. Keeps track of locations of replacements.
static void formatFromAndTo(char **pestrDest, ChatLogFormatSpan ***peaSpans, S32 *piPos,
								const char *pchMessageKey,
								const char *pchFromText, ChatLink *pFromLink,
								const char *pchToText, ChatLink *pToLink)
{
	const char *pchMessage = TranslateMessageKey(pchMessageKey);
	const char *pchStart, *pch;
	S32 iFromLen, iToLen;

	if (!pchMessage)
	{
		return;
	}

	iFromLen = pchFromText ? (S32)strlen(pchFromText) : 0;
	iToLen = pchToText ? (S32)strlen(pchToText) : 0;

	pchStart = pch = pchMessage;
	while (*pch)
	{
		if (*pch == '{')
		{
			if (!strnicmp(pch, "{from}", 6))
			{
				formatCapture(pestrDest, piPos, pchStart, pch-pchStart, kChatLogSpanType_Default, peaSpans, NULL);
				formatCapture(pestrDest, piPos, pchFromText, iFromLen, kChatLogSpanType_From, peaSpans, pFromLink);
				pch += 6;
				pchStart = pch;
			}
			else if (!strnicmp(pch, "{to}", 4))
			{
				formatCapture(pestrDest, piPos, pchStart, pch-pchStart, kChatLogSpanType_Default, peaSpans, NULL);
				formatCapture(pestrDest, piPos, pchToText, iToLen, kChatLogSpanType_To, peaSpans, pToLink);
				pch += 4;
				pchStart = pch;
			}
			else
			{
				pch++;
			}
		}
		else
		{
			pch++;
		}
	}

	if (pchStart < pch)
		formatCapture(pestrDest, piPos, pchStart, pch-pchStart, kChatLogSpanType_Default, peaSpans, NULL);
}

// Dumb strfmt like formatter. Keeps track of locations of replacements.
static void formatMessage(char **pestrDest, const char *pchMessageKey, ChatLogFormatSpan ***peaSpans,
							const char *pchTime,
							const char *pchSpy,
							const char *pchChannel,
							const char *pchFromAndToMessageKey,
							const char *pchFromText, ChatLink *pFromLink,
							const char *pchToText, ChatLink *pToLink,
							const char *pchMsg, ChatData *pData)
{
	const char *pchMessage = TranslateMessageKey(pchMessageKey);
	const char *pchStart, *pch;
	S32 iTimeLen, iSpyLen, iChannelLen, iMsgLen, iPos = 0;

	if (!pchMessage)
	{
		return;
	}

	iTimeLen = pchTime ? (S32)strlen(pchTime) : 0;
	iSpyLen = pchSpy ? (S32)strlen(pchSpy) : 0;
	iChannelLen = pchChannel ? (S32)strlen(pchChannel) : 0;
	iMsgLen = pchMsg ? (S32)strlen(pchMsg) : 0;

	pchStart = pch = pchMessage;
	while (*pch)
	{
		if (*pch == '{')
		{
			if (!strnicmp(pch, "{time}", 6))
			{
				formatCapture(pestrDest, &iPos, pchStart, pch-pchStart, kChatLogSpanType_Default, peaSpans, NULL);
				formatCapture(pestrDest, &iPos, pchTime, iTimeLen, kChatLogSpanType_Time, peaSpans, NULL);
				pch += 6;
				pchStart = pch;
			}
			else if (!strnicmp(pch, "{spy}", 5))
			{
				formatCapture(pestrDest, &iPos, pchStart, pch-pchStart, kChatLogSpanType_Default, peaSpans, NULL);
				formatCapture(pestrDest, &iPos, pchSpy, iSpyLen, kChatLogSpanType_Spy, peaSpans, NULL);
				pch += 5;
				pchStart = pch;
			}
			else if (!strnicmp(pch, "{channel}", 9))
			{
				formatCapture(pestrDest, &iPos, pchStart, pch-pchStart, kChatLogSpanType_Default, peaSpans, NULL);
				formatCapture(pestrDest, &iPos, pchChannel, iChannelLen, kChatLogSpanType_Channel, peaSpans, NULL);
				pch += 9;
				pchStart = pch;
			}
			else if (!strnicmp(pch, "{fromAndTo}", 11))
			{
				formatCapture(pestrDest, &iPos, pchStart, pch-pchStart, kChatLogSpanType_Default, peaSpans, NULL);
				formatFromAndTo(pestrDest, peaSpans, &iPos, pchFromAndToMessageKey, pchFromText, pFromLink, pchToText, pToLink);
				pch += 11;
				pchStart = pch;
			}
			else if (!strnicmp(pch, "{msg}", 5))
			{
				formatCapture(pestrDest, &iPos, pchStart, pch-pchStart, kChatLogSpanType_Default, peaSpans, NULL);
				formatMessageContent(pestrDest, peaSpans, &iPos, pchMsg, iMsgLen, pData);
				pch += 5;
				pchStart = pch;
			}
			else
			{
				pch++;
			}
		}
		else
		{
			pch++;
		}
	}

	if (pchStart < pch)
		formatCapture(pestrDest, &iPos, pchStart, pch-pchStart, kChatLogSpanType_Default, peaSpans, NULL);
}

void gclChatLogFormat(ChatConfig *pConfig, ChatLogEntry *pEntry)
{
	static char *pchTime = NULL;
	static char *pchChannel = NULL;
	static char *pchFromUser = NULL;
	static char *pchToUser = NULL;
	static char *pchFilteredMessage = NULL;
	const char *pchFormat = NULL;
	const char *pchFromAndToMessageKey = NULL;
	const char *pchFromText = NULL;
	const char *pchToText = NULL;
	const ChatMessage *pMsg = pEntry->pMsg;
	ChatLogEntryType eType = pMsg->eType;
	bool bTell = eType == kChatLogEntryType_Private;
	bool bSentTell = eType == kChatLogEntryType_Private_Sent;
	const char *pchSpy = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Clear all of the chat log entry parts
	estrClear(&pchTime);
	estrClear(&pchChannel);
	estrClear(&pchFromUser);
	estrClear(&pchToUser);
	estrClear(&pchFilteredMessage);

	// Create the time part.
	// TODO: Make this use a single message, and have separate spans for date/time.
	if (pConfig && (pConfig->bShowDate || pConfig->bShowTime))
	{
		const char *pchOldFormat = pConfig->bShowDate && pConfig->bShowTime
			? "ChatEntryLine_DateTimeFormat"
			: pConfig->bShowDate ? "ChatEntryLine_DateOnlyFormat"
			: "ChatEntryLine_TimeOnlyFormat";
		char *pchNewFormat = pConfig->bShowDate && pConfig->bShowTime
			? "ChatEntryLine_SimpleDateTimeFormat"
			: pConfig->bShowDate ? "ChatEntryLine_SimpleDateOnlyFormat"
			: "ChatEntryLine_SimpleTimeOnlyFormat";

		if (TranslateMessageKey(pchNewFormat))
		{
			FormatGameMessageKey(&pchTime, pchNewFormat,
				STRFMT_DATETIME("Time", pEntry->iTimestamp),
				STRFMT_DATETIME("Value", pEntry->iTimestamp),
				STRFMT_END);
		}
		else if (TranslateMessageKey(pchOldFormat))
		{
			const time_t time = timeMakeLocalTimeFromSecondsSince2000(pEntry->iTimestamp); // Convert to time_t
			struct tm TimeInfo;
			if (!localtime_s(&TimeInfo, &time))
			{
				char pchMin[3];
				// Preformat the minutes so we guarantee having 2 digits.
				sprintf(pchMin,  "%02u", TimeInfo.tm_min);
				FormatMessageKey(&pchTime, pchOldFormat,
					STRFMT_INT("month", TimeInfo.tm_mon+1),
					STRFMT_INT("day", TimeInfo.tm_mday),
					STRFMT_INT("hour", TimeInfo.tm_hour),
					STRFMT_STRING("minute", pchMin),
					STRFMT_END);
			}
		}
	}

	// Create the spy part
	if (eType == kChatLogEntryType_Spy)
	{
		pchSpy = TranslateMessageKey("ChatEntryLine_SpyFormat");
	}

	// Create the channel part
	if (eType == kChatLogEntryType_Channel || eType == kChatLogEntryType_Spy)
	{
		char *channelDisplay = pMsg->pchChannel;
		if (pMsg->pchChannelDisplay && *pMsg->pchChannelDisplay)
		{
			channelDisplay = pMsg->pchChannelDisplay;
		}
		if (channelDisplay && *channelDisplay)
		{
			FormatGameMessageKey(&pchChannel, "ChatEntryLine_ChannelFormat",
				STRFMT_STRING("channel", channelDisplay),
				STRFMT_END);
		}
	} else {
		static char *pchDisplayName = NULL;

		// Override the display for sent tells
		if (bSentTell)
		{
			ChatMessage Temp = *pMsg;
			Temp.eType = kChatLogEntryType_Private;
			ClientChat_GetMessageTypeDisplayNameByMessage(&pchDisplayName, &Temp);
		}
		else
		{
			ClientChat_GetMessageTypeDisplayNameByMessage(&pchDisplayName, pMsg);
		}

		FormatGameMessageKey(&pchChannel, "ChatEntryLine_ChannelFormat",
			STRFMT_STRING("channel", pchDisplayName),
			STRFMT_END);
	}

	// Create the from user
	// If this is a PM then we use pTo because that's the name that's going to display
	if (pMsg->pFrom && eType == kChatLogEntryType_Private_Sent) {
		formatUser(&pchFromUser, pMsg->pTo, eType, pEntry, &pEntry->pFromLink);
	}
	else if (pMsg->pFrom) {
		formatUser(&pchFromUser, pMsg->pFrom, eType, pEntry, &pEntry->pFromLink);
	}

	// Create the "to" user
	if (pMsg->pTo && eType != kChatLogEntryType_Private) {
		formatUser(&pchToUser, pMsg->pTo, eType, pEntry, &pEntry->pToLink);
	}

	// Create the from-to part
	if (bTell && pchFromUser && *pchFromUser) {
		// From ONLY (for Tells)
		pchFromAndToMessageKey = "ChatEntryLine_TellFromFormat";
		pchFromText = pchFromUser;
	} else if (bSentTell && pchToUser && *pchToUser) {
		// Only show To user for sent tells
		pchFromAndToMessageKey = "ChatEntryLine_ToOnlyFormat";
		pchToText = pchToUser;
	} else if (pchFromUser && *pchFromUser) {
		if (pchToUser && *pchToUser) {
			// From AND To
			pchFromAndToMessageKey = "ChatEntryLine_FromToFormat";
			pchFromText = pchFromUser;
			pchToText = pchToUser;
		} else if (eType != kChatLogEntryType_System || (pConfig && pConfig->bShowMessageTypeNames)) {
			// From ONLY (System message from is only shown if message types are visible)
			pchFromAndToMessageKey = "ChatEntryLine_FromOnlyFormat";
			pchFromText = pchFromUser;
		}
	} else if (pchToUser && *pchToUser) {
		// To ONLY
		pchFromAndToMessageKey = "ChatEntryLine_ToOnlyFormat";
		pchToText = pchToUser;
	}

	estrAppend2(&pchFilteredMessage, pMsg->pchText);

	// Convert <br>'s into spaces if any exist.
	estrReplaceOccurrences(&pchFilteredMessage, "<br>", " ");

	// Censor profanity from "msg" text if the player wants profanity filtering
	if ( (!pConfig || pConfig->bProfanityFilter) && shouldApplyProfanityFilter(eType))
		ReplaceAnyWordProfane(pchFilteredMessage);

	// Choose a format
	if (pMsg->pData && pMsg->pData->bEmote) {
		pchFormat = "ChatEntryLine_EmoteEntryFormat";
	} else if (pchFromAndToMessageKey && eType != kChatLogEntryType_System) {
		pchFormat = "ChatEntryLine_DefaultEntryFormat";
	} else {
		pchFormat = "ChatEntryLine_DefaultEntryNoUserFormat";
	}

	// Combine everything together
	estrClear(&pEntry->pchFormattedText);
	eaClearStruct(&pEntry->eaFormatSpans, parse_ChatLogFormatSpan);
	formatMessage(&pEntry->pchFormattedText, pchFormat, &pEntry->eaFormatSpans,
					pchTime,
					pchSpy,
					pchChannel,
					pchFromAndToMessageKey,
					pchFromText, pEntry->pFromLink,
					pchToText, pEntry->pToLink,
					pchFilteredMessage, pMsg->pData);

	if (pEntry->pchFormattedText && pEntry->eaFormatSpans)
	{
		S32 iLength = (S32)strlen(pEntry->pchFormattedText);
		S32 i;
		for (i = 0; i < eaSize(&pEntry->eaFormatSpans); i++)
		{
			S32 iStart = pEntry->eaFormatSpans[i]->iStart;
			S32 iEnd = pEntry->eaFormatSpans[i]->iStart + pEntry->eaFormatSpans[i]->iLength;
			if (!devassertmsgf(0 <= iStart && iStart <= iEnd && iEnd <= iLength, "Invalid span (%d, %d, %d); [%d, %d]", iLength, pEntry->eaFormatSpans[i]->iStart, pEntry->eaFormatSpans[i]->iLength, iStart, iEnd))
				pEntry->eaFormatSpans[i]->iLength = 0;
		}
	}
	else
	{
		devassertmsgf(0, "Failed to format message key: %s. This means either the message key is missing, or is untranslated for the language you are using.", pchFormat);
	}

	PERFINFO_AUTO_STOP_FUNC();
}
