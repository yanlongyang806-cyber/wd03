#pragma once
GCC_SYSTEM
//
// QueueEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the item power editor and displays the main window
MEWindow *queueEditor_init(MultiEditEMDoc *pEditorDoc);

// Creates a store for editing
void queueEditor_createQueue(char *pcName);

#endif