//
// EditorPrefs.c
//

#include "file.h"
#include "Prefs.h"
#include "UIWindow.h"

//---------------------------------------------------------------------------------------------------
// Global Data
//---------------------------------------------------------------------------------------------------

static int iEditorPrefSet = -1;


//---------------------------------------------------------------------------------------------------
// Editor Access Functions
//---------------------------------------------------------------------------------------------------

void EditorPrefFormatName(char buf[], const char *pcEditorName, const char *pcCategory, const char *pcPrefName)
{
	if (pcEditorName && pcCategory) {
		quick_sprintf(buf, 260, "%s.%s.%s", pcEditorName, pcCategory, pcPrefName);
	} else 
	if (pcEditorName) {
		quick_sprintf(buf, 260, "%s.%s", pcEditorName, pcPrefName);
	} else {
		strcpy_s(buf, 260, pcPrefName);
	}
}

int EditorPrefGetPrefSet(void)
{
	if (iEditorPrefSet < 0) {
		// Initialize the pref set
		char buf[260];
		sprintf(buf, "%s/editor/editorprefs.pref", fileLocalDataDir());
		iEditorPrefSet = PrefSetGet(buf);
	}
	return iEditorPrefSet;
}

const char *EditorPrefGetString(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, const char *pcDefault)
{
	char buf[260];
	EditorPrefFormatName(buf, pcEditorName, pcCategory, pcPrefName);
	return PrefGetString(EditorPrefGetPrefSet(), buf, pcDefault);
}

int EditorPrefGetInt(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, int iDefault)
{
	char buf[260];
	EditorPrefFormatName(buf, pcEditorName, pcCategory, pcPrefName);
	return PrefGetInt(EditorPrefGetPrefSet(), buf, iDefault);
}

F32 EditorPrefGetFloat(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, F32 fDefault)
{
	char buf[260];
	EditorPrefFormatName(buf, pcEditorName, pcCategory, pcPrefName);
	return PrefGetFloat(EditorPrefGetPrefSet(), buf, fDefault);
}

int EditorPrefGetPosition(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, F32 *pX, F32 *pY, F32 *pW, F32 *pH)
{
	char buf[260];
	EditorPrefFormatName(buf, pcEditorName, pcCategory, pcPrefName);
	return PrefGetPosition(EditorPrefGetPrefSet(), buf, pX, pY, pW, pH);
}

void EditorPrefGetWindowPosition(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, UIWindow *pWindow)
{
	char buf[260];
	EditorPrefFormatName(buf, pcEditorName, pcCategory, pcPrefName);
	if (PrefIsSet(EditorPrefGetPrefSet(), buf)) {
		F32 x, y, w, h;
		if (PrefGetPosition(EditorPrefGetPrefSet(), buf, &x, &y, &w, &h)) {
			ui_WidgetSetPosition(UI_WIDGET(pWindow), x, y);
			ui_WidgetSetDimensions(UI_WIDGET(pWindow), w, h);
		}
	}
}

void EditorPrefGetWindowPositionIgnoreDimensions(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, UIWindow *pWindow)
{
	char buf[260];
	EditorPrefFormatName(buf, pcEditorName, pcCategory, pcPrefName);
	if (PrefIsSet(EditorPrefGetPrefSet(), buf)) 
	{
		F32 x, y, w, h;
		if (PrefGetPosition(EditorPrefGetPrefSet(), buf, &x, &y, &w, &h)) 
		{
			ui_WidgetSetPosition(UI_WIDGET(pWindow), x, y);
		}
	}
}

void EditorPrefStoreString(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, const char *pcValue)
{
	char buf[260];
	EditorPrefFormatName(buf, pcEditorName, pcCategory, pcPrefName);
	PrefStoreString(EditorPrefGetPrefSet(), buf, pcValue);
}

void EditorPrefStoreInt(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, int iValue)
{
	char buf[260];
	EditorPrefFormatName(buf, pcEditorName, pcCategory, pcPrefName);
	PrefStoreInt(EditorPrefGetPrefSet(), buf, iValue);
}

void EditorPrefStoreFloat(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, F32 fValue)
{
	char buf[260];
	EditorPrefFormatName(buf, pcEditorName, pcCategory, pcPrefName);
	PrefStoreFloat(EditorPrefGetPrefSet(), buf, fValue);
}

void EditorPrefStorePosition(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, F32 x, F32 y, F32 w, F32 h)
{
	char buf[260];
	EditorPrefFormatName(buf, pcEditorName, pcCategory, pcPrefName);
	PrefStorePosition(EditorPrefGetPrefSet(), buf, x, y, w, h);
}

void EditorPrefStoreWindowPosition(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, UIWindow *pWindow)
{
	char buf[260];
	EditorPrefFormatName(buf, pcEditorName, pcCategory, pcPrefName);
	PrefStorePosition(EditorPrefGetPrefSet(), buf, pWindow->widget.x, pWindow->widget.y, pWindow->widget.width, pWindow->widget.height);
}

void EditorPrefGetStruct(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, ParseTable *pParseTable, void *pStruct)
{
	char buf[260];
	EditorPrefFormatName(buf, pcEditorName, pcCategory, pcPrefName);
	PrefGetStruct(EditorPrefGetPrefSet(), buf, pParseTable, pStruct);
}

void EditorPrefStoreStruct(const char *pcEditorName, const char *pcCategory, const char *pcPrefName, ParseTable *pParseTable, void *pStruct)
{
	char buf[260];
	EditorPrefFormatName(buf, pcEditorName, pcCategory, pcPrefName);
	PrefStoreStruct(EditorPrefGetPrefSet(), buf, pParseTable, pStruct);
}

bool EditorPrefIsSet(const char *pcEditorName, const char *pcCategory, const char *pcPrefName)
{
	char buf[260];
	EditorPrefFormatName(buf, pcEditorName, pcCategory, pcPrefName);
	return PrefIsSet(EditorPrefGetPrefSet(), buf);
}

void EditorPrefClear(const char *pcEditorName, const char *pcCategory, const char *pcPrefName)
{
	char buf[260];
	EditorPrefFormatName(buf, pcEditorName, pcCategory, pcPrefName);
	PrefClear(EditorPrefGetPrefSet(), buf);
}
