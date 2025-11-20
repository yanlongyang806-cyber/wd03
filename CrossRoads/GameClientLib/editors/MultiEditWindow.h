//
// MultiEditWindow.h
//

#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "EditorManager.h"
#include "UIFilteredList.h"
#include "UITextEntry.h"
#include "UIMenu.h"
#include "UIWindow.h"

struct MEWindow;
typedef struct METable METable;
typedef struct MultiEditEMDoc MultiEditEMDoc;
typedef struct CSVColumn CSVColumn;
typedef struct CSVConfig CSVConfig;

typedef void *(*MECreateFunc)(struct MEWindow *pWindow, void *pObjectToClone);

typedef struct MEColumnSetInfo {
	struct MEWindow *pWindow;
	int index;
	char *pcName;
	char **eaColGroups;
	UIComboBox *pCombo;
} MEColumnSetInfo;

typedef struct MEWindow {
	MultiEditEMDoc *pEditorDoc;

	// The underlying table object
	METable *pTable;
	char *pcDisplayName;
	char *pcDisplayNamePlural;

	// The main window
	UIWindow *pUIWindow;

	// Menu information
	int *eaiSubTableIds;
	MEColumnSetInfo **eaColumnInfos;

	// Metadata about the objects being edited
	ParseTable *pParseTable;
	DictionaryHandle hDict;
	char *pcNamePTName;
	int iNameIndex;

	// Callbacks
	MECreateFunc cbCreate;
} MEWindow;

//////////////////////////////////////////////////////////////////////////
// CSV Export Stuff
//////////////////////////////////////////////////////////////////////////

//An enum describing which referents to export
AUTO_ENUM;
typedef enum CSVExportType
{
	kCSVExport_Open,
	//Export the powers open in the editor

	kCSVExport_Group,
	//Export a specific group of powers

	kCSVExport_COUNT,							EIGNORE
	//The count of export types, used in loops
} CSVExportType;

//An enum describing the columns to be exported
AUTO_ENUM;
typedef enum ColumnsExport
{
	kColumns_All,
	//Export all the columns
	kColumns_Selected,
	//Export columns that are fully selected
	kColumns_Visible,
	//Export the columns that are not hidden
	kColumns_COUNT,								EIGNORE
		//Used while looping over the columns
} ColumnsExport;

typedef void (*OkayClickedFunc) (CSVConfig *pConfig);

typedef struct CSVConfigWindow
{
	UIWindow *pWindow;
		//This window
	MEWindow *pMEWindow;
		//The multi edit window I'm operating on

	ColumnsExport eDefaultExportColumns;
		//Which columns export to export is default
	CSVExportType eDefaultExportType;
		//What type of export is default

	char *pchBaseFilename;
		//The name of the base file.  Defaults to "CSVExport"
	
	UIFileNameEntry *pFileEntry;
		// The file entry for the output of the csv export
	
	UIComboBox *pGroupCombo;
		//The group you'd like to export

	char ***peaModel;
		// The model for the group combo
	
	UIRadioButtonGroup *ExportTypeGroup;
		//The radio button group that has the export types
	
	UIRadioButtonGroup *ColumnsGroup;
		// The radio button group that has the columns you want to export
	
	OkayClickedFunc okayClickedf;
		// The function that gets executed when okay is clicked.  Do any specifics that need to get setup per csv exporter here.
} CSVConfigWindow;

// After clicking okay, this sets up the other exporting info before calling the server command to export things
void csvExportSetup(MEWindow *pWindow, char ***peaPowerDefList, CSVColumn ***peaColumns, CSVExportType eExportType, ColumnsExport eColumns);

// Initializes all the UI elements, needs to be done once before any exporting
void initCSVConfigWindow(CSVConfigWindow *pCSVWindow);

// Does the per-export setup
void setupCSVConfigWindow(CSVConfigWindow *pCSVWindow, MEWindow *pMEWindow, OkayClickedFunc okayClickedFunc);

//////////////////////////////////////////////////////////////////////////

MEWindow *MEWindowCreate(char *pcWindowTitle, char *pcDisplayName, char *pcDisplayNamePlural, char *pcTypeName,
						 DictionaryHandle hDict, 
						 ParseTable *pParseTable, char *pcNamePTName, char *pcFilePTName,
						 char *pcDefaultFilePath, MultiEditEMDoc *pEditorDoc);
void MEWindowDestroy(MEWindow *pWindow);

void MEWindowExit(MEWindow *pWindow);

void MEWindowLostFocus(MEWindow *pWindow);

UIWindow *MEWindowGetUIWindow(MEWindow *pWindow);

void MEWindowOpenObject(MEWindow *pWindow, char *pcObjName);

EMTaskStatus MEWindowSaveObject(MEWindow *pWindow, void *pObject);

EMTaskStatus MEWindowSaveAll(MEWindow *pWindow);

void MEWindowCloseObject(MEWindow *pWindow, void *pObject);

void MEWindowRevertObject(MEWindow *pWindow, void *pObject);

void MEWindowSetCreateCallback(MEWindow *pWindow, MECreateFunc cbCreate);

void MEWindowSavePrefs(MEWindow *pWindow);

// Call this after adding columns and subtables to the table to create the column menus
void MEWindowInitTableMenus(MEWindow *pWindow);

#endif