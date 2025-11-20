#pragma once
GCC_SYSTEM
//
// MissionSetEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the editor and displays the main window
MEWindow *missionSetEditor_init(MultiEditEMDoc *pEditorDoc);

// Creates a new object for editing
void missionSetEditor_createMissionSet(char *pcName);

#endif