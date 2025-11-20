#pragma once
GCC_SYSTEM
//
// CritterOverrideEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the critter override power editor and displays the main window
MEWindow *CritterOverrideEditor_init(MultiEditEMDoc *pEditorDoc);

// Creates a critter override for editing
void CritterOverrideEditor_createCritterOverride(char *pcName);

#endif