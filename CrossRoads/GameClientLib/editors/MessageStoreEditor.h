#pragma once
GCC_SYSTEM
//
// MessageStoreEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the message store editor and displays the main window
MEWindow *messageStoreEditor_init(MultiEditEMDoc *pEditorDoc);

// Create a message for editing
void messageStoreEditor_createMessage(char *pcName);

#endif

