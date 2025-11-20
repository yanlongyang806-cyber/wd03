#pragma once
GCC_SYSTEM
//
// CritterEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the critter editor and displays the main window
MEWindow *critterEditor_init(MultiEditEMDoc *pEditorDoc);

// Creates a new critter and opens it for editing
void critterEditor_createCritter(char *pcName);

#endif