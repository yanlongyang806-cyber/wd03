#pragma once
GCC_SYSTEM
//
// PowersEditor.h
//


typedef struct EMSearchResult EMSearchResult;

#ifndef NO_EDITORS
typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

typedef struct PEGenerateItempowerWindow
{
	UIWindow *pWindow;
	//This window
	MEWindow *pMEWindow;
	//The multi edit window I'm operating on

	UITextEntry *pScopeEntry;
	UITextEntry *pPrefixEntry;
	UITextEntry *pSuffixEntry;
	UILabel *pFilenamePreview;

} PEGenerateItempowerWindow;

// Starts up the powers editor and displays the main window
MEWindow *powersEditor_init(MultiEditEMDoc *pEditorDoc);

// Create a power for editing
void powersEditor_createPower(char *pcName);

void pe_CopyToClipboard();

#endif

