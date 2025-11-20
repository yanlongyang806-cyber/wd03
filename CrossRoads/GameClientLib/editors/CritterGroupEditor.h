#pragma once
GCC_SYSTEM
//
// CritterGroupEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the item editor and displays the main window
MEWindow *crittergroupEditor_init(MultiEditEMDoc *pEditorDoc);

// Create a item for editing
void crittergroupEditor_createGroup(char *pcName);

#endif