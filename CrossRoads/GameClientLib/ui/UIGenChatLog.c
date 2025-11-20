#include "earray.h"
#include "EString.h"
#include "MemoryPool.h"
#include "StringFormat.h"
#include "StringUtil.h"
#include "sysutil.h"
#include "TextFilter.h"

#include "inputLib.h"
#include "inputMouse.h"
#include "inputText.h"
#include "GfxPrimitive.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"

#include "chatCommonStructs.h"
#include "ChatData.h"
#include "gclChat.h"
#include "chat/gclChatLog.h"
#include "gclChatConfig.h"
#include "gclEntity.h"
#include "gclNotify.h"
#include "NotifyCommon.h"

#include "GameClientLib.h"

#include "UIGen.h"
#include "UIGenChatLog.h"
#include "TextBuffer.h"
#include "UIGenTextEntry.h"
#include "gclChatChannelUI.h"
#include "gclCommandParse.h"

#include "UICore_h_ast.h"
#include "UIGen_h_ast.h"
#include "Autogen/chatCommon_h_ast.h"
#include "Autogen/ChatData_h_ast.h"
#include "Autogen/gclChatLog_h_ast.h"
#include "Autogen/UIGenChatLog_h_ast.h"
#include "Autogen/UIGenScrollbar_h_ast.h"
#include "Autogen/NotifyEnum_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

MP_DEFINE(UIGenChatLogEntryLayout);
MP_DEFINE(UIGenChatLogSpanLayout);

#define getFontScale(pGen, pConfig) ((pGen)->fScale * ((pConfig) ? (pConfig)->fFontScale : 1))

#define SetScrollPosition(pGen, pScrollbarState, fPosition) ((pScrollbarState)->fScrollPosition = CLAMP((fPosition), 0.f, (pScrollbarState)->fTotalHeight))
#define AdjustScrollPosition(pGen, pScrollbarState, fAdjustment) ((pScrollbarState)->fScrollPosition = CLAMP((pScrollbarState)->fScrollPosition + (fAdjustment), 0.f, (pScrollbarState)->fTotalHeight))
#define ScrollToBeginning(pGen, pScrollbarState) ((pScrollbarState)->fScrollPosition = 0.f)
#define ScrollToEnd(pGen, pScrollbarState) ((pScrollbarState)->fScrollPosition = MAX(0, (pScrollbarState)->fTotalHeight - CBoxHeight(&(pGen)->ScreenBox)))

static UIGenChatLogEntryPoint s_InvalidSelectionPoint = {-1};

static bool s_bGenChatLogDebugMouseOver;
AUTO_CMD_INT(s_bGenChatLogDebugMouseOver, GenChatLogDebugMouseOver) ACMD_CATEGORY(Debug);

static bool s_bGenChatLogDebugSpans;
AUTO_CMD_INT(s_bGenChatLogDebugSpans, GenChatLogDebugSpans) ACMD_CATEGORY(Debug);

static bool s_bGenChatLogDebugSpriteCount;
AUTO_CMD_INT(s_bGenChatLogDebugSpriteCount, GenChatLogDebugInfo) ACMD_CATEGORY(Debug);

extern void ui_GenExprChatLog_CopySelectionToClipboard(SA_PARAM_NN_VALID UIGen *pGen);
extern void ui_GenExprChatLog_ClearSelection(SA_PARAM_NN_VALID UIGen *pGen);
extern U32 ui_GenChatLog_Highlight(U32 iColor, F32 fPercent);

__forceinline static bool layoutIncludesSpan(UIGenChatLog *pChatLog, ChatConfig *pConfig, ChatLogEntry *pEntry, ChatLogFormatSpan *pSpan)
{
	switch (pSpan->eType)
	{
	xcase kChatLogSpanType_Time:
		if (pConfig && !pConfig->bShowDate && !pConfig->bShowTime)
			return false;
	xcase kChatLogSpanType_Channel:
		if (pChatLog->bHideChannelName)
			return false;
		if (pConfig && !pConfig->bShowChannelNames)
			return false;
	}
	return true;
}

__forceinline static void normalizeSelectionOrder(UIGenChatLogState *pState, UIGenChatLogEntryPoint *pSelectionStart, UIGenChatLogEntryPoint *pSelectionEnd)
{
	if (pState->SelectionStart.iEntry < pState->SelectionEnd.iEntry)
	{
		*pSelectionStart = pState->SelectionStart;
		*pSelectionEnd = pState->SelectionEnd;
	}
	else if (pState->SelectionStart.iEntry > pState->SelectionEnd.iEntry)
	{
		*pSelectionStart = pState->SelectionEnd;
		*pSelectionEnd = pState->SelectionStart;
	}
	else if (pState->SelectionStart.iSpan < pState->SelectionEnd.iSpan)
	{
		*pSelectionStart = pState->SelectionStart;
		*pSelectionEnd = pState->SelectionEnd;
	}
	else if (pState->SelectionStart.iSpan > pState->SelectionEnd.iSpan)
	{
		*pSelectionStart = pState->SelectionEnd;
		*pSelectionEnd = pState->SelectionStart;
	}
	else if (pState->SelectionStart.iPos < pState->SelectionEnd.iPos)
	{
		*pSelectionStart = pState->SelectionStart;
		*pSelectionEnd = pState->SelectionEnd;
	}
	else
	{
		*pSelectionStart = pState->SelectionEnd;
		*pSelectionEnd = pState->SelectionStart;
	}
}

__forceinline static S32 findChatLogEntryByID(S32 id)
{
	S32 l = 0, h = eaSize(&g_ChatLog) - 1;
	while (l <= h)
	{
		S32 m = (h - l) / 2 + l;
		S32 eid = g_ChatLog[m]->id;
		if (eid < id)
			l = m + 1;
		else if (eid > id)
			h = m - 1;
		else
			return m;
	}
	return -l - 1;
}

static bool shouldLogEntryBeVisible(UIGenChatLog *pChatLog, UIGenChatLogState *pState, ChatTabConfig *pTab, ChatLogEntry *pEntry)
{
	if (!pEntry->pMsg || !eaSize(&pEntry->eaFormatSpans))
	{
		return false;
	}

	if (pState->pchPMHandle)
	{
		return ChatCommon_IsLogEntryVisibleInPrivateMessageWindow(pEntry->pMsg, pState->pchPMHandle);
	}

	if (pState->pchChannel)
	{
		if (pEntry->pMsg->eType == pState->eChannelType)
			return stricmp(pChatLog->pchFilterChannel, pEntry->pMsg->pchChannel) == 0;
		return false;
	}

	if (pTab)
	{
		if (!ChatCommon_IsLogEntryVisibleInTab(pTab, pEntry->pMsg->eType, pEntry->pMsg->pchChannel))
		{
			return false;
		}
	}

	if (eaiSize(&pChatLog->peNotifications) > 0)
	{
		static const char *s_apchNotifyTypeNames[kNotifyType_COUNT];
		S32 i;

		if (pEntry->pMsg->eType != kChatLogEntryType_System || !pEntry->pMsg->pFrom || !pEntry->pMsg->pFrom->pchName)
			return false;

		// Most often, peNotifications will be a much smaller list than NotifyTypeEnum
		// so it'd be faster to just go through this list, than the StaticDefineInt list.
		for (i = eaiSize(&pChatLog->peNotifications) - 1; i >= 0; i--)
		{
			const char *pchName;

			if (pChatLog->peNotifications[i] < kNotifyType_COUNT)
			{
				if (!s_apchNotifyTypeNames[pChatLog->peNotifications[i]])
					s_apchNotifyTypeNames[pChatLog->peNotifications[i]] = StaticDefineIntRevLookupNonNull(NotifyTypeEnum, pChatLog->peNotifications[i]);
				pchName = s_apchNotifyTypeNames[pChatLog->peNotifications[i]];
			}
			else
			{
				pchName = StaticDefineIntRevLookupNonNull(NotifyTypeEnum, pChatLog->peNotifications[i]);
			}

			if (pchName && !stricmp(pEntry->pMsg->pFrom->pchName, pchName))
			{
				return true;
			}
		}
		return false;
	}

	if (pEntry->pMsg->eType == kChatLogEntryType_System && pEntry->pMsg->pFrom && pEntry->pMsg->pFrom->pchName)
	{
		NotifyType eType = StaticDefineIntGetInt(NotifyTypeEnum, pEntry->pMsg->pFrom->pchName);

		if (gclNotify_CheckSettingFlags(eType, kNotifySettingFlags_DisableChat))
		{
			return false;
		}
	}

	// Default behavior is to be completely unfiltered
	return true;
}

static void scrollUpOneLine(UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeChatLog))
	{
		ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
		UIGenChatLog *pChatLog = UI_GEN_RESULT(pGen, ChatLog);
		UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);

		if (pChatLog)
		{
			AdjustScrollPosition(pGen, &pState->scrollbar, -ui_StyleFontLineHeight(GET_REF(pChatLog->hFont), getFontScale(pGen, pConfig)));
		}
	}
}

static void scrollDownOneLine(UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeChatLog))
	{
		ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
		UIGenChatLog *pChatLog = UI_GEN_RESULT(pGen, ChatLog);
		UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);

		if (pChatLog)
		{
			AdjustScrollPosition(pGen, &pState->scrollbar, ui_StyleFontLineHeight(GET_REF(pChatLog->hFont), getFontScale(pGen, pConfig)));
		}
	}
}

