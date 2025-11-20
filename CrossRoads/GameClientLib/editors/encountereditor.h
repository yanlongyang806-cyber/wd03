#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#ifndef NO_EDITORS

#include "EditorManager.h"
#include "gameeditorshared.h"
#include "UICore.h"

typedef struct ActorPropUI ActorPropUI;
typedef struct CritterDef CritterDef;
typedef struct EditDocDefinition EditDocDefinition;
typedef enum EMTaskStatus EMTaskStatus;
typedef struct EncounterDef EncounterDef;
typedef struct EncounterPropUI EncounterPropUI;
typedef void* GEEditorDocPtr;
typedef struct GEPlacementTool GEPlacementTool;

typedef struct EncounterEditorUI
{
	UIWindow* propertiesWindow;
	UIExpanderGroup* propExpGroup;
	EncounterPropUI* encounterUI;
	ActorPropUI* actorUI;
	UIExpander* namedPointExpander;
	UIEditable* namedPointNameEntry;
} EncounterEditorUI;

typedef struct EncounterEditDoc
{
	// NOTE: These must be first
	EMEditorDoc emDoc;
	EditDocDefinition* docDefinition;

	GEPlacementTool placementTool;

	EncounterEditorUI uiInfo;
	EncounterDef* def;
	EncounterDef* origDef;  // Copy used for langApplyEditorCopy()
	EncounterDef* previousState;  // Copy used for Undo/Redo
	GESelectedObject** selectedObjects;

	Mat4 encCenter;

	U32 activeTeamSize;

	REF_TO(CritterDef) newCritter;
	GEObjectType typeToCreate;

	U32 newAndNeverSaved : 1;
	bool bNeedsRefresh;
} EncounterEditDoc;


// Editor callbacks
EncounterDef* EDENewEncounter(const char* name, EncounterEditDoc* encDoc);
void EDEOpenEncounter(EncounterDef *def, EncounterEditDoc* encDoc);
EMTaskStatus EDESaveEncounter(EncounterEditDoc* encDoc);
void EDESetupUI(EncounterEditDoc* encDoc);
void EDECloseEncounter(EncounterEditDoc* encDoc);
void EDEProcessInput(EncounterEditDoc* encDoc);
void EDEDrawEncounter(EncounterEditDoc* encDoc);
void EDEPlaceObject(EncounterEditDoc* encDoc, const char* name, const char* type);
void EDEFixupEncounterMessages(EncounterDef *encDef);

// EM callbacks used by the editor
void EDESetupUIEM(EncounterEditDoc* encDoc);
void EDEDestroyUIEM(EncounterEditDoc* encDoc);
void EDENameChangedEM(const char* newName, EncounterEditDoc* encDoc);

// Calls used by commands to execute
typedef bool(*EDEEnableQPFunc)(GEEditorDocPtr editorDoc);
void EDEPlacementToggleGizmo(GEPlacementTool* placementTool, bool isDown);
void EDEPlacementToggleCopy(GEPlacementTool* placementTool, bool isDown);
void EDEInputLeftDrag(GEEditorDocPtr editorDoc, GEPlacementTool* placementTool, bool startDrag, EDEEnableQPFunc enableQPFunc);
void EDEPlacementBore(GEPlacementTool* placementTool, bool isDown);
void EDEPlacementAdditive(GEPlacementTool* placementTool, bool isDown);


#endif