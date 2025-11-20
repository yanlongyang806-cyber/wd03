#ifndef __WORLDEDITORUI_H__
#define __WORLDEDITORUI_H__
GCC_SYSTEM

#ifndef NO_EDITORS

#include "UILib.h"
#include "WorldLibEnums.h"
#include "Encounter_enums.h"

typedef struct EditLibGizmosToolbar EditLibGizmosToolbar;
typedef struct GroupTracker GroupTracker;
typedef struct GroupDef GroupDef;
typedef struct GroupDefLockedFile GroupDefLockedFile;
typedef struct TrackerHandle TrackerHandle;
typedef struct EMPanel EMPanel;
typedef struct EMEditor EMEditor;
typedef struct EMEditorDoc EMEditorDoc;
typedef struct EMEditorSubDoc EMEditorSubDoc;
typedef struct EMMapLayerType EMMapLayerType;
typedef struct StashTableImp *StashTable;
typedef struct EditorObject EditorObject;
typedef struct ZoneMapLayer ZoneMapLayer;
typedef struct EditorObject EditorObject;
typedef struct WleSettingsRegionMngrRegion WleSettingsRegionMngrRegion;
typedef struct WleFilterList WleFilterList;
typedef struct EMInfoWinText EMInfoWinText;
typedef struct WorldVariableDef WorldVariableDef;
typedef struct GenesisRuntimeStatus GenesisRuntimeStatus;
typedef struct GlobalGAELayersEntry GlobalGAELayersEntry;
typedef struct GlobalGAELayerDef GlobalGAELayerDef;
typedef struct WorldEncounterProperties WorldEncounterProperties;

#endif

extern int gEditorTeamSize;

AUTO_ENUM;
typedef enum WleUINewMapType
{
	WLEUI_NEW_INDOOR_MAP,			ENAMES(Indoor)
	WLEUI_NEW_OUTDOOR_MAP,			ENAMES(Outdoor)
	WLEUI_NEW_GENESIS_MAP,			ENAMES(Genesis)
} WleUINewMapType;

AUTO_ENUM;
typedef enum WleUITrackerFilter
{
	WLEUI_FILTER_HIDDEN,			ENAMES(Hidden)
	WLEUI_FILTER_FROZEN,			ENAMES(Frozen)
} WleUITrackerFilter;

#ifndef NO_EDITORS

typedef enum
{
	WleAllVols			= 1,
	WleOcclusionVol		= 1 << 1,
	WleAudioVol			= 1 << 2,
	WleSkyfadeVol		= 1 << 3,
} WleVolumeType;

typedef struct AttribUIState
{
	TrackerHandle *selected;
	bool changed;
	StashTable widgets;

	UISkin *skinBlue, *skinGreen, *skinRed;
} AttribUIState;

#endif
AUTO_STRUCT;
typedef struct WleSettingsRegionMngrRegion
{
	char *regionName;

	char **sky_names;	NO_AST

	bool tint;
	int argb;
} WleSettingsRegionMngrRegion;

AUTO_STRUCT;
typedef struct WleSettingsRegionMngr
{
	WleSettingsRegionMngrRegion **regions;	AST(NAME(Regions),DEFAULT(NULL))
} WleSettingsRegionMngr;
#ifndef NO_EDITORS

typedef struct VariableDefPropertiesEntry
{
	int index;

	UILabel *nameLabel;
	UILabel *typeLabel;
	UILabel *defaultLabel;
	UILabel *valueLabel;
	UILabel *choiceTableLabel;
	UILabel *choiceNameLabel;
	UILabel *choiceIndexLabel;
	UILabel *expressionLabel;
	UILabel *activityNameLabel;
	UILabel *activityVariableNameLabel;
	UILabel *activityDefaultValueLabel;
	UILabel *mapNameLabel;		//for map point vars
	UILabel *spawnPointLabel;	//for map point vars

	UICheckButton *removeButton;
	UITextEntry *nameEntry;
	UIComboBox *typeCombo;
	UIComboBox *defaultValueCombo;
	UICheckButton *isPublicButton;

	UITextEntry *varSimpleValue;
	UITextEntry *varAnimValue;
	UITextEntry *varCritterDefValue;
	UITextEntry *varCritterGroupValue;
	UIMessageEntry *varMessageValue;
	UITextEntry *varItemDefValue;
	UITextEntry *varMissionDefValue;
	UITextEntry *varChoiceTableValue;
	UITextEntry *varChoiceNameValue;
	UITextEntry *varMapNameValue;
	UISpinnerEntry *varChoiceIndexValue;
	UIExpressionEntry *varExpressionValue;
	UITextEntry *varActivityNameValue;
	UITextEntry *varActivityVariableNameValue;

	char** choiceTableNames;
} VariableDefPropertiesEntry;


