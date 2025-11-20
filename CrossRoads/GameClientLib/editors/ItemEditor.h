#pragma once
GCC_SYSTEM
//
// ItemEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the item editor and displays the main window
MEWindow *itemEditor_init(MultiEditEMDoc *pEditorDoc);

// Create a item for editing
void itemEditor_createItem(char *pcName);

#endif