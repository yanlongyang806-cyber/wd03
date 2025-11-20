#pragma once
GCC_SYSTEM
//
// PlayerStatsEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the editor and displays the main window
MEWindow *playerStatsEditor_init(MultiEditEMDoc *pEditorDoc);

// Creates a new object for editing
void playerStatsEditor_createPlayerStat(char *pcName);

#endif