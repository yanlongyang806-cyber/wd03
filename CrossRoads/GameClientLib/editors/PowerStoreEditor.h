#pragma once
GCC_SYSTEM
//
// PowerStoreEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the item power editor and displays the main window
MEWindow *powerStoreEditor_init(MultiEditEMDoc *pEditorDoc);

// Creates a power store for editing
void powerStoreEditor_createPowerStore(char *pcName);

#endif