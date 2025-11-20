#ifndef __WORLDEDITORATTRIBUTES_H__
#define __WORLDEDITORATTRIBUTES_H__
GCC_SYSTEM

#ifndef NO_EDITORS

#include "StashTable.h"

typedef struct DisplayMessage DisplayMessage;
typedef struct EMEditorDoc EMEditorDoc;
typedef struct EMPanel EMPanel;
typedef struct EditorObject EditorObject;
typedef struct EditorObjectType EditorObjectType;
typedef struct GroupChild GroupChild;
typedef struct GroupDef GroupDef;
typedef struct GroupProperties GroupProperties;
typedef struct GroupTracker GroupTracker;
typedef struct LogicalGroup LogicalGroup;
typedef struct MEField MEField;
typedef struct MEFieldContext MEFieldContext;
typedef struct Model Model;
typedef struct UIButton UIButton;
typedef struct UICheckButton UICheckButton;
typedef struct UIColorButton UIColorButton;
typedef struct UIList UIList;
typedef struct UISprite UISprite;
typedef struct UITab UITab;
typedef struct UIWindow UIWindow;
typedef struct WleAEPanel WleAEPanel;
typedef struct WleAEPasteData WleAEPasteData;
typedef struct WorldActorProperties WorldActorProperties;

// panel functions
typedef void (*WleAEPanelCreateCallback)(EMPanel*);
typedef int (*WleAEPanelCallback)(EMPanel*, EditorObject*);
typedef void (*WleAEPanelActionCallback)(void *, void*);
WleAEPanel* wleAERegisterPanel(char *name, WleAEPanelCallback reloadFunc, WleAEPanelCreateCallback createFunc, 
	WleAEPanelActionCallback addFunc, WleAEPanelActionCallback removeFunc, 
	const char *field_name, ParseTable *pti, EditorObjectType **types);
void wleAEGenericCreate(EMPanel *panel);

// attribute editor main functions
bool wleAEGetPanelsForType(EditorObjectType *type, WleAEPanel ***panels);
void wleAESetApplyingData(bool applying_data);
void wleAESetActive(EditorObject *edObj);
void wleAESetActiveQueued(EditorObject *edObj);
void wleAERefresh(void);
void wleAECreate(EMEditorDoc *doc);
void wleAEOncePerFrame(void);

#define WL_VAL_DIFF  -2
#define WL_VAL_UNSET -1
#define WL_VAL_FALSE  0
#define WL_VAL_TRUE	  1
void wleAEWLVALSetFromBool(int *iCurrentVal, bool bNewVal);

#define WL_INHERITED_BG_COLOR 0x00000022

typedef enum WleAESelectedDataFlags
{
	WleAESelectedDataFlags_Inactive		= (1<<0),
	WleAESelectedDataFlags_Model		= (1<<1),
	WleAESelectedDataFlags_ObjLib		= (1<<2),
	WleAESelectedDataFlags_Failed		= (1<<3),
	WleAESelectedDataFlags_SomeMissing	= (1<<4),
} WleAESelectedDataFlags;

typedef enum WleUIPanelValid
{
	WLE_UI_PANEL_INVALID = 0,		//This panel is not allowed to be displayed for this data
	WLE_UI_PANEL_UNOWNED,			//This data can be shown but the properties are not present
	WLE_UI_PANEL_OWNED,				//This data can be shown and the properties are present
} WleUIPanelValid;

typedef bool (*WleAESelectedExcludeFunc)(GroupTracker *pTracker);
void** wleAEGetSelectedDataFromPath(const char *pchPath, WleAESelectedExcludeFunc cbExclude, U32 *iRetFlags);
GroupChild* wleAEGetSingleSelectedGroupChild( bool prompt, bool* out_isEditable );
GroupDef* wleAEGetSingleSelectedGroupDef( bool prompt, bool* out_isEditable );

typedef void (*wleAEFieldChangedExtraCallback)(MEField *pField, GroupTracker *pTracker, const void *pOldProps, void *pNewProps);
void wleAEAddFieldChangedCallback(MEFieldContext *pContext, wleAEFieldChangedExtraCallback cbExtra);
void wleAECallFieldChangedCallback(MEField *pField, wleAEFieldChangedExtraCallback cbExtra);

// The following functions are like the above ones, but for operating
// on the selected tracker's GroupChild in the tracker's parent.
void wleAEAddGroupChildFieldChangedCallback(MEFieldContext *pContext, wleAEFieldChangedExtraCallback cbExtra);
void wleAECallGroupChildFieldChangedCallback(MEField *pField, wleAEFieldChangedExtraCallback cbExtra);


typedef struct WleAESelectionCBData
{
	GroupTracker *pTracker;
	LogicalGroup *pGroup;
	WorldActorProperties *pActor;
} WleAESelectionCBData;
typedef void (*wleAEApplyToSelectionCallback)(EditorObject *pObject, WleAESelectionCBData *pData, UserData pUserData, UserData pUserData2);
void wleAEApplyToSelection(wleAEApplyToSelectionCallback cbCallback, UserData pUserData, UserData pUserData2);


typedef struct WleAEPropStructData
{
	const char *pchPath;
	ParseTable *pTable;
} WleAEPropStructData;

void wleAEMessageMakeEditorCopy(DisplayMessage *pMessage, const char *pchKey, const char *pchScope, const char *pchDescription);
void wleAEAddPropsToSelection(void *pUnused, WleAEPropStructData *pPropData);
void wleAERemovePropsToSelection(void *pUnused, WleAEPropStructData *pPropData);


// attribute editor attribute copying
typedef struct WleAEAttributeBuffer 
{
	WleAEPasteData **pasteData;
} WleAEAttributeBuffer;

typedef WleAEPasteData *(*WleAECopyCallback)(const EditorObject *object, void *copyData);
typedef void (*WleAEPasteCallback)(const EditorObject **objects, void *pasteData);
typedef void (*WleAECopyPasteFreeCallback)(void *data);

WleAEPasteData *wleAEPasteDataCreate(UserData pasteData, WleAEPasteCallback pasteFunc, WleAECopyPasteFreeCallback freeFunc);
UIButton *wleAECopyButtonCreate(WleAECopyCallback copyFunc, void *copyData, WleAECopyPasteFreeCallback freeFunc);
bool wleAECopyAttributes(void);
void wleAEPasteAttributes(WleAEAttributeBuffer *buffer);

// accessors
EditorObject *wleAEGetSelected(void);

#endif // NO_EDITORS

#endif // __WORLDEDITORATTRIBUTES_H__
