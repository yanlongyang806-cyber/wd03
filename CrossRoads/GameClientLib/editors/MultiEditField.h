//
// MultiEditField.h
//

#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "textparser.h"
#include "UICore.h"

typedef struct ExprContext ExprContext;
typedef struct MEField MEField;
typedef enum MultiValType MultiValType;
typedef struct SingleFieldInheritanceData SingleFieldInheritanceData;
typedef struct UICheckButton UICheckButton;
typedef struct UIComboBox UIComboBox;
typedef struct UIExpander UIExpander;
typedef struct UIExpressionEntry UIExpressionEntry;
typedef struct UIFileNameEntry UIFileNameEntry;
typedef struct UIGameActionEditButton UIGameActionEditButton;
typedef struct UIGameEventEditButton UIGameEventEditButton;
typedef struct UIComboBox UIFlagComboBox;
typedef struct UILabel UILabel;
typedef struct UIMessageEntry UIMessageEntry;
typedef struct UISlider UISlider;
typedef struct UISliderTextEntry UISliderTextEntry;
typedef struct UIColorSlider UIColorSlider;
typedef struct UISpinnerEntry UISpinnerEntry;
typedef struct UIMultiSpinnerEntry UIMultiSpinnerEntry;
typedef struct UISprite UISprite;
typedef struct UITextArea UITextArea;
typedef struct UITextEntry UITextEntry;
typedef struct UITextureEntry UITextureEntry;
typedef struct UIColorButton UIColorButton;
typedef struct UIWindow UIWindow;
typedef struct UIButton UIButton;
typedef struct UIEditable UIEditable;

#endif

// The AUTO_ENUM needs to be present even if NO_EDITORS

AUTO_ENUM;
typedef enum MEFieldBoolean
{
	kBool_FALSE,
	kBool_TRUE
} MEFieldBoolean;
extern StaticDefineInt MEFieldBooleanEnum[];

#ifndef NO_EDITORS

typedef enum MEFieldType
{
	kMEFieldType_BooleanCombo,
	kMEFieldType_Check,
	kMEFieldType_Color,			// Unsupported, but planned
	kMEFieldType_Hue,			// Uses the UIColorSlider widget to provide a Hue shifter (i.e. a single F32 ranging from 0 to 360, inclusive. Default is 0)
	kMEFieldType_Combo,
	kMEFieldType_FileName,
	kMEFieldType_FlagCombo,
	kMEFieldType_DisplayMessage,
	kMEFieldType_Message,
	kMEFieldType_MultiText,  // Text entry with a text area pull-down
	kMEFieldType_SMFTextEntry, // Text entry with a smf preview pull-down
	kMEFieldType_Slider,
	kMEFieldType_SliderText,
	kMEFieldType_TextArea,
	kMEFieldType_TextEntry,
	kMEFieldType_Texture,
	kMEFieldType_MultiTexture,
	kMEFieldType_EMPicker, //Only supports References. Button that will open up a picker and set the text on the button to match your selection.
	kMEFieldType_ValidatedTextEntry, // Like TextEntry but if a dictionary is attached only lets you pick valid values
	//kMEFieldType_Radio,        // Commented out is unsupported, but planned
	kMEFieldType_Spinner,
	kMEFieldType_MultiSpinner,	// Currently only works with float arrays of size 2-4 inclusive
}MEFieldType;

typedef enum MEFieldColorType
{
	kMEFieldColorType_RGB,
	kMEFieldColorType_RGBA,
	kMEFieldColorType_HSV,
}MEFieldColorType;

extern MEFieldType kMEFieldTypeEx_Expression;
extern MEFieldType kMEFieldTypeEx_GameActionBlock;
extern MEFieldType kMEFieldTypeEx_GameEvent;

typedef bool (*MEFieldPreChangeCallback)(MEField *pField, bool bFinished, void *pUserData);
typedef void (*MEFieldChangeCallback)(MEField *pField, bool bFinished, void *pUserData);

typedef struct MEIntArrayStruct {
	int *eaInt;
} MEIntArrayStruct;

typedef struct MERefArrayStruct {
	 REF_TO(void) hRef;
} MERefArrayStruct;

typedef struct MEArrayWithStringStruct {
	char *pcString;
} MEArrayWithStringStruct;

