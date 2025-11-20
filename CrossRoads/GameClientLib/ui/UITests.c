GCC_SYSTEM
#include "Expression.h"
#include "cmdparse.h"
#include "EArray.h"
#include "crypt.h"
#include "TimedCallback.h"
#include "ControllerScriptingSupport.h"

#include "GfxTexAtlas.h"
#include "GfxSpriteText.h"

#include "rand.h"
#include "UILib.h"
#include "UIAuxDevice.h"
#include "UIAutoWidget.h"
#include "UIGraph.h"
#include "UIBoxSizer.h"
#include "UIGridSizer.h"
#include "UIGenWidget.h"
#include "UIWebView.h"
#include "GfxHeadshot.h"

extern ParseTable parse_Entity[];
#define TYPE_parse_Entity Entity

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define TEST_LOREM_IPSUM "Lorem ipsum dolor sit amet, consectetur adipisicing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum."
#define TEST_LONG_WORD "Lopado-te-macho-se-lacho-galeo-kranio-leipsano-drimhypo-trimmato-silphio-parao-melito-katakechymeno-kichlepikossypho-phatto-peristeralektryonop-tekephallio-kigklopeleiol-agoiosiraio-baphetraganop-terygon."
//#define TEST_UTF8_TEXT "\xE5\x90\xBE\xE8\xBC\xA9\xE3\x81\xAF\xE7\x8C\xAB\xE3\x81\xA7\xE3\x81\x82\xE3\x82\x8B" // "吾輩は猫である" This in a japanese string that none of our fonts can correctly display.
#define TEST_UTF8_TEXT "\xC4\xAD\x20\xC6\x9C\xC4\x81\xC6\xA7\x20\xC4\x80\x20\xC4\x86\xC4\x81\xC5\xA4" // "ĭ ƜāƧ Ā ĆāŤ" This is displayable in FreeSans, and still demonstrates UTF8 reasonably well.

#define TEST_TEXT TEST_LOREM_IPSUM "\n\n" TEST_LONG_WORD "\n\n"

AUTO_STRUCT;
typedef struct UITestSubStructure
{
	char *pchName;
	char *pchOtherName;
} UITestSubStructure;

AUTO_STRUCT;
typedef struct UITestStructure
{
	char *pchName;
	char *pchOtherName;
	S32 iStart;
	S32 iEnd;
	UITestSubStructure **eaAlternates;
} UITestStructure;

typedef struct UITestTreeStructure UITestTreeStructure;

AUTO_STRUCT;
typedef struct UITestTreeStructure
{
	char *name;
	UITestTreeStructure **children;
} UITestTreeStructure;

AUTO_ENUM;
typedef enum UITestEnum
{
	UITest_BelongingToTheEmperor = 1 << 0, ENAMES(BelongingToTheEmperor)
	UITest_EmbalmedOnes = 1 << 1, ENAMES(EmbalmedOnes)
	UITest_Tamed = 1 << 2, ENAMES(Tamed)
	UITest_SucklingPigs = 1 << 3, ENAMES(SucklingPigs)
	UITest_Mermaids = 1 << 4, ENAMES(Mermaids)
	UITest_FabulousOnes = 1 << 5, ENAMES(FabulousOnes)
	UITest_StrayDogs = 1 << 6, ENAMES(StrayDogs)
	UITest_IncludedInThisClassification = 1 << 7, ENAMES(IncludedInThisClassification)
	UITest_AgitatedLikeFools = 1 << 8, ENAMES(AgitatedLikeFools)
	UITest_InnumerableOnes = 1 << 9, ENAMES(InnumerableOnes)
	UITest_DrawnWithAFineCamelHairBrush = 1 << 10, ENAMES(DrawnWithAFineCamelHairBrush)
	UITest_Etc = 1 << 11, ENAMES(Etc EtCetera)
	UITest_JustBrokenTheJug = 1 << 12, ENAMES(JustBrokenTheJug)
	UITest_FromAfarResembleFlies = 1 << 13, ENAMES(FromAfarResembleFlies)
} UITestEnum;

#include "UITests_c_ast.c"

static UITestStructure s_aStructures[] = {
	{ "The Three August Ones and the Five Emperors", "san huang wu di", -5000, -2070 },
	{ "Xia", "xia", -2070, 1600 },
	{ "Shang", "shang", -1600, -1046 },
	{ "Western Zhou", "xi zhou", -1046, -771 },
	{ "Eastern Zhou", "dong zhou", -770, -221 },
	{ "Qin", "qin", -221, -206},
	{ "Western Han", "xi han", -206, 9},
	{ "Xin", "xin", 9, 25 },
	{ "Eastern Han", "dong han", 25, 220 },
	{ "Three Kingdoms", "san guo", 220, 265 },
	{ "Western Jin", "xi jin", 265, 317 },
	{ "Eastern Jin", "dong jin", 317, 420 },
	{ "Northern and Southern", "nan bei chao", 420, 581 },
	{ "Sui", "sui", 581, 618 },
	{ "Tang", "tang", 618, 907 },
	{ "Five Dynasties and Ten Kingdoms", "wu dai shi guo", 907, 960 },
	{ "Northern Song", "bei song", 960, 1127 },
	{ "Southern Song", "nan song", 1127, 1279 },
	{ "Liao", "liao", 916, 1125 },
	{ "Jin", "jin", 1115, 1234 },
	{ "Yuan", "yuan", 1271, 1368 },
	{ "Ming", "ming", 1368, 1644 },
	{ "Qing", "qing", 1644, 1911 },
};

static UITestTreeStructure *treeRoot;

static UITestStructure **s_eaStructures = NULL;

static const char **s_eachStrings = NULL;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillTestList");
void ui_TestGenListModel(ExprContext *pContext)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	if (pGen)
		ui_GenSetList(pGen, (void ***)&s_eaStructures, parse_UITestStructure);
}

AUTO_RUN;
void UITest_CreateTestData(void)
{
	S32 i;
	for (i = 0; i < ARRAY_SIZE_CHECKED(s_aStructures); i++)
	{
		S32 j;
		UITestStructure *pStruct = StructCreate(parse_UITestStructure);
		StructCopyAll(parse_UITestStructure, &s_aStructures[i], pStruct);
		for (j = 0; j < ((i + 3) % 5) + 1; j++)
		{
			UITestSubStructure *pSub = StructCreate(parse_UITestSubStructure);
			pSub->pchName = StructAllocString(s_aStructures[i].pchName + (j % 3));
			pSub->pchOtherName = StructAllocString(s_aStructures[i].pchOtherName + (j % 3));
			eaPush(&pStruct->eaAlternates, pSub);
		}
		eaPush(&s_eaStructures, pStruct);
		eaPush(&s_eachStrings, pStruct->pchName);
	}
}

void ButtonSetLabelText(UIButton *pButton, UILabel *pLabel)
{
	char ach[1000];
	sprintf(ach, "You clicked %p", pButton);
	ui_LabelSetText(pLabel, ach);
}

static void Buttons(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Buttons");
	UILabel *pLabel = ui_LabelCreate("Press a Button", 0, 0);
	UIButton *pButton = ui_ButtonCreate("Button #1", 0, 0, ButtonSetLabelText, pLabel);
	UIButton *pButtonAndImage = ui_ButtonCreateWithIcon("Button #2", UI_XBOX_AB, ButtonSetLabelText, pLabel);
	UIButton *pButtonAndImageCentered = ui_ButtonCreateWithIcon( "Button #4", UI_XBOX_AB, ButtonSetLabelText, pLabel);
	UIButton *pImage = ui_ButtonCreateImageOnly(UI_XBOX_BB, 0, 0, ButtonSetLabelText, pLabel);

	pButtonAndImageCentered->bCenterImageAndText = true;

	ui_WidgetSetDimensionsEx(UI_WIDGET(pButton), 1.f, UI_WIDGET(pButton)->height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pButtonAndImage), 1.f, UI_WIDGET(pButtonAndImage)->height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pImage), 1.f, UI_WIDGET(pImage)->height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pButtonAndImageCentered), 1.f, UI_WIDGET(pButtonAndImageCentered)->height, UIUnitPercentage, UIUnitFixed);

	ui_TabAddChild(pTab, pLabel);
	ui_TabAddChild(pTab, pButton);
	ui_TabAddChild(pTab, pButtonAndImage);
	ui_TabAddChild(pTab, pImage);
	ui_TabAddChild(pTab, pButtonAndImageCentered);

	ui_LayOutVertically(&pTab->eaChildren);
	ui_TabGroupAddTab(pTabs, pTab);
}

static void Labels(UITabGroup *pTabs)
{
	UITab* pTab = ui_TabCreate("Labels");
	UIPane* pPane;
	UILabel* pLabel;

	pPane = ui_PaneCreate( 0, 0, 1, 0.5, UIUnitPercentage, UIUnitPercentage, 0 );
	ui_TabAddChild( pTab, pPane );
	pLabel = ui_LabelCreate( "TopLeft", 0, 0 );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UITopLeft );
	ui_PaneAddChild( pPane, pLabel );
	pLabel = ui_LabelCreate( "Top", 0, 0 );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UITop );
	ui_PaneAddChild( pPane, pLabel );
	pLabel = ui_LabelCreate( "TopRight", 0, 0 );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UITopRight );
	ui_PaneAddChild( pPane, pLabel );
	pLabel = ui_LabelCreate( "Left", 0, 0 );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UILeft );
	ui_PaneAddChild( pPane, pLabel );
	pLabel = ui_LabelCreate( "Center", 0, 0 );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UINoDirection );
	ui_PaneAddChild( pPane, pLabel );
	pLabel = ui_LabelCreate( "Right", 0, 0 );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UIRight );
	ui_PaneAddChild( pPane, pLabel );
	pLabel = ui_LabelCreate( "BottomLeft", 0, 0 );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UIBottomLeft );
	ui_PaneAddChild( pPane, pLabel );
	pLabel = ui_LabelCreate( "Bottom", 0, 0 );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UIBottom );
	ui_PaneAddChild( pPane, pLabel );
	pLabel = ui_LabelCreate( "BottomRight", 0, 0 );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UIBottomRight );
	ui_PaneAddChild( pPane, pLabel );

	pPane = ui_PaneCreate( 0, 0, 1, 0.5, UIUnitPercentage, UIUnitPercentage, 0 );
	ui_WidgetSetPositionEx( UI_WIDGET( pPane ), 0, 0, 0, 0.5, UITopLeft );
	ui_TabAddChild( pTab, pPane );
	pLabel = ui_LabelCreate( "TopLeft", 0, 0 );
	pLabel->bRotateCCW = true;
	ui_LabelResize( pLabel );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UITopLeft );
	ui_PaneAddChild( pPane, pLabel );
	pLabel = ui_LabelCreate( "Top", 0, 0 );
	pLabel->bRotateCCW = true;
	ui_LabelResize( pLabel );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UITop );
	ui_PaneAddChild( pPane, pLabel );
	pLabel = ui_LabelCreate( "TopRight", 0, 0 );
	pLabel->bRotateCCW = true;
	ui_LabelResize( pLabel );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UITopRight );
	ui_PaneAddChild( pPane, pLabel );
	pLabel = ui_LabelCreate( "Left", 0, 0 );
	pLabel->bRotateCCW = true;
	ui_LabelResize( pLabel );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UILeft );
	ui_PaneAddChild( pPane, pLabel );
	pLabel = ui_LabelCreate( "Center", 0, 0 );
	pLabel->bRotateCCW = true;
	ui_LabelResize( pLabel );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UINoDirection );
	ui_PaneAddChild( pPane, pLabel );
	pLabel = ui_LabelCreate( "Right", 0, 0 );
	pLabel->bRotateCCW = true;
	ui_LabelResize( pLabel );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UIRight );
	ui_PaneAddChild( pPane, pLabel );
	pLabel = ui_LabelCreate( "BottomLeft", 0, 0 );
	pLabel->bRotateCCW = true;
	ui_LabelResize( pLabel );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UIBottomLeft );
	ui_PaneAddChild( pPane, pLabel );
	pLabel = ui_LabelCreate( "Bottom", 0, 0 );
	pLabel->bRotateCCW = true;
	ui_LabelResize( pLabel );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UIBottom );
	ui_PaneAddChild( pPane, pLabel );
	pLabel = ui_LabelCreate( "BottomRight", 0, 0 );
	pLabel->bRotateCCW = true;
	ui_LabelResize( pLabel );
	ui_WidgetSetPositionEx( UI_WIDGET( pLabel ), 0, 0, 0, 0, UIBottomRight );
	ui_PaneAddChild( pPane, pLabel );

	ui_TabGroupAddTab( pTabs, pTab );
}

