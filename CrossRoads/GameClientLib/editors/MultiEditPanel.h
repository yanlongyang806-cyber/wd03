//
// MultiEditPanel.h
//

#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "MultiEditCommon.h"
#include "MultiEditField.h"

typedef struct MEPanel MEPanel;

typedef void (*MEPanelChangeFunc)(MEPanel *pPanel, void *pObject, void *pUserData, bool bInitNotify);

typedef char** (*MEPanelDataFunc)(struct MEPanel *pPanel);

typedef struct MEPanelEntry
{
	// The basic definition of the entry
	char *pcEntryLabel;
	char *pcEntryPTName;
	MEFieldType eType;
	int flags;

	// If it's an enum, then this is set
	StaticDefineInt *pEnum;

	// If it's an expression, then this is set
	ExprContext *pExprContext;

	// If it's a file entry, then this is set
	MEFileBrowseData *pFileData;

	// If it's a dictionary, then these are set
	const char *pDictName;
	ParseTable *pDictParseTable;
	char *pcDictNamePTName;

	// If it's a global dictionary, then this is set
	const char *pGlobalDictName;

	// If it takes a callback function for data, then this is set
	MEPanelDataFunc cbDataFunc;

	// The change callback registry
	MEPanelChangeFunc cbChange;
	void *pChangeUserData;
} MEPanelEntry;

typedef struct MEPanelDataEntry
{
	MEField *pField;
	bool bVisible;
} MEPanelDataEntry;

typedef struct MEPanel
{
	UIWidget *pParentWidget;
	MEPanelEntry **eaEntries;
	F32 fLabelWidth;

	// The actual data
	void *pOldData;
	void *pNewData;
} MEPanel;

// Create a multi-edit panel
MEPanel *MEPanelCreate(ParseTable *pParseTable, UIWidget *pParentWidget);

// Release memory for the multi-edit panel
void MEPanelDestroy(MEPanel *pPanel);

// Get the multi-edit panel's widget
UIWidget* MEPanelGetWidget(MEPanel *pPanel);

// Set the width of the label area.  Use -1 for auto.
void MEPanelSetLabelWidth(MEPanel *pPanel, int fWidth);

// Define an entry for the panel
void MEPanelAddEntry(MEPanel *pPanel, char *pcEntryLabel, char *pcEntryPTName, 
					  MEFieldType eType, StaticDefineInt *pEnum, ExprContext *pExprContext,
					  const char *pDictName, ParseTable *pDictParseTable, char *pcDictNamePTName, const char *pGlobalDictName,
					  MEPanelDataFunc cbDataFunc);
void MEPanelAddSimpleEntry(MEPanel *pPanel, char *pcEntryLabel, char *pcEntryPTName, MEFieldType eType);
void MEPanelAddDictEntry(MEPanel *pPanel, char *pcEntryLabel, char *pcEntryPTName, 
						  MEFieldType eType, const char *pDictName, ParseTable *pDictParseTable, char *pcDictNamePTName);
void MEPanelAddGlobalDictEntry(MEPanel *pPanel, char *pcEntryLabel, char *pcEntryPTName, 
						  MEFieldType eType, const char *pDictName, char *pcDictNamePTName);
void MEPanelAddEnumEntry(MEPanel *pPanel, char *pcEntryLabel, char *pcEntryPTName, MEFieldType eType, StaticDefineInt *pEnum);
void MEPanelAddExprEntry(MEPanel *pPanel, char *pcEntryLabel, char *pcEntryPTName, ExprContext *pExprContext);
void MEPanelAddFileNameEntry(MEPanel *pPanel, char *pcEntryLabel, char *pcEntryPTName, 
							 char *pcBrowseTitle, char *pcTopDir, char *pcStartDir, char *pcExtension, UIBrowserMode eMode);
void MEPanelAddScopeEntry(MEPanel *pPanel, char *pcEntryLabel, char *pcEntryPTName, MEFieldType eType);

void MEPanelFinishEntries(MEPanel *pPanel);

// Set the state of an entry
void MEPanelSetEntryState(MEPanel *pPanel, char *pcEntryName, int columnStateFlags);

// Set the data for the panel
void MEPanelSetData(MEPanel *pPanel, void *pOldData, void *pNewData);

// Revert all rows
void MEPanelRevertAll(MEPanel *pPanel);

// Refresh the panel from the data
void MEPanelRefresh(MEPanel *pPanel);

// Toggle an entry's "not applicable" state
void MEPanelSetEntryNotApplicable(MEPanel *pPanel, char *pcEntryName, bool bNotApplicable);

// Sets the callback for changes to an entry
void MEPanelSetEntryChangeCallback(MEPanel *pPanel, char *pcEntryName, MEPanelChangeFunc cbChange, void *pUserData);

#endif