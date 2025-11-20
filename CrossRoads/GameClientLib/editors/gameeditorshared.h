/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "cmdparse.h"
#include "EditorManager.h"
#include "MultiEditField.h"
#include "stdtypes.h"
#include "UICore.h"
#include "WorldVariable.h"

typedef struct WorldGameActionProperties WorldGameActionProperties;
typedef struct ActorPropUI ActorPropUI;
typedef struct AIJobDesc AIJobDesc;
typedef struct CritterDef CritterDef;
typedef struct DisplayMessage DisplayMessage;
typedef struct DisplayMessageList DisplayMessageList;
typedef struct EditDocDefinition EditDocDefinition;
typedef struct EMEditorDoc EMEditorDoc;
typedef struct Expression Expression;
typedef struct FSM FSM;
typedef struct GEActionGroup GEActionGroup;
typedef struct GEList GEList;
typedef struct GEListItem GEListItem;
typedef struct GroupDef GroupDef;
typedef struct GEPlacementTool GEPlacementTool;
typedef struct GEVariableGroup GEVariableGroup;
typedef struct MEField MEField;
typedef struct Message Message;
typedef struct MissionDef MissionDef;
typedef struct OldNamedPointInEncounter OldNamedPointInEncounter;
typedef struct RotateGizmo RotateGizmo;
typedef struct TranslateGizmo TranslateGizmo;
typedef void   UIAnyWidget;
typedef struct UIButton UIButton;
typedef struct UIColorButton UIColorButton;
typedef struct UIComboBox UIComboBox;
typedef struct UIExpander UIExpander;
typedef struct UIExpanderGroup UIExpanderGroup;
typedef struct UILabel UILabel;
typedef struct UIMessageEntry UIMessageEntry;
typedef struct UIPairedBox UIPairedBox;
typedef struct UISeparator UISeparator;
typedef struct UIWidget UIWidget;
typedef struct WorldVariable WorldVariable;
typedef struct WorldVariableDef WorldVariableDef;
typedef enum WorldVariableType WorldVariableType;
typedef struct OldActor OldActor;
typedef struct OldActorAIInfo OldActorAIInfo;
typedef struct OldActorInfo OldActorInfo;
typedef struct EncounterDef EncounterDef;
typedef struct EncounterLayer EncounterLayer;
typedef struct OldEncounterMasterLayer OldEncounterMasterLayer;
typedef struct OldStaticEncounter OldStaticEncounter;
typedef struct MissionLevelDef MissionLevelDef;

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
typedef struct OldPatrolRoute OldPatrolRoute;
#endif

// Generic pointer for an editor doc
typedef void* GEEditorDocPtr;

typedef void (*GERemoveVariableFunc)(UIButton *pButton, GEVariableGroup *pGroup);
typedef bool (*GEActorChangeFunc)(OldActor *pActor, OldActor *pBaseActor, const void *pChangeData);
typedef void (*GEModifySelectedActorsFunc)(GEEditorDocPtr pDoc, GEActorChangeFunc modifyActorFunc, const void *pChangeData);

// These two must be the first members of any GE editor doc
typedef struct GenericMissionEditDoc
{
	EMEditorDoc emDoc;
	EditDocDefinition* docDefinition;
} GenericMissionEditDoc;

#define GE_SPACE 4

typedef struct GEPlacementTool
{
	// Gizmos
	RotateGizmo* rotGizmo;
	TranslateGizmo* transGizmo;

	// Quick movement fields
	int qrMy;
	F32 rotAngle;
	Vec3 rotMousePos;

	// For the marquee selection tool
	int marqueeMx, marqueeMy;
	Vec3 mouseMin, mouseMax;
	Mat44 scrProjMat;
	U32	inMarqueeSelect : 1;
	U32	processMarqueeSelect : 1;

	// Current mat that gets updated by the gizmos
	Mat4 gizmoMat;

	// Origin of the movement tool
	Mat4 movementOrigin;

	// Offset of the mouse location from the position of the object under the mouse
	Vec3 clickOffset;

	// Different placement methods
	U32 createNew : 1;
	U32 moveSelected : 1;
	U32 copySelected : 1;
	U32 moveOrigin : 1;

	// For the quickplace tool
	int qpMx, qpMy;
	U32	isQuickPlacing : 1;
	U32	isRotating : 1;

	// Tool modifiers
	U32	useAdditiveSelect : 1;
	U32	useBoreSelect : 1;
	U32	useCopySelect : 1;
	U32	useRotateMovement : 1;

	// Placement tool settings
	U32	useRotateTool : 1;
	U32	useGizmos : 1;
	U32	collideWithWorld : 1;
} GEPlacementTool;

