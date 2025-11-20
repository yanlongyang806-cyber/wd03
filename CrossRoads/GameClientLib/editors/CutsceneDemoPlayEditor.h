#ifndef __CUTSCENE_DEMO_PLAY_EDITOR_H__
#define __CUTSCENE_DEMO_PLAY_EDITOR_H__
GCC_SYSTEM

#ifndef NO_EDITORS

#include "CutsceneEditorCommon.h"

typedef struct CutsceneDemoPlayEditor
{
	UIPane *pMainPane;
	CutsceneEditorState state;
} CutsceneDemoPlayEditor;

#endif // NO_EDITORS

extern void cutEdOpenWindow(bool dontShow);
extern void cutEdCloseWindow();
extern CutsceneDemoPlayEditor *cutEdDemoPlayEditor();
extern CutsceneEditorState* cutEdDemoPlaybackState();

#endif // __CUTSCENE_DEMO_PLAY_EDITOR_H__
