#pragma once
GCC_SYSTEM
//
// DonationTaskEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

// Starts up the donation task editor and displays the main window
MEWindow *donationTaskEditor_init(MultiEditEMDoc *pEditorDoc);

// Creates a donation task for editing
void donationTaskEditor_createDonationTask(char *pcName);

#endif