typedef struct ActorPropUI
{
	GEEditorDocPtr pDoc;
	UIExpander* actorPropExpander;
	UIComboBox* critterTypeCombo;
	UILabel* critterLabel;
	UITextEntry* critterCombo;
	UILabel* critterGroupLabel;
	UIComboBox* rankCombo;
	UIComboBox* subRankCombo;
	UITextEntry* groupCombo;
	UIComboBox* factionCombo;
	UIEditable* contactEntry;
	UIEditable* nameEntry;
	UIEditable* fsmEntry;
	UIButton* fsmButton;
	UIEditable* spawnAnim;
	UIMessageEntry *dispNameEntry;
	UIButton* spawnWhenExpr;
	UIButton* interactCondExpr;
	UICheckButton** spawnEnabledButtons;
	UICheckButton** flavorButtons;
	UICheckButton** bossBarButtons;
	UILabel* overriddenLabel;
	UILabel* infoInstancedLabel;
	UILabel* aiInstancedLabel;
	UIExpanderGroup* expanderGroup;
	UIExpander* dialogExpander;
	UIExpander* varExpander;
	UIWidget** varUIList;
	UIActivationFunc dialogStateChanged;
	UIActivationFunc dialogTextChanged;
	UIActivationFunc dialogAudioChanged;
	UIActivationFunc dialogWhenChanged;
	UIActivationFunc varChanged;
	UIActivationFunc varMessageChanged;
	GEModifySelectedActorsFunc modifyActorsFunc;
} ActorPropUI;

typedef struct EncounterPropUI
{
	UIExpander* encPropExpander;
	UIEditable* nameEntry;
	UIEditable* scopeEntry;
	UICheckButton* ambushCheckBox;
	UICheckButton* snapToGroundCheckBox;
	UICheckButton* usePlayerLevelButton;
	UICheckButton* spawnForPlayerButton;
	UICheckButton* noDespawnButton;
	UIEditable* minLevelEntry;
	UIEditable* maxLevelEntry;
	UIButton* spawnExpr;
	UIButton* successExpr;
	UIButton* failExpr;
	UIButton* successAction;
	UIButton* failAction;
	UIButton* waveCondExpr;
	UIEditable* waveIntervalEntry;
	UIEditable* waveDelayMinEntry;
	UIEditable* waveDelayMaxEntry;
	UIEditable* spawnRadiusEntry;
	UIEditable* lockoutRadiusEntry;
	UIEditable* respawnTimeEntry;
	UIEditable* gangIDEntry;
	UIEditable* spawnAnim;
	UIEditable* ownerMapVarEntry;
	UIComboBox* factionCombo;
	UIComboBox* groupCombo;
	UIComboBox* patrolCombo;
	UIComboBox* dynamicSpawnCombo;
	UILabel* instancedLabel;
	UISlider* teamSizeSlider;
	UILabel* teamSizeCount;
	UISlider* chanceSlider;
	UILabel* chanceCount;
	UILabel* encounterValue;
	UIExpander* jobExpander;
	GEList* jobList;
	EncounterDef **defList;
	OldStaticEncounter **staticEncList;
	AIJobDesc **aiJobDescs;
} EncounterPropUI;

// Defines the list of functions that can be implemented by each mission edit doc
typedef struct EditDocDefinition
{
	void (*PasteCB)(GEEditorDocPtr);
	void (*CutCopyCB)(GEEditorDocPtr, bool, bool, const Vec3, const Vec3);
	void (*CancelCB)(GEEditorDocPtr);
	void (*DeleteCB)(GEEditorDocPtr);
	void (*MoveOriginCB)(GEEditorDocPtr);
	void (*MoveSpawnLocCB)(GEEditorDocPtr);
	void (*FreezeCB)(GEEditorDocPtr);
	void (*UnfreezeCB)(GEEditorDocPtr);
	void (*LeftClickCB)(GEEditorDocPtr);
	void (*RightClickCB)(GEEditorDocPtr);
	void (*GroupCB)(GEEditorDocPtr);
	void (*UngroupCB)(GEEditorDocPtr);
	void (*AttachCB)(GEEditorDocPtr);
	void (*DetachCB)(GEEditorDocPtr);
	void (*CenterCamCB)(GEEditorDocPtr);
	GEPlacementTool* (*GetGizmoCB)(GEEditorDocPtr);
	bool (*QuickPlaceCB)(GEEditorDocPtr);
	void (*PlaceObjectCB) (GEEditorDocPtr, const char*, const char*);
	void* (*GetStateCB)(GEEditorDocPtr);
	bool (*UndoStateCB)(GEEditorDocPtr, void*);
	bool (*RedoStateCB)(GEEditorDocPtr, void*);
	bool (*FreeStateCB)(GEEditorDocPtr, void*);
} EditDocDefinition;

typedef enum GEObjectType
{
	GEObjectType_Actor,
	GEObjectType_Encounter,
	GEObjectType_Group,
#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	GEObjectType_PatrolPoint,
	GEObjectType_PatrolRoute,
#endif
	GEObjectType_Point,		// Not to be confused with patrol or spawn points
} GEObjectType;

typedef struct GEDisplayDefData
{
	GroupDef* actDispDef;
	GroupDef* encDispDef;
	GroupDef* spawnLocDispDef;
} GEDisplayDefData;

