#pragma once
GCC_SYSTEM
//
// GroupProjectEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the group project editor and displays the main window
MEWindow *groupProjectEditor_init(MultiEditEMDoc *pEditorDoc);

// Creates a group project for editing
void groupProjectEditor_createGroupProject(char *pcName);

#endif