// Wrapper for a field
typedef struct MEField
{
	// Old, new, and parent data
	// And parse table information for the this data
	void *pOld, *pNew, *pParent; // The actual data
	ParseTable *pTable;		     // Keep track of what table this is for
	const char *pchFieldName;		     // Keep track of the name of the field in the parse table
	int column;				     // So we know where to go looking for the data
	int arrayIndex;              // If underlying storage is an array
	int arraySize;				 // If we are editing all the items in the array
	const char* pchIndexSeparator;	 // If set, then this string separates indexes instead of space; only valid if field is an EArray

	// Data objects that contain the inheritance structure
	// And parse table information for this data
	const char *pchRootDictName; // Dictionary of root
	void *pRootOld, *pRootNew;   // The old and new data for inheritance checks
	ParseTable *pRootTable;      // Parse table to use in inheritance checks
	char **eaRootPaths;          // Paths to use in the inheritance checks

	// Field type information
	StructTypeField type;	// So we know what type it is
	bool bIsIntFlags; //this field's type is INT_X or one of the other different bit size ints, but it has TOK_FORMAT_FLAGS, so
		//it uses the old TOK_FLAGS_X code
	const char *pchDictName;
	const char *pchGlobalDictName;
	ParseTable *pDictTable;
	const char *pchDictField;  // Overloaded to hold file path for texture/file entry
	const char *pchDictDisplayField;  // optionally holds a different field to use for display
	const char *pchEMPickerName;
	int iDictCol;
	StaticDefineInt *pEnum; // The enum its using (if any)
	void *pExtensionData;

	// Edit information
	const char *pchLabel;

	// State flags
	bool bDirty;			// If this field has been changed from the original
	bool bEditable;
	bool bParented;			// True if this field is derived from the parent
	bool bNotParentable;    // True if this field cannot be parented
	bool bNotApplicable;
	bool bNotRevertable;    // True if this field cannot be reverted
	bool bNotGroupEditable;
	bool bIsMessage;

	// Action request flags
	bool bRevert;			// True if this field wants to be reverted
	bool bUpdate;			// True if this field should be updated to its new struct
	bool bParentUpdate;		// True if this field should be updated to its parent

	// Callbacks
	MEFieldPreChangeCallback	preChangedCallback;
	void *pPreChangeUserData;
	MEFieldChangeCallback	changedCallback;
	bool bSkipSiblingChangedCallbacks;
	void *pChangeUserData;

	// Multi-Edit
	MEField **eaSiblings;

	// Scale cache
	F32 fLastScale;

	// Scale value
	// Only used for F32 and Int variable types
	F32 fScale;

	// Data format of color
	// Only used for color button field types
	MEFieldColorType eColorType;

	// If true, don't sort the enum
	bool bDontSortEnums;

	// UI Widgets
	MEFieldType eType;
	union
	{
		UIWidget *pUIWidget;
		UICheckButton *pUICheck; 
		UIComboBox *pUICombo;	
		UIExpressionEntry *pUIExpression;
		UIFileNameEntry *pUIFileName; 
		UIFlagComboBox *pUIFlagBox;
		UIMessageEntry *pUIMessage;
		UISlider *pUISlider;
		UISliderTextEntry *pUISliderText;
		UIColorSlider *pUIColorSlider;
		UITextArea *pUITextArea;
		UIEditable *pUIEditable;
		UITextEntry *pUIText;	
		UITextureEntry *pUITexture; 
		UIColorButton *pUIColor; 
		UIButton *pUIButton;
		UISpinnerEntry *pUISpinner;
		UIMultiSpinnerEntry *pUIMultiSpinner;
		UIGameActionEditButton *pUIGameActionButton;
		UIGameEventEditButton *pUIGameEventButton;
	};
	UILabel *pUILabel;		
	cUIModel peaComboStrings; // For text entries
	cUIModel peaComboModel;
} MEField;


// This takes a ton of parameters, time for wrappers or multiple functions?
// Note: pcRootPath must be allocated since it will be freed when the field is destroyed
// Note: peaComboStrings applies to the text entry type only at this time
MEField *MEFieldCreate(MEFieldType eType,  
					   void *pOld, void *pNew, ParseTable *pTable, const char *pchField, void *pParent,
					   void *pRootOld, void *pRootNew, ParseTable *pRootTable, char *pcRootPath,
					   const char *pchDictName, ParseTable *pDictTable, const char *pchDictField,
					   const char *pchDictDisplayField, const char *pchGlobalDictName, const char *pchAlias, bool bLabel,
					   cUIModel peaComboStrings, cUIModel peaComboModel, StaticDefineInt *pEnum, void *pExtensionData,
					   int arrayIndex, int x, int y, int width, const char* pchIndexSeparator);

