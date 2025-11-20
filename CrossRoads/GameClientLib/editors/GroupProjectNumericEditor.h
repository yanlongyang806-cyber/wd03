#pragma once
GCC_SYSTEM
//
// GroupProjectNumericEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the group project numeric editor and displays the main window
MEWindow *groupProjectNumericEditor_init(MultiEditEMDoc *pEditorDoc);

// Creates a group project numeric for editing
void groupProjectNumericEditor_createGroupProjectNumeric(char *pcName);

#endif