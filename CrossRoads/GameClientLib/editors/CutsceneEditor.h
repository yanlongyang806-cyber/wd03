#ifndef __CUTSCENE_EDITOR_H__
#define __CUTSCENE_EDITOR_H__
GCC_SYSTEM

#ifndef NO_EDITORS

#include "CutsceneEditorCommon.h"

typedef struct CutsceneEditorDoc 
{
	EMEditorDoc emDoc; // This must be first for EDITOR MANAGER
	CutsceneEditorState state;
	U32 newDoc : 1;
	U32 savingAs : 1;
} CutsceneEditorDoc;

#endif // NO_EDITORS

#endif // __CUTSCENE_EDITOR_H__