// Depending on what object it is and what editor, only some fields will be filled out
typedef struct GESelectedObject
{
	// Type of selected object this is, corresponds to the types of things selectable in the editor
	GEObjectType selType;

	// Data for the selected object
	void* objData;

	// These indices are used to address the object if the pointer cannot be used directly
	int groupIndex;
	int objIndex;
} GESelectedObject;

typedef void (*GEDataChangedCB)(void *pUserData, bool bUndoable);
typedef void (*GEUpdateDisplayCB)(void *pUserData);
typedef bool (*GEIsEditableFunc)(void *pUserData);

typedef struct GEVariableGroup
{
	void *pData; // Used by caller to store data
	WorldVariable ***peaVariables;
	int index;
	MEFieldChangeCallback changeFunc;
	MEFieldPreChangeCallback preChangeFunc;
	WorldVariableType ePrevType;

	UILabel *pTitleLabel;
	UILabel *pNameLabel;
	UILabel *pValueLabel;
	UILabel *pTypeLabel;
	UILabel *pTypeValueLabel;
	UIButton *pRemoveButton;

	MEField *pNameField;
	MEField *pValueField;
} GEVariableGroup;

typedef struct GEVariableDefGroup
{
	void *pData; // Used by caller to store data
	WorldVariableDef ***peaVariableDefs;
	WorldVariableDef **eaMissionVariableDefs;
	int index;
	MEFieldChangeCallback changeFunc;
	MEFieldPreChangeCallback preChangeFunc;
	WorldVariableType ePrevType;

	UILabel *pTitleLabel;
	UILabel *pNameLabel;
	UILabel *pInitFromLabel;
	UILabel *pValueLabel;
	UILabel *pValueLabel2;
	UILabel *pChoiceTableLabel;
	UILabel *pChoiceNameLabel;
	UILabel *pChoiceIndexLabel;
	UILabel *pMapVariableLabel;
	UILabel *pMissionLabel;
	UILabel *pMissionVariableLabel;
	UILabel *pExpressionLabel;
	UILabel *pActivityNameLabel;
	UILabel *pActivityVariableNameLabel;
	UILabel *pActivityDefaultTypeLabel;
	UILabel *pActivityDefaultValueLabel;
	UILabel *pActivityDefaultValueLabel2;

	UILabel *pTypeLabel;
	UILabel *pTypeValueLabel;
	UIButton *pRemoveButton;

	MEField *pNameField;
	MEField *pInitFromField;
	MEField *pTypeField;
	MEField *pValueField;
	MEField *pValueField2;
	MEField *pChoiceTableField;
	MEField *pChoiceNameField;
	MEField *pChoiceIndexField;
	MEField *pMapVariableField;
	MEField *pMissionField;
	MEField *pMissionVariableField;
	MEField *pExpressionField;
	MEField *pActivityNameField;
	MEField *pActivityVariableNameField;
	MEField *pActivityDefaultTypeField;
	MEField *pActivityDefaultValueField;
	MEField *pActivityDefaultValueField2;

	char** eaChoiceTableNames;
	char** eaSrcMapVariables;
	char** eaDestVariableDefs;
} GEVariableDefGroup;

typedef struct GEMissionLevelDefGroup
{
	void *pData; // Used by caller to store data
	MEFieldChangeCallback changeFunc;
	MEFieldPreChangeCallback preChangeFunc;

	UILabel *pLevelTypeLabel;
	MEField *pLevelTypeField;
	
	UILabel *pLevelLabel;
	MEField *pLevelField;
	
	UILabel *pLevelMapVarLabel;
	MEField *pLevelMapVarField;
	
	UILabel *pLevelClampTypeLabel;
	MEField *pLevelClampTypeField;
	
	UILabel *pLevelClampMapVarLabel;
	MEField *pLevelClampMapVarField;
	
	UILabel *pLevelClampSpecifiedMinLabel;
	MEField *pLevelClampSpecifiedMinField;
	
	UILabel *pLevelClampSpecifiedMaxLabel;
	MEField *pLevelClampSpecifiedMaxField;
	
	UILabel *pLevelClampOffsetMinLabel;
	MEField *pLevelClampOffsetMinField;
	
	UILabel *pLevelClampOffsetMaxLabel;
	MEField *pLevelClampOffsetMaxField;
} GEMissionLevelDefGroup;

// UI for a GameAction
typedef struct GEActionGroup
{
	// Callbacks
	GEDataChangedCB dataChangedCB;
	GEUpdateDisplayCB updateDisplayCB;
	GEIsEditableFunc isEditableFunc;
	void *pUserData;

	// Optional - Submissions for the "Grant Sub-Mission" Action
	MissionDef ***peaSubMissions;

	// Optional - Variable defs to choose from for the "Grant Mission" and "Mission Offer" actions
	WorldVariableDef **peaVarDefs;

	WorldGameActionProperties ***peaActions;
	WorldGameActionProperties ***peaOrigActions;
	int index;

	GEVariableDefGroup **eaVariableDefGroups;

	UIWidget *pWidgetParent;
	UISeparator *pSeparator;
	UIButton *pRemoveButton;
	UILabel *pMainLabel;
	UILabel *pLabel1;
	UILabel *pLabel2;
	UILabel *pLabel3;
	UILabel *pLabel4;
	UILabel *pLabel5;
	UILabel *pLabel6;
	UILabel *pLabel7;
	UILabel *pLabel8;
	UILabel *pLabel9;
	UIComboBox *pColorCombo;
	UIColorButton *pColorButton;
	UIPairedBox *pOutBox;
	UIButton *pVarAddButton;
	UICheckButton *pDepartSequenceOverride;

	MEField *pTypeField;
	MEField *pMissionFromField;
	MEField *pMissionField;
	MEField *pMissionVarField;
	MEField *pMissionMapVarField;
	MEField *pSubMissionField;
	MEField *pDropMissionField;
	MEField *pItemField;
	MEField *pCountField;
	MEField *pMessageField;
	MEField *pBoolField;
	MEField *pContactField;
	MEField *pNameField;
	MEField *pTransSequenceField;
	MEField *pNemesisStateField;
	MEField *pExpressionField;
	MEField *pSubjectField;
	MEField *pBodyField;
	MEField *pNotifyTypeField;
	MEField *pNotifySoundField;
	MEField *pNotifyLogicalStringField;
	MEField *pNotifyHeadshotTypeField;
	MEField *pNotifyHeadshotStyleField;
	MEField *pNotifyHeadshotField;
	MEField *pNotifyHeadshotPetContactField;
	MEField *pNotifyHeadshotCritterGroupTypeField;
	MEField *pNotifyHeadshotCritterGroupField;
	MEField *pNotifyHeadshotCritterGroupMapVarField;
	MEField *pNotifyHeadshotCritterGroupIdentifierField;
	MEField *pOfferHeadshotDisplayName;
	MEField *pOfferHeadshotTypeField;
	MEField *pOfferHeadshotStyleField;
	MEField *pOfferHeadshotField;
	MEField *pOfferHeadshotPetContactField;
	MEField *pOfferHeadshotCritterGroupTypeField;
	MEField *pOfferHeadshotCritterGroupField;
	MEField *pOfferHeadshotCritterGroupMapVarField;
	MEField *pOfferHeadshotCritterGroupIdentifierField;
	MEField *pNotifySplatFXField;
	MEField *pAttribKeyField;
	MEField *pAttribValueField;
	MEField *pAttribModifyTypeField;
	MEField *pVarNameField;
	MEField *pModifyTypeField;
	MEField *pIntIncrementField;
	MEField *pFloatIncrementField;
	MEField *pActivityLogTypeField;
	MEField *pDoorKeyField;
	MEField *pGuildStatNameField;
	MEField *pGuildOperationTypeField;
	MEField *pGuildOperationValueField;
	MEField *pGuildThemeNameField;
	MEField *pItemAssignmentNameField;
	MEField *pItemAssignmentOperationField;

	UICheckButton* pSpecifyMapOverride;
	UICheckButton* pIncludeTeammates;
	const char* pOverrideSrcMap;
	const char* pOverrideDestMap;
	UITextEntry* pSourceMapOverride;
	UITextEntry* pDestMapOverride;
	
	GEVariableDefGroup *pWarpDestGroup;
	GEVariableDefGroup *pDoorKeyDestGroup;
	GEVariableGroup *pShardVarGroup;
} GEActionGroup;

extern GEDisplayDefData g_GEDisplayDefs;
extern char** g_GEPowerStrengthDispNames;
extern bool g_ShowAggroRadius;


// -- GEList -- 
// This is a somewhat generic list that displays an earray of structs according 
// to a "Create UI" callback.  The list can be added to, deleted from, and re-ordered.
// -----------------------------------------------------------------------------------
typedef F32 (*GEListItemCreateUIFunc)(GEListItem *listItem, UIWidget*** widgetList, void *item, F32 x, F32 y, UIAnyWidget* widgetParent, void *userData);
typedef void (*GEListHeightChangedCB)(GEList *list, F32 newHeight, void *userData);
typedef void* (*GEListItemCreateFunc)(void* userData);
typedef void (*GEListItemDestroyFunc)(void *item, void* userData);

typedef struct GEListItem
{
	GEList *parentList;
	F32 y;
	UIWidget** widgets;
} GEListItem;

typedef struct GEList
{
	void*** items;
	GEListItem** listItems;
	UIAnyWidget *widgetParent;
	F32 x;
	F32 y;
	UIButton *addNewButton;
	void *userData;
	GEListItemCreateUIFunc createUIFunc;
	GEListItemCreateFunc createItemFunc;
	GEListItemDestroyFunc destroyItemFunc;
	GEListHeightChangedCB heightChangedCB;
	bool orderable;
	bool setDocUnsavedOnChange;
} GEList;

extern EMPicker s_EncounterPicker;

GEList* GEListCreate(void*** items, F32 x, F32 y, UIAnyWidget* widgetParent, void *userData, GEListItemCreateUIFunc createUIFunc, GEListItemCreateFunc createItemFunc, GEListItemDestroyFunc destroyItemFunc, GEListHeightChangedCB heightChangedCB, bool orderable, bool setDocUnsavedOnChange);
void GEListDestroy(GEList *list);
void GEListRefresh(GEList *list);

