/***************************************************************************



***************************************************************************/

#ifndef UI_TEXTAREA_H
#define UI_TEXTAREA_H
GCC_SYSTEM

#include "UICore.h"
#include "UIEditable.h"

typedef struct UISMFView UISMFView;
typedef struct UIButton UIButton;
typedef struct UITextShortcutTab UITextShortcutTab;

//////////////////////////////////////////////////////////////////////////
// A multiline text entry.

typedef struct UITextArea
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE UI_EDITABLE_TYPE);

	const unsigned char **lines;
	F32 lastWidth;

	UIActivationFunc finishedF;
	UserData finishedData;

	bool collapse : 1;
	int collapseHeight;
	bool wordWrap : 1;
	bool lastWordWrap : 1;
	bool usingDefault : 1;
	unsigned trimWhitespace : 1;

	// An attached set of SMF editing buttons
	unsigned smfEditIsActive : 1;
	UITextShortcutTab** smfEditTabs;
	UIWidget** smfEditWidgets;

	// An attached SMFView for previews
	UISMFView *smf;
	bool smfOpened;
} UITextArea;

AUTO_STRUCT;
typedef struct UITextShortcut
{
	const char* label;					AST(NAME("Label"))
	const char* beforeRegionText;		AST(NAME("BeforeRegionText"))
	const char* afterRegionText;		AST(NAME("AfterRegionText"))
} UITextShortcut;
extern ParseTable parse_UITextShortcut[];
#define TYPE_parse_UITextShortcut UITextShortcut

AUTO_STRUCT;
typedef struct UITextShortcutTab
{
	const char* label;					AST(NAME("Label"))
	UITextShortcut** shortcuts;			AST(NAME("Shortcut"))
} UITextShortcutTab;
extern ParseTable parse_UITextShortcutTab[];
#define TYPE_parse_UITextShortcutTab UITextShortcutTab

SA_RET_NN_VALID UITextArea *ui_TextAreaCreate(SA_PARAM_NN_STR const unsigned char *text);
SA_RET_NN_VALID UITextArea *ui_TextAreaCreateWithSMFView(SA_PARAM_NN_STR const unsigned char *text);
void ui_TextAreaInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UITextArea *textarea, SA_PARAM_NN_STR const unsigned char *text);
void ui_TextAreaFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UITextArea *textarea);

bool ui_TextAreaSetText(SA_PARAM_NN_VALID UITextArea *textarea, SA_PARAM_NN_STR const unsigned char *text);
void ui_TextAreaSetTextAndCallback(UITextArea *textarea, SA_PARAM_NN_STR const unsigned char *text);
SA_RET_NN_STR const unsigned char *ui_TextAreaGetText(UITextArea *textarea);
void ui_TextAreaSetChangedCallback(UITextArea *textarea, UIActivationFunc changedF, UserData changedData);
void ui_TextAreaSetFinishedCallback(UITextArea *textarea, UIActivationFunc finishedF, UserData finishedData);
void ui_TextAreaSetCollapse(SA_PARAM_NN_VALID UITextArea *textarea, bool collapse);
void ui_TextAreaSetCollapseHeight(SA_PARAM_NN_VALID UITextArea *textarea, int collapseHeight);
void ui_TextAreaSetWordWrap(SA_PARAM_NN_VALID UITextArea *textarea, bool enabled);
void ui_TextAreaSetSMFEdit(SA_PARAM_NN_VALID UITextArea *textarea, UITextShortcutTab** smfEditTabs);

void ui_TextAreaSetCursorPosition(SA_PARAM_NN_VALID UITextArea *textarea, U32 cursorPos);
U32 ui_TextAreaGetCursorPosition(SA_PARAM_NN_VALID UITextArea *textarea);
U32 ui_TextAreaInsertTextAt(SA_PARAM_NN_VALID UITextArea *textarea, U32 offset, SA_PARAM_NN_STR const unsigned char *text);
void ui_TextAreaDeleteTextAt(SA_PARAM_NN_VALID UITextArea *textarea, U32 offset, U32 length);
void ui_TextAreaDeleteSelection(SA_PARAM_NN_VALID UITextArea *textarea);

bool ui_TextAreaInput(SA_PARAM_NN_VALID UITextArea *textarea, SA_PARAM_NN_VALID KeyInput *input);
void ui_TextAreaTick(SA_PARAM_NN_VALID UITextArea *textarea, UI_PARENT_ARGS);
void ui_TextAreaDraw(SA_PARAM_NN_VALID UITextArea *textarea, UI_PARENT_ARGS);
void ui_TextAreaUnfocus(SA_PARAM_NN_VALID UITextArea *textarea, UIAnyWidget *focusitem);
void ui_TextAreaReflow(SA_PARAM_NN_VALID UITextArea *textarea, F32 width, F32 scale);
S32 ui_TextAreaGetLines(SA_PARAM_NN_VALID UITextArea *textarea);

S32 ui_TextAreaCursorGetLine(SA_PARAM_NN_VALID UITextArea *textarea);
S32 ui_TextAreaCursorGetLines(SA_PARAM_NN_VALID UITextArea *textarea);

void ui_TextAreaCursorUpLine(SA_PARAM_NN_VALID UITextArea *textarea);
void ui_TextAreaCursorDownLine(SA_PARAM_NN_VALID UITextArea *textarea);
void ui_TextAreaCursorStartLine(SA_PARAM_NN_VALID UITextArea *textarea);
void ui_TextAreaCursorEndLine(SA_PARAM_NN_VALID UITextArea *textarea);

void ui_TextAreaCut(SA_PARAM_NN_VALID UITextArea *textarea);
void ui_TextAreaCopy(SA_PARAM_NN_VALID UITextArea *textarea);
void ui_TextAreaPaste(SA_PARAM_NN_VALID UITextArea *textarea);
void ui_TextAreaSelectAll(SA_PARAM_NN_VALID UITextArea *textarea);

void ui_TextAreaSetSMFView(SA_PARAM_NN_VALID UITextArea *entry, SA_PARAM_OP_VALID UISMFView *smf);

#endif