// Global GAE Layers Entry
typedef struct GlobalGAELayersEntry
{
	int index;

	UIButton *removeButton;
	
	UIComboBox *gaeLayerCombo;
} GlobalGAELayersEntry;

typedef struct WorldEditorUIState
{
	struct
	{
		EMPanel *panel;
		UITree *objectTree;
		UIMenu *objectTreeRClickMenu;
	} objectTreeUI;

	struct 
	{
		EMPanel *trackerTreePanel;
		UITree *trackerTree;
		UITreeRefreshNode *topRefreshNode;
		UITreeRefreshNode **currRefreshNode;
		UITreeRefreshNode *logicalTreeRefreshNode;
		UIMenu *rightClickMenu;
		EditorObject **selection;
		EditorObject *allSelect;
		UIComboBox *editing;
		UITreeNode *lastSelected;

		UIExpanderGroup *panelExpanderGroup;
		UIExpander *panelToggles;
		UIList *validPanels;
		UIComboBox *panelMode;
		UIButton *panelAdd;

		TrackerHandle *activeScopeTracker;

		// skinning
		UISkin *trackerTreeSkin;
		REF_TO(UIStyleFont) fontNormalLocked;
		REF_TO(UIStyleFont) fontNormalUnlocked;
		REF_TO(UIStyleFont) fontSelected;
		REF_TO(UIStyleFont) fontFrozen;
		REF_TO(UIStyleFont) fontPrivateLocked;
		REF_TO(UIStyleFont) fontLibraryLocked;
		REF_TO(UIStyleFont) fontPrivateUnlocked;
		REF_TO(UIStyleFont) fontLibraryUnlocked;
		REF_TO(UIStyleFont) fontUniqueName;
	} trackerTreeUI;

	struct 
	{
		EMPanel *attribViewPanel;
		UIComboBox *attribSelecter;
		UIList *attribList;
		UIMenu *context_menu;
	} attribViewUI;

	struct
	{
		EMPanel *searchPanel;
		UITreeIterator *searchIterator;
		UIComboBox *searchFilterCombo;
		UICheckButton *selectCheck;
		UICheckButton *uniqueCheck;

		StashTable uniqueObjs;
	} trackerSearchUI;

	struct 
	{
		EMPanel *mapPanel;
		UILabel *mapPathLabel;
		UITextEntry *publicNameEntry;
		UIMessageEntry *displayNameEntry;
		UIComboBox *mapTypeCombo;
		UITextEntry *DefaultQueueEntry;
		UIComboBox *DefaultPVPGameTypeCombo;
		UITextEntry *mapLevelEntry;
		UITextEntry *mapDifficultyEntry;
		UITextEntry *mapForceTeamSizeEntry;
		UITextEntry *privateToEntry;
		char **privateToList;
		UITextEntry *parentMapEntry;
		UITextEntry *parentMapSpawnEntry;
		UITextEntry *startSpawnEntry;
		UIComboBox *rewardTableCombo;
		UITextEntry *playerRewardTableEntry;
		UIComboBox *respawnTypeCombo;
		UITextEntry *respawnWaveTimeEntry;
		UITextEntry *respawnMinTimeEntry;
		UITextEntry *respawnMaxTimeEntry;
		UITextEntry *respawnIncrTimeEntry;
		UITextEntry *respawnAttrTimeEntry;
		UIExpressionEntry *requiresExprEntry;
		UIExpressionEntry *permissionExprEntry;
		UITextEntry *requiredClassCategorySetEntry;
		UITextEntry *mastermindDefEntry;
		UITextEntry *civilianMapDefEntry;
		UICheckButton *disableVisitedTrackingButton;
		UICheckButton *ignoreTeamSizeBonusXPButton;
		UICheckButton *usedInUgcButton;
	} mapPropertiesUI;

	struct 
	{
		EMPanel *variablePanel;
		VariableDefPropertiesEntry **entries;
		UIButton *addButton;
	} variablePropertiesUI;

	struct
	{
		UICheckButton *collectDoorStatusCheck;
		UICheckButton *shardVariablesCheck;
		UICheckButton *duelsCheck;
		UICheckButton *powersRequireValidTargetCheck;
		UICheckButton *disableInstanceChangeCheck;
		UICheckButton *unteamedOwnedMapCheck;
		UICheckButton *guildOwnedCheck;
		UICheckButton *guildNotRequiredCheck;
		UICheckButton *terrainStaticLightingCheck;
		UICheckButton *recordPlayerMatchStats;
		UICheckButton *enableUpsellFeatures;
	} miscPropertiesUI;

	struct 
	{
		EMPanel *globalGAELayersPanel;
		GlobalGAELayersEntry **entries;
		UIButton *addButton;
		UIComboBox *selectedGAELayer;
		const char **gaeLayerChoices;
	} globalGAELayersUI;


	struct
	{
		EMPanel *panel;
		UITree *tree;
		UIList *sky_list;
		UIMenu *rclickMenu;
		UITextEntry *maxPetsEntry;
		UITextEntry *vehicleRulesEntry;
		UIButton *cubemapOverrideButton;
		UICheckButton *clusterWorldGeoButton;
		UICheckButton *indoorLightingButton;
		UIRadioButtonGroup *typeRadioGroup;
		WleSettingsRegionMngr *regionMngr;
	} regionMngrUI;

	struct
	{
		EMPanel *panel;
		UITree *tree;
		UITreeNode *groupFileNode;
		UIMenu *rclickMenu;
	} lockMngrUI;

	struct 
	{
		UIButton *editOrigButton;
		UIButton *lockButton;
		UIButton *marqueeCrossingButton;
		UIComboBox *marqueeFilterCombo;
		UIComboBox *gizmoModeCombo;
		UIButton *scratchLayerToggleButton;
		UIButton *groupTreeButton;
		UIButton *logicalTreeButton;
		UIButton *groupNamesButton;
		UIButton *logicalNamesButton;
	} toolbarUI;

	UIWindow *currModalWin;
	UIMenu *snapModeMenu, *rightClickMenu;

	UIWindow *closingFilesWin;
	ZoneMapLayer **closingLayers;

	GroupDefLockedFile **lockedObjLib;

	UISkin *skinBlue, *skinGreen, *skinRed;

	EditLibGizmosToolbar *gizmosToolbar;

	char *name;

	bool showHiddenLibs;
	bool disableVolColl;
	bool lockMsgShown;
	bool showingScratchLayer;

	bool showingLogicalTree;
	bool showingLogicalNames;

	WleFilterList *searchFilters;


} WorldEditorUIState;

