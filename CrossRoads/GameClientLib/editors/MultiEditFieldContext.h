//
// MultiEditFieldContext.h
//
#pragma once
GCC_SYSTEM

#include "MultiEditField.h"
#include "textparser.h"

typedef enum MEFieldType MEFieldType;
typedef struct MEField MEField;
typedef struct MEFieldContext MEFieldContext;
typedef struct MEFieldContextEntry MEFieldContextEntry;
typedef struct UILabel UILabel;
typedef struct UIPane UIPane;
typedef struct UIScrollArea UIScrollArea;
typedef struct UISeparator UISeparator;
typedef struct UIWidget UIWidget;
typedef struct UIRadioButton UIRadioButton;
typedef struct UIRadioButtonGroup UIRadioButtonGroup;

#define MEFC_DEFAULT_X 5
#define MEFC_DEFAULT_Y 5
#define MEFC_DEFAULT_X_DATA_START 150
#define MEFC_DEFAULT_X_INDENT_STEP 20
#define MEFC_DEFAULT_WIDGET_HEIGHT 22
#define MEFC_DEFAULT_Y_STEP 25
#define MEFC_DEFAULT_Y_SPACER 15
#define MEFC_DEFAULT_TEXT_AREA_HEIGHT 3
#define MEFC_DEFAULT_DATA_WIDGET_WIDTH 1

#define MEFC_DEFAULT_ERROR_ICON_SPACE_WIDTH 30
#define MEFC_DEFAULT_ACTION_BUTTON_PAD 0

#define MEFC_DEFAULT_LEFT_PAD 0
#define MEFC_DEFAULT_RIGHT_PAD 0
#define MEFC_DEFAULT_TOP_PAD 0
#define MEFC_DEFAULT_BOTTOM_PAD 0

typedef void (*MEContextFieldAddedCallback)(MEFieldContextEntry *pEntry, void *pUserData);

typedef bool (*MEFieldContextErrorFunction)(void *pErrorContext, const char *pchFieldName, int iFieldIndex, char **estrToolTip_out);
typedef void (*MEFieldContextAllocResetFn)(void *pPtr);

typedef struct MEFieldContextEntry
{
	const char *pchFieldName;
	int iFieldIndex;
	int iButtonIndex;
	bool bInited;
	bool bTouched;
	bool bNeedsDataUpdate;
	bool bOverrideActive;
	bool bActive;
	bool bErrorPaddingSet;
	int iActionButtons;

	UILabel *pLabel;
	UILabel *pLabel2;
	UIButton *pButton;
	UICheckButton *pCheck;
	UISeparator *pSeparator;
	UISprite *pSprite;
	UIRadioButtonGroup* pRadioGroup;
	UIRadioButton* pRadioButton1;
	UIRadioButton* pRadioButton2;
	UIWidget *pCustomWidget;

	UIWindow *pWindow;

	int iFieldCount;				//The count of active fields
	MEField **ppFields;				//Each field assumed to point to same data in different structs

} MEFieldContextEntry;

typedef struct MEFieldContextAlloc
{
	const char *pchFieldName;
	int iIndex;
	size_t iSize;
	void *pPtr;
	MEFieldContextAllocResetFn pfResetFunction;
	ParseTable *pParseTable;
} MEFieldContextAlloc;

