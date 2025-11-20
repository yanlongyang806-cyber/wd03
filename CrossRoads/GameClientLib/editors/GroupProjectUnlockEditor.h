#pragma once
GCC_SYSTEM
//
// GroupProjectUnlockEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the group project unlock editor and displays the main window
MEWindow *groupProjectUnlockEditor_init(MultiEditEMDoc *pEditorDoc);

// Creates a group project unlock for editing
void groupProjectUnlockEditor_createGroupProjectUnlock(char *pcName);

#endif