/********************
* UTIL
********************/
void wleUISelectionLockWarn(void);
void wleUIFocusCameraOnEdObjs(EditorObject **edObjs);

/********************
* TOOLBAR
********************/
void wleToolbarEditOrigRefresh(void);
void wleToolbarSelectionLockRefresh(void);
void wleToolbarMarqueeCrossingRefresh(void);
void wleToolbarToggleEditingScratchLayer(void);
void wleToolbarUpdateEditingScratchLayer(ZoneMapLayer *layer);

/********************
* TRACKER TREE
********************/
SA_RET_OP_VALID const char *wleUIEdObjGetDisplayText(SA_PARAM_OP_VALID EditorObject *object);
void wleUITrackerTreeRefresh(UITreeRefreshNode *refreshNode);
bool wleUITrackerNodeFilterCheck(SA_PARAM_NN_VALID TrackerHandle *handle);
void wleUITrackerTreeHighlightEdObj(SA_PARAM_OP_VALID EditorObject *edObj);
void wleUITrackerTreeExpandToEdObj(SA_PARAM_OP_VALID EditorObject *edObj);
SA_RET_OP_VALID UITreeNode *wleUITrackerTreeGetNodeForEdObj(SA_PARAM_OP_VALID EditorObject *edObj);
SA_RET_OP_VALID EditorObject *wleUITrackerTreeGetSelectedEdObj(void);
void wleUITrackerTreeCenterOnEdObj(EditorObject *edObj);
void wleUISearchClearUniqueness(void);

/********************
* Attribute Panel
********************/
void wleUIAttribViewPanelRefresh(void);

/********************
* REGION MANAGER
********************/
void wleUIRegionMngrRefresh(void);
void wleUIRegionMngrSettingsInit(void);

/********************
* MAP PROPERTIES
********************/
void wleUIMapPropertiesRefresh(void);
EncounterDifficulty wleGetEncounterDifficulty(WorldEncounterProperties *pEncounter);

/********************
* VARIABLE PROPERTIES
********************/
void wleUIVariablePropertiesRefresh(void);

/********************
* MISC PROPERTIES
********************/
void wleUIMiscPropertiesRefresh(void);

/********************
* GLOBAL GAE LAYERS
********************/
void wleUIGlobalGAELayersRefresh(void);
void wleUISelectGAELayer(UIAnyWidget *ui_widget, UserData user_data);
void wleUIRemoveGAELayer(UIAnyWidget *ui_widget, UserData user_data);
void wleUISetupGlobalGAELayers(void);
void wleUIReloadGlobalGAELayers(void);
void wleUIReloadGlobalGAELayersCB(const char *relPath, int when, void *userData);

/********************
* GENESIS PANEL
********************/
void wleGenesisUITick(void);

/********************
* FILE LIST
********************/
void wleUIFileListRefresh(void);

/********************
* INFO WINDOW
********************/
void wleUIRegisterInfoWinEntries(SA_PARAM_NN_VALID EMEditor *editor);

/********************
* LOCK MANAGER
********************/
void wleUILockLayerWrapper(ZoneMapLayer *layer);
void wleUIUnlockLayerWrapper(ZoneMapLayer *layer);
void wleUILockGroupFileWrapper(GroupDefLockedFile *gfile);
void wleUILockMngrRefresh(void);

/********************
* LAYER UI'S
********************/
void wleUILayerContextMenu(EditorObject *edObj, UIMenuItem ***outItems);

void wleUIToolbarInit(void);
void wleUIDraw(void);
void wleUIDrawCompass(void);
void wleUIInit(EMEditorDoc *doc);
void wleUIOncePerFrame(void);
void wleUIEdObjRenameDialog(EditorObject *edObj);

// menu callbacks
bool wleUIVolumeTypeHideCheck(UserData unused);

// view confirm prompt
void wleUIViewConfirmDialogCreate(ZoneMapLayer *layer);

// auto-lock prompt
void wleUILockPrompt(SA_PARAM_NN_STR const char *filename);

// find and replace dialog
void wleUIFindAndReplaceDialogCreate(void);

// misc
void worldEditorUICenterTrackerTreeOnSelect(void);
void wleUIDeleteFromLibError(GroupDef *refDef, GroupDef **containingDefs);
void wleUIDeleteFromLibConfirm(GroupDef *def);

void wleUIMapLayersRefresh(void);

void wleUISetLayerModeEx(ZoneMapLayer *layer, ZoneMapLayerMode mode, bool closing, bool force);
#define wleUISetLayerMode(layer, mode, closing) wleUISetLayerModeEx(layer, mode, closing, false)

bool wleIsScratchVisible();
ZoneMapLayer *wleGetScratchLayer(bool make);


/********************
* DIALOGS
********************/
// ZoneMap dialogs
void wleUINewZoneMapDialogCreate(void);
void wleUIOpenZoneMapDialogCreate(void);
void wleUISaveZoneMapAsDialogCreate(void);

// layer dialogs
void wleUINewLayerDialog();
void wleUIImportLayerDialogCreate(void);
void wleUIDeleteLayerDialogCreate(SA_PARAM_NN_VALID ZoneMapLayer *layer);

// miscellaneous dialogs and windows
void worldEditorOpenDocumentation(UIMenuItem *menuItem, UserData *stuff);
void wleUISaveToLibDialogCreate(GroupTracker **trackers);
void wleUIViewMenuVolumeToggle(UIMenuItem *menuItem, void *volumeType);
void wleUIInfoWinMaterial(const char *indexed_name, EMInfoWinText ***text_lines);
void wleGenesisDisplayErrorDialog(GenesisRuntimeStatus *gen_status);

typedef bool (*WleUIUniqueNameOkCallback)(const char *uniqueName, void *data);
typedef void (*WleUIUniqueNameCancelCallback)(void *data);
void wleUIUniqueNameRequiredDialog(SA_PARAM_OP_STR const char *prompt, SA_PARAM_OP_STR const char *initialName, SA_PARAM_OP_VALID WleUIUniqueNameOkCallback okCallback, void *okData, SA_PARAM_OP_VALID WleUIUniqueNameCancelCallback cancelCallback, void *cancelData);

void wleUIGizmoModeEnable(bool active);
void wleUIGizmoModeRefresh(void);
void wleUIRefreshEditingSelector(EditorObject **edObjList);

extern WorldEditorUIState *editorUIState;

#endif // NO_EDITORS

#endif // __WORLDEDITORUI_H__