AUTO_STRUCT;
typedef struct MEFieldContext
{
	AST_STOP
	struct	//Internal data to be preserved on copy
	{
		const char *pchName;
		void **ppOldData;			
		void **ppNewData;			
		ParseTable *pTable;	

		MEFieldContext *pParent;
		MEFieldContext* pExternalContextPrevContext;				
		MEFieldContext **ppChildren;			
		MEFieldContextEntry **ppEntries;
		MEFieldContextAlloc **ppAllocs;
	} id;
	bool bIsExternalContext;

	//Other data that will be copied from parent to child

	UIWidget *pUIContainer;
	MEFieldPreChangeCallback cbPreChanged;
	UserData pPreChangedData;
	MEFieldChangeCallback cbChanged;
	bool bSkipSiblingChangedCallbacks;
	UserData pChangedData;

	MEContextFieldAddedCallback cbFieldAdded;
	UserData pFieldAddedData;

	MEFieldContextErrorFunction pErrorFunction;
	const char *pchErrorIcon;
	IVec2 iErrorIconSize;
	IVec2 iErrorIconOffset;
	bool bErrorIconOffsetFromRight;
	void *pErrorContext;

	AST_START
	int iXPos;	//Try not to change this directly, instead use the helpers bellow.  If appropriate, make new helpers.
	float fXPosPercentage;
	int iYPos;	//Try not to change this directly, instead use the helpers bellow.  If appropriate, make new helpers.
	
	// Layout of a field
	int iXLabelStart;
	int iYLabelStart;
	bool bLabelPaddingFromData;
	int iXDataStart;
	int iYDataStart;

	int iXIndentStep;
	int iWidgetHeight;
	int iYStep;
	int iYSpacer;
	float fDataWidgetWidth;
	const char* astrOverrideSkinName; AST(POOL_STRING)

	int iErrorIconSpaceWidth;
	int iActionButtonPad;

	int iRightPad;
	int iLeftPad;
	int iTopPad;
	int iBottomPad;

	bool bDisabled;
	bool bLabelsDisabled;

	int iEditableMaxLength; // If non zero, sets the max length on text area and text entry
	int iTextAreaHeight; // If non zero, sets the default height (in rows) of a text area
	bool bTextEntryTrimWhitespace; // Sets the corresponding flag on text entry

	bool bDontSortComboEnums;

} MEFieldContext;
extern ParseTable parse_MEFieldContext[];
#define TYPE_parse_MEFieldContext MEFieldContext

AUTO_STRUCT;
typedef struct MEFieldGenericError {
	char *pchFieldName;	
	int iFieldIndex;
	char *estrErrorText;				AST(ESTRING)
} MEFieldGenericError;
extern ParseTable parse_MEFieldGenericError[];
#define TYPE_parse_MEFieldGenericError MEFieldGenericError

AUTO_STRUCT;
typedef struct MEFieldGenericErrorList {
	MEFieldGenericError **eaErrors;
} MEFieldGenericErrorList;
extern ParseTable parse_MEFieldGenericErrorList[];
#define TYPE_parse_MEFieldGenericErrorList MEFieldGenericErrorList

MEFieldContext* MEContextPush(const char *pchName, void *pOld, void *pNew, ParseTable *pTable);
MEFieldContext* MEContextPushEA(const char *pchName, void **ppOldData, void **ppNewData, ParseTable *pTable);

void MEContextPop(const char *pchName);
void MEContextPopNoIncrementYPos(const char *pchName);
bool MEContextExists();
MEFieldContext* MEContextGetCurrent(void);
void MEContextDestroyByName(const char *pchName);

MEFieldContext* MEContextCreateExternalContext(const char* pchName);
void MEContextPushExternalContext(MEFieldContext* pContext, void *pOld, void *pNew, ParseTable *pTable);
void MEContextPushEAExternalContext(MEFieldContext* pContext, void **ppOldData, void **ppNewData, ParseTable *pTable);
void MEContextPopExternalContext(MEFieldContext* pContext);
void MEContextDestroyExternalContext(MEFieldContext* ctx);

void MEContextSetParent(UIWidget *pParent);

bool MEContextFieldDiff(const char *pchFieldName);
void* MEContextGetData(void);
ParseTable* MEContextGetParseTable(void);

void MEContextIndentRight();
void MEContextIndentLeft();
void MEContextSetIndent(int iIndent);
void MEContextStepBackUp();
void MEContextStepDown();
void MEContextAddSpacer();
void MEContextAddCustomSpacer( int space );

void MEContextSetEnabled(bool bEnabled);

void MEContextSetErrorFunction(MEFieldContextErrorFunction pFunction);
void MEContextSetErrorIcon(const char *pchErrorIcon, int iWidth, int iHeight);
void MEContextSetErrorContext(void *pErrorContext);
void MEContextSetEntryError(MEFieldContextEntry *pEntry, bool bHasError, const char *pchToolTip);
void MEContextSetEntryErrorForField(MEFieldContextEntry *pEntry, const char* pcFieldNameStr);
void MEContextSetActive(MEFieldContextEntry *pEntry, bool bActive);