static void scrollUpOnePage(UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeChatLog))
	{
		UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
		AdjustScrollPosition(pGen, &pState->scrollbar, -CBoxHeight(&pGen->ScreenBox));
	}
}

static void scrollDownOnePage(UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeChatLog))
	{
		UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
		AdjustScrollPosition(pGen, &pState->scrollbar, CBoxHeight(&pGen->ScreenBox));
	}
}

static void scrollToBeginning(UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeChatLog))
	{
		UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
		ScrollToBeginning(pGen, &pState->scrollbar);
	}
}

static void scrollToEnd(UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeChatLog))
	{
		UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
		ScrollToEnd(pGen, &pState->scrollbar);
	}
}

void ui_GenUpdateChatLog(UIGen *pGen)
{
	UIGenChatLog *pChatLog = UI_GEN_RESULT(pGen, ChatLog);
	UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	ChatTabConfig *pTabConfig = NULL;
	S32 i, j, iEntry;
	bool bFilterChange = false;

	PERFINFO_AUTO_START_FUNC();

	// Chat log version changes also trigger filter updates
	if (g_ChatLogVersion != pState->iChatLogVersion)
	{
		bFilterChange = true;
	}

	// Determine if the filter has been changed
	{
		const char *pchChannel = NULL, *pchTab = NULL, *pchPMHandle = NULL;
		ChatLogEntryType eType = kChatLogEntryType_Channel;
		U32 uTabCRC = 0;

		// Determine tab/channel/handle to display
		if (pChatLog->pDisplayChannelExpr)
		{
			MultiVal mv = {0};
			ui_GenEvaluate(pGen, pChatLog->pDisplayChannelExpr, &mv);
			pchChannel = MultiValGetString(&mv, NULL);
		}
		else if (pChatLog->pDisplayTabExpr)
		{
			MultiVal mv = {0};
			ui_GenEvaluate(pGen, pChatLog->pDisplayTabExpr, &mv);
			pchTab = MultiValGetString(&mv, NULL);
		}
		else if (pChatLog->pPrivateMessageHandleExpr)
		{
			MultiVal mv = {0};
			ui_GenEvaluate(pGen, pChatLog->pPrivateMessageHandleExpr, &mv);
			pchPMHandle = MultiValGetString(&mv, NULL);
		}
		else if (pChatLog->pchFilterChannel)
		{
			pchChannel = pChatLog->pchFilterChannel;
		}
		else if (pChatLog->pchFilterTab)
		{
			pchTab = pChatLog->pchFilterTab;
		}
		else if (pChatLog->pchFilterTabGroup)
		{
			ChatTabGroupConfig *pChatTabGroup = ChatCommon_GetTabGroupConfig(pConfig, pChatLog->pchFilterTabGroup);
			if (pChatTabGroup)
			{
				ChatTabConfig *pTab = eaGet(&pChatTabGroup->eaChatTabConfigs, pState->iFilterTab);
				if (pTab)
					pchTab = pTab->pchTitle;
			}
		}

		// Find ChatTabConfig if displaying a tab
		if (pchTab && pConfig)
		{
			for (i = eaSize(&pConfig->eaChatTabGroupConfigs) - 1; i >= 0 && !pTabConfig; --i)
			{
				ChatTabGroupConfig *pTabGroup = pConfig->eaChatTabGroupConfigs[i];
				for (j = eaSize(&pTabGroup->eaChatTabConfigs) - 1; j >= 0 && !pTabConfig; --j)
				{
					ChatTabConfig *pTab = pTabGroup->eaChatTabConfigs[j];
					if (!stricmp(pTab->pchTitle, pchTab))
						pTabConfig = pTab;
				}
			}

			if (pTabConfig)
			{
				// Calculate the CRC of the tab settings, to determine if the tab settings changed.
				uTabCRC = StructCRC(parse_ChatTabConfig, pTabConfig);
			}
			else
			{
				// The tab doesn't actually exist, display nothing.
				pchTab = NULL;
			}
		}

		if (pchChannel && ChatCommon_IsBuiltInChannel(pchChannel))
			eType = ChatCommon_GetChannelMessageType(pchChannel);

		// Update state
		if ((!pchChannel) != (!pState->pchChannel) || (pchChannel && stricmp(pchChannel, pState->pchChannel)))
		{
			if (pState->pchChannel)
				StructFreeString(pState->pchChannel);
			pState->pchChannel = pchChannel ? StructAllocString(pchChannel) : NULL;
			pState->eChannelType = eType;
			bFilterChange = true;
		}

		if ((!pchTab) != (!pState->pchTab) || uTabCRC != pState->uTabCRC || (pchTab && stricmp(pchTab, pState->pchTab)))
		{
			if (pState->pchTab)
				StructFreeString(pState->pchTab);
			pState->pchTab = pchTab ? StructAllocString(pchTab) : NULL;
			bFilterChange = true;
			pState->uTabCRC = pchTab ? uTabCRC : 0;
		}

		if ((!pchPMHandle) != (!pState->pchPMHandle) || (pchPMHandle && stricmp(pchPMHandle, pState->pchPMHandle)))
		{
			if (pState->pchPMHandle)
				StructFreeString(pState->pchPMHandle);
			pState->pchPMHandle = pchPMHandle ? StructAllocString(pchPMHandle) : NULL;
			bFilterChange = true;
		}

		if (pChatLog->bShowNewMessages)
		{
			if (pState->iFirstEntryID < 0 && eaSize(&g_ChatLog))
			{
				U32 uNow = timeSecondsSince2000();

				// Include messages that occurred in the last second
				for (i = eaSize(&g_ChatLog) - 1; i >= 0; --i)
				{
					if (g_ChatLog[i]->iTimestamp < uNow)
						break;
				}

				// Figure out from which ID to include
				if (i < 0)
					pState->iFirstEntryID = g_ChatLog[0]->id;
				else if (i < eaSize(&g_ChatLog) - 1)
					pState->iFirstEntryID = g_ChatLog[i + 1]->id;
				else
					pState->iFirstEntryID = eaTail(&g_ChatLog)->id + 1;
				pState->iScanEntryID = pState->iFirstEntryID;

				bFilterChange = true;
			}
		}
		else if (pState->iFirstEntryID >= 0)
		{
			pState->iFirstEntryID = -1;
			bFilterChange = true;
		}
	}

	// ----------------------
	// This next section of code is to maintain backwards compatibility with the old chat log behavior.
	//
	// It is possible to make it optional, however, since the master chat log is responsible for limiting,
	// that would need to be changed to remember *everything*.

	if (eaSize(&g_ChatLog) == 0)
	{
		// Fast clear
		eaClearStruct(&pState->eaEntryLayouts, parse_UIGenChatLogEntryLayout);
		pState->SelectionStart = s_InvalidSelectionPoint;
		pState->SelectionEnd = s_InvalidSelectionPoint;
	}
	else
	{
		S32 iTrimmed = 0;
		// Trim off dead entries
		while (eaSize(&pState->eaEntryLayouts) > 0 && pState->eaEntryLayouts[0]->id < g_ChatLog[0]->id)
		{
			StructDestroy(parse_UIGenChatLogEntryLayout, eaRemove(&pState->eaEntryLayouts, 0));
			iTrimmed++;
		}
		if (pState->SelectionStart.iEntry >= 0)
			pState->SelectionStart.iEntry = MAX(0, pState->SelectionStart.iEntry - iTrimmed);
		if (pState->SelectionEnd.iEntry >= iTrimmed)
			pState->SelectionEnd.iEntry = pState->SelectionEnd.iEntry - iTrimmed;
		else
			pState->SelectionStart = pState->SelectionEnd = s_InvalidSelectionPoint;
	}

	// ----------------------

	// Rescan old entries.
	if (bFilterChange)
	{
		iEntry = 0;

		// Reset selection if there is a filter change
		pState->SelectionStart = s_InvalidSelectionPoint;
		pState->SelectionEnd = s_InvalidSelectionPoint;

		PERFINFO_AUTO_START("Updating Filtered List", 1);
		for (i = 0; i < eaSize(&g_ChatLog); ++i)
		{
			ChatLogEntry *pEntry = g_ChatLog[i];
			if (pEntry->id < pState->iFirstEntryID)
				continue;
			if (pEntry->id >= pState->iScanEntryID)
				break;
			if (shouldLogEntryBeVisible(pChatLog, pState, pTabConfig, pEntry))
			{
				// Remove hidden entries
				while (eaSize(&pState->eaEntryLayouts) > iEntry && pState->eaEntryLayouts[iEntry]->id < pEntry->id)
					StructDestroy(parse_UIGenChatLogEntryLayout, eaRemove(&pState->eaEntryLayouts, iEntry));
				// Entry currently doesn't exist
				if (iEntry >= eaSize(&pState->eaEntryLayouts) || pState->eaEntryLayouts[iEntry]->id > pEntry->id)
				{
					UIGenChatLogEntryLayout *pLayout = StructCreate(parse_UIGenChatLogEntryLayout);
					if (pLayout)
					{
						pLayout->id = pEntry->id;
						pLayout->uFadeTime = gGCLState.totalElapsedTimeMs;
						if (iEntry >= eaSize(&pState->eaEntryLayouts))
							eaPush(&pState->eaEntryLayouts, pLayout);
						else
							eaInsert(&pState->eaEntryLayouts, pLayout, iEntry);
					}
				}
				iEntry++;
			}
		}
		while (eaSize(&pState->eaEntryLayouts) > iEntry)
			StructDestroy(parse_UIGenChatLogEntryLayout, eaRemove(&pState->eaEntryLayouts, iEntry));
		PERFINFO_AUTO_STOP();
	}

	if (pState->iScanEntryID < 0)
		pState->iScanEntryID = MAX(0, pState->iFirstEntryID);

	// Scan new entries
	if (eaTail(&g_ChatLog) && eaTail(&g_ChatLog)->id >= pState->iScanEntryID)
	{
		PERFINFO_AUTO_START("Scanning New Entries", 1);
		i = findChatLogEntryByID(pState->iScanEntryID);
		if (i < 0)
			i = -(i + 1);
		while (i < eaSize(&g_ChatLog))
		{
			ChatLogEntry *pEntry = g_ChatLog[i];
			if (pEntry->id < pState->iFirstEntryID)
				continue;
			if (shouldLogEntryBeVisible(pChatLog, pState, pTabConfig, pEntry))
			{
				UIGenChatLogEntryLayout *pLayout = StructCreate(parse_UIGenChatLogEntryLayout);
				if (pLayout)
				{
					pLayout->id = pEntry->id;
					pLayout->uFadeTime = gGCLState.totalElapsedTimeMs;
					eaPush(&pState->eaEntryLayouts, pLayout);
				}
			}
			i++;
		}
		if (eaSize(&g_ChatLog))
			pState->iScanEntryID = eaTail(&g_ChatLog)->id + 1;
		PERFINFO_AUTO_STOP();
	}

	// Validate the ActiveLink
	if (pState->iActiveLinkID >= 0 && findChatLogEntryByID(pState->iActiveLinkID) < 0)
	{
		pState->iActiveLinkID = -1;
		pState->pActiveLink = NULL;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void flowEntryLayout(UIGenChatLog *pChatLog, ChatConfig *pConfig, ChatLogEntry *pEntry, UIGenChatLogEntryLayout *pLayout, F32 fWidth, F32 fScale)
{
	if (pEntry)
	{
		static GfxFont *s_pQuickWidthFont;
		static F32 s_fQuickWidths[128];
		static U8 s_WordBreak[128];
		UIStyleFont *pFont = GET_REF(pChatLog->hFont);
		S32 i, n = eaSize(&pEntry->eaFormatSpans);
		F32 fRunningWidth = pChatLog->fIndent;

		// Build a lookup table of valid word break characters.
		if (!s_WordBreak[' '])
		{
			const unsigned char *pchWordBreakChars = " ";
			const unsigned char *pchNonWordBreakChars = ",.;!?\"'()[]{}";
			for (i = 0; i < 128; i++)
				s_WordBreak[i] = ispunct(i) || isspace(i);
			for (; *pchWordBreakChars; pchWordBreakChars++)
				s_WordBreak[*pchWordBreakChars] = true;
			for (; *pchNonWordBreakChars; pchNonWordBreakChars++)
				s_WordBreak[*pchNonWordBreakChars] = false;
		}

		// Reset the really fast lookup table for characters between 32 and 127.
		if (s_pQuickWidthFont != GET_REF(pFont->hFace))
		{
			memset(s_fQuickWidths, 0, sizeof(s_fQuickWidths));
			s_pQuickWidthFont = GET_REF(pFont->hFace);
		}

		eaiClearFast(&pLayout->piLineBreaks);
		pLayout->siMaxLineWidth = 0;

		for (i = 0; i < n; i++)
		{
			ChatLogFormatSpan *pSpan = pEntry->eaFormatSpans[i];
			const unsigned char *pch, *pchEnd;
			S32 ch, chBest, chSecondBest;
			F32 fBestWidth, fSecondBestWidth;

			// Ignore spans we don't care about
			if (!layoutIncludesSpan(pChatLog, pConfig, pEntry, pSpan))
				continue;

			ch = pSpan->iStart;
			pch = (unsigned char *)pEntry->pchFormattedText + ch;
			pchEnd = pch + pSpan->iLength;
			chBest = -1;
			fBestWidth = fRunningWidth;
			chSecondBest = -1;
			fSecondBestWidth = fRunningWidth;

			// Special handling for @handles
			if (pSpan->pLink && pSpan->pLink->eType == kChatLinkType_PlayerHandle)
			{
				char *pchAt = strchr(pEntry->pchFormattedText + ch, '@');
				if (pConfig && pConfig->bHideAccountNames && !(pchAt == pEntry->pchFormattedText + ch))
				{
					pchEnd = pchAt;
				}
			}

			while (*pch && pch < pchEnd)
			{
				if (0 && *pch == '\n')
				{
					eaiPush(&pLayout->piLineBreaks, ch);
					if (fRunningWidth > pLayout->siMaxLineWidth)
						pLayout->siMaxLineWidth = round(fRunningWidth);
					fRunningWidth = pChatLog->fHangingIndent;
					ch++;
					pch++;
					chBest = -1;
					chSecondBest = -1;
					continue;
				}

				// The illusion of speed, ideally the text will be typically
				// standard ASCII and all the chat logs will be of the same font.
				// So use a shared lookup table for the character widths.
				if ((*pch & 0xFF) <= 127)
				{
					if (!s_fQuickWidths[*pch])
						s_fQuickWidths[*pch] = ttGetGlyphWidth(s_pQuickWidthFont, *pch & 0xff, 1, 1);
					fRunningWidth += s_fQuickWidths[*pch] * fScale;

					if (fRunningWidth >= fWidth && (chBest >= 0 || chSecondBest >= 0))
					{
						if (chBest < 0 || chBest > ch)
						{
							chBest = chSecondBest;
							fBestWidth = fSecondBestWidth;
						}

						eaiPush(&pLayout->piLineBreaks, chBest);
						if (fBestWidth > pLayout->siMaxLineWidth)
							pLayout->siMaxLineWidth = round(fBestWidth);
						fRunningWidth = fRunningWidth - fBestWidth + pChatLog->fHangingIndent;
						chBest = -1;
					}
					else if (s_WordBreak[*pch])
					{
						// This would be an ideal place to break
						fBestWidth = fRunningWidth;
						chBest = ch + 1;
					}

					fSecondBestWidth = fRunningWidth;
					chSecondBest = ch;

					ch++;
					pch++;
				}
				else
				{
					fRunningWidth += ttGetGlyphWidth(s_pQuickWidthFont, UTF8ToWideCharConvert(pch), fScale, fScale);

					if (fRunningWidth >= fWidth && (chBest >= 0 || chSecondBest >= 0))
					{
						if (chBest < 0 || chBest > ch)
						{
							chBest = chSecondBest;
							fBestWidth = fSecondBestWidth;
						}

						eaiPush(&pLayout->piLineBreaks, chBest);
						if (fBestWidth > pLayout->siMaxLineWidth)
							pLayout->siMaxLineWidth = round(fBestWidth);
						fRunningWidth = fRunningWidth - fBestWidth + pChatLog->fHangingIndent;
						chBest = -1;
					}

					fSecondBestWidth = fRunningWidth;
					chSecondBest = ch;

					ch += UTF8GetCodepointLength(pch);
					pch = UTF8GetNextCodepoint(pch);
				}
			}
		}

		pLayout->siHeight = (eaiSize(&pLayout->piLineBreaks) + 1) * ui_StyleFontLineHeight(pFont, fScale);
		pLayout->fScale = fScale;
	}
	else
	{
		eaiClearFast(&pLayout->piLineBreaks);
		pLayout->siHeight = 0;
		pLayout->siMaxLineWidth = 0;
		pLayout->fScale = fScale;
	}
}

void ui_GenLayoutEarlyChatLog(UIGen *pGen)
{
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	UIGenChatLog *pChatLog = UI_GEN_RESULT(pGen, ChatLog);
	UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
	UIGenScrollbar *pScrollbar = &pChatLog->scrollbar;
	UIGenScrollbarState *pScrollbarState = &pState->scrollbar;
	UIStyleFont *pFont = GET_REF(pChatLog->hFont);
	F32 fFontScale = getFontScale(pGen, pConfig);
	CBox origBox = pGen->ScreenBox;
	F32 fX = pGen->ScreenBox.left;
	F32 fY = pGen->ScreenBox.top;
	F32 fW = pGen->ScreenBox.right - pGen->ScreenBox.left - ui_GenScrollbarWidth(pScrollbar, pScrollbarState) * pGen->fScale;
	F32 fH = pGen->ScreenBox.bottom - pGen->ScreenBox.top;
	F32 fLastH = pState->fLastHeight;
	S32 iVirtualHeight = 0;
	S32 iEntry = -1;
	S32 i, n;
	bool bReformat = false;
	bool bAutoScroll = false;
	bool bResetFade = false;

	ui_GenScrollbarBox(pScrollbar, pScrollbarState, &pGen->ScreenBox, &pGen->ScreenBox, pGen->fScale);

	fX = pGen->ScreenBox.left;
	fY = pGen->ScreenBox.top;
	fW = floorf(CBoxWidth(&pGen->ScreenBox));
	fH = floorf(CBoxHeight(&pGen->ScreenBox));

	if (fW <= 0.0f || fH <= 0.0f || !eaSize(&pState->eaEntryLayouts) || !pFont)
	{
		ui_GenLayoutScrollbar(pGen, &pChatLog->scrollbar, &pState->scrollbar, iVirtualHeight);
		pGen->ScreenBox = origBox;
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	if (!nearf(pState->fLastHeight, fH))
	{
		pState->fLastHeight = fH;
	}

	if (!nearf(pState->fLastWidth, fW) || !nearf(pState->fLastFontScale, fFontScale))
	{
		pState->fLastWidth = fW;
		pState->fLastFontScale = fFontScale;
		pState->iLayoutVersion++;
	}

	if (pState->pFont != GET_REF(pFont->hFace))
	{
		bReformat = true;
		pState->pFont = GET_REF(pFont->hFace);
	}

	if (g_ChatLogVersion != pState->iChatLogVersion)
	{
		bReformat = true;
		pState->iChatLogVersion = g_ChatLogVersion;
	}

	if (pChatLog->bTextFading != pState->bLastTextFading)
	{
		// Reset the fading time if bTextFading was just turned on.
		//
		// FIXME(jm): This probably ideally should just be a flag on
		// the state that an expression function sets. But I'm just
		// being lazy so I don't have to update all the Gens.
		if (pChatLog->bTextFading)
			bResetFade = true;
		pState->bLastTextFading = pChatLog->bTextFading;
	}

	// Do a single pass through the entries
	PERFINFO_AUTO_START("Format Entries", eaSize(&pState->eaEntryLayouts));
	n = eaSize(&pState->eaEntryLayouts);
	for (i = 0; i < n; ++i)
	{
		UIGenChatLogEntryLayout *pLayout = pState->eaEntryLayouts[i];
		if (bReformat || pLayout->siHeight == 0 || fW <= pLayout->siMaxLineWidth || pState->iLayoutVersion != pLayout->iLayoutVersion)
		{
			if (iEntry < 0 || pLayout->id - g_ChatLog[iEntry]->id >= 16)
			{
				// It'll probably be faster to just do a binary lookup
				iEntry = findChatLogEntryByID(pLayout->id);
			}
			else
			{
				// Scan forward linearly.
				while (iEntry < eaSize(&g_ChatLog) && g_ChatLog[iEntry]->id < pLayout->id)
					iEntry++;
			}

			if (devassertmsg(iEntry >= 0 && iEntry < eaSize(&g_ChatLog), "Have EntryLayout for non-existant ChatLogEntry"))
			{
				flowEntryLayout(pChatLog, pConfig, g_ChatLog[iEntry], pLayout, fW, pState->fLastFontScale);
				pLayout->iLayoutVersion = pState->iLayoutVersion;
			}
		}
		// Check to see if the fade needs to be reset.
		if (bResetFade)
			pLayout->uFadeTime = gGCLState.totalElapsedTimeMs;
		iVirtualHeight += pLayout->siHeight;
	}
	PERFINFO_AUTO_STOP();

	bAutoScroll = pState->scrollbar.fScrollPosition + fLastH + 1 >= pState->scrollbar.fTotalHeight;
	ui_GenLayoutScrollbar(pGen, &pChatLog->scrollbar, &pState->scrollbar, iVirtualHeight);
	if (bAutoScroll)
		ScrollToEnd(pGen, &pState->scrollbar);

	pGen->ScreenBox = origBox;
	PERFINFO_AUTO_STOP_FUNC();
}

void ui_GenTickEarlyChatLog(UIGen *pGen)
{
	UIGenChatLog *pChatLog = UI_GEN_RESULT(pGen, ChatLog);
	UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	S32 iMouseX;
	S32 iMouseY;
	S32 i, n, iEntry;
	F32 x = 0, y = 0, lastY = 0;
	bool bScrollToMouse = false;
	bool bInputHandled = inpCheckHandled();
	bool bMouseInside = false;
	CBox ScreenBox;
	UIGenChatLogEntryPoint MouseOver = {-1};
	UIGenChatLogEntryPoint SoftMouseOver = {-1};
	ChatLink *pHitLink = NULL;

	if (!GET_REF(pChatLog->hFont))
	{
		ui_GenTickScrollbar(pGen, &pChatLog->scrollbar, &pState->scrollbar);
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	ui_GenTickScrollbar(pGen, &pChatLog->scrollbar, &pState->scrollbar);
	ui_GenScrollbarBox(&pChatLog->scrollbar, &pState->scrollbar, &pGen->ScreenBox, &ScreenBox, pGen->fScale);

	mousePos(&iMouseX, &iMouseY);

	PERFINFO_AUTO_START("Find MouseOver Coordinate", 1);
	n = eaSize(&pState->eaEntryLayouts);
	y = ScreenBox.top + (-pState->scrollbar.fScrollPosition);
	for (iEntry = 0; iEntry < n - 1 && y + pState->eaEntryLayouts[iEntry]->siHeight < iMouseY; iEntry++)
		y += pState->eaEntryLayouts[iEntry]->siHeight;

	if (iEntry < n /*&& y <= iMouseY && iMouseY < y + pState->eaEntryLayouts[iEntry]->siHeight*/)
	{
		// Mouse is inside this entry
		UIGenChatLogEntryLayout *pLayout = pState->eaEntryLayouts[iEntry];
		S32 iEntryIndex = findChatLogEntryByID(pLayout->id);
		ChatLogEntry *pEntry = iEntryIndex >= 0 ? g_ChatLog[iEntryIndex] : NULL;
		UIStyleFont *pFont = GET_REF(pChatLog->hFont);
		GfxFont *pGfxFont = pFont ? GET_REF(pFont->hFace) : NULL;
		F32 fScale = pLayout->fScale;
		S32 iLineHeight = ui_StyleFontLineHeight(pFont, fScale);
		S32 ch = 0, chEnd = -1;

		// Determine which line of the entry it's over
		S32 iLine = (iMouseY - y) / MAX(iLineHeight, 1);
		iLine = iLine < 0 ? 0 : iLine > eaiSize(&pLayout->piLineBreaks) ? eaiSize(&pLayout->piLineBreaks) : iLine;
		if (pEntry && iLine <= eaiSize(&pLayout->piLineBreaks))
		{
			chEnd = iLine >= eaiSize(&pLayout->piLineBreaks) ? (S32)strlen(pEntry->pchFormattedText) : pLayout->piLineBreaks[iLine];
			if (iLine > 0)
				ch = pLayout->piLineBreaks[iLine - 1];
		}

		// Adjust for the hanging indent.
		x = iLine > 0 ? ScreenBox.left + pChatLog->fHangingIndent : ScreenBox.left + pChatLog->fIndent;

		// Locate character in line
		if (pEntry && pGfxFont)
		{
			S32 chEndPos = -1;

			// Check to see if the mouse is at the beginning of the line
			if (iMouseX && iMouseX < x)
			{
				SoftMouseOver.iEntry = iEntry;
				SoftMouseOver.iSpan = 0;
				SoftMouseOver.iPos = -1;
			}
			// The magic of the C spec: else SoftMouseOver.iPos = 0

			for (i = 0; i < eaSize(&pEntry->eaFormatSpans) && MouseOver.iEntry < 0 && ch <= chEnd; ++i)
			{
				ChatLogFormatSpan *pSpan = pEntry->eaFormatSpans[i];
				char *pch, *pchEnd;
				if (pSpan->iStart + pSpan->iLength <= ch || !layoutIncludesSpan(pChatLog, pConfig, pEntry, pSpan))
					continue;

				// Figure out the boundaries to scan
				ch = MAX(pSpan->iStart, ch);
				pch = pEntry->pchFormattedText + ch;
				chEndPos = MIN(pSpan->iStart + pSpan->iLength, chEnd);
				pchEnd = pEntry->pchFormattedText + chEndPos;

				// Special handling for @handles
				if (pSpan->pLink && pSpan->pLink->eType == kChatLinkType_PlayerHandle)
				{
					char *pchAt = strchr(pEntry->pchFormattedText + ch, '@');
					if (pConfig && pConfig->bHideAccountNames && !(pchAt == pEntry->pchFormattedText + ch))
					{
						//if hideAccountNames and there is a character name (@ is not first char)
						pchEnd = pchAt;
					}
				}

				// Set it to the location of the first character scanned
				if (SoftMouseOver.iPos < 0)
					SoftMouseOver.iPos = ch - pSpan->iStart;
				else if (SoftMouseOver.iEntry < 0)
					SoftMouseOver.iPos = pSpan->iLength - 1;

				// Scan the segment
				while (pch < pchEnd)
				{
					F32 fCharWidth = ttGetGlyphWidth(pGfxFont, UTF8ToWideCharConvert(pch), pLayout->fScale, pLayout->fScale);
					if (x <= iMouseX && iMouseX < x + fCharWidth)
					{
						// MouseOver debugging
						pState->MouseOverDebug.lx = x;
						pState->MouseOverDebug.ly = y + iLine * iLineHeight;
						pState->MouseOverDebug.hx = x + fCharWidth;
						pState->MouseOverDebug.hy = y + (iLine + 1) * iLineHeight;

						// Fill in mouse over information
						MouseOver.iEntry = iEntry;
						MouseOver.iSpan = i;
						MouseOver.iPos = ch - pSpan->iStart;
						SoftMouseOver = MouseOver;
						pHitLink = pSpan->pLink;
						chEnd = -1;
						break;
					}
					x += fCharWidth;
					ch += UTF8GetCodepointLength(pch);
					pch += UTF8GetCodepointLength(pch);
				}
			}

			// If there's no soft MouseOver, set it to the last char on the line
			if (SoftMouseOver.iEntry < 0)
			{
				SoftMouseOver.iEntry = iEntry;
				SoftMouseOver.iSpan = MIN(i, eaSize(&pEntry->eaFormatSpans) - 1);
			}
		}

		// MouseOver debugging
		pState->MouseOverDebugLine.lx = iLine > 0 ? ScreenBox.left + pChatLog->fHangingIndent : ScreenBox.left + pChatLog->fIndent;
		pState->MouseOverDebugLine.ly = y + iLine * iLineHeight;
		pState->MouseOverDebugLine.hx = x;
		pState->MouseOverDebugLine.hy = y + (iLine + 1) * iLineHeight;
	}
	else
	{
		pState->MouseOverDebug.lx = 0;
		pState->MouseOverDebug.ly = 0;
		pState->MouseOverDebug.hx = 0;
		pState->MouseOverDebug.hy = 0;
		pState->MouseOverDebugLine.lx = 0;
		pState->MouseOverDebugLine.ly = 0;
		pState->MouseOverDebugLine.hx = 0;
		pState->MouseOverDebugLine.hy = 0;
	}
	PERFINFO_AUTO_STOP();

	if (SoftMouseOver.iEntry >= 0 && mouseCollision(&ScreenBox))
	{
		// Is the hit link different?
		if (pState->pActiveLink != pHitLink)
		{
			UIGenLinkHandlerAction *pHandler = pHitLink ? eaIndexedGetUsingInt(&pChatLog->eaLinkHoverHandlers, pHitLink->eType) : NULL;
			pState->pActiveLink = pHitLink;
			pState->iActiveLinkID = pHitLink ? pState->eaEntryLayouts[SoftMouseOver.iEntry]->id : -1;
			if (pHandler)
				ui_GenRunAction(pGen, &pHandler->OnLinkClicked);
		}

		// Handle click action
		if (pHitLink && pChatLog->eLinkButton >= 0 && mouseClick(pChatLog->eLinkButton))
		{
			UIGenLinkHandlerAction *pHandler = eaIndexedGetUsingInt(&pChatLog->eaLinkClickedHandlers, pHitLink->eType);
			if (pHandler)
			{
				ui_GenRunAction(pGen, &pHandler->OnLinkClicked);
				bInputHandled = true;
			}
		}

		// Start selecting
		if (!bInputHandled && mouseIsDown(MS_LEFT) && ui_GenInState(pGen, kUIGenStateLeftMouseDownStartedOver) && !pState->bSelecting)
		{
			pState->SelectionStart = SoftMouseOver;
			pState->SelectionEnd = SoftMouseOver;
			pState->bSelecting = true;
		}
	}
	else
	{
		pState->pActiveLink = NULL;
		pState->iActiveLinkID = -1;
	}

	// Capture the selection
	if (!mouseIsDown(MS_LEFT) && pState->bSelecting)
	{
		pState->bSelecting = false;
		if (pChatLog->bCopyOnSelect)
		{
			ui_GenExprChatLog_CopySelectionToClipboard(pGen);
			ui_GenExprChatLog_ClearSelection(pGen);
		}
	}

	if (pState->bSelecting)
	{
		// Update the end of the selection
		pState->SelectionEnd = SoftMouseOver;

		// Adjust the scrollbar if the mouse is outside of the screen box.
		if (iMouseY < ScreenBox.ly)
		{
			AdjustScrollPosition(pGen, &pState->scrollbar, -(ScreenBox.ly - iMouseY));
		}
		else if (iMouseY >= ScreenBox.hy)
		{
			AdjustScrollPosition(pGen, &pState->scrollbar, iMouseY - ScreenBox.hy);
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Get the color to use for the link.
static U32 getSpanColor(UIGenChatLog *pChatLog, ChatConfig *pConfig, ChatLogEntry *pEntry, ChatLogFormatSpan *pSpan, U32 uEntryColor, ChatLink *pActiveLink)
{
	const ChatMessage *pMsg = pEntry->pMsg;
	ChatLink *pLink = pSpan->pLink;

	if (pLink)
	{
		U32 iLinkColor = pChatLog->uiDefaultLinkColor;

		switch (pLink->eType)
		{
		xcase kChatLinkType_PlayerHandle:
			if (pLink->bIsGM || pLink->bIsDev)
				iLinkColor = ChatCommon_GetChatColor(pConfig, kChatLogEntryType_Admin, NULL, ChatCommon_GetChatConfigSourceForEntity(NULL));
			else if (pChatLog->uiPlayerLinkColor)
				iLinkColor = pChatLog->uiPlayerLinkColor;
			else
				iLinkColor = ChatClient_GetChatLogPlayerHandleLinkColor(pConfig, pMsg->eType, pMsg->pchChannel);
		xcase kChatLinkType_Item:
		case kChatLinkType_ItemDef:
			iLinkColor = ChatClient_GetChatLogItemLinkColor(pLink, &pSpan->pItem);
		xcase kChatLinkType_PowerDef:
			if (pChatLog->uiPowerLinkColor)
				iLinkColor = pChatLog->uiPowerLinkColor;
			else
				iLinkColor = 0x00FFFFFFFF;
		}

		if (pActiveLink == pLink)
			iLinkColor = ui_GenChatLog_Highlight(iLinkColor, 0.5);
		return iLinkColor;
	}

	return uEntryColor;
}

// Draw a layout, note this is dumb and will not automatically clip
// lines outside of the screen box.
//
// It could probably be optimized to do that, if it's still too slow,
// because of all the little segments it ends up having to draw.
//
// There is probably a ton of room for improvements.
static S32 drawEntryLayout(UIGenChatLog *pChatLog, ChatConfig *pConfig, ChatLogEntry *pEntry, UIGenChatLogEntryLayout *pLayout, F32 fX, F32 fY, F32 fZ, U8 chAlpha, bool bHideHandle, S32 iSelectionStart, S32 iSelectionEnd, ChatLink *pActiveLink)
{
	S32 i, n, iLine, chLineEnd;
	F32 x, y, fScale = pLayout->fScale, fLineHeight = gfxfont_FontHeight(g_font_Active, fScale);
	U32 uEntryColor, uColor;
	S32 iRenderedSprites = 0;

	// Draw the requested alpha
	gfxfont_SetAlpha(chAlpha);

	// Set the entry color
	uEntryColor = ChatCommon_GetChatColor(pConfig, pEntry->pMsg->eType, pEntry->pMsg->pchChannel, ChatCommon_GetChatConfigSourceForEntity(NULL));

	// Prepare for the first line
	x = fX;
	y = fY;
	iLine = 0;
	chLineEnd = eaiSize(&pLayout->piLineBreaks) > iLine ? pLayout->piLineBreaks[iLine] : (S32)strlen(pEntry->pchFormattedText);
	uColor = uEntryColor;
	gfxfont_SetColorRGBA((uColor & 0xffffff00) | chAlpha, (uColor & 0xffffff00) | chAlpha);

	// Loop through the spans
	n = eaSize(&pEntry->eaFormatSpans);
	for (i = 0; i < n; ++i)
	{
		ChatLogFormatSpan *pSpan = pEntry->eaFormatSpans[i];
		const char *pchText;
		S32 ch, chSpanEnd, chEnd, chAt;
		U32 uSpanColor, uOtherColor = 0;
		S32 iFormatLen = (S32)strlen(pEntry->pchFormattedText);
		Color color = {0,0,0,0};
		if (!layoutIncludesSpan(pChatLog, pConfig, pEntry, pSpan))
			continue;

		pchText = pEntry->pchFormattedText;
		ch = pSpan->iStart;
		chAt = chSpanEnd = ch + pSpan->iLength;
		if (!devassertmsgf(chSpanEnd <= iFormatLen, "Invalid span (%d, %d, %d)", iFormatLen, pSpan->iStart, pSpan->iLength))
			chSpanEnd = iFormatLen;

		// Special handling for @handles
		if (pSpan->pLink && pSpan->pLink->eType == kChatLinkType_PlayerHandle)
		{
			const char *pchAt = strchr(pchText + ch, '@');
			chAt = pchAt && pchAt < pchText + chSpanEnd ? pchAt - pchText : chSpanEnd;
			if (bHideHandle && chAt != ch)
				chSpanEnd = chAt;
		}

		// Set the color
		uSpanColor = getSpanColor(pChatLog, pConfig, pEntry, pSpan, uEntryColor, pActiveLink);
		if (uColor != uSpanColor)
		{
			uColor = uSpanColor;
			setColorFromRGBA(&color, (uColor & 0xffffff00) | chAlpha);
			gfxfont_SetColor(color, color);
		}

		if (chAt < chSpanEnd)
		{
			// Determine handle color
			Color baseColor = { (uColor >> 24) & 0xff, (uColor >> 16) & 0xff, (uColor >> 8) & 0xff, uColor & 0xff };
			Color darkerColor = ColorDarkenPercent(baseColor, 0.6);
			uOtherColor = darkerColor.r << 24 | darkerColor.g << 16 | darkerColor.b << 8 | darkerColor.a;
		}

		do
		{
			Vec2 v2Size;
			S32 iChar, iCodepoints = 0;

			PERFINFO_AUTO_START("Draw Span", 1);

			if (ch < chAt && chAt < chSpanEnd)
			{
				chEnd = MIN(chAt, chLineEnd);
			}
			else
			{
				// Switch to handle color
				if (chAt < chSpanEnd && uColor != uOtherColor)
				{
					uColor = uOtherColor;
					setColorFromRGBA(&color, (uColor & 0xffffff00) | chAlpha);
					gfxfont_SetColor(color, color);
				}

				chEnd = MIN(chSpanEnd, chLineEnd);
			}

			// Clip to the selection boundaries
			if (iSelectionStart > ch && iSelectionStart >= 0)
				chEnd = MIN(chEnd, iSelectionStart);
			if (iSelectionEnd > ch && iSelectionEnd >= 0)
				chEnd = MIN(chEnd, iSelectionEnd);

			// Need to count codepoints
			for (iChar = ch; iChar < chEnd; iCodepoints++)
				iChar += UTF8GetCodepointLength(pchText + iChar);

			// TODO: Eliminate this measure string
			gfxfont_Dimensions(g_font_Active, fScale, fScale, pchText + ch, iCodepoints, &v2Size[0], &v2Size[1], NULL, false);

			// If this is in the selection, draw the selection indicator
			if (ch >= iSelectionStart && ch < iSelectionEnd)
			{
				CBox box = { x, y, x + v2Size[0], y + fLineHeight };
				display_sprite_box(white_tex_atlas, &box, fZ, pChatLog->uiSelectionColor);
				iRenderedSprites++;
			}

			// Draw underline for link
			if (pSpan->pLink)
			{
				U8 uThickness = pSpan->pLink == pActiveLink ? pChatLog->uShowLinkHoverUnderline : pChatLog->uShowLinkUnderline;
				if (uThickness)
				{
					gfxDrawLineWidth(x, y + v2Size[1], fZ, x + v2Size[0], y + v2Size[1], color, uThickness);
					iRenderedSprites++;
				}
			}

			gfxfont_PrintEx(g_font_Active, x, y + v2Size[1], fZ, fScale, fScale, 0, pchText + ch, iCodepoints, NULL);
			iRenderedSprites += chEnd - ch;

			if (s_bGenChatLogDebugSpans)
			{
				CBox debugBox = { x, y, x + v2Size[0], y + fLineHeight };
				gfxDrawCBox(&debugBox, fZ, ColorCyan);
			}

			x += v2Size[0];
			ch = chEnd;

			if (ch >= chLineEnd)
			{
				// Start new line
				x = fX + pChatLog->fHangingIndent;
				y += fLineHeight;
				iLine++;
				chLineEnd = eaiSize(&pLayout->piLineBreaks) > iLine ? pLayout->piLineBreaks[iLine] : iFormatLen;
				while (*(pchText + ch) == ' ' || *(pchText + ch) == '\n' || *(pchText + ch) == '\r' || *(pchText + ch) == '\t')
					++ch;
			}

			PERFINFO_AUTO_STOP();
		} while (ch < chSpanEnd);
	}

	return iRenderedSprites;
}

void ui_GenDrawEarlyChatLog(UIGen *pGen)
{
	static UIGenChatLogEntryLayout **s_eaVisibleSpans;
	static S32 *s_eaiSpanIndexes;
	UIGenChatLog *pChatLog = UI_GEN_RESULT(pGen, ChatLog);
	UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
	ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
	CBox ScreenBox;
	S32 i, n;
	F32 y;
	F32 fStartY = 0;
	F32 z = UI_GET_Z();
	bool bHideHandle = pConfig && pConfig->bHideAccountNames;
	UIGenChatLogEntryPoint SelectionStart, SelectionEnd;
	ChatLink *pActiveLink = pState->pActiveLink;
	F32 fFadeStartTime = (pConfig ? pConfig->siTimeRequiredToStartFading : 0) / 1000.0f;
	F32 fFadeEndTime = (pConfig ? pConfig->siTimeRequiredToStartFading + pConfig->siFadeAwayDuration : 0) / 1000.0f;
	U8 uBaseAlpha = pConfig && pConfig->bTextFadesWithWindow ? pGen->chAlpha : 255;
	S32 iRenderedSprites = 0;

	PERFINFO_AUTO_START_FUNC();

	normalizeSelectionOrder(pState, &SelectionStart, &SelectionEnd);

	ui_GenScrollbarBox(&pChatLog->scrollbar, &pState->scrollbar, &pGen->ScreenBox, &ScreenBox, pGen->fScale);

	PERFINFO_AUTO_START("Find Visible Entries", 1);
	eaClearFast(&s_eaVisibleSpans);
	eaiClearFast(&s_eaiSpanIndexes);
	n = eaSize(&pState->eaEntryLayouts);
	y = ScreenBox.ly + -pState->scrollbar.fScrollPosition;
	for (i = 0; i < n && y < ScreenBox.hy; i++)
	{
		UIGenChatLogEntryLayout *pLayout = pState->eaEntryLayouts[i];
		if (ScreenBox.ly <= y || ScreenBox.ly <= y + pLayout->siHeight)
		{
			if (!eaSize(&s_eaVisibleSpans))
				fStartY = y;
			eaPush(&s_eaVisibleSpans, pLayout);
			eaiPush(&s_eaiSpanIndexes, i);
		}
		y += pLayout->siHeight;
	}
	PERFINFO_AUTO_STOP();

	n = eaSize(&s_eaVisibleSpans);
	if (n > 0)
	{
		S32 iEntry = -1;
		PERFINFO_AUTO_START("Draw Visible Entries", eaSize(&s_eaVisibleSpans));
		y = fStartY;
		ui_StyleFontUse(GET_REF(pChatLog->hFont), false, kWidgetModifier_None);
		for (i = 0; i < n; i++)
		{
			UIGenChatLogEntryLayout *pLayout = s_eaVisibleSpans[i];

			if (iEntry < 0 || pLayout->id - g_ChatLog[iEntry]->id >= 16)
			{
				// It'll probably be faster to just do a binary lookup
				iEntry = findChatLogEntryByID(pLayout->id);
			}
			else
			{
				// Scan forward linearly.
				while (iEntry < eaSize(&g_ChatLog) && g_ChatLog[iEntry]->id < pLayout->id)
					iEntry++;
			}

			// Draw the layout
			if (devassertmsg(iEntry >= 0 && iEntry < eaSize(&g_ChatLog), "Have EntryLayout for non-existant ChatLogEntry"))
			{
				ChatLogEntry *pEntry = g_ChatLog[iEntry];
				S32 iIndex = s_eaiSpanIndexes[i];
				S32 iSelectionStart = -1, iSelectionEnd = -1;
				U8 chAlpha = uBaseAlpha;

				// Figure out what's selected in the current entry
				if (SelectionStart.iEntry <= iIndex && iIndex <= SelectionEnd.iEntry)
				{
					if (SelectionStart.iEntry == iIndex && SelectionStart.iSpan < eaSize(&g_ChatLog[iEntry]->eaFormatSpans))
						iSelectionStart = g_ChatLog[iEntry]->eaFormatSpans[SelectionStart.iSpan]->iStart + SelectionStart.iPos;
					else
						iSelectionStart = 0;

					if (SelectionEnd.iEntry == iIndex && SelectionEnd.iSpan < eaSize(&g_ChatLog[iEntry]->eaFormatSpans))
						iSelectionEnd = g_ChatLog[iEntry]->eaFormatSpans[SelectionEnd.iSpan]->iStart + SelectionEnd.iPos + 1;
					else
						iSelectionEnd = (S32)strlen(g_ChatLog[iEntry]->pchFormattedText);
				}

				// Figure out alpha of line
				if (pChatLog->bTextFading && fFadeStartTime)
				{
					F32 fTime = (gGCLState.totalElapsedTimeMs - pLayout->uFadeTime) / 1000.0f;
					if (fTime >= fFadeEndTime)
						chAlpha = 0;
					else if (fTime >= fFadeStartTime)
						chAlpha *= (fFadeEndTime - fTime) / (fFadeEndTime - fFadeStartTime);
				}

				// Draw the entry
				if (chAlpha != 0)
					iRenderedSprites += drawEntryLayout(pChatLog, pConfig, pEntry, pLayout, ScreenBox.lx, y, z, chAlpha, bHideHandle, iSelectionStart, iSelectionEnd, pActiveLink);
			}

			y += pLayout->siHeight;
		}
		PERFINFO_AUTO_STOP();
	}
	
	ui_GenDrawScrollbar(pGen, &pChatLog->scrollbar, &pState->scrollbar);

	if (s_bGenChatLogDebugMouseOver)
	{
		if (CBoxWidth(&pState->MouseOverDebugLine) >= 1 && CBoxHeight(&pState->MouseOverDebugLine) >= 1)
			gfxDrawCBox(&pState->MouseOverDebugLine, UI_GET_Z(), ColorMagenta);
		if (CBoxWidth(&pState->MouseOverDebug) >= 1 && CBoxHeight(&pState->MouseOverDebug) >= 1)
			gfxDrawCBox(&pState->MouseOverDebug, UI_GET_Z(), ColorMagenta);
	}

	if (s_bGenChatLogDebugSpriteCount)
	{
		static char *s_pchDebugBuffer = NULL;
		estrPrintf(&s_pchDebugBuffer, "Sprites: %d", iRenderedSprites);
		gfxfont_SetColorRGBA(0xFFFFFFFF, 0xFFFFFFFF);
		gfxfont_SetAlpha(pGen->chAlpha);
		gfxfont_Print(ScreenBox.lx, ScreenBox.hy - ui_StyleFontLineHeight(GET_REF(pChatLog->hFont), pGen->fScale), UI_GET_Z(), pGen->fScale, pGen->fScale, 0, s_pchDebugBuffer);
		estrClear(&s_pchDebugBuffer);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

//////////////////////////////////////////////////////////////////////////
// expressions for the chat log gen

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatLog_CopySelection);
void ui_GenExprChatLog_CopySelectionToClipboard(SA_PARAM_NN_VALID UIGen *pGen)
{
	static char *s_pch;

	if (UI_GEN_READY(pGen) && UI_GEN_IS_TYPE(pGen, kUIGenTypeChatLog))
	{
		UIGenChatLog *pChatLog = UI_GEN_RESULT(pGen, ChatLog);
		UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
		ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());

		if (pState->SelectionStart.iEntry >= 0 && pState->SelectionEnd.iEntry >= 0)
		{
			UIGenChatLogEntryPoint SelectionStart, SelectionEnd;
			S32 i, j, iEntry = -1;

			// Normalize the order of the selection position
			normalizeSelectionOrder(pState, &SelectionStart, &SelectionEnd);

			// Go through the selected entries
			for (i = SelectionStart.iEntry; i <= SelectionEnd.iEntry; ++i)
			{
				UIGenChatLogEntryLayout *pLayout = pState->eaEntryLayouts[i];

				if (iEntry < 0 || pLayout->id - g_ChatLog[iEntry]->id >= 16)
				{
					// It'll probably be faster to just do a binary lookup
					iEntry = findChatLogEntryByID(pLayout->id);
				}
				else
				{
					// Scan forward linearly.
					while (iEntry < eaSize(&g_ChatLog) && g_ChatLog[iEntry]->id < pLayout->id)
						iEntry++;
				}

				if (devassertmsg(iEntry >= 0 && iEntry < eaSize(&g_ChatLog), "Have EntryLayout for non-existant ChatLogEntry"))
				{
					ChatLogEntry *pEntry = g_ChatLog[iEntry];
					S32 iFirstSpan = i == SelectionStart.iEntry ? SelectionStart.iSpan : 0;
					S32 iLastSpan = i == SelectionEnd.iEntry ? SelectionEnd.iSpan : eaSize(&pEntry->eaFormatSpans) - 1;
					for (j = iFirstSpan; j <= iLastSpan; j++)
					{
						ChatLogFormatSpan *pSpan = pEntry->eaFormatSpans[j];
						S32 iFirstPos = i == SelectionStart.iEntry && j == SelectionStart.iSpan ? SelectionStart.iPos : 0;
						S32 iLastPos = i == SelectionEnd.iEntry && j == SelectionEnd.iSpan ? SelectionEnd.iPos : pSpan->iLength - 1;
						if (!layoutIncludesSpan(pChatLog, pConfig, pEntry, pSpan))
							continue;
						if (iLastPos + 1 - iFirstPos <= 0)
							continue;
						// Appending the spans
						estrConcat(&s_pch, pEntry->pchFormattedText + pSpan->iStart + iFirstPos, iLastPos + 1 - iFirstPos);
					}
					estrAppend2(&s_pch, "\r\n");
				}
			}
		}

		if (s_pch && *s_pch)
			winCopyUTF8ToClipboard(s_pch);
		estrClear(&s_pch);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatLog_SelectAll);
void ui_GenExprChatLog_SelectAll(SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_READY(pGen) && UI_GEN_IS_TYPE(pGen, kUIGenTypeChatLog))
	{
		UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
		if (pState)
		{
			UIGenChatLogEntryLayout *pLast = eaTail(&pState->eaEntryLayouts);
			S32 iLastEntry = pLast ? findChatLogEntryByID(pLast->id) : -1;
			ChatLogEntry *pLastEntry = iLastEntry >= 0 ? g_ChatLog[iLastEntry] : NULL;

			if (pLastEntry && eaSize(&pLastEntry->eaFormatSpans) > 0)
			{
				pState->SelectionStart.iEntry = 0;
				pState->SelectionStart.iSpan = 0;
				pState->SelectionStart.iPos = 0;
				pState->SelectionEnd.iEntry = eaSize(&pState->eaEntryLayouts) - 1;
				pState->SelectionEnd.iSpan = eaSize(&pLastEntry->eaFormatSpans) - 1;
				pState->SelectionEnd.iPos = eaTail(&pLastEntry->eaFormatSpans)->iLength - 1;
			}
			else
			{
				pState->SelectionStart = s_InvalidSelectionPoint;
				pState->SelectionEnd = s_InvalidSelectionPoint;
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ChatLog_ClearSelection);
void ui_GenExprChatLog_ClearSelection(SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_READY(pGen) && UI_GEN_IS_TYPE(pGen, kUIGenTypeChatLog))
	{
		UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
		if (pState)
		{
			pState->SelectionStart = s_InvalidSelectionPoint;
			pState->SelectionEnd = s_InvalidSelectionPoint;
		}
	}
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ChatLog_ScrollToEnd");
void ui_GenExprChatLog_ScrollToEnd(SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_READY(pGen) && UI_GEN_IS_TYPE(pGen, kUIGenTypeChatLog))
	{
		UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
		ScrollToEnd(pGen, &pState->scrollbar);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ChatLog_ReceivedChatMessageInActiveTab");
bool ui_GenExprChatLog_ReceivedChatMessageInActiveTab(SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_READY(pGen) && UI_GEN_IS_TYPE(pGen, kUIGenTypeChatLog))
	{
		UIGenChatLog *pChatLog = UI_GEN_RESULT(pGen, ChatLog);
		UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
		ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
		ChatTabConfig *pTabConfig = ChatCommon_GetTabConfig(pConfig, pChatLog->pchFilterTabGroup, pState->iFilterTab);
		return pTabConfig && ChatLog_HasReceivedNewMessage(pTabConfig->pchTitle);
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ChatLog_ReceivedChatMessageInTab");
bool ui_GenExprChatLog_ReceivedChatMessageInTab(SA_PARAM_NN_VALID UIGen *pGen, S32 iTabIndex)
{
	if (UI_GEN_READY(pGen) && UI_GEN_IS_TYPE(pGen, kUIGenTypeChatLog))
	{
		UIGenChatLog *pChatLog = UI_GEN_RESULT(pGen, ChatLog);
		UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
		ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
		ChatTabConfig *pTabConfig = ChatCommon_GetTabConfig(pConfig, pChatLog->pchFilterTabGroup, iTabIndex);
		return pTabConfig && ChatLog_HasReceivedNewMessage(pTabConfig->pchTitle);
	}
	return false;
}

// Deprecated. Stop using FilterTabGroup.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ChatLog_SetFilterTabIndex");
void ui_GenExprChatLog_SetFilterTabIndex(SA_PARAM_NN_VALID UIGen *pGen, S32 iTabIndex)
{
	if (UI_GEN_READY(pGen) && UI_GEN_IS_TYPE(pGen, kUIGenTypeChatLog))
	{
		UIGenChatLog *pChatLog = UI_GEN_RESULT(pGen, ChatLog);
		UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
		ChatConfig *pConfig = ClientChatConfig_GetChatConfig(entActivePlayerPtr());
		ChatTabConfig *pTabConfig = ChatCommon_GetTabConfig(pConfig, pChatLog->pchFilterTabGroup, iTabIndex);
		if (pTabConfig)
		{
			pState->iFilterTab = iTabIndex;
		}
	}
}

// Lighten the given color by fPercent.  Unless that doesn't change
// the color, then darken the color by 1-fPercent.
U32 ui_GenChatLog_Highlight(U32 iColor, F32 fPercent) {
	Color baseColor = { (iColor >> 24) & 0xff, (iColor >> 16) & 0xff, (iColor >> 8) & 0xff, iColor & 0xff };
	Color color = ColorLightenPercent(baseColor, fPercent);
	U32 iHighlightedColor = color.r << 24 | color.g << 16 | color.b << 8 | color.a;

	if (iHighlightedColor == iColor) {
		color = ColorDarkenPercent(baseColor, 1.f-fPercent);
		iHighlightedColor = color.r << 24 | color.g << 16 | color.b << 8 | color.a;
	}

	return iHighlightedColor;
}

bool ui_GenInputChatLog(UIGen *pGen, KeyInput *pKey)
{
	UIGenChatLog *pChatLog = UI_GEN_RESULT(pGen, ChatLog);
	UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
	if (pKey->type == KIT_EditKey)
	{
		switch (pKey->scancode)
		{
			case INP_NUMPAD8:
				// Ignore NUMPAD if NUMLOCK is on.
				if (pKey->attrib & KIA_NUMLOCK)
					break;
			case INP_UPARROW:
				scrollUpOneLine(pGen);
				return true;

			case INP_NUMPAD2:
				// Ignore NUMPAD if NUMLOCK is on.
				if (pKey->attrib & KIA_NUMLOCK)
					break;
			case INP_DOWNARROW:
				scrollDownOneLine(pGen);
				return true;

			case INP_NUMPAD7:
				// Ignore NUMPAD if NUMLOCK is on.
				if (pKey->attrib & KIA_NUMLOCK)
					break;
			case INP_HOME:
				scrollToBeginning(pGen);
				return true;

			case INP_NUMPAD1:
				// Ignore NUMPAD if NUMLOCK is on.
				if (pKey->attrib & KIA_NUMLOCK)
					break;
			case INP_END:
				scrollToEnd(pGen);
				return true;

			case INP_NUMPAD9:
				// Ignore NUMPAD if NUMLOCK is on.
				if (pKey->attrib & KIA_NUMLOCK)
					break;
			case INP_PGUP:
				scrollUpOnePage(pGen);
				return true;

			case INP_NUMPAD3:
				// Ignore NUMPAD if NUMLOCK is on.
				if (pKey->attrib & KIA_NUMLOCK)
					break;
			case INP_PGDN:
				scrollDownOnePage(pGen);
				return true;

			case INP_NUMPAD4:
			case INP_NUMPAD6:
				// Ignore NUMPAD if NUMLOCK is on.
				if (pKey->attrib & KIA_NUMLOCK)
					break;
			case INP_LEFTARROW:
			case INP_RIGHTARROW:
				// Note: These aren't mapped to anything, but we want to consume them
				// for better usability because all other related arrow & numpad keys
				// are consumed.  i.e. it's weird to be able to use left/right to affect
				// the character while up/down don't.
				return true;

			case INP_C:
				if (pKey->attrib & KIA_CONTROL)
				{
					ui_GenExprChatLog_CopySelectionToClipboard(pGen);
					return true;
				}
				break;

			case INP_X:
				if (pKey->attrib & KIA_CONTROL)
				{
					ui_GenExprChatLog_CopySelectionToClipboard(pGen);
					return true;
				}
				break;

			case INP_A:
				if (pKey->attrib & KIA_CONTROL)
				{
					ui_GenExprChatLog_SelectAll(pGen);
					// TODO: Is this even useful?
					ui_GenExprChatLog_CopySelectionToClipboard(pGen);
					return true;
				}
				break;
		}
	}

	return false;
}

void ui_GenHideChatLog(UIGen *pGen)
{
	UIGenChatLogState *pState = UI_GEN_STATE(pGen, ChatLog);
	if (pState)
	{
		pState->pActiveLink = NULL;
		pState->iActiveLinkID = -1;
		eaDestroyStruct(&pState->eaEntryLayouts, parse_UIGenChatLogEntryLayout);
		pState->SelectionStart = s_InvalidSelectionPoint;
		pState->SelectionEnd = s_InvalidSelectionPoint;
		StructFreeStringSafe(&pState->pchChannel);
		StructFreeStringSafe(&pState->pchTab);
		StructFreeStringSafe(&pState->pchPMHandle);
		pState->iChatLogVersion = -1;
		pState->iFirstEntryID = -1;
		pState->iScanEntryID = -1;
	}
}

void ClientChat_ParseTellCommand(char* esFullCmd, OUT char **ppchSendName, OUT char **ppchSendMessage);

// If the text entry starts with a slash, check if the stuff following it is a channel name, followed by a space.
// If it is, strip all of that (including the space) and change the current chat channel to the requested one.
// Returns the handle of the user the tell will go to, if "/replyAll" or "/t name@handle, ", or "<clear>".
// TODO: This should handle "/t user@handle ".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Chat_ParseLeadingChatChannelChanges);
SA_ORET_OP_STR const char* exprFuncChat_ParseLeadingChatChannelChanges(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	static char* s_pTellHandle = NULL;
	estrClear(&s_pTellHandle);

	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry))
	{
		//UIGenTextEntry *pTextEntry = UI_GEN_RESULT(pGen, TextEntry);
		UIGenTextEntryState *pTextEntryState = UI_GEN_STATE(pGen, TextEntry);
		const char* pFullText = TextBuffer_GetText(pTextEntryState->pBuffer);

		//starts with "/"
		if( *pFullText == '/' )
		{
			static const char *sDelims = " \n,\t";
			const char * channelNameBegin = pFullText + 1;

			//the next few non-whitespace, non-comma characters are the channel name
			const char * channelNameEnd = channelNameBegin;
			while (*channelNameEnd && !strchr(sDelims, *channelNameEnd))
			{
				channelNameEnd++;
			}

			//followed by a space, and not 0-length
			if( *channelNameEnd == ' ' && channelNameEnd > channelNameBegin )
			{
				// Copy the channel name, including the trailing space
				char *channelName = NULL;
				const char *pchCommandToExecute = NULL;
				estrCreate(&channelName);
				estrSetSize(&channelName, channelNameEnd - channelNameBegin + 1);
				memcpy(channelName, channelNameBegin, channelNameEnd - channelNameBegin);
				// replace the trailing space with a \0
				channelName[channelNameEnd-channelNameBegin] = '\0';

				// Check if this is a command alias
				pchCommandToExecute = gclCommandAliasGetToExecute(channelName);
				if( pchCommandToExecute )
				{
					//We expect the command aliases to be of the form:
					//  Team {} $$ Channel_Setcurrent Party

					//the next few non-whitespace, non-comma characters are the channel name
					const char * pSetCurrentStart = pchCommandToExecute; //strstr(pchCommandToExecute, "Channel_Setcurrent ");
					if( pSetCurrentStart )
					{
						const char *pchSystemName = LOCAL_CHANNEL_NAME;
						const char* commandChannelNameBegin = pSetCurrentStart; // + strlen("Channel_Setcurrent ");
						const char * commandChannelNameEnd = commandChannelNameBegin;
						while (*commandChannelNameEnd && !strchr(sDelims, *commandChannelNameEnd))
						{
							commandChannelNameEnd++;
						}

						// These won't be followed by a space necessarily
						if( commandChannelNameEnd > commandChannelNameBegin )
						{
							estrSetSize(&channelName, commandChannelNameEnd - commandChannelNameBegin);
							memcpy(channelName, commandChannelNameBegin, commandChannelNameEnd - commandChannelNameBegin);
							channelName[commandChannelNameEnd-commandChannelNameBegin] = '\0';
						}

						// Now convert from "Party" to the real system name for that channel,
						// but leave "Local" as "Local", since it's not really a real channel anyway.
						if( stricmp(channelName, LOCAL_CHANNEL_NAME) )
							pchSystemName = ClientChat_GetSubscribedChannelSystemName(channelName);

						if(pchSystemName)
							estrCopy2(&channelName, pchSystemName);

					}
				}

				if( !stricmp(channelName, "tell") )
				{
					// Parse out the recipient's name from the rest of the message.
					char *esFullCmd = StructAllocString(channelNameEnd+1);
					char *pchSendName = NULL;
					char *pchSendMessage = NULL;
					ClientChat_ParseTellCommand(esFullCmd, &pchSendName, &pchSendMessage);
					if( pchSendName && *pchSendName && pchSendMessage )
					{
						S32 iCursorOld = TextBuffer_GetCursor(pTextEntryState->pBuffer);
						TextBuffer_SetText(pTextEntryState->pBuffer, pchSendMessage);
						TextBuffer_SetCursor(pTextEntryState->pBuffer, iCursorOld - (pchSendMessage+1-esFullCmd) - (channelNameEnd+1-channelNameBegin));

						estrPrintf(&s_pTellHandle, "%s", pchSendName);
					}
					free(esFullCmd);
				}
				else if( !stricmp(channelName, "replylast") )
				{
					// Replace "/reply " with "/tell <LastSender> "
					if( ClientChat_LastTellCorrespondent() ) // Only change the reply text entry if there's a known correspondent to send to
					{
						S32 iCursorOld = TextBuffer_GetCursor(pTextEntryState->pBuffer);
						TextBuffer_SetText(pTextEntryState->pBuffer, channelNameEnd+1);
						TextBuffer_SetCursor(pTextEntryState->pBuffer, iCursorOld - (1+channelNameEnd+1-channelNameBegin));

						estrPrintf(&s_pTellHandle, "%s", ClientChat_LastTellCorrespondent());
					}
				}
				else if( ClientChat_GetCachedChannelInfo(channelName) || // If the channel exists,
					!stricmp(channelName, LOCAL_CHANNEL_NAME) || 
					!stricmp(channelName, CHAT_ZONE_SHORTCUT) || 
					!stricmp(channelName, CHAT_GUILD_SHORTCUT) || 
					!stricmp(channelName, CHAT_GUILD_OFFICER_SHORTCUT) || 
					!stricmp(channelName, CHAT_TEAM_SHORTCUT)
					)
				{
					S32 iCursorOld = TextBuffer_GetCursor(pTextEntryState->pBuffer);

					// Strip the slash, channelname and space from pBuffer
					TextBuffer_SetText(pTextEntryState->pBuffer, channelNameEnd+1);
					TextBuffer_SetCursor(pTextEntryState->pBuffer, iCursorOld - (1+channelNameEnd+1-channelNameBegin));

					// Change to that channel
					ClientChat_SetCurrentChannelByName(channelName);

					estrPrintf(&s_pTellHandle, "<clear>");
				}

				estrDestroy(&channelName);
			}
		}
	}

	return s_pTellHandle;
}

//////////////////////////////////////////////////////////////////////////

AUTO_RUN;
void ui_GenRegisterChatLog(void)
{
	// We might go over the max a little bit while working.
	MP_CREATE(UIGenChatLogEntryLayout, MAX_CHAT_LOG_ENTRIES + 20);

	ui_GenRegisterType(kUIGenTypeChatLog, 
		UI_GEN_NO_VALIDATE,
		UI_GEN_NO_POINTERUPDATE,
		ui_GenUpdateChatLog,
		ui_GenLayoutEarlyChatLog, 
		UI_GEN_NO_LAYOUTLATE,
		ui_GenTickEarlyChatLog, 
		UI_GEN_NO_TICKLATE, 
		ui_GenDrawEarlyChatLog,
		UI_GEN_NO_FITCONTENTSSIZE, 
		UI_GEN_NO_FITPARENTSIZE, 
		ui_GenHideChatLog, 
		ui_GenInputChatLog, 
		UI_GEN_NO_UPDATECONTEXT, 
		UI_GEN_NO_QUEUERESET);
}

#include "UIGenChatLog_h_ast.c"