#pragma once
GCC_SYSTEM

#include "UIGen.h"
#include "gclUIGen.h"
#include "UIGenScrollbar.h"
#include "inputData.h"
#include "ChatData.h"
#include "NotifyEnum.h"

extern StaticDefineInt MouseButtonEnum[];

typedef struct ChatLink ChatLink;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenChatLogEntryLayout {
	S32 id; // Entry id -- corresponds to ID in ChatLogEntries
	S16 siMaxLineWidth; // The width of the longest line
	S16 siHeight; // The total height of the layout
	S32 *piLineBreaks; // The locations where the output should be broken up into new lines

	U32 uFadeTime; // The reference time used for fading
	S32 iLayoutVersion; // The version of layout this span corresponds to
	F32 fScale; // The scale this entry corresponds to
} UIGenChatLogEntryLayout;

AUTO_STRUCT;
typedef struct UIGenLinkHandlerAction
{
	ChatLinkType eType; AST(STRUCTPARAM KEY)
	UIGenAction OnLinkClicked; AST(EMBEDDED_FLAT)
} UIGenLinkHandlerAction;

AUTO_STRUCT;
typedef struct UIGenChatLogEntryPoint
{
	// The layout entry
	S32 iEntry; AST(DEFAULT(-1))
	S32 iSpan; // The span in the entry
	S16 iPos; // The position in the span
} UIGenChatLogEntryPoint;

AUTO_STRUCT;
typedef struct UIGenChatLog
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeChatLog))

	UIGenScrollbar scrollbar; AST(EMBEDDED_FLAT)

	// The font in which the chat log text will be rendered
	REF_TO(UIStyleFont) hFont; AST(NAME(Font))

	// The color used for links by default
	U32 uiDefaultLinkColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(DefaultLinkColor))

	// The color used for player links, if unspecified, then it will use the message type's color.
	U32 uiPlayerLinkColor; AST(SUBTABLE(ColorEnum) NAME(PlayerLinkColor))

	// The color used for power links, if unspecified, then it will use white.
	U32 uiPowerLinkColor; AST(SUBTABLE(ColorEnum) NAME(PowerLinkColor))

	// Deprecated: The chat log does not need to know which tab group is controlling it. Use TabExpr <& &> instead.
	char *pchFilterTabGroup; AST(NAME(FilterTabGroup) ESTRING)

	// Deprecated: Use the more flexible ChannelExpr <& &> instead.
	char *pchFilterChannel;	AST(NAME(FilterChannel) ESTRING)

	// Deprecated: Use the more flexible TabExpr <& &> instead.
	char *pchFilterTab; AST(NAME(FilterTab))

	// Used to show only private messages from the handle returned from this expression
	Expression *pPrivateMessageHandleExpr; AST(NAME(PrivateMessageHandleExpr) REDUNDANT_STRUCT(HandleExpr, parse_Expression_StructParam) LATEBIND)

	// The expression to display the contents for a given tab
	Expression *pDisplayTabExpr; AST(NAME(DisplayTabExprBlock) REDUNDANT_STRUCT(TabExpr, parse_Expression_StructParam) LATEBIND)

	// The channel to display
	Expression *pDisplayChannelExpr; AST(NAME(DisplayChannelExpr) REDUNDANT_STRUCT(ChannelExpr, parse_Expression_StructParam) LATEBIND)

	// The list of system notifications to display
	NotifyType *peNotifications; AST(NAME("FilterNotifyType"))

	// The mouse button to handle link clicks
	MouseButton eLinkButton; AST(DEFAULT(-1) SUBTABLE(MouseButtonEnum))

	// Link click actions
	UIGenLinkHandlerAction **eaLinkClickedHandlers; AST(NAME(LinkClicked))

	// Link hover actions
	UIGenLinkHandlerAction **eaLinkHoverHandlers; AST(NAME(LinkHovered))

	// The indent of the first line
	F32 fIndent; AST(NAME(Indent))

	// The indent of following lines, better known as the hanging indent
	F32 fHangingIndent; AST(NAME(HangingIndent) ADDNAMES(FollowingLineIndent) DEFAULT(10.0f))

	// The thickness of the link underline.
	U8 uShowLinkUnderline; AST(NAME(ShowLinkUnderline))

	// The thickness of the link underline, when it's being hovered.
	U8 uShowLinkHoverUnderline; AST(NAME(ShowLinkHoverUnderline))

	// The color of the selection
	U32 uiSelectionColor; AST(NAME(SelectionColor) DEFAULT(0x669999CC))

	// Hides the name of the channel
	bool bHideChannelName : 1; AST(NAME(HideChannelName))

	// Only show messages after the gen is created
	bool bShowNewMessages : 1; AST(NAME(ShowNewMessages) ADDNAMES(OnlyShowMessagesSentAfterGenIsCreated))

	// Copy the selected text to the clipboard when some text gets selected
	bool bCopyOnSelect : 1; AST(NAME(CopyOnSelect) DEFAULT(1))

	// Enable/disable the text fade alpha
	bool bTextFading : 1; AST(NAME(TextFading) DEFAULT(1))
} UIGenChatLog;

AUTO_STRUCT;
typedef struct UIGenChatLogState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeChatLog))
	UIGenScrollbarState scrollbar;

	// This is the link to be processed by the Link Clicked/Hover Expressions.
	ChatLink *pActiveLink;

	// The EntryLayout ID of the ActiveLink
	S32 iActiveLinkID;

	// The selection points, note, these points are inclusive.
	UIGenChatLogEntryPoint SelectionStart;
	UIGenChatLogEntryPoint SelectionEnd;

	// The mouse over point
	UIGenChatLogEntryPoint MouseOver;

	// The first ChatLogEntry ID to show
	S32 iFirstEntryID;

	// The ChatLogEntry ID to start scanning from
	S32 iScanEntryID;

	// The last filter information
	char *pchChannel;
	ChatLogEntryType eChannelType;
	char *pchTab;
	U32 uTabCRC;
	char *pchPMHandle;

	// The displayed entries
	UIGenChatLogEntryLayout **eaEntryLayouts;

	// The chat log version the layout information corresponds to
	S32 iChatLogVersion;

	// The layout version
	S32 iLayoutVersion;

	// The font to draw using, note, only used in pointer comparisons
	void *pFont; NO_AST

	// The mouse over debug box
	CBox MouseOverDebug; NO_AST
	CBox MouseOverDebugLine; NO_AST

	// The tab to filter by if using a FilterTabGroup.
	S32 iFilterTab;

	S32 iFramesSinceLastCompletedResize;
	F32 fLastWidth;
	F32 fLastHeight;
	F32 fLastFontScale;

	// Set if the gen is tracking selections
	bool bSelecting : 1;

	// Set the last state of the TextFading flag
	bool bLastTextFading : 1;
} UIGenChatLogState;
