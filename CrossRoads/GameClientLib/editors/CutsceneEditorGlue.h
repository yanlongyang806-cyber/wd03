#ifndef __CUTSCENE_EDITOR_GLUE_H__
#define __CUTSCENE_EDITOR_GLUE_H__
GCC_SYSTEM

#ifndef NO_EDITORS

#include "CutsceneEditorCommon.h"

#endif // NO_EDITORS

/*
 * Acquires the currently active CutsceneEditorState. Mainly used by AUTO_COMMANDs (e.g. CutsceneEditor.FocusCamera) to acquire its context.
 *
 * If in the designer's CutsceneEditor, returns the state for the currently active cutscene doc.
 *
 * If in the demo record CutsceneDemoPlayEditor, returns the editor state for the current demo being played.
 */
extern CutsceneEditorState *cutEdCutsceneEditorState();

/*
 * These functions know how to behave depending on whether the state represents a designer cutscene or a demo record cutscene
 */
extern bool cutEdIsEditable(CutsceneEditorState *pState);
extern void cutEdSetUnsaved(CutsceneEditorState *pState);
extern bool cutEdIsUnsaved(CutsceneEditorState *pState);

#endif // __CUTSCENE_EDITOR_GLUE_H__
