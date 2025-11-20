#pragma once
GCC_SYSTEM
//
// StoreEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the item power editor and displays the main window
MEWindow *storeEditor_init(MultiEditEMDoc *pEditorDoc);

// Creates a store for editing
void storeEditor_createStore(char *pcName);

#endif