void GEListItemSetHeight(GEListItem *listItem, F32 newHeight);

// -- End GEList -------------------------------------------------------------------------


// -- Message stuff --

// Fast way to create a Message Entry
F32 GECreateMessageEntryEx(UIAnyWidget** msgEntry, UILabel** labelPtr, const char* labelName, const Message* message, F32 x, F32 y, F32 w, F32 labelWidth, UIActivationFunc callback, void* cbData, UIAnyWidget *parent, bool canEditKey, bool canEditScope);
#define GECreateMessageEntry(ppMsgEntry, labelName, message, x, y, w, labelWidth, callback, cbData, parent)   \
	GECreateMessageEntryEx(ppMsgEntry, NULL, labelName, message, x, y, w, labelWidth, callback, cbData, parent, false, false)

// Updates a message to the new value and fixes up the key and scope using the default message.
// Creates message if it doesn't exist.
void GEGenericUpdateMessage(Message **messageToUpdate, const Message *newMessage, const Message *defaultMessage);


// -- UI Control Utilities --

// Define which validation function to use in MECreateTextEntry
#define GE_VALIDFUNC_NONE 0
#define GE_VALIDFUNC_NOSPACE 1
#define GE_VALIDFUNC_INTEGER 2
#define GE_VALIDFUNC_FLOAT 3
#define GE_VALIDFUNC_LEVEL 4
#define GE_VALIDFUNC_NAME 5
#define GE_VALIDFUNC_SCOPE 6

#define GE_NAMELENGTH_MAX 1024
#define GE_SPACE 4
#define GE_ENCLABEL_WIDTH 95
#define ME_MINSNAPDIST 20
#define ME_NAMED_POINT_RADIUS 1

// Default colors to use to indicate the current functionality
#define GE_SEL_COLOR ColorRed
#define GE_INFO_COLOR ColorBlue
#define GE_COPY_COLOR ColorGreen
#define GE_MOVE_COLOR ColorYellow
#define GE_AGGRO_COLOR ColorPurple

#define GEAD_SPACE 4
#define GEAD_XPOS 0
#define GEAD_TEXT_WIDTH 150
#define GEAD_SOUND_WIDTH 100

extern F32 g_GEDrawScale;
#define ENC_MAX_DIST_SQ (1000000*g_GEDrawScale) // 1000 squared

// Fast way to create a text entry that fills out all the fields and adds it to the window
// Uses expander if that exists, otherwise defaults to adding it to a window, must have one of the two
// Non-zero textAreaHeight makes this to be an expandable text entry into multiple lines
int GETextEntryCreateEx(UIAnyWidget** widgetPtr, UILabel** labelPtr, const char* labelName, const char* startString, const char ***optionsList, const void *dictHandleOrName, EArrayHandle* optionsEArray, ParseTable *pTable, const unsigned char* pTableField, int x, int y, int w, int textAreaHeight, int labelBuffer, int whichValidFunc, UIActivationFunc callback, void* cbData, UIAnyWidget *parent);
#define GETextEntryCreate(widgetPtr, labelName, startString, x, y, w, textAreaHeight, labelBuffer, whichValidFunc, callback, cbData, parent)	\
	GETextEntryCreateEx(widgetPtr, NULL, labelName, startString, NULL, NULL, NULL, NULL, NULL, x, y, w, textAreaHeight, labelBuffer, whichValidFunc, callback, cbData, parent)

// Fast way to create a combo box entry that fills out all the fields and adds it to the window
// Uses expander if that exists, otherwise defaults to adding it to a window, must have one of the two
// Options for the combo box must be specified as an array of strings
int GEComboBoxCreateEx(UIAnyWidget** widgetPtr, UILabel** labelPtr, const char* labelName, const char*** options, int initialVal, int x, int y, int w, int labelWidth, UIActivationFunc callback, void* cbData, UIAnyWidget* parent);


// -- Encounter Editors Common Code --

// Defined in expressioneditor.h
typedef void (*ExprEdExprFunc) (Expression *expr, UserData data);

