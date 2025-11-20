/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "editormanager.h"
#include "gameeditorshared.h"

typedef struct OldActor OldActor;
typedef struct AIJobDesc AIJobDesc;
typedef struct EncounterDef EncounterDef;
typedef struct EncounterLayer EncounterLayer;
typedef struct EncounterLayerEditorUI EncounterLayerEditorUI;
typedef struct OldEncounterMasterLayer OldEncounterMasterLayer;
typedef struct GEActionGroup GEActionGroup;
typedef enum SkillType SkillType;
typedef struct OldStaticEncounter OldStaticEncounter;
typedef struct UIButton UIButton;
typedef struct UICheckButton UICheckButton;
typedef struct UIComboBox UIComboBox;
typedef struct UIEditable UIEditable;
typedef struct UIExpander UIExpander;
typedef struct UIExpanderGroup UIExpanderGroup;
typedef struct UILabel UILabel;
typedef struct UIMenu UIMenu;
typedef struct UISlider UISlider;
typedef struct UITextEntry UITextEntry;
typedef struct UIMessageEntry UIMessageEntry;
typedef struct EventEditor EventEditor;

typedef struct EncounterLayerEditorUI
{
	UIWindow* layerTreeWindow;
	UITree*	layerTree;

	UIWindow* propertiesWindow;
	UIExpanderGroup* propExpGroup;

	EncounterPropUI* encounterUI;
	ActorPropUI* actorUI;
	UIWindow* renameWindow;

	UIWindow* createToolbar;

	UIWindow* invalidSpawnWindow;
	UIWindow* eventEditorWindow;

	UIWindow* mouseoverInfoWindow;
	UILabel* mouseoverInfoObjectName;
	UILabel* mouseoverInfoObjectLayer;

	UIExpander* patrolExpander;
	UIEditable* patrolNameEntry;
	UIComboBox* patrolTypeCombo;

	// Widgets for the group properties window
	UIWindow* groupPropWindow;
	UILabel* spawnCountLabel;
	UITextEntry** weightEntries;

	// Widgets for the team size override window
	UIWindow* forceTeamSizeWindow;
	UILabel* teamSizeLabel;

	// Context menu
	UIMenu* contextMenu;
} EncounterLayerEditorUI;

typedef struct EncounterLayerEditDoc
{
	// NOTE: This must be first
	EMEditorDoc emDoc;
	EditDocDefinition* docDefinition;

	GEPlacementTool placementTool;

	EncounterLayerEditorUI uiInfo;
	EncounterLayer* layerDef;
	EncounterLayer* origLayerDef;  // This is a saved copy of the original layer, used for langApplyEditorCopy()
	EncounterLayer* previousState;  // used for Undo/Redo
	REF_TO(EncounterDef) newEncDef;
	REF_TO(CritterDef) newCritter;
	Mat4 newObjectMat;

	GESelectedObject** selectedObjects;

	U32 activeTeamSize;

	bool selectionFromTree;
	bool refreshTree;

	// This needs to be set/cleared while any UI containing MEFields is
	// refreshed, to prevent recursive UI refreshing 
	bool bIgnoreFieldChanges;

	GEObjectType typeToCreate;
} EncounterLayerEditDoc;

typedef enum ELETreeNodeType
{
	ELETreeNodeType_EncGroup,
	ELETreeNodeType_Encounter,
	ELETreeNodeType_Actor,
	ELETreeNodeType_PatrolGroup,
	ELETreeNodeType_Patrol,
	ELETreeNodeType_NamedPoint,
	ELETreeNodeType_ClickableGroup,
	ELETreeNodeType_Clickable,
} ELETreeNodeType;

typedef struct ELETreeNode ELETreeNode;
typedef struct ELETreeNode
{
	void* nodeData;
	ELETreeNode* parent;
	ELETreeNodeType nodeType;
	bool selected;
} ELETreeNode;

typedef struct ELEWorldEditorInterfaceEM
{
	EMMapLayerType** mapLayers;
} ELEWorldEditorInterfaceEM;

typedef enum EMTaskStatus EMTaskStatus;

// Callbacks used by enclayereditorEM.c
void ELESetupUI(EncounterLayerEditDoc* encLayerDoc);
EncounterLayer* ELENewEncounterLayer(const char* name, EncounterLayerEditDoc* encLayerDoc);
void ELEOpenEncounterLayer(EncounterLayer *layer, EncounterLayerEditDoc* encLayerDoc);
void ELECloseEncounterLayer(EncounterLayerEditDoc* encLayerDoc);
EMTaskStatus ELESaveEncounterLayer(EncounterLayerEditDoc* encLayerDoc);
bool ele_SaveEncounterLayer(EncounterLayer* encLayer, char* fileOverride);
void ELEProcessInput(EncounterLayerEditDoc* encLayerDoc);
void ELEDrawActiveLayer(EncounterLayerEditDoc* encLayerDoc);
void ELEPlaceInEncounterLayer(EncounterLayerEditDoc* encLayerDoc, const char* name, const char* type);
void ELEGotFocus(EncounterLayerEditDoc* encLayerDoc);
void ELESetupWorldEditorInterface(ELEWorldEditorInterfaceEM* weInterface, bool bMapReset);
void ELERefreshStaticEncounters(EncounterLayerEditDoc* encLayerDoc, bool fullRefresh);
void ELERefreshUI(EncounterLayerEditDoc* encLayerDoc);


// EM callbacks used by the editor
void ELESetupUIEM(EncounterLayerEditDoc* encLayerDoc);
void ELEDestroyUIEM(EncounterLayerEditDoc* encLayerDoc);
void ELEPlaceEncounterEM(void);
void ELERefreshWorldEditorInterfaceEM(void);

// Add a project specific UI creation callback for the encounter layer editor clickable object creation
typedef void(*ELEClickableUIFunc)(EncounterLayerEditDoc* encLayerDoc);

typedef void(*EncounterChangeFunc)(EncounterLayer* encLayer, OldStaticEncounter* staticEnc, EncounterDef* defOverride, void* changeData);
typedef void(*ActorChangeFunc)(OldStaticEncounter* staticEnc, OldActor* actor, const void* changeData, U32 teamSize, OldActor* srcActor);

extern U32 g_DisableQuickPlace;

void ELECopySelectedToClipboard(EncounterLayerEditDoc* encLayerDoc, bool makeCopy);
void ELEPasteSelectedFromClipboard(EncounterLayerEditDoc* encLayerDoc);
int* ELEGetSelectedEncounterIndexList(EncounterLayerEditDoc* encLayerDoc);
void ELESaveEncAsDef(EncounterLayerEditDoc* encLayerDoc, OldStaticEncounter* staticEnc);
void ELEForceTeamSizeChanged(UISlider* slider, bool bFinished, EncounterLayerEditDoc* encLayerDoc);
void ELEEncounterMapUnload(OldEncounterMasterLayer *encMasterLayer);
SA_RET_OP_STR char* ELELayerNameFromFilename(const char *filename);

void MDEEventLogSendFilterToServer(EventEditor *editor, void *unused);

#endif