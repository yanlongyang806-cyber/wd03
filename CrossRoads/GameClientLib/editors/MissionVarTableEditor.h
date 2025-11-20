#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#ifndef NO_EDITORS

#include "EditorManager.h"

typedef struct EditDocDefinition EditDocDefinition;
typedef struct Message Message;
typedef struct MissionVarTable MissionVarTable;
typedef struct TemplateVariable TemplateVariable;


typedef struct MissionVarTableEditDoc
{
	// NOTE: These must be first
	EMEditorDoc emDoc;
	EditDocDefinition* docDefinition;

	MissionVarTable* varTable;
	MissionVarTable* varTableOrig;

	UIWindow* window;
	UIScrollArea* mainScrollArea;

	UIComboBox *dependencyTypeCombo;
	UIComboBox *variableTypeCombo;
	GEList* mainList;
	GEList** subGroupLists;

	U32 newAndNeverSaved : 1;
	bool bNeedsRefresh;
} MissionVarTableEditDoc;

typedef void (*MVETemplateVarUpdateFunc)(UIAnyWidget *, TemplateVariable *var, void* userData);
typedef Message* (*MVETemplateVarCreateMessageFunc)(const void *parent, const char *varName, U32 id);


void MVESetupUI(MissionVarTableEditDoc* mveDoc);
MissionVarTable* MVENewMissionVarTable(const char* name, MissionVarTableEditDoc* editDoc);
MissionVarTable* MVEOpenMissionVarTable(const char* name, MissionVarTableEditDoc* editDoc);
void MVECloseMissionVarTable(MissionVarTableEditDoc* mveDoc);
EMTaskStatus MVESaveMissionVarTable(MissionVarTableEditDoc* mveDoc, bool saveAsCopy);
//void MVESaveMissionVarTableAs(MissionVarTableEditDoc* mveDoc);  - See comment on definition
bool MVESaveMissionVarTableFile(MissionVarTable* varTable, char* fileOverride);

// Editor-manager APIs
void MVESetupUIEM(MissionVarTableEditDoc* editDoc);
void MVENameChangedEM(const char* newName, MissionVarTableEditDoc* editDoc);

#endif