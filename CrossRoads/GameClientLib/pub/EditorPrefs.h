#pragma once
GCC_SYSTEM
//
// EditorPrefs.h
//


typedef struct UIWindow UIWindow;

// ---- Editor preferences access ---------------------------------------------------------

// These variations all act on the editor preference set
// They initialize that preference set if required

// Get the Id of the editor prefs
int EditorPrefGetPrefSet(void);

// Get/set operations on preferences
const char *EditorPrefGetString(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, const char *pcDefault);
int EditorPrefGetInt(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, int iDefault);
F32 EditorPrefGetFloat(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, F32 fDefault);
int EditorPrefGetPosition(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, F32 *pX, F32 *pY, F32 *pW, F32 *pH);
void EditorPrefGetWindowPosition(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, UIWindow *pWindow);
void EditorPrefGetWindowPositionIgnoreDimensions(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, UIWindow *pWindow);

void EditorPrefStoreString(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, const char *pcValue);
void EditorPrefStoreInt(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, int iValue);
void EditorPrefStoreFloat(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, F32 fValue);
void EditorPrefStorePosition(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, F32 x, F32 y, F32 w, F32 h);
void EditorPrefStoreWindowPosition(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, UIWindow *pWindow);

// Text parser get/set operations
void EditorPrefGetStruct(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, ParseTable *pParseTable, void *pStruct);
void EditorPrefStoreStruct(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, ParseTable *pParseTable, void *pStruct);

// General use operations
bool EditorPrefIsSet(const char *pcEditorName, const char *pcCategory, const char *pcPrefName);
void EditorPrefClear(const char *pcEditorName, const char *pcCategory, const char *pcPrefName);