bool MEContextGenericErrorFunction(MEFieldGenericErrorList *pErrorList, const char *pchFieldName, int iFieldIndex, char **estrToolTip_out);
bool MEContextGenericErrorMsgFunction(MEFieldGenericErrorList *pErrorList, const char *pchFieldName, int iFieldIndex, char **estrToolTip_out);
MEFieldGenericError *MEContextGenericErrorAdd(MEFieldGenericErrorList *pErrorList, const char *pchFieldName, int iFieldIndex, const char *pcErrorFormat, ...);

MEFieldContextEntry* MEContextAddLabel(const char *pchUID, const char *pchLabelText, const char *pchToolTip);
MEFieldContextEntry* MEContextAddLabelMsg(const char *pchUID, const char *pchLabelText, const char *pchToolTip);
MEFieldContextEntry* MEContextAddLabelIndex(const char *pchUID, int index, const char *pchLabelText, const char *pchToolTip);
MEFieldContextEntry* MEContextAddLabelIndexMsg(const char *pchUID, int index, const char *pchLabelText, const char *pchToolTip);
MEFieldContextEntry* MEContextAddTwoLabels(const char *pchUID, const char *pchLabel1Text, const char *pchLabel2Text, const char *pchToolTip);
MEFieldContextEntry* MEContextAddTwoLabelsMsg(const char *pchUID, const char *pchLabel1Text, const char *pchLabel2Text, const char *pchToolTip);
MEFieldContextEntry* MEContextAddTwoLabelsIndex(const char *pchUID, int index, const char *pchLabel1Text, const char *pchLabel2Text, const char *pchToolTip);
MEFieldContextEntry* MEContextAddTwoLabelsIndexMsg(const char *pchUID, int index, const char *pchLabel1Text, const char *pchLabel2Text, const char *pchToolTip);
MEFieldContextEntry* MEContextAddButton(const char *pchButtonText, const char *pchButtonIcon, UIActivationFunc clickedF, UserData clickedData, const char *pchUID, const char *pchLabelText, const char *pchToolTip);
MEFieldContextEntry* MEContextAddButtonMsg(const char *pchButtonText, const char *pchButtonIcon, UIActivationFunc clickedF, UserData clickedData, const char *pchUID, const char *pchLabelText, const char *pchToolTip);
MEFieldContextEntry* MEContextAddButtonIndex(const char *pchButtonText, const char *pchButtonIcon, UIActivationFunc clickedF, UserData clickedData, const char *pchUID, int index, const char *pchLabelText, const char *pchToolTip);
MEFieldContextEntry* MEContextAddButtonIndexMsg(const char *pchButtonText, const char *pchButtonIcon, UIActivationFunc clickedF, UserData clickedData, const char *pchUID, int index, const char *pchLabelText, const char *pchToolTip);
MEFieldContextEntry* MEContextAddCheck(UIActivationFunc clickedF, UserData clickedData, bool bCurrentState, const char *pchUID, const char *pchLabelText, const char *pchToolTip);
MEFieldContextEntry* MEContextAddSeparator(const char *pchUID);
MEFieldContextEntry* MEContextAddSeparatorIndex(const char *pchUID, int index);
MEFieldContextEntry* MEContextAddColoredSeparator( U32 color, const char* pchUID, int height );
MEFieldContextEntry* MEContextAddSprite(const char *pchTextureName, const char *pchUID, const char *pchLabelText, const char *pchToolTip);
MEFieldContextEntry* MEContextAddSpriteIndex(const char *pchTextureName, const char *pchUID, int index, const char *pchLabelText, const char *pchToolTip);
MEFieldContextEntry* MEContextAddCustom(const char *pchUID);
MEFieldContextEntry* MEContextAddCustomIndex(const char *pchUID, int index);