MEField *MEFieldCreateSimple(MEFieldType eType, void *pOld, void *pNew, ParseTable *pTable, const char *pchField);
MEField *MEFieldCreateSimpleEnum(MEFieldType eType, void *pOld, void *pNew, ParseTable *pTable, const char *pchField, StaticDefineInt *pEnum);
MEField *MEFieldCreateSimpleExpression(MEFieldType eType, void *pOld, void *pNew, ParseTable *pTable, const char *pchField, ExprContext *pExprContext);
MEField *MEFieldCreateSimpleDictionary(MEFieldType eType, void *pOld, void *pNew, ParseTable *pTable, const char *pchField, const char *pchDictName, ParseTable *pDictTable, const char *pchDictField);
MEField *MEFieldCreateSimpleGlobalDictionary(MEFieldType eType, void *pOld, void *pNew, ParseTable *pTable, const char *pchField, const char *pchDictName, const char *pchDictField);
MEField *MEFieldCreateSimpleDataProvided(MEFieldType eType, void *pOld, void *pNew, ParseTable *pTable, const char *pchField, ParseTable *pComboParseTable, cUIModel peaComboModel, const char *pchComboField);

void MEFieldAddAlternatePath(MEField *pField, char *pcAltPath);

void MEFieldDestroy(MEField *pField);

#define MEFieldSafeDestroy(ppField) { if (*ppField) { MEFieldDestroy(*ppField) ; *ppField = NULL; } }

void MEFieldAddToParent(MEField *pField, UIWidget *pParentWidget, F32 x, F32 y);
void MEFieldAddToParentPriority(MEField *pField, UIWidget *pParentWidget, F32 x, F32 y, U8 priority);
void MEFieldDisplay(MEField *pField, UIWidget *pParentWidget, CBox *pBox);
void MEFieldDismiss(MEField *pField, UIWidget *pParentWidget);

void MEFieldShowMenu(MEField *pField);
void MEFieldShowMultiFieldMenu(MEField **eaFields);

void MEDrawField(MEField *pField, UI_MY_ARGS, F32 z, Color *pBackgroundColor, UISkin *pSkin, bool bTinted);
void MEUpdateFieldDirty(MEField *pField, bool bDirty);
void MEUpdateFieldParented(MEField *pField, bool bParented);
void MEFieldSetParent(MEField *pField, void *pNewParent);
void MEFieldGetString(MEField *pField, char **estr);

void MEFieldRevert(MEField *pField);
void MEFieldRefreshFromData(MEField *pField);
void MEFieldSetAndRefreshFromData(MEField *pField, void *pOld, void *pNew);
void MEFieldRefreshFromWidget(MEField *pField);
void MEFieldValidateInheritance(MEField *pField);
void MEFieldSetEnum(MEField* pField, StaticDefineInt* pEnum);
void MEFieldSetComboModel(MEField* pField, cUIModel peaComboModel);

void MEFieldFixupNameString(MEField *pField, const char **ppchName);

UIWidget *MEFieldCreateEditWidget(MEField *pField);
void MEFieldCreateThisWidget(MEField *pField);

UIWindow *MEFieldOpenEditor(MEField *pField, char *pcTitle);
UIWindow *MEFieldOpenMultiEditor(MEField **eaFields, char *pcTitle);
void MEFieldCloseEditor(void);
bool MEFieldHasCompatibleTypes(MEField *pField1, MEField *pField2);

bool MEFieldCanOpenEMEditor(MEField *pField);
void MEFieldOpenEMEditor(MEField *pField);

void MEFieldOpenFile(MEField *pField);
void MEFieldOpenFolder(MEField *pField);

bool MEFieldCanPreview(MEField *pField);
void MEFieldPreview(MEField *pField);

bool MEFieldCanValueSearch(MEField *pField);
void MEFieldValueSearch(MEField *pField);


