//// An editor for Choice Tables.
////
//// This uses the METable to create a spreadsheet editor for Choice
//// Tables.
#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "EditorManager.h"

typedef struct ChoiceEd_Doc ChoiceEd_Doc;
typedef struct ChoiceEd_EntryGroup ChoiceEd_EntryGroup;
typedef struct ChoiceTable ChoiceTable;
typedef struct ChoiceTableValueDef ChoiceTableValueDef;
typedef struct MEField MEField;
typedef struct UIGimmeButton UIGimmeButton;
typedef struct UIScrollArea UIScrollArea;

#define CHOICE_TABLE_EDITOR "Choice Table Editor"
#define DEFAULT_CHOICE_TABLE_NAME "New_Choice_Table"

typedef struct ChoiceEd_DefGroup
{
	ChoiceEd_Doc* pDoc;

	// Infrastructure
	int index;
	UIExpander* pExpander;
	UIButton* pDelButton;
	UIButton* pUpButton;
	UIButton* pDownButton;

	// Editable fields
	UILabel* pNameLabel;
	MEField* pNameField;

	UILabel* pTypeLabel;
	MEField* pTypeField;
} ChoiceEd_DefGroup;

typedef struct ChoiceEd_ValueGroup
{
	ChoiceEd_EntryGroup* pEntry;

	// Infrastructure
	int index;
	UIPane* pPane;

	// Editable fields
	MEField* pTypeField;

	MEField* pIntValueField;
	MEField* pFloatValueField;
	MEField* pStringValueField;
	MEField* pMessageValueField;
	MEField* pAnimationValueField;
	MEField* pCritterDefValueField;
	MEField* pCritterGroupValueField;
	UILabel* pZoneMapValueLabel;
	MEField* pZoneMapValueField;
	UILabel* pSpawnPointValueLabel;
	MEField* pSpawnPointValueField;
	MEField* pItemDefValueField;
	MEField* pMissionDefValueField;

	UILabel* pChoiceTableLabel;
	MEField* pChoiceTableField;

	UILabel* pChoiceNameLabel;
	MEField* pChoiceNameField;

	UILabel* pChoiceIndexLabel;
	MEField* pChoiceIndexField;
	ChoiceTableValueDef** eaChoiceTableValueDefs;
} ChoiceEd_ValueGroup;

typedef struct ChoiceEd_EntryGroup
{
	ChoiceEd_Doc* pDoc;

	// Infrastructure
	int index;
	UIPane* pPane;
	UIMenuButton* pPopupMenuButton;
	//UIButton* pDelButton;

	// Editable fields
	UILabel* pNameLabel;
	
	MEField* pWeightField;
	MEField* pTypeField;

	UILabel* pChoiceTableLabel;
	MEField* pChoiceTableField;

	ChoiceEd_ValueGroup** eaValueGroups;
} ChoiceEd_EntryGroup;

typedef struct ChoiceEd_Doc
{
	EMEditorDoc emDoc;

	ChoiceTable *pChoiceTable;
	ChoiceTable *pOrigChoiceTable;
	ChoiceTable *pNextUndoChoiceTable;

	bool bIgnoreFieldChanges;
	bool bIgnoreFilenameChanges;

	// Main window controls
	UIWindow *pMainWindow;
	UILabel *pFilenameLabel;
	UIGimmeButton *pFileButton;
	UIExpanderGroup *pChoiceValueDefExpGroup;
	UIPane* pChoiceEntryPane;
	UIScrollArea *pChoiceEntryScrollArea;
	UIWidget **eaTimedRandomWidgets;

	UILabel *pValueDefinitionsLabel;
	UILabel *pChoicesLabel;

	// Simple fields
	MEField **eaDocFields;

	// Entry header
	UILabel* pHeaderTypeLabel;
	UILabel* pHeaderWeightLabel;
	UILabel** eaHeaderValueLabel;
	UIMenuButton** eaHeaderValueButton;
	UIPane** eaHeaderValuePane;
	UIPane** eaFooterPane;

	// Groups
	ChoiceEd_DefGroup **eaChoiceValueDefGroups;
	ChoiceEd_EntryGroup **eaChoiceEntryGroups;
} ChoiceEd_Doc;

ChoiceEd_Doc* choiceEd_Open(EMEditor* pEditor, char* pcName);
void choiceEd_Revert(ChoiceEd_Doc* pDoc);
void choiceEd_Close(ChoiceEd_Doc* pDoc);
EMTaskStatus choiceEd_Save(ChoiceEd_Doc* pDoc, bool bSaveAsNew);
void choiceEd_Init(EMEditor *pEditor);

#endif
