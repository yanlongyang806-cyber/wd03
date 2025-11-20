#pragma once
GCC_SYSTEM
//
// MicroTransactionEditor.h
//


typedef struct EMSearchResult EMSearchResult;

#ifndef NO_EDITORS
typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the powers editor and displays the main window
MEWindow *MicroTransEditor_init(MultiEditEMDoc *pEditorDoc);

// Create a power for editing
void MicroTransEditor_createMT(char *pcName);

void mte_CopyToClipboard();

#endif