static void ModalWindowCreate(UIButton *pButton, UIWindow *pWindow)
{
	ui_WindowShowEx(pWindow, true);
}

static void WindowShadeProxy(UIWindow *window, UserData dummy)
{
	window->shaded ^= true;
}

static void Windows(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Windows");
	UIWindow *inner = ui_WindowCreate("Inner Window", 100, 100, 50, 50);
	UIWindow *inner2 = ui_WindowCreate("Third Window", 100, 100, 150, 150);
	UIWindow *pModal = ui_WindowCreate("Modal Window", 200, 200, 200, 200);
	UIButton *pButton = ui_ButtonCreate("Open a Modal Window", UI_HSTEP, UI_HSTEP, ModalWindowCreate, pModal);
	UIButton *pInner2Button = ui_ButtonCreate("Do Nothing", UI_HSTEP, UI_HSTEP, NULL, NULL);
	UIButton *pInner2Button2 = ui_ButtonCreate("Do Nothing Again", UI_HSTEP, UI_DSTEP*3, NULL, NULL);
	UIWindow *pShading = ui_WindowCreate("Shade or Close", 50, 50, 100, 100);
	static UITitleButton s_ShadeButton = {"eui_button_minus", WindowShadeProxy, NULL};
	inner->widget.scale = 1.1;
	inner2->widget.scale = 0.6;
	ui_WindowSetModal(pModal, true);
	ui_WindowAddChild(inner2, pInner2Button);
	ui_WindowAddChild(inner2, pInner2Button2);
	ui_WidgetSetHeight(UI_WIDGET(inner2), ui_WidgetCalcHeight(UI_WIDGET(inner2)));
	ui_TabAddChild(pTab, inner);
	ui_TabAddChild(pTab, inner2);
	ui_TabAddChild(pTab, pButton);
	ui_TabAddChild(pTab, pShading);
	ui_WindowAddTitleButton(pShading, &s_ShadeButton);
	ui_TabGroupAddTab(pTabs, pTab);
}

static void TextEntries(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Text Entry");
	UITextEntry *pEntry = ui_TextEntryCreate("This is a text entry, with format strings: %s, %d, %%, %?", 0, 0);
	UITextEntry *pUniEntry = ui_TextEntryCreate(TEST_UTF8_TEXT, 0, 0);
	UITextEntry *pAsciiEntry = ui_TextEntryCreate("Try typing non-ASCII here", 0, 0);
	UITextEntry *pCBEntry = ui_TextEntryCreate("Combo box text entry", 0, 0);
	UITextEntry *pCompEntry = ui_TextEntryCreate("Auto-completing combo box text entry", 0, 0);
	UITextArea *pCollapsedArea = ui_TextAreaCreate(TEST_TEXT);
	UITextArea *pArea = ui_TextAreaCreate(TEST_TEXT TEST_TEXT TEST_TEXT);
	UISpinnerEntry *pIntSpinner = ui_SpinnerEntryCreate(-1000, 1000, 1, 200, false);
	UISpinnerEntry *pFloatSpinner = ui_SpinnerEntryCreate(-1, 1, 0, -0.3, true);
	UIMultiSpinnerEntry *pMultiSpinner = ui_MultiSpinnerEntryCreate(0, 100, 0.1, 0, 3, true);
	UITextureEntry *pTexEntry = ui_TextureEntryCreate(UI_XBOX_AB, NULL, false);
	static const char **eaModel = NULL;
	static UITextShortcutTab** eaShortcutTabs = NULL;
	F32 y = UI_HSTEP;

	if (!eaModel)
	{
		eaPush(&eaModel, "First String");
		eaPush(&eaModel, "Second String");
		eaPush(&eaModel, "Third String");
		eaPush(&eaModel, "Fourth String");
	}
	if (!eaShortcutTabs)
	{
		UITextShortcutTab* tab1 = StructCreate( parse_UITextShortcutTab );
		UITextShortcutTab* tab2 = StructCreate( parse_UITextShortcutTab );
		UITextShortcut* shortcut;

		tab1->label = "Tab1";
		eaPush( &eaShortcutTabs, tab1 );
		tab2->label = "Tab2";
		eaPush( &eaShortcutTabs, tab2 );

		shortcut = StructCreate( parse_UITextShortcut );
		shortcut->label = "One";
		shortcut->beforeRegionText = "<One>";
		shortcut->afterRegionText = "</One>";
		eaPush( &tab1->shortcuts, shortcut );

		shortcut = StructCreate( parse_UITextShortcut );
		shortcut->label = "Two";
		shortcut->beforeRegionText = "<Two>";
		shortcut->afterRegionText = "</Two>";
		eaPush( &tab1->shortcuts, shortcut );

		shortcut = StructCreate( parse_UITextShortcut );
		shortcut->label = "One";
		shortcut->beforeRegionText = "<One />";
		eaPush( &tab2->shortcuts, shortcut );

		shortcut = StructCreate( parse_UITextShortcut );
		shortcut->label = "Two";
		shortcut->beforeRegionText = "<Two />";
		eaPush( &tab2->shortcuts, shortcut );
	}

	ui_TextAreaSetCollapse(pCollapsedArea, true);
	ui_TextAreaSetSMFEdit(pCollapsedArea, eaShortcutTabs);
	ui_TextEntrySetComboBox(pCBEntry, ui_ComboBoxCreate(0, 0, 0, NULL, &eaModel, NULL));
	ui_TextEntrySetComboBox(pCompEntry, ui_ComboBoxCreate(0, 0, 0, NULL, &eaModel, NULL));
	pCompEntry->autoComplete = true;

	ui_TextEntrySetSelectOnFocus(pAsciiEntry, true);

	ui_WidgetSetDimensionsEx(UI_WIDGET(pEntry), 1.f, UI_WIDGET(pEntry)->height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pUniEntry), 1.f, UI_WIDGET(pUniEntry)->height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pAsciiEntry), 1.f, UI_WIDGET(pAsciiEntry)->height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pCBEntry), 1.f, UI_WIDGET(pCBEntry)->height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pCompEntry), 1.f, UI_WIDGET(pCompEntry)->height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pTexEntry), 1.f, UI_WIDGET(pTexEntry)->height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pMultiSpinner), 1.f, UI_WIDGET(pMultiSpinner)->height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pCollapsedArea), 1.f, 200, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pArea), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);

	ui_TextEntrySetValidateCallback(pAsciiEntry, ui_EditableForceAscii, (void *)1);

	ui_WidgetSetPosition(UI_WIDGET(pEntry), 0, y);
	y += UI_WIDGET(pEntry)->height + UI_HSTEP;

	ui_WidgetSetPosition(UI_WIDGET(pUniEntry), 0, y);
	y += UI_WIDGET(pUniEntry)->height + UI_HSTEP;

	ui_WidgetSetPosition(UI_WIDGET(pAsciiEntry), 0, y);
	y += UI_WIDGET(pAsciiEntry)->height + UI_HSTEP;

	ui_WidgetSetPosition(UI_WIDGET(pCBEntry), 0, y);
	y += UI_WIDGET(pCBEntry)->height + UI_HSTEP;

	ui_WidgetSetPosition(UI_WIDGET(pCompEntry), 0, y);
	y += UI_WIDGET(pCompEntry)->height + UI_HSTEP;

	ui_WidgetSetPosition(UI_WIDGET(pTexEntry), 0, y);
	y += UI_WIDGET(pTexEntry)->height + UI_HSTEP;

	ui_WidgetSetPosition(UI_WIDGET(pFloatSpinner), 0, y);
	y += UI_WIDGET(pFloatSpinner)->height + UI_HSTEP;

	ui_WidgetSetPosition(UI_WIDGET(pMultiSpinner), 0, y);
	y += UI_WIDGET(pMultiSpinner)->height + UI_HSTEP;

	ui_WidgetSetPosition(UI_WIDGET(pIntSpinner), 0, y);
	y += UI_WIDGET(pIntSpinner)->height + UI_HSTEP;

	ui_WidgetSetPosition(UI_WIDGET(pCollapsedArea), 0, y);
	// Add the "small" height, since this collapses, not the full height.
	y += UI_WIDGET(pCompEntry)->height + UI_HSTEP;

	UI_WIDGET(pArea)->topPad = y;

	ui_TabAddChild(pTab, pEntry);
	ui_TabAddChild(pTab, pUniEntry);
	ui_TabAddChild(pTab, pAsciiEntry);
	ui_TabAddChild(pTab, pCBEntry);
	ui_TabAddChild(pTab, pCompEntry);
	ui_TabAddChild(pTab, pTexEntry);
	ui_TabAddChild(pTab, pFloatSpinner);
	ui_TabAddChild(pTab, pMultiSpinner);
	ui_TabAddChild(pTab, pIntSpinner);
	ui_TabAddChild(pTab, pCollapsedArea);
	ui_TabAddChild(pTab, pArea);
	ui_TabGroupAddTab(pTabs, pTab);
}

static void ScrollArea(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Scroll Area");
	UIScrollArea *pSA = ui_ScrollAreaCreate(0, 0, 0, 0, 1000, 1000, true, true);
	
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSA), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_ScrollAreaSetDraggable(pSA, true);
	ui_ScrollAreaAddChild(pSA, UI_WIDGET(ui_ButtonCreate("Button", 30, 30, NULL, NULL)));
	ui_ScrollAreaAddChild(pSA, UI_WIDGET(ui_ButtonCreate("Button", 100, 100, NULL, NULL)));
	ui_ScrollAreaAddChild(pSA, UI_WIDGET(ui_ButtonCreate("Button", 200, 80, NULL, NULL)));
	ui_ScrollAreaAddChild(pSA, UI_WIDGET(ui_ButtonCreate("Button", 300, 500, NULL, NULL)));
	ui_ScrollAreaAddChild(pSA, UI_WIDGET(ui_ButtonCreate("Button", 500, 300, NULL, NULL)));
	ui_ScrollAreaAddChild(pSA, UI_WIDGET(ui_ButtonCreate("Button", 700, 900, NULL, NULL)));

	ui_TabAddChild(pTab, pSA);
	ui_TabGroupAddTab(pTabs, pTab);
}

static void ListLabelUpdateHover(UIList *pList, UIListColumn *pCol, S32 row, UILabel *pLabel)
{
	char ach[1000];
	if (pCol)
	{
		sprintf(ach, "You're hovering over %s row %d.", ui_ListColumnGetTitle(pCol), row);
	}
	else // hovering over nothing
	{
		strcpy(ach, "");
	}
	ui_LabelSetText(pLabel, ach);
}

static void ListDragFunc(UIList *list, int from, int to, UITestStructure ***eaStructures)
{
	eaMove(eaStructures, to, from);
	ui_ListSetSelectedRowAndCallback(list, to);
}

static void ListLabelUpdateSelected(UIList *pList, UILabel *pLabel)
{
	char *pchMessage = NULL;
	ParseTable *pTable = NULL;
	const UIListSelectionObject **eaSelected = NULL;

	estrStackCreate(&pchMessage);

	ui_ListGetSelectedCells(pList, &eaSelected);

	if (eaSize(&eaSelected) == 0)
		estrPrintf(&pchMessage, "You don't have any rows selected.");
	else
	{
		S32 *eaiPath = NULL;
		ui_ListGetSelectedPath(pList, &eaiPath);
		estrPrintf(&pchMessage, "You've selected %d cells.", eaSize(&eaSelected));

		if (eaiSize(&eaiPath))
		{
			S32 i;
			estrConcatf(&pchMessage, " One of them is (");
			for (i = 0; i < eaiSize(&eaiPath); i++)
				estrConcatf(&pchMessage, "%d,", eaiPath[i]);
			estrConcatf(&pchMessage, ")");
		}
	}
	ui_LabelSetText(pLabel, pchMessage);
	eaDestroy(&eaSelected);
	estrDestroy(&pchMessage);
}

static void NameUnfocused(UITextEntry *pEntry, UITestStructure *pStruct)
{
	StructFreeString(pStruct->pchName);
	pStruct->pchName = StructAllocString(ui_TextEntryGetText(pEntry));
	ui_WidgetQueueFree(UI_WIDGET(pEntry));
}

static void NameActivated(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	UIList *pTopList = ui_ListGetParent(pList);
	if (iColumn == 0)
	{
		void *pStruct = eaGet(pList->peaModel, iRow);
		if (pStruct)
		{
			char *pchValue = NULL;
			S32 iParseIndex = ui_ListColumnGetParseTableIndex(pList, pList->eaColumns[iColumn]);
			if (TokenWriteText(pList->pTable, iParseIndex, pStruct, &pchValue, false))
			{
				UITextEntry *pEntry = ui_TextEntryCreate(pchValue, 0, 0);
				ui_WidgetSetCBox(UI_WIDGET(pEntry), pBox);
				ui_WidgetAddChild(UI_WIDGET(pTopList), UI_WIDGET(pEntry));
				ui_WidgetSetUnfocusCallback(UI_WIDGET(pEntry), NameUnfocused, pStruct);
				ui_SetFocus(pEntry);
			}
		}
	}
}

// static void CellHovered(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
// {
// 	printf("Got a click at %g %g (Box: %g %g)\n", (double)fMouseX, (double)fMouseY, (double)CBoxWidth(pBox), (double)CBoxHeight(pBox));
// }

static void List(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("List");
	UIList *pList = ui_ListCreate(parse_UITestStructure, &s_eaStructures, gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP);
	UIList *pSubList = ui_ListCreate(NULL, NULL, gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP);
	UIList *pSubList2 = ui_ListCreate(NULL, NULL, gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP);
	UIListColumn *pName = ui_ListColumnCreate(UIListPTName, "Name", (intptr_t)"Name", NULL);
	UIListColumn *pStart = ui_ListColumnCreate(UIListPTName, "From", (intptr_t)"Start", NULL);
	UIListColumn *pEnd = ui_ListColumnCreate(UIListPTName, "To", (intptr_t)"End", NULL);

	UIListColumn *pSubColumn1 = ui_ListColumnCreate(UIListPTName, "Name", (intptr_t)"Name", NULL);
	UIListColumn *pSubColumn2 = ui_ListColumnCreate(UIListPTName, "Other Name", (intptr_t)"OtherName", NULL);

	UIListColumn *pSubColumn21 = ui_ListColumnCreate(UIListPTName, "Name", (intptr_t)"Name", NULL);
	UIListColumn *pSubColumn22 = ui_ListColumnCreate(UIListPTName, "Other Name", (intptr_t)"OtherName", NULL);

	UILabel *pHover = ui_LabelCreate("", UI_HSTEP, UI_HSTEP);
	UILabel *pSelected = ui_LabelCreate("Select some rows.", UI_HSTEP, UI_HSTEP + UI_WIDGET(pHover)->height);
	UIRebuildableTree *pTree = ui_RebuildableTreeCreate();

	ui_ListColumnSetClickedCallback(pName, ui_ListColumnSelectCallback, pList);

	ui_ListAppendColumn(pSubList, pSubColumn1);
	ui_ListAppendColumn(pSubList, pSubColumn2);
	ui_ListAppendColumn(pSubList2, pSubColumn22);
	ui_ListAppendColumn(pSubList2, pSubColumn21);
	ui_ListColumnSetSortable(pSubColumn1, true);
	ui_ListColumnSetSortable(pSubColumn2, true);
	ui_ListColumnSetWidth(pSubColumn1, true, 1);
	ui_ListColumnSetWidth(pSubColumn2, true, 1);
	ui_ListAddSubList(pList, pSubList, 32.f, "Alternates");
	ui_ListAddSubList(pList, pSubList2, 32.f, "Alternates");
	ui_ListSetMultiselect(pSubList, true);

	ui_ListSetCellActivatedCallback(pList, NameActivated, NULL);
	ui_ListSetCellActivatedCallback(pSubList, NameActivated, NULL);
// 	ui_ListSetCellHoverCallback(pList, CellHovered, NULL);
// 	ui_ListSetCellHoverCallback(pSubList, CellHovered, NULL);

	ui_ListSetColumnsSelectable(pList, true);

	UI_WIDGET(pList)->scale = 0.8;
	ui_WidgetSetPaddingEx(UI_WIDGET(pList), UI_HSTEP, UI_HSTEP,
		UI_HSTEP + UI_WIDGET(pHover)->height + UI_WIDGET(pSelected)->height, UI_HSTEP);
	ui_ListColumnSetWidth(pName, true, 1);
	ui_ListColumnSetSortable(pStart, true);
	ui_ListColumnSetSortable(pEnd, true);
// 	ui_ListSetRowsDraggable(pList, true);
// 	ui_ListSetDragCallback(pList, ListDragFunc, &s_eaStructures);
	ui_ListColumnSetAlignment(pStart, UIRight, UIRight);
	ui_ListColumnSetAlignment(pEnd, UIRight, UIRight);
	ui_ListAppendColumn(pList, pName);
	ui_ListAppendColumn(pList, pStart);
	ui_ListAppendColumn(pList, pEnd);
	ui_ListSetHoverCallback(pList, ListLabelUpdateHover, pHover);
	ui_ListSetSelectedCallback(pList, ListLabelUpdateSelected, pSelected);
	ui_ListSetMultiselect(pList, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pList), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	UI_WIDGET(pList)->sb->scrollX = true;
	ui_TabAddChild(pTab, pList);
	ui_TabAddChild(pTab, pHover);
	ui_TabAddChild(pTab, pSelected);
	ui_TabGroupAddTab(pTabs, pTab);
}

static void CoordinateDoubleClickCallback(UICoordinate *pCoord, F32 fX, F32 fY, UILabel *pLabel)
{
	char ach[1000];
	sprintf(ach, "Double Clicked on %f, %f.", fX, fY);
	ui_LabelSetText(pLabel, ach);
}

static void Coordinates(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Coordinates");
	UICoordinate *pCoords = ui_CoordinateCreate();
	UILabel *pDoubleClick = ui_LabelCreate("", UI_HSTEP, UI_HSTEP);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pCoords), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_CoordinateSetDoubleClickCallback(pCoords, CoordinateDoubleClickCallback, pDoubleClick);
	ui_CoordinateDrawCenter(pCoords, true);
	ui_TabAddChild(pTab, pCoords);
	ui_TabAddChild(pTab, pDoubleClick);
	ui_TabGroupAddTab(pTabs, pTab);
}

static void ComboBoxMakeSameDate(UIComboBox *pBox, UIComboBox *pOtherBox)
{
	ui_ComboBoxSetSelectedObjectAndCallback(pOtherBox, ui_ComboBoxGetSelectedObject(pBox));
}

static void ComboBoxCheckSameDate(UIComboBox *pBox, UIComboBox *pOtherBox)
{
	bool bChanged = ui_ComboBoxGetSelectedObject(pBox) != ui_ComboBoxGetSelectedObject(pOtherBox);
	if (bChanged)
	{
		UI_WIDGET(pBox)->state |= kWidgetModifier_Changed;
		UI_WIDGET(pOtherBox)->state |= kWidgetModifier_Changed;
	}
	else
	{
		UI_WIDGET(pBox)->state &= ~kWidgetModifier_Changed;
		UI_WIDGET(pOtherBox)->state &= ~kWidgetModifier_Changed;
	}
}

static void ComboBox(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Combo Box");
	UIComboBox *pNameBox = ui_ComboBoxCreate(0, UI_HSTEP, 0, parse_UITestStructure, &s_eaStructures, "Name");
	UIComboBox *pStartBox = ui_ComboBoxCreate(0, UI_HSTEP, 0, parse_UITestStructure, &s_eaStructures, "Start");
	UIComboBox *pEnumBox = ui_ComboBoxCreate(0, 0, 0, NULL, NULL, NULL);
	UIComboBox *pMultiBox = ui_ComboBoxCreate(0, 0, 0, parse_UITestStructure, &s_eaStructures, "Name");
	UIComboBox *pMultiEnumBox = ui_ComboBoxCreate(0, 0, 0, NULL, NULL, NULL);
	UIFlagComboBox *pFlagBox = ui_FlagComboBoxCreate(UITestEnumEnum);
	
	ui_WidgetSetDimensionsEx(UI_WIDGET(pNameBox), 0.8f, UI_WIDGET(pNameBox)->height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pStartBox), 0.2f, UI_WIDGET(pNameBox)->height, UIUnitPercentage, UIUnitFixed);
	UI_WIDGET(pStartBox)->xPOffset = 0.8f;
	UI_WIDGET(pNameBox)->rightPad = UI_HSTEP;
	UI_WIDGET(pStartBox)->leftPad = UI_HSTEP;
	ui_ComboBoxSetSelectedCallback(pNameBox, ComboBoxMakeSameDate, pStartBox);
	ui_ComboBoxSetSelectedCallback(pStartBox, ComboBoxCheckSameDate, pNameBox);

	ui_ComboBoxSetEnum(pEnumBox, UITestEnumEnum, NULL, NULL);
	ui_ComboBoxSetEnum(pMultiEnumBox, UITestEnumEnum, NULL, NULL);
	ui_ComboBoxSetMultiSelect(pMultiBox, true);
	ui_ComboBoxSetMultiSelect(pMultiEnumBox, true);

	ui_WidgetSetDimensionsEx(UI_WIDGET(pEnumBox), 1.f, UI_WIDGET(pEnumBox)->height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pFlagBox), 1.f, UI_WIDGET(pFlagBox)->height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pMultiBox), 1.f, UI_WIDGET(pMultiBox)->height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pMultiEnumBox), 1.f, UI_WIDGET(pMultiEnumBox)->height, UIUnitPercentage, UIUnitFixed);
	UI_WIDGET(pEnumBox)->y = UI_WIDGET(pNameBox)->y + UI_WIDGET(pNameBox)->height;
	UI_WIDGET(pFlagBox)->y = UI_WIDGET(pEnumBox)->y + UI_WIDGET(pEnumBox)->height;
	UI_WIDGET(pMultiBox)->y = UI_WIDGET(pFlagBox)->y + UI_WIDGET(pFlagBox)->height;
	UI_WIDGET(pMultiEnumBox)->y = UI_WIDGET(pMultiBox)->y + UI_WIDGET(pMultiBox)->height;

	ui_ComboBoxSetSelectedEnum(pEnumBox, UITest_Etc);

	ui_TabAddChild(pTab, pNameBox);
	ui_TabAddChild(pTab, pStartBox);
	ui_TabAddChild(pTab, pEnumBox);
	ui_TabAddChild(pTab, pFlagBox);
	ui_TabAddChild(pTab, pMultiBox);
	ui_TabAddChild(pTab, pMultiEnumBox);
	ui_TabGroupAddTab(pTabs, pTab);
}

static void CheckBoxSetNot(UICheckButton *pCheck1, UICheckButton *pCheck2)
{
	ui_CheckButtonSetState(pCheck2, !ui_CheckButtonGetState(pCheck1));
}

static void CheckBox(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Check Boxes");
	UICheckButton *pCheck1 = ui_CheckButtonCreate(0, 0, "Button #1, defaults to on.", true);
	UICheckButton *pCheck2 = ui_CheckButtonCreate(0, 0, "Button #2, defaults to off.", false);
	UICheckButton *pCheck3 = ui_CheckButtonCreate(0, 0,	"Button #3, set to not button #2.", !ui_CheckButtonGetState(pCheck2));
	UISeparator *pSep = ui_SeparatorCreate(UIHorizontal);
	UIRadioButtonGroup *pGroup = ui_RadioButtonGroupCreate();
	UIRadioButton *pButton1 = ui_RadioButtonCreate(0, 0, "Radio button #1.1", pGroup);
	UIRadioButton *pButton2 = ui_RadioButtonCreate(0, 0, "Radio button #1.2", pGroup);
	UIRadioButton *pButton3 = ui_RadioButtonCreate(0, 0, "Radio button #1.3", pGroup);
	UIRadioButton *pButton4 = ui_RadioButtonCreate(0, 0, "Radio button #1.4", pGroup);
	UISeparator *pSep2 = ui_SeparatorCreate(UIHorizontal);
	UIRadioButtonGroup *pGroup2 = ui_RadioButtonGroupCreate();
	UIRadioButton *pButton21 = ui_RadioButtonCreate(0, 0, "Radio button #2.1", pGroup2);
	UIRadioButton *pButton22 = ui_RadioButtonCreate(0, 0, "Radio button #2.2", pGroup2);
	UIRadioButton *pButton23 = ui_RadioButtonCreate(0, 0, "Radio button #2.3", pGroup2);
	UIRadioButton *pButton24 = ui_RadioButtonCreate(0, 0, "Radio button #2.4", pGroup2);
	void *apWidgets[] = {pCheck1, pCheck2, pCheck3, pSep, pButton1, pButton2,
		pButton3, pButton4, pSep2, pButton21, pButton22, pButton23, pButton24};
	S32 i;

	ui_CheckButtonSetToggledCallback(pCheck3, CheckBoxSetNot, pCheck2);
	ui_CheckButtonSetToggledCallback(pCheck2, CheckBoxSetNot, pCheck3);

	for (i = 0; i < ARRAY_SIZE_CHECKED(apWidgets); i++)
		ui_TabAddChild(pTab, apWidgets[i]);
	ui_LayOutVertically(&pTab->eaChildren);
	ui_TabGroupAddTab(pTabs, pTab);
}

static void AutoWidget(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Auto Widget");
	UIRebuildableTree *pTree = ui_RebuildableTreeCreate();
	S32 i;

	ui_RebuildableTreeInit(pTree, &pTab->eaChildren, 0, UI_HSTEP, UIRTOptions_YScroll);

	ui_AutoWidgetAddButton(pTree->root, "Do Nothing", NULL, NULL, true, "Really, this does nothing.", NULL);
	for (i = 0; i < eaSize(&s_eaStructures); i++)
	{
		UIRTNode *pNode = ui_RebuildableTreeAddGroup(pTree->root, s_eaStructures[i]->pchOtherName, s_eaStructures[i]->pchOtherName, false, NULL);
		ui_AutoWidgetAdd(pNode, parse_UITestStructure, "Name", s_eaStructures[i], true, NULL, NULL, NULL, NULL);
		ui_AutoWidgetAdd(pNode, parse_UITestStructure, "Start", s_eaStructures[i], false, NULL, NULL, NULL, NULL);
		ui_AutoWidgetAdd(pNode, parse_UITestStructure, "End", s_eaStructures[i], false, NULL, NULL, NULL, NULL);
	}

	ui_RebuildableTreeDoneBuilding(pTree);
	ui_TabGroupAddTab(pTabs, pTab);
}

static void SetLabelBySlider(UISlider *pSlider, bool bFinished, UILabel *pLabel)
{
	char pch[1024];
	F64 val = ui_SliderGetValue(pSlider);
	
	if ((S32)val == val)
		sprintf(pch, "%d", (S32)val);
	else
		sprintf(pch, "%0.2f", (F32)val);
	ui_LabelSetText(pLabel, pch);
}

static void SetUpSlider(UISlider *pSlider, UITab *pTab, F32 *pfY)
{
	UILabel *pLabel = ui_LabelCreate("", 0, *pfY);
	UI_WIDGET(pSlider)->y = *pfY;
	ui_WidgetSetDimensionsEx(UI_WIDGET(pLabel), 0.2, UI_WIDGET(pLabel)->height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSlider), 0.8, UI_WIDGET(pSlider)->height, UIUnitPercentage, UIUnitFixed);
	UI_WIDGET(pSlider)->xPOffset = 0.2;
	ui_TabAddChild(pTab, pLabel);
	ui_TabAddChild(pTab, pSlider);
	ui_SliderSetChangedCallback(pSlider, SetLabelBySlider, pLabel);
	ui_SliderSetValueAndCallback(pSlider, ui_SliderGetValue(pSlider));
	*pfY += UI_WIDGET(pSlider)->height;
}

static void Sliders(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Sliders");
	UILabel *pLabel = NULL;
	UISlider *pSlider = NULL;
	UISeparator *pSep = NULL;
	F64 mSep = 0;
	F32 fY = UI_HSTEP;

	pSlider = ui_FloatSliderCreate(0, 0, 0, -1, 1, 0);
	ui_SliderSetPolicy(pSlider, UISliderDiscrete);
	SetUpSlider(pSlider, pTab, &fY);

	pSlider = ui_FloatSliderCreate(0, 0, 0, -1, 1, 0);
	ui_SliderSetPolicy(pSlider, UISliderContinuous);
	SetUpSlider(pSlider, pTab, &fY);

	pSlider = ui_FloatSliderCreate(0, 0, 0, -1, 1, 0);
	ui_SliderSetPolicy(pSlider, UISliderContinuous);
	pSlider->step = 0.25;
	SetUpSlider(pSlider, pTab, &fY);

	pSlider = ui_IntSliderCreate(0, 0, 0, -20, 20, 0);
	ui_SliderSetPolicy(pSlider, UISliderDiscrete);
	SetUpSlider(pSlider, pTab, &fY);

	pSlider = ui_IntSliderCreate(0, 0, 0, -20, 20, 0);
	ui_SliderSetPolicy(pSlider, UISliderContinuous);
	SetUpSlider(pSlider, pTab, &fY);

	pSlider = ui_IntSliderCreate(0, 0, 0, -20, 20, 0);
	ui_SliderSetPolicy(pSlider, UISliderContinuous);
	pSlider->step = 4;
	SetUpSlider(pSlider, pTab, &fY);

	pSep = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSep), 0, fY);
	fY += UI_WIDGET(pSep)->height;
	ui_TabAddChild(pTab, pSep);

	pSlider = ui_FloatSliderCreate(0, 0, 0, -1, 1, 0);
	ui_SliderSetPolicy(pSlider, UISliderDiscrete);
	ui_SliderSetCount(pSlider, 3, mSep);
	ui_FloatSliderSetValueAndCallbackEx(pSlider, 0, -0.5, false);
	ui_FloatSliderSetValueAndCallbackEx(pSlider, 1, 0, false);
	ui_FloatSliderSetValueAndCallbackEx(pSlider, 2, 0.5, false);
	SetUpSlider(pSlider, pTab, &fY);

	pSlider = ui_FloatSliderCreate(0, 0, 0, -1, 1, 0);
	ui_SliderSetPolicy(pSlider, UISliderContinuous);
	ui_SliderSetCount(pSlider, 3, mSep);
	ui_FloatSliderSetValueAndCallbackEx(pSlider, 0, -0.5, false);
	ui_FloatSliderSetValueAndCallbackEx(pSlider, 1, 0, false);
	ui_FloatSliderSetValueAndCallbackEx(pSlider, 2, 0.5, false);
	SetUpSlider(pSlider, pTab, &fY);

	pSlider = ui_FloatSliderCreate(0, 0, 0, -1, 1, 0);
	ui_SliderSetPolicy(pSlider, UISliderContinuous);
	pSlider->step = 0.25;
	ui_SliderSetCount(pSlider, 3, mSep);
	ui_FloatSliderSetValueAndCallbackEx(pSlider, 0, -0.5, false);
	ui_FloatSliderSetValueAndCallbackEx(pSlider, 1, 0, false);
	ui_FloatSliderSetValueAndCallbackEx(pSlider, 2, 0.5, false);
	SetUpSlider(pSlider, pTab, &fY);

	pSlider = ui_IntSliderCreate(0, 0, 0, -20, 20, 0);
	ui_SliderSetPolicy(pSlider, UISliderDiscrete);
	ui_SliderSetCount(pSlider, 3, mSep);
	ui_IntSliderSetValueAndCallbackEx(pSlider, 0, -10, false);
	ui_IntSliderSetValueAndCallbackEx(pSlider, 1, 0, false);
	ui_IntSliderSetValueAndCallbackEx(pSlider, 2, 10, false);
	SetUpSlider(pSlider, pTab, &fY);

	pSlider = ui_IntSliderCreate(0, 0, 0, -20, 20, 0);
	ui_SliderSetPolicy(pSlider, UISliderContinuous);
	ui_SliderSetCount(pSlider, 3, mSep);
	ui_IntSliderSetValueAndCallbackEx(pSlider, 0, -10, false);
	ui_IntSliderSetValueAndCallbackEx(pSlider, 1, 0, false);
	ui_IntSliderSetValueAndCallbackEx(pSlider, 2, 10, false);
	SetUpSlider(pSlider, pTab, &fY);

	pSlider = ui_IntSliderCreate(0, 0, 0, -20, 20, 0);
	ui_SliderSetPolicy(pSlider, UISliderContinuous);
	pSlider->step = 4;
	ui_SliderSetCount(pSlider, 3, mSep);
	ui_IntSliderSetValueAndCallbackEx(pSlider, 0, -10, false);
	ui_IntSliderSetValueAndCallbackEx(pSlider, 1, 0, false);
	ui_IntSliderSetValueAndCallbackEx(pSlider, 2, 10, false);
	SetUpSlider(pSlider, pTab, &fY);

	ui_TabGroupAddTab(pTabs, pTab);
}

static void MenuSetLabel(UIMenuItem *pItem, UILabel *pLabel)
{
	ui_LabelSetText(pLabel, ui_MenuItemGetText(pItem));
}

static void MenuPopupMenu(UIPane *pPane, UIMenu *pMenu)
{
	ui_MenuPopupAtCursor(pMenu);
}