void MEFieldSetChangeCallbackEX(MEField *pField, MEFieldChangeCallback cbChange, void *pUserData, bool bSkipSiblingChangedCallbacks);
#define MEFieldSetChangeCallback(pField, cbChange, pUserData) MEFieldSetChangeCallbackEX(pField, cbChange, pUserData, false);
void MEFieldSetPreChangeCallback(MEField *pField, MEFieldPreChangeCallback cbPreChange, void *pUserData);

bool MEFieldGetParseTableInfo(ParseTable *pParseTable, int col, MEFieldType *pFieldType, 
							  char **ppDictName, ParseTable **ppDictParseTable, char **ppDictFieldName, char **ppGlobalDictName,
							  StaticDefineInt **ppEnum);

void MEFieldSetDictField(MEField *pField, const char *pchDictField, const char *pchDictDisplayField, bool bIsMessage);
void MEFieldSetRootDictName(MEField *pField, const char *pchDictField);

typedef void (*MEFieldDrawFunc)(MEField *pField, UI_MY_ARGS, F32 z, Color *pBackgroundColor, UISkin *pSkin);

typedef UIWidget* (*MEFieldCreateFunc)(MEField *pField, F32 x, F32 y, F32 w, F32 h);

typedef int (*MEFieldCompareFunc)(MEField *pField, void *pLeft, void *pRight);

typedef void (*MEFieldUpdateFunc)(UIWidget *pWidget, MEField *pField);

typedef void (*MEFieldGetStringFunc)(MEField *pField, char **estr);

// Returns the new MEFieldType value for this field type
int MEFieldRegisterFieldType(MEFieldCreateFunc cbCreate, MEFieldDrawFunc cbDraw, 
							 MEFieldCompareFunc cbCompare, MEFieldUpdateFunc cbUpdate,
							 MEFieldGetStringFunc cbGetString);

// Internal functions used by registered field types
void mef_checkIfDirty(MEField *pField, UIAnyWidget *pWidget);
void mef_displayMenu(UIAnyWidget *pWidget, MEField *pField);

// Option to disable context menus
void MEFieldSetDisableContextMenu(bool bDisable);

// Functions for convenient use of MEField using Expanders
void MEExpanderRefreshSimpleField(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, const char *pcFieldName, MEFieldType eType,
								  UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, 
								  MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData);
void MEExpanderRefreshTextSliderField(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, const char *pcFieldName, MEFieldType eType,
								  UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, F32 min, F32 max, F32 step,
								  int arrayIdx, MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData);
void MEExpanderRefreshColorField(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, const char *pcFieldName, MEFieldType eType,
								 UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, 
								 MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData, MEFieldColorType eColorType);
void MEExpanderRefreshEnumField(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, StaticDefineInt pEnum[],
								UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, 
								MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData);
void MEExpanderRefreshEnumFieldNoSort(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, StaticDefineInt pEnum[],
									  UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, 
									  MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData);
void MEExpanderRefreshFlagEnumField(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, StaticDefineInt pEnum[],
									UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, 
									MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData);
void MEExpanderRefreshGlobalDictionaryField(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, char *pcDict,
											UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight,
											MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData);
void MEExpanderRefreshGlobalDictionaryFieldEx(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, MEFieldType eType, char *pcDict,
											  UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight,
											  MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData);
void MEExpanderRefreshDataField(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, const char *pcFieldName, const void ***peaModel, bool bValidated,
								UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight,
								MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData);
void MEExpanderRefreshDataFieldEx(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, MEFieldType eType, 
								  const void ***peaModel, ParseTable *pModelPTI, const char* pModelKey, 
								  UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight,
								  MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData);
void MEExpanderRefreshLabel(UILabel **ppLabel, const char *pcText, const char *pcTooltip, F32 x, F32 xPercent, F32 y, UIWidget *pParent);
void MEExpanderRefreshSprite(UISprite **ppSprite, const char *pcTexture, F32 x, F32 y, F32 w, F32 h, UIWidget *pParent);
void MEExpanderRefreshButton(UIButton **ppButton, const char *pcText, UIActivationFunc fn, UserData cb,
							 F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, UIWidget* pParent );
void MEExpanderRefreshButtonImageOnly(UIButton **ppButton, const char *pcTexture, F32 x, F32 y, UIWidget* pParent, UIActivationFunc fn, UserData cb);
void MEExpanderAddFieldToParent(MEField *pField, UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, 
								MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData);


#endif