MEFieldContextEntry* MEContextAddSimple(MEFieldType eType, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddSimpleMsg(MEFieldType eType, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddIndex(MEFieldType eType, const char *pchFieldName, int iIndex, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddIndexMsg(MEFieldType eType, const char *pchFieldName, int iIndex, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddText(bool bIsMultiline, const char* strDefaultText, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddTextMsg(bool bIsMultiline, const char* strDefaultText, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddTextIndex(bool bIsMultiline, const char* strDefaultText, const char *pchFieldName, int index, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddTextIndexMsg(bool bIsMultiline, const char* strDefaultText, const char *pchFieldName, int index, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddMinMax(MEFieldType eType, F32 min, F32 max, F32 step, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddMinMaxIndex(MEFieldType eType, F32 min, F32 max, F32 step, const char *pchFieldName, int iIndex, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddDict(MEFieldType eType, const char *pchDictionary, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddDictIdx(MEFieldType eType, const char *pchDictionary, const char *pchFieldName, int iIndex, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddList(MEFieldType eType, const char ***ppchList, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddListIdx(MEFieldType eType, const char ***ppchList, const char *pchFieldName, int iIndex, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddExpr(ExprContext *pExprContext, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddPicker(const char *pchDictionary, const char *pchPickerName, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddEnum(MEFieldType eType, StaticDefineInt *pEnum, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddEnumIndex(MEFieldType eType, StaticDefineInt *pEnum, const char *pchFieldName, int iIndex, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddEnumMsg(MEFieldType eType, StaticDefineInt *pEnum, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddEnumIndexMsg(MEFieldType eType, StaticDefineInt *pEnum, const char *pchFieldName, int iIndex, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddDataProvided(MEFieldType eType, ParseTable *pComboParseTable, cUIModel peaComboModel, const char *pchComboField, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddDataProvidedIndex(MEFieldType eType, ParseTable *pComboParseTable, cUIModel peaComboModel, const char *pchComboField, const char *pchFieldName, int index, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddDataProvidedMsg(MEFieldType eType, ParseTable *pComboParseTable, cUIModel peaComboModel, const char *pchComboField, const char *pchFieldName, const char *pchDisplayName, const char *pchToolTip);
MEFieldContextEntry* MEContextAddDataProvidedIndexMsg(MEFieldType eType, ParseTable *pComboParseTable, cUIModel peaComboModel, const char *pchComboField, const char *pchFieldName, int index, const char *pchDisplayName, const char *pchToolTip);

MEFieldContextEntry *MEContextEntryAddActionButton(MEFieldContextEntry *pParentEntry, const char *pchButtonText, const char *pchButtonIcon, UIActivationFunc clickedF, UserData clickedData, int iSize, const char *pchToolTip);
MEFieldContextEntry *MEContextEntryAddActionButtonMsg(MEFieldContextEntry *pParentEntry, const char *pchButtonText, const char *pchButtonIcon, UIActivationFunc clickedF, UserData clickedData, int iSize, const char *pchToolTip);
MEFieldContextEntry *MEContextEntryAddIsSpecifiedCheck(MEFieldContextEntry *pParentEntry, UIActivationFunc clickedF, UserData clickedData, bool bCurrentlySpecified, const char *pchToolTip);
MEFieldContextEntry *MEContextEntryMakeHighlighted(MEFieldContextEntry *pParentEntry, int iRGBA);

MEFieldContextEntry *MEContextCreateWindowParent(const char *pchTitle, int iWidth, int iHeight, bool bModal, const char *pchUID);
MEFieldContextEntry *MEContextCreateScrollAreaParent(int iHeight, const char *pchUID);
MEFieldContextEntry *MEContextCreatePaneParent(int iHeight, const char *pchUID);

//UIWindow *MEContextPushWindowParent(const char *pchTitle, int iWidth, int iHeight, bool bModal, const char *pchUID);
UIScrollArea *MEContextPushScrollAreaParent(const char *pchUID);
UIPane *MEContextPushPaneParent(const char *pchUID);

void *MEContextAllocMem(const char *pchUID, size_t iSize, MEFieldContextAllocResetFn pfResetFunction, bool bResetNow);
void *MEContextAllocMemIndex(const char *pchUID, int index, size_t iSize, MEFieldContextAllocResetFn pfResetFunction, bool bResetNow);
void *MEContextAllocStruct(const char *pchUID, ParseTable *pParseTable, bool bResetNow);
void *MEContextAllocStructIndex(const char *pchUID, int index, ParseTable *pParseTable, bool bResetNow);

// Use these preferentially when modifying the return value of the above functions
#define ENTRY_LABEL(entry) (entry->pLabel)
#define ENTRY_LABEL2(entry) (entry->pLabel2)
#define ENTRY_BUTTON(entry) (entry->pButton)
#define ENTRY_SPRITE(entry) (entry->pSprite)
#define ENTRY_WIDGET(entry) (entry->pCustomWidget)
#define ENTRY_FIELD(entry) (entry->ppFields[0])
#define ENTRY_WINDOW(entry) (entry->pWindow)