// Wrapper for when you call actor update functions. This is to make sure edit for all teamsizes works
// When editing actor fields, you should not manually access activeTeamSize but instead use ACTOR_UPDATE_TEAMSIZE
#define ACTOR_UPDATE_BEGIN(doc) { U32 ACTOR_UPDATE_TEAMSIZE, maxTeamSize = doc->editInfoForAllTeamSize ? MAX_TEAM_SIZE : doc->activeTeamSize; \
	for (ACTOR_UPDATE_TEAMSIZE = doc->editInfoForAllTeamSize ? 1 : doc->activeTeamSize; ACTOR_UPDATE_TEAMSIZE <= maxTeamSize; ACTOR_UPDATE_TEAMSIZE++) {
#define ACTOR_UPDATE_END(doc) } }

char* GECreateUniqueActorName(EncounterDef* encDef, const char* desiredName);
const OldActorInfo *GEGetActorInfoNoUpdate(OldActor* actor, OldActor* baseActor);
OldActorInfo* GEGetActorInfoForUpdate(OldActor* actor, OldActor* baseActor);
OldActorAIInfo* GEGetActorAIInfoForUpdate(OldActor* actor, OldActor* baseActor);
void GEChangeActorFSM(OldStaticEncounter* staticEnc, OldActor* actor, const char* fsmName, U32 teamSize, OldActor* srcActor);
OldActor* GEActorCreate(U32 uniqueID, OldActor* copyThisActor);
void GEApplyActorPositionChange(OldActor* actor, const Mat4 newRelMat, OldActor* baseActor);
void GEEncounterDefAddActor(EncounterDef* def, CritterDef* critterDef, Mat4 actorMat, U32 uniqueID);
void GEInstanceStaticEncounter(OldStaticEncounter *staticEnc);

char* GEAllocStringIfNN(const char* string);
char* GEAllocPooledStringIfNN(const char* string);
Message *GECreateMessageCopy(const Message *originalMsg, const Message *defaultMsg);
void GESelectedObjectDestroyCB(GESelectedObject* object);

// Reflow callback for when an expander group is inside another expander
void GENestedExpanderReflowCB(UIExpanderGroup *childGroup, UIExpander *expander);

// Updates an expression from the given expression, removes it if the expression is NULL
void GEUpdateExpressionFromExpression(Expression** exprPtr, const Expression* exprToCopy);

// Create a button that will open the expression editor widget
int GEExpressionEditButtonCreate(UIButton** buttonPtr, UILabel** labelPtr, const char* labelName, Expression* startExpr, int x, int y, int w, int labelBuffer, ExprEdExprFunc exprFunc, UserData data, UIAnyWidget* parent);

void GESaveEncAsDefHelper(UITextEntry* textEntry, EncounterDef* oldEnc);
bool GESaveEncounterInternal(EncounterDef* def, char* fileOverride);

// Find the common FSM from the selected actors, if any
FSM* GEFindCommonActorFSM(OldActor*** pppActors);
const char* GEFindCommonActorFSMName(OldActor*** pppActors);

// Returns true if the tool is in placement mode
bool GEPlacementToolIsInPlacementMode(GEPlacementTool* placementTool);

// Applies current placement tool(differs if we are quick moving or not)
void GEPlacementToolApply(GEPlacementTool* placementTool, Mat4 initialMat);

// Returns true if the current tool settings will cause a copy
bool GEPlacementToolShouldCopy(GEPlacementTool* placementTool);

// Get the world position of the actor
void GEFindActorMat(OldActor* actor, Mat4 encCenter, Mat4 actorMat);

// Get the world position of a named point within an encounter
void GEFindEncPointMat(SA_PARAM_NN_VALID OldNamedPointInEncounter* point, Mat4 encCenter, Mat4 actorMat);

// Resets the current state of the placement tool and sets the new positions
void GEPlacementToolReset(GEPlacementTool* placementTool, const Vec3 gizmoPos, const Vec3 clickOffset);

// Update the placement tool each frame
void GEPlacementToolUpdate(GEPlacementTool* placementTool);

// Returns true if it canceled something, otherwise it had nothing to cancel
bool GEPlacementToolCancelAction(GEPlacementTool* placementTool);

// Find the closest actor under the mouse's left click, returns -1 if no actors are under the mouse
// Takes a maximum distance from the mouse and sets that distance to the distance to the actor found
int GEWhichActorUnderMouse(EncounterDef* def, Mat4 encCenter, Vec3 selActorPos, Vec3 clickOffset, F32 *distance);

// Find the closest named point (in an encounter) under the mouse's left click, returns -1 if no points are under the mouse
int GEWhichEncounterPointUnderMouse(EncounterDef* def, Mat4 encCenter, Vec3 selPointPos, Vec3 clickOffset, F32 *distance);

// Checks legality of a marquee selection, within distance, backfacing, etc.
bool GEIsLegalMargqueeSelection(Vec3 centerPos, Mat4 camMat);

// Draw the currently loaded encounter actors and center point of the group
void GEDrawEncounter(EncounterDef* def, Mat4 encCenter, int* selectedActors, int* selectedPoints, bool groupSelected, bool inPlacement, bool isCopy, bool isMouseover, U32 teamSize, bool bInWorld, bool snapToGround);

// Draw a point
void GEDrawPoint(Mat4 pointLoc, bool isSelected, Color selColor);

// Draw an actor at the position given with the bounding box highlighted if selected
void GEDrawActor(OldActor* actor, Mat4 actorMat, bool isSelected, Color selColor, U32 teamSize, bool bInWorld, F32 distToEncSquared);

void GEDrawPatrolPoint(Mat4 mat, bool isSelected, Color selColor, bool bInWorld);
bool GEFindMouseLocation(GEPlacementTool* placementTool, Vec3 resultPos);
bool GECompareMessages(const DisplayMessage *displayMsgA, const DisplayMessage *displayMsgB);
void GETextEntryFreeDataCB(UIEditable *editable);

// Finds a selected type in a given object list
GESelectedObject* GESelectedObjectFind(GESelectedObject*** selObjListPtr, GEObjectType selType, void* objData, int groupIndex, int objIndex);

// Tries to select an object based on whether or not additive is true
void GESelectObject(GESelectedObject*** selObjListPtr, GEObjectType selType, void* objData, int groupIndex, int objIndex, bool additive);

void GEEncounterPropUIRefreshSingle(EncounterPropUI* encounterUI, EncounterDef* pEncDef, OldStaticEncounter*** pppStaticEncs, EncounterLayer* encLayer, bool fullRefresh);

// Fixes all ui widgets to have something selected if all fields are the same, otherwise clears them
// If fullRefresh is false, only refreshes "Overridden" and "Instanced" labels
void GEActorPropUIRefresh(ActorPropUI* actorUI, OldActor*** pppActors, bool fullRefresh);

// Fixes all ui widgets to have something selected if all fields are the same, otherwise clears them
void GEEncounterPropUIRefresh(EncounterPropUI* encounterUI, EncounterDef*** pppEncDefs, OldStaticEncounter*** pppStaticEncs, EncounterLayer* encLayer, bool fullRefresh);

// Create the encounter prop ui given a list of callback functions
EncounterPropUI* GEEncounterPropUICreate(GEEditorDocPtr geDoc, int y, UIActivationFunc nameChangeFunc, UIActivationFunc ambushCheckBoxChangeFunc, UIActivationFunc spawnForPlayerChangeFunc, 
										 UIActivationFunc snapToGroundChangeFunc, UIActivationFunc doNotDespawnChangeFunc, UIComboBoxEnumFunc dynamicSpawnChangeFunc, ExprEdExprFunc spawnChangeFunc,
										 ExprEdExprFunc successChangeFunc, ExprEdExprFunc failChangeFunc,
										 ExprEdExprFunc successActionChangeFunc, ExprEdExprFunc failActionChangeFunc,
										 ExprEdExprFunc waveCondChangeFunc, UIActivationFunc waveIntervalChangeFunc, 
										 UIActivationFunc waveMinDelayChangeFunc, UIActivationFunc waveMaxDelayChangeFunc, 
										 UIActivationFunc spawnRadChangeFunc,
										 UIActivationFunc lockoutRadChangeFunc, UISliderChangeFunc teamSizeChangeFunc, U32 initTeamSize,  UIActivationFunc multiTeamSizeSelectFunc,
										 UIActivationFunc critterGroupChangeFunc, UIActivationFunc factionChangeFunc, UIActivationFunc gangIDChangeFunc, UISliderChangeFunc chanceChangeFunc,
										 UIActivationFunc patrolChangeFunc, const char*** patrolListPtr, UIActivationFunc minLevelChangeFunc, UIActivationFunc maxLevelChangeFunc,
										 UIActivationFunc usePlayerLevelChangeFunc, UIActivationFunc spawnAnimChangeFunc, UIActivationFunc respawnTimeChangeFunc, UIActivationFunc scopeChangeFunc);

// Create the actor prop ui given a list of callback functions
ActorPropUI* GEActorPropUICreate(GEEditorDocPtr geDoc, 
								 UIActivationFunc nameChangeFunc, UIActivationFunc selCritterFunc, UIActivationFunc selFSMFunc, 
								 UIActivationFunc openFSMFunc, UIActivationFunc seChangedFunc, UIActivationFunc selRankFunc,
								 UIActivationFunc selSubRankFunc, UIActivationFunc selFactionFunc,
								 UIActivationFunc dialogStateChanged, UIActivationFunc dialogTextChanged, UIActivationFunc dialogAudioChanged, UIActivationFunc dialogWhenChanged,
								 UIActivationFunc selContactFunc, UIActivationFunc dispNameChangedFunc, ExprEdExprFunc spawnWhenChangedFunc, UIActivationFunc interactCondChangedFunc,
								 UIActivationFunc bossBarChangedFunc, UIActivationFunc varChangedFunc, UIActivationFunc varMessageChangedFunc,
								 UIActivationFunc spawnAnimChangedFunc,
								 GEModifySelectedActorsFunc modifyActorsFunc);

// Load the display defs used to show representations of the actors
void GELoadDisplayDefs(void* unused, bool bUnused);

// Find the screen coordinates of a display def with the given mat
#define GEFindScreenCoords(def, worldMat, scrProjMat, bottomLeft, topRight) \
	GEFindScreenCoordsEx(def->bounds.min, def->bounds.max, worldMat, scrProjMat, bottomLeft, topRight)

void GEFindScreenCoordsEx(Vec3 minbounds, Vec3 maxbounds, Mat4 worldMat, Mat44 scrProjMat, Vec3 bottomLeft, Vec3 topRight);


// Functions to generate lists of objects for combo boxes
void GERefreshClickableList(SA_PARAM_NN_VALID const char*** peaList);
void GERefreshVolumeAndInteractableList(const char*** peaList);
void GERefreshEncounterLayerList(SA_PARAM_NN_VALID const char*** peaList, SA_PARAM_OP_VALID OldEncounterMasterLayer *pMasterLayer);
void GERefreshEncGroupList(SA_PARAM_NN_VALID const char*** peaList);
void GERefreshEncounterList(SA_PARAM_NN_VALID const char*** peaList);
void GERefreshActorList(SA_PARAM_NN_VALID const char*** peaList);
void GERefreshUsedEncounterTemplateList(SA_PARAM_NN_VALID const char*** peaList);
void GECheckNameList( char*** peaCurrentList, char*** peaNewList );


// -- APIs specific to Editor Manager

void GEEditorInitExpressionsEM(EMEditor* editor);

// Sets an editor doc to an unsaved state.  Should only be called on GE editor docs, but slightly safer
// than GESetCurrentDocUnsaved.
void GESetDocUnsaved(GEEditorDocPtr docPtr);
// Sets the current editor doc to an unsaved state
void GESetCurrentDocUnsaved(void);

void* GEGetActiveEditorDocEM(SA_PARAM_OP_STR const char* docType);

bool GECheckoutDocEM(EMEditorDoc* editorDoc);
void GEPushUndoState(GEEditorDocPtr docPtr);

void GEDestroyUIGenericEM(EMEditorDoc* editorDoc);
void GESetDocFileEM(EMEditorDoc* emDoc, const char* filename);

void GERefreshMapNamesList(void);
void GERefreshPatrolRouteList(void);

// Validation functions for text entries
bool GEValidateNameFunc(UIAnyWidget *widget, unsigned char **oldString, unsigned char **newString, void *unused);
bool GEValidateScopeFunc(UIAnyWidget *widget, unsigned char **oldString, unsigned char **newString, void *unused);

void GEAddFieldToParent(MEField *pField, UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, MEFieldChangeCallback changeFunc, MEFieldPreChangeCallback preChangeFunc, void *pData);

// ---- GameAction UI --------

F32 GEUpdateAction(GEActionGroup *pGroup, char *pcText, char* pcSrcZoneMap, WorldGameActionProperties ***peaActions, WorldGameActionProperties ***peaOrigActions, F32 y, int index);
void GEFreeActionGroup(GEActionGroup *pGroup);

F32 GEUpdateVariableGroup(GEVariableGroup *pGroup, UIWidget *pParent, WorldVariable ***peaVariables, WorldVariable *pVar, WorldVariable *pOrigVar, WorldVariableDef ***peaVarDefs, const char ***peaVarNames, int index, F32 y, MEFieldChangeCallback changeFunc, MEFieldPreChangeCallback preChangeFunc, void *pData);
void GEFreeVariableGroup(GEVariableGroup *pGroup);

F32 GEUpdateVariableDefGroup(GEVariableDefGroup *pGroup, UIWidget *pParent, WorldVariableDef ***peaVariableDefs, WorldVariableDef *pVarDef, WorldVariableDef *pOrigVarDef, const char* pchSrcZoneMap, const char** eaSrcMapVariableNames, const char * pchDestZoneMap, int index, F32 x, F32 xControl, F32 y, MEFieldChangeCallback changeFunc, MEFieldPreChangeCallback preChangeFunc, void *pData);
F32 GEUpdateVariableDefGroupNoName(GEVariableDefGroup *pGroup, UIWidget *pParent, WorldVariableDef *pVarDef, WorldVariableDef *pOrigVarDef, WorldVariableType varType, const char* pchSrcZoneMap, const char** eaSrcMapVAriableNames, char* initFromText, char* initFromTooltip, F32 x, F32 xControl, F32 y, MEFieldChangeCallback changeFunc, MEFieldPreChangeCallback preChangeFunc, void *pData);
F32 GEUpdateVariableDefGroupFromNames(GEVariableDefGroup *pGroup, UIWidget *pParent, const char*** peaDefNames, WorldVariableDef ***peaAllVariableDefs, WorldVariableDef ***peaVariableDefs, WorldVariableDef *pVarDef, WorldVariableDef *pOrigVarDef, const char* pchZoneMap, int index, F32 x, F32 xControl, F32 y, MEFieldChangeCallback changeFunc, MEFieldPreChangeCallback preChangeFunc, void *pData);
void GEFreeVariableDefGroup(GEVariableDefGroup *pGroup);
void GEFreeVariableDefGroupSafe(GEVariableDefGroup **pGroup);

F32 GEUpdateMissionLevelDefGroup(GEMissionLevelDefGroup *pGroup, UIExpander *pParent, MissionLevelDef *levelDef, MissionLevelDef *origLevelDef,
								 F32 xOffsetBase, F32 xOffsetControl, F32 y,
								 MEFieldChangeCallback changeFunc, MEFieldPreChangeCallback preChangeFunc, void *pData);
void GEFreeMissionLevelDefGroupSafe(GEMissionLevelDefGroup **ppGroup);
void GEMissionLevelDefPreSaveFixup(MissionLevelDef *levelDef);

extern const char** g_GEMapDispNames;
extern const char** g_GEPatrolRouteNames;

#endif