static void Menus(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Menus");
	UIPane *pPane = ui_PaneCreate(0, 0, 1.f, 1.f, UIUnitPercentage, UIUnitPercentage, false);
	UIMenuBar *pBar = ui_MenuBarCreate(NULL);
	UIMenu *pMenu = ui_MenuCreate("Normal Menus");
	UIMenu *pSubMenu = NULL;
	UILabel *pLabel = ui_LabelCreate("Select a menu item, or right-click for a context menu.", 0, 0);
	S32 i;

	ui_WidgetSetWidthEx(UI_WIDGET(pBar), 1.0f, UIUnitPercentage);

	ui_MenuAppendItems(pMenu,
		ui_MenuItemCreate("Menu #1.1", UIMenuCallback, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("Menu #1.2", UIMenuCallback, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("Menu #1.3", UIMenuCallback, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
		ui_MenuItemCreate("Menu #1.4", UIMenuCallback, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("Menu #1.5", UIMenuCallback, MenuSetLabel, pLabel, NULL),
		NULL);
	ui_MenuBarAppendMenu(pBar, pMenu);

	pMenu = ui_MenuCreate("Checked Items");
	ui_MenuAppendItems(pMenu,
		ui_MenuItemCreate("Menu #2.1", UIMenuCheckButton, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
		ui_MenuItemCreate("Menu #2.2", UIMenuCheckButton, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("Menu #2.3", UIMenuCheckButton, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("Menu #2.4", UIMenuCheckButton, MenuSetLabel, pLabel, NULL),
		NULL);
	ui_MenuAppendItem(pMenu, ui_MenuItemCreate("Menu #2.1 (Again)",
		UIMenuCheckRefButton, MenuSetLabel, pLabel, &pMenu->items[0]->data.state));
	ui_MenuBarAppendMenu(pBar, pMenu);

	pMenu = ui_MenuCreate("Submenus");
	ui_MenuAppendItem(pMenu, ui_MenuItemCreate("Normal Item", UIMenuCallback, MenuSetLabel, pLabel, NULL));
	pSubMenu = ui_MenuCreate("Submenu #1");
	ui_MenuAppendItems(pSubMenu,
		ui_MenuItemCreate("Subitem #1.1", UIMenuCheckButton, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("Subitem #1.2", UIMenuCheckButton, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("Subitem #1.3", UIMenuCheckButton, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("Subitem #1.4", UIMenuCheckButton, MenuSetLabel, pLabel, NULL),
		NULL);
	ui_MenuAppendItem(pMenu, ui_MenuItemCreate("Submenu #1", UIMenuSubmenu, MenuSetLabel, pLabel, pSubMenu));
	pSubMenu = ui_MenuCreate("Submenu #2");
	ui_MenuAppendItems(pSubMenu,
		ui_MenuItemCreate("Subitem #2.1", UIMenuCheckButton, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("Subitem #2.2", UIMenuCheckButton, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("Subitem #2.3", UIMenuCheckButton, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("Subitem #2.4", UIMenuCheckButton, MenuSetLabel, pLabel, NULL),
		NULL);
	ui_MenuAppendItem(pMenu, ui_MenuItemCreate("Submenu #2", UIMenuSubmenu, MenuSetLabel, pLabel, pSubMenu));
	ui_MenuBarAppendMenu(pBar, pMenu);

	pMenu = ui_MenuCreate("A Long Menu");
	for (i = 0; i < 100; i++)
	{
		char ach[1000];
		sprintf(ach, "Long Item %d", i);
		ui_MenuAppendItem(pMenu, ui_MenuItemCreate(ach, UIMenuCallback, MenuSetLabel, pLabel, NULL));
	}
	ui_MenuBarAppendMenu(pBar, pMenu);

	pMenu = ui_MenuCreate("Context Menu");
	ui_MenuAppendItems(pMenu,
		ui_MenuItemCreate("Context #1", UIMenuCallback, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("Context #2", UIMenuCallback, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("Context #3", UIMenuCheckButton, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
		ui_MenuItemCreate("Context #4", UIMenuCallback, MenuSetLabel, pLabel, NULL),
		ui_MenuItemCreate("Context #5", UIMenuCallback, MenuSetLabel, pLabel, NULL),
		NULL);
	ui_WidgetSetContextCallback(UI_WIDGET(pPane), MenuPopupMenu, pMenu);

	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pBar));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pLabel));
	ui_LayOutVertically(&pPane->widget.children);
	ui_TabAddChild(pTab, pPane);
	ui_TabGroupAddTab(pTabs, pTab);
}

static void TestSMF(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("SMF");
	UISMFView *pSMF = ui_SMFViewCreate(0, 0, 0, 0);
	UITextEntry *pText = ui_TextEntryCreateWithSMFView("<b>A bunch of text</b>", 0, 0);
	UITextArea *pArea = ui_TextAreaCreateWithSMFView("<p>Test<br><b>Test2</b><br><i>Test3</i></p><span align=left>this paragraph is left aligned</span>");
	
	ui_SMFViewSetText(pSMF, "<p>"TEST_LOREM_IPSUM"<br><br><b>BOLD!</b><br><i>ITALICS!</i><br><font shadow=1 scale=2>Big and shadow!</font></p>"\
		"<span align=left>this paragraph is left aligned</span><br>"\
		"<span align=center>this paragraph is centered</span><br>"\
		"<span align=right>this paragraph is right aligned</span><br>"\
		"<p>some tables:</p><table><tr><td>one</td><td>two</td><td>three</td></tr><tr><td>four</td><td>five</td><td>six</td></tr></table><br>"\
		"<table><tr><td>one</td><td>two</td></tr><tr><td>three</td><td>four</td></tr><tr><td>five</td><td>six</td></tr></table>", NULL);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSMF), 1.f, 0.5, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(pTab, UI_WIDGET(pSMF));

	ui_WidgetSetWidthEx(UI_WIDGET(pText), 1.0, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(pText), 0, 0, 0, 0.5, UITopLeft);
	ui_TabAddChild(pTab, UI_WIDGET(pText));

	ui_WidgetSetWidthEx(UI_WIDGET(pArea), 1.0, UIUnitPercentage);
	ui_WidgetSetHeight(UI_WIDGET(pArea), 60);
	ui_WidgetSetPositionEx(UI_WIDGET(pArea), 0, 28, 0, 0.5, UITopLeft);
	ui_TabAddChild(pTab, UI_WIDGET(pArea));

	ui_TabGroupAddTab(pTabs, pTab);
}

static void Flowchart(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Flowchart");
	UIFlowchart *pFlow = ui_FlowchartCreate(NULL, NULL, NULL, NULL);
	UIFlowchartButton **pInputs = NULL, **pOutputs = NULL;

	ui_WidgetSetDimensionsEx(UI_WIDGET(pFlow), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);

	eaPush(&pOutputs, ui_FlowchartButtonCreate(pFlow, UIFlowchartNormal, NULL, NULL));
	ui_FlowchartAddNode(pFlow, ui_FlowchartNodeCreate("Start", 0, 0, 100, 100, &pInputs, &pOutputs, NULL));
	eaClear(&pOutputs); eaClear(&pInputs);

	eaPush(&pInputs, ui_FlowchartButtonCreate(pFlow, UIFlowchartNormal, NULL, NULL));
	eaPush(&pOutputs, ui_FlowchartButtonCreate(pFlow, UIFlowchartNormal, ui_LabelCreate("No", 0, 0), NULL));
	eaPush(&pOutputs, ui_FlowchartButtonCreate(pFlow, UIFlowchartNormal, ui_LabelCreate("Yes", 0, 0), NULL));
	eaPush(&pOutputs, ui_FlowchartButtonCreate(pFlow, UIFlowchartIsChild, ui_LabelCreate("Maybe", 0, 0), NULL));
	ui_FlowchartAddNode(pFlow, ui_FlowchartNodeCreate("The 90s?", 150, 0, 100, 100, &pInputs, &pOutputs, NULL));
	eaClear(&pOutputs); eaClear(&pInputs);

	eaPush(&pInputs, ui_FlowchartButtonCreate(pFlow, UIFlowchartNormal, NULL, NULL));
	ui_FlowchartAddNode(pFlow, ui_FlowchartNodeCreate("Stop", 300, 0, 100, 100, &pInputs, &pOutputs, NULL));
	eaClear(&pOutputs); eaClear(&pInputs);

	eaPush(&pInputs, ui_FlowchartButtonCreate(pFlow, UIFlowchartNormal, NULL, NULL));
	eaPush(&pOutputs, ui_FlowchartButtonCreate(pFlow, UIFlowchartNormal, NULL, NULL));
	ui_FlowchartAddNode(pFlow, ui_FlowchartNodeCreate("Stop", 300, 150, 100, 100, &pInputs, &pOutputs, NULL));
	eaClear(&pOutputs); eaClear(&pInputs);

	eaPush(&pInputs, ui_FlowchartButtonCreate(pFlow, UIFlowchartNormal, NULL, NULL));
	ui_FlowchartAddNode(pFlow, ui_FlowchartNodeCreate("Hammertime", 450, 0, 100, 100, &pInputs, &pOutputs, NULL));
	eaClear(&pOutputs); eaClear(&pInputs);

	eaPush(&pInputs, ui_FlowchartButtonCreate(pFlow, UIFlowchartNormal, NULL, NULL));
	eaPush(&pOutputs, ui_FlowchartButtonCreate(pFlow, UIFlowchartNormal, NULL, NULL));
	ui_FlowchartButtonSetSingleConnection(pInputs[0], true);
	ui_FlowchartButtonSetSingleConnection(pOutputs[0], true);
	ui_FlowchartAddNode(pFlow, ui_FlowchartNodeCreate("Collaborate", 450, 150, 100, 100, &pInputs, &pOutputs, NULL));
	eaClear(&pOutputs); eaClear(&pInputs);

	eaPush(&pInputs, ui_FlowchartButtonCreate(pFlow, UIFlowchartNormal, NULL, NULL));
	ui_FlowchartAddNode(pFlow, ui_FlowchartNodeCreate("Listen", 600, 0, 100, 100, &pInputs, &pOutputs, NULL));
	eaClear(&pOutputs); eaClear(&pInputs);

	ui_TabAddChild(pTab, pFlow);
	ui_TabGroupAddTab(pTabs, pTab);

	eaDestroy(&pOutputs);
	eaDestroy(&pInputs);
}

static void ColorPicked(UIColorButton *pButton, bool bFinished, UILabel *pLabel)
{
	char ach[1000];
	Vec4 v4Color;
	ui_ColorButtonGetColor(pButton, v4Color);
	sprintf(ach, "(%0.2f, %0.2f, %0.2f, %0.2f)", v4Color[0], v4Color[1], v4Color[2], v4Color[3]);
	ui_LabelSetText(pLabel, ach);
}

static void ColorPicker(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Color Picker");
	S32 i;
	F32 y = UI_HSTEP;
	for (i = 0; i < 10; i++)
	{
		Vec4 v4Initial = {i * 0.09, i * 0.09, i * 0.09, i * 0.09};
		UIColorButton *pButton = ui_ColorButtonCreateEx(UI_HSTEP, y, 0.0, 1.0 + 0.1 * i, v4Initial);
		UILabel *pLabel = ui_LabelCreate("", UI_WIDGET(pButton)->x + UI_WIDGET(pButton)->width + UI_STEP, y);
		pButton->liveUpdate = true;
		ui_TabAddChild(pTab, pButton);
		ui_TabAddChild(pTab, pLabel);
		ui_ColorButtonSetChangedCallback(pButton, ColorPicked, pLabel);
		ColorPicked(pButton, true, pLabel);
		y += UI_WIDGET(pLabel)->height;
	}

	ui_TabGroupAddTab(pTabs, pTab);
}

static void DragStart(UIWidget *pWidget, char *pchTexture)
{
	ui_DragStart(pWidget, UI_DND_TEXTURENAME, pchTexture, atlasLoadTexture(pchTexture));
}

static void DragDrop(UIWidget *pSource, UIWidget *pDest, UIDnDPayload *pPayload, UIButton *pButton)
{
	ui_ButtonSetImage(pButton, (char *)pPayload->payload);
}

void DragAndDrop(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Drag and Drop");
	UIButton *pBottom;
	char *apchButtons[] = {UI_XBOX_AB, UI_XBOX_BB, UI_XBOX_XB, UI_XBOX_YB};
	S32 i;

	for (i = 0; i < ARRAY_SIZE(apchButtons); i++)
	{
		UIButton *pButton = ui_ButtonCreateImageOnly(apchButtons[i], 0, 0, NULL, NULL);
		ui_WidgetSetDimensionsEx(UI_WIDGET(pButton), 1.f / ARRAY_SIZE(apchButtons), UI_WIDGET(pButton)->height,
			UIUnitPercentage, UIUnitFixed);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 0, (F32)i / ARRAY_SIZE(apchButtons), 0, UITopLeft);
		ui_WidgetSetDragCallback(UI_WIDGET(pButton), DragStart, apchButtons[i]);
		ui_TabAddChild(pTab, pButton);
	}

	pBottom = ui_ButtonCreateWithIcon("Drag To Here", UI_XBOX_AB, NULL, NULL);
	ui_WidgetSetDropCallback(UI_WIDGET(pBottom), DragDrop, pBottom);
	ui_WidgetSetPositionEx(UI_WIDGET(pBottom), 0, 0, 0, 0, UIBottom);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pBottom), 0.8f, 48, UIUnitPercentage, UIUnitFixed);
	ui_TabAddChild(pTab, pBottom);

	ui_TabGroupAddTab(pTabs, pTab);
}

int lowestRand(int size, int iterations)
{
	int i;

	for ( i = 0; i < size; ++i )
	{
		if ( !(rand() & 7) )
			return i;
	}
	for ( i = 0; i < size; ++i )
	{
		if ( !(rand() & 3) )
			return i;
	}
	return 0;
}

char *randomName()
{
	char name[256] = {0};
	char consonants[] = "nstrlmpbcdhfgkjvwzx";
	char doubleConsonants[] = "thshtrchcrplckphqu";
	char vowels[] = "eaiouy";
	int len = (rand() % 12) + 3;
	int i;
	char *c = name;
	int consonantWeight = 0, vowelWeight = 0;

	for ( i = 0; i < len; ++i )
	{
		// consonant
		if ( rand() & 1 && consonantWeight < 2 )
		{
			// double consonant
			if ( rand() % 5 == 0 )
			{
				int pos = lowestRand(ARRAY_SIZE(doubleConsonants)>>1, 2);
				*c = doubleConsonants[pos];
				++c;
				*c = doubleConsonants[pos+1];
				++c;
			}
			else
			{
				*c = consonants[lowestRand(ARRAY_SIZE(consonants), 3)];
				++c;
			}
			vowelWeight = 0;
			++consonantWeight;
		}
		// vowel
		else if ( vowelWeight < 2 )
		{
			*c = vowels[lowestRand(ARRAY_SIZE(vowels), 2)];
			++c;
			consonantWeight = 0;
			++vowelWeight;
		}
		else // try again
			--i;
	}

	*c = 0;
	return strdup(name);
}

static int numAvailableChildren = 3;

static void initializeTreeNode(UITestTreeStructure **node, int makeChildren)
{
	int i, numChildren;
	(*node) = calloc(1,sizeof(UITestTreeStructure));
	(*node)->name = randomName();

	if ( numAvailableChildren < 0 )
		numAvailableChildren = 0;

	if ( makeChildren )
	{
		numChildren = /*(rand() % 20) +*/ 5;
		for ( i = 0; i < numChildren; ++i )
		{
			UITestTreeStructure *newChild = NULL;
			initializeTreeNode(&newChild, !(rand() & 3) && numAvailableChildren--);
			eaPush(&(*node)->children, newChild);
		}
	}
}

static void initializeTreeData()
{
	initializeTreeNode(&treeRoot, 1);
}

void drawTreeNode(UITreeNode *node, const char *field, UI_MY_ARGS, F32 z)
{
	ui_TreeDisplayText(node, ((UITestTreeStructure*)node->contents)->name, UI_MY_VALUES, z);
}

void treeDragCallback(UITree *tree, UITreeNode *node, UITreeNode *dragFromParent, UITreeNode *dragToParent, int dragToIndex, UserData dragData)
{
	UITestTreeStructure *fromParent = (UITestTreeStructure*)dragFromParent->contents;
	UITestTreeStructure *toParent = (UITestTreeStructure*)dragToParent->contents;
	if ( fromParent && toParent )
	{
		eaFindAndRemove(&fromParent->children, (UITestTreeStructure*)node->contents);
		if ( dragToIndex >= eaSize(&toParent->children) )
			eaPush(&toParent->children, (UITestTreeStructure*)node->contents);
		else
			eaInsert(&toParent->children, (UITestTreeStructure*)node->contents, dragToIndex);
		ui_TreeRefresh(tree);
	}
}

static void treeFillChild(UITreeNode *node, void *fillData)
{
	UITestTreeStructure *parent = (UITestTreeStructure*)fillData;
	int i;
	static int fakeCRC = 2;

	for ( i = 0; i < eaSize(&parent->children); ++i )
	{
		UITestTreeStructure *data = parent->children[i];
		UITreeNode *newNode = ui_TreeNodeCreate(
			node->tree, cryptAdler32String(data->name), parse_UITestTreeStructure, data,
			treeFillChild, parent->children[i],
			drawTreeNode, "Test Tree", 20);
		ui_TreeNodeAddChild(node, newNode);
	}
}

static void treeFill(UITreeNode *node, void *fillData)
{
	UITestTreeStructure *data = (UITestTreeStructure*)fillData;
	UITreeNode *newNode = ui_TreeNodeCreate(
		node->tree, cryptAdler32String(data->name), parse_UITestTreeStructure, fillData,
		treeFillChild, fillData,
		drawTreeNode, "Test Tree", 20);
	ui_TreeNodeAddChild(node, newNode);
}

void Tree(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Tree");
	UITree *pTree = ui_TreeCreate(UI_DSTEP, UI_DSTEP, 400, 360);
	numAvailableChildren = 3;
	initializeTreeData();
	ui_TreeNodeSetFillCallback(&pTree->root, treeFill, treeRoot);
	ui_TreeEnableDragAndDrop(pTree);
	ui_TreeSetDragCallback(pTree, treeDragCallback, NULL);
	ui_TreeNodeExpand(&pTree->root);
	ui_TabAddChild(pTab, pTree);
	ui_TabGroupAddTab(pTabs, pTab);
}

static void ProgressBarUpdate(TimedCallback *pCallback, F32 fSince, UITab *pTab)
{
	S32 i;
	for (i = 0; i < eaSize(&pTab->eaChildren); i++)
	{
		UIProgressBar *pBar = (UIProgressBar *)pTab->eaChildren[i];
		pBar->progress += fSince / (10 * (i + 1));
		if (pBar->progress > 1.f)
			pBar->progress = 0.f;
	}
}

static void UnregisterProgressBar(UIWidget *pWidget)
{
	TimedCallback_RemoveByFunction(ProgressBarUpdate);
}

static void ProgressBar(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Progress Bar");
	TimedCallback *pCallback = TimedCallback_Add(ProgressBarUpdate, pTab, 0.1);
	F32 y = UI_HSTEP;
	S32 i;

	for (i = 0; i < 8; i++)
	{
		UIProgressBar *pBar = ui_ProgressBarCreate(0, y, 1.f);
		y += UI_WIDGET(pBar)->height;
		UI_WIDGET(pBar)->widthUnit = UIUnitPercentage;
		ui_TabAddChild(pTab, pBar);
	}
	pTab->eaChildren[0]->onFreeF = UnregisterProgressBar;
	ui_TabGroupAddTab(pTabs, pTab);
}

static void ToolTips(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Tooltips");

	UIButton *b1 = ui_ButtonCreate("normal", 10, 10, NULL, NULL);
	UIButton *b2 = ui_ButtonCreate("smf", 10, 40, NULL, NULL);
	UIButton *b3 = ui_ButtonCreate("...", 10, 70, NULL, NULL);

	ui_WidgetSetDimensionsEx(UI_WIDGET(b1), 100, 25, UIUnitFixed, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(b2), 100, 25, UIUnitFixed, UIUnitFixed);
	ui_WidgetSetDimensionsEx(UI_WIDGET(b3), 100, 25, UIUnitFixed, UIUnitFixed);

	ui_WidgetSetTooltipString(UI_WIDGET(b1), "This is a normal tool tip, with plain text.");
	ui_WidgetSetTooltipString(UI_WIDGET(b2), 
		"<p>This is a tool tip that uses SMF text."\
		"<br>Here is <b>BOLD</b> and <i>Italics</i>."\
		"<span align=left>left aligned</span><br>"\
		"<span align=center>centered</span><br>"\
		"<span align=right>right aligned</span>"\
		"</p>");
	ui_WidgetSetTooltipString(UI_WIDGET(b3), "<p>This<br><b>tool</b><br><b>tip</b><br>was<br>designed<br>to<br>look<br><color = blue>really</color><br><color = red>really</color><br><color = green>really</color><br><color = yellow>really</color><br>bad</p>");

	ui_TabAddChild(pTab, b1);
	ui_TabAddChild(pTab, b2);
	ui_TabAddChild(pTab, b3);

	ui_TabGroupAddTab(pTabs, pTab);
}

static void Graph(UITabGroup *pTabs)
{
	Vec2 v2Lower = {0, 0};
	Vec2 v2Upper = {1, 1};
	UIGraphPane *pGraphPane = ui_GraphPaneCreate("X Axis", "Y Axis", v2Lower, v2Upper, 6, true, true, true);
	UIGraph *pGraph = ui_GraphPaneGetGraph(pGraphPane);
	UITab *pTab = ui_TabCreate("Graphs");
	Vec2 v2MinUpper = {1, 1};
	Vec2 v2MaxUpper = {1, 1.5};
	Vec2 v2MinLower = {-1, -1};
	Vec2 v2MaxLower = {0, 0};

	ui_WidgetSetDimensionsEx(UI_WIDGET(pGraph), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPadding(UI_WIDGET(pGraph), UI_STEP, UI_STEP);

	ui_GraphPaneSetBounds(pGraphPane, v2MinLower, v2MaxLower, v2MinUpper, v2MaxUpper);
	ui_GraphSetDrawConnection(pGraph, true, true);
	ui_GraphSetSort(pGraph, true);
	ui_GraphSetMinPoints(pGraph, 1);
	ui_GraphSetResolution(pGraph, 10, 10);
	ui_GraphSetDrawScale(pGraph, true);

	ui_TabAddChild(pTab, pGraphPane);
	ui_TabGroupAddTab(pTabs, pTab);
}

static void TimelineTrackRC(UITimelineTrack *track, int time, void *none)
{
	printf("TrackRC(%s): %d\n", track->name, time);
}

static void TimelineTimeSet(UITimeline *timeline, int time, void *none)
{
	printf("SetTime: %d\n", time);
}

static void TimelineMovedCB(UITimelineKeyFrame *frame, void *none)
{
	printf("FrameChanged (%s -> %d): %d\n", frame->track->name, eaFind(&frame->track->frames, frame), frame->time);
}

static void Timeline(UITabGroup *pTabs)
{
	int i, j;
	UITab *pTab = ui_TabCreate("Timelines");
	UITimeline *pTimeline;
	Color *colorList[10] = {&ColorBlack, &ColorRed, &ColorHalfBlack, 
		&ColorBlue, &ColorGray, &ColorPurple, 
		&ColorOrange, &ColorCyan, &ColorYellow, &ColorMagenta};
		
	pTimeline = ui_TimelineCreate(0,0,600);
	ui_WidgetSetDimensionsEx( UI_WIDGET( pTimeline ), 1, 1, UIUnitPercentage, UIUnitPercentage );
	ui_TimelineSetTimeChangedCallback(pTimeline, TimelineTimeSet, NULL);
	ui_TabAddChild( pTab, pTimeline );


	for ( i=0; i < 10; i++ ) {
		char buf[255];
		F32 new_time = 0;
		bool has_length = (rand()%3 == 0);
		bool use_lines = (rand()%2 == 0);
		UITimelineTrack *pTimelineTrack;

		sprintf(buf, "Track %d", i+1);
		pTimelineTrack = ui_TimelineTrackCreate(buf);
		pTimelineTrack->draw_lines = (!has_length && use_lines);
		pTimelineTrack->prevent_order_changes = use_lines;
		ui_TimelineTrackSetRightClickCallback(pTimelineTrack, TimelineTrackRC, NULL);

		if(i < 5) {
			pTimelineTrack->draw_background = true;
			pTimelineTrack->allow_overlap = true;
		} else {
			pTimelineTrack->height = 20;
		}

		for ( j=0; j < 20; j++ ) {
			UITimelineKeyFrame *pTimelineKeyFrame = ui_TimelineKeyFrameCreate();
			F32 time = randomPositiveF32() + 0.1;

			new_time += time;
			pTimelineKeyFrame->time = new_time*1000.0f;
			pTimelineKeyFrame->color = *colorList[i];
			ui_TimelineKeyFrameSetChangedCallback(pTimelineKeyFrame, TimelineMovedCB, NULL);

			if(has_length)
				pTimelineKeyFrame->length = (randomPositiveF32()*0.5f + 0.1f)*1000.0f;

			ui_TimelineTrackAddFrame(pTimelineTrack, pTimelineKeyFrame);
		}
		ui_TimelineAddTrack(pTimeline, pTimelineTrack);
	}

	ui_TabGroupAddTab(pTabs, pTab);
}

static void Gen(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Gen");
	UIGenWidget *pGenWidget = ui_GenWidgetCreate("UITests_Root", 2);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pGenWidget), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPadding(UI_WIDGET(pGenWidget), UI_STEP, UI_STEP);
	ui_TabAddChild(pTab, pGenWidget);
	ui_TabGroupAddTab(pTabs, pTab);
}

static void Sprites(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Sprites");

	UISprite* sprite1 = ui_SpriteCreate( 0, 0, 100, 100, "Common_Gray_Swatches" );
	UISprite* sprite2 = ui_SpriteCreate( 0, 120, 100, 100, "" );

	ui_SpriteSetBasicTexture( sprite2, gfxHeadshotDebugTexture() );
	ui_TabAddChild( pTab, sprite1 );
	ui_TabAddChild( pTab, sprite2 );
	ui_TabGroupAddTab(pTabs, pTab);
}

static void UGCSkins(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("UGCSkins");
	UIScrollArea* pArea = ui_ScrollAreaCreate( 0, 0, 0, 0, 1, 1, true, true );
	UIPane* bgPane = ui_PaneCreate( 0, 0, 1, 1, UIUnitFixed, UIUnitFixed, false );
	UIPane* pane;
	pArea->autosize = true;
	ui_WidgetSetDimensionsEx( UI_WIDGET( pArea ), 1, 1, UIUnitPercentage, UIUnitPercentage );
	ui_TabAddChild( pTab, pArea );

	pane = ui_PaneCreate( 10, 10, 200, 60, UIUnitFixed, UIUnitFixed, false );
	ui_WidgetSetTextString( UI_WIDGET( pane ), "RoundedPressed" );
	ui_PaneSetStyle( pane, "CarbonFibre_RoundedPressed", true, false );
	ui_PaneAddChild( bgPane, pane );
	pane = ui_PaneCreate( 215, 10, 200, 60, UIUnitFixed, UIUnitFixed, false );
	ui_WidgetSetTextString( UI_WIDGET( pane ), "RoundedNormal" );
	ui_PaneSetStyle( pane, "CarbonFibre_RoundedNormal", true, false );
	ui_PaneAddChild( bgPane, pane );
	pane = ui_PaneCreate( 420, 10, 200, 60, UIUnitFixed, UIUnitFixed, false );
	ui_WidgetSetTextString( UI_WIDGET( pane ), "RoundedHover" );
	ui_PaneSetStyle( pane, "CarbonFibre_RoundedHover", true, false );
	ui_PaneAddChild( bgPane, pane );
	
	pane = ui_PaneCreate( 10, 75, 200, 60, UIUnitFixed, UIUnitFixed, false );
	ui_WidgetSetTextString( UI_WIDGET( pane ), "SharpPressed" );
	ui_PaneSetStyle( pane, "CarbonFibre_SharpPressed", true, false );
	ui_PaneAddChild( bgPane, pane );
	pane = ui_PaneCreate( 215, 75, 200, 60, UIUnitFixed, UIUnitFixed, false );
	ui_WidgetSetTextString( UI_WIDGET( pane ), "SharpNormal" );
	ui_PaneSetStyle( pane, "CarbonFibre_SharpNormal", true, false );
	ui_PaneAddChild( bgPane, pane );
	pane = ui_PaneCreate( 420, 75, 200, 60, UIUnitFixed, UIUnitFixed, false );
	ui_WidgetSetTextString( UI_WIDGET( pane ), "SharpHover" );
	ui_PaneSetStyle( pane, "CarbonFibre_SharpHover", true, false );
	ui_PaneAddChild( bgPane, pane );
	
	pane = ui_PaneCreate( 10, 140, 200, 60, UIUnitFixed, UIUnitFixed, false );
	ui_WidgetSetTextString( UI_WIDGET( pane ), "TextEntrySelected" );
	ui_PaneSetStyle( pane, "CarbonFibre_TextEntrySelected", true, false );
	ui_PaneAddChild( bgPane, pane );
	pane = ui_PaneCreate( 215, 140, 200, 60, UIUnitFixed, UIUnitFixed, false );
	ui_WidgetSetTextString( UI_WIDGET( pane ), "TextEntryNormal" );
	ui_PaneSetStyle( pane, "CarbonFibre_TextEntryNormal", true, false );
	ui_PaneAddChild( bgPane, pane );
	pane = ui_PaneCreate( 420, 140, 200, 60, UIUnitFixed, UIUnitFixed, false );
	ui_WidgetSetTextString( UI_WIDGET( pane ), "TextEntryHover" );
	ui_PaneSetStyle( pane, "CarbonFibre_TextEntryHover", true, false );
	ui_PaneAddChild( bgPane, pane );
	
	pane = ui_PaneCreate( 10, 205, 200, 60, UIUnitFixed, UIUnitFixed, false );
	ui_WidgetSetTextString( UI_WIDGET( pane ), "BackgroundDark" );
	ui_PaneSetStyle( pane, "CarbonFibre_BackgroundDark", true, false );
	ui_PaneAddChild( bgPane, pane );
	pane = ui_PaneCreate( 215, 205, 200, 60, UIUnitFixed, UIUnitFixed, false );
	ui_WidgetSetTextString( UI_WIDGET( pane ), "BackgroundLight" );
	ui_PaneSetStyle( pane, "CarbonFibre_BackgroundLight", true, false );
	ui_PaneAddChild( bgPane, pane );
	pane = ui_PaneCreate( 420, 205, 200, 60, UIUnitFixed, UIUnitFixed, false );
	ui_WidgetSetTextString( UI_WIDGET( pane ), "BackgroundTextured" );
	ui_PaneSetStyle( pane, "CarbonFibre_BackgroundTextured", true, false );
	ui_PaneAddChild( bgPane, pane );
	
	pane = ui_PaneCreate( 10, 270, 200, 60, UIUnitFixed, UIUnitFixed, false );
	ui_WidgetSetTextString( UI_WIDGET( pane ), "MenuBarBG" );
	ui_PaneSetStyle( pane, "CarbonFibre_MenuBarBG", true, false );
	ui_PaneAddChild( bgPane, pane );
	pane = ui_PaneCreate( 215, 270, 200, 60, UIUnitFixed, UIUnitFixed, false );
	ui_WidgetSetTextString( UI_WIDGET( pane ), "MenuPopupBG" );
	ui_PaneSetStyle( pane, "CarbonFibre_MenuPopupBG", true, false );
	ui_PaneAddChild( bgPane, pane );
	pane = ui_PaneCreate( 420, 270, 200, 60, UIUnitFixed, UIUnitFixed, false );
	ui_WidgetSetTextString( UI_WIDGET( pane ), "MenuEntryHover" );
	ui_PaneSetStyle( pane, "CarbonFibre_MenuEntryHover", true, false );
	ui_PaneAddChild( bgPane, pane );

	ui_WidgetSetDimensions( UI_WIDGET( bgPane ), 700, 400 );
	ui_PaneSetStyle( bgPane, "CarbonFibre_BackgroundTextured", true, false );
	ui_ScrollAreaAddChild( pArea, UI_WIDGET( bgPane ));

	ui_TabGroupAddTab( pTabs, pTab );
}

typedef UISizer *(*SizerDemoCreateFunction)();

static UISizer *sizerDemoCreateBoxSizer(UIDirection direction)
{
	UIBoxSizer *boxSizer = ui_BoxSizerCreate(direction);
	UIButton *helloButton = ui_ButtonCreate("Hello", 0, 0, NULL, NULL);
	UIButton *worldButton = ui_ButtonCreate("World", 0, 0, NULL, NULL);
	UIButton *fooButton = ui_ButtonCreate("Foo", 0, 0, NULL, NULL);
	UIButton *barButton = ui_ButtonCreate("Bar", 0, 0, NULL, NULL);

	ui_BoxSizerAddWidget(boxSizer, UI_WIDGET(helloButton), 0, UINoDirection, 0);
	ui_BoxSizerAddWidget(boxSizer, UI_WIDGET(worldButton), 0, UINoDirection, 0);
	ui_BoxSizerAddWidget(boxSizer, UI_WIDGET(fooButton), 0, UINoDirection, 0);
	ui_BoxSizerAddWidget(boxSizer, UI_WIDGET(barButton), 0, UINoDirection, 0);

	return UI_SIZER(boxSizer);
}

static UISizer *sizerDemoCreateHorizontalBoxSizer()
{
	return sizerDemoCreateBoxSizer(UIHorizontal);
}

static UISizer *sizerDemoCreateVerticalBoxSizer()
{
	return sizerDemoCreateBoxSizer(UIVertical);
}

static UISizer *sizerDemoCreateNestedBoxSizer()
{
	UIButton *helloButton = ui_ButtonCreate("Hello", 0, 0, NULL, NULL);
	UIButton *worldButton = ui_ButtonCreate("World", 0, 0, NULL, NULL);
	UIButton *fooButton = ui_ButtonCreate("Foo", 0, 0, NULL, NULL);
	UIButton *barButton = ui_ButtonCreate("Bar", 0, 0, NULL, NULL);

	UIBoxSizer *verticalBoxSizer = ui_BoxSizerCreate(UIVertical);
	UIBoxSizer *horizontalBoxSizer = ui_BoxSizerCreate(UIHorizontal);

	ui_BoxSizerAddWidget(verticalBoxSizer, UI_WIDGET(helloButton), 1, UIWidth, 2);
	ui_BoxSizerAddSpacer(verticalBoxSizer, 10);
	ui_BoxSizerAddSizer(verticalBoxSizer, UI_SIZER(horizontalBoxSizer), 0, UIWidth, 0);

	ui_BoxSizerAddWidget(horizontalBoxSizer, UI_WIDGET(worldButton), 0, UINoDirection, 2);
	ui_BoxSizerAddFiller(horizontalBoxSizer, 1);
	ui_BoxSizerAddWidget(horizontalBoxSizer, UI_WIDGET(fooButton), 0, UINoDirection, 2);
	ui_BoxSizerAddWidget(horizontalBoxSizer, UI_WIDGET(barButton), 0, UINoDirection, 2);

	return UI_SIZER(verticalBoxSizer);
}

static UISizer *sizerDemoCreateProportionBoxSizer()
{
	UIBoxSizer *horizontalBoxSizer = ui_BoxSizerCreate(UIHorizontal);
	UIButton *helloButton = ui_ButtonCreate("Hello", 0, 0, NULL, NULL);
	UIButton *worldButton = ui_ButtonCreate("World", 0, 0, NULL, NULL);
	UIButton *fooButton = ui_ButtonCreate("Foo", 0, 0, NULL, NULL);
	UIButton *barButton = ui_ButtonCreate("Bar", 0, 0, NULL, NULL);

	ui_BoxSizerAddWidget(horizontalBoxSizer, UI_WIDGET(helloButton), 0, UINoDirection, 0);
	ui_BoxSizerAddWidget(horizontalBoxSizer, UI_WIDGET(worldButton), 1, UINoDirection, 0);
	ui_BoxSizerAddWidget(horizontalBoxSizer, UI_WIDGET(fooButton), 2, UINoDirection, 0);
	ui_BoxSizerAddWidget(horizontalBoxSizer, UI_WIDGET(barButton), 3, UINoDirection, 0);

	return UI_SIZER(horizontalBoxSizer);
}

static UISizer *sizerDemoCreateDirectionBoxSizer()
{
	UIBoxSizer *verticalBoxSizer = ui_BoxSizerCreate(UIVertical);
	UIButton *helloButton = ui_ButtonCreate("Hello", 0, 0, NULL, NULL);
	UIButton *worldButton = ui_ButtonCreate("World", 0, 0, NULL, NULL);
	UIButton *fooButton = ui_ButtonCreate("Foo", 0, 0, NULL, NULL);
	UIButton *barButton = ui_ButtonCreate("Bar", 0, 0, NULL, NULL);

	ui_BoxSizerAddWidget(verticalBoxSizer, UI_WIDGET(helloButton), 0, UILeft, 0);
	ui_BoxSizerAddWidget(verticalBoxSizer, UI_WIDGET(worldButton), 0, UIRight, 0);
	ui_BoxSizerAddWidget(verticalBoxSizer, UI_WIDGET(fooButton), 0, UINoDirection, 0);
	ui_BoxSizerAddWidget(verticalBoxSizer, UI_WIDGET(barButton), 0, UIWidth, 0);

	return UI_SIZER(verticalBoxSizer);
}

static UISizer *sizerDemoCreateGridSizer()
{
	UIGridSizer *gridSizer = ui_GridSizerCreate(/*nrows=*/3, /*ncols=*/2, /*rowProportion=*/0, /*colProportion=*/0, /*rowMinHeight=*/0, /*colMinHeight=*/0);
	UILabel *labelA = ui_LabelCreate("Label A:", 0, 0);
	UILabel *labelB = ui_LabelCreate("Label B:", 0, 0);
	UILabel *labelC = ui_LabelCreate("Label C:", 0, 0);
	UIButton *buttonA = ui_ButtonCreate("Button A", 0, 0, NULL, NULL);
	UIButton *buttonB = ui_ButtonCreate("Button B", 0, 0, NULL, NULL);
	UIButton *buttonC = ui_ButtonCreate("Button C", 0, 0, NULL, NULL);

	ui_GridSizerSetColProportion(gridSizer, /*col=*/1, /*proportion=*/1);

	ui_GridSizerSetRowProportion(gridSizer, /*row=*/1, /*proportion=*/1);
	ui_GridSizerSetRowProportion(gridSizer, /*row=*/2, /*proportion=*/2);

	ui_GridSizerAddWidget(gridSizer, UI_WIDGET(labelA), /*row=*/0, /*col=*/0, /*direction=*/UIRight, /*border=*/0);
	ui_GridSizerAddWidget(gridSizer, UI_WIDGET(labelB), /*row=*/1, /*col=*/0, /*direction=*/UIRight, /*border=*/0);
	ui_GridSizerAddWidget(gridSizer, UI_WIDGET(labelC), /*row=*/2, /*col=*/0, /*direction=*/UIRight, /*border=*/0);
	ui_GridSizerAddWidget(gridSizer, UI_WIDGET(buttonA), /*row=*/0, /*col=*/1, /*direction=*/UILeft, /*border=*/0);
	ui_GridSizerAddWidget(gridSizer, UI_WIDGET(buttonB), /*row=*/1, /*col=*/1, /*direction=*/UIRight, /*border=*/0);
	ui_GridSizerAddWidget(gridSizer, UI_WIDGET(buttonC), /*row=*/2, /*col=*/1, /*direction=*/UIAnyDirection, /*border=*/0);

	return UI_SIZER(gridSizer);
}

typedef struct SizerDemo {
	const char *text;
	SizerDemoCreateFunction sizerDemoCreateF;
	UIWindow *window;
} SizerDemo;

static SizerDemo s_SizerDemos[] = {
	{ "Horizontal Box Sizer", sizerDemoCreateHorizontalBoxSizer, NULL },
	{ "Vertical Box Sizer", sizerDemoCreateVerticalBoxSizer, NULL },
	{ "Nested Box Sizer", sizerDemoCreateNestedBoxSizer, NULL },
	{ "Proportion Box Sizer", sizerDemoCreateProportionBoxSizer, NULL },
	{ "Direction Box Sizer", sizerDemoCreateDirectionBoxSizer, NULL },
	{ "Grid Box Sizer", sizerDemoCreateGridSizer, NULL }
};

static void sizerDemoButtonClicked(UIAnyWidget *unused, UserData sizerDemoIndex)
{
	SizerDemo *demo = &s_SizerDemos[(int)sizerDemoIndex];

	if(!demo->window)
	{
		UISizer *sizer = demo->sizerDemoCreateF();
		UIWindow *window = ui_WindowCreate(demo->text, 200, 200, 300, 300);
		Vec2 minSize;

		ui_WidgetSetSizer(UI_WIDGET(window), sizer);

		ui_SizerGetMinSize(sizer, minSize);

		ui_WindowSetDimensions(window, minSize[0], minSize[1], minSize[0], minSize[1]);

		demo->window = window;
	}

	ui_WindowShowEx(demo->window, true);
}

static void Sizers(UITabGroup *pTabs)
{
	UITab *pTab = ui_TabCreate("Sizers");
	int count = sizeof(s_SizerDemos) / sizeof(SizerDemo);
	int i;
	F32 x = 2;

	for(i = 0; i < count; i++)
	{
		SizerDemo *demo = &s_SizerDemos[i];
		UIButton *button = ui_ButtonCreate(demo->text, x, 2, sizerDemoButtonClicked, (UserData)i);
		x += UI_WIDGET(button)->width * UI_WIDGET(button)->scale + 2;
		ui_TabAddChild(pTab, button);
	}

	ui_TabGroupAddTab(pTabs, pTab);
}

typedef void (*UITestTabCreate)(UITabGroup *pTabs);
static UITestTabCreate acbTestFunctions[] = {
	Labels,
	Buttons,
	Windows,
	TextEntries,
	ScrollArea,
	List,
	Coordinates,
	ComboBox,
	CheckBox,
	AutoWidget,
	Sliders,
	Menus,
	TestSMF,
	Flowchart,
	ColorPicker,
	DragAndDrop,
	Tree,
	ProgressBar,
	ToolTips,
	Graph,
	Timeline,
	Gen,
	Sprites,
	UGCSkins,
	Sizers,
};

static bool TestWindowClose(UIWindow *pWindow, UserData closeData)
{
	ui_WidgetQueueFree(UI_WIDGET(pWindow));
	return true;
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void ui_Tests(void)
{
	UIWindow *pWindow = ui_WindowCreate("User Interface Tests", 0, 0, 500, 500);
	UITabGroup *pTabs = ui_TabGroupCreate(0, 0, 1.f, 1.f);
	S32 i;

	ui_WidgetSetDimensionsEx(UI_WIDGET(pTabs), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WindowSetDimensions(pWindow, 500, 500, 200, 200);

	for (i = 0; i < ARRAY_SIZE(acbTestFunctions); i++)
		acbTestFunctions[i](pTabs);

	ui_WindowSetCloseCallback(pWindow, TestWindowClose, NULL);
	pTabs->bEqualWidths = true;
	ui_TabGroupSetActiveIndex(pTabs, 0);
	ui_WindowAddChild(pWindow, pTabs);
	ui_WindowSetCycleBetweenDisplays(pWindow, true);
	ui_WindowShowEx(pWindow, true);
}

static void NonInteractiveTestUpdate(TimedCallback *pCallback, F32 fSince, UIWindow *pWindow)
{
	UITabGroup *pTabs = (UITabGroup *)UI_WIDGET(pWindow)->children[0];
	S32 iTab = ui_TabGroupGetActiveIndex(pTabs);
	UIWidgetGroup eaChildren = NULL;
	S32 i;
	eaCopy(&eaChildren, &UI_WIDGET(pTabs)->children);
	for (i = 0; i < eaSize(&eaChildren); i++)
		ui_SetFocus(eaChildren[i]);
	eaDestroy(&eaChildren);

	iTab++;
	if (iTab >= eaSize(&pTabs->eaTabs))
	{
		ui_WindowClose(pWindow);
		TimedCallback_RemoveByFunction(NonInteractiveTestUpdate);
	}
	else
		ui_TabGroupSetActiveIndex(pTabs, iTab);
}

static bool NonInteractiveTestWindowClose(UIWindow *pWindow, UserData closeData)
{
	ui_WidgetForceQueueFree(UI_WIDGET(pWindow));
	ControllerScript_Succeeded();
	return true;
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void ui_Tests_NonInteractive(void)
{
	UIWindow *pWindow = ui_WindowCreate("User Interface Tests", 0, 0, 500, 500);
	UITabGroup *pTabs = ui_TabGroupCreate(0, 0, 1.f, 1.f);
	TimedCallback *pCallback = TimedCallback_Add(NonInteractiveTestUpdate, pWindow, 0.f);
	S32 i;

	ui_WidgetSetDimensionsEx(UI_WIDGET(pTabs), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WindowSetDimensions(pWindow, 500, 500, 200, 200);

	for (i = 0; i < ARRAY_SIZE(acbTestFunctions); i++)
		acbTestFunctions[i](pTabs);

	ui_WindowSetCloseCallback(pWindow, NonInteractiveTestWindowClose, NULL);
	pTabs->bEqualWidths = true;
	ui_TabGroupSetActiveIndex(pTabs, 0);
	ui_WindowAddChild(pWindow, pTabs);
	ui_WindowShowEx(pWindow, true);
}

static int TestCmdParse(const char *pchCommand, UserData dummy)
{
	return globCmdParse(pchCommand);
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void ui_TestSecondDevice(void)
{
	UIWindow *window = ui_WindowCreate("A test window", 50, 50, 300, 150);
	UITextEntry *entry = ui_TextEntryCreate("Enter text", 0, 0);
	UIButton *button = ui_ButtonCreate("Click to set text", 0, 100, NULL, entry);
	UIButton *button2 = ui_ButtonCreate("Test UIList", 100, 100, NULL, NULL);
	RdrDevice *pDevice = ui_AuxDeviceCreate("Another Device", NULL, NULL);

	ui_WindowSetDimensions(window, 300, 150, 200, 100);

	ui_WidgetSetDimensions((UIWidget *)button, 150, 50);
	ui_WindowAddChild(window, button);

	ui_WidgetSetDimensions((UIWidget *)entry, 150, 50);
	ui_WindowAddChild(window, entry);

	ui_ButtonSetCommand(button2, "ui_TestList");
	ui_WindowAddChild(window, button2);

	ui_WindowAddToDevice(window, pDevice);
}

AUTO_COMMAND;
void ui_TestGenericDialog(const char *pchTitle, ACMD_SENTENCE pchText)
{
	ui_DialogPopup(pchTitle, pchText);
}

AUTO_COMMAND;
void ui_TestSpecificDialog(ACMD_SENTENCE pchText)
{
	ui_WindowShowEx(UI_WINDOW(ui_DialogCreateEx(NULL, pchText, NULL, NULL, NULL, "Do It!", kUIDialogButton_Ok, "Cancel", kUIDialogButton_Cancel, NULL)), true);
}

struct {
	UIWindow *window;
	UIWebView *view;
} wvTest;

AUTO_COMMAND;
void ui_TestWebView()
{
	wvTest.window = ui_WindowCreate("WebView", 0, 0, 500, 500);
	wvTest.view = ui_WebViewCreate("http://www.google.com", 500, 500);
	wvTest.view->viewportFill = true;

	ui_WidgetSetDimensionsEx(UI_WIDGET(wvTest.view), 1, 1, UIUnitPercentage, UIUnitPercentage);

	ui_WindowAddChild(wvTest.window, UI_WIDGET(wvTest.view));
	ui_WindowShowEx(wvTest.window, true